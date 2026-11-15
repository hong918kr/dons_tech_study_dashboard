/* COMM-B12 추가 연습 — 한 번에 검증
 *
 * 연습 1: cobs_encoded_len (쓰지 않고 정확한 길이) + 컴파일 시간 버퍼 크기 매크로 COBS_MAX_ENC(n)
 * 연습 2: 엄격(canonical) 디코더 — "FF 블록 바로 뒤 마지막 01" 을 거부 → 받아들인 것은 다시 인코딩하면 똑같다
 * 연습 3: 제자리(in-place) 디코딩 — RX 버퍼 하나로 끝내기
 * 연습 4: 구분자를 0x00 이 아닌 값으로 (인코딩 결과를 XOR s)
 * 연습 5: 손상 실험 — 데이터 바이트 손상은 COBS 구조가 절대 못 본다 (정확히 1 바이트만 바뀜), 코드 바이트는?
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { COBS_ERR_OVERFLOW = -1, COBS_ERR_ZERO = -2, COBS_ERR_TRUNC = -3, COBS_ERR_EMPTY = -4, COBS_ERR_NONCANON = -5 };

/* ---- 본문 Part A 구현 (solution.c 와 같음) ---- */
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

/* ---- 연습 1: 정확한 길이 / 최악 길이 매크로 ---- */
#define COBS_MAX_ENC(n) ((n) + ((n) == 0 ? 1 : ((n) + 253) / 254))

static size_t cobs_encoded_len(const uint8_t *src, size_t n) {
    size_t len = 1, run = 0;                     /* 첫 코드 바이트 */
    for (size_t i = 0; i < n; i++) {
        if (src[i] == 0) { len++; run = 0; continue; }      /* 0 → 다음 블록의 코드 바이트 */
        len++;
        if (++run == 254 && i + 1 < n) { len++; run = 0; }  /* 꽉 찬 블록 뒤 데이터가 더 있으면 새 코드 */
    }
    return len;
}

/* ---- 연습 2: 엄격 디코더 ---- */
static int cobs_decode_strict(const uint8_t *src, size_t n, uint8_t *dst, size_t cap) {
    /* 정규형이 아닌 유일한 모양: 마지막 블록이 01 이고 바로 앞 블록이 FF. 마지막 두 코드 위치를 기억한다 */
    size_t i = 0, prev = SIZE_MAX, last = SIZE_MAX;
    while (i < n && src[i] != 0) { prev = last; last = i; i += src[i]; }
    if (i == n && last != SIZE_MAX && src[last] == 0x01 && prev != SIZE_MAX && src[prev] == 0xFF)
        return COBS_ERR_NONCANON;
    return cobs_decode(src, n, dst, cap);       /* 나머지 검사(0, 끝 너머)는 원래 디코더에 맡긴다 */
}

/* ---- 연습 4: 구분자 s ---- */
static int cobs_encode_sentinel(const uint8_t *src, size_t n, uint8_t *dst, size_t cap, uint8_t s) {
    int e = cobs_encode(src, n, dst, cap);
    for (int i = 0; i < e; i++) dst[i] ^= s;     /* 인코딩에 0 이 없으니 XOR s 후엔 s 가 없다 */
    return e;
}
static int cobs_decode_sentinel(uint8_t *buf, size_t n, uint8_t *dst, size_t cap, uint8_t s) {
    for (size_t i = 0; i < n; i++) buf[i] ^= s;  /* 받은 버퍼를 제자리에서 되돌린다 */
    return cobs_decode(buf, n, dst, cap);
}

static uint16_t crc16_ccitt_false(const uint8_t *p, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static void fill(uint8_t *p, size_t n, int mode, uint32_t *seed) {
    for (size_t i = 0; i < n; i++) {
        uint32_t r = xs32(seed);
        if (mode == 0) p[i] = (uint8_t)r;
        else if (mode == 1) p[i] = (r & 0x100) ? 0 : (uint8_t)(r | 1);
        else p[i] = (uint8_t)(1 + r % 255u);
    }
}

int main(void) {
    uint32_t seed = 0xB12u;
    static uint8_t a[1100], b[1200], c[1200], d[1200];

    /* 연습 1 */
    static uint8_t txbuf[COBS_MAX_ENC(64 + 2) + 1];                 /* payload 64 + CRC 2 + 구분자 */
    CHECK(sizeof txbuf == 68);                                      /* 66 + 1 + 1 */
    _Static_assert(COBS_MAX_ENC(254) == 255 && COBS_MAX_ENC(255) == 257 && COBS_MAX_ENC(0) == 1, "max");
    int bad1 = 0;
    for (int t = 0; t < 5000; t++) {
        size_t n = xs32(&seed) % 1001u;
        fill(a, n, t % 3, &seed);
        int e = cobs_encode(a, n, b, sizeof b);
        if (e < 0 || cobs_encoded_len(a, n) != (size_t)e || (size_t)e > COBS_MAX_ENC(n)) bad1++;
    }
    for (size_t n = 250; n <= 520; n++) {                           /* 경계 근처 0 없는 데이터 */
        fill(a, n, 2, &seed);
        if (cobs_encoded_len(a, n) != COBS_MAX_ENC(n) || cobs_encode(a, n, b, sizeof b) != (int)COBS_MAX_ENC(n)) bad1++;
    }
    CHECK(bad1 == 0);
    CHECK(cobs_encoded_len(NULL, 0) == 1);

    /* 연습 2: 랜덤 블록 열을 직접 만들어 넣는다 (FF 와 01 을 자주) */
    int lax_ok = 0, strict_ok = 0, noncanon = 0, bad2 = 0;
    for (int t = 0; t < 20000; t++) {
        size_t n = 0; int blocks = 1 + (int)(xs32(&seed) % 4u);
        for (int k = 0; k < blocks; k++) {
            uint32_t r = xs32(&seed) % 4u;
            uint8_t code = r == 0 ? 0xFF : r == 1 ? 0x01 : (uint8_t)(1 + xs32(&seed) % 255u);
            c[n++] = code;
            fill(c + n, (size_t)code - 1, 2, &seed); n += (size_t)code - 1;
        }
        int dl = cobs_decode(c, n, a, sizeof a);
        int ds = cobs_decode_strict(c, n, a, sizeof a);
        if (dl >= 0) lax_ok++;
        if (ds >= 0) strict_ok++;
        if (ds == COBS_ERR_NONCANON) noncanon++;
        int dd = cobs_decode(c, n, a, sizeof a);
        bool canon = dd >= 0 && cobs_encode(a, (size_t)dd, b, sizeof b) == (int)n && memcmp(b, c, n) == 0;
        if ((ds >= 0) != canon) bad2++;                              /* 엄격 통과 ⇔ 재인코딩이 같음 */
    }
    printf("ex2 strict: 20000 block sequences, lax accepted=%d strict accepted=%d noncanonical=%d bad=%d\n",
           lax_ok, strict_ok, noncanon, bad2);
    CHECK(bad2 == 0 && noncanon > 0 && lax_ok == strict_ok + noncanon);
    memset(c, 0x41, 255); c[0] = 0xFF; c[255] = 0x01;
    CHECK(cobs_decode(c, 256, a, sizeof a) == 254 && cobs_decode_strict(c, 256, a, sizeof a) == COBS_ERR_NONCANON);
    CHECK(cobs_decode_strict(c, 255, a, sizeof a) == 254);

    /* 연습 3: 제자리 디코딩 */
    int bad3 = 0;
    for (int t = 0; t < 5000; t++) {
        size_t n = xs32(&seed) % 1001u;
        fill(a, n, t % 3, &seed);
        int e = cobs_encode(a, n, b, sizeof b);
        memcpy(d, b, (size_t)e);
        int o1 = cobs_decode(b, (size_t)e, c, sizeof c);
        int o2 = cobs_decode(d, (size_t)e, d, (size_t)e);           /* dst == src */
        if (o1 != (int)n || o2 != (int)n || (n && (memcmp(c, a, n) || memcmp(d, a, n)))) bad3++;
    }
    CHECK(bad3 == 0);

    /* 연습 4: 모든 구분자 s */
    int bad4 = 0;
    for (int s = 0; s < 256; s++) {
        for (int t = 0; t < 40; t++) {
            size_t n = xs32(&seed) % 600u;
            fill(a, n, t % 3, &seed);
            int e = cobs_encode_sentinel(a, n, b, sizeof b, (uint8_t)s);
            for (int i = 0; i < e; i++) if (b[i] == (uint8_t)s) bad4++;
            int o = cobs_decode_sentinel(b, (size_t)e, c, sizeof c, (uint8_t)s);
            if (o != (int)n || (n && memcmp(c, a, n))) bad4++;
        }
    }
    CHECK(bad4 == 0);

    /* 연습 5: 한 바이트 손상 (0 이 아닌 다른 값으로) */
    long data_hits = 0, data_bad = 0, code_hits = 0, code_struct = 0, code_crc = 0, code_missed = 0;
    for (int t = 0; t < 40000; t++) {
        uint8_t pl[34], dec[64];
        fill(pl, 32, t & 1, &seed);
        uint16_t cr = crc16_ccitt_false(pl, 32);
        pl[32] = (uint8_t)(cr >> 8); pl[33] = (uint8_t)cr;
        int en = cobs_encode(pl, 34, b, sizeof b);
        size_t k = xs32(&seed) % (size_t)en;
        bool is_code = false;
        for (size_t i = 0; i < (size_t)en; i += b[i]) if (i == k) { is_code = true; break; }
        uint8_t nv;
        do nv = (uint8_t)(1 + xs32(&seed) % 255u); while (nv == b[k]);
        b[k] = nv;
        int o = cobs_decode(b, (size_t)en, dec, sizeof dec);
        if (!is_code) {                                  /* 데이터 바이트: 구조 통과 + 정확히 1 바이트 다름 */
            data_hits++;
            int diff = 0;
            for (int i = 0; i < 34 && o == 34; i++) diff += dec[i] != pl[i];
            if (o != 34 || diff != 1) data_bad++;
        } else {
            code_hits++;
            if (o < 0) code_struct++;
            else if (o >= 2 && crc16_ccitt_false(dec, (size_t)o - 2) == (uint16_t)(dec[o - 2] << 8 | dec[o - 1])) code_missed++;
            else code_crc++;
        }
    }
    printf("ex5 corruption: data-byte hits=%ld (struct-invisible, exactly 1 byte wrong: bad=%ld)\n", data_hits, data_bad);
    printf("                code-byte hits=%ld -> COBS structure %ld, CRC %ld, missed %ld\n",
           code_hits, code_struct, code_crc, code_missed);
    CHECK(data_hits > 0 && data_bad == 0);
    CHECK(code_hits > 0 && code_missed == 0 && code_struct + code_crc == code_hits);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
