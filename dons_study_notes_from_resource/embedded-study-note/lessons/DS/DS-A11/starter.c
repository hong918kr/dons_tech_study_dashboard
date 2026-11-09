/* DS-A11 캐시 친화 레이아웃: AoS vs SoA와 정렬
 *
 * 같은 논리 레코드(Rec, 64바이트) 배열을 세 가지 메모리 배치로 구현한다.
 *   AoS    Rec 가 연속으로 놓인 배열 (주어짐 — 기준점)
 *   SoA    필드마다 배열 하나 (x[], y[], ...)
 *   AoSoA  W=8 개씩 묶은 Block 의 배열. Block 안은 SoA, Block 끼리는 AoS.
 *
 *   Part A  SoA 접근자 + 부분 필드 커널(sum_x, sum_masked) + 갱신 커널(update_pos, update_all)
 *   Part B  AoSoA 인덱스 계산·꼬리(n 이 W 배수가 아닐 때), swap_remove, 패딩 계산(layout_size),
 *           커널이 건드리는 캐시 라인 수를 세는 모델(line_switches)
 *   Part C  펌웨어 현실: 정렬 안 된 DMA 바이트열 → SoA 디코드, 윈도 통계, 노드 풀 위 연결 리스트
 *
 * 이 파일의 체크는 결정적인 것만 둔다: 레이아웃 3개의 값·합·체크섬 일치, sizeof/offsetof/스트라이드,
 * 라인 수 모델. 시간 측정은 demo_bench.c 가 한다 (타이밍은 CHECK 에 넣지 않는다).
 * 스트레스: 작은 n 은 연산마다 전수 검사, n≈1024 는 K=1024 연산마다 + 마지막에 전수 검사.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

#define CAP     1024u   /* 레코드 최대 개수 */
#define W_SHIFT 3u
#define W       (1u << W_SHIFT)   /* AoSoA 블록 폭 */
#define LINE    128u    /* 이 맥의 hw.cachelinesize. Cortex-A 는 보통 64, Cortex-M7 은 32 */
#define POISON  0xA5u   /* 빈 슬롯을 채우는 바이트 — n 밖을 건드리면 드러난다 */

/* 논리 레코드 — 레이아웃과 무관한 "값". 필드를 큰 정렬부터 놓아 패딩이 0 이다. */
typedef struct {
    uint64_t t_us;              /* offset  0 타임스탬프 */
    uint32_t x, y, z;           /* offset  8 위치 (Q16.16, 감김 허용 — 부호 오버플로 UB 를 피하려 unsigned) */
    uint32_t vx, vy, vz;        /* offset 20 속도 */
    uint32_t seq;               /* offset 32 시퀀스 번호 */
    uint16_t temp;              /* offset 36 온도 */
    uint8_t  flags;             /* offset 38 bit0 = 활성 */
    uint8_t  health;            /* offset 39 */
    uint32_t cal[6];            /* offset 40 교정 계수 (콜드) */
} Rec;

_Static_assert(sizeof(Rec) == 64, "Rec must be exactly 64 bytes");
_Static_assert(alignof(Rec) == 8, "Rec alignment is that of its uint64_t");
_Static_assert(offsetof(Rec, x) == 8 && offsetof(Rec, vx) == 20, "field order");
_Static_assert(offsetof(Rec, seq) == 32 && offsetof(Rec, temp) == 36, "field order");
_Static_assert(offsetof(Rec, flags) == 38 && offsetof(Rec, cal) == 40, "field order");

/* 패딩 대조군: 같은 멤버인데 순서만 다르다. */
typedef struct { uint8_t flags; uint64_t t_us; uint8_t health; uint32_t x; uint16_t temp; } BadOrder;
typedef struct { uint64_t t_us; uint32_t x; uint16_t temp; uint8_t flags; uint8_t health; } GoodOrder;
_Static_assert(sizeof(BadOrder) == 32 && sizeof(GoodOrder) == 16, "padding: 16 bytes vs 0");

typedef struct {
    alignas(LINE) Rec r[CAP];
    uint32_t n;
} Aos;

typedef struct {
    alignas(LINE) uint64_t t_us[CAP];
    alignas(LINE) uint32_t x[CAP];
    alignas(LINE) uint32_t y[CAP];
    alignas(LINE) uint32_t z[CAP];
    alignas(LINE) uint32_t vx[CAP];
    alignas(LINE) uint32_t vy[CAP];
    alignas(LINE) uint32_t vz[CAP];
    alignas(LINE) uint32_t seq[CAP];
    alignas(LINE) uint16_t temp[CAP];
    alignas(LINE) uint8_t  flags[CAP];
    alignas(LINE) uint8_t  health[CAP];
    alignas(LINE) uint32_t cal[CAP][6];
    uint32_t n;
} Soa;

typedef struct {
    uint32_t x[W], y[W], z[W];
    uint32_t vx[W], vy[W], vz[W];
    uint64_t t_us[W];
    uint32_t seq[W];
    uint16_t temp[W];
    uint8_t  flags[W], health[W];
    uint32_t cal[W][6];
} Block;

typedef struct {
    alignas(LINE) Block b[CAP / W];
    uint32_t n;
} Aosoa;

/* 스트라이드(인접 원소 간격)와 정렬: 틀리면 컴파일이 멈춘다. */
_Static_assert(CAP % W == 0, "whole blocks only");
_Static_assert(alignof(Aos) == LINE && alignof(Soa) == LINE && alignof(Aosoa) == LINE, "containers start on a line");
_Static_assert(offsetof(Aos, r[1]) - offsetof(Aos, r[0]) == 64, "AoS stride = 64");
_Static_assert(offsetof(Soa, x[1]) - offsetof(Soa, x[0]) == 4, "SoA stride = 4");
_Static_assert(offsetof(Soa, y) - offsetof(Soa, x) == CAP * 4, "no padding between hot arrays");
_Static_assert(sizeof(Block) == 64 * W, "block = 64 bytes per lane, no holes");
_Static_assert(offsetof(Block, vx) == W * 12 && offsetof(Block, t_us) == W * 24, "lane offsets");
_Static_assert(sizeof(Block) % LINE == 0, "blocks stay line-aligned");

static Rec g_poison;

/* 레코드 1개의 갱신 규칙 — 모든 필드가 바뀐다. 순서가 중요하다: 위치는 "옛 속도" 로 적분한다. */
static inline void rec_update_all(Rec *r, uint32_t dt) {
    r->t_us += dt;
    r->x += r->vx;  r->y += r->vy;  r->z += r->vz;
    r->vx -= r->vx >> 4;  r->vy -= r->vy >> 4;  r->vz -= r->vz >> 4;
    r->seq += 1;
    r->temp = (uint16_t)(r->temp + 1);
    r->flags = (uint8_t)(r->flags ^ 1u);
    r->health = (uint8_t)(r->health - (r->health > 0));
    for (uint32_t j = 0; j < 6; j++) r->cal[j] += j + 1;
}

/* ======================= AoS — 주어짐 (기준점) ======================= */
static void aos_init(Aos *a) { memset(a, POISON, sizeof *a); a->n = 0; }
static void aos_get(const Aos *a, uint32_t i, Rec *o) { *o = a->r[i]; }
static void aos_set(Aos *a, uint32_t i, const Rec *r) { a->r[i] = *r; }
static void aos_push(Aos *a, const Rec *r) { a->r[a->n++] = *r; }
static uint32_t aos_sum_x(const Aos *a) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < a->n; i++) sum += a->r[i].x;
    return sum;
}
static uint32_t aos_sum_masked(const Aos *a) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < a->n; i++) sum += (a->r[i].flags & 1u) ? a->r[i].x : 0u;
    return sum;
}
static void aos_update_pos(Aos *a) {
    for (uint32_t i = 0; i < a->n; i++) {
        a->r[i].x += a->r[i].vx;
        a->r[i].y += a->r[i].vy;
        a->r[i].z += a->r[i].vz;
    }
}
static void aos_update_all(Aos *a, uint32_t dt) {
    for (uint32_t i = 0; i < a->n; i++) rec_update_all(&a->r[i], dt);
}
static void aos_swap_remove(Aos *a, uint32_t i) {
    uint32_t last = a->n - 1;
    if (i != last) a->r[i] = a->r[last];
    a->r[last] = g_poison;
    a->n = last;
}

/* ======================= Part A — SoA ======================= */
static void soa_init(Soa *s) { memset(s, POISON, sizeof *s); s->n = 0; }

/* i 번 레코드의 모든 필드를 필드 배열에서 모아 *o 로. */
static void soa_get(const Soa *s, uint32_t i, Rec *o) {
    /* TODO: 각 필드 배열의 i 번째를 *o 에 복사 (cal 은 memcpy) */
    (void)s; (void)i;
    memset(o, 0, sizeof *o);
}

static void soa_set(Soa *s, uint32_t i, const Rec *r) {
    /* TODO: *r 의 모든 필드를 필드 배열의 i 번째에 저장 */
    (void)s; (void)i; (void)r;
}

static void soa_push(Soa *s, const Rec *r) {
    /* TODO: 맨 뒤에 추가하고 n 을 늘린다 */
    (void)s; (void)r;
}

/* 부분 필드 읽기 ① x 하나만 합산. */
static uint32_t soa_sum_x(const Soa *s) {
    /* TODO */
    (void)s;
    return 0;
}

/* 부분 필드 읽기 ② flags bit0 이 켜진 레코드의 x 만 합산. */
static uint32_t soa_sum_masked(const Soa *s) {
    /* TODO */
    (void)s;
    return 0;
}

/* 절반 필드 갱신: x/y/z += vx/vy/vz. */
static void soa_update_pos(Soa *s) {
    /* TODO */
    (void)s;
}

/* 전체 갱신: rec_update_all 과 레코드별 결과가 똑같아야 한다. */
static void soa_update_all(Soa *s, uint32_t dt) {
    /* TODO: 위치 적분은 속도 감쇠보다 먼저 (rec_update_all 과 순서가 같아야 한다) */
    (void)s; (void)dt;
}

/* i 번을 지운다: 마지막 레코드를 i 로 옮기고 빈 자리에 g_poison 을 쓴다. 순서는 안 지킨다. */
static void soa_swap_remove(Soa *s, uint32_t i) {
    /* TODO */
    (void)s; (void)i;
}

/* ======================= Part B — AoSoA: 인덱스·꼬리, 패딩, 라인 모델 ======================= */
static void aosoa_init(Aosoa *a) { memset(a, POISON, sizeof *a); a->n = 0; }

/* i → (블록 번호, 레인). */
static inline uint32_t blk_of(uint32_t i) {
    /* TODO: 블록 번호 */
    (void)i;
    return 0;
}
static inline uint32_t lane_of(uint32_t i) {
    /* TODO: 블록 안의 레인 */
    (void)i;
    return 0;
}

static void aosoa_get(const Aosoa *a, uint32_t i, Rec *o) {
    /* TODO: blk_of / lane_of 로 찾아 모든 필드 복사 */
    (void)a; (void)i;
    memset(o, 0, sizeof *o);
}

static void aosoa_set(Aosoa *a, uint32_t i, const Rec *r) {
    /* TODO */
    (void)a; (void)i; (void)r;
}

static void aosoa_push(Aosoa *a, const Rec *r) {
    /* TODO */
    (void)a; (void)r;
}

/* 힌트: 블록 단위 for + "이 블록의 유효 레인 수 m = min(W, 남은 개수)". n 밖 레인은 절대 건드리지 않는다. */
static uint32_t aosoa_sum_x(const Aosoa *a) {
    /* TODO */
    (void)a;
    return 0;
}

static uint32_t aosoa_sum_masked(const Aosoa *a) {
    /* TODO */
    (void)a;
    return 0;
}

static void aosoa_update_pos(Aosoa *a) {
    /* TODO */
    (void)a;
}

static void aosoa_update_all(Aosoa *a, uint32_t dt) {
    /* TODO: 꼬리 블록의 유효 레인까지만, 필드별 갱신 순서는 rec_update_all 과 같게 */
    (void)a; (void)dt;
}

static void aosoa_swap_remove(Aosoa *a, uint32_t i) {
    /* TODO */
    (void)a; (void)i;
}

/* --- 패딩 계산 --- */
typedef struct { uint32_t size, align; } Field;

/* 주어진 순서로 멤버를 놓을 때의 sizeof: 각 offset 을 align 배수로 올리고, 끝을 최대 align 배수로 올린다. */
static uint32_t layout_size(const Field *f, uint32_t n) {
    /* TODO: 위 규칙 그대로 */
    (void)f; (void)n;
    return 0;
}

/* --- 라인 모델 ---
 * 커널이 원소를 i = 0..n-1 순서로 건드릴 때 "다른 라인으로 넘어간 횟수" 를 센다.
 * 주소가 단조 증가하면 = 건드린 라인 수. 플랫폼과 무관한 숫자로 시간 측정을 뒷받침한다. */
typedef const void *(*AddrFn)(const void *c, uint32_t i);

static const void *aos_x_addr(const void *c, uint32_t i) { return &((const Aos *)c)->r[i].x; }
static const void *soa_x_addr(const void *c, uint32_t i) { return &((const Soa *)c)->x[i]; }
static const void *aosoa_x_addr(const void *c, uint32_t i) {
    const Aosoa *a = c;
    return &a->b[blk_of(i)].x[lane_of(i)];
}

static uint32_t line_switches(const void *c, AddrFn addr, uint32_t n, uint32_t elem, uint32_t line) {
    /* TODO: 원소 하나가 차지하는 첫 라인 ~ 끝 라인을 차례로 보며, 직전 라인과 다르면 +1 */
    (void)c; (void)addr; (void)n; (void)elem; (void)line;
    return 0;
}

/* ======================= Part C — 펌웨어: DMA 프레임 → SoA, 윈도 통계, 노드 풀 리스트 ======================= */

/* 와이어 포맷 (19 바이트, little-endian, 정렬 없음):
 *   0: seq u32   4: t_us u64   12: x u32   16: temp u16   18: flags u8
 * DMA 버퍼 안에서 프레임은 아무 주소에서나 시작한다 → 포인터 캐스트 금지, 바이트로 조립한다. */
#define WIRE_SIZE 19u

static uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* buf[0..len) 의 프레임을 SoA 뒤에 붙인다. y/z/vx/vy/vz/cal/health 는 0 으로.
 * 끝에 남는 조각(프레임 미만)은 무시, CAP 에 차면 멈춘다. 붙인 개수를 반환. */
static uint32_t wire_decode(Soa *s, const uint8_t *buf, size_t len) {
    /* TODO: get_le32 로 프레임을 조립해 soa_push. 절대 (const uint32_t *)buf 로 캐스트하지 않는다 */
    (void)s; (void)buf; (void)len; (void)get_le32;
    return 0;
}

typedef struct { uint32_t min, max; uint64_t sum; } WinStats;

/* x[first .. first+count) 의 min/max/sum. 범위가 [0, n) 을 벗어나거나 count == 0 이면 false. */
static bool soa_window_stats(const Soa *s, uint32_t first, uint32_t count, WinStats *o) {
    /* TODO: first + count 의 덧셈이 감기지 않게 범위 검사 */
    (void)s; (void)first; (void)count; (void)o;
    return false;
}

/* --- 연결 리스트 ---
 * 노드는 정적 풀 (malloc 없음). order[0] → order[1] → ... 순서로 next 를 잇고 head 를 돌려준다. */
typedef struct Node {
    struct Node *next;
    uint32_t val;
    uint8_t pad[64 - sizeof(void *) - sizeof(uint32_t)];
} Node;
_Static_assert(sizeof(Node) == 64, "Node = 64 bytes (AoS record 와 같은 크기)");

static Node *list_link(Node *pool, const uint32_t *order, uint32_t n) {
    /* TODO */
    (void)pool; (void)order; (void)n;
    return NULL;
}

static uint32_t list_sum(const Node *h) {
    /* TODO */
    (void)h;
    return 0;
}

/* 순회 중 다른 캐시 라인으로 넘어간 횟수 (노드당 next+val 만 본다 — 앞 12 바이트). */
static uint32_t list_line_switches(const Node *h, uint32_t line) {
    /* TODO */
    (void)h; (void)line;
    return 0;
}

/* ======================= 하네스 ======================= */
static uint32_t rng_s;
static uint32_t rnd(void) {
    uint32_t x = rng_s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_s = x;
}

static Rec random_rec(void) {
    Rec r;
    r.t_us = (uint64_t)rnd() << 32 | rnd();
    r.x = rnd(); r.y = rnd(); r.z = rnd();
    r.vx = rnd(); r.vy = rnd(); r.vz = rnd();
    r.seq = rnd();
    r.temp = (uint16_t)rnd();
    r.flags = (uint8_t)rnd();
    r.health = (uint8_t)(rnd() % 4u);
    for (int j = 0; j < 6; j++) r.cal[j] = rnd();
    return r;
}

static uint64_t fnv_rec(uint64_t h, const Rec *r) {
    /* Rec 는 패딩이 없다(_Static_assert) → 바이트 해시가 값 해시와 같다. */
    const uint8_t *p = (const uint8_t *)r;
    for (size_t i = 0; i < sizeof *r; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

#define FNV0 14695981039346656037ull

static Aos   g_aos, g_aos2;
static Soa   g_soa, g_soa2;
static Aosoa g_aosoa, g_aosoa2;
static Rec   g_ref[CAP];
static uint32_t g_ref_n;

static void all_init(void) {
    aos_init(&g_aos); soa_init(&g_soa); aosoa_init(&g_aosoa);
    memset(g_ref, 0, sizeof g_ref);
    g_ref_n = 0;
}
static void all_push(const Rec *r) {
    aos_push(&g_aos, r); soa_push(&g_soa, r); aosoa_push(&g_aosoa, r);
    g_ref[g_ref_n++] = *r;
}
static void all_update_all(uint32_t dt) {
    aos_update_all(&g_aos, dt); soa_update_all(&g_soa, dt); aosoa_update_all(&g_aosoa, dt);
    for (uint32_t i = 0; i < g_ref_n; i++) rec_update_all(&g_ref[i], dt);
}
static void all_update_pos(void) {
    aos_update_pos(&g_aos); soa_update_pos(&g_soa); aosoa_update_pos(&g_aosoa);
    for (uint32_t i = 0; i < g_ref_n; i++) {
        g_ref[i].x += g_ref[i].vx; g_ref[i].y += g_ref[i].vy; g_ref[i].z += g_ref[i].vz;
    }
}
static void all_swap_remove(uint32_t i) {
    aos_swap_remove(&g_aos, i); soa_swap_remove(&g_soa, i); aosoa_swap_remove(&g_aosoa, i);
    g_ref_n--;
    if (i != g_ref_n) g_ref[i] = g_ref[g_ref_n];
}

/* 전수 불변식: 세 컨테이너 모두 레퍼런스와 같고, 합·체크섬이 같고, n 밖은 포이즌 그대로. */
static bool invariants_ok(void) {
    uint32_t n = g_ref_n;
    if (g_aos.n != n || g_soa.n != n || g_aosoa.n != n) return false;
    uint64_t ha = FNV0, hs = FNV0, hb = FNV0, hr = FNV0;
    uint32_t sum = 0, sum_m = 0;
    for (uint32_t i = 0; i < n; i++) {
        Rec ra, rs, rb;
        aos_get(&g_aos, i, &ra); soa_get(&g_soa, i, &rs); aosoa_get(&g_aosoa, i, &rb);
        if (memcmp(&ra, &g_ref[i], sizeof(Rec)) != 0) return false;
        if (memcmp(&rs, &g_ref[i], sizeof(Rec)) != 0) return false;
        if (memcmp(&rb, &g_ref[i], sizeof(Rec)) != 0) return false;
        ha = fnv_rec(ha, &ra); hs = fnv_rec(hs, &rs); hb = fnv_rec(hb, &rb); hr = fnv_rec(hr, &g_ref[i]);
        sum += g_ref[i].x;
        sum_m += (g_ref[i].flags & 1u) ? g_ref[i].x : 0u;
    }
    if (ha != hr || hs != hr || hb != hr) return false;
    if (aos_sum_x(&g_aos) != sum || soa_sum_x(&g_soa) != sum || aosoa_sum_x(&g_aosoa) != sum) return false;
    if (aos_sum_masked(&g_aos) != sum_m || soa_sum_masked(&g_soa) != sum_m) return false;
    if (aosoa_sum_masked(&g_aosoa) != sum_m) return false;
    for (uint32_t i = n; i < CAP; i++) {
        Rec ra, rs, rb;
        aos_get(&g_aos, i, &ra); soa_get(&g_soa, i, &rs); aosoa_get(&g_aosoa, i, &rb);
        if (memcmp(&ra, &g_poison, sizeof(Rec)) != 0) return false;
        if (memcmp(&rs, &g_poison, sizeof(Rec)) != 0) return false;
        if (memcmp(&rb, &g_poison, sizeof(Rec)) != 0) return false;
    }
    return true;
}

static void test_partA(void) {
    printf("--- Part A: 레이아웃이 달라도 값은 같다\n");
    Rec r;
    memset(&r, 0, sizeof r);
    r.t_us = 1000; r.x = 1; r.y = 2; r.z = 3; r.vx = 16; r.vy = 32; r.vz = 48;
    r.seq = 7; r.temp = 100; r.flags = 2; r.health = 1;
    for (uint32_t j = 0; j < 6; j++) r.cal[j] = j + 1;
    all_init();
    all_push(&r);
    Rec out;
    soa_get(&g_soa, 0, &out);
    CHECK(memcmp(&out, &r, sizeof r) == 0);
    CHECK(soa_sum_x(&g_soa) == 1u && soa_sum_masked(&g_soa) == 0u);
    all_update_all(5);
    soa_get(&g_soa, 0, &out);
    CHECK(out.t_us == 1005 && out.x == 17 && out.y == 34 && out.z == 51);   /* 옛 속도로 적분 */
    CHECK(out.vx == 15 && out.vy == 30 && out.vz == 45);
    CHECK(out.seq == 8 && out.temp == 101 && out.flags == 3 && out.health == 0);
    CHECK(out.cal[0] == 2 && out.cal[5] == 12);
    CHECK(soa_sum_masked(&g_soa) == 17u);                                   /* flags bit0 이 켜졌다 */
    all_update_all(5);
    soa_get(&g_soa, 0, &out);
    CHECK(out.health == 0 && out.flags == 2);                               /* health 는 0 에서 멈춘다 */
    all_update_pos();
    CHECK(invariants_ok());
    all_init();
    for (uint32_t i = 0; i < CAP; i++) { Rec q = random_rec(); all_push(&q); }
    for (uint32_t i = 0; i < 16; i++) all_update_all(i * 3u + 1u);
    CHECK(invariants_ok());
}

static void test_partB(void) {
    printf("--- Part B: AoSoA 인덱스·꼬리, swap_remove, 패딩, 라인 모델\n");
    CHECK(blk_of(0) == 0 && lane_of(0) == 0);
    CHECK(blk_of(7) == 0 && lane_of(7) == 7);
    CHECK(blk_of(8) == 1 && lane_of(8) == 0);
    CHECK(blk_of(1023) == 127 && lane_of(1023) == 7);
    CHECK(aosoa_x_addr(&g_aosoa, 9) == (const void *)&g_aosoa.b[1].x[1]);

    /* 꼬리: W 배수가 아닌 n. 포이즌은 합에도, 갱신 커널에도 닿으면 안 된다. */
    uint32_t ns[] = {1, 7, 8, 9, 13, 16, 17};
    bool tail_ok = true;
    for (size_t k = 0; k < sizeof ns / sizeof ns[0]; k++) {
        all_init();
        for (uint32_t i = 0; i < ns[k]; i++) { Rec q = random_rec(); all_push(&q); }
        all_update_all(9);
        all_update_pos();
        tail_ok = tail_ok && invariants_ok();
    }
    CHECK(tail_ok);

    /* swap_remove: 가운데 / 마지막 / 블록 경계를 넘는 이동. */
    all_init();
    for (uint32_t i = 0; i < 17; i++) { Rec q = random_rec(); all_push(&q); }
    all_swap_remove(3);        /* 마지막(16) 이 3 번으로 — 블록 2 → 블록 0 */
    CHECK(invariants_ok());
    all_swap_remove(15);       /* 15 → 마지막(15) 삭제 */
    CHECK(invariants_ok());
    all_swap_remove(0);
    CHECK(invariants_ok());

    /* 패딩: sizeof 를 규칙으로 재현한다. */
    Field bad[]  = {{1, 1}, {8, 8}, {1, 1}, {4, 4}, {2, 2}};
    Field good[] = {{8, 8}, {4, 4}, {2, 2}, {1, 1}, {1, 1}};
    Field t1[]   = {{1, 1}, {4, 4}, {1, 1}};
    Field t2[]   = {{2, 2}, {1, 1}};
    CHECK(layout_size(bad, 5) == sizeof(BadOrder) && layout_size(bad, 5) == 32u);
    CHECK(layout_size(good, 5) == sizeof(GoodOrder) && layout_size(good, 5) == 16u);
    CHECK(layout_size(t1, 3) == 12u);          /* 1 + pad3 + 4 + 1 + 꼬리 pad3 */
    CHECK(layout_size(t2, 2) == 4u);           /* 꼬리 패딩: 배열로 쓸 때 다음 원소 정렬 */

    /* 라인 수: 128B(이 맥), 64B, 32B(Cortex-M7) */
    all_init();
    for (uint32_t i = 0; i < CAP; i++) { Rec q = random_rec(); all_push(&q); }
    CHECK(line_switches(&g_aos, aos_x_addr, CAP, 4, 128) == CAP / 2u);       /* 라인 하나에 레코드 2개 */
    CHECK(line_switches(&g_soa, soa_x_addr, CAP, 4, 128) == CAP / 32u);      /* 라인 하나에 x 32개 */
    CHECK(line_switches(&g_aosoa, aosoa_x_addr, CAP, 4, 128) == CAP / 8u);   /* 블록(512B)당 라인 1개 */
    CHECK(line_switches(&g_aos, aos_x_addr, CAP, 4, 32) == CAP);             /* 레코드마다 라인 1개 */
    CHECK(line_switches(&g_aosoa, aosoa_x_addr, CAP, 4, 32) == CAP / 8u);    /* x[8] = 32B = 라인 1개 */
    CHECK(line_switches(&g_soa, soa_x_addr, CAP, 4, 32) == CAP / 8u);        /* W×4 = 라인이면 SoA 와 같다 */
}

static void test_partC(void) {
    printf("--- Part C: 와이어 디코드, 윈도, 리스트\n");
    static uint8_t raw[WIRE_SIZE * 8 + 5];
    /* 프레임 8개를 일부러 홀수 오프셋에 놓는다 (x 가 4 바이트 경계에 안 걸림). */
    uint8_t *p = raw + 1;
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t seq = 100 + i, x = 1000 * (i + 1);
        uint64_t t = 0x1122334455667788ull + i;
        uint16_t temp = (uint16_t)(250 + i);
        for (int k = 0; k < 4; k++) p[k] = (uint8_t)(seq >> (k * 8));
        for (int k = 0; k < 8; k++) p[4 + k] = (uint8_t)(t >> (k * 8));
        for (int k = 0; k < 4; k++) p[12 + k] = (uint8_t)(x >> (k * 8));
        p[16] = (uint8_t)temp; p[17] = (uint8_t)(temp >> 8);
        p[18] = (uint8_t)(i & 1u);
        p += WIRE_SIZE;
    }
    soa_init(&g_soa);
    /* 끝에 조각 3 바이트가 남는다. */
    CHECK(wire_decode(&g_soa, raw + 1, WIRE_SIZE * 8 + 3) == 8u);
    CHECK(g_soa.n == 8u);
    Rec o;
    soa_get(&g_soa, 2, &o);
    CHECK(o.seq == 102u && o.t_us == 0x1122334455667788ull + 2u && o.x == 3000u);
    CHECK(o.temp == 252u && o.flags == 0u && o.y == 0u && o.health == 0u && o.cal[3] == 0u);
    soa_get(&g_soa, 7, &o);
    CHECK(o.seq == 107u && o.x == 8000u && o.temp == 257u && o.flags == 1u);
    CHECK(wire_decode(&g_soa, raw + 1, 18) == 0u && g_soa.n == 8u);     /* 프레임 미만 */

    /* 가득 참: CAP 에서 멈춘다. */
    soa_init(&g_soa);
    static uint8_t many[WIRE_SIZE * (CAP + 3)];
    CHECK(wire_decode(&g_soa, many, sizeof many) == CAP && g_soa.n == CAP);

    /* 윈도 통계 */
    soa_init(&g_soa);
    uint32_t xs[] = {5, 1, 9, 9, 3};
    for (int i = 0; i < 5; i++) { Rec q; memset(&q, 0, sizeof q); q.x = xs[i]; soa_push(&g_soa, &q); }
    WinStats st;
    CHECK(soa_window_stats(&g_soa, 1, 3, &st) && st.min == 1u && st.max == 9u && st.sum == 19u);
    CHECK(soa_window_stats(&g_soa, 0, 5, &st) && st.min == 1u && st.max == 9u && st.sum == 27u);
    CHECK(!soa_window_stats(&g_soa, 3, 3, &st));                        /* 범위 초과 */
    CHECK(!soa_window_stats(&g_soa, 2, 0, &st));                        /* 빈 윈도 */
    CHECK(!soa_window_stats(&g_soa, 5, 1, &st));
    CHECK(!soa_window_stats(&g_soa, 1, UINT32_MAX, &st));               /* first+count 가 감기는 경우 */

    /* 리스트: 할당 순서 vs 뒤섞인 순서 — 합은 같고, 라인 전환 수만 다르다. */
    static Node pool[CAP];
    static uint32_t order[CAP];
    uint32_t sum = 0;
    for (uint32_t i = 0; i < CAP; i++) {
        pool[i].val = i * 7u + 3u;
        sum += pool[i].val;
        order[i] = i;
    }
    Node *seq = list_link(pool, order, CAP);
    CHECK(seq == &pool[0] && list_sum(seq) == sum);
    uint32_t sw_seq = list_line_switches(seq, 128);
    rng_s = 0x12345678u;
    for (uint32_t i = CAP - 1; i > 0; i--) {
        uint32_t j = rnd() % (i + 1);
        uint32_t t = order[i]; order[i] = order[j]; order[j] = t;
    }
    Node *shuf = list_link(pool, order, CAP);
    CHECK(list_sum(shuf) == sum);
    uint32_t sw_shuf = list_line_switches(shuf, 128);
    printf("  list line switches: in-order %u, shuffled %u\n", sw_seq, sw_shuf);
    CHECK(sw_seq <= CAP / 2u + 1u);
    CHECK(sw_shuf >= sw_seq * 3u / 2u);
    CHECK(list_link(pool, order, 0) == NULL && list_sum(NULL) == 0u);
}

static void test_random(void) {
    printf("--- 랜덤 연산 vs 레퍼런스\n");
    /* 작은 n: 매 연산마다 전수 검사 */
    all_init();
    rng_s = 0xC0FFEE11u;
    bool ok = true;
    for (uint32_t step = 0; step < 4000u && ok; step++) {
        uint32_t op = rnd() % 6u;
        if (op <= 1u && g_ref_n < 40u) { Rec q = random_rec(); all_push(&q); }
        else if (op == 2u && g_ref_n > 0u) all_swap_remove(rnd() % g_ref_n);
        else if (op == 3u) all_update_all((rnd() % 7u) + 1u);
        else if (op == 4u) all_update_pos();
        else if (op == 5u && g_ref_n > 0u) {
            uint32_t i = rnd() % g_ref_n;
            Rec q = random_rec();
            aos_set(&g_aos, i, &q); soa_set(&g_soa, i, &q); aosoa_set(&g_aosoa, i, &q);
            g_ref[i] = q;
        }
        ok = invariants_ok();
    }
    CHECK(ok);

    /* n ≈ 1024: K = 1024 연산마다 + 마지막에 전수 검사 */
    all_init();
    rng_s = 0x0BADF00Du;
    uint32_t ops = 300000u;
    ok = true;
    for (uint32_t step = 0; step < ops && ok; step++) {
        uint32_t op = rnd() % 32u;
        if (op <= 13u && g_ref_n < CAP) { Rec q = random_rec(); all_push(&q); }
        else if (op <= 20u && g_ref_n > 0u) all_swap_remove(rnd() % g_ref_n);
        else if (op == 21u) all_update_pos();
        else if (op == 22u) all_update_all(1u);
        else if (g_ref_n > 0u) {
            uint32_t i = rnd() % g_ref_n;
            Rec q = random_rec();
            aos_set(&g_aos, i, &q); soa_set(&g_soa, i, &q); aosoa_set(&g_aosoa, i, &q);
            g_ref[i] = q;
        }
        if ((step & 1023u) == 0u) ok = invariants_ok();
    }
    CHECK(ok && invariants_ok());
}

int main(void) {
    memset(&g_poison, POISON, sizeof g_poison);
    printf("sizeof Rec=%zu Block=%zu Aos=%zu Soa=%zu Aosoa=%zu\n",
           sizeof(Rec), sizeof(Block), sizeof(Aos), sizeof(Soa), sizeof(Aosoa));
    test_partA();
    test_partB();
    test_partC();
    test_random();
    /* 체크섬 일치 (같은 입력, 다른 컨테이너 인스턴스) */
    all_init();
    aos_init(&g_aos2); soa_init(&g_soa2); aosoa_init(&g_aosoa2);
    rng_s = 0xFEEDBEEFu;
    for (uint32_t i = 0; i < 977u; i++) {
        Rec q = random_rec();
        aos_push(&g_aos2, &q); soa_push(&g_soa2, &q); aosoa_push(&g_aosoa2, &q);
    }
    for (uint32_t i = 0; i < 25u; i++) {
        aos_update_all(&g_aos2, i); soa_update_all(&g_soa2, i); aosoa_update_all(&g_aosoa2, i);
    }
    uint64_t ha = FNV0, hs = FNV0, hb = FNV0;
    for (uint32_t i = 0; i < 977u; i++) {
        Rec a, s, b;
        aos_get(&g_aos2, i, &a); soa_get(&g_soa2, i, &s); aosoa_get(&g_aosoa2, i, &b);
        ha = fnv_rec(ha, &a); hs = fnv_rec(hs, &s); hb = fnv_rec(hb, &b);
    }
    printf("  checksum aos=%016llx soa=%016llx aosoa=%016llx\n",
           (unsigned long long)ha, (unsigned long long)hs, (unsigned long long)hb);
    CHECK(ha == hs && hs == hb);
    CHECK(aos_sum_x(&g_aos2) == soa_sum_x(&g_soa2) && soa_sum_x(&g_soa2) == aosoa_sum_x(&g_aosoa2));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
