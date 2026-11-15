/* COMM-B12 실험: COBS vs SLIP 오버헤드 표 + 한 바이트 손상 때 COBS 구조만으로 얼마나 잡히나
 *
 * 1) 프레임 오버헤드 = (선로에 나가는 바이트 수) − n
 *    COBS: cobs_encode(n) + 구분자 0x00 1개
 *    SLIP: 앞 END + 이스케이프된 데이터 + 뒤 END (RFC 1055 권장 형태, COMM-B11 축약판)
 * 2) 32 바이트 payload + CRC-16 을 COBS 로 싸서 인코딩된 바이트 하나를 바꿨을 때
 *    디코더 구조 오류(TRUNC/ZERO)로 잡힘 / CRC 로 잡힘 / 못 잡힘 비율, 그리고 틀린 디코딩 바이트 수
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_overhead.c -o demo_overhead && ./demo_overhead
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { COBS_ERR_OVERFLOW = -1, COBS_ERR_ZERO = -2, COBS_ERR_TRUNC = -3, COBS_ERR_EMPTY = -4 };

/* solution.c 와 같은 구현 */
static int cobs_encode(const uint8_t *src, size_t n, uint8_t *dst, size_t cap) {
    if (cap < 1) return COBS_ERR_OVERFLOW;
    size_t code_at = 0, o = 1;
    uint8_t code = 1;
    for (size_t i = 0; i < n; i++) {
        if (src[i] != 0) {
            if (o >= cap) return COBS_ERR_OVERFLOW;
            dst[o++] = src[i];
            if (++code != 0xFF) continue;
            if (i + 1 == n) { dst[code_at] = 0xFF; return (int)o; }
        }
        dst[code_at] = code;
        if (o >= cap) return COBS_ERR_OVERFLOW;
        code_at = o++;
        code = 1;
    }
    dst[code_at] = code;
    return (int)o;
}

static int cobs_decode(const uint8_t *src, size_t n, uint8_t *dst, size_t cap) {
    if (n == 0) return COBS_ERR_EMPTY;
    size_t i = 0, o = 0;
    while (i < n) {
        uint8_t code = src[i++];
        if (code == 0) return COBS_ERR_ZERO;
        size_t len = (size_t)code - 1;
        if (len > n - i) return COBS_ERR_TRUNC;
        if (len > cap - o) return COBS_ERR_OVERFLOW;
        for (size_t k = 0; k < len; k++) {
            if (src[i] == 0) return COBS_ERR_ZERO;
            dst[o++] = src[i++];
        }
        if (code != 0xFF && i < n) {
            if (o >= cap) return COBS_ERR_OVERFLOW;
            dst[o++] = 0;
        }
    }
    return (int)o;
}

/* SLIP (COMM-B11 축약판): END C0, ESC DB, ESC_END DC, ESC_ESC DD. 앞뒤 END. 길이만 센다 */
static size_t slip_framed_len(const uint8_t *p, size_t n) {
    size_t len = 2 + n;
    for (size_t i = 0; i < n; i++) if (p[i] == 0xC0 || p[i] == 0xDB) len++;
    return len;
}

static uint16_t crc16_ccitt_false(const uint8_t *p, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

enum { D_UNIFORM, D_ASCII, D_ZERO_HEAVY, D_ALL00, D_ALLC0, D_NOZERO, D_N };
static const char *DNAME[D_N] = {"uniform random", "ASCII 0x20-0x7E", "50% zeros", "all 0x00", "all 0xC0", "no zero (1..255)"};

static void fill(uint8_t *p, size_t n, int d, uint32_t *seed) {
    for (size_t i = 0; i < n; i++) {
        uint32_t r = xs32(seed);
        switch (d) {
        case D_UNIFORM: p[i] = (uint8_t)r; break;
        case D_ASCII: p[i] = (uint8_t)(0x20 + r % 95u); break;
        case D_ZERO_HEAVY: p[i] = (r & 0x100) ? 0 : (uint8_t)(1 + r % 255u); break;
        case D_ALL00: p[i] = 0x00; break;
        case D_ALLC0: p[i] = 0xC0; break;
        default: p[i] = (uint8_t)(1 + r % 255u); break;
        }
    }
}

int main(void) {
    static uint8_t a[1600], e[1700];
    uint32_t seed = 0x5EEDu;
    static const size_t NS[] = {16, 64, 253, 254, 255, 508, 1000, 1500};

    printf("== 1) framed overhead in bytes (wire bytes - n). random rows: mean / max over trials ==\n");
    printf("%-18s %6s | %-15s | %-15s\n", "data", "n", "COBS (+00)", "SLIP (END..END)");
    for (int d = 0; d < D_N; d++) {
        for (size_t k = 0; k < sizeof NS / sizeof NS[0]; k++) {
            size_t n = NS[k];
            bool rnd = (d == D_UNIFORM || d == D_ASCII || d == D_ZERO_HEAVY || d == D_NOZERO);
            int trials = rnd ? (n <= 255 ? 100000 : 20000) : 1;
            double sc = 0, ss = 0; size_t mc = 0, ms = 0;
            for (int t = 0; t < trials; t++) {
                fill(a, n, d, &seed);
                size_t c = (size_t)cobs_encode(a, n, e, sizeof e) + 1 - n;
                size_t s = slip_framed_len(a, n) - n;
                sc += (double)c; ss += (double)s;
                if (c > mc) mc = c;
                if (s > ms) ms = s;
            }
            if (rnd) printf("%-18s %6zu | %6.3f / %-5zu | %7.3f / %-5zu\n", DNAME[d], n, sc / trials, mc, ss / trials, ms);
            else     printf("%-18s %6zu | %-15zu | %-15zu\n", DNAME[d], n, mc, ms);
        }
    }
    printf("worst case formula: COBS n + max(1, ceil(n/254)) + 1, SLIP 2n + 2\n");
    for (size_t k = 0; k < sizeof NS / sizeof NS[0]; k++) {
        size_t n = NS[k];
        printf("  n=%-5zu COBS worst %-5zu (+%zu)   SLIP worst %-5zu (+%zu)\n",
               n, n + (n + 253) / 254 + 1, (n + 253) / 254 + 1, 2 * n + 2, n + 2);
    }

    printf("\n== 2) one corrupted byte in COBS(payload32 + CRC16): who catches it? ==\n");
    const int T = 2000000;
    long by_struct = 0, by_crc = 0, missed = 0, to_zero = 0, code_hits = 0, code_struct = 0;
    long len_changed = 0; size_t max_wrong = 0; double sum_wrong = 0; long n_wrong = 0;
    for (int t = 0; t < T; t++) {
        uint8_t pl[34], dec[64];
        fill(pl, 32, (t & 1) ? D_UNIFORM : D_ZERO_HEAVY, &seed);
        uint16_t c = crc16_ccitt_false(pl, 32);
        pl[32] = (uint8_t)(c >> 8); pl[33] = (uint8_t)c;
        int en = cobs_encode(pl, 34, e, sizeof e);
        size_t k = xs32(&seed) % (size_t)en;
        /* 어느 바이트가 코드 바이트였나 (원래 인코딩 기준) */
        bool is_code = false;
        for (size_t i = 0; i < (size_t)en; i += e[i]) { if (i == k) { is_code = true; break; } }
        uint8_t nv = (uint8_t)(e[k] ^ (uint8_t)(1 + xs32(&seed) % 255u));
        e[k] = nv;
        if (nv == 0) { to_zero++; continue; }          /* 0 이 되면 프레임이 둘로 쪼개진다 — 아래 따로 */
        if (is_code) code_hits++;
        int d = cobs_decode(e, (size_t)en, dec, sizeof dec);
        if (d < 0) { by_struct++; if (is_code) code_struct++; continue; }
        size_t wrong = 0;
        if (d != 34) len_changed++;
        for (int i = 0; i < 34; i++) if (i >= d || dec[i] != pl[i]) wrong++;
        sum_wrong += (double)wrong; n_wrong++;
        if (wrong > max_wrong) max_wrong = wrong;
        if (d >= 2 && crc16_ccitt_false(dec, (size_t)d - 2) == (uint16_t)(dec[d - 2] << 8 | dec[d - 1])) missed++;
        else by_crc++;
    }
    long nz = T - to_zero;
    printf("trials %d (byte -> nonzero %ld, byte -> 0x00 %ld)\n", T, nz, to_zero);
    printf("  caught by COBS structure (TRUNC/ZERO): %ld (%.2f%%)\n", by_struct, 100.0 * by_struct / nz);
    printf("    of which code-byte hits: %ld of %ld code-byte corruptions (%.2f%%)\n", code_struct, code_hits, 100.0 * code_struct / code_hits);
    printf("  decoded OK structurally, caught by CRC-16: %ld (%.2f%%)\n", by_crc, 100.0 * by_crc / nz);
    printf("  undetected: %ld\n", missed);
    printf("  structurally-OK decodes: length changed %ld, wrong bytes mean %.2f max %zu (of 34)\n",
           len_changed, sum_wrong / (double)n_wrong, max_wrong);
    return 0;
}
