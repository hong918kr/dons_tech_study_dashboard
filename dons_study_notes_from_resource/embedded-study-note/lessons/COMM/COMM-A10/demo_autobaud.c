/* COMM-A10 demo: auto-baud 정확도 / 고정 baud 디코드의 클럭 오차 한계 / SPI·I2C 의 최소 샘플링률
 * solution.c 의 디코더·생성기를 그대로 쓴다 (LA_NO_MAIN 으로 main 만 뺌). 레슨 폴더에서 빌드:
 *   cc -std=c11 -Wall -Wextra -O2 demo_autobaud.c -o demo_autobaud && ./demo_autobaud
 */
#include <math.h>
#define LA_NO_MAIN
#pragma GCC diagnostic ignored "-Wunused-function"   /* solution.c 함수 중 여기서 안 쓰는 것 */
#include "solution.c"

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* 표 1: OVS(= fs / baud) 와 클럭 오차에 따른 auto-baud 오차, 그 추정으로 디코드한 성공률 */
static void table_autobaud(void) {
    static const double ovs[] = { 2.5, 3, 4, 5, 6, 8, 12, 16, 32, 100 };
    const int64_t ts = 100 * PS_PER_NS;                                  /* 10 MS/s 고정, baud 를 바꿈 */
    uart_fmt f = { 8, 'N', 1 };
    printf("[1] auto-baud @ fs = 10 MS/s, 8N1, 20 random bytes/capture, idle gap U[0,3T], jitter +-1%% T, 300 captures/row\n");
    printf("  OVS  | clk err | median |err| | p99 |err| | max |err| | captures fully decoded\n");
    for (size_t r = 0; r < sizeof ovs / sizeof ovs[0]; r++)
        for (int ce = 0; ce < 2; ce++) {
            static double errs[300];
            int full = 0;
            g_rng = 0xDE300u + (uint32_t)r * 7 + (uint32_t)ce;
            for (int k = 0; k < 300; k++) {
                double e = ce ? (double)rnd_range(-300, 300) / 10000.0 : 0.0;
                int64_t T = (int64_t)((double)ts * ovs[r] / (1.0 + e) + 0.5);
                uint32_t v[20];
                for (int i = 0; i < 20; i++) v[i] = xs32() & 0xFF;
                make_uart(T, f, v, NULL, 20, ts, 3 * T, T / 100, 15 * T);
                uart_est est;
                if (uart_autobaud(&G_CAP.ch[0], &est) != LA_OK) { errs[k] = 1; continue; }
                errs[k] = fabs((double)(est.bit_ps - T) / (double)T);
                int n = uart_decode(&G_CAP.ch[0], G_CAP.t_end, est.bit_ps, f, G_EV, 64);
                full += ev_list_same(G_EV, n, G_EXP, 20);
            }
            qsort(errs, 300, sizeof errs[0], cmp_d);
            printf("  %5.1f | %-7s | %10.3f%%  | %8.3f%% | %8.3f%% | %5.1f%%\n", ovs[r], ce ? "+-3%" : "0",
                   errs[150] * 100, errs[296] * 100, errs[299] * 100, full / 3.0);
        }
}

/* 표 2: auto-baud 없이 "공칭 baud" 로 디코드할 때, TX 클럭 오차가 얼마까지 견디나 */
static void table_fixed_baud(void) {
    static const uart_fmt fm[3] = { { 8, 'N', 1 }, { 8, 'E', 1 }, { 8, 'N', 2 } };
    static const char *const fn[3] = { "8N1", "8E1", "8N2" };
    static const int ovs[2] = { 8, 100 };
    printf("\n[2] fixed nominal baud decode vs TX clock error (fs = 10 MS/s, 100 captures x 20 bytes, back-to-back frames)\n");
    printf("  fmt  OVS |");
    for (int e = -7; e <= 7; e++) printf(" %+d%%  ", e);
    printf("| theory: -1/(2n+1) .. (n+1)/(n+0.5+1/OVS)-1, n = last bit index\n");
    for (int fi = 0; fi < 3; fi++)
        for (int oi = 0; oi < 2; oi++) {
            const int64_t ts = 100 * PS_PER_NS;
            int64_t Tn = ts * ovs[oi];
            printf("  %s %3d |", fn[fi], ovs[oi]);
            for (int e = -7; e <= 7; e++) {
                int ok = 0;
                g_rng = 0xDE500u + (uint32_t)(fi * 100 + oi * 30 + e + 10);
                for (int k = 0; k < 100; k++) {
                    int64_t T = (int64_t)((double)Tn / (1.0 + e / 100.0) + 0.5);
                    uint32_t v[20];
                    for (int i = 0; i < 20; i++) v[i] = xs32() & 0xFF;
                    make_uart(T, fm[fi], v, NULL, 20, ts, 0, 0, 15 * T);
                    int n = uart_decode(&G_CAP.ch[0], G_CAP.t_end, Tn, fm[fi], G_EV, 64);
                    ok += ev_list_same(G_EV, n, G_EXP, 20);
                }
                printf(" %4d ", ok);
            }
            double n = uart_nbits(fm[fi]) - 1;                            /* 마지막 비트 번호 */
            printf("| %.2f%% .. +%.2f%%\n", -100.0 / (2 * n + 1), 100.0 * ((n + 1) / (n + 0.5 + 1.0 / ovs[oi]) - 1));
        }
}

/* 표 3: 클럭 한 주기당 샘플 수(OVS)에 따른 SPI / I2C 디코드 성공률 */
static void table_spi_i2c(void) {
    static const int ovs[] = { 2, 3, 4, 5, 6, 8, 10, 16 };
    printf("\n[3] SPI (mode 0..3, 4-byte transfers) and I2C (TMP117-like reg read) decode success vs samples per clock period\n");
    printf("  OVS | SPI ok/400 | I2C ok/200\n");
    for (size_t r = 0; r < sizeof ovs / sizeof ovs[0]; r++) {
        int spi_ok = 0, i2c_ok = 0;
        g_rng = 0xDE700u + (uint32_t)r;
        static const char *const sn[4] = { "CS_N", "SCLK", "MOSI", "MISO" };
        const int chs[4] = { 0, 1, 2, 3 };
        for (int k = 0; k < 400; k++) {
            spi_fmt f = { (uint8_t)(k & 3), 8, 0 };
            int init[4] = { 1, f.mode >> 1, 0, 0 };
            cap_init(&G_IDEAL, 4, sn, init);
            int64_t P = 100 * PS_PER_NS * ovs[r];                        /* fs = 10 MS/s, SCK = fs/OVS */
            uint32_t mo[4], mi[4];
            for (int w = 0; w < 4; w++) {
                mo[w] = xs32() & 0xFF; mi[w] = xs32() & 0xFF;
                la_event ex = { 0, 0, EV_SPI, 0, 8, mo[w], mi[w] };
                G_EXP[w] = ex;
            }
            int64_t t = gen_spi_xfer(&G_IDEAL, chs, rnd_range(2 * P, 3 * P), P / 2, f, mo, mi, 4, 0, true, P / 100);
            G_IDEAL.t_end = t + 2 * P;
            la_sample(&G_IDEAL, 100 * PS_PER_NS, &G_CAP);
            int n = spi_decode(&G_CAP, 0, 1, 2, 3, f, G_EV, 64);
            spi_ok += ev_list_same(G_EV, n, G_EXP, 4);
        }
        static const char *const nm[2] = { "SDA", "SCL" };
        for (int k = 0; k < 200; k++) {
            cap_init(&G_IDEAL, 2, nm, INIT_HI);
            int64_t P = 100 * PS_PER_NS * ovs[r];
            i2c_wr w = { &G_IDEAL.ch[0], &G_IDEAL.ch[1], rnd_range(2 * P, 3 * P), P / 2, P / 4, P / 100, G_EXP, 0, 0 };
            uint8_t d[2] = { (uint8_t)xs32(), (uint8_t)xs32() };
            i2c_g_regread(&w, (uint8_t)rnd_range(0x08, 0x77), (uint8_t)xs32(), d, 2);
            G_IDEAL.t_end = w.now + 2 * P;
            la_sample(&G_IDEAL, 100 * PS_PER_NS, &G_CAP);
            int n = i2c_decode(&G_CAP.ch[0], &G_CAP.ch[1], G_CAP.t_end, G_EV, 64);
            i2c_ok += ev_list_same(G_EV, n, G_EXP, w.nexp);
        }
        printf("  %3d | %10d | %10d\n", ovs[r], spi_ok, i2c_ok);
    }
}

int main(void) {
    table_autobaud();
    table_fixed_baud();
    table_spi_i2c();
    return 0;
}
