// 04_ipc_budget.c  —  REFERENCE SOLUTION
// IPC & 자원 예산 분배 (message passing / bandwidth arbitration)  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=04_ipc_budget
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g 04_ipc_budget.c -o /tmp/n04 && /tmp/n04
//
// 왜 이 주제인가 (Neros Platform 팀 관점):
//   - MCU 펌웨어는 malloc 을 쓰지 않는다. 힙은 파편화(fragmentation)되고 할당 시간이
//     비결정적(non-deterministic)이라 제어 루프의 WCET 를 망가뜨린다 -> 고정 크기
//     block pool + 고정 크기 message queue 로 O(1) / 결정적 동작을 보장한다.
//   - 플랫폼 런타임(logging / telemetry / IPC / config)은 모든 팀이 공유하는 코드라
//     "누가 얼마나 쓸 수 있나"를 정하는 주체가 곧 플랫폼 팀이다.
//   - FPV 드론의 무선 링크는 좁고 재밍당한다. 비행제어·자율주행·텔레메트리·영상이
//     같은 대역을 두고 경쟁하므로, 최소 보장(min) + 우선순위 분배 + 열화 시 graceful
//     degradation 정책을 런타임이 직접 들고 있어야 한다.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

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
// Q1 : 고정 크기 블록 풀 할당기 (fixed-size block pool, no malloc)
// ---------------------------------------------------------------------------
// 정적 배열 위에 "블록 리스트"를 올린다. 빈 블록의 앞 sizeof(void*) 바이트를
// next 포인터 저장소로 재활용(intrusive free list)하므로 별도 메타데이터 배열이
// 필요 없다. alloc = head pop, free = head push -> 둘 다 O(1), 결정적.
// ===========================================================================
typedef struct {
    uint8_t *mem;         // 외부에서 준 정적 저장소 (heap 사용 안 함)
    size_t   block_size;  // 블록 하나 크기 (>= sizeof(void*))
    size_t   nblocks;     // 블록 개수
    void    *free_head;   // intrusive free list 의 머리 (NULL = 고갈)
    size_t   used;        // 현재 나가 있는 블록 수
    uint32_t bad_free;    // 풀 밖/오정렬 포인터를 free 하려 한 횟수 (진단용)
} pool_t;

/* ---------------------------------------------------------------------------
 * Q1.  고정 크기 블록 풀 (pool_init / pool_alloc / pool_free / pool_used)
 *   KO: 정적 배열을 block_size 짜리 블록 nblocks 개로 쪼개고, 빈 블록 안에 다음
 *       빈 블록 주소를 써 넣는 intrusive free list 로 관리한다. alloc/free 모두
 *       O(1). 풀 범위 밖이거나 블록 경계에 정렬되지 않은 포인터를 free 하면
 *       free list 를 오염시키지 말고 bad_free 만 올리고 그냥 돌아온다.
 *   EN: Fixed-size block allocator over static storage using an intrusive free
 *       list; O(1) alloc/free. Reject (and count) a free of a pointer that is
 *       outside the pool or not on a block boundary, without corrupting state.
 *   ex: pool_init(&p, mem, 32, 4) -> pool_alloc x4 성공, 5번째는 NULL
 *       pool_free(&p, &어떤_지역변수) -> used 그대로, bad_free == 1
 *   hint: next 포인터는 memcpy 로 읽고 쓴다 (정렬/strict-aliasing 안전).
 *         범위 검사는 uintptr_t 로 변환해서 [base, base + block*n) 확인.
 * ------------------------------------------------------------------------- */
bool pool_init(pool_t *p, void *mem, size_t block_size, size_t nblocks) {
    if (!p || !mem || nblocks == 0) return false;
    if (block_size < sizeof(void *)) return false;   // next 포인터를 넣을 공간 필요

    p->mem        = (uint8_t *)mem;
    p->block_size = block_size;
    p->nblocks    = nblocks;
    p->used       = 0;
    p->bad_free   = 0;

    // 모든 블록을 free list 로 엮는다: 0 -> 1 -> ... -> n-1 -> NULL
    p->free_head = NULL;
    for (size_t i = nblocks; i > 0; --i) {
        uint8_t *blk = p->mem + (i - 1) * block_size;
        memcpy(blk, &p->free_head, sizeof(void *));   // blk->next = free_head
        p->free_head = blk;
    }
    return true;
}

void *pool_alloc(pool_t *p) {
    if (!p || !p->free_head) return NULL;             // 고갈 -> NULL (블로킹 없음)
    void *blk = p->free_head;
    void *next;
    memcpy(&next, blk, sizeof(void *));               // free_head = blk->next
    p->free_head = next;
    p->used++;
    return blk;
}

void pool_free(pool_t *p, void *ptr) {
    if (!p || !ptr) return;                           // free(NULL) 은 무해한 no-op

    // 소유권 검사: 이 포인터가 정말 우리 풀의 블록 시작 주소인가?
    uintptr_t base = (uintptr_t)p->mem;
    uintptr_t end  = base + (uintptr_t)(p->block_size * p->nblocks);
    uintptr_t q    = (uintptr_t)ptr;
    if (q < base || q >= end) { p->bad_free++; return; }          // 풀 밖
    if (((size_t)(q - base)) % p->block_size != 0) {              // 블록 중간
        p->bad_free++;
        return;
    }

    memcpy(ptr, &p->free_head, sizeof(void *));       // ptr->next = free_head
    p->free_head = ptr;
    if (p->used > 0) p->used--;
}

size_t pool_used(const pool_t *p) {
    return p ? p->used : 0;
}

// ===========================================================================
// Q2 / Q3 : IPC 메시지 큐 (fixed-size ring) + 토픽 구독 필터
// ---------------------------------------------------------------------------
// 서브시스템 간 통신은 "고정 크기 메시지 + 고정 용량 링"이 정석. 큐가 가득 차면
// 블로킹하지 않고 false 를 돌려준다(생산자가 제어 루프면 절대 멈추면 안 되므로).
// ===========================================================================
#define MSGQ_CAP 4            // 2의 거듭제곱 -> % 대신 & (MSGQ_CAP-1)
#define MSG_DATA_MAX 16

typedef struct {
    uint8_t topic;            // 0~31 (32비트 구독 마스크와 1:1 대응)
    uint8_t len;              // 유효 payload 길이
    uint8_t data[MSG_DATA_MAX];
} msg_t;

typedef struct {
    msg_t    slot[MSGQ_CAP];
    uint32_t head;            // 생산자만 쓴다 (free-running)
    uint32_t tail;            // 소비자만 쓴다 (free-running)
    uint32_t dropped;         // 큐가 가득 차서 버린 수
    uint32_t filtered;        // 구독하지 않은 토픽이라 버린 수 (Q3)
} msgq_t;

void msgq_init(msgq_t *q) {
    if (!q) return;
    q->head = q->tail = 0;
    q->dropped = q->filtered = 0;
}

uint32_t msgq_count(const msgq_t *q) {
    if (!q) return 0;
    return q->head - q->tail;   // 부호없는 뺄셈: free-running 인덱스가 랩해도 정확
}

/* ---------------------------------------------------------------------------
 * Q2.  고정 크기 IPC 메시지 큐 (msgq_push / msgq_pop)
 *   KO: msg_t 를 최대 MSGQ_CAP 개 담는 링 버퍼. push 는 슬롯에 값을 복사하고
 *       가득 차면 false(+dropped), pop 은 값을 복사해 내보내고 비면 false.
 *       단일 생산자/단일 소비자(SPSC)면 락이 필요 없다: 생산자만 head 를 쓰고
 *       소비자만 tail 을 쓰므로 같은 변수에 대한 write-write 경합이 없기 때문.
 *       (멀티코어라면 head/tail 을 acquire/release atomic 으로 공개해야 한다.)
 *   EN: Fixed-capacity ring of msg_t. push copies in (false when full), pop
 *       copies out (false when empty). Lock-free by construction for a single
 *       producer / single consumer: each index has exactly one writer.
 *   ex: 4개 push 후 5번째 push -> false, dropped==1
 *       push(topic=7) -> pop -> topic==7, payload 동일
 *   hint: idx = head & (MSGQ_CAP-1) ; full = (head - tail) == MSGQ_CAP
 * ------------------------------------------------------------------------- */
bool msgq_push(msgq_t *q, const msg_t *m) {
    if (!q || !m) return false;
    if (msgq_count(q) >= MSGQ_CAP) { q->dropped++; return false; }  // 덮어쓰지 않음
    q->slot[q->head & (MSGQ_CAP - 1)] = *m;   // 값 복사 (소유권 이전 문제 없음)
    q->head++;                                // 1) 데이터 먼저, 2) 인덱스 나중
    return true;
}

bool msgq_pop(msgq_t *q, msg_t *out) {
    if (!q || !out) return false;
    if (q->head == q->tail) return false;     // empty
    *out = q->slot[q->tail & (MSGQ_CAP - 1)];
    q->tail++;
    return true;
}

/* ---------------------------------------------------------------------------
 * Q3.  토픽 구독 필터 (msgq_push_filtered)
 *   KO: 구독자는 32비트 마스크로 관심 토픽을 표현한다. 메시지의 topic 비트가
 *       마스크에 없으면 큐에 넣지 않고 filtered++ 후 false. 토픽이 32 이상이면
 *       마스크로 표현 불가 -> 역시 drop. 할당 0회짜리 pub/sub 의 핵심.
 *   EN: Pub/sub with zero allocation: drop (and count) any message whose topic
 *       bit is not set in the subscriber's 32-bit mask, else push normally.
 *   ex: mask = (1<<1)|(1<<7) 일 때 topic 3 -> drop(filtered++), topic 7 -> push
 *   hint: (topic_mask >> m->topic) & 1u   (m->topic > 31 먼저 걸러낼 것)
 * ------------------------------------------------------------------------- */
bool msgq_push_filtered(msgq_t *q, const msg_t *m, uint32_t topic_mask) {
    if (!q || !m) return false;
    if (m->topic > 31u || ((topic_mask >> m->topic) & 1u) == 0u) {
        q->filtered++;      // 구독자가 원하지 않는 토픽: 큐 용량을 아예 소모하지 않는다
        return false;
    }
    return msgq_push(q, m);
}

// ===========================================================================
// Q4 : 토큰 버킷 레이트 리미터 (token bucket, 정수 전용)
// ---------------------------------------------------------------------------
// 로깅/텔레메트리가 링크를 잡아먹지 못하게 초당 전송량을 묶되, 짧은 폭주(burst)는
// 허용한다. 토큰을 1/1000 단위(milli-token)로 들고 있으면 1ms 간격 호출에서도
// 소수 토큰이 버림당하지 않는다 (rate[tokens/s] == rate[milli-tokens/ms]).
// ===========================================================================
typedef struct {
    uint32_t rate;          // 초당 충전 토큰 수
    uint32_t burst;         // 버킷 용량 (최대 누적 토큰)
    uint64_t tokens_milli;  // 현재 토큰 x 1000 (정수 전용, 잔여분 보존)
    uint32_t last_ms;       // 마지막 갱신 시각
} tb_t;

/* ---------------------------------------------------------------------------
 * Q4.  토큰 버킷 (tb_init / tb_allow)
 *   KO: tb_init 은 버킷을 가득 찬 상태(burst)로 시작한다. tb_allow 는 지난 시간에
 *       비례해 토큰을 충전하고(상한 burst), cost 만큼 있으면 소비 후 true.
 *       float 금지 -> 토큰을 1000배 정수로 저장해 1ms 단위 충전도 손실 없이 누적.
 *       elapsed 는 부호없는 뺄셈이라 tick 이 랩어라운드해도 안전.
 *   EN: Integer-only token bucket. Refill proportionally to elapsed ms, clamp at
 *       burst, spend `cost` tokens if available. Keep tokens scaled by 1000 so
 *       sub-token refills are not lost on rapid successive calls.
 *   ex: rate=10/s, burst=5 -> t=0 에서 5개 통과, 6번째 차단
 *       t=500ms 에 5토큰 충전 완료 ; 1ms 씩 100번 호출하면 정확히 1번 통과
 *   hint: tokens_milli += (uint64_t)elapsed_ms * rate;  cap = burst * 1000
 * ------------------------------------------------------------------------- */
void tb_init(tb_t *tb, uint32_t rate_per_s, uint32_t burst) {
    if (!tb) return;
    tb->rate         = rate_per_s;
    tb->burst        = burst;
    tb->tokens_milli = (uint64_t)burst * 1000u;   // 가득 찬 상태로 시작
    tb->last_ms      = 0;
}

bool tb_allow(tb_t *tb, uint32_t now_ms, uint32_t cost) {
    if (!tb) return false;

    uint32_t elapsed = now_ms - tb->last_ms;      // unsigned: tick wraparound 안전
    tb->last_ms      = now_ms;

    // rate [tokens/s] == rate [milli-tokens/ms] -> 나눗셈 없이 정확히 누적된다
    uint64_t cap = (uint64_t)tb->burst * 1000u;
    tb->tokens_milli += (uint64_t)elapsed * tb->rate;   // 64비트: 오버플로 방지
    if (tb->tokens_milli > cap) tb->tokens_milli = cap; // burst 상한

    uint64_t need = (uint64_t)cost * 1000u;
    if (tb->tokens_milli < need) return false;          // 부족: 토큰 소비 안 함
    tb->tokens_milli -= need;
    return true;
}

// ===========================================================================
// Q5 / Q6 : 링크 대역폭 중재 (priority split + graceful degradation)
// ---------------------------------------------------------------------------
// 같은 무선 링크를 flight control / autonomy / telemetry / video / logging 이
// 나눠 쓴다. 정책은 두 단계: (1) 우선순위 순으로 min 을 먼저 보장(기아 방지),
// (2) 남은 것을 다시 우선순위 순으로 want 까지. 전부 정수, 전부 결정적.
// ===========================================================================
#define MAX_STREAMS 16        // 동적 할당 없이 처리할 수 있는 스트림 상한

typedef struct {
    const char *name;
    uint32_t    want_bps;     // 이상적으로 쓰고 싶은 대역
    uint32_t    min_bps;      // 이거 아래로는 의미 없는 최소 대역
    uint8_t     prio;         // 값이 작을수록 중요 (0 = 비행 제어)
} stream_t;

/* ---------------------------------------------------------------------------
 * Q5.  우선순위 대역폭 분배 (budget_alloc)
 *   KO: 1단계 - prio 오름차순으로 돌며 min_bps 를 먼저 예약한다. 남은 예산이
 *       어떤 스트림의 min 도 못 채우면 거기서 컷오프하고 그 이후(더 낮은 우선순위)
 *       스트림은 전부 0 (싼 하위 스트림이 끼어드는 우선순위 역전을 막는다).
 *       2단계 - 남은 예산을 다시 prio 순으로 want_bps 까지 채운다.
 *       같은 prio 면 배열 순서를 유지하는 stable 정렬 -> 결과가 항상 결정적.
 *   EN: Guarantee each stream its min_bps in priority order (cut off once a min
 *       no longer fits: lower-priority streams get 0), then distribute the
 *       remainder in priority order up to each want_bps. Integer, deterministic.
 *   ex: total=100000, {min 4000/2000/50000/8000, want 40000/30000/200000/20000}
 *       -> {40000, 2000, 50000, 8000} (합 = 100000)
 *       total=50000 -> video/log 의 min 을 못 채움 -> {40000, 10000, 0, 0}
 *   hint: 인덱스 배열을 insertion sort 로 prio 정렬(원본 배열은 건드리지 않는다).
 * ------------------------------------------------------------------------- */
void budget_alloc(stream_t *s, size_t n, uint32_t total_bps, uint32_t *out_bps) {
    if (!s || !out_bps || n == 0) return;
    if (n > MAX_STREAMS) n = MAX_STREAMS;

    for (size_t i = 0; i < n; ++i) out_bps[i] = 0;

    // prio 오름차순 stable 정렬 (인덱스만 옮긴다 -> 호출자 배열 순서 보존)
    uint8_t order[MAX_STREAMS];
    for (size_t i = 0; i < n; ++i) order[i] = (uint8_t)i;
    for (size_t i = 1; i < n; ++i) {
        uint8_t key = order[i];
        size_t  j   = i;
        while (j > 0 && s[order[j - 1]].prio > s[key].prio) { order[j] = order[j - 1]; --j; }
        order[j] = key;
    }

    // 1단계: 최소 보장 (min_bps). 못 채우는 순간 컷오프.
    uint32_t remain  = total_bps;
    size_t   granted = 0;
    for (; granted < n; ++granted) {
        uint8_t k = order[granted];
        if (s[k].min_bps > remain) break;          // 여기부터는 전부 0 유지
        out_bps[k] = s[k].min_bps;
        remain -= s[k].min_bps;
    }

    // 2단계: 잔여 예산을 우선순위 순으로 want 까지
    for (size_t i = 0; i < granted && remain > 0; ++i) {
        uint8_t k = order[i];
        if (s[k].want_bps <= out_bps[k]) continue; // want <= min 인 스트림은 통과
        uint32_t extra = s[k].want_bps - out_bps[k];
        if (extra > remain) extra = remain;
        out_bps[k] += extra;
        remain     -= extra;
    }
}

/* ---------------------------------------------------------------------------
 * Q6.  링크 열화 시 전송 스트림 선택 (select_to_send)
 *   KO: 재밍 등으로 링크가 공칭의 10% 로 주저앉았을 때, 각 스트림의 min_bps 만
 *       기준으로 "그래도 보낼 것"을 우선순위 순으로 고른다. out_idx 에 중요한
 *       순서대로 인덱스를 채우고 개수를 반환. 들어가지 않는 스트림은 건너뛰되
 *       (Q5 의 컷오프와 달리) 더 싼 하위 스트림이 잔여 대역에 들어갈 수 있다 —
 *       링크가 죽었을 땐 "뭐라도 보내는" 쪽이 옳기 때문. graceful degradation.
 *   EN: Under a collapsed link, pick the streams that still fit in
 *       available_bps using each stream's min_bps, most important first;
 *       write their indices to out_idx and return how many were selected.
 *   ex: 공칭 100kbps -> 재밍으로 10000bps: flight(4000)+telemetry(2000) 생존,
 *       video(50000) / verbose_log(8000) drop -> 반환 2, out_idx = {0, 1}
 *   hint: prio 순으로 greedy. remain 이 min_bps 이상일 때만 선택하고 차감.
 * ------------------------------------------------------------------------- */
size_t select_to_send(const stream_t *s, size_t n, uint32_t available_bps, uint8_t *out_idx) {
    if (!s || !out_idx || n == 0) return 0;
    if (n > MAX_STREAMS) n = MAX_STREAMS;

    uint8_t order[MAX_STREAMS];
    for (size_t i = 0; i < n; ++i) order[i] = (uint8_t)i;
    for (size_t i = 1; i < n; ++i) {
        uint8_t key = order[i];
        size_t  j   = i;
        while (j > 0 && s[order[j - 1]].prio > s[key].prio) { order[j] = order[j - 1]; --j; }
        order[j] = key;
    }

    uint32_t remain = available_bps;
    size_t   cnt    = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t k = order[i];
        if (s[k].min_bps <= remain) {              // 최소 대역이 들어가면 살린다
            out_idx[cnt++] = k;
            remain -= s[k].min_bps;
        }
        // 안 들어가면 skip: 더 싼 하위 스트림에게 기회를 준다
    }
    return cnt;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    // -------- Q1 : block pool --------
    printf("== Q1. 고정 크기 블록 풀 할당기 (pool allocator) ==\n");
    static _Alignas(8) uint8_t pool_mem[4 * 32];
    static pool_t pool;
    bool pinit = pool_init(&pool, pool_mem, 32, 4);
    T("Q1 pool_init: 정적 배열 위 4블록, used==0", pinit && pool_used(&pool) == 0);

    void *b[4];
    for (int i = 0; i < 4; ++i) b[i] = pool_alloc(&pool);
    bool distinct = (b[0] && b[1] && b[2] && b[3]);
    for (int i = 0; i < 4 && distinct; ++i)
        for (int j = i + 1; j < 4; ++j)
            if (b[i] == b[j]) distinct = false;
    T("Q1 4블록 alloc: 전부 서로 다른 주소, used==4", distinct && pool_used(&pool) == 4);

    void *exhausted = pool_alloc(&pool);
    T("Q1 풀 고갈 -> NULL (블로킹/확장 없음)", exhausted == NULL && pool_used(&pool) == 4);

    pool_free(&pool, b[1]);
    void *reuse = pool_alloc(&pool);
    T("Q1 free 후 재사용(reuse): 같은 블록이 다시 나옴", pool_used(&pool) == 4 && reuse == b[1]);

    uint8_t foreign = 0;
    pool_free(&pool, &foreign);                 // 풀 밖 포인터
    pool_free(&pool, pool_mem + 1);             // 블록 경계에 안 맞는 포인터
    pool_free(&pool, NULL);                     // free(NULL) 은 무해한 no-op
    T("Q1 풀 밖/오정렬 free 거부: 상태 무손상, bad_free==2",
      pool_used(&pool) == 4 && pool.bad_free == 2 && pool_alloc(&pool) == NULL);

    // -------- Q2 : SPSC IPC message queue --------
    printf("== Q2. 고정 크기 IPC 메시지 큐 (SPSC ring) ==\n");
    static msgq_t q;
    msgq_init(&q);
    msg_t m = {0, 0, {0}};
    bool pushed = true;
    for (uint8_t i = 0; i < MSGQ_CAP; ++i) {
        m.topic = (uint8_t)(i + 1);
        m.len   = 4;
        memcpy(m.data, "ABCD", 4);
        m.data[0] = (uint8_t)('A' + i);
        pushed = pushed && msgq_push(&q, &m);
    }
    m.topic = 99;
    T("Q2 4개 push 후 full -> 5번째 push false, dropped==1",
      pushed && msgq_count(&q) == MSGQ_CAP && !msgq_push(&q, &m) && q.dropped == 1);

    msg_t out = {0, 0, {0}};
    bool fifo = true;
    for (uint8_t i = 0; i < MSGQ_CAP; ++i) {
        if (!msgq_pop(&q, &out)) { fifo = false; break; }
        if (out.topic != (uint8_t)(i + 1) || out.len != 4 || out.data[0] != (uint8_t)('A' + i))
            fifo = false;
    }
    T("Q2 FIFO 순서 + payload 복사 무결, empty pop -> false",
      fifo && !msgq_pop(&q, &out) && msgq_count(&q) == 0);

    bool wrap = true;
    for (uint8_t r = 0; r < 6; ++r) {            // head/tail 이 용량을 넘어 랩된다
        m.topic   = (uint8_t)(10 + r);
        m.len     = 1;
        m.data[0] = r;
        wrap = wrap && msgq_push(&q, &m);
        wrap = wrap && msgq_pop(&q, &out);
        wrap = wrap && (out.topic == (uint8_t)(10 + r) && out.data[0] == r);
    }
    T("Q2 랩어라운드 후에도 슬롯 재사용 정상", wrap && msgq_count(&q) == 0);

    // -------- Q3 : topic subscription filter --------
    printf("== Q3. 토픽 구독 필터 (pub/sub mask) ==\n");
    static msgq_t fq;
    msgq_init(&fq);
    uint32_t mask = (1u << 1) | (1u << 7);       // 구독 토픽: 1, 7
    msg_t fm = {0, 2, {0xAB, 0xCD}};
    fm.topic  = 3;
    bool drop1 = msgq_push_filtered(&fq, &fm, mask);
    fm.topic   = 33;                             // 32 이상 -> 마스크로 표현 불가
    bool drop2 = msgq_push_filtered(&fq, &fm, mask);
    T("Q3 미구독 토픽 drop: 큐 용량 소모 0, filtered==2",
      !drop1 && !drop2 && msgq_count(&fq) == 0 && fq.filtered == 2);

    fm.topic = 7;
    bool accepted = msgq_push_filtered(&fq, &fm, mask);
    msg_t fo = {0, 0, {0}};
    T("Q3 구독 토픽은 통과 + 내용 보존",
      accepted && msgq_pop(&fq, &fo) && fo.topic == 7 && fo.len == 2 && fo.data[1] == 0xCD);

    // -------- Q4 : token bucket rate limiter --------
    printf("== Q4. 토큰 버킷 레이트 리미터 (token bucket) ==\n");
    static tb_t tb;
    tb_init(&tb, 10, 5);                         // 10 msg/s, burst 5
    bool burst_ok = true;
    for (int i = 0; i < 5; ++i) burst_ok = burst_ok && tb_allow(&tb, 0, 1);
    T("Q4 burst 5개 즉시 통과 후 6번째 차단", burst_ok && !tb_allow(&tb, 0, 1));

    T("Q4 500ms 경과 -> 5토큰 충전 (cost=5 통과, 직후 재시도 차단)",
      tb_allow(&tb, 500, 5) && !tb_allow(&tb, 500, 1));

    int allowed = 0;
    for (uint32_t t = 501; t <= 600; ++t) if (tb_allow(&tb, t, 1)) allowed++;
    T("Q4 1ms 간격 연속 호출에도 잔여분 누적 -> 100ms 후 정확히 1회 통과", allowed == 1);

    T("Q4 장시간 idle 후에도 burst 상한을 넘지 않음",
      tb_allow(&tb, 600000, 5) && !tb_allow(&tb, 600000, 1));

    // -------- Q5 : priority bandwidth split --------
    printf("== Q5. 우선순위 대역폭 분배 (budget split) ==\n");
    stream_t st[4] = {
        { "flight_ctrl",  40000,  4000, 0 },     // 비행 제어: 가장 중요
        { "telemetry",    30000,  2000, 1 },     // 상태 텔레메트리
        { "video",       200000, 50000, 2 },     // FPV 영상
        { "verbose_log",  20000,  8000, 3 },     // 디버그 로깅: 제일 먼저 포기
    };
    uint32_t alloc_full[4] = {0, 0, 0, 0};
    budget_alloc(st, 4, 100000, alloc_full);     // 공칭 100 kbps
    T("Q5 100kbps: min 보장 후 잔여를 우선순위대로 want 까지",
      alloc_full[0] == 40000 && alloc_full[1] == 2000 &&
      alloc_full[2] == 50000 && alloc_full[3] == 8000 &&
      (alloc_full[0] + alloc_full[1] + alloc_full[2] + alloc_full[3]) == 100000);

    uint32_t alloc_tight[4] = {0, 0, 0, 0};
    budget_alloc(st, 4, 50000, alloc_tight);     // min 합(64000) 을 못 채우는 예산
    T("Q5 50kbps: 모든 min 충족 불가 -> 하위 우선순위 0, 예산 초과 없음",
      alloc_tight[0] == 40000 && alloc_tight[1] == 10000 &&
      alloc_tight[2] == 0 && alloc_tight[3] == 0 &&
      (alloc_tight[0] + alloc_tight[1] + alloc_tight[2] + alloc_tight[3]) == 50000);

    // -------- Q6 : degraded-link selection --------
    printf("== Q6. 링크 열화(재밍) 시 전송 스트림 선택 ==\n");
    uint8_t sel[4] = {0, 0, 0, 0};
    size_t nsel = select_to_send(st, 4, 10000, sel);       // 공칭의 10% 로 붕괴
    T("Q6 재밍 10%: 비행제어+텔레메트리 생존, 영상/verbose 로그 drop",
      nsel == 2 && sel[0] == 0 && sel[1] == 1);

    uint8_t sel2[4] = {0, 0, 0, 0};
    size_t nsel2 = select_to_send(st, 4, 14100, sel2);     // 조금 회복된 링크
    T("Q6 14.1kbps: 안 들어가는 영상은 건너뛰고 로그가 잔여 대역에 편승",
      nsel2 == 3 && sel2[0] == 0 && sel2[1] == 1 && sel2[2] == 3);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
