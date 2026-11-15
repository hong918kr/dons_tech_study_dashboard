/* COMM-B03 demo_sweep.c — 클럭 편차 스윕 (solution.c 의 Part B 모델을 그대로 복사)
 *
 * 1) 온셋(onset): e = 0 에서 바깥으로 0.01% 씩 늘리며 처음으로 데이터 오류 / noise 플래그가 나는 편차.
 *    프레임 20000 개 (랜덤 데이터, 랜덤 위상). 이론 최악값(tol_theory)과 비교.
 * 2) 오류율 곡선: 8N1 / 8E1, back-to-back vs isolated, e = −7% … +7% (0.25% 간격), 프레임 4000 개.
 * 3) 8x 오버샘플링(샘플 4,5,6) 온셋.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o demo_sweep && ./demo_sweep
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
#define MAXF 40000

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

static int64_t p_for(int ovs, double e_pct) { return (int64_t)(ovs * 1000.0 / (1.0 + e_pct / 100.0) + 0.5); }
static double e_of(int ovs, int64_t p) { return (ovs * 1000.0 / (double)p - 1.0) * 100.0; }

/* sign 방향으로 0.01% 씩: 첫 오류 / 첫 NF 의 실제 편차 */
static void onset(const ufmt *f, int ovs, int gap, int sign, int frames, double *e_err, double *e_nf) {
    *e_err = *e_nf = 0;
    for (int k = 1; k <= 1000; k++) {
        double e = sign * 0.01 * k;
        int64_t p = p_for(ovs, e);
        link_stats st;
        sim_link(f, ovs, p, 1000, frames, gap, 0x5EED0000u + (uint32_t)k, &st);
        if (*e_nf == 0 && st.nf > 0) *e_nf = e_of(ovs, p);
        if (st.err > 0) { *e_err = e_of(ovs, p); if (*e_nf == 0) *e_nf = *e_err; return; }
    }
}

int main(void) {
    ufmt f81 = { 8, 'N', 1 }, f8e1 = { 8, 'E', 1 };
    struct { const char *name; const ufmt *f; int ovs, gap, j; double th_late_err, th_late_nf; } C[] = {
        { "8N1 x16 back-to-back", &f81, 16, 0, 9, 0, 0 }, { "8N1 x16 isolated", &f81, 16, 2, 9, 0, 0 },
        { "8E1 x16 back-to-back", &f8e1, 16, 0, 10, 0, 0 }, { "8E1 x16 isolated", &f8e1, 16, 2, 10, 0, 0 },
        { "8N1 x8  back-to-back", &f81, 8, 0, 9, 0, 0 }, { "8N1 x8  isolated", &f81, 8, 2, 9, 0, 0 },
    };
    printf("== onset (20000 frames, step 0.01%%) vs worst-case theory ==\n");
    printf("%-22s | %8s %8s | %8s %8s || %8s %8s | %8s %8s\n", "config", "fast err", "theory", "fast NF", "theory",
           "slow err", "theory", "slow NF", "theory");
    for (int c = 0; c < 6; c++) {
        int ovs = C[c].ovs, m = ovs / 2 + 1, j = C[c].j;
        /* late: b2b → stop 비트의 마지막 샘플 m+1 이 다음 start 에 닿으면 에지를 놓친다.
                 isolated → stop 뒤가 idle(1) 이라 무해. 마지막 '이웃이 다를 수 있는' 비트는 j−1 의 가운데 샘플 m. */
        double tl_err = C[c].gap == 0 ? tol_theory(j, m + 1, ovs, 1) : tol_theory(j - 1, m, ovs, 1);
        double tl_nf  = C[c].gap == 0 ? tol_theory(j, m + 1, ovs, 1) : tol_theory(j - 1, m + 1, ovs, 1);
        double te_err = tol_theory(j, m, ovs, 0), te_nf = tol_theory(j, m - 1, ovs, 0);
        double fe, fn, se, sn;
        onset(C[c].f, ovs, C[c].gap, +1, 20000, &fe, &fn);
        onset(C[c].f, ovs, C[c].gap, -1, 20000, &se, &sn);
        printf("%-22s | %+7.2f%% %+7.3f%% | %+7.2f%% %+7.3f%% || %+7.2f%% %+7.3f%% | %+7.2f%% %+7.3f%%\n",
               C[c].name, fe, tl_err, fn, tl_nf, se, te_err, sn, te_nf);
    }

    printf("\n== error rate (%%) vs mismatch, 4000 frames, x16 ==\n");
    printf("%7s | %9s %9s %9s %9s\n", "e%", "8N1 b2b", "8N1 iso", "8E1 b2b", "8E1 iso");
    for (int k = -28; k <= 28; k++) {
        double e = 0.25 * k;
        if (e > -3.0 && e < 3.0) continue;
        int64_t p = p_for(16, e);
        printf("%+6.2f%% |", e_of(16, p));
        for (int c = 0; c < 4; c++) {
            link_stats st;
            sim_link(C[c].f, 16, p, 1000, 4000, C[c].gap, 0xABCD0000u + (uint32_t)(k + 100) * 7u + (uint32_t)c, &st);
            printf(" %8.2f%%", 100.0 * st.err / 4000.0);
        }
        printf("\n");
    }
    return 0;
}
