---
id: TOOL-B05
time: 60분
updated: 2026-11-15
---
# TOOL-B05 링크 에러 해결: undefined, multiple definition, weak

## 학습 목표

- 링크 에러 메시지 한 덩어리를 보고 **어느 .o 가 · 어떤 심볼을 · 왜** 못 찾았는지(또는 두 번 찾았는지) 30초 안에 말한다 — ld64(맥) 형식과 GNU ld(펌웨어) 형식 둘 다.
- undefined reference 의 원인 4가지(선언만 있고 정의 없음 / 이름 오타 / 링크 줄에서 빠진 .o / 다른 파일의 `static` 함수)를 `nm -u`, `nm` 글자(T/t/U)로 구분한다.
- 헤더에 변수를 **정의**하면 무슨 일이 생기는지 `-fcommon` / `-fno-common` 로 나눠 설명하고, `static` 으로 "고친 척" 하면 왜 더 위험한지 실행 결과로 보인다.
- C++ 에서 C 함수를 부를 때의 맹글링(name mangling) 링크 에러를 `c++filt` 로 읽고 `extern "C"` 가드로 고친다.
- Cortex-M 스타트업의 weak 기본 핸들러가 강한(strong) 정의에 지는 규칙을 알고, **핸들러 이름 오타 · 빌드에서 빠진 파일은 링크 에러가 안 난다**는 함정을 `nm` 으로 잡는다.

## 1. 문제

링크 에러는 "컴파일은 되는데 빌드가 안 된다" 의 거의 전부라서, 펌웨어 면접에서 "이 에러 왜 나요?" 로 바로 나오고, 실무에서는 새 보드 포팅·벤더 HAL 통합·C++ 도입 때마다 만난다. 더 무서운 건 **에러가 안 나는 링크 버그**다 — weak 기본 핸들러에 묶인 UART 인터럽트는 빌드가 초록불인 채로 현장에서 멈춘다.

링커(linker)가 하는 일은 단순하다. 각 `.o` 의 심볼 표(symbol table)에서 **정의(defined: T/D/B…)** 와 **미정의 참조(undefined: U)** 를 모아, 모든 U 를 정확히 하나의 정의에 연결하고 그 주소를 재배치(relocation) 자리에 채운다 (오브젝트 파일 구조는 TOOL-B04). 실패는 세 가지뿐이다: 정의가 **0개**(undefined), **2개 이상**(multiple definition), 그리고 정의가 있긴 한데 **내가 원한 것이 아닌** 경우(weak 가 이김, 이름이 맹글링돼 다른 이름이 됨).

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">s1 starter: 링커가 보는 심볼 표 (맥 nm 실측, -O2)</text>
  <rect x="10" y="35" width="210" height="130" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="55" fill="currentColor" font-weight="bold">main.o</text>
  <text x="20" y="75" fill="currentColor" font-family="monospace" font-size="12">U _sensor_selftest</text>
  <text x="20" y="93" fill="currentColor" font-family="monospace" font-size="12">U _crc8</text>
  <text x="20" y="111" fill="currentColor" font-family="monospace" font-size="12">U _adc_read_raw</text>
  <text x="20" y="129" fill="currentColor" font-family="monospace" font-size="12">U _adc_read_mv</text>
  <text x="20" y="147" fill="currentColor" font-family="monospace" font-size="12">U _uart_puts</text>
  <rect x="10" y="180" width="210" height="80" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="200" fill="currentColor" font-weight="bold">uart.o</text>
  <text x="20" y="220" fill="currentColor" font-family="monospace" font-size="12">T _uart_put_char  (오타)</text>
  <text x="20" y="238" fill="currentColor" font-family="monospace" font-size="12">T _uart_puts  U _uart_putc</text>
  <rect x="265" y="90" width="200" height="110" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="275" y="112" fill="currentColor" font-weight="bold">링커의 전역 심볼 표</text>
  <text x="275" y="134" fill="currentColor" font-size="12">U 하나마다 정의를 찾는다</text>
  <text x="275" y="154" fill="currentColor" font-size="12">0개 → undefined</text>
  <text x="275" y="174" fill="currentColor" font-size="12">강한 정의 2개+ → duplicate</text>
  <text x="275" y="192" fill="currentColor" font-size="12">weak + strong → strong</text>
  <path d="M220 110 L263 130" stroke="currentColor" stroke-width="2"/>
  <path d="M220 220 L263 170" stroke="currentColor" stroke-width="2"/>
  <rect x="510" y="35" width="220" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="520" y="55" fill="currentColor" font-weight="bold">adc.o</text>
  <text x="520" y="75" fill="currentColor" font-family="monospace" font-size="12">T _adc_read_mv</text>
  <text x="520" y="90" fill="currentColor" font-size="12">static 도우미는 인라인 → 심볼 없음</text>
  <rect x="510" y="110" width="220" height="45" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="520" y="130" fill="currentColor" font-weight="bold">sensor.o</text>
  <text x="520" y="148" fill="currentColor" font-size="12">selftest 정의 없음 (TODO)</text>
  <rect x="510" y="170" width="220" height="45" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="520" y="190" fill="currentColor" font-weight="bold">crc.o — 링크 줄에 없음</text>
  <text x="520" y="208" fill="currentColor" font-size="12">SRCS 에서 crc.c 누락</text>
  <path d="M465 130 L508 70" stroke="currentColor" stroke-width="2"/>
  <path d="M465 150 L508 135" stroke="currentColor" stroke-width="2"/>
  <rect x="265" y="230" width="465" height="65" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="275" y="252" fill="currentColor" font-weight="bold">결과: 미해결 4개 (ld64 실제 메시지 순서)</text>
  <text x="275" y="272" fill="currentColor" font-family="monospace" font-size="12">_adc_read_raw  _crc8  _sensor_selftest  _uart_putc</text>
  <text x="275" y="288" fill="currentColor" font-size="12">원인 4개가 다 다르다 — 메시지 모양은 같다</text>
</svg>
```

레슨 폴더에는 시나리오 4개가 있고, 각각 `common/`(그대로 두는 파일) + `starter/`(고칠 파일) + `solution/` 으로 나뉜다. `SRCS` 파일이 **링크 줄**(어떤 .c 를 컴파일해서 링크하나)이다 — Makefile/CMakeLists 의 소스 목록 역할.

| 시나리오 | 증상 (starter) | ld64 핵심 문구 | GNU ld 핵심 문구 |
|---|---|---|---|
| `s1_undefined` | 링크 실패, 심볼 4개 | `Undefined symbols for architecture arm64` | `undefined reference to 'crc8'` |
| `s2_multidef` | 링크 실패 / 반쯤 고치면 **링크 성공 + 값 틀림** | `duplicate symbol '_g_log_level' in:` | `multiple definition of 'g_ticks'; main.o:(.bss+0x0): first defined here` |
| `s3_mangling` | 링크 실패 (양방향) | `NOTE: found '_ring_pop' in ring.o, declaration possibly missing 'extern "C"'` | `undefined reference to 'ring_init(ring*, unsigned char*, unsigned long)'` |
| `s4_weak` | **링크 성공**, 인터럽트가 기본 핸들러로 샘 | (없음) | (없음) |

### Part A — undefined reference 4종 (`s1_undefined`)

센서 노드 부팅 코드가 링크되지 않는다. 메시지는 한 덩어리지만 원인은 넷이다: `sensor_selftest` 는 헤더에만 있고 정의가 없다, UART 함수 이름이 헤더와 다르다, `crc.c` 가 링크 줄(`SRCS`)에 없다, `adc_read_raw` 는 `adc.c` 안의 `static` 함수인데 `main.c` 가 직접 `extern` 선언하고 부른다. 원인마다 **알맞은 곳**을 고친다 (main.c 에 가짜 정의를 추가해서 막지 않는다). 체크: 링크 성공, 출력 4줄 일치, 최종 실행 파일 `nm` 에 네 심볼이 모두 `T`, `adc.o` 의 `adc_read_raw` 가 전역.

### Part B — multiple definition 과 C/C++ 맹글링 (`s2_multidef`, `s3_mangling`)

- `s2`: `config.h` 에 전역 3개가 **정의**돼 있고 네 파일이 include 한다. 고친 결과는 맥 기본(`-fcommon`)과 펌웨어 툴체인 규칙(`-fno-common`) **둘 다**에서 링크돼야 하고, `ticks=5 errs=2 level=2` 가 나와야 한다(세 파일이 **같은** 변수를 본다). `nm` 으로 각 변수의 정의가 .o 전체에서 딱 1개인지 확인한다.
- `s3`: C 로 쓴 링 버퍼(`ring.c`)를 C++ 앱(`app.cpp`)이 쓴다. `ring.h` **만** 고쳐서 양방향(C++ → C 함수, C → C++ 이 정의한 콜백)을 모두 링크한다.

### Part C — weak 기본 핸들러 (`s4_weak`)

`startup.c` 는 CMSIS 스타트업처럼 모든 핸들러를 **weak** 로 `Default_Handler` 에 묶어 둔다. 앱이 같은 이름의 강한 정의를 주면 그게 벡터 테이블에 들어간다. starter 는 링크가 **성공**하지만 USART1 · TIM2 인터럽트가 모두 기본 핸들러로 간다(`default_hits=3`). 원인 둘(이름 오타, 빌드에서 빠진 파일)을 고치고, 맥(Mach-O, `nm -m` 의 `weak external`)과 VM 의 `arm-none-eabi-gcc` ELF(`nm` 의 `W`, alias 라서 Default_Handler 와 **같은 주소**) 양쪽에서 강한 정의가 이겼다는 근거를 남긴다.

## 2. Problem (English)

> **Fix four broken firmware builds and prove each fix with `nm`.**
>
> You get four small projects. Each has a `SRCS` file (the link line) and files you may edit. Do not add definitions to the wrong file just to silence the linker; fix the root cause.
>
> 1. `s1_undefined` fails with four undefined symbols (`_adc_read_raw`, `_crc8`, `_sensor_selftest`, `_uart_putc`). Each has a different cause. Expected output: `selftest=0`, `crc8=0xF4`, `adc_raw=2048 adc_mv=1650`, `UART OK`.
> 2. `s2_multidef` defines globals in a header included by four `.c` files. Make it link both with `-fcommon` and `-fno-common` and print `ticks=5 errs=2 level=2`. Each global must have exactly one definition across all objects.
> 3. `s3_mangling`: C++ code calls a C ring buffer and the C code calls back into a hook defined in C++. Only `ring.h` may change. Expected: `sum=10 dropped=2`.
> 4. `s4_weak`: the build links, but firing USART1 and TIM2 reaches `Default_Handler`. Make it print `uart_rx=1 tim2=2 default_hits=0 last_unhandled=-1`, and show that the final image has strong (non-weak) `USART1_IRQHandler` / `TIM2_IRQHandler` — on Mach-O and on an `arm-none-eabi` ELF.
>
> Follow-ups: what does the linker do with `U` symbols; `-fcommon` vs `-fno-common`; why does `static` in a header "fix" the error but break the program; how do weak symbols work and why are they dangerous; how would you catch an unbound IRQ handler in CI?

## 3. 시작 코드

```text
TOOL-B05/  check.sh (starter | solution | practice)    practice/ (9. 추가 연습 답)
  s1_undefined/ s2_multidef/ s3_mangling/ s4_weak/     각각 {common, starter, solution}/ + SRCS(링크 줄)
  demo_link_errors.sh → .txt   s1~s3 의 ld64 / GNU ld 메시지 원문
  demo_weak.sh        → .txt   s4 의 Mach-O / ELF 근거, 연습용 실험
```

check.sh 는 시나리오마다 `common/` 위에 변형(`starter/` 또는 `solution/`)을 덮어 `/tmp/TOOL-B05_work/<변형>/` 에 펼친 뒤, `SRCS` 의 파일을 `-std=c11 -Wall -Wextra -Werror -O2` 로 하나씩 컴파일하고 링크한다. 그리고 링크 성공 여부 · 실행 출력 · `nm` 근거를 검사한다. Part 2 는 같은 소스를 Lima VM(`elinux`)에서 gcc 15.2 / GNU ld 2.46 / arm-none-eabi-gcc 14.2.1 로 다시 확인한다 (VM 이 꺼져 있으면 SKIP).

<!-- code: check.sh -->

고칠 파일 (starter):

<!-- code: s1_undefined/starter/SRCS -->
<!-- code: s1_undefined/starter/main.c -->
<!-- code: s1_undefined/starter/adc.h -->
<!-- code: s1_undefined/starter/adc.c -->
<!-- code: s1_undefined/starter/sensor.c -->
<!-- code: s1_undefined/starter/uart.c -->
<!-- code: s2_multidef/starter/config.h -->
<!-- code: s2_multidef/starter/config.c -->
<!-- code: s3_mangling/starter/ring.h -->
<!-- code: s4_weak/starter/uart.c -->
<!-- code: s4_weak/starter/SRCS -->

그대로 두는 핵심 파일 (읽기용):

<!-- code: s3_mangling/common/app.cpp -->
<!-- code: s4_weak/common/startup.c -->
<!-- code: s4_weak/common/irq.h -->

```bash
bash check.sh starter     # 깨진 빌드: FAIL 이 나와야 정상, exit 1
bash check.sh solution    # 전부 PASS, exit 0
```

<details><summary>bash check.sh starter — 실제 출력 (2/27 passed, exit 1)</summary>

```text
== TOOL-B05 check: starter   (work dir /tmp/TOOL-B05_work/starter)
-- Part 1: 이 맥 (Apple clang 21 + ld64, Mach-O) --
FAIL  s1 links (undefined symbol 0개)
        | Undefined symbols for architecture arm64:
        |   "_adc_read_raw", referenced from:
        |       _main in main.o
        |   "_crc8", referenced from:
        |       _main in main.o
FAIL  s1 output
FAIL  s1 nm: app 에 'T _sensor_selftest'
FAIL  s1 nm: app 에 'T _crc8'
FAIL  s1 nm: app 에 'T _uart_putc'
FAIL  s1 nm: app 에 'T _adc_read_raw'
FAIL  s1 nm: adc.o 의 adc_read_raw 가 전역(T, 소문자 t 아님)
FAIL  s2 links (맥 기본: Apple clang 은 -fcommon)
        | duplicate symbol '_g_log_level' in:
        |     /private/tmp/TOOL-B05_work/starter/s2/tick.o
        |     /private/tmp/TOOL-B05_work/starter/s2/main.o
        |     /private/tmp/TOOL-B05_work/starter/s2/config.o
        |     /private/tmp/TOOL-B05_work/starter/s2/log.o
FAIL  s2 links with -fno-common (GCC 10+/Clang 11+ 기본과 같은 규칙)
        | duplicate symbol '_g_ticks' in:
        |     /private/tmp/TOOL-B05_work/starter/s2_nocommon/log.o
        |     /private/tmp/TOOL-B05_work/starter/s2_nocommon/main.o
        |     /private/tmp/TOOL-B05_work/starter/s2_nocommon/config.o
        |     /private/tmp/TOOL-B05_work/starter/s2_nocommon/tick.o
FAIL  s2 output (세 파일이 같은 변수를 봄)
FAIL  s2 nm: _g_log_level 정의가 .o 전체에서 딱 1개 (static 사본 0개)
FAIL  s2 nm: _g_ticks 정의가 .o 전체에서 딱 1개 (static 사본 0개)
FAIL  s2 nm: _g_err_count 정의가 .o 전체에서 딱 1개 (static 사본 0개)
FAIL  s3 links (C++ main + C ring.c)
        | Undefined symbols for architecture arm64:
        |   "ring_pop(ring*, unsigned char*)", referenced from:
        |       _main in app.o
        |       _main in app.o
        |    NOTE: found '_ring_pop' in ring.o, declaration possibly missing 'extern "C"'
FAIL  s3 output
FAIL  s3 nm: app.o 가 C 이름 '_ring_push' 를 찾음 (U)
FAIL  s3 nm: app.o 에 맹글링된 ring 심볼(__Z*ring*) 0개
PASS  s4 links (weak 기본값 덕분에 버그가 있어도 성공한다)
FAIL  s4 output (기본 핸들러로 샌 인터럽트 0개)
        | uart_rx=0 tim2=0 default_hits=3 last_unhandled=5
FAIL  s4 nm -m: _USART1_IRQHandler 가 'weak external' 이 아닌 'external'
FAIL  s4 nm -m: _TIM2_IRQHandler 가 'weak external' 이 아닌 'external'
-- Part 2: Lima VM elinux (GNU 툴체인, ELF) --
FAIL  [VM gcc/GNU ld] s1_undefined links + output
        | /usr/bin/aarch64-linux-gnu-ld.bfd: main.o: in function `main':
        | main.c:(.text.startup+0x8): undefined reference to `sensor_selftest'
        | /usr/bin/aarch64-linux-gnu-ld.bfd: main.c:(.text.startup+0x2c): undefined reference to `crc8'
        | /usr/bin/aarch64-linux-gnu-ld.bfd: main.c:(.text.startup+0x44): undefined reference to `adc_read_raw'
        | /usr/bin/aarch64-linux-gnu-ld.bfd: uart.o: in function `uart_puts':
FAIL  [VM gcc/GNU ld] s2_multidef links + output
        | In file included from tick.c:2:
        | config.h:7:17: error: ‘g_err_count’ defined but not used [-Werror=unused-variable]
        |     7 | static unsigned g_err_count;     /* "static 붙이면 에러 안 나던데?" */
        |       |                 ^~~~~~~~~~~
        | cc1: all warnings being treated as errors
FAIL  [VM g++/GNU ld] s3_mangling links + output
        | /usr/bin/aarch64-linux-gnu-ld.bfd: app.o: in function `main':
        | app.cpp:(.text.startup+0x34): undefined reference to `ring_init(ring*, unsigned char*, unsigned long)'
        | /usr/bin/aarch64-linux-gnu-ld.bfd: app.cpp:(.text.startup+0x50): undefined reference to `ring_push(ring*, unsigned char)'
        | /usr/bin/aarch64-linux-gnu-ld.bfd: app.cpp:(.text.startup+0x74): undefined reference to `ring_pop(ring*, unsigned char*)'
        | /usr/bin/aarch64-linux-gnu-ld.bfd: ring.o: in function `ring_push':
PASS  [VM arm-none-eabi] s4_weak links (weak 기본값 덕분에 늘 성공)
FAIL  [VM arm-none-eabi-nm] USART1_IRQHandler 이 아직 weak alias: 00008000 W USART1_IRQHandler
FAIL  [VM arm-none-eabi-nm] TIM2_IRQHandler 이 아직 weak alias: 00008000 W TIM2_IRQHandler
2/27 passed
```

</details>

PASS 두 개가 둘 다 **s4 링크 성공**이라는 점이 이 레슨의 요점 하나다: weak 버그는 링크 단계에서 안 보인다. (`duplicate symbol` 아래 .o 목록의 **순서**는 실행할 때마다 달랐다 — 여러 번 돌려 본 실측. 목록의 집합만 의미가 있다.)

## 4. 힌트

<details><summary>힌트 1 — 메시지에서 세 가지를 뽑아라</summary>

ld64: `"_crc8", referenced from: _main in main.o` → 심볼 `_crc8`(맥은 C 이름 앞에 `_`), 참조하는 쪽 `main` 함수 in `main.o`. GNU ld: `main.c:(.text.startup+0x2c): undefined reference to 'crc8'` → 참조 위치가 섹션+오프셋까지 나온다. 그 다음 질문은 하나: "그 이름의 **정의**는 어느 .o 에 있어야 하나? 그 .o 는 링크 줄에 있나? 있다면 `nm` 에 `T` 로 보이나?"
</details>

<details><summary>힌트 2 — nm 글자 세 개로 대부분 끝난다</summary>

`U` = 여기서 쓰는데 정의는 밖에 있음, `T`/`D`/`B` = 전역 정의(밖에서 찾을 수 있음), 소문자 `t`/`d`/`b` = 파일 안 전용(`static`, 밖에서는 안 보임). `nm -u x.o` 는 U 만, 맥 `nm -m` 은 `external` / `non-external` / `weak external` 을 글로 보여 준다. `-O2` 에서는 static 함수가 인라인돼 심볼 자체가 없을 수도 있다 — `-O0` 로 다시 보면 `t` 가 나온다.
</details>

<details><summary>힌트 3 — 헤더에는 "선언", .c 한 곳에 "정의"</summary>

`int x = 2;` 와 `int x;`(파일 범위) 는 둘 다 **정의**다(뒤의 것은 잠정 정의, tentative definition). 헤더에는 `extern int x;` 만 두고, 정의는 .c 파일 **하나**에. `static int x;` 를 헤더에 두면 링크 에러는 사라지지만 include 한 파일마다 **다른 변수**가 생긴다.
</details>

<details><summary>힌트 4 — C++ 이 찾는 이름은 C 이름이 아니다</summary>

`nm app.o | c++filt` 로 U 심볼을 풀어 보라. `__Z9ring_pushP4ringh` = `ring_push(ring*, unsigned char)`. C 쪽 정의는 `_ring_push` 다. 헤더 선언을 `#ifdef __cplusplus extern "C" { #endif` 로 감싸면 C++ 컴파일러도 C 이름을 쓴다 — 그 블록 안의 선언을 **정의**하는 C++ 함수(콜백)도 C 이름이 된다.
</details>

<details><summary>힌트 5 — weak 는 "아무도 안 주면 내가 쓴다"</summary>

`./app` 출력의 `last_unhandled=5` 는 벡터 5번(TIM2)이 마지막으로 기본 핸들러로 갔다는 뜻. `nm -m app | grep IRQHandle` 로 최종 이미지에 무엇이 남았는지 보라. 철자 하나 다른 `_USART1_IRQHandle` 은 아무도 부르지 않는 평범한 함수다. 그리고 `SRCS` 에 `timer.c` 가 있나?
</details>

## 5. 풀이

### 링커 규칙 한 장 (실측)

| 상황 | ld64 (맥) | GNU ld (VM) | 근거 |
|---|---|---|---|
| U 인데 정의 0개 | 에러 `Undefined symbols` | 에러 `undefined reference` | s1 |
| 강한 정의 2개 (.o 끼리) | 에러 `duplicate symbol` | 에러 `multiple definition … first defined here` | s2, 연습 3 c5 |
| common(`C`) 여러 개 | 합쳐서 1개 | `-fcommon` 일 때 합쳐서 1개 | s2 반쯤 고친 상태 |
| common + 초기값 있는 정의 | 정의가 이김 (`x=5`) | 정의가 이김 (`x=5`) | 아래 실험 |
| weak + strong | strong 이 이김 (`f=3`) | strong 이 이김 (`f=3`) | s4 |
| weak + weak | 링크 줄에서 **먼저 나온 것** (`w1 w2`→1, `w2 w1`→2) | 같음 | 아래 실험 |
| 다른 파일의 `static`(`t`/`b`) | 안 보임 → undefined 또는 각자 사본 | 같음 | s1, s2 |

"common + 정의", "weak + strong", "weak + weak" 줄은 `demo_link_errors.sh` 의 "링커 규칙 실험" 에서 `int x;`+`int x = 5;`, weak `f()` 두 개(1, 2)와 strong `f()`(3)를 순서만 바꿔 링크해 본 실측이다 (맥 `cc -fcommon`, VM `gcc -fcommon` 모두 `x=5 f=1` / `f=2` / `f=3` 로 동일).

### Part A — s1: 메시지 4줄, 원인 4개

ld64(맥, ld-1266.8)와 GNU ld(VM, ld.bfd 2.46) 원문을 나란히 놓으면 (`demo_link_errors.txt`):

```text
[ld64]
Undefined symbols for architecture arm64:
  "_adc_read_raw", referenced from:
      _main in main.o
  "_crc8", referenced from:
      _main in main.o
  "_sensor_selftest", referenced from:
      _main in main.o
  "_uart_putc", referenced from:
      _uart_puts in uart.o
ld: symbol(s) not found for architecture arm64

[GNU ld]
/usr/bin/aarch64-linux-gnu-ld.bfd: main.o: in function `main':
main.c:(.text.startup+0x8): undefined reference to `sensor_selftest'
/usr/bin/aarch64-linux-gnu-ld.bfd: main.c:(.text.startup+0x2c): undefined reference to `crc8'
/usr/bin/aarch64-linux-gnu-ld.bfd: main.c:(.text.startup+0x44): undefined reference to `adc_read_raw'
/usr/bin/aarch64-linux-gnu-ld.bfd: uart.o: in function `uart_puts':
uart.c:(.text+0x40): undefined reference to `uart_putc'
collect2: error: ld returned 1 exit status
```

읽는 법: ld64 는 **심볼별**로 묶어서 "누가 참조했나" 를 나열한다. GNU ld 는 **참조 위치별**로 `파일:(섹션+오프셋)` 을 찍는다 — `-g` 로 빌드하면 DWARF 줄 정보로 `main.c:14:(.text.startup+0x2c): undefined reference to 'crc8'` 처럼 줄 번호까지 나온다(VM 실측). `main` 이 `.text.startup` 에 있는 건 GCC 가 `-O2` 에서 `main` 을 따로 두는 섹션이다. `collect2` 는 gcc 가 ld 를 부르는 래퍼라서 마지막 줄이 늘 그 모양이다.

원인별 근거와 수정:

| 심볼 | `nm` 근거 (실측) | 원인 | 고친 곳 |
|---|---|---|---|
| `_sensor_selftest` | `nm -u main.o` 에 있음, 어느 .o 에도 T 없음 | 선언만 있고 정의 없음 | `sensor.c` 에 정의 |
| `_uart_putc` | `uart.o`: `T _uart_put_char` + `U _uart_putc` | 정의 이름 오타 (자기 파일에서 자기를 못 찾음) | `uart.c` 이름을 헤더와 같게 |
| `_crc8` | `crc.c` 는 있지만 `SRCS` 에 없음 → crc.o 가 링크 줄에 없음 | 링크 줄 누락 | `SRCS` 에 `crc.c` 추가 |
| `_adc_read_raw` | `-O2`: `adc.o` 에 심볼 없음(인라인됨), `-O0`: `t _adc_read_raw` | 다른 파일의 `static` 함수 | `adc.h` 에 원형, `adc.c` 에서 static 제거, main 의 손 선언 삭제 |

`adc.o` 를 두 최적화 레벨로 본 실측:

```text
$ nm adc.o                        # -O2
0000000000000000 T _adc_read_mv
$ cc -std=c11 -O0 -c adc.c -o adc_O0.o && nm adc_O0.o
0000000000000000 T _adc_read_mv
0000000000000024 t _adc_read_raw  ← 소문자 t: 이 파일 안에서만 보이는 정의
```

`static` 은 "내부 링크(internal linkage)" 다 (C-B10). 링커 입장에서 `t` 는 다른 파일의 U 를 채울 자격이 없다. 그래서 "정의가 분명히 있는데 undefined" 가 된다. 고치는 방법은 둘 — 공개 API 로 올리거나(이 레슨의 답), 이미 있는 공개 API(`adc_read_mv`)를 쓰게 바꾸거나. `main.c` 의 `extern int adc_read_raw(void);` 같은 **손 선언은 지운다**: 헤더 하나에만 선언이 있어야 시그니처가 바뀔 때 컴파일러가 잡아 준다.

`uart_put_char` 오타는 `-Wmissing-prototypes` 를 켰다면 컴파일 단계에서 먼저 보인다 — 실측: `uart.c:5:6: warning: no previous prototype for function 'uart_put_char' [-Wmissing-prototypes]` (TOOL-B02).

### Part B-1 — s2: 헤더에 정의한 변수

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">config.h 를 4개 .c 가 include → 같은 정의가 4번 생긴다 (맥 nm 실측 글자)</text>
  <rect x="10" y="35" width="200" height="88" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="55" fill="currentColor" font-weight="bold">config.h (starter)</text>
  <text x="20" y="75" fill="currentColor" font-family="monospace" font-size="12">int g_log_level = 2;</text>
  <text x="20" y="93" fill="currentColor" font-family="monospace" font-size="12">unsigned long g_ticks;</text>
  <text x="20" y="111" fill="currentColor" font-family="monospace" font-size="12">static unsigned g_err_count;</text>
  <rect x="240" y="35" width="230" height="88" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="250" y="55" fill="currentColor" font-weight="bold">main.o tick.o log.o config.o 각각</text>
  <text x="250" y="75" fill="currentColor" font-family="monospace" font-size="12">D _g_log_level   ×4</text>
  <text x="250" y="93" fill="currentColor" font-family="monospace" font-size="12">C _g_ticks       ×4 (-fcommon)</text>
  <text x="250" y="111" fill="currentColor" font-family="monospace" font-size="12">b _g_err_count   쓰는 파일마다 사본</text>
  <path d="M210 79 L238 79" stroke="currentColor" stroke-width="2"/>
  <rect x="500" y="35" width="230" height="88" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="510" y="55" fill="currentColor" font-weight="bold">링커 판정</text>
  <text x="510" y="75" fill="currentColor" font-size="12">D×4 → 늘 duplicate</text>
  <text x="510" y="93" fill="currentColor" font-size="12">C×4 → 합침 / -fno-common: 에러</text>
  <text x="510" y="111" fill="currentColor" font-size="12">b → 에러 없음, 사본 여러 개</text>
  <path d="M470 79 L498 79" stroke="currentColor" stroke-width="2"/>
  <rect x="10" y="145" width="720" height="70" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="167" fill="currentColor" font-weight="bold">반쯤 고친 상태 (g_log_level 만 extern) — 맥 기본 빌드가 "성공"</text>
  <text x="20" y="187" fill="currentColor" font-family="monospace" font-size="12">ticks=5 errs=0 level=2     ← log.c 의 g_err_count 는 2, main.c 의 g_err_count 는 0</text>
  <text x="20" y="205" fill="currentColor" font-size="12">같은 소스를 펌웨어 gcc(-fno-common 기본)로 빌드하면 g_ticks 가 multiple definition</text>
  <rect x="10" y="230" width="720" height="60" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="20" y="252" fill="currentColor" font-weight="bold">정답: config.h = extern 선언 3줄,  config.c = 정의 3줄</text>
  <text x="20" y="274" fill="currentColor" font-size="12">nm: config.o 만 D/C(또는 S) 정의, 나머지 .o 는 전부 U → 정의 1개 · 사본 0개</text>
</svg>
```

맥 starter 의 실제 메시지는 `duplicate symbol '_g_log_level' in:` + .o 4개다. `-fno-common` 으로 컴파일하면 `_g_ticks` 도 추가돼 `ld: 2 duplicate symbols`. GNU ld(gcc 15.2, 기본 `-fno-common`) 는 쌍마다 한 줄씩 6줄을 찍는다:

```text
/usr/bin/aarch64-linux-gnu-ld.bfd: tick.o:(.bss+0x0): multiple definition of `g_ticks'; main.o:(.bss+0x0): first defined here
/usr/bin/aarch64-linux-gnu-ld.bfd: tick.o:(.data+0x0): multiple definition of `g_log_level'; main.o:(.data+0x0): first defined here
... (log.o, config.o 도 같은 모양)
```

`(.bss+0x0)` / `(.data+0x0)` 가 바로 "이 변수의 정의가 이 .o 의 이 섹션에 있다" 는 근거다 (초기값 없는 변수 → .bss, 있는 변수 → .data; TOOL-B06).

**`-fcommon` 이야기.** 초기값 없는 파일 범위 변수(`int x;`)를 옛 C 컴파일러들은 "common" 심볼(nm 글자 `C`)로 내보냈고, 링커는 같은 이름의 common 을 하나로 합쳤다 — 그래서 헤더에 `int x;` 를 써도 링크가 됐다. 이건 C 표준이 보장하는 동작이 아니라 Unix 관습이다(C11 부록 J.5.11 "common extensions" 에 소개됨). GCC 10 과 Clang 11 부터 기본값이 `-fno-common` 으로 바뀌었다고 알려져 있다 (버전은 릴리스 노트 확인 권장). 이 맥에서 실측한 사실은 이렇다:

```text
$ cc -### -c tick.c 2>&1 | tr ' ' '\n' | grep -i common
"-fcommon"                                         ← Apple clang 은 Darwin 에서 지금도 -fcommon 을 넘긴다
$ nm -m tick.o | grep g_                           (기본)
0000000000000008 (common) (alignment 2^3) external _g_ticks
$ nm -m tick.o | grep g_                           (-fno-common)
0000000000000038 (__DATA,__common) external _g_ticks
```

같은 소스를 `clang --target=aarch64-linux-gnu -c` 로 만들면 `B`(.bss) 가 나오고, VM gcc 도 `B` 다. 즉 **맥에서 링크되는 헤더가 펌웨어 빌드(arm-none-eabi-gcc 14)에서는 깨진다**. 그래서 check.sh 는 s2 를 두 번(기본, `-fno-common`) 링크한다.

**`static` 으로 고친 척.** 반쯤 고친 상태(데모)에서 맥 기본 빌드는 링크되고 이렇게 나온다:

```text
$ cc main.o tick.o log.o config.o -o app && ./app
E[5] sensor timeout
E[5] crc mismatch
ticks=5 errs=0 level=2
$ nm -m main.o | grep g_err_count; nm -m log.o | grep g_err_count
0000000000000088 (__DATA,__bss) non-external _g_err_count      ← log.o 의 사본 하나만 보인다
```

`main.o` 에는 `g_err_count` 심볼이 아예 없다: main.c 안에서 그 static 변수에 쓰는 코드가 없으니 컴파일러가 "항상 0" 으로 접어 버렸다. log.c 의 사본은 2 까지 올라가지만 main 은 0 을 출력한다. 링크 에러는 "같은 이름이 둘" 이라고 알려 주는 **고마운 신호**였는데, static 은 그 신호만 끄고 버그는 남긴다. 펌웨어로 치면 ISR 이 올리는 에러 카운터를 메인 루프가 영원히 0 으로 보는 상황이다. 덤으로 GCC 는 이걸 경고로 잡는다 — `config.h:7:17: warning: 'g_err_count' defined but not used [-Wunused-variable]` (tick.c 처럼 안 쓰는 파일에서). 이 맥의 clang 은 헤더 안 static 에는 이 경고를 내지 않았다 (실측: `-Wall -Wextra -Werror` 통과).

### Part B-2 — s3: C++ 맹글링, 양방향

```text
$ nm app.o | grep ring
0000000000000000 T __Z18ring_overflow_hookP4ringh
                 U __Z8ring_popP4ringPh
                 U __Z9ring_initP4ringPhm
                 U __Z9ring_pushP4ringh
$ nm app.o | grep ring | c++filt
0000000000000000 T ring_overflow_hook(ring*, unsigned char)
                 U ring_pop(ring*, unsigned char*)
                 U ring_init(ring*, unsigned char*, unsigned long)
                 U ring_push(ring*, unsigned char)
$ nm ring.o | grep ring_
0000000000000000 T _ring_init
                 U _ring_overflow_hook
0000000000000074 T _ring_pop
0000000000000010 T _ring_push
```

C++ 는 오버로딩 때문에 매개변수 타입을 이름에 넣는다 (Itanium C++ ABI: `_Z` + 길이 + 이름 + 타입 코드, `P4ring` = `ring*`, `h` = `unsigned char`, `m` = `unsigned long`; 맥은 앞에 `_` 하나 더). 그러면 두 방향 모두 깨진다:

- C++ → C: app.o 는 `__Z9ring_pushP4ringh` 를 찾는데 ring.o 에는 `_ring_push` 만 있다.
- C → C++: ring.o 는 `_ring_overflow_hook` 을 찾는데 app.o 는 `__Z18ring_overflow_hookP4ringh` 로 정의했다.

ld64 는 친절하게 `NOTE: found '_ring_pop' in ring.o, declaration possibly missing 'extern "C"'` 까지 붙여 준다 (C++ → C 방향만; 반대 방향 `_ring_overflow_hook` 에는 NOTE 가 없다). GNU ld 는 기본으로 디맹글된 이름 ``undefined reference to `ring_init(ring*, unsigned char*, unsigned long)'`` 을 보여 주고, `-Wl,--no-demangle` 이면 `_Z9ring_initP4ringPhm` 원형이 나온다. **괄호와 타입이 붙은 undefined 이름 = C++ 이름으로 찾았다**는 신호다.

`ring.h` 를 `extern "C" { }` 로 감싸면 app.cpp 의 호출도, app.cpp 의 `ring_overflow_hook` **정의**도 C 이름이 된다 (헤더의 extern "C" 선언을 먼저 본 함수는 정의할 때 다시 안 써도 C 링크를 유지). 결과 `nm app.o`: `U _ring_init`, `U _ring_pop`, `U _ring_push`, `T _ring_overflow_hook` — 체크가 확인하는 그대로. `extern "C"` 의 자세한 규칙(오버로드 불가, 구조체 레이아웃 호환)은 CPP-B02.

### Part C — s4: weak 기본 핸들러

Cortex-M 스타트업(벤더 `startup_*.s` 나 CMSIS 의 C 버전)은 벡터 테이블의 모든 칸에 핸들러 **이름**을 넣고, 각 이름을 weak 로 `Default_Handler` 에 묶는다. 앱이 같은 이름의 강한 정의를 주면 링커가 그걸 고른다. 이 레슨의 `startup.c` 는 매크로 하나로 두 포맷을 다 지원한다:

```c
#if defined(__APPLE__)   /* Mach-O 에는 alias 속성이 없다 → weak "정의" 로 같은 효과 */
#define WEAK_DEFAULT(name) __attribute__((weak)) void name(void) { Default_Handler(); }
#else                    /* ELF: 이름만 Default_Handler 에 붙인다 */
#define WEAK_DEFAULT(name) void name(void) __attribute__((weak, alias("Default_Handler")));
#endif
```

맥에서 alias 를 쓰면 실제로 이렇게 거절된다:

```text
$ cc -c alias.c
alias.c:2:51: error: aliases are not supported on darwin
$ clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -c alias.c -o alias_elf.o && nm alias_elf.o
00000000 T Default_Handler
00000000 W USART1_IRQHandler      ← 같은 주소, 글자 W
```

**맥(Mach-O)의 근거는 `nm -m`.** 함정: 맥 `nm` 은 weak 정의도 그냥 `T` 로 보여 준다(`nm startup.o` 실측: `T _USART1_IRQHandler`). `nm -m` 이라야 `weak external` 이 보인다:

```text
$ ./app                                   (starter)
uart_rx=0 tim2=0 default_hits=3 last_unhandled=5
$ nm -m app | grep -E 'IRQHandle'
00000001000006b0 (__TEXT,__text) weak external _TIM2_IRQHandler
00000001000007b8 (__TEXT,__text) external _USART1_IRQHandle       ← 오타: 아무도 안 부르는 고아 함수
0000000100000688 (__TEXT,__text) weak external _USART1_IRQHandler ← 기본값이 살아남음
$ ./app                                   (solution)
uart_rx=1 tim2=2 default_hits=0 last_unhandled=-1
$ nm -m app | grep -E 'IRQHandle'
0000000100000770 (__TEXT,__text) external _TIM2_IRQHandler
0000000100000768 (__TEXT,__text) external _USART1_IRQHandler
```

solution 에서는 weak 사본이 최종 이미지에서 아예 사라지고 강한 정의만 남는다. `default_hits=3` 은 USART1 한 번 + TIM2 두 번이 전부 기본 핸들러로 갔다는 뜻이고, `last_unhandled=5` 는 마지막이 벡터 5(TIM2)였다는 뜻 — 실제 칩이라면 `Default_Handler` 안에서 IPSR(현재 예외 번호)을 읽어 크래시 로그에 남기는 바로 그 정보다.

**ELF(arm-none-eabi-gcc 14.2.1, VM)의 근거는 `W` 와 같은 주소.**

```text
$ arm-none-eabi-readelf -s startup.o | grep -E 'Num|Handler'
   Num:    Value  Size Type    Bind   Vis      Ndx Name
    16: 00000001    24 FUNC    GLOBAL DEFAULT    1 Default_Handler
    17: 00000001    24 FUNC    WEAK   DEFAULT    1 TIM2_IRQHandler
    18: 00000001    24 FUNC    WEAK   DEFAULT    1 USART1_IRQHandler
    ...
$ arm-none-eabi-nm -n fw.elf | grep -E 'Handler|IRQHandle'      (starter)
00008000 T Default_Handler
00008000 W TIM2_IRQHandler
00008000 W USART1_IRQHandler
00008018 T Reset_Handler
00008058 T USART1_IRQHandle
$ arm-none-eabi-objdump -s -j .rodata fw.elf | tail -2           (starter: 벡터 테이블)
 8060 19800000 01800000 01800000 01800000  ................
 8070 01800000 01800000                    ........
```

alias 는 "같은 코드에 붙은 두 번째 이름" 이라 크기·주소가 `Default_Handler` 와 똑같다(Value 1 = 주소 0 + Thumb 비트). 링크된 벡터 테이블의 여섯 칸을 리틀 엔디언으로 읽으면 바로 보인다:

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">fw.elf 의 g_vectors[] (.rodata, objdump -s 실측) — 값 = 핸들러 주소 | 1 (Thumb)</text>
  <text x="10" y="45" fill="currentColor" font-weight="bold">starter (0x8060~)</text>
  <text x="380" y="45" fill="currentColor" font-weight="bold">solution (0x8064~)</text>
  <g font-family="monospace" font-size="12">
    <rect x="10" y="55" width="330" height="22" class="f-mut" stroke="currentColor"/>
    <text x="18" y="71" fill="currentColor">[0] Reset     0x00008019 → Reset_Handler</text>
    <rect x="10" y="77" width="330" height="22" class="f-warn" stroke="currentColor"/>
    <text x="18" y="93" fill="currentColor">[1] NMI       0x00008001 → Default</text>
    <rect x="10" y="99" width="330" height="22" class="f-warn" stroke="currentColor"/>
    <text x="18" y="115" fill="currentColor">[2] HardFault 0x00008001 → Default</text>
    <rect x="10" y="121" width="330" height="22" class="f-warn" stroke="currentColor"/>
    <text x="18" y="137" fill="currentColor">[3] SysTick   0x00008001 → Default</text>
    <rect x="10" y="143" width="330" height="22" class="f-warn" stroke="currentColor"/>
    <text x="18" y="159" fill="currentColor">[4] USART1    0x00008001 → Default (!)</text>
    <rect x="10" y="165" width="330" height="22" class="f-warn" stroke="currentColor"/>
    <text x="18" y="181" fill="currentColor">[5] TIM2      0x00008001 → Default (!)</text>
    <rect x="380" y="55" width="350" height="22" class="f-mut" stroke="currentColor"/>
    <text x="388" y="71" fill="currentColor">[0] Reset     0x00008019 → Reset_Handler</text>
    <rect x="380" y="77" width="350" height="22" class="f-warn" stroke="currentColor"/>
    <text x="388" y="93" fill="currentColor">[1] NMI       0x00008001 → Default</text>
    <rect x="380" y="99" width="350" height="22" class="f-warn" stroke="currentColor"/>
    <text x="388" y="115" fill="currentColor">[2] HardFault 0x00008001 → Default</text>
    <rect x="380" y="121" width="350" height="22" class="f-warn" stroke="currentColor"/>
    <text x="388" y="137" fill="currentColor">[3] SysTick   0x00008001 → Default</text>
    <rect x="380" y="143" width="350" height="22" class="f-ok" stroke="currentColor"/>
    <text x="388" y="159" fill="currentColor">[4] USART1    0x00008059 → USART1_IRQHandler</text>
    <rect x="380" y="165" width="350" height="22" class="f-ok" stroke="currentColor"/>
    <text x="388" y="181" fill="currentColor">[5] TIM2      0x0000805f → TIM2_IRQHandler</text>
  </g>
  <text x="10" y="212" fill="currentColor">Default_Handler = 0x8000 (nm), USART1_IRQHandler = 0x8058, TIM2_IRQHandler = 0x805e.</text>
  <text x="10" y="232" fill="currentColor">starter 의 USART1_IRQHandle(0x8058) 는 테이블 어디에도 없다 — 이름이 다르면 링커에겐 남이다.</text>
  <text x="10" y="252" fill="currentColor">NMI/HardFault/SysTick 이 Default 인 건 정상: 앱이 그 핸들러를 안 썼을 뿐.</text>
</svg>
```

**왜 링크 에러가 안 나나.** weak 기본값이 "이 이름의 정의는 있다" 를 늘 보장하기 때문이다. 그게 weak 의 목적(앱이 안 쓰는 수십 개 IRQ 를 일일이 정의하지 않아도 됨)이자 위험이다. starter 의 두 원인은 링커 입장에서 이렇게 보인다:

1. `USART1_IRQHandle` (r 빠짐) — 링커는 "새 함수 하나 더" 로 본다. 강한 `USART1_IRQHandler` 가 없으니 weak 가 남는다.
2. `timer.c` 가 `SRCS` 에 없음 — 그 .o 가 링크 줄에 없으니 강한 `TIM2_IRQHandler` 도 없다. s1 의 crc.c 누락과 **같은 실수**인데, 이번엔 weak 가 구멍을 메워서 조용하다.

**두 버그를 빌드에서 잡는 법** (9. 추가 연습에서 직접 만든다):

- 컴파일 단계: 핸들러 원형이 있는 헤더(`irq.h`)를 include 하고 `-Wmissing-prototypes` 를 켜면 오타가 경고로 나온다 — `uart.c:5:6: warning: no previous prototype for function 'USART1_IRQHandle' [-Wmissing-prototypes]` (실측). 하지만 **빠진 파일은 못 잡는다** (컴파일할 파일이 없으니까).
- 링크 후: 최종 이미지에서 "반드시 강해야 하는 핸들러 목록" 을 `nm` 으로 확인한다 (ELF: `W` 이거나 주소가 `Default_Handler` 와 같으면 실패). 이건 두 버그 모두 잡는다.

강한 정의가 **둘**이면 그건 에러다 — 실측(VM): `dup.c:(.text+0x0): multiple definition of 'USART1_IRQHandler'; ...uart.c:(.text+0x0): first defined here`. weak 는 "하나만 있으면 조용히 진다" 일 뿐, strong 끼리의 충돌을 숨기지는 않는다.

실제 ST 의 GCC 스타트업 어셈블리는 같은 일을 `.weak USART1_IRQHandler` + `.thumb_set USART1_IRQHandler,Default_Handler` 로 한다(기억에 의존, CubeMX 생성 파일로 확인 권장). 벡터 테이블을 `.isr_vector` 섹션에 놓고 링커 스크립트로 0x08000000 에 고정하는 부분은 BM-B08 / TOOL-M01, 테이블 첫 칸이 초기 SP 이고 핸들러 주소에 Thumb 비트가 붙는 이유는 ARM-M08.

### 고친 뒤 — bash check.sh solution (실제 출력)

```text
== TOOL-B05 check: solution   (work dir /tmp/TOOL-B05_work/solution)
-- Part 1: 이 맥 (Apple clang 21 + ld64, Mach-O) --
PASS  s1 links (undefined symbol 0개)
PASS  s1 output
PASS  s1 nm: app 에 'T _sensor_selftest'
PASS  s1 nm: app 에 'T _crc8'
PASS  s1 nm: app 에 'T _uart_putc'
PASS  s1 nm: app 에 'T _adc_read_raw'
PASS  s1 nm: adc.o 의 adc_read_raw 가 전역(T, 소문자 t 아님)
PASS  s2 links (맥 기본: Apple clang 은 -fcommon)
PASS  s2 links with -fno-common (GCC 10+/Clang 11+ 기본과 같은 규칙)
PASS  s2 output (세 파일이 같은 변수를 봄)
PASS  s2 nm: _g_log_level 정의가 .o 전체에서 딱 1개 (static 사본 0개)
PASS  s2 nm: _g_ticks 정의가 .o 전체에서 딱 1개 (static 사본 0개)
PASS  s2 nm: _g_err_count 정의가 .o 전체에서 딱 1개 (static 사본 0개)
PASS  s3 links (C++ main + C ring.c)
PASS  s3 output
PASS  s3 nm: app.o 가 C 이름 '_ring_push' 를 찾음 (U)
PASS  s3 nm: app.o 에 맹글링된 ring 심볼(__Z*ring*) 0개
PASS  s4 links (weak 기본값 덕분에 버그가 있어도 성공한다)
PASS  s4 output (기본 핸들러로 샌 인터럽트 0개)
PASS  s4 nm -m: _USART1_IRQHandler 가 'weak external' 이 아닌 'external'
PASS  s4 nm -m: _TIM2_IRQHandler 가 'weak external' 이 아닌 'external'
-- Part 2: Lima VM elinux (GNU 툴체인, ELF) --
PASS  [VM gcc/GNU ld] s1_undefined links + output
PASS  [VM gcc/GNU ld] s2_multidef links + output
PASS  [VM g++/GNU ld] s3_mangling links + output
PASS  [VM arm-none-eabi] s4_weak links (weak 기본값 덕분에 늘 성공)
PASS  [VM arm-none-eabi-nm] USART1_IRQHandler = T, Default_Handler(00008000) 와 다른 주소
PASS  [VM arm-none-eabi-nm] TIM2_IRQHandler = T, Default_Handler(00008000) 와 다른 주소
27/27 passed
```

전체 실행 시간은 이 맥에서 starter 약 3 초 · solution 약 5 초 · practice 약 6 초 (VM 호출 포함, `time` 실측, 다른 작업과 병행 중이라 오차 있음).

## 6. 정답 코드

s1 — 링크 줄, 정의 추가, 오타, static:

<!-- code: s1_undefined/solution/SRCS diff=s1_undefined/starter/SRCS -->
<!-- code: s1_undefined/solution/sensor.c diff=s1_undefined/starter/sensor.c -->
<!-- code: s1_undefined/solution/uart.c diff=s1_undefined/starter/uart.c -->
<!-- code: s1_undefined/solution/adc.h diff=s1_undefined/starter/adc.h -->
<!-- code: s1_undefined/solution/adc.c diff=s1_undefined/starter/adc.c -->
<!-- code: s1_undefined/solution/main.c diff=s1_undefined/starter/main.c -->

s2 — 헤더는 선언, 정의는 config.c 한 곳:

<!-- code: s2_multidef/solution/config.h diff=s2_multidef/starter/config.h -->
<!-- code: s2_multidef/solution/config.c diff=s2_multidef/starter/config.c -->

s3 — extern "C" 가드:

<!-- code: s3_mangling/solution/ring.h diff=s3_mangling/starter/ring.h -->

s4 — 핸들러 이름, 빌드 목록:

<!-- code: s4_weak/solution/uart.c diff=s4_weak/starter/uart.c -->
<!-- code: s4_weak/solution/SRCS diff=s4_weak/starter/SRCS -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 모습 |
|---|---|---|
| undefined 가 나면 쓰는 쪽 파일에 빈 정의를 추가 | 진짜 정의(crc.c)는 여전히 빌드 밖. 두 개가 되는 순간 duplicate | `SRCS`/Makefile 에 crc.c 추가 |
| 다른 파일 함수를 `extern int f(void);` 로 손 선언 | 원형이 두 곳에 생겨 시그니처가 어긋나도 컴파일러가 못 잡는다. static 이면 링크에서 터짐 | 정의 파일의 헤더 하나에만 원형, 그 헤더를 정의 파일도 include |
| 헤더 변수 duplicate 를 `static` 으로 해결 | 파일마다 사본 → `errs=0` 처럼 조용히 틀림 | `extern` 선언 + .c 한 곳 정의 |
| "맥에서 링크되니 됐다" | Apple clang 은 `-fcommon` 이라 헤더의 `int x;` 가 통과. 펌웨어 gcc 는 실패 | CI/로컬에 `-fno-common` 추가해 같은 규칙으로 |
| C 헤더에 `extern "C"` 가드 없이 C++ 에서 include | 호출이 `__Z…` 이름으로 나가 undefined. 콜백 정의도 맹글링 | `#ifdef __cplusplus extern "C" { #endif` … `}` |
| `#include` 를 `extern "C" { }` 로 감싸서 해결 | 그 헤더가 C++ 헤더(템플릿 등)를 include 하면 깨짐. 헤더 자체가 가드를 가져야 함 | 가드는 C 헤더 안에 |
| 핸들러 이름을 손으로 타이핑 | 오타 = 새 함수. weak 가 남아 링크 성공 | 원형 헤더 + `-Wmissing-prototypes`, 링크 후 nm 검사 |
| C++ 파일에 `void TIM2_IRQHandler()` | `__Z15TIM2_IRQHandlerv` 로 맹글링 → weak 가 남음 (연습 1 실측: `tim2=0 default_hits=2`) | `extern "C"` |
| 맥 `nm` 의 T 만 보고 "strong 이네" | Mach-O weak 정의도 `nm` 에선 T | `nm -m` 의 `weak external` 확인 |

## 8. 면접 Q&A

<details><summary>Q1. "undefined reference to `foo'" 를 받았다. 확인 순서는?</summary>

(1) 메시지에서 심볼 이름과 참조하는 .o/함수를 뽑는다. 이름에 괄호·타입이 붙어 있으면 C++ 이름으로 찾은 것 → extern "C" 문제. (2) 정의가 있어야 할 소스를 찾고, 그 .o 가 **링크 줄에 있는지**(Makefile/CMake 소스 목록, 라이브러리 `-l` 순서 — TOOL-B09). (3) 그 .o 에서 `nm` 으로 `T foo` 인지: 없으면 이름 오타·`#ifdef` 로 빠짐, 소문자 `t` 면 static. (4) 아카이브라면 `nm -s lib.a` 로 인덱스에 있는지, 링크 순서가 맞는지.
</details>

<details><summary>Q2. 헤더에 `int g;` 를 두면 링크가 되기도 하고 안 되기도 한다. 왜?</summary>

`int g;` 는 잠정 정의라 `-fcommon` 에서는 common 심볼(`C`)로 나가고 링커가 여러 개를 하나로 합친다. `-fno-common`(GCC 10 / Clang 11 이후 기본으로 알려짐)이면 .bss 의 일반 정의(`B`)라 둘 이상이면 multiple definition. 이 맥의 Apple clang 은 지금도 `-fcommon` 을 넘긴다(`cc -###` 실측)라서 맥에선 되고 arm-none-eabi-gcc 에선 깨진다. 옳은 답은 플래그가 아니라 `extern` 선언 + 한 곳 정의. common 은 크기가 다른 정의끼리도 조용히 합쳐질 수 있어 위험하다 (GNU ld 는 `--warn-common` 으로 경고를 켤 수 있다 — 동작 세부는 확인 권장).
</details>

<details><summary>Q3. `static int x;` 를 헤더에 두는 건 왜 나쁜가? 괜찮은 경우는?</summary>

include 한 .c 마다 독립 변수가 생긴다 — 링크 에러는 없지만 한 파일이 쓴 값을 다른 파일이 못 본다(실측 `errs=0`). RAM 도 파일 수만큼 쓴다. 괜찮은 경우는 `static const` 테이블이나 `static inline` 함수처럼 "파일마다 사본이어도 의미가 같은" 것뿐이다(그래도 큰 const 테이블을 **쓰는** 파일마다 사본이 생겨 Flash 가 늘 수 있음 — 확인은 TOOL-B06 의 size, 줄이는 법은 TOOL-M13).
</details>

<details><summary>Q4. name mangling 이 뭐고 링크 에러를 어떻게 읽나?</summary>

C++ 은 오버로딩·네임스페이스 때문에 심볼 이름에 타입 정보를 넣는다(`_Z9ring_pushP4ringh`). C 컴파일러는 그대로(`ring_push`, 맥은 `_ring_push`). C++ 에서 C 함수를 C++ 선언으로 부르면 다른 이름을 찾게 되어 undefined. `c++filt` 로 풀고, ld64 는 `declaration possibly missing 'extern "C"'` NOTE 까지 준다. 해결은 C 헤더에 `extern "C"` 가드. 반대로 C 가 부르는 콜백을 C++ 에서 정의할 때도 `extern "C"` 가 필요하다(s3 의 `ring_overflow_hook`).
</details>

<details><summary>Q5. weak 심볼은 어떻게 동작하고 언제 쓰나? 위험은?</summary>

링커 규칙: strong 하나 + weak 여럿 → strong. weak 만 여럿 → 하나 고름(실측: 링크 줄에서 먼저 나온 것). strong 둘 → 에러. 쓰는 곳: Cortex-M 스타트업의 기본 IRQ 핸들러, 라이브러리의 오버라이드 가능한 훅(`_write`, `HAL_UART_RxCpltCallback` 같은 weak 콜백), 테스트에서 하드웨어 함수 대체(link seam — CPP-A08). 위험: 이름 오타·빠진 파일·C++ 맹글링이 **링크 에러 없이** 기본 동작으로 떨어진다. 그래서 CI 에서 최종 이미지 `nm` 으로 필수 핸들러가 strong 인지 검사하고, 기본 핸들러는 무한 루프 + 예외 번호 기록으로 "조용히 계속 실행" 하지 않게 만든다.
</details>

<details><summary>Q6. 맥과 리눅스/펌웨어 링커 메시지는 어떻게 다른가?</summary>

ld64: `Undefined symbols for architecture arm64:` + 심볼별 `referenced from: _main in main.o`, 중복은 `duplicate symbol '_x' in:` + .o 목록, C 이름 앞에 `_`. GNU ld: 참조 위치별 ``main.c:(.text.startup+0x2c): undefined reference to `crc8'``, 중복은 ``multiple definition of `g_ticks'; main.o:(.bss+0x0): first defined here`` 처럼 섹션+오프셋, 기본으로 C++ 이름 디맹글. 마지막 줄은 각각 `clang: error: linker command failed` / `collect2: error: ld returned 1 exit status`.
</details>

<details><summary>Q7. 인터럽트가 안 들어오는 것 같다. 빌드 산출물만으로 확인하는 방법?</summary>

(1) `arm-none-eabi-nm -n fw.elf | grep IRQHandler` — `W` 이거나 주소가 `Default_Handler` 와 같으면 기본값에 묶인 것. (2) 벡터 테이블 덤프(`objdump -s -j .isr_vector`)에서 해당 칸 값이 핸들러 주소|1 인지. (3) map 파일에서 핸들러가 어느 .o 에서 왔는지(TOOL-M02). 하드웨어 쪽(NVIC enable, 주변장치 인터럽트 enable, 우선순위)은 그 다음이다. 디버거로 `Default_Handler` 에 브레이크포인트를 걸고 IPSR 을 보면 어느 벡터인지 바로 나온다.
</details>

<details><summary>Q8. `-Wl,-undefined,dynamic_lookup` (맥) 이나 `--unresolved-symbols=ignore-all` (GNU) 을 쓰면?</summary>

링크 에러를 런타임으로 미룬다. 실측: 맥에서 링크 rc 0, 실행하면 `Segmentation fault: 11`(rc 139) — `dyld_info -fixups` 에 `lazy-bind <flat-namespace>/_board_name` 이 남아 있다. VM GNU ld 는 `Illegal instruction`(rc 132). 플러그인처럼 호스트 프로그램이 심볼을 제공하는 경우(TOOL-M06)용이지, 펌웨어 정적 링크에서는 절대 쓰면 안 된다 — 정적 이미지엔 실행 시 심볼을 찾아 줄 로더가 없다.
</details>

## 9. 추가 연습

### 연습 1 — C++ 파일로 옮긴 IRQ 핸들러

TIM2 드라이버를 `timer.cpp` 로 옮겼더니 링크는 되는데 TIM2 가 기본 핸들러로 간다. 실측(버그 버전 `#include "events.h"` + `void TIM2_IRQHandler(void) {…}`): `uart_rx=1 tim2=0 default_hits=2 last_unhandled=5`, `nm timer.o` → `T __Z15TIM2_IRQHandlerv`. 고쳐라.

<details><summary>답</summary>

`extern "C" void TIM2_IRQHandler(void)` 로 정의하고, 원형이 있는 `irq.h`(자체 extern "C" 가드 보유)도 include 한다. 컴파일 단계 가드도 된다: clang 은 C++ 에서도 `-Wmissing-prototypes` 가 `no previous prototype for function 'TIM2_IRQHandler'` 를 냈고(`-Wmissing-declarations` 는 clang 에선 이 경우 조용했다), g++ 는 `-Wmissing-declarations` 가 `no previous declaration for 'void TIM2_IRQHandler()'` 를 냈다 (둘 다 `demo_weak.txt` 실측).

<!-- code: practice/p1_cpp_irq/timer.cpp -->
</details>

### 연습 2 — CI 가드: 필수 핸들러가 strong 인지 검사

`require_handlers <image> NAME...` bash 함수를 만든다. 최종 이미지(Mach-O 실행 파일 또는 ELF)에서 각 NAME 이 강한 정의가 아니면 `UNBOUND NAME` 을 찍고 return 1. s4 starter/solution 빌드와, VM 에서 링크한 `fw.elf` 두 개에 돌려 본다.

<details><summary>답</summary>

`file -b` 로 포맷을 가른다. Mach-O 는 `nm -m` 에서 `) external _NAME` (앞에 weak 가 붙으면 불일치), ELF 는 `nm` 에서 ` T NAME`. 맥 `nm`(llvm-nm) 은 ELF 도 읽고 weak 를 `W` 로 보여 줬다. 이 함수는 **이름 오타와 빠진 파일 둘 다** 잡는다(컴파일 경고는 오타만). 필수 목록은 "이 보드에서 enable 하는 IRQ" 와 같아야 하니, NVIC enable 코드와 같은 곳(예: 보드 설정 헤더)에서 생성하는 게 좋다.

<!-- code: practice/p2_lint/require_handlers.sh -->
</details>

### 연습 3 — 링크 줄 예측표

`liba.a` = `board_name()` → "A", `libb.a` = `board_name()` → "B" 와 `board_rev()` 가 **한 멤버(b_name.o)** 에. 링크 줄 6개의 결과(링크 ok/fail, 실행 출력)를 `predict.txt` 에 예측하고 check 로 확인한다. 아카이브 탐색 순서 자체는 TOOL-B09.

<details><summary>답</summary>

c1 `main.o liba.a libb.a` → ok `A`, c2 순서 반대 → ok `B` (필요한 심볼을 먼저 채운 아카이브가 이기고, 뒤 아카이브의 같은 이름은 **끌려오지 않으니** 충돌도 없다 — 조용히 첫 번째가 이김). c3 `main2.o liba.a libb.a` → fail: `board_name` 은 liba 에서 채웠지만 `board_rev` 때문에 b_name.o 가 통째로 끌려오면서 `board_name` 이 두 번 → ld64 `duplicate symbol '_board_name' in: …/liba.a[2](a_name.o) …/libb.a[2](b_name.o)` (경로 생략), GNU ld 도 `multiple definition` (VM 실측). c4 순서 반대 → ok `B rev2`. c5 .o 로 직접 주면 무조건 둘 다 링크 → fail. c6 `-undefined dynamic_lookup` → 링크 ok, 실행 crash(rc 139). 아카이브 "멤버 단위" 로 끌려온다는 게 핵심 — 같은 .o 에 다른 필요한 심볼이 있으면 원치 않는 정의도 같이 온다.

<!-- code: practice/p3_predict/predict.txt -->
</details>

### 연습 4 — 컴파일 단계 가드 플래그

s4 starter 의 `uart.c`(오타)는 컴파일이 **실패**하고, solution 전체는 `-Werror` 로 경고 0개가 되는 플래그를 `guard.flags` 에 적어라.

<details><summary>답</summary>

`-Wmissing-prototypes`. 핸들러 원형이 `irq.h` 에 있으니 올바른 이름은 "이전 원형 있음", 오타 이름은 "원형 없는 외부 함수" 로 경고된다. 단, 원형 헤더를 include 하지 않은 파일에서는 올바른 이름도 경고가 나므로, 핸들러 파일은 원형 헤더를 꼭 include 하는 규칙과 짝이다. 빠진 파일은 못 잡으므로 연습 2 와 함께 쓴다. 경고 플래그 전체 기준선은 TOOL-B02.

<!-- code: practice/p4_guard/guard.flags -->
</details>

### 한 번에 검증

```bash
bash check.sh practice
```

```text
== TOOL-B05 check: practice   (work dir /tmp/TOOL-B05_work/practice)
-- Part 1: 9. 추가 연습 (이 맥) --
PASS  p1 C++ 핸들러 빌드 링크
PASS  p1 output (tim2=2, default_hits=0)
PASS  p1 nm -m: _TIM2_IRQHandler strong
PASS  p1 nm: timer.o 에 __Z*TIM2_IRQHandler* 없음
PASS  p2 require_handlers: s4 starter(Mach-O) → 3개 UNBOUND, rc 1
PASS  p2 require_handlers: s4 solution(Mach-O) → 출력 없음, rc 0
PASS  p3 c1 예측 'ok A'
PASS  p3 c2 예측 'ok B'
PASS  p3 c3 예측 'fail -'
PASS  p3 c4 예측 'ok B rev2'
PASS  p3 c5 예측 'fail -'
PASS  p3 c6 예측 'ok crash'
PASS  p4 가드 플래그 [-Wmissing-prototypes] 로 starter uart.c(오타) 컴파일이 실패
PASS  p4 같은 플래그로 solution 전체가 경고 0개로 빌드
-- Part 2: Lima VM elinux (GNU 툴체인, ELF) --
PASS  p2 require_handlers: starter fw.elf(ELF) → 2개 UNBOUND
PASS  p2 require_handlers: solution fw.elf(ELF) → rc 0
16/16 passed
```

p2 의 starter 검사에 `NMI_Handler` 를 일부러 넣어 "weak 인 게 정상인 핸들러도 목록에 넣으면 UNBOUND 로 나온다" 를 확인한다 — 필수 목록에는 실제로 쓰는 IRQ 만 넣는다.

## 10. 핵심 정리

- 링커 = 모든 `U` 를 정의 **정확히 하나**에 연결. 0개 → undefined, strong 2개 → duplicate, weak+strong → strong, weak 끼리 → 먼저 나온 것.
- undefined 원인 4종: 정의 없음 / 이름 오타 / 링크 줄 누락 / 다른 파일의 `static`(`t`). `nm -u`, `nm` 글자로 구분.
- 헤더에는 `extern` 선언만, 정의는 .c 한 곳. `static` 으로 막으면 파일마다 사본 → 조용한 버그.
- Apple clang 은 Darwin 에서 `-fcommon`(실측), 펌웨어 gcc 는 `-fno-common` → 맥에서 되던 헤더 변수가 펌웨어에서 깨진다.
- C++ ↔ C: 괄호·타입이 붙은 undefined = 맹글링. C 헤더에 `extern "C"` 가드, `c++filt` 로 읽기.
- weak 기본 핸들러: 오타·빠진 파일·C++ 맹글링 모두 **링크 성공 + 기본 핸들러**. ELF `nm` 의 `W`/같은 주소, Mach-O `nm -m` 의 `weak external` 로 확인하고 CI 에서 검사.
- ld64: 심볼별 묶음, `_` 접두사, extern "C" NOTE. GNU ld: 위치별 `file:(section+off)`, `first defined here`.

## 참고자료

- GNU ld 매뉴얼 — Options (`--no-demangle`, `--unresolved-symbols`, `-y`): https://sourceware.org/binutils/docs/ld/Options.html
- GCC 매뉴얼 — `-fcommon` / `-fno-common` (Code Gen Options): https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html
- GCC 10 Porting Guide — "Default to -fno-common": https://gcc.gnu.org/gcc-10/porting_to.html
- GCC 매뉴얼 — Common Function Attributes (`weak`, `alias`): https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html
- Itanium C++ ABI — Mangling: https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling
- ISO C11 (N1570) 6.9.2 External object definitions (tentative definition), J.5.11 Multiple external definitions
- ARM CMSIS 6 (디바이스 템플릿의 `startup_<device>.c`: weak handler + `Default_Handler`): https://github.com/ARM-software/CMSIS_6
- `man ld` (ld64, `-undefined`), `man nm` (`-m`), `dyld_info` — macOS Command Line Tools
- 관련 레슨: TOOL-B02(경고 플래그), TOOL-B04(오브젝트 파일·nm 글자), TOOL-B06(.data/.bss), TOOL-B09(아카이브 링크 순서), C-B10(static·linkage), CPP-B02(extern "C"), ARM-M08(벡터 테이블·Thumb 비트), BM-B08(C 스타트업)
