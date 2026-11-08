---
id: C-A01
time: 100분
updated: 2026-11-08
---
# C-A01 호스트 테스트 가능한 레지스터 접근 계층 설계

## 학습 목표

- `REG_READ` / `REG_WRITE` / `REG_MODIFY` 를 **빌드 옵션 하나(`-DREG_TARGET`)로** 타깃의 `volatile` 직접 접근 ↔ 호스트의 기록형 mock 으로 바꾸는 계층을 설계하고, 타깃 쪽 비용이 0 임을 Cortex-M4 asm 으로 증명한다.
- 호스트 mock 을 **세 부분**(접근 로그 · 스크립트 읽기 · 하드웨어 규칙 모델)으로 나누고, 각각이 어떤 종류의 드라이버 버그를 잡는지 설명한다.
- SPI 드라이버 `spi_init` 이 데이터시트가 요구하는 **레지스터 순서와 값**을 쓰는지 기대 시퀀스 표로 단위 테스트하고, 실패 메시지가 "몇 번째 접근, 기대/실제 값, 소스 줄" 을 바로 알려 주게 만든다.
- 정확한 시퀀스 · 쓰기만 · 규칙 · 최종 상태 — 네 가지 검사 수준의 장단점(깨지기 쉬움 vs 잡는 버그)을 구분해, 면접에서 "그 테스트 너무 brittle 하지 않나요?" 에 답한다.
- link seam · 함수 포인터 ops · RAM 가짜 구조체와 비교해 이 설계를 언제 고르는지 말할 수 있다 (자세한 비교 실습은 TEST-B04).

## 1. 문제

시니어 펌웨어 면접의 설계 질문 단골: *"하드웨어 없이 드라이버를 어떻게 테스트하나요?"* 답이 "보드에 올려 봅니다" 면 거기서 끝이다. 면접관이 듣고 싶은 것은 **레지스터 접근을 한 곳으로 모으고, 그 한 곳을 호스트에서 바꿔 끼워, 드라이버가 데이터시트대로 레지스터를 만지는지 CI 에서 매 커밋 검사하는 구조** 와, 그 구조의 비용·한계다.

C-M01 에서는 레지스터를 `volatile` 구조체 오버레이로 표현하고, 호스트에서는 베이스 주소를 정렬된 RAM 배열로 바꿔 "최종 값" 을 확인했다. 그 방식은 **무엇을 썼는지** 는 보지만 **어떤 순서로, 몇 번, 읽기를 했는지** 는 못 본다. 그런데 주변장치 버그의 대부분은 순서다: 클록을 켜기 전에 설정을 씀, 설정 전에 enable, 켜진 채로 분주 변경, 읽어야 지워지는 플래그를 안 읽음. 이번 레슨은 접근 **하나하나를 가로채 기록** 한다.

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">드라이버 소스는 하나, 레지스터 접근의 "끝" 만 빌드 옵션으로 바뀐다</text>
  <rect x="190" y="36" width="360" height="46" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="204" y="55" fill="currentColor">spi_drv.c — spi_init(), spi_set_speed()</text>
  <text x="204" y="73" fill="currentColor" font-family="monospace" font-size="12">REG_WRITE(SPI1_CR1, cr1 | SPI_CR1_SPE);</text>
  <path d="M370 82 L370 98" stroke="currentColor"/>
  <rect x="190" y="98" width="360" height="46" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="204" y="117" fill="currentColor">hal_reg.h — REG_READ / REG_WRITE / REG_MODIFY</text>
  <text x="204" y="135" fill="currentColor" font-family="monospace" font-size="12">reg_write32((uintptr_t)a, v, __LINE__)</text>
  <path d="M300 144 L180 170" stroke="currentColor"/>
  <path d="M440 144 L560 170" stroke="currentColor"/>
  <rect x="10" y="170" width="340" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="22" y="190" fill="currentColor" font-weight="bold">-DREG_TARGET (arm-none-eabi 펌웨어)</text>
  <text x="22" y="210" fill="currentColor" font-family="monospace" font-size="12">*(volatile uint32_t *)0x40013000u = v;</text>
  <text x="22" y="230" fill="currentColor">→ str r0, [r2] 한 명령. 함수 호출 0</text>
  <rect x="390" y="170" width="340" height="70" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="402" y="190" fill="currentColor" font-weight="bold">(기본) 호스트 단위 테스트</text>
  <text x="402" y="210" fill="currentColor" font-family="monospace" font-size="12">mock_reg_write(0x40013000u, v, 277)</text>
  <text x="402" y="230" fill="currentColor">주소는 "키" 일 뿐, 역참조하지 않는다</text>
  <rect x="10" y="256" width="340" height="64" rx="6" fill="none" stroke="currentColor"/>
  <text x="22" y="278" fill="currentColor">APB2 버스 → 진짜 SPI1 주변장치</text>
  <text x="22" y="300" fill="currentColor">검증 수단: HIL, 로직 분석기, 디버거</text>
  <rect x="390" y="256" width="340" height="64" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="402" y="275" fill="currentColor" font-size="12">① 접근 로그 g_log[]: R/W, 주소, 값, 소스 줄</text>
  <text x="402" y="293" fill="currentColor" font-size="12">② 하드웨어 규칙 모델: g_val[], 위반 비트 g_viol</text>
  <text x="402" y="311" fill="currentColor" font-size="12">③ 스크립트 읽기: SR 이 BSY, BSY, TXE 순으로</text>
  <text x="10" y="335" fill="currentColor">테스트는 오른쪽 아래 세 가지를 본다: mock_check_seq() · mock_peek() · g_viol</text>
</svg>
```

대상은 STM32F4 (RM0090) 의 SPI1 과 RCC 일부다. 주소는 데이터시트 정수를 그대로 쓴다 — 호스트 빌드는 이 주소를 **절대 역참조하지 않으므로** C-M01 처럼 가짜 베이스 배열이 필요 없다.

| 레지스터 | 주소 | 이 레슨에서 쓰는 비트 | 리셋 값 |
|---|---|---|---|
| `RCC_APB2RSTR` | 0x4002_3824 | b12 SPI1RST | 0 |
| `RCC_APB2ENR` | 0x4002_3844 | b12 SPI1EN (b4 USART1EN 은 "다른 비트 보존" 확인용) | 0 |
| `RCC_APB1ENR` | 0x4002_3840 | b14 SPI2EN — SPI1 과 헷갈리는 함정. mock 맵에는 일부러 없다 | — |
| `SPI1_CR1` | 0x4001_3000 | CPHA b0, CPOL b1, MSTR b2, BR[2:0] b5:3, SPE b6, LSBFIRST b7, SSI b8, SSM b9, DFF b11 | 0 |
| `SPI1_CR2` | 0x4001_3004 | SSOE b2 (ERRIE b5 는 연습) | 0 |
| `SPI1_SR` | 0x4001_3008 | RXNE b0, TXE b1, MODF b5, OVR b6, BSY b7 (읽기 전용으로 모델링) | 0x0002 |
| `SPI1_DR` | 0x4001_300C | 데이터 | 0 |

### Part A — 접근 계층과 기록 mock

- `reg_modify32(a, clr, set, line)` — RMW 를 **계층 함수를 거쳐서**: 읽고, `clr` 지우고 `set` 세우고, 쓴다. 값이 같아도 쓴다.
- `log_push(op, a, v, line)` — 접근 로그에 한 줄. 꽉 차면 `V_LOG_FULL` 만 세우고 버린다 (배열 밖 쓰기 금지 — ASan 으로도 확인).
- `mock_reg_read(a, line)` — 맵에 없는 주소면 `V_UNKNOWN` + 0. 아직 안 쓴 **같은 주소의 스크립트 항목** 이 있으면 소비해서 하드웨어 값을 바꾼 뒤, 규칙 모델(`model_read`)로 읽고, CPU 가 받은 값을 로그에 남긴다. (`mock_reg_write` 는 주어진다.)
- `mock_check_seq(exp, n, msg, cap)` — 로그를 기대 표와 비교해 같으면 -1, 아니면 첫 차이 인덱스와 사람이 읽을 메시지. `EXP_W(a, v)` 는 값 전체, `EXP_WM(a, v, m)` 은 `m` 비트만, `EXP_R(a)` 는 값 무시.

### Part B — 하드웨어처럼 행동하는 mock: 규칙 모델

`model_read(i)` / `model_write(i, v)` 를 채워 다음 규칙을 모델링한다. 주어진 **버그 드라이버 4개** 가 각각 맞는 위반 비트로 잡혀야 하고, 올바른 시퀀스는 위반 0 이어야 한다.

| 규칙 | 근거 | 위반 비트 |
|---|---|---|
| 클록 꺼진 SPI1: 쓰기 무시, 읽기 0 | RCC 클록 게이팅 | `V_CLK_OFF` |
| `APB2RSTR.SPI1RST=1` 쓰기 → SPI1 레지스터 전부 리셋 값 | RCC 리셋 | — |
| SR 쓰기는 무시 | 상태 레지스터 (F4 실제로는 CRCERR 만 rc_w0) | `V_RO_WRITE` |
| SPE=1 인 채로 BR/CPOL/CPHA/MSTR/DFF/LSBFIRST 변경 | RM0090 비트 설명 "통신 중 바꾸지 말 것" / DFF "SPE=0 일 때만" | `V_CFG_WHILE_ON` |
| SPE·MSTR·SSM=1 인데 SSI=0 → SR.MODF=1, SPE·MSTR 강제 0 | 마스터 mode fault (내부 NSS 가 low) | `V_MODF` |

### Part C — SPI1 드라이버를 기대 시퀀스로 검증

- `spi_init(cfg)` — 설정이 틀리면(NULL, mode > 3, 분주 불가) **버스 접근 0회** 로 -1. 맞으면 ① `APB2ENR.SPI1EN` (RMW) ② `APB2ENR` read-back ③ `APB2RSTR` 리셋 펄스 ④ CR1 (SPE=0) ⑤ CR2 ⑥ CR1 | SPE. 분주는 주어진 `spi_pick_br` 로.
- `spi_set_speed(pclk, max_hz, max_polls)` — 동작 중 속도 변경. CR1 을 한 번 읽고, 같으면 끝, 꺼져 있으면 한 번 쓰고 끝. 켜져 있으면 SR 을 최대 `max_polls` 번 읽어 `TXE=1 && BSY=0` 을 기다리고(시간 초과면 CR1 을 안 쓰고 -1), SPE 끄기 → BR 바꾸기 → SPE 켜기.

> 레지스터 구조체·정수→포인터 캐스트는 C-M01, RMW 와 W1C 는 C-M02, ISR 과 RMW 경쟁은 C-M14, 매크로 vs inline 은 C-M11, 레지스터 표를 X-macro 로 생성하는 것은 C-A07, 레이아웃 `_Static_assert` 는 C-A08 에서 다룬다.

## 2. Problem (English)

> **Design a host-testable register access layer and unit-test an SPI init sequence.**
>
> All driver register traffic goes through `REG_READ(addr)`, `REG_WRITE(addr, val)` and `REG_MODIFY(addr, clr, set)`. With `-DREG_TARGET` they must compile to plain `volatile` 32-bit loads/stores (zero overhead on Cortex-M); by default they call a host mock that never dereferences the address.
>
> 1. Implement the mock: an access log `{op, addr, value, source line}` with overflow protection, scripted read values per address (FIFO), and `int mock_check_seq(const reg_expect_t *exp, int n, char *msg, size_t cap)` that returns −1 on a match or the index of the first mismatch with a message such as `#8: expected W SPI1_CR2 0x00000000, got W SPI1_CR1 0x0000035F (line 16)`. Expectations may mask bits; reads may be "any value".
> 2. Add a hardware rule model: SPI1 accesses with its RCC clock off are ignored/read 0; `SPI1RST` resets SPI1 registers; SR is read-only; changing BR/CPOL/CPHA/MSTR/DFF/LSBFIRST while `SPE=1` is flagged; enabling master with `SSM=1, SSI=0` raises MODF and clears SPE/MSTR.
> 3. Implement `int spi_init(const spi_cfg_t *cfg)` (clock on, read-back, reset pulse, CR1 with SPE=0, CR2, CR1|SPE; no bus access on invalid config) and `int spi_set_speed(uint32_t pclk_hz, uint32_t max_hz, int max_polls)` (bounded wait for `TXE && !BSY`, then disable → change BR → re-enable).
>
> Example: `pclk = 84 MHz, max = 10 MHz, mode 3, software NSS` → `BR = 3` (5.25 MHz), CR1 writes `0x31F` then `0x35F`; full trace has 10 accesses.
>
> Follow-ups: why an inline function behind the macro? How do you keep the mock honest? Isn't an exact-sequence test a change detector? How would you test CMSIS-style `SPI1->CR1 = x` code?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄들과 "9/53 passed" — TODO(A1–A4, B1–B2, C1–C2) 를 채워 53/53 을 만든다
```

파일 하나에 실제 프로젝트의 다섯 파일(`stm32f4_spi_regs.h`, `hal_reg.h`, `reg_mock.c`, `spi_drv.c`, `test_spi.c`)을 순서대로 담았다. 실제 저장소에서는 `reg_mock.c` 와 `test_spi.c` 만 호스트 테스트 타깃에 링크되고, 펌웨어 빌드는 `-DREG_TARGET` 으로 `spi_drv.c` 를 컴파일한다.

관찰용 파일 (하네스와 별개, 결과는 5. 풀이에 실측으로 실었다):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -DREG_TARGET -S demo_target.c -o -   # 타깃: ldr/str 만
cc -std=c11 -O2 -S demo_target.c -o -                                                               # 호스트: bl _mock_reg_*
cc -std=c11 -Wall -Wextra -Werror -O2 -DREG_TARGET solution.c -o x                                  # 호스트에 REG_TARGET → #error
cc -std=c11 -Wall -Wextra -Werror -O2 demo_mismatch.c -o demo_mismatch && ./demo_mismatch             # 실패 메시지·위반 trace
```

## 4. 힌트

<details>
<summary>힌트 1 — Part A: 매크로는 얇게, 몸통은 static inline 함수</summary>

매크로가 하는 일은 딱 두 가지다: 인자를 `uintptr_t` / `uint32_t` 로 캐스트하고, **호출한 자리의** `__LINE__` 을 붙이는 것. 나머지는 함수에 둔다 — 인자가 한 번만 평가되고(C-B15), 타입 검사가 되고, 디버거에서 브레이크포인트를 걸 수 있다. `reg_modify32` 가 `*(volatile uint32_t *)a` 를 직접 쓰면 mock 을 **우회** 한다 (호스트에서 0x4002_3844 역참조 → SIGSEGV). 반드시 `reg_read32` / `reg_write32` 를 부른다.

</details>

<details>
<summary>힌트 2 — Part A: 스크립트 항목은 "읽기 직전 하드웨어가 바뀐 것"</summary>

스크립트 값을 그냥 반환하면 규칙 모델(클록 꺼짐 → 0)과 따로 놀고, 스크립트가 끝난 다음 읽기가 엉뚱한 옛 값을 본다. 대신 `g_val[i] = 스크립트 값` 으로 **하드웨어 상태를 바꾼 다음** `model_read(i)` 로 읽어라. 그러면 BSY 가 두 번 보인 뒤 스크립트가 바닥나도 마지막 값이 유지된다(테스트 3번째 SR 읽기). 주소가 다른 항목은 건너뛴다.

</details>

<details>
<summary>힌트 3 — Part A: 비교 루프는 max(n, g_nlog) 까지</summary>

`k < n || k < g_nlog` 로 돌면서 세 경우를 나눈다: 기대가 끝났는데 로그가 남음(extra), 로그가 끝났는데 기대가 남음(end of log), 둘 다 있는데 다름. 값 비교는 `((got ^ want) & mask) != 0`. mask 0 이면 값은 자동으로 무시된다.

</details>

<details>
<summary>힌트 4 — Part B: 검사 순서와 MODF 조건</summary>

클록 검사를 맨 앞에(꺼져 있으면 아무 일도 안 일어난다). CR1 은 **이전 값** `old` 를 먼저 잡아 둔 뒤 "old 에 SPE 가 있고 `(old ^ v) & CR1_LOCKED` 가 0 이 아님" 을 본다 — SPE 만 끄고 켜는 쓰기는 위반이 아니다. MODF 는 네 비트를 한꺼번에: `(v & (SPE|MSTR|SSM|SSI)) == (SPE|MSTR|SSM)`.

</details>

<details>
<summary>힌트 5 — Part C: 검증 먼저, 그다음 버스</summary>

`spi_pick_br` 와 mode 검사를 **첫 레지스터 접근 전에** 끝내야 "실패 → 접근 0회" 테스트가 통과한다. SPI mode 번호는 `(CPOL << 1) | CPHA` 이고 CR1 의 비트 1:0 이 바로 CPOL:CPHA 라서 `cr1 |= mode` 한 줄이면 된다. `spi_set_speed` 의 폴링은 `for (n = 0; n < max_polls && !ready; n++)` — 상한 없는 `while` 은 mock 에서 테스트를 영원히 멈추게 한다.

</details>

## 5. 풀이

### 0. 설계 선택지: "하드웨어 없이" 의 다섯 가지 방법

| 방법 | 어떻게 바꿔 끼우나 | 순서·읽기 관찰 | 타깃 비용 | 침투성 | 이 트랙에서 |
|---|---|---|---|---|---|
| RAM 가짜 구조체 | `#define SPI1 (&ram_spi1)` | 못 봄 (최종 값만) | 0 | 낮음 | C-M01, 연습 4 |
| **접근 계층 + 기록 mock** | `REG_*` 의 끝을 `#if` 로 | **전부** | 0 (inline) | 중간: 모든 접근이 `REG_*` | **이 레슨** |
| link seam | 같은 이름의 `reg_write32()` 를 테스트용 .c 로 링크 | 전부 | 함수 호출 1회 (LTO 없으면) | 낮음 | TEST-B04 |
| 함수 포인터 ops | 드라이버가 `const bus_ops_t *` 를 받음 | 전부 | 간접 호출 | 높음 (API 가 바뀜) | TEST-B04 |
| CMock 식 기대 큐 | 헤더에서 mock 생성, `Expect/Return` | 함수 단위 | 0 | 낮음 (생성기 필요) | TEST-B05 |

이 레슨이 컴파일 타임 스위치를 고른 이유: 레지스터 접근은 드라이버에서 가장 자주 일어나는 연산이라 **타깃에서 한 명령** 이어야 하고(아래 asm), 레지스터 단위 순서를 봐야 하기 때문이다. 대가는 "모든 레지스터 접근은 `REG_*` 로" 라는 코딩 규칙이다 — 리뷰와 `grep 'volatile uint32_t \*'` 로 지킨다.

### Part A — 계층: 매크로는 줄 번호만, 몸통은 inline

```c
#if defined(REG_TARGET)
#  if !defined(__arm__)
#    error "REG_TARGET 은 32비트 ARM(MCU) 빌드 전용: ..."
#  endif
static inline uint32_t reg_read32(uintptr_t a, int line) { (void)line; return *(const volatile uint32_t *)a; }
static inline void reg_write32(uintptr_t a, uint32_t v, int line) { (void)line; *(volatile uint32_t *)a = v; }
#else
static inline uint32_t reg_read32(uintptr_t a, int line) { return mock_reg_read(a, line); }
static inline void reg_write32(uintptr_t a, uint32_t v, int line) { mock_reg_write(a, v, line); }
#endif
#define REG_READ(a)      reg_read32((uintptr_t)(a), __LINE__)
#define REG_WRITE(a, v)  reg_write32((uintptr_t)(a), (uint32_t)(v), __LINE__)
```

설계 결정 하나하나가 면접 꼬리질문이다.

| 결정 | 이유 |
|---|---|
| 주소를 `uintptr_t` 정수로 받음 | 데이터시트 숫자를 그대로 쓴다. 호스트에서 포인터를 **만들지도** 않으므로 "정수 → 포인터 변환은 구현 정의 (C11 6.3.2.3p5)" 문제가 타깃 쪽 한 줄에만 남는다 |
| `volatile` 은 타깃 분기에만 | 타깃: 접근이 관찰 가능한 부수 효과여야 한다 (5.1.2.3p2·p6, 6.7.3p7). 호스트: 각 접근이 전역 로그를 바꾸는 함수 호출이라 컴파일러가 지우거나 합칠 수 없다 |
| 몸통은 `static inline` 함수 | 인자 한 번 평가 (`REG_WRITE(p++, x)` 도 안전), 타입 검사, 디버거 브레이크포인트 |
| `__LINE__` 은 매크로에서 | 함수 안에서 쓰면 항상 hal_reg.h 의 줄이 찍힌다. 매크로 인자 위치에서 펼쳐져야 **드라이버의 줄** 이 된다 |
| `#error` 가드 | 호스트 빌드에 `-DREG_TARGET` 이 새면 컴파일 단계에서 막는다 (실행하면 SIGSEGV) |
| `REG_MODIFY` 도 계층 함수로 | mock 이 R 과 W 를 둘 다 본다. 원자적이지 않다는 점은 그대로 (C-M02, Q7) |

**`#error` 가드 실측** — 호스트에서 `-DREG_TARGET` 으로 빌드:

```text
solution.c:52:6: error: "REG_TARGET 은 32비트 ARM(MCU) 빌드 전용: 호스트에서 0x4000_0000 대를 역참조하면 SIGSEGV"
   52 | #    error "REG_TARGET 은 32비트 ARM(MCU) 빌드 전용: 호스트에서 0x4000_0000 대를 역참조하면 SIGSEGV"
      |      ^
1 error generated.
```

`__arm__` 은 32비트 ARM 타깃에서만 정의된다. 이 맥에서 확인: `--target=thumbv7em-none-eabi` 는 `__arm__ 1`, `__thumb__ 1`, `__ARM_ARCH_PROFILE 'M'` 을, 호스트 arm64 는 `__aarch64__ 1` 과 `__ARM_ARCH_PROFILE 'A'` 를 정의하고 `__arm__` 은 없다.

**타깃 비용 0 — `demo_target.c` 실측.** 같은 `spi_init` 레지스터 부분을 두 번 컴파일했다. Cortex-M4 (`-DREG_TARGET`):

```text
spi_init_regs:
	movw	r2, #14372          @ r2 = 0x4002_3824 (APB2RSTR)
	movt	r2, #16386
	ldr	r3, [r2, #32]       @ #0 R APB2ENR (0x3824 + 0x20 = 0x3844)
	orr	r3, r3, #4096       @     | SPI1EN
	str	r3, [r2, #32]       @ #1 W APB2ENR
	ldr	r3, [r2, #32]       @ #2 R APB2ENR (read-back)
	ldr	r3, [r2]            @ #3 R APB2RSTR
	orr	r3, r3, #4096
	str	r3, [r2]            @ #4 W APB2RSTR
	ldr	r3, [r2]            @ #5 R APB2RSTR
	bic	r3, r3, #4096
	str	r3, [r2]            @ #6 W APB2RSTR
	movw	r2, #12288          @ r2 = 0x4001_3000 (SPI1_CR1)
	movt	r2, #16385
	str	r0, [r2]            @ #7 W CR1 = cr1
	orr	r0, r0, #64         @     cr1 | SPE (계산만, 메모리 접근 아님)
	str	r1, [r2, #4]        @ #8 W CR2
	str	r0, [r2]            @ #9 W CR1 = cr1|SPE
	bx	lr
```

(`@` 뒤 설명 주석은 내가 붙였고, `.p2align` 같은 지시어 줄은 뺐다. 명령어는 clang 출력 그대로.) 메모리 명령 10개가 호스트 로그의 10줄과 **일대일, 같은 순서** 다. 함수 호출 없음, `line` 인자는 흔적도 없다. `dmb`/`dsb` 도 없다 — `volatile` 은 컴파일러에게 "지우지 말고 순서 바꾸지 말 것" 만 말하지 하드웨어 배리어를 만들지 않는다. Cortex-M4 의 주변장치 영역은 Device 메모리라 접근이 순서대로 버스에 나가지만, "쓰기가 주변장치에 **도착했음**" 이 필요하면 read-back 이나 `DSB` 가 필요하다 — #2 의 read-back 이 바로 그 용도다(아래 Part C). CPU/컴파일러 배리어의 구분은 C-A02.

호스트 (기본 빌드) 같은 함수 — 발췌 (`...` 은 생략한 부분):

```text
	mov	w21, #14372                     ; =0x3824
	movk	w21, #16386, lsl #16
	add	x0, x21, #32
	mov	w1, #37                         ; =0x25
	bl	_mock_reg_read
	mov	x8, x0
	add	x0, x21, #32
	orr	w1, w8, #0x1000
	mov	w2, #37                         ; =0x25
	bl	_mock_reg_write
	...
	mov	w2, #43                         ; =0x2b
	...
	b	_mock_reg_write
```

`__LINE__` 이 상수 인자(37 = `demo_target.c` 의 `REG_MODIFY(RCC_APB2ENR, ...)` 줄)로 들어가고, 모든 접근이 `bl _mock_reg_*` 로 바뀐다. 마지막 쓰기는 꼬리 호출(`b`).

**로그 + 스크립트.** 로그 한 줄 `{op, addr, val, line}` 에서 `val` 은 **CPU 가 쓴/받은 값** 이다. 모델이 쓰기를 무시하거나 MODF 로 비트를 지워 저장해도 로그에는 드라이버가 쓴 값이 남아야 진단이 쉽다 (저장된 값은 `mock_peek` 으로 따로 본다).

```c
static uint32_t mock_reg_read(uintptr_t a, int line) {
    const int i = reg_index(a);
    if (i < 0) { g_viol |= V_UNKNOWN; log_push('R', a, 0u, line); return 0u; }
    for (int k = 0; k < g_nscript; k++)
        if (!g_script[k].used && g_script[k].addr == a) { g_script[k].used = true; g_val[i] = g_script[k].val; break; }
    const uint32_t v = model_read(i);
    log_push('R', a, v, line);
    return v;
}
```

스크립트는 시간을 흉내 낸다: "다음 SR 읽기에서는 BSY, 그다음도 BSY, 그다음은 TXE". 시간이 흐르지 않는 RAM 가짜에서는 BSY 를 1 로 두면 영원히 1 이라 폴링 루프를 테스트할 수 없다 (연습 4 에서 직접 확인).

**기대 시퀀스와 실패 메시지.** 비교는 `op`, `addr`, 그리고 `((got ^ want) & mask) == 0`. 세 가지 실패 모양:

```text
#2: expected W SPI1_CR1 0x0000031F, got W SPI1_CR1 0x0000035F (line ...)   ← 값이 다름
#3: expected R SPI1_SR any, got <end of log>                               ← 접근 누락
#2: unexpected extra W SPI1_CR1 0x0000035F (line ...)                      ← 접근이 더 있음
```

`demo_mismatch.c` 실측 — SPE 를 CR2 보다 먼저 켜는 init 을 본문 기대 표에 넣었다:

```text
[1] SPE before CR2: check=8
    #8: expected W SPI1_CR2 0x00000000, got W SPI1_CR1 0x0000035F (line 16)
    final CR1=0x35F CR2=0x000 viol=0x00
```

"8번째 접근, 기대는 CR2 쓰기인데 CR1 에 0x35F 를 썼다, `demo_mismatch.c` 16번째 줄" — 테스트 실패 한 줄로 고칠 곳까지 간다. 그리고 셋째 줄이 중요하다: **최종 상태는 정답과 같고 규칙 모델도 조용하다** (viol=0). 이 버그를 잡는 건 정확한 시퀀스 비교뿐이다. (CR2 에 ERRIE 를 쓰는 판이면 "에러 인터럽트를 켜기 전에 SPE → enable 순간의 MODF 를 놓침" 이라는 실제 결과가 있다 — 연습 2.)

### Part B — 규칙 모델: mock 이 "일반 메모리" 면 놓치는 것

```c
static void model_write(int i, uint32_t v) {
    const uintptr_t a = k_regs[i].addr;
    if (k_regs[i].spi && !spi_clk_on()) { g_viol |= V_CLK_OFF; return; }
    if (a == RCC_APB2RSTR) {
        g_val[i] = v;
        if (v & RCC_APB2_SPI1)
            for (int j = 0; j < NREGS; j++)
                if (k_regs[j].spi) g_val[j] = k_regs[j].reset;
    } else if (a == SPI1_SR) {
        g_viol |= V_RO_WRITE;
    } else if (a == SPI1_CR1) {
        const uint32_t old = g_val[i];
        if ((old & SPI_CR1_SPE) && ((old ^ v) & CR1_LOCKED)) g_viol |= V_CFG_WHILE_ON;
        if ((v & (SPI_CR1_SPE | SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI)) == (SPI_CR1_SPE | SPI_CR1_MSTR | SPI_CR1_SSM)) {
            g_viol |= V_MODF;
            g_val[reg_index(SPI1_SR)] |= SPI_SR_MODF;
            v &= ~(SPI_CR1_SPE | SPI_CR1_MSTR);
        }
        g_val[i] = v;
    } else {
        g_val[i] = v;
    }
}
```

`demo_mismatch.c` 의 나머지 실측 — 버그 드라이버가 남긴 로그 (줄 번호는 `solution.c` 의 버그 함수):

```text
-- [2] bug_init_wrong_bus (4 accesses, viol=0x03)
  #0  R 0x40023840    0x00000000  line 304
  #1  W 0x40023840    0x00004000  line 304
  #2  W SPI1_CR1      0x0000031C  line 305
  #3  W SPI1_CR1      0x0000035C  line 306
-- [3] bug_init_no_ssi (4 accesses, viol=0x10)
  #0  R RCC_APB2ENR   0x00000000  line 309
  #1  W RCC_APB2ENR   0x00001000  line 309
  #2  W SPI1_CR1      0x0000021C  line 310
  #3  W SPI1_CR1      0x0000025C  line 311
    after: CR1=0x218 SR=0x22 (MODF=1)
```

| 버그 드라이버 | 실제 보드에서의 증상 | mock 이 보는 것 |
|---|---|---|
| `bug_init_wrong_bus` — SPI1 클록을 `APB1ENR` b14 (SPI2EN) 로 켬 | SPI1 은 꺼진 채, CR1 쓰기가 사라짐. SCK 가 안 나옴. 스코프 들고 반나절 | `viol=0x03` = `V_UNKNOWN` (맵에 없는 0x4002_3840) + `V_CLK_OFF`, `CR1` 저장값 0 |
| `bug_init_no_ssi` — SSM=1, SSI=0 | enable 하자마자 MODF, MSTR 이 꺼져 **슬레이브가 됨**. 전송 함수는 TXE 만 보고 "보냈다" 고 믿음 | `viol=0x10` (MODF). 저장된 CR1 = 0x25C & ~(SPE\|MSTR) = **0x218**, SR = TXE\|MODF = 0x22 |
| `bug_set_speed_hot` — SPE=1 인 채 BR 변경 | 전송 중이면 프레임 하나가 섞인 클록으로 나감. 간헐적 | `V_CFG_WHILE_ON` |
| `bug_clear_flags` — `SR = 0` 으로 "플래그 지우기" | 아무 일도 안 일어남 (OVR/MODF 는 쓰기로 안 지워짐) → 에러 처리 루프가 무한 반복 | `V_RO_WRITE`, SR 값 그대로 |

모델에 넣은 각 규칙의 근거를 코드 주석에 **데이터시트 문장 단위로** 적어 두는 것이 실무 포인트다. mock 은 "데이터시트를 실행 가능하게 옮긴 것" 이라서, 규칙이 틀리면 테스트가 거짓 PASS 를 낸다 (Q4). 이 레슨 모델의 단순화도 적어 둔다:

- MODF 는 "SPE·MSTR·SSM=1, SSI=0 인 값이 써지는 순간" 으로 단순화했다. 실제로는 마스터 모드에서 NSS 입력(SSM=1 이면 SSI 값)이 low 가 되는 사건이다. 실제 MODF 클리어는 "MODF 가 선 상태에서 SR 읽기/쓰기 → CR1 쓰기" 시퀀스다(RM0090) — 모델에는 넣지 않았다.
- 클록 꺼진 주변장치 접근의 결과는 칩마다 다르다. STM32 는 대체로 버스 폴트 없이 쓰기가 무시되고 0 이 읽히지만, NXP Kinetis 처럼 클록 게이트가 닫힌 주변장치 접근이 **HardFault** 인 MCU 도 있다. 모델은 쓰는 칩에 맞춘다.
- "SPE=1 인 채 설정 변경" 은 RM0090 이 BR/CPOL/CPHA 에 "통신 중 바꾸지 말 것", DFF 에 "SPE=0 일 때만" 이라고 쓴 것을 보수적으로 "SPE=1 이면 금지" 로 굳힌 **프로젝트 규칙** 이다.

### Part C — 드라이버와 기대 시퀀스

**설정값 계산** (cfg a: 84 MHz APB2, 슬레이브 최대 10 MHz, mode 3, MSB first, 8비트, 소프트웨어 NSS):

| 필드 | 근거 | CR1 비트 |
|---|---|---|
| MSTR | 마스터 | 0x004 |
| BR = 3 | 84/2=42, /4=21, /8=10.5 > 10, **/16 = 5.25 ≤ 10** | 3 << 3 = 0x018 |
| mode 3 | CPOL=1, CPHA=1 → `cr1 \|= mode` | 0x003 |
| SSM, SSI | 소프트웨어 NSS: 내부 NSS 를 high 로 고정 | 0x300 |
| 합 (SPE=0) / SPE 포함 | | **0x31F / 0x35F** |

`spi_pick_br` 는 `((uint64_t)max_hz << (br + 1)) >= pclk_hz` 로 비교한다. 나눗셈 `pclk >> (br+1) <= max` 는 내림 때문에 나누어떨어지지 않을 때 **허용보다 빠른** 클록을 고를 수 있고(pclk=21, max=10 → 21>>1=10 통과, 실제 10.5), `max_hz << 8` 은 32비트에서 넘칠 수 있다. 테스트: `pick_br(84M, 10.5M) == 2`, `pick_br(84M, 10499999) == 3`, `pick_br(84M, 328125) == 7` (=84M/256 정확히).

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-size="13" font-weight="bold">spi_init(cfg a) 의 기대 시퀀스 10개 — 묶음마다 이유가 있다</text>
  <g fill="currentColor" font-size="13">
    <text x="20" y="48">① 클록 + read-back</text>
    <text x="233" y="48">② 리셋 펄스</text>
    <text x="517" y="48">③ 설정</text>
    <text x="659" y="48">④ 켜기</text>
  </g>
  <path d="M20 54 L229 54" stroke="currentColor"/>
  <path d="M233 54 L513 54" stroke="currentColor"/>
  <path d="M517 54 L655 54" stroke="currentColor"/>
  <path d="M659 54 L726 54" stroke="currentColor"/>
  <g stroke="currentColor">
    <rect x="20" y="62" width="66" height="44" class="f-mut"/>
    <rect x="91" y="62" width="66" height="44" class="f-mut"/>
    <rect x="162" y="62" width="66" height="44" class="f-mut"/>
    <rect x="233" y="62" width="66" height="44" class="f-mut"/>
    <rect x="304" y="62" width="66" height="44" class="f-mut"/>
    <rect x="375" y="62" width="66" height="44" class="f-mut"/>
    <rect x="446" y="62" width="66" height="44" class="f-mut"/>
    <rect x="517" y="62" width="66" height="44" class="f-acc"/>
    <rect x="588" y="62" width="66" height="44" class="f-acc"/>
    <rect x="659" y="62" width="66" height="44" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="26" y="80">#0 R ENR</text><text x="97" y="80">#1 W ENR</text><text x="168" y="80">#2 R ENR</text>
    <text x="239" y="80">#3 R RST</text><text x="310" y="80">#4 W RST</text><text x="381" y="80">#5 R RST</text>
    <text x="452" y="80">#6 W RST</text><text x="523" y="80">#7 W CR1</text><text x="594" y="80">#8 W CR2</text>
    <text x="665" y="80">#9 W CR1</text>
    <text x="97" y="98">0x1010</text><text x="168" y="98">지연</text>
    <text x="310" y="98">0x1000</text><text x="452" y="98">0x0000</text>
    <text x="523" y="98">0x31F</text><text x="594" y="98">0x000</text><text x="665" y="98">0x35F</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="136">① 클록 전에 SPI1 을 만지면 쓰기 무시·읽기 0 (V_CLK_OFF). RMW 라 USART1EN(b4) 은 보존 → 0x1010</text>
    <text x="10" y="158">   read-back #2: 클록 enable 이 버스 브리지를 지나 실제로 반영될 시간을 번다 (ST errata / HAL 매크로)</text>
    <text x="10" y="184">② 부트로더가 SPI1 을 SPE=1, BR=7 로 남겼어도 리셋 후 0 → ③ 에서 "켜진 채 변경" 이 생기지 않는다</text>
    <text x="10" y="210">③ BR/CPOL/CPHA/DFF 는 SPE=0 에서. SSM=1 이면 SSI=1 도 함께 (없으면 ④ 에서 MODF)</text>
    <text x="10" y="236">④ SPE 는 마지막 한 번. 순서가 바뀌면 mock_check_seq 가 #8 에서 실패 (demo_mismatch [1])</text>
    <text x="10" y="260" font-family="monospace">ENR = RCC_APB2ENR, RST = RCC_APB2RSTR</text>
  </g>
</svg>
```

```c
static int spi_init(const spi_cfg_t *c) {
    if (c == NULL || c->mode > 3u) return -1;
    const int br = spi_pick_br(c->pclk_hz, c->max_hz);
    if (br < 0) return -1;                       /* ← 여기까지 버스 접근 0 */
    uint32_t cr1 = SPI_CR1_MSTR | ((uint32_t)br << SPI_CR1_BR_Pos) | (uint32_t)c->mode;
    uint32_t cr2 = 0u;
    if (c->lsb_first) cr1 |= SPI_CR1_LSBFIRST;
    if (c->frame16)   cr1 |= SPI_CR1_DFF;
    if (c->hw_nss)    cr2 |= SPI_CR2_SSOE;
    else              cr1 |= SPI_CR1_SSM | SPI_CR1_SSI;
    REG_MODIFY(RCC_APB2ENR, 0u, RCC_APB2_SPI1);
    (void)REG_READ(RCC_APB2ENR);
    REG_MODIFY(RCC_APB2RSTR, 0u, RCC_APB2_SPI1);
    REG_MODIFY(RCC_APB2RSTR, RCC_APB2_SPI1, 0u);
    REG_WRITE(SPI1_CR1, cr1);
    REG_WRITE(SPI1_CR2, cr2);
    REG_WRITE(SPI1_CR1, cr1 | SPI_CR1_SPE);
    return 0;
}
```

**read-back 의 근거.** STM32 HAL 의 `__HAL_RCC_SPI1_CLK_ENABLE()` 은 `SET_BIT(RCC->APB2ENR, ...)` 다음에 `tmpreg = READ_BIT(RCC->APB2ENR, ...)` 를 하고 주석에 "Delay after an RCC peripheral clock enabling" 이라고 적는다. ST errata sheet 의 같은 제목 항목이 근거다: 클록 enable 직후 몇 사이클 안에 그 주변장치에 접근하면 반영이 안 될 수 있다. 호스트 테스트 관점에서는 "이 read-back 이 빠지면 기대 시퀀스 #2 에서 실패" — **errata 대응 코드가 리팩터링 중 지워지는 것을 테스트가 막아 준다.** 그런 코드는 보통 "의미 없어 보이는 읽기" 라서 제일 먼저 지워진다.

**solution 실행의 마지막 출력** (실측, `mock_dump`):

```text
-- spi_init(84 MHz, <=10 MHz, mode 3, SW NSS) (10 accesses, viol=0x00)
  #0  R RCC_APB2ENR   0x00000010  line 271
  #1  W RCC_APB2ENR   0x00001010  line 271
  #2  R RCC_APB2ENR   0x00001010  line 272
  #3  R RCC_APB2RSTR  0x00000000  line 273
  #4  W RCC_APB2RSTR  0x00001000  line 273
  #5  R RCC_APB2RSTR  0x00001000  line 274
  #6  W RCC_APB2RSTR  0x00000000  line 274
  #7  W SPI1_CR1      0x0000031F  line 275
  #8  W SPI1_CR2      0x00000000  line 276
  #9  W SPI1_CR1      0x0000035F  line 277
53/53 passed
```

이 덤프를 그대로 기대 표로 복사하고 싶은 유혹이 있다 — **하면 안 된다.** 그러면 테스트는 "현재 동작" 을 정답으로 굳힐 뿐이다(golden master). 기대 표는 데이터시트와 위 계산표에서 **손으로** 만들고, 덤프는 실패했을 때 차이를 읽는 데만 쓴다.

**동작 중 속도 변경** — `spi_set_speed(84 MHz, 21 MHz, 5)`, SR 스크립트 `0x82, 0x82, 0x02`:

| # | 접근 | 값 | 이유 |
|---|---|---|---|
| 0 | R CR1 | 0x35F | 현재 BR 과 SPE 확인. BR 이 이미 1 이면 여기서 끝 (테스트: 접근 1회) |
| 1–3 | R SR ×3 | 0x82, 0x82, 0x02 | `TXE=1 && BSY=0` 대기. 상한 `max_polls` — 넘으면 CR1 을 **안 쓰고** -1 |
| 4 | W CR1 | 0x31F | SPE 만 끈다 (`CR1_LOCKED` 변경 없음 → 위반 아님) |
| 5 | W CR1 | 0x30F | SPE=0 에서 BR 3 → 1 |
| 6 | W CR1 | 0x34F | 다시 켠다 |

BR=1 은 84/4 = 21 MHz. SPE 가 처음부터 꺼져 있으면 기다릴 이유가 없으므로 BR 만 바꿔 한 번 쓴다(테스트: SR 읽기 0회, CR1 = 0x307). RM0090 의 정식 비활성화 절차는 수신 데이터까지 고려해 "RXNE 로 마지막 데이터 받기 → TXE=1 → BSY=0 → SPE=0" 이고, 이 레슨은 송신 전용 마스터로 단순화했다.

**두 번째 설정** (cfg b: 16 MHz → 1 MHz, mode 0, LSB first, 16비트, 하드웨어 NSS): BR = 3 (16/16), CR1 = MSTR 0x004 + 0x018 + LSBFIRST 0x080 + DFF 0x800 = 0x89C, SPE 포함 **0x8DC**, CR2 = SSOE 0x004. 하드웨어 NSS 라 SSM/SSI 가 없고 MODF 규칙에 걸리지 않는다. 이 테스트는 시퀀스 대신 최종 값과 "마지막 접근이 SPE 를 켜는 CR1 쓰기" 만 본다 — 같은 순서를 표로 두 번 쓰는 대신 **값이 다른 경로만** 확인하는 것이다.

### 검사 수준 고르기 — "그 테스트 너무 brittle 하지 않나요?"

| 수준 | 비교하는 것 | 잡는 것 | 놓치는 것 / 비용 | 이 레슨 |
|---|---|---|---|---|
| 정확한 시퀀스 | R/W·주소·값·순서 전부 | 순서, 누락, 중복, read-back 삭제 | 동작이 같은 리팩터링(읽기 하나 추가)에도 깨짐 | Part C init, set_speed |
| 쓰기만 | W 의 순서와 값 | 쓰기 순서·값 | 읽기 부작용 (OVR 클리어, errata read-back) | 연습 1 |
| 규칙 모델 | 하드웨어 규칙 위반 비트 | 클록, MODF, 켜진 채 변경, RO 쓰기, 미지 주소 | 규칙으로 안 적은 버그 (demo [1]: viol=0) | Part B |
| 최종 상태 | 끝난 뒤 레지스터 값 | 값 오류 | 순서 버그 전부 (연습 2) | `mock_peek` |

실무 조합: **규칙 모델은 모든 테스트에서 항상 켜 두고**(`g_viol == 0` 을 기본 검사로), 정확한 시퀀스는 데이터시트가 순서를 규정한 짧은 구간(init, 저전력 진입, 플래시 unlock)에만, 나머지는 최종 상태로. 그러면 리팩터링 때 깨지는 테스트는 "순서가 정말 중요한" 테스트뿐이고, 그건 깨져야 맞다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    # 53/53 passed (위 trace 포함)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 53/53, 보고 없음
```

ASan/UBSan 은 Part A 의 "로그 넘침" 테스트(쓰기 40번, `LOG_MAX` 32)에서 `log_push` 가 배열 밖에 쓰지 않음을 함께 확인한다. 경계 검사 한 줄을 지운 사본(작업용 임시 파일, 레슨에는 없음)으로 돌린 실측:

```text
sol_nobound.c:145:5: runtime error: index 32 out of bounds for type 'reg_access_t[32]'
==87734==ERROR: AddressSanitizer: global-buffer-overflow on address 0x00010088fa40 at pc 0x000100883afc bp 0x00016f5824b0 sp 0x00016f5824a8
```

UBSan 이 먼저 배열 인덱스 32 를, 이어서 ASan 이 그 쓰기를 잡는다. 새니타이저 없이 돌리면 아무 보고 없이 배열 뒤의 전역 변수를 덮어쓸 수 있어, 증상이 엉뚱한 곳에서 나타난다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `#define REG_MODIFY(a,c,s) REG_WRITE(a, (REG_READ(a) & ~(c)) \| (s))` | `a` 를 두 번 평가 (`REG_MODIFY(p++, ...)` 사고, C-B15). 줄 번호·타입 검사도 흐려짐 | `static inline reg_modify32(...)` + 얇은 매크로 |
| 드라이버 한구석에서 `*(volatile uint32_t *)SPI1_SR` 직접 접근 | mock 을 우회 → 호스트에서 SIGSEGV, 또는 그 접근만 테스트에서 안 보임 | 모든 접근은 `REG_*`. 리뷰/grep 규칙 |
| 호스트 테스트 빌드에 `-DREG_TARGET` 이 섞임 | 0x4001_3000 역참조 → 실행 즉시 죽음 | `#if defined(REG_TARGET) && !defined(__arm__)` → `#error` |
| mock 이 일반 메모리처럼만 동작 | 클록 꺼짐, SSI 누락(MODF), SR 쓰기, 켜진 채 변경이 전부 PASS | 데이터시트 규칙을 모델에 (Part B), 모든 테스트에서 `g_viol == 0` |
| 로그에 "모델이 저장한 값" 을 기록 | MODF 로 지워진 0x218 이 찍혀 "드라이버가 0x218 을 썼다" 로 오해 | 로그 = CPU 가 쓴 값, 저장값은 `mock_peek` |
| 실패 경로에서 레지스터를 반쯤 쓰고 -1 | 호출자는 "실패 = 변화 없음" 을 기대. 반쯤 설정된 주변장치가 남음 | 검증을 첫 접근 앞에, 테스트는 `g_nlog == 0` |
| 폴링 루프에 상한 없음 | 실제 보드에선 하드웨어 고장 시 영원히 멈춤, mock 에선 테스트 자체가 멈춤 | `for (n = 0; n < max_polls && !ready; n++)` + 시간 초과 테스트 |
| `uint32_t sr = REG_READ(SR); while (sr & BSY) {}` | 한 번만 읽고 캐시. 타깃·호스트 둘 다 무한 루프 | 루프 안에서 매번 `REG_READ`. 테스트는 `mock_count('R', SR)` |
| 덤프 출력을 그대로 기대 표로 복사 | 현재 버그를 정답으로 굳힘 | 기대 표는 데이터시트에서 손으로 |
| 로그 배열 경계 검사 없음 | 긴 테스트에서 배열 밖 쓰기 — 옆 전역을 덮어쓸 수 있어 증상이 엉뚱한 곳에 | `V_LOG_FULL` 세우고 버림. ASan 으로 확인 |

## 8. 면접 Q&A

<details>
<summary>Q1. REG_WRITE 를 그냥 매크로 한 줄로 하지 왜 inline 함수를 거치나요? 타깃에서 느려지지 않나요?</summary>

매크로는 캐스트와 `__LINE__` 만 맡고 몸통은 `static inline` 이다. 이유: 인자가 정확히 한 번 평가되고(C-B15), 주소·값 타입이 함수 시그니처로 검사되고, 호스트에서 브레이크포인트를 걸 수 있다. 타깃 비용은 0 이다 — `-O2` Cortex-M4 실측에서 `spi_init_regs` 는 `ldr`/`str` 10개와 `orr`/`bic` 뿐이고 `bl` 이 하나도 없다. `line` 인자는 타깃 분기에서 `(void)line` 이라 사라진다. 꼬리: "`-O0` 디버그 빌드에선?" → inline 이 안 될 수 있어 호출이 생긴다. 타이밍에 민감한 비트뱅잉이 디버그 빌드에서 깨진다면 `__attribute__((always_inline))` (GCC/Clang 확장)을 쓰거나 그 부분만 최적화 빌드로.

</details>

<details>
<summary>Q2. 컴파일 타임 스위치, link seam, 함수 포인터 ops — 언제 무엇을 쓰나요?</summary>

**컴파일 타임 스위치** (이 레슨): 레지스터 수준처럼 자주 불리고 타깃 비용 0 이 필요한 곳. 단점: 같은 바이너리로 둘 다 할 수 없고, 빌드 구성이 둘. **link seam**: `reg_write32()` 를 외부 함수로 두고 테스트 빌드에서 `reg_mock.c` 를 링크. 소스에 `#if` 가 없지만 타깃에서 함수 호출이 남는다 (LTO 가 없으면). **함수 포인터 ops**: `spi_init(const bus_ops_t *ops, ...)` 처럼 주입 — 런타임에 바꿀 수 있어 "같은 드라이버가 SPI1 직접 접근 / I2C 브리지 칩 경유" 처럼 **제품 기능** 으로 필요할 때 맞다. 간접 호출 비용과 API 침투가 있다. 경험칙: 레지스터 계층은 컴파일 타임, 버스(SPI/I2C 트랜잭션) 계층은 ops, 서드파티 라이브러리 경계는 link seam. 세 방식을 같은 드라이버로 비교하는 실습은 TEST-B04.

</details>

<details>
<summary>Q3. 레지스터 접근 순서를 정확히 비교하는 테스트는 구현을 조금만 바꿔도 깨지는 change detector 아닌가요?</summary>

맞는 지적이고, 그래서 수준을 섞는다. 데이터시트가 **순서 자체를 요구하는** 구간(클록 → 리셋 → 설정 → enable, 플래시 unlock 키 시퀀스, 저전력 진입)은 정확한 시퀀스가 곧 스펙이라 깨져야 맞다. 그 밖에는 ① 쓰기만 비교(읽기 횟수는 구현 세부, 연습 1), ② 마스크로 관심 비트만(`EXP_WM`), ③ 규칙 모델 위반 0, ④ 최종 상태. 그리고 규칙 모델은 항상 켠다. demo_mismatch [1] 이 보여 주듯 "SPE 를 CR2 보다 먼저" 는 최종 상태와 규칙 모두 통과하고 정확한 시퀀스만 잡는다 — 무엇을 포기하는지 알고 고르는 게 핵심이다.

</details>

<details>
<summary>Q4. mock 이 실제 하드웨어와 다르게 동작하면 테스트가 거짓 PASS 를 내지 않나요? mock 은 어떻게 검증하죠?</summary>

그렇다 — mock 은 데이터시트를 내가 읽은 대로 옮긴 것이라 내 오해까지 옮긴다. 대책: ① 규칙마다 데이터시트/errata 문장을 주석으로 달아 리뷰 가능하게, ② **같은 기대 표를 실제 보드에서도** 돌린다: 타깃용 세 번째 빌드 옵션(예: `REG_TRACE`)에서 `reg_write32` 가 진짜 `str` 후 RAM 링 버퍼(또는 ITM/SWO)에 `{addr, val}` 을 남기게 하고, HIL 테스트가 그 trace 를 같은 `mock_check_seq` 로 비교, ③ bring-up 때 로직 분석기로 SCK/NSS 를 보고 레지스터 dump 와 대조, ④ 칩이 바뀌면(F4 → H7: SPI 블록 자체가 다름) 모델도 새로 쓴다. 꼬리: "Renode/QEMU 같은 시뮬레이터는?" → 주변장치 모델 품질에 같은 문제가 있다. 레지스터 계층 mock 은 빠르고 결정적이라 매 커밋, 시뮬레이터/HIL 은 야간.

</details>

<details>
<summary>Q5. 코드베이스가 CMSIS 식 `SPI1->CR1 = x` 로 되어 있으면 이 방식을 어떻게 적용하나요?</summary>

C 에는 구조체 멤버 접근을 가로챌 방법이 없다. 선택지: ① 테스트 빌드에서 `#define SPI1 (&ram_spi1)` 로 RAM 가짜 — 고칠 게 없지만 최종 값만 보이고, 순서·읽기 부작용·시간(BSY 해제)을 못 본다 (연습 4 에서 두 한계를 테스트로 확인). ② 드라이버를 `REG_WRITE(&SPI1->CR1, x)` 로 바꿈 — 이 레슨의 계층을 그대로 쓴다. 다만 호스트에서 `SPI1` 이 정수 주소를 가리키는 포인터면 `&SPI1->CR1` 을 계산하는 것 자체가 유효하지 않은 포인터 연산이라, 호스트에선 `SPI1` 을 RAM 구조체로 두고 mock 은 "RAM 주소 - RAM 베이스 + 실제 베이스" 로 이름을 붙이는 식으로 맞춘다. ③ 레거시는 ①, 새 드라이버와 순서가 중요한 경로는 ② 로 점진 이전. 레지스터 표 자체는 X-macro 로 한 곳에서 생성하면 mock 의 `k_regs[]` 와 헤더가 어긋나지 않는다 (C-A07).

</details>

<details>
<summary>Q6. 읽기에 부작용이 있는 레지스터(읽으면 지워지는 플래그, FIFO DR)는 계층과 mock 에서 무엇을 조심해야 하나요?</summary>

계층은 읽기를 **절대 캐시하거나 생략하면 안 된다.** 타깃에서 `(void)REG_READ(SPI1_DR);` 는 값을 버려도 volatile 접근이라 부수 효과로서 반드시 수행된다 (5.1.2.3p2, 6.7.3p7) — 이 줄이 OVR 클리어의 절반이다 (RM0090: DR 읽기 후 SR 읽기). 호스트에선 외부에서 보이는 로그를 바꾸는 함수 호출이라 역시 지워지지 않는다. mock 은 그 부작용을 모델링해야 한다: 연습 3 의 모델은 "직전 접근이 DR 읽기였고 지금 SR 을 읽으면 OVR 을 지운다" 이고, 순서를 바꾼 버전은 OVR 이 남는 것을 테스트가 확인한다. 꼬리: "디버거 메모리 창이 DR 을 읽으면?" → 실제 보드에서 플래그/FIFO 데이터가 사라진다. 디버거가 주변장치 영역을 자동 갱신하지 않게 설정해야 하는 이유.

</details>

<details>
<summary>Q7. REG_MODIFY 는 원자적인가요? ISR 이 같은 레지스터를 만지면 테스트로 잡을 수 있나요?</summary>

아니다. 읽기와 쓰기 사이에 ISR 이 같은 레지스터를 바꾸면 그 변경을 되돌린다 (C-M02 Part C, C-M14). 해결은 하드웨어 set/clear 레지스터(GPIO BSRR, RP2040 의 SET/CLR alias), Cortex-M3/M4 bit-banding(M7/M33 에는 없음), 또는 짧은 임계 구역. 테스트로 잡으려면 mock 에 **주입 훅** 을 둔다: "n 번째 접근 직후 이 함수를 호출" 로 ISR 핸들러를 R 과 W 사이에 결정적으로 끼워 넣으면 lost update 가 매번 재현된다 — C-M14 의 `rd32()` 주입과 같은 기법이다. 계층이 접근을 한 곳으로 모아 두었기 때문에 가능한 테스트다.

</details>

<details>
<summary>Q8. 호스트 경로에는 volatile 이 없는데 폴링 루프 `while (REG_READ(SR) & BSY)` 가 최적화로 사라지지 않나요?</summary>

사라지지 않는다. 호스트의 `REG_READ` 는 `mock_reg_read` 호출이고, 그 함수는 전역 로그와 스크립트 상태를 바꾼다. 같은 번역 단위에서 inline 되더라도 컴파일러는 그 부수 효과를 보존해야 하므로 루프의 매 반복마다 읽기가 일어난다. 오히려 위험한 건 드라이버가 **값을 한 번 읽고 캐시** 하는 실수인데, mock 에서는 이게 `max_polls` 시간 초과나 `mock_count('R', SR)` 불일치로 드러난다. 반대로 타깃 분기에서 `volatile` 을 빠뜨리면 C-B09 의 "루프가 사라짐" 이 생기는데 이건 호스트 테스트가 잡을 수 없다 — 타깃 분기는 짧게 두고 `demo_target.c` 처럼 asm 을 한 번 확인하거나 C-M01 식 `_Generic` 한정자 검사로 고정한다.

</details>

## 9. 추가 연습

### 연습 1 — 읽기는 무시하고 쓰기만 비교하기

드라이버가 SR 을 몇 번 폴링했는지는 하드웨어 타이밍에 따라 다르다. 로그의 `'W'` 항목만 순서대로 기대 표와 비교하는 `mock_check_writes(exp, n)` 을 작성하고, 폴링 횟수만 다른 두 실행(1번/4번)이 모두 통과하는 반면 정확한 시퀀스 비교는 깨짐을 보여라.

<details>
<summary>답 보기</summary>

로그를 돌며 `op != 'W'` 는 건너뛰고, 기대 인덱스 `e` 를 쓰기를 만날 때만 올린다. 기대보다 많이 썼으면 `n`, 덜 썼으면 `e`, 같으면 -1. 정확한 비교 `mock_check_seq(full)` 은 4번 폴링 실행에서 인덱스 1 (`R SR` 자리에 기대 `W CR1`)에서 실패한다. 대가: OVR 클리어처럼 **읽기가 의미인** 시퀀스는 이 매처로 검증할 수 없다 (연습 3). 그래서 "쓰기만" 은 기본값이 아니라 선택지다.

</details>

### 연습 2 — 최종 상태로는 못 잡는 순서 버그

`init_good` (CR1 → CR2=ERRIE → CR1|SPE) 과 `init_early` (CR1|SPE → CR2=ERRIE) 는 끝난 뒤 레지스터 값이 똑같다. 둘을 구분하는 규칙 검사 `spe_last_ok()` — "CR1 에 SPE=1 을 처음 쓴 뒤 CR2 쓰기가 없어야 한다" — 를 작성하라. 왜 이 순서가 실제로 문제인가?

<details>
<summary>답 보기</summary>

첫 `W CR1` 중 SPE 비트가 있는 인덱스 `on` 을 찾고(없으면 false), 그 뒤에 `W CR2` 가 있으면 false. 테스트는 최종 값 비교가 두 드라이버 모두 통과함을 먼저 CHECK 해서 **한계를 문서로 남기고**, `spe_last_ok` 만 `init_early` 를 잡는다. 실제 문제: SPE 를 켜는 순간 생기는 에러(예: NSS 설정 실수로 인한 MODF)는 ERRIE 가 꺼져 있으면 인터럽트가 안 난다. 에러가 플래그로만 남고, 아무도 SR 을 안 보면 버스는 조용히 멈춘다. 이런 "켜기 전에 다 준비" 규칙은 DMA 채널(EN 마지막), 타이머(CEN 마지막), UART(UE 마지막)에 똑같이 있다.

</details>

### 연습 3 — 읽기 순서가 곧 명령: OVR 클리어

RM0090: OVR 은 **DR 읽기 다음 SR 읽기** 로 지워진다. mock 에 이 규칙을 넣고(직전 접근이 DR 읽기면 SR 읽기 후 OVR 클리어, 쓰기가 끼면 무효), `spi_clear_ovr()` 를 작성해 기대 시퀀스와 SR 최종 값을 확인하라. 순서를 바꾼 버전은?

<details>
<summary>답 보기</summary>

`(void)REG_READ(SPI1_DR); (void)REG_READ(SPI1_SR);` 두 줄. 기대 표는 `{EXP_R(DR), EXP_R(SR)}` 이고, 로그의 SR 읽기 값에는 OVR 이 **보이고**(지우기 전 값을 돌려줌) 그 뒤 `mock_peek(SR) == TXE`. 순서를 바꾸면 OVR 이 그대로 남는다. 값을 버리는 읽기가 반드시 실행돼야 한다는 점이 계층 설계 요구사항이 된다 — 타깃은 volatile 이 보장(Q6), 호스트는 함수 호출. "읽기 전용 접근이니 최적화로 빼자" 는 리팩터링을 이 테스트가 막는다.

</details>

### 연습 4 — CMSIS 식 구조체 + RAM 가짜, 그리고 그 한계

`typedef struct { volatile uint32_t CR1, CR2, SR, DR; } SPI_TypeDef;` 를 두고 `REG_TARGET` 이면 `SPI1 = (SPI_TypeDef *)SPI1_BASE`, 아니면 정적 RAM 인스턴스로 정의하라. `spi_enable_struct` 를 테스트하고, RAM 가짜가 (a) 순서 버그 (b) BSY 해제를 테스트할 수 있는지 확인하라.

<details>
<summary>답 보기</summary>

오프셋은 `_Static_assert(offsetof(SPI_TypeDef, DR) == 0x0C)` 로 고정 (C-A08). (a) `spi_enable_struct_early` 도 같은 최종 RAM 값을 남겨 **구분 불가** — 테스트가 그 사실을 CHECK 로 기록한다. (b) SR 에 BSY 를 써 두면 시간이 흐르지 않으므로 `spi_wait_idle_struct(SPI1, 1000)` 은 항상 시간 초과(-1). 스크립트 읽기가 없으면 "BSY 두 번 후 해제" 를 표현할 수 없다. 주의: RAM 인스턴스도 `volatile` 멤버를 가지므로 `memset((void *)&ram, ...)` 으로 지우면 volatile 객체를 non-volatile lvalue 로 접근하는 것이라 UB 다 (C11 6.7.3p6) — 필드별로 대입한다. 결론: RAM 가짜는 값 위주의 단순 레지스터에 싸고 충분하고, 순서·시간·읽기 부작용이 있는 경로는 기록 mock 이 필요하다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 17/17 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 17/17, 보고 없음
```

## 10. 핵심 정리

- 레지스터 접근은 **한 곳**(`REG_READ/REG_WRITE/REG_MODIFY`)으로 모으고, 그 끝만 `-DREG_TARGET` 으로 바꾼다. 매크로는 캐스트와 `__LINE__`, 몸통은 `static inline`. 호스트에 `REG_TARGET` 이 새면 `#error`.
- 타깃 비용 0: Cortex-M4 `-O2` 에서 `spi_init` 은 `ldr`/`str` 10개, `bl` 0개 — 호스트 로그 10줄과 일대일, 같은 순서. `volatile` 은 배리어를 만들지 않는다 (주변장치 도착 확인은 read-back/DSB, C-A02).
- 호스트 mock = ① 로그 `{R/W, 주소, CPU 가 쓴/받은 값, 소스 줄}` ② 스크립트 읽기(시간 흉내) ③ 데이터시트 규칙 모델(클록, 리셋, RO, 켜진 채 변경, MODF). 주소는 키일 뿐 역참조하지 않는다.
- 실패 메시지는 "몇 번째 접근, 기대 vs 실제, 소스 줄" 까지 — 테스트 한 줄로 고칠 곳에 간다.
- 검사 수준은 섞는다: 규칙 위반 0 은 항상, 정확한 시퀀스는 데이터시트가 순서를 정한 구간만, 나머지는 쓰기만/최종 상태. 최종 상태만으로는 순서 버그를, 규칙만으로는 안 적은 버그를 놓친다.
- 드라이버 쪽 규칙: 검증은 첫 접근 앞에(실패 = 접근 0회), 폴링엔 상한, 읽기는 캐시하지 말 것, errata read-back 은 테스트로 지킨다.
- 기대 표는 데이터시트에서 손으로. 덤프를 복사하면 버그를 정답으로 굳힌다. mock 의 정직성은 같은 표를 HIL trace 로 재사용해 검증한다.

## 참고자료

- STMicroelectronics RM0090 (STM32F405/415, F407/417, F427/437, F429/439 Reference Manual) — RCC `APB2RSTR`/`APB2ENR`/`APB1ENR`, SPI 레지스터(`SPI_CR1` 비트 설명: BR/CPOL/CPHA "통신 중 변경 금지", DFF "SPE=0 일 때만"), Mode fault (MODF), Overrun 클리어 시퀀스, SPI 비활성화 절차 — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>
- ST errata sheet ES0182 (STM32F405/407/415/417) — "Delay after an RCC peripheral clock enabling" 항목, 그리고 STM32CubeF4 HAL `stm32f4xx_hal_rcc*.h` 의 `__HAL_RCC_SPI1_CLK_ENABLE()` read-back — <https://github.com/STMicroelectronics/stm32f4xx-hal-driver>
- C11 (N1570): 6.3.2.3p5 정수→포인터 변환(구현 정의), 5.1.2.3p2·p6 volatile 접근 = 부수 효과, 6.7.3p6 volatile 객체를 non-volatile lvalue 로 접근하면 UB, 6.7.3p7 volatile 접근의 의미 — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- James W. Grenning, *Test-Driven Development for Embedded C* (Pragmatic Bookshelf, 2011) — "Testing with mock IO / LED driver", 하드웨어 의존성 분리와 seam
- ThrowTheSwitch Unity / CMock — <https://www.throwtheswitch.org/>
- ARMv7-M Architecture Reference Manual (ARM DDI0403E) — A3.5 메모리 타입·속성과 순서 모델(Device), A3.7 메모리 접근 순서와 배리어 — <https://developer.arm.com/documentation/ddi0403/latest/>
- 관련 레슨: C-M01 (레지스터 구조체, 정수→포인터), C-M02 (RMW, W1C), C-M11 (매크로 vs inline), C-M14 (ISR 공유 데이터·결정적 주입), C-B09 (volatile), C-B15 (매크로 함정), C-A02 (배리어), C-A07 (X-macro 레지스터 표), C-A08 (정적 검사), TEST-B03 (TDD 드라이버), TEST-B04 (seam 세 가지 비교), TEST-B05 (CMock 식 mock 생성)
