/* COMM-A10 추가 연습 — 한 번에 검증
 * solution.c 의 캡처 표현 · 디코더 · 생성기를 그대로 쓴다 (LA_NO_MAIN 으로 main 만 뺌).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 *   연습 1  Saleae 식 "Time [s]" 초 단위 시각 → 정수 ps (float 금지)
 *   연습 2  SPI 모드 추정 (CPOL = CS↓ 순간 SCK 레벨, CPHA = MOSI 가 바뀌는 에지)
 *   연습 3  UART 형식 추정 (8N1 / 8E1 / 8O1 / 7E1 / 7O1)
 *   연습 4  I2C 이벤트 → i2ctransfer 식 요약 + TMP117 온도
 */
#define LA_NO_MAIN
#pragma GCC diagnostic ignored "-Wunused-function"   /* solution.c 함수 중 여기서 안 쓰는 것 */
#include "solution.c"

/* ------------------------------------------------------------------ 연습 1 */
/* "-0.000000510" → -510000 ps. 소수 12자리(ps)까지, 정수부 ≤ 6자리(초). 실패 -1 */
static int parse_seconds_ps(const char *s, int64_t *out) {
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    int64_t ip = 0, fp = 0;
    int nd = 0, nf = 0;
    while (*s >= '0' && *s <= '9') { if (++nd > 6) return -1; ip = ip * 10 + (*s++ - '0'); }
    if (nd == 0) return -1;
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { if (++nf > 12) return -1; fp = fp * 10 + (*s++ - '0'); }
        if (nf == 0) return -1;
    }
    if (*s) return -1;
    for (int i = nf; i < 12; i++) fp *= 10;                       /* 12자리로 맞추면 그대로 ps */
    int64_t v = ip * PS_PER_S + fp;
    *out = neg ? -v : v;
    return 0;
}
static void fmt_seconds(char *o, size_t cap, int64_t ps, int digits) {     /* Saleae 식: 소수 digits 자리 */
    int64_t a = ps < 0 ? -ps : ps, div = 1;
    for (int i = digits; i < 12; i++) div *= 10;
    snprintf(o, cap, "%s%" PRId64 ".%0*" PRId64, ps < 0 ? "-" : "", a / PS_PER_S, digits, a % PS_PER_S / div);
}

/* ------------------------------------------------------------------ 연습 2 */
/* 반환 mode 0..3, MOSI 가 한 번도 안 바뀌면 -1 (CPHA 판단 불가) */
static int spi_guess_mode(const la_capture *cp, int cs, int sck, int mosi) {
    const la_chan *C = &cp->ch[cs], *K = &cp->ch[sck], *M = &cp->ch[mosi];
    int cpol = -1, v0 = 0, v1 = 0;
    for (uint32_t k = 0; k < C->n; k++) {
        if ((C->init ^ (int)((k + 1) & 1u)) != 0) continue;
        int64_t ta = C->t[k], tb = k + 1 < C->n ? C->t[k + 1] : cp->t_end;
        if (cpol < 0) cpol = la_level_at(K, ta);
        uint32_t e0 = la_edge_upper(K, ta);
        for (uint32_t m = la_edge_upper(M, ta); m < M->n && M->t[m] < tb; m++) {
            uint32_t ne = la_edge_upper(K, M->t[m]) - e0;              /* 이 MOSI 변화 전까지의 SCK 에지 수 */
            if (ne == 0 || ((ne - 1) & 1u)) v0++;                       /* 첫 에지 전 / trailing 직후 → CPHA 0 */
            else v1++;                                                  /* leading 직후 → CPHA 1 */
        }
    }
    if (cpol < 0 || v0 + v1 == 0) return -1;
    return cpol << 1 | (v1 > v0);
}

/* ------------------------------------------------------------------ 연습 3 */
/* parity 형식부터: 무작위 데이터면 프레임마다 1/2 확률로 parity 가 틀리므로 "오류 0" 은 강한 증거.
 * 정확히 하나만 오류 0 이면 그것, 아니면 8N1 (8N1 vs 8N2 는 프레임 사이 idle 이 있으면 구별 불가). */
static int uart_guess_fmt(const la_chan *c, int64_t t_end, int64_t T, uart_fmt *out) {
    static const uart_fmt cand[4] = { { 8, 'E', 1 }, { 8, 'O', 1 }, { 7, 'E', 1 }, { 7, 'O', 1 } };
    static la_event ev[512];
    int zero = -1, nzero = 0;
    for (int i = 0; i < 4; i++) {
        int n = uart_decode(c, t_end, T, cand[i], ev, 512), bad = 0;
        if (n < 8) continue;                                           /* 프레임이 적으면 증거 부족 */
        for (int k = 0; k < n; k++) bad += ev[k].type != EV_UART || ev[k].flags != 0;
        if (bad == 0) { zero = i; nzero++; }
    }
    uart_fmt d = { 8, 'N', 1 };
    *out = nzero == 1 ? cand[zero] : d;
    return nzero;
}

/* ------------------------------------------------------------------ 연습 4 */
/* START..STOP 하나 = 한 줄: "w1@0x48 0x00 r2@0x48 0x0C 0x80" (주소 NACK 이면 "w0@0x50 NACK").
 * TMP117(0x48) 의 reg 0x00 을 2바이트 읽었으면 " => 25.000 C" (7.8125 m°C/LSB, 데이터시트 확인 권장) */
static int i2c_summarize(const la_event *ev, int n, char *buf, size_t cap) {
    wbuf w = { buf, cap, 0, 0 };
    if (cap) buf[0] = 0;
    int seg_n = 0, seg_r = 0, seg_addr = -1, first = 1, last_reg = -1, reg_addr = -1;
    uint8_t rd[4];
    int nrd = 0;
    char seg[256];
    size_t sl = 0;
    for (int i = 0; i < n; i++) {
        const la_event *e = &ev[i];
        if (e->type == EV_I2C_ADDR) {
            seg_addr = (int)e->val; seg_r = e->aux; seg_n = 0; sl = 0; nrd = 0;
            if (e->flags & F_NACK) {
                wput(&w, "%s%c0@0x%02X NACK", first ? "" : " ", seg_r ? 'r' : 'w', seg_addr);
                first = 0; seg_addr = -1;
            }
        } else if (e->type == EV_I2C_DATA && seg_addr >= 0) {
            if (seg_n == 0 && !seg_r) { last_reg = (int)e->val; reg_addr = seg_addr; }
            if (seg_r && nrd < 4) rd[nrd++] = (uint8_t)e->val;
            sl += (size_t)snprintf(seg + sl, sizeof seg - sl, " 0x%02" PRIX32, e->val);
            if (sl >= sizeof seg) sl = sizeof seg - 1;
            seg_n++;
        } else if ((e->type == EV_I2C_RSTART || e->type == EV_I2C_STOP) && seg_addr >= 0) {
            wput(&w, "%s%c%d@0x%02X%s", first ? "" : " ", seg_r ? 'r' : 'w', seg_n, seg_addr, seg);
            first = 0;
            if (seg_r && seg_addr == 0x48 && reg_addr == 0x48 && last_reg == 0 && nrd == 2) {
                int16_t raw = (int16_t)(rd[0] << 8 | rd[1]);
                int32_t mc = raw * 78125 / 10000;                       /* m°C */
                wput(&w, " => %s%d.%03d C", mc < 0 ? "-" : "", abs(mc) / 1000, abs(mc) % 1000);
            }
            seg_addr = -1;
            sl = 0;
            seg[0] = 0;
        }
        if (e->type == EV_I2C_STOP) { wput(&w, "\n"); first = 1; }
    }
    return (int)w.n;
}

int main(void) {
    /* 연습 1 */
    {
        int64_t v;
        CHECK(parse_seconds_ps("0.000000510", &v) == 0 && v == 510000);
        CHECK(parse_seconds_ps("-0.000001", &v) == 0 && v == -1000000);
        CHECK(parse_seconds_ps("12.5", &v) == 0 && v == 12500000000000LL);
        CHECK(parse_seconds_ps("0.0000000000001", &v) == -1 && parse_seconds_ps("1e-6", &v) == -1 &&
              parse_seconds_ps(".5", &v) == -1 && parse_seconds_ps("1.", &v) == -1);
        g_rng = 0xE1001u;
        int ok = 0, float_bad = 0;
        char s[40];
        for (int i = 0; i < 20000; i++) {
            int64_t t = rnd_range(-PS_PER_S, 20 * PS_PER_S) / 1000 * 1000;    /* ns 해상도, ±수 초 */
            fmt_seconds(s, sizeof s, t, 9);
            ok += parse_seconds_ps(s, &v) == 0 && v == t;
            float_bad += (int64_t)(atof(s) * 1e12) != t;                    /* float 로 읽으면? */
        }
        printf("ex1 20000 ns-resolution times: exact %d, (int64)(atof*1e12) wrong %d\n", ok, float_bad);
        CHECK(ok == 20000 && float_bad > 0);
    }
    /* 연습 2 */
    {
        g_rng = 0xE2001u;
        static const char *const nm[4] = { "CS_N", "SCLK", "MOSI", "MISO" };
        const int chs[4] = { 0, 1, 2, 3 };
        int ok = 0, total = 0;
        for (int it = 0; it < 200; it++) {
            spi_fmt f = { (uint8_t)(it & 3), 8, 0 };
            int init[4] = { 1, f.mode >> 1, 0, 0 };
            cap_init(&G_IDEAL, 4, nm, init);
            uint32_t mo[4], mi[4];
            for (int w = 0; w < 4; w++) { mo[w] = xs32() & 0xFF; mi[w] = xs32() & 0xFF; }
            int64_t t = gen_spi_xfer(&G_IDEAL, chs, 500 * PS_PER_NS, 100 * PS_PER_NS, f, mo, mi, 4, 0, true, 3 * PS_PER_NS);
            G_IDEAL.t_end = t + 500 * PS_PER_NS;
            la_sample(&G_IDEAL, 10 * PS_PER_NS, &G_CAP);
            ok += spi_guess_mode(&G_CAP, 0, 1, 2) == f.mode;
            total++;
        }
        cap_init(&G_IDEAL, 4, nm, INIT_HI);                               /* MOSI 가 늘 1 (읽기 더미 0xFF) */
        const uint32_t ff[2] = { 0xFF, 0xFF };
        int64_t t = gen_spi_xfer(&G_IDEAL, chs, 500 * PS_PER_NS, 100 * PS_PER_NS, (spi_fmt){ 3, 8, 0 }, ff, ff, 2, 0, true, 0);
        G_IDEAL.ch[2].init = 1;
        G_IDEAL.t_end = t + 500 * PS_PER_NS;
        la_sample(&G_IDEAL, 10 * PS_PER_NS, &G_CAP);
        printf("ex2 spi mode guess: %d/%d, constant MOSI -> %d\n", ok, total, spi_guess_mode(&G_CAP, 0, 1, 2));
        CHECK(ok == total);
        CHECK(spi_guess_mode(&G_CAP, 0, 1, 2) == -1);
    }
    /* 연습 3 */
    {
        g_rng = 0xE3001u;
        static const uart_fmt truth[5] = { { 8, 'N', 1 }, { 8, 'E', 1 }, { 8, 'O', 1 }, { 7, 'E', 1 }, { 7, 'O', 1 } };
        int ok = 0;
        int64_t T = (PS_PER_S + 57600) / 115200;
        for (int it = 0; it < 250; it++) {
            uart_fmt f = truth[it % 5];
            uint32_t v[32];
            for (int i = 0; i < 32; i++) v[i] = xs32() & 0xFF;
            make_uart(T, f, v, NULL, 32, 100 * PS_PER_NS, 0, T / 50, 15 * T);
            uart_fmt g;
            uart_guess_fmt(&G_CAP.ch[0], G_CAP.t_end, T, &g);
            ok += g.data_bits == f.data_bits && g.parity == f.parity && g.stop_bits == f.stop_bits;
        }
        printf("ex3 uart format guess (32 random bytes, back-to-back): %d/250\n", ok);
        CHECK(ok == 250);
    }
    /* 연습 4 */
    {
        static const char *const nm[2] = { "SDA", "SCL" };
        static la_event exp[64];
        cap_init(&G_IDEAL, 2, nm, INIT_HI);
        i2c_wr w = { &G_IDEAL.ch[0], &G_IDEAL.ch[1], 5000 * PS_PER_NS, 1250 * PS_PER_NS, 625 * PS_PER_NS, 0, exp, 0, 0 };
        i2c_g_start(&w); i2c_g_byte(&w, 0x50 << 1, 1, true); i2c_g_stop(&w);              /* 없는 EEPROM */
        i2c_g_start(&w); i2c_g_byte(&w, 0x48 << 1, 0, true);                                /* CONFIG 쓰기 */
        i2c_g_byte(&w, 0x01, 0, false); i2c_g_byte(&w, 0x02, 0, false); i2c_g_byte(&w, 0x20, 0, false); i2c_g_stop(&w);
        const uint8_t t25[2] = { 0x0C, 0x80 }, tneg[2] = { 0xFE, 0x00 };
        i2c_g_regread(&w, 0x48, 0x00, t25, 2);
        i2c_g_regread(&w, 0x48, 0x00, tneg, 2);
        G_IDEAL.t_end = w.now + 5000 * PS_PER_NS;
        la_sample(&G_IDEAL, 100 * PS_PER_NS, &G_CAP);
        int n = i2c_decode(&G_CAP.ch[0], &G_CAP.ch[1], G_CAP.t_end, G_EV, 128);
        char out[512];
        i2c_summarize(G_EV, n, out, sizeof out);
        fputs(out, stdout);
        CHECK(strcmp(out, "w0@0x50 NACK\n"
                          "w3@0x48 0x01 0x02 0x20\n"
                          "w1@0x48 0x00 r2@0x48 0x0C 0x80 => 25.000 C\n"
                          "w1@0x48 0x00 r2@0x48 0xFE 0x00 => -4.000 C\n") == 0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
