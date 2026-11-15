---
id: BM-B02
time: 60분
updated: 2026-11-11
---
# BM-B02 Mock 주변장치 프레임워크: read/write 훅

## 학습 목표

- 드라이버가 레지스터를 `REG_READ` / `REG_WRITE` 두 매크로로만 만지게 하고, 테스트 빌드(`UNIT_TEST`)에서는 그 매크로가 **함수 훅(hook)** 으로, 타깃 빌드에서는 `volatile` 메모리 접근으로 바뀌는 구조를 직접 만든다.
- 희소(sparse) 레지스터 표 + 주소별 read/write 콜백 + **접근 로그(access log)** + 리셋값 + "지도에 없는 주소" 탐지로 된 mock 코어를 정적 배열만으로 구현한다.
- 레퍼런스 매뉴얼의 접근 타입(access type) `rw / r / w / rc_w1(W1C) / rc_w0 / rc_r` 을 비트마스크로 기술하고, 무작위 property test 로 검증한다.
- 상태 비트가 저절로 바뀌는 주변장치(TXE 가 DR 쓰기 후 N tick 뒤 1, RX 큐 → RXNE, DR 읽기로 RXNE 클리어)를 tick 기반 모델로 만들고, 그 위에서 `uart_putc/getc` 의 **정확한 접근 순서**를 assert 한다.
- 같은 드라이버 소스를 Cortex-M4 로 컴파일해 진짜 `ldr/str` 를 확인하고, 매크로 · 링크 타임 · 함수 포인터 이음매(seam)의 비용 차이를 말할 수 있다.

## 1. 문제

보드는 아직 안 왔는데 드라이버는 써야 한다 — 펌웨어 팀의 일상이다. SSD 컨트롤러 bring-up 에서도 실리콘이 나오기 전에 FPGA·시뮬레이터로 펌웨어를 먼저 돌렸고, RF 모듈 보드에서도 UART 콘솔 드라이버가 틀렸는지 보드가 틀렸는지 가르는 게 첫 일이었다. 그래서 면접에서 **"레지스터를 직접 만지는 드라이버를 어떻게 유닛 테스트하나요?"** 는 단골 질문이고, 답의 핵심은 "레지스터 접근을 가로채는 이음매 + 주변장치처럼 반응하는 mock" 이다.

진짜 하드웨어 레지스터는 그냥 메모리가 아니다. 읽으면 값이 바뀌기도 하고(DR 을 읽으면 RXNE 가 꺼진다), 쓴 값과 다른 값이 남기도 하고(W1C: 1 을 쓰면 0 이 된다), 시간이 지나면 저절로 바뀐다(TXE). 그냥 `uint32_t` 배열로 흉내 내면 이런 동작을 하나도 못 잡는다. 이 레슨은 그 동작을 **주소별 훅**으로 넣는 작은 프레임워크를 만든다. BM-B03~B14 의 드라이버 레슨들이 이 프레임워크의 축약판("BM-B02 mock 축약판")을 그대로 쓴다.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">하나의 드라이버 소스, 두 개의 빌드</text>
  <rect x="250" y="32" width="240" height="46" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="262" y="51" fill="currentColor" font-weight="bold">드라이버 (공용 소스)</text>
  <text x="262" y="69" fill="currentColor" font-family="monospace" font-size="12">uart_init / putc / getc</text>
  <rect x="250" y="96" width="240" height="32" rx="6" fill="none" stroke="currentColor"/>
  <text x="262" y="117" fill="currentColor" font-family="monospace" font-size="12">REG_READ(a) / REG_WRITE(a, v)</text>
  <path d="M370 78 L370 96" stroke="currentColor"/>
  <path d="M300 128 L190 160" stroke="currentColor"/><path d="M440 128 L560 160" stroke="currentColor"/>
  <text x="150" y="150" fill="currentColor" font-size="12">#ifdef UNIT_TEST (맥)</text>
  <text x="520" y="150" fill="currentColor" font-size="12">#else (Cortex-M4)</text>
  <rect x="20" y="160" width="360" height="32" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="181" fill="currentColor" font-family="monospace" font-size="12">mock_read32(a) / mock_write32(a, v)</text>
  <rect x="20" y="206" width="360" height="56" rx="6" fill="none" stroke="currentColor"/>
  <text x="32" y="226" fill="currentColor" font-size="12">레지스터 표[16]: addr·val·reset·rd·wr·ctx</text>
  <text x="32" y="246" fill="currentColor" font-size="12">표에 없는 주소 → unexpected++, 로그 'r'/'w', 0 반환</text>
  <rect x="20" y="276" width="170" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="30" y="296" fill="currentColor">rd/wr 훅 → 모델</text>
  <text x="30" y="314" fill="currentColor" font-size="12">UART: TXE 타이머,</text>
  <text x="30" y="331" fill="currentColor" font-size="12">TX 캡처, RX 큐</text>
  <rect x="200" y="276" width="180" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="210" y="296" fill="currentColor">접근 로그 [256]</text>
  <text x="210" y="314" fill="currentColor" font-size="12">{R/W, addr, value}</text>
  <text x="210" y="331" fill="currentColor" font-size="12">접근 1회 = 1 tick</text>
  <path d="M200 192 L200 206" stroke="currentColor"/><path d="M105 262 L105 276" stroke="currentColor"/>
  <path d="M290 262 L290 276" stroke="currentColor"/>
  <rect x="420" y="160" width="300" height="32" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="432" y="181" fill="currentColor" font-family="monospace" font-size="12">*(volatile uint32_t *)a</text>
  <rect x="420" y="206" width="300" height="56" rx="6" fill="none" stroke="currentColor"/>
  <text x="432" y="226" fill="currentColor" font-family="monospace" font-size="12">ldr r3, [r0]   str r1, [r0, #0x4]</text>
  <text x="432" y="246" fill="currentColor" font-size="12">Thumb-2 load/store → APB1 버스</text>
  <rect x="420" y="276" width="300" height="70" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="432" y="296" fill="currentColor">USART2 @ 0x4000_4400</text>
  <text x="432" y="314" fill="currentColor" font-size="12">오프셋 SR 00 · DR 04 · BRR 08 · CR1 0C</text>
  <text x="432" y="331" fill="currentColor" font-size="12">mock 코드는 타깃 오브젝트에 0 바이트</text>
  <path d="M570 192 L570 206" stroke="currentColor"/><path d="M570 262 L570 276" stroke="currentColor"/>
</svg>
```

레지스터 주소·비트는 **STM32F4 (RM0090) 기준 대표값 — RM 확인 권장**. 테스트의 정답(ground truth)은 이 mock 이 정의한 동작이다.

### Part A — mock 코어: 표 · 훅 · 로그 · tick

- `mock_add_reg(addr, reset_val, rd, wr, ctx)` 로 레지스터를 표에 올린다 (값은 리셋값으로 시작).
- `mock_read32(addr)` / `mock_write32(addr, val)` (**프론트도어**, CPU 쪽 접근): 표에서 찾고 → 훅을 부르고 → 로그 `{'R'/'W', addr, CPU 가 본/쓴 값}` 을 남기고 → `mock_tick(1)`.
  - read 훅은 "CPU 가 볼 값" 을 돌려준다. write 훅은 "새 저장값" 을 돌려준다.
  - 표에 없는 주소는 **크래시 없이** `unexpected` 를 세고 첫 주소를 기억, 로그에는 소문자 `'r'/'w'`.
- `mock_peek/mock_poke` (**백도어**, 하드웨어·테스트 쪽 접근): 훅·로그·tick 없음. 모델은 이걸로 상태 비트를 바꾼다.

### Part B — 접근 타입과 고장 케이스

- `mock_access_t { ro, wo, w1c, w0c, rc }` — 비트마다 타입 하나(마스크끼리 안 겹침, 나머지는 rw). 공용 훅 `mock_access_wr/rd` 를 구현한다.
- 등록 오류: 비정렬 주소, 중복 주소, 표 가득 참 → -1. 로그가 256 을 넘으면 버리고 `log_dropped` 를 센다 (`mock_clean()` 이 false).
- 2000 개 무작위 (비트 타입 분할, old, val) 에 대해 느린 비트 단위 기준 구현과 비교한다.

### Part C — USART2 모델과 드라이버

- 모델: SR(리셋 0x00C0 = TXE|TC), DR, BRR, CR1. DR 쓰기 → TXE·TC 를 0 으로, `tx_ticks` tick 뒤 바이트를 **TX 캡처 버퍼**에 넣고 TXE·TC=1. RX 주입 큐 → RDR 이 비어 있으면 다음 tick 에 RDR 적재 + RXNE=1. DR 읽기 → RXNE=0. UE·TE / UE·RE 가 꺼져 있으면 송신·수신 바이트는 버려진다.
- 드라이버: `uart_init`, `uart_putc` (TXE 대기), `uart_getc` (RXNE 대기), 둘 다 폴링 상한 100 회 → -1.
- 테스트: 정확한 접근 순서, TX 캡처 "Hi" / "Hello\n", RX "OK", 타임아웃, 그리고 버그 드라이버 3종(TXE 안 기다림, init 전 송신, 베이스 주소 오타)을 mock 이 잡는지.
- 같은 `solution.c` 를 `-DTARGET_BUILD` 로 Cortex-M4 오브젝트로 만들어 `ldr/str` 를 본다 (`target_build.sh`).

## 2. Problem (English)

> **Host-test a register-level UART driver with a hookable mock bus.**
>
> Driver code accesses hardware only through `REG_READ(addr)` / `REG_WRITE(addr, val)`. In the target build they expand to `volatile uint32_t` loads/stores; in the `UNIT_TEST` build they call `uint32_t mock_read32(uint32_t)` / `void mock_write32(uint32_t, uint32_t)`.
>
> 1. Implement the mock core with static storage only: `int mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read rd, mock_on_write wr, void *ctx)` (reject unaligned / duplicate / table full with -1), `mock_read32`, `mock_write32`, back-door `mock_peek` / `mock_poke`, an access log of `{op, addr, value}` with fixed capacity, and `mock_tick(n)` calling registered tickers (one bus access = one tick). An access to an unmapped address must not crash: count it, remember the first bad address, log it.
> 2. Implement generic access-type hooks for per-bit types read-only, write-only (reads as 0), write-1-to-clear, write-0-to-clear and clear-on-read.
> 3. Model USART2 (SR 0x00 reset 0x00C0, DR 0x04, BRR 0x08, CR1 0x0C; TXE bit 7, TC bit 6, RXNE bit 5; UE bit 13, TE bit 3, RE bit 2): a DR write clears TXE/TC and the byte is captured `tx_ticks` ticks later; injected RX bytes set RXNE; a DR read clears RXNE. Write `uart_putc` / `uart_getc` with a bounded poll count (100).
>
> Example (`tx_ticks = 3`): `uart_init(USART2, 0x16D); uart_putc('H'); uart_putc('i');` must produce exactly
> `W BRR=0x16D, W CR1=0x200C, R SR=0xC0, W DR=0x48, R SR=0x00, R SR=0x00, R SR=0xC0, W DR=0x69`.
>
> Follow-ups: why `volatile`? What can a mock never catch? Why must the debugger's memory view not show DR? Macro vs link-time vs function-pointer seam — what does each cost on an M4?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 이 쭉 나오고 "16/53 passed" (exit 1) — TODO 10곳을 채워 53/53
./starter -v      # 정답 코드로 돌리면 init+putc, getc 의 접근 로그를 찍는다 (5. 풀이의 표)
bash target_build.sh   # Part C: 같은 소스를 thumbv7em 오브젝트로 → objdump (레슨 디렉터리 어디서 불러도 됨)
```

TODO 위치: `uart_putc`, `uart_getc` (드라이버) · `mock_add_reg` 의 검사 2줄 · `mock_read32`, `mock_write32` (Part A) · `mock_access_wr`, `mock_access_rd` (Part B) · `uart_dr_wr`, `uart_dr_rd`, `uart_tick` (Part C 모델). 스텁은 모두 즉시 돌아온다 — 폴링 루프도 상한이 있어서 시작 코드가 멈추는 일은 없다.

## 4. 힌트

<details>
<summary>힌트 1 — mock_read32 는 네 줄짜리 파이프라인</summary>

`g = mock_find(addr)` → 없으면 `mock_bad(addr)` + `'r'` 로그 + 0. 있으면 `v = g->rd ? g->rd(addr, g->val, g->ctx) : g->val`, `'R'` 로그에 **v** (CPU 가 본 값)를 남긴다. 어느 쪽이든 마지막에 `mock_tick(1)`. 훅이 `mock_poke` 로 저장값을 바꿀 수 있으니, 훅을 부른 뒤 `g->val` 을 다시 덮어쓰지 않는다.

</details>

<details>
<summary>힌트 2 — 훅 안에서는 절대 REG_READ/REG_WRITE 를 쓰지 않는다</summary>

훅은 "하드웨어" 다. 하드웨어가 자기 상태 비트를 바꿀 때는 버스를 거치지 않는다. 훅 안에서 `mock_write32` 를 부르면 로그에 가짜 접근이 끼고, tick 이 한 번 더 흐르고, 같은 레지스터면 재귀가 된다. 상태는 `mock_peek/mock_poke` 로.

</details>

<details>
<summary>힌트 3 — 접근 타입 쓰기는 네 조각의 OR</summary>

`rw = ~(ro | rc | w1c | w0c)` (wo 비트도 쓰기는 rw 와 같다). 새 값 = `(old & (ro|rc))` | `(val & rw)` | `(old & w1c & ~val)` | `(old & w0c & val)`. 마지막 두 조각을 말로: W1C 는 "1 을 쓴 자리만 지워지고 나머지는 old", rc_w0 은 "0 을 쓴 자리만 지워지고 나머지는 old".

</details>

<details>
<summary>힌트 4 — UART 모델의 시간</summary>

DR 쓰기 훅은 `tx_left = tx_ticks` 만 걸고, 실제 "보냄" 은 ticker 가 한다: `if (tx_left && --tx_left == 0) { 캡처; SR |= TXE|TC; }`. 쓰기 접근 자신도 끝에서 tick 을 하나 먹으므로, `tx_ticks = 3` 이면 그 뒤 SR 읽기 **2번**이 TXE=0 을 본다.

</details>

<details>
<summary>힌트 5 — 드라이버 폴링은 for 로</summary>

`for (n = 0; n < UART_TIMEOUT_POLLS; n++) if (REG_READ(SR) & TXE) { REG_WRITE(DR, c); return 0; } return -1;` — `while (!(SR & TXE));` 는 mock 이 TXE 를 영영 안 올리면(버그·고장) 테스트가 멈춘다. 타깃에서도 같다.

</details>

## 5. 풀이

### Part A — 표 · 훅 · 로그 · tick

| 단계 | `mock_read32(addr)` | `mock_write32(addr, val)` |
|---|---|---|
| ① 찾기 | `mock_find` (선형 탐색, 레지스터 16개면 충분) | 같음 |
| ② 없는 주소 | `unexpected++`, 첫 주소 기록, 로그 `'r'`, **0 반환** | `unexpected++`, 로그 `'w'`, 무시 |
| ③ 훅 | `v = rd ? rd(addr, val, ctx) : val` | `val' = wr ? wr(addr, old, val, ctx) : val` 을 저장 |
| ④ 로그 | `{'R', addr, v}` — CPU 가 **본** 값 | `{'W', addr, val}` — CPU 가 **쓴** 값 |
| ⑤ 시간 | `mock_tick(1)` → 모든 ticker 호출 | 같음 |

설계 선택과 이유:

- **로그는 CPU 의 관점**: 테스트가 확인하고 싶은 것은 "드라이버가 무엇을 읽고 무엇을 썼나" 다. 저장값(W1C 로 지워진 결과)을 로그하면 드라이버 버그(`SR |= FLAG` 처럼 엉뚱한 값을 쓴 것)가 가려진다.
- **없는 주소는 세고 계속**: `abort()` 하면 그 테스트 뒤의 결과를 다 잃고, 어디서 처음 틀렸는지도 모른다. 대신 `first_bad` 와 로그 위치가 남는다. 모든 테스트 끝에 `CHECK(mock_clean())` 를 두면 "몰래 지도 밖을 건드린" 드라이버가 걸린다. 진짜 Cortex-M 에서는 매핑 안 된 주소 접근이 BusFault (꺼져 있으면 HardFault 로 승격)다 — Q7.
- **접근 1회 = 1 tick**: 드라이버의 폴링 루프가 돌수록 모델 시간이 흐르므로 별도 스레드나 실제 시간이 필요 없고, 결과가 완전히 결정적(deterministic)이다. 테스트가 "CPU 가 다른 일을 하는 시간" 을 넣고 싶으면 `mock_tick(n)`.
- **프론트도어 / 백도어**: UVM 레지스터 모델과 같은 말이다. 프론트도어 = 버스를 타는 진짜 접근(부작용 발생), 백도어 = 시뮬레이터가 값을 직접 보거나 바꿈(부작용 없음). 테스트의 `CHECK` 는 백도어로 봐야 관찰이 동작을 바꾸지 않는다 — DR 을 프론트도어로 읽으면 그 자체로 RXNE 가 꺼진다.

Part A 테스트 추적 (TIM3_ARR = 하위 16비트만 구현, TIM3_CNT = 읽을 때마다 +1 하는 예제 훅):

| # | CPU 접근 | 훅이 한 일 | 로그 | 저장값 뒤 |
|---|---|---|---|---|
| 0 | `REG_READ(ARR)` | 없음 (rd 훅 없음) | `R ARR 0xFFFF` (리셋값) | 0xFFFF |
| 1 | `REG_WRITE(ARR, 0x12345678)` | `impl_mask_wr` → `val & 0xFFFF` | `W ARR 0x12345678` | 0x5678 |
| 2 | `REG_READ(ARR)` | — | `R ARR 0x5678` | 0x5678 |
| 3–5 | `REG_READ(CNT)` ×3 | `cnt_rd`: poke(stored+1), stored 반환 | `R CNT 0`, `1`, `2` | 3 |

`M.now == 6` (접근 6번), `mock_peek(CNT) == 3` (백도어는 로그·tick 없음). 그 뒤 `REG_READ(0x40000430)`, `REG_WRITE(0x50000000, 1)` → `unexpected == 2`, `first_bad == 0x40000430`, 로그 6·7번이 `'r'`, `'w'`.

### Part B — 접근 타입

레퍼런스 매뉴얼은 비트마다 접근 타입을 적는다 (RM0090 의 약어 기준, 확인 권장):

| 타입 | 쓰기 동작 | 읽기 동작 | 대표 예 (대표값 — RM 확인 권장) |
|---|---|---|---|
| rw | 쓴 값 | 저장값 | USART_CR1, USART_BRR |
| r | 무시 | 저장값 (HW 가 바꿈) | GPIOx_IDR, USART_SR.TXE |
| w | 쓴 값(효과) | **0** | GPIOx_BSRR |
| rc_w1 (W1C) | 1 → 클리어, 0 → 무영향 | 저장값 | F4 EXTI_PR, 신형 STM32 의 xxx_ICR 계열 |
| rc_w0 | 0 → 클리어, 1 → 무영향 | 저장값 | F4 TIMx_SR.UIF, USART_SR.RXNE/TC |
| rc_r | 무시 | 저장값, 읽은 **뒤** 클리어 | 칩마다 다름 (RM 의 표기로 확인) |
| rs | 1 → 세트, 0 → 무영향 | 저장값 | 이 mock 에는 없음 (연습 거리) |

W1C 와 rc_w0 이 왜 있나: "읽고-바꾸고-쓰기(RMW)" 없이 **원하는 플래그 하나만** 지우기 위해서다. 상태 레지스터는 하드웨어가 언제든 다른 비트를 세트한다. W1C 면 `REG_WRITE(SR, FLAG_A)` 한 번에 A 만 지워지고 다른 비트에는 0 이 쓰여 무영향. 여기에 `SR |= FLAG_A` 를 쓰면 읽은 값의 1 들이 전부 다시 쓰여 **켜져 있던 플래그를 다 지운다** — 그 함정과 ISR 경쟁은 BM-B04 의 몫이다.

테스트 레지스터 (`ro = 0xFFFFFE80`, `wo = bit8`, `w1c = bits 3..0`, `w0c = bits 5..4`, `rc = bit6`, `ro` = bit7 과 bit9 이상) 에 저장값 0xFF 를 넣고 0x05 를 썼을 때, 비트마다:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">mock_access_wr: old = 0x0FF, CPU 가 0x005 를 씀 → 새 저장값 0x0CA</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="56">비트</text><text x="10" y="90">타입</text><text x="10" y="124">old</text>
    <text x="10" y="158">val (CPU)</text><text x="10" y="192">new</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="130" y="72" width="504" height="136"/>
  </g>
  <g class="f-warn">
    <rect x="298" y="174" width="56" height="34"/><rect x="354" y="174" width="56" height="34"/>
    <rect x="466" y="174" width="56" height="34"/><rect x="578" y="174" width="56" height="34"/>
  </g>
  <g stroke="currentColor">
    <path d="M186 72 V208"/><path d="M242 72 V208"/><path d="M298 72 V208"/><path d="M354 72 V208"/>
    <path d="M410 72 V208"/><path d="M466 72 V208"/><path d="M522 72 V208"/><path d="M578 72 V208"/>
    <path d="M130 106 H634"/><path d="M130 140 H634"/><path d="M130 174 H634"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="158" y="56">8</text><text x="214" y="56">7</text><text x="270" y="56">6</text><text x="326" y="56">5</text>
    <text x="382" y="56">4</text><text x="438" y="56">3</text><text x="494" y="56">2</text><text x="550" y="56">1</text><text x="606" y="56">0</text>
    <text x="158" y="94">w</text><text x="214" y="94">r</text><text x="270" y="94">rc_r</text><text x="326" y="94">rc_w0</text>
    <text x="382" y="94">rc_w0</text><text x="438" y="94">W1C</text><text x="494" y="94">W1C</text><text x="550" y="94">W1C</text><text x="606" y="94">W1C</text>
    <text x="158" y="128">0</text><text x="214" y="128">1</text><text x="270" y="128">1</text><text x="326" y="128">1</text>
    <text x="382" y="128">1</text><text x="438" y="128">1</text><text x="494" y="128">1</text><text x="550" y="128">1</text><text x="606" y="128">1</text>
    <text x="158" y="162">0</text><text x="214" y="162">0</text><text x="270" y="162">0</text><text x="326" y="162">0</text>
    <text x="382" y="162">0</text><text x="438" y="162">0</text><text x="494" y="162">1</text><text x="550" y="162">0</text><text x="606" y="162">1</text>
    <text x="158" y="196">0</text><text x="214" y="196">1</text><text x="270" y="196">1</text><text x="326" y="196">0</text>
    <text x="382" y="196">0</text><text x="438" y="196">1</text><text x="494" y="196">0</text><text x="550" y="196">1</text><text x="606" y="196">0</text>
  </g>
  <text x="10" y="234" fill="currentColor" font-size="12">색칠 = 바뀐 비트 (5·4·2·0). rc_w0 은 0 을 쓴 5·4 가 지워졌다.</text>
  <text x="10" y="253" fill="currentColor" font-size="12">W1C 는 1 을 쓴 2·0 만 지워지고, 0 을 쓴 3·1 은 그대로 1.</text>
  <text x="10" y="272" fill="currentColor" font-size="12">r · rc_r 비트(7·6)는 CPU 쓰기를 무시. w 비트(8)는 쓴 값 0 이 그대로 들어간다.</text>
  <text x="10" y="291" fill="currentColor" font-size="12">읽기에서는 w 비트가 0 으로 보이고, rc_r 비트는 읽은 뒤 지워진다.</text>
</svg>
```

두 번째 테스트는 rc_w0 의 정석 `REG_WRITE(SR, ~FLAG)` 를 같은 레지스터에 쓴다: 저장값 0xFF 에 `~0x10` → **0x1E0**. rc_w0 비트 4 만 지워졌지만, 같은 값이 W1C 비트 3..0 에는 전부 1 이라 **W1C 플래그 넷이 같이 지워졌다**. "rc_w0 레지스터에서 쓰던 `~mask` 습관을 W1C 레지스터에 그대로 쓰면 모든 플래그를 날린다" 는 실제 버그를 한 줄로 재현한 것이다 (bit 8 은 wo/rw 라 쓴 값 1 이 들어갔다).

읽기 쪽: `mock_poke(SR, 0x1FF)` → `REG_READ` 는 0xFF (wo 비트 8 은 0 으로 보임), 저장값은 0x1BF (rc_r 비트 6 이 읽은 뒤 지워짐), 한 번 더 읽으면 0xBF.

property test: 비트마다 6가지(ro/wo/w1c/w0c/rc/rw) 중 하나를 xorshift32(시드 0x2026B02)로 고른 무작위 분할 2000 개 × 무작위 old/val. 기준 구현 `ref_wr` 은 비트 하나씩 if-else 로 규칙을 적용하는 느리지만 뻔한 코드다. 마스크 공식과 2000 번 모두 같았다(`bad_w == 0 && bad_r == 0`). 이 레지스터 모델은 CMSIS-SVD 의 `access` / `modifiedWriteValues`(`oneToClear`, `zeroToClear` …) / `readAction`(`clear`) 와 같은 정보다 — SVD 에서 자동으로 mock 표를 만들 수 있다는 뜻이고, 헤더 생성은 SIV-B01 에서 했다.

등록 오류와 넘침: `0x40020002` (비정렬) · 같은 주소 두 번 · 17번째 등록 → -1. 같은 주소를 300번 읽으면 로그 256 개 + `log_dropped == 44`, `mock_clean()` 은 false — **넘친 로그로 "통과" 판정을 하지 않게** 하는 장치다.

### Part C — USART2 모델

레지스터 정의 (RM0090 기준 대표값 — RM 확인 권장):

| 레지스터 | 오프셋 | 리셋값 (mock) | mock 동작 |
|---|---|---|---|
| SR | 0x00 | 0x0000_00C0 (TXE=1, TC=1) | RXNE(5)·TC(6) = rc_w0, 나머지 r. 모델이 poke 로 갱신 |
| DR | 0x04 | 0 | 쓰기 = TDR 적재 (TXE·TC → 0), 읽기 = RDR (부작용: RXNE → 0) |
| BRR | 0x08 | 0 | rw (값만 저장) |
| CR1 | 0x0C | 0 | rw. UE(13)·TE(3)·RE(2) 를 모델이 peek |

DR 은 "같은 주소, 다른 레지스터 두 개"(쓰기는 TDR, 읽기는 RDR)다. 그래서 DR 의 저장값은 의미가 없고, 읽기 훅이 `u->rdr` 를 돌려준다. 또 DR 읽기는 **다른 레지스터(SR)** 의 비트를 바꾼다 — 접근 타입 마스크로는 표현이 안 되는, 교차 레지스터 부작용이다. 이런 건 전용 훅이 맡는다.

`uart_init(B, 0x16D)` + `putc('H')` + `putc('i')`, `tx_ticks = 3` 의 실제 로그 (`./solution -v`):

```text
-- init + putc('H') + putc('i') (8 entries, t=10)
   0  W 0x40004408 = 0x0000016D
   1  W 0x4000440C = 0x0000200C
   2  R 0x40004400 = 0x000000C0
   3  W 0x40004404 = 0x00000048
   4  R 0x40004400 = 0x00000000
   5  R 0x40004400 = 0x00000000
   6  R 0x40004400 = 0x000000C0
   7  W 0x40004404 = 0x00000069
```

```svg
<svg viewBox="0 0 740 305" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">접근 #0–#7 과 SR.TXE (tx_ticks = 3, 접근 1회 = 1 tick, tick 은 접근 끝에)</text>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="110" y="48">#0</text><text x="180" y="48">#1</text><text x="250" y="48">#2</text><text x="320" y="48">#3</text>
    <text x="390" y="48">#4</text><text x="460" y="48">#5</text><text x="530" y="48">#6</text><text x="600" y="48">#7</text>
    <text x="680" y="48">tick×2</text>
    <text x="110" y="68">W BRR</text><text x="180" y="68">W CR1</text><text x="250" y="68">R SR</text><text x="320" y="68">W DR</text>
    <text x="390" y="68">R SR</text><text x="460" y="68">R SR</text><text x="530" y="68">R SR</text><text x="600" y="68">W DR</text>
    <text x="110" y="84">16D</text><text x="180" y="84">200C</text><text x="250" y="84">=C0</text><text x="320" y="84">'H'</text>
    <text x="390" y="84">=00</text><text x="460" y="84">=00</text><text x="530" y="84">=C0</text><text x="600" y="84">'i'</text>
  </g>
  <text x="10" y="126" fill="currentColor">TXE</text>
  <text x="40" y="116" fill="currentColor" font-size="12">1</text><text x="40" y="150" fill="currentColor" font-size="12">0</text>
  <path d="M75 112 H320 V146 H495 V112 H600 V146 H700 V112 H730" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="320" y="112" width="175" height="34" class="f-warn" opacity="0.5"/>
  <rect x="600" y="112" width="100" height="34" class="f-warn" opacity="0.5"/>
  <text x="10" y="186" fill="currentColor">tx_left</text>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="110" y="186">0</text><text x="180" y="186">0</text><text x="250" y="186">0</text><text x="320" y="186">3→2</text>
    <text x="390" y="186">1</text><text x="460" y="186">0</text><text x="530" y="186">0</text><text x="600" y="186">3→2</text>
    <text x="680" y="186">1, 0</text>
  </g>
  <text x="10" y="216" fill="currentColor">TX 캡처</text>
  <rect x="440" y="200" width="70" height="24" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="475" y="217" fill="currentColor" text-anchor="middle" font-family="monospace">'H'</text>
  <rect x="660" y="200" width="70" height="24" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="695" y="217" fill="currentColor" text-anchor="middle" font-family="monospace">'i'</text>
  <text x="10" y="252" fill="currentColor">#3: DR 쓰기 훅이 tx_left = 3, 그 접근의 tick 이 2 로 → SR 읽기 #4·#5 가 TXE=0.</text>
  <text x="10" y="272" fill="currentColor">#5 의 tick 에서 0 → 'H' 캡처, TXE·TC=1 → #6 이 0xC0, #7 에서 'i'.</text>
  <text x="10" y="292" fill="currentColor">'i' 는 테스트의 mock_tick(2) 뒤에 캡처된다.</text>
</svg>
```

검증 포인트: #2 에서 **첫 SR 읽기가 0xC0** 인 것은 리셋값 덕이다. 리셋값을 0 으로 등록한 mock 이면 드라이버의 첫 `putc` 가 TXE 를 영원히 기다린다 — 리셋값은 mock 의 일부다. CR1 에 쓴 0x200C = UE(0x2000)|TE(0x8)|RE(0x4). BRR 0x16D 는 PCLK1 42 MHz, 115200 baud 의 USARTDIV 22.8125 (mantissa 22, fraction 13) — 계산과 반올림 규칙은 BM-B12.

RX: `inject_rx("OK")` 뒤 `getc` ×2 의 실제 로그:

| # | 접근 | 값 | 그 접근의 tick 이 한 일 |
|---|---|---|---|
| 0 | R SR | 0xC0 | 큐에서 'O' → RDR, RXNE=1 (주입 직후라 아직 0 이었다) |
| 1 | R SR | 0xE0 | — |
| 2 | R DR | 0x4F 'O' | 훅이 RXNE=0 → tick 이 'K' → RDR, RXNE=1 |
| 3 | R SR | 0xE0 | — |
| 4 | R DR | 0x4B 'K' | 훅이 RXNE=0, 큐 빔 |

결함 주입 결과 (모두 `solution.c` 의 테스트):

| 시나리오 | 드라이버가 본 것 | mock 이 잡은 것 |
|---|---|---|
| TXE 안 기다리고 DR 에 "Hello" 연속 쓰기 | 다 성공 | `tx_overwrite == 4`, 캡처는 `"o"` 한 글자 |
| `uart_init` 전에 `putc('X')` | 0 (성공) — 리셋값 TXE=1 이라 | `tx_lost == 1`, 캡처 0개. 보드라면 "스코프에 아무것도 없음" |
| 수신기 꺼진 채 바이트 도착 | `getc` = -1 (100번 폴링) | `rx_lost == 1` |
| 베이스 주소 오타 0x4004_4400 | `putc` = -1 | `unexpected == 100`, `first_bad == 0x40044400`, 로그 2번 `'r'` |
| 받을 바이트 없음 | `getc` = -1 | 로그 정확히 100개 (SR 읽기만) |

`uart_write("Hello\n")` 는 SR 을 **1 + 5·3 = 16번** 읽고 DR 을 6번 쓴다: 첫 바이트는 TXE 가 이미 1, 그 뒤 바이트마다 busy 2번 + ready 1번 = `tx_ticks` 번. 일반식 `1 + (n−1)·tx_ticks` 는 연습 4 에서 tx_ticks 1–8 × n 1–10 전부 확인한다.

이 모델이 **하지 않는** 것 (다른 레슨 몫): TDR → 시프트 레지스터 2단(TXE 와 TC 의 시점 차이, "TC 를 기다려야 마지막 바이트가 나간다") = BM-B12, ORE/FE/NF 와 "SR 읽고 DR 읽기" 클리어 순서 = BM-B12, baud 오차 = COMM-B03, 클럭 게이팅 = BM-B03, 인터럽트 = BM-M04. 실제 RM 에서 TC 는 "SR 읽기 후 DR 쓰기" 순서로 지워진다 — 이 모델은 DR 쓰기만으로 지운다(드라이버가 항상 SR 을 먼저 읽으므로 결과는 같다).

### Part C — 같은 소스를 타깃으로: `target_build.sh`

<!-- code: target_build.sh -->

`solution.c` 는 첫 줄에서 `#ifndef TARGET_BUILD` → `#define UNIT_TEST 1` 이고, mock·모델·`main` 은 전부 `#ifdef UNIT_TEST` 안에 있다. 그래서 `-DTARGET_BUILD` 로 같은 파일을 컴파일하면 드라이버만 남는다. 실제 출력 (Apple clang 21, `--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os`, 일부):

```text
00000000 <uart_init>:
       0: 6081         	str	r1, [r0, #0x8]
       2: f242 010c    	movw	r1, #0x200c
       6: 60c1         	str	r1, [r0, #0xc]
       8: 4770         	bx	lr

0000000a <uart_putc>:
       a: 2264         	movs	r2, #0x64
       c: 6803         	ldr	r3, [r0]
       e: 061b         	lsls	r3, r3, #0x18
      10: d402         	bmi	0x18 <uart_putc+0xe>    @ imm = #0x4
      12: 3a01         	subs	r2, #0x1
      14: d001         	beq	0x1a <uart_putc+0x10>   @ imm = #0x2
      16: e7f9         	b	0xc <uart_putc+0x2>     @ imm = #-0xe
      18: 6041         	str	r1, [r0, #0x4]
      1a: 2000         	movs	r0, #0x0
      1c: 2b00         	cmp	r3, #0x0
      1e: bf58         	it	pl
      20: f04f 30ff    	movpl.w	r0, #0xffffffff
      24: 4770         	bx	lr

== 2. 심볼: mock/테스트 코드가 오브젝트에 남았나?
00000026 T uart_getc
00000000 T uart_init
0000000a T uart_putc
00000044 T uart_write
   text	   data	    bss	    dec	    hex	filename
    154	      0	      0	    154	     9a	/tmp/BM-B02_work/drv_m4.o
```

읽는 법: `r0` = base(0x4000_4400), `str r1, [r0, #0x8]` = BRR, `str ... [r0, #0xc]` = CR1 ← 0x200C. 폴링은 `ldr r3, [r0]` (SR) → `lsls r3, r3, #0x18` 로 **bit 7(TXE)을 부호 비트로 밀어** `bmi` 로 분기 — 마스크 상수 없이 한 비트를 시험하는 Thumb 관용구. `movs r2, #0x64` 가 폴링 상한 100. TXE 가 서면 `str r1, [r0, #0x4]` = DR. `uart_getc` 는 같은 모양에 `lsls #0x1a` (bit 5 = RXNE) 와 `ldr r0, [r0, #0x4]` / `strb`. 심볼은 `uart_*` 넷뿐, 154 바이트 — **mock 프레임워크는 타깃 이미지에 1 바이트도 남지 않는다**. 같은 파일의 호스트 빌드에는 `_mock_read32`, `_mock_write32`, `_uart_dr_rd` … 가 들어 있다 (스크립트 3번).

(`-O2` 로 하면 clang 이 폴링 루프를 펼쳐(unroll) `uart_putc` 가 0x0c–0xcd, 194 바이트가 된다 — 같은 동작, 7배 크기. 플래시가 빠듯한 펌웨어가 `-Os` 를 많이 쓰는 이유 중 하나.)

**`volatile` 이 빠지면** (`demo_novolatile.c`, 같은 스크립트 4번, 실제 출력):

<!-- code: demo_novolatile.c -->

```text
00000012 <putc_plain>:
      12: f244 4100    	movw	r1, #0x4400
      16: f2c4 0100    	movt	r1, #0x4000
      1a: 780a         	ldrb	r2, [r1]
      1c: 0612         	lsls	r2, r2, #0x18
      1e: bf44         	itt	mi
      20: 6048         	strmi	r0, [r1, #0x4]
      22: 4770         	bxmi	lr
      24: e7fe         	b	0x24 <putc_plain+0x12>  @ imm = #-0x4

00000026 <two_writes_plain>:
      26: f244 4004    	movw	r0, #0x4404
      2a: f2c4 0000    	movt	r0, #0x4000
      2e: 2142         	movs	r1, #0x42
      30: 6001         	str	r1, [r0]
      32: 4770         	bx	lr
```

세 가지가 한 번에 보인다. ① SR 을 **딱 한 번** 읽고, TXE 가 0 이면 `b 0x24` — 자기 자신으로 점프하는 무한 루프. ② 32비트 레지스터를 `ldrb` (바이트 읽기)로 읽는다 — TXE 는 하위 바이트에 있으니 컴파일러 입장에선 합법. 32비트 접근만 허용하는 주변장치라면 버스 오류나 엉뚱한 값. ③ DR 에 'A', 'B' 를 차례로 쓴 코드가 `str` 하나('B')로 합쳐졌다 — 'A' 는 영영 안 나간다. `volatile` 판(`putc_volatile`, `two_writes_volatile`)은 매번 `ldr` 하고 `str` 를 두 번 한다. mock 빌드에서는 함수 호출이라 이 버그가 **안 보인다** — 이것이 타깃 오브젝트를 직접 보는 이유다.

**이음매 세 가지 비교** (C++ 버전의 같은 비교는 CPP-A08):

| 이음매 | 어떻게 바꾸나 | 타깃 비용 (이 레슨 실측) | 장점 | 단점 |
|---|---|---|---|---|
| 매크로 (전처리 이음매) | `#ifdef UNIT_TEST` 로 `REG_*` 정의 교체 | 0 — `uart_putc` 28 B, 직접 `ldr/str` | 타깃 코드가 손으로 쓴 MMIO 와 같음 | 빌드가 둘 (테스트 빌드는 타깃과 다른 바이너리) |
| 링크 타임 | `mock_read32` 를 정의한 `.o` 를 테스트에서만 링크, 타깃은 `hw_read32` (인라인 불가 시 `bl`) | 함수 호출 1회/접근 (LTO 면 사라질 수 있음) | 드라이버 `.o` 를 다시 컴파일 안 함 | 링커가 엉뚱한 구현을 골라도 조용함 (CPP-A08) |
| 함수 포인터 (`bus_ops_t`) | 드라이버가 `b->rd(addr)` 호출 | `uart_putc_ops` 50 B, 접근마다 `blx` 간접 호출 + 레지스터 저장 6개 | 런타임에 바꿈 — 같은 바이너리로 스파이·고장 주입 | 느림, 인라인 불가, 함수 포인터 표가 RAM/Flash 에 |

함수 포인터 판의 실제 출력 (`demo_seam_fp.c`, 스크립트 5번):

<!-- code: demo_seam_fp.c -->

```text
00000008 <uart_putc_ops>:
       8: e92d 41f0    	push.w	{r4, r5, r6, r7, r8, lr}
       ...
      14: 6831         	ldr	r1, [r6]
      16: 4628         	mov	r0, r5
      18: 4788         	blx	r1
      1a: 0604         	lsls	r4, r0, #0x18
       ...
      24: 6872         	ldr	r2, [r6, #0x4]
      26: 1d28         	adds	r0, r5, #0x4
      28: 4641         	mov	r1, r8
      2a: 4790         	blx	r2
       ...
      36: e8bd 81f0    	pop.w	{r4, r5, r6, r7, r8, pc}
```

폴링 한 번이 `ldr` 하나에서 "표에서 함수 주소 읽기 + `blx` + 그 함수 안의 `ldr` + 복귀" 로 늘어난다. 폴링 루프에서는 견딜 만하지만, 비트뱅잉이나 ISR 의 레지스터 접근에 넣으면 비싸다. 이 레슨이 기본으로 **매크로 이음매**를 쓰는 이유다. 함수 포인터 이음매는 연습 3 에서 "로그가 매크로 판과 똑같다" 로 확인한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 53/53 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 53/53, 경고 없음
bash target_build.sh                                                              # 타깃 오브젝트 · nm · volatile 데모 · 이음매 비교
```

## 7. 자주 하는 실수

1. **로그에 저장값을 남긴다.** W1C 레지스터에 `0xFF` 를 쓴 버그 드라이버라도 저장값은 0 이라 로그가 "0 을 썼다" 로 보인다. → 쓰기 로그는 CPU 가 버스에 낸 값 `val`, 읽기 로그는 CPU 가 받은 값 `v`.
2. **훅 안에서 프론트도어를 쓴다.** `uart_dr_rd` 에서 `REG_WRITE(SR, ...)` 를 부르면 로그에 드라이버가 안 한 쓰기가 끼고 tick 이 한 번 더 흘러 접근 순서 테스트가 깨진다. SR 에 rc_w0 훅이 있으니 의도와 다른 비트 결과까지 나온다. → 하드웨어 쪽 상태 변경은 `mock_poke`.
3. **rc_r 을 "읽기 전에" 지운다.** `mock_poke(addr, stored & ~rc); return mock_peek(addr);` 처럼 쓰면 CPU 는 항상 0 을 본다. → 읽은 값을 먼저 정하고, 지우는 건 그 **뒤** (`return stored & ~wo` 는 poke 전 값 기준).
4. **리셋값을 0 으로 대충 둔다.** USART SR 이 0 으로 시작하면 첫 `putc` 가 TXE 를 기다리다 타임아웃 — 드라이버가 아니라 mock 이 틀렸다. → mock 표의 리셋값은 RM 의 "Reset value" 줄을 그대로 옮긴다 (그리고 RM 확인 권장 표시).
5. **드라이버 폴링에 상한이 없다.** `while (!(REG_READ(SR) & TXE));` 는 stuck-at 고장(연습 2)이나 클럭 미공급(BM-B03)에서 테스트도 보드도 멈춘다. → `for (n < LIMIT)` + 에러 반환. 진짜 시간 기반 타임아웃은 BM-B13.
6. **mock 을 너무 똑똑하게 만든다.** baud 타이밍, 프레이밍, 노이즈까지 넣으면 mock 자체에 버그가 생기고 드라이버 테스트가 mock 디버깅이 된다. → 드라이버가 **관찰할 수 있는** 동작(상태 비트, 데이터, 순서)만 모델링하고 나머지는 해당 레슨으로.
7. **`mock_clean()` 확인을 빼먹는다.** 베이스 주소 오타 드라이버도 "-1 을 돌려줬으니 타임아웃 테스트 통과" 로 보일 수 있다. → 테스트마다 끝에서 `CHECK(mock_clean())` (또는 기대한 `unexpected` 수를 명시).

## 8. 면접 Q&A

<details>
<summary>Q1. 레지스터를 직접 만지는 드라이버를 어떻게 유닛 테스트하나요?</summary>

드라이버가 레지스터를 매크로나 얇은 함수 하나로만 접근하게 만들고(이음매), 테스트 빌드에서 그 접근을 mock 버스로 보낸다. mock 은 주소별 레지스터 표 + 훅으로 "쓰면 비트가 바뀌고, 시간이 지나면 플래그가 서는" 동작을 흉내 내고, 모든 접근을 순서대로 기록한다. 테스트는 ① 결과(TX 캡처 "Hi"), ② 접근 순서(정확한 로그), ③ 금지 사항(지도 밖 접근 0, 덮어쓰기 0)을 본다. 이 레슨의 결과: 53개 검사, 결함 4종(TXE 미대기, init 전 송신, 수신기 꺼짐, 주소 오타)을 크래시 없이 잡음. 꼬리: "CMock 은?" → 함수 단위 기대(expect) 기반의 엄격한 mock 생성기다. 레지스터 수준에서는 이 레슨 같은 **행동 모델(fake)** 이 폴링 횟수 변화에 덜 깨진다 — 기대 순서를 하나하나 적으면 폴링 한 번 늘어도 테스트가 깨진다.

</details>

<details>
<summary>Q2. 왜 volatile 이 필요한가요? volatile 이면 충분한가요?</summary>

컴파일러는 "이 메모리를 바꾸는 건 이 코드뿐" 이라고 가정하고 읽기를 루프 밖으로 빼고, 같은 주소 쓰기를 합치고, 접근 폭을 줄인다. 실측(clang, Cortex-M4 `-Os`): volatile 없는 폴링은 SR 을 한 번 `ldrb` 로 읽고 `b .` 무한 루프, DR 에 'A','B' 쓰기는 'B' 하나로 합쳐졌다. volatile 은 "이 접근은 소스에 적힌 횟수·순서·폭대로 하라" 를 보장한다(volatile 접근끼리의 순서). 충분하지 않은 것: ① 원자성 — `REG |= x` 는 여전히 ldr/orr/str 세 명령이고 ISR 이 끼어들 수 있다 (BM-B04, C-M14). ② 비-volatile 메모리와의 순서 (DMA 버퍼를 채운 뒤 DMA 시작 레지스터를 쓰는 경우 — 컴파일러 배리어 / `__DMB()`). ③ 버스 쓰기가 실제로 끝났는지 — Cortex-M 의 쓰기 버퍼 때문에 "주변장치에 썼다" 와 "효과가 났다" 사이에 틈이 있다; 클럭 enable 직후 읽기(BM-B03), 인터럽트 플래그 클리어 직후 ISR 복귀 같은 곳에서 `__DSB()` 나 read-back.

</details>

<details>
<summary>Q3. W1C 와 rc_w0 은 왜 존재하고, 각각 어떻게 지워야 하나요?</summary>

상태 레지스터는 하드웨어가 아무 때나 다른 비트를 세트한다. RMW (`SR = SR & ~A`) 로 지우면, 읽은 뒤 쓰기 전에 하드웨어가 세트한 B 를 0 으로 덮어 **B 를 잃는다**. W1C(rc_w1)는 "1 을 쓴 자리만 지움" 이라 `REG_WRITE(SR, A)` 한 번이면 A 만 지워진다 — 읽기 없음, 경쟁 없음. rc_w0 은 반대로 `REG_WRITE(SR, ~A)` (STM32F4 TIM SR 의 UIF 가 대표: `TIMx->SR = ~TIM_SR_UIF`). 함정: W1C 레지스터에 `SR |= A` 하면 읽은 1 들을 전부 다시 써서 **모든 플래그를 지운다**. rc_w0 에 `SR &= ~A` 는 흔히 쓰지만 RMW 라 읽은 뒤 세트된 플래그를 잃을 수 있다 (분석은 BM-B04). 이 레슨의 mock 에서 같은 레지스터에 `~0x10` 을 쓰면 rc_w0 비트 하나 + W1C 비트 넷이 함께 지워진다(0xFF → 0x1E0).

</details>

<details>
<summary>Q4. 디버거 메모리 창에 USART DR 을 띄워 두면 어떤 일이 생기나요?</summary>

디버거가 화면을 갱신할 때마다 DR 을 **읽는다** — 그 읽기는 CPU 의 읽기와 구별되지 않으므로 RXNE 가 지워지고 받은 바이트를 디버거가 가져간다. 펌웨어는 바이트를 "잃는다". F4 에서는 SR 읽기 + DR 읽기 순서로 ORE/IDLE 같은 에러 플래그까지 지워진다 (RM0090 기준, 확인 권장). 그래서 읽기에 부작용이 있는 레지스터(DR, FIFO, rc_r 상태)는 메모리 창·watch 에 두지 않는다. mock 에서도 같은 원리로, 테스트의 관찰은 백도어(`mock_peek`)로 해야 동작을 바꾸지 않는다 — 프론트도어로 DR 을 읽으면 테스트가 RXNE 를 지운다.

</details>

<details>
<summary>Q5. mock 으로 절대 못 잡는 버그는 무엇인가요?</summary>

mock 은 **내가 이해한 하드웨어** 를 검증한다. 내 이해가 틀리면 mock 과 드라이버가 같이 틀려서 테스트는 통과한다 (리셋값, 비트 위치, 클리어 순서를 RM 에서 잘못 읽은 경우). 그 밖에: 실제 타이밍(폴링 100번이 실제로는 몇 µs 인가 — Q8), 쓰기 버퍼·버스 지연(클럭 enable 직후 접근, BM-B03), 칩 errata, 클럭 트리 설정 오류, 전기적 문제(핀 AF 설정, 레벨, 노이즈), 컴파일러가 만든 코드 자체(volatile 누락 — 호스트 mock 빌드에서는 함수 호출이라 안 보인다). 그래서 계층을 둔다: 호스트 mock (빠름, 논리) → QEMU/Renode 같은 명령어 수준 시뮬레이터 (진짜 바이너리, BM-B08) → HIL 실보드 (스코프·로직 분석기). SSD 펌웨어에서 시뮬레이터 → FPGA → 실리콘 순서로 간 것과 같은 구조다.

</details>

<details>
<summary>Q6. 매크로 · 링크 타임 · 함수 포인터 이음매 중 무엇을 고르나요?</summary>

타깃 비용 0 이 필요하면 매크로(이 레슨: `uart_putc` 28 B, 직접 `ldr/str`). 드라이버 오브젝트를 다시 컴파일하지 않고 테스트하고 싶으면 링크 타임(`mock_read32` 를 정의한 파일만 바꿔 링크) — 대신 호출 비용이 남고(LTO 로 줄일 수 있음), 엉뚱한 구현이 링크돼도 조용하다. 같은 바이너리에서 런타임에 바꿔야 하면(스파이, 고장 주입, 여러 버스 인스턴스) 함수 포인터 — 실측 50 B, 접근마다 `blx` 간접 호출과 레지스터 6개 push. 실무 조합: 레지스터 접근은 매크로, 드라이버 위 계층(센서 드라이버가 I2C 버스를 쓰는 곳)은 ops 테이블(Zephyr 의 device API 구조체, Linux 의 `regmap` 이 이 모양)이다. C++ 이면 템플릿 파라미터 주입이 넷째 선택지 (CPP-A08).

</details>

<details>
<summary>Q7. 진짜 칩에서 지도에 없는 주소를 읽으면 어떻게 되나요? mock 은 왜 크래시하지 않게 했나요?</summary>

Cortex-M 에서 아무 장치도 응답하지 않는 주소 접근은 버스 에러 → **BusFault** 다. BusFault 가 활성화돼 있지 않으면(SHCSR.BUSFAULTENA = 0, 리셋 기본) HardFault 로 승격된다. 읽기는 보통 정확(precise) BusFault (BFAR 에 주소), 쓰기 버퍼를 거친 쓰기는 부정확(imprecise) BusFault 라 PC 가 이미 지나간 뒤다. 주변장치 영역 안의 예약 구간은 칩에 따라 조용히 0 을 읽기도 한다 (RM 확인). mock 이 크래시하지 않는 이유: 테스트 러너가 죽으면 나머지 결과를 잃고, 몇 번째 접근이 처음 틀렸는지(`first_bad`, 로그 위치)를 알 수 없다. 숫자로 남기고 `mock_clean()` 으로 실패시키는 편이 정보가 많다. 꼬리: "HardFault 에서 그 주소를 어떻게 찾나요?" → CFSR.BFARVALID 면 BFAR, 아니면 스택에 쌓인 PC 근처를 본다 (BM-M13, BM-A09).

</details>

<details>
<summary>Q8. 폴링 상한 100 은 실제 보드에서도 맞는 값인가요?</summary>

아니다. mock 에서 100 은 "tick 100개" 라 `tx_ticks` 만 정하면 정확히 맞는다 (연습 4: tx_ticks = 100 은 통과, 101 은 타임아웃). 실제 보드에서 115200 baud 8N1 한 문자는 10 비트 / 115200 ≈ 86.8 µs 다. 폴링 한 번이 APB 읽기 + 분기로 대략 수십~100 ns 수준이라면 100번은 수 µs — 한 문자가 끝나기도 전에 타임아웃이 난다. 반대로 CPU 클럭을 바꾸면 같은 횟수가 다른 시간이 된다. 그래서 실제 드라이버의 타임아웃은 **시간** 으로 쓴다: SysTick ms 카운터와 wrap-safe 비교 `(int32_t)(now - deadline) >= 0` (BM-B13). mock 쪽은 tick 을 "시간" 으로 해석하면 같은 코드가 동작한다 — tick 을 µs 로 정의하고 모델의 `tx_ticks` 를 문자 시간에 맞추면 된다.

</details>

## 9. 추가 연습

### 연습 1 — 하드웨어 리셋 `mock_hw_reset()`

`mock_reset()` 은 표까지 지우는 "테스트 픽스처 초기화" 다. 훅·모델은 그대로 두고 모든 레지스터 값만 리셋값으로 되돌리는 `mock_hw_reset()` (NRST 핀/워치독 리셋 흉내)을 만들고, 리셋 뒤 재초기화 없이 보낸 바이트가 어떻게 되는지 확인하라.

<details>
<summary>답 보기</summary>

`for (i < nreg) reg[i].val = reg[i].reset; mock_log_clear();` 리셋 뒤 CR1 = 0, BRR = 0, SR = 0xC0. `uart_putc('B')` 는 TXE=1 이라 **0 (성공)** 을 돌려주지만 모델은 TE 가 꺼져 있어 `tx_lost == 1`, 캡처는 리셋 전의 'A' 하나뿐. `uart_init` 을 다시 부른 뒤의 'C' 는 정상. 교훈: "리셋에서 깨어난 경로(워치독, Standby 복귀)도 초기화를 다시 탄다" 를 테스트할 수 있다. 실제로는 모델 내부 상태(전송 중 바이트 `tx_left`, RX 큐)도 리셋돼야 하므로 모델마다 리셋 콜백을 두는 것이 정석 — 이 연습은 전송이 끝난 뒤(`mock_tick(3)`) 리셋해 그 문제를 피했다.

</details>

### 연습 2 — stuck-at 고장 주입

기존 훅을 감싸는(데코레이터) `mock_inject_stuck(addr, stuck0, stuck1)` 을 만들어, CPU 가 보는 값의 특정 비트를 0 또는 1 로 고정하라. TXE stuck-at-0 과 RXNE stuck-at-1 에서 드라이버는 어떻게 되나?

<details>
<summary>답 보기</summary>

`fault_rd` 는 원래 rd 훅을 **먼저** 불러(부작용 유지) 그 결과에 `(v & ~stuck0) | stuck1` 을 적용한다. ctx 를 고장 슬롯으로 바꾸므로 wr 훅이 있으면 `fault_wr` 로 감싸 원래 ctx 를 넘겨야 한다 — 안 그러면 SR 의 `mock_access_wr` 가 고장 슬롯을 `mock_access_t` 로 읽는다 (테스트: 고장 주입 후 `REG_WRITE(SR, ~TC)` 가 여전히 rc_w0 으로 동작해 저장값 0x80). TXE stuck-at-0: `putc` 는 정확히 100번 폴링 후 -1, 캡처 0 — 폴링 상한이 없었다면 여기서 영원히 멈췄다. RXNE stuck-at-1: "Q" 하나만 받았는데 `getc` 세 번이 모두 성공하고 "QQQ" — **유령 바이트**. 진짜 RXNE 는 0 인데 CPU 는 1 로 본다. 실리콘 검증의 stuck-at 고장 모델과 같은 발상이고, 드라이버가 상태 비트를 어디까지 믿는지 드러낸다 (실제로는 ORE/FE 확인, 프로토콜 레벨 CRC 가 2차 방어선).

</details>

### 연습 3 — 함수 포인터 이음매로 같은 결과 확인

`bus_ops_t { rd, wr }` 를 받는 `uart_putc_ops` 를 만들고, 접근 수를 세면서 mock 으로 넘기는 "스파이 버스" 로 'x','y' 를 보내라. 매크로 판과 로그가 같은가?

<details>
<summary>답 보기</summary>

두 판 모두 로그 6개 (`R SR C0, W DR 'x', R SR 00, R SR 00, R SR C0, W DR 'y'`)로 `memcmp` 가 같다. 스파이 카운터는 읽기 4, 쓰기 2. 이음매가 바뀌어도 **관찰되는 버스 동작이 같다** 는 것이 이음매가 올바르다는 증거다. 타깃 비용은 5. 풀이의 실측대로 50 B, 접근마다 `blx`. 스파이를 매크로 판에서 하려면 mock 쪽에 카운터를 두면 되므로, 함수 포인터는 "같은 바이너리 안에서 버스를 바꿔야 할 때" 만 쓴다.

</details>

### 연습 4 — 폴링 비용과 타임아웃 경계

`tx_ticks` = t 일 때 n 바이트를 보내는 데 SR 읽기가 몇 번인지 식을 세우고 t = 1–8, n = 1–10 전부 확인하라. 폴링 상한 100 에서 `putc` 가 성공하는 최대 t 는?

<details>
<summary>답 보기</summary>

첫 바이트는 TXE 가 이미 1 → 1번. 이후 바이트마다 쓰기 접근이 tick 1개를 먹고, 남은 t−1 tick 동안 busy 읽기 t−1번, 그 다음 ready 1번 → t번. 합계 **1 + (n−1)·t** (80 조합 모두 일치). 두 번째 바이트의 폴링 t번이 상한 100 안에 들어야 하므로 t ≤ 100: t = 100 → SR 읽기 101번(1 + 100), 성공. t = 101 → busy 100번으로 상한 소진, -1 (SR 읽기도 101번). 경계 테스트(off-by-one)를 꼭 두 쪽 다 둔다. 실제 시간으로의 해석은 Q8.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 20/20 passed
```

## 10. 핵심 정리

- 드라이버는 `REG_READ/REG_WRITE` 로만 레지스터를 만진다. `UNIT_TEST` 면 `mock_read32/write32`, 타깃이면 `*(volatile uint32_t *)addr` — 타깃 오브젝트에 mock 은 0 바이트 (실측 `uart_*` 154 B 뿐).
- mock 코어 = 레지스터 표(addr, val, reset, rd, wr, ctx) + 로그 {R/W, addr, CPU 가 본/쓴 값} + 접근 1회 = 1 tick. 지도 밖 접근은 크래시 대신 세고 기록, 끝에서 `mock_clean()`.
- 프론트도어(버스, 부작용 있음) vs 백도어(peek/poke, 없음). 훅과 테스트 관찰은 백도어로.
- 접근 타입: `new = (old & (ro|rc)) | (val & rw) | (old & w1c & ~val) | (old & w0c & val)`; 읽기는 `stored & ~wo`, rc 는 읽은 **뒤** 클리어. W1C 는 `REG_WRITE(SR, A)`, rc_w0 은 `REG_WRITE(SR, ~A)`.
- 교차 레지스터 부작용(DR 읽기 → SR.RXNE 클리어)과 시간(TXE 가 N tick 뒤 1)은 전용 훅 + ticker 로. 리셋값(SR 0x00C0)도 모델의 일부.
- volatile 없으면: 한 번 읽고 `b .`, 바이트 읽기, 쓰기 합침 — mock 빌드에선 안 보이므로 타깃 objdump 로 확인.
- 이음매 비용: 매크로 0 (28 B) < 링크 타임 (호출) < 함수 포인터 (50 B, 접근마다 `blx`). 폴링 상한은 상한일 뿐 시간이 아니다 → BM-B13.

## 참고자료

- ST RM0090 *STM32F405/415, F407/417, F427/437, F429/439 Reference Manual* — 1.2 레지스터 접근 약어(rw, r, w, rc_w1, rc_w0, rc_r, rs …), USART 장 (USART_SR/DR/BRR/CR1 비트·리셋값·클리어 순서), 메모리 맵 (USART2 0x4000_4400) — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf> (이 레슨의 값은 기억 기반 대표값 — RM 확인 권장)
- ST PM0214 *STM32 Cortex-M4 MCUs and MPUs programming manual* — BusFault/HardFault, SHCSR, CFSR/BFAR, 쓰기 버퍼와 DSB
- ARM *ARMv7-M Architecture Reference Manual* (DDI 0403) — B3.5 메모리 속성, Device 메모리 접근 순서, BusFault 의 precise/imprecise
- CMSIS-SVD 형식: `access`, `modifiedWriteValues` (oneToClear, zeroToClear …), `readAction` (clear …) — <https://open-cmsis-pack.github.io/svd-spec/main/index.html>
- James W. Grenning, *Test-Driven Development for Embedded C* (Pragmatic, 2011) — 하드웨어 레지스터 mock (`IO_Read/IO_Write` + 기대 순서), LED 드라이버 예제
- Michael Feathers, *Working Effectively with Legacy Code*, 4장 "The Seam Model" (전처리·링크·객체 이음매)
- ThrowTheSwitch Unity / CMock (C 함수 mock 생성기) — <https://www.throwtheswitch.org/cmock>
- Accellera UVM 1.2 레지스터 계층 (front-door / back-door 접근 용어)
- Renode (주변장치 모델 + 실제 펌웨어 바이너리 실행), QEMU `-M mps2-an385` (BM-B08)
- 관련 레슨: SIV-B01 (레지스터 헤더 생성), CPP-A08 (C++ 이음매 3종 + 링크 심 실험), C-M14 (ISR↔main 공유 데이터), BM-B01 (CMSIS 헤더 `_Pos/_Msk`), BM-B03 (RCC 클럭 게이팅 mock), BM-B04 (W1C·BSRR RMW 함정), BM-B11 (GPIO 드라이버), BM-B12 (폴링 UART: BRR·TC·ORE), BM-B13 (SysTick 시간 기반 타임아웃), BM-B14 (타이머 모델), BM-M04 (인터럽트 UART), BM-M13 · BM-A09 (fault 처리·덤프), COMM-B01~B03 (UART 프레이밍·baud)
