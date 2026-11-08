---
id: CPP-B10
time: 50분
updated: 2026-11-07
---
# CPP-B10 std::array로 C 배열 대체하기

## 학습 목표

- `std::array<T, N>` 이 **C 배열을 감싼 구조체(aggregate)** 일 뿐이라 크기·배치·-O2 어셈블리가 C 배열과 같다는 것을 직접 확인한다.
- CRC-8 같은 **룩업 테이블(lookup table)** 을 `constexpr` 함수로 만들어 플래시(.rodata)에 넣는다 — C에서는 스크립트로 리터럴을 생성하거나 부팅 때 RAM에 계산해야 했던 일.
- **값 의미론(value semantics)**: 복사·대입·`==` 비교·값 반환이 되고, 함수에 넘겨도 포인터로 붕괴(decay)하지 않아 크기 `N` 이 타입에 남는다.
- `-fno-exceptions` 빌드에서 `at()` 이 **조용히 `abort()`** 한다는 것을 libc++ 소스와 실행으로 확인하고, 대신 `std::get<I>` · `requires` · `static_assert` 로 **컴파일 타임 범위 검사**를 한다.
- DMA 버퍼를 `alignas(32) std::array` 로 선언하고, 주소·개수를 한 인자에서 뽑아 **길이 불일치 버그**를 없앤다.

## 1. 문제

펌웨어에는 DMA 버퍼, CRC/사인/감마 테이블, 프레임 버퍼처럼 **고정 크기 배열이 가장 흔한 자료구조**다. "C 배열 대신 뭘 쓰나요? 비용은요? `at()` 은요?" 는 C++ 펌웨어 면접의 단골 첫 질문이고, 답은 *"std::array — 비용 0, 단 at()은 안 씀"* 이다.

C 배열의 문제는 딱 하나로 요약된다: **함수에 넘기는 순간 크기를 잃는다** (C-B06 참고). 그래서 `(ptr, len)` 쌍을 손으로 맞추다 버그가 난다. `std::array` 는 같은 바이트를 쓰면서 `N` 을 타입에 붙여 둔다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 8바이트, 다른 타입 정보</text>
  <text x="10" y="52" fill="currentColor" font-family="monospace">uint8_t buf[8];</text>
  <text x="390" y="52" fill="currentColor" font-family="monospace">std::array&lt;uint8_t, 8&gt; buf;</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="64" width="320" height="30"/><rect x="390" y="64" width="320" height="30"/>
  </g>
  <rect x="390" y="64" width="320" height="30" class="f-ok" opacity=".35"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle" font-size="12">
    <text x="30" y="84">b0</text><text x="70" y="84">b1</text><text x="110" y="84">b2</text><text x="150" y="84">b3</text>
    <text x="190" y="84">b4</text><text x="230" y="84">b5</text><text x="270" y="84">b6</text><text x="310" y="84">b7</text>
    <text x="410" y="84">b0</text><text x="450" y="84">b1</text><text x="490" y="84">b2</text><text x="530" y="84">b3</text>
    <text x="570" y="84">b4</text><text x="610" y="84">b5</text><text x="650" y="84">b6</text><text x="690" y="84">b7</text>
  </g>
  <text x="390" y="114" fill="currentColor" font-size="12" font-family="monospace">= struct { uint8_t __elems_[8]; }  sizeof 8</text>
  <text x="10" y="114" fill="currentColor" font-size="12">sizeof 8</text>
  <g stroke="currentColor" fill="none"><path d="M170 124 L170 150"/><path d="M550 124 L550 150"/></g>
  <text x="10" y="170" fill="currentColor" font-family="monospace">void f(const uint8_t b[8])</text>
  <text x="10" y="190" fill="currentColor" font-family="monospace">  == f(const uint8_t *b)</text>
  <rect x="10" y="202" width="320" height="82" rx="8" class="f-warn" opacity=".35"/>
  <g fill="currentColor"><text x="20" y="222">• 크기 8 은 사라짐 (sizeof(b) == 8 = 포인터)</text>
    <text x="20" y="244">• b2 = b 대입 불가, b == c 는 주소 비교</text>
    <text x="20" y="266">• return 불가 → out 파라미터 + len</text></g>
  <text x="390" y="170" fill="currentColor" font-family="monospace">template &lt;size_t N&gt;</text>
  <text x="390" y="190" fill="currentColor" font-family="monospace">void f(const std::array&lt;uint8_t,N&gt;&amp; b)</text>
  <rect x="390" y="202" width="340" height="82" rx="8" class="f-ok" opacity=".35"/>
  <g fill="currentColor"><text x="400" y="222">• N 은 타입에 남음: b.size() 는 constexpr</text>
    <text x="400" y="244">• 복사·대입·== (원소 비교)·값 return 가능</text>
    <text x="400" y="266">• 생성 코드는 C 배열과 동일 (아래 5. 풀이)</text></g>
</svg>
```

### Part A — constexpr CRC-8 룩업 테이블

- `make_crc8_table(poly)` 를 `constexpr` 함수로 구현해 `std::array<uint8_t, 256>` 을 **값으로 반환**한다 (CRC-8/SMBUS: poly `0x07`, init `0x00`, 반사 없음).
- `inline constexpr auto kCrc8Table = make_crc8_table(0x07);` — 컴파일 타임에 계산되어야 한다 (`constexpr bool` 로 검사).
- `crc8(const std::array<uint8_t, N>&)` — 길이 인자 없이 `N` 을 타입에서 얻는 템플릿. `crc8("123456789") == 0xF4`.

### Part B — 값 의미론 · to_array · 컴파일 타임 인덱스

- `seal_frame(Frame f)` — 8바이트 프레임을 **값으로** 받아 마지막 바이트에 앞 7바이트의 CRC를 넣어 **값으로** 반환. 호출자 원본은 그대로여야 한다. 복사·대입·`==` / `!=` 로 검증.
- `kPrescalers = std::to_array<uint16_t>({1, 2, 4, …, 1024})` — 개수를 손으로 세지 않는 표. `prescaler_index(div)` 를 `constexpr` 로.
- `adc_channel<Ch>(frame)` — 채널 번호가 컴파일 타임 상수일 때 **범위 밖이면 호출 자체가 불가능**해야 한다 (`kChannelOk<4> == false`). `at()` 금지.

### Part C — DMA 버퍼 (mock STM32 DMA 채널)

- `dma_start_circular(ch, buf)` — `CMAR` = 버퍼 주소, `CNDTR` = 원소 개수, `CCR` = 원소 크기에 맞는 MSIZE/PSIZE + MINC + CIRC + HTIE + TCIE + EN. 하드웨어 제약(16비트 CNDTR, 짝수 개, 원소 1/2/4바이트)은 `static_assert` 로.
- DMA 버퍼 `g_adc_dma`, `g_uart_rx` 를 `alignas(32)` 로 선언하고 `cache_line_safe<32>(buf)` 로 검사 (Cortex-M7 D-cache 라인 = 32B).
- `sum_half<Half>(buf)` — half-transfer(HT) / transfer-complete(TC) 인터럽트에서 처리할 반쪽 합. `Half` 가 0/1이 아니면 컴파일 에러.

## 2. Problem (English)

> **Replace C arrays with `std::array` in a firmware module (C++20, `-fno-exceptions -fno-rtti`, no heap).**
>
> 1. Write `constexpr std::array<uint8_t,256> make_crc8_table(uint8_t poly)` (MSB-first, init 0) and a template `crc8(const std::array<uint8_t,N>&)` that takes no length argument. The table must be computed at compile time. Example: `crc8({'1',…,'9'}) == 0xF4`.
> 2. Write `seal_frame(std::array<uint8_t,N> f)` that returns a copy of `f` whose last byte is the CRC of the first `N-1` bytes; the caller's frame must be unchanged. Build a prescaler table with `std::to_array` and a `constexpr` reverse lookup. Write `adc_channel<Ch>(const std::array<uint16_t,N>&)` such that an out-of-range `Ch` is rejected at compile time (and is detectable with a `requires`-expression).
> 3. Given a DMA channel `{CMAR, CNDTR, CCR}`, write `dma_start_circular(ch, std::array<T,N>& buf)` deriving address, count and element size from `buf` alone, with `static_assert`s for `2 <= N <= 65535`, even `N`, `sizeof(T) ∈ {1,2,4}`. Declare 32-byte-aligned DMA buffers. Implement `sum_half<Half>()` used by the HT/TC ISR.
>
> Follow-ups: Why not `at()` in this build? What does the -O2 code look like compared to a C array? What does passing a `std::array` by value cost?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 FAIL 15줄, "5/20 passed" — TODO를 채워 20/20 을 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — constexpr 함수 안에서도 그냥 루프를 돈다</summary>

C++14부터 `constexpr` 함수 안에 지역 변수·`for` 루프가 허용되고, C++17부터 `std::array` 의 non-const `operator[]` 가 `constexpr` 이다. 그러니 `std::array<uint8_t,256> t{};` 를 만들고 평범한 비트 단위 CRC 루프로 `t[i]` 를 채운 뒤 `return t;` 하면 된다. `{}` 로 0 초기화하는 것을 잊지 말자.

</details>

<details>
<summary>힌트 2 — CRC 테이블 한 칸의 계산</summary>

`c = i;` 를 8번: 최상위 비트가 1이면 `c = (c << 1) ^ poly`, 아니면 `c <<= 1`. `uint8_t` 로 잘라 주는 `static_cast` 가 필요하다 (`c << 1` 은 `int` 로 승격, C-B02 참고). 테이블 사용은 `crc = table[crc ^ byte]`.

</details>

<details>
<summary>힌트 3 — 컴파일 타임 인덱스는 템플릿 인자로</summary>

`std::get<Ch>(arr)` 는 `Ch >= N` 이면 컴파일 에러다 (라이브러리 내부 `static_assert`). 하지만 그건 "본문 안의 hard error" 라서 `requires` 로 **탐지**할 수 없다. 호출 자체를 후보에서 빼려면 템플릿 머리에 `requires (Ch < N)` 를 붙인다.

</details>

<details>
<summary>힌트 4 — CNDTR 에 넣을 값은 sizeof 가 아니다</summary>

`std::array<uint16_t, 64>` 의 `sizeof` 는 128(바이트), `size()` 는 64(원소). STM32 DMA 의 CNDTR 은 **전송 횟수(원소 수)** 다. 반대로 캐시 정렬 검사는 **바이트** 기준이라 `sizeof(buf)` 를 쓴다.

</details>

<details>
<summary>힌트 5 — alignas 는 변수 선언 앞에</summary>

`alignas(32) static std::array<uint16_t, 64> g_adc_dma{};` — `alignas` 는 변수·멤버·클래스에만 붙는다. `using DmaBuf alignas(32) = …` 는 에러다.

</details>

## 5. 풀이

### Part A — 테이블을 "값" 으로 만들고 플래시에 둔다

**왜 C 배열로는 안 되나**: C/C++ 모두 함수는 배열 타입을 반환할 수 없다 ([dcl.fct]/11). `std::array` 는 `T __elems_[N]` 멤버 하나뿐인 **aggregate 구조체** ([array.overview]) 라서 구조체처럼 값으로 반환된다. 그리고 `constexpr` 함수 안에서 루프로 채울 수 있으니 ([dcl.constexpr], C++14 relaxed constexpr), `inline constexpr auto kCrc8Table = make_crc8_table(0x07);` 는 **상수 초기화(constant initialization)** 된다 — 런타임 코드 0, 결과는 읽기 전용 섹션에 256바이트 상수로 들어간다 (정적 초기화 순서 문제는 CPP-B09 참고).

```cpp
constexpr std::array<std::uint8_t, 256> make_crc8_table(std::uint8_t poly) {
    std::array<std::uint8_t, 256> t{};
    for (std::size_t i = 0; i < t.size(); ++i) { /* 8번 시프트 + 조건부 XOR */ }
    return t;
}
```

"정말 컴파일 타임에 계산됐나?" 는 하네스의 `constexpr bool kTableBuiltAtCompileTime = kCrc8Table[1] == 0x07;` 가 보장한다 — `kCrc8Table` 이 상수 식이 아니면 **이 줄이 컴파일되지 않는다**. `crc8(kCheck)` 도 `constexpr` 변수로 받아서 CRC 계산까지 컴파일 타임에 끝난다.

| 방식 | 테이블 위치 | 런타임 코드 | 단점 |
|---|---|---|---|
| C ① 스크립트로 256개 리터럴 생성 | .rodata (플래시) | 없음 | 생성기 유지, poly 바꾸면 재생성 |
| C ② `crc8_init()` 부팅 때 계산 | .bss (RAM 256B) | init 함수 | RAM 낭비, init 호출 순서 의존 |
| C++ `constexpr std::array` | .rodata (플래시) | 없음 | — (코드 자체가 생성기 + `static_assert` 검증) |

**-O2 어셈블리 비교 (실측)**. `demo_asm_carray.c` (C 배열 + ② 방식) 와 `demo_asm_stdarray.cpp` (std::array + constexpr) 에 같은 함수 3개를 `extern "C"` 이름으로 만들고 함수별로 diff 했다 (Apple clang 21, arm64).

```bash
cc  -std=c11   -O2 -S -o c.s   demo_asm_carray.c
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o cpp.s demo_asm_stdarray.cpp
```

```text
diff sum64:   (.cfi_startproc / .cfi_endproc 두 줄만 다름) — 명령어 동일 (ldp q0..q7 + uaddl 벡터 합)
diff get3:    (.cfi 두 줄만 다름) — 둘 다  ldrh w0, [x0, #6] ; ret
diff crc8_frame:
<   adrp x9, _crc8_table@PAGE               ; C: __DATA,__bss 의 RAM 테이블
>   adrp x9, __ZL10crc8_table@PAGE          ; C++: __TEXT,__const 의 상수 테이블
    (나머지 8× ldrb/eor/and/ldrb 언롤 루프는 완전히 동일)
```

`.cfi_*` 차이는 C 빌드가 unwind 정보를 내고 `-fno-exceptions` C++ 빌드는 내지 않아서 생긴다 (CPP-B13 참고) — 배열 표현과는 무관하다. 오브젝트 크기 (`size -m`):

```text
C   (demo_asm_carray.o)        C++ (demo_asm_stdarray.o)
__TEXT,__text      1204        __TEXT,__text    228
__TEXT,__literal16  208        __TEXT,__const   256   ← 테이블 (플래시)
__DATA,__bss        256 (RAM)
__LD,__compact_unwind 128
```

세 함수 본문은 양쪽 다 228바이트로 같고, C 쪽의 나머지 976바이트 + 208바이트 상수는 `crc8_init()` (벡터화된 테이블 계산 루프) 다. ELF 로 보면 C++ 쪽 테이블은 `.rodata`, C ② 의 테이블은 `.bss` 다. arm-none-eabi-g++ (Cortex-M, Thumb-2) 에서도 같은 결과가 **예상**된다 — `std::array` 의 `operator[]` 는 인라인된 `__elems_[i]` 일 뿐이라 `ldrb r0, [r1, r2]` 형태로 C 와 같게 나온다 (여기서는 크로스 컴파일러가 없어 실행하지 못함).

### Part B — 값 의미론과 "인덱스가 언제 정해지나"

| 동작 | C 배열 `uint8_t a[8]` | `std::array<uint8_t,8> a` |
|---|---|---|
| `b = a;` / `T b = a;` | 에러 (`array initializer must be an initializer list`) | 원소별 복사 |
| `a == b` | 주소 비교 (C++20 deprecated, C++26 ill-formed) | 원소별 비교, `<` 는 사전식 |
| 함수 인자로 전달 | `uint8_t*` 로 decay, 크기 소실 | 값/참조 그대로, `N` 유지 |
| 함수에서 반환 | 불가 | 가능 (값 반환) |
| 크기 | `sizeof a / sizeof a[0]` 매크로 | `a.size()` (constexpr) |

`seal_frame(Frame f)` 는 **값으로 받아 수정하고 값으로 반환**하므로 호출자의 `raw` 는 그대로다. 비용이 걱정되면 실측하자 — `demo_byvalue.cpp`:

```bash
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_byvalue.cpp | c++filt
```

```text
bump_last(std::__1::array<unsigned char, 8ul>):      ; 8B 프레임 값 전달·반환
	mov	x8, #72057594037927936   ; =0x0100000000000000 (f[7] 자리의 +1)
	add	x0, x0, x8               ; 프레임 전체가 레지스터 x0 하나에 들어옴
	ret
call_val():                      ; 256B 배열을 값으로 넘기면 (본문을 못 보는 외부 함수)
	sub	sp, sp, #288
	ldp	q0, q1, [x8, #192] / stp q0, q1, [sp, #192]   ... ×8   ; 256B 스택 복사
	mov	x0, sp
	bl	ext_val(...)
call_ref():                      ; const& 로 넘기면
	adrp x0, _g_big@PAGE ; add x0, ... ; b ext_ref(...)   ; 주소만, 꼬리 호출
```

AArch64 AAPCS64 는 16바이트 이하 composite 를 레지스터로, 그보다 크면 **호출자가 복사본을 만들어 주소를 넘긴다**. 그러니 작은 프레임은 값으로, 큰 버퍼는 `const&` 로. ARM32 AAPCS 는 composite 를 r0–r3 + 스택으로 넘기므로 Cortex-M 에서도 "작으면 값, 크면 참조" 규칙은 같다 (예상).

**범위 검사 — 인덱스가 언제 정해지느냐가 전부다.**

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">범위 밖 인덱스 4 (N = 4) 를 읽으면? — 실측, Apple clang 21 / libc++</text>
  <rect x="270" y="38" width="200" height="34" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="370" y="60" fill="currentColor" text-anchor="middle">인덱스가 컴파일 타임 상수?</text>
  <g stroke="currentColor" fill="none"><path d="M300 72 L150 104"/><path d="M440 72 L560 104"/></g>
  <text x="190" y="92" fill="currentColor">예</text><text x="510" y="92" fill="currentColor">아니오 (런타임 값)</text>
  <rect x="10" y="106" width="290" height="96" rx="8" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="20" y="126" font-family="monospace">std::get&lt;4&gt;(a)  → static_assert 실패</text>
    <text x="20" y="146" font-family="monospace">constexpr x = a[4] → 상수식 에러</text>
    <text x="20" y="166" font-family="monospace">requires (Ch &lt; N) → 후보에서 제외</text>
    <text x="20" y="190" font-weight="bold">= 컴파일 에러. 런타임 비용 0</text>
  </g>
  <g font-size="12" fill="currentColor">
    <rect x="330" y="106" width="400" height="40" rx="6" class="f-warn" stroke="currentColor"/>
    <text x="340" y="123" font-family="monospace">a.at(i)  예외 켬</text><text x="470" y="123">throw → terminate, 메시지 출력, exit 134</text>
    <text x="340" y="140" font-family="monospace">a.at(i)  -fno-exceptions</text><text x="530" y="140">메시지 없이 abort(), exit 134</text>
    <rect x="330" y="152" width="400" height="40" rx="6" class="f-warn" stroke="currentColor"/>
    <text x="340" y="169" font-family="monospace">a[i]  기본</text><text x="470" y="169">검사 없음 → 쓰레기 128 읽고 exit 0 (UB)</text>
    <text x="340" y="186" font-family="monospace">a[i]  hardening FAST</text><text x="530" y="186">brk #1 트랩, exit 133</text>
    <rect x="330" y="198" width="400" height="40" rx="6" class="f-ok" stroke="currentColor"/>
    <text x="340" y="215" font-family="monospace">if (i &lt; a.size()) … else return Err;</text>
    <text x="340" y="232">펌웨어 권장: 명시적 검사 + 에러 코드 (복구 경로가 있음)</text>
  </g>
  <text x="10" y="270" fill="currentColor">펌웨어에서 abort() 는 보통 HardFault 나 무한 루프 — 원인 메시지도, 복구도 없다.</text>
  <text x="10" y="292" fill="currentColor">그래서 원칙: 상수 인덱스는 컴파일 타임에 막고, 런타임 인덱스는 직접 검사해 에러를 반환한다.</text>
  <text x="10" y="314" fill="currentColor">-fno-exceptions 에서 at() 은 "검사는 하지만 처리할 방법이 없는" 함수가 된다.</text>
</svg>
```

**`at()` 이 `-fno-exceptions` 에서 무엇을 하는가 — 소스로 확인.** 표준은 `at()` 이 `out_of_range` 를 throw 한다고만 정한다 ([sequence.reqmts]). `-fno-exceptions` 는 표준 밖 방언이라 동작은 라이브러리 마음이다. 이 맥의 libc++ (`MacOSX26.4.sdk/usr/include/c++/v1`) 는:

```cpp
// <array>
reference at(size_type __n) {
    if (__n >= _Size)
      std::__throw_out_of_range("array::at");
    return __elems_[__n];
}
// <stdexcept>
[[__noreturn__]] inline void __throw_out_of_range(const char* __msg) {
#  if _LIBCPP_HAS_EXCEPTIONS
  throw out_of_range(__msg);
#  else
  //_LIBCPP_VERBOSE_ABORT("out_of_range was thrown in -fno-exceptions mode with message \"%s\"", __msg);
  // Workaround to prevent having to link against `libc++.dylib`. ...
  (void)__msg;
  ::abort();
#  endif
}
```

즉 이 버전은 verbose abort 줄이 **주석 처리**되어 있어 메시지 없이 `::abort()` 만 부른다 (다른 libc++ 버전은 `_LIBCPP_VERBOSE_ABORT` 로 메시지를 찍기도 한다). `demo_at.cpp` 실측:

```text
$ c++ -std=c++20 -O2 demo_at.cpp -o demo_at && ./demo_at 4          # ① 예외 켬
reading lut[4]
libc++abi: terminating due to uncaught exception of type std::out_of_range: array::at
Abort trap: 6        exit=134
$ c++ ... -fno-exceptions -fno-rtti demo_at.cpp && ./demo_at 4      # ② 예외 끔
reading lut[4]
Abort trap: 6        exit=134          ← 메시지 없음
$ c++ ... -fno-exceptions -fno-rtti -DUSE_BRACKET \
      -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST ... && ./demo_at 4   # ③
reading lut[4]
Trace/BPT trap: 5    exit=133          ← operator[] 의 hardening assert = brk #0x1
$ c++ ... -fno-exceptions -fno-rtti -DUSE_BRACKET ... && ./demo_at 4          # ④ 기본 []
reading lut[4]
value = 128          exit=0            ← 조용한 UB: 스택 쓰레기를 읽음
```

`-S` 로 보면 ② 는 `cmp x19, #3 ; b.hi … ; bl _abort`, ③ 은 같은 비교 뒤 `brk #0x1` 이다. 예외를 켜면 `nm` 에 `___cxa_allocate_exception`, `___cxa_throw`, `typeinfo for std::out_of_range`, `__Unwind_Resume` 이 undefined 로 잡히고 `__gcc_except_tab` 섹션이 생긴다 (CPP-B13 참고).

**libstdc++ / arm-none-eabi-g++ (예상, 여기선 실행 못 함)**: libstdc++ 의 `at()` 은 라이브러리 안에 정의된 `std::__throw_out_of_range_fmt` 를 호출한다. 그 함수는 예외를 켜고 빌드된 libstdc++ 에 들어 있어서, 사용자 코드가 `-fno-exceptions` 여도 링크 시 throw 경로(`__cxa_throw`, 언와인더, 기본 terminate 핸들러 등)가 딸려 들어와 플래시가 수십 KB 늘 수 있다 — 그래서 많은 펌웨어 코딩 규칙이 `at()` 자체를 금지한다. libstdc++ 에서 `operator[]` 검사를 켜는 스위치는 `-D_GLIBCXX_ASSERTIONS`.

**컴파일 타임 검사 3종의 차이** (`demo_bounds.cpp -DSHOW_ERROR` 실측, 경로 줄임):

```text
.../c++/v1/array:513: error: static assertion failed due to requirement '4UL < 4UL':
      Index out of bounds in std::get<> (const std::array)
demo_bounds.cpp:26: error: constexpr variable 'e2' must be initialized by a constant expression
demo_bounds.cpp:26: note: read of dereferenced one-past-the-end pointer is not allowed in a constant expression
demo_bounds.cpp:27: error: no matching function for call to 'pick'
demo_bounds.cpp:13: note: candidate template ignored: constraints not satisfied [with Ch = 4, N = 4]
demo_bounds.cpp:12: note: because '4UL < 4UL' (4 < 4) evaluated to false
demo_bounds.cpp:31: error: array initializer must be an initializer list or string literal   ← C 배열 복사
demo_bounds.cpp:17: error: static assertion failed due to requirement '3UL % 2 == 0':
      half-transfer double buffering needs an even element count
5 errors generated.
```

| 방법 | 언제 에러 | `requires` 로 탐지 | 메시지 |
|---|---|---|---|
| `std::get<I>(a)` | 인스턴스화 시 (라이브러리 `static_assert`, 표준 "Mandates: I < N", [array.tuple]) | 불가 (hard error) | 라이브러리 문구 |
| `constexpr x = a[I]` | 상수 평가 중 UB → 상수식 아님 ([expr.const]) | — | 컴파일러 note |
| `requires (Ch < N)` | 오버로드 해석에서 후보 제외 ([temp.constr]) | **가능** | "constraints not satisfied" |
| 본문 `static_assert(cond, "msg")` | 인스턴스화 시 | 불가 | **내가 쓴 문장** |

그래서 Part B 의 `adc_channel` 은 `requires (Ch < N)` 를 쓰고, 하네스는 그것을 변수 템플릿 + requires-expression 으로 **런타임 CHECK 가능한 bool** 로 바꿔 검증한다:

```cpp
template <std::size_t Ch>
constexpr bool kChannelOk = requires(const AdcFrame& f) { adc_channel<Ch>(f); };
// 템플릿 문맥이라 호출이 ill-formed 면 에러가 아니라 false ([expr.prim.req])
```

반대로 DMA 같은 하드웨어 제약은 "왜 안 되는지" 를 사람이 읽어야 하므로 본문 `static_assert` + 메시지가 낫다 (C 의 `_Static_assert` 와 같은 역할, C-A08 참고).

`std::to_array<uint16_t>({…})` (C++20, [array.creation]) 는 원소 개수 `N` 을 초기화 목록에서 추론한다. `std::array<uint16_t, 8> k{…}` 처럼 8을 손으로 쓰면 원소를 하나 빼먹어도 0 으로 채워져 조용히 컴파일된다 — `to_array` 는 그런 불일치가 생길 자리가 없다.

### Part C — DMA 버퍼: 주소·개수·크기를 한 인자에서

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">alignas(32) std::array&lt;uint16_t, 64&gt; g_adc_dma — 원형(circular) DMA + 반쪽 인터럽트</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="50" width="160" height="40"/><rect x="180" y="50" width="160" height="40"/>
    <rect x="370" y="50" width="160" height="40"/><rect x="530" y="50" width="160" height="40"/>
  </g>
  <rect x="20" y="50" width="320" height="40" class="f-ok" opacity=".35"/>
  <rect x="370" y="50" width="320" height="40" class="f-acc" opacity=".35"/>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="100" y="75">[0..15] 32B</text><text x="260" y="75">[16..31] 32B</text>
    <text x="450" y="75">[32..47] 32B</text><text x="610" y="75">[48..63] 32B</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="108">0x..00 (32B 경계)</text><text x="262" y="108">HT 인터럽트</text><text x="612" y="108">TC 인터럽트</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M340 92 L340 116"/><path d="M690 92 L690 116"/>
    <path d="M690 130 C 700 170, 30 170, 20 130" stroke-dasharray="4 3"/></g>
  <text x="300" y="180" fill="currentColor" font-size="12">CIRC: 끝나면 CNDTR 다시 64, 처음부터</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="204">HT → dma_isr(true)  → sum_half&lt;0&gt;  (DMA 는 뒤 반쪽을 채우는 중)</text>
    <text x="20" y="224">TC → dma_isr(false) → sum_half&lt;1&gt;  (DMA 는 앞 반쪽을 채우는 중)</text>
    <text x="20" y="244">CMAR = buf.data()   CNDTR = buf.size() = 64   MSIZE = size_code(sizeof(T))</text>
  </g>
</svg>
```

C 에서는 `dma_start(ch, buf, sizeof buf)` 처럼 길이를 따로 넘기다가 **바이트 수(128)를 개수(64) 자리에 넣는** 버그가 흔하다 (DMA 가 버퍼 뒤 128바이트를 덮어씀). `dma_start_circular(ch, std::array<T,N>& buf)` 는 `buf.data()`, `buf.size()`, `sizeof(T)` 를 모두 한 인자에서 꺼내므로 그 실수를 할 자리가 없다. `CCR` 계산:

| 필드 | 비트 | `uint16_t` (64개) | `uint8_t` (32개) |
|---|---|---|---|
| MSIZE[11:10] / PSIZE[9:8] | size_code: 1B→0, 2B→1, 4B→2 | `0x500` | `0x000` |
| MINC(7) CIRC(5) HTIE(2) TCIE(1) EN(0) | 고정 | `0x0A7` | `0x0A7` |
| 합계 | | **`0x5A7`** | **`0x0A7`** |

`static_assert` 3개는 칩의 하드웨어 규칙을 타입 수준으로 끌어올린 것이다: CNDTR 은 16비트(`N <= 0xFFFF`), 반쪽 처리는 짝수 개, MSIZE 는 8/16/32비트. 홀수 개 버퍼로 호출하면 위 `demo_bounds` 의 ④ 처럼 **내가 쓴 메시지**로 컴파일이 멈춘다.

**`alignas(32)`** ([dcl.align]) 는 변수 선언에 붙여 시작 주소를 32의 배수로 만든다. Cortex-M7 (STM32F7/H7) 은 D-cache 라인이 32바이트이고, DMA 수신 후 `SCB_InvalidateDCache_by_Addr(addr, size)` 는 라인 단위로 동작한다. 버퍼가 라인 경계에서 시작·끝나지 않으면 **이웃 변수까지 무효화**되어 CPU 가 막 쓴 값이 사라질 수 있다. 그래서 `cache_line_safe<32>` 는 시작 주소와 `sizeof(buf)` (바이트!) 둘 다 본다. ELF 에서는 `.bss` 섹션 정렬이 32 로 올라가고, 특정 RAM 영역(예: STM32H7 의 DMA 접근 가능 SRAM)에 두려면 섹션 속성/링커 스크립트가 추가로 필요하다 (C-B11, C-A05 참고). DMA 가 쓴 내용을 CPU 가 언제 봐도 되는지(배리어·volatile)는 C-A02 참고.

**ISR**: `dma_isr(bool half_transfer, buf)` 는 런타임 플래그를 `sum_half<0>` / `sum_half<1>` 두 **컴파일 타임 인덱스** 로 바꾼다. "반쪽 번호 × N/2" 를 런타임에 계산하다 경계를 틀릴 여지가 없고, `sum_half<2>` 는 컴파일 에러다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 20/20 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined solution.cpp -o solution_asan && ./solution_asan          # 20/20, 경고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| "안전하니까" `a.at(i)` | `-fno-exceptions` 에서 메시지 없는 `abort()` (libc++) / libstdc++ 는 throw 경로를 링크 | 상수 인덱스: `std::get<I>(a)` · 런타임: `if (i >= a.size()) return Err::Range; a[i]` |
| `CNDTR = sizeof(buf)` | `uint16_t` 버퍼면 바이트 수 128 ≠ 원소 수 64 → DMA 가 버퍼 밖 덮어씀 | `CNDTR = buf.size()`; 캐시 검사만 `sizeof(buf)` |
| `std::array<uint8_t,256> t;` (지역, `{}` 없음) | C 배열처럼 **초기화 안 됨** — 쓰레기 값 | `std::array<uint8_t,256> t{};` |
| 256B 버퍼를 값으로 ISR 함수에 전달 | 호출마다 스택에 256B 복사 (위 `call_val` 실측) — ISR 스택 터짐 | `const std::array<T,N>&` |
| 핑퐁 버퍼를 `std::swap(a, b)` | 포인터 교환이 아니라 N 바이트 **내용 복사** (추가 연습 4) | 인덱스/포인터만 뒤집기 |
| `std::array<Reg,2> s{{0,1},{4,2}};` | 바깥 `{}` 가 내부 C 배열을 먹어 `excess elements in struct initializer` (실측) | `s{{ {0,1}, {4,2} }}` (이중 중괄호) |
| `using DmaBuf alignas(32) = std::array<…>;` | `'alignas' attribute only applies to variables, data members and tag types` (실측) | 변수에 `alignas(32) std::array<…> buf;` 또는 `struct alignas(32) DmaBuf { std::array<…> d; };` |
| `std::to_array("ABC")` 를 4바이트 프레임으로 | 문자열 리터럴이라 `'\0'` 포함 `array<char,4>` | `std::to_array<uint8_t>({'A','B','C'})` |

## 8. 면접 Q&A

<details>
<summary>Q1. std::array 는 C 배열 대비 오버헤드가 있나요? 어떻게 증명하죠?</summary>

없다. `T[N]` 멤버 하나뿐인 aggregate 라 `sizeof` 가 같고(표준이 `sizeof == N*sizeof(T)` 를 문구로 보장하진 않지만 모든 주요 구현이 그렇고 `static_assert` 로 고정), `operator[]` 는 인라인되어 `__elems_[i]` 가 된다. 증명: 같은 함수를 C 배열/std::array 로 짜서 `-O2 -S` diff — 이번 레슨 실측에서 `sum64`, `get3`, `crc8_frame` 명령어가 동일했다(테이블 심볼 이름만 다름). 꼬리질문 "디버그 빌드는?" → `-O0` 에선 `operator[]` 호출이 남아 느리다. 펌웨어는 디버그 빌드도 `-Og` 를 쓰는 이유.

</details>

<details>
<summary>Q2. -fno-exceptions 빌드에서 at() 을 쓰면 어떻게 되나요? 대안은?</summary>

표준 밖 동작이라 라이브러리마다 다르다. libc++ (이 맥 버전) 은 `__throw_out_of_range` 가 메시지 없이 `::abort()` — 실측 exit 134, 출력 없음. libstdc++ 는 라이브러리 안의 `__throw_out_of_range_fmt` 를 호출해 예외 런타임을 링크로 끌어올 수 있다. 펌웨어에서 abort 는 HardFault/리셋이지 에러 처리가 아니다. 대안: ① 인덱스가 상수면 `std::get<I>` / `requires` / `static_assert` 로 컴파일 에러 ② 런타임이면 명시적 `if (i < a.size())` + 에러 코드 ③ 개발 빌드에 `_LIBCPP_HARDENING_MODE` / `_GLIBCXX_ASSERTIONS` 로 `operator[]` 트랩 (원인 위치가 남는 brk/assert).

</details>

<details>
<summary>Q3. 룩업 테이블이 플래시에 있는지 RAM 에 있는지 어떻게 확인하고, 어떻게 보장하나요?</summary>

확인: `nm -m` / `objdump -t` 로 심볼 섹션을 본다 — 이번 실측에서 `constexpr` 테이블은 `__TEXT,__const` (ELF `.rodata`), C 런타임 init 테이블은 `__DATA,__bss`. 보장: `constexpr` (또는 `constinit const`) 로 **상수 초기화**를 강제한다. `const` 만 붙이고 초기화식이 constexpr 가 아니면 동적 초기화가 되어 RAM + `.init_array` 엔트리가 생긴다 (CPP-B09 참고). `const` 를 빼면 `.data` — 플래시에 초기값, RAM 에 사본으로 **두 번** 공간을 쓴다 (C-B11 참고).

</details>

<details>
<summary>Q4. std::array 를 값으로 넘기면 비싸지 않나요?</summary>

크기에 달렸다. AAPCS64 에서 16B 이하 composite 는 레지스터로 — 8B 프레임 `bump_last` 는 `add x0, x0, x8 ; ret` 한 줄이었다. 그보다 크면 호출자가 스택에 복사본을 만들고 주소를 넘긴다 — 256B 배열은 `ldp/stp q` 8쌍 + 스택 288B. ARM32 AAPCS 도 r0–r3 이후 스택으로 넘기니 같은 원칙(예상). 규칙: 작은 프레임·좌표·버전 번호는 값으로(복사 = 안전한 스냅샷), 버퍼는 `const&`. 꼬리질문 "그럼 C 배열은 왜 이 문제가 없나?" → 항상 decay 해서 포인터만 가기 때문 — 대신 크기 정보와 복사 의미를 잃는다.

</details>

<details>
<summary>Q5. std::get&lt;I&gt; 와 operator[] 와 at() 의 차이를 한 줄씩.</summary>

`std::get<I>`: 인덱스가 템플릿 인자, `I >= N` 은 컴파일 에러 (Mandates, [array.tuple]), 런타임 비용 0. `operator[]`: 검사 없음, 범위 밖은 UB (hardening 모드면 트랩), `noexcept`. `at()`: 런타임 검사 후 `out_of_range` throw — 예외 없는 빌드에선 abort. 덧붙여 `requires (I < N)` 로 직접 제약을 걸면 오버로드에서 빠지므로 `requires` 표현식으로 "호출 가능한가" 를 질문할 수 있지만, 본문 `static_assert` (`std::get` 내부 포함) 는 hard error 라 탐지할 수 없다.

</details>

<details>
<summary>Q6. DMA 버퍼 선언에서 주의할 점을 모두 말해보세요.</summary>

① 정렬: `alignas(32)` (캐시 라인/버스 폭), 크기도 라인 배수 ② 위치: DMA 가 접근 가능한 RAM 영역 (H7 의 DTCM 은 일부 DMA 불가) → 섹션 속성/링커 스크립트 ③ 개수 vs 바이트: CNDTR 은 원소 수 ④ 캐시 일관성: 수신 후 invalidate, 송신 전 clean (또는 MPU 로 non-cacheable 영역) ⑤ 수명: 전역/static — 스택 버퍼를 DMA 에 넘기고 함수가 리턴하면 DMA 가 남의 스택을 덮어씀 ⑥ CPU 와 공유: 완료 플래그는 ISR 에서 세우고 배리어 고려 (C-A02 참고). `std::array<T,N>&` 인자로 받으면 ③ 은 타입이 해결하고, ①·크기 제약은 `static_assert` 로 문서화+강제한다.

</details>

<details>
<summary>Q7. std::vector 나 std::span 이 아니라 std::array 를 쓰는 기준은?</summary>

`std::array` = 크기가 컴파일 타임에 정해진 **소유(owning)** 저장소, 힙 없음 — 펌웨어 버퍼·테이블의 기본. `std::vector` 는 힙 할당이라 대부분의 펌웨어에서 금지 (CPP-B14 참고). 함수 **인터페이스**에서 여러 크기의 배열을 템플릿 없이 받고 싶으면 비소유 뷰가 필요한데 그건 CPP-B11 의 주제다. 이번 레슨처럼 `template <size_t N>` 으로 받으면 크기마다 함수가 인스턴스화되어 코드가 늘 수 있다는 점이 트레이드오프.

</details>

## 9. 추가 연습

### 연습 1 — 비트 뒤집기 테이블을 컴파일 타임에

C-B01 연습 4 의 `reverse8` 을 256칸 `constexpr std::array` 로 만들고 `static_assert` 로 `kRev8[0x01]==0x80`, `kRev8[0xB4]==0x2D` 를 검증하라.

<details>
<summary>답 보기</summary>

`make_crc8_table` 과 같은 패턴: `t{}` 를 만들고 각 `i` 의 비트 `b` 가 1이면 `r |= 0x80 >> b`. `inline constexpr auto kRev8 = make_rev8_table();` 다음 줄에 `static_assert` — 테이블이 틀리면 **빌드가 실패**하므로 단위 테스트가 컴파일러 안에서 돈다. 성질 검사로 `kRev8[kRev8[x]] == x` (두 번 뒤집으면 원래대로) 도 좋다.

</details>

### 연습 2 — 5-샘플 메디안 필터

`constexpr uint16_t median5(std::array<uint16_t,5> s)` — ADC 스파이크 제거. 원본 윈도는 바뀌면 안 된다.

<details>
<summary>답 보기</summary>

값으로 받은 `s` 가 곧 작업용 복사본이라 `std::sort(s.begin(), s.end()); return s[2];` 로 끝 (10바이트라 복사 비용 무시). C++20 부터 `std::sort` 가 `constexpr` 라 `static_assert(median5({100, 4095, 102, 98, 101}) == 101);` 가 컴파일 타임에 돈다. C 배열이었다면 호출자 버퍼가 정렬되어 버리거나 `memcpy` 를 직접 해야 한다.

</details>

### 연습 3 — 2차원 배열과 구조적 바인딩

`std::array<std::array<uint8_t,4>,3>` 의 `sizeof` 와 행 간격이 `uint8_t[3][4]` 와 같은지 확인하고, `std::array<uint8_t,3>` 픽셀을 `auto [r, g, b] = px;` 로 풀어라.

<details>
<summary>답 보기</summary>

`sizeof == 12`, `&m[1][0]` 과 `&m[0][0]` 의 주소 차 4 → 행 사이 패딩 없음 (실측). `std::array` 는 `tuple_size`/`get` 이 정의된 tuple-like 타입이라 C++17 구조적 바인딩이 된다. C 배열도 구조적 바인딩은 되지만 함수 인자로 받은 순간 포인터라 불가능하다.

</details>

### 연습 4 — 핑퐁 버퍼와 std::swap 함정

`std::swap(x, y)` 후 `x.data()` 는 바뀌는가? DMA 핑퐁 버퍼는 어떻게 "교체" 해야 하나?

<details>
<summary>답 보기</summary>

바뀌지 않는다 — `std::array` 의 swap 은 원소를 하나씩 교환하는 O(N) **내용 복사**이고 주소는 그대로다 (vector 의 O(1) 포인터 swap 과 다름). 64KB 버퍼면 ISR 안에서 64KB 복사. 핑퐁은 `std::array<std::array<uint8_t,N>,2> buf;` + `dma_side ^= 1` 인덱스만 뒤집는 `flip()` 으로 O(1) 에 한다. DMA 에는 새 `buf[dma_side].data()` 주소를 다시 프로그램.

</details>

### 연습 5 — 펌웨어 버전 비교

`using Version = std::array<uint8_t,3>;` 로 `needs_update(running, available)` 를 한 줄로.

<details>
<summary>답 보기</summary>

`return running < available;` — `std::array` 의 비교는 원소별 **사전식(lexicographic)** 이라 `{1,4,2} < {1,10,0}` 이 참 (문자열 `"1.4.2" < "1.10.0"` 이었다면 거짓). C++20 부터 `operator<=>` 로 합성되고 `constexpr`. C 에서는 `memcmp` 를 쓰면 원소가 `uint8_t` 일 때만 맞고, `uint16_t` 리틀 엔디언이면 틀린다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 12/12 passed
```

**관찰용 데모 파일** (레슨 폴더, 하네스와 별개):

```bash
cc  -std=c11   -O2 -S -o - demo_asm_carray.c                                  # C 배열 버전 asm
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_asm_stdarray.cpp     # std::array 버전 asm
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_byvalue.cpp | c++filt  # 값 전달 비용
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_at.cpp -o demo_at && ./demo_at 4   # 파일 머리 주석에 ①~④
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -DSHOW_ERROR -c demo_bounds.cpp     # 컴파일 에러 5개
```

## 10. 핵심 정리

- `std::array<T,N>` = `struct { T elems[N]; }`. sizeof·배치·-O2 코드가 C 배열과 같다 (실측 diff 동일).
- 차이는 **타입에 N 이 남는 것**: decay 없음, `size()` constexpr, 복사·대입·`==`·`<`·값 반환 가능.
- 룩업 테이블은 `constexpr` 함수로 만들어 `inline constexpr` 변수에 → `.rodata`(플래시), 런타임 init 0, `static_assert` 로 검증.
- `at()` 은 `-fno-exceptions` 에서 libc++ = 메시지 없는 `abort()`. 펌웨어에선 쓰지 않는다.
- 상수 인덱스 → `std::get<I>` / `requires (I < N)` / `static_assert` 로 컴파일 에러. 런타임 인덱스 → 명시적 검사 + 에러 코드.
- `requires` 는 탐지 가능(후보 제외), `static_assert` 는 hard error 지만 메시지가 친절.
- DMA: `alignas(32)` + 크기도 라인 배수, CNDTR 은 `size()` (바이트 아님), 하드웨어 제약은 `static_assert`.
- 값 전달: 16B 이하는 레지스터, 큰 버퍼는 `const&`. `std::swap` 은 내용 복사.

## 참고자료

- [cppreference — std::array](https://en.cppreference.com/w/cpp/container/array) · [std::get(std::array)](https://en.cppreference.com/w/cpp/container/array/get) · [std::to_array](https://en.cppreference.com/w/cpp/container/array/to_array)
- C++20 표준 [array.overview], [array.tuple], [array.creation], [sequence.reqmts] (`at`), [dcl.constexpr], [dcl.align], [temp.constr], [expr.prim.req], [depr.array.comp]
- [libc++ Hardening Modes](https://libcxx.llvm.org/Hardening.html) — `_LIBCPP_HARDENING_MODE`
- libc++ 소스: `MacOSX26.4.sdk/usr/include/c++/v1/array`, `.../stdexcept` (`__throw_out_of_range`)
- [Arm AAPCS64 — Procedure Call Standard](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst) (composite 인자 전달) · [AAPCS32](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst)
- [STM32F1/L4 Reference Manual — DMA CCR/CNDTR/CMAR](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [ST AN4839 — Level 1 cache on STM32F7/H7](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf) (DMA 와 캐시 일관성)
- [CRC catalogue — CRC-8/SMBUS (check 0xF4)](https://reveng.sourceforge.io/crc-catalogue/1-15.htm)
