/* SIV-M05 추가 연습 — 한 번에 검증
 *   연습 1 — 2D eye (Vref x delay): Vref 별 폭, 최대 폭 plateau 중앙의 Vref, eye 높이
 *   연습 2 — row hammer: 한쪽 vs 양쪽 aggressor, refresh 주기 2배의 효과
 *   연습 3 — LFSR 주소 생성기 테이블 (4~20비트) 주기·전수 커버 검사 + 비원시 다항식 검출
 *   연습 4 — training 반복 안정성: 8회 반복의 중심 흔들림(spread) + 최악 폭
 *   연습 5 — DQ swizzle: 컨트롤러 비트 번호 → DRAM 핀 이름
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* ---------------- 연습 1: 2D eye ---------------- */
#define ND 64          /* delay tap 0..63   */
#define NV 32          /* Vref step 0..31   */
static bool eye_pass(int d, int v) {                 /* 타원 eye: 중심 (30, 18), 반폭 14 tap, 반높이 9 step */
    long dx = d - 30, dv = v - 18, hw = 14, hv = 9;
    return dx * dx * hv * hv + dv * dv * hw * hw < hw * hw * hv * hv;
}
/* Vref v 에서 가장 긴 연속 pass 구간 [*l, *r] 와 그 길이(pass tap 수) */
static int row_width(int v, int *l, int *r) {
    int best = 0, run = 0;
    for (int d = 0; d < ND; d++) {
        run = eye_pass(d, v) ? run + 1 : 0;
        if (run > best) { best = run; *r = d; *l = d - run + 1; }
    }
    return best;
}
static int col_height(int d) {                        /* delay d 에서 연속 pass Vref 수 */
    int best = 0, run = 0;
    for (int v = 0; v < NV; v++) { run = eye_pass(d, v) ? run + 1 : 0; if (run > best) best = run; }
    return best;
}

/* ---------------- 연습 2: row hammer 축소 모델 (bank 1개) ---------------- */
#define NR 64
#define TH 600u
typedef struct { uint32_t disturb[NR]; int open; uint32_t nacc, refresh_every; bool flipped; int weak; } hm_t;
static void hm_init(hm_t *h, uint32_t refresh_every, int weak) {
    memset(h, 0, sizeof *h); h->open = -1; h->refresh_every = refresh_every; h->weak = weak;
}
static void hm_act(hm_t *h, int r) {
    if (++h->nacc % h->refresh_every == 0) memset(h->disturb, 0, sizeof h->disturb);
    if (h->open == r) return;
    h->open = r;
    h->disturb[r] = 0;
    if (r > 0) h->disturb[r - 1]++;
    if (r + 1 < NR) h->disturb[r + 1]++;
    if (h->disturb[h->weak] >= TH) h->flipped = true;
}
/* victim 의 양쪽(v-1, v+1) 을 n 번씩 번갈아 */
static bool hammer_double(int v, unsigned n, uint32_t refresh_every) {
    hm_t h; hm_init(&h, refresh_every, v);
    for (unsigned i = 0; i < n; i++) { hm_act(&h, v - 1); hm_act(&h, v + 1); }
    return h.flipped;
}
/* 한쪽 aggressor(v-1) 만: 같은 행을 연속으로 읽으면 row hit 라 ACT 가 안 생긴다 → 멀리 있는 dummy 행과 번갈아 */
static bool hammer_single(int v, unsigned n, uint32_t refresh_every) {
    hm_t h; hm_init(&h, refresh_every, v);
    for (unsigned i = 0; i < n; i++) { hm_act(&h, v - 1); hm_act(&h, NR - 1); }
    return h.flipped;
}
static unsigned min_n(bool (*fn)(int, unsigned, uint32_t), uint32_t refresh_every) {
    for (unsigned n = 1; n <= 5000; n++) if (fn(10, n, refresh_every)) return n;
    return 0;                                           /* 5000 까지 안 뒤집힘 */
}

/* ---------------- 연습 3: LFSR 테이블 ---------------- */
static uint32_t galois_next(uint32_t s, uint32_t mask) { uint32_t lsb = s & 1u; s >>= 1; if (lsb) s ^= mask; return s; }
static uint32_t taps_mask(const uint8_t *taps) { uint32_t m = 0; for (; *taps; taps++) m |= 1u << (*taps - 1u); return m; }
/* 1 에서 시작해 다시 1 이 될 때까지의 길이 (상한 limit) */
static uint32_t lfsr_period(uint32_t mask, uint32_t limit) {
    uint32_t s = 1u, n = 0;
    do { s = galois_next(s, mask); n++; } while (s != 1u && n < limit);
    return n;
}
static uint8_t g_seen[1u << 20];
static bool lfsr_covers_all(unsigned nbits, uint32_t mask) {   /* word 0 + LFSR 상태 = 0..2^n-1 각각 정확히 1번 */
    uint32_t n = 1u << nbits, s = 1u;
    memset(g_seen, 0, n);
    g_seen[0] = 1;
    do { if (s >= n || g_seen[s]++) return false; s = galois_next(s, mask); } while (s != 1u);
    for (uint32_t i = 0; i < n; i++) if (g_seen[i] != 1) return false;
    return true;
}

/* ---------------- 연습 4: training 반복 안정성 ---------------- */
typedef struct { int center_min, center_max, worst_width; } stab_t;
static void training_runs(unsigned runs, uint32_t seed, stab_t out[4]) {
    static const int base[4] = { 22, 25, 23, 27 };
    static const int jit[4]  = { 1, 1, 1, 4 };           /* lane3 은 ±4 tap 흔들림 (불안정한 DQS3) */
    uint32_t s = seed;
    for (int ln = 0; ln < 4; ln++) { out[ln].center_min = 1000; out[ln].center_max = -1; out[ln].worst_width = 1000; }
    for (unsigned k = 0; k < runs; k++)
        for (int ln = 0; ln < 4; ln++) {
            int span = 2 * jit[ln] + 1;
            int jl = (int)(xorshift32(&s) % (uint32_t)span) - jit[ln];
            int jr = (int)(xorshift32(&s) % (uint32_t)span) - jit[ln];
            int l = base[ln] + jl, r = base[ln] + 30 + jr, c = (l + r) / 2;
            if (c < out[ln].center_min) out[ln].center_min = c;
            if (c > out[ln].center_max) out[ln].center_max = c;
            if (r - l < out[ln].worst_width) out[ln].worst_width = r - l;
        }
}

/* ---------------- 연습 5: DQ swizzle ---------------- */
/* lane2 (컨트롤러 DQ16-23) → U2(x16 칩) 하위 바이트 DQL0-7. 보드 라우팅 때문에 순서가 섞였다 */
static const uint8_t SWZ_LANE2[8] = { 3, 1, 0, 2, 6, 7, 5, 4 };
static void swizzle_names(uint32_t failbits, char *out, size_t sz) {
    size_t len = 0;
    out[0] = '\0';
    for (unsigned b = 16; b < 24; b++)
        if ((failbits >> b) & 1u)
            len += (size_t)snprintf(out + len, sz - len, "%sDQ%u->U2.DQL%u", len ? " " : "", b, SWZ_LANE2[b - 16u]);
}

int main(void) {
    /* 연습 1 */
    int l, r, bestw = 0, plo = -1, phi = -1;
    for (int v = 0; v < NV; v++) { int w = row_width(v, &l, &r); if (w > bestw) bestw = w; }
    for (int v = 0; v < NV; v++) if (row_width(v, &l, &r) == bestw) { if (plo < 0) plo = v; phi = v; }
    int bestv = (plo + phi) / 2;                          /* 최대 폭 plateau 의 중앙 = Vref 방향 마진 최대 */
    row_width(bestv, &l, &r);
    int c = (l + r) / 2, h = col_height(c), l12, r12, w12 = row_width(12, &l12, &r12);
    printf("[1] 2D eye: max width=%d taps on Vref %d..%d -> pick Vref=%d [%d,%d] center=%d height=%d steps; "
           "default Vref=12 width=%d [%d,%d]\n", bestw, plo, phi, bestv, l, r, c, h, w12, l12, r12);
    CHECK(plo == 15 && phi == 21 && bestv == 18 && bestw == 27 && l == 17 && r == 43 && c == 30 && h == 17);
    CHECK(w12 == 21 && w12 < bestw);

    /* 연습 2 */
    unsigned d_fast = min_n(hammer_double, 100000u), s_fast = min_n(hammer_single, 100000u);
    unsigned d_2x = min_n(hammer_double, 500u), d_1k = min_n(hammer_double, 1000u);
    printf("[2] hammer TH=%u: min n for flip  double=%u single=%u  | refresh/500 acc: double=%u  refresh/1000 acc: double=%u\n",
           TH, d_fast, s_fast, d_2x, d_1k);
    CHECK(d_fast == 300 && s_fast == 600);
    CHECK(hammer_double(10, 400, 100000u) && !hammer_single(10, 400, 100000u));
    CHECK(d_2x == 0 && d_1k == 300);

    /* 연습 3 */
    static const uint8_t T[17][5] = {
        { 4, 3 }, { 5, 3 }, { 6, 5 }, { 7, 6 }, { 8, 6, 5, 4 }, { 9, 5 }, { 10, 7 }, { 11, 9 }, { 12, 6, 4, 1 },
        { 13, 4, 3, 1 }, { 14, 5, 3, 1 }, { 15, 14 }, { 16, 15, 13, 4 }, { 17, 14 }, { 18, 11 }, { 19, 6, 2, 1 }, { 20, 17 } };
    bool all_ok = true;
    for (unsigned i = 0; i < 17; i++) {
        unsigned nb = T[i][0];
        uint32_t m = taps_mask(T[i]), p = lfsr_period(m, 1u << nb);
        bool ok = p == (1u << nb) - 1u && lfsr_covers_all(nb, m);
        all_ok = all_ok && ok;
        if (nb == 4 || nb == 8 || nb == 16 || nb == 20) printf("[3] n=%2u mask=0x%05" PRIX32 " period=%" PRIu32 " covers=%s\n", nb, m, p, ok ? "yes" : "NO");
    }
    static const uint8_t BAD[] = { 4, 2, 0 };                 /* x^4+x^2+1 = (x^2+x+1)^2 : 원시 다항식 아님 */
    uint32_t pbad = lfsr_period(taps_mask(BAD), 16);
    printf("[3] non-primitive x^4+x^2+1: period=%" PRIu32 " covers=%s\n", pbad, lfsr_covers_all(4, taps_mask(BAD)) ? "yes" : "NO");
    CHECK(all_ok);
    CHECK(pbad == 6 && !lfsr_covers_all(4, taps_mask(BAD)));

    /* 연습 4 */
    stab_t st[4];
    training_runs(8, 0xBADC0DEu, st);
    unsigned flagged = 0;
    for (int ln = 0; ln < 4; ln++) {
        int spread = st[ln].center_max - st[ln].center_min;
        bool bad = spread > 3;
        flagged |= (unsigned)bad << ln;
        printf("[4] lane%d centers %d..%d spread=%d worst_width=%d %s\n", ln, st[ln].center_min, st[ln].center_max,
               spread, st[ln].worst_width, bad ? "UNSTABLE" : "ok");
    }
    CHECK(flagged == 0x8u);
    CHECK(st[3].worst_width < st[0].worst_width);

    /* 연습 5 */
    char names[128];
    bool perm = true;
    uint8_t seen[8] = { 0 };
    for (unsigned i = 0; i < 8; i++) if (SWZ_LANE2[i] > 7 || seen[SWZ_LANE2[i]]++) perm = false;
    swizzle_names(0x00220000u, names, sizeof names);
    printf("[5] failbits=0x00220000 -> %s\n", names);
    CHECK(perm);
    CHECK(strcmp(names, "DQ17->U2.DQL1 DQ21->U2.DQL7") == 0);

    printf("\n%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
