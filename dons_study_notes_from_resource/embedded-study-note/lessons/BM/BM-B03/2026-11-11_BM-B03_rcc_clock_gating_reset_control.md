---
id: BM-B03
time: 60분
updated: 2026-11-11
---
# BM-B03 RCC: 주변장치 클럭 게이팅과 리셋 제어

## 학습 목표

- 주변장치(peripheral)마다 RCC 의 enable 비트(xENR)와 리셋 비트(xRSTR)가 있다는 것, 그리고 **표(버스, 비트)** 하나로 `periph_clock_enable(id)` / `periph_reset_pulse(id)` 를 만드는 방법을 안다.
- "클럭 안 켜고 쓰기" 가 왜 **조용히** 실패하는지(쓰기 무시, 읽기 0 — 예외도 없음) mock 으로 재현하고, 디버깅 순서(ENR 먼저 확인)를 말할 수 있다.
- enable 직후 바로 접근하면 첫 쓰기가 사라지는 해저드(STM32 errata "Delay after an RCC peripheral clock enabling")를 모델로 재현하고, **ENR 되읽기(dummy read)** 가 왜 고치는지, NOP·DSB 우회책과 무엇이 다른지 설명할 수 있다.
- 클럭 게이팅(clock gating)과 리셋의 차이 — 끄면 값이 남고, 리셋해야 리셋값으로 돌아간다 — 를 테스트로 보이고 `deinit = 리셋 펄스 + 클럭 끄기` 를 쓸 수 있다.
- ENR 을 `=` 로 쓰면 다른 비트(CCM RAM 클럭, 이미 켠 GPIOB)가 꺼진다는 것, GPIOA 를 통째로 덮으면 SWD 핀이 죽는다는 것을 안다.

## 1. 문제

보드 브링업(bring-up) 첫날 "GPIO 를 썼는데 LED 가 안 켜져요 / UART 가 아무것도 안 내보내요" 의 1순위 원인은 **RCC 에서 그 주변장치 클럭을 안 켠 것**이고, 면접에서는 "그럼 HAL 은 왜 enable 직후에 ENR 을 한 번 더 읽나요?" 가 바로 따라 나온다. SSD 컨트롤러 브링업에서 블록마다 clock/reset 컨트롤러를 풀어 주던 일, RF 모듈 보드에서 SPI 가 먹통이던 일과 같은 계열이다 — 칩 안의 블록은 기본적으로 **꺼져 있고 리셋에 묶여 있다**.

Cortex-M MCU 는 전력 때문에 주변장치 클럭을 기본으로 꺼 둔다 (동적 전력 ∝ C·V²·f — 클럭이 안 들어가면 그 블록의 플립플롭은 토글하지 않는다). STM32F4 에서는 RCC(Reset and Clock Control) 블록의 `AHB1ENR` / `APB1ENR` / `APB2ENR` 비트가 버스별로 주변장치 클럭 게이트를 열고, `AHB1RSTR` / `APB1RSTR` / `APB2RSTR` 비트가 그 주변장치를 리셋에 묶었다 푼다. 클럭이 없는 주변장치의 레지스터는 **쓰기가 무시되고 읽으면 0** 이 나온다 (STM32 에서 흔히 관찰되는 동작 — 이 레슨 mock 의 정의. 칩 계열마다 다르다: 어떤 MCU 는 버스 폴트를 낸다 → 면접 Q&A 2).

```svg
<svg viewBox="0 0 740 305" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">그림 1. 실제 칩의 클럭 게이트·리셋 (위) 와 이 레슨의 mock (아래)</text>
  <rect x="10" y="40" width="100" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="58" fill="currentColor" font-weight="bold">Cortex-M4</text>
  <text x="20" y="75" fill="currentColor" font-family="monospace">LDR / STR</text>
  <rect x="150" y="40" width="130" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="160" y="58" fill="currentColor" font-weight="bold">버스 매트릭스</text>
  <text x="160" y="75" fill="currentColor">AHB1 (HCLK)</text>
  <path d="M110 62 H150 M280 56 H430" stroke="currentColor" fill="none"/>
  <text x="300" y="50" fill="currentColor">레지스터 접근</text>
  <rect x="430" y="40" width="150" height="80" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="440" y="58" fill="currentColor" font-weight="bold">GPIOA 레지스터</text>
  <text x="440" y="76" fill="currentColor" font-family="monospace">MODER ODR BSRR</text>
  <text x="440" y="94" fill="currentColor" font-family="monospace">0x4002_0000</text>
  <text x="440" y="113" fill="currentColor">CLK ◀</text>
  <text x="300" y="98" fill="currentColor">HCLK</text>
  <path d="M300 102 H372" stroke="currentColor" fill="none"/>
  <path d="M372 96 h12 a14 14 0 0 1 0 28 h-12 z" fill="none" stroke="currentColor"/>
  <text x="376" y="114" fill="currentColor" font-size="12">&amp;</text>
  <path d="M398 110 H430" stroke="currentColor" fill="none"/>
  <text x="150" y="130" fill="currentColor" font-weight="bold">RCC</text>
  <rect x="150" y="135" width="200" height="50" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="160" y="155" fill="currentColor" font-family="monospace">AHB1ENR.bit0 GPIOAEN</text>
  <text x="160" y="175" fill="currentColor" font-family="monospace">AHB1RSTR.bit0 GPIOARST</text>
  <path d="M350 151 H362 V118 H372" stroke="currentColor" fill="none"/>
  <path d="M350 171 H505 V120" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
  <text x="362" y="191" fill="currentColor">리셋 (RSTR=1 동안 유지)</text>
  <text x="595" y="58" fill="currentColor">EN=0 이면</text>
  <text x="595" y="76" fill="currentColor">· 쓰기 무시</text>
  <text x="595" y="94" fill="currentColor">· 읽기 0, 예외 없음</text>
  <text x="595" y="112" fill="currentColor">· 저장값은 유지</text>
  <text x="595" y="140" fill="currentColor">APB1/APB2 도 같은 구조</text>
  <text x="595" y="158" fill="currentColor">(APB1ENR, APB1RSTR …)</text>
  <line x1="10" y1="198" x2="730" y2="198" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="10" y="212" width="130" height="58" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="232" fill="currentColor" font-weight="bold">드라이버</text>
  <text x="20" y="250" fill="currentColor" font-family="monospace">periph_clock_*</text>
  <text x="20" y="264" fill="currentColor" font-family="monospace">board_led_init</text>
  <rect x="160" y="212" width="120" height="58" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="170" y="236" fill="currentColor" font-family="monospace">REG_READ</text>
  <text x="170" y="256" fill="currentColor" font-family="monospace">REG_WRITE</text>
  <rect x="300" y="212" width="140" height="58" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="310" y="232" fill="currentColor" font-weight="bold">mock core</text>
  <text x="310" y="250" fill="currentColor">레지스터 표 · 로그</text>
  <text x="310" y="264" fill="currentColor">접근 1회 = cycle++</text>
  <rect x="460" y="208" width="270" height="88" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="470" y="226" fill="currentColor" font-weight="bold">주소별 훅 (g_rcc 상태)</text>
  <text x="470" y="244" fill="currentColor">ENR 쓰기: 0→1 비트 ready_at = c+D+1</text>
  <text x="470" y="260" fill="currentColor">ENR 읽기: 대기 중 enable → 다음부터</text>
  <text x="470" y="276" fill="currentColor">RSTR 쓰기: 리셋값 복원 + 쓰기 잠금</text>
  <text x="470" y="292" fill="currentColor">주변장치: 클럭 없으면 읽기 0·쓰기 무시</text>
  <path d="M140 241 H160 M280 241 H300 M440 241 H460" stroke="currentColor" fill="none"/>
</svg>
```

이 레슨은 맥에서 돈다. BM-B02 의 mock 어휘(`REG_READ/REG_WRITE` → `mock_read32/mock_write32` → 주소별 훅, 접근 로그)를 축약판으로 담고, 그 위에 **RCC + GPIOA/GPIOB + USART2** 모델을 올린다. 주소·오프셋·리셋값은 RM0090 (STM32F405/407) 기준 대표값이다 — **RM 확인 권장**. 테스트의 정답은 mock 자체의 정의다.

| 레지스터 (RM0090 기준 대표값) | 주소 | 리셋값 | 이 레슨에서 쓰는 비트 |
|---|---|---|---|
| `RCC_AHB1RSTR` | 0x4002 3810 | 0 | bit0 GPIOARST, bit1 GPIOBRST |
| `RCC_APB1RSTR` / `APB2RSTR` | 0x4002 3820 / 3824 | 0 | APB1 bit17 USART2RST, bit0 TIM2RST / APB2 bit4 USART1RST |
| `RCC_AHB1ENR` | 0x4002 3830 | **0x0010 0000** | bit0 GPIOAEN, bit1 GPIOBEN, (bit20 CCMDATARAMEN = 1) |
| `RCC_APB1ENR` / `APB2ENR` | 0x4002 3840 / 3844 | 0 | APB1 bit17 USART2EN, bit0 TIM2EN / APB2 bit4 USART1EN |
| `GPIOA_MODER` / `OSPEEDR` / `PUPDR` | 0x4002 0000 / 08 / 0C | 0xA800 0000 / 0x0C00 0000 / 0x6400 0000 | PA13/14 = SWD (AF) |
| `GPIOB_MODER` / `OSPEEDR` / `PUPDR` | 0x4002 0400 / 08 / 0C | 0x0000 0280 / 0x0000 00C0 / 0x0000 0100 | PB3/PB4 = JTAG (AF) |
| `USART2_SR` / `BRR` / `CR1` | 0x4000 4400 / 08 / 0C | 0x0000 00C0 / 0 / 0 | SR: TXE(7)·TC(6) = 1 |

접근 유형(access type): ENR·RSTR 는 rw, GPIO MODER/ODR 는 rw, IDR 는 r, BSRR 는 w (읽으면 0).

### Part A — 클럭 게이팅 모델과 enable/disable

- TODO 1 `clk_effective(id)` — 그 주변장치에 지금 클럭이 들어오는가: ENR 비트 = 1 **그리고** 현재 버스 사이클 ≥ `ready_at[id]`. 주변장치 레지스터 훅(`periph_on_read/write`, 주어짐)이 이 함수로 게이팅한다: 클럭 없으면 읽기 0, 쓰기 무시 (`dropped++`).
- TODO 5·6 `periph_clock_enable(id)` / `periph_clock_disable(id)` — 표 `PERIPH[id] = {name, bus, bit, base}` 로 ENR 주소와 비트를 찾아 **RMW** 로 그 비트만 바꾼다. enable 은 끝에 ENR 을 한 번 되읽는다. 잘못된 id → -1.
- 테스트: 클럭 없이 쓰기 무시·읽기 0, enable 후 정상, `CCMDATARAMEN`(bit20) 보존, 무작위로 채운 ENR 3개 × 5 주변장치 × 200회에서 자기 비트만 바뀜, 끄고 접근(무시) → 다시 켜면 **예전 값 그대로**.

### Part B — "켜자마자 접근" 해저드와 되읽기

- 모델: ENR 비트가 0→1 이 되는 쓰기(사이클 c) 뒤 **D 사이클 동안은 클럭이 아직 안 섰다** → `ready_at = c + D + 1`. 모델 D 값은 errata 문구를 흉내 낸 AHB1 = 2, APB1 = 5 (= 1 + 프리스케일러 4), APB2 = 3 (= 1 + 2).
- TODO 2 `rcc_enr_on_write` — 0→1 비트에만 `ready_at` 설정 (이미 켜진 비트는 다시 기다리지 않는다).
- TODO 3 `rcc_enr_on_read` — 되읽기 = 동기화: 그 버스에서 대기 중인 enable 을 **다음 접근부터** 유효하게.
- 테스트: 되읽기 없는 enable 뒤 MODER 쓰기 2번 소실 / D = 0…8 스윕에서 "되읽기 없음 → 정확히 D 번 소실, 되읽기 → 0 번" / USART2 의 BRR·CR1 이 둘 다 사라지는 증상 / 같은 ENR 의 다른 비트를 켜도 이미 켠 GPIOA 는 영향 없음 / 엉뚱한 레지스터(그 주변장치 자신의 IDR)를 더미로 읽는 것은 동기화가 아님.

### Part C — 리셋 펄스와 보드 초기화

- TODO 4 `rcc_rstr_on_write` — RSTR 비트가 1 이면 그 주변장치의 레지스터 전부를 리셋값으로 되돌리고 `in_reset = true` (쓰기 무시), 0 이면 해제.
- TODO 7 `periph_reset_pulse(id)` — RSTR 비트 RMW set → RMW clear. 다른 RSTR 비트(일부러 잡아 둔 리셋)는 보존.
- TODO 8 `board_led_init()` — 부트로더가 GPIOA 를 어지럽혀 놓았어도 항상 같은 결과: 클럭 enable(+되읽기) → 리셋 펄스 → `MODER5 = 01` (다른 핀 보존 — PA13/14 SWD!) → `BSRR = 1<<5` 로 켜기. 접근 로그 10개가 정확한 순서인지 확인.

## 2. Problem (English)

> **RCC clock gating and peripheral reset on a mocked STM32F4.**
>
> Peripherals are described by a table `{name, bus, bit, base}` (GPIOA = AHB1 bit 0, GPIOB = AHB1 bit 1, USART2 = APB1 bit 17, TIM2 = APB1 bit 0, USART1 = APB2 bit 4). All register access goes through `REG_READ(addr)` / `REG_WRITE(addr, val)`, which in the host build call a mock with per-address hooks and an access log; one access = one bus cycle.
>
> 1. Model: a peripheral register write is ignored and a read returns 0 unless its clock is effective. A 0→1 write to an `xENR` bit at cycle *c* makes the clock effective at cycle *c + D + 1* (D per bus: AHB1 2, APB1 5, APB2 3), unless the CPU reads that `xENR` register back, which makes it effective from the next access. Writing 1 to an `xRSTR` bit restores the peripheral's reset values and ignores writes until the bit is cleared.
> 2. `int periph_clock_enable(periph_id_t id)`, `int periph_clock_disable(periph_id_t id)`, `int periph_reset_pulse(periph_id_t id)` — read-modify-write only the peripheral's bit; return -1 for an invalid id without touching the bus.
> 3. `int board_led_init(void)` — PA5 push-pull output, LED on, from any prior GPIOA state, without disturbing other pins.
>
> Example: `RCC_AHB1ENR` reset = `0x00100000`. `periph_clock_enable(GPIOA)` → log `R AHB1ENR=0x00100000, W AHB1ENR=0x00100001, R AHB1ENR=0x00100001`; the very next `MODER` write lands. Without the read-back, the next 2 `MODER` writes are dropped. `board_led_init()` on a dirty port → `MODER = 0xA8000400`, `ODR = 0x00000020`, 10 bus accesses.
>
> Follow-ups: why read back the register you just wrote? Why `|=` and not `=`? What survives a clock disable? Why does this bug vanish when you single-step in the debugger?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/BM/BM-B03
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/BM-B03_st && /tmp/BM-B03_st
# 처음엔 "14/47 passed" (mock 자체·버그 재현·잘못된 id 테스트는 시작 코드로도 통과) — TODO 를 채워 47/47
/tmp/BM-B03_st -v      # 정답 코드로 돌리면 접근 로그 trace 표가 나온다 (5. 풀이의 표)
```

TODO 8개: Part A `clk_effective`, `periph_clock_enable`, `periph_clock_disable` / Part B `rcc_enr_on_write`, `rcc_enr_on_read` / Part C `rcc_rstr_on_write`, `periph_reset_pulse`, `board_led_init`. 스텁은 "게이팅 없는 모델 + 아무것도 안 하는 드라이버" 라 멈추거나 죽지 않는다.

관찰용 파일: `demo_target.c` — 같은 드라이버 패턴을 Cortex-M4 로 컴파일해 되읽기 `ldr` 이 실제로 남는지 본다.

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -Wall -Wextra -Werror -O2 -S demo_target.c -o -
cc -std=c11 -Wall -Wextra -Werror -O2 -c demo_target.c -o /tmp/BM-B03_demo.o     # 호스트에서도 경고 0 (실행 파일 아님)
```

## 4. 힌트

<details>
<summary>힌트 1 — Part A: 게이트는 AND 두 개</summary>

`clk_effective` = (ENR 저장값의 그 비트) AND (지금 사이클 ≥ ready_at). ENR 은 **백도어** `mock_peek` 로 읽어야 한다 — `REG_READ` 로 읽으면 그 자체가 버스 접근이라 사이클이 흐르고, 되읽기 훅까지 불려 모델이 스스로를 동기화해 버린다.

</details>

<details>
<summary>힌트 2 — Part A: 표 한 줄이면 주소와 비트가 다 나온다</summary>

`BUS_ENR[PERIPH[id].bus]` 가 ENR 주소, `1u << PERIPH[id].bit` 가 마스크. 쓰기는 `REG_WRITE(enr, REG_READ(enr) | mask)` — 읽고, 고치고, 쓴다. id 검사는 버스에 손대기 **전에**: `(unsigned)id >= PERIPH_COUNT` 하나로 음수까지 잡힌다.

</details>

<details>
<summary>힌트 3 — Part B: "올라간 비트" 만</summary>

`rising = val & ~old`. 이미 1 이던 비트를 다시 기다리게 만들면, GPIOB 를 켜는 순간 잘 돌던 GPIOA 가 2 사이클 동안 죽는 모델이 된다 (테스트가 잡는다). 읽기 훅은 `stored` 에서 1 인 비트 중 `ready_at > cycle + 1` 인 것만 `cycle + 1` 로 당긴다.

</details>

<details>
<summary>힌트 4 — Part C: 리셋은 "값 복원 + 잠금"</summary>

`mock_restore_range(base, base + 0x400)` 가 그 주변장치 레지스터를 리셋값으로 되돌린다. `in_reset[id] = (val >> bit) & 1` 을 **그 버스의 모든 주변장치에 대해** 갱신해야 GPIOB 를 잡아 둔 채 GPIOA 를 펄스해도 GPIOB 가 풀리지 않는다 (RMW 로 써도 GPIOB 비트는 1 로 다시 써지니까).

</details>

<details>
<summary>힌트 5 — Part C: LED 초기화는 네 줄</summary>

`periph_clock_enable` → `periph_reset_pulse` → MODER RMW (`& ~(3u << 10) | (1u << 10)`) → `BSRR = 1u << 5`. 로그 순서: ENR R/W/R, RSTR R/W/R/W, MODER R/W, BSRR W = 10.

</details>

## 5. 풀이

### Part A — 게이팅 모델: 무엇이 "조용히" 실패하나

```c
static bool clk_effective(periph_id_t id) {
    const periph_info_t *p = &PERIPH[id];
    if (!((mock_peek(BUS_ENR[p->bus]) >> p->bit) & 1u)) return false;   /* 게이트 닫힘 */
    return g_mock.cycle >= g_rcc.ready_at[id];                          /* 열었지만 아직 안 섰나 */
}
```

주변장치 훅이 이걸로 판정한다: 읽기는 0 을 돌려주고(`zero_reads++`), 쓰기는 **이전 저장값을 그대로 돌려줘서** 무시한다(`dropped++`). 저장값 자체는 지우지 않는다 — 실제 칩에서 클럭이 멈춘 플립플롭이 값을 유지하는 것과 같다. 그래서 "끄고 접근 → 다시 켜면 예전 값" 테스트가 성립한다: **게이팅은 리셋이 아니다.**

드라이버는 표 하나로 끝난다:

```c
int periph_clock_enable(periph_id_t id) {
    if ((unsigned)id >= PERIPH_COUNT) return -1;
    uint32_t enr = BUS_ENR[PERIPH[id].bus];
    REG_WRITE(enr, REG_READ(enr) | (1u << PERIPH[id].bit));
    (void)REG_READ(enr);                       /* 되읽기: 위 쓰기가 끝난 뒤에야 돌아온다 */
    return 0;
}
```

`|=` 여야 하는 이유는 리셋값에 있다. `RCC_AHB1ENR` 의 리셋값은 0 이 아니라 **0x0010 0000** — bit20 `CCMDATARAMEN` (64 KB CCM 데이터 RAM 클럭)이 기본으로 켜져 있다 (RM0090 기준, 확인 권장). `AHB1ENR = GPIOAEN` 으로 대입하면 CCM RAM 클럭이 꺼지고, 링커 스크립트가 스택이나 버퍼를 CCM(0x1000 0000)에 두었다면 그 다음 push 에서 결과가 이상해진다. 테스트 `naive_enable_assign` 이 0x00100002 → 0x00000001 (GPIOB 와 bit20 소실)을 보여 준다.

```svg
<svg viewBox="0 0 740 236" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">그림 3. RCC_AHB1ENR 비트 (RM0090 기준 대표값) — |= 와 = 의 차이</text>
  <text x="29" y="40" fill="currentColor" text-anchor="middle">31</text><text x="271" y="40" fill="currentColor" text-anchor="middle">20</text><text x="535" y="40" fill="currentColor" text-anchor="middle">8</text><text x="689" y="40" fill="currentColor" text-anchor="middle">1</text><text x="711" y="40" fill="currentColor" text-anchor="middle">0</text>
  <text x="18" y="58" fill="currentColor">리셋값 0x00100000 — bit20 CCMDATARAMEN = 1 (CCM RAM 클럭이 기본 ON)</text>
  <rect x="18" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="29" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="40" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="51" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="62" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="73" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="84" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="95" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="106" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="117" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="128" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="139" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="150" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="161" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="172" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="183" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
  <rect x="194" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="205" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="216" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="227" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="238" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="249" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="260" y="64" width="22" height="22" class="f-acc" stroke="currentColor"/><text x="271" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">1</text><rect x="282" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="293" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="304" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="315" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="326" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="337" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="348" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="359" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
  <rect x="370" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="381" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="392" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="403" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="414" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="425" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="436" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="447" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="458" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="469" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="480" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="491" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="502" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="513" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="524" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="535" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
  <rect x="546" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="557" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="568" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="579" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="590" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="601" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="612" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="623" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="634" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="645" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="656" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="667" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="678" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="689" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="700" y="64" width="22" height="22" fill="none" stroke="currentColor"/><text x="711" y="80" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
  <text x="18" y="108" fill="currentColor">AHB1ENR |= GPIOAEN → 0x00100001 (bit0 만 바뀜)</text>
  <rect x="18" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="29" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="40" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="51" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="62" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="73" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="84" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="95" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="106" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="117" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="128" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="139" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="150" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="161" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="172" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="183" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
  <rect x="194" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="205" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="216" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="227" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="238" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="249" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="260" y="114" width="22" height="22" class="f-acc" stroke="currentColor"/><text x="271" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">1</text><rect x="282" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="293" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="304" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="315" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="326" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="337" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="348" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="359" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
  <rect x="370" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="381" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="392" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="403" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="414" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="425" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="436" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="447" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="458" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="469" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="480" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="491" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="502" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="513" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="524" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="535" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
  <rect x="546" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="557" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="568" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="579" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="590" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="601" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="612" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="623" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="634" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="645" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="656" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="667" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="678" y="114" width="22" height="22" fill="none" stroke="currentColor"/><text x="689" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="700" y="114" width="22" height="22" class="f-ok" stroke="currentColor"/><text x="711" y="130" fill="currentColor" text-anchor="middle" font-family="monospace">1</text>
  <text x="18" y="158" fill="currentColor">AHB1ENR = GPIOAEN → 0x00000001 (bit20 까지 지움: CCM RAM 클럭 OFF)</text>
  <rect x="18" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="29" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="40" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="51" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="62" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="73" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="84" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="95" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="106" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="117" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="128" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="139" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="150" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="161" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="172" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="183" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
  <rect x="194" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="205" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="216" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="227" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="238" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="249" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="260" y="164" width="22" height="22" class="f-warn" stroke="currentColor"/><text x="271" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="282" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="293" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="304" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="315" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="326" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="337" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="348" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="359" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
  <rect x="370" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="381" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="392" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="403" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="414" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="425" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="436" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="447" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="458" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="469" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="480" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="491" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="502" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="513" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="524" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="535" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
  <rect x="546" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="557" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="568" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="579" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="590" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="601" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="612" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="623" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="634" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="645" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="656" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="667" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="678" y="164" width="22" height="22" fill="none" stroke="currentColor"/><text x="689" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">0</text><rect x="700" y="164" width="22" height="22" class="f-acc" stroke="currentColor"/><text x="711" y="180" fill="currentColor" text-anchor="middle" font-family="monospace">1</text>
  <text x="18" y="210" fill="currentColor">bit0 GPIOAEN · bit1 GPIOBEN … bit8 GPIOIEN · bit12 CRCEN · bit18 BKPSRAMEN</text>
  <text x="18" y="228" fill="currentColor">bit20 CCMDATARAMEN · bit21 DMA1EN · bit22 DMA2EN</text>
</svg>
```

무작위 회귀 테스트: ENR 3개를 xorshift32 로 채우고 5 주변장치 각각 enable → disable 을 200 회. 5 × 200 × (enable, disable) = 2000 번의 연산마다 ENR 3개를 비교해 "자기 버스의 자기 비트만 바뀌고 다른 두 ENR 은 그대로" 가 모두 성립한다 (`bad == 0`).

### Part B — enable 직후 접근: 모델과 trace

STM32F4 errata(ES0182 류, "Delay after an RCC peripheral clock enabling" — **기억 기준, 문구·번호 확인 권장**)는 대략 이렇게 말한다: RCC 에서 클럭을 켠 뒤 주변장치가 실제로 켜지기까지 지연이 있다 — AHB 주변장치는 2 AHB 사이클, APB 주변장치는 1 + (AHB/APB 프리스케일러) 사이클. 우회책은 ① DSB, ② NOP n 개, ③ 해당 레지스터를 한 번 더미로 읽기. 이 레슨의 모델은 그 문장을 "버스 접근 1회 = 1 사이클" 단위로 옮긴 것이다 (실제 HCLK/PCLK 사이클과 1:1 은 아니다 — **모델**).

| 규칙 | 모델 정의 |
|---|---|
| ENR 비트 0→1 쓰기 (사이클 c) | `ready_at = c + D + 1` — 다음 D 번의 접근은 아직 클럭 없음 |
| 이미 1 인 비트 다시 쓰기 | 변화 없음 (다시 기다리지 않음) |
| ENR 읽기 (사이클 r) | 그 버스의 대기 중 enable → `ready_at = r + 1` (다음 접근부터 유효) |
| ENR 비트 1→0 | 즉시 꺼짐 |
| D (모델 값) | AHB1 2, APB1 5 (/4), APB2 3 (/2) |

`./solution -v` 실제 출력 (D_AHB1 = 2):

```text
-- enable_no_sync(GPIOA) then MODER writes, D_AHB1=2
  cyc op register       value       note
    1  R RCC_AHB1ENR    0x00100000  
    2  W RCC_AHB1ENR    0x00100001  
    3  W GPIOA_MODER    0xA8000400  IGNORED (no clock / in reset)
    4  W GPIOA_MODER    0xA8000400  IGNORED (no clock / in reset)
    5  W GPIOA_MODER    0xA8000400  
-- periph_clock_enable(GPIOA) then MODER write, D_AHB1=2
  cyc op register       value       note
    1  R RCC_AHB1ENR    0x00100000  
    2  W RCC_AHB1ENR    0x00100001  
    3  R RCC_AHB1ENR    0x00100001  
    4  W GPIOA_MODER    0xA8000400  
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">그림 2. enable 직후 접근 (모델 D_AHB1 = 2, solution -v 의 실제 로그와 같은 사이클)</text>
  <text x="130" y="42" fill="currentColor" text-anchor="middle">cyc 1</text>
  <text x="250" y="42" fill="currentColor" text-anchor="middle">cyc 2</text>
  <text x="370" y="42" fill="currentColor" text-anchor="middle">cyc 3</text>
  <text x="490" y="42" fill="currentColor" text-anchor="middle">cyc 4</text>
  <text x="610" y="42" fill="currentColor" text-anchor="middle">cyc 5</text>
  <text x="10" y="64" fill="currentColor" font-weight="bold">되읽기 없음</text>
  <rect x="75" y="72" width="110" height="36" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="130" y="94" fill="currentColor" text-anchor="middle" font-family="monospace">R AHB1ENR</text>
  <rect x="195" y="72" width="110" height="36" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="250" y="94" fill="currentColor" text-anchor="middle" font-family="monospace">W AHB1ENR</text>
  <rect x="315" y="72" width="110" height="36" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="370" y="94" fill="currentColor" text-anchor="middle" font-family="monospace">W MODER 무시</text>
  <rect x="435" y="72" width="110" height="36" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="490" y="94" fill="currentColor" text-anchor="middle" font-family="monospace">W MODER 무시</text>
  <rect x="555" y="72" width="110" height="36" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="610" y="94" fill="currentColor" text-anchor="middle" font-family="monospace">W MODER 반영</text>
  <path d="M310 118 H550" stroke="currentColor" fill="none"/>
  <path d="M310 113 V123 M550 113 V123" stroke="currentColor" fill="none"/>
  <text x="430" y="136" fill="currentColor" text-anchor="middle">클럭 아직 없음: ready_at = 2 + D + 1 = 5</text>
  <text x="10" y="166" fill="currentColor" font-weight="bold">되읽기 있음</text>
  <rect x="75" y="174" width="110" height="36" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="130" y="196" fill="currentColor" text-anchor="middle" font-family="monospace">R AHB1ENR</text>
  <rect x="195" y="174" width="110" height="36" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="250" y="196" fill="currentColor" text-anchor="middle" font-family="monospace">W AHB1ENR</text>
  <rect x="315" y="174" width="110" height="36" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="370" y="196" fill="currentColor" text-anchor="middle" font-family="monospace">R AHB1ENR</text>
  <rect x="435" y="174" width="110" height="36" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="490" y="196" fill="currentColor" text-anchor="middle" font-family="monospace">W MODER 반영</text>
  <text x="370" y="232" fill="currentColor" text-anchor="middle">되읽기 = 동기화: ready_at 5 → 4</text>
  <text x="680" y="196" fill="currentColor">1 사이클</text>
  <text x="680" y="212" fill="currentColor">빠름</text>
</svg>
```

D 스윕 (`-v`, 실제 출력 — USART2 의 BRR 을 반영될 때까지 계속 쓴다):

```text
  D=0  lost writes: no read-back=0  read-back=0
  D=1  lost writes: no read-back=1  read-back=0
  D=2  lost writes: no read-back=2  read-back=0
  D=3  lost writes: no read-back=3  read-back=0
  D=4  lost writes: no read-back=4  read-back=0
  D=5  lost writes: no read-back=5  read-back=0
  D=6  lost writes: no read-back=6  read-back=0
  D=7  lost writes: no read-back=7  read-back=0
  D=8  lost writes: no read-back=8  read-back=0
```

핵심은 마지막 열이다: 되읽기는 1 사이클짜리 **지연이 아니라 동기화 지점(sync point)** 이다. D = 8 이어도 손실 0. NOP 으로 때우는 방식은 D 를 알아야 하고, D 는 버스 프리스케일러에 따라 바뀐다 (연습 2: APB1 을 /4 → /8 로 바꾸면 NOP 5 개는 깨진다).

증상으로 보면 더 무섭다. USART2 (APB1, D = 5) 를 되읽기 없이 켜고 `BRR = 0x16D` (42 MHz 에서 115200 — 계산은 BM-B12), `CR1 = UE|TE` 를 쓰면 **둘 다 사라진다**. UART 는 enable 조차 안 된 채 아무것도 내보내지 않는다. 그런데 디버거로 한 줄씩 밟으면 명령 사이에 수천 사이클이 흐르니 멀쩡히 동작한다 — "디버그 빌드/스텝에서는 되는데 릴리스(-O2)에서만 안 돼요" 의 전형.

엉뚱한 더미 읽기: 모델에서 D = 3 일 때 ENR 대신 **GPIOA_IDR** 을 읽으면 0 이 돌아오고 (아직 클럭 없음), 다음 MODER 쓰기도 사라진다. 되읽기는 "방금 쓴 RCC 레지스터" 여야 한다 — ST HAL/LL 도 그렇게 한다:

```c
/* STM32CubeF4 stm32f4xx_hal_rcc.h (버전별 확인 권장) */
#define __HAL_RCC_GPIOA_CLK_ENABLE()  do { \
        __IO uint32_t tmpreg = 0x00U; \
        SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIOAEN); \
        /* Delay after an RCC peripheral clock enabling */ \
        tmpreg = READ_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIOAEN); \
        UNUSED(tmpreg); \
      } while(0U)
```

**실제 HW 에서 왜 되읽기가 통하나.** CPU 의 STR 은 버스에 "보내 놓고" 다음 명령으로 넘어갈 수 있다 (버퍼된 쓰기, posted write). 그래서 바로 다음 STR 이 GPIOA 에 도착할 때 RCC 쪽 쓰기가 아직 효과를 내지 않았을 수 있다. 같은 RCC 레지스터에 대한 LDR 은 앞선 쓰기가 끝난 뒤에야 데이터가 돌아오고(같은 슬레이브, 순서 보존), 그 사이 흐른 사이클이 errata 의 지연을 덮는다. 정확한 사이클 수는 버스 매트릭스·브리지 구현에 달렸으므로, "되읽기가 공식 우회책" 이라는 사실(errata + HAL) 을 근거로 쓰고, 사이클 단위 설명은 모델로만 취급한다.

### 타깃 빌드 — 되읽기 `ldr` 은 정말 남나

`demo_target.c` 를 Apple clang 21 `--target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2` 로 컴파일한 실제 출력 (지시어 줄 제거, 14384 = 0x3830, 16386 = 0x4002 → `r0 = 0x40023830` = RCC_AHB1ENR):

```text
enable_sync:                      enable_plain_ptr:                 enable_hal_style:
  movw r0, #14384                   movw r0, #14384                   sub  sp, #4
  movt r0, #16386                   movt r0, #16386                   movw r0, #14384
  ldr  r1, [r0]                     ldr  r1, [r0]                     movt r0, #16386
  movs r2, #1                       movs r2, #1                       ldr  r1, [r0]
  orr  r1, r1, #1                   orr  r1, r1, #1                   movs r2, #1
  str  r1, [r0]                     str  r1, [r0]                     orr  r1, r1, #1
  ldr  r0, [r0]      <- 되읽기      (되읽기 사라짐!)                  str  r1, [r0]
  movs r0, #0                       movs r0, #0                       ldr  r0, [r0]     <- 되읽기
  movt r0, #16386                   movt r0, #16386                   and  r0, r0, #1
  ldr  r1, [r0]      <- MODER       ldr  r1, [r0]                     str  r0, [sp]     <- tmpreg (volatile 지역)
  bfi  r1, r2, #10, #2              bfi  r1, r2, #10, #2              ldr  r0, [sp]
  str  r1, [r0]                     str  r1, [r0]                     movs r0, #0 ...
  bx   lr                           bx   lr
```

- `enable_sync` (`REG_READ` = volatile): 되읽기 `ldr r0, [r0]` 이 STR 바로 뒤에 남는다. 결과를 안 써도 volatile 접근은 지울 수 없다 (C11 5.1.2.3).
- `enable_plain_ptr` (volatile 아닌 `uint32_t *`): 컴파일러가 "쓸모없는 읽기" 로 보고 **지웠다**. 출력은 `enable_nosync` 와 한 글자도 다르지 않다. 동기화 코드가 소스에는 있는데 바이너리에는 없는 버그.
- `enable_hal_style`: HAL 의 `__IO uint32_t tmpreg` 모양은 되읽기 + 스택 str/ldr 두 개가 더 붙는다. `(void)REG_READ(...)` 가 더 싸다.
- `enable_dsb`: `str` 뒤에 `dsb sy` 가 들어간다 (컴파일러가 `movs r0, #0` 을 dsb 앞으로 옮긴 것까지 실제 출력 그대로). DSB 는 "앞선 메모리 접근이 완료될 때까지" 다음 명령을 멈추는 아키텍처 배리어(ARMv7-M ARM A3.7.3)다 — errata 의 우회책 ①.

### Part C — 리셋 펄스: 값을 되돌리는 유일한 스위치

```c
static uint32_t rcc_rstr_on_write(uint32_t addr, uint32_t old, uint32_t val, void *ctx) {
    bus_t bus = *(bus_t *)ctx;
    for (int id = 0; id < PERIPH_COUNT; id++) {
        if (PERIPH[id].bus != bus) continue;
        bool r = (val >> PERIPH[id].bit) & 1u;
        if (r) mock_restore_range(PERIPH[id].base, PERIPH[id].base + 0x400u);
        g_rcc.in_reset[id] = r;
    }
    return val;
}
int periph_reset_pulse(periph_id_t id) {
    if ((unsigned)id >= PERIPH_COUNT) return -1;
    uint32_t rstr = BUS_RSTR[PERIPH[id].bus], bit = 1u << PERIPH[id].bit;
    REG_WRITE(rstr, REG_READ(rstr) | bit);      /* 리셋에 묶기 (= HAL __HAL_RCC_GPIOA_FORCE_RESET)   */
    REG_WRITE(rstr, REG_READ(rstr) & ~bit);     /* 풀기       (= HAL __HAL_RCC_GPIOA_RELEASE_RESET) */
    return 0;
}
```

RSTR 는 "펄스" 레지스터가 아니라 **레벨** 레지스터다: 1 을 써 두면 0 을 쓸 때까지 계속 리셋에 묶여 있다. 그래서 두 번 다 RMW 여야 다른 비트(일부러 리셋에 잡아 둔 GPIOB)가 풀리지 않는다. 테스트: `AHB1RSTR = 1<<1` 로 GPIOB 를 잡아 둔 채 GPIOA 를 펄스 → 끝난 뒤 `AHB1RSTR == 0x2`, GPIOB ODR 쓰기는 여전히 무시.

| 상태 | ENR=0 (클럭 off) | ENR=1 (클럭 on) |
|---|---|---|
| RSTR=0 | 읽기 0, 쓰기 무시, **값 유지** | 정상 동작 |
| RSTR=1 | 리셋값으로 강제 (모델: 클럭과 무관하게) | 읽기 = 리셋값, 쓰기 무시 |

모델에서 리셋은 클럭과 무관하게 걸린다 (USART2 를 클럭 off 상태에서 펄스 → 켜면 BRR 0, SR 0xC0). 실제 칩에서 클럭이 꺼진 채 리셋이 걸리는지는 RM 이 명확히 말하지 않는 부분이라, 실무 순서는 **enable → reset 펄스 → 설정** 이 안전하다 (`board_led_init` 이 그 순서).

`board_led_init()` 실제 trace (`-v`):

```text
-- board_led_init(), D_AHB1=2
  cyc op register       value       note
    1  R RCC_AHB1ENR    0x00100000  
    2  W RCC_AHB1ENR    0x00100001  
    3  R RCC_AHB1ENR    0x00100001  
    4  R RCC_AHB1RSTR   0x00000000  
    5  W RCC_AHB1RSTR   0x00000001  
    6  R RCC_AHB1RSTR   0x00000001  
    7  W RCC_AHB1RSTR   0x00000000  
    8  R GPIOA_MODER    0xA8000000  
    9  W GPIOA_MODER    0xA8000400  
   10  W GPIOA_BSRR     0x00000020  
```

| 단계 | MODER (저장값) | ODR | 의미 |
|---|---|---|---|
| 시작 (부트로더가 남긴 값) | 0xA800 0A80 | 0x0000 FFFF | PA5 = AF(10), PA4 = AF, 모든 출력 1 |
| cyc 5 RSTR=1 | 0xA800 0000 | 0 | 리셋값 복원 — 부트로더 흔적 제거 |
| cyc 9 MODER RMW | 0xA800 0400 | 0 | PA5 = 01 출력, PA13/14/15 = 10 (AF) 그대로 |
| cyc 10 BSRR | 0xA800 0400 | 0x0000 0020 | PA5 = 1 (LED ON) |

`MODER >> 26 == 0b101010` 테스트가 SWD 핀 보존을 확인한다. `GPIOA->MODER = 1 << 10` 처럼 대입하면 PA13 (SWDIO)·PA14 (SWCLK) 가 입력으로 바뀌어 **그 순간부터 디버거 연결이 끊긴다** — "보드가 이제 ST-LINK 에 안 붙어요" 의 단골 원인 (복구: connect-under-reset 또는 BOOT0 로 부팅). 리셋 펄스 자체는 MODER 를 0xA800 0000 으로 되돌리므로 SWD 를 건드리지 않는다. 같은 이유로 부트로더 → 앱 점프 전에 주변장치를 리셋 펄스로 정리하는 것이 BM-A03 의 단계 중 하나다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-B03_so && /tmp/BM-B03_so      # 47/47 passed
/tmp/BM-B03_so -v                                                                          # trace · D 스윕
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/BM-B03_as && /tmp/BM-B03_as   # 47/47, 보고 없음
```

타깃 빌드: `UNIT_TEST` 를 빼면 `REG_READ/REG_WRITE` 가 volatile MMIO 가 된다. 드라이버 부분(`PERIPH` 표, `periph_*`, `board_led_init`)만 떼어 펌웨어에 넣으면 그대로 동작하는 코드다 — mock·하네스는 `#ifdef UNIT_TEST` 쪽 테스트 파일로 분리 (BM-B02 의 seam 설명 참고). 그 모양의 실제 asm 이 위 `demo_target.c` 출력이다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 클럭 enable 을 빼먹고 GPIO/UART 설정 | 쓰기가 **예외 없이** 무시되고 읽기는 0. 레지스터 뷰에서 전부 0 으로 보인다 | 주변장치 init 첫 줄 = `periph_clock_enable(id)`. 디버깅은 ENR 부터 본다 |
| `RCC->AHB1ENR = RCC_AHB1ENR_GPIOAEN;` | 리셋값 0x0010 0000 의 CCMDATARAMEN 과 이미 켠 다른 포트를 끈다 | `RCC->AHB1ENR \|= RCC_AHB1ENR_GPIOAEN;` |
| enable 직후 바로 레지스터 쓰기 (되읽기 없음) | 첫 1~D 번 쓰기가 사라짐 (errata). -O0 이나 스텝 실행에서는 안 보이는 하이젠버그 | `(void)RCC->AHB1ENR;` 또는 `__DSB();` |
| 되읽기를 non-volatile 포인터로 | `-O2` 가 읽기를 지운다 (위 `enable_plain_ptr` asm) | CMSIS 구조체(`__IO`)나 `REG_READ` (volatile) 로 |
| 더미 읽기를 그 주변장치 자신에게 | 클럭 없는 레지스터 읽기 = 0, 동기화가 아님 (모델). errata 문구도 "해당(RCC) 레지스터" | 방금 쓴 RCC ENR 을 읽는다 |
| "꺼다 켜면 초기화되겠지" | 게이팅은 값 유지. 이전 설정이 그대로 살아난다 | 기본값이 필요하면 `periph_reset_pulse(id)` |
| `RCC->AHB1RSTR = GPIOARST; RCC->AHB1RSTR = 0;` | 잡아 둔 다른 주변장치의 리셋까지 풀린다 | 둘 다 RMW (`\|=`, `&= ~`) |
| `GPIOA->MODER = 1u << 10;` | PA13/14 SWD 가 입력이 되어 디버거 연결이 끊김 | 2비트 필드 clear+set RMW (BM-B11) |
| ISR 과 메인이 둘 다 ENR 을 RMW | RMW 는 원자적이지 않다 — 갱신 유실 | 초기화는 한 곳에서, 또는 임계 구역 (BM-B04, C-M14) |

## 8. 면접 Q&A

<details>
<summary>Q1. 주변장치 클럭은 왜 기본으로 꺼져 있나? 켜 두면 뭐가 문제인가?</summary>

동적 전력이 C·V²·f 에 비례하고, 클럭 트리와 플립플롭 토글이 MCU 런 전류의 큰 몫이기 때문이다. 안 쓰는 블록의 클럭을 끊으면(clock gating) 그 블록은 누설 전류만 남는다. 그래서 리셋 직후 대부분의 ENR 은 0 이고, 펌웨어가 쓰는 것만 켠다. 꼬리질문 "Sleep 모드에서도?" → STM32 는 Sleep 중 주변장치 클럭을 따로 고르는 `xLPENR` 레지스터가 있다 (RM0090 기준, 확인 권장). 전력 모드 전체는 BM-M11.

</details>

<details>
<summary>Q2. 클럭이 꺼진 주변장치에 쓰면 정확히 무슨 일이 일어나나?</summary>

STM32F4 에서 흔히 관찰되는 동작은 "쓰기 무시, 읽기 0, 예외 없음" 이다 (이 레슨 mock 의 정의 — RM 에 명시된 보장이라기보다 관찰 기반이므로 확인 권장). 그래서 증상이 조용하다: 코드는 끝까지 돌고 하드웨어만 반응이 없다. 칩마다 다르다는 점이 꼬리질문 포인트다 — NXP Kinetis 계열은 SIM_SCGCx 로 꺼진 주변장치에 접근하면 버스 에러 → HardFault 가 난다고 알려져 있고 (확인 권장), nRF52 는 주변장치 클럭을 하드웨어가 자동 관리해 이런 enable 단계가 없다. 디버깅 순서: 레지스터 뷰에서 그 주변장치가 전부 0 → RCC ENR 비트 확인 → 리셋 비트가 1 로 남아 있지 않은지 확인.

</details>

<details>
<summary>Q3. HAL 은 왜 클럭을 켠 직후 ENR 을 다시 읽나? 결과는 버리는데.</summary>

STM32 errata 의 "Delay after an RCC peripheral clock enabling": enable 쓰기 후 실제로 클럭이 들어가기까지 AHB 2 사이클, APB 1 + 프리스케일러 사이클이 걸린다 (기억 기준, ES0182 류 확인 권장). CPU 의 STR 은 버퍼링되어 다음 명령이 바로 진행되므로, 바로 다음 STR 이 주변장치에 닿을 때 클럭이 아직 없을 수 있다 → 그 쓰기는 무시된다. 같은 RCC 레지스터를 읽으면 앞선 쓰기가 끝나야 데이터가 돌아오고 그 동안 지연이 지나간다. 결과를 버려도 되는 이유는 **값이 아니라 타이밍이 목적** 이기 때문이고, volatile 이라 컴파일러가 지우지 못한다 (non-volatile 이면 지워진다 — demo_target.c asm). 이 레슨 모델에서는 D = 8 이어도 되읽기 한 번으로 손실 0.

</details>

<details>
<summary>Q4. 되읽기 대신 NOP 몇 개나 DSB 를 넣어도 되나? 뭐가 제일 낫나?</summary>

errata 는 세 가지 모두 우회책으로 든다 (확인 권장). NOP n 개는 n 을 버스·프리스케일러에 맞춰야 하고, 나중에 누가 APB 프리스케일러를 /4 → /8 로 바꾸면 조용히 깨진다 (연습 2: 모델에서 NOP 5 개 → BRR 쓰기 소실). 게다가 Cortex-M4 Generic User Guide 의 NOP 설명대로 "NOP 이 시간을 쓴다는 보장이 없다 — 실행 단계 전에 파이프라인에서 제거될 수 있다" (확인 권장) — 지연용으로 믿을 것이 못 된다. DSB 는 "앞선 명시적 메모리 접근이 완료될 때까지" 멈추는 아키텍처 배리어라 의도가 분명하다. 되읽기는 이식성 좋고(어느 코어든 동작), HAL/LL 이 쓰는 방식이라 코드 리뷰에서 설명이 필요 없다. 실무 답: "CMSIS/HAL 매크로를 쓰거나, 직접 쓰면 `(void)RCC->xENR;` 한 줄 + 주석".

</details>

<details>
<summary>Q5. 클럭을 끄는 것과 리셋은 뭐가 다른가? deinit 은 어떻게 짜나?</summary>

클럭을 끄면 플립플롭은 그대로 값을 유지한다 — 다시 켜면 예전 설정이 살아난다 (Part A 테스트). 리셋(xRSTR=1)은 레지스터를 리셋값으로 되돌리고, 0 을 쓸 때까지 묶어 둔다. 그래서 "알려진 상태에서 시작" 이 필요하면 리셋 펄스, "전력을 아끼려면" 클럭 off. HAL 의 `HAL_xxx_DeInit()` → `HAL_xxx_MspDeInit()` 가 `FORCE_RESET; RELEASE_RESET;` 후 `CLK_DISABLE` 을 하는 것이 그 조합이다 (연습 3). 부트로더가 UART 를 쓰고 앱으로 점프할 때 이 정리를 빼먹으면, 앱은 "리셋 직후" 라고 가정한 주변장치가 이미 켜져 인터럽트를 내는 상태에서 시작한다 (BM-A03).

</details>

<details>
<summary>Q6. 디버거로 한 줄씩 실행하면 되는데 그냥 돌리면 UART 가 안 나온다. 원인 후보는?</summary>

타이밍 의존 버그다. 1순위가 이 레슨의 "enable 직후 접근" — 스텝 실행은 명령 사이에 수천 사이클을 넣어 지연을 덮는다. -O0 빌드도 명령 사이에 스택 load/store 가 끼어 우연히 지연을 채운다. 다른 후보: PLL/HSE 가 안정되기 전에 클럭을 전환(ready 플래그 폴링 누락), 플래시 wait state 를 올리기 전에 SYSCLK 를 올림 (BM-B10), TXE 대신 TC 를 안 기다리고 전원/클럭을 끔 (BM-B12). 확인 방법: enable 직후 ENR 되읽기 / DSB 를 넣어 사라지는지, 또는 첫 레지스터 쓰기 직후 그 레지스터를 읽어 값이 들어갔는지 로그.

</details>

<details>
<summary>Q7. 왜 `|=` 인가? `RCC->AHB1ENR = RCC_AHB1ENR_GPIOAEN` 이 실제로 깨뜨리는 것은?</summary>

ENR 은 여러 주변장치의 비트를 한 레지스터에 모아 둔 공유 레지스터다. 대입은 다른 드라이버가 켜 둔 클럭(GPIOB, DMA)을 끄고, STM32F4 에서는 리셋값이 1 인 bit20 CCMDATARAMEN 까지 끈다 — CCM 에 스택이나 버퍼가 있으면 그 다음 접근부터 이상해진다. `|=` 도 완전하지 않다: 읽기-수정-쓰기 사이에 ISR 이 같은 ENR 을 바꾸면 그 갱신을 덮어쓴다 (BM-B04 의 RMW 경쟁, C-M14 임계 구역). 그래서 클럭 enable 은 보통 init 시점 한 곳에서만 한다. 비트밴딩(bit-banding)이 있는 M3/M4 는 비트 단위 원자 쓰기도 가능하다 (BM-M/A 범위).

</details>

<details>
<summary>Q8. 클럭 enable 과 리셋 펄스는 어느 순서로? 리셋 펄스 사이에 지연이 필요한가?</summary>

이 레슨은 enable → 리셋 펄스 → 설정 순서를 쓴다. 클럭이 없는 상태에서 리셋이 걸리는지는 RM 이 명확하지 않은 부분이라 클럭을 먼저 켜는 쪽이 안전하고, ST HAL 의 MspInit 도 CLK_ENABLE 이 먼저다. HAL 의 FORCE_RESET / RELEASE_RESET 매크로 사이에는 지연이 없다 (확인 권장) — 리셋 비트를 1 로 쓰는 순간 비동기적으로 리셋이 걸리는 구조로 보인다. 다만 release 직후 첫 접근에 대해서는 enable 과 같은 이유로 RSTR 되읽기를 넣는 팀도 있다. 모델은 리셋을 즉시 반영하므로 이 차이를 재현하지 않는다 — 모델의 한계를 말할 수 있는 것도 답의 일부다.

</details>

## 9. 추가 연습

### 연습 1 — 여러 주변장치를 한 번에 켜기

`int periph_clock_enable_many(const periph_id_t *ids, int n)` — 같은 버스의 비트를 모아 **ENR 레지스터마다 RMW 1번 + 되읽기 1번**. 목록에 잘못된 id 가 하나라도 있으면 버스에 손대지 않고 -1. {GPIOA, USART2, GPIOB, TIM2} 의 접근 수는?

<details>
<summary>답 보기</summary>

먼저 목록 전체를 검사하면서 `mask[bus] |= 1u << bit` 를 모은 뒤, 버스마다 `REG_WRITE(enr, REG_READ(enr) | mask[b]); (void)REG_READ(enr);`. 접근 6번 (AHB1 R W=0x00100003 R, APB1 R W=0x00020001 R) — 하나씩 하면 12번. 검사를 먼저 하는 이유: 절반만 켜진 상태로 -1 을 돌려주면 호출자가 상태를 알 수 없다 (all-or-nothing). 모든 주변장치가 첫 접근부터 동작함을 D = (2, 5, 3) 에서 확인.

</details>

### 연습 2 — NOP 우회책은 얼마나 깨지기 쉬운가

`periph_clock_enable_nops(id, nops)` — 되읽기 대신 `mock_idle(nops)` (버스 접근 없이 시간만 흐름). 버스별 최소 NOP 수를 탐색하고, APB1 프리스케일러를 /4 → /8 로 바꾼 뒤(모델 D = 1 + 8 = 9) 같은 NOP 수가 여전히 동작하는지 확인하라.

<details>
<summary>답 보기</summary>

탐색 결과 최소 NOP = D: GPIOA(AHB1) 2, USART2(APB1 /4) 5, APB1 /8 에서는 9. NOP 5 개로 맞춰 둔 코드는 /8 에서 BRR 쓰기 1번을 잃는다 (`dropped == 1`). 되읽기는 그대로 손실 0. 교훈: 지연을 "숫자" 로 넣으면 클럭 설정이 바뀌는 순간 깨지고, 동기화 지점(되읽기·DSB)은 클럭 설정과 무관하다. 실제 코어에서는 NOP 이 접혀 사이클을 안 쓸 수도 있어 더 나쁘다.

</details>

### 연습 3 — deinit: 리셋 + 클럭 끄기

`int periph_deinit(periph_id_t id)` — 부트로더가 앱으로 점프하기 전 주변장치를 정리하는 함수. 끝난 뒤 레지스터·ENR·RSTR 상태는? 다시 enable 하면 무엇이 읽히나?

<details>
<summary>답 보기</summary>

`periph_reset_pulse(id)` 후 `periph_clock_disable(id)`. 결과: MODER 저장값 0xA800 0000 (리셋값), AHB1ENR 0x0010 0000 (GPIOAEN 만 0, CCM 비트 보존), AHB1RSTR 0. 클럭이 꺼졌으므로 읽기 0, 쓰기 무시. 다시 켜면 0xA800 0000 — 부트로더 설정(0xA800 0A80)은 남지 않는다. 순서를 반대로(끄고 리셋) 하면 모델에서는 같은 결과지만, 클럭 없는 리셋을 RM 이 보장하는지 확인이 필요하므로 "리셋 먼저" 가 안전하다.

</details>

### 연습 4 — 지금 어떤 클럭이 켜져 있나 (디버그 덤프)

`int rcc_dump(char *buf, size_t cap)` — ENR 3개를 읽어 켜진 주변장치 이름을 공백으로 잇고, 표에 없는 비트는 `AHB1[20]` 처럼 찍는다. 용량을 넘으면 잘린 문자열(NUL 종료)과 -1. 전력 디버깅("Stop 에서 전류가 안 내려가요" — 뭐가 켜져 있나)에 쓰는 함수.

<details>
<summary>답 보기</summary>

버스 순서(AHB1, APB1, APB2) × 비트 오름차순으로 돈다. 리셋 직후 `"AHB1[20]"` (8자) — CCM RAM 클럭이 기본 ON 이라는 사실이 덤프에 바로 보인다. GPIOA·USART2·TIM2 를 켜면 `"GPIOA AHB1[20] TIM2 USART2"` (26자), USART1 추가 시 33자. cap = 8 → `"GPIOA A"` + -1, cap = 0 → 버퍼를 건드리지 않고 -1. 출력 조립은 고정 크기 `item[16]` + 한 글자씩 복사로 용량을 지킨다 (malloc 없음).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-B03_pr && /tmp/BM-B03_pr      # 21/21 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o /tmp/BM-B03_pas && /tmp/BM-B03_pas   # 21/21, 보고 없음
```

## 10. 핵심 정리

- 주변장치 = RCC 의 (버스, 비트) 한 쌍: xENR 은 클럭 게이트, xRSTR 은 리셋 레벨. 표 하나로 `periph_clock_enable/disable/reset_pulse(id)` 를 만든다.
- 클럭 없는 주변장치: 쓰기 무시, 읽기 0, 예외 없음 (STM32 관찰·모델; Kinetis 는 폴트). 레지스터 뷰가 전부 0 이면 ENR 부터 본다.
- ENR 은 공유 레지스터 → 항상 RMW (`|=`, `&= ~`). F4 의 AHB1ENR 리셋값 0x0010 0000 (CCMDATARAMEN) 을 대입으로 지우지 마라.
- enable 직후 접근은 errata 지연(AHB 2, APB 1+프리스케일러 — 확인 권장) 때문에 사라질 수 있다 → `(void)RCC->xENR;` 되읽기 (volatile 필수) 또는 DSB. NOP 개수는 프리스케일러가 바뀌면 깨진다.
- 이 버그는 스텝 실행·-O0 에서 숨는다 — "릴리스에서만 안 돼요" 의 단골.
- 게이팅 ≠ 리셋: 꺼도 값 유지, 리셋 펄스(RMW set → RMW clear)만 리셋값으로 되돌린다. deinit = 리셋 펄스 + 클럭 off.
- GPIOA 를 대입으로 덮으면 PA13/14 SWD 가 죽는다 — 필드 RMW.

## 참고자료

- ST RM0090 Reference manual (STM32F405/415, 407/417, 427/437, 429/439) — RCC 장 (RCC_AHB1RSTR/APB1RSTR/APB2RSTR, AHB1ENR/APB1ENR/APB2ENR 와 리셋값), GPIO 장 (MODER/OSPEEDR/PUPDR 포트별 리셋값), USART 장 (SR 리셋값) — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf> (이 레슨의 주소·비트·리셋값은 기억 기준 대표값 — 원문 확인 권장)
- ST ES0182 Errata sheet (STM32F405/407xx, STM32F415/417xx) — "Delay after an RCC peripheral clock enabling" 항목 (다른 F4/F2/F7 errata 에도 같은 항목) — <https://www.st.com> (항목 번호·문구 확인 권장)
- STM32CubeF4 HAL `stm32f4xx_hal_rcc.h` (`__HAL_RCC_GPIOA_CLK_ENABLE`, `__HAL_RCC_GPIOA_FORCE_RESET/RELEASE_RESET`), LL `stm32f4xx_ll_bus.h` (`LL_AHB1_GRP1_EnableClock` 의 tmpreg 되읽기) — <https://github.com/STMicroelectronics/STM32CubeF4>
- ARMv7-M Architecture Reference Manual (ARM DDI 0403E) — A3.7.3 Memory barriers (DSB), A3.5 메모리 타입 (Device 메모리의 순서 보장) — <https://developer.arm.com/documentation/ddi0403/latest/>
- CMSIS-Core `__DSB()`, `__IO` — <https://arm-software.github.io/CMSIS_6/latest/Core/index.html>
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.) — 버스 시스템·쓰기 버퍼, 메모리 배리어 장
- 관련 레슨: BM-B01 (CMSIS 헤더의 `_Pos/_Msk`), BM-B02 (mock 프레임워크 원본, link/macro seam), BM-B04 (RMW 경쟁, BSRR, W1C), BM-B10 (클럭 트리·APB 프리스케일러), BM-B11 (GPIO 드라이버 — 여기 규칙의 축약판을 씀), BM-B12 (USART BRR 계산, TC vs TXE), BM-M11 (저전력 — 켜진 클럭이 곧 전류), BM-A03 (부트로더 → 앱 점프 전 주변장치 정리), C-M14 (ISR↔메인 공유 데이터와 임계 구역), SIV-B01 (레지스터 헤더 생성)
