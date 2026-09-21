// 06_isr_timing.c  —  PRACTICE STUB (직접 채워넣기)
// ISR 핸드셰이크 / 틱 산술 / 협력형 스케줄링 (embedded fundamentals)  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=06_isr_timing
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g problems/06_isr_timing.c -o /tmp/n06p && /tmp/n06p
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다.)
//
// 왜 이 토픽인가 (embedded fundamentals — 어느 펌웨어 면접에서나 나온다):
//   - 비행 펌웨어의 골격은 단 한 문장이다: "ISR 에서는 짧게(플래그/큐에 넣고 끝), 나머지는
//     main loop / 태스크에서." 이 경계를 못 그으면 8kHz 제어 루프가 바로 지터를 먹는다.
//   - 32비트 ms 틱은 49.7일에 랩한다. `now > deadline` 로 쓴 타임아웃은 그날 전부 오동작한다.
//     wrap-safe 비교 `(int32_t)(b - a) < 0` 는 타협 불가능한 기본기다.
//   - 주기 태스크는 "period 만큼 더하기"로 재스케줄해야 드리프트가 누적되지 않고, 시스템이
//     멈췄다 돌아왔을 때는 밀린 주기를 몰아서 실행(burst)하는 대신 건너뛰어야 루프가 산다.
//   - 이 파일은 전부 호스트 C 로 테스트한다: 인터럽트는 main 에서 isr_event()/isr_tick() 같은
//     함수를 "인터럽트가 난 척" 직접 호출해서 시뮬레이션한다. 하드웨어 없이 로직만 검증.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <signal.h>     // sig_atomic_t
#include <stdatomic.h>  // atomic_signal_fence (컴파일러 배리어)

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
// Q1 : ISR -> main 플래그 핸드셰이크 (flag + payload)
// ---------------------------------------------------------------------------
// 가장 기본적인 ISR↔main 통신 패턴. ISR 은 payload 를 쓰고 "다 됐다"는 깃발을 세운다.
// main loop 는 깃발을 보고 payload 를 가져간 뒤 깃발을 내린다.
//
// 반드시 지켜야 하는 두 가지:
//   1) 순서: payload 를 **먼저** 쓰고 flag 를 **나중에** 세운다.
//      뒤집히면 main 이 "데이터 있다"를 보고 아직 안 쓴 쓰레기를 읽는다.
//   2) 배리어: `volatile` 은 **컴파일러에게** "이 변수는 레지스터에 캐싱하지 말고 매번
//      메모리에서 읽어라"라고 말할 뿐이다. volatile 접근끼리의 순서는 보장되지만,
//      volatile 접근과 **비-volatile** 접근 사이의 재정렬은 막지 못하고, CPU 의
//      out-of-order/store buffer 재정렬은 **전혀** 막지 못한다.
//      -> 단일코어 MCU(ISR↔main)에서는 컴파일러 배리어(atomic_signal_fence)면 충분하고,
//         멀티코어/SMP 라면 진짜 메모리 배리어(atomic_thread_fence / DMB)가 필요하다.
//      한 줄 요약: "volatile 은 컴파일러에게 하는 말, barrier 는 CPU 에게 하는 말."
//
// sig_atomic_t 는 "인터럽트에 의해 중간이 찢기지 않는 정수형"으로 표준이 보장하는
// 유일한 타입이라 전통적으로 이 깃발에 쓴다 (실무에서는 volatile uint8_t 도 흔하다).
// ===========================================================================
static volatile sig_atomic_t g_evt_flag   = 0;   // ISR 이 set, main 이 clear
static volatile uint16_t     g_evt_sample = 0;   // payload (깃발이 보호한다)
static volatile uint32_t     g_evt_missed = 0;   // 소비 전에 덮어쓴 이벤트 수

/* ---------------------------------------------------------------------------
 * Q1.  ISR 쪽: 이벤트 발생 / main 쪽: 이벤트 소비
 *   KO: isr_event(sample) 는 payload 를 먼저 쓰고 배리어 후 flag=1. 이미 flag 가 서
 *       있으면(=main 이 아직 안 가져감) missed++ 후 최신값으로 덮어쓴다(latest-wins).
 *       main_take(out) 는 flag 가 없으면 false, 있으면 payload 를 복사하고 flag=0.
 *   EN: ISR sets the payload then raises the flag (never the other way round);
 *       main consumes the payload and clears the flag. A second event arriving
 *       before consumption bumps `missed` (latest-wins overwrite policy).
 *   ex: isr_event(0x111); isr_event(0x222); -> missed==1, main_take -> 0x222
 *   note: 이 파일에서 "인터럽트"는 main 이 isr_event() 를 직접 호출해 시뮬레이션한다.
 *   hint: 순서가 전부다 — payload -> atomic_signal_fence(memory_order_release) -> flag.
 *         소비 쪽은 flag 확인 -> acquire fence -> payload 복사 -> release fence -> flag=0.
 * ------------------------------------------------------------------------- */
void isr_event(uint16_t sample) {
    (void)sample; (void)g_evt_sample; (void)g_evt_flag; (void)g_evt_missed;
    // TODO: implement (flag 가 이미 서 있으면 missed++, payload 먼저, 배리어, flag=1)
}

bool main_take(uint16_t *out) {
    (void)out; (void)g_evt_flag; (void)g_evt_sample;
    // TODO: implement (flag 없으면 false, 있으면 payload 복사 후 flag=0 하고 true)
    return false;  // placeholder
}

uint32_t evt_missed(void) {
    (void)g_evt_missed;
    // TODO: implement
    return 0;  // placeholder
}

// ===========================================================================
// Q2 : 틱 wraparound-safe 시간 비교
// ---------------------------------------------------------------------------
// 1kHz ms 틱을 uint32_t 에 담으면 2^32 ms ≈ 49.7일에 0 으로 랩한다. 24/7 로 도는
// 기체·지상국에서 `if (now > deadline)` 은 랩 순간 조용히 뒤집힌다.
//
// 리눅스 커널의 `time_after()` 관용구:
//     (int32_t)(b - a) < 0   <=>   a 가 b 보다 나중
// 부호없는 뺄셈은 랩을 자동으로 처리(모듈로 2^32)하고, 그 차이를 **부호있는** 값으로
// 해석하면 "2^31 ms(≈24.8일) 이내의 시간차"에 대해 항상 올바른 대소 비교가 된다.
// 전제: 비교하는 두 시각의 간격이 2^31 미만이어야 한다 (실무에서는 항상 성립).
// ===========================================================================

/* ---------------------------------------------------------------------------
 * Q2.  wrap-safe 시간 비교 / 타임아웃 / 경과시간
 *   KO: time_after(a,b) 는 a 가 b 보다 나중이면 true (같으면 false).
 *       timeout_expired(now,start,timeout) 는 (now-start) >= timeout 이면 true.
 *       elapsed(now,start) 는 부호없는 뺄셈 결과 = 랩을 건너도 정확한 경과 ms.
 *   EN: Linux-style wrap-safe tick comparison. Never use `now > deadline`:
 *       unsigned subtraction handles the 0xFFFFFFFF -> 0 rollover correctly.
 *   ex: time_after(5, 0xFFFFFFF0) == true (랩을 건너 5 가 나중),
 *       naive 5 > 0xFFFFFFF0 은 false -> 타임아웃이 49.7일마다 안 터진다
 *   hint: 세 함수 모두 한 줄이다. deadline 변수를 만들지 말고 **차이**로만 판단하라.
 * ------------------------------------------------------------------------- */
bool time_after(uint32_t a, uint32_t b) {
    (void)a; (void)b;
    // TODO: implement ((int32_t)(b - a) < 0)
    return false;  // placeholder
}

bool timeout_expired(uint32_t now, uint32_t start, uint32_t timeout_ms) {
    (void)now; (void)start; (void)timeout_ms;
    // TODO: implement ((now - start) >= timeout_ms, 부호없는 뺄셈)
    return false;  // placeholder
}

uint32_t elapsed(uint32_t now, uint32_t start) {
    (void)now; (void)start;
    // TODO: implement (now - start)
    return 0;  // placeholder
}

// ===========================================================================
// ISR 시뮬레이션 훅 (테스트 지원 — 건드리지 말 것)
// ---------------------------------------------------------------------------
// Q3 에서 "읽는 도중에 인터럽트가 들어왔다"를 호스트에서 재현하기 위한 장치.
// tick_read() 가 hi 를 읽은 직후 이 훅을 호출하고, 훅은 한 번만 발사된다.
// 실제 타깃에는 없는 코드다 (테스트 전용 seam).
// ===========================================================================
static void (*g_isr_probe)(void) = NULL;

void tick_set_probe(void (*fn)(void)) {
    g_isr_probe = fn;
}

void isr_probe_fire(void) {
    void (*p)(void) = g_isr_probe;
    if (p) {
        g_isr_probe = NULL;   // one-shot: 재시도 루프가 무한히 돌지 않도록
        p();
    }
}

// ===========================================================================
// Q3 : 32비트 MCU 에서 64비트 틱을 원자적으로 읽기 (seqlock 스타일 재시도)
// ---------------------------------------------------------------------------
// 32비트 코어는 64비트를 한 번에 load 하지 못한다 -> hi/lo 두 번 읽는 사이에
// 인터럽트가 들어오면 "찢어진(torn)" 값이 나온다.
//   lo = 0xFFFFFFFF, hi = 0 인 상태에서 hi 를 먼저 읽고(0) 그 사이 캐리가 일어나면
//   lo 는 0 -> 결과 0x0000_0000_0000_0000. 42억 틱이 통째로 사라진다.
//
// 해결: hi -> lo -> hi 를 읽고 두 hi 가 같을 때만 채택, 다르면 재시도.
// (리눅스 seqlock 과 같은 아이디어: 읽는 쪽은 락을 잡지 않고 "변했으면 다시 읽는다".)
// 대안은 읽는 구간만 인터럽트를 마스킹하는 것 — 확실하지만 지터를 만든다.
// ===========================================================================
static volatile uint32_t g_tick_hi = 0;   // 상위 32비트 (캐리될 때만 갱신)
static volatile uint32_t g_tick_lo = 0;   // 하위 32비트 (매 틱 갱신)

/* ---------------------------------------------------------------------------
 * Q3.  ISR 의 64비트 틱 증가 / main 의 일관된 스냅샷 읽기
 *   KO: isr_tick() 은 lo 를 1 증가시키고 0 으로 랩했을 때만 hi 를 1 증가시킨다.
 *       tick_read() 는 hi, lo, hi 를 읽어 두 hi 가 같을 때만 값을 반환하고,
 *       다르면(=읽는 도중 캐리) 처음부터 다시 읽는다.
 *   EN: 64-bit tick maintained by an ISR on a 32-bit core; the reader takes a
 *       consistent snapshot with a hi/lo/hi double-read retry (seqlock idiom).
 *   ex: hi=0, lo=0xFFFFFFFF 에서 읽는 도중 캐리 -> 0xFFFFFFFF 도 0 도 아닌
 *       0x1_0000_0000 (일관된 값) 이 나와야 한다
 *   hint: tick_read 안에서 hi 를 읽은 **직후** isr_probe_fire() 를 호출해야
 *         테스트가 "읽는 도중 인터럽트"를 주입할 수 있다. 루프는 for(;;) + 재시도.
 * ------------------------------------------------------------------------- */
void isr_tick(void) {
    (void)g_tick_lo; (void)g_tick_hi;
    // TODO: implement (lo++, lo 가 0 으로 랩했을 때만 hi++)
}

uint64_t tick_read(void) {
    (void)g_tick_hi; (void)g_tick_lo;
    // TODO: implement (hi1 -> isr_probe_fire() -> lo -> hi2, hi1==hi2 일 때만 반환)
    return 0;  // placeholder
}

void tick_set(uint32_t hi, uint32_t lo) {   // 테스트 지원: 상태 직접 주입
    g_tick_hi = hi;
    g_tick_lo = lo;
}

// ===========================================================================
// Q4 : GPIO 디바운스 FSM (N회 연속 안정 샘플)
// ---------------------------------------------------------------------------
// 기계식 스위치는 누를 때 수 ms 동안 수십 번 튄다(bounce). 원시 레벨을 그대로 쓰면
// 한 번 누른 게 이벤트 여러 개가 된다. 정석은 **주기 샘플링 + N회 연속 일치**:
//   - 인터럽트(EXTI)로 매 에지마다 깨우는 대신 타이머 틱에서 폴링 -> ISR 폭주 방지
//   - N * 샘플주기 = 디바운스 시간 (예: 1ms 샘플 x 5 = 5ms)
//   - 에지 이벤트는 "확정 레벨이 바뀐 순간"에만 한 번 발생시킨다
// ===========================================================================
typedef struct {
    bool    stable;      // 확정된(디바운스된) 레벨
    bool    candidate;   // 현재 관찰 중인 레벨
    uint8_t count;       // candidate 가 연속으로 관찰된 횟수
    uint8_t threshold;   // N: 이만큼 연속이면 확정
    bool    edge;        // 아직 소비되지 않은 rising edge 이벤트
} debounce_t;

/* ---------------------------------------------------------------------------
 * Q4.  디바운스 업데이트 / rising-edge 이벤트 소비
 *   KO: debounce_update 는 raw_level 이 candidate 와 같으면 count++, 다르면
 *       candidate 를 갈아끼우고 count=1. candidate 가 stable 과 다르면서 count 가
 *       threshold 이상이면 stable 을 갱신하고, false->true 전이면 edge 를 세운다.
 *       항상 **확정 레벨(stable)** 을 반환한다.
 *       debounce_pressed_edge 는 edge 를 반환하고 소비(clear)한다 — 한 번만 true.
 *   EN: Level is committed only after N consecutive identical samples; a rising
 *       transition latches a one-shot press event.
 *   ex: N=3, 1,0,1,1,0,1,1,1 -> 마지막 샘플에서야 true 로 확정
 *   hint: count 는 포화(saturating) 시켜라 — uint8_t 가 255 에서 0 으로 돌면
 *         멀쩡히 눌린 버튼이 갑자기 풀린다. threshold 0 은 1 로 보정.
 * ------------------------------------------------------------------------- */
void debounce_init(debounce_t *d, uint8_t threshold, bool initial) {
    (void)d; (void)threshold; (void)initial;
    // TODO: implement (stable=candidate=initial, count=0, threshold 보정, edge=false)
}

bool debounce_update(debounce_t *d, bool raw_level) {
    (void)d; (void)raw_level;
    // TODO: implement (같으면 count++ 포화, 다르면 candidate 교체 count=1,
    //                  candidate != stable && count >= threshold 이면 확정 + edge)
    return false;  // placeholder
}

bool debounce_pressed_edge(debounce_t *d) {
    (void)d;
    // TODO: implement (edge 를 반환하고 clear — consume-on-read)
    return false;  // placeholder
}

// ===========================================================================
// Q5 : 협력형(cooperative) 주기 스케줄러 틱
// ---------------------------------------------------------------------------
// RTOS 없이 main loop 하나로 여러 주기 태스크를 돌리는 가장 흔한 구조.
//   while (1) { sched_run(&s, millis()); }
//
// 재스케줄 규칙이 전부다:
//   - next_due += period      (X) next_due = now + period
//     전자는 **고정 케이던스**(드리프트 0). 후자는 실행 지연이 매 주기 누적돼
//     100Hz 가 97Hz 로 서서히 밀린다.
//   - 시스템이 오래 멈췄다 돌아왔다면(플래시 쓰기, 디버거 정지, 긴 ISR) 밀린 주기가
//     수십 개 쌓인다. 이걸 다 실행하면 한 틱에 폭주(burst)해서 루프가 또 밀린다.
//     -> **1회만 실행하고 나머지는 건너뛴다(skip backlog)** + 건너뛴 수를 센다.
//     (반대 정책 = catch-up: 적산이 중요한 작업(적분/카운팅)에서만 쓴다.)
// ===========================================================================
#define SCHED_MAX_TASKS 4

typedef void (*task_fn_t)(void *arg);

typedef struct {
    uint32_t  period_ms;     // 주기 (0 = 비활성 슬롯)
    uint32_t  next_due_ms;   // 다음 실행 예정 시각 (절대 틱)
    uint32_t  ran_count;     // 실제 실행 횟수
    uint32_t  missed_count;  // 스톨로 건너뛴 주기 수
    task_fn_t fn;
    void     *arg;
} task_t;

typedef struct {
    task_t tasks[SCHED_MAX_TASKS];
    size_t n;
} sched_t;

/* ---------------------------------------------------------------------------
 * Q5.  태스크 등록 / 틱마다 호출되는 스케줄러 본체
 *   KO: sched_run 은 next_due 가 도래한(= !time_after(next_due, now)) 태스크를
 *       실행하고 next_due += period 로 재스케줄한다. 그래도 여전히 밀려 있으면
 *       (= 오래 스톨했다) 밀린 주기를 missed_count 에 더하고 next_due 를 now 이후로
 *       한 번에 점프시킨다 — 몰아서 여러 번 실행하지 않는다.
 *   EN: Run every task whose deadline has arrived, reschedule on a fixed cadence
 *       (next_due += period, drift-free), and on a long stall skip the backlog
 *       instead of bursting N runs; count what was skipped.
 *   ex: period=10, now=0 에서 1회 실행 후 now=205 로 점프 -> 실행은 1회 더,
 *       missed=19, next_due=210 (20회 몰아치기 금지)
 *   note: 시간 비교는 전부 Q2 의 time_after 를 쓴다 -> 틱 랩에도 안전하다.
 *   hint: skip = (now - next_due) / period + 1  이면 next_due 가 반드시 now 를 넘고
 *         period 격자(위상)도 유지된다. `next_due = now + period` 로 바꾸면 위상이 깨진다.
 * ------------------------------------------------------------------------- */
void sched_init(sched_t *s) {
    (void)s;
    // TODO: implement (구조체 전체 0 으로)
}

bool sched_add(sched_t *s, uint32_t period_ms, uint32_t first_due_ms,
               task_fn_t fn, void *arg) {
    (void)s; (void)period_ms; (void)first_due_ms; (void)fn; (void)arg;
    // TODO: implement (period 0 / 슬롯 초과면 false, 아니면 슬롯 채우고 true)
    return false;  // placeholder
}

void sched_run(sched_t *s, uint32_t now_ms) {
    (void)s; (void)now_ms;
    // TODO: implement (도래한 태스크 실행 -> next_due += period ->
    //                  아직도 밀렸으면 skip 만큼 점프 + missed_count 누적)
}

// ===========================================================================
// Q6 : deferred work (bottom half) — ISR 은 post 만, 처리는 main loop 에서
// ---------------------------------------------------------------------------
// 리눅스의 top half / bottom half, RTOS 의 "ISR -> 큐 -> 태스크"와 같은 구조.
//   top half   (ISR)      : 하드웨어 응답 + 이벤트 id 를 큐에 push. 수십 사이클.
//   bottom half (main)    : 큐를 비우며 핸들러 테이블로 디스패치. 느려도 된다.
// ISR 에서 절대 하면 안 되는 것: 블로킹, malloc, printf, mutex 대기.
//   -> 큐가 꽉 차면 **기다리지 않고** lost++ 하고 즉시 리턴한다.
//
// 인덱스는 free-running(마스킹 안 함) + 접근할 때만 & mask. head 는 생산자(ISR)만,
// tail 은 소비자(main)만 갱신하므로 단일코어에서는 락이 필요 없다 (SPSC).
// ===========================================================================
#define WORK_Q_SIZE    4u              // 반드시 2의 거듭제곱
#define WORK_Q_MASK    (WORK_Q_SIZE - 1u)
#define WORK_MAX_EVENT 8u              // 핸들러 테이블 크기

typedef void (*work_handler_t)(uint8_t event_id);

static uint8_t           g_workq[WORK_Q_SIZE];
static volatile uint32_t g_wq_head = 0;    // ISR 만 store
static volatile uint32_t g_wq_tail = 0;    // main 만 store
static volatile uint32_t g_wq_lost = 0;    // 큐가 꽉 차서 버린 이벤트 수
static work_handler_t    g_handlers[WORK_MAX_EVENT];

/* ---------------------------------------------------------------------------
 * Q6.  ISR 측 post / main 측 처리 / 핸들러 등록
 *   KO: work_post 는 "ISR 에서" 호출된다 — 큐가 꽉 차면 블로킹하지 않고 lost++ 후
 *       false. 자리가 있으면 데이터를 먼저 쓰고 배리어 후 head 를 전진시킨다.
 *       work_process 는 head 를 **한 번만 스냅샷**해 그 시점까지만 FIFO 로 비우고
 *       (처리 중 새로 들어온 건 다음 패스로), 처리한 개수를 반환한다.
 *   EN: ISR-side post into a small fixed queue (overflow -> lost++, never block);
 *       main-loop drain dispatches FIFO through a handler table.
 *   ex: 4칸 큐에 5개 post -> 4개 성공 + lost==1, process() -> 4 (1,2,3,4 순서)
 *   hint: full 판정은 (head - tail) >= WORK_Q_SIZE (부호없는 뺄셈이라 랩 안전).
 *         head 를 while 조건에서 매번 읽으면 처리 중 들어온 이벤트까지 끌려와
 *         최악의 경우 main loop 가 영원히 이 함수에서 못 나온다 -> 스냅샷 필수.
 * ------------------------------------------------------------------------- */
void work_init(void) {
    (void)g_wq_head; (void)g_wq_tail; (void)g_wq_lost;
    (void)g_workq; (void)g_handlers;
    // TODO: implement (head/tail/lost 0, 큐와 핸들러 테이블 초기화)
}

bool work_register(uint8_t event_id, work_handler_t h) {
    (void)event_id; (void)h; (void)g_handlers;
    // TODO: implement (event_id 범위 검사 후 테이블에 등록)
    return false;  // placeholder
}

bool work_post(uint8_t event_id) {          // "ISR 컨텍스트"에서 호출
    (void)event_id; (void)g_workq; (void)g_wq_head; (void)g_wq_tail; (void)g_wq_lost;
    // TODO: implement (full 이면 lost++ 후 false, 아니면 데이터 먼저 -> 배리어 -> head++)
    return false;  // placeholder
}

size_t work_process(void) {                 // main loop 에서만 호출
    (void)g_wq_head; (void)g_wq_tail; (void)g_workq; (void)g_handlers;
    // TODO: implement (head 스냅샷 -> tail 이 따라잡을 때까지 FIFO 디스패치, 개수 반환)
    return 0;  // placeholder
}

uint32_t work_lost(void) {
    (void)g_wq_lost;
    // TODO: implement
    return 0;  // placeholder
}

// ===========================================================================
// 테스트 지원 (ISR 시뮬레이션 / 태스크 / 핸들러 — 건드리지 말 것)
// ---------------------------------------------------------------------------
// 하드웨어가 없으므로 "인터럽트"는 main 에서 isr_event()/isr_tick() 을 직접 호출해
// 흉내낸다. probe_tick 은 tick_read() 가 hi 를 읽은 직후에 끼어드는 인터럽트다.
// ===========================================================================
static void probe_tick(void) { isr_tick(); }

static void task_bump(void *arg) { (*(uint32_t *)arg)++; }

static uint8_t g_work_log[16];
static size_t  g_work_log_n = 0;

static void work_record(uint8_t event_id) {
    if (g_work_log_n < sizeof g_work_log) g_work_log[g_work_log_n++] = event_id;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL (건드리지 말 것)
// ===========================================================================
int main(void) {
    // -------- Q1 : ISR -> main 플래그 핸드셰이크 --------
    printf("== Q1. ISR->main flag handshake (flag + payload + missed) ==\n");
    uint16_t v16 = 0;
    T("Q1 이벤트 없음 -> main_take false", main_take(&v16) == false);

    isr_event(0x1234);                     // <- "인터럽트 발생" (main 에서 직접 호출)
    bool h1_ok = main_take(&v16) && (v16 == 0x1234);
    h1_ok &= (main_take(&v16) == false);   // 깃발이 내려갔으므로 두 번은 못 가져간다
    h1_ok &= (evt_missed() == 0);
    T("Q1 payload 전달 + 소비 후 flag clear (재소비 불가)", h1_ok);

    uint32_t m0 = evt_missed();
    isr_event(0x0111);                     // main 이 가져가기 전에
    isr_event(0x0222);                     // 두 번째 인터럽트가 덮어쓴다
    bool h2_ok = (evt_missed() == m0 + 1); // 유실을 조용히 넘기지 않고 센다
    h2_ok &= main_take(&v16) && (v16 == 0x0222);   // latest-wins
    T("Q1 소비 전 재발생 -> missed 1 증가 + 최신값 유지", h2_ok);

    // -------- Q2 : 틱 wraparound-safe 비교 --------
    printf("== Q2. wrap-safe tick comparison (Linux time_after idiom) ==\n");
    bool ta_ok = time_after(1000u, 999u)
              && !time_after(999u, 1000u)
              && !time_after(1000u, 1000u);        // 같으면 "나중"이 아니다
    T("Q2 일반 구간: time_after(a,b) 대소/동등 정확", ta_ok);

    const uint32_t near_max = 0xFFFFFFF0u;         // 랩까지 16ms 남음
    const uint32_t after_wrap = 5u;                // 랩을 건넌 21ms 뒤
    bool wrap_ok = time_after(after_wrap, near_max);          // 올바른 답: true
    wrap_ok &= ((after_wrap > near_max) == false);            // naive 비교는 false = 틀림
    wrap_ok &= (elapsed(after_wrap, near_max) == 21u);        // 0xF0..0xFF(16) + 0..5(5)
    T("Q2 랩 경계: time_after 는 맞고 naive (a > b) 는 틀린다", wrap_ok);

    const uint32_t start = 0xFFFFFF00u;
    bool to_ok = !timeout_expired(0xFFFFFF63u, start, 100u);  // 99ms 경과 -> 아직
    to_ok &= timeout_expired(0xFFFFFF64u, start, 100u);       // 100ms 경과 -> 만료
    to_ok &= timeout_expired(0x00000050u, start, 100u);       // 336ms 경과 (랩 통과)
    to_ok &= ((0x00000050u > (uint32_t)(start + 100u)) == false);  // naive deadline 은 실패
    T("Q2 timeout_expired 가 0xFFFFFFFF 경계를 넘어서도 동작", to_ok);

    // -------- Q3 : 64비트 틱 원자적 스냅샷 --------
    printf("== Q3. 64-bit tick snapshot on a 32-bit core (double-read retry) ==\n");
    tick_set_probe(NULL);
    tick_set(0u, 0u);
    isr_tick(); isr_tick(); isr_tick();
    bool tk_ok = (tick_read() == 3u);
    tick_set(0u, 0xFFFFFFFFu);
    isr_tick();                                    // lo 랩 -> hi 캐리
    tk_ok &= (tick_read() == UINT64_C(0x100000000));
    T("Q3 isr_tick 증가 + lo 랩 시 hi 캐리", tk_ok);

    tick_set(0u, 0xFFFFFFFFu);
    tick_set_probe(probe_tick);                    // hi 를 읽은 직후 캐리가 일어난다
    uint64_t torn = tick_read();
    // 재시도가 없었다면 hi=0(구값) + lo=0(신값) = 0 이 나온다 (42억 틱 소실)
    T("Q3 읽는 도중 캐리 -> 재시도로 일관된 0x1_00000000 (torn 0 아님)",
      torn == UINT64_C(0x100000000));

    tick_set(2u, 5u);
    tick_set_probe(probe_tick);                    // 캐리 없는 평범한 인터럽트
    uint64_t t3 = tick_read();
    T("Q3 캐리 없는 인터럽트는 재시도 없이 통과 (hi 불변)", t3 == UINT64_C(0x200000006));

    // -------- Q4 : GPIO 디바운스 FSM --------
    printf("== Q4. GPIO debounce FSM (N consecutive stable samples) ==\n");
    debounce_t db;
    debounce_init(&db, 3u, false);                 // N=3, 초기 레벨 LOW
    const bool press_seq[8] = {1, 0, 1, 1, 0, 1, 1, 1};   // 튀는 입력
    bool db_ok = true;
    for (int i = 0; i < 7; ++i)
        if (debounce_update(&db, press_seq[i]) != false) db_ok = false;  // 아직 확정 전
    db_ok &= (debounce_update(&db, press_seq[7]) == true);               // 3연속 달성
    T("Q4 바운스 중에는 LOW 유지, 3회 연속 HIGH 에서만 확정", db_ok);

    bool edge_ok = debounce_pressed_edge(&db);         // 눌림 이벤트 1회
    edge_ok &= (debounce_pressed_edge(&db) == false);  // 소비형 -> 두 번은 안 뜬다
    T("Q4 rising edge 이벤트는 정확히 한 번만 소비된다", edge_ok);

    const bool rel_seq[5] = {0, 1, 0, 0, 0};           // 떼면서 다시 튄다
    bool rel_ok = true;
    for (int i = 0; i < 4; ++i)
        if (debounce_update(&db, rel_seq[i]) != true) rel_ok = false;    // 아직 HIGH
    rel_ok &= (debounce_update(&db, rel_seq[4]) == false);               // 3연속 LOW
    rel_ok &= (debounce_pressed_edge(&db) == false);   // 하강 에지는 press 가 아니다
    T("Q4 해제도 3회 연속 필요 + 하강 에지는 press 이벤트가 아님", rel_ok);

    // -------- Q5 : 협력형 주기 스케줄러 --------
    printf("== Q5. cooperative scheduler tick (drift-free + skip backlog) ==\n");
    uint32_t cnt_a = 0, cnt_b = 0;
    sched_t sch;
    sched_init(&sch);
    bool add_ok = sched_add(&sch, 10u, 0u, task_bump, &cnt_a)    // 100Hz
               && sched_add(&sch, 25u, 0u, task_bump, &cnt_b);   // 40Hz
    sched_run(&sch, 0u);
    sched_run(&sch, 5u);     // 아직 둘 다 아님
    sched_run(&sch, 13u);    // A 만 (3ms 늦게 호출됨)
    sched_run(&sch, 26u);    // A, B 둘 다
    bool run_ok = add_ok && (cnt_a == 3u) && (cnt_b == 2u);
    // 드리프트 0: 13ms 에 실행됐어도 다음 예정은 23 이 아니라 20, 그 다음은 30
    run_ok &= (sch.tasks[0].next_due_ms == 30u) && (sch.tasks[1].next_due_ms == 50u);
    run_ok &= (sch.tasks[0].missed_count == 0u);
    T("Q5 고정 케이던스 재스케줄: 늦게 호출돼도 next_due 는 period 격자에 고정", run_ok);

    sched_t st;
    uint32_t cnt_s = 0;
    sched_init(&st);
    sched_add(&st, 10u, 0u, task_bump, &cnt_s);
    sched_run(&st, 0u);                 // 1회 실행, next_due=10
    sched_run(&st, 205u);               // 195ms 스톨 후 복귀
    bool stall_ok = (cnt_s == 2u);                       // 20회 몰아치지 않았다
    stall_ok &= (st.tasks[0].missed_count == 19u);       // 건너뛴 주기를 센다
    stall_ok &= (st.tasks[0].next_due_ms == 210u);       // now 이후로 점프 + 위상 유지
    sched_run(&st, 205u);                                // 같은 시각 재호출은 무동작
    stall_ok &= (cnt_s == 2u);
    T("Q5 긴 스톨 후 backlog 는 burst 대신 skip + missed 로 계수", stall_ok);

    // -------- Q6 : deferred work (bottom half) --------
    printf("== Q6. deferred work / bottom half (post from ISR, run in main) ==\n");
    work_init();
    g_work_log_n = 0;
    for (uint8_t id = 1; id <= 4; ++id) work_register(id, work_record);

    T("Q6 빈 큐 처리 -> 0 (main loop 가 헛돌지 않는다)", work_process() == 0);

    bool fifo_ok = work_post(1) && work_post(2) && work_post(3);   // "ISR 에서" 3건
    fifo_ok &= (work_process() == 3u);
    fifo_ok &= (g_work_log_n == 3u)
            && (g_work_log[0] == 1) && (g_work_log[1] == 2) && (g_work_log[2] == 3);
    T("Q6 ISR post -> main 에서 FIFO 순서대로 디스패치", fifo_ok);

    g_work_log_n = 0;
    bool ovf_ok = work_post(1) && work_post(2) && work_post(3) && work_post(4);
    ovf_ok &= (work_post(5) == false);          // 4칸 큐가 꽉 찼다 -> 블로킹 금지
    ovf_ok &= (work_lost() == 1u);              // 대신 센다
    ovf_ok &= (work_process() == 4u);
    ovf_ok &= (g_work_log_n == 4u) && (g_work_log[0] == 1) && (g_work_log[3] == 4);
    // 핸들러가 없는 이벤트도 큐에서 빠지고 계수된다 (크래시 없음)
    ovf_ok &= work_post(7) && (work_process() == 1u) && (g_work_log_n == 4u);
    T("Q6 오버플로는 lost++ 로 기록, 미등록 이벤트도 안전하게 소비", ovf_ok);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
