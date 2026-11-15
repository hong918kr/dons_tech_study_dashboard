---
id: BM-B04
time: 60분
updated: 2026-11-11
---
# BM-B04 RMW 함정: BSRR set/reset 레지스터와 W1C 상태 비트

## 학습 목표

- 레지스터 접근 타입(access type) rw / r / w / rc_r / rc_w0 / rc_w1(W1C) / rs 를 구분하고, 각 타입에서 **"플래그 하나만 지우는" 올바른 한 줄**을 바로 쓸 수 있다 (`PR = bit`, `SR = ~bit`).
- `EXTI->PR |= bit` 가 왜 **다른 펜딩 플래그까지 지우는지**, `TIM2->SR &= ~UIF` 가 왜 **가끔** 플래그를 잃는지를 LDR/ORR/STR 수준에서 설명할 수 있다.
- mock 에 "k 번째 버스 접근 직후 ISR/하드웨어 이벤트 주입" 을 넣어 **ODR read-modify-write 레이스를 결정적으로 재현**하고, 어느 지점이 위험한지 표로 보여 줄 수 있다.
- BSRR(원자적 set/reset), 임계 구역(PRIMASK), 상태/클리어 레지스터 분리 설계가 각각 **무엇을 막고 무엇을 못 막는지** 비교할 수 있다.
- 여러 라인이 한 벡터를 공유하는 EXTI 핸들러를 "한 번 읽고, 읽은 것만 지운다" 규칙으로 쓸 수 있다.

## 1. 문제

**"`GPIOA->ODR |= LED;` 에 무슨 문제가 있죠?"**, **"상태 플래그를 `|=` 로 지우면 왜 안 되죠?"** — 펌웨어 면접의 단골이고, 보드 브링업(bring-up)에서 "가끔 버튼 입력이 씹힌다", "하트비트 LED 가 가끔 안 깜빡인다" 같은 재현 안 되는 버그의 단골 원인이다. 한 줄짜리 C 문장이 실제로는 **읽기(LDR) → 수정(ORR) → 쓰기(STR)** 세 단계(read-modify-write, RMW)이고, 그 사이에 ISR 이나 하드웨어가 끼어들 수 있다.

Don 의 경험과 바로 이어진다. PCIe 설정 공간의 상태 비트(Device Status, AER Uncorrectable Error Status)는 **RW1C**(1 을 써서 지움), AHCI 의 `PxIS` 도 write-1-to-clear, NVMe 컨트롤러 레지스터에는 인터럽트 마스크를 켜는 `INTMS` 와 끄는 `INTMC` 가 **따로** 있다 — BSRR 과 같은 "set 레지스터 / clear 레지스터" 발상이다. STM32 의 `EXTI_PR`(rc_w1), `TIMx_SR`(rc_w0), `GPIOx_BSRR`(w) 가 이 레슨의 세 주인공이다.

| 접근 타입 (ST RM 표기) | 읽기 | 쓰기 | 예 (RM0090 기준 대표값, RM 확인 권장) | 지우는 올바른 한 줄 |
|---|---|---|---|---|
| rw | 값 | 값 | `GPIOx_ODR`, `TIMx_ARR` | (공유되면) RMW + 보호 |
| r / w | 값 / 0 | 무시 / 동작 | `GPIOx_IDR` / `GPIOx_BSRR` | — |
| rc_r | 읽으면 지워짐 | — | 일부 주변장치의 "읽으면 클리어" 상태 | 읽은 값을 버리지 말 것 |
| **rc_w0** | 값 | **0 을 쓴 비트만** 지워짐, 1 은 무시 | `TIMx_SR` (UIF, CC1IF …), `USART_SR` 의 TC/RXNE | `SR = ~FLAG;` |
| **rc_w1 (W1C)** | 값 | **1 을 쓴 비트만** 지워짐, 0 은 무시 | `EXTI_PR`, 신형 STM32 의 `xxx_ICR` | `PR = FLAG;` |
| rs | 값 | 1 을 쓰면 set (0 무시) | `FLASH_CR` 의 LOCK 비트 | — |

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">W1C(rc_w1) 상태 레지스터 EXTI_PR — 펜딩 라인 0, 1, 13 이 서 있을 때 라인 13 만 지우기</text>
<text x="10" y="50" fill="currentColor" font-weight="bold">틀림: EXTI-&gt;PR |= (1u &lt;&lt; 13);</text>
<text x="163" y="72" fill="currentColor" font-size="12">b13</text>
<text x="197" y="72" fill="currentColor" font-size="12">b11</text>
<text x="231" y="72" fill="currentColor" font-size="12">b1</text>
<text x="265" y="72" fill="currentColor" font-size="12">b0</text>
<text x="10" y="95" fill="currentColor">처음 PR</text>
<rect x="160" y="80" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="171" y="96" fill="currentColor" font-family="monospace">1</text>
<rect x="194" y="80" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="205" y="96" fill="currentColor" font-family="monospace">0</text>
<rect x="228" y="80" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="239" y="96" fill="currentColor" font-family="monospace">1</text>
<rect x="262" y="80" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="273" y="96" fill="currentColor" font-family="monospace">1</text>
<text x="302" y="95" fill="currentColor" font-family="monospace">0x2003</text>
<text x="10" y="132" fill="currentColor" font-family="monospace">LDR  r3, [PR]    r3 = 0x2003</text>
<text x="10" y="150" fill="currentColor" font-family="monospace">ORRS r0, r3      r0 = 0x2003</text>
<text x="10" y="168" fill="currentColor" font-family="monospace">STR  r0, [PR]    1 을 쓴 비트 = 13, 1, 0</text>
<text x="10" y="197" fill="currentColor">결과 PR</text>
<rect x="160" y="182" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="171" y="198" fill="currentColor" font-family="monospace">0</text>
<rect x="194" y="182" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="205" y="198" fill="currentColor" font-family="monospace">0</text>
<rect x="228" y="182" width="30" height="22" class="f-warn" stroke="currentColor"/>
<text x="239" y="198" fill="currentColor" font-family="monospace">0</text>
<rect x="262" y="182" width="30" height="22" class="f-warn" stroke="currentColor"/>
<text x="273" y="198" fill="currentColor" font-family="monospace">0</text>
<text x="302" y="197" fill="currentColor" font-family="monospace">0x0000</text>
<text x="10" y="232" fill="currentColor">라인 0·1 은 처리도 안 됐는데 사라졌다</text>
<text x="10" y="250" fill="currentColor">(라인 0 은 아예 다른 벡터 EXTI0 의 몫)</text>
<text x="390" y="50" fill="currentColor" font-weight="bold">맞음: EXTI-&gt;PR = (1u &lt;&lt; 13);</text>
<text x="543" y="72" fill="currentColor" font-size="12">b13</text>
<text x="577" y="72" fill="currentColor" font-size="12">b11</text>
<text x="611" y="72" fill="currentColor" font-size="12">b1</text>
<text x="645" y="72" fill="currentColor" font-size="12">b0</text>
<text x="390" y="95" fill="currentColor">처음 PR</text>
<rect x="540" y="80" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="551" y="96" fill="currentColor" font-family="monospace">1</text>
<rect x="574" y="80" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="585" y="96" fill="currentColor" font-family="monospace">0</text>
<rect x="608" y="80" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="619" y="96" fill="currentColor" font-family="monospace">1</text>
<rect x="642" y="80" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="653" y="96" fill="currentColor" font-family="monospace">1</text>
<text x="682" y="95" fill="currentColor" font-family="monospace">0x2003</text>
<text x="390" y="132" fill="currentColor" font-family="monospace">STR  r0, [PR]    r0 = 0x2000</text>
<text x="390" y="150" fill="currentColor">읽기 없음 → 창(window)도 없음</text>
<text x="390" y="168" fill="currentColor">0 을 쓴 비트는 그대로</text>
<text x="390" y="197" fill="currentColor">결과 PR</text>
<rect x="540" y="182" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="551" y="198" fill="currentColor" font-family="monospace">0</text>
<rect x="574" y="182" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="585" y="198" fill="currentColor" font-family="monospace">0</text>
<rect x="608" y="182" width="30" height="22" class="f-ok" stroke="currentColor"/>
<text x="619" y="198" fill="currentColor" font-family="monospace">1</text>
<rect x="642" y="182" width="30" height="22" class="f-ok" stroke="currentColor"/>
<text x="653" y="198" fill="currentColor" font-family="monospace">1</text>
<text x="682" y="197" fill="currentColor" font-family="monospace">0x0003</text>
<text x="390" y="232" fill="currentColor">라인 0·1 은 펜딩으로 남아 각자 ISR 이 처리</text>
<line x1="375" y1="40" x2="375" y2="260" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="10" y="290" fill="currentColor">rc_w0 (TIM_SR) 은 거울상: 지울 비트만 0, 나머지는 1 → TIM2-&gt;SR = ~TIM_SR_UIF; (0xFFFFFFFE)</text>
<text x="10" y="310" fill="currentColor">EXTI-&gt;PR &amp;= ~(1u &lt;&lt; 13) 는 정반대 결과: 0x0003 을 써서 0·1 을 지우고 13 은 남긴다 → PR = 0x2000</text>
</svg>
```

모든 코드는 맥에서 돈다. 드라이버는 `REG_READ` / `REG_WRITE` 두 매크로로만 레지스터를 만지고, 호스트 빌드(`UNIT_TEST`)에서는 이것이 **mock**(BM-B02 mock 축약판: 레지스터 표 + 쓰기 훅 + 접근 로그)으로 간다. 이번 레슨이 mock 에 더하는 것은 **끼어들기(injection)** 하나다: "CPU 의 k 번째 버스 접근 직후에 ISR(PRIMASK 로 막힘) 또는 하드웨어 이벤트(안 막힘)를 실행" — 이것으로 레이스를 운에 맡기지 않고 **모든 지점에 하나씩** 넣어 본다(sweep). 같은 드라이버 소스는 `UNIT_TEST` 를 빼면 진짜 MMIO(`*(volatile uint32_t *)addr`)로 컴파일된다 — `demo_rmw_asm.c` 로 Cortex-M4 asm 을 직접 본다.

### Part A — mock 에 접근 타입 세 개 + 올바른 드라이버

- 쓰기 훅 `w1c_on_write` (EXTI_PR, 라인 0..22 유효), `rcw0_on_write` (TIM2_SR), `bsrr_on_write` (BSRR 쓰기를 ODR 에 반영, set 우선, 저장값 0 = 읽으면 0).
- 드라이버: `exti_clear_pending(line)`, `tim_clear_flags(base, flags)`, `gpio_set_pins` / `gpio_reset_pins`, `gpio_write_field(port, mask, value)` — 모두 **쓰기 한 번, 읽기 0 번**이어야 하고 테스트가 접근 로그로 확인한다.

### Part B — 버그 재현과 고침

- 주어진 버그 코드: `PR |= bit`, `PR &= ~bit`, `SR &= ~UIF`, `ODR |= pins`. 결과 레지스터 값과 로그(R 다음 W)로 재현한다.
- `sweep()` 이 각 동작의 k = 0 … n 모든 지점에 끼어들기를 넣고 "기대값과 다른 k" 를 비트마스크로 돌려준다. 기대: `ODR |= P5` → {1}, `BSRR = P5` → ∅, `SR &= ~UIF` + 하드웨어 CC1IF → {1}, **임계 구역 안의 `SR &= ~UIF` 도 {1}** (PRIMASK 는 하드웨어를 못 막는다).
- 고침 두 가지: BSRR, 그리고 `odr_set_pins_cs()` (이전 PRIMASK 를 저장·복원하는 임계 구역 — C-M14, ARM-M10).

### Part C — 펌웨어처럼

- `exti15_10_irq_handler()` — 라인 10..15 가 공유하는 벡터. 라인 0 (다른 벡터 몫) 은 건드리지 않고, 핸들러 도중에 도착한 라인은 펜딩으로 남겨 NVIC 가 다시 부르게 한다.
- 메인 루프가 LED 필드(핀 0..3)를 1000 번 쓰는 동안 TIM2 업데이트 ISR 이 1~5 접근마다 끼어들어 UIF 를 지우고 하트비트(핀 6)를 토글한다. 메인이 ODR RMW 를 쓰면 하트비트가 묻히고, `gpio_write_field`(BSRR) 를 쓰면 0 건이어야 한다.

## 2. Problem (English)

> **RMW traps on memory-mapped registers (STM32F4-style, host mock).**
>
> All register access goes through `REG_READ(addr)` / `REG_WRITE(addr, val)`. On the host they hit a mock with per-register write hooks, an access log and an injection point ("run this ISR / hardware event right after the k-th bus access"; ISRs are deferred while PRIMASK = 1, hardware events are not).
>
> 1. Implement the write semantics: `EXTI_PR` is **rc_w1** (writing 1 clears, 0 has no effect), `TIM2_SR` is **rc_w0** (writing 0 clears, 1 has no effect), `GPIOA_BSRR` is write-only: bits [15:0] set ODR bits, bits [31:16] reset them, set wins if both, reads return 0.
> 2. Implement `exti_clear_pending(line)`, `tim_clear_flags(base, flags)`, `gpio_set_pins(port, pins)`, `gpio_reset_pins(port, pins)`, `gpio_write_field(port, mask, value)` — each must be **exactly one bus write and no read**.
>    Example: `PR = 0x2003; exti_clear_pending(13)` → log `W PR=0x2000`, `PR = 0x0003`. `ODR = 0x00FA; gpio_write_field(GPIOA, 0x0F, 0x05)` → log `W BSRR=0x000A0005`, `ODR = 0x00F5`.
> 3. Show that `EXTI->PR |= bit` clears every pending line, that `TIM2->SR &= ~UIF` loses a CC1IF set between its read and write, and that `GPIOA->ODR |= P5` loses an ISR's update when the ISR lands between the read and the write. Fix with BSRR and with a PRIMASK critical section that restores the previous mask (nest-safe).
> 4. Write `EXTI15_10_IRQHandler`: lines 10–15 share it; don't touch other lines; never drop a line that becomes pending while the handler runs.
>
> Follow-ups: Why does BSRR exist? Why `SR = ~UIF` and not `SR &= ~UIF`? Does disabling interrupts fix the rc_w0 case? What about DMA or a second core? Why can clearing the flag at the very end of an ISR cause a second, spurious entry? What does bit-banding do to a W1C register?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/BM/BM-B04
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/BM-B04_st && /tmp/BM-B04_st
# 처음엔 "10/40 passed" — 버그 재현 중 mock 훅이 필요 없는 것(ODR 레이스 등)은 시작 코드로도 통과
/tmp/BM-B04_st -v     # sweep 표 출력 (정답 코드로 돌리면 5. 풀이의 표가 나온다)
```

관찰용 파일 (하네스와 별개):

```bash
# 같은 드라이버의 타깃 형태 → Cortex-M4 asm (링크·실행 없음)
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_rmw_asm.c -o -
```

## 4. 힌트

<details>
<summary>힌트 1 — 쓰기 훅은 "old 와 val 로 new 를 만드는" 한 줄짜리 식</summary>

W1C: val 에서 1 인 비트만 old 에서 내린다 → `old & ~val`. rc_w0: val 에서 0 인 비트만 내린다 → `old & val`. 둘 다 소프트웨어가 비트를 **세울 수는 없다** (테스트: 0xFFFFFFFF 를 써도 0 이 그대로). BSRR 훅은 저장값이 아니라 **ODR** 을 바꾼다: reset 을 먼저, set 을 나중에 적용하면 "둘 다면 set" 규칙이 저절로 된다. 그리고 `return 0` — BSRR 은 읽으면 0.

</details>

<details>
<summary>힌트 2 — 드라이버: 지울 비트가 아닌 비트에는 "무해한 값" 을 쓴다</summary>

W1C 에서 무해한 값은 0, rc_w0 에서 무해한 값은 1. 그러니 W1C 는 `REG_WRITE(PR, bit)`, rc_w0 은 `REG_WRITE(SR, ~bits)`. 둘 다 읽기가 없다. `gpio_write_field` 는 mask 안에서 value 가 0 인 핀 → BR(위 절반), 1 인 핀 → BS(아래 절반).

</details>

<details>
<summary>힌트 3 — 임계 구역은 "끄기" 가 아니라 "저장 후 끄기, 복원"</summary>

`cs_enter()` 가 돌려준 이전 PRIMASK 를 `cs_exit(prev)` 에 넘긴다. 이미 막힌 문맥(다른 임계 구역 안, 또는 ISR 이 `__disable_irq` 한 뒤)에서 불려도 나갈 때 **풀어 버리지 않는다** — 테스트가 그 중첩(nesting) 경우를 확인한다.

</details>

<details>
<summary>힌트 4 — 공유 핸들러: 스냅샷 하나</summary>

PR 을 **한 번** 읽어 `& 0xFC00` 으로 내 라인만 고른다. 그 스냅샷을 그대로 W1C 로 쓰면 "읽은 것만" 지워진다. 읽은 뒤 도착한 라인은 스냅샷에 없으니 1 이 안 쓰이고 펜딩으로 남는다 → 핸들러가 끝나면 NVIC 가 다시 부른다.

</details>

## 5. 풀이

### Part A — 접근 타입을 식으로

| 레지스터 | 접근 타입 | 쓰기 훅 (new = …) | 무해한 쓰기 값 | 올바른 클리어 |
|---|---|---|---|---|
| EXTI_PR | rc_w1 | `old & ~val` | 0 | `PR = bit` |
| TIM2_SR | rc_w0 | `old & val` | 1 | `SR = ~bit` |
| GPIOA_BSRR | w | ODR = (ODR & ~(val>>16)) \| (val & 0xFFFF), 저장 0 | 0 | — |
| GPIOA_ODR | rw | `val & 0xFFFF` | (없음) | — |

```c
static uint32_t w1c_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    return old & ~val & EXTI_PR_VALID;        /* 1 쓴 비트만 내린다 */
}
static uint32_t bsrr_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    uint32_t odr_addr = addr - GPIO_BSRR + GPIO_ODR, odr = mock_peek(odr_addr);
    odr &= ~(val >> 16);                      /* BRy 먼저 적용하고 */
    odr |= val & 0xFFFFu;                     /* BSy 를 나중에 → 같은 핀이면 set 이 이긴다 */
    mock_poke(odr_addr, odr & 0xFFFFu);
    return 0u;                                /* 저장값 0 = 읽으면 0 */
}
```

BSRR 의 규칙("BSx 와 BRx 가 둘 다 1 이면 BSx 우선", "읽으면 0x0000")은 RM0090 GPIO 장의 BSRR 설명 기준이다 (확인 권장). 옛 STM32F4 SPL 헤더는 이 레지스터를 16비트 `BSRRL` / `BSRRH` 두 칸으로 나눠 정의하기도 했다 — 같은 레지스터를 하프워드로 쓴 것이다.

드라이버는 모두 쓰기 한 번이다. 테스트가 결과 값뿐 아니라 **로그의 길이와 내용**까지 본다 (`g_nlog == 1 && log_is(0, 'W', …)`) — "결과만 맞는 RMW" 를 걸러 내기 위해서다.

```c
static void exti_clear_pending(uint32_t line)          { REG_WRITE(EXTI_PR, PIN(line)); }
static void tim_clear_flags(uint32_t base, uint32_t f) { REG_WRITE(base + TIM_SR, ~f); }
static void gpio_write_field(uint32_t port, uint32_t mask, uint32_t value) {
    REG_WRITE(port + GPIO_BSRR, ((mask & ~value & 0xFFFFu) << 16) | (mask & value & 0xFFFFu));
}
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">GPIOx_BSRR (w, 읽으면 0) — gpio_write_field(GPIOA, mask = 0x000F, value = 0x0005)</text>
<rect x="10" y="35" width="355" height="34" class="f-warn" stroke="currentColor"/>
<text x="20" y="57" fill="currentColor">[31:16] BR15 … BR0 : 1 → 그 핀 ODR = 0</text>
<rect x="365" y="35" width="355" height="34" class="f-ok" stroke="currentColor"/>
<text x="375" y="57" fill="currentColor">[15:0] BS15 … BS0 : 1 → 그 핀 ODR = 1</text>
<text x="10" y="88" fill="currentColor">0 을 쓴 비트 = 아무 일 없음. 같은 핀에 BR, BS 둘 다 1 이면 BS(set) 가 이긴다 (RM0090 기준, 확인 권장)</text>
<text x="10" y="119" fill="currentColor">BR3 BR2 BR1 BR0  = mask &amp; ~value = 0xA</text>
<rect x="10" y="125" width="30" height="22" class="f-warn" stroke="currentColor"/>
<text x="21" y="141" fill="currentColor" font-family="monospace">1</text>
<rect x="44" y="125" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="55" y="141" fill="currentColor" font-family="monospace">0</text>
<rect x="78" y="125" width="30" height="22" class="f-warn" stroke="currentColor"/>
<text x="89" y="141" fill="currentColor" font-family="monospace">1</text>
<rect x="112" y="125" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="123" y="141" fill="currentColor" font-family="monospace">0</text>
<text x="365" y="119" fill="currentColor">BS3 BS2 BS1 BS0  = mask &amp; value = 0x5</text>
<rect x="365" y="125" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="376" y="141" fill="currentColor" font-family="monospace">0</text>
<rect x="399" y="125" width="30" height="22" class="f-ok" stroke="currentColor"/>
<text x="410" y="141" fill="currentColor" font-family="monospace">1</text>
<rect x="433" y="125" width="30" height="22" class="f-mut" stroke="currentColor"/>
<text x="444" y="141" fill="currentColor" font-family="monospace">0</text>
<rect x="467" y="125" width="30" height="22" class="f-ok" stroke="currentColor"/>
<text x="478" y="141" fill="currentColor" font-family="monospace">1</text>
<text x="160" y="141" fill="currentColor" font-family="monospace">비트 19..16</text>
<text x="515" y="141" fill="currentColor" font-family="monospace">비트 3..0</text>
<text x="10" y="180" fill="currentColor" font-family="monospace">BSRR = (0xA &lt;&lt; 16) | 0x5 = 0x000A0005   — STR 한 번</text>
<text x="10" y="202" fill="currentColor" font-family="monospace">ODR  0x00FA → 0x00F5   (핀 3..0 = 0101, 핀 7..4 = 1111 그대로)</text>
<text x="10" y="232" fill="currentColor">읽기가 없으니 RMW 창이 없다 — ISR·DMA 가 다른 핀을 바꿔도 덮어쓰지 않는다</text>
</svg>
```

`gpio_write_field` 는 무작위 2000 케이스(xorshift32 고정 시드)에서 결과가 `(odr & ~mask) | (value & mask)` 와 같고 버스 접근이 정확히 1 번인지 확인한다. 이것이 "여러 핀을 동시에, 원자적으로" 바꾸는 방법이다 — 4비트 병렬 버스, LED 바, 모터 드라이버의 방향+enable 핀 쌍처럼 **같은 순간에** 바뀌어야 하는 핀들에 쓴다. ST HAL 도 같은 방식이다: `__HAL_GPIO_EXTI_CLEAR_IT(line)` 은 `EXTI->PR = (line)`, `__HAL_TIM_CLEAR_FLAG(h, f)` 는 `(h)->Instance->SR = ~(f)`, `HAL_GPIO_WritePin` 은 BSRR 에 쓴다 (HAL 버전마다 표기 차이, 소스 확인 권장).

### Part B — W1C 에 `|=`: 레이스가 아니라 "항상" 틀림

`EXTI->PR |= (1u << 13)` 를 컴파일러가 만드는 그대로 따라가면 (PR = 0x2003 에서 시작):

| 단계 | 명령 | 버스 | r / PR |
|---|---|---|---|
| 1 | `LDR r3, [PR]` | R PR = 0x2003 | r3 = 0x2003 |
| 2 | `ORRS r0, r3` (r0 = 1<<13) | — | r0 = 0x2003 |
| 3 | `STR r0, [PR]` | W PR = 0x2003 | W1C: 1 인 13·1·0 이 모두 지워짐 → **PR = 0x0000** |

읽은 값에 이미 1 인 펜딩 비트가 전부 들어 있으니, 그대로 써 돌려주면 **읽은 순간 펜딩이던 모든 라인을 지운다**. `&= ~bit` 은 정반대로 0x0003 을 써서 0·1 을 지우고 13 은 남긴다 (테스트: `PR == 0x2000`). 둘 다 테스트로 재현된다. 차이를 기억하는 한 문장: **W1C 레지스터를 읽어서 다시 쓰는 코드는 전부 틀렸다**.

sweep 은 하드웨어 이벤트 "라인 11 펜딩" 을 k = 0(동작 전), 1, …, n(동작 후)에 하나씩 넣는다. 다음은 `./solution -v` 의 실제 출력이다 (`hw.|` = 하드웨어가 비트를 세움, `isr.` = ISR 안의 접근):

```text
  PR = bit13             k=0  PR=0801 ok   | hw.|:PR=800 W:PR=2000
  PR = bit13             k=1  PR=0801 ok   | W:PR=2000 hw.|:PR=800
  PR |= bit13            k=0  PR=0000 LOST | hw.|:PR=800 R:PR=2801 W:PR=2801
  PR |= bit13            k=1  PR=0800 LOST | R:PR=2001 hw.|:PR=800 W:PR=2001
  PR |= bit13            k=2  PR=0800 LOST | R:PR=2001 W:PR=2001 hw.|:PR=800
```

시작 PR = 0x2001 (라인 13 + 라인 0), 기대 = 0x0801 (13 만 지워지고 0 과 새로 온 11 은 남음). `PR |= bit13` 는 **모든 k 에서** 틀린다 (마스크 0x7) — 타이밍 문제가 아니라 논리 문제다.

### Part B — rc_w0 에 `&=`: "대부분 되는" 레이스

`TIM2->SR &= ~TIM_SR_UIF` 는 결과 값만 보면 맞다: SR = 0x0003 을 읽어 0x0002 를 쓰면 rc_w0 규칙으로 UIF 만 지워진다. 그래서 흔하다 — 예제 코드와 오래된 펌웨어에 많다. 문제는 **읽기와 쓰기 사이**에 하드웨어가 다른 플래그를 세울 때다:

```text
  SR &= ~UIF             k=0  SR=0002 ok   | hw.|:SR=2 R:SR=3 W:SR=2
  SR &= ~UIF             k=1  SR=0000 LOST | R:SR=1 hw.|:SR=2 W:SR=0
  SR &= ~UIF             k=2  SR=0002 ok   | R:SR=1 W:SR=0 hw.|:SR=2
  SR = ~UIF              k=0  SR=0002 ok   | hw.|:SR=2 W:SR=FFFFFFFE
  SR = ~UIF              k=1  SR=0002 ok   | W:SR=FFFFFFFE hw.|:SR=2
  cs{SR &= ~UIF}         k=0  SR=0002 ok   | hw.|:SR=2 R:SR=3 W:SR=2
  cs{SR &= ~UIF}         k=1  SR=0000 LOST | R:SR=1 hw.|:SR=2 W:SR=0
  cs{SR &= ~UIF}         k=2  SR=0002 ok   | R:SR=1 W:SR=0 hw.|:SR=2
```

k = 1: 읽을 때 CC1IF 는 0 이었다 → 계산한 값 0x0000 에서 CC1IF 비트도 0 → 그 0 이 rc_w0 규칙으로 **방금 선 CC1IF 를 지운다**. 캡처 이벤트 하나가 조용히 사라진다. 창은 LDR 과 STR 사이 몇 클럭뿐이라 실기에서는 수십만 번에 한 번 — 그래서 "재현 안 되는 버그" 가 된다. `SR = ~UIF` 는 읽기가 없어 창이 없다.

마지막 세 줄이 핵심이다: **임계 구역으로 감싸도 똑같이 잃는다**. PRIMASK 는 CPU 의 예외 진입만 막는다. 타이머 하드웨어가 플래그를 세우는 것은 CPU 와 무관하게 일어난다. 그러니 rc_w0 / rc_w1 은 "보호" 가 아니라 **쓰는 값** 으로 고쳐야 한다.

### Part B — ODR RMW 레이스: ISR 이 끼어들 때

이번엔 상대가 ISR 이다. 메인은 `ODR |= P5`, ISR 은 P6 을 켠다 (ISR 안의 RMW 는 메인이 끼어들 수 없으니 이 모델에선 안전 — 더 높은 우선순위 ISR 이 같은 레지스터를 만지면 또 레이스다).

```text
  ODR |= P5 (isr+P6)     k=0  ODR=0060 ok   | isr.R:ODR=0 isr.W:ODR=40 R:ODR=40 W:ODR=60
  ODR |= P5 (isr+P6)     k=1  ODR=0020 LOST | R:ODR=0 isr.R:ODR=0 isr.W:ODR=40 W:ODR=20
  ODR |= P5 (isr+P6)     k=2  ODR=0060 ok   | R:ODR=0 W:ODR=20 isr.R:ODR=20 isr.W:ODR=60
  ODR |= P5 (isr-P6)     k=0  ODR=0020 ok   | isr.R:ODR=40 isr.W:ODR=0 R:ODR=0 W:ODR=20
  ODR |= P5 (isr-P6)     k=1  ODR=0060 LOST | R:ODR=40 isr.R:ODR=40 isr.W:ODR=0 W:ODR=60
  ODR |= P5 (isr-P6)     k=2  ODR=0020 ok   | R:ODR=40 W:ODR=60 isr.R:ODR=60 isr.W:ODR=20
  BSRR = P5              k=0  ODR=0060 ok   | isr.R:ODR=0 isr.W:ODR=40 W:BSRR=20
  BSRR = P5              k=1  ODR=0060 ok   | W:BSRR=20 isr.R:ODR=20 isr.W:ODR=60
  cs{ODR |= P5}          k=0  ODR=0060 ok   | isr.R:ODR=0 isr.W:ODR=40 R:ODR=40 W:ODR=60
  cs{ODR |= P5}          k=1  ODR=0060 ok   | R:ODR=0 W:ODR=20 isr.R:ODR=20 isr.W:ODR=60
  cs{ODR |= P5}          k=2  ODR=0060 ok   | R:ODR=0 W:ODR=20 isr.R:ODR=20 isr.W:ODR=60
```

두 방향 다 있다: ISR 의 **set 이 사라지거나** (0x0020), ISR 의 **clear 가 되살아난다** (0x0060 — 꺼야 할 모터 enable 핀이 다시 켜지는 쪽이 더 무섭다). 임계 구역 버전의 k = 1 trace 를 보면 ISR 이 메인의 W 뒤로 **밀려서** 실행된다 — 펜딩으로 기다렸다가 PRIMASK 가 풀리는 순간 들어온다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">ODR |= P5 의 읽기와 쓰기 사이(k = 1)에 ISR 이 P6 을 켠다 — sweep 실측 trace 그대로</text>
<text x="10" y="62" fill="currentColor" font-weight="bold">main</text>
<text x="10" y="122" fill="currentColor" font-weight="bold">ISR</text>
<text x="10" y="182" fill="currentColor" font-weight="bold">ODR</text>
<rect x="60" y="45" width="150" height="40" rx="5" class="f-acc" stroke="currentColor"/>
<text x="68" y="61" fill="currentColor" font-family="monospace">LDR r2,[ODR]</text>
<text x="68" y="78" fill="currentColor" font-family="monospace">r2 = 0x0000</text>
<rect x="220" y="105" width="170" height="40" rx="5" class="f-ok" stroke="currentColor"/>
<text x="228" y="121" fill="currentColor" font-family="monospace">ISR: R ODR, W ODR</text>
<text x="228" y="138" fill="currentColor" font-family="monospace">ODR ← 0x0040 (P6=1)</text>
<rect x="400" y="45" width="140" height="40" rx="5" class="f-acc" stroke="currentColor"/>
<text x="408" y="61" fill="currentColor" font-family="monospace">ORRS r0, r2</text>
<text x="408" y="78" fill="currentColor" font-family="monospace">r0 = 0x0020</text>
<rect x="550" y="45" width="150" height="40" rx="5" class="f-acc" stroke="currentColor"/>
<text x="558" y="61" fill="currentColor" font-family="monospace">STR r0,[ODR]</text>
<text x="558" y="78" fill="currentColor" font-family="monospace">ODR ← 0x0020</text>
<rect x="60" y="165" width="110" height="26" class="f-mut" stroke="currentColor"/>
<text x="90" y="183" fill="currentColor" font-family="monospace">0x0000</text>
<rect x="220" y="165" width="110" height="26" class="f-ok" stroke="currentColor"/>
<text x="250" y="183" fill="currentColor" font-family="monospace">0x0040</text>
<rect x="400" y="165" width="110" height="26" class="f-ok" stroke="currentColor"/>
<text x="430" y="183" fill="currentColor" font-family="monospace">0x0040</text>
<rect x="550" y="165" width="110" height="26" class="f-warn" stroke="currentColor"/>
<text x="580" y="183" fill="currentColor" font-family="monospace">0x0020</text>
<text x="550" y="210" fill="currentColor">P6 소실 (lost update)</text>
<path d="M210 65 L220 110" stroke="currentColor" fill="none"/>
<path d="M390 125 L400 75" stroke="currentColor" fill="none"/>
<text x="60" y="210" fill="currentColor">r2 는 ISR 전에 읽은 낡은 값</text>
<line x1="10" y1="225" x2="730" y2="225" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="10" y="248" fill="currentColor" font-weight="bold">고침 1 — BSRR:</text>
<text x="130" y="248" fill="currentColor" font-family="monospace">STR r0,[BSRR] (r0 = 0x20)</text>
<text x="330" y="248" fill="currentColor">쓰기 한 번 → 틈 없음, ODR = 0x0060 (ISR 앞/뒤 모두)</text>
<text x="10" y="272" fill="currentColor" font-weight="bold">고침 2 — 임계 구역:</text>
<text x="160" y="272" fill="currentColor" font-family="monospace">mrs r1,primask; cpsid i; LDR/ORRS/STR; msr primask,r1</text>
<text x="10" y="292" fill="currentColor">ISR 은 펜딩으로 기다렸다가 msr 직후 실행 → ODR = 0x0060. 단, 하드웨어·DMA·다른 코어는 못 막는다</text>
</svg>
```

실제 명령어로 확인한다 — `demo_rmw_asm.c` 를 Apple clang 21.0.0 으로 `--target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S` 한 결과에서 함수 6 개를 골랐다 (`.eabi_attribute` 같은 지시어와 `@APP` 표시 줄은 빼고, 오른쪽 `@ …` 주석은 설명용으로 붙였다. 명령어는 그대로):

```text
odr_set_rmw:                     @ GPIOA->ODR |= pins
	movs	r1, #20
	movt	r1, #16386          @ r1 = 0x40020014
	ldr	r2, [r1]            @ ── 여기부터
	orrs	r0, r2
	str	r0, [r1]            @ ── 여기까지가 창 (ISR 은 명령 사이 어디서나 들어온다)
	bx	lr
odr_set_bsrr:                    @ GPIOA->BSRR = pins
	movs	r1, #24
	movt	r1, #16386          @ r1 = 0x40020018
	str	r0, [r1]            @ 버스 쓰기 한 번
	bx	lr
odr_field_bsrr:                  @ BSRR = ((mask & ~v) << 16) | (mask & v)
	bic.w	r2, r0, r1
	ands	r0, r1
	movs	r1, #24
	orr.w	r0, r0, r2, lsl #16
	movt	r1, #16386
	str	r0, [r1]
	bx	lr
exti_clear_wrong:                @ EXTI->PR |= 1u << line
	movw	r2, #15380
	movt	r2, #16385          @ r2 = 0x40013C14
	movs	r1, #1
	ldr	r3, [r2]
	lsl.w	r0, r1, r0
	orrs	r0, r3
	str	r0, [r2]
	bx	lr
tim_clear_right:                 @ TIM2->SR = ~TIM_SR_UIF
	movs	r0, #16
	movt	r0, #16384          @ r0 = 0x40000010
	mvn	r1, #1              @ r1 = 0xFFFFFFFE
	str	r1, [r0]
	bx	lr
odr_set_cs:                      @ save PRIMASK; cpsid i; ODR |= pins; restore
	movs	r2, #20
	mrs	r1, primask
	cpsid i
	movt	r2, #16386
	ldr	r3, [r2]
	orrs	r0, r3
	str	r0, [r2]
	msr	primask, r1
	bx	lr
```

(`#16386` = 0x4002, `#15380` = 0x3C14, `#16385` = 0x4001.) `volatile` 이 있어도 RMW 는 여전히 세 명령이다 — `volatile` 은 "접근을 없애거나 합치지 마라" 일 뿐 **원자성(atomicity)을 주지 않는다**. 비교:

| 방법 | 접근 | ISR 에 안전 | 하드웨어 플래그에 안전 | DMA·다른 코어 | 비용 |
|---|---|---|---|---|---|
| `ODR \|= pins` | R+W | ✗ (k=1) | — | ✗ | 3 명령 |
| `BSRR = pins` | W | ✓ | — | ✓ (쓰기 한 번) | 1 STR |
| 임계 구역 + RMW | R+W | ✓ (ISR 지연) | ✗ | ✗ | + mrs/cpsid/msr, 인터럽트 지연(latency) 증가 |
| `SR = ~flag` / `PR = flag` | W | ✓ | ✓ | ✓ | 1 STR |
| 비트밴딩(bit-banding, M3/M4) | 버스가 잠근 RMW | ✓ | W1C 에는 **위험** | (버스 잠금) | BM-M/A 에서 |

비트밴딩 별칭(alias) 주소에 쓰면 코어가 워드를 읽고-한 비트만 바꿔-다시 쓰는 RMW 를 대신 한다. W1C 레지스터라면 읽은 워드의 **다른 1 들도 다시 쓰여 지워진다** — `|=` 와 같은 버그다 (Cortex-M3/M4 한정, M0/M7 에는 없음 — 상세는 BM-M/A 레인). LDREX/STREX(ARM-M11)는 일반 RAM 의 원자 연산용이고 주변장치 레지스터에는 쓰지 않는다 (Device 메모리에서의 exclusive 동작은 구현 정의 — 확인 권장).

### Part C — EXTI15_10 공유 핸들러

```c
static void exti15_10_irq_handler(void) {
    uint32_t pending = REG_READ(EXTI_PR) & EXTI15_10_LINES;   /* 한 번만 읽는다 */
    REG_WRITE(EXTI_PR, pending);                               /* 읽은 것만 W1C — 나중에 온 것은 남는다 */
    for (uint32_t line = 10; line <= 15; line++)
        if (pending & PIN(line)) g_exti_count[line]++;
}
```

PR = 0x2401 (라인 13, 10 + EXTI0 벡터 몫인 라인 0), 첫 접근 직후 라인 11 도착:

| 단계 | 정답 핸들러 | PR | 흔한 버그 핸들러 (라인마다 `if (PR & bit) PR \|= bit`) | PR |
|---|---|---|---|---|
| 1 | R PR = 0x2401 → pending = 0x2400 | 0x2401 | 라인 10: R PR = 0x2401 (비트 10 = 1) | 0x2401 |
| — | (하드웨어: 라인 11) | 0x2C01 | (하드웨어: 라인 11) | 0x2C01 |
| 2 | W PR = 0x2400 | **0x0801** | `PR \|= bit10`: R 0x2C01, W 0x2C01 | **0x0000** |
| 3 | 10, 13 카운트 | | 라인 11·13: R PR = 0 → 건너뜀 | |
| 결과 | 10·13 처리, 11 은 펜딩 → 재진입에서 처리, 라인 0 보존 | 0x0001 | 10 만 처리. **11·13 소실, 라인 0(다른 ISR 몫)까지 소실** | 0x0000 |

정답 핸들러는 접근 2 번(R, W)이고, 테스트가 로그 `R PR=0x2401, W PR=0x2400` 를 확인한다. 지우는 시점: **처리 전, ISR 앞부분에서** 지운다. 처리하는 동안 같은 라인이 다시 오면 다시 펜딩되어 한 번 더 불린다 (이벤트 유실 없음). 처리 후에 지우면 처리 중에 온 이벤트를 같이 지운다. 단 레벨 트리거 소스(센서의 INT 핀처럼 원인을 지울 때까지 유지)는 원인을 먼저 처리하지 않으면 바로 다시 서므로, "원인 레지스터 처리 → 플래그 클리어" 순서가 맞는 경우도 있다 — 데이터시트의 클리어 순서를 따른다.

### Part C — 메인 루프 + TIM2 하트비트 ISR, 1000 회

TIM2 ISR 은 1~5 버스 접근마다(xorshift32, 시드 0xC0FFEE) 끼어들어 `tim_clear_flags(TIM2, UIF)` 후 자기 shadow 로 핀 6 을 BSRR 로 토글한다. 메인은 매 반복 무작위 4비트를 핀 0..3 에 쓰고, 쓴 직후 ODR 을 검사한다. 실제 출력:

```text
fw naive(ODR RMW): ops=1000 isr=676 main_mismatch=0 hb_mismatch=286 uif_left=0
fw fixed(BSRR)   : ops=1000 isr=331 main_mismatch=0 hb_mismatch=0 uif_left=0
```

- naive: 메인 자신의 핀은 늘 맞다(main_mismatch 0) — 피해자는 **남의 핀**이다. 1000 번 중 286 번, 하트비트 핀이 ISR 의 shadow 와 다르다 (ISR 의 BSRR 쓰기를 메인의 낡은 ODR 쓰기가 덮음). 실기에서는 "가끔 LED 가 한 박자 쉰다" 로 보인다. (isr 수가 다른 것은 naive 가 반복당 접근을 2 번 해서 끼어들기 시계가 더 빨리 가기 때문이다.)
- fixed: 메인이 `gpio_write_field(GPIOA, 0xF, want)` → BSRR 한 번. 하트비트 0 건, UIF 남은 것 0 건.

교훈: **핀(또는 비트)마다 주인을 정하고, 주인은 set/clear 레지스터로만 자기 비트를 바꾼다.** 그러면 공유 레지스터에 대한 RMW 자체가 사라진다.

### 타깃 빌드

파일 맨 위 `#define UNIT_TEST 1` 을 빼면 `REG_READ/REG_WRITE` 는 `*(volatile uint32_t *)(uintptr_t)addr` 가 되고, `cs_enter/cs_exit` 은 CMSIS `__get_PRIMASK()` / `__disable_irq()` / `__set_PRIMASK()` 로 바뀐다 (mock 함수들은 타깃 펌웨어에서 따로 떼어 내면 된다). 드라이버 함수 본문은 한 글자도 안 바뀐다 — 이것이 BM-B02 매크로 seam 의 요점이다. 실제 명령어는 위 `demo_rmw_asm.c` 출력 그대로.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-B04_so && /tmp/BM-B04_so        # 40/40 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/BM-B04_as && /tmp/BM-B04_as   # 깨끗
```

## 7. 자주 하는 실수

**1. W1C 플래그를 `|=` 로 지움.** `EXTI->PR |= EXTI_PR_PR13;` → 읽은 순간 펜딩이던 모든 라인이 지워진다 (다른 벡터 몫까지). 고침: `EXTI->PR = EXTI_PR_PR13;`. "|= 가 안전한 습관" 이라는 직관이 정확히 거꾸로 작동하는 곳이다.

**2. rc_w0 플래그를 `&= ~` 로 지움.** `TIM2->SR &= ~TIM_SR_UIF;` → 대부분 동작하지만 읽기~쓰기 사이에 선 CC1IF/CC2IF 를 지운다. 고침: `TIM2->SR = ~TIM_SR_UIF;`. 그리고 이건 `__disable_irq()` 로 못 고친다 (Part B 의 `cs{SR &= ~UIF}` = {1}).

**3. 접근 타입을 섞어 외움.** W1C 식(`SR = UIF`)을 rc_w0 레지스터에 쓰면 UIF 외의 모든 비트에 0 을 쓴 셈 → 다른 플래그가 전부 지워지고 UIF 는 남는다 (연습 3 테스트: 0x7 → 0x2). 클리어 코드를 쓰기 전에 RM 의 접근 타입 칸(rc_w0 / rc_w1)을 확인한다. 같은 회사 안에서도 세대마다 다르다 (F4 는 TIM_SR rc_w0, 신형 USART 는 ISR + ICR(rc_w1) 분리).

**4. 메인과 ISR 이 같은 ODR 을 `|=` / `&=`.** 고침: 각자 BSRR. ISR 쪽 토글도 `ODR ^=` 대신 "ODR 읽기 + BSRR 쓰기" (연습 1, BM-B11). 핀 주인이 정해지면 그 주인만 그 핀을 쓴다.

**5. 임계 구역에서 무조건 `__enable_irq()`.**

```c
__disable_irq(); GPIOA->ODR |= P5; __enable_irq();          /* 바깥이 막아 둔 상태였다면 여기서 풀어 버린다 */
uint32_t p = __get_PRIMASK(); __disable_irq(); GPIOA->ODR |= P5; __set_PRIMASK(p);   /* 중첩 안전 */
```

테스트 "바깥 PRIMASK = 1 이면 나갈 때도 1, ISR 은 펜딩 유지" 가 이것을 잡는다 (C-M14).

**6. 공유 핸들러에서 PR 을 "전부" 지움.** `EXTI->PR = 0x7FFFFF;` 처럼 상수로 전부 지우면 읽은 뒤 도착한 라인과 다른 벡터의 라인까지 지운다. 고침: 내 라인 마스크로 스냅샷 → 스냅샷만 쓰기 (연습 2).

## 8. 면접 Q&A

<details>
<summary>Q1. `GPIOA->ODR |= (1 << 5);` 에 무슨 문제가 있나요? BSRR 은 왜 있죠?</summary>

C 한 줄이 LDR / ORR / STR 세 명령(RMW)이다. LDR 과 STR 사이에 ISR 이 같은 포트의 다른 핀을 바꾸면 메인이 낡은 값으로 덮어써 ISR 의 변경이 사라진다 (set 이 사라지거나 clear 가 되살아남). BSRR 은 "1 을 쓴 비트만 set / reset, 0 은 무시" 라서 **읽기 없이 쓰기 한 번**으로 원하는 핀만 바꾼다 — 그 자체가 원자적이다. 위 절반 BR, 아래 절반 BS, 같은 핀에 둘 다면 set 우선 (RM0090 기준). 꼬리: "여러 핀을 동시에?" → mask 안 핀들을 한 번에: `BSRR = ((mask & ~v) << 16) | (mask & v)`. "ODR 쓰기가 필요한 경우?" → 16 핀 전부를 한 번에 쓰는 포트(병렬 버스)이고 다른 문맥이 그 포트를 안 만질 때.

</details>

<details>
<summary>Q2. W1C 상태 비트를 `|=` 로 지우면 왜 안 되나요? `&= ~` 는요?</summary>

W1C 는 "1 을 쓴 비트를 지운다". `|=` 는 레지스터를 읽어(펜딩인 비트들이 1) 내 비트를 OR 해 다시 쓴다 → 읽을 때 1 이던 **모든** 비트에 1 을 쓰니 다 지워진다. 레이스가 아니라 항상 일어난다 (sweep 마스크 0x7). `&= ~bit` 은 내 비트에 0, 나머지 펜딩 비트에 1 을 써서 **남들만 지우고 내 것은 남긴다**. 정답은 `PR = bit` 한 줄. 꼬리: "W1C 는 왜 이런 설계?" → 소프트웨어가 읽기 없이 원하는 플래그만 지울 수 있고, 0 을 쓴 비트는 영향이 없어 하드웨어가 동시에 세우는 플래그와 충돌하지 않는다. PCIe RW1C, AHCI PxIS, 신형 STM32 ICR 이 같은 방식이다.

</details>

<details>
<summary>Q3. STM32 TIM_SR 는 rc_w0 인데, 왜 HAL 은 `SR = ~flag` 를 쓰죠? `SR &= ~flag` 도 결과는 같지 않나요?</summary>

조용할 때는 같다. 차이는 LDR 과 STR 사이에 하드웨어가 다른 플래그(CC1IF 등)를 세울 때다: `&=` 는 읽을 때 0 이던 그 비트에 0 을 써서 방금 선 플래그를 지운다 → 캡처 이벤트 유실 (sweep k = 1). `SR = ~flag` 는 지울 비트에만 0, 나머지엔 1(rc_w0 에서 무해)을 쓰고 읽기가 없어 창이 없다. 꼬리: "인터럽트를 끄면?" → 안 된다. PRIMASK 는 CPU 의 예외 진입만 막고, 타이머 하드웨어가 플래그를 세우는 것은 못 막는다 (sweep `cs{SR &= ~UIF}` 도 {1}). 예약(reserved) 비트에 1 을 쓰는 것이 걱정이면 RM 의 "reserved, must be kept at reset value" 문구를 확인 — HAL 은 `~flag` 를 그대로 쓴다 (확인 권장).

</details>

<details>
<summary>Q4. 메인과 ISR 이 공유하는 레지스터 RMW, 고치는 방법들을 비교해 보세요.</summary>

① set/clear 레지스터(BSRR, NVMe INTMS/INTMC, NVIC ISER/ICER): 쓰기 한 번, ISR·DMA·다른 코어 모두에 안전, 비용 최소 — 있으면 최우선. ② 임계 구역(`__get_PRIMASK` 저장 → `cpsid i` → RMW → 복원): 같은 코어의 ISR 에만 안전, 인터럽트 지연이 늘고, 하드웨어·DMA·다른 코어엔 무력. BASEPRI 로 일부 우선순위만 막는 변형은 ARM-M10. ③ 소유권 분리: 레지스터(또는 핀)를 한 문맥만 쓰고 다른 문맥은 요청 큐/플래그로 부탁. ④ 비트밴딩(M3/M4): 버스 수준 RMW 라 ISR 에 안전하지만 W1C 레지스터에선 다른 플래그를 지운다. ⑤ LDREX/STREX 는 RAM 변수용 (ARM-M11). 꼬리: "멀티코어 MCU(STM32H7 듀얼코어 등)?" → 하드웨어 세마포어(HSEM) 같은 칩 기능이나 set/clear 레지스터 — PRIMASK 는 내 코어만 막는다.

</details>

<details>
<summary>Q5. ISR 맨 끝에서 플래그를 지웠더니 ISR 이 한 번 더 불립니다. 왜죠?</summary>

Cortex-M 의 쓰기는 버퍼링되고(write buffer), 주변장치는 버스 브리지(AHB→APB) 너머에 있어 **STR 이 끝난 것처럼 보여도 주변장치에 도착하기 전**일 수 있다. 그 사이 예외 복귀가 일어나면 NVIC 는 여전히 서 있는 인터럽트 신호를 보고 같은 ISR 로 다시 들어간다 (tail-chain) — 두 번째 진입에선 플래그가 이미 0 이라 "헛 인터럽트(spurious)" 가 된다. 고침: 플래그를 **ISR 앞부분**에서 지우거나, 지운 뒤 같은 레지스터를 **다시 읽는다**(read-back — 같은 주변장치에 대한 읽기는 앞선 쓰기 뒤에 완료). `__DSB()` 는 코어 쪽 쓰기 완료를 기다리지만 브리지 너머 도착까지 보장하지 않는 구현이 있어 read-back 이 더 확실하다 (ARM·ST 커뮤니티에서 반복되는 사례, 칩별 확인 권장). 연습 4 가 이 "지연 쓰기" 를 모델로 재현한다: 끝에서 지우면 진입 2 번, read-back 또는 앞에서 지우면 1 번.

</details>

<details>
<summary>Q6. `volatile` 을 붙였는데 왜 원자적이지 않죠?</summary>

`volatile` 은 컴파일러에게 "이 접근을 없애거나 합치거나 순서를 바꾸지 마라" 만 말한다 (C11 5.1.2.3, 6.7.3). `x |= m` 은 여전히 읽기 하나 + 쓰기 하나다 — demo asm 의 `ldr` / `orrs` / `str`. 원자성은 하드웨어 기능(BSRR 같은 레지스터, LDREX/STREX, 비트밴딩)이나 인터럽트 마스킹으로만 얻는다. 꼬리: "C11 `_Atomic` 은?" → RAM 변수용이다. MMIO 에 `atomic_fetch_or` 를 쓰면 LDREX/STREX 루프가 나오는데 주변장치 레지스터에서의 exclusive 동작은 보장되지 않는다.

</details>

<details>
<summary>Q7. 여러 EXTI 라인이 한 벡터를 공유합니다. 핸들러를 어떻게 쓰나요?</summary>

`pending = EXTI->PR & (내 라인 마스크);` 로 **한 번** 읽고, `EXTI->PR = pending;` 로 읽은 것만 지운 뒤, pending 의 각 비트를 처리한다. 라인마다 PR 을 다시 읽고 `|=` 로 지우는 흔한 코드는 첫 라인을 지울 때 나머지(그리고 다른 벡터의 라인)까지 지워 이벤트를 잃는다 (Part C 표: 11·13·0 소실). 핸들러가 도는 중에 온 라인은 스냅샷에 없으니 펜딩으로 남고, NVIC 가 핸들러를 다시 부른다. 꼬리: "지우는 시점은?" → 엣지 트리거면 처리 **전**에 지워야 처리 중 재발생을 놓치지 않는다. 레벨형 원인(센서 INT 핀)은 원인을 해소한 뒤 지운다 — 아니면 바로 다시 선다.

</details>

<details>
<summary>Q8. BSRR 에 같은 핀의 set 과 reset 을 동시에 쓰면? 비트밴딩은 왜 W1C 에 위험하죠?</summary>

STM32F4 는 "BSx 와 BRx 가 둘 다 1 이면 BSx 우선" — 핀은 1 이 된다 (RM0090 GPIO BSRR 설명, 확인 권장. 다른 벤더는 다를 수 있으니 의존하지 않는 편이 낫다). 비트밴딩: Cortex-M3/M4 의 별칭 영역(주변장치 0x4000_0000 → 0x4200_0000 + 오프셋×32 + 비트×4)에 쓰면 코어가 대상 워드를 읽어 한 비트만 바꾸고 다시 쓴다. W1C 레지스터면 읽은 워드의 다른 1 들이 함께 쓰여 지워진다 — `|=` 버그와 같다. rc_w0 에 비트밴딩으로 0 을 쓰는 것은 다른 비트에 1 을 다시 쓰므로 대체로 무해하지만, 읽기와 쓰기 사이 하드웨어가 세운 비트의 처리는 칩 구현에 달려 있다 (확인 권장). 상세는 BM-M/A 레인.

</details>

## 9. 추가 연습

### 연습 1 — 토글: `ODR ^=` vs "ODR 읽기 + BSRR 쓰기"

핀 5 를 토글하는 동안 ISR 이 핀 6 을 켠다. `ODR ^= P5` 와 `odr = ODR; BSRR = ((odr & P5) << 16) | (~odr & P5)` 를 sweep 하라. 두 번째도 읽기가 있는데 왜 안전한가? (BM-B11 의 toggle 과 같은 구현)

<details>
<summary>답 보기</summary>

`ODR ^=` 는 k = 1 에서 P6 을 잃는다 (마스크 0x2). BSRR 토글도 ODR 을 읽지만, **쓰기가 P5 비트에만 효과가 있다** — BSRR 의 다른 비트는 0 이라 다른 핀을 건드리지 않는다. 그래서 ISR 이 P6 을 바꿔도 덮어쓰지 않는다 (마스크 0, 0→1·1→0 두 방향 모두). 남는 위험은 "같은 핀을 ISR 도 바꾸는" 경우뿐인데, 그건 레이스가 아니라 핀 주인 설계 문제다. 최신 ST HAL 의 `HAL_GPIO_TogglePin` 도 이 방식이다 (HAL 버전 확인 권장).

</details>

### 연습 2 — W1C "펜딩 전부 지우기"

처리 루프가 PR 을 읽어 처리한 뒤 `PR = 0x7FFFFF` 로 전부 지운다. 읽은 뒤(k = 1) 라인 11 이 오면? `PR = PR`(읽은 값 그대로 쓰기)과 비교하라.

<details>
<summary>답 보기</summary>

상수로 전부 지우면 읽은 뒤 도착한 라인 11 (아직 처리 안 함)까지 지워 PR = 0 → 이벤트 유실. 읽은 값을 그대로 쓰면(W1C 에서 `PR = PR` 은 "읽은 것만 지우기" 라는 뜻이 된다) PR = 0x0800 으로 남아 다음 진입에서 처리된다. 쓰기 **뒤**(k = 2)에 오면 두 방법 다 괜찮다 — 창은 읽기~쓰기 사이다. W1C 에서 RMW 를 안 하는 것이 아니라 "읽은 값을 수정 없이 쓰는" 것은 의미가 있다는 점이 재밌다: 여기서 수정(OR)이 버그의 원인이었다.

</details>

### 연습 3 — 접근 타입별 범용 `flag_clear(type, addr, mask)`

rc_w1 / rc_w0 / rc_r / rw 네 타입을 받아 mask 비트만 지우는 함수를 써라. rc_r(읽으면 전부 0 이 되는 상태 레지스터)은 하드웨어에서 선택적으로 지울 수 없다 — 어떻게 하나? W1C 식 쓰기를 rc_w0 레지스터에 실수로 하면?

<details>
<summary>답 보기</summary>

rc_w1: `REG_WRITE(addr, mask)`. rc_w0: `REG_WRITE(addr, ~mask)`. rw: RMW (`REG_READ & ~mask` 쓰기 — 다른 문맥이 그 레지스터를 쓰면 임계 구역 필요). rc_r: 읽는 순간 전부 지워지므로 읽은 값을 **소프트웨어 shadow 에 OR 로 모아 두고** shadow 에서 mask 를 지운다 — 읽고 버리면 다른 플래그를 잃는다 (테스트: 0x7 읽고 0x2 지움 → shadow 0x5, 다음에 0x8 → 0x1 지움 → shadow 0xC). 실수: rc_w0 에 `SR = 0x2` 를 쓰면 비트 1 외 전부 0 을 쓴 셈 → 0x7 이 0x2 가 된다 (지우려던 비트만 남는다).

</details>

### 연습 4 — ISR 끝에서 지우면 생기는 재진입 (지연 쓰기 모델)

mock 에 "쓰기는 다음 버스 접근 때 도착한다" 옵션을 켠다. 예외 복귀 때 NVIC 는 도착한 PR 만 본다. ISR 이 (a) 일하고 끝에서 지움, (b) 끝에서 지우고 read-back, (c) 처음에 지우고 일함 — 각 몇 번 진입하나?

<details>
<summary>답 보기</summary>

(a) 2 번: 지우는 쓰기가 아직 버퍼에 있는 상태로 복귀 → PR 이 여전히 1 → 재진입. 두 번째 진입의 첫 접근이 쓰기를 밀어내 PR = 0 이 되고 끝난다 — 헛 진입 1 회 (실기에서는 "인터럽트가 두 배로 센다" 로 보인다). (b) 1 번: 같은 레지스터를 읽으면 앞선 쓰기가 먼저 끝난다. (c) 1 번: 지운 뒤 일하는 동안의 접근이 쓰기를 밀어 준다. 실칩에서 쓰기 도착 지연은 버스 구조·클럭 비(AHB/APB 프리스케일러)에 따라 다르다 — 모델은 "적어도 다음 접근까지" 로 단순화했다. Q5 참고.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-B04_pr && /tmp/BM-B04_pr   # 16/16 passed
```

## 10. 핵심 정리

- C 의 `reg |= m` 은 LDR/ORR/STR 세 명령이다. `volatile` 은 원자성을 주지 않는다.
- W1C(rc_w1): 지울 비트만 1 → `PR = bit`. `|=` 는 읽은 펜딩 **전부** 지운다(항상), `&= ~` 는 남의 것만 지운다.
- rc_w0: 지울 비트만 0 → `SR = ~bit`. `&= ~bit` 은 읽기~쓰기 사이 하드웨어가 세운 플래그를 지운다 — 인터럽트를 꺼도 못 막는다.
- 메인·ISR 이 공유하는 출력은 BSRR(쓰기 한 번, set 우선)로. 여러 핀은 `BSRR = ((m & ~v) << 16) | (m & v)`.
- 임계 구역은 이전 PRIMASK 를 저장·복원 (중첩 안전). 같은 코어 ISR 만 막는다 — 하드웨어·DMA·다른 코어는 못 막는다.
- 공유 EXTI 핸들러: 한 번 읽고(내 마스크), 읽은 것만 W1C, 그 다음 처리. 늦게 온 라인은 펜딩으로 남겨 재진입시킨다.
- ISR 끝에서 플래그를 지우면 쓰기 지연으로 헛 재진입 가능 → 앞에서 지우거나 read-back.
- 테스트는 결과뿐 아니라 **접근 로그**(R 이 없는지, W 한 번인지)와 **모든 끼어들기 지점 sweep** 으로 한다.

## 참고자료

- ST RM0090 *STM32F405/415, 407/417, 427/437, 429/439 Reference Manual* — 1.1 "List of abbreviations for registers" (rw, r, w, rc_w1, rc_w0, rc_r, rs 정의), 8.4.7 GPIOx_BSRR (BS/BR, "BSx has priority"), 12.3.6 EXTI_PR (rc_w1), 18.4.5 TIMx_SR (rc_w0) — 절 번호는 판(revision)에 따라 다를 수 있음, 확인 권장 — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>
- STM32CubeF4 HAL 소스: `stm32f4xx_hal_gpio.c` (`HAL_GPIO_WritePin`, `HAL_GPIO_TogglePin`), `stm32f4xx_hal_gpio.h` (`__HAL_GPIO_EXTI_CLEAR_IT`), `stm32f4xx_hal_tim.h` (`__HAL_TIM_CLEAR_FLAG`) — <https://github.com/STMicroelectronics/STM32CubeF4>
- ARMv7-M Architecture Reference Manual (ARM DDI 0403) — 메모리 타입(Device), 예외 모델, CPS / MRS / MSR (PRIMASK) — <https://developer.arm.com/documentation/ddi0403/latest/>
- ARM Cortex-M4 Technical Reference Manual (DDI 0439) — Bit-banding, 쓰기 버퍼 — <https://developer.arm.com/documentation/ddi0439/latest/>
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.) — 비트밴드 RMW, 인터럽트 플래그 클리어 시점과 DSB 논의
- PCI Express Base Specification — 레지스터 속성 RW1C / RW1CS; NVM Express Base Specification — INTMS / INTMC (set/clear 쌍 레지스터)
- 관련 레슨: BM-B02 (mock 프레임워크 전체판), BM-B03 (RCC 클럭 enable 후 dummy read), BM-B11 (GPIO 드라이버, BSRR toggle), BM-B12 (USART SR 플래그), BM-B14 (TIM UIF·UG), C-M14 (ISR↔메인 공유 데이터, PRIMASK 저장/복원), ARM-M10 (PRIMASK/BASEPRI), ARM-M11 (LDREX/STREX), BM-M03 (EXTI·디바운스), SIV-B01 (레지스터 헤더 생성 — 접근 타입 필드)
