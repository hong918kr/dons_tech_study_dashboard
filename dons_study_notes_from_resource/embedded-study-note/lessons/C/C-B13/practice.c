/* C-B13 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static inline uint16_t bswap16(uint16_t x) { return (uint16_t)((x << 8) | (x >> 8)); }
static inline uint32_t bswap32(uint32_t x) {
    return (x >> 24) | ((x >> 8) & 0x0000FF00u) | ((x << 8) & 0x00FF0000u) | (x << 24);
}
static inline uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline uint32_t read_le32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 연습 1: ARM REV16 / REVSH 를 C로 */
static uint32_t rev16(uint32_t x) {          /* 각 16비트 반쪽 안에서 바이트 교환 */
    return ((x & 0x00FF00FFu) << 8) | ((x & 0xFF00FF00u) >> 8);
}
static int32_t revsh(uint32_t x) {           /* 하위 16비트 바이트 교환 후 부호 확장 */
    uint16_t h = bswap16((uint16_t)x);
    return (h & 0x8000u) ? (int32_t)h - 65536 : (int32_t)h;
}

/* 연습 2: 24비트 빅 엔디안 ADC 샘플 (예: ADS1220) → 부호 있는 int32 */
static int32_t read_be24_signed(const uint8_t *p) {
    uint32_t v = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    return (v & 0x800000u) ? (int32_t)v - 0x1000000 : (int32_t)v;
}

/* 연습 3: CAN 신호 — DBC의 Intel(리틀) vs Motorola(빅) 바이트 순서 (바이트 정렬된 16비트) */
static uint16_t can_get_u16(const uint8_t data[8], unsigned byte, bool motorola) {
    return motorola ? (uint16_t)((data[byte] << 8) | data[byte + 1])
                    : (uint16_t)(data[byte] | (data[byte + 1] << 8));
}

/* 연습 4: 매직 넘버로 파일/패킷의 바이트 순서 판별 (pcap: 0xA1B2C3D4) */
static int pcap_order(const uint8_t hdr[4]) {   /* 0 = 리틀, 1 = 빅, -1 = 아님 */
    if (read_le32(hdr) == 0xA1B2C3D4u) return 0;
    if (read_be32(hdr) == 0xA1B2C3D4u) return 1;
    return -1;
}
static uint32_t rd32(const uint8_t *p, int order) { return order ? read_be32(p) : read_le32(p); }

/* 연습 5: memcpy 로 네이티브 로드 + 필요 시 스왑 (정렬·aliasing 문제 없음) */
static uint32_t read_be32_memcpy(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, sizeof v);                   /* 컴파일러가 단일 load 로 바꾼다 */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    v = bswap32(v);
#endif
    return v;
}

/* 연습 6: SPI LCD(ILI9341 등)는 RGB565 픽셀을 MSB 먼저 받는다 → 버퍼 전체 16비트 스왑 */
static void swap16_buf(uint16_t *px, size_t n) {
    for (size_t i = 0; i < n; i++) px[i] = bswap16(px[i]);
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    CHECK(rev16(0x11223344u) == 0x22114433u);
    CHECK(revsh(0x12340080u) == -32768);           /* 0x0080 → 0x8000 → 부호 확장 */
    CHECK(revsh(0x00003412u) == 0x1234);

    CHECK(read_be24_signed((const uint8_t[]){ 0x7F, 0xFF, 0xFF }) == 8388607);
    CHECK(read_be24_signed((const uint8_t[]){ 0x80, 0x00, 0x00 }) == -8388608);
    CHECK(read_be24_signed((const uint8_t[]){ 0xFF, 0xFF, 0xFF }) == -1);

    const uint8_t can[8] = { 0, 0, 0x12, 0x34, 0, 0, 0, 0 };
    CHECK(can_get_u16(can, 2, false) == 0x3412u);
    CHECK(can_get_u16(can, 2, true) == 0x1234u);

    const uint8_t le_hdr[8] = { 0xD4, 0xC3, 0xB2, 0xA1, 0x02, 0x00, 0x04, 0x00 };
    const uint8_t be_hdr[8] = { 0xA1, 0xB2, 0xC3, 0xD4, 0x00, 0x02, 0x00, 0x04 };
    CHECK(pcap_order(le_hdr) == 0 && pcap_order(be_hdr) == 1);
    CHECK(rd32(le_hdr + 4, 0) == 0x00040002u && rd32(be_hdr + 4, 1) == 0x00020004u);
    CHECK(pcap_order((const uint8_t[]){ 1, 2, 3, 4 }) == -1);

    const uint8_t pkt[7] = { 0xAA, 0xDE, 0xAD, 0xBE, 0xEF, 0x55, 0x66 };
    CHECK(read_be32_memcpy(pkt + 1) == 0xDEADBEEFu);   /* 홀수 주소 */
    CHECK(read_be32_memcpy(pkt + 3) == read_be32(pkt + 3));

    uint16_t fb[3] = { 0xF800u, 0x07E0u, 0x001Fu };   /* 빨강, 초록, 파랑 */
    swap16_buf(fb, 3);
    CHECK(fb[0] == 0x00F8u && fb[1] == 0xE007u && fb[2] == 0x1F00u);
    const unsigned char *raw = (const unsigned char *)fb;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    CHECK(raw[0] == 0xF8 && raw[1] == 0x00);           /* 메모리상 MSB 먼저 → SPI DMA 그대로 전송 */
#else
    CHECK(raw[0] == 0x00 && raw[1] == 0xF8);
#endif

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
