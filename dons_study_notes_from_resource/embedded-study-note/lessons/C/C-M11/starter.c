/* C-M11 매크로 vs inline 함수 vs _Generic
 * C-B15 의 함수형 매크로(MAX / ABS / SQUARE / ARRAY_SIZE / REG_FIELD_WRITE)를
 *   Part A: 타입이 정해진 static inline 함수로,
 *   Part B: C11 _Generic 타입별 디스패치로,
 *   Part C: "매크로만 할 수 있는 일(#, __FILE__/__LINE__, ##, 상수식)만 매크로 + 나머지는 inline 함수" 로
 * 다시 쓴다. TODO 표시된 함수/매크로만 채워서 전부 PASS 로 만든다.
 */
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(int) == 4, "이 파일은 int = 32비트를 가정한다 (Cortex-M, arm64, x86-64 모두 해당)");

/* ---------------- C-B15 의 매크로 (비교용 — 고치지 않는다) ---------------- */
#define SQUARE(x)   ((x) * (x))
#define MAX(a, b)   ((a) > (b) ? (a) : (b))

/* 가짜 UART 수신 FIFO: DR 을 읽을 때마다 한 바이트가 빠진다 (읽기에 부작용이 있는 레지스터) */
static const uint32_t rx_bytes[4] = { 7, 9, 2, 11 };
static unsigned rx_pos;
static uint32_t uart_read_dr(void) { return rx_bytes[rx_pos++ % 4u]; }

/* ---------------- Part A: static inline — 타입이 정해진 함수 ---------------- */
static inline int32_t max_i32(int32_t a, int32_t b) {
    (void)b; return a;                                         /* TODO */
}
static inline uint32_t max_u32(uint32_t a, uint32_t b) {
    (void)b; return a;                                         /* TODO */
}
/* NaN 규칙은 fmaxf 와 같게: 한쪽만 NaN 이면 다른 쪽을 돌려준다 (인자 순서와 무관하게) */
static inline float max_f(float a, float b) {
    return a > b ? a : b;                                      /* TODO: 지금은 MAX 매크로와 같은 NaN 동작 */
}
/* |x| 를 부호 없는 타입으로 — INT32_MIN 도 2147483648u (ABS(INT32_MIN) 은 오버플로 UB) */
static inline uint32_t uabs_i32(int32_t x) {
    return (uint32_t)x;                                        /* TODO */
}
/* x*x 를 넓은 타입으로 — square_i32(50000) 도 정확히 (SQUARE(50000) 은 int 오버플로 UB) */
static inline int64_t square_i32(int32_t x) {
    (void)x; return 0;                                         /* TODO */
}

/* 이미 채워진 64비트 / double 버전 (Part B 디스패치 표에서 쓴다) */
static inline int64_t  max_i64(int64_t a, int64_t b)   { return a > b ? a : b; }
static inline uint64_t max_u64(uint64_t a, uint64_t b) { return a > b ? a : b; }
static inline double   max_d(double a, double b)       { return isnan(a) ? b : isnan(b) ? a : (a > b ? a : b); }
static inline uint64_t uabs_i64(int64_t x)             { return x < 0 ? 0u - (uint64_t)x : (uint64_t)x; }

/* ---------------- Part B: _Generic — 타입별 함수 고르기 ---------------- */
/* long 은 플랫폼마다 32/64비트다 (Cortex-M: 4바이트, macOS/Linux 64비트: 8바이트).
 * 어느 함수로 보낼지는 전처리기가 정한다 — 매크로가 여전히 필요한 곳 하나. */
#if LONG_MAX == INT64_MAX
#define MAX_LONG_FN   max_i64
#define MAX_ULONG_FN  max_u64
#define UABS_LONG_FN  uabs_i64
#else
#define MAX_LONG_FN   max_i32
#define MAX_ULONG_FN  max_u32
#define UABS_LONG_FN  uabs_i32
#endif

/* (a) + (b) 의 타입(정수 승격 + 보통 산술 변환 뒤의 공통 타입)으로 함수를 고르고, 인자는 호출에서 한 번만 평가.
 * default 를 일부러 두지 않는다 → 표에 없는 타입(long double, 구조체...)은 컴파일 에러. */
#define max_of(a, b) max_d((a), (b))            /* TODO: _Generic 으로 타입별 함수 선택 (지금은 전부 double) */

/* 안전한 abs: 정수는 부호 없는 결과(INT_MIN 안전), 실수는 fabsf/fabs.
 * (x) + 0 으로 int8_t/int16_t 를 int 로 승격시켜 표를 줄인다. unsigned 는 일부러 뺐다 (abs 할 이유가 없다 → 에러). */
#define abs_of(x) uabs_i64((int64_t)(x))        /* TODO: _Generic */

/* 식의 타입 이름 — 제어식 (x) 는 평가되지 않고, lvalue 변환(최상위 한정자 제거·배열/함수 decay)만 거친다 */
#define type_name(x) ((void)sizeof(x), "?")     /* TODO: _Generic */

/* ---------------- Part C: 매크로가 꼭 필요한 곳만 매크로로 ---------------- */
/* C-1. 변수 로그: 이름(#x)과 호출 위치(__FILE__, __LINE__)는 매크로만 얻을 수 있다. 값 포맷은 inline 함수가. */
#define LOG_CAP 512u                       /* 배열 크기 = 상수식 → static const 로는 안 된다 (C, C++ 아님) */
static char   g_log[LOG_CAP];
static size_t g_log_len;
static void log_reset(void) { g_log_len = 0; g_log[0] = '\0'; }
static void log_put(const char *file, int line, const char *name, const char *val) {
    size_t room = LOG_CAP - g_log_len;
    int n = snprintf(g_log + g_log_len, room, "%s:%d %s=%s\n", file, line, name, val);
    if (n > 0) g_log_len += ((size_t)n < room) ? (size_t)n : room - 1u;
}
static inline void log_i64(const char *f, int l, const char *n, long long v) {
    char b[24]; snprintf(b, sizeof b, "%lld", v); log_put(f, l, n, b);
}
static inline void log_u64(const char *f, int l, const char *n, unsigned long long v) {
    char b[24]; snprintf(b, sizeof b, "%llu", v); log_put(f, l, n, b);
}
static inline void log_dbl(const char *f, int l, const char *n, double v) {
    char b[32]; snprintf(b, sizeof b, "%g", v); log_put(f, l, n, b);
}
static inline void log_str(const char *f, int l, const char *n, const char *v) {
    char b[48]; snprintf(b, sizeof b, "\"%s\"", v); log_put(f, l, n, b);
}
/* LOG_VAR(x): "file:line x=값" 한 줄. x 는 정확히 한 번 평가. (x) + 0 → 작은 정수는 int 로 승격, 배열은 포인터로. */
#define LOG_VAR(x) ((void)sizeof(x), log_put("?", 0, "?", "?"))   /* TODO */

/* C-2. 레지스터 필드: 이름 조립(##)만 매크로, read-modify-write 로직은 inline 함수 (C-B15 Part C 의 재작성) */
typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t BRR;
} UART_TypeDef;
static UART_TypeDef fake_uart1;                    /* 실제 칩이라면 (UART_TypeDef *)0x40011000 */
#define UART1 (&fake_uart1)
#define UART_CR1_UE_Pos     0u
#define UART_CR1_UE_Msk     (1u << UART_CR1_UE_Pos)
#define UART_CR1_TE_Pos     3u
#define UART_CR1_TE_Msk     (1u << UART_CR1_TE_Pos)
#define UART_CR2_STOP_Pos   12u
#define UART_CR2_STOP_Msk   (3u << UART_CR2_STOP_Pos)
#define REG_POS(P, R, F)    P##_##R##_##F##_Pos
#define REG_MSK(P, R, F)    P##_##R##_##F##_Msk

static inline void reg_field_write(volatile uint32_t *reg, uint32_t msk, uint32_t pos, uint32_t v) {
    (void)reg; (void)msk; (void)pos; (void)v;      /* TODO: 읽기 1번, 쓰기 1번 */
}
static inline uint32_t reg_field_read(const volatile uint32_t *reg, uint32_t msk, uint32_t pos) {
    return (*reg & msk) >> pos;
}
/* 함수 호출 "식" 이라 do-while(0) 없이도 if/else 안에서 한 문장처럼 동작한다 */
#define REG_FIELD_WRITE(inst, P, R, F, v) reg_field_write(&(inst)->R, REG_MSK(P, R, F), REG_POS(P, R, F), (v))
#define REG_FIELD_READ(inst, P, R, F)     reg_field_read(&(inst)->R, REG_MSK(P, R, F), REG_POS(P, R, F))

/* C-3. ARRAY_SIZE 는 함수가 될 수 없다 (배열을 넘기는 순간 포인터로 decay, 결과도 상수식이어야 함).
 * 대신 포인터를 넘기면 컴파일 에러가 나게 만든다. IS_ARRAY 는 정수 상수식이어야 한다. */
#define IS_ARRAY(a) (0 * sizeof(a) + 1)         /* TODO: 지금은 무엇이든 "배열" */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]) \
    + 0 * sizeof(struct { _Static_assert(IS_ARRAY(a), "ARRAY_SIZE: argument is not an array"); char c_; }))

static const uint32_t baud_table[] = { 9600u, 19200u, 38400u, 57600u, 115200u };
_Static_assert(ARRAY_SIZE(baud_table) == 5, "baud_table 항목 수");     /* 상수식이라 정적 단언에 쓸 수 있다 */
static uint8_t baud_hits[ARRAY_SIZE(baud_table)];                      /* 파일 범위 배열 크기에도 */
static int baud_index(uint32_t baud) {
    for (size_t k = 0; k < ARRAY_SIZE(baud_table); k++)
        if (baud_table[k] == baud) { baud_hits[k]++; return (int)k; }
    return -1;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define IS_TYPE(e, T)  _Generic((e), T: 1, default: 0)
#define TN_IS(x, s)    (strcmp(type_name(x), (s)) == 0)

/* LOG_VAR 기대값: "<__FILE__>:<line> <name>=<val>\n" */
static bool log_is(int line, const char *name, const char *val) {
    char exp[128];
    snprintf(exp, sizeof exp, "%s:%d %s=%s\n", __FILE__, line, name, val);
    return strcmp(g_log, exp) == 0;
}

int main(void) {
    volatile bool go = false;      /* 런타임 조건 (컴파일러가 미리 접지 못하게) */
    int r;

    /* Part A — static inline: 타입이 정해진 함수 */
    CHECK(max_i32(-1, 1) == 1 && max_i32(INT32_MIN, INT32_MAX) == INT32_MAX);
    CHECK(max_u32(0xFFFFFFFFu, 1u) == 0xFFFFFFFFu && max_u32(3u, 9u) == 9u);
    int i = 5, j = 3;
    r = max_i32(i++, j);        CHECK(r == 5 && i == 6);          /* 인자는 호출 전에 한 번만 평가 */
    rx_pos = 0;
    CHECK(max_u32(uart_read_dr(), 4u) == 7u && rx_pos == 1u);       /* FIFO 에서 1바이트만 */
    rx_pos = 0;
    uint32_t mm = MAX(uart_read_dr(), 4u);
    CHECK(mm == 9u && rx_pos == 2u);                                /* 비교용: 매크로는 2번 읽는다 (C-B15) */
    CHECK(max_f(NAN, 1.0f) == 1.0f && max_f(1.0f, NAN) == 1.0f && max_f(-2.0f, -3.0f) == -2.0f);
    float mf = MAX(1.0f, NAN);
    CHECK(isnan(mf));                                               /* 비교용: 매크로는 순서에 따라 NaN 이 샌다 */
    CHECK(uabs_i32(-5) == 5u && uabs_i32(INT32_MIN) == 2147483648u && uabs_i32(INT32_MAX) == 2147483647u);
    CHECK(square_i32(-3) == 9 && square_i32(50000) == 2500000000LL);
    CHECK(square_i32(INT32_MIN) == 4611686018427387904LL);          /* 2^62 */
    CHECK(max_i64(-1, 0) == 0 && max_u64(1u, UINT64_MAX) == UINT64_MAX && max_d(NAN, 1.0) == 1.0);
    CHECK(uabs_i64(INT64_MIN) == 9223372036854775808ull);

    /* Part B — max_of: 타입별 함수 선택 + 결과 타입 보존 */
    int si = -1, sj = 1;
    unsigned ua = 3u, ub = 9u;
    float fa = 2.5f, fb = -1.0f;
    double da = -3.25, db = -7.5;
    uint64_t big = 0x100000000ull;
    uint8_t u8a = 200u, u8b = 100u;
    CHECK(max_of(si, sj) == 1 && IS_TYPE(max_of(si, sj), int));
    CHECK(max_of(ua, ub) == 9u && IS_TYPE(max_of(ua, ub), unsigned int));
    CHECK(max_of(fa, fb) == 2.5f && IS_TYPE(max_of(fa, fb), float));
    CHECK(max_of(da, db) == -3.25 && IS_TYPE(max_of(da, db), double));
    CHECK(max_of(fa, da) == 2.5 && IS_TYPE(max_of(fa, da), double));         /* float + double → double */
    CHECK(max_of(big, ua) == big && IS_TYPE(max_of(big, ua), uint64_t));
    CHECK(max_of(u8a, u8b) == 200 && IS_TYPE(max_of(u8a, u8b), int));        /* uint8_t + uint8_t → int (승격) */
    i = 5;
    r = max_of(i++, j);         CHECK(r == 5 && i == 6);
    rx_pos = 0;
    CHECK(max_of(uart_read_dr(), 4u) == 7u && rx_pos == 1u);
    CHECK(max_of(NAN, 1.0f) == 1.0f);
    CHECK(max_of(si, ua) == UINT32_MAX);    /* 같은 함정: int + unsigned → unsigned, -1 이 4294967295 (C-B02) */

    /* Part B — abs_of: 안전한 abs */
    int32_t imin = INT32_MIN;
    int8_t  s8 = -128;
    int64_t i64min = INT64_MIN;
    double  dneg = -2.5;
    CHECK(abs_of(imin) == 2147483648u && IS_TYPE(abs_of(imin), uint32_t));
    CHECK(abs_of(s8) == 128u && IS_TYPE(abs_of(s8), uint32_t));              /* int8_t + 0 → int */
    CHECK(abs_of(i64min) == 9223372036854775808ull);
    CHECK(abs_of(dneg) == 2.5 && IS_TYPE(abs_of(dneg), double));

    /* Part B — type_name: _Generic 의 함정들 */
    char c = 'A';
    signed char sc = 1;
    unsigned char uc = 1;
    const int ci = 7;
    volatile uint32_t vreg = 0;
    int arr4[4] = { 0 };
    const char *cs = "x";
    bool bb = true;
    struct pt { int x, y; } pt = { 1, 2 };
    CHECK(TN_IS('A', "int"));                       /* 문자 상수는 C 에서 int (C++ 은 char) */
    CHECK(TN_IS(c, "char"));
    CHECK(TN_IS(sc, "signed char") && TN_IS(uc, "unsigned char"));   /* char 와 셋 다 다른 타입 */
    CHECK(TN_IS(ci, "int"));                        /* lvalue 변환: 최상위 const 가 떨어진다 */
    CHECK(TN_IS(vreg, "unsigned int"));             /* volatile 도 떨어진다 */
    CHECK(TN_IS(arr4, "int *"));                    /* 배열 → 포인터 decay */
    CHECK(TN_IS("abc", "char *"));                  /* 문자열 리터럴은 C 에서 char[4] → char * */
    CHECK(TN_IS(cs, "const char *"));               /* 가리키는 쪽 const 는 남는다 */
    CHECK(TN_IS(true, "int") && TN_IS(bb, "_Bool"));/* C11 의 true 는 매크로 1 */
    CHECK(TN_IS(uc + uc, "int"));                   /* 정수 승격 */
    CHECK(TN_IS(1.0f * 2, "float") && TN_IS(1.0f * 2.0, "double"));
    CHECK(TN_IS(pt, "other"));                      /* default */

    /* Part C-1 — LOG_VAR: #x + __FILE__/__LINE__ (매크로) + _Generic (포맷 선택) */
    log_reset();
    log_i64("f.c", 1, "a", -1); log_u64("f.c", 2, "b", 2u); log_dbl("f.c", 3, "c", 0.5); log_str("f.c", 4, "d", "hi");
    CHECK(strcmp(g_log, "f.c:1 a=-1\nf.c:2 b=2\nf.c:3 c=0.5\nf.c:4 d=\"hi\"\n") == 0);   /* 포맷 함수는 이미 맞다 */
    uint8_t rx_cnt = 5;
    int32_t temp = -40;
    uint32_t status = 0xFFFFFFFFu;
    const char *mode = "IDLE";
    char dev[] = "uart1";
    float gain = 1.5f;
    int ln;
    log_reset(); ln = __LINE__; LOG_VAR(rx_cnt);    CHECK(log_is(ln, "rx_cnt", "5"));
    log_reset(); ln = __LINE__; LOG_VAR(temp);      CHECK(log_is(ln, "temp", "-40"));
    log_reset(); ln = __LINE__; LOG_VAR(status);    CHECK(log_is(ln, "status", "4294967295"));   /* -1 이 아니다 */
    log_reset(); ln = __LINE__; LOG_VAR(mode);      CHECK(log_is(ln, "mode", "\"IDLE\""));
    log_reset(); ln = __LINE__; LOG_VAR(dev);       CHECK(log_is(ln, "dev", "\"uart1\""));     /* char[6] → char * */
    log_reset(); ln = __LINE__; LOG_VAR(gain);      CHECK(log_is(ln, "gain", "1.5"));
    rx_pos = 0;
    log_reset(); ln = __LINE__; LOG_VAR(uart_read_dr());
    CHECK(log_is(ln, "uart_read_dr()", "7") && rx_pos == 1u);                   /* 한 번만 읽는다 */

    /* Part C-2 — REG_FIELD_WRITE: ## 는 매크로, RMW 는 inline */
    UART1->CR2 = 0x00003005u;                                  /* STOP = 3, 다른 비트 0x5 */
    REG_FIELD_WRITE(UART1, UART, CR2, STOP, 2u);
    CHECK(UART1->CR2 == 0x00002005u);                          /* STOP 만 바뀌고 0x5 는 그대로 */
    CHECK(REG_FIELD_READ(UART1, UART, CR2, STOP) == 2u);
    REG_FIELD_WRITE(UART1, UART, CR2, STOP, 0xFFu);
    CHECK(UART1->CR2 == 0x00003005u);                          /* 필드보다 큰 값은 마스크로 잘린다 */
    UART1->CR1 = 0u;
    if (go) REG_FIELD_WRITE(UART1, UART, CR1, TE, 1u);         /* do-while(0) 없이도 if/else OK */
    else    REG_FIELD_WRITE(UART1, UART, CR1, UE, 1u);
    CHECK(UART1->CR1 == UART_CR1_UE_Msk);
    rx_pos = 0;
    REG_FIELD_WRITE(UART1, UART, CR2, STOP, uart_read_dr());   /* 7 → 하위 2비트 3 */
    CHECK(rx_pos == 1u && REG_FIELD_READ(UART1, UART, CR2, STOP) == 3u);

    /* Part C-3 — IS_ARRAY / ARRAY_SIZE */
    int *p4 = arr4;
    int *const pc4 = arr4;
    volatile uint32_t regs3[3];
    int m34[3][4];
    CHECK(IS_ARRAY(arr4) == 1 && IS_ARRAY(regs3) == 1 && IS_ARRAY(m34) == 1 && IS_ARRAY("abc") == 1);
    CHECK(IS_ARRAY(p4) == 0 && IS_ARRAY(pc4) == 0 && IS_ARRAY(cs) == 0);
    CHECK(ARRAY_SIZE(m34) == 3 && ARRAY_SIZE(m34[0]) == 4 && ARRAY_SIZE("abc") == 4);
    CHECK(baud_index(115200u) == 4 && baud_hits[4] == 1u && baud_index(1234u) == -1);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
