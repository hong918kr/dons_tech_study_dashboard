---
id: BM-B12
time: 60분
updated: 2026-11-11
---
# BM-B12 폴링 UART 드라이버: baud divisor와 상태 플래그

## 학습 목표

- STM32F4 식 `USARTDIV = f_ck / (8·(2−OVER8)·baud)` 를 정수만으로 계산해 BRR(mantissa/fraction)을 만들고, **소수부 반올림이 16이 되는 carry** 와 OVER8=1 의 bit3=0 규칙을 틀리지 않는다. 실제 baud 와 오차(ppm)도 같이 낸다.
- TXE / TC / RXNE / ORE / FE 가 각각 **무엇이 비었거나 찼다는 뜻인지**, 어떻게 지워지는지(r, rc_r, rc_w0, "SR 읽기 → DR 접근" 시퀀스) 말하고 그대로 폴링 코드로 옮긴다.
- 모든 폴링 루프에 상한(timeout)을 두고, ORE·FE 를 호출자에게 오류 코드로 돌려주는 `putc / getc / write / flush` 를 mock 위에서 테스트로 증명한다.
- "보내고 전원 끄기" 에서 **TXE 만 기다리면 마지막 바이트가 잘리고 TC 를 기다려야 끝까지 나간다** 는 것을 tick 단위로 재현하고, 슬립 진입·RS-485 DE 해제·baud 변경에도 같은 규칙을 적용한다.

## 1. 문제

UART 는 새 보드를 받았을 때 **가장 먼저 살리는 생존 신호** 이고, 펌웨어 면접에서 "폴링 UART 드라이버를 써 보세요 — 그런데 TXE 와 TC 는 뭐가 달라요?" 는 단골 문제다. SSD 컨트롤러 bring-up 에서 ROM 다음에 처음 보는 것이 디버그 UART 콘솔의 한 줄이고, RF 모듈 보드의 AT 명령도 결국 이 `putc/getc` 위에서 돈다. 코드는 20줄이지만 baud 계산의 반올림, 플래그가 지워지는 방식, 마지막 바이트가 "레지스터에서 나갔다" 와 "선에서 나갔다" 의 차이까지 정확해야 한다.

USART 송신부에는 데이터가 머무는 곳이 **두 군데** 있다. CPU 가 DR 에 쓴 바이트는 먼저 송신 데이터 레지스터(TDR)에 들어가고, 시프트 레지스터(shift register)가 비면 곧바로 그쪽으로 옮겨져 한 비트씩 TX 핀으로 나간다. **TXE = "TDR 이 비었다(다음 바이트를 써도 된다)"**, **TC = "TDR 도 비었고 시프트 레지스터의 마지막 stop 비트까지 다 나갔다"** 이다. 수신부는 반대로 시프트 레지스터 → RDR 이고 **RXNE = "RDR 에 읽을 바이트가 있다"**, RDR 이 안 비었는데 다음 바이트가 도착하면 **ORE (overrun)**.

이 레슨은 맥에서 돈다. 드라이버는 `REG_READ / REG_WRITE` 두 매크로로만 레지스터를 만지고, 호스트 빌드에서 그 매크로가 mock 으로 가서 USART2 **행동 모델** 을 돌린다 (BM-B02 mock 축약판). 같은 소스는 `UNIT_TEST` 를 빼면 진짜 MMIO 코드가 된다.

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">호스트에서 도는 구조: 드라이버는 매크로 두 개만 안다 (BM-B02 mock 축약판)</text>
  <rect x="10" y="40" width="150" height="88" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="60" fill="currentColor" font-weight="bold">드라이버 (TODO)</text>
  <text x="20" y="80" fill="currentColor" font-family="monospace">uart_init</text>
  <text x="20" y="96" fill="currentColor" font-family="monospace">uart_putc / getc</text>
  <text x="20" y="112" fill="currentColor" font-family="monospace">uart_write / flush</text>
  <rect x="185" y="40" width="160" height="88" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="195" y="60" fill="currentColor" font-weight="bold">REG_READ / REG_WRITE</text>
  <text x="195" y="80" fill="currentColor">UNIT_TEST: mock_read32</text>
  <text x="195" y="96" fill="currentColor">타깃: volatile MMIO</text>
  <text x="195" y="112" fill="currentColor">(같은 소스, 매크로만 바뀜)</text>
  <rect x="370" y="40" width="160" height="88" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="380" y="60" fill="currentColor" font-weight="bold">mock core</text>
  <text x="380" y="80" fill="currentColor">주소 표 + 읽기/쓰기 훅</text>
  <text x="380" y="96" fill="currentColor">접근 로그 (마지막 64개)</text>
  <text x="380" y="112" fill="currentColor">모르는 주소 → 카운터</text>
  <rect x="555" y="40" width="175" height="150" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="565" y="60" fill="currentColor" font-weight="bold">USART2 모델</text>
  <text x="565" y="80" fill="currentColor">SR 읽기 1번 = 1 tick</text>
  <text x="565" y="96" fill="currentColor">1 tick = 1 비트, 프레임 10</text>
  <text x="565" y="112" fill="currentColor">DR → TDR → 시프트 레지스터</text>
  <text x="565" y="128" fill="currentColor">→ 다 나가면 cap[] 에 기록</text>
  <text x="565" y="144" fill="currentColor">RX 큐 → RDR (RXNE / ORE)</text>
  <text x="565" y="160" fill="currentColor">APB1ENR.17=0 → 무시, 0</text>
  <text x="565" y="176" fill="currentColor">PB0 BSRR reset = 전원 차단</text>
  <path d="M160 84 L185 84 M345 84 L370 84 M530 84 L555 84 M530 190 L555 190" stroke="currentColor" fill="none"/>
  <path d="M178 80 L185 84 L178 88 M363 80 L370 84 L363 88 M548 80 L555 84 L548 88" stroke="currentColor" fill="none"/>
  <rect x="10" y="160" width="520" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="180" fill="currentColor" font-weight="bold">테스트 하네스</text>
  <text x="20" y="198" fill="currentColor" font-family="monospace">mock_tick(n)  mock_uart_rx_inject(b,n,SR_FE)  mock_peek(a)</text>
  <text x="20" y="214" fill="currentColor" font-family="monospace">mock_log_back(k)  U.cap  U.ore_lost  U.aborted_frames</text>
  <text x="10" y="248" fill="currentColor">시간은 폴링이 굴린다: putc/getc/flush 가 SR 을 한 번 읽을 때마다 선 위로 비트가 하나 나간다.</text>
</svg>
```

레지스터 (RM0090 기준 대표값 — RM 확인 권장. 테스트의 정답은 solution.c 모델의 정의다):

| 레지스터 | 주소/오프셋 | 이 레슨에서 쓰는 비트 | 접근 타입 |
|---|---|---|---|
| `RCC_APB1ENR` | 0x4002 3840 | bit17 USART2EN | rw |
| `USART2_SR` | 0x4000 4400 +0x00 | PE0 FE1 NF2 ORE3 IDLE4: r (SR 읽기 → DR 읽기로 clear) · RXNE5: rc_w0 (DR 읽기로도 clear) · TC6: rc_w0 (SR 읽기 → DR 쓰기로도 clear) · TXE7: r (DR 쓰기로 clear) | 리셋값 0x00C0 (TXE=1, TC=1) |
| `USART2_DR` | +0x04 | [8:0] 쓰기 → TDR, 읽기 ← RDR | rw |
| `USART2_BRR` | +0x08 | [15:4] DIV_Mantissa, [3:0] DIV_Fraction | rw |
| `USART2_CR1` | +0x0C | RE2 TE3 M12 UE13 OVER8 15 | rw |
| `USART2_CR2` | +0x10 | STOP[13:12] (00 = 1 stop) | rw |
| `GPIOB_BSRR` | 0x4002 0418 | 보드 설정: PB0 = PWR_HOLD. bit16 (BR0) = 전원 차단 | w |

모델의 시간 단위는 단순화했다: **1 tick = 1 비트 시간**, 8N1 프레임 = 10 tick, SR 을 한 번 읽을 때마다 1 tick. 실제로는 115200 baud 에서 1 비트 = 8.68 µs = 168 MHz 코어로 약 1458 사이클이라 폴링이 수백 번 돈다. 비율만 다를 뿐 순서와 인과는 같다.

### Part A — BRR 계산과 초기화

- `int uart_brr_compute(fck, baud, over8, uart_brr_t *out)` — `out` 에 `brr, mantissa, fraction, div (= round(fck/baud)), actual_baud, err_ppm` 을 채운다. 반올림은 가장 가까운 값(동점은 올림), fraction 이 16 (OVER8=1 이면 8) 으로 반올림되면 mantissa 로 carry. 범위 밖(USARTDIV < 1, mantissa > 4095, 0 인자)은 `UART_EINVAL`.
- `int uart_init(base, fck, baud, over8)` — 인자를 먼저 검사(실패하면 레지스터를 하나도 건드리지 않는다) → `APB1ENR |= USART2EN` + read-back → `CR1 = UE` → `CR2 = 0` → `BRR` → `CR1 |= TE | RE`.

### Part B — putc / getc 와 상태 플래그

- `uart_putc(base, c)` — TXE 를 상한 있게 폴링한 뒤 DR 에 쓴다. 클럭이 안 켜졌으면 SR 이 0 으로 읽혀 영원히 기다릴 것 — `UART_ETIMEOUT`.
- `uart_getc(base, &out, timeout_polls)` — SR **스냅숏 한 번** 으로 RXNE 를 보고 DR 을 읽는다. 스냅숏의 ORE / FE / NF / PE 를 `UART_EOVERRUN / EFRAME / ENOISE / EPARITY` 로 보고 (바이트는 그래도 `out` 에 준다). 타임아웃이면 `UART_ETIMEOUT`.
- 주입하는 고장: 데이터 없음(타임아웃), 3바이트를 연달아 넣고 35 tick 동안 안 읽기(ORE, 2바이트 유실), stop 비트 0(FE), 잡음(NF), 클럭 미설정.

### Part C — write / flush 와 "보내고 전원 끄기"

- `uart_write(base, buf, n)` — putc 반복, 보낸 바이트 수 또는 첫 오류. `uart_flush(base)` — TC 를 상한 있게 기다린다.
- 주어진 `app_report_and_power_off(msg, wait_tc)` 가 `"SHUTDOWN OK\r\n"` 을 보내고 PB0 을 내려 보드 전원을 끊는다. `wait_tc = 0` (TXE 만 확인) 이면 모델이 캡처한 TX 에 **마지막 `\n` 이 없어야** 하고, `wait_tc = 1` 이면 13바이트 전부 있어야 한다. 두 대기의 차이가 정확히 한 프레임(10 tick)임도 잰다.

## 2. Problem (English)

> **Polling UART driver for an STM32F4-class USART.**
>
> 1. `int uart_brr_compute(uint32_t fck, uint32_t baud, int over8, uart_brr_t *out)`: USARTDIV = fck / (8·(2−OVER8)·baud). Program BRR as DIV_Mantissa[15:4] | DIV_Fraction[3:0] (4 fraction bits when OVER8=0; 3 bits with bit 3 kept clear when OVER8=1). Round to nearest; a fraction that rounds up to 16 (or 8) must carry into the mantissa. Report the actual baud and the error in ppm. Reject USARTDIV < 1 and mantissa > 4095. Integer arithmetic only.
>    - `uart_brr_compute(42000000, 115200, 0)` → BRR = 0x16D (22 + 13/16), actual 115068 baud, −1142 ppm.
>    - `uart_brr_compute(45000000, 9600, 0)` → 4687.5 → BRR = 0x1250 (fraction carried).
>    - `uart_brr_compute(42000000, 921600, 1)` → BRR = 0x56.
> 2. `int uart_init(uint32_t base, uint32_t fck, uint32_t baud, int over8)`: 8N1, TX+RX. Enable the peripheral clock without disturbing other enable bits; write BRR while TE/RE are off.
> 3. `int uart_putc(uint32_t base, uint8_t c)`, `int uart_getc(uint32_t base, uint8_t *out, uint32_t timeout_polls)`: busy-wait on TXE / RXNE. Every wait is bounded. `getc` reports overrun, framing, noise and parity errors from a single SR snapshot taken before reading DR.
> 4. `int uart_write(uint32_t base, const uint8_t *buf, size_t n)` and `int uart_flush(uint32_t base)`. The board cuts its own power right after printing a shutdown message — make sure the last byte actually leaves the pin. Explain TXE vs TC.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/BM/BM-B12
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/BM-B12_st && /tmp/BM-B12_st
# 처음엔 "13/52 passed" (잘못된 인자 검사처럼 스텁도 우연히 맞는 것만) — TODO 6개를 채워 52/52
```

TODO 는 `uart_brr_compute`, `uart_init`, `uart_putc`, `uart_getc`, `uart_write`, `uart_flush` 여섯 개다. 스텁은 모두 경고 없이 컴파일되고, 아무것도 기다리지 않으므로 멈추지 않는다. mock core, USART 모델, 하네스는 주어진 코드다.

## 4. 힌트

<details>
<summary>힌트 1 — USARTDIV 를 실수로 계산하지 마라</summary>

OVER8=0 에서 BRR 의 단위는 USARTDIV 의 1/16 이다. 그러니 `BRR = 16·USARTDIV = fck / baud` — 나눗셈 한 번이면 mantissa 와 fraction 이 동시에 나온다. 반올림은 `(2·fck + baud) / (2·baud)` (64비트로). OVER8=1 은 단위가 1/8 이라 같은 정수 `D = round(fck/baud)` 를 `D/8`, `D%8` 로 나눠 다시 붙인다.

</details>

<details>
<summary>힌트 2 — carry 는 "따로 처리" 하지 않으면 저절로 된다</summary>

mantissa 를 먼저 floor 로 구하고 fraction 을 따로 반올림하면 fraction 이 16 이 되는 경우가 생긴다. 정수 D 하나를 먼저 반올림하고 나중에 `D / 16`, `D % 16` 로 쪼개면 carry 는 나눗셈이 알아서 처리한다.

</details>

<details>
<summary>힌트 3 — 오차 ppm 을 정수로</summary>

`actual = fck / D` 이므로 `err = (fck − baud·D) / (baud·D)`. 분자·분모를 `int64_t` 로 두고 `|분자|·10^6` 을 분모로 반올림 나눗셈한 뒤 부호를 붙인다.

</details>

<details>
<summary>힌트 4 — getc 에서 SR 을 몇 번 읽나</summary>

DR 을 읽는 순간 RXNE 와 (SR 을 읽은 뒤라면) ORE/FE/NF/PE 가 지워진다. 그래서 루프 안에서 `sr = REG_READ(SR)` 를 **한 번** 받아 두고, RXNE 면 DR 을 읽은 다음 **그 sr 변수로** 오류를 판정한다. SR 을 다시 읽으면 오류 비트가 이미 사라졌다.

</details>

<details>
<summary>힌트 5 — flush 는 무엇을 기다리나</summary>

TXE 는 "TDR 이 비었다" 일 뿐 시프트 레지스터는 아직 마지막 바이트를 보내는 중일 수 있다. 전원·클럭·트랜시버를 끄기 전에는 TC 를 기다린다. 이 루프에도 상한을 둔다.

</details>

## 5. 풀이

### Part A — BRR: 나눗셈 한 번, 반올림 한 번

RM0090 (확인 권장) 의 정의:

- `baud = f_ck / (8·(2 − OVER8)·USARTDIV)`, `USARTDIV = DIV_Mantissa + DIV_Fraction / 16` (OVER8=0) 또는 `/ 8` (OVER8=1, bit3 은 0 으로 둔다).
- 그러면 BRR 이 뜻하는 정수 **D = 16·USARTDIV (OVER8=0) = 8·USARTDIV (OVER8=1) = f_ck / baud** 이다. 두 모드 모두 `actual_baud = f_ck / D`.

그래서 계산은 세 줄이다: `D = round(fck / baud)` → `mantissa = D / ovs`, `fraction = D % ovs` (ovs = 16 또는 8) → `BRR = mantissa << 4 | fraction`. OVER8=0 이면 `BRR == D` 그 자체다 (연습 1 에서 전수 확인).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">USART_BRR (RM0090 기준 대표값): [15:4] DIV_Mantissa · [3:0] DIV_Fraction</text>
  <g fill="currentColor" text-anchor="middle" font-size="12">
  <text x="187" y="44">15</text><text x="221" y="44">14</text><text x="255" y="44">13</text><text x="289" y="44">12</text><text x="323" y="44">11</text><text x="357" y="44">10</text><text x="391" y="44">9</text><text x="425" y="44">8</text>
  <text x="459" y="44">7</text><text x="493" y="44">6</text><text x="527" y="44">5</text><text x="561" y="44">4</text><text x="595" y="44">3</text><text x="629" y="44">2</text><text x="663" y="44">1</text><text x="697" y="44">0</text>
  </g>
  <text x="10" y="69" fill="currentColor" font-weight="bold">OVER8=0</text>
  <text x="10" y="85" fill="currentColor">42 MHz / 115200</text>
  <rect x="170" y="52" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="204" y="52" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="238" y="52" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="272" y="52" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="306" y="52" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="340" y="52" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="374" y="52" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="408" y="52" width="34" height="24" class="f-acc" stroke="currentColor"/>
  <rect x="442" y="52" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="476" y="52" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="510" y="52" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="544" y="52" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="578" y="52" width="34" height="24" class="f-ok" stroke="currentColor"/><rect x="612" y="52" width="34" height="24" class="f-ok" stroke="currentColor"/><rect x="646" y="52" width="34" height="24" class="f-ok" stroke="currentColor"/><rect x="680" y="52" width="34" height="24" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="187" y="69">0</text><text x="221" y="69">0</text><text x="255" y="69">0</text><text x="289" y="69">0</text><text x="323" y="69">0</text><text x="357" y="69">0</text><text x="391" y="69">0</text><text x="425" y="69">1</text><text x="459" y="69">0</text><text x="493" y="69">1</text><text x="527" y="69">1</text><text x="561" y="69">0</text><text x="595" y="69">1</text><text x="629" y="69">1</text><text x="663" y="69">0</text><text x="697" y="69">1</text>
  </g>
  <text x="170" y="94" fill="currentColor" font-family="monospace">BRR = 0x016D</text>
  <text x="280" y="94" fill="currentColor">364.58 → D = 365 → mantissa 22, fraction 13 (22.8125)</text>
  <text x="10" y="147" fill="currentColor" font-weight="bold">OVER8=0</text>
  <text x="10" y="163" fill="currentColor">45 MHz / 9600 (carry)</text>
  <rect x="170" y="130" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="204" y="130" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="238" y="130" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="272" y="130" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="306" y="130" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="340" y="130" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="374" y="130" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="408" y="130" width="34" height="24" class="f-acc" stroke="currentColor"/>
  <rect x="442" y="130" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="476" y="130" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="510" y="130" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="544" y="130" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="578" y="130" width="34" height="24" class="f-ok" stroke="currentColor"/><rect x="612" y="130" width="34" height="24" class="f-ok" stroke="currentColor"/><rect x="646" y="130" width="34" height="24" class="f-ok" stroke="currentColor"/><rect x="680" y="130" width="34" height="24" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="187" y="147">0</text><text x="221" y="147">0</text><text x="255" y="147">0</text><text x="289" y="147">1</text><text x="323" y="147">0</text><text x="357" y="147">0</text><text x="391" y="147">1</text><text x="425" y="147">0</text><text x="459" y="147">0</text><text x="493" y="147">1</text><text x="527" y="147">0</text><text x="561" y="147">1</text><text x="595" y="147">0</text><text x="629" y="147">0</text><text x="663" y="147">0</text><text x="697" y="147">0</text>
  </g>
  <text x="170" y="172" fill="currentColor" font-family="monospace">BRR = 0x1250</text>
  <text x="280" y="172" fill="currentColor">4687.5 → D = 4688 = 293·16 + 0 (fraction 16 → carry)</text>
  <text x="10" y="225" fill="currentColor" font-weight="bold">OVER8=1</text>
  <text x="10" y="241" fill="currentColor">42 MHz / 921600</text>
  <rect x="170" y="208" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="204" y="208" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="238" y="208" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="272" y="208" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="306" y="208" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="340" y="208" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="374" y="208" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="408" y="208" width="34" height="24" class="f-acc" stroke="currentColor"/>
  <rect x="442" y="208" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="476" y="208" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="510" y="208" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="544" y="208" width="34" height="24" class="f-acc" stroke="currentColor"/><rect x="578" y="208" width="34" height="24" class="f-warn" stroke="currentColor"/><rect x="612" y="208" width="34" height="24" class="f-ok" stroke="currentColor"/><rect x="646" y="208" width="34" height="24" class="f-ok" stroke="currentColor"/><rect x="680" y="208" width="34" height="24" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="187" y="225">0</text><text x="221" y="225">0</text><text x="255" y="225">0</text><text x="289" y="225">0</text><text x="323" y="225">0</text><text x="357" y="225">0</text><text x="391" y="225">0</text><text x="425" y="225">0</text><text x="459" y="225">0</text><text x="493" y="225">1</text><text x="527" y="225">0</text><text x="561" y="225">1</text><text x="595" y="225">0</text><text x="629" y="225">1</text><text x="663" y="225">1</text><text x="697" y="225">0</text>
  </g>
  <text x="170" y="250" fill="currentColor" font-family="monospace">BRR = 0x0056</text>
  <text x="280" y="250" fill="currentColor">45.57 → D = 46 = 5·8 + 6 → 5&lt;&lt;4 | 6, bit3 = 0</text>
  <text x="10" y="294" fill="currentColor">OVER8=0 이면 BRR 은 D(=16·USARTDIV) 그 자체. 경고색 bit3 은 OVER8=1 에서 반드시 0 (소수부 3비트).</text>
</svg>
```

**반올림 규칙.** "가장 가까운 D" 를 `(2·fck + baud) / (2·baud)` 로 구한다 (정확히 .5 면 올림). 45 MHz / 9600 은 fck/baud = 4687.5 로 정확한 동점인데, 올림(4688) 이 baud 오차도 더 작다: 9598.98 (−106.6 ppm) vs 내림 4687 → 9601.02 (+106.7 ppm). baud 오차는 `1/D` 에 비례해서 같은 거리면 큰 D 쪽이 조금 유리하다.

수학적으로 한 가지 더: "D 오차 최소" 와 "baud 오차 최소" 의 경계는 산술평균 D+0.5 가 아니라 **조화평균 2D(D+1)/(2D+1)** 이다. 그 사이 좁은 창(폭 ≈ 1/(4D+2))에서는 최근접 D 가 최적이 아니다. `demo_brr_table.c` 로 세어 보면 (실측):

```text
fck=16000000: baud 1200..1000000 (998801개) 중 최근접≠최소오차 7779개 (최대 손해 1838 ppm @969697), &0xF carry 버그 영향 20282개
fck=42000000: baud 1200..2625000 (2623801개) 중 최근접≠최소오차 20495개 (최대 손해 1838 ppm @2545455), &0xF carry 버그 영향 53367개
```

최대 손해 1838 ppm 은 D ≈ 16.5 (가능한 가장 빠른 쪽)에서 `0.5/16 − 0.5/17` 이고, D 가 수백인 실사용 영역에서는 몇 ppm 이다. RM 과 HAL 이 "가장 가까운 값" 을 쓰는 것으로 충분한 이유다. 레슨의 테스트는 RM 방식(최근접)을 정답으로 둔다.

**carry.** RM0090 의 예제 (기억 기준 — 확인 권장): USARTDIV = 25.62 → fraction 16×0.62 = 9.92 → 10 → BRR 0x19A; USARTDIV = 50.99 → 16×0.99 = 15.84 → **16 → 넘침 → mantissa 51, BRR 0x330**. 테스트는 fck 를 맞춘 합성 값(4 099 200 / 10 000, 8 158 400 / 10 000)으로 둘 다 재현하고, 실제 클럭에서 나는 carry 로 45 MHz(F429 의 APB1) / 9600 (OVER8=0) 과 45 MHz / 19200 (OVER8=1, 2343.75 → 2344 = 293·8 + 0) 을 넣었다. "mantissa 를 floor 로, fraction 을 따로 반올림" 하는 흔한 구현을 세 가지로 돌린 결과 (`demo_brr_table` 실측):

```text
carry 사례 (OVER8=0): 세 가지 두 단계 구현 vs 정답
  fck=8158400 baud=10000 fck/baud=815.840: &0xF→0x320(+1.98%)  |→0x330(-0.02%)  +→0x330(-0.020%)
  fck=45000000 baud=9600 fck/baud=4687.500: &0xF→0x1240(+0.33%)  |→0x1250(-0.01%)  +→0x1250(-0.011%)
  fck=16000000 baud=41700 fck/baud=383.693: &0xF→0x170(+4.26%)  |→0x170(+4.26%)  +→0x180(-0.080%)
  fck=42000000 baud=50500 fck/baud=831.683: &0xF→0x330(+1.92%)  |→0x330(+1.92%)  +→0x340(-0.038%)
```

- `& 0xF` 로 자르면 carry 가 사라져 USARTDIV 가 거의 1 작아진다 → +2~4 %, 8N1 수신 허용 오차를 넘길 수 있다 (허용치 이론은 COMM-B03).
- `|` 로 붙이면 16 = 0x10 이 bit4 에 OR 된다. mantissa 가 짝수면 우연히 +1 이 되어 맞고, **홀수면 이미 1 인 bit4 에 OR 해서 아무 일도 없다** — 테스트 벡터에 따라 통과했다 실패했다 하는 최악의 버그.
- `+` 로 더하면 항상 맞다. 처음부터 D 하나로 계산하면 이 고민 자체가 없다.

**실제 표.** `demo_brr_table` 출력 중 F407 (APB1 42 MHz = USART2/3, APB2 84 MHz = USART1/6) 부분:

```text
fck(MHz)  baud     | OVER8=0: BRR    USARTDIV   err%   | OVER8=1: BRR    err%
  42.0      9600  |  0x1117   273.4375   +0.000 |  0x2227   +0.000
  42.0    115200  |  0x016D    22.8125   -0.114 |  0x02D5   -0.114
  42.0    921600  |  0x002E     2.8750   -0.928 |  0x0056   -0.928
  42.0   2000000  |  0x0015     1.3125   +0.000 |  0x0025   +0.000
  84.0    115200  |  0x02D9    45.5625   +0.023 |  0x05B1   +0.023
  16.0    921600  |  0x0011     1.0625   +2.124 |  0x0021   +2.124
  16.0   2000000  |  (불가)                   |  0x0010   +0.000
```

두 OVER8 열의 오차가 **항상 같다** — D 가 같기 때문이다. F4 에서 OVER8=1 은 분해능을 올려 주지 않는다. 바뀌는 것은 범위(최고 baud 가 fck/16 → fck/8)와 수신 허용 오차(샘플이 8개라 줄어듦 — RM 의 허용 오차 표, 확인 권장)뿐이다. 16 MHz HSI 로 921600 을 쓰면 +2.1 % 라서 위험하다는 것도 이 표가 바로 알려 준다.

**오차 ppm.** `err = (fck − baud·D) / (baud·D)`. 42 MHz / 115200: D = 365, 분자 = 42 000 000 − 42 048 000 = −48 000, 분모 = 42 048 000 → −1141.6 → **−1142 ppm** (= −0.114 %). 모두 `int64_t` 정수 연산이라 float 동등 비교가 없다.

**초기화 순서.** RM0090 의 송신 절차(확인 권장)는 "UE=1 → M → STOP → BRR → TE=1 (첫 전송으로 idle 프레임)" 이다. 정답 코드가 만든 실제 접근 로그 (`uart_init(USART2, 42 MHz, 115200, 0)`, APB1ENR 은 TIM2EN 이 켜진 0x1 에서 시작):

| # | 접근 | 주소 | 값 | 이유 |
|---|---|---|---|---|
| 1 | R | 4002 3840 APB1ENR | 0000 0001 | RMW 의 R — 다른 enable 비트 보존 |
| 2 | W | 4002 3840 | 0002 0001 | USART2EN (bit17) |
| 3 | R | 4002 3840 | 0002 0001 | read-back: 버스 쓰기 완료 후 접근 (BM-B03) |
| 4 | W | 4000 440C CR1 | 0000 2000 | UE 만 |
| 5 | W | 4000 4410 CR2 | 0000 0000 | 1 stop bit |
| 6 | W | 4000 4408 BRR | 0000 016D | TE/RE 가 꺼진 동안 |
| 7 | W | 4000 440C CR1 | 0000 200C | UE + TE + RE |

모델은 TE/RE 가 켜진 상태에서 BRR 을 쓰면 `brr_while_on` 을 올린다 (RM: 통신 중 BRR 변경 금지, 확인 권장). 핀을 AF7 (PA2 = TX, PA3 = RX) 로 바꾸는 GPIO 설정은 BM-B11 의 일이라 여기서는 생략했다.

### Part B — putc / getc: 무엇이 무엇을 지우나

`putc('O'); putc('K'); flush();` 를 정답 코드로 돌린 실제 SR/DR 접근 (주소 하위 12비트, `SR` 값의 0x80 = TXE, 0x40 = TC):

| tick | 접근 | 값 | 모델 상태 |
|---|---|---|---|
| 1 | R SR | C0 | TXE=1, TC=1 (리셋값). SR 읽음 → clear 시퀀스 준비 |
| 1 | W DR | 4F 'O' | TC clear. TDR → 시프트 레지스터로 **즉시** 이동 → TXE 는 계속 1 |
| 2 | R SR | 80 | TXE=1, TC=0 |
| 2 | W DR | 4B 'K' | TDR = 'K' (시프트 레지스터는 'O' 전송 중) → TXE=0 |
| 3–10 | R SR ×8 | 00 | 기다림 |
| 11 | R SR | 80 | 'O' 의 stop 비트 끝 → 'K' 가 시프트로 이동 → TXE=1 (**여기서 TXE 대기는 끝난다**) |
| 12–20 | R SR ×9 | 80 | 'K' 전송 중, TC=0 |
| 21 | R SR | C0 | 'K' 끝, TDR 비었음 → **TC=1** |

규칙을 정리하면:

- **TXE (r)**: DR 쓰기로 0, TDR → 시프트 이동으로 1. 소프트웨어가 직접 못 지운다.
- **TC (rc_w0)**: 마지막 프레임이 끝나고 TDR 도 비었을 때 1. "SR 읽기 → DR 쓰기" 시퀀스나 0 쓰기로 clear. putc 는 항상 SR 을 먼저 읽으므로 자연히 시퀀스가 완성된다. 그래서 SR 을 읽지 않고 DR 에 바로 쓰는 코드는 TC 가 안 지워진 채 남을 수 있다 (모델은 이 RM 문장을 그대로 따른다 — 실칩 동작은 확인 권장).
- **RXNE (rc_w0)**: RDR 에 바이트가 오면 1, DR 읽기로 0 (rc_r 처럼 동작).
- **ORE / FE / NF / PE (r)**: 받은 바이트와 함께 1. **"SR 읽기 → DR 읽기"** 시퀀스로만 clear. 새 STM32 (F0/F3/L4/G4/H7 의 USARTv2) 는 ISR/ICR 로 나뉘어 ICR 에 1 을 써서 지운다 (W1C, BM-B04) — 계열마다 다르니 RM 확인.

**ORE 재현.** `"abc"` 를 주입하고 main 이 35 tick 동안 안 읽은 실제 상태:

```text
t+10 sr=60 rdr=a ore_lost=0      ← 'a' 도착: RXNE(0x20) + TC(0x40, 송신 안 했으니 리셋값 그대로)
t+20 sr=68 rdr=a ore_lost=1      ← 'b' 도착: RDR 이 안 비었다 → ORE(0x08), 'b' 유실
t+30 sr=68 rdr=a ore_lost=2      ← 'c' 도 유실
getc rc=-3 c=a sr after=40 tick=36
```

F4 RM 의 설명 (확인 권장) 대로 RDR 의 옛 값 'a' 는 남고 새 바이트가 사라진다. 그래서 getc 는 **'a' 를 돌려주면서** `UART_EOVERRUN` 을 보고한다 — "이 바이트는 유효하지만 그 뒤가 끊겼다". 프로토콜 계층은 이걸 보고 프레임을 버리고 재동기화한다 (COMM-B10). 폴링으로 ORE 를 없애는 근본책은 없다: 바이트 하나 시간(115200 에서 86.8 µs) 안에 반드시 읽어야 하는데 main 루프는 그걸 보장 못 한다 → RXNE 인터럽트 + 링버퍼 (BM-M04) 또는 DMA (BM-M08).

**SR 스냅숏이 왜 필요한가.** 오류 비트는 DR 을 읽는 순간 지워진다. `if (REG_READ(SR) & RXNE) { c = REG_READ(DR); if (REG_READ(SR) & ORE) ... }` 는 두 번째 SR 에서 ORE 가 이미 0 이라 오류를 영영 못 본다. 반대로 DR 을 먼저 읽고 SR 을 나중에 읽으면 시퀀스가 "DR → SR" 이라 오류 비트가 **안 지워지고 다음 바이트에 붙어서** 보고된다. 테스트는 접근 로그의 마지막 두 항목이 `R SR`, `R DR` 순서인지 확인한다.

**같은 코드의 타깃 버전.** `UNIT_TEST` 를 지우고 드라이버 부분만 Cortex-M4 로 컴파일한 실제 출력 (`/tmp/BM-B12_work` 에서, Apple clang 21):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -ffreestanding -std=c11 -Os -Wall -Wextra -Werror -c drv.c -o drv_os.o
xcrun llvm-objdump -d --no-show-raw-insn drv_os.o
```

```text
00000152 <uart_putc>:
     152:      	mov.w	r2, #0x3e8          ; 1000 = UART_TX_TIMEOUT
     156:      	nop
     158:      	ldr	r3, [r0]                ; SR (volatile → 매번 진짜로 읽는다)
     15a:      	lsls	r3, r3, #0x18       ; bit7 (TXE) → N 플래그
     15c:      	bmi	0x16a <uart_putc+0x18>
     15e:      	subs	r2, #0x1
     160:      	itt	eq
     162:      	mvneq	r0, #0x1            ; return -2 (ETIMEOUT)
     166:      	bxeq	lr
     168:      	b	0x158 <uart_putc+0x6>
     16a:      	str	r1, [r0, #0x4]          ; DR = c
     16c:      	movs	r0, #0x0
     16e:      	bx	lr
00000170 <uart_getc>:
     ...                                  ; cbz: out == NULL / timeout == 0 처리
     174:      	ldr.w	r12, [r0]           ; sr 스냅숏 (r12 에 보관)
     178:      	lsls.w	r3, r12, #0x1a      ; bit5 (RXNE) → N
     17c:      	bmi	0x18e <uart_getc+0x1e>
     ...
     18e:      	ldr	r0, [r0, #0x4]          ; DR 읽기 (RXNE·오류 비트 clear)
     190:      	lsls.w	r2, r12, #0x1c      ; 스냅숏의 bit3 (ORE) 검사
     194:      	strb	r0, [r1]
```

(`;` 주석은 덧붙인 것.) 비트 검사가 `lsls #(31−n)` + `bmi` 로 바뀌고, 오류 판정은 다시 읽지 않고 레지스터 r12 의 스냅숏으로 한다. `-O2` 에서는 같은 루프가 25번 펼쳐져 putc 하나가 196바이트, 전체 오브젝트 text 가 2086 B (`-Os` 732 B) 가 된다 — 폴링 루프는 빨리 돌아 봐야 이득이 없으니 드라이버는 `-Os` 가 맞다.

### Part C — TXE 는 "레지스터가 비었다", TC 는 "선이 비었다"

`./solution -v` 의 실제 출력 (`"OK"` 를 보내고 전원 차단):

```text
[TXE 만 기다리고 전원 차단] "OK"
  tick   1  LOAD 'O'  TDR→시프트 (TXE=1)
  tick  11  DONE 'O'  stop 비트 끝
  tick  11  LOAD 'K'  TDR→시프트 (TXE=1)
  tick  11  POWER OFF
  captured="O" aborted_frames=1 aborted_bits=0
[TC 까지 기다리고 전원 차단] "OK"
  tick   1  LOAD 'O'  TDR→시프트 (TXE=1)
  tick  11  DONE 'O'  stop 비트 끝
  tick  11  LOAD 'K'  TDR→시프트 (TXE=1)
  tick  21  DONE 'K'  stop 비트 끝
  tick  21  TC=1
  tick  21  POWER OFF
  captured="OK" aborted_frames=0 aborted_bits=0
```

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">"OK" 송신: TXE 와 TC 는 정확히 한 프레임(10 tick) 차이 — 위 -v 출력과 같은 tick</text>
  <text x="10" y="50" fill="currentColor">CPU</text>
  <text x="140" y="50" fill="currentColor" font-family="monospace">t1 W DR 'O', t2 W DR 'K', 그 뒤 SR 폴링</text>
  <text x="10" y="95" fill="currentColor">TDR</text>
  <rect x="182" y="80" width="234" height="22" class="f-mut" stroke="currentColor"/>
  <text x="290" y="96" fill="currentColor" font-family="monospace">'K' 대기</text>
  <text x="10" y="130" fill="currentColor">시프트 → TX 핀</text>
  <rect x="156" y="115" width="260" height="22" class="f-acc" stroke="currentColor"/>
  <text x="270" y="131" fill="currentColor" font-family="monospace">'O' 10 비트</text>
  <rect x="416" y="115" width="260" height="22" class="f-acc" stroke="currentColor"/>
  <text x="530" y="131" fill="currentColor" font-family="monospace">'K' 10 비트</text>
  <text x="10" y="168" fill="currentColor">TXE</text>
  <path d="M130 152 H182 V172 H416 V152 H702" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="10" y="208" fill="currentColor">TC</text>
  <path d="M130 192 H156 V212 H676 V192 H702" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M130 232 H702" stroke="currentColor" fill="none"/>
  <path d="M130 228 V236 M156 228 V236 M182 228 V236 M416 228 V236 M676 228 V236" stroke="currentColor" fill="none"/>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="130" y="250">0</text><text x="156" y="250">1</text><text x="182" y="250">2</text><text x="416" y="250">11</text><text x="676" y="250">21</text><text x="716" y="250">tick</text>
  </g>
  <path d="M416 66 V232" stroke="currentColor" stroke-dasharray="4 3" fill="none"/>
  <path d="M676 66 V232" stroke="currentColor" stroke-dasharray="4 3" fill="none"/>
  <rect x="10" y="262" width="12" height="12" class="f-warn" stroke="currentColor"/>
  <text x="30" y="273" fill="currentColor">tick 11: TXE=1 을 보고 전원 차단 → 'K' 는 막 시프트에 들어가 0 비트 나감 → 상대가 받은 것 "O"</text>
  <rect x="10" y="284" width="12" height="12" class="f-ok" stroke="currentColor"/>
  <text x="30" y="295" fill="currentColor">tick 21: TC=1 을 보고 전원 차단 → "OK" 전부 도착</text>
  <text x="10" y="320" fill="currentColor">차이 10 tick = 한 프레임 = 115200 baud 에서 86.8 µs. 그 사이 전원이 꺼지면 마지막 바이트가 잘린다.</text>
</svg>
```

테스트는 `"SHUTDOWN OK\r\n"` (13바이트) 로 같은 것을 확인한다: TXE 만 기다리면 캡처 12바이트 (`\n` 없음, `aborted_frames == 1`), TC 를 기다리면 13바이트. 시간도 고정값으로 잰다: `write("OK")` 가 tick 2 에 돌아오고, TXE 대기는 tick 11, TC 대기는 tick 21 에 끝난다 (`t_tc − t_txe == frame_ticks`).

**실무에서 TC 를 기다려야 하는 곳** — 공통점은 "곧 선을 놓는다":

1. 전원 차단, 리셋(`NVIC_SystemReset()` 전에 "rebooting..." 출력), 부트로더 → 앱 점프 (BM-A03).
2. Stop/Standby 진입 (BM-M11) — 클럭이 멈추면 시프트 레지스터도 멈춘다. RM0090 도 "USART 를 끄거나 Halt 모드로 가기 전에 TC=1 을 기다려 마지막 전송이 깨지지 않게" 라고 쓴다 (확인 권장).
3. RS-485 반이중의 DE 핀 해제 (연습 4) — DE 를 일찍 내리면 마지막 바이트가 버스에서 잘린다. 새 STM32 USART 의 하드웨어 DE (CR3.DEM) 는 이걸 대신 해 준다 (계열별 확인).
4. baud 변경·UE=0·핀을 GPIO 로 되돌리기.

평소의 연속 송신에는 TXE 만 본다 — TC 까지 기다리면 바이트 사이에 빈 시간이 생겨 처리량이 떨어진다.

**타임아웃.** 정상 동작에서 putc 는 최대 두 프레임, flush 도 두 프레임이면 끝난다. 그래도 상한을 두는 이유는 "정상이 아닐 때" 다: 클럭을 안 켰으면 SR 이 0 으로 읽히고(모델이 그렇게 한다, 실제 칩에서도 클럭이 꺼진 주변장치는 응답이 없다 — BM-B03), TE 를 안 켰거나 CTS 흐름 제어가 막혀도 TXE/TC 가 영원히 안 온다. 테스트는 클럭 없이 putc 가 정확히 1000 번 폴링 후 `UART_ETIMEOUT` 을 돌려주는지 확인한다. 실제 펌웨어에서는 폴링 횟수 대신 SysTick ms (BM-B13) 나 DWT 사이클로 상한을 둔다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-B12_so && /tmp/BM-B12_so      # 52/52 passed
/tmp/BM-B12_so -v                                                                         # 5. 풀이의 TXE/TC 사건 기록
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/BM-B12_as && /tmp/BM-B12_as   # 52/52, 보고 없음
```

관찰용 파일:

<!-- code: demo_brr_table.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_brr_table.c -o /tmp/BM-B12_demo && /tmp/BM-B12_demo   # BRR 표 · carry 버그 · 조화평균 경계 (0.1 s)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `BRR = fck / baud;` | 반올림 없이 버림. 42 MHz/115200 → 364 (+0.160 %) — 365 (−0.114 %) 보다 나쁘다 | `(2ull*fck + baud) / (2ull*baud)` |
| mantissa floor + `round(frac*16) & 0xF` | fraction 이 16 이면 0 이 되고 carry 가 사라짐 → +1.98 % (RM 의 50.99 예), +4.26 % (16 MHz/41700) | D 를 먼저 반올림하고 `D/16`, `D%16` |
| `(mant << 4) \| frac` (frac 이 16 일 수 있음) | mantissa 짝수면 우연히 맞고 홀수면 틀림 | `+` 로 더하거나 D 로 계산 |
| OVER8=1 인데 `BRR = D` | fraction 이 4비트처럼 들어가 bit3 오염. 42 MHz/921600: 0x2E → mant 2 + 6/8 = 2.75 → 1.909 Mbaud (+107 %) | `((D >> 3) << 4) \| (D & 7)` |
| fck 에 SYSCLK 168 MHz 를 넣음 | USART2 는 APB1 (42 MHz) 클럭을 쓴다 → baud 가 1/4 로 나온다 | 버스 클럭 = SYSCLK / AHB / APBx (BM-B10) |
| putc 가 TXE 확인 없이 `DR = c` | TDR 이 찼는데 덮어씀 → 앞 바이트 유실 (모델 `tx_overwrite`) | `while (!(SR & TXE))` (상한 포함) |
| 전원 차단/슬립/DE 해제 전에 TXE 만 확인 | 마지막 바이트가 시프트 레지스터에 있다 | `uart_flush()` = TC 대기 |
| getc 에서 DR 을 읽은 뒤 SR 을 다시 읽어 오류 판정 | DR 읽기가 ORE/FE 를 지웠다 → 오류를 못 봄 | SR 스냅숏 하나로 판정 |
| 폴링 루프에 상한 없음 | 클럭·TE·CTS 문제 하나로 펌웨어 전체가 멈춤 (워치독만이 살림) | 횟수/시간 상한 + 오류 코드 |
| ORE 를 무시하고 바이트만 넘김 | 스트림 중간이 빠진 걸 상위 계층이 모른다 | `UART_EOVERRUN` 보고 → 프레임 재동기화 |

## 8. 면접 Q&A

<details>
<summary>Q1. TXE 와 TC 의 차이는? 각각 언제 기다리나요?</summary>

TXE = TDR(송신 데이터 레지스터)이 비었다 → 다음 바이트를 DR 에 써도 된다. TC = TDR 도 비었고 시프트 레지스터의 마지막 stop 비트까지 선 위로 나갔다. 연속 송신은 TXE 로 (TC 를 기다리면 바이트 사이에 공백이 생긴다). 선을 놓기 직전 — 전원 차단, 리셋, 저전력 진입, RS-485 DE 해제, baud 변경, UE=0 — 에는 TC. 두 사건의 시간 차는 정확히 한 프레임 (115200 8N1 에서 86.8 µs). 꼬리질문 "TC 는 어떻게 지워지나": SR 읽기 → DR 쓰기 시퀀스, 또는 SR 의 TC 에 0 쓰기 (rc_w0, RM0090 기준).

</details>

<details>
<summary>Q2. PCLK1 = 42 MHz 에서 115200 baud 의 BRR 을 손으로 계산해 보세요.</summary>

USARTDIV = 42e6 / (16 × 115200) = 22.786. fraction = 0.786 × 16 = 12.58 → 13, mantissa 22 → BRR = 22 << 4 | 13 = 0x16D. 실제 baud = 42e6 / 365 = 115068 → −0.114 %. 지름길: OVER8=0 이면 BRR = round(fck / baud) = round(364.58) = 365 = 0x16D. 꼬리질문 "fraction 이 16 으로 반올림되면?" → mantissa 로 carry (RM 예: 50.99 → 0x330). D 하나로 계산하면 자동.

</details>

<details>
<summary>Q3. OVER8=1 로 하면 baud 정확도가 좋아지나요?</summary>

F4 에서는 아니다. BRR 이 뜻하는 정수 D = fck / baud 가 두 모드에서 같다 (OVER8=0 은 1/16 단위 4비트 fraction, OVER8=1 은 1/8 단위 3비트 fraction — 단위가 같이 반으로 줄어든다). 그래서 오차 표의 두 열이 똑같다. OVER8=1 의 의미는 최고 baud 가 fck/16 → fck/8 로 두 배가 되는 것 (84 MHz → 10.5 Mbaud). 대가로 비트당 샘플이 8개라 클럭 오차·잡음 허용도가 줄어든다 (COMM-B02/B03). 느린 쪽 한계는 오히려 16x 가 넓다 (mantissa 4095 제한).

</details>

<details>
<summary>Q4. ORE 는 언제 생기고 폴링 드라이버에서 어떻게 다루나요? 왜 SR 을 먼저 읽고 DR 을 읽나요?</summary>

RDR 을 아직 안 읽었는데(RXNE=1) 다음 바이트가 수신 완료되면 ORE. F4 는 RDR 의 옛 바이트를 남기고 새 바이트를 버린다 (RM 기준, 확인 권장). ORE·FE·NF·PE 는 "SR 읽기 → DR 읽기" 시퀀스로 지워지므로, SR 을 한 번 읽어 스냅숏을 들고 DR 을 읽은 뒤 스냅숏으로 판정한다. 바이트는 유효하니 돌려주고, 오류 코드로 "그 뒤가 끊겼다" 를 알린다. 근본 해결은 폴링을 버리는 것: RXNE 인터럽트 + 링버퍼 (BM-M04) 나 DMA (BM-M08). 꼬리질문 "새 STM32 에서는?" — ICR 의 ORECF 에 1 을 써서 지운다 (W1C, BM-B04, RM 확인 권장). 인터럽트 UART 에서 ORE 를 안 지우면 RXNEIE 로 같은 IRQ 가 계속 다시 걸리는 것이 단골 버그다 (BM-M04).

</details>

<details>
<summary>Q5. 폴링 루프에 왜 타임아웃이 필요하죠? 정상이라면 TXE 는 반드시 오잖아요.</summary>

"정상" 이 아닐 때가 문제다: RCC 클럭을 안 켰으면 레지스터가 0 으로 읽히고, TE=0, CTS 흐름 제어로 막힘, 클럭 설정 실수로 baud 가 극단적으로 느림 등. 상한이 없으면 디버그 printf 한 줄이 펌웨어 전체를 멈추고, 증상은 "가끔 부팅이 안 된다" 로 나타난다. 상한은 폴링 횟수보다 시간(SysTick, DWT CYCCNT)이 좋다 — 코어 클럭이 바뀌어도 의미가 같다. 오류 코드를 돌려주면 상위에서 재초기화나 무시를 고를 수 있다.

</details>

<details>
<summary>Q6. 115200 baud 에서 printf 로 100바이트 로그를 찍으면 무슨 일이 생기나요?</summary>

8N1 이면 바이트당 10비트 → 11 520 B/s → 100바이트 = 8.68 ms 동안 CPU 가 TXE 폴링에 묶인다. 1 kHz 제어 루프라면 8주기를 놓친다. 그래서 실무 printf 는 ISR + TX 링버퍼 (BM-M04) 나 DMA 로 보내고, 링버퍼가 차면 버릴지 기다릴지 정책을 정한다. 폴링 printf 는 부팅 초기, fault 핸들러(인터럽트를 믿을 수 없을 때), 전원 차단 직전처럼 "동기적으로 확실히 나가야 하는" 곳에 남긴다. 리타겟팅 자체는 BM-B09.

</details>

<details>
<summary>Q7. 새 보드에서 TX 핀에 아무것도 안 나옵니다. 무엇부터 보나요?</summary>

순서대로: ① RCC 클럭 enable (USART + GPIO 포트) — 레지스터를 읽어 봤을 때 쓴 값이 안 남아 있으면 클럭 문제. ② 핀 AF 설정 (F4 의 USART2 = PA2/PA3 AF7, 데이터시트 표 확인) 과 MODER=AF. ③ CR1 의 UE/TE. ④ BRR 계산에 쓴 클럭이 실제 PCLK 인지 (APB 분주, PLL 이 실제로 잠겼는지). ⑤ 스코프로 비트 폭을 잰다 — 115200 이면 8.68 µs. 2배·4배로 나오면 클럭 가정이 틀린 것. ⑥ TX/RX 크로스, 레벨 (3.3 V TTL vs RS-232). SSD bring-up 때 "콘솔이 깨진 글자로 나온다" 는 대개 ④ 다.

</details>

<details>
<summary>Q8. baud 오차는 얼마까지 괜찮나요?</summary>

송신기와 수신기의 오차가 더해진 상대 오차가 문제다. 수신기는 start 비트의 하강 에지에 맞춰 각 비트 가운데를 샘플하므로 마지막 비트(stop)까지 누적된 어긋남이 반 비트보다 작아야 한다 → 10비트 프레임이면 이론상 ±5 % 근처, 샘플링 창과 에지 검출 지연을 빼면 실제 허용치는 그보다 작다. 유도와 실측 스윕은 COMM-B03 에 있다. 실무 감각으로는 한쪽 오차를 ±1~2 % 안에 두라고 하며 (경험칙, 칩별 RM 의 허용 오차 표 확인), 위 표에서 16 MHz / 921600 의 +2.1 % 는 이미 위험 구간이다.

</details>

## 9. 추가 연습

### 연습 1 — BRR 한 줄 공식

OVER8=0 에서 `BRR == round(fck/baud)` 임을 보이고, OVER8=1 에서 D 를 BRR 로 바꾸는 식을 써라. 4개 fck × 수십만 개 baud 에서 본문 방식과 같은지 전수 비교하라.

<details>
<summary>답 보기</summary>

OVER8=0: `BRR = mant·16 + frac = D` (mant = D/16, frac = D%16 이므로 그대로 D). 그래서 `BRR = (2·fck + baud) / (2·baud)` 한 줄. OVER8=1: `BRR = ((D >> 3) << 4) | (D & 7)` — 하위 3비트는 그대로, 나머지는 한 칸 왼쪽으로 밀어 bit3 을 비운다. practice.c 는 16/42/45/84 MHz 에서 baud 1200 부터 fck/16 까지 7 간격으로 100만 개 이상을 비교한다 (불일치 0, bit3 위반 0).

</details>

### 연습 2 — OVER8 고르기

`int uart_pick_over8(fck, baud)` — 0, 1, 또는 불가(−1). 오차가 같다면 무엇을 기준으로 고르나?

<details>
<summary>답 보기</summary>

D 가 같으니 오차도 같다 → 범위가 허락하면 OVER8=0 (16x 오버샘플링이 클럭 오차·잡음에 강함). `D ≥ 16 && D/16 ≤ 4095` → 0, 아니면 `D ≥ 8 && D/8 ≤ 4095` → 1, 둘 다 아니면 −1. 42 MHz: 2 625 000 → 0 (D=16), 3 000 000 → 1 (D=14), 5 250 000 → 1 (D=8), 6 000 000 → −1 (D=7), 600 → −1 (D=70000: 두 모드 모두 mantissa 초과), 1200 → 0.

</details>

### 연습 3 — `uart_puts` 와 줄바꿈

`'\n'` 을 `"\r\n"` 으로 바꿔 보내고 선 위로 나간 바이트 수를 돌려주는 `uart_puts` 를 써라. 터미널 프로그램(minicom, PuTTY)이 줄을 계단처럼 찍는 이유도 설명하라.

<details>
<summary>답 보기</summary>

`'\n'` 앞에 `'\r'` 을 먼저 putc 하고 카운트한다: `"a\nb\n"` → `"a\r\nb\r\n"` 6바이트. LF 는 "다음 줄", CR 은 "줄 처음으로" 라서 LF 만 보내면 터미널이 커서를 아래로만 내려 계단 모양이 된다. 변환은 드라이버가 아니라 그 위 계층(리타겟팅 `_write`, BM-B09)에 두는 경우가 많다 — 바이너리 프로토콜을 같은 UART 로 보낼 때 바이트가 바뀌면 안 되기 때문.

</details>

### 연습 4 — RS-485 DE 핀

반이중 RS-485 트랜시버의 DE(드라이버 enable)를 PA1 으로 제어한다. `rs485_send(buf, n, wait_tc)` 를 쓰고, TXE 만 기다린 뒤 DE 를 내리면 버스 반대편이 무엇을 받는지 모델로 보여라.

<details>
<summary>답 보기</summary>

`DE=1 → putc × n → (TC 대기) → DE=0`. 모델은 "프레임 내내 DE=1 이었던 바이트만 버스에 도착" 으로 판정한다. `"PING"` 을 TXE 만 기다리고 보내면 TX 핀에서는 4바이트가 다 나가지만 버스에는 `"PIN"` 만 도착하고 (`truncated == 1`), TC 를 기다리면 4바이트 전부. DE 가 내려가는 tick 은 TC 가 1 이 된 tick 과 같다 (4 프레임 × 10 + 첫 폴링 1 = 41). 반대 방향 실수 — TC 를 기다린 뒤에도 DE 를 너무 오래 잡고 있으면 상대의 응답 첫 바이트와 충돌 — 도 있어서 Modbus RTU 같은 프로토콜은 응답 지연 규칙을 둔다 (COMM-B13).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-B12_pr && /tmp/BM-B12_pr   # 18/18 passed
```

## 10. 핵심 정리

- `baud = fck / (8·(2−OVER8)·USARTDIV)`. BRR 이 뜻하는 정수 **D = round(fck/baud)**: OVER8=0 이면 BRR = D, OVER8=1 이면 `((D>>3)<<4) | (D&7)` (bit3 = 0). carry 는 D 로 계산하면 자동.
- 오차 = (fck − baud·D)/(baud·D). 42 MHz/115200 → 0x16D, −0.114 %. 두 OVER8 모드의 오차는 같고 범위만 다르다.
- 초기화: 클럭(RMW + read-back) → UE → CR2 → BRR → TE/RE. 핀 AF 는 GPIO 쪽 (BM-B11).
- TXE = TDR 비었음 (연속 송신용), TC = 시프트까지 끝 (선을 놓기 전: 전원·리셋·슬립·DE·baud 변경). 차이는 한 프레임.
- RXNE 는 DR 읽기로, ORE/FE/NF/PE 는 "SR 읽기 → DR 읽기" 로 clear → **SR 스냅숏 하나** 로 판정. ORE 면 바이트는 유효, 그 뒤가 유실.
- 모든 폴링 루프에 상한 + 오류 코드. 폴링은 바이트당 86.8 µs (115200) 동안 CPU 를 묶는다 → 본격 통신은 IRQ+링버퍼 (BM-M04) / DMA (BM-M08).

## 참고자료

- ST RM0090 *STM32F405/415, F407/417, F427/437, F429/439 Reference Manual* — USART 장: 분수 baud 생성(USARTDIV, OVER8, BRR 예제와 오차 표), 송신 절차와 TC, 수신기 오류 플래그와 clear 시퀀스, 레지스터 맵 (SR 리셋값, 비트 위치) — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>
- 각 계열 RM (RM0360 F0, RM0351 L4, RM0440 G4 등) 의 USART 장 — USARTv2 의 ISR/ICR, CR3.DEM 하드웨어 DE, CR3.OVRDIS: 계열별 차이 확인용
- STM32CubeF4 HAL `stm32f4xx_hal_uart.h` 의 BRR 계산 매크로 (`UART_BRR_SAMPLING16/8`) — 소수부 carry 처리 방식 비교용 (원문 확인 권장)
- Texas Instruments SLLA272 "The RS-485 Design Guide" — DE 제어와 반이중 턴어라운드
- 관련 레슨: COMM-B01 (UART 프레이밍), COMM-B02 (16x 오버샘플링, 노이즈 플래그), COMM-B03 (divisor 계산과 클럭 허용 오차 스윕), COMM-B10/B13 (프레임 재동기화, Modbus RTU), BM-B02 (mock 프레임워크), BM-B03 (클럭 게이팅과 read-back), BM-B04 (W1C · rc_w0), BM-B09 (printf 리타겟팅), BM-B10 (버스 클럭 계산), BM-B11 (GPIO AF), BM-B13 (ms 타임아웃), BM-M04 (인터럽트 UART + 링버퍼), BM-M08 (DMA), BM-M11 (저전력 진입 전 TC), C-M10 (mini printf)
