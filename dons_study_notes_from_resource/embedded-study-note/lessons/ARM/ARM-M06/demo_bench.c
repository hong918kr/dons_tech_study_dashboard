/* ARM-M06 demo_bench: scalar / auto(clang -O2) / intrinsics / asm (+ 연습의 asm4, nodot) 처리량
 *   cc -std=c11 -O2 demo_bench.c solution.S practice.S shared_bugs.S -o /tmp/ARM-M06_bench && /tmp/ARM-M06_bench
 * 함수는 solution.c / demo_emit.c 를 그대로 #include 해서 쓴다 (테스트 main 은 이름만 바꿔 둔다).
 * 샘플 하나 = 약 32M 원소를 처리할 만큼 반복, 샘플 11개의 중앙값. 타이머 CLOCK_UPTIME_RAW (~42 ns 해상도).
 * QoS USER_INTERACTIVE 는 P 코어로 가라는 "힌트"일 뿐 고정이 아니다 → 수치는 흔들린다. 비율을 보라.
 */
#define _DARWIN_C_SOURCE
#define main solution_harness_main
#include "solution.c"
#undef main
#include "demo_emit.c"
#include <pthread.h>
#include <time.h>

extern float   dot_f32_asm4(const float *a, const float *b, size_t n);
extern int32_t dot_s8_nodot(const int8_t *a, const int8_t *b, size_t n);

static volatile double sink;
static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static int cmp_d(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); }

enum { SAMPLES = 11 };
#define TARGET_ELEMS (32u << 20)

/* kind 0 = vadd, 1 = dot_f32, 2 = dot_s8. 함수 포인터는 volatile → 인라인/루프 밖 끌어내기 불가 */
static double ns_per_call(int kind, void *fn, void *d, const void *a, const void *b, size_t n) {
    size_t reps = TARGET_ELEMS / n; if (reps < 2) reps = 2;
    double t[SAMPLES];
    for (int s = 0; s < SAMPLES; s++) {
        double acc = 0;
        uint64_t t0 = now_ns();
        for (size_t r = 0; r < reps; r++) {
            if (kind == 0) { vadd_fn volatile f = (vadd_fn)fn; f(d, a, b, n); }
            else if (kind == 1) { dotf_fn volatile f = (dotf_fn)fn; acc += f(a, b, n); }
            else { dots8_fn volatile f = (dots8_fn)fn; acc += f(a, b, n); }
        }
        uint64_t t1 = now_ns();
        sink = acc;
        t[s] = (double)(t1 - t0) / (double)reps;
    }
    qsort(t, SAMPLES, sizeof t[0], cmp_d);
    return t[SAMPLES / 2];
}

int main(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    static const size_t sizes[] = { 1024, 65536, 4u << 20 };
    size_t maxn = 4u << 20;
    float *fa = malloc(maxn * 4), *fb = malloc(maxn * 4), *fd = malloc(maxn * 4);
    int8_t *ia = malloc(maxn), *ib = malloc(maxn);
    for (size_t i = 0; i < maxn; i++) { fa[i] = rnd_f(-1, 1); fb[i] = rnd_f(-1, 1); fd[i] = 0; ia[i] = rnd_s8(); ib[i] = rnd_s8(); }

    struct { const char *name; void *fn; } vadd[] = {
        { "scalar", (void *)vadd_f32_scalar }, { "auto", (void *)vadd_plain },
        { "neon", (void *)vadd_f32_neon }, { "asm", (void *)vadd_f32_asm } };
    struct { const char *name; void *fn; } dotf[] = {
        { "scalar", (void *)dot_f32_scalar }, { "auto", (void *)dot_f32_plain },
        { "neon", (void *)dot_f32_neon }, { "asm", (void *)dot_f32_asm }, { "asm4", (void *)dot_f32_asm4 } };
    struct { const char *name; void *fn; } dots[] = {
        { "scalar", (void *)dot_s8_scalar }, { "auto", (void *)dot_s8_plain },
        { "neon", (void *)dot_s8_neon }, { "asm", (void *)dot_s8_asm }, { "nodot", (void *)dot_s8_nodot } };

    printf("vadd_f32  GFLOP/s (1 FLOP/elem; GB/s = x12)\n%9s", "n");
    for (int k = 0; k < 4; k++) printf("%9s", vadd[k].name);
    printf("\n");
    for (int si = 0; si < 3; si++) {
        size_t n = sizes[si]; printf("%9zu", n);
        for (int k = 0; k < 4; k++) printf("%9.2f", (double)n / ns_per_call(0, vadd[k].fn, fd, fa, fb, n));
        printf("\n");
    }
    printf("dot_f32   GFLOP/s (2 FLOP/elem)\n%9s", "n");
    for (int k = 0; k < 5; k++) printf("%9s", dotf[k].name);
    printf("\n");
    for (int si = 0; si < 3; si++) {
        size_t n = sizes[si]; printf("%9zu", n);
        for (int k = 0; k < 5; k++) printf("%9.2f", 2.0 * (double)n / ns_per_call(1, dotf[k].fn, NULL, fa, fb, n));
        printf("\n");
    }
    {   /* n = 4M 은 계약(n <= 131071) 밖이지만 무작위 데이터라 부분합이 int32 근처도 안 간다 → 확인 */
        int64_t s = 0, mx = 0;
        for (size_t i = 0; i < maxn; i++) { s += (int64_t)ia[i] * ib[i]; if (llabs(s) > mx) mx = llabs(s); }
        printf("(dot_s8 4M random: exact %lld, max |partial| %lld, asm %d)\n", (long long)s, (long long)mx, dot_s8_asm(ia, ib, maxn));
    }
    printf("dot_s8    GOPS (2 OP/elem)\n%9s", "n");
    for (int k = 0; k < 5; k++) printf("%9s", dots[k].name);
    printf("\n");
    for (int si = 0; si < 3; si++) {
        size_t n = sizes[si]; printf("%9zu", n);
        for (int k = 0; k < 5; k++) printf("%9.2f", 2.0 * (double)n / ns_per_call(2, dots[k].fn, NULL, ia, ib, n));
        printf("\n");
    }
    free(fa); free(fb); free(fd); free(ia); free(ib);
    return 0;
}
