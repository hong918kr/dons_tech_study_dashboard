// 04_realtime_buffers.c  —  PRACTICE STUB (직접 채워넣기)
// 실시간 버퍼링 (더블/트리플 버퍼) (Real-time Buffering (Double/Triple))  —  Q31~Q38
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=04_realtime_buffers
//   또는:     cc -std=c11 -Wall -Wextra -O1 -g -pthread 04_realtime_buffers.c -o /tmp/vk04p && /tmp/vk04p
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 → [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다. 스레드 테스트는
//  생산자가 유한 루프라 반드시 종료하고, seqlock reader 는 재시도 상한이 있어
//  절대 멈추지 않는다.)
//
// 리크루터 메일이 "real-time data handling patterns (such as double buffering)"을
// 대놓고 지목했다. 핵심 사고방식 하나: **락으로 '데이터'를 보호하지 말고 '버퍼
// 소유권'을 보호한다.** 임계구역은 인덱스 몇 개 교환하는 것뿐이고(수 나노초),
// 프레임 바이트 복사는 락 밖에서 일어난다.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>

// ===========================================================================
// 테스트 하네스 (건드리지 말 것)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ===========================================================================
// 공통 타입 — 프레임과 N-버퍼 풀 (주어짐)
// ---------------------------------------------------------------------------
// 버퍼 하나는 언제나 셋 중 한 상태다:
//   WRITER 소유(write_idx) / READY(publish됨, ready_idx) / READER 소유(reader_idx)
// 어느 상태에도 속하지 않으면 FREE. 인덱스 세 개가 곧 소유권 장부다.
// nbuf==2 면 더블 버퍼링, nbuf==3 이면 트리플 버퍼링 — 코드는 같아야 한다.
// ===========================================================================
#define FRAME_LEN 128
#define MAXBUF    3

typedef struct {
    unsigned      seq;                 // 프레임 일련번호 (1부터)
    unsigned char data[FRAME_LEN];     // 전부 (seq & 0xFF) 로 채운다 → tearing 검출용
} Frame;

typedef struct {
    Frame           buf[MAXBUF];
    int             nbuf;              // 실제로 쓰는 버퍼 수 (2 또는 3)
    int             write_idx;         // writer 소유 버퍼, 없으면 -1
    int             ready_idx;         // publish된 최신 버퍼, 없으면 -1
    int             reader_idx;        // reader 대여 중 버퍼, 없으면 -1
    unsigned long   dropped;           // 소비되지 못하고 버려진 프레임 총수
    unsigned long   starved;           // write_begin에서 빈 버퍼가 없어 READY를 희생한 횟수
    pthread_mutex_t m;                 // '데이터'가 아니라 '소유권 장부'를 보호한다
} MBuf;

/* ---------------------------------------------------------------------------
 * Q31.  더블 버퍼 소유권 모델 — init / destroy / write_begin
 *   KO: mb_init 은 nbuf 를 [2, MAXBUF] 로 클램프하고 세 인덱스를 모두 -1,
 *       카운터를 0, mutex 를 초기화한다. mb_write_begin 은 writer 가 채울
 *       FREE 버퍼(= ready_idx 도 reader_idx 도 아닌 인덱스)를 찾아 write_idx 에
 *       걸고 그 Frame* 를 돌려준다. **절대 블로킹하지 않고 항상 성공한다** —
 *       FREE 가 없으면 READY 를 희생하고 dropped++, starved++ (reader 가 든
 *       버퍼는 절대 뺏지 않는다; 그게 곧 tearing 이다).
 *   EN: Grab a free buffer for the writer. Never block: if none is free,
 *       sacrifice the READY buffer and count the drop. Never touch the
 *       buffer the reader currently owns.
 *   ex: nbuf=2, ready=0, reader=1 -> write_begin() returns &buf[0], starved==1
 * ------------------------------------------------------------------------- */
void mb_init(MBuf *mb, int nbuf) {
    (void)mb; (void)nbuf;
    // TODO: implement
}

void mb_destroy(MBuf *mb) {
    (void)mb;
    // TODO: implement (pthread_mutex_destroy)
}

Frame *mb_write_begin(MBuf *mb) {
    if (!mb) return NULL;
    // TODO: implement — FREE 버퍼 탐색, 없으면 READY 희생(dropped++/starved++)
    return &mb->buf[0];   // 더미 리턴(테스트가 크래시하지 않게). 구현하면 지울 것.
}

/* ---------------------------------------------------------------------------
 * Q32.  소유권 이전 — write_commit / read_acquire / read_release
 *   KO: write_commit 은 writer 소유 버퍼를 READY 로 publish 한다. 직전 READY 가
 *       아직 안 읽혔으면 그건 버려지는 것이므로 dropped++.
 *       read_acquire 는 READY 를 reader 소유로 옮기고 Frame* 를 준다. READY 가
 *       없거나 reader 가 이미 하나를 들고 있으면 NULL(논블로킹).
 *       read_release 는 reader_idx 를 -1 로 되돌린다.
 *   EN: Transfer ownership writer->READY->reader->free. Non-blocking reader:
 *       return NULL when there is nothing new or one frame is already lent out.
 *   ex: commit(ready=1) -> acquire() returns &buf[1], ready_idx==-1
 * ------------------------------------------------------------------------- */
void mb_write_commit(MBuf *mb) {
    (void)mb;
    // TODO: implement
}

Frame *mb_read_acquire(MBuf *mb) {
    (void)mb;
    // TODO: implement
    return NULL;
}

void mb_read_release(MBuf *mb) {
    (void)mb;
    // TODO: implement
}

/* ---------------------------------------------------------------------------
 * Q33.  tearing 검출 + 회계 불변식
 *   KO: frame_fill 은 seq 를 넣고 data 전 바이트를 (seq & 0xFF) 로 채운다.
 *       frame_is_torn 은 data 의 모든 바이트가 (seq & 0xFF) 인지 검사해 하나라도
 *       다르면 true(= 두 세대가 섞인 프레임). NULL 은 true.
 *       mb_accounting_ok 는 정지 상태에서
 *         consumed + dropped + leftover(READY 1개) + inflight(WRITER 1개) == produced
 *       를 검사한다. "생산된 프레임은 읽혔거나, 버려졌거나, 아직 남아있다."
 *   EN: Fill a frame with a seq-derived pattern; detect a torn frame by
 *       scanning for a byte that breaks the pattern; assert the accounting
 *       invariant so no frame is ever lost silently.
 *   ex: frame_fill(&f,0x1234) -> torn==false; f.data[64]^=0xFF -> torn==true
 * ------------------------------------------------------------------------- */
void frame_fill(Frame *f, unsigned seq) {
    (void)f; (void)seq;
    // TODO: implement
}

bool frame_is_torn(const Frame *f) {
    (void)f;
    // TODO: implement
    return false;
}

bool mb_accounting_ok(const MBuf *mb, unsigned long produced, unsigned long consumed) {
    (void)mb; (void)produced; (void)consumed;
    // TODO: implement
    return false;
}

// ---- 생산자/소비자 스레드 (주어짐 — 건드리지 말 것) -------------------------
typedef struct {
    MBuf         *mb;
    unsigned      nframes;
    _Atomic bool  done;
    unsigned long read_ok;        // 소비자만 씀
    unsigned long torn;           // 소비자만 씀
    unsigned long out_of_order;   // 소비자만 씀
} PingArgs;

static void *pp_producer(void *arg)
{
    PingArgs *a = (PingArgs *)arg;
    for (unsigned s = 1; s <= a->nframes; s++) {
        Frame *f = mb_write_begin(a->mb);
        frame_fill(f, s);                           // 락 밖에서 채운다
        mb_write_commit(a->mb);
        if ((s & 0x7u) == 0u) sched_yield();
    }
    atomic_store_explicit(&a->done, true, memory_order_release);
    return NULL;
}

static void *pp_consumer(void *arg)
{
    PingArgs *a = (PingArgs *)arg;
    unsigned last = 0;
    for (;;) {
        // done을 '먼저' 읽는다: done==true를 본 뒤의 acquire가 NULL이면
        // 남은 프레임이 정말 없다는 뜻 → 무한 스핀 없이 안전하게 종료.
        bool done = atomic_load_explicit(&a->done, memory_order_acquire);
        Frame *f = mb_read_acquire(a->mb);
        if (!f) {
            if (done) break;
            continue;
        }
        if (frame_is_torn(f))  a->torn++;
        if (f->seq < last)     a->out_of_order++;
        last = f->seq;
        a->read_ok++;
        mb_read_release(a->mb);
    }
    return NULL;
}

static bool pp_run(MBuf *mb, unsigned nframes, PingArgs *out)
{
    pthread_t p, c;
    PingArgs a;
    memset(&a, 0, sizeof a);
    a.mb = mb;
    a.nframes = nframes;
    atomic_store(&a.done, false);
    pthread_create(&c, NULL, pp_consumer, &a);
    pthread_create(&p, NULL, pp_producer, &a);
    pthread_join(p, NULL);
    pthread_join(c, NULL);
    if (out) *out = a;
    return mb_accounting_ok(mb, nframes, a.read_ok);
}

/* ---------------------------------------------------------------------------
 * Q34.  트리플 버퍼로 확장 (nbuf = 3)
 *   KO: 새 함수는 없다 — Q31 의 mb_write_begin 이 nbuf 를 제대로 쓰고 있으면
 *       mb_init(&mb, 3) 만으로 트리플 버퍼가 된다. 확인할 것:
 *       reader 1개 + READY 1개여도 세 번째 버퍼가 FREE 로 남으므로
 *       **writer 는 절대 굶지 않는다(starved == 0)**. 대가는 메모리 +50%.
 *       (commit 에서 안 읽힌 READY 를 덮는 drop 은 트리플에도 남는다 — 그건
 *        writer 가 막힌 게 아니라 소비자가 느려 프레임이 낡은 것이다.)
 *   EN: With three buffers the writer always finds a free one, so it never
 *       has to steal the READY buffer. Cost: 50% more memory.
 *   ex: nbuf=3, reader=0, ready=1 -> write_begin() returns &buf[2], starved==0
 * ------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * Q35.  latest-value 슬롯 — mutex 버전 vs seqlock 버전
 *   KO: (A) LvMutex: mutex 로 보호되는 값 하나. store 는 덮어쓰기, load 는
 *       valid 일 때만 복사해 true. 단순·정확하지만 writer 가 reader 를 막는다.
 *       (B) SeqSlot: seqlock. writer 는 seq 를 홀수로 올리고(진입) 페이로드를
 *       쓰고 짝수로 올린다(이탈) — **절대 막히지 않는다**. reader 는 앞뒤 seq
 *       가 같고 짝수일 때만 값을 채택하고, 아니면 재시도(상한 SL_MAX_RETRY).
 *       페이로드를 _Atomic + relaxed 로 두는 이유: 일반 변수면 C 표준상 data
 *       race(UB)이고 TSan 도 경고한다. relaxed atomic + fence 면 의미는 같다.
 *   EN: Two ways to publish a single latest value. Mutex is simple but blocks
 *       the writer; a seqlock never blocks the writer and makes readers retry.
 *       Seqlock needs a single writer and a POD payload (no pointers).
 *   ex: sl_store(s); sl_load(&out) -> true, out == s, sl_seq() is even
 * ------------------------------------------------------------------------- */
typedef struct {
    uint64_t ts_us;
    int32_t  rssi_dbm;
    int32_t  check;        // ts/rssi에서 유도 — 찢어진 읽기 검출용
} LinkSample;

// (주어짐) 체크섬 헬퍼
static int32_t sample_check(uint64_t ts, int32_t rssi)
{
    return (int32_t)(ts & 0x7FFFFFFFu) ^ rssi;
}

bool sample_consistent(const LinkSample *s) {
    (void)s;
    // TODO: implement — s->check 가 sample_check(s->ts_us, s->rssi_dbm) 와 같은가
    return false;
}

// ---- (A) mutex 버전 --------------------------------------------------------
typedef struct {
    LinkSample      v;
    bool            valid;
    pthread_mutex_t m;
} LvMutex;

void lv_init(LvMutex *lv) {
    (void)lv;
    // TODO: implement
}

void lv_destroy(LvMutex *lv) {
    (void)lv;
    // TODO: implement
}

void lv_store(LvMutex *lv, LinkSample s) {
    (void)lv; (void)s;
    // TODO: implement
}

bool lv_load(LvMutex *lv, LinkSample *out) {
    (void)lv; (void)out;
    // TODO: implement
    return false;
}

// ---- (B) seqlock 버전 ------------------------------------------------------
#define SL_MAX_RETRY 1000

typedef struct {
    _Atomic unsigned seq;          // 짝수=안정, 홀수=쓰기 중
    _Atomic uint64_t ts_us;
    _Atomic int32_t  rssi_dbm;
    _Atomic int32_t  check;
} SeqSlot;

void sl_init(SeqSlot *sl) {
    (void)sl;
    // TODO: implement
}

void sl_store(SeqSlot *sl, LinkSample s) {
    (void)sl; (void)s;
    // TODO: implement — seq+1(홀수) / release fence / 페이로드 / release fence / seq+2
}

// 재시도 상한을 반드시 지킬 것 — 무한 루프 금지.
bool sl_load(const SeqSlot *sl, LinkSample *out) {
    (void)sl; (void)out;
    // TODO: implement
    return false;
}

unsigned sl_seq(const SeqSlot *sl) {
    (void)sl;
    // TODO: implement
    return 1u;   // 더미(짝수가 아니어서 FAIL 로 뜬다)
}

// ---- seqlock 스레드 테스트 (주어짐 — 건드리지 말 것) -----------------------
#define SL_WRITES   20000u
#define SL_ATTEMPTS 20000

typedef struct {
    SeqSlot      *sl;
    unsigned long succ, torn, retry_exhausted;
} SlArgs;

static void *sl_writer(void *arg)
{
    SlArgs *a = (SlArgs *)arg;
    for (unsigned i = 1; i <= SL_WRITES; i++) {
        LinkSample s;
        s.ts_us    = (uint64_t)i * 1000u;
        s.rssi_dbm = -30 - (int32_t)(i % 40u);
        s.check    = sample_check(s.ts_us, s.rssi_dbm);
        sl_store(a->sl, s);
    }
    return NULL;
}

static void *sl_reader(void *arg)
{
    SlArgs *a = (SlArgs *)arg;
    for (int i = 0; i < SL_ATTEMPTS; i++) {          // 고정 횟수 → 스케줄링 독립
        LinkSample s;
        if (sl_load(a->sl, &s)) {
            a->succ++;
            if (!sample_consistent(&s)) a->torn++;
        } else {
            a->retry_exhausted++;
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Q36.  레이트 디커플링 — 400Hz 생산 → 100Hz 소비 (4:1 데시메이션)
 *   KO: 두 정책을 '순수 함수'로 분리해 하드웨어 없이 테스트한다.
 *       rd_latest(blk,n) = 블록의 마지막 샘플(지연 최소, 계단/이벤트 신호용)
 *       rd_mean(blk,n)   = 블록 평균, 0에서 먼 쪽 반올림(노이즈 억제, 연속 신호용)
 *       rd_decimate 는 in[n_in] 을 factor:1 로 줄여 out 에 쓰고 개수를 반환한다.
 *       꽉 찬 블록만 처리(꼬리는 다음 호출로), out_cap 을 넘지 않는다.
 *       합계는 int64 로 누적해 오버플로를 막는다. NULL/0/음수 방어 필수.
 *   EN: Decimate a fast sensor stream to a slower consumer rate with two
 *       policies — take-latest (low latency, event-like signals) and
 *       block-average (noise rejection, continuous analog signals).
 *   ex: in={10,20,30,40,50,60,70,80}, factor=4 -> LATEST {40,80} / MEAN {25,65}
 * ------------------------------------------------------------------------- */
typedef enum { RD_LATEST = 0, RD_MEAN = 1 } RatePolicy;

// (주어짐) 반올림 나눗셈: 0에서 먼 쪽으로. den > 0 가정.
static int32_t round_div(int64_t num, int64_t den)
{
    if (den <= 0) return 0;
    if (num >= 0) return (int32_t)((num + den / 2) / den);
    return (int32_t)((num - den / 2) / den);
}

int32_t rd_latest(const int32_t *blk, int n) {
    (void)blk; (void)n;
    // TODO: implement
    return 0;
}

int32_t rd_mean(const int32_t *blk, int n) {
    (void)blk; (void)n; (void)round_div;   // round_div 를 쓸 것 (미구현 경고 억제)
    // TODO: implement (int64 누적 + round_div)
    return 0;
}

int rd_decimate(const int32_t *in, int n_in, int factor,
                RatePolicy pol, int32_t *out, int out_cap) {
    (void)in; (void)n_in; (void)factor; (void)pol; (void)out; (void)out_cap;
    // TODO: implement
    return 0;
}

/* ---------------------------------------------------------------------------
 * Q37.  DMA ping-pong 시뮬레이션
 *   KO: DMA 가 buf[0] 을 채우는 동안 CPU 는 buf[1] 을 처리한다.
 *       dma_pp_isr_complete 는 **인덱스 교환만** 한다 — 데이터 복사·파싱·락
 *       대기 금지(ISR 은 수 마이크로초 안에 끝나야 한다). active 를 토글하고
 *       방금 끝난 버퍼를 full 에 싣는다. 직전 full 을 앱이 안 가져갔으면
 *       overrun++. dma_pp_take 는 full 을 -1 로 교환하며 그 버퍼의 포인터를
 *       준다(복사 없음). 없으면 NULL.
 *       ⚠ 실제 하드웨어라면: DMA→CPU 전에 cache invalidate, CPU→DMA 전에
 *         cache clean/flush. 버퍼는 캐시 라인 정렬 + 라인 크기 배수여야 한다.
 *   EN: Ping-pong DMA. The completion ISR only swaps indices — no copying,
 *       no locks. The consumer's deadline is one transfer period.
 *   ex: init -> isr_complete() -> active==1, take() returns buf[0]
 * ------------------------------------------------------------------------- */
#define DMA_LEN 64

typedef struct {
    unsigned char        buf[2][DMA_LEN];
    int                  active;      // DMA가 지금 채우는 버퍼 (ISR 컨텍스트 전용)
    _Atomic int          full;        // 앱이 가져갈 수 있는 버퍼, 없으면 -1
    _Atomic unsigned long overrun;    // 앱이 못 가져간 채 덮인 횟수
    _Atomic unsigned long completes;  // 전송 완료 인터럽트 횟수
} DmaPP;

void dma_pp_init(DmaPP *d) {
    (void)d;
    // TODO: implement (active=0, full=-1, 카운터 0)
}

void dma_pp_isr_complete(DmaPP *d) {
    (void)d;
    // TODO: implement — 인덱스 교환만! 복사 금지.
}

unsigned char *dma_pp_take(DmaPP *d, int *idx_out) {
    (void)d; (void)idx_out;
    // TODO: implement
    return NULL;
}

int dma_pp_active(const DmaPP *d) {
    (void)d;
    // TODO: implement
    return -1;
}

unsigned long dma_pp_overrun(const DmaPP *d) {
    (void)d;
    // TODO: implement
    return 0UL;
}

/* ---------------------------------------------------------------------------
 * Q38.  프레임 지터/누락 측정 (순수 함수)
 *   KO: 타임스탬프 배열만 받아 통계를 낸다 — 카메라도 커널도 없이 단위 테스트
 *       가능해야 한다. 각 간격 gap = ts[i]-ts[i-1] 에 대해:
 *         · max_gap_us / min_gap_us 갱신
 *         · max_jitter_us = max |gap - nominal_us|
 *         · k = (gap + nominal/2) / nominal  (반올림!)  k>1 이면 missed += k-1
 *       반올림하는 이유: 정상 범위 지터(±40%)를 누락으로 오판하지 않기 위해서.
 *       ts[i] < ts[i-1] 이면 nonmonotonic++ 로 따로 세고 gap=0 취급.
 *       n<2 · ts==NULL · nominal==0 이면 전부 0 인 결과(0 나누기 금지).
 *   EN: Pure function over a timestamp array: estimate missed frames by
 *       rounding each gap to the nearest multiple of the nominal period, and
 *       report max gap / min gap / max jitter. Guard every degenerate input.
 *   ex: ts={0,10000,30000,40000}, nominal=10000 -> missed=1, max_gap=20000
 * ------------------------------------------------------------------------- */
typedef struct {
    int           n_gaps;          // 간격 개수 (= n-1)
    unsigned long missed;          // 추정 누락 프레임 수
    unsigned long max_gap_us;      // 최대 간격
    unsigned long min_gap_us;      // 최소 간격
    unsigned long max_jitter_us;   // max |gap - nominal|
    unsigned long nonmonotonic;    // 타임스탬프가 거꾸로 간 횟수
} JitterStats;

JitterStats jitter_analyze(const unsigned long *ts_us, int n, unsigned long nominal_us) {
    JitterStats st;
    memset(&st, 0, sizeof st);
    (void)ts_us; (void)n; (void)nominal_us;
    // TODO: implement
    return st;
}

// ===========================================================================
// main — 테스트 (정답 파일과 동일. 건드리지 말 것)
// ===========================================================================
int main(void)
{
    // ---------------- Q31 : 소유권 모델 + write_begin ----------------
    printf("== Q31 더블 버퍼 소유권 모델 (write_begin) ==\n");
    MBuf db = {0};        // stub 상태에서도 안전하도록 0으로 시작
    mb_init(&db, 2);
    T("Q31 init: 세 인덱스 모두 -1",
      db.write_idx == -1 && db.ready_idx == -1 && db.reader_idx == -1);
    T("Q31 init: 카운터 0 + nbuf=2", db.dropped == 0 && db.starved == 0 && db.nbuf == 2);

    Frame *w0 = mb_write_begin(&db);
    T("Q31 write_begin은 항상 버퍼를 준다(논블로킹)", w0 == &db.buf[0]);
    T("Q31 write_begin이 write_idx를 점유", db.write_idx == 0);
    mb_write_commit(&db);

    Frame *w1 = mb_write_begin(&db);
    T("Q31 READY(0)는 건너뛰고 FREE(1)을 고른다", w1 == &db.buf[1] && db.write_idx == 1);
    mb_write_commit(&db);
    T("Q31 안 읽힌 READY를 덮었으므로 dropped=1, starved=0",
      db.dropped == 1 && db.starved == 0);

    // ---------------- Q32 : commit / acquire / release ----------------
    printf("== Q32 소유권 이전 (commit / acquire / release) ==\n");
    Frame *r0 = mb_read_acquire(&db);
    T("Q32 acquire가 최신 READY(1)를 대여", r0 == &db.buf[1] && db.reader_idx == 1);
    T("Q32 acquire 후 READY는 비워진다", db.ready_idx == -1);
    T("Q32 이미 대여 중이면 두 번째 acquire는 NULL", mb_read_acquire(&db) == NULL);

    Frame *w2 = mb_write_begin(&db);
    T("Q32 reader가 1을 들고 있으니 writer는 0을 쓴다", w2 == &db.buf[0]);
    mb_write_commit(&db);
    T("Q32 commit이 publish (ready=0, write=-1)",
      db.ready_idx == 0 && db.write_idx == -1);

    unsigned long before = db.starved;
    Frame *w3 = mb_write_begin(&db);
    T("Q32 nbuf=2에서 빈 버퍼 없음 → READY(0)를 희생",
      w3 == &db.buf[0] && db.starved == before + 1 && db.ready_idx == -1);
    mb_write_commit(&db);

    mb_read_release(&db);
    T("Q32 release 후 reader_idx=-1", db.reader_idx == -1);
    Frame *r1 = mb_read_acquire(&db);
    T("Q32 release 뒤에는 다시 acquire 가능", r1 != NULL);
    mb_read_release(&db);
    mb_destroy(&db);

    // ---------------- Q33 : tearing 검출 + 불변식 ----------------
    printf("== Q33 tearing 검출과 회계 불변식 ==\n");
    Frame f = {0};
    frame_fill(&f, 0x1234u);
    T("Q33 frame_fill로 채운 프레임은 torn이 아니다", frame_is_torn(&f) == false);
    f.data[FRAME_LEN / 2] ^= 0xFFu;
    T("Q33 한 바이트만 달라도 torn으로 검출", frame_is_torn(&f) == true);
    T("Q33 NULL은 안전하게 torn 취급", frame_is_torn(NULL) == true);

    MBuf d2 = {0};
    mb_init(&d2, 2);
    PingArgs a2;
    bool acc2 = pp_run(&d2, 8000u, &a2);
    printf("     [info] nbuf=2 read=%lu dropped=%lu starved=%lu\n",
           a2.read_ok, d2.dropped, d2.starved);
    T("Q33 더블 버퍼: tearing 0건", a2.torn == 0);
    T("Q33 더블 버퍼: 순서 역전 0건 (항상 최신 프레임)", a2.out_of_order == 0);
    T("Q33 더블 버퍼: 최소 1프레임은 소비된다", a2.read_ok >= 1);
    T("Q33 회계 불변식 read+dropped+leftover+inflight == produced", acc2);
    T("Q33 소비된 프레임 수는 생산 수를 넘지 않는다", a2.read_ok <= 8000UL);
    mb_destroy(&d2);

    // ---------------- Q34 : 트리플 버퍼 ----------------
    printf("== Q34 트리플 버퍼 (nbuf=3) ==\n");
    MBuf t1 = {0};
    mb_init(&t1, 3);
    T("Q34 init nbuf=3", t1.nbuf == 3);
    // 결정적 시나리오: reader가 1개 + READY 1개여도 writer는 FREE를 찾는다
    mb_write_begin(&t1); mb_write_commit(&t1);        // ready=0
    Frame *tr = mb_read_acquire(&t1);                  // reader=0
    mb_write_begin(&t1); mb_write_commit(&t1);        // ready=1
    Frame *tw = mb_write_begin(&t1);                   // FREE=2 가 남아있다
    T("Q34 reader 1 + READY 1 이어도 세 번째 버퍼가 남는다", tw == &t1.buf[2]);
    T("Q34 그러므로 starved(=빈 버퍼 없음)는 0", t1.starved == 0);
    T("Q34 reader가 든 버퍼는 절대 뺏기지 않는다", tr == &t1.buf[0] && t1.reader_idx == 0);
    mb_write_commit(&t1);
    mb_read_release(&t1);
    mb_destroy(&t1);

    MBuf d3 = {0};
    mb_init(&d3, 3);
    PingArgs a3;
    bool acc3 = pp_run(&d3, 8000u, &a3);
    printf("     [info] nbuf=3 read=%lu dropped=%lu starved=%lu\n",
           a3.read_ok, d3.dropped, d3.starved);
    T("Q34 스레드 테스트에서도 writer는 한 번도 굶지 않는다", d3.starved == 0);
    T("Q34 트리플 버퍼: tearing 0건", a3.torn == 0 && a3.out_of_order == 0);
    T("Q34 회계 불변식 유지", acc3);
    mb_destroy(&d3);

    // ---------------- Q35 : latest-value 슬롯 ----------------
    printf("== Q35 latest-value 슬롯 (mutex vs seqlock) ==\n");
    LvMutex lv = {0};
    lv_init(&lv);
    LinkSample got;
    T("Q35 mutex: 값 쓰기 전 load는 false", lv_load(&lv, &got) == false);
    LinkSample s1 = { 1000, -52, 0 };
    s1.check = sample_check(s1.ts_us, s1.rssi_dbm);
    lv_store(&lv, s1);
    T("Q35 mutex: store 후 load 성공 + 값 일치",
      lv_load(&lv, &got) && got.ts_us == 1000 && got.rssi_dbm == -52);
    LinkSample s2 = { 2000, -61, 0 };
    s2.check = sample_check(s2.ts_us, s2.rssi_dbm);
    lv_store(&lv, s2);
    T("Q35 mutex: 최신값으로 덮어쓴다(오래된 값은 사라진다)",
      lv_load(&lv, &got) && got.ts_us == 2000);
    T("Q35 mutex: out이 NULL이면 false", lv_load(&lv, NULL) == false);
    lv_destroy(&lv);

    SeqSlot sl = {0};
    sl_init(&sl);
    T("Q35 seqlock: 초기 seq는 짝수(안정 상태)", (sl_seq(&sl) & 1u) == 0u);
    sl_store(&sl, s2);
    T("Q35 seqlock: 단일 스레드 store/load 왕복",
      sl_load(&sl, &got) && got.ts_us == 2000 && sample_consistent(&got));
    T("Q35 seqlock: store 1회에 seq는 2 증가", sl_seq(&sl) == 2u);

    SlArgs sa;
    memset(&sa, 0, sizeof sa);
    sa.sl = &sl;
    pthread_t wt, rt;
    pthread_create(&wt, NULL, sl_writer, &sa);
    pthread_create(&rt, NULL, sl_reader, &sa);
    pthread_join(wt, NULL);
    pthread_join(rt, NULL);
    printf("     [info] seqlock succ=%lu retry_exhausted=%lu torn=%lu\n",
           sa.succ, sa.retry_exhausted, sa.torn);
    T("Q35 seqlock: 찢어진 값이 새어나온 적 0건", sa.torn == 0);
    T("Q35 seqlock: 모든 시도는 성공 또는 재시도초과 둘 중 하나",
      sa.succ + sa.retry_exhausted == (unsigned long)SL_ATTEMPTS);
    T("Q35 seqlock: writer 종료 후 seq는 짝수", (sl_seq(&sl) & 1u) == 0u);
    T("Q35 seqlock: 정지 상태 load는 성공하고 일관적",
      sl_load(&sl, &got) && sample_consistent(&got));

    // ---------------- Q36 : 레이트 디커플링 ----------------
    printf("== Q36 레이트 디커플링 400Hz -> 100Hz ==\n");
    const int32_t ramp[8]  = { 10, 20, 30, 40, 50, 60, 70, 80 };
    const int32_t noisy[4] = { 1000, -1000, 1000, -1000 };
    int32_t out[4];

    T("Q36 rd_latest: 블록의 마지막 샘플", rd_latest(ramp, 4) == 40);
    T("Q36 rd_mean: 블록 평균(반올림)", rd_mean(ramp, 4) == 25);
    T("Q36 rd_mean: 대칭 노이즈는 0으로 수렴", rd_mean(noisy, 4) == 0);
    T("Q36 rd_latest: 같은 노이즈에서는 순간값 그대로", rd_latest(noisy, 4) == -1000);

    const int32_t neg[4] = { -10, -20, -30, -41 };     // 합 -101 → -25.25
    T("Q36 rd_mean: 음수도 0에서 먼 쪽으로 반올림", rd_mean(neg, 4) == -25);

    int n = rd_decimate(ramp, 8, 4, RD_LATEST, out, 4);
    T("Q36 decimate 4:1 → 8샘플에서 2출력(LATEST)",
      n == 2 && out[0] == 40 && out[1] == 80);
    n = rd_decimate(ramp, 8, 4, RD_MEAN, out, 4);
    T("Q36 decimate 4:1 → 평균 정책", n == 2 && out[0] == 25 && out[1] == 65);
    n = rd_decimate(ramp, 7, 4, RD_LATEST, out, 4);
    T("Q36 꽉 찬 블록만 처리(꼬리 3샘플은 다음 호출로)", n == 1 && out[0] == 40);
    n = rd_decimate(ramp, 8, 4, RD_MEAN, out, 1);
    T("Q36 out_cap을 넘지 않는다", n == 1);
    T("Q36 NULL/0 입력 방어",
      rd_decimate(NULL, 8, 4, RD_MEAN, out, 4) == 0 &&
      rd_decimate(ramp, 8, 0, RD_MEAN, out, 4) == 0 &&
      rd_mean(NULL, 4) == 0 && rd_latest(NULL, 4) == 0);

    // ---------------- Q37 : DMA ping-pong ----------------
    printf("== Q37 DMA ping-pong (ISR은 인덱스 교환만) ==\n");
    DmaPP dma = {0};
    dma_pp_init(&dma);
    T("Q37 init: active=0, full 없음, overrun=0",
      dma_pp_active(&dma) == 0 && dma_pp_take(&dma, NULL) == NULL &&
      dma_pp_overrun(&dma) == 0);

    memset(dma.buf[0], 0xA5, DMA_LEN);                 // 'DMA가 채운' 것으로 간주
    dma_pp_isr_complete(&dma);
    T("Q37 완료 후 DMA는 반대쪽 버퍼로 넘어간다", dma_pp_active(&dma) == 1);

    int idx = -1;
    unsigned char *p = dma_pp_take(&dma, &idx);
    T("Q37 take는 복사 없이 그 버퍼의 포인터를 준다", p == dma.buf[0] && idx == 0);
    T("Q37 ISR이 데이터를 건드리지 않았다(내용 보존)",
      p != NULL && p[0] == 0xA5 && p[DMA_LEN - 1] == 0xA5);
    T("Q37 가져간 뒤 full은 비워진다", dma_pp_take(&dma, NULL) == NULL);

    dma_pp_init(&dma);
    dma_pp_isr_complete(&dma);
    dma_pp_isr_complete(&dma);                         // 앱이 안 가져간 사이 또 완료
    T("Q37 앱이 못 가져가면 overrun 카운트", dma_pp_overrun(&dma) == 1);
    T("Q37 overrun이어도 최신 버퍼는 가져갈 수 있다(최신 우선)",
      dma_pp_take(&dma, &idx) != NULL && idx == 1);

    dma_pp_init(&dma);
    for (int i = 0; i < 4; i++) { dma_pp_isr_complete(&dma); dma_pp_take(&dma, NULL); }
    T("Q37 제때 가져가면 overrun 0 + active는 4회 후 제자리",
      dma_pp_overrun(&dma) == 0 && dma_pp_active(&dma) == 0);

    dma_pp_init(&dma);
    dma_pp_isr_complete(&dma);
    dma_pp_take(&dma, &idx);                           // 앱이 buf[0]을 처리 중
    dma_pp_isr_complete(&dma);
    T("Q37 2버퍼의 데드라인: 다음 완료에서 DMA가 앱의 버퍼를 다시 쓴다",
      idx == 0 && dma_pp_active(&dma) == 0);

    // ---------------- Q38 : 프레임 지터/누락 ----------------
    printf("== Q38 프레임 지터/누락 측정 ==\n");
    const unsigned long nominal = 10000UL;             // 100Hz → 10ms
    const unsigned long clean[4]   = { 0, 10000, 20000, 30000 };
    const unsigned long dropped1[4]= { 0, 10000, 30000, 40000 };  // 1프레임 누락
    const unsigned long jitter[3]  = { 0, 10400,  20000 };        // ±4% 지터
    const unsigned long bigstall[2]= { 0, 55000 };                // 5.5주기 정지
    const unsigned long backward[4]= { 0, 10000,  5000, 15000 };

    JitterStats js = jitter_analyze(clean, 4, nominal);
    T("Q38 정상 스트림: 누락 0, 지터 0",
      js.n_gaps == 3 && js.missed == 0 && js.max_jitter_us == 0);
    T("Q38 정상 스트림: max/min gap = nominal",
      js.max_gap_us == 10000 && js.min_gap_us == 10000);

    js = jitter_analyze(dropped1, 4, nominal);
    T("Q38 한 프레임 누락 검출", js.missed == 1);
    T("Q38 누락 구간이 최대 간격/최대 지터",
      js.max_gap_us == 20000 && js.max_jitter_us == 10000);

    js = jitter_analyze(jitter, 3, nominal);
    T("Q38 정상 범위 지터를 누락으로 오판하지 않는다", js.missed == 0);
    T("Q38 지터 값은 그대로 보고", js.max_jitter_us == 400);

    js = jitter_analyze(bigstall, 2, nominal);
    T("Q38 긴 정지(5.5주기)는 반올림해 5프레임 누락", js.missed == 5);

    js = jitter_analyze(backward, 4, nominal);
    T("Q38 시계 역행을 별도 카운트(누락으로 뒤섞지 않음)",
      js.nonmonotonic == 1 && js.n_gaps == 3);

    js = jitter_analyze(clean, 1, nominal);
    T("Q38 샘플 1개 이하는 0 결과", js.n_gaps == 0 && js.missed == 0);
    js = jitter_analyze(NULL, 4, nominal);
    T("Q38 NULL 방어", js.n_gaps == 0);
    js = jitter_analyze(clean, 4, 0);
    T("Q38 nominal=0 방어(0 나누기 없음)", js.n_gaps == 0 && js.missed == 0);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
