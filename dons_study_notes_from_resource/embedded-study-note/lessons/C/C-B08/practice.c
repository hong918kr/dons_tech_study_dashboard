/* C-B08 추가 연습 — const 정확히 쓰기
 *   연습 1: 선언 읽기 (오른쪽→왼쪽 규칙, east const)
 *   연습 2: const 입력 → const 결과, 그리고 "합법적인" const 떼기 (strchr 문제)
 *   연습 3: const ≠ 불변 — 별칭(aliasing)이 있으면 값이 바뀐다
 *   연습 4: 플래시에 두는 보정 테이블 (const 구조체 배열 + 선형 보간)
 *   연습 5: const 드라이버 ops 테이블 (함수 포인터 표를 플래시에)
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IS_TYPE(e, T) _Generic((e), T: 1, default: 0)

/* ---------- 연습 1: 선언 읽기 ---------- */
static char ch = 'x';
static char *pc = &ch;
static char *const *x1 = &pc;          /* const 포인터(char*)를 가리키는 포인터       */
static const char *pcc = "hi";
static const char **const x2 = &pcc;   /* const char * 를 가리키는 const 포인터       */
static char const *x3 = &ch;           /* east const: const char * 와 같은 타입       */

/* ---------- 연습 2: const 를 지키는 검색 + 가변 래퍼 ---------- */
static const char *find_char(const char *s, char c) {
    for (; *s; s++)
        if (*s == c) return s;
    return NULL;
}
/* 원래 객체가 const 가 아닐 때만 쓰는 래퍼: const 를 떼는 캐스트가 합법인 유일한 경우 */
static char *find_char_mut(char *s, char c) {
    return (char *)find_char(s, c);
}

/* ---------- 연습 3: const ≠ 불변 ---------- */
static int read_write_read(const int *p, int *q) {
    int before = *p;
    *q = 50;               /* p 와 q 가 같은 곳을 가리키면 *p 도 바뀐다 */
    return before + *p;    /* 컴파일러는 *p 를 다시 읽어야 한다 */
}

/* ---------- 연습 4: 보정 테이블 (NTC 서미스터 ADC → 0.1°C) ---------- */
typedef struct { uint16_t raw; int16_t temp_c10; } cal_pt_t;
static const cal_pt_t k_cal[] = {
    {  100, -200 }, {  500,    0 }, {  900,  250 }, { 1300,  600 },
};
static int16_t cal_lookup(const cal_pt_t *tbl, size_t n, uint16_t raw) {
    if (raw <= tbl[0].raw) return tbl[0].temp_c10;
    if (raw >= tbl[n - 1].raw) return tbl[n - 1].temp_c10;
    size_t i = 1;
    while (tbl[i].raw < raw) i++;
    int32_t r0 = tbl[i - 1].raw, r1 = tbl[i].raw;
    int32_t t0 = tbl[i - 1].temp_c10, t1 = tbl[i].temp_c10;
    return (int16_t)(t0 + (t1 - t0) * ((int32_t)raw - r0) / (r1 - r0));
}

/* ---------- 연습 5: const ops 테이블 ---------- */
typedef struct {
    int    (*init)(void);
    size_t (*write)(const uint8_t *buf, size_t len);
} uart_ops_t;

static size_t g_written;
static int    fake_init(void) { return 0; }
static size_t fake_write(const uint8_t *buf, size_t len) { (void)buf; g_written += len; return len; }

static const uart_ops_t k_fake_uart_ops = { .init = fake_init, .write = fake_write };

static size_t console_puts(const uart_ops_t *ops, const char *s) {
    return ops->write((const uint8_t *)s, strlen(s));
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    CHECK(IS_TYPE(&x1, char *const **));
    CHECK(IS_TYPE(&x2, const char **const *));
    CHECK(IS_TYPE(x3, const char *));
    CHECK(**x1 == 'x' && **x2 == 'h' && *x3 == 'x');

    /* 연습 2 */
    const char *msg = "AT+RST";
    CHECK(find_char(msg, '+') == msg + 2);
    CHECK(find_char(msg, '?') == NULL);
    CHECK(IS_TYPE(find_char(msg, 'A'), const char *));
    char line[] = "key=value";
    char *eq = find_char_mut(line, '=');
    *eq = '\0';                                    /* line 은 const 가 아니므로 합법 */
    CHECK(strcmp(line, "key") == 0 && strcmp(eq + 1, "value") == 0);

    /* 연습 3 */
    int v = 1, w = 1;
    CHECK(read_write_read(&v, &w) == 2);           /* 별칭 없음: 1 + 1 */
    CHECK(read_write_read(&v, &v) == 51);          /* 별칭: 1 + 50 */

    /* 연습 4 */
    size_t n = sizeof k_cal / sizeof k_cal[0];
    CHECK(IS_TYPE(&k_cal, const cal_pt_t (*)[4]));
    CHECK(cal_lookup(k_cal, n, 50) == -200);
    CHECK(cal_lookup(k_cal, n, 300) == -100);
    CHECK(cal_lookup(k_cal, n, 700) == 125);
    CHECK(cal_lookup(k_cal, n, 900) == 250);
    CHECK(cal_lookup(k_cal, n, 5000) == 600);

    /* 연습 5 */
    CHECK(IS_TYPE(&k_fake_uart_ops, const uart_ops_t *));
    CHECK(k_fake_uart_ops.init() == 0);
    CHECK(console_puts(&k_fake_uart_ops, "boot ok\r\n") == 9 && g_written == 9);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
