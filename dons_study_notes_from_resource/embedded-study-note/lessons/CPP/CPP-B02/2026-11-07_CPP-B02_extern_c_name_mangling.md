---
id: CPP-B02
time: 50분
updated: 2026-11-07
---
# CPP-B02 extern "C"와 name mangling: C HAL과 링크

## 학습 목표

- C++ 컴파일러가 함수 이름을 **맹글링(name mangling)** 하는 이유와 Itanium C++ ABI 규칙(`_Z`, `N…E`, `j`/`m`/`PKh`)을 손으로 풀 수 있다.
- `#ifdef __cplusplus extern "C" {` 가드가 없을 때 나는 **undefined symbol 링크 에러**를 재현하고, `nm` / `c++filt` 로 원인을 30초 안에 짚는다.
- Mach-O(macOS)의 **추가 밑줄(`__Z…`)** 과 ELF(arm-none-eabi)의 이름 차이, 그리고 `uint32_t` 가 타깃마다 다르게 맹글링되는 이유를 설명한다.
- `extern "C"` 를 빼먹은 ISR이 **링크 에러 없이** `Default_Handler` 로 빠지는 조용한 버그를 이해하고 잡아내는 린트를 짠다.
- C HAL 콜백(함수 포인터 + `void* ctx`)에 C++ 객체를 **트램펄린(trampoline)** 으로 연결한다.

## 1. 문제

펌웨어 C++ 프로젝트는 거의 100% **벤더 C 코드(STM32 HAL, nRF SDK, CMSIS, FreeRTOS)** 위에 올라간다 — C와 C++ 사이의 "이름 규칙"을 모르면 링크 에러에서 하루를 날리고, ISR 이름 하나 때문에 보드가 `Default_Handler` 에서 멈춘다. 면접에서도 "C++에서 C 라이브러리를 어떻게 부르나요? `extern "C"` 가 실제로 뭘 하나요?" 는 단골 질문이다.

C 컴파일러는 `void hal_gpio_write(uint32_t, bool)` 를 오브젝트 파일에 **`hal_gpio_write`** 라는 이름으로 넣는다. C++ 컴파일러는 같은 선언을 오버로드·네임스페이스를 구분하려고 **`_Z14hal_gpio_writejb`** 로 바꾼다. 링커는 타입을 모르고 **문자열만 비교**하므로 두 이름이 다르면 "없다"고 한다. `extern "C"` 는 "이 선언은 C 규칙(맹글링 없음)으로 이름을 만들어라" 라는 **언어 링키지(language linkage)** 지정이다.

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">링커는 이름(문자열)만 맞춘다 — extern "C" 유무에 따른 차이</text>
  <rect x="10" y="40" width="160" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor">hal.c  → cc</text>
  <text x="22" y="84" fill="currentColor" font-family="monospace" font-size="12">T hal_gpio_write</text>
  <rect x="10" y="160" width="160" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="182" fill="currentColor">app.cpp → c++</text>
  <text x="22" y="204" fill="currentColor">#include "hal.h"</text>
  <rect x="250" y="40" width="230" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="262" y="62" fill="currentColor">가드 있음 (extern "C")</text>
  <text x="262" y="84" fill="currentColor" font-family="monospace" font-size="12">U hal_gpio_write</text>
  <rect x="250" y="160" width="230" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="262" y="182" fill="currentColor">가드 없음 (C++ 링키지)</text>
  <text x="262" y="204" fill="currentColor" font-family="monospace" font-size="12">U _Z14hal_gpio_writejb</text>
  <path d="M170 188 L245 70" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <path d="M170 188 L245 188" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <rect x="560" y="40" width="170" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="572" y="62" fill="currentColor">링커: 이름 일치</text>
  <text x="572" y="84" fill="currentColor">→ 링크 성공</text>
  <rect x="560" y="160" width="170" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="572" y="182" fill="currentColor">링커: 정의 없음</text>
  <text x="572" y="204" fill="currentColor">→ Undefined symbols</text>
  <path d="M480 68 L555 68" stroke="currentColor" fill="none"/>
  <path d="M480 188 L555 188" stroke="currentColor" fill="none"/>
  <text x="10" y="248" fill="currentColor">ISR 은 반대 방향: startup 은 "USART1_IRQHandler" 를 찾는데</text>
  <text x="10" y="270" fill="currentColor">C++ 정의가 _Z17USART1_IRQHandlerv 이면 아무 에러 없이 weak 기본값(Default_Handler)이 이긴다.</text>
</svg>
```

이 레슨은 두 층으로 진행한다. **(1) 관찰** — 레슨 폴더의 `hal.h`/`hal.c`/`startup.c`/`app.cpp` 를 `build.sh` 로 섞어 링크하며 링크 에러와 조용한 ISR 버그를 직접 본다. **(2) 코딩** — `starter.cpp` 에서 맹글링 규칙·nm 파싱·벡터 테이블 링크를 직접 구현하고, 실행 파일의 **진짜 심볼**(`dladdr`/`dlsym`)과 비교해 검증한다.

### Part A — 맹글링 규칙 구현 (자유 함수)

`FuncDecl{lang, ns, name, params}` 를 받아 링커가 보는 이름을 만드는 `mangle()` 를 구현한다.

- `builtin_code(Ty)` — `unsigned int → 'j'`, `unsigned long → 'm'`, `unsigned char → 'h'` 같은 한 글자 코드.
- `Lang::C` 면 이름 그대로 (네임스페이스 안에 있어도!). `Lang::Cpp` 면 `_Z` + 이름 + 파라미터 코드.
- 네임스페이스가 있으면 `N3hal4gpio5writeE` 처럼 `N … E` 로 감싼다. 파라미터가 없으면 `v`.
- 결과가 실행 파일 안의 실제 심볼(`dladdr`)과 같고, `abi::__cxa_demangle` 로 풀면 원래 선언이 나와야 한다.

### Part B — 오브젝트 포맷 차이와 nm 출력 해석

- `object_symbol(d, fmt)` — `nm` 이 보여주는 이름. **Mach-O 는 모든 심볼 앞에 `_` 를 하나 더** 붙인다 (`__ZN3hal…`, `_hal_init`). ELF 는 그대로.
- top-level `const` 파라미터(`void f(const int)`, `char* const`)는 시그니처에서 무시된다 → 맹글링에 안 나온다.
- `parse_symbol(sym, fmt)` — nm 한 줄을 받아 `{base 이름, 맹글링 여부}` 를 돌려준다. `__ZN3hal4gpio5writeEjb` → `{"write", true}`, `_SysTick_Handler` → `{"SysTick_Handler", false}`.

### Part C — 벡터 테이블 링크와 C 콜백

- `link_vectors(names, n, table, lookup)` — startup 파일의 `.weak X` + `.thumb_set X, Default_Handler` 를 흉내낸다: **이름으로** 강한 정의를 찾고, 없으면 `Default_Handler`. 테스트는 진짜 `dlsym(RTLD_DEFAULT, name)` 을 lookup 으로 넘긴다.
- `TIM2_IRQHandler` 정의가 벡터에 안 걸린다. **한 단어** 고쳐서 걸리게 하라.
- `lint_isr(nm_symbols, vec_names, n, fmt)` — "벡터 이름과 base 는 같은데 **맹글링된 정의만** 있는" ISR을 찾아내는 CI 린트.
- `LineReader::attach()` / `rx_trampoline()` — C HAL 의 `hal_uart_set_rx_callback(hal_rx_cb_t, void* ctx)` 에 C++ 객체를 연결한다.

## 2. Problem (English)

> **C/C++ linkage on a firmware project.**
>
> A vendor HAL is written in C (`hal.c`) and the application in C++ (`app.cpp`).
>
> 1. Implement `std::string mangle(const FuncDecl&)` producing the Itanium C++ ABI symbol for a free function with optional namespaces and parameters drawn from built-in types, `T*` and `const T*` (no repeated compound types). Functions with C language linkage are not mangled. Example: `hal::gpio::write(uint32_t, bool)` → `_ZN3hal4gpio5writeEjb` on macOS/arm64.
> 2. Implement `object_symbol()` (Mach-O prepends `_` to every symbol) and `parse_symbol()` that recovers the unqualified base name from an `nm` symbol and reports whether it was mangled.
> 3. Implement `link_vectors()` that fills a vector table by **name** lookup, falling back to `Default_Handler` (weak-alias semantics). Explain why `void TIM2_IRQHandler() {...}` in a `.cpp` file links without error yet never runs, fix it, and write `lint_isr()` that flags such handlers from an `nm` listing.
> 4. Register a C++ object with a C callback API `void hal_uart_set_rx_callback(void (*)(void* ctx, uint8_t), void* ctx)`.
>
> Constraints: `-std=c++20 -fno-exceptions -fno-rtti`. Explain what `extern "C"` changes and what it does *not* change.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # macOS 27.2 SDK 링크 문제 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "3/25 passed" — TODO 를 채워 25/25 를 만든다
# Linux: 끝에 -rdynamic 추가 (dlsym 이 실행 파일 자신의 심볼을 보려면 필요)
```

**관찰용 데모 파일** (레슨 폴더, 먼저 한 번 돌려 보자):

| 파일 | 역할 |
|---|---|
| `hal.h` | mock C HAL 헤더. `#if defined(__cplusplus) && !defined(HAL_FORGET_EXTERN_C)` 가드 |
| `hal.c` | mock HAL 구현 (가짜 ODR, UART 출력, RX 콜백) — `cc` 로 컴파일 |
| `startup.c` | 벡터 테이블 + weak `SysTick_Handler`/`USART1_IRQHandler` + `Default_Handler` |
| `app.cpp` | C++ 앱: HAL 호출, `LineReader` 콜백 등록, `USART1_IRQHandler` 정의 |
| `build.sh` | 시나리오 3개 (정상 / 가드 없음 / ISR 에 extern "C" 누락) + nm + c++filt |
| `demo_errors.cpp` | `extern "C"` 로 안 되는 것 3가지 (`-DSHOW_ERROR`) |

```bash
bash build.sh                    # macOS (산출물은 mktemp 디렉터리에)
ARM=1 bash build.sh              # arm-none-eabi-gcc 가 있는 Linux(예: Lima VM)에서 Cortex-M4 ELF 까지
c++ -std=c++20 -fsyntax-only -DSHOW_ERROR demo_errors.cpp
```

> `ARM=1` 분기는 작성 시점에 VM 이 꺼져 있어 **아직 실행하지 못했다**. 기대 동작: `fw_ISR.elf` 의 `arm-none-eabi-nm` 에서 `USART1_IRQHandler` 가 `W`(weak) 이고 주소가 `Default_Handler` 와 **같다** (alias 이므로), 그리고 `_Z17USART1_IRQHandlerv` 가 따로 `T` 로 존재. `fw_OK.elf` 에서는 `USART1_IRQHandler` 가 `T` 이고 주소가 다르다. 직접 돌려서 확인해 보자.

## 4. 힌트

<details>
<summary>힌트 1 — 맹글링 이름의 문법은 딱 세 조각</summary>

`_Z` (C++ 이름 표시) + **이름** + **파라미터 타입들**. 이름은 `<길이><글자>` 의 반복이다: `hal_init` → `8hal_init`. 네임스페이스가 있으면 `N` 과 `E` 사이에 조각들을 순서대로: `N3hal4gpio5writeE`. 반환형은 (템플릿이 아닌 함수에선) 안 들어간다.

</details>

<details>
<summary>힌트 2 — 타입 코드</summary>

`v`=void `b`=bool `c`=char `a`=signed char `h`=unsigned char `s`/`t`=short/unsigned short `i`/`j`=int/unsigned `l`/`m`=long/unsigned long `x`/`y`=long long/unsigned long long `f`/`d`=float/double. 포인터는 앞에 `P`, 가리키는 쪽 const 는 `K`: `const uint8_t*` → `PKh`. **`uint32_t` 는 typedef 라 맹글링에 이름이 안 남는다** — 실제 타입(`unsigned int`)의 코드가 들어간다.

</details>

<details>
<summary>힌트 3 — parse_symbol 은 거꾸로</summary>

Mach-O 면 맨 앞 `_` 하나를 먼저 떼어낸다. 남은 게 `_Z` 로 시작하지 않으면 C 심볼이다. 시작하면: `N` 이 있으면 `E` 를 만날 때까지 `<길이><글자>` 를 읽고 **마지막** 조각을 쓰고, 없으면 한 조각만 읽는다. 길이는 여러 자리(`17USART1_IRQHandler`)일 수 있다.

</details>

<details>
<summary>힌트 4 — TIM2 는 왜 안 걸리나</summary>

`dlsym(RTLD_DEFAULT, "TIM2_IRQHandler")` 는 **그 이름**을 찾는다. 지금 `.cpp` 안의 정의는 어떤 이름으로 오브젝트에 들어가 있을까? 다른 핸들러들과 선언을 비교해 보라.

</details>

<details>
<summary>힌트 5 — 멤버 함수는 콜백이 될 수 없다</summary>

C 는 `this` 를 모른다. `static` 멤버 함수(=보통 함수)를 등록하고, `ctx` 에 `this` 를 넣어 두었다가 트램펄린 안에서 `static_cast<LineReader*>(ctx)->on_byte(b)` 로 되돌린다.

</details>

## 5. 풀이

### 0) 관찰 — build.sh 실제 출력 (macOS arm64, Apple clang 21)

**시나리오 1 (정상)**: C 쪽 정의와 C++ 쪽 참조가 같은 이름이다. Mach-O 라 둘 다 앞에 `_` 가 붙어 있다.

```text
[app] ODR = 0x00000020
[hal] uart_write: hello from C++
[app] rx line = "AT+RST"
[app] usart1 ISR hits = 1  (OK)
--- nm hal.o (C 컴파일러가 만든 정의, T=text 정의)
0000000000000030 T _hal_gpio_read_odr
0000000000000000 T _hal_gpio_write
...
--- nm app.o (U=미정의 참조: C 이름 그대로 찾는다)
                 U _hal_gpio_read_odr
                 U _hal_gpio_write
                 U _hal_uart_write
```

**시나리오 2 (가드 없음)**: 같은 헤더를 C++ 링키지로 읽으니 참조가 전부 맹글링되었다. ld64 는 친절하게 "extern "C" 빠진 것 같다"고 NOTE 까지 준다.

```text
--- nm app_bad.o (맹글링된 이름으로 찾는다)
                 U __Z14hal_gpio_writejb
                 U __Z14hal_uart_writePKhm
                 U __Z17hal_gpio_read_odrv
                 U __Z24hal_uart_set_rx_callbackPFvPvhES_
--- c++filt 로 해석
hal_gpio_write(unsigned int, bool)
hal_uart_write(unsigned char const*, unsigned long)
hal_gpio_read_odr()
hal_uart_set_rx_callback(void (*)(void*, unsigned char), void*)
--- 링크
Undefined symbols for architecture arm64:
  "hal_gpio_write(unsigned int, bool)", referenced from:
   NOTE: found '_hal_gpio_write' in hal.o, declaration possibly missing 'extern "C"'
  "hal_uart_write(unsigned char const*, unsigned long)", referenced from:
   NOTE: found '_hal_uart_write' in hal.o, declaration possibly missing 'extern "C"'
...
(link exit=1)
```

- macOS 의 `c++filt` 는 `__Z…`(밑줄 2개)와 `_Z…`(1개) 둘 다 풀어 준다 (실측). GNU binutils `c++filt` 는 ELF 기준이라 `_Z…` 를 기대한다 — Mach-O 이름을 넣을 땐 `-_`(`--strip-underscore`) 옵션을 쓴다.
- GNU ld(arm-none-eabi)는 같은 상황에서 `undefined reference to `hal_gpio_write(unsigned int, bool)'` 처럼 **디맹글된** 이름을 보여준다(기본 `--demangle`). 메시지에 괄호와 타입이 보이면 "C++ 이름으로 찾고 있다" = extern "C" 문제라고 바로 알 수 있다. (이 문장은 기대 동작 — 이번 세션에선 ARM 링크까지는 못 돌렸다.)

**시나리오 3 (ISR 에 extern "C" 누락)**: 링크는 **성공**한다. 그런데 IRQ 를 쏘면 Default_Handler 가 돈다.

```text
[startup] Default_Handler: unexpected IRQ!
[app] usart1 ISR hits = 0  (BUG: Default_Handler ran instead)
(run exit=1)
--- 두 개의 서로 다른 심볼이 생겼다
000000010000081c (__TEXT,__text) external _Default_Handler
0000000100000828 (__TEXT,__text) weak external _USART1_IRQHandler
00000001000005d0 (__TEXT,__text) external __Z17USART1_IRQHandlerv
```

`nm -m` 이 핵심을 보여준다: startup 의 **weak** `_USART1_IRQHandler` 와 앱의 `__Z17USART1_IRQHandlerv` 는 **이름이 달라서 충돌조차 안 한다**. 벡터 테이블은 weak 쪽을 가리키고, 앱의 핸들러는 아무도 부르지 않는 죽은 코드가 된다. 실제 STM32 에서는 `Default_Handler` 가 `b .`(무한 루프)라서 "UART 받자마자 보드가 멈춤" 으로 보인다.

### Part A — 맹글링 이름 해부

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">hal::gpio::write(uint32_t, bool)  →  _ZN3hal4gpio5writeEjb</text>
  <g stroke="currentColor">
    <rect x="20" y="45" width="50" height="36" class="f-acc"/>
    <rect x="70" y="45" width="30" height="36" class="f-mut"/>
    <rect x="100" y="45" width="80" height="36" class="f-ok"/>
    <rect x="180" y="45" width="90" height="36" class="f-ok"/>
    <rect x="270" y="45" width="100" height="36" class="f-ok"/>
    <rect x="370" y="45" width="30" height="36" class="f-mut"/>
    <rect x="400" y="45" width="40" height="36" class="f-warn"/>
    <rect x="440" y="45" width="40" height="36" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="45" y="68">_Z</text><text x="85" y="68">N</text><text x="140" y="68">3hal</text>
    <text x="225" y="68">4gpio</text><text x="320" y="68">5write</text><text x="385" y="68">E</text>
    <text x="420" y="68">j</text><text x="460" y="68">b</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="104">C++ 이름</text>
    <text x="70" y="122">중첩 시작</text>
    <text x="100" y="140">&lt;길이&gt;&lt;글자&gt; 반복 (네임스페이스 → 함수)</text>
    <text x="350" y="122">중첩 끝</text>
    <text x="400" y="104">uint32_t → unsigned int → j</text>
    <text x="440" y="158">bool → b</text>
  </g>
  <text x="10" y="190" fill="currentColor">반환형은 없다 (비템플릿 함수). typedef 이름(uint32_t)도 없다 — 컴파일러가 본 "진짜 타입"만 남는다.</text>
  <text x="10" y="214" fill="currentColor">extern "C" 이면 이 전체가 그냥 "write" (네임스페이스도 사라짐). Mach-O nm 에서는 앞에 _ 가 하나 더.</text>
</svg>
```

구현은 표를 그대로 옮기면 된다.

| 조각 | 규칙 (Itanium C++ ABI) | 예 |
|---|---|---|
| 접두사 | 모든 C++ 링키지 이름은 `_Z` | `_Z` |
| 이름 (전역) | `<source-name>` = 10진 길이 + 식별자 | `8hal_init` |
| 이름 (중첩) | `N` + 조각들 + `E` | `N3hal4gpio5writeE` |
| 파라미터 없음 | `v` 한 글자 (`f()` == `f(void)`) | `_Z8hal_initv` |
| 기본 타입 | 한 글자 | `j` `m` `h` `b` |
| 포인터 / const 대상 | `P` / `K` 를 앞에 | `const uint8_t*` → `PKh` |
| top-level const | **무시** — 시그니처에 포함 안 됨 | `f(const int)` → `_Z1fi` |
| 반환형 | 비템플릿 함수엔 없음 | — |

왜 이렇게 하나? C++ 에는 **오버로드**(`write(uint8_t)` / `write(uint32_t)`)와 **네임스페이스**가 있는데, 링커(ELF/Mach-O)는 1970년대 C 시절 그대로 "이름 문자열 = 주소" 표만 다룬다. 그래서 컴파일러가 타입 정보를 이름 안에 **인코딩**한다. 맹글링 방식은 표준이 정하지 않는다 — **ABI** 가 정한다. GCC/Clang 은 x86-64·AArch64·ARM(AAPCS) 모두 Itanium C++ ABI 규칙을 쓰고, MSVC 는 `?write@gpio@hal@@YAXI_N@Z` 같은 전혀 다른 규칙을 쓴다 → **C++ 오브젝트는 컴파일러 간 링크가 보장되지 않는다. C 인터페이스는 된다.** 벤더 SDK 가 C 로 나오는 이유 중 하나다.

검증은 실제 컴파일러가 만든 이름으로 한다. `dladdr(&hal::gpio::write)` 가 돌려주는 `dli_sname` 은 `_ZN3hal4gpio5writeEjb` 다 (dladdr/dlsym 은 Mach-O 의 앞 `_` 를 뗀 "C 수준 이름"을 쓴다). 우리 `mangle()` 결과와 문자열이 같으면 규칙을 제대로 구현한 것이다.

범위 밖이지만 알아둘 것 — **치환(substitution)**: 같은 복합 타입이 반복되면 `S_`, `S0_`, `S1_`… 로 앞의 것을 가리킨다. 시나리오 2 의 `__Z24hal_uart_set_rx_callbackPFvPvhES_` 에서 마지막 `S_` 는 처음 나온 복합 타입 `Pv`(void*) 를 다시 쓴 것이다. 네임스페이스 조각도 후보가 되므로 `hal::gpio::write(const char*, const char*)` 는 `_ZN3hal4gpio5writeEPKcS2_` 가 된다 (추가 연습 4 에서 실측).

### Part B — 포맷 차이, top-level const, nm 파싱

| 선언 | macOS `nm` (Mach-O) | ELF `nm` (Linux / arm-none-eabi) |
|---|---|---|
| `extern "C" void hal_init(void)` | `_hal_init` | `hal_init` |
| `void hal_init()` (C++) | `__Z8hal_initv` | `_Z8hal_initv` |
| `void hal::gpio::write(uint32_t,bool)` | `__ZN3hal4gpio5writeEjb` | `_ZN3hal4gpio5writeEjb` (aarch64) / `_ZN3hal4gpio5writeEmb` (arm-none-eabi) |

Mach-O 의 밑줄은 C 컴파일러가 **모든** 전역 심볼에 붙이는 옛 관례(Unix a.out/COFF 유산)이고, C++ 이름은 `_Z…` 에 그게 하나 더 붙어 `__Z…` 가 된다. ELF 는 붙이지 않는다. 그래서 Mac 에서 링커 스크립트나 어셈블리로 C 심볼을 참조할 때는 `_hal_init`, ELF 에서는 `hal_init` 이라고 써야 한다 (Mac 에서 `.s` 파일로 `bl hal_init` 을 쓰면 못 찾는다).

**top-level const 가 무시되는 이유**: `void f(int)` 와 `void f(const int)` 는 **같은 함수의 선언**이다 ([dcl.fct]: 파라미터 타입 목록을 만들 때 top-level cv 한정자는 지워진다). 파라미터는 값 복사라 호출자 입장에서 const 여부가 아무 의미가 없기 때문이다. 반면 `const char*` 의 `const` 는 "가리키는 대상"에 붙은 것이라 타입의 일부다 (`PKc`). `char* const` 는 포인터 자체가 const → 무시 → `Pc`. 테스트가 이 둘을 실측 심볼과 비교한다.

`parse_symbol` 은 디맹글러의 축소판이다. 길이가 여러 자리일 수 있다는 점(`17USART1_IRQHandler`), 중첩이면 **마지막** 조각이 함수 이름이라는 점, Mach-O 밑줄을 먼저 떼야 한다는 점이 함정이다.

### Part C — ISR 이름과 weak 심볼, 그리고 C 콜백

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">벡터 테이블은 "이름"으로 채워진다 (weak 기본값 + strong 덮어쓰기)</text>
  <rect x="10" y="40" width="190" height="150" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor">startup_stm32 (.s/.c)</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="22" y="90">[TIM2 슬롯]</text>
    <text x="22" y="110">.word TIM2_IRQHandler</text>
    <text x="22" y="140">.weak TIM2_IRQHandler</text>
    <text x="22" y="160">.thumb_set TIM2_IRQ…,</text>
    <text x="22" y="178">     Default_Handler</text>
  </g>
  <rect x="270" y="40" width="270" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="282" y="62" fill="currentColor" font-size="12">extern "C" void TIM2_IRQHandler()</text>
  <text x="282" y="84" fill="currentColor" font-family="monospace" font-size="12">T TIM2_IRQHandler (strong)</text>
  <rect x="270" y="130" width="270" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="282" y="152" fill="currentColor" font-size="12">void TIM2_IRQHandler()  // .cpp</text>
  <text x="282" y="174" fill="currentColor" font-family="monospace" font-size="12">T _Z15TIM2_IRQHandlerv</text>
  <path d="M200 105 L265 70" stroke="currentColor" fill="none"/>
  <text x="206" y="74" fill="currentColor" font-size="12">같은 이름</text>
  <path d="M200 160 L265 160" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="206" y="152" fill="currentColor" font-size="12">이름 다름</text>
  <rect x="575" y="40" width="160" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="585" y="62" fill="currentColor" font-size="12">strong 이 weak 를 이김</text>
  <text x="585" y="84" fill="currentColor" font-size="12">→ 내 핸들러 실행</text>
  <rect x="575" y="130" width="160" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="585" y="152" fill="currentColor" font-size="12">경쟁자 없음 → weak 유지</text>
  <text x="585" y="174" fill="currentColor" font-size="12">→ Default_Handler (b .)</text>
  <path d="M540 68 L570 68" stroke="currentColor" fill="none"/>
  <path d="M540 158 L570 158" stroke="currentColor" fill="none"/>
  <text x="10" y="222" fill="currentColor">링크 에러도 경고도 없다. 링커 입장에선 "아무도 안 쓰는 함수 하나 + 정상적인 weak 심볼" 일 뿐이다.</text>
  <text x="10" y="246" fill="currentColor">잡는 법: map 파일/nm 에서 _Z…IRQHandler / _Z…_Handler 검색 → lint_isr() 가 하는 일.</text>
</svg>
```

**왜 Cortex-M 에선 이름만 맞으면 되나?** Cortex-M 은 예외 진입 시 하드웨어가 r0–r3, r12, lr, pc, xPSR 을 스택에 쌓고 `lr` 에 EXC_RETURN 값을 넣는다. 그래서 ISR 은 **평범한 AAPCS 함수**면 충분하다 (`__attribute__((interrupt))` 불필요). 남은 유일한 연결 고리가 startup 파일이 참조하는 **심볼 이름**이고, 그게 `extern "C"` 의 일이다.

`link_vectors` 구현:

1. 각 벡터 이름으로 `lookup(name)` — 테스트에선 `dlsym(RTLD_DEFAULT, name)` = 실행 파일 심볼 표에서 진짜로 찾기.
2. 있으면 `reinterpret_cast<Handler>(p)` (POSIX 가 `void*` ↔ 함수 포인터 변환을 보장, C++ 표준에선 conditionally-supported [expr.reinterpret.cast]).
3. 없으면 `&Default_Handler` — weak 별칭의 의미.

starter 의 `TIM2_IRQHandler` 는 C++ 링키지라 실행 파일에 `__Z15TIM2_IRQHandlerv`(실측 nm)로만 존재한다. `dlsym("TIM2_IRQHandler")` 는 `nullptr` → Default. 정답은 **`extern "C"` 한 단어**를 붙이는 것. 하네스의 `nm_out` 목록은 바로 그 버그 상태에서 실제 `nm` 이 보여준 이름들이다.

`lint_isr` 는 각 벡터 이름에 대해 nm 심볼들을 `parse_symbol` 로 풀어 **base 가 같은 C 정의가 없고 C++ 정의만 있으면** 경보를 낸다. 실무에선 빌드 후 스텝으로 `arm-none-eabi-nm fw.elf | grep -E '_Z[0-9]+\w*(IRQHandler|_Handler)'` 한 줄만 걸어 둬도 이 버그를 영구히 막는다.

**C 콜백 트램펄린** — C HAL 은 `void (*)(void* ctx, uint8_t)` 만 안다. 멤버 함수 포인터(`void (LineReader::*)(uint8_t)`)는 `this` 가 숨은 인자라 타입도 크기도 달라서 넘길 수 없다. 그래서:

```cpp
void attach() { hal_uart_set_rx_callback(&LineReader::rx_trampoline, this); }       // ctx = this
static void rx_trampoline(void* ctx, uint8_t b) { static_cast<LineReader*>(ctx)->on_byte(b); }
```

C 로 쓰던 `void (*cb)(void *ctx, …)` + `struct drv *d = ctx;` 패턴과 **똑같은 기계어**다 — C++ 이라고 비용이 붙지 않는다 (vtable 도 힙도 없음).

엄밀한 표준 이야기 하나: [dcl.link] 는 "언어 링키지가 다른 **함수 타입**은 서로 다른 타입" 이라고 한다. 즉 C++ 링키지인 static 멤버 함수의 주소를 `extern "C"` 함수 포인터 타입 파라미터에 넘기는 것은 엄밀히는 타입 불일치다. 하지만 **GCC·Clang·MSVC 모두 함수 타입의 언어 링키지를 구분하지 않고**(Clang 21 에서 `-Wpedantic` 로도 경고 없음, 실측) 호출 규약도 같으므로 실무 표준 관행은 static 멤버 트램펄린이다. 100% 이식성이 필요하면 네임스페이스 범위의 `extern "C"` 자유 함수를 트램펄린으로 쓴다. 그리고 `extern "C"` 는 클래스 안에 쓸 수 없다 — 링키지 지정은 네임스페이스 범위에서만 허용되고, 클래스 멤버에는 C 링키지가 무시된다 ([dcl.link]).

### C 와 비교 — extern "C" 가 바꾸는 것 / 안 바꾸는 것

| 항목 | extern "C" 영향 |
|---|---|
| 심볼 이름 | **맹글링 안 함** (C 와 동일한 이름) |
| 오버로드 | 불가 — 같은 이름의 C 링키지 함수는 하나뿐 ([dcl.link]) |
| 네임스페이스 | 이름에서 사라짐 — `drv::drv_c_entry` 의 심볼은 `drv_c_entry` |
| 함수 본문 | 영향 없음 — 안에서 클래스·템플릿·RAII 자유롭게 사용 |
| 호출 규약 | GCC/Clang 에선 동일 (AAPCS / AAPCS64 / SysV) |
| 런타임 비용 | **0** — 이름표만 바뀐다 |
| 템플릿 | C 링키지 불가 (`demo_errors.cpp` 실측) |

`demo_errors.cpp` 를 `-DSHOW_ERROR` 로 돌린 실제 에러 (Apple clang 21):

```text
demo_errors.cpp:10:17: error: conflicting types for 'hal_write'
demo_errors.cpp:6:17: note: previous declaration is here
demo_errors.cpp:14:12: error: expected member name or ';' after declaration specifiers
demo_errors.cpp:18:12: error: templates must have C++ linkage
demo_errors.cpp:18:1: note: extern "C" language linkage specification begins here
3 errors generated.
```

### 타깃 ABI 에 따라 맹글링이 달라진다 (실측)

`void f(uint32_t, uint8_t, uint64_t, size_t)` 를 세 컴파일러로 빌드한 `nm` 결과:

| 타깃 | 심볼 | uint32_t | uint64_t | size_t |
|---|---|---|---|---|
| macOS arm64 (clang 21) | `__Z1fjhym` | `j` unsigned int | `y` unsigned long long | `m` unsigned long |
| Linux aarch64 (g++ 15.2) | `_Z1fjhmm` | `j` | `m` unsigned long | `m` |
| arm-none-eabi (g++ 14.2.1, Cortex-M4) | `_Z1fmhyj` | **`m` unsigned long** | `y` | **`j` unsigned int** |

(arm-none-eabi 와 aarch64 결과는 Lima VM 에서 실행한 실제 출력.) newlib 은 32비트 ARM 에서 `uint32_t` 를 `unsigned long` 으로 정의한다 — `printf("%u", u32)` 가 ARM 에서만 `-Wformat` 경고를 내는 바로 그 이유다. 결론: **호스트에서 만든 C++ 오브젝트/라이브러리를 타깃에 링크할 생각은 하지 말 것**, 그리고 맹글링된 이름을 손으로 링커 스크립트에 적지 말 것 (`KEEP(*(.text._Z…))` 같은 건 타깃 바뀌면 깨진다).

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 25/25 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined solution.cpp -o sol_asan && ./sol_asan                  # 25/25, ASan/UBSan 깨끗
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| C 헤더에 가드 없이 C++ 에서 include | 선언이 C++ 링키지로 읽혀 `_Z…` 를 찾음 → undefined symbol | 헤더에 `#ifdef __cplusplus` / `extern "C" {` / `}` / `#endif` |
| 가드를 `#include` 줄 **바깥**에 두기: `extern "C" { #include <vector> }` | C++ 헤더(템플릿)까지 C 링키지로 감싸 "templates must have C++ linkage" | 가드는 **C 헤더 안**에, 자기 선언만 감싼다 |
| `.cpp` 의 ISR 에 `extern "C"` 누락 | 링크 OK, weak 기본값이 이김 → Default_Handler 무한 루프 | `extern "C" void USART1_IRQHandler(void) {…}` + nm 린트 |
| ISR 이름 오타 (`USART1_IRQHandle`) | 같은 증상 — 새 심볼이 하나 생길 뿐 | 핸들러를 extern "C" 헤더에 선언해 두고 `-Wmissing-prototypes`(Clang) / `-Wmissing-declarations`(GCC) 로 "선언 없는 전역 함수" 경고 — 오타 핸들러가 바로 걸린다 (Clang 실측: `no previous prototype for function 'USART1_IRQHandle'`) |
| `extern "C"` 로 오버로드 시도 | C 이름은 하나뿐 → conflicting types | `hal_write_u8/_u16` 처럼 이름에 타입 (추가 연습 3) |
| 헤더에 `extern "C" { uint32_t g_tick; }` | 블록 형태는 **정의**가 된다 → 여러 .cpp 가 include 하면 duplicate symbol | `extern "C" uint32_t g_tick;` (한 줄 형태는 `extern` 취급 = 선언) 또는 블록 안에 `extern uint32_t g_tick;`. 실측 nm: 블록 형태 `S _g_tick`(정의), 한 줄 형태 `U _g_decl`(참조) |
| 멤버 함수를 C 콜백으로 넘기기 `hal_set_cb(&Drv::on_rx)` | 멤버 함수 포인터는 `this` 가 필요해 타입이 다름 → 컴파일 에러 | static 트램펄린 + `void* ctx = this` |
| C 콜백 안으로 C++ 예외를 던짐 | C 프레임을 가로지르는 unwind 는 보장 안 됨 (펌웨어는 `-fno-exceptions` 라 대개 해당 없음) | 콜백 경계에서 에러는 반환값으로 |
| Mac 에서 `c++filt` 결과를 ELF 로 착각 | Mach-O 이름엔 `_` 하나가 더 있다 | GNU c++filt 에는 `-_` 옵션, ELF 는 `_Z…` 그대로 |

## 8. 면접 Q&A

<details>
<summary>Q1. extern "C" 는 정확히 무엇을 하나요? 함수 안에서 C++ 기능을 못 쓰게 되나요?</summary>

선언의 **언어 링키지**를 C 로 지정한다 → 이름을 맹글링하지 않아 C 컴파일러가 만든/찾는 심볼과 같아진다. 함수 **본문**에는 영향이 없다: 안에서 클래스, 템플릿, RAII 를 다 쓸 수 있다. 부작용은 이름 기반 기능을 잃는 것뿐 — 오버로드 불가, 네임스페이스가 심볼에서 사라짐, 템플릿 불가. 런타임 비용은 0.

</details>

<details>
<summary>Q2. 링크 에러 "undefined reference to `hal_init()'" 를 보면 무엇부터 확인하나요?</summary>

괄호 `()` 가 보이면 링커가 **C++ 이름**을 찾고 있다는 뜻 → C 로 정의된 함수라면 extern "C" 가드 누락. `nm hal.o | grep hal_init` (정의 쪽 이름) 과 `nm app.o | grep hal_init` (참조 쪽 이름) 을 비교하고, `c++filt` 로 푼다. 둘 다 맹글링이면 다른 원인: 시그니처 불일치(헤더와 정의의 파라미터 타입 차이), 파일이 빌드에 안 들어감, `static` 으로 정의됨, 라이브러리 링크 순서.

</details>

<details>
<summary>Q3. (꼬리) ISR 을 .cpp 에 옮겼더니 인터럽트가 오면 보드가 멈춥니다. 링크 에러는 없었어요. 원인은?</summary>

startup 파일은 각 핸들러를 `Default_Handler` 의 **weak 별칭**으로 정의한다. `.cpp` 의 ISR 에 extern "C" 가 없으면 이름이 `_Z17USART1_IRQHandlerv` 라서 weak 심볼을 덮어쓰지 못하고, 둘 다 공존한다 → 벡터 테이블은 Default_Handler(`b .`) 를 가리킨다. 확인: 디버거로 멈춘 PC 가 Default_Handler, 또는 `nm fw.elf | grep IRQHandler` 에 `W` 와 `_Z…` 가 같이 보임. 해결: `extern "C"`. 재발 방지: 빌드 후 nm 린트, 또는 ISR 을 extern "C" 블록 헤더에 선언해 정의와 선언 불일치를 컴파일 단계에서 잡기.

</details>

<details>
<summary>Q4. 왜 C++ 는 맹글링을 해야 하죠? 표준이 정한 규칙인가요?</summary>

오버로드·네임스페이스·템플릿 인자 등 같은 "이름"의 여러 엔티티를 이름 문자열만 다루는 기존 링커/오브젝트 포맷에서 구분하려면 타입 정보를 이름에 넣어야 한다. 규칙은 C++ 표준이 아니라 **ABI** 가 정한다: GCC/Clang 은 Itanium C++ ABI (ARM 도 AAPCS 의 C++ ABI 가 Itanium 기반), MSVC 는 자체 규칙. 그래서 서로 다른 C++ 컴파일러의 오브젝트는 섞기 어렵고, 라이브러리 경계는 C API 로 두는 게 관례다. 같은 Itanium 규칙이라도 `uint32_t` 같은 typedef 는 실제 타입으로 인코딩되어 타깃마다 달라진다 (Mac `j`, arm-none-eabi `m`).

</details>

<details>
<summary>Q5. C 라이브러리가 콜백을 요구할 때 C++ 멤버 함수를 넘기는 방법은?</summary>

멤버 함수 포인터는 `this` 가 필요해서 C 함수 포인터로 변환되지 않는다. API 에 `void* ctx`(user data)가 있으면 static 멤버(또는 extern "C" 자유 함수) 트램펄린을 등록하고 ctx 에 `this` 를 넣어 `static_cast<T*>(ctx)->method()` 로 되돌린다. ctx 가 없는 API(예: 일부 HAL 의 weak `HAL_UART_RxCpltCallback(UART_HandleTypeDef*)`)라면 핸들 → 객체 매핑 테이블이나 싱글톤이 필요하다. 꼬리: 캡처 없는 람다도 함수 포인터로 변환되므로 `hal_set_cb([](void* c, uint8_t b){ static_cast<T*>(c)->on(b); }, this)` 도 가능하다 (언어 링키지 엄밀성 문제는 static 멤버와 동일).

</details>

<details>
<summary>Q6. `#ifdef __cplusplus` 가드 헤더를 쓰면 C 쪽에선 어떻게 보이나요? `__cplusplus` 값은?</summary>

C 컴파일러는 `__cplusplus` 를 정의하지 않으므로 `extern "C" {` 줄이 사라지고 평범한 C 선언만 남는다. C++ 에서는 표준 버전 값(C++17 = `201703L`, C++20 = `202002L`)으로 정의된다. 이 가드가 있어야 같은 헤더를 `hal.c`(C) 와 `app.cpp`(C++) 가 함께 쓸 수 있다. 주의: 가드 안 선언은 C 와 C++ 양쪽에서 유효한 문법만 써야 한다 (C 의 `bool` 은 `<stdbool.h>`, `restrict` 는 C++ 에 없음 등).

</details>

<details>
<summary>Q7. extern "C" 함수가 서로 다른 네임스페이스에 같은 이름으로 선언되면?</summary>

같은 함수다. [dcl.link] 는 C 링키지 함수는 이름(네임스페이스 무시)당 하나만 존재할 수 있고, 다른 네임스페이스의 같은 이름 C 링키지 선언은 같은 함수를 가리킨다고 정한다. 그래서 `namespace drv { extern "C" void drv_c_entry(uint32_t); }` 의 심볼은 그냥 `drv_c_entry` 다 (Part A 테스트에서 dladdr 로 실측). 두 곳에서 각각 **정의**하면 duplicate symbol 링크 에러.

</details>

## 9. 추가 연습

### 연습 1 — 미니 디맹글러

Part A 범위(네임스페이스 + 기본 타입 + `P`/`PK`)의 `demangle_simple()` 을 짜라. 출력 형식은 c++filt 와 같게(`unsigned char const*`). 실행 파일의 실제 심볼을 넣고 `abi::__cxa_demangle` 결과와 비교한다.

<details>
<summary>답 보기</summary>

`_Z` 확인 → `N` 이면 `E` 까지 `<길이><id>` 를 `::` 로 이어붙임 → 나머지가 `v` 면 `()` → 아니면 `P`, `K` 플래그를 읽고 기본 타입 이름 + `" const"` + `"*"`. 범위 밖 문자(`S_` 치환, `V` volatile 등)를 만나면 입력을 그대로 돌려준다. c++filt 가 east-const(`char const*`)로 출력한다는 점이 맞춰야 할 디테일. 참고로 `volatile uint32_t*` 는 `PVj` 다 (`dma_start(volatile uint32_t*, uint16_t)` → `_Z9dma_startPVjt`, 실측).

</details>

### 연습 2 — 같은 typedef, 다른 심볼

`typedef_code(Target, Td)` 를 표로 구현하라: `uint32_t`, `uint64_t`, `size_t` 가 macOS arm64 / Linux aarch64 / arm-none-eabi 에서 각각 어떤 코드로 맹글링되는지. 호스트 열은 실제 심볼로 검증한다.

<details>
<summary>답 보기</summary>

| | uint32_t | uint64_t | size_t |
|---|---|---|---|
| macOS arm64 | j | y | m |
| Linux aarch64 (LP64) | j | m | m |
| arm-none-eabi (ILP32, newlib) | m | y | j |

호스트: `abi_probe(uint32_t, uint64_t, size_t)` → `_Z9abi_probejym`. 같은 소스가 타깃마다 다른 심볼이 되므로 맹글링된 이름을 링커 스크립트·어셈블리에 하드코딩하면 안 된다.

</details>

### 연습 3 — 오버로드된 C++ API 를 C 에 내보내기

`hal::reg_write(uint8_t/uint16_t/uint32_t)` 오버로드 세트를 C 코드가 부를 수 있게 하라. `EXTERN_C_BEGIN/END` 매크로를 직접 정의해서 써라.

<details>
<summary>답 보기</summary>

C 링키지는 오버로드가 안 되므로 이름에 폭을 넣은 래퍼 `hal_reg_write_u8/_u16/_u32` 를 extern "C" 로 정의하고 각각 해당 오버로드를 부른다 (오버로드 해석은 C++ 컴파일 시점에 끝남). 매크로는 `#ifdef __cplusplus` 일 때만 `extern "C" {` / `}` 로 확장. 검증: `dladdr` 이름이 `hal_reg_write_u16` 그대로, `dlsym("hal_reg_write_u8")` 가 그 함수 주소. 실제 CMSIS-Core 헤더도 비슷한 가드 패턴을 쓰고, 많은 SDK 가 이런 매크로를 둔다.

</details>

### 연습 4 — 치환(S_) 예측

`hal::gpio::write(const char*, const char*)` 의 맹글링 이름을 **먼저 손으로 예측**한 뒤 실제와 비교하라. 미니 디맹글러는 이걸 만나면 어떻게 해야 할까?

<details>
<summary>답 보기</summary>

치환 후보는 등장 순서대로 번호가 붙는다: `hal`→`S_`, `hal::gpio`→`S0_`, `char const`→`S1_`, `char const*`→`S2_` (기본 타입 `c` 와 함수 이름 자체는 후보가 아님). 두 번째 파라미터가 `char const*` 의 반복이므로 `_ZN3hal4gpio5writeEPKcS2_` — 실측 일치. 범위 밖 입력이므로 `demangle_simple` 은 원문을 그대로 돌려줘야 한다 (잘못 푸는 것보다 낫다).

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 17/17 passed
```

## 10. 핵심 정리

- C++ 는 오버로드·네임스페이스 때문에 이름을 **맹글링**한다: `hal::gpio::write(uint32_t,bool)` → `_ZN3hal4gpio5writeEjb`. 규칙은 표준이 아니라 **ABI**(Itanium) 소관.
- `extern "C"` = 언어 링키지 지정 → 맹글링 없음. 본문 C++ 기능·런타임 비용엔 영향 0. 대가: 오버로드·템플릿 불가, 네임스페이스가 심볼에서 사라짐.
- C 헤더는 `#ifdef __cplusplus` / `extern "C" {` … `}` 가드로 자기 선언만 감싼다.
- 링크 에러 메시지에 `(타입, …)` 가 보이면 C++ 이름으로 찾는 중 → `nm` 양쪽 비교 + `c++filt`.
- Mach-O 는 모든 심볼에 `_` 하나 추가 (`__Z…`, `_hal_init`), ELF 는 없음. typedef 는 실제 타입으로 인코딩 → `uint32_t` 가 Mac `j`, arm-none-eabi `m`.
- **ISR 에 extern "C" 누락 = 링크 성공 + weak Default_Handler 실행**. nm 에서 `_Z…IRQHandler` 를 찾는 린트로 막는다.
- C 콜백에는 static 트램펄린 + `void* ctx = this`. 멤버 함수 포인터는 C 함수 포인터가 아니다.

## 참고자료

- [Itanium C++ ABI — Mangling](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling) (`<builtin-type>`, `<nested-name>`, `<substitution>`)
- [cppreference — Language linkage](https://en.cppreference.com/w/cpp/language/language_linkage) / C++20 표준 [dcl.link], [dcl.fct], [temp.pre]
- [ARM — C++ ABI for the Arm Architecture (CPPABI32)](https://github.com/ARM-software/abi-aa/blob/main/cppabi32/cppabi32.rst)
- [CMSIS-Core startup / vector table (weak handlers)](https://arm-software.github.io/CMSIS_6/latest/Core/startup_c_pg.html)
- STM32CubeF4 `startup_stm32f4xx.s` — `.weak` / `.thumb_set` 별칭 패턴
- `man nm` (macOS: `nm -m` 로 weak/external 표시), `man c++filt`, `man dladdr`, `man dlsym`
- 관련 레슨: CPP-B01 (C 드라이버를 C++ 로 옮기기), CPP-B13 (-fno-exceptions/-fno-rtti), C-B10 (static과 Linkage: 내부·외부 링크)
