/* C-B01 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* 연습 1: 1인 비트 개수 (Brian Kernighan) — 루프 횟수 = 1의 개수 */
static unsigned popcount32(uint32_t x) {
    unsigned c = 0;
    while (x) { x &= x - 1u; c++; }   /* 가장 낮은 1을 하나씩 지운다 */
    return c;
}

/* 연습 2: 2의 거듭제곱인가? (0은 아님) */
static bool is_pow2(uint32_t x) { return x && !(x & (x - 1u)); }

/* 연습 3: 가장 낮은 1비트만 남기기 — 2의 보수: -x == ~x + 1 */
static uint32_t lowest_set(uint32_t x) { return x & (~x + 1u); }

/* 연습 4: 바이트의 니블 교환, 비트 순서 뒤집기 */
static uint8_t swap_nibbles(uint8_t b) { return (uint8_t)((b << 4) | (b >> 4)); }
static uint8_t reverse8(uint8_t b) {
    b = (uint8_t)(((b & 0xF0u) >> 4) | ((b & 0x0Fu) << 4));  /* 4비트 묶음 교환 */
    b = (uint8_t)(((b & 0xCCu) >> 2) | ((b & 0x33u) << 2));  /* 2비트 묶음 교환 */
    b = (uint8_t)(((b & 0xAAu) >> 1) | ((b & 0x55u) << 1));  /* 1비트 교환     */
    return b;
}

/* 연습 5: 바이트 순서 뒤집기 (엔디언 변환) */
static uint32_t bswap32(uint32_t x) {
    return (x >> 24) | ((x >> 8) & 0x0000FF00u) | ((x << 8) & 0x00FF0000u) | (x << 24);
}

/* 연습 6: W1C(write-1-to-clear) 상태 레지스터 흉내
 * 하드웨어 규칙: SR에 쓴 값의 1인 비트만 지워진다. 0을 쓴 비트는 그대로. */
static uint32_t SR;
static void sr_write(uint32_t v) { SR &= ~v; }
static void clear_flag_wrong(uint32_t flag) { sr_write(SR | flag); }  /* SR |= flag 습관 → 전부 지움! */
static void clear_flag_right(uint32_t flag) { sr_write(flag); }        /* 지울 비트만 1로 쓴다 */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    CHECK(popcount32(0) == 0);
    CHECK(popcount32(0xFFFFFFFFu) == 32);
    CHECK(popcount32(0xA5u) == 4);

    CHECK(is_pow2(1) && is_pow2(64) && is_pow2(0x80000000u));
    CHECK(!is_pow2(0) && !is_pow2(6) && !is_pow2(0xFFFFFFFFu));

    CHECK(lowest_set(0x58u) == 0x08u);
    CHECK(lowest_set(0x80000000u) == 0x80000000u);
    CHECK(lowest_set(0) == 0);

    CHECK(swap_nibbles(0xA5) == 0x5A);
    CHECK(reverse8(0x01) == 0x80);
    CHECK(reverse8(0xB4) == 0x2D);

    CHECK(bswap32(0x11223344u) == 0x44332211u);
    CHECK(bswap32(bswap32(0xDEADBEEFu)) == 0xDEADBEEFu);

    SR = 0x7u;  clear_flag_right(0x2u);  CHECK(SR == 0x5u);
    SR = 0x7u;  clear_flag_wrong(0x2u);  CHECK(SR == 0x0u);   /* 다른 플래그(0x1, 0x4)까지 날아감 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
