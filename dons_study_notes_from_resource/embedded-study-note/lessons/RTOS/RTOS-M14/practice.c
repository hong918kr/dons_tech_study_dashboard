/* RTOS-M14 추가 연습 — 한 번에 검증
 *
 * solution.c 의 미니 커널 + "libc" 를 그대로 쓰고, 커널에 스케줄러 정지(vTaskSuspendAll 대역)를 더했다.
 * 연습 1: UART 로 글자 단위 출력 → 두 태스크의 줄이 섞인다 → 줄 단위 락.
 * 연습 2: errno(REENT->err)를 전역 하나로 두면 선점한 태스크가 덮는다 → 태스크별 reent.
 * 연습 3: malloc 락을 스케줄러 정지로 vs 뮤텍스로 — 힙을 안 쓰는 H(모터 제어)의 완료 시점 비교.
 * 연습 4: 라이브러리 락(strtok 세션 락) + 중간 우선순위 M → H 의 대기 = 남은 세션 + C_M (RTOS-M03 의 역전). _r 은 락이 없어 0.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#define _XOPEN_SOURCE 600            /* macOS: ucontext 선언을 보이게 (모든 include 전에) */
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
/* macOS SDK 는 getcontext/makecontext/swapcontext 를 deprecated 로 표시한다 (POSIX.1-2008 에서 삭제됨).
 * 결정적인 코루틴 커널을 만들려고 일부러 쓰는 것이므로 이 경고 하나만 끈다. */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define STREQ(a, b) (strcmp((a), (b)) == 0)

/* ======================= 미니 커널 (주어짐) ======================= */
#define MAXT 4
#define MAXTICK 48
#define STACK_SZ (64 * 1024)
enum { T_DORMANT, T_READY, T_BLOCKED, T_DONE };
enum { K_OK = 0, K_DEADLOCK = -1, K_TIMEOUT = -2 };

typedef struct k_reent {            /* newlib struct _reent 의 축소판 (태스크별 C 런타임 상태) */
    char *strtok_last;              /* _REENT_STRTOK_LAST(r) */
    char fmt_buf[11];               /* 정적 결과 버퍼의 태스크별 사본 (newlib: _asctime_buf 등) */
    int err;                        /* _errno — 연습 2 */
} k_reent_t;

typedef struct k_mutex k_mutex_t;
typedef struct tcb {
    const char *name;
    int prio, state, done_tick;
    void (*entry)(void);
    ucontext_t ctx;
    k_reent_t reent;                /* FreeRTOS: TCB 의 configTLS_BLOCK_TYPE xTLSBlock (tasks.c:434) */
    k_mutex_t *wait;                /* 기다리는 뮤텍스 */
    char tl[MAXTICK + 1];           /* 타임라인: R 실행, r READY, B 블록, . 미출시/끝 */
} tcb_t;
struct k_mutex { tcb_t *owner; };
typedef struct { int tick; void (*isr)(void); } k_event_t;

static tcb_t g_t[MAXT];
static int g_nt;
static tcb_t *g_cur;                /* CPU 를 가진(가졌던) 태스크. ISR 은 그 위에서 돈다 */
static ucontext_t g_sched;
static int g_tick, g_why, g_ticks_used, g_self_deadlock, g_isr_lock;
static int g_sched_susp;            /* vTaskSuspendAll 깊이 (중첩 가능한 카운터) */
static bool g_in_isr;
static bool g_cfg_reent;            /* configUSE_NEWLIB_REENTRANT 스위치 */
static k_reent_t g_impure_data;     /* newlib _impure_data: 전역 하나 */
static k_reent_t *g_impure_ptr = &g_impure_data;   /* newlib _impure_ptr */
#define REENT (g_impure_ptr)        /* newlib _REENT (__DYNAMIC_REENT__ 없을 때 = _impure_ptr) */
static _Alignas(16) char g_stack[MAXT][STACK_SZ];
enum { WHY_SWITCH, WHY_TICK };

static void port_switch_hook(tcb_t *next);

static void k_reset(bool cfg_reent) {
    memset(g_t, 0, sizeof g_t);
    g_nt = 0; g_cur = NULL; g_tick = 0; g_ticks_used = 0; g_self_deadlock = 0; g_isr_lock = 0; g_sched_susp = 0;
    g_in_isr = false; g_cfg_reent = cfg_reent;
    memset(&g_impure_data, 0, sizeof g_impure_data);
    g_impure_ptr = &g_impure_data;
}
static void k_trampoline(void) {    /* 태스크 함수가 return 하면 DONE → uc_link 로 스케줄러 복귀 */
    g_cur->entry();
    g_cur->state = T_DONE;
    g_cur->done_tick = g_tick;
    g_why = WHY_SWITCH;
}
static void reent_init(k_reent_t *r) { memset(r, 0, sizeof *r); }   /* configINIT_TLS_BLOCK = _REENT_INIT_PTR */
static int k_create(const char *name, int prio, void (*entry)(void)) {
    tcb_t *t = &g_t[g_nt];
    t->name = name; t->prio = prio; t->entry = entry; t->state = T_DORMANT; t->done_tick = -1;
    memset(t->tl, '.', MAXTICK); t->tl[MAXTICK] = '\0';
    reent_init(&t->reent);
    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_sp = g_stack[g_nt];
    t->ctx.uc_stack.ss_size = STACK_SZ;
    t->ctx.uc_link = &g_sched;
    makecontext(&t->ctx, k_trampoline, 0);
    return g_nt++;
}
static tcb_t *k_self(void) { return g_in_isr ? NULL : g_cur; }
static void k_release(int i) { if (g_t[i].state == T_DORMANT) g_t[i].state = T_READY; }  /* GiveFromISR 로 깨우기 */
static void kpoint(void) {          /* tick 경계 = 선점 가능 지점. ISR 안이나 커널 밖에서는 아무 일 없음 */
    if (g_in_isr || g_cur == NULL) return;
    g_why = WHY_TICK;
    swapcontext(&g_cur->ctx, &g_sched);
}
static void k_switch_out(void) { g_why = WHY_SWITCH; swapcontext(&g_cur->ctx, &g_sched); }
static void k_mutex_lock(k_mutex_t *m) {
    tcb_t *me = k_self();
    if (me == NULL) { g_isr_lock += g_in_isr; return; } /* ISR 은 블록 못 함 (실제: configASSERT) */
    if (m->owner == NULL) { m->owner = me; return; }
    if (m->owner == me) g_self_deadlock++;              /* 비재귀 뮤텍스를 소유자가 다시 잡음 → 영원히 대기 */
    me->wait = m;
    me->state = T_BLOCKED;
    k_switch_out();                                     /* 돌아오면 소유권을 넘겨받은 상태 */
}
static void k_mutex_unlock(k_mutex_t *m) {
    if (g_in_isr || g_cur == NULL) return;
    tcb_t *best = NULL;
    for (int i = 0; i < g_nt; i++)
        if (g_t[i].state == T_BLOCKED && g_t[i].wait == m && (!best || g_t[i].prio > best->prio))
            best = &g_t[i];                             /* 우선순위 높은 대기자, 같으면 생성 순 */
    m->owner = best;
    if (best) {
        best->wait = NULL;
        best->state = T_READY;
        if (best->prio > g_cur->prio) k_switch_out();   /* 즉시 양보 (FreeRTOS queueYIELD_IF_USING_PREEMPTION) */
    }
}
static void k_sched_suspend(void) { g_sched_susp++; }         /* vTaskSuspendAll: 문맥 전환 금지, ISR 은 계속 */
static void k_sched_resume(void) { if (g_sched_susp > 0) g_sched_susp--; }   /* xTaskResumeAll (밀린 전환은 다음 kpoint 에) */
static tcb_t *k_pick(void) {
    if (g_sched_susp > 0 && g_cur && g_cur->state == T_READY) return g_cur;   /* 정지 중: 지금 태스크 계속 */
    tcb_t *best = NULL;
    for (int i = 0; i < g_nt; i++) {
        tcb_t *t = &g_t[i];
        if (t->state != T_READY) continue;
        if (!best || t->prio > best->prio || (t->prio == best->prio && t == g_cur)) best = t;
    }
    return best;
}
/* ev[] 의 ISR 대역을 정해진 tick 시작에 부르며 태스크를 돌린다. 끝나면 g_cur = NULL (커널 밖). */
static int k_run_ticks(const k_event_t *ev, int nev) {
    for (g_tick = 0; g_tick < MAXTICK; g_tick++) {
        for (int i = 0; i < nev; i++)
            if (ev[i].tick == g_tick) { g_in_isr = true; ev[i].isr(); g_in_isr = false; }
        tcb_t *ran = NULL;
        for (tcb_t *n; (n = k_pick()) != NULL;) {
            if (n != g_cur) { if (g_cfg_reent) port_switch_hook(n); g_cur = n; }   /* vTaskSwitchContext */
            swapcontext(&g_sched, &n->ctx);
            if (g_why == WHY_TICK) { ran = n; break; }  /* 이 tick 은 n 이 썼다 */
        }                                               /* 블록/끝/양보면 같은 tick 을 다음 태스크에게 */
        for (int i = 0; i < g_nt; i++) {
            tcb_t *t = &g_t[i];
            t->tl[g_tick] = t == ran ? 'R' : t->state == T_READY ? 'r' : t->state == T_BLOCKED ? 'B' : '.';
        }
        bool future = false, ready = false, blocked = false, all_done = true;
        for (int i = 0; i < nev; i++) if (ev[i].tick > g_tick && ev[i].tick < MAXTICK) future = true;
        for (int i = 0; i < g_nt; i++) {
            if (g_t[i].state == T_READY) ready = true;
            if (g_t[i].state == T_BLOCKED) blocked = true;
            if (g_t[i].state != T_DONE) all_done = false;
        }
        g_ticks_used = g_tick + 1;
        if (all_done && !future) { if (!ran) g_ticks_used = g_tick; return K_OK; }   /* 빈 마지막 열은 생략 */
        if (!ready && !future) return blocked ? K_DEADLOCK : K_OK;
    }
    return K_TIMEOUT;
}
static int k_run(const k_event_t *ev, int nev) {
    int r = k_run_ticks(ev, nev);
    g_cur = NULL;
    return r;
}
static void k_print_timeline(const char *title) {
    printf("%s\ntick : ", title);
    for (int k = 0; k < g_ticks_used; k++) printf("%d ", k % 10);
    printf("\n");
    for (int p = 9; p >= 0; p--)                         /* 우선순위 높은 줄부터 */
        for (int i = 0; i < g_nt; i++) {
            if (g_t[i].prio != p) continue;
            printf("%-4s : ", g_t[i].name);
            for (int k = 0; k < g_ticks_used; k++) printf("%c ", g_t[i].tl[k]);
            printf("\n");
        }
}
static int count_ch(const tcb_t *t, char c) {
    int n = 0;
    for (int k = 0; k < g_ticks_used; k++) n += t->tl[k] == c;
    return n;
}

/* ======================= "libc" (주어짐) ======================= */
static int is_delim(char c, const char *d) {
    for (; *d; d++) if (*d == c) return 1;
    return 0;
}
/* POSIX strtok_r 의미 (C-A04 에서 구현·검증한 것과 같다). 상태는 *save 에만. */
static char *lib_strtok_r(char *s, const char *d, char **save) {
    if (s == NULL) s = *save;
    while (*s && is_delim(*s, d)) s++;
    if (*s == '\0') { *save = s; return NULL; }
    char *tok = s;
    while (*s && !is_delim(*s, d)) s++;
    if (*s) { *s = '\0'; *save = s + 1; } else *save = s;
    return tok;
}
/* 옛 libc 의 strtok: 숨은 static 상태 하나를 모든 태스크가 공유 (macOS libc 도 static char *last). */
static char *lib_strtok(char *s, const char *d) {
    static char *last;
    return lib_strtok_r(s, d, &last);
}
static k_mutex_t g_libc_mtx;        /* "라이브러리 락" 하나 (FreeRTOS: xSemaphoreCreateMutex) */
/* ======================= 공통 도우미 ======================= */
static void port_switch_hook(tcb_t *next) { g_impure_ptr = &next->reent; }   /* configSET_TLS_BLOCK */
typedef struct { const char *key, *val; } kv_t;
static int parse_kv_with(char *(*tk)(char *, const char *), char *line, kv_t *kv, int max) {
    int n = 0;
    char *k = tk(line, ",=");
    while (k && n < max) {
        kpoint();
        char *v = tk(NULL, ",=");
        if (!v) break;
        kv[n].key = k; kv[n].val = v; n++;
        kpoint();
        k = tk(NULL, ",=");
    }
    return n;
}
static char *strtok_r_any(char *s, const char *d) {   /* 연습 4 의 _r 판: 태스크마다 다른 save (태스크 인덱스로) */
    static char *save[MAXT];
    return lib_strtok_r(s, d, &save[g_cur - g_t]);
}
static int parse_kv_locked(char *line, kv_t *kv, int max) {
    k_mutex_lock(&g_libc_mtx);
    int n = parse_kv_with(lib_strtok, line, kv, max);
    k_mutex_unlock(&g_libc_mtx);
    return n;
}

/* ======================= 연습 1 — UART 한 줄이 섞인다 ======================= */
static char g_uart[64];
static size_t g_uart_n;
static k_mutex_t g_uart_mtx;
static bool g_ex1_locked;
static void uart_putc(char c) {                        /* TX 레지스터에 한 글자 = 한 tick */
    if (g_uart_n + 1 < sizeof g_uart) { g_uart[g_uart_n++] = c; g_uart[g_uart_n] = '\0'; }
    kpoint();
}
static void uart_puts_line(const char *s) {            /* 한 줄 전체를 락 하나로 */
    if (g_ex1_locked) k_mutex_lock(&g_uart_mtx);
    while (*s) uart_putc(*s++);
    if (g_ex1_locked) k_mutex_unlock(&g_uart_mtx);
}
static void ex1_L(void) { uart_puts_line("L:3300\n"); }
static void ex1_H(void) { uart_puts_line("H:7\n"); }
static int g_hid;
static void isr_wake_h(void) { k_release(g_hid); }
static int ex1(bool locked, int h_rel) {
    k_reset(false);
    g_ex1_locked = locked; g_uart_mtx.owner = NULL; g_uart_n = 0; g_uart[0] = '\0';
    int l = k_create("L", 1, ex1_L);
    g_hid = k_create("H", 3, ex1_H);
    k_release(l);
    k_event_t ev[1] = { { h_rel, isr_wake_h } };
    return k_run(ev, 1);
}

/* ======================= 연습 2 — errno 도 태스크별이어야 한다 ======================= */
static int parse_u32(const char *s, uint32_t *out) {   /* strtoul 처럼: 넘치면 err = ERANGE */
    uint64_t v = 0;
    REENT->err = 0;
    for (; *s >= '0' && *s <= '9'; s++) {
        v = v * 10u + (uint64_t)(*s - '0');
        if (v > UINT32_MAX) { REENT->err = ERANGE; *out = UINT32_MAX; return -1; }
    }
    *out = (uint32_t)v;
    return 0;
}
static int g_l_err, g_h_err;
static void ex2_L(void) {
    uint32_t v;
    (void)parse_u32("99999999999", &v);
    kpoint();                                          /* 결과를 쓰기 전에 선점 */
    g_l_err = REENT->err;
}
static void ex2_H(void) { uint32_t v; (void)parse_u32("42", &v); g_h_err = REENT->err; kpoint(); }
static int ex2(bool reent) {
    k_reset(reent);
    g_l_err = g_h_err = -1;
    int l = k_create("L", 1, ex2_L);
    g_hid = k_create("H", 3, ex2_H);
    k_release(l);
    k_event_t ev[1] = { { 1, isr_wake_h } };
    return k_run(ev, 1);
}

/* ======================= 연습 3 — malloc 락: 스케줄러 정지 vs 뮤텍스 ======================= */
static bool g_heap_used[8];
static k_mutex_t g_heap_mtx;
static int g_heap_mode;                                /* 0 = 뮤텍스, 1 = 스케줄러 정지 (heap_3 식) */
static void heap_lock(void)   { if (g_heap_mode) k_sched_suspend(); else k_mutex_lock(&g_heap_mtx); }
static void heap_unlock(void) { if (g_heap_mode) k_sched_resume();  else k_mutex_unlock(&g_heap_mtx); }
static int toy_alloc(int walk_ticks) {
    heap_lock();
    int i = 0;
    while (i < 8 && g_heap_used[i]) i++;
    for (int k = 0; k < walk_ticks; k++) kpoint();      /* 큰 힙을 걷는 시간 */
    if (i < 8) g_heap_used[i] = true;
    heap_unlock();
    return i;
}
static int g_l_blk;
static void ex3_L(void) { g_l_blk = toy_alloc(4); }
static void ex3_H(void) { for (int k = 0; k < 3; k++) kpoint(); }   /* 모터 제어 3 tick — 힙은 안 쓴다 */
static int ex3(int mode) {
    k_reset(false);
    g_heap_mode = mode; g_heap_mtx.owner = NULL; memset(g_heap_used, 0, sizeof g_heap_used);
    g_l_blk = -1;
    int l = k_create("L", 1, ex3_L);
    g_hid = k_create("H", 3, ex3_H);
    k_release(l);
    k_event_t ev[1] = { { 1, isr_wake_h } };
    return k_run(ev, 1);
}

/* ======================= 연습 4 — 라이브러리 락이 낳는 우선순위 역전 ======================= */
static char g_lbuf[40], g_hbuf[40];
static kv_t g_lkv[4], g_hkv[4];
static int g_ln, g_hn, g_cm, g_mid;
static bool g_ex4_r;
static void ex4_L(void) {
    g_ln = g_ex4_r ? parse_kv_with(strtok_r_any, g_lbuf, g_lkv, 4) : parse_kv_locked(g_lbuf, g_lkv, 4);
}
static void ex4_H(void) {
    g_hn = g_ex4_r ? parse_kv_with(strtok_r_any, g_hbuf, g_hkv, 4) : parse_kv_locked(g_hbuf, g_hkv, 4);
}
static void ex4_M(void) { for (int k = 0; k < g_cm; k++) kpoint(); }   /* 락과 무관한 CPU 일 */
static void isr_wake_m(void) { k_release(g_mid); }
static int ex4(bool use_r, int cm) {
    k_reset(false);
    g_ex4_r = use_r; g_cm = cm; g_libc_mtx.owner = NULL;
    strcpy(g_lbuf, "rate=100,mode=fast,gain=3");
    strcpy(g_hbuf, "led=1,pwm=50");
    g_ln = g_hn = -1;
    int l = k_create("L", 1, ex4_L);
    g_hid = k_create("H", 3, ex4_H);
    g_mid = k_create("M", 2, ex4_M);
    k_release(l);
    k_event_t ev[2] = { { 2, isr_wake_h }, { 3, isr_wake_m } };
    return k_run(ev, 2);
}

int main(void) {
    int kr;

    /* 연습 1 */
    kr = ex1(false, 2);
    printf("[1] raw   : \"%s\"\n", g_uart);
    CHECK(kr == K_OK && STREQ(g_uart, "L:H:7\n3300\n"));     /* 한 줄 안에 다른 줄이 끼었다 */
    kr = ex1(true, 2);
    k_print_timeline("[1] line lock, H released at tick 2");
    printf("[1] locked: \"%s\"  H blocked=%d\n", g_uart, count_ch(&g_t[g_hid], 'B'));
    CHECK(kr == K_OK && STREQ(g_uart, "L:3300\nH:7\n"));
    CHECK(count_ch(&g_t[g_hid], 'B') == 5);                   /* L 의 남은 5글자 */

    /* 연습 2 */
    kr = ex2(false);
    printf("[2] shared errno: L err=%d H err=%d\n", g_l_err, g_h_err);
    CHECK(kr == K_OK && g_l_err == 0 && g_h_err == 0);        /* L 이 ERANGE 를 놓쳤다 */
    kr = ex2(true);
    printf("[2] per-task errno: L err=%d H err=%d\n", g_l_err, g_h_err);
    CHECK(kr == K_OK && g_l_err == ERANGE && g_h_err == 0);

    /* 연습 3 */
    kr = ex3(0);
    k_print_timeline("[3] heap lock = mutex");
    int done_mtx = g_t[g_hid].done_tick;
    CHECK(kr == K_OK && g_l_blk == 0 && count_ch(&g_t[g_hid], 'r') == 0);   /* 힙을 안 쓰는 H 는 바로 돈다 */
    kr = ex3(1);
    k_print_timeline("[3] heap lock = scheduler suspend");
    int done_sus = g_t[g_hid].done_tick;
    printf("[3] H done tick: mutex=%d suspend=%d\n", done_mtx, done_sus);
    CHECK(kr == K_OK && g_l_blk == 0 && count_ch(&g_t[g_hid], 'r') == 3);   /* 정지 중엔 H 가 READY 인 채 못 돈다 */
    CHECK(done_mtx == 4 && done_sus == 7);                    /* 힙과 무관한 H 까지 L 의 임계 구역만큼 늦어진다 */

    /* 연습 4 */
    int cms[4] = { 0, 2, 4, 8 }, ok = 0;
    for (int i = 0; i < 4; i++) {
        kr = ex4(false, cms[i]);
        int hb = count_ch(&g_t[g_hid], 'B');
        printf("[4] lock, C_M=%d: H blocked=%d (L pairs=%d H pairs=%d)\n", cms[i], hb, g_ln, g_hn);
        if (i == 2) k_print_timeline("[4] lock, C_M=4");
        ok += kr == K_OK && g_ln == 3 && g_hn == 2 && hb == 4 + cms[i];
    }
    CHECK(ok == 4);                                           /* H 의 대기 = L 의 남은 세션 + M 의 길이 */
    kr = ex4(true, 8);
    printf("[4] _r,   C_M=8: H blocked=%d (L pairs=%d H pairs=%d)\n", count_ch(&g_t[g_hid], 'B'), g_ln, g_hn);
    CHECK(kr == K_OK && g_ln == 3 && g_hn == 2 && count_ch(&g_t[g_hid], 'B') == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
