/* C-B13 엔디안 판별과 바이트 스왑
 * Part A: 런타임/컴파일타임 엔디안 판별 + 메모리 덤프
 * Part B: bswap16/32/64 (시프트) vs __builtin_bswap*, cpu_to_be32
 * Part C: 호스트 엔디안과 무관한 read_be/write_le + 센서·패킷 파싱
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- Part A: 엔디안 판별 + 메모리 덤프 ---------------- */
typedef enum { ENDIAN_UNKNOWN = 0, ENDIAN_LITTLE, ENDIAN_BIG } endian_t;

/* 런타임 ①: union 으로 같은 4바이트를 두 타입으로 본다 (C에선 type punning 허용) */
static bool is_le_union(void) {
    union { uint32_t u; uint8_t b[4]; } v = { .u = 0x01020304u };
    return v.b[0] == 0x04u;
}

/* 런타임 ②: 문자 타입 포인터로 첫(가장 낮은 주소) 바이트를 읽는다 */
static bool is_le_charptr(void) {
    const uint32_t one = 1u;
    return *(const unsigned char *)&one == 1u;
}

/* 컴파일타임: GCC/Clang 미리 정의 매크로 (__BYTE_ORDER__) */
static endian_t endian_ct(void) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ENDIAN_LITTLE;
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return ENDIAN_BIG;
#else
    return ENDIAN_UNKNOWN;
#endif
}

/* p부터 n바이트를 낮은 주소부터 "44 33 22 11" 형태로 out에 쓴다 (소문자, NUL 종료).
 * cap < 3*n (n==0 이면 1) 이면 아무것도 안 쓰고 0. 성공 시 쓴 글자 수(NUL 제외). */
static size_t hexdump(const void *p, size_t n, char *out, size_t cap) {
    static const char hex[] = "0123456789abcdef";
    const unsigned char *b = p;
    size_t need = n ? 3 * n : 1;
    if (cap < need) return 0;
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (i) out[k++] = ' ';
        out[k++] = hex[b[i] >> 4];
        out[k++] = hex[b[i] & 0x0Fu];
    }
    out[k] = '\0';
    return k;
}

/* ---------------- Part B: 바이트 스왑 (시프트) ---------------- */
static inline uint16_t bswap16(uint16_t x) {
    return (uint16_t)((x << 8) | (x >> 8));
}
static inline uint32_t bswap32(uint32_t x) {
    return (x >> 24) | ((x >> 8) & 0x0000FF00u) | ((x << 8) & 0x00FF0000u) | (x << 24);
}
static inline uint64_t bswap64(uint64_t x) {
    x = ((x & 0x00000000FFFFFFFFull) << 32) | ((x & 0xFFFFFFFF00000000ull) >> 32);
    x = ((x & 0x0000FFFF0000FFFFull) << 16) | ((x & 0xFFFF0000FFFF0000ull) >> 16);
    x = ((x & 0x00FF00FF00FF00FFull) << 8)  | ((x & 0xFF00FF00FF00FF00ull) >> 8);
    return x;
}
/* htonl 과 같은 일: 호스트 값 → 메모리에 빅 엔디안으로 놓일 값 */
static inline uint32_t cpu_to_be32(uint32_t x) {
    return endian_ct() == ENDIAN_LITTLE ? bswap32(x) : x;
}

/* ---------------- Part C: 호스트 무관 바이트 배열 접근 ---------------- */
static inline uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
static inline uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline uint32_t read_le32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void write_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static inline void write_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* TMP117 온도 레지스터: I2C로 MSB 먼저 2바이트, 16비트 2의 보수, 1 LSB = 7.8125 m°C = 125/16 */
static int32_t tmp117_mC(const uint8_t rx[2]) {
    uint16_t raw = read_be16(rx);
    int32_t s = (raw & 0x8000u) ? (int32_t)raw - 65536 : (int32_t)raw;  /* 이식성 있는 부호 확장 */
    return s * 125 / 16;
}

/* Ethernet II + IPv4 헤더 파싱 (네트워크 바이트 순서 = 빅 엔디안) */
typedef struct {
    uint16_t total_len;
    uint8_t  proto;
    uint32_t src, dst;     /* 호스트 값: 192.168.1.10 == 0xC0A8010A */
} ipv4_info;

static bool parse_eth_ipv4(const uint8_t *f, size_t len, ipv4_info *out) {
    if (len < 14 + 20) return false;
    if (read_be16(f + 12) != 0x0800u) return false;          /* EtherType IPv4 */
    const uint8_t *ip = f + 14;
    if ((ip[0] >> 4) != 4 || (ip[0] & 0x0Fu) < 5) return false;  /* version, IHL */
    out->total_len = read_be16(ip + 2);
    out->proto     = ip[9];
    out->src       = read_be32(ip + 12);                      /* 프레임 오프셋 26: 4바이트 정렬 아님 */
    out->dst       = read_be32(ip + 16);
    return true;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define HOST_IS_LE 0
#else
#define HOST_IS_LE 1
#endif

static uint64_t rng = 0x9E3779B97F4A7C15ull;   /* xorshift64 — 무작위 대조용 */
static uint64_t next_rand(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

int main(void) {
    char buf[64];

    /* Part A */
    CHECK(is_le_union() == HOST_IS_LE);
    CHECK(is_le_charptr() == HOST_IS_LE);
    CHECK(endian_ct() == (HOST_IS_LE ? ENDIAN_LITTLE : ENDIAN_BIG));
    uint32_t v32 = 0x11223344u;
    CHECK(hexdump(&v32, 4, buf, sizeof buf) == 11 &&
          strcmp(buf, HOST_IS_LE ? "44 33 22 11" : "11 22 33 44") == 0);
    uint16_t v16 = 0xABCDu;
    CHECK(hexdump(&v16, 2, buf, sizeof buf) == 5 && strcmp(buf, HOST_IS_LE ? "cd ab" : "ab cd") == 0);
    CHECK(hexdump(&v32, 4, buf, 11) == 0);            /* 11 < 3*4 → 거부 */

    /* Part B */
    CHECK(bswap16(0x1234u) == 0x3412u);
    CHECK(bswap16(0xFF00u) == 0x00FFu);
    CHECK(bswap32(0x11223344u) == 0x44332211u);
    CHECK(bswap32(0x80000001u) == 0x01000080u);
    CHECK(bswap64(0x0102030405060708ull) == 0x0807060504030201ull);
    unsigned bad = 0;
    for (int i = 0; i < 1000; i++) {
        uint64_t r = next_rand();
        if (bswap16((uint16_t)r) != __builtin_bswap16((uint16_t)r)) bad++;
        if (bswap32((uint32_t)r) != __builtin_bswap32((uint32_t)r)) bad++;
        if (bswap64(r) != __builtin_bswap64(r)) bad++;
        if (bswap64(bswap64(r)) != r) bad++;
    }
    CHECK(bad == 0);
    uint32_t be = cpu_to_be32(0x11223344u);
    CHECK(hexdump(&be, 4, buf, sizeof buf) == 11 && strcmp(buf, "11 22 33 44") == 0);  /* 어느 호스트든 */

    /* Part C */
    const uint8_t b[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01 };
    CHECK(read_be16(b) == 0xDEADu);
    CHECK(read_be32(b) == 0xDEADBEEFu);
    CHECK(read_le32(b) == 0xEFBEADDEu);
    CHECK(read_be32(b + 1) == 0xADBEEF01u);            /* 홀수 주소에서도 OK */
    uint8_t w[6] = { 0 };
    write_le16(w, 0x1234u);
    write_be32(w + 2, 0xCAFEF00Du);
    CHECK(hexdump(w, 6, buf, sizeof buf) == 17 && strcmp(buf, "34 12 ca fe f0 0d") == 0);
    CHECK(read_be32(w + 2) == 0xCAFEF00Du);

    CHECK(tmp117_mC((const uint8_t[]){ 0x0C, 0x80 }) == 25000);   /* +25 °C */
    CHECK(tmp117_mC((const uint8_t[]){ 0xFF, 0x80 }) == -1000);   /* -1 °C  */
    CHECK(tmp117_mC((const uint8_t[]){ 0x80, 0x00 }) == -256000);

    const uint8_t frame[] = {
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0x02,0x11,0x22,0x33,0x44,0x55, 0x08,0x00,  /* Ethernet */
        0x45,0x00,0x00,0x54, 0x12,0x34,0x40,0x00, 0x40,0x01,0x00,0x00,             /* IPv4 ... */
        0xC0,0xA8,0x01,0x0A, 0x08,0x08,0x08,0x08                                   /* src, dst */
    };
    ipv4_info ip = { 0 };
    CHECK(parse_eth_ipv4(frame, sizeof frame, &ip) &&
          ip.total_len == 84 && ip.proto == 1 && ip.src == 0xC0A8010Au && ip.dst == 0x08080808u);
    CHECK(!parse_eth_ipv4(frame, 33, &ip));            /* 너무 짧음 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
