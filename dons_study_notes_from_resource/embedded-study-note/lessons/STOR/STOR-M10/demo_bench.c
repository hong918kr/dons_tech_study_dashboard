/* STOR-M10 demo_bench — 세 가지 래치 방식의 처리량 (Mops/s)
 *
 *   coarse : 트리 전체에 pthread_rwlock_t 하나 (lookup = R, insert = W). 노드 래치는 끈다(g_latch_off).
 *   crab   : lookup = R crabbing, insert = pessimistic W crabbing (bt_insert_pess)
 *   opt    : lookup = R crabbing, insert = optimistic (bt_insert_opt, 리프만 W, 꽉 차면 재시작)
 *   nolatch: (read-only 에서만) 래치를 하나도 안 잡는 기준선 — 트리 순회 자체가 스레드 수에 따라 늘어나는지 확인용
 *
 * 부하: 미리 200000 키(짝수)를 넣은 트리에서, 스레드마다 xorshift 로
 *   read-only = lookup 100%,  read-heavy = lookup 95% / insert 5%,  write-heavy = lookup 20% / insert 80%
 * 칸 = 150 ms 동안 모든 스레드가 끝낸 연산 수 / 시간. 5 회 중 중앙값. 칸마다 트리를 새로 만든다.
 * 한 번에 30 초를 넘지 않도록 부하별로 따로 돌린다: ./bench ro ; ./bench rh ; ./bench wh
 * 숫자는 이 맥의 다른 작업(다른 에이전트의 벤치마크)과 같이 돈 값이다 → 비율만 믿을 것.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o bench && ./bench ro
 */
#define main stor_m10_tests
#include "solution.c"
#undef main

static pthread_rwlock_t g_big = PTHREAD_RWLOCK_INITIALIZER;
static atomic_int g_stop;
typedef struct { bt_t *t; int mech, read_pct; uint32_t seed; long ops; } b_arg_t;

static void *bench_worker(void *p) {
    b_arg_t *a = p;
    uint32_t s = a->seed;
    long ops = 0;
    int v;
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        for (int i = 0; i < 64; i++) {
            uint32_t r = xs32(&s);
            bool rd = (int)(r % 100u) < a->read_pct;
            int key = (int)(xs32(&s) & 0x7fffffffu);
            if (rd) key = (int)(xs32(&s) % 400000u);
            if (a->mech == 3) bt_lookup(a->t, key, &v);       /* 기준선: 래치 0 개 (읽기 전용일 때만 안전) */
            else if (a->mech == 0) {
                if (rd) { pthread_rwlock_rdlock(&g_big); bt_lookup(a->t, key, &v); pthread_rwlock_unlock(&g_big); }
                else    { pthread_rwlock_wrlock(&g_big); bt_insert_pess(a->t, key, key); pthread_rwlock_unlock(&g_big); }
            } else if (rd) bt_lookup(a->t, key, &v);
            else if (a->mech == 1) bt_insert_pess(a->t, key, key);
            else bt_insert_opt(a->t, key, key);
        }
        ops += 64;
    }
    a->ops = ops;
    return NULL;
}

static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9; }

static double run_cell(int mech, int read_pct, int nth, double *restart_ratio) {
    bt_t t; bt_init(&t);
    g_latch_off = false;
    for (int k = 0; k < 200000; k++) bt_insert_pess(&t, k * 2, k);
    g_latch_off = (mech == 0 || mech == 3);
    atomic_store(&t.restarts, 0); atomic_store(&t.leaf_splits, 0);
    atomic_store(&g_stop, 0);
    pthread_t th[16]; b_arg_t a[16];
    double t0 = now_s();
    for (int i = 0; i < nth; i++) { a[i] = (b_arg_t){ &t, mech, read_pct, 0x1234567u * (uint32_t)(i + 1) + (uint32_t)nth, 0 }; pthread_create(&th[i], NULL, bench_worker, &a[i]); }
    struct timespec d = { 0, 150 * 1000000L };
    nanosleep(&d, NULL);
    atomic_store(&g_stop, 1);
    long ops = 0;
    for (int i = 0; i < nth; i++) { pthread_join(th[i], NULL); ops += a[i].ops; }
    double el = now_s() - t0;
    *restart_ratio = (double)atomic_load(&t.restarts);
    g_latch_off = false;
    long cnt;
    if (bt_check(&t, &cnt) != 0) { printf("INVARIANT BROKEN mech=%d\n", mech); exit(1); }
    *restart_ratio /= (double)(cnt - 200000 > 0 ? cnt - 200000 : 1);   /* 재시작 / 새로 들어간 키 */
    bt_destroy(&t);
    return (double)ops / el / 1e6;
}

static int cmp_d(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); }

int main(int argc, char **argv) {
    const char *mn[4] = { "coarse ", "crab   ", "opt    ", "nolatch" };
    const int thr[4] = { 1, 2, 4, 8 };
    const int mix[3] = { 100, 95, 20 };
    const char *mixname[3] = { "ro", "rh", "wh" };
    double dummy;
    run_cell(1, 95, 4, &dummy);                          /* 워밍업 (첫 칸이 느리게 나오는 것 방지) */
    for (int m = 0; m < 3; m++) {
        if (argc > 1 && strcmp(argv[1], mixname[m]) != 0) continue;
        printf("== %s (lookup %d%% / insert %d%%), Mops/s, median of 5 x 150 ms ==\n", mixname[m], mix[m], 100 - mix[m]);
        printf("mech      T=1     T=2     T=4     T=8    | restarts/new key (T=8)\n");
        for (int me = 0; me < 4; me++) {
            if (m == 0 && me == 2) continue;              /* read-only: opt 의 lookup 은 crab 과 같은 코드 */
            if (m != 0 && me == 3) continue;              /* nolatch 기준선은 read-only 에서만 */
            printf("%s", mn[me]);
            double rr = 0;
            for (int ti = 0; ti < 4; ti++) {
                double v[5];
                for (int r = 0; r < 5; r++) v[r] = run_cell(me, mix[m], thr[ti], &rr);
                qsort(v, 5, sizeof v[0], cmp_d);
                printf("  %6.2f", v[2]);
                fflush(stdout);
            }
            if (me == 2) printf("    | %.3f", rr);
            printf("\n");
        }
    }
    return 0;
}
