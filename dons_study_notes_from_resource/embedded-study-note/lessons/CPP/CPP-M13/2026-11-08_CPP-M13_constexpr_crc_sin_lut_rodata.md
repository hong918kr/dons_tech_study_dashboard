---
id: CPP-M13
time: 80분
updated: 2026-11-08
---
# CPP-M13 constexpr 테이블 생성: CRC·sin LUT를 .rodata로

## 학습 목표

- `constexpr` 함수로 CRC-32 테이블과 Q15 sin LUT(참조표, look-up table)를 만들고 `static_assert` 로 값을 고정해서, 표가 틀리면 **빌드가 깨지게** 한다.
- `std::sin` 이 C++20 에서 constexpr 이 아니라는 것을 clang·GCC 양쪽에서 확인하고, 직접 만든 Taylor 급수를 런타임에 `std::sin` 과 대조해 오차 한계를 잰다.
- Q15 변환의 함정(+1.0 은 표현 불가, 범위 밖 float→int 는 UB)을 상수 평가 에러로 재현하고 안전한 `to_q15` 를 쓴다.
- 1/4 주기 표 + 위상(phase) 사분면 처리 + 선형 보간으로 flash 와 정확도를 맞바꾼다. 표 크기별 최대 오차를 실측표로 말할 수 있다.
- ELF 에서 `.rodata` 배치, `.init_array`/`.bss` 부재를 `objdump -h -s` 와 `size` 로 **증명**하고, `constexpr` / `constinit` / 동적 초기화의 차이를 flash·RAM 바이트로 설명한다.

## 1. 문제

펌웨어 면접에서 "CRC 표나 sin 표를 어디에 어떻게 두겠냐"는 질문은 flash/RAM 예산, 부팅 시간, 시작 순서(startup)까지 한 번에 묻는 질문이다. C 에서는 보통 둘 중 하나다.

```c
/* 방법 1: 스크립트로 만든 숫자 1024바이트를 소스에 붙여 넣기 → 수정 불가능한 매직 넘버 */
static const uint32_t crc32_table[256] = { 0x00000000, 0x77073096, /* ...254개 더... */ };

/* 방법 2: 부팅 때 계산 → RAM 1 KB + 생성 코드 + (sin 이면) soft-float 라이브러리 */
static uint32_t crc32_table[256];
void crc32_init(void) { for (...) { ... } }   /* 호출을 까먹으면? 표가 전부 0 */
```

C++ 의 `constexpr` 은 방법 1 의 결과물(flash 안의 상수)을 방법 2 의 소스(짧은 생성 코드)로 얻는다. 이 레슨의 핵심 주장은 말이 아니라 **실제 ELF 의 바이트 수**로 확인한다. 같은 표 두 개(CRC 1024 B + sin 514 B)를 Cortex-M4 로 링크한 실측값(`arm-none-eabi-g++ 14.2.1`, 아래 §5 Part C):

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 표, 만드는 시점만 다르다 (Cortex-M4 ELF 실측)</text>
  <rect x="10" y="38" width="350" height="150" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="24" y="60" fill="currentColor" font-weight="bold">constexpr: 빌드 때 계산</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="24" y="84">constexpr auto t = make_table();</text>
    <text x="24" y="108">.rodata   1538 B  (표 그대로)</text>
    <text x="24" y="128">.bss      0 B</text>
    <text x="24" y="148">.init_array 없음</text>
    <text x="24" y="170">flash 1651 B / RAM 0 B</text>
  </g>
  <rect x="380" y="38" width="350" height="150" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="394" y="60" fill="currentColor" font-weight="bold">동적 초기화: 부팅 때 계산</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="394" y="84">const auto t = make_table_rt();</text>
    <text x="394" y="108">.bss       1540 B</text>
    <text x="394" y="128">.init_array 4 B + 생성 코드</text>
    <text x="394" y="148">soft-float 심볼 16개 끌려옴</text>
    <text x="394" y="170">flash 3799 B / RAM 1540 B</text>
  </g>
  <text x="10" y="212" fill="currentColor">오른쪽은 main 전에 코드가 돌아야 값이 생긴다 → 시작 순서·부팅 시간·ISR 첫 접근이 문제.</text>
  <text x="10" y="234" fill="currentColor">왼쪽은 값이 이미지에 박혀 있다 → 리셋 직후 첫 명령부터 유효하다.</text>
  <text x="10" y="254" fill="currentColor">flash = text + data (arm-none-eabi-size). 두 쪽 모두 같은 _start 와 조회 함수 포함.</text>
</svg>
```

> 표를 만드는 **언어 기능**(`constexpr` 함수, 왜 Part A 가 컴파일되는가)의 기초는 CPP-B04, `.init_array`·`constinit` 의 동작 원리는 CPP-B09, `std::array` 는 CPP-B10 참고. 여기서는 그 위에 "표"를 올린다. CRC 알고리즘 자체의 속도 비교는 ALGO 트랙(비트 → 테이블 → slicing) 소관이다.

### Part A — CRC-32 테이블 (`solution.cpp` 의 TODO A1~A5)

- `constexpr Crc32Table make_crc32_table()`: reflected CRC-32 (다항식 `0xEDB88320`) 256 엔트리. `inline constexpr` 전역 하나.
- `crc32_update(crc, byte)`, `crc32(span<const uint8_t>)`, `crc32(string_view)` 를 모두 `constexpr` 로. 초깃값 `0xFFFFFFFF`, 마지막에 비트 반전.
- `static_assert` 로 표 몇 칸과 체크값 `crc32("123456789") == 0xCBF43926` 을 고정한다. 함정: 왜 `string_view` 오버로드가 필요한가?

### Part B — 자체 sin, Q15, 위상 조회 (TODO B1~B5)

- `sin_taylor(x)`: `0 ≤ x ≤ π/2` 용 Taylor 급수. `std::sin` 을 쓰면 안 된다.
- `to_q15(v)`: 스케일 32768, 가장 가까운 정수, **범위 포화**. `+1.0` 을 넣어도 터지면 안 된다.
- `make_sin_quarter()`: 1/4 주기 257 엔트리. `sin_q15(phase)`: 16비트 위상의 상위 2비트로 사분면, 다음 8비트로 인덱스, 하위 6비트로 선형 보간.
- 전체 65536 위상을 전수 검사해 `std::sin` 대비 오차, 사분면 대칭(`sin(π−x)=sin(x)`, `sin(−x)=−sin(x)`)이 정확히 성립하는지 확인한다.

### Part C — 펌웨어 시나리오: 이미지 봉인과 NCO (TODO C1~C5)

- `seal_image(payload)`: 헤더(magic, 길이, payload CRC)를 붙인 바이트 배열을 **컴파일타임에** 만든다. `verify_image(flash)` 는 부트로더 쪽 런타임 검증이다. 같은 함수가 양쪽에서 돈다.
- `consteval nco_step(freq, fs)`: NCO(수치 제어 발진기, numerically controlled oscillator) 위상 증분. `Nco::next()` + `fill_dac()`: DMA 하프/풀 콜백에서 버퍼를 채운다.
- 섹션 배치는 `demo_sections.cpp` + `target_check.sh` 로 확인한다 (호스트 harness 가 할 수 없는 일).

## 2. Problem (English)

> **Compile-time lookup tables that land in flash.**
>
> 1. Implement `constexpr` generators for a 256-entry reflected CRC-32 table (poly `0xEDB88320`) and `crc32()` over `std::span<const uint8_t>` and `std::string_view`. Pin values with `static_assert`: `crc32_table[1] == 0x77073096`, `crc32("123456789") == 0xCBF43926`. Why is a `string_view` overload needed instead of casting `const char*` to `const uint8_t*`?
> 2. `std::sin` is not `constexpr` in C++20. Write your own for `[0, π/2]`, and validate it against the host `std::sin` at runtime. Produce a 257-entry quarter-wave Q15 table. Handle the fact that `+1.0` is not representable in Q15. Implement `int16_t sin_q15(uint16_t phase)` (`65536 == 2π`) with linear interpolation. Report the maximum error over all 65536 phases.
> 3. Implement `seal_image()` (compile-time: prepend magic/length/CRC) and `verify_image()` (runtime: checks length without integer overflow). Implement an NCO with a `consteval` phase-step that rejects `freq >= fs/2`.
> 4. Show on a Cortex-M ELF that the tables are in `.rodata`, with no `.init_array` and no `.bss` usage. Contrast with a non-`constexpr` version. Explain `constexpr` vs `constinit` vs dynamic initialization in terms of flash and RAM bytes.
>
> Constraints: `-std=c++20 -fno-exceptions -fno-rtti`, no heap, no `std::sin` in the generator.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "29/86 passed" — TODO 를 채워 86/86 을 만든다
# 섹션 확인 (맥): bash target_check.sh obj   (타깃 링크는 bash target_check.sh elf, Lima VM elinux 필요)
```

이 레슨의 데모 (`ARM_INC` 는 CPP-A04 와 같은 기본 경로를 쓰며 없으면 VM 에서 가져온다):

| 파일 | 보여 주는 것 | 실행 |
|---|---|---|
| `demo_sections.cpp` | 같은 표를 6가지 방식으로 정의: `.rodata`/`.data`/`.bss`/`.init_array` | `bash target_check.sh obj` / `elf` / `rodata` / `o0` |
| `demo_fold_probe.cpp` | 동적 초기화를 최적화기가 지우는가 (clang·GCC) | `bash target_check.sh fold` |
| `demo_local_constexpr.cpp` | 함수 안 `constexpr` vs `static constexpr` 어셈블리 | `bash target_check.sh local` |
| `demo_odr.hpp` `demo_odr_a.cpp` `demo_odr_b.cpp` | 헤더 `constexpr` vs `inline constexpr`: 표 사본 수 | `bash target_check.sh odr` |
| `demo_errors.cpp` | 상수 평가 실패 5종 (`-DSHOW_ERROR`) | `c++ -std=c++20 -fsyntax-only -DSHOW_ERROR demo_errors.cpp` 또는 `bash target_check.sh errors` |
| `demo_error_table.cpp` | 표 크기별 최대 오차 (호스트) | `c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_error_table.cpp -o demo_error_table && ./demo_error_table` |
| `demo_big_table.cpp` | 표 크기별 상수 평가 한도·컴파일 시간 | `bash target_check.sh big` |

시작 코드에는 `static_assert` 를 일부러 주석으로 두었다(표가 비어 있으면 컴파일이 안 되므로). 대신 하네스의 `CT_CHECK(expr)` 는 `constexpr bool` 변수에 넣어 **상수 평가를 강제**한 뒤 결과를 런타임 CHECK 로 보고한다.

## 4. 힌트

<details>
<summary>힌트 1 — constexpr 함수 안에서 배열을 채워도 되나?</summary>

C++14 부터 `constexpr` 함수에 지역 변수와 반복문을 쓸 수 있고, C++17 부터 `std::array` 의 non-const `operator[]` 가 constexpr 이다. 함수 안에서 `Crc32Table t{}; t[i] = c;` 하고 반환하면 된다. `{}` 를 빼면 C++17 까지는 constexpr 안의 비초기화 변수 자체가 금지였고(C++20 에서 완화), 어쨌든 표는 0 으로 시작하는 게 안전하다.

</details>

<details>
<summary>힌트 2 — 왜 span 으로 "abc" 를 못 받나?</summary>

`const char*` → `const uint8_t*` 는 `reinterpret_cast` 이고, 그것은 상수식에 올 수 없다([expr.const]). 상수 평가 경로에는 `string_view` 를 받아 문자마다 `static_cast<uint8_t>` 를 하면 된다. 런타임 버퍼는 span 으로 받는다.

</details>

<details>
<summary>힌트 3 — sin 급수의 다음 항은?</summary>

항을 따로 팩토리얼로 계산하지 말고 앞 항에서 얻는다: `term *= -x*x / ((2n)*(2n+1))`. n=1 은 -x³/3! 이다. 팩토리얼·거듭제곱을 직접 계산하면 double 범위를 넘을 위험과 항마다 큰 반복이 생긴다.

</details>

<details>
<summary>힌트 4 — 왜 32768 이 문제인가?</summary>

Q15 의 범위는 [-1.0, +1.0) = [-32768, 32767] 이다. `sin(π/2) * 32768 = 32768` 은 `int16_t` 에 들어가지 않는다. **먼저 범위를 확인하고** 그다음 cast 한다. 순서를 바꾸면 그 cast 가 UB([conv.fpint])이고, 상수 평가라면 컴파일 에러가 난다.

</details>

<details>
<summary>힌트 5 — 사분면에서 인덱스 어떻게 뒤집나?</summary>

사분면 1 은 사분면 0 의 거울상이다: `a = t[256 - idx]`, `b = t[255 - idx]` 로 시작점과 끝점을 정한 뒤 **보간식은 그대로** 쓴다. 사분면 2/3 은 부호만 뒤집는다. 음수 쪽 `-32767` 을 쓰면 `-y` 가 안전하고 (Q15 의 `-32768` 은 negate 하면 넘친다), 이 표의 값 범위는 `[0, 32767]` 이다.

</details>

<details>
<summary>힌트 6 — length 검사에서 덧셈을 쓰지 마라</summary>

`HEADER_SIZE + len > flash.size()` 는 `len = 0xFFFFFFFF` 일 때 넘친다(이 harness 에선 size_t 가 64비트라 안 넘치지만, Cortex-M 의 32비트 size_t 에서는 넘친다). `len > flash.size() - HEADER_SIZE` 로 쓰면 앞의 크기 검사 덕분에 뺄셈이 안전하다.

</details>

## 5. 풀이

### Part A — "함수를 컴파일러가 실행한다"

`make_crc32_table` 의 본체는 C 로 쓰던 것과 똑같다. 달라지는 건 **누가 언제 실행하느냐**뿐이다. `inline constexpr Crc32Table crc32_table = make_crc32_table();` 의 초기화식은 반드시 상수식이어야 하므로, 컴파일러는 함수를 추상 기계에서 끝까지 실행해 256 개 값을 얻는다. 표가 잘못되면 실행 시점이 아니라 **컴파일 시점**에 `static_assert` 가 깨진다. 값을 소스에 복붙하지 않아도 된다.

| 문맥 | 평가 시점 | 비고 |
|---|---|---|
| `constexpr` 변수의 초기화식 | 반드시 컴파일타임 | 상수식이 아니면 에러 |
| `static_assert(...)` | 반드시 컴파일타임 | |
| `crc32(user_buffer)` (런타임 인자) | 런타임 | 같은 함수가 그냥 일반 코드로 컴파일된다 |
| `consteval` 함수 호출 | 반드시 컴파일타임 | 런타임 인자로 부르면 에러 (Part C 의 `nco_step`) |

`crc32()` 를 `constexpr` 로 만들면 코드가 두 벌 생기는 것이 아니다. 하나의 함수를 두 가지 문맥에서 쓸 뿐이다. 그래서 Part C 의 `verify_image` 는 `CT_CHECK` 안에서(컴파일타임) **그리고** 런타임에도 돈다.

harness 의 체크값(`"123456789"`, `"a"`, `"abc"`, fox 문장)이 실제 CRC-32 값인지는 Python `zlib` 로 따로 확인했다 (실행 결과):

```text
$ python3 -c "import zlib; print(hex(zlib.crc32(b'123456789')), hex(zlib.crc32(b'a')), hex(zlib.crc32(b'abc')), hex(zlib.crc32(b'The quick brown fox jumps over the lazy dog')))"
0xcbf43926 0xe8b7be43 0x352441c2 0x414fa339
```

호스트 빌드에서도 같은 일이 일어난다: `solution.cpp` 를 -O2 로 컴파일한 Mach-O 오브젝트에서 `xcrun llvm-nm -m` 은 `(__TEXT,__const) ... _crc32_table`, `(__TEXT,__const) ... _sin_quarter` 를 보여 주고, 링크된 실행 파일에는 `__mod_init_func` 섹션이 없다 (`objdump -h` 로 확인).

`string_view` 오버로드가 필요한 이유는 상수 평가의 규칙이다. `demo_errors.cpp` 의 `first_byte()` 는 `reinterpret_cast<const uint8_t*>(s)` 를 쓰는 함수를 컴파일타임에 부르면 어떻게 되는지 본다(실제 출력, Apple clang 21):

```text
demo_errors.cpp:20:25: error: constexpr function never produces a constant expression [-Winvalid-constexpr]
demo_errors.cpp:22:21: note: reinterpret_cast is not allowed in a constant expression
```

런타임 경로에서는 `std::span<const std::uint8_t>` 가 그대로 쓰인다. 호스트에서 `crc32("abc")` 와 `crc32(span)` 이 같은 값을 내는지는 harness 가 확인한다.

### Part B — sin 을 직접 만들고 Q15 로 내리기

**왜 std::sin 을 못 쓰나.** C++20 표준에서 `std::sin` 은 constexpr 이 아니다 (C++26 P1383 에서야 constexpr 로 지정됐다). 실제로 돌려 보면 컴파일러가 갈린다.

```text
Apple clang 21, -std=c++20 (demo_errors.cpp):
demo_errors.cpp:36:18: error: constexpr variable 'e4' must be initialized by a constant expression
demo_errors.cpp:36:23: note: non-constexpr function 'sin' cannot be used in a constant expression

arm-none-eabi-g++ 14.2.1, -std=c++20 -DSHOW_ERROR: 위 줄(e4)에는 에러가 나지 않는다.
따로 만든 1줄 파일로 static_assert(x > 0.84 && x < 0.85) 도 통과 확인.
같은 파일에 -fno-builtin 을 주면: error: call to non-'constexpr' function 'double sin(double)'
```

GCC 는 `sin` 을 내장 함수(builtin)로 처리해 접어 주는 비표준 확장이다. 이식성이 없으니 쓰면 안 된다 — 클랜으로 옮기는 순간 깨진다. 그래서 직접 구현한다.

**sin_taylor 의 오차.** n=13 항(x²⁷)까지 쓰면 x=π/2 에서 다음 항은 (π/2)²⁹/29! ≈ 6×10⁻²⁶ 으로 double 의 반올림 오차(≈1.1×10⁻¹⁶)보다 훨씬 작다. 즉 잘림 오차가 아니라 반올림 오차가 지배한다. 호스트 `std::sin` 과 4097 점에서 비교한 실측 최대 차이는 **3.3×10⁻¹⁶** 이다 (harness 임계값 `1e-15`).

"컴파일타임 double 과 런타임 double 이 같은가"는 표준이 보장하지 않는다([expr.const] 의 부동소수점 주석: 번역 중의 평가와 실행 중의 평가가 같은 결과일지는 미지정). 그런데 이 표는 **정수로 반올림된 결과**라서 double 의 마지막 비트 차이에 둔감하다. 실제로 clang 이 만든 `.rodata` 와 GCC 가 만든 `.rodata` 를 비교해 보니 첫 1536 바이트(CRC 표 전체 + sin 표 대부분)가 완전히 같았다 (`demo_sections.cpp` VARIANT=1 을 두 컴파일러로 빌드해 `cmp`).

**Q15 변환의 함정.** 두 가지를 순서대로 해결한다.

| 문제 | 왜 문제인가 | 대처 |
|---|---|---|
| `+1.0 * 32768 == 32768` | `int16_t` 범위 밖 | 반올림 후 `32767` 로 포화 |
| float → int 범위 밖 변환 | UB ([conv.fpint]) | **cast 전에** 범위 확인 |
| 음수 반올림 | 0 쪽 절삭이면 음수 쪽이 짧아져 sin 이 대칭이 안 된다 | `v >= 0 ? v + 0.5 : v - 0.5` |

나쁜 구현을 그대로 컴파일하면 상수 평가가 UB 를 잡아 준다 (실제 출력):

```text
clang: demo_errors.cpp:12:77: note: value 32768 is outside the range of representable values of type 'std::int16_t' (aka 'short')
gcc:   demo_errors.cpp:12:51: error: overflow in constant expression [-fpermissive]
```

`requires` 식으로 하네스에서도 같은 사실을 증명한다 (`solution.cpp`):

```cpp
template <double V> concept NaiveQ15Const = requires { typename Q15Probe<naive_q15(V)>; };
CT_CHECK(NaiveQ15Const<0.5>);    // 상수식 OK
CT_CHECK(!NaiveQ15Const<1.0>);   // 범위 밖 변환 → 상수식 아님 → false (컴파일 에러가 아니라 bool)
CT_CHECK(SafeQ15Const<1.0>);     // to_q15 는 포화라서 OK
```

**왜 1/4 주기인가.** sin 은 π/2 를 기준으로 대칭이고 π 를 기준으로 부호만 바뀐다. 그러니 첫 사분면만 저장하면 나머지 세 사분면은 인덱스 뒤집기와 부호 반전으로 복원된다. 저장량이 약 1/4 이 된다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">16비트 위상 하나로 사분면·인덱스·보간비를 모두 얻는다</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <rect x="30" y="38" width="120" height="30" class="f-acc" stroke="currentColor"/>
    <text x="52" y="58">[15:14] 사분면</text>
    <rect x="150" y="38" width="270" height="30" class="f-ok" stroke="currentColor"/>
    <text x="210" y="58">[13:6] 표 인덱스 idx (0~255)</text>
    <rect x="420" y="38" width="180" height="30" class="f-mut" stroke="currentColor"/>
    <text x="440" y="58">[5:0] frac (0~63)</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <line x1="30" y1="170" x2="700" y2="170"/>
    <path d="M30 170 C 80 100, 130 90, 197 90 C 265 90, 315 100, 365 170 C 415 240, 465 250, 532 250 C 600 250, 650 240, 700 170"/>
  </g>
  <g stroke="currentColor" stroke-dasharray="3 3">
    <line x1="197" y1="84" x2="197" y2="256"/>
    <line x1="365" y1="84" x2="365" y2="256"/>
    <line x1="532" y1="84" x2="532" y2="256"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="58" y="84">Q0: t[idx]→t[idx+1]</text>
    <text x="205" y="84">Q1: t[256-idx]→t[255-idx]</text>
    <text x="374" y="266">Q2: −t[idx]→</text>
    <text x="374" y="284">−t[idx+1]</text>
    <text x="540" y="266">Q3: −t[256-idx]→</text>
    <text x="540" y="284">−t[255-idx]</text>
    <text x="30" y="266">표 저장: Q0 만 257칸 (0~256, 끝 칸은 포화)</text>
    <text x="30" y="284">flash 514 B, 전체 주기 1024칸이면 2048 B</text>
  </g>
</svg>
```

보간은 `y = a + (((b - a) * frac + 32) >> 6)` 이다. `(b-a)*frac` 은 음수가 될 수 있지만 C++20 부터 부호 있는 정수의 산술 우측 시프트가 정의되어 있다([expr.shift], P0907). 사분면 1 에서는 `a`/`b` 가 이미 뒤집혀서 보간식 자체를 바꿀 필요가 없다. 그리고 이 구성 덕분에 **전체 위상에서** `sin_q15(0x8000-p) == sin_q15(p)` 와 `sin_q15(-p) == -sin_q15(p)` 가 정확히 성립한다 — 격자점뿐 아니라 frac≠0 에서도. harness 가 65536 위상 전수로 확인한다 (mirrored/negated 불일치 0).

왜 정확히 맞아떨어지는지 한 줄로: 거울상 phase 의 frac 은 `64 - frac`, 양 끝이 `a↔b` 뒤바뀌므로 정리하면 원래 식 `a + round((b-a)·frac/64)` 와 항등이다.

**오차 계산.** 최대 오차는 다음 세 가지 합이다.

| 항목 | 크기 | 비고 |
|---|---|---|
| 표 반올림 | ≤ 0.5 LSB | 실측 표 전체 최대 **0.4964** |
| 보간 곡률 오차 | h²/8·max\|f″\| ≈ 0.154 LSB | h=(π/2)/256, f″=32768·sin |
| 출력 반올림 | ≤ 0.5 LSB | `+32 >> 6` |
| 피크 포화 | 1 LSB (π/2 칸만) | sin=1.0 표현 불가 |

전체 65536 위상 실측 최대 오차는 **1.0007 LSB** (worst phase `0x3D5B`). harness 임계값은 `1.1`.

**표 크기 결정.** 값의 정확도와 flash 는 교환 관계다. `demo_error_table.cpp` 실제 출력:

```text
구간  엔트리   flash(B)  no-interp   interp   (최대 오차, Q15 LSB)
  16      17        34     3208.86       40.51
  32      33        66     1604.86       10.61
  64      65       130      801.03        3.49
 128     129       258      399.03        1.44
 256     257       514      198.03        1.00
 512     513      1026       97.86        1.00
1024    1025      2050       47.59        1.00
```

읽는 법: 보간 없이(`no-interp`) 쓰면 256 구간도 198 LSB(±0.6%)나 틀린다 — 대부분의 오디오/모터 제어에는 쓸 수 없다. **보간을 넣으면** 256 구간에서 1.00 LSB 로 바닥에 닿고, 그 이상은 표를 늘려도 나아지지 않는다(표 반올림 0.5 + 출력 반올림 0.5 의 양자화 바닥). 128 구간도 1.44 LSB 면 많은 제어 루프에서 충분하다. 즉 표 크기를 정하는 것은 "최대 허용 오차" 이고, 이 표가 면접에서 "왜 256칸인가"에 대한 답이다.

### Part C — 무엇이 어디에 놓이는가

**이미지 봉인.** `seal_image` 는 `constexpr` 이므로 `inline constexpr auto sealed_cal = seal_image(CAL_PAYLOAD);` 는 헤더와 CRC 가 계산된 20바이트 배열이 된다. 주소가 필요하면 `.rodata` 에 놓이지만, 호스트 -O2 빌드에서는 실측상 `sealed_cal` 심볼이 아예 없었다(런타임 `verify_image` 까지 컴파일타임에 접혔다는 뜻). 실제 플래시에 반드시 남겨야 하면 `[[gnu::used]]` 나 링커 스크립트의 KEEP 이 필요하다. 부트로더가 런타임에 부르는 `verify_image` 와 **같은 CRC 코드**가 빌드 때 도장을 찍는다. 빌드 스크립트로 별도 도구를 돌려 CRC 를 패치하는 파이프라인(후처리 단계)이 필요 없는 경우에 쓸 수 있다. 실제 펌웨어 이미지 전체(링커가 만든 바이트)를 봉인하는 것은 여전히 후처리 도구의 일이다 — constexpr 은 **소스에 있는 데이터**만 본다.

`verify_image` 의 길이 검사는 `len > flash.size() - HEADER_SIZE` 다. `HEADER_SIZE + len > flash.size()` 로 쓰면 `len=0xFFFFFFFF` 일 때 32비트 `size_t` 에서 덧셈이 넘쳐 검사를 통과해 버린다. 주의: 호스트 harness 는 `size_t` 가 64비트라 이 실수를 잡지 못한다 (`0xFFFFFFFF` 케이스는 양쪽 식 모두 통과) — 32비트 타깃에서만 드러나는 버그라서 리뷰로 잡아야 한다.

**NCO.** `phase` 는 32비트 누산기로 `uint32_t` wrap 이 곧 2π 순환이다 (unsigned 오버플로는 잘 정의됨). `step = round(f · 2³² / fs)` 는 `consteval` 이라 런타임 나눗셈이 없고, `fs/2` 이상이면 빌드가 깨진다(하네스 `StepConst` 로 증명, 나이퀴스트 위반 차단). 위상 상위 16비트만 조회에 쓰므로 위상 절단(phase truncation) 오차가 있다: 한 위상 LSB(65536분의 1 회전)는 `2π/65536 × 32768 ≈ 3.1` LSB 진폭에 해당한다. 그래서 테스트에서 `0x7FFFFFF8` 근처의 표본이 0 이 아니라 ±3 LSB 정도다.

**섹션 실측 — 이 레슨의 핵심 증거.** `demo_sections.cpp` 는 같은 CRC·sin 표를 다섯 방식으로 정의한다. Mac clang → ELF `.o` (`target_check.sh obj`, 실제 출력, 필터링):

```text
=== VARIANT=1 (clang -O2)    ← constexpr
  2 .text           00000102 00000000 TEXT
  6 .rodata         00000602 00000000 DATA
00000000 00000400 r _ZL9crc_table
00000400 00000202 r _ZL9sin_table
soft-float 호출: 0개
=== VARIANT=2 (clang -O2)    ← constinit (const 없음)
  2 .text           0000012a 00000000 TEXT
  6 .data           00000602 00000000 DATA
00000000 00000400 D crc_table
00000400 00000202 D sin_table
=== VARIANT=3 (clang -O2)    ← constinit const
  6 .rodata         00000602 00000000 DATA
=== VARIANT=4 (clang -O2)    ← 생성 함수가 constexpr 아님
  2 .text           000008a8 00000000 TEXT
  6 .bss            00000602 00000000 BSS
  7 .init_array     00000004 00000000 DATA
000006c0 000001e8 t _GLOBAL__sub_I_demo_sections.cpp
soft-float 호출: 5개
=== VARIANT=5 (clang -O2)    ← C 방식: .bss + 직접 호출하는 init()
  2 .text           000008c6 00000000 TEXT
  6 .bss            00000602 00000000 BSS
=== VARIANT=6 (clang -O2)    ← CRC 표만 동적 초기화
  2 .text           000002b2 00000000 TEXT
  6 .bss            00000400 00000000 BSS
  7 .init_array     00000004 00000000 DATA
```

`.rodata` 안의 표 바이트(`llvm-objdump -s -j .rodata`, VARIANT=1): 첫 4 바이트 `00000000` 은 `table[0]`, 그다음 `96300777` 은 little-endian 으로 `0x77073096` = `table[1]` 이다. 끝의 `ff7f` 가 `sin_table[256] = 32767` 이다.

```text
Contents of section .rodata:
 0000 00000000 96300777 2c610eee ba510999  .....0.w,a...Q..
 0010 19c46d07 8ff46a70 35a563e9 a395649e  ..m...jp5.c...d.
 ...
 05f0 d97fe27f ea7ff17f f67ffa7f fe7fff7f  ................
 0600 ff7f                                 ..
```

**실제 링크** (Lima VM, `target_check.sh elf`, `arm-none-eabi-g++ 14.2.1`, `arm-none-eabi-size`):

| VARIANT | 정의 | text | data | bss | .init_array | soft-float 심볼 |
|---|---|---:|---:|---:|---|---:|
| 1 | `constexpr` | 1651 | 0 | 0 | 없음 | 0 |
| 2 | `constinit` (non-const) | 115 | 1538 | 0 | 없음 | 0 |
| 3 | `constinit const` | 1651 | 0 | 0 | 없음 | 0 |
| 4 | 동적 초기화 | 3795 | 4 | 1540 | 4 B | 16 |
| 5 | C 방식(.bss + init) | 4191 | 0 | 1540 | 없음 | 16 |
| 6 | 동적 초기화, CRC 표만 | 327 | 4 | 1024 | 4 B | 0 |

`text` 는 `.text` + `.rodata`, `data` 는 `.data` + `.init_array` 다. flash 사용량은 `text + data`, RAM 은 `.data` 와 `.bss` 의 합이다. 그러니 VARIANT 1 은 flash 1651 B / RAM 0, VARIANT 4 는 flash 3799 B / RAM 1540 B 다 (`.init_array` 4 B 는 `size` 가 `data` 칸에 넣어 보여 주지만 쓰지 않는 포인터 표라서, 실제 링커 스크립트는 보통 flash 에 둔다 — 이 데모는 데모용 기본 스크립트라 RAM/flash 구분을 그대로 읽지 않고 숫자만 읽었다). 표 두 개 때문에 `__aeabi_d*` 등 soft-float 헬퍼 16개가 flash 로 끌려온 것이 차이의 큰 부분이다 (M4 FPU 는 단정밀도뿐이라 `double` 은 소프트웨어 연산이고, 이 빌드는 `-mfloat-abi` 를 주지 않아 soft 기본값이다).

VARIANT 2 가 눈여겨볼 지점이다. `constinit` 은 "상수 초기화"를 **보증**할 뿐, 변수를 읽기 전용으로 만들지 않는다. const 가 없으니 쓰기 가능 → `.data` → **flash 에 초기값 1538 B + RAM 1538 B** 로 부트 시 복사된다. 표를 수정할 일이 없는데 `constinit` 만 쓰면 RAM 을 낭비한다.

```svg
<svg viewBox="0 0 740 256" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">constexpr · constinit · const · 동적 초기화: 어디에 놓이나</text>
  <g fill="currentColor" font-size="12">
    <text x="20" y="48" font-weight="bold">선언</text><text x="280" y="48" font-weight="bold">초기화</text><text x="420" y="48" font-weight="bold">쓰기</text><text x="500" y="48" font-weight="bold">섹션 (실측)</text>
  </g>
  <line x1="10" y1="56" x2="730" y2="56" stroke="currentColor"/>
  <rect x="10" y="62" width="720" height="34" class="f-ok"/>
  <rect x="10" y="102" width="720" height="34" class="f-warn"/>
  <rect x="10" y="142" width="720" height="34" class="f-ok"/>
  <rect x="10" y="182" width="720" height="34" class="f-mut"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="84">constexpr T t = make();</text><text x="280" y="84">상수 (보증)</text><text x="420" y="84">불가</text><text x="500" y="84">.rodata  flash만</text>
    <text x="20" y="124">constinit T t = make();</text><text x="280" y="124">상수 (보증)</text><text x="420" y="124">가능</text><text x="500" y="124">.data  flash+RAM</text>
    <text x="20" y="164">constinit const T t = make();</text><text x="280" y="164">상수 (보증)</text><text x="420" y="164">불가</text><text x="500" y="164">.rodata  flash만</text>
    <text x="20" y="204">const T t = make_rt();</text><text x="280" y="204">동적 (코드 실행)</text><text x="420" y="204">불가</text><text x="500" y="204">.bss + .init_array</text>
  </g>
  <text x="10" y="244" fill="currentColor">const 는 "쓰지 않는다"는 약속, constinit 은 "빌드 때 값이 정해진다"는 약속 — 서로 다른 축이다.</text>
</svg>
```

**"최적화기가 알아서 접어 주지 않나?"** 표준은 동적 초기화를 정적으로 바꾸는 것을 *허용*만 한다 ([basic.start.static]). `demo_fold_probe.cpp` 로 같은 코드를 컴파일러·옵션별로 비교했다 (`target_check.sh fold`, 실제 출력):

```text
--- clang CASE=1 -O0                                  (CASE=1: 단순 함수 호출로 얻은 전역)
 10 .bss                                00000004 00000000 BSS
 11 .init_array                         00000004 00000000 DATA
--- clang CASE=1 -O2
  3 .rodata         00000004 00000000 DATA              ← 접혔다
--- clang CASE=2 -O2                                  (CASE=2: 16칸 표, 루프)
  3 .rodata         00000040 00000000 DATA              ← 접혔다
--- gcc CASE=1 -O2
.bss 00000004
.init_array 00000004                                    ← 안 접혔다
--- gcc CASE=2 -O2
.bss 00000040
.init_array 00000004
```

즉 같은 "비 constexpr" 코드가 clang 에서는 사라지고 GCC 에서는 남았다. VARIANT 4/6 (256 칸 표) 은 clang·GCC 모두 안 접었다. 표 크기나 컴파일러 버전이 바뀌면 조용히 달라지는 것이 문제다 — `constexpr` / `constinit` 은 이것을 **컴파일 에러로 바꾸는** 도구다. `-O0` 에서도 VARIANT 1/3 은 `.rodata` 였고 (`target_check.sh o0`), 동적 VARIANT 4 는 `.bss`+`.init_array` 그대로였다.

**함수 안의 constexpr: static 을 붙여라.** `demo_local_constexpr.cpp` 는 지역 `constexpr` 표와 지역 `static constexpr` 표를 비교한다 (`target_check.sh local`, 실제 asm, 지시어 제거).

```text
arm-none-eabi-g++ 14.2.1 -O2                clang -O2 (같은 함수)
lut_local:                                  lut_local:
    push  {r4, lr}                              movw r1, :lower16:.L__const...
    sub   sp, sp, #1024                         uxtb r0, r0
    ldr   r1, .L4                               movt r1, :upper16:.L__const...
    mov   r4, r0                                ldr.w r0, [r1, r0, lsl #2]
    mov   r2, #1024                             bx   lr
    mov   r0, sp
    bl    memcpy          ← 1 KB 스택 복사!
    uxtb  r4, r4
    ldr   r0, [r0, r4, lsl #2]
    add   sp, sp, #1024
    pop   {r4, pc}
lut_static:                                 pass_local (주소를 넘기면 clang 도):
    ldr   r3, .L7                               sub.w sp, sp, #1024
    uxtb  r0, r0                                ...
    add   r3, r3, r0, lsl #2                    bl   __aeabi_memcpy4
    ldr   r0, [r3, #1024]                       ...
    bx    lr
```

지역 `constexpr` 은 **자동 저장 기간**(automatic storage) 객체이므로, GCC 는 -O2 에서도 호출마다 1 KB 를 스택으로 복사했고, clang 은 인덱싱만 하는 경우 `.rodata` 로 승격했지만 표의 주소를 함수에 넘기면(`pass_local`) 역시 스택 복사를 했다. **함수 안 표는 `static constexpr`** — 아니면 파일 범위에 `inline constexpr` 로 둔다.

**헤더의 constexpr: inline 을 붙여라.** 네임스페이스 범위 `constexpr` 변수는 내부 링크(internal linkage)다. `demo_odr.hpp` 를 두 TU 가 포함할 때 실측(`target_check.sh odr`):

```text
=== 플래그: []                    (constexpr)
   2104	      0	      0	   2104	    838	o.elf
00000400 r table
00000400 r table                  ← 표가 2벌
=== 플래그: [-DUSE_INLINE]        (inline constexpr)
   1080	      0	      0	   1080	    438	o.elf
00000400 V table                  ← 1벌 (weak, 링커가 병합)
```

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `static_cast<int16_t>(v * 32768.0)` | `v=1.0` 이면 32768 → 범위 밖 float→int = UB ([conv.fpint]). 상수 평가면 에러, 런타임이면 조용한 쓰레기 | 범위 확인 후 cast |
| `constinit Table t = make();` 로 "플래시에 넣었다"고 생각 | constinit 은 초기화 시점만 보증. non-const → `.data` → RAM 1538 B 낭비 (실측 VARIANT 2) | `constexpr` 또는 `constinit const` |
| 헤더에 `constexpr Table t = ...;` | 내부 링크라 TU 마다 복사본 (실측 2104 B vs 1080 B) | `inline constexpr` |
| 함수 안에 `constexpr Table t = ...;` | 자동 저장 기간. GCC -O2 도 호출마다 1 KB memcpy (실측) | `static constexpr` 또는 파일 범위 |
| `std::sin` 을 constexpr 안에서 사용 | C++20 비표준. GCC 는 통과, clang 은 에러 → 이식성 없음 (실측) | 자체 Taylor/recurrence |
| `crc32(reinterpret_cast<const uint8_t*>("abc"))` 를 컴파일타임에 | 상수식에 reinterpret_cast 불가 | `string_view` 오버로드 + `static_cast<uint8_t>` |
| 표 하나만 보고 "constexpr 이니 런타임 비용 0" | 표가 `.rodata` 에 있어도 **조회**는 런타임. "표 생성"만 공짜 | 조회 비용·flash 대기 상태까지 설명 |
| `.init_array` 있는데 자체 reset handler 작성 | `__libc_init_array` 를 안 부르면 동적 초기화가 안 돌아 표 전체가 0 (이 데모는 링크만 했고 실행하지 않음; CPP-B09) | constexpr 로 옮기거나 startup 에서 호출 |
| `CT_CHECK` 없이 그냥 `CHECK(crc32("abc") == ...)` | 런타임 평가일 뿐 상수 평가를 증명하지 못한다 | `constexpr bool` 변수나 `static_assert` 로 강제 |
| 표가 너무 커서 컴파일 실패 | clang 기본 step 한도 (`-fconstexpr-steps`, 실측: 32768 칸에서 초과) | 구간을 줄이거나 플래그로 한도 상향 |

`demo_big_table.cpp` 실측 (`target_check.sh big`, 이 맥, Apple clang 21): `sin_taylor` 13항 × N 칸 표는 기본 한도로 N=16384 까지는 통과, 32768 에서 `constexpr evaluation hit maximum step limit`. `-fconstexpr-steps=200000000` 으로 올리면 N=65536 도 컴파일되지만 시간이 N=1024: 0.28 s → 65536: 1.77 s 로 늘었다. GCC 의 한도 이름은 다르다(`-fconstexpr-ops-limit`, 실측 에러 메시지에서 `33554432`).

## 8. 면접 Q&A

<details>
<summary>Q1. constexpr 표가 .rodata 에 들어간다고 어떻게 증명합니까? "constexpr 이니까 들어간다"는 답은 안 됩니다.</summary>

`objdump -h` 로 `.rodata` 크기를 보고 `objdump -s -j .rodata` 로 **첫 엔트리 값(0x77073096)을 눈으로 확인**하며, `.init_array` 와 `.bss` 항목이 없는지 본다. 심볼은 `nm -S` 로 크기까지 본다. 그리고 비교군(동적 초기화, 실측 VARIANT 4: `.bss` 1540 B + `.init_array` + soft-float 16개 심볼)을 같이 보여 준다. 표준적으로는 [basic.start.static] 의 상수 초기화 + const 라서 `.rodata` 가 가능한 것이고, 실제 배치는 ABI/컴파일러의 일이다 — 그래서 측정한다.

</details>

<details>
<summary>Q2. constinit 과 constexpr 의 차이를, 펌웨어 예시로 설명하세요.</summary>

`constexpr`: 변수가 const 이고 초기화식이 상수식(컴파일타임 평가 필수). `constinit`: 초기화만 상수 초기화임을 보증(`[dcl.constinit]`), 변수 자체는 mutable. 실측: `constexpr` 은 `.rodata`(flash 1538 B / RAM 0), non-const `constinit` 은 `.data`(flash 1538 B + RAM 1538 B). 언제 `constinit` 을 쓰나: 부팅 시 캘리브레이션으로 **수정해야 하는** 기본값 표, SIOF(정적 초기화 순서 문제, CPP-B09) 없이 전역 드라이버 상태를 상수 초기화할 때. `constinit` 전역은 상수 초기화이므로 다른 TU 의 동적 초기화 순서를 신경 쓸 필요가 없다.

</details>

<details>
<summary>Q3. Q15 sin 표에서 32768 을 어떻게 처리했고, 왜 대칭성이 깨지지 않습니까?</summary>

`+1.0` 은 Q15 로 표현할 수 없어 32767 로 포화한다. 양의 피크가 음의 피크보다 1 LSB 작아지지만, 음수 쪽을 `-32767` 로 만들면(표는 양수만 저장하고 부호만 뒤집으므로 자동) 진폭이 대칭이 된다. `-32768` 을 쓰지 않는다는 것도 중요하다 — `-(-32768)` 은 `int16` 범위를 넘고 Q15 곱셈에서 `0x8000 * 0x8000` 의 포화 문제를 만든다. 비용은 피크에서 1 LSB 뿐이다 (harness 가 `sin_q15(0x4000)==32767`, `sin_q15(0xC000)==-32767` 을 고정).

</details>

<details>
<summary>Q4. 왜 std::sin 대신 Taylor 급수입니까? CORDIC 이나 recurrence 는요?</summary>

`std::sin` 은 C++20 표준에서 constexpr 이 아니다(C++26 P1383). GCC 가 통과시키는 것은 builtin 확장이고 clang 은 에러다. 빌드 때 한 번만 도는 코드라 속도는 무의미하고 **수렴 범위와 정확도**만 중요하다: 입력을 `[0, π/2]` 로 제한해 Taylor 13항이면 double 반올림 수준(실측 3.3e-16). Recurrence(`y[n+1]=2cos(h)·y[n]-y[n-1]`) 는 곱셈 하나로 끝나지만 256번 반복하면서 오차가 누적된다 — 런타임 NCO 에는 유리해도 표 생성에는 Taylor 가 낫다. CORDIC 은 런타임에 곱셈기 없는 환경에서 쓰는 알고리즘이다.

</details>

<details>
<summary>Q5. CRC 를 표로 만들었는데, 표 없이 비트 단위로 하는 것보다 항상 좋습니까?</summary>

아니다. 비트 단위는 flash 수십 바이트 + 바이트당 8회 루프, 표는 flash 1 KB + 바이트당 조회 1회. 256 B 를 쓸 여유도 없는 부트 ROM 이면 **4비트 니블 표(16 엔트리, 64 B)** 나 비트 단위가 맞다. 많은 MCU(STM32 등)에는 CRC 하드웨어가 있어 표 자체가 불필요할 수도 있다 — 단 다항식/반사(reflection) 설정이 맞는지 확인해야 한다. slicing-by-N 은 속도를 위해 표를 N배 더 쓴다(ALGO 트랙).

</details>

<details>
<summary>Q6. constexpr 로 바꾸면 조회가 더 빨라집니까?</summary>

아니다. 그 생각이 흔한 오해다. 달라지는 것은 **생성 시점**이지 조회 코드가 아니다. VARIANT 1 의 `crc32_run` 은 VARIANT 4 와 같은 `table[(c^b)&0xFF]^(c>>8)` 조회다. 오히려 flash 에서 읽으면 STM32 F4 같은 칩에서는 wait state 만큼 느릴 수 있고(ART 가속기·I-cache 영향), 정말 hot path 면 일부러 RAM/CCM/ITCM 에 복사해 두는 설계도 있다(그때는 `constinit` + section 지정). 사이클 수는 이 환경(맥, 실기 없음)에서 재지 않았으므로 숫자로 주장하지 않는다.

</details>

<details>
<summary>Q7. 컴파일타임 계산인데 부동소수점 결과가 런타임과 달라지지 않습니까?</summary>

표준상 보장되지 않는다([expr.const] 의 부동소수점 주석: 번역 시 평가와 실행 시 평가가 같을지는 미지정). 그래도 안전한 이유: 이 표는 **정수로 반올림된 값**이고, 잘림 오차(≈6e-26)와 반올림 오차(≈3e-16)가 0.5 LSB 경계보다 수십 자릿수 작다. 실제로 clang 과 GCC 가 만든 표의 바이트가 같았다. 반대로 부동소수 값을 그대로 저장하는 표(`constexpr float t[]`)라면 마지막 비트 차이가 생길 수 있어 그렇게 말할 수 없다. 또 생성 코드를 타깃 FPU 가 아니라 컴파일러가 실행하므로, `-mfloat-abi=soft` 타깃에서도 soft-float 코드가 이미지에 들어가지 않는다 (실측 VARIANT 1: 0개).

</details>

<details>
<summary>Q8. 이 표들을 왜 ISR 에서 안전하다고 말합니까?</summary>

상수 초기화 + 읽기 전용이므로 **락도, 초기화 플래그도, 지연 초기화도 없다**. C 의 `if (!init_done) crc32_init();` 패턴은 ISR 에서 호출되면 재진입·경쟁(flag 와 표 쓰기 사이에 다른 ISR)이 생기는데, 그 경우 자체가 사라진다. 대비되는 코드: 함수 안 `static const T t = make_rt();` 는 첫 호출에 동적 초기화 + 스레드 안전 static 의 guard 변수까지 들어온다(CPP-B09). flash 읽기는 사이클은 걸리지만 호출마다 동일하고 예측 가능하다.

</details>

## 9. 추가 연습

### 연습 1 — 다항식을 템플릿 인자로: CRC-32C

`make_reflected_table<Poly>()` 로 일반화하고 Castagnoli `0x82F63B78` 의 체크값 `0xE3069283` 을 `static_assert` 로 고정하라.

<details>
<summary>답 보기</summary>

다항식이 NTTP 이면 표는 다항식별로 하나씩 `.rodata` 에 생긴다 (템플릿 변수). 체크값은 CRC-32 = `0xCBF43926`, CRC-32C = `0xE3069283`, `reflected_table<POLY_CASTAGNOLI>[1] == 0xF26B8303`.

</details>

### 연습 2 — MSB-first CRC-16/CCITT-FALSE

`poly=0x1021, init=0xFFFF, reflect 없음` 으로 표를 만들어라. `crc16("123456789") == 0x29B1`.

<details>
<summary>답 보기</summary>

reflected 와 달리 `c = i << 8` 로 시작해 `c & 0x8000` 을 보고 왼쪽 시프트한다. 표 인덱스도 `(c >> 8) ^ byte`. 16비트로 잘라야 하니 `static_cast<uint16_t>` 를 빠뜨리면 -Wconversion 경고나 잘못된 값이 나온다.

</details>

### 연습 3 — Newton 법으로 Q15 sqrt 표

`std::sqrt` 없이 constexpr `sqrt_newton` 을 만들고 257 칸 Q15 sqrt 표를 만들어 호스트 `std::sqrt` 와 대조하라.

<details>
<summary>답 보기</summary>

`g = 0.5*(g + x/g)` 를 40 번 반복, 시작값 `x>=1?x:1`. 실측 오차 < 1e-14. 포화: `sqrt(1.0)*32768` 은 Part B 와 같은 이유로 32767 로. RMS·벡터 크기 계산의 LUT 후보.

</details>

### 연습 4 — 1/4 표에서 1024 칸 전체 표로

`quarter[]` 로부터 전체 주기 표를 컴파일타임에 전개하고 `sizeof` 로 flash 차이를 고정하라 (`514 B` vs `2048 B`). `cos` 는 90도 앞선 sin 조회.

<details>
<summary>답 보기</summary>

`quad = i >> 8`, `k = i & 0xFF`; `v = (quad&1) ? quarter[256-k] : quarter[k]`, `quad&2` 면 부호 반전. 사분면 처리 분기가 싫고 flash 가 넉넉하면(1.5 KB 더) 전체 표가 더 단순하고 빠르다는 트레이드오프.

</details>

### 연습 5 — 컴파일타임 slicing-by-4 표

`std::array<std::array<uint32_t,256>,4>` 를 만들어 바이트 단위 CRC 와 같은 결과인지 확인하라 (slicing 알고리즘 자체는 ALGO 트랙).

<details>
<summary>답 보기</summary>

`t[s][i] = (t[s-1][i] >> 8) ^ t[0][t[s-1][i] & 0xFF]`. `sizeof` = 4096 B (`static_assert` 로 고정). 길이 0/1/3/4/5/7/8/255/256/1023/1024 경계에서 바이트 방식과 일치해야 한다. 4 KiB 를 flash 에 쓰는 대가로 4 바이트당 조회 4회.

</details>

### 연습 6 — 수정 가능한 표는 constinit

공장 보정(`apply_factory_trim`)으로 값을 바꿔야 하는 게인 표를 어떻게 선언해야 하나? 왜 `constexpr` 로는 안 되나?

<details>
<summary>답 보기</summary>

`constinit std::array<uint16_t,8> g_gain_table = default_gains();` — `constinit` 은 상수 초기화를 보증(SIOF 없음)하면서 수정 가능하다. `constexpr` 은 const 라 쓸 수 없다. 대가: `.data` 로 flash + RAM 양쪽을 쓴다 (본문 VARIANT 2 실측 그대로).

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 18/18 passed
```

## 10. 핵심 정리

- `constexpr` 표 = 생성 코드는 빌드 머신에서 돌고, 결과만 `.rodata` 로. 실측: 동적 초기화 대비 flash 3799→1651 B, RAM 1540→0 B, soft-float 심볼 16→0개.
- `constexpr` = const + 상수 초기화(.rodata). `constinit` = 상수 초기화만 보증(const 없으면 .data). `const` + 동적 초기화 = .bss + .init_array + 코드.
- 비 constexpr 코드를 최적화기가 접어 주기도 하지만(clang 은 작은 경우, GCC 는 이번엔 안 접음) 보장 없음. 보증이 필요하면 키워드로 요구한다.
- 함수 안 표는 `static constexpr`, 헤더 표는 `inline constexpr` — 안 그러면 스택 1 KB 복사 / TU 마다 복사본 (둘 다 실측).
- `std::sin` 은 C++20 에서 constexpr 이 아니다. 자체 Taylor + `[0, π/2]` 로 제한, `std::sin` 과 런타임 대조.
- Q15: `+1.0` 은 표현 불가 → 포화. float→int 범위 밖은 UB, 상수 평가에서 잡힌다. 음수는 `-32767` 까지로.
- 1/4 표 + 보간: 256 구간 514 B 로 최대 오차 1.00 LSB. 표 크기를 정하는 것은 허용 오차.
- 조회는 여전히 런타임 — constexpr 은 조회 속도가 아니라 **초기화와 RAM** 을 없앤다.

## 참고자료

- [dcl.constexpr], [dcl.constinit], [basic.start.static], [expr.const], [conv.fpint], [expr.shift] — C++ 표준 초안: https://eel.is/c++draft/
- cppreference `constinit`: https://en.cppreference.com/w/cpp/language/constinit
- P1383 More constexpr for `<cmath>` and `<complex>` (C++26): https://wg21.link/P1383
- P2647 Permitting static constexpr variables in constexpr functions (C++23): https://wg21.link/P2647
- Ross Williams, A Painless Guide to CRC Error Detection Algorithms: http://www.ross.net/crc/download/crc_v3.txt
- CRC RevEng catalogue (체크값): https://reveng.sourceforge.io/crc-catalogue/
- ARM, *Run-time ABI for the Arm Architecture* (`__aeabi_*` 헬퍼): https://github.com/ARM-software/abi-aa
