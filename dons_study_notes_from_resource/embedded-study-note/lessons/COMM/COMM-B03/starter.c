/* COMM-B03 Baud rate 오차 계산과 허용 한계
 *
 * Part A: baud 계산기 — 정수 분주(OVS 16/8) + 분수 분주(STM32 USARTDIV 식 BRR), 실제 baud, % 오차, 범위 검사
 * Part B: 클럭 편차 실험 — 송신 비트 폭 P_tx 와 수신 샘플 주기 S_rx 가 다른 링크를 시간 단위로 시뮬레이션해
 *         16x 오버샘플링 수신기(COMM-B02 모델 축약판)가 몇 % 부터 틀리는지 보고, 이론 한계식과 비교
 * Part C: 링크 예산 — 양쪽의 분주 오차 + 발진기 ppm 으로 최악 편차를 구해 판정하고, 최악 코너를 시뮬레이션으로 확인
 *
 * 테스트: 손으로 검산한 분주값(AVR 16 MHz, STM32 72 MHz, 1.8432/14.7456 MHz), 정수 부등식으로 반올림 성질 검사,
 *         이론 한계 바로 안쪽(오류 0) / 바깥쪽(오류 > 0) 의 고정 편차 점, xorshift32 고정 시드.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================= Part A: baud 계산기 ================= */
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
    (void)fclk; (void)baud; (void)ovs; (void)frac_bits; (void)div_max; (void)r;
    /* TODO: 인자 검사 → N = round(fclk·2^F / (ovs·baud)) 를 uint64 정수로 → 범위 검사 → actual, err_pct */
    return BAUD_EARG;
}

/* STM32 USART_BRR 인코딩 (대표값 — 패밀리별 RM 확인 권장).
 *   OVER8 = 0: n = USARTDIV×16 → BRR = n (가수 <<4 | 분수 4비트)
 *   OVER8 = 1: n = USARTDIV×8  → BRR = 가수 <<4 | 분수 3비트, BRR[3] = 0 */
static uint16_t stm32_brr(uint32_t n, int over8) {
    (void)n; (void)over8;
    return 0; /* TODO: OVER8 = 0 → n 그대로, OVER8 = 1 → 가수 <<4 | 분수 3비트 */
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
    (void)bits; (void)n; (void)t0; (void)p; (void)t;
    return 1; /* TODO: t 가 프레임 안이면 몇 번째 비트인지 → 그 비트 값, 밖이면 idle */
}

typedef struct { int nframes, rx_frames, ok, fe, pe, nf, err; } link_stats;
#define MAXF 4096

/* 링크 시뮬레이션: 송신 비트 폭 p_tx, 수신 샘플 주기 s_rx (같은 시간 단위). 프레임 nframes 개 (랜덤 데이터).
 * gap_bits = 0: back-to-back (stop 끝 = 다음 start). gap_bits = g > 0: 프레임 사이 g 비트 + 0..p_tx−1 랜덤 idle.
 * RX 샘플 격자의 시작 위상도 랜덤. err = 보낸 것과 다른 프레임 수 (+ 남는 프레임 수). */
static void sim_link(const ufmt *f, int ovs, int64_t p_tx, int64_t s_rx, int nframes, int gap_bits,
                     uint32_t seed, link_stats *st) {
    memset(st, 0, sizeof *st);
    st->nframes = nframes;
    (void)f; (void)ovs; (void)p_tx; (void)s_rx; (void)gap_bits; (void)seed;
    (void)urx_init; (void)urx_step;   /* 수신기는 주어진다 — 이 함수에서 쓴다 */
    /* TODO: RX 격자 위상 랜덤 → 시각 t 를 s_rx 씩 올리며
     *       (1) t 가 현재 프레임 끝을 지나면 다음 프레임(랜덤 바이트, gap)을 시간표에 올리고 sent[] 에 기록
     *       (2) urx_step(tx_level(...)) → 프레임이 나오면 보낸 것과 비교해 ok/fe/pe/nf 를 센다
     *       (3) 마지막 프레임 뒤 충분히 지나면 끝. err = (nframes − ok) + 남는 프레임 수 */
    st->err = nframes;
}

/* 이론 한계 (%). 비트 j (start = 0) 의 샘플 m (1..ovs) 이 그 비트 안에 있어야 한다.
 * 샘플 m 의 위치 = (m − 1 + φ)/ovs 비트, φ ∈ (0, 1] = 에지에서 첫 0 샘플까지 지연 (샘플 단위).
 * late = 1 (TX 가 빠름, e > 0): 최악 φ = 1 → (1+e)(j + m/ovs) < j + 1 → e_max = (1 − m/ovs)/(j + m/ovs)
 * late = 0 (TX 가 느림, e < 0): 최악 φ → 0 → (1+e)(j + (m−1)/ovs) ≥ j → e_min = −((m−1)/ovs)/(j + (m−1)/ovs) */
static double tol_theory(int j, int m, int ovs, int late) {
    (void)j; (void)m; (void)ovs; (void)late;
    return 0.0; /* TODO: 위 두 식 (단위 %) */
}

/* ================= Part C: 링크 예산 ================= */
typedef struct {
    const char *name;
    uint32_t fclk; int32_t ppm;          /* 클럭원 허용 오차 ±ppm (대표값) */
    unsigned ovs, frac_bits; uint32_t div_max;
} uart_end;
typedef struct { double e_lo, e_hi, lim_lo, lim_hi; int verdict; } link_dir;
enum { LINK_OK = 0, LINK_MARGINAL = 1, LINK_FAIL = 2, LINK_BADCFG = 3 };

/* 한 방향 (tx → rx). 편차 e = baud_tx·(1±ppm_tx) / (baud_rx·(1∓ppm_rx)) − 1 의 범위 [e_lo, e_hi] (%).
 * 한계 = 수신측 ovs 의 back-to-back 최악 한계: late 는 샘플 m+1, early 는 샘플 m, j = frame_bits − 1.
 * 판정: 범위가 한계를 넘으면 FAIL, 한계의 절반을 넘으면 MARGINAL (이 레슨의 여유 규칙), 아니면 OK. */
static int link_dir_check(const uart_end *tx, const uart_end *rx, uint32_t baud, int frame_bits, link_dir *d) {
    (void)tx; (void)rx; (void)baud; (void)frame_bits;
    memset(d, 0, sizeof *d);
    /* TODO: 양쪽 baud_calc (실패 → BADCFG) → 편차 범위 [e_lo, e_hi] → 수신측 한계 [lim_lo, lim_hi] → 판정 */
    d->verdict = LINK_OK;
    return d->verdict;
}

/* 양방향 판정: 나쁜 쪽 */
static int link_check(const uart_end *a, const uart_end *b, uint32_t baud, int frame_bits, link_dir *ab, link_dir *ba) {
    int v1 = link_dir_check(a, b, baud, frame_bits, ab);
    int v2 = link_dir_check(b, a, baud, frame_bits, ba);
    return v1 > v2 ? v1 : v2;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static int near(double a, double b, double tol) { return a - b <= tol && b - a <= tol; }

/* 편차 e(%) → 송신 비트 폭 (RX 샘플 주기 1000, RX 비트 16000 단위). e = 16000/p − 1 */
static int64_t p_for(double e_pct) { return (int64_t)(16000.0 / (1.0 + e_pct / 100.0) + 0.5); }

static int errs_at(const ufmt *f, double e_pct, int gap, int frames, uint32_t seed, int *nf) {
    link_stats st;
    sim_link(f, 16, p_for(e_pct), 1000, frames, gap, seed, &st);
    if (nf) *nf = st.nf;
    return st.err;
}

/* 링크 코너 시뮬레이션: ps 단위. tx 쪽 실제 baud × (1 + stx), rx 쪽 × (1 + srx) */
static int sim_corner(const uart_end *tx, const uart_end *rx, uint32_t baud, const ufmt *f, double stx, double srx) {
    baud_res a, b;
    if (baud_calc(tx->fclk, baud, tx->ovs, tx->frac_bits, tx->div_max, &a) != BAUD_OK ||
        baud_calc(rx->fclk, baud, rx->ovs, rx->frac_bits, rx->div_max, &b) != BAUD_OK) return -1;
    int64_t p = (int64_t)(1e12 / (a.actual * (1 + stx)) + 0.5);
    int64_t s = (int64_t)(1e12 / (rx->ovs * b.actual * (1 + srx)) + 0.5);
    link_stats st;
    sim_link(f, (int)rx->ovs, p, s, 2000, 0, 0xC0FFEEu, &st);
    return st.err;
}

int main(void) {
    baud_res r;
    /* ---- Part A ---- */
    CHECK(baud_calc(1843200, 9600, 16, 0, 0xFFFF, &r) == BAUD_OK && r.div == 12 && near(r.err_pct, 0, 1e-12));
    CHECK(baud_calc(1843200, 115200, 16, 0, 0xFFFF, &r) == BAUD_OK && r.div == 1 && near(r.err_pct, 0, 1e-12));
    CHECK(baud_calc(14745600, 921600, 16, 0, 0xFFFF, &r) == BAUD_OK && r.div == 1 && near(r.err_pct, 0, 1e-12));
    CHECK(baud_calc(16000000, 115200, 16, 0, 0xFFFF, &r) == BAUD_OK && r.div == 9 && near(r.err_pct, -3.5494, 1e-4));
    CHECK(near(r.actual, 111111.111, 1e-3));
    CHECK(baud_calc(16000000, 115200, 8, 0, 0xFFFF, &r) == BAUD_OK && r.div == 17 && near(r.err_pct, 2.1242, 1e-4));
    CHECK(baud_calc(16000000, 115200, 16, 4, 0xFFFF, &r) == BAUD_OK && r.div == 139 && near(r.err_pct, -0.0799, 1e-4));
    CHECK(baud_calc(72000000, 115200, 16, 4, 0xFFFF, &r) == BAUD_OK && r.div == 625 && stm32_brr(r.div, 0) == 0x271
          && near(r.err_pct, 0, 1e-12));
    CHECK(baud_calc(72000000, 88253, 16, 4, 0xFFFF, &r) == BAUD_OK && stm32_brr(r.div, 0) == 0x330); /* 50.99 → 분수 올림이 가수로 */
    CHECK(baud_calc(72000000, 86555, 16, 4, 0xFFFF, &r) == BAUD_OK && stm32_brr(r.div, 0) == 0x340); /* 51.99 → 0x340 (0x330|0x10 아님) */
    CHECK(baud_calc(16000000, 115200, 8, 3, 0x7FFF, &r) == BAUD_OK && r.div == 139 && stm32_brr(r.div, 1) == 0x113);
    CHECK(baud_calc(8000000, 921600, 16, 0, 0xFFFF, &r) == BAUD_OK && r.div == 1 && near(r.err_pct, -45.7465, 1e-4));
    CHECK(baud_calc(8000000, 921600, 16, 4, 0xFFFF, &r) == BAUD_ETOO_FAST);
    CHECK(baud_calc(8000000, 921600, 8, 3, 0x7FFF, &r) == BAUD_OK && r.div == 9 && near(r.err_pct, -3.5494, 1e-4));
    CHECK(baud_calc(1843200, 230400, 16, 0, 0xFFFF, &r) == BAUD_OK && r.div == 1 && near(r.err_pct, -50.0, 1e-12));
    CHECK(baud_calc(80000000, 300, 16, 0, 0xFFFF, &r) == BAUD_OK && r.div == 16667);
    CHECK(baud_calc(80000000, 300, 16, 4, 0xFFFF, &r) == BAUD_ETOO_SLOW);
    CHECK(baud_calc(16000000, 0, 16, 0, 0xFFFF, &r) == BAUD_EARG && baud_calc(16000000, 9600, 12, 0, 0xFFFF, &r) == BAUD_EARG);
    CHECK(baud_calc(1000, 115200, 16, 0, 0xFFFF, &r) == BAUD_ETOO_FAST);     /* n = 0 */
    {   /* 실패 시 *r 보존 */
        baud_res keep = { 1234, 1.0, 2.0 };
        CHECK(baud_calc(8000000, 921600, 16, 4, 0xFFFF, &keep) == BAUD_ETOO_FAST && keep.div == 1234);
    }
    {   /* 랜덤 성질: |N − x| ≤ 1/2 를 정수로 (|2·N·den − 2·num| ≤ den), 오차 부호 = 반올림 방향 */
        uint32_t s = 0xB03B03u; int bad = 0, n = 0;
        for (int t = 0; t < 200000; t++) {
            uint32_t fclk = 1000000u + xs32(&s) % 99000001u, baud = 300u + xs32(&s) % 1000000u;
            unsigned ovs = (xs32(&s) & 1) ? 16 : 8, fb = xs32(&s) % 5;
            if (baud_calc(fclk, baud, ovs, fb, 0xFFFFFFFFu, &r) != BAUD_OK) continue;
            n++;
            uint64_t num = (uint64_t)fclk << fb, den = (uint64_t)ovs * baud, a = 2 * r.div * den, b2 = 2 * num;
            uint64_t diff = a > b2 ? a - b2 : b2 - a;
            if (diff > den) bad++;
            if ((a > b2 && r.err_pct > 0) || (a < b2 && r.err_pct < 0)) bad++;  /* N 이 크면 느려진다 */
        }
        printf("baud_calc random: %d valid cases, bad=%d\n", n, bad);
        CHECK(n > 150000 && bad == 0);
    }
    {   /* 표: 흔한 클럭 × baud — 정수 분주 OVS16 / STM32 식 분수 분주 OVS16 (F=4) */
        static const uint32_t FCLK[] = { 1843200, 8000000, 14745600, 16000000, 48000000, 72000000, 80000000 };
        static const uint32_t BAUD[] = { 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600 };
        printf("err%% int16 / frac16 |");
        for (int j = 0; j < 8; j++) printf(" %7u", BAUD[j]);
        printf("\n");
        for (int i = 0; i < 7; i++) {
            printf("%10.4f MHz int  |", FCLK[i] / 1e6);
            for (int j = 0; j < 8; j++) {
                if (baud_calc(FCLK[i], BAUD[j], 16, 0, 0xFFFF, &r) == BAUD_OK) printf(" %+7.2f", r.err_pct);
                else printf("       -");
            }
            printf("\n%10s     frac |", "");
            for (int j = 0; j < 8; j++) {
                if (baud_calc(FCLK[i], BAUD[j], 16, 4, 0xFFFF, &r) == BAUD_OK) printf(" %+7.2f", r.err_pct);
                else printf("       -");
            }
            printf("\n");
        }
    }

    /* ---- Part B ---- */
    {
        static const uint8_t A_8N1[10] = { 0, 1, 0, 0, 0, 0, 0, 1, 0, 1 };   /* 'A' 0x41, LSB first */
        uint8_t bits[16]; ufmt f81 = { 8, 'N', 1 };
        int nb = frame_encode(&f81, 0x41, bits);
        CHECK(nb == 10 && memcmp(bits, A_8N1, 10) == 0);
        CHECK(tx_level(bits, nb, 100, 10, 99) == 1);     /* 시작 전 idle */
        CHECK(tx_level(bits, nb, 100, 10, 100) == 0);    /* start */
        CHECK(tx_level(bits, nb, 100, 10, 110) == 1 && tx_level(bits, nb, 100, 10, 119) == 1 && tx_level(bits, nb, 100, 10, 120) == 0);  /* 비트 1 = LSB = 1, 비트 2 = 0 */
        CHECK(tx_level(bits, nb, 100, 10, 199) == 1 && tx_level(bits, nb, 100, 10, 200) == 1);  /* stop, 이후 idle */
    }
    {
        ufmt f81 = { 8, 'N', 1 }, f8e1 = { 8, 'E', 1 };
        link_stats st;
        sim_link(&f81, 16, 16000, 1000, 3000, 0, 1u, &st);
        CHECK(st.rx_frames == 3000 && st.ok == 3000 && st.err == 0 && st.nf == 0);
        sim_link(&f8e1, 16, 16000, 1000, 3000, 2, 2u, &st);
        CHECK(st.rx_frames == 3000 && st.ok == 3000 && st.err == 0 && st.nf == 0 && st.pe == 0);

        /* 이론 한계 */
        double l81 = tol_theory(9, 10, 16, 1), e81 = tol_theory(9, 9, 16, 0), n81 = tol_theory(9, 8, 16, 0);
        double i81 = tol_theory(8, 9, 16, 1), l8e = tol_theory(10, 10, 16, 1), e8e = tol_theory(10, 9, 16, 0);
        double i8e = tol_theory(9, 9, 16, 1);
        CHECK(near(l81, 100 * 0.375 / 9.625, 1e-9) && near(e81, -100 * 0.5 / 9.5, 1e-9));
        CHECK(near(n81, -100 * 0.4375 / 9.4375, 1e-9) && near(i81, 100 * 0.4375 / 8.5625, 1e-9));
        printf("theory 8N1: b2b late %+.3f%%  early %+.3f%% (NF from %+.3f%%)  isolated late %+.3f%%\n", l81, e81, n81, i81);
        printf("theory 8E1: b2b late %+.3f%%  early %+.3f%%  isolated late %+.3f%%\n", l8e, e8e, i8e);

        struct { const char *name; const ufmt *f; int gap; double lo, hi; } C[] = {
            { "8N1 back-to-back", &f81, 0, e81, l81 }, { "8N1 isolated", &f81, 2, e81, i81 },
            { "8E1 back-to-back", &f8e1, 0, e8e, l8e }, { "8E1 isolated", &f8e1, 2, e8e, i8e },
        };
        for (int c = 0; c < 4; c++) {
            int nf;
            int in_hi = errs_at(C[c].f, C[c].hi - 0.10, C[c].gap, 3000, 11u + (uint32_t)c, NULL);
            int in_lo = errs_at(C[c].f, C[c].lo + 0.10, C[c].gap, 3000, 22u + (uint32_t)c, NULL);
            int out_hi = errs_at(C[c].f, C[c].hi + 0.30, C[c].gap, 3000, 33u + (uint32_t)c, &nf);
            int out_lo = errs_at(C[c].f, C[c].lo - 0.30, C[c].gap, 3000, 44u + (uint32_t)c, NULL);
            printf("%-17s  e=%+.2f%%: err %4d | e=%+.2f%%: err %4d | e=%+.2f%%: err %4d | e=%+.2f%%: err %4d\n",
                   C[c].name, C[c].lo - 0.30, out_lo, C[c].lo + 0.10, in_lo, C[c].hi - 0.10, in_hi, C[c].hi + 0.30, out_hi);
            CHECK(in_hi == 0 && in_lo == 0);
            CHECK(out_hi > 0 && out_lo > 0);
        }
        /* NF 만 서고 데이터는 맞는 구간 (8N1, 느린 TX): −4.64% ~ −5.26% */
        int nf1, nf2;
        int err1 = errs_at(&f81, n81 + 0.10, 0, 3000, 55u, &nf1);
        int err2 = errs_at(&f81, -4.95, 0, 3000, 66u, &nf2);
        printf("8N1 slow TX: e=%+.2f%% err %d nf %d | e=-4.95%% err %d nf %d\n", n81 + 0.10, err1, nf1, err2, nf2);
        CHECK(err1 == 0 && nf1 == 0);
        CHECK(err2 == 0 && nf2 > 0);
    }

    /* ---- Part C ---- */
    {
        uart_end pc   = { "PC/bridge 14.7456M", 14745600, 50, 16, 0, 0xFFFF };
        uart_end stm  = { "STM32 16M HSE frac", 16000000, 50, 16, 4, 0xFFFF };
        uart_end hsi  = { "STM32 HSI +-1% frac", 16000000, 10000, 16, 4, 0xFFFF };
        uart_end avr  = { "AVR 16M int x16", 16000000, 50, 16, 0, 0xFFFF };
        uart_end avr2 = { "AVR 16M U2X x8", 16000000, 50, 8, 0, 0xFFFF };
        uart_end rc4  = { "RC +-4% frac", 16000000, 40000, 16, 4, 0xFFFF };
        uart_end f8   = { "8M frac x16", 8000000, 50, 16, 4, 0xFFFF };
        struct { const uart_end *a; int fb; uint32_t baud; int expect; } L[] = {
            { &stm, 10, 115200, LINK_OK }, { &hsi, 10, 115200, LINK_OK }, { &avr, 10, 115200, LINK_MARGINAL },
            { &avr, 11, 115200, LINK_FAIL }, { &avr2, 10, 115200, LINK_MARGINAL }, { &rc4, 10, 115200, LINK_FAIL },
            { &f8, 10, 921600, LINK_BADCFG },
        };
        static const char *VN[] = { "OK", "MARGINAL", "FAIL", "BADCFG" };
        for (int k = 0; k < 7; k++) {
            link_dir ab, ba;
            int v = link_check(L[k].a, &pc, L[k].baud, L[k].fb, &ab, &ba);
            CHECK(v == L[k].expect);
            if (v == LINK_BADCFG) { printf("%-20s %s @%u: BADCFG\n", L[k].a->name, L[k].fb == 10 ? "8N1" : "8E1", L[k].baud); continue; }
            ufmt f = { 8, L[k].fb == 11 ? 'E' : 'N', 1 };
            double pa = L[k].a->ppm * 1e-6, pb = pc.ppm * 1e-6;
            /* 네 코너: (a 빠름, pc 느림) / (a 느림, pc 빠름) × 두 방향 */
            int e1 = sim_corner(L[k].a, &pc, L[k].baud, &f, +pa, -pb), e2 = sim_corner(L[k].a, &pc, L[k].baud, &f, -pa, +pb);
            int e3 = sim_corner(&pc, L[k].a, L[k].baud, &f, +pb, -pa), e4 = sim_corner(&pc, L[k].a, L[k].baud, &f, -pb, +pa);
            printf("%-20s %s: A->PC [%+.2f,%+.2f]%% lim [%+.2f,%+.2f] | PC->A [%+.2f,%+.2f]%% lim [%+.2f,%+.2f] => %-8s sim err %d %d %d %d\n",
                   L[k].a->name, L[k].fb == 10 ? "8N1" : "8E1", ab.e_lo, ab.e_hi, ab.lim_lo, ab.lim_hi,
                   ba.e_lo, ba.e_hi, ba.lim_lo, ba.lim_hi, VN[v], e1, e2, e3, e4);
            if (v != LINK_FAIL) CHECK(e1 == 0 && e2 == 0 && e3 == 0 && e4 == 0);   /* 이론이 OK 면 시뮬레이션도 0 */
            else CHECK(e1 + e2 + e3 + e4 > 0);                                     /* FAIL 이면 어느 코너에선 깨진다 */
        }
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
