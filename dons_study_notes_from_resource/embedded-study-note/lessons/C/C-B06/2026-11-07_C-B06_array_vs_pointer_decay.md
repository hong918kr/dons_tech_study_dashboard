---
id: C-B06
time: 50분
updated: 2026-11-07
---
# C-B06 배열 vs 포인터: Decay, sizeof, 다차원 배열

## 학습 목표

- 배열이 포인터로 바뀌는 **decay(배열→포인터 변환)** 가 언제 일어나고, 언제 **안** 일어나는지(예외 3가지) 말할 수 있다.
- `arr`, `&arr`, `&arr[0]`, `*&arr` 의 **타입·sizeof·+1 이동량**을 손으로 예측한다.
- 함수 인자로 넘긴 배열의 `sizeof` 함정을 재현하고, 포인터를 넣으면 **컴파일 에러**가 나는 `ARRAY_SIZE` 를 만든다 (Linux 커널 `__must_be_array` 방식).
- 2차원 배열을 **배열 포인터 `T (*m)[COLS]`** 로 받아 행·열을 다루고, VLA 파라미터가 무엇인지 안다.
- 펌웨어의 lookup table, ADC DMA 버퍼, **DMA 디스크립터 배열**에서 이 지식이 어떻게 버그를 막는지 설명한다.

## 1. 문제

`memset(buf, 0, sizeof buf)` 가 함수 안에서 8바이트만 지운다 — 면접관이 "이 코드의 버그는?" 하고 내미는 단골이고, 실제 펌웨어에서는 **DMA 디스크립터 일부만 캐시 clean 되는** 식의 찾기 힘든 버그가 된다.

C에서 배열(array)은 "같은 타입 N개가 연속으로 놓인 객체"다. 포인터가 아니다. 하지만 배열 이름을 식에 쓰면 거의 항상 **첫 원소를 가리키는 포인터로 자동 변환**된다 (C11 6.3.2.1p3). 이것이 decay다. 그래서 `arr` 와 `&arr` 는 **주소는 같지만 타입이 다르고**, `+1` 했을 때 움직이는 거리도 다르다.

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">int32_t arr[5]; — 주소는 같고, 타입과 +1 의 크기가 다르다</text>
  <g stroke="currentColor" fill="none">
    <rect x="120" y="80" width="80" height="34"/><rect x="200" y="80" width="80" height="34"/>
    <rect x="280" y="80" width="80" height="34"/><rect x="360" y="80" width="80" height="34"/>
    <rect x="440" y="80" width="80" height="34"/>
  </g>
  <rect x="120" y="80" width="80" height="34" class="f-acc"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="160" y="102">arr[0]</text><text x="240" y="102">arr[1]</text><text x="320" y="102">arr[2]</text>
    <text x="400" y="102">arr[3]</text><text x="480" y="102">arr[4]</text>
    <text x="120" y="134" font-size="12">+0</text><text x="200" y="134" font-size="12">+4</text>
    <text x="280" y="134" font-size="12">+8</text><text x="520" y="134" font-size="12">+20</text>
  </g>
  <path d="M120 74 C 140 50, 180 50, 198 74" stroke="currentColor" fill="none"/>
  <text x="140" y="48" fill="currentColor" font-family="monospace">arr + 1</text>
  <path d="M120 140 C 220 190, 420 190, 518 140" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
  <text x="270" y="190" fill="currentColor" font-family="monospace">&amp;arr + 1</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="538" y="70">arr     → int32_t *</text>
    <text x="538" y="90">&amp;arr[0] → int32_t *</text>
    <text x="538" y="110">&amp;arr    → int32_t (*)[5]</text>
    <text x="538" y="130">*&amp;arr   → int32_t [5]</text>
  </g>
  <text x="10" y="222" fill="currentColor">세 식 모두 같은 주소를 출력하지만, arr + 1 은 4바이트, &amp;arr + 1 은 배열 전체(20바이트)를 건너뛴다.</text>
  <text x="10" y="243" fill="currentColor">맥 실측: arr=0x16b712ac4  &amp;arr=0x16b712ac4  arr+1=…ac8  &amp;arr+1=…ad8</text>
</svg>
```

### Part A — 예측: sizeof 와 +1 이동량 12개

`starter.c` 맨 위의 선언(`int32_t arr[5]`, `int16_t grid[3][4]`, `char msg[] = "ok"`, `const char *pmsg = "ok"`, `char tag[4] = "ABCD"`)을 보고, enum `P_...` 12개 값을 **실행 전에** 손으로 채운다. 64비트 호스트(포인터 8바이트) 기준. 틀린 것은 실행 결과를 보고 "왜"를 설명할 수 있을 때까지 고친다.

### Part B — 포인터를 거부하는 ARRAY_SIZE

- `IS_ARRAY(x)` — x가 진짜 배열이면 1, 포인터면 0 (GNU 확장 `__builtin_types_compatible_p` + `__typeof__`).
- `MUST_BE_ARRAY(x)` — 배열이 아니면 **컴파일 에러**, 배열이면 값 0인 정수 상수식. → `ARRAY_SIZE(x)` 에 더해서 포인터를 넣으면 빌드가 깨지게 한다. `_Static_assert` 의 파일 스코프 사용(`ARRAY_SIZE(rx_ring) == ARRAY_SIZE(rx_bufs)`)도 통과해야 한다.
- `key_fill(uint8_t (*key)[16], v)` — **배열 포인터** 파라미터로 받아서, 숫자 16을 쓰지 않고 `sizeof` 로 키 전체를 채운다.

### Part C — ADC DMA 2D 버퍼와 DMA 디스크립터 링

- `adc_channel_avg(frames, const uint16_t (*buf)[N_CH], ch)` — DMA 스캔 모드가 `[frame][ch]` 로 인터리브해서 채운 `adc_dma_buf[8][4]` 에서 채널 하나의 평균. 열 개수는 매크로가 아니라 `*buf` 에서 얻는다.
- `dma_ring_init(ring, n, uint8_t (*bufs)[RX_BLK])` — n개 디스크립터를 링으로 묶는다(buf/len/OWN/next, 마지막 → 처음). 반환값은 **링 전체 바이트 수**(캐시 clean 길이). 시작 코드의 `return sizeof ring;` 은 의도적인 버그다.

## 2. Problem (English)

> **Arrays vs. pointers.**
>
> 1. Given `int32_t arr[5]; int16_t grid[3][4]; char msg[] = "ok"; const char *pmsg = "ok"; char tag[4] = "ABCD";` on an LP64 host, state the values of `sizeof arr`, `sizeof &arr`, `sizeof &arr[0]`, `sizeof *&arr`, the byte distance of `arr + 1` and `&arr + 1`, `sizeof grid`, `sizeof grid[0]`, the byte distance of `grid + 1`, `sizeof msg`, `sizeof pmsg`, `sizeof tag`.
> 2. Write `ARRAY_SIZE(x)` that yields the element count as an integer constant expression and **fails to compile** when `x` is a pointer (including an array-typed function parameter). GNU extensions are allowed; it must build with `-std=c11`.
> 3. Write `uint16_t adc_channel_avg(size_t frames, const uint16_t (*buf)[N_CH], size_t ch)` for a DMA-interleaved ADC buffer `uint16_t adc[N_FRAMES][N_CH]`, returning 0 for an invalid channel or zero frames.
> 4. Write `size_t dma_ring_init(struct dma_desc *ring, size_t n, uint8_t (*bufs)[RX_BLK])` that links `n` descriptors into a circular chain, each owning one `RX_BLK`-byte buffer, and returns the number of bytes the ring occupies (to be passed to a D-cache clean).
>
> Example: `ARRAY_SIZE(grid) == 3`, `ARRAY_SIZE(grid[0]) == 4`, `ARRAY_SIZE("hello") == 6`; `ARRAY_SIZE(p)` with `int *p` → compile error.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "8/34 passed" (ARRAY_SIZE 값 검사·범위 밖 검사 등은 이미 통과) — TODO를 채워 34/34
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

## 4. 힌트

<details>
<summary>힌트 1 — 식마다 "타입"을 먼저 적는다</summary>

`arr` 는 `int32_t[5]`, `&arr` 는 `int32_t (*)[5]`, `&arr[0]` 은 `int32_t *`, `grid[0]` 은 `int16_t[4]`, `grid + 1` 은 `int16_t (*)[4]`. 타입을 알면 sizeof 와 +1 이동량은 저절로 나온다: **포인터 +1 = 가리키는 타입의 sizeof 만큼**.

</details>

<details>
<summary>힌트 2 — sizeof 는 decay 를 막지만, 괄호 안의 연산은 막지 못한다</summary>

`sizeof arr` 는 배열 그대로(20). 하지만 `sizeof(arr + 0)` 이나 `sizeof &arr[0]` 은 이미 포인터로 변한 값의 크기(8). 문자열 리터럴로 초기화한 `char msg[]` 는 NUL 포함 크기, `char *` 는 포인터 크기.

</details>

<details>
<summary>힌트 3 — 배열과 포인터를 타입으로 구별하는 법</summary>

배열 `a` 에 대해 `a` 의 타입은 `T[N]`, `&a[0]` 의 타입은 `T *` → **다르다**. 포인터 `p` 에 대해 `p` 와 `&p[0]` 은 둘 다 `T *` → **같다**. `__builtin_types_compatible_p(__typeof__(x), __typeof__(&(x)[0]))` 가 1이면 포인터다. `-std=c11` 에선 `typeof` 가 키워드가 아니므로 `__typeof__` 를 쓴다.

</details>

<details>
<summary>힌트 4 — _Static_assert 를 "식" 안에 넣기</summary>

C11은 구조체 멤버 선언 자리에 `_Static_assert` 를 허용한다 (6.7.2.1의 struct-declaration). 그래서 `sizeof(struct { _Static_assert(cond, "msg"); char c; })` 는 cond 가 거짓이면 컴파일 에러, 참이면 그냥 상수다. 여기에 `0 *` 를 곱하면 값 0인 상수식이 된다. (멤버가 하나도 없는 구조체는 C에서 UB라 `char` 멤버를 하나 둔다.)

</details>

<details>
<summary>힌트 5 — 배열 포인터의 * 는 다시 배열이다</summary>

`const uint16_t (*buf)[N_CH]` 에서 `*buf` 는 `const uint16_t[4]` — 배열이므로 `ARRAY_SIZE(*buf)` 가 4. 같은 이유로 `uint8_t (*bufs)[RX_BLK]` 의 `bufs[i]` 는 배열이라 `sizeof bufs[i]` 가 32. 링 전체 크기는 `n * sizeof ring[0]`.

</details>

## 5. 풀이

### Part A — 규칙 하나: "배열 타입의 식은 3가지 경우만 빼고 포인터로 바뀐다"

C11 6.3.2.1p3: 배열 타입의 식은 **① sizeof 의 피연산자 ② 단항 `&` 의 피연산자 ③ 배열을 초기화하는 문자열 리터럴** 일 때를 빼고, 첫 원소를 가리키는 포인터로 변환된다 (결과는 lvalue 가 아니다 → `arr = ...` 대입 불가).

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">배열 타입 식 E (T[N]) 가 나타나면 — decay 판정</text>
  <rect x="10" y="40" width="200" height="40" rx="6" class="f-acc"/>
  <text x="22" y="65" fill="currentColor">식 E 의 타입 = T[N]</text>
  <g stroke="currentColor" fill="none">
    <rect x="250" y="40" width="250" height="34" rx="4"/>
    <rect x="250" y="94" width="250" height="34" rx="4"/>
    <rect x="250" y="148" width="250" height="34" rx="4"/>
    <path d="M210 60 L248 57"/><path d="M375 74 L375 92"/><path d="M375 128 L375 146"/>
    <path d="M500 57 L540 57"/><path d="M500 111 L540 80"/><path d="M500 165 L540 90"/>
    <path d="M375 182 L375 200"/>
  </g>
  <g fill="currentColor">
    <text x="262" y="62">① sizeof E 인가?</text>
    <text x="262" y="116">② &amp;E 인가?</text>
    <text x="262" y="170">③ char s[] = "..." 초기화인가?</text>
    <text x="510" y="50" font-size="12">예</text>
    <text x="385" y="88" font-size="12">아니오</text><text x="385" y="142" font-size="12">아니오</text>
    <text x="385" y="196" font-size="12">아니오</text>
  </g>
  <rect x="542" y="40" width="170" height="60" rx="6" class="f-ok"/>
  <text x="552" y="64" fill="currentColor">배열 그대로</text>
  <text x="552" y="86" fill="currentColor" font-size="12">sizeof = N·sizeof(T)</text>
  <rect x="250" y="202" width="462" height="38" rx="6" class="f-warn"/>
  <text x="262" y="226" fill="currentColor">decay → T * (첫 원소 주소). 예: p = arr, f(arr), arr + i, arr[i]</text>
</svg>
```

> 표준 버전 차이: C11 원문에는 `_Alignof` 도 예외 목록에 있었지만 `_Alignof` 는 타입 이름만 받으므로 의미가 없어 C17에서 빠졌다. C23은 `typeof` 연산자를 예외에 추가했다 (GNU `__typeof__` 도 원래 decay 하지 않는다 — 그래서 `IS_ARRAY` 가 동작한다).

예측 정답표 (arm64 macOS 실측 = 정답):

| 식 | 타입 | 값 | 이유 |
|---|---|---|---|
| `sizeof arr` | `int32_t[5]` | 20 | 예외 ① — 배열 전체 |
| `sizeof &arr` | `int32_t (*)[5]` | 8 | 예외 ② — 결과는 *포인터* |
| `sizeof &arr[0]` | `int32_t *` | 8 | `arr[0]` = `*(arr+0)` → decay 후 원소 주소 |
| `sizeof *&arr` | `int32_t[5]` | 20 | 배열 포인터를 역참조하면 다시 배열 |
| `arr + 1` 이동 | `int32_t *` | 4 | 원소 하나 |
| `&arr + 1` 이동 | `int32_t (*)[5]` | 20 | 배열 하나 통째로 |
| `sizeof grid` | `int16_t[3][4]` | 24 | 3×4×2 |
| `sizeof grid[0]` | `int16_t[4]` | 8 | 한 행 |
| `grid + 1` 이동 | `int16_t (*)[4]` | 8 | 한 행씩 이동 |
| `sizeof msg` | `char[3]` | 3 | 예외 ③ — `'o' 'k' '\0'` |
| `sizeof pmsg` | `const char *` | 8 | 포인터. 리터럴 길이와 무관 |
| `sizeof tag` | `char[4]` | 4 | 자리가 없으면 NUL 없이 초기화 (6.7.9p14) — C++에선 에러 |

`char tag[4] = "ABCD"` 는 C에선 합법이지만 clang 21은 `-Wextra` 에서 `initializer-string for character array is too long ... did you mean to use the 'nonstring' attribute? [-Wunterminated-string-initialization]` 로 경고한다(실측). 의도된 것이면 `__attribute__((nonstring))` 로 표시한다.

`sizeof(arr + 0)` 처럼 sizeof 안에서 decay 를 일으키는 식도 clang이 잡는다: `sizeof on pointer operation will return size of 'int32_t *' ... [-Wsizeof-array-decay]` — 그래서 정답 코드에선 `sizeof &arr[0]` 를 썼다.

### Part B — 함수 인자의 sizeof 함정과 안전한 ARRAY_SIZE

**왜 함정인가**: 파라미터를 `uint8_t buf[64]` 로 써도 컴파일러는 이를 `uint8_t *buf` 로 **조정(adjust)** 한다 (6.7.6.3p7). `[64]` 는 문서일 뿐, 타입에 남지 않는다. `demo_sizeof_param.c` 로 확인:

```bash
cc -std=c11 -Wall -Wextra -O2 demo_sizeof_param.c -o demo_sizeof_param && ./demo_sizeof_param
```

```text
demo_sizeof_param.c:14:26: warning: sizeof on array function parameter will return size of 'uint8_t *'
  (aka 'unsigned char *') instead of 'uint8_t[64]' (aka 'unsigned char[64]') [-Wsizeof-array-argument]
   (그 외: [-Wsizeof-pointer-div] [-Wsizeof-pointer-memaccess] [-Wsizeof-array-decay])
6 warnings generated.
main      : sizeof(rx_buf) = 64
clear_buf : sizeof(buf) = 8, NAIVE_ARRAY_SIZE(buf) = 8
after clear: rx_buf[7] = 0x00, rx_buf[8] = 0xAA
sizeof(rx_buf + 0) = 8
ring_bytes(ring) = 8 (진짜는 64)
```

- 위 4가지 경고는 clang에서 **플래그 없이도 기본으로 켜져** 있다 (`cc demo.c` 만 해도 나옴 — 확인함). 경고를 끄거나 무시하는 레거시 빌드, 다른 벤더 컴파일러에서는 조용히 지나간다.
- 마지막 줄이 무서운 부분: 파라미터를 처음부터 `const uint32_t *ring` 으로 선언하면 **경고가 하나도 없다**. 컴파일러 입장에선 포인터의 sizeof 를 구한 정상 코드다.

**해결 = 타입으로 막기.** 배열 `a` 와 `&a[0]` 은 타입이 다르고, 포인터 `p` 와 `&p[0]` 은 같다:

| 인자 x | `__typeof__(x)` | `__typeof__(&(x)[0])` | 호환? | IS_ARRAY |
|---|---|---|---|---|
| `arr` | `int32_t[5]` | `int32_t *` | 아니오 | 1 |
| `"hello"` | `char[6]` | `char *` | 아니오 | 1 |
| `grid[1]` | `int16_t[4]` | `int16_t *` | 아니오 | 1 |
| `pmsg` | `const char *` | `const char *` | 예 | 0 |
| `&arr` | `int32_t (*)[5]` | `int32_t (*)[5]` | 예 | 0 |
| 파라미터 `int32_t a[5]` | `int32_t *` | `int32_t *` | 예 | 0 |

`__builtin_types_compatible_p` 는 최상위 한정자(const/volatile)를 무시하고 타입 호환성만 본다. 이 결과를 `_Static_assert` 에 넣어 **컴파일 에러**로 바꾼 것이 `MUST_BE_ARRAY` 다. Linux 커널의 고전 버전(`include/linux/kernel.h`, `compiler.h`, `build_bug.h`)은 같은 아이디어를 음수 폭 비트필드로 구현했다:

```c
#define __same_type(a, b)   __builtin_types_compatible_p(typeof(a), typeof(b))
#define BUILD_BUG_ON_ZERO(e) ((int)(sizeof(struct { int:(-!!(e)); })))
#define __must_be_array(a)  BUILD_BUG_ON_ZERO(__same_type((a), &(a)[0]))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))
```

커널은 `-std=gnu11` 로 빌드하므로 `typeof` 를 쓴다. **우리는 `-std=c11` 이라 `typeof` 가 없다** — `demo_array_size_error.c` 로 확인:

```bash
cc -std=c11 -Wall -Wextra -O2 -DUSE_TYPEOF demo_array_size_error.c -o demo_ase
#   error: unknown type name 'typeof'; did you mean 'typedef'?
cc -std=gnu11 -Wall -Wextra -Werror -O2 -DUSE_TYPEOF demo_array_size_error.c -o demo_ase && ./demo_ase
#   ARRAY_SIZE(ring) = 8, count_param = 8      (gnu11 / c23 에선 typeof OK)
```

`__typeof__` 는 예약 식별자(밑줄 2개)라 엄격 모드에서도 항상 쓸 수 있다 (`-pedantic` 까지 켜도 경고 0 — 확인함). 이제 포인터를 넣으면 (`-DBREAK`):

```bash
cc -std=c11 -Wall -Wextra -O2 -DBREAK demo_array_size_error.c -o demo_ase
```

```text
demo_array_size_error.c:24:12: error: static assertion failed due to requirement
  '!__builtin_types_compatible_p(struct dma_desc *, struct dma_desc *)': ARRAY_SIZE() needs an array, not a pointer
   24 |     return ARRAY_SIZE(d);            /* 에러 1: 파라미터는 포인터 */
(36행 ARRAY_SIZE(p), 37행 ARRAY_SIZE(&ring[0]) 도 같은 에러)
4 warnings and 3 errors generated.
```

경고(`-Wsizeof-pointer-div` 등)는 플래그에 따라 사라질 수 있지만, 이 **에러는 어떤 플래그로도 못 끈다** — 그게 커널 방식의 핵심이다.

**크기를 지키며 넘기는 법 — 배열 포인터**: `key_fill(uint8_t (*key)[16], v)` 의 `key` 는 "16바이트 배열 하나를 가리키는 포인터"다. `*key` 는 배열이므로 `sizeof *key == 16` 이 함수 안에서도 유지된다. 호출은 `key_fill(&keys[1], …)` 또는 `key_fill(keys + 2, …)` — 2차원 배열 `uint8_t keys[3][16]` 의 decay 결과가 바로 `uint8_t (*)[16]` 이기 때문이다. 길이가 다른 배열(`uint8_t k[32]`)의 주소를 넣으면 `-Wincompatible-pointer-types` 로 잡힌다.

### Part C — 2차원 배열은 "배열의 배열", 행 단위로 decay 한다

```svg
<svg viewBox="0 0 720 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">uint16_t adc_dma_buf[8][4] — 메모리엔 행 우선(row-major)으로 한 줄</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="50" width="240" height="34"/><rect x="260" y="50" width="240" height="34"/><rect x="500" y="50" width="200" height="34"/>
  </g>
  <rect x="20" y="50" width="240" height="34" class="f-acc"/>
  <rect x="260" y="50" width="240" height="34" class="f-mut"/>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="50" y="72">ch0</text><text x="110" y="72">ch1</text><text x="170" y="72">ch2</text><text x="230" y="72">ch3</text>
    <text x="290" y="72">ch0</text><text x="350" y="72">ch1</text><text x="410" y="72">ch2</text><text x="470" y="72">ch3</text>
    <text x="600" y="72">… frame 2..7</text>
    <text x="140" y="104">buf[0] (frame 0, 8B)</text><text x="380" y="104">buf[1] (frame 1)</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M20 112 C 80 150, 200 150, 258 112"/>
    <path d="M110 46 L110 36 L350 36 L350 46" stroke-dasharray="4 3"/>
  </g>
  <text x="70" y="160" fill="currentColor" font-family="monospace">buf + 1 → +8 바이트 (한 행 = 한 프레임)</text>
  <text x="360" y="40" fill="currentColor" font-size="12">← ch1 간격 = 8B (stride)</text>
  <g fill="currentColor">
    <text x="10" y="190" font-family="monospace">함수 인자: uint16_t adc[8][4]  →  const uint16_t (*buf)[4]   (int ** 아님!)</text>
    <text x="10" y="212">buf[f][ch] = *(*(buf + f) + ch): 행 f 로 8·f 바이트 이동 → 그 행(배열)이 decay → ch 원소.</text>
    <text x="10" y="232">컴파일러가 열 수 4 를 알아야 행 이동 거리를 계산할 수 있다 → 첫 차원만 생략 가능.</text>
  </g>
</svg>
```

- `T m[R][C]` 를 함수에 넘기면 **첫 차원만** 사라져서 `T (*m)[C]` 가 된다. 열 수 C는 타입에 남는다 → `ARRAY_SIZE(*buf)` 로 함수 안에서 4를 얻을 수 있고, 매크로 `N_CH` 와 어긋날 일이 없다.
- `int **` 는 "포인터들의 배열"을 가리키는 전혀 다른 구조다. `int m[3][4]` 를 `int **` 로 넘기면 `-Wincompatible-pointer-types`, 강제 캐스트하면 첫 원소 값을 주소로 읽어서 크래시.
- `adc_channel_avg(4, adc_dma_buf + 4, 1)` — `adc_dma_buf + 4` 는 5번째 프레임을 가리키는 `uint16_t (*)[4]` 다. **DMA 하프 전송(half-transfer) 인터럽트**에서 "앞쪽 절반 처리, 완료 인터럽트에서 뒤쪽 절반 처리" 하는 더블 버퍼링이 정확히 이 모양이다 (STM32 HAL `HAL_ADC_ConvHalfCpltCallback` / `ConvCpltCallback`).
- `const` 함정: 비-const 2D 배열을 `const uint16_t (*)[4]` 에 넘기는 것은 C23 이전 표준 문구로는 호환되지 않는 포인터 변환이다 (배열 원소의 한정자는 "배열 타입의 한정자"가 아니었음). clang 21은 `-pedantic` 에서도 조용했고(확인함), GCC는 `-Wpedantic` 에서 경고하는 것으로 알려져 있다. C23에서 규칙이 고쳐졌다.

```svg
<svg viewBox="0 0 720 235" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">struct dma_desc rx_ring[4] + uint8_t rx_bufs[4][32] — 디스크립터 링</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="45" width="150" height="56" rx="6"/><rect x="195" y="45" width="150" height="56" rx="6"/>
    <rect x="370" y="45" width="150" height="56" rx="6"/><rect x="545" y="45" width="150" height="56" rx="6"/>
    <path d="M170 73 L193 73"/><path d="M345 73 L368 73"/><path d="M520 73 L543 73"/>
    <path d="M620 101 L620 125 L95 125 L95 103" stroke-dasharray="5 3"/>
    <path d="M95 101 L95 108"/>
  </g>
  <g fill="currentColor">
    <text x="30" y="66">ring[0]  OWN</text><text x="30" y="88" font-size="12">len=sizeof bufs[0]</text>
    <text x="205" y="66">ring[1]  OWN</text><text x="380" y="66">ring[2]  OWN</text><text x="555" y="66">ring[3]  OWN</text>
    <text x="330" y="142" font-size="12">ring[3].next → ring[0] (링)</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="160" width="150" height="26"/><rect x="195" y="160" width="150" height="26"/>
    <rect x="370" y="160" width="150" height="26"/><rect x="545" y="160" width="150" height="26"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="40" y="178">rx_bufs[0] 32B</text><text x="215" y="178">rx_bufs[1] 32B</text>
    <text x="390" y="178">rx_bufs[2] 32B</text><text x="565" y="178">rx_bufs[3] 32B</text>
  </g>
  <text x="10" y="208" fill="currentColor">캐시 clean 길이 = n · sizeof ring[0] (= sizeof rx_ring = 96바이트).</text>
  <text x="10" y="228" fill="currentColor">함수 안의 sizeof ring 은 8 → 첫 디스크립터 일부만 clean 된다.</text>
</svg>
```

- `ring[i].len = sizeof bufs[i]` — `bufs[i]` 는 `uint8_t[32]` 배열이므로 decay 없이 32. 블록 크기를 바꿔도 이 줄은 고칠 필요가 없다.
- `next = (uintptr_t)&ring[(i + 1) % n]` — 마지막만 처음으로. 실제 칩(STM32 ETH DMA의 DES3, NXP eDMA TCD의 scatter-gather 주소)에선 32비트 버스 주소 필드다.
- 반환값: **포인터 파라미터 `ring` 의 sizeof 는 8** 이고 경고도 없다(앞의 demo 마지막 줄). 그래서 개수 `n` 을 받아 `n * sizeof ring[0]` 로 계산한다. 호출 쪽은 `ARRAY_SIZE(rx_ring)` 로 개수를 넘기고, 파일 스코프 `_Static_assert(ARRAY_SIZE(rx_ring) == ARRAY_SIZE(rx_bufs), …)` 로 디스크립터·버퍼 개수 불일치를 빌드 단계에서 막는다.
- Cortex-M7(STM32H7 등)에서 D-cache 가 켜져 있으면, CPU가 쓴 디스크립터는 `SCB_CleanDCache_by_Addr(ring, bytes)` 로 메모리에 내려보내야 DMA 가 본다. 길이가 8이면 링의 대부분이 캐시에만 남아 DMA 가 낡은 디스크립터를 읽는다 — 재현이 어려운 "가끔 패킷이 깨지는" 버그. (디스크립터 배열은 캐시 라인 32바이트 정렬도 필요 — C-B12/정렬 레슨 참고.)

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 34/34 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o sol_asan && ./sol_asan   # 34/34, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `void f(uint8_t buf[64]) { memset(buf, 0, sizeof buf); }` | 파라미터는 `uint8_t *` — 8바이트만 지움 | 길이를 인자로: `f(uint8_t *buf, size_t n)` + 호출부 `ARRAY_SIZE` / 또는 `uint8_t (*buf)[64]` |
| `#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))` 를 포인터에 사용 | 8/원소크기 → 엉뚱한 값, 경고는 플래그 의존 | `+ MUST_BE_ARRAY(a)` 로 컴파일 에러화 |
| `void g(int **m)` 에 `int m[3][4]` 전달 | `int[3][4]` 는 `int (*)[4]` 로 decay. `int **` 와 메모리 구조가 다름 | `void g(size_t rows, int (*m)[4])` |
| `char *s = "abc"; s[0] = 'X';` | 문자열 리터럴 수정은 UB (6.4.5p7) — 플래시/`__TEXT,__cstring` 에 있음 | `char s[] = "abc";` (배열로 복사) |
| `char tag[4] = "ABCD"; strlen(tag)` | NUL 없음 → 배열 밖까지 읽음 | `memcmp(tag, "ABCD", sizeof tag)` / `strnlen(tag, sizeof tag)` |
| `int a[5], b[5]; a = b;` | `error: array type 'int[5]' is not assignable` (실측) | `memcpy(a, b, sizeof a)` 또는 struct 로 감싸기 (연습 2) |
| `int *p = &arr;` | `int (*)[5]` → `int *` 불일치 (`-Wincompatible-pointer-types`, 실측) | `int *p = arr;` 또는 `int (*pa)[5] = &arr;` |
| `extern int *table;` (정의는 `int table[16];`) | 타입 불일치 → 배열 내용을 주소로 읽음 (UB) | 헤더에 `extern int table[16];` 하나로 통일 |

## 8. 면접 Q&A

<details>
<summary>Q1. 배열과 포인터는 같은 건가요? 차이를 세 가지 말해보세요.</summary>

아니다. 배열은 N개 원소의 **저장소 자체**, 포인터는 **주소를 담는 변수**다. ① `sizeof`: 배열은 전체 크기, 포인터는 4/8 ② 대입: 배열은 대입 불가(수정 가능한 lvalue 아님), 포인터는 가능 ③ `&`: `&arr` 는 `T (*)[N]`, `&p` 는 `T **`. 같아 보이는 이유는 식에서 배열이 첫 원소 포인터로 decay 하고, `a[i]` 가 `*(a + i)` 로 정의되어 있어서다 (6.5.2.1 — 그래서 `2[arr]` 도 합법).

</details>

<details>
<summary>Q2. (꼬리) 한 파일에 `int table[16];`, 다른 파일에 `extern int *table;` 이 있으면?</summary>

링커는 이름만 맞추므로 링크는 된다. 하지만 두 번째 파일은 `table` 위치의 첫 8바이트(배열 원소 값)를 **주소로 읽어서** 역참조 → 하드폴트/크래시. 선언 타입이 호환되지 않아 UB (6.2.7). 해결: 헤더 하나에 `extern int table[16];` 을 두고 정의 파일에서도 include → 컴파일러가 불일치를 잡는다. *Expert C Programming* 4장의 고전 사례.

</details>

<details>
<summary>Q3. 함수 안에서 넘겨받은 배열의 크기를 알 방법이 있나요?</summary>

포인터만 받으면 **없다**. 방법: ① 길이를 같이 넘김 (`buf, n` — 가장 흔함, 호출부에서 `ARRAY_SIZE`) ② 고정 크기면 배열 포인터 `T (*a)[N]` — `sizeof *a` 유지 ③ struct 로 감싸서 넘김 ④ C99 `void f(int a[static 16])` — 호출자가 최소 16개를 보장한다는 약속. 4개를 요구하는 함수에 2개짜리 배열을 넘기면 clang은 `array argument is too small; contains 2 elements, callee requires at least 4 [-Warray-bounds]`, `NULL` 에는 `-Wnonnull` 경고를 냈다 (실측). 단 `static` 이 있어도 함수 안 `sizeof a` 는 여전히 포인터 크기다.

</details>

<details>
<summary>Q4. 2차원 배열을 함수로 넘기는 올바른 시그니처는? int ** 는 왜 안 되나요?</summary>

`void f(size_t rows, int (*m)[COLS])` (또는 같은 의미인 `int m[][COLS]`). `int m[R][C]` 는 R×C 개가 연속으로 놓인 한 덩어리이고, decay 하면 "C개짜리 행을 가리키는 포인터"가 된다. `int **` 는 행 포인터 배열을 따로 둔 jagged 구조로, `m[i]` 를 계산할 때 메모리에서 **포인터를 한 번 더 읽는다**. 두 구조의 메모리 모양이 달라 호환되지 않는다. 열 수가 런타임 값이면 VLA 파라미터 `void f(size_t r, size_t c, int m[r][c])` (연습 3).

</details>

<details>
<summary>Q5. 커널의 ARRAY_SIZE 는 어떻게 포인터를 거부하나요? MSVC나 C++에서는?</summary>

`__builtin_types_compatible_p(typeof(a), typeof(&(a)[0]))` 로 "a 와 &a[0] 이 같은 타입이면 포인터"를 판정하고, 그 결과로 음수 폭 비트필드(`BUILD_BUG_ON_ZERO`) 또는 `_Static_assert` 를 터뜨려 컴파일 에러를 낸다. 성공 시엔 0을 더하므로 값은 그대로. GCC/clang 확장이라 IAR·옛 Keil armcc·MSVC 에서는 안 되므로 이식성 있는 코드는 `#ifdef __GNUC__` 로 순진한 버전으로 떨어진다. C++ 은 `std::size(arr)` (템플릿 `template<class T, size_t N> constexpr size_t size(const T(&)[N])` 은 포인터를 받으면 오버로드 실패 → 컴파일 에러).

</details>

<details>
<summary>Q6. 유명한 트릭 `*(&arr + 1) - arr` 은 요소 개수를 주나요?</summary>

실제로는 5가 나오지만 **표준상 UB** 다. `&arr + 1` 은 배열 끝 바로 다음(one-past-the-end) 포인터로, 만드는 건 합법이지만 평가되는 단항 `*` 의 피연산자로 쓰면 안 된다 (6.5.6p8). 컴파일 타임 상수도 아니다. 면접에선 "동작은 하지만 UB, `sizeof arr / sizeof arr[0]` 이 정답"이라고 답한다. 반면 `&arr + 1` 을 **비교나 끝 표시**로만 쓰는 것은 합법 (연습 6의 `end` 포인터).

</details>

<details>
<summary>Q7. VLA 를 펌웨어에서 써도 되나요? sizeof 는 어떻게 되나요?</summary>

C11에서 VLA 는 **선택 기능** (`__STDC_NO_VLA__` 가 정의되면 미지원). VLA **객체**를 스택에 만들면 크기가 런타임 값이라 스택 사용량을 정적으로 분석할 수 없고, 큰 값이면 스택 오버플로 → MISRA C:2012 Rule 18.8 이 금지. 다만 **VLA 파라미터 타입** `int m[r][c]` 는 스택을 할당하지 않고 `int (*m)[c]` 포인터일 뿐이라 스택 위험은 없다 — 다만 MISRA 18.8 은 VLA *타입* 자체를 금지하므로 MISRA 프로젝트에선 이것도 안 된다 (C23은 이런 가변 수정(variably modified) 타입을 필수로, 자동 저장 VLA 객체는 여전히 선택으로 정리). VLA 에 대한 `sizeof` 는 컴파일 상수가 아니라 **런타임에 평가**된다 (6.5.3.4p2). clang `-Wvla` 를 켜면 사용처를 전부 경고로 찾을 수 있다.

</details>

## 9. 추가 연습

### 연습 1 — 7-세그먼트 lookup table 경계 검사

`seg7_lut[]`(크기 미지정, 0~F 16개)로 `seg7_encode(d)` 를 만들어라. `d` 가 범위 밖이면 0(전부 꺼짐). 길이 숫자 16을 코드에 쓰지 마라.

<details>
<summary>답 보기</summary>

`return d < ARRAY_SIZE(seg7_lut) ? seg7_lut[d] : 0x00;` — 크기를 생략한 `[]` 배열은 초기화 목록 개수로 크기가 정해지고, `ARRAY_SIZE` 가 그 값을 컴파일 타임에 준다. 표에 문자를 추가해도 검사는 자동으로 따라온다. 테이블은 `static const` 라 Cortex-M 에선 플래시(.rodata)에 남는다 (C-B08 참고). 표 길이가 enum 과 맞아야 하면 `_Static_assert(ARRAY_SIZE(names) == STATE_COUNT, "...")` 로 고정한다.

</details>

### 연습 2 — 배열을 struct 로 감싸 값으로 복사·반환

`typedef struct { uint8_t b[6]; } mac_addr_t;` 로 `mac_broadcast()` 를 만들고, `b = a;` 대입으로 복사가 되는지 보여라.

<details>
<summary>답 보기</summary>

함수는 배열을 반환할 수 없고(6.7.6.3p1) 배열은 대입할 수 없지만, **배열을 멤버로 가진 구조체**는 둘 다 된다. `memset(m.b, 0xFF, sizeof m.b); return m;` — `m.b` 는 배열이라 sizeof 6. 구조체 크기도 6 (uint8_t 만 있어 패딩 없음). 큰 버퍼를 값으로 넘기면 스택 복사가 생기니 펌웨어에선 MAC 주소·키처럼 작은 것에만 쓴다.

</details>

### 연습 3 — VLA 파라미터로 n×n 행렬

`int32_t mat_trace(size_t n, const int32_t m[n][n])` 와 제자리 전치 `mat_transpose_sq(size_t n, int32_t m[n][n])`. 3×3, 4×4 로 같은 함수를 호출하라.

<details>
<summary>답 보기</summary>

`n` 이 `m` 보다 **먼저** 선언되어야 `m[n][n]` 에서 쓸 수 있다. 파라미터 타입은 `int32_t (*m)[n]` — 스택 할당은 없다. 전치는 `j = i + 1` 부터 위 삼각형만 교환해야 두 번 바꿔서 원상복구되는 실수를 피한다.

</details>

### 연습 4 — 프레임버퍼 바이트 오프셋과 행 포인터

`uint16_t fb[240][320]` (RGB565) 에서 `fb_offset(y, x)` 바이트 오프셋을 구하고 `(char *)&fb[y][x] - (char *)fb` 와 같은지 확인하라. `fb_fill_row(uint16_t (*row)[FB_W], color)` 로 한 행을 채워라.

<details>
<summary>답 보기</summary>

`(y * FB_W + x) * sizeof fb[0][0]` — 행 우선이라 `y` 가 W 배수로 곱해진다. `fb_fill_row(&fb[5], …)` 안에서 `ARRAY_SIZE(*row)` 가 320. LCD 컨트롤러/DMA2D 에 "라인 오프셋(stride)" 을 설정할 때 이 계산을 그대로 쓴다.

</details>

### 연습 5 — NUL 없는 고정 길이 필드

펌웨어 이미지 헤더 `struct fw_header { char magic[4]; uint32_t version; }` 의 magic 이 `"FWv1"` 인지 검사하라.

<details>
<summary>답 보기</summary>

`memcmp(h->magic, "FWv1", sizeof h->magic) == 0`. magic 은 NUL 이 없으므로 `strcmp`/`strlen` 은 배열 밖을 읽는다. 멤버에 `__attribute__((nonstring))` 을 붙이면 clang `-Wextra` 의 미종결 문자열 경고도 사라지고 의도가 문서화된다.

</details>

### 연습 6 — [begin, end) 포인터 범위

`int32_t sum_range(const int16_t *begin, const int16_t *end)` 를 만들고 `sum_range(samples, samples + ARRAY_SIZE(samples))` 로 호출하라.

<details>
<summary>답 보기</summary>

`for (p = begin; p != end; p++) s += *p;` — `samples + N` 은 one-past-the-end 포인터로, **만들고 비교하는 것은 합법**이지만 역참조하면 UB (6.5.6p8). 빈 범위(`begin == end`)는 0. C++ STL 반복자와 같은 관례.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed
```

## 10. 핵심 정리

- 배열 식은 **sizeof / 단항 & / 문자열 리터럴 초기화** 3경우만 빼고 첫 원소 포인터로 decay (C11 6.3.2.1p3).
- `arr` 와 `&arr` 는 주소 같고 타입 다름: `T *` (+1 = 원소) vs `T (*)[N]` (+1 = 배열 전체).
- 파라미터 `T a[N]` 은 `T *a` — 함수 안 `sizeof a` 는 포인터 크기. 길이를 같이 넘기거나 `T (*a)[N]` 로 받는다.
- 안전한 `ARRAY_SIZE` = `sizeof(a)/sizeof((a)[0]) + MUST_BE_ARRAY(a)`, 판정은 `__builtin_types_compatible_p(__typeof__(a), __typeof__(&(a)[0]))`. `-std=c11` 에선 `typeof` 대신 `__typeof__`.
- `T m[R][C]` → `T (*m)[C]` (첫 차원만 사라짐). `int **` 와 호환 안 됨. `*m` 은 다시 배열이라 `ARRAY_SIZE(*m) == C`.
- `char s[] = "ab"` 는 3바이트 배열, `char *p = "ab"` 는 8바이트 포인터(리터럴 수정 UB), `char t[2] = "ab"` 는 NUL 없음.
- DMA 디스크립터·LUT 의 크기는 **배열 자신에서** (`ARRAY_SIZE`, `sizeof arr`) 얻고, 개수 불일치는 `_Static_assert` 로 빌드 때 막는다.

## 참고자료

- C11 표준 초안 N1570 — 6.3.2.1p3 (배열→포인터 변환), 6.5.2.1 (첨자), 6.5.3.4 (sizeof), 6.5.6p8 (포인터 산술), 6.7.6.3p7 (배열 파라미터 조정, `static`), 6.7.9p14 (문자열 리터럴 초기화): https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
- [comp.lang.c FAQ 6장 — Arrays and Pointers](https://c-faq.com/aryptr/index.html)
- [Linux 커널 `include/linux/array_size.h` / `compiler.h` — ARRAY_SIZE, __must_be_array](https://github.com/torvalds/linux/blob/master/include/linux/array_size.h)
- [GCC 문서 — `__builtin_types_compatible_p`, `typeof`](https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html)
- [Clang 진단 목록 — -Wsizeof-array-argument, -Wsizeof-pointer-div, -Wsizeof-array-decay](https://clang.llvm.org/docs/DiagnosticsReference.html)
- Peter van der Linden, *Expert C Programming: Deep C Secrets* — 4장 "The Shocking Truth: C Arrays and Pointers Are NOT the Same!"
- [ST AN4839 — Level 1 cache on STM32F7/H7 (DMA 와 캐시 일관성)](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf)
- MISRA C:2012 — Rule 18.8 (VLA 금지), Rule 17.5 (배열 파라미터에 맞는 원소 수)
