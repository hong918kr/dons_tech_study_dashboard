---
id: CPP-B09
time: 50분
updated: 2026-11-07
---
# CPP-B09 정적 초기화: 전역 생성자와 .init_array

## 학습 목표

- 전역 객체의 초기화가 **정적 초기화(static initialization: 0 초기화 + 상수 초기화)** 와 **동적 초기화(dynamic initialization)** 중 어느 쪽인지 코드만 보고 판단한다.
- 두 번역 단위(TU)에서 **static initialization order fiasco(SIOF)** 를 재현하고, 링크 순서가 결과를 바꾸는 것을 직접 본다.
- `constexpr` 생성자 + `constinit` 으로 동적 초기화를 상수 초기화로 옮기고, `.init_array`(Mach-O `__mod_init_func`) 엔트리가 사라지는 것을 objdump로 확인한다.
- Cortex-M에서 `Reset_Handler → __libc_init_array → main` 순서와, 생성자가 **클럭·주변장치 초기화보다 먼저** 돈다는 사실의 함정을 설명한다.
- 함수 내부 static(Meyers singleton)의 guard 변수와 `__cxa_guard_acquire` 비용, `-fno-threadsafe-statics` 의 의미를 안다.

## 1. 문제

C에서는 전역 변수 초기값이 반드시 상수식이라 "main 전에 내 코드가 돈다"는 일이 없었다. C++ 전역 객체의 생성자는 **main 전에, 클럭 설정 전에, 정해지지 않은 순서로** 돈다 — 펌웨어가 부팅 직후 이상하게 죽는 고전적 원인이고, 면접 단골 꼬리질문("전역 생성자는 누가 호출하나요?")이다.

C++ 표준([basic.start.static])은 정적 저장 기간 변수의 초기화를 둘로 나눈다.

| 종류 | 언제 | 어디에 흔적이 남나 | 예 |
|---|---|---|---|
| 0 초기화 (zero-init) | 프로그램 시작 전 (이미지 자체) | `.bss` | `int g;` `Uart g_uart;`(동적 초기화 대기 중에도 일단 0) |
| 상수 초기화 (constant init) | 프로그램 시작 전 (이미지 자체) | `.data` (값이 그대로 박힘) | `constinit ClockConfig g{16'000'000, 1};` |
| 동적 초기화 | **main 전, 런타임에 코드로** | 생성자 thunk + `.init_array` 포인터 1개 | `Uart g_uart{115200};` (다른 전역을 읽는 생성자) |

표준은 "모든 정적 초기화는 어떤 동적 초기화보다도 먼저 일어난다"고 보장한다. 하지만 **다른 TU끼리의 동적 초기화 순서는 정하지 않는다** — 이것이 SIOF다.

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Cortex-M 부팅: 전역 생성자는 '클럭 설정 전에' 돈다</text>
  <line x1="20" y1="120" x2="725" y2="120" stroke="currentColor"/>
  <rect x="20" y="70" width="120" height="40" class="f-mut" stroke="currentColor"/>
  <text x="28" y="88" fill="currentColor">Reset_Handler</text><text x="28" y="104" fill="currentColor" font-size="12">SP 설정</text>
  <rect x="150" y="70" width="130" height="40" class="f-mut" stroke="currentColor"/>
  <text x="158" y="88" fill="currentColor">SystemInit()</text><text x="158" y="104" fill="currentColor" font-size="12">FPU, VTOR (HSI)</text>
  <rect x="290" y="70" width="140" height="40" class="f-ok" stroke="currentColor"/>
  <text x="298" y="88" fill="currentColor">.data 복사/.bss 0</text><text x="298" y="104" fill="currentColor" font-size="12">= 정적 초기화 완료</text>
  <rect x="440" y="70" width="140" height="40" class="f-warn" stroke="currentColor"/>
  <text x="448" y="88" fill="currentColor" font-family="monospace" font-size="12">__libc_init_array</text><text x="448" y="104" fill="currentColor" font-size="12">= 동적 초기화</text>
  <rect x="590" y="70" width="135" height="40" class="f-acc" stroke="currentColor"/>
  <text x="598" y="88" fill="currentColor">main()</text><text x="598" y="104" fill="currentColor" font-size="12">HAL_Init, 클럭 PLL</text>
  <g fill="currentColor" font-size="12">
    <text x="440" y="145">.init_array = [ a.o ctor, b.o ctor, ... ]</text>
    <text x="440" y="163">순서 = 링커가 이어 붙인 순서</text>
    <text x="290" y="145">constinit 객체는</text>
    <text x="290" y="163">여기서 이미 끝</text>
    <text x="598" y="145">SysTick, RCC 클럭</text>
    <text x="598" y="163">게이트는 이제야 켜짐</text>
  </g>
  <text x="10" y="200" fill="currentColor">생성자 안에서 HAL_Delay() → SysTick 미설정이라 영원히 대기. UART 레지스터 쓰기 → RCC 클럭 꺼져 있어 무시.</text>
  <text x="10" y="222" fill="currentColor">무거운 생성자 → 168MHz 가 아니라 리셋 클럭(HSI 16MHz)에서 돌아 부팅 시간이 길어진다.</text>
  <text x="10" y="244" fill="currentColor">해결 방향: 전역은 '상수 초기화'만 (constinit), 하드웨어 접근은 main 에서 명시적 init().</text>
</svg>
```

### Part A — 상수 초기화로 SIOF 없애기

`Uart g_uart{115200}` 의 생성자는 `g_clock.apb1_hz()` 를 읽어 BRR(= f_APB1 / baud)을 계산한다. 하네스에서는 한 파일 안에 `g_uart` 를 `g_clock` **보다 먼저** 정의해 "다른 TU의 g_clock이 아직 초기화 안 됨" 상황을 결정적으로 재현한다(같은 TU 안의 동적 초기화는 정의 순서대로 — [basic.start.dynamic]).

- TODO(A1): `ClockConfig` 생성자를 `constexpr` 로. 실행 횟수 카운터는 **런타임 실행일 때만** 올린다 (`std::is_constant_evaluated()`).
- TODO(A2): `g_clock` 에 `constinit` 을 붙인다. 그러면 g_clock은 이미지에 박힌 값이 되어 g_uart가 언제 읽든 8 MHz다.
- 검증: `ConstantConstructible<ClockConfig, ...>`, main 진입 시 ClockConfig 런타임 생성 0회, `g_uart.brr == 69`.

### Part B — 첫 사용 시 초기화 (construct on first use)

`SensorCal` 은 OTP(mock `volatile` 배열)를 읽어야 하므로 **constexpr가 될 수 없다**. 이럴 때의 고전적 해법이 함수 내부 static(Meyers singleton)이다.

- TODO(B1): `sensor_cal()` — 전역 `g_cal_global` 을 없애고 `static SensorCal inst;` 로. main 전에 `g_filter` 가 불러도 올바른 offset(37)을 받아야 한다.
- TODO(B2): `adc_lut()` — 256개 테이블을 **처음 쓸 때** 만든다. main 진입 시점에 생성 0회여야 한다(부팅 시간 절약).

### Part C — 부트 시퀀스 시뮬레이션

- TODO(C1): `run_init_arrays(pre_b, pre_e, b, e)` — newlib `__libc_init_array` 처럼 preinit 배열 전부 → init 배열 전부를 앞에서부터 호출.
- 주어진 `EagerUart` 는 생성자에서 USART 레지스터를 쓴다 → main 전이라 RCC 클럭 게이트가 꺼져 있어 쓰기가 사라진다(테스트가 이 버그를 재현).
- TODO(C2): `UartDriver` 생성자를 `constexpr` 로(설정만 저장), 전역에 `constinit`.
- TODO(C3): `init()` — main에서 `rcc_enable_usart2()` 뒤에 호출되면 BRR, CR1을 쓰고 `ready = true`.

## 2. Problem (English)

> **Static initialization on a microcontroller.**
>
> 1. `Uart g_uart{115200};` computes `brr = g_clock.apb1_hz() / baud` in its constructor, and `ClockConfig g_clock{16'000'000, 1};` lives in another translation unit. Explain why `g_uart.brr` may be 0, and show that changing the link order flips the result. Fix it so `g_clock` is guaranteed to be initialized before any dynamic initializer runs, and prove the guarantee at compile time.
> 2. `SensorCal` must read factory calibration from OTP at runtime. Provide `SensorCal& sensor_cal()` that is safe to call from another global's constructor, constructs exactly once, and costs nothing at boot if never called.
> 3. On a Cortex-M, who calls global constructors and when? Implement `void run_init_arrays(const InitFn* pre_b, const InitFn* pre_e, const InitFn* b, const InitFn* e)` with the same semantics as newlib's `__libc_init_array`. Then redesign a UART driver whose constructor touches hardware so that the global object needs no runtime constructor at all.
>
> Constraints: C++20, `-fno-exceptions -fno-rtti`, no heap. Expected: `g_uart.brr == 69` (8 MHz / 115200), zero runtime `ClockConfig` constructions before `main`, `adc_lut()` constructed lazily exactly once, `BRR == 69` and `CR1 == UE|TE` after `rcc_enable_usart2(); g_uart2.init();`.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o b09 && ./b09
# 처음엔 FAIL 10줄, "6/16 passed", exit 1 — TODO를 채워 16/16
```

관찰용 데모 파일(같은 폴더):

| 파일 | 무엇을 보나 | 실행 |
|---|---|---|
| `board.h` `a.cpp` `b.cpp` `main.cpp` `build.sh` | 두 TU SIOF, 링크 순서, `__mod_init_func`/`.init_array` 전후 비교 (FIX=0/1/2) | `sh build.sh` |
| `demo_guard.cpp` | Meyers singleton의 guard / `__cxa_guard_acquire` (arm64 + Thumb-2) | 파일 머리 주석의 명령 |
| `demo_constinit_error.cpp` | constinit이 동적 초기화를 컴파일 에러로 바꾸는 모습 | `-DSHOW_ERROR` 로 빌드 |

## 4. 힌트

<details>
<summary>힌트 1 — 왜 g_uart.brr 가 0 인가?</summary>

g_uart의 생성자가 도는 순간 g_clock은 아직 **0 초기화만** 된 상태다(sysclk_hz = 0). `0 >> 1 = 0`, `0 / 115200 = 0`. g_clock의 생성자가 "나중에" 돌기 때문이다.

</details>

<details>
<summary>힌트 2 — constexpr 생성자 안에서 카운터를 올리려면?</summary>

상수 평가 중에는 전역을 수정할 수 없다. C++20 `std::is_constant_evaluated()` 는 컴파일 타임 평가면 `true`. `if (!std::is_constant_evaluated()) ++counter;` 로 런타임 경로에서만 실행되게 한다.

</details>

<details>
<summary>힌트 3 — constexpr 만으로는 부족한가?</summary>

`constexpr` 생성자가 있으면 컴파일러는 **보통** 상수 초기화를 하지만, 인자가 런타임 값이면 조용히 동적 초기화로 떨어진다. `constinit` 은 "상수 초기화가 아니면 컴파일 에러"라는 **보증**이다([dcl.constinit]).

</details>

<details>
<summary>힌트 4 — 함수 내부 static</summary>

`static T inst;` 는 **처음 그 선언을 지날 때** 한 번 생성된다([stmt.dcl]). 누가 먼저 부르든(다른 전역 생성자라도) 그 시점에 만들어지므로 순서 문제가 없다.

</details>

<details>
<summary>힌트 5 — 하드웨어를 만지는 드라이버를 constinit 으로?</summary>

생성자는 **포인터·숫자 설정만 저장**하고(전역의 주소 `&g_clock` 은 상수식이다), 레지스터 쓰기는 `init()` 으로 분리한다. C에서 `uart_init(&cfg)` 를 main에서 부르던 방식과 같다.

</details>

## 5. 풀이

### 5.1 실제 두 TU 재현 — 링크 순서가 결과를 바꾼다

`board.h` 에 `ClockConfig`, `Uart` 를 선언하고 `a.cpp`(g_uart), `b.cpp`(g_clock), `main.cpp` 로 나눈다. 생성자는 부트 로그(`trace`)를 찍는다. `sh build.sh` 의 실제 출력(맥, Apple clang 21, arm64):

```text
================ FIX=0 ================
-- link order: a.o b.o main.o
  [pre-main] a: Uart ctor sees g_clock.sysclk_hz = 0
  [pre-main] b: ClockConfig ctor (runtime) 16000000
  main: g_clock.sysclk_hz=16000000  g_uart.brr()=0  (기대값 138)
  exit=1
-- link order: b.o a.o main.o
  [pre-main] b: ClockConfig ctor (runtime) 16000000
  [pre-main] a: Uart ctor sees g_clock.sysclk_hz = 16000000
  main: g_clock.sysclk_hz=16000000  g_uart.brr()=138  (기대값 138)
  exit=0
-- sections with init functions (Mach-O):
  a.o:   4 __mod_init_func 00000008 00000000000000f0 DATA
  b.o:   5 __mod_init_func 00000008 00000000000000b0 DATA
  final binary:   2 __init_offsets 00000008 0000000100000758 DATA
```

(데모는 APB1 분주 없이 16 MHz라 기대값 138, 하네스는 /2 라 69.) **같은 소스, 같은 플래그**인데 링크 명령의 파일 순서만 바꿨더니 결과가 갈렸다. 각 `.o` 에 `__mod_init_func` 가 8바이트(= 64비트 함수 포인터 1개)씩 있고, 링커는 이것들을 **입력 순서대로** 이어 붙인다. 최신 ld는 최종 바이너리에서 이를 32비트 오프셋 배열 `__init_offsets` 로 바꿔 담는다(8 = 엔트리 2개 × 4바이트). macOS에서는 dyld가 main 전에 이 배열을 순서대로 호출한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">.init_array(= __mod_init_func) 는 링크 순서대로 이어 붙여진다</text>
  <text x="10" y="58" fill="currentColor" font-family="monospace">ld a.o b.o</text>
  <rect x="150" y="40" width="150" height="28" class="f-warn" stroke="currentColor"/><text x="158" y="59" fill="currentColor" font-family="monospace" font-size="12">sub_I_a (g_uart)</text>
  <rect x="300" y="40" width="150" height="28" class="f-mut" stroke="currentColor"/><text x="308" y="59" fill="currentColor" font-family="monospace" font-size="12">sub_I_b (g_clock)</text>
  <text x="470" y="59" fill="currentColor">→ g_uart 가 0 을 읽음 ✗</text>
  <text x="10" y="108" fill="currentColor" font-family="monospace">ld b.o a.o</text>
  <rect x="150" y="90" width="150" height="28" class="f-mut" stroke="currentColor"/><text x="158" y="109" fill="currentColor" font-family="monospace" font-size="12">sub_I_b (g_clock)</text>
  <rect x="300" y="90" width="150" height="28" class="f-ok" stroke="currentColor"/><text x="308" y="109" fill="currentColor" font-family="monospace" font-size="12">sub_I_a (g_uart)</text>
  <text x="470" y="109" fill="currentColor">→ 우연히 정상 ✓</text>
  <text x="10" y="158" fill="currentColor" font-family="monospace">constinit</text>
  <rect x="150" y="140" width="150" height="28" class="f-ok" stroke="currentColor"/><text x="158" y="159" fill="currentColor" font-family="monospace" font-size="12">sub_I_a (g_uart)</text>
  <rect x="320" y="140" width="245" height="28" class="f-acc" stroke="currentColor"/><text x="328" y="159" fill="currentColor" font-family="monospace" font-size="12">.data: 00 24 f4 00 | 00 00 00 00</text>
  <text x="578" y="159" fill="currentColor">g_clock = 이미지 값</text>
  <text x="10" y="205" fill="currentColor">g_clock 의 초기화 코드가 아예 없어져 순서라는 개념 자체가 사라진다 → 어떤 링크 순서든 정상.</text>
  <text x="10" y="228" fill="currentColor">"우연히 정상"인 빌드가 제일 위험하다: 파일 하나 추가/Makefile 정렬만 바뀌어도 터진다.</text>
</svg>
```

### 5.2 고치기 — constexpr 생성자 + constinit (FIX=1, FIX=2)

```text
================ FIX=1 ================            (ClockConfig: constexpr ctor + constinit)
-- link order: a.o b.o main.o
  [pre-main] a: Uart ctor sees g_clock.sysclk_hz = 16000000
  main: g_clock.sysclk_hz=16000000  g_uart.brr()=138  (기대값 138)
-- link order: b.o a.o main.o   ... 동일 (138)
-- sections with init functions (Mach-O):
  a.o:   4 __mod_init_func 00000008 00000000000000f0 DATA
  b.o: (no __mod_init_func)
  final binary:   2 __init_offsets 00000004 00000001000006d4 DATA
================ FIX=2 ================            (Uart 도 constexpr ctor, 설정만 저장 + constinit)
  main: g_clock.sysclk_hz=16000000  g_uart.brr()=138  (기대값 138)   ← 두 순서 모두
  a.o: (no __mod_init_func)
  b.o: (no __mod_init_func)
  final binary: (no __mod_init_func / __init_offsets)
```

- FIX=1: `b.o` 의 `__mod_init_func` 가 **사라졌고**, "b: ClockConfig ctor (runtime)" 로그도 안 찍힌다(`is_constant_evaluated()` 가 true였다). g_uart는 여전히 동적이지만, 읽는 대상이 정적 초기화돼 있으니 순서와 무관하게 정상.
- FIX=2: g_uart까지 `constinit` → 최종 바이너리에 초기화 함수 배열이 **0개**. 그 대가로 `brr()` 은 값이 아니라 쓸 때 계산하는 함수가 됐다(= Part C의 `UartDriver::init()` 패턴).
- `nm`/`objdump -t b.o` 로도 보인다: FIX=0 은 `_g_clock` 이 `__DATA,__common`(0 초기화, `.bss` 격) + `s __GLOBAL__sub_I_b.cpp` 가 있고 `__mod_init_func` 재배치가 `ARM64_RELOC_UNSIGNED __GLOBAL__sub_I_b.cpp` 를 가리킨다. FIX=1 은 `D _g_clock`(`__data`) 하나뿐이고 `__data` 내용이 `0024f400 00000000` (= 16 000 000, 0 리틀 엔디언).

**ELF / Cortex-M4 (Thumb-2)** — 같은 `b.cpp` 를 `clang++ --target=armv7em-none-eabi -mthumb -mcpu=cortex-m4 -ffreestanding -c` 로 컴파일(링크는 sysroot가 없어 생략)한 실제 출력:

```text
-- b.cpp FIX=0 (ELF sections)
  7 .bss            00000008 00000000 BSS          ← g_clock 은 0 으로 시작
  8 .init_array     00000004 00000000 DATA         ← 32비트 함수 포인터 1개
RELOCATION RECORDS FOR [.init_array]:
00000000 R_ARM_TARGET1            _GLOBAL__sub_I_b.cpp
00000018 <_GLOBAL__sub_I_b.cpp>:
      1c:  movw r1, #0x2400 / 28: movt r1, #0xf4    ← 0x00f42400 = 16000000
      32:  str  r1, [r2]                              ← g_clock.sysclk_hz = 16000000
      34:  str  r3, [r2, #0x4]                        ← g_clock.apb1_shift = 0
      36:  b.w  _Z5tracePKcj                          ← 부트 로그 (tail call)
-- b.cpp FIX=1 (ELF sections)
  2 .text           00000000 00000000 TEXT         ← 코드 0 바이트
  3 .data           00000008 00000000 DATA
 0000 0024f400 00000000                             ← .data 에 값이 박혀 있다
```

`.init_array` 는 Mach-O `__mod_init_func` 와 같은 역할이다. GNU ld(arm-none-eabi)도 입력 섹션을 링크 순서대로 모으므로 같은 SIOF가 난다고 **예상**한다(이 맥에서는 arm-none-eabi 링크를 돌리지 못했다).

| | C | C++ 동적 초기화 | C++ `constinit` |
|---|---|---|---|
| 전역 초기값 | 상수식만 허용 (C11 6.7.9p4) | 아무 식/생성자 | 상수식 + constexpr 생성자 |
| 런타임 코드 | 없음 | thunk + `.init_array` 엔트리 | 없음 |
| 저장 위치 | `.data`/`.bss` (C-B11 참고) | `.bss`(0) → 생성자가 채움 | `.data`/`.bss` |
| TU 간 순서 문제 | 없음 | **있음 (SIOF)** | 없음 |

### 5.3 Part A 정리 — 왜 constinit까지 붙이나

- `constexpr ClockConfig(...)` 만 있으면, `ClockConfig g_clock{16'000'000, 1};` 는 인자가 상수라 **상수 초기화된다**(constant-initialized, [basic.start.static]/2). 하지만 누가 `ClockConfig g_clock{read_strap(), 1};` 로 바꾸면 경고 없이 동적 초기화로 돌아가 SIOF가 부활한다.
- `constinit` 은 C++20의 "상수 초기화가 아니면 ill-formed" 지정자. 컴파일러 최적화가 우연히 정적으로 처리해 주는 경우([basic.start.static]/3의 early initialization)도 인정하지 않는다. 실제 에러(`demo_constinit_error.cpp -DSHOW_ERROR`):

```text
demo_constinit_error.cpp:20:23: error: variable does not have a constant initializer
   20 | constinit LegacyClock g_bad1{16'000'000};          // ① constexpr 아닌 생성자 호출
note: non-constexpr constructor 'LegacyClock' cannot be used in a constant expression
demo_constinit_error.cpp:21:23: error: variable does not have a constant initializer
   21 | constinit uint32_t    g_bad2 = g_otp_word;         // ② volatile(런타임) 값 읽기
note: read of volatile-qualified type 'volatile uint32_t' (aka 'volatile unsigned int') is not allowed in a constant expression
```

- 하네스는 같은 보증을 `concept ConstantConstructible`(템플릿 인자 자리에 `T{Args...}` 를 넣어 상수식인지 묻기)로 **런타임 CHECK** 로도 보여준다 — starter에선 false라 FAIL.
- 참고: starter의 g_clock은 생성자가 다른 전역(`g_clock_ctor_runs`)을 바꾸므로 컴파일러가 early initialization을 할 수도 없다 → FAIL이 확정적이다.

### 5.4 Part B — 함수 내부 static과 guard 비용

```svg
<svg viewBox="0 0 740 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">sensor_cal() 호출마다 일어나는 일 (thread-safe statics)</text>
  <rect x="20" y="45" width="190" height="34" class="f-mut" stroke="currentColor"/>
  <text x="30" y="67" fill="currentColor">guard 바이트 읽기 (acquire)</text>
  <path d="M210 62 L260 62" stroke="currentColor" fill="none"/>
  <rect x="260" y="45" width="150" height="34" class="f-ok" stroke="currentColor"/>
  <text x="270" y="67" fill="currentColor">bit0==1 → 주소 반환</text>
  <text x="420" y="67" fill="currentColor">빠른 경로: 로드 1 + 분기 1 (+ 배리어)</text>
  <path d="M115 79 L115 115" stroke="currentColor" fill="none"/>
  <text x="122" y="102" fill="currentColor" font-size="12">0 이면 (첫 호출)</text>
  <rect x="20" y="115" width="200" height="34" class="f-warn" stroke="currentColor"/>
  <text x="30" y="137" fill="currentColor" font-family="monospace" font-size="12">__cxa_guard_acquire(&amp;g)</text>
  <path d="M220 132 L260 132" stroke="currentColor" fill="none"/>
  <rect x="260" y="115" width="130" height="34" class="f-acc" stroke="currentColor"/>
  <text x="270" y="137" fill="currentColor">SensorCal() 실행</text>
  <path d="M390 132 L430 132" stroke="currentColor" fill="none"/>
  <rect x="430" y="115" width="200" height="34" class="f-warn" stroke="currentColor"/>
  <text x="440" y="137" fill="currentColor" font-family="monospace" font-size="12">__cxa_guard_release(&amp;g)</text>
  <text x="10" y="185" fill="currentColor">-fno-threadsafe-statics: "guard 확인 → 생성 → guard=1" 만 남는다 (락·배리어 없음).</text>
  <text x="10" y="207" fill="currentColor">constexpr 생성자인 local static: 상수 초기화 → guard 자체가 없다 (그냥 .data 주소 반환).</text>
  <text x="10" y="229" fill="currentColor">초기화 '도중' 같은 선언에 다시 들어오면(ISR 재진입 포함) 표준상 UB — 런타임은 보통 abort/데드락.</text>
</svg>
```

`demo_guard.cpp` 를 Cortex-M4 Thumb-2로 컴파일한 실제 출력(요약):

```text
=== thumb2 (기본)                                   === thumb2 -fno-threadsafe-statics
<_Z10sensor_calv>:                                   <_Z10sensor_calv>:
   ldrb  r0, [r0, #0x4]      ← guard                    ldrb  r0, [r4]      ← guard
   dmb   sy                  ← acquire 배리어           cbz   r0, 첫호출
   lsls  r0, r0, #0x1f       ← bit0 검사                adds  r0, r4, #0x4  ← 주소 반환
   beq   첫호출 (.cold)                                 pop   {r4, pc}
<_Z10sensor_calv.cold.1>:                            <_Z10sensor_calv.cold.1>:
   bl    __cxa_guard_acquire                            bl    _ZN9SensorCalC1Ev
   cbz   r0, 끝                                         movs  r0, #0x1
   bl    _ZN9SensorCalC1Ev                              strb  r0, [r4]      ← guard = 1
   b.w   __cxa_guard_release
<_Z10board_infov>:  (constexpr 생성자 local static, 두 빌드 동일)
   movw/movt r0, _ZZ10board_infovE4inst ; bx lr      ← guard 없음
```

맥 arm64에서도 같은 구조(`ldaprb` 로 acquire 로드 → `___cxa_guard_acquire`/`___cxa_guard_release`; `-fno-threadsafe-statics` 면 평범한 `ldrb`)를 확인했다. ARM C++ ABI의 guard는 32비트 워드이고 **bit 0** 만 "초기화 완료"를 뜻해서 `lsls #31` 로 검사한다.

| 방식 | 매 호출 비용 | 첫 호출 | 링크되는 런타임 | 주의 |
|---|---|---|---|---|
| `constinit` 전역 | 0 | — | 없음 | 값이 컴파일 타임에 정해져야 함 |
| local static + constexpr 생성자 | 0 (주소만) | — | 없음 | 위와 동일 |
| local static (기본) | 로드+배리어+분기 | `__cxa_guard_*` | libsupc++/libc++abi guard 코드 | RTOS면 내부 락 구현 필요 |
| local static + `-fno-threadsafe-statics` | 로드+분기 | 생성자만 | 없음 | 두 태스크/ISR 동시 첫 호출 시 이중 생성 |

펌웨어 결론: Meyers singleton은 "런타임 값이 필요하고 순서를 모르는" 경우의 도구다. 가능하면 constinit, 아니면 **main에서 클럭 후 명시적으로 한 번** 불러 두고(인터럽트 켜기 전에) 이후엔 빠른 경로만 타게 한다. `__cxa_guard_*` 자체의 의존성 추적은 CPP-A03 참고.

### 5.5 Part C — 누가 생성자를 부르나 (Cortex-M)

STM32Cube GCC 스타트업(`startup_stm32f4xx.s`)의 `Reset_Handler` 는 대략 `ldr sp, =_estack → bl SystemInit → .data 복사 / .bss 0 채우기 → bl __libc_init_array → bl main` 이다(Cube 버전에 따라 SystemInit이 복사 앞/뒤). .data 복사·.bss 제로화 자체는 BM-B07, SystemInit 순서는 BM-A01 참고. 링커 스크립트가 배열의 시작/끝 심볼을 만든다:

```text
.init_array :
{
  PROVIDE_HIDDEN (__init_array_start = .);
  KEEP (*(SORT(.init_array.*)))     /* init_priority(N) → .init_array.N, 번호순 */
  KEEP (*(.init_array*))
  PROVIDE_HIDDEN (__init_array_end = .);
} >FLASH
```

newlib의 `__libc_init_array()` 는 `__preinit_array_start..end` 를 차례로 호출 → (`_init()`) → `__init_array_start..end` 를 차례로 호출한다. Part C의 `run_init_arrays` 가 바로 그 두 루프다:

```cpp
for (const InitFn* f = pre_b; f != pre_e; ++f) (*f)();
for (const InitFn* f = b; f != e; ++f) (*f)();
```

`KEEP` 이 없으면 `--gc-sections` 가 "아무도 참조 안 하는" `.init_array` 를 버려 생성자가 조용히 사라진다. 반대로 커스텀 스타트업에서 `__libc_init_array` 호출을 빼먹으면 **모든 전역 객체가 0인 채로** main에 들어간다 — 전역이 전부 constinit이면 이 호출 자체가 필요 없다.

EagerUart vs UartDriver:

| | EagerUart (생성자에서 레지스터 쓰기) | UartDriver (constinit + init()) |
|---|---|---|
| 실행 시점 | `__libc_init_array` 안, 클럭 게이트 OFF | main에서 `rcc_enable_usart2()` 뒤 |
| 결과 | 쓰기 무시, BRR=0 (하네스 `brr_at_main == 0`) | BRR=69, CR1=UE\|TE |
| `.init_array` | 엔트리 있음 | 없음 |
| 오류 처리 | 생성자는 반환값이 없고 예외도 꺼짐 → 실패를 알릴 길 없음 | `init()` 이 에러 코드 반환 가능 (CPP-M11 참고) |

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o b09 && ./b09   # 16/16 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-exceptions -fno-rtti solution.cpp -o b09a && ./b09a   # 16/16, 경고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 전역 생성자에서 다른 TU 전역을 읽음 | TU 간 동적 초기화 순서는 미지정 → 링크 순서에 따라 0을 읽음 | 읽히는 쪽을 `constinit`, 또는 `get()` 함수 + local static |
| "constexpr 생성자 있으니 됐다" | 인자가 런타임 값이면 조용히 동적 초기화로 떨어짐 | 전역에 `constinit` 을 붙여 컴파일러가 확인하게 |
| `constinit` 을 `const` 로 오해 | constinit은 초기화 시점만 보증, 값은 수정 가능 | 읽기 전용이면 `constexpr`(= const + 상수 초기화) |
| 전역 생성자에서 HAL_Delay / 레지스터 쓰기 | main 전 = SysTick·RCC 미설정 → 무한 대기 / 쓰기 무시 | 생성자는 설정 저장만, `init()` 은 main에서 |
| 커스텀 Reset_Handler에서 `__libc_init_array` 누락 | 생성자가 안 돌아 객체가 전부 0 | 호출 추가, 또는 모든 전역을 constinit으로 |
| 링커 스크립트에 `KEEP(*(.init_array*))` 없음 | `--gc-sections` 가 엔트리를 제거 | `KEEP` 추가 |
| ISR에서 Meyers singleton 첫 호출 | 메인이 초기화 도중이면 재진입 → UB (abort/데드락/이중 생성) | 인터럽트 켜기 전에 main에서 한 번 미리 호출 |
| 전역 객체에 사용자 정의 소멸자 | `__cxa_atexit` 등록 코드 + 런타임이 링크됨, 펌웨어에선 영원히 안 불림 | 소멸자를 trivial하게 (연습 5) |

## 8. 면접 Q&A

<details>
<summary>Q1. Static initialization order fiasco가 뭔가요? 해결책을 여러 개 말해보세요.</summary>

서로 다른 TU의 전역 객체가 동적 초기화될 때 표준은 순서를 정하지 않는다. A의 생성자가 B를 쓰는데 B가 아직 0 상태면 버그. 해결: ① B를 `constexpr` 생성자 + `constinit` 으로 상수 초기화 (가장 좋음, 비용 0) ② construct-on-first-use: `B& b() { static B x; return x; }` (guard 비용) ③ 전역 생성자를 없애고 main에서 순서대로 `init()` 호출 (C 방식, 펌웨어 표준) ④ Nifty/Schwarz counter (`std::cout` 이 쓰는 기법) ⑤ GCC `init_priority` (비표준). 꼬리질문 "같은 TU 안이면?" → 정의 순서대로 보장된다([basic.start.dynamic]).

</details>

<details>
<summary>Q2. Cortex-M에서 전역 생성자는 누가, 언제 호출하나요? 안 불리는 경우는?</summary>

컴파일러가 TU마다 `_GLOBAL__sub_I_<file>` thunk를 만들고 그 주소를 `.init_array` 에 넣는다. 링커가 이를 모아 `__init_array_start/end` 를 정의하고, 스타트업의 `Reset_Handler` 가 .data/.bss 준비 후 newlib `__libc_init_array()` 를 불러 배열을 순서대로 실행, 그다음 `main`. 안 불리는 경우: 커스텀 스타트업이 `__libc_init_array` 를 빼먹음, `-nostartfiles` 로 crt를 빼고 직접 호출 안 함, 링커 스크립트에 `KEEP` 없이 `--gc-sections`. 호스트는 macOS dyld(`__mod_init_func`/`__init_offsets`), Linux glibc 동적 로더/`__libc_start_main` 이 같은 역할.

</details>

<details>
<summary>Q3. const, constexpr, constinit 차이는?</summary>

`const`: 수정 불가, 초기화 시점은 무관(동적일 수도). `constexpr` 변수: const + 반드시 상수 초기화 + 상수식에서 값 사용 가능. `constinit`(C++20): 상수 초기화만 보증, **수정 가능**, 정적/스레드 저장 기간 변수에만. 펌웨어 매핑: 레지스터 주소·테이블 = `constexpr`(.rodata, CPP-B04/CPP-M13 참고), 드라이버 상태 객체·카운터 = `constinit`(.data/.bss). `extern constinit` 선언을 헤더에 두면 다른 TU에서도 "이건 이미 초기화됨"을 알 수 있다.

</details>

<details>
<summary>Q4. Meyers singleton은 thread-safe한가요? 펌웨어에서 비용과 위험은?</summary>

C++11부터 표준이 "동시에 들어오면 기다린다"를 보장 → 컴파일러가 guard 변수 + `__cxa_guard_acquire/release` 를 삽입. 매 호출: guard 로드 + (ARM) `dmb` + 분기. 첫 호출: 런타임 함수 호출, RTOS에선 내부 락. `-fno-threadsafe-statics` 면 락 없이 guard 바이트만(단일 스레드 가정). 위험: ISR이 초기화 도중 같은 함수에 들어오면 표준상 UB(재귀 진입) — libc++abi/libsupc++는 보통 abort/terminate, 락 기반 구현은 데드락. 대책: 인터럽트 켜기 전에 미리 호출, 또는 constinit.

</details>

<details>
<summary>Q5. 전역 객체의 소멸자는 펌웨어에서 어떻게 되나요?</summary>

non-trivial 소멸자를 가진 전역은 생성 직후 `__cxa_atexit(dtor, &obj, &__dso_handle)` 로 등록된다. 실제 Thumb-2 출력: `_GLOBAL__sub_I_dtor.cpp` 안에 `_ZN6LoggerC1Ev` 호출 뒤 `__cxa_atexit` 로 tail call (clang, armv7em-none-eabi; GCC arm-none-eabi는 `__aeabi_atexit` 를 쓰는 것으로 알고 있다 — 미실행). main은 반환하지 않으니 소멸자는 영원히 안 불리는데 등록 테이블·코드만 먹는다. 대책: 전역 타입을 trivially destructible하게, clang은 `-fno-c++-static-destructors` (확인: 이 플래그로 `__cxa_atexit` 재배치가 사라짐), 또는 `__dso_handle`/`__cxa_atexit` 를 빈 스텁으로.

</details>

<details>
<summary>Q6. C에서는 왜 이 문제가 없었나요? __attribute__((constructor))는?</summary>

C11 6.7.9p4: 정적 저장 기간 객체의 초기화식은 상수식이어야 한다 → 모두 .data/.bss, 런타임 코드 0. 순서가 필요한 초기화는 main에서 명시적으로 호출했다. GCC 확장 `__attribute__((constructor(prio)))` 는 C에서도 `.init_array` 엔트리를 만들므로 같은 문제가 생길 수 있다. C++ `constinit` + 명시적 `init()` 은 C의 이 규칙을 타입 시스템으로 되찾는 것이다.

</details>

<details>
<summary>Q7. init_priority로 순서를 정하면 안 되나요?</summary>

GCC/Clang 확장 `__attribute__((init_priority(N)))`(101~65535, 작을수록 먼저)은 `.init_array.N` 섹션으로 들어가고 링커 스크립트의 `SORT(.init_array.*)` 로 정렬된다(실제 ELF 출력에서 `.init_array.101` 섹션 확인). 동작은 하지만 비표준이고, 우선순위 숫자가 전역적 결합을 만들며, 클럭 전이라는 근본 문제는 그대로다. 면접 답: "가능하지만 최후 수단, constinit/명시적 init 우선".

</details>

## 9. 추가 연습

### 연습 1 — constinit 은 const 가 아니다

SysTick ISR이 증가시키는 `g_tick_count` 를 상수 초기화로 선언하되 수정 가능하게 하라. `constexpr` 상수와의 차이를 `static_assert` 로 보여라.

<details>
<summary>답 보기</summary>

`constinit uint32_t g_tick_count = 0;` → `!std::is_const_v<decltype(g_tick_count)>`. `constexpr uint32_t kTickHz = 1000;` → `std::is_const_v<...>` true. (ISR과 공유하는 카운터의 원자성은 C-M14/CPP-A04 참고.)

</details>

### 연습 2 — guard 없는 함수 내부 static

`board_info()` 가 local static을 돌려주되 guard·런타임 생성자가 전혀 없게 하라. 런타임 생성 횟수 0을 증명하라.

<details>
<summary>답 보기</summary>

`BoardInfo` 에 constexpr 생성자를 주고 `static BoardInfo inst{3};`. 상수 초기화되므로 [stmt.dcl]의 "첫 통과 시 동적 초기화"가 일어나지 않는다. `demo_guard.cpp` 의 `_Z10board_infov` 가 `movw/movt + bx lr` 뿐인 것이 증거. 하네스: `is_constant_evaluated` 카운터가 호출 후에도 0, 지역 객체를 만들면 1.

</details>

### 연습 3 — 같은 TU 안의 순서 예측

`Tag g_tag_c{'C'}; Tag g_tag_a{'A'}; Tag g_tag_b{'B'};` 가 로그 버퍼에 남기는 순서는? 로그 버퍼 자체는 왜 안전한가?

<details>
<summary>답 보기</summary>

"CAB" — 같은 TU의 ordered 동적 초기화는 **정의 순서**(이름·선언 순서와 무관). 로그 버퍼 `static char g_log[8]; static int g_log_n;` 은 0 초기화(정적 초기화)라 어떤 생성자보다 먼저 준비되어 있다 — 부트 로그 버퍼를 만들 때 꼭 지킬 조건.

</details>

### 연습 4 — 2단계 초기화: constinit std::optional

런타임 값(OTP)이 필요한 `SensorCal` 을 전역으로 두되 `.init_array` 엔트리 없이, main의 `board_init()` 에서 정확히 한 번 생성하라.

<details>
<summary>답 보기</summary>

`constinit std::optional<SensorCal> g_cal;` — 비어 있는 optional의 기본 생성자는 constexpr이라 상수 초기화된다. `board_init()` 에서 `if (!g_cal) g_cal.emplace();`. SensorCal이 trivially destructible이면 optional도 trivially destructible → atexit 등록도 없다(`static_assert`). 힙 없이 "언제 만들지"를 코드가 정한다. placement new로 직접 만드는 버전은 CPP-M07 참고.

</details>

### 연습 5 — 전역은 trivially destructible 하게

`struct StatsWithDtor { uint32_t n; ~StatsWithDtor() {} };` 는 왜 전역으로 두면 손해인가? 고쳐라.

<details>
<summary>답 보기</summary>

빈 본문이라도 사용자 제공 소멸자는 non-trivial → 전역 정의 시 `__cxa_atexit` 등록이 생긴다(Q5). 소멸자 선언을 지우면 `std::is_trivially_destructible_v<Stats>` 가 true. `= default` 도 trivial 조건(멤버·베이스가 trivial)을 만족하면 trivial이다.

</details>

### 연습 6 — 손으로 만든 init-once와 ISR 재진입

`-fno-threadsafe-statics` 가 만드는 것과 비슷한 init-once를 상태 3개(Uninit/Busy/Done)로 구현하고, 생성 도중 ISR이 들어와도 이중 생성·데드락 없이 "아직 준비 안 됨(nullptr)"을 돌려주게 하라.

<details>
<summary>답 보기</summary>

`switch(state)`: Done → 객체 주소, Busy → `nullptr`(ISR은 이번 샘플을 건너뛴다), Uninit → Busy로 바꾸고 `emplace()` 후 Done. 컴파일러 guard는 "기다리거나 abort" 둘 중 하나라 ISR 문맥에 맞지 않는다 — 그래서 ISR이 쓰는 객체는 constinit 또는 인터럽트 enable 전 초기화가 원칙. 실제 멀티코어/RTOS라면 상태 변수는 `std::atomic` 과 acquire/release가 필요(CPP-A04).

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o b09p && ./b09p   # 11/11 passed
```

## 10. 핵심 정리

- 정적 초기화(0 + 상수) = 이미지에 박힌 값, 코드 0. 동적 초기화 = 생성자 thunk + `.init_array`(Mach-O `__mod_init_func`) 엔트리.
- 모든 정적 초기화는 동적 초기화보다 먼저. 같은 TU의 동적 초기화는 정의 순서, **TU 간 순서는 미지정** → SIOF, 링크 순서로 결과가 바뀐다.
- `constexpr` 생성자 + `constinit` 전역 = 상수 초기화를 컴파일러가 **보증**. `constinit` ≠ `const`.
- Cortex-M: `Reset_Handler → (.data/.bss) → __libc_init_array → main`. 생성자는 클럭·SysTick 설정 **전**에 리셋 클럭으로 돈다.
- 펌웨어 패턴: 생성자는 설정 저장만(constexpr), 하드웨어는 main에서 `init()`.
- 함수 내부 static = 첫 사용 시 생성 + guard. 기본은 `__cxa_guard_acquire/release` + 배리어, `-fno-threadsafe-statics` 면 바이트 검사만. constexpr 생성자면 guard 없음.
- ISR에서 local static 첫 호출 금지(재진입 UB). 전역 타입은 trivially destructible하게(`__cxa_atexit` 회피).

## 참고자료

- C++20 표준 [basic.start.static], [basic.start.dynamic], [stmt.dcl], [dcl.constinit] — <https://eel.is/c++draft/basic.start.static>
- cppreference — [Initialization order / SIOF](https://en.cppreference.com/w/cpp/language/siof), [constinit](https://en.cppreference.com/w/cpp/language/constinit), [std::is_constant_evaluated](https://en.cppreference.com/w/cpp/types/is_constant_evaluated)
- isocpp FAQ — [What's the "static initialization order fiasco"?](https://isocpp.org/wiki/faq/ctors#static-init-order)
- ARM — *C++ ABI for the Arm Architecture* (CPPABI32), guard variable / `__aeabi_atexit` 절 — <https://github.com/ARM-software/abi-aa>
- newlib `libc/misc/init.c` — `__libc_init_array` 구현
- STM32CubeF4 `startup_stm32f4xx.s`, `STM32F4xx_FLASH.ld` — Reset_Handler와 `.init_array` 링커 섹션
- GCC 매뉴얼 — `-fno-threadsafe-statics`, `init_priority` 속성; Clang 매뉴얼 — `-fno-c++-static-destructors`
