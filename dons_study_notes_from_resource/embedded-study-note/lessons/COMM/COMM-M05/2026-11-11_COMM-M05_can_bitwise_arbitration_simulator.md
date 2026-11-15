---
id: COMM-M05
time: 80분
updated: 2026-11-11
---
# COMM-M05 CAN 중재 시뮬레이션: 낮은 ID가 이기는 이유

## 학습 목표

- dominant(0) / recessive(1) 와 **wired-AND 버스**에서, 여러 노드가 같은 비트에 SOF 를 내도 프레임이 하나도 깨지지 않는 **비파괴 비트 단위 중재**(non-destructive bitwise arbitration)를 비트 시뮬레이터로 재현한다.
- 송신기 판정 규칙 하나 — "**중재 필드에서 recessive 를 냈는데 dominant 를 읽으면 졌다, 그 밖의 불일치는 에러다**" — 를 코드로 쓰고, LOST / BIT / STUFF / ACK 에러를 구분한다.
- 진 노드가 **그 비트부터 수신기로 계속** 이긴 프레임을 온전히 받고(ACK 까지 내고), 다음 bus idle 에 재시도하는 것을 테스트로 증명한다.
- 표준 vs 확장, data vs remote 의 경쟁 결과(RTR / SRR / IDE)를 비트 위치까지 말하고, 같은 ID 를 두 노드가 보내면 무엇이 깨지는지 보인다.
- 송신 큐 정책(FIFO vs 우선순위)이 만드는 **우선순위 역전**과 과부하에서의 **높은 ID 굶주림**을 측정하고, ID 배정 원칙(짧은 주기 = 낮은 ID)을 설명한다.

## 1. 문제

"CAN 에서 왜 낮은 ID 가 이기나요?" 는 자동차·로봇·드론 펌웨어 면접의 단골 첫 질문이고, 꼬리질문(확장 ID 와 겨루면? 같은 ID 를 두 노드가 보내면? 높은 ID 는 언제 나가나?)에서 실력이 갈린다. 답은 공식이 아니라 **비트 하나하나에서 무슨 일이 일어나는지**다 — 이 레슨은 그걸 직접 돌려 본다.

CAN 트랜시버는 dominant 일 때만 CANH/CANL 을 벌려 구동하고, recessive 일 때는 손을 뗀다. 그래서 버스 레벨은 I2C 와 똑같이 **모든 노드의 AND** 다 (COMM-B06 의 오픈드레인 wired-AND, COMM-M02 의 I2C 중재와 같은 원리). 누구든 0(dominant)을 내면 버스는 0. 모든 송신 노드는 자기가 낸 비트를 같은 비트 시간 안에 다시 읽는다(bit monitoring). 1 을 냈는데 0 이 읽혔다면 — 중재 필드 안에서는 "나보다 우선인 누군가가 있다"는 뜻이니 **조용히 물러나 수신기가 된다**. 이긴 노드의 비트열은 한 비트도 바뀌지 않았으므로 이긴 쪽은 아무 일 없었던 것처럼 프레임을 끝낸다. 이더넷(CSMA/CD)이 충돌 시 둘 다 버리고 랜덤 백오프하는 것과 달리, CAN 은 충돌이 곧 **우선순위 스케줄링**이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">3노드 동시 SOF (0x065 / 0x063 / 0x070) — solution.c 실제 트레이스 첫 16비트</text>
  <rect x="330" y="40" width="36" height="248" class="f-mut" opacity=".35"/>
  <rect x="438" y="184" width="36" height="32" class="f-warn" opacity=".5"/>
  <rect x="510" y="74" width="36" height="32" class="f-warn" opacity=".5"/>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="168" y="56">SOF</text>
    <text x="204" y="56">10</text>
    <text x="240" y="56">9</text>
    <text x="276" y="56">8</text>
    <text x="312" y="56">7</text>
    <text x="348" y="56">stf</text>
    <text x="384" y="56">6</text>
    <text x="420" y="56">5</text>
    <text x="456" y="56">4</text>
    <text x="492" y="56">3</text>
    <text x="528" y="56">2</text>
    <text x="564" y="56">1</text>
    <text x="600" y="56">0</text>
    <text x="636" y="56">RTR</text>
    <text x="672" y="56">IDE</text>
    <text x="708" y="56">r0</text>
  </g>
  <text x="186" y="40" fill="currentColor" font-size="12">ID[10..7]</text>
  <text x="366" y="40" fill="currentColor" font-size="12">ID[6..0] ──────────────</text>
  <g stroke="currentColor" stroke-dasharray="2 3" opacity=".35">
    <line x1="150" y1="62" x2="150" y2="282"/>
    <line x1="186" y1="62" x2="186" y2="282"/>
    <line x1="222" y1="62" x2="222" y2="282"/>
    <line x1="258" y1="62" x2="258" y2="282"/>
    <line x1="294" y1="62" x2="294" y2="282"/>
    <line x1="330" y1="62" x2="330" y2="282"/>
    <line x1="366" y1="62" x2="366" y2="282"/>
    <line x1="402" y1="62" x2="402" y2="282"/>
    <line x1="438" y1="62" x2="438" y2="282"/>
    <line x1="474" y1="62" x2="474" y2="282"/>
    <line x1="510" y1="62" x2="510" y2="282"/>
    <line x1="546" y1="62" x2="546" y2="282"/>
    <line x1="582" y1="62" x2="582" y2="282"/>
    <line x1="618" y1="62" x2="618" y2="282"/>
    <line x1="654" y1="62" x2="654" y2="282"/>
    <line x1="690" y1="62" x2="690" y2="282"/>
    <line x1="726" y1="62" x2="726" y2="282"/>
  </g>
  <g fill="none" stroke="currentColor" stroke-width="2">
    <polyline points="150,100 186,100 222,100 258,100 294,100 330,100 330,80 366,80 402,80 438,80 438,100 474,100 510,100 510,80 546,80 582,80 618,80 654,80 690,80 726,80"/>
    <polyline points="150,155 186,155 222,155 258,155 294,155 330,155 330,135 366,135 402,135 438,135 438,155 474,155 510,155 546,155 546,135 582,135 618,135 618,155 654,155 690,155 726,155"/>
    <polyline points="150,210 186,210 222,210 258,210 294,210 330,210 330,190 366,190 402,190 438,190 474,190 510,190 546,190 582,190 618,190 654,190 690,190 726,190"/>
    <polyline points="150,265 186,265 222,265 258,265 294,265 330,265 330,245 366,245 402,245 438,245 438,265 474,265 510,265 546,265 546,245 582,245 618,245 618,265 654,265 690,265 726,265"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="95">node0 0x065</text>
    <text x="10" y="150">node1 0x063</text>
    <text x="10" y="205">node2 0x070</text>
    <text x="10" y="260">BUS (AND)</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="408" y="230">rec 냈는데 dom → LOST (uidx 7)</text>
    <text x="480" y="120">LOST (uidx 9), 이후 rec = 수신만</text>
    <text x="10" y="300">위 = recessive 1, 아래 = dominant 0. 회색 열 = 스터프 비트: 세 노드가 똑같이 넣는다.</text>
    <text x="10" y="318">BUS = 0x063 의 비트열 그대로 (파괴 없는 중재). 1비트 = 2 µs @ 500 kbit/s.</text>
  </g>
</svg>
```

필드와 비스터프 인덱스(uidx, SOF = 0)는 COMM-M04 와 같다. 중재에 쓰이는 부분만:

| 형식 | uidx 1–11 | 12 | 13 | 14–31 | 32 | 중재 필드 (ISO 11898-1, 확인 권장) |
|---|---|---|---|---|---|---|
| 표준 data | ID[10:0] | RTR = **d** | IDE = **d** | r0, DLC… | — | ID + RTR (uidx 1–12) |
| 표준 remote | ID[10:0] | RTR = r | IDE = **d** | r0, DLC… | — | ID + RTR (uidx 1–12) |
| 확장 data | ID[28:18] (ID-A) | SRR = r | IDE = r | ID[17:0] (ID-B) | RTR = **d** | ID-A + SRR + IDE + ID-B + RTR (1–32) |
| 확장 remote | ID[28:18] | SRR = r | IDE = r | ID[17:0] | RTR = r | 1–32 |

d = dominant 0, r = recessive 1. 표준의 IDE 는 규격상 제어 필드지만 dominant 라서 표준 노드는 거기서 질 수가 없고, 확장 노드는 IDE 가 자기 중재 필드 안이라 거기서 진다. 그래서 같은 11비트 base 끼리면:

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 base ID 0x123 끼리의 경쟁 — 처음 갈리는 비트에서 dominant 가 이긴다</text>
  <rect x="330" y="76" width="110" height="34" class="f-warn" opacity=".45"/>
  <rect x="330" y="116" width="110" height="34" class="f-warn" opacity=".45"/>
  <rect x="450" y="156" width="110" height="34" class="f-warn" opacity=".45"/>
  <rect x="570" y="196" width="150" height="34" class="f-warn" opacity=".45"/>
  <g stroke="currentColor" fill="none">
    <rect x="160" y="36" width="160" height="34"/><rect x="330" y="36" width="110" height="34"/>
    <rect x="450" y="36" width="110" height="34"/><rect x="570" y="36" width="150" height="34"/>
    <rect x="160" y="76" width="160" height="34"/><rect x="330" y="76" width="110" height="34"/>
    <rect x="450" y="76" width="110" height="34"/><rect x="570" y="76" width="150" height="34"/>
    <rect x="160" y="116" width="160" height="34"/><rect x="330" y="116" width="110" height="34"/>
    <rect x="450" y="116" width="110" height="34"/><rect x="570" y="116" width="150" height="34"/>
    <rect x="160" y="156" width="160" height="34"/><rect x="330" y="156" width="110" height="34"/>
    <rect x="450" y="156" width="110" height="34"/><rect x="570" y="156" width="150" height="34"/>
    <rect x="160" y="196" width="160" height="34"/><rect x="330" y="196" width="110" height="34"/>
    <rect x="450" y="196" width="110" height="34"/><rect x="570" y="196" width="150" height="34"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="240" y="58">uidx 1–11</text><text x="385" y="58">uidx 12</text><text x="505" y="58">uidx 13</text><text x="645" y="58">uidx 14 … 32</text>
    <text x="240" y="98">ID 0x123</text><text x="385" y="98">RTR = d</text><text x="505" y="98">IDE = d</text><text x="645" y="98">r0, DLC …</text>
    <text x="240" y="138">ID 0x123</text><text x="385" y="138">RTR = r</text><text x="505" y="138">IDE = d</text><text x="645" y="138">r0, DLC …</text>
    <text x="240" y="178">ID-A 0x123</text><text x="385" y="178">SRR = r</text><text x="505" y="178">IDE = r</text><text x="645" y="178">ID-B …, RTR = d</text>
    <text x="240" y="218">ID-A 0x123</text><text x="385" y="218">SRR = r</text><text x="505" y="218">IDE = r</text><text x="645" y="218">ID-B …, RTR = r</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="98">표준 data</text><text x="10" y="138">표준 remote</text>
    <text x="10" y="178">확장 data</text><text x="10" y="218">확장 remote</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="252">표준 data vs 나머지: uidx 12 에서 RTR(d) 이 이김 (상대는 RTR = r 또는 SRR = r)</text>
    <text x="10" y="270">표준 remote vs 확장: uidx 12 는 r = r 동률 → uidx 13 에서 IDE(d) 가 이김</text>
    <text x="10" y="288">확장 data vs 확장 remote (같은 29비트 ID): uidx 32 RTR 에서 data 가 이김</text>
    <text x="10" y="306">칠한 칸 = 그 행이 지는 비트. 모두 solution.c race2() + 오라클로 확인.</text>
  </g>
</svg>
```

**모델 (주어짐).** 1 스텝 = 1 비트 시간. 한 스텝에서 ① 놀고 있는(IDLE) 노드가 큐에 메시지가 있으면 SOF 를 시작하고, ② 모든 노드가 이번 비트에 낼 값을 정하고(`node_drive`), ③ 버스 = AND, ④ **모든 노드가 같은 버스 비트를 샘플**한다 — 송신 중인 노드는 `tx_check` 로 비교, 그리고 모든 노드(송신 중이어도)가 COMM-M04 디코더를 비트 스트리밍으로 바꾼 수신기(디스터핑 + 필드 파싱 + CRC-15 + 고정 형식 검사)를 돌린다. 인코더(CRC-15/CAN 0x4599, 스터핑)는 **COMM-M04 모델 축약판**이고, 하네스가 CRC-15 check value `0x059E` 와 스터핑 위치를 먼저 확인한다. 모든 노드는 error-active 로만 동작하고 에러 카운터는 없다(TEC/REC·passive·bus-off 는 COMM-M07). 에러 처리는 최소한만: 에러를 본 노드는 다음 비트부터 dominant 6개(능동 에러 플래그) → recessive 가 보일 때까지 기다림 → recessive 8개(딜리미터) → IFS 3 → IDLE.

### Part A — wired-AND, 구동 비트, 송신기 판정

- `bus_resolve(drv, n)` — AND. 노드가 없으면 recessive.
- `node_drive(nd)` — 에러 플래그 중이면 0, 송신 중이면 이미지 비트 `img.bit[bitpos]`(ACK 슬롯은 1), ACK 슬롯에서 CRC 를 맞게 받은 수신기(listen-only 아님)는 0, 그 밖은 1.
- `tx_check(img, pos, sent, bus)` — `TXV_OK / LOST / BIT_ERR / STUFF_ERR / ACK_ERR`. 이미지의 `uidx[pos]` 가 −1 이면 스터프 비트, `1..arb_last`(표준 12, 확장 32) 면 중재 필드.
- 테스트: AND 전수, 판정 표(ID·RTR·표준 IDE·DLC·ACK·CRC delim·확장 SRR/IDE·스터프 비트), 그림의 3노드 경주가 비트 단위로 일치, 진 노드가 이긴 프레임을 받았는지.

### Part B — 패배와 에러의 처리, 고장 주입

- `tx_on_verdict(s, nd, v)` — **LOST**: 송신 중단(이 비트부터 수신기), `arb_lost`·`first_lost_uidx`·메시지 `losses` 기록, 메시지는 큐에 그대로(에러가 아니다). **에러**: 카운터·`last_err`·메시지 `errs` 증가, `start_error()`, 에러 재전송 상한(`max_err_retx`, 모델 정의 기본 3) 초과면 `finish_msg(false)`.
- 필수 비교: 낮은 ID, data vs remote(같은 ID), 표준 data vs 확장(SRR), 표준 remote vs 확장(IDE), 확장 data vs remote(RTR 32), base 가 낮은 확장 vs 표준.
- 전수 + 랜덤: 표준 ID 2048개마다 (a vs a^1), (a^0x400 vs a), 같은 base 의 확장 vs 표준 data, 표준 remote vs 확장 — 합쳐 2048 × 4 = 8192 경주. 2–6 노드 동시 시작 1500회(같은 base 를 일부러 섞음): 완료 순서 = 오라클 순서, 모든 노드가 남의 프레임을 순서대로 받음, k 번째 노드는 정확히 k−1 번 지고 첫 패배 위치 = 오라클의 첫 차이 비트, 총 비트 = Σ(len + 3), 에러 0개.
- 주입할 고장: ① 완전히 같은 프레임 둘 → 둘 다 성공, 버스엔 한 프레임 ② 같은 ID·DLC, 다른 데이터 → 중재 패배 0, 데이터 비트에서 BIT_ERR, 상한 후 둘 다 포기 ③ 같은 ID, 다른 DLC → DLC 에서 BIT_ERR ④ 혼자 / listen-only 만 있음 → ACK_ERR ⑤ 노이즈가 중재 중 recessive 비트를 dominant 로 → 송신기는 "졌다"고 믿고 물러남 → 수신기 스터프 에러 → 재시도 성공 ⑥ 노이즈가 중재 중 **스터프 비트**를 뒤집음 → LOST 가 아니라 STUFF_ERR.

### Part C — 큐 정책, 대기 시간, 굶주림

- `arb_key(f)` — 중재 비트열을 먼저 나가는 비트가 상위가 되게 32비트로 묶은 값(작을수록 이김). 랜덤 20000쌍에서 순서가 실제 중재(오라클)와 일치해야 한다.
- `txq_pick(nd)` — `Q_FIFO` 면 가장 오래된 것, `Q_PRIO` 면 `arb_key` 최소(동률이면 오래된 것). 하드웨어 송신 메일박스의 "ID 우선" 모드에 해당.
- 테스트: 노드 0 큐 [0x400, 0x010] + 노드 1 의 0x200 ×3 → FIFO 면 0x010 이 **맨 마지막**(우선순위 역전), PRIO 면 맨 처음. 완료 비트 시각 = 앞선 프레임 (len + 3) 합 + 자기 len − 1. 주기 부하: 가벼울 때 0x700 은 15/15 전송, 과부하(0x100–0x300 각 300비트 주기)에선 0x700 은 **0번** 나가고 큐가 넘쳐 7개 버림, 그래도 0x100 의 최대 대기 ≤ Lmax + 3 + L(0x100). 이벤트 로그는 브링업 엔지니어가 읽는 형식으로 찍는다.

## 2. Problem (English)

> **Simulate CAN bus arbitration at bit level.**
>
> You are given a CAN frame encoder (CRC-15, bit stuffing, fixed-form tail) and a bit-streaming receiver. Each bit time, every node chooses a level to drive (`0` = dominant, `1` = recessive), the bus resolves to the AND of all drivers, and every node samples the same bus bit.
>
> 1. Implement `uint8_t bus_resolve(const uint8_t *drv, int n)`, `uint8_t node_drive(const Node *nd)` and `TxVerdict tx_check(const TxImage *img, int pos, uint8_t sent, uint8_t bus)`. A transmitter that sends recessive but reads dominant **inside the arbitration field** (standard: ID + RTR, extended: ID-A + SRR + IDE + ID-B + RTR) has lost arbitration; any other mismatch is a bit error, a mismatch on a stuff bit is a stuff error, and a recessive ACK slot is an ACK error.
> 2. Implement `void tx_on_verdict(Sim *s, Node *nd, TxVerdict v)`. On loss, stop driving immediately, keep receiving (the loser must receive and ACK the winner's frame), keep the message queued and retry when the bus becomes idle (after EOF + 3-bit intermission). On an error, start an active error flag and give up after `max_err_retx` retransmissions.
> 3. Implement `uint32_t arb_key(const CanFrame *f)` (smaller key wins arbitration, valid across standard/extended and data/remote) and `int txq_pick(const Node *nd)` for FIFO and priority queues.
>
> Example: nodes sending standard IDs `0x065`, `0x063`, `0x070` start together. `0x070` loses at ID bit 4 (bit time 8, after one stuff bit), `0x065` loses at ID bit 2 (bit time 10); frames complete in the order `0x063` (bit 45), `0x065` (bit 94), `0x070` (bit 144).
>
> Follow-ups: standard vs extended with the same 11-bit base? Two nodes using the same ID? Why don't stuff bits break arbitration? What bounds the latency of the highest-priority message, and why can a lower-priority message starve?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여섯 군데: Part A `bus_resolve`, `node_drive`, `tx_check` / Part B `tx_on_verdict` / Part C `arb_key`, `txq_pick`. 인코더, 수신기, 큐, `finish_msg`, `start_error`, 시뮬레이터 루프, 오라클, 이벤트 로그는 주어진다. 스텁은 모두 recessive 를 내고 모든 판정이 OK 라서 송신기들은 "각자 혼자인 것처럼" 끝까지 달리고(멈추지 않음), 수신기는 SOF 를 영영 못 본다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:455  good == 8
FAIL starter.c:460  tx_check(&im, 3, 1, 0) == TXV_LOST
…
3-node race, first 16 bits (SOF..RTR..IDE):
  node0 065  1111111111111111
  node1 063  1111111111111111
  node2 070  1111111111111111
  BUS        1111111111111111
t=45    node0 TX OK      id=0x065  errs=0 losses=0 wait=45
t=45    node1 TX OK      id=0x063  errs=0 losses=0 wait=45
t=46    node2 TX OK      id=0x070  errs=0 losses=0 wait=46
…
exhaustive std races: 8192 runs, 8192 bad
random races: 1500 trials, bad order=1303 rx=6012 lost=4512 bits=1500, errors=0
…
16/66 passed
```

(exit 1. 통과한 16개는 주어진 모델 검사 2개와 "에러 없음"처럼 스텁이 우연히 맞는 항목들.)

## 4. 힌트

<details><summary>힌트 1 — AND 는 1 에서 시작</summary>

`uint8_t b = 1;` 에서 모든 드라이버를 `&=`. 아무도 없거나 모두 recessive 면 1. CAN 에 "1 을 강하게 내는" 노드는 없다 — recessive 는 *구동하지 않음*이다.

</details>

<details><summary>힌트 2 — node_drive 는 우선순위가 있는 if 4줄</summary>

에러 플래그(0) → 송신 중(이미지 비트) → ACK 슬롯의 정상 수신기(0) → 나머지(1). 송신기는 자기 ACK 슬롯에 1 을 낸다(이미지에 이미 1). 송신기가 자기 프레임에 ACK 하면 혼자 있어도 ACK 에러가 안 나는 버그가 된다.

</details>

<details><summary>힌트 3 — tx_check 의 순서가 답이다</summary>

① `pos == ack_pos` 면 bus 0 이 정상, 1 이면 ACK_ERR (sent 와 비교하지 않는다). ② `sent == bus` 면 OK. ③ 스터프 비트(uidx −1)면 STUFF_ERR. ④ `sent == 1` 이고 `1 ≤ uidx ≤ arb_last` 면 LOST. ⑤ 나머지 BIT_ERR. ④ 에서 `sent == 1` 조건을 빼면 "dom 냈는데 rec" 같은 하드웨어 고장을 패배로 숨긴다.

</details>

<details><summary>힌트 4 — 진 노드는 아무것도 리셋하지 않는다</summary>

`tx_active = false` 만 하면 다음 비트부터 `node_drive` 가 1 을 내고, 수신기는 SOF 부터 계속 같은 버스 비트를 봐 왔으니 그대로 이긴 프레임을 받는다. 수신 상태를 초기화하면 이긴 프레임을 놓치고 ACK 도 못 낸다. 메시지는 큐에서 빼지 않는다 — `maybe_start_tx()` 가 IFS 뒤 IDLE 에서 다시 고른다.

</details>

<details><summary>힌트 5 — arb_key 는 "비트를 그대로 이어 붙인 수"</summary>

표준: `ID[10:0]` 을 비트 31..21, RTR 을 20, IDE(0)를 19, 나머지 0. 확장: ID-A 31..21, SRR(1) 20, IDE(1) 19, ID-B 18..1, RTR 0. 사전식(lexicographic) 비트 비교 = 정수 비교가 되도록 **먼저 나가는 비트를 상위에** 두면 된다. 29비트 ID 를 숫자로 비교하면 틀린다(7. 실수 1).

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 |
|---|---|
| 1. 물리 | dominant 는 구동, recessive 는 안 구동 → 버스 = AND. 모든 노드가 같은 비트 시간 안에 같은 레벨을 본다(비트 시간 ≫ 전파 지연 왕복 — Q6) |
| 2. 규칙 | 송신기는 낸 비트를 읽는다. 중재 필드에서 r 냈는데 d → 패배 → 즉시 수신기. 그 밖 불일치 → 에러 |
| 3. 결과 | 처음 다른 비트에서 0 을 낸 쪽이 이긴다 = 비트열의 사전식 최소 = 같은 형식이면 낮은 ID. 이긴 프레임은 안 깨진다 |
| 4. 모서리 | 표준 vs 확장(SRR/IDE), data vs remote(RTR), 같은 ID 금지, 스터프 비트가 왜 안전한가 |
| 5. 시스템 | 우선순위 = 고정 우선순위 비선점 스케줄링. 큐가 FIFO 면 역전, 과부하면 높은 ID 굶주림. ID 배정 = 짧은 주기·마감에 낮은 ID |

### Part A — 3노드 경주를 비트 단위로

그림의 트레이스를 표로 읽으면 (t = SOF 로부터의 버스 비트, 스터프 포함):

| t | 필드 | 0x065 | 0x063 | 0x070 | BUS | 일어난 일 |
|---|---|---|---|---|---|---|
| 0–4 | SOF, ID[10..7] | 0 0 0 0 0 | 0 0 0 0 0 | 0 0 0 0 0 | 0 0 0 0 0 | dominant 5개 |
| 5 | 스터프 | 1 | 1 | 1 | 1 | 셋 다 같은 위치에 같은 스터프 |
| 6–7 | ID[6..5] | 1 1 | 1 1 | 1 1 | 1 1 | |
| 8 | ID[4] (uidx 7) | 0 | 0 | **1** | 0 | 0x070: r 냈는데 d → **LOST** |
| 9 | ID[3] | 0 | 0 | (1) | 0 | 괄호 = 이미 놓음 |
| 10 | ID[2] (uidx 9) | **1** | 0 | (1) | 0 | 0x065 **LOST** |
| 11–12 | ID[1..0] | (1) | 1 1 | (1) | 1 1 | 0x063 혼자 |
| 13 | RTR | (1) | 0 | (1) | 0 | data 프레임 |

`tx_check` 하나로 이 표가 나온다. 진 두 노드는 비트 8, 10 부터 `node_drive` 가 1 이지만 수신기는 SOF 부터 계속 돌았으니 0x063 프레임을 끝까지 받고, CRC 가 맞으므로 ACK 슬롯에 0 을 낸다 — 이긴 0x063 의 `tx_check` 가 ACK 슬롯에서 OK 를 받는 이유가 바로 **진 노드들의 ACK** 다. 실제 로그:

```text
t=8     node2 LOST    id=0x070  at uidx 7 -> receiver, retry at next idle
t=10    node0 LOST    id=0x065  at uidx 9 -> receiver, retry at next idle
t=45    node1 TX OK      id=0x063  errs=0 losses=0 wait=45
t=57    node2 LOST    id=0x070  at uidx 7 -> receiver, retry at next idle
t=94    node0 TX OK      id=0x065  errs=0 losses=1 wait=94
t=144   node2 TX OK      id=0x070  errs=0 losses=2 wait=144
```

0x063 의 프레임은 46비트(SOF..EOF, 스터프 2개 포함)라 비트 45 에 끝나고, IFS 3비트(46–48) 뒤 비트 49 에 두 노드가 **다시 동시에** SOF 를 낸다 — 둘 다 같은 프레임을 같은 비트에 끝까지 봤으므로 IDLE 로 돌아오는 비트도 같다. 둘째 판에서 0x070 은 같은 uidx 7 에서 또 진다(49 + 8 = 57). 0x070 은 비트 144 에 끝나고 IFS 까지 합치면 148비트 = (46+3) + (46+3) + (47+3) = Σ(len + 3) — 테스트가 매 경주마다 이 식으로 버스 시간을 검사한다 (프레임 길이 46/46/47 은 따로 짠 python 인코더로도 같게 나왔다). 버스가 한 비트도 놀지 않았다는 뜻이다: **CAN 중재는 대역폭을 낭비하지 않는다.**

**왜 스터프 비트가 중재를 안 깨나.** 스터핑은 "지금까지 보낸 비트"만의 함수다. 아직 중재 중인 송신기들은 정의상 SOF 부터 지금까지 버스와 똑같은 비트를 냈으므로(다르면 이미 졌다) 같은 위치에 같은 스터프 비트를 넣는다. 수신기도 버스 비트만 보고 같은 규칙으로 빼낸다. 그래서 중재 중 스터프 비트의 불일치는 "경쟁자" 때문일 수 없고 노이즈·고장이다 → `tx_check` 가 LOST 가 아니라 STUFF_ERR 를 낸다 (ISO 11898-1 은 이 경우를 스터프 에러로 보고 TEC 예외를 둔다 — 확인 권장, COMM-M07). 랜덤 1500회에서 스터프·비트 에러가 0 인 것이 이 성질의 실험적 확인이다.

**오라클과 전수.** 하네스는 학생 코드와 독립인 `oracle_first_diff()` 로 두 프레임의 비스터프 비트를 uidx 1 부터 비교한다(표준이 끼면 IDE 13 까지, 둘 다 확장이면 RTR 32 까지). 처음 다른 위치 d 에서 0 인 쪽이 이기고, 진 쪽의 `first_lost_uidx == d` 여야 한다. 표준 ID 는 2048개뿐이라 "인접 ID(a vs a^1 → 마지막 ID 비트 uidx 11 에서 갈림)", "최상위 비트만 다름(uidx 1)", "같은 base 의 확장 vs 표준 data/remote" 를 모두 돌린다:

```text
exhaustive std races: 8192 runs, 0 bad
random races: 1500 trials, bad order=0 rx=0 lost=0 bits=0, errors=0
```

### Part B — 상태 머신과 고장

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m05arr" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">노드 상태 머신 (송신기 + 항상 도는 수신기)</text>
  <rect x="20" y="130" width="110" height="44" rx="8" class="f-mut" stroke="currentColor"/>
  <rect x="190" y="40" width="170" height="44" rx="8" class="f-acc" stroke="currentColor" opacity=".9"/>
  <rect x="440" y="40" width="170" height="44" rx="8" class="f-ok" stroke="currentColor"/>
  <rect x="190" y="230" width="170" height="44" rx="8" class="f-ok" stroke="currentColor"/>
  <rect x="420" y="130" width="210" height="44" rx="8" class="f-warn" stroke="currentColor"/>
  <rect x="440" y="230" width="170" height="44" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="75" y="157">IDLE</text>
    <text x="275" y="67">송신: 중재 필드</text>
    <text x="525" y="67">송신: 제어 ~ EOF</text>
    <text x="275" y="257">수신기 (+ ACK)</text>
    <text x="525" y="157">에러 플래그 d×6 → 딜리미터 r×8</text>
    <text x="525" y="257">IFS r×3</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m05arr)">
    <line x1="110" y1="130" x2="190" y2="70"/>
    <line x1="110" y1="174" x2="190" y2="240"/>
    <line x1="275" y1="84" x2="275" y2="228"/>
    <line x1="360" y1="62" x2="438" y2="62"/>
    <line x1="525" y1="84" x2="525" y2="128"/>
    <line x1="360" y1="240" x2="430" y2="176"/>
    <line x1="360" y1="262" x2="438" y2="262"/>
    <line x1="525" y1="174" x2="525" y2="228"/>
    <polyline points="610,62 690,62 690,252 612,252"/>
    <polyline points="525,274 525,305 75,305 75,176"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="95">큐 있음 → SOF</text>
    <text x="40" y="222">남의 SOF</text>
    <text x="283" y="140">LOST: r 냈는데 d</text>
    <text x="283" y="156">(이 비트부터 수신,</text>
    <text x="283" y="172">메시지는 큐에 그대로)</text>
    <text x="366" y="54">중재 통과</text>
    <text x="533" y="110">BIT / STUFF / ACK</text>
    <text x="368" y="200">STUFF/FORM/CRC</text>
    <text x="368" y="290">EOF: 수신 확정</text>
    <text x="620" y="54">EOF: TX OK</text>
    <text x="533" y="210">딜리미터 끝</text>
    <text x="150" y="322">IFS 끝 → IDLE: 큐에 남은 메시지(진 것·에러 난 것)를 다시 SOF — 같은 비트에 다 같이</text>
  </g>
</svg>
```

`tx_on_verdict` 의 두 갈래가 핵심이다. **LOST 는 에러가 아니다** — 카운터(`errs`)도, 에러 플래그도 없고, 버스에선 아무 일도 없었다. 메시지는 큐에 남아 다음 IDLE 에 자동 재전송된다(실제 컨트롤러의 automatic retransmission). 에러는 반대로 에러 플래그(6 dominant — 일부러 스터프 규칙을 어겨 **모든** 노드가 프레임을 버리게 만든다)를 내고, 메시지별 `errs` 가 상한을 넘으면 앱에 실패를 보고한다.

**고장 ② 같은 ID, 다른 데이터.** 0x100 DLC 2, data[0] = 0x5A vs 0x52 (bit3 만 다름). ID·RTR·DLC 가 같으니 중재는 아무도 안 진다 — 둘 다 "이겼다"고 믿고 데이터 필드까지 간다. 실제 로그:

```text
t=25    node0 BIT_ERR   id=0x100  at pos 25 (uidx 23) -> error flag
t=27    node1 BIT_ERR   id=0x100  at pos 27 (uidx 25) -> error flag
t=30    node2 RX STUFF  at frame bit 30 -> error flag
t=73    node0 BIT_ERR   id=0x100  at pos 25 (uidx 23) -> error flag
…
t=169   node0 TX ABORTED id=0x100  errs=4 losses=0 wait=169
t=171   node1 TX ABORTED id=0x100  errs=4 losses=0 wait=171
```

uidx 23 = 19 + 4 = data[0] 의 bit3 (MSB 먼저). node0 은 1 을 냈는데 0 을 읽었다 — 중재 필드 밖이므로 LOST 가 아니라 BIT_ERR. node0 의 에러 플래그(t 26–31)가 node1 의 다음 recessive 비트(t 27, data[0] bit1)를 덮어 node1 도 BIT_ERR, 수신기 node2 는 dominant 6개째(t 30)에서 스터프 에러. 세 플래그가 겹쳐 버스는 t 26–36 동안 dominant(11비트, 겹친 에러 플래그는 6–12비트 — ISO 확인 권장), 딜리미터 37–44, IFS 45–47, **t 48 에 둘이 또 동시에 SOF** → 같은 위치(48 + 25 = 73)에서 또 충돌. 결정적으로 반복되므로 재전송 상한이 없으면 영원히 돈다. 실제 칩에서는 매번 TEC += 8 이 쌓여 둘 다 error-passive 를 거쳐 bus-off 로 끊긴다(COMM-M07). 결론: **한 ID 는 한 노드만 보낸다** 는 설계 규칙은 중재 알고리즘이 강제하는 것이다.

**고장 ⑤ 노이즈가 중재 비트를 덮음.** 0x555 를 혼자 보내는데 t=1(ID[10], recessive)에 버스를 강제로 dominant 로 만들면, 송신기는 진짜 경쟁자와 구별할 수 없으므로 "졌다"며 물러난다. 이제 아무도 구동하지 않아 버스는 recessive 만 이어지고, 수신기들은 recessive 6개째(t=7)에서 스터프 에러 → 에러 프레임 → 재시도 성공:

```text
t=1     node0 LOST    id=0x555  at uidx 1 -> receiver, retry at next idle
t=7     node0 RX STUFF  at frame bit 7 -> error flag
t=7     node1 RX STUFF  at frame bit 7 -> error flag
t=78    node0 TX OK      id=0x555  errs=0 losses=1 wait=78
```

"유령에게 진" 송신기의 프레임이 사라지지 않는 이유는 스터핑 덕분이다: 송신기가 없는 프레임은 6비트 안에 반드시 규칙을 어긴다. 같은 노이즈가 스터프 비트(고장 ⑥, 0x000 의 t=5)에 오면 `tx_check` 가 STUFF_ERR 를 내고 에러 1회 후 재전송 성공. 고장 ④ — 혼자 있거나 수신기가 모두 listen-only(STM32 SILM, Linux `listen-only on` 같은 모드 — 확인 권장)면 ACK 슬롯이 recessive → ACK_ERR 4회 후 포기. 실제 버스 브링업에서 "노드 하나만 연결했더니 TX 가 계속 에러" 의 정체가 이것이다.

### Part C — 우선순위 = 비선점 고정 우선순위 스케줄링

`arb_key` 가 있으면 중재 전체가 "**버스가 놀게 되는 순간, 대기 중인 모든 노드의 맨 앞 메시지 중 key 최소가 나간다**" 로 요약된다. 이미 나가고 있는 프레임은 끝까지 간다(비선점). 그래서 RTOS 의 고정 우선순위 스케줄링(RTOS-M11)과 같은 분석이 된다:

- 최고 우선순위 메시지의 최악 대기 = 막 시작한 남의 프레임 1개(블로킹, 최대 Lmax 비트) + IFS 3 + 자기 프레임. 테스트의 상한 `max_wait ≤ Lmax + 3 + L` 이 이것이고, 과부하에서도 성립한다 (light 111, heavy 221 ≤ 112 + 3 + 112 = 227).
- 그 아래 우선순위는 자기보다 key 가 작은 메시지들의 간섭이 더해진다. 부하가 100% 를 넘으면 맨 아래는 **영원히** 못 나간다.
- 노드 안 큐가 FIFO 면, 그 노드의 급한 메시지가 자기 앞의 느긋한 메시지 뒤에 줄을 서고, 그 느긋한 메시지가 **다른 노드의 중간 우선순위**에 계속 진다 → 우선순위 역전(RTOS-M03 의 Mars Pathfinder 와 같은 구조).

Part C 테스트의 FIFO 로그 (노드 0 = [0x400, 0x010], 노드 1 = 0x200 ×3):

```text
t=1     node0 LOST    id=0x400  at uidx 1 -> receiver, retry at next idle
t=112   node1 TX OK      id=0x200  errs=0 losses=0 wait=112
t=117   node0 LOST    id=0x400  at uidx 1 -> receiver, retry at next idle
t=227   node1 TX OK      id=0x200  errs=0 losses=0 wait=227
t=232   node0 LOST    id=0x400  at uidx 1 -> receiver, retry at next idle
t=343   node1 TX OK      id=0x200  errs=0 losses=0 wait=343
t=458   node0 TX OK      id=0x400  errs=0 losses=3 wait=458
t=573   node0 TX OK      id=0x010  errs=0 losses=0 wait=573
```

버스에서 가장 급한 0x010 이 다섯 번째로 573비트(500 kbit/s 에서 1146 µs) 만에 나간다. PRIO 면 첫 번째(111비트). 0x010 은 한 번도 중재에서 지지 않았다 — **버스가 아니라 노드 안 큐에서 진 것**이라 버스 분석기로 보면 안 보인다는 점이 실무 함정이다. 하드웨어 대응: STM32 bxCAN 은 송신 메일박스 3개 사이 우선순위를 ID 순(TXFP=0) 또는 요청 순(TXFP=1)으로 고를 수 있다(RM 확인 권장). 메일박스보다 많은 메시지를 소프트웨어 FIFO 로 받으면 결국 같은 문제가 생기므로, 드라이버 큐도 ID 순으로 두거나 더 급한 것이 오면 낮은 우선순위 메일박스를 abort 하는 방식을 쓴다(Di Natale 외 — 확인 권장).

`demo_latency.c` 실측 (Apple clang 21, 200000 비트 = 500 kbit/s 0.4 s, 표준 DLC 8 프레임 = 112비트). 실험 1: 0x100/0x200/0x300 을 같은 주기 P 로, 0x700 은 2000비트마다, 모두 PRIO 큐. U = Σ(len + 3)/주기.

| P (비트) | U | 0x100 최대 대기 | 0x300 전송/버림, 최대 대기 | 0x700 전송/버림, 최대 대기 |
|---|---|---|---|---|
| 2000 | 0.23 | 111 | 100/0, 327 | 100/0, 445 |
| 600 | 0.63 | 111 | 333/0, 327 | 100/0, 445 |
| 450 | 0.82 | 187 | 444/0, 403 | 100/0, 445 |
| 400 | 0.92 | 171 | 500/0, 387 | 100/0, 445 |
| 360 | 1.02 | 221 | 555/0, 437 | 73/20, **22085** |
| 340 | 1.07 | 221 | 563/19, 2757 | **0/92**, — |
| 300 | 1.21 | 221 | 406/254, 3932 | 0/92, — |
| 250 | 1.44 | 221 | 139/653, 11342 | 0/92, — |

0x100 의 최대 대기는 부하와 무관하게 221 비트에서 멈춘다(블로킹 1프레임 + 자기). 0x700 은 U ≈ 1.0 에서 갑자기 무너진다(대기 445 → 22085 비트, 그리고 0). 큐 용량 8 이라 100 − 8 = 92개를 버린다. 위상이 고정된 주기 부하라 평균 = 최대인 행이 많다 — 실제 버스는 지터가 있어 더 퍼진다. 실험 2: 노드 0 이 급한 0x080(2000비트마다, 위상은 고정 시드 랜덤) + 0x600 B개 묶음(1000비트마다), 노드 1·2 배경 부하:

| B | U | 0x080 FIFO 평균 / 최대 | 0x080 PRIO 평균 / 최대 |
|---|---|---|---|
| 1 | 0.65 | 147.1 / 312 | 144.7 / 226 |
| 2 | 0.77 | 197.4 / 469 | 152.2 / 226 |
| 3 | 0.89 | 292.2 / 668 | 166.7 / 226 |
| 4 | 1.00 | 734.6 / 1323 | 172.7 / 225 |

PRIO 의 최대는 B 와 무관하게 225–226 에 고정(상한 Lmax + 3 + L = 113 + 3 + 113 = 229 이내), FIFO 는 노드 안 대기열 길이만큼 늘어난다. 정확한 최악 응답 시간 공식(Tindell 1994, Davis 외 2007 수정판: R = J + w + C, w = B + Σ⌈(w + J + τ)/T⌉·C)과 버스 부하 계산은 COMM-M08 에서 다룬다(식 형태는 확인 권장).

### 모델이 빼먹은 것

- 비트 시간 안의 동기화(hard/re-sync, SJW), 샘플 포인트, 전파 지연: 이 모델은 "모든 노드가 같은 비트를 본다"를 가정한다. 그 가정이 성립하는 조건이 COMM-M06 비트 타이밍과 Q6 이다.
- 버스 통합(bus integration: 켜진 노드는 recessive 11개를 본 뒤 참여), IFS 3번째 비트의 dominant 를 SOF 로 받는 규칙, 과부하 프레임, 수신기가 EOF 마지막 직전 비트에서 프레임을 확정하는 규칙 — 모델은 EOF 마지막 비트에서 확정한다(ISO 확인 권장).
- error-passive 의 recessive 에러 플래그, TEC/REC, bus-off → COMM-M07. CAN FD 의 BRS 이후 구간은 중재와 무관(중재는 항상 nominal 속도) → COMM-M08.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 66/66 passed (0.06 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o so_asan && ./so_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -O2 demo_latency.c -o demo_latency && ./demo_latency    # 5. 풀이의 두 표
```

## 7. 자주 하는 실수

**1. 29비트 ID 를 숫자로 비교해 승자를 예측한다.** 표준 0x7FF 와 확장 0x00000001(base 0) 을 숫자로 비교하면 표준이 작아 보이지만, 실제로는 uidx 1 에서 확장(base 0)이 이긴다. 표준과 확장이 섞이면 "ID 숫자"가 아니라 **비트열**을 비교해야 한다 → `arb_key()` 처럼 표준 ID 를 상위 11비트에 정렬하고 SRR/IDE 를 끼워 넣는다.

**2. "읽은 값 ≠ 낸 값" 이면 무조건 LOST.** dominant 를 냈는데 recessive 를 읽는 것은 경쟁으로 일어날 수 없다(AND 버스) — 트랜시버·배선 고장이다. 패배로 처리하면 고장 노드가 조용히 물러나 문제가 숨는다. `sent == 1 && bus == 0` 만 LOST.

**3. 중재 구간을 "ID 비트"로만 잡는다.** RTR(표준 uidx 12, 확장 32)이 빠지면 data vs remote 경쟁이 BIT_ERR 가 되고, 확장 노드의 SRR/IDE 가 빠지면 표준 vs 확장 경쟁이 에러 프레임 폭주가 된다. 반대로 표준 노드의 구간을 IDE 까지 늘려도 해는 없지만(표준은 IDE 에 0 을 낸다) 의미가 틀린다.

**4. 스터프 비트 불일치를 LOST 로 본다.** 중재 중 스터프 비트는 모든 경쟁자가 같으므로 불일치는 노이즈다. LOST 로 처리하면 송신기가 물러나고 아무도 프레임을 끝내지 않는다 — 결국 스터프 에러로 복구되긴 하지만 "누가 에러를 냈는가" 진단이 틀어진다.

**5. 진 노드가 수신기를 리셋한다.**

```c
case TXV_LOST: nd->tx_active = false; nd->st = ST_IDLE;   /* 틀림: SOF 를 다시 기다림 */
```

이긴 프레임의 나머지 비트에서 dominant 를 SOF 로 오인하고, 0x063 프레임을 못 받고, ACK 도 안 낸다(마지막 남은 경쟁자 둘이면 이긴 쪽이 ACK 에러!). 진 노드는 `tx_active = false` 만 한다.

**6. 패배를 에러로 센다.** `m->errs++` 를 LOST 에도 하면 바쁜 버스에서 낮은 우선순위 메시지가 "재전송 상한 초과"로 버려지고, 실제 칩의 TEC 를 흉내 낸다면 정상 중재만으로 error-passive 가 된다. ISO 에서 중재 패배는 에러가 아니다.

**7. 송신 큐를 FIFO 로 둔다.** 드라이버의 소프트웨어 FIFO(또는 Linux 기본 qdisc 의 FIFO 동작 — 확인 권장)는 노드 안에서 우선순위를 뒤집는다. 5. 풀이 실험 2 의 B=4 에서 급한 0x080 의 최대 대기 1323 vs 225 비트.

## 8. 면접 Q&A

<details><summary>Q1. CAN 에서 왜 낮은 ID 가 이기나요?</summary>

dominant = 0 이 recessive = 1 을 덮는 AND 버스이고, ID 는 MSB 부터 나간다. 모든 송신기는 낸 비트를 읽어 보고, 중재 필드에서 1 을 냈는데 0 이 읽히면 물러난다. 그러면 처음 갈리는 비트에서 0 을 가진 쪽이 남고, MSB 부터 비교해 처음 0 이 나오는 쪽 = 숫자가 작은 쪽이다. 이긴 프레임은 한 비트도 안 바뀌어서(비파괴) 대역폭 손실이 없다. 꼬리: "그럼 모두 동시에 시작해야 하나?" — 노드는 bus idle(IFS 뒤)에만 SOF 를 시작하고, 거의 동시에 시작한 노드들은 SOF 엣지에 하드 싱크해 같은 비트 경계를 갖는다. 늦게 온 노드는 버스가 busy 라 시작하지 않고 수신기가 된다.

</details>

<details><summary>Q2. 표준 ID 0x123 과 확장 ID (base 0x123) 가 겨루면? 표준 remote 라면?</summary>

uidx 1–11 은 같다. uidx 12 는 표준의 RTR(data 면 dominant) vs 확장의 SRR(항상 recessive) → 표준 data 가 이긴다. 표준 remote 면 RTR 도 recessive 라 동률, uidx 13 에서 표준 IDE(dominant) vs 확장 IDE(recessive) → 표준이 이긴다. 즉 같은 base 면 **표준은 형식에 관계없이 확장을 이긴다**. SRR 을 recessive 로 둔 이유가 바로 이것(표준 data 프레임과의 호환 우선순위)이다. 확장끼리는 ID-B 까지 비교하고, 같은 29비트면 RTR(32) 에서 data 가 이긴다. solution.c 의 `race2()` 가 이 넷을 비트 위치까지 검사한다.

</details>

<details><summary>Q3. 두 노드가 같은 ID 로 보내면 어떻게 되나요?</summary>

프레임이 완전히 같으면 아무 일도 없다 — 둘 다 끝까지 보내고 버스엔 한 프레임, 둘 다 성공(테스트 ①). 데이터나 DLC 가 다르면 중재는 아무도 지지 않은 채 데이터/제어 필드까지 가서, recessive 를 낸 쪽이 bit error → 에러 플래그 → 둘 다 재전송 → 같은 자리에서 또 충돌 — 결정적으로 반복된다(로그: 48비트 주기). 실제 칩에서는 TEC 가 매번 +8 되어 결국 bus-off(COMM-M07). 그래서 시스템 설계 규칙은 "data 프레임의 한 ID 는 한 노드만 송신"이다. remote 프레임은 여러 노드가 같은 ID 로 요청할 수 있는데, DLC 까지 같아야 겹쳐도 무해하다(연습 2 — DLC 가 다르면 DLC 비트에서 bit error).

</details>

<details><summary>Q4. 비트 스터핑이 중재를 망가뜨리지 않나요?</summary>

아니다. 스터프 비트는 지금까지 보낸 비트로만 정해지는데, 아직 중재 중인 송신기들은 지금까지 버스와 같은 비트를 보냈으므로 같은 위치에 같은 스터프 비트를 넣는다(그림 1 의 회색 열). 그래서 중재 중 스터프 비트의 불일치는 경쟁이 아니라 노이즈이고, 규격도 그것을 스터프 에러로 다룬다(전송 에러 카운터 예외 조항 — ISO 11898-1 확인 권장). 반대로 송신기가 사라진 프레임(노이즈에 "진" 경우)은 recessive 만 남아 6비트 안에 스터프 규칙을 어기므로 모든 수신기가 버린다 — 스터핑은 동기화용이면서 에러 검출 장치다.

</details>

<details><summary>Q5. 중재에서 진 노드의 메시지는 어떻게 되나요? 소프트웨어가 뭘 해야 하나요?</summary>

컨트롤러가 그 비트부터 수신 모드로 바꿔 이긴 프레임을 받고(필터에 맞으면 RX FIFO 로), 버스가 idle 이 되면 자동으로 재전송한다. 소프트웨어는 보통 할 일이 없다. 진단용으로는 STM32 bxCAN 의 TSR ALSTx 비트, SJA1000 의 ALC(arbitration lost capture, 진 비트 위치) 레지스터, Linux 에서는 `CAN_ERR_LOSTARB` 에러 프레임(data[0] = 비트 위치)과 `ip -details -statistics link show can0` 의 arbitration-lost 카운터로 볼 수 있다(이름·형식은 확인 권장). one-shot 모드(STM32 NART 등)에서는 진 메시지도 재전송하지 않으므로 앱이 다시 넣어야 한다.

</details>

<details><summary>Q6. 비트 단위 중재가 물리적으로 성립하려면 무엇이 필요하죠? 왜 1 Mbit/s 에선 버스가 짧아야 하나요?</summary>

송신기는 같은 비트 시간 안에, 샘플 포인트 전에, 버스 끝에 있는 다른 노드의 dominant 를 봐야 한다. 내 SOF 엣지가 상대에게 가는 시간 + 상대 비트가 돌아오는 시간 + 트랜시버·컨트롤러 지연(왕복 전파 지연)이 샘플 포인트 앞 구간(PROP_SEG)에 들어가야 한다. 비트 시간이 1 µs 인 1 Mbit/s 에서는 왕복이 수백 ns 안이어야 하므로 버스 길이가 대략 40 m 정도로 제한되고(대표값), 125 kbit/s 면 수백 m 까지 간다. 이 모델의 "모든 노드가 같은 비트를 본다" 가정이 깨지면 중재가 안 되며, PROP_SEG/PS1/PS2 배분은 COMM-M06 에서 계산한다.

</details>

<details><summary>Q7. 낮은 우선순위 메시지의 지연은 어떻게 보장하나요? 굶어 죽을 수 있나요?</summary>

CAN 은 비선점 고정 우선순위 스케줄러다. 최고 우선순위의 최악 대기는 "막 시작한 낮은 우선순위 프레임 1개 + 자기 프레임"으로 부하와 무관하다(실측 221 비트에서 고정). 낮은 우선순위는 상위 메시지들의 간섭을 더 받고, 부하가 100% 를 넘으면 맨 아래는 영원히 못 나간다(실측: U 1.02 에서 0x700 최대 대기 22085 비트, U 1.07 부터 0번). 보장은 응답 시간 분석(Tindell, Davis 외 2007 수정판 — COMM-M08)으로 하고, ID 는 마감·주기가 짧은 메시지에 낮은 값을 준다(deadline-monotonic, 연습 4). 실무 경험칙으로 버스 부하는 여유를 크게 남긴다(흔히 50–70% 이하 권장 — 대표값).

</details>

<details><summary>Q8. 송신 큐 구현에서 생기는 우선순위 역전은 무엇이고 어떻게 막나요?</summary>

드라이버가 소프트웨어 FIFO 하나로 메시지를 컨트롤러에 넣으면, 노드 안에서 급한 메시지가 앞의 느긋한 메시지 뒤에 서고, 그 느긋한 메시지는 다른 노드의 중간 우선순위에 계속 진다. 버스에서는 아무 이상이 없어 보여서 찾기 어렵다(Part C 로그: 0x010 이 다섯 번째, 573 비트). 막는 법: ① 큐를 ID 순 우선순위 큐로, ② 하드웨어 메일박스를 ID 우선 모드(bxCAN TXFP=0)로, ③ 더 급한 메시지가 오면 메일박스의 가장 낮은 우선순위 메시지를 abort 후 교체, ④ 노드마다 급한 메시지 전용 메일박스. RTOS 의 우선순위 역전(RTOS-M03)과 같은 구조라 설명이 쉽다.

</details>

## 9. 추가 연습

### 연습 1 — 진 위치를 필드 이름으로

중재 패배의 uidx 를 사람이 읽는 필드 이름으로 바꾸는 `lost_field_name(ext, uidx, out, cap)` 를 짜라: 표준 1–11 → `ID[10]`…`ID[0]`, 12 → `RTR`; 확장 1–11 → `ID[28]`…`ID[18]`, 12 → `SRR`, 13 → `IDE`, 14–31 → `ID[17]`…`ID[0]`, 32 → `RTR`; 그 밖 −1, 출력 용량 부족 −1. 2노드 경주로 `ID[4]`, `SRR`, `IDE`, ID-B 비트, `RTR` 이 나오는지 확인.

<details><summary>답 보기</summary>

표준 `ID[11 − uidx]`, 확장 ID-A `ID[29 − uidx]`, ID-B `ID[31 − uidx]`. 실행 결과 `ex1: std 0x065 vs 0x070 -> loser lost at ID[4]`. 브링업 로그에서 "LOST at SRR" 이 자주 보이면 같은 base 의 표준·확장이 섞여 있다는 뜻이고, 같은 노드가 "LOST at ID[x]" 를 끝없이 내면 그 노드 ID 가 너무 높게 배정된 것이다. SJA1000 의 ALC 레지스터나 Linux `CAN_ERR_LOSTARB` 의 비트 위치도 같은 용도다(번호 매기는 기준은 데이터시트 확인).

</details>

### 연습 2 — 같은 ID 의 remote 프레임 둘

두 노드가 같은 ID 0x3A0 으로 remote 프레임을 보낸다. ① DLC 가 같을 때(2, 2) ② 다를 때(2, 4) 무슨 일이 생기는지 예측하고 테스트로 보여라.

<details><summary>답 보기</summary>

① remote 프레임엔 데이터가 없으니 두 프레임의 비트열이 완전히 같다 → 둘 다 성공, 수신기는 1개 받음, 버스 시간 = 한 프레임 + 3. ② DLC 0010 vs 0100 → uidx 16 에서 DLC=4 쪽이 1 을 내고 0 을 읽음 → 중재 밖이므로 BIT_ERR, 둘 다 4번 실패 후 포기, 수신기 0개 (`ex2: remote DLC2 vs DLC4 -> node1 BIT_ERR at uidx 16, receiver got 0 frames`). 그래서 remote 프레임의 DLC 는 응답할 data 프레임의 DLC 와 맞추라고 한다(규격 권고 — 확인 권장). CAN FD 에는 remote 프레임이 없다.

</details>

### 연습 3 — 완료 시각을 식으로 예측

K 개 노드가 같은 비트에 각자 프레임 하나로 시작하면, 프레임은 `arb_key` 오름차순으로 나간다. `predict_done(fr, k, done_t, order)` 로 각 프레임의 완료 비트를 닫힌 식으로 구하고 랜덤 400회 시뮬레이션과 대조하라. 0x065/0x063/0x070 (DLC 8) 예에서 마지막 프레임은 500 kbit/s 에서 몇 µs 에 끝나나?

<details><summary>답 보기</summary>

정렬 후 r 번째 프레임: `done = Σ_{j<r}(L_j + 3) + L_r − 1` (SOF 를 0 번 비트로). 400회 전부 일치. 예: 길이 112/113/115 → 0x063 은 비트 112, 0x065 는 227, 0x070 은 345 → (345 + 1) × 2 µs = 692 µs (`ex3` 실제 출력). 이 식이 성립한다는 것 = 버스가 경쟁 중에도 한 비트도 놀지 않는다는 것. 단 길이 L 은 스터프 비트 수 때문에 데이터마다 다르다(최악 스터핑 식은 COMM-M04).

</details>

### 연습 4 — ID 배정: 주기가 짧을수록 낮은 ID

4개 노드가 주기 500/1000/2000/5000 비트로 DLC 8 메시지를 보내고 t=0 에 동시 출발한다(가장 나쁜 위상). 500비트 주기 제어 메시지의 마감을 250비트로 할 때, 잘못된 배정(제어 메시지 = 0x400, 가장 높은 ID)과 deadline-monotonic 배정 `assign_ids_dm()` 의 최악 대기를 비교하라.

<details><summary>답 보기</summary>

`assign_ids_dm` = 주기가 짧은 순서(동률은 인덱스 순)로 base + rank × step. 실행: `ex4: control msg worst wait: wrong IDs 456 bits, deadline-monotonic 111 bits (bound 227)` — 잘못 배정하면 t=0 에 다른 세 프레임 뒤로 밀려 마감 250 을 넘기고, 바르게 배정하면 블로킹 상한 이내다. 버스 부하는 둘 다 같다(≈ 0.42) — **부하가 낮아도 ID 배정이 틀리면 마감을 놓친다**. 실제 차량·로봇 네트워크에서는 메시지 사전(DBC 등)에서 ID 를 정하므로 이 원칙이 설계 리뷰 항목이다(RTOS-M11 의 rate/deadline-monotonic 과 같은 논리).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 17/17 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_asan && ./pr_asan   # 오류 없음
```

## 10. 핵심 정리

- 버스 = AND (dominant 0 이 이김). 송신기는 매 비트 자기 비트를 읽는다. **중재 필드에서 r 냈는데 d → 패배 → 그 비트부터 수신기**, 그 밖의 불일치는 bit/stuff/ACK 에러.
- 승자 = 비스터프 비트열의 사전식 최소 = 같은 형식이면 낮은 ID. 이긴 프레임은 안 깨지고, 버스는 한 비트도 놀지 않는다 (총 시간 = Σ(len + 3)).
- 중재 필드: 표준 ID + RTR (uidx 1–12), 확장 ID-A + SRR + IDE + ID-B + RTR (1–32). 같은 base 면 표준이 이김(data 는 RTR 12 에서, remote 는 IDE 13 에서), data 가 remote 를 이김.
- 스터프 비트는 중재 중인 모든 송신기에서 같다 → 중재 중 스터프 불일치는 노이즈(STUFF_ERR). 송신기가 사라진 프레임은 6비트 안에 스터프 위반으로 버려진다.
- 진 노드는 아무것도 리셋하지 않는다: 이긴 프레임을 받고 ACK 하고, 메시지는 큐에 둔 채 IDLE 에서 자동 재시도. 패배는 에러가 아니다.
- 같은 ID 를 두 노드가 보내면 중재가 못 가른다 → 데이터에서 bit error 무한 반복 → TEC → bus-off (M07). 한 ID = 한 송신 노드.
- CAN = 비선점 고정 우선순위. 최고 우선순위 대기 ≤ Lmax + 3 + L (실측 221 ≤ 227), 과부하에서 높은 ID 는 굶는다(U ≈ 1.02 에서 붕괴). 노드 안 FIFO 큐는 우선순위 역전 → ID 순 큐/메일박스.

## 참고자료

- ISO 11898-1:2015 *Road vehicles — Controller area network (CAN) — Part 1: Data link layer and physical signalling* — 중재 필드, 비트 모니터링, 스터핑, 에러 플래그·딜리미터, 중재 중 스터프 에러의 예외 (유료 문서, 이 레슨의 규격 문장은 기억 기반 — 원문 확인 권장)
- Bosch, *CAN Specification Version 2.0* (1991), Part A/B — SRR/IDE 배치와 "표준이 같은 base 의 확장보다 우선" 설명
- Linux kernel docs, *SocketCAN — Controller Area Network*: <https://docs.kernel.org/networking/can.html> 와 `include/uapi/linux/can/error.h` (`CAN_ERR_LOSTARB`) — COMM-M09 에서 실제로 쓴다
- K. Tindell, A. Burns, A. Wellings, "Calculating Controller Area Network (CAN) message response times", *Control Engineering Practice* (1995; 1994 IFAC 워크숍판) — 최악 응답 시간 분석
- R. Davis, A. Burns, R. Bril, J. Lukkien, "Controller Area Network (CAN) schedulability analysis: Refuted, revisited and revised", *Real-Time Systems* 35 (2007)
- M. Di Natale, H. Zeng, P. Giusto, A. Ghosal, *Understanding and Using the Controller Area Network Communication Protocol*, Springer (2012) — 드라이버 송신 큐·메일박스와 우선순위 역전
- ST, STM32F4 Reference Manual RM0090 의 bxCAN 장 — 송신 메일박스 3개, CAN_MCR TXFP/NART/ABOM, CAN_TSR ALSTx/TERRx (비트 이름·위치 RM 확인 권장)
- NXP, *SJA1000 Stand-alone CAN controller* 데이터시트 — Arbitration Lost Capture 레지스터
- 관련 레슨: COMM-M04 (프레임·스터핑·CRC-15), COMM-M06 (비트 타이밍·전파 지연), COMM-M07 (에러 카운터·bus-off), COMM-M08 (CAN FD·버스 부하), COMM-M09 (SocketCAN), COMM-B06 / COMM-M02 (I2C wired-AND·중재), RTOS-M03 (우선순위 역전), RTOS-M11 (rate-monotonic·RTA)
