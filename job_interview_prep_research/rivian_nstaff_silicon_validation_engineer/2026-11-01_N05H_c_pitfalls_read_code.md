# N05H · 코드 읽기 · C 함정
<!-- theme: pastel -->

> **묶음**: H 코드 읽기 · C 함정 (출력 맞히기 · 버그 찾기) · **문제 수**: 9 (★★★ 1 · ★★ 6 · ★ 2) · **목표**: 남이 쓴 C를 읽고 **"이건 int로 승격 / 이건 UB / 이건 W1C RMW"라고 10초 안에 이름 붙이기** — 출력은 전부 이 Mac(Apple arm64, clang)에서 **실제 실행한 값** · **선행**: [N05 목록](2026-11-01_N05_c_paper_drill_bank.html) · **이전/다음**: [N05G 상태 머신·파서](2026-11-01_N05G_state_machines_parsers.html) / [N05I 검증형 C](2026-11-01_N05I_validation_c.html) · **코드**: onsitePrep/n05/h/ (`./run.sh` → Part 1 H1–H9 PASS, Part 2 = 깨진 코드의 실제 출력·UBSan 보고)

## 0. TL;DR · 사용법

- 이 페이지가 훈련하는 것: **코드 읽기 라운드**("what does this print?", "find the bugs in this driver"). 손으로 쓰는 문제보다 짧다(★/★★ = 10분, H6만 20분). 개념 15초 답은 [C 정리 §11 퀵파이어](2026-10-29_c_basics_cram.html)에 있고, 여기선 **실제 컴파일러가 뭘 하는지(asm·UBSan·-O0 vs -O2)**를 증거로 붙였다.
- 외울 한 줄 3개
  - "**Everything narrower than int is promoted to int before arithmetic** — so `~u8`, `u8 << 24`, `u16 * u16` are int operations." (H1·H5)
  - "**Without volatile the compiler may load once, or delete the loop** — and volatile still isn't atomic and isn't a barrier." (H3·H6)
  - "**Cast pointers only to char; for anything else use memcpy or shifts**" — strict aliasing·정렬·엔디안을 한 번에 피함. (H9)
- ★★★ (온사이트 확률 가장 높음): **H6 드라이버 버그 5개** — 체크리스트(volatile · 부호 시프트 · W1C RMW · timeout · sizeof/길이 단위)로 위에서 아래로 훑는 연습. 그다음 H3 · H5 · H1.
- 사용법: §1 문제지를 인쇄 → **답을 종이에 먼저 적고**(출력값 + "UB/impl-defined/OK" 태그) → `<details>`의 모범 답(실제 출력)과 대조. UB 문제는 "출력 = X"가 아니라 **"UB, 이유, UBSan이 이렇게 보고"**가 정답.
- 플랫폼 차이: plain `char`는 **Apple arm64에서 signed, Linux aarch64에서 unsigned** — Lima VM(Ubuntu aarch64, gcc 15.2)에서 실제로 돌려 확인함 (H1 (f)).
- 채점 공통: ① 승격 후 타입을 먼저 적었나 ② UB / implementation-defined / 정의됨을 구분했나 ③ 고친 코드에 `1u`·캐스트 위치·`volatile`·timeout이 있나 ④ "컴파일러 경고가 이미 알려준다"(-Wall -Wextra, UBSan)는 말을 했나.

## 1. 문제지 (인쇄용 — 답 없음)

공통 가정: LP64 (int 32비트, long·포인터 64비트), 2의 보수, `#include <stdint.h> <stdio.h> <stddef.h> <string.h>` 되어 있음. 각 줄마다 **출력** 또는 **UB / implementation-defined / compile error** 를 적을 것.

### H1 · Integer promotion — what does each line print? ★★

```c
uint8_t  x = 0x0F, b = 0xF0, p = 200, q = 100;
uint16_t h1 = 1, h2 = 2, m = 0xFFFF;
uint32_t big = 0xFFFFFFFFu;
int8_t   neg = -1;
char     c = (char)0xFF;

printf("%d\n", ~x == 0xF0);                    /* (a) */
printf("0x%X\n", b << 4);                      /* (b) */
printf("%d %u\n", p + q, (uint8_t)(p + q));    /* (c) */
uint64_t w = big + 1;   printf("%llu\n", w);   /* (d) */
printf("0x%08X\n", (uint32_t)neg);             /* (e) */
printf("%d %d\n", c, c == 0xFF);               /* (f) Apple arm64? Linux aarch64? */
printf("%d\n", h1 - h2 < 0);                   /* (g) */
uint32_t r = m * m;                            /* (h) value? defined? */
```

### H2 · sizeof, padding, alignment, packed ★★

```c
struct A { char a; int b; char c; };
struct B { int b; char a; char c; };
struct C { char a; double d; char c; };
struct D { uint8_t a; uint16_t b; uint32_t c; uint64_t d; };
struct E { char tag[5]; int v; };
struct F { uint8_t a; uint32_t b; } __attribute__((packed));
struct G { uint8_t n; struct A inner; };

size_t param_size(const uint8_t buf[64]) { return sizeof buf; }

uint8_t frame[64];  const char *s = "hello";  char t[] = "hello";
```

Give: `sizeof` of A–G (and offsets of every member of A, C, D), `sizeof frame`, `param_size(frame)`, `sizeof s`, `strlen(s)`, `sizeof t`, `sizeof "abc"`, `_Alignof(struct F)`.

### H3 · Which loops break without volatile? (and what volatile does NOT fix) ★★

```c
int flag;                           /* set by an ISR */
extern void wfi_stub(void);         /* opaque, e.g. wraps __WFI() */

void L1(void)          { while (!flag) { } }
void L2(uint32_t *st)  { while ((*st & 1u) == 0) { } }       /* st -> MMIO status */
void L3(void)          { for (uint32_t i = 0; i < 1000000u; i++) { } }   /* delay */
void L4(void)          { while (!flag) { wfi_stub(); } }
void X1(uint32_t *fifo){ *fifo = 0xA; *fifo = 0xB; }         /* TX FIFO register */

volatile uint32_t isr_count;
void V1(void) { isr_count++; }      /* called from main AND from an ISR */
void V2(uint32_t *desc, volatile uint32_t *doorbell) {
    desc[0] = 0x1000; desc[1] = 64; *doorbell = 1;            /* DMA reads desc */
}
```

At `-O2`: which of L1–L4 (and X1) does the compiler break, and how? Why is L4 different? What is still wrong in V1 and V2 even though they use volatile?

### H4 · Macro traps ★

```c
#define BAD_SQUARE(x)  x * x
#define SQUARE(x)      ((x) * (x))
#define BAD_DOUBLE(x)  (x) + (x)
#define BAD_BIT(n)     1u << n
#define MAX(a, b)      ((a) > (b) ? (a) : (b))
#define BAD_RESET(s)   (s).len = 0; (s).err = 0

int a = 3, i = 5, j = 3;
BAD_SQUARE(a + 1)      /* (a) */      10 / BAD_DOUBLE(5)      /* (b) */
~BAD_BIT(3)            /* (c) hex */  MAX(i++, j) and then i  /* (d) */
MAX(read_fifo(), 0)    /* (e) FIFO holds 5, 9, 2 — returns? how many pops? */
SQUARE(i++)            /* (f) */
if (enabled) BAD_RESET(c);            /* (g) enabled = 0, c = {7, 1}: c after? */
if (enabled) BAD_RESET(c); else c.len = 8;   /* (h) */
```

### H5 · Shift UB ★★

```c
int      s_one(int n)                  { return 1 << n; }          /* (1) n = 31 */
uint32_t u_shl(uint32_t x, unsigned n) { return x << n; }          /* (2) x=1, n=32 */
int      neg_shl(int v)                { return v << 1; }          /* (3) v = -1 */
uint32_t be32(const uint8_t *b) {                                  /* (4) 80 00 00 01 */
    return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
}
uint64_t be32_64(const uint8_t *b) { /* same body as be32 */ }     /* (4b) as uint64_t */
uint64_t wide(int n)                   { return 1 << n; }          /* (5) n = 40 */
int      asr(int v)                    { return v >> 1; }          /* (6) v = -7 */
```

For each: well-defined / implementation-defined / UB? What does arm64 actually produce? Write the fix.

### H6 · Find the 5 bugs in this DMA driver ★★★

```c
#define DMA_SRC      0x00u   /* bus address of source                       */
#define DMA_DST      0x04u   /* bus address of destination                  */
#define DMA_LEN      0x08u   /* transfer length in BYTES                    */
#define DMA_CTRL     0x0Cu   /* bit0 START, bit1 IRQ_EN                     */
#define DMA_STATUS   0x10u   /* W1C. bit n = channel n DONE (n = 0..3),     */
                             /*      bit 31 = BUS_ERR. Shared by all chans  */
#define FRAME_LEN    64u
#define REG(off)     (*(volatile uint32_t *)(0x40020000u + (off)))

#define CTRL_START   (1u << 0)
#define CTRL_IRQ_EN  (1u << 1)
#define ST_DONE0     (1u << 0)
#define ST_BUS_ERR   (1 << 31)

static bool dma_done, dma_err;

void dma0_isr(void) {
    uint32_t st = REG(DMA_STATUS);
    if (st & ST_BUS_ERR) dma_err = true;
    if (st & ST_DONE0)   dma_done = true;
    REG(DMA_STATUS) |= ST_DONE0 | ST_BUS_ERR;         /* ack */
}

int dma0_copy(void *dst, const void *src, size_t nbytes) {
    dma_done = dma_err = false;
    REG(DMA_SRC)  = virt_to_bus(src);
    REG(DMA_DST)  = virt_to_bus(dst);
    REG(DMA_LEN)  = nbytes;
    REG(DMA_CTRL) = CTRL_START | CTRL_IRQ_EN;
    while (!dma_done) { cpu_relax(); }
    return dma_err ? -1 : 0;
}

int send_frame(uint8_t *tx_buf, const uint8_t frame[FRAME_LEN]) {
    return dma0_copy(tx_buf, frame, sizeof(frame));
}
```

Find 5 bugs, say the failure each one causes on real hardware, and rewrite the code.

### H7 · Read these declarations out loud ★

```c
const int *p1;          int const *p2;          int *const p3 = &x;
const int *const p4 = &x;
int *ap[4];             int (*pa)[4];           int grid[3][4];   int (*g)[4] = grid;
typedef void (*isr_t)(void);           isr_t vector_table[4];
void (*my_signal(int sig, void (*h)(int)))(int);

*p1 = 3;   p1 = &y;   *p3 = 5;   p3 = &y;          /* which lines compile? */
```

Say each type in English; `sizeof ap`, `sizeof pa`, `sizeof *pa`; how many bytes do `pa + 1` and `g + 1` move? Rewrite `my_signal` with a typedef.

### H8 · Signed/unsigned comparisons ★★

```c
printf("%d\n", -1 < sizeof(int));                               /* (a) */
int i = -1; unsigned u = 1;          printf("%d\n", i < u);     /* (b) */
int8_t sx = -1; uint8_t uy = 255;    printf("%d\n", sx == uy);  /* (c1) */
int ix = -1; unsigned iy = 0xFFFFFFFFu; printf("%d\n", ix == iy); /* (c2) */
size_t n = 3;  for (size_t k = n - 1; k >= 0; k--) printf("%zu ", k);  /* (d) */
printf("%d\n", strlen("abc") - 5 > 0);                          /* (e) */
uint32_t deadline = 0xFFFFFFF0u, now = 0x10u;
printf("%d\n", now >= deadline);           /* (f) deadline passed 0x20 ticks ago? */
int len = -1;  size_t sz = len;  printf("%zu\n", sz);           /* (g) */
```

### H9 · Type punning by pointer cast ★★

```c
int alias(int *i, float *f) { *i = 1; *f = 0.0f; return *i; }
union { int i; float f; } u;
alias(&u.i, &u.f);                                   /* (1) -O0? -O2? */

float one = 1.0f;  uint32_t bits = *(uint32_t *)&one;          /* (2) */
_Alignas(4) uint8_t buf[8] = {0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0};
uint32_t v0 = *(const uint32_t *)buf;                          /* (3) */
uint32_t v1 = *(const uint32_t *)(buf + 1);                    /* (4) */
```

What is printed at `-O0` and at `-O2`? Which lines are UB and why? Write `float_bits(float)`, `load_le32(const uint8_t *)`, `load_be32(const uint8_t *)` the right way.

## H1 · 출력 맞히기: integer promotion (★★ · 10분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: `uint8_t`·`uint16_t`·`char`가 섞인 8줄을 보고 각각 무엇이 찍히는지, 정의된 동작인지 적기.
- **상황**: 레지스터 값은 대부분 `uint8_t`/`uint16_t`로 들어온다. `~status == 0xF0` 같은 비교가 항상 거짓이 되거나, 32비트 카운터 +1이 64비트에 넣어도 0이 되는 버그가 실제 검증 스크립트·펌웨어에서 나온다.

| 줄 | 보는 것 | 적을 것 |
|---|---|---|
| (a)–(c) | 좁은 unsigned 타입의 `~`, `<<`, `+` | 출력값 |
| (d)–(e) | 32→64 확장 시점, 음수 → unsigned 변환 | 출력값 |
| (f) | plain `char`의 부호 | 두 플랫폼 각각의 출력 |
| (g)–(h) | `uint16_t` 뺄셈·곱셈 | 출력값 / UB 여부 |

- **예시를 말로 풀면**: (a)는 "0x0F를 뒤집으면 0xF0이니 1"이라고 답하면 틀린다 — 뒤집기 **전에** 무슨 일이 생기는지가 문제의 핵심.
- **지켜야 할 조건**: int 32비트, 2의 보수. 각 줄마다 "승격 후 타입"을 먼저 적고 값을 적는다.

### 1. Problem (English)

"Here are eight lines. For each one, tell me what it prints on our target (arm64), and flag anything that isn't well-defined C."

```c
printf("%d\n", ~x == 0xF0);          /* x = (uint8_t)0x0F */
uint32_t r = m * m;                  /* m = (uint16_t)0xFFFF */
```

- `~x == 0xF0` → `0` (not 1)
- `(uint8_t)(p + q)` with p = 200, q = 100 → `44`, but `p + q` → `300`
- `m * m` → **undefined behavior** (signed int overflow), not "0xFFFE0001"

### 2. 먼저 물어볼 것 (clarify)

- "Is int 32 bits on this target?" → Yes (arm64 / Cortex-M both have 32-bit int).
- "Is plain char signed or unsigned here?" → Ask! Apple arm64 = signed, Linux aarch64 / ARM EABI = unsigned. Answer both.
- "Do you want the value the hardware happens to produce, or what the standard guarantees?" → Both; flag UB explicitly.

### 3. 10분 타임박스

| 분 | 할 일 |
|---|---|
| 0–1 | clarify (int 폭, char 부호) |
| 1–3 | 각 줄 옆에 **승격 후 타입** 먼저 적기 (int / unsigned / uint64_t) |
| 3–7 | 값 계산 |
| 7–9 | UB 줄 표시 + 고친 식 |
| 9–10 | "컴파일러 경고가 잡는 줄" 언급 |

- 소리 내서: "Before any arithmetic, anything narrower than int gets promoted to int, so `~x` is `~0x0000000F` = `0xFFFFFFF0`, which is not 0xF0."
- 소리 내서: "The width of the arithmetic is decided by the operands, not by the variable you assign to — `big + 1` is a 32-bit add even if I store it into a uint64_t."

<details><summary>힌트 1 (막히면 5분 후)</summary>

규칙 두 개만: ① `int`보다 좁은 타입(`uint8_t`, `uint16_t`, `char`, `int8_t`)은 연산 전에 **`int`로** 승격 (값이 int에 다 들어가므로 unsigned가 아니라 signed int). ② 그다음 두 피연산자 타입이 다르면 usual arithmetic conversions (int vs unsigned → unsigned).

</details>

<details><summary>힌트 2</summary>

(h): `uint16_t * uint16_t` → 둘 다 **signed int**로 승격 → 65535 × 65535 = 4,294,836,225 > INT_MAX (2,147,483,647) → signed overflow = UB. (f): `c == 0xFF`에서 `0xFF`는 int 255. signed char면 c는 -1로 승격되니 같을 수 없다.

</details>

<details><summary>모범 답 (C)</summary>

| 줄 | 승격 후 | 결과 (이 Mac, 실측) | 분류 |
|---|---|---|---|
| (a) `~x == 0xF0` | `~(int)15` = int `0xFFFFFFF0` | `0` | 정의됨 (항상 거짓) |
| (b) `b << 4` | int `0xF0 << 4` | `0xF00` (8비트로 안 잘림) | 정의됨 |
| (c) `p + q` / `(uint8_t)(p+q)` | int 300 / 300 mod 256 | `300 44` | 정의됨 |
| (d) `big + 1` → uint64_t | **uint32_t** 덧셈 후 확장 | `0` | 정의됨 (unsigned wrap) |
| (e) `(uint32_t)neg` | -1 mod 2^32 | `0xFFFFFFFF` | 정의됨 |
| (f) `c`, `c == 0xFF` | char → int | Apple arm64: `-1 0` · Linux aarch64: `255 1` | **implementation-defined** (char 부호) |
| (g) `h1 - h2 < 0` | int 1 - 2 = -1 | `1` (참!) | 정의됨 — `uint32_t`였으면 0 |
| (h) `m * m` | int × int overflow | UBSan 보고, 실제 값은 `0xFFFE0001` | **UB** |

실제 출력 — macOS arm64, Apple clang, `run.sh` Part 1:

```text
(a) ~x == 0xF0 -> 0   (~x = 0xFFFFFFF0)
(b) b << 4 = 0xF00, (uint8_t)(b << 4) = 0x0
(c) p + q = 300, stored in uint8_t = 44
(d) big + 1 = 0, (uint64_t)big + 1 = 4294967296
(e) (uint32_t)(int8_t)-1 = 0xFFFFFFFF
(f) CHAR_MIN = -128, (int)c = -1, c == 0xFF -> 0
(g) u16: (h1 - h2 < 0) -> 1   u32: w1 - w2 = 0xFFFFFFFF (never < 0)
(h) (uint32_t)m * m = 0xFFFE0001
H1 PASS
```

같은 파일을 Lima VM(Ubuntu aarch64, gcc 15.2)에서 실행 — (f)만 다르다:

```text
(f) CHAR_MIN = 0, (int)c = 255, c == 0xFF -> 1
```

(h) UB 부분 — `H1_promotion_ub.c`, `-Werror` 없이 `-fsanitize=undefined`로 빌드:

```text
H1_promotion_ub.c:7:14: runtime error: signed integer overflow: 65535 * 65535 cannot be represented in type 'int'
mul16(0xFFFF, 0xFFFF) = 0xFFFE0001
```

- "값이 맞게 나왔으니 괜찮다"가 아니다: UB라서 최적화기는 "이 곱은 overflow하지 않는다"고 가정해도 된다(예: 결과 ≥ 0 이라고 보고 비교문 삭제). 고친 식: `(uint32_t)m * m`.
- 컴파일러가 이미 알려주는 줄: clang은 (f)의 `c == 0xFF`에 `-Wtautological-constant-out-of-range-compare`(signed char에서 항상 거짓), gcc는 (a)에 `-Wsign-compare: comparison of promoted bitwise complement of an unsigned value with constant` 경고를 냈다(둘 다 실측). 그래서 아래 검증 코드는 (f)를 런타임 `volatile int ff`로 비교한다.

```c
/* H1 · integer promotion — "what does this print?"
 * Every expression here is well-defined (the UB one, u16*u16, lives in
 * H1_promotion_ub.c). Each line prints the value and asserts it. */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    /* (a) ~ on a uint8_t: operand is promoted to int FIRST, then inverted */
    uint8_t x = 0x0F;
    int a = (~x == 0xF0);               /* ~x is int 0xFFFFFFF0, not 0xF0 */
    printf("(a) ~x == 0xF0 -> %d   (~x = 0x%X)\n", a, (unsigned)~x);
    assert(a == 0);
    assert((uint8_t)~x == 0xF0);        /* fix: cast back to the narrow type */

    /* (b) shift of a uint8_t is done in int: bits are NOT lost */
    uint8_t b = 0xF0;
    printf("(b) b << 4 = 0x%X, (uint8_t)(b << 4) = 0x%X\n",
           (unsigned)(b << 4), (unsigned)(uint8_t)(b << 4));
    assert((b << 4) == 0xF00);
    assert((uint8_t)(b << 4) == 0x00);

    /* (c) uint8_t + uint8_t is an int addition */
    uint8_t p = 200, q = 100;
    uint8_t s8 = (uint8_t)(p + q);
    printf("(c) p + q = %d, stored in uint8_t = %u\n", p + q, (unsigned)s8);
    assert(p + q == 300 && s8 == 44);

    /* (d) uint32_t + 1 wraps in 32 bits BEFORE widening to 64 */
    uint32_t big = 0xFFFFFFFFu;
    uint64_t w_bad  = big + 1;              /* computed as uint32_t -> 0 */
    uint64_t w_good = (uint64_t)big + 1;    /* cast first */
    printf("(d) big + 1 = %llu, (uint64_t)big + 1 = %llu\n",
           (unsigned long long)w_bad, (unsigned long long)w_good);
    assert(w_bad == 0 && w_good == 0x100000000ull);

    /* (e) int8_t -> uint32_t: sign-extend, then reduce mod 2^32 */
    int8_t neg = -1;
    uint32_t u = (uint32_t)neg;
    printf("(e) (uint32_t)(int8_t)-1 = 0x%08X\n", (unsigned)u);
    assert(u == 0xFFFFFFFFu);

    /* (f) plain char: signedness is implementation-defined */
    /* (clang flags `c == 0xFF` with a literal as always-false on signed-char
     * targets; `ff` is a runtime int so the comparison really executes) */
    char c = (char)0xFF;
    volatile int ff = 0xFF;
    printf("(f) CHAR_MIN = %d, (int)c = %d, c == 0xFF -> %d\n",
           CHAR_MIN, (int)c, c == ff);
#if CHAR_MIN < 0          /* Apple arm64, x86 Linux: signed char */
    assert((int)c == -1 && !(c == ff));
#else                     /* Linux aarch64 / ARM EABI: unsigned char */
    assert((int)c == 255 && (c == ff));
#endif

    /* (g) uint16_t - uint16_t < 0 is TRUE (int math); uint32_t version is FALSE */
    uint16_t h1 = 1, h2 = 2;
    uint32_t w1 = 1, w2 = 2;
    printf("(g) u16: (h1 - h2 < 0) -> %d   u32: w1 - w2 = 0x%X (never < 0)\n",
           (h1 - h2 < 0), (unsigned)(w1 - w2));
    assert(h1 - h2 < 0);
    assert(w1 - w2 == 0xFFFFFFFFu);

    /* (h) the safe u16*u16: widen one operand to unsigned 32 first */
    uint16_t m = 0xFFFF;
    uint32_t prod = (uint32_t)m * m;
    printf("(h) (uint32_t)m * m = 0x%08X\n", (unsigned)prod);
    assert(prod == 0xFFFE0001u);

    puts("H1 PASS");
    return 0;
}
```

복잡도: 해당 없음 (식 평가). 핵심은 각 식의 **연산 타입**.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

| 식 | 1단계: 승격 | 2단계: 연산 | 3단계: 비교/저장 |
|---|---|---|---|
| `~x == 0xF0` | x: uint8_t 0x0F → int 0x0000000F | `~` → int 0xFFFFFFF0 (= -16) | -16 == 240 → 0 |
| `big + 1` → u64 | big: uint32_t (승격 없음), 1: int → uint32_t | 0xFFFFFFFF + 1 = 0 (mod 2^32) | 0 → uint64_t 0 |
| `h1 - h2 < 0` | 1 → int 1, 2 → int 2 | 1 - 2 = int -1 | -1 < 0 → 1 |
| `m * m` | 0xFFFF → int 65535 (×2) | 65535 × 65535 = 4294836225 > INT_MAX | **UB** — 저장 전에 이미 끝 |
| `c == 0xFF` (signed char) | c: char -1 → int -1, 0xFF: int 255 | — | -1 == 255 → 0 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: `(uint8_t)~x == 0xF0` — 다시 좁은 타입으로 캐스트하면 의도대로 1.
- 0/경계: `x = 0` → `~x` = int -1 (0xFFFFFFFF), `(uint8_t)~x` = 0xFF.
- 경계: `m = 0xB504` (46340) → 46340² = 2,147,395,600 < INT_MAX → 정의됨; 46341²부터 UB. "UB는 값에 따라 생긴다".
- 플랫폼: 같은 바이너리 소스를 `-funsigned-char` / `-fsigned-char`로 빌드해 (f)가 바뀌는지.
- 왕복: `(int8_t)(uint8_t)0xFF == -1` (C11에서 implementation-defined지만 gcc/clang은 modulo로 정의).

</details>

### 4. 꼬리 질문

"How would you write the u16 × u16 multiply so it's correct on both a 16-bit-int and a 32-bit-int compiler?"

<details><summary>답</summary>

"Cast one operand to the result width before multiplying: `(uint32_t)a * b`. Then the other operand is converted to uint32_t too, the multiply is unsigned, and unsigned arithmetic wraps by definition. On a 16-bit-int MSP430-style target the original even overflows silently in 16 bits."

한국어 핵심: **연산 전에 결과 폭의 unsigned로 캐스트** — 대입 대상의 타입은 연산 폭을 바꾸지 못한다.

</details>

"Your test reads a status byte and does `if (~st & READY)` — is that safe?"

<details><summary>답</summary>

"Yes for testing a single low bit: `~st` is an int with the upper 24 bits set, but `& READY` masks them away. It becomes a bug only when you compare the whole complemented value, like `~st == 0xFE`, or store it in something wider than 8 bits."

한국어 핵심: `~`의 상위 비트 오염은 **마스크로 걸러내면** 무해, **전체 비교·확장 저장**에서 터진다.

</details>

"Why does the char signedness matter in firmware?"

<details><summary>답</summary>

"Any byte buffer typed as plain `char` — UART RX, a parsed log line — sign-extends values ≥ 0x80 on Apple/x86 but not on Linux Arm, so `if (c == 0xFF)` or a table index `lut[c]` behaves differently per platform. Use `uint8_t` for bytes and plain `char` only for text."

한국어 핵심: **바이트는 `uint8_t`, 글자는 `char`** — `lut[c]`에 음수 인덱스가 들어가는 버그.

</details>

### 5. 채점 · 자주 틀리는 것

- `~x`를 8비트 결과로 생각함 — 승격 후 32비트.
- `uint64_t w = a + 1;`에서 64비트 덧셈이라고 생각함 — 피연산자 타입이 연산 폭을 결정.
- `uint16_t` 곱셈을 "unsigned라 wrap"이라고 답함 — **signed int**로 승격되어 UB.
- (f)에 한 플랫폼 답만 적음 — "implementation-defined, Apple은 signed / Linux Arm은 unsigned"가 만점.
- (g)를 0이라고 답함 — `uint16_t`는 int로, `uint32_t`는 그대로 unsigned: 폭에 따라 결과가 반대.

## H2 · `sizeof` 배열 vs 포인터 · struct 패딩/정렬 · packed (★★ · 10분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 구조체 7개의 크기와 멤버 offset, 그리고 배열/포인터/문자열의 `sizeof`를 표로 채우기.
- **상황**: 레지스터 블록을 `struct`로 덮어쓰거나, 센서 패킷을 구조체로 정의해 DMA/UART로 보낼 때 **패딩 때문에 offset이 어긋나는 버그**. 함수 인자로 받은 배열에 `sizeof`를 쓰면 8이 나와 memset/DMA 길이가 틀어짐 (H6 버그 5와 같은 뿌리).

| 대상 | 적을 것 | 규칙 |
|---|---|---|
| struct A–G | sizeof, 각 멤버 offset | 멤버는 자기 정렬의 배수 offset, 전체 크기는 최대 정렬의 배수 |
| packed F | sizeof, 정렬 | 패딩 없음, 정렬 1 |
| frame / param / s / t / "abc" | sizeof, strlen | 배열 인자는 포인터로 바뀜 |

- **예시를 말로 풀면**: `struct A {char a; int b; char c;}`는 1+4+1 = 6이 아니다 — b가 4의 배수 주소에 와야 하고, 배열로 나열해도 다음 원소의 b가 정렬되도록 끝에도 채운다.
- **지켜야 할 조건**: LP64 (pointer 8, double/uint64_t 정렬 8). offset은 바이트 단위.

### 1. Problem (English)

"Fill in sizeof and the member offsets for these structs on our 64-bit target. Then tell me what `param_size(frame)` returns and why."

```c
struct A { char a; int b; char c; };
size_t param_size(const uint8_t buf[64]) { return sizeof buf; }
```

- `sizeof(struct A)` → `12` (a@0, b@4, c@8)
- `sizeof(struct B)` (`int, char, char`) → `8`
- `param_size(frame)` with `uint8_t frame[64]` → `8`, not 64

### 2. 먼저 물어볼 것 (clarify)

- "64-bit LP64 ABI, or a 32-bit MCU?" → LP64 arm64. (32-bit: pointer 4; `double` still 8-aligned on ARM EABI.)
- "Any `#pragma pack` or compiler flags like `-fpack-struct`?" → No, except the struct marked packed.
- "Is this struct going on the wire or is it purely internal?" → Both cases are follow-ups.

### 3. 10분 타임박스

| 분 | 할 일 |
|---|---|
| 0–1 | clarify (ABI) |
| 1–6 | 구조체마다 offset 열을 위에서 아래로 (현재 offset → 정렬로 올림 → 크기 더함) |
| 6–8 | 끝 패딩: 최대 정렬의 배수로 올림 |
| 8–10 | sizeof 배열/포인터/문자열, packed의 비용 언급 |

- 소리 내서: "Each member goes at the next offset that's a multiple of its own alignment; the struct size is rounded up to the largest member alignment so arrays of it stay aligned."
- 소리 내서: "An array parameter is adjusted to a pointer, so sizeof gives the pointer size — clang even warns about it."

<details><summary>힌트 1 (막히면 5분 후)</summary>

offset 계산 루프: `off = round_up(off, align(member)); member@off; off += size(member);` 마지막에 `size = round_up(off, max_align)`. 정렬: char 1, uint16_t 2, int/uint32_t 4, double/uint64_t/pointer 8. 배열 멤버의 정렬 = 원소 정렬.

</details>

<details><summary>힌트 2</summary>

G: 안쪽 struct A의 정렬은 A의 최대 정렬(4) → inner는 offset 4. `sizeof t` (char t[] = "hello") = 6 (NUL 포함), `sizeof s` (포인터) = 8, `sizeof "abc"` = 4.

</details>

<details><summary>모범 답 (C)</summary>

| 대상 | 레이아웃 (offset: 멤버) | sizeof (실측) |
|---|---|---|
| A `char,int,char` | 0:a · 1–3 pad · 4:b · 8:c · 9–11 pad | **12** |
| B `int,char,char` | 0:b · 4:a · 5:c · 6–7 pad | **8** (큰 것부터 → 패딩 감소) |
| C `char,double,char` | 0:a · 1–7 pad · 8:d · 16:c · 17–23 pad | **24** |
| D `u8,u16,u32,u64` | 0:a · 1 pad · 2:b · 4:c · 8:d | **16** (작은→큰 순서가 우연히 빈틈 최소) |
| E `char[5],int` | 0–4:tag · 5–7 pad · 8:v | **12** |
| F packed `u8,u32` | 0:a · 1:b (정렬 안 됨) | **5**, `_Alignof` = 1 |
| G `u8, struct A` | 0:n · 1–3 pad · 4:inner(12) | **16** |
| `frame` / `param_size(frame)` | 배열 / 포인터 | **64 / 8** |
| `s` / `strlen(s)` / `t` / `"abc"` | 포인터 / 길이 / 배열(NUL 포함) / 리터럴 배열 | **8 / 5 / 6 / 4** |

실제 출력 (`run.sh` Part 1):

```text
sizeof frame=64  param_size(frame)=8  count=64
sizeof s=8 strlen(s)=5  sizeof t=6  sizeof "abc"=4
A=12 (b@4 c@8)  B=8  C=24 (d@8)  D=16 (b@2 c@4 d@8)
E=12 (v@8)  F(packed)=5 (b@1, align 1)  G=16 (inner@4)
H2 PASS
```

clang은 `param_size`를 **기본 경고**로 잡는다 (실측 — pragma로 끄기 전 첫 빌드에서 `-Werror` 때문에 실패한 메시지 그대로):

```text
H2_sizeof_layout.c:19:19: error: sizeof on array function parameter will return size of 'const uint8_t *' (aka 'const unsigned char *') instead of 'const uint8_t[64]' (aka 'const unsigned char[64]') [-Werror,-Wsizeof-array-argument]
```

- packed의 비용: `F.b`는 offset 1 → 비정렬 4바이트 접근. Cortex-M0/M0+는 비정렬 LDR이 HardFault, M3/M4는 일반 메모리 LDR/STR은 허용하지만 LDRD/LDM은 fault, Device 메모리 비정렬은 fault [미검증: 여기선 Arm 하드웨어로 실행 안 함, Arm ARM 기술 기준]. 그래서 wire 포맷은 packed struct 대신 **바이트 배열 + memcpy/shift** (N05B B3).
- 32비트 x86(i386 SysV)에서는 struct 안 double 정렬이 4라 C = 16이 된다 [미검증].

```c
/* H2 · sizeof array vs pointer · struct padding/alignment · packed
 * Values asserted here are for LP64 arm64/x86-64 (int=4, pointer=8,
 * double/uint64_t aligned to 8). */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct A { char a; int b; char c; };                 /* 1+3pad, 4, 1+3pad = 12 */
struct B { int b; char a; char c; };                 /* 4, 1, 1, 2pad     = 8  */
struct C { char a; double d; char c; };              /* 1+7, 8, 1+7       = 24 */
struct D { uint8_t a; uint16_t b; uint32_t c; uint64_t d; }; /* 1+1,2,4,8 = 16 */
struct E { char tag[5]; int v; };                    /* 5+3pad, 4         = 12 */
struct F { uint8_t a; uint32_t b; } __attribute__((packed));  /* 1, 4      = 5  */
struct G { uint8_t n; struct A inner; };             /* 1+3pad, 12        = 16 */

/* clang flags this by default (-Wsizeof-array-argument) — the warning IS the
 * answer; silenced here only so the -Werror build can demonstrate the value. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsizeof-array-argument"
static size_t param_size(const uint8_t buf[64]) {    /* buf is really a pointer */
    return sizeof buf;
}
#pragma clang diagnostic pop

int main(void) {
    uint8_t frame[64];
    const char *s = "hello";
    char t[] = "hello";

    printf("sizeof frame=%zu  param_size(frame)=%zu  count=%zu\n",
           sizeof frame, param_size(frame), sizeof frame / sizeof frame[0]);
    printf("sizeof s=%zu strlen(s)=%zu  sizeof t=%zu  sizeof \"abc\"=%zu\n",
           sizeof s, strlen(s), sizeof t, sizeof "abc");
    assert(sizeof frame == 64 && param_size(frame) == sizeof(uint8_t *));
    assert(sizeof s == 8 && strlen(s) == 5 && sizeof t == 6 && sizeof "abc" == 4);

    printf("A=%zu (b@%zu c@%zu)  B=%zu  C=%zu (d@%zu)  D=%zu (b@%zu c@%zu d@%zu)\n",
           sizeof(struct A), offsetof(struct A, b), offsetof(struct A, c),
           sizeof(struct B), sizeof(struct C), offsetof(struct C, d),
           sizeof(struct D), offsetof(struct D, b), offsetof(struct D, c),
           offsetof(struct D, d));
    printf("E=%zu (v@%zu)  F(packed)=%zu (b@%zu, align %zu)  G=%zu (inner@%zu)\n",
           sizeof(struct E), offsetof(struct E, v), sizeof(struct F),
           offsetof(struct F, b), _Alignof(struct F), sizeof(struct G),
           offsetof(struct G, inner));

    assert(sizeof(struct A) == 12 && offsetof(struct A, b) == 4 && offsetof(struct A, c) == 8);
    assert(sizeof(struct B) == 8);
    assert(sizeof(struct C) == 24 && offsetof(struct C, d) == 8);
    assert(sizeof(struct D) == 16 && offsetof(struct D, b) == 2 &&
           offsetof(struct D, c) == 4 && offsetof(struct D, d) == 8);
    assert(sizeof(struct E) == 12 && offsetof(struct E, v) == 8);
    assert(sizeof(struct F) == 5 && offsetof(struct F, b) == 1 && _Alignof(struct F) == 1);
    assert(sizeof(struct G) == 16 && offsetof(struct G, inner) == 4);

    /* the wire-safe way to read F.b out of a packed byte buffer: memcpy */
    uint8_t wire[5] = {0xAA, 0x78, 0x56, 0x34, 0x12};
    uint32_t b;
    memcpy(&b, wire + 1, sizeof b);              /* no unaligned deref */
    assert(b == 0x12345678u);                    /* little-endian host */

    puts("H2 PASS");
    return 0;
}
```

복잡도: 해당 없음. 표 하나를 위→아래로 채우는 O(멤버 수).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

struct C `{char a; double d; char c;}`:

| 멤버 | 정렬 | 현재 off | 올림 후 off | 다음 off |
|---|---|---|---|---|
| a (char) | 1 | 0 | 0 | 1 |
| d (double) | 8 | 1 | **8** (7바이트 pad) | 16 |
| c (char) | 1 | 16 | 16 | 17 |
| 끝 | max 8 | 17 | **24** (7바이트 pad) | — |

struct G `{uint8_t n; struct A inner;}`:

| 멤버 | 정렬 | 현재 off | 올림 후 off | 다음 off |
|---|---|---|---|---|
| n | 1 | 0 | 0 | 1 |
| inner (A, 크기 12) | 4 (A의 최대) | 1 | **4** | 16 |
| 끝 | max 4 | 16 | 16 | — |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: `_Static_assert(offsetof(struct reg_block, CTRL) == 0x0C, "...")` — 레지스터 맵 struct는 **컴파일 타임 assert로 offset 고정**.
- 빈/경계: 멤버 하나짜리 struct `{uint64_t x;}` = 8, `{char x;}` = 1 (패딩 없음).
- 배열: `sizeof(struct A[3])` = 36 — 끝 패딩이 있어야 원소 1의 b가 정렬됨.
- 플랫폼: 같은 헤더를 32비트 MCU 빌드에서 `sizeof(void *)` = 4로 다시 계산.
- packed 왕복: wire 버퍼 `{AA 78 56 34 12}`에서 `memcpy(&b, wire + 1, 4)` → 0x12345678 (LE 호스트).

</details>

### 4. 꼬리 질문

"You map a peripheral's registers with a struct. How do you make sure the offsets match the datasheet?"

<details><summary>답</summary>

"Use only uint32_t members (all 4-aligned, so no padding sneaks in), name reserved gaps explicitly like `uint32_t RESERVED0[3]`, mark the members volatile, and pin every offset with `_Static_assert(offsetof(...) == 0x..)`. The build fails the moment someone inserts a field."

한국어 핵심: **같은 폭 멤버 + RESERVED 배열 + `_Static_assert(offsetof)`** — CMSIS 장치 헤더 방식.

</details>

"Why not just send a packed struct over UART/DMA?"

<details><summary>답</summary>

"Packing fixes the padding but not endianness or bit-field ordering, and it creates unaligned accesses — some cores fault, others pay extra cycles, and taking `&pkt.b` gives a misaligned pointer. Explicit serialization with put_le32-style helpers is portable and just as fast."

한국어 핵심: packed = **패딩만 해결**, 엔디안·비트필드·비정렬 문제는 그대로.

</details>

"How would you shrink struct C?"

<details><summary>답</summary>

"Sort members by decreasing alignment: `double d; char a; char c;` gives 8 + 1 + 1 + 6 padding = 16 instead of 24. In arrays of thousands of descriptors that's a real memory saving."

한국어 핵심: **큰 정렬부터 나열** → 24 → 16.

</details>

### 5. 채점 · 자주 틀리는 것

- 끝 패딩을 빼먹음 (A를 9, C를 17로 적음).
- 배열 인자 `buf[64]`의 sizeof를 64로 적음 — 포인터 8.
- `sizeof t`(배열)에 NUL을 안 셈, `sizeof s`(포인터)를 6으로 적음.
- 중첩 struct의 정렬을 1로 봄 — 안쪽 struct의 최대 정렬을 따른다.
- packed를 "공짜"라고 말함 — 비정렬 접근 비용/fault 언급 필요.

## H3 · `volatile` 없으면 깨지는 루프 3개 고르기 (+ volatile로 안 되는 것) (★★ · 10분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: volatile이 없는 루프 4개(L1–L4) + FIFO 쓰기(X1) 중 `-O2`에서 컴파일러가 망가뜨리는 것 고르기, 그리고 volatile을 써도 여전히 틀린 코드 2개(V1, V2) 설명.
- **상황**: ISR 플래그 대기, 상태 레지스터 폴링, 지연 루프, TX FIFO에 두 번 쓰기 — 디버그 빌드(-O0)에선 되다가 **릴리스 빌드(-O2)에서만 멈추거나 건너뛰는** 전형적인 bring-up 버그. 또 "volatile 붙였으니 ISR-safe"라는 착각.

| 대상 | 적을 것 | 규칙 |
|---|---|---|
| L1–L4, X1 | 깨짐/정상, -O2에서 컴파일러가 실제로 하는 일 | 일반 변수는 "아무도 안 바꾼다"고 가정 가능 |
| V1 | 왜 여전히 경쟁 상태인가 | volatile ≠ atomic |
| V2 | 왜 여전히 순서가 틀릴 수 있나 | volatile ≠ barrier |

- **예시를 말로 풀면**: L1에서 `flag`는 ISR이 1로 바꾸지만, 컴파일러는 이 함수 안에서 `flag`를 바꾸는 코드를 못 보므로 한 번만 읽어도 된다고 판단한다.
- **지켜야 할 조건**: `clang -O2`, arm64. 답은 "깨짐" 3개 + 정상 1개 + 보너스 X1.

### 1. Problem (English)

"Here are some polling loops from a bring-up codebase. They work in the debug build and fail in release. Which ones, why, and how do you fix them? Then: these two functions already use volatile — are they correct?"

```c
void L1(void)         { while (!flag) { } }            /* flag set in ISR */
void L4(void)         { while (!flag) { wfi_stub(); } }
void V1(void)         { isr_count++; }                  /* volatile, main + ISR */
```

- L1 at `-O2` → compiled to a bare `ret` (loop deleted)
- L4 at `-O2` → reloads `flag` after every call (not broken by the compiler)
- V1 → still load / add / store: an ISR between them loses a count

### 2. 먼저 물어볼 것 (clarify)

- "What optimization level and compiler?" → clang `-O2` (release).
- "Is the ISR in another translation unit, and is LTO on?" → Assume another TU, no LTO (LTO only makes it worse).
- "Single core, or does another core / DMA master touch these?" → Single core + ISR; DMA for V2.

### 3. 10분 타임박스

| 분 | 할 일 |
|---|---|
| 0–1 | clarify (최적화, 단일 코어?) |
| 1–4 | 각 루프: "본문 안에서 이 값을 바꿀 수 있는 게 보이나?" 체크 |
| 4–7 | 깨짐 3 + 정상 1 + X1 적기, 실제 결과(hoist / 삭제 / dead store) 예측 |
| 7–9 | V1(원자성), V2(순서) 설명 |
| 9–10 | fix: `volatile`, critical section/atomic, barrier |

- 소리 내서: "Without volatile the compiler only has to preserve what this thread can observe, so a loop whose body changes nothing may load once — or, since C11 lets it assume such a loop terminates, delete it."
- 소리 내서: "volatile guarantees each access is emitted in program order relative to other volatile accesses. It doesn't make read-modify-write atomic and it doesn't order the CPU's normal memory stores against the device."

<details><summary>힌트 1 (막히면 5분 후)</summary>

판단 질문 하나: **루프 본문 안에, 컴파일러가 모르는 일(불투명한 함수 호출, volatile 접근, asm)이 있나?** 없으면 루프 조건의 일반 메모리 값은 루프 불변 → 한 번만 읽거나 루프 자체를 없앨 수 있다. 지연 루프 L3는 아무 관찰 가능한 효과가 없다.

</details>

<details><summary>힌트 2</summary>

L4의 `wfi_stub()`는 다른 파일의 함수 → 그 함수가 전역 `flag`를 바꿀 수도 있으니 호출 후 다시 읽어야 한다. 하지만 "운 좋게 동작"일 뿐 — LTO로 인라인되면 L1과 같아진다. X1: 같은 주소에 연달아 두 번 쓰면 첫 쓰기는 dead store.

</details>

<details><summary>모범 답 (C)</summary>

정답: **깨짐 = L1, L2, L3** (+ 보너스 X1), **정상 = L4** (단, volatile을 붙이는 게 맞다).

| 함수 | -O2에서 실제 생성 코드 (clang, 실측) | 결과 |
|---|---|---|
| L1 `while(!flag){}` | `ret` 한 줄 | **루프 삭제** — 기다리지 않고 바로 반환 |
| L2 `while((*st&1)==0){}` | `ldrb` 한 번 → 0이면 `LBB2_1: b LBB2_1` (자기 자신으로 점프) | **한 번 읽고 무한 루프** |
| L3 지연 `for` | `ret` (volatile 카운터 버전은 `ldr/add/str [sp,#12]` 루프 유지) | **지연 0** |
| L4 `while(!flag) wfi_stub();` | `bl _wfi_stub` 다음 `ldr w8, [x19]` 재로드 | 정상 (불투명 호출 덕분) |
| X1 `*fifo=0xA; *fifo=0xB;` | `mov w8,#11; str` 한 번 | **0xA 전송 사라짐** |
| volatile 버전 전부 | 루프마다 `ldr`, 두 번 `str` | 정상 |

trim한 실제 asm (`clang -O2 -S H3_volatile_asm.c`, `run.sh` Part 2):

```text
_L1_wait_flag:                 ; non-volatile flag
	ret                        ; <- the whole loop is gone
_L1_wait_vflag:                ; volatile flag
	adrp	x8, _vflag@GOTPAGE
	ldr	x8, [x8, _vflag@GOTPAGEOFF]
LBB1_1:
	ldr	w9, [x8]               ; reload every iteration
	cbz	w9, LBB1_1
	ret
_L2_poll:                      ; plain uint32_t *
	ldrb	w8, [x0]               ; read ONCE
	tbnz	w8, #0, LBB2_2
LBB2_1:
	b	LBB2_1                 ; spin forever, never re-reads
LBB2_2:
	ret
_L2_poll_v:                    ; volatile uint32_t *
LBB3_1:
	ldr	w8, [x0]
	tbz	w8, #0, LBB3_1
	ret
_L3_delay:
	ret                        ; delay = 0
_L4_wait_call:                 ; (prologue trimmed)
LBB6_1:
	bl	_wfi_stub
	ldr	w8, [x19]              ; flag reloaded after the opaque call
	cbz	w8, LBB6_1
_X1_tx2:
	mov	w8, #11                ; only 0xB is written
	str	w8, [x0]
	ret
_X1_tx2_v:
	mov	w8, #10
	str	w8, [x0]
	mov	w8, #11
	str	w8, [x0]
	ret
```

런타임 증거 — SIGALRM 핸들러를 ISR 대신 20 ms 뒤에 실행 (`H3_novolatile_demo.c`, 2초 watchdog):

```text
--- H3 no-volatile -O0 flag: left the loop after 24.9 ms (ISR fires at 20 ms), done=1
--- H3 no-volatile -O0 poll: left the loop after 24.4 ms (ISR fires at 20 ms), done=1
--- H3 no-volatile -O2 flag: left the loop after 0.0 ms (ISR fires at 20 ms), done=0
--- H3 no-volatile -O2 poll: HUNG -> killed by 2s watchdog
```

- 같은 버그가 **두 가지 증상**: L1형은 "안 기다리고 통과"(done=0인데 진행 → 다음 단계에서 쓰레기 데이터), L2형은 "영원히 멈춤". 둘 다 -O0에선 정상. L1이 삭제될 수 있는 근거: C11 6.8.5p6 — 조건이 상수가 아니고 본문에 I/O·volatile·동기화가 없는 반복문은 종료한다고 가정 가능. 왜 clang이 L1은 삭제하고 L2는 무한 루프로 남겼는지는 최적화 패스 내부 사정 [추정] — 둘 다 허용되는 결과.
- **V1** (`volatile` 카운터 `++`): asm이 여전히 `ldr w9,[x8] / add w9,w9,#1 / str w9,[x8]` 3개. main이 ldr한 직후 ISR이 들어와 +1 하고 나가면, main의 str이 ISR의 증가를 덮어씀. 고침: ISR만 쓰고 main은 읽기만(single writer), 또는 main 쪽을 `__disable_irq()`/`__enable_irq()`로 감싸기, 또는 C11 `atomic_fetch_add` (Cortex-M3+는 LDREX/STREX).
- **V2** (일반 메모리 descriptor 쓰기 → volatile doorbell): C 표준상 volatile 접근끼리만 순서가 보장되고, **일반 store를 volatile store 뒤로 옮기는 것도 허용**. CPU 쪽에서도 Normal 메모리 쓰기와 Device 쓰기 순서, 캐시 write-back은 별개. 고침: barrier. 아래 asm처럼 `__atomic_thread_fence(__ATOMIC_RELEASE)`는 arm64에서 `dmb ish`를 낸다(실측). DMA 비코히어런트 시스템이면 캐시 clean + `dsb`까지 — Linux에선 `dma_wmb()` 후 `writel()`이 이 일을 한다 ([LP08 DMA·코히어런시](2026-10-31_LP08_dma_iommu_coherency.html)).

```text
_V1_count:  ldr w9, [x8] / add w9, w9, #1 / str w9, [x8]
_V2_kick:   str d0, [x0]      ; desc[0..1]
            dmb ish           ; from __atomic_thread_fence(__ATOMIC_RELEASE)
            str w8, [x1]      ; doorbell = 1
```

검증 코드 1 — asm 확인용 소스 (main 없음, `-O2 -S`로만 빌드):

```c
/* H3 · volatile — compile with:  clang -O2 -S H3_volatile_asm.c  and read the asm.
 * Not a runnable program (no main); run.sh greps the interesting functions. */
#include <stdint.h>

int flag;                       /* set by an ISR (not volatile!)   */
volatile int vflag;             /* same, done right                */
extern void wfi_stub(void);     /* opaque call (e.g. __WFI wrapper) */

/* L1: spin on ISR flag */
void L1_wait_flag(void)        { while (!flag) { } }
void L1_wait_vflag(void)       { while (!vflag) { } }

/* L2: poll a status register through a plain pointer */
void L2_poll(uint32_t *st)               { while ((*st & 1u) == 0) { } }
void L2_poll_v(volatile uint32_t *st)    { while ((*st & 1u) == 0) { } }

/* L3: busy-wait delay loop */
void L3_delay(void)            { for (uint32_t i = 0; i < 1000000u; i++) { } }
void L3_delay_v(void)          { for (volatile uint32_t i = 0; i < 1000000u; i++) { } }

/* L4: NOT broken by the compiler: the opaque call forces a reload of flag */
void L4_wait_call(void)        { while (!flag) { wfi_stub(); } }

/* X1 (bonus, not a loop): push two words into a TX FIFO register */
void X1_tx2(uint32_t *fifo)              { *fifo = 0xA; *fifo = 0xB; }
void X1_tx2_v(volatile uint32_t *fifo)   { *fifo = 0xA; *fifo = 0xB; }

/* V1: volatile does NOT make ++ atomic: still load / add / store */
volatile uint32_t isr_count;
void V1_count(void)            { isr_count++; }

/* V2: volatile does NOT order normal-memory stores vs the doorbell on the CPU;
 * you need a barrier (dmb/dsb on Arm; Linux wmb()/writel()). */
void V2_kick(uint32_t *desc, volatile uint32_t *doorbell) {
    desc[0] = 0x1000; desc[1] = 64;
    __atomic_thread_fence(__ATOMIC_RELEASE);   /* -> dmb ish on arm64 */
    *doorbell = 1;
}
```

검증 코드 2 — 올바른 버전 실행 테스트 (`volatile sig_atomic_t`는 C 표준이 시그널 핸들러 공유용으로 보장하는 유일한 타입, C11 7.14.1.1p5). `-O0`, `-O2` 둘 다 PASS:

```c
/* H3 · volatile, runnable: a SIGALRM handler plays the role of the ISR.
 * `volatile sig_atomic_t` is exactly the case the C standard blesses
 * (C11 7.14.1.1p5). Works at -O0 and -O2 (run.sh builds both). */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

static volatile sig_atomic_t done;          /* ISR-shared flag: volatile */
static volatile uint32_t fake_status;       /* "MMIO" status register    */

static void fake_isr(int sig) {
    (void)sig;
    fake_status |= 1u;   /* hardware sets READY ... (single writer here)  */
    done = 1;            /* ... and the ISR raises the flag                */
}

static void arm_timer_ms(int ms) {
    struct itimerval it = {{0, 0}, {0, ms * 1000}};
    signal(SIGALRM, fake_isr);
    setitimer(ITIMER_REAL, &it, NULL);
}

int main(void) {
    unsigned long spins = 0;
    arm_timer_ms(20);
    while (!done) { spins++; }                    /* L1 done right */
    assert(done == 1);

    while ((fake_status & 1u) == 0) { }          /* L2 done right (already set) */
    assert(fake_status & 1u);

    printf("flag seen after %s spins\n", spins > 1000 ? ">1000" : "few");
    puts("H3 PASS");
    return 0;
}
```

```text
flag seen after >1000 spins
H3 PASS
(-O2) H3 PASS
```

복잡도: 해당 없음. 판단 기준은 "루프 본문에 컴파일러가 모르는 부작용이 있나".

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

L2를 컴파일러 입장에서 trace (`st` 가 가리키는 값은 루프 안에서 안 바뀐다고 가정):

| 단계 | 소스 의미 | 컴파일러 추론 | 생성 코드 |
|---|---|---|---|
| 1 | 매 반복 `*st` 읽기 | 루프 본문에 store·call 없음 → `*st` 불변 | load를 루프 밖으로 hoist |
| 2 | `(*st & 1) == 0` 이면 반복 | 조건도 불변 → 처음 값으로 결정 | `tbnz` 한 번 |
| 3 | 처음에 0이었다면 | 영원히 0 | `b .` (자기 자신으로 점프) |
| 4 | HW가 READY=1로 바꿈 | 반영 안 됨 | **hang** |

V1 경쟁 상태 trace (isr_count = 5에서 시작):

| 시점 | main | ISR | 메모리 |
|---|---|---|---|
| t0 | `ldr` → r = 5 | | 5 |
| t1 | (인터럽트 진입) | `ldr` 5, `add` 6, `str` 6 | 6 |
| t2 | `add` r = 6 | | 6 |
| t3 | `str` 6 | | **6 (7이어야 함)** |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: volatile 플래그 + 실제 ISR(타이머) → -O0, -O2, -Os, LTO 빌드 모두 통과.
- 이미 준비됨: 루프 진입 전 READY가 이미 1 → 즉시 통과 (L2_poll의 `tbnz` 경로).
- 절대 안 옴: HW가 READY를 안 올림 → volatile만으로는 영원히 대기 → **timeout 필요** (H6 버그 4).
- 회귀: 릴리스 빌드의 `objdump -d`에서 폴링 루프 안에 `ldr`가 있는지 확인 — CI에 grep 체크.
- V1: 고빈도 타이머 ISR + main 루프에서 동시에 증가 → 총합이 기대값보다 작으면 경쟁 상태.

</details>

### 4. 꼬리 질문

"Is volatile enough for a flag shared between two cores?"

<details><summary>답</summary>

"No. volatile only constrains the compiler; the other core can still see stores out of order, and nothing publishes the data that the flag guards. Use C11 atomics with release/acquire (`atomic_store_explicit(&flag, 1, memory_order_release)`), which emit the right barriers, or the RTOS / kernel primitive."

한국어 핵심: 멀티코어 = **atomic + release/acquire** (컴파일러 + CPU 순서 둘 다).

</details>

"When is volatile actually the right tool?"

<details><summary>답</summary>

"Memory-mapped I/O registers, variables shared with a signal handler or ISR on the same core (as `volatile sig_atomic_t` or a single-writer flag), and memory changed behind the compiler's back like a DMA buffer you poll — plus `setjmp`/`longjmp` locals. For thread synchronization it's the wrong tool."

한국어 핵심: **MMIO · 같은 코어 ISR 플래그 · setjmp** — 스레드 동기화엔 아님.

</details>

"Your delay loop disappeared. How do you write a delay properly?"

<details><summary>답</summary>

"Don't count iterations at all — poll a hardware timer: read the SysTick/DWT cycle counter or a free-running timer, and loop until `(now - start) >= ticks` with unsigned wrap-safe subtraction. A volatile counter loop 'works' but its duration changes with clock, flash wait states and compiler."

한국어 핵심: 지연 = **하드웨어 타이머 차이**(`now - start`, unsigned) — 반복 횟수 지연은 금지.

</details>

### 5. 채점 · 자주 틀리는 것

- L4를 "깨짐"으로 고름 — 불투명 호출이 있으면 재로드됨 (그래도 volatile 권장 이유까지 말하면 만점).
- 증상을 "무한 루프"로만 말함 — L1처럼 **루프가 사라져 그냥 통과**하는 경우도 실측.
- volatile = atomic 이라고 답함 (V1).
- volatile = memory barrier 라고 답함 (V2) — `dmb`/`dsb`, `dma_wmb()`.
- X1(같은 주소 두 번 쓰기)의 dead store 제거를 놓침 — FIFO/doorbell 레지스터에 치명적.

## H4 · 매크로 함정: `SQUARE(x++)`, 괄호, `do{}while(0)`, 다중 평가 (★ · 10분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 괄호 없는 매크로, 인자를 두 번 평가하는 매크로, 여러 문장짜리 매크로가 각각 어떤 값/동작을 만드는지 적기.
- **상황**: 펌웨어 코드베이스는 `BIT(n)`, `MAX()`, `REG_WRITE()` 같은 매크로 천지. `MAX(read_fifo(), 0)`처럼 **하드웨어 읽기를 두 번** 하면 FIFO에서 데이터가 사라진다. `if` 안의 다중 문장 매크로는 의도와 다르게 항상 실행된다.

| 줄 | 보는 것 | 적을 것 |
|---|---|---|
| (a)(b)(c) | 괄호 누락 | 펼친 식과 값 |
| (d)(e) | 인자 두 번 평가 | 값 + 부작용 횟수 |
| (f) | 같은 변수 두 번 수정 | UB 여부 |
| (g)(h) | 다중 문장 매크로 + if/else | 결과 / 컴파일 에러 |

- **예시를 말로 풀면**: 매크로는 함수가 아니라 **텍스트 치환**. `BAD_SQUARE(a + 1)`은 `a + 1 * a + 1`로 펼쳐진다.
- **지켜야 할 조건**: 각 줄을 먼저 손으로 펼쳐 쓰고 나서 계산.

### 1. Problem (English)

"Expand each macro by hand, tell me the result, and tell me how you'd rewrite the macro."

```c
#define BAD_SQUARE(x)  x * x
#define MAX(a, b)      ((a) > (b) ? (a) : (b))
```

- `BAD_SQUARE(a + 1)` with a = 3 → `7` (expands to `3 + 1*3 + 1`), not 16
- `MAX(i++, j)` with i = 5, j = 3 → returns `6`, and i becomes `7`
- `MAX(read_fifo(), 0)` with FIFO = 5, 9, 2 → returns `9` after **2** pops (the 5 is lost)

### 2. 먼저 물어볼 것 (clarify)

- "Is `read_fifo()` side-effecting — does each call pop an entry?" → Yes, like reading an RX data register.
- "Can I use C99 `static inline` / GNU statement expressions?" → `static inline` yes; GNU extensions avoid.
- "Do you want the value or just 'is it defined'?" → Both.

### 3. 10분 타임박스

| 분 | 할 일 |
|---|---|
| 0–1 | clarify (부작용 있는 인자?) |
| 1–5 | 각 줄 펼쳐 쓰기 → 연산자 우선순위로 계산 |
| 5–7 | 다중 평가 줄: 부작용 횟수 세기 |
| 7–9 | (f) UB, (g)(h) if/else |
| 9–10 | 고친 매크로 + `static inline` 제안 |

- 소리 내서: "A macro is text substitution, so I parenthesize every parameter and the whole body; and if an argument can have side effects, I switch to a static inline function, which evaluates each argument exactly once."
- 소리 내서: "Multi-statement macros get wrapped in do { … } while (0) so they behave like a single statement after an if and still need a semicolon."

<details><summary>힌트 1 (막히면 5분 후)</summary>

(c) `~BAD_BIT(3)` → `~1u << 3` → 단항 `~`가 `<<`보다 먼저 → `(~1u) << 3` = `0xFFFFFFFE << 3`. (b) `10 / (5) + (5)`.

</details>

<details><summary>힌트 2</summary>

(d) `?:`의 첫 피연산자 뒤에는 sequence point가 있다 → `MAX(i++, j)`는 **정의된 동작**(UB 아님), i가 비교에서 한 번, 선택된 가지에서 또 한 번 증가. (f) `((i++) * (i++))`는 `*` 양쪽 사이에 sequence point가 없음 → UB.

</details>

<details><summary>모범 답 (C)</summary>

| 줄 | 펼친 결과 | 값 (실측) | 분류 |
|---|---|---|---|
| (a) `BAD_SQUARE(a + 1)` | `a + 1 * a + 1` | **7** (`SQUARE` → 16) | 정의됨, 틀린 값 |
| (b) `10 / BAD_DOUBLE(5)` | `10 / (5) + (5)` | **7** (`DOUBLE` → 1) | 정의됨, 틀린 값 |
| (c) `~BAD_BIT(3)` | `~1u << 3` | **0xFFFFFFF0** (`~BIT(3)` → 0xFFFFFFF7) | 정의됨, 마스크 깨짐 |
| (d) `MAX(i++, j)` | `((i++) > (j) ? (i++) : (j))` | **6**, i = **7** | 정의됨 (`?:` sequence point) |
| (e) `MAX(read_fifo(), 0)` | read_fifo() 2번 | **9**, pop **2**회 | 정의됨, 데이터 손실 |
| (f) `SQUARE(i++)` | `((i++) * (i++))` | 12, i = 5 (clang·gcc 모두) | **UB** — 어떤 값도 "정답" 아님 |
| (g) `if (0) BAD_RESET(c);` | `if (0) c.len = 0; c.err = 0;` | len = 7, **err = 0** | 정의됨, 의도와 다름 |
| (h) `... BAD_RESET(c); else` | `if (r) c.len=0; c.err=0; else ...` | **compile error** | `else` 짝 없음 |

실제 출력 (`run.sh`):

```text
BAD_SQUARE(a+1)=7  SQUARE(a+1)=16
10/BAD_DOUBLE(5)=7  10/DOUBLE(5)=1
~BAD_BIT(3)=0xFFFFFFF0  ~BIT(3)=0xFFFFFFF7
MAX(i++, j) = 6, i is now 7
MAX(read_fifo(), 0) = 9 after 2 FIFO pops
BAD_RESET under false if: len=7 err=0
H4 PASS
```

(f)(h) — `H4_macros_ub.c`, `-Werror` 없이 `-fsanitize=undefined`로 빌드. UBSan은 unsequenced 수정은 **잡지 못한다**(런타임 보고 없음) — 컴파일 경고가 유일한 신호:

```text
H4_macros_ub.c:13:21: warning: multiple unsequenced modifications to 'i' [-Wunsequenced]
SQUARE(i++) = 12, i = 5   (UB: any result is 'correct')
H4_macros_ub.c:17:26: error: expected expression
```

(gcc 15.2 on Lima: `warning: operation on 'i' may be undefined [-Wsequence-point]`, 출력은 같은 12 / 5.)

```c
/* H4 · macro traps — each "BAD_" macro next to its fix; all well-defined. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define BAD_SQUARE(x)  x * x
#define SQUARE(x)      ((x) * (x))
#define BAD_DOUBLE(x)  (x) + (x)
#define DOUBLE(x)      ((x) + (x))
#define BAD_BIT(n)     1u << n
#define BIT(n)         (1u << (n))
#define MAX(a, b)      ((a) > (b) ? (a) : (b))      /* still evaluates twice */

/* multi-statement macro: BAD without do{}while(0) */
#define BAD_RESET(s)   (s).len = 0; (s).err = 0
#define RESET(s)       do { (s).len = 0; (s).err = 0; } while (0)

struct chan { int len, err; };

static int fifo_reads;                       /* stub "RX FIFO": pops 5, 9, 2 ... */
static int read_fifo(void) { static const int v[] = {5, 9, 2}; return v[fifo_reads++ % 3]; }

static inline int max_int(int a, int b) { return a > b ? a : b; }   /* the fix */

int main(void) {
    int a = 3;
    printf("BAD_SQUARE(a+1)=%d  SQUARE(a+1)=%d\n", BAD_SQUARE(a + 1), SQUARE(a + 1));
    assert(BAD_SQUARE(a + 1) == 7 && SQUARE(a + 1) == 16);     /* a+1*a+1 */

    printf("10/BAD_DOUBLE(5)=%d  10/DOUBLE(5)=%d\n", 10 / BAD_DOUBLE(5), 10 / DOUBLE(5));
    assert(10 / BAD_DOUBLE(5) == 7 && 10 / DOUBLE(5) == 1);    /* 10/5+5 */

    uint32_t m_bad = ~BAD_BIT(3), m_ok = ~BIT(3);
    printf("~BAD_BIT(3)=0x%08X  ~BIT(3)=0x%08X\n", (unsigned)m_bad, (unsigned)m_ok);
    assert(m_bad == 0xFFFFFFF0u && m_ok == 0xFFFFFFF7u);       /* (~1u)<<3 */

    int i = 5, j = 3;
    int r = MAX(i++, j);              /* ?: has a sequence point -> defined */
    printf("MAX(i++, j) = %d, i is now %d\n", r, i);
    assert(r == 6 && i == 7);         /* i incremented TWICE */

    fifo_reads = 0;
    int m = MAX(read_fifo(), 0);      /* hardware read evaluated twice! */
    printf("MAX(read_fifo(), 0) = %d after %d FIFO pops\n", m, fifo_reads);
    assert(m == 9 && fifo_reads == 2); /* compared 5, returned 9: data lost */

    fifo_reads = 0;
    m = max_int(read_fifo(), 0);
    assert(m == 5 && fifo_reads == 1);

    struct chan c = {7, 1};
    int enabled = 0;
    if (enabled) BAD_RESET(c);        /* expands to: if(enabled) c.len=0; c.err=0; */
    printf("BAD_RESET under false if: len=%d err=%d\n", c.len, c.err);
    assert(c.len == 7 && c.err == 0); /* err cleared anyway */

    c.err = 1;
    if (enabled) RESET(c); else c.len = 8;   /* with BAD_RESET this "else" won't compile */
    assert(c.len == 8 && c.err == 1);

    puts("H4 PASS");
    return 0;
}
```

복잡도: 해당 없음. 핵심 = **펼쳐 쓰기 → 우선순위 → 평가 횟수**.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

(e) `MAX(read_fifo(), 0)`, FIFO = [5, 9, 2]:

| 단계 | 펼친 코드 조각 | FIFO pop | 값 |
|---|---|---|---|
| 1 | `(read_fifo()) > (0)` | 5 pop | 5 > 0 → 참 |
| 2 | `? (read_fifo())` | 9 pop | 결과 9 |
| 3 | 반환 | 2회 pop | 9 반환, **5는 버려짐** |

(c) `~BAD_BIT(3)`:

| 단계 | 식 | 값 |
|---|---|---|
| 1 | 펼침 | `~1u << 3` |
| 2 | `~` 먼저 (단항 > 시프트) | `0xFFFFFFFE << 3` |
| 3 | 시프트 | `0xFFFFFFF0` — bit 0–2가 지워짐 (bit 3만 지우려던 의도) |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: `SQUARE(3)` = 9, `BIT(0)` = 1.
- 식 인자: `SQUARE(a + 1)`, `10 / DOUBLE(5)`, `~BIT(31)` = 0x7FFFFFFF.
- 부작용 인자: `MAX(read_fifo(), 0)` → pop 횟수 1이어야 함 (`static inline` 버전).
- 제어 흐름: `if (x) RESET(c); else ...` 컴파일 + 양쪽 가지 결과.
- 경계: `BIT(31)`은 `1u`라서 정의됨; `BIT(32)`는 여전히 UB (H5).

</details>

### 4. 꼬리 질문

"When would you still prefer a macro over a static inline function?"

<details><summary>답</summary>

"When it must be a compile-time constant — array sizes, case labels, `#if`, static initializers like `BIT(3)` in a register table — or when it needs `__FILE__`/`__LINE__` or stringizing, like an ASSERT or LOG macro. For anything that evaluates arguments at runtime, static inline is type-checked and single-evaluation."

한국어 핵심: 매크로 = **상수식·`#`·`__LINE__`**, 나머지는 `static inline`.

</details>

"How does the Linux kernel's `max()` avoid double evaluation?"

<details><summary>답</summary>

"It uses GNU statement expressions with `__typeof__`: it copies each argument into a local of the same type once, then compares the locals — and also adds a compile-time type check so `max(int, unsigned)` doesn't compile silently. That's a GNU extension, not ISO C."

한국어 핵심: `({ __typeof__(a) _a = (a); ... })` — GNU 확장, 타입 검사까지.

</details>

### 5. 채점 · 자주 틀리는 것

- 펼쳐 쓰지 않고 머릿속으로 계산 → (a)(b)를 16, 1로 적음.
- `MAX(i++, j)`를 UB라고 답함 — `?:`의 sequence point 때문에 정의됨 (다중 평가는 맞음).
- `SQUARE(i++)`에 숫자 하나를 "답"으로 적음 — UB라고 답해야 함.
- `do { } while (0)` 대신 `{ }`만 씌움 — `if (x) M(); else`에서 `;` 때문에 여전히 에러.
- 마스크 매크로 `BIT(n)` 안의 `(n)` 괄호와 `1u` 접미사 누락.

## H5 · 시프트 UB: `1<<31`, 폭 이상 시프트, 음수 `>>` (★★ · 10분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 시프트 7개를 "정의됨 / implementation-defined / UB"로 분류하고, arm64가 실제로 내는 값과 고친 식을 적기.
- **상황**: 레지스터 비트 정의(`#define ERR (1 << 31)`), 바이트 → 정수 조립(`b[0] << 24`), 가변 시프트(`x << n`, n이 32가 될 수 있음)는 **매일 쓰는 코드**고 UB가 숨어 있다. 특히 `b[0] << 24`는 b[0] ≥ 0x80이면 UB + 64비트로 넓히면 부호 확장.

| 줄 | 보는 것 | 적을 것 |
|---|---|---|
| (1)(3)(4) | signed 왼쪽 시프트 | 분류 + 실제 값 |
| (2)(5) | 폭 이상 시프트 | 분류 + arm64 값 |
| (4b) | int 결과를 uint64_t로 | 실제 값 |
| (6) | 음수 오른쪽 시프트 | 분류 + 값 |

- **예시를 말로 풀면**: `1 << 31`의 `1`은 signed int. 결과 0x80000000은 int로 표현 불가(INT_MAX = 0x7FFFFFFF) → UB. `1u << 31`은 정의됨.
- **지켜야 할 조건**: C11 기준 (C++20은 signed 시프트 규칙이 다름 — 여기선 C).

### 1. Problem (English)

"These shifts come from a register header and a packet parser. Classify each one — well-defined, implementation-defined, or undefined — tell me what arm64 actually produces, and fix it."

```c
#define ST_BUS_ERR (1 << 31)
uint32_t be32(const uint8_t *b) { return (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]; }
```

- `1 << 31` → UB in C11 (arm64 happens to give 0x80000000)
- `be32({80 00 00 01})` → UB; the same expression stored into a uint64_t gives `0xFFFFFFFF80000001`
- `x << 32` (x = 1, uint32_t) → UB; arm64 gives `1` at runtime (shift amount taken mod 32), gcc `-O2` constant-folds it to `0`

### 2. 먼저 물어볼 것 (clarify)

- "C or C++, and which standard?" → C11 (C++20 made signed left shift defined; C did not).
- "Can n be ≥ 32 in the variable shift?" → Yes, it comes from a field width.
- "What should a shift by ≥ 32 produce?" → 0 (logical), so we need a branch.

### 3. 10분 타임박스

| 분 | 할 일 |
|---|---|
| 0–1 | clarify (C11, n 범위) |
| 1–4 | 각 줄: 왼쪽 피연산자 승격 타입 + 시프트 양 범위 |
| 4–7 | 분류 3종 + arm64 값 |
| 7–9 | fix: `1u`, `(uint32_t)b[0] << 24`, `n >= 32 ? 0 : x << n`, `UINT64_C(1) << 40` |
| 9–10 | "UBSan으로 찾는다" 언급 |

- 소리 내서: "Two rules: the shift count must be less than the width of the promoted left operand, and a left shift of a signed value must not overflow or start negative. A uint8_t on the left is promoted to signed int, so `b[0] << 24` breaks the second rule for bytes ≥ 0x80."
- 소리 내서: "Right-shifting a negative value is implementation-defined, not undefined — gcc and clang do an arithmetic shift, which rounds toward minus infinity, unlike division."

<details><summary>힌트 1 (막히면 5분 후)</summary>

C11 6.5.7: (p3) 시프트 양이 음수이거나 **승격된 왼쪽 피연산자의 폭 이상**이면 UB. (p4) `E1 << E2`에서 E1이 signed이고 음수이거나 결과가 표현 불가면 UB. (p5) `E1 >> E2`에서 E1이 음수면 implementation-defined.

</details>

<details><summary>힌트 2</summary>

(5) `1 << 40`: 왼쪽 피연산자는 **int**(32비트) → 40 ≥ 32 → UB. 반환 타입이 uint64_t여도 연산은 int. arm64 `LSL Wd, Wn, Wm`은 시프트 양의 하위 5비트만 쓴다 → 40 mod 32 = 8 → 0x100.

</details>

<details><summary>모범 답 (C)</summary>

| 줄 | 분류 | arm64 실제 값 (실측) | 고친 식 |
|---|---|---|---|
| (1) `1 << 31` | **UB** (int overflow) | 0x80000000 | `1u << 31` |
| (2) `x << 32`, uint32_t | **UB** (폭 이상) | 1 (런타임, mod 32) / gcc -O2 상수 접힘 시 0 | `n >= 32 ? 0u : x << n` |
| (3) `-1 << 1` | **UB** (음수 왼쪽 시프트) | -2 | `(int)((unsigned)v << 1)` 또는 `v * 2` |
| (4) `b[0] << 24`, b[0]=0x80 | **UB** (int로 승격 후 overflow) | 0x80000001 | `(uint32_t)b[0] << 24` |
| (4b) 같은 식 → uint64_t | UB + 부호 확장 | **0xFFFFFFFF80000001** | 캐스트하면 0x0000000080000001 |
| (5) `1 << 40` → uint64_t | **UB** (int 폭 이상) | 0x100 (40 mod 32 = 8) | `UINT64_C(1) << 40` |
| (6) `-7 >> 1` | **implementation-defined** | -4 (산술 시프트, 내림) — `-7 / 2`는 -3 | 부호 있는 나눗셈 의도면 `/` |

UBSan 보고 (`H5_shifts_ub.c`, `-Werror` 없이 `-fsanitize=undefined`):

```text
H5_shifts_ub.c:7:76: runtime error: left shift of 1 by 31 places cannot be represented in type 'int'
H5_shifts_ub.c:8:77: runtime error: shift exponent 32 is too large for 32-bit type 'uint32_t' (aka 'unsigned int')
H5_shifts_ub.c:9:76: runtime error: left shift of negative value -1
H5_shifts_ub.c:11:18: runtime error: left shift of 128 by 24 places cannot be represented in type 'int'
H5_shifts_ub.c:14:18: runtime error: left shift of 128 by 24 places cannot be represented in type 'int'
H5_shifts_ub.c:17:76: runtime error: shift exponent 40 is too large for 32-bit type 'int'
H5_shifts_ub.c:16:67: runtime error: shift exponent 32 is too large for 32-bit type 'uint32_t' (aka 'unsigned int')
(1) 1 << 31          = 0x80000000
(2) 0x1u << 32       = 0x00000001
(3) -1 << 1          = -2
(4) be32(80 00 00 01)= 0x80000001
(4b) as uint64_t     = 0xFFFFFFFF80000001
(5) (uint64_t)(1<<40)= 0x100
(6) inlined 1u << 32 = 0x00000001
(7) const   1u << 32 = 0x00000001
```

"출력이 안정적이지 않다"의 실제 증거 — 같은 소스, Lima VM gcc 15.2, (7)은 상수 `32`로 인라인된 `1u << n`:

```text
gcc -O0:  (7) const   1u << 32 = 0x00000001
gcc -O2:  (7) const   1u << 32 = 0x00000000
```

Apple clang은 -O0 / -O2 모두 1이었다 — **컴파일러·최적화 수준마다 다른 답**이 UB의 정체. "x86에선 0이 나오더라" 같은 경험칙도 믿지 말 것 [미검증: x86 머신에서 실행 안 함].

고친 버전 (`-Werror` + ASan/UBSan 통과):

```c
/* H5 · shifts done right (the UB versions are in H5_shifts_ub.c). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define BIT(n)    (1u << (n))                 /* n in 0..31            */
#define BIT64(n)  (UINT64_C(1) << (n))        /* n in 0..63            */

static uint32_t be32(const uint8_t *b) {      /* cast BEFORE the shift */
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}
static uint32_t shl32(uint32_t x, unsigned n) {   /* defined for every n */
    return n >= 32 ? 0u : x << n;
}
static uint32_t rotl32(uint32_t x, unsigned n) {  /* n = 0 and n >= 32 safe */
    n &= 31u;
    return (x << n) | (x >> ((32u - n) & 31u));
}

int main(void) {
    static const uint8_t pkt[4] = {0x80, 0x00, 0x00, 0x01};
    assert(BIT(31) == 0x80000000u);
    assert(BIT64(40) == 0x10000000000ull);
    assert(be32(pkt) == 0x80000001u);
    uint64_t w = be32(pkt);                       /* no sign extension now */
    assert(w == 0x0000000080000001ull);
    assert(shl32(1u, 31) == 0x80000000u && shl32(1u, 32) == 0u && shl32(5u, 0) == 5u);
    assert(rotl32(0x80000001u, 1) == 0x00000003u);
    assert(rotl32(0x12345678u, 0) == 0x12345678u && rotl32(0x12345678u, 32) == 0x12345678u);
    /* -8 >> 1 is implementation-defined (C11 6.5.7p5); clang/gcc: arithmetic */
    int32_t neg = -8, neg7 = -7;
    printf("-8 >> 1 = %d (impl-defined), -7 >> 1 = %d, -7 / 2 = %d\n",
           neg >> 1, neg7 >> 1, neg7 / 2);
    assert((neg7 >> 1) == -4 && neg7 / 2 == -3);  /* >> floors, / truncates */
    printf("BIT(31)=0x%08X be32=0x%08X as u64=0x%016llX rotl(0x80000001,1)=0x%08X\n",
           (unsigned)BIT(31), (unsigned)be32(pkt), (unsigned long long)w,
           (unsigned)rotl32(0x80000001u, 1));
    puts("H5 PASS");
    return 0;
}
```

```text
-8 >> 1 = -4 (impl-defined), -7 >> 1 = -4, -7 / 2 = -3
BIT(31)=0x80000000 be32=0x80000001 as u64=0x0000000080000001 rotl(0x80000001,1)=0x00000003
H5 PASS
```

복잡도: O(1). 핵심은 **캐스트를 시프트 앞에**.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

(4b) `uint64_t v = (b[0] << 24) | ...;`, b = {80 00 00 01}:

| 단계 | 식 | 타입 | 값 |
|---|---|---|---|
| 1 | `b[0]` 승격 | int | 128 |
| 2 | `128 << 24` | int | 2^31 → 표현 불가 → **UB** (arm64 실제 비트: 0x80000000 = INT_MIN) |
| 3 | `\| b[3]` | int | 0x80000001 (음수 -2147483647) |
| 4 | uint64_t로 변환 | uint64_t | 음수 → 2^64 더함 → **0xFFFFFFFF80000001** |

고친 식 `(uint32_t)b[0] << 24`:

| 단계 | 식 | 타입 | 값 |
|---|---|---|---|
| 1 | `(uint32_t)b[0]` | uint32_t | 0x80 |
| 2 | `<< 24` | uint32_t | 0x80000000 (정의됨) |
| 3 | uint64_t 변환 | uint64_t | 0x0000000080000001 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: `be32({12 34 56 78})` = 0x12345678.
- 경계(최상위 비트): `be32({80 00 00 01})` = 0x80000001, **uint64_t로 받아도** 0x0000000080000001.
- 폭 경계: `shl32(x, 31)`, `shl32(x, 32)` = 0, `rotl32(x, 0)` = x, `rotl32(x, 32)` = x.
- 음수: `-7 >> 1` = -4 vs `-7 / 2` = -3 (반올림 방향 차이) — 부호 있는 ADC 값 스케일링에서 중요.
- 도구: 모든 단위 테스트를 `-fsanitize=undefined`로 한 번 더 돌림 — 값이 맞아도 UB 보고가 나오면 실패.

</details>

### 4. 꼬리 질문

"Your header has hundreds of `(1 << n)` bit definitions. Do you fix all of them?"

<details><summary>답</summary>

"Only n = 31 is actually undefined for int, but I'd switch the macro to `1u << (n)` globally anyway: it's free, it makes masks unsigned so `~MASK` and comparisons behave, and it removes the sign-extension trap when a mask is widened to 64 bits. A `BIT(n)` / `GENMASK(h, l)` helper avoids hand-written shifts entirely."

한국어 핵심: **전부 `1u`로** — UB는 31번 하나지만 `~MASK`·64비트 확장 문제까지 같이 해결.

</details>

"Why does arm64 give 1 for `1u << 32` but you said the answer could be 0?"

<details><summary>답</summary>

"The A64 variable shift instruction uses the shift amount modulo the register width, so a runtime shift by 32 is a shift by 0. But the C standard says it's undefined, so when the amount is a known constant the compiler can fold it to anything — gcc at -O2 gave 0 on the same machine. I write the behavior I want explicitly: `n >= 32 ? 0 : x << n`."

한국어 핵심: 하드웨어 동작(mod 32) ≠ C 의미(UB) — **원하는 동작을 분기로 명시**.

</details>

### 5. 채점 · 자주 틀리는 것

- `1 << 31`을 "그냥 0x80000000"이라고 답함 — C11에서 UB.
- `uint8_t << 24`를 unsigned 연산이라고 봄 — **int로 승격**.
- `uint64_t x = 1 << 40;`이 64비트 시프트라고 봄 — 대입 대상은 무관, `1`은 int.
- 음수 `>>`를 UB라고 답함 — implementation-defined (그리고 `/`와 반올림이 다름).
- 고친 코드에서 캐스트 위치가 틀림: `(uint32_t)(b[0] << 24)` (이미 늦음) vs `(uint32_t)b[0] << 24`.

## H6 · 드라이버 함수의 버그 5개 찾기 (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 4채널 DMA 컨트롤러의 채널 0 드라이버(ISR + 복사 함수 + 프레임 전송 래퍼, 약 30줄)에서 심어둔 버그 5개를 찾고, 각각이 실제 보드에서 어떤 증상을 내는지 말하고, 고쳐 쓰기.
- **상황**: 실리콘/보드 검증에서 가장 흔한 일 = **남이 쓴 bring-up 드라이버를 읽고 "왜 가끔 멈추지 / 왜 채널 1 완료가 사라지지"를 찾는 것**. 면접관은 코드를 주고 "what's wrong with this?"라고 묻는다.

| 함수 | 하는 일 | 레지스터 규칙 |
|---|---|---|
| `dma0_isr` | 완료/에러 인터럽트 처리, 상태 ack | `STATUS`는 **W1C**, 4채널이 **공유** (bit n = ch n DONE, bit 31 = BUS_ERR) |
| `dma0_copy` | 주소·길이 설정 → START → 완료 대기 → 에러 반환 | `LEN`은 **바이트** 단위, START는 HW가 스스로 clear |
| `send_frame` | 64바이트 프레임 하나 전송 | 프레임 길이 = `FRAME_LEN` (64) |

- **예시를 말로 풀면**: 정상이라면 `send_frame(tx, frame)`는 64바이트를 복사하고 0을 반환해야 하고, 채널 1이 동시에 끝나서 대기 중이던 완료 비트는 채널 0 ISR이 건드리면 안 된다. DMA가 고장 나면 영원히 멈추지 말고 에러를 돌려줘야 한다.
- **지켜야 할 조건**: 버그는 정확히 5개. 레지스터 매크로 `REG()`는 올바르게 volatile이다(그건 버그 아님). 위에서 아래로 **체크리스트**로 훑을 것.

### 1. Problem (English)

"This is the channel-0 driver for our DMA block. It passed a smoke test on the bench but we see hangs and lost completions on other channels in the system test. Walk me through it and find the bugs. For each one, tell me the symptom on real hardware."

```c
int dma0_copy(void *dst, const void *src, size_t nbytes);   /* 0 ok, <0 error */
void dma0_isr(void);
int send_frame(uint8_t *tx_buf, const uint8_t frame[FRAME_LEN]);
```

- Normal: `send_frame(tx, frame)` → copies 64 bytes, returns 0. (Buggy code: copies **8**.)
- Channel 1 DONE pending when channel 0 completes → must still be pending after `dma0_isr()`. (Buggy code: **cleared**.)
- DMA never completes (clock gated, bad address) → must return a timeout error. (Buggy code: **hangs**.)

### 2. 먼저 물어볼 것 (clarify)

- "Is STATUS write-1-to-clear, and is it shared with the other channels' drivers?" → Yes, W1C and shared (bit n per channel, bit 31 bus error).
- "Is LEN in bytes or in beats/words, and is it N or N−1?" → Bytes, exact count.
- "Is there an upper bound on how long a transfer can take?" → Max 64 KB at the bus clock → a few ms; treat > 100k polls as a timeout.
- "Is the data cache enabled on the buffers?" → Assume non-cacheable SRAM here (cache maintenance is a follow-up).

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (W1C 공유?, LEN 단위, timeout 기준) |
| 2–5 | 체크리스트를 종이 위에 먼저 적기 (아래 소리 내서 문장) |
| 5–12 | 위에서 아래로 줄마다 체크 → 버그 표시 + 증상 한 줄 |
| 12–17 | 고친 코드 (변경 줄만) |
| 17–20 | 테스트 4개 + 꼬리(캐시·경쟁 상태·채널 abort) |

- 소리 내서: "I'll scan it with a fixed checklist: shared state between ISR and main — volatile and races; bit definitions — unsigned; every register write — RMW on W1C or write-only registers; every loop — can it run forever; every length — right unit and not sizeof a pointer."
- 소리 내서: "For a W1C register you never do `|=`: the read returns every pending bit, and writing them back clears events you didn't handle — here, the other channels' completions."

<details><summary>힌트 1 (막히면 5분 후)</summary>

5개는 각각 다른 범주: ① ISR과 공유 변수 ② 비트 정의 상수 ③ W1C 레지스터 쓰기 방법 ④ 대기 루프 ⑤ 길이 계산. 범주마다 한 줄씩 찾으면 된다.

</details>

<details><summary>힌트 2</summary>

`send_frame`의 `frame` 인자는 `[FRAME_LEN]`으로 선언됐지만 포인터 (H2). `ST_BUS_ERR`는 `1 << 31` (H5). `REG(DMA_STATUS) |= ...`는 읽기 + OR + 쓰기 — 읽은 값에 다른 채널 DONE이 들어 있다.

</details>

<details><summary>모범 답 (C)</summary>

| # | 줄 (H6_dma_buggy.c) | 버그 | 실제 하드웨어 증상 | 수정 |
|---|---|---|---|---|
| 1 | 16 `static bool dma_done, dma_err;` | ISR 공유 변수에 **volatile 없음** | 릴리스 빌드에서 대기 루프가 플래그를 한 번만 읽음 → hang, 또는 루프 자체 삭제 | `static volatile bool` |
| 2 | 14 `#define ST_BUS_ERR (1 << 31)` | **signed 시프트 UB** (H5) | 대부분 "동작"하지만 UB; 64비트 마스크로 쓰면 0xFFFFFFFF80000000으로 부호 확장 | `(1u << 31)` |
| 3 | 22 `REG(DMA_STATUS) \|= ...` | **W1C에 RMW** | 읽은 값의 모든 pending 비트(ch1–3 DONE)를 다시 써서 **다른 채널 완료 이벤트 소실** → 그 채널 드라이버가 영원히 대기 | 처리한 비트만 plain write: `REG(DMA_STATUS) = st & (ST_DONE0 \| ST_BUS_ERR)` |
| 4 | 31 `while (!dma_done) { cpu_relax(); }` | **timeout 없음** | DMA가 안 끝나면(클럭 gate, 잘못된 주소, IRQ 미배선) 시스템 hang, 진단 정보 없음 | 횟수/타이머 상한 → 채널 abort + `-2` 반환 |
| 5 | 36 `sizeof(frame)` | **배열 인자 sizeof = 포인터 크기** (H2) | 64바이트 대신 **8바이트**만 전송, rc = 0이라 조용히 깨짐 | 길이를 인자로 받거나 `FRAME_LEN` 사용 |

호스트 모델(`H6_sim.c`: 공유 W1C STATUS, `cpu_relax()` 3회 후 완료 + ISR 호출)에서 **버그 코드를 실제로 돌린 결과** — `-Werror` 없이 `-fsanitize=undefined`:

```text
H6_dma_buggy.c:36:43: warning: sizeof on array function parameter will return size of 'const uint8_t *' (aka 'const unsigned char *') instead of 'const uint8_t[64]' (aka 'const unsigned char[64]') [-Wsizeof-array-argument]
H6_dma_buggy.c:20:14: runtime error: left shift of 1 by 31 places cannot be represented in type 'int'
H6_dma_buggy.c:22:35: runtime error: left shift of 1 by 31 places cannot be represented in type 'int'
rc=0  LEN programmed=8 (want 64)  tx[63]=0 (want 64)
channel-1 DONE still pending after ch0 ISR? NO - lost
--- H6 buggy, DMA never completes: hang mode: DMA never completes ...
HUNG -> killed by 2s watchdog
```

버그 5(경고), 2(UBSan), 3·5(모델 결과), 4(watchdog)가 전부 실측으로 드러난다. 버그 1은 호스트에선 안 보인다 — `cpu_relax()`가 불투명 함수라 매번 재로드되기 때문. 실제 Cortex-M4 빌드(`clang --target=thumbv7em-none-eabi -O2 -DTARGET`)에서 확인:

- `cpu_relax()` = `asm volatile("wfi")`일 때: clang·gcc(aarch64) 모두 루프 안에서 `dma_done`을 재로드했다(실측) — 즉 **잠복 버그**. 표준은 이걸 보장하지 않으며 asm에 `"memory"` clobber도 없다.
- busy-poll 빌드(`-D'cpu_relax()=((void)0)'`)에서는 `dma0_copy`가 아래처럼 컴파일됐다 — 레지스터 4개를 쓰고 **루프도, `dma_done = false`도, return도 없다**. 함수 끝에서 다음 함수로 그냥 흘러간다:

```text
dma0_copy:
	movs	r3, #0
	movt	r3, #16386        ; r3 = 0x40020000 (DMA base)
	str	r1, [r3]           ; SRC
	str	r0, [r3, #4]       ; DST
	movs	r0, #3
	str	r2, [r3, #8]       ; LEN
	str	r0, [r3, #12]      ; CTRL = START | IRQ_EN
.Lfunc_end1:                  ; <- no loop, no bx lr: falls into the next function
```

같은 빌드에서 고친 버전은 `ldrb r1, [r3]` 재로드 + 10만 회 카운트다운 + timeout 경로(`mvn r0, #1` = -2)를 그대로 유지했다(실측, `run.sh`엔 버그 버전만 출력).

고친 코드 — `cc ... -Werror -fsanitize=address,undefined H6_dma_fixed.c H6_sim.c`:

```c
/* H6 · MODEL ANSWER — the 5 bugs fixed (diff vs H6_dma_buggy.c, marked FIX n). */
#ifndef TARGET
#include <assert.h>
#endif
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "H6_sim.h"
#ifndef TARGET
#include <stdio.h>
#endif

#define CTRL_START    (1u << 0)
#define CTRL_IRQ_EN   (1u << 1)
#define ST_DONE0      (1u << 0)
#define ST_BUS_ERR    (1u << 31)                    /* FIX 2: unsigned shift   */
#define DMA_TIMEOUT   100000u                       /* spins; derive from max len */

static volatile bool dma_done, dma_err;             /* FIX 1: ISR-shared       */

void dma0_isr(void) {
    uint32_t st   = REG(DMA_STATUS);
    uint32_t mine = st & (ST_DONE0 | ST_BUS_ERR);
    if (mine & ST_BUS_ERR) dma_err = true;
    if (mine & ST_DONE0)   dma_done = true;
    REG(DMA_STATUS) = mine;                         /* FIX 3: W1C = plain write of my bits */
}

int dma0_copy(void *dst, const void *src, size_t nbytes) {
    dma_done = dma_err = false;
    REG(DMA_SRC)  = virt_to_bus(src);
    REG(DMA_DST)  = virt_to_bus(dst);
    REG(DMA_LEN)  = (uint32_t)nbytes;
    REG(DMA_CTRL) = CTRL_START | CTRL_IRQ_EN;
    for (uint32_t spins = 0; !dma_done; spins++) {  /* FIX 4: bounded wait     */
        if (spins >= DMA_TIMEOUT) {
            REG(DMA_CTRL) = 0;                      /* abort the channel       */
            return -2;                              /* timeout                 */
        }
        cpu_relax();
    }
    return dma_err ? -1 : 0;
}

int send_frame(uint8_t *tx_buf, const uint8_t *frame, size_t len) {
    return dma0_copy(tx_buf, frame, len);           /* FIX 5: caller passes length */
}

#ifndef TARGET
int main(void) {
    uint8_t *frame = &sim_sram[0], *tx = &sim_sram[512];

    sim_reset();                                    /* normal frame + ch1 pending */
    for (unsigned i = 0; i < FRAME_LEN; i++) frame[i] = (uint8_t)(i + 1);
    sim_hw_status = 1u << 1;
    assert(send_frame(tx, frame, FRAME_LEN) == 0);
    assert(sim_last_len == FRAME_LEN && tx[0] == 1 && tx[63] == 64);
    assert(sim_hw_status == (1u << 1));             /* ch1 DONE survived the ack */

    sim_reset(); sim_bus_err = 1;                   /* bus error reported      */
    assert(send_frame(tx, frame, 16) == -1);
    assert(sim_hw_status == 0);                     /* both of my bits acked   */

    sim_reset(); sim_never_complete = 1;            /* hardware never finishes */
    assert(send_frame(tx, frame, 16) == -2);
    assert((sim_regs[DMA_CTRL / 4u] & CTRL_START) == 0);

    sim_reset();                                    /* minimal 1-byte transfer */
    frame[0] = 0xA5; tx[0] = 0;
    assert(send_frame(tx, frame, 1) == 0 && tx[0] == 0xA5 && sim_last_len == 1);

    printf("normal/bus-err/timeout/1-byte OK, ch1 pending preserved\n");
    puts("H6 PASS");
    return 0;
}
#endif
```

```text
normal/bus-err/timeout/1-byte OK, ch1 pending preserved
H6 PASS
```

레지스터 접근 레이어 (`TARGET` = 실제 MCU, 기본 = 호스트 모델). 모델 본체는 `onsitePrep/n05/h/H6_sim.c`:

```c
/* H6 · register access layer. TARGET = real MCU; default = host simulation. */
#ifndef H6_SIM_H
#define H6_SIM_H
#include <stdint.h>

#define DMA_SRC      0x00u   /* bus address of source                     */
#define DMA_DST      0x04u   /* bus address of destination                */
#define DMA_LEN      0x08u   /* transfer length in BYTES                  */
#define DMA_CTRL     0x0Cu   /* bit0 START, bit1 IRQ_EN                   */
#define DMA_STATUS   0x10u   /* W1C. bit n = channel n DONE (n=0..3),     */
                             /*      bit 31 = BUS_ERR. Shared by all chans */
#define FRAME_LEN    64u

#ifdef TARGET
#define REG(off)     (*(volatile uint32_t *)(0x40020000u + (off)))
#ifndef cpu_relax                      /* -D"cpu_relax()=((void)0)" = busy-poll build */
#define cpu_relax()  __asm__ volatile("wfi")
#endif
static inline uint32_t virt_to_bus(const void *p) { return (uint32_t)(uintptr_t)p; }
#else
extern volatile uint32_t sim_regs[8];
#define REG(off)     (sim_regs[(off) / 4u])
void     cpu_relax(void);               /* host: advances the DMA model   */
uint32_t virt_to_bus(const void *p);    /* host: offset into sim_sram[]   */
/* knobs for tests */
extern uint8_t  sim_sram[1024];
extern int      sim_never_complete, sim_bus_err;
extern uint32_t sim_hw_status, sim_last_len;
void sim_reset(void);
#endif
void dma0_isr(void);
#endif
```

복잡도: 전송 O(1) CPU 작업 + 최대 `DMA_TIMEOUT`회 폴링. 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

버그 3 — STATUS의 W1C RMW. ch1이 먼저 끝나 DONE1 pending, 그다음 ch0 완료:

| 단계 | 동작 | HW STATUS (실제 상태) | 버스에 쓴 값 | 결과 |
|---|---|---|---|---|
| 0 | ch1 완료 | `0x00000002` | — | ch1 IRQ 대기 |
| 1 | ch0 완료 | `0x00000003` | — | ch0 ISR 진입 |
| 2 | 버그: `REG \|= DONE0 \| ERR` → 읽기 | 읽음 `0x00000003` | — | |
| 3 | OR 후 쓰기 | | `0x80000003` | W1C: 1인 비트 전부 clear |
| 4 | 결과 | `0x00000000` | | **DONE1 소실** → ch1 드라이버 영원히 대기 |
| 2' | 고침: `REG = st & (DONE0 \| ERR)` | 읽음 `0x00000003` | `0x00000001` | |
| 3' | 결과 | `0x00000002` | | ch1 DONE 유지 ✓ |

버그 5 — `send_frame(tx, frame)`:

| 단계 | 식 | 값 |
|---|---|---|
| 1 | `frame` 선언 `const uint8_t frame[FRAME_LEN]` | 실제 타입 `const uint8_t *` (C11 6.7.6.3p7) |
| 2 | `sizeof(frame)` | 8 |
| 3 | `REG(DMA_LEN) = 8` | 64바이트 중 8바이트만 전송, 반환값 0 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 64바이트 프레임 → rc 0, `LEN` 레지스터 = 64, 목적지 64바이트 일치.
- 다른 채널 pending 보존: ch1 DONE을 미리 세팅 → ch0 전송 후에도 DONE1 그대로.
- 에러: 모델에 BUS_ERR 주입 → rc -1, STATUS의 내 비트(DONE0, BUS_ERR)는 ack됨.
- 고장: DMA가 영원히 안 끝남 → rc -2 (timeout), 채널 START 해제됨, 시스템 계속 동작.
- 경계: 1바이트 전송, (실제 HW라면) 최대 길이 전송 시간 < timeout 인지 계산.
- 빌드: `-O2` 릴리스 빌드 asm에서 대기 루프 안에 `dma_done` load가 있는지.

</details>

### 4. 꼬리 질문

"The buffers are in cacheable DDR on a Cortex-A / Cortex-M7. What else is missing?"

<details><summary>답</summary>

"Cache maintenance and ordering. Before START, clean the source range to memory and invalidate (or clean+invalidate) the destination, then a `dsb` so the DMA sees the data before the doorbell; after DONE, invalidate the destination again before the CPU reads it, because speculative prefetch may have refilled the lines. Buffers should be cache-line aligned and padded so invalidation doesn't destroy neighbouring data."

한국어 핵심: **START 전 clean(src)/invalidate(dst) + `dsb`, 완료 후 invalidate(dst)**, 캐시 라인 정렬 ([LP08](2026-10-31_LP08_dma_iommu_coherency.html)).

</details>

"There's a race between `dma_done = false` and the interrupt. Where, and does it matter?"

<details><summary>답</summary>

"If a stale DONE0 from a previous transfer is still pending, the ISR can fire right after we clear the flag and before START, so we'd return early with the old completion. The fix is to ack any stale DONE0 in STATUS before clearing the flag and starting — or use a transfer sequence number set by the ISR. After a timeout we also have to abort and ack, so a late completion doesn't leak into the next call."

한국어 핵심: 시작 전 **stale DONE을 먼저 ack**, timeout 후엔 abort + ack — 이전 전송의 완료가 다음 호출로 새지 않게.

</details>

"How would you pick the timeout value?"

<details><summary>답</summary>

"From the worst case: max length divided by the slowest bus throughput, plus arbitration latency, times a safety margin like 10×. And measure in time, not loop iterations — read a free-running timer so the bound doesn't change with CPU clock or compiler. On timeout, log STATUS/CTRL/SRC/DST before aborting, because that's what you need to debug the hang."

한국어 핵심: **최악 길이 / 최저 처리량 × 마진**, 반복 횟수 말고 타이머로, timeout 시 레지스터 덤프 로그.

</details>

"Why is `REG(DMA_LEN) = nbytes` with a size_t risky even after the fix?"

<details><summary>답</summary>

"It silently truncates to 32 bits on a 64-bit host, and most DMA engines have a smaller LEN field — say 16 bits — so 70 000 bytes would wrap to 4 464. A production driver checks `nbytes == 0 or nbytes > DMA_MAX_LEN` and returns -EINVAL, or splits the transfer into chunks or a descriptor chain."

한국어 핵심: **LEN 필드 폭 검사** (0, 최대값) — 아니면 조용히 잘린 길이로 전송.

</details>

### 5. 채점 · 자주 틀리는 것

- `REG()` 매크로의 volatile만 보고 "volatile OK"라고 넘어감 — **ISR 공유 변수**의 volatile은 별개.
- W1C를 "`|=`로 1 쓰면 clear되니 맞다"고 봄 — 읽기 단계에서 다른 비트까지 끌려옴.
- timeout을 찾았지만 timeout 후 처리(채널 abort, 에러 코드, 로그)를 안 씀.
- `sizeof(frame)`을 64로 읽음 — 배열 인자는 포인터.
- 버그 5개만 나열하고 **증상**을 말 안 함 — 면접관은 "system test에서 무엇이 보였을까"를 듣고 싶어 한다.

## H7 · 선언 읽기: `const int *p` vs `int *const p`, 함수 포인터 typedef, 배열 포인터 (★ · 10분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 포인터/const/배열/함수 포인터 선언 10개를 영어로 읽고, 어떤 대입이 컴파일 에러인지, `sizeof`와 포인터 증가 폭을 적기.
- **상황**: 인터럽트 벡터 테이블(`isr_t vector_table[]`), 콜백 등록(`signal`-형 API), 읽기 전용 상태 레지스터(`const volatile uint32_t *`), 2차원 버퍼(`int (*row)[N]`) — 펌웨어 코드에서 매일 보는 선언들.

| 선언 | 적을 것 |
|---|---|
| `const int *p1`, `int const *p2`, `int *const p3`, `const int *const p4` | 무엇이 const인가, 어떤 대입이 에러인가 |
| `int *ap[4]` vs `int (*pa)[4]` | 영어로, sizeof, `+1` 증가 폭 |
| `isr_t`, `my_signal` | 영어로, typedef로 다시 쓰기 |

- **예시를 말로 풀면**: 오른쪽-왼쪽 규칙: 이름에서 시작해 오른쪽(`[]`, `()`)을 먼저, 막히면 왼쪽(`*`, `const`)으로. `int *const p3` = "p3 is a const pointer to int".
- **지켜야 할 조건**: LP64 (`int` 4, 포인터 8).

### 1. Problem (English)

"Read each declaration out loud. Then tell me which of these four assignments compile."

```c
const int *p1;   int *const p3 = &x;
*p1 = 3;   p1 = &y;   *p3 = 5;   p3 = &y;
```

- `const int *p1` → "pointer to const int": `p1 = &y` OK, `*p1 = 3` error
- `int *const p3` → "const pointer to int": `*p3 = 5` OK, `p3 = &y` error
- `int (*pa)[4]` → "pointer to array of 4 int": `sizeof pa` = 8, `pa + 1` moves 16 bytes

### 2. 먼저 물어볼 것 (clarify)

- "64-bit target?" → Yes, LP64.
- "Do you want the C-standard reading or what the compiler error says?" → Both; we'll show the real clang errors.

### 3. 10분 타임박스

| 분 | 할 일 |
|---|---|
| 0–1 | clarify |
| 1–5 | 각 선언에 오른쪽-왼쪽 규칙 적용, 영어로 쓰기 |
| 5–7 | 대입 4개 판정 |
| 7–9 | sizeof · `+1` 증가 폭 |
| 9–10 | `my_signal` typedef로 다시 쓰기 |

- 소리 내서: "const applies to whatever is immediately to its left — or to the right if it's first. So `const int *` and `int const *` are the same: the int is const; `int *const` makes the pointer itself const."
- 소리 내서: "Pointer arithmetic scales by the size of the pointee: `pa` points to a whole 4-int array, so `pa + 1` skips 16 bytes."

<details><summary>힌트 1 (막히면 5분 후)</summary>

`int (*pa)[4]`: 이름 pa → 괄호 안 먼저 → `*` "pointer to" → 괄호 밖 오른쪽 `[4]` "array of 4" → 왼쪽 `int`. `int *ap[4]`: ap → 오른쪽 `[4]` "array of 4" → 왼쪽 `*` "pointers to" → `int`.

</details>

<details><summary>힌트 2</summary>

`void (*my_signal(int sig, void (*h)(int)))(int)`: my_signal → 오른쪽 `(int, void(*)(int))` "function taking int and a handler" → 왼쪽 `*` "returning pointer to" → 바깥 오른쪽 `(int)` "function taking int" → `void`. typedef: `typedef void (*sighandler_t)(int); sighandler_t my_signal(int, sighandler_t);`

</details>

<details><summary>모범 답 (C)</summary>

| 선언 | 영어 (right-left rule) | 비고 (실측) |
|---|---|---|
| `const int *p1` / `int const *p2` | pointer to const int | 같은 타입 (`_Static_assert`로 증명) |
| `int *const p3` | const pointer to int | `p3 = &y` → error |
| `const int *const p4` | const pointer to const int | 둘 다 고정 |
| `int *ap[4]` | array of 4 pointers to int | `sizeof ap` = **32** |
| `int (*pa)[4]` | pointer to array of 4 int | `sizeof pa` = **8**, `sizeof *pa` = **16**, `pa+1` = **+16 B** |
| `int (*g)[4] = grid` | `grid[3][4]`가 첫 행 포인터로 decay | `g+1` = **+16 B** (다음 행) |
| `typedef void (*isr_t)(void)` | isr_t = pointer to function (void) returning void | 벡터 테이블 원소 |
| `void (*my_signal(int, void (*)(int)))(int)` | function (int, handler) returning pointer to function (int) returning void | = `sighandler_t my_signal(int, sighandler_t)` |

대입 4개: `*p1 = 3` **error** · `p1 = &y` OK · `*p3 = 5` OK · `p3 = &y` **error**. 실제 clang 에러 (`-DSHOW_ERRORS`):

```text
H7_declarations.c:66:9: error: read-only variable is not assignable
   66 |     *p1 = 3;                     /* error: read-only variable */
H7_declarations.c:67:8: error: cannot assign to variable 'p3' with const-qualified type 'int *const'
   67 |     p3 = &y;                     /* error: cannot assign to const pointer */
```

실제 출력:

```text
*p1=2 *p3=5 x=5 *p4=5
sizeof ap=32  sizeof pa=8  sizeof *pa=16
pa+1 moves 16 bytes, g+1 moves 16 bytes, (*pa)[2]=2
H7 PASS
```

타입 해석은 `__builtin_types_compatible_p`(gcc/clang 확장) + `_Static_assert`로 **컴파일 타임에 증명** — 하나라도 틀리면 빌드 실패:

```c
/* H7 · reading declarations (right-left rule). Types are PROVEN with
 * __builtin_types_compatible_p (gcc/clang). -DSHOW_ERRORS shows the
 * compile errors for writing through the const parts (run.sh records them). */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SAME(a, b) __builtin_types_compatible_p(a, b)

typedef void (*isr_t)(void);              /* pointer to function () -> void */
typedef int  (*cmp_fn)(const void *, const void *);

static void default_handler(void) {}
static void uart_isr(void) {}
static isr_t vector_table[4] = {default_handler, uart_isr, default_handler, default_handler};

/* "signal(): function taking (int, handler) returning handler" — raw vs typedef */
typedef void (*sighandler_t)(int);
void (*my_signal(int sig, void (*h)(int)))(int);
sighandler_t my_signal2(int sig, sighandler_t h);
static void h0(int s) { (void)s; }
void (*my_signal(int sig, void (*h)(int)))(int) { (void)sig; (void)h; return h0; }
sighandler_t my_signal2(int sig, sighandler_t h) { return my_signal(sig, h); }

int main(void) {
    int x = 1, y = 2;
    int row[4] = {0, 1, 2, 3};
    int grid[3][4] = {{0}};

    const int *p1 = &x;          /* pointer to const int   : *p1 read-only, p1 movable */
    int const *p2 = &x;          /* same type as p1                                    */
    int *const p3 = &x;          /* const pointer to int   : *p3 writable, p3 fixed    */
    const int *const p4 = &x;    /* both fixed                                         */
    int *ap[4];                  /* array of 4 pointers to int                         */
    int (*pa)[4] = &row;         /* pointer to array of 4 int                          */
    int (*g)[4] = grid;          /* grid decays to pointer to its first row            */

    _Static_assert(SAME(__typeof__(p1), __typeof__(p2)), "const int * == int const *");
    _Static_assert(!SAME(__typeof__(p1), __typeof__(p3)), "differs");
    _Static_assert(SAME(__typeof__(ap), int *[4]), "array of pointers");
    _Static_assert(SAME(__typeof__(pa), int (*)[4]), "pointer to array");
    _Static_assert(SAME(__typeof__(&grid[0]), int (*)[4]), "row pointer");
    _Static_assert(SAME(__typeof__(vector_table[1]), void (*)(void)), "isr_t");
    _Static_assert(SAME(__typeof__(&my_signal), __typeof__(&my_signal2)), "same fn type");

    p1 = &y;                     /* OK: the pointer itself is not const */
    *p3 = 5;                     /* OK: the pointee is not const        */
    printf("*p1=%d *p3=%d x=%d *p4=%d\n", *p1, *p3, x, *p4);
    assert(*p1 == 2 && x == 5 && *p4 == 5);

    ap[0] = &x;
    printf("sizeof ap=%zu  sizeof pa=%zu  sizeof *pa=%zu\n", sizeof ap, sizeof pa, sizeof *pa);
    assert(sizeof ap == 4 * sizeof(int *) && sizeof pa == sizeof(void *) && sizeof *pa == 16);

    /* pointer arithmetic scales by the POINTEE size */
    ptrdiff_t step_pa = (char *)(pa + 1) - (char *)pa;
    ptrdiff_t step_g  = (char *)(g + 1)  - (char *)g;
    printf("pa+1 moves %td bytes, g+1 moves %td bytes, (*pa)[2]=%d\n", step_pa, step_g, (*pa)[2]);
    assert(step_pa == 16 && step_g == 16 && (*pa)[2] == 2);

    vector_table[1]();           /* call through a function-pointer table */
    assert(vector_table[1] == uart_isr && my_signal2(0, h0) == h0);

#ifdef SHOW_ERRORS
    *p1 = 3;                     /* error: read-only variable */
    p3 = &y;                     /* error: cannot assign to const pointer */
#endif
    puts("H7 PASS");
    return 0;
}
```

복잡도: 해당 없음.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`void (*my_signal(int sig, void (*h)(int)))(int)` 읽기:

| 단계 | 위치 | 읽은 것 |
|---|---|---|
| 1 | 이름 | my_signal is |
| 2 | 오른쪽 `(int sig, void (*h)(int))` | a function taking (int, pointer to function(int) returning void) |
| 3 | 왼쪽 `*` (괄호 안) | returning a pointer to |
| 4 | 괄호 밖 오른쪽 `(int)` | a function taking int |
| 5 | 맨 왼쪽 `void` | returning void |

`int (*pa)[4] = &row;` 포인터 산술 (row 주소 = A):

| 식 | 타입 | 주소/값 |
|---|---|---|
| `pa` | `int (*)[4]` | A |
| `pa + 1` | `int (*)[4]` | A + 16 |
| `*pa` | `int [4]` → `int *`로 decay | A |
| `(*pa)[2]` | `int` | row[2] = 2 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: `p1 = &y`, `*p3 = 5` 컴파일 + 값 확인.
- 에러: `*p1 = 3`, `p3 = &y` → 컴파일 에러가 나야 "테스트 통과" (negative compile test).
- sizeof: `sizeof ap` = 4 × 포인터, `sizeof *pa` = 4 × int.
- 함수 포인터: `vector_table[1]()` 호출 → `uart_isr` 실행, `my_signal2(0, h0) == h0`.
- 타입 동등: `_Static_assert(SAME(typeof(p1), typeof(p2)))`.

</details>

### 4. 꼬리 질문

"What does `const volatile uint32_t *const STATUS` mean, and when would you write it?"

<details><summary>답</summary>

"A const pointer to a const volatile uint32_t: the pointer always points at the same address, software may not write the register, but every read really happens because hardware changes it. That's exactly a read-only status register mapped at a fixed address."

한국어 핵심: **주소 고정 + SW 쓰기 금지 + 매번 실제 읽기** = 읽기 전용 상태 레지스터.

</details>

"Can you cast away const and write through it?"

<details><summary>답</summary>

"The cast compiles, but writing to an object that was defined const is undefined behavior — it may live in flash or .rodata and fault, or the compiler may have constant-folded it. It's only legal if the original object wasn't const and the const came from a parameter type."

한국어 핵심: 원래 const로 정의된 객체에 쓰기 = **UB** (.rodata/flash).

</details>

### 5. 채점 · 자주 틀리는 것

- `const int *`와 `int *const`를 반대로 읽음.
- `int *ap[4]`와 `int (*pa)[4]`를 같은 것으로 봄 — 괄호가 바인딩을 바꾼다.
- `pa + 1`을 +4바이트로 계산 — 가리키는 대상(배열 16바이트) 단위.
- 함수 포인터 typedef에서 `*`를 빼먹음 (`typedef void isr_t(void)`는 함수 타입).
- `my_signal`을 "pointer to function"이라고 읽음 — 함수이고, **반환값**이 함수 포인터.

## H8 · signed/unsigned 비교, `size_t` 역방향 루프, `-1 < sizeof(x)` (★★ · 10분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: signed와 unsigned가 섞인 비교 7개의 출력을 맞히고, 역방향 루프·길이 계산·타이머 wrap 비교를 올바르게 고쳐 쓰기.
- **상황**: 검증 코드에서 `if (now >= deadline)` 타이머 비교가 카운터 wrap 순간에 틀리고, `for (size_t i = n - 1; i >= 0; i--)`가 버퍼 끝을 넘어 읽고, 프로토콜에서 받은 `int len = -1`이 memcpy에서 18경 바이트가 된다.

| 줄 | 보는 것 | 적을 것 |
|---|---|---|
| (a)(b)(c) | 섞인 비교 | 출력 |
| (d) | `size_t` 역방향 루프 | 방문하는 인덱스 |
| (e) | unsigned 뺄셈 | 출력 |
| (f) | 32비트 타이머 wrap | 출력 + 올바른 비교 |
| (g) | 음수 → size_t | 출력 |

- **예시를 말로 풀면**: (a) `-1 < sizeof(int)`: sizeof는 `size_t`(unsigned 64비트) → -1이 unsigned로 변환되어 18446744073709551615 → `< 4` 거짓.
- **지켜야 할 조건**: LP64, `size_t` = 64비트 unsigned. (c)는 H1 승격 규칙과 연결.

### 1. Problem (English)

"What does each line print? Then fix the loop, the length check, and the timer comparison."

```c
for (size_t k = n - 1; k >= 0; k--) printf("%zu ", k);       /* n = 3 */
uint32_t deadline = 0xFFFFFFF0u, now = 0x10u;  printf("%d\n", now >= deadline);
```

- `-1 < sizeof(int)` → `0`
- reverse loop with n = 3 → `2 1 0 18446744073709551615 18446744073709551614 ...` (never ends)
- `now >= deadline` → `0` although the deadline passed 0x20 ticks ago; `(int32_t)(now - deadline) >= 0` → `1`

### 2. 먼저 물어볼 것 (clarify)

- "Is the timer a free-running 32-bit up-counter that wraps?" → Yes.
- "Can n be 0 in the reverse loop?" → Yes — the fix must handle it.
- "Is `len` coming from an untrusted field (packet, register)?" → Yes, validate it.

### 3. 10분 타임박스

| 분 | 할 일 |
|---|---|
| 0–1 | clarify (타이머 wrap, n=0) |
| 1–5 | 각 줄: 두 피연산자의 **변환 후 타입** 먼저 → 값 |
| 5–8 | 고친 코드: `for (size_t i = n; i-- > 0; )`, `strlen(s) > 5`, `(int32_t)(a - b) >= 0` |
| 8–10 | "gcc `-Wextra`는 (a)(d)도 경고, clang은 안 함" 언급 |

- 소리 내서: "When an int meets an unsigned of the same or larger rank, the int is converted to unsigned — so -1 becomes the maximum value and every 'negative' check silently flips."
- 소리 내서: "For wrapping counters I never compare absolute values; I subtract in unsigned, which is exact modulo 2^32, and look at the sign of the difference — that's Linux's time_after()."

<details><summary>힌트 1 (막히면 5분 후)</summary>

(c1) `int8_t` -1과 `uint8_t` 255는 **둘 다 int로 승격**(H1) → -1 vs 255 → 다름. (c2) `int` -1과 `unsigned` → -1이 unsigned로 → 0xFFFFFFFF → 같음. 폭에 따라 결과가 반대.

</details>

<details><summary>힌트 2</summary>

(f) `now - deadline` = 0x10 - 0xFFFFFFF0 = 0x20 (mod 2^32). 이걸 int32_t로 보면 +32 → "deadline 이후 32틱". 반대 방향이면 0xFFFFFFE0 → int32_t -32. 2^31틱 이내 거리에서만 유효.

</details>

<details><summary>모범 답 (C)</summary>

| 줄 | 변환 | 출력 (실측) | 고친 식 |
|---|---|---|---|
| (a) `-1 < sizeof(int)` | -1 → size_t 2^64-1 | **0** | `-1 < (int)sizeof(int)` |
| (b) `i < u` (-1, 1u) | -1 → unsigned 0xFFFFFFFF | **0** | `i < 0 \|\| (unsigned)i < u` |
| (c1) `int8_t -1 == uint8_t 255` | 둘 다 int: -1 vs 255 | **0** | — |
| (c2) `int -1 == 0xFFFFFFFFu` | -1 → unsigned | **1** | — |
| (d) `size_t k = n-1; k >= 0` | unsigned는 항상 ≥ 0 | **2 1 0 18446744073709551615 …** (무한) | `for (size_t i = n; i-- > 0; )` |
| (e) `strlen("abc") - 5 > 0` | size_t 3 - 5 = 2^64-2 | **1** | `strlen(s) > 5` |
| (f) `now >= deadline` | 절대값 비교 | **0** (실제론 지남) | `(int32_t)(now - deadline) >= 0` → 1 |
| (g) `size_t sz = (int)-1` | -1 → 2^64-1 | **18446744073709551615** | 변환 전에 `len < 0` 검사 |

실제 출력 (`H8_snippets.c`, `-Werror` 없이 — 정의된 동작이라 UBSan 보고 없음):

```text
H8_snippets.c:11:65: warning: comparison of integers of different signs: 'int' and 'unsigned int' [-Wsign-compare]
H8_snippets.c:14:35: warning: comparison of integers of different signs: 'int' and 'unsigned int' [-Wsign-compare]
(a) -1 < sizeof(int)        -> 0
(b) -1 < 1u                   -> 0
(c) (int8_t)-1 == (uint8_t)255 -> 0,  -1 == 0xFFFFFFFFu -> 1
(d) for (size_t k = n-1; k >= 0; k--) visits: 2 1 0 18446744073709551615 18446744073709551614 ... (forced break)
(e) strlen("abc") - 5 > 0     -> 1  (value 18446744073709551614)
(f) now >= deadline           -> 0  (timer wrapped)
(g) size_t sz = (int)-1       -> 18446744073709551615
```

경고 차이 (실측): Apple clang `-Wall -Wextra`는 (b)(c2)만 경고하고 **(a)와 (d)는 조용**했다. Lima gcc 15.2 `-Wall -Wextra`는 (a)(b)(c2)에 `-Wsign-compare`, (d)에 `-Wtype-limits: comparison of unsigned expression in '>= 0' is always true`까지 냈다 → CI에 **두 컴파일러 모두** 돌리는 이유.

고친 버전:

```c
/* H8 · signed/unsigned done right (the surprising versions: H8_snippets.c). */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Linux-style wrap-safe "a is after b" for a free-running 32-bit counter.
 * (uint32_t -> int32_t of a value > INT32_MAX is implementation-defined,
 *  C11 6.3.1.3p3; gcc/clang/armcc define it as modulo 2^32.) */
static bool time_after_eq(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

/* mixed signed/unsigned "less than" without the implicit conversion */
static bool lt_int_uint(int a, unsigned b) { return a < 0 || (unsigned)a < b; }

/* reverse walk over a buffer: correct for n == 0 too */
static size_t reverse_sum_idx(const uint8_t *buf, size_t n, size_t *last_idx) {
    size_t sum = 0;
    for (size_t i = n; i-- > 0; ) { sum += buf[i]; *last_idx = i; }
    return sum;
}

/* length that may arrive as a signed int (from a protocol field) */
static int copy_checked(uint8_t *dst, size_t cap, const uint8_t *src, int len) {
    if (len < 0 || (size_t)len > cap) return -1;    /* check sign BEFORE converting */
    memcpy(dst, src, (size_t)len);
    return len;
}

int main(void) {
    assert(-1 < (int)sizeof(int));                   /* compare in the signed domain */
    assert(lt_int_uint(-1, 1u) && !lt_int_uint(5, 1u));   /* test the sign first */

    const char *s = "abc";
    assert(!(strlen(s) > 5));                        /* not strlen(s) - 5 > 0         */

    assert(time_after_eq(0x00000010u, 0xFFFFFFF0u)); /* 0x20 ticks late: deadline hit */
    assert(!time_after_eq(0xFFFFFFF0u, 0x00000010u));
    assert(time_after_eq(100u, 100u));

    static const uint8_t b[3] = {1, 2, 3};
    size_t last = 99;
    assert(reverse_sum_idx(b, 3, &last) == 6 && last == 0);
    last = 99;
    assert(reverse_sum_idx(b, 0, &last) == 0 && last == 99);   /* n == 0: no iterations */

    uint8_t dst[4];
    assert(copy_checked(dst, sizeof dst, b, -1) == -1);
    assert(copy_checked(dst, sizeof dst, b, 5) == -1);
    assert(copy_checked(dst, sizeof dst, b, 3) == 3 && dst[2] == 3);

    printf("time_after_eq(0x10, 0xFFFFFFF0)=%d  (int32_t)(0x10-0xFFFFFFF0)=%d\n",
           time_after_eq(0x10u, 0xFFFFFFF0u), (int)(int32_t)(0x10u - 0xFFFFFFF0u));
    puts("H8 PASS");
    return 0;
}
```

```text
time_after_eq(0x10, 0xFFFFFFF0)=1  (int32_t)(0x10-0xFFFFFFF0)=32
H8 PASS
```

복잡도: 각 O(1), 역방향 루프 O(n).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

(d) n = 3, `for (size_t k = n - 1; k >= 0; k--)`:

| 반복 | k (size_t) | `k >= 0` | 동작 |
|---|---|---|---|
| 1 | 2 | 참 | buf[2] |
| 2 | 1 | 참 | buf[1] |
| 3 | 0 | 참 | buf[0] |
| 4 | 0 - 1 = 18446744073709551615 | **참** | buf[2^64-1] → 범위 밖 읽기 |

고친 `for (size_t i = n; i-- > 0; )`, n = 0:

| 단계 | 조건 평가 | i 이후 | 동작 |
|---|---|---|---|
| 1 | `0 > 0` 거짓 (비교는 감소 전 값) | 2^64-1 (안 씀) | 루프 진입 안 함 ✓ |

(f) `time_after_eq(now=0x10, deadline=0xFFFFFFF0)`:

| 단계 | 식 | 값 |
|---|---|---|
| 1 | `now - deadline` (uint32_t) | 0x00000020 |
| 2 | `(int32_t)` | +32 |
| 3 | `>= 0` | 참 → deadline 지남 ✓ |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: `time_after_eq(200, 100)` 참, `time_after_eq(100, 200)` 거짓.
- wrap 경계: deadline = 0xFFFFFFF0, now = 0x10 → 참; 반대 → 거짓; 같음 → 참.
- 빈 입력: 역방향 루프 n = 0 → 반복 0회, 인덱스 변수 안 건드림.
- 에러 입력: `copy_checked(len = -1)` → -1, `len > cap` → -1.
- 한계: 두 시각 차이 ≥ 2^31틱이면 `time_after`도 틀림 — 타임아웃은 그보다 훨씬 짧게.

</details>

### 4. 꼬리 질문

"Why is the `(int32_t)` cast in time_after not itself undefined?"

<details><summary>답</summary>

"Converting an out-of-range unsigned to a signed type is implementation-defined, not undefined — C11 6.3.1.3p3 — and gcc, clang and Arm compilers all define it as modulo 2^N. C23 goes further and mandates two's complement representation. If I wanted zero reliance on that, I'd test the top bit: `((a - b) & 0x80000000u) == 0`."

한국어 핵심: unsigned → signed 범위 밖 변환 = **implementation-defined** (UB 아님); 완전 이식이 필요하면 최상위 비트 검사.

</details>

"How do you get a 64-bit timestamp from this wrapping 32-bit counter?"

<details><summary>답</summary>

"Keep a software high word incremented in the overflow interrupt, and read with the hi–lo–hi pattern so a wrap between the two reads is detected and retried. Or, if the hardware has a 64-bit counter split into two registers, the same hi–lo–hi loop applies."

한국어 핵심: **hi–lo–hi 재읽기** — [C 정리 C4](2026-10-29_c_basics_cram.html) 참고.

</details>

### 5. 채점 · 자주 틀리는 것

- (a)를 1이라고 답함 — sizeof는 unsigned.
- (c1)과 (c2)를 같은 답으로 적음 — 8비트는 int 승격, 32비트는 unsigned 변환.
- 역방향 루프를 `int i`로 고쳐서 `n > INT_MAX`나 sign-compare 경고를 남김 — `i-- > 0` 패턴이 정석.
- 타이머 비교를 `now >= deadline`으로 둠 — wrap 순간 49.7일(1 kHz) / 71.6분(1 MHz)마다 오동작.
- `int len`을 검사 없이 size_t로 넘김.

## H9 · 포인터 캐스트로 엔디안·타입 바꾸기 — strict aliasing, 정렬, memcpy가 정답인 이유 (★★ · 10분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: `*(uint32_t *)&f`, `*(uint32_t *)buf` 같은 포인터 캐스트가 왜 UB인지(strict aliasing, 정렬), `-O0`과 `-O2`에서 실제로 무엇이 다른지, 올바른 방법(`memcpy`, shift)을 쓰기.
- **상황**: float 센서 값의 비트 패턴 로깅, 수신 버퍼에서 32비트 필드 읽기, 엔디안 변환 — 펌웨어에서 가장 흔한 "포인터 캐스트 한 줄". -O0 디버그에선 되다가 -O2에서 값이 틀리거나, Cortex-M0에서 비정렬 접근으로 HardFault.

| 줄 | 보는 것 | 적을 것 |
|---|---|---|
| (1) `alias(&u.i, &u.f)` | strict aliasing | -O0 / -O2 값 |
| (2) `*(uint32_t *)&one` | float 비트 읽기 | 값 + UB 여부 |
| (3) `*(uint32_t *)buf` | 엔디안 | 값 |
| (4) `*(uint32_t *)(buf + 1)` | 정렬 | 값 + UB 여부 + 어떤 코어에서 fault |

- **예시를 말로 풀면**: 컴파일러는 "`int *`와 `float *`는 같은 메모리를 가리키지 않는다"고 가정해도 된다(C11 6.5p7). 그래서 `*f = 0.0f`가 `*i`를 바꿨을 리 없다고 보고 `return *i`를 상수 1로 바꾼다.
- **지켜야 할 조건**: little-endian arm64 호스트. 고친 함수 3개(`float_bits`, `load_le32`, `load_be32`)는 정렬·엔디안 무관해야 함.

### 1. Problem (English)

"A colleague's parser does `uint32_t v = *(uint32_t *)(rx + off);` and logs floats with `*(uint32_t *)&f`. It works in the debug build. What's wrong, what changes at -O2, and what would you write instead?"

```c
uint32_t float_bits(float f);
uint32_t load_le32(const uint8_t *p);   /* any alignment */
uint32_t load_be32(const uint8_t *p);   /* any alignment */
```

- `alias(&u.i, &u.f)` → `0` at -O0, **`1` at -O2** (same source)
- `*(uint32_t *)buf` with bytes 12 34 56 78 → `0x78563412` on little-endian
- `load_be32(buf + 1)` → `0x3456789A`; `load_le32(buf + 1)` → `0x9A785634`

### 2. 먼저 물어볼 것 (clarify)

- "What's the wire byte order?" → Big-endian network order for the packet; floats are host order.
- "Which cores does this run on?" → Cortex-M0+ and Cortex-A — so unaligned access must be avoided.
- "Is the code built with `-fno-strict-aliasing`?" → No (the Linux kernel is; most MCU projects aren't).

### 3. 10분 타임박스

| 분 | 할 일 |
|---|---|
| 0–1 | clarify (wire 엔디안, 코어, 컴파일 옵션) |
| 1–4 | (1)–(4) 분류: aliasing UB / 정렬 UB / 엔디안 의존 |
| 4–6 | -O0 vs -O2 값 예측 (1) |
| 6–9 | `memcpy` / shift 버전 작성 |
| 9–10 | "memcpy는 -O2에서 ldr 한 개" 언급 |

- 소리 내서: "The only pointer type that may alias anything is a character type. To reinterpret bytes as another type, memcpy into a local — the compiler turns a 4-byte memcpy into a single load, so it costs nothing."
- 소리 내서: "For wire formats I don't even memcpy: I assemble with shifts from individual bytes, which fixes the byte order and works at any alignment."

<details><summary>힌트 1 (막히면 5분 후)</summary>

C11 6.5p7: 객체는 자기 유효 타입(effective type), 그 signed/unsigned 짝, qualified 버전, 그걸 멤버로 가진 aggregate/union, 또는 **문자 타입**으로만 접근 가능. `float` 객체를 `uint32_t` lvalue로 읽는 건 해당 없음 → UB.

</details>

<details><summary>힌트 2</summary>

`alias()`를 -O2로 컴파일한 asm은 `mov w0, #1; ret` — `*i`를 다시 읽지 않는다. union으로 같은 주소를 넘기면 실제 메모리는 0.0f(= 0x00000000)인데 1을 반환. `-fno-strict-aliasing`으로 빌드하면 0.

</details>

<details><summary>모범 답 (C)</summary>

| 줄 | 분류 | -O0 (실측) | -O2 (실측) | 비고 |
|---|---|---|---|---|
| (1) `alias(&u.i, &u.f)` | **UB** (strict aliasing) | 0 | **1** | `-O2 -fno-strict-aliasing` → 0; Lima gcc -O2도 1 |
| (2) `*(uint32_t *)&one` | **UB** (strict aliasing) | 0x3F800000 | 0x3F800000 | "우연히" 동작 — 주변 코드에 따라 깨질 수 있음 |
| (3) `*(uint32_t *)buf` (정렬됨) | **UB** (aliasing: uint8_t 배열을 uint32_t로 읽음) + 엔디안 의존 | 0x78563412 | 0x78563412 | BE 호스트면 0x12345678 |
| (4) `*(uint32_t *)(buf + 1)` | **UB** (정렬) | 0x9A785634 | 0x9A785634 | UBSan 보고; arm64 일반 메모리는 HW가 처리 |

UBSan 보고 + `-O0`/`-O2` 차이 (`H9_aliasing_ub.c`, `-Werror` 없이):

```text
H9_aliasing_ub.c:17:75: runtime error: load of misaligned address 0x00016b932b31 for type 'const uint32_t' (aka 'const unsigned int'), which requires 4 byte alignment
0x00016b932b31: note: pointer points here
 00 00 00  12 34 56 78 9a bc de f0  3a 00 06 04 f3 59 13 b3  c0 31 93 6b 01 00 00 00  54 a0 12 8d 01
              ^
(1) alias(&u.i, &u.f) = 0
(2) bits of 1.0f via cast = 0x3F800000
(3) *(uint32_t *)buf       = 0x78563412  (bytes 12 34 56 78)
(4) *(uint32_t *)(buf + 1) = 0x9A785634  (misaligned)
--- H9 plain -O0: (1) alias(&u.i, &u.f) = 0
--- H9 plain -O2: (1) alias(&u.i, &u.f) = 1
--- H9 plain -O2 -fno-strict-aliasing: (1) alias(&u.i, &u.f) = 0
```

(주소 값은 실행마다 다름. UBSan은 aliasing 위반 (1)(2)는 **보고하지 않는다** — 정렬 (4)만 잡는다. aliasing은 TypeSanitizer 같은 별도 도구 영역 [미검증: 이 Mac에서 TySan 미실행].)

`alias()`의 -O2 asm (실측) — `*i`를 다시 읽지 않고 상수 반환:

```text
_alias:
	mov	w8, #1
	str	w8, [x0]          ; *i = 1
	str	wzr, [x1]         ; *f = 0.0f  (same address!)
	mov	w0, #1            ; return 1 — assumed *i unchanged
	ret
```

비정렬 (4)의 실제 하드웨어 동작: arm64(Apple, Linux)는 Normal 메모리의 비정렬 LDR을 하드웨어가 처리 → 값이 나옴(실측). Cortex-M0/M0+는 비정렬 LDR = HardFault, Cortex-M3/M4/M7은 LDR/STR 단일 접근은 허용(`CCR.UNALIGN_TRP`로 trap 가능)하지만 LDRD/LDM은 fault, Device 메모리는 비정렬 불가 [미검증: Arm 하드웨어 미실행, Arm 아키텍처 매뉴얼 기준].

고친 버전 — `memcpy`, union(C에서 허용), shift 조립:

```c
/* H9 · type punning done right: memcpy (or a union in C), shifts for endian. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t float_bits(float f) {        /* the portable way */
    uint32_t u;
    _Static_assert(sizeof u == sizeof f, "32-bit float");
    memcpy(&u, &f, sizeof u);                /* compiles to one fmov at -O2 */
    return u;
}
static float bits_float(uint32_t u) { float f; memcpy(&f, &u, sizeof f); return f; }

static uint32_t float_bits_union(float f) {  /* OK in C (C11 6.5.2.3 fn 95); NOT in C++ */
    union { float f; uint32_t u; } x = { .f = f };
    return x.u;
}

static uint32_t load32_native(const uint8_t *p) {  /* any alignment, host order */
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return v;
}
static uint32_t load_le32(const uint8_t *p) {      /* any alignment, fixed order */
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

int main(void) {
    assert(float_bits(1.0f) == 0x3F800000u && float_bits(-2.0f) == 0xC0000000u);
    assert(float_bits_union(1.0f) == 0x3F800000u);
    assert(bits_float(0x40490FDBu) > 3.14159f && bits_float(0x40490FDBu) < 3.14160f);

    static const uint8_t buf[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    assert(load_le32(buf) == 0x78563412u && load_be32(buf) == 0x12345678u);
    assert(load_le32(buf + 1) == 0x9A785634u && load_be32(buf + 1) == 0x3456789Au);
    const uint16_t probe = 1;                      /* endianness check via char view */
    int little = *(const uint8_t *)&probe == 1;    /* char-type access is allowed */
    assert(load32_native(buf + 1) == (little ? 0x9A785634u : 0x3456789Au));

    printf("float_bits(1.0f)=0x%08X  le32(buf+1)=0x%08X  be32(buf+1)=0x%08X  host=%s\n",
           (unsigned)float_bits(1.0f), (unsigned)load_le32(buf + 1),
           (unsigned)load_be32(buf + 1), little ? "little" : "big");
    puts("H9 PASS");
    return 0;
}
```

```text
float_bits(1.0f)=0x3F800000  le32(buf+1)=0x9A785634  be32(buf+1)=0x3456789A  host=little
H9 PASS
```

"memcpy는 공짜"의 실제 증거 — 위 함수들을 inline 안 되게(`-Dstatic=`) `-O2 -S`로 빌드한 arm64 asm:

```text
_float_bits:      fmov w0, s0       ; memcpy(&u, &f, 4) -> 1 instruction
_load32_native:   ldr  w0, [x0]     ; memcpy from any alignment -> 1 load
_load_le32:       ldr  w0, [x0]     ; 4 byte loads + shifts recognized -> 1 load
_load_be32:       ldr  w8, [x0]
                  rev  w0, w8       ; + byte reverse
```

복잡도: O(1), 실제 명령 1–2개.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

(1) `alias(&u.i, &u.f)` — 두 포인터가 같은 주소 A:

| 단계 | 소스 | 메모리 A | -O0 반환 | -O2 컴파일러 추론 |
|---|---|---|---|---|
| 1 | `*i = 1` | 0x00000001 | | `*i` = 1을 기억 |
| 2 | `*f = 0.0f` | 0x00000000 | | float* 쓰기는 int 객체를 못 바꾼다고 가정 |
| 3 | `return *i` | 0x00000000 | 0 (다시 읽음) | 1 (기억한 값) |

(4) `load_be32(buf + 1)` — bytes 34 56 78 9A:

| 단계 | 식 | 값 |
|---|---|---|
| 1 | `(uint32_t)p[0] << 24` | 0x34000000 |
| 2 | `\| (uint32_t)p[1] << 16` | 0x34560000 |
| 3 | `\| (uint32_t)p[2] << 8` | 0x34567800 |
| 4 | `\| p[3]` | 0x3456789A (정렬·호스트 엔디안 무관) |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: `float_bits(1.0f)` = 0x3F800000, `float_bits(-2.0f)` = 0xC0000000.
- 왕복: `bits_float(float_bits(x)) == x`, `bits_float(0x40490FDB)` ≈ π.
- 정렬: `buf`, `buf + 1`, `buf + 3` 오프셋 모두에서 load_le32/load_be32 결과 확인 + UBSan 무보고.
- 엔디안: 같은 바이트에서 `load_le32` vs `load_be32`가 바이트 역순 관계.
- 빌드: 단위 테스트를 `-O0`, `-O2` 둘 다 + `-fsanitize=undefined`로 실행 — (1)류 버그는 **-O2에서만** 드러난다.

</details>

### 4. 꼬리 질문

"Then why does the Linux kernel get away with casting pointers all over the place?"

<details><summary>답</summary>

"It's built with `-fno-strict-aliasing`, which tells gcc not to use type-based alias analysis — you can see the effect here: the same alias() returns 0 again at -O2 with that flag. And for unaligned data it uses `get_unaligned_le32()` / `get_unaligned_be32()` helpers rather than raw casts."

한국어 핵심: 커널 = **`-fno-strict-aliasing`** + `get_unaligned_*()` 헬퍼.

</details>

"Is type punning through a union legal?"

<details><summary>답</summary>

"In C, yes — reading a different member than the one last written reinterprets the bytes (C11 6.5.2.3, footnote 95), as long as the sizes match. In C++ it's undefined; there you use memcpy or C++20 `std::bit_cast`. memcpy works in both, so it's my default."

한국어 핵심: union 펀닝은 **C에선 허용, C++에선 UB** — 둘 다 되는 건 memcpy.

</details>

"How do you check the host's endianness without a cast that breaks aliasing?"

<details><summary>답</summary>

"Look at the first byte through an unsigned char pointer — character types may alias anything: `uint16_t one = 1; bool little = *(const uint8_t *)&one == 1;`. Or use the compiler's predefined `__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__` at compile time. But code that assembles with shifts doesn't need to know."

한국어 핵심: **char 포인터는 예외적으로 허용** / `__BYTE_ORDER__` / shift 조립이면 몰라도 됨.

</details>

### 5. 채점 · 자주 틀리는 것

- "-O0에서 잘 되니 괜찮다" — strict aliasing 위반은 **-O2에서만** 드러남 (실측 0 → 1).
- `memcpy`가 느리다고 답함 — 4바이트 고정 memcpy는 명령 1개.
- 정렬 문제를 "x86/arm64는 괜찮다"로 끝냄 — Cortex-M0 HardFault, Device 메모리, LDRD.
- wire 포맷을 `memcpy` 후 `ntohl`로 처리하면서 엔디안을 호스트에 의존 — shift 조립이 의도를 드러낸다.
- union 펀닝을 C++에서도 된다고 답함.

## 출처

1. ISO/IEC 9899:201x (C11) committee draft N1570 — 6.2.5p15 (char), 6.3.1.1 (integer promotions), 6.3.1.3 (signed/unsigned conversion), 6.3.1.8 (usual arithmetic conversions), 6.5p7 (aliasing), 6.5.7 (shift operators), 6.5.15 (conditional operator), 6.7.6.3p7 (array parameters), 6.8.5p6 (loop termination), 7.14.1.1p5 (signal handlers): https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
2. cppreference — Implicit conversions: https://en.cppreference.com/w/c/language/conversion
3. cppreference — Arithmetic operators (shifts): https://en.cppreference.com/w/c/language/operator_arithmetic
4. cppreference — volatile type qualifier: https://en.cppreference.com/w/c/language/volatile
5. cppreference — Objects and alignment / strict aliasing: https://en.cppreference.com/w/c/language/object
6. Clang UndefinedBehaviorSanitizer: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
7. Apple — Writing ARM64 code for Apple platforms (char is signed): https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms
8. Arm AAPCS64 (plain char is unsigned): https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst
9. Linux kernel `include/linux/jiffies.h` (time_after) · `include/linux/unaligned.h`: https://github.com/torvalds/linux/blob/master/include/linux/jiffies.h
10. 실측: 이 페이지의 모든 출력은 `onsitePrep/n05/h/run.sh` (macOS arm64, Apple clang, SDK 26.4) 및 Lima VM `elinux` (Ubuntu aarch64, gcc 15.2.0) 실행 결과.
