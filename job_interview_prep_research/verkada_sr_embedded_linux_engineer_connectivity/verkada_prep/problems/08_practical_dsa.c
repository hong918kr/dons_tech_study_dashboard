// 08_practical_dsa.c  —  PRACTICE STUB (직접 채워넣기)
// 실전 자료구조 (이벤트·로그 처리) (Practical Data Structures for Event & Log Processing)  —  Q69~Q78
// ---------------------------------------------------------------------------
// 빌드: make prob N=08_practical_dsa
// 각 함수의 '// TODO' 를 구현하고 다시 실행 → [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다.)
//
// Verkada 에서 실제로 보고된 코딩 문제(LRU, Merge Intervals, Non-overlapping Intervals,
// Extract IP, Camera Logs API, Camera States, Filter Alerts, top-k)는 퍼즐이 아니라
// "스트림·로그·인터벌" 모양이다. 여기서는 그 형태를 게이트웨이/카메라 fleet 시나리오로
// 다시 쓰되, 임베디드 습관대로 malloc 없이 고정 크기 풀 + 직접 구현한 해시/힙으로 푼다.
//
// 규칙: **malloc/free 를 쓰지 말 것.** 모든 저장소는 구조체 안의 고정 배열이다.
//       구조체 정의는 API 계약이므로 그대로 두되, 내부 필드는 마음대로 재설계해도 된다
//       (단, main() 의 테스트가 읽는 필드 이름은 유지).
// ---------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>      // qsort (in-place, 추가 할당 없음)
#include <string.h>      // strcmp / memcpy / memset
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ===========================================================================
// 테스트 하네스 (PASS/FAIL) — 건드리지 말 것
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

/* ---------------------------------------------------------------------------
 * Q69.  LRU 캐시 (해시 + 이중 연결 리스트, 노드 풀)
 *   KO: 고정 용량 LRU 캐시를 malloc 없이 구현한다. 노드는 정적 풀에서 free list 로
 *       꺼내 쓰고, prev/next/hnext 는 포인터가 아니라 배열 인덱스다. 해시는 버킷
 *       체이닝(노드 안의 hnext)으로 별도 할당 없이 붙인다. get/put 평균 O(1).
 *       가득 차면 tail(LRU)을 축출하고 evictions 를 센다.
 *   EN: Fixed-capacity LRU cache with zero malloc: hash bucket chaining + an
 *       intrusive doubly linked list, both over one static node pool. O(1) ops.
 *   ex: cap=3, put(1,2,3), get(1), put(4) -> key 2 evicted, MRU order = 1,4,3... then 3,4,1
 * ------------------------------------------------------------------------- */
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

void lru_init(LruCache *c, int cap) {
    (void)cap;
    if (c) memset(c, 0, sizeof *c);
    // TODO: cap 을 [1, LRU_MAX_CAP] 로 클램프, head/tail = LRU_NIL,
    //       bucket[] 전부 LRU_NIL, pool[i].next 로 free list 를 엮고 free_head = 0.
}

/* 있으면 *out 에 값을 쓰고 MRU 로 올린 뒤 true. 없으면 false. 평균 O(1). */
bool lru_get(LruCache *c, int key, int *out) {
    (void)c; (void)key; (void)out;
    // TODO: 해시 버킷에서 key 를 찾고 → 리스트에서 unlink → push_front → 값 복사.
    return false;
}

/* 삽입/갱신. 가득 차면 tail(LRU)을 축출한다. 평균 O(1). */
void lru_put(LruCache *c, int key, int val) {
    (void)c; (void)key; (void)val;
    // TODO: 이미 있으면 값만 갱신 후 MRU 로.
    //       없고 가득 찼으면 tail 을 해시에서 제거 + unlink + free list 로 반납(evictions++).
    //       그 다음 free list 에서 노드를 꺼내 채우고 해시 삽입 + push_front.
}

/* 디버그/테스트용: MRU→LRU 순서로 키를 훑는다. O(count). */
size_t lru_keys_mru_first(const LruCache *c, int *out, size_t out_cap) {
    (void)c; (void)out; (void)out_cap;
    // TODO: head 부터 next 를 따라가며 out_cap 까지 채우고 개수를 반환.
    return 0;
}

/* ---------------------------------------------------------------------------
 * Q70.  인터벌 병합 — 디바이스 오프라인 구간 합치기
 *   KO: [start,end] 닫힌 구간 배열을 in-place 로 정렬·병합하고 병합 후 개수를 반환.
 *       여러 소스가 보고한 오프라인 구간의 총 다운타임을 구하기 위한 전처리다.
 *       경계가 맞닿는 구간([100,200],[200,260])도 끊긴 적이 없으므로 병합한다.
 *   EN: Sort by start, then sweep once merging overlapping/touching intervals in place.
 *   ex: {{300,400},{100,200},{150,250},{900,1000}} -> 3개: [100,250],[300,400],[900,1000]
 * ------------------------------------------------------------------------- */
typedef struct { long start, end; } Interval;   /* [start, end] 닫힌 구간(초) */

size_t merge_intervals(Interval *iv, size_t n) {
    (void)iv; (void)n;
    // TODO: qsort(start 오름차순) 후 쓰기 커서 w 를 두고 iv[r].start <= iv[w].end 면 흡수.
    return 0;
}

/* 병합된 구간들의 총 길이(초). */
long merged_total_seconds(const Interval *iv, size_t n) {
    (void)iv; (void)n;
    // TODO: end - start 의 합.
    return 0;
}

/* ---------------------------------------------------------------------------
 * Q71.  겹치지 않게 만들기 — 제거해야 할 최소 인터벌 수
 *   KO: 유지보수 창은 동시에 하나만 실행 가능하다. 겹침이 사라지도록 제거해야 하는
 *       최소 인터벌 개수를 반환한다. 그리디: "끝나는 시각"이 빠른 것부터 유지.
 *       Q70 과 반대로, 맞닿는 구간([1,2],[2,3])은 겹치지 않는 것으로 본다.
 *   EN: Minimum number of intervals to drop so the rest are non-overlapping
 *       (greedy by earliest end; touching endpoints do not count as overlap).
 *   ex: {{1,2},{2,3},{3,4},{1,3}} -> 1
 * ------------------------------------------------------------------------- */
size_t erase_overlap_intervals(Interval *iv, size_t n) {
    (void)iv; (void)n;
    // TODO: qsort(end 오름차순) → last_end 를 들고 훑으며 iv[i].start < last_end 면 removed++.
    return 0;
}

/* ---------------------------------------------------------------------------
 * Q72.  top-K 이벤트 — 크기 K 의 min-heap
 *   KO: 이벤트 n 개 중 상위 K 개만 out 에 내림차순으로 쓰고 개수를 반환한다.
 *       전체 정렬 금지 — 크기 K 의 min-heap 을 유지해 O(n log K).
 *       힙 저장소는 호출자가 준 out[] 을 그대로 쓴다(할당 0).
 *       동점 규칙: score 가 같으면 dev_id 가 작은 쪽이 상위(결정적 출력).
 *   EN: Keep the K best events with a size-K min-heap built inside out[]; O(n log K).
 *   ex: scores {5,90,12,90,3,47,47,1}, k=3 -> dev 1002(90), 1004(90), 1006(47)
 * ------------------------------------------------------------------------- */
typedef struct { uint32_t dev_id; int score; } Event;

size_t top_k_events(const Event *ev, size_t n, size_t k, Event *out) {
    (void)ev; (void)n; (void)k; (void)out;
    // TODO: 힙이 K 개 미만이면 push(sift_up), 아니면 루트(최하위)보다 나을 때만 교체 후 sift_down.
    //       마지막에 out 을 "상위 먼저"로 정렬하고 min(n,k) 반환.
    return 0;
}

/* ---------------------------------------------------------------------------
 * Q73.  이벤트 스트림 집계 — 디바이스별 상태 카운트
 *   KO: 상태 변경 스트림을 받아 디바이스(문자열 키)별 online/offline/degraded
 *       횟수를 센다. 문자열 해시맵을 직접 구현할 것 — FNV-1a + 선형 탐사,
 *       슬롯 수는 2의 거듭제곱(& 마스킹). 테이블 포화/빈 키/너무 긴 키는
 *       조용히 버리지 말고 false + dropped++ 로 보고한다.
 *   EN: Fixed-size open-addressing string hash map counting per-device states.
 *   ex: record("GC31-0042", OFFLINE) x2 -> agg_get -> counts[OFFLINE] == 2
 * ------------------------------------------------------------------------- */
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

void agg_init(Aggregator *a) {
    if (a) memset(a, 0, sizeof *a);
    // (전부 0 이면 used=false 이므로 이것만으로 충분하다)
}

/* 이벤트 1건 반영. 성공 true, 버리면 false(dropped++). */
bool agg_record(Aggregator *a, const char *dev, DevState st) {
    (void)a; (void)dev; (void)st;
    // TODO: 키 검증(빈 문자열/AGG_DEV_LEN 이상 거부) → FNV-1a 해시 → 선형 탐사로
    //       기존 슬롯 또는 빈 슬롯 확보 → counts[st]++. 포화면 dropped++ 후 false.
    return false;
}

/* 디바이스별 카운트 조회. 없으면 false. */
bool agg_get(const Aggregator *a, const char *dev, uint32_t out[DEV_STATE_COUNT]) {
    (void)a; (void)dev; (void)out;
    // TODO: 같은 탐사 루프로 찾아 counts 를 복사.
    return false;
}

/* ---------------------------------------------------------------------------
 * Q74.  토큰 버킷 rate limiter — 업로드/알람 폭주 억제
 *   KO: "평균 R개/초, 순간 최대 C개"를 보장하는 토큰 버킷. 부동소수점 없이
 *       milli-token(1/1000 토큰) 정수로 계산한다. 시간은 인자로 주입(now_ms)해
 *       sleep 없이 결정적으로 테스트한다. 시계가 뒤로 가면 충전하지 말고 재동기화만.
 *   EN: Integer (milli-token) token bucket with injected clock; O(1) per call.
 *   ex: cap=5, rate=2/s -> 5 allowed at t=1000, 6th rejected, 1 more at t=1500
 * ------------------------------------------------------------------------- */
typedef struct {
    uint64_t tokens_milli;    /* 현재 토큰 * 1000 */
    uint64_t cap_milli;       /* 버스트 상한 * 1000 */
    uint32_t refill_per_sec;  /* 초당 충전 토큰 수 */
    uint64_t last_ms;
    uint64_t allowed, rejected;
} TokenBucket;

void tb_init(TokenBucket *tb, uint32_t capacity, uint32_t refill_per_sec, uint64_t now_ms) {
    (void)capacity; (void)refill_per_sec; (void)now_ms;
    if (tb) memset(tb, 0, sizeof *tb);
    // TODO: cap_milli = capacity*1000, tokens_milli = cap_milli(처음엔 가득),
    //       refill_per_sec 저장, last_ms = now_ms.
}

/* cost 개의 토큰을 요구한다. 있으면 소비하고 true, 없으면 false(rejected++). */
bool tb_allow(TokenBucket *tb, uint64_t now_ms, uint32_t cost) {
    (void)tb; (void)now_ms; (void)cost;
    // TODO: 먼저 refill — elapsed_ms * refill_per_sec milli-token 을 더하고 cap 으로 clamp.
    //       now_ms <= last_ms 면 충전 0 (last_ms 만 재동기화).
    //       그 다음 cost*1000 만큼 차감 가능하면 true, 아니면 rejected++ 후 false.
    return false;
}

/* 현재 사용 가능한 (내림) 토큰 수 — 테스트/텔레메트리용. */
uint32_t tb_available(const TokenBucket *tb) {
    (void)tb;
    // TODO: tokens_milli / 1000.
    return 0;
}

/* ---------------------------------------------------------------------------
 * Q75.  타임스탬프 정렬 병합 — 두 개의 정렬된 로그 스트림을 하나로
 *   KO: 이미 ts 오름차순인 두 배열을 two-pointer 로 한 번에 병합한다(다시 정렬 금지).
 *       안정성: ts 가 같으면 스트림 A 를 먼저 쓴다. out_cap 을 넘으면 잘라내고
 *       실제로 쓴 개수를 반환한다. NULL 스트림은 길이 0으로 취급.
 *   EN: Stable two-pointer merge of two ts-sorted log arrays into a capped buffer.
 *   ex: A ts {10,30,50}, B ts {20,40} -> 10,20,30,40,50
 * ------------------------------------------------------------------------- */
typedef struct { uint64_t ts; uint32_t dev; int code; } LogRec;

size_t merge_logs(const LogRec *a, size_t na, const LogRec *b, size_t nb,
                  LogRec *out, size_t out_cap) {
    (void)a; (void)na; (void)b; (void)nb; (void)out; (void)out_cap;
    // TODO: while(i<na && j<nb && w<out_cap) — b[j].ts < a[i].ts 일 때만 b 를 꺼낸다(동점은 a).
    //       남은 꼬리도 out_cap 까지 복사.
    return 0;
}

/* ---------------------------------------------------------------------------
 * Q76.  최근 N개 기반 중복 제거 — 같은 알람이 반복될 때 억제
 *   KO: 최근 N개의 "서로 다른" 알람 키만 기억해, 그 창 안에 있으면 억제(false),
 *       없으면 등록하고 내보낸다(true). 순환 버퍼(FIFO 창) + 해시(멤버십 O(1)).
 *       창에서 밀려난 항목은 해시에서도 정확히 제거되어야 한다. 할당 금지.
 *   EN: Suppress an alarm key if it is among the last N distinct keys;
 *       ring buffer as the FIFO window + intrusive hash chain for O(1) lookup.
 *   ex: A,A,B, ... (N개 밀어낸 뒤) A -> emit, suppress, emit, ... emit
 * ------------------------------------------------------------------------- */
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

void dedup_init(Dedup *d) {
    if (d) memset(d, 0, sizeof *d);
    // TODO: bucket[] 전부 DEDUP_NIL, ring[i].used = false, ring[i].hnext = DEDUP_NIL.
}

/* 최근 창에 없으면 등록하고 true(=내보낸다). 있으면 false(=억제). */
bool dedup_should_emit(Dedup *d, uint32_t key) {
    (void)d; (void)key;
    // TODO: 버킷 체인을 훑어 있으면 suppressed++ 후 false.
    //       없으면 ring[pos] 가 이미 쓰였을 때 그 키를 버킷 체인에서 제거하고,
    //       새 키를 ring[pos] 에 써서 체인 앞에 붙인다. pos = (pos+1) % DEDUP_WINDOW.
    return false;
}

/* ---------------------------------------------------------------------------
 * Q77.  시간 범위 쿼리 — 정렬된 타임스탬프 배열에서 구간 추출
 *   KO: append-only 로그는 이미 시간순이다. 선형 스캔 대신 lower_bound(첫 >= key)/
 *       upper_bound(첫 > key)를 직접 구현해 O(log n) 에 [lo,hi] 닫힌 구간을 잘라낸다.
 *       중복 타임스탬프가 있어도 두 경계가 구간 전체를 정확히 감싸야 한다.
 *   EN: Hand-rolled lower_bound/upper_bound; ts_range returns the count in [lo,hi]
 *       and writes the start index.
 *   ex: {100,200,200,200,300,400,500}, [200,300] -> begin=1, count=4
 * ------------------------------------------------------------------------- */
size_t ts_lower_bound(const uint64_t *ts, size_t n, uint64_t key) {
    (void)ts; (void)n; (void)key;
    // TODO: lo=0, hi=n; ts[mid] < key 면 lo=mid+1 아니면 hi=mid. 반환 lo.
    return 0;
}

size_t ts_upper_bound(const uint64_t *ts, size_t n, uint64_t key) {
    (void)ts; (void)n; (void)key;
    // TODO: 비교만 <= 로 바꾼다.
    return 0;
}

size_t ts_range(const uint64_t *ts, size_t n, uint64_t lo, uint64_t hi, size_t *out_begin) {
    (void)ts; (void)n; (void)lo; (void)hi;
    if (out_begin) *out_begin = 0;
    // TODO: lo > hi / 빈 배열이면 0. b=lower_bound(lo), e=upper_bound(hi), 반환 e-b.
    return 0;
}

/* ---------------------------------------------------------------------------
 * Q78.  로그 라인 파싱 — "ts=... dev=... state=... rssi=..." 를 구조체로
 *   KO: key=value 한 줄을 구조체로 바꾼다. 입력은 신뢰할 수 없다 — 필드 누락,
 *       모르는 키, 중복 키, 숫자 아닌 값, 범위를 벗어난 RSSI(-120..0 밖),
 *       너무 긴 dev 는 전부 거부한다. 실패 시 *out 을 건드리지 않는다
 *       (임시 구조체에 채운 뒤 전부 통과하면 커밋). 입력 버퍼를 변형하지 말 것
 *       (strtok_r 대신 const 포인터 수동 스캔 권장).
 *   EN: Strict key=value log line parser; rejects malformed input and never
 *       leaves a partially parsed result in *out.
 *   ex: "ts=1700000000 dev=GC31-0042 state=offline rssi=-97" -> true, all 4 fields set
 * ------------------------------------------------------------------------- */
#define LOG_DEV_LEN 16

typedef struct {
    uint64_t ts;
    char     dev[LOG_DEV_LEN];
    DevState state;
    int      rssi;
} LogLine;

bool parse_log_line(const char *line, LogLine *out) {
    (void)line; (void)out;
    // TODO: 공백을 건너뛰며 토큰 단위로:
    //         key = ['='  전까지], value = [공백 전까지]  ('=' 없으면 거부, 값 비면 거부)
    //       seen 비트마스크로 중복 키 거부, 마지막에 4비트 다 켜졌는지 확인.
    //       숫자 파싱은 strtoul 대신 직접 — 빈 문자열/비숫자/오버플로를 거부해야 한다.
    return false;
}

// ===========================================================================
// main — 테스트 (정답 파일과 동일. 건드리지 말 것)
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
