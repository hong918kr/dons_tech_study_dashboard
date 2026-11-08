/* RTOS-M14 재진입성과 스레드 안전: 공유 라이브러리 함수 보호
 *
 * 미니 커널 (단일 OS 스레드, tick 구동, ucontext 코루틴 — 결정적):
 *   - 태스크 = ucontext 코루틴 (각자 64 KB 스택). 진짜 호출 스택이 있으므로 "함수 한가운데서 선점" 이 그대로 일어난다.
 *   - kpoint() = "여기서 tick 인터럽트가 걸릴 수 있다". 스케줄러로 돌아가 tick 이 1 늘고, 그 tick 에 예정된
 *     ISR 대역(스크립트)이 돌고, 가장 높은 우선순위 READY 태스크가 다음 tick 을 받는다 (같으면 돌던 태스크 계속).
 *   - k_mutex: 비재귀, 소유자 + 우선순위순 대기, 해제 시 직접 넘겨주고 더 높은 쪽이면 즉시 양보. 상속 없음 (RTOS-B10/M04).
 * Part A: 정적 버퍼 fmt_u32_static / 숨은 상태 lib_strtok 를 두 태스크가 공유 → 선점되면 결과가 섞인다. _r 로 수정.
 * Part B: 뮤텍스 래핑 — 락 범위 = "정적 결과를 다 쓸 때까지" / "토큰화 세션 전체". 호출 단위 락은 strtok 을 못 고친다.
 *         newlib __malloc_lock 처럼 **재귀** 락으로 toy 힙 보호 (realloc 이 락을 쥔 채 malloc/free 를 부른다).
 * Part C: 태스크별 컨텍스트 — newlib struct _reent / _impure_ptr 모델. 컨텍스트 스위치 훅이 g_impure_ptr 를
 *         바꾼다 (FreeRTOS configUSE_NEWLIB_REENTRANT → configSET_TLS_BLOCK). 태스크끼리는 해결, ISR 은 여전히 깨진다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   (solution.c 도 같은 방법)
 */
#define _XOPEN_SOURCE 600            /* macOS: ucontext 선언을 보이게 (모든 include 전에) */
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
static int str_is(const char *p, const char *s) { return p != NULL && STREQ(p, s); }   /* NULL 안전 */

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
    g_nt = 0; g_cur = NULL; g_tick = 0; g_ticks_used = 0; g_self_deadlock = 0; g_isr_lock = 0;
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
static tcb_t *k_pick(void) {
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
/* 10진 변환의 몸통: buf[11] 의 뒤에서부터 자릿수를 쓰고 시작 위치를 돌려준다. 자릿수마다 선점 가능. */
static const char *fmt_u32_core(uint32_t v, char *buf) {
    char *p = &buf[10];
    *p = '\0';
    do {
        *--p = (char)('0' + v % 10u);
        v /= 10u;
        kpoint();                   /* 변환 "도중" 에 tick 이 걸린다 */
    } while (v);
    return p;
}
/* 옛날식 API: 정적 버퍼를 돌려준다 (itoa, inet_ntoa, asctime, strerror 와 같은 모양). */
static const char *fmt_u32_static(uint32_t v) {
    static char buf[11];
    return fmt_u32_core(v, buf);
}
static k_mutex_t g_libc_mtx;        /* "라이브러리 락" 하나 (FreeRTOS: xSemaphoreCreateMutex) */
/* 흔한 오답: strtok 을 호출 단위로만 감싼다. 각 호출은 원자적이지만 호출 "사이" 의 상태는 여전히 공유. */
static char *lib_strtok_percall(char *s, const char *d) {
    k_mutex_lock(&g_libc_mtx);
    char *r = lib_strtok(s, d);
    k_mutex_unlock(&g_libc_mtx);
    return r;
}

/* ======================= Part A ======================= */
/* 재진입 버전: 결과를 호출자 버퍼 buf[n] 에 쓴다. 자릿수 + NUL 이 안 들어가면 아무것도 안 쓰고 NULL.
 * fmt_u32_core 처럼 자릿수마다 kpoint() 를 둘 것 (같은 선점 지점에서 비교하기 위해). 반환: buf 안의 시작 위치. */
static char *fmt_u32_r(uint32_t v, char *buf, size_t n) {
    int len = 0;
    for (uint32_t t = v; ; t /= 10u) { len++; if (t < 10u) break; }
    if (buf == NULL || n < (size_t)len + 1) return NULL;
    buf[len] = '\0';
    for (int i = len - 1; i >= 0; i--) {
        buf[i] = (char)('0' + v % 10u);
        v /= 10u;
        kpoint();
    }
    return buf;
}

typedef struct { const char *key, *val; } kv_t;
/* "rate=100,mode=fast" → 키/값 쌍. 구분자 ",=" 하나로 토큰을 번갈아 키, 값으로 읽는다 (중첩 없는 한 세션).
 * 토큰마다 kpoint() = 필드 처리 시간. tk = 사용할 strtok 류 (상태 위치만 다르고 API 는 같다). */
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
static int parse_kv_static(char *line, kv_t *kv, int max) { return parse_kv_with(lib_strtok, line, kv, max); }
static int parse_kv_percall(char *line, kv_t *kv, int max) { return parse_kv_with(lib_strtok_percall, line, kv, max); }

/* parse_kv_static 과 같은 결과·같은 kpoint 위치. 단 lib_strtok_r + 지역 save 포인터로 (상태를 호출자에게). */
static int parse_kv_r(char *line, kv_t *kv, int max) {
    char *save = NULL;
    int n = 0;
    char *k = lib_strtok_r(line, ",=", &save);
    while (k && n < max) {
        kpoint();
        char *v = lib_strtok_r(NULL, ",=", &save);
        if (!v) break;
        kv[n].key = k; kv[n].val = v; n++;
        kpoint();
        k = lib_strtok_r(NULL, ",=", &save);
    }
    return n;
}

/* ======================= Part B ======================= */
/* 정적 버퍼 함수를 뮤텍스로 감싸기: 락을 쥔 채 fmt_u32_static 을 부르고, 결과를 dst[n] 에 **복사한 뒤** 푼다.
 * (락을 결과 사용 전에 풀면 다음 태스크가 같은 버퍼를 덮는다.) */
static void fmt_u32_locked(uint32_t v, char *dst, size_t n) {
    k_mutex_lock(&g_libc_mtx);
    const char *p = fmt_u32_static(v);
    snprintf(dst, n, "%s", p);
    k_mutex_unlock(&g_libc_mtx);
}
/* strtok 세션 전체를 락으로: lock → parse_kv_static → unlock. */
static int parse_kv_locked(char *line, kv_t *kv, int max) {
    k_mutex_lock(&g_libc_mtx);
    int n = parse_kv_static(line, kv, max);
    k_mutex_unlock(&g_libc_mtx);
    return n;
}

/* toy 힙: 32바이트 블록 8개. newlib 처럼 malloc/free/realloc 이 MALLOC_LOCK/UNLOCK 을 부른다. */
#define HB 8
static unsigned char g_heap[HB][32];
static bool g_used[HB];
static k_mutex_t g_heap_mtx;
static int g_heap_depth;            /* 재귀 깊이 */
static bool g_mlock_nonrec;         /* 비교용: true 면 비재귀 락 (주어짐) */
/* newlib __malloc_lock 의미: 같은 태스크가 이미 쥐고 있으면 깊이만 +1 (malloc 이 재귀적으로 부를 수 있다 —
 * newlib mlock.c 설명). 아니면 g_heap_mtx 를 잡고 깊이 1. 비재귀 모드는 주어진 대로 둔다. */
static void malloc_lock(void) {
    if (g_mlock_nonrec) { k_mutex_lock(&g_heap_mtx); return; }
    if (g_heap_mtx.owner != NULL && g_heap_mtx.owner == k_self()) { g_heap_depth++; return; }
    k_mutex_lock(&g_heap_mtx);
    g_heap_depth = 1;
}
static void malloc_unlock(void) {
    if (g_mlock_nonrec) { k_mutex_unlock(&g_heap_mtx); return; }
    if (g_heap_depth > 0 && --g_heap_depth == 0) k_mutex_unlock(&g_heap_mtx);
}
static void *toy_malloc(void) {
    malloc_lock();
    int i = 0;
    while (i < HB && g_used[i]) i++;
    kpoint();                       /* 프리 리스트를 걷다가 선점 (가장 위험한 순간) */
    void *p = NULL;
    if (i < HB) { g_used[i] = true; p = g_heap[i]; }
    malloc_unlock();
    return p;
}
static void toy_free(void *p) {
    malloc_lock();
    for (int i = 0; i < HB; i++) if (p == g_heap[i]) g_used[i] = false;
    malloc_unlock();
}
static void *toy_realloc(void *p, size_t n) {   /* newlib _mallocr.c rEALLOc 처럼 락을 쥔 채 malloc/free */
    malloc_lock();
    void *q = toy_malloc();
    if (q) { memcpy(q, p, n); toy_free(p); }
    malloc_unlock();
    return q;
}

/* ======================= Part C ======================= */
/* 컨텍스트 스위치 훅 (FreeRTOS vTaskSwitchContext 안의 configSET_TLS_BLOCK, tasks.c:5292):
 * 다음 태스크의 reent 를 "현재 C 런타임 상태" 로 만든다 — newlib-freertos.h: _impure_ptr = &xTLSBlock. */
static void port_switch_hook(tcb_t *next) {
    g_impure_ptr = &next->reent;
}
/* newlib 식 strtok: API 는 옛 strtok 과 같고 상태만 REENT 안에 (newlib strtok.c: &_REENT_STRTOK_LAST(reent)). */
static char *strtok_reent(char *s, const char *d) {
    return lib_strtok_r(s, d, &REENT->strtok_last);
}
/* newlib 식 정적 결과 함수: 버퍼가 REENT 안에 있다. fmt_u32_core 사용. */
static const char *fmt_u32_reent(uint32_t v) {
    return fmt_u32_core(v, REENT->fmt_buf);
}
static int parse_kv_reent(char *line, kv_t *kv, int max) { return parse_kv_with(strtok_reent, line, kv, max); }

/* ======================= 시나리오 (주어짐) ======================= */
/* 텔레메트리 태스크 L(우선순위 1): 설정 줄 파싱 + 전압 포맷.
 * CLI 태스크 H(우선순위 3): UART RX ISR 이 깨운다. 명령 파싱 + 응답 값 포맷. 둘이 같은 "libc" 를 쓴다. */
enum { V_STATIC, V_R, V_PERCALL, V_LOCKED, V_REENT };
enum { SC_FMT = 1, SC_TOK = 2, SC_BOTH = 3 };
static int g_var, g_scn, g_lid, g_hid;
static char g_lbuf[40], g_hbuf[40], g_isrbuf[16], g_lout[16], g_hout[16];
static kv_t g_lkv[4], g_hkv[4];
static int g_ln, g_hn;

static void emit_u32(uint32_t v, char *dst, size_t n) {
    char tmp[11] = "";
    const char *p;
    switch (g_var) {
    case V_R:      p = fmt_u32_r(v, tmp, sizeof tmp); break;
    case V_LOCKED: fmt_u32_locked(v, tmp, sizeof tmp); p = tmp; break;
    case V_REENT:  p = fmt_u32_reent(v); break;
    default:       p = fmt_u32_static(v); break;
    }
    kpoint();                       /* 결과를 UART 로 내보내는 동안에도 선점될 수 있다 */
    snprintf(dst, n, "%s", p ? p : "(null)");
}
static int parse_kv(char *line, kv_t *kv, int max) {
    switch (g_var) {
    case V_R:       return parse_kv_r(line, kv, max);
    case V_PERCALL: return parse_kv_percall(line, kv, max);
    case V_LOCKED:  return parse_kv_locked(line, kv, max);
    case V_REENT:   return parse_kv_reent(line, kv, max);
    default:        return parse_kv_static(line, kv, max);
    }
}
static void task_L(void) {
    if (g_scn & SC_TOK) g_ln = parse_kv(g_lbuf, g_lkv, 4);
    if (g_scn & SC_FMT) emit_u32(3300u, g_lout, sizeof g_lout);
}
static void task_H(void) {
    if (g_scn & SC_TOK) g_hn = parse_kv(g_hbuf, g_hkv, 4);
    if (g_scn & SC_FMT) emit_u32(7u, g_hout, sizeof g_hout);
}
static void isr_uart_rx(void) { k_release(g_hid); }   /* xSemaphoreGiveFromISR + portYIELD_FROM_ISR 대역 */
static void isr_parse_bad(void) {                      /* ISR 이 직접 newlib 식 strtok 을 쓴다 (나쁜 예) */
    strcpy(g_isrbuf, "x=1,y=2");
    (void)strtok_reent(g_isrbuf, ",=");
    (void)strtok_reent(NULL, ",=");
}
static void isr_parse_good(void) {                     /* ISR 이 자기 save 포인터로 _r 을 쓴다 */
    char *sv = NULL;
    strcpy(g_isrbuf, "x=1,y=2");
    (void)lib_strtok_r(g_isrbuf, ",=", &sv);
    (void)lib_strtok_r(NULL, ",=", &sv);
}

static int run(int scn, int var, bool reent, int h_rel, int isr_tick, void (*isr)(void)) {
    k_reset(reent);
    g_var = var; g_scn = scn;
    g_libc_mtx.owner = NULL;
    strcpy(g_lbuf, "rate=100,mode=fast,gain=3");
    strcpy(g_hbuf, "led=1,pwm=50");
    g_lout[0] = g_hout[0] = '\0';
    g_ln = g_hn = -1;
    memset(g_lkv, 0, sizeof g_lkv); memset(g_hkv, 0, sizeof g_hkv);
    g_lid = k_create("L", 1, task_L);
    g_hid = k_create("H", 3, task_H);
    k_release(g_lid);
    k_event_t ev[2] = { { h_rel, isr_uart_rx }, { isr_tick, isr } };
    return k_run(ev, isr ? 2 : 1);
}
static bool kv_is(const kv_t *kv, int i, const char *k, const char *v) {
    return kv[i].key && kv[i].val && STREQ(kv[i].key, k) && STREQ(kv[i].val, v);
}
static bool l_kv_ok(void) {
    return g_ln == 3 && kv_is(g_lkv, 0, "rate", "100") && kv_is(g_lkv, 1, "mode", "fast") && kv_is(g_lkv, 2, "gain", "3");
}
static bool h_kv_ok(void) { return g_hn == 2 && kv_is(g_hkv, 0, "led", "1") && kv_is(g_hkv, 1, "pwm", "50"); }
static bool fmt_ok(void) { return STREQ(g_lout, "3300") && STREQ(g_hout, "7"); }

/* 해제 tick 0..N 전부에 대해 돌려 "모두 맞다" 를 센다 (모든 지점 주입). */
static int sweep_ok(int scn, int var, bool reent, int nrel) {
    int ok = 0;
    for (int r = 0; r < nrel; r++) {
        int kr = run(scn, var, reent, r, -1, NULL);
        bool good = kr == K_OK;
        if (scn & SC_FMT) good = good && fmt_ok();
        if (scn & SC_TOK) good = good && l_kv_ok() && h_kv_ok();
        ok += good;
    }
    return ok;
}

static void sweep_print(const char *label, int scn, int var, bool reent, int nrel) {
    printf("%-14s", label);
    for (int r = 0; r < nrel; r++) {
        (void)run(scn, var, reent, r, -1, NULL);
        if (scn == SC_FMT) printf(" %d:%s/%s", r, g_lout, g_hout);
        else printf(" %d:%d/%d", r, g_ln, g_hn);
    }
    printf("\n");
}

/* 힙 시나리오 */
static void *g_lp, *g_hp, *g_q;
static void task_L_heap(void) { g_lp = toy_malloc(); if (g_lp) memset(g_lp, 'L', 32); kpoint(); }
static void task_H_heap(void) { g_hp = toy_malloc(); if (g_hp) memset(g_hp, 'H', 32); kpoint(); }
static void task_realloc(void) {
    char *p = toy_malloc();
    if (p) strcpy(p, "cfg");
    g_q = p ? toy_realloc(p, 4) : NULL;
}
static void heap_reset(bool nonrec) {
    memset(g_used, 0, sizeof g_used); memset(g_heap, 0, sizeof g_heap);
    g_heap_mtx.owner = NULL; g_heap_depth = 0; g_mlock_nonrec = nonrec;
    g_lp = g_hp = g_q = NULL;
}
static bool points_into(const char *p, const char *buf, size_t n) {   /* == 비교만 (관계 비교는 같은 객체끼리만 정의) */
    for (size_t i = 0; i <= n; i++) if (p == buf + i) return true;
    return false;
}
static int blk_index(const void *p) { for (int i = 0; i < HB; i++) if (p == g_heap[i]) return i; return -1; }
static int used_count(void) { int n = 0; for (int i = 0; i < HB; i++) n += g_used[i]; return n; }

int main(void) {
    char b[16];
    int kr;

    /* ---------- Part A: 재현 + _r ---------- */
    kr = run(SC_FMT, V_STATIC, false, 99, -1, NULL);          /* H 가 안 오면 정적 버퍼도 멀쩡 */
    CHECK(kr == K_OK && STREQ(g_lout, "3300"));
    kr = run(SC_FMT, V_STATIC, false, 2, -1, NULL);           /* L 이 두 자리 쓴 뒤 H 가 선점 */
    k_print_timeline("[A] static fmt, H released at tick 2");
    printf("     L=\"%s\" H=\"%s\"\n", g_lout, g_hout);
    CHECK(kr == K_OK && STREQ(g_lout, "3307") && STREQ(g_hout, "7"));   /* 3300 의 아래 두 자리를 H 가 덮었다 */
    sweep_print("[A] fmt static", SC_FMT, V_STATIC, false, 8);   /* 해제 tick:L/H */
    CHECK(sweep_ok(SC_FMT, V_STATIC, false, 8) < 8);          /* 어딘가에서는 깨진다 */

    CHECK(str_is(fmt_u32_r(0u, b, sizeof b), "0"));
    CHECK(str_is(fmt_u32_r(4294967295u, b, 11), "4294967295"));
    CHECK(fmt_u32_r(4294967295u, b, 10) == NULL);             /* NUL 자리가 없다 */
    CHECK(fmt_u32_r(100u, b, 4) == b && str_is(b, "100"));
    CHECK(sweep_ok(SC_FMT, V_R, false, 8) == 8);              /* 모든 해제 시점에서 맞다 */

    kr = run(SC_TOK, V_STATIC, false, 99, -1, NULL);
    CHECK(kr == K_OK && l_kv_ok());
    kr = run(SC_TOK, V_STATIC, false, 3, -1, NULL);           /* L 이 rate=100 을 읽은 뒤 H 가 선점 */
    k_print_timeline("[A] static strtok, H released at tick 3");
    printf("     L pairs=%d H pairs=%d\n", g_ln, g_hn);
    CHECK(kr == K_OK && g_ln == 1 && kv_is(g_lkv, 0, "rate", "100") && h_kv_ok());   /* L 은 mode, gain 을 잃었다 */
    sweep_print("[A] tok static", SC_TOK, V_STATIC, false, 8);   /* 해제 tick:L쌍/H쌍 */
    CHECK(sweep_ok(SC_TOK, V_STATIC, false, 8) < 8);
    CHECK(sweep_ok(SC_TOK, V_R, false, 8) == 8);
    kr = run(SC_BOTH, V_R, false, 3, -1, NULL);
    CHECK(kr == K_OK && l_kv_ok() && h_kv_ok() && fmt_ok());

    /* ---------- Part B: 뮤텍스 래핑 + 재귀 malloc 락 ---------- */
    kr = run(SC_TOK, V_PERCALL, false, 3, -1, NULL);          /* 호출 단위 락: 여전히 깨진다 */
    CHECK(kr == K_OK && g_ln == 1 && h_kv_ok());
    kr = run(SC_FMT, V_LOCKED, false, 2, -1, NULL);
    k_print_timeline("[B] locked fmt, H released at tick 2");
    printf("     L=\"%s\" H=\"%s\"  H blocked ticks=%d\n", g_lout, g_hout, count_ch(&g_t[g_hid], 'B'));
    CHECK(kr == K_OK && fmt_ok());
    CHECK(count_ch(&g_t[g_hid], 'B') == 2);                   /* L 의 남은 두 자리 동안 H 가 기다린다 */
    CHECK(sweep_ok(SC_FMT, V_LOCKED, false, 8) == 8);
    kr = run(SC_TOK, V_LOCKED, false, 3, -1, NULL);
    k_print_timeline("[B] locked strtok session, H released at tick 3");
    printf("     L pairs=%d H pairs=%d  H blocked ticks=%d\n", g_ln, g_hn, count_ch(&g_t[g_hid], 'B'));
    CHECK(kr == K_OK && l_kv_ok() && h_kv_ok());
    CHECK(count_ch(&g_t[g_hid], 'B') == 3);                   /* L 의 남은 세션(3 tick) 전체 */
    CHECK(sweep_ok(SC_BOTH, V_LOCKED, false, 12) == 12);

    heap_reset(false);
    k_reset(false);
    g_lid = k_create("L", 1, task_L_heap);
    g_hid = k_create("H", 3, task_H_heap);
    k_release(g_lid);
    {
        k_event_t ev[1] = { { 1, isr_uart_rx } };             /* L 이 프리 리스트를 걷는 중에 H 가 깨어난다 */
        kr = k_run(ev, 1);
    }
    k_print_timeline("[B] toy heap, H released at tick 1");
    printf("     L=block %d  H=block %d  used=%d\n", blk_index(g_lp), blk_index(g_hp), used_count());
    CHECK(kr == K_OK && g_lp && g_hp && g_lp != g_hp && used_count() == 2);   /* 같은 블록을 두 번 주지 않는다 */
    CHECK(g_lp && ((unsigned char *)g_lp)[0] == 'L' && g_hp && ((unsigned char *)g_hp)[0] == 'H');

    heap_reset(false);
    k_reset(false);
    k_create("T", 2, task_realloc);
    k_release(0);
    kr = k_run(NULL, 0);
    CHECK(kr == K_OK && str_is(g_q, "cfg") && used_count() == 1 && g_heap_depth == 0);
    heap_reset(true);                                         /* 비재귀 락이면 realloc 에서 자기 교착 */
    k_reset(false);
    k_create("T", 2, task_realloc);
    k_release(0);
    kr = k_run(NULL, 0);
    printf("[B] realloc with NON-recursive lock: k_run=%d self_deadlock=%d\n", kr, g_self_deadlock);
    CHECK(kr == K_DEADLOCK && g_self_deadlock == 1);

    /* ---------- Part C: 태스크별 reent (configUSE_NEWLIB_REENTRANT) ---------- */
    kr = run(SC_BOTH, V_REENT, true, 3, -1, NULL);
    k_print_timeline("[C] newlib-style reent ON, UART RX ISR at tick 3");
    printf("     L pairs=%d \"%s\"  H pairs=%d \"%s\"\n", g_ln, g_lout, g_hn, g_hout);
    CHECK(kr == K_OK && l_kv_ok() && h_kv_ok() && fmt_ok());
    CHECK(sweep_ok(SC_BOTH, V_REENT, true, 12) == 12);
    CHECK(points_into(g_t[g_lid].reent.strtok_last, g_lbuf, sizeof g_lbuf));   /* 각자의 상태가 각자의 줄을 가리킨다 */
    CHECK(points_into(g_t[g_hid].reent.strtok_last, g_hbuf, sizeof g_hbuf));
    kr = run(SC_TOK, V_REENT, false, 3, -1, NULL);            /* 옵션 꺼짐: 전역 _impure_data 하나 = static 과 같다 */
    CHECK(kr == K_OK && g_ln == 1 && h_kv_ok());
    CHECK(sweep_ok(SC_BOTH, V_REENT, false, 12) < 12);
    kr = run(SC_TOK, V_REENT, true, 99, 2, isr_parse_bad);    /* ISR 은 선점한 태스크(L)의 reent 를 쓴다 */
    printf("[C] reent ON + ISR strtok at tick 2: L pairs=%d  [1]=%s=%s\n", g_ln,
           g_lkv[1].key ? g_lkv[1].key : "-", g_lkv[1].val ? g_lkv[1].val : "-");
    CHECK(kr == K_OK && g_ln == 2 && kv_is(g_lkv, 0, "rate", "100") && kv_is(g_lkv, 1, "y", "2"));   /* ISR 의 문자열을 이어 읽었다 */
    kr = run(SC_TOK, V_REENT, true, 99, 2, isr_parse_good);   /* ISR 이 자기 save 를 쓰면 L 은 무사 */
    CHECK(kr == K_OK && l_kv_ok());

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
