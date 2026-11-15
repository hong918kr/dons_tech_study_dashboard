/* ALGO-B14 추가 연습 — 한 번에 검증
 * 연습 1 NMEA "*hh" XOR 검사 · 연습 2 Fletcher-16 체크 바이트 · 연습 3 Adler-32
 * 연습 4 UDP(IPv4) 의사 헤더 체크섬 · 연습 5 RFC 1071 바이트 순서 독립성
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 공통: 본문과 같은 RFC 1071 / Fletcher-16 */
static uint32_t ones_sum(const uint8_t *p, size_t n, uint32_t sum) {   /* 접지 않은 BE 워드 합 누적 */
    while (n > 1) { sum += (uint32_t)p[0] << 8 | p[1]; p += 2; n -= 2; }
    if (n) sum += (uint32_t)p[0] << 8;
    return sum;
}
static uint16_t fold16(uint32_t s) { while (s >> 16) s = (s & 0xFFFFu) + (s >> 16); return (uint16_t)s; }
static uint16_t inet_checksum(const uint8_t *p, size_t n) { return (uint16_t)~fold16(ones_sum(p, n, 0)); }
static uint16_t fletcher16(const uint8_t *p, size_t n) {
    uint32_t s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; i++) { s1 = (s1 + p[i]) % 255u; s2 = (s2 + s1) % 255u; }
    return (uint16_t)(s2 << 8 | s1);
}

/* ---- 연습 1: NMEA 0183 "$<본문>*hh" — '$' 와 '*' 사이 문자의 XOR 를 16진 두 자리와 비교 ---- */
static int hex1(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static bool nmea_star_ok(const char *s) {
    if (*s != '$') return false;
    uint8_t x = 0;
    const char *q = s + 1;
    for (; *q && *q != '*'; q++) x ^= (uint8_t)*q;
    if (*q != '*') return false;
    int hi = hex1(q[1]), lo = hi < 0 ? -1 : hex1(q[2]);
    if (hi < 0 || lo < 0) return false;
    return x == (uint8_t)(hi << 4 | lo);
}

/* ---- 연습 2: 메시지 뒤에 붙이면 fletcher16(전체) == 0 이 되는 체크 바이트 c0, c1 ---- */
static void fletcher16_checkbytes(const uint8_t *p, size_t n, uint8_t *c0, uint8_t *c1) {
    uint16_t f = fletcher16(p, n);
    uint32_t f0 = f & 0xFFu, f1 = f >> 8;
    *c0 = (uint8_t)(255u - (f0 + f1) % 255u);
    *c1 = (uint8_t)(255u - (f0 + *c0) % 255u);
}

/* ---- 연습 3: Adler-32 (zlib, RFC 1950): A 는 1 에서 시작, mod 65521(소수) ---- */
static uint32_t adler32(const uint8_t *p, size_t n) {
    uint32_t a = 1, b = 0;
    while (n) {
        size_t blk = n < 5552u ? n : 5552u;     /* zlib 의 NMAX: 이 길이까지 b 가 32비트에 들어간다 */
        n -= blk;
        do { a += *p++; b += a; } while (--blk);
        a %= 65521u; b %= 65521u;
    }
    return b << 16 | a;
}

/* ---- 연습 4: UDP/IPv4 체크섬 = 의사 헤더(src, dst, 0, proto=17, UDP 길이) + UDP 헤더 + 데이터 ----
 * udp[6..7] (체크섬 필드)은 0 으로 두고 호출. 계산 결과 0 은 0xFFFF 로 보낸다 (0 = "체크섬 없음", RFC 768). */
static uint16_t udp4_checksum(uint32_t src, uint32_t dst, const uint8_t *udp, size_t len) {
    uint32_t s = 0;
    s += src >> 16; s += src & 0xFFFFu;
    s += dst >> 16; s += dst & 0xFFFFu;
    s += 17u;                       /* 0x00 | protocol */
    s += (uint32_t)len;             /* UDP 길이 (헤더 포함) */
    s = ones_sum(udp, len, s);
    uint16_t c = (uint16_t)~fold16(s);
    return c == 0 ? 0xFFFF : c;
}

/* ---- 연습 5: 리틀 엔디언 CPU 에서 "그냥 16비트로 읽어" 더한 뒤 결과만 바이트 스왑 ---- */
static uint16_t inet_checksum_le_then_swap(const uint8_t *p, size_t n) {
    uint32_t s = 0;
    while (n > 1) { s += (uint32_t)p[1] << 8 | p[0]; p += 2; n -= 2; }   /* LE 워드 */
    if (n) s += p[0];                                                     /* LE 에선 끝 바이트가 하위 */
    uint16_t c = (uint16_t)~fold16(s);
    return (uint16_t)(c << 8 | c >> 8);
}

/* ---------------- 테스트 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

int main(void) {
    static uint8_t buf[20000];
    uint32_t seed = 0xB14B14u;

    /* 연습 1 */
    CHECK(nmea_star_ok("$GPGLL,4916.45,N,12311.12,W,225444,A,*1D"));
    CHECK(nmea_star_ok("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"));
    CHECK(!nmea_star_ok("$GPGLL,4916.45,N,12311.12,W,225444,A,*1E"));
    CHECK(!nmea_star_ok("$GPGLL,4916.46,N,12311.12,W,225444,A,*1D"));
    CHECK(!nmea_star_ok("$GPGLL,4916.45,N,12311.12,W,225444,A,"));      /* '*' 없음 */
    CHECK(!nmea_star_ok("GPGLL,4916.45,N,12311.12,W,225444,A,*1D"));    /* '$' 없음 */
    CHECK(!nmea_star_ok("$GPGLL,4916.45,N,12311.12,W,225444,A,*1"));    /* 한 자리 */
    CHECK(nmea_star_ok("$GPGLL,4916.45,N,12311.12,W,225444,A,*1d"));    /* 소문자 허용 */
    /* XOR 약점: 두 문자를 서로 바꾸면(순서 오류) 못 잡는다 */
    CHECK(nmea_star_ok("$GPGLL,4916.45,N,12311.12,W,225444,A,*1D") &&
          nmea_star_ok("$GPGLL,4961.45,N,12311.12,W,225444,A,*1D"));

    /* 연습 2 */
    int bad2 = 0;
    for (int t = 0; t < 2000; t++) {
        size_t n = xs32(&seed) % 300u;
        for (size_t i = 0; i < n; i++) buf[i] = (t % 4 == 0) ? 0 : (uint8_t)xs32(&seed);
        fletcher16_checkbytes(buf, n, &buf[n], &buf[n + 1]);
        bad2 += fletcher16(buf, n + 2) != 0;
    }
    CHECK(bad2 == 0);
    uint8_t c0, c1, abc[7] = { 'a', 'b', 'c', 'd', 'e' };
    fletcher16_checkbytes(abc, 5, &c0, &c1);
    abc[5] = c0; abc[6] = c1;
    CHECK(fletcher16(abc, 7) == 0 && c0 == 0x46 && c1 == 0xC8);   /* "abcde": f0=0xF0, f1=0xC8 → 70, 200 */

    /* 연습 3: 위키백과 예 "Wikipedia" → 0x11E60398, 기준 = 닫힌 식 */
    CHECK(adler32((const uint8_t *)"Wikipedia", 9) == 0x11E60398u);
    CHECK(adler32(buf, 0) == 1u);
    int bad3 = 0;
    for (int t = 0; t < 300; t++) {
        size_t n = (t % 2) ? xs32(&seed) % 20000u : (size_t)(5552 * (t % 4) + t % 7);
        for (size_t i = 0; i < n; i++) buf[i] = (t % 3 == 0) ? 0xFF : (uint8_t)xs32(&seed);
        uint64_t A = 1, B = n;                         /* A = 1 + Σb, B = n + Σ (n-i) b_i */
        for (size_t i = 0; i < n; i++) { A += buf[i]; B += (uint64_t)(n - i) * buf[i]; }
        bad3 += adler32(buf, n) != (uint32_t)((B % 65521u) << 16 | (A % 65521u));
    }
    CHECK(bad3 == 0);

    /* 연습 4: 의사 헤더를 실제로 이어 붙인 버퍼로 계산한 값과 같아야 + 수신 측 검증 = 0 */
    int bad4 = 0;
    for (int t = 0; t < 2000; t++) {
        uint32_t src = xs32(&seed), dst = xs32(&seed);
        size_t len = 8 + xs32(&seed) % 100u;
        uint8_t pkt[12 + 108];
        uint8_t *u = pkt + 12;
        for (size_t i = 0; i < len; i++) u[i] = (uint8_t)xs32(&seed);
        u[4] = (uint8_t)(len >> 8); u[5] = (uint8_t)len; u[6] = u[7] = 0;
        uint8_t ph[12] = { (uint8_t)(src >> 24), (uint8_t)(src >> 16), (uint8_t)(src >> 8), (uint8_t)src,
                           (uint8_t)(dst >> 24), (uint8_t)(dst >> 16), (uint8_t)(dst >> 8), (uint8_t)dst,
                           0, 17, (uint8_t)(len >> 8), (uint8_t)len };
        memcpy(pkt, ph, 12);
        uint16_t ref = inet_checksum(pkt, 12 + len);
        uint16_t c = udp4_checksum(src, dst, u, len);
        bad4 += c != (ref == 0 ? 0xFFFF : ref);
        u[6] = (uint8_t)(c >> 8); u[7] = (uint8_t)c;
        bad4 += inet_checksum(pkt, 12 + len) != 0;       /* 수신 측: 전부 더하면 -0 → 보수 0 */
    }
    CHECK(bad4 == 0);
    {   /* 계산값이 0 이 되는 패킷을 일부러 만든다: 마지막 데이터 워드 = (그 워드를 0 으로 둔) 체크섬 */
        uint8_t u[12] = { 0x30, 0x39, 0x00, 0x35, 0x00, 0x0C, 0, 0, 'h', 'i', 0, 0 };
        uint16_t c = udp4_checksum(0xC0A80001u, 0xC0A800C7u, u, 12);
        u[10] = (uint8_t)(c >> 8); u[11] = (uint8_t)c;
        CHECK(udp4_checksum(0xC0A80001u, 0xC0A800C7u, u, 12) == 0xFFFF);
    }

    /* 연습 5: 홀수·짝수 길이 모두 */
    int bad5 = 0;
    for (int t = 0; t < 3000; t++) {
        size_t n = xs32(&seed) % 257u;
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
        bad5 += inet_checksum_le_then_swap(buf, n) != inet_checksum(buf, n);
    }
    CHECK(bad5 == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
