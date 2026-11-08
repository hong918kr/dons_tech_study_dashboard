/* CPP-B01 demo — "레거시" C 링버퍼 + GPIO 드라이버 (C11로는 깨끗하게 빌드됨)
 *   C   : cc  -std=c11   -Wall -Wextra -Werror -O2 -c ring_gpio.c -o /tmp/rg_c.o          → OK
 *   C++ : c++ -x c++ -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti -c ring_gpio.c → 에러 9 + 경고 3 (-Werror 면 12)
 * C++로 고친 버전은 ring_gpio_port.cpp, 비교 스크립트는 build.sh */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- 정적 메모리 풀 (펌웨어: 힙 없음) ---- */
static _Alignas(8) uint8_t pool[256];
static size_t pool_used;

void *pool_alloc(size_t n) {
    n = (n + 7u) & ~(size_t)7u;                 /* 8바이트 정렬 */
    if (pool_used + n > sizeof pool) return NULL;
    void *p = &pool[pool_used];
    pool_used += n;
    return p;
}

/* ---- 링버퍼 ---- */
typedef struct {
    uint8_t *buf;
    uint32_t head, tail, mask;                  /* 크기는 2의 거듭제곱 */
    char    *name;                              /* 디버그용 이름 */
} rb_t;

rb_t *rb_create(uint32_t size, char *name) {
    rb_t *rb = pool_alloc(sizeof *rb);          /* ① void* → rb_t* 암시적 변환 */
    if (!rb) return NULL;
    rb->buf = pool_alloc(size);                 /* ① void* → uint8_t*         */
    rb->head = rb->tail = 0;
    rb->mask = size - 1u;
    rb->name = name;
    return rb;
}

bool rb_push(rb_t *rb, uint8_t b) {
    uint32_t new = (rb->head + 1u) & rb->mask;  /* ② 'new' 는 C++ 키워드 */
    if (new == rb->tail) return false;          /* full */
    rb->buf[rb->head] = b;
    rb->head = new;
    return true;
}

bool rb_pop(rb_t *rb, uint8_t *out) {
    if (rb->head == rb->tail) return false;     /* empty */
    *out = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1u) & rb->mask;
    return true;
}

/* ---- GPIO ---- */
typedef enum { MODE_INPUT, MODE_OUTPUT, MODE_AF, MODE_ANALOG } gpio_mode_t;

typedef struct {
    uint8_t     pin;
    gpio_mode_t mode;
    uint8_t     class;                          /* ② 'class' 도 C++ 키워드 */
} gpio_cfg_t;

typedef struct { volatile uint32_t MODER, ODR, BSRR; } GPIO_TypeDef;

/* ③ 선언 순서와 다른 지정 초기화 (.mode 가 .pin 보다 먼저) */
const gpio_cfg_t led_cfg = { .mode = MODE_OUTPUT, .pin = 5, .class = 1 };

/* ③ 배열 지정자 [index] = ... */
const char *const mode_names[4] = {
    [MODE_OUTPUT] = "out", [MODE_INPUT] = "in", [MODE_ANALOG] = "ana", [MODE_AF] = "af",
};

void gpio_init(GPIO_TypeDef *g, const gpio_cfg_t *c) {
    uint32_t sh = 2u * c->pin;
    g->MODER = (g->MODER & ~(3u << sh)) | ((uint32_t)c->mode << sh);
}

gpio_mode_t gpio_get_mode(const GPIO_TypeDef *g, unsigned pin) {
    return (g->MODER >> (2u * pin)) & 3u;       /* ④ unsigned → enum 암시적 변환 */
}

void gpio_next_mode(gpio_cfg_t *c) {
    c->mode = (c->mode + 1u) & 3u;              /* ④ unsigned → enum 대입 */
}

/* ⑤ 문자열 리터럴 → char* */
rb_t *uart_rx_create(void) { return rb_create(16, "uart_rx"); }
