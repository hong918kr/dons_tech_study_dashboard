/* C-B02 고정 폭 정수와 정수 승격의 함정 */
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---------------- Part A: 승격 때문에 틀리는 계산 ---------------- */
/* A1. 8비트 체크섬: 바이트 합의 하위 8비트 (모듈로 256) */
static uint8_t checksum8(const uint8_t *buf, size_t n) {
    uint8_t sum = 0;
    for (size_t i = 0; i < n; i++)
        sum = (uint8_t)(sum + buf[i]);          /* int로 계산 → 명시적으로 잘라 저장 */
    return sum;
}

/* A2. 1의 보수 체크 바이트가 맞는지: ~a == b ?  (a, b 는 uint8_t) */
static bool is_complement(uint8_t a, uint8_t b) {
    return (uint8_t)~a == b;                    /* ~a 는 int: 0xFFFFFF5A 같은 값 */
}

/* A3. 16비트 × 16비트 → 32비트 곱 (int 오버플로 UB 회피) */
static uint32_t mul_u16(uint16_t a, uint16_t b) {
    return (uint32_t)a * b;                     /* 한쪽을 먼저 uint32_t 로 */
}

/* A4. 리틀 엔디언 4바이트 → uint32_t */
static uint32_t load_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---------------- Part B: signed / unsigned 비교 ---------------- */
/* B1. x 가 [0, limit) 범위인가? x 는 음수일 수 있다 */
static bool in_range(int32_t x, uint32_t limit) {
    return x >= 0 && (uint32_t)x < limit;       /* 음수를 먼저 걸러야 한다 */
}

/* B2. 버퍼에 len 바이트를 더 쓸 수 있나? (used <= cap 가정) */
static bool has_room(size_t used, size_t cap, size_t len) {
    return len <= cap - used;                   /* used + len 은 오버플로 가능 */
}

/* ---------------- Part C: 랩어라운드 안전 타이머 & 포화 연산 ---------------- */
/* C1. 32비트 틱 카운터가 넘쳐도 맞는 타임아웃 검사 */
static bool timer_elapsed(uint32_t now, uint32_t start, uint32_t timeout) {
    return (uint32_t)(now - start) >= timeout;  /* 부호 없는 뺄셈은 모듈로 2^32 */
}

/* C2. int16_t 포화 덧셈 (오디오/센서 샘플) */
static int16_t sat_add_i16(int16_t a, int16_t b) {
    int32_t s = (int32_t)a + b;                 /* 넓은 타입에서 계산 */
    if (s > INT16_MAX) return INT16_MAX;
    if (s < INT16_MIN) return INT16_MIN;
    return (int16_t)s;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    const uint8_t pkt[] = {0xF0, 0x20, 0x01};
    CHECK(checksum8(pkt, 3) == 0x11);
    CHECK(checksum8(pkt, 0) == 0x00);

    CHECK(is_complement(0xA5, 0x5A) == true);
    CHECK(is_complement(0x00, 0xFF) == true);
    CHECK(is_complement(0x12, 0x34) == false);

    CHECK(mul_u16(65535, 65535) == 4294836225u);
    CHECK(mul_u16(300, 300) == 90000u);

    const uint8_t le[] = {0x78, 0x56, 0x34, 0x12, 0xEF, 0xBE, 0xAD, 0xDE};
    CHECK(load_le32(le) == 0x12345678u);
    CHECK(load_le32(le + 4) == 0xDEADBEEFu);

    CHECK(in_range(5, 10) == true);
    CHECK(in_range(-1, 10) == false);
    CHECK(in_range(10, 10) == false);
    CHECK(in_range(INT32_MIN, UINT32_MAX) == false);

    CHECK(has_room(10, 16, 6) == true);
    CHECK(has_room(10, 16, 7) == false);
    CHECK(has_room(10, 16, SIZE_MAX) == false);

    CHECK(timer_elapsed(1500, 1000, 500) == true);
    CHECK(timer_elapsed(1499, 1000, 500) == false);
    CHECK(timer_elapsed(100, 0xFFFFFF00u, 300) == true);    /* 0xFFFFFF00 → 100 : 356 틱 */
    CHECK(timer_elapsed(10, 0xFFFFFF00u, 300) == false);    /* 266 틱 */

    CHECK(sat_add_i16(30000, 10000) == INT16_MAX);
    CHECK(sat_add_i16(-30000, -10000) == INT16_MIN);
    CHECK(sat_add_i16(100, -300) == -200);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
