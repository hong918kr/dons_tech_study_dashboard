// CPP-B01 demo — ring_gpio.c 를 "최소 수정"으로 C++ 로 옮긴 버전 (로직·자료구조 동일)
//   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -c ring_gpio_port.cpp
// build.sh 가 C 빌드와 size / objdump 를 비교한다. 바뀐 줄에는 [①..⑥] 표시.
#include <cstddef>
#include <cstdint>

// ---- 정적 메모리 풀 ----
alignas(8) static uint8_t pool[256];                        // _Alignas → alignas
static size_t pool_used;

void *pool_alloc(size_t n) {
    n = (n + 7u) & ~(size_t)7u;
    if (pool_used + n > sizeof pool) return nullptr;
    void *p = &pool[pool_used];
    pool_used += n;
    return p;
}

// ---- 링버퍼 ----
struct rb_t {
    uint8_t    *buf;
    uint32_t    head, tail, mask;
    const char *name;                                       // [⑤] const char*
};

rb_t *rb_create(uint32_t size, const char *name) {          // [⑤]
    rb_t *rb = static_cast<rb_t *>(pool_alloc(sizeof *rb)); // [①] 명시적 캐스트
    if (!rb) return nullptr;
    rb->buf = static_cast<uint8_t *>(pool_alloc(size));     // [①]
    rb->head = rb->tail = 0;
    rb->mask = size - 1u;
    rb->name = name;
    return rb;
}

bool rb_push(rb_t *rb, uint8_t b) {
    uint32_t next = (rb->head + 1u) & rb->mask;             // [②] new → next
    if (next == rb->tail) return false;
    rb->buf[rb->head] = b;
    rb->head = next;
    return true;
}

bool rb_pop(rb_t *rb, uint8_t *out) {
    if (rb->head == rb->tail) return false;
    *out = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1u) & rb->mask;
    return true;
}

// ---- GPIO ----
enum gpio_mode_t { MODE_INPUT, MODE_OUTPUT, MODE_AF, MODE_ANALOG };

struct gpio_cfg_t {
    uint8_t     pin;
    gpio_mode_t mode;
    uint8_t     cls;                                        // [②] class → cls
};

struct GPIO_TypeDef { volatile uint32_t MODER, ODR, BSRR; };

// [⑥] C++ 에서 namespace-scope const 는 내부 링크(internal linkage) → extern 을 붙여야 C와 같다.
//      -DNAIVE_CONST 로 빌드하면 extern 이 빠져 심볼이 사라지는 것을 nm 으로 볼 수 있다.
#ifdef NAIVE_CONST
#define LINK
#else
#define LINK extern
#endif

// [③] 지정 초기화는 선언 순서대로 (C++20)
LINK const gpio_cfg_t led_cfg = { .pin = 5, .mode = MODE_OUTPUT, .cls = 1 };

// [③] 배열 지정자 대신 enum 순서대로 + 순서가 바뀌면 빌드가 깨지게
LINK const char *const mode_names[4] = { "in", "out", "af", "ana" };
static_assert(MODE_INPUT == 0 && MODE_OUTPUT == 1 && MODE_AF == 2 && MODE_ANALOG == 3,
              "mode_names[] 순서가 enum 과 어긋남");

void gpio_init(GPIO_TypeDef *g, const gpio_cfg_t *c) {
    uint32_t sh = 2u * c->pin;
    g->MODER = (g->MODER & ~(3u << sh)) | ((uint32_t)c->mode << sh);
}

gpio_mode_t gpio_get_mode(const GPIO_TypeDef *g, unsigned pin) {
    return static_cast<gpio_mode_t>((g->MODER >> (2u * pin)) & 3u);   // [④]
}

void gpio_next_mode(gpio_cfg_t *c) {
    c->mode = static_cast<gpio_mode_t>((c->mode + 1u) & 3u);          // [④]
}

rb_t *uart_rx_create(void) { return rb_create(16, "uart_rx"); }      // [⑤] 이제 OK
