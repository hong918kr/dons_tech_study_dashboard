/* C-M14 인터럽트 안전 코드: 공유 변수와 원자성
 *
 * 호스트 모델: SIGALRM 핸들러 = ISR, main 흐름 = 스레드 모드(thread mode).
 * 32비트 MCU 를 흉내 내려고 64비트 µs 타임스탬프를 uint32_t 두 칸(hi/lo)에 나눠 둔다.
 * (arm64 맥에서는 uint64_t 읽기가 ldr 한 번이라 찢어진 읽기가 자연히 생기지 않기 때문.)
 *
 * main 쪽의 공유 변수 읽기는 모두 rd32() 로 한다. rd32 는 "n번째 읽기 직후" 에 raise(SIGALRM) 으로
 * 인터럽트를 끼워 넣을 수 있다 → 경쟁(race)을 매번 똑같이 재현하는 결정적(deterministic) 테스트.
 * 실제 칩에서 rd32(p) 는 그냥 *p 한 줄이다.
 * raise() 로 생긴 시그널은 C11 7.14.1.1p5 의 비동기 시그널 제약 밖이라 이 테스트는 정의된 동작이다.
 */
#define _POSIX_C_SOURCE 200809L   /* -std=c11 에서 sigaction/sigprocmask 선언을 보이게 (include 전에) */
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TICK_US 1000u                 /* 1 ms 타이머 tick = 1000 µs */

/* ---------------- ISR 과 main 이 공유하는 데이터 ---------------- */
static volatile uint32_t g_ts_hi, g_ts_lo;    /* 64비트 µs 타임스탬프 = hi:lo (ISR 이 갱신)       */
typedef struct { uint32_t seq, ch0, ch1, ch2; } sample_t;   /* ADC 3채널 샘플                 */
static volatile sample_t g_sample;            /* 불변식: chK == seq*100 + K  (ISR 이 갱신)        */
static volatile uint32_t g_sample_ver;        /* seqlock 버전: ISR 이 쓰기 전후로 +1 (홀수=쓰는 중) */
static volatile sig_atomic_t g_tick_flag;     /* Part A: ISR 은 1 을 "대입" 만 한다               */
static atomic_uint g_tick_count;              /* Part A: lock-free 원자 카운터 (ISR 이 +1)        */
static uint64_t g_main_ts;                    /* Part A: main 만 소유하는 64비트 시간 (공유 안 함) */

/* ---------------- 시뮬레이터 (주어진 코드 — 고치지 않는다) ---------------- */
static char g_trace[256];                     /* 예: "rH IRQ rL"                         */
static int  g_nacc;                           /* 지금까지 main 이 한 공유 변수 읽기 수     */
static int  g_fire_at = -1;                   /* 이 번호의 읽기 직후 SIGALRM (-1 = 없음)  */
static volatile uint32_t g_isr_runs;          /* ISR 이 실제로 실행된 횟수                */

static void trace(const char *s) {            /* 핸들러에서도 부르므로 라이브러리 함수 없이 */
    size_t n = 0;
    while (g_trace[n]) n++;
    if (n && n + 1 < sizeof g_trace) g_trace[n++] = ' ';
    while (*s && n + 1 < sizeof g_trace) g_trace[n++] = *s++;
    g_trace[n] = '\0';
}
/* 타이머 ISR (= SIGALRM 핸들러). 실제 MCU 에서는 SysTick_Handler / TIMx_IRQHandler. */
static void timer_isr(int sig) {
    (void)sig;
    trace("IRQ");
    g_isr_runs = g_isr_runs + 1u;
    /* ① 타임스탬프 += 1 ms : 32비트 CPU 처럼 lo, hi 를 두 번에 나눠 쓴다 */
    uint32_t lo = g_ts_lo + TICK_US;
    g_ts_lo = lo;
    if (lo < TICK_US) g_ts_hi = g_ts_hi + 1u;          /* 자리올림(carry) */
    /* ② 새 ADC 샘플 — seqlock 버전으로 감싸서 쓴다 */
    uint32_t s = g_sample.seq + 1u;
    g_sample_ver = g_sample_ver + 1u;                  /* 홀수: 쓰는 중 */
    g_sample.seq = s;
    g_sample.ch0 = s * 100u + 0u;
    g_sample.ch1 = s * 100u + 1u;
    g_sample.ch2 = s * 100u + 2u;
    g_sample_ver = g_sample_ver + 1u;                  /* 짝수: 다 썼음 */
    /* ③ main 에 알림 */
    g_tick_flag = 1;
    atomic_fetch_add_explicit(&g_tick_count, 1u, memory_order_relaxed);
}
static const char *name_of(const volatile uint32_t *p) {
    return p == &g_ts_hi ? "rH" : p == &g_ts_lo ? "rL" : p == &g_sample_ver ? "rV"
         : p == &g_sample.seq ? "rS" : p == &g_sample.ch0 ? "r0" : p == &g_sample.ch1 ? "r1"
         : p == &g_sample.ch2 ? "r2" : "r?";
}
static void after_access(void) {
    if (++g_nacc == g_fire_at) raise(SIGALRM);  /* 막혀(blocked) 있으면 펜딩 → 풀리는 순간 실행 */
}
static uint32_t rd32(const volatile uint32_t *p) {   /* 타깃에서는: return *p; */
    uint32_t v = *p;
    trace(name_of(p));
    after_access();
    return v;
}
static void sim_reset(uint32_t hi, uint32_t lo, int fire_at) {
    sigset_t s;                                 /* 이전 테스트가 SIGALRM 을 막아 둔 채면 풀어 둔다 */
    sigemptyset(&s);
    sigaddset(&s, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &s, NULL);
    g_ts_hi = hi; g_ts_lo = lo;
    g_sample.seq = 7u; g_sample.ch0 = 700u; g_sample.ch1 = 701u; g_sample.ch2 = 702u;
    g_sample_ver = 0u;
    g_tick_flag = 0;
    atomic_store(&g_tick_count, 0u);
    g_main_ts = 0u;
    g_trace[0] = '\0'; g_nacc = 0; g_fire_at = fire_at; g_isr_runs = 0u;
}
static bool sample_ok(const sample_t *s) {
    return s->ch0 == s->seq * 100u && s->ch1 == s->seq * 100u + 1u && s->ch2 == s->seq * 100u + 2u;
}
static bool sigalrm_blocked(void) {
    sigset_t cur;
    sigprocmask(SIG_BLOCK, NULL, &cur);         /* set == NULL → 현재 마스크 조회만 */
    return sigismember(&cur, SIGALRM) == 1;
}

/* ---------------- (주어진 코드 — 버그 재현용) ---------------- */
static uint64_t ts_read_naive(void) {           /* hi → lo. 사이에 ISR 이 오면 찢어진다 */
    uint32_t hi = rd32(&g_ts_hi);
    uint32_t lo = rd32(&g_ts_lo);
    return ((uint64_t)hi << 32) | lo;
}
static void sample_read_naive(sample_t *out) {  /* 필드 4개를 하나씩 — 중간에 ISR 이 오면 섞인다 */
    out->seq = rd32(&g_sample.seq);
    out->ch0 = rd32(&g_sample.ch0);
    out->ch1 = rd32(&g_sample.ch1);
    out->ch2 = rd32(&g_sample.ch2);
}
static unsigned main_take_count_naive(void) {   /* "읽고 → 0 쓰기" 두 단계: lost update */
    unsigned n = atomic_load_explicit(&g_tick_count, memory_order_relaxed);
    trace("rC");
    after_access();                             /* ← 여기서 ISR 이 끼어들 수 있다 */
    atomic_store_explicit(&g_tick_count, 0u, memory_order_relaxed);
    return n;
}

/* ---------------- Part A: ISR 은 신호만, 64비트 값은 main 이 소유 ---------------- */
/* g_tick_flag 가 서 있으면 내리고 g_main_ts += TICK_US 후 1 반환. 아니면 0 반환. */
static int main_poll_flag(void) {
    return 0; /* TODO */
}
/* 쌓인 tick 수 n 을 "한 번의 원자 연산" 으로 가져가며 0 으로 만든다. g_main_ts += n * TICK_US, n 반환. */
static unsigned main_take_count(void) {
    return 0; /* TODO */
}

/* ---------------- Part B: 임계 구역 (sigprocmask = PRIMASK) ---------------- */
typedef sigset_t irq_state_t;
/* SIGALRM 을 막고 "이전" 마스크를 돌려준다.   Cortex-M: pm = __get_PRIMASK(); __disable_irq(); */
static irq_state_t irq_save(void) {
    irq_state_t old;
    sigemptyset(&old);
    return old; /* TODO */
}
/* 이전 마스크로 "복원" 한다 (무조건 해제가 아니다).   Cortex-M: __set_PRIMASK(pm); */
static void irq_restore(irq_state_t old) {
    (void)old; /* TODO */
}
/* 임계 구역 안에서 hi → lo 를 읽어 찢어지지 않은 64비트 값을 돌려준다. */
static uint64_t ts_read_masked(void) {
    return 0; /* TODO */
}
/* 임계 구역 안에서 샘플 4필드를 복사한다 (읽는 순서: seq, ch0, ch1, ch2). */
static void sample_read_masked(sample_t *out) {
    memset(out, 0, sizeof *out); /* TODO */
}

/* ---------------- Part C: 락 없는 재읽기 (인터럽트를 막지 않는다) ---------------- */
/* hi → lo → hi 를 읽고, 두 hi 가 다르면 처음부터 다시. *tries = 시도 횟수(1 이상). */
static uint64_t ts_read_reread(int *tries) {
    *tries = 0;
    return 0; /* TODO */
}
/* seqlock 읽기: v1 = ver (홀수면 재시도) → seq, ch0, ch1, ch2 → v2 = ver. v1 != v2 면 재시도.
 * *tries = 시도 횟수(1 이상). */
static void sample_read_seq(sample_t *out, int *tries) {
    memset(out, 0, sizeof *out);
    *tries = 0; /* TODO */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define TRACE_IS(s) (strcmp(g_trace, (s)) == 0)

#define LO_NEAR_WRAP 0xFFFFFE0Cu   /* 4294966796 µs. +1000 하면 0x1_000001F4 로 자리올림 */
#define TS_OLD       0x00000000FFFFFE0Cull
#define TS_NEW       0x00000001000001F4ull

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = timer_isr;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);

    /* 재현: 찢어진 읽기 · 섞인 구조체 · lost update (주어진 버그 코드) */
    sim_reset(0u, LO_NEAR_WRAP, 1);              /* hi 를 읽은 직후 ISR (자리올림 발생) */
    CHECK(ts_read_naive() == 500u);              /* 0:0x1F4 — 시간이 2^32 µs(약 71.6분) 뒤로 */
    CHECK(TRACE_IS("rH IRQ rL"));
    sim_reset(0u, LO_NEAR_WRAP, 2);              /* lo 까지 읽은 뒤 ISR → 이번엔 운 좋게 멀쩡 */
    CHECK(ts_read_naive() == TS_OLD);
    sample_t s;
    sim_reset(0u, 0u, 2);                        /* seq, ch0 를 읽은 뒤 새 샘플 도착 */
    sample_read_naive(&s);
    CHECK(s.seq == 7u && s.ch0 == 700u && s.ch1 == 801u && !sample_ok(&s));
    sim_reset(0u, 0u, 1);
    raise(SIGALRM); raise(SIGALRM); raise(SIGALRM);
    CHECK(main_take_count_naive() == 3u);        /* 3 을 가져갔지만 */
    CHECK(g_isr_runs == 4u && atomic_load(&g_tick_count) == 0u);   /* 4번째 tick 은 사라졌다 */

    /* Part A */
    CHECK(ATOMIC_INT_LOCK_FREE == 2 && atomic_is_lock_free(&g_tick_count));
    sim_reset(0u, 0u, -1);
    CHECK(main_poll_flag() == 0 && g_main_ts == 0u);
    raise(SIGALRM);
    CHECK(main_poll_flag() == 1 && g_main_ts == 1000u);
    CHECK(main_poll_flag() == 0 && g_tick_flag == 0);
    raise(SIGALRM); raise(SIGALRM);              /* 폴링 사이에 tick 2개 */
    CHECK(main_poll_flag() == 1 && g_main_ts == 2000u);   /* 플래그는 "1개 이상" 만 안다 (합쳐짐) */
    sim_reset(0u, 0u, -1);
    raise(SIGALRM); raise(SIGALRM); raise(SIGALRM);
    CHECK(main_take_count() == 3u && g_main_ts == 3000u);  /* 카운터는 하나도 안 잃는다 */
    CHECK(main_take_count() == 0u && g_main_ts == 3000u);
    CHECK(atomic_load(&g_tick_count) == 0u);

    /* Part B */
    sim_reset(0u, LO_NEAR_WRAP, 1);
    CHECK(ts_read_masked() == TS_OLD);           /* 임계 구역 안에선 ISR 이 못 끼어든다 */
    CHECK(TRACE_IS("rH rL IRQ"));                /* ISR 은 펜딩됐다가 복원 직후 실행 */
    CHECK(g_isr_runs == 1u && !sigalrm_blocked());
    sim_reset(0u, LO_NEAR_WRAP, -1);
    CHECK(ts_read_masked() == TS_OLD && g_isr_runs == 0u && TRACE_IS("rH rL"));
    sim_reset(0u, 0u, 2);
    sample_read_masked(&s);
    CHECK(sample_ok(&s) && s.seq == 7u);
    CHECK(TRACE_IS("rS r0 r1 r2 IRQ") && g_isr_runs == 1u);
    sim_reset(0u, 0u, -1);                       /* 중첩(nesting): 안쪽 restore 는 풀면 안 된다 */
    irq_state_t outer = irq_save();
    CHECK(sigalrm_blocked());
    irq_state_t inner = irq_save();
    raise(SIGALRM);
    irq_restore(inner);
    CHECK(g_isr_runs == 0u && sigalrm_blocked());
    irq_restore(outer);
    CHECK(g_isr_runs == 1u && !sigalrm_blocked());
    sigset_t u1, cur;                            /* 다른 시그널의 마스크 상태는 보존 */
    sigemptyset(&u1);
    sigaddset(&u1, SIGUSR1);
    sigprocmask(SIG_BLOCK, &u1, NULL);
    irq_restore(irq_save());
    sigprocmask(SIG_BLOCK, NULL, &cur);
    CHECK(sigismember(&cur, SIGUSR1) == 1 && sigismember(&cur, SIGALRM) == 0);
    sigprocmask(SIG_UNBLOCK, &u1, NULL);

    /* Part C */
    int tries;
    sim_reset(0u, LO_NEAR_WRAP, 1);              /* hi1 직후 ISR (자리올림) */
    CHECK(ts_read_reread(&tries) == TS_NEW && tries == 2);
    CHECK(TRACE_IS("rH IRQ rL rH rH rL rH"));
    sim_reset(0u, LO_NEAR_WRAP, 2);              /* lo 직후 ISR (자리올림) */
    CHECK(ts_read_reread(&tries) == TS_NEW && tries == 2);
    CHECK(TRACE_IS("rH rL IRQ rH rH rL rH"));
    sim_reset(0u, 5000u, 1);                     /* 자리올림 없는 ISR: hi 그대로 → 한 번에 성공 */
    CHECK(ts_read_reread(&tries) == 6000u && tries == 1 && TRACE_IS("rH IRQ rL rH"));
    sim_reset(3u, 42u, -1);
    CHECK(ts_read_reread(&tries) == ((3ull << 32) | 42u) && tries == 1);
    sim_reset(0u, 0u, 3);                        /* seq, ch0 읽은 뒤 새 샘플 */
    sample_read_seq(&s, &tries);
    CHECK(sample_ok(&s) && s.seq == 8u && tries == 2);
    CHECK(TRACE_IS("rV rS r0 IRQ r1 r2 rV rV rS r0 r1 r2 rV"));
    sim_reset(0u, 0u, -1);
    sample_read_seq(&s, &tries);
    CHECK(sample_ok(&s) && s.seq == 7u && tries == 1 && g_isr_runs == 0u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
