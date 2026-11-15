/* ALGO-M07 demo_depth: 하향식 메모의 재귀 깊이와 스택 바이트 (이 맥, arm64, -O2)
 *   1) 같은 메모 함수라도 "어떤 순서로 하위 문제를 부르나" 에 따라 최대 깊이가 n 에서 상수까지 달라진다
 *   2) 최대 깊이에서의 스택 사용량 = 최상위 프레임 주소 - 가장 깊은 프레임 주소 (__builtin_frame_address)
 *   3) 작은 스택(64 KiB pthread — Cortex-M 태스크 스택 흉내)에서 깊은 메모가 실제로 죽는지: fork 한 자식에서 실행
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_depth.c -o demo_depth && ./demo_depth
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned g_depth, g_max;
static char *g_top, *g_low;
#define ENTER() do { if (++g_depth > g_max) g_max = g_depth; char *fp_ = (char *)__builtin_frame_address(0); \
                     if (fp_ < g_low) g_low = fp_; } while (0)
#define LEAVE() (g_depth--)
static void reset(void) { g_depth = 0; g_max = 0; g_top = (char *)__builtin_frame_address(0); g_low = g_top; }
static long stack_bytes(void) { return (long)(g_top - g_low); }

/* ---- 계단 memo ---- */
static uint64_t sm[300]; static uint8_t sh[300];
static const uint8_t *S; static size_t SK;
static uint64_t stairs_f(uint32_t i) {
    ENTER();
    uint64_t w = 0;
    if (i == 0) w = 1;
    else if (sh[i]) w = sm[i];
    else {
        for (size_t j = 0; j < SK; j++) if (S[j] <= i) w += stairs_f(i - S[j]);
        sh[i] = 1; sm[i] = w;
    }
    LEAVE();
    return w;
}

/* ---- 동전 최소 개수 memo ---- */
#define AMT_MAX 200000
static int32_t cm[AMT_MAX + 1];
static const uint32_t *C; static size_t CK;
static int32_t coin_f(uint32_t a) {
    ENTER();
    int32_t best;
    if (a == 0) best = 0;
    else if (cm[a] != -2) best = cm[a];
    else {
        best = -1;
        for (size_t j = 0; j < CK; j++) {
            if (C[j] > a) continue;
            int32_t s = coin_f(a - C[j]);
            if (s >= 0 && (best < 0 || s + 1 < best)) best = s + 1;
        }
        cm[a] = best;
    }
    LEAVE();
    return best;
}
static int32_t coin_memo(const uint32_t *c, size_t k, uint32_t amt) {
    C = c; CK = k;
    for (uint32_t a = 0; a <= amt; a++) cm[a] = -2;
    return coin_f(amt);
}

/* ---- 조합 수 memo: h(i, a) ---- */
static uint64_t hm[8][2001]; static uint8_t hh[8][2001];
static uint64_t combo_f(size_t i, uint32_t a) {
    ENTER();
    uint64_t w;
    if (a == 0) w = 1;
    else if (i == CK) w = 0;
    else if (hh[i][a]) w = hm[i][a];
    else {
        w = combo_f(i + 1, a);
        if (C[i] <= a) w += combo_f(i, a - C[i]);
        hh[i][a] = 1; hm[i][a] = w;
    }
    LEAVE();
    return w;
}

/* ---- LIS memo: 안쪽 루프 방향 선택 ---- */
static size_t lm[5000];
static const int32_t *LA; static int inner_desc;
static size_t lis_f(size_t i) {
    ENTER();
    size_t best = lm[i];
    if (!best) {
        best = 1;
        for (size_t q = 0; q < i; q++) {
            size_t j = inner_desc ? i - 1 - q : q;
            if (LA[j] < LA[i]) { size_t t = lis_f(j) + 1; if (t > best) best = t; }
        }
        lm[i] = best;
    }
    LEAVE();
    return best;
}
static size_t lis_memo(const int32_t *a, size_t n, int outer_desc, int idesc) {
    LA = a; inner_desc = idesc;
    memset(lm, 0, sizeof lm);
    size_t best = 0;
    for (size_t q = 0; q < n; q++) { size_t i = outer_desc ? n - 1 - q : q; size_t t = lis_f(i); if (t > best) best = t; }
    return best;
}

/* ---- 작은 스택 스레드 ---- */
static uint32_t g_amt;
static int32_t g_res;
static void *thread_main(void *arg) {
    (void)arg;
    static const uint32_t c1[] = { 1, 2, 5 };
    g_res = coin_memo(c1, 3, g_amt);
    return NULL;
}
static void run_small_stack(size_t stack_bytes_, uint32_t amt) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        pthread_attr_t at; pthread_t th;
        pthread_attr_init(&at);
        pthread_attr_setstacksize(&at, stack_bytes_);
        g_amt = amt;
        pthread_create(&th, &at, thread_main, NULL);
        pthread_join(th, NULL);
        printf("    stack %4zu KiB, amount %6u: ok, answer=%d\n", stack_bytes_ / 1024, amt, g_res);
        fflush(stdout);
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFSIGNALED(st))
        printf("    stack %4zu KiB, amount %6u: child killed by signal %d (%s)\n", stack_bytes_ / 1024, amt, WTERMSIG(st), strsignal(WTERMSIG(st)));
}

int main(void) {
    printf("1) max recursion depth / stack bytes of memoized top-down calls\n");
    static const uint8_t asc[] = { 1, 2, 3 }, desc[] = { 3, 2, 1 };
    S = asc; SK = 3; memset(sh, 0, sizeof sh); reset(); stairs_f(256);
    printf("  stairs n=256  steps {1,2,3}        : depth %4u, stack %6ld B (%.0f B/frame)\n", g_max, stack_bytes(), (double)stack_bytes() / (g_max - 1));
    S = desc; SK = 3; memset(sh, 0, sizeof sh); reset(); stairs_f(256);
    printf("  stairs n=256  steps {3,2,1}        : depth %4u, stack %6ld B\n", g_max, stack_bytes());

    static const uint32_t up[] = { 1, 5, 10, 25 }, dn[] = { 25, 10, 5, 1 };
    reset(); int32_t r = coin_memo(up, 4, 2000);
    printf("  coin_min amt=2000 coins {1,5,10,25}: depth %4u, stack %6ld B (%.0f B/frame), answer %d\n", g_max, stack_bytes(), (double)stack_bytes() / (g_max - 1), r);
    reset(); r = coin_memo(dn, 4, 2000);
    printf("  coin_min amt=2000 coins {25,10,5,1}: depth %4u, stack %6ld B, answer %d\n", g_max, stack_bytes(), r);

    C = up; CK = 4; memset(hh, 0, sizeof hh); reset(); uint64_t w = combo_f(0, 2000);
    printf("  combo    amt=2000 coins {1,5,10,25}: depth %4u, stack %6ld B, ways %llu\n", g_max, stack_bytes(), (unsigned long long)w);
    C = dn; CK = 4; memset(hh, 0, sizeof hh); reset(); w = combo_f(0, 2000);
    printf("  combo    amt=2000 coins {25,10,5,1}: depth %4u, stack %6ld B, ways %llu\n", g_max, stack_bytes(), (unsigned long long)w);

    static int32_t inc[4000];
    for (int i = 0; i < 4000; i++) inc[i] = i;                                    /* 최악: 이미 정렬 */
    static const char *nm[2] = { "asc ", "desc" };
    for (int o = 0; o < 2; o++)
        for (int d = 0; d < 2; d++) {
            reset(); size_t L = lis_memo(inc, 4000, o, d);
            printf("  lis      n=4000 sorted, outer %s inner %s: depth %4u, stack %6ld B, LIS %zu\n", nm[o], nm[d], g_max, stack_bytes(), L);
        }

    printf("\n2) coin_min memo (coins {1,2,5}) on a small pthread stack, run in a forked child\n");
    run_small_stack(64 * 1024, 500);
    run_small_stack(64 * 1024, 2000);
    run_small_stack(1024 * 1024, 10000);
    run_small_stack(1024 * 1024, 20000);
    return 0;
}
