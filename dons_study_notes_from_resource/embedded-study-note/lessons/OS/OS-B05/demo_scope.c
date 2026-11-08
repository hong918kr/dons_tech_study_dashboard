/* OS-B05 demo_scope.c — 락 범위(scope) 버그: "잠근 읽기 + 잠근 쓰기" 는 "잠근 검사-후-갱신" 이 아니다.
 *
 * try_add_broken(): mc_get() 으로 검사하고 mc_add() 로 더한다. 각 접근은 락 안이라 데이터 레이스는 없다.
 * 하지만 두 임계 구역 사이의 틈에 다른 스레드가 끼어들면 한도(limit)를 넘는다 = 레이스 컨디션.
 *
 *   ① 강제 스케줄 (항상 재현): 스레드 A 가 검사 직후 멈추고 B 를 끝까지 돌린 뒤 이어서 더한다.
 *      같은 강제 지점을 올바른 버전(임계 구역 하나)에 넣으면 B 는 락에 막혀 끼어들지 못한다.
 *   ② 자유 실행: 4 스레드 × 100000 번 시도, 한도 250000 — 실제로 몇 번 넘는지 센다.
 *
 * build: cc -std=c11 -Wall -Wextra -O2 -pthread demo_scope.c -o ds && ./ds
 *        cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_scope.c -o ds_t && ./ds_t   # TSan: 보고 없음
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

typedef struct { pthread_mutex_t lock; long value; } mcounter_t;
static void mc_add(mcounter_t *c, long n) { pthread_mutex_lock(&c->lock); c->value += n; pthread_mutex_unlock(&c->lock); }
static long mc_get(mcounter_t *c) { pthread_mutex_lock(&c->lock); long v = c->value; pthread_mutex_unlock(&c->lock); return v; }

typedef void (*hook_fn)(void);

/* 틀린 범위: 검사와 갱신이 서로 다른 임계 구역 */
static bool try_add_broken(mcounter_t *c, long n, long limit, hook_fn hook) {
    long v = mc_get(c);                    /* 임계 구역 1: 검사 */
    if (v + n > limit) return false;
    if (hook) hook();                      /* ← 여기가 틈. 강제 스케줄은 이 지점에서 B 를 돌린다 */
    mc_add(c, n);                          /* 임계 구역 2: 갱신 (검사 결과는 이미 낡았을 수 있다) */
    return true;
}
/* 올바른 범위: 검사 + 갱신이 한 임계 구역 (solution.c 의 mc_try_add + 같은 강제 지점) */
static bool try_add_ok(mcounter_t *c, long n, long limit, hook_fn hook) {
    pthread_mutex_lock(&c->lock);
    bool ok = c->value + n <= limit;
    if (ok && hook) hook();                /* 락을 쥔 채 B 를 기다려 본다 (B 는 들어오지 못한다) */
    if (ok) c->value += n;
    pthread_mutex_unlock(&c->lock);
    return ok;
}

/* ---- ① 강제 스케줄: A 가 hook 에서 B 를 깨우고, B 가 끝나기를 최대 200 ms 기다린다 ---- */
static pthread_mutex_t sm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sc = PTHREAD_COND_INITIALIZER;
static int b_may_run, b_done;
static bool b_result;
static mcounter_t g_c;
static bool (*g_fn)(mcounter_t *, long, long, hook_fn);

static void hook_run_b(void) {
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_nsec += 200 * 1000000L;
    if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&sm);
    b_may_run = 1;
    pthread_cond_broadcast(&sc);
    int rc = 0;
    while (!b_done && rc != ETIMEDOUT) rc = pthread_cond_timedwait(&sc, &sm, &dl);
    printf("    A: B %s\n", b_done ? "finished while A was between check and add" : "could not finish within 200 ms (blocked on the lock)");
    pthread_mutex_unlock(&sm);
}
static void *thread_b(void *arg) {
    (void)arg;
    pthread_mutex_lock(&sm);
    while (!b_may_run) pthread_cond_wait(&sc, &sm);
    pthread_mutex_unlock(&sm);
    bool r = g_fn(&g_c, 2, 10, NULL);      /* B 는 강제 지점 없이 끝까지 */
    pthread_mutex_lock(&sm);
    b_result = r;
    b_done = 1;
    pthread_cond_broadcast(&sc);
    pthread_mutex_unlock(&sm);
    return NULL;
}
static void forced(const char *name, bool (*fn)(mcounter_t *, long, long, hook_fn)) {
    pthread_mutex_init(&g_c.lock, NULL);
    g_c.value = 8;                          /* 한도 10, 지금 8: 2 를 더할 수 있는 건 한 번뿐 */
    b_may_run = b_done = 0;
    g_fn = fn;
    pthread_t b;
    pthread_create(&b, NULL, thread_b, NULL);
    printf("[forced] %s: value=8 limit=10, A and B both try +2\n", name);
    bool a = fn(&g_c, 2, 10, hook_run_b);
    pthread_join(b, NULL);
    printf("    A -> %s, B -> %s, final value=%ld %s\n", a ? "true" : "false", b_result ? "true" : "false",
           g_c.value, g_c.value > 10 ? "(OVER LIMIT)" : "(ok)");
    pthread_mutex_destroy(&g_c.lock);
}

/* ---- ② 자유 실행 ---- */
#define NT 4
#define ITERS 100000
#define LIMIT 250000L
static long ok_cnt[NT];
static void *free_worker(void *arg) {
    long *ok = arg;
    for (int i = 0; i < ITERS; i++) *ok += g_fn(&g_c, 1, LIMIT, NULL);
    return NULL;
}
static void free_run(const char *name, bool (*fn)(mcounter_t *, long, long, hook_fn), int runs) {
    for (int r = 0; r < runs; r++) {
        pthread_mutex_init(&g_c.lock, NULL);
        g_c.value = 0;
        g_fn = fn;
        pthread_t th[NT];
        for (int i = 0; i < NT; i++) { ok_cnt[i] = 0; pthread_create(&th[i], NULL, free_worker, &ok_cnt[i]); }
        long ok = 0;
        for (int i = 0; i < NT; i++) { pthread_join(th[i], NULL); ok += ok_cnt[i]; }
        printf("[free] %-6s run %d: final=%ld (limit %ld) successes=%ld over=%ld\n",
               name, r + 1, g_c.value, LIMIT, ok, g_c.value - LIMIT);
        pthread_mutex_destroy(&g_c.lock);
    }
}

int main(void) {
    forced("broken", try_add_broken);
    forced("ok    ", try_add_ok);
    free_run("broken", try_add_broken, 5);
    free_run("ok", try_add_ok, 2);
    return 0;
}
