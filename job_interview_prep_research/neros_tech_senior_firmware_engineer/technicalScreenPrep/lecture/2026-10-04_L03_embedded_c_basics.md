# 🔧 L03 · 임베디드 C 기본기 — 테스트 엔지니어가 알아야 할 것만, 가볍게 복습

> 이번 스크린 면접관은 Full Stack 엔지니어로 추정되니 [추정] 임베디드 C를 깊게 파고들기보다 **"C 코드를 읽고, 흔한 버그를 알아보고, 테스트할 줄 아나"** 정도를 볼 가능성이 크다. 그래서 이 강의는 **짧은 코드 + 한 줄 답** 위주다. 코드는 전부 `lecture/code_c/l03_embedded_c.c` 한 파일이고, `-Wall -Wextra -Werror` + **ASan/UBSan**으로 컴파일해서 모든 `assert`가 통과한 것이다 (`make -C lecture/code_c test` → `L03: all examples PASS`). 더 깊은 C/C++ 질문은 온사이트용 [onsitePrep N04 C/C++ 기본기](../../onsitePrep/site/notes/2026-10-01_N04_c_cpp_fundamentals.html) · [N07 임베디드 퀴즈](../../onsitePrep/site/notes/2026-10-01_N07_embedded_quiz_bank.html)에 있다.

## 0. 한 장 요약 — 나오면 이 한 줄로

| 주제 | 30초 답의 핵심 |
|---|---|
| 고정 폭 타입 | 레지스터 · 프로토콜엔 `uint8_t`/`uint32_t`. `int` 크기는 플랫폼마다 다르다 |
| signed / unsigned | 섞어서 비교하면 `-1`이 큰 unsigned가 된다. unsigned overflow는 wrap(정의됨), signed overflow는 UB |
| 비트 | set `|=`, clear `&= ~`, toggle `^=`, test `>> n & 1`. 필드 = shift + mask |
| 포인터 | 주소. `p + 1`은 한 **원소** 뒤. `->`는 구조체 포인터 멤버 |
| 문자열 | `'\0'`로 끝나는 char 배열. 길이 + 1 바이트 필요. `snprintf`는 항상 끝을 막는다 |
| memcpy vs memmove | 영역이 **겹치면 memmove** |
| 엔디언 | little = 낮은 주소에 낮은 바이트. wire 데이터는 바이트 단위로 조립 |
| `volatile` | "매번 메모리에서 다시 읽어라" — ISR · 레지스터. **원자성 · 순서 보장은 아님** |
| `static` | 함수 안: 값 유지 / 파일 범위: 이 파일 안에서만 보임 |
| `const` | 읽기 전용 (보통 flash에 놓인다) |
| 구조체 패딩 | 정렬 때문에 빈 바이트가 생긴다 → `sizeof` ≠ 필드 합. wire 포맷에 struct를 그대로 쓰지 말 것 |
| 매크로 | 인자마다 괄호, 전체에 괄호, 부작용 있는 인자 금지, 여러 문장은 `do { } while (0)` |
| ring buffer | 2의 거듭제곱 크기 + free-running index + 마스크 ([L00](2026-10-04_L00_special_lecture.md)) |
| timeout | `(uint32_t)(now - start) >= timeout` — tick이 wrap 해도 맞다 |
| ISR | 짧게, 블로킹 금지, 플래그 · 링에 넣고 main에서 처리 |

## 1. 고정 폭 타입과 정수 함정

```c
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
```

| 질문 | 답 |
|---|---|
| 왜 `int` 대신 `uint32_t`? | `int`는 16비트일 수도 32비트일 수도 있다. 레지스터 · 패킷은 크기가 정확해야 한다 |
| `uint8_t` 255 + 1? | 0. **unsigned는 wrap이 정의된 동작** |
| `int` 최댓값 + 1? | **UB(정의되지 않은 동작)**. 컴파일러가 "안 일어난다"고 가정하고 최적화할 수 있다 |
| `-1 < 1u`는? | **거짓**. `-1`이 unsigned로 바뀌어 4294967295가 된다 → `-Wsign-compare` 경고를 켜 둔다 |
| `uint8_t a=200, b=100; a+b`? | **300** — 계산은 `int`로 올라가서 한다(integer promotion). 다시 `uint8_t`에 넣으면 44 |

## 2. 비트 조작 — 가장 자주 나온다

```c
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
```

- 말로 할 것: "set은 OR, clear는 AND NOT, toggle은 XOR, test는 shift 후 AND 1"
- 레지스터 필드 쓰기 = **read-modify-write**: 읽고 → 그 필드만 지우고 → 새 값 OR → 쓰기. ISR도 같은 레지스터를 건드리면 그 사이에 끼어들 수 있다 → 인터럽트를 잠깐 끄거나, 하드웨어의 set/clear 전용 레지스터(예: STM32 `BSRR`)를 쓴다
- `v & (v - 1)` = 가장 낮은 1비트 지우기 → popcount, 2의 거듭제곱 검사

## 3. 포인터

```c
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
```

| 선언 | 뜻 |
|---|---|
| `const uint8_t *p` | 가리키는 **값**을 못 바꿈 (읽기 전용 버퍼를 받을 때) |
| `uint8_t *const p` | **포인터 자체**를 못 바꿈 (고정 주소) |
| `volatile uint32_t *reg` | 그 주소의 값을 매번 다시 읽음 (레지스터) |
| `p[i]` | `*(p + i)`와 같다 |

- 흔한 버그: NULL 역참조, 해제한 메모리 사용, 지역 변수의 주소를 반환, 배열 범위 밖 접근 → **테스트에서 ASan으로 잡는다**

## 4. 문자열

```c
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
```

- `char buf[16]`에 넣을 수 있는 글자는 **15개** (마지막은 `'\0'`)
- `strcpy` · `sprintf` · `gets`는 길이를 모른다 → `snprintf` · `strncpy`(끝 처리 주의) · 직접 길이 검사
- `snprintf` 반환값이 버퍼 크기 이상이면 **잘린 것** — 테스트할 때 확인할 포인트

## 5. memcpy vs memmove

```c
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
```

- `memcpy`는 영역이 겹치면 **UB**. 겹칠 수 있으면 `memmove` (뒤에서부터 복사해서 덮어쓰기를 피한다)
- 면접 단골: "memcpy를 직접 짜 봐" → 바이트 루프 + "겹치면 memmove"까지 말하면 충분

## 6. 엔디언

```c
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
```

- 네트워크 · 많은 센서 레지스터는 **big-endian**, MCU 메모리는 보통 little
- 받은 바이트를 `*(uint32_t *)buf`로 읽으면 엔디언 + **정렬(alignment)** 둘 다 문제 → 시프트로 조립 (위의 `rd_le16`, `rd_be32`)

## 7. volatile · static · const · extern

```c
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
```

| 키워드 | 한 줄 | 흔한 오해 |
|---|---|---|
| `volatile` | 컴파일러가 읽기 · 쓰기를 생략하거나 레지스터에 캐시하지 못하게 | "volatile이면 스레드 · ISR에 안전하다" → **아니다**. 원자성 · 메모리 순서는 보장 안 함 |
| `static` (함수 안) | 호출 사이에 값 유지, 0으로 초기화 (.bss) | 스택이 아니라 전역 메모리에 있다 |
| `static` (파일 범위) | 이 `.c` 파일 밖에서 안 보임 (내부 링크) | 이름 충돌 방지, 캡슐화 |
| `const` | 읽기 전용. 전역 const는 보통 flash(.rodata) | `#define`과 달리 타입이 있다 |
| `extern` | "정의는 다른 파일에 있다"는 선언 | 헤더에 정의(=값)를 넣으면 다중 정의 에러 |
| `volatile const` | 프로그램은 못 쓰지만 하드웨어가 바꾸는 값 (읽기 전용 상태 레지스터) | 모순 아님 |

## 8. 구조체 패딩

```c
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
```

- 이 Mac(arm64)에서 실제 `sizeof`: `padded_t` = **12**, 필드 순서만 바꾼 `reordered_t` = **8**
- `__attribute__((packed))`로 패딩을 없앨 수 있지만, 정렬 안 된 접근이 느리거나(일부 코어에선 fault) 이식성이 떨어진다 → wire 포맷은 **바이트 직렬화**가 정석
- 테스트 포인트: `_Static_assert(sizeof(hdr_t) == 4, "...")`로 레이아웃을 컴파일 타임에 고정

## 9. 매크로 함정

```c
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
```

- 규칙 4개: **인자마다 괄호 · 전체에 괄호 · 부작용 있는 인자(`i++`) 금지 · 여러 문장은 `do { ... } while (0)`**
- 가능하면 `static inline` 함수가 낫다 (타입 검사, 인자 한 번만 평가, 디버거에서 보임)

## 10. Ring buffer (C)

```c
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
```

- 핵심 세 줄: **크기는 2의 거듭제곱, head/tail은 마스크 없이 계속 증가, `head - tail`이 개수** (unsigned wrap 덕분에 2^32를 넘어도 맞다)
- ISR이 push, main이 pop이면 → head는 ISR만, tail은 main만 쓴다(단일 writer). 멀티코어 · 최적화까지 생각하면 C11 atomics의 acquire/release ([onsitePrep N05](../../onsitePrep/site/notes/2026-10-01_N05_ring_buffer_onsite.html))
- Python 버전과 비교: [L00 Part 1](2026-10-04_L00_special_lecture.md)

## 11. 체크섬 · CRC

```c
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
```

- 구현이 맞는지는 **표준 check 값**으로: CRC-8/SMBUS(poly 0x07)는 `"123456789"` → `0xF4`
- XOR 체크섬은 같은 비트가 두 번 틀리면 못 잡는다 → 링크에는 CRC

## 12. 시간 — tick wrap과 디바운스

```c
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
```

- `now >= start + timeout`은 **wrap 직전에 틀린다** (`start + timeout`이 넘쳐서 작은 수가 됨) → 항상 `now - start` 형태로
- 디바운스: 버튼 · 리밋 스위치 · 노이즈 많은 입력. "N번 연속 같은 값일 때만 바뀐다"

## 13. 상태 머신

```c
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
```

- 테스트 관점: **모든 (상태, 이벤트) 조합**을 표로 돌린다 — 허용 안 되는 전이가 무시되는지가 버그의 단골 자리

## 14. 연결 리스트 뒤집기 (C 코딩 질문 단골)

```c
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
```

- 말로: "prev, curr, next 세 포인터. 다음을 기억하고, 화살표를 뒤집고, 한 칸 전진." 빈 리스트 · 노드 1개 경계 확인

## 15. 메모리 맵 — 변수는 어디에 사나

| 영역 | 무엇 | 예 |
|---|---|---|
| `.text` (flash) | 코드 | 함수 |
| `.rodata` (flash) | 읽기 전용 상수 | `const uint8_t table[] = {...}`, 문자열 리터럴 |
| `.data` (RAM, 초기값은 flash에서 복사) | 초기값 있는 전역 · static | `int x = 5;` |
| `.bss` (RAM, 0으로 초기화) | 초기값 없는 전역 · static | `static int count;` |
| stack (RAM) | 지역 변수, 함수 호출 | `uint8_t buf[64];` (함수 안) |
| heap (RAM) | `malloc` | 임베디드에선 **잘 안 쓴다** — 단편화 · 실패 처리 · 시간 비결정성 |

- 테스트 포인트: 스택 오버플로(큰 지역 배열, 깊은 재귀) — 스택 끝에 패턴(0xDEADBEEF)을 채우고 나중에 지워졌는지 확인하는 **stack painting**, map 파일로 RAM/flash 사용량 추적

## 16. ISR(인터럽트) 규칙

| 규칙 | 왜 |
|---|---|
| 짧게 — 플래그 세우기 · 링에 넣기만 | 다른 인터럽트 · 메인 루프 지연 |
| 블로킹 금지 (`printf`, `malloc`, 대기 루프, mutex) | ISR은 기다릴 수 없다 |
| ISR과 공유하는 변수는 `volatile` + 원자적 접근 | 컴파일러 캐시 방지, 찢어진 읽기 방지 |
| main에서 여러 바이트 공유 데이터를 읽을 땐 짧게 인터럽트 끄기(critical section) 또는 단일 writer 구조 | 읽는 도중 값이 바뀜 |
| 인터럽트 플래그를 지운다 | 안 지우면 무한히 다시 들어온다 |

> 영어 한 줄: **"Keep the ISR short: capture the data, push it into a ring buffer or set a flag, clear the interrupt, and do the real work in the main loop or a task."**

## 17. 테스트 엔지니어 관점 — C 코드를 받으면

```c
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
```

| 할 것 | 어떻게 |
|---|---|
| 경계값 표 | 최솟값 −1, 최솟값, 최솟값 +1, 중간, 최댓값 −1, 최댓값, 최댓값 +1, 타입 극단값(`INT_MIN`) |
| 컴파일러에게 먼저 시키기 | `-Wall -Wextra -Werror` (sign-compare, unused, implicit 변환) |
| 런타임 버그 잡기 | host 테스트를 **ASan/UBSan**으로 (범위 밖 접근, use-after-free, signed overflow) |
| 레이아웃 고정 | `_Static_assert(sizeof(...))` |
| HAL은 mock | 하드웨어 없이 재시도 · 에러 경로 ([L02 Part A](2026-10-04_L02_embedded_test_frameworks.md)) |
| C를 Python에서 | ctypes로 같은 시나리오를 pytest로 ([문제 07](../python/problems/07_ctypes_c_ring_buffer.md)) |

## 18. 빠른 퀴즈 20 — 가리고 답해 보기

| # | 질문 | 답 |
|---|---|---|
| 1 | `uint8_t x = 0; x--;` 결과? | 255 (unsigned wrap) |
| 2 | `int` overflow는? | UB |
| 3 | 3번 비트 set / clear | `r |= 1u << 3;` / `r &= ~(1u << 3);` |
| 4 | 2의 거듭제곱 검사 | `v && !(v & (v - 1))` |
| 5 | `volatile`이 필요한 곳 3개 | 하드웨어 레지스터, ISR과 공유하는 변수, (setjmp 이후 지역 변수) |
| 6 | `volatile`이면 thread-safe? | 아니다. 원자성 · 순서 보장 없음 |
| 7 | 함수 안 `static` 변수 | 호출 사이 값 유지, 0 초기화, .bss/.data |
| 8 | `const int *p` vs `int *const p` | 값 읽기 전용 vs 포인터 고정 |
| 9 | `sizeof(arr)`를 함수 인자로 받은 배열에 쓰면? | 포인터 크기가 나온다 (배열 길이 아님) |
| 10 | memcpy vs memmove | 겹치면 memmove |
| 11 | little-endian 판별 | `uint16_t x = 1; *(uint8_t *)&x == 1` |
| 12 | `struct {uint8_t a; uint32_t b;}` 크기 | 보통 8 (패딩 3) |
| 13 | `#define SQ(x) x*x`의 `SQ(1+2)` | 5 |
| 14 | tick wrap 안전한 timeout | `(uint32_t)(now - start) >= timeout` |
| 15 | ring buffer full/empty 구분법 | count / 한 칸 비우기 / free-running index |
| 16 | ISR에서 `printf`? | 하지 않는다 (블로킹, 재진입 불가) |
| 17 | 임베디드에서 `malloc`을 피하는 이유 | 단편화, 실패 처리, 실행 시간 비결정성 |
| 18 | 초기값 없는 전역 변수는 어디? | .bss (시작 시 0) |
| 19 | 스택 오버플로를 어떻게 찾나 | stack painting, MPU guard, 정적 분석(최대 스택 깊이) |
| 20 | CRC 구현이 맞는지 어떻게 확인? | 표준 check 값 (`"123456789"`) |

## 체크

- [ ] 0절 표를 가리고 15줄을 각각 한 문장으로
- [ ] [playground](../playground/README.md)의 `c/`에서 2절 비트 매크로 + `field_get`/`field_set`을 빈 파일로 → `make c F=c/…`
- [ ] 10절 ring buffer를 5분 안에 종이에
- [ ] 12절 "왜 `now >= start + timeout`이 틀리나"를 예시 숫자로 설명
- [ ] 18절 퀴즈를 가리고 15개 이상
