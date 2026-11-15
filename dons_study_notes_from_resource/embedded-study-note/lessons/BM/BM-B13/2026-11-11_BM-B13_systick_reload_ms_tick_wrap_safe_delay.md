---
id: BM-B13
time: 60분
updated: 2026-11-11
---
# BM-B13 SysTick: reload 계산, ms tick, wrap-safe delay

## 학습 목표

- 코어 클럭과 원하는 tick 주파수로 SysTick reload 값(`N − 1`)을 정수 연산으로 구하고, 24비트 한계(`1..0xFFFFFF`), 나누어떨어지지 않을 때의 오차(ppm)까지 보고할 수 있다.
- SysTick 다운카운터(down-counter)의 동작 — 0 에서 RVR 재장전, 1→0 전이 때 COUNTFLAG·예외, CVR 쓰기 = 클리어, COUNTFLAG 는 읽으면 지워짐(rc_r) — 을 모델로 직접 구현하고, 그 위에서 "정확히 N 클럭마다 tick" 을 검증한다.
- `volatile uint32_t g_ticks` + `millis()` 를 만들고, `(int32_t)(now − deadline) >= 0` 이 왜 32비트 wrap(0xFFFFFFFF → 0)에 안전한지, 어디까지(거리 < 2^31) 안전한지 모듈러 산술로 설명한다.
- `delay_ms(1)` 이 0.x ms 만에 끝나는 tick 위상(phase) 문제를 재현하고 **+1 규칙** 으로 고친다 (STM32 HAL `HAL_Delay` 가 하는 일).

## 1. 문제

"SysTick 으로 1 ms tick 을 만들고 `delay_ms` 와 타임아웃을 짜 보세요" 는 펌웨어 면접의 단골 첫 문제이고, 보드 브링업(bring-up)에서 LED 깜빡이기 다음으로 처음 만드는 시간 기준(time base)이다. 여기서 틀리는 세 가지 — reload 를 `N` 으로 넣는 off-by-one, tick 카운터가 49.7일 만에 넘칠 때 터지는 `now >= deadline`, 1 ms 를 기다렸는데 0.1 ms 만에 돌아오는 `delay_ms(1)` — 는 모두 실제 제품 버그로 나온다. SSD 컨트롤러의 명령 타임아웃, RF 모듈의 AT 응답 대기 타임아웃이 전부 이 비교 한 줄 위에 서 있다.

**SysTick** 은 Cortex-M 코어 안에 들어 있는 24비트 다운카운터다 (ARMv7-M 은 필수, ARMv6-M(M0/M0+)은 구현 선택 — ARM ARM 확인 권장). 칩 벤더와 무관하게 주소가 같아서(System Control Space, `0xE000E010`) STM32 / nRF52 / NXP 어디서든 같은 코드가 돈다. 레지스터는 넷이다.

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">SysTick 레지스터 (ARMv7-M B3.3 — 모든 Cortex-M 공통 주소)</text>
  <text x="10" y="50" fill="currentColor" font-family="monospace">CSR  0xE000E010</text>
  <rect x="140" y="34" width="200" height="26" class="f-mut" stroke="currentColor"/>
  <text x="200" y="51" fill="currentColor">[31:17] 예약</text>
  <rect x="340" y="34" width="100" height="26" class="f-warn" stroke="currentColor"/>
  <text x="346" y="51" fill="currentColor">16 COUNTFLAG</text>
  <rect x="440" y="34" width="90" height="26" class="f-mut" stroke="currentColor"/>
  <text x="452" y="51" fill="currentColor">[15:3] 예약</text>
  <rect x="530" y="34" width="70" height="26" class="f-acc" stroke="currentColor"/>
  <text x="536" y="51" fill="currentColor">2 CLKSRC</text>
  <rect x="600" y="34" width="66" height="26" class="f-acc" stroke="currentColor"/>
  <text x="604" y="51" fill="currentColor">1 TICKINT</text>
  <rect x="666" y="34" width="64" height="26" class="f-acc" stroke="currentColor"/>
  <text x="670" y="51" fill="currentColor">0 ENABLE</text>
  <text x="340" y="76" fill="currentColor">rc_r: 1→0 전이 때 1, 읽으면 0</text>
  <text x="530" y="76" fill="currentColor">1 = 프로세서 클럭 / 0 = 외부 기준</text>
  <text x="10" y="112" fill="currentColor" font-family="monospace">RVR  0xE000E014</text>
  <rect x="140" y="96" width="140" height="26" class="f-mut" stroke="currentColor"/>
  <text x="170" y="113" fill="currentColor">[31:24] 예약</text>
  <rect x="280" y="96" width="450" height="26" class="f-acc" stroke="currentColor"/>
  <text x="380" y="113" fill="currentColor">[23:0] RELOAD = N − 1  (1 .. 0xFFFFFF)</text>
  <text x="10" y="158" fill="currentColor" font-family="monospace">CVR  0xE000E018</text>
  <rect x="140" y="142" width="140" height="26" class="f-mut" stroke="currentColor"/>
  <text x="170" y="159" fill="currentColor">[31:24] 예약</text>
  <rect x="280" y="142" width="450" height="26" class="f-ok" stroke="currentColor"/>
  <text x="330" y="159" fill="currentColor">[23:0] CURRENT — 아무 값이나 쓰면 0 + COUNTFLAG 클리어</text>
  <text x="10" y="204" fill="currentColor" font-family="monospace">CALIB 0xE000E01C</text>
  <rect x="140" y="188" width="70" height="26" class="f-mut" stroke="currentColor"/>
  <text x="146" y="205" fill="currentColor">31 NOREF</text>
  <rect x="210" y="188" width="70" height="26" class="f-mut" stroke="currentColor"/>
  <text x="218" y="205" fill="currentColor">30 SKEW</text>
  <rect x="280" y="188" width="450" height="26" class="f-mut" stroke="currentColor"/>
  <text x="380" y="205" fill="currentColor">[23:0] TENMS — 10 ms 용 reload (0 = 정보 없음), 읽기 전용</text>
  <text x="10" y="242" fill="currentColor">RVR·CVR 리셋값은 UNKNOWN → 초기화 순서: RVR 쓰기 → CVR 클리어 → CSR 켜기. 이 레슨의 mock 은 CALIB 를 모델링하지 않는다.</text>
</svg>
```

| 레지스터 | 접근 형식 | 이 레슨에서 쓰는 규칙 |
|---|---|---|
| CSR.ENABLE / TICKINT / CLKSOURCE | rw | 켜기, 0 도달 시 SysTick 예외(번호 15) 요청, 클럭 선택. CLKSOURCE=0 의 "외부 기준 클럭" 은 구현 정의 — STM32F4 는 HCLK/8 (RM0090 클럭 트리 기준, 확인 권장) |
| CSR.COUNTFLAG | r, **rc_r** | 마지막으로 읽은 뒤 카운터가 1→0 을 지났으면 1. CSR 을 읽으면 0. CVR 쓰기로도 0 |
| RVR.RELOAD | rw (24비트) | 0 에서 다음 클럭에 실리는 값. 주기 = RELOAD + 1 클럭 |
| CVR.CURRENT | rw (쓰기 = 클리어) | 지금 값. 쓰기는 값과 무관하게 0 으로, 예외는 안 일으킨다 |

카운터가 어떻게 도는지가 reload 공식 `N − 1` 의 이유다. 아래는 이 레슨의 테스트가 그대로 확인하는 RVR = 3 의 궤적이다 (CVR 을 클리어한 직후부터 9 클럭).

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">SysTick 다운카운터: RVR = 3 → 주기 4 클럭 (solution.c Part A2 의 기대값과 같다)</text>
  <line x1="80" y1="200" x2="680" y2="200" stroke="currentColor"/>
  <line x1="80" y1="60" x2="80" y2="200" stroke="currentColor"/>
  <text x="66" y="204" fill="currentColor" text-anchor="end">0</text>
  <text x="66" y="164" fill="currentColor" text-anchor="end">1</text>
  <text x="66" y="124" fill="currentColor" text-anchor="end">2</text>
  <text x="66" y="84" fill="currentColor" text-anchor="end">3</text>
  <text x="20" y="54" fill="currentColor">CVR</text>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="80,200 140,200 140,80 200,80 200,120 260,120 260,160 320,160 320,200 380,200 380,80 440,80 440,120 500,120 500,160 560,160 560,200 620,200 620,80 670,80"/>
  <text x="140" y="218" fill="currentColor" text-anchor="middle">1</text>
  <text x="200" y="218" fill="currentColor" text-anchor="middle">2</text>
  <text x="260" y="218" fill="currentColor" text-anchor="middle">3</text>
  <text x="320" y="218" fill="currentColor" text-anchor="middle">4</text>
  <text x="380" y="218" fill="currentColor" text-anchor="middle">5</text>
  <text x="440" y="218" fill="currentColor" text-anchor="middle">6</text>
  <text x="500" y="218" fill="currentColor" text-anchor="middle">7</text>
  <text x="560" y="218" fill="currentColor" text-anchor="middle">8</text>
  <text x="620" y="218" fill="currentColor" text-anchor="middle">9</text>
  <text x="690" y="204" fill="currentColor">클럭</text>
  <rect x="306" y="226" width="28" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="546" y="226" width="28" height="18" class="f-warn" stroke="currentColor"/>
  <text x="200" y="262" fill="currentColor">1→0 전이 (클럭 4, 8): COUNTFLAG = 1, TICKINT 면 SysTick_Handler()</text>
  <text x="100" y="38" fill="currentColor">클럭 0: CVR 쓰기 → 0. 클럭 1: RVR(3) 재장전 — 이벤트 없음</text>
  <text x="100" y="56" fill="currentColor">0 에서도 한 클럭 머문다 → 3, 2, 1, 0 = 주기 4 클럭 = RVR + 1</text>
</svg>
```

카운터는 RVR, RVR−1, …, 1, 0 을 지나 **다음 클럭에** 다시 RVR 을 싣는다. 0 도 한 클럭 동안 머무르므로 주기는 RVR + 1 클럭이다. 그래서 N 클럭마다 tick 을 원하면 `RVR = N − 1` (Cortex-M4 Generic User Guide 4.4.2 의 "100 클럭마다면 RELOAD = 99"). RVR = 0 은 1→0 전이가 없으니 tick 이 영원히 안 나온다.

이 레슨은 맥에서 돈다. 진짜 MMIO 대신 **BM-B02 mock 축약판** 을 쓴다: 드라이버는 `REG_READ/REG_WRITE` 로만 레지스터를 만지고, 호스트 빌드에서 이 매크로는 레지스터 표 + 읽기/쓰기 훅(hook) + 접근 로그로 간다. SysTick 모델은 그 위에 얹힌다: `mock_cycles(n)` 이 코어 클럭 n 개를 흘리고, 클럭마다 `systick_hw_clock()` 이 카운터를 한 칸 움직이며, 1→0 에서 여러분의 `SysTick_Handler()` 를 부른다. 폴링 루프 한 바퀴(`cpu_spin()`)는 4 클럭으로 친다 (타깃 asm 의 `ldr/subs/cmp/blo` 4 명령 — 5. 풀이).

### Part A — reload 계산, SysTick 모델, tick ISR

- `st_status_t systick_reload(uint32_t core_hz, uint32_t tick_hz, uint32_t *reload, int32_t *err_ppm)` — N = core_hz / tick_hz 를 가장 가까운 정수로, reload = N − 1. 인자 오류(`ST_ERR_ARG`), reload 가 `1..0xFFFFFF` 밖(`ST_ERR_RANGE`, `*reload` 무손상), 나누어떨어지면 `ST_OK`, 아니면 `ST_INEXACT` + 실제 tick 주파수의 오차 ppm.
- `void systick_hw_clock(void)` — 모델의 카운터 한 클럭 (위 그림 그대로).
- `st_status_t systick_init(uint32_t core_hz, uint32_t tick_hz)` — 실패하면 레지스터를 **하나도** 건드리지 않는다. 성공하면 RVR → CVR 클리어 → CSR = CLKSOURCE | TICKINT | ENABLE.
- `void SysTick_Handler(void)` (`g_ticks++`), `uint32_t millis(void)`.

### Part B — wrap-safe 시간 비교

- `bool time_after_eq(uint32_t now, uint32_t deadline)` = `(int32_t)(now − deadline) >= 0`.
- `bool timeout_expired(uint32_t start, uint32_t ms)` = `millis() − start >= ms` (경과 시간 형식).
- 테스트는 `g_ticks` 를 **0xFFFFFFF0** 에서 시작해 실행 도중 0 을 지나게 하고, 64비트 오라클(넘치지 않는 진짜 시각)과 20만 번 비교하며, 하네스가 준 naive `now >= deadline` 이 즉시 만료 / 영영 안 끝남 두 방향으로 틀리는 것과 2^31 한계를 확인한다.

### Part C — delay_ms 와 tick 위상

- `uint32_t delay_wait_ticks(uint32_t ms)` — 기다릴 tick 경계 수: 0 → 0, 그 밖에는 ms + 1, `UINT32_MAX` 는 넘치지 않게 그대로.
- `void delay_ms(uint32_t ms)` — 최소 ms, 최대 ms + 1 (+ 루프 한 바퀴). 하네스는 1 ms 주기(N = 1000 클럭) 안의 **모든 위상** 에서 `delay_ms(1)` 을 불러 걸린 클럭을 재고, 주어진 `delay_ms_naive` 가 1 ms 미만에 끝나는 위상을 찾아낸다.

## 2. Problem (English)

> **SysTick time base for a bare-metal Cortex-M.**
>
> All register access goes through `REG_READ(addr)` / `REG_WRITE(addr, val)`. SysTick: CSR 0xE000E010 (ENABLE bit0, TICKINT bit1, CLKSOURCE bit2, COUNTFLAG bit16 — read-to-clear), RVR 0xE000E014 (24-bit RELOAD), CVR 0xE000E018 (any write clears it to 0 and clears COUNTFLAG).
>
> 1. `st_status_t systick_reload(uint32_t core_hz, uint32_t tick_hz, uint32_t *reload, int32_t *err_ppm)`: round N = core_hz / tick_hz to nearest, reload = N − 1, valid range 1..0xFFFFFF. Return `ST_OK` / `ST_INEXACT` (with the tick-frequency error in ppm, rounded) / `ST_ERR_RANGE` / `ST_ERR_ARG`. Integer math only.
>    Examples: (168 MHz, 1 kHz) → OK, 167999 (0x2903F). (32768 Hz, 1 kHz) → INEXACT, 32, −7030 ppm. (168 MHz, 10 Hz) → ERR_RANGE. (16 777 216 Hz, 1 Hz) → OK, 0xFFFFFF.
> 2. Model the counter: `systick_hw_clock()` reloads from RVR when CVR is 0, otherwise decrements; on the 1→0 transition set COUNTFLAG and call `SysTick_Handler()` if TICKINT.
> 3. `systick_init(core_hz, tick_hz)` — touch no register on error. After init at 1 MHz / 1 kHz, the first tick arrives after exactly 1000 cycles and then every 1000 cycles.
> 4. `volatile uint32_t g_ticks`, `SysTick_Handler()`, `millis()`, `time_after_eq(now, deadline)` (correct across the 32-bit wrap for distances < 2^31), `timeout_expired(start, ms)`.
>    Example: `time_after_eq(0x00000005, 0xFFFFFFF0)` → true, `time_after_eq(0xFFFFFFF0, 0x00000010)` → false (naive `now >= deadline` gives the opposite for both).
> 5. `delay_ms(ms)` must wait **at least** `ms` milliseconds regardless of where in the tick period it is called, and at most ms + 1 (plus one poll iteration). `delay_ms(0)` returns immediately; `delay_ms(UINT32_MAX)` must not return early.
>
> Follow-ups: Why N − 1? Why must `g_ticks` be volatile, and is volatile enough? What breaks after 49.7 days? Why can `HAL_Delay(1)` take 2 ms? What if interrupts are disabled for 3 ms? How do you get microseconds out of SysTick?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "24/73 passed" (mock 자체 점검·naive 버그 재현 일부는 시작 코드로도 통과) — TODO 를 채워 73/73
./starter -v      # init 접근 로그, wrap 타임아웃, 위상 sweep 표 (정답 코드로 돌리면 5. 풀이의 표가 나온다)
```

시작 코드는 SysTick 모델(`systick_hw_clock`)도 비어 있으니 tick 이 안 센다. 그래도 멈추지 않는다: 하네스의 `cpu_spin()` 은 20만 바퀴(모델 시간 800 ms)를 넘으면 `longjmp` 로 측정 함수에 돌아와 "끝나지 않은 delay" 로 기록한다 — 폴링 루프가 있는 테스트는 언제나 상한이 필요하다.

타깃 코드 관찰용 (실행하지 않음):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -   # ISR, init, 비교, volatile 유무
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -S demo_asm.c -o -   # 폴링 루프가 4 명령이 되는 것
```

## 4. 힌트

<details>
<summary>힌트 1 — reload: 반올림 나눗셈과 범위 두 개</summary>

가장 가까운 정수 N = `(core + tick/2) / tick` 을 `uint64_t` 로 (`core + tick/2` 가 32비트를 넘을 수 있다). 범위는 N 기준으로 `2 ≤ N ≤ 2^24` — N = 1 이면 reload 0 (동작 안 함), N = 2^24 + 1 이면 reload 가 24비트를 넘는다. 오차: 실제 주파수 core/N 과 목표 tick 의 상대 오차 = (core − N·tick) / (N·tick). 분자에 1e6 을 곱하고 부호를 보고 ±분모/2 를 더해 나누면 반올림이 된다.

</details>

<details>
<summary>힌트 2 — 하드웨어 모델: "0 이면 재장전, 아니면 감소, 감소해서 0 이 됐으면 이벤트"</summary>

이벤트는 **감소** 로 0 이 될 때만이다. 재장전(0 → RVR)은 이벤트가 아니다. 그래서 CVR 쓰기(→ 0)는 tick 을 만들지 않고, RVR = 0 이면 0 → 0 재장전만 반복해 이벤트가 없다. `mock_peek/mock_poke` 는 CPU 접근이 아니라 하드웨어 내부 상태 변경이라 로그에 남지 않는다.

</details>

<details>
<summary>힌트 3 — (int32_t)(now − deadline): 원 위의 거리</summary>

32비트 tick 은 직선이 아니라 둘레 2^32 인 원이다. `now − deadline` (unsigned 뺄셈)은 deadline 에서 now 까지 시계 방향 거리 mod 2^32 이고, 이를 int32 로 보면 [−2^31, 2^31) 로 "가까운 쪽" 부호가 나온다. 양수/0 = 지났다, 음수 = 아직. 두 시각의 실제 거리가 2^31 미만이면 원 위의 가까운 쪽이 진짜 방향이다.

</details>

<details>
<summary>힌트 4 — delay_ms(1) 이 짧은 이유: 첫 경계는 언제 오나</summary>

`start = millis()` 를 읽은 순간은 tick 주기 안의 아무 위상이다. 첫 tick 경계는 0 ~ 1 ms 뒤 어디든 올 수 있다. "경계 ms 개" 를 기다리면 실제 시간은 (ms − 1, ms] ms. 최소 ms 를 보장하려면 경계를 하나 더 기다린다. `ms + 1` 이 넘치는 값 하나를 조심.

</details>

## 5. 풀이

### Part A — reload: `N − 1`, 24비트, 그리고 ppm

규칙: **주기 N 클럭 → RVR = N − 1, 1 ≤ RVR ≤ 0xFFFFFF**. 정수 연산만 쓴다 (부동소수점 비교로 "나누어떨어지나" 를 판단하면 16 MHz / 3 Hz 같은 경우 반올림 오차로 틀린다).

```c
uint64_t n = ((uint64_t)core_hz + tick_hz / 2u) / tick_hz;          /* 가장 가까운 N */
if (n < 2u || n - 1u > SYST_RVR_MAX) return ST_ERR_RANGE;           /* *reload 는 건드리지 않음 */
*reload = (uint32_t)(n - 1u);
int64_t num = (int64_t)core_hz - (int64_t)(n * tick_hz);            /* core − N·tick */
int64_t den = (int64_t)(n * tick_hz);
int64_t ppm = (num >= 0 ? num * 1000000 + den / 2 : num * 1000000 - den / 2) / den;
```

실제 계산값 (`solution.c` 의 `systick_reload` 를 그대로 불러 찍은 출력):

```text
 168000000 Hz   1000 Hz -> OK        reload   167999 (0x02903F) err +0 ppm
 180000000 Hz   1000 Hz -> OK        reload   179999 (0x02BF1F) err +0 ppm
  72000000 Hz   1000 Hz -> OK        reload    71999 (0x01193F) err +0 ppm
  21000000 Hz   1000 Hz -> OK        reload    20999 (0x005207) err +0 ppm
     32768 Hz   1000 Hz -> INEXACT   reload       32 (0x000020) err -7030 ppm
     32768 Hz   1024 Hz -> OK        reload       31 (0x00001F) err +0 ppm
  10000000 Hz   3000 Hz -> INEXACT   reload     3332 (0x000D04) err +100 ppm
  16000000 Hz      3 Hz -> INEXACT   reload  5333332 (0x516154) err +0 ppm
  14745600 Hz   1000 Hz -> INEXACT   reload    14745 (0x003999) err -27 ppm
  16000000 Hz      1 Hz -> OK        reload 15999999 (0xF423FF) err +0 ppm
 168000000 Hz     10 Hz -> ERR_RANGE
  21000000 Hz     10 Hz -> OK        reload  2099999 (0x200B1F) err +0 ppm
  16777216 Hz      1 Hz -> OK        reload 16777215 (0xFFFFFF) err +0 ppm
  16777217 Hz      1 Hz -> ERR_RANGE
      1000 Hz   1000 Hz -> ERR_RANGE
```

읽는 법:

- **24비트 한계 = 최대 주기 2^24 클럭.** 168 MHz 에서 2^24 / 168 MHz = **99.86 ms** 가 SysTick 한 바퀴의 최대치다. 10 Hz(100 ms) tick 은 못 만든다. 방법은 CLKSOURCE = 0 으로 느린 기준 클럭을 쓰는 것 — STM32F4 에선 HCLK/8 = 21 MHz 라 최대 798.9 ms, 10 Hz 는 reload 2 099 999 (표의 21 MHz 줄, HCLK/8 은 RM0090 기준 확인 권장). 또는 일반 타이머(BM-B14)를 쓴다.
- **INEXACT 인데 0 ppm** (16 MHz / 3 Hz): N = 5 333 333, 실제 주파수 3.000 000 19 Hz → 0.06 ppm 이라 반올림하면 0. "나누어떨어지나" 는 ppm 이 아니라 나머지로 판단해야 하는 이유다.
- **32.768 kHz 로 1 ms 는 정확히 안 된다** (−7030 ppm = 하루 10분 늦음). 1024 Hz tick(reload 31)은 정확 — 대신 ms 변환이 필요하다 (연습 4). 14.7456 MHz(UART 친화 크리스털, COMM-B03)는 −27 ppm.

`ST_ERR_RANGE` 에서 `*reload` 를 건드리지 않는 것, `systick_init` 이 실패할 때 레지스터를 하나도 안 쓰는 것은 같은 원칙이다: **실패한 설정 함수는 하드웨어를 반쯤 바꿔 놓지 않는다.** 테스트가 `g_nlog == 0` 으로 확인한다.

### Part A — 하드웨어 모델과 init 순서

```c
static void systick_hw_clock(void) {
    uint32_t csr = mock_peek(SYST_CSR);
    if (!(csr & SYST_CSR_ENABLE)) return;
    uint32_t cvr = mock_peek(SYST_CVR);
    if (cvr == 0u) { mock_poke(SYST_CVR, mock_peek(SYST_RVR)); return; }   /* 재장전: 이벤트 없음 */
    mock_poke(SYST_CVR, --cvr);
    if (cvr == 0u) {                                                       /* 1 → 0 전이 */
        mock_poke(SYST_CSR, csr | SYST_CSR_COUNTFLAG);
        if (csr & SYST_CSR_TICKINT) SysTick_Handler();
    }
}
```

COUNTFLAG 의 clear-on-read 와 CVR 쓰기 = 클리어는 하네스의 훅(`csr_on_read`, `cvr_on_write`)이 이미 구현한다 — "읽기에 부작용이 있는 레지스터" 를 mock 으로 표현하는 전형이다 (BM-B02). 테스트 A2 가 확인하는 것:

| 단계 | 기대 | 의미 |
|---|---|---|
| RVR=3, CVR 쓰기, CSR=ENABLE\|CLKSOURCE 후 9 클럭 | CVR = 3 2 1 0 3 2 1 0 3 | 위 그림 |
| COUNTFLAG 이 선 클럭 | 4, 8 번째 | 1→0 에서만 |
| 0 에 도달 후 CSR 두 번 읽기 | 1, 그다음 0 | rc_r |
| TICKINT 켜고 400 클럭 | g_ticks += 100 | 4 클럭마다 ISR |
| CVR = 0 인 상태에서 RVR=0, 100 클럭 | tick 0, CVR 0 | reload 0 은 "꺼짐" 과 같다 |
| CLKSOURCE=0, 320 클럭 | tick 10 | 모델의 외부 기준 = /8 → 8배 느림 |

`systick_init` 의 실제 접근 로그 (`./solution -v`, 1 MHz / 1 kHz):

```text
init log 0: W 0xE000E010 = 0x00000000
init log 1: W 0xE000E014 = 0x000003E7
init log 2: W 0xE000E018 = 0x00000000
init log 3: W 0xE000E010 = 0x00000007
```

순서의 이유:

1. **CSR = 0 (끄기)** — 이미 돌고 있는 SysTick 을 다른 주파수로 바꿀 때, RVR 을 바꾸는 도중 tick 이 섞이지 않게. 리셋 직후 첫 init 이면 생략해도 되지만 재진입 안전하게 쓴다. (CMSIS `SysTick_Config()` 는 이 단계 없이 LOAD → 우선순위 → VAL → CTRL 순서다.)
2. **RVR = N − 1.**
3. **CVR 클리어** — 리셋값이 UNKNOWN 이다. 안 지우면 첫 tick 이 쓰레기 값(최대 2^24 클럭 = 168 MHz 에서 99.9 ms)만큼 늦게 온다. 이 mock 은 리셋 때 CVR = 0x123456 을 넣어 둬서, 클리어를 빼먹으면 "999 클럭에 아직, 1000 클럭에 첫 tick" 테스트가 깨진다. 클리어 직후 다음 클럭에 RVR 이 실리므로 첫 tick 은 **정확히 N 클럭 뒤** 다.
4. **CSR = CLKSOURCE | TICKINT | ENABLE (= 7)** 마지막에 한 번에.

실제 RTOS 도 같다. FreeRTOS-Kernel V11.3.2 `portable/GCC/ARM_CM4F/port.c` 의 `vPortSetupTimerInterrupt()` (823–829행, 이 맥의 소스에서 확인):

```c
/* Stop and clear the SysTick. */
portNVIC_SYSTICK_CTRL_REG = 0UL;
portNVIC_SYSTICK_CURRENT_VALUE_REG = 0UL;
/* Configure SysTick to interrupt at the requested rate. */
portNVIC_SYSTICK_LOAD_REG = ( configSYSTICK_CLOCK_HZ / configTICK_RATE_HZ ) - 1UL;
portNVIC_SYSTICK_CTRL_REG = ( portNVIC_SYSTICK_CLK_BIT_CONFIG | portNVIC_SYSTICK_INT_BIT | portNVIC_SYSTICK_ENABLE_BIT );
```

끄기 → CVR 클리어 → LOAD = N − 1 → 켜기. 차이는 FreeRTOS 가 나눗셈을 **내림** 하고 범위·오차를 확인하지 않는다는 것 — `configTICK_RATE_HZ` 를 나누어떨어지게 고르는 것은 사용자 몫이다.

ISR 은 한 줄이다. 타깃에서는 이렇게 된다 (`demo_asm.c`, Apple clang 21, `-O2`, 실제 출력):

```text
SysTick_Handler:
	movw	r0, :lower16:g_ticks
	movt	r0, :upper16:g_ticks
	ldr	r1, [r0]
	adds	r1, #1
	str	r1, [r0]
	bx	lr
systick_init_1k_168m:
	movw	r0, #57360          @ 0xE010
	movw	r2, #36927          @ 0x903F
	movt	r0, #57344          @ 0xE000 → r0 = 0xE000E010
	movs	r1, #0
	movt	r2, #2              @ r2 = 0x0002903F = 167999
	str	r1, [r0]            @ CSR = 0
	str	r2, [r0, #4]        @ RVR
	str	r1, [r0, #8]        @ CVR = 0
	movs	r1, #7
	str	r1, [r0]            @ CSR = 7
	bx	lr
```

(`@` 뒤 주석은 읽기 편하게 붙인 것.) `g_ticks++` 는 ldr/adds/str 의 RMW 지만 **쓰는 쪽이 ISR 하나뿐** 이라 안전하다. main 은 읽기만 하고, 정렬된 32비트 `ldr` 한 번은 Cortex-M 에서 단일 복사 원자(single-copy atomic)이므로 `millis()` 가 찢어진 값을 볼 일이 없다. 64비트 tick 이면 얘기가 다르다 (연습 1, C-M14).

### Part B — 원 위의 비교: `(int32_t)(now − deadline) >= 0`

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">tick 이 0 을 지나는 32 ms: start = 0xFFFFFFF0, deadline = start + 32 = 0x00000010</text>
  <line x1="40" y1="80" x2="700" y2="80" stroke="currentColor" stroke-width="2"/>
  <line x1="370" y1="62" x2="370" y2="98" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="330" y="56" fill="currentColor">wrap: 0xFFFFFFFF → 0</text>
  <rect x="130" y="72" width="16" height="16" class="f-acc" stroke="currentColor"/>
  <text x="96" y="112" fill="currentColor" font-family="monospace">start F..FF0</text>
  <rect x="610" y="72" width="16" height="16" class="f-ok" stroke="currentColor"/>
  <text x="566" y="112" fill="currentColor" font-family="monospace">deadline 0x10</text>
  <rect x="230" y="72" width="16" height="16" class="f-warn" stroke="currentColor"/>
  <text x="200" y="112" fill="currentColor" font-family="monospace">now F..FF5</text>
  <text x="40" y="144" fill="currentColor" font-family="monospace">now = 0xFFFFFFF5 (5 ms 경과)</text>
  <text x="40" y="164" fill="currentColor" font-family="monospace">  naive : 0xFFFFFFF5 &gt;= 0x10            → true  (즉시 "만료" — 틀림)</text>
  <text x="40" y="184" fill="currentColor" font-family="monospace">  safe  : (int32_t)0xFFFFFFE5 = −27 &gt;= 0  → false (27 ms 남음 — 맞음)</text>
  <text x="40" y="212" fill="currentColor" font-family="monospace">now = 0x00000003, deadline = 0xFFFFFFFE (6 ms 마다 확인해 정확한 순간을 놓침)</text>
  <text x="40" y="232" fill="currentColor" font-family="monospace">  naive : 3 &gt;= 0xFFFFFFFE → false 가 49.7일 동안 계속   safe : (int32_t)5 = 5 &gt;= 0 → true</text>
</svg>
```

모듈러 산술로 보면 간단하다. 진짜 시각을 넘치지 않는 정수 T_now, T_dl 이라 하고 d = T_now − T_dl 이라 하자. 32비트 값은 T mod 2^32 이므로 `now − deadline` (uint32 뺄셈)은 d mod 2^32 이다. 이것을 int32 로 해석하면 [−2^31, 2^31) 범위의 대표값이 나오는데, **|d| < 2^31 이면 그 대표값이 d 자신** 이다. 따라서 부호가 진짜 선후 관계와 같다. naive `now >= deadline` 은 d 가 아니라 두 나머지의 대소를 비교하므로 한쪽만 0 을 넘은 경우에 뒤집힌다.

테스트가 보여 주는 한계 (2^31 근처):

| now | deadline | 결과 | 진짜 관계 | 판정 |
|---|---|---|---|---|
| 0x00000000 | 0x7FFFFFFF | false | 2^31 − 1 뒤 | 맞음 |
| 0x00000000 | 0x80000001 | true | 2^31 + 1 뒤 | **"이미 지남" 오판** |
| 0x7FFFFFFF | 0x00000000 | true | 2^31 − 1 지남 | 맞음 |
| 0x80000000 | 0x00000000 | false | 2^31 지남 | **"아직" 오판** |

1 kHz tick 에서 2^31 ms = **24.8일**. 그래서 규칙은 두 개다: ① 타임아웃 길이 < 2^31 tick, ② deadline 이 지난 뒤 2^31 tick 안에는 확인한다 (24일 동안 한 번도 안 보는 타이머 = 다시 "아직" 으로 보인다). `UINT32_MAX` 같은 "영원히 대기" 센티널은 deadline 형식에 넣으면 안 되고 따로 처리한다.

**경과 시간 형식** `millis() − start >= ms` 는 같은 모듈러 산술인데 범위가 다르다. 경과 시간 e = T_now − T_start 는 항상 0 이상이므로 부호 해석이 필요 없고, e < 2^32 이기만 하면 맞다. 테스트: start = 0xFFFFFFF0 에서 ms = 0xFFFFFFFF 짜리 타임아웃이 now = 0xFFFFFFEF 에서 만료, 0xFFFFFFEE 에서 아직. 그래서 STM32 HAL 의 모든 타임아웃은 `(HAL_GetTick() − tickstart) > Timeout` 형식이다. 대신 "여러 타이머 중 가장 빠른 deadline 고르기" 처럼 두 시각을 직접 비교해야 할 때는 deadline 형식이 필요하다 (RTOS-B06 의 delayed list).

| 형식 | 식 | 유효 범위 | 쓰는 곳 |
|---|---|---|---|
| deadline | `(int32_t)(now − dl) >= 0` | 거리 < 2^31 (24.8일 @1 kHz) | 타이머 리스트 정렬, Linux `time_after_eq` |
| 경과 | `(uint32_t)(now − start) >= ms` | 경과 < 2^32 (49.7일 @1 kHz) | 폴링 타임아웃, `HAL_Delay` |
| naive | `now >= dl` | wrap 을 안 걸칠 때만 | 쓰지 않는다 |

wrap 을 실제 tick 으로 넘기는 테스트(B3)의 실측 (`./solution -v`): deadline = 0x10 까지 기다리는 폴링 루프가 wrap-safe 는 **32000 클럭**(= 32 ms), naive 는 **0 클럭** — 시작하자마자 "만료".

```text
deadline 0x00000010: wrap-safe 32000 cycles, naive 0 cycles
```

타깃 asm 도 깔끔하다 (`demo_asm.c`, `-O2`, 실제 출력). wrap-safe 쪽이 오히려 분기가 없다 — `(now − dl)` 의 부호 비트를 뒤집어 꺼낸 것이 결과다.

```text
time_after_eq:
	subs	r0, r0, r1
	mvns	r0, r0
	lsrs	r0, r0, #31
	bx	lr
time_after_eq_naive:
	cmp	r0, r1
	mov.w	r0, #0
	it	hs
	movhs	r0, #1
	bx	lr
```

C 표준 메모: `(int32_t)` 로 2^31 이상 값을 바꾸는 것은 C11 6.3.1.3p3 에 따라 **구현 정의**(implementation-defined)다. GCC·Clang 은 "mod 2^N" 으로 문서화돼 있어 실무에서는 문제없지만, 완전히 정의된 식이 필요하면 `(uint32_t)(now − dl) < 0x80000000u` 를 쓴다 — 연습 3 에서 두 식이 같은 답을 내는 것을 확인한다. Linux 의 `time_after_eq(a,b)` 는 `(long)((a) − (b)) >= 0`, `time_after(a,b)` 는 엄격한 `>` 버전이다. Linux 는 부팅 5분 뒤 jiffies 가 일부러 넘치도록 초기값을 `-300*HZ` 로 둔다 (`INITIAL_JIFFIES`) — 이 레슨이 0xFFFFFFF0 에서 시작하는 것과 같은 발상이다.

### Part C — tick 위상과 +1 규칙

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">delay_ms(1): 부른 위상에 따라 걸리는 시간 (모델 N = 1000 클럭 = 1 ms)</text>
  <line x1="60" y1="60" x2="700" y2="60" stroke="currentColor"/>
  <line x1="100" y1="44" x2="100" y2="76" stroke="currentColor" stroke-width="2"/>
  <line x1="300" y1="44" x2="300" y2="76" stroke="currentColor" stroke-width="2"/>
  <line x1="500" y1="44" x2="500" y2="76" stroke="currentColor" stroke-width="2"/>
  <line x1="700" y1="44" x2="700" y2="76" stroke="currentColor" stroke-width="2"/>
  <text x="100" y="40" fill="currentColor" text-anchor="middle">tick k</text>
  <text x="300" y="40" fill="currentColor" text-anchor="middle">k+1</text>
  <text x="500" y="40" fill="currentColor" text-anchor="middle">k+2</text>
  <text x="680" y="40" fill="currentColor" text-anchor="middle">k+3</text>
  <text x="10" y="104" fill="currentColor">위상 999</text>
  <rect x="296" y="92" width="6" height="16" class="f-warn" stroke="currentColor"/>
  <text x="312" y="104" fill="currentColor">naive: 경계 1개 → 4 클럭 (0.004 ms!)</text>
  <rect x="296" y="116" width="206" height="16" class="f-ok" stroke="currentColor"/>
  <text x="510" y="128" fill="currentColor">+1: 경계 2개 → 1004 클럭</text>
  <text x="10" y="168" fill="currentColor">위상 0</text>
  <rect x="100" y="156" width="200" height="16" class="f-warn" stroke="currentColor"/>
  <text x="310" y="168" fill="currentColor">naive: 1000 클럭</text>
  <rect x="100" y="180" width="400" height="16" class="f-ok" stroke="currentColor"/>
  <text x="510" y="192" fill="currentColor">+1: 2000 클럭</text>
  <text x="10" y="226" fill="currentColor">전 위상 sweep 실측 — naive(1): 4 ~ 1000 클럭 → 최소 1 ms 를 못 지킨다</text>
  <text x="10" y="246" fill="currentColor">전 위상 sweep 실측 — +1(1): 1004 ~ 2000 클럭 → 항상 1 ms 이상, 2 ms (+루프 한 바퀴) 이하</text>
</svg>
```

`millis()` 는 연속 시간이 아니라 **계단** 이다. `start = millis()` 를 읽은 순간, 다음 경계까지 남은 시간은 0 ~ 1 ms 중 어디든 될 수 있다. 경계 w 개를 기다리면 실제 시간 t 는

  (w − 1)·T_tick < t ≤ w·T_tick (+ 폴링 한 바퀴)

이다. "최소 ms" 를 보장하려면 w − 1 ≥ ms, 즉 **w = ms + 1**. 대가는 평균 0.5 tick, 최대 1 tick 더 기다리는 것. 하네스가 1000 개 위상 전부(ms = 5 는 7 클럭 간격)에서 잰 실제 결과 (`./solution -v`):

```text
delay      | min cycles | max cycles  (N = 1000 cycles = 1 ms, spin = 4)
naive(1)   |          4 |       1000
+1   (1)   |       1004 |       2000
naive(5)   |       4008 |       5000
+1   (5)   |       5008 |       6000
```

naive(1) 의 최소 4 클럭은 "경계 직전에 불려서 루프 한 바퀴 만에 돌아온" 경우다. 실제 보드에서 `delay_ms(1)` 로 센서 파워업 1 ms 를 기다렸는데 가끔 초기화가 실패하는 버그가 바로 이것이다.

```c
static uint32_t delay_wait_ticks(uint32_t ms) {
    if (ms == 0u) return 0u;                         /* 0 은 즉시 */
    return ms < UINT32_MAX ? ms + 1u : ms;           /* UINT32_MAX + 1 = 0 → 즉시 반환 버그 방지 */
}
static void delay_ms(uint32_t ms) {
    uint32_t wait = delay_wait_ticks(ms);
    uint32_t start = millis();
    while ((uint32_t)(millis() - start) < wait) cpu_spin();
}
```

설계 선택:

- **경과 형식** 으로 비교하니 wrap 이 저절로 해결된다. 테스트: g_ticks = 0xFFFFFFF0 + 위상 999 에서 `delay_ms(40)` → 40 000 ~ 41 004 클럭, 끝난 뒤 g_ticks 가 0x100 미만 (wrap 을 실제로 지남). g_ticks = 0xFFFFFFFE 에서 `delay_ms(3)` 도 3000 ~ 4004.
- **`delay_ms(0)` = 즉시.** STM32 HAL 의 `HAL_Delay` 는 `if (wait < HAL_MAX_DELAY) wait += uwTickFreq;` 로 0 에도 1 tick 을 더한다 (그래서 `HAL_Delay(0)` 은 0 ~ 1 ms) — 정책 차이다. 어느 쪽이든 문서에 적는다. (HAL 소스 `stm32f4xx_hal.c` 기준, 버전에 따라 다를 수 있음 — 확인 권장)
- **포화(saturation)**: `ms + 1` 이 `UINT32_MAX` 에서 0 으로 넘치면 "영원히 대기" 가 "즉시 반환" 이 된다. HAL 도 같은 이유로 `HAL_MAX_DELAY` 는 더하지 않는다.
- **tick 이 1 kHz 가 아니면** ms → tick 변환을 **올림** 으로 하고 그 위에 +1 (연습 4: 1024 Hz 에서 `delay(1)` = 3 tick).

타깃에서 폴링 루프는 이렇게 된다 (`-Os`, 실제 출력). `ldr` 이 루프 안에 있는 것이 volatile 의 효과다.

```text
wait_ticks:
	movw	r1, :lower16:g_ticks
	movt	r1, :upper16:g_ticks
	ldr	r2, [r1]            @ start
.LBB4_1:
	ldr	r3, [r1]            @ 매번 메모리에서 다시 읽는다
	subs	r3, r3, r2
	cmp	r3, r0
	blo	.LBB4_1
	bx	lr
```

같은 루프에서 `volatile` 을 빼면 (`delay_ms_nonvolatile`, `-O2`, 실제 출력):

```text
delay_ms_nonvolatile:
	cmp	r0, #0
	it	eq
	bxeq	lr
.LBB5_1:
	b	.LBB5_1             @ 무한 루프: 루프 안에서 아무도 g_ticks_nv 를 안 바꾼다고 가정
```

컴파일러는 "이 함수 안에서 g_ticks_nv 를 바꾸는 코드가 없으니 `g_ticks_nv − start` 는 항상 0" 이라 판단하고 `ms != 0` 이면 영원히 도는 코드를 낸다. ISR 이 바꾼다는 사실은 C 추상 기계 밖의 일이라, 그것을 알리는 것이 volatile 이다 (C-B09).

### 호스트 모델 ↔ 타깃

| 이 레슨 (맥) | Cortex-M 타깃 |
|---|---|
| `REG_READ/REG_WRITE` → `mock_read32/mock_write32` | `*(volatile uint32_t *)addr` (같은 매크로의 `#else` 쪽) |
| `mock_cycles(n)` + `systick_hw_clock()` | 실제 코어 클럭, 디버거로 정지하면 SysTick 도 멈춘다 |
| 1→0 에서 `SysTick_Handler()` 직접 호출 | 예외 15 펜딩 → NVIC 가 우선순위에 따라 선점 (CMSIS 는 SysTick 을 가장 낮은 우선순위로 설정, BM-M01/M02) |
| `cpu_spin()` = 4 클럭 + 폭주 시 longjmp | `nop` 또는 아무것도 없음. 전력을 아끼려면 `__WFI()` (다음 SysTick 이 깨운다, BM-M11) |
| `g_ticks` 시작값 0xFFFFFFF0 | 0 (테스트 빌드에서만 바꾸는 것도 좋은 습관 — FreeRTOS `configINITIAL_TICK_COUNT` (V11.3.2 FreeRTOS.h, 기본 0), Linux `INITIAL_JIFFIES`) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 73/73 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 73/73, 경고 없음
```

## 7. 자주 하는 실수

**1. reload 에 N 을 넣는다 (−1 빠짐).** `SysTick->LOAD = SystemCoreClock / 1000;` → 주기 168 001 클럭 = 999.994 Hz, −5.95 ppm. 하루 0.51 초, 한 달 15 초 늦는 시계. 크리스털 오차(±20 ppm)에 묻혀 몰래 산다.

```c
SysTick->LOAD = SystemCoreClock / 1000u - 1u;   /* 주기 = LOAD + 1 */
```

**2. 24비트를 확인하지 않는다.** 168 MHz 에서 10 Hz 를 원해 `LOAD = 16799999` (0x10058FF) 를 쓰면 RVR 에는 하위 24비트 0x0058FF = 22 783 만 남는다 (상위는 예약 비트 — 이 mock 은 버린다). 주기 22 784 클럭 = 10 Hz 대신 약 7.37 kHz tick. 함수가 `ST_ERR_RANGE` 를 돌려줘야 한다. (CMSIS `SysTick_Config` 도 `(ticks − 1) > 0xFFFFFF` 면 1 을 반환한다 — 반환값을 버리지 말 것.)

**3. CVR 을 안 지운다.** 리셋값 UNKNOWN. 첫 tick 이 최대 2^24 클럭 늦게 온다. 테스트 "999 클럭에 0, 1000 클럭에 1" 이 이것을 잡는다.

**4. `g_ticks` 에 volatile 이 없다.** 5. 풀이의 `b .LBB5_1` — `-O0` 디버그 빌드에선 되고 `-O2` 릴리스에서만 멈추는 전형적인 "최적화 켜면 죽는" 버그.

**5. `now >= deadline` / `while (millis() < deadline)`.** 49.7일마다 즉시 만료 또는 영영 안 끝남. Windows 95/98 이 `GetTickCount` 가 넘치는 49.7일 뒤 멈추던 유명한 사례가 같은 구조다.

```c
while (!time_after_eq(millis(), deadline)) { }       /* deadline 형식 */
while ((uint32_t)(millis() - start) < ms) { }         /* 경과 형식 */
```

**6. `delay_ms(ms)` 가 경계 ms 개만 기다린다.** 최소 (ms − 1) ms 남짓 — `delay_ms(1)` 은 0 ~ 1 ms. +1. 그리고 `ms + 1` 오버플로를 포화로 막는다.

**7. deadline 형식에 "무한 대기" 를 넣는다.** `deadline = now + 0xFFFFFFFF` = `now − 1` → 즉시 만료. 2^31 이상 타임아웃은 센티널로 따로 처리하거나 경과 형식을 쓴다.

## 8. 면접 Q&A

<details>
<summary>Q1. 1 ms tick 을 만들려면 RVR 에 무엇을 넣나요? 왜 −1 이죠? 0 을 넣으면?</summary>

`RVR = SystemCoreClock / 1000 − 1`. 카운터는 RVR … 1, 0 을 지나 다음 클럭에 RVR 을 다시 싣는다. 0 에서도 한 클럭 머무르므로 주기는 RVR + 1 클럭이다 (Cortex-M4 Generic User Guide: "100 클럭마다면 RELOAD = 99"). 168 MHz 면 167 999 = 0x2903F. RVR = 0 이면 1→0 전이가 없어서 COUNTFLAG 도 예외도 안 생긴다 — 사실상 정지. 범위는 1 ~ 0xFFFFFF 라 168 MHz 에서 최대 99.86 ms. 꼬리: "더 긴 주기는?" → CLKSOURCE = 0 (구현 정의 외부 기준, STM32F4 는 HCLK/8 — RM0090 확인 권장) 또는 일반 타이머(BM-B14), 아니면 소프트웨어로 tick 을 나눈다.

</details>

<details>
<summary>Q2. g_ticks 는 왜 volatile 인가요? volatile 이면 충분한가요?</summary>

ISR 이 바꾸는 값을 main 이 루프에서 읽기 때문이다. volatile 이 없으면 컴파일러는 루프 안에서 값이 안 변한다고 보고 읽기를 루프 밖으로 빼거나 (5. 풀이처럼) 무한 루프 `b .` 로 바꾼다. 충분한가: 이 경우는 **그렇다** — 쓰는 쪽은 ISR 하나, 값은 정렬된 32비트라 Cortex-M 의 `ldr` 한 번이 원자적이고, main 은 읽기만 한다. 충분하지 않은 경우: ① 64비트 tick (Cortex-M3/M4 에서 두 번 읽기 → 찢어진 읽기, 연습 1 / C-M14), ② main 도 쓰는 경우 (`g_ticks = 0` 리셋은 ISR 의 RMW 와 경쟁 → lost update, BM-B04 / C-M14), ③ 멀티코어. volatile 은 "매번 메모리 접근" 만 보장하지 원자성·순서(다른 변수와의)는 보장하지 않는다.

</details>

<details>
<summary>Q3. `(int32_t)(now − deadline) >= 0` 이 왜 wrap 에 안전한지 설명해 보세요. 한계는?</summary>

32비트 tick 은 진짜 시각 mod 2^32 다. 부호 없는 뺄셈 `now − deadline` 은 진짜 차이 d 의 mod 2^32 값이고, int32 로 보면 [−2^31, 2^31) 대표값이 나온다. |d| < 2^31 이면 그 대표값이 d 자신이라 부호가 맞다. 한계: 거리 2^31 tick (1 kHz 에서 24.8일) 이상이면 방향이 뒤집힌다 — 테스트: deadline 이 2^31 + 1 뒤면 "이미 지남", 정확히 2^31 지났으면 "아직". 그래서 타임아웃 < 2^31, 그리고 지나간 deadline 을 2^31 tick 안에 확인. C 표준 관점: 범위 밖 unsigned → signed 변환은 C11 6.3.1.3p3 구현 정의 (GCC/Clang 은 modulo). 완전히 정의된 대안은 `(uint32_t)(now − deadline) < 0x80000000u` (연습 3). 꼬리: "경과 시간 형식은?" → `now − start >= ms` 는 경과 < 2^32 이면 맞아 범위가 두 배.

</details>

<details>
<summary>Q4. HAL_Delay(1) 을 오실로스코프로 재 보니 1 ~ 2 ms 사이로 흔들립니다. 버그인가요?</summary>

의도된 동작이다. `millis()` 는 1 ms 계단이라 호출 순간이 tick 주기 안 어디냐(위상)에 따라 첫 경계까지 0 ~ 1 ms 가 걸린다. 경계를 ms 개만 기다리면 실제 시간은 (ms − 1, ms] — `delay(1)` 이 몇 µs 만에 끝날 수 있다. HAL 은 `wait += uwTickFreq` 로 경계를 하나 더 기다려 **최소** 를 보장하고, 그 대가로 [ms, ms + 1] 로 흔들린다 (이 레슨 실측: 1004 ~ 2000 클럭). 정밀한 짧은 지연이 필요하면 tick 이 아니라 DWT CYCCNT (BM-A10) 나 하드웨어 타이머(BM-B14)로 사이클 단위로 잰다. 꼬리: "UINT32_MAX 를 넘기면?" → +1 이 0 으로 넘치므로 HAL 은 `HAL_MAX_DELAY` 에는 안 더한다.

</details>

<details>
<summary>Q5. COUNTFLAG 는 언제 쓰나요? 함정은?</summary>

인터럽트 없이 SysTick 을 폴링할 때 쓴다 — 예: 벡터 테이블·NVIC 준비 전의 부트 코드에서 `while (!(SysTick->CTRL & COUNTFLAG))` 로 1 tick 대기. 함정: ① **읽으면 지워진다** (rc_r). 다른 이유로 CSR 을 읽는 코드(예: CLKSOURCE 확인)가 플래그를 먹어 버리면 폴링이 한 주기를 놓친다. 디버거의 메모리/레지스터 창이 CSR 을 읽어도 같은 일이 생길 수 있다 (디버거·IDE 동작에 따라 다름). ② CVR 쓰기도 지운다. ③ 한 비트뿐이라 두 번 넘친 것과 한 번 넘친 것을 구분 못 한다. 이 레슨 mock 은 `csr_on_read` 훅으로 ① 을 모델링하고 테스트가 "첫 읽기 1, 두 번째 0" 을 확인한다.

</details>

<details>
<summary>Q6. 플래시 지우기 동안 인터럽트를 3 ms 막았습니다. millis() 는 어떻게 되나요?</summary>

SysTick 예외의 펜딩 상태는 **1 비트** 다. 막혀 있는 3 ms 동안 tick 이 세 번 오지만 펜딩은 하나로 합쳐지고, 풀리는 순간 ISR 은 한 번만 돈다 → `g_ticks` 는 2 ms 를 잃는다. tick 기반 시계는 그만큼 영구히 느려진다. 대책: ① 임계 구역을 1 tick 보다 짧게, ② 시간을 tick 개수가 아니라 자유 실행 하드웨어 카운터(32비트 타이머, RTC, DWT CYCCNT)에서 읽는다 — 카운터는 인터럽트를 막아도 계속 센다, ③ ISR 에서 CVR/하드웨어 카운터로 놓친 tick 을 보정 (RTOS tickless idle 이 SysTick 을 멈췄다 다시 켤 때 `vTaskStepTick()` 으로 보정하는 것과 같은 발상 — BM-M11 Q8). 같은 "펜딩 합쳐짐" 은 C-M14, RTOS-B08 참고.

</details>

<details>
<summary>Q7. SysTick 으로 µs 단위 시간을 얻으려면?</summary>

`us = g_ticks × 1000 + (RVR − CVR + 1) % (RVR + 1) × 1000 / (RVR + 1)`. 다운카운터라 경과 클럭은 RVR − CVR (+1, 재장전 클럭까지 세면) 이다. 함정은 **g_ticks 와 CVR 이 서로 다른 순간의 값** 일 수 있다는 것 — g_ticks 를 읽은 직후 경계가 지나면 CVR 은 새 주기의 작은 경과값인데 g_ticks 는 옛 값이라 1 ms 과거로 간다. 해법: `do { t1 = g_ticks; cvr = CVR; t2 = g_ticks; } while (t1 != t2);` (연습 2: 위상 3000 개 중 naive 가 9 번 뒤로 감, 재읽기는 0 번). 인터럽트가 꺼진 상태에서 부르면 ISR 이 못 돌아 t1 == t2 인데 CVR 은 이미 재장전된 경우가 남는다 — 그때는 COUNTFLAG 나 ICSR.PENDSTSET 를 확인해 1 tick 을 더한다. Cortex-M3/M4/M7 에 DWT CYCCNT 가 있으면 더 간단하다 (구현 선택 — BM-A10).

</details>

<details>
<summary>Q8. tick 주파수는 어떻게 고르나요? 1 kHz vs 10 kHz vs 100 Hz.</summary>

해상도 vs 오버헤드 vs 전력. ISR 이 진입·복귀 포함 약 수십 클럭이라 1 kHz × 168 MHz 에서는 CPU 의 0.0x % 지만, 10 kHz 면 10배, 그리고 저전력 노드에서는 tick 마다 깨어나는 것 자체가 전력을 먹는다 (그래서 tickless idle — BM-M11, RTOS). 해상도가 tick 보다 세밀해야 하는 일(µs 펄스, 통신 타임아웃의 짧은 구간)은 tick 을 올리는 대신 하드웨어 타이머나 CYCCNT 로 한다. 1 kHz 가 관례인 이유는 ms 단위 API 와 1:1 이라 변환·반올림 실수가 없기 때문 — 32.768 kHz 처럼 1 kHz 가 정확히 안 나오는 클럭에선 1024 Hz 를 쓰고 변환을 올림으로 한다 (연습 4).

</details>

## 9. 추가 연습

### 연습 1 — 64비트 tick: hi:lo 두 번 읽기

ISR 이 `if (++lo == 0) hi++;` 로 64비트 tick 을 두 칸에 유지한다. main 의 `ticks64()` 를 쓰라. 읽기 사이 아무 곳에나 ISR 을 주입해도 결과가 "읽기 시작 시각 ~ 끝 시각" 안에 있어야 한다.

<details>
<summary>답 보기</summary>

`do { h1 = hi; lo = lo; h2 = hi; } while (h1 != h2); return (h1 << 32) | lo;` — hi 가 그대로였다면 그 사이 lo 의 넘침이 없었으므로 (h1, lo) 는 한 순간의 값이다. naive `hi → lo` 는 hi 를 읽은 직후 lo 가 0xFFFFFFFF → 0 으로 넘치면 `7:00000000` 을 돌려준다 — 2^32 − 1 tick 과거 (1 kHz 면 49.7일 뒤로). 테스트: 주입 위치 5가지 × wrap 근처 시작값 5개 중 naive 는 정확히 1 경우 틀림, 재읽기는 0, 그 경우 재읽기는 한 번 재시도(읽기 6회). 인터럽트를 잠깐 막는 방법도 맞지만, 재읽기는 인터럽트 지연을 늘리지 않는다. 자세한 분석은 C-M14.

</details>

### 연습 2 — micros(): (tick, CVR) 일관성

1 MHz 코어, 1 kHz tick (RVR = 999, 1 클럭 = 1 µs) 에서 `micros()` 를 쓰라. CVR 을 읽기 직전에 3 클럭 지연(선점 또는 버스 대기)을 주입했을 때 결과가 호출 구간 안에 있어야 한다.

<details>
<summary>답 보기</summary>

경계 이후 경과 클럭 = `(RVR − CVR + 1) % (RVR + 1)` — CVR = 0 (경계 직후, 아직 재장전 전)이면 0, CVR = RVR 이면 1. `do { t1 = g_ticks; cvr = CVR; t2 = g_ticks; } while (t1 != t2);` 후 `t1·1000 + offset·1000/(RVR+1)`. 정지 상태에서 3000 개 위상 전부 `micros() == 실제 클럭` 이고, 3 클럭 주입 시 naive 는 경계 3개 × 위상 3개 = **9 번** 뒤로 간다. 예: 2999 클럭에서 부르면 naive 는 2002 (진짜 2999 ~ 3002 — 약 1 ms 과거), 재읽기는 3002. 실제 칩에서는 SysTick 클럭이 CLKSOURCE 에 따라 다르니 `RVR + 1` 클럭이 1 tick 이라는 관계만 쓴다.

</details>

### 연습 3 — 구현 정의 없는 비교

`(int32_t)(now − dl) >= 0` 과 `(uint32_t)(now − dl) < 0x80000000u` 가 모든 입력에서 같은지 확인하라.

<details>
<summary>답 보기</summary>

int32 로 봤을 때 음수 ⟺ 최상위 비트 1 ⟺ unsigned 값 ≥ 2^31 이므로 같다 (int32 변환이 mod 2^32 인 구현에서). 연습은 거리 전 구간을 65521 간격으로 + 0 과 2^31 근처 ±300 전부 + 무작위 100만 쌍에서 불일치 0 을 확인한다. 실무 코드는 둘 중 아무거나 — MISRA 처럼 구현 정의를 싫어하는 규칙 아래에선 unsigned 버전, 의도가 잘 보이는 건 signed 버전. `0x80000000 vs 0` 에서 둘 다 false (같은 한계).

</details>

### 연습 4 — 32.768 kHz 에서 1024 Hz tick

코어 클럭이 32.768 kHz 인 초저전력 모드에서 tick 을 1024 Hz 로 둔다 (reload 31, 정확). `ms_to_ticks_ceil(ms)`, `ticks_to_ms(t)`, 그리고 +1 규칙을 적용한 `delay_wait_ticks_1024(ms)` 를 쓰라. 각 tick 주파수의 wrap 주기는?

<details>
<summary>답 보기</summary>

`ceil(ms × 1024 / 1000)` 을 **64비트 중간값** 으로 (`ms × 1024` 는 ms ≥ 4 194 304 에서 32비트를 넘는다), 결과가 2^32 − 1 을 넘으면 포화. 1 ms → 2 tick (1.024 를 올림), 125 ms → 정확히 128, 1000 → 1024. 포화하지 않고 잘라 버리면 `UINT32_MAX` ms 가 103 079 215 tick 으로 둔갑한다. delay 는 올림 + 1: `delay(1)` = 3 tick — 최악 위상에서 실제 대기 (3 − 1)/1024 s = 1.95 ms ≥ 1 ms. 1 ~ 3000 ms 전부에서 "최악 위상 대기 ≥ ms" 를 확인한다. wrap 주기 = 2^32 / tick_hz: 1 kHz → 4 294 967 s = **49.71 일**, 1024 Hz → 4 194 304 s = 48.5 일, 1 MHz(µs 카운터) → 4294 s = **71.6 분**. deadline 형식의 한계는 그 절반 (1 kHz 에서 24.8 일).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 29/29 passed
```

## 10. 핵심 정리

- SysTick = 코어 내장 24비트 다운카운터, `0xE000E010` CSR / `…14` RVR / `…18` CVR / `…1C` CALIB. 주기 = RVR + 1 클럭 → **RVR = N − 1**, 1 ≤ RVR ≤ 0xFFFFFF (168 MHz 에서 최대 99.86 ms).
- init: (끄기) → RVR → **CVR 클리어** (리셋값 UNKNOWN) → CSR = CLKSOURCE|TICKINT|ENABLE. 계산 실패면 레지스터를 건드리지 않는다.
- 이벤트는 1→0 감소에서만. COUNTFLAG 는 rc_r (읽으면 0), CVR 쓰기로도 0. 펜딩은 1 비트 → 인터럽트를 1 tick 넘게 막으면 tick 을 잃는다.
- `volatile uint32_t g_ticks`: ISR 만 쓰고 32비트라 volatile 로 충분. 빠지면 `-O2` 에서 `b .` 무한 루프. 64비트는 hi→lo→hi 재읽기.
- deadline 형식 `(int32_t)(now − dl) >= 0` 은 거리 < 2^31 에서 정확 (1 kHz 24.8일), 경과 형식 `now − start >= ms` 는 < 2^32 (49.7일). `now >= dl` 은 쓰지 않는다.
- `delay_ms(ms)` 는 경계 **ms + 1** 개 → [ms, ms + 1] ms. `ms + 1` 은 `UINT32_MAX` 에서 포화. HAL_Delay 와 같은 규칙.
- 테스트는 tick 을 0xFFFFFFF0 에서 시작해 wrap 을 반드시 지나게 한다 (Linux `INITIAL_JIFFIES` 와 같은 발상).

## 참고자료

- ARMv7-M Architecture Reference Manual (ARM DDI 0403E) — B3.3 "The system timer, SysTick" (SYST_CSR/RVR/CVR/CALIB 비트, COUNTFLAG, CVR 쓰기 동작) — <https://developer.arm.com/documentation/ddi0403/latest/>
- Cortex-M4 Devices Generic User Guide (ARM DUI 0553) — 4.4 System timer: "RELOAD = N − 1", reload 0 은 효과 없음, 초기화 순서 — <https://developer.arm.com/documentation/dui0553/latest/>
- CMSIS-Core `SysTick_Config()` (core_cm4.h: `(ticks − 1) > SysTick_LOAD_RELOAD_Msk` 검사, 우선순위 최하위, VAL = 0, CTRL 설정) — <https://arm-software.github.io/CMSIS_6/latest/Core/group__SysTick__gr.html>
- STM32CubeF4 HAL `stm32f4xx_hal.c` — `HAL_InitTick`, `HAL_IncTick`, `HAL_GetTick`, `HAL_Delay` (`wait += uwTickFreq`) — <https://github.com/STMicroelectronics/STM32CubeF4>
- ST RM0090 (STM32F405/407/42x/43x) 클럭 트리의 Cortex System timer HCLK/8, PM0214 (STM32F4 Cortex-M4 programming manual) SysTick 장 — 이 레슨에선 미확인, 확인 권장 — <https://www.st.com>
- Linux `include/linux/jiffies.h` — `time_after`, `time_after_eq`, `INITIAL_JIFFIES` — <https://github.com/torvalds/linux/blob/master/include/linux/jiffies.h>
- ISO/IEC 9899:2011 (C11) 6.3.1.3 Signed and unsigned integers (p3: 범위 밖 signed 변환은 구현 정의)
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.), SysTick 장
- FreeRTOS-Kernel V11.3.2 `portable/GCC/ARM_CM4F/port.c` `vPortSetupTimerInterrupt()`, `include/FreeRTOS.h` `configINITIAL_TICK_COUNT` — <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- 관련 레슨: BM-B02 (mock 프레임워크), BM-B04 (RMW·lost update), BM-B14 (타이머 PSC/ARR), C-B09 (volatile), C-M14 (ISR↔main 공유, 64비트 찢어진 읽기), RTOS-B05 (tick 기반 선점), RTOS-B06 (Delay vs DelayUntil, tick wrap 비교, delayed list), BM-M11 (WFI·tickless), BM-A10 (DWT CYCCNT), BM-M01/M02 (SysTick 우선순위)
