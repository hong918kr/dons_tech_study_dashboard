/* L03 예제 — 각 구역은 실제로 컴파일·실행된다. main() 의 assert 가 '결과'다.
 *   make -C lecture/code_c test      (-Wall -Wextra -Werror + ASan/UBSan)
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// --- types ---
static void demo_types(void)
{
    /* 고정 폭 타입: 레지스터 · 프로토콜 · 파일 포맷에는 int 대신 이것 */
    assert(sizeof(uint8_t) == 1 && sizeof(uint16_t) == 2 && sizeof(uint32_t) == 4 && sizeof(uint64_t) == 8);

    uint8_t u8 = 255;
    u8++;                                   /* unsigned 는 wrap: 255 + 1 == 0 (정의된 동작) */
    assert(u8 == 0);

    int8_t s8 = (int8_t)200;                /* 범위 밖 → 구현 정의 (보통 -56) */
    assert(s8 == -56);

    /* 함정 1: signed vs unsigned 비교 — -1 이 unsigned 로 바뀌어 아주 큰 수가 된다 */
    int neg = -1;
    unsigned int one = 1;
    assert((neg < (int)one) && !((unsigned)neg < one));

    /* 함정 2: integer promotion — uint8_t 끼리 연산하면 int 로 올라간다 */
    uint8_t a = 200, b = 100;
    assert(a + b == 300);                   /* int 로 계산 */
    assert((uint8_t)(a + b) == 44);         /* 다시 넣을 때 잘린다 */
}
// --- end ---

// --- bits ---
#define BIT(n)            (1u << (n))
#define SET_BIT(r, n)     ((r) |= BIT(n))
#define CLEAR_BIT(r, n)   ((r) &= ~BIT(n))
#define TOGGLE_BIT(r, n)  ((r) ^= BIT(n))
#define TEST_BIT(r, n)    (((r) >> (n)) & 1u)

/* reg 의 [shift, shift+width) 필드 읽기 / 쓰기 (width < 32) */
static uint32_t field_get(uint32_t reg, unsigned shift, unsigned width)
{
    return (reg >> shift) & ((1u << width) - 1u);
}

static uint32_t field_set(uint32_t reg, unsigned shift, unsigned width, uint32_t val)
{
    uint32_t mask = ((1u << width) - 1u) << shift;
    return (reg & ~mask) | ((val << shift) & mask);
}

static int popcount(uint32_t v)                 /* 1 인 비트 수 */
{
    int n = 0;
    while (v) {
        v &= v - 1;                             /* 가장 낮은 1 비트 지우기 */
        n++;
    }
    return n;
}

static bool is_pow2(uint32_t v) { return v && !(v & (v - 1)); }

static void demo_bits(void)
{
    uint32_t r = 0;
    SET_BIT(r, 3);
    SET_BIT(r, 0);
    assert(r == 0x9);
    CLEAR_BIT(r, 0);
    TOGGLE_BIT(r, 4);
    assert(r == 0x18 && TEST_BIT(r, 4) == 1 && TEST_BIT(r, 0) == 0);

    assert(field_get(0xABCD1234u, 8, 8) == 0x12);
    assert(field_set(0xFFFFFFFFu, 4, 4, 0x0) == 0xFFFFFF0Fu);
    assert(popcount(0xF0F0u) == 8);
    assert(is_pow2(64) && !is_pow2(0) && !is_pow2(12));
}
// --- end ---

// --- pointers ---
static void swap(int *x, int *y)
{
    int t = *x;
    *x = *y;
    *y = t;
}

typedef struct {
    uint16_t id;
    int16_t  value;
} sample_t;

static void demo_pointers(void)
{
    int a = 1, b = 2;
    swap(&a, &b);                           /* 주소를 넘겨야 바깥 값이 바뀐다 */
    assert(a == 2 && b == 1);

    uint16_t arr[4] = {10, 20, 30, 40};
    uint16_t *p = arr;                      /* 배열 이름 = 첫 원소의 주소 */
    assert(*(p + 2) == 30 && p[2] == 30);   /* p + 2 는 2 '원소' 뒤 (4 바이트 뒤) */
    assert((uint8_t *)(p + 1) - (uint8_t *)p == 2);

    sample_t s = {7, -3};
    sample_t *ps = &s;
    ps->value = 5;                          /* (*ps).value 와 같다 */
    assert(s.value == 5);

    const uint8_t *ro = (const uint8_t *)arr;   /* 가리키는 값을 못 바꿈 */
    uint8_t *const fixed = (uint8_t *)arr;      /* 포인터 자체를 못 바꿈 */
    (void)ro;
    (void)fixed;
}
// --- end ---

// --- strings ---
static size_t my_strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

static void reverse_in_place(char *s)
{
    size_t n = my_strlen(s);
    if (n < 2)
        return;
    for (size_t i = 0, j = n - 1; i < j; i++, j--) {
        char t = s[i];
        s[i] = s[j];
        s[j] = t;
    }
}

static int my_atoi(const char *s)               /* 부호 + 숫자만, 오버플로 검사는 생략 */
{
    int sign = 1, v = 0;
    if (*s == '-' || *s == '+')
        sign = (*s++ == '-') ? -1 : 1;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return sign * v;
}

static void demo_strings(void)
{
    char buf[16] = "neros";                     /* 문자열 = '\0' 로 끝나는 char 배열 */
    assert(my_strlen(buf) == 5 && sizeof buf == 16);
    reverse_in_place(buf);
    assert(strcmp(buf, "soren") == 0);
    assert(my_atoi("-123") == -123 && my_atoi("42x") == 42);

    char small[4];
    int n = snprintf(small, sizeof small, "%d", 12345);  /* 잘려도 항상 '\0' 로 끝난다 */
    assert(n == 5 && strcmp(small, "123") == 0);          /* n >= sizeof → 잘렸다는 뜻 */
}
// --- end ---

// --- memcpy ---
static void *my_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

static void *my_memmove(void *dst, const void *src, size_t n)   /* 겹쳐도 안전 */
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;                        /* 뒤에서부터 복사 */
    }
    return dst;
}

static void demo_memcpy(void)
{
    uint8_t a[6] = {1, 2, 3, 4, 5, 6}, b[6] = {0};
    my_memcpy(b, a, 6);
    assert(memcmp(a, b, 6) == 0);
    my_memmove(a + 1, a, 4);                    /* 겹치는 복사: 오른쪽으로 한 칸 */
    assert(a[0] == 1 && a[1] == 1 && a[4] == 4 && a[5] == 6);
}
// --- end ---

// --- endian ---
static bool is_little_endian(void)
{
    uint16_t x = 0x0001;
    return *(uint8_t *)&x == 0x01;              /* 낮은 주소에 낮은 바이트 → little */
}

static uint16_t rd_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t bswap32(uint32_t v)
{
    return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
}

static void demo_endian(void)
{
    assert(is_little_endian());                 /* Apple Silicon 도, 거의 모든 Cortex-M 도 little */
    const uint8_t wire[4] = {0x12, 0x34, 0x56, 0x78};
    assert(rd_le16(wire) == 0x3412);
    assert(rd_be32(wire) == 0x12345678u);
    assert(bswap32(0x12345678u) == 0x78563412u);
}
// --- end ---

// --- keywords ---
static int counter_calls(void)
{
    static int calls;                           /* 함수 안 static: 호출 사이에 값 유지, 0 으로 시작 */
    return ++calls;
}

static volatile uint32_t g_tick;                /* ISR 이 바꾸는 값: 매번 메모리에서 다시 읽게 */

void SysTick_Handler(void);                     /* 실제 MCU 에선 벡터 테이블에 등록되는 ISR */
void SysTick_Handler(void) { g_tick++; }

static void demo_keywords(void)
{
    assert(counter_calls() == 1 && counter_calls() == 2);
    SysTick_Handler();
    assert(g_tick == 1);
    const int limit = 10;                       /* 읽기 전용 (컴파일러가 대입을 막는다) */
    assert(limit == 10);
}
// --- end ---

// --- structs ---
typedef struct {
    uint8_t  a;     /* 1 바이트, 뒤에 패딩 3 */
    uint32_t b;     /* 4 바이트 정렬 */
    uint8_t  c;     /* 1 바이트, 뒤에 패딩 3 */
} padded_t;

typedef struct {
    uint32_t b;     /* 큰 것부터 놓으면 패딩이 준다 */
    uint8_t  a;
    uint8_t  c;
} reordered_t;

static void demo_structs(void)
{
    assert(sizeof(padded_t) == 12);             /* 1+3, 4, 1+3 */
    assert(sizeof(reordered_t) == 8);           /* 4, 1, 1, +2 */
    assert(offsetof(padded_t, b) == 4);
    /* 그래서 wire 포맷은 struct 를 그대로 memcpy 하지 말고 바이트 단위로 직렬화 (rd_le16 처럼) */
}
// --- end ---

// --- macros ---
#define SQUARE_BAD(x)  x * x
#define SQUARE(x)      ((x) * (x))
#define ARRAY_LEN(a)   (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b)      ((a) < (b) ? (a) : (b))

static void demo_macros(void)
{
    assert(SQUARE_BAD(1 + 2) == 5);             /* 1 + 2 * 1 + 2 → 괄호 없는 매크로 함정 */
    assert(SQUARE(1 + 2) == 9);
    int arr[7];
    (void)arr;
    assert(ARRAY_LEN(arr) == 7);                /* 포인터로 받으면 안 된다 (sizeof 포인터) */
    int i = 3;
    int m = MIN(i, 5);                          /* MIN(i++, 5) 는 i 가 두 번 증가할 수 있다 */
    assert(m == 3);
}
// --- end ---

// --- ring ---
#define RB_SIZE 8u                              /* 2 의 거듭제곱 */
typedef struct {
    uint8_t  buf[RB_SIZE];
    uint32_t head, tail;                        /* free-running: 마스크는 접근할 때만 */
} rb_t;

static bool rb_push(rb_t *rb, uint8_t v)
{
    if (rb->head - rb->tail == RB_SIZE)
        return false;                           /* full → drop-new */
    rb->buf[rb->head++ & (RB_SIZE - 1)] = v;
    return true;
}

static bool rb_pop(rb_t *rb, uint8_t *v)
{
    if (rb->head == rb->tail)
        return false;                           /* empty */
    *v = rb->buf[rb->tail++ & (RB_SIZE - 1)];
    return true;
}

static void demo_ring(void)
{
    rb_t rb = {{0}, UINT32_MAX - 2, UINT32_MAX - 2};    /* 인덱스 wrap 까지 */
    uint8_t v;
    for (uint8_t i = 0; i < 8; i++)
        assert(rb_push(&rb, i));
    assert(!rb_push(&rb, 99));
    for (uint8_t i = 0; i < 8; i++)
        assert(rb_pop(&rb, &v) && v == i);
    assert(!rb_pop(&rb, &v));
}
// --- end ---

// --- crc ---
static uint8_t checksum_xor(const uint8_t *p, size_t n)
{
    uint8_t x = 0;
    while (n--)
        x ^= *p++;
    return x;
}

static uint8_t crc8(const uint8_t *p, size_t n)        /* poly 0x07, init 0 (CRC-8/SMBUS) */
{
    uint8_t crc = 0;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

static void demo_crc(void)
{
    const uint8_t msg[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    assert(crc8(msg, sizeof msg) == 0xF4);      /* 표준 check 값으로 구현 검증 */
    const uint8_t f[] = {0x01, 0x02, 0x03};
    assert(checksum_xor(f, 3) == 0x00);         /* XOR 은 두 바이트가 같이 틀리면 못 잡는다 */
}
// --- end ---

// --- timing ---
/* tick 이 32비트로 wrap 해도 맞는 timeout 검사 — unsigned 뺄셈 */
static bool timed_out(uint32_t now, uint32_t start, uint32_t timeout)
{
    return (uint32_t)(now - start) >= timeout;
}

typedef struct {
    bool    stable;
    uint8_t count;
} debounce_t;

static bool debounce(debounce_t *d, bool raw)   /* 3 번 연속 같은 값이어야 바뀐다 */
{
    if (raw == d->stable)
        d->count = 0;
    else if (++d->count >= 3) {
        d->stable = raw;
        d->count = 0;
    }
    return d->stable;
}

static void demo_timing(void)
{
    assert(!timed_out(105, 100, 10) && timed_out(110, 100, 10));
    assert(timed_out(5, UINT32_MAX - 4, 10));          /* wrap 을 넘어 10 tick */
    assert(!timed_out(UINT32_MAX - 1, UINT32_MAX - 4, 10));

    debounce_t d = {false, 0};
    assert(!debounce(&d, true) && !debounce(&d, true) && debounce(&d, true));
    assert(debounce(&d, false) && debounce(&d, true));  /* 한 번 튀는 건 무시 */
}
// --- end ---

// --- state_machine ---
typedef enum { ST_IDLE, ST_ARMED, ST_FLYING } state_t;
typedef enum { EV_ARM, EV_DISARM, EV_TAKEOFF, EV_LAND } event_t;

static state_t step(state_t s, event_t e)
{
    switch (s) {
    case ST_IDLE:   return (e == EV_ARM) ? ST_ARMED : s;
    case ST_ARMED:  return (e == EV_TAKEOFF) ? ST_FLYING : (e == EV_DISARM) ? ST_IDLE : s;
    case ST_FLYING: return (e == EV_LAND) ? ST_ARMED : s;     /* 비행 중 disarm 은 무시 */
    }
    return s;
}

static void demo_state_machine(void)
{
    assert(step(ST_IDLE, EV_TAKEOFF) == ST_IDLE);        /* 허용 안 되는 전이는 무시 */
    assert(step(step(ST_IDLE, EV_ARM), EV_TAKEOFF) == ST_FLYING);
    assert(step(ST_FLYING, EV_DISARM) == ST_FLYING);
}
// --- end ---

// --- linked_list ---
typedef struct node {
    int val;
    struct node *next;
} node_t;

static node_t *reverse_list(node_t *head)
{
    node_t *prev = NULL;
    while (head) {
        node_t *next = head->next;      /* 1) 다음을 기억 */
        head->next = prev;              /* 2) 화살표를 뒤로 */
        prev = head;                    /* 3) 한 칸씩 전진 */
        head = next;
    }
    return prev;
}

static void demo_linked_list(void)
{
    node_t c = {3, NULL}, b = {2, &c}, a = {1, &b};
    node_t *h = reverse_list(&a);
    assert(h == &c && h->next == &b && h->next->next == &a && a.next == NULL);
    assert(reverse_list(NULL) == NULL);
}
// --- end ---

// --- test_style ---
/* 테스트 엔지니어가 C 함수를 받으면: 경계값 표 하나로 */
static int clamp_percent(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

static void demo_test_style(void)
{
    static const struct { int in, want; } cases[] = {
        {-1, 0}, {0, 0}, {1, 1}, {50, 50}, {99, 99}, {100, 100}, {101, 100}, {-2147483647 - 1, 0},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int got = clamp_percent(cases[i].in);
        if (got != cases[i].want)
            printf("case %zu: in=%d got=%d want=%d\n", i, cases[i].in, got, cases[i].want);
        assert(got == cases[i].want);
    }
}
// --- end ---

int main(void)
{
    demo_types();
    demo_bits();
    demo_pointers();
    demo_strings();
    demo_memcpy();
    demo_endian();
    demo_keywords();
    demo_structs();
    demo_macros();
    demo_ring();
    demo_crc();
    demo_timing();
    demo_state_machine();
    demo_linked_list();
    demo_test_style();
    puts("L03: all examples PASS");
    return 0;
}
