---
id: BM-M01
time: 80분
updated: 2026-11-11
---
# BM-M01 NVIC 기초: ISER/ICER/IPR 비트 계산과 ISR 규칙

## 학습 목표

- IRQ 번호 n 하나로 **ISER/ICER/ISPR/ICPR/IABR 의 워드·비트**(n>>5, n&31)와 **IPR 의 워드·바이트 레인**(n>>2, n&3), 그리고 실제로 구현된 우선순위 비트(STM32F4 는 바이트의 [7:4])를 바로 계산할 수 있다.
- set/clear 쌍 레지스터(W1S/W1C, 읽으면 상태)를 mock 으로 구현하고, `ICER |= bit` 이 왜 **워드 안의 모든 IRQ 를 끄는지**, `ISER |= bit` 이 왜 **ISR 이 방금 끈 IRQ 를 되살리는지** 재현할 수 있다.
- 음수 IRQn(SysTick −1, PendSV −2, SVCall −5 …)을 SCB SHPR1..3 의 바이트로 옮기고, 설정할 수 없는 예외(NMI, HardFault, 예약)는 거부한다.
- "pending 은 disable 이어도 래치된다", "ICPR 를 써도 소스 라인이 high 면 바로 다시 선다" 를 이용해 **인터럽트 초기화 순서**를 정할 수 있다.
- ISR 끝에서 플래그를 지우면 버스 지연(posted write) 때문에 **ISR 이 한 번 더 불리는** 이유를 사이클 단위로 설명하고, 먼저 지우기 + read-back 으로 고친다.

## 1. 문제

**"`NVIC_SetPriority(37, 5)` 는 어느 레지스터의 어느 비트를 바꾸죠?"**, **"`NVIC->ICER[1] |= bit` 에 무슨 문제가 있죠?"**, **"플래그를 ISR 끝에서 지웠는데 왜 ISR 이 두 번 들어오죠?"** — 펌웨어 면접의 인터럽트 라운드는 거의 항상 이 셋으로 시작한다. NVIC(Nested Vectored Interrupt Controller)는 Cortex-M 모든 칩에 똑같이 들어 있는 코어 부품이라, 칩이 바뀌어도 답이 같다 — 그래서 면접관이 좋아한다.

Don 의 경험으로 옮기면: NVMe 컨트롤러의 `INTMS`/`INTMC`(마스크 set/clear 쌍), MSI-X 테이블의 벡터별 마스크 비트, SSD 펌웨어에서 "인터럽트 원인 레지스터를 지우고 나서 리턴해야 다시 안 들어온다" 는 규칙 — 모두 NVIC 에 그대로 있다. 차이는 NVIC 가 **코어 옆(PPB, Private Peripheral Bus)** 에 있어 쓰기가 즉시 효력이 있지만, 인터럽트 **원인**(주변장치 플래그)은 버스 브리지 너머에 있어 쓰기가 늦게 도착할 수 있다는 점이다. Part C 가 바로 그 차이다.

| 레지스터 (ARMv7-M ARM B3.4 기준 대표값 — 확인 권장) | 주소 | IRQ 당 | 쓰기 | 읽기 |
|---|---|---|---|---|
| ISER0..15 (Set-Enable) | 0xE000E100 + 4·k | 1 비트 | 1 = enable, 0 = 영향 없음 | enable 상태 |
| ICER0..15 (Clear-Enable) | 0xE000E180 + 4·k | 1 비트 | 1 = disable, 0 = 영향 없음 | enable 상태 (ISER 과 같은 값) |
| ISPR0..15 (Set-Pending) | 0xE000E200 + 4·k | 1 비트 | 1 = pending 설정 | pending 상태 |
| ICPR0..15 (Clear-Pending) | 0xE000E280 + 4·k | 1 비트 | 1 = pending 해제 | pending 상태 |
| IABR0..15 (Active Bit) | 0xE000E300 + 4·k | 1 비트 | 읽기 전용 | active 상태 |
| IPR0..123 (Priority) | 0xE000E400 + 4·k | 1 바이트 | 상위 `__NVIC_PRIO_BITS` 비트만 구현 | 하위 비트는 0 |
| STIR (Software Trigger) | 0xE000EF00 | — | [8:0] 에 IRQ 번호 → pending | — |
| SHPR1..3 (System Handler Priority) | 0xE000ED18 / 1C / 20 | 1 바이트 | 예외 4..15 의 우선순위 | 예약 바이트는 0 |

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">IRQ 37 하나로 계산하는 위치 (STM32F4: __NVIC_PRIO_BITS = 4)</text>
<text x="10" y="46" fill="currentColor" font-family="monospace">enable : 워드 = 37 &gt;&gt; 5 = 1, 비트 = 37 &amp; 31 = 5</text>
<rect x="10" y="56" width="220" height="26" class="f-mut" stroke="currentColor"/>
<text x="18" y="74" fill="currentColor" font-family="monospace">ISER0 0xE000E100  IRQ 0..31</text>
<rect x="10" y="82" width="220" height="26" class="f-acc" stroke="currentColor"/>
<text x="18" y="100" fill="currentColor" font-family="monospace">ISER1 0xE000E104  IRQ 32..63</text>
<rect x="10" y="108" width="220" height="26" class="f-mut" stroke="currentColor"/>
<text x="18" y="126" fill="currentColor" font-family="monospace">ISER2 0xE000E108  IRQ 64..95</text>
<text x="245" y="100" fill="currentColor" font-family="monospace">STR 0x00000020 (bit5) — 읽기 없음</text>
<text x="245" y="120" fill="currentColor">끄기는 ICER1 0xE000E184 에 같은 값. |= 금지</text>
<text x="10" y="166" fill="currentColor" font-family="monospace">priority : 워드 = 37 &gt;&gt; 2 = 9 → IPR9 0xE000E424, 레인 = 37 &amp; 3 = 1 → [15:8]</text>
<rect x="10" y="178" width="170" height="30" class="f-mut" stroke="currentColor"/>
<text x="18" y="198" fill="currentColor" font-family="monospace">[31:24] IRQ 39</text>
<rect x="180" y="178" width="170" height="30" class="f-mut" stroke="currentColor"/>
<text x="188" y="198" fill="currentColor" font-family="monospace">[23:16] IRQ 38</text>
<rect x="350" y="178" width="170" height="30" class="f-acc" stroke="currentColor"/>
<text x="358" y="198" fill="currentColor" font-family="monospace">[15:8] IRQ 37</text>
<rect x="520" y="178" width="170" height="30" class="f-mut" stroke="currentColor"/>
<text x="528" y="198" fill="currentColor" font-family="monospace">[7:0] IRQ 36</text>
<text x="350" y="232" fill="currentColor">IRQ 37 의 바이트 (주소 0xE000E425):</text>
<rect x="350" y="242" width="21" height="24" class="f-ok" stroke="currentColor"/><text x="356" y="259" fill="currentColor" font-family="monospace">0</text>
<rect x="371" y="242" width="21" height="24" class="f-ok" stroke="currentColor"/><text x="377" y="259" fill="currentColor" font-family="monospace">1</text>
<rect x="392" y="242" width="21" height="24" class="f-ok" stroke="currentColor"/><text x="398" y="259" fill="currentColor" font-family="monospace">0</text>
<rect x="413" y="242" width="21" height="24" class="f-ok" stroke="currentColor"/><text x="419" y="259" fill="currentColor" font-family="monospace">1</text>
<rect x="434" y="242" width="21" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="440" y="259" fill="currentColor" font-family="monospace">0</text>
<rect x="455" y="242" width="21" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="461" y="259" fill="currentColor" font-family="monospace">0</text>
<rect x="476" y="242" width="21" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="482" y="259" fill="currentColor" font-family="monospace">0</text>
<rect x="497" y="242" width="21" height="24" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="503" y="259" fill="currentColor" font-family="monospace">0</text>
<text x="528" y="252" fill="currentColor">[7:4] = 5 구현</text>
<text x="528" y="268" fill="currentColor">[3:0] RAZ/WI (점선)</text>
<text x="10" y="252" fill="currentColor" font-family="monospace">prio 5 → 5 &lt;&lt; (8−4) = 0x50</text>
<text x="10" y="272" fill="currentColor" font-family="monospace">워드 값 = 0x50 &lt;&lt; 8 = 0x00005000</text>
<text x="10" y="306" fill="currentColor">음수 IRQn: SysTick(−1) → 예외 15 → SHPR3 0xE000ED20 [31:24] = CMSIS SHP[11]</text>
<text x="10" y="328" fill="currentColor">바이트 주소 = 워드 주소 + 레인 (리틀 엔디언). 실칩 IPR 은 바이트 접근 가능 → CMSIS 는 STRB 한 번.</text>
<text x="10" y="348" fill="currentColor">이 레슨의 mock 은 32비트 접근만 → 워드 RMW 가 필요하고, 그래서 ISR 과의 레이스가 생긴다 (Part B).</text>
</svg>
```

모든 코드는 맥에서 돈다. 드라이버는 `REG_READ`/`REG_WRITE` 로만 레지스터를 만지고, 호스트 빌드(`UNIT_TEST`)에서는 **mock**(BM-B02 mock 축약판: 레지스터 표 + 읽기/쓰기 훅 + 접근 로그 + BM-B04 식 끼어들기)으로 간다. 이 레슨이 BM-M 전체에서 쓰는 **NVIC 모델 전체판**을 소유한다: `en/pend/act/line` 비트 배열, IPR/SHPR 바이트, 그리고 "NVIC-lite" 디스패처 — 주변장치 모델이 `irq_line_set(IRQn, level)` 로 라인을 올리면, `sim_step()` 이 한 사이클 진행한 뒤 enable+pending 인 IRQ 의 핸들러를 동기 호출한다 (단일 스레드, 결정적, 선점 없음 — 선점은 BM-M02).

### Part A — NVIC 레지스터 모델 + CMSIS 식 드라이버

- 모델 훅 3개: `iser_on_write` (1 쓴 비트만 enable, 구현 밖 비트 무시), `icer_on_write` (1 쓴 비트만 disable), `ipr_on_write` (바이트마다 `& 0xF0`). 나머지(ISPR/ICPR/IABR/STIR/SHPR)는 주어진다.
- 드라이버 10개: `nvic_enable_irq / disable_irq / get_enable / set_pending / clear_pending / get_pending / get_active / trigger_stir / set_priority / get_priority` + 위치 계산 `prio_loc`.
- 테스트: IRQ 0..95 **전수** — 무작위로 채운 다른 워드·비트는 그대로, 대상 비트만 바뀌고, 버스 접근은 **쓰기 1 번·읽기 0 번**. 우선순위는 96 × 16 = 1536 경우 전수, 무작위 5000 연산을 그림자(shadow) 모델과 비교.

### Part B — 시스템 예외, 잘못된 입력, 고전 버그

- 음수 IRQn → SHPR1..3 (`prio_loc` 의 두 번째 절반). 설정 가능한 7개 × 16 값 전수, Reset/NMI/HardFault/예약/범위 밖은 −1 + **버스 접근 0 번**. prio ≥ 16 도 거부 (CMSIS 는 조용히 잘라 0 = 최고 우선순위가 된다).
- 주어진 버그 코드 재현: `ICER |= bit`, `ISER |= bit` + ISR 레이스, 시프트 빠진 prio, IRQ 번호를 워드 인덱스로 (ISER·IPR 두 가지).
- 32비트 전용 mock 의 IPR 워드 RMW: ISR 이 같은 워드의 이웃 바이트를 바꾸는 순간을 모든 접근 지점에 끼워 넣어(sweep) 잃는 지점을 찾고, `nvic_set_priority` 는 임계 구역으로 막는다.

### Part C — 늦은 플래그 클리어와 초기화 순서

- 시간 모델: CPU 버스 접근 1 번 = 1 사이클. 주변장치(0x4000_0000~) 쓰기는 **posted write** — `1 + 코어 쓰기 버퍼 지연 + 브리지 지연` 사이클 뒤 도착. PPB(NVIC/SCB) 쓰기는 즉시. 주변장치 읽기는 앞선 쓰기가 모두 도착할 때까지 멈춘다(같은 브리지, 순서 보장). DSB 는 코어 쪽 쓰기 버퍼만 비운다.
- TIM2 모델: 100 사이클마다 UIF, 인터럽트 라인 = UIF & UIE (레벨). NVIC 는 **예외 복귀 순간** 라인이 high 면 pending 을 다시 세운다.
- 주어진 ISR 4종(끝에서 지움 / + DSB / 먼저 지움 W=20 / W=2)의 헛 진입 수를 지연 5종에서 표로 만들고, `tim2_isr_fixed()` (먼저 지우기 + read-back) 와 `tim2_irq_init()` (소스 → 도착 확인 → NVIC pending → 우선순위 → enable) 을 작성한다.

## 2. Problem (English)

> **NVIC driver on a register mock (ARMv7-M, STM32F4: 96 IRQ lines modelled, `__NVIC_PRIO_BITS = 4`).**
>
> 1. Model the set/clear register pairs: writing 1 to ISERn/ICERn sets/clears that IRQ's enable bit, writing 0 has no effect, and both read back the current enable state. IPRn holds one byte per IRQ; only bits [7:4] are implemented (the rest read as 0).
> 2. Implement `int nvic_enable_irq(int irqn)`, `nvic_disable_irq`, `nvic_get_enable`, `nvic_set_pending`, `nvic_clear_pending`, `nvic_get_pending`, `nvic_get_active`, `nvic_trigger_stir`, `int nvic_set_priority(int irqn, uint32_t prio)`, `int nvic_get_priority(int irqn)`. Valid IRQs are 0..95; return −1 for invalid input **without touching the bus**. Enable/disable/pend/unpend must be **one write, no read**.
>    Example: `nvic_enable_irq(37)` → `W 0xE000E104 = 0x00000020`. `nvic_set_priority(37, 5)` → `R 0xE000E424`, `W 0xE000E424 = 0x00005000` (other three bytes preserved).
> 3. Negative IRQn are system exceptions: priority lives in SHPR1–3 (exception number = 16 + IRQn). `nvic_set_priority(SysTick_IRQn /* −1 */, 15)` → byte [31:24] of 0xE000ED20 = 0xF0. NMI/HardFault/reserved → −1. `prio > 15` → −1.
> 4. The mock only does 32-bit accesses, so priority updates are a word read-modify-write. Make it safe against an ISR that updates a neighbouring IRQ's priority in the same word.
> 5. Peripheral writes are posted: they reach the peripheral several cycles later; the NVIC re-samples a level-sensitive line at exception return. Explain and fix "the ISR runs twice when the flag is cleared as the last statement", and write an init routine that never produces a spurious first interrupt even if a previous owner left the flag and the NVIC pending bit set.
>
> Follow-ups: Why are there separate set and clear registers? What does `ICER[1] |= bit` do? What if you forget `<< (8 - __NVIC_PRIO_BITS)`? Does `__DSB()` fix the double entry? Why can `NVIC_ClearPendingIRQ()` "not work"? How do you discover how many IRQs and priority bits a part implements?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/BM/BM-M01
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/BM-M01_st && /tmp/BM-M01_st      # 처음엔 10/47 passed, exit 1
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-M01_so && /tmp/BM-M01_so -v  # -v: 브링업 트레이스
```

시작 코드에서 통과하는 10개는 주어진 코드만으로 결정되는 것들이다 (IABR 읽기 전용, STIR, 주어진 ISR 4종의 표 등). 고친 ISR 자리는 비어 있어 표의 마지막 두 줄이 `storm`(한 스텝에 16 번 넘게 진입 → 디스패처가 끊음)으로 찍힌다 — 시작 코드가 멈추지 않게 하는 상한이다.

관찰용 (하네스와 별개): `demo_target.c` — 같은 코드의 Cortex-M4 명령. 실제 출력은 `demo_target_asm.txt`.

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -ffreestanding -std=c11 -Wall -Wextra -Werror \
      -c demo_target.c -o /tmp/BM-M01_demo.o && xcrun llvm-objdump -d --no-show-raw-insn /tmp/BM-M01_demo.o
```

## 4. 힌트

<details>
<summary>힌트 1 — 다섯 비트 뱅크는 같은 모양</summary>

ISER/ICER/ISPR/ICPR/IABR 는 모두 "32 IRQ 당 워드 하나": 워드 = `n >> 5`, 비트 = `1u << (n & 31)`. 다섯 뱅크의 시작 주소가 0x80 씩 떨어져 있어서 `(addr & 0x7F) >> 2` 하나로 뱅크 안의 워드 번호가 나온다 (모델의 `BANK_WORD`). set 쪽 훅은 `en |= val`, clear 쪽 훅은 `en &= ~val` — 두 레지스터가 **같은 상태**를 읽는다.

</details>

<details>
<summary>힌트 2 — 우선순위는 "바이트 배열" 로 생각하고 워드로 쓴다</summary>

IPR 은 IRQ 마다 1 바이트: 바이트 주소 = `0xE000E400 + n`. 워드로 바꾸면 워드 = `n >> 2`, 시프트 = `8 * (n & 3)`. 값은 `prio << (8 - 4)` — 상위 4비트에 넣어야 한다. 하위 4비트에 넣으면 하드웨어가 버리고 0 으로 읽힌다 = **가장 높은 우선순위**.

</details>

<details>
<summary>힌트 3 — 음수 IRQn</summary>

예외 번호 = 16 + IRQn. SHPR1 이 예외 4..7, SHPR2 가 8..11, SHPR3 가 12..15 를 바이트로 갖는다 → 워드 = `SHPR1 + 4 * ((exc - 4) >> 2)`, 시프트 = `8 * (exc & 3)`. 설정 가능한 것은 4, 5, 6, 11, 12, 14, 15 뿐 (1 Reset, 2 NMI, 3 HardFault 는 고정, 7..10·13 은 예약).

</details>

<details>
<summary>힌트 4 — Part C: "지웠다" 와 "도착했다" 는 다르다</summary>

STR 은 버스에 올려놓고 다음 명령으로 넘어간다. 같은 주변장치를 **읽으면** 그 읽기는 앞선 쓰기 뒤에 처리되므로, 읽기가 끝났다면 쓰기는 도착한 것이다. ISR 은 (1) 원인 읽기 → (2) 바로 지우기 → (3) 일 → (4) 같은 레지스터 한 번 더 읽기. 초기화도 같다: 소스 플래그 쓰기 → 읽기 → 그 다음에 ICPR.

</details>

## 5. 풀이

### Part A — set/clear 쌍과 위치 계산

모델 훅 세 개는 한 줄씩이다.

```c
g_nv.en[w] |= val & impl_mask(w);     /* ISER: 1 쓴 비트만 set, 구현 안 된 IRQ 비트는 RAZ/WI */
g_nv.en[w] &= ~val;                   /* ICER: 1 쓴 비트만 clear */
g_nv.ipr[irq] = (val >> 8*lane) & 0xF0;   /* IPR: 바이트마다 구현 비트만 */
```

왜 enable 레지스터가 둘인가? **읽기 없이 한 IRQ 만** 켜고 끄기 위해서다 — BM-B04 의 BSRR 과 같은 발상. "1 = 동작, 0 = 무시" 라서 `ISER1 = 0x20` 은 IRQ 37 하나에만 영향이 있고, 같은 워드의 나머지 31 개 IRQ 를 누가(ISR, 다른 코어의 코드, 디버거) 바꾸고 있든 상관없다. 하나의 rw enable 레지스터였다면 모든 enable/disable 이 RMW 가 되고 임계 구역이 필요했을 것이다.

드라이버는 그래서 쓰기 한 번이다.

```c
int nvic_enable_irq(int irqn) {
    if (!irq_valid(irqn)) return -1;
    REG_WRITE(NVIC_ISER0 + 4u * IRQ_WORD(irqn), IRQ_BIT(irqn));   /* IRQ_WORD = n>>5, IRQ_BIT = 1u<<(n&31) */
    return 0;
}
```

실제 출력 (solution.c):

```text
example: enable(37) -> W E000E104=00000020 ; set_priority(37,5) -> R E000E424, W E000E424=00005000
exhaustive: enable/disable/pend/unpend/get x 96 IRQs, priority 1536 cases
```

전수 테스트가 보는 것 — 결과 값만이 아니라 **접근 로그**:

| 함수 | 매 IRQ 마다 확인 | 실패 시 무엇을 잡나 |
|---|---|---|
| enable / disable | 다른 두 워드 그대로, 대상 워드는 `before \| b` / `before & ~b`, 로그 = `W` 1 개 | `|=` (R 이 생김), 워드 인덱스 오류 |
| set / clear pending | 같은 방식, ISPR / ICPR | ISER 와 ISPR 혼동 |
| get_enable / pending / active | 무작위 상태에서 모델과 같은 0/1 | 비트 위치 오류 |
| set_priority (96 × 16) | 96 바이트 중 대상만 `p << 4`, 로그 = 같은 IPR 워드 R → W, 다시 읽으면 p | 시프트 누락, 레인 오류, 이웃 바이트 손상 |

IABR(active)은 ISR 안에서만 1 이다. 테스트는 IRQ 10 을 disable 상태에서 pending 시키고(ISR 안 불림 — **pending 은 enable 과 무관하게 래치된다**), enable 하는 순간 다음 스텝에서 ISR 이 불리며, ISR 안에서 `nvic_get_active(10) == 1`, `nvic_get_pending(10) == 0` 임을 확인한다 (진입 = pending → active).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">IRQ 하나의 상태 (NVIC 모델 — 레벨 인터럽트, Generic User Guide 기준, 확인 권장)</text>
<rect x="20" y="110" width="130" height="50" rx="8" class="f-mut" stroke="currentColor"/>
<text x="50" y="140" fill="currentColor" font-weight="bold">Inactive</text>
<rect x="300" y="40" width="140" height="50" rx="8" class="f-warn" stroke="currentColor"/>
<text x="335" y="70" fill="currentColor" font-weight="bold">Pending</text>
<rect x="300" y="190" width="140" height="50" rx="8" class="f-acc" stroke="currentColor"/>
<text x="340" y="220" fill="currentColor" font-weight="bold">Active</text>
<rect x="580" y="110" width="150" height="50" rx="8" class="f-warn" stroke="currentColor"/>
<text x="590" y="132" fill="currentColor" font-weight="bold">Active + Pending</text>
<text x="590" y="150" fill="currentColor">(ISR 안에서 ISPR/STIR)</text>
<path d="M150 125 L300 70" stroke="currentColor" fill="none"/>
<text x="140" y="80" fill="currentColor">라인 high (enable 무관!)</text>
<text x="160" y="96" fill="currentColor">ISPR / STIR 쓰기</text>
<path d="M300 80 L150 140" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
<text x="175" y="128" fill="currentColor">ICPR (라인 low 일 때만 유지)</text>
<path d="M370 90 L370 190" stroke="currentColor" fill="none"/>
<text x="378" y="135" fill="currentColor">enable + 최고 우선순위 → 진입</text>
<text x="378" y="151" fill="currentColor">(pending 해제, active 설정)</text>
<path d="M300 220 L150 150" stroke="currentColor" fill="none"/>
<text x="120" y="215" fill="currentColor">복귀, 라인 low</text>
<path d="M440 200 L470 90 L440 75" stroke="currentColor" fill="none"/>
<text x="455" y="52" fill="currentColor">복귀 순간 라인 HIGH</text>
<text x="455" y="68" fill="currentColor">→ 다시 pending (tail-chain)</text>
<path d="M440 225 L580 150" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
<text x="10" y="270" fill="currentColor">핵심 두 줄: ① disable 이어도 pending 은 래치된다 → enable 하는 순간 묵은 인터럽트가 들어온다.</text>
<text x="10" y="290" fill="currentColor">② 소스 라인이 high 인 동안 ICPR 은 효과가 없다 → 주변장치 플래그를 먼저 지운다 (Part B/C).</text>
</svg>
```

### Part B — 음수 IRQn, 거부, 그리고 버그 네 가지

`prio_loc` 의 두 번째 절반:

| IRQn (CMSIS) | 예외 번호 | 레지스터 | 레인 | CMSIS 바이트 인덱스 `(IRQn & 0xF) − 4` |
|---|---|---|---|---|
| MemoryManagement −12 | 4 | SHPR1 0xE000ED18 | [7:0] | 0 |
| BusFault −11 / UsageFault −10 | 5 / 6 | SHPR1 | [15:8] / [23:16] | 1 / 2 |
| SVCall −5 | 11 | SHPR2 0xE000ED1C | [31:24] | 7 |
| DebugMonitor −4 | 12 | SHPR3 0xE000ED20 | [7:0] | 8 |
| PendSV −2 | 14 | SHPR3 | [23:16] | 10 |
| SysTick −1 | 15 | SHPR3 | [31:24] | 11 |
| NMI −14 / HardFault −13 | 2 / 3 | 고정 (−2 / −1) | — | 거부 |

테스트는 SysTick 과 PendSV 를 15 로 놓은 뒤 `SHPR3 == 0xF0F00000` 을 읽고, 7개 × 16 값 전수에서 다른 SHPR 바이트와 IPR 96 바이트가 그대로인지 본다. 잘못된 입력 13 종(96, 200, 1000, HardFault, NMI, −3, −6..−9, −15, −16, −100)과 `prio = 16`, `nvic_enable_irq(SysTick_IRQn)` 은 모두 −1 이고 `g_nacc == 0` — **검증은 버스에 닿기 전에**. (시스템 예외의 enable 은 NVIC 가 아니라 SHCSR / SysTick CSR 의 일 — BM-M13, BM-B13.)

버그 재현 결과 (solution.c 테스트가 단언하는 값):

| 버그 코드 (주어짐) | 시나리오 | 결과 |
|---|---|---|
| `NVIC->ICER[1] \|= bit` | IRQ 32·37·40·63 enable, 37 을 끄려 함 | `en[1] = 0` — **네 개 전부 꺼짐**. ICER 를 읽으면 enable 상태가 나오고, 그 1 들을 다시 써서 다 끈다 |
| `ipr byte = prio` (시프트 누락) | prio 9 였던 IRQ 37 에 5 | 바이트 0x05 → 구현 비트 밖 → 읽으면 **0 = 최고 우선순위** |
| `NVIC->ISER[irq] = bit` | IRQ 33 enable 상태에서 다시 enable | 주소 0xE000E100 + 33·4 = 0xE000E184 = **ICER1** → IRQ 33 이 **꺼진다** |
| 같은 버그, IRQ 37 | | 0xE000E194 (ICER5, 이 모델엔 없음) → mock 이 "예상 못 한 접근" 1 회. 실칩은 RAZ/WI 라 **조용히 무시** |
| `IPR[irq]` 를 워드 인덱스로 | `prio(5, 3)` | IPR5 레인 1 = **IRQ 21** 이 0x30, IRQ 5 는 그대로 |

레이스 두 개는 BM-B04 의 sweep 으로 본다 — 메인 동작의 k 번째 버스 접근 직후에 ISR 을 끼워 넣고(k = 0 … n) 결과가 틀린 k 를 비트마스크로:

```text
sweep ISER|=: lost-at-k mask=0x2 (n=2)  ISER=bit: 0x0 (n=1)
sweep IPR RMW: unprotected 0x2 (n=2)  nvic_set_priority: 0x0 (n=2)
```

- `ISER[1] |= bit(33)` 의 읽기(k=1)와 쓰기 사이에 ISR 이 IRQ 40 을 끄면(ICER1), 메인이 읽어 둔 값의 bit 8 을 다시 1 로 써서 **IRQ 40 이 되살아난다**. ISR 이 "이 장치 고장, 인터럽트 끈다" 를 했는데 메인이 몰래 다시 켜는 버그 — 재현이 거의 안 된다. `ISER = bit` 는 접근 1 번, 창 없음.
- IPR 워드 RMW 도 같은 창이 있다 (k=1 에서 IRQ 38 의 prio 9 유실). 실칩에서는 `NVIC->IP[n] = x` 가 **STRB 한 번**이라 창이 없지만, 이 mock 은 32비트만 되므로 `nvic_set_priority` 를 `cs_enter()`/`cs_exit(prev)` (이전 PRIMASK 저장·복원, C-M14) 로 감쌌다 → 끼어든 ISR 은 미뤄졌다가 cs_exit 에서 실행되어 결과가 맞다.

`demo_target_asm.txt` 의 실제 Cortex-M4 명령(발췌, nop 생략)이 이 차이를 그대로 보여 준다 — `enable_irq` 는 `str.w` 한 번, `disable_irq_bug` 는 `ldr.w`/`orrs`/`str.w`, `set_prio_byte` 는 `strb` 한 번, `set_prio_word` 는 `ldr.w`…`str.w` RMW:

```text
0000001c <disable_irq_bug>:
      1c:      	movw	r3, #0xe180
      20:      	and	r1, r0, #0x1f
      24:      	lsrs	r0, r0, #0x5
      26:      	movt	r3, #0xe000
      2a:      	mov.w	r12, #0x1
      2e:      	ldr.w	r2, [r3, r0, lsl #2]
      32:      	lsl.w	r1, r12, r1
      36:      	orrs	r1, r2
      38:      	str.w	r1, [r3, r0, lsl #2]
      3c:      	bx	lr

00000040 <set_prio_byte>:
      40:      	movw	r2, #0xe400
      44:      	lsls	r1, r1, #0x4
      46:      	movt	r2, #0xe000
      4a:      	strb	r1, [r0, r2]
      4c:      	bx	lr

00000050 <set_prio_word>:
      50:      	push	{r7, lr}
      52:      	movs	r2, #0x18
      54:      	movw	r12, #0xe400
      58:      	and.w	r2, r2, r0, lsl #3
      5c:      	bic	r0, r0, #0x3
      60:      	movt	r12, #0xe000
      64:      	ldr.w	lr, [r0, r12]
      68:      	movs	r3, #0xff
      6a:      	lsls	r3, r2
      6c:      	lsls	r1, r1, #0x4
      6e:      	bic.w	r3, lr, r3
      72:      	lsls	r1, r2
      74:      	orrs	r1, r3
      76:      	str.w	r1, [r0, r12]
      7a:      	pop	{r7, pc}
```

마지막 하나: ICPR 를 써도 소스 라인이 high 면 pending 은 **바로 다시 선다** (테스트: TIM2 UIF=1, UIE=1 인 채로 `nvic_clear_pending(28)` → `get_pending == 1`. UIF 를 지운 뒤 ICPR → 0). "NVIC_ClearPendingIRQ 가 안 먹어요" 의 정체다.

### Part C — 늦은 플래그 클리어

흔한 ISR 은 이렇게 생겼다.

```c
void TIM2_IRQHandler(void) {
    if (TIM2->SR & TIM_SR_UIF) { do_work(); TIM2->SR = ~TIM_SR_UIF; }   /* 마지막 문장에서 지움 */
}
```

`demo_target_asm.txt` 에서 `TIM2_IRQHandler_late` 의 끝은 `str r1, [r0]` 바로 다음 `bx lr` 이다. STR 은 "버스에 올렸다" 일 뿐 "TIM2 에 도착했다" 가 아니다. Cortex-M3/M4 는 쓰기 버퍼가 있고, STM32 의 TIM2 는 AHB→APB1 브리지 너머에 있다(APB1 은 보통 AHB 의 1/4 클럭). 쓰기가 도착하기 전에 예외 복귀가 일어나면 NVIC 는 아직 high 인 라인을 보고 pending 을 다시 세운다 → tail-chain 으로 ISR 이 또 불리고, 두 번째에는 UIF 가 이미 0 이다 = **헛 인터럽트(spurious interrupt)**.

`solution.c -v` 의 실제 트레이스 (지연 {코어 1, 브리지 3}, ISR 일 20 사이클):

```text
trace (late clear, lat {1,3}):
  cyc  50  TIM2: update event, UIF=1                                 1
  cyc  50  NVIC: exception entry IRQ                                 28
  cyc  51  ISR: read TIM2_SR                                         1
  cyc  71  CPU: write posted (not yet at peripheral)                 0xFFFFFFFE
  cyc  72  NVIC: exc return, line HIGH -> pending again (tail-chain) 28
  cyc  72  NVIC: exception entry IRQ                                 28
  cyc  76  bridge: write arrives at peripheral                       0xFFFFFFFE
  cyc  77  ISR: read TIM2_SR                                         0
  cyc  77  NVIC: exc return, line low                                28
  -> ticks=1 spurious=1 isr_calls=2
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">늦은 클리어 타임라인 — 위 트레이스 그대로 (x: 모델 사이클 45..80)</text>
<text x="10" y="58" fill="currentColor">UIF / 라인</text>
<path d="M90 70 L180 70 L180 44 L648 44 L648 70 L720 70" stroke="currentColor" fill="none" stroke-width="2"/>
<text x="10" y="108" fill="currentColor">CPU</text>
<rect x="180" y="90" width="396" height="30" class="f-acc" stroke="currentColor"/>
<text x="188" y="110" fill="currentColor">ISR #1: SR 읽기(51) → 일 20 사이클 → STR SR(71)</text>
<rect x="576" y="90" width="90" height="30" class="f-warn" stroke="currentColor"/>
<text x="580" y="110" fill="currentColor">ISR #2 헛진입</text>
<text x="10" y="158" fill="currentColor">버스 위 쓰기</text>
<rect x="558" y="140" width="90" height="28" class="f-mut" stroke="currentColor"/>
<text x="563" y="159" fill="currentColor">71 → 도착 76</text>
<text x="10" y="208" fill="currentColor">NVIC</text>
<path d="M576 125 L576 195" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
<text x="400" y="212" fill="currentColor">72 복귀: 라인 HIGH → pending</text>
<path d="M666 175 L666 225" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
<text x="560" y="240" fill="currentColor">77 복귀: 라인 low</text>
<text x="582" y="136" fill="currentColor">LDR 은 76 까지 멈춤</text>
<text x="180" y="40" fill="currentColor">50</text>
<text x="640" y="40" fill="currentColor">76</text>
<text x="10" y="270" fill="currentColor">고친 ISR: 읽기 → 곧바로 STR → 일 → 같은 SR 다시 LDR (도착까지 멈춤) → 복귀 때 라인 low.</text>
<text x="10" y="290" fill="currentColor">실칩의 지연은 코어/버스 구조·APB 분주비에 달렸다 — 이 그림의 숫자는 모델 값이다.</text>
</svg>
```

ISR 변형 × 지연 표 (200 번의 업데이트 동안 헛 진입 수, `solution.c` 실제 출력):

```text
spurious entries per 200 updates   lat{wb,bridge}: {0,0} {1,0} {2,0} {1,3} {2,6}
  late clear (W=20)               0   200   200   200   200
  late clear + DSB (W=20)         0     0     0   200   200
  clear first (W=20)              0     0     0     0     0
  clear first (W=2)               0     0     0   200   200
  fixed: first+readback W20       0     0     0     0     0
  fixed: first+readback W2        0     0     0     0     0
```

읽는 법:

1. **끝에서 지움**: 지연이 1 사이클이라도 있으면 매 업데이트마다 한 번 더 들어온다 (200/200). 인터럽트 부하가 두 배가 되고, "틱 카운터" 를 ISR 진입 횟수로 세는 코드는 두 배로 센다.
2. **+ DSB**: 이 모델의 DSB 는 **코어 쪽 쓰기 버퍼**만 비운다. 브리지가 쓰기를 받아 놓고 먼저 "완료" 를 돌려주는 구조({1,3}, {2,6})라면 DSB 로는 부족하다. 실칩에서 DSB 가 충분한지는 버스 구조에 달렸다 — ARM 은 DSB 를 권하는 글이 많고 ST 커뮤니티에는 "DSB 로 안 돼서 read-back 했다" 는 사례가 반복된다 (칩별 확인 권장). 그래서 면접 답은 "DSB, 그리고 확실하게는 read-back".
3. **먼저 지움**: 지운 뒤 남은 일 W 가 지연 이상이면 괜찮다 (연습 4: 필요한 최소 W = 지연, 모델에서 전수 확인). 짧은 ISR(W=2)에서는 실패 — 먼저 지우기는 **우연히** 맞는 경우가 많을 뿐이다.
4. **먼저 지움 + read-back**: 지연과 무관하게 0. 읽기는 같은 브리지를 지나므로 앞선 쓰기 뒤에 처리된다 (Device 메모리 접근 순서 — ARMv7-M ARM A3.7, 확인 권장).

```c
void tim2_isr_fixed(void) {
    if ((REG_READ(TIM2_SR) & TIM_UIF) == 0u) { g_spurious++; return; }   /* 원인 확인 */
    REG_WRITE(TIM2_SR, ~TIM_UIF);       /* 1) 먼저 지운다 (rc_w0: 지울 비트만 0 — BM-B04) */
    g_ticks++;
    cpu_work(g_work);                    /* 2) 일 */
    (void)REG_READ(TIM2_SR);             /* 3) read-back: 복귀 전에 도착 보장 */
}
```

**초기화 순서.** 앞선 코드(부트로더, 이전 설정, UG 비트 — BM-B14)가 UIF=1, UIE=1 을 남겨 두었고, NVIC 는 disable 이지만 pending 이 래치되어 있다고 하자. 세 가지 init 의 실제 결과 (지연 {1,3}):

```text
init naive      : first ISR @5 ticks=201 updates=200 spurious=0
init no readback: first ISR @7 ticks=200 updates=200 spurious=1
tim2_irq_init   : first ISR @50 ticks=200 updates=200 spurious=0
```

- naive (우선순위 → UIE → enable): enable 하는 순간 묵은 pending 으로 들어가고 UIF 도 1 이라 **가짜 첫 틱**(사이클 5, 진짜 첫 업데이트는 50) → 틱 201 개.
- no readback (SR 지움 → ICPR → …): 순서는 맞는데 SR 쓰기는 브리지에 있고 ICPR 는 PPB 라 **먼저 도착**한다. ICPR 시점에 라인이 아직 high → pending 이 다시 서고 → 헛 진입 1 회.
- `tim2_irq_init`: 소스 SR 쓰기 → **SR 읽기(도착 확인)** → ICPR → 우선순위 → UIE → NVIC enable 마지막. 첫 진입 = 첫 업데이트(50), 헛 진입 0.

**ISR 작성 규칙** (이 레슨이 확인한 것 + 면접 단골):

| 규칙 | 이유 |
|---|---|
| 원인 플래그를 읽고, **처리 전에** 지운다 | 처리 중 새로 온 이벤트를 다음 진입으로 받는다(엣지성 이벤트). 레벨성 원인(외부 센서 INT 핀)은 원인을 해소한 뒤 지운다 |
| 복귀 전 같은 주변장치를 read-back (또는 DSB + 칩 문서 확인) | 쓰기 지연으로 인한 tail-chain 재진입 방지 (Part C) |
| W1C/rc_w0 는 그 비트만 쓰기 (`SR = ~UIF`, `PR = bit`) | 다른 플래그 소실 방지 (BM-B04) |
| 짧게: 블로킹 대기·printf·malloc·긴 루프 금지, 일은 플래그/큐로 메인(또는 태스크)에 넘긴다 | 다른 인터럽트 지연(latency)을 늘리지 않는다 — ARM-A08 / BM-A10 에서 측정 |
| ISR↔메인 공유 변수는 `volatile` + 원자성(한 워드, 또는 임계 구역) | C-M14, ARM-M10 |
| 원인이 없으면 조용히 리턴하되 **세어라** (`g_spurious`) | 브링업 때 헛 인터럽트 수가 버스 지연·init 순서 버그의 증거다 |
| 공유 벡터(EXTI15_10 등)는 한 번 읽고 읽은 것만 지운다 | BM-B04 Part C |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-M01_so && /tmp/BM-M01_so          # 47/47 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/BM-M01_as && /tmp/BM-M01_as -v
```

같은 드라이버 소스는 `#define UNIT_TEST` 를 빼면 `REG_READ/WRITE` 가 진짜 `volatile` MMIO 가 되고, `cs_enter/cs_exit` 는 `__get_PRIMASK`/`__disable_irq`/`__set_PRIMASK` 로 바꾼다 (BM-B02 Part C 방식). 타깃에서는 `nvic_set_priority` 를 워드 RMW 대신 **바이트 쓰기**로 바꾸는 편이 낫다 — `demo_target.c` 의 `set_prio_byte` (STRB 한 번, 임계 구역 불필요).

## 7. 자주 하는 실수

**1. `NVIC->ICER[n] |= bit`.** ICER 를 읽으면 enable 상태가 나온다 → 켜져 있던 모든 IRQ 비트에 1 을 다시 써서 **워드 전체를 끈다**. "IRQ 하나 껐더니 UART 랑 DMA 도 죽었어요." 고침: `NVIC->ICER[n >> 5] = 1u << (n & 31);`.

**2. `NVIC->ISER[n] |= bit`.** 평소엔 결과가 같아서 리뷰에서 잘 안 걸린다. 그러나 읽기~쓰기 사이에 ISR 이 다른 IRQ 를 끄면 되살린다 (sweep k=1). 고침: `=`.

**3. 우선순위 시프트 누락.** `NVIC->IP[n] = 5;` → 하위 4비트는 구현이 안 돼 0 으로 읽힘 → **최고 우선순위**. CMSIS `NVIC_SetPriority(n, 5)` 는 안에서 `<< (8 - __NVIC_PRIO_BITS)` 를 하므로, 레지스터를 직접 쓸 때와 CMSIS 를 쓸 때 값의 의미가 다르다. 반대로 `NVIC_SetPriority(n, 0x50)` 처럼 미리 시프트한 값을 CMSIS 에 넘기면 `(0x50 << 4) & 0xFF = 0x00` → 역시 최고 우선순위.

**4. IRQ 번호를 워드 인덱스로 / `1u << n` (n ≥ 32).** `ISER[33]` 은 ICER1 을 건드려 IRQ 를 **끈다**. `1u << 37` 은 C 에서 정의되지 않은 동작(UB) — ARM 에서는 레지스터 시프트가 0 이 되거나 다른 값이 될 수 있다. 고침: 항상 `n >> 5`, `n & 31`.

**5. ISR 마지막 문장에서 플래그 클리어.** Part C — 지연이 있으면 매번 재진입. 고침: 먼저 지우고, 복귀 전에 같은 주변장치 read-back.

**6. 소스보다 NVIC pending 을 먼저 지움 / read-back 없이 바로 ICPR.** 소스 라인이 high 이면 ICPR 는 효과가 없다 (no readback init → 헛 진입 1). 고침: 소스 → 읽기 → ICPR → enable.

**7. 음수 IRQn 을 IPR 경로로.** `NVIC->IP[(uint32_t)-1]` 은 0xE000E400 + 0xFFFFFFFF = 0xE000E3FF (IPR 바로 앞 예약 영역) — 엉뚱한 주소. CMSIS 는 `IRQn < 0` 이면 SCB->SHP 로 분기한다. 그리고 HardFault(−13)를 넘기면 `(−13 & 0xF) − 4 = 0xFFFFFFFF` 로 배열 밖을 쓴다 — CMSIS 는 검사하지 않는다 (연습 3).

## 8. 면접 Q&A

<details>
<summary>Q1. `NVIC_SetPriority(37, 5)` 는 어느 레지스터, 어느 비트를 바꾸나요? STM32F4 기준으로.</summary>

IPR 은 IRQ 당 1 바이트라 바이트 주소 0xE000E400 + 37 = 0xE000E425. 워드로 보면 IPR9(0xE000E424, 37/4 = 9)의 레인 1 = 비트 [15:8]. STM32F4 는 `__NVIC_PRIO_BITS = 4` 라 값은 그 바이트의 [7:4] 에 들어가 0x50, 워드로는 0x00005000 — 우리 테스트 로그와 같다. 꼬리: "enable 은?" → ISER1(0xE000E104) bit 5 = 0x20. "우선순위 비트가 3 인 칩(TI TM4C 등 — 확인 권장)이라면?" → `5 << 5 = 0xA0`, 바이트 [7:5]. 그래서 CMSIS 가 `__NVIC_PRIO_BITS` 를 디바이스 헤더에서 받는다.

</details>

<details>
<summary>Q2. NVIC 에는 왜 ISER 와 ICER 가 따로 있죠? `NVIC->ICER[1] |= (1 << 5)` 는 어떻게 되나요?</summary>

"1 = 동작, 0 = 무시" 인 set/clear 쌍이면 **읽기 없이** 한 비트만 바꿀 수 있다 → RMW 가 없으니 ISR·다른 문맥과 경쟁하지 않는다. BSRR(BM-B04), NVMe INTMS/INTMC 와 같은 설계. 두 레지스터 모두 읽으면 현재 enable 상태다. 그래서 `ICER[1] |= bit` 은 "현재 enable 된 모든 비트 | bit" 를 ICER 에 써서 **워드 1 의 IRQ 32..63 을 전부 끈다** (테스트: 32·37·40·63 → 0). 꼬리: "`ISER |=` 는 괜찮나?" → 결과는 보통 같지만 읽기~쓰기 사이에 ISR 이 끈 IRQ 를 되살린다 (sweep 마스크 0x2).

</details>

<details>
<summary>Q3. ISR 이 두 번씩 들어옵니다. 두 번째에는 플래그가 이미 0 이에요. 원인과 고침은?</summary>

플래그를 ISR 마지막에 지우는 쓰기가 **주변장치에 도착하기 전에** 예외 복귀가 일어나서다. Cortex-M 의 쓰기는 버퍼링되고 주변장치는 AHB→APB 브리지 너머(APB 는 더 느린 클럭)라 몇 사이클 걸린다. NVIC 는 레벨 인터럽트 라인을 복귀 순간 다시 보고, 아직 high 면 pending 을 세워 tail-chain 으로 재진입시킨다. 고침: ① 플래그를 ISR **앞에서** 지운다 ② 복귀 전에 같은 주변장치 레지스터를 **읽는다**(read-back — 그 읽기는 앞선 쓰기 뒤에 처리된다) ③ `__DSB()` — 코어 쪽 쓰기 완료는 보장하지만 브리지가 쓰기를 post 하면 부족할 수 있다 (모델 표의 {1,3} 열, 칩별 확인 권장). 꼬리: "먼저 지우면 충분?" → 지운 뒤 남은 일이 지연보다 길 때만. 짧은 ISR 이면 실패한다 (W=2 행).

</details>

<details>
<summary>Q4. `NVIC_ClearPendingIRQ()` 를 불렀는데도 enable 하자마자 ISR 이 들어옵니다. 왜죠? 초기화 순서는?</summary>

두 가지. ① 소스(주변장치 플래그)가 아직 서 있으면 레벨 라인이 high 라 ICPR 직후 pending 이 바로 다시 선다. ② 소스를 지웠어도 그 쓰기가 아직 브리지에 있으면 ICPR(PPB, 즉시)가 먼저 도착한다. 순서: 소스 플래그 지우기 → 같은 레지스터 read-back → `NVIC_ClearPendingIRQ` → `NVIC_SetPriority` → 주변장치 인터럽트 enable(DIER 등) → `NVIC_EnableIRQ` 마지막. 이유: pending 은 **disable 상태에서도 래치되기** 때문에 "이전 주인" 이 남긴 pending 이 enable 순간 튀어나온다 (naive init: 사이클 5 에 가짜 첫 틱).

</details>

<details>
<summary>Q5. 우선순위 0 과 15 중 어느 쪽이 높죠? CMSIS `NVIC_SetPriority(n, 16)` 은?</summary>

숫자가 **작을수록** 급하다(0 이 가장 높음). 같은 우선순위끼리 동시에 pending 이면 IRQ(예외) 번호가 작은 쪽이 먼저 — 우리 디스패처의 `nvic_pick` 규칙과 같다. CMSIS 는 `(16 << 4) & 0xFF = 0` 으로 **조용히 잘라** 우선순위 0 이 된다 — 그래서 이 레슨 드라이버는 16 이상을 거부한다. 리셋 기본값은 모두 0 이라 아무것도 설정하지 않으면 모든 IRQ 가 최고 우선순위 동급이다. 꼬리: "선점(preemption)과 서브 우선순위는?" → AIRCR.PRIGROUP 으로 바이트를 그룹/서브로 나눈다 — BM-M02 에서 QEMU 로 검증.

</details>

<details>
<summary>Q6. SysTick 이나 PendSV 우선순위는 어디에 있죠? HardFault 우선순위를 바꿀 수 있나요?</summary>

시스템 예외는 NVIC 가 아니라 SCB 의 SHPR1..3 에 있다. 예외 번호 = 16 + IRQn 이고, SysTick(15) 은 SHPR3 [31:24], PendSV(14) 는 SHPR3 [23:16], SVCall(11) 은 SHPR2 [31:24]. CMSIS 는 `SCB->SHP[(IRQn & 0xF) - 4]` 한 줄로 바이트 인덱스를 만든다. Reset(−3)/NMI(−2)/HardFault(−1) 는 **고정 음수 우선순위**라 바꿀 수 없다. RTOS 는 보통 PendSV·SysTick 을 가장 낮게(0xF0) 둔다 — 문맥 전환이 다른 ISR 을 막지 않게 (RTOS-B, BM-M02). 꼬리: "MemManage/BusFault/UsageFault 는?" → SHPR1 에 우선순위가 있지만 SHCSR 에서 enable 하지 않으면 HardFault 로 승격된다 (BM-M13).

</details>

<details>
<summary>Q7. 처음 보는 Cortex-M 칩에서 IRQ 가 몇 개 구현됐는지, 우선순위 비트가 몇 개인지 어떻게 알아내죠?</summary>

문서: 디바이스 헤더의 `__NVIC_PRIO_BITS` 와 마지막 IRQn, 그리고 ICTR(0xE000E004)의 INTLINESNUM — 32 단위 상한 `32·(INTLINESNUM+1)`. 실측: 인터럽트를 막은 상태에서 ISERk 에 0xFFFFFFFF 를 쓰고 읽으면 구현된 비트만 1 (구현 안 된 것은 RAZ/WI), 원래 켜져 있던 것만 남기고 ICER 로 되돌린다. IPR 바이트에 0xFF 를 쓰고 읽으면 0xF0 → 4비트. 연습 1 이 82(F407)/91(F429)/96/32/240/1 IRQ × 2..8 비트 모델에서 이것을 맞히고 상태를 그대로 복원하는지 확인한다. BM-M02 는 QEMU mps2-an385 에서 실제로 해 본다.

</details>

<details>
<summary>Q8. 공유 데이터를 보호하려고 `NVIC_DisableIRQ(TIM2_IRQn)` 을 불렀습니다. 바로 다음 명령부터 안전한가요? PRIMASK 와 비교하면?</summary>

ICER 쓰기는 PPB 라 빠르지만, 이미 파이프라인에 들어온 진입이나 쓰기 완료 타이밍 때문에 "직후 한두 명령 사이에" ISR 이 들어올 수 있다 — 그래서 CMSIS 5 의 `__NVIC_DisableIRQ` 는 쓰기 뒤에 `__DSB(); __ISB();` 를 넣는다 (ARM AN321 메모리 배리어 가이드, 확인 권장). 비교: IRQ 개별 disable 은 **그 IRQ 만** 막아 다른 인터럽트 지연이 없지만, 같은 데이터를 만지는 ISR 이 여럿이면 모두 꺼야 한다. PRIMASK(`cpsid i`)는 전부 막고 짧아야 한다. BASEPRI 는 "우선순위 N 이하만" 막는 중간 — ARM-M10. 이 레슨의 `nvic_set_priority` 는 PRIMASK 저장·복원(중첩 안전)을 썼다.

</details>

<details>
<summary>Q9. ISR 안에서 하면 안 되는 것은? "ISR 은 짧게" 를 구체적으로.</summary>

블로킹 대기(폴링 UART 출력, `HAL_Delay`), printf/semihosting(BM-B09 — 디버거 없으면 HardFault), malloc, 긴 루프, RTOS 의 블로킹 API(`FromISR` 버전만). 해야 할 것: 원인 확인 → 원인 클리어 → 최소 처리(데이터를 링버퍼에, 플래그 세팅) → 메인/태스크에 알림 → read-back 후 복귀. 원인이 없는 진입은 세어 두기. 우선순위가 높은 ISR 일수록 짧아야 하는 이유는 그 아래 모든 인터럽트의 지연에 더해지기 때문 (측정은 BM-A10 / ARM-A08 — DWT CYCCNT). 진입 지연 자체는 Cortex-M3/M4 에서 대략 12 사이클(제로 대기 메모리 기준, 확인 권장).

</details>

## 9. 추가 연습

### 연습 1 — 구현된 IRQ 수와 우선순위 비트 수 알아내기

`nvic_probe(&n_irq, &prio_bits)` 를 써라. ISER0..15 에 0xFFFFFFFF 를 쓰고 읽어 popcount 합 = IRQ 수, IPR0 바이트 0 에 0xFF 를 쓰고 읽어 popcount = 우선순위 비트 수. 원래 enable 상태와 IPR 값을 **정확히** 되돌려야 한다. 실칩에서는 무엇을 조심해야 하나?

<details>
<summary>답 보기</summary>

되돌리기의 핵심: `ICER[k] = got & ~before` — 내가 켠 것만 끈다 (`ICER = 0xFFFFFFFF` 로 다 끄면 원래 켜져 있던 IRQ 까지 꺼진다). IPR 은 원래 워드를 저장했다가 그대로 다시 쓴다. 실제 출력: `probe: model 82 IRQs / 4 prio bits -> found 82 / 4`, `91/4`, `96/4`, `32/3`, `240/8`, `1/2` 모두 일치, 상태 복원 확인. 실칩 주의: 모든 IRQ 를 잠깐 enable 하는 동안 pending 인 것이 있으면 즉시 진입한다 → PRIMASK=1 상태에서, 가능하면 리셋 직후에 한다. ICTR.INTLINESNUM 으로 먼저 상한을 얻으면 접근 수도 줄어든다.

</details>

### 연습 2 — 부트로더 → 앱 점프 전 NVIC 정리

`nvic_quiesce(still[])`: ICER0..15 = 0xFFFFFFFF, ICPR0..15 = 0xFFFFFFFF, 그 뒤 ISPR 을 읽어 "그래도 남은 pending" 을 돌려준다. TIM2 소스가 high 인 채로 부르면? 어떻게 해야 0 이 되나?

<details>
<summary>답 보기</summary>

모두 disable + pending 해제를 해도 TIM2 라인(UIF & UIE)이 high 면 ICPR 직후 pending 이 다시 선다 → `left == 1`, `still[0] == 1 << 28`. disable 상태라 진입은 안 하지만, 앱이 TIM2 를 enable 하는 순간 묵은 인터럽트가 들어간다 (Q4). 고침: 점프 전에 **주변장치 쪽을** 먼저 멈춘다 — `TIM2_DIER = 0` + read-back (또는 RCC 리셋 펄스, BM-B03), 그 다음 quiesce → `left == 0`. 부트로더 점프 전체 절차(SysTick 끄기, VTOR, MSP)는 BM-A03.

</details>

### 연습 3 — CMSIS 식 바이트 주소와 워드+레인 계산이 같은가

`prio_byte_addr(irqn)` = IRQn ≥ 0 이면 `0xE000E400 + n`, 음수면 `0xE000ED18 + ((n & 0xF) − 4)`. IRQn −16..95 전부에서 `prio_loc` 의 `word + shift/8` 과 같은지 확인하고, 설정 불가 번호는 둘 다 거부해야 한다. HardFault(−13)를 CMSIS 식에 넣으면?

<details>
<summary>답 보기</summary>

유효한 103 개(IRQ 96 + 시스템 7)가 모두 일치, SysTick → 0xE000ED23 (= SHPR3 + 3, 리틀 엔디언 레인 3 = [31:24]). HardFault: `(−13 & 0xF) − 4 = 3 − 4 = 0xFFFFFFFF` (실제 출력) → `SHP[0xFFFFFFFF]` 는 배열 밖 쓰기다. CMSIS 의 `NVIC_SetPriority` 는 이 검사를 하지 않으므로 "음수면 무조건 SHP" 를 믿고 HardFault/NMI 를 넘기면 안 된다. 내 드라이버처럼 허용 목록(switch)으로 거른다.

</details>

### 연습 4 — "먼저 지우기" 만으로 충분한 최소 일

ISR 이 플래그를 지운 **뒤** W 사이클 일하고 복귀한다 (read-back 없음). 지연 lat = 0..8 에서 헛 진입이 0 이 되는 최소 W 를 시뮬레이션으로 찾고, 공식 `W_min(lat)` 과 비교하라.

<details>
<summary>답 보기</summary>

모델에서 쓰기는 발행 사이클 t 에서 t + 1 + lat 에 도착하고, 복귀는 t + 1 + W 에 일어난다 → `W ≥ lat` 이면 안전, `W_min = lat`. 실제 출력: lat=1..8 모두 최소 W = lat, W−1 에서는 5000 사이클(50 업데이트) 동안 헛 진입 50 회. 교훈: "먼저 지우기" 의 안전 여유는 **버스 지연과 ISR 길이의 우연한 관계**다. 컴파일러 최적화로 ISR 이 짧아지거나 APB 분주비를 바꾸면 깨진다 → read-back 은 이 관계를 없앤다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-M01_pr && /tmp/BM-M01_pr   # 17/17 passed
```

## 10. 핵심 정리

- IRQ n: 비트 뱅크(ISER/ICER/ISPR/ICPR/IABR)는 워드 `n>>5`, 비트 `n&31`. IPR 은 바이트 `0xE000E400 + n` = 워드 `n>>2`, 레인 `n&3`. 값은 `prio << (8 − __NVIC_PRIO_BITS)` (F4: [7:4]).
- set/clear 쌍은 "1 = 동작, 0 = 무시, 읽으면 상태" → 항상 `=` 로 쓴다. `ICER |= bit` 은 워드 전체를 끄고, `ISER |= bit` 은 ISR 이 끈 IRQ 를 되살린다.
- 시프트 빠진 우선순위는 0(최고)이 된다. CMSIS 는 16 이상도 조용히 0 으로 자른다 — 검증은 버스 접근 전에.
- 음수 IRQn → 예외 16+n → SHPR1..3 (SysTick SHPR3[31:24], PendSV [23:16], SVCall SHPR2[31:24]). Reset/NMI/HardFault 는 고정.
- pending 은 disable 이어도 래치되고, 소스 라인이 high 면 ICPR 이 안 먹는다 → init: 소스 클리어 → read-back → ICPR → 우선순위 → 소스 enable → NVIC enable.
- ISR 끝에서 플래그를 지우면 posted write 가 복귀보다 늦어 재진입한다. 고침: 먼저 지우기 + 같은 주변장치 read-back (DSB 는 버스 구조에 따라 부족할 수 있음).
- 헛 진입은 세어라 — 브링업에서 버스 지연·init 순서 버그의 증거다.

## 참고자료

- ARM *ARMv7-M Architecture Reference Manual* (DDI 0403) — B3.4 NVIC (ISER/ICER/ISPR/ICPR/IABR/IPR/STIR, ICTR), B3.2 SCB (SHPR1–3, AIRCR), B1.5 예외 모델과 우선순위, A3.7 메모리 접근 순서 (절 번호는 판에 따라 다름, 확인 권장) — <https://developer.arm.com/documentation/ddi0403/latest/>
- ARM *Cortex-M4 Devices Generic User Guide* (DUI 0553) — 4.2 NVIC 레지스터 표, "Level-sensitive and pulse interrupts" (복귀 시 라인이 high 면 다시 pending) — <https://developer.arm.com/documentation/dui0553/latest/>
- ARM Application Note 321 *ARM Cortex-M Programming Guide to Memory Barrier Instructions* — NVIC 설정 후 DSB/ISB, 쓰기 버퍼 (절 번호 확인 권장)
- CMSIS-Core `core_cm4.h` — `__NVIC_EnableIRQ`, `__NVIC_DisableIRQ` (DSB/ISB), `__NVIC_SetPriority` (`NVIC->IP[n]`, `SCB->SHP[(IRQn & 0xF) - 4]`) — <https://github.com/ARM-software/CMSIS_5>
- ST RM0090 *STM32F405/415, 407/417, 427/437, 429/439 Reference Manual* — 벡터 테이블(IRQ 수, TIM2 = IRQ 28), TIM SR rc_w0, APB1/APB2 브리지 (확인 권장)
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.) — 7장 예외와 인터럽트, NVIC 레지스터, 인터럽트 플래그 클리어 시점
- 관련 레슨: BM-B02 (mock 프레임워크 전체판), BM-B04 (W1C/rc_w0, sweep, 지연 쓰기 연습 4), BM-B14 (UG 가 세우는 가짜 첫 UIF), C-M14 (ISR↔메인 공유 데이터, PRIMASK), ARM-M05 (DMB/DSB/ISB), BM-M02 (PRIGROUP·선점, QEMU 실측), BM-M13 (fault enable), 다음 단계: ARM-M10 (PRIMASK/BASEPRI 임계 구역), ARM-A08 / BM-A10 (인터럽트 지연 측정), BM-A03 (부트로더 → 앱 점프)
