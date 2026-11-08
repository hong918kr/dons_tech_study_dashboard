/* C-A08 추가 연습 — 한 번에 검증
 *   cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BUILD_BUG_ON_ZERO(cond) \
    (0 * sizeof(struct { _Static_assert(!(cond), "BUILD_BUG_ON_ZERO(" #cond ")"); char c_; }))

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------- 연습 1: C11 이전의 정적 단언 (typedef + 음수 배열) ---------- */
#define CT_ASSERT(cond, tag)   typedef char ct_assert_##tag[(cond) ? 1 : -1]
#define CT_CAT_(a, b)          a##b
#define CT_CAT(a, b)           CT_CAT_(a, b)                 /* 인자를 먼저 펼친 뒤 붙인다 */
#define CT_ASSERT_L(cond)      typedef char CT_CAT(ct_assert_line_, __LINE__)[(cond) ? 1 : -1]
#define STR_(x)                #x
#define STR(x)                 STR_(x)

CT_ASSERT(sizeof(uint32_t) == 4, u32_is_4);
CT_ASSERT_L(sizeof(uint16_t) == 2);
CT_ASSERT_L(sizeof(uint8_t) == 1);                           /* 다른 줄 → 다른 이름 → 충돌 없음 */

/* ---------- 연습 2: 입력을 검사하는 GENMASK(h, l) ---------- */
#define GENMASK(h, l) \
    ((uint32_t)(BUILD_BUG_ON_ZERO((l) > (h) || (h) > 31) + ((~0u >> (31u - (h))) & (~0u << (l)))))

/* ---------- 연습 3: 플래시 설정 레코드 — 8바이트 배수로 채우기 + 버전 트립와이어 ---------- */
#define ROUND_UP(x, a)   (((x) + (a) - 1u) / (a) * (a))
#define FLASH_DW         8u                                  /* 더블워드 단위 프로그램 (STM32L4/G4 류) */
#define FLASH_PAGE       2048u
#define CFG_VERSION      2u
#define CFG_BODY_SIZE_V2 12u                                 /* 버전 2 의 본문 크기 — 레이아웃이 바뀌면 버전도 올린다 */

typedef struct {
    uint16_t magic;
    uint16_t version;
    uint32_t baud;
    uint8_t  addr;
    uint8_t  mode;
    uint16_t crc16;
} cfg_body_t;

typedef union {                                              /* pad[0] 이 될 수 있는 패딩 배열 대신 union */
    cfg_body_t b;
    uint8_t    raw[ROUND_UP(sizeof(cfg_body_t), FLASH_DW)];
} cfg_rec_t;

_Static_assert(sizeof(cfg_rec_t) % FLASH_DW == 0u, "cfg record must be a whole number of flash double-words");
_Static_assert(sizeof(cfg_rec_t) <= FLASH_PAGE, "cfg record must fit one flash page");
_Static_assert(sizeof(cfg_body_t) == CFG_BODY_SIZE_V2 && CFG_VERSION == 2u,
               "cfg_body_t layout changed: bump CFG_VERSION and add a migration");

/* ---------- 연습 4: 디스패치 표의 구멍 ---------- */
typedef enum { CMD_PING, CMD_READ, CMD_WRITE, CMD_RESET, CMD_COUNT } cmd_t;
typedef int (*cmd_fn)(int);
static int h_ping(int x)  { return x + 1; }
static int h_read(int x)  { return x * 2; }
static int h_write(int x) { return x - 1; }
static int h_reset(int x) { (void)x; return 0; }

static const cmd_fn table_ok[] = {
    [CMD_PING] = h_ping, [CMD_READ] = h_read, [CMD_WRITE] = h_write, [CMD_RESET] = h_reset,
};
_Static_assert(ARRAY_SIZE(table_ok) == CMD_COUNT, "table_ok out of sync with cmd_t");

static const cmd_fn table_sized[CMD_COUNT] = {               /* 크기를 CMD_COUNT 로 적음 + WRITE 빠짐 */
    [CMD_PING] = h_ping, [CMD_READ] = h_read, [CMD_RESET] = h_reset,
};
_Static_assert(ARRAY_SIZE(table_sized) == CMD_COUNT, "always true — this check is vacuous");

static int first_hole(const cmd_fn *t, size_t n) {           /* 첫 NULL 의 인덱스, 없으면 -1 */
    for (size_t i = 0; i < n; i++)
        if (t[i] == NULL) return (int)i;
    return -1;
}
static int dispatch(const cmd_fn *t, size_t n, unsigned op, int arg, int *out) {
    if (op >= n || t[op] == NULL) return -1;
    *out = t[op](arg);
    return 0;
}

int main(void) {
    /* 연습 1 */
    CHECK(sizeof(ct_assert_u32_is_4) == 1u);
    CHECK(strcmp(STR(CT_CAT_(L, __LINE__)), "L__LINE__") == 0);   /* 한 단계: __LINE__ 이 안 펼쳐진다 */
    {
        char want[16];
        snprintf(want, sizeof want, "L%d", __LINE__); const char *got = STR(CT_CAT(L, __LINE__));
        CHECK(strcmp(got, want) == 0);                               /* 두 단계: L<줄번호> */
    }

    /* 연습 2 */
    CHECK(GENMASK(5, 3) == 0x38u && GENMASK(15, 0) == 0xFFFFu && GENMASK(0, 0) == 1u);
    CHECK(GENMASK(31, 0) == 0xFFFFFFFFu && GENMASK(31, 31) == 0x80000000u && GENMASK(31, 16) == 0xFFFF0000u);
    {
        enum { M = GENMASK(7, 4) };                                  /* 상수식이다 */
        CHECK(M == 0xF0);
    }

    /* 연습 3 */
    CHECK(ROUND_UP(12u, 8u) == 16u && ROUND_UP(16u, 8u) == 16u && ROUND_UP(0u, 8u) == 0u && ROUND_UP(1u, 8u) == 8u);
    CHECK(sizeof(cfg_body_t) == 12u && sizeof(cfg_rec_t) == 16u);
    {
        cfg_rec_t r;
        memset(&r, 0xFF, sizeof r);                                  /* 지워진 플래시 = 0xFF */
        r.b = (cfg_body_t){ .magic = 0xC0F1, .version = CFG_VERSION, .baud = 115200u, .addr = 0x42 };
        CHECK(r.raw[0] == 0xF1 && r.raw[1] == 0xC0 && r.raw[4] == 0x00 && r.raw[5] == 0xC2 && r.raw[8] == 0x42);
    }

    /* 연습 4 */
    {
        int out = 0;
        CHECK(first_hole(table_ok, ARRAY_SIZE(table_ok)) == -1);
        CHECK(first_hole(table_sized, ARRAY_SIZE(table_sized)) == CMD_WRITE);
        CHECK(dispatch(table_ok, ARRAY_SIZE(table_ok), CMD_READ, 21, &out) == 0 && out == 42);
        CHECK(dispatch(table_sized, ARRAY_SIZE(table_sized), CMD_WRITE, 5, &out) == -1);
        CHECK(dispatch(table_ok, ARRAY_SIZE(table_ok), 99u, 5, &out) == -1);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
