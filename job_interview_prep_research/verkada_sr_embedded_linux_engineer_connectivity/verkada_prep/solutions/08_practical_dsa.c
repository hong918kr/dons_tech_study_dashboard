// 08_practical_dsa.c  —  REFERENCE SOLUTION
// 실전 자료구조 (이벤트·로그 처리) (Practical Data Structures for Event & Log Processing)  —  Q69~Q78
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra -pthread 08_practical_dsa.c -o /tmp/vk_practical_dsa && /tmp/vk_practical_dsa
// Verkada 에서 실제로 보고된 코딩 문제(LRU, Merge Intervals, Non-overlapping Intervals,
// Extract IP, Camera Logs API, Camera States, Filter Alerts, top-k)는 퍼즐이 아니라
// "스트림·로그·인터벌" 모양이다. 여기서는 그 형태를 게이트웨이/카메라 fleet 시나리오로
// 다시 쓰되, 임베디드 습관대로 malloc 없이 고정 크기 풀 + 직접 구현한 해시/힙으로 푼다.
// ---------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>      // qsort (in-place, 추가 할당 없음)
#include <string.h>      // strcmp / memcpy / memset
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ===========================================================================
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ===========================================================================
// 공용 타입 — 디바이스 상태 (Q73, Q78 에서 함께 쓴다)
// ===========================================================================
typedef enum {
    DEV_ONLINE   = 0,
    DEV_OFFLINE  = 1,
    DEV_DEGRADED = 2,
    DEV_STATE_COUNT = 3
} DevState;

// ===========================================================================
// Q69. LRU 캐시 (해시 + 이중 연결 리스트, 고정 용량, 노드 풀)
// ---------------------------------------------------------------------------
// Command 게이트웨이가 디바이스 메타데이터(마지막 상태/펌웨어 버전)를 캐시한다.
// 용량이 고정이고 heap fragmentation 을 피해야 하므로 malloc 을 반복하지 않는다.
// 노드는 정적 풀에서 free list 로 꺼내 쓰고, 링크는 포인터가 아니라 배열 인덱스다.
// 해시는 버킷 체이닝을 노드 안(hnext)에 심어 별도 할당이 전혀 없다.
// get/put 모두 평균 O(1): 해시 조회 O(1) + 리스트 재연결 O(1).
// ===========================================================================
#define LRU_MAX_CAP   64
#define LRU_BUCKETS   128           /* 2의 거듭제곱 → & 마스킹으로 모듈로 대체 */
#define LRU_NIL       (-1)

typedef struct {
    int  key, val;
    int  prev, next;   /* LRU 이중 연결 리스트 (free list 일 때는 next 만 사용) */
    int  hnext;        /* 해시 버킷 체인 */
    bool used;
} LruNode;

typedef struct {
    LruNode  pool[LRU_MAX_CAP];
    int      bucket[LRU_BUCKETS];
    int      head, tail;       /* head = MRU(가장 최근), tail = LRU(가장 오래됨) */
    int      free_head;        /* 노드 풀의 free list */
    int      cap, count;
    unsigned evictions;        /* 조용한 손실 금지 — 항상 센다 */
} LruCache;

static uint32_t lru_mix(int key) {          /* Knuth 곱셈 해시 (음수 키 안전) */
    uint32_t x = (uint32_t)key * 2654435761u;
    return x ^ (x >> 16);
}

void lru_init(LruCache *c, int cap) {
    if (!c) return;
    if (cap < 1) cap = 1;
    if (cap > LRU_MAX_CAP) cap = LRU_MAX_CAP;
    memset(c, 0, sizeof *c);
    c->cap = cap;
    c->head = c->tail = LRU_NIL;
    for (int i = 0; i < LRU_BUCKETS; i++) c->bucket[i] = LRU_NIL;
    for (int i = 0; i < cap; i++) {         /* free list 를 미리 엮어둔다 */
        c->pool[i].next = (i + 1 < cap) ? i + 1 : LRU_NIL;
        c->pool[i].used = false;
    }
    c->free_head = 0;
}

/* --- 노드 풀 --------------------------------------------------------------- */
static int lru_alloc(LruCache *c) {
    int i = c->free_head;
    if (i == LRU_NIL) return LRU_NIL;
    c->free_head = c->pool[i].next;
    c->pool[i].used = true;
    return i;
}
static void lru_release(LruCache *c, int i) {
    c->pool[i].used = false;
    c->pool[i].next = c->free_head;
    c->free_head = i;
}

/* --- 이중 연결 리스트 ------------------------------------------------------- */
static void lru_unlink(LruCache *c, int i) {
    int p = c->pool[i].prev, n = c->pool[i].next;
    if (p != LRU_NIL) c->pool[p].next = n; else c->head = n;
    if (n != LRU_NIL) c->pool[n].prev = p; else c->tail = p;
    c->pool[i].prev = c->pool[i].next = LRU_NIL;
}
static void lru_push_front(LruCache *c, int i) {
    c->pool[i].prev = LRU_NIL;
    c->pool[i].next = c->head;
    if (c->head != LRU_NIL) c->pool[c->head].prev = i;
    c->head = i;
    if (c->tail == LRU_NIL) c->tail = i;
}

/* --- 해시 버킷 체인 --------------------------------------------------------- */
static int lru_find(const LruCache *c, int key) {
    int b = (int)(lru_mix(key) & (LRU_BUCKETS - 1));
    for (int i = c->bucket[b]; i != LRU_NIL; i = c->pool[i].hnext)
        if (c->pool[i].key == key) return i;
    return LRU_NIL;
}
static void lru_hash_insert(LruCache *c, int i) {
    int b = (int)(lru_mix(c->pool[i].key) & (LRU_BUCKETS - 1));
    c->pool[i].hnext = c->bucket[b];
    c->bucket[b] = i;
}
static void lru_hash_remove(LruCache *c, int i) {
    int b = (int)(lru_mix(c->pool[i].key) & (LRU_BUCKETS - 1));
    int *link = &c->bucket[b];
    while (*link != LRU_NIL) {
        if (*link == i) { *link = c->pool[i].hnext; return; }
        link = &c->pool[*link].hnext;
    }
}

/* 있으면 *out 에 값을 쓰고 MRU 로 올린 뒤 true. 없으면 false. 평균 O(1). */
bool lru_get(LruCache *c, int key, int *out) {
    if (!c || c->cap == 0) return false;
    int i = lru_find(c, key);
    if (i == LRU_NIL) return false;
    lru_unlink(c, i);
    lru_push_front(c, i);
    if (out) *out = c->pool[i].val;
    return true;
}

/* 삽입/갱신. 가득 차면 tail(LRU)을 축출한다. 평균 O(1). */
void lru_put(LruCache *c, int key, int val) {
    if (!c || c->cap == 0) return;
    int i = lru_find(c, key);
    if (i != LRU_NIL) {                      /* 갱신도 "사용"이므로 MRU 로 */
        c->pool[i].val = val;
        lru_unlink(c, i);
        lru_push_front(c, i);
        return;
    }
    if (c->count == c->cap) {                /* 축출: tail 부터 */
        int victim = c->tail;
        lru_hash_remove(c, victim);
        lru_unlink(c, victim);
        lru_release(c, victim);
        c->count--;
        c->evictions++;
    }
    i = lru_alloc(c);
    if (i == LRU_NIL) return;                /* 여기 도달하면 불변식 위반 */
    c->pool[i].key = key;
    c->pool[i].val = val;
    lru_hash_insert(c, i);
    lru_push_front(c, i);
    c->count++;
}

/* 디버그/테스트용: MRU→LRU 순서로 키를 훑는다. O(count). */
size_t lru_keys_mru_first(const LruCache *c, int *out, size_t out_cap) {
    size_t n = 0;
    if (!c || !out) return 0;
    for (int i = c->head; i != LRU_NIL && n < out_cap; i = c->pool[i].next)
        out[n++] = c->pool[i].key;
    return n;
}

// ===========================================================================
// Q70. 인터벌 병합 — 디바이스 오프라인 구간 합치기
// ---------------------------------------------------------------------------
// GW31-E 는 오프라인 30분이면 자가 재부팅한다. 여러 소스(heartbeat 누락, 모뎀 로그,
// Command ping)가 겹치는 오프라인 구간을 보고하므로 하나로 합쳐야 총 다운타임이 나온다.
// 경계가 맞닿는 구간([100,200],[200,260])도 끊긴 적이 없으므로 병합한다.
// 시작 시각으로 정렬 후 한 번 훑는다: O(n log n) time, O(1) 추가 공간(in-place).
// ===========================================================================
typedef struct { long start, end; } Interval;   /* [start, end] 닫힌 구간(초) */

static int iv_cmp_start(const void *pa, const void *pb) {
    const Interval *a = (const Interval *)pa, *b = (const Interval *)pb;
    if (a->start != b->start) return (a->start < b->start) ? -1 : 1;
    if (a->end   != b->end)   return (a->end   < b->end)   ? -1 : 1;
    return 0;
}

/* iv 를 in-place 로 정렬·병합하고 병합 후 개수를 반환한다. */
size_t merge_intervals(Interval *iv, size_t n) {
    if (!iv || n == 0) return 0;
    qsort(iv, n, sizeof *iv, iv_cmp_start);
    size_t w = 0;                                  /* 쓰기 커서 = 현재 병합 중인 구간 */
    for (size_t r = 1; r < n; r++) {
        if (iv[r].start <= iv[w].end) {            /* 겹치거나 맞닿음 → 흡수 */
            if (iv[r].end > iv[w].end) iv[w].end = iv[r].end;
        } else {
            iv[++w] = iv[r];                       /* 새 구간 시작 */
        }
    }
    return w + 1;
}

/* 병합된 구간들의 총 길이(초). 닫힌 구간이지만 다운타임은 end-start 로 센다. */
long merged_total_seconds(const Interval *iv, size_t n) {
    long sum = 0;
    for (size_t i = 0; i < n; i++) sum += iv[i].end - iv[i].start;
    return sum;
}

// ===========================================================================
// Q71. 겹치지 않게 만들기 — 제거해야 할 최소 인터벌 수
// ---------------------------------------------------------------------------
// 게이트웨이 유지보수 창(펌웨어 OTA, 채널 스캔)은 동시에 하나만 실행할 수 있다.
// 요청된 창 목록에서 최소 개수만 취소해 겹침을 없애야 한다.
// 그리디: "끝나는 시각"이 빠른 것부터 유지하면 남은 시간이 최대가 된다.
// 여기서는 맞닿는 구간([1,2],[2,3])은 겹치지 않는 것으로 본다(Q70 과 반대 규약).
// O(n log n) time, O(1) 추가 공간.
// ===========================================================================
static int iv_cmp_end(const void *pa, const void *pb) {
    const Interval *a = (const Interval *)pa, *b = (const Interval *)pb;
    if (a->end   != b->end)   return (a->end   < b->end)   ? -1 : 1;
    if (a->start != b->start) return (a->start < b->start) ? -1 : 1;
    return 0;
}

/* 겹침이 사라지도록 제거해야 하는 최소 인터벌 개수. iv 는 정렬되어 바뀐다. */
size_t erase_overlap_intervals(Interval *iv, size_t n) {
    if (!iv || n <= 1) return 0;
    qsort(iv, n, sizeof *iv, iv_cmp_end);
    size_t removed = 0;
    long last_end = iv[0].end;                 /* 가장 빨리 끝나는 창을 무조건 유지 */
    for (size_t i = 1; i < n; i++) {
        if (iv[i].start < last_end) removed++; /* 겹침 → 끝이 더 늦은 이쪽을 버린다 */
        else last_end = iv[i].end;
    }
    return removed;
}

// ===========================================================================
// Q72. top-K 이벤트 — 크기 K 의 min-heap 으로 상위 K개만 유지
// ---------------------------------------------------------------------------
// fleet 대시보드는 "지금 가장 심각한 카메라 10대"만 보여준다. 이벤트가 수만 건이어도
// 전체 정렬(O(n log n))은 낭비다. 크기 K 힙이면 O(n log K) time, O(K) space.
// 힙 저장소로 호출자가 준 out[] 을 그대로 쓰므로 할당이 0이다.
// 동점 처리 규칙: score 가 같으면 dev_id 가 작은 쪽이 상위(결정적 출력).
// ===========================================================================
typedef struct { uint32_t dev_id; int score; } Event;

/* a 가 b 보다 "상위"인가 (score 내림차순, 동점이면 dev_id 오름차순) */
static bool ev_better(const Event *a, const Event *b) {
    if (a->score != b->score) return a->score > b->score;
    return a->dev_id < b->dev_id;
}
/* min-heap 불변식: 부모는 자식보다 상위가 아니다 → 루트가 "가장 낮은" 이벤트 */
static void ev_sift_up(Event *h, size_t i) {
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (!ev_better(&h[p], &h[i])) break;   /* 부모가 더 낮으면 정상 */
        Event t = h[i]; h[i] = h[p]; h[p] = t;
        i = p;
    }
}
static void ev_sift_down(Event *h, size_t n, size_t i) {
    for (;;) {
        size_t l = 2 * i + 1, r = 2 * i + 2, low = i;
        if (l < n && ev_better(&h[low], &h[l])) low = l;
        if (r < n && ev_better(&h[low], &h[r])) low = r;
        if (low == i) break;
        Event t = h[i]; h[i] = h[low]; h[low] = t;
        i = low;
    }
}
static int ev_cmp_desc(const void *pa, const void *pb) {
    const Event *a = (const Event *)pa, *b = (const Event *)pb;
    if (ev_better(a, b)) return -1;
    if (ev_better(b, a)) return 1;
    return 0;
}

/* 상위 min(n,k) 개를 out 에 내림차순으로 쓰고 개수를 반환. out 은 k 칸 이상. */
size_t top_k_events(const Event *ev, size_t n, size_t k, Event *out) {
    if (!ev || !out || k == 0 || n == 0) return 0;
    size_t cnt = 0;
    for (size_t i = 0; i < n; i++) {
        if (cnt < k) {                          /* 힙이 덜 찼으면 그냥 push */
            out[cnt] = ev[i];
            ev_sift_up(out, cnt);
            cnt++;
        } else if (ev_better(&ev[i], &out[0])) { /* 루트(최하위)보다 나으면 교체 */
            out[0] = ev[i];
            ev_sift_down(out, cnt, 0);
        }
    }
    qsort(out, cnt, sizeof *out, ev_cmp_desc);  /* 보고용으로 상위부터 정렬 */
    return cnt;
}

// ===========================================================================
// Q73. 이벤트 스트림 집계 — 디바이스별 상태 카운트 (고정 크기 오픈 어드레싱 해시맵)
// ---------------------------------------------------------------------------
// "Camera States" 유형: 상태 변경 스트림을 받아 디바이스별로 online/offline/degraded
// 횟수를 센다. STL 이 없으므로 문자열 키 해시맵을 직접 짠다 — FNV-1a + 선형 탐사,
// 슬롯 수는 2의 거듭제곱이라 & 마스킹으로 나눗셈을 피한다(MCU 에서 중요).
// 테이블이 가득 차거나 키가 너무 길면 조용히 버리지 않고 dropped 를 올린다.
// 평균 O(1) per record, O(AGG_SLOTS) space (정적, malloc 없음).
// ===========================================================================
#define AGG_SLOTS    64          /* 2의 거듭제곱 */
#define AGG_DEV_LEN  16          /* "GC31-0042" 형태 (NUL 포함) */

typedef struct {
    char     dev[AGG_DEV_LEN];
    uint32_t counts[DEV_STATE_COUNT];
    bool     used;
} AggSlot;

typedef struct {
    AggSlot  slot[AGG_SLOTS];
    size_t   n_used;
    uint64_t dropped;            /* 포화/잘못된 키로 버린 이벤트 수 */
} Aggregator;

static uint32_t fnv1a(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}

/* 키를 찾으면 (found=true) 그 슬롯을, 없으면 (found=false) 삽입할 빈 슬롯을 반환.
   테이블이 포화면 AGG_SLOTS 를 반환한다. 읽기 전용이라 get/record 가 공유한다. */
static size_t agg_probe(const AggSlot *slots, const char *dev, bool *found) {
    size_t i = (size_t)fnv1a(dev) & (AGG_SLOTS - 1);
    for (size_t p = 0; p < AGG_SLOTS; p++) {
        size_t j = (i + p) & (AGG_SLOTS - 1);
        if (!slots[j].used)                 { *found = false; return j; }
        if (strcmp(slots[j].dev, dev) == 0) { *found = true;  return j; }
    }
    *found = false;
    return AGG_SLOTS;                        /* 포화 */
}

void agg_init(Aggregator *a) { if (a) memset(a, 0, sizeof *a); }

/* 이벤트 1건 반영. 성공 true, 버리면 false(dropped++). */
bool agg_record(Aggregator *a, const char *dev, DevState st) {
    if (!a || !dev) return false;
    if (st < 0 || st >= DEV_STATE_COUNT) { a->dropped++; return false; }
    size_t len = strlen(dev);
    if (len == 0 || len >= AGG_DEV_LEN) { a->dropped++; return false; }  /* 잘림 금지 */

    bool found = false;
    size_t j = agg_probe(a->slot, dev, &found);
    if (j == AGG_SLOTS) { a->dropped++; return false; }
    if (!found) {
        memcpy(a->slot[j].dev, dev, len + 1);
        a->slot[j].used = true;
        a->n_used++;
    }
    a->slot[j].counts[st]++;
    return true;
}

/* 디바이스별 카운트 조회. 없으면 false. */
bool agg_get(const Aggregator *a, const char *dev, uint32_t out[DEV_STATE_COUNT]) {
    if (!a || !dev) return false;
    bool found = false;
    size_t j = agg_probe(a->slot, dev, &found);
    if (!found || j == AGG_SLOTS) return false;
    if (out) for (int s = 0; s < DEV_STATE_COUNT; s++) out[s] = a->slot[j].counts[s];
    return true;
}

// ===========================================================================
// Q74. 토큰 버킷 rate limiter — 업로드/알람 폭주 억제
// ---------------------------------------------------------------------------
// 링크가 살아날 때 큐에 쌓인 알람이 한꺼번에 터지면 LTE 업링크를 잡아먹는다.
// 토큰 버킷은 "평균 R/초, 순간 최대 C개"를 한 줄로 표현한다.
// 부동소수점 없이 milli-token(1/1000 토큰) 정수로 계산 → MCU 에서도 그대로 쓴다.
// 시간을 인자로 주입(now_ms)해 테스트가 sleep 없이 결정적이다 — modular/testable 설계.
// O(1) time, O(1) space.
// ===========================================================================
typedef struct {
    uint64_t tokens_milli;    /* 현재 토큰 * 1000 */
    uint64_t cap_milli;       /* 버스트 상한 * 1000 */
    uint32_t refill_per_sec;  /* 초당 충전 토큰 수 */
    uint64_t last_ms;
    uint64_t allowed, rejected;
} TokenBucket;

void tb_init(TokenBucket *tb, uint32_t capacity, uint32_t refill_per_sec, uint64_t now_ms) {
    if (!tb) return;
    memset(tb, 0, sizeof *tb);
    tb->cap_milli      = (uint64_t)capacity * 1000u;
    tb->tokens_milli   = tb->cap_milli;      /* 처음엔 가득 = 즉시 버스트 허용 */
    tb->refill_per_sec = refill_per_sec;
    tb->last_ms        = now_ms;
}

/* 경과 시간만큼 충전. 1ms 당 refill_per_sec milli-token (= rate/1000 토큰). */
static void tb_refill(TokenBucket *tb, uint64_t now_ms) {
    if (now_ms <= tb->last_ms) { tb->last_ms = now_ms; return; }  /* 시계 역행 → 재동기화만 */
    uint64_t elapsed = now_ms - tb->last_ms;
    tb->tokens_milli += elapsed * (uint64_t)tb->refill_per_sec;
    if (tb->tokens_milli > tb->cap_milli) tb->tokens_milli = tb->cap_milli;
    tb->last_ms = now_ms;
}

/* cost 개의 토큰을 요구한다. 있으면 소비하고 true, 없으면 false(rejected++). */
bool tb_allow(TokenBucket *tb, uint64_t now_ms, uint32_t cost) {
    if (!tb) return false;
    tb_refill(tb, now_ms);
    uint64_t need = (uint64_t)cost * 1000u;
    if (tb->tokens_milli < need) { tb->rejected++; return false; }
    tb->tokens_milli -= need;
    tb->allowed++;
    return true;
}

/* 현재 사용 가능한 (내림) 토큰 수 — 테스트/텔레메트리용. */
uint32_t tb_available(const TokenBucket *tb) {
    return tb ? (uint32_t)(tb->tokens_milli / 1000u) : 0u;
}

// ===========================================================================
// Q75. 타임스탬프 정렬 병합 — 두 개의 정렬된 로그 스트림을 하나로
// ---------------------------------------------------------------------------
// 게이트웨이 로컬 로그와 카메라 업로드 로그를 시간순 하나의 뷰로 합친다("Camera Logs API").
// 둘 다 이미 정렬되어 있으므로 다시 정렬하지 말고 two-pointer 로 한 번에 훑는다.
// 안정성(stable): 타임스탬프가 같으면 스트림 A 를 먼저 — 재실행해도 같은 순서가 나온다.
// out_cap 을 넘으면 잘라내고 쓴 개수만 반환(고정 버퍼 안전).
// O(na + nb) time, O(1) 추가 공간.
// ===========================================================================
typedef struct { uint64_t ts; uint32_t dev; int code; } LogRec;

size_t merge_logs(const LogRec *a, size_t na, const LogRec *b, size_t nb,
                  LogRec *out, size_t out_cap) {
    if (!out || out_cap == 0) return 0;
    if (!a) na = 0;
    if (!b) nb = 0;
    size_t i = 0, j = 0, w = 0;
    while (i < na && j < nb && w < out_cap) {
        if (b[j].ts < a[i].ts) out[w++] = b[j++];   /* 동점(==)이면 a 우선 = stable */
        else                   out[w++] = a[i++];
    }
    while (i < na && w < out_cap) out[w++] = a[i++];
    while (j < nb && w < out_cap) out[w++] = b[j++];
    return w;
}

// ===========================================================================
// Q76. 최근 N개 기반 중복 제거 — 같은 알람이 반복될 때 억제
// ---------------------------------------------------------------------------
// 링크 플래핑 중이면 "device offline" 알람이 초당 수십 번 올라온다. 최근 N개의
// 서로 다른 알람 키만 기억해 그 안에 있으면 억제한다(N 개를 지나면 다시 통과).
// 순환 버퍼(FIFO 창) + 해시(멤버십 O(1))의 조합. 링 슬롯 자체가 해시 체인 노드라
// 할당이 0이고, 창에서 밀려난 항목은 해시에서도 정확히 제거된다.
// 평균 O(1) time, O(N) space.
// ===========================================================================
#define DEDUP_WINDOW 8           /* 최근 몇 개의 서로 다른 알람을 기억하나 */
#define DEDUP_SLOTS  32          /* 2의 거듭제곱, 여유롭게 */
#define DEDUP_NIL    (-1)

typedef struct { uint32_t key; int hnext; bool used; } DedupEntry;
typedef struct {
    DedupEntry ring[DEDUP_WINDOW];
    int        bucket[DEDUP_SLOTS];
    size_t     pos;              /* 다음에 덮어쓸 링 슬롯 */
    uint64_t   emitted, suppressed;
} Dedup;

static uint32_t dd_mix(uint32_t x) { x *= 2654435761u; return x ^ (x >> 16); }

static void dd_bucket_remove(Dedup *d, int idx) {
    int b = (int)(dd_mix(d->ring[idx].key) & (DEDUP_SLOTS - 1));
    int *link = &d->bucket[b];
    while (*link != DEDUP_NIL) {
        if (*link == idx) { *link = d->ring[idx].hnext; return; }
        link = &d->ring[*link].hnext;
    }
}

void dedup_init(Dedup *d) {
    if (!d) return;
    memset(d, 0, sizeof *d);
    for (int i = 0; i < DEDUP_SLOTS; i++) d->bucket[i] = DEDUP_NIL;
    for (int i = 0; i < DEDUP_WINDOW; i++) { d->ring[i].used = false; d->ring[i].hnext = DEDUP_NIL; }
}

/* 최근 창에 없으면 등록하고 true(=내보낸다). 있으면 false(=억제). */
bool dedup_should_emit(Dedup *d, uint32_t key) {
    if (!d) return false;
    int b = (int)(dd_mix(key) & (DEDUP_SLOTS - 1));
    for (int i = d->bucket[b]; i != DEDUP_NIL; i = d->ring[i].hnext) {
        if (d->ring[i].key == key) { d->suppressed++; return false; }
    }
    int slot = (int)d->pos;
    if (d->ring[slot].used) dd_bucket_remove(d, slot);   /* 창 밖으로 밀려난 키 제거 */
    d->ring[slot].key   = key;
    d->ring[slot].used  = true;
    d->ring[slot].hnext = d->bucket[b];
    d->bucket[b] = slot;
    d->pos = (d->pos + 1) % DEDUP_WINDOW;
    d->emitted++;
    return true;
}

// ===========================================================================
// Q77. 시간 범위 쿼리 — 정렬된 타임스탬프 배열에서 구간 추출
// ---------------------------------------------------------------------------
// "지난 10분의 로그를 줘" — 로그는 append-only 라 이미 시간순이다. 선형 스캔 대신
// lower_bound(첫 >= lo) / upper_bound(첫 > hi) 로 O(log n) 에 구간을 잘라낸다.
// 중복 타임스탬프가 있어도 두 경계가 구간 전체를 정확히 감싼다.
// O(log n) time, O(1) space.
// ===========================================================================

/* ts[0..n) 중 key 이상인 첫 인덱스 (없으면 n) */
size_t ts_lower_bound(const uint64_t *ts, size_t n, uint64_t key) {
    size_t lo = 0, hi = n;                     /* 답은 항상 [lo, hi] 안에 있다 */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;       /* 오버플로 없는 중점 */
        if (ts[mid] < key) lo = mid + 1;
        else               hi = mid;
    }
    return lo;
}

/* ts[0..n) 중 key 초과인 첫 인덱스 (없으면 n) */
size_t ts_upper_bound(const uint64_t *ts, size_t n, uint64_t key) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (ts[mid] <= key) lo = mid + 1;
        else                hi = mid;
    }
    return lo;
}

/* [lo, hi] 닫힌 구간에 드는 원소 개수. *out_begin 에 시작 인덱스. */
size_t ts_range(const uint64_t *ts, size_t n, uint64_t lo, uint64_t hi, size_t *out_begin) {
    if (out_begin) *out_begin = 0;
    if (!ts || n == 0 || lo > hi) return 0;
    size_t b = ts_lower_bound(ts, n, lo);
    size_t e = ts_upper_bound(ts, n, hi);
    if (out_begin) *out_begin = b;
    return (e > b) ? (e - b) : 0;
}

// ===========================================================================
// Q78. 로그 라인 파싱 — "ts=... dev=... state=... rssi=..." 를 구조체로
// ---------------------------------------------------------------------------
// 모뎀/에이전트가 뱉는 key=value 한 줄을 구조체로 바꾼다. 입력은 신뢰할 수 없다 —
// 필드 누락, 모르는 키, 중복 키, 숫자가 아닌 값, 범위를 벗어난 RSSI, 너무 긴 dev 는
// 전부 거부한다(부분 파싱 결과를 out 에 남기지 않는다: 임시 구조체에 채운 뒤 커밋).
// 입력 버퍼를 변형하지 않도록 strtok_r 대신 const 포인터 수동 스캔을 쓴다.
// O(L) time (L = 줄 길이), O(1) space.
// ===========================================================================
#define LOG_DEV_LEN 16

typedef struct {
    uint64_t ts;
    char     dev[LOG_DEV_LEN];
    DevState state;
    int      rssi;
} LogLine;

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}
static bool key_is(const char *s, const char *e, const char *lit) {
    size_t len = (size_t)(e - s);
    return len == strlen(lit) && memcmp(s, lit, len) == 0;
}
/* [s,e) 를 부호 없는 10진수로. 빈 문자열/비숫자/오버플로는 거부. */
static bool parse_u64_range(const char *s, const char *e, uint64_t *out) {
    if (s >= e) return false;
    uint64_t v = 0;
    for (const char *p = s; p < e; p++) {
        if (*p < '0' || *p > '9') return false;
        uint64_t d = (uint64_t)(*p - '0');
        if (v > (UINT64_MAX - d) / 10u) return false;
        v = v * 10u + d;
    }
    *out = v;
    return true;
}
/* [s,e) 를 부호 있는 32비트 정수로. */
static bool parse_i32_range(const char *s, const char *e, int *out) {
    bool neg = false;
    if (s < e && (*s == '-' || *s == '+')) { neg = (*s == '-'); s++; }
    uint64_t mag;
    if (!parse_u64_range(s, e, &mag)) return false;
    if (mag > 2147483647u) return false;
    *out = neg ? -(int)mag : (int)mag;
    return true;
}
static bool parse_state(const char *s, const char *e, DevState *out) {
    static const struct { const char *name; DevState st; } tbl[] = {
        { "online",   DEV_ONLINE   },
        { "offline",  DEV_OFFLINE  },
        { "degraded", DEV_DEGRADED },
    };
    for (size_t i = 0; i < sizeof tbl / sizeof tbl[0]; i++) {
        if (key_is(s, e, tbl[i].name)) { *out = tbl[i].st; return true; }
    }
    return false;
}

/* 성공하면 *out 을 채우고 true. 한 군데라도 이상하면 out 을 건드리지 않고 false. */
bool parse_log_line(const char *line, LogLine *out) {
    if (!line || !out) return false;
    LogLine tmp;
    memset(&tmp, 0, sizeof tmp);
    unsigned seen = 0;                            /* ts=1 dev=2 state=4 rssi=8 */

    const char *p = skip_ws(line);
    while (*p) {
        const char *ks = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '=') p++;
        if (*p != '=') return false;              /* '=' 없는 토큰 */
        const char *ke = p++;                     /* '=' 를 건너뛴다 */
        const char *vs = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        const char *ve = p;
        if (vs == ve) return false;               /* 값이 비었다 */

        if (key_is(ks, ke, "ts")) {
            if (seen & 1u) return false;          /* 중복 키 */
            if (!parse_u64_range(vs, ve, &tmp.ts)) return false;
            seen |= 1u;
        } else if (key_is(ks, ke, "dev")) {
            if (seen & 2u) return false;
            size_t len = (size_t)(ve - vs);
            if (len == 0 || len >= LOG_DEV_LEN) return false;   /* 잘림 대신 거부 */
            memcpy(tmp.dev, vs, len);
            tmp.dev[len] = '\0';
            seen |= 2u;
        } else if (key_is(ks, ke, "state")) {
            if (seen & 4u) return false;
            if (!parse_state(vs, ve, &tmp.state)) return false;
            seen |= 4u;
        } else if (key_is(ks, ke, "rssi")) {
            if (seen & 8u) return false;
            if (!parse_i32_range(vs, ve, &tmp.rssi)) return false;
            if (tmp.rssi > 0 || tmp.rssi < -120) return false;  /* dBm 범위 검증 */
            seen |= 8u;
        } else {
            return false;                          /* 모르는 키 */
        }
        p = skip_ws(p);
    }
    if (seen != 0xFu) return false;                /* 네 필드 모두 필수 */
    *out = tmp;                                    /* 전부 통과한 뒤에만 커밋 */
    return true;
}

// ===========================================================================
// main — 테스트
// ===========================================================================
int main(void) {
    printf("== Q69 LRU 캐시 (해시 + 이중 연결 리스트 + 노드 풀) ==\n");
    {
        LruCache c;
        int v = 0;
        lru_init(&c, 3);
        T("lru_get on empty cache misses", lru_get(&c, 1, &v) == false);
        lru_put(&c, 1, 100);
        lru_put(&c, 2, 200);
        lru_put(&c, 3, 300);
        T("lru_get(1) hits with 100", lru_get(&c, 1, &v) && v == 100);
        lru_put(&c, 4, 400);                        /* 용량 초과 → LRU(=2) 축출 */
        T("lru_get(2) evicted after overflow", lru_get(&c, 2, &v) == false);
        T("lru eviction counter == 1", c.evictions == 1);
        T("lru_get(3) still present", lru_get(&c, 3, &v) && v == 300);
        lru_put(&c, 3, 333);                        /* 갱신 = 사용 → MRU */
        T("lru_put updates existing value", lru_get(&c, 3, &v) && v == 333);
        T("lru count stays at cap", c.count == 3);
        int keys[8] = {0};
        size_t nk = lru_keys_mru_first(&c, keys, 8);
        T("lru MRU order is 3,4,1", nk == 3 && keys[0] == 3 && keys[1] == 4 && keys[2] == 1);

        LruCache one;
        lru_init(&one, 1);                          /* 경계: 용량 1 */
        lru_put(&one, 7, 70);
        lru_put(&one, 8, 80);
        T("cap=1 keeps only newest", !lru_get(&one, 7, &v) && lru_get(&one, 8, &v) && v == 80);

        LruCache reuse;                             /* 노드 풀 재사용: 축출 후에도 빈틈 없음 */
        lru_init(&reuse, 2);
        for (int i = 0; i < 50; i++) lru_put(&reuse, i, i * 10);
        T("node pool survives 50 puts (cap=2)",
          reuse.count == 2 && lru_get(&reuse, 49, &v) && v == 490 && !lru_get(&reuse, 47, &v));
    }

    printf("\n== Q70 인터벌 병합 (디바이스 오프라인 구간) ==\n");
    {
        Interval a[] = { {300, 400}, {100, 200}, {150, 250}, {900, 1000} };
        size_t n = merge_intervals(a, 4);
        T("merge: unsorted input merged to 3", n == 3);
        T("merge: first becomes [100,250]", a[0].start == 100 && a[0].end == 250);
        T("merge: last stays [900,1000]", a[2].start == 900 && a[2].end == 1000);

        Interval touch[] = { {100, 200}, {200, 260} };   /* 경계 접촉 → 병합 */
        T("merge: touching boundaries merge", merge_intervals(touch, 2) == 1 &&
                                              touch[0].start == 100 && touch[0].end == 260);

        Interval nested[] = { {0, 1000}, {10, 20}, {30, 40} };
        T("merge: nested swallowed", merge_intervals(nested, 3) == 1 && nested[0].end == 1000);

        Interval one[] = { {5, 9} };
        T("merge: single interval untouched", merge_intervals(one, 1) == 1 && one[0].end == 9);
        T("merge: empty input -> 0", merge_intervals(NULL, 0) == 0);

        Interval down[] = { {0, 30}, {20, 60}, {100, 130} };
        size_t m = merge_intervals(down, 3);
        T("merge: total downtime = 90s", m == 2 && merged_total_seconds(down, m) == 90);
    }

    printf("\n== Q71 겹치지 않게 만들기 (최소 제거 수) ==\n");
    {
        Interval a[] = { {1, 2}, {2, 3}, {3, 4}, {1, 3} };
        T("erase: classic case -> 1", erase_overlap_intervals(a, 4) == 1);

        Interval b[] = { {1, 2}, {1, 2}, {1, 2} };
        T("erase: three identical -> 2", erase_overlap_intervals(b, 3) == 2);

        Interval c[] = { {1, 2}, {2, 3} };
        T("erase: touching is not overlapping -> 0", erase_overlap_intervals(c, 2) == 0);

        Interval d[] = { {0, 100}, {1, 2}, {3, 4}, {5, 6} };
        T("erase: one greedy window beats the giant -> 1", erase_overlap_intervals(d, 4) == 1);

        Interval e[] = { {7, 9} };
        T("erase: single -> 0", erase_overlap_intervals(e, 1) == 0);
        T("erase: empty -> 0", erase_overlap_intervals(NULL, 0) == 0);
    }

    printf("\n== Q72 top-K 이벤트 (크기 K min-heap) ==\n");
    {
        Event ev[] = {
            {1001, 5}, {1002, 90}, {1003, 12}, {1004, 90},
            {1005, 3}, {1006, 47}, {1007, 47}, {1008, 1},
        };
        Event out[8] = {0};
        size_t n = top_k_events(ev, 8, 3, out);
        T("topk: returns k=3", n == 3);
        T("topk: best is dev1002 (tie -> smaller id)", out[0].dev_id == 1002 && out[0].score == 90);
        T("topk: second is dev1004", out[1].dev_id == 1004 && out[1].score == 90);
        T("topk: third is dev1006 (tie -> smaller id)", out[2].dev_id == 1006 && out[2].score == 47);

        size_t m = top_k_events(ev, 8, 100, out);      /* k > n */
        T("topk: k>n returns n and is sorted desc",
          m == 8 && out[0].score == 90 && out[7].score == 1);

        T("topk: k=0 -> 0", top_k_events(ev, 8, 0, out) == 0);
        T("topk: empty input -> 0", top_k_events(ev, 0, 3, out) == 0);

        Event neg[] = { {1, -5}, {2, -1}, {3, -9} };    /* 음수 score 도 정상 */
        Event o2[3] = {0};
        T("topk: negative scores ordered",
          top_k_events(neg, 3, 2, o2) == 2 && o2[0].dev_id == 2 && o2[1].dev_id == 1);
    }

    printf("\n== Q73 이벤트 스트림 집계 (오픈 어드레싱 해시맵) ==\n");
    {
        Aggregator ag;
        agg_init(&ag);
        agg_record(&ag, "GC31-0042", DEV_ONLINE);
        agg_record(&ag, "GC31-0042", DEV_OFFLINE);
        agg_record(&ag, "GC31-0042", DEV_OFFLINE);
        agg_record(&ag, "GW31-0007", DEV_DEGRADED);

        uint32_t c[DEV_STATE_COUNT] = {0};
        T("agg: GC31-0042 offline count == 2",
          agg_get(&ag, "GC31-0042", c) && c[DEV_OFFLINE] == 2 && c[DEV_ONLINE] == 1);
        T("agg: GW31-0007 degraded count == 1",
          agg_get(&ag, "GW31-0007", c) && c[DEV_DEGRADED] == 1);
        T("agg: unknown device -> false", agg_get(&ag, "NOPE-0000", c) == false);
        T("agg: distinct device count == 2", ag.n_used == 2);
        T("agg: empty key rejected", agg_record(&ag, "", DEV_ONLINE) == false);
        T("agg: over-long key rejected (no truncation)",
          agg_record(&ag, "THIS-KEY-IS-WAY-TOO-LONG", DEV_ONLINE) == false);

        Aggregator full;                               /* 경계: 테이블 포화 */
        agg_init(&full);
        char name[AGG_DEV_LEN];
        bool all_ok = true;
        for (int i = 0; i < AGG_SLOTS; i++) {
            snprintf(name, sizeof name, "D%05d", i);
            if (!agg_record(&full, name, DEV_ONLINE)) all_ok = false;
        }
        T("agg: fills all 64 slots", all_ok && full.n_used == AGG_SLOTS);
        snprintf(name, sizeof name, "D%05d", AGG_SLOTS);
        T("agg: 65th device dropped, counted",
          agg_record(&full, name, DEV_ONLINE) == false && full.dropped == 1);
        T("agg: existing key still updatable when full",
          agg_record(&full, "D00000", DEV_OFFLINE) == true);
    }

    printf("\n== Q74 토큰 버킷 rate limiter (시간 주입) ==\n");
    {
        TokenBucket tb;
        tb_init(&tb, 5, 2, 1000);                      /* 버스트 5, 초당 2개 */
        int ok = 0;
        for (int i = 0; i < 5; i++) if (tb_allow(&tb, 1000, 1)) ok++;
        T("tb: burst of 5 allowed at t=1000", ok == 5);
        T("tb: 6th rejected immediately", tb_allow(&tb, 1000, 1) == false && tb.rejected == 1);
        T("tb: available drained to 0", tb_available(&tb) == 0);

        T("tb: after 500ms only 1 token", tb_allow(&tb, 1500, 1) == true);
        T("tb: and the next one is rejected", tb_allow(&tb, 1500, 1) == false);

        tb_allow(&tb, 100000, 0);                      /* 오래 쉬면 cap 까지만 */
        T("tb: long idle clamps at capacity", tb_available(&tb) == 5);

        T("tb: cost=3 consumes three tokens",
          tb_allow(&tb, 100000, 3) == true && tb_available(&tb) == 2);
        T("tb: cost larger than capacity always rejected", tb_allow(&tb, 100000, 9) == false);

        TokenBucket back;                              /* 경계: 시계 역행 */
        tb_init(&back, 2, 1, 5000);
        tb_allow(&back, 5000, 2);
        T("tb: clock going backwards grants nothing", tb_allow(&back, 4000, 1) == false);
        T("tb: resyncs and still refills forward", tb_allow(&back, 6000, 1) == true);
    }

    printf("\n== Q75 타임스탬프 정렬 병합 (두 로그 스트림) ==\n");
    {
        LogRec a[] = { {10, 1, 100}, {30, 1, 101}, {50, 1, 102} };
        LogRec b[] = { {20, 2, 200}, {40, 2, 201} };
        LogRec out[8] = {0};
        size_t n = merge_logs(a, 3, b, 2, out, 8);
        T("merge_logs: writes 5", n == 5);
        T("merge_logs: timestamps ascending",
          out[0].ts == 10 && out[1].ts == 20 && out[2].ts == 30 && out[3].ts == 40 && out[4].ts == 50);

        LogRec ta[] = { {7, 1, 1}, {7, 1, 2} };        /* 동일 타임스탬프 */
        LogRec tb2[] = { {7, 2, 3} };
        n = merge_logs(ta, 2, tb2, 1, out, 8);
        T("merge_logs: equal ts keeps stream A first",
          n == 3 && out[0].dev == 1 && out[1].dev == 1 && out[2].dev == 2);

        n = merge_logs(a, 3, NULL, 0, out, 8);
        T("merge_logs: empty B copies A", n == 3 && out[2].ts == 50);
        n = merge_logs(NULL, 0, b, 2, out, 8);
        T("merge_logs: empty A copies B", n == 2 && out[0].ts == 20);
        T("merge_logs: both empty -> 0", merge_logs(NULL, 0, NULL, 0, out, 8) == 0);

        n = merge_logs(a, 3, b, 2, out, 2);            /* 경계: 출력 버퍼 초과 */
        T("merge_logs: truncates at out_cap", n == 2 && out[0].ts == 10 && out[1].ts == 20);
    }

    printf("\n== Q76 최근 N개 기반 중복 제거 (순환 버퍼 + 해시) ==\n");
    {
        Dedup d;
        dedup_init(&d);
        T("dedup: first alarm emitted", dedup_should_emit(&d, 0xAAAA) == true);
        T("dedup: immediate repeat suppressed", dedup_should_emit(&d, 0xAAAA) == false);
        T("dedup: different alarm emitted", dedup_should_emit(&d, 0xBBBB) == true);
        T("dedup: suppressed counter == 1", d.suppressed == 1);

        for (uint32_t i = 0; i < DEDUP_WINDOW; i++)    /* 창을 완전히 밀어낸다 */
            dedup_should_emit(&d, 0x1000u + i);
        T("dedup: alarm re-emitted after window rolls over",
          dedup_should_emit(&d, 0xAAAA) == true);

        Dedup f;                                       /* 플래핑 100회 → 1개만 통과 */
        dedup_init(&f);
        for (int i = 0; i < 100; i++) dedup_should_emit(&f, 0xDEAD);
        T("dedup: 100 flaps collapse to 1 emit", f.emitted == 1 && f.suppressed == 99);

        Dedup g;                                       /* 서로 다른 키는 전부 통과 */
        dedup_init(&g);
        bool all = true;
        for (uint32_t i = 0; i < 1000; i++) if (!dedup_should_emit(&g, i)) all = false;
        T("dedup: 1000 distinct keys all emitted", all && g.suppressed == 0);
    }

    printf("\n== Q77 시간 범위 쿼리 (lower_bound / upper_bound) ==\n");
    {
        uint64_t ts[] = { 100, 200, 200, 200, 300, 400, 500 };
        size_t n = sizeof ts / sizeof ts[0];
        T("lower_bound(200) == 1", ts_lower_bound(ts, n, 200) == 1);
        T("upper_bound(200) == 4", ts_upper_bound(ts, n, 200) == 4);
        T("lower_bound(250) == 4 (not present)", ts_lower_bound(ts, n, 250) == 4);
        T("lower_bound(0) == 0, upper_bound(999) == n",
          ts_lower_bound(ts, n, 0) == 0 && ts_upper_bound(ts, n, 999) == n);

        size_t begin = 0;
        T("range [200,300] -> 4 items from idx 1",
          ts_range(ts, n, 200, 300, &begin) == 4 && begin == 1);
        T("range [201,299] -> 0 items", ts_range(ts, n, 201, 299, &begin) == 0);
        T("range [100,500] -> all", ts_range(ts, n, 100, 500, &begin) == n && begin == 0);
        T("range with lo>hi -> 0", ts_range(ts, n, 400, 200, &begin) == 0);
        T("range on empty array -> 0", ts_range(ts, 0, 0, 100, &begin) == 0);
        T("range entirely above data -> 0", ts_range(ts, n, 900, 1000, &begin) == 0);
    }

    printf("\n== Q78 로그 라인 파싱 (key=value, 잘못된 입력 거부) ==\n");
    {
        LogLine L = {0};
        T("parse: valid line",
          parse_log_line("ts=1700000000 dev=GC31-0042 state=offline rssi=-97", &L));
        T("parse: ts field", L.ts == 1700000000ull);
        T("parse: dev field", strcmp(L.dev, "GC31-0042") == 0);
        T("parse: state field", L.state == DEV_OFFLINE);
        T("parse: rssi field", L.rssi == -97);

        T("parse: extra whitespace tolerated",
          parse_log_line("  ts=1  dev=A   state=online\trssi=0 ", &L) && L.state == DEV_ONLINE);
        T("parse: fields may be reordered",
          parse_log_line("rssi=-60 state=degraded dev=B ts=9", &L) &&
          L.state == DEV_DEGRADED && L.ts == 9);

        LogLine keep = L;
        T("parse: missing field rejected",
          parse_log_line("ts=1 dev=A state=online", &L) == false);
        T("parse: out untouched on failure", memcmp(&keep, &L, sizeof L) == 0);
        T("parse: unknown key rejected",
          parse_log_line("ts=1 dev=A state=online rssi=-5 bogus=7", &L) == false);
        T("parse: duplicate key rejected",
          parse_log_line("ts=1 ts=2 dev=A state=online rssi=-5", &L) == false);
        T("parse: non-numeric rssi rejected",
          parse_log_line("ts=1 dev=A state=online rssi=abc", &L) == false);
        T("parse: bad state rejected",
          parse_log_line("ts=1 dev=A state=zombie rssi=-5", &L) == false);
        T("parse: rssi out of range rejected",
          parse_log_line("ts=1 dev=A state=online rssi=-200", &L) == false);
        T("parse: over-long dev rejected",
          parse_log_line("ts=1 dev=ABCDEFGHIJKLMNOPQRSTU state=online rssi=-5", &L) == false);
        T("parse: empty value rejected",
          parse_log_line("ts= dev=A state=online rssi=-5", &L) == false);
        T("parse: token without '=' rejected",
          parse_log_line("ts=1 dev=A state=online rssi=-5 junk", &L) == false);
        T("parse: empty line rejected", parse_log_line("", &L) == false);
        T("parse: NULL rejected", parse_log_line(NULL, &L) == false);
    }

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
