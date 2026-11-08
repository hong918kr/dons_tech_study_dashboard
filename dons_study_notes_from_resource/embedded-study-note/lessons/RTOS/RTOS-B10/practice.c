/* RTOS-B10 추가 연습 — 한 번에 검증
 *
 * 연습 1  진짜 POSIX 뮤텍스 타입 (ERRORCHECK / RECURSIVE) 이 소유권을 어떻게 보고하나 — pthread 로 실측
 * 연습 2  바이너리 세마포어 + owner 필드 = "소유권 검사 락" — 그래도 뮤텍스가 아닌 이유 (상속 없음)
 * 연습 3  ISR 은 뮤텍스 대신 세마포어로 태스크를 깨우고, 공유 로그는 태스크가 뮤텍스로 보호
 * 연습 4  재귀 뮤텍스 + 에러 경로: goto 정리로 깊이 균형 맞추기 vs 조기 return 버그
 * 모델 커널(아래)은 solution.c 와 같은 코드다.
 */
#define _XOPEN_SOURCE 700              /* -std=c11 에서 pthread_mutexattr_settype 선언 (include 전에) */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define NTASK   6
#define MAXW    8
#define NO_TASK (-1)
#define CTX_ISR (-2)                   /* 호출자가 ISR: 태스크 번호가 없다 */

enum { K_OK = 0, K_BLOCKED = 1,        /* 성공 / 대기 리스트에 들어감 */
       K_EAGAIN = -1,                  /* 지금은 못 얻음 (블록 안 함)   */
       K_EPERM = -2,                   /* 소유자가 아닌데 give          */
       K_EDEADLK = -3,                 /* 이미 내가 가진 일반 뮤텍스를 또 take */
       K_EFULL = -4,                   /* 바이너리 세마포어가 이미 1    */
       K_EISR = -5 };                  /* ISR 에서 뮤텍스 사용          */

typedef enum { OBJ_BINSEM, OBJ_MUTEX, OBJ_RMUTEX } kind_t;

typedef struct kobj kobj_t;
typedef struct {
    const char *name;
    int base_prio;                     /* 생성 시 우선순위                    */
    int prio;                          /* 현재 우선순위 (상속으로 올라갈 수 있음) */
    int held;                          /* 보유 중인 뮤텍스 수 (FreeRTOS uxMutexesHeld) */
    const kobj_t *blocked_on;          /* 대기 중인 객체 (없으면 NULL)          */
} task_t;

struct kobj {
    kind_t kind;
    int count;                         /* 세마포어: 0/1. 뮤텍스: 1 = 비어 있음 */
    int holder;                        /* 뮤텍스 소유 태스크 (NO_TASK = 없음)  */
    int depth;                         /* 재귀 뮤텍스 중첩 깊이               */
    int nwait;
    int wait[MAXW];                    /* 대기 태스크 (들어온 순서)            */
};

static task_t T[NTASK];

/* ---------------- 주어진 코드 (고치지 않는다) ---------------- */
static void task_init(int id, const char *name, int prio) {
    T[id].name = name; T[id].base_prio = T[id].prio = prio;
    T[id].held = 0; T[id].blocked_on = NULL;
}
static void obj_init(kobj_t *o, kind_t kind, int initial) {
    memset(o, 0, sizeof *o);
    o->kind = kind;
    o->holder = NO_TASK;
    o->count = (kind == OBJ_BINSEM) ? initial : 1;   /* 뮤텍스는 항상 "비어 있음" 으로 시작 */
}
static void enqueue(kobj_t *o, int id) {
    if (o->nwait < MAXW) o->wait[o->nwait++] = id;
    T[id].blocked_on = o;
}
/* 대기자 중 우선순위가 가장 높은 태스크를 꺼낸다. 같으면 먼저 온 쪽 (FreeRTOS 이벤트 리스트와 같은 규칙). */
static int pop_best(kobj_t *o) {
    if (o->nwait == 0) return NO_TASK;
    int bi = 0;
    for (int i = 1; i < o->nwait; i++)
        if (T[o->wait[i]].prio > T[o->wait[bi]].prio) bi = i;
    int id = o->wait[bi];
    for (int i = bi; i + 1 < o->nwait; i++) o->wait[i] = o->wait[i + 1];
    o->nwait--;
    T[id].blocked_on = NULL;
    return id;
}
/* 바이너리 세마포어: 소유자 개념이 없다. 누구든(ISR 포함) give 할 수 있다. */
static int sem_take(kobj_t *o, int caller, int may_block) {
    if (o->count > 0) { o->count--; return K_OK; }
    if (caller == CTX_ISR || !may_block) return K_EAGAIN;      /* ISR 은 절대 블록하지 않는다 */
    enqueue(o, caller);
    return K_BLOCKED;
}
static int sem_give(kobj_t *o, int caller) {
    (void)caller;                                              /* 누가 주는지 보지 않는다 */
    if (pop_best(o) != NO_TASK) return K_OK;                   /* 대기자에게 바로 넘김 (count 는 0 그대로) */
    if (o->count >= 1) return K_EFULL;                         /* FreeRTOS: errQUEUE_FULL */
    o->count = 1;
    return K_OK;
}
/* 스케줄러가 고를 태스크: ready(블록 안 됨) 중 우선순위 최고. mask 의 비트 i = 태스크 i 후보. */
static int pick_ready(unsigned mask) {
    int best = NO_TASK;
    for (int i = 0; i < NTASK; i++)
        if ((mask >> i & 1u) && T[i].blocked_on == NULL && (best == NO_TASK || T[i].prio > T[best].prio))
            best = i;
    return best;
}

/* ---------------- Part C: 우선순위 상속 (FreeRTOS 규칙) ---------------- */
/* waiter 가 o 를 기다리며 블록할 때: 소유자 우선순위가 더 낮으면 waiter 의 우선순위로 올린다. */
static void pi_on_block(kobj_t *o, int waiter) {
    int h = o->holder;
    if (h == NO_TASK) return;                                  /* ISR 이 가져간 경우 등: 올릴 대상이 없다 */
    if (T[waiter].prio > T[h].prio) T[h].prio = T[waiter].prio;
}
/* owner 가 뮤텍스 하나를 내려놓은 직후: 더 쥔 뮤텍스가 없을 때만 기본 우선순위로 복원. */
static void pi_on_release(int owner) {
    if (owner < 0) return;
    if (T[owner].held == 0) T[owner].prio = T[owner].base_prio;
}

/* ---------------- Part A: 뮤텍스 = 소유권이 있는 락 ---------------- */
static int mutex_take(kobj_t *o, int caller, int may_block) {
    if (caller == CTX_ISR) return K_EISR;                      /* 소유자가 될 태스크가 없다 */
    if (o->holder == caller) return K_EDEADLK;                 /* 자기 자신을 기다리게 된다 */
    if (o->holder == NO_TASK) {
        o->count = 0; o->holder = caller; T[caller].held++;
        return K_OK;
    }
    if (!may_block) return K_EAGAIN;
    enqueue(o, caller);
    pi_on_block(o, caller);
    return K_BLOCKED;
}
static int mutex_give(kobj_t *o, int caller) {
    if (caller == CTX_ISR) return K_EISR;
    if (o->holder != caller) return K_EPERM;                   /* 소유자만 풀 수 있다 */
    T[caller].held--;
    pi_on_release(caller);
    int next = pop_best(o);
    if (next != NO_TASK) { o->holder = next; T[next].held++; }  /* 대기자에게 소유권을 넘긴다 */
    else { o->holder = NO_TASK; o->count = 1; }
    return K_OK;
}

/* ---------------- Part B: 재귀 뮤텍스 = 깊이를 세는 뮤텍스 ---------------- */
static int rmutex_take(kobj_t *o, int caller, int may_block) {
    if (caller == CTX_ISR) return K_EISR;
    if (o->holder == caller) { o->depth++; return K_OK; }      /* 중첩: 깊이만 +1 */
    int r = mutex_take(o, caller, may_block);
    if (r == K_OK) o->depth = 1;
    return r;
}
static int rmutex_give(kobj_t *o, int caller) {
    if (caller == CTX_ISR) return K_EISR;
    if (o->holder != caller) return K_EPERM;
    if (--o->depth > 0) return K_OK;                           /* 아직 바깥 take 가 남았다 */
    int r = mutex_give(o, caller);                             /* 깊이 0: 진짜로 놓는다 */
    if (o->holder != NO_TASK) o->depth = 1;                    /* 넘겨받은 태스크는 깊이 1 */
    return r;
}

/* ---------------- 공용 진입점 ---------------- */
static int k_take(kobj_t *o, int caller, int may_block) {
    switch (o->kind) {
    case OBJ_BINSEM: return sem_take(o, caller, may_block);
    case OBJ_MUTEX:  return mutex_take(o, caller, may_block);
    default:         return rmutex_take(o, caller, may_block);
    }
}
static int k_give(kobj_t *o, int caller) {
    switch (o->kind) {
    case OBJ_BINSEM: return sem_give(o, caller);
    case OBJ_MUTEX:  return mutex_give(o, caller);
    default:         return rmutex_give(o, caller);
    }
}

/* ---------------- 테스트 하네스 ---------------- */
enum { A, B, C, D };
enum { L, M, H };
#define BIT(x) (1u << (x))

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ================= 연습 1: POSIX 뮤텍스 타입 ================= */
static pthread_mutex_t g_pm;
static void *try_from_other(void *arg) {       /* 다른 스레드에서 trylock / unlock 시도 */
    int *res = arg;
    res[0] = pthread_mutex_trylock(&g_pm);
    res[1] = pthread_mutex_unlock(&g_pm);      /* 소유자 아님 */
    return NULL;
}
static int make_mutex(pthread_mutex_t *m, int type) {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, type);
    int r = pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    return r;
}
static void ex1_posix(void) {
    int res[2];
    pthread_t th;
    /* ERRORCHECK: 재잠금 → EDEADLK, 비소유 unlock → EPERM */
    CHECK(make_mutex(&g_pm, PTHREAD_MUTEX_ERRORCHECK) == 0);
    CHECK(pthread_mutex_unlock(&g_pm) == EPERM);          /* 아무도 안 잡았는데 unlock */
    CHECK(pthread_mutex_lock(&g_pm) == 0);
    CHECK(pthread_mutex_lock(&g_pm) == EDEADLK);          /* 같은 스레드 재잠금 */
    pthread_create(&th, NULL, try_from_other, res);
    pthread_join(th, NULL);
    CHECK(res[0] == EBUSY && res[1] == EPERM);            /* 남의 스레드: 못 잡고, 못 푼다 */
    CHECK(pthread_mutex_unlock(&g_pm) == 0);
    pthread_mutex_destroy(&g_pm);
    /* RECURSIVE: 3번 잠그고 3번 풀어야 남이 잡을 수 있다 */
    CHECK(make_mutex(&g_pm, PTHREAD_MUTEX_RECURSIVE) == 0);
    CHECK(pthread_mutex_lock(&g_pm) == 0 && pthread_mutex_lock(&g_pm) == 0 && pthread_mutex_lock(&g_pm) == 0);
    CHECK(pthread_mutex_unlock(&g_pm) == 0 && pthread_mutex_unlock(&g_pm) == 0);
    pthread_create(&th, NULL, try_from_other, res);
    pthread_join(th, NULL);
    CHECK(res[0] == EBUSY && res[1] == EPERM);            /* 깊이 1 남음 */
    CHECK(pthread_mutex_unlock(&g_pm) == 0);
    CHECK(pthread_mutex_unlock(&g_pm) == EPERM);          /* 초과 unlock */
    pthread_mutex_destroy(&g_pm);
}

/* ================= 연습 2: 소유권 검사 바이너리 세마포어 ================= */
typedef struct { kobj_t sem; int owner; } olock_t;
static void olock_init(olock_t *l) { obj_init(&l->sem, OBJ_BINSEM, 1); l->owner = NO_TASK; }
static int olock_take(olock_t *l, int caller, int may_block) {
    if (caller == CTX_ISR) return K_EISR;
    if (l->owner == caller) return K_EDEADLK;
    int r = sem_take(&l->sem, caller, may_block);
    if (r == K_OK) l->owner = caller;
    return r;                                   /* K_BLOCKED: 넘겨받을 때 owner 를 바꿔야 함 */
}
static int olock_give(olock_t *l, int caller) {
    if (l->owner != caller) return K_EPERM;     /* 소유권 검사 — 여기까지는 뮤텍스와 같다 */
    int next = pop_best(&l->sem);
    if (next != NO_TASK) { l->owner = next; return K_OK; }
    l->owner = NO_TASK;
    l->sem.count = 1;
    return K_OK;
}
static void ex2_olock(void) {
    olock_t lk;
    task_init(L, "L", 1); task_init(M, "M", 2); task_init(H, "H", 3);
    olock_init(&lk);
    CHECK(olock_take(&lk, L, 1) == K_OK && lk.owner == L);
    CHECK(olock_give(&lk, M) == K_EPERM);       /* 남이 못 푼다 */
    CHECK(olock_take(&lk, CTX_ISR, 0) == K_EISR);
    CHECK(olock_take(&lk, H, 1) == K_BLOCKED);
    CHECK(T[L].prio == 1);                      /* 그러나 상속은 없다 → M 이 L 을 선점 */
    CHECK(pick_ready(BIT(L) | BIT(M) | BIT(H)) == M);
    CHECK(olock_give(&lk, L) == K_OK && lk.owner == H && T[H].blocked_on == NULL);
    CHECK(olock_give(&lk, H) == K_OK && lk.owner == NO_TASK && lk.sem.count == 1);
}

/* ================= 연습 3: ISR → 세마포어 → 태스크 → 뮤텍스 ================= */
static char g_ring[16]; static unsigned g_rh, g_rt;     /* ISR 이 rh 를, 태스크가 rt 를 올린다 */
static char g_log[32]; static int g_loglen;
static kobj_t g_rxsem, g_logmx;
static int g_isr_mutex_err;
static void uart_rx_isr(char c) {                      /* ISR: 블록 금지, 뮤텍스 금지 */
    if (k_take(&g_logmx, CTX_ISR, 0) == K_EISR) g_isr_mutex_err++;   /* 하면 안 되는 시도 (항상 거부) */
    g_ring[g_rh++ % sizeof g_ring] = c;
    (void)k_give(&g_rxsem, CTX_ISR);                   /* K_EFULL 이어도 괜찮다: 태스크가 링을 다 비운다 */
}
static int parser_task_step(int self) {                /* 깨어나면 링을 전부 비우고 로그에 쓴다 */
    if (k_take(&g_rxsem, self, 0) != K_OK) return 0;
    if (k_take(&g_logmx, self, 1) != K_OK) return -1;
    int n = 0;
    while (g_rt != g_rh && g_loglen + 1 < (int)sizeof g_log) { g_log[g_loglen++] = g_ring[g_rt++ % sizeof g_ring]; n++; }
    g_log[g_loglen] = '\0';
    (void)k_give(&g_logmx, self);
    return n;
}
static void ex3_deferred(void) {
    task_init(M, "parser", 2);
    obj_init(&g_rxsem, OBJ_BINSEM, 0); obj_init(&g_logmx, OBJ_MUTEX, 0);
    uart_rx_isr('o'); uart_rx_isr('k'); uart_rx_isr('!');   /* 신호 3번 → 세마포어는 1 (합쳐짐) */
    CHECK(g_rxsem.count == 1);
    CHECK(parser_task_step(M) == 3 && strcmp(g_log, "ok!") == 0);   /* 그래도 데이터는 링에 다 있다 */
    CHECK(parser_task_step(M) == 0);                    /* 신호 없음 */
    CHECK(g_isr_mutex_err == 3 && g_logmx.holder == NO_TASK);
}

/* ================= 연습 4: 재귀 뮤텍스 + 에러 경로 ================= */
static kobj_t g_i2c;                                   /* I2C 드라이버 재귀 뮤텍스 */
static int g_max_depth;
static void note_depth(void) { if (g_i2c.depth > g_max_depth) g_max_depth = g_i2c.depth; }
/* 공개 API 들도 각자 락을 잡는다 → rmw 안에서 부르면 중첩 */
static int i2c_read(int self, int fail)  { k_take(&g_i2c, self, 1); note_depth(); k_give(&g_i2c, self); return fail ? -1 : 0; }
static int i2c_write(int self, int fail) { k_take(&g_i2c, self, 1); note_depth(); k_give(&g_i2c, self); return fail ? -1 : 0; }
static int i2c_rmw(int self, int fail_at) {            /* 정답: 모든 경로가 out 을 지난다 */
    int r = 0;
    k_take(&g_i2c, self, 1);
    if (i2c_read(self, fail_at == 1) != 0) { r = -1; goto out; }
    if (i2c_write(self, fail_at == 2) != 0) { r = -2; goto out; }
out:
    k_give(&g_i2c, self);
    return r;
}
static int i2c_rmw_buggy(int self, int fail_at) {      /* 버그: 에러에서 give 없이 return */
    k_take(&g_i2c, self, 1);
    if (i2c_read(self, fail_at == 1) != 0) return -1;
    if (i2c_write(self, fail_at == 2) != 0) return -2;
    k_give(&g_i2c, self);
    return 0;
}
static void ex4_balance(void) {
    task_init(A, "A", 1); task_init(B, "B", 2);
    for (int f = 0; f <= 2; f++) {
        obj_init(&g_i2c, OBJ_RMUTEX, 0);
        CHECK(i2c_rmw(A, f) == -f);
        CHECK(g_i2c.holder == NO_TASK && g_i2c.depth == 0 && T[A].held == 0);
    }
    CHECK(g_max_depth == 2);                           /* rmw(1) 안의 read/write(2) — 일반 뮤텍스였다면 여기서 EDEADLK */
    obj_init(&g_i2c, OBJ_RMUTEX, 0);
    CHECK(i2c_rmw_buggy(A, 1) == -1);
    CHECK(g_i2c.holder == A && g_i2c.depth == 1);      /* 깊이 1 이 샌다 */
    CHECK(k_take(&g_i2c, B, 0) == K_EAGAIN);           /* 다른 태스크는 영원히 못 잡는다 */
}

int main(void) {
    ex1_posix();
    ex2_olock();
    ex3_deferred();
    ex4_balance();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
