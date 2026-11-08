---
id: BM-M11
time: 90분
updated: 2026-11-08
---
# BM-M11 저전력 모드: WFI/WFE, Stop/Standby, 슬립 진입 레이스

## 학습 목표

- Run / Sleep / Stop / Standby 를 **전류, 깨는 데 걸리는 시간(wake latency), 무엇이 남나(retention), 깨면 어디서 다시 시작하나(resume point), 무엇이 깨울 수 있나(wake source)** 다섯 칸으로 구분해 표로 말할 수 있다.
- duty cycle(활성 비율)로 평균 전류와 배터리 수명을 계산하고, "Standby 가 항상 이득은 아니다" 를 손익분기(break-even) 시간으로 설명할 수 있다.
- `if (!flag) __WFI();` 가 웨이크업을 잃는 지점을 찾고, `__disable_irq(); if (!flag) __WFI(); __enable_irq();` 가 왜 막는지 ARMv7-M ARM B1.5.19 문장으로 설명할 수 있다 (PRIMASK=1 이어도 펜딩 IRQ 가 WFI 를 끝낸다).
- WFE + 이벤트 레지스터(event register) / SEV / SEVONPEND, SLEEPONEXIT 가 각각 언제 쓰이고 무엇이 함정인지 안다.
- 이 패턴이 FreeRTOS tickless idle(`vPortSuppressTicksAndSleep`)과 Zephyr `k_cpu_atomic_idle()` 안에 그대로 들어 있음을 확인한다.

## 1. 문제

배터리로 몇 년 버텨야 하는 센서 노드(sensor node)에서 펌웨어가 실제로 하는 일은 대부분 **"자는 것"** 이다. 10초마다 깨어 1 ms 동안 센서를 읽고 다시 자는 노드는 시간의 99.99 % 를 잔다. 그래서 면접에서는 "어떤 모드로 자나, 깨면 무엇이 남아 있나" 와 함께, **"`if (!flag) __WFI();` 에 버그가 있습니다. 어디죠?"** 라는 질문이 거의 항상 나온다. 플래그를 확인한 직후, WFI 를 실행하기 직전에 IRQ 가 오면 ISR 은 플래그를 세우고 돌아오는데 CPU 는 그 사실을 모른 채 잠든다. 다음 깨움(1초 뒤 RTC)까지 이벤트가 방치된다 — **슬립 진입 레이스(sleep-entry race), 웨이크업 유실(lost wakeup)**.

전력 모드는 칩마다 이름이 다르지만(STM32: Sleep / Stop0·1·2 / Standby / Shutdown, nRF52: System ON idle / System OFF), 구조는 같다. 깊이 잘수록 전류는 작아지고, 꺼지는 것이 많아지며, 깨는 데 오래 걸리고, 깨울 수 있는 소스가 줄어든다.

```svg
<svg viewBox="0 0 740 345" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">전력 상태 머신 (이 레슨의 모델 값 — STM32L4 계열 자릿수 참고, 특정 칩 수치 아님)</text>
  <rect x="290" y="40" width="160" height="56" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="300" y="62" fill="currentColor" font-weight="bold">Run 1.5 mA</text>
  <text x="300" y="82" fill="currentColor">CPU + 클럭 + 주변장치</text>
  <rect x="560" y="40" width="170" height="56" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="570" y="62" fill="currentColor" font-weight="bold">리셋 벡터</text>
  <text x="570" y="82" fill="currentColor">startup → main (2 ms)</text>
  <rect x="20" y="200" width="200" height="74" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="30" y="220" fill="currentColor" font-weight="bold">Sleep 400 µA, 깨움 ~1 µs</text>
  <text x="30" y="238" fill="currentColor">CPU 클럭만 멈춤, 다 남음</text>
  <text x="30" y="256" fill="currentColor">깨움: 아무 IRQ</text>
  <rect x="270" y="200" width="200" height="74" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="280" y="220" fill="currentColor" font-weight="bold">Stop 1.8 µA, 깨움 ~5 µs</text>
  <text x="280" y="238" fill="currentColor">SRAM·레지스터 남음, PLL 꺼짐</text>
  <text x="280" y="256" fill="currentColor">깨움: EXTI·RTC·LPUART</text>
  <rect x="520" y="200" width="210" height="74" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="530" y="220" fill="currentColor" font-weight="bold">Standby 400 nA</text>
  <text x="530" y="238" fill="currentColor">SRAM 소실, 백업 레지스터만</text>
  <text x="530" y="256" fill="currentColor">깨움: WKUP 핀·RTC·리셋</text>
  <path d="M300 96 L150 200" stroke="currentColor" fill="none"/>
  <path d="M370 96 L370 200" stroke="currentColor" fill="none"/>
  <path d="M440 96 L600 200" stroke="currentColor" fill="none"/>
  <text x="120" y="140" fill="currentColor">WFI/WFE</text>
  <text x="120" y="156" fill="currentColor">(SLEEPDEEP=0)</text>
  <text x="378" y="140" fill="currentColor">SLEEPDEEP=1</text>
  <text x="378" y="156" fill="currentColor">+ LPMS=Stop</text>
  <text x="520" y="140" fill="currentColor">LPMS=Standby</text>
  <path d="M100 200 L270 90" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
  <path d="M420 200 L420 96" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
  <path d="M700 200 L700 96" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
  <path d="M560 68 L450 68" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
  <text x="20" y="296" fill="currentColor">점선 = 깨어남. Sleep·Stop 은 WFI 다음 명령에서 이어서 실행한다.</text>
  <text x="20" y="314" fill="currentColor">Stop 은 SYSCLK 가 HSI16/MSI 로 바뀌어 있어 PLL 을 다시 켜야 한다.</text>
  <text x="20" y="332" fill="currentColor">Standby 는 리셋 벡터부터 — SBF 플래그로 "Standby 복귀" 를 알고 백업 레지스터에서 복원.</text>
</svg>
```

이 레슨은 맥에서 돈다. 진짜 MCU 가 없으니 두 가지 시뮬레이터를 쓴다: ① 전류·시간을 정수(nA, µs)로 적분하는 **전력 상태 머신**, ② Cortex-M 코어의 PRIMASK·NVIC 펜딩·이벤트 레지스터를 "연산(op) 단위" 로 흉내 내고 **IRQ 를 시퀀스의 모든 지점에 하나씩 주입하는** 결정적(deterministic) 레이스 시뮬레이터. 같은 레이스가 호스트에서도 진짜로 일어나는지는 `demo_sigsuspend_race.c` (POSIX `pause()` vs `sigsuspend()`)로 보인다.

### Part A — 전력 상태 머신과 duty cycle

`MODE[]` 표(전류, wake_us, boot_us, RAM 보존, 고속 클럭 보존, 복귀 지점, 깨울 수 있는 소스)는 주어진다.

- `int pm_enter(mcu_t *m, pmode_t mode, const wake_ev_t *ev, int n)` — 모드로 들어가 잔다. **그 모드를 깨울 수 있는** 첫 이벤트에서 깨고, 잠든 구간은 그 모드 전류, wake_us + boot_us 는 Run 전류로 전하를 적분한다. RAM/클럭 소실, 복귀 지점, Standby 의 SBF 플래그를 반영한다. 깨울 이벤트가 없으면 -1 (Stop 에 들어가면서 TIM2 만 깨움 소스로 남겨 둔 버그 = 영영 안 깸).
- `period_charge(T, t_act, mode)` / `avg_nA(...)` / `battery_days(cap_mAh, avg)` — 주기 T 마다 t_act 동안 일하는 노드의 평균 전류와 CR2032 수명.

### Part B — 슬립 진입 레이스: 모든 지점에 IRQ 주입

시뮬레이터 연산: `CPSID()`, `CPSIE()`, `rd_flag()` (ldr+cmp+분기), `WFI()`, `WFE()`, `SEV()`. IRQ 는 k 번째 연산이 끝난 직후 도착한다 (k = 0 은 idle 진입 전, 잠든 WFI 는 "잠든 직후" 가 그 연산의 끝 = 자는 동안 도착).

- `wfi_wake_cond(isr_ran)` — WFI 가 잠들지 않거나 깨어나는 조건 (B1.5.19). 시작 코드는 "ISR 이 돌아야 깬다" 라는 흔한 오해로 되어 있다.
- `wfe_wake_cond()` — WFE 조건 (B1.5.18: 이벤트 레지스터).
- `idle_correct()` — 정석 패턴. `idle_atomic_wfe()` — PRIMASK 를 건 채 WFE 로 자기 (Zephyr 방식): 마스크된 IRQ 가 WFE 를 깨우려면 무엇이 필요한가?
- 하네스의 `sweep()` 이 각 idle 패턴을 모든 k 에 대해 돌려 "이미 와 있는 이벤트를 두고 잠든" 지점을 비트마스크로 돌려준다. naive 는 {1}, WFI 전에 켜는 "틀린 고침" 은 {1,2,3}, 정석은 ∅ 이어야 한다.

### Part C — 센서 노드의 슬립 매니저

- `pmode_t pm_choose(const pm_req_t *r)` — 다음 일까지 `idle_us`, RAM 유지 필요 여부, 고속 클럭이 필요한 주변장치 동작 여부(USART1 수신), 반드시 깨울 수 있어야 하는 소스, 허용 웨이크업 지연이 주어지면, 조건을 만족하는 모드 중 전하가 가장 작은 것을 고른다.
- `uint64_t breakeven_us(shallow, deep)` — deep 모드가 이득이 되기 시작하는 idle 시간. 모델 값으로 Stop → Standby 손익분기는 약 **2.19 s**.

## 2. Problem (English)

> **Low-power modes and the sleep-entry race on a Cortex-M sensor node.**
>
> You are given a mode table (Run 1.5 mA; Sleep 400 µA, 1 µs wake; Stop 1.8 µA, 5 µs wake, SRAM kept, PLL lost, wakes only on EXTI/RTC/LPUART/WKUP; Standby 400 nA, 50 µs wake + 2 ms re-boot, SRAM lost, resumes at the reset vector, wakes only on RTC/WKUP pin).
>
> 1. `int pm_enter(mcu_t *m, pmode_t mode, const wake_ev_t *ev, int n)` — sleep until the first event at or after `m->t_us` that can wake `mode`; integrate charge in nA·µs (sleep current while asleep, Run current for wake + boot); apply retention/resume effects; return the event index or -1. `uint64_t period_charge(T, t_act, mode)`, `uint32_t avg_nA(...)` (rounded), `uint32_t battery_days(cap_mAh, avg_nA)`.
> 2. A deterministic core model with PRIMASK, NVIC pending, event register and SEVONPEND. Implement the WFI/WFE wake conditions, `idle_correct()` (mask → check flag → WFI → unmask) and `idle_atomic_wfe()`. A harness injects the IRQ after every operation of each idle sequence and reports where a wakeup was lost (the core went to sleep although the event had already arrived).
> 3. `pmode_t pm_choose(const pm_req_t *r)` (deepest *useful* mode under constraints) and `uint64_t breakeven_us(shallow, deep)`.
>
> Example: T = 10 s, t_act = 1 ms → Run 1500000 nA, Sleep 400110 nA, Stop 1951 nA, Standby 857 nA; CR2032 (225 mAh) on Stop ≈ 4805 days. Sweep of `if (!flag) WFI();` → lost at k = 1, trace `"rF IRQ WFI zz RTC"`. Correct pattern, IRQ during sleep with PRIMASK = 1 → `"CPSID rF WFI zz pend CPSIE IRQ"`.
>
> Follow-ups: why does WFI wake while PRIMASK = 1 but the ISR does not run? Why does BASEPRI masking break this? When is Standby *worse* than Stop? What does SLEEPONEXIT do to your main loop?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "9/64 passed" (naive · 틀린 고침의 유실 재현 테스트는 시작 코드로도 통과) — TODO 를 채워 64/64
./starter -v      # 주입 지점별 trace 표 (정답 코드로 돌리면 5. 풀이의 표가 나온다)
```

관찰용 파일 (하네스와 별개):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_duty_table.c -o demo_duty_table && ./demo_duty_table   # 평균 전류 표·CSV
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_sigsuspend_race.c -o dsr && ./dsr          # 호스트에서 진짜 레이스
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4.c -o -               # 패턴의 실제 asm
clang --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -O2 -S demo_m4.c -o -            # M0+ 비교
```

## 4. 힌트

<details>
<summary>힌트 1 — Part A: 이벤트를 고를 때 두 조건, 전하는 두 토막</summary>

이벤트는 ① `ev[i].t_us >= m->t_us` (이미 지난 것은 무시) ② `ev[i].src & MODE[mode].wake_mask` 둘 다 맞아야 한다. 전하 = 그 모드 전류 × (이벤트 시각 − 지금) + Run 전류 × (wake_us + boot_us). 곱하기 전에 `uint64_t` 로 올려라 — 1.5e6 nA × 1e7 µs 는 32비트를 넘는다.

</details>

<details>
<summary>힌트 2 — Part A: 한 주기 안의 세 구간</summary>

한 주기 = 일(t_act, Run) + 깨는 오버헤드(wake+boot, Run) + 나머지(그 모드 전류). 오버헤드는 **주기 안에** 넣는다 — 펌웨어는 RTC 알람을 웨이크업 지연만큼 일찍 맞춰 일이 제시간에 시작하게 하니까 (테스트의 3주기 시뮬레이션이 그렇게 한다). t_act + 오버헤드가 T 이상이면 잘 틈이 없으므로 주기 전체가 Run.

</details>

<details>
<summary>힌트 3 — Part B: PRIMASK 는 "ISR 실행" 을 막지 "깨어남" 을 막지 않는다</summary>

B1.5.19 의 Note: PRIMASK 가 1 이어도, 활성 예외보다 (그리고 BASEPRI 보다) 높은 우선순위의 비동기 예외는 **WFI 를 끝낸다**. 실행은 되지 않고 펜딩으로 남는다. 그러니 WFI 조건은 "ISR 이 돌았다" 뿐 아니라 "펜딩이 있다" 도 포함해야 한다. WFE 는 다르다: B1.5.18 의 wake-up 이벤트 목록에는 "선점하는 예외" 와 "SEVONPEND 가 켜져 있을 때 펜딩으로 바뀌는 예외" 가 있다 — 마스크된 IRQ 는 SEVONPEND 없이는 WFE 를 깨우지 못한다.

</details>

<details>
<summary>힌트 4 — Part B: 확인과 잠들기를 한 덩어리로</summary>

창(window)은 "플래그를 0 으로 읽은 순간 ~ 잠드는 순간" 이다. 그 사이에 ISR 이 **실행되지 못하게** 하면 IRQ 는 펜딩으로 남고, 펜딩은 WFI 를 바로 끝낸다. 켜는 건 WFI **뒤**. 켜는 순간 펜딩 ISR 이 실행된다.

</details>

<details>
<summary>힌트 5 — Part C: 거르고, 비교하고, 같으면 얕은 쪽</summary>

후보 Sleep → Stop → Standby 순서로 돌며 제약(RAM, 고속 클럭, 깨울 소스 전부 지원, 지연 한도, 오버헤드 ≤ idle)을 걸러 내고 `idle_charge` 가 **엄격히** 작을 때만 바꾼다. 손익분기는 두 직선 I_s(t − o_s) + I_r·o_s 와 I_d(t − o_d) + I_r·o_d 의 교점 — 정수 나눗셈은 올림 `(a + b − 1) / b`.

</details>

## 5. 풀이

### 0. 모드별 다섯 칸 — 면접에서 그리는 표

| | Run | Sleep (WFI, SLEEPDEEP=0) | Stop (SLEEPDEEP=1) | Standby (SLEEPDEEP=1) |
|---|---|---|---|---|
| 모델 전류 | 1.5 mA | 400 µA | 1.8 µA (RTC+LSE 포함) | 400 nA (RTC 포함) |
| 꺼지는 것 | — | CPU 클럭 | 코어 도메인 클럭 전부 (PLL·HSI·HSE), 레귤레이터 저전력 | 코어 도메인 전원 |
| 남는 것 | 전부 | 전부 (주변장치도 계속 동작) | SRAM, 레지스터, 주변장치 설정 | 백업 레지스터, RTC, (칩에 따라 SRAM2 옵션) |
| 깨움 소스 | — | NVIC 에 켜진 아무 IRQ | EXTI 라인: GPIO, RTC, LPUART, LPTIM, 비교기 | WKUP 핀, RTC 알람/웨이크업, IWDG, NRST |
| 복귀 지점 | — | WFI 다음 명령 (먼저 ISR) | WFI 다음 명령 (먼저 ISR), 클럭은 HSI16/MSI | **리셋 벡터** — `Reset_Handler` 부터 |
| 모델 웨이크업 | — | 1 µs | 5 µs | 50 µs + 재부팅 2 ms |

수치는 STM32L4 계열 데이터시트 표의 자릿수를 참고한 **이 레슨의 모델 값** 이다 (칩·전압·온도·클럭에 따라 몇 배씩 다르다 — 실제 설계는 그 칩 데이터시트의 "Current consumption" / "Low-power mode wakeup timings" 표를 쓴다). Stop 의 "TIM2/USART1 로는 못 깬다" 는 그 주변장치의 클럭이 멈춰 있기 때문이다 — 그래서 LPUART·LPTIM 처럼 32.768 kHz LSE 로 도는 저전력 주변장치가 따로 있다. 아키텍처 쪽은 단순하다: ARMv7-M 은 WFI/WFE 와 SCR.SLEEPDEEP 힌트("깨는 데 더 오래 걸려도 됨")만 정의하고, 무엇을 끌지는 **구현 정의(IMPLEMENTATION DEFINED)** 다 (B1.5.17, B3.2.3 SCR). Stop/Standby 의 구분은 칩 벤더의 PWR 블록(STM32 `PWR_CR1.LPMS`)이 한다.

### Part A — `pm_enter`: 무엇이 깨우고, 무엇이 남나

```c
for (int i = 0; i < n; i++) {
    if (ev[i].t_us < m->t_us || !(ev[i].src & mi->wake_mask)) continue;   /* 지난 것·못 깨우는 것 건너뜀 */
    uint64_t over = (uint64_t)mi->wake_us + mi->boot_us;
    m->q += (uint64_t)mi->i_nA * (ev[i].t_us - m->t_us) + (uint64_t)MODE[PM_RUN].i_nA * over;
    m->t_us = ev[i].t_us + over;
    if (!mi->ram_kept)   m->sram_var = 0u;      /* Standby: SRAM 소실 */
    if (!mi->hsclk_kept) m->hsclk_on = false;   /* Stop/Standby: PLL 다시 켜야 */
    if (mode == PM_STANDBY) m->sbf = true;      /* PWR SBF */
    m->resumed = mi->resume; m->woke_by = ev[i].src;
    return i;
}
return -1;
```

테스트가 확인하는 시나리오:

| 테스트 | 이벤트 | 결과 | 펌웨어 의미 |
|---|---|---|---|
| Sleep + TIM2 @100 | TIM2 | 깸, t = 101, RAM·클럭 그대로 | Sleep 은 아무 IRQ 로 깬다 |
| Stop + TIM2 @200, USART1 @300, GPIO @500 | GPIO (인덱스 2) | t = 505, `hsclk_on == false` | 일반 타이머·UART 는 Stop 에서 죽어 있다. 깬 뒤 `SystemClock_Config()` 재호출 |
| Standby + GPIO @1000, RTC @5000 | RTC | `RES_RESET_VECTOR`, SRAM = 0, 백업 = 42, SBF | 일반 GPIO 는 Standby 를 못 깨운다 (WKUP 핀만) |
| Stop + TIM2 만 | 없음 | -1 | **영영 안 깸** — 실제로는 워치독 리셋이나 전원 재투입까지 먹통 |

마지막 줄이 실무 버그 1순위다: "Stop 으로 바꿨더니 안 깨어나요" 의 원인은 거의 항상 깨움 소스가 그 모드에서 살아 있지 않은 것(EXTI 라인 미설정, RTC 웨이크업 플래그를 안 지움, 일반 UART 로 수신 대기)이다.

### Part A — duty cycle 과 평균 전류

한 주기 전하 Q = I_run·(t_act + o) + I_mode·(T − t_act − o), 평균 = Q / T. o = wake_us + boot_us. 테스트 숫자 하나를 손으로 풀어 보자 (Stop, T = 10 s, t_act = 1 ms):

| 구간 | 시간 | 전류 | 전하 [nA·µs] |
|---|---|---|---|
| 일 + 웨이크업 | 1000 + 5 µs | 1 500 000 nA | 1 507 500 000 |
| Stop | 9 998 995 µs | 1 800 nA | 17 998 191 000 |
| 합 / T | 10 000 000 µs | | **1 950.57 → 1951 nA** |

배터리 수명 = 225 mAh / 1.951 µA = 115 325 h = **4805 일 (13.2 년)**. 같은 노드를 Run 으로 두면 6 일. 단, CR2032 의 자가 방전·펄스 부하 용량 감소를 넣으면 크게 줄어든다 (연습 3: 이상적 30 년 → 18.4 년).

`demo_duty_table.c` 실제 출력 (T = 10 s):

```text
duty    t_act     | Run                    | Sleep                  | Stop                   | Standby
100%    10000000  | 1.500 mA    6 d       | 1.500 mA    6 d       | 1.500 mA    6 d       | 1.500 mA    6 d       |
10%     1000000   | 1.500 mA    6 d       | 510.00 uA   18 d      | 151.62 uA   62 d      | 150.67 uA   62 d      |
1%      100000    | 1.500 mA    6 d       | 411.00 uA   23 d      | 16.78 uA    559 d     | 15.70 uA    597 d     |
0.1%    10000     | 1.500 mA    6 d       | 401.10 uA   23 d      | 3.30 uA     7.8 y     | 2.21 uA     11.6 y    |
0.01%   1000      | 1.500 mA    6 d       | 400.11 uA   23 d      | 1.95 uA     13.2 y    | 857 nA      30.0 y    |
0.001%  100       | 1.500 mA    6 d       | 400.01 uA   23 d      | 1.82 uA     14.1 y    | 722 nA      35.6 y    |
```

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">평균 전류 vs duty cycle (T = 10 s, demo_duty_table.c 실제 출력, 로그-로그)</text>
  <rect x="90" y="40" width="510" height="250" fill="none" stroke="currentColor"/>
  <line x1="90" y1="290" x2="600" y2="290" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="84" y="294" fill="currentColor" text-anchor="end">100 nA</text>
  <line x1="90" y1="240" x2="600" y2="240" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="84" y="244" fill="currentColor" text-anchor="end">1 µA</text>
  <line x1="90" y1="190" x2="600" y2="190" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="84" y="194" fill="currentColor" text-anchor="end">10 µA</text>
  <line x1="90" y1="140" x2="600" y2="140" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="84" y="144" fill="currentColor" text-anchor="end">100 µA</text>
  <line x1="90" y1="90" x2="600" y2="90" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="84" y="94" fill="currentColor" text-anchor="end">1 mA</text>
  <line x1="90" y1="40" x2="600" y2="40" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="84" y="44" fill="currentColor" text-anchor="end">10 mA</text>
  <line x1="90" y1="40" x2="90" y2="290" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="90" y="306" fill="currentColor" text-anchor="middle">100%</text>
  <line x1="192" y1="40" x2="192" y2="290" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="192" y="306" fill="currentColor" text-anchor="middle">10%</text>
  <line x1="294" y1="40" x2="294" y2="290" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="294" y="306" fill="currentColor" text-anchor="middle">1%</text>
  <line x1="396" y1="40" x2="396" y2="290" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="396" y="306" fill="currentColor" text-anchor="middle">0.1%</text>
  <line x1="498" y1="40" x2="498" y2="290" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="498" y="306" fill="currentColor" text-anchor="middle">0.01%</text>
  <line x1="600" y1="40" x2="600" y2="290" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="600" y="306" fill="currentColor" text-anchor="middle">0.001%</text>
  <text x="345" y="324" fill="currentColor" text-anchor="middle">duty cycle (활성 비율) — 오른쪽일수록 오래 잔다</text>
  <polyline points="90,81 192,81 294,81 396,81 498,81 600,81" fill="none" stroke="currentColor" stroke-width="2"/>
  <rect x="86" y="77" width="8" height="8" class="f-mut" stroke="currentColor"/>
  <rect x="188" y="77" width="8" height="8" class="f-mut" stroke="currentColor"/>
  <rect x="290" y="77" width="8" height="8" class="f-mut" stroke="currentColor"/>
  <rect x="392" y="77" width="8" height="8" class="f-mut" stroke="currentColor"/>
  <rect x="494" y="77" width="8" height="8" class="f-mut" stroke="currentColor"/>
  <rect x="596" y="77" width="8" height="8" class="f-mut" stroke="currentColor"/>
  <text x="608" y="85" fill="currentColor">Run 1.5 mA</text>
  <polyline points="90,81 192,105 294,109 396,110 498,110 600,110" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3"/>
  <path d="M90 76 L95 85 L85 85 Z" class="f-warn" stroke="currentColor"/>
  <path d="M192 100 L197 109 L187 109 Z" class="f-warn" stroke="currentColor"/>
  <path d="M294 104 L299 113 L289 113 Z" class="f-warn" stroke="currentColor"/>
  <path d="M396 105 L401 114 L391 114 Z" class="f-warn" stroke="currentColor"/>
  <path d="M498 105 L503 114 L493 114 Z" class="f-warn" stroke="currentColor"/>
  <path d="M600 105 L605 114 L595 114 Z" class="f-warn" stroke="currentColor"/>
  <text x="608" y="114" fill="currentColor">Sleep 400 µA</text>
  <polyline points="90,81 192,131 294,179 396,214 498,225 600,227" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="2 3"/>
  <circle cx="90" cy="81" r="4.5" class="f-acc" stroke="currentColor"/>
  <circle cx="192" cy="131" r="4.5" class="f-acc" stroke="currentColor"/>
  <circle cx="294" cy="179" r="4.5" class="f-acc" stroke="currentColor"/>
  <circle cx="396" cy="214" r="4.5" class="f-acc" stroke="currentColor"/>
  <circle cx="498" cy="225" r="4.5" class="f-acc" stroke="currentColor"/>
  <circle cx="600" cy="227" r="4.5" class="f-acc" stroke="currentColor"/>
  <text x="608" y="237" fill="currentColor">Stop 1.8 µA</text>
  <polyline points="90,81 192,131 294,180 396,223 498,243 600,247" fill="none" stroke="currentColor" stroke-width="2"/>
  <path d="M90 76 L95 81 L90 86 L85 81 Z" class="f-ok" stroke="currentColor"/>
  <path d="M192 126 L197 131 L192 136 L187 131 Z" class="f-ok" stroke="currentColor"/>
  <path d="M294 175 L299 180 L294 185 L289 180 Z" class="f-ok" stroke="currentColor"/>
  <path d="M396 218 L401 223 L396 228 L391 223 Z" class="f-ok" stroke="currentColor"/>
  <path d="M498 238 L503 243 L498 248 L493 243 Z" class="f-ok" stroke="currentColor"/>
  <path d="M600 242 L605 247 L600 252 L595 247 Z" class="f-ok" stroke="currentColor"/>
  <text x="608" y="251" fill="currentColor">Standby 722 nA</text>
  <text x="10" y="332" fill="currentColor">CR2032 225 mAh 이상적 수명 (duty 0.01%): Run 6일 · Sleep 23일 · Stop 13.2년 · Standby 30년</text>
</svg>
```

읽는 법:

- duty 가 10 % 이상이면 어느 모드든 **일하는 전류가 지배** 한다 (Stop 151.6 µA ≈ 1.5 mA × 10 %). 이때 최적화 대상은 슬립 모드가 아니라 클럭 주파수·일하는 시간이다.
- duty 가 작아질수록 곡선이 **그 모드의 바닥 전류** 로 수렴한다. Sleep 은 400 µA 바닥에 붙어 CR2032 로 23 일이 한계 — "WFI 만 넣었는데 배터리가 한 달도 안 가요" 의 정체다. 저전력 설계 = 딥슬립(SLEEPDEEP).
- Stop 과 Standby 의 차이(1.8 µA vs 0.4 µA)는 duty 0.01 % 근처에서야 수명 2배가 된다. 그 대가는 SRAM 소실과 2 ms 재부팅이다.

### Part B — 레이스: 어디서 잃나

먼저 `demo_m4.c` 로 실제 명령을 보자 (CMSIS 와 같은 inline asm 을 직접 정의한 타깃 전용 파일):

<!-- code: demo_m4.c -->

Cortex-M4, `clang --target=thumbv7em-none-eabi -O2` 실제 출력 (지시어와 `@APP` 표시만 빼고 두 열로 배치, `;` 주석은 덧붙인 것):

```text
idle_naive:                          idle_correct:
    movw  r0, :lower16:g_flag            movw  r0, :lower16:g_flag
    movt  r0, :upper16:g_flag            cpsid i                  ; PRIMASK = 1
    ldr   r0, [r0]        ; ① 읽기        movt  r0, :upper16:g_flag
    cmp   r0, #0          ; ②             ldr   r0, [r0]
    it    ne                             cbz   r0, .LBB1_2
    bxne  lr              ; ③             cpsie i                  ; 플래그 있음 → 켜고 복귀
.LBB0_1:                                 bx    lr
    wfi                   ; ④ 잠듦      .LBB1_2:
    bx    lr                                 dsb   sy
                                             wfi                      ; 펜딩이면 즉시 통과
                                             isb   sy
                                             cpsie i                  ; ← 여기서 ISR 실행
                                             bx    lr
```

naive 의 창은 `ldr` 뒤 · `cmp` 뒤 · `bxne` 뒤 — **명령어 경계 3곳** 이다. 이 3곳 중 어디서든 IRQ 가 들어오면 ISR 은 `g_flag = 1` 을 쓰고 돌아오지만 r0 에는 이미 0 이 들어 있고, `wfi` 가 잠든다. 시뮬레이터의 `rd_flag()` 연산은 이 `ldr + cmp + 분기` 묶음 하나라서 창이 주입 지점 k = 1 하나로 보인다. M0+ 도 같은 모양이다 (`ldr r0,[r0]; cmp r0,#0; beq; ... wfi`). `movt` 가 `cpsid` 뒤로 옮겨진 것은 주소 계산이라 상관없고, `ldr` 은 `"memory"` 클로버 덕분에 `cpsid i` 와 `wfi` 사이에 머문다 (C-M14 에서 본 것과 같다).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">같은 순간(플래그 읽은 직후)에 IRQ 가 와도 결과가 다르다</text>
  <text x="10" y="56" fill="currentColor" font-weight="bold">naive</text>
  <rect x="80" y="38" width="110" height="28" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="88" y="57" fill="currentColor" font-family="monospace">ldr flag=0</text>
  <rect x="200" y="38" width="120" height="28" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="208" y="57" fill="currentColor">IRQ: ISR 실행</text>
  <rect x="330" y="38" width="80" height="28" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="338" y="57" fill="currentColor" font-family="monospace">wfi</text>
  <rect x="420" y="38" width="310" height="28" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="428" y="57" fill="currentColor">zz… flag=1 인데 잔다 → 1 s 뒤 RTC 에 깸</text>
  <text x="80" y="88" fill="currentColor">ISR 이 플래그를 세웠지만 r0 는 이미 0. 이벤트가 백스톱 주기(1 s)만큼 지연 = 웨이크업 유실</text>
  <line x1="10" y1="104" x2="730" y2="104" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="142" fill="currentColor" font-weight="bold">정석</text>
  <rect x="80" y="124" width="70" height="28" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="88" y="143" fill="currentColor" font-family="monospace">cpsid</text>
  <rect x="160" y="124" width="100" height="28" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="168" y="143" fill="currentColor" font-family="monospace">ldr flag=0</text>
  <rect x="270" y="124" width="120" height="28" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="278" y="143" fill="currentColor">IRQ → 펜딩만</text>
  <rect x="400" y="124" width="140" height="28" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="408" y="143" fill="currentColor">wfi: 펜딩 → 즉시</text>
  <rect x="550" y="124" width="70" height="28" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="558" y="143" fill="currentColor" font-family="monospace">cpsie</text>
  <rect x="630" y="124" width="100" height="28" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="638" y="143" fill="currentColor">ISR 실행</text>
  <text x="80" y="174" fill="currentColor">IRQ 가 자는 동안 와도: wfi 가 깬다 (PRIMASK=1 이라 ISR 은 아직) → cpsie 에서 ISR → main 이 플래그 처리</text>
  <rect x="10" y="190" width="720" height="98" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="212" fill="currentColor">ARMv7-M ARM B1.5.19 (DDI 0403C) 요지: WFI 는 "활성 예외를 선점할 우선순위의 비동기 예외" 에서 끝난다.</text>
  <text x="22" y="232" fill="currentColor">Note: PRIMASK 가 1 이고 FAULTMASK 가 0 이면, 활성 예외보다 그리고 BASEPRI 보다 높은 우선순위의 예외는</text>
  <text x="22" y="252" fill="currentColor">WFI 를 끝낸다. 실행 우선순위 이하인 예외는 무시된다. → PRIMASK 는 ISR 실행을 미룰 뿐 깨어남은 막지 않는다.</text>
  <text x="22" y="276" fill="currentColor">반대로 BASEPRI 로 막은 IRQ 는 WFI 를 깨우지 못한다 → idle 은 BASEPRI 가 아니라 PRIMASK 로 (Q2).</text>
</svg>
```

정답 코드로 `./solution -v` 를 돌린 실제 출력 (주입 지점 k 별 trace; `pend` = PRIMASK 때문에 펜딩으로 남음, `zz` = 잠듦, `RTC` = 아무것도 못 깨워 1 s 백스톱까지 잠):

```text
idle_naive         IRQ 없음: rF WFI zz RTC
  k=0 ok    IRQ rF
  k=1 LOST  rF IRQ WFI zz RTC
  k=2 ok    rF WFI zz IRQ
idle_enable_first  IRQ 없음: CPSID rF CPSIE WFI zz RTC
  k=0 ok    IRQ CPSID rF CPSIE
  k=1 LOST  CPSID pend rF CPSIE IRQ WFI zz RTC
  k=2 LOST  CPSID rF pend CPSIE IRQ WFI zz RTC
  k=3 LOST  CPSID rF CPSIE IRQ WFI zz RTC
  k=4 ok    CPSID rF CPSIE WFI zz IRQ
idle_correct       IRQ 없음: CPSID rF WFI zz RTC CPSIE
  k=0 ok    IRQ CPSID rF CPSIE
  k=1 ok    CPSID pend rF WFI CPSIE IRQ
  k=2 ok    CPSID rF pend WFI CPSIE IRQ
  k=3 ok    CPSID rF WFI zz pend CPSIE IRQ
  k=4 ok    CPSID rF WFI zz RTC CPSIE IRQ
```

- **naive**: k = 1 (확인 직후) 에서만 잃는다. 창이 좁을수록 드물게 나므로 더 위험하다 — "가끔 버튼 반응이 1초 늦어요" 같은 리포트로 온다.
- **WFI 전에 켜는 고침** (`cpsid; f = flag; cpsie; if (!f) wfi;`): 오히려 나빠졌다 (3곳). 확인할 때 막았어도 WFI 전에 풀면, 막혀 있던 동안 쌓인 펜딩 ISR 이 cpsie 에서 실행되고 → 이미 읽은 f = 0 으로 잠든다. **확인과 잠들기 둘 다** 막힌 구간 안에 있어야 한다.
- **정석**: 모든 지점에서 0. k = 1, 2 는 "펜딩이 WFI 를 즉시 통과시킴" (잠들지 않음), k = 3 은 "자는 동안 도착 → PRIMASK=1 인데도 깸", k = 4 는 IRQ 가 백스톱 뒤에 온 정상 경우다.

왜 이게 동작하나 — 핵심은 **ISR 실행** 과 **깨어남** 을 분리한 것이다. PRIMASK=1 은 ISR 이 끼어드는 것을 막아 플래그 확인과 잠들기 사이를 원자적으로 만들고, 그 사이 도착한 IRQ 는 NVIC 펜딩 비트에 남는다. WFI 의 wake-up 조건은 "선점할 만한 예외" 라는 상태 조건이라 펜딩이 남아 있으면 바로 끝난다. B1.5.19 에는 "WFI wake-up events are recognized after the WFI instruction is issued" 라는 문장도 있는데, 펜딩 예외는 WFI 를 실행한 뒤에도 계속 "선점할 만한 상태" 로 남아 있으므로 이 문장과 충돌하지 않는다고 읽는다 — FreeRTOS·Zephyr 의 idle 코드가 바로 이 동작에 의존한다 (아래). 이 맥에서 실칩 검증은 하지 않았다. 또 "or earlier if the implementation chooses" — WFI 는 아무 이유 없이 일찍 끝날 수도 있으므로(spurious wakeup, B1.5.17 Note) **idle 은 항상 루프 안에서** 부른다.

#### WFE 와 이벤트 레지스터

ARMv7-M ARM B1.5.18 (DDI 0403C, 원문 확인):

- WFE wake-up 이벤트: 어느 프로세서든 `SEV` 실행 / **SEVONPEND 가 켜져 있을 때 펜딩 상태로 들어가는 예외** / 활성 예외를 선점하는 비동기 예외 / 디버그 이벤트.
- 이벤트 레지스터(1비트): 리셋 값 UNKNOWN, **wake-up 이벤트 또는 예외 복귀(exception return) 실행으로 set**, WFE 로만 clear, 소프트웨어가 직접 읽고 쓸 수 없다.
- WFE: 레지스터가 set 이면 지우고 즉시 반환, clear 면 잘 수 있다. "wake-up 이벤트는 WFE 이전에 일어날 수 있고, 소프트웨어는 헛깸(spurious wake-up)을 견뎌야 한다."

`./solution -v` 출력 발췌 (`←` 설명은 덧붙인 것):

```text
idle_wfe_loop      IRQ 없음: rF WFE zz RTC rF WFE zz RTC rF WFE zz RTC
  k=1 ok    rF IRQ WFE rF            ← ISR(선점 예외 + 예외 복귀)이 이벤트 레지스터를 세워 WFE 가 그냥 통과
  k=2 ok    rF WFE zz IRQ rF
idle_atomic_wfe    IRQ 없음: CPSID rF WFE zz RTC CPSIE
  k=1 ok    CPSID pend rF WFE CPSIE IRQ     ← SEVONPEND: 펜딩 전이가 이벤트
  k=3 ok    CPSID rF WFE zz pend CPSIE IRQ
```

그래서 `while (!flag) __WFE();` 는 인터럽트를 켠 채로도 안전하다 — 확인과 WFE 사이에 ISR 이 돌면 이벤트 레지스터가 서 있으니 WFE 가 그냥 통과하고 루프가 플래그를 다시 본다. 대신 **첫 WFE 가 헛통과** 할 수 있다 (리셋 직후 UNKNOWN, 혹은 예전 ISR 의 흔적): 테스트 `"rF WFE rF WFE zz IRQ rF"`. "한 번의 `__WFE()` 로 잔다" 고 가정한 코드는 깨진다 — STM32 HAL 의 WFE 진입이 `__SEV(); __WFE(); __WFE();` 로 레지스터를 먼저 비우는 이유다 (HAL 소스 기준, 이 맥에서 미확인). 테스트 `SEV WFE WFE zz RTC` 가 그 관용구다.

PRIMASK 를 건 채 WFE 를 쓰려면 SEVONPEND 가 **필수** 다. 마스크된 IRQ 는 "선점하는" 예외가 아니므로, SEVONPEND 가 없으면 이벤트가 생기지 않아 WFE 가 계속 잔다 (시작 코드의 `idle_atomic_wfe` 가 k = 1, 2 에서 그렇게 FAIL 한다). B1.5.17 도 "Inactive → Pending 전이를 wake-up 이벤트로 설정할 수 있다 — 마스크된 인터럽트로 WFE 를 재개할 수 있게" 라고 쓴다. Zephyr 의 `arch_cpu_atomic_idle()` (arch/arm/core/cortex_m/cpu_idle.c) 가 정확히 이 구조다: 부팅 때 `SCB->SCR = SCB_SCR_SEVONPEND_Msk;` 를 한 번 쓰고, PRIMASK 를 건 채 `__WFE()` 후 `arch_irq_unlock(key)`. 주석: "Lock PRIMASK while sleeping: wfe will still get interrupted by incoming interrupts but the CPU will not service them right away." (GitHub main 소스 기준.)

#### 실제 RTOS 는 이렇게 한다

FreeRTOS V11.3.2 `portable/GCC/ARM_CM4F/port.c` 의 `vPortSuppressTicksAndSleep()` (tickless idle):

```c
/* port.c:600-604 */
/* Enter a critical section but don't use the taskENTER_CRITICAL()
 * method as that will mask interrupts that should exit sleep mode. */
__asm volatile ( "cpsid i" ::: "memory" );
__asm volatile ( "dsb" );
__asm volatile ( "isb" );
/* port.c:608 */  if( eTaskConfirmSleepModeStatus() == eAbortSleep ) { ... cpsie i ... }
/* port.c:669-685 */
configPRE_SLEEP_PROCESSING( xModifiableIdleTime );
if( xModifiableIdleTime > 0 ) {
    __asm volatile ( "dsb" ::: "memory" );
    __asm volatile ( "wfi" );
    __asm volatile ( "isb" );
}
configPOST_SLEEP_PROCESSING( xExpectedIdleTime );
/* Re-enable interrupts to allow the interrupt that brought the MCU
 * out of sleep mode to execute immediately. */
__asm volatile ( "cpsie i" ::: "memory" );
```

`eTaskConfirmSleepModeStatus()` 가 이 레슨의 `rd_flag()` 다 — "그 사이 태스크가 깨어났으면(ISR 이 세마포어를 줬으면) 잠들지 말라". 그리고 첫 주석이 Q2 의 답이다: `taskENTER_CRITICAL()` 은 Cortex-M3/M4 에서 BASEPRI 로 막는데, BASEPRI 로 막힌 IRQ 는 WFI 를 깨우지 못하므로 일부러 `cpsid i` (PRIMASK) 를 쓴다. Zephyr `arch_cpu_idle()` 도 `__disable_irq(); __set_BASEPRI(0); __ISB();` 뒤 `__WFI()`, 그리고 `__enable_irq()`.

`dsb` 를 WFI 앞에 두는 이유: 직전의 메모리 쓰기(주변장치 플래그 클리어, SCR 쓰기)가 끝난 뒤 잠들게 하려는 관례다. CMSIS `__WFI()` 자체는 `wfi` 한 줄이라 필요하면 직접 넣는다.

#### 호스트에서 진짜로 — `pause()` vs `sigsuspend()`

POSIX 에도 똑같은 레이스와 똑같은 해법이 있다. `if (!flag) pause();` 는 확인과 `pause` 사이에 시그널이 오면 다음 시그널까지 잔다. 해법 `sigsuspend(&old)` 는 **"마스크를 풀고 잠들기" 를 한 번의 원자적 호출** 로 한다 — Cortex-M 은 "막은 채로 잠들고, 펜딩이 깨우고, 깬 뒤 푼다" 로 같은 효과를 낸다. `demo_sigsuspend_race.c` 는 창을 20 µs 로 일부러 넓히고 IRQ(SIGUSR1)를 0–60 µs 사이 무작위(고정 시드)에 보낸다. 2 ms 안에 main 이 안 깨면 보조 스레드가 백스톱(SIGUSR2)을 보낸다.

<!-- code: demo_sigsuspend_race.c -->

실제 출력 3회 (Apple M6, P 2 + E 4 코어, Apple clang 21, load avg ~5):

```text
window=20 us, IRQ delay 0..60 us, backstop=2 ms
naive      trials=2000  lost(backstop needed)= 597 (29.9%)  total 1288.6 ms
sigsuspend trials=2000  lost(backstop needed)=   0 (0.0%)  total 81.7 ms
naive      trials=2000  lost(backstop needed)= 596 (29.8%)  total 1285.5 ms
sigsuspend trials=2000  lost(backstop needed)=   0 (0.0%)  total 80.0 ms
naive      trials=2000  lost(backstop needed)= 593 (29.6%)  total 1272.2 ms
sigsuspend trials=2000  lost(backstop needed)=   0 (0.0%)  total 79.7 ms
```

유실률 ≈ 30 % 는 "창 20 µs / 도착 구간 60 µs = 33 %" 와 거의 맞는다 (조금 낮은 것은 시그널 전달 지연 때문으로 추정). TSan(`-fsanitize=thread`)으로도 돌려 보고 없이 같은 결과를 얻었다. 스레드 쪽의 같은 구조가 OS-B06 의 `while (!pred) pthread_cond_wait(&c, &m);` 다 — 뮤텍스가 "확인 ~ 잠들기" 를 원자적으로 만든다.

### Part C — 슬립 매니저: 제일 깊은 모드가 정답은 아니다

```c
for (int m = PM_SLEEP; m < PM_COUNT; m++) {
    if (r->need_ram && !mi->ram_kept) continue;
    if (r->need_hsclk && !mi->hsclk_kept) continue;
    if ((mi->wake_mask & r->wake_src) != r->wake_src) continue;
    if ((uint64_t)mi->wake_us + mi->boot_us > r->max_wake_us) continue;
    uint64_t q = idle_charge((pmode_t)m, r->idle_us);      /* 오버헤드 > idle 이면 UINT64_MAX */
    if (q < best_q) { best_q = q; best = (pmode_t)m; }
}
```

| 요청 | 선택 | 이유 |
|---|---|---|
| idle 10 s, RAM 필요, RTC | Stop | Standby 는 SRAM 소실 |
| idle 10 s, RAM 불필요 | **Standby** | 손익분기 2.19 s 초과 |
| idle 1 s, RAM 불필요 | Stop | 1 s 로는 2 ms 재부팅 비용을 못 뽑는다 |
| idle 10 s, 지연 한도 100 µs | Stop | Standby 2050 µs 초과 |
| RTC + 일반 GPIO 버튼 | Stop | 일반 GPIO 는 Standby 를 못 깨움 |
| RTC + WKUP 핀 버튼 | Standby | 버튼을 WKUP 핀에 달면 가능 — **하드웨어 설계 단계의 결정** |
| USART1 수신 중 50 ms | Sleep | 고속 클럭이 필요한 주변장치 동작 중 |
| idle 3 µs | Sleep | Stop 웨이크업(5 µs)보다 짧다 |
| idle 0 | Run | 잘 틈이 없다 |

손익분기: I_s(t − o_s) + I_r·o_s = I_d(t − o_d) + I_r·o_d 를 t 에 대해 풀면

t* = ((I_r − I_d)·o_d − (I_r − I_s)·o_s) / (I_s − I_d)

Stop → Standby: ((1 500 000 − 400)·2050 − (1 500 000 − 1800)·5) / 1400 = 3 066 689 000 / 1400 = 2 190 492.1 → **2 190 493 µs**. Sleep → Stop 은 17 µs. 테스트가 t* 에서 Standby ≤ Stop, t* − 1 에서 Standby > Stop 을 확인한다. `demo_duty_table.c` 표 2 (일 5 ms 고정, 깨는 주기 T):

```text
T             Sleep       Stop    Standby  더 나은 쪽
10 ms        950110     751649    1057618  Stop
100 ms       455011      76785     106122  Stop
1 s          405501       9298      10972  Stop
2 s          402751       5549       5686  Stop
2.5 s        402200       4799       4629  Standby
10 s         400550       2550       1457  Standby
1 min        400092       1925        576  Standby
1 h          400002       1802        403  Standby
```

이 계산이 Zephyr PM 의 devicetree 속성 `min-residency-us` (이보다 짧게 머물면 그 상태로 가지 않음)와 `exit-latency-us` 다. 정책 `pm_policy_next_state()` 가 다음 타임아웃까지 남은 시간과 이 두 값으로 상태를 고른다. FreeRTOS 는 `configEXPECTED_IDLE_TIME_BEFORE_SLEEP` (기본 2 tick) 보다 짧은 idle 에는 tickless 진입 자체를 하지 않는다.

### 호스트 모델 ↔ 실제 API

| 이 레슨 | CMSIS-Core | STM32 HAL (이름 기준, 버전별 확인) | FreeRTOS | Zephyr |
|---|---|---|---|---|
| `CPSID()` / `CPSIE()` | `__disable_irq()` / `__enable_irq()` | — | tickless 안의 `cpsid i` (port.c:602) | `__disable_irq()` in `arch_cpu_idle` |
| `WFI()` | `__WFI()` | `HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI)` | `portSUPPRESS_TICKS_AND_SLEEP()` → `vPortSuppressTicksAndSleep` | `k_cpu_idle()` |
| `WFE()` + `sevonpend` | `__WFE()`, `SCB->SCR \|= SCB_SCR_SEVONPEND_Msk` | `PWR_SLEEPENTRY_WFE`, `HAL_PWR_EnableSEVOnPend()` | — | `k_cpu_atomic_idle(key)` |
| `SEV()` | `__SEV()` | — | (SMP 포트) | — |
| `PM_STOP` | `SCB->SCR \|= SCB_SCR_SLEEPDEEP_Msk` + PWR 설정 | `HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI)` (L4) | `configPRE_SLEEP_PROCESSING()` 에서 | `pm_state` (SoC 별 매핑) |
| `PM_STANDBY`, `sbf`, `bkp_dr0` | SLEEPDEEP + LPMS | `HAL_PWR_EnterSTANDBYMode()`, `__HAL_PWR_GET_FLAG(PWR_FLAG_SB)`, `HAL_RTCEx_BKUPWrite()` | — | `sys_poweroff()` |
| SLEEPONEXIT (연습 1) | `SCB->SCR \|= SCB_SCR_SLEEPONEXIT_Msk` | `HAL_PWR_EnableSleepOnExit()` | — | — |
| `pm_choose` | — | — | `configEXPECTED_IDLE_TIME_BEFORE_SLEEP`, `eTaskConfirmSleepModeStatus()` | `pm_policy_next_state()`, `min-residency-us` |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 64/64 passed
./solution -v                                                                  # 주입 표 (5. 풀이)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 64/64, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `if (!flag) __WFI();` | 확인 ~ WFI 사이(명령어 경계 3곳)에 온 IRQ 의 플래그를 못 보고 잔다 | `__disable_irq(); if (!flag) __WFI(); __enable_irq();` |
| `__disable_irq(); f = flag; __enable_irq(); if (!f) __WFI();` | 확인만 막고 잠들기는 안 막음. 펜딩이 enable 에서 실행된 뒤 잔다 (3곳 유실) | 켜는 것은 WFI **뒤** |
| idle 을 `taskENTER_CRITICAL()` / BASEPRI 로 막고 WFI | BASEPRI 로 막힌 IRQ 는 WFI 를 깨우지 못한다 (B1.5.19 Note) | PRIMASK(`cpsid i`) + `__set_BASEPRI(0)` (Zephyr, FreeRTOS tickless) |
| PRIMASK + WFE 인데 SEVONPEND 없음 | 마스크된 IRQ 는 WFE wake-up 이벤트가 아님 → 계속 잠 | 부팅 때 `SCB->SCR \|= SCB_SCR_SEVONPEND_Msk` |
| `__WFE();` 한 번이면 잔다고 가정 | 이벤트 레지스터가 서 있으면 헛통과 (리셋 값 UNKNOWN) | `while (!cond) __WFE();` 또는 `__SEV(); __WFE(); __WFE();` |
| idle 을 루프 없이 한 번만 | WFI/WFE 는 힌트 — 일찍/헛되이 끝날 수 있다 (B1.5.17 Note, 디버그 이벤트) | `for (;;) { idle(); 처리(); }` |
| Stop 에서 깨운 뒤 바로 UART 송신 | SYSCLK 가 HSI16 으로 바뀌어 BRR 이 안 맞음 → 115200 이 23054 baud (연습 4) | 깬 직후 `SystemClock_Config()` |
| Stop 진입 전 깨움 소스 확인 안 함 | TIM2/USART1 은 Stop 에서 멈춤 → 영영 안 깸 | EXTI/RTC/LPUART/LPTIM 으로 깨우기 |
| Standby 에서 SRAM 변수로 상태 유지 | 깨면 리셋 — .bss 는 0, .data 는 초기값 | 백업 레지스터/RTC, SBF 로 웜 부트 판별 (연습 2) |
| 짧은 idle 에도 무조건 가장 깊은 모드 | 웨이크업·재부팅 비용이 절약보다 큼 (Stop→Standby 손익분기 2.19 s) | `pm_choose` 처럼 min-residency 로 고른다 |
| Sleep(WFI)만 넣고 "저전력 구현 완료" | Sleep 바닥 전류(모델 400 µA)는 CR2032 로 23일 | SLEEPDEEP 모드 + 주변장치 클럭 게이팅 + 미사용 GPIO analog |
| 깨운 뒤 RTC/EXTI 펜딩 플래그를 안 지움 | 다음 Stop 진입이 즉시 끝나거나(바로 깸) 다음 이벤트를 놓친다 (칩별 동작, 문서 기준) | ISR 에서 플래그 클리어 (W1C, BM-B04) |

## 8. 면접 Q&A

<details>
<summary>Q1. "조건 확인 후 WFI" 사이에 인터럽트가 오면 어떻게 되나요? 어떻게 막죠?</summary>

ISR 은 실행되어 플래그를 세우지만 메인은 이미 플래그를 0 으로 읽었으므로 WFI 로 잠든다. 다음 아무 인터럽트(RTC tick 등)까지 이벤트가 방치되고, 그런 인터럽트가 없으면 영원히. 고침: `__disable_irq(); if (!flag) __WFI(); __enable_irq();`. 인터럽트를 끈 채 확인하므로 사이에 ISR 이 끼어들 수 없고, 그 사이 온 IRQ 는 펜딩으로 남는데 **펜딩 IRQ 는 PRIMASK=1 이어도 WFI 를 끝낸다** (ARMv7-M ARM B1.5.19). 깬 뒤 `__enable_irq()` 에서 ISR 이 실행되고 루프가 플래그를 처리한다. 꼬리: "어떻게 테스트하나?" → 시퀀스의 모든 경계에 IRQ 를 주입하는 시뮬레이션(이 레슨), 실칩에서는 GPIO 토글로 창을 넓혀 놓고 신호 발생기로 IRQ 타이밍을 스윕하거나 디버거 없이 장시간 응답 지연 히스토그램을 본다.

</details>

<details>
<summary>Q2. PRIMASK=1 인데 WFI 는 왜 깨나요? BASEPRI 로 막았다면?</summary>

PRIMASK 는 "예외를 **받아들이는**(take)" 것을 막는 마스크이고, WFI 의 깨움 조건은 "활성 예외를 선점할 우선순위의 예외가 있는가" 다. B1.5.19 Note: PRIMASK 가 set 이고 FAULTMASK 가 clear 면, 활성 예외보다 그리고 BASEPRI 보다 높은 우선순위의 예외는 WFI 를 끝내고, 실행 우선순위 이하인 것은 무시된다. 그러니 BASEPRI 로 막은 IRQ (우선순위 값 ≥ BASEPRI) 는 WFI 를 깨우지 **못한다**. FreeRTOS tickless 가 "taskENTER_CRITICAL() 을 쓰지 말라 — sleep 을 끝내야 할 인터럽트까지 막는다" 고 주석을 달고 `cpsid i` 를 쓰는 이유(port.c:600–602), Zephyr `arch_cpu_idle()` 이 `__disable_irq()` 후 `__set_BASEPRI(0)` 하는 이유다. 꼬리: "깬 뒤 ISR 은 언제?" → `cpsie i` 직후. 그 전까지 WFI 다음 명령들은 ISR 없이 실행된다 (FreeRTOS 는 여기서 SysTick 보정 계산을 한다).

</details>

<details>
<summary>Q3. WFI 와 WFE 의 차이는? WFE 는 언제 쓰나요?</summary>

WFI 는 인터럽트(정확히는 선점할 만한 예외)를 기다린다. WFE 는 1비트 **이벤트 레지스터** 를 본다: set 이면 지우고 즉시 통과, clear 면 잔다. 레지스터는 SEV(다른 코어 포함), SEVONPEND 상태의 펜딩 전이, 선점 예외, 그리고 **예외 복귀** 로 set 된다 (B1.5.18). 용도: ① 멀티코어 스핀락 — 락 해제 쪽이 `SEV`, 대기 쪽이 `WFE` 로 전력 절약 (RP2040, 듀얼 코어 STM32H7) ② 마스크된 인터럽트로 깨우기(SEVONPEND, Zephyr atomic idle) ③ 주변장치 이벤트 신호 (nRF52 의 이벤트 모델에서 `__WFE()` 를 쓰는 idle 예제가 흔하다). 함정: 이벤트가 미리 서 있으면 헛통과하므로 반드시 조건 루프 안에서 쓴다. WFE 깨어남이 이벤트 레지스터를 지우는지는 구현 정의다 — 루프가 둘 다 견딘다.

</details>

<details>
<summary>Q4. Sleep, Stop, Standby 의 차이를 "깨면 어디서부터 실행되나" 중심으로 설명해 보세요.</summary>

Sleep: CPU 클럭만 멈추고 전부 남는다. 깨면 ISR 을 실행하고 WFI 다음 명령으로 돌아온다. Stop: 코어 도메인 클럭(PLL/HSE/HSI)이 꺼지고 SRAM·레지스터는 남는다. 깨면 역시 ISR → WFI 다음 명령이지만, SYSCLK 가 HSI16/MSI 로 바뀌어 있어 PLL 을 다시 켜야 한다 (안 하면 UART baud 가 1/5 — 연습 4). 깨울 수 있는 것은 EXTI 라인(GPIO, RTC, LPUART, LPTIM)뿐. Standby: 코어 도메인 전원이 꺼진다. 깨면 **리셋 벡터부터** — startup 이 .data 복사·.bss 제로화를 다시 하고, 펌웨어는 PWR SBF 플래그로 "Standby 복귀" 를 알아내 백업 레지스터/RTC 에서 상태를 복원한다. 깨움은 WKUP 핀, RTC, IWDG, NRST. 꼬리: "Shutdown 은?" → 더 깊다(STM32L4: 수십 nA 대, RTC 만 LSE 로 유지 가능, BOR 도 꺼짐 — 데이터시트 기준). nRF52 의 System OFF 도 리셋으로 깬다.

</details>

<details>
<summary>Q5. 10초마다 1 ms 일하는 노드의 CR2032 수명을 계산해 보세요. Standby 로 바꾸면 항상 이득인가요?</summary>

평균 = (I_run·(1 ms + 웨이크업) + I_sleep·나머지) / 10 s. 모델 값으로 Stop: (1.5 mA × 1.005 ms + 1.8 µA × 9.999 s) / 10 s ≈ 1.95 µA → 225 mAh / 1.95 µA ≈ 115 000 h ≈ 13 년 (이상적). Standby 는 857 nA → 30 년. 하지만 Standby 는 깰 때마다 2 ms 재부팅(1.5 mA)을 낸다. 손익분기 t* = ((I_r − I_d)·o_d − (I_r − I_s)·o_s) / (I_s − I_d) ≈ 2.19 s — 이보다 자주 깨면 Stop 이 낫다 (표 2: 1 s 주기에서 Stop 9.3 µA vs Standby 11.0 µA). 그리고 RAM 상태가 필요하면(필터 상태, 송신 대기 샘플) 애초에 불가. 꼬리: "30 년이 현실적?" → 아니다. 자가 방전(약 1 %/년 → 등가 약 256 nA), 사용 가능 용량(펄스 부하·저온), 그리고 무선 송신 피크가 지배한다 — 연습 3 에서 18.4 년.

</details>

<details>
<summary>Q6. SLEEPONEXIT 는 무엇이고, 쓰면 무엇이 달라지나요?</summary>

SCR.SLEEPONEXIT=1 이면 스레드 모드로 돌아가는 예외 복귀 때 복귀하지 않고 바로 잔다 (B1.5.17: "exception return is not performed", 다음 예외는 tail-chain 처럼 동작). 모든 일을 ISR 에서 하는 인터럽트 구동(interrupt-driven) 앱에서 스택 pop/push 와 메인 루프 왕복을 없앤다 — 연습 1 모델: 이벤트 5개에 stack 5회/unstack 5회 → 2회/1회. 함정: **main 루프 코드가 실행되지 않는다**. main 에서 할 일이 생기면 ISR 이 `SCB->SCR &= ~SCB_SCR_SLEEPONEXIT_Msk` 로 끄고 돌아가야 한다. 또 B1.5.17 Note: SLEEPONEXIT 상태는 PRIMASK 가 set 일 때 BASEPRI 보다 높은 예외로 반드시 벗어난다고 보장되지 않는다 — WFI 와 다르다.

</details>

<details>
<summary>Q7. 보드에서 잰 Stop 전류가 데이터시트보다 100배 큽니다. 무엇을 의심하나요?</summary>

① 디버거 연결: DBGMCU_CR 의 DBG_STOP/DBG_STANDBY 가 켜져 있으면 디버그를 위해 클럭을 유지한다 (그리고 디버거를 떼면 Stop 진입 후 SWD 연결이 끊긴다). 측정은 프로브를 떼고. ② 떠 있는(floating) 입력 GPIO — 입력 버퍼 누설. 미사용 핀은 analog 모드. ③ 외부 부품: 풀업 저항으로 흐르는 전류, 센서/레귤레이터 대기 전류, LED. ④ 실제로는 Stop 에 못 들어감: 펜딩 플래그 때문에 즉시 깨어나 Run/Sleep 을 반복 (GPIO 토글로 확인). ⑤ 주변장치 클럭이 켜진 채(Sleep 전류). ⑥ 측정 장비: µA 범위의 burden voltage, 순간 피크를 못 잡는 멀티미터 — 전류 프로파일러(Nordic PPK2, Joulescope 등)로 파형을 본다.

</details>

<details>
<summary>Q8. RTOS 의 tickless idle 은 이 레이스를 어떻게 처리하나요?</summary>

FreeRTOS `vPortSuppressTicksAndSleep()` (V11.3.2 ARM_CM4F port.c:590–): `cpsid i` → `eTaskConfirmSleepModeStatus()` 로 "그 사이 태스크가 준비됐거나 컨텍스트 스위치가 펜딩인가" 확인(= 플래그 확인) → 아니면 SysTick 을 멈추고 예상 idle 만큼 reload 를 다시 계산 → `dsb; wfi; isb` → `cpsie i` 로 깨운 인터럽트를 실행 → 다시 `cpsid i` 하고 SysTick 이 흐른 tick 수를 계산해 `vTaskStepTick()` 로 보정. 즉 이 레슨의 정석 패턴 + 시간 보정이다. `configPRE_SLEEP_PROCESSING()` 에서 Stop 진입(SLEEPDEEP, 저전력 타이머로 SysTick 대체)을 끼워 넣는 것이 흔한 구조다. 태스크 쪽 "이벤트 유실" (바이너리 세마포어 합쳐짐 등)은 RTOS-B08, ISR↔메인 공유 데이터는 C-M14.

</details>

## 9. 추가 연습

### 연습 1 — SLEEPONEXIT 로 몇 번의 스택 push/pop 이 줄어드나

이벤트 5개 중 4번째만 "main 에서 처리할 일" 을 만든다. SLEEPONEXIT 가 없을 때와 있을 때 스택 push / pop 횟수와 trace 를 구하라. ISR 이 main 에 일을 넘기려면 무엇을 해야 하나?

<details>
<summary>답 보기</summary>

없을 때: 매 IRQ 마다 stack + unstack + 스레드의 `WFI` 루프 → 5/5, trace `IRQ WFI IRQ WFI IRQ WFI IRQ main WFI IRQ WFI`. 있을 때: 처음 한 번 stack, ISR 이 끝나면 복귀 없이 잠(`zz`), 다음 IRQ 는 이미 쌓인 문맥 위에서 tail-chain → 4번째 ISR 이 `SCB->SCR &= ~SLEEPONEXIT` 로 끄고 나서야 unstack → main 이 처리 후 다시 켜고 WFI → 다섯 번째 IRQ 에서 다시 stack. 결과 2/1, trace `IRQ zz IRQ zz IRQ zz IRQ main WFI IRQ zz`. 할 일이 전혀 없으면 스레드는 한 번도 안 돈다 (stack 1, unstack 0).

</details>

### 연습 2 — Standby 웜 부트: SBF 와 백업 레지스터

Standby 에서 깰 때마다 리셋이 일어난다. 샘플 카운터를 깨어날 때마다 이어서 세도록 `node_boot()` 를 작성하라. 전원 투입(콜드 부트) 때 백업 레지스터에 남은 쓰레기는?

<details>
<summary>답 보기</summary>

startup 이 .bss 를 0 으로 만든 뒤 main 에서 `if (SBF) { count = BKP0R; PWR_SCR = CSBF | CWUF; return BOOT_FROM_STANDBY; } else { BKP0R = 0; return BOOT_COLD; }`. Standby 진입 직전에 `BKP0R = count`. 플래그를 지우지 않으면 다음 핀 리셋도 "Standby 복귀" 로 오인한다 — 테스트 마지막 줄. 콜드 부트 때 백업 도메인 값은 신뢰하지 말고 초기화한다 (또는 매직 넘버 + CRC 로 유효성 확인 — BM-A09 의 .noinit 크래시 덤프와 같은 발상). STM32L4 의 비트 이름은 `PWR_SR1.SBF` / `PWR_SCR.CSBF` (RM0351 기준, 이 맥에서 미확인), F4 는 `PWR_CSR.SBF` / `PWR_CR.CSBF`.

</details>

### 연습 3 — 배터리 수명 디레이팅

CR2032 225 mAh, 사용 가능 80 %, 자가 방전 1 %/년. Standby 노드(857 nA)와 Stop 노드(1951 nA)의 수명은?

<details>
<summary>답 보기</summary>

자가 방전 등가 전류 = 225 mAh × 1 % / 8760 h = 256 nA — Standby 노드 평균의 30 % 에 해당한다. 수명 = 0.8 × 225e6 nA·h / (857 + 256) nA / 24 = **6738 일 (18.4 년)**, Stop 노드는 3398 일 (9.3 년). 이상적 10939 일(30 년)에서 38 % 줄었다. 교훈: 평균이 1 µA 아래로 내려가면 배터리 자체의 자가 방전과 저온·펄스 부하 용량 감소가 펌웨어 최적화보다 커진다. 실제 수치는 배터리 데이터시트(제조사별)로.

</details>

### 연습 4 — Stop 에서 깬 뒤 클럭을 복원하지 않으면

USART1 BRR 을 PCLK 80 MHz, 115200 baud 로 맞춰 두었다. Stop 에서 깬 뒤 SYSCLK 가 HSI16 (16 MHz) 인 채로 보내면 실제 baud 와 오차는?

<details>
<summary>답 보기</summary>

BRR = round(80e6 / 115200) = 694 → 정상 115273 baud (+0.06 %). 16 MHz 에서는 16e6 / 694 = 23054 baud (−79.98 %) — 받는 쪽은 쓰레기 문자를 본다. UART 는 보통 ±2–3 % 이내여야 한다. 해법: 깬 직후 `SystemClock_Config()` (PLL 재설정·락 대기) 또는 STOPWUCK 로 깨는 클럭을 고르고 그 클럭 기준 BRR 을 쓴다. 저전력 통신은 LSE 로 도는 LPUART(9600 baud 등)로 Stop 중에도 수신해 깨운다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 20/20 passed
```

## 10. 핵심 정리

- 모드는 다섯 칸으로 외운다: 전류 · 웨이크업 지연 · 남는 것 · 깨움 소스 · 복귀 지점. Sleep/Stop 은 WFI 다음 명령(Stop 은 클럭 복원), Standby 는 **리셋 벡터** (SBF + 백업 레지스터).
- 평균 전류 = (I_run·(일 + 웨이크업) + I_sleep·나머지) / T. duty 가 크면 일 전류가, 작으면 슬립 바닥 전류가 지배. Sleep(WFI)만으로는 저전력이 아니다.
- 더 깊은 모드는 손익분기 이후에만 이득: t* = ((I_r − I_d)·o_d − (I_r − I_s)·o_s) / (I_s − I_d). 모델 Stop→Standby 2.19 s. = Zephyr `min-residency-us`.
- `if (!flag) __WFI();` 는 확인 ~ WFI 사이 IRQ 를 잃는다. 정석 `__disable_irq(); if (!flag) __WFI(); __enable_irq();` — PRIMASK 는 ISR 실행만 미루고, 펜딩 IRQ 는 WFI 를 끝낸다 (B1.5.19). WFI 전에 켜면 다시 깨진다.
- BASEPRI 로 막은 IRQ 는 WFI 를 못 깨운다 → idle 은 PRIMASK (FreeRTOS tickless `cpsid i`, Zephyr `__set_BASEPRI(0)`).
- WFE 는 이벤트 레지스터: SEV / SEVONPEND 펜딩 / 선점 예외 / 예외 복귀가 set, WFE 만 clear. 헛통과가 있으니 항상 루프. PRIMASK + WFE 에는 SEVONPEND 필수 (Zephyr atomic idle).
- SLEEPONEXIT = ISR 끝나면 바로 잠 (main 은 안 돈다). POSIX 의 같은 해법은 `sigsuspend()` — 호스트 실측 유실 30 % → 0 %.

## 참고자료

- ARMv7-M Architecture Reference Manual (ARM DDI 0403C errata) — B1.5.17 Power management (SLEEPONEXIT, SEVONPEND 설명), B1.5.18 Wait For Event and Send Event (wake-up 이벤트, 이벤트 레지스터), B1.5.19 Wait For Interrupt (PRIMASK Note), B3.2.3 Table B3-9 SCR (SEVONPEND / SLEEPDEEP / SLEEPONEXIT) — <https://developer.arm.com/documentation/ddi0403/latest/>
- ARMv8-M ARM 의 규칙 R_BPBR (예외 진입/복귀가 이벤트 레지스터 set) — QEMU 의 Cortex-M WFE/SEVONPEND 구현 패치 주석에서 인용 확인, 원문 미확인 — <https://patchew.org/QEMU/20260210135206.229528-1-peter.maydell@linaro.org/20260210135206.229528-26-peter.maydell@linaro.org>
- FreeRTOS-Kernel V11.3.2 `portable/GCC/ARM_CM4F/port.c` — `vPortSuppressTicksAndSleep()` (590–700행), Tickless idle 문서 — <https://www.freertos.org/low-power-tickless-rtos.html>
- Zephyr `arch/arm/core/cortex_m/cpu_idle.c` (`arch_cpu_idle`, `arch_cpu_atomic_idle`, SEVONPEND 초기화) — <https://github.com/zephyrproject-rtos/zephyr/blob/main/arch/arm/core/cortex_m/cpu_idle.c>, Power Management 문서 (`min-residency-us`, `exit-latency-us`) — <https://docs.zephyrproject.org/latest/services/pm/index.html>
- CMSIS-Core: `__WFI`, `__WFE`, `__SEV`, `__disable_irq`, SCB->SCR — <https://arm-software.github.io/CMSIS_6/latest/Core/index.html>
- ST AN4621 "STM32L4 ultra-low-power features overview" 및 STM32L4 데이터시트의 전류·웨이크업 표, RM0351 PWR 장 (모드 정의·SBF) — <https://www.st.com>
- POSIX.1-2017 `sigsuspend`, `pause` — <https://pubs.opengroup.org/onlinepubs/9699919799/functions/sigsuspend.html>
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.), 저전력 기능 장 (Sleep-on-exit, WFE/SEV)
- 관련 레슨: C-M14 (ISR↔메인 공유 데이터, PRIMASK 저장/복원, 펜딩 비트 합쳐짐), OS-B06 (condvar 의 `while` + 뮤텍스 = 같은 "확인~잠들기 원자화"), RTOS-B08 (ISR→태스크 신호의 이벤트 합쳐짐), RTOS-A08 (멀티코어에서 인터럽트 끄기만으로 부족한 이유), BM-B04 (W1C 플래그), BM-B13 (SysTick), BM-M12 (RTC 알람 웨이크업·백업 도메인), BM-A09 (.noinit 크래시 덤프), RUST-M06 (critical-section)
