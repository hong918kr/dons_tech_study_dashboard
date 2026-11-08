/* C-B03 Shift 연산의 함정: 폭 초과·음수·산술 시프트
 * 목표: 어떤 n 이 들어와도 UB/구현 정의 동작 없이 "수학적으로 맞는" 값을 주는 시프트 헬퍼.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ---------------- Part A: 폭을 넘어도 안전한 논리 시프트 ---------------- */
/* n 은 0 이상 아무 값. 비트가 모두 밀려나면 0 (x << 32 를 그대로 쓰면 UB!) */
static inline uint32_t shl32(uint32_t x, unsigned n) {
    return (n >= 32u) ? 0u : (x << n);
}
static inline uint32_t shr32(uint32_t x, unsigned n) {
    return (n >= 32u) ? 0u : (x >> n);
}

/* ---------------- Part B: 산술 오른쪽 시프트와 회전 ---------------- */
/* floor(x / 2^n). 음수 x 에 x >> n (구현 정의) 를 쓰지 말 것.
 * n >= 32 이면 부호만 남는다: 음수 → -1, 0 이상 → 0 */
static inline int32_t sar32(int32_t x, unsigned n) {
    if (n >= 32u) n = 31u;                       /* 31칸이면 이미 부호만 남는다 */
    return (x < 0) ? ~(~x >> n) : (x >> n);      /* ~x 는 0 이상 → >> 가 정의됨 */
}
/* 왼쪽 회전. n 은 32로 나눈 나머지만 의미 있음. n == 0 에서 x >> 32 가 나오면 UB! */
static inline uint32_t rotl32(uint32_t x, unsigned n) {
    n &= 31u;
    return (x << n) | (x >> ((32u - n) & 31u)); /* n == 0 → 두 항 모두 x */
}

/* ---------------- Part C: CAN 신호 디코더 (Intel / little-endian) ---------------- */
/* 주어짐: uint32_t → int32_t 변환을 구현 정의 없이 (C11 6.3.1.3p3 회피) */
static inline int32_t u32_to_s32(uint32_t u) {
    return (u <= (uint32_t)INT32_MAX) ? (int32_t)u
                                      : (int32_t)(u - 0x80000000u) + INT32_MIN;
}

/* 8바이트 payload → uint64_t. d[0] 이 최하위 바이트. d[i] << (8*i) 의 함정 주의 */
static uint64_t load_le64(const uint8_t d[8]) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8u; i++)
        v |= (uint64_t)d[i] << (8u * i);         /* 캐스트 먼저: int 도 uint32_t 도 모자람 */
    return v;
}
/* start 비트부터 len 비트 (1 <= len <= 32, start + len <= 64) 를 부호 없이 꺼낸다 */
static uint32_t can_get_raw(const uint8_t d[8], unsigned start, unsigned len) {
    uint32_t mask = shr32(0xFFFFFFFFu, 32u - len); /* len == 32 → 0xFFFFFFFF */
    return (uint32_t)(load_le64(d) >> start) & mask;
}
/* 같은 필드를 len 비트 2의 보수로 해석해 부호 확장한다 (len == 32, len == 1 포함) */
static int32_t can_get_signed(const uint8_t d[8], unsigned start, unsigned len) {
    unsigned k = 32u - len;                       /* 필드의 부호 비트를 bit 31 로 */
    return sar32(u32_to_s32(shl32(can_get_raw(d, start, len), k)), k);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* Part A — 경계값: 0, 31, 32, 32 초과 */
    CHECK(shl32(0x12345678u, 0) == 0x12345678u);
    CHECK(shl32(1u, 31) == 0x80000000u);
    CHECK(shl32(0xFFFFFFFFu, 32) == 0u);
    CHECK(shl32(0xFFFFFFFFu, 33) == 0u);
    CHECK(shl32(0xFFFFFFFFu, 256) == 0u);          /* ARM LSL 은 하위 8비트만 봄 → 256 이면 0칸 */
    CHECK(shr32(0x80000000u, 31) == 1u);
    CHECK(shr32(0x12345678u, 0) == 0x12345678u);
    CHECK(shr32(0xFFFFFFFFu, 32) == 0u);
    CHECK(shr32(0xFFFFFFFFu, 0xFFFFFFFFu) == 0u);

    /* Part B — sar32: 음수, floor 반올림, n >= 32 */
    CHECK(sar32(-1, 1) == -1);
    CHECK(sar32(-7, 1) == -4);                     /* -7 / 2 == -3 과 다르다! */
    CHECK(sar32(7, 1) == 3);
    CHECK(sar32(-8, 0) == -8);
    CHECK(sar32(INT32_MIN, 31) == -1);
    CHECK(sar32(INT32_MAX, 30) == 1);
    CHECK(sar32(INT32_MIN, 32) == -1);
    CHECK(sar32(-5, 100) == -1);
    CHECK(sar32(12345, 32) == 0);
    /* rotl32 */
    CHECK(rotl32(0x80000001u, 1) == 0x00000003u);
    CHECK(rotl32(0x12345678u, 0) == 0x12345678u);
    CHECK(rotl32(0x12345678u, 32) == 0x12345678u);
    CHECK(rotl32(0x12345678u, 36) == 0x23456781u);

    /* Part C — 가상의 모터 제어기 CAN 프레임 */
    const uint8_t f[8] = { 0x34, 0x12, 0x18, 0xFC, 0xEF, 0xBE, 0xAD, 0xDE };
    CHECK(load_le64(f) == 0xDEADBEEFFC181234ull);
    CHECK(can_get_raw(f, 0, 16) == 0x1234u);       /* 회전수 (unsigned 16)   */
    CHECK(can_get_raw(f, 4, 12) == 0x123u);
    CHECK(can_get_signed(f, 16, 16) == -1000);     /* 토크 0xFC18 (signed 16) */
    CHECK(can_get_signed(f, 20, 12) == -63);       /* 12비트 0xFC1           */
    CHECK(can_get_raw(f, 0, 32) == 0xFC181234u);   /* len == 32 경계        */
    CHECK(can_get_raw(f, 32, 32) == 0xDEADBEEFu);
    CHECK(can_get_signed(f, 32, 32) == -559038737);
    CHECK(can_get_raw(f, 63, 1) == 1u);            /* 1비트 플래그 (최상위)   */
    CHECK(can_get_signed(f, 63, 1) == -1);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
