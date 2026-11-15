/* ARM-M03 추가 연습 — 한 번에 검증
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S -o /tmp/ARM-M03_practice
 */
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
    else printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

/* ---- 연습 1: 해상도 → 필요한 반복 수 ----
 * 카운터 1 tick = tick_ps, 명령 1개 ≈ op_ps, 허용 양자화 오차 err_ppm (1e6 = 100%).
 * 구간 길이 ≥ tick / 오차 가 되도록 하는 최소 iters (반복당 16개). 입력 0 → 0 */
static uint64_t ceil_div(uint64_t a, uint64_t b) { return a / b + (a % b != 0); }
uint64_t iters_for_error(uint64_t tick_ps, uint64_t op_ps, uint64_t err_ppm) {
    if (!tick_ps || !op_ps || !err_ppm) return 0;
    uint64_t need_ps = ceil_div(tick_ps * 1000000u, err_ppm);   /* tick_ps ≤ 2^40 가정 */
    uint64_t ops = ceil_div(need_ps, op_ps);
    return ceil_div(ops, 16);
}

/* ---- 연습 2: Cortex-M 카운터 경과 시간 ----
 * SysTick: 24비트 down-counter, reload → 0 → reload ... (주기 reload+1). 한 주기 안의 경과 tick.
 * DWT->CYCCNT: 32비트 up-counter, wrap 은 unsigned 뺄셈이 처리 */
uint32_t systick_elapsed(uint32_t start, uint32_t end, uint32_t reload) {
    return start >= end ? start - end : start + (reload + 1u) - end;
}
uint32_t cyccnt_elapsed(uint32_t t0, uint32_t t1) { return t1 - t0; }

/* ---- 연습 3: 꼬리 지연 — nearest-rank 백분위 (a 를 정렬함). p ∈ [0,100], n==0 → 0 ---- */
static void isort(uint64_t *a, size_t n) {
    for (size_t i = 1; i < n; i++) { uint64_t v = a[i]; size_t j = i; while (j && a[j - 1] > v) { a[j] = a[j - 1]; j--; } a[j] = v; }
}
uint64_t u64_percentile(uint64_t *a, size_t n, unsigned p) {
    if (n == 0) return 0;
    isort(a, n);
    size_t rank = (size_t)ceil_div((uint64_t)p * n, 100);       /* 1-based */
    return a[rank ? rank - 1 : 0];
}

/* ---- 연습 4, 5: asm (practice.S) ---- */
uint64_t lat_ldr_idx(uint64_t iters, uint64_t i0, const uint64_t *tab);
double   tput_fadd4(uint64_t iters, double x, double y);

/* 연습 5 보조: 필요한 사슬 수와 사슬 k 개일 때 기대 처리량(밀리사이클/명령) = max(T, L/k) */
uint64_t chains_needed(uint64_t lat_mcyc, uint64_t rtp_mcyc) { return rtp_mcyc ? ceil_div(lat_mcyc, rtp_mcyc) : 0; }
uint64_t expected_mcyc(uint64_t lat_mcyc, uint64_t rtp_mcyc, uint64_t k) {
    uint64_t bound = k ? ceil_div(lat_mcyc, k) : lat_mcyc;
    return bound > rtp_mcyc ? bound : rtp_mcyc;
}

int main(void) {
    /* 1 */
    CHECK(iters_for_error(41667, 250, 1000) == 10417);           /* 24 MHz 눈금, 0.1% → 16.7만 add */
    CHECK(iters_for_error(41667, 250, 10000) == 1042);           /* 1% */
    CHECK(iters_for_error(1000, 250, 1000) == 250);              /* 진짜 1 ns 카운터라면 */
    CHECK(iters_for_error(0, 250, 1000) == 0 && iters_for_error(41667, 250, 0) == 0);
    /* 2 */
    CHECK(systick_elapsed(100, 40, 0xFFFFFF) == 60);
    CHECK(systick_elapsed(5, 0xFFFFF0, 0xFFFFFF) == 21);          /* 0 을 지나 reload 로 */
    CHECK(systick_elapsed(777, 777, 0xFFFFFF) == 0);
    CHECK(systick_elapsed(10, 990, 999) == 20);                  /* 1 ms 틱(1 MHz, reload 999) */
    CHECK(cyccnt_elapsed(0xFFFFFFF0u, 0x10u) == 0x20u);
    CHECK(cyccnt_elapsed(5, 5) == 0);
    /* 3 */
    {
        uint64_t a[10] = {7, 3, 10, 1, 9, 2, 8, 4, 6, 5};
        CHECK(u64_percentile(a, 10, 50) == 5 && u64_percentile(a, 10, 90) == 9);
        CHECK(u64_percentile(a, 10, 100) == 10 && u64_percentile(a, 10, 0) == 1);
        CHECK(u64_percentile(a, 0, 50) == 0);
        uint64_t b[3] = {300, 100, 200};
        CHECK(u64_percentile(b, 3, 99) == 300 && u64_percentile(b, 3, 34) == 200);
    }
    /* 4: 무작위 순환 순열 64칸 */
    {
        uint64_t perm[64], tab[64];
        for (int i = 0; i < 64; i++) perm[i] = (uint64_t)i;
        for (int i = 63; i > 0; i--) { int j = (int)(rnd() % (uint64_t)(i + 1)); uint64_t t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
        for (int i = 0; i < 64; i++) tab[perm[i]] = perm[(i + 1) % 64];
        int bad = 0;
        static const uint64_t ITS[] = {0, 1, 3, 4, 100};
        for (size_t k = 0; k < 5; k++) {
            uint64_t i = perm[5];
            for (uint64_t s = 0; s < ITS[k] * 16; s++) i = tab[i];
            if (lat_ldr_idx(ITS[k], perm[5], tab) != i) bad++;
        }
        CHECK(bad == 0);
        CHECK(lat_ldr_idx(4, perm[0], tab) == perm[0]);          /* 64스텝 = 한 바퀴 */
    }
    /* 5 */
    {
        int bad = 0;
        static const uint64_t ITS[] = {0, 1, 2, 9, 1000};
        for (size_t k = 0; k < 5; k++) {
            double x = 1.0 + (double)(rnd() >> 12) * 0x1p-52, y = (double)(rnd() >> 12) * 0x1p-60, c = x;
            for (uint64_t i = 0; i < ITS[k] * 4; i++) c += y;
            double want = c + c; want += c; want += c;
            double got = tput_fadd4(ITS[k], x, y);
            if (memcmp(&got, &want, sizeof got)) bad++;
        }
        CHECK(bad == 0);
        CHECK(chains_needed(2500, 250) == 10 && chains_needed(3000, 330) == 10);
        CHECK(expected_mcyc(2500, 250, 4) == 625);               /* 사슬 4개: L/4 가 한계 */
        CHECK(expected_mcyc(2500, 250, 16) == 250);              /* 16개: 유닛 처리량이 한계 */
        CHECK(expected_mcyc(3000, 330, 0) == 3000);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
