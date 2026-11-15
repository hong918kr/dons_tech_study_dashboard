---
id: COMM-M11
time: 90분
updated: 2026-11-11
---
# COMM-M11 DMA 기반 UART 수신: circular DMA + idle line 감지

## 학습 목표

- circular DMA 의 상태를 레지스터 하나(NDTR)로 읽는 법을 정확히 말한다: 쓰기 위치 `pos = N − NDTR`, `NDTR == N` 이면 0, TC 에서 N 으로 재장전. 그리고 HT / TC / IDLE 세 IRQ 가 **같은 함수 하나**를 부르는 구조를 짠다.
- "지난번 처리 위치 `last` 부터 지금 위치 `pos` 까지" 를 파서로 넘기는 핸들러를 랩어라운드(두 조각)까지 포함해 구현하고, 3000 개 랜덤 도착/이벤트 순서 시나리오에서 바이트 스트림이 정확히 같다는 것을 테스트로 보인다.
- NDTR 로는 볼 수 없는 것 — **핸들러 사이에 N 바이트 이상** 오면 데이터가 조용히 사라진다 — 을 재현하고, HT/TC 플래그로 "한 바퀴 돌았다" 를 의심하는 검출기와 그 한계(전수 조사로 75.8~81.2% 만 잡음, 이론식과 일치)를 숫자로 말한다.
- 지연 예산을 계산한다: HT 를 켜면 "이벤트 뒤 N/2 바이트 시간 미만", N = 256 이면 115200 baud 에서 11.1 ms, 1 Mbaud 에서 1.28 ms. HT 를 끄면 쉬지 않는 스트림에서 100% 손실이 날 수 있다는 것을 시뮬레이션으로 보인다.
- 펌웨어 현실을 엮는다: 태스크와 IRQ 가 같은 핸들러를 부를 때의 중복 처리(임계 구역), 플래그 클리어 순서, Cortex-M7 캐시 invalidate(ARCH-M12), HAL `HAL_UARTEx_ReceiveToIdle_DMA` 의 `Size` 의미.

## 1. 문제

GNSS·LiDAR·모터 드라이버·RF 모듈이 1 Mbaud 로 가변 길이 프레임을 쏟아낼 때 "바이트마다 RXNE 인터럽트" 는 초당 10만 번의 IRQ 다. 그래서 실전 STM32 펌웨어의 표준형은 **circular DMA + IDLE line 인터럽트** 이고, 면접에서는 "DMA 로 받는데 프레임 끝은 어떻게 알죠? 버퍼가 한 바퀴 돌면요? 정확히 N 바이트가 오면요?" 로 이어진다. 이 레슨은 그 핸들러를 결정적(deterministic) 모델 위에서 짜고, 경계 사례를 전부 주입해 본다.

바이트 단위 수신기(COMM-B02)와 프레임 파서(COMM-B10)는 이미 있다. 이 레슨의 자리는 그 사이 — **하드웨어가 메모리에 쏟아 놓은 바이트를 빠짐없이, 두 번 없이 파서로 넘기는 것** 이다.

모델 (전부 이 레슨이 정한 정의. STM32 DMA/USART 를 본떴지만 레지스터 이름·비트는 대표값, RM 확인 권장):

| 요소 | 모델의 동작 |
|---|---|
| DMA 버퍼 `buf[N]` | N 짝수(테스트 8~256). USART 가 바이트를 받을 때마다 DMA 가 `buf[N − NDTR]` 에 쓰고 NDTR −1 |
| NDTR | N 에서 시작해 감소. 0 이 되면 **TC 플래그 + 즉시 N 으로 재장전** (circular) |
| HT 플래그 | 칸 N/2−1 을 쓴 직후 (NDTR == N/2) |
| TC 플래그 | 칸 N−1 을 쓴 직후 |
| IDLE 플래그 | 마지막 바이트 뒤 라인이 1 프레임 이상 조용하면. 한 번 서면 **새 바이트가 와야** 다시 설 수 있다 |
| 플래그와 IRQ | 플래그는 인터럽트 enable 과 상관없이 선다. 어떤 플래그가 IRQ 를 부를지는 설정(HT IRQ 켬/끔) |
| 시간 | 1 슬롯 = 1 바이트 시간 (8N1 = 10 비트). 슬롯마다 바이트 하나가 오거나 라인이 조용하다 |
| 핸들러 | HT / TC / IDLE IRQ 가 모두 `rx_dma_irq()` 하나를 부른다: 플래그 클리어 → NDTR 한 번 읽기 → `[last, pos)` 처리 |

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">circular DMA 버퍼 N = 16, 테스트 A3 의 순간: last = 12, NDTR = 13 → pos = 16 − 13 = 3</text>
  <g stroke="currentColor">
    <rect x="40" y="60" width="40" height="40" class="f-ok"/><rect x="80" y="60" width="40" height="40" class="f-ok"/>
    <rect x="120" y="60" width="40" height="40" class="f-ok"/>
    <rect x="160" y="60" width="40" height="40" class="f-mut"/><rect x="200" y="60" width="40" height="40" class="f-mut"/>
    <rect x="240" y="60" width="40" height="40" class="f-mut"/><rect x="280" y="60" width="40" height="40" class="f-mut"/>
    <rect x="320" y="60" width="40" height="40" class="f-mut"/><rect x="360" y="60" width="40" height="40" class="f-mut"/>
    <rect x="400" y="60" width="40" height="40" class="f-mut"/><rect x="440" y="60" width="40" height="40" class="f-mut"/>
    <rect x="480" y="60" width="40" height="40" class="f-mut"/>
    <rect x="520" y="60" width="40" height="40" class="f-acc"/><rect x="560" y="60" width="40" height="40" class="f-acc"/>
    <rect x="600" y="60" width="40" height="40" class="f-acc"/><rect x="640" y="60" width="40" height="40" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="55" y="55">0</text><text x="95" y="55">1</text><text x="135" y="55">2</text><text x="175" y="55">3</text>
    <text x="215" y="55">4</text><text x="255" y="55">5</text><text x="295" y="55">6</text><text x="335" y="55">7</text>
    <text x="375" y="55">8</text><text x="415" y="55">9</text><text x="451" y="55">10</text><text x="491" y="55">11</text>
    <text x="531" y="55">12</text><text x="571" y="55">13</text><text x="611" y="55">14</text><text x="651" y="55">15</text>
  </g>
  <line x1="360" y1="38" x2="360" y2="112" stroke="currentColor" stroke-dasharray="4,3"/>
  <text x="300" y="34" fill="currentColor" font-size="12">HT: 칸 7 쓴 뒤 (NDTR = 8)</text>
  <line x1="680" y1="38" x2="680" y2="112" stroke="currentColor" stroke-dasharray="4,3"/>
  <text x="560" y="34" fill="currentColor" font-size="12">TC: 칸 15 뒤, NDTR 0 → 16</text>
  <line x1="160" y1="140" x2="160" y2="104" stroke="currentColor" stroke-width="2"/>
  <polygon points="160,102 155,112 165,112" fill="currentColor"/>
  <text x="100" y="156" fill="currentColor">pos = 3 (DMA 가 다음에 쓸 칸)</text>
  <line x1="520" y1="140" x2="520" y2="104" stroke="currentColor" stroke-width="2"/>
  <polygon points="520,102 515,112 525,112" fill="currentColor"/>
  <text x="470" y="156" fill="currentColor">last = 12 (다음에 처리할 칸)</text>
  <path d="M680,100 C700,200 20,200 40,100" fill="none" stroke="currentColor" stroke-dasharray="2,3"/>
  <text x="300" y="186" fill="currentColor" font-size="12">DMA 는 칸 15 다음에 칸 0 으로 (circular)</text>
  <rect x="40" y="206" width="16" height="12" class="f-acc" stroke="currentColor"/>
  <text x="62" y="217" fill="currentColor" font-size="12">조각 1: buf[12..15] 4 B → sink</text>
  <rect x="280" y="206" width="16" height="12" class="f-ok" stroke="currentColor"/>
  <text x="302" y="217" fill="currentColor" font-size="12">조각 2: buf[0..2] 3 B → sink</text>
  <rect x="510" y="206" width="16" height="12" class="f-mut" stroke="currentColor"/>
  <text x="532" y="217" fill="currentColor" font-size="12">이미 처리한 칸 (지난 바퀴)</text>
  <text x="40" y="250" fill="currentColor" font-size="12">pos == last → 0 B · pos &gt; last → [last,pos) · pos &lt; last → [last,N) + [0,pos) · 그다음 last = pos</text>
</svg>
```

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">시간축 (N = 16, 지연 0): "HELLO" 5 B → 조용 → 14 B → 조용. 바이트 IRQ 0 번, 핸들러 4 번</text>
  <text x="4" y="66" fill="currentColor" font-size="12">RX</text>
  <g stroke="currentColor" class="f-acc">
    <rect x="60" y="50" width="140" height="24"/>
    <rect x="256" y="50" width="392" height="24"/>
  </g>
  <line x1="200" y1="74" x2="256" y2="74" stroke="currentColor"/><line x1="648" y1="74" x2="704" y2="74" stroke="currentColor"/>
  <g stroke="currentColor" stroke-dasharray="1,3" opacity="0.6">
    <line x1="88" y1="50" x2="88" y2="74"/><line x1="116" y1="50" x2="116" y2="74"/><line x1="144" y1="50" x2="144" y2="74"/>
    <line x1="172" y1="50" x2="172" y2="74"/>
    <line x1="284" y1="50" x2="284" y2="74"/><line x1="312" y1="50" x2="312" y2="74"/><line x1="340" y1="50" x2="340" y2="74"/>
    <line x1="368" y1="50" x2="368" y2="74"/><line x1="396" y1="50" x2="396" y2="74"/><line x1="424" y1="50" x2="424" y2="74"/>
    <line x1="452" y1="50" x2="452" y2="74"/><line x1="480" y1="50" x2="480" y2="74"/><line x1="508" y1="50" x2="508" y2="74"/>
    <line x1="536" y1="50" x2="536" y2="74"/><line x1="564" y1="50" x2="564" y2="74"/><line x1="592" y1="50" x2="592" y2="74"/>
    <line x1="620" y1="50" x2="620" y2="74"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="70" y="67">0</text><text x="98" y="67">1</text><text x="126" y="67">2</text><text x="154" y="67">3</text><text x="182" y="67">4</text>
    <text x="266" y="67">5</text><text x="294" y="67">6</text><text x="322" y="67">7</text><text x="350" y="67">8</text><text x="378" y="67">9</text>
    <text x="402" y="67">10</text><text x="430" y="67">11</text><text x="458" y="67">12</text><text x="486" y="67">13</text>
    <text x="514" y="67">14</text><text x="542" y="67">15</text><text x="574" y="67">0</text><text x="602" y="67">1</text><text x="630" y="67">2</text>
  </g>
  <text x="60" y="44" fill="currentColor" font-size="12">칸 번호 (DMA 가 쓴 위치)</text>
  <text x="4" y="116" fill="currentColor" font-size="12">IRQ</text>
  <g stroke="currentColor" stroke-width="2">
    <line x1="214" y1="80" x2="214" y2="124"/><line x1="326" y1="80" x2="326" y2="124"/>
    <line x1="550" y1="80" x2="550" y2="124"/><line x1="662" y1="80" x2="662" y2="124"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="196" y="140">IDLE</text><text x="314" y="140">HT</text><text x="540" y="140">TC</text><text x="644" y="140">IDLE</text>
    <text x="180" y="160">NDTR 11</text><text x="300" y="160">NDTR 8</text><text x="520" y="160">NDTR 16</text><text x="630" y="160">NDTR 13</text>
  </g>
  <g stroke="currentColor">
    <rect x="160" y="172" width="108" height="40" class="f-ok"/><rect x="272" y="172" width="108" height="40" class="f-ok"/>
    <rect x="496" y="172" width="108" height="40" class="f-ok"/><rect x="612" y="172" width="108" height="40" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="168" y="188">pos 5, last 0</text><text x="168" y="204">[0,5) → 5 B</text>
    <text x="280" y="188">pos 8, last 5</text><text x="280" y="204">[5,8) → 3 B</text>
    <text x="504" y="188">pos 0, last 8</text><text x="504" y="204">[8,16) → 8 B</text>
    <text x="620" y="188">pos 3, last 0</text><text x="620" y="204">[0,3) → 3 B</text>
  </g>
  <text x="10" y="240" fill="currentColor" font-size="12">IDLE = "프레임이 끝난 듯", HT/TC = "버퍼 절반이 찼다" (N/2 바이트마다 최소 한 번)</text>
  <text x="10" y="262" fill="currentColor" font-size="12">총 5 + 3 + 8 + 3 = 19 B. 위치·NDTR 값은 solution.c 모델로 확인한 값</text>
</svg>
```

### Part A — 위치 계산과 last..pos 처리 (랩어라운드)

1. `dma_pos(n, ndtr)`: `n − ndtr`, 결과가 n 이상이면 0. NDTR 을 0 으로 읽는 경계값(재장전 전, 또는 HAL 이 TC 에서 `Size = N` 을 주는 것과 같은 꼴)도 0 으로 접는다.
2. `rx_dma_process(rx, pos)`: `pos == last` → `empty_calls++` 하고 아무것도 안 함. `pos > last` → 한 조각. `pos < last` → `[last, N)` 다음 `[0, pos)`(pos 가 0 이면 한 조각), `wraps++`. 그리고 `last = pos`, `bytes`, `max_batch` 갱신.
3. `rx_dma_irq()`(주어짐): 플래그 스냅샷 + 클리어 → **NDTR 한 번** → 의심 검사(Part B) → `rx_dma_process`.
4. 테스트: NDTR→pos 표, "HELLO" + IDLE, 랩어라운드 두 조각(4 B + 3 B), 끝 칸에서 정확히 멈춤, **랜덤 3000 케이스**: N ∈ {8,16,32,64,256}, 버스트 1..2N, idle 0..3 슬롯, IRQ 지연 상한 0..2N−1 슬롯(실행마다 0..상한 랜덤), 가짜 호출 0~5%, HT IRQ 75% 켬 — 단, 실행 사이가 N−1 바이트를 넘지 않게 guard 를 둔다. 받은 스트림 == 보낸 스트림.

### Part B — NDTR 이 숨기는 것: 0 바이트, 두 플래그 동시, 정확히 N, N 초과

`rx_lap_suspect(last, pos, n, ht, tc)`: 지난번 처리 뒤 쌓인 바이트 수 d 는 `d0 = (pos − last) mod N` 일 수도, `d0 + N`, `d0 + 2N` 일 수도 있다. d0 칸이 칸 N/2−1 을 지나지 않는데 HT 가 서 있거나, 칸 N−1 을 지나지 않는데 TC 가 서 있으면 **d ≥ N 이 확실** → `lap_suspect++`. 주입하고 단언하는 사례:

| # | 주입 | 기대 결과 |
|---|---|---|
| B1 | HT 에서 8 B 처리 직후 IDLE (새 바이트 0) | 넘긴 것 0, `empty_calls` 1, 경보 없음 |
| B2 | last = 5 에서 14 B (HT·TC 둘 다 pending, d < N) | 14 B 정확히, 경보 없음, last = 3 |
| B3 | last = 10 에서 **정확히 16 B** | pos == last → 0 B 로 보임(16 B 조용히 손실), **경보 1** |
| B4 | last = 10 에서 21 B | 최신 5 B 만, 16 B 손실, 경보 1 |
| B5 | last = 7 에서 31 B (d0 = 15 가 칸 7·15 를 다 지남) | 16 B 손실, **경보 없음** (검출기의 한계 → Part C) |
| B6 | 전수: N ∈ {8,16,64} × last ∈ [0,N) × d ∈ [0,3N] | 무차별 대입 기대값과 100% 일치, d < N 에서 경보 0, d == N 에서 항상 경보 |
| B7 | NDTR 을 0 으로 읽힘 | pos 0 으로 접어 4 B 정확히 |
| B8 | HT IRQ 끔, 쉬지 않는 스트림 160 B, 지연 0 | **0 B 받음** — 매 TC 에서 pos == last == 0. HT 켜면 같은 스트림 무손실 |

### Part C — 펌웨어 쪽: 프레임 계층의 SEQ 검출기, 태스크와 IRQ, 기동 로그

1. 프레임 = COMM-B10 모델 축약판 `[A5][LEN 0..32][SEQ][PAYLOAD][CRC-16/CCITT-FALSE, BE]`. sink 가 바이트를 파서로 넘기고, 응용 계층의 `app_check_seq()` 가 SEQ 끊김을 센다(`seq_gaps`, `lost_est = Σ (seq − expect) mod 256`).
2. 시나리오: 프레임 1500 개, N = 64, HT 켬, 지연 0..31 (= N/2 − 1) → **무손실** 이어야 함. 같은 스트림에 IRQ 블랙아웃 10 번(실행 사이 N + 1..N + 64 B) → **10 번 모두** 무언가(랩 경보 / CRC·LEN 오류 / SEQ 끊김)가 울려야 하고, `lost_est` 가 실제 손실 프레임 수와 같아야 한다.
3. `rx_poll_from_task()`: 수신 타임아웃 폴링처럼 **태스크에서도** 같은 핸들러를 부를 때, 임계 구역 없이 부르면(주어진 `naive_poll_from_task`) IRQ 가 sink 안에서 선점해 바이트가 **두 번** 전달됨을 결정적으로 재현하고, `irq_disable / irq_enable` 로 고친다.
4. 한 줄 기동 로그: `calls / empty / wraps / bytes / max_batch / lap_suspect | frames ok / crc_err / len_err / seq_gaps / lost_est`.

## 2. Problem (English)

> **UART RX with circular DMA and idle-line detection.** A UART receives into `uint8_t buf[N]` via a DMA channel in circular mode. The firmware can read `NDTR` (remaining transfers: counts down from N, reloads to N after the last element and sets TC). Three interrupts exist: DMA half-transfer (HT), DMA transfer-complete (TC), and UART IDLE (line idle for one character time after at least one byte).
>
> ```c
> uint16_t dma_pos(uint16_t n, uint16_t ndtr);                 // write index in [0, n)
> size_t   rx_dma_process(uart_dma_rx *rx, uint16_t pos);      // hand [last, pos) to sink, handle wrap
> bool     rx_lap_suspect(uint16_t last, uint16_t pos, uint16_t n, bool ht, bool tc);
> size_t   rx_poll_from_task(uart_dma_rx *rx);                 // safe to call from thread context
> ```
>
> - All three IRQs call one handler. It reads NDTR exactly once, delivers every new byte exactly once, in order, as at most two contiguous chunks, and never allocates.
> - Example (N = 16): last = 12, NDTR = 13 → pos = 3 → sink gets `buf[12..15]` then `buf[0..2]`, last becomes 3.
> - Edge cases: an IDLE event with no new data; HT and TC both pending; exactly N bytes between two handler runs; more than N.
>
> Follow-ups: (1) What is the maximum interrupt latency you can tolerate at 1 Mbaud with N = 256? (2) How would you detect that the buffer lapped? (3) What changes on a Cortex-M7 with D-cache? (4) The same handler is also called from a task on a timeout — what can go wrong?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 다섯 개: A1 `dma_pos`, A2 `rx_dma_process`, B1 `rx_lap_suspect`, C1 `app_check_seq`, C2 `rx_poll_from_task`. mock 하드웨어(`hw_rx_byte`, `hw_line_idle`), 공통 진입점 `rx_dma_irq`, 슬롯 시뮬레이터, 프레임 파서, 임계 구역 모델(`irq_disable/enable/raise`)은 주어진다. 스텁은 "위치 0 / 아무것도 안 넘김 / 경보 없음" 이라 모든 시뮬레이션 루프가 그대로 끝난다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:354  dma_pos(16, 15) == 1
FAIL starter.c:355  dma_pos(16, 8) == 8
…
A4 random: 3000 cases, 825558 bytes, wraps=0 empty_calls=0 HT+TC-both-pending=8297 lap_suspect=0 bad=3000
…
B6 N= 64: overrun (d>N) cases  8192, caught by HT/TC check     0 (0.0%)
…
C2 naive (no lock)  : sent 10 bytes, got 0: ""
FAIL starter.c:636  g_got_n > g_sent_n
C2 with critical section: sent 10 bytes, got 0: ""
FAIL starter.c:635  got_equals_sent() && rx.last == 15
14/44 passed
```

(exit 1.) 단계별로 확인하기 좋다 (실측): A1·A2 만 채우면 **35/44** — 데이터 단언은 다 통과하고 경보·SEQ·임계 구역 단언 9 개가 남는다. B1 까지 **40/44**, C1·C2 까지 **44/44**.

## 4. 힌트

<details><summary>힌트 1 — NDTR 은 "남은 수", 위치는 "쓴 수"</summary>

DMA 는 `buf[0]` 부터 쓰고 NDTR 은 N 에서 내려간다. 지금까지 이 바퀴에서 쓴 수 = N − NDTR = 다음에 쓸 칸. NDTR == N 이면 0 칸 썼다(또는 정확히 한 바퀴 다 썼다 — 이게 Part B 의 모호함). 재장전 직전 0 으로 읽히면 N − 0 = N 이니 0 으로 접는다. `uint16_t` 빼기가 음수로 넘쳐도 `>= n` 검사 하나로 같이 걸린다.

</details>

<details><summary>힌트 2 — 경우는 셋뿐이다</summary>

`pos == last`, `pos > last`, `pos < last`. 마지막 경우는 `[last, N)` 과 `[0, pos)` 두 번 sink 를 부른다 — **복사하지 말고** 버퍼 포인터를 그대로 넘긴다(파서가 바이트 단위로 먹으면 연속일 필요가 없다). pos 가 0 이면 두 번째 조각은 건너뛴다. `last = pos` 는 **넘긴 다음에** 쓴다 (Part C 의 선점 문제가 바로 이 사이에서 생긴다).

</details>

<details><summary>힌트 3 — "칸 c 가 구간 안에 있나" 를 mod 로</summary>

구간 `[last, last + d0)` (mod N) 안에 칸 c 가 있다 ⇔ `(c − last) mod N < d0`. C 에서는 음수를 피하려고 `(c + N − last) % N`. HT 는 칸 N/2−1, TC 는 칸 N−1. 플래그가 서 있는데 그 칸이 구간 밖이면, 그 플래그는 "이번 d0 바이트" 로 설명이 안 된다 → 최소 한 바퀴 더 돌았다.

</details>

<details><summary>힌트 4 — SEQ 의 빼기는 mod 256 에서</summary>

`(uint8_t)(seq − expect)` 는 255 → 0 넘어감도 맞게 센다(예: expect 250, seq 3 → 9 개 손실). 첫 프레임은 기준을 잡을 뿐 끊김이 아니다(`synced` 플래그). 256 개를 통째로 잃으면 mod 256 이라 안 보인다 — 오버런 한 번이 잃는 바이트(≈ N)보다 SEQ 공간이 충분히 커야 한다.

</details>

<details><summary>힌트 5 — 태스크에서 부를 때는 IRQ 를 잠깐 막는다</summary>

`last` 는 IRQ 와 태스크가 같이 쓰는 상태다. 태스크가 `last` 를 읽고 → sink → `last` 를 쓰는 사이에 IRQ 가 들어오면, IRQ 도 같은 `last` 로 같은 바이트를 넘긴다. `irq_disable()` → `rx_dma_irq()` → `irq_enable()`. 막혀 있던 IRQ 는 enable 순간 실행된다(모델의 `irq_enable` 이 그렇게 해 준다).

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "바이트 IRQ 대신 DMA 가 링 버퍼에 쓰고, 나는 **위치만** 읽는다. 위치 = N − NDTR."
2. "언제 읽나? 세 번: 프레임이 끝났을 때(IDLE), 버퍼 절반(HT), 끝(TC). 셋 다 같은 함수."
3. "함수는 last..pos 를 넘기고 last = pos. 랩어라운드면 두 조각."
4. "한계: 핸들러 사이 N 바이트 이상이면 NDTR 로는 모른다 → 지연 예산 N/2 바이트 시간, 검출은 플래그 + 상위 계층 SEQ/CRC."
5. "실전 함정: 태스크·다른 우선순위 IRQ 에서 부르면 임계 구역, M7 이면 캐시 invalidate, HAL 의 Size 는 위치."

### Part A — 위치와 두 조각

`dma_pos` 는 두 줄이다. 핵심은 **NDTR 을 한 번만 읽는 것**: `pos` 를 계산할 때 한 번, 길이를 계산할 때 또 한 번 읽으면 그 사이 DMA 가 바이트를 써서 두 값이 다를 수 있다. 읽은 값을 지역 변수에 고정하면 그 뒤에 들어온 바이트는 다음 이벤트가 처리한다 — 잃지 않는다.

시간축 그림의 예를 표로 따라가면 (N = 16, solution.c 모델로 확인한 값):

| 이벤트 | NDTR | pos | last (전) | 넘긴 구간 | 바이트 | last (후) |
|---|---|---|---|---|---|---|
| IDLE ("HELLO" 끝) | 11 | 5 | 0 | [0,5) | 5 | 5 |
| HT (칸 7 씀) | 8 | 8 | 5 | [5,8) | 3 | 8 |
| TC (칸 15 씀, 재장전) | 16 | 0 | 8 | [8,16) — pos 가 0 이라 한 조각 | 8 | 0 |
| IDLE | 13 | 3 | 0 | [0,3) | 3 | 3 |

TC 행이 `pos < last` 이지만 `pos == 0` 이라 두 번째 조각이 비는 경우다. 테스트 A3 은 진짜 두 조각(last 12 → pos 3: 4 B + 3 B)과 이 경우를 둘 다 확인한다.

랜덤 테스트 A4 의 실제 출력:

```text
A4 random: 3000 cases, 825558 bytes, wraps=21928 empty_calls=9041 HT+TC-both-pending=8297 lap_suspect=0 bad=0
```

빈 호출 9041 번, HT 와 TC 가 함께 쌓인 실행 8297 번, 랩 21928 번 — 브리프가 요구한 "IDLE 늦게 / HT·TC 동시 / 0 바이트 이벤트" 가 실제로 많이 일어났고, 그 모두에서 스트림이 같았다. 테스트가 그 경우의 **발생 횟수 > 0** 까지 CHECK 한다(커버리지를 단언하지 않으면 랜덤 테스트가 쉬운 경우만 돌아도 모른다).

### Part B — 정확히 N 은 왜 모호한가, 그리고 검출기

NDTR 은 "이번 바퀴에서 몇 칸" 만 안다. d 바이트가 오면 pos 는 `(last + d) mod N` 이 되므로, d = 0 과 d = N 은 **같은 레지스터 값** 이다. d = N 일 때 버퍼에는 새 데이터 N 바이트가 전부 멀쩡히 있지만(아직 하나도 덮이지 않았다), 핸들러는 "새 바이트 0" 으로 읽는다 (B3). d = N + k 이면 가장 오래된 k 바이트가 덮였고, 핸들러는 최신 k 바이트만 넘긴다 — 손실 N 바이트가 **연속 덩어리** 로 사라진다 (B4).

검출기는 이 모호함을 플래그로 깬다. 플래그는 "그 칸을 썼다" 는 사건의 흔적이므로:

- d = N 이면 N 칸을 다 썼으니 HT·TC 둘 다 서 있다. d0 = 0 인 구간은 아무 칸도 안 지나므로 둘 다 설명 불가 → 항상 경보 (B6 의 `eqn_miss == 0`).
- d < N 이면 플래그는 정확히 d0 구간이 지난 칸에서만 선다 → 경보 0 (B6 의 `lt_flag == 0`, A4 의 3000 케이스에서도 0).
- d > N 이면 HT·TC 가 다 서 있다. d0 구간이 칸 N/2−1 과 N−1 을 **둘 다** 지나면 플래그가 다 설명돼 놓친다 (B5).

놓치는 비율은 계산된다. 길이 d0 인 호(arc)가 N/2 떨어진 두 칸을 다 덮는 시작점 수는 `max(0, 2(d0 − N/2))`. d ∈ (N, 3N] × last ∈ [0, N) 전체 2N² 경우 중 놓치는 수 = 2 · Σ_{j=1}^{N/2−1} 2j = N(N−2)/2, 따라서 **잡는 비율 = 1 − (N−2)/(4N)** → N = 8: 81.25%, 16: 78.13%, 64: 75.78% (N → ∞ 이면 75%). 전수 조사 실측:

```text
B6 N=  8: overrun (d>N) cases   128, caught by HT/TC check   104 (81.2%)
B6 N= 16: overrun (d>N) cases   512, caught by HT/TC check   400 (78.1%)
B6 N= 64: overrun (d>N) cases  8192, caught by HT/TC check  6208 (75.8%)
```

이론과 정확히 같다 (104/128, 400/512, 6208/8192). 단, 이건 "d 가 균등" 이라는 인위적 분포다. 실제 지연으로 생긴 오버런은 대개 d 가 N 을 조금 넘는 경우라 d0 가 작고, 그래서 demo 에서는 Lmax ≤ N 인 모든 행에서 100% 잡혔다(아래 표). Lmax = 2N 처럼 두 바퀴 가까이 밀리면 80~86% 로 떨어진다.

**B8 — HT 를 끄면.** "인터럽트가 너무 많다" 며 HT 를 끄는 일이 흔하다. 쉬지 않는 스트림에서 핸들러가 TC 에서 즉시 돌면: 직전 TC 에서 last = 0, 이번 TC 에서 pos = 0 → 매번 "0 바이트", **전부 손실**. 실행 결과 160 B 중 0 B 수신, 경보 10 번. idle 로 끊기는 짧은 프레임이면 IDLE 이 대신 일해서 괜찮아 보인다 — 그래서 벤치에서는 통과하고 연속 스트림 센서를 붙인 날 터진다.

> 현장 주의: 이 검출기는 모델에서 핸들러가 원자적이라 거짓 경보가 0 이다. 실제 칩에서는 플래그 레지스터를 읽은 뒤 클리어하기 전 사이에 새 플래그가 서면, 그 바이트는 이번에 처리했는데 플래그는 다음 번까지 남아 거짓 경보가 될 수 있다. 그래서 `lap_suspect` 는 **경고 카운터** 로 쓰고, 손실 판정은 상위 계층 SEQ/CRC 로 한다 (Part C).

### Part C — SEQ 검출기, 선점, 로그

프레임 시나리오 실제 출력:

```text
C1 normal    uart1 rx: calls=1233 empty=3 wraps=485 bytes=31061 max_batch=60/64 lap_suspect=0 | frames ok=1500 crc_err=0 len_err=0 seq_gaps=0 lost_est=0 (sent 1500, overrun runs 0)
C1 blackout  uart1 rx: calls=1208 empty=5 wraps=474 bytes=30391 max_batch=58/64 lap_suspect=8 | frames ok=1458 crc_err=4 len_err=1 seq_gaps=10 lost_est=42 (sent 1500, overrun runs 10)
   blackouts=10 detected=10 (windows with: lap_suspect 8, crc/len err 5, seq gap 10)
```

읽는 법:

- **normal**: 지연 0..31 슬롯(= N/2 − 1)에서 1500/1500, 오버런 0. `max_batch=60/64` — 한 번에 60 바이트까지 쌓였다. 이론 상한 N/2 + Lmax = 63. 이 숫자를 로그로 내보내면 "여유가 4 바이트뿐" 을 필드에서 미리 안다.
- **blackout**: 10 번의 오버런을 랩 경보는 8 번, CRC/LEN 오류는 5 번(잘린 프레임이 다음 프레임과 이어 붙음), **SEQ 끊김은 10 번 전부** 잡았다. `lost_est = 42 = 1500 − 1458` — SEQ 로 센 손실이 실제 손실과 정확히 같다. 받은 프레임은 보낸 프레임의 순서 유지 부분열(가짜 0).
- 결론: 바이트 계층의 경보는 빠르지만 불완전, 프레임 계층의 SEQ 는 늦지만(다음 프레임이 와야) 확실하다. 둘 다 둔다.

**태스크 + IRQ.** 수신 타임아웃(예: 5 ms 동안 IDLE 이 안 오면 강제로 비우기)을 RTOS 태스크에서 같은 핸들러로 부르는 코드는 흔하다. 결정적 주입: 태스크가 "ABCDEFGHIJ" 를 sink 로 넘기는 도중(= `last = pos` 저장 전) 5 바이트가 오고 IRQ 가 선점한다.

```text
C2 naive (no lock)  : sent 15 bytes, got 30: "ABCDEFGHIJABCDEFGHIJ5678956789"
C2 with critical section: sent 15 bytes, got 15: "ABCDEFGHIJ56789"
```

임계 구역이 없으면: IRQ 가 아직 0 인 `last` 로 "ABCDEFGHIJ56789" 를 다시 넘기고 `last = 15`, 태스크로 돌아와 자기가 계산한 `last = 10` 을 덮어써 **되감는다** → 다음 IRQ 가 "56789" 를 또 넘긴다. 15 바이트가 30 바이트가 됐다. 고친 코드는 IRQ 를 막고 같은 함수를 부른 뒤 풀고, 막혀 있던 IRQ 는 풀리는 순간 돌아 빈 호출 없이 "56789" 만 넘긴다. 실제 Cortex-M 에서는:

- `__disable_irq()` 대신 PRIMASK 저장/복원(`uint32_t m = __get_PRIMASK(); __disable_irq(); … __set_PRIMASK(m);`) — 이미 막힌 문맥에서 불려도 안전. 또는 BASEPRI 로 해당 IRQ 우선순위만 막기.
- **DMA IRQ 와 USART IRQ 가 서로 다른 벡터** 다. 둘의 우선순위가 다르면 하나가 다른 하나를 선점해 똑같은 문제가 IRQ 끼리 생긴다. 같은 우선순위로 두면(같은 그룹 우선순위끼리는 선점하지 않는다) 해결된다.
- 대안 설계: IRQ 는 플래그만 보고 태스크 알림(semaphore / task notification)만 주고, **처리는 태스크 하나만** 한다 — 공유 상태가 없어진다(RTOS-B 의 큐·세마포 레슨). 대신 태스크 지연이 지연 예산에 들어간다.

### 지연 예산 — 몇 ms 안에 돌아야 하나

마지막 실행 때 위치가 p0 였다고 하자. 다음 HT/TC 이벤트는 늦어도 N/2 바이트 뒤에 온다(p0 가 마침 경계였을 때). 이벤트 뒤 지연 L 바이트 시간 후 핸들러가 돌면 쌓인 양은 최대 N/2 + L. 이것이 N − 1 이하여야 하므로 **L ≤ N/2 − 1 바이트 시간** (연속 시간으로는 "N/2 바이트 시간 미만"). 핸들러가 NDTR 을 읽은 뒤 데이터를 소비하는 시간도 이 예산에 들어간다(그동안 DMA 는 계속 쓴다). 연습 1 이 고정 지연 L 을 늘려 가며 문턱을 시뮬레이션으로 찾았다: N = 16 → 첫 손실 L = 8, N = 64 → L = 32, 즉 안전 최대 N/2 − 1 이 정확히 맞는다. 시간으로 바꾸면 (8N1, N = 256, practice.c 출력):

| baud | 바이트 시간 | 한 바퀴 (N 바이트) | 예산 (HT 켬, &lt; N/2 바이트) |
|---|---|---|---|
| 115200 | 86.81 µs | 22.222 ms | 11.111 ms |
| 460800 | 21.70 µs | 5.556 ms | 2.778 ms |
| 921600 | 10.85 µs | 2.778 ms | 1.389 ms |
| 1000000 | 10.00 µs | 2.560 ms | 1.280 ms |
| 3000000 | 3.33 µs | 0.853 ms | 0.427 ms |

1 Mbaud 에서 1.28 ms 는 "더 높은 우선순위 IRQ + 가장 긴 임계 구역 + 핸들러 자신의 파싱" 의 합으로 넉넉하지만, 태스크에서 처리하는 설계면 태스크 스케줄링 지연(틱 1 ms 단위의 다른 태스크)이 바로 들어온다 → N 을 1024 로 키우거나 처리 태스크 우선순위를 올린다.

### 실험 — `demo_latency.c` (실측)

이벤트 뒤 지연을 0..Lmax 균등 랜덤으로 두고 행마다 100만 바이트 (`./demo_latency`, 약 0.25 초, Apple clang 21, Apple Silicon). 발췌 (전체 표는 demo 실행):

| N | HT | 트래픽 | Lmax | 손실 | 오버런 실행 | 랩 경보가 잡은 비율 | max batch |
|---|---|---|---|---|---|---|---|
| 64 | on | 연속 | 31 | 0.0000% | 0 | - | 63 |
| 64 | on | 연속 | 32 | 0.4032% | 63 | 100.0% | 63 |
| 64 | on | 연속 | 48 | 18.6496% | 2914 | 100.0% | 63 |
| 64 | on | 연속 | 128 | 61.2288% | 7842 | 80.9% | 63 |
| 64 | on | 프레임+idle | 32 | 0.0000% | 0 | - | 62 |
| 64 | on | 프레임+idle | 48 | 1.8304% | 286 | 100.0% | 63 |
| 64 | off | 연속 | 0 | 100.0000% | 15625 | 100.0% | 0 |
| 64 | off | 프레임+idle | 0 | 0.0000% | 0 | - | 37 |
| 64 | off | 프레임+idle | 31 | 0.0640% | 10 | 100.0% | 63 |
| 256 | on | 연속 | 127 | 0.0000% | 0 | - | 255 |
| 256 | on | 연속 | 128 | 0.0256% | 1 | 100.0% | 255 |
| 256 | on | 프레임+idle | 192 | 0.0000% | 0 | - | 210 |
| 256 | off | 연속 | 0 | 99.9936% | 3906 | 100.0% | 64 |
| 256 | off | 프레임+idle | 127 | 0.0000% | 0 | - | 150 |

(프레임+idle = 5..37 바이트 프레임 뒤 idle 1..4 슬롯. 거짓 경보는 모든 행에서 0. N = 256 HT 끔 연속이 99.9936% 인 건 100만 = 3906 × 256 + 64 라 마지막 64 바이트를 꼬리 IDLE 이 건졌기 때문.)

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">demo_latency.c 실측 (N = 64): 손실률 vs 최대 IRQ 지연 Lmax / N</text>
  <line x1="80" y1="260" x2="700" y2="260" stroke="currentColor"/>
  <line x1="80" y1="60" x2="80" y2="260" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="50" y="264">0%</text><text x="44" y="214">25%</text><text x="44" y="164">50%</text><text x="44" y="114">75%</text><text x="38" y="64">100%</text>
    <text x="76" y="278">0</text><text x="220" y="278">0.5</text><text x="374" y="278">1</text><text x="674" y="278">2</text>
    <text x="300" y="300">Lmax / N (이벤트 뒤 지연 상한, 바이트 시간 / 버퍼 크기)</text>
  </g>
  <g stroke="currentColor" stroke-dasharray="2,4" opacity="0.5">
    <line x1="80" y1="210" x2="700" y2="210"/><line x1="80" y1="160" x2="700" y2="160"/><line x1="80" y1="110" x2="700" y2="110"/>
  </g>
  <line x1="230" y1="50" x2="230" y2="260" stroke="currentColor" stroke-dasharray="6,4"/>
  <text x="236" y="58" fill="currentColor" font-size="12">N/2: 이론 문턱 (L ≤ N/2 − 1 이면 무손실)</text>
  <polyline points="80,260 117.5,260 155,260 225.3,260 230,259.2 305,222.7 375.3,191.3 380,189.9 680,137.5" fill="none" stroke="currentColor" stroke-width="2"/>
  <polyline points="80,260 117.5,260 155,260 225.3,260 230,260 305,256.3 375.3,234.3 380,231.3 680,156.3" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6,3"/>
  <polyline points="80,60 225.3,156.8 375.3,158.4" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="1,3"/>
  <g stroke="currentColor">
    <circle cx="305" cy="222.7" r="4" class="f-warn"/><circle cx="375.3" cy="191.3" r="4" class="f-warn"/><circle cx="680" cy="137.5" r="4" class="f-warn"/>
    <circle cx="305" cy="256.3" r="4" class="f-ok"/><circle cx="375.3" cy="234.3" r="4" class="f-ok"/><circle cx="680" cy="156.3" r="4" class="f-ok"/>
    <circle cx="80" cy="60" r="4" class="f-acc"/><circle cx="225.3" cy="156.8" r="4" class="f-acc"/><circle cx="375.3" cy="158.4" r="4" class="f-acc"/>
  </g>
  <g font-size="12" fill="currentColor">
    <text x="470" y="150">HT 켬, 연속 스트림 (61.2% @ 2N)</text>
    <text x="470" y="232">HT 켬, 프레임+idle (51.8% @ 2N)</text>
    <text x="100" y="80">HT 끔, 연속: 지연 0 에서도 100%</text>
    <text x="240" y="150">HT 끔 ≈ 51%</text>
  </g>
  <text x="10" y="322" fill="currentColor" font-size="12">실선 HT 켬·연속 / 긴 점선 HT 켬·프레임 / 짧은 점선 HT 끔·연속. 점 = 실측값</text>
</svg>
```

읽는 법: (1) HT 를 켜면 Lmax ≤ N/2 − 1 에서 손실 0 — 이론 문턱이 실측과 맞는다. 문턱을 1 넘기면(Lmax = N/2) 바로 손실이 생기지만 드물다(0.40%, N = 256 에서는 100만 바이트 중 오버런 1 번) — **"가끔 한 바이트 덩어리가 빠져요" 버그의 정체**. (2) idle 로 끊긴 트래픽은 IDLE 이 추가 실행을 만들어 문턱이 오른쪽으로 밀린다 — 벤치 트래픽으로 예산을 재면 낙관적이 된다. (3) HT 를 끄면 연속 스트림은 지연 0 에서 100% 손실, 지연이 있으면 오히려 ~50% (TC 실행이 다음 바이트들 뒤로 밀려 pos ≠ 0 이 되므로) — 어떤 경우에도 쓸 수 없다.

### Cortex-M7 캐시와 HAL

- **캐시** (STM32F7/H7, i.MX RT): DMA 는 RAM 에 쓰고 CPU 는 D-cache 에서 읽는다. 핸들러가 `[last, pos)` 를 읽기 **전에** 그 범위를 덮는 32 B 라인을 invalidate 해야 한다(`SCB_InvalidateDCache_by_Addr`). 버퍼는 32 B 정렬 + 크기 32 B 배수 — 아니면 invalidate 가 같은 라인의 옆 변수 쓰기를 지운다. 연습 4 가 실측: invalidate 없이 844 바이트 stale, 정렬 + invalidate 0, 16 B 어긋난 버퍼 + invalidate 는 데이터는 맞지만 **옆 변수 값이 사라짐**. 원리·순서 전체는 ARCH-M12. 대안: MPU 로 그 영역을 non-cacheable, 또는 DMA 가 닿는 non-cacheable SRAM 영역에 두기(칩별 메모리 맵 확인 권장).
- **HAL** (대표값, HAL 버전별 소스 확인 권장): `HAL_UARTEx_ReceiveToIdle_DMA(&huart, buf, N)` + DMA circular 설정 → `HAL_UARTEx_RxEventCallback(huart, Size)` 가 HT·TC·IDLE 에서 불린다. `Size` 는 **버퍼 안 위치**(HT = N/2, TC = N, IDLE = N − NDTR)이지 새 바이트 수가 아니다. 그래서 콜백 본문이 이 레슨의 `rx_dma_process(pos = Size == N ? 0 : Size)` 와 같다. 새 HAL 은 `HAL_UARTEx_GetRxEventType()` 으로 HT/TC/IDLE 를 구분해 준다. 연습 5: Size 를 바이트 수로 오해하면 5879 바이트가 13441 바이트로 불어난다.
- **ORE**: USART 의 overrun 플래그는 "DMA 가 RDR 을 제때 못 읽었다"(DMA 가 꺼졌거나 버스가 막힘)는 뜻이다. circular 버퍼를 **소프트웨어가** 못 따라가 덮인 것은 어떤 하드웨어 플래그도 알려 주지 않는다 — 이 레슨의 검출기와 SEQ 가 필요한 이유. ORE 발생 시 DMA 요청이 계속되는지는 패밀리마다 다르니 RM 확인 권장.
- **circular 가 없는 DMA** (예: nRF52 UARTE EasyDMA — 확인 권장): 버퍼 두 개를 번갈아 걸고(ENDRX 에서 다음 포인터), 프레임 끝은 RX 타임아웃이나 "바이트 수를 세는 타이머" 로 안다. 위치 대신 "버퍼 번호 + 개수" 로 같은 last..pos 논리를 짠다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 44/44 passed, exit 0 (약 0.12 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -O2 demo_latency.c -o demo_latency && ./demo_latency   # 약 0.25 초, 위 표
```

`solution.c` 실행 출력 중 테스트 외 줄 (실제):

```text
A4 random: 3000 cases, 825558 bytes, wraps=21928 empty_calls=9041 HT+TC-both-pending=8297 lap_suspect=0 bad=0
B6 N=  8: overrun (d>N) cases   128, caught by HT/TC check   104 (81.2%)
B6 N= 16: overrun (d>N) cases   512, caught by HT/TC check   400 (78.1%)
B6 N= 64: overrun (d>N) cases  8192, caught by HT/TC check  6208 (75.8%)
C1 normal    uart1 rx: calls=1233 empty=3 wraps=485 bytes=31061 max_batch=60/64 lap_suspect=0 | frames ok=1500 crc_err=0 len_err=0 seq_gaps=0 lost_est=0 (sent 1500, overrun runs 0)
C1 blackout  uart1 rx: calls=1208 empty=5 wraps=474 bytes=30391 max_batch=58/64 lap_suspect=8 | frames ok=1458 crc_err=4 len_err=1 seq_gaps=10 lost_est=42 (sent 1500, overrun runs 10)
   blackouts=10 detected=10 (windows with: lap_suspect 8, crc/len err 5, seq gap 10)
C2 naive (no lock)  : sent 15 bytes, got 30: "ABCDEFGHIJABCDEFGHIJ5678956789"
C2 with critical section: sent 15 bytes, got 15: "ABCDEFGHIJ56789"
44/44 passed
```

## 7. 자주 하는 실수

**1. NDTR 을 두 번 읽는다.** `if (N - NDTR > last) process(&buf[last], N - NDTR - last)` 처럼 조건과 길이에서 따로 읽으면, 그 사이 바이트가 들어와 조건은 "한 조각" 인데 길이는 다른 값이 된다. 고친 코드: `uint16_t pos = dma_pos(n, hw_read_ndtr(h));` 한 번, 그 뒤로는 `pos` 만.

**2. HT 를 끈다.** "IDLE 과 TC 면 충분하다" 는 idle 로 끊기는 벤치 트래픽에서만 맞다. 쉬지 않는 스트림에서는 TC 마다 pos == last == 0 이라 **전부** 잃는다(B8, demo 100%). 고친 코드: HT·TC·IDLE 셋 다 같은 핸들러, 예산 N/2.

**3. HAL 의 `Size` 를 새 바이트 수로 쓴다.** `parse(buf, Size)` 는 매번 `buf[0]` 부터 다시 넘긴다(연습 5: 5879 → 13441 바이트). 고친 코드: `Size` 를 위치로 보고 last 와 비교 — `rx_dma_process(rx, Size == N ? 0 : Size)`.

**4. 플래그를 NDTR 다음에 지운다.** NDTR 을 읽은 뒤 새 바이트 + IDLE 이 오고, 그 IDLE 을 지워 버리면 그 바이트는 다음 트래픽까지 갇힌다 — 요청-응답 프로토콜이면 응답 하나가 영원히 안 온다(연습 3). 고친 코드: 플래그 클리어 → NDTR 읽기. 최악이라도 빈 호출 한 번이 더 생길 뿐이다.

**5. 태스크에서 임계 구역 없이 부른다 / DMA IRQ 와 USART IRQ 우선순위가 다르다.** 같은 바이트가 두 번, `last` 되감기(C2: 15 → 30 바이트). 고친 코드: PRIMASK 저장/복원으로 감싸기, 두 IRQ 를 같은 우선순위로, 또는 처리를 한 문맥으로 몰기.

**6. Cortex-M7 에서 invalidate 를 빼먹거나 정렬 안 된 버퍼를 invalidate 한다.** 앞의 것은 stale 데이터(연습 4: 844 바이트), 뒤의 것은 옆 변수 손상. 고친 코드: `__attribute__((aligned(32)))` + 크기 32 배수 + 읽기 직전 범위 invalidate, 또는 non-cacheable 영역(ARCH-M12).

**7. ISR 안에서 무거운 파싱을 한다.** 예산 N/2 바이트 시간에는 핸들러 자신의 시간도 들어간다. 콜백이 프레임마다 플래시 쓰기·printf 를 하면 다음 이벤트를 놓친다. 고친 코드: ISR 은 위치 계산 + 짧은 파서(바이트당 O(1), COMM-B10/M10) 또는 SPSC 링으로 복사 후 태스크 알림 (DS-A02).

**8. `lap_suspect == 0` 을 "손실 없음" 의 증거로 쓴다.** 균등 분포에서 24% 이상을 놓치고(B5, B6), 실제 칩에서는 거짓 경보도 가능하다. 고친 코드: 프레임에 SEQ(+CRC)를 넣고 손실 판정은 거기서 (C1: SEQ 가 10/10).

## 8. 면접 Q&A

<details><summary>Q1. 왜 바이트마다 RXNE 인터럽트를 받지 않고 DMA + IDLE 을 쓰나요?</summary>

1 Mbaud 8N1 이면 초당 10만 바이트 = 10 µs 마다 IRQ. 진입/퇴장만 수십 사이클에 우선순위가 높은 다른 IRQ 가 10 µs 이상 막으면 RDR 이 덮여 ORE 가 난다. DMA 는 바이트 이동을 하드웨어로 하고, CPU 는 HT/TC/IDLE 때만 깬다 — 프레임 하나당 IRQ 1~2 번. IDLE 이 핵심인 이유: DMA 의 TC 는 "N 개 다 받았다" 만 아는데 프레임은 가변 길이라, "라인이 1 문자 시간 조용해졌다 = 프레임이 끝났을 가능성이 높다" 를 알려 주는 게 IDLE 이다. 꼬리질문 "IDLE 이 프레임 끝을 보장하나요?" → 아니다. 송신 쪽이 프레임 중간에 쉬면 IDLE 이 중간에 뜬다. 그래서 IDLE 은 "지금 있는 걸 파서에 넘겨라" 의 신호일 뿐이고, 경계는 파서(길이·CRC, COMM-B10/M10)가 정한다.

</details>

<details><summary>Q2. IDLE 만 쓰면 안 되나요? HT 와 TC 는 왜 필요하죠?</summary>

IDLE 은 라인이 쉴 때만 온다. GNSS 처럼 쉬지 않고 쏟아지는 스트림이나 N 보다 긴 프레임이면 IDLE 이 오기 전에 버퍼가 한 바퀴 돈다. HT·TC 가 "최소 N/2 바이트마다 한 번" 을 보장해 줘서 예산이 N/2 바이트 시간이 된다. HT 를 끄면 연속 스트림에서 매 TC 가 pos == last == 0 을 보게 돼 100% 손실이다(B8 과 demo 에서 실측). 꼬리: "HT 인터럽트도 아까우면?" → N 을 키우면 IRQ 빈도가 N/2 바이트당 1 로 줄어든다. 256 B 에 1 Mbaud 면 1.28 ms 마다 한 번 — 바이트 IRQ 의 1/128.

</details>

<details><summary>Q3. 두 이벤트 사이에 정확히 N 바이트가 오면 어떻게 되나요? 어떻게 알아채죠?</summary>

NDTR 은 같은 값으로 돌아와 pos == last. 핸들러는 "새 바이트 0" 이라 판단하고 N 바이트를 조용히 버린다 — 데이터는 버퍼에 멀쩡히 있는데도. N + k 면 가장 오래된 k 개가 덮여 N 바이트 덩어리가 사라진다. 알아채는 법: (1) HT/TC 플래그가 이번 구간으로 설명되는지 검사 — d = N 은 항상, d > N 은 균등 분포에서 1 − (N−2)/(4N) ≈ 76% 를 잡는다(전수 조사와 이론이 일치), 실제 지연 분포에서는 대부분. (2) 프레임 SEQ — 블랙아웃 10/10, 손실 프레임 수까지 정확. (3) 프레임 CRC — 덩어리가 프레임 중간을 자르면 잡힌다. (4) 하드웨어 ORE 는 이 경우를 **못** 잡는다(DMA 는 제때 읽었으니). 실무 답: 예산을 지켜 일어나지 않게 설계하고, SEQ/CRC 로 일어났음을 반드시 안다.

</details>

<details><summary>Q4. 버퍼 크기 N 은 어떻게 정하나요?</summary>

최악 지연 L_max(가장 높은 우선순위 IRQ들의 실행 시간 + 가장 긴 임계 구역 + 핸들러 자신의 처리 시간, 태스크에서 처리하면 스케줄링 지연까지)를 바이트 시간으로 바꾸고 N/2 > L_max 가 되게. 1 Mbaud 에서 L_max 가 1 ms 면 100 바이트 → N ≥ 256 (2의 거듭제곱일 필요는 없지만 HT 를 위해 짝수). 여유를 보는 지표로 `max_batch` 를 로그에 둔다 — C1 에서 60/64 는 "거의 꽉 참" 경고다. 프레임 최대 길이와 N 은 무관해도 된다: 파서가 바이트 단위 스트림으로 먹으므로 프레임이 랩 경계에 걸쳐도 된다(연습 2 의 줄 350 개가 조각에 걸쳤다). RAM 이 빠듯하면 N 을 줄이고 처리 우선순위를 올리는 trade-off.

</details>

<details><summary>Q5. 이 핸들러를 ISR 과 태스크 양쪽에서 부르면 무엇이 깨지나요?</summary>

`last` 의 read-modify-write 가 쪼개진다. 태스크가 last 를 읽고 sink 하는 동안 IRQ 가 같은 last 로 같은 바이트를 또 넘기고, 태스크는 돌아와서 오래된 pos 로 last 를 되감는다. 결정적 재현에서 15 → 30 바이트(C2). 해결: (1) 태스크 호출을 PRIMASK/BASEPRI 임계 구역으로 감싼다 — `__disable_irq/__enable_irq` 쌍은 중첩되면 안쪽 enable 이 바깥 보호를 풀어 버리니 저장/복원형으로. (2) DMA IRQ 와 USART IRQ 를 같은 우선순위로 — 같은 우선순위끼리는 선점하지 않는다. (3) 처리 문맥을 하나로 — IRQ 는 알림만, 태스크가 처리. (4) volatile 은 답이 아니다: 컴파일러 재정렬은 막아도 read-modify-write 의 원자성은 주지 않는다 (C-M14).

</details>

<details><summary>Q6. Cortex-M7 (STM32H7) 에서 이 코드를 그대로 쓰면요?</summary>

DMA 버퍼가 캐시 가능한 SRAM 에 있으면 CPU 는 지난번에 읽어 캐시에 남은 라인을 다시 본다 — 새로 쓴 바이트가 안 보이는 stale 버그. 읽기 직전에 `[last, pos)` 를 덮는 라인을 invalidate(`SCB_InvalidateDCache_by_Addr`), 버퍼는 32 B 정렬 + 32 B 배수 크기(옆 변수와 라인 공유 금지), 또는 MPU 로 non-cacheable. RX 버퍼는 DMA 시작 전에도 clean/invalidate 해 둬야 더티 라인 축출이 DMA 데이터를 덮지 않는다 — 순서 전체가 ARCH-M12 의 주제다. H7 에서는 DTCM 에 DMA1/2 가 닿지 않는 등 메모리 맵 제약도 있다(RM 확인 권장). 연습 4: invalidate 없이 844 바이트 stale.

</details>

<details><summary>Q7. STM32 HAL 로는 어떻게 쓰나요? `Size` 는 무엇인가요?</summary>

`HAL_UARTEx_ReceiveToIdle_DMA(&huart1, buf, N)` 를 DMA circular 모드로 한 번 시작하면 HT·TC·IDLE 에서 `HAL_UARTEx_RxEventCallback(huart, Size)` 가 불린다. Size 는 버퍼 안 위치 — HT 에서 N/2, TC 에서 N, IDLE 에서 N − NDTR (HAL 버전별 소스 확인 권장). 그래서 콜백 = "pos = Size (N 이면 0), last..pos 처리". 흔한 실수는 Size 를 "이번에 받은 바이트 수" 로 보는 것(연습 5). 새 버전에는 이벤트 종류를 알려 주는 `HAL_UARTEx_GetRxEventType()` 이 있다. HT 콜백이 성가셔서 `__HAL_DMA_DISABLE_IT(hdma, DMA_IT_HT)` 로 끄는 예제가 인터넷에 많은데, 그건 Q2 의 함정을 사는 것이다. LL 로는 `LL_DMA_GetDataLength()` 가 NDTR 이다.

</details>

<details><summary>Q8. 바이트가 갇히는(stuck) 버그를 들어 본 적 있나요?</summary>

"응답이 가끔 하나씩 늦게 온다 — 다음 요청을 보내야 이전 응답이 나온다." 원인 후보: (1) 플래그를 NDTR 읽은 **뒤에** 지워서, 그 사이 들어온 마지막 바이트의 IDLE 을 지워 버림(연습 3). (2) IDLE 인터럽트를 안 켜고 HT/TC 에만 의존 — 짧은 응답은 버퍼 절반을 못 채워 다음 트래픽까지 대기. (3) IDLE 플래그 클리어 방법이 틀려(F1/F4 는 SR 읽고 DR 읽기, F7/H7/G4 등은 ICR 에 IDLECF 쓰기 — RM 확인 권장) 인터럽트가 무한 반복되거나 다시 안 섬. 디버깅: 로직 애널라이저로 RX 핀과 GPIO 토글(핸들러 진입) 비교, `empty_calls`·`calls` 카운터, 마지막 바이트 이후 IDLE 진입 여부.

</details>

## 9. 추가 연습

### 연습 1 — 지연 예산: 공식과 시뮬레이션

`max_safe_latency_bytes(n, ht_irq)`, `byte_time_ns`, `wrap_time_ns`, `budget_ns` 를 짜고 N = 256 표(115200…3 Mbaud)를 출력하라. 그리고 쉬지 않는 스트림에 **고정** 지연 L 을 0 부터 늘려 첫 손실이 나는 L 을 N = 16, 64 에서 찾아 공식(N/2 − 1)과 비교하라. HT 를 끄고 지연 0 이면?

<details><summary>답</summary>

HT 켬: 안전 최대 = N/2 − 1 바이트 시간, 시간 예산 = N/2 × 바이트 시간 미만. 8N1 이면 바이트 시간 = 10/baud — 115200 에서 86.805 µs(정수 ns 로 86805), N = 256 한 바퀴 22.22 ms, 예산 11.11 ms. 시뮬레이션: N = 16 은 L = 8 에서, N = 64 는 L = 32 에서 첫 손실 → 공식과 일치. 고정 지연에서는 L < N/2 이면 실행 간격이 정확히 N/2 이고, L = N/2 가 되면 HT 실행이 TC 와 겹쳐 다음 실행까지 N 바이트 → pos == last. HT 끔 + 지연 0: 320/320, 1280/1280 전부 손실 (B8 과 같은 이유).

</details>

### 연습 2 — 줄 조립기 (AT 명령 응답, NMEA)

sink 를 줄 조립기로 바꿔라: `'\r'` 은 무시하고 `'\n'` 에서 줄 하나(LF 와 CRLF 모두), 최대 64 자, 넘으면 다음 `'\n'` 까지 버리고 `overlong++`. N = 32 DMA 에 줄 500 개(길이 0..79, 64 초과 섞임)를 랜덤 지연 ≤ 15 로 흘려 보내 결과가 정확한지, 그리고 조각 경계에 걸친 줄이 충분히 나왔는지 확인하라.

<details><summary>답</summary>

조립기는 조각 단위로 불려도 상태(`cur`, `len`, `discarding`)를 들고 있으므로 랩 경계·핸들러 경계를 신경 쓸 필요가 없다 — 이게 "바이트 스트림 파서는 연속 메모리를 요구하지 않는다" 의 실례. 실측: 20762 바이트, 정상 줄 400/400 일치, 긴 줄 100/100 버림, **두 조각 이상에 걸친 줄 350 개**. 64 자 줄 + CRLF 를 "65 자" 로 세어 버리는 실수를 피하려고 `'\r'` 을 저장하지 않는다. COMM-B14 의 라인 에디터가 이 위에 그대로 얹힌다.

</details>

### 연습 3 — 플래그 클리어 순서

`irq_wrong_order` (NDTR 읽기 → 플래그 클리어) 와 `irq_right_order` (클리어 → NDTR) 를 만들고, 두 단계 **사이에** 바이트 'C' 와 IDLE 이 들어오게 주입하라. "AB" 를 받은 IDLE IRQ 에서 시작해, 남은 IRQ 를 다 돌린 뒤 무엇이 남는지 보라.

<details><summary>답</summary>

실측: 틀린 순서 → "AB" 만 받고 'C' 는 DMA 버퍼에 있지만 IDLE 이 이미 지워져 **어떤 IRQ 도 다시 안 온다**(다음 트래픽까지 갇힘). 옳은 순서 → NDTR 이 'C' 까지 포함해 "ABC", 그리고 새 IDLE 플래그 때문에 IRQ 가 한 번 더 와서 빈 호출(`extra == 1`). 원칙: "이벤트 플래그를 먼저 지우고 상태를 읽어라 — 그러면 놓친 이벤트는 없고 중복 이벤트만 있다. 중복은 핸들러가 멱등이면 공짜다." 같은 원칙이 GPIO EXTI, 소켓 이벤트, RTOS 이벤트 그룹에도 그대로 적용된다.

</details>

### 연습 4 — Cortex-M7 D-cache (ARCH-M12 축약판)

32 B 라인, write-back · write-allocate 캐시 모델에서 DMA 는 RAM 에만 쓴다. N = 64 링을 (a) 32 B 정렬 + invalidate 없이, (b) 정렬 + 읽기 전 `[last, pos)` invalidate, (c) 16 B 어긋난 버퍼 + invalidate 로 읽고, 같은 라인의 CPU 변수(주소 0, 0x55 를 써 둠)가 살아남는지 보라.

<details><summary>답</summary>

실측: (a) 844 바이트 stale — 핸들러가 라인의 앞부분을 읽어 캐시에 올린 뒤 DMA 가 같은 라인의 뒷부분을 RAM 에 써도 CPU 는 캐시를 본다. 랩이 없어도 생긴다. (b) stale 0, 옆 변수 무사. (c) 데이터는 맞지만 **옆 변수가 0 으로 되돌아감** — invalidate 는 라인 단위라 버퍼와 라인을 공유하는 더티 변수의 쓰기를 버린다. 그래서 버퍼를 `aligned(32)` + 32 배수로. 실제 칩에서는 반대 방향(더티 라인 축출이 DMA 데이터를 덮는 것)도 있어 RX 시작 전 clean+invalidate 가 필요하다 (ARCH-M12).

</details>

### 연습 5 — HAL 식 RxEvent 콜백 어댑터

HT → `Size = N/2`, TC → `Size = N`, IDLE → `Size = N − NDTR` 로 콜백을 부르는 HAL 흉내 위에서, (a) `Size` 를 위치로 쓰는 어댑터와 (b) `Size` 바이트를 `buf[0]` 부터 넘기는 흔한 코드를 비교하라 (N = 32, 버스트 300 개).

<details><summary>답</summary>

어댑터: `rx_dma_process(rx, Size >= N ? 0 : Size)` — 이 레슨 핸들러와 같다. 실측: 이벤트 667 번, 5879 바이트 → 어댑터 5879 바이트 동일, 순진한 코드 13441 바이트(같은 바이트를 반복해서 넘김). HT 에서 Size 는 실제 위치가 아니라 "N/2" 라는 고정값이지만, 그 뒤 바이트는 다음 이벤트가 가져가므로 손실은 없다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 19/19 passed
```

```text
P1 N=256, 8N1 (10 bit/char):
   baud      byte time   wrap (N bytes)   budget HT on (< N/2 bytes)
   115200      86.81 us      22.222 ms        11.111 ms
   460800      21.70 us       5.556 ms         2.778 ms
   921600      10.85 us       2.778 ms         1.389 ms
   1000000     10.00 us       2.560 ms         1.280 ms
   3000000      3.33 us       0.853 ms         0.427 ms
P1 sim N=16: HT on → first loss at latency 8 bytes (max safe 7); HT off, latency 0 → lost 320/320
P1 sim N=64: HT on → first loss at latency 32 bytes (max safe 31); HT off, latency 0 → lost 1280/1280
P2 lines: 20762 bytes through N=32, lines ok 400/400, overlong dropped 100/100, lines split across chunks 350
P3 NDTR→clear (wrong): got "AB" of "ABC"  ← 'C' 는 DMA 버퍼에 있는데 다음 트래픽까지 아무 IRQ 도 없다
P3 clear→NDTR (right): got "ABC" of "ABC"
P4 cache: no invalidate → 844 stale bytes; aligned + invalidate → 0 stale, neighbor intact; unaligned(+16) + invalidate → 0 stale, neighbor LOST
P5 adapter (Size = pos) : 667 events, sent 5879 bytes, got 5879 bytes, stream identical
P5 naive (Size = count): 667 events, sent 5879 bytes, got 13441 bytes, stream WRONG
19/19 passed
```

## 10. 핵심 정리

- circular DMA 의 상태는 NDTR 하나: `pos = N − NDTR` (N 이면 0으로 접기), **한 번만 읽는다**. 핸들러는 `[last, pos)` 를 넘기고 `last = pos`; `pos < last` 면 `[last, N)` + `[0, pos)`.
- HT·TC·IDLE 세 IRQ → 같은 함수. IDLE = "프레임이 끝난 듯", HT/TC = "최소 N/2 바이트마다". 빈 호출은 정상(멱등).
- d = 0 과 d = N 은 같은 NDTR → 정확히 N 바이트는 조용히 사라진다. d > N 이면 N 바이트 덩어리 손실. 어떤 하드웨어 플래그(ORE 포함)도 알려 주지 않는다.
- 예산: 이벤트 뒤 **N/2 바이트 시간 미만**(처리 시간 포함). N = 256: 115200 → 11.1 ms, 1 Mbaud → 1.28 ms. 실측 문턱 N/2 − 1 과 일치. HT 끄면 연속 스트림 100% 손실.
- 검출: HT/TC 플래그 검사(d = N 은 항상, 균등 d > N 은 1 − (N−2)/(4N) ≈ 76%) + 프레임 SEQ/CRC(블랙아웃 10/10, 손실 수까지 정확). `max_batch` 로 여유를 로그.
- 플래그 클리어 → NDTR 순서(놓침 대신 중복), 태스크 호출은 PRIMASK 저장/복원 임계 구역, DMA·USART IRQ 같은 우선순위.
- M7: 읽기 전 invalidate, 버퍼 32 B 정렬·배수(ARCH-M12). HAL `RxEventCallback` 의 Size 는 위치.

## 참고자료

- STMicroelectronics, RM0090 (STM32F405/407 등) — DMA 스트림 `SxNDTR`(circular 모드에서 자동 재장전), `LISR/HISR` 의 HTIF/TCIF 와 `LIFCR/HIFCR` 클리어, USART `SR.IDLE`(클리어: SR 읽고 DR 읽기, 새 RXNE 전엔 다시 안 섬). F7/H7/G4 계열은 `ISR`/`ICR.IDLECF` — 패밀리별 RM 확인 권장.
- STMicroelectronics, STM32Cube HAL 소스 `stm32xxxx_hal_uart_ex.c` / `stm32xxxx_hal_uart.c` — `HAL_UARTEx_ReceiveToIdle_DMA`, `HAL_UARTEx_RxEventCallback`, `HAL_UARTEx_GetRxEventType` (Size 의미는 버전별 소스 확인 권장).
- Tilen Majerle, "STM32 UART DMA RX and TX" — circular DMA + IDLE + HT/TC 를 한 함수로 처리하는 널리 쓰이는 예제와 설명: <https://github.com/MaJerle/stm32-usart-uart-dma-rx-tx> (내용은 기억에 의존, 원문 확인 권장)
- ARM, *Cortex-M7 Devices Generic User Guide* / CMSIS-Core `SCB_InvalidateDCache_by_Addr` — 캐시 유지보수. 이 레슨에서는 ARCH-M12 를 따른다.
- Nordic, nRF52840 Product Specification — UARTE EasyDMA (`RXD.PTR`, `ENDRX`, `RXTO`), circular 없음 (확인 권장).
- 관련 레슨: COMM-B01 (UART 프레임), COMM-B02 (수신기·overrun), COMM-B10 (프레임 파서·재동기화), COMM-B14 (라인 에디터), COMM-M10 (견고한 파서·바이트 타임아웃), COMM-M13 (SPI 비동기 큐 — 같은 임계 구역 이야기), COMM-M14 (ARQ — 손실 뒤 재전송), ARCH-M12 (DMA 캐시 일관성), DS-B03/B04 (링 버퍼), DS-A02 (ISR↔태스크 SPSC 링), C-M14 (ISR 공유 변수), RTOS-B 레슨 (세마포·큐·태스크 알림), BM-M08 (DMA 드라이버, 다음 단계)
