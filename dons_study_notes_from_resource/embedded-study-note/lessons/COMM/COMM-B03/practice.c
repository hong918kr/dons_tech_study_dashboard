/* COMM-B03 추가 연습 — 한 번에 검증
 *
 * 연습 1: FPU 없는 MCU 용 정수 ppm 오차 (double 버전과 대조)
 * 연습 2: 8x 오버샘플링(샘플 4,5,6)의 허용 한계 — 이론 + 시뮬레이션
 * 연습 3: 8N2 로 보내고 8N1 로 받기 (back-to-back) — 두 번째 stop 이 빠른 쪽 한계를 얼마나 올리나
 * 연습 4: "매직 크리스털" — 표준 baud 를 오차 0 으로 내는 클럭과 최대 정확 baud
 * 연습 5: 발진기 허용 오차 예산 — MCU 클럭이 ±x% 흔들려도 되는 최대 x (닫힌 식 vs 이분 탐색 vs 시뮬레이션)
 *
 * 본문 코드(baud_calc, 수신기, sim_link, tol_theory)는 solution.c 와 같은 것을 아래에 복사해 둔다 (자기 완결).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 본문 코드 (solution.c 와 동일) ---------------- */
enum { BAUD_OK = 0, BAUD_EARG = -1, BAUD_ETOO_FAST = -2, BAUD_ETOO_SLOW = -3 };

typedef struct {
    uint32_t div;      /* 분주값 N. frac_bits = F 이면 N = USARTDIV × 2^F (정수 분주는 F = 0) */
    double   actual;   /* 실제 baud = fclk·2^F / (ovs·N) */
    double   err_pct;  /* (actual − baud) / baud × 100. + 면 목표보다 빠르다 */
} baud_res;

/* N = round(fclk·2^F / (ovs·baud)) (반올림은 half-up, 정수 연산).
 * ovs ∉ {8,16}, baud = 0, fclk = 0, F > 4 → BAUD_EARG. N < 2^F (분주 < 1) → BAUD_ETOO_FAST.
 * N > div_max → BAUD_ETOO_SLOW. 실패하면 *r 은 건드리지 않는다. */
static int baud_calc(uint32_t fclk, uint32_t baud, unsigned ovs, unsigned frac_bits, uint32_t div_max, baud_res *r) {
    if (!r || fclk == 0 || baud == 0 || (ovs != 8 && ovs != 16) || frac_bits > 4) return BAUD_EARG;
    uint64_t num = (uint64_t)fclk << frac_bits;          /* fclk·2^F */
    uint64_t den = (uint64_t)ovs * baud;                 /* ovs·baud */
    uint64_t n = (2 * num + den) / (2 * den);            /* round(num/den), half-up */
    if (n < (1u << frac_bits)) return BAUD_ETOO_FAST;    /* 가수 0 → 분주 불가 (n = 0 도 여기서 걸린다) */
    if (n > div_max) return BAUD_ETOO_SLOW;
    r->div = (uint32_t)n;
    r->actual = (double)num / ((double)ovs * (double)n);
    r->err_pct = (r->actual - (double)baud) / (double)baud * 100.0;
    return BAUD_OK;
}


/* ================= Part B: 클럭 편차 실험 ================= */
/* --- 프레임 형식과 송신 비트 열 (COMM-B01 모델 축약판) --- */
typedef struct { int data_bits; char parity; int stop_bits; } ufmt;   /* parity 'N' / 'E' / 'O' */

static int frame_encode(const ufmt *f, uint16_t v, uint8_t *bits) {  /* LSB first, start 0, stop 1 */
    int n = 0, ones = 0;
    bits[n++] = 0;
    for (int i = 0; i < f->data_bits; i++) { int b = (v >> i) & 1; ones += b; bits[n++] = (uint8_t)b; }
    if (f->parity != 'N') bits[n++] = (uint8_t)((ones & 1) ^ (f->parity == 'O'));
    for (int i = 0; i < f->stop_bits; i++) bits[n++] = 1;
    return n;
}

/* --- 오버샘플링 수신기 (COMM-B02 모델 축약판) ---
 * IDLE 에서 직전 샘플 1 → 이번 샘플 0 이면 이 샘플이 start 비트의 1번 샘플.
 * 비트 k 의 샘플 s (1..ovs) 중 m−1, m, m+1 (m = ovs/2 + 1: 16x → 8,9,10 / 8x → 4,5,6) 다수결.
 * 셋이 다르면 noise. start 다수결이 1 이면 false start → IDLE. 정지 비트는 첫 stop 하나만 본다. */
typedef struct {
    int ovs, data_bits; char parity;
    int busy, cnt, prev, ones, nf;
    uint16_t sh; int par;
} urx;
typedef struct { uint16_t data; uint8_t fe, pe, nf; } urx_out;

static void urx_init(urx *r, int ovs, const ufmt *f) {
    memset(r, 0, sizeof *r);
    r->ovs = ovs; r->data_bits = f->data_bits; r->parity = f->parity; r->prev = 1;
}

static int urx_step(urx *r, int lv, urx_out *o) {        /* 샘플 하나. 프레임이 끝나면 1 */
    int done = 0;
    if (!r->busy) {
        if (r->prev == 1 && lv == 0) { r->busy = 1; r->cnt = 0; r->ones = 0; r->nf = 0; r->sh = 0; r->par = 0; }
    }
    if (r->busy) {
        r->cnt++;
        int bit = (r->cnt - 1) / r->ovs, s = (r->cnt - 1) % r->ovs + 1, m = r->ovs / 2 + 1;
        int nbits = 1 + r->data_bits + (r->parity != 'N') + 1;
        if (s >= m - 1 && s <= m + 1) {
            r->ones += lv;
            if (s == m + 1) {
                int v = r->ones >= 2;
                if (r->ones == 1 || r->ones == 2) r->nf = 1;
                r->ones = 0;
                if (bit == 0) {
                    if (v) r->busy = 0;                                   /* false start */
                } else if (bit <= r->data_bits) {
                    r->sh |= (uint16_t)(v << (bit - 1)); r->par ^= v;
                } else if (bit < nbits - 1) {
                    r->par ^= v;                                          /* 패리티 비트 */
                } else {                                                  /* 첫 stop */
                    o->data = r->sh; o->fe = (uint8_t)!v; o->nf = (uint8_t)r->nf;
                    o->pe = (uint8_t)(r->parity == 'N' ? 0 : (r->par != (r->parity == 'O')));
                    r->busy = 0; done = 1;
                }
            }
        }
    }
    r->prev = lv;
    return done;
}

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 송신선 레벨: 프레임 비트 bits[0..n) 가 시각 t0 부터 비트 폭 p 로 나간다. 프레임 밖은 idle = 1. */
static int tx_level(const uint8_t *bits, int n, int64_t t0, int64_t p, int64_t t) {
    if (t < t0) return 1;
    int64_t k = (t - t0) / p;
    return k < n ? bits[k] : 1;
}

typedef struct { int nframes, rx_frames, ok, fe, pe, nf, err; } link_stats;
#define MAXF 4096

/* 링크 시뮬레이션: 송신 비트 폭 p_tx, 수신 샘플 주기 s_rx (같은 시간 단위). 프레임 nframes 개 (랜덤 데이터).
 * gap_bits = 0: back-to-back (stop 끝 = 다음 start). gap_bits = g > 0: 프레임 사이 g 비트 + 0..p_tx−1 랜덤 idle.
 * RX 샘플 격자의 시작 위상도 랜덤. err = 보낸 것과 다른 프레임 수 (+ 남는 프레임 수). */
static void sim_link(const ufmt *f, int ovs, int64_t p_tx, int64_t s_rx, int nframes, int gap_bits,
                     uint32_t seed, link_stats *st) {
    static uint16_t sent[MAXF];
    memset(st, 0, sizeof *st);
    st->nframes = nframes;
    if (nframes > MAXF) return;
    urx rx; urx_init(&rx, ovs, f);
    uint32_t s = seed ? seed : 1u;
    uint8_t bits[16]; int nb = 0, fi = 0;
    int64_t t0 = 0, tend = 3 * p_tx;                        /* 처음 3비트 idle */
    int64_t t = (int64_t)(xs32(&s) % (uint32_t)s_rx);       /* RX 격자 위상 */
    int64_t drain = 2 * 16 * (int64_t)ovs * s_rx;           /* 마지막 프레임 뒤 여유 */
    for (;;) {
        while (t >= tend && fi < nframes) {                  /* 다음 프레임을 시간표에 올린다 */
            int64_t gap = gap_bits > 0 ? gap_bits * p_tx + (int64_t)(xs32(&s) % (uint32_t)p_tx) : 0;
            uint16_t v = (uint16_t)(xs32(&s) & ((1u << f->data_bits) - 1));
            sent[fi++] = v;
            nb = frame_encode(f, v, bits);
            t0 = tend + gap; tend = t0 + nb * p_tx;
        }
        if (fi == nframes && t >= tend + drain) break;
        urx_out o;
        if (urx_step(&rx, tx_level(bits, nb, t0, p_tx, t), &o)) {
            int k = st->rx_frames++;
            st->fe += o.fe; st->pe += o.pe; st->nf += o.nf;
            if (k < nframes && o.data == sent[k] && !o.fe && !o.pe) st->ok++;
        }
        t += s_rx;
    }
    st->err = (nframes - st->ok) + (st->rx_frames > nframes ? st->rx_frames - nframes : 0);
}

/* 이론 한계 (%). 비트 j (start = 0) 의 샘플 m (1..ovs) 이 그 비트 안에 있어야 한다.
 * 샘플 m 의 위치 = (m − 1 + φ)/ovs 비트, φ ∈ (0, 1] = 에지에서 첫 0 샘플까지 지연 (샘플 단위).
 * late = 1 (TX 가 빠름, e > 0): 최악 φ = 1 → (1+e)(j + m/ovs) < j + 1 → e_max = (1 − m/ovs)/(j + m/ovs)
 * late = 0 (TX 가 느림, e < 0): 최악 φ → 0 → (1+e)(j + (m−1)/ovs) ≥ j → e_min = −((m−1)/ovs)/(j + (m−1)/ovs) */
static double tol_theory(int j, int m, int ovs, int late) {
    if (late) { double x = (double)m / ovs; return 100.0 * (1.0 - x) / (j + x); }
    double x = (double)(m - 1) / ovs;
    return -100.0 * x / (j + x);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------------- 연습 1: 정수 ppm ---------------- */
/* err_ppm = round(1e6 · (fclk·2^F − ovs·N·baud) / (ovs·N·baud)), N 은 baud_calc 와 같은 반올림. 실패 시 baud_calc 코드. */
static int err_ppm_int(uint32_t fclk, uint32_t baud, unsigned ovs, unsigned fb, uint32_t div_max, int32_t *ppm) {
    if (fclk == 0 || baud == 0 || (ovs != 8 && ovs != 16) || fb > 4) return BAUD_EARG;
    uint64_t num = (uint64_t)fclk << fb, den = (uint64_t)ovs * baud;
    uint64_t n = (2 * num + den) / (2 * den);
    if (n < (1u << fb)) return BAUD_ETOO_FAST;
    if (n > div_max) return BAUD_ETOO_SLOW;
    int64_t q = (int64_t)(ovs * n * baud);                 /* q ≈ num ≤ 2^36 (|q − num| ≤ den/2) */
    int64_t d = ((int64_t)num - q) * 1000000;              /* |num − q| ≤ den/2 < 2^36 → |d| < 2^56, 넘침 없음 */
    *ppm = (int32_t)(d >= 0 ? (d + q / 2) / q : -((-d + q / 2) / q));   /* 대칭 반올림 */
    return BAUD_OK;
}

/* ---------------- 연습 4: 매직 크리스털 ---------------- */
static const uint32_t STD_BAUD[] = { 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600 };
/* 정수 분주 ovs 로 오차 0 인 가장 큰 표준 baud (9600 이 안 되면 0) — 9600 부터 그 baud 까지 전부 0 이어야 한다 */
static uint32_t max_exact_baud(uint32_t fclk, unsigned ovs) {
    uint32_t best = 0;
    for (size_t i = 4; i < sizeof STD_BAUD / sizeof STD_BAUD[0]; i++) {
        if (fclk % (ovs * STD_BAUD[i]) != 0) break;
        best = STD_BAUD[i];
    }
    return best;
}

/* ---------------- 연습 5: 발진기 예산 ---------------- */
/* MCU 쪽 분주 오차 e0 (%), PC 쪽 이상적. 한계 [lo, hi] (%). MCU 클럭이 (1+δ), |δ| ≤ x 일 때
 * MCU→PC: (1+e0)(1+δ) − 1 ∈ (lo, hi),  PC→MCU: 1/((1+e0)(1+δ)) − 1 ∈ (lo, hi) 를 지키는 최대 x (%). */
static double max_osc_tol(double e0, double lo, double hi) {
    double a = 1 + e0 / 100, L = 1 + lo / 100, H = 1 + hi / 100, x[4];
    x[0] = H / a - 1;              /* MCU 빠름 → PC 가 받을 때 late */
    x[1] = 1 - L / a;              /* MCU 느림 → PC 가 받을 때 early */
    x[2] = 1 - 1 / (H * a);        /* MCU 느림 → MCU 가 받을 때 PC 가 상대적으로 빠름 */
    x[3] = 1 / (L * a) - 1;        /* MCU 빠름 → MCU 가 받을 때 PC 가 상대적으로 느림 */
    double m = x[0];
    for (int i = 1; i < 4; i++) if (x[i] < m) m = x[i];
    return m * 100;
}
static int osc_ok(double e0, double lo, double hi, double x) {   /* 이분 탐색용: 4 코너를 직접 계산 */
    double a = 1 + e0 / 100;
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        double t = a * (1 + sgn * x / 100);
        double e1 = (t - 1) * 100, e2 = (1 / t - 1) * 100;
        if (e1 >= hi || e1 <= lo || e2 >= hi || e2 <= lo) return 0;
    }
    return 1;
}

static int64_t p_for16(double e_pct) { return (int64_t)(16000.0 / (1.0 + e_pct / 100.0) + 0.5); }
static int64_t p_for8(double e_pct) { return (int64_t)(8000.0 / (1.0 + e_pct / 100.0) + 0.5); }

int main(void) {
    /* 연습 1 */
    {
        int bad = 0, n = 0; uint32_t s = 0x9911u; baud_res r; int32_t ppm;
        for (int t = 0; t < 100000; t++) {
            uint32_t fclk = 1000000u + xs32(&s) % 99000001u, baud = 300u + xs32(&s) % 1000000u;
            unsigned ovs = (xs32(&s) & 1) ? 16 : 8, fb = xs32(&s) % 5;
            int a = baud_calc(fclk, baud, ovs, fb, 0xFFFFFFFFu, &r), b = err_ppm_int(fclk, baud, ovs, fb, 0xFFFFFFFFu, &ppm);
            if (a != b) { bad++; continue; }
            if (a != BAUD_OK) continue;
            n++;
            double dp = r.err_pct * 1e4;   /* % → ppm */
            if (dp - ppm > 1.0 || ppm - dp > 1.0) bad++;
        }
        printf("ex1: %d cases, int ppm vs double mismatch(>1ppm)=%d\n", n, bad);
        CHECK(n > 50000 && bad == 0);
        CHECK(err_ppm_int(16000000, 115200, 16, 0, 0xFFFF, &ppm) == BAUD_OK && ppm == -35494);
        CHECK(err_ppm_int(16000000, 115200, 8, 0, 0xFFFF, &ppm) == BAUD_OK && ppm == 21242);
        CHECK(err_ppm_int(16000000, 115200, 16, 4, 0xFFFF, &ppm) == BAUD_OK && ppm == -799);
        CHECK(err_ppm_int(14745600, 921600, 16, 0, 0xFFFF, &ppm) == BAUD_OK && ppm == 0);
    }
    /* 연습 2: 8x — 샘플 4,5,6. back-to-back 8N1: late 는 샘플 6, early 는 샘플 5 */
    {
        ufmt f81 = { 8, 'N', 1 };
        double hi = tol_theory(9, 6, 8, 1), lo = tol_theory(9, 5, 8, 0), lo_nf = tol_theory(9, 4, 8, 0);
        printf("ex2: x8 8N1 b2b theory late %+.3f%% early %+.3f%% (NF from %+.3f%%)\n", hi, lo, lo_nf);
        CHECK(hi > 2.56 && hi < 2.57 && lo < -5.26 && lo > -5.27);
        link_stats a, b, c, d;
        sim_link(&f81, 8, p_for8(hi - 0.10), 1000, 3000, 0, 1u, &a);
        sim_link(&f81, 8, p_for8(hi + 0.30), 1000, 3000, 0, 2u, &b);
        sim_link(&f81, 8, p_for8(lo + 0.10), 1000, 3000, 0, 3u, &c);
        sim_link(&f81, 8, p_for8(lo - 0.30), 1000, 3000, 0, 4u, &d);
        printf("ex2: sim err  %+.2f%%:%d  %+.2f%%:%d  %+.2f%%:%d  %+.2f%%:%d\n",
               hi - 0.10, a.err, hi + 0.30, b.err, lo + 0.10, c.err, lo - 0.30, d.err);
        CHECK(a.err == 0 && c.err == 0);
        CHECK(b.err > 0 && d.err > 0);
        /* x16 대비: 빠른 쪽 한계가 3.896% → 2.564% 로 줄어든다 */
        CHECK(tol_theory(9, 10, 16, 1) - hi > 1.3);
    }
    /* 연습 3: 8N2 송신 → 8N1 수신, back-to-back */
    {
        ufmt tx82 = { 8, 'N', 2 };    /* 수신기는 첫 stop 만 본다 (urx 는 stop_bits 를 쓰지 않는다) */
        double hi = tol_theory(8, 9, 16, 1), hi_nf = tol_theory(8, 10, 16, 1), lo = tol_theory(9, 9, 16, 0);
        link_stats a, b, c, d;
        sim_link(&tx82, 16, p_for16(hi - 0.10), 1000, 3000, 0, 5u, &a);
        sim_link(&tx82, 16, p_for16(hi + 0.30), 1000, 3000, 0, 6u, &b);
        sim_link(&tx82, 16, p_for16(hi_nf - 0.10), 1000, 3000, 0, 7u, &c);
        sim_link(&tx82, 16, p_for16(lo + 0.10), 1000, 3000, 0, 8u, &d);
        printf("ex3: 8N2->8N1 b2b: %+.2f%% err %d | %+.2f%% err %d | %+.2f%% nf %d | %+.2f%% err %d\n",
               hi - 0.10, a.err, hi + 0.30, b.err, hi_nf - 0.10, c.nf, lo + 0.10, d.err);
        CHECK(a.err == 0 && b.err > 0);   /* 한계가 3.90% → 5.11% 로 */
        CHECK(c.nf == 0 && d.err == 0);
    }
    /* 연습 4 */
    {
        static const uint32_t X[] = { 1843200, 3686400, 7372800, 11059200, 14745600, 18432000, 8000000, 12000000, 16000000, 24000000 };
        static const uint32_t EXP[] = { 115200, 230400, 460800, 230400, 921600, 230400, 0, 0, 0, 0 };
        int bad = 0;
        for (int i = 0; i < 10; i++) {
            uint32_t m = max_exact_baud(X[i], 16);
            printf("ex4: %9u Hz -> max exact baud (x16, int) %u\n", X[i], m);
            if (m != EXP[i]) bad++;
            /* baud_calc 로도 같은지: 그 baud 까지 err == 0 */
            for (size_t k = 4; m && k < sizeof STD_BAUD / sizeof STD_BAUD[0] && STD_BAUD[k] <= m; k++) {
                baud_res r;
                if (baud_calc(X[i], STD_BAUD[k], 16, 0, 0xFFFF, &r) != BAUD_OK || r.err_pct != 0.0) bad++;
            }
        }
        CHECK(bad == 0);
        CHECK(max_exact_baud(18432000, 16) < max_exact_baud(14745600, 16));   /* 큰 크리스털이 항상 낫지는 않다 */
    }
    /* 연습 5 */
    {
        baud_res r;
        CHECK(baud_calc(16000000, 115200, 16, 4, 0xFFFF, &r) == BAUD_OK);
        double e0 = r.err_pct, lo = tol_theory(9, 9, 16, 0), hi = tol_theory(9, 10, 16, 1);
        double xf = max_osc_tol(e0, lo, hi), xo = max_osc_tol(e0, lo / 2, hi / 2);
        double a = 0, b = 20;                                   /* 이분 탐색 */
        for (int it = 0; it < 60; it++) { double c = (a + b) / 2; if (osc_ok(e0, lo, hi, c)) a = c; else b = c; }
        printf("ex5: e0=%+.4f%%  max osc tol: fail-limit %.3f%% (bisect %.3f%%), half-margin %.3f%%\n", e0, xf, a, xo);
        CHECK(xf - a < 1e-6 && a - xf < 1e-6);
        /* 시뮬레이션: MCU 가 x 만큼 느린 코너(구속 조건 x[2]: PC→MCU, PC 가 상대적으로 빠름) */
        ufmt f81 = { 8, 'N', 1 };
        link_stats in, out;
        double pc = 115200.0, mcu_in = r.actual * (1 - (xf - 0.05) / 100), mcu_out = r.actual * (1 - (xf + 0.30) / 100);
        sim_link(&f81, 16, (int64_t)(1e12 / pc + 0.5), (int64_t)(1e12 / (16 * mcu_in) + 0.5), 3000, 0, 9u, &in);
        sim_link(&f81, 16, (int64_t)(1e12 / pc + 0.5), (int64_t)(1e12 / (16 * mcu_out) + 0.5), 3000, 0, 10u, &out);
        printf("ex5: PC->MCU, MCU slow by %.2f%%: err %d | by %.2f%%: err %d\n", xf - 0.05, in.err, xf + 0.30, out.err);
        CHECK(in.err == 0 && out.err > 0);
        CHECK(xo < xf / 2 + 0.1 && xo > 1.5);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
