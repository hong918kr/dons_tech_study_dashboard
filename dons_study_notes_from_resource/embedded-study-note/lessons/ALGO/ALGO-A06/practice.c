/* ALGO-A06 추가 연습 — 한 번에 검증
 *   연습 1  package-merge: 최적 길이 제한 허프만 → 전수 탐색과 같은 비용, 본문 휴리스틱(huff_limit)과 비교
 *   연습 2  미리 학습한 고정 표(헤더 없음) vs 블록마다 표 — 작은 블록에서 128 B 헤더가 얼마나 비싼가
 *   연습 3  zigzag 영역의 0-런 부호 vs 바이트 RLE
 *   연습 4  2차 델타(delta-of-delta): 매끄러운 신호에선 이득, 백색 잡음에선 분산 3배 (+½·log2 3 비트)
 * solution.c 의 함수(델타·zigzag·varint·RLE·허프만·생성기)를 그대로 가져다 쓴다 (main 은 빼고).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice
 */
#define ALGO_A06_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"
#include "solution.c"

/* ---------- 연습 1: package-merge (Larmore–Hirschberg) ----------
 * 레벨 L 목록 = 잎(빈도 오름차순). 레벨 j (L−1 … 1) 목록 = 잎 ∪ (레벨 j+1 목록의 이웃 두 개씩 묶은 꾸러미), 무게순 병합.
 * 레벨 1 목록의 앞 2n−2 개를 고르고, 고른 꾸러미는 아래 레벨의 앞 2P 개를 고른 것으로 내려보낸다.
 * 기호의 부호 길이 = 고른 항목들 중 그 잎이 나타난 횟수. 비용 = 길이 ≤ L 인 접두 부호 중 최소 (최적). */
static int pm_lengths(const uint32_t *freq, uint8_t *len, int L) {
    static uint64_t w[HUF_MAXBITS + 1][2 * NSYM];
    static int kind[HUF_MAXBITS + 1][2 * NSYM];          /* ≥ 0: 잎 순번, < 0: 꾸러미 번호 −1−k */
    int cnt[HUF_MAXBITS + 1], sym[NSYM], n = 0;
    memset(len, 0, NSYM);
    for (int s = 0; s < NSYM; s++) if (freq[s]) sym[n++] = s;
    if (n == 0) return 0;
    if (n == 1) { len[sym[0]] = 1; return 0; }
    if (L < 1 || L > HUF_MAXBITS || n > (1 << L)) return -1;
    for (int i = 1; i < n; i++) {
        int s = sym[i], j = i;
        while (j > 0 && freq[sym[j - 1]] > freq[s]) { sym[j] = sym[j - 1]; j--; }
        sym[j] = s;
    }
    for (int i = 0; i < n; i++) { w[L][i] = freq[sym[i]]; kind[L][i] = i; }
    cnt[L] = n;
    for (int j = L - 1; j >= 1; j--) {
        int P = cnt[j + 1] / 2, a = 0, b = 0, o = 0;
        while (a < n || b < P) {
            uint64_t pw = b < P ? w[j + 1][2 * b] + w[j + 1][2 * b + 1] : 0;
            if (b >= P || (a < n && freq[sym[a]] <= pw)) { w[j][o] = freq[sym[a]]; kind[j][o++] = a++; }
            else { w[j][o] = pw; kind[j][o++] = -1 - b++; }
        }
        cnt[j] = o;
    }
    int m = 2 * n - 2;
    for (int j = 1; j <= L && m > 0; j++) {
        int P = 0;
        for (int t = 0; t < m && t < cnt[j]; t++) {
            if (kind[j][t] >= 0) len[sym[kind[j][t]]]++;
            else P++;
        }
        m = 2 * P;
    }
    return 0;
}

/* ---------- 연습 2: 고정 표 ----------
 * 블록 = [mode 0xF0][nch][nframes u16][RLE 바이트 수 u32][부호 비트] — 128 B 길이표 대신 펌웨어 플래시의 상수 표를 쓴다. */
typedef struct { uint8_t len[NSYM]; uint16_t code[NSYM]; hdec_t dec; } fixed_t;
static size_t fixed_enc(const fixed_t *F, const int16_t *x, size_t nf, int nch, uint8_t *out, size_t cap) {
    pstat_t st;
    static uint8_t tmp[MAX_BLOCK];
    if (pipe_encode(x, nf, nch, tmp, sizeof tmp, &st) == 0 || cap < 8) return 0;
    out[0] = 0xF0; out[1] = (uint8_t)nch; out[2] = (uint8_t)nf; out[3] = (uint8_t)(nf >> 8);
    for (int k = 0; k < 4; k++) out[4 + k] = (uint8_t)(st.rle >> (8 * k));
    bw_t w = { out + 8, cap - 8, 0, 0, 0, 0 };
    for (size_t i = 0; i < st.rle; i++) bw_put(&w, F->code[st.rb[i]], F->len[st.rb[i]]);
    bw_flush(&w);
    return w.ovf ? 0 : 8 + w.pos;
}
static long fixed_dec(const fixed_t *F, const uint8_t *in, size_t len, int16_t *x, size_t cap) {
    static uint8_t rb[MAX_RLE], vb[MAX_VARINT];
    static uint16_t z[MAX_SAMPLES];
    if (len < 8 || in[0] != 0xF0) return -1;
    int nch = in[1];
    size_t nf = (size_t)in[2] | (size_t)in[3] << 8, n = nf * (size_t)nch;
    uint32_t m = (uint32_t)in[4] | (uint32_t)in[5] << 8 | (uint32_t)in[6] << 16 | (uint32_t)in[7] << 24;
    if (nch < 1 || nch > 8 || n > cap || n > MAX_SAMPLES || m > MAX_RLE) return -1;
    br_t r = { in + 8, len - 8, 0, 0 };
    for (uint32_t i = 0; i < m; i++) { int s = huff_dec_sym(&F->dec, &r); if (s < 0) return -1; rb[i] = (uint8_t)s; }
    long nv = rle_dec(rb, m, vb, sizeof vb);
    if (nv < 0 || varint_dec(vb, (size_t)nv, z, n) != 0) return -1;
    for (size_t i = 0; i < n; i++) z[i] = zz16_dec(z[i]);
    delta16_dec(z, n, nch, x);
    return (long)nf;
}
static void fixed_train(fixed_t *F, const int16_t *x, size_t nframes, int nch, size_t blk) {
    uint32_t f[NSYM];
    static uint8_t tmp[MAX_BLOCK];
    for (int s = 0; s < NSYM; s++) f[s] = 1;             /* 라플라스 평활: 학습에 없던 바이트도 부호를 갖게 */
    for (size_t f0 = 0; f0 < nframes; f0 += blk) {
        pstat_t st;
        size_t nf = nframes - f0 < blk ? nframes - f0 : blk;
        pipe_encode(x + f0 * (size_t)nch, nf, nch, tmp, sizeof tmp, &st);
        for (size_t i = 0; i < st.rle; i++) f[st.rb[i]]++;
    }
    huff_lengths(f, F->len);
    huff_limit(f, F->len, HUF_MAXBITS);
    huff_canon(F->len, F->code);
    hdec_build(&F->dec, F->len);
}

/* ---------- 연습 3: zigzag 영역의 0-런 ----------
 * 0 이 아닌 값 v → varint(v).  0 이 k 개 (1..65536) 이어지면 → 0x00 + varint(k−1).
 * 0 이 아닌 값의 varint 첫 바이트는 절대 0x00 이 아니다 (v < 128 이면 v, 아니면 연속 비트) → 0x00 이 런 표시로 안전. */
static size_t zr_enc(const uint16_t *z, size_t n, uint8_t *out) {
    size_t o = 0;
    for (size_t i = 0; i < n;) {
        if (z[i]) { o += (size_t)vu16_put(out + o, z[i]); i++; continue; }
        size_t k = 1;
        while (i + k < n && z[i + k] == 0 && k < 65536) k++;
        out[o++] = 0;
        o += (size_t)vu16_put(out + o, (uint16_t)(k - 1));
        i += k;
    }
    return o;
}
static long zr_dec(const uint8_t *in, size_t len, uint16_t *z, size_t cap) {
    size_t i = 0, o = 0;
    while (i < len) {
        uint16_t v;
        int c = vu16_get(in + i, len - i, &v);
        if (!c) return -1;
        i += (size_t)c;
        if (v) { if (o >= cap) return -1; z[o++] = v; continue; }
        c = vu16_get(in + i, len - i, &v);
        if (!c || (size_t)v + 1 > cap - o) return -1;
        i += (size_t)c;
        for (size_t k = 0; k <= v; k++) z[o++] = 0;
    }
    return (long)o;
}

/* ---------- 연습 4: 2차 델타 ---------- */
static void dd_enc(const int16_t *x, size_t n, uint16_t *e) {
    static uint16_t d1[MAX_SAMPLES]; static int16_t t[MAX_SAMPLES];
    delta16_enc(x, n, 1, d1);
    for (size_t i = 0; i < n; i++) t[i] = bits_i16(d1[i]);
    delta16_enc(t, n, 1, e);
}
static void dd_dec(const uint16_t *e, size_t n, int16_t *x) {
    static uint16_t d1[MAX_SAMPLES]; static int16_t t[MAX_SAMPLES];
    delta16_dec(e, n, 1, t);
    for (size_t i = 0; i < n; i++) d1[i] = i16_bits(t[i]);
    delta16_dec(d1, n, 1, x);
}
static double h16_of(const uint16_t *v, size_t n) {
    static uint32_t h[65536];
    memset(h, 0, sizeof h);
    for (size_t i = 0; i < n; i++) h[v[i]]++;
    return entropy(h, 65536, n);
}
static double var_of(const uint16_t *v, size_t n, size_t skip) {   /* 부호 있는 값으로 본 표본 분산 */
    double s = 0, s2 = 0;
    for (size_t i = skip; i < n; i++) { double d = bits_i16(v[i]); s += d; s2 += d * d; }
    double m = s / (double)(n - skip);
    return s2 / (double)(n - skip) - m * m;
}

int main(void) {
    uint32_t seed = 0x5EEDu;
    gen_temp(g_temp, TEMP_N, 2026);
    gen_accel(g_acc, ACC_F, 1111);

    /* 연습 1 */
    {
        uint32_t f[NSYM]; uint8_t lp[NSYM], lh[NSYM];
        int eq = 1, ge = 1, worse = 0, cases = 0;
        for (int t = 0; t < 120; t++) {
            int n = 4 + t % 5, L = 3 + t % 2;
            memset(f, 0, sizeof f);
            for (int i = 0; i < n; i++) f[i] = (t % 3 == 0) ? (1u << i) : 1 + xs32(&seed) % 200;
            pm_lengths(f, lp, L);
            huff_lengths(f, lh); huff_limit(f, lh, L);
            uint64_t best = brute_opt(f, n, L), cp = huff_cost(f, lp), ch = huff_cost(f, lh);
            if (cp != best || max_len(lp) > L || kraft(lp, L) > 1u << L) eq = 0;
            if (ch < cp) ge = 0;
            cases++; worse += ch > cp;
        }
        printf("ex1: package-merge == brute force on %d cases; heuristic worse on %d\n", cases, worse);
        CHECK(eq);                                           /* package-merge = 최적 */
        CHECK(ge);
        uint32_t a = 1, b = 1;
        memset(f, 0, sizeof f);
        for (int i = 0; i < 20; i++) { f[i] = a; uint32_t c = a + b; a = b; b = c; }
        pm_lengths(f, lp, 15);
        huff_lengths(f, lh); huff_limit(f, lh, 15);
        printf("ex1: fib20 L=15: package-merge %llu bits, heuristic %llu bits\n",
               (unsigned long long)huff_cost(f, lp), (unsigned long long)huff_cost(f, lh));
        CHECK(huff_cost(f, lp) <= huff_cost(f, lh) && max_len(lp) == 15);
        int same = 1;                                        /* 제한이 필요 없으면 허프만과 같은 비용 */
        for (int t = 0; t < 200; t++) {
            memset(f, 0, sizeof f);
            for (int i = 0; i < 300; i++) f[xs32(&seed) % NSYM] += 1 + xs32(&seed) % 100;
            if (huff_lengths(f, lh) > 15) continue;
            pm_lengths(f, lp, 15);
            if (huff_cost(f, lp) != huff_cost(f, lh)) same = 0;
        }
        CHECK(same);
    }
    /* 연습 2 */
    {
        static uint8_t blk[MAX_BLOCK];
        static int16_t y[MAX_SAMPLES];
        int rt = 1;
        for (int which = 0; which < 2; which++) {
            fixed_t F;
            const int16_t *x = which ? g_acc : g_temp;
            int nch = which ? 3 : 1;
            size_t total = which ? ACC_F : TEMP_N, train = which ? 12000 : 21600;   /* 앞 2분 / 앞 6시간 */
            fixed_train(&F, x, train, nch, 1024);
            const size_t blks[3] = { 256, 1024, 4096 };
            for (int k = 0; k < 3; k++) {
                size_t B = which ? blks[k] / 2 : blks[k];     /* 가속도는 프레임당 3 샘플이라 절반 */
                size_t per = 0, fix = 0, raw = 0;
                for (size_t f0 = train; f0 < total; f0 += B) {
                    size_t nf = total - f0 < B ? total - f0 : B;
                    const int16_t *xb = x + f0 * (size_t)nch;
                    pstat_t st;
                    per += pipe_encode(xb, nf, nch, blk, sizeof blk, &st);
                    size_t b = fixed_enc(&F, xb, nf, nch, blk, sizeof blk);
                    fix += b;
                    raw += 2 * nf * (size_t)nch;
                    if (b == 0 || fixed_dec(&F, blk, b, y, MAX_SAMPLES) != (long)nf || memcmp(xb, y, nf * (size_t)nch * sizeof *y) != 0) rt = 0;
                }
                printf("ex2: %s block=%5zu samples: raw %6zu  per-block table %6zu (%.1f%%)  fixed table %6zu (%.1f%%)\n",
                       which ? "accel" : "temp ", B * (size_t)nch, raw, per, 100.0 * (double)per / (double)raw, fix, 100.0 * (double)fix / (double)raw);
                if (k == 0) CHECK(fix < per);                /* 작은 블록에선 고정 표가 이긴다 */
            }
        }
        CHECK(rt);
    }
    /* 연습 3 */
    {
        static uint8_t o[MAX_VARINT + 8], h[MAX_HUF];
        static uint16_t back[MAX_SAMPLES];
        size_t tot_zr = 0, tot_rle = 0, tot_zrh = 0, tot_rh = 0; int rt = 1;
        static uint8_t tmp[MAX_BLOCK];
        for (size_t f0 = 0; f0 < TEMP_N; f0 += 4096) {
            size_t nf = TEMP_N - f0 < 4096 ? TEMP_N - f0 : 4096;
            pstat_t st;
            pipe_encode(g_temp + f0, nf, 1, tmp, sizeof tmp, &st);
            size_t m = zr_enc(st.zz, st.n, o);
            if (zr_dec(o, m, back, MAX_SAMPLES) != (long)st.n || memcmp(back, st.zz, st.n * sizeof *back) != 0) rt = 0;
            tot_zr += m; tot_rle += st.rle; tot_rh += st.huf_vr;
            tot_zrh += huff_block_enc(o, m, h, sizeof h, NULL);
        }
        printf("ex3: temp zero-run %zu B vs varint+RLE %zu B | +Huffman: %zu B vs %zu B\n", tot_zr, tot_rle, tot_zrh, tot_rh);
        CHECK(rt);
        CHECK(tot_zr < tot_rle);
        uint16_t z5[5] = { 0, 0, 0, 7, 0 };
        size_t m = zr_enc(z5, 5, o);
        CHECK(m == 5 && o[0] == 0 && o[1] == 2 && o[2] == 7 && o[3] == 0 && o[4] == 0);
        uint8_t bad[] = { 0x00, 0xFF, 0xFF, 0x03 };          /* 65536 개 0 → cap 초과 */
        CHECK(zr_dec(bad, 4, back, 100) == -1 && zr_dec(bad, 1, back, 100) == -1);
    }
    /* 연습 4 */
    {
        static int16_t ramp[MAX_SAMPLES], noise[MAX_SAMPLES], back[MAX_SAMPLES];
        static uint16_t d1[MAX_SAMPLES], d2[MAX_SAMPLES];
        int32_t pos = 0, vel = 0;
        for (size_t i = 0; i < MAX_SAMPLES; i++) {           /* 엔코더 위치: 속도가 천천히 변한다 */
            vel += (int32_t)(xs32(&seed) % 3) - 1;
            if (vel > 40) vel = 40;
            if (vel < -40) vel = -40;
            pos += vel;
            ramp[i] = (int16_t)(pos & 0x7FFF);               /* 15비트 카운터처럼 감긴다 */
            int32_t v = 0;
            for (int k = 0; k < 3; k++) v += (int32_t)(xs32(&seed) % 17) - 8;
            noise[i] = (int16_t)v;                           /* 백색 잡음 σ ≈ 8.5 */
        }
        int rt = 1;
        double hr1, hr2, hn1, hn2, vr;
        delta16_enc(ramp, MAX_SAMPLES, 1, d1); for (size_t i = 0; i < MAX_SAMPLES; i++) d1[i] = zz16_enc(d1[i]);
        dd_enc(ramp, MAX_SAMPLES, d2); dd_dec(d2, MAX_SAMPLES, back); if (memcmp(back, ramp, sizeof ramp)) rt = 0;
        for (size_t i = 0; i < MAX_SAMPLES; i++) d2[i] = zz16_enc(d2[i]);
        hr1 = h16_of(d1, MAX_SAMPLES); hr2 = h16_of(d2, MAX_SAMPLES);
        delta16_enc(noise, MAX_SAMPLES, 1, d1);
        dd_enc(noise, MAX_SAMPLES, d2); dd_dec(d2, MAX_SAMPLES, back); if (memcmp(back, noise, sizeof noise)) rt = 0;
        vr = var_of(d2, MAX_SAMPLES, 2) / var_of(d1, MAX_SAMPLES, 1);
        for (size_t i = 0; i < MAX_SAMPLES; i++) { d1[i] = zz16_enc(d1[i]); d2[i] = zz16_enc(d2[i]); }
        hn1 = h16_of(d1, MAX_SAMPLES); hn2 = h16_of(d2, MAX_SAMPLES);
        printf("ex4: ramp H(zz d1)=%.3f H(zz d2)=%.3f bits | white noise H(zz d1)=%.3f H(zz d2)=%.3f (+%.3f), Var(d2)/Var(d1)=%.3f\n",
               hr1, hr2, hn1, hn2, hn2 - hn1, vr);
        CHECK(rt);
        CHECK(hr2 < hr1 - 2.0);                              /* 매끄러운 신호: 2차 델타가 크게 이득 */
        CHECK(vr > 2.8 && vr < 3.2);                         /* 백색 잡음: 6σ² / 2σ² = 3 */
        CHECK(hn2 - hn1 > 0.6 && hn2 - hn1 < 1.0);           /* ≈ ½·log2 3 = 0.792 비트 손해 */
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
