/* C-M08 벤치마크: libc vs 바이트 버전 vs 워드 버전 (solution.c 의 구현을 그대로 #include)
 * 숫자는 테스트(CHECK)에 넣지 않는다 — 기계·컴파일러·플래그마다 다르기 때문.
 *
 *   cc -std=c11 -O2 demo_bench.c -o bench_o2 && ./bench_o2                         # (1) 기본: 루프가 libc 호출로 바뀜
 *   cc -std=c11 -O2 -fno-builtin demo_bench.c -o bench_nb && ./bench_nb             # (2) 바뀌지 않음, 대신 NEON 자동 벡터화
 *   cc -std=c11 -O2 -fno-builtin -fno-vectorize -fno-slp-vectorize demo_bench.c -o bench_sc && ./bench_sc   # (3) 순수 스칼라
 */
#define _POSIX_C_SOURCE 200809L
#define MEMLAB_NO_MAIN
#include "solution.c"
#include <stdlib.h>
#include <time.h>

typedef void *(*cpy_t)(void *, const void *, size_t);
typedef void *(*set_t)(void *, int, size_t);

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* volatile 함수 포인터로 부른다 → 컴파일러가 크기를 알고 특수화하거나 호출을 없애지 못한다 */
static cpy_t volatile g_cpy;
static set_t volatile g_set;

enum { MODE_CPY, MODE_SET };
/* 한 측정: 총 ~256 MiB 를 옮길 만큼 반복, 3번 중 최고 → GB/s (1e9 바이트/초) */
static double run(int mode, unsigned char *d, const unsigned char *s, size_t n) {
    size_t reps = ((size_t)256 << 20) / n;
    if (reps < 3) reps = 3;
    double best = 1e30;
    for (int k = 0; k < 3; k++) {
        double t0 = now_s();
        for (size_t r = 0; r < reps; r++) {
            if (mode == MODE_CPY) g_cpy(d, s, n);
            else                  g_set(d, (int)r, n);
        }
        double t = now_s() - t0;
        if (t < best) best = t;
    }
    return (double)n * (double)reps / best / 1e9;
}

static void row(const char *what, int mode, void *f_libc, void *f_b, void *f_w,
                unsigned char *d, const unsigned char *s, size_t n) {
    double g[3];
    void *fs[3] = { f_libc, f_b, f_w };
    for (int i = 0; i < 3; i++) {
        if (mode == MODE_CPY) g_cpy = (cpy_t)fs[i]; else g_set = (set_t)fs[i];
        g[i] = run(mode, d, s, n);
    }
    printf("%-22s %9zu %9.2f %9.2f %9.2f\n", what, n, g[0], g[1], g[2]);
}

int main(void) {
    static const size_t sizes[] = { 16, 256, 4096, 65536, 1u << 20, 32u << 20 };
    size_t big = (32u << 20) + 64;
    unsigned char *A = aligned_alloc(64, big), *B = aligned_alloc(64, big);
    if (!A || !B) return 1;
    for (size_t i = 0; i < big; i++) { A[i] = (unsigned char)i; B[i] = (unsigned char)(i * 7); }

    /* 함수 포인터 ↔ void* 변환은 POSIX 가 보장 (ISO C 는 아님) — 표를 짧게 쓰려는 편의 */
    printf("%-22s %9s %9s %9s %9s   (GB/s, best of 3)\n", "case", "bytes", "libc", "byte", "word");
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        row("memcpy aligned", MODE_CPY, (void *)memcpy, (void *)my_memcpy_b, (void *)my_memcpy, A, B, sizes[i]);
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        row("memcpy dst+1 src+3", MODE_CPY, (void *)memcpy, (void *)my_memcpy_b, (void *)my_memcpy, A + 1, B + 3, sizes[i]);
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        row("memmove overlap d=s+5", MODE_CPY, (void *)memmove, (void *)my_memmove_b, (void *)my_memmove, A + 5, A, sizes[i]);
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
        row("memset aligned", MODE_SET, (void *)memset, (void *)my_memset_b, (void *)my_memset, A, NULL, sizes[i]);
    free(A); free(B);
    return 0;
}
