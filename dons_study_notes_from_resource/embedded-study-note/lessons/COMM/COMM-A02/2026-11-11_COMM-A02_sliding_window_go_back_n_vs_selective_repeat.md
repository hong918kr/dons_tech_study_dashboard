---
id: COMM-A02
time: 120분
updated: 2026-11-11
---
# COMM-A02 슬라이딩 윈도 신뢰 전송: Go-Back-N vs Selective Repeat

## 학습 목표

- 결정적 링크 시뮬레이터(방향별 지연·지터·손실, 1 프레임/틱 링크 속도) 위에서 **Go-Back-N(GBN)** 과 **Selective Repeat(SR)** 의 송신·수신 상태 머신을 짜고, 무작위 손실·재정렬에서도 "정확히 한 번, 순서대로, 내용 그대로" 를 수만 개 메시지로 검사한다.
- 시퀀스 번호 공간 조건 **GBN: W ≤ 2^k − 1, SR: W ≤ 2^(k−1)** 을 FIFO 링크에서 증명하고, 경계 + 1 에서 "옛 메시지를 새 것으로 받는" 정확한 실패를 k = 1..6 전수로 재현한다. 재정렬 링크에서는 무엇이 더 필요한지(지연 편차 항, RTO > RTTmax) 말한다.
- goodput 을 창 크기(1..64) × 손실률(0–20 %) × RTT(4/20/80)로 측정하고 **min(1, W/RTT)**, GBN 손실 비용식, SR 상한/라운드 모델과 비교해 어디가 맞고 어디가 어긋나는지 숫자로 설명한다.
- 57600 baud · RTT 200 ms · 손실 5 % 텔레메트리 무선에 W, k, RAM 을 정하고 시뮬레이션으로 확인한다 (MCU 에서 SR 수신 버퍼가 얼마인지).

## 1. 문제

COMM-M14 의 stop-and-wait 는 메시지 하나에 RTT 하나를 쓴다. 57600 baud 무선 모뎀에서 72 바이트 프레임은 12.5 ms 인데 RTT 가 200 ms 면 링크의 1/16 = 6.25 % 밖에 못 쓴다. 면접관의 다음 질문은 정해져 있다: "**창(window)** 을 열면? 그럼 시퀀스 번호는 몇 비트 필요하죠? 손실 하나에 몇 개를 다시 보내죠? 수신 쪽 RAM 은요?" 드론 텔레메트리, 위성 링크, 무선 부트로더 업데이트(YMODEM 류 — BM-A05, 작성 예정), 칩 간 링크(PCIe/D2D 의 replay buffer)까지 같은 뼈대다.

이 레슨의 모델 (전부 이 레슨이 정한 값):

| 항목 | 정의 |
|---|---|
| 시간 | 정수 틱. **1 틱 = DATA 프레임 하나의 송신 시간** → 송신은 틱마다 프레임 ≤ 1 개 (링크 속도 = 1 프레임/틱) |
| 지연 | 방향마다 `delay + U[0, jitter]` 틱. 지터 0 이면 FIFO. 기본 10 + 10 → RTT R = 20 틱 |
| 손실 | 방향마다 `loss_ppm` (백만분율), 스크립트로 "n 번째 패킷" drop / extra delay. 손상(CRC 실패)은 손실로 본다 (COMM-M14) |
| 비행 중 상한 | 방향당 512 패킷 (넘치면 버리고 `overflow` — 모든 테스트에서 0 확인) |
| DATA / ACK | DATA = {seq: k 비트, payload: 32 비트 = msg_payload(메시지 번호)}. ACK: GBN = **다음 기대 seq (누적)**, SR = **받은 프레임의 seq (개별)** |
| 창 | W ≤ min(64, 2^k), RTO 고정 틱, 한 프레임을 `max_tx` 번 보내고도 미확인이면 FAIL (송신 정지) |
| 한 틱 순서 | (1) 도착 DATA → 수신 (ACK 를 rev 로) (2) 도착 ACK → 송신 (3) 타이머 (4) 프레임 ≤ 1 개 송신 |
| 정확성 판정 | 앱이 받은 i 번째 payload == msg_payload(i) 인지 매번 확인 → 틀리면 `wrong++`. 끝까지 확인 + N 개 + wrong 0 = ok |

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">송신 창과 수신 창 — W = 8, 메시지 #0..#17 (절대 번호), 와이어의 seq = 번호 mod 2^k</text>
  <rect x="40" y="50" width="32" height="26" class="f-mut" stroke="currentColor"/> <text x="46" y="68" fill="currentColor">#0</text> <rect x="76" y="50" width="32" height="26" class="f-mut" stroke="currentColor"/> <text x="82" y="68" fill="currentColor">#1</text>
  <rect x="112" y="50" width="32" height="26" class="f-mut" stroke="currentColor"/> <text x="118" y="68" fill="currentColor">#2</text> <rect x="148" y="50" width="32" height="26" class="f-mut" stroke="currentColor"/> <text x="154" y="68" fill="currentColor">#3</text>
  <rect x="184" y="50" width="32" height="26" class="f-mut" stroke="currentColor"/> <text x="190" y="68" fill="currentColor">#4</text> <rect x="220" y="50" width="32" height="26" class="f-acc" stroke="currentColor"/> <text x="226" y="68" fill="currentColor">#5</text>
  <rect x="256" y="50" width="32" height="26" class="f-acc" stroke="currentColor"/> <text x="262" y="68" fill="currentColor">#6</text> <rect x="292" y="50" width="32" height="26" class="f-acc" stroke="currentColor"/> <text x="298" y="68" fill="currentColor">#7</text>
  <rect x="328" y="50" width="32" height="26" class="f-acc" stroke="currentColor"/> <text x="334" y="68" fill="currentColor">#8</text> <rect x="364" y="50" width="32" height="26" class="f-acc" stroke="currentColor"/> <text x="370" y="68" fill="currentColor">#9</text>
  <rect x="400" y="50" width="32" height="26" class="f-acc" stroke="currentColor"/> <text x="406" y="68" fill="currentColor">#10</text> <rect x="436" y="50" width="32" height="26" class="f-ok" stroke="currentColor"/> <text x="442" y="68" fill="currentColor">#11</text>
  <rect x="472" y="50" width="32" height="26" class="f-ok" stroke="currentColor"/> <text x="478" y="68" fill="currentColor">#12</text> <rect x="508" y="50" width="32" height="26" fill="none" stroke="currentColor"/> <text x="514" y="68" fill="currentColor">#13</text>
  <rect x="544" y="50" width="32" height="26" fill="none" stroke="currentColor"/> <text x="550" y="68" fill="currentColor">#14</text> <rect x="580" y="50" width="32" height="26" fill="none" stroke="currentColor"/> <text x="586" y="68" fill="currentColor">#15</text>
  <rect x="616" y="50" width="32" height="26" fill="none" stroke="currentColor"/> <text x="622" y="68" fill="currentColor">#16</text> <rect x="652" y="50" width="32" height="26" fill="none" stroke="currentColor"/> <text x="658" y="68" fill="currentColor">#17</text> <g stroke="currentColor" stroke-width="1.5" fill="none">
  <path d="M218,88 V96 H502 V88"/> </g> <text x="220" y="112" fill="currentColor">창 W = 8: [base, base + W)</text> <text x="220" y="44" fill="currentColor">base=5</text> <text x="436" y="44" fill="currentColor">hi=11 (= next, GBN 은 되감으면 next &lt; hi)</text> <g font-size="12" fill="currentColor">
  <rect x="40" y="124" width="14" height="12" class="f-mut" stroke="currentColor"/><text x="60" y="134">확인됨</text> <rect x="120" y="124" width="14" height="12" class="f-acc" stroke="currentColor"/><text x="140" y="134">보냄, 미확인 (사본 보관 + 타이머)</text>
  <rect x="360" y="124" width="14" height="12" class="f-ok" stroke="currentColor"/><text x="380" y="134">보낼 수 있음</text> <rect x="480" y="124" width="14" height="12" fill="none" stroke="currentColor"/><text x="500" y="134">창 밖 — 아직 못 보냄</text> </g>
  <text x="10" y="170" fill="currentColor" font-weight="bold" font-size="13">GBN 수신: 칸 하나 — expected 만 받는다 (#7 이 빠지면 #8, #9 … 는 버림)</text>
  <rect x="40" y="180" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="46" y="197" fill="currentColor">#0</text> <rect x="76" y="180" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="82" y="197" fill="currentColor">#1</text>
  <rect x="112" y="180" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="118" y="197" fill="currentColor">#2</text> <rect x="148" y="180" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="154" y="197" fill="currentColor">#3</text>
  <rect x="184" y="180" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="190" y="197" fill="currentColor">#4</text> <rect x="220" y="180" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="226" y="197" fill="currentColor">#5</text>
  <rect x="256" y="180" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="262" y="197" fill="currentColor">#6</text> <rect x="292" y="180" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="298" y="197" fill="currentColor">#7</text>
  <rect x="328" y="180" width="32" height="24" fill="none" stroke="currentColor"/> <text x="334" y="197" fill="currentColor">#8</text> <rect x="364" y="180" width="32" height="24" fill="none" stroke="currentColor"/> <text x="370" y="197" fill="currentColor">#9</text>
  <rect x="400" y="180" width="32" height="24" fill="none" stroke="currentColor"/> <text x="406" y="197" fill="currentColor">#10</text> <rect x="436" y="180" width="32" height="24" fill="none" stroke="currentColor"/> <text x="442" y="197" fill="currentColor">#11</text>
  <rect x="472" y="180" width="32" height="24" fill="none" stroke="currentColor"/> <text x="478" y="197" fill="currentColor">#12</text> <rect x="508" y="180" width="32" height="24" fill="none" stroke="currentColor"/> <text x="514" y="197" fill="currentColor">#13</text>
  <rect x="544" y="180" width="32" height="24" fill="none" stroke="currentColor"/> <text x="550" y="197" fill="currentColor">#14</text> <rect x="580" y="180" width="32" height="24" fill="none" stroke="currentColor"/> <text x="586" y="197" fill="currentColor">#15</text>
  <rect x="616" y="180" width="32" height="24" fill="none" stroke="currentColor"/> <text x="622" y="197" fill="currentColor">#16</text> <rect x="652" y="180" width="32" height="24" fill="none" stroke="currentColor"/> <text x="658" y="197" fill="currentColor">#17</text>
  <text x="292" y="222" fill="currentColor">expected=7 → ACK(seq 7) 를 계속 (누적 ACK = "다음에 기대하는 것")</text>
  <text x="10" y="252" fill="currentColor" font-weight="bold" font-size="13">SR 수신: 창 [rcv_base, rcv_base + W) 를 버퍼링, 개별 ACK, 구멍이 메워지면 연속 구간을 한꺼번에 전달</text>
  <rect x="40" y="262" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="46" y="279" fill="currentColor">#0</text> <rect x="76" y="262" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="82" y="279" fill="currentColor">#1</text>
  <rect x="112" y="262" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="118" y="279" fill="currentColor">#2</text> <rect x="148" y="262" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="154" y="279" fill="currentColor">#3</text>
  <rect x="184" y="262" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="190" y="279" fill="currentColor">#4</text> <rect x="220" y="262" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="226" y="279" fill="currentColor">#5</text>
  <rect x="256" y="262" width="32" height="24" class="f-mut" stroke="currentColor"/> <text x="262" y="279" fill="currentColor">#6</text> <rect x="292" y="262" width="32" height="24" class="f-warn" stroke="currentColor"/> <text x="298" y="279" fill="currentColor">#7</text>
  <rect x="328" y="262" width="32" height="24" class="f-ok" stroke="currentColor"/> <text x="334" y="279" fill="currentColor">#8</text> <rect x="364" y="262" width="32" height="24" class="f-ok" stroke="currentColor"/> <text x="370" y="279" fill="currentColor">#9</text>
  <rect x="400" y="262" width="32" height="24" fill="none" stroke-dasharray="4,3" stroke="currentColor"/> <text x="406" y="279" fill="currentColor">#10</text> <rect x="436" y="262" width="32" height="24" class="f-ok" stroke="currentColor"/> <text x="442" y="279" fill="currentColor">#11</text>
  <rect x="472" y="262" width="32" height="24" fill="none" stroke-dasharray="4,3" stroke="currentColor"/> <text x="478" y="279" fill="currentColor">#12</text> <rect x="508" y="262" width="32" height="24" fill="none" stroke-dasharray="4,3" stroke="currentColor"/> <text x="514" y="279" fill="currentColor">#13</text>
  <rect x="544" y="262" width="32" height="24" fill="none" stroke-dasharray="4,3" stroke="currentColor"/> <text x="550" y="279" fill="currentColor">#14</text> <rect x="580" y="262" width="32" height="24" fill="none" stroke="currentColor"/> <text x="586" y="279" fill="currentColor">#15</text>
  <rect x="616" y="262" width="32" height="24" fill="none" stroke="currentColor"/> <text x="622" y="279" fill="currentColor">#16</text> <rect x="652" y="262" width="32" height="24" fill="none" stroke="currentColor"/> <text x="658" y="279" fill="currentColor">#17</text> <g stroke="currentColor" stroke-width="1.5" fill="none">
  <path d="M290,292 V300 H574 V292"/> </g> <text x="10" y="318" fill="currentColor">rcv_base=7: #8 #9 #11 은 버퍼에 (초록), 점선 칸은 아직 빈 칸. rcv_base 앞 W 칸 = "옛 것" → ACK 만 다시</text>
</svg>
```

### Part A — GBN 과 SR 의 핵심 메커니즘

1. **A1** `gbn_rx_on_data()`: seq == expected 면 앱에 넘기고 expected++, 아니면 버림. **항상** ACK(expected).
2. **A2** `gbn_tx_on_ack()`: d = (ack − base) mod 2^k 가 1..(hi − base) 이면 base += d, `next < base` 면 next = base, 타이머 정지/재시작.
3. **A3** `gbn_tx_timeout()`: base 를 `max_tx` 번 보냈으면 FAIL, 아니면 **next = base** (되감기) + 타이머 재시작.
4. **A4** `sr_rx_on_data()`: d = (seq − rcv_base) mod 2^k. d < W → 버퍼 + ACK(seq) + 연속 구간 전달, d ≥ 2^k − W → "옛 것" → ACK(seq) 만, 나머지 무시.
5. **A5** `sr_tx_on_ack()`: d = (seq − base) mod 2^k < hi − base 면 그 프레임 확인, base 를 확인된 것 너머로 민다.
6. **A6** `sr_tx_timeouts()`: 프레임별 타이머 만료 → `max_tx` 면 FAIL, 아니면 재전송 대기열에 (송신 단계가 오래된 것부터 하나씩 보낸다).
7. 테스트: 손실 없는 링크 64 구성(W 1..32 × 2)의 **완료 틱이 공식과 정확히 일치**, k = 3 으로 seq 가 125 바퀴 이상 도는 1000 메시지, DATA #3 손실(GBN 은 창 전체 8 개, SR 은 1 개), ACK #3 손실(GBN 0 개, SR 1 개), 되감기 함정.

### Part B — 시퀀스 공간, 결함, 증명

| # | 주입 | 기대 결과 (테스트로 단언) |
|---|---|---|
| B1 | 첫 창의 ACK 전부 손실, k = 1..6 × W = 1..2^k × GBN/SR (252 경우) | 정상 ⇔ GBN W ≤ 2^k − 1, SR W ≤ 2^(k−1). 경계 + 1 은 6 개 k 모두 `wrong > 0` |
| B2 | SR k = 3, W = 5 트레이스 / W = 4 | W = 5: 앱이 #8, #9 를 옛 내용으로 받음 (wrong = 2). W = 4: 재전송 4 개 전부 "옛 것" (rx_old = 4) |
| B3 | 무작위 손실 5/10/15 %, FIFO, **경계 W**, RTO 12 (< RTT!) 또는 30 — 48 실행 | 위반 0. 같은 조건에서 W + 1 은 35/48 실행 실패 |
| B4 | 재정렬 (지연 5..15/방향, 손실 10 %, RTO 12) | FIFO 공식의 k 로는 GBN 8/24, SR 24/24 실패. `seq_bits_needed()` 의 k 로는 0 |
| B5 | DATA 100 % 손실 / ACK 100 % 손실 | 정확히 W·max_tx = 40 프레임, t = max_tx·RTO = 150 에 FAIL / FAIL 인데 첫 창 8 개는 이미 전달 |
| B6 | 무작위 스트레스: 지터 0/4/8, 손실 0–10 %, W 4..32, k = 8 — 24 실행 × 3000 | 전부 ok, **확인된 프레임을 다시 보낸 횟수 0**, SR 재전송 수 == 잃은 DATA + 잃은 ACK (정확히) |

### Part C — 텔레메트리 무선 링크 설계

1. **C1** `link_plan()`: 57600 baud 8N1, 72 B 프레임(payload 64 B), RTT 200 ms → 프레임 시간(올림), R(틱), RTO = R + ⌈R/2⌉, SR 창 W = R + RTO, 2^k ≥ 2W, RAM (송신 사본 W × 64 B, SR 수신 버퍼 W × 64 B).
2. 손실 5 % 링크로 W = 8/16/32/40 의 GBN·SR goodput (B/s) 을 시뮬레이션하고, 계획한 W 의 SR 이 이론 상한 q·line 의 85 % 이상인지 확인.
3. `demo_window.c`: goodput 곡선 3 세트(RTT 4/20/80) + 이론, 재정렬에서 k 별 실패 수, 텔레메트리 W 별 B/s 와 RAM.

## 2. Problem (English)

> **Sliding-window ARQ over a long, lossy link.** A telemetry radio link carries frames at one frame per tick; each direction has a propagation delay (optionally jittered, so frames can be reordered) and independent loss. DATA frames carry a k-bit sequence number. Implement both:
>
> ```c
> /* Go-Back-N: cumulative ACK = next expected seq, one timer, rewind on timeout */
> void gbn_rx_on_data(win_rx *r, uint32_t now, uint16_t seq, uint32_t payload);
> void gbn_tx_on_ack (win_tx *t, uint32_t now, uint16_t ack);
> void gbn_tx_timeout(win_tx *t, uint32_t now);
> /* Selective Repeat: per-frame ACK and timer, receiver buffers out-of-order frames within its window */
> void sr_rx_on_data (win_rx *r, uint32_t now, uint16_t seq, uint32_t payload);
> void sr_tx_on_ack  (win_tx *t, uint32_t now, uint16_t seq);
> void sr_tx_timeouts(win_tx *t, uint32_t now);
> uint8_t seq_bits_needed(proto_t p, uint32_t W, const chan_cfg *fwd, const chan_cfg *rev);
> ```
>
> Requirements: the application receives every message exactly once, in order, with the right content, under random loss and reordering; a frame that is still unacknowledged after `max_tx` transmissions makes the sender report FAILED (bounded failure); no dynamic allocation (W ≤ 64 slots).
>
> Example (W = 8, RTT 20, RTO 22, DATA #3 lost): GBN retransmits #3..#10 (8 frames) at t = 44 and the receiver discards #4..#10; SR retransmits only #3 at t = 25 and delivers #3..#10 at once at t = 35.
>
> Follow-ups: (1) Prove that GBN needs W ≤ 2^k − 1 and SR needs W ≤ 2^(k−1); show the exact failure one past the bound. (2) What changes if the link reorders? (3) Plot goodput vs. W for several loss rates; when does a bigger window stop helping GBN? (4) Pick W, k and the receive-buffer RAM for a 57600-baud radio with 200 ms RTT and 5 % loss.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여덟 개: A1–A3 (GBN), A4–A6 (SR), B1 `seq_bits_needed`, C1 `link_plan`. 링크 시뮬레이터(도착 시각 힙, 스크립트 결함), 송신 단계(`gbn_tx_poll`/`sr_tx_poll` 의 "프레임 하나 보내기"), `tx_frame`/`rx_deliver`/`rx_send_ack`, 실행기 `run_sim()`, 하네스는 주어진다. 스텁은 "아무것도 안 받음 + 첫 타임아웃에 바로 FAIL" 이라 모든 실행이 금방 끝난다. 처음 실행 (실제 출력, 트레이스 줄 생략):

```text
FAIL starter.c:316  bad == 0
FAIL starter.c:319  r.ok && r.ticks == clean_finish(1000, 4, 20)
FAIL starter.c:322  r.ok && r.ticks == clean_finish(1000, 7, 20)
A  clean RTT 20: W=1 4000 ticks, W=5 804, W=20 219, W=32 219 (200 msgs)
…
2/36 passed
```

(exit 1.) 단계별 확인: GBN(A1–A3)만 채우면 **10/36**, SR(A4–A6)까지 **24/36**, B1 까지 **30/36**, C1 까지 **36/36**.

## 4. 힌트

<details><summary>힌트 1 — 절대 번호로 생각하고, 와이어에서만 mod 2^k</summary>

송신은 `base, next, hi` 를 32 비트 **절대 번호**로 들고, 프레임에 실을 때만 `idx & mask`. 수신도 `expected`(GBN) / `rcv_base`(SR) 를 절대 번호로 둔다. 받은 seq 를 절대 번호로 되돌리는 유일한 방법이 **"기준점과의 거리" d = (seq − 기준) mod 2^k** 이다. 시퀀스 공간 조건은 전부 "이 d 가 모호하지 않으려면?" 이라는 질문이다.

</details>

<details><summary>힌트 2 — GBN 수신은 세 줄</summary>

seq 가 `expected & mask` 와 같으면 넘기고 expected++. 다르면 버린다 (순서 밖 = 앞 프레임이 빠졌거나 옛 재전송). 그리고 **어느 경우든** `ACK(expected & mask)`. 순서 밖 프레임에도 ACK 를 보내야 송신이 "지금 어디까지 받았는지" 를 계속 알 수 있다 (같은 값이 반복되는 duplicate ACK).

</details>

<details><summary>힌트 3 — GBN 송신의 ACK 범위는 next 가 아니라 hi</summary>

되감기(next = base) 직후에도 링크에는 되감기 **전**에 보낸 프레임들이 날아가고 있고, 그 ACK 는 next 보다 앞을 확인할 수 있다. 그래서 유효 범위는 1 ≤ d ≤ hi − base (hi = 보낸 최대 + 1). base 가 next 를 앞지르면 next = base 로 당긴다 — 안 그러면 이미 확인된 프레임을 다시 보낸다. 타이머: base 가 전진하면 재시작, base == hi 면 정지.

</details>

<details><summary>힌트 4 — SR 수신의 세 구역</summary>

d < W 는 지금 창 (버퍼에 넣고 ACK). d ≥ 2^k − W 는 바로 앞 창 = 이미 넘긴 것의 재전송 → 내 ACK 가 사라졌다는 뜻이니 **ACK 만 다시**. 나머지는 이 설계에서 올 수 없는 값(무시). 넣은 뒤에는 `while (have[rcv_base])` 로 연속 구간을 전부 넘긴다 — `if` 하나면 구멍이 메워져도 하나만 나간다.

</details>

<details><summary>힌트 5 — 증명은 "송신이 i 를 보낼 때 어디까지 보낼 수 있었나"</summary>

i 가 미확인일 때 보낸 사본이면 그 순간 hi ≤ base + W ≤ i + W. FIFO 고정 지연이면 그 사본이 도착할 때 수신이 받은 것은 정확히 그때까지 보낸 것들이다 → expected (또는 rcv_base) ≤ i + W. GBN 은 "expected ≡ i (mod 2^k), expected > i" 가 불가능하면 되고, SR 은 "지금 창" 과 "앞 창" 두 구역이 겹치지 않으면 된다. 재정렬이 있으면 도착까지 (지연 편차) 틱 동안 송신이 더 보낸 프레임 수가 더해진다.

</details>

<details><summary>힌트 6 — link_plan 은 올림 나눗셈만 조심</summary>

frame_us = ⌈bytes · 10 · 10^6 / baud⌉, R = ⌈rtt_us / frame_us⌉ (틱 = 프레임 시간), RTO = R + ⌈R/2⌉, W = R + RTO (첫 손실을 복구하는 동안에도 쉬지 않고 보내려면 base 가 묶인 시간 R + RTO 만큼의 프레임이 창에 들어가야 한다), 2^k ≥ 2W 인 최소 k. 64 비트로 곱하고 나눈다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "stop-and-wait 는 1 프레임/RTT. 대역폭 × 지연 곱(BDP) 만큼 창을 열어야 링크를 채운다. 손실이 없으면 goodput = min(1, W/RTT)."
2. 두 설계: **GBN** = 수신 버퍼 없음 + 누적 ACK + 타이머 하나 + 되감기. **SR** = 수신 버퍼 W 칸 + 개별 ACK + 프레임별 타이머.
3. 대가: GBN 은 손실 하나에 창 전체를 다시 보낸다(airtime), SR 은 RAM 과 타이머 W 개.
4. seq 공간: GBN W ≤ 2^k − 1, SR W ≤ 2^(k−1) — 증명 한 줄 + 실패 예.
5. 숫자: 손실·RTT 별 goodput 곡선, 그리고 실제 링크에 W, k, RAM 을 정하는 법.

### Part A — 손실 없는 링크: 정확히 min(1, W/RTT)

틱 순서상 프레임을 t 에 보내면 ACK 는 t + R 에 와서 **같은 틱에** 다음 프레임을 내보낸다. 그래서 W ≤ R 이면 "W 개 보내고 R 틱마다 반복", W ≥ R 이면 매 틱 하나: 프레임 i 의 송신 시각은 ⌊i/W⌋·R + i mod W, 마지막 ACK 는 거기에 + R. `clean_finish()` 가 이 식이고, W = 1..32 × GBN/SR 64 구성이 전부 정확히 일치했다 (N = 200, RTT 20: W = 1 → 4000 틱, W = 5 → 804, W ≥ 20 → 219). **W = RTT 가 BDP** — 그 이상은 손실 없는 링크에서는 아무것도 더 주지 않는다.

### Part A — 손실 하나: GBN 은 창 전체, SR 은 하나 (그림 = 실제 트레이스)

```svg
<svg viewBox="0 0 760 375" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a02a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="34" fill="currentColor" font-weight="bold" font-size="13">(a) GBN, W = 8, RTT 20, RTO 22 — DATA #3 손실 (solution.c 트레이스 그대로, 1 틱 = 7.5 px)</text>
  <g stroke="currentColor"><line x1="60" y1="70" x2="720" y2="70"/><line x1="60" y1="130" x2="720" y2="130"/></g> <text x="10" y="74" fill="currentColor">송신</text><text x="10" y="134" fill="currentColor">수신</text> <rect x="68.0" y="58" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="75.5" y="58" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="83.0" y="58" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="90.5" y="58" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="98.0" y="58" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="105.5" y="58" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="113.0" y="58" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="120.5" y="58" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="218.0" y="58" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="225.5" y="58" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="233.0" y="58" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="548.0" y="58" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="398.0" y="58" width="5" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <rect x="405.5" y="58" width="5" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="413.0" y="58" width="5" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="420.5" y="58" width="5" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <rect x="428.0" y="58" width="5" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="435.5" y="58" width="5" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="443.0" y="58" width="5" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <rect x="450.5" y="58" width="5" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <circle cx="145.0" cy="136" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/> <circle cx="152.5" cy="136" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/>
  <circle cx="160.0" cy="136" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/> <circle cx="475.0" cy="136" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/> <circle cx="482.5" cy="136" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/>
  <circle cx="490.0" cy="136" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/> <circle cx="497.5" cy="136" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/> <circle cx="505.0" cy="136" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/>
  <circle cx="512.5" cy="136" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/> <circle cx="520.0" cy="136" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/> <circle cx="527.5" cy="136" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/>
  <circle cx="625.0" cy="136" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/> <rect x="173.0" y="132" width="5" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="180.5" y="132" width="5" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <rect x="188.0" y="132" width="5" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="195.5" y="132" width="5" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="293.0" y="132" width="5" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <rect x="300.5" y="132" width="5" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <rect x="308.0" y="132" width="5" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <g stroke="currentColor" stroke-width="1.4" fill="none">
  <line x1="400.0" y1="70" x2="475.0" y2="128" marker-end="url(#a02a)"/> <line x1="92.5" y1="70" x2="130.0" y2="100.0"/> </g>
  <text x="126.0" y="105.0" fill="currentColor" font-size="14" font-weight="bold">✕</text>
  <line x1="700.0" y1="52" x2="700.0" y2="144" stroke="currentColor" stroke-dasharray="3,3"/> <text x="86" y="54" fill="currentColor">#3</text> <text x="330" y="54" fill="currentColor">t=44 타임아웃 → #3..#10 전부 (8 개)</text> <text x="150" y="158" fill="currentColor">#4..#10 도착했지만 버림 (7 개)</text>
  <text x="545" y="54" fill="currentColor">#11</text> <text x="640" y="158" fill="currentColor">t=84 끝</text>
  <text x="10" y="204" fill="currentColor" font-weight="bold" font-size="13">(b) SR, 같은 링크 — #3 만 다시, #4..#10 은 버퍼에서 기다렸다가 t=35 에 8 개 한꺼번에 전달</text>
  <g stroke="currentColor"><line x1="60" y1="240" x2="720" y2="240"/><line x1="60" y1="300" x2="720" y2="300"/></g> <text x="10" y="244" fill="currentColor">송신</text><text x="10" y="304" fill="currentColor">수신</text> <rect x="68.0" y="228" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="75.5" y="228" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="83.0" y="228" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="90.5" y="228" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="98.0" y="228" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="105.5" y="228" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="113.0" y="228" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="120.5" y="228" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="218.0" y="228" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="225.5" y="228" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="233.0" y="228" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="405.5" y="228" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="255.5" y="228" width="5" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
  <circle cx="145.0" cy="306" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/> <circle cx="152.5" cy="306" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/> <circle cx="160.0" cy="306" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/>
  <circle cx="332.5" cy="306" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/> <circle cx="482.5" cy="306" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/> <rect x="173.0" y="302" width="5" height="8" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="180.5" y="302" width="5" height="8" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="188.0" y="302" width="5" height="8" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="195.5" y="302" width="5" height="8" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="293.0" y="302" width="5" height="8" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="300.5" y="302" width="5" height="8" class="f-ok" stroke="currentColor" stroke-width="0.5"/> <rect x="308.0" y="302" width="5" height="8" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <g stroke="currentColor" stroke-width="1.4" fill="none"> <line x1="257.5" y1="240" x2="332.5" y2="298" marker-end="url(#a02a)"/> <line x1="92.5" y1="240" x2="130.0" y2="270.0"/> </g>
  <text x="126.0" y="275.0" fill="currentColor" font-size="14" font-weight="bold">✕</text>
  <line x1="557.5" y1="222" x2="557.5" y2="314" stroke="currentColor" stroke-dasharray="3,3"/> <text x="86" y="224" fill="currentColor">#3</text> <text x="252" y="224" fill="currentColor">t=25 #3 만</text> <text x="150" y="328" fill="currentColor">#4..#10 은 버퍼로</text>
  <text x="330" y="328" fill="currentColor">t=35 #3..#10 한꺼번에 전달 (버퍼 최대 8 = W 칸)</text> <text x="402" y="224" fill="currentColor">#11</text> <text x="520" y="224" fill="currentColor">t=65 끝 (GBN 보다 19 틱 빠름)</text> <g font-size="12" fill="currentColor">
  <rect x="60" y="352" width="5" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/><text x="70" y="362">새 프레임</text> <rect x="150" y="352" width="5" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/><text x="160" y="362">재전송</text>
  <circle cx="232" cy="358" r="3" class="f-ok" stroke="currentColor" stroke-width="0.6"/><text x="240" y="362">앱으로 전달</text> <rect x="330" y="354" width="5" height="8" class="f-warn" stroke="currentColor" stroke-width="0.5"/><text x="340" y="362">순서 밖 → 버림 (GBN)</text>
  <rect x="490" y="354" width="5" height="8" class="f-ok" stroke="currentColor" stroke-width="0.5"/><text x="500" y="362">순서 밖 → 버퍼 (SR)</text> </g>
</svg>
```

`solution.c` 가 찍은 GBN 트레이스 (DATA #3 손실, 중간 생략):

```text
  t=   3  TX       #3 seq=3
  t=  12  RX deliver    #2
  t=  20  TX       #8 seq=8
  t=  22  TX       #10 seq=10
  t=  44  TIMEOUT       go back to #3 (8 개 재전송 예정)
  t=  44  TX RETX  #3 seq=3
  …
  t=  51  TX RETX  #10 seq=10
  t=  54  RX deliver    #3
```

타임아웃이 t = 3 + 22 = 25 가 아니라 **44** 인 것이 중요하다. GBN 의 타이머는 "가장 오래된 미확인 프레임" 용이고 **base 가 전진할 때 다시 시작**한다 (교과서 FSM, TCP 의 RFC 6298 5.3 과 같은 규칙). #2 의 ACK 가 t = 22 에 와서 base 가 #3 이 되는 순간 타이머가 22 + 22 = 44 로 다시 잡힌다. 그래서 GBN 은 손실 하나에 **R − 1 + RTO 틱**을 잃는다 (이 식이 아래 goodput 이론의 핵심; 연습 1 은 타이머를 "base 를 보낸 시각 + RTO" 로 바꿔 RTO 만 잃게 만든다).

| DATA #3 손실 (W = 8, N = 12) | GBN | SR |
|---|---|---|
| 재전송 프레임 | 8 (#3..#10 = 미확인 전부) | 1 (#3) |
| 수신이 버린 프레임 | 7 (#4..#10) | 0 (버퍼 최대 8 칸 = W) |
| 완료 틱 | 84 | 65 |

**ACK 하나 손실** (rev 의 3 번째 패킷)은 반대 결과: GBN 은 다음 누적 ACK(값 5)가 #4 까지 덮으므로 재전송 0, 타임아웃 0. SR 은 #3 의 ACK 가 유일한 증거라 #3 이 타임아웃 → 재전송 → 수신은 "앞 창(옛 것)" 으로 보고 ACK 만 다시 (`rx_old = 1`). **개별 ACK 는 ACK 손실에 약하다** — 아래 goodput 표에서 작은 창일 때 GBN 이 SR 을 이기는 이유이고, 실제 프로토콜이 SR 에 누적 ACK + SACK 비트맵을 붙이는 이유다 (연습 2).

### Part A — 되감기 함정: ACK 범위는 hi 기준

ACK #0..#3 을 늦춰 t = 24 에 한꺼번에 오게 하면 (RTO 22 라 t = 22 에 이미 되감음):

```text
  t=  22  TIMEOUT       go back to #0 (8 개 재전송 예정)
  t=  22  TX RETX  #0 seq=0
  t=  23  TX RETX  #1 seq=1
  t=  24  TX RETX  #5 seq=5
  t=  25  TX RETX  #6 seq=6
  t=  26  TX RETX  #7 seq=7
```

t = 24 에 ACK(1)..ACK(5) 가 오면 base = 5 인데 next = 2 다. 범위를 `next − base` 로 검사하면 ACK(3..5) 를 버리고, `next < base → next = base` 를 빼먹으면 이미 확인된 #2..#4 를 다시 보낸다 (재전송 수는 똑같이 5 라서 재전송 수만 세는 테스트로는 못 잡는다 — 그래서 `sent_acked == 0` 불변식을 따로 단언한다). RTO 15 < RTT 20 으로 쓸데없는 타임아웃이 300 메시지 동안 38 번 나도 결과는 정확했다 (`retx=300 frames=600`) — **RTO 가 짧으면 비효율이지 오류가 아니다** (FIFO 링크에서는; 재정렬이 끼면 아래 B4).

### Part B — 시퀀스 공간 조건: 증명

기호: 송신 `base ≤ next ≤ hi ≤ base + W`, 수신 `e` (GBN 의 expected, SR 의 rcv_base). 항상 base ≤ e (확인된 것은 받은 것). FIFO 고정 지연 링크를 가정한다.

**보조 정리.** 송신이 프레임 i 의 사본을 시각 s 에 보냈다면 그때 i 는 미확인이므로 hi(s) ≤ base(s) + W ≤ i + W. FIFO 고정 지연이면 그 사본이 도착하는 순간 수신이 받은 프레임은 정확히 s 이전에 보낸 것들이다 → **e ≤ i + W**. 또 e ≥ base(s) > i − W.

- **GBN 수신:** 옛 사본을 새 것으로 받으려면 e ≡ i (mod 2^k) 이면서 e > i, 즉 e ≥ i + 2^k. 보조 정리로 e ≤ i + W 이므로 **W < 2^k 면 불가능**.
- **GBN 송신:** FIFO 에서 ACK 값은 만들어진 순서대로(단조 증가) 도착하므로 도착한 값 A 는 A ≥ base, A ≤ hi. d = A − base ∈ [0, W] 가 mod 2^k 에서 모호하지 않으려면 W ≤ 2^k − 1.
- **SR 수신:** 사본 i 의 d = (i − e) mod 2^k. i ≥ e 면 i − e < W (보조 정리) → d ∈ [0, W). i < e 면 e − i ∈ [1, W] → d ∈ [2^k − W, 2^k). 두 구역 "지금 창" 과 "앞 창" 이 겹치지 않을 조건이 **2W ≤ 2^k**. 겹치면 앞 창의 재전송이 지금 창의 새 프레임으로 둔갑한다.
- **SR 송신:** j 의 낡은 ACK 가 아직 살아 있는 동안 송신이 보낼 수 있는 최대 번호는 j + 2W − 1 (j 가 확인되기 전엔 j + W − 1 까지, 확인 뒤 한 RTT 안에는 다음 창을 열어 줄 ACK 가 아직 없다 — 고정 지연). 그래서 2^k ≥ 2W 면 j + 2^k 는 미확인 구간에 없다.

**필요성 (경계 + 1 의 실패).** 첫 창 #0..#W−1 이 전부 도착하고 그 ACK 가 전부 손실되면 수신은 e = W 로 앞서 가고 송신은 seq 0.. 을 재전송한다. GBN W = 2^k: e = 2^k ≡ 0 → 재전송 #0 을 #2^k 로 받는다. SR W = 2^(k−1) + 1: d = (0 − W) mod 2^k = 2^k − W < W → 지금 창으로 받는다. B1 은 이 시나리오를 k = 1..6, W = 1..2^k 전부(252 경우)에 돌려 **"정상 ⇔ 조건 만족" 이 252/252** 였다.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">SR, k = 3 (seq 0..7): 첫 창 #0..#W−1 은 전부 도착, 그 ACK 는 전부 손실 → 송신은 seq 0.. 을 재전송</text>
  <text x="80" y="38" fill="currentColor" font-weight="bold">W = 4 = 2^(k−1): 안전</text>
  <circle cx="190" cy="150" r="78" fill="none" stroke="currentColor" stroke-dasharray="2,4"/> <circle cx="190.0" cy="72.0" r="17" class="f-mut" stroke="currentColor"/> <text x="186.0" y="77.0" fill="currentColor" font-size="14">0</text> <circle cx="245.2" cy="94.8" r="17" class="f-mut" stroke="currentColor"/>
  <text x="241.2" y="99.8" fill="currentColor" font-size="14">1</text> <circle cx="268.0" cy="150.0" r="17" class="f-mut" stroke="currentColor"/> <text x="264.0" y="155.0" fill="currentColor" font-size="14">2</text> <circle cx="245.2" cy="205.2" r="17" class="f-mut" stroke="currentColor"/>
  <text x="241.2" y="210.2" fill="currentColor" font-size="14">3</text> <circle cx="190.0" cy="228.0" r="17" class="f-ok" stroke="currentColor"/> <text x="186.0" y="233.0" fill="currentColor" font-size="14">4</text> <circle cx="134.8" cy="205.2" r="17" class="f-ok" stroke="currentColor"/>
  <text x="130.8" y="210.2" fill="currentColor" font-size="14">5</text> <circle cx="112.0" cy="150.0" r="17" class="f-ok" stroke="currentColor"/> <text x="108.0" y="155.0" fill="currentColor" font-size="14">6</text> <circle cx="134.8" cy="94.8" r="17" class="f-ok" stroke="currentColor"/>
  <text x="130.8" y="99.8" fill="currentColor" font-size="14">7</text> <text x="40" y="262" fill="currentColor">새 창 {4,5,6,7} (초록) 과 옛 창 {0,1,2,3} (회색) 이</text> <text x="40" y="279" fill="currentColor">겹치지 않는다 → 재전송 seq 0..3 은 "옛 것" → ACK 만</text> <text x="40" y="296" fill="currentColor">(solution.c: rx_old = 4, 정상 완료)</text>
  <text x="450" y="38" fill="currentColor" font-weight="bold">W = 5 &gt; 2^(k−1): 깨짐</text>
  <circle cx="560" cy="150" r="78" fill="none" stroke="currentColor" stroke-dasharray="2,4"/> <circle cx="560.0" cy="72.0" r="17" class="f-warn" stroke="currentColor"/> <text x="556.0" y="77.0" fill="currentColor" font-size="14">0</text> <circle cx="615.2" cy="94.8" r="17" class="f-warn" stroke="currentColor"/>
  <text x="611.2" y="99.8" fill="currentColor" font-size="14">1</text> <circle cx="638.0" cy="150.0" r="17" class="f-mut" stroke="currentColor"/> <text x="634.0" y="155.0" fill="currentColor" font-size="14">2</text> <circle cx="615.2" cy="205.2" r="17" class="f-mut" stroke="currentColor"/>
  <text x="611.2" y="210.2" fill="currentColor" font-size="14">3</text> <circle cx="560.0" cy="228.0" r="17" class="f-mut" stroke="currentColor"/> <text x="556.0" y="233.0" fill="currentColor" font-size="14">4</text> <circle cx="504.8" cy="205.2" r="17" class="f-ok" stroke="currentColor"/>
  <text x="500.8" y="210.2" fill="currentColor" font-size="14">5</text> <circle cx="482.0" cy="150.0" r="17" class="f-ok" stroke="currentColor"/> <text x="478.0" y="155.0" fill="currentColor" font-size="14">6</text> <circle cx="504.8" cy="94.8" r="17" class="f-ok" stroke="currentColor"/>
  <text x="500.8" y="99.8" fill="currentColor" font-size="14">7</text> <text x="410" y="262" fill="currentColor">새 창 {5,6,7,0,1} ∩ 옛 창 {0..4} = {0,1} (주황)</text> <text x="410" y="279" fill="currentColor">→ 재전송 seq 0, 1 을 #8, #9 로 버퍼에 넣음</text> <text x="410" y="296" fill="currentColor">→ t=64 앱이 #8, #9 를 옛 내용으로 받음 (wrong=2)</text>
</svg>
```

B2 트레이스 (SR, k = 3, W = 5 — 실제 출력 발췌):

```text
  t=  14  RX deliver    #4
  t=  32  TX RETX  #0 seq=0          ← 수신 창 {5,6,7,0,1}: d = 3 → "#8" 로 버퍼에
  t=  33  TX RETX  #1 seq=1          ← d = 4 → "#9" 로 버퍼에
  t=  34  TX RETX  #2 seq=2          ← d = 5 ≥ 8 − 5 → 옛 것, ACK 만
  t=  55  TX       #8 seq=0
  t=  64  RX deliver    #7
  t=  64  RX deliver    #8  <-- 내용 불일치!
  t=  64  RX deliver    #9  <-- 내용 불일치!
```

**조용한 손상**이다: 송신은 모든 ACK 를 정상으로 받았고 (seq 0, 1 의 ACK 는 송신 입장에서는 #0, #1 의 것), 진짜 #8, #9 는 도착했을 때 "이미 버퍼에 있음" 으로 버려진다. CRC 도 통과한다 — 프레임은 멀쩡하고 **번호 해석**이 틀렸기 때문이다. 이 레슨은 payload 에 메시지 번호의 해시를 넣어 앱에서 잡았지만, 실제 링크에서는 이것이 "가끔 센서 값이 과거로 튄다" 로 보인다.

**무작위 손실 (B3).** 경계 W (GBN 2^k − 1, SR 2^(k−1)), k = 2..5, 손실 5–15 %, RTO 12 (RTT 보다 짧음) 또는 30, 48 실행·28800 메시지·62667 프레임에서 위반 0. 같은 시드로 W + 1 은 48 중 35 실행이 실패했다 — 경계 + 1 의 실패는 "모든 ACK 손실" 같은 극단이 아니어도 흔하다 (SR k = 3, W = 5 면 #i 의 ACK 하나만 잃고 #i+1..#i+3 이 먼저 도착하면 충분).

### Part B — 재정렬이 끼면

지터가 있으면 보조 정리의 "도착 순간 수신이 받은 것 = s 이전에 보낸 것" 이 깨진다. 사본 i 는 최대 s + dmax_f 에 도착하는데, 수신이 받은 다른 프레임은 그 시각 − dmin_f 까지 보낸 것일 수 있다. 그 J_f = dmax_f − dmin_f 틱 동안 송신은 (1 프레임/틱이므로) 최대 J_f 개를 더 보낸다 → **e ≤ i + W + J_f**. 낡은 ACK 는 만든 사본을 보낸 뒤 최대 RTTmax 틱 살아 있고 그동안 송신은 최대 RTTmax 개를 더 보낸다. 그래서 `seq_bits_needed()` 의 충분조건:

| | FIFO (지터 0) | 재정렬 (속도 상한으로 증명한 충분조건) |
|---|---|---|
| GBN | 2^k ≥ W + 1 | 2^k ≥ max(W + 1 + J_f, W + RTTmax + 1) |
| SR | 2^k ≥ 2W | 2^k ≥ max(2W + J_f, W + RTTmax) |

B4 (지연 5..15/방향, 손실 10 %, RTO 12 = 쓸데없는 재전송 다수, W = 2/4/8 × 시드 8): FIFO 공식의 k 로는 **GBN 8/24, SR 24/24 실행 실패**, `seq_bits_needed` 의 k 로는 0/24. `demo_window.c` 3번은 W = 8 에서 k 를 3..7 로 훑는다 (실패한 실행 수 / 20, 손실 10 %):

| 링크 (지연/방향) | RTO | 필요 k (공식) | GBN k=3 / 4 / 5 | SR k=3 / 4 / 5 |
|---|---|---|---|---|
| 5..5 (FIFO) | 8 | GBN 4, SR 4 | 20 / 0 / 0 | 20 / 0 / 0 |
| 5..11 | 8 | 5 | 20 / 0 / 0 | 20 / **20** / 0 |
| 5..11 | 24 (> RTTmax 22) | 5 | 20 / 0 / 0 | 20 / 0 / 0 |
| 5..17 | 8 | 6 | 20 / 0 / 0 | 20 / **20** / 0 |
| 5..17 | 36 (> RTTmax 34) | 6 | 20 / 0 / 0 | 20 / 0 / 0 |

(GBN W = 8 은 k = 3 이면 W = 2^k 라 FIFO 에서도 깨진다.) 두 가지가 보인다. (1) 공식은 **충분조건이고 보수적**이다: GBN 은 이 링크들에서 k = 4 로도 0, SR 도 k = 5 면 0. (2) SR 을 깨는 것은 재정렬 **더하기 짧은 RTO** 다. RTO > RTTmax (그리고 링크가 복제하지 않음) 면 한 프레임의 사본·ACK 는 동시에 하나만 살아 있으므로 (다음 사본은 앞 사본과 그 ACK 가 죽은 뒤에야 나간다) 보조 정리가 다시 성립해 **2W 로 충분**하다 — 연습 3 에서 80 실행으로 확인 (RTO 8: 62/80 실패, RTO 36: 0/80). TCP 가 32 비트 seq 에 "최대 세그먼트 수명(MSL)" 가정과 PAWS(타임스탬프로 랩어라운드된 옛 세그먼트 거르기, RFC 7323)를 같이 두는 것이 같은 이야기다.

### Part B — 실패는 유한해야 한다 (B5)

DATA 가 전부 사라지면 GBN·SR 모두 **정확히 W·max_tx = 8 × 5 = 40 프레임**을 보내고 t = max_tx·RTO = 150 에 FAIL 한다 (GBN: 되감을 때마다 8 개, 5 라운드. SR: 8 개 타이머가 1 틱씩 어긋나 각자 5 번). ACK 만 전부 사라지면 역시 FAIL 이지만 수신은 첫 창 8 개를 **이미 전달**했다 — COMM-M14 의 "FAIL ≠ 안 감" 이 창 단위로 커진 것이다. 상위 계층이 다시 보내려면 멱등성이나 세션 재동기화가 필요하다 (COMM-A11).

B6 무작위 스트레스(24 실행, 72000 메시지)에서 SR 의 재전송 수는 **매번 정확히 잃은 DATA + 잃은 ACK** 였다 (12/12). RTO > RTTmax 면 모든 타임아웃은 "그 사본이나 그 ACK 가 죽었다" 와 1:1 이기 때문이다 — 현장에서 SR 링크의 재전송 카운터로 양방향 손실률을 바로 읽을 수 있다는 뜻이다. GBN 은 재전송 ≤ W × 타임아웃만 성립한다.

### Part B — goodput 곡선: 이론 vs 실측

```svg
<svg viewBox="0 0 760 400" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">goodput vs 창 W — RTT 20 틱, RTO 30, demo_window.c 실측 (20000 메시지 × 시드 4 평균)</text>
  <g stroke="currentColor" opacity="0.25"> <line x1="80" y1="250.0" x2="700" y2="250.0"/> <line x1="80" y1="200.0" x2="700" y2="200.0"/> <line x1="80" y1="150.0" x2="700" y2="150.0"/> <line x1="80" y1="100.0" x2="700" y2="100.0"/> <line x1="80" y1="50.0" x2="700" y2="50.0"/> </g>
  <g stroke="currentColor"><line x1="80" y1="300" x2="710" y2="300"/><line x1="80" y1="40" x2="80" y2="300"/></g> <g fill="currentColor"> <text x="48" y="304.0">0%</text> <text x="48" y="254.0">20%</text> <text x="48" y="204.0">40%</text> <text x="48" y="154.0">60%</text> <text x="48" y="104.0">80%</text>
  <text x="42" y="54.0">100%</text> <text x="74.0" y="316">0</text> <text x="151.5" y="316">8</text> <text x="229.0" y="316">16</text> <text x="267.8" y="316">20</text> <text x="384.0" y="316">32</text> <text x="539.0" y="316">48</text> <text x="694.0" y="316">64</text> <text x="360" y="336">창 크기 W (프레임)</text> </g>
  <polyline points="89.7,287.5 99.4,275.0 118.8,250.0 157.5,200.0 196.2,150.0 235.0,100.0 273.8,50.2 312.5,50.2 390.0,50.2 545.0,50.2 700.0,50.2" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="2,3"/>
  <polyline points="89.7,289.2 99.4,280.0 118.8,265.0 157.5,241.5 196.2,222.0 235.0,205.2 273.8,189.0 312.5,175.5 390.0,149.8 545.0,105.0 700.0,87.0" fill="none" stroke="currentColor" stroke-width="1.8"/>
  <polyline points="89.7,289.2 99.4,279.5 118.8,264.8 157.5,247.0 196.2,237.5 235.0,232.2 273.8,229.8 312.5,228.5 390.0,229.8 545.0,229.5 700.0,229.5" fill="none" stroke="currentColor" stroke-width="1.8" stroke-dasharray="6,4"/>
  <polyline points="89.7,293.2 99.4,289.0 118.8,282.2 157.5,271.2 196.2,262.2 235.0,253.2 273.8,245.2 312.5,237.2 390.0,223.0 545.0,198.0 700.0,178.0" fill="none" stroke="currentColor" stroke-width="1.2"/>
  <polyline points="89.7,293.2 99.4,287.8 118.8,283.5 157.5,281.0 196.2,280.0 235.0,280.0 273.8,279.8 312.5,279.8 390.0,279.8 545.0,279.8 700.0,279.8" fill="none" stroke="currentColor" stroke-width="1.2" stroke-dasharray="6,4"/> <circle cx="89.7" cy="289.2" r="3.5" class="f-ok" stroke="currentColor"/>
  <circle cx="99.4" cy="280.0" r="3.5" class="f-ok" stroke="currentColor"/> <circle cx="118.8" cy="265.0" r="3.5" class="f-ok" stroke="currentColor"/> <circle cx="157.5" cy="241.5" r="3.5" class="f-ok" stroke="currentColor"/> <circle cx="196.2" cy="222.0" r="3.5" class="f-ok" stroke="currentColor"/>
  <circle cx="235.0" cy="205.2" r="3.5" class="f-ok" stroke="currentColor"/> <circle cx="273.8" cy="189.0" r="3.5" class="f-ok" stroke="currentColor"/> <circle cx="312.5" cy="175.5" r="3.5" class="f-ok" stroke="currentColor"/> <circle cx="390.0" cy="149.8" r="3.5" class="f-ok" stroke="currentColor"/>
  <circle cx="545.0" cy="105.0" r="3.5" class="f-ok" stroke="currentColor"/> <circle cx="700.0" cy="87.0" r="3.5" class="f-ok" stroke="currentColor"/> <rect x="86.2" y="285.8" width="7" height="7" class="f-warn" stroke="currentColor"/> <rect x="95.9" y="276.0" width="7" height="7" class="f-warn" stroke="currentColor"/>
  <rect x="115.2" y="261.2" width="7" height="7" class="f-warn" stroke="currentColor"/> <rect x="154.0" y="243.5" width="7" height="7" class="f-warn" stroke="currentColor"/> <rect x="192.8" y="234.0" width="7" height="7" class="f-warn" stroke="currentColor"/>
  <rect x="231.5" y="228.8" width="7" height="7" class="f-warn" stroke="currentColor"/> <rect x="270.2" y="226.2" width="7" height="7" class="f-warn" stroke="currentColor"/> <rect x="309.0" y="225.0" width="7" height="7" class="f-warn" stroke="currentColor"/>
  <rect x="386.5" y="226.2" width="7" height="7" class="f-warn" stroke="currentColor"/> <rect x="541.5" y="226.0" width="7" height="7" class="f-warn" stroke="currentColor"/> <rect x="696.5" y="226.0" width="7" height="7" class="f-warn" stroke="currentColor"/>
  <circle cx="89.7" cy="293.2" r="3.5" class="f-acc" stroke="currentColor"/> <circle cx="99.4" cy="289.0" r="3.5" class="f-acc" stroke="currentColor"/> <circle cx="118.8" cy="282.2" r="3.5" class="f-acc" stroke="currentColor"/> <circle cx="157.5" cy="271.2" r="3.5" class="f-acc" stroke="currentColor"/>
  <circle cx="196.2" cy="262.2" r="3.5" class="f-acc" stroke="currentColor"/> <circle cx="235.0" cy="253.2" r="3.5" class="f-acc" stroke="currentColor"/> <circle cx="273.8" cy="245.2" r="3.5" class="f-acc" stroke="currentColor"/> <circle cx="312.5" cy="237.2" r="3.5" class="f-acc" stroke="currentColor"/>
  <circle cx="390.0" cy="223.0" r="3.5" class="f-acc" stroke="currentColor"/> <circle cx="545.0" cy="198.0" r="3.5" class="f-acc" stroke="currentColor"/> <circle cx="700.0" cy="178.0" r="3.5" class="f-acc" stroke="currentColor"/> <rect x="86.2" y="289.8" width="7" height="7" class="f-mut" stroke="currentColor"/>
  <rect x="95.9" y="284.2" width="7" height="7" class="f-mut" stroke="currentColor"/> <rect x="115.2" y="280.0" width="7" height="7" class="f-mut" stroke="currentColor"/> <rect x="154.0" y="277.5" width="7" height="7" class="f-mut" stroke="currentColor"/>
  <rect x="192.8" y="276.5" width="7" height="7" class="f-mut" stroke="currentColor"/> <rect x="231.5" y="276.5" width="7" height="7" class="f-mut" stroke="currentColor"/> <rect x="270.2" y="276.2" width="7" height="7" class="f-mut" stroke="currentColor"/>
  <rect x="309.0" y="276.2" width="7" height="7" class="f-mut" stroke="currentColor"/> <rect x="386.5" y="276.2" width="7" height="7" class="f-mut" stroke="currentColor"/> <rect x="541.5" y="276.2" width="7" height="7" class="f-mut" stroke="currentColor"/>
  <rect x="696.5" y="276.2" width="7" height="7" class="f-mut" stroke="currentColor"/> <line x1="273.8" y1="40" x2="273.8" y2="300" stroke="currentColor" stroke-dasharray="1,3"/> <text x="277.8" y="60" fill="currentColor">W = RTT (BDP)</text> <g fill="currentColor">
  <line x1="74" y1="352" x2="96" y2="352" stroke="currentColor" stroke-dasharray="2,3"/><text x="104" y="356">손실 0 %: min(1, W/RTT) — GBN = SR</text> <circle cx="430" cy="352" r="3.5" class="f-ok" stroke="currentColor"/><text x="440" y="356">SR, 손실 5 % (상한 q = 90.25 %)</text>
  <rect x="81.5" y="368.5" width="7" height="7" class="f-warn" stroke="currentColor"/><text x="104" y="376">GBN, 손실 5 % — 28 % 에서 포화 (파선)</text> <circle cx="430" cy="372" r="3.5" class="f-acc" stroke="currentColor"/><text x="440" y="376">SR, 손실 20 %</text>
  <rect x="81.5" y="388.5" width="7" height="7" class="f-mut" stroke="currentColor"/><text x="104" y="396">GBN, 손실 20 % — 8 % 에서 포화 (파선)</text> </g>
</svg>
```

이론 세 가지 (q = (1 − p)², p = 방향별 손실):

- **손실 없음:** min(1, W/R) — 위에서 정확히 일치.
- **GBN:** 프레임 x 의 첫 손실은 R − 1 + RTO 틱 밀림 (위 트레이스), 되감아 다시 보낸 x 가 또 손실되면 RTO 밀림 (되감는 틱에 타이머 재시작 + x 송신). 누적 ACK 라 ACK 손실은 거의 공짜. 프레임당 시간 ≈ **max(1, R/W) + p·(R − 1 + RTO) + p²/(1 − p)·RTO**. (W = 1 은 stop-and-wait 라 COMM-M14 식 R + (1/q − 1)·RTO.)
- **SR:** 낙관적 상한 **min(q, W/R)** (링크 칸의 q 만 쓸모 있고, 창이 R 보다 작으면 창이 한계). 비관적 "라운드 모델": W 개를 보내고 전부 확인될 때까지 멈춘다고 보면 라운드 = max(R, W) + RTO·E[max_{i≤W} G_i] (G = 실패 횟수 ~ 기하분포, 실패확률 1 − q), q 로 자름.

`demo_window.c` 실측 (RTT 20, RTO 30, 20000 메시지 × 시드 4 평균, 링크 대비 %):

| W | 손실 5 %: GBN 측정 / 이론 | SR 측정 / 상한 / 라운드 | 프레임/메시지 GBN · SR | 손실 20 %: GBN 측정 / 이론 | SR 측정 |
|---|---|---|---|---|---|
| 1 | 4.3 / 4.3 | 4.3 / 5.0 / 4.3 | 1.11 · 1.11 | 2.7 / 2.7 | 2.7 |
| 2 | **8.2** / 8.0 | 8.0 / 10.0 / 7.6 | 1.11 · 1.11 | **4.9** / 4.7 | 4.4 |
| 8 | 21.2 / 19.9 | 23.4 / 40.0 / 20.4 | 1.41 · 1.11 | 7.6 / 7.2 | 11.5 |
| 16 | 27.1 / 26.5 | 37.9 / 80.0 / 32.7 | 1.84 · 1.11 | 8.0 / 8.0 | 18.7 |
| 20 | 28.1 / 28.3 | 44.4 / 90.2 / 38.5 | 2.06 · 1.11 | 8.1 / 8.1 | 21.9 |
| 32 | 28.1 / 28.3 | 60.1 / 90.2 / 45.9 | 2.69 · 1.11 | 8.1 / 8.1 | 30.8 |
| 64 | 28.2 / 28.3 | 85.2 / 90.2 / 58.4 | 3.55 · 1.11 | 8.1 / 8.1 | 48.8 |

(전체 표는 demo 출력 1번: 손실 0/1/5/10/20 %, W 11 개. 2번은 RTT 4 와 80.)

읽는 법:

1. **GBN 은 W ≥ RTT 에서 포화한다** (5 %: 28 %, 20 %: 8 %). 창을 키워도 손실 하나의 비용 R − 1 + RTO 는 그대로이고, 다시 보내는 프레임만 늘어난다 (프레임/메시지 2.06 → 3.55 = **airtime·전력 낭비**). 이론식은 RTT 20 · W ≥ R 에서 1 % 안팎으로 맞는다 (RTT 4 · 20 % 에서는 3 % 높게 예측). 창이 R 보다 작으면 측정이 2–12 % 높은데, 식이 손실 비용을 항상 R − 1 + RTO 로 세기 때문이다: W < R 이면 프레임이 W 개씩 묶여 나가고, 잃은 프레임이 묶음의 첫 프레임이면 앞 프레임(이전 묶음의 끝)의 ACK 가 R − W 틱 일찍 와서 타이머도 그만큼 일찍 재시작된다 (비용 W − 1 + RTO).
2. **SR 은 창을 키우는 만큼 계속 오른다** — 그러나 R(20)이 아니라 R + RTO 수준을 훨씬 넘어야 상한 q 에 다가간다 (W = 64 에서 85.2 / 90.2). 손실 프레임이 창의 맨 앞(base)에 걸리면 R + RTO 동안 창이 못 나가는 **head-of-line 막힘** 때문이고, 그래서 측정은 상한과 라운드 모델 사이에 있다. RTT 4 (짧은 링크)에서는 W = 16 (= 4R) 에서 90.2 로 상한에 붙었다.
3. **창이 아주 작으면 (W = 2) GBN 이 SR 보다 낫다** (5 %: 8.2 vs 8.0, 20 %: 4.9 vs 4.4, RTT 4·20 %: 23.9 vs 21.8). 누적 ACK 덕에 ACK 손실이 공짜이기 때문 (Part A). SACK 를 붙인 SR 은 8.62 로 둘 다 이긴다 (연습 2).
4. RTT 80 에서는 손실 5 % · W = 64 에서도 SR 28.9 %, GBN 9.0 % — 긴 링크는 창 크기가 전부다 (BDP 80 프레임).

### Part C — 57600 baud 텔레메트리 무선

`link_plan()` (테스트로 고정한 값): 72 B × 10 비트 / 57600 = **12500 µs/프레임**, R = ⌈200000 / 12500⌉ = **16 틱** (BDP 16 프레임), RTO = 16 + 8 = **24 틱 (300 ms)**, W = R + RTO = **40**, 2^k ≥ 80 → **k = 7** (헤더에는 8 비트 seq 바이트를 쓰면 여유), 송신 사본 40 × 64 = **2560 B** + SR 수신 버퍼 **2560 B**, 손실 없을 때 상한 5120 B/s, q·line = 0.9025 × 5120 = 4621 B/s. 115200 baud · 40 B 프레임 · 50 ms 예도 같이 고정 (3473 µs, R = 15, W = 38, k = 7).

`demo_window.c` 4번 (손실 5 % 양방향, 10000 메시지 × 시드 3 평균):

| W | GBN B/s | SR B/s | SR 이 q·line 대비 | SR RAM (송신 사본 + 수신 버퍼) |
|---|---|---|---|---|
| 4 | 898 | 894 | 19 % | 512 B |
| 8 | 1348 | 1484 | 32 % | 1024 B |
| 16 | 1722 | 2396 | 52 % | 2048 B |
| 32 | 1728 | 3663 | 79 % | 4096 B |
| **40** | 1726 | **4122** | **89 %** | 5120 B |
| 64 | 1724 | 4570 | 99 % | 8192 B |

결정: **SR, W = 40, seq 8 비트(필요 7), RTO 300 ms.** GBN 은 W = 16 이후 1.72 kB/s 에서 멈추고 airtime 만 더 쓴다. W = 64 면 99 % 지만 RAM 이 8 KB — nRF52832 (64 KB RAM 판) 같은 칩이면 괜찮고, 16 KB 급 MCU 라면 W = 40 이 적당하다 (`solution.c` 의 W = 40 단일 시드 실측 4053 B/s = 이론 상한의 87.8 % > 85 % 단언). stop-and-wait (W = 1) 는 이론식으로 1/(16 + (1/q − 1)·24) = 5.4 % ≈ 275 B/s 다. 수신 RAM 이 1 KB 로 묶이면? 연습 4: 프레임을 32 B 로 줄여 W = 32 로 늘리는 것보다 64 B × 16 이 낫다 (2878 vs 2661 B/s) — **RAM 이 같으면 창이 덮는 시간(W × 프레임 시간)이 거의 같고**, 작은 프레임은 헤더 비율만 나빠진다.

현실 메모 (규격·제품 세부는 확인 권장): SiK/RFD900 류 텔레메트리 무선은 반이중 TDM 이라 ACK 가 같은 airtime 을 나눠 쓴다 — 이 모델은 ACK 채널을 공짜로 줬으므로 실제 이득은 조금 작다. HDLC/LAPB 는 mod 8 / mod 128 seq 에 REJ(= GBN) 와 SREJ(= SR) 를 둘 다 정의한다 (창 최대 7 / 127 = 2^k − 1 과 일치). TCP 는 누적 ACK 위에 SACK 블록(RFC 2018)을 얹은 혼합형이다. LoRaWAN 처럼 duty cycle 제한이 있는 링크에서는 창보다 airtime 예산이 먼저 막힌다.

### 복잡도와 메모리

| | 송신 | 수신 | 이 Mac 의 sizeof (WMAX = 64) |
|---|---|---|---|
| GBN | 사본 W 칸 + 타이머 1 개, ACK 처리 O(1) | 상태 = expected 하나, 버퍼 0 | `win_tx` 800 B, `win_rx` 392 B (두 프로토콜 공용 구조체) |
| SR | 사본 W 칸 + 타이머 W 개, 틱마다 [base, hi) 스캔 O(W) | 버퍼 W 칸 + have 비트 W | (SR 수신 버퍼 64 × 4 B payload 포함) |

실제 펌웨어에서 타이머 W 개는 하드웨어 타이머 W 개가 아니라 `deadline[]` 배열 + 1 ms 틱 스캔(이 레슨처럼) 또는 만료 순 정렬 리스트 + 하드웨어 타이머 하나 (RTOS 소프트웨어 타이머, RTOS-B 레슨). W ≤ 64 면 스캔이 충분히 싸다. 비교는 `(int32_t)(now − deadline) >= 0` (틱 랩어라운드 안전).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 36/36 passed, exit 0 (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -O2 demo_window.c -o demo_window -lm && ./demo_window    # 약 3 초, 5. 풀이의 표 (Apple clang 21.0.0)
```

`solution.c` 실행 출력 중 테스트 외 줄 (트레이스 제외):

```text
A  clean RTT 20: W=1 4000 ticks, W=5 804, W=20 219, W=32 219 (200 msgs)
A  DATA#3 lost: GBN retx=8 discarded=7 ticks=84 | SR retx=1 max_buffered=8 ticks=65
A  ACK#3 lost: GBN retx=0 | SR retx=1 rx_old=1
A  GBN RTO 15 < RTT 20 (spurious): ok=1 timeouts=38 retx=300 frames=600 ticks=946
B  seq-space scenario: 252 cases, mismatches=0 (expected: GBN ok iff W <= 2^k-1, SR ok iff W <= 2^(k-1))
B  random FIFO at bound: runs=48 msgs=28800 frames=62667 violations=0 | bound+1: failed runs=35/48
B  jitter 5..15 + RTO 12: FIFO-formula k -> failed runs GBN 8 SR 24 | seq_bits_needed k -> GBN 0 SR 0 (of 24 each)
B  GBN all DATA lost: failed=1 frames=40 t_fail=150 delivered=0
B  GBN all ACK lost : failed=1 frames=40 delivered=8 (FAIL != not delivered)
B  SR  all DATA lost: failed=1 frames=40 t_fail=150 delivered=0
B  SR  all ACK lost : failed=1 frames=40 delivered=8 (FAIL != not delivered)
B  stress k=8: runs=24 msgs=72000 bad=0, SR retx == lostDATA+lostACK in 12/12 runs
C  plan 57600: frame 12500 us, R=16 ticks (BDP 16 frames), RTO=24, SR W=40, k=7, RAM tx 2560 B + rx 2560 B, line 5120 B/s
C  GBN W= 8: 15891 ticks = 198.6 s, goodput 1289 B/s (25.2 % of line), retx 1832
C  SR  W= 8: 13876 ticks = 173.4 s, goodput 1476 B/s (28.8 % of line), retx 428
C  GBN W=16: 13142 ticks = 164.3 s, goodput 1558 B/s (30.4 % of line), retx 3789
C  SR  W=16: 8668 ticks = 108.3 s, goodput 2363 B/s (46.1 % of line), retx 428
C  GBN W=32: 12884 ticks = 161.1 s, goodput 1590 B/s (31.0 % of line), retx 7282
C  SR  W=32: 5864 ticks = 73.3 s, goodput 3492 B/s (68.2 % of line), retx 428
C  GBN W=40: 12856 ticks = 160.7 s, goodput 1593 B/s (31.1 % of line), retx 8793
C  SR  W=40: 5053 ticks = 63.2 s, goodput 4053 B/s (79.2 % of line), retx 428
36/36 passed
```

## 7. 자주 하는 실수

**1. GBN 에서 ACK 범위를 `next` 로 검사한다.** 되감은 직후 next = base 라 되감기 전에 보낸 프레임의 정상 ACK 를 전부 버린다. 짝 실수: base 가 next 를 앞질렀는데 `next = base` 를 안 해서 이미 확인된 프레임을 다시 보낸다 (재전송 수는 같을 수 있어 `sent_acked` 로 잡는다). 고친 코드: `if (d == 0 || d > t->hi - t->base) return;` 다음에 `if (t->next < t->base) t->next = t->base;`.

**2. SR 수신이 "앞 창" 프레임에 ACK 를 안 보낸다.** "이미 넘겼으니 무시" 하면 그 ACK 가 사라졌던 송신은 같은 프레임을 max_tx 번 보내고 FAIL 한다 (ACK #3 손실 테스트가 바로 깨진다). 고친 코드: `else if (d >= r->mask + 1u - r->cfg.W) { r->old++; rx_send_ack(r, now, seq); }`.

**3. SR 전달을 `if` 로 한 번만.** 구멍(#3)이 메워져도 #3 하나만 나가고 #4..#10 은 다음 도착 때까지 갇힌다 → 순서·지연 테스트 실패, 끝에서는 영영 안 나감. 고친 코드: `while (r->have[r->expected % WMAX]) { … r->expected++; }`.

**4. 창을 seq 공간만큼 연다.** "8 비트 seq 니까 창 256" 은 GBN 이면 255, SR 이면 128 이 한계다. 넘기면 크래시가 아니라 **조용한 손상**(B2: CRC 통과, ACK 정상, 내용만 과거)이라 현장에서 가장 찾기 어렵다. 경계 W ≤ 2^k − 1 / 2^(k−1) 을 `cfg_valid` 나 정적 assert 로 못 박는다.

**5. FIFO 증명을 재정렬 링크에 그대로 쓴다.** 중간에 큐잉·재전송하는 무선 모뎀, USB-시리얼 + OS 버퍼, 다중 경로 네트워크는 순서를 바꾼다. 거기에 공격적인 RTO 가 겹치면 SR 이 2W 로는 깨진다 (demo 3번: k = 4, 20/20 실패). 고친 방법: RTO > RTTmax 를 보장하거나, k 를 `seq_bits_needed()` 로 늘리거나, 8/16 비트 seq 를 쓰고 링크 수명 상한을 문서화한다.

**6. 송신 버퍼를 잊고 RAM 을 계산한다.** GBN 도 수신 버퍼만 없을 뿐 **송신 사본 W 칸**은 필요하다 (되감으면 다시 보내야 하므로). 텔레메트리 예: GBN 2560 B, SR 5120 B.

**7. 실패 횟수 off-by-one.** `txcount > max_tx` 로 쓰면 한 라운드 더 보낸다. 전부 손실 테스트가 프레임 수(40)와 FAIL 시각(150)을 같이 못 박는다.

## 8. 면접 Q&A

<details><summary>Q1. GBN 과 SR 의 차이, 그리고 언제 무엇을 쓰나요?</summary>

GBN: 수신 버퍼 없음(상태 = expected 하나), 누적 ACK, 타이머 하나, 손실 시 미확인 전부 재전송. SR: 수신 버퍼 W 칸, 개별 ACK, 프레임별 타이머, 잃은 것만 재전송. 측정으로 말하면: RTT 20·손실 5 % 에서 GBN 은 W ≥ 20 부터 28 % 에서 멈추고 프레임/메시지가 3.55 까지 오르는 반면 SR 은 W = 64 에서 85 %, 프레임/메시지 1.11 (≈ 1/q). 선택 기준: 손실 × BDP 가 크면(무선, 긴 RTT) SR, 손실이 거의 없고 수신 쪽 RAM 이 귀하면(칩 내부 링크, 짧은 버스) GBN. 실제로 PCIe 데이터 링크 계층은 Ack/Nak + replay buffer 로 GBN 계열이다 (규격 확인 권장) — 비트 오류율이 극히 낮아 재전송이 드물기 때문.

</details>

<details><summary>Q2. SR 의 창이 왜 seq 공간의 절반 이하여야 하나요? 증명해 보세요.</summary>

수신이 받은 seq 를 해석하는 구역이 둘이다: 지금 창 [e, e + W) 와 바로 앞 창 [e − W, e) (ACK 가 사라져 재전송된 옛 프레임). FIFO 링크에서 송신은 i 가 미확인일 때만 i 를 보내므로 그 순간 hi ≤ i + W 이고, 그 사본이 도착할 때 e − i ∈ [−W + 1, W] 다. 두 구역이 mod 2^k 에서 겹치지 않을 조건이 2W ≤ 2^k. 반례: k = 3, W = 5, 첫 창 ACK 전부 손실 → 새 창 {5,6,7,0,1} 이 재전송 seq 0, 1 을 #8, #9 로 받는다 (solution.c 트레이스, wrong = 2). 이 레슨은 k = 1..6 전수로 "정상 ⇔ W ≤ 2^(k−1)" 을 확인했다.

</details>

<details><summary>Q3. 그럼 GBN 은 왜 2^k − 1 까지 되죠?</summary>

GBN 수신은 "지금 창" 이 한 칸(expected)뿐이라 겹칠 앞 창이 W 개, 지금 창이 1 개 → W + 1 ≤ 2^k. 반례는 W = 2^k: 첫 창이 다 도착하고 ACK 가 다 사라지면 expected = 2^k ≡ 0 이라 재전송 #0 을 새 #2^k 로 받는다. 송신 쪽도 누적 ACK 값 d ∈ [0, W] 를 구분하려면 W + 1 개의 값이 필요하다. HDLC/LAPB 의 mod 8 창 최대 7, mod 128 창 최대 127 이 이 조건이다.

</details>

<details><summary>Q4. 링크가 패킷 순서를 바꾼다면요?</summary>

FIFO 증명의 "도착 순간 수신이 받은 것 = 그 사본 이전에 보낸 것" 이 깨진다. 지연 편차 J_f 동안 송신이 더 보낸 프레임 수만큼 공간이 더 필요하다: SR 은 2^k ≥ 2W + J_f (+ 낡은 ACK 쪽 W + RTTmax), GBN 은 W + 1 + J_f (+ W + RTTmax + 1) — 이 레슨의 충분조건. 실측(demo 3번)은 이것이 보수적임을 보여 주고, SR 을 실제로 깨는 것은 재정렬 + RTO < RTTmax 의 조합이었다. RTO > RTTmax 면 프레임마다 사본·ACK 가 하나만 살아 있어 2W 로 다시 충분 (연습 3: 0/80 실패). TCP 의 답은 "seq 를 충분히 크게(32 비트) + 세그먼트 수명 상한(MSL) + 타임스탬프로 옛 세그먼트 거르기(PAWS)" 다.

</details>

<details><summary>Q5. 창 크기는 어떻게 정하나요? 손실이 있으면 BDP 면 되나요?</summary>

손실이 없으면 W = BDP = RTT / 프레임 시간이면 링크가 찬다 (min(1, W/R) 가 정확히 측정됨). 손실이 있으면 SR 은 base 가 막히는 R + RTO 동안에도 보낼 수 있게 W ≈ R + RTO 이상 (텔레메트리 예: 16 + 24 = 40 → 상한의 89 %, 64 → 99 %). GBN 은 W > R 이 의미가 없다 (W = 16 이후 포화) — 오히려 손실마다 다시 보내는 양만 늘어난다. 그리고 W 가 정해지면 k 와 RAM 이 따라 나온다: 2^k ≥ 2W, 송신 사본 W × payload, SR 수신 버퍼 W × payload.

</details>

<details><summary>Q6. ACK 가 사라지면 GBN 과 SR 은 각각 어떻게 되나요?</summary>

GBN: 다음 누적 ACK 가 덮으므로 대개 공짜 (ACK #3 손실 → 재전송 0). 창 끝의 마지막 ACK 처럼 뒤따르는 ACK 가 없을 때만 타임아웃. SR: 그 프레임의 유일한 확인이 사라졌으므로 타임아웃 → 재전송 → 수신은 "앞 창" 으로 판정해 ACK 만 다시 (rx_old). 그래서 SR 의 재전송 수는 정확히 "잃은 DATA + 잃은 ACK" (B6 12/12). 해결: SR 의 ACK 에 누적 번호 + SACK 비트맵을 실으면 ACK 하나가 앞의 손실을 덮는다 — ACK 손실 20 % 에서 재전송 1276 → 3 (연습 2).

</details>

<details><summary>Q7. GBN 에서 손실 하나의 비용은 정확히 얼마인가요?</summary>

이 구현(교과서·TCP 식: base 가 전진하면 타이머 재시작)에서는 R − 1 + RTO 틱: 손실 프레임 x 의 바로 앞 프레임 ACK 가 R − 1 틱 뒤에 와서 base 가 x 가 되는 순간 타이머가 새로 시작하기 때문 (트레이스: #3 을 t = 3 에 잃었는데 타임아웃은 t = 44). 이 식으로 만든 goodput 이론이 RTT 20 · W ≥ R 에서 1 % 안팎으로 맞았다. 타이머를 "base 를 보낸 시각 + RTO" 로 바꾸면 비용이 RTO 로 줄어 goodput 28.1 % → 38.7 % (이론 38.8 %, 연습 1). 대가는 프레임별 송신 시각을 저장해야 한다는 것 (어차피 SR 은 갖고 있다).

</details>

<details><summary>Q8. MCU 에서 SR 수신 버퍼 RAM 이 부족하면요?</summary>

(1) W 를 줄이고 그만큼의 처리량 손실을 숫자로 받아들인다 (텔레메트리: W 16 → 2396 B/s, 40 → 4122 B/s). (2) 프레임을 작게 해서 칸 수를 늘리는 것은 생각보다 안 된다: RAM 이 같으면 창이 덮는 시간이 같고 헤더 비율만 나빠진다 (연습 4: 64 B × 16 칸 2878 B/s vs 32 B × 32 칸 2661 B/s). (3) 수신 버퍼를 DMA 수신 링과 공유해 복사를 없앤다 (COMM-A08 의 디스크립터 링 — 버퍼 소유권을 "창 안 미전달" 동안 붙잡는 구조). (4) SACK 로 쓸데없는 재전송을 줄여 같은 W 에서 효율을 올린다. (5) 손실이 드물면 GBN + 작은 W 도 정당한 선택이다.

</details>

<details><summary>Q9. 이 프로토콜을 펌웨어에 넣으면 어디서 무엇을 돌리나요?</summary>

UART DMA + IDLE 수신(COMM-M11) → 프레임 파서/CRC(COMM-M10) → "프레임 도착" 이벤트를 링크 태스크 큐로. 링크 태스크 하나가 도착 프레임과 1 ms 틱을 둘 다 처리하므로 `*_on_data`/`*_on_ack`/타이머 스캔이 같은 문맥 — 락 불필요 (M14 와 같은 구조). TX 는 DMA 로 보내고 그 버퍼(송신 사본)는 확인될 때까지 재사용 금지. 앱은 고정 큐로 넣고 backpressure(창이 차면 큐가 차고 앱이 대기)를 받는다. 진단 카운터: retx, timeouts, rx_old(= 내 ACK 가 사라짐), rx_dup, discarded(GBN), max_buffered, sent_acked(항상 0) — SR 이면 retx 로 양방향 손실률을 바로 추정한다.

</details>

## 9. 추가 연습

### 연습 1 — GBN 타이머를 "base 를 보낸 시각 + RTO" 로

`tx_frame()` 이 이미 프레임마다 `deadline[]` 을 적는다. `gbn_st_poll()` 에서 base 의 deadline 이 지나면 되감는 GBN 변형을 만들고, RTT 20 · 손실 5 % · W = 32 에서 원래 GBN 과 goodput 을 비교하라. 이론값은?

<details><summary>답</summary>

손실 하나의 비용이 R − 1 + RTO 에서 RTO 로 준다 → 프레임당 시간 1 + p/(1 − p)·RTO. 실측 (practice.c 출력):

```text
ex1 GBN W=32 RTT 20 RTO 30 loss 5%: restart-on-advance 28.1% (theory 28.3) | send-time timer 38.7% (theory 38.8)
```

1.38 배. 함정 하나: base 가 아직 다시 안 나간 상태(`next == base`)에서는 타임아웃을 보지 않는다 — 되감기 직후나 ACK 가 next 를 앞지른 직후에는 base 가 이번 틱에 (다시) 나갈 참이고, 그 deadline 은 옛 송신 시각 기준이라 바로 또 타임아웃이 난다. 무작위 손실·재정렬·짧은 RTO 20 실행에서 정확성 유지 확인.

</details>

### 연습 2 — SR + 누적 ACK + SACK 비트맵

ACK = { seq: rcv_base (누적, 다음 기대), payload: bit i = rcv_base + 1 + i 를 버퍼에 가짐 (i < 32) }. 수신은 매 도착마다 현재 상태 전체를 보내고, 송신은 누적 구간과 비트를 전부 확인 처리한다. DATA 손실 0 · ACK 손실 20 % 와, 손실 5 % · W = 2 / 32 에서 비교하라.

<details><summary>답</summary>

```text
ex2 ACK loss 20%, W=16: SR retx=1276 (lost ACKs 1276) ticks=17965 | SR+SACK retx=3 ticks=6704
ex2 loss 5%, W=2: GBN 8.25%  SR 7.96%  SR+SACK 8.62% | W=32: SR 60.0%  SR+SACK 69.7%
```

ACK 하나가 앞의 ACK 손실을 전부 덮어 재전송이 1276 → 3 (연속 ACK 가 타이머 안에 전부 사라진 경우만 남음). 작은 창에서 SR 이 GBN 에 지던 문제도 사라지고(8.62 > 8.25), W = 32 에서도 60.0 → 69.7 %. 이것이 TCP SACK(RFC 2018)·대부분의 실전 SR 이 "누적 + 선택" 혼합형인 이유다. 주의: 누적 필드는 GBN 과 같은 모호성 조건을, 비트맵은 SR 의 조건을 따른다 — FIFO 경계 W = 2^(k−1) 에서 20 실행 정확성 확인.

</details>

### 연습 3 — 재정렬 링크: RTO 를 RTTmax 보다 길게 하면 2W 로 충분한가

지연 5..17/방향(RTTmax 34), 손실 10 %, W = 2/4/8/16, k = log2(2W) 로 RTO 8 과 RTO 36 을 각각 시드 20 개씩 돌려라. 이유를 한 문단으로.

<details><summary>답</summary>

```text
ex3 SR k=log2(2W), delay 5..17: RTO 8 -> failed 62/80, RTO 36 (> RTTmax 34) -> failed 0/80 | seq_bits_needed(W=16)=6 vs 5
```

RTO > RTTmax 면 사본 c+1 은 사본 c 를 보낸 뒤 RTO 가 지나야 나가므로 그때 사본 c 와 그 ACK 는 이미 도착했거나 죽었다. 그러면 i 의 사본·ACK 는 링크에 동시에 하나만 있고, i 가 확인되기 전에는 송신이 i + W − 1 너머를 못 보내므로 "도착 순간 e ≤ i + W" 가 재정렬과 무관하게 다시 성립한다 → 2W 로 충분. `seq_bits_needed()` 는 RTO 를 모르는 충분조건이라 6 비트를 요구한다. 링크가 패킷을 **복제**하면(일부 메시 무선) 이 논증은 깨진다.

</details>

### 연습 4 — 수신 RAM 1 KB: 큰 프레임 적게 vs 작은 프레임 많이

비트 오류율 7·10^-5 의 57600 baud 링크(RTT 200 ms)에서 (a) payload 64 B / 프레임 72 B / W = 16, (b) payload 32 B / 프레임 40 B / W = 32 의 SR goodput 을 비교하라. 프레임 손실은 1 − (1 − BER)^bits, ACK 는 8 B 프레임으로.

<details><summary>답</summary>

```text
ex4 64 B payload (72 B frame): loss 4.92%, frame 12500 us, R=16, RTO=24, W=16 (1 KB rx) -> 2878 B/s (line 5120 B/s)
ex4 32 B payload (40 B frame): loss 2.76%, frame 6945 us, R=29, RTO=44, W=32 (1 KB rx) -> 2661 B/s (line 4607 B/s)
```

(a) 가 이긴다. 작은 프레임은 손실이 4.92 → 2.76 % 로 줄지만, 같은 RAM 이 덮는 시간이 16 × 12.5 = 200 ms 대 32 × 6.9 = 222 ms 로 거의 같고(창은 "바이트 × 시간" 자원이다), 헤더 비율 때문에 line 상한이 5120 → 4607 B/s 로 10 % 떨어진다. BER 이 훨씬 높아지면 결론이 뒤집힐 수 있다 — 프레임 크기 최적화는 BER 과 헤더 크기의 함수다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 17/17 passed (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_asan && ./pr_asan   # 오류 없음
```

## 10. 핵심 정리

- 손실 없는 링크: goodput = min(1, W/RTT) (정확히 측정됨). W = BDP 가 링크를 채운다. stop-and-wait 는 W = 1.
- GBN = 누적 ACK + 타이머 하나 + 되감기, 수신 버퍼 0. 손실 하나 = R − 1 + RTO 틱 + 미확인 전부 재전송 → W ≥ RTT 에서 포화 (5 %: 28 %, 20 %: 8 %). ACK 범위는 hi 기준, base 가 next 를 앞지르면 next = base.
- SR = 개별 ACK + 프레임별 타이머 + 수신 버퍼 W 칸, 잃은 것만 재전송 (재전송 = 잃은 DATA + 잃은 ACK). 상한 min(q, W/R), head-of-line 때문에 W ≫ R + RTO 에서야 q 근처.
- seq 공간 (FIFO): **GBN W ≤ 2^k − 1, SR W ≤ 2^(k−1)**. 경계 + 1 은 크래시가 아니라 "옛 메시지를 새 것으로" 받는 조용한 손상 (CRC 통과).
- 재정렬: 지연 편차·링크 수명만큼 공간이 더 필요 (2^k ≥ 2W + J_f, W + RTTmax). RTO > RTTmax 면 SR 은 2W 로 다시 충분.
- 작은 창·ACK 손실 많은 링크에서는 GBN ≥ SR. 실전 SR 은 누적 + SACK 비트맵.
- 설계 순서: BDP → W (SR 은 ≈ R + RTO) → k (2^k ≥ 2W, 바이트 단위로 여유) → RAM (송신 사본 + SR 수신 버퍼) → 시뮬레이션으로 확인. 57600 baud · 200 ms · 5 %: SR W = 40, k = 7(8 비트 필드), 5 KB, 4.1 kB/s.

## 참고자료

- J. Kurose, K. Ross, *Computer Networking: A Top-Down Approach*, 3.4 절 (GBN/SR FSM, SR 창 ≤ seq 공간의 절반, 재정렬과 최대 패킷 수명 가정).
- A. Tanenbaum, D. Wetherall, *Computer Networks*, 3 장 — 프로토콜 5 (Go-Back-N, 창 ≤ MAX_SEQ) 와 6 (Selective Repeat, 창 ≤ (MAX_SEQ + 1)/2).
- D. Bertsekas, R. Gallager, *Data Networks*, 2nd ed., 2.4 절 — ARQ 의 정확성 증명과 모듈러 seq 조건, GBN 효율 분석.
- RFC 2018, *TCP Selective Acknowledgment Options*: <https://www.rfc-editor.org/rfc/rfc2018> — 누적 ACK + SACK 블록 (연습 2 의 실전판).
- RFC 7323, *TCP Extensions for High Performance*: <https://www.rfc-editor.org/rfc/rfc7323> — 창 확장(BDP 가 64 KB 를 넘을 때)과 PAWS (랩어라운드된 옛 세그먼트 거르기).
- RFC 6298, *Computing TCP's Retransmission Timer* — 5.3 "새 데이터를 확인하는 ACK 가 오면 타이머 재시작" (이 레슨 GBN 타이머 규칙의 근거; 조항 문구는 원문 확인 권장).
- ISO/IEC 13239 (HDLC) / ITU-T X.25 LAPB — mod 8 / mod 128 순서 번호, REJ / SREJ (세부는 규격 확인 권장).
- 관련 레슨: COMM-M14 (stop-and-wait ARQ, RTO 추정, FAIL ≠ 안 감), COMM-M10 (견고한 프레임 파서), COMM-M11 (DMA + IDLE 수신), COMM-B10 (프레임 + CRC), COMM-A07 (ISO-TP 의 블록 크기·흐름 제어 = 수신 쪽이 창을 정하는 방식), COMM-A08 (DMA 디스크립터 링과 버퍼 소유권), COMM-A11 (통신 스택 종합), TEST-A05 (결정적 시뮬레이션), RTOS-B 소프트웨어 타이머·큐 레슨, BM-A05 (YMODEM 부트로더, 작성 예정).
