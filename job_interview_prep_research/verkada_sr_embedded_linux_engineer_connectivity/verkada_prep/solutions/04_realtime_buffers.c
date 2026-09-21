// 04_realtime_buffers.c  —  REFERENCE SOLUTION
// 실시간 버퍼링 (더블/트리플 버퍼) (Real-time Buffering (Double/Triple))  —  Q31~Q38
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra -O1 -g -pthread 04_realtime_buffers.c -o /tmp/vk04 && /tmp/vk04
// TSan: cc -std=c11 -Wall -Wextra -O1 -g -fsanitize=thread -pthread 04_realtime_buffers.c -o /tmp/vk04t && /tmp/vk04t
//
// 리크루터 메일이 "real-time data handling patterns (such as double buffering)"을
// 대놓고 지목했다. 핵심 사고방식 하나: **락으로 '데이터'를 보호하지 말고 '버퍼
// 소유권'을 보호한다.** 임계구역은 인덱스 몇 개 교환하는 것뿐이고(수 나노초),
// 프레임 바이트 복사는 락 밖에서 일어난다. 생산자(센서/DMA/카메라)는 절대
// 블로킹되지 않고, 소비자가 느리면 오래된 프레임을 버리되 반드시 카운트한다.
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
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ===========================================================================
// 공통 타입 — 프레임과 N-버퍼 풀
// ---------------------------------------------------------------------------
// 버퍼 하나는 언제나 셋 중 한 상태다:
//   WRITER 소유(write_idx) / READY(publish됨, ready_idx) / READER 소유(reader_idx)
// 어느 상태에도 속하지 않으면 FREE. 인덱스 세 개가 곧 소유권 장부다.
// nbuf==2 면 더블 버퍼링, nbuf==3 이면 트리플 버퍼링 — 코드는 같다.
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

// ===========================================================================
// Q31. 더블 버퍼 소유권 모델 — 초기화 + write_begin
// ---------------------------------------------------------------------------
// mb_write_begin: writer가 채울 빈(FREE) 버퍼를 확보한다. **항상 성공한다** —
// 실시간 경로(센서 ISR/DMA 완료 콜백)는 소비자를 기다릴 수 없기 때문이다.
// 빈 버퍼가 없으면(reader가 하나 + READY가 하나 점유) READY를 희생시키고
// dropped/starved를 올린다. reader가 든 버퍼는 절대 건드리지 않는다 — 그게
// 곧 tearing이다. GC31-E 게이트웨이의 링크 통계 스냅샷, MT81 트레일러 카메라
// 프레임처럼 "최신값만 중요한" 데이터의 표준 패턴.
// ===========================================================================
void mb_init(MBuf *mb, int nbuf)
{
    if (!mb) return;
    memset(mb, 0, sizeof *mb);
    if (nbuf < 2)      nbuf = 2;        // 2 미만은 소유권 모델이 성립하지 않는다
    if (nbuf > MAXBUF) nbuf = MAXBUF;
    mb->nbuf       = nbuf;
    mb->write_idx  = -1;
    mb->ready_idx  = -1;
    mb->reader_idx = -1;
    pthread_mutex_init(&mb->m, NULL);
}

void mb_destroy(MBuf *mb)
{
    if (!mb) return;
    pthread_mutex_destroy(&mb->m);
}

Frame *mb_write_begin(MBuf *mb)
{
    if (!mb) return NULL;
    pthread_mutex_lock(&mb->m);
    int idx = -1;
    for (int i = 0; i < mb->nbuf; i++) {          // 아무도 소유하지 않은 버퍼 찾기
        if (i != mb->ready_idx && i != mb->reader_idx) { idx = i; break; }
    }
    if (idx < 0) {
        // nbuf>=2 이면 여기 도달했다는 건 reader 1 + READY 1 로 꽉 찬 경우뿐이다.
        // → READY를 희생한다. (reader 버퍼를 뺏으면 tearing)
        idx = mb->ready_idx;
        mb->ready_idx = -1;
        mb->dropped++;
        mb->starved++;
    }
    mb->write_idx = idx;
    pthread_mutex_unlock(&mb->m);
    return &mb->buf[idx];                          // 데이터 write는 락 '밖'에서
}

// ===========================================================================
// Q32. write_commit (publish) / read_acquire / read_release — 소유권 이전
// ---------------------------------------------------------------------------
// commit: writer 소유 → READY. 이전 READY가 아직 안 읽혔으면 그건 버려진다(dropped++).
// acquire: READY → reader 소유. 없으면 NULL(논블로킹). reader가 이미 하나를
//          들고 있으면 반납 전까지 NULL — 한 번에 한 프레임만 대여한다.
// release: reader 소유 → FREE.
// 세 함수 모두 임계구역 안에서 하는 일은 int 대입 몇 줄뿐이다.
// ===========================================================================
void mb_write_commit(MBuf *mb)
{
    if (!mb) return;
    pthread_mutex_lock(&mb->m);
    if (mb->write_idx >= 0) {
        if (mb->ready_idx >= 0)
            mb->dropped++;                         // 소비 전에 더 새 프레임이 나옴
        mb->ready_idx = mb->write_idx;             // publish
        mb->write_idx = -1;
    }
    pthread_mutex_unlock(&mb->m);
}

Frame *mb_read_acquire(MBuf *mb)
{
    if (!mb) return NULL;
    Frame *f = NULL;
    pthread_mutex_lock(&mb->m);
    if (mb->reader_idx < 0 && mb->ready_idx >= 0) {
        mb->reader_idx = mb->ready_idx;            // READY를 reader 소유로 이전
        mb->ready_idx  = -1;
        f = &mb->buf[mb->reader_idx];
    }
    pthread_mutex_unlock(&mb->m);
    return f;
}

void mb_read_release(MBuf *mb)
{
    if (!mb) return;
    pthread_mutex_lock(&mb->m);
    mb->reader_idx = -1;                           // 다시 FREE
    pthread_mutex_unlock(&mb->m);
}

// ===========================================================================
// Q33. tearing 검출 + 회계 불변식
// ---------------------------------------------------------------------------
// tearing = 소비자가 '절반만 갱신된' 프레임을 보는 것. 검출하려면 프레임 전체를
// seq에서 유도한 같은 패턴으로 채우고, 읽을 때 전 바이트가 그 패턴인지 본다.
// 한 바이트라도 다르면 두 세대의 데이터가 섞인 것 = torn.
//
// 회계 불변식(면접에서 반드시 말할 문장):
//   consumed + dropped + leftover(READY) + inflight(WRITER) == produced
// 즉 생산된 모든 프레임은 "읽혔거나, 버려졌거나, 아직 남아있다" 셋 중 하나다.
// 조용한 손실(silent loss)이 실시간 시스템에서 가장 디버깅하기 어렵다.
// ===========================================================================
void frame_fill(Frame *f, unsigned seq)
{
    if (!f) return;
    f->seq = seq;
    memset(f->data, (int)(seq & 0xFFu), FRAME_LEN); // 전 바이트를 같은 패턴으로
}

bool frame_is_torn(const Frame *f)
{
    if (!f) return true;                            // NULL은 안전하게 "찢어짐" 취급
    unsigned char expect = (unsigned char)(f->seq & 0xFFu);
    for (int i = 0; i < FRAME_LEN; i++)
        if (f->data[i] != expect) return true;
    return false;
}

// 스레드 join 후(정지 상태)에만 호출한다 — 락 없이 장부를 읽는다.
bool mb_accounting_ok(const MBuf *mb, unsigned long produced, unsigned long consumed)
{
    if (!mb) return false;
    unsigned long leftover = (mb->ready_idx >= 0) ? 1UL : 0UL;
    unsigned long inflight = (mb->write_idx  >= 0) ? 1UL : 0UL;
    return consumed + mb->dropped + leftover + inflight == produced;
}

// ---- 생산자/소비자 스레드 (Q33/Q34 공용) -----------------------------------
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
        // 실제 센서는 프레임 사이에 간격이 있다. 여기서는 yield로 소비자에게
        // 스케줄 기회를 준다 — 단정은 전부 '불변식'이라 결과는 여전히 결정적이다.
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
            continue;                               // 실전이라면 condvar로 대기
        }
        if (frame_is_torn(f))  a->torn++;
        if (f->seq < last)     a->out_of_order++;   // 항상 최신만 오므로 단조 증가
        last = f->seq;
        a->read_ok++;
        mb_read_release(a->mb);
    }
    return NULL;
}

// 스레드 테스트 1회 실행. 반환: 회계 불변식 통과 여부.
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

// ===========================================================================
// Q34. 트리플 버퍼로 확장 (nbuf=3)
// ---------------------------------------------------------------------------
// 더블 버퍼의 약점: reader가 하나를 들고 있고 다른 하나가 READY인 순간
// writer는 쓸 곳이 없어 READY를 빼앗는다(starved++). 버퍼를 하나 더 두면
// "reader 1 + READY 1 + writer 1"이 동시에 성립하므로 **writer는 절대 굶지
// 않는다**. 대가는 메모리 +50% (1080p RGB 프레임이면 ~6MB → ~9MB).
// 주의: 트리플이어도 commit 시 '아직 안 읽힌 READY를 덮어쓰는' drop은 남는다.
// 그건 writer가 막힌 게 아니라 소비자가 느려 프레임이 낡은 것 — 다른 현상이다.
// ===========================================================================
// (구현은 mb_* 그대로. nbuf만 3으로 주면 된다 — 그게 이 설계의 요점이다.)

// ===========================================================================
// Q35. latest-value 슬롯 — mutex 버전 vs seqlock 버전
// ---------------------------------------------------------------------------
// "최신값 하나"만 필요한 작은 구조체(링크 통계, 배터리 상태, GPS fix)는 버퍼
// 풀까지 갈 필요가 없다. 두 가지 선택지:
//   (A) mutex: 단순·정확. writer가 reader를 잠깐 막는다(우선순위 역전 가능).
//   (B) seqlock: writer가 **절대 막히지 않는다**. reader는 찢어진 읽기를
//       감지하고 재시도한다. 읽기 多 · 쓰기 少 · 페이로드 小 일 때 최적.
// seqlock은 writer가 하나뿐이어야 하고, 페이로드에 포인터가 있으면 위험하다
// (찢어진 포인터를 역참조하게 된다). 그래서 POD 스냅샷 전용.
// ===========================================================================
typedef struct {
    uint64_t ts_us;
    int32_t  rssi_dbm;
    int32_t  check;        // ts/rssi에서 유도 — 찢어진 읽기 검출용
} LinkSample;

static int32_t sample_check(uint64_t ts, int32_t rssi)
{
    return (int32_t)(ts & 0x7FFFFFFFu) ^ rssi;
}

bool sample_consistent(const LinkSample *s)
{
    if (!s) return false;
    return s->check == sample_check(s->ts_us, s->rssi_dbm);
}

// ---- (A) mutex 버전 --------------------------------------------------------
typedef struct {
    LinkSample      v;
    bool            valid;
    pthread_mutex_t m;
} LvMutex;

void lv_init(LvMutex *lv)
{
    if (!lv) return;
    memset(lv, 0, sizeof *lv);
    pthread_mutex_init(&lv->m, NULL);
}

void lv_destroy(LvMutex *lv)
{
    if (!lv) return;
    pthread_mutex_destroy(&lv->m);
}

void lv_store(LvMutex *lv, LinkSample s)
{
    if (!lv) return;
    pthread_mutex_lock(&lv->m);
    lv->v     = s;
    lv->valid = true;
    pthread_mutex_unlock(&lv->m);
}

bool lv_load(LvMutex *lv, LinkSample *out)
{
    if (!lv || !out) return false;
    bool ok;
    pthread_mutex_lock(&lv->m);
    ok = lv->valid;
    if (ok) *out = lv->v;
    pthread_mutex_unlock(&lv->m);
    return ok;
}

// ---- (B) seqlock 버전 ------------------------------------------------------
// seq 홀수 = 쓰기 진행 중. 짝수 = 안정. reader는 앞뒤 seq가 같고 짝수일 때만 채택.
// 페이로드를 _Atomic + relaxed 로 두는 이유: 알고리즘상 "읽고 버리는" 경합이
// 정상 동작이지만, 일반 변수로 두면 그것이 C 표준상 data race(UB)이고
// ThreadSanitizer도 경고한다. relaxed atomic + fence면 의미는 같고 UB가 아니다.
#define SL_MAX_RETRY 1000

typedef struct {
    _Atomic unsigned seq;          // 짝수=안정, 홀수=쓰기 중
    _Atomic uint64_t ts_us;
    _Atomic int32_t  rssi_dbm;
    _Atomic int32_t  check;
} SeqSlot;

void sl_init(SeqSlot *sl)
{
    if (!sl) return;
    atomic_init(&sl->seq, 0u);
    atomic_init(&sl->ts_us, (uint64_t)0);
    atomic_init(&sl->rssi_dbm, (int32_t)0);
    atomic_init(&sl->check, sample_check(0, 0));
}

void sl_store(SeqSlot *sl, LinkSample s)
{
    if (!sl) return;
    unsigned s0 = atomic_load_explicit(&sl->seq, memory_order_relaxed);
    atomic_store_explicit(&sl->seq, s0 + 1u, memory_order_relaxed);   // 홀수 진입
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&sl->ts_us,    s.ts_us,    memory_order_relaxed);
    atomic_store_explicit(&sl->rssi_dbm, s.rssi_dbm, memory_order_relaxed);
    atomic_store_explicit(&sl->check,    s.check,    memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&sl->seq, s0 + 2u, memory_order_release);   // 짝수 복귀
}

// 재시도 상한이 있다 — 미구현/폭주 상태에서도 절대 무한 루프에 빠지지 않는다.
bool sl_load(const SeqSlot *sl, LinkSample *out)
{
    if (!sl || !out) return false;
    for (int try_i = 0; try_i < SL_MAX_RETRY; try_i++) {
        unsigned s1 = atomic_load_explicit(&sl->seq, memory_order_acquire);
        if (s1 & 1u) continue;                       // writer가 쓰는 중
        LinkSample tmp;
        tmp.ts_us    = atomic_load_explicit(&sl->ts_us,    memory_order_relaxed);
        tmp.rssi_dbm = atomic_load_explicit(&sl->rssi_dbm, memory_order_relaxed);
        tmp.check    = atomic_load_explicit(&sl->check,    memory_order_relaxed);
        atomic_thread_fence(memory_order_acquire);
        unsigned s2 = atomic_load_explicit(&sl->seq, memory_order_relaxed);
        if (s1 == s2) { *out = tmp; return true; }   // 읽는 동안 안 바뀜 → 채택
    }
    return false;                                     // 재시도 상한 초과
}

unsigned sl_seq(const SeqSlot *sl)
{
    if (!sl) return 0u;
    return atomic_load_explicit(&sl->seq, memory_order_acquire);
}

// ---- seqlock 스레드 테스트 -------------------------------------------------
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
            if (!sample_consistent(&s)) a->torn++;   // 찢어진 값이 새어나왔는가
        } else {
            a->retry_exhausted++;
        }
    }
    return NULL;
}

// ===========================================================================
// Q36. 레이트 디커플링 — 400Hz 생산 → 100Hz 소비
// ---------------------------------------------------------------------------
// 4:1 데시메이션. 두 정책 모두 순수 함수로 분리해서 하드웨어 없이 테스트한다.
//   RD_LATEST: 블록의 마지막 샘플 채택. 지연 최소, 계단/이벤트성 신호에 적합
//              (링크 up/down, 도어 상태, 최신 GPS fix).
//   RD_MEAN  : 블록 평균. 노이즈 억제, 연속 아날로그 신호에 적합
//              (RSSI, 온도, 전류, 공기질 PM2.5).
// 판단 기준: "이 신호의 순간값이 의미가 있나, 아니면 추세가 의미가 있나?"
// 평균은 에일리어싱을 줄이는 대신 계단 엣지를 뭉갠다(지연 ~블록 절반).
// ===========================================================================
typedef enum { RD_LATEST = 0, RD_MEAN = 1 } RatePolicy;

// 반올림 나눗셈(0에서 먼 쪽으로). den > 0 가정.
static int32_t round_div(int64_t num, int64_t den)
{
    if (den <= 0) return 0;
    if (num >= 0) return (int32_t)((num + den / 2) / den);
    return (int32_t)((num - den / 2) / den);
}

int32_t rd_latest(const int32_t *blk, int n)
{
    if (!blk || n <= 0) return 0;
    return blk[n - 1];
}

int32_t rd_mean(const int32_t *blk, int n)
{
    if (!blk || n <= 0) return 0;
    int64_t sum = 0;
    for (int i = 0; i < n; i++) sum += blk[i];       // int64 누적 → 오버플로 방지
    return round_div(sum, n);
}

// in[n_in]을 factor:1로 줄여 out에 쓴다. 꽉 찬 블록만 처리(꼬리는 다음 호출로 미룸).
// 반환: 실제로 쓴 출력 개수.
int rd_decimate(const int32_t *in, int n_in, int factor,
                RatePolicy pol, int32_t *out, int out_cap)
{
    if (!in || !out || n_in <= 0 || factor <= 0 || out_cap <= 0) return 0;
    int nblocks = n_in / factor;
    if (nblocks > out_cap) nblocks = out_cap;
    for (int b = 0; b < nblocks; b++) {
        const int32_t *blk = in + (size_t)b * (size_t)factor;
        out[b] = (pol == RD_MEAN) ? rd_mean(blk, factor) : rd_latest(blk, factor);
    }
    return nblocks;
}

// ===========================================================================
// Q37. DMA ping-pong 시뮬레이션
// ---------------------------------------------------------------------------
// 하드웨어 DMA가 buf[0]을 채우는 동안 CPU는 buf[1]을 처리한다. 전송 완료
// 인터럽트에서 하는 일은 **인덱스 교환뿐**이다 — 데이터 복사도, 파싱도,
// 락 대기도 하지 않는다(ISR은 수 마이크로초 안에 끝나야 하고, 블로킹 API는
// 애초에 호출 불가).
//
// ⚠ 캐시 주의(실제 하드웨어): DMA는 캐시를 우회해 DRAM에 직접 쓴다.
//   - DMA가 채운 버퍼를 CPU가 읽기 전  → invalidate (오래된 캐시 라인 폐기)
//   - CPU가 채운 버퍼를 DMA가 읽기 전  → clean/flush (쓰기버퍼→DRAM 반영)
//   버퍼는 캐시 라인 정렬 + 라인 크기 배수여야 한다. 아니면 인접 변수까지
//   같이 무효화되어 "가끔 깨지는" 최악의 버그가 된다. Linux에서는 보통
//   dma_map_single()/dma_unmap_single()(스트리밍) 또는 dma_alloc_coherent()
//   (논캐시 메모리)가 이걸 대신해준다.
//
// 버퍼가 2개뿐이면 소비자의 데드라인은 **전송 1주기**다. 그 안에 못 가져가면
// overrun이고, 가져간 뒤에도 다음 완료에서 DMA가 그 버퍼를 다시 쓰기 시작한다
// → 처리시간이 주기를 넘으면 트리플 버퍼(Q34)로 가야 한다.
// ===========================================================================
#define DMA_LEN 64

typedef struct {
    unsigned char        buf[2][DMA_LEN];
    int                  active;      // DMA가 지금 채우는 버퍼 (ISR 컨텍스트 전용)
    _Atomic int          full;        // 앱이 가져갈 수 있는 버퍼, 없으면 -1
    _Atomic unsigned long overrun;    // 앱이 못 가져간 채 덮인 횟수
    _Atomic unsigned long completes;  // 전송 완료 인터럽트 횟수
} DmaPP;

void dma_pp_init(DmaPP *d)
{
    if (!d) return;
    memset(d, 0, sizeof *d);
    d->active = 0;
    atomic_init(&d->full, -1);
    atomic_init(&d->overrun, 0UL);
    atomic_init(&d->completes, 0UL);
}

// ISR 컨텍스트. 인덱스 교환만 — 복사/처리 금지.
void dma_pp_isr_complete(DmaPP *d)
{
    if (!d) return;
    int done   = d->active;
    d->active  = done ^ 1;                          // 다음 전송은 반대쪽 버퍼로
    int prev   = atomic_exchange_explicit(&d->full, done, memory_order_acq_rel);
    if (prev >= 0)                                  // 직전 것을 앱이 못 가져갔다
        atomic_fetch_add_explicit(&d->overrun, 1UL, memory_order_relaxed);
    atomic_fetch_add_explicit(&d->completes, 1UL, memory_order_relaxed);
}

// 앱 컨텍스트. full 버퍼를 '가져간다'(복사하지 않고 포인터만 넘긴다).
unsigned char *dma_pp_take(DmaPP *d, int *idx_out)
{
    if (!d) return NULL;
    int idx = atomic_exchange_explicit(&d->full, -1, memory_order_acq_rel);
    if (idx < 0) return NULL;
    if (idx_out) *idx_out = idx;
    return d->buf[idx];
}

int dma_pp_active(const DmaPP *d)
{
    return d ? d->active : -1;
}

unsigned long dma_pp_overrun(const DmaPP *d)
{
    return d ? atomic_load_explicit(&d->overrun, memory_order_relaxed) : 0UL;
}

// ===========================================================================
// Q38. 프레임 지터/누락 측정
// ---------------------------------------------------------------------------
// 타임스탬프 배열 하나만 받는 **순수 함수**로 만든다. 그래야 카메라도 커널도
// 없이 단위 테스트가 가능하다(모듈성·테스트 가능성 항목에 그대로 해당).
// 놓친 프레임 수는 간격을 nominal로 '반올림' 나눗셈해 추정한다 — 그래야
// 정상 범위의 지터(±40%)를 누락으로 오판하지 않는다.
// ===========================================================================
typedef struct {
    int           n_gaps;          // 간격 개수 (= n-1)
    unsigned long missed;          // 추정 누락 프레임 수
    unsigned long max_gap_us;      // 최대 간격
    unsigned long min_gap_us;      // 최소 간격
    unsigned long max_jitter_us;   // max |gap - nominal|
    unsigned long nonmonotonic;    // 타임스탬프가 거꾸로 간 횟수
} JitterStats;

JitterStats jitter_analyze(const unsigned long *ts_us, int n, unsigned long nominal_us)
{
    JitterStats st;
    memset(&st, 0, sizeof st);
    if (!ts_us || n < 2 || nominal_us == 0) return st;   // 안전한 0 결과

    unsigned long min_gap = ULONG_MAX;
    for (int i = 1; i < n; i++) {
        unsigned long gap;
        if (ts_us[i] < ts_us[i - 1]) {                   // 시계 역행 / 잘못된 입력
            st.nonmonotonic++;
            gap = 0;
        } else {
            gap = ts_us[i] - ts_us[i - 1];
        }
        st.n_gaps++;
        if (gap > st.max_gap_us) st.max_gap_us = gap;
        if (gap < min_gap)       min_gap = gap;

        unsigned long jit = (gap > nominal_us) ? gap - nominal_us : nominal_us - gap;
        if (jit > st.max_jitter_us) st.max_jitter_us = jit;

        // 간격을 nominal의 몇 배로 볼 것인가 — 반올림
        unsigned long k = (gap + nominal_us / 2) / nominal_us;
        if (k > 1) st.missed += k - 1;                   // k개 주기 = k-1개 누락
    }
    st.min_gap_us = (st.n_gaps > 0) ? min_gap : 0UL;
    return st;
}

// ===========================================================================
// main — 테스트
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
