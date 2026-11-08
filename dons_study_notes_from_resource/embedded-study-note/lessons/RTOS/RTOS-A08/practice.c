/* RTOS-A08 추가 연습 — 한 번에 검증
 *
 *   연습 1  task_set_affinity : 실행/대기 중인 태스크의 친화성을 바꿀 때 누구를 yield 시키나 (vTaskCoreAffinitySet)
 *   연습 2  replace_evicted   : 선점당해 쫓겨난 태스크를 다른 코어에 다시 올리기 (prvSelectHighestPriorityTask 끝부분)
 *   연습 3  RP2040 SIO 하드웨어 스핀락 모델 : "읽으면 잡기, 쓰면 풀기" + 인터럽트 저장/복원 (spin_lock_blocking)
 *   연습 4  pthread_kill IPI : 시그널 핸들러 = IPI 핸들러, 펜딩 비트만 세우고 코어 루프가 처리
 *
 * 연습 1·2 는 코어 4개 모델, 단일 스레드로 결정적으로 검사. 연습 3·4 는 진짜 스레드.
 */
#define _DARWIN_C_SOURCE              /* pthread_kill, sigaction, nanosleep (macOS) */
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS  %s\n", #e); } \
                      else printf("FAIL  %s   (line %d)\n", #e, __LINE__); } while (0)

/* ===================== 연습 1·2: 코어 4개 스케줄러 상태 모델 ===================== */
#define NC 4
#define ALL ((1u << NC) - 1u)
enum { T_BLOCKED, T_READY, T_RUNNING };
typedef struct { const char *name; int prio; unsigned affinity; int state, run_core; } task_t;
typedef struct { task_t *cur; bool yield_requested, yield_pending; int ipi_sent; } core_t;
static core_t g_core[NC];

static void reset(void) { memset(g_core, 0, sizeof g_core); }
static int cur_prio(int c) { return g_core[c].cur ? g_core[c].cur->prio : -1; }   /* idle = -1 */
static bool flagged(int c) { return g_core[c].yield_requested || g_core[c].yield_pending; }
static void request_yield(int c, int from) {           /* FreeRTOS prvYieldCore */
    if (c == from) g_core[c].yield_pending = true;
    else { g_core[c].yield_requested = true; g_core[c].ipi_sent++; }
}
static int pick_core_to_preempt(const task_t *t) {     /* 본문 Part A 의 답 그대로 */
    int best = -1, lowest = t->prio - 1;
    for (int c = 0; c < NC; c++) {
        if (!(t->affinity & (1u << c)) || flagged(c)) continue;
        if (cur_prio(c) <= lowest) { lowest = cur_prio(c); best = c; }
    }
    return best;
}
static void run_on(task_t *t, int c) { t->state = T_RUNNING; t->run_core = c; g_core[c].cur = t; }

/* 연습 1 */
static void task_set_affinity(task_t *t, unsigned mask, int from) {
    t->affinity = mask;
    if (t->state == T_RUNNING) {
        if (!(mask & (1u << t->run_core))) request_yield(t->run_core, from);   /* 지금 코어가 금지됨 → 그 코어를 비킨다 */
    } else if (t->state == T_READY) {
        int c = pick_core_to_preempt(t);                                       /* 새 마스크로 다시 자리 찾기 */
        if (c >= 0) request_yield(c, from);
    }
}

/* 연습 2: core c 가 방금 nw 를 올리면서 prev 를 ready list 로 돌려보냈다. prev 가 갈 수 있는 코어를 찾아 yield.
 *   - prev 가 c 에서 돌 수 있었다면 nw 의 친화성 코어는 후보에서 뺀다 (nw 가 그중 가장 낮은 코어를 골랐으므로
 *     나머지는 prev 이상인 우선순위를 돌리고 있다 — FreeRTOS 주석의 논증)
 *   - 높은 번호 코어부터, 엄격히 더 낮은(<) 우선순위일 때만 갱신 → 같으면 번호 큰 코어 */
static int replace_evicted(const task_t *prev, int c) {
    unsigned map = prev->affinity;
    if (map & (1u << c)) map &= ~g_core[c].cur->affinity;
    map &= ALL;
    int lowest = prev->prio, best = -1;
    for (int x = NC - 1; x >= 0; x--) {
        if (!(map & (1u << x)) || flagged(x)) continue;
        if (cur_prio(x) < lowest) { lowest = cur_prio(x); best = x; }
    }
    if (best >= 0) request_yield(best, c);
    return best;
}

static void test_ex1(void) {
    printf("-- 연습 1: task_set_affinity\n");
    task_t a = {"a", 2, ALL, T_BLOCKED, -1}, lo = {"lo", 1, ALL, T_BLOCKED, -1};
    reset(); run_on(&a, 1);
    task_set_affinity(&a, 1u << 0, 0);                 /* core1 에서 도는 중인데 core0 전용으로 */
    CHECK(g_core[1].yield_requested && g_core[1].ipi_sent == 1);
    reset(); run_on(&a, 0);
    task_set_affinity(&a, 0x3u, 2);                    /* 지금 코어(0)가 여전히 허용 → 아무 일 없음 */
    CHECK(!flagged(0) && !flagged(1) && g_core[0].ipi_sent == 0);
    reset(); a.state = T_READY; a.run_core = -1;
    run_on(&lo, 0);
    task_t h1 = {"h1", 3, ALL, T_BLOCKED, -1}, h2 = {"h2", 3, ALL, T_BLOCKED, -1}, h3 = {"h3", 3, ALL, T_BLOCKED, -1};
    run_on(&h1, 1); run_on(&h2, 2); run_on(&h3, 3);
    task_set_affinity(&a, 1u << 0, 0);                 /* READY 태스크가 core0(우선순위 1 실행 중)으로 → 자기 코어 yield */
    CHECK(g_core[0].yield_pending && g_core[0].ipi_sent == 0);
    reset(); run_on(&h1, 0); run_on(&h2, 1); run_on(&h3, 2); run_on(&lo, 3);
    task_set_affinity(&a, 0x7u, 3);                    /* core0..2 는 모두 3 순위 → 자리 없음 */
    CHECK(!flagged(0) && !flagged(1) && !flagged(2) && !flagged(3));
}

static void test_ex2(void) {
    printf("-- 연습 2: replace_evicted\n");
    task_t P = {"P", 2, ALL, T_READY, -1}, N = {"N", 3, 0x3u, T_BLOCKED, -1};
    task_t r1 = {"r1", 3, ALL, T_BLOCKED, -1}, r2 = {"r2", 0, ALL, T_BLOCKED, -1}, r3 = {"r3", 1, ALL, T_BLOCKED, -1};
    reset(); run_on(&N, 0); run_on(&r1, 1); run_on(&r2, 2); run_on(&r3, 3);
    CHECK(replace_evicted(&P, 0) == 2 && g_core[2].yield_requested);   /* 후보 {2,3} 중 가장 낮은 0 순위 */
    reset(); r2.prio = 1; run_on(&N, 0); run_on(&r1, 1); run_on(&r2, 2); run_on(&r3, 3);
    CHECK(replace_evicted(&P, 0) == 3);                /* 2·3 동률 → 높은 번호부터 보고 엄격 < 라서 3 */
    reset(); run_on(&N, 0); run_on(&r1, 1); run_on(&r2, 2); run_on(&r3, 3);
    g_core[3].yield_requested = true;
    CHECK(replace_evicted(&P, 0) == 2);                /* 3 은 이미 IPI 받음 → 건너뜀 */
    reset(); P.affinity = 0x6u; r2.prio = 0; run_on(&N, 0); run_on(&r1, 1); run_on(&r2, 2); run_on(&r3, 3);
    CHECK(replace_evicted(&P, 0) == 2);                /* P 가 core0 금지로 바뀐 경우: 자기 마스크 전체 {1,2} 탐색 */
    reset(); P.affinity = ALL; r2.prio = 2; r3.prio = 3; run_on(&N, 0); run_on(&r1, 1); run_on(&r2, 2); run_on(&r3, 3);
    CHECK(replace_evicted(&P, 0) == -1);               /* 같은 우선순위(2)는 선점 안 함 */
}

/* ===================== 연습 3: RP2040 SIO 하드웨어 스핀락 모델 ===================== */
static atomic_uint g_sio_lock[32];                     /* 하드웨어: SIO SPINLOCK0..31 */
static uint32_t sio_spinlock_read(int n) {             /* 읽기 = 잡기 시도. 성공하면 0 이 아닌 값 */
    return atomic_exchange_explicit(&g_sio_lock[n], 1u, memory_order_acquire) == 0u ? (1u << n) : 0u;
}
static void sio_spinlock_write(int n) {                /* 쓰기(아무 값) = 해제 */
    atomic_store_explicit(&g_sio_lock[n], 0u, memory_order_release);
}
static _Thread_local bool t_irq_masked;                /* 이 스레드(=코어)의 PRIMASK */
static uint32_t save_and_disable_interrupts(void) { uint32_t s = t_irq_masked; t_irq_masked = true; return s; }
static void restore_interrupts(uint32_t s) { t_irq_masked = s != 0u; }

static uint32_t hw_spin_lock_blocking(int n) {         /* pico-sdk spin_lock_blocking 과 같은 모양 */
    uint32_t saved = save_and_disable_interrupts();    /* ① 이 코어 ISR 차단 */
    while (sio_spinlock_read(n) == 0u) { }             /* ② 다른 코어 차단 (하드웨어가 원자적으로 판정) */
    return saved;
}
static void hw_spin_unlock(int n, uint32_t saved) {
    sio_spinlock_write(n);                             /* ① 해제 먼저 */
    restore_interrupts(saved);                         /* ② 인터럽트 복원 */
}

#define P3_N 200000
static uint32_t g_p3_count;
static void *p3_core(void *a) {
    (void)a;
    for (int i = 0; i < P3_N; i++) {
        uint32_t s = hw_spin_lock_blocking(7);
        g_p3_count++;
        hw_spin_unlock(7, s);
    }
    return NULL;
}
static void test_ex3(void) {
    printf("-- 연습 3: RP2040 SIO 스핀락 모델\n");
    uint32_t s = hw_spin_lock_blocking(5);
    CHECK(t_irq_masked && atomic_load(&g_sio_lock[5]) == 1u);
    CHECK(sio_spinlock_read(5) == 0u);                 /* 다른 코어의 시도: 실패 */
    hw_spin_unlock(5, s);
    CHECK(!t_irq_masked && sio_spinlock_read(5) == (1u << 5));
    sio_spinlock_write(5);
    t_irq_masked = true;                               /* 이미 막혀 있던 호출자 */
    s = hw_spin_lock_blocking(5); hw_spin_unlock(5, s);
    CHECK(t_irq_masked);                               /* 복원은 "켜기" 가 아니라 "원래대로" */
    t_irq_masked = false;
    pthread_t a, b;
    pthread_create(&a, NULL, p3_core, NULL);
    pthread_create(&b, NULL, p3_core, NULL);
    pthread_join(a, NULL); pthread_join(b, NULL);
    CHECK(g_p3_count == 2u * P3_N);
}

/* ===================== 연습 4: pthread_kill 을 IPI 로 ===================== */
static atomic_bool g_ipi_pending;                      /* lock-free 확인 → 핸들러에서 써도 된다 */
static atomic_int  g_ipi_handled, g_quit;
static void ipi_handler(int sig) {                     /* = SIO_IRQ_PROCn / SGI 핸들러: 펜딩 비트만 */
    (void)sig;
    atomic_store_explicit(&g_ipi_pending, true, memory_order_release);
}
static bool core_check_ipi(void) {                     /* 명령어 경계 = 스케줄러가 보는 곳 */
    return atomic_exchange_explicit(&g_ipi_pending, false, memory_order_acquire);
}
static void *p4_core(void *a) {
    (void)a;
    volatile uint32_t work = 0;
    while (!atomic_load(&g_quit)) {
        work = work + 1;                               /* "태스크 실행 중" */
        if (core_check_ipi()) atomic_fetch_add(&g_ipi_handled, 1);   /* 여기서 reschedule */
    }
    return NULL;
}
static void test_ex4(void) {
    printf("-- 연습 4: pthread_kill IPI\n");
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = ipi_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
    CHECK(atomic_is_lock_free(&g_ipi_pending));
    pthread_t th;
    pthread_create(&th, NULL, p4_core, NULL);
    const int N = 200;
    int timeouts = 0;
    for (int i = 1; i <= N; i++) {
        pthread_kill(th, SIGUSR1);
        int spins = 0;                                 /* 처리될 때까지 기다린다 (최대 ~2 s) */
        struct timespec us = {0, 10000};
        while (atomic_load(&g_ipi_handled) < i && spins < 200000) { nanosleep(&us, NULL); spins++; }
        if (atomic_load(&g_ipi_handled) < i) { timeouts++; break; }
    }
    atomic_store(&g_quit, 1);
    pthread_join(th, NULL);
    CHECK(timeouts == 0 && atomic_load(&g_ipi_handled) == N);   /* 하나 보내고 처리 확인 → 합쳐짐 없음 */
}

int main(void) {
    test_ex1();
    test_ex2();
    test_ex3();
    test_ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
