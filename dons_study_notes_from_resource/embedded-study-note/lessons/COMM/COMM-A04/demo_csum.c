/* COMM-A04 demo — 인터넷 체크섬 실험 (긴 실행, 테스트 아님)
 *
 * 표 1: 64바이트 메시지에서 오류 종류별 미검출률 — 인터넷 체크섬(RFC 1071) vs CRC-32 (Ethernet FCS 다항식)
 * 표 2: 증분 갱신 RFC 1141(식 2) vs RFC 1624(식 3) — 다른 필드 합 S = 0xCD7A 에서 (m, m') 2^32 쌍 전수 + 랜덤 S
 * 표 3: 바이트 쌍 루프 vs 32비트 워드 루프 속도 비율 (호스트 부하가 있으므로 비율만)
 *
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_csum.c -o demo_csum && ./demo_csum
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint16_t rd16be(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32le(const uint8_t *p) { return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]; }
static uint32_t fold(uint32_t s) { while (s >> 16) s = (s & 0xFFFFu) + (s >> 16); return s; }
static uint32_t csum_add(uint32_t sum, const uint8_t *p, size_t n) {
    size_t i;
    for (i = 0; i + 1 < n; i += 2) sum += rd16be(p + i);
    if (n & 1u) sum += (uint32_t)p[n - 1] << 8;
    return fold(sum);
}
static uint16_t inet(const uint8_t *p, size_t n) { return (uint16_t)~csum_add(0, p, n); }
static uint16_t fast32(const uint8_t *p, size_t n) {
    uint64_t s = 0; size_t i = 0;
    for (; i + 4 <= n; i += 4) s += rd32le(p + i);
    uint8_t t[4] = { 0 }; if (i < n) { memcpy(t, p + i, n - i); s += rd32le(t); }
    while (s >> 16) s = (s & 0xFFFFu) + (s >> 16);
    return (uint16_t)~(uint16_t)((s >> 8) | ((s & 0xFFu) << 8));
}
static uint32_t crc_tab[256];
static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1; crc_tab[i] = c; }
}
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static uint32_t g_rng = 0x1071u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

#define MSG 64
enum { E_BITS, E_BURST, E_SWAP, E_ZERO_FFFF };

/* 오류 하나 주입. 반환 false = 이 시도는 "오류 없음"(예: 같은 값 워드 교환) → 세지 않음 */
static bool inject(uint8_t *m, int kind, int k) {
    if (kind == E_BITS) {                                 /* 서로 다른 k 비트 */
        int pos[8]; int n = 0;
        while (n < k) { int b = (int)(xs32() % (MSG * 8)); bool dup = false; for (int j = 0; j < n; j++) if (pos[j] == b) dup = true; if (!dup) pos[n++] = b; }
        for (int j = 0; j < n; j++) m[pos[j] / 8] ^= (uint8_t)(1u << (pos[j] % 8));
        return true;
    }
    if (kind == E_BURST) {                                /* 길이 k: 첫·끝 비트는 반드시, 사이는 랜덤 */
        int start = (int)(xs32() % (MSG * 8 - k + 1));
        for (int j = 0; j < k; j++) if (j == 0 || j == k - 1 || (xs32() & 1u)) m[(start + j) / 8] ^= (uint8_t)(0x80u >> ((start + j) % 8));
        return true;
    }
    if (kind == E_SWAP) {                                 /* 정렬된 16비트 워드 두 개 교환 */
        int a = (int)(xs32() % (MSG / 2)), b = (int)(xs32() % (MSG / 2));
        if (a == b || rd16be(m + 2 * a) == rd16be(m + 2 * b)) return false;
        uint8_t t0 = m[2 * a], t1 = m[2 * a + 1];
        m[2 * a] = m[2 * b]; m[2 * a + 1] = m[2 * b + 1]; m[2 * b] = t0; m[2 * b + 1] = t1;
        return true;
    }
    return false;                                         /* E_ZERO_FFFF 는 main 에서 직접 (0x0000 워드 → 0xFFFF) */
}

int main(void) {
    crc_init();
    {
        const uint8_t chk[] = "123456789";
        printf("CRC-32/ISO-HDLC check(\"123456789\") = 0x%08X (expect 0xCBF43926)\n", (unsigned)crc32(chk, 9));
    }
    /* ---- 표 1 ---- */
    {
        const int N = 1000000;
        struct { int kind, k; const char *name; } rows[] = {
            { E_BITS, 1, "1 bit" }, { E_BITS, 2, "2 bits" }, { E_BITS, 3, "3 bits" }, { E_BITS, 4, "4 bits" },
            { E_BURST, 8, "burst 8" }, { E_BURST, 16, "burst 16" }, { E_BURST, 17, "burst 17" }, { E_BURST, 24, "burst 24" },
            { E_BURST, 32, "burst 32" }, { E_BURST, 33, "burst 33" }, { E_BURST, 64, "burst 64" },
            { E_SWAP, 0, "word swap" }, { E_ZERO_FFFF, 0, "0000->FFFF" } };
        printf("\n표 1: 64-byte message, %d trials per row (fixed seed)\n", N);
        printf("%-12s %12s %14s %12s %14s\n", "error", "inet undet", "inet rate", "crc32 undet", "crc32 rate");
        uint8_t m[MSG], e[MSG];
        for (size_t r = 0; r < sizeof rows / sizeof rows[0]; r++) {
            long ui = 0, uc = 0, trials = 0;
            for (int t = 0; t < N; t++) {
                for (int i = 0; i < MSG; i++) m[i] = (uint8_t)xs32();
                if (rows[r].kind == E_ZERO_FFFF) { int a = (int)(xs32() % (MSG / 2)); m[2 * a] = 0; m[2 * a + 1] = 0; }
                uint16_t ci = inet(m, MSG); uint32_t cc = crc32(m, MSG);
                memcpy(e, m, MSG);
                if (rows[r].kind == E_ZERO_FFFF) {
                    int a = -1; for (int w = 0; w < MSG / 2; w++) if (rd16be(m + 2 * w) == 0) { a = w; break; }
                    if (a < 0) continue;
                    e[2 * a] = 0xFF; e[2 * a + 1] = 0xFF;
                } else if (!inject(e, rows[r].kind, rows[r].k)) continue;
                if (memcmp(e, m, MSG) == 0) continue;
                trials++;
                if (inet(e, MSG) == ci) ui++;
                if (crc32(e, MSG) == cc) uc++;
            }
            printf("%-12s %12ld %14.3e %12ld %14.3e\n", rows[r].name, ui, (double)ui / trials, uc, (double)uc / trials);
        }
        printf("(2^-16 = %.3e, 2^-32 = %.3e)\n", 1.0 / 65536, 1.0 / 4294967296.0);
    }
    /* ---- 표 2 ---- */
    {
        clock_t t0 = clock();
        const uint32_t S = 0xCD7A;
        uint64_t m3 = 0, m2 = 0, m2_zero = 0, m2_v = 0;
        for (uint32_t m = 0; m <= 0xFFFF; m++) {
            uint16_t hc = (uint16_t)~fold(S + m);
            for (uint32_t mp = 0; mp <= 0xFFFF; mp++) {
                uint16_t full = (uint16_t)~fold(S + mp);
                uint16_t e3 = (uint16_t)~fold((uint16_t)~hc + (uint16_t)~m + mp);
                uint16_t e2 = (uint16_t)fold((uint32_t)hc + m + (uint16_t)~mp);
                if (e3 != full) m3++;
                if (e2 != full) {
                    m2++;
                    if (full == 0x0000 && e2 == 0xFFFF) m2_zero++;
                    if (fold(S + mp + e2) == 0xFFFF) m2_v++;   /* 받는 쪽 검증은 통과하나? */
                }
            }
        }
        printf("\n표 2: S=0xCD7A, all 2^32 (m, m') pairs: eqn3 mismatches %llu; eqn2 mismatches %llu "
               "(of which full=0x0000 & eqn2=0xFFFF: %llu; still verify: %llu)  [%.1f s]\n",
               (unsigned long long)m3, (unsigned long long)m2, (unsigned long long)m2_zero, (unsigned long long)m2_v,
               (double)(clock() - t0) / CLOCKS_PER_SEC);
        uint64_t r3 = 0, r2 = 0, n = 100000000ull;
        for (uint64_t i = 0; i < n; i++) {
            uint32_t s = xs32() & 0xFFFFu; if (s == 0) s = 1;          /* 다른 필드 합 (0 아닌 접힌 값) */
            uint32_t m = xs32() & 0xFFFFu, mp = xs32() & 0xFFFFu;
            if ((i & 1023u) == 0) mp = (uint16_t)~s;                     /* 가끔 결과가 0 이 되는 경우를 강제 */
            uint16_t hc = (uint16_t)~fold(s + m), full = (uint16_t)~fold(s + mp);
            if ((uint16_t)~fold((uint16_t)~hc + (uint16_t)~m + mp) != full) r3++;
            if ((uint16_t)fold((uint32_t)hc + m + (uint16_t)~mp) != full) r2++;
        }
        printf("     random S (1e8 triples, every 1024th forced to full=0): eqn3 mismatches %llu, eqn2 mismatches %llu\n",
               (unsigned long long)r3, (unsigned long long)r2);
    }
    /* ---- 표 3 ---- */
    {
        static uint8_t buf[1500]; for (int i = 0; i < 1500; i++) buf[i] = (uint8_t)xs32();
        volatile uint16_t sink = 0; const int R = 200000;
        clock_t t0 = clock();
        for (int r = 0; r < R; r++) { buf[r % 1500] ^= 1; sink ^= inet(buf, 1500); }
        double a = (double)(clock() - t0);
        t0 = clock();
        for (int r = 0; r < R; r++) { buf[r % 1500] ^= 1; sink ^= fast32(buf, 1500); }
        double b = (double)(clock() - t0);
        printf("\n표 3: 1500-byte checksum x %d: bytewise / fast32 time ratio = %.2f (same result: %s)\n", R, a / b,
               inet(buf, 1500) == fast32(buf, 1500) ? "yes" : "no");
        (void)sink;
    }
    return 0;
}
