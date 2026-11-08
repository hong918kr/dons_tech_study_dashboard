/* C-M11 추가 연습 — 한 번에 검증 */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static inline int32_t  max_i32(int32_t a, int32_t b)   { return a > b ? a : b; }
static inline uint32_t max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }
static inline float    max_f(float a, float b)         { return isnan(a) ? b : isnan(b) ? a : (a > b ? a : b); }

/* 가짜 UART 수신 FIFO */
static const uint32_t rx_bytes[4] = { 7, 9, 2, 11 };
static unsigned rx_pos;
static uint32_t uart_read_dr(void) { return rx_bytes[rx_pos++ % 4u]; }

/* 연습 1: 두 인자의 (승격 후) 타입이 같을 때만 허용하는 max_strict — 섞이면 컴파일 에러.
 * 바깥 _Generic 이 a 의 타입으로, 안쪽 _Generic 이 b 의 타입으로 고른다.
 * 선택되지 않은 항목의 식도 "유효한 식" 이어야 하므로 안쪽마다 default 가 필요하다.
 * default 는 인자 0개짜리 선언뿐인 함수 → 실제로 골라져 (a, b) 로 호출되면 그때만 에러. */
void max_strict_TYPE_MISMATCH(void);              /* 정의하지 않는다 — 골라지면 컴파일 에러 */
#define max_strict(a, b) _Generic((a) + 0,                                                      \
        int:          _Generic((b) + 0, int: max_i32,          default: max_strict_TYPE_MISMATCH), \
        unsigned int: _Generic((b) + 0, unsigned int: max_u32, default: max_strict_TYPE_MISMATCH), \
        float:        _Generic((b) + 0, float: max_f,          default: max_strict_TYPE_MISMATCH))((a), (b))

/* 연습 2: bswap_of — 여기서는 + 0 을 쓰면 안 된다 (uint16_t 가 int 로 승격돼 uint16_t 항목을 못 찾는다) */
static inline uint16_t bswap16(uint16_t x) { return (uint16_t)((x >> 8) | (x << 8)); }
static inline uint32_t bswap32(uint32_t x) {
    return (x >> 24) | ((x >> 8) & 0x0000FF00u) | ((x << 8) & 0x00FF0000u) | (x << 24);
}
static inline uint64_t bswap64(uint64_t x) {
    return ((uint64_t)bswap32((uint32_t)x) << 32) | bswap32((uint32_t)(x >> 32));
}
#define bswap_of(x) _Generic((x), uint16_t: bswap16, uint32_t: bswap32, uint64_t: bswap64)(x)

/* 연습 3: 표준 C11 만으로 SWAP — 바이트 교환 함수 + 크기 검사(정적 단언)는 매크로 */
static inline void swap_bytes(void *a, void *b, size_t n) {
    unsigned char *p = a, *q = b;
    for (size_t k = 0; k < n; k++) { unsigned char t = p[k]; p[k] = q[k]; q[k] = t; }
}
#define SWAP_ANY(a, b) swap_bytes(&(a), &(b), sizeof(a) \
    + 0 * sizeof(struct { _Static_assert(sizeof(a) == sizeof(b), "SWAP_ANY: size mismatch"); char c_; }))

/* 연습 4: clamp_of — 인자 셋을 한 번씩만 평가하는 타입 제네릭 clamp */
static inline int32_t  clamp_i32(int32_t x, int32_t lo, int32_t hi)    { return x < lo ? lo : x > hi ? hi : x; }
static inline uint32_t clamp_u32(uint32_t x, uint32_t lo, uint32_t hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline float    clamp_f(float x, float lo, float hi)            { return x < lo ? lo : x > hi ? hi : x; }
#define clamp_of(x, lo, hi) _Generic((x) + (lo) + (hi),             \
        int: clamp_i32, unsigned int: clamp_u32, float: clamp_f)((x), (lo), (hi))

/* 연습 5: TRACE() — 호출한 함수 이름과 줄을 기록. __func__ / __LINE__ 은 "쓰인 곳" 기준이라 매크로여야 한다 */
typedef struct { const char *func; int line; } trace_t;
static trace_t g_trace[4];
static unsigned g_trace_n;
static inline void trace_at(const char *func, int line) {
    g_trace[g_trace_n % 4u] = (trace_t){ func, line };
    g_trace_n++;
}
#define TRACE() trace_at(__func__, __LINE__)
static inline const char *who_am_i(void) { return __func__; }     /* inline 함수 안의 __func__ = 자기 이름 */
static int motor_start_line;
static void motor_start(void) { motor_start_line = __LINE__; TRACE(); }

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define IS_TYPE(e, T)  _Generic((e), T: 1, default: 0)

int main(void) {
    /* 연습 1 */
    int si = -1, sj = 1;
    unsigned ua = 3u, ub = 9u;
    uint8_t u8 = 200u;
    CHECK(max_strict(si, sj) == 1 && IS_TYPE(max_strict(si, sj), int));
    CHECK(max_strict(ua, ub) == 9u && IS_TYPE(max_strict(ua, ub), unsigned int));
    CHECK(max_strict(1.5f, 2.5f) == 2.5f);
    CHECK(max_strict(u8, si) == 200);               /* uint8_t + 0 → int 라서 int 와 짝이 맞는다 */
    /* max_strict(si, ua) 는 컴파일 에러 — demo_generic.c -DMIXED 참고 */

    /* 연습 2 */
    uint16_t h = 0x1234u;
    uint32_t w = 0x11223344u;
    uint64_t d = 0x0102030405060708ull;
    CHECK(bswap_of(h) == 0x3412u && IS_TYPE(bswap_of(h), uint16_t));
    CHECK(bswap_of(w) == 0x44332211u && IS_TYPE(bswap_of(w), uint32_t));
    CHECK(bswap_of(d) == 0x0807060504030201ull && IS_TYPE(bswap_of(d), uint64_t));
    CHECK(bswap_of((uint16_t)0xABCDu) == 0xCDABu);

    /* 연습 3 */
    double x = 1.5, y = -2.0;
    SWAP_ANY(x, y);
    CHECK(x == -2.0 && y == 1.5);
    struct cfg { uint8_t id; uint32_t baud; } c1 = { 1, 9600u }, c2 = { 2, 115200u };
    SWAP_ANY(c1, c2);
    CHECK(c1.id == 2 && c1.baud == 115200u && c2.id == 1 && c2.baud == 9600u);
    uint64_t q1 = 0x100000001ull, q2 = 7u;
    SWAP_ANY(q1, q2);
    CHECK(q1 == 7u && q2 == 0x100000001ull);

    /* 연습 4 */
    CHECK(clamp_of(-5, 0, 10) == 0 && clamp_of(50, 0, 10) == 10 && clamp_of(7, 0, 10) == 7);
    CHECK(clamp_of(5u, 1u, 3u) == 3u && IS_TYPE(clamp_of(5u, 1u, 3u), unsigned int));
    CHECK(clamp_of(0.25f, 0.5f, 1.0f) == 0.5f);
    rx_pos = 0;
    CHECK(clamp_of(uart_read_dr(), 2u, 5u) == 5u && rx_pos == 1u);   /* 7 → 5, FIFO 에서 1바이트만 */

    /* 연습 5 */
    int here = __LINE__; TRACE();
    motor_start();
    CHECK(g_trace_n == 2u);
    CHECK(strcmp(g_trace[0].func, "main") == 0 && g_trace[0].line == here);
    CHECK(strcmp(g_trace[1].func, "motor_start") == 0 && g_trace[1].line == motor_start_line);
    CHECK(strcmp(who_am_i(), "who_am_i") == 0);     /* 함수로 감싸면 호출자 이름을 잃는다 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
