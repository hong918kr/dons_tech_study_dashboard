/* ARCH-M07 추가 연습 — 한 번에 검증
 *
 *   연습 1  루프 순서 6가지 → 안쪽 반복당 미스 (CS:APP 식 분석) 와 시뮬레이터 비교
 *   연습 2  캐시 친화 전치 (blocked transpose): 정확성 + 미스 수
 *   연습 3  B 타일 패킹 (packing): ld 패딩 없이 타일 자기 충돌 없애기 — GotoBLAS/BLIS 의 핵심 아이디어
 *   연습 4  GFLOP/s 와 메모리 트래픽 어림셈 (n = 2048, ijk vs 타일링)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

static int imin(int a, int b) { return a < b ? a : b; }

/* ---- 본문 Part B 와 같은 축소 LRU 시뮬레이터 ---- */
typedef struct {
    uint32_t sets, ways; unsigned off_bits, idx_bits;
    uint32_t *tag; uint8_t *valid; uint64_t *last;
    uint64_t tick, accesses, misses;
} cache_t;
static unsigned ilog2u(uint32_t x) { unsigned r = 0; while (x > 1) { x >>= 1; r++; } return r; }
static void cache_init(cache_t *c, uint32_t cap, uint32_t line, uint32_t ways) {
    memset(c, 0, sizeof *c);
    c->sets = cap / (line * ways); c->ways = ways;
    c->off_bits = ilog2u(line); c->idx_bits = ilog2u(c->sets);
    size_t nl = (size_t)c->sets * ways;
    c->tag = calloc(nl, 4); c->valid = calloc(nl, 1); c->last = calloc(nl, 8);
}
static void cache_free(cache_t *c) { free(c->tag); free(c->valid); free(c->last); }
static void acc(cache_t *c, uint32_t addr) {
    uint32_t la = addr >> c->off_bits, set = la & (c->sets - 1), tag = (uint32_t)((uint64_t)la >> c->idx_bits);
    size_t b = (size_t)set * c->ways, victim = b;
    uint64_t oldest = UINT64_MAX;
    c->accesses++; c->tick++;
    for (size_t w = b; w < b + c->ways; w++) {
        if (c->valid[w] && c->tag[w] == tag) { c->last[w] = c->tick; return; }
        uint64_t key = c->valid[w] ? c->last[w] : 0;
        if (key < oldest) { oldest = key; victim = w; }
    }
    c->misses++; c->valid[victim] = 1; c->tag[victim] = tag; c->last[victim] = c->tick;
}

/* ======================= 연습 1 — 루프 순서 6가지 ======================= */
/* 안쪽 루프 변수가 무엇이냐로 세 무리가 갈린다. 안쪽 반복 하나당 미스 (라인이 다음 바깥 반복까지 못 버틸 만큼 n 이 클 때):
 *   안쪽 k (ijk, jik): A 행 연속 e/L + B 열 1            → 1 + e/L
 *   안쪽 j (ikj, kij): B 행 연속 e/L + C 행 연속 e/L      → 2e/L
 *   안쪽 i (jki, kji): A 열 1 + C 열 1                  → 2
 * (e = 원소 바이트, L = 라인 바이트. 쓰기도 write-allocate 로 센다) */
enum { O_IJK, O_JIK, O_IKJ, O_KIJ, O_JKI, O_KJI };
static double predicted_miss_per_iter(int order, double e, double L) {
    switch (order) {
    case O_IJK: case O_JIK: return 1.0 + e / L;
    case O_IKJ: case O_KIJ: return 2.0 * e / L;
    default: return 2.0;
    }
}

/* 6가지 순서의 주소 트레이스 (A, B, C 를 base 부터 붙여 놓음, 행 우선, ld). C 를 미리 0 으로 지우는 접근은 빼고,
 * 안쪽 문장만 센다: 안쪽 k 면 A·B 읽기 (합은 레지스터, C 는 바깥에서 한 번 쓰기), 아니면 B/A 읽기 + C 읽기·쓰기. */
static uint64_t sim_order(int order, int n, int ld, uint32_t e, uint32_t cap, uint32_t line, uint32_t ways) {
    cache_t c;
    cache_init(&c, cap, line, ways);
    uint32_t base = 0x10000000u, sz = (uint32_t)n * (uint32_t)ld * e;
    uint32_t A = base, B = base + sz, C = base + 2 * sz;
#define AT(M, r, col) ((M) + ((uint32_t)(r) * (uint32_t)ld + (uint32_t)(col)) * e)
    for (int x = 0; x < n; x++)
        for (int y = 0; y < n; y++) {
            int i, j, k;
            if (order == O_IJK || order == O_JIK) {
                i = order == O_IJK ? x : y; j = order == O_IJK ? y : x;
                for (k = 0; k < n; k++) { acc(&c, AT(A, i, k)); acc(&c, AT(B, k, j)); }
                acc(&c, AT(C, i, j));
            } else if (order == O_IKJ || order == O_KIJ) {
                i = order == O_IKJ ? x : y; k = order == O_IKJ ? y : x;
                acc(&c, AT(A, i, k));
                for (j = 0; j < n; j++) { acc(&c, AT(B, k, j)); acc(&c, AT(C, i, j)); acc(&c, AT(C, i, j)); }
            } else {
                j = order == O_JKI ? x : y; k = order == O_JKI ? y : x;
                acc(&c, AT(B, k, j));
                for (i = 0; i < n; i++) { acc(&c, AT(A, i, k)); acc(&c, AT(C, i, j)); acc(&c, AT(C, i, j)); }
            }
        }
#undef AT
    uint64_t m = c.misses;
    cache_free(&c);
    return m;
}

/* ======================= 연습 2 — 캐시 친화 전치 ======================= */
static void transpose_naive(int n, const float *src, float *dst) {
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) dst[j * n + i] = src[i * n + j];
}
static void transpose_blocked(int n, int T, const float *src, float *dst) {
    for (int ii = 0; ii < n; ii += T)
        for (int jj = 0; jj < n; jj += T)
            for (int i = ii; i < imin(ii + T, n); i++)
                for (int j = jj; j < imin(jj + T, n); j++) dst[j * n + i] = src[i * n + j];
}
/* 전치 트레이스: 원소마다 src 읽기 + dst 쓰기. dst 는 src 바로 뒤 + pad 바이트 */
static uint64_t sim_transpose(int n, int T, uint32_t pad, uint32_t cap, uint32_t line, uint32_t ways) {
    cache_t c;
    cache_init(&c, cap, line, ways);
    uint32_t S = 0x10000000u, D = S + (uint32_t)n * (uint32_t)n * 4u + pad;
    if (T <= 0) T = n;   /* T = n 이면 naive 와 같은 순서 */
    for (int ii = 0; ii < n; ii += T)
        for (int jj = 0; jj < n; jj += T)
            for (int i = ii; i < imin(ii + T, n); i++)
                for (int j = jj; j < imin(jj + T, n); j++) {
                    acc(&c, S + (uint32_t)(i * n + j) * 4u);
                    acc(&c, D + (uint32_t)(j * n + i) * 4u);
                }
    uint64_t m = c.misses;
    cache_free(&c);
    return m;
}

/* ======================= 연습 3 — B 타일 패킹 ======================= */
/* 타일링 (본문 ii→jj→kk) 과 같은 계산이지만, (jj, kk) 마다 B 타일을 연속 버퍼 P (T*T, 0x2000_0000) 에 복사해 두고
 * 안쪽에서는 P 를 읽는다. 루프 순서: jj → kk → [B 타일 → P 복사] → ii → i → k → j.
 * 복사 비용 (B 읽기 + P 쓰기) 도 트레이스에 넣는다. C 0 으로 지우기는 맨 앞에 n^2 번 쓰기. */
static uint64_t sim_tiled(int n, int ld, int T, bool pack, uint32_t e, uint32_t cap, uint32_t line, uint32_t ways) {
    cache_t c;
    cache_init(&c, cap, line, ways);
    uint32_t sz = (uint32_t)n * (uint32_t)ld * e, A = 0x10000000u, B = A + sz, C = A + 2 * sz, P = 0x20000000u;
#define AT(M, r, col) ((M) + ((uint32_t)(r) * (uint32_t)ld + (uint32_t)(col)) * e)
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) acc(&c, AT(C, i, j));
    for (int jj = 0; jj < n; jj += T)
        for (int kk = 0; kk < n; kk += T) {
            int je = imin(jj + T, n), ke = imin(kk + T, n), w = je - jj;
            if (pack)
                for (int k = kk; k < ke; k++)
                    for (int j = jj; j < je; j++) { acc(&c, AT(B, k, j)); acc(&c, P + (uint32_t)((k - kk) * w + (j - jj)) * e); }
            for (int ii = 0; ii < n; ii += T)
                for (int i = ii; i < imin(ii + T, n); i++)
                    for (int k = kk; k < ke; k++) {
                        acc(&c, AT(A, i, k));
                        for (int j = jj; j < je; j++) {
                            acc(&c, pack ? P + (uint32_t)((k - kk) * w + (j - jj)) * e : AT(B, k, j));
                            acc(&c, AT(C, i, j)); acc(&c, AT(C, i, j));
                        }
                    }
        }
#undef AT
    uint64_t m = c.misses;
    cache_free(&c);
    return m;
}
/* 패킹을 실제 계산으로도: 결과가 비패킹 타일링과 같아야 한다 (float, 정수값) */
static void mm_tiled_packed(int n, int T, const float *A, const float *B, float *C, float *P) {
    for (int i = 0; i < n * n; i++) C[i] = 0.0f;
    for (int jj = 0; jj < n; jj += T)
        for (int kk = 0; kk < n; kk += T) {
            int je = imin(jj + T, n), ke = imin(kk + T, n), w = je - jj;
            for (int k = kk; k < ke; k++)
                for (int j = jj; j < je; j++) P[(k - kk) * w + (j - jj)] = B[k * n + j];
            for (int i = 0; i < n; i++)
                for (int k = kk; k < ke; k++) {
                    float r = A[i * n + k];
                    for (int j = jj; j < je; j++) C[i * n + j] += r * P[(k - kk) * w + (j - jj)];
                }
        }
}

/* ======================= 연습 4 — 어림셈 ======================= */
static double gflops(int n, double sec) { return 2.0 * (double)n * n * n / sec * 1e-9; }
/* L1 밖에서 가져와야 하는 바이트 (모델):
 *   ijk  (B 열 라인이 j 사이에 못 버팀): 안쪽 반복마다 B 라인 하나 → n^3 * L 바이트 + A, C 는 무시할 만큼
 *   ikj  : i 마다 B 전체를 한 번 → n * n^2 * e 바이트
 *   타일 T: (ii,jj,kk) 마다 A·B 타일 → 2 * (n/T)^3 * T^2 * e = 2 n^3 e / T 바이트 (+ C n^2 e) */
static double bytes_ijk(int n, double L) { return (double)n * n * n * L; }
static double bytes_ikj(int n, double e) { return (double)n * n * n * e; }
static double bytes_tiled(int n, int T, double e) { return 2.0 * (double)n * n * n * e / T + (double)n * n * e; }

int main(void) {
    printf("-- 연습 1: six loop orders, predicted vs simulated misses per inner iteration --\n");
    {
        /* 작은 캐시 (1 KB, 32 B, 4-way) + n=128 double, ld 패딩 132 (행 = 33 라인, 홀수) → 충돌이 아닌 용량 효과만.
         * 행 하나 (1 KB) 가 캐시 전체라서 "다음 바깥 반복까지 아무 행도 못 버틴다" 는 분석의 가정이 성립한다. */
        int n = 128, ld = 132;
        bool ok = true;
        static const char *nm[] = { "ijk", "jik", "ikj", "kij", "jki", "kji" };
        for (int o = O_IJK; o <= O_KJI; o++) {
            double p = predicted_miss_per_iter(o, 8, 32);
            double s = (double)sim_order(o, n, ld, 8, 1024, 32, 4) / ((double)n * n * n);
            printf("      %s  predicted %.3f  simulated %.3f\n", nm[o], p, s);
            if (s < p * 0.9 || s > p * 1.1) ok = false;
        }
        CHECK(ok);
        CHECK(predicted_miss_per_iter(O_IJK, 8, 128) == 1.0625);   /* M6: e=8, L=128 */
        CHECK(predicted_miss_per_iter(O_KIJ, 8, 128) == 0.125);
    }

    printf("-- 연습 2: blocked transpose (float, n=128, 16 KB 4-way 32 B) --\n");
    {
        int n = 128;
        float *s = malloc(sizeof(float) * n * n), *d1 = malloc(sizeof(float) * n * n), *d2 = malloc(sizeof(float) * n * n);
        for (int i = 0; i < n * n; i++) s[i] = (float)i;
        transpose_naive(n, s, d1);
        transpose_blocked(n, 8, s, d2);
        CHECK(memcmp(d1, d2, sizeof(float) * n * n) == 0);
        CHECK(d1[5 * n + 3] == s[3 * n + 5]);
        transpose_blocked(n, 24, s, d2);   /* T 가 n 의 약수가 아니어도 */
        CHECK(memcmp(d1, d2, sizeof(float) * n * n) == 0);
        uint64_t mn = sim_transpose(n, 0, 0, 16384, 32, 4), mb = sim_transpose(n, 8, 0, 16384, 32, 4);
        printf("      naive %llu misses, blocked T=8 %llu (compulsory %d)\n",
               (unsigned long long)mn, (unsigned long long)mb, 2 * n * n * 4 / 32);
        CHECK(mb * 2 < mn);
        CHECK(mb == (uint64_t)(2 * n * n * 4 / 32));   /* 블록 전치는 compulsory 만 남는다 */
        free(s); free(d1); free(d2);
    }

    printf("-- 연습 3: packing the B tile (M7-like 16 KB 4-way 32 B, float, n=128, ld=128 unpadded) --\n");
    {
        int n = 128;
        uint64_t plain = sim_tiled(n, n, 32, false, 4, 16384, 32, 4);
        uint64_t packed = sim_tiled(n, n, 32, true, 4, 16384, 32, 4);
        uint64_t padded = sim_tiled(n, 136, 32, false, 4, 16384, 32, 4);
        printf("      T=32: ld=128 plain %llu, ld=128 packed %llu, ld=136 padded %llu\n",
               (unsigned long long)plain, (unsigned long long)packed, (unsigned long long)padded);
        CHECK(packed * 2 < plain);
        CHECK(packed < padded + padded / 4);   /* 패딩과 비슷하거나 더 좋다 */

        float *A = malloc(sizeof(float) * n * n), *B = malloc(sizeof(float) * n * n);
        float *C1 = malloc(sizeof(float) * n * n), *C2 = malloc(sizeof(float) * n * n), *P = malloc(sizeof(float) * 48 * 48);
        uint32_t x = 2463534242u;
        for (int i = 0; i < n * n; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5; A[i] = (float)((int)(x % 9) - 4);
            x ^= x << 13; x ^= x >> 17; x ^= x << 5; B[i] = (float)((int)(x % 9) - 4);
        }
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                float s = 0;
                for (int k = 0; k < n; k++) s += A[i * n + k] * B[k * n + j];
                C1[i * n + j] = s;
            }
        mm_tiled_packed(n, 32, A, B, C2, P);
        CHECK(memcmp(C1, C2, sizeof(float) * n * n) == 0);
        mm_tiled_packed(n, 48, A, B, C2, P);   /* 48 은 128 의 약수가 아니다 → 가장자리 타일 */
        CHECK(memcmp(C1, C2, sizeof(float) * n * n) == 0);
        free(A); free(B); free(C1); free(C2); free(P);
    }

    printf("-- 연습 4: GFLOP/s and traffic estimates (n=2048, double, L=128) --\n");
    {
        CHECK(gflops(2048, 1.0) > 17.17 && gflops(2048, 1.0) < 17.18);   /* 2*2048^3 = 17.18e9 */
        double bi = bytes_ijk(2048, 128), bk = bytes_ikj(2048, 8), bt = bytes_tiled(2048, 64, 8);
        printf("      ijk %.1f GB, ikj %.1f GB, tiled T=64 %.2f GB → flop/byte %.3f / %.3f / %.2f\n",
               bi / 1e9, bk / 1e9, bt / 1e9, 2.0 * 2048.0 * 2048 * 2048 / bi, 2.0 * 2048.0 * 2048 * 2048 / bk,
               2.0 * 2048.0 * 2048 * 2048 / bt);
        CHECK(bi / bk == 16.0);
        CHECK(bk / bt > 31.0 && bk / bt < 32.0);   /* ikj 대비 약 T/2 = 32 배 적다 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
