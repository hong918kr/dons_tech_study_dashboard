/* DS-A08 B+ 트리: 페이지 단위 인덱스
 *
 * 노드 = 4 KB 페이지. 페이지는 호출자가 준 정적 풀(page_t 배열)에서만 나온다 (malloc 없음).
 * 페이지끼리의 연결은 raw pointer 가 아니라 **페이지 번호(page id)** 다 → 풀을 통째로 다른 주소로 복사해도 그대로 동작.
 *   Part A  검색 / 삽입(리프 분할 = copy up, 내부 분할 = push up) / 범위 스캔 / 풀 고갈 사전 점검
 *   Part B  삭제(왼쪽 → 오른쪽 재분배, 병합, 루트 축소) + 불변식 검사기 + 고장 주입으로 검사기 검증 + 무작위 스트레스
 *   Part C  LBA → (PBA, len) 익스텐트 맵: floor 질의, 덮어쓰기(쪼개기·잘라내기), 순차 쓰기 합치기
 *
 * 규약 (shape 체크가 이 규약을 그대로 고정한다)
 *   키 u32 / 값 u32, 중복 키 없음(insert 는 갱신). child[i] 의 키 범위 = [key[i-1], key[i]) (i==0 은 왼쪽 끝 열림).
 *   리프 분할: 합쳐서 cap+1 개 → 왼쪽 ceil((cap+1)/2) 개, 구분키 = 오른쪽 첫 키 (복사, 리프에 남음).
 *   내부 분할: cap+1 개 키 중 인덱스 (cap+1)/2 를 위로 올림 (이동, 내부에 안 남음).
 *   삭제: 왼쪽 형제에서 1칸 빌리기 → 오른쪽 형제에서 1칸 빌리기 → 왼쪽과 병합 → 오른쪽과 병합 순서.
 *   최소 채움: 리프 ceil(cap/2), 내부 ceil((cap+1)/2)-1 키. 루트는 예외.
 *
 * leaf_cap / node_cap 은 실행 시 인자라 cap=4 로 분할·병합을 다 보고, cap=510 으로 실제 4 KB 팬아웃을 돌린다.
 * 체크는 결정적인 것만. 시간 측정과 높이·채움률 표는 demo_measure.c / demo_bench.c.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)
#define MAYBE_UNUSED __attribute__((unused))

/* ---------- 페이지 형식 ---------- */
#define PAGE_SIZE 4096u
#define PID_NIL   0u        /* 페이지 0 은 할당하지 않는다 → 0 으로 초기화된 링크 = "없음" */
#define BT_MAX_H  16u
#define LEAF_MAX  510u      /* (4096-16)/8 */
#define NODE_MAX  509u      /* 키 509 + 자식 510 */

enum { PK_FREE = 0x4652, PK_LEAF = 0x4C46, PK_NODE = 0x4E44 };

typedef struct {
    uint16_t kind;
    uint16_t n;        /* 키 개수 */
    uint32_t next;     /* 리프: 오른쪽 형제 / free: 다음 free 페이지 */
    uint32_t prev;     /* 리프: 왼쪽 형제 */
    uint32_t stamp;    /* 런타임 전용: (op << 1) | dirty — 연산당 접촉 페이지 수 세기. 실제 포맷이면 버퍼 풀 프레임에 둔다 */
} pg_hdr_t;

typedef union {
    uint8_t  raw[PAGE_SIZE];
    pg_hdr_t h;
    struct { pg_hdr_t h; uint32_t key[LEAF_MAX]; uint32_t val[LEAF_MAX]; } leaf;
    struct { pg_hdr_t h; uint32_t key[NODE_MAX]; uint32_t child[NODE_MAX + 1u]; } node;
} page_t;

_Static_assert(sizeof(pg_hdr_t) == 16u, "header layout");
_Static_assert(sizeof(page_t) == PAGE_SIZE, "a node is exactly one page");
_Static_assert(offsetof(page_t, leaf.key) == 16u && offsetof(page_t, node.key) == 16u, "keys follow the header");

typedef struct {
    page_t  *pg;                    /* 호출자의 풀 */
    uint32_t npages;
    uint32_t leaf_cap, node_cap;    /* 최대 키 수 */
    uint32_t leaf_min, node_min;    /* 비루트 최소 키 수 */
    uint32_t root, height;          /* height 1 = 루트가 곧 리프 */
    uint32_t free_head, free_cnt;
    uint32_t nkeys;
    uint32_t op;                    /* 공개 연산마다 +1 (접촉 페이지 중복 제거용) */
    uint64_t rd, wr;                /* 연산마다 서로 다른 페이지 기준: 읽은 페이지 수 / 고친 페이지 수 */
    uint32_t bad_id;                /* 범위 밖 페이지 번호 접근 횟수 (0 이어야 정상) */
    uint32_t split_leaf, split_node, merge_leaf, merge_node, redist_l, redist_r, collapses;
} bt_t;

enum { BT_NEW = 1, BT_UPD = 2, BT_NOSPACE = -1 };
enum { BTC_TODO = -1, BTC_OK = 0, BTC_KIND, BTC_ORDER, BTC_FILL, BTC_DEPTH, BTC_SEP, BTC_CHAIN, BTC_COUNT, BTC_PAGES };

typedef struct { uint32_t pid, idx; } step_t;   /* 경로 한 칸: 내부 페이지와, 거기서 내려간 자식 인덱스 */

/* ---------- 주어지는 도우미 ---------- */
/* 페이지 접근은 전부 이 둘을 거친다. 범위 밖 번호는 페이지 0(항상 비어 있음)으로 돌려 크래시 대신 체크에서 잡히게 한다. */
static page_t *pg_r(bt_t *t, uint32_t id) {
    if (id == PID_NIL || id >= t->npages) { t->bad_id++; return &t->pg[0]; }
    page_t *p = &t->pg[id];
    if ((p->h.stamp >> 1) != t->op) { p->h.stamp = t->op << 1; t->rd++; }
    return p;
}
MAYBE_UNUSED static page_t *pg_w(bt_t *t, uint32_t id) {
    page_t *p = pg_r(t, id);
    if (p != &t->pg[0] && !(p->h.stamp & 1u)) { p->h.stamp |= 1u; t->wr++; }
    return p;
}
MAYBE_UNUSED static uint32_t pg_alloc(bt_t *t, uint16_t kind) {
    uint32_t id = t->free_head;
    if (id == PID_NIL) return PID_NIL;
    page_t *p = &t->pg[id];
    t->free_head = p->h.next;
    t->free_cnt--;
    p->h.kind = kind;
    p->h.n = 0;
    p->h.next = p->h.prev = PID_NIL;
    p->h.stamp = (t->op << 1) | 1u;     /* 새 페이지: 읽기 없이 쓰기 1 */
    t->wr++;
    return id;
}
MAYBE_UNUSED static void pg_free(bt_t *t, uint32_t id) {
    page_t *p = &t->pg[id];
    p->h.kind = PK_FREE;
    p->h.n = 0;
    p->h.prev = PID_NIL;
    p->h.next = t->free_head;
    t->free_head = id;
    t->free_cnt++;
}
MAYBE_UNUSED static void arr_ins(uint32_t *a, uint32_t n, uint32_t i, uint32_t v) {
    memmove(a + i + 1u, a + i, (n - i) * sizeof *a);
    a[i] = v;
}
MAYBE_UNUSED static void arr_del(uint32_t *a, uint32_t n, uint32_t i) {
    memmove(a + i, a + i + 1u, (n - i - 1u) * sizeof *a);
}
/* 첫 번째 a[i] >= key 의 i (없으면 n) */
MAYBE_UNUSED static uint32_t lb_u32(const uint32_t *a, uint32_t n, uint32_t key) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) { uint32_t m = lo + (hi - lo) / 2u; if (a[m] < key) lo = m + 1u; else hi = m; }
    return lo;
}
/* 첫 번째 a[i] > key 의 i (없으면 n) = 내려갈 자식 인덱스 */
MAYBE_UNUSED static uint32_t ub_u32(const uint32_t *a, uint32_t n, uint32_t key) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) { uint32_t m = lo + (hi - lo) / 2u; if (a[m] <= key) lo = m + 1u; else hi = m; }
    return lo;
}

static void bt_init(bt_t *t, page_t *pool, uint32_t npages, uint32_t leaf_cap, uint32_t node_cap) {
    memset(t, 0, sizeof *t);
    memset(pool, 0, (size_t)npages * sizeof(page_t));
    t->pg = pool;
    t->npages = npages;
    t->leaf_cap = leaf_cap;
    t->node_cap = node_cap;
    t->leaf_min = (leaf_cap + 1u) / 2u;
    t->node_min = (node_cap + 2u) / 2u - 1u;   /* ceil((cap+1)/2) - 1 */
    for (uint32_t i = npages - 1u; i >= 1u; i--) {
        pool[i].h.kind = PK_FREE;
        pool[i].h.next = t->free_head;
        t->free_head = i;
        t->free_cnt++;
    }
    t->root = pg_alloc(t, PK_LEAF);
    t->height = 1u;
}

/* ======================= Part A — 검색 · 삽입 · 범위 스캔 ======================= */

/* key 가 있어야 할 리프까지 내려간다. path 가 NULL 이 아니면 path[d] = {내부 페이지, 내려간 자식 인덱스} 기록.
 * 리프 번호를 반환. 내부 페이지에서 자식 인덱스 = ub_u32(key[], n, key) */
MAYBE_UNUSED static uint32_t bt_descend(bt_t *t, uint32_t key, step_t *path) {
    /* TODO: 루트에서 height-1 번 내려간다. 각 내부 페이지에서 ub_u32 로 자식을 고르고 path 를 기록 */
    (void)key; (void)path;
    return t->root;
}

static bool bt_search(bt_t *t, uint32_t key, uint32_t *val) {
    /* TODO: t->op++ 후 리프까지 내려가 lb_u32 로 찾는다. 있으면 *val (NULL 허용) 채우고 true */
    (void)t; (void)key; (void)val;
    return false;
}

/* 꽉 찬 리프 lid 에 (key,val) 을 위치 i 로 넣으면서 분할. 새 오른쪽 리프 번호 반환, *sep = 오른쪽 첫 키.
 * 임시 배열 없이: 왼쪽 목표 L = ceil((cap+1)/2). i < L 이면 옛 [L-1..] 을 오른쪽으로, 아니면 옛 [L..] 을 오른쪽으로.
 * 리프 사슬(next/prev)도 여기서 고친다. */
MAYBE_UNUSED static uint32_t leaf_split_insert(bt_t *t, uint32_t lid, uint32_t i, uint32_t key, uint32_t val, uint32_t *sep) {
    /* TODO: pg_alloc(PK_LEAF) → 키·값 절반 이동 → 맞는 쪽에 삽입 → next/prev 연결 (옆 리프의 prev 도!) → *sep = 오른쪽 첫 키 */
    (void)t; (void)lid; (void)i; (void)key; (void)val; (void)sep;
    return PID_NIL;
}

/* 꽉 찬 내부 페이지 nid 의 위치 p 에 (ins_key, 오른쪽 자식 ins_child) 를 넣으면서 분할. 새 오른쪽 페이지 번호 반환, *up = 위로 올릴 키.
 * 개념상 cap+1 개 키 K' 에서 인덱스 mid = (cap+1)/2 가 올라간다: 왼쪽 K'[0..mid-1], 오른쪽 K'[mid+1..cap].
 * 임시 배열 없이 p < mid / p == mid / p > mid 세 경우로 나눈다. */
MAYBE_UNUSED static uint32_t node_split_insert(bt_t *t, uint32_t nid, uint32_t p, uint32_t ins_key, uint32_t ins_child, uint32_t *up) {
    /* TODO: 세 경우. 올라가는 키는 p<mid 면 옛 key[mid-1], p==mid 면 ins_key, p>mid 면 옛 key[mid] */
    (void)t; (void)nid; (void)p; (void)ins_key; (void)ins_child; (void)up;
    return PID_NIL;
}

/* 반환: BT_NEW(새 키) / BT_UPD(기존 키 값 갱신) / BT_NOSPACE(풀 부족 — 트리는 전혀 바뀌지 않아야 한다) */
static int bt_insert(bt_t *t, uint32_t key, uint32_t val) {
    /* TODO: ① descend ② 있으면 갱신(BT_UPD) ③ 자리가 있으면 삽입 ④ 분할이 몇 층 번지는지 세어 free_cnt 사전 점검
     *       ⑤ 리프 분할 후 경로를 거슬러 올라가며 (up, rid) 삽입, 꽉 찼으면 node_split_insert ⑥ 루트까지 넘치면 새 루트 */
    (void)t; (void)key; (void)val;
    return BT_NOSPACE;
}

/* [lo, hi] (양끝 포함) 를 키 오름차순으로 최대 max 개 복사. 복사한 개수 반환. 리프 사슬(next)로 옆 페이지로 넘어간다. */
static uint32_t bt_scan(bt_t *t, uint32_t lo, uint32_t hi, uint32_t *ks, uint32_t *vs, uint32_t max) {
    /* TODO: lo 의 리프로 내려가 lb_u32 위치부터 복사, 페이지 끝이면 next 로. key > hi 또는 cnt == max 이면 끝 */
    (void)t; (void)lo; (void)hi; (void)ks; (void)vs; (void)max;
    return 0u;
}

/* ======================= Part B — 삭제 · 불변식 검사기 ======================= */

/* 리프 lid 가 최소 미만이 됐다. s = 부모 경로 칸. 재분배로 끝나면 false, 병합해서 부모가 키 1개를 잃으면 true.
 * 빌리기: 왼쪽 먼저, 그다음 오른쪽. 병합: 왼쪽 형제가 있으면 왼쪽으로, 없으면 오른쪽을 자기에게. 구분키·리프 사슬 갱신 잊지 말 것. */
MAYBE_UNUSED static bool leaf_fix(bt_t *t, const step_t *s, uint32_t lid) {
    /* TODO: 왼쪽에 여유(n > leaf_min)면 맨 끝 1칸 가져오고 구분키 = 내 첫 키 / 오른쪽에 여유면 맨 앞 1칸 가져오고 구분키 = 오른쪽의 새 첫 키 /
     *       둘 다 없으면 병합 (왼쪽 형제 우선). 사슬(next/prev) 고치고 부모에서 키·자식 제거, 빈 페이지 pg_free */
    (void)t; (void)s; (void)lid;
    return false;
}

/* 내부 페이지 nid 가 최소 미만. s = 부모 경로 칸. 재분배면 false, 병합이면 true.
 * 내부 페이지 빌리기는 부모를 통한 회전: 구분키가 내려오고 형제의 끝 키가 올라간다. 병합은 구분키가 내려와 가운데에 낀다. */
MAYBE_UNUSED static bool node_fix(bt_t *t, const step_t *s, uint32_t nid) {
    /* TODO: leaf_fix 와 같은 순서. 빌리기 = 부모 구분키가 내려오고 형제 끝 키가 올라감 + 자식 1개 이동. 병합 = 왼쪽 + 구분키 + 오른쪽 */
    (void)t; (void)s; (void)nid;
    return false;
}

/* 삭제 후 리프 lid 가 최소 미만일 때: leaf_fix → 병합이 났으면 부모로 올라가며 node_fix 반복. 루트가 키 0개가 되면 축소. */
MAYBE_UNUSED static void rebalance(bt_t *t, const step_t *path, uint32_t lid) {
    /* TODO: d = height-2 에서 leaf_fix. true 면 부모 nid = path[d].pid 를 본다: d==0 이면 루트 (n==0 이면 자식으로 축소, height--),
     *       아니면 n >= node_min 이면 끝, 모자라면 d-- 후 node_fix */
    (void)t; (void)path; (void)lid;
}

/* 있으면 지우고 true, 없으면 false. 첫 키를 지워도 부모 구분키는 고치지 않는다 (구분키는 경계일 뿐). */
static bool bt_delete(bt_t *t, uint32_t key) {
    /* TODO: descend → 찾기 → 리프에서 제거 → nkeys-- → 루트가 아니고 최소 미만이면 rebalance */
    (void)t; (void)key;
    return false;
}

/* ---- 불변식 검사기: 페이지를 직접 읽는다 (접촉 카운터 영향 없음). 검사기도 구현 과제다 — 고장 주입 테스트가 검사기를 채점한다. ---- */
#define CHK_MAX_PAGES (1u << 17)
MAYBE_UNUSED static uint8_t g_chk_seen[CHK_MAX_PAGES];
typedef struct {
    const bt_t *t;
    uint32_t prev_leaf, pages;
    uint64_t keys;
    int err;
} chk_t;

/* 페이지 id, 깊이 depth(루트=1), 허용 키 범위 [lo, hi) 를 검사. 첫 위반을 c->err 에 기록하고 멈춘다.
 * 볼 것: 번호 범위·중복 방문(PAGES), kind(KIND/DEPTH: 리프는 depth==height 에만), 채움(FILL, 루트 예외), 엄격 정렬(ORDER),
 *        키가 [lo,hi) 안(SEP), 자식 i 의 범위 = [key[i-1], key[i]), 리프 순서 = 사슬 next/prev (CHAIN). */
MAYBE_UNUSED static void chk_rec(chk_t *c, uint32_t id, uint32_t depth, uint64_t lo, uint64_t hi) {
    /* TODO: 위 주석의 항목을 차례로 검사하고 내부 페이지면 자식마다 재귀 (깊이는 height 로 유계) */
    (void)c; (void)id; (void)depth; (void)lo; (void)hi;
}

/* BTC_OK 또는 첫 위반의 코드. 추가로: 리프 수 끝의 next == NIL(CHAIN), 총 키 수 == nkeys(COUNT), 페이지 회계: 도달 페이지 + free 사슬 + 페이지 0 == npages (PAGES) */
static int bt_check(const bt_t *t) {
    /* TODO: g_chk_seen 을 npages 만큼 지우고 chk_rec(root, 1, 0, 2^32) → 마지막 리프 next == NIL → 키 수 → free 사슬 회계 */
    (void)t;
    return BTC_TODO;
}

/* ======================= Part C — LBA 익스텐트 맵 ======================= */

/* key 이하에서 가장 큰 키를 찾는다 (floor). 있으면 true. 함정: key 의 리프에 key 이하가 하나도 없을 수 있다
 * (그 리프의 첫 키가 지워졌는데 구분키는 그대로일 때) → prev 사슬로 왼쪽 리프의 마지막 키. */
static bool bt_floor(bt_t *t, uint32_t key, uint32_t *k_out, uint32_t *v_out) {
    /* TODO: 리프로 내려가 ub_u32 → 0 이면 prev 리프(없으면 false)의 마지막 키, 아니면 [i-1] */
    (void)t; (void)key; (void)k_out; (void)v_out;
    return false;
}

#define EM_LEN_MAX 255u
#define EM_NONE 0xFFFFFFFFu
MAYBE_UNUSED static uint32_t em_pack(uint32_t pba, uint32_t len) { return (pba << 8) | len; }
MAYBE_UNUSED static uint32_t em_pba(uint32_t v) { return v >> 8; }
MAYBE_UNUSED static uint32_t em_len(uint32_t v) { return v & 0xFFu; }

/* lba 를 덮는 익스텐트가 있으면 PBA, 없으면 EM_NONE */
static uint32_t em_lookup(bt_t *t, uint32_t lba) {
    /* TODO: floor 로 시작이 lba 이하인 익스텐트를 찾아 범위 안이면 pba + (lba - start) */
    (void)t; (void)lba;
    return EM_NONE;
}

/* [lba, lba+len) 의 매핑을 없앤다. 걸친 익스텐트는 잘라내고, 가운데가 뚫리면 앞·뒤 둘로 쪼갠다. */
MAYBE_UNUSED static void em_trim(bt_t *t, uint32_t lba, uint32_t len) {
    /* TODO: ① floor 로 a 를 걸치는 익스텐트를 앞부분만 남기고 (end > b 면 꼬리도 새로 넣고 끝)
     *       ② [a, b) 안에서 시작하는 익스텐트를 하나씩 지우되, b 를 넘는 꼬리는 다시 넣는다 */
    (void)t; (void)lba; (void)len;
}

/* [lba, lba+len) → [pba, pba+len) 을 기록 (기존 매핑 덮어씀). 바로 앞 익스텐트와 LBA·PBA 모두 이어지면 합친다 (합친 길이 ≤ 255). */
static int em_write(bt_t *t, uint32_t lba, uint32_t len, uint32_t pba) {
    /* TODO: em_trim → 앞 익스텐트와 합칠 수 있으면 길이만 늘려 갱신, 아니면 새로 삽입 */
    (void)t; (void)lba; (void)len; (void)pba;
    return BT_NOSPACE;
}

/* ======================= 테스트 하네스 ======================= */
#define POOL_PAGES 8192u
#define KS_MAX (1u << 20)
_Alignas(4096) static page_t g_pool[POOL_PAGES];
_Alignas(4096) static page_t g_snap[POOL_PAGES];
static uint32_t g_ref[KS_MAX];              /* 참조 모델: g_ref[k] = 값 (0 = 없음) */
static uint32_t g_ks[KS_MAX], g_vs[KS_MAX];

static uint32_t g_rng;
static uint32_t rnd(void) {                 /* xorshift32, 고정 시드 */
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

/* 구조를 문자열로: 내부 = (k k: 자식 ...), 리프 = [k k k] */
static int dump_rec(const bt_t *t, uint32_t id, uint32_t depth, char *b, int cap, int pos) {
    if (id == PID_NIL || id >= t->npages || depth > t->height) return pos + snprintf(b + pos, (size_t)(cap - pos), "?");
    const page_t *p = &t->pg[id];
    if (depth == t->height) {
        pos += snprintf(b + pos, (size_t)(cap - pos), "[");
        for (uint32_t i = 0; i < p->h.n && pos < cap - 16; i++)
            pos += snprintf(b + pos, (size_t)(cap - pos), i ? " %u" : "%u", p->leaf.key[i]);
        return pos + snprintf(b + pos, (size_t)(cap - pos), "]");
    }
    pos += snprintf(b + pos, (size_t)(cap - pos), "(");
    for (uint32_t i = 0; i < p->h.n && pos < cap - 16; i++)
        pos += snprintf(b + pos, (size_t)(cap - pos), i ? " %u" : "%u", p->node.key[i]);
    pos += snprintf(b + pos, (size_t)(cap - pos), ":");
    for (uint32_t i = 0; i <= p->h.n && pos < cap - 64; i++) {
        pos += snprintf(b + pos, (size_t)(cap - pos), " ");
        pos = dump_rec(t, p->node.child[i], depth + 1u, b, cap, pos);
    }
    return pos + snprintf(b + pos, (size_t)(cap - pos), ")");
}
static char g_shape[4096];
static const char *shape(const bt_t *t) { dump_rec(t, t->root, 1u, g_shape, (int)sizeof g_shape, 0); return g_shape; }
static bool shape_is(const bt_t *t, const char *want) {
    const char *got = shape(t);
    bool ok = strcmp(got, want) == 0;
    printf("  %s %s\n", ok ? "shape" : "shape MISMATCH got", got);
    if (!ok) printf("  wanted %s\n", want);
    return ok;
}

static bool tree_ok(const bt_t *t) { return bt_check(t) == BTC_OK && t->bad_id == 0u; }

static void fill_ascending(bt_t *t, uint32_t lo, uint32_t step, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) bt_insert(t, lo + i * step, lo + i * step + 1u);
}

/* 맨 왼쪽 리프 번호 (검사기 없이 child[0] 만 따라감) */
static uint32_t leftmost(const bt_t *t) {
    uint32_t id = t->root;
    for (uint32_t d = 1; d < t->height; d++) id = t->pg[id].node.child[0];
    return id;
}

static bool part_a_units(void) {
    bt_t t;
    uint32_t v = 0, ks[64], vs[64];
    bt_init(&t, g_pool, 64u, 4u, 4u);
    bool ok = true;
    ok &= !bt_search(&t, 1u, &v) && tree_ok(&t);
    CHECK(bt_insert(&t, 5u, 50u) == BT_NEW && bt_search(&t, 5u, &v) && v == 50u);
    CHECK(bt_insert(&t, 5u, 51u) == BT_UPD && bt_search(&t, 5u, &v) && v == 51u && t.nkeys == 1u);
    bt_init(&t, g_pool, 64u, 4u, 4u);
    fill_ascending(&t, 10u, 10u, 5u);
    CHECK(shape_is(&t, "(40: [10 20 30] [40 50])") && t.height == 2u && t.split_leaf == 1u && tree_ok(&t));
    bt_init(&t, g_pool, 64u, 4u, 4u);
    for (uint32_t k = 50u; k >= 10u; k -= 10u) bt_insert(&t, k, k + 1u);
    CHECK(shape_is(&t, "(40: [10 20 30] [40 50])") && tree_ok(&t));   /* 오른쪽에 못 넣는 경우(i<L)도 같은 모양 */
    bt_init(&t, g_pool, 64u, 4u, 4u);
    for (uint32_t k = 1u; k <= 7u; k++) ok &= bt_insert(&t, k * 2u, k) == BT_NEW && tree_ok(&t);
    ok &= bt_insert(&t, 7u, 99u) == BT_NEW && tree_ok(&t);   /* 중간 삽입 */
    CHECK(ok && t.nkeys == 8u && bt_search(&t, 7u, &v) && v == 99u);
    /* 범위 스캔: 페이지 경계를 넘고, 양끝 포함, 빈 구간 */
    uint32_t n = bt_scan(&t, 4u, 12u, ks, vs, 64u);
    CHECK(n == 6u && ks[0] == 4u && ks[1] == 6u && ks[2] == 7u && ks[3] == 8u && ks[5] == 12u && vs[2] == 99u);
    CHECK(bt_scan(&t, 5u, 5u, ks, vs, 64u) == 0u && bt_scan(&t, 8u, 8u, ks, vs, 64u) == 1u && bt_scan(&t, 9u, 3u, ks, vs, 64u) == 0u);
    CHECK(bt_scan(&t, 0u, 0xFFFFFFFFu, ks, vs, 3u) == 3u && ks[0] == 2u && ks[2] == 6u);
    return ok;
}

/* 높이 3 만들기: 내부 분할과 push-up 확인 */
static void part_a_height(void) {
    bt_t t;
    bt_init(&t, g_pool, 256u, 4u, 4u);
    bool ok = true;
    for (uint32_t k = 1u; k <= 40u; k++) { bt_insert(&t, k, k); ok &= tree_ok(&t); }
    uint32_t v;
    for (uint32_t k = 1u; k <= 40u; k++) ok &= bt_search(&t, k, &v) && v == k;
    CHECK(ok && t.split_node >= 1u && t.height == 3u);
    printf("  ascending 1..40 cap=4: %s\n", shape(&t));
    /* 내부 페이지 분할의 세 경우(p<mid, p==mid, p>mid)를 모두 지나가도록 섞어서 넣는다 */
    uint32_t cases_ok = 1;
    bt_init(&t, g_pool, 256u, 4u, 4u);
    for (uint32_t k = 0; k < 200u; k++) {
        uint32_t x = (k * 37u) % 200u;
        bt_insert(&t, x, x);
        cases_ok &= tree_ok(&t);
    }
    CHECK(cases_ok && t.nkeys == 200u && t.split_node >= 5u);
    printf("  height %u, leaf splits %u, internal splits %u\n", t.height, t.split_leaf, t.split_node);
    /* 큰 팬아웃: 4 KB 실제 용량 */
    bt_init(&t, g_pool, 64u, LEAF_MAX, NODE_MAX);
    for (uint32_t k = 0; k < LEAF_MAX; k++) bt_insert(&t, k, k + 1u);
    CHECK(t.height == 1u && t.free_cnt == 64u - 2u && tree_ok(&t));
    bt_insert(&t, LEAF_MAX, 1u);
    CHECK(t.height == 2u && t.free_cnt == 64u - 4u && tree_ok(&t));
    printf("  fanout: leaf %u keys, internal %u keys / %u children, page %zu B\n", LEAF_MAX, NODE_MAX, NODE_MAX + 1u, sizeof(page_t));
}

static void part_a_nospace_and_relocate(void) {
    bt_t t;
    uint32_t v;
    bt_init(&t, g_pool, 8u, 4u, 4u);   /* 사용 가능 7 페이지 */
    uint32_t k = 0;
    int rc = BT_NEW;
    while (rc != BT_NOSPACE && k < 2000u) { rc = bt_insert(&t, k * 3u, k + 1u); k++; }
    uint32_t last = (k - 1u) * 3u;
    bool found = bt_search(&t, last, &v);
    uint32_t keys_at_fail = t.nkeys, free_at_fail = t.free_cnt;
    CHECK(rc == BT_NOSPACE && k > 1u && !found && tree_ok(&t));
    bool all = true;
    for (uint32_t j = 0; j + 1u < k; j++) all &= bt_search(&t, j * 3u, &v) && v == j + 1u;
    CHECK(all && keys_at_fail == k - 1u && t.nkeys == keys_at_fail && t.free_cnt == free_at_fail);
    printf("  pool exhausted after %u keys, free pages left %u (need > free for this split), tree intact\n", keys_at_fail, free_at_fail);

    /* 위치 독립: 풀을 다른 주소로 통째로 복사해도 페이지 번호만 쓰므로 그대로 동작 */
    bt_init(&t, g_pool, 512u, 8u, 8u);
    bool built = true;
    for (uint32_t j = 0; j < 500u; j++) built &= bt_insert(&t, j * 7u, j + 1u) == BT_NEW;
    memcpy(g_snap, g_pool, 512u * sizeof(page_t));
    bt_t w = t;
    w.pg = g_snap;
    memset(g_pool, 0xA5, 512u * sizeof(page_t));   /* 원본은 쓰레기로 */
    bool rel = built && tree_ok(&w);
    for (uint32_t j = 0; j < 500u; j++) rel &= bt_search(&w, j * 7u, &v) && v == j + 1u;
    CHECK(rel && bt_insert(&w, 3u, 9u) == BT_NEW && tree_ok(&w));
}

/* 내부 페이지 재분배·병합을 눈으로 고정하는 시나리오들 (leaf cap 4, node cap 4) */
static void part_b_scenarios(bool *ok_out) {
    bt_t t;
    uint32_t v, k, val;
    bool ok = true;

#define FRESH(n) do { bt_init(&t, g_pool, 256u, 4u, 4u); fill_ascending(&t, 10u, 10u, (n)); } while (0)
    /* 재분배: 오른쪽에서 빌리기 */
    FRESH(6u); bt_delete(&t, 30u);
    ok &= shape_is(&t, "(40: [10 20] [40 50 60])");
    CHECK(bt_delete(&t, 10u) && shape_is(&t, "(50: [20 40] [50 60])") && t.redist_r == 1u && tree_ok(&t));
    /* 재분배: 왼쪽에서 빌리기 */
    FRESH(5u);
    CHECK(bt_delete(&t, 50u) && shape_is(&t, "(30: [10 20] [30 40])") && t.redist_l == 1u && tree_ok(&t));
    /* 양쪽 다 여유가 있으면 왼쪽 먼저 */
    FRESH(9u);
    bt_delete(&t, 50u);
    CHECK(bt_delete(&t, 40u) && shape_is(&t, "(30 70: [10 20] [30 60] [70 80 90])") && t.redist_l == 1u && t.redist_r == 0u && tree_ok(&t));
    /* 병합 왼쪽 + 루트 축소 (높이 2 → 1) */
    FRESH(5u); bt_delete(&t, 30u);
    CHECK(bt_delete(&t, 50u) && shape_is(&t, "[10 20 40]") && t.merge_leaf == 1u && t.collapses == 1u && t.height == 1u && t.free_cnt == 256u - 2u && tree_ok(&t));
    /* 왼쪽 형제가 없으면 오른쪽을 자기에게 병합 */
    FRESH(5u); bt_delete(&t, 30u);
    CHECK(bt_delete(&t, 10u) && shape_is(&t, "[20 40 50]") && t.merge_leaf == 1u && tree_ok(&t));
    /* 첫 키를 지워도 구분키 40 은 그대로 (리프에 없는 값이 구분키로 남는다) */
    FRESH(6u);
    CHECK(bt_delete(&t, 40u) && shape_is(&t, "(40: [10 20 30] [50 60])") && !bt_search(&t, 40u, &v) && tree_ok(&t));
    /* 없는 키 삭제 */
    CHECK(!bt_delete(&t, 41u) && t.nkeys == 5u && tree_ok(&t));
    /* floor 함정: 41~49 는 오른쪽 리프로 내려가지만 그 리프에 41 이하 키가 없다 → prev 로 넘어가야 한다 */
    CHECK(bt_floor(&t, 45u, &k, &val) && k == 30u && val == 31u);
    CHECK(bt_floor(&t, 50u, &k, &val) && k == 50u && !bt_floor(&t, 9u, &k, &val));

    *ok_out = ok;
}

/* 고장 주입: 멀쩡한 트리의 복사본을 한 군데씩 망가뜨려 검사기가 잡는지 본다 */
static void part_b_checker(bool gate) {
    bt_t base;
    bt_init(&base, g_pool, 256u, 4u, 4u);
    for (uint32_t k = 1u; k <= 120u; k++) bt_insert(&base, k * 3u, k);
    CHECK(gate && bt_check(&base) == BTC_OK);
    uint32_t aid = leftmost(&base);
    uint32_t bid = base.pg[aid].h.next;
    /* 맨 왼쪽 두 리프의 부모 찾기 */
    uint32_t pid = base.root;
    for (uint32_t d = 1; d + 1u < base.height; d++) pid = base.pg[pid].node.child[0];

#define CORRUPT(name, ...) do { \
        bt_t w = base; w.pg = g_snap; memcpy(g_snap, g_pool, (size_t)base.npages * sizeof(page_t)); \
        __VA_ARGS__ \
        int rc = bt_check(&w); \
        printf("  corrupt %-22s -> bt_check %d\n", name, rc); \
        CHECK(gate && rc != BTC_OK); \
    } while (0)

    CORRUPT("swap two keys", { page_t *a = &w.pg[aid]; uint32_t x = a->leaf.key[0]; a->leaf.key[0] = a->leaf.key[1]; a->leaf.key[1] = x; });
    /* 왼쪽 리프의 뒤 2칸을 오른쪽 리프로 옮기고 구분키도 고친다 → 키 순서·구분키는 맞고 채움만 위반 */
    CORRUPT("underfill leaf", { page_t *a = &w.pg[aid], *b = &w.pg[bid];
        uint32_t m = 2u;
        memmove(b->leaf.key + m, b->leaf.key, b->h.n * sizeof(uint32_t));
        memmove(b->leaf.val + m, b->leaf.val, b->h.n * sizeof(uint32_t));
        memcpy(b->leaf.key, a->leaf.key + a->h.n - m, m * sizeof(uint32_t));
        memcpy(b->leaf.val, a->leaf.val + a->h.n - m, m * sizeof(uint32_t));
        a->h.n = (uint16_t)(a->h.n - m); b->h.n = (uint16_t)(b->h.n + m);
        w.pg[pid].node.key[0] = b->leaf.key[0]; });
    CORRUPT("separator +1", { w.pg[pid].node.key[0] += 1u; });
    CORRUPT("chain skips a leaf", { w.pg[aid].h.next = w.pg[bid].h.next; });
    CORRUPT("prev link wrong", { w.pg[bid].h.prev = PID_NIL; });
    CORRUPT("nkeys off by one", { w.nkeys += 1u; });
    CORRUPT("leaked page", { uint32_t f = w.free_head; w.free_head = w.pg[f].h.next; w.free_cnt--; });
    CORRUPT("leaf page marked free", { w.pg[bid].h.kind = PK_FREE; });

    /* 깊이 불균형: node_cap=3 (최소 1키) 트리에서 자식 둘을 새 내부 페이지로 묶어 한 층 내린다 — 채움·순서·사슬은 그대로 */
    bt_t d3;
    bt_init(&d3, g_pool, 256u, 4u, 3u);
    for (uint32_t k = 1u; k <= 120u; k++) bt_insert(&d3, k * 3u, k);
    CHECK(gate && bt_check(&d3) == BTC_OK && d3.height >= 3u);
    bt_t w = d3;
    w.pg = g_snap;
    memcpy(g_snap, g_pool, (size_t)d3.npages * sizeof(page_t));
    uint32_t x = w.root;
    for (uint32_t d = 1; d + 1u < w.height; d++) x = w.pg[x].node.child[0];
    if (w.pg[x].h.n < 2u) x = w.pg[w.pg[w.root].node.child[0]].node.child[1];
    page_t *X = &w.pg[x];
    uint32_t y = w.free_head;
    w.free_head = w.pg[y].h.next;
    w.free_cnt--;
    page_t *Y = &w.pg[y];
    Y->h.kind = PK_NODE;
    Y->h.n = 1u;
    Y->h.next = Y->h.prev = PID_NIL;
    Y->node.key[0] = X->node.key[0];
    Y->node.child[0] = X->node.child[0];
    Y->node.child[1] = X->node.child[1];
    arr_del(X->node.key, X->h.n, 0u);
    arr_del(X->node.child, X->h.n + 1u, 1u);
    X->h.n--;
    X->node.child[0] = y;
    int rc = bt_check(&w);
    printf("  corrupt %-22s -> bt_check %d\n", "uneven leaf depth", rc);
    CHECK(gate && X->h.n >= 1u && rc != BTC_OK);
}

typedef struct { uint32_t leaf_cap, node_cap, keyspace, ops, full_every, seed, npages; } stress_t;

/* 전체 비교: 스캔 결과 == 참조 모델, 그리고 검색 일치 */
static bool full_compare(bt_t *t, uint32_t ks, uint32_t refn) {
    uint32_t n = bt_scan(t, 0u, 0xFFFFFFFFu, g_ks, g_vs, KS_MAX);
    if (n != refn || t->nkeys != refn) return false;
    uint32_t j = 0;
    for (uint32_t k = 0; k < ks; k++) {
        if (g_ref[k] == 0u) continue;
        if (j >= n || g_ks[j] != k || g_vs[j] != g_ref[k]) return false;
        j++;
    }
    return j == n;
}

static bool g_stress_coverage_ok;
static bool stress(const stress_t *c) {
    bt_t t;
    uint32_t refn = 0, cursor = 0, maxh = 1u;
    uint64_t maxkeys = 0;
    g_rng = c->seed;
    bt_init(&t, g_pool, c->npages, c->leaf_cap, c->node_cap);
    memset(g_ref, 0, sizeof g_ref);
    for (uint32_t i = 0; i < c->ops; i++) {
        uint32_t phase = (uint32_t)(((uint64_t)i * 8u) / c->ops) % 3u;
        uint32_t p_ins = phase == 0u ? 72u : phase == 1u ? 20u : 45u;
        uint32_t sel = rnd() % 16u, key;
        if (sel < 4u) { key = cursor; cursor = (cursor + 1u) % c->keyspace; }
        else if (sel < 6u) { cursor = cursor ? cursor - 1u : c->keyspace - 1u; key = cursor; }
        else key = rnd() % c->keyspace;
        uint32_t o = rnd() % 100u;
        if (o < p_ins) {
            uint32_t val = rnd() | 1u;
            int rc = bt_insert(&t, key, val);
            int want = g_ref[key] ? BT_UPD : BT_NEW;
            if (rc != want) { printf("  insert mismatch: key %u rc %d want %d (op %u)\n", key, rc, want, i); return false; }
            if (want == BT_NEW) refn++;
            g_ref[key] = val;
        } else if (o < 90u) {
            bool rc = bt_delete(&t, key), want = g_ref[key] != 0u;
            if (rc != want) { printf("  delete mismatch: key %u (op %u)\n", key, i); return false; }
            if (want) refn--;
            g_ref[key] = 0u;
        } else if (o < 98u) {
            uint32_t v = 0;
            bool rc = bt_search(&t, key, &v);
            if (rc != (g_ref[key] != 0u) || (rc && v != g_ref[key])) { printf("  search mismatch: key %u (op %u)\n", key, i); return false; }
        } else {
            uint32_t ks[128], vs[128], lo = key, hi = key + rnd() % 64u;
            if (hi >= c->keyspace) hi = c->keyspace - 1u;
            uint32_t n = bt_scan(&t, lo, hi, ks, vs, 128u), j = 0;
            for (uint32_t k = lo; k <= hi; k++) if (g_ref[k]) {
                if (j >= n || ks[j] != k || vs[j] != g_ref[k]) { printf("  scan mismatch at %u (op %u)\n", k, i); return false; }
                j++;
            }
            if (j != n) { printf("  scan count mismatch (op %u)\n", i); return false; }
        }
        if (t.height > maxh) maxh = t.height;
        if (t.nkeys > maxkeys) maxkeys = t.nkeys;
        if ((i % c->full_every) == 0u || i + 1u == c->ops) {
            int rc = bt_check(&t);
            if (rc != BTC_OK || t.bad_id) { printf("  bt_check %d bad_id %u after op %u: cap %u\n", rc, t.bad_id, i, c->leaf_cap); return false; }
            if (!full_compare(&t, c->keyspace, refn)) { printf("  full compare failed after op %u\n", i); return false; }
        }
    }
    printf("  cap %3u/%3u ks %7u ops %7u check every %5u: max height %u, max keys %llu | leaf splits %u, node splits %u, merges %u+%u, redist L%u R%u, root collapses %u\n",
           c->leaf_cap, c->node_cap, c->keyspace, c->ops, c->full_every, maxh, (unsigned long long)maxkeys,
           t.split_leaf, t.split_node, t.merge_leaf, t.merge_node, t.redist_l, t.redist_r, t.collapses);
    g_stress_coverage_ok = t.split_leaf && t.merge_leaf && t.redist_l && t.redist_r;
    return true;
}

/* 비우기까지: 오름차순·내림차순으로 모두 지우면 루트 리프만 남는다 */
static void part_b_drain(void) {
    bt_t t;
    bool ok = true;
    bt_init(&t, g_pool, 2048u, 4u, 4u);
    fill_ascending(&t, 0u, 1u, 3000u);
    for (uint32_t k = 0; k < 3000u; k++) { ok &= bt_delete(&t, k); ok &= tree_ok(&t); }
    CHECK(ok && t.nkeys == 0u && t.height == 1u && t.free_cnt == 2048u - 2u && t.collapses >= 2u);
    bt_init(&t, g_pool, 2048u, 5u, 5u);
    fill_ascending(&t, 0u, 1u, 3000u);
    ok = true;
    for (uint32_t k = 3000u; k-- > 0u; ) { ok &= bt_delete(&t, k); ok &= tree_ok(&t); }
    CHECK(ok && t.nkeys == 0u && t.height == 1u && t.free_cnt == 2048u - 2u);
    /* 4 KB 팬아웃 비우기 */
    bt_init(&t, g_pool, 2048u, LEAF_MAX, NODE_MAX);
    fill_ascending(&t, 0u, 2u, 200000u);
    CHECK(t.height == 3u && tree_ok(&t));      /* 오름차순: 리프가 절반만 차서 784 리프 > 팬아웃 510 */
    ok = true;
    for (uint32_t k = 0; k < 200000u; k++) ok &= bt_delete(&t, k * 2u);
    CHECK(ok && t.nkeys == 0u && t.height == 1u && t.free_cnt == 2048u - 2u && tree_ok(&t));
}

/* ---- Part C 참조 모델 (LBA 마다 PBA) ---- */
#define LBA_SPACE (1u << 15)
static uint32_t g_l2p[LBA_SPACE];
static uint32_t g_extk[LBA_SPACE + 1u], g_extv[LBA_SPACE + 1u];

static bool em_verify(bt_t *t, uint32_t lba_n) {
    for (uint32_t l = 0; l < lba_n; l++) if (em_lookup(t, l) != g_l2p[l]) { printf("  L2P mismatch at lba %u\n", l); return false; }
    uint32_t n = bt_scan(t, 0u, 0xFFFFFFFFu, g_extk, g_extv, LBA_SPACE + 1u);
    for (uint32_t i = 0; i < n; i++) {
        if (em_len(g_extv[i]) == 0u) { printf("  zero-length extent\n"); return false; }
        if (i + 1u < n && g_extk[i] + em_len(g_extv[i]) > g_extk[i + 1u]) { printf("  overlapping extents\n"); return false; }
    }
    return true;
}

static void part_c(void) {
    bt_t t;
    uint32_t k, v;
    /* 익스텐트 조각내기 시나리오 */
    bt_init(&t, g_pool, 256u, 6u, 6u);
    memset(g_l2p, 0xFF, sizeof g_l2p);
    em_write(&t, 100u, 50u, 1000u);                    /* [100,150) → 1000 */
    CHECK(em_lookup(&t, 100u) == 1000u && em_lookup(&t, 149u) == 1049u && em_lookup(&t, 150u) == EM_NONE && em_lookup(&t, 99u) == EM_NONE);
    em_write(&t, 120u, 10u, 5000u);                    /* 가운데 덮어쓰기 → 3 조각 */
    CHECK(t.nkeys == 3u && em_lookup(&t, 119u) == 1019u && em_lookup(&t, 120u) == 5000u && em_lookup(&t, 129u) == 5009u && em_lookup(&t, 130u) == 1030u);
    em_trim(&t, 90u, 35u);                             /* 앞쪽 잘라내기 + 시작점 지나가기 */
    CHECK(em_lookup(&t, 119u) == EM_NONE && em_lookup(&t, 124u) == EM_NONE && em_lookup(&t, 125u) == 5005u && bt_check(&t) == BTC_OK);
    em_write(&t, 0u, 8u, 200u);
    em_write(&t, 8u, 8u, 208u);                        /* LBA·PBA 연속 → 합침 */
    CHECK(bt_floor(&t, 15u, &k, &v) && k == 0u && em_len(v) == 16u && em_pba(v) == 200u);
    em_write(&t, 16u, 8u, 999u);                       /* PBA 불연속 → 합치지 않음 */
    CHECK(bt_floor(&t, 16u, &k, &v) && k == 16u && em_pba(v) == 999u && t.bad_id == 0u);

    /* 순차 쓰기는 합쳐져 익스텐트가 적어야 한다: 8섹터 × 1000회 = 8000 섹터, 익스텐트당 최대 248 (31회) */
    bt_init(&t, g_pool, 256u, 64u, 64u);
    bool ok = true;
    for (uint32_t i = 0; i < 1000u; i++) ok &= em_write(&t, i * 8u, 8u, 4096u + i * 8u) != BT_NOSPACE;
    CHECK(ok && t.nkeys == 33u);

    /* 무작위 덮어쓰기 + 순차 구간, 참조 L2P 와 대조. 작은 cap (작은 공간, 매번 검사), 큰 cap (큰 공간) */
    static const struct { uint32_t cap, lba, ops, every, seed; } cfg[2] = {
        { 4u,   256u,   20000u, 1u,    0xA8000001u },
        { 510u, LBA_SPACE, 200000u, 4096u, 0xA8000002u },
    };
    for (unsigned c = 0; c < 2u; c++) {
        bool good = true;
        uint32_t nops = cfg[c].ops, every = cfg[c].every;
        bt_init(&t, g_pool, POOL_PAGES, cfg[c].cap, cfg[c].cap > 509u ? 509u : cfg[c].cap);
        memset(g_l2p, 0xFF, sizeof g_l2p);
        g_rng = cfg[c].seed;
        uint32_t head = 0x1000u, cursor = 0;
        for (uint32_t i = 0; i < nops && good; i++) {
            uint32_t r = rnd() % 16u, lba, len;
            if (r < 6u) { lba = cursor; len = 1u + rnd() % 64u; cursor += len; if (cursor >= cfg[c].lba) cursor = 0; }
            else { lba = rnd() % cfg[c].lba; len = 1u + rnd() % 64u; }
            if (lba + len > cfg[c].lba) len = cfg[c].lba - lba;
            if (r == 15u) {
                em_trim(&t, lba, len);
                for (uint32_t l = 0; l < len; l++) g_l2p[lba + l] = EM_NONE;
            } else {
                if (em_write(&t, lba, len, head) == BT_NOSPACE) { good = false; printf("  pool exhausted\n"); }
                for (uint32_t l = 0; l < len; l++) g_l2p[lba + l] = head + l;
                head += len;
                if (rnd() % 8u == 0u) head += 1u;      /* 가끔 PBA 를 끊어 합치기를 막는다 */
            }
            if ((i % every) == 0u || i + 1u == nops) {
                if (bt_check(&t) != BTC_OK || !em_verify(&t, cfg[c].lba)) { printf("  check failed at op %u\n", i); good = false; }
            }
        }
        CHECK(good && t.bad_id == 0u);
        uint32_t used = t.npages - t.free_cnt - 1u;
        if (c == 1u) printf("  extents %u, height %u, pages %u = %zu B  vs flat L2P %zu B (LBA space %u)\n",
               t.nkeys, t.height, used, (size_t)used * sizeof(page_t), (size_t)cfg[c].lba * 4u, cfg[c].lba);
    }
}

int main(void) {
    printf("== Part A: search / insert with split / range scan\n");
    bool a_ok = part_a_units();
    CHECK(a_ok);
    if (a_ok) {
        part_a_height();
        part_a_nospace_and_relocate();
    } else {
        printf("SKIP height/fanout/no-space/relocation: unit checks failed\n");
        g_total += 7;
    }

    printf("== Part B: delete (redistribute, merge, root collapse) + invariant checker\n");
    bool b_ok = false;
    if (a_ok) {
        part_b_scenarios(&b_ok);
        CHECK(b_ok);
        part_b_checker(b_ok);
        part_b_drain();
        bool all = b_ok;
        static const stress_t st[] = {
            { 3u,   3u,   300u,     30000u,  1u,    0xB0000001u, 512u },
            { 4u,   4u,   512u,     30000u,  1u,    0xB0000002u, 512u },
            { 5u,   5u,   512u,     30000u,  1u,    0xB0000003u, 512u },
            { 6u,   7u,   1024u,    30000u,  1u,    0xB0000004u, 1024u },
            { 8u,   8u,   2048u,    30000u,  1u,    0xB0000005u, 1024u },
            { 16u,  16u,  32768u,   300000u, 256u,  0xB0000006u, 8192u },
            { 510u, 509u, 1048576u, 1000000u, 16384u, 0xB0000007u, 8192u },
        };
        for (size_t i = 0; i < sizeof st / sizeof st[0] && all; i++) {
            stress_t s = st[i];
            all &= stress(&s);
            all &= g_stress_coverage_ok;
        }
        CHECK(all);
    } else {
        printf("SKIP delete scenarios, checker, stress: Part A failed\n");
        g_total += 34;
    }

    printf("== Part C: LBA extent map on the tree\n");
    if (a_ok && b_ok) {
        part_c();
    } else {
        printf("SKIP extent map: Part A/B failed\n");
        g_total += 8;
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
