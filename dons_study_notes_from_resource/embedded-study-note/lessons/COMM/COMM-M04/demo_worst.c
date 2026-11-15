/* COMM-M04 demo_worst.c — 최악의 스터프 비트 수: Davis/Burns 상한 vs 실제 최댓값
 *
 * 세 가지 방법으로 "DLC 가 n 인 데이터 프레임에서 스터프 비트가 최대 몇 개인가" 를 구한다.
 *   1) 랜덤 탐색: 랜덤 ID · 데이터 프레임 100만 개
 *   2) 손으로 만든 패턴: 모든 ID(표준 2048개 / 확장은 랜덤 2만개) × 0000 1111 주기 패턴의 8가지 위상 (0x0F, 0x78, 0x3C ...)
 *   3) 동적 계획법(DP): 상태 = (CRC 레지스터 15비트, 마지막 와이어 비트, 연속 길이 1..4) — 2^18 상태.
 *      자유 비트(ID, 데이터)마다 0/1 두 갈래, 스터프 수의 최댓값을 상태별로 유지. 끝에서 CRC 15비트를
 *      그 상태의 CRC 로 붙여 스터핑을 마저 센다 → "모든 ID × 모든 데이터" 에 대한 정확한 최댓값.
 *      역추적으로 최악 프레임을 복원해 실제 인코더로 다시 세어 검증한다.
 * 상한(Davis, Burns, Bril, Lukkien 2007): 스터핑 대상 g 비트에서 floor((g-1)/4).
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_worst.c -o demo_worst && ./demo_worst
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- solution.c 축약판: CRC-15, 필드, 스터핑 ---- */
static uint16_t crc15_update(uint16_t crc, unsigned bit) {
    unsigned fb = (bit & 1u) ^ ((crc >> 14) & 1u);
    crc = (uint16_t)((crc << 1) & 0x7FFFu);
    return fb ? (uint16_t)(crc ^ 0x4599u) : crc;
}
static void put_bits(uint8_t *b, int *k, uint32_t v, int w) { for (int i = w - 1; i >= 0; i--) b[(*k)++] = (v >> i) & 1u; }
/* 표준/확장 데이터 프레임의 언스터프 SOF..CRC */
static int build(uint32_t id, bool ext, int dlc, const uint8_t *data, uint8_t *bits) {
    int k = 0, n = dlc > 8 ? 8 : dlc;
    bits[k++] = 0;
    if (!ext) { put_bits(bits, &k, id, 11); bits[k++] = 0; bits[k++] = 0; bits[k++] = 0; }
    else {
        put_bits(bits, &k, id >> 18, 11); bits[k++] = 1; bits[k++] = 1;
        put_bits(bits, &k, id & 0x3FFFF, 18); bits[k++] = 0; bits[k++] = 0; bits[k++] = 0;
    }
    put_bits(bits, &k, (uint32_t)dlc, 4);
    for (int i = 0; i < n; i++) put_bits(bits, &k, data[i], 8);
    uint16_t c = 0;
    for (int i = 0; i < k; i++) c = crc15_update(c, bits[i]);
    put_bits(bits, &k, c, 15);
    return k;
}
static int count_stuff(const uint8_t *b, int n) {
    int run = 0, s = 0, last = 2;
    for (int i = 0; i < n; i++) {
        if (b[i] == last) run++; else { last = b[i]; run = 1; }
        if (run == 5) { last ^= 1; run = 1; s++; }
    }
    return s;
}
static int stuff_of(uint32_t id, bool ext, int dlc, const uint8_t *data) {
    uint8_t bits[128];
    return count_stuff(bits, build(id, ext, dlc, data, bits));
}
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---- DP ---- */
#define NST (1 << 18)            /* crc(15) << 3 | last << 2 | (run-1) */
enum { T0 = 0, T1 = 1, TF = 2 }; /* 템플릿: 고정 0, 고정 1, 자유 */

static int16_t cur[NST], nxt[NST];

/* 템플릿 tp[0..L) (tp[0] = SOF), 반환: 최대 스터프 수, out_bits 에 최악 언스터프 비트열(L+15) */
static int dp_max(const uint8_t *tp, int L, uint8_t *out_bits) {
    uint8_t *bp = malloc((size_t)L * NST);     /* 역추적: 비트 b | prev(last,run) << 1 */
    if (!bp) { perror("malloc"); exit(1); }
    for (int i = 0; i < NST; i++) cur[i] = -1;
    cur[(crc15_update(0, 0) << 3) | 0 << 2 | 0] = 0;       /* SOF: crc 0, last 0, run 1 */
    for (int t = 1; t < L; t++) {
        for (int i = 0; i < NST; i++) nxt[i] = -1;
        for (int st = 0; st < NST; st++) {
            if (cur[st] < 0) continue;
            unsigned crc = (unsigned)st >> 3, last = (st >> 2) & 1u, run = (st & 3u) + 1;
            for (unsigned b = 0; b < 2; b++) {
                if (tp[t] != TF && tp[t] != b) continue;
                unsigned c2 = crc15_update((uint16_t)crc, b), l2, r2;
                int v = cur[st];
                if (b == last) r2 = run + 1; else r2 = 1;
                l2 = b;
                if (r2 == 5) { v++; l2 = !b; r2 = 1; }
                int ns = (int)(c2 << 3 | l2 << 2 | (r2 - 1));
                if (v > nxt[ns]) { nxt[ns] = (int16_t)v; bp[(size_t)t * NST + (size_t)ns] = (uint8_t)(b | (st & 7) << 1); }
            }
        }
        memcpy(cur, nxt, sizeof cur);
    }
    int best = -1, bst = -1;
    for (int st = 0; st < NST; st++) {
        if (cur[st] < 0) continue;
        unsigned crc = (unsigned)st >> 3, last = (st >> 2) & 1u, run = (st & 3u) + 1;
        int v = cur[st];
        for (int i = 14; i >= 0; i--) {             /* CRC 15비트를 이어서 스터핑 (마지막 뒤 포함) */
            unsigned b = (crc >> i) & 1u;
            if (b == last) run++; else { last = b; run = 1; }
            if (run == 5) { v++; last ^= 1u; run = 1; }
        }
        if (v > best) { best = v; bst = st; }
    }
    /* 역추적 */
    int st = bst;
    for (int t = L - 1; t >= 1; t--) {
        uint8_t e = bp[(size_t)t * NST + (size_t)st];
        unsigned b = e & 1u, c2 = (unsigned)st >> 3;
        unsigned fb = c2 & 1u, tt = fb ? (c2 ^ 0x4599u) : c2;
        unsigned crc = (tt >> 1) | ((fb ^ b) << 14);
        out_bits[t] = (uint8_t)b;
        st = (int)(crc << 3 | (e >> 1));
    }
    out_bits[0] = 0;
    free(bp);
    return best;
}

/* DLC 고정 템플릿 */
static int make_tp(bool ext, int dlc, uint8_t *tp) {
    int k = 0, n = dlc > 8 ? 8 : dlc;
    tp[k++] = T0;
    if (!ext) { for (int i = 0; i < 11; i++) tp[k++] = TF; tp[k++] = T0; tp[k++] = T0; tp[k++] = T0; }
    else {
        for (int i = 0; i < 11; i++) tp[k++] = TF;
        tp[k++] = T1; tp[k++] = T1;
        for (int i = 0; i < 18; i++) tp[k++] = TF;
        tp[k++] = T0; tp[k++] = T0; tp[k++] = T0;
    }
    for (int i = 3; i >= 0; i--) tp[k++] = (uint8_t)((dlc >> i) & 1);
    for (int i = 0; i < 8 * n; i++) tp[k++] = TF;
    return k;
}

static int bits_val(const uint8_t *b, int s, int w) { int v = 0; for (int i = 0; i < w; i++) v = v << 1 | b[s + i]; return v; }

int main(void) {
    printf("worst-case stuff bits, classic DATA frames (DLC = n)\n");
    printf("%-4s %2s %5s | %6s %7s %5s | %s\n", "type", "n", "bound", "random", "crafted", "DP", "DP worst frame (re-encoded stuff count)");
    for (int ext = 0; ext < 2; ext++)
        for (int n = 0; n <= 8; n++) {
            int g = (ext ? 54 : 34) + 8 * n, bound = (g - 1) / 4;
            /* 1) 랜덤 */
            uint32_t s = 0x2468ACEu + (uint32_t)n * 7919u + (uint32_t)ext;
            int rnd = 0;
            for (int t = 0; t < 1000000; t++) {
                uint8_t d[8];
                uint32_t id = xs32(&s) & (ext ? 0x1FFFFFFFu : 0x7FFu);
                for (int i = 0; i < 8; i++) d[i] = (uint8_t)xs32(&s);
                int v = stuff_of(id, ext, n, d);
                if (v > rnd) rnd = v;
            }
            /* 2) 0000 1111 주기 패턴 × 8 위상 × ID */
            int cr = 0;
            uint32_t ids = ext ? 20000u : 2048u;
            for (uint32_t j = 0; j < ids; j++) {
                uint32_t id = ext ? (xs32(&s) & 0x1FFFFFFFu) : j;
                for (int ph = 0; ph < 8; ph++) {
                    uint8_t d[8];
                    for (int i = 0; i < 8; i++) {
                        uint32_t v = 0;
                        for (int k = 0; k < 8; k++) v = v << 1 | (((unsigned)(8 * i + k + ph) / 4u) & 1u);
                        d[i] = (uint8_t)v;
                    }
                    int v = stuff_of(id, ext, n, d);
                    if (v > cr) cr = v;
                }
            }
            /* 3) DP */
            uint8_t tp[128], wb[160];
            int L = make_tp(ext, n, tp);
            int dp = dp_max(tp, L, wb);
            uint32_t id = ext ? ((uint32_t)bits_val(wb, 1, 11) << 18 | (uint32_t)bits_val(wb, 14, 18)) : (uint32_t)bits_val(wb, 1, 11);
            int hb = ext ? 39 : 19;
            uint8_t d[8] = { 0 };
            for (int i = 0; i < n; i++) d[i] = (uint8_t)bits_val(wb, hb + 8 * i, 8);
            int re = stuff_of(id, ext, n, d);
            printf("%-4s %2d %5d | %6d %7d %5d | ID 0x%0*X data", ext ? "ext" : "std", n, bound, rnd, cr, dp,
                   ext ? 8 : 3, (unsigned)id);
            for (int i = 0; i < n; i++) printf(" %02X", d[i]);
            printf(" -> %d%s\n", re, re == dp ? "" : "  MISMATCH");
        }
    /* DLC 9..15 도 8 바이트: DLC 비트까지 자유롭게 두면? */
    for (int ext = 0; ext < 2; ext++) {
        int best = 0, bd = 0;
        for (int dlc = 8; dlc <= 15; dlc++) {
            uint8_t tp[128], wb[160];
            int v = dp_max(tp, make_tp(ext, dlc, tp), wb);
            if (v > best) { best = v; bd = dlc; }
        }
        printf("%s 8 data bytes, DLC 8..15 free: DP max %d (at DLC %d), bound %d\n", ext ? "ext" : "std", best, bd,
               ((ext ? 54 : 34) + 64 - 1) / 4);
    }
    return 0;
}
