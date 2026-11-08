/* TOOL-M09 추가 연습 — 한 번에 검증
 *   연습 1  지연 초기화(lazy init): "if (!table) init();" 레이스 → pthread_once
 *   연습 2  순서를 정할 수 없을 때: trylock + 물러서기(back-off) 로 교착 피하기
 *   연습 3  busy-wait 대신 잠드는 정지 이벤트: mutex + cond + timedwait
 *   연습 4  join 은 동기화다: 스레드별 결과 칸은 atomic 없이도 레이스가 아니다
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 * TSan : cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_t && ./pr_t   (보고 0개)
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define NT 8

/* ---------------- 연습 1 — pthread_once 로 지연 초기화 ---------------- */
static uint16_t g_crc_table[256];
static atomic_int g_init_calls;                 /* 초기화 함수가 몇 번 돌았나 */
static pthread_once_t g_crc_once = PTHREAD_ONCE_INIT;

static void crc_table_init(void) {              /* CRC-16/CCITT 테이블 (0x1021) */
    atomic_fetch_add(&g_init_calls, 1);
    for (unsigned i = 0; i < 256; i++) {
        uint16_t c = (uint16_t)(i << 8);
        for (int k = 0; k < 8; k++) c = (uint16_t)((c & 0x8000u) ? (c << 1) ^ 0x1021u : c << 1);
        g_crc_table[i] = c;
    }
}
static uint16_t crc16(const uint8_t *p, size_t n) {
    pthread_once(&g_crc_once, crc_table_init);  /* 처음 부른 스레드만 init, 나머지는 끝날 때까지 기다림 */
    uint16_t c = 0xFFFFu;
    for (size_t i = 0; i < n; i++) c = (uint16_t)((c << 8) ^ g_crc_table[(c >> 8) ^ p[i]]);
    return c;
}
static void *crc_worker(void *arg) {
    uint16_t *out = arg;
    static const uint8_t msg[] = "123456789";
    *out = crc16(msg, 9);
    return NULL;
}

/* ---------------- 연습 2 — trylock back-off ---------------- */
typedef struct { pthread_mutex_t m; long credits; } acct_t;
static acct_t g_a = { PTHREAD_MUTEX_INITIALIZER, 500 }, g_b = { PTHREAD_MUTEX_INITIALIZER, 500 };
static atomic_ulong g_backoffs;

/* 첫 번째 락은 기다려서 잡고, 두 번째는 trylock. 실패하면 첫 번째를 놓고 다시 시도. */
static void move_backoff(acct_t *from, acct_t *to, long n) {
    for (;;) {
        pthread_mutex_lock(&from->m);
        if (pthread_mutex_trylock(&to->m) == 0) break;
        pthread_mutex_unlock(&from->m);         /* 쥔 걸 놓는다 → 순환 대기 조건이 깨진다 */
        atomic_fetch_add_explicit(&g_backoffs, 1u, memory_order_relaxed);
        sched_yield();
    }
    from->credits -= n;
    to->credits += n;
    pthread_mutex_unlock(&to->m);
    pthread_mutex_unlock(&from->m);
}
#define MOVES 50000
static void *a_to_b(void *x) { (void)x; for (int i = 0; i < MOVES; i++) move_backoff(&g_a, &g_b, 1); return NULL; }
static void *b_to_a(void *x) { (void)x; for (int i = 0; i < MOVES; i++) move_backoff(&g_b, &g_a, 1); return NULL; }

/* ---------------- 연습 3 — 잠드는 정지 이벤트 ---------------- */
typedef struct { pthread_mutex_t m; pthread_cond_t cv; bool set; } event_t;
#define EVENT_INIT { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, false }

static void event_set(event_t *e) {
    pthread_mutex_lock(&e->m);
    e->set = true;                              /* 조건 변수의 "조건" 은 락 안에서 바꾼다 */
    pthread_cond_broadcast(&e->cv);             /* 기다리는 스레드 모두 깨우기 */
    pthread_mutex_unlock(&e->m);
}
/* set 될 때까지 최대 timeout_ms 동안 잠든다. set 이면 true. */
static bool event_wait(event_t *e, unsigned timeout_ms) {
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);         /* macOS 에는 pthread_condattr_setclock 이 없다 → REALTIME */
    dl.tv_sec += (time_t)(timeout_ms / 1000u);
    dl.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&e->m);
    int rc = 0;
    while (!e->set && rc != ETIMEDOUT)          /* while: 가짜 깨어남(spurious wakeup) 대비 */
        rc = pthread_cond_timedwait(&e->cv, &e->m, &dl);
    bool r = e->set;
    pthread_mutex_unlock(&e->m);
    return r;
}
static event_t g_stop_ev = EVENT_INIT;
static atomic_int g_woken;
static void *idle_task(void *x) {
    (void)x;
    if (event_wait(&g_stop_ev, 5000u)) atomic_fetch_add(&g_woken, 1);
    return NULL;
}

/* ---------------- 연습 4 — 스레드별 결과 칸 + join ---------------- */
typedef struct { uint32_t seed; uint64_t sum; char pad[48]; } slot_t;   /* 칸마다 64 B: false sharing 줄이기 */
static slot_t g_slots[NT];
static void *sum_task(void *arg) {
    slot_t *s = arg;
    uint64_t acc = 0;
    uint32_t x = s->seed;
    for (int i = 0; i < 100000; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; acc += x & 0xFFFu; }
    s->sum = acc;                               /* 내 칸에만 쓴다. main 은 join 뒤에 읽는다 */
    return NULL;
}
static uint64_t sum_single(uint32_t x) {
    uint64_t acc = 0;
    for (int i = 0; i < 100000; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; acc += x & 0xFFFu; }
    return acc;
}

int main(void) {
    /* 연습 1 */
    uint16_t crcs[NT];
    pthread_t t[NT];
    for (int i = 0; i < NT; i++) pthread_create(&t[i], NULL, crc_worker, &crcs[i]);
    for (int i = 0; i < NT; i++) pthread_join(t[i], NULL);
    int same = 0;
    for (int i = 0; i < NT; i++) same += crcs[i] == 0x29B1u;      /* CRC-16/CCITT-FALSE("123456789") */
    CHECK(same == NT);
    CHECK(atomic_load(&g_init_calls) == 1);

    /* 연습 2 */
    pthread_t x, y;
    pthread_create(&x, NULL, a_to_b, NULL);
    pthread_create(&y, NULL, b_to_a, NULL);
    pthread_join(x, NULL);
    pthread_join(y, NULL);
    CHECK(g_a.credits == 500 && g_b.credits == 500);
    printf("  [2] back-offs: %lu\n", (unsigned long)atomic_load(&g_backoffs));

    /* 연습 3 */
    event_t quick = EVENT_INIT;
    CHECK(!event_wait(&quick, 30u));                              /* 아무도 set 안 하면 타임아웃 */
    pthread_t idle[4];
    for (int i = 0; i < 4; i++) pthread_create(&idle[i], NULL, idle_task, NULL);
    event_set(&g_stop_ev);
    for (int i = 0; i < 4; i++) pthread_join(idle[i], NULL);
    CHECK(atomic_load(&g_woken) == 4);                            /* broadcast 로 모두 깨어남 */
    CHECK(event_wait(&g_stop_ev, 0u));                            /* 이미 set 이면 바로 true */

    /* 연습 4 */
    for (int i = 0; i < NT; i++) { g_slots[i].seed = 0x9E3779B9u + (uint32_t)i; pthread_create(&t[i], NULL, sum_task, &g_slots[i]); }
    for (int i = 0; i < NT; i++) pthread_join(t[i], NULL);
    int ok = 0;
    for (int i = 0; i < NT; i++) ok += g_slots[i].sum == sum_single(0x9E3779B9u + (uint32_t)i);
    CHECK(ok == NT);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
