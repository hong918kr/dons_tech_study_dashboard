---
id: C-B08
time: 50분
updated: 2026-11-07
---
# C-B08 const 정확히 쓰기: const T* vs T* const

## 학습 목표

- `T*` / `const T*` / `T* const` / `const T* const` 를 오른쪽→왼쪽으로 읽고, 각각 **무엇이 막히는지**(값 쓰기 vs 포인터 바꾸기) 바로 말할 수 있다.
- `char **` → `const char **` 가 왜 암묵 변환되지 않는지 "구멍(loophole)" 코드로 설명하고, `const char *const *` API를 쓸 수 있다.
- 드라이버 API를 **const-correct** 하게 설계한다: 읽기만 하는 버퍼는 `const uint8_t *`, 읽기 전용 상태 레지스터는 `const volatile`, 고정 베이스 포인터는 `T *const`.
- 큰 룩업 테이블에 `const` 를 붙이면 **섹션이 바뀌는 것**(Mach-O `__DATA,__data` → `__TEXT,__const`, ELF `.data` → `.rodata`)을 `size`/`nm` 으로 직접 확인하고, Cortex-M 에서 그것이 **RAM 절약**이라는 뜻임을 설명한다.

## 1. 문제

`const` 는 펌웨어에서 "컴파일러가 지켜주는 계약서"이자 "**플래시에 둘지 RAM에 둘지 정하는 스위치**"다. RAM 이 64 KB 인 MCU에서 4 KB 테이블에 `const` 하나를 빼먹으면 RAM 6%가 그냥 사라진다 — 그래서 면접에서 "const T* 와 T* const 차이"는 워밍업, "그게 메모리 배치에 어떤 영향?"은 꼬리질문으로 단골이다.

핵심 규칙은 하나다: **`const` 는 바로 왼쪽에 있는 것을 고정한다** (왼쪽에 아무것도 없으면 바로 오른쪽). 그래서 선언은 변수 이름에서 출발해 **오른쪽→왼쪽**으로 읽는다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">const 위치 4조합 — 무엇이 잠기나 (색칠된 칸 = 고정)</text>
  <g fill="currentColor"><text x="470" y="46">p = &amp;b</text><text x="590" y="46">*p = 9</text></g>
  <g fill="currentColor" font-family="monospace">
    <text x="10" y="74">① int *p</text>
    <text x="10" y="119">② const int *p</text>
    <text x="10" y="164">③ int *const p</text>
    <text x="10" y="209">④ const int *const p</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="250" y="56" width="60" height="28"/><rect x="360" y="56" width="60" height="28"/>
    <rect x="250" y="101" width="60" height="28"/>
    <rect x="360" y="146" width="60" height="28"/>
  </g>
  <rect x="360" y="101" width="60" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="250" y="146" width="60" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="250" y="191" width="60" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="360" y="191" width="60" height="28" class="f-warn" stroke="currentColor"/>
  <g stroke="currentColor">
    <line x1="310" y1="70" x2="356" y2="70"/><line x1="310" y1="115" x2="356" y2="115"/>
    <line x1="310" y1="160" x2="356" y2="160"/><line x1="310" y1="205" x2="356" y2="205"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="280" y="75">p</text><text x="390" y="75">a</text>
    <text x="280" y="120">p</text><text x="390" y="120">a</text>
    <text x="280" y="165">p</text><text x="390" y="165">a</text>
    <text x="280" y="210">p</text><text x="390" y="210">a</text>
    <text x="490" y="75">O</text><text x="610" y="75">O</text>
    <text x="490" y="120">O</text><text x="610" y="120">X</text>
    <text x="490" y="165">X</text><text x="610" y="165">O</text>
    <text x="490" y="210">X</text><text x="610" y="210">X</text>
  </g>
  <text x="10" y="245" fill="currentColor">② 잠긴 건 a (대상)   ·   ③ 잠긴 건 p 자신   ·   ④ 둘 다 잠김</text>
  <text x="10" y="263" fill="currentColor">int const *p 는 ② 와 완전히 같은 타입 (east const). 별표(*)의 왼쪽이냐 오른쪽이냐만 본다.</text>
</svg>
```

### Part A — const 위치 4조합 선언

`p_plain`, `p_ro`, `p_fixed`, `p_both` 4개 포인터 선언에 주석대로 `const` 를 붙인다. 테스트는 `_Generic` 으로 **`&p` 의 타입**을 검사한다 (`&p_fixed` 는 `int *const *` 여야 한다 — p 자신의 const 가 `&` 를 통해 드러난다). 각 조합에서 "컴파일이 실패해야 하는 줄"은 데모 파일 `demo_const_errors.c` 에서 `-DE1 … -DE8` 로 하나씩 켜서 **실제 clang 에러**를 확인한다.

### Part B — 이중 포인터와 const

- `k_cmds` 명령어 표를 "문자열도, 포인터 슬롯도 읽기 전용"(`const char *const []`)으로 만든다.
- `str_index(const char *const *v, size_t n, const char *key)` — 표에서 key 의 인덱스(없으면 -1).
- `skip_ws(const char *s)` — 앞 공백을 건너뛴 위치. **입력이 const 면 결과도 const**.
- 테스트에는 `char *argv_like[]` 를 넘기려면 **캐스트가 필요**한 장면이 있다 — 왜 그런지 설명할 수 있어야 한다 (`char **` → `const char **` 금지).

### Part C — const-correct UART 드라이버

가짜 `UART_TypeDef` 레지스터 블록과 드라이버 3개 함수가 이미 있다. **본문은 거의 손대지 말고 const 만** 바로잡는다.

- `SR` 은 하드웨어만 바꾸는 상태 레지스터 → `const volatile uint32_t`.
- `UART1` 베이스 포인터는 절대 다른 곳을 가리키면 안 된다 → `UART_TypeDef *const`.
- `crc8_nib[16]` 테이블은 바뀌지 않는다 → `const` (실칩에선 플래시에 산다).
- `uart_tx_ready(const UART_TypeDef *u)`, `uart_write(UART_TypeDef *u, const uint8_t *buf, size_t len)`, `crc8(const uint8_t *buf, size_t len)`.
- `crc8` 의 본문에는 **숨은 버그**가 있다. 시그니처에 `const` 를 붙이는 순간 컴파일러가 그 줄을 찾아준다.

## 2. Problem (English)

> **Const-correctness in a small driver.**
>
> 1. Declare four pointers to `int a`: plain `int *`; pointer-to-const; const pointer; const pointer to const. A compile-time check (`_Generic` on `&p`) must confirm each type. For each, list which of `p = &b` and `*p = 9` must fail to compile.
> 2. Write `int str_index(const char *const *v, size_t n, const char *key)` (return index or `-1`) and `const char *skip_ws(const char *s)`. Explain why a `char **argv` cannot be passed to `str_index` without a cast in C (but can in C++), and why `char **` → `const char **` would be unsafe.
> 3. Given a memory-mapped UART (`SR`, `DR`, `BRR`), make `SR` read-only (`const volatile`), make the base pointer immutable, put the CRC-8 nibble table in read-only storage, and fix the signatures of `uart_tx_ready`, `uart_write`, and `crc8` so that they promise not to modify what they only read. Fix any bug the compiler reveals.
> 4. Show, with `size`/`nm`, where a 4 KB lookup table lands with and without `const`, and explain the flash/RAM cost on a Cortex-M.
>
> Example: `crc8("123456789", 9) == 0xF4` (CRC-8/SMBUS check value) and the input buffer is unchanged afterwards.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "12/30 passed" — const 와 TODO 본문을 채워 30/30 을 만든다
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

## 4. 힌트

<details>
<summary>힌트 1 — 이름에서 출발해 오른쪽→왼쪽으로 읽는다</summary>

`const int *const p` → "p 는 / const / 포인터(*) / 가리키는 건 int / 그 int 는 const". 별표 **왼쪽** const = 가리키는 값, 별표 **오른쪽** const = 포인터 자신.

</details>

<details>
<summary>힌트 2 — 테스트가 왜 &amp;p 를 보나</summary>

`_Generic` 은 제어식에 lvalue 변환을 해서 **맨 바깥 qualifier 를 버린다** (C17 DR 481 에서 명확화, clang 동작) — `_Generic(p_fixed, …)` 로는 `int *const` 와 `int *` 를 구분 못 한다. `&p_fixed` 의 타입 `int *const *` 에서는 그 const 가 "한 단계 안쪽"이라 살아남는다.

</details>

<details>
<summary>힌트 3 — skip_ws 에서 s++ 는 합법인가?</summary>

`const char *s` 는 "s 가 가리키는 문자"가 const 일 뿐, **s 자신은 바꿔도 된다** (②번 조합). `while (*s == ' ' || *s == '\t') s++;`

</details>

<details>
<summary>힌트 4 — Part C 는 "시그니처 먼저, 컴파일러 따라가기"</summary>

`crc8` 시그니처에 `const` 를 붙이고 빌드해 보라. `error: read-only variable is not assignable` 이 가리키는 줄이 입력 버퍼를 몰래 고치던 버그다. 작업은 지역 변수 `crc` 에서 하면 된다.

</details>

## 5. 풀이

### Part A — 4조합과 실제 컴파일러 에러

| 선언 | 읽는 법 | `p = &b` | `*p = 9` | 펌웨어에서 쓰는 곳 |
|---|---|---|---|---|
| `int *p` | int 를 가리키는 포인터 | O | O | 출력 버퍼, 수정할 구조체 |
| `const int *p` (= `int const *p`) | **const int** 를 가리키는 포인터 | O | **X** | 입력 버퍼 `write(const uint8_t *buf, …)` |
| `int *const p` | int 를 가리키는 **const 포인터** | **X** | O | 고정 베이스 주소 `UART_TypeDef *const UART1` |
| `const int *const p` | const int 를 가리키는 const 포인터 | **X** | **X** | 플래시 표의 고정 핸들, `const char *const names[]` 의 슬롯 |

`demo_const_errors.c` 는 그냥 빌드하면 깨끗하고, `-DEn` 을 켜면 그 줄만 에러가 난다. 실제로 돌린 결과(Apple clang 21):

```bash
for e in E1 E2 E3 E4 E5 E6 E7 E8; do
  echo "== $e"; cc -std=c11 -Wall -Wextra -Werror -fsyntax-only -D$e demo_const_errors.c 2>&1 | grep -E 'error'
done
```

```text
== E1      # *p2 = 20;  (const int *)
demo_const_errors.c:28:9: error: read-only variable is not assignable
== E2      # p3 = &b;   (int *const)
demo_const_errors.c:31:8: error: cannot assign to variable 'p3' with const-qualified type 'int *const'
== E3      # *p4 = 40;
demo_const_errors.c:34:9: error: read-only variable is not assignable
== E4      # p4 = &b;
demo_const_errors.c:37:8: error: cannot assign to variable 'p4' with const-qualified type 'const int *const'
== E5      # int *q = p2;  (const 떼기)
demo_const_errors.c:40:10: error: initializing 'int *' with an expression of type 'const int *' discards qualifiers [-Werror,-Wincompatible-pointer-types-discards-qualifiers]
== E6      # const char **cpp = pp;  (pp 는 char **)
demo_const_errors.c:46:18: error: initializing 'const char **' with an expression of type 'char **' discards qualifiers in nested pointer types [-Werror,-Wincompatible-pointer-types-discards-qualifiers]
== E7      # const char *const *cpp = &s;
demo_const_errors.c:51:24: error: initializing 'const char *const *' with an expression of type 'char **' discards qualifiers in nested pointer types [-Werror,-Wincompatible-pointer-types-discards-qualifiers]
== E8      # u->SR = 0;  (const volatile 레지스터)
demo_const_errors.c:55:11: error: cannot assign to non-static data member 'SR' with const-qualified type 'const volatile uint32_t' (aka 'const volatile unsigned int')
```

눈여겨볼 점 두 가지:

- **E1~E4, E8 은 진짜 에러**: const 객체(또는 const lvalue)에 대입하는 것은 제약 위반(constraint violation, C11 6.5.16p2 "modifiable lvalue")이고 clang 은 무조건 에러로 낸다.
- **E5~E7 은 원래 경고**: `-Werror` 를 빼고 돌리면 `warning: ... discards qualifiers` 만 나오고 **오브젝트 파일이 만들어진다** (직접 확인함). 표준은 제약 위반에 "진단(diagnostic)"만 요구하므로 경고도 합법이다. 그래서 펌웨어 빌드에 `-Werror` (최소한 `-Werror=incompatible-pointer-types-discards-qualifiers`)가 필요하다.

규칙 근거: 포인터 대입(인자 전달·초기화도 같은 규칙, 6.5.2.2p7, 6.7.9p11)은 C11 **6.5.16.1p1** — "왼쪽이 가리키는 타입이 오른쪽이 가리키는 타입의 qualifier 를 **모두** 가져야 한다". 그래서 `int *` → `const int *` (qualifier 추가)는 암묵 변환 OK (6.3.2.3p2), 반대 방향(E5)은 금지.

### Part B — 왜 `char **` → `const char **` 는 안 되나

"한 단계 qualifier 추가"만 허용된다. `char **` → `const char **` 는 **가리키는 타입** `char *` 와 `const char *` 가 서로 다른 타입(호환 안 됨)이라 6.5.16.1 을 통과하지 못한다. 직관적으로 "const 를 더하는데 왜 안 돼?" 싶지만, 허용하면 아래처럼 **캐스트 없이 const 를 깨는 구멍**이 생긴다:

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">char ** → const char ** 가 허용된다면 (C FAQ 11.10의 구멍)</text>
  <rect x="560" y="50" width="160" height="34" class="f-warn" stroke="currentColor"/>
  <text x="572" y="72" fill="currentColor" font-family="monospace">const char c='A'</text>
  <rect x="300" y="50" width="160" height="34" class="f-mut" stroke="currentColor"/>
  <text x="312" y="72" fill="currentColor" font-family="monospace">char *p</text>
  <rect x="40" y="50" width="180" height="34" class="f-mut" stroke="currentColor"/>
  <text x="52" y="72" fill="currentColor" font-family="monospace">const char **pp=&amp;p</text>
  <g stroke="currentColor" fill="none">
    <path d="M220 67 L296 67"/><path d="M290 62 L296 67 L290 72"/>
    <path d="M460 67 L556 67" stroke-dasharray="5 3"/><path d="M550 62 L556 67 L550 72"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="40" y="120">① const char **pp = &amp;p;   // 가정: 허용됨</text>
    <text x="40" y="145">② *pp = &amp;c;               // 합법: *pp 는 const char *</text>
    <text x="40" y="170">③ *p = 'B';               // 합법: p 는 char * ... 그런데 c 는 const!</text>
  </g>
  <text x="40" y="205" fill="currentColor">② 에서 char *p 가 const 객체를 가리키게 됨(점선) → ③ 은 캐스트 없이 const 를 깨는 UB.</text>
  <text x="40" y="223" fill="currentColor">그래서 C 는 ① 을 막는다. C++ 은 ① 은 막고 const char *const * 로의 변환만 허용한다.</text>
</svg>
```

`demo_cast_away.c` 에서 ① 을 캐스트로 강제해 실제로 돌려 봤다:

```bash
cc -std=c11 -O2 demo_cast_away.c -o demo_cast_away && ./demo_cast_away; ./demo_cast_away global; echo "exit=$?"
cc -std=c11 -O0 demo_cast_away.c -o demo_cast_away0 && ./demo_cast_away0 global; echo "exit=$?"
```

```text
-- -O2, 인자 없음
(1) p == &k_limit ? 1  -> 이제 int *p 로 const 객체를 쓸 수 있는 상태
(2) local = 5, *w = 6                       ← 같은 메모리인데 값이 둘! 컴파일러가 local 을 5 로 접었다
-- -O2, global
(3) writing k_limit through p ...
(3) survived? k_limit = 100                 ← k_limit 으로의 store 자체가 사라짐 (clang -S 로 확인)
exit=0
-- -O0, global
(3) writing k_limit through p ...
exit=138                                    ← 128+10 = SIGBUS: __TEXT,__const 는 읽기 전용 페이지
```

같은 UB 가 최적화 수준에 따라 "조용히 무시"와 "즉시 크래시"로 갈렸다 — **UB 는 결과를 예측할 수 없다**는 것의 실물이다 (C11 6.7.3p6: const 로 *정의된* 객체를 non-const lvalue 로 수정하면 UB). Cortex-M 이라면 그 객체는 플래시에 있어서, 칩에 따라 BusFault/HardFault 가 나거나 쓰기가 무시되고 플래시 에러 플래그만 선다.

**그럼 `char *argv[]` 를 받는 함수는?** C 에서는 `char **` → `const char *const *` 조차 암묵 변환이 안 된다 (E7, C23 으로 빌드해도 같은 경고). 선택지는:

| 매개변수 타입 | `char **` 넘기기 | `const char **` 넘기기 | 예 |
|---|---|---|---|
| `char **` | O | 경고 | 거의 안 씀 |
| `char *const *` | O | 경고 | POSIX `execv(const char *path, char *const argv[])` — 바로 이 문제 때문에 고른 타입 (POSIX exec 의 Rationale 표) |
| `const char **` | 경고 | O | 함수가 슬롯에 새 포인터를 써 넣을 때 |
| `const char *const *` | 경고 → **캐스트** | O | 읽기 전용 문자열 표 (이 레슨의 `str_index`) |

우리 테스트의 `str_index((const char *const *)argv_like, 3, "file")` 캐스트는 **안전한** 캐스트다 — qualifier 를 *더하는* 방향이라 구멍이 생기지 않는다. 반대로 const 를 *떼는* 캐스트는 "원래 객체가 const 가 아님"을 내가 증명할 수 있을 때만 쓴다 (추가 연습 2).

**`skip_ws` 와 strchr 의 const 구멍**: 표준 `char *strchr(const char *s, int c)` 는 const 를 받아 non-const 를 돌려준다 — const 문자열 안의 포인터를 `char *` 로 세탁하는 구멍이다 (C 에 오버로딩이 없던 시절의 타협). C23 은 `strchr`/`memchr`/`strstr` 등을 **qualifier 를 보존하는 제네릭 함수**로 바꿨다 (`const char *` 를 넣으면 `const char *` 가 나온다). 우리 `skip_ws` 는 처음부터 `const char *` → `const char *` 로 설계했다.

**매개변수의 맨 바깥 const**: `int deref(int *const p)` 와 `int deref(int *p)` 는 **같은 함수 타입**이다 (C11 6.7.6.3p15: 호환성 판단 시 매개변수의 qualifier 는 무시). 테스트 `IS_TYPE(&deref, int (*)(int *))` 가 1인 이유다. 정의 쪽에서 "본문에서 p 를 다시 대입하지 않겠다"는 구현 메모일 뿐, 호출자에게는 아무 의미가 없다 → 헤더 프로토타입에는 쓰지 않는 게 관례.

### Part C — const 가 API 문서이자 버그 탐지기

```c
static uint8_t crc8(const uint8_t *buf, size_t len)   // const 만 붙이고 빌드하면 →
```

```text
starter.c:91:16: error: read-only variable is not assignable
        buf[i] ^= crc;                 /* 입력 버퍼를 작업 공간으로 재사용 */
```

시작 코드의 CRC 값(0xF4, 0x3B)은 **맞게** 나왔지만 호출자 버퍼 `msg` 가 망가졌다 (`memcmp(msg, "hi!", 3)` FAIL). 런타임 테스트로만 잡히던 버그가 const 하나로 **컴파일 타임 에러**가 됐다. 수정은 작업을 지역 변수에서: `crc = (uint8_t)(crc ^ buf[i]);`.

| 선언 | 의미 | 호출자 입장 |
|---|---|---|
| `bool uart_tx_ready(const UART_TypeDef *u)` | 레지스터를 읽기만 | const 핸들로도 상태 조회 가능 |
| `size_t uart_write(UART_TypeDef *u, const uint8_t *buf, size_t len)` | u 는 쓴다(DR), buf 는 읽기만 | `static const uint8_t k_banner[]` (플래시 문자열)을 **캐스트 없이** 바로 넘길 수 있다 |
| `uint8_t crc8(const uint8_t *buf, size_t len)` | 입력 불변 약속 | 받은 패킷을 CRC 확인 후 그대로 재사용 가능 |
| `const volatile uint32_t SR` | 프로그램은 못 쓰고(const), 하드웨어가 바꾸니 매번 읽어라(volatile) | `u->SR = 0` 은 E8 처럼 컴파일 에러 |
| `UART_TypeDef *const UART1` | 베이스 주소 고정 | `UART1 = …` 실수 방지 |

non-const 시그니처의 비용은 호출자에게 전가된다. `uart_write(UART1, k_banner, n)` 을 `uint8_t *buf` 버전에 넘기면 실제로 이렇게 된다:

```text
error: passing 'const uint8_t[3]' (aka 'const unsigned char[3]') to parameter of type 'uint8_t *' (aka 'unsigned char *') discards qualifiers [-Werror,-Wincompatible-pointer-types-discards-qualifiers]
```

→ 호출자가 `(uint8_t *)` 캐스트를 쓰기 시작하고, 그 캐스트가 진짜 버그를 숨긴다. **const 는 아래(라이브러리)에서 위로 전파**된다 — 리프 함수부터 const-correct 하게.

CMSIS 헤더가 바로 이 패턴이다: `#define __I volatile const` (읽기 전용 레지스터), `__O volatile` (쓰기 전용), `__IO volatile`. (C++ 로 컴파일될 때는 `__I` 가 `volatile` 만 — const 멤버가 있으면 C++ 에서 구조체 생성·대입 규칙이 까다로워지기 때문.)

### 관찰 — 4 KB 테이블: const 하나로 섹션이 바뀐다

`demo_table.c` 의 `TABLE_QUAL uint32_t big_table[1024]` 를 `-DUSE_CONST` 유무로 빌드했다.

```bash
cc -std=c11 -O2 -c demo_table.c -o t_data.o
cc -std=c11 -O2 -c -DUSE_CONST demo_table.c -o t_const.o
size -m t_data.o; nm -m t_data.o      # t_const.o 도 같은 식으로
```

```text
### size -m t_data.o                          ### size -m t_const.o
	Section (__TEXT, __text): 40                 	Section (__TEXT, __text): 40
	Section (__DATA, __data): 4096    ◀──▶       	Section (__TEXT, __const): 4096
	Section (__TEXT, __cstring): 25              	Section (__TEXT, __cstring): 25
	Section (__DATA, __const): 32                	Section (__DATA, __const): 32
### nm -m (발췌)
0000000000000028 (__DATA,__data) external _big_table      ← const 없음
0000000000000028 (__TEXT,__const) external _big_table     ← const 있음
0000000000001048 (__DATA,__const) external _names         ← const char *const names[] (둘 다 같음)
```

링크한 실행 파일에서는 const 판에 **`__DATA` 세그먼트가 아예 없다** (`size -m e_const`: `__TEXT`, `__DATA_CONST`, `__LINKEDIT` 만). `names` 는 링크 후 `(__DATA_CONST,__const)` 로 간다 — 이유는 아래 표.

같은 파일을 **Cortex-M4 ELF** 로 교차 컴파일해 보면 (Apple clang 이 ARM 타깃을 갖고 있어 링커 없이 `.o` 까지 가능):

```bash
cc --target=thumbv7em-none-eabi -mcpu=cortex-m4 -ffreestanding -std=c11 -O2 -c demo_table.c -o m4_data.o
cc --target=thumbv7em-none-eabi -mcpu=cortex-m4 -ffreestanding -std=c11 -O2 -c -DUSE_CONST demo_table.c -o m4_const.o
xcrun llvm-size m4_data.o m4_const.o ; xcrun llvm-size -A m4_data.o ; nm m4_data.o
```

```text
   text	   data	    bss	    dec	    hex	filename
     95	   4096	      0	   4191	   105f	m4_data.o
   4191	      0	      0	   4191	   105f	m4_const.o      ← 4096 이 data → text 로 이동
-- llvm-size -A m4_data.o          -- llvm-size -A m4_const.o
.text                 38          .text                 38
.data               4096          .rodata             4112   (= 4096 + names 16)
.rodata.str1.1        25          .rodata.str1.1        25
.rodata               16
-- nm m4_data.o                    -- nm m4_const.o
00000000 D big_table              00000000 R big_table
00000000 R names                  00001000 R names
```

Berkeley 형식 `size` 의 `text` 열에는 `.rodata` 가 포함된다 — `arm-none-eabi-size firmware.elf` 를 볼 때 **flash 사용량 = text + data, RAM 사용량 = data + bss** 로 읽는 이유다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Cortex-M (예: STM32F4) — const 유무에 따른 4 KB 테이블의 위치</text>
  <text x="40" y="50" fill="currentColor" font-weight="bold">FLASH 0x0800_0000</text>
  <text x="440" y="50" fill="currentColor" font-weight="bold">SRAM 0x2000_0000</text>
  <rect x="40" y="60" width="260" height="34" class="f-mut" stroke="currentColor"/>
  <text x="52" y="82" fill="currentColor">.isr_vector / .text (코드)</text>
  <rect x="40" y="94" width="260" height="44" class="f-ok" stroke="currentColor"/>
  <text x="52" y="113" fill="currentColor">.rodata — const big_table 4 KB</text>
  <text x="52" y="130" fill="currentColor" font-size="12">CPU 가 플래시에서 바로 읽음 (XIP)</text>
  <rect x="40" y="138" width="260" height="44" class="f-warn" stroke="currentColor"/>
  <text x="52" y="157" fill="currentColor">.data 초기값 이미지 (LMA)</text>
  <text x="52" y="174" fill="currentColor" font-size="12">non-const big_table 4 KB</text>
  <rect x="440" y="60" width="260" height="44" class="f-warn" stroke="currentColor"/>
  <text x="452" y="79" fill="currentColor">.data (VMA) — big_table 4 KB 복사본</text>
  <text x="452" y="96" fill="currentColor" font-size="12">런타임에 쓸 수 있음 (= 망가질 수도)</text>
  <rect x="440" y="104" width="260" height="34" class="f-mut" stroke="currentColor"/>
  <text x="452" y="126" fill="currentColor">.bss (0 으로 채움)</text>
  <rect x="440" y="138" width="260" height="44" class="f-mut" stroke="currentColor"/>
  <text x="452" y="164" fill="currentColor">heap ↑   ↓ stack</text>
  <path d="M300 160 C370 160 370 82 436 82" stroke="currentColor" fill="none"/>
  <path d="M428 77 L436 82 L428 87" stroke="currentColor" fill="none"/>
  <text x="318" y="198" fill="currentColor" font-size="12">Reset_Handler 가 main 전에 복사</text>
  <text x="40" y="232" fill="currentColor">const 없음: flash 4 KB + RAM 4 KB + 부팅 때 4 KB 복사 시간. 런타임 버그로 표가 덮어써질 수 있다.</text>
  <text x="40" y="254" fill="currentColor">const 있음: flash 4 KB 만. RAM 0. 쓰기 시도는 컴파일 에러(정적) 또는 플래시라서 안 써짐(동적).</text>
  <text x="40" y="276" fill="currentColor">링커 스크립트: .rodata &gt; FLASH,  .data : { … } &gt; RAM AT&gt; FLASH  (LMA = 플래시, VMA = RAM)</text>
</svg>
```

| 선언 | Mach-O `.o` (arm64) | Mach-O 실행 파일 | ELF Cortex-M `.o` | Cortex-M 실제 위치 |
|---|---|---|---|---|
| `uint32_t big_table[1024] = {…}` | `__DATA,__data` | `__DATA,__data` | `.data` (`D`) | 플래시(초기값) **+ RAM** |
| `const uint32_t big_table[1024] = {…}` | `__TEXT,__const` | `__TEXT,__const` | `.rodata` (`R`) | 플래시만 |
| `const char *const names[]` | `__DATA,__const` | `__DATA_CONST,__const` | `.rodata` (`R`) | 플래시만 |
| `const char *names_rw[]` (슬롯 non-const) | — | — | `.data` (`D`) — `demo_slots.c` 로 확인 | 플래시 + RAM |
| 문자열 리터럴 `"reset"` | `__TEXT,__cstring` | `__TEXT,__cstring` | `.rodata.str1.1` | 플래시만 |

**왜 `names` 는 맥에서 `__TEXT` 가 아니라 `__DATA_CONST` 인가?** 슬롯에 들어가는 건 *주소*다. macOS 실행 파일은 PIE(위치 독립)라 로더(dyld)가 시작 시 주소를 고쳐 써야(rebase) 하므로, "로더가 고친 뒤 읽기 전용으로 잠그는" `__DATA_CONST` 에 둔다. 리눅스 `-fPIC` 도 같은 이유로 `.data.rel.ro` 에 둔다 (`--target=aarch64-linux-gnu -fPIC` 로 빌드하면 `.data.rel.ro 32`, nm 은 `D`). Cortex-M 펌웨어는 보통 절대 주소로 링크하므로(재배치가 링크 때 끝남) 그냥 `.rodata` → 플래시.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 30/30 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_san && ./solution_san   # 30/30, 보고 없음
```

> 테스트용 "하드웨어"는 `union { struct {uint32_t SR, DR, BRR;} hw; UART_TypeDef regs; }` 다. 테스트는 `sim.hw.SR` (non-const 멤버)로 상태를 바꾸고 드라이버는 `regs` 로 읽는다 — union 을 통한 재해석은 C 에서 허용된다 (C11 6.5.2.3 각주 95). const 멤버를 캐스트로 떼고 쓰는 방식은 6.7.3p6 회색 지대라 피했다. 실칩에서는 C 객체가 없고 `(UART_TypeDef *)0x40011000u` 같은 주소일 뿐이라 이 문제가 없다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `const int *p` 를 "상수 포인터"라고 부름 | 잠긴 건 가리키는 값. 포인터는 움직인다 (②) | 상수 포인터는 `int *const p` |
| 큰 테이블 `uint32_t tbl[1024] = {…}` | `.data` → 플래시 + RAM 4 KB, 부팅 복사 | `static const uint32_t tbl[1024]` |
| `const char *names[] = {…}` 로 끝냄 | 문자열만 const. **슬롯 배열**은 `.data` (위 표, `nm`: `D names_rw`) | `const char *const names[]` |
| 함수 안 `const uint8_t tbl[64] = {…}` (static 없음) | 자동 변수라 매 호출마다 스택에 복사. `demo_slots.c` 의 `use_local` 은 `ldm/stm` 으로 64바이트 복사, `use_static` 은 플래시 주소만 넘김 | `static const uint8_t tbl[64]` |
| `void send(uint8_t *buf, size_t n)` | 플래시 상수를 넘길 때 캐스트 강요 → 캐스트가 버그를 숨김 | `void send(const uint8_t *buf, size_t n)` |
| `*(int *)&k_const = 1;` | const 로 정의된 객체 수정 = UB. -O2 는 store 삭제, -O0 은 SIGBUS (위 실측) | 바뀌어야 하면 애초에 const 로 정의하지 않는다 |
| 상태 레지스터를 `const uint32_t SR` (volatile 빠짐) | 컴파일러가 폴링 루프에서 읽기를 한 번으로 줄일 수 있다 (C-B09 참고) | `const volatile uint32_t SR` |
| `const int N = 16; int arr[N];` (파일 스코프) | C 의 const 변수는 **상수식(constant expression)이 아니다** — 파일 스코프 배열 크기·`case` 라벨 불가 | `enum { N = 16 };` 또는 `#define N 16` |

## 8. 면접 Q&A

<details>
<summary>Q1. const int *p, int const *p, int *const p 의 차이는?</summary>

앞의 둘은 같은 타입: "const int 를 가리키는 포인터" — `*p = x` 불가, `p = q` 가능. `int *const p` 는 "int 를 가리키는 const 포인터" — `p = q` 불가, `*p = x` 가능. 판별법: `*` 왼쪽의 const 는 대상, 오른쪽의 const 는 포인터 자신. 꼬리질문 "`const int *const *const p` 는?" → 이름부터 오른쪽→왼쪽: p 는 const 포인터 → (const 포인터) → const int. 세 단계 모두 수정 불가.

</details>

<details>
<summary>Q2. char ** 를 const char ** 매개변수에 넘기면 왜 경고가 나나요? const 를 더하는 건데요.</summary>

허용하면 `*pp = &const_char;` 로 `char *` 가 const 객체를 가리키게 되고 `*p = 'x'` 가 캐스트 없이 const 를 깬다 (위 그림). 그래서 C11 6.5.16.1 은 **첫 단계** qualifier 추가만 허용한다. C 는 `const char *const *` 로도 암묵 변환을 안 해 주지만 (clang: "discards qualifiers in nested pointer types"), C++ 은 그건 안전하다고 보고 허용한다. 실무 답: 읽기 전용 표는 `const char *const *` 로 받고 `char **argv` 쪽에서 명시 캐스트, 또는 POSIX `execv` 처럼 `char *const []` 로 받는다.

</details>

<details>
<summary>Q3. const volatile 은 모순 아닌가요? 어디에 쓰나요?</summary>

모순 아니다. const = "**이 프로그램이** 쓰지 않는다", volatile = "**누군가(하드웨어/ISR)가** 바꿀 수 있으니 매번 실제로 읽어라". 읽기 전용 상태 레지스터(UART SR, ADC DR, 타이머 CNT 읽기용 뷰), 다른 코어/DMA 가 쓰는 공유 메모리의 읽기 쪽, 하드웨어 ID 레지스터에 쓴다. CMSIS `__I` 가 정확히 `volatile const`. 꼬리질문 "const 객체인데 값이 바뀌어도 UB 아닌가?" → UB 는 *프로그램이* non-const lvalue 로 수정할 때다. 하드웨어가 바꾸는 건 volatile 이 다루는 "구현 밖의 수정"이다.

</details>

<details>
<summary>Q4. const 를 붙이면 컴파일러가 더 최적화하나요?</summary>

경우가 나뉜다. **const 로 정의된 객체**(`static const int k = 5;`)는 상수 접기·ROM 배치가 가능하다 (실측: `k_limit` 은 심볼조차 안 남고 100 으로 접혔다). 하지만 **`const T *` 매개변수는 최적화에 거의 도움이 안 된다**: ① 다른 non-const 포인터가 같은 곳을 가리킬 수 있고 (추가 연습 3: `read_write_read(&v, &v) == 51`), ② 원래 객체가 non-const 면 callee 가 캐스트로 떼고 써도 합법이다. 그래서 "값이 안 바뀐다"는 보장은 `restrict` 나 객체 정의의 const 에서 온다. pointer-to-const 의 가치는 **API 계약과 실수 방지**다.

</details>

<details>
<summary>Q5. 4 KB 룩업 테이블에 const 를 빼먹으면 Cortex-M 에서 무슨 일이 생기나요? 어떻게 확인하나요?</summary>

`.data` 로 가서 ① 플래시에 초기값 4 KB, ② RAM 에 복사본 4 KB, ③ Reset_Handler 가 main 전에 복사하는 시간이 든다. 게다가 RAM 에 있으니 버퍼 오버런이 표를 덮어쓸 수 있다. 확인: `arm-none-eabi-size fw.elf` 의 `data` 열이 4096 늘어나고, `nm` 에서 `D big_table` (const 면 `R`), `.map` 파일에서 `.data` 섹션 소속. 이 레슨에서 thumbv7em `.o` 로 실측: `text 95 / data 4096` → `text 4191 / data 0`.

</details>

<details>
<summary>Q6. C 에서 const int N = 10; 을 #define 대신 써도 되나요?</summary>

C 에서는 const 변수가 **상수식이 아니다** (C++ 과 다름). 파일 스코프 배열 크기, `case N:`, 비트필드 폭, `_Static_assert` 에 못 쓴다 (블록 스코프 `int a[N]` 은 VLA 가 되어 버린다). 대신 타입이 있고 디버거에 심볼이 보이고 스코프 규칙을 따른다는 장점이 있다. 정수 상수는 `enum { N = 10 };` 이 두 장점을 모두 갖는 관용구. (C23 은 `constexpr` 객체를 도입했다.)

</details>

<details>
<summary>Q7. strchr 는 왜 const char * 를 받아서 char * 를 돌려주나요?</summary>

C 에 오버로딩이 없어서 `char *` 입력과 `const char *` 입력 모두 받으려면 const 로 받고, 결과를 쓸 수 있게 하려면 non-const 로 돌려줄 수밖에 없었다. 결과적으로 const 를 조용히 벗기는 구멍이다. C++ 은 두 오버로드로 해결했고, C23 은 `strchr`/`memchr`/`strstr`/`strpbrk`/`strrchr` 를 입력의 qualifier 를 보존하는 제네릭 함수로 바꿨다. 내 API 는 `const char *find(const char *)` + 필요하면 `char *` 래퍼 (추가 연습 2).

</details>

## 9. 추가 연습

### 연습 1 — 선언 읽기

다음을 선언하라: (a) "char 를 가리키는 const 포인터를 가리키는 포인터" (b) "const char 를 가리키는 포인터를 가리키는 const 포인터" (c) `char const *` 는 무엇과 같은가.

<details>
<summary>답 보기</summary>

(a) `char *const *x1` (b) `const char **const x2` (c) `const char *` — const 는 `*` 기준 왼쪽에 있으니 대상에 붙는다 (east const 표기). 테스트는 `IS_TYPE(&x1, char *const **)`, `IS_TYPE(&x2, const char **const *)`.

</details>

### 연습 2 — const 를 지키는 검색 + 합법적인 const 떼기

`const char *find_char(const char *s, char c)` 와, non-const 문자열용 `char *find_char_mut(char *s, char c)` 를 만들어라. 후자로 `"key=value"` 를 `'='` 에서 잘라라.

<details>
<summary>답 보기</summary>

`find_char_mut` 은 `return (char *)find_char(s, c);` — const 를 떼는 캐스트지만 **원래 객체(`char line[]`)가 const 가 아님**을 시그니처(`char *s`)가 보장하므로 합법이다. 한 구현을 공유해 중복을 없애는 C 의 관용구 (C++ 의 `const_cast` 패턴과 같은 논리).

</details>

### 연습 3 — const ≠ 불변

`int read_write_read(const int *p, int *q)` 가 `*p` 를 읽고, `*q = 50` 을 쓴 뒤, 다시 `*p` 를 읽어 합을 돌려준다. `(&v, &v)` 로 부르면?

<details>
<summary>답 보기</summary>

`1 + 50 = 51`. `const int *p` 는 "p 를 통해서는 안 쓴다"는 뜻일 뿐, 같은 객체를 다른 경로(q)로 바꾸는 것을 막지 않는다. 컴파일러도 이 때문에 `*p` 를 다시 읽어야 한다. "안 바뀐다"를 약속하려면 `restrict` (C-M 레벨에서 다룸).

</details>

### 연습 4 — 플래시에 두는 보정 테이블

NTC 서미스터 ADC 값 → 0.1°C 단위 온도 보정표 `static const cal_pt_t k_cal[]` 와 선형 보간 `int16_t cal_lookup(const cal_pt_t *tbl, size_t n, uint16_t raw)` 를 만들어라 (범위 밖은 끝값으로 clamp).

<details>
<summary>답 보기</summary>

구간 `[r0, r1]` 을 찾아 `t0 + (t1 - t0) * (raw - r0) / (r1 - r0)` 를 `int32_t` 로 계산 (곱이 int16 을 넘을 수 있으므로). 표가 `const` 라 플래시에 남고, 함수가 `const cal_pt_t *` 를 받아서 플래시 표·RAM 표(현장 보정값) 모두 캐스트 없이 받을 수 있다. 예: raw 300 → -100 (= -10.0°C), raw 700 → 125.

</details>

### 연습 5 — const 드라이버 ops 테이블

`typedef struct { int (*init)(void); size_t (*write)(const uint8_t *, size_t); } uart_ops_t;` 의 인스턴스를 플래시에 두고, `console_puts(const uart_ops_t *ops, const char *s)` 로 써라.

<details>
<summary>답 보기</summary>

`static const uart_ops_t k_fake_uart_ops = { .init = fake_init, .write = fake_write };` — 함수 포인터 표가 RAM 에 있으면 메모리 손상 한 번으로 **임의 코드 실행 경로**가 된다. 리눅스 커널이 `const struct file_operations` 를 강제하는 이유와 같다 (보안 + RAM 절약). 문자열을 바이트 버퍼로 넘길 땐 `(const uint8_t *)s` — qualifier 는 유지하고 타입만 바꾸는 캐스트.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 19/19 passed
```

**관찰용 데모 파일** (하네스 아님, 위 5장의 출력을 재현):

```bash
# ① 컴파일 실패해야 하는 줄 8개
for e in E1 E2 E3 E4 E5 E6 E7 E8; do echo "== $e"; cc -std=c11 -Wall -Wextra -Werror -fsyntax-only -D$e demo_const_errors.c 2>&1 | grep error; done
# ② const 떼고 쓰기 (UB 데모: -O2 는 store 삭제, -O0 은 SIGBUS)
cc -std=c11 -O0 demo_cast_away.c -o demo_cast_away && ./demo_cast_away global
# ③ 4 KB 테이블 섹션 이동 (맥 + Cortex-M)
cc -std=c11 -O2 -c -DUSE_CONST demo_table.c -o t_const.o && size -m t_const.o && nm -m t_const.o
cc --target=thumbv7em-none-eabi -mcpu=cortex-m4 -ffreestanding -std=c11 -O2 -c demo_table.c -o m4_data.o && xcrun llvm-size m4_data.o
# ④ 슬롯 const / 지역 const 배열 복사
cc --target=thumbv7em-none-eabi -mcpu=cortex-m4 -ffreestanding -std=c11 -O2 -c demo_slots.c && nm demo_slots.o
```

> `/usr/local/bin/llvm-size` 가 있으면 아주 오래된 버전(LLVM 3.2)일 수 있다 — `xcrun llvm-size` 로 CLT 버전을 쓴다.

## 10. 핵심 정리

- **`*` 왼쪽 const = 대상 고정, `*` 오른쪽 const = 포인터 고정.** 이름에서 시작해 오른쪽→왼쪽으로 읽는다. `int const *` = `const int *`.
- 암묵 변환은 **첫 단계 qualifier 추가만**: `T*` → `const T*` OK. `char**` → `const char**` 금지 (구멍), C 에선 `const char *const *` 도 경고 → 캐스트.
- const 로 **정의된** 객체를 캐스트로 고치면 UB (실측: -O2 store 삭제, -O0 SIGBUS). non-const 객체에서 떼는 건 합법.
- API: 읽기만 하는 버퍼는 `const uint8_t *buf`, 상태 레지스터 `const volatile`, 고정 베이스 포인터 `T *const`, ops 표 `static const`.
- `const T *` 매개변수는 계약이지 불변 보장이 아니다 (aliasing). 최적화보다 **버그를 컴파일 에러로 바꾸는 도구**.
- 큰 표 = `static const`: Mach-O `__TEXT,__const` / ELF `.rodata` → Cortex-M 플래시만. 빠지면 `.data` = 플래시 + RAM + 부팅 복사.
- 포인터 표는 `const char *const names[]` 까지 써야 슬롯도 플래시. 함수 안 const 배열은 `static` 을 붙여야 매 호출 스택 복사가 없다.
- `arm-none-eabi-size`: flash = text + data, RAM = data + bss.

## 참고자료

- C11 표준 (N1570) — 6.7.3 Type qualifiers (p6: const 객체 수정 UB), 6.5.16.1 Simple assignment (포인터 qualifier 제약), 6.3.2.3p2, 6.7.6.3p15 (매개변수 qualifier 와 함수 타입)
- [C FAQ 11.10 — Why can't I pass a char ** to a function which expects a const char **?](https://c-faq.com/ansi/constmismatch.html)
- [POSIX exec — Rationale (char *const argv[] 를 고른 이유와 호환성 표)](https://pubs.opengroup.org/onlinepubs/9799919799/functions/exec.html)
- [ARM CMSIS-Core — `__I` / `__O` / `__IO` 레지스터 qualifier](https://arm-software.github.io/CMSIS_6/latest/Core/index.html)
- [N3096 (C23 draft) 7.26.5 — qualifier 를 보존하는 strchr/memchr 등](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3096.pdf)
- Apple `size(1)`, `nm(1)` man page (`-m` 옵션: Mach-O 세그먼트·섹션 표시), GNU ld 매뉴얼 — `AT>` 로 LMA/VMA 분리
- 관련 레슨: C-B09 (volatile), C-B11 (변수는 어디에 사는가: 섹션 전반)
