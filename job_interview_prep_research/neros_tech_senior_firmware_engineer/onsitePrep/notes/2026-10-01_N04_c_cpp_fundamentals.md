# 🧱 N04 · 기본 C/C++ — 1:1 라운드 속사포 Q&A

> 온사이트 1:1 중 "기본 C/C++" 라운드 대비. 질문마다 **30초 영어 답변 + 아주 짧은 코드**로 정리했다. C는 키워드 → 포인터 → 메모리 표현 → 비트 → 정수와 UB → 메모리 배치 → ISR과 동시성 순서, C++은 임베디드에서 실제로 묻는 것(RAII, vtable 비용, 예외 끄기, placement new)만 골랐다. 끝에 "C 출신이 C++에서 막히는 것" 표와 함정 코드 8개가 있다.

## 0. 답하는 틀

- **정의 한 문장 → 왜 중요한가 (임베디드에서) → 짧은 예시**. 30초 안에 끝내고 면접관이 파고들게 둔다
- 코드를 쓰라고 하면 먼저 "Let me write the minimal version, then talk about edge cases."
- 모르는 건 아는 데까지만 말하고 "I'd verify that in the reference manual / standard." UB나 메모리 모델은 아는 척이 가장 위험하다
- Don의 배경: Cortex-R8/R82/M0+, Xtensa에서 bare-metal production C/C++. "In SSD firmware we…"로 실제 맥락을 붙이면 답이 단단해진다 (Don: 실제 사례 한 줄씩 준비)

---

## 1. C 키워드

### Q1. What does `volatile` do?

> "volatile tells the compiler that every read and write to this object is observable, so it must not cache the value in a register, remove the access, or merge accesses. We use it for memory-mapped registers, variables shared with an ISR, and memory changed by DMA. It does not make anything atomic, and it doesn't order non-volatile accesses or stop the CPU from reordering — so it's not a synchronization primitive."

```c
#include <stdint.h>

#define UART_SR (*(volatile uint32_t *)0x40011000u)   /* 메모리 맵 레지스터 (예시 주소) */
#define SR_RXNE (1u << 5)

volatile int g_rx_ready;          /* ISR이 set, main loop가 polling */

void uart_wait_rx(void) {
    while ((UART_SR & SR_RXNE) == 0) {
        /* volatile 없으면 컴파일러가 한 번만 읽고 무한 루프로 만들 수 있다 */
    }
}
```

### Q2. Why is `volatile` not enough for sharing data between an ISR and main?

> "Because volatile only guarantees the access happens, not that it's indivisible. A read-modify-write like `count++` is load, add, store — an interrupt between them loses an update. For multi-word data, main can read half old and half new. You need a critical section, an atomic operation, or a lock-free structure like a single-producer single-consumer ring buffer with proper ordering."

```c
#include <stdint.h>

volatile uint32_t g_ticks;

void systick_isr(void) { g_ticks++; }          /* ISR 안에서는 안전 (더 높은 우선순위가 같은 변수를 안 건드린다면) */

void main_loop_bad(void) { g_ticks++; }         /* main에서 RMW: ISR과 경쟁 → 업데이트 유실 가능 */
```

### Q3. `const volatile` — does that make sense?

> "Yes. const means *my code* won't write it; volatile means *something else* can change it. A read-only hardware status register is the classic example: `const volatile uint32_t *status`."

```c
#include <stdint.h>

typedef struct {
    volatile uint32_t CR;          /* 제어: 읽기/쓰기 */
    const volatile uint32_t SR;    /* 상태: HW가 바꾸고, SW는 읽기만 */
    volatile uint32_t DR;
} uart_regs_t;

uint32_t uart_status(const uart_regs_t *u) { return u->SR; }
```

| 선언 | 의미 |
|---|---|
| `const int *p` | 가리키는 값을 못 바꿈 (포인터는 바꿀 수 있음) |
| `int *const p` | 포인터를 못 바꿈 (값은 바꿀 수 있음) |
| `const int *const p` | 둘 다 못 바꿈 |
| `volatile uint32_t *p` | 가리키는 값이 volatile (레지스터 접근의 정석) |
| `uint32_t *volatile p` | 포인터 변수 자체가 volatile (ISR이 포인터를 바꿀 때) |

### Q4. What are the meanings of `static`?

> "Three common ones. At file scope, static gives internal linkage — the symbol is private to that translation unit. Inside a function, static gives the variable static storage duration — it keeps its value between calls and lives in .data or .bss, not on the stack. In C++, a static class member belongs to the class, not to each object."

```c
static int s_counter;                 /* 1) 이 .c 파일 안에서만 보임 */

static void helper(void) {}           /* 1) 함수도 마찬가지 */

int next_id(void) {
    static int id = 0;                /* 2) 호출 사이에 값 유지, 스택이 아님 */
    helper();
    return ++id + s_counter;
}
```

- 임베디드 포인트: 함수 안 `static` 버퍼는 스택을 아끼지만 **재진입(reentrant)이 깨진다** — ISR과 main이 같이 부르면 안 된다

### Q5. `extern` vs definition?

> "A declaration says the symbol exists somewhere; a definition allocates it. Put `extern int x;` in the header and `int x;` in exactly one .c file — otherwise you get multiple-definition link errors, or with older toolchains silently merged 'common' symbols."

---

## 2. 포인터

### Q6. Pointer arithmetic — what does `p + 1` mean?

> "It advances by `sizeof(*p)` bytes, not one byte. For byte offsets, cast to `uint8_t *` first."

```c
#include <stdint.h>

uint32_t read_word_at(const uint32_t *base, unsigned idx) {
    return *(base + idx);                         /* base + idx*4 바이트 */
}

const uint8_t *byte_offset(const void *base, unsigned off) {
    return (const uint8_t *)base + off;           /* 바이트 단위 */
}
```

### Q7. Pointer vs array — what's array decay?

> "In most expressions an array name converts to a pointer to its first element. That's why `sizeof` inside a function that takes an 'array' parameter gives the pointer size, not the array size — you must pass the length separately."

```c
#include <stddef.h>
#include <stdint.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))  /* 진짜 배열에만 쓸 것 */

uint32_t sum(const uint32_t *buf, size_t n) {      /* 'uint32_t buf[16]'으로 써도 실제 타입은 포인터 */
    uint32_t s = 0;
    for (size_t i = 0; i < n; i++) s += buf[i];
    return s;
}

uint32_t demo(void) {
    uint32_t data[16] = {1, 2, 3};
    return sum(data, ARRAY_LEN(data));             /* 여기서는 16 */
}
```

### Q8. Function pointers and callback tables

> "A function pointer stores the address of a function so you can choose behavior at runtime — command dispatch tables, driver ops structs, state machines. On MCUs a const table of function pointers lives in flash, which is cheap and deterministic."

```c
#include <stdint.h>
#include <stddef.h>

typedef int (*cmd_handler_t)(const uint8_t *payload, size_t len);

static int cmd_ping(const uint8_t *p, size_t n) { (void)p; (void)n; return 0; }
static int cmd_reset(const uint8_t *p, size_t n) { (void)p; (void)n; return 1; }

static const cmd_handler_t k_handlers[] = { cmd_ping, cmd_reset };   /* const → flash */

int dispatch(uint8_t id, const uint8_t *p, size_t n) {
    if (id >= sizeof k_handlers / sizeof k_handlers[0]) return -1;   /* 범위 체크 필수 */
    return k_handlers[id](p, n);
}

/* 드라이버 ops 구조체: Board A/B 변종을 같은 인터페이스로 */
typedef struct {
    int (*init)(void);
    int (*read)(uint8_t reg, uint8_t *val);
} imu_ops_t;
```

### Q9. `void *` and `NULL` — anything to watch?

> "void pointers can't be dereferenced or used in arithmetic in standard C; cast to the right type first. Always check pointers from APIs that can fail, and on MCUs remember address 0 may be valid memory — the vector table on many Cortex-M parts — so a null dereference may not fault."

---

## 3. 구조체와 메모리 표현

### Q10. Why does `sizeof(struct)` differ from the sum of members?

> "Padding. Each member is aligned to its natural alignment so the CPU can access it efficiently, and the struct size is rounded up to its largest alignment so arrays stay aligned. Ordering members from largest to smallest minimizes padding."

```c
#include <stdint.h>

struct bad  { uint8_t a; uint32_t b; uint8_t c; };   /* 보통 12 바이트: 1 + 3pad + 4 + 1 + 3pad */
struct good { uint32_t b; uint8_t a; uint8_t c; };   /* 보통 8 바이트: 4 + 1 + 1 + 2pad */

_Static_assert(sizeof(struct good) <= sizeof(struct bad), "ordering reduces padding");
```

### Q11. Why are packed structs risky for wire formats?

> "Packing removes padding, but the layout still depends on the compiler, endianness, and bit-field ordering, which the standard leaves implementation-defined. Accessing a misaligned member can fault on some cores or be slow, and taking its address gives a misaligned pointer. For telemetry or protocols I serialize field by field into a byte buffer with explicit little-endian encoding — portable and testable."

```c
#include <stdint.h>
#include <stddef.h>

static void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* [type:1][seq:2][timestamp_us:4] = 7 바이트, 컴파일러와 무관 */
size_t encode_hdr(uint8_t *out, uint8_t type, uint16_t seq, uint32_t ts) {
    out[0] = type;
    put_le16(&out[1], seq);
    put_le32(&out[3], ts);
    return 7;
}
```

### Q12. How do you detect and convert endianness?

> "Little-endian stores the least significant byte at the lowest address — ARM Cortex-M and x86 are little-endian; network byte order is big-endian. The best code doesn't need to detect it: build values with shifts from bytes, which works on any host."

```c
#include <stdint.h>

static int is_little_endian(void) {
    const uint16_t x = 1;
    return *(const uint8_t *)&x == 1;           /* char 포인터로 읽는 건 aliasing 규칙상 허용 */
}

static uint32_t bswap32(uint32_t v) {
    return (v >> 24) | ((v >> 8) & 0x0000FF00u) | ((v << 8) & 0x00FF0000u) | (v << 24);
}

static uint16_t get_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }   /* 호스트 무관 */

uint32_t endian_demo(void) {
    const uint8_t b[2] = {0x12, 0x34};
    return (uint32_t)is_little_endian() + bswap32(0x11223344u) + get_be16(b);
}
```

---

## 4. 비트 조작

### Q13. Set, clear, toggle, test a bit — and extract/insert a field

> "Set with OR, clear with AND-NOT, toggle with XOR, test with AND. For a field, build a mask from width and shift, clear the field, then OR in the new value — that's the read-modify-write pattern for registers. Always use unsigned constants like `1u`."

```c
#include <stdint.h>

#define BIT(n)            (1u << (n))
#define FIELD_MASK(w, s)  ((((uint32_t)1u << (w)) - 1u) << (s))   /* w < 32 가정 */

static inline uint32_t field_get(uint32_t reg, unsigned w, unsigned s) {
    return (reg & FIELD_MASK(w, s)) >> s;
}
static inline uint32_t field_set(uint32_t reg, unsigned w, unsigned s, uint32_t v) {
    return (reg & ~FIELD_MASK(w, s)) | ((v << s) & FIELD_MASK(w, s));
}

uint32_t bit_demo(uint32_t r) {
    r |= BIT(3);                  /* set */
    r &= ~BIT(4);                 /* clear */
    r ^= BIT(5);                  /* toggle */
    if (r & BIT(3)) {             /* test */
        r = field_set(r, 3, 8, 5u);   /* bits[10:8] = 5 */
    }
    return field_get(r, 3, 8);
}
```

### Q14. Count set bits, check power of two, reverse bits

> "Popcount: Kernighan's trick `x &= x - 1` clears the lowest set bit, so the loop runs once per set bit; in production I'd use `__builtin_popcount`, which maps to a single instruction where available. Power of two: `x && !(x & (x - 1))`. Reverse: swap halves, then bytes, nibbles, pairs, bits — five steps for 32 bits; ARM has an `RBIT` instruction."

```c
#include <stdint.h>

static unsigned popcount32(uint32_t x) {
    unsigned n = 0;
    while (x) { x &= x - 1u; n++; }
    return n;
}

static int is_pow2(uint32_t x) { return x != 0u && (x & (x - 1u)) == 0u; }

static uint32_t reverse32(uint32_t x) {
    x = (x >> 16) | (x << 16);
    x = ((x & 0xFF00FF00u) >> 8) | ((x & 0x00FF00FFu) << 8);
    x = ((x & 0xF0F0F0F0u) >> 4) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x & 0xCCCCCCCCu) >> 2) | ((x & 0x33333333u) << 2);
    x = ((x & 0xAAAAAAAAu) >> 1) | ((x & 0x55555555u) << 1);
    return x;
}

uint32_t bits_demo(void) { return popcount32(0xF0u) + (uint32_t)is_pow2(64u) + reverse32(1u); }
```

### Q15. Round up to a power-of-two boundary (alignment)

> "`(x + (a - 1)) & ~(a - 1)` when `a` is a power of two — used for buffer alignment, DMA, and cache lines."

```c
#include <stdint.h>

static inline uintptr_t align_up(uintptr_t x, uintptr_t a) { return (x + (a - 1u)) & ~(a - 1u); }

uintptr_t align_demo(void) { return align_up(33u, 32u); }   /* 64 */
```

---

## 5. 정수 규칙과 Undefined Behavior

### Q16. What is integer promotion, and where does it bite?

> "Before arithmetic, types smaller than int are promoted to int. So `uint8_t` math happens in int, and the result only wraps when you store it back. It bites with `~` on small types, with comparisons after arithmetic, and with `uint16_t * uint16_t` on a 32-bit int platform — both promote to signed int, and 65535 times 65535 overflows a signed int, which is undefined behavior."

```c
#include <stdint.h>

int promo_demo(void) {
    uint8_t a = 0xF0;
    int t1 = (~a == 0x0F);                 /* 0: ~a는 int 0xFFFFFF0F */
    int t2 = ((uint8_t)~a == 0x0F);        /* 1: 다시 8비트로 잘라야 함 */
    uint16_t x = 65535, y = 65535;
    uint32_t ok = (uint32_t)x * y;         /* 먼저 unsigned로 캐스트해서 UB 회피 */
    return t1 + t2 + (int)(ok & 1u);
}
```

### Q17. Signed vs unsigned comparison pitfall

> "When you compare a signed int with an unsigned int of the same rank, the signed value converts to unsigned. So `-1 < 1u` is false, because -1 becomes 0xFFFFFFFF. `sizeof` returns size_t, which is unsigned, so `if (len - 1 < sizeof buf)` has the same trap when len is zero."

```c
#include <stddef.h>

int cmp_demo(int i, size_t n) {
    if (i < 0) return 0;                   /* 음수 먼저 걸러낸 뒤에 비교 */
    return (size_t)i < n;
}
```

### Q18. Give examples of undefined behavior

> "Signed integer overflow, shifting by a negative amount or by the width of the type or more, dereferencing null or out-of-bounds pointers, reading uninitialized automatic variables, violating strict aliasing, modifying a string literal, and data races. The danger is that the optimizer assumes UB never happens, so the 'obvious' behavior isn't guaranteed — for example it can delete an overflow check written as `if (x + 1 < x)`."

```c
#include <stdint.h>
#include <string.h>
#include <limits.h>

int safe_add(int a, int b, int *out) {          /* 오버플로를 일으키기 전에 검사 */
    if ((b > 0 && a > INT_MAX - b) || (b < 0 && a < INT_MIN - b)) return -1;
    *out = a + b;
    return 0;
}

uint32_t float_bits(float f) {                  /* *(uint32_t *)&f 는 strict aliasing 위반 */
    uint32_t u;
    memcpy(&u, &f, sizeof u);                    /* memcpy는 정의된 동작, 컴파일러가 move 한 번으로 최적화 */
    return u;
}

uint32_t shift_ok(unsigned s) { return s < 32u ? (1u << s) : 0u; }   /* 1 << 31 (int)도 UB → 1u */
```

---

## 6. 메모리 배치 · 스택 · 할당

### Q19. Describe the memory layout of an embedded C program

> "`.text` is code in flash, `.rodata` is constants in flash, `.data` is initialized globals — stored in flash and copied to RAM by the startup code — and `.bss` is zero-initialized globals, cleared in RAM at startup. Then the heap, if any, and the stack, which on Cortex-M usually starts at the top of RAM and grows down. The linker script defines all of this, and the map file shows how much flash and RAM each section uses."

```c
#include <stdint.h>

const uint8_t k_table[4] = {1, 2, 3, 4};   /* .rodata (flash) */
uint32_t g_init = 42;                       /* .data (flash에 초기값, 부팅 시 RAM으로 복사) */
uint32_t g_zero;                            /* .bss (부팅 시 0으로) */

uint32_t layout_demo(void) {
    uint32_t local = k_table[1];            /* stack */
    static uint32_t s_keep;                 /* .bss */
    s_keep += local;
    return s_keep + g_init + g_zero;
}
```

### Q20. How do you size the stack and detect overflow?

> "Static analysis first — GCC's `-fstack-usage` and call-graph tools give per-function usage; add the worst ISR nesting. Then measure: fill the stack with a known pattern at boot and check the high-water mark later — that's what RTOS stack watermark APIs do. For detection, put a guard region at the stack limit using the MPU so overflow faults immediately, or use the stack-limit register on Armv8-M parts."

### Q21. Why avoid malloc in embedded firmware? What instead?

> "Heap allocation has non-deterministic timing, can fragment over long uptimes, and can fail at runtime in ways that are hard to test. Flight firmware has to run for hours with bounded memory, so we allocate statically at init or use fixed-size block pools: O(1) alloc and free, no fragmentation, and exhaustion is easy to detect and test."

```c
#include <stddef.h>
#include <stdint.h>

#define POOL_BLOCKS 8
#define BLOCK_SIZE  32

static union blk { union blk *next; uint8_t data[BLOCK_SIZE]; } s_pool[POOL_BLOCKS];
static union blk *s_free;

void pool_init(void) {
    for (int i = 0; i < POOL_BLOCKS - 1; i++) s_pool[i].next = &s_pool[i + 1];
    s_pool[POOL_BLOCKS - 1].next = NULL;
    s_free = &s_pool[0];
}

void *pool_alloc(void) {                     /* O(1). ISR와 공유하면 critical section 필요 */
    union blk *b = s_free;
    if (b) s_free = b->next;
    return b;
}

void pool_free(void *p) {
    union blk *b = p;
    b->next = s_free;
    s_free = b;
}
```

---

## 7. 매크로 vs inline

### Q22. Macros vs inline functions — pitfalls?

> "Macros are text substitution: no type checking, arguments can be evaluated more than once, and missing parentheses change precedence. A `static inline` function gives the same performance with type safety and single evaluation. I keep macros for things functions can't do — compile-time constants, `sizeof` tricks, stringizing, include guards."

```c
#include <stdint.h>

#define SQUARE_BAD(x)  x * x                 /* SQUARE_BAD(1 + 2) == 1 + 2*1 + 2 == 5 */
#define SQUARE(x)      ((x) * (x))           /* 괄호는 고쳐도 SQUARE(i++)는 두 번 증가 */
#define MAX_BAD(a, b)  ((a) > (b) ? (a) : (b))

static inline int32_t square_i32(int32_t x) { return x * x; }   /* 타입 안전, 한 번만 평가 */

/* 여러 문장 매크로는 do { } while (0)로 감싸야 if/else 안에서 안전 */
#define LOG_AND_COUNT(cnt) do { (cnt)++; } while (0)

int macro_demo(int i) {
    int c = 0;
    if (i) LOG_AND_COUNT(c); else c = -1;
    return SQUARE_BAD(1 + 2) + SQUARE(3) + MAX_BAD(1, 2) + square_i32(i) + c;
}
```

---

## 8. ISR과 동시성

### Q23. What are the rules for writing an ISR?

> "Keep it short and bounded: acknowledge the interrupt source, move data into a buffer or set a flag, and defer the real work to a task or main loop. No blocking calls, no waiting on mutexes, no printf or malloc, no floating point unless the context saves the FPU state. Shared variables are volatile and protected properly, and I always clear the interrupt flag — otherwise it fires again immediately."

```c
#include <stdint.h>

#define RX_SIZE 64u                              /* 2의 거듭제곱 → 마스크로 wrap */
static volatile uint8_t  s_rx[RX_SIZE];
static volatile uint32_t s_head;                 /* ISR만 씀 */
static volatile uint32_t s_tail;                 /* main만 씀 */
static volatile uint32_t s_dropped;

void uart_rx_isr(uint8_t byte) {                  /* 실제로는 DR 레지스터에서 읽음 */
    uint32_t h = s_head;
    if (h - s_tail == RX_SIZE) { s_dropped++; return; }   /* 가득 참: 버리고 센다 */
    s_rx[h & (RX_SIZE - 1u)] = byte;
    s_head = h + 1u;                              /* 데이터를 쓴 뒤에 인덱스 공개 */
}

int uart_getc(uint8_t *out) {
    uint32_t t = s_tail;
    if (t == s_head) return 0;
    *out = s_rx[t & (RX_SIZE - 1u)];
    s_tail = t + 1u;
    return 1;
}
```

- 이 코드가 단일 코어 Cortex-M에서 동작하는 근거: 인덱스는 정렬된 32비트라 load/store가 atomic이고, 각 인덱스는 한쪽만 쓴다. 멀티코어나 캐시가 있으면 barrier가 필요하다 → [ring buffer 노트](2026-10-01_N05_ring_buffer_onsite.md)

### Q24. What's atomic on a Cortex-M?

> "Aligned byte, halfword, and word loads and stores are single-copy atomic. Read-modify-write is not — `x++` is three instructions. For RMW you either disable interrupts briefly, or on Cortex-M3 and above use exclusive load/store, LDREX/STREX, which retry if anything interfered — that's what C11 atomics compile to. Cortex-M0 and M0+ don't have exclusives, so you mask interrupts there. 64-bit values are never atomic on a 32-bit core without protection."

```text
/* CMSIS 스타일 critical section — 이전 상태를 저장하고 복원해서 중첩에도 안전 */
uint32_t primask = __get_PRIMASK();
__disable_irq();
shared_counter++;              /* 짧게! */
__set_PRIMASK(primask);

/* Cortex-M3+ exclusive access: 실패하면 다시 시도 */
do {
    old = __LDREXW(&counter);
} while (__STREXW(old + 1, &counter));
```

### Q25. What do DMB, DSB, and ISB do?

> "They're Arm barrier instructions. DMB orders memory accesses — accesses before it are observed before accesses after it. DSB is stronger: it waits until all prior memory accesses complete before executing anything further, used before sleep (WFI) or after configuring something like the MPU. ISB flushes the pipeline so the following instructions are re-fetched with the new context — for example after changing CONTROL or relocating the vector table. On a simple single-core M4 you rarely need them for plain RAM, but you do for DMA, multicore, and system configuration changes."

### Q26. C11 atomics and memory_order basics

> "`_Atomic` types give indivisible operations and let me state the ordering I need. Release on the producer's index store and acquire on the consumer's index load is the classic pattern: everything the producer wrote before the release is visible to the consumer after its acquire. `memory_order_relaxed` is atomicity with no ordering — fine for statistics counters."

```c
#include <stdatomic.h>
#include <stdint.h>

#define Q 16u
static uint32_t q_data[Q];
static _Atomic uint32_t q_head, q_tail;

int q_push(uint32_t v) {                                         /* producer 하나 */
    uint32_t h = atomic_load_explicit(&q_head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q_tail, memory_order_acquire);
    if (h - t == Q) return 0;
    q_data[h % Q] = v;
    atomic_store_explicit(&q_head, h + 1u, memory_order_release); /* 데이터 쓰기 → 공개 */
    return 1;
}

int q_pop(uint32_t *v) {                                         /* consumer 하나 */
    uint32_t t = atomic_load_explicit(&q_tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&q_head, memory_order_acquire);
    if (h == t) return 0;
    *v = q_data[t % Q];
    atomic_store_explicit(&q_tail, t + 1u, memory_order_release);
    return 1;
}

static _Atomic uint32_t s_stat_drops;
void count_drop(void) { atomic_fetch_add_explicit(&s_stat_drops, 1u, memory_order_relaxed); }
```

### Q27. What is reentrancy, and how is it different from thread safety?

> "A reentrant function can be interrupted and called again before the first call finishes — usually because it uses only its arguments and locals, no static or global state. Thread-safe means it's correct when called from multiple threads, possibly using locks. A function that takes a mutex is thread-safe but not reentrant from an ISR, because the ISR would block or deadlock."

---

## 9. C++ (임베디드에서 묻는 것)

### Q28. struct vs class?

> "In C++ the only difference is default access: struct members are public by default, class members are private. By convention I use struct for plain data and class when there's an invariant to protect."

### Q29. What is RAII, and why is it useful in firmware?

> "Resource Acquisition Is Initialization: a constructor acquires a resource and the destructor releases it, so release happens on every exit path automatically. In firmware the best examples are a scoped interrupt lock and a mutex guard — you can't forget to re-enable interrupts on an early return."

```cpp
#include <cstdint>

namespace hw {                                   // 호스트 컴파일용 가짜 HAL
inline std::uint32_t irq_save() { return 0; }
inline void irq_restore(std::uint32_t) {}
}

class IrqGuard {
public:
    IrqGuard() : saved_(hw::irq_save()) {}
    ~IrqGuard() { hw::irq_restore(saved_); }
    IrqGuard(const IrqGuard&) = delete;           // 복사 금지: 두 번 복원되면 안 된다
    IrqGuard& operator=(const IrqGuard&) = delete;
private:
    std::uint32_t saved_;
};

static volatile std::uint32_t g_shared;

bool update(std::uint32_t v) {
    IrqGuard g;                                  // 여기서 IRQ 끔
    if (v == 0) return false;                    // 이른 return에서도 소멸자가 복원
    g_shared = g_shared + v;
    return true;
}                                                // 여기서 IRQ 복원
```

### Q30. In what order are constructors and destructors called?

> "Base classes first, then members in the order they're declared in the class — not the order in the initializer list — then the constructor body. Destruction is the exact reverse. Compilers warn with -Wreorder when the initializer list order differs, because initializing one member from another can read an uninitialized value."

### Q31. What does a virtual function cost on an MCU?

> "Each polymorphic object carries one vtable pointer, each class has one vtable in flash, and a virtual call is an indirect branch that usually can't be inlined. That's cheap — a few cycles and a few bytes — so it's fine for driver interfaces. I'd avoid it in the innermost per-sample loop, and if the type is known at compile time, templates give static polymorphism with zero overhead. And a class meant for polymorphic deletion needs a virtual destructor."

```cpp
#include <cstdint>

struct ImuDriver {
    virtual ~ImuDriver() = default;              // 기반 포인터로 delete할 거면 필수
    virtual bool read(std::int16_t (&xyz)[3]) = 0;
};

struct FakeImu final : ImuDriver {
    bool read(std::int16_t (&xyz)[3]) override { xyz[0] = 0; xyz[1] = 0; xyz[2] = 16384; return true; }
};

template <typename Driver>                       // 정적 다형성: 타입이 컴파일 시 정해지면 vtable 없음
std::int16_t read_z(Driver& d) { std::int16_t v[3]{}; d.read(v); return v[2]; }

std::int16_t demo_virtual() {
    FakeImu imu;
    ImuDriver& base = imu;
    std::int16_t a[3]{};
    base.read(a);                                // 간접 호출
    return static_cast<std::int16_t>(a[2] + read_z(imu));
}
```

### Q32. Templates vs macros?

> "Both generate code at compile time, but templates are type-checked, scoped, debuggable, and evaluate arguments once. The costs are code bloat if you instantiate for many types, and harder error messages. For a fixed-capacity container — a ring buffer of T with N slots — a template is the right tool."

```cpp
#include <array>
#include <cstddef>

template <typename T, std::size_t N>
class Ring {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of two");
public:
    bool push(const T& v) { if (count_ == N) return false; buf_[(head_ + count_) & (N - 1)] = v; ++count_; return true; }
    bool pop(T& out) { if (count_ == 0) return false; out = buf_[head_]; head_ = (head_ + 1) & (N - 1); --count_; return true; }
private:
    std::array<T, N> buf_{};
    std::size_t head_ = 0, count_ = 0;
};

int ring_demo() { Ring<int, 8> r; r.push(7); int v = 0; r.pop(v); return v; }
```

### Q33. What is `constexpr`, and why do embedded people like it?

> "constexpr lets the compiler evaluate a function or value at compile time. Lookup tables, CRC tables, register values, and baud divisors can be computed at build time and placed in flash, with static_assert checking them — no startup cost and no magic numbers."

```cpp
#include <cstdint>

constexpr std::uint32_t uart_div(std::uint32_t clk_hz, std::uint32_t baud) {
    return (clk_hz + baud / 2) / baud;           // 반올림
}

constexpr std::uint32_t kDiv = uart_div(84'000'000u, 420'000u);   // CRSF 420000 baud 예시
static_assert(kDiv == 200u, "unexpected divisor");
```

### Q34. References vs pointers?

> "A reference is an alias that must be bound at initialization and can't be reseated or null, so it documents 'this argument is required'. A pointer can be null and reassigned, so it fits optional arguments and ownership transfer in C APIs. Under the hood both are usually an address."

### Q35. What does const correctness mean in C++?

> "Mark everything that shouldn't change as const — parameters passed by const reference, and member functions that don't modify the object. A const member function can be called on a const object; the compiler enforces it, so it catches bugs and documents intent. `mutable` is the escape hatch for things like a cached value or a mutex inside a const method."

```cpp
#include <cstdint>

class Sensor {
public:
    std::int32_t last() const { ++reads_; return last_; }   // const 함수: 상태를 안 바꿈
    void set(std::int32_t v) { last_ = v; }
private:
    std::int32_t last_ = 0;
    mutable std::uint32_t reads_ = 0;                         // 통계용 예외
};

std::int32_t use_const(const Sensor& s) { return s.last(); }  // set()은 호출 불가
```

### Q36. Move semantics in one minute

> "Moving transfers resources from an object that's about to die instead of copying them — for a buffer, you steal the pointer and leave the source empty. `std::move` is just a cast that says 'you may move from this'. In firmware without heap allocation it matters less, but it's how unique_ptr transfers ownership and how containers avoid copies."

```cpp
#include <memory>
#include <utility>

struct Frame { int data[16]; };

std::unique_ptr<Frame> make_frame() { return std::make_unique<Frame>(); }

int move_demo() {
    std::unique_ptr<Frame> a = make_frame();
    std::unique_ptr<Frame> b = std::move(a);       // 소유권 이전, a는 이제 null
    return a == nullptr && b != nullptr;
}
```

### Q37. Smart pointers — which are OK on an MCU?

> "unique_ptr is basically free — same size as a raw pointer with a destructor — so it's fine when there's dynamic allocation at all, for example at init. shared_ptr adds a heap-allocated control block and atomic reference counting, so it costs memory and time; in firmware I'd rarely use it. Many flight codebases avoid the heap after init entirely, so ownership is mostly static."

### Q38. Why do many embedded codebases disable exceptions and RTTI?

> "Exceptions add unwind tables and runtime support that cost flash, and the timing of a throw is hard to bound — bad for real-time paths. RTTI adds type info for dynamic_cast and typeid. So builds often use `-fno-exceptions -fno-rtti`, and errors are returned as status codes, `std::optional`, or an expected-style result type."

### Q39. What is placement new?

> "It constructs an object in memory you already own, without allocating — for example in a static buffer or a memory pool. You then call the destructor explicitly when done. It's how you get C++ objects with constructors into pools or special memory sections."

```cpp
#include <new>
#include <cstdint>

struct Motor { explicit Motor(int id) : id_(id) {} ~Motor() {} int id_; };

alignas(Motor) static std::uint8_t g_motor_storage[sizeof(Motor)];

int placement_demo() {
    Motor* m = new (g_motor_storage) Motor(3);   // 할당 없음, 생성자만 실행
    int id = m->id_;
    m->~Motor();                                 // 소멸자는 직접 호출
    return id;
}
```

### Q40. std::array vs C array?

> "std::array has the same layout and zero overhead, but it knows its size, can be copied and returned by value, works with algorithms, and doesn't decay to a pointer. `at()` gives bounds checking when you want it."

```cpp
#include <array>
#include <cstdint>
#include <numeric>

std::uint32_t sum_array(const std::array<std::uint16_t, 4>& a) {   // 크기가 타입에 포함
    return std::accumulate(a.begin(), a.end(), 0u);
}

std::uint32_t array_demo() { std::array<std::uint16_t, 4> a{1, 2, 3, 4}; return sum_array(a) + a.size(); }
```

### Q41. What is the static initialization order fiasco?

> "Globals with dynamic initialization in different translation units are initialized in an unspecified order, so one global's constructor can use another global that isn't constructed yet. Fixes: make it constexpr or constant-initialized, or wrap it in a function with a local static that's constructed on first use. On bare metal, note that local statics may need guard variables — some projects use `-fno-threadsafe-statics` — and that startup code must actually run the constructors in `.init_array`."

```cpp
struct Config { int rate_hz = 500; };

Config& config() {                     // 처음 쓸 때 생성 → 순서 문제 회피
    static Config instance;
    return instance;
}

int init_demo() { return config().rate_hz; }
```

---

## 10. C 출신이 C++ 질문에서 자주 막히는 것

| 막히는 질문 | 핵심 한 줄 |
|---|---|
| "Why is your destructor not virtual?" | 기반 클래스 포인터로 delete할 클래스는 virtual 소멸자가 필수. 아니면 파생 소멸자가 안 불리는 UB |
| "What's the rule of three/five/zero?" | 소멸자·복사·이동 중 하나를 직접 쓰면 나머지도 생각하라. 가능하면 멤버가 관리하게 해서 아무것도 안 쓰는 게(zero) 최선 |
| "Why pass by const reference?" | 큰 객체 복사를 피하면서 수정 불가를 보장. 작은 타입(int, 포인터)은 값으로 |
| "What does `explicit` do?" | 인자 하나짜리 생성자의 암묵적 변환을 막는다. `Motor m = 3;` 같은 실수 방지 |
| "`nullptr` vs `NULL`?" | nullptr는 진짜 포인터 타입이라 오버로드에서 int로 착각되지 않는다 |
| "`static_cast` vs C cast?" | C 캐스트는 무엇이든 해 버린다. static_cast, reinterpret_cast, const_cast로 의도를 드러내고 검색 가능하게 |
| "What's in a class's memory?" | 비정적 멤버 + 패딩 + (virtual이 있으면) vptr 하나. 멤버 함수는 객체 안에 없다 |
| "Can you use the STL on an MCU?" | 할당 없는 것(array, algorithm, optional, string_view)은 OK. vector/map/string은 heap → init에서만, 또는 custom allocator |
| "`inline` in C++ means?" | 최적화 힌트라기보다 "여러 번 정의돼도 링크 에러 아님"(ODR). 헤더에 함수 본문을 둘 때 |
| "`enum class` why?" | 범위가 있고 int로 암묵 변환되지 않는다. 밑바탕 타입 지정 가능(`enum class Mode : uint8_t`) |

---

## 11. 흔한 함정 코드 읽기

### T1. 무엇이 문제인가?

```c
void t1(void) {
    char *s = "abc";
    s[0] = 'x';
}
```

- **답**: 문자열 리터럴 수정은 UB. 리터럴은 보통 `.rodata`(flash)에 있어서 MCU에서는 무시되거나 fault. `char s[] = "abc";`로 복사본을 만들거나 `const char *`로 선언

### T2. 무엇을 반환하나?

```c
int t2(void) {
    int i = -1;
    return i < sizeof(int);
}
```

- **답**: 0. `sizeof`는 `size_t`(unsigned)라서 -1이 아주 큰 unsigned 값으로 바뀐다. `-Wsign-compare`가 경고한다

### T3. 결과는?

```c
#define SQ(x) x * x
int t3(void) { return SQ(1 + 2); }
```

- **답**: 5 (`1 + 2*1 + 2`). 괄호를 쳐도 `SQ(i++)`는 두 번 증가 → inline 함수

### T4. 출력 값은?

```c
#include <stdio.h>
void t4(int a[10]) { printf("%zu\n", sizeof(a)); }
```

- **답**: 포인터 크기(64비트 호스트 8, Cortex-M 4). 배열 파라미터는 포인터로 decay. GCC가 `-Wsizeof-array-argument`로 경고

### T5. 왜 안 끝나나?

```c
#include <stdint.h>
int t5(void) {
    int n = 0;
    for (uint8_t i = 0; i < 256; i++) { n++; if (n > 1000) break; }
    return n;
}
```

- **답**: `uint8_t`는 255 다음 0으로 돌아가서 `i < 256`이 항상 참. (여기선 break로 탈출하게 만들어 둠) 루프 변수는 범위를 담을 수 있는 타입으로

### T6. 최적화를 켜면 왜 멈추나?

```c
static int g_done;                   /* ISR이 1로 바꾼다고 하자 */
void t6_wait(void) { while (!g_done) { } }
void t6_isr(void) { g_done = 1; }
```

- **답**: `volatile`이 없어서 컴파일러가 `g_done`을 한 번만 읽고 무한 루프로 만들 수 있다. `-O0`에서는 되고 `-O2`에서만 멈추는 고전적인 버그. `volatile int g_done;` (그리고 데이터를 넘긴다면 순서 보장까지)

### T7. 무엇이 새나?

```cpp
#include <cstdio>
struct Base { ~Base() { std::puts("~Base"); } };
struct Derived : Base { int* buf = new int[16]; ~Derived() { delete[] buf; } };
void t7() { Base* p = new Derived; delete p; }
```

- **답**: `Base`의 소멸자가 virtual이 아니어서 `delete p`는 UB이고, 실제로는 `~Derived`가 안 불려 `buf`가 샌다. `virtual ~Base()`

### T8. 무엇이 잘못됐나?

```c
#include <stdio.h>
const char *t8_name(int id) {
    char buf[16];
    snprintf(buf, sizeof buf, "motor%d", id);
    return buf;
}
```

- **답**: 지역 배열의 주소를 반환 → 함수가 끝나면 스택에서 사라진다(dangling). 호출자가 버퍼를 넘기게 하거나(`char *out, size_t n`), static 버퍼(재진입 불가)를 쓴다

---

## 12. Don 경험과 연결할 문장

- "In SSD firmware on multi-core Cortex-R, the bugs that cost the most were almost always about shared state between cores, DMA, and interrupts — so I'm careful about what's atomic and what needs ordering." (Don: 실제 사례)
- "I serialize telemetry field by field rather than with packed structs — the NVMe telemetry feature I shipped had to stay parseable across firmware releases." (Don: 실제 호환성 관리 방식 확인)
- "For error handling in production firmware, I used status codes and a central error-reporting path rather than anything exception-like, because the behavior has to be bounded and observable." (Don: 실제 구조)

## 체크

- [ ] volatile이 무엇을 보장하고 무엇을 보장하지 않는지 30초에 말할 수 있다
- [ ] static의 세 가지 의미, const 포인터 표를 보지 않고 설명할 수 있다
- [ ] 비트 set/clear/toggle/test와 필드 RMW를 화이트보드에 1분 안에 쓴다
- [ ] 정수 promotion과 signed/unsigned 비교 함정을 예시와 함께 말할 수 있다
- [ ] UB 예시 5개를 바로 댈 수 있다
- [ ] Cortex-M에서 무엇이 atomic인지, M0와 M3+의 차이를 말할 수 있다
- [ ] release/acquire 패턴을 SPSC 큐로 설명할 수 있다
- [ ] RAII IRQ guard, virtual 소멸자, placement new를 코드로 쓸 수 있다
- [ ] 함정 코드 8개를 보고 바로 답할 수 있다
