/* BM-M09 추가 연습 — 한 번에 검증
 *
 * 연습 1  NDTR + 자기 플래그 → "반쪽 h 에 새 샘플이 몇 개 써졌나" (0..N), 전 상태 전수 비교
 * 연습 2  다채널 스캔(인터리브) 버퍼: 반쪽이 프레임 경계로 나뉘는지 검사 + 채널별 평균
 * 연습 3  첫 OVR 반쪽 번호와 OVR 주기의 닫힌 식 vs 사건 시뮬레이션 (전수 격자)
 * 연습 4  F4 이중 버퍼 모드(DBM/CT) 덮어쓰기 판정 + TC 플래그 하나라서 생기는 사각지대
 * 연습 5  예산 역산: 지연 한도 안의 최대 N, 부하 한도 안의 최소 N (BM-M09 budget_calc 축약판)
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t rng = 0x2545F491u;                              /* xorshift32, 고정 시드 */
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* ======================= 연습 1 ======================= */
/* circular, 버퍼 2N, ISR 진입 때 h 의 플래그를 지웠다. 지금 NDTR 과 h 자신의 플래그(HT 또는 TC)로
 * 반쪽 h 에 새로 써진 샘플 수를 돌려준다. 한 바퀴 이상(자기 플래그 다시 섬)이면 N. */
static uint32_t written_into(uint32_t h, uint32_t ndtr, bool self_flag, uint32_t n) {
    if (self_flag) return n;
    uint32_t pos = 2u * n - ndtr;                                /* ndtr = 2N → pos 0 */
    return (pos / n == h) ? pos % n : 0u;
}
/* 기준 모델: h 가 찬 직후부터 k 번 전송한 상태 */
static void model_state(uint32_t h, uint32_t k, uint32_t n, uint32_t *ndtr, bool *self) {
    uint32_t pos = ((1u - h) * n + k) % (2u * n);                /* h 가 찬 직후 DMA 는 다른 반쪽 시작 */
    *ndtr = 2u * n - pos;
    *self = k >= 2u * n;                                         /* 다른 반쪽 N 개 + h 의 N 개 */
}
static void ex1(void) {
    uint32_t bad = 0, cases = 0;
    for (uint32_t n = 2; n <= 64; n++) for (uint32_t h = 0; h < 2; h++) for (uint32_t k = 0; k <= 5u * n; k++) {
        uint32_t ndtr; bool self;
        model_state(h, k, n, &ndtr, &self);
        uint32_t expect = k <= n ? 0u : (k >= 2u * n ? n : k - n);
        if (written_into(h, ndtr, self, n) != expect) bad++;
        cases++;
    }
    CHECK(bad == 0u && cases == 20916u);                         /* N=2..64, h=0/1, k=0..5N */
    /* 손 계산 예: N=8, h=0 → NDTR=8 은 "h 첫 칸 직전" (0 개), NDTR=7 은 1 개 */
    CHECK(written_into(0, 16, false, 8) == 0u && written_into(0, 8, false, 8) == 0u && written_into(0, 15, false, 8) == 1u);
    CHECK(written_into(1, 8, false, 8) == 0u && written_into(1, 7, false, 8) == 1u && written_into(1, 16, true, 8) == 8u);
}

/* ======================= 연습 2 ======================= */
/* 버퍼 = [ch0 ch1 .. ch(C−1)] 프레임의 반복, 길이 total(= DMA NDTR). HT/TC 반쪽이 프레임을 자르면 안 된다. */
static bool scan_layout_ok(uint32_t total, uint32_t n_ch) {
    return n_ch >= 1u && total >= 2u * n_ch && total % 2u == 0u && (total / 2u) % n_ch == 0u;
}
/* 반쪽 h 의 채널별 평균 (반올림). 레이아웃이 틀리면 -1. */
static int scan_half_means(const uint16_t *buf, uint32_t total, uint32_t n_ch, uint32_t h, uint16_t *out) {
    if (!scan_layout_ok(total, n_ch) || h > 1u) return -1;
    uint32_t half = total / 2u, frames = half / n_ch;
    for (uint32_t c = 0; c < n_ch; c++) {
        uint32_t sum = 0;
        for (uint32_t f = 0; f < frames; f++) sum += buf[h * half + f * n_ch + c];
        out[c] = (uint16_t)((sum + frames / 2u) / frames);
    }
    return 0;
}
static void ex2(void) {
    static uint16_t buf[512];
    uint32_t bad = 0;
    for (int it = 0; it < 2000; it++) {
        uint32_t n_ch = 1u + xs32() % 6u, frames = 1u + xs32() % 40u, total = 2u * frames * n_ch;
        for (uint32_t i = 0; i < total; i++) buf[i] = (uint16_t)(1000u * (i % n_ch) + xs32() % 1000u);
        uint16_t out[8];
        uint32_t h = xs32() & 1u;
        if (scan_half_means(buf, total, n_ch, h, out) != 0) { bad++; continue; }
        for (uint32_t c = 0; c < n_ch; c++) {                    /* 기준: 인덱스로 채널을 직접 판정 */
            uint32_t sum = 0, cnt = 0;
            for (uint32_t i = h * total / 2u; i < (h + 1u) * total / 2u; i++) if (i % n_ch == c) { sum += buf[i]; cnt++; }
            if (cnt != frames || out[c] != (sum + cnt / 2u) / cnt || out[c] / 1000u != c) bad++;
        }
    }
    CHECK(bad == 0u);
    /* 3채널인데 NDTR = 2·16 = 32 → 반쪽 16 이 프레임(3)으로 안 나뉨: 뒤 반쪽이 ch1 부터 시작 */
    CHECK(!scan_layout_ok(32u, 3u) && scan_layout_ok(36u, 3u) && !scan_layout_ok(30u, 4u));
    uint16_t o[3];
    CHECK(scan_half_means(buf, 32u, 3u, 0u, o) == -1);
}

/* ======================= 연습 3 ======================= */
/* 본문 정책: 반쪽 i 는 R_i = (i+1)·N·S 에 참. 처리 비용 C. 끝 시각 − R_i ≥ (N+1)·S 면 OVR → 그때까지
 * 찬 반쪽은 모두 버림. ISR 은 펜딩이면 바로 다음(tail-chain). 시작 시 다음 반쪽도 이미 찼으면 late. */
static int32_t first_ovr(uint32_t n, uint32_t s, uint32_t c) {
    if (c <= n * s) return -1;
    uint32_t d = c - n * s;
    return (int32_t)((s + d - 1u) / d) - 1;
}
static uint32_t ovr_period(uint32_t n, uint32_t s, uint32_t c) {   /* OVR 1 번 + 정상 i* 번 + 버림 */
    int32_t i = first_ovr(n, s, c);
    if (i < 0) return 0u;
    uint32_t lat = (uint32_t)i * (c - n * s);
    return (uint32_t)i + 1u + (lat + c) / (n * s);
}
typedef struct { int32_t first; uint32_t records, ovr, late; } evsim_t;
static evsim_t event_sim(uint32_t n, uint32_t s, uint32_t c, uint32_t halves) {
    evsim_t r = { -1, 0, 0, 0 };
    uint64_t t_free = 0, drop_until = 0;
    for (uint32_t i = 0; i < halves; i++) {
        uint64_t R = (uint64_t)(i + 1u) * n * s;
        if (R <= drop_until) continue;                           /* resync 로 플래그가 지워진 반쪽 */
        uint64_t st = R > t_free ? R : t_free;
        if (R + (uint64_t)n * s <= st) { r.late++; drop_until = st; t_free = st; continue; }
        uint64_t end = st + c;
        if (end - R >= (uint64_t)(n + 1u) * s) {
            if (r.first < 0) r.first = (int32_t)r.records;
            r.ovr++; drop_until = end;
        }
        r.records++; t_free = end;
    }
    return r;
}
static void ex3(void) {
    uint32_t bad = 0, grid = 0, late = 0;
    for (uint32_t n = 2; n <= 16; n++) for (uint32_t s = 1; s <= 12; s++) for (uint32_t c = 0; c <= 3u * n * s; c++) {
        evsim_t e = event_sim(n, s, c, 400u);
        late += e.late;
        if (e.first != first_ovr(n, s, c)) bad++;
        uint32_t p = ovr_period(n, s, c);
        if (p) {                                                 /* 주기적: OVR 수 = 레코드 주기 수 */
            uint32_t full = 400u / p, rem = 400u % p, recs_per = (uint32_t)first_ovr(n, s, c) + 1u;
            uint32_t ovr = full + (rem > (uint32_t)first_ovr(n, s, c) ? 1u : 0u);
            uint32_t recs = full * recs_per + (rem < recs_per ? rem : recs_per);
            if (e.ovr != ovr || e.records != recs) bad++;
        } else if (e.ovr || e.records != 400u) bad++;
        grid++;
    }
    CHECK(bad == 0u && grid > 10000u && late == 0u);             /* 막힘 주입이 없으면 late 는 안 생긴다 */
    /* 본문 demo 표와 같은 값: N=32, S=16 */
    CHECK(first_ovr(32, 16, 513) == 15 && ovr_period(32, 16, 513) == 17u);
    CHECK(first_ovr(32, 16, 520) == 1 && ovr_period(32, 16, 520) == 3u);
    CHECK(ovr_period(32, 16, 528) == 2u && ovr_period(32, 16, 1024) == 3u && ovr_period(32, 16, 1536) == 4u);
    evsim_t e = event_sim(32, 16, 513, 1000u);
    CHECK(e.records == 942u && e.ovr == 58u);    /* demo 표 941: 마지막 반쪽 ISR 이 시뮬레이션 끝 시각 뒤라 잘림 */
}

/* ======================= 연습 4 ======================= */
/* DBM: 버퍼 M0/M1 각 M 칸. 버퍼 b 가 차면 TC + CT 토글(이제 !b 에 씀) + NDTR=M 재장전.
 * ISR 은 TC 를 지우고 b 를 처리. 끝에서 CT, NDTR, TCIF 를 읽어 "b 에 새 샘플이 써졌나" 를 판정. */
static bool dbm_overrun(uint32_t b, uint32_t ct, uint32_t ndtr, uint32_t m, bool tcif) {
    return (ct == b && ndtr != m) || (tcif && ct != b);
}
static void dbm_state(uint32_t b, uint32_t k, uint32_t m, uint32_t *ct, uint32_t *ndtr, bool *tcif) {
    uint32_t done = k / m;                                       /* b 이후 다 찬 버퍼 수 */
    *ct = (b + 1u + done) & 1u;
    *ndtr = m - k % m;
    *tcif = done >= 1u;
}
static void ex4(void) {
    uint32_t bad = 0, blind = 0;
    for (uint32_t m = 1; m <= 64; m++) for (uint32_t b = 0; b < 2; b++) for (uint32_t k = 0; k <= 4u * m; k++) {
        uint32_t ct, nd; bool tc;
        dbm_state(b, k, m, &ct, &nd, &tc);
        bool got = dbm_overrun(b, ct, nd, m, tc), truth = k >= m + 1u;
        if (k < 3u * m && got != truth) bad++;
        if (k == 3u * m && got) bad++;                           /* 사각지대: k=M 과 같은 모습 */
        if (got != truth) blind++;
    }
    CHECK(bad == 0u);
    CHECK(blind == 2u * 64u);                                    /* 각 (m, b) 에서 정확히 k = 3M 하나 */
    uint32_t ct1, nd1, ct3, nd3; bool tc1, tc3;
    dbm_state(0, 8, 8, &ct1, &nd1, &tc1); dbm_state(0, 24, 8, &ct3, &nd3, &tc3);
    CHECK(ct1 == ct3 && nd1 == nd3 && tc1 == tc3);              /* 1 버퍼 뒤 vs 3 버퍼 뒤: 레지스터가 똑같다 */
}

/* ======================= 연습 5 ======================= */
/* (BM-M09 budget_calc 축약판) */
static bool budget(uint32_t fs, uint32_t n, uint32_t cps, uint32_t fixed, uint32_t cpu,
                   uint64_t *lat_ns, uint32_t *load_pm) {
    uint64_t cyc = (uint64_t)n * cps + fixed;
    uint64_t proc_ns = (cyc * 1000000000ull + cpu / 2u) / cpu;
    *lat_ns = ((uint64_t)(n - 1u) * 1000000000ull + fs / 2u) / fs + proc_ns;
    uint64_t num = cyc * fs * 1000u, den = (uint64_t)cpu * n;
    *load_pm = (uint32_t)((num + den / 2u) / den);
    return cyc * fs <= (uint64_t)n * cpu;
}
/* 지연 ≤ lat_max 이고 ok 인 최대 N (2..nmax). 지연은 N 에 대해 증가 → 위에서부터 첫 만족. 없으면 0. */
static uint32_t max_n_for_latency(uint32_t fs, uint64_t lat_max, uint32_t cps, uint32_t fixed, uint32_t cpu, uint32_t nmax) {
    uint32_t lo = 2u, hi = nmax, best = 0u;
    while (lo <= hi) {                                           /* 이분 탐색 (지연 단조 증가) */
        uint32_t mid = lo + (hi - lo) / 2u; uint64_t lat; uint32_t pm;
        budget(fs, mid, cps, fixed, cpu, &lat, &pm);
        if (lat <= lat_max) { best = mid; lo = mid + 1u; } else { if (mid == 0u) break; hi = mid - 1u; }
    }
    if (best) { uint64_t lat; uint32_t pm; if (!budget(fs, best, cps, fixed, cpu, &lat, &pm)) best = 0u; }
    return best;
}
/* 부하 ≤ pm_max 인 최소 N (고정 비용이 N 개 샘플에 나눠짐 → 부하는 N 에 대해 감소). 없으면 0. */
static uint32_t min_n_for_load(uint32_t fs, uint32_t pm_max, uint32_t cps, uint32_t fixed, uint32_t cpu, uint32_t nmax) {
    uint32_t lo = 2u, hi = nmax, best = 0u;
    while (lo <= hi) {
        uint32_t mid = lo + (hi - lo) / 2u; uint64_t lat; uint32_t pm;
        budget(fs, mid, cps, fixed, cpu, &lat, &pm);
        if (pm <= pm_max) { best = mid; hi = mid - 1u; } else lo = mid + 1u;
    }
    return best;
}
static void ex5(void) {
    uint32_t bad = 0;
    for (int it = 0; it < 300; it++) {
        uint32_t fs = 1000u + xs32() % 2000000u, cps = xs32() % 300u, fixed = xs32() % 5000u, cpu = 168000000u;
        uint64_t lat_max = 10000u + xs32() % 5000000u; uint32_t pm_max = 100u + xs32() % 900u;
        uint32_t bl = 0, bm = 0;
        for (uint32_t n = 2; n <= 2048u; n++) {                  /* brute force */
            uint64_t lat; uint32_t pm; bool ok = budget(fs, n, cps, fixed, cpu, &lat, &pm);
            if (lat <= lat_max && ok) bl = n;
            if (pm <= pm_max && bm == 0u) bm = n;
        }
        if (max_n_for_latency(fs, lat_max, cps, fixed, cpu, 2048u) != bl) bad++;
        if (min_n_for_load(fs, pm_max, cps, fixed, cpu, 2048u) != bm) bad++;
    }
    CHECK(bad == 0u);
    /* 예: 1 MSPS, 40 cyc/샘플 + 600 cyc, 168 MHz. 지연 300 µs 이하 → N 최대? 부하 30 % 이하 → N 최소? */
    uint32_t a = max_n_for_latency(1000000u, 300000u, 40u, 600u, 168000000u, 4096u);
    uint32_t b = min_n_for_load(1000000u, 300u, 40u, 600u, 168000000u, 4096u);
    printf("1 MSPS: max N for latency<=300us = %u, min N for load<=30%% = %u\n", (unsigned)a, (unsigned)b);
    CHECK(a == 240u && b == 58u);
}

int main(void) {
    ex1(); ex2(); ex3(); ex4(); ex5();
    printf("%u/%u passed\n", (unsigned)g_pass, (unsigned)g_total);
    return g_pass == g_total ? 0 : 1;
}
