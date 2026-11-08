/* SIV-M03 추가 연습 — 한 번에 검증
 *  1. log2 히스토그램 (bpftrace hist() 식 2의 거듭제곱 bucket) 과 분위수 상한
 *  2. N-bit serial number 차이 (12-bit / 16-bit / 4-bit seq, 반 바퀴 모호성)
 *  3. "꼬리 사건을 보려면 이벤트가 몇 개 필요한가" — n >= ln(1-CL)/ln(1-p), rule of three
 *  4. FIFO 없는 카운터 전용 IP: ISR 이 읽은 EVT_CNT 차이로 정상/합쳐짐/spurious 집계
 * <math.h> 사용: macOS 는 libm 이 libSystem 에 있어 -lm 없이 링크된다 (Linux 는 -lm 추가).
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/* ---------- 1. log2 히스토그램 ---------- */
/* bucket 0 = {0}, bucket b>=1 = [2^(b-1), 2^b - 1]. 32-bit 값이면 bucket 0..32 */
static unsigned log2_bucket(uint32_t v) {
    unsigned b = 0;
    while (v) { b++; v >>= 1; }
    return b;
}
static uint32_t log2_upper(unsigned b) { return b == 0 ? 0 : b >= 32 ? UINT32_MAX : (1u << b) - 1u; }
typedef struct { uint32_t n, cnt[33]; } l2hist_t;
static uint32_t l2_quantile(const l2hist_t *h, uint32_t num, uint32_t den) {
    uint64_t r = ((uint64_t)h->n * num + den - 1) / den, c = 0;
    if (r == 0) r = 1;
    for (unsigned b = 0; b < 33; b++) { c += h->cnt[b]; if (c >= r) return log2_upper(b); }
    return UINT32_MAX;
}

/* ---------- 2. N-bit serial 차이 ---------- */
/* (a - b) mod 2^bits 를 [-2^(bits-1), 2^(bits-1)-1] 로. bits 는 1..31 */
static int32_t serial_diff(uint32_t a, uint32_t b, unsigned bits) {
    uint32_t mask = (1u << bits) - 1u, half = 1u << (bits - 1);
    uint32_t u = (a - b) & mask;
    return u < half ? (int32_t)u : (int32_t)u - (int32_t)(mask + 1u);
}

/* ---------- 3. 꼬리 사건 관측에 필요한 이벤트 수 ---------- */
/* 확률 p 인 사건(예: latency > 예산)을 신뢰도 CL 로 '적어도 한 번' 보려면 1-(1-p)^n >= CL */
static uint64_t events_needed(double p, double cl) { return (uint64_t)ceil(log1p(-cl) / log1p(-p)); }
/* n 번 동안 0 번 봤을 때 CL 로 말할 수 있는 p 의 상한: 1-(1-p)^n = CL → p = 1 - (1-CL)^(1/n) */
static double p_upper_zero(uint64_t n, double cl) { return -expm1(log1p(-cl) / (double)n); }

/* ---------- 4. 카운터 전용 IP 의 합쳐짐 집계 ---------- */
typedef struct { uint32_t normal, coalesced_entries, merged, spurious, total; } cntacc_t;
/* start = 테스트 시작 시 EVT_CNT, reads[i] = i 번째 ISR 진입에서 읽은 EVT_CNT (16-bit wrap) */
static void cnt_account(uint16_t start, const uint16_t *reads, int n, cntacc_t *a) {
    memset(a, 0, sizeof *a);
    uint16_t last = start;
    for (int i = 0; i < n; i++) {
        uint32_t d = (uint16_t)(reads[i] - last);
        if (d == 0) a->spurious++;
        else if (d == 1) a->normal++;
        else { a->coalesced_entries++; a->merged += d - 1; }
        a->total += d;
        last = reads[i];
    }
}

int main(void) {
    /* 1 */
    CHECK(log2_bucket(0) == 0 && log2_bucket(1) == 1 && log2_bucket(2) == 2 && log2_bucket(3) == 2);
    CHECK(log2_bucket(4) == 3 && log2_bucket(1023) == 10 && log2_bucket(1024) == 11 && log2_bucket(UINT32_MAX) == 32);
    CHECK(log2_upper(0) == 0 && log2_upper(10) == 1023 && log2_upper(32) == UINT32_MAX);
    {
        static uint32_t xs[2000];
        l2hist_t h = {0};
        uint32_t rs = 12345u;
        for (int i = 0; i < 2000; i++) {
            xs[i] = xs32(&rs) % 20 == 0 ? xs32(&rs) % 30000u : 120u + xs32(&rs) % 900u;
            h.cnt[log2_bucket(xs[i])]++; h.n++;
        }
        qsort(xs, 2000, sizeof xs[0], cmp_u32);
        uint32_t qn[] = {1, 9, 99, 999}, qd[] = {2, 10, 100, 1000};
        int ok = 0;
        for (int i = 0; i < 4; i++) {
            uint64_t r = (2000ull * qn[i] + qd[i] - 1) / qd[i];
            uint32_t x = xs[r - 1], q = l2_quantile(&h, qn[i], qd[i]);
            ok += x <= q && q <= 2u * x + 1u;          /* 상대 오차 < 2배 */
            printf("  log2 p=%u/%u exact=%u upper=%u\n", qn[i], qd[i], x, q);
        }
        CHECK(ok == 4);
    }

    /* 2 */
    CHECK(serial_diff(0x000, 0xFFF, 12) == 1 && serial_diff(0xFFF, 0x000, 12) == -1);
    CHECK(serial_diff(0x7FF, 0x000, 12) == 2047 && serial_diff(0x800, 0x000, 12) == -2048);  /* 반 바퀴: 모호 */
    CHECK(serial_diff(0x0001, 0xFFFF, 16) == 2 && serial_diff(3, 14, 4) == 5 && serial_diff(14, 3, 4) == -5);
    CHECK(serial_diff(0x123, 0x123, 12) == 0);

    /* 3 */
    uint64_t n1 = events_needed(1e-4, 0.95), n2 = events_needed(1e-6, 0.99);
    double pu = p_upper_zero(10000, 0.95);
    printf("  events_needed(p=1e-4, 95%%)=%llu  events_needed(p=1e-6, 99%%)=%llu  p_upper(0 in 1e4, 95%%)=%.3e\n",
           (unsigned long long)n1, (unsigned long long)n2, pu);
    CHECK(n1 == 29956);
    CHECK(n2 >= 4605168 && n2 <= 4605171);
    CHECK(pu > 2.99e-4 && pu < 3.0e-4);                 /* rule of three: 3/n */

    /* 4 */
    {
        uint16_t reads[] = {0xFFFF, 0x0001, 0x0001, 0x0002, 0x0005};
        cntacc_t a;
        cnt_account(0xFFFE, reads, 5, &a);
        CHECK(a.normal == 2 && a.coalesced_entries == 2 && a.merged == 3 && a.spurious == 1);
        CHECK(a.total == 7 && a.total == (uint16_t)(0x0005 - 0xFFFE));
        CHECK(a.normal + a.coalesced_entries + a.spurious == 5);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
