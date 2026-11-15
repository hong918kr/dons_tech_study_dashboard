/* COMM-B02 실험 — 샘플 잡음에 대한 다수결(8/9/10) vs 샘플 1개(9) 수신기
 *
 * 한 번의 시행: idle 2비트 + 랜덤 바이트 1문자 + idle 3비트 를 16x 샘플로 만들고,
 * 모든 샘플(idle 포함)을 독립적으로 확률 p 로 뒤집는다. 수신기를 새로 시작해 결과를 분류한다.
 *   exact   : 프레임 1개, 값 맞음, FE/PE 없음 (NF 는 있어도 됨)
 *   flagged : 틀렸지만 어떤 프레임에 FE 또는 PE 가 붙음 (드라이버가 알 수 있음)
 *   lost    : 프레임 0개 (start 가 글리치로 판정돼 버려짐 등)
 *   silent  : 프레임은 나왔고 FE/PE 없음, 그런데 값이 틀리거나 프레임 수가 1이 아님 (조용한 오류)
 * 수신기 3종: maj3 = solution.c 의 rx_tick 축약판 (false start 뒤 prev=1 재무장 포함),
 * single = 샘플 9 하나만 (재무장 포함), naive = maj3 에서 재무장을 뺀 것 (연습 5). theory = 1 − (1 − q)^(프레임 비트 수), q = 비트 하나의 판정 오류 확률.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_noise.c -o demo && ./demo
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { SR_NF = 1u << 2, SR_FE = 1u << 3, SR_PE = 1u << 4 };
typedef struct { int data_bits, parity; } cfg_t;   /* parity 0=N 1=E */

typedef struct {
    cfg_t cfg; bool single, naive;
    int st, ph, nbit; uint8_t prev, smp, noisy, par; uint16_t shift;
    int nfr; uint16_t d[4]; uint8_t f[4];
} rx_t;

static int parity_of(unsigned v) { int p = 0; while (v) { p ^= (int)(v & 1u); v >>= 1; } return p; }

static void rx_tick(rx_t *r, uint8_t s) {
    if (r->st == 0) {
        if (r->prev && !s) { r->st = 1; r->ph = 1; r->smp = 0; r->nbit = 0; r->shift = 0; r->noisy = 0; r->par = 0; }
        r->prev = s; return;
    }
    r->ph++;
    if (r->ph >= 8 && r->ph <= 10) r->smp |= (uint8_t)(s << (r->ph - 8));
    if (r->ph == 10) {
        int a = r->smp & 1, b = (r->smp >> 1) & 1, c = (r->smp >> 2) & 1;
        int v = r->single ? b : (a + b + c >= 2);
        if (!r->single) r->noisy |= (uint8_t)!(a == b && b == c);
        r->smp = 0;
        if (r->st == 1 && v) { r->st = 0; if (!r->naive) { r->prev = 1; return; } }   /* 재무장 (naive 는 안 함) */
        else if (r->st == 2) { r->shift |= (uint16_t)(v << r->nbit); r->nbit++; }
        else if (r->st == 3) r->par = (uint8_t)v;
        else if (r->st == 4) {
            uint8_t f = r->noisy ? SR_NF : 0;
            if (r->cfg.parity && r->par != parity_of(r->shift)) f |= SR_PE;
            if (!v) f |= SR_FE;
            if (r->nfr < 4) { r->d[r->nfr] = r->shift; r->f[r->nfr] = f; }
            r->nfr++;
            r->st = 0;
        }
    }
    if (r->ph == 16 && r->st != 0) {
        r->ph = 0;
        if (r->st == 1) r->st = 2;
        else if (r->st == 2 && r->nbit == r->cfg.data_bits) r->st = r->cfg.parity ? 3 : 4;
        else if (r->st == 3) r->st = 4;
    }
    r->prev = s;
}

static uint32_t g_rng = 0x9E3779B9u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

int main(void) {
    const double ps[] = {0.001, 0.003, 0.01, 0.03, 0.1};
    const cfg_t cfgs[2] = {{8, 0}, {8, 1}};
    const char *names[2] = {"8N1", "8E1"};
    const int N = 200000;
    uint8_t line[16 * 16];
    printf("trials per row = %d, every sample flipped independently with prob p (idle included)\n", N);
    printf("cfg  p      rx     | not-exact  theory    | flagged   lost      silent    silent&!NF | NF on exact\n");
    for (int k = 0; k < 2; k++) {
        int nbits = 1 + cfgs[k].data_bits + cfgs[k].parity + 1;
        for (size_t pi = 0; pi < sizeof ps / sizeof ps[0]; pi++) {
            double p = ps[pi];
            uint32_t thr = (uint32_t)(p * 4294967296.0);
            for (int mode = 0; mode < 3; mode++) {
                long exact = 0, flagged = 0, lost = 0, silent = 0, silent_nonf = 0, nf_exact = 0;
                for (int t = 0; t < N; t++) {
                    uint8_t byte = (uint8_t)xs32();
                    int n = 0;
                    for (int i = 0; i < 32; i++) line[n++] = 1;
                    for (int i = 0; i < 16; i++) line[n++] = 0;
                    for (int b = 0; b < 8; b++) for (int i = 0; i < 16; i++) line[n++] = (byte >> b) & 1u;
                    if (cfgs[k].parity) for (int i = 0; i < 16; i++) line[n++] = (uint8_t)parity_of(byte);
                    for (int i = 0; i < 16 * 4; i++) line[n++] = 1;      /* stop + idle 3 */
                    for (int i = 0; i < n; i++) if (xs32() < thr) line[i] ^= 1u;
                    rx_t r; memset(&r, 0, sizeof r); r.cfg = cfgs[k]; r.single = (mode == 1); r.naive = (mode == 2); r.prev = 1;
                    for (int i = 0; i < n; i++) rx_tick(&r, line[i]);
                    int m = r.nfr < 4 ? r.nfr : 4;
                    bool any_fp = false, any_nf = false;
                    for (int i = 0; i < m; i++) { any_fp |= (r.f[i] & (SR_FE | SR_PE)) != 0; any_nf |= (r.f[i] & SR_NF) != 0; }
                    if (r.nfr == 1 && r.d[0] == byte && !any_fp) { exact++; nf_exact += any_nf; }
                    else if (r.nfr == 0) lost++;
                    else if (any_fp) flagged++;
                    else { silent++; silent_nonf += !any_nf; }
                }
                double q = (mode == 1) ? p : 3 * p * p * (1 - p) + p * p * p;
                double theory = 1 - pow(1 - q, nbits);
                printf("%s  %-6g %-6s | %8.4f%%  %8.4f%% | %8.4f%% %8.4f%% %8.4f%% %8.4f%%  | %7.2f%%\n",
                       names[k], p, mode == 0 ? "maj3" : mode == 1 ? "single" : "naive",
                       100.0 * (N - exact) / N, 100 * theory,
                       100.0 * flagged / N, 100.0 * lost / N, 100.0 * silent / N, 100.0 * silent_nonf / N,
                       exact ? 100.0 * nf_exact / exact : 0.0);
            }
        }
    }
    return 0;
}
