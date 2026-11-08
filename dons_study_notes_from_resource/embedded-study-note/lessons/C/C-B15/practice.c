/* C-B15 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 연습 1: 올림 나눗셈 — 괄호를 빼먹으면 DIV_ROUND_UP(9, 2 + 2) 가 틀린다 */
#define DIV_ROUND_UP(n, d)   (((n) + (d) - 1) / (d))
#define BITS_TO_BYTES(bits)  DIV_ROUND_UP((bits), 8)

/* 연습 2: ## 도 # 처럼 인자를 펼치지 않는다 → 한 단계 더 거쳐야 __LINE__ 이 숫자가 된다 */
#define STR(x)        #x
#define XSTR(x)       STR(x)
#define CAT(a, b)     a##b
#define XCAT(a, b)    CAT(a, b)

/* 연습 3: X-macro — 레지스터 목록을 한 곳에만 쓰고 enum·이름 표·오프셋 표를 자동 생성 */
#define UART_REG_LIST(X) \
    X(CR1, 0x00u)        \
    X(CR2, 0x04u)        \
    X(BRR, 0x08u)        \
    X(SR,  0x0Cu)

#define AS_ENUM(name, off)   REG_##name,
#define AS_NAME(name, off)   #name,
#define AS_OFF(name, off)    off,
enum uart_reg { UART_REG_LIST(AS_ENUM) REG_COUNT };
static const char *const uart_reg_name[] = { UART_REG_LIST(AS_NAME) };
static const uint32_t    uart_reg_off[]  = { UART_REG_LIST(AS_OFF) };

/* 연습 4: 표준 C11 _Generic 으로 타입별 inline 함수 고르기 (GNU 확장 없이 다중 평가 없음) */
static inline int      max_i(int a, int b)                { return a > b ? a : b; }
static inline unsigned max_u(unsigned a, unsigned b)      { return a > b ? a : b; }
static inline double   max_d(double a, double b)          { return a > b ? a : b; }
#define MAX_G(a, b) _Generic((a) + (b), int: max_i, unsigned: max_u, double: max_d)((a), (b))

/* 연습 5: 펌웨어용 ASSERT — 실패한 식(문자열)과 줄 번호를 기록. if/else 안에서도 안전 */
static const char *g_last_expr;
static int g_last_line, g_fail_count;
#define FW_ASSERT(expr) do { if (!(expr)) { g_fail_count++; g_last_expr = #expr; \
                                            g_last_line = __LINE__; } } while (0)

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 1 */
    CHECK(DIV_ROUND_UP(10, 4) == 3);
    CHECK(DIV_ROUND_UP(8, 4) == 2);
    CHECK(DIV_ROUND_UP(9, 2 + 2) == 3);        /* 괄호 없으면 (9+2+2-1)/2+2 = 8 */
    CHECK(BITS_TO_BYTES(13) == 2 && BITS_TO_BYTES(16) == 2);

    /* 2 */
    CHECK(strcmp(XSTR(CAT(reg_, __LINE__)), "reg___LINE__") == 0);
    CHECK(strcmp(XSTR(XCAT(reg_, __LINE__)), "reg_" XSTR(__LINE__)) == 0);

    /* 3 */
    CHECK(REG_COUNT == 4);
    CHECK(strcmp(uart_reg_name[REG_SR], "SR") == 0);
    CHECK(uart_reg_off[REG_BRR] == 0x08u);
    CHECK(sizeof uart_reg_name / sizeof uart_reg_name[0] == REG_COUNT);

    /* 4 */
    int i = 5;
    CHECK(MAX_G(i++, 3) == 5 && i == 6);        /* 한 번만 평가 */
    CHECK(MAX_G(3u, 7u) == 7u);
    CHECK(MAX_G(2.5, 1.0) == 2.5);

    /* 5 */
    volatile bool go = true;
    int v = 3;
    if (go) FW_ASSERT(v == 4);
    else    FW_ASSERT(v == 3);
    int line = __LINE__ - 2;
    CHECK(g_fail_count == 1 && strcmp(g_last_expr, "v == 4") == 0 && g_last_line == line);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
