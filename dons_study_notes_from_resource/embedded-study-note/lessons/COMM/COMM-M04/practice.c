/* COMM-M04 추가 연습 — 한 번에 검증
 *
 * 연습 1: ID 0x000 DLC 0 표준 프레임의 스터프 비트 위치 (손으로 → 코드로)
 * 연습 2: 같은 base ID 의 표준 vs 확장 프레임 — 언스터프 비트가 처음 달라지는 위치 (중재 = COMM-M05)
 * 연습 3: 8 바이트 프레임의 최악 전송 시간과 초당 프레임 수 (상한 vs DP 정확값)
 * 연습 4: 2비트만 다른 두 와이어 비트열이 둘 다 "유효한 다른 프레임" — 스터핑이 CRC 보장을 깨는 예 (demo_flip.c)
 * 연습 5: SOF 를 CRC 에 넣든 빼든 같은 값 (init = 0), 스터핑된 비트로 계산하면 다른 값
 *
 * 인코더는 solution.c 축약판, 디코더는 "한 번에 배열을 훑는" 다른 구현 (solution 의 상태 기계와 교차 검증).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint32_t id; bool ide, rtr; uint8_t dlc; uint8_t data[8]; } can_frame_t;

static int dlen(bool rtr, unsigned dlc) { return rtr ? 0 : (dlc > 8 ? 8 : (int)dlc); }
static uint16_t crc15_update(uint16_t crc, unsigned bit) {
    unsigned fb = (bit & 1u) ^ ((crc >> 14) & 1u);
    crc = (uint16_t)((crc << 1) & 0x7FFFu);
    return fb ? (uint16_t)(crc ^ 0x4599u) : crc;
}
static uint16_t crc15_bits(const uint8_t *b, int n) { uint16_t c = 0; for (int i = 0; i < n; i++) c = crc15_update(c, b[i]); return c; }
static void put_bits(uint8_t *b, int *k, uint32_t v, int w) { for (int i = w - 1; i >= 0; i--) b[(*k)++] = (uint8_t)((v >> i) & 1u); }
static uint32_t get_bits(const uint8_t *b, int s, int w) { uint32_t v = 0; for (int i = 0; i < w; i++) v = v << 1 | b[s + i]; return v; }

/* 언스터프 SOF..데이터 끝 (CRC 앞) 길이 = 반환값, crc 는 붙이지 않는다 */
static int build_head(const can_frame_t *f, uint8_t *b) {
    int k = 0;
    b[k++] = 0;
    if (!f->ide) { put_bits(b, &k, f->id, 11); b[k++] = f->rtr; b[k++] = 0; b[k++] = 0; }
    else {
        put_bits(b, &k, f->id >> 18, 11); b[k++] = 1; b[k++] = 1;
        put_bits(b, &k, f->id & 0x3FFFFu, 18); b[k++] = f->rtr; b[k++] = 0; b[k++] = 0;
    }
    put_bits(b, &k, f->dlc, 4);
    for (int i = 0; i < dlen(f->rtr, f->dlc); i++) put_bits(b, &k, f->data[i], 8);
    return k;
}
static int stuff(const uint8_t *in, int n, uint8_t *out, int *nst, int *spos) {
    int o = 0, run = 0, s = 0, last = 2;
    for (int i = 0; i < n; i++) {
        out[o++] = in[i];
        if (in[i] == last) run++; else { last = in[i]; run = 1; }
        if (run == 5) { last ^= 1; if (spos) spos[s] = o; out[o++] = (uint8_t)last; run = 1; s++; }
    }
    *nst = s;
    return o;
}
static int encode(const can_frame_t *f, bool acked, uint8_t *out, int *nst, int *spos) {
    uint8_t raw[128];
    int k = build_head(f, raw);
    put_bits(raw, &k, crc15_bits(raw, k), 15);
    int w = stuff(raw, k, out, nst, spos);
    out[w++] = 1; out[w++] = acked ? 0 : 1; out[w++] = 1;
    for (int i = 0; i < 7; i++) out[w++] = 1;
    return w;
}
/* 배열 한 번 훑기 디코더: 0 = OK, -1 stuff, -2 crc, -3 form, -4 짧음 */
static int decode_simple(const uint8_t *w, int n, can_frame_t *f) {
    uint8_t ub[128];
    int nu = 0, need = 118, run = 0, last = 2, i = 0;
    while (i < n && w[i] == 1) i++;                          /* idle 건너뛰기 */
    for (; i < n; i++) {
        if (run == 5) {                                      /* 스터프 자리 */
            if (w[i] == last) return -1;
            last = w[i]; run = 1;
            if (nu == need) { i++; break; }
            continue;
        }
        if (w[i] == last) run++; else { last = w[i]; run = 1; }
        ub[nu++] = w[i];
        if (nu == 19 && ub[13] == 0) need = 34 + 8 * dlen(ub[12], get_bits(ub, 15, 4));
        if (nu == 39 && ub[13] == 1) need = 54 + 8 * dlen(ub[32], get_bits(ub, 35, 4));
        if (nu == need && run != 5) { i++; break; }
    }
    if (nu != need) return -4;
    if (crc15_bits(ub, need - 15) != get_bits(ub, need - 15, 15)) return -2;
    if (i + 10 > n) return -4;
    if (w[i] != 1 || w[i + 2] != 1) return -3;               /* CRC delim, ACK delim */
    for (int e = 0; e < 6; e++) if (w[i + 3 + e] != 1) return -3;
    memset(f, 0, sizeof *f);
    f->ide = ub[13];
    int d = f->ide ? 39 : 19;
    f->id = f->ide ? (get_bits(ub, 1, 11) << 18 | get_bits(ub, 14, 18)) : get_bits(ub, 1, 11);
    f->rtr = ub[f->ide ? 32 : 12];
    f->dlc = (uint8_t)get_bits(ub, d - 4, 4);
    for (int k = 0; k < dlen(f->rtr, f->dlc); k++) f->data[k] = (uint8_t)get_bits(ub, d + 8 * k, 8);
    return 0;
}
static int from_str(const char *s, uint8_t *b) { int n = 0; for (; s[n]; n++) b[n] = (uint8_t)(s[n] - '0'); return n; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    uint8_t w[200], raw[128];
    int ns = 0, spos[40];

    /* 연습 1 */
    {
        can_frame_t f = { 0x000, false, false, 0, { 0 } };
        int n = encode(&f, false, w, &ns, spos);
        printf("ex1 ID 0x000 DLC0: %d wire bits, %d stuff at", n, ns);
        for (int i = 0; i < ns; i++) printf(" %d", spos[i]);
        printf("\n");
        static const int EXP[] = { 5, 11, 17, 23, 29, 35 }; /* 언스터프 34비트가 전부 0 (CRC(0..0) = 0) → 6비트마다 하나 */
        CHECK(ns == 6 && n == 34 + 6 + 10);
        CHECK(memcmp(spos, EXP, sizeof EXP) == 0);
    }
    /* 연습 2 */
    {
        can_frame_t s = { 0x123, false, false, 1, { 0xAA } }, sr = { 0x123, false, true, 1, { 0 } };
        can_frame_t e = { 0x123u << 18 | 0x00001u, true, false, 1, { 0xAA } };
        uint8_t a[128], b[128], c[128];
        build_head(&s, a); build_head(&e, b); build_head(&sr, c);
        int d1 = 0, d2 = 0;
        while (a[d1] == b[d1]) d1++;
        while (c[d2] == b[d2]) d2++;
        printf("ex2 std data vs ext: first diff bit %d (std %d / ext %d); std remote vs ext: bit %d (std %d / ext %d)\n",
               d1, a[d1], b[d1], d2, c[d2], b[d2]);
        CHECK(d1 == 12 && a[12] == 0 && b[12] == 1);     /* RTR(dom) vs SRR(rec) → 표준 데이터 승 */
        CHECK(d2 == 13 && c[13] == 0 && b[13] == 1);     /* RTR(rec)=SRR(rec), IDE dom vs rec → 표준 리모트 승 */
        int ns1 = 0, ns2 = 0, o1 = stuff(a, 12, w, &ns1, NULL);
        uint8_t w2[64];
        int o2 = stuff(b, 12, w2, &ns2, NULL);
        CHECK(o1 == o2 && memcmp(w, w2, (size_t)o1) == 0);  /* 같은 접두사 → 같은 스터핑 → 와이어도 같다 */
    }
    /* 연습 3 */
    {
        int bound = 8 * 8 + 47 + (34 + 64 - 1) / 4;      /* 135 */
        int exact = 44 + 64 + 22 + 3;                     /* DP 최댓값 22 (demo_worst) → 133 */
        int nostuff = 44 + 64 + 3;                        /* 111 */
        double us500 = bound * 2.0, fps500 = 500000.0 / bound, fps1m = 1000000.0 / bound;
        printf("ex3 std 8B: bound %d bits = %.0f us @500k (%.1f frames/s), exact %d bits = %.0f us, no-stuff %d bits = %.0f us; @1M %.1f frames/s\n",
               bound, us500, fps500, exact, exact * 2.0, nostuff, nostuff * 2.0, fps1m);
        CHECK(bound == 135 && exact == 133 && nostuff == 111);
        CHECK((int)(fps500 * 10) == 37037 && (int)fps1m == 7407);
        int ext_bound = 8 * 8 + 67 + (54 + 64 - 1) / 4;
        CHECK(ext_bound == 160);
    }
    /* 연습 4: demo_flip.c 가 찾은 미검출 2비트 오류 (와이어 45, 83) */
    {
        static const char *SENT =
            "00100011010011011010101000111100000101111000001101110010000010011000111100100111110100010001101100000100100110010001011111111";
        static const char *RECV =
            "00100011010011011010101000111100000101111000011101110010000010011000111100100111110000010001101100000100100110010001011111111";
        uint8_t a[200], b[200];
        int na = from_str(SENT, a), nb = from_str(RECV, b), ham = 0, first = -1, second = -1;
        for (int i = 0; i < na; i++) if (a[i] != b[i]) { ham++; if (first < 0) first = i; else second = i; }
        can_frame_t fa, fb;
        int ra = decode_simple(a, na, &fa), rb = decode_simple(b, nb, &fb);
        printf("ex4 hamming %d (@%d,@%d) sent=%d recv=%d, data[0..2] %02X %02X %02X -> %02X %02X %02X\n",
               ham, first, second, ra, rb, fa.data[0], fa.data[1], fa.data[2], fb.data[0], fb.data[1], fb.data[2]);
        CHECK(na == nb && ham == 2 && first == 45 && second == 83);
        CHECK(ra == 0 && rb == 0);                       /* 둘 다 유효한 프레임 */
        CHECK(fa.id == fb.id && fa.dlc == 7 && memcmp(fa.data, fb.data, 7) != 0);
        CHECK(fa.data[1] == 0xE4 && fb.data[1] == 0x72);  /* 사이 구간이 1비트 밀림: E4 >> 1 = 72 */
    }
    /* 연습 5 */
    {
        uint32_t s = 0xBEEFu;
        int same_sof = 0, diff_stuffed = 0, with_stuff = 0;
        for (int t = 0; t < 2000; t++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            can_frame_t f = { s & 0x7FFu, false, false, (uint8_t)(s >> 11 & 7u) + 1, { 0 } };
            for (int i = 0; i < 8; i++) f.data[i] = (uint8_t)(s >> (i * 3));
            int k = build_head(&f, raw);
            if (crc15_bits(raw, k) == crc15_bits(raw + 1, k - 1)) same_sof++;
            int nst = 0, o = stuff(raw, k, w, &nst, NULL);
            if (nst > 0) { with_stuff++; if (crc15_bits(w, o) != crc15_bits(raw, k)) diff_stuffed++; }
        }
        printf("ex5 SOF in/out same CRC: %d/2000; CRC over stuffed bits differs: %d/%d frames with stuff\n",
               same_sof, diff_stuffed, with_stuff);
        CHECK(same_sof == 2000);
        CHECK(with_stuff > 0 && diff_stuffed == with_stuff);
    }
    /* 교차 검증: 간단 디코더도 왕복을 통과 */
    {
        uint32_t s = 0x1357u;
        int bad = 0;
        for (int t = 0; t < 5000; t++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            can_frame_t f = { 0, (s & 1u) != 0, (s & 6u) == 0, (uint8_t)(s >> 3 & 15u), { 0 } };
            f.id = (s * 2654435761u) & (f.ide ? 0x1FFFFFFFu : 0x7FFu);
            for (int i = 0; i < 8; i++) f.data[i] = (uint8_t)((s >> i) * 31u);
            int n = encode(&f, true, w, &ns, NULL);
            can_frame_t g;
            if (decode_simple(w, n, &g) != 0 || g.id != f.id || g.ide != f.ide || g.rtr != f.rtr || g.dlc != f.dlc
                || memcmp(g.data, f.data, (size_t)dlen(f.rtr, f.dlc)) != 0) bad++;
        }
        CHECK(bad == 0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
