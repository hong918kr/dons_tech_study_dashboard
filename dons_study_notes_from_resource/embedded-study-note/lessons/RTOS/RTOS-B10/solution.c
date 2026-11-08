/* RTOS-B10 뮤텍스 vs 세마포어: 소유권과 재귀 뮤텍스
 *
 * 호스트 모델: RTOS 동기화 객체 3종(바이너리 세마포어 / 뮤텍스 / 재귀 뮤텍스)의 "의미" 만 떼어 낸
 * 단일 스레드 결정적(deterministic) 모델. 진짜 문맥 전환은 없다:
 *   - 호출자 = 태스크 번호(0..NTASK-1) 또는 CTX_ISR (ISR 에는 태스크 신원이 없다)
 *   - "블록" = 대기 리스트에 넣고 K_BLOCKED 반환. 나중에 give 가 그 태스크에게 직접 넘겨준다(hand-off)
 *   - 우선순위: 숫자가 클수록 높다 (FreeRTOS 와 같음, Cortex-M NVIC 와는 반대)
 * 실제 FreeRTOS 동작은 demo_freertos_*.c 로 확인한다 (이 파일은 커널 없이 빌드되는 모델).
 */
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
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

enum { A, B, C, D };                   /* Part A/B 태스크 */
enum { L, M, H };                      /* Part C 태스크: 센서(L) · 로거(M) · 텔레메트리(H) */
#define BIT(x) (1u << (x))

static void reset_tasks(void) {
    task_init(A, "A", 1); task_init(B, "B", 2); task_init(C, "C", 3); task_init(D, "D", 2);
}

int main(void) {
    kobj_t s, mx, rm, bus, spi, rx;

    /* ---- 기준: 바이너리 세마포어 (주어진 코드, 처음부터 PASS) ---- */
    reset_tasks();
    obj_init(&s, OBJ_BINSEM, 0);
    CHECK(k_take(&s, A, 0) == K_EAGAIN);         /* 처음엔 비어 있다 (xSemaphoreCreateBinary) */
    CHECK(k_give(&s, CTX_ISR) == K_OK);          /* ISR 이 give 해도 된다 */
    CHECK(k_give(&s, CTX_ISR) == K_EFULL);       /* 최대 1: 두 번째 신호는 합쳐진다 (RTOS-B08) */
    CHECK(k_take(&s, A, 0) == K_OK);
    CHECK(k_give(&s, B) == K_OK);                /* take 하지 않은 B 가 give 해도 통과 — 소유자 없음 */
    CHECK(k_take(&s, C, 0) == K_OK);

    /* ---- Part A: 뮤텍스 소유권 ---- */
    reset_tasks();
    obj_init(&mx, OBJ_MUTEX, 0);
    CHECK(k_take(&mx, A, 0) == K_OK);            /* 뮤텍스는 "비어 있음" 으로 시작 */
    CHECK(mx.holder == A && T[A].held == 1);
    CHECK(k_take(&mx, B, 0) == K_EAGAIN);
    CHECK(k_give(&mx, B) == K_EPERM);            /* 비소유자 give 거부 */
    CHECK(mx.holder == A && mx.count == 0);      /* 상태가 그대로여야 한다 */
    CHECK(k_take(&mx, CTX_ISR, 0) == K_EISR);    /* ISR 은 소유자가 될 수 없다 */
    CHECK(k_give(&mx, CTX_ISR) == K_EISR);
    CHECK(k_take(&mx, A, 1) == K_EDEADLK);       /* 일반 뮤텍스 재진입 = 자기 자신과 교착 */
    CHECK(k_give(&mx, A) == K_OK);
    CHECK(mx.holder == NO_TASK && mx.count == 1 && T[A].held == 0);
    CHECK(k_give(&mx, A) == K_EPERM);            /* 두 번 give: 이미 소유자가 아니다 */
    /* hand-off: 대기자 중 우선순위 최고에게, 같으면 먼저 온 쪽 */
    CHECK(k_take(&mx, A, 1) == K_OK);
    CHECK(k_take(&mx, B, 1) == K_BLOCKED);       /* B prio 2 */
    CHECK(k_take(&mx, D, 1) == K_BLOCKED);       /* D prio 2, B 보다 늦게 */
    CHECK(k_take(&mx, C, 1) == K_BLOCKED);       /* C prio 3 */
    CHECK(k_give(&mx, A) == K_OK && mx.holder == C && T[C].blocked_on == NULL && T[C].held == 1);
    CHECK(k_give(&mx, C) == K_OK && mx.holder == B);
    CHECK(k_give(&mx, B) == K_OK && mx.holder == D && mx.nwait == 0);
    CHECK(k_give(&mx, D) == K_OK && mx.holder == NO_TASK);

    /* ---- Part B: 재귀 뮤텍스 ---- */
    reset_tasks();
    obj_init(&rm, OBJ_RMUTEX, 0);
    CHECK(k_take(&rm, A, 0) == K_OK && k_take(&rm, A, 0) == K_OK && k_take(&rm, A, 0) == K_OK);
    CHECK(rm.holder == A && rm.depth == 3);
    CHECK(T[A].held == 1);                       /* 중첩해도 "뮤텍스 1개 보유" */
    CHECK(k_take(&rm, B, 0) == K_EAGAIN);
    CHECK(k_give(&rm, B) == K_EPERM);            /* 비소유자는 깊이를 못 줄인다 */
    CHECK(k_give(&rm, A) == K_OK && rm.depth == 2 && rm.holder == A);
    CHECK(k_give(&rm, A) == K_OK && rm.depth == 1 && rm.holder == A);
    CHECK(k_take(&rm, B, 1) == K_BLOCKED);       /* 깊이 1 이 남아 있으면 아직 잠김 */
    CHECK(k_give(&rm, A) == K_OK && rm.holder == B && rm.depth == 1);   /* 깊이 0 → B 에게 넘김 */
    CHECK(T[A].held == 0 && T[B].held == 1);
    CHECK(k_give(&rm, A) == K_EPERM);            /* 초과 give */
    CHECK(k_take(&rm, CTX_ISR, 0) == K_EISR);
    CHECK(k_take(&rm, B, 0) == K_OK && rm.depth == 2);
    CHECK(k_give(&rm, B) == K_OK && k_give(&rm, B) == K_OK && rm.holder == NO_TASK && rm.depth == 0);

    /* ---- Part C: I2C 버스 락 — 센서 L(1) · 로거 M(2) · 텔레메트리 H(3) ---- */
    /* (1) 뮤텍스: H 가 기다리면 L 이 H 의 우선순위를 물려받아 M 이 끼어들지 못한다 */
    task_init(L, "L", 1); task_init(M, "M", 2); task_init(H, "H", 3);
    obj_init(&bus, OBJ_MUTEX, 0);
    CHECK(k_take(&bus, L, 1) == K_OK);
    CHECK(pick_ready(BIT(L) | BIT(M)) == M);     /* 아직 H 대기 전: M 이 L 보다 높다 (정상) */
    CHECK(k_take(&bus, H, 1) == K_BLOCKED);
    CHECK(T[L].prio == 3);                       /* 상속 */
    CHECK(pick_ready(BIT(L) | BIT(M) | BIT(H)) == L);   /* M 이 L 을 선점 못 함 → H 대기가 짧다 */
    CHECK(k_give(&bus, M) == K_EPERM && bus.holder == L);   /* 로거의 잘못된 정리 코드는 거부 */
    CHECK(k_give(&bus, L) == K_OK && T[L].prio == 1 && bus.holder == H);
    CHECK(pick_ready(BIT(L) | BIT(M) | BIT(H)) == H);
    /* (2) 같은 시나리오를 바이너리 세마포어 락으로 */
    task_init(L, "L", 1); task_init(M, "M", 2); task_init(H, "H", 3);
    obj_init(&bus, OBJ_BINSEM, 1);
    CHECK(k_take(&bus, L, 1) == K_OK);
    CHECK(k_take(&bus, H, 1) == K_BLOCKED);
    CHECK(T[L].prio == 1);                       /* 세마포어엔 소유자가 없으니 올릴 대상도 없다 */
    CHECK(pick_ready(BIT(L) | BIT(M) | BIT(H)) == M);   /* 우선순위 역전: M 이 먼저 돈다 (RTOS-M03) */
    CHECK(k_give(&bus, M) == K_OK);              /* 남이 풀어도 통과 → H 가 들어감, L 은 아직 안 */
    CHECK(T[H].blocked_on == NULL);              /* → L 과 H 가 동시에 버스 사용 (상호배제 깨짐) */
    /* (3) 뮤텍스 2개 보유: FreeRTOS 는 "모두 놓을 때" 기본 우선순위로 복원 */
    task_init(L, "L", 1); task_init(M, "M", 2); task_init(H, "H", 3);
    obj_init(&bus, OBJ_MUTEX, 0); obj_init(&spi, OBJ_MUTEX, 0);
    CHECK(k_take(&bus, L, 1) == K_OK && k_take(&spi, L, 1) == K_OK && T[L].held == 2);
    CHECK(k_take(&bus, H, 1) == K_BLOCKED && T[L].prio == 3);
    CHECK(k_give(&bus, L) == K_OK && bus.holder == H);
    CHECK(T[L].prio == 3);                       /* spi 를 아직 쥐고 있어 복원 보류 (보수적 규칙, RTOS-M04) */
    CHECK(k_give(&spi, L) == K_OK && T[L].prio == 1);
    /* (4) UART RX ISR: 버스 뮤텍스는 못 쓰고, 세마포어로 태스크를 깨운다 */
    obj_init(&rx, OBJ_BINSEM, 0);
    CHECK(k_take(&bus, CTX_ISR, 0) == K_EISR);
    CHECK(k_give(&rx, CTX_ISR) == K_OK && k_take(&rx, M, 0) == K_OK);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
