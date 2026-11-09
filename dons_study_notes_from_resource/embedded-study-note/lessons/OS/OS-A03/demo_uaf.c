/* OS-A03 demo_uaf.c — "교체하자마자 free" 의 use-after-free 를 강제 스케줄로 재현
 *
 * solution.c 의 설정 DB 를 그대로 쓰고 (main 만 이름을 바꿔 포함), writer 만 두 가지로 바꾼다:
 *   MODE=0  cfg_update_now(): 포인터 교체 직후 옛 버전을 바로 free   ← 버그
 *   MODE=1  solution 의 cfg_update(): EBR 로 retire, grace period 뒤 free
 * 스케줄 (뮤텍스+조건 변수로 강제 — 매번 같은 순서):
 *   ① 리더: cfg_read_begin → 포인터 p (v1) 를 얻음
 *   ② writer: 새 버전 v2 게시 (MODE 에 따라 v1 free) + 다른 할당 하나 (해제된 블록 재사용 유도)
 *   ③ 리더: p->gain[], p->version, cfg_ok(p) 를 읽음  ← MODE=0 이면 해제된 메모리
 *
 * build:
 *   cc -std=c11 -O2 -pthread -DMODE=0 demo_uaf.c -o uaf0 && ./uaf0      # 쓰레기 / 다른 버전 내용이 보임
 *   cc -std=c11 -g -O1 -fsanitize=address -fno-omit-frame-pointer -pthread -DMODE=0 demo_uaf.c -o uaf0a && ./uaf0a
 *   cc -std=c11 -g -O1 -fsanitize=address -fno-omit-frame-pointer -pthread -DMODE=1 demo_uaf.c -o uaf1a && ./uaf1a
 */
#define main os_a03_tests
#include "solution.c"
#undef main

#ifndef MODE
#define MODE 0
#endif

static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static int g_step;
static void set_step(int s) { pthread_mutex_lock(&g_m); g_step = s; pthread_cond_broadcast(&g_cv); pthread_mutex_unlock(&g_m); }
static void wait_step(int s) { pthread_mutex_lock(&g_m); while (g_step < s) pthread_cond_wait(&g_cv, &g_m); pthread_mutex_unlock(&g_m); }

static cfgdb_t g_demo;

#if MODE == 0
/* 버그 버전: 교체 → 즉시 free (RCU 없이 "포인터만 원자적으로 바꾸면 되겠지") */
static void cfg_update_now(cfgdb_t *db, uint64_t v) {
    cfg_t *n = cfg_new(v);
    cfg_t *old = atomic_exchange_explicit(&db->cur, n, memory_order_seq_cst);
    cfg_free(old);                       /* 리더가 아직 old 를 쥐고 있을 수 있다 */
}
#endif

static void *reader(void *arg) {
    (void)arg;
    const cfg_t *p = cfg_read_begin(&g_demo, 0);          /* ① */
    printf("reader: got v%llu (gain[0]=%d)\n", (unsigned long long)p->version, p->gain[0]);
    set_step(1);
    wait_step(2);                                          /* writer 가 ② 를 끝낼 때까지 */
    uint64_t v = p->version;                               /* ③ */
    int32_t g0 = p->gain[0];
    bool ok = cfg_ok(p);
    printf("reader: after update, same pointer says v%llu gain[0]=%d checksum %s\n",
           (unsigned long long)v, g0, ok ? "OK" : "BROKEN");
    cfg_read_end(&g_demo, 0);
    set_step(3);
    return NULL;
}

int main(void) {
    cfgdb_init(&g_demo, 1);
    pthread_t th;
    pthread_create(&th, NULL, reader, NULL);
    wait_step(1);
#if MODE == 0
    cfg_update_now(&g_demo, 2);                            /* ② */
    printf("writer: published v2, freed v1 immediately\n");
#else
    cfg_update(&g_demo, 2);
    printf("writer: published v2, v1 retired (limbo=%d, freed so far=%ld)\n", g_demo.ebr.nlimbo, g_nfreed);
#endif
    cfg_t *other = cfg_new(77);                            /* 같은 크기 할당: 해제된 블록을 재사용하기 쉽다 */
    set_step(2);
    wait_step(3);
#if MODE == 1
    cfg_synchronize(&g_demo, 1000);
    printf("writer: after reader left + synchronize: limbo=%d, freed=%ld (v1 freed: %s)\n",
           g_demo.ebr.nlimbo, g_nfreed, was_freed(1) ? "yes" : "no");
#endif
    pthread_join(th, NULL);
    cfg_free(other);
    cfgdb_destroy(&g_demo);
    return 0;
}
