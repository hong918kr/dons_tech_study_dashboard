/* OS-B04 스레드 기초: pthread 생성과 공유 카운터 레이스
 *
 *   Part A  pthread_create / pthread_join: 인자 넘기기, 결과 돌려받기, 공유 주소 공간 확인, 구간 합 병렬화
 *   Part B  counter++ = LOAD → ADD → STORE 세 단계. 결정적 인터리빙 시뮬레이터로 lost update 를
 *           재현하고, 가능한 모든 인터리빙을 열거해 최종값 분포(최소 2!)를 구한다.
 *   Part C  단일 코어 MCU: UART RX ISR 과 main 루프가 공유하는 pending 카운터.
 *           인터럽트가 끼어드는 지점마다 결과를 본다 (LDR/SUB/STR vs LDREX/STREX vs 인터럽트 끄기).
 *
 * 체크 테스트는 매 실행 항상 성립하는 것만 본다. 실제 스레드 레이스(실행마다 다른 값)와 TSan 보고는
 * demo_race.c 에 있다. Part B/C 의 "깨지는 스케줄" 은 하네스가 일부러 고른 강제 스케줄이다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------------- 하네스가 주는 도구: 출발선(gate) ----------------
 * n 개 스레드가 모두 도착할 때까지 기다린다 → 모든 스레드가 "동시에 살아 있음" 을 보장.
 * macOS 에는 pthread_barrier_t 가 없어서 mutex + cond 로 만든다 (조건 변수 자체는 OS-B06).
 * 2초 안에 다 안 오면 포기하고 timed_out 을 켠다 → 틀린 코드가 멈추지 않고 FAIL 한다. */
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int need, arrived;
    bool timed_out;
} gate_t;

static void gate_init(gate_t *g, int need) {
    pthread_mutex_init(&g->mu, NULL);
    pthread_cond_init(&g->cv, NULL);
    g->need = need;
    g->arrived = 0;
    g->timed_out = false;
}
static void gate_destroy(gate_t *g) {
    pthread_cond_destroy(&g->cv);
    pthread_mutex_destroy(&g->mu);
}
static void gate_wait(gate_t *g) {
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += 2;
    pthread_mutex_lock(&g->mu);
    if (++g->arrived >= g->need) pthread_cond_broadcast(&g->cv);
    while (g->arrived < g->need && !g->timed_out)
        if (pthread_cond_timedwait(&g->cv, &g->mu, &dl) == ETIMEDOUT) g->timed_out = true;
    pthread_mutex_unlock(&g->mu);
}

/* ======================= Part A — pthread 생성과 공유 주소 공간 ======================= */
#define MAX_THREADS 16
static uint32_t g_magic;                 /* main 이 스레드를 만들기 "전에" 쓴다 */

typedef struct {
    int id;                              /* in : 0..n-1 */
    const uint32_t *a;                   /* in : 맡은 구간의 시작 */
    size_t n;                            /* in : 구간 길이 */
    int *heap_slot;                      /* in : main 이 malloc 한 배열에서 내 칸 */
    gate_t *gate;                        /* in : 출발선 */
    uint64_t sum;                        /* out: 구간 합 */
    uintptr_t local_addr;                /* out: 내 스택에 있는 지역 변수의 주소 */
    const uint32_t *global_addr;         /* out: 내가 본 &g_magic */
    uint32_t global_seen;                /* out: 내가 읽은 g_magic 값 */
} worker_t;

/* 스레드 본체. ① gate_wait 로 출발선 통과 ② 지역 변수 하나의 주소를 local_addr 에
 * ③ &g_magic 과 그 값을 기록 ④ 구간 합을 sum 에 ⑤ *heap_slot = id + 1
 * ⑥ arg 를 그대로 반환 (pthread_join 이 돌려준다). 공유 변수에는 쓰지 않는다 — 각자 자기 칸만. */
static void *worker_main(void *arg) {
    worker_t *w = arg;
    gate_wait(w->gate);
    uint64_t s = 0;                                   /* 지역 변수 = 이 스레드 스택 */
    w->local_addr = (uintptr_t)&s;
    w->global_addr = &g_magic;
    w->global_seen = g_magic;
    for (size_t i = 0; i < w->n; i++) s += w->a[i];
    w->sum = s;
    *w->heap_slot = w->id + 1;
    return w;
}

/* w[0..n) 마다 스레드 하나로 worker_main(&w[i]) 을 실행하고 전부 join 한다.
 * 반환: 0 = 성공 / pthread_create 의 에러 코드 (이미 만든 스레드는 join 한 뒤) /
 *       -1 = join 이 돌려준 값이 &w[i] 가 아님. */
static int run_workers(worker_t *w, int n) {
    pthread_t tid[MAX_THREADS];
    int made = 0, err = 0;
    if (n < 1 || n > MAX_THREADS) return EINVAL;
    for (; made < n; made++) {
        err = pthread_create(&tid[made], NULL, worker_main, &w[made]);
        if (err != 0) break;
    }
    for (int i = 0; i < made; i++) {
        void *ret = NULL;
        pthread_join(tid[i], &ret);
        if (err == 0 && ret != &w[i]) err = -1;
    }
    return err;
}

/* a[0..n) 를 nthreads 개 구간으로 나눠 병렬로 합한다. 나머지 n % nthreads 개는 앞 구간들에 1 개씩.
 * 각 스레드는 자기 worker_t 에만 쓰고, 합치는 일은 join 뒤 main 이 한다. 실패하면 UINT64_MAX. */
static uint64_t sum_parallel(const uint32_t *a, size_t n, int nthreads) {
    worker_t w[MAX_THREADS];
    int slots[MAX_THREADS];
    gate_t g;
    if (nthreads < 1 || nthreads > MAX_THREADS) return UINT64_MAX;
    gate_init(&g, nthreads);
    size_t base = n / (size_t)nthreads, extra = n % (size_t)nthreads, off = 0;
    for (int i = 0; i < nthreads; i++) {
        size_t len = base + ((size_t)i < extra ? 1u : 0u);
        w[i] = (worker_t){ .id = i, .a = a + off, .n = len, .heap_slot = &slots[i], .gate = &g };
        off += len;
    }
    int err = run_workers(w, nthreads);
    gate_destroy(&g);
    if (err != 0) return UINT64_MAX;
    uint64_t total = 0;
    for (int i = 0; i < nthreads; i++) total += w[i].sum;
    return total;
}

/* ======================= Part B — counter++ 인터리빙 시뮬레이터 ======================= */
#define VM_MAXT 4
enum { ST_LOAD, ST_ADD, ST_STORE };      /* pc = 다음에 실행할 단계 (arm64: ldr / add / str) */
typedef enum { INC_LDR_ADD_STR, INC_LDADD } incmode_t;   /* 3 단계 vs 원자 RMW 1 단계 */
typedef struct { int pc; uint32_t reg; int left; } vcpu_t; /* left: 남은 증가 횟수 */
typedef struct { uint32_t mem; int nt; incmode_t mode; vcpu_t cpu[VM_MAXT]; } vm_t;

static void vm_init(vm_t *vm, int nt, int k, incmode_t mode) {
    memset(vm, 0, sizeof *vm);
    vm->nt = nt;
    vm->mode = mode;
    for (int t = 0; t < nt; t++) vm->cpu[t].left = k;
}

/* 스레드 t 의 "다음 한 단계" 를 실행한다. 이미 끝난 스레드(left == 0)면 아무것도 안 하고 false.
 *   INC_LDR_ADD_STR : ST_LOAD reg = mem → ST_ADD reg += 1 → ST_STORE mem = reg, left--, pc = ST_LOAD
 *   INC_LDADD       : 한 단계에 mem += 1, left-- (arm64 ldadd / 원자 RMW 를 흉내) */
static bool vm_step(vm_t *vm, int t) {
    vcpu_t *c = &vm->cpu[t];
    if (c->left == 0) return false;
    if (vm->mode == INC_LDADD) {
        vm->mem += 1u;
        c->left--;
        return true;
    }
    switch (c->pc) {
    case ST_LOAD:  c->reg = vm->mem; c->pc = ST_ADD;   break;
    case ST_ADD:   c->reg += 1u;     c->pc = ST_STORE; break;
    default:       vm->mem = c->reg; c->pc = ST_LOAD;  c->left--; break;
    }
    return true;
}

/* 스케줄 문자열 대로 실행: 'A' = 스레드 0, 'B' = 스레드 1, ... 한 글자 = 한 단계.
 * 문자열이 끝나면 남은 일을 A, B, ... 순서로 끝까지 실행. 최종 mem 을 반환. (하네스 제공) */
static uint32_t vm_run(int nt, int k, incmode_t mode, const char *sched) {
    vm_t vm;
    vm_init(&vm, nt, k, mode);
    for (const char *p = sched; *p; p++) {
        int t = *p - 'A';
        if (t >= 0 && t < nt) (void)vm_step(&vm, t);
    }
    for (int t = 0; t < nt; t++)
        while (vm_step(&vm, t)) { }
    return vm.mem;
}

/* 가능한 모든 인터리빙을 열거한다 (DFS: 매 단계 "아직 안 끝난 스레드 중 누구를 돌릴까" 로 갈라짐).
 * 끝까지 간 인터리빙마다 hist[최종 mem] += 1. 반환: 인터리빙 개수.
 * hist 는 호출자가 0 으로 채운 nt*k+1 칸. vm_t 는 값으로 복사해서 갈래마다 따로 진행한다. */
static uint64_t explore(vm_t vm, uint64_t *hist) {
    uint64_t leaves = 0;
    bool any = false;
    for (int t = 0; t < vm.nt; t++) {
        vm_t next = vm;
        if (vm_step(&next, t)) {
            any = true;
            leaves += explore(next, hist);
        }
    }
    if (!any) {
        hist[vm.mem]++;
        return 1;
    }
    return leaves;
}
static uint64_t count_outcomes(int nt, int k, incmode_t mode, uint64_t *hist) {
    vm_t vm;
    vm_init(&vm, nt, k, mode);
    return explore(vm, hist);
}

/* 2 스레드 × k 번 증가(k >= 2)에서 최종값이 2 가 되는 스케줄을 out 에 쓴다 ('A'/'B', 길이 6k, NUL 끝).
 * (INC_LDR_ADD_STR 모드 기준. 한 증가 = 3 글자.) */
static void worst_schedule(int k, char *out) {
    char *p = out;
    *p++ = 'A';                                         /* A: LOAD 0 하고 멈춤 */
    for (int i = 0; i < 3 * (k - 1); i++) *p++ = 'B';   /* B: k-1 번 온전히 증가 → mem = k-1 */
    *p++ = 'A'; *p++ = 'A';                             /* A: ADD, STORE 1 → B 의 일을 덮음 */
    *p++ = 'B';                                         /* B: LOAD 1 하고 멈춤 */
    for (int i = 0; i < 3 * (k - 1); i++) *p++ = 'A';   /* A: 나머지 k-1 번 → mem = k */
    *p++ = 'B'; *p++ = 'B';                             /* B: ADD, STORE 2 → A 의 일을 덮음 */
    *p = '\0';
}

/* ======================= Part C — 단일 코어 MCU: UART RX ISR vs main ======================= */
typedef enum { RMW_PLAIN, RMW_LDREX, RMW_IRQOFF } rmwkind_t;
typedef struct {
    uint32_t pending;        /* 공유: ISR 이 ++ (바이트 도착), main 이 -- (바이트 처리) */
    bool monitor;            /* 로컬 배타적 모니터: LDREX 가 켜고, 예외 진입/복귀가 지운다 */
    bool primask;            /* true = 인터럽트 막힘 (CPSID i) */
    bool irq_latched;        /* 막혀 있는 동안 들어온 IRQ (NVIC pending) */
    int isr_runs;            /* ISR 실행 횟수 */
    int main_steps;          /* main 이 실행한 단계 수 */
    int retries;             /* STREX 실패로 다시 돈 횟수 */
    int isr_delay;           /* IRQ 가 들어온 뒤 ISR 이 실행되기까지 main 이 실행한 단계 수 */
} mcu_t;

static void uart_rx_isr(mcu_t *m) {          /* ISR 은 끝까지 한 번에 실행된다 (단일 코어) */
    m->pending++;                            /* 더 높은 우선순위 ISR 은 없다고 가정 */
    m->isr_runs++;
    m->monitor = false;                      /* 예외 진입/복귀가 로컬 모니터를 지운다 (ARMv7-M, C-M14) */
}

/* main 의 "pending -= 1" 을 kind 방식의 단계들로 실행한다. 초기 pending = 5.
 *   RMW_PLAIN : LDR r ; SUBS r,#1 ; STR r                                (3 단계)
 *   RMW_LDREX : LDREX r (monitor=on) ; SUBS ; STREX → monitor 가 꺼져 있으면 실패 → LDREX 부터 다시
 *   RMW_IRQOFF: CPSID i ; LDR ; SUBS ; STR ; CPSIE i                     (5 단계)
 * IRQ 는 main 이 inject_at 번째 단계(0 부터)를 실행하기 "직전" 에 한 번 들어온다.
 * main 이 그 전에 끝나면 끝난 뒤에 들어온다. 막혀 있으면(primask) 래치해 두었다가 CPSIE 직후 실행.
 * isr_delay = IRQ 가 들어온 뒤 ISR 실행까지 main 이 실행한 단계 수. 결과 상태를 반환. */
static mcu_t mcu_main_take(rmwkind_t kind, int inject_at) {
    mcu_t m = { .pending = 5u };
    uint32_t r = 0;
    int pc = 0, fired_at = -1;
    bool done = false;
    while (!done) {
        if (m.main_steps == inject_at) {                 /* IRQ 들어옴 */
            fired_at = m.main_steps;
            if (m.primask) m.irq_latched = true;
            else uart_rx_isr(&m);
        }
        m.main_steps++;
        if (kind == RMW_IRQOFF) {
            switch (pc++) {
            case 0: m.primask = true; break;                       /* CPSID i */
            case 1: r = m.pending; break;                          /* LDR */
            case 2: r -= 1u; break;                                /* SUBS */
            case 3: m.pending = r; break;                          /* STR */
            default:
                m.primask = false;                                 /* CPSIE i */
                if (m.irq_latched) {
                    m.irq_latched = false;
                    m.isr_delay = m.main_steps - fired_at;
                    uart_rx_isr(&m);
                }
                done = true;
            }
        } else {
            switch (pc) {
            case 0: r = m.pending; if (kind == RMW_LDREX) m.monitor = true; pc = 1; break;
            case 1: r -= 1u; pc = 2; break;
            default:
                if (kind == RMW_LDREX && !m.monitor) { m.retries++; pc = 0; break; }  /* STREX 실패 */
                m.pending = r;
                m.monitor = false;
                done = true;
            }
        }
    }
    if (m.isr_runs == 0) uart_rx_isr(&m);                 /* main 이 먼저 끝났다 → 그 뒤에 IRQ */
    return m;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
        else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

int main(void) {
    /* ---------- Part A ---------- */
    {
        enum { N = 6 };
        static const uint32_t data[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
        worker_t w[N];
        gate_t g;
        int *heap = calloc(N, sizeof *heap);
        int main_local = 0;
        g_magic = 0xC0FFEEu;                 /* pthread_create 전에 쓴 값은 새 스레드에 보인다 */
        gate_init(&g, N);
        for (int i = 0; i < N; i++)
            w[i] = (worker_t){ .id = i, .a = data + 2 * i, .n = 2, .heap_slot = &heap[i], .gate = &g };
        int err = run_workers(w, N);
        CHECK(err == 0);
        CHECK(!g.timed_out);                                  /* 6 개가 동시에 살아 있었다 */
        bool same_global = true, seen = true, heap_ok = true, sums_ok = true, distinct = true;
        for (int i = 0; i < N; i++) {
            same_global &= (w[i].global_addr == &g_magic);
            seen &= (w[i].global_seen == 0xC0FFEEu);
            heap_ok &= (heap[i] == i + 1);
            sums_ok &= (w[i].sum == (uint64_t)(data[2 * i] + data[2 * i + 1]));
            distinct &= (w[i].local_addr != 0 && w[i].local_addr != (uintptr_t)&main_local);
            for (int j = 0; j < i; j++) distinct &= (w[i].local_addr != w[j].local_addr);
        }
        CHECK(same_global);         /* 전역은 모든 스레드에서 같은 주소 */
        CHECK(seen);                /* create 전에 쓴 값을 읽었다 */
        CHECK(heap_ok);             /* 스레드가 힙에 쓴 것이 join 뒤 main 에 보인다 */
        CHECK(sums_ok);
        CHECK(distinct);            /* 지역 변수는 스레드마다 다른 스택 */
        if (w[0].local_addr && w[1].local_addr) {
            uintptr_t d = w[0].local_addr > w[1].local_addr ? w[0].local_addr - w[1].local_addr
                                                            : w[1].local_addr - w[0].local_addr;
            printf("info: &g_magic=%p  T0 local=%#lx  T1 local=%#lx  (stack gap %lu KiB)\n",
                   (void *)&g_magic, (unsigned long)w[0].local_addr, (unsigned long)w[1].local_addr,
                   (unsigned long)(d / 1024));
        }
        gate_destroy(&g);
        free(heap);
        CHECK(run_workers(w, 0) == EINVAL && run_workers(w, MAX_THREADS + 1) == EINVAL);

        /* 구간 합: 길이·스레드 수 조합 (나누어떨어지지 않는 경우 포함) */
        enum { LEN = 100003 };
        uint32_t *big = malloc(LEN * sizeof *big), seed = 2026u;
        uint64_t serial = 0;
        for (size_t i = 0; i < LEN; i++) { big[i] = xorshift32(&seed) & 0xFFFFu; serial += big[i]; }
        bool all = true;
        for (int nt = 1; nt <= 8; nt++) all &= (sum_parallel(big, LEN, nt) == serial);
        CHECK(all);
        CHECK(sum_parallel(big, 5, 8) == (uint64_t)big[0] + big[1] + big[2] + big[3] + big[4]); /* 빈 구간 */
        CHECK(sum_parallel(big, 0, 3) == 0u);
        free(big);
    }

    /* ---------- Part B ---------- */
    {
        CHECK(vm_run(2, 1, INC_LDR_ADD_STR, "AAABBB") == 2u);   /* 직렬: 정상 */
        CHECK(vm_run(2, 1, INC_LDR_ADD_STR, "ABABAB") == 1u);   /* 둘 다 0 을 읽음 → 하나 증발 */
        CHECK(vm_run(2, 1, INC_LDR_ADD_STR, "ABBBAA") == 1u);   /* B 가 끝낸 뒤 A 가 옛 값으로 덮음 */
        CHECK(vm_run(3, 2, INC_LDR_ADD_STR, "") == 6u);         /* 스케줄 없음 = 순서대로 */
        vm_t vm;
        vm_init(&vm, 1, 1, INC_LDR_ADD_STR);
        CHECK(vm_step(&vm, 0) && vm.cpu[0].reg == 0u && vm.cpu[0].pc == ST_ADD && vm.mem == 0u);
        CHECK(vm_step(&vm, 0) && vm.cpu[0].reg == 1u && vm.mem == 0u);       /* ADD 는 레지스터만 */
        CHECK(vm_step(&vm, 0) && vm.mem == 1u && vm.cpu[0].left == 0);
        CHECK(!vm_step(&vm, 0));                                             /* 끝난 스레드 */

        uint64_t h[16] = { 0 };
        CHECK(count_outcomes(2, 1, INC_LDR_ADD_STR, h) == 20u);   /* 6!/(3!3!) */
        CHECK(h[2] == 2u && h[1] == 18u);                          /* 직렬 2 가지만 정답 */
        memset(h, 0, sizeof h);
        uint64_t n22 = count_outcomes(2, 2, INC_LDR_ADD_STR, h);
        CHECK(n22 == 924u && h[0] == 0u && h[1] == 0u && h[2] > 0u && h[4] > 0u);  /* 최소 2, 최대 4 */
        printf("info: 2 threads x 2 incs: %llu interleavings -> final 2:%llu 3:%llu 4:%llu\n",
               (unsigned long long)n22, (unsigned long long)h[2], (unsigned long long)h[3],
               (unsigned long long)h[4]);
        memset(h, 0, sizeof h);
        uint64_t n31 = count_outcomes(3, 1, INC_LDR_ADD_STR, h);
        CHECK(n31 == 1680u && h[3] == 6u && h[1] + h[2] + h[3] == 1680u);   /* 정답 = 3! 직렬 순서뿐 */
        printf("info: 3 threads x 1 inc : %llu interleavings -> final 1:%llu 2:%llu 3:%llu\n",
               (unsigned long long)n31, (unsigned long long)h[1], (unsigned long long)h[2],
               (unsigned long long)h[3]);
        memset(h, 0, sizeof h);
        CHECK(count_outcomes(3, 2, INC_LDADD, h) == 90u && h[6] == 90u);    /* 원자 RMW: 항상 정답 */

        static char s[6 * 64 + 1];
        bool worst_ok = true;
        for (int k = 2; k <= 64; k++) {
            memset(s, 0, sizeof s);
            worst_schedule(k, s);
            int na = 0, nb = 0;
            for (const char *p = s; *p; p++) { na += (*p == 'A'); nb += (*p == 'B'); }
            worst_ok &= (strlen(s) == (size_t)(6 * k) && na == 3 * k && nb == 3 * k &&
                         vm_run(2, k, INC_LDR_ADD_STR, s) == 2u);
        }
        CHECK(worst_ok);                       /* 2 x 64 번 증가해도 결과 2 가 가능하다 */
    }

    /* ---------- Part C ---------- */
    {
        uint32_t plain[4];
        for (int i = 0; i < 4; i++) plain[i] = mcu_main_take(RMW_PLAIN, i).pending;
        CHECK(plain[0] == 5u && plain[3] == 5u);   /* LDR 전·STR 후에 온 IRQ: 정상 (5 + 1 - 1) */
        CHECK(plain[1] == 4u && plain[2] == 4u);   /* LDR 과 STR 사이: ISR 의 ++ 증발 */

        bool ldrex_ok = true;
        for (int i = 0; i < 4; i++) {
            mcu_t m = mcu_main_take(RMW_LDREX, i);
            ldrex_ok &= (m.pending == 5u && m.isr_runs == 1);
        }
        CHECK(ldrex_ok);                           /* 어느 지점이든 정답 */
        mcu_t m1 = mcu_main_take(RMW_LDREX, 1), m0 = mcu_main_take(RMW_LDREX, 0);
        CHECK(m1.retries == 1 && m1.main_steps == 6);   /* ISR 이 모니터를 지움 → STREX 실패 → 재시도 */
        CHECK(m0.retries == 0 && m0.main_steps == 3);

        bool off_ok = true;
        for (int i = 0; i < 6; i++) {
            mcu_t m = mcu_main_take(RMW_IRQOFF, i);
            off_ok &= (m.pending == 5u && m.isr_runs == 1);
        }
        CHECK(off_ok);
        CHECK(mcu_main_take(RMW_IRQOFF, 1).isr_delay == 4);   /* 막힌 동안 ISR 이 늦어진다 */
        CHECK(mcu_main_take(RMW_IRQOFF, 0).isr_delay == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
