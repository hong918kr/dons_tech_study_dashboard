/* ALGO-B14 체크섬: 합 체크섬·인터넷 체크섬·Fletcher
 *
 * Part A: sum8 / xor8 / sum8_twos(Intel HEX 방식) / RFC 1071 인터넷 체크섬 / Fletcher-16 / Fletcher-32
 * Part B: RFC 1624 증분 갱신, 나눗셈 없는 Fletcher-16 (지연 환원 + 255 접기)
 * Part C: Intel HEX 레코드 검사(부트로더), IPv4 라우터의 TTL 감소 + 체크섬 증분 갱신
 *
 * 테스트: 공개 test vector(RFC 1071 예제, IPv4 헤더, Fletcher "abcde"…, Intel HEX 예제) +
 *         다른 방식으로 계산한 기준 구현(닫힌 식, uint64 + 나머지)과 xorshift32 고정 시드 랜덤 대조.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- Part A: 체크섬 다섯 가지 ---------------- */

/* 모든 바이트의 합 mod 256 */
static uint8_t sum8(const uint8_t *p, size_t n) {
    uint8_t s = 0;
    for (size_t i = 0; i < n; i++) s = (uint8_t)(s + p[i]);
    return s;
}

/* 모든 바이트의 XOR (= 비트 위치마다 따로 센 패리티 8개) */
static uint8_t xor8(const uint8_t *p, size_t n) {
    uint8_t x = 0;
    for (size_t i = 0; i < n; i++) x ^= p[i];
    return x;
}

/* 2의 보수 체크섬: 데이터 + 이 바이트의 합이 mod 256 으로 0 이 되게 (Intel HEX) */
static uint8_t sum8_twos(const uint8_t *p, size_t n) {
    return (uint8_t)(0u - sum8(p, n));
}

/* RFC 1071: 16비트 big-endian 워드의 1의 보수 합을 다시 보수. 홀수 길이는 0 바이트로 채운다.
 * 32비트 누산기: n <= 131074 바이트까지 넘침 없음 (65537 워드 × 0xFFFF < 2^32). */
static uint16_t inet_checksum(const uint8_t *p, size_t n) {
    uint32_t sum = 0;
    while (n > 1) { sum += (uint32_t)p[0] << 8 | p[1]; p += 2; n -= 2; }
    if (n) sum += (uint32_t)p[0] << 8;                    /* 홀수 끝 바이트 = 상위 바이트 */
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16); /* end-around carry */
    return (uint16_t)~sum;
}

/* Fletcher-16: 두 합을 mod 255 로. 결과 = sum2 << 8 | sum1 */
static uint16_t fletcher16(const uint8_t *p, size_t n) {
    uint32_t s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; i++) {
        s1 = (s1 + p[i]) % 255u;
        s2 = (s2 + s1) % 255u;
    }
    return (uint16_t)(s2 << 8 | s1);
}

/* Fletcher-32: 16비트 워드(little-endian 조립, 홀수 길이는 0 채움)의 두 합을 mod 65535 로 */
static uint32_t fletcher32(const uint8_t *p, size_t n) {
    uint32_t s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; i += 2) {
        uint32_t w = p[i] | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0u);
        s1 = (s1 + w) % 65535u;
        s2 = (s2 + s1) % 65535u;
    }
    return s2 << 16 | s1;
}

/* ---------------- Part B: 증분 갱신, 나눗셈 없는 Fletcher ---------------- */

/* RFC 1624 식 3: 16비트 필드가 m → m2 로 바뀔 때 HC' = ~(~HC + ~m + m2) (1의 보수 덧셈) */
static uint16_t inet_update16(uint16_t hc, uint16_t m, uint16_t m2) {
    uint32_t s = (uint32_t)(uint16_t)~hc + (uint16_t)~m + m2;
    s = (s & 0xFFFFu) + (s >> 16);
    s = (s & 0xFFFFu) + (s >> 16);
    return (uint16_t)~s;
}

/* x mod 255 를 나눗셈 없이: 256 ≡ 1 (mod 255) 이므로 상위 바이트를 하위에 더해도 나머지가 같다.
 * 4번 접으면 x <= 256 → 마지막 한 번 빼서 [0, 254]. */
static uint32_t fold255(uint32_t x) {
    x = (x & 0xFFu) + (x >> 8);
    x = (x & 0xFFu) + (x >> 8);
    x = (x & 0xFFu) + (x >> 8);
    x = (x & 0xFFu) + (x >> 8);
    return x >= 255u ? x - 255u : x;
}

/* 블록 5802 바이트마다 한 번만 환원 (s1, s2 <= 254 에서 시작하면 5802 바이트 동안 s2 < 2^32) */
#define F16_BLOCK 5802u
static uint16_t fletcher16_fast(const uint8_t *p, size_t n) {
    uint32_t s1 = 0, s2 = 0;
    while (n) {
        size_t blk = n < F16_BLOCK ? n : F16_BLOCK;
        n -= blk;
        do { s1 += *p++; s2 += s1; } while (--blk);
        s1 = fold255(s1);
        s2 = fold255(s2);
    }
    return (uint16_t)(s2 << 8 | s1);
}

/* ---------------- Part C: 부트로더 · 라우터 ---------------- */

/* Intel HEX 한 줄 ":LLAAAATT<data>CC" 검사. 끝의 \r \n 은 허용. */
enum { IHEX_OK = 0, IHEX_ERR_FORMAT = -1, IHEX_ERR_LEN = -2, IHEX_ERR_CKSUM = -3 };
typedef struct { uint8_t len, type; uint16_t addr; uint8_t data[255]; } ihex_rec;

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int ihex_parse(const char *line, ihex_rec *r) {
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
    if (n < 11 || line[0] != ':' || (n - 1) % 2 != 0) return IHEX_ERR_FORMAT;
    uint8_t b[5 + 255];
    size_t nb = (n - 1) / 2;
    if (nb > sizeof b) return IHEX_ERR_LEN;
    for (size_t i = 0; i < nb; i++) {
        int hi = hexval(line[1 + 2 * i]), lo = hexval(line[2 + 2 * i]);
        if (hi < 0 || lo < 0) return IHEX_ERR_FORMAT;
        b[i] = (uint8_t)(hi << 4 | lo);
    }
    if (nb != (size_t)b[0] + 5) return IHEX_ERR_LEN;       /* LL + AAAA + TT + data + CC */
    if (sum8(b, nb) != 0) return IHEX_ERR_CKSUM;           /* 체크섬 바이트까지 더하면 0 */
    r->len = b[0];
    r->addr = (uint16_t)(b[1] << 8 | b[2]);
    r->type = b[3];
    memcpy(r->data, b + 4, b[0]);
    return IHEX_OK;
}

/* IPv4 라우터 전달: 헤더 체크섬 검사 → TTL 감소 → 체크섬 증분 갱신.
 * 반환 0 = 전달, -1 = 헤더 불량(버전/IHL/길이/체크섬), -2 = TTL 만료(TTL <= 1, 버림) */
static int ipv4_forward(uint8_t *h, size_t len) {
    if (len < 20 || (h[0] >> 4) != 4) return -1;
    size_t ihl = (size_t)(h[0] & 0x0Fu) * 4u;
    if (ihl < 20 || ihl > len) return -1;
    if (inet_checksum(h, ihl) != 0) return -1;             /* 체크섬 필드까지 넣고 계산 → 0 */
    if (h[8] <= 1) return -2;
    uint16_t m = (uint16_t)(h[8] << 8 | h[9]);             /* 워드 4 = TTL | protocol */
    h[8]--;
    uint16_t m2 = (uint16_t)(h[8] << 8 | h[9]);
    uint16_t hc = (uint16_t)(h[10] << 8 | h[11]);
    hc = inet_update16(hc, m, m2);
    h[10] = (uint8_t)(hc >> 8);
    h[11] = (uint8_t)hc;
    return 0;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 기준 구현: 접기 대신 "정수 전체 합 → 나머지" 와 닫힌 식으로 계산 (다른 길로 같은 답) */
static uint8_t ref_sum8(const uint8_t *p, size_t n) { uint64_t s = 0; for (size_t i = 0; i < n; i++) s += p[i]; return (uint8_t)(s % 256u); }
static uint8_t ref_xor8(const uint8_t *p, size_t n) {  /* 비트 위치별 1의 개수 홀짝 */
    uint8_t r = 0;
    for (int b = 0; b < 8; b++) { size_t c = 0; for (size_t i = 0; i < n; i++) c += (p[i] >> b) & 1u; r |= (uint8_t)((c & 1u) << b); }
    return r;
}
static uint16_t ref_inet(const uint8_t *p, size_t n) {  /* 1의 보수 합 = 0 이면 0, 아니면 1 + (T-1) mod 65535 */
    uint64_t t = 0;
    for (size_t i = 0; i < n; i++) t += (i % 2 == 0) ? (uint64_t)p[i] << 8 : p[i];
    uint64_t s = t == 0 ? 0 : 1 + (t - 1) % 65535u;
    return (uint16_t)~s;
}
static uint16_t ref_f16(const uint8_t *p, size_t n) {   /* s1 = Σ b_i, s2 = Σ (n-i) b_i */
    uint64_t a = 0, b = 0;
    for (size_t i = 0; i < n; i++) { a += p[i]; b += (uint64_t)(n - i) * p[i]; }
    return (uint16_t)((b % 255u) << 8 | (a % 255u));
}
static uint32_t ref_f32(const uint8_t *p, size_t n) {
    size_t nw = (n + 1) / 2;
    uint64_t a = 0, b = 0;
    for (size_t k = 0; k < nw; k++) {
        uint64_t w = p[2 * k] | (2 * k + 1 < n ? (uint64_t)p[2 * k + 1] << 8 : 0u);
        a += w; b += (uint64_t)(nw - k) * w;
    }
    return (uint32_t)((b % 65535u) << 16 | (a % 65535u));
}

static const uint8_t IP_HDR[20] = {   /* 흔히 인용되는 IPv4 헤더 예 (192.168.0.1 → 192.168.0.199, UDP) */
    0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11,
    0xB8, 0x61, 0xC0, 0xA8, 0x00, 0x01, 0xC0, 0xA8, 0x00, 0xC7 };

int main(void) {
    static uint8_t buf[131072];
    uint32_t seed = 0xB14C0DEu;

    /* ===== Part A: 공개 예제 ===== */
    const uint8_t *abcde = (const uint8_t *)"abcdefgh";
    CHECK(fletcher16(abcde, 5) == 0xC8F0);
    CHECK(fletcher16(abcde, 6) == 0x2057);
    CHECK(fletcher16(abcde, 8) == 0x0627);
    CHECK(fletcher32(abcde, 5) == 0xF04FC729u);
    CHECK(fletcher32(abcde, 6) == 0x56502D2Au);
    CHECK(fletcher32(abcde, 8) == 0xEBE19591u);

    const uint8_t rfc[8] = { 0x00, 0x01, 0xF2, 0x03, 0xF4, 0xF5, 0xF6, 0xF7 };  /* RFC 1071 §3: 합 = ddf2 */
    CHECK(inet_checksum(rfc, 8) == (uint16_t)~0xDDF2u);   /* 0x220D */
    uint8_t ip[20];
    memcpy(ip, IP_HDR, 20);
    ip[10] = ip[11] = 0;                                   /* 체크섬 필드를 0 으로 두고 계산 */
    CHECK(inet_checksum(ip, 20) == 0xB861);
    CHECK(inet_checksum(IP_HDR, 20) == 0);                 /* 필드 포함 검증 → 0 */

    const uint8_t eof_rec[4] = { 0x00, 0x00, 0x00, 0x01 };  /* ":00000001FF" */
    CHECK(sum8_twos(eof_rec, 4) == 0xFF);
    CHECK(sum8((const uint8_t *)"123456789", 9) == 0xDD);  /* 0x31+…+0x39 = 477 = 0x1DD */
    CHECK(xor8((const uint8_t *)"123456789", 9) == 0x31);

    /* 빈 입력 */
    CHECK(sum8(buf, 0) == 0 && xor8(buf, 0) == 0 && sum8_twos(buf, 0) == 0);
    CHECK(inet_checksum(buf, 0) == 0xFFFF && fletcher16(buf, 0) == 0 && fletcher32(buf, 0) == 0);

    /* ===== Part A: 기준 구현과 랜덤 대조 (길이 0~300, 3000개) ===== */
    int bad[6] = {0};
    for (int t = 0; t < 3000; t++) {
        size_t n = xs32(&seed) % 301u;
        int mode = t % 3;   /* 0: 랜덤, 1: 0x00/0xFF 만, 2: 전부 0xFF */
        for (size_t i = 0; i < n; i++)
            buf[i] = mode == 0 ? (uint8_t)xs32(&seed) : mode == 1 ? ((xs32(&seed) & 1u) ? 0xFF : 0x00) : 0xFF;
        bad[0] += sum8(buf, n) != ref_sum8(buf, n);
        bad[1] += xor8(buf, n) != ref_xor8(buf, n);
        bad[2] += (uint8_t)(ref_sum8(buf, n) + sum8_twos(buf, n)) != 0;
        bad[3] += inet_checksum(buf, n) != ref_inet(buf, n);
        bad[4] += fletcher16(buf, n) != ref_f16(buf, n);
        bad[5] += fletcher32(buf, n) != ref_f32(buf, n);
    }
    printf("random 3000 msgs: mismatches sum8=%d xor8=%d twos=%d inet=%d f16=%d f32=%d\n",
           bad[0], bad[1], bad[2], bad[3], bad[4], bad[5]);
    CHECK(bad[0] == 0);
    CHECK(bad[1] == 0);
    CHECK(bad[2] == 0);
    CHECK(bad[3] == 0);
    CHECK(bad[4] == 0);
    CHECK(bad[5] == 0);

    /* 홀수 길이: 끝 바이트는 상위 바이트로, 0 패딩한 짝수 길이와 같아야 */
    uint8_t odd[4] = { 0x12, 0x34, 0x56, 0x00 };
    CHECK(inet_checksum(odd, 3) == inet_checksum(odd, 4));
    CHECK(inet_checksum(odd, 3) == (uint16_t)~(0x1234u + 0x5600u));
    CHECK(fletcher32(odd, 3) == fletcher32(odd, 4));

    /* 누산기 한계: 131072 바이트 전부 0xFF (inet 32비트 누산기 최대치 근처) */
    memset(buf, 0xFF, sizeof buf);
    CHECK(inet_checksum(buf, sizeof buf) == ref_inet(buf, sizeof buf));
    CHECK(inet_checksum(buf, sizeof buf) == 0x0000);       /* 0xFFFF 의 합 = -0 → 보수 0 */

    /* ===== Part B: RFC 1624 증분 갱신 = 전체 재계산 ===== */
    int badu = 0;
    for (int t = 0; t < 20000; t++) {
        uint8_t h[20];
        for (int i = 0; i < 20; i++) h[i] = (uint8_t)xs32(&seed);
        h[0] = 0x45;
        h[10] = h[11] = 0;
        uint16_t hc = ref_inet(h, 20);
        h[10] = (uint8_t)(hc >> 8); h[11] = (uint8_t)hc;
        int w = (int)(xs32(&seed) % 10u);
        if (w == 5) w = 4;                                 /* 체크섬 워드 자신은 바꾸지 않는다 */
        if (w == 0) w = 1;                                 /* 버전 바이트는 유지 (모두 0 헤더 방지) */
        uint16_t m = (uint16_t)(h[2 * w] << 8 | h[2 * w + 1]);
        uint16_t m2 = (t % 4 == 0) ? (uint16_t)(0u - (t & 1u)) : (uint16_t)xs32(&seed);  /* 0x0000/0xFFFF 도 섞음 */
        h[2 * w] = (uint8_t)(m2 >> 8); h[2 * w + 1] = (uint8_t)m2;
        uint16_t inc = inet_update16(hc, m, m2);
        h[10] = h[11] = 0;
        badu += inc != ref_inet(h, 20);   /* 기준 구현으로 전체 재계산 */
    }
    printf("RFC 1624 incremental vs full: 20000 updates, mismatches=%d\n", badu);
    CHECK(badu == 0);
    CHECK(inet_update16(0xB861, 0x4011, 0x3F11) == 0xB961);  /* TTL 0x40 → 0x3F: 합 -0x100 → 체크섬 +0x100 */

    /* ===== Part B: 255 접기, 지연 환원 ===== */
    int badf = 0;
    const uint32_t edge[] = { 0, 1, 254, 255, 256, 509, 510, 65279, 65280, 65535, 0x00FFFFFFu, 0xFEFFFFFFu, 0xFFFFFFFEu, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof edge / sizeof edge[0]; i++) badf += fold255(edge[i]) != edge[i] % 255u;
    for (int t = 0; t < 1000000; t++) { uint32_t x = xs32(&seed); badf += fold255(x) != x % 255u; }
    CHECK(badf == 0);

    /* 5802 가 최대 블록인지: s1,s2 <= 254 에서 바이트 255 를 k 번 더한 뒤 s2 상한 (테스트 쪽 uint64 계산) */
    uint64_t kmax = 0;
    for (uint64_t k = 1; k < 10000; k++) {
        uint64_t s2max = 254 + 254 * k + 255 * k * (k + 1) / 2;
        if (s2max > 0xFFFFFFFFu) break;
        kmax = k;
    }
    CHECK(kmax == F16_BLOCK);

    int badfast = 0;
    for (int t = 0; t < 400; t++) {
        size_t n = (t < 100) ? (size_t)t : (t < 200) ? 3 * F16_BLOCK - 50 + (size_t)(t - 100) : xs32(&seed) % 40000u;
        for (size_t i = 0; i < n; i++) buf[i] = (t % 2) ? 0xFF : (uint8_t)xs32(&seed);   /* 홀수 t = 최악(0xFF) */
        badfast += fletcher16_fast(buf, n) != ref_f16(buf, n);
    }
    printf("fletcher16_fast vs ref: 400 msgs (len 0..40000, half all-0xFF), mismatches=%d\n", badfast);
    CHECK(badfast == 0);
    memset(buf, 0xFF, sizeof buf);
    CHECK(fletcher16_fast(buf, sizeof buf) == ref_f16(buf, sizeof buf));

    /* ===== Part C: Intel HEX ===== */
    ihex_rec r;
    CHECK(ihex_parse(":0B0010006164647265737320676170A7\r\n", &r) == IHEX_OK);
    CHECK(r.len == 11 && r.addr == 0x0010 && r.type == 0x00 && memcmp(r.data, "address gap", 11) == 0);
    CHECK(ihex_parse(":00000001FF", &r) == IHEX_OK && r.type == 0x01 && r.len == 0);
    CHECK(ihex_parse(":10010000214601360121470136007EFE09D2190140", &r) == IHEX_OK && r.addr == 0x0100);
    CHECK(ihex_parse(":0B0010006164647265737320676170A8", &r) == IHEX_ERR_CKSUM);   /* CC 1 틀림 */
    CHECK(ihex_parse(":0B0010006164647265737320676171A7", &r) == IHEX_ERR_CKSUM);   /* 데이터 1비트 */
    CHECK(ihex_parse(":0C0010006164647265737320676170A7", &r) == IHEX_ERR_LEN);     /* LL 과 길이 불일치 */
    CHECK(ihex_parse("00000001FF", &r) == IHEX_ERR_FORMAT);
    CHECK(ihex_parse(":00000001F", &r) == IHEX_ERR_FORMAT);
    CHECK(ihex_parse(":0000000GFF", &r) == IHEX_ERR_FORMAT);
    CHECK(ihex_parse(":", &r) == IHEX_ERR_FORMAT);

    /* ===== Part C: IPv4 TTL 감소 ===== */
    memcpy(ip, IP_HDR, 20);
    CHECK(ipv4_forward(ip, 20) == 0 && ip[8] == 0x3F && ip[10] == 0xB9 && ip[11] == 0x61);
    CHECK(inet_checksum(ip, 20) == 0);
    memcpy(ip, IP_HDR, 20); ip[15] ^= 0x01;
    CHECK(ipv4_forward(ip, 20) == -1);                    /* 주소 1비트 오류 → 버림 */
    memcpy(ip, IP_HDR, 20); ip[8] = 1; ip[10] = ip[11] = 0;
    { uint16_t c = inet_checksum(ip, 20); ip[10] = (uint8_t)(c >> 8); ip[11] = (uint8_t)c; }
    CHECK(ipv4_forward(ip, 20) == -2);                    /* TTL 1 → 만료 */
    memcpy(ip, IP_HDR, 20);
    CHECK(ipv4_forward(ip, 19) == -1);                    /* 너무 짧음 */
    int badfw = 0;
    for (int t = 0; t < 20000; t++) {
        uint8_t h[24];
        for (int i = 0; i < 24; i++) h[i] = (uint8_t)xs32(&seed);
        h[0] = (t & 1) ? 0x46 : 0x45;                     /* IHL 5 또는 6 (옵션 4바이트) */
        h[8] = (uint8_t)(2 + xs32(&seed) % 254u);
        size_t ihl = (size_t)(h[0] & 0xF) * 4;
        h[10] = h[11] = 0;
        uint16_t c = ref_inet(h, ihl); h[10] = (uint8_t)(c >> 8); h[11] = (uint8_t)c;
        uint8_t ttl = h[8];
        if (ipv4_forward(h, ihl) != 0 || h[8] != ttl - 1 || ref_inet(h, ihl) != 0) badfw++;
    }
    printf("ipv4_forward: 20000 random headers, bad=%d\n", badfw);
    CHECK(badfw == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
