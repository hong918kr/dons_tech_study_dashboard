---
id: TOOL-B07
time: 60분
updated: 2026-11-15
---
# TOOL-B07 전처리기 활용: 조건부 컴파일과 빌드 변형

## 학습 목표

- `-D` 로 들어온 입력 매크로(BOARD_A/B, BUILD_DEBUG/RELEASE, FEATURE_X)를 **헤더 한 곳**에서 검증하고, 앱 코드가 쓸 **파생 매크로**(LED_PIN, UART_BAUD, LOG_LEVEL, HAS_USB, ASSERT …)로 바꾸는 board_config.h 를 쓴다.
- 변형(variant)마다 `cc -E -P` (전처리 결과)와 `cc -E -dM` (매크로 덤프)로 "이 빌드에 무엇이 켜졌고 어느 코드 경로가 남았나" 를 **grep 한 줄로** 증명한다.
- 잘못된 조합은 조용한 기본값이 아니라 **빌드 실패**로 막는다: 숫자/정의 여부는 `#error`(전처리 단계), 타입·sizeof 가 필요한 규칙은 `_Static_assert`(컴파일 단계) — 둘이 언제 터지는지 설명한다.
- `#ifdef` vs `#if` (`-DFEATURE_USB=0` 이 "켜짐" 이 되는 함정), 정의 안 된 이름 = 0 (`-Wundef`), 빈 매크로, `defined` 를 만들어 내는 매크로, `-D`/`-U` 순서를 실측으로 안다.
- 타깃이 미리 정의하는 매크로(predefined macro: `__ARM_ARCH`, `__thumb__`, `__ARM_FP`, `__ARM_PCS_VFP` …)를 Mac arm64 · clang Cortex-M4 · arm-none-eabi-gcc 에서 덤프해 비교하고 헤더 가드에 쓴다.

## 1. 문제

펌웨어 소스 하나로 보드 리비전 2~3개 × debug/release × 기능 SKU 를 빌드하는 것이 현업의 기본이고, 면접에서는 "이 릴리스 바이너리에 디버그 셸이 들어갔는지 어떻게 확인하나요?", "`#ifdef` 와 `#if` 차이는?" 가 그대로 나온다. SSD 컨트롤러 펌웨어도 NAND 벤더 · 용량 · OEM 고객별 변형을 같은 트리에서 `-D` 로 갈라 CI 에서 수십 개 빌드한다.

핵심 아이디어: **입력(-D) → 검증(#error) → 파생 매크로 → 앱 코드의 #if** 를 한 방향으로 흐르게 하고, 앱 코드(`app.c`)는 입력 매크로를 직접 보지 않는다. 그러면 변형의 규칙이 헤더 한 파일에 모이고, 잘못된 조합은 첫 파일을 전처리하는 순간 멈춘다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">-D 입력이 board_config.h 를 거쳐 app.c 의 코드 경로를 고른다</text>
  <rect x="10" y="40" width="200" height="96" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="60" fill="currentColor" font-weight="bold">빌드 시스템 / CI</text>
  <text x="20" y="82" fill="currentColor" font-family="monospace" font-size="12">-DBOARD_B</text>
  <text x="20" y="100" fill="currentColor" font-family="monospace" font-size="12">-DBUILD_RELEASE</text>
  <text x="20" y="118" fill="currentColor" font-family="monospace" font-size="12">-DFEATURE_USB=1</text>
  <rect x="10" y="150" width="200" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="170" fill="currentColor" font-weight="bold">컴파일러 predefined</text>
  <text x="20" y="190" fill="currentColor" font-family="monospace" font-size="12">__arm__ __thumb__</text>
  <text x="20" y="208" fill="currentColor" font-family="monospace" font-size="12">__ARM_PCS_VFP …</text>
  <rect x="250" y="40" width="230" height="180" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="260" y="60" fill="currentColor" font-weight="bold">board_config.h</text>
  <text x="260" y="82" fill="currentColor" font-size="12">① 검증: 보드 정확히 1개,</text>
  <text x="260" y="100" fill="currentColor" font-size="12">   빌드 1개, USB 는 B 만, …</text>
  <text x="260" y="118" fill="currentColor" font-size="12">   → 위반이면 #error 로 중단</text>
  <text x="260" y="142" fill="currentColor" font-size="12">② 파생 매크로 (출력)</text>
  <text x="260" y="162" fill="currentColor" font-family="monospace" font-size="12">LED_PIN 13  UART_BAUD 921600</text>
  <text x="260" y="180" fill="currentColor" font-family="monospace" font-size="12">LOG_LEVEL 2  HAS_USB 1</text>
  <text x="260" y="198" fill="currentColor" font-family="monospace" font-size="12">ASSERT(c) → (void)sizeof(c)</text>
  <rect x="520" y="40" width="210" height="180" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="530" y="60" fill="currentColor" font-weight="bold">app.c (출력만 사용)</text>
  <text x="530" y="84" fill="currentColor" font-family="monospace" font-size="12">#if HAS_USB</text>
  <text x="530" y="102" fill="currentColor" font-family="monospace" font-size="12">  usb_init();   ← 남음</text>
  <text x="530" y="120" fill="currentColor" font-family="monospace" font-size="12">#endif</text>
  <text x="530" y="144" fill="currentColor" font-family="monospace" font-size="12">#if HAS_SHELL</text>
  <text x="530" y="162" fill="currentColor" font-family="monospace" font-size="12">  shell_start(); ← 사라짐</text>
  <text x="530" y="180" fill="currentColor" font-family="monospace" font-size="12">#endif</text>
  <text x="530" y="204" fill="currentColor" font-family="monospace" font-size="12">ASSERT(...) → 호출 없음</text>
  <path d="M210 88 L248 110" stroke="currentColor" stroke-width="2"/>
  <path d="M210 185 L248 160" stroke="currentColor" stroke-width="2"/>
  <path d="M480 130 L518 130" stroke="currentColor" stroke-width="2"/>
  <rect x="10" y="240" width="720" height="78" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="262" fill="currentColor" font-weight="bold">증명하는 도구</text>
  <text x="20" y="284" fill="currentColor" font-family="monospace" font-size="12">cc ... -E -dM app.c | grep '^#define HAS_USB '   → "#define HAS_USB 1"   (매크로 덤프)</text>
  <text x="20" y="304" fill="currentColor" font-family="monospace" font-size="12">cc ... -E -P app.c | grep -c 'usb_init();'        → 남은 코드 경로   (전처리 결과)</text>
</svg>
```

| 입력 (`-D`) | 규칙 | 출력 (app.c 가 쓰는 것) |
|---|---|---|
| `BOARD_A` \| `BOARD_B` | 정확히 1개 | `BOARD_NAME`, `LED_PIN` (A: 5 = PA5, B: 13 = PC13), `UART_BAUD` (115200 / 921600), `rx_idx_t` (uint8_t / uint16_t) |
| `BUILD_DEBUG` \| `BUILD_RELEASE` | 정확히 1개 | `BUILD_NAME`, `LOG_LEVEL` 기본값 4 / 2, `ASSERT()` 켜짐 / 꺼짐 |
| `FEATURE_USB=0\|1` | 기본 0, 0/1 만, BOARD_B 에서만 | `HAS_USB` |
| `FEATURE_SHELL=0\|1` | 기본 0, 0/1 만, release 금지 | `HAS_SHELL` |
| `LOG_LEVEL=n` | 0..4 | `LOG_LEVEL` |
| `RX_BUF_SIZE=n` | 2의 거듭제곱 ≥ 16, `RX_BUF_SIZE-1` 이 `rx_idx_t` 에 들어감 | `RX_BUF_SIZE` |

### Part A — 변형 빌드와 -E / -dM 으로 활성 경로 확인

- `-DBOARD_A -DBUILD_DEBUG`, `-DBOARD_B -DBUILD_RELEASE -DFEATURE_USB=1` 등 합법 조합 7개를 빌드한다.
- 각 변형에서 `-E -dM` 덤프에 기대한 `#define` 줄(`LED_PIN 13`, `HAS_USB 1`, `LOG_LEVEL 2` …)이 **정확히** 있는지, `-E -P` 출력에 `usb_init();` / `shell_start();` / `assert_fail("app.c"` / `dbg: boot done` 이 있어야 할 때만 있는지 확인한다.
- 그리고 `-Wundef -Werror` 로 실제 빌드해 실행한 첫 줄(`board=B build=release led=13 …`)이 맞는지 본다.

### Part B — 잘못된 조합을 빌드 실패로: #error 와 _Static_assert

- 보드 0개/2개, 빌드 타입 0개/2개, BOARD_A 에 USB, release 에 셸, `FEATURE_USB=2`, `LOG_LEVEL=7`, `RX_BUF_SIZE=100` → **전처리 단계에서** 헤더의 `#error` 문구로 실패.
- `-DBOARD_A -DRX_BUF_SIZE=512` (2의 거듭제곱이지만 uint8_t 인덱스로 511 을 못 셈) → 전처리는 통과하고 **컴파일 단계**에서 `_Static_assert` 문구로 실패. 전처리기는 타입을 모르기 때문이다.
- `-DFEATURE_USB=0` 은 **꺼짐**이어야 한다 (`#ifdef` 로 쓰면 켜짐이 된다).

### Part C — 펌웨어 현실: CI 매트릭스, 타깃 매크로, GNU 툴체인

- 같은 매트릭스를 Lima VM 의 `arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard` (컴파일만)와 호스트 `gcc` (빌드+실행)로 돌려 결과가 같음을 확인한다 (`demo_vm.sh`).
- `-dM -E - </dev/null` 로 Mac arm64 · clang `--target=thumbv7em-none-eabi` (hard/softfp/soft) · arm-none-eabi-gcc (hard/soft) 의 predefined 매크로를 비교하고, 그중 무엇으로 "Cortex-M4F 보드인데 soft-float 로 빌드함" 을 막을 수 있는지 고른다 (추가 연습 4 에서 구현).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-size="13" font-weight="bold">CI 빌드 매트릭스: 보드 × 빌드/기능 (check.sh 케이스 ID)</text>
  <g fill="currentColor" font-weight="bold">
    <text x="130" y="48">DEBUG</text><text x="225" y="48">RELEASE</text><text x="320" y="48">DEBUG+USB</text>
    <text x="425" y="48">REL+USB=0</text><text x="530" y="48">DEBUG+SHELL</text><text x="635" y="48">REL+SHELL</text>
    <text x="14" y="80">BOARD_A</text><text x="14" y="120">BOARD_B</text><text x="14" y="160">보드 없음</text><text x="14" y="200">A 와 B 둘 다</text>
  </g>
  <g stroke="currentColor">
    <rect x="120" y="60" width="100" height="32" class="f-ok"/><rect x="220" y="60" width="100" height="32" class="f-ok"/>
    <rect x="320" y="60" width="100" height="32" class="f-warn"/><rect x="420" y="60" width="100" height="32" class="f-ok"/>
    <rect x="520" y="60" width="100" height="32" class="f-ok"/><rect x="620" y="60" width="100" height="32" class="f-warn"/>
    <rect x="120" y="100" width="100" height="32" class="f-ok"/><rect x="220" y="100" width="100" height="32" class="f-ok"/>
    <rect x="320" y="100" width="100" height="32" class="f-ok"/><rect x="420" y="100" width="100" height="32" class="f-ok"/>
    <rect x="520" y="100" width="100" height="32" class="f-ok"/><rect x="620" y="100" width="100" height="32" class="f-warn"/>
    <rect x="120" y="140" width="600" height="32" class="f-warn"/>
    <rect x="120" y="180" width="600" height="32" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="130" y="81">OK L01</text><text x="230" y="81">OK L02</text><text x="330" y="81">#error E05</text>
    <text x="430" y="81">OK</text><text x="530" y="81">OK L06</text><text x="630" y="81">#error</text>
    <text x="130" y="121">OK</text><text x="230" y="121">OK L05</text><text x="330" y="121">OK L03</text>
    <text x="430" y="121">OK L04</text><text x="530" y="121">OK L03</text><text x="630" y="121">#error E07</text>
    <text x="130" y="161">#error E01 "got none" — 어떤 빌드/기능이든</text>
    <text x="130" y="201">#error E02 "got both"</text>
  </g>
  <text x="10" y="236" fill="currentColor">빌드 타입 0개(E03)·2개(E04), FEATURE_USB=2(E08), LOG_LEVEL=7(E09), RX 100(E10)도 #error. RX 512 on A(S01)는 _Static_assert.</text>
</svg>
```

## 2. Problem (English)

> **Board/build variants with the C preprocessor.**
>
> One firmware tree is built for two boards and two build types. The build system passes `-DBOARD_A` or `-DBOARD_B`, `-DBUILD_DEBUG` or `-DBUILD_RELEASE`, and optional `-DFEATURE_USB=0|1`, `-DFEATURE_SHELL=0|1`, `-DLOG_LEVEL=0..4`, `-DRX_BUF_SIZE=n`. Application code (`app.c`, which you must not edit) only uses the derived macros `BOARD_NAME BUILD_NAME LED_PIN UART_BAUD LOG_LEVEL HAS_USB HAS_SHELL RX_BUF_SIZE`, the type `rx_idx_t`, and `ASSERT(cond)`.
>
> Fix `board_config.h` (header only) so that:
>
> 1. Exactly one board and exactly one build type are required. Today the header silently picks board B when none is given and board A when both are given.
> 2. `-DFEATURE_USB=0` means *off*; values other than 0/1 are rejected; USB is only legal on BOARD_B; the shell is never legal in a release build; `LOG_LEVEL` must be 0..4; `RX_BUF_SIZE` must be a power of two ≥ 16.
> 3. `RX_BUF_SIZE - 1` must fit in `rx_idx_t` (uint8_t on board A) — this one needs the type system.
> 4. In release, `ASSERT(c)` leaves no call and does not evaluate `c`, but `c` must still be type-checked.
>
> Every illegal combination must fail with a clear message from the header. Show, for any variant, which macros are active (`-E -dM`) and which code paths survived (`-E`). Example:

```text
$ cc -std=c11 -DBOARD_A -DBOARD_B -DBUILD_DEBUG -E app.c -o /dev/null
./board_config.h:22:2: error: "board_config: define exactly one of BOARD_A / BOARD_B (got both)"
$ cc -std=c11 -DBOARD_B -DBUILD_RELEASE -DFEATURE_USB=1 app.c -o app && ./app | head -1
board=B build=release led=13 baud=921600 log=2 usb=1 shell=0 rx=256
```

> Follow-ups: `#if` vs `#ifdef`; what an undefined identifier evaluates to in `#if`; `#error` vs `_Static_assert`; how you would find out which macros a cross compiler predefines; how a CI matrix keeps all variants building.

## 3. 시작 코드

이 레슨은 **스크립트 검증형(Form S)** 이다. 고칠 파일은 헤더 하나 — `starter.h` (check.sh 가 작업 폴더에 `board_config.h` 로 복사한다).

<!-- code: starter.h -->

앱 코드 (모든 변형이 공유, 수정 금지):

<!-- code: app.c -->

검사기:

<!-- code: check.sh -->

```bash
bash check.sh starter     # 작업 폴더 /tmp/TOOL-B07_work/starter, 약 2초
```

처음 실행 결과 (실제 출력):

```text
== starter: illegal combinations must stop in the preprocessor with your #error
FAIL E01_no_board -- preprocessed fine, expected #error "exactly one of BOARD_A / BOARD_B (got none)"
FAIL E02_both_boards -- preprocessed fine, expected #error "exactly one of BOARD_A / BOARD_B (got both)"
FAIL E03_no_build -- preprocessed fine, expected #error "define BUILD_DEBUG or BUILD_RELEASE"
FAIL E04_both_builds -- preprocessed fine, expected #error "mutually exclusive"
FAIL E05_usb_on_A -- preprocessed fine, expected #error "FEATURE_USB requires BOARD_B"
FAIL E06_usb_on_A_bare -- preprocessed fine, expected #error "FEATURE_USB requires BOARD_B"
FAIL E07_shell_release -- preprocessed fine, expected #error "FEATURE_SHELL is not allowed in BUILD_RELEASE"
FAIL E08_usb_is_2 -- preprocessed fine, expected #error "FEATURE_USB must be 0 or 1"
FAIL E09_log_level_7 -- preprocessed fine, expected #error "LOG_LEVEL must be 0..4"
FAIL E10_rx_not_pow2 -- preprocessed fine, expected #error "power of two >= 16"
== starter: type-level rule -> _Static_assert (passes -E, fails at compile)
FAIL S01_rx_512_on_A -- compiled fine, expected _Static_assert "does not fit rx_idx_t"
== starter: legal variants: -dM values, -E code path, -Wundef -Werror build, run
PASS L01_A_debug
FAIL L02_A_release -- -E: dead path still contains "assert_fail("app.c""
PASS L03_B_debug_usb_shell
FAIL L04_B_release_usb0 -- -dM: missing "#define HAS_USB 0" (have: #define HAS_USB 1)
FAIL L05_B_release_usb_bare -- -E: dead path still contains "assert_fail("app.c""
PASS L06_A_debug_log0_shell
PASS L07_A_debug_rx128
4/18 passed
```

starter 가 "틀린 조합" 을 실제로 어떻게 빌드해 버리는지 (`demo_traps.sh` T10, `-Wall -Wextra -Wundef -Werror` 로도 경고 0개):

```text
$ cc ... -DBUILD_DEBUG app.c && ./app | head -1                            # 보드 없음
board=B build=debug led=13 baud=921600 log=4 usb=0 shell=0 rx=256
$ cc ... -DBOARD_A -DBOARD_B -DBUILD_DEBUG app.c && ./app | head -1         # 보드 둘
board=A build=debug led=5 baud=115200 log=4 usb=0 shell=0 rx=64
$ cc ... -DBOARD_A app.c && ./app | head -1                                 # 빌드 타입 없음
board=A build=release led=5 baud=115200 log=2 usb=0 shell=0 rx=64
$ cc ... -DBOARD_B -DBUILD_RELEASE -DFEATURE_USB=0 app.c && ./app | head -1 # "끄라" 했는데
board=B build=release led=13 baud=921600 log=2 usb=1 shell=0 rx=256
$ cc ... -DBOARD_A -DBUILD_DEBUG -DRX_BUF_SIZE=512 app.c && ./app | head -1 # uint8_t 인덱스로 512칸
board=A build=debug led=5 baud=115200 log=4 usb=0 shell=0 rx=512
```

다섯 개 모두 경고 없이 링크되고 돈다 — 보드 B 펌웨어가 A 보드에 구워지는 사고가 이런 모양이다. 마지막 줄은 `rx_head` 가 255 에서 0 으로 돌아가 버퍼의 절반을 영영 못 쓰는 조용한 버그다.

관찰용 데모 (검사기와 별개, 모두 이 디렉터리):

```bash
bash demo_variants.sh > variants.txt     # 두 변형의 -dM / -E diff / 오브젝트 크기
bash demo_traps.sh    > traps.txt        # #ifdef·-Wundef·빈 매크로·sizeof·-D/-U 순서·ASSERT 평가 (T1~T11)
bash demo_predef.sh   > predef_table.txt # predefined 매크로 비교 (Mac + VM)
bash demo_vm.sh       > vm_check.txt     # 같은 check.sh 를 VM 의 arm-none-eabi-gcc / gcc 로
```

## 4. 힌트

<details>
<summary>힌트 1 — "정확히 하나" 는 #if / #elif 두 줄</summary>

`#if defined(A) && defined(B)` → 둘 다, `#elif !defined(A) && !defined(B)` → 둘 다 없음. 각각 `#error "..."`. starter 의 `#if defined(BOARD_A) ... #else` 는 그대로 둬도 된다 — 검증이 그 **앞에** 있으면 `#else` 는 "B 가 확실할 때" 만 도달한다.

</details>

<details>
<summary>힌트 2 — 기능 플래그는 "정의 여부" 가 아니라 "값"</summary>

`#ifdef FEATURE_USB` 는 `-DFEATURE_USB=0` 에도 참이다. 먼저 `#ifndef FEATURE_USB / #define FEATURE_USB 0` 으로 기본값을 주고, 그다음부터는 `#if FEATURE_USB` 로 **값**을 본다. 0/1 범위 검사, 보드 조건(`!defined(BOARD_B)`), 빌드 조건(`defined(BUILD_RELEASE)`)을 각각 `#error` 로.

</details>

<details>
<summary>힌트 3 — 전처리기가 못 하는 검사는 _Static_assert 로</summary>

`#if` 안에서는 정수 상수와 `defined` 만 쓸 수 있다. `(rx_idx_t)(RX_BUF_SIZE - 1)` 같은 **캐스트·sizeof·타입**은 컴파일러 단계의 `_Static_assert(식, "문구");` 로. 헤더 파일 범위(file scope)에 둬도 되는 선언이다 (C11 6.7.10).

</details>

<details>
<summary>힌트 4 — release ASSERT: 호출은 없애고 타입 검사는 남기기</summary>

`#define ASSERT(c)` (빈 매크로)는 `c` 의 오타도 같이 사라진다. `((void)(c))` 는 `c` 를 **평가**한다(부작용 실행). `do { (void)sizeof(c); } while (0)` 는 `sizeof` 의 피연산자가 평가되지 않으므로(C11 6.5.3.4p2, VLA 제외) 둘 다 해결한다.

</details>

## 5. 풀이

### Part A — 변형을 -dM 과 -E 로 "증명" 하기

**-E -dM** 은 전처리가 끝난 시점에 정의되어 있는 **모든** 매크로를 `#define 이름 값` 형태로 찍는다. `app.c` 하나에도 `<stdio.h>`, `<stdint.h>`, predefined 매크로까지 합쳐 2397 줄이므로(실측, 두 변형 모두) 우리 매크로만 grep 한다. `demo_variants.sh` 실제 출력:

```text
$ cc -std=c11 -DBOARD_A -DBUILD_DEBUG -E -dM app.c | grep -E '<our macros>' | sort
#define ASSERT(c) do { if (!(c)) assert_fail(__FILE__, __LINE__, #c); } while (0)
#define BOARD_A 1
#define BOARD_NAME "A"
#define BUILD_DEBUG 1
#define BUILD_NAME "debug"
#define FEATURE_SHELL 0
#define FEATURE_USB 0
#define HAS_SHELL 0
#define HAS_USB 0
#define LED_PIN 5
#define LOG_LEVEL 4
#define RX_BUF_SIZE 64
#define UART_BAUD 115200

$ cc -std=c11 -DBOARD_B -DBUILD_RELEASE -DFEATURE_USB=1 -E -dM app.c | grep -E '<our macros>' | sort
#define ASSERT(c) do { (void)sizeof(c); } while (0)
#define BOARD_B 1
#define BOARD_NAME "B"
#define BUILD_NAME "release"
#define BUILD_RELEASE 1
...                                   (FEATURE_*, HAS_SHELL 0 생략)
#define HAS_USB 1
#define LED_PIN 13
#define LOG_LEVEL 2
#define RX_BUF_SIZE 256
#define UART_BAUD 921600
```

- `-DBOARD_A` 처럼 값 없이 준 `-D` 는 `#define BOARD_A 1` 이 된다 (덤프에 그대로 보인다).
- `-dM` 은 **치환 목록을 쓰인 그대로** 보여 줄 뿐 계산하지 않는다. 연습용 헤더에서 `-DBOARD=BOARD_ID_B` 로 빌드하면 덤프는 `#define BOARD BOARD_ID_B` 이고, 실제 값은 `-E` 로 펼쳐야 보인다 (T8, 실제 출력):

```text
$ cc -DBOARD=BOARD_ID_B -DBUILD_DEBUG -E -dM t8.c | grep -E '^#define (BOARD|LED_PIN) '
#define BOARD BOARD_ID_B
#define LED_PIN 13
$ cc -DBOARD=BOARD_ID_B -DBUILD_DEBUG -E -P t8.c | grep probe_
int probe_board = 2; int probe_led = 13;
```

  그래서 solution 헤더는 파생 매크로를 `#define HAS_USB FEATURE_USB` 가 아니라 `#if FEATURE_USB / #define HAS_USB 1` 로 **리터럴**로 정의했다 — 덤프 한 줄이 곧 증거가 된다.

**-E -P** 는 전처리 결과(.i)를 줄 표시(linemarker `# 12 "app.c"`)없이 찍는다(-E/linemarker 자체는 TOOL-B01). 두 변형의 끝부분 diff (실제):

```text
1c1
< typedef uint8_t rx_idx_t;
---
> typedef uint16_t rx_idx_t;
...
13a14
> static void usb_init(void) { puts("usb_init"); }
...
<     do { if (!(sizeof rx_buf == 64)) assert_fail("app.c", 41, "sizeof rx_buf == RX_BUF_SIZE"); } while (0);
---
>     do { (void)sizeof(sizeof rx_buf == 256); } while (0);
28c29
<     puts("dbg: boot done");
---
>     usb_init();
```

- `#if` 가 거짓인 그룹은 .i 에 **한 글자도** 안 남는다 → 컴파일러는 그 코드를 보지도 않는다. 그래서 "릴리스에 셸이 들어갔나?" 는 `-E -P ... | grep -c shell_start` 가 0 인지로 답한다. 바이너리에서 보려면 `nm`/`strings` (TOOL-B04, TOOL-B13).
- `__FILE__`/`__LINE__`/`#c` 는 전처리 때 `"app.c"`, `41`, `"sizeof rx_buf == RX_BUF_SIZE"` 로 굳는다. `#c`(문자열화)는 **치환 전** 인자 글자를 쓰므로 `RX_BUF_SIZE` 가 이름 그대로 남는다.

check.sh 의 합법 케이스는 이 둘에 실행 결과까지 3중으로 본다:

| 케이스 | `-D` | `-dM` 에 있어야 할 줄 | `-E` 에 있어야 / 없어야 |
|---|---|---|---|
| L01 | `BOARD_A BUILD_DEBUG` | `LED_PIN 5`, `LOG_LEVEL 4`, `HAS_USB 0`, `RX_BUF_SIZE 64` | 있음 `assert_fail("app.c"`, `dbg: boot done` / 없음 `usb_init`, `shell_start` |
| L02 | `BOARD_A BUILD_RELEASE` | `LOG_LEVEL 2` | 없음 `assert_fail("app.c"`, `dbg: boot done` |
| L03 | `BOARD_B BUILD_DEBUG FEATURE_USB=1 FEATURE_SHELL=1` | `HAS_USB 1`, `HAS_SHELL 1` | 있음 `usb_init();`, `shell_start();` |
| L04 | `BOARD_B BUILD_RELEASE FEATURE_USB=0` | `HAS_USB 0` | 없음 `usb_init` |
| L05 | `BOARD_B BUILD_RELEASE FEATURE_USB` (값 없음 = 1) | `HAS_USB 1` | 있음 `usb_init();` |
| L06 | `BOARD_A BUILD_DEBUG LOG_LEVEL=0 FEATURE_SHELL=1` | `LOG_LEVEL 0`, `HAS_SHELL 1` | 없음 `dbg: boot done` |
| L07 | `BOARD_A BUILD_DEBUG RX_BUF_SIZE=128` | `RX_BUF_SIZE 128` | — |

Mac 오브젝트 크기 (`-Os -c`, `llvm-size -A`, 실측): A-debug `__text=208 __cstring=148`, A-release `__text=196 __cstring=135`, B-release+USB `__text=212 __cstring=144`. 차이는 `"dbg: boot done"` 문자열과 `usb_init` 정도다. debug 의 ASSERT 도 조건이 상수(`sizeof rx_buf == 64`)라 최적화로 지워져 크기 차이가 작다 — 진짜 펌웨어에서 ASSERT/로그가 KB 단위로 차이 나는 이유는 파일 이름·조건 문자열이 수백 개 쌓이기 때문이다 (TOOL-B13 에서 실측).

### Part B — #error 와 _Static_assert: 언제 무엇으로 막나

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-size="13" font-weight="bold">검사가 터지는 단계: 전처리기는 숫자와 이름만, 컴파일러는 타입까지 안다</text>
  <rect x="10" y="40" width="80" height="40" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="22" y="65" fill="currentColor" font-family="monospace">app.c</text>
  <rect x="120" y="34" width="260" height="140" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="130" y="54" fill="currentColor" font-weight="bold">전처리 (cc -E)</text>
  <text x="130" y="74" fill="currentColor" font-family="monospace">#if / #ifdef / defined()</text>
  <text x="130" y="92" fill="currentColor" font-family="monospace">#error "..."  → 여기서 실패</text>
  <text x="130" y="110" fill="currentColor">볼 수 있는 것: 정수 상수, 매크로,</text>
  <text x="130" y="128" fill="currentColor">defined(X). 모르는 이름 = 0</text>
  <text x="130" y="146" fill="currentColor">못 보는 것: sizeof, 캐스트, enum,</text>
  <text x="130" y="164" fill="currentColor">typedef (rx_idx_t)</text>
  <rect x="410" y="34" width="230" height="140" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="420" y="54" fill="currentColor" font-weight="bold">컴파일 (cc -fsyntax-only / -c)</text>
  <text x="420" y="74" fill="currentColor" font-family="monospace">_Static_assert(expr, "...")</text>
  <text x="420" y="92" fill="currentColor">→ 여기서 실패</text>
  <text x="420" y="110" fill="currentColor">볼 수 있는 것: sizeof, 캐스트,</text>
  <text x="420" y="128" fill="currentColor">enum 상수, offsetof, 타입 크기</text>
  <text x="420" y="146" fill="currentColor">못 보는 것: 링크 결과(주소, 크기 합)</text>
  <rect x="670" y="40" width="60" height="40" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="684" y="65" fill="currentColor" font-family="monospace">.o</text>
  <path d="M90 60 L118 60" stroke="currentColor" stroke-width="2"/>
  <path d="M380 60 L408 60" stroke="currentColor" stroke-width="2"/>
  <path d="M640 60 L668 60" stroke="currentColor" stroke-width="2"/>
  <text x="10" y="200" fill="currentColor">E01~E10: cc -E 가 rc=1 + 헤더 문구.   S01 (RX 512, board A): cc -E 는 rc=0, -fsyntax-only 에서 실패.</text>
  <text x="10" y="222" fill="currentColor">링크 단계 검사(Flash/RAM 넘침)는 링커 스크립트 ASSERT — TOOL-M01 / TOOL-A07.</text>
</svg>
```

**#error** (C11 6.10.5) — 건너뛰지 않은 그룹에 `#error` 가 있으면 구현은 그 번역 단위를 성공적으로 번역하면 안 된다(C11 4p4). Mac clang 실제 출력:

```text
$ cc -std=c11 -DBOARD_B -DBUILD_DEBUG -DBUILD_RELEASE -DFEATURE_USB=2 -E app.c -o /dev/null
In file included from app.c:7:
./board_config.h:42:2: error: "board_config: BUILD_DEBUG and BUILD_RELEASE are mutually exclusive"
   42 | #error "board_config: BUILD_DEBUG and BUILD_RELEASE are mutually exclusive"
      |  ^
./board_config.h:63:2: error: "board_config: FEATURE_USB must be 0 or 1"
   63 | #error "board_config: FEATURE_USB must be 0 or 1"
      |  ^
2 errors generated.
rc=1
```

- `#error` 뒤에도 전처리는 **계속된다** → 잘못된 것 여러 개가 한 번에 다 보고된다 (CI 로그에 유리). 반대로 연쇄 에러도 난다: 연습 헤더에 `-DBOARD=B` 를 주면 `#error` 1개 + `'B' is not defined, evaluates to 0 [-Werror,-Wundef]` 5개 (실측).
- 문구에 **파일/모듈 이름과 고치는 법**을 넣는다 ("define exactly one of …"). 빌드 로그만 보는 사람이 바로 고칠 수 있게.
- GNU 표기는 `error: #error "..."` (VM arm-none-eabi-gcc 14.2.1, `vm_check.txt`):

```text
board_config.h:22:2: error: #error "board_config: define exactly one of BOARD_A / BOARD_B (got both)"
```

**_Static_assert** (C11 6.7.10) — 상수 식이 0 이면 컴파일 에러. `-E` 는 통과한다. clang 은 식의 값까지 보여 준다:

```text
$ cc -std=c11 -Wall -Wextra -Wundef -Werror -DBOARD_A -DBUILD_DEBUG -DRX_BUF_SIZE=512 app.c -o a
In file included from app.c:7:
./board_config.h:121:16: error: static assertion failed due to requirement '(unsigned char)(512 - 1) == 512 - 1': board_config: RX_BUF_SIZE - 1 does not fit rx_idx_t
...
./board_config.h:121:44: note: expression evaluates to '255 == 511'
```

GNU (VM arm-none-eabi-gcc): `board_config.h:121:1: error: static assertion failed: "board_config: RX_BUF_SIZE - 1 does not fit rx_idx_t"` — 값 설명은 없다.

규칙별로 어느 쪽을 쓰나:

| 규칙 | 도구 | 이유 |
|---|---|---|
| 보드/빌드 정확히 1개, USB↔보드, 셸↔release | `#error` | `defined()` 와 0/1 값만 필요. **가장 이른 단계**에서, `-E` 만 돌리는 도구(의존성 생성 `-MM`, IDE 인덱서)에서도 걸림 |
| `LOG_LEVEL` 0..4, `RX_BUF_SIZE` 2의 거듭제곱 | `#error` | 정수 산술은 `#if` 가 한다 (`(n & (n-1)) != 0`) |
| `RX_BUF_SIZE - 1` 이 `rx_idx_t` 에 들어감 | `_Static_assert` | `#if` 는 `typedef` 를 모른다. T4: `#if sizeof(int) == 4` → `error: function-like macro 'sizeof' is not defined` |
| 레지스터 구조체 오프셋, 패킷 구조체 크기 | `_Static_assert` | `offsetof`, `sizeof` (C-B 트랙의 레지스터 맵 레슨들과 같은 기법) |

**왜 starter 의 `#else` 가 위험했나**: `#if defined(BOARD_A) … #else (B) #endif` 는 "A 가 아니면 B" 라서 **보드 없음 = B** 가 된다. 기본값을 두는 것은 하나의 설계지만, 보드처럼 틀리면 하드웨어가 안 맞는 입력은 기본값 대신 에러가 맞다. solution 은 검증을 앞에 두어 `#else` 에 도달하는 경우를 "B 확실" 로 좁혔다.

**함정 실측 모음** (`demo_traps.sh` → `traps.txt`, Mac clang 21):

| # | 코드 / 명령 | 실제 결과 | 교훈 |
|---|---|---|---|
| T1 | `#ifdef FEATURE_USB` + `-DFEATURE_USB=0` | `int ifdef_on;` 남음, `#if` 쪽은 사라짐 | 0/1 플래그는 `#if` 로. `#ifdef` 는 "정의 여부" 만 본다 |
| T2 | `#if FEATUER_USB` (오타) | 조용히 0, 경고 없음. `-Wundef` 를 줘야 `warning: 'FEATUER_USB' is not defined, evaluates to 0 [-Wundef]` | `-Wundef` 는 `-Wall -Wextra` 에 **안** 들어 있다 (clang·gcc 15 둘 다 실측) → 따로 켠다 |
| T3a | `#define DEBUG` 후 `#if DEBUG` | `error: expected value in expression` (gcc: `#if with no expression`) | 빈 매크로는 `#ifdef` 전용. `#if` 로 쓸 플래그는 값을 준다 |
| T3b | `-DFEATURE_USB=` (빈 값) + `#if FEATURE_USB != 0` | `error: invalid token at start of a preprocessor expression` | 빌드 스크립트의 빈 변수(`-DFEATURE_USB=$(USB)`)가 이렇게 터진다 |
| T4 | `#if sizeof(int) == 4` | `error: function-like macro 'sizeof' is not defined` | 남은 이름은 0 으로 바뀐다(C11 6.10.1p4) → `0(0)` |
| T5 | `#if -1 > 0u` | 참! `warning: left side of operator converted from negative value to unsigned: -1 to 18446744073709551615` (gcc 는 경고 없이 참) | `#if` 산술은 intmax_t/uintmax_t (C11 6.10.1p4). 일반 C 의 정수 변환 규칙(C-B02)과 같은 함정 |
| T6 | `#define HAS(x) defined(x)` 후 `#if HAS(X)` | clang `-Wall -Wextra`: 조용함. `-Wpedantic`: `macro expansion producing 'defined' has undefined behavior`. gcc `-Wextra`: `this use of ‘defined’ may not be portable` | C11 6.10.1p4: 치환 결과로 생긴 `defined` 는 UB. 연습 3 의 `IS_ENABLED()` 가 대안 |
| T7 | `-DLOG_LEVEL=2 -DLOG_LEVEL=4` | `warning: 'LOG_LEVEL' macro redefined [-Wmacro-redefined]`, 결과 4 | 명령줄 `-D`/`-U` 는 **적힌 순서대로** 처리. `-DX=2 -UX -DX=3` → 3, 경고 없음 |
| T7b | 헤더에 `#define LOG_LEVEL 4` (#ifndef 없이) + `-DLOG_LEVEL=2` | 경고 + **헤더가 이김** (4) | 덮어쓸 수 있는 기본값은 반드시 `#ifndef X / #define X 기본값` |
| T9 | `#warning "..."`, `#pragma message("...")` | 둘 다 경고로 출력. `-std=c11 -Wpedantic` 이면 `#warning is a C23 extension` | 빌드 로그에 설정을 찍고 싶을 때. `-Werror` 와 함께면 `#warning` 이 에러가 된다 |

**release ASSERT 세 가지 (T11 실측)** — `ASSERT(n++ == 0); ASSERT(check_hw() > 0);` 후 `n`, `calls` 출력, 그리고 `ASSERT(no_such_var == 1)` 오타 검사:

| release 정의 | 실행 결과 | 오타가 잡히나 | 비고 |
|---|---|---|---|
| `do { (void)sizeof(c); } while (0)` | `n=0 calls=0` (평가 안 함) | 예 — `use of undeclared identifier 'no_such_var'` | `-Wunneeded-internal-declaration`: ASSERT 에서만 쓰는 static 함수 경고 |
| `((void)(c))` | `n=1 calls=1` (평가함) | 예 | 부작용·하드웨어 읽기가 release 에도 실행됨 |
| (빈 매크로) | `n=0 calls=0` | **아니오** — rc=0 | `-Wunused-function` 경고. 오타가 debug 빌드 때까지 숨는다 |

`do { … } while (0)` 로 감싸는 이유: `if (x) ASSERT(y); else …` 에서 문장 하나처럼 동작하게 (세미콜론 문제).

### Part C — 타깃 predefined 매크로와 GNU 툴체인

컴파일러는 `-D` 를 하나도 안 줘도 수백 개의 매크로를 미리 정의한다. `-dM -E - </dev/null` (빈 입력을 전처리하고 매크로만 덤프)로 본다. `demo_predef.sh` 실제 결과 — 개수: Mac arm64 479, clang thumbv7em hard 411 / softfp 410 / soft 406, arm-none-eabi-gcc hard 422 / soft 419, VM host gcc(aarch64) 434. 주요 매크로 (빈칸 = 정의 안 됨):

| 매크로 | Mac arm64 clang | clang M4 hard | clang M4 softfp | clang M4 soft | GNU M4 hard | GNU M4 soft | gcc aarch64 Linux |
|---|---|---|---|---|---|---|---|
| `__APPLE__` / `__MACH__` | 1 / 1 | | | | | | |
| `__ELF__` | | 1 | 1 | 1 | 1 | 1 | 1 |
| `__aarch64__` | 1 | | | | | | 1 |
| `__arm__`, `__thumb__`, `__thumb2__` | | 1 | 1 | 1 | 1 | 1 | |
| `__ARM_ARCH` | 8 | 7 | 7 | 7 | 7 | 7 | 8 |
| `__ARM_ARCH_7EM__` | | 1 | 1 | 1 | 1 | 1 | |
| `__ARM_ARCH_PROFILE` | `'A'` | `'M'` | `'M'` | `'M'` | 77 | 77 | 65 |
| `__ARM_FP` | 0xE | 0x6 | 0x6 | | 4 | | 14 |
| `__ARM_PCS_VFP` | | 1 | | | 1 | | |
| `__ARM_PCS` | | 1 | 1 | 1 | | 1 | |
| `__SOFTFP__` | | | | 1 | | 1 | |
| `__VFP_FP__` | | 1 | 1 | 1 | 1 | 1 | |
| `__ARM_FEATURE_DSP` | | 1 | 1 | 1 | 1 | 1 | |
| `__ARM_NEON` | 1 | | | | | | 1 |
| `__SIZEOF_POINTER__` / `__SIZEOF_LONG__` | 8 / 8 | 4 / 4 | 4 / 4 | 4 / 4 | 4 / 4 | 4 / 4 | 8 / 8 |
| `__CHAR_UNSIGNED__` | | 1 | 1 | 1 | 1 | 1 | 1 |
| `__clang__` / `__GNUC__` | 1 / 4 | 1 / 4 | 1 / 4 | 1 / 4 | — / 14 | — / 14 | — / 15 |
| `__STDC_VERSION__` | 201710L | 201710L | 201710L | 201710L | 201710L | 201710L | 202311L |

읽을 거리:

- **"Cortex-M4F 를 hard-float 로 빌드했나"** 를 가리는 매크로는 `__ARM_PCS_VFP` 하나다. `__ARM_FP` 는 softfp 에도 있고(FPU 는 쓰되 인자는 정수 레지스터로), `__VFP_FP__` 는 soft 에도 있으며(실측), `__ARM_PCS` 는 clang 이 hard 에서도 정의한다(GNU 는 안 함). → 연습 4 의 가드.
- `__ARM_FP` 는 지원 정밀도 비트마스크(ACLE: 0x2 half, 0x4 single, 0x8 double)인데 같은 `-mfpu=fpv4-sp-d16` 에 clang 은 0x6, GNU 는 4 를 낸다 — **컴파일러마다 다를 수 있다**. 값 비교보다 "정의됐나" + 필요한 비트 검사로 쓴다 (half 정밀도 변환 지원 해석 차이로 보이나 확인 권장).
- `__ARM_ARCH_PROFILE` 은 clang 이 문자 상수 `'M'`, GNU 가 숫자 77 로 적지만 `#if __ARM_ARCH_PROFILE == 'M'` 은 둘 다 참이다 (`#if` 에서 문자 상수는 정수).
- **char 부호**: Apple arm64 는 `__CHAR_UNSIGNED__` 가 없고(signed char), Linux aarch64 gcc · ARM EABI 는 unsigned. 같은 `char c = 0xFF; if (c < 0)` 가 Mac 단위 테스트와 타깃에서 다르게 돈다 — 호스트 테스트의 대표 함정.
- 호스트 gcc 15 의 기본 표준은 C23(202311L), arm-none-eabi-gcc 14 와 Apple clang 은 gnu17(201710L). 빌드 스크립트에 `-std=` 를 고정하는 이유. `-std=c11` 은 `__STDC_VERSION__ 201112L` + `__STRICT_ANSI__ 1`, `-std=gnu11` 은 `__STRICT_ANSI__` 없음 (실측).
- 최적화 레벨도 매크로를 바꾼다 (Mac clang 실측): `-O0` → `__NO_INLINE__`, `-O1/-O2/-Og` → `__OPTIMIZE__`, `-Os/-Oz` → `__OPTIMIZE__` + `__OPTIMIZE_SIZE__`. 코드가 `#ifdef __OPTIMIZE__` 로 동작을 바꾸면 debug/release 차이가 하나 더 생기는 셈이다.

**GNU 툴체인에서 같은 매트릭스** (`demo_vm.sh`, 실제 출력 발췌 — Mac 홈은 VM 에서 읽기 전용이라 VM `/tmp/TOOL-B07_work/vm_src` 로 복사 후 실행):

```text
### arm-none-eabi-gcc (Cortex-M4F, hard float), solution, compile only   -> 18/18 passed
### arm-none-eabi-gcc, practice (FPU guard uses the real GNU predefined macros) -> 13/13 passed
### host gcc (aarch64 Linux), solution, build + run                     -> 18/18 passed
```

(각 섹션의 마지막 줄을 한 줄로 모은 것. 원본: `vm_check.txt`, arm-none-eabi-gcc 14.2.1 / gcc 15.2.0)

`CC="arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16" NORUN=1` 이면 check.sh 는 링크·실행 대신 `-c` 까지만 한다 (newlib 의 `<stdio.h>` 로 컴파일은 된다). 헤더가 **컴파일러 독립적**이어야 Mac 단위 테스트와 타깃 빌드가 같은 규칙을 공유한다.

**CI 에서**: 매트릭스를 셸 루프(또는 CI 의 matrix 기능)로 돌리고, 합법 변형은 "빌드 성공", 불법 변형은 "**실패해야** 성공" 으로 둔다 — check.sh 가 바로 그 구조다. 불법 조합 테스트가 없으면 누군가 `#error` 를 지워도 아무도 모른다.

## 6. 정답 코드

<!-- code: solution.h diff=starter.h -->

```text
$ bash check.sh solution          # 앞부분 PASS E01~E10, PASS S01 (11줄) 생략
== solution: legal variants: -dM values, -E code path, -Wundef -Werror build, run
PASS L01_A_debug
PASS L02_A_release
PASS L03_B_debug_usb_shell
PASS L04_B_release_usb0
PASS L05_B_release_usb_bare
PASS L06_A_debug_log0_shell
PASS L07_A_debug_rx128
18/18 passed
```

바뀐 곳 요약: TODO 1·2 검증 블록 추가, TODO 3 `#ifdef` → 기본값 0 + `#if` 값 검사 + 조합 `#error` 4개, TODO 4 범위 `#error`, TODO 5 ASSERT 를 debug/release 로 분기, TODO 6 2의 거듭제곱 `#error` + `_Static_assert`. 실행 시간 약 2.7초 (Mac, 컴파일러 호출 33번: 불법 10 × 1, S01 × 2, 합법 7 × 3).

## 7. 자주 하는 실수

- **`#ifdef` 로 0/1 플래그 검사** → `-DFEATURE_USB=0` 이 켜짐 (T1, starter 의 L04 FAIL). 고침: `#ifndef X / #define X 0 / #endif` 후 `#if X`.
- **정의 안 된 이름을 `#if` 에** → 오타가 조용히 0 (T2). 고침: 빌드 기본 플래그에 `-Wundef` (이 레슨의 check.sh 는 `-Wundef -Werror`), 그리고 기본값을 헤더에서 항상 정의.
- **`#else` 에 기본 보드** → 보드 플래그를 빠뜨린 빌드가 B 로 조용히 나간다 (starter T10). 고침: "정확히 하나" 검증을 앞에.
- **`-DBOARD_A=0` 이면 A 가 꺼진다고 생각** → `defined(BOARD_A)` 는 값과 무관하게 참. solution 헤더에 `-DBOARD_A=0 -DBUILD_DEBUG` 를 주면 실제로 `board=A …` 로 빌드된다 (실측). 정의 여부 플래그와 값 플래그를 섞지 않는다 (또는 연습 1 처럼 `BOARD=1|2` 값 하나로).
- **덮어쓸 수 있는 값을 `#ifndef` 없이 정의** → 명령줄 `-DLOG_LEVEL=2` 가 헤더에 져서 4 가 된다 (T7b, 경고 하나뿐).
- **release ASSERT 를 빈 매크로로** → 조건식 오타가 release 빌드에서 안 잡힌다. `((void)(c))` 는 부작용을 실행한다 (T11). 고침: `do { (void)sizeof(c); } while (0)`. 그리고 ASSERT 안에 부작용(`ASSERT(n++ …)`, 레지스터 읽기)을 넣지 않는다.
- **`#if` 에 sizeof/캐스트/enum** → 에러거나(T4) 엉뚱한 값(enum 상수 이름은 0). 타입이 필요하면 `_Static_assert`.
- **`-DFOO=$(VAR)` 에서 VAR 가 비어 있음** → `#if FOO != 0` 이 `invalid token` (T3b). Makefile/CMake 에서 기본값을 주거나 `ifeq` 로 걸러 낸다.

## 8. 면접 Q&A

<details>
<summary>Q1. #ifdef X 와 #if X 의 차이는? -DX=0 이면 각각 어떻게 되나요?</summary>

`#ifdef X` 는 "X 가 매크로로 정의되어 있나" 만 본다 — 값이 0 이어도 참. `#if X` 는 X 를 치환해 **값**을 계산한다 — 0 이면 거짓, 정의 안 됐으면 이름이 0 으로 바뀌어 거짓(C11 6.10.1p4), 빈 매크로면 에러. 실측: `-DFEATURE_USB=0` 에서 `#ifdef` 블록은 남고 `#if` 블록은 사라졌다. 그래서 기능 플래그는 "기본값 0 을 헤더에서 정의 + `#if`" 가 안전하고, `-Wundef` 로 오타를 잡는다. 꼬리: Linux 커널은 Kconfig 가 `CONFIG_X` 를 정의/미정의로만 주므로 `#ifdef`/`IS_ENABLED()` 를 쓴다 — 관례가 프로젝트마다 다르니 하나로 통일하는 것이 핵심.

</details>

<details>
<summary>Q2. 이 릴리스 바이너리에 디버그 셸 코드가 들어갔는지 어떻게 확인하나요?</summary>

빌드 단계별로: ① 그 변형의 플래그로 `cc -E -dM file.c | grep HAS_SHELL` → `#define HAS_SHELL 0` 인지, ② `cc -E -P file.c | grep -c shell_start` 가 0 인지 (전처리에서 제거됐나), ③ 최종 ELF 에서 `nm fw.elf | grep shell_` / `strings` 로 심볼·문자열이 없는지 (TOOL-B04). ①②는 "소스가 그렇게 설계됐나", ③은 "실제로 구운 파일이 그런가" 다. 그리고 CI 에서 "release + FEATURE_SHELL=1 은 빌드가 **실패**해야 한다" 를 테스트로 둔다 (check.sh E07).

</details>

<details>
<summary>Q3. #error 와 _Static_assert 는 언제 각각 쓰나요?</summary>

`#error` 는 전처리 단계 — `#if` 로 계산 가능한 것(매크로 정의 여부, 정수 산술)에 쓰고, 가장 일찍 터진다(`-E` 만 돌리는 도구에서도 — `cc -MM` 의존성 생성도 `#error` 로 rc=1, 실측). `_Static_assert` 는 컴파일 단계 — `sizeof`, 캐스트, `offsetof`, enum 처럼 **타입 시스템**이 필요한 규칙. 예: `RX_BUF_SIZE-1` 이 `uint8_t` 인덱스에 들어가는지는 `typedef` 를 알아야 하므로 `_Static_assert`; 실측으로 `-DRX_BUF_SIZE=512` 는 `-E` rc=0, `-fsyntax-only` 에서 `'255 == 511'` 로 실패했다. 링크 결과(섹션 크기 합, 주소)는 둘 다 못 보고 링커 스크립트 `ASSERT` 가 맡는다 (TOOL-M01). C23 에서는 `static_assert` 키워드, 메시지 생략 가능.

</details>

<details>
<summary>Q4. 크로스 컴파일러가 어떤 매크로를 미리 정의하는지 어떻게 알아내나요?</summary>

`arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -dM -E - </dev/null | sort` — **타깃 플래그를 실제 빌드와 똑같이** 줘야 한다(같은 컴파일러도 `-mfloat-abi` 에 따라 `__ARM_PCS_VFP`, `__SOFTFP__` 가 바뀐다). 두 설정을 `diff` 하면 차이가 바로 보인다. 실측 예: clang 과 GCC 는 같은 M4F 에서 `__ARM_FP` 를 0x6 / 4 로 다르게, `__ARM_ARCH_PROFILE` 을 `'M'` / 77 로 다르게 적는다. 그래서 판단은 ACLE 표준 매크로(`__ARM_ARCH`, `__ARM_PCS_VFP`, `__ARM_FEATURE_DSP`)로 하고 값 대신 정의 여부/비트를 본다.

</details>

<details>
<summary>Q5. 보드 선택을 -DBOARD_A 같은 플래그로 하는 것과 -DBOARD=2 같은 값으로 하는 것, 장단점은?</summary>

플래그 방식은 `#ifdef BOARD_A` 로 읽기 쉽지만 "둘 다 / 둘 다 없음" 을 따로 막아야 하고, `-DBOARD_A=0` 이 여전히 A 인 함정이 있다. 값 방식은 한 매크로라 "둘 다" 가 원천적으로 불가능하고 `#if BOARD == BOARD_ID_B` 로 범위 검사가 쉽다. 대신 `-DBOARD=B` (정의 안 된 이름 → 0) 같은 실수가 생기므로 범위 `#error` + `-Wundef` 가 필요하고, `-DBOARD` (값 없음 = 1) 는 조용히 1번 보드가 된다(실측). 큰 프로젝트는 아예 생성 헤더(Kconfig `autoconf.h`, CMake `configure_file`)로 간다 — 입력 검증을 설정 도구가 맡는다.

</details>

<details>
<summary>Q6. #if 로 너무 많이 분기한 코드의 문제는? 대안은?</summary>

`#if` 로 꺼진 코드는 컴파일러가 **보지도 않으므로** 그 변형을 빌드하지 않으면 문법 오류·타입 오류가 숨어 있다 ("debug 빌드만 깨짐" 사고). 변형 수는 곱으로 늘어난다(보드 2 × 빌드 2 × 기능 2×2 = 16). 대안: ① 분기를 board_config.h 한 곳에 모으고 앱은 파생 매크로만, ② `if (IS_ENABLED(FEATURE_USB)) usb_init();` 처럼 **일반 if** 로 써서 양쪽 다 컴파일되게 하고 죽은 코드는 최적화가 지우게 (연습 3), ③ 보드 차이는 함수 테이블/링크 시 선택(board_a.c vs board_b.c)으로, ④ CI 에서 모든 합법 변형 빌드.

</details>

<details>
<summary>Q7. 왜 기본 빌드 플래그에 -Wundef 를 넣나요? -Wall 에 없나요?</summary>

없다 — 실측으로 clang 21 과 gcc 15 모두 `-Wall -Wextra` 에서 `#if FEATUER_USB` 오타에 아무 경고도 안 냈다. `#if` 의 미정의 이름 = 0 규칙은 표준 동작이라 기본 경고가 아니다. 펌웨어는 설정 매크로 오타 하나로 기능이 통째로 빠지므로 `-Wundef` 를 켜고, 그러려면 헤더가 모든 설정 매크로의 기본값을 **정의**해 둬야 한다(이 레슨의 `#ifndef FEATURE_USB / #define FEATURE_USB 0`). 서드파티 헤더가 시끄러우면 `-isystem` 으로 (TOOL-B02).

</details>

<details>
<summary>Q8. ASSERT 를 release 에서 끄는 올바른 방법과, 끄는 것 자체의 위험은?</summary>

`#define ASSERT(c) do { (void)sizeof(c); } while (0)` — 호출과 평가는 없고(sizeof 피연산자는 평가 안 됨, C11 6.5.3.4p2) 타입 검사는 남는다. 실측: 빈 매크로는 오타를 놓쳤고 `((void)(c))` 는 `n++` 를 실행했다. 위험: ASSERT 안의 부작용이 release 에서 사라져 동작이 달라진다(`ASSERT(spi_read(...) == OK)` 같은 코드). 펌웨어는 release 에서도 "치명적 조건은 남겨서 fault 로그 + 리셋" 정책을 쓰는 경우가 많다 — 표준 `assert` 는 `NDEBUG` 로 끄고, 프로젝트 ASSERT 는 레벨을 나눈다(TOOL-B13 의 컴파일 타임 레벨과 같은 방식).

</details>

## 9. 추가 연습

`practice.h` 는 solution 을 **값 방식 보드 선택**으로 바꾸고, 아래 연습의 답을 모두 담은 헤더다. check.sh 가 별도 매트릭스(P01~P13)로 검증한다.

### 연습 1 — `-DBOARD=1|2` 값 방식으로 바꾸기

`BOARD_ID_A 1`, `BOARD_ID_B 2` 를 정의하고 `-DBOARD=1`, `-DBOARD=2`, `-DBOARD=BOARD_ID_B` 를 모두 받게. 미설정·범위 밖은 `#error`. 옛 플래그 `-DBOARD_A` 를 쓰면 "옮기는 법" 을 알려 주는 `#error`.

<details>
<summary>답</summary>

```c
#if defined(BOARD_A) || defined(BOARD_B)
#error "board_config: old flag -DBOARD_A/-DBOARD_B; use -DBOARD=1 (A) or -DBOARD=2 (B)"
#elif !defined(BOARD)
#error "board_config: BOARD is not set; use -DBOARD=1 (A) or -DBOARD=2 (B)"
#elif BOARD != BOARD_ID_A && BOARD != BOARD_ID_B
#error "board_config: BOARD must be 1 (A) or 2 (B)"
#endif
```

이후 `defined(BOARD_A)` 는 전부 `BOARD == BOARD_ID_A` 로. `-DBOARD=BOARD_ID_B` 는 `#if` 안에서 BOARD → BOARD_ID_B → 2 로 재치환되어 동작한다 (P03). `-dM` 덤프는 `#define BOARD BOARD_ID_B` 그대로다(T8). 남는 함정: `-DBOARD` (값 없음)는 1 = 보드 A 로 조용히 빌드된다 (실측) — 막을 수 없으니 빌드 시스템에서 값을 강제한다.

</details>

### 연습 2 — 잘못된 값의 실제 진단 읽기

`-DBOARD=B` (따옴표 없는 글자), `-DBOARD=3`, `-DBOARD=` 를 각각 줬을 때 어떤 에러가 몇 개 나오는지 예측하고 확인하라.

<details>
<summary>답</summary>

실측 (practice.h, `-Wundef -Werror`): `-DBOARD=B` → `'B' is not defined, evaluates to 0 [-Werror,-Wundef]` 5개 + 우리 `#error "BOARD must be 1 (A) or 2 (B)"` 1개 = 6 errors (B 가 0 으로 바뀌어 범위 검사에 걸림, `#error` 뒤에도 전처리가 계속되어 연쇄). `-DBOARD=3` → 우리 `#error` 만 (P05). `-DBOARD=` → `invalid token at start of a preprocessor expression` 5개, 우리 문구는 안 나온다 — 빈 값은 `#if` 식 자체를 깨뜨린다. 그래서 check.sh 는 P06(`-DBOARD=B`)에서 "우리 문구가 **포함**되어 있나" 만 본다.

</details>

### 연습 3 — `IS_ENABLED(X)`: 정의 안 됨 / 0 / 빈 매크로 모두 0

Linux `include/linux/kconfig.h` 의 기법으로, 매크로가 **1 로 정의됐을 때만** 1 로 펼쳐지는 `IS_ENABLED(x)` 를 만들어라. `#if` 와 일반 `if ()` 둘 다에서 써야 하고, `defined` 를 매크로로 만들어 내면 안 된다(T6).

<details>
<summary>답</summary>

```c
#define BC_PLACEHOLDER_1 0,
#define BC_SECOND(ignored, val, ...) val
#define IS_ENABLED(x) BC_IS_ENABLED1(x)
#define BC_IS_ENABLED1(val) BC_IS_ENABLED2(BC_PLACEHOLDER_##val)
#define BC_IS_ENABLED2(arg1_or_junk) BC_SECOND(arg1_or_junk 1, 0, 0)
```

`F_ON` 이 1 이면 `BC_PLACEHOLDER_1` → `0,` 가 되어 `BC_SECOND(0, 1, 0, 0)` → 1. 정의 안 됨/0/빈 값이면 `BC_PLACEHOLDER_F_NONE` 같은 의미 없는 토큰 하나 → `BC_SECOND(junk 1, 0, 0)` → 0. 한 단계 더 거치는 `BC_IS_ENABLED1` 은 `##` 전에 인자를 먼저 펼치기 위해서다. 커널판은 `__take_second_arg(arg1_or_junk 1, 0)` 처럼 인자 2개로 부르는데(기억 기준, 확인 권장), 그러면 `...` 가 빈다. 2개 버전을 `-std=c11 -Wpedantic` 으로 실측하면 `warning: passing no argument for the '...' parameter of a variadic macro is a C23 extension [-Wvariadic-macro-arguments-omitted]` → 여기서는 `, 0, 0` 로 3개를 넘겨 조용하게 했다. 확인(P10): `ie_on = 1; ie_off = 0; ie_none = 0; ie_empty = 0;` 와 `#if IS_ENABLED(F_ON) && !IS_ENABLED(F_NONE)` 블록 생존. 쓰임: `if (IS_ENABLED(FEATURE_USB)) usb_init();` → 양쪽 코드가 항상 컴파일되어 타입 검사되고, 상수 조건이라 죽은 쪽은 최적화가 지운다 (단 `usb_init` 선언은 항상 보여야 한다).

</details>

### 연습 4 — predefined 매크로로 FPU ABI 가드

보드 2 는 Cortex-M4F 다. 32비트 ARM 으로 컴파일하는데 hard-float ABI 가 아니면 `#error`. 호스트(Mac arm64) 빌드는 통과해야 한다.

<details>
<summary>답</summary>

```c
#if BOARD == BOARD_ID_B && defined(__arm__) && !defined(__ARM_PCS_VFP)
#error "board_config: BOARD 2 (Cortex-M4F) needs -mfloat-abi=hard -mfpu=fpv4-sp-d16"
#endif
```

Part C 표에서 `__ARM_PCS_VFP` 만 hard 에서만 정의된다 (`__ARM_FP` 는 softfp 에도 있음, `__VFP_FP__` 는 soft 에도 있음). `__arm__` 은 32비트 ARM 에서만 정의되고 Mac arm64·Linux aarch64 에는 없으므로 호스트 빌드는 통과. 실측: clang `--target=thumbv7em-none-eabi -mcpu=cortex-m4` 에서 soft → 에러(P11), softfp → 에러, hard → 통과(P12), 보드 1 + soft → 통과(P13). VM 의 arm-none-eabi-gcc 로도 같은 3개 PASS (`vm_check.txt`). 왜 막나: hard 와 soft ABI 오브젝트는 섞어 링크할 수 없고, 벤더 DSP 라이브러리는 보통 한 ABI 로만 배포된다. VM 실측 (hard 로 만든 `h.o` + soft 로 만든 `m.o` 를 soft 로 링크):

```text
.../arm-none-eabi/bin/ld: error: h.o uses VFP register arguments, x.elf does not
.../arm-none-eabi/bin/ld: failed to merge target specific data of file h.o
collect2: error: ld returned 1 exit status
```

헤더 가드는 이 링크 에러를 **첫 파일의 전처리** 시점으로 앞당겨, 원인("-mfloat-abi=hard 가 필요")을 문장으로 알려 준다.

</details>

### 한 번에 검증

<!-- code: practice.h diff=solution.h -->

```text
$ bash check.sh practice          # 앞부분 PASS P01~P09 (값 방식 보드), PASS P10 (IS_ENABLED) 생략
== practice: FPU guard via predefined macros (32-bit ARM target, BOARD 2 = Cortex-M4F)
PASS P11_m4_soft_board2
PASS P12_m4_hard_board2
PASS P13_m4_soft_board1
13/13 passed
```

## 10. 핵심 정리

- 흐름은 한 방향: **-D 입력 → board_config.h 검증(#error) → 파생 매크로 → 앱의 #if**. 앱 코드는 입력 매크로를 직접 보지 않는다.
- "정확히 하나" 는 `#if defined(A) && defined(B)` / `#elif !defined(A) && !defined(B)` + `#error`. `#else` 기본값은 보드 같은 입력엔 사고다.
- 0/1 플래그는 `#ifndef X → #define X 0` 후 `#if X`. `#ifdef` 는 `-DX=0` 도 참. `-D` 값 생략 = 1.
- `#if` 의 모르는 이름 = 0 → `-Wundef` (`-Wall -Wextra` 에 없음, 실측). 빈 매크로·빈 `-D` 값은 `#if` 에서 에러.
- 숫자 규칙은 `#error` (전처리, `-E` 에서 실패), 타입 규칙은 `_Static_assert` (컴파일, `-E` 는 통과).
- 증명: `cc <flags> -E -dM f.c | grep '^#define X '` (정의 그대로, 계산 안 함), `cc <flags> -E -P f.c | grep` (남은 코드 경로).
- 타깃 판별은 `-dM -E - </dev/null` 로 덤프해 ACLE 매크로로: hard-float = `__ARM_PCS_VFP`, Cortex-M = `__ARM_ARCH_PROFILE == 'M'`, 32비트 ARM = `__arm__`. 값은 컴파일러마다 다를 수 있다 (`__ARM_FP` 0x6 vs 4).
- release ASSERT = `do { (void)sizeof(c); } while (0)`: 호출 없음, 평가 없음, 타입 검사 있음. 불법 조합도 CI 테스트로 "실패해야 통과".

## 참고자료

- ISO/IEC 9899:2011 (C11) — 6.10.1 조건부 포함(`#if`, `defined`, 미정의 이름 = 0, intmax_t 산술), 6.10.3 매크로 치환(`#`, `##`, 가변 인자), 6.10.5 `#error`, 6.7.10 `_Static_assert`, 6.5.3.4 `sizeof` 피연산자 비평가, 4p4 `#error` 와 번역 실패. 초안 N1570: https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
- GCC 매뉴얼 — Preprocessor Options (`-D`, `-U` 순서, `-dM`, `-dD`, `-P`): https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html , Warning Options (`-Wundef`, `-Wexpansion-to-defined`): https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
- The C Preprocessor (cpp 매뉴얼) — Conditionals, Defined, Diagnostics: https://gcc.gnu.org/onlinedocs/cpp/
- Clang 컴파일러 사용 설명서 — 진단 플래그 레퍼런스(`-Wundef`, `-Wmacro-redefined`, `-Wexpansion-to-defined`): https://clang.llvm.org/docs/DiagnosticsReference.html
- Arm C Language Extensions (ACLE) — `__ARM_ARCH`, `__ARM_ARCH_PROFILE`, `__ARM_FP`, `__ARM_PCS_VFP`, `__ARM_FEATURE_DSP`: https://arm-software.github.io/acle/main/acle.html
- Linux `include/linux/kconfig.h` — `IS_ENABLED()`, `__is_defined()`: https://github.com/torvalds/linux/blob/master/include/linux/kconfig.h
- 관련 레슨: TOOL-B01 (컴파일 단계, -E), TOOL-B02 (경고 기준선), TOOL-B04 (nm 으로 심볼 확인), TOOL-B13 (컴파일 타임 로그 레벨), TOOL-B14 (크로스 컴파일 타깃), TOOL-M01 (링커 스크립트 ASSERT), TOOL-M04/M05 (CMake 옵션·툴체인 파일).
