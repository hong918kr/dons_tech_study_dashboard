// CPP-B01 C에서 C++로: 펌웨어용 C++ 부분집합
// ring_gpio.c (C 링버퍼 + GPIO 드라이버)를 C++ 로 옮긴 버전과 그 테스트.
// 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>     // std::size
#include <new>          // placement new (힙 아님)
#include <type_traits>

// ================= Part A — C 코드가 C++ 에서 막히는 곳 고치기 =================

// --- 정적 메모리 풀: C 시절 API 그대로 (void* 반환) ---
alignas(8) static std::uint8_t pool[256];
static std::size_t pool_used;

static void pool_reset() { pool_used = 0; std::memset(pool, 0xA5, sizeof pool); }

void *pool_alloc(std::size_t n, std::size_t align) {   // C API 그대로 (외부 링크). align 은 2의 거듭제곱
    std::size_t off = (pool_used + align - 1u) & ~(align - 1u);
    if (off + n > sizeof pool) return nullptr;
    pool_used = off + n;
    return &pool[off];
}

// A1. C: `rb_t *rb = pool_alloc(sizeof *rb);`  → C++: void* 는 암시적으로 T* 가 되지 않는다.
//     타입 있는 할당 함수로 감싸서 캐스트를 한 곳에 가둔다.
template <typename T>
static T *pool_new() {
    void *p = pool_alloc(sizeof(T), alignof(T));
    if (!p) return nullptr;
    return ::new (p) T{};          // placement new: 이 메모리에 T 의 수명을 시작 + 0 초기화
}

// --- 링버퍼 (C 의 `char *name` → `const char *name`, 지역변수 `new` → `next`) ---
struct rb_t {
    std::uint8_t *buf;
    std::uint32_t head, tail, mask;          // 크기는 2의 거듭제곱
    const char   *name;                      // 문자열 리터럴은 const char[N]
};

// A2. size 가 0 이거나 2의 거듭제곱이 아니면 nullptr. 풀이 모자라도 nullptr.
static rb_t *rb_create(std::uint32_t size, const char *name) {
    if (size == 0 || (size & (size - 1u)) != 0) return nullptr;
    rb_t *rb = pool_new<rb_t>();
    if (!rb) return nullptr;
    rb->buf = static_cast<std::uint8_t *>(pool_alloc(size, 1));
    if (!rb->buf) return nullptr;
    rb->mask = size - 1u;
    rb->name = name;
    return rb;
}

static bool rb_push(rb_t *rb, std::uint8_t b) {
    std::uint32_t next = (rb->head + 1u) & rb->mask;   // C 코드의 `new` 는 C++ 키워드
    if (next == rb->tail) return false;                // full (한 칸은 비워 둔다)
    rb->buf[rb->head] = b;
    rb->head = next;
    return true;
}

static bool rb_pop(rb_t *rb, std::uint8_t *out) {
    if (rb->head == rb->tail) return false;            // empty
    *out = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1u) & rb->mask;
    return true;
}

// --- GPIO 설정 (C 의 멤버 이름 `class` → `cls`) ---
enum gpio_mode_t { MODE_INPUT, MODE_OUTPUT, MODE_AF, MODE_ANALOG };   // 값 범위 0..3

struct gpio_cfg_t {
    std::uint8_t pin;
    gpio_mode_t  mode;
    std::uint8_t cls;                        // 우선순위 클래스
};

// A3. C: `{ .mode = MODE_OUTPUT, .pin = 5, .class = 1 }` — C++20 은 선언 순서만 허용.
constexpr gpio_cfg_t led_cfg = { .pin = 5, .mode = MODE_OUTPUT, .cls = 1 };

// A4. C: `[MODE_OUTPUT] = "out", [MODE_INPUT] = "in", ...` — 배열 지정자는 C++ 에 없다.
static constexpr const char *kModeNames[] = { "in", "out", "af", "ana" };
static_assert(std::size(kModeNames) == MODE_ANALOG + 1, "kModeNames 길이가 enum 과 다름");

static const char *mode_name(gpio_mode_t m) { return kModeNames[m]; }

// ================= Part B — enum ↔ 정수, 경계 조건 =================

// B1. 레지스터에서 읽은 32비트 값 → enum. 위쪽 쓰레기 비트가 있을 수 있다.
//     C 는 `return raw;` 로 끝. C++ 은 static_cast 가 필요하고, 범위 밖 값이면 UB.
static constexpr gpio_mode_t mode_from_bits(std::uint32_t raw) {
    return static_cast<gpio_mode_t>(raw & 3u);
}

// B2. C: `c->mode++` (enum 에 ++ 는 C++ 에러). ANALOG 다음은 INPUT 으로 돈다.
static constexpr gpio_mode_t next_mode(gpio_mode_t m) {
    return static_cast<gpio_mode_t>((m + 1u) & 3u);
}

// ================= Part C — mock GPIO + UART RX "ISR" =================
struct GPIO_TypeDef { volatile std::uint32_t MODER, ODR, BSRR; };
static GPIO_TypeDef fake_gpioa;                 // 실제 칩: reinterpret_cast<GPIO_TypeDef*>(0x48000000)

static void sim_apply_bsrr(GPIO_TypeDef *g) {   // 하드웨어 흉내: BSRR → ODR
    std::uint32_t b = g->BSRR;
    g->ODR = (g->ODR & ~(b >> 16)) | (b & 0xFFFFu);
    g->BSRR = 0;
}

// C1. MODER 의 핀당 2비트 필드에 cfg.mode 를 쓴다 (다른 핀은 보존)
static void gpio_init(GPIO_TypeDef *g, const gpio_cfg_t *c) {
    std::uint32_t sh = 2u * c->pin;
    g->MODER = (g->MODER & ~(3u << sh)) | (static_cast<std::uint32_t>(c->mode) << sh);
}

static gpio_mode_t gpio_get_mode(const GPIO_TypeDef *g, unsigned pin) {
    return mode_from_bits(g->MODER >> (2u * pin));
}

struct uart_drv_t {
    rb_t         *rx;
    std::uint32_t overruns;
};

// C2. UART RX 인터럽트: 받은 바이트를 링버퍼에 넣는다.
//     가득 찼으면 overruns++ 하고 led_cfg.pin 의 에러 LED 를 BSRR 로 켠다.
static void uart_rx_isr(uart_drv_t *u, GPIO_TypeDef *g, std::uint8_t byte) {
    if (!u->rx) return;
    if (!rb_push(u->rx, byte)) {
        u->overruns++;
        g->BSRR = 1u << led_cfg.pin;         // 단일 store — ISR 안에서 RMW 없음
    }
}

// C3. 메인 루프: 최대 n 바이트를 꺼내 dst 에 담고 개수를 돌려준다.
static std::size_t uart_read(uart_drv_t *u, std::uint8_t *dst, std::size_t n) {
    std::size_t i = 0;
    while (u->rx && i < n && rb_pop(u->rx, &dst[i])) i++;
    return i;
}

// ================= 테스트 하네스 =================
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

// 컴파일 타임 사실 확인 (starter 에서도 성립)
static_assert(std::is_same_v<decltype("uart_rx"), const char (&)[8]>);  // 리터럴은 const char[8]
static_assert(!std::is_convertible_v<void *, rb_t *>);                    // void* → T* 암시적 변환 없음
static_assert(!std::is_convertible_v<int, gpio_mode_t>);                  // int → enum 암시적 변환 없음
static_assert(std::is_convertible_v<gpio_mode_t, int>);                   // enum → int 는 된다
static_assert(sizeof('A') == 1);                                          // C 에선 sizeof(int)

int main() {
    // ---- Part A ----
    pool_reset();
    rb_t *rb = rb_create(8, "test");
    CHECK(rb != nullptr);
    CHECK(rb && reinterpret_cast<std::uintptr_t>(rb) % alignof(rb_t) == 0);
    CHECK(rb && rb->head == 0 && rb->tail == 0 && rb->mask == 7u);   // 0xA5 쓰레기가 아니라 0
    CHECK(rb && std::strcmp(rb->name, "test") == 0);
    CHECK(led_cfg.pin == 5 && led_cfg.mode == MODE_OUTPUT && led_cfg.cls == 1);
    CHECK(std::strcmp(mode_name(MODE_INPUT), "in") == 0);
    CHECK(std::strcmp(mode_name(MODE_ANALOG), "ana") == 0);

    // ---- Part B ----
    CHECK(rb_create(0, "zero") == nullptr);
    CHECK(rb_create(12, "npot") == nullptr);
    CHECK(rb_create(1024, "huge") == nullptr);                       // 풀(256B) 초과
    CHECK(mode_from_bits(0x6u) == MODE_AF);                          // 위쪽 비트 무시
    CHECK(mode_from_bits(0xFFFFFFFDu) == MODE_OUTPUT);
    constexpr gpio_mode_t ce = mode_from_bits(7);                    // 상수식에서 UB 면 빌드 실패
    CHECK(ce == MODE_ANALOG);
    CHECK(next_mode(MODE_AF) == MODE_ANALOG);
    CHECK(next_mode(MODE_ANALOG) == MODE_INPUT);                     // 4 가 아니라 0 으로 돈다
    int pushed = 0;
    for (int i = 0; i < 10; i++) pushed += rb && rb_push(rb, std::uint8_t(i));
    CHECK(pushed == 7);                                              // 크기 8 → 7 칸 사용
    std::uint8_t v = 0xFF; bool fifo = true;
    for (int i = 0; i < 7; i++) fifo = fifo && rb && rb_pop(rb, &v) && v == i;
    CHECK(fifo && !(rb && rb_pop(rb, &v)));                          // 순서대로 나오고, 다 비면 false

    // ---- Part C ----
    pool_reset();
    fake_gpioa = {};
    fake_gpioa.MODER = 0xFFFFFFFFu;
    gpio_init(&fake_gpioa, &led_cfg);
    CHECK(fake_gpioa.MODER == 0xFFFFF7FFu);                          // 핀5 = 01, 나머지 보존
    CHECK(gpio_get_mode(&fake_gpioa, 5) == MODE_OUTPUT);
    CHECK(gpio_get_mode(&fake_gpioa, 4) == MODE_ANALOG);

    uart_drv_t uart = { rb_create(4, "uart_rx"), 0 };                // 3 바이트까지
    const char *msg = "OK\n";
    for (const char *p = msg; *p; p++) uart_rx_isr(&uart, &fake_gpioa, std::uint8_t(*p));
    sim_apply_bsrr(&fake_gpioa);
    CHECK(uart.overruns == 0 && fake_gpioa.ODR == 0);
    uart_rx_isr(&uart, &fake_gpioa, '!');                            // 넘침
    sim_apply_bsrr(&fake_gpioa);                                     // 하드웨어가 BSRR → ODR 반영
    CHECK(uart.overruns == 1 && fake_gpioa.ODR == (1u << 5));        // 에러 LED 켜짐
    std::uint8_t got[8] = {};
    CHECK(uart_read(&uart, got, sizeof got) == 3 && std::memcmp(got, "OK\n", 3) == 0);
    CHECK(uart_read(&uart, got, sizeof got) == 0);

    std::printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
