/* C-B06 추가 연습 — 배열 vs 포인터 (한 번에 검증)
 * cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IS_ARRAY(x) (!__builtin_types_compatible_p(__typeof__(x), __typeof__(&(x)[0])))
#define MUST_BE_ARRAY(x) (0 * sizeof(struct { \
    _Static_assert(IS_ARRAY(x), "ARRAY_SIZE() needs an array, not a pointer"); char dummy_; }))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]) + MUST_BE_ARRAY(x))

/* ---- 연습 1: 7-세그먼트 lookup table + 경계 검사 ---- */
static const uint8_t seg7_lut[] = {          /* gfedcba, 0~F */
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
    0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71,
};
static uint8_t seg7_encode(unsigned d) {
    return d < ARRAY_SIZE(seg7_lut) ? seg7_lut[d] : 0x00;   /* 범위 밖 → 꺼짐 */
}

/* ---- 연습 2: 배열을 struct 로 감싸면 대입·반환이 된다 ---- */
typedef struct { uint8_t b[6]; } mac_addr_t;
static mac_addr_t mac_broadcast(void) {
    mac_addr_t m;
    memset(m.b, 0xFF, sizeof m.b);            /* m.b 는 배열 → sizeof 6 */
    return m;                                 /* 구조체라 값으로 반환 가능 */
}

/* ---- 연습 3: VLA 파라미터 (n×n 정방행렬) ---- */
static int32_t mat_trace(size_t n, const int32_t m[n][n]) {
    int32_t t = 0;
    for (size_t i = 0; i < n; i++) t += m[i][i];
    return t;
}
static void mat_transpose_sq(size_t n, int32_t m[n][n]) {
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++) {
            int32_t tmp = m[i][j]; m[i][j] = m[j][i]; m[j][i] = tmp;
        }
}

/* ---- 연습 4: 프레임버퍼 바이트 오프셋 ---- */
#define FB_W 320
#define FB_H 240
static uint16_t fb[FB_H][FB_W];               /* RGB565, 행 우선(row-major) */
static size_t fb_offset(size_t y, size_t x) { return (y * FB_W + x) * sizeof fb[0][0]; }
static void fb_fill_row(uint16_t (*row)[FB_W], uint16_t color) {
    for (size_t x = 0; x < ARRAY_SIZE(*row); x++) (*row)[x] = color;
}

/* ---- 연습 5: NUL 없는 고정 길이 필드 (펌웨어 이미지 헤더) ---- */
struct fw_header {
    char     magic[4] __attribute__((nonstring));  /* "FWv1" — NUL 없음 */
    uint32_t version;
};
static bool fw_magic_ok(const struct fw_header *h) {
    return memcmp(h->magic, "FWv1", sizeof h->magic) == 0;   /* strcmp 금지 */
}

/* ---- 연습 6: [begin, end) 포인터 범위 순회 ---- */
static int32_t sum_range(const int16_t *begin, const int16_t *end) {
    int32_t s = 0;
    for (const int16_t *p = begin; p != end; p++) s += *p;    /* end 는 역참조하지 않는다 */
    return s;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 1 */
    CHECK(ARRAY_SIZE(seg7_lut) == 16);
    CHECK(seg7_encode(0) == 0x3F && seg7_encode(8) == 0x7F && seg7_encode(0xF) == 0x71);
    CHECK(seg7_encode(16) == 0x00 && seg7_encode(1000) == 0x00);

    /* 2 */
    mac_addr_t a = mac_broadcast(), b;
    b = a;                                     /* 배열이었다면 컴파일 에러 */
    b.b[0] = 0x02;
    CHECK(sizeof(mac_addr_t) == 6 && a.b[0] == 0xFF && a.b[5] == 0xFF && b.b[0] == 0x02);

    /* 3 */
    int32_t m3[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
    int32_t m4[4][4] = {{1, 0, 0, 0}, {0, 2, 0, 0}, {0, 0, 3, 0}, {0, 0, 0, 4}};
    CHECK(mat_trace(3, m3) == 15 && mat_trace(4, m4) == 10);
    mat_transpose_sq(3, m3);
    CHECK(m3[0][1] == 4 && m3[1][0] == 2 && m3[2][0] == 3 && m3[0][2] == 7 && m3[1][1] == 5);

    /* 4 */
    CHECK(fb_offset(0, 1) == 2 && fb_offset(1, 0) == 640 && fb_offset(239, 319) == sizeof fb - 2);
    CHECK((size_t)((char *)&fb[10][7] - (char *)fb) == fb_offset(10, 7));
    fb_fill_row(&fb[5], 0xF800);
    CHECK(fb[5][0] == 0xF800 && fb[5][FB_W - 1] == 0xF800 && fb[4][FB_W - 1] == 0 && fb[6][0] == 0);

    /* 5 */
    struct fw_header ok = { "FWv1", 1 }, bad = { "FWv2", 1 };
    CHECK(sizeof ok.magic == 4 && fw_magic_ok(&ok) && !fw_magic_ok(&bad));

    /* 6 */
    static const int16_t samples[] = { 100, -50, 25, -75, 10 };
    CHECK(sum_range(samples, samples + ARRAY_SIZE(samples)) == 10);
    CHECK(sum_range(samples + 1, samples + 3) == -25 && sum_range(samples, samples) == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
