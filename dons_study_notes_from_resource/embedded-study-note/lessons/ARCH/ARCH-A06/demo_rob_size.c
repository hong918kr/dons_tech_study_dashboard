/* ARCH-A06 demo: 진짜 M6 의 비순차 창(instruction window) 크기 감 잡기 — 실측 (arm64 전용)
 *
 * 방법 (H. Wong 의 "두 캐시 미스 사이에 N 개 채우기" 측정법):
 *   반복마다  ldr A ← [A]  ;  필러 N 개  ;  ldr B ← [B]  ;  필러 N 개
 *   A, B 는 서로 다른 큰 버퍼(각 256 MB)를 무작위 순환으로 도는 포인터 체이스 → 거의 매번 DRAM 미스.
 *   B 의 load 가 A 의 미스가 끝나기 전에 창 안에 들어오면 두 미스가 겹친다 → 반복당 ≈ 미스 1 번.
 *   필러가 창보다 많으면 B 는 A 가 retire 할 때까지 못 들어온다 → 반복당 ≈ 미스 2 번.
 *   "ns/반복" 이 두 배로 뛰는 N ≈ 그 필러 종류에 대해 먼저 바닥나는 자원의 크기 (ROB, 물리 레지스터 등).
 *   필러 2 종: nop (레지스터를 안 씀) / add x9, x10, #1 (매번 새 물리 레지스터가 필요).
 *
 * build: cc -std=c11 -O2 -Wall -Wextra -D_DARWIN_C_SOURCE demo_rob_size.c -o demo_rob && ./demo_rob
 * 타이밍은 여기서만 (CHECK 없음). 결과는 측정 당시 부하·코어 종류에 따라 흔들린다.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#if !defined(__aarch64__)
int main(void) { puts("arm64 전용 demo"); return 0; }
#else

#define LINE 128                              /* hw.cachelinesize = 128 */
#define BYTES (256u << 20)                    /* 체인 하나당 256 MB ≫ L2 + SLC */
#define NODES (BYTES / LINE)

static uint64_t xs64(uint64_t *s) { uint64_t x = *s; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return *s = x; }

static void **make_chain(uint64_t seed) {
    char *buf = aligned_alloc(16384, BYTES);
    uint32_t *perm = malloc(sizeof(uint32_t) * NODES);
    if (!buf || !perm) { perror("alloc"); exit(1); }
    for (uint32_t i = 0; i < NODES; i++) perm[i] = i;
    for (uint32_t i = NODES - 1; i > 0; i--) {      /* Fisher–Yates → 하나의 큰 순환 (Sattolo) */
        uint32_t j = (uint32_t)(xs64(&seed) % i);
        uint32_t t = perm[i]; perm[i] = perm[j]; perm[j] = t;
    }
    for (uint32_t i = 0; i < NODES; i++)
        *(void **)(buf + (size_t)perm[i] * LINE) = buf + (size_t)perm[(i + 1) % NODES] * LINE;
    void **start = (void **)(buf + (size_t)perm[0] * LINE);
    free(perm);
    return start;
}

#define STR2(x) #x
#define STR(x) STR2(x)
#define DEF(N, NAME, FILL)                                                         \
    static void NAME##_##N(void ***pa, void ***pb, long it) {                      \
        void **a = *pa, **b = *pb;                                                 \
        __asm__ volatile("1:\n\t"                                                  \
                         "ldr %0, [%0]\n\t"                                        \
                         ".rept " STR(N) "\n\t" FILL "\n\t.endr\n\t"               \
                         "ldr %1, [%1]\n\t"                                        \
                         ".rept " STR(N) "\n\t" FILL "\n\t.endr\n\t"               \
                         "subs %2, %2, #1\n\t"                                     \
                         "b.ne 1b\n"                                               \
                         : "+r"(a), "+r"(b), "+r"(it)                              \
                         :                                                         \
                         : "x9", "x10", "cc", "memory");                           \
        *pa = a; *pb = b;                                                          \
    }

/* nop 은 넓게, add 는 꺾이는 곳 근처를 촘촘히 (첫 실행에서 add 는 192–256 사이, nop 은 1280 까지 뚜렷한 꺾임 없음) */
#define FOR_NOP(X) X(0) X(128) X(256) X(384) X(512) X(640) X(768) X(896) X(1024) X(1280) X(1536) X(2048) X(2560) X(3072)
#define FOR_ADD(X) X(0) X(64) X(128) X(160) X(176) X(192) X(200) X(208) X(216) X(224) X(232) X(240) X(256) X(320) X(512)
#define DEF_NOP(N) DEF(N, fnop, "nop")
#define DEF_ADD(N) DEF(N, fadd, "add x9, x10, #1")
FOR_NOP(DEF_NOP)
FOR_ADD(DEF_ADD)

typedef void (*fn_t)(void ***, void ***, long);
#define ENTRY_NOP(N) {N, fnop_##N},
#define ENTRY_ADD(N) {N, fadd_##N},
typedef struct { int n; fn_t f; } pt_t;
static const pt_t T_NOP[] = {FOR_NOP(ENTRY_NOP)}, T_ADD[] = {FOR_ADD(ENTRY_ADD)};

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int cmpd(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); }

int main(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);   /* 힌트일 뿐, 코어 고정 아님 */
    void **A = make_chain(1), **B = make_chain(2);
    const long IT = 100000;
    enum { REP = 5 };
    printf("load: 실행 직전 uptime 을 같이 기록할 것. median of %d, %ld iterations\n", REP, IT);
    const pt_t *T[2] = {T_NOP, T_ADD};
    const int NT[2] = {(int)(sizeof T_NOP / sizeof T_NOP[0]), (int)(sizeof T_ADD / sizeof T_ADD[0])};
    const char *name[2] = {"nop", "add x9, x10, #1"};
    for (int k = 0; k < 2; k++) {
        printf("filler = %s\n     N   ns/iter   min    max\n", name[k]);
        for (int i = 0; i < NT[k]; i++) {
            double t[REP];
            T[k][i].f(&A, &B, IT / 10);                                  /* 예열 */
            for (int r = 0; r < REP; r++) { double t0 = now(); T[k][i].f(&A, &B, IT); t[r] = (now() - t0) / IT * 1e9; }
            qsort(t, REP, sizeof(double), cmpd);
            printf("  %5d  %7.1f  %6.1f %6.1f\n", T[k][i].n, t[REP / 2], t[0], t[REP - 1]);
        }
    }
    printf("(sink %p %p)\n", (void *)A, (void *)B);
    return 0;
}
#endif
