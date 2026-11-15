---
id: COMM-M14
time: 90분
updated: 2026-11-11
---
# COMM-M14 재시도·타임아웃·백오프: stop-and-wait ARQ

## 학습 목표

- 손실·중복·손상·지연(재정렬 포함)·링크 단절을 주입하는 **결정적 링크 시뮬레이터** 위에서 시퀀스 번호 + ACK + 타임아웃 재전송(stop-and-wait ARQ, Automatic Repeat reQuest)을 짜고, "OK 인데 안 감 0 / 두 번 감 0 / 순서 역전 0" 불변식을 수천 개 메시지로 검사한다.
- 결함마다 무엇이 일어나는지 틱 단위로 말한다: DATA 손실, ACK 손실(→ 수신 쪽 중복), RTO 보다 늦은 ACK(→ 쓸데없는 재전송), 복제, 손상(CRC 실패 = 손실), 전부 손실(→ 정확히 `max_retries + 1` 번 보내고 FAIL), 링크 down(→ LINK_DOWN 분류).
- 1비트 교대 시퀀스(alternating bit)가 **재정렬/긴 지연** 링크에서 옛 ACK 를 새 DATA 의 ACK 로 받아 **조용히 메시지를 잃는** 고전 버그를 재현하고, 필요한 seq 폭을 링크 지연 범위로부터 계산한다.
- 고정 / 지수(+cap, +지터) / 적응형(RFC 6298 SRTT + 4·RTTVAR, Karn 규칙) 타임아웃을 손실률 0–50% 에서 측정해 **직접 유도한 이론식과 비교**하고, 언제 어떤 정책이 맞는지 숫자로 말한다.

## 1. 문제

센서 허브 ↔ 메인 MCU, 드론 ↔ 지상국 무선 모뎀, 부트로더 펌웨어 업데이트처럼 "보낸 명령이 **정확히 한 번** 도착해야 하는" 링크에서 면접관은 반드시 "ACK 가 없어지면요? 그럼 수신 쪽은 같은 명령을 두 번 받죠? 몇 번 재시도하고 언제 포기하죠?" 를 묻는다. 신뢰성은 링크가 아니라 **프로토콜이** 만든다 — 이 레슨은 그 가장 작은 형태다.

COMM-B10/M10 의 프레임 파서는 "깨진 프레임을 버린다" 까지만 한다. 버린 뒤 다시 받으려면 송신 쪽이 **몰랐다는 것을 알아야** 한다: 수신 쪽이 잘 받은 것마다 ACK 를 돌려주고, 송신 쪽은 ACK 가 일정 시간(RTO, retransmission timeout) 안에 안 오면 다시 보낸다. 다시 보내면 수신 쪽이 같은 것을 두 번 받을 수 있으므로 **시퀀스 번호(seq)** 로 중복을 걸러낸다. 이 세 가지(ACK, 타이머, seq)가 TCP, IEEE 802.15.4 MAC 재전송, BLE 링크 계층(1비트 SN/NESN), YMODEM 부트로더(BM-A05), MAVLink 미션 프로토콜까지 공통 뼈대다.

이 레슨의 모델 (전부 이 레슨이 정한 값):

| 항목 | 정의 |
|---|---|
| 시간 | 정수 틱. 기본 링크 지연 10 틱/방향 → RTT 20 틱 (예: 1 틱 = 1 ms 인 UART 무선 모뎀) |
| DATA 프레임 | `[0x01][SEQ][LEN][PAYLOAD × LEN][CRC_HI][CRC_LO]`, LEN ≤ 16 |
| ACK 프레임 | `[0x02][SEQ][CRC_HI][CRC_LO]` — SEQ = **확인하는 DATA 의 seq** |
| CRC | CRC-16/CCITT-FALSE (check 0x29B1, COMM-B10 축약판). 손상 = 비트 하나 뒤집기 → CRC 실패 → 버림 |
| seq | `seq_bits` 비트 (1..8), 메시지마다 +1 (mod 2^b) |
| 링크 결함 | 방향별 손실·복제·손상 확률(‰), 지연 = `delay + U[0, jitter]` (+ 확률 `late_pm` 로 `late_extra`), `up = false` 면 보내기 자체 거부 |
| 결과 분류 | `ARQ_OK`, `ARQ_FAIL_TIMEOUT` (TIMEOUT_EXHAUSTED), `ARQ_FAIL_LINKDOWN`, 그리고 손상으로 버린 프레임 수(CORRUPT_DROPPED) 카운터 |

```svg
<svg viewBox="0 0 760 340" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="m14a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">(a) ACK 손실 — 지연 10 틱/방향, RTO 30 (solution.c 이벤트 로그 그대로)</text>
  <g stroke="currentColor"><line x1="80" y1="60" x2="720" y2="60"/><line x1="80" y1="130" x2="720" y2="130"/></g>
  <text x="10" y="64" fill="currentColor">송신</text><text x="10" y="134" fill="currentColor">수신</text>
  <g stroke="currentColor" stroke-width="1.6" fill="none">
    <line x1="90" y1="60" x2="178" y2="128" marker-end="url(#m14a)"/>
    <line x1="180" y1="130" x2="225" y2="95"/>
    <line x1="360" y1="60" x2="448" y2="128" marker-end="url(#m14a)"/>
    <line x1="450" y1="130" x2="538" y2="62" marker-end="url(#m14a)"/>
  </g>
  <text x="217" y="100" fill="currentColor" font-size="14" font-weight="bold">✕</text>
  <g fill="currentColor">
    <text x="96" y="112">DATA s0</text><text x="232" y="94">ACK0 손실</text>
    <text x="150" y="150">t=10 전달 ✓</text>
    <text x="318" y="48">t=30 타임아웃</text><text x="284" y="112">재전송 try2</text>
    <text x="400" y="150">t=40 중복 → 버림, ACK0 다시</text>
    <text x="500" y="48">t=50 ACK0 → OK (전송 2회)</text>
  </g>
  <rect x="356" y="54" width="8" height="12" class="f-warn" stroke="currentColor"/>
  <text x="10" y="186" fill="currentColor" font-weight="bold" font-size="13">(b) ACK 가 RTO 보다 늦음 (+25 틱) — 쓸데없는 재전송 + stale ACK</text>
  <g stroke="currentColor"><line x1="80" y1="220" x2="720" y2="220"/><line x1="80" y1="290" x2="720" y2="290"/></g>
  <text x="10" y="224" fill="currentColor">송신</text><text x="10" y="294" fill="currentColor">수신</text>
  <g stroke="currentColor" stroke-width="1.6" fill="none">
    <line x1="90" y1="220" x2="178" y2="288" marker-end="url(#m14a)"/>
    <line x1="180" y1="290" x2="493" y2="222" stroke-dasharray="5,4" marker-end="url(#m14a)"/>
    <line x1="360" y1="220" x2="448" y2="288" marker-end="url(#m14a)"/>
    <line x1="450" y1="290" x2="538" y2="222" marker-end="url(#m14a)"/>
    <line x1="495" y1="220" x2="583" y2="288" marker-end="url(#m14a)"/>
    <line x1="585" y1="290" x2="673" y2="222" marker-end="url(#m14a)"/>
  </g>
  <rect x="356" y="214" width="8" height="12" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="96" y="272">DATA s0</text><text x="225" y="252">ACK0 (35 틱 걸림)</text>
    <text x="300" y="208">t=30 타임아웃 → 재전송</text>
    <text x="430" y="190">t=45 늦은 ACK0 → OK, DATA s1 시작</text>
    <text x="545" y="208">t=50 ACK0 또 → stale, 무시</text>
    <text x="150" y="310">t=10 전달</text><text x="400" y="310">t=40 중복 → 버림, ACK0</text><text x="560" y="310">t=55 s1 전달</text>
    <text x="690" y="240">t=65 OK</text>
  </g>
  <text x="10" y="332" fill="currentColor">ACK 에 seq 가 없으면 t=50 의 ACK0 를 s1 의 ACK 로 착각한다 — seq 를 ACK 에 싣는 이유.</text>
</svg>
```

### Part A — 기본 메커니즘: seq + ACK + 타이머

1. **A1** `seq_is_new(seq, last, bits)`: d = (seq − last) mod 2^b 가 1..2^(b−1) 이면 "새 메시지". (8가지 폭 × 모든 last × 모든 seq 전수 검사)
2. **A2** `arq_tx_on_frame()`: 손상 ACK → 버리고 카운트, seq 불일치 → stale(무시), 일치 → OK 완료.
3. **A3** `arq_timeout(t, attempt)`: FIXED = base, EXP = min(base·2^(attempt−1), cap), ADAPTIVE = 현재 RTO, + 지터 U[0, T·pct/100].
4. **A4** `arq_tx_poll()` 의 타임아웃 분기: 시도 < `max_retries + 1` 이면 재전송, 아니면 FAIL — 모든 시도가 링크에 거부됐으면 LINK_DOWN, 아니면 TIMEOUT_EXHAUSTED.
5. **A5** `arq_rx_on_frame()`: CRC 실패 → 버림(ACK 없음). 새 것 → 전달 + `last = seq`. 중복 → 전달 안 함. **둘 다 ACK(seq)**.
6. 테스트: 깨끗한 링크 200 메시지 = 정확히 200 프레임, 4001 틱(메시지당 RTT 20 틱, 빈틈 없음).

### Part B — 결함 주입과 고전 버그

| # | 주입 (링크 스크립트) | 기대 결과 (테스트로 단언) |
|---|---|---|
| B1 | DATA #0 손실 | 전송 2회, 수신 중복 0, t=40 전달 |
| B2 | ACK #0 손실 | 전달 **1번**, 수신 중복 1, ACK 2개 |
| B3 | ACK #0 지연 +25 (> RTO) | 재전송 1(쓸데없음), 중복 1, stale ACK 1, t=45 완료, RTT 샘플 0 (Karn) |
| B4 | DATA #0 복제 | 중복 1, stale ACK 1, 재전송 0 |
| B5 | DATA #0 손상 + ACK #0 손상 | 수신·송신 CRC 버림 각 1, 전송 3회, 전달 1번 |
| B6 | DATA 100% 손실 | **정확히 5회** 전송 후 TIMEOUT_EXHAUSTED: FIXED t=150, EXP t=690 |
| B7 | ACK 100% 손상 | FAIL 인데 **수신 쪽은 이미 받음** (fail_but_delivered 1, 중복 4) |
| B8 | 링크 down / t=45 에 up / up 이지만 전부 손실 | LINK_DOWN(거부 5, 링크 0개) / 3번째에 OK / 거부 2 + 손실 3 → TIMEOUT |
| B9 | FAIL 다음 메시지 (DATA 경로 죽음 / ACK 경로 죽음) | 두 경우 모두 다음 메시지가 정확히 1번 전달 |
| B10 | 1비트 seq, ACK0 +70 지연, C 의 첫 DATA 손실 | **조용한 손실 2개** (C, D 가 OK 보고됐는데 안 감). 8비트면 0 |
| B11 | `seq_bits_required()` + 지터 링크 10..40, RTO 25 | 1비트 위반 > 0, 계산값(3비트)에서 위반 0 |
| B12 | 무작위 4종 (손실 10–50%, 복제, 손상, 지터, 늦은 패킷, FAIL 섞임) × 3000 | 불변식 전부 유지 |

### Part C — 펌웨어 현실: RTT 추정, API, 로그

1. **C1** `rtt_sample()`: RFC 6298 SRTT/RTTVAR/RTO 를 Jacobson 정수 스케일(8·SRTT, 4·RTTVAR)로. Karn 규칙: **재전송한 메시지에서는 샘플을 안 받고**, 타임아웃마다 RTO 를 두 배로 올려 유효한 샘플이 올 때까지 유지.
2. 손으로 계산한 값 (50 → 150, 50 → 125, 90 → 152), 고정 RTT 50 에서 수렴값 53, 지터 링크(RTT 40..160)에서 고정 RTO 60 vs 적응형의 쓸데없는 재전송 수.
3. 앱 API: 4칸 큐 (`ARQ_EQUEUE_FULL`), payload 16 바이트 (`ARQ_EMSGSIZE`), 완료 콜백 정확히 한 번. 이벤트 로그(고정 64칸)를 출력해 bring-up 엔지니어가 읽는 모양으로.
4. `demo_arq.c`: 손실률 스윕(이론 비교), seq 폭 스윕, 단절(outage), 지터 링크 정책 비교.

## 2. Problem (English)

> **Stop-and-wait ARQ over a lossy link.** You have a byte-frame link between two MCUs that may drop, duplicate, corrupt (CRC fails), delay and reorder frames, and may go down. Implement:
>
> ```c
> int  arq_send(arq_tx *t, uint32_t now, const uint8_t *pl, uint8_t len, uint32_t tag); // 0, ARQ_EQUEUE_FULL, ARQ_EMSGSIZE
> void arq_tx_poll(arq_tx *t, uint32_t now);                                            // every tick
> void arq_tx_on_frame(arq_tx *t, uint32_t now, const uint8_t *b, size_t n);            // ACK in
> void arq_rx_on_frame(arq_rx *r, uint32_t now, const uint8_t *b, size_t n);            // DATA in
> ```
>
> Requirements: one message in flight; DATA carries a b-bit sequence number; ACK carries the sequence number it acknowledges; the sender retransmits on timeout (fixed, or exponential with cap, plus jitter) and after `max_retries` retransmissions reports `FAILED` exactly once, classified as `LINK_DOWN` (every attempt refused by the driver) or `TIMEOUT_EXHAUSTED`. The receiver delivers each message to the application **exactly once and in order** and re-ACKs duplicates. No dynamic allocation.
>
> Example (delay 10 ticks each way, RTO 30, ACK of the first transmission lost): `TX s0 @0 → deliver @10 → timeout @30 → RETX s0 @30 → duplicate @40 (not delivered, re-ACK) → ACK ok @50`.
>
> Follow-ups: (1) Your link can delay a frame longer than the RTO. Is a 1-bit sequence number enough? Prove a bound on the sequence space. (2) Derive goodput vs. loss rate p and compare with simulation. (3) How do you pick the RTO? What happens to your RTT estimate when you retransmit? (4) The sender reports FAILED — was the message delivered?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 일곱 개: A1 `seq_is_new`, A2 `arq_tx_on_frame`, A3 `arq_timeout`, A4 `arq_tx_poll` 의 타임아웃 분기, A5 `arq_rx_on_frame`, B1 `seq_bits_required`, C1 `rtt_sample`. 링크 시뮬레이터(`chan_send`/`chan_recv`, 스크립트 결함), 프레임 인코더/파서, 큐(`arq_send`), `tx_transmit`/`tx_finish`, 하네스(`w_run`, 불변식 검사 `w_verdict`)는 주어진다. 스텁은 "아무것도 안 받음 / 첫 타임아웃에 바로 FAIL" 이라 모든 루프가 금방 끝난다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:455  bad == 0
FAIL starter.c:457  seq_is_new(0, 255, 8) && !seq_is_new(3, 5, 8) && seq_is_new(133, 5, 8) && !seq_is_new(134, 5, 8)
FAIL starter.c:465  arq_timeout(&t, 1) == 30 && arq_timeout(&t, 2) == 60 && ...
…
B12 stress 0: ok=0 fail=3000 (fail_but_delivered=0) retx=0 rx_dup=0 corrupt rx/tx=0/0 stale_acks=0 ticks=90001
…
13/54 passed
```

(exit 1.) 단계별 확인: A1–A5 만 채우면 **44/54** (B1 의 하한 계산 3개 + C 의 RTT 테스트 7개가 실패), B1 까지 **47/54**, C1 까지 **54/54**.

## 4. 힌트

<details><summary>힌트 1 — 수신 쪽은 "새 것인가?" 하나만 판단하고, ACK 는 무조건</summary>

중복 DATA 가 왔다는 것은 **송신 쪽이 내 ACK 를 못 받았다**는 뜻이다. 그러니 중복에도 ACK 를 다시 보내야 송신이 멈춘다 (안 보내면 송신은 재시도를 다 쓰고 FAIL 한다). "새 것인가" 는 `last` (마지막으로 전달한 seq) 와의 거리 d = (seq − last) mod 2^b 로 정한다. 1비트면 d = 1 만 새 것 = 교대 비트(alternating bit). 손상된 프레임은 seq 를 믿을 수 없으니 ACK 도 보내지 않는다.

</details>

<details><summary>힌트 2 — 송신 쪽: ACK 의 seq 가 "지금" 과 같을 때만</summary>

`!busy` 거나 `v.seq != t->seq` 면 지난 메시지의 ACK(복제, 지연, 재전송에 대한 재ACK) — `stale_acks++` 하고 끝. 같으면 `tx_finish(t, now, ARQ_OK)`. 손상 ACK 는 버리고 `corrupt_dropped++`, 지금 메시지의 `rep.corrupt_acks++` — 재전송은 타이머가 알아서 한다.

</details>

<details><summary>힌트 3 — 재시도 횟수는 "전송 횟수" 로 세면 off-by-one 이 없다</summary>

`max_retries = 4` 면 전송은 최대 5번. 타임아웃이 났을 때 `rep.attempts >= max_retries + 1` 이면 FAIL, 아니면 `tx_transmit()`. 분류는 `rep.refused == rep.attempts` (한 번도 링크에 못 실었다) → LINK_DOWN. 한 번이라도 실렸으면 상대가 죽었는지 링크가 나쁜지 모르므로 TIMEOUT_EXHAUSTED. 시간 비교는 `(int32_t)(now - deadline) >= 0` — 32비트 틱이 넘어가도 안전하다.

</details>

<details><summary>힌트 4 — 지수 백오프는 곱하기 루프 + cap</summary>

`base << (attempt − 1)` 은 attempt 가 32 이상이면 UB 다 (테스트에 attempt 40 이 있다). `T < cap` 인 동안만 두 배로 하고 마지막에 cap 으로 자른다. 지터는 `T += xs32() % (T·pct/100 + 1)` — 범위 [T, T + T·pct/100].

</details>

<details><summary>힌트 5 — seq 폭 하한: "송신은 seq 하나에 RTTmin 이상 머문다"</summary>

seq 를 하나 넘기려면 그 seq 의 ACK 가 와야 하고(최소 dmin_f + dmin_r = RTTmin), 마지막으로 보낸 그 seq 의 DATA 는 다음 seq 가 시작하기 **전**에 나갔다. 그 DATA(또는 그에 대한 ACK) 가 링크에서 최대 dmax 만큼 살아 있는 동안 송신이 몇 칸 나아갈 수 있나? 수신 쪽(DATA, 최대 지연 차이 dmax_f − dmin_f)과 송신 쪽(ACK, DATA+ACK 수명 dmax_f + dmax_r)을 따로 세라.

</details>

<details><summary>힌트 6 — RTT 추정은 정수 스케일로</summary>

`srtt8 = 8·SRTT`, `rttvar4 = 4·RTTVAR` 로 두면 α = 1/8, β = 1/4 가 시프트가 된다: `err = R − srtt8/8; srtt8 += err; rttvar4 += |err| − rttvar4/4`. RTO = SRTT + max(G, 4·RTTVAR) = `srtt8/8 + max(1, rttvar4)`. 첫 샘플은 SRTT = R, RTTVAR = R/2 → `rttvar4 = 2R`. 그리고 Karn: 재전송한 메시지의 ACK 는 어느 전송에 대한 것인지 모르므로 샘플을 버린다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "신뢰성 = 검출(CRC) + 확인(ACK) + 재전송(타이머) + 중복 제거(seq)." 넷 중 하나라도 빠지면 무엇이 깨지는지 한 줄씩.
2. 송신 상태 머신 그림 → 수신 규칙 한 줄 → "ACK 가 사라지면 중복, 중복에도 ACK".
3. 재시도·백오프·실패 분류 → "FAIL 은 '안 갔다' 가 아니라 '모른다'".
4. seq 폭과 링크 수명 → 1비트가 깨지는 시나리오.
5. 숫자: goodput 이론식과 측정, RTO 정하는 법(RFC 6298 + Karn).

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="m14b" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="16" fill="currentColor" font-weight="bold" font-size="13">송신 상태 머신 (arq_tx) — M = max_retries + 1</text>
  <g stroke="currentColor">
    <rect x="20" y="70" width="120" height="44" rx="8" class="f-mut"/>
    <rect x="290" y="70" width="160" height="44" rx="8" class="f-acc"/>
    <rect x="600" y="34" width="140" height="40" rx="8" class="f-ok"/>
    <rect x="600" y="100" width="150" height="60" rx="8" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="13">
    <text x="58" y="97">IDLE</text><text x="330" y="97">WAIT_ACK</text>
    <text x="625" y="59">DONE: OK</text><text x="608" y="118">FAIL</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5" fill="none">
    <line x1="140" y1="92" x2="288" y2="92" marker-end="url(#m14b)"/>
    <line x1="450" y1="82" x2="598" y2="56" marker-end="url(#m14b)"/>
    <line x1="450" y1="104" x2="598" y2="126" marker-end="url(#m14b)"/>
    <path d="M330,70 C330,30 410,30 410,68" marker-end="url(#m14b)"/>
    <path d="M330,114 C330,160 410,160 410,116" marker-end="url(#m14b)"/>
    <path d="M740,54 H754 V205 H80 V118" marker-end="url(#m14b)"/>
    <line x1="670" y1="160" x2="670" y2="205"/>
  </g>
  <g fill="currentColor">
    <text x="146" y="84">큐에 메시지: seq 부여,</text><text x="146" y="108">전송, 타이머 T(1)</text>
    <text x="470" y="52">ACK(seq 일치)</text>
    <text x="460" y="148">타임아웃 ∧ 시도 = M</text>
    <text x="180" y="40">타임아웃 ∧ 시도 &lt; M:</text><text x="180" y="56">재전송, 타이머 T(k+1)</text>
    <text x="268" y="176">ACK 손상 / seq 불일치: 무시 + 카운트</text>
    <text x="250" y="222">seq ← seq + 1 (OK 든 FAIL 이든), 완료 콜백 정확히 한 번</text>
    <text x="608" y="136" font-size="11">전부 거부 → LINK_DOWN</text><text x="608" y="152" font-size="11">아니면 TIMEOUT_EXH.</text>
  </g>
  <text x="10" y="262" fill="currentColor" font-weight="bold" font-size="13">수신 규칙 (arq_rx) — d = (seq − last) mod 2^b</text>
  <g stroke="currentColor">
    <rect x="20" y="274" width="230" height="44" class="f-ok"/>
    <rect x="265" y="274" width="230" height="44" class="f-mut"/>
    <rect x="510" y="274" width="230" height="44" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="28" y="292">1 ≤ d ≤ 2^(b−1): 새 것</text><text x="28" y="310">→ 전달, last = seq, ACK(seq)</text>
    <text x="273" y="292">그 외 (d = 0 또는 뒤쪽 반): 중복</text><text x="273" y="310">→ 전달 안 함, ACK(seq) 다시</text>
    <text x="518" y="292">CRC/형식 실패: 손상</text><text x="518" y="310">→ 버림, ACK 없음, 카운트</text>
  </g>
</svg>
```

### Part A — 한 메시지의 일생 (그림 (a) 를 로그로)

`solution.c` 가 찍은 실제 이벤트 로그 (ACK #0 손실, 지연 10, RTO 30):

```text
  t=   0  TX             seq=0 try=1 rto=30
  t=  10  RX deliver     seq=0
  t=  30  TIMEOUT        seq=0
  t=  30  RETX           seq=0 try=2 rto=30
  t=  40  RX dup->reACK  seq=0
  t=  50  ACK ok         seq=0 try=2 rtt_from_start=50
  t=  50  DONE ok        seq=0 tries=2 corrupt_acks=0
```

설계 포인트 다섯 개:

- **ACK 는 "무엇을" 확인하는지 싣는다.** 그림 (b) 의 t=50 처럼 지난 메시지의 ACK 가 늦게 오는 일은 정상이다. seq 가 없으면 그걸 지금 메시지의 ACK 로 착각한다.
- **중복에도 ACK.** 수신 쪽 입장에서 중복 DATA = "내 ACK 가 안 갔다" 는 증거. 이 한 줄을 빼면 B2 에서 송신이 5번 보내고 FAIL 한다 (7. 자주 하는 실수 1).
- **손상 = 손실.** CRC 가 깨진 프레임은 seq 도 믿을 수 없으니 그냥 버린다. NACK 를 보내면 복구가 빨라지지만(연습 거리), NACK 도 사라질 수 있으므로 **타이머가 최후의 보루**라는 구조는 그대로다. CRC-16/CCITT-FALSE 는 단일 비트 오류를 전부 잡는다 (테스트: "hello" 프레임 80비트 전수, 미검출 0).
- **틱 안의 순서가 결정적이다.** 한 틱 = (1) 도착 DATA → 수신 (2) 도착 ACK → 송신 (3) 앱이 큐 채움 (4) `arq_tx_poll`. 같은 틱에 ACK 와 타임아웃이 겹치면 ACK 가 이긴다. 실제 펌웨어에서도 "RX 처리 후 타이머 검사" 순서를 고정해 두면 재현 가능한 로그가 나온다 (TEST-A05 결정적 시뮬레이션).
- **stop-and-wait 의 상한.** 깨끗한 링크 200 메시지 = 4001 틱 = 메시지당 정확히 RTT. 링크가 아무리 빨라도 처리량 ≤ 1 메시지/RTT — sliding window(COMM-A02)로 가는 이유.

### Part A — 백오프 세 가지

| attempt k | FIXED (30) | EXP (30, cap 240) | ADAPTIVE |
|---|---|---|---|
| 1 | 30 | 30 | RTO (RTT 추정) |
| 2 | 30 | 60 | 2·RTO (A4 가 두 배로) |
| 3 | 30 | 120 | 4·RTO |
| 4, 5, … | 30 | 240, 240 | … ≤ rto_cap |
| 전부 실패까지 (M = 5) | 150 | 690 | — |

`+ 지터 20%` 면 각 값에 U[0, 6] (T = 30 일 때) 이 더해진다 (테스트: 1000번 뽑아 최소 30, 최대 36).

### Part B — 실패 분류와 "FAIL ≠ 안 감"

B7 이 이 레슨의 가장 중요한 숫자다: ACK 가 전부 손상되면 송신은 5번 보내고 **FAIL** 을 보고하지만 수신 쪽은 첫 번째에 이미 **전달했다** (중복 4번은 걸러냄). 무작위 스트레스에서도 같은 일이 흔하다:

```text
B12 stress 0: ok=2998 fail=2 (fail_but_delivered=1) retx=1070 rx_dup=649 corrupt rx/tx=208/171 stale_acks=275 ticks=92121
B12 stress 1: ok=2992 fail=8 (fail_but_delivered=8) retx=3438 rx_dup=1515 corrupt rx/tx=92/67 stale_acks=147 ticks=497677
B12 stress 2: ok=3000 fail=0 (fail_but_delivered=0) retx=482 rx_dup=306 corrupt rx/tx=45/28 stale_acks=162 ticks=125494
B12 stress 3: ok=2039 fail=961 (fail_but_delivered=763) retx=5287 rx_dup=1327 corrupt rx/tx=0/0 stale_acks=0 ticks=228221
```

손실 50% (stress 3) 에서 FAIL 961 중 **763 개는 실제로 전달됐다**. 그러니 stop-and-wait ARQ 의 보장은:

- OK → 정확히 한 번 전달됨 (조용한 손실 0 — 모든 스트레스에서 검사).
- FAIL → **전달됐을 수도, 안 됐을 수도.** 앱이 같은 명령을 다시 보내려면 그 명령이 **멱등(idempotent)** 하거나 앱 계층 op_id 로 중복을 걸러야 한다 (연습 2, DIST-B05). "모터 +10° 회전" 은 위험하고 "모터를 37° 로" 는 안전하다.

분류 규칙: 모든 시도가 드라이버에서 거부(`LINK_EDOWN` — 예: UART TX 가 막힘, 무선 모뎀 연결 끊김, CAN bus-off(COMM-M07))면 LINK_DOWN — "상대는 아무것도 못 받았다" 가 **확실한** 유일한 경우다. 하나라도 링크에 실렸으면 TIMEOUT_EXHAUSTED. 손상으로 버린 ACK 수(`corrupt_acks`)도 보고에 넣으면 "상대는 살아 있고 응답도 하는데 링크가 시끄럽다" 와 "아무 응답 없음" 을 구분할 수 있다 (B7: corrupt_acks = 5).

**FAIL 뒤 seq 는 넘긴다.** 실패한 seq 를 다음 메시지에 재사용하면: 수신 쪽이 실패한 메시지를 받았던 경우(B9 b — ACK 경로만 죽음) 새 메시지가 "중복" 으로 버려진다 → 조용한 손실. 넘기면 두 경우 모두 d ∈ {1, 2} 라 새 것으로 받는다 (B9 a/b 테스트). 대가: 연속 FAIL 이 2^(b−1) 개를 넘으면 송신이 수신보다 반 바퀴 이상 앞서 이 규칙도 깨진다 — 8비트면 127번 연속 실패까지 안전하고, 그 이상이면 세션 재동기화(핸드셰이크)가 필요하다 (COMM-A11).

### Part B — 고전 버그: 1비트 seq + RTO 보다 오래 사는 ACK

```svg
<svg viewBox="0 0 760 320" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="m14c" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">1비트 seq (교대 비트): 메시지 A B C D E — 실제 로그를 그대로 그림 (x = 시간, 1 틱 = 5 px)</text>
  <g stroke="currentColor"><line x1="60" y1="110" x2="740" y2="110"/><line x1="60" y1="210" x2="740" y2="210"/></g>
  <text x="10" y="114" fill="currentColor">송신</text><text x="10" y="214" fill="currentColor">수신</text>
  <g stroke="currentColor" stroke-width="1.5" fill="none">
    <line x1="70" y1="110" x2="118" y2="208" marker-end="url(#m14c)"/>
    <line x1="120" y1="210" x2="518" y2="112" stroke-dasharray="5,4" marker-end="url(#m14c)"/>
    <line x1="220" y1="110" x2="268" y2="208" marker-end="url(#m14c)"/>
    <line x1="270" y1="210" x2="318" y2="112" marker-end="url(#m14c)"/>
    <line x1="320" y1="110" x2="368" y2="208" marker-end="url(#m14c)"/>
    <line x1="370" y1="210" x2="418" y2="112" marker-end="url(#m14c)"/>
    <line x1="420" y1="110" x2="445" y2="160"/>
    <line x1="520" y1="110" x2="568" y2="208" marker-end="url(#m14c)"/>
    <line x1="570" y1="210" x2="618" y2="112" marker-end="url(#m14c)"/>
    <line x1="620" y1="110" x2="668" y2="208" marker-end="url(#m14c)"/>
    <line x1="520" y1="80" x2="520" y2="106"/><line x1="660" y1="80" x2="620" y2="106"/>
  </g>
  <text x="440" y="168" fill="currentColor" font-size="14" font-weight="bold">✕</text>
  <g fill="currentColor">
    <text x="64" y="102">A s0</text><text x="190" y="102">A 재전송</text><text x="300" y="102">B s1</text><text x="398" y="102">C s0</text>
    <text x="98" y="230">A 전달</text><text x="240" y="230">A 중복</text><text x="350" y="230">B 전달</text>
    <text x="500" y="230">D(s1): last=1 → "중복"!</text><text x="650" y="248">E 전달</text>
  </g>
  <rect x="440" y="44" width="160" height="36" class="f-warn" stroke="currentColor"/>
  <text x="446" y="59" fill="currentColor">t=90 낡은 ACK0 → "C OK"</text><text x="446" y="74" fill="currentColor">→ D(s1) 시작</text>
  <rect x="610" y="44" width="130" height="36" class="f-warn" stroke="currentColor"/>
  <text x="616" y="59" fill="currentColor">t=110 re-ACK1</text><text x="616" y="74" fill="currentColor">→ "D OK" → E(s0)</text>
  <text x="10" y="276" fill="currentColor">점선 = A 의 첫 ACK0 (80 틱 지연, t=90 도착). 송신은 5개 전부 OK 보고, 수신은 A B E 만 받음</text>
  <text x="10" y="294" fill="currentColor">→ C, D 조용한 손실 (B10 테스트: silent_loss = 2). 8비트 seq 면 C = s2 라 t=90 의 ACK0 은 stale 로 무시,</text>
  <text x="10" y="312" fill="currentColor">t=100 타임아웃 재전송 → 5개 전부 정확히 한 번.</text>
</svg>
```

`solution.c` 가 B10 에서 찍는 1비트 로그의 핵심 부분 (실제 출력에서 발췌, 오른쪽 `←` 주석만 덧붙임):

```text
  t=  70  TX             seq=0 try=1 rto=30      ← C (링크가 버림)
  t=  90  ACK ok         seq=0 try=1 rtt_from_start=20   ← A 의 낡은 ACK 를 C 의 것으로!
  t=  90  DONE ok        seq=0 tries=1 corrupt_acks=0
  t=  90  TX             seq=1 try=1 rto=30      ← D
  t= 100  RX dup->reACK  seq=1                   ← 수신은 B(s1) 다음이라 D 를 중복으로 버림
  t= 110  DONE ok        seq=1 tries=1 corrupt_acks=0
```

덤으로 `rtt_from_start=20` — 낡은 ACK 로 RTT 샘플까지 받아 RTT 추정도 오염된다. 교대 비트 프로토콜은 **FIFO 링크**(순서를 바꾸지 않고, 한 번에 하나만 사는 링크)를 가정한다. UART 직결이나 BLE 링크 계층(1비트 SN/NESN — Core Spec 확인 권장)처럼 링크가 순서를 보장하면 충분하지만, 중간에 큐·재전송하는 무선 모뎀, USB-시리얼 브리지 + OS 버퍼, 멀티패스 네트워크를 지나면 깨진다.

**seq 폭 하한 (이 레슨의 증명).** 메시지 k 의 첫 전송 시각을 t_k 라 하자. (i) t_{k+1} − t_k ≥ RTTmin = dmin_f + dmin_r (OK 는 진짜 ACK 가 와야 하고, FAIL 은 타임아웃 합 ≥ RTO ≥ RTTmin 가정). (ii) 메시지 j 의 DATA 사본은 전부 t_{j+1} **전에** 나갔다(같은 틱이면 ACK 처리가 먼저). 그러면:

- **송신 쪽 (낡은 ACK j):** 시각 s < t_{j+1} 에 나간 DATA j 의 ACK 는 s + dmax_f + dmax_r 까지 살 수 있다. 그때 송신이 메시지 n = j + K 에 있다면 t_n > s + (K−1)·RTTmin 이므로 K < (dmax_f + dmax_r)/RTTmin + 1, 즉 **K ≤ Ks = ⌈(dmax_f + dmax_r)/RTTmin⌉**. 일치 비교가 안 속으려면 1..Ks 중 2^b 의 배수가 없어야 한다 → **Ks < 2^b**.
- **수신 쪽 (낡은 DATA j):** 수신이 이미 j + K 를 전달했다면 그 DATA 는 t_{j+K} + dmin_f 이후에 도착했고, 낡은 j 는 s + dmax_f 이전에 도착해야 하므로 K < (dmax_f − dmin_f)/RTTmin + 1 → **K ≤ Kr = ⌈(dmax_f − dmin_f)/RTTmin⌉**. 반-공간 규칙에서 "중복" 으로 보이려면 **Kr < 2^(b−1)**.

`seq_bits_required()` 는 두 조건을 만족하는 최소 b 다. 고정 지연(10, 10, 10, 10)이면 Kr = 0, Ks = 1 → **1비트** (교대 비트가 FIFO 에서 맞는 이유가 공식으로 나온다). 지연 10..40 이면 Kr = 2, Ks = 4 → 3비트. 10..160 이면 Kr = 8, Ks = 16 → 5비트. TCP 가 32비트 seq 와 "최대 세그먼트 수명(MSL)" 가정을 같이 두는 것과 같은 논리다: **seq 공간 > (링크 수명 / 송신이 seq 하나에 쓰는 최소 시간) + 여유**.

이 하한은 **충분조건**이다 (최악 정렬을 다 가정). `demo_arq.c` 의 실측 (손실 없음, RTO 25 로 쓸데없는 재전송을 일부러 많이, 20000 메시지, 위반 = 조용한 손실 + 중복 전달 + 순서 역전 수):

```text
  delay 10..40    required=3 :  b=1:9172  b=2:10  b=3:0  b=4:0  b=5:0  b=6:0
  delay 10..110   required=4 :  b=1:17497  b=2:1145  b=3:0  b=4:0  b=5:0  b=6:0
  10..20 +2% +300 required=6 :  b=1:6053  b=2:1217  b=3:171  b=4:4870  b=5:0  b=6:0
```

계산값 이상에서는 전부 0, 그 아래는 링크에 따라 깨지거나(b = 2, 3, 4) 운 좋게 0 이 나오기도 한다(지연 10..110 의 b = 3). 위반 수가 b 에 대해 단조가 아닌 것(b=3: 171, b=4: 4870)은 한 번 어긋나면 `last` 가 엉뚱한 값으로 가서 뒤따르는 메시지들이 연쇄로 틀리기 때문이다 — 그래서 "가끔 하나" 가 아니라 "한번 터지면 수천" 이다.

### Part B — goodput 이론식 유도와 실측

가정: 손실 p 가 DATA·ACK 각각 독립, 지연 d 고정(RTT = 2d), 타임아웃 T_k > RTT, 최대 M 번 전송. 시도 하나가 성공하려면 그 DATA 와 그 ACK 가 둘 다 살아야 하므로 q = (1 − p)². 앞 시도의 ACK 가 뒤늦게 오는 일은 없다(고정 지연, T > RTT). S_k = T_1 + … + T_k 라 하면 한 메시지가 송신을 차지하는 시간은

- n 번째 시도에서 성공 (확률 q(1−q)^(n−1)): S_{n−1} + RTT (ACK 받은 틱에 바로 다음 메시지 시작)
- M 번 전부 실패 (확률 (1−q)^M): S_M

이 주기들은 독립·동일 분포(renewal)이므로 **goodput = P(성공) / E[주기]** (renewal-reward 정리) = (1 − (1−q)^M) / [Σ_{n=1..M} q(1−q)^(n−1)(S_{n−1} + 2d) + (1−q)^M S_M]. FIXED 에 M → ∞ 이면 E[주기] = (1/q − 1)·T + RTT 로 줄어든다 (p = 0.3: q = 0.49, (1/0.49 − 1)·30 + 20 = 51.2 틱 → 1000/51.2 ≈ 19.52 — 아래 표의 이론값, 측정 19.53).

첫 전송 → 수신 전달 지연은 ACK 와 무관하다: 처음으로 DATA 가 살아남는 시도 j (확률 p^(j−1)(1−p)) 에서 S_{j−1} + d. 전달된 것만 조건부로 평균·분위수를 구한다.

`demo_arq.c` 실측 (지연 10/방향, 20000 메시지, max_retries 15 → M = 16, goodput = OK 메시지 / 1000 틱, 이상값 1000/RTT = 50):

| p | FIXED 30 측정 / 이론 | 전송/메시지 측정 / 이론 | EXP 30→240 측정 / 이론 | FAIL (F / E) |
|---|---|---|---|---|
| 0.00 | 50.00 / 50.00 | 1.00 / 1.00 | 50.00 / 50.00 | 0 / 0 |
| 0.10 | 36.90 / 36.99 | 1.24 / 1.23 | 34.45 / 34.39 | 0 / 0 |
| 0.20 | 27.13 / 27.12 | 1.56 / 1.56 | 19.95 / 19.81 | 0 / 0 |
| 0.30 | 19.53 / 19.52 | 2.04 / 2.04 | 9.98 / 10.00 | 0 / 0 |
| 0.40 | 13.68 / 13.64 | 2.77 / 2.78 | 4.88 / 4.83 | 16 / 16 |
| 0.50 | 9.17 / 9.09 | 3.93 / 3.96 | 2.38 / 2.34 | 204 / 204 |

(전체 11행은 `demo_arq.c` 출력; 측정과 이론 차이는 FIXED 최대 0.9%, EXP 최대 1.7%. FAIL 수가 두 정책에서 같은 것은 같은 시드 → 같은 손실 패턴이고 실패 여부는 타이밍과 무관하기 때문이다.)

```svg
<svg viewBox="0 0 760 340" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">goodput vs 손실률 p — 선 = 이론, 점 = demo_arq.c 실측 (20000 메시지)</text>
  <g stroke="currentColor" opacity="0.25">
    <line x1="90" y1="40" x2="690" y2="40"/><line x1="90" y1="90" x2="690" y2="90"/><line x1="90" y1="140" x2="690" y2="140"/>
    <line x1="90" y1="190" x2="690" y2="190"/><line x1="90" y1="240" x2="690" y2="240"/>
  </g>
  <g stroke="currentColor"><line x1="90" y1="290" x2="700" y2="290"/><line x1="90" y1="30" x2="90" y2="290"/></g>
  <g fill="currentColor">
    <text x="60" y="44">50</text><text x="60" y="94">40</text><text x="60" y="144">30</text><text x="60" y="194">20</text><text x="60" y="244">10</text><text x="66" y="294">0</text>
    <text x="84" y="308">0</text><text x="200" y="308">0.1</text><text x="320" y="308">0.2</text><text x="440" y="308">0.3</text><text x="560" y="308">0.4</text><text x="680" y="308">0.5</text>
    <text x="360" y="330">손실률 p (DATA, ACK 각각)</text>
    <text x="10" y="170">msg/1000틱</text>
  </g>
  <g fill="none" stroke="currentColor" stroke-width="1.6">
    <polyline points="90,40.0 150,74.8 210,105.0 270,131.4 330,154.4 390,174.6 450,192.4 510,208.1 570,221.8 630,233.9 690,244.6"/>
    <polyline stroke-dasharray="6,4" points="90,40.0 150,78.4 210,118.1 270,156.6 330,190.9 390,218.8 450,240.0 510,255.2 570,265.9 630,273.2 690,278.3"/>
  </g>
  <g class="f-acc" stroke="currentColor">
    <circle cx="90" cy="40.0" r="4"/><circle cx="150" cy="74.8" r="4"/><circle cx="210" cy="105.5" r="4"/><circle cx="270" cy="131.3" r="4"/><circle cx="330" cy="154.3" r="4"/><circle cx="390" cy="174.6" r="4"/><circle cx="450" cy="192.3" r="4"/><circle cx="510" cy="207.9" r="4"/><circle cx="570" cy="221.6" r="4"/><circle cx="630" cy="234.0" r="4"/><circle cx="690" cy="244.2" r="4"/>
  </g>
  <g class="f-warn" stroke="currentColor">
    <rect x="86" y="36.0" width="8" height="8"/><rect x="146" y="74.3" width="8" height="8"/><rect x="206" y="113.8" width="8" height="8"/><rect x="266" y="153.1" width="8" height="8"/><rect x="326" y="186.2" width="8" height="8"/><rect x="386" y="214.4" width="8" height="8"/><rect x="446" y="236.1" width="8" height="8"/><rect x="506" y="251.2" width="8" height="8"/><rect x="566" y="261.6" width="8" height="8"/><rect x="626" y="269.2" width="8" height="8"/><rect x="686" y="274.1" width="8" height="8"/>
  </g>
  <circle cx="480" cy="54" r="4" class="f-acc" stroke="currentColor"/><line x1="492" y1="54" x2="520" y2="54" stroke="currentColor" stroke-width="1.6"/>
  <text x="526" y="58" fill="currentColor">FIXED T = 30</text>
  <rect x="476" y="70" width="8" height="8" class="f-warn" stroke="currentColor"/><line x1="492" y1="74" x2="520" y2="74" stroke="currentColor" stroke-width="1.6" stroke-dasharray="6,4"/>
  <text x="526" y="78" fill="currentColor">EXP 30·2^(k−1), cap 240</text>
</svg>
```

지연 (첫 전송 → 전달, 틱) — 같은 실행:

| p | FIXED 평균 측정 / 이론 | FIXED p99 측정 / 이론 | EXP 평균 측정 / 이론 | EXP p99 측정 / 이론 |
|---|---|---|---|---|
| 0.10 | 13.4 / 13.3 | 70 / 40 | 13.8 / 13.7 | 100 / 40 |
| 0.20 | 17.5 / 17.5 | 70 / 70 | 19.7 / 19.8 | 100 / 100 |
| 0.30 | 22.9 / 22.9 | 100 / 100 | 30.7 / 30.4 | 220 / 220 |
| 0.40 | 30.0 / 30.0 | 130 / 160 | 49.4 / 49.5 | 460 / 700 |
| 0.50 | 39.7 / 40.0 | 190 / 190 | 84.0 / 84.9 | 940 / 940 |

p99 가 두 군데 어긋나는 것은 오류가 아니라 **분포가 계단**이라서다: 지연은 10, 40, 70, … (FIXED) 처럼 이산값만 가진다. p = 0.1 에서 P(지연 ≤ 40) = 1 − 0.1² = 0.99 **정확히** 경계라, 표본에서 1% 를 살짝 넘으면 다음 계단(70)으로 간다. p = 0.4 에서는 P(≤ 130) = 1 − 0.4⁵ = 0.98976 로 0.99 에 0.0002 모자라 이론은 160, 표본은 130. 꼬리 분위수를 비교할 때는 계단 경계를 먼저 본다.

**결론: 무작위 손실에서 지수 백오프는 손해다** — p = 0.3 에서 goodput 이 절반(19.5 → 10.0), p99 지연 2.2배. 지수 백오프의 목적은 **혼잡(congestion)·충돌**: 여러 송신자가 같은 매체를 쓰고 재전송 자체가 손실의 원인일 때 물러서는 것이다(이더넷 CSMA/CD, 802.11 의 contention window, TCP). 점대점 링크의 랜덤 비트 오류에는 물러설 이유가 없다. 지수 백오프가 이기는 곳은 **단절(outage)** 이다 (`demo_arq.c` 3번, 1000 메시지 중 t ∈ [5000, 8000) 양방향 down, max_retries 6):

| 정책 | FAIL link-down | 단절 중 거부된 전송 | 복구 후 첫 전달 |
|---|---|---|---|
| FIXED 30 | 14 | 100 | +10 틱 |
| EXP 30..240 | 2 | 19 | +40 틱 |
| EXP 30..240 + 지터 25% | 2 | 18 | +229 틱 |

FIXED 는 3000 틱 단절 동안 메시지 14개를 FAIL 로 버리고 무선 송신(전력!)을 100번 낭비한다. EXP 는 메시지당 재시도 예산(30+60+120+240·4 = 1170 틱)이 길어 2개만 버리지만, 링크가 돌아와도 자고 있던 만큼 늦게 깨어난다. 지터는 혼자일 때는 손해(+229)지만, **여러 노드가 같은 단절을 겪고 동시에 재시도할 때** 재시도 시각이 맞물려 다시 충돌하는 것(thundering herd)을 흩어 준다 — 이 단일 링크 모델에는 그 효과가 안 나타난다는 점까지 말할 수 있어야 한다.

### Part C — RTO 를 정하는 법: RFC 6298 + Karn

타임아웃이 RTT 보다 짧으면 쓸데없는 재전송, 길면 손실 복구가 느리다. RFC 6298 (Jacobson/Karels 알고리즘):

- 첫 샘플 R: SRTT = R, RTTVAR = R/2. 이후 R': RTTVAR ← (1−β)·RTTVAR + β·|SRTT − R'|, SRTT ← (1−α)·SRTT + α·R' (α = 1/8, β = 1/4, RTTVAR 를 옛 SRTT 로 먼저).
- RTO = SRTT + max(G, K·RTTVAR), K = 4. 처음(샘플 전)은 1 s, 하한 1 s 권장, 상한 ≥ 60 s (RFC 문구는 원문 확인 권장 — TCP 용 값이고, 이 레슨은 `rto_base`/`rto_min`/`rto_cap` 로 틱 단위 설정).
- 타임아웃마다 RTO ← 2·RTO (상한), **Karn 규칙**: 재전송한 메시지의 ACK 는 어느 전송에 대한 것인지 모호 → RTT 샘플로 쓰지 않고, 백오프된 RTO 를 유효한 샘플이 올 때까지 유지.

손으로 검증한 값 (테스트): 50 → SRTT 50, RTTVAR 25 → RTO 150. 다시 50 → RTTVAR = 0.75·25 = 18.75 → RTO 125. 90 → err 40, SRTT 55, RTTVAR = 0.75·18.75 + 0.25·40 = 24.06 → 실수 RTO 151.25, 정수 스케일 152. 고정 RTT 50 에서 60 샘플 뒤 RTO = **53**: `rttvar4` 는 100, 75, 57, 43, 33, 25, 19, 15, 12, 9, 7, 6, 5, 4, 3 으로 줄다가 `3 − (3 >> 2) = 3` 에서 멈춘다 — 정수 스케일의 바닥이 "분산 0 인 링크에서도 RTO 가 SRTT 보다 조금 크게" 남는 효과를 준다.

지터 링크(지연 20..80/방향, RTT 40..160, 손실 0, 2000 메시지, solution.c 출력):

```text
C  jitter RTT 40..160: fixed RTO 60 -> spurious(rx dup)=2306 ticks=199808 | adaptive -> spurious=8 ticks=200424 final rto=222 srtt=98
```

고정 60 은 메시지보다 많은 2306번 쓸데없이 재전송했고, 적응형은 8번. 그런데 `demo_arq.c` 4번 표가 보여 주는 반전:

| 손실 | 정책 | goodput | 전송/메시지 | 수신 중복 | p99 지연 |
|---|---|---|---|---|---|
| 0% | FIXED 60 | 10.03 | 2.15 | 5764 | 80 |
| 0% | ADAPTIVE | 9.95 | 1.00 | 17 | 80 |
| 10% | FIXED 60 | 8.77 | 2.40 | 5769 | 140 |
| 10% | ADAPTIVE | 5.73 | 1.23 | 582 | 656 |
| 30% | FIXED 60 | 6.14 | 3.20 | 6221 | 257 |
| 30% | FIXED 200 | 3.22 | 2.05 | 2175 | 672 |
| 30% | ADAPTIVE | 0.57 | 2.05 | 2175 | 8071 |

- 손실 0%: 적응형이 전송을 절반으로(2.15 → 1.00) — 전용 링크에서 쓸데없는 재전송은 시간은 거의 안 잡아먹지만 **airtime·전력**을 두 배로 쓰고, 공유 버스라면 남의 대역폭을 뺏는다.
- 손실 30%: 적응형이 **붕괴**(goodput 0.57, p99 8071). 원인: 타임아웃마다 두 배 + Karn "유효 샘플까지 유지" — 재전송이 흔하면 유효 샘플이 드물어 RTO 가 상한(4000)까지 올라간 채 다음 메시지로 넘어간다. TCP 가 무선 링크에서 겪는 고전적 문제와 같은 구조다. 연습 3 에서 "OK 로 끝나면 백오프를 리셋" 하면 goodput 2.2배, p99 1/5 로 좋아지지만 여전히 FIXED 60 보다 나쁘다 — 랜덤 손실 링크에서는 RTO 를 "RTT 분포의 높은 분위수 근처, 고정" 에 두고 재시도 횟수로 예산을 정하는 편이 낫다. 재전송분의 RTT 도 재고 싶으면 DATA 에 시도 번호(또는 타임스탬프)를 싣고 ACK 가 그대로 돌려주게 한다 (TCP timestamps, RFC 7323 — Karn 모호성 제거).

### Part C — 펌웨어에 넣을 때

- **API 경계:** 앱은 `arq_send()` (고정 4칸 큐, 꽉 차면 `ARQ_EQUEUE_FULL` — 앱이 backpressure 를 받는다)와 완료 콜백만 안다. 콜백 안에서 `arq_send()` 를 다시 불러도 된다(연습 2 의 재전송). 큐·프레임·로그 모두 정적, malloc 없음.
- **실행 문맥:** `arq_tx_poll()` 은 1 ms 타이머 틱(또는 RTOS 태스크의 타임아웃 대기)에서, `arq_tx_on_frame()`/`arq_rx_on_frame()` 은 프레임 파서(COMM-M10, DMA+IDLE 은 COMM-M11)가 완성 프레임을 넘기는 곳에서 부른다. 둘이 다른 문맥(타이머 ISR vs UART 태스크)이면 `busy/seq/rep` 를 동시에 고친다 → 같은 태스크로 직렬화하거나(이벤트 큐, RTOS-B 큐 레슨) 짧은 임계 구역으로 감싼다. ISR 에서는 "프레임 도착" 이벤트만 큐에 넣는 것이 정석 (COMM-M13 의 완료 콜백 구조와 같은 고민).
- **틱 비교:** `(int32_t)(now - deadline) >= 0` — 1 ms 틱 32비트는 49.7일에 넘어간다. `now >= deadline` 은 그날 멈춘다.
- **로그:** 위 이벤트 로그처럼 `t, 이벤트, seq, try, rto` 한 줄이면 현장에서 "ACK 는 오는데 seq 가 안 맞는다(stale 폭증 = 지연/재정렬)", "corrupt_acks 만 오른다(노이즈, baud 오차 — COMM-B03)", "TX refused(드라이버/링크 down)" 를 바로 가른다. 링크 down 로그 (실제 출력 일부):

```text
  t=   0  TX refused     seq=0 try=1 rto=30
  t=  30  TIMEOUT        seq=0
  …
  t= 120  TX refused     seq=0 try=5 rto=30
  t= 150  TIMEOUT        seq=0
  t= 150  FAIL link-down seq=0 tries=5 corrupt_acks=0
```

### 복잡도와 메모리

`sizeof(arq_tx)` = 288 B, `sizeof(arq_rx)` = 56 B (이 Mac, 64비트 포인터 기준 — Cortex-M 은 포인터 4 B 라 더 작다). 대부분 큐(4 × 28 B)와 프레임(21 B). 프레임마다 CRC 한 번(보낼 때 또는 받을 때) + 상수 시간. 링크 시뮬레이터의 `chan_recv` 는 32칸 선형 탐색 — 실제 펌웨어에는 없는 부분이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 54/54 passed, exit 0 (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -O2 demo_arq.c -o demo_arq && ./demo_arq                 # 약 0.5 초, 5. 풀이의 표 (Apple M6, Apple clang 21.0.0)
```

`solution.c` 실행 출력 중 테스트 외 줄 (로그 제외):

```text
B10 1-bit seq: ok=5 silent_loss=2 (msg2 delivered 0, msg3 delivered 0) dup_deliv=0
B11 jitter 10..40, seq_bits=1: ok=3000 silent_loss=382 dup_deliv=458 order_viol=541 retx=2153
B11 jitter 10..40, seq_bits=2: ok=3000 silent_loss=0 dup_deliv=0 order_viol=0 retx=4355
B11 jitter 10..40, seq_bits=3: ok=3000 silent_loss=0 dup_deliv=0 order_viol=0 retx=4355
B12 stress 0: ok=2998 fail=2 (fail_but_delivered=1) retx=1070 rx_dup=649 corrupt rx/tx=208/171 stale_acks=275 ticks=92121
B12 stress 1: ok=2992 fail=8 (fail_but_delivered=8) retx=3438 rx_dup=1515 corrupt rx/tx=92/67 stale_acks=147 ticks=497677
B12 stress 2: ok=3000 fail=0 (fail_but_delivered=0) retx=482 rx_dup=306 corrupt rx/tx=45/28 stale_acks=162 ticks=125494
B12 stress 3: ok=2039 fail=961 (fail_but_delivered=763) retx=5287 rx_dup=1327 corrupt rx/tx=0/0 stale_acks=0 ticks=228221
C  jitter RTT 40..160: fixed RTO 60 -> spurious(rx dup)=2306 ticks=199808 | adaptive -> spurious=8 ticks=200424 final rto=222 srtt=98
54/54 passed
```

## 7. 자주 하는 실수

**1. 중복 DATA 에 ACK 를 안 보낸다.** "이미 받은 건데 왜 또 ACK?" — 중복이 왔다는 것 자체가 앞의 ACK 가 사라졌다는 뜻이다. 안 보내면 B2 에서 송신이 5번 보내고 FAIL. 고친 코드: `if (new) {...} else { dup++; }` 다음에 **분기 밖에서** `chan_send(ACK(seq))`.

**2. seq 검사 없이 전달한다.** ACK 손실마다 같은 명령이 두 번 실행된다 — "밸브 열기" 는 괜찮아도 "카운터 +1", "모터 10° 더" 는 사고다. 고친 코드: `seq_is_new()` 로 거르고 `last` 갱신.

**3. ACK 의 seq 를 안 본다 / ACK 에 seq 가 없다.** 그림 1(b)의 늦은 ACK0 이 다음 메시지를 "확인" 해 버린다 = B10 과 같은 조용한 손실. 고친 코드: `if (!t->busy || v.seq != t->seq) { stale++; return; }`.

**4. FAIL 한 seq 를 다음 메시지에 재사용한다.** "실패했으니 안 간 것" 이라 생각하고 seq 를 그대로 두면, ACK 만 죽었던 경우(B7, B9 b) 수신은 이미 그 seq 를 전달했으므로 다음 메시지를 중복으로 버린다 → 조용한 손실. 고친 코드: `tx_finish()` 가 OK·FAIL 모두 `seq + 1`.

**5. 재전송한 메시지에서 RTT 를 잰다.** ACK 가 첫 전송에 대한 것인지 재전송에 대한 것인지 모른다 — 첫 전송 기준이면 RTT 를 과대(RTO 가 계속 커짐), 재전송 기준이면 과소(쓸데없는 재전송 → 또 과소) 추정. 고친 코드: `if (t->rep.attempts == 1 && t->rep.refused == 0) rtt_sample(...)` (Karn).

**6. 재시도 횟수 off-by-one.** "재시도 4번" 이 전송 4번인지 5번인지 문서와 코드가 다르다. 테스트로 못 박는다: 전부 손실 → `fwd.sent == 5`, 완료 시각 = 타임아웃 합 (150 / 690).

**7. 백오프·타이머 산술.** `base << (attempt-1)` (attempt ≥ 32 에서 UB), `rto * 2` 오버플로, `now >= deadline` (틱 랩어라운드). 고친 코드: cap 에서 멈추는 곱하기 루프, `rto > cap/2 ? cap : rto*2`, `(int32_t)(now - deadline) >= 0`.

## 8. 면접 Q&A

<details><summary>Q1. 재전송 타임아웃은 몇으로 잡나요? 근거는요?</summary>

RTT 분포를 재서 정한다. 고정값이면 "RTT 의 높은 분위수 + 수신 쪽 처리 시간 + 여유" (이 레슨의 지터 링크 RTT 40..160 에서 RTO 60 은 2306번 쓸데없는 재전송, 200 은 0번). 적응형이면 RFC 6298: SRTT + 4·RTTVAR, 정수 스케일(8·SRTT, 4·RTTVAR)로 시프트만 써서 계산, 하한·상한 클램프, 첫 값은 보수적으로 크게. 꼬리질문 "재전송하면 RTT 는?" → Karn: 그 메시지는 샘플에서 제외, RTO 두 배로 유지. 더 나아가 ACK 가 시도 번호/타임스탬프를 echo 하면 재전송분도 잴 수 있다(RFC 7323). 마지막으로 "랜덤 손실 30% 에서는 Karn+두 배 유지가 붕괴했다(goodput 0.57)" 같은 실측을 말하면 강하다.

</details>

<details><summary>Q2. ACK 가 사라지면 수신 쪽에는 무슨 일이 생기고, 어떻게 막나요?</summary>

송신은 타임아웃 후 같은 DATA 를 다시 보낸다 → 수신은 같은 메시지를 두 번 받는다. 막는 것은 seq: 마지막으로 전달한 seq 를 기억하고, 같은(또는 뒤쪽) seq 면 전달하지 않고 **ACK 만 다시** 보낸다. 상태는 수신 쪽 1바이트(`last`). 이것이 "최소 한 번(at-least-once) 전송 + 중복 제거 = 정확히 한 번 전달" 의 링크 계층 버전이다. 단, 수신기가 재부팅하면 `last` 를 잃는다 — 연습 4 에서 재부팅 한 번에 56개가 조용히 사라지는 것과, 그걸 고쳐도 중복 1개가 남는 것을 봤다.

</details>

<details><summary>Q3. 1비트 시퀀스(교대 비트)로 충분한 경우와 아닌 경우를 구분해 보세요.</summary>

충분: 링크가 순서를 바꾸지 않고 한 번에 한 프레임만 "살아 있는" 경우 — UART 직결, BLE 링크 계층의 SN/NESN 1비트(연결 이벤트마다 주고받는 구조, Core Spec 확인 권장). 내 하한 공식으로도 고정 지연이면 Kr = 0, Ks = 1 → 1비트. 불충분: 지연 편차가 RTT 보다 큰 링크(재전송·큐잉하는 무선 모뎀, USB-시리얼 + OS 버퍼, IP 네트워크). 그때는 옛 ACK 가 새 DATA 를 확인해 조용한 손실이 난다 (B10: 2개, 지터 링크 3000 메시지에서 1비트 위반 382+458+541). 필요한 폭은 ⌈(dmax_f + dmax_r)/RTTmin⌉ < 2^b 와 ⌈(dmax_f − dmin_f)/RTTmin⌉ < 2^(b−1). 실무에서는 계산값보다 넉넉히 8비트(또는 16비트)를 쓰고, 수명 상한을 시스템 요구로 문서화한다 (TCP 의 MSL 과 같은 역할).

</details>

<details><summary>Q4. 지수 백오프는 왜 쓰고, 언제 오히려 나쁜가요? 지터는요?</summary>

목적은 혼잡/충돌 제어: 여러 노드가 공유 매체를 쓰고 재전송 자체가 손실을 키울 때 물러서서 매체를 비워 준다 (이더넷 CSMA/CD 의 binary exponential backoff, 802.11 contention window, TCP). 점대점 링크의 랜덤 비트 오류에서는 손해: p = 0.3 에서 goodput 19.5 → 10.0, p99 지연 2.2배 (실측). 단절에서는 이득: 3000 틱 단절 동안 FIXED 는 14개 FAIL·100번 헛송신, EXP 는 2개·19번 (배터리·airtime 절약), 대신 복구 후 깨어나는 데 +40 틱. 지터는 같은 사건을 겪은 노드들이 같은 시각에 재시도하는 동기화(thundering herd)를 깨려고 넣는다 — 단일 링크에서는 지연만 늘린다(+229). 정리: "무엇 때문에 잃는가(노이즈 vs 혼잡 vs 단절)" 를 먼저 묻고 정책을 고른다.

</details>

<details><summary>Q5. 송신 쪽이 FAILED 를 보고했습니다. 그 메시지는 안 간 건가요?</summary>

모른다. ACK 경로만 죽었으면 이미 전달됐다 (B7; 손실 50% 스트레스에서 FAIL 961 중 763 개가 실제로 전달됨). 확실히 안 간 경우는 모든 시도가 드라이버에서 거부된 LINK_DOWN 뿐이다. 그래서 (1) 상위 계층은 FAIL 을 "결과 불명" 으로 다루고, (2) 재시도할 명령은 멱등하게 설계하거나(절대값 설정, 상태 질의) op_id 로 중복 제거(연습 2: 같은 op 가 ARQ 로 3번 전달돼도 적용은 2번), (3) 그것도 안 되면 "읽고 확인" 단계(상태 조회 후 필요할 때만 재실행). 분산 시스템의 "정확히 한 번은 전송이 아니라 처리의 성질" 이라는 말과 같은 이야기다 (DIST-B05).

</details>

<details><summary>Q6. stop-and-wait 의 처리량 한계는? 언제 sliding window 로 가나요?</summary>

메시지 하나에 최소 RTT (+ 프레임 송신 시간 T_f). 이용률 U = T_f / (T_f + RTT) — 손실 p 가 있으면 대략 q 배와 재전송 대기만큼 더 줄어든다(이론식, 표). 예: 115200 baud 8N1 에서 21바이트 프레임 T_f ≈ 1.8 ms, 무선 모뎀 RTT 20 ms 면 U ≈ 8%. 대역폭 × RTT (BDP) 가 프레임 하나보다 훨씬 크면 창(window) 안에 여러 프레임을 띄워야 한다 → Go-Back-N / Selective Repeat (COMM-A02). 그때 seq 공간 조건도 바뀐다: GBN 은 창 < 2^b, SR 은 창 ≤ 2^(b−1) — 이 레슨의 반-공간 규칙이 SR 의 조건과 같은 모양인 것을 짚으면 좋다.

</details>

<details><summary>Q7. 이 ARQ 를 STM32 펌웨어에 넣는다면 어디서 무엇을 돌리나요?</summary>

UART RX 는 DMA circular + IDLE(COMM-M11) → 프레임 파서(COMM-M10) 가 CRC 통과한 프레임을 태스크 큐로. ARQ 태스크 하나가 "프레임 도착" 과 "1 ms 틱(또는 xQueueReceive 타임아웃)" 을 둘 다 처리 → `arq_*_on_frame` 과 `arq_tx_poll` 이 같은 문맥이라 락 불필요. TX 는 DMA 로 프레임을 보내고 완료 전에는 재전송하지 않는다(버퍼 재사용 주의). 타임아웃 값은 틱 단위 정수, 비교는 랩어라운드 안전하게. 앱은 `arq_send()` 로 큐에 넣고 콜백(또는 이벤트 플래그)으로 결과를 받는다. 카운터(retx, stale, corrupt, fail 분류)는 진단 명령으로 꺼내게 해 둔다 — 현장에서 "ACK 는 오는데 stale 만 오른다" 를 보면 바로 지연/재정렬을 의심한다.

</details>

<details><summary>Q8. 손상된 프레임에 NACK 를 보내면 더 빠르지 않나요? 왜 이 레슨은 그냥 버리나요?</summary>

빠르다: 타임아웃(30 틱)을 기다리지 않고 RTT(20 틱)만에 재전송. 하지만 (1) 손상된 프레임의 seq·type 은 믿을 수 없어 "무엇을 NACK 하는지" 가 모호하고, (2) NACK 자체도 사라질 수 있어 타이머는 어차피 필요하다. 그래서 NACK 는 **최적화**, 타이머는 **정확성**이다. 헤더에 별도 CRC 를 두어 seq 만이라도 믿을 수 있게 하는 프로토콜도 있다. 고속 링크(PCIe 데이터 링크 계층의 Ack/Nak DLLP + replay 버퍼, D2D 링크 — SOCX-A02)는 NAK 로 지연을 줄이고, 그래도 replay timer 를 둔다 (세부는 각 규격 확인 권장).

</details>

## 9. 추가 연습

### 연습 1 — 이론식 계산기 vs 시뮬레이션

5. 풀이의 식으로 `theory(p, d, T[], M)` (goodput, 메시지당 전송 수, 평균 전달 지연)를 짜고, p = 0 에서 정확히 (50, 1, 10) 이 나오는지, p = 0.2 / 0.4 × FIXED / EXP 에서 5000 메시지 시뮬레이션과 goodput·전송 수 3% 이내, 지연 5% 이내로 맞는지 확인하라.

<details><summary>답</summary>

`pow` 대신 정수 거듭제곱 루프(`powi`)로 libm 없이 계산한다. 실측 (practice.c 출력):

```text
ex1 p=0.2 FIXED goodput 27.46 (theory 27.12)  frames/msg 1.547 (1.562)  mean latency 17.34 (17.50)
ex1 p=0.2 EXP   goodput 20.14 (theory 19.81)  frames/msg 1.553 (1.562)  mean latency 19.55 (19.84)
ex1 p=0.4 FIXED goodput 13.70 (theory 13.64)  frames/msg 2.765 (2.776)  mean latency 30.12 (30.00)
ex1 p=0.4 EXP   goodput 4.89 (theory 4.83)  frames/msg 2.768 (2.776)  mean latency 47.98 (49.52)
```

5000 개라 demo(20000)보다 표본 오차가 크다 (최대 goodput 1.7%, 지연 3.1%). 허용 오차는 "표본 크기에서 나올 만한 흔들림" 보다 넉넉하게, 그러나 식이 틀리면 확실히 걸리게 잡는다 (예: q = (1−p)² 대신 1−p 를 쓰면 p = 0.4 FIXED 이론값이 13.64 → 25.0 으로 83% 틀린다 — 따로 계산해 확인).

</details>

### 연습 2 — 앱 계층 멱등성: FAIL 뒤 같은 op_id 로 재전송

명령 payload = `[op_id LE32][value]`. 수신 앱은 최근 op_id 16개를 기억해 이미 적용한 op 는 무시한다. ACK 경로가 op0 의 5번 동안 죽어 op0 이 FAIL → 완료 콜백 안에서 같은 op_id 로 다시 `arq_send()`. ARQ 는 op0 을 두 번 전달하지만 효과(value 합)는 한 번이어야 한다.

<details><summary>답</summary>

실측: `resends=1, ARQ deliveries=3, applied=2, ignored(dup op)=1, value=21` (= 10 + 11). op0 의 두 번째 전달은 **새 ARQ seq** 로 왔으므로 ARQ 계층은 중복인 줄 모른다 — 중복 제거는 의미를 아는 계층(앱)에서 해야 한다는 증거. 또 재전송된 op0 은 이미 큐에 있던 op1 **뒤에** 도착한다: 앱 계층 재시도는 순서를 바꿀 수 있다. 최근 16개 창은 "재시도가 그 안에 끝난다" 는 가정이며, 크기는 (최대 재시도 시간 × 명령률)로 정한다.

</details>

### 연습 3 — 적응형 RTO: 성공하면 백오프 리셋

`world.on_done` 훅에서 OK 로 끝날 때마다 `rto = SRTT + max(1, 4·RTTVAR)` 로 되돌리는 정책을 넣고, 지터 링크(20..80)에서 손실 0/10/30% 를 Karn 유지 정책과 비교하라.

<details><summary>답</summary>

```text
ex3 loss  0% Karn-retain    goodput 9.90  p99 latency 80  frames/msg 1.00  clean=1
ex3 loss  0% reset-on-OK    goodput 9.90  p99 latency 80  frames/msg 1.00  clean=1
ex3 loss 10% Karn-retain    goodput 5.56  p99 latency 641  frames/msg 1.24  clean=1
ex3 loss 10% reset-on-OK    goodput 6.48  p99 latency 333  frames/msg 1.24  clean=1
ex3 loss 30% Karn-retain    goodput 0.62  p99 latency 8061  frames/msg 2.02  clean=1
ex3 loss 30% reset-on-OK    goodput 1.38  p99 latency 1501  frames/msg 2.02  clean=1
```

손실 0% 에서는 같고, 30% 에서 goodput 2.2배, p99 1/5. 근거: 점대점 링크의 랜덤 손실은 "RTT 가 길어졌다" 는 신호가 아니므로 백오프를 다음 메시지로 끌고 갈 이유가 없다. 다만 메시지 **안**에서의 두 배 증가는 그대로라 여전히 FIXED 60(6.14)보다 나쁘다. 공유 매체·혼잡이 있는 링크에서는 이 리셋이 위험하다(물러서야 할 때 안 물러섬) — 링크 성격을 알 때만 쓰는 정책이다.

</details>

### 연습 4 — 수신기 재부팅과 재동기화

seq 가 200 일 때 수신기가 재부팅(= `last` 가 초기값 255 로)되면 몇 개를 잃는가? "부팅 후 첫 정상 DATA 는 무조건 새 것(`last = seq − 1`)" 으로 고치고, 그래도 남는 위험을 재현하라.

<details><summary>답</summary>

d = (200 − 255) mod 256 = 201 > 128 → 중복 판정 → ACK 만 하고 버림. seq 200..255 의 **56개가 OK 로 보고되면서 조용히 사라지고**, seq 0 (d = 1) 에서야 다시 받는다 (실측 `silent_loss=56`). seq 폭 문제가 아니다 — 이 링크는 `seq_bits_required = 1` 이다. "첫 프레임에 동기" 로 고치면 0. 남는 위험: msg0 을 전달한 직후 ACK 가 사라지고 t=20 에 재부팅하면, t=30 의 재전송이 "부팅 후 첫 프레임" 이라 새 것으로 받아 **두 번 전달**된다 (실측 `ndeliv[0]=2 dup_deliv=1`, 로그에 t=10 과 t=40 의 `RX deliver seq=0`). 재부팅은 중복 제거 상태를 지우므로 정확히 한 번은 불가능 → 앱 멱등성(연습 2)이나, 송신·수신이 epoch/세션 ID 를 교환하는 핸드셰이크(SYNC, COMM-A11)가 필요하다. `last` 를 백업 레지스터·NVM 에 두는 방법도 있지만 쓰기 수명과 "쓰기 직전 리셋" 창이 남는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 19/19 passed (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_asan && ./pr_asan   # 오류 없음
```

```text
ex2 op0 status: first=FAIL, resends=1, ARQ deliveries=3, applied=2, ignored(dup op)=1, value=21
ex4 reboot at seq 200, no fix       : ok=300 silent_loss=56 dup_deliv=0
ex4 reboot at seq 200, sync-on-first: ok=300 silent_loss=0 dup_deliv=0
ex4 reboot between delivery and lost ACK: ndeliv[0]=2 dup_deliv=1 silent_loss=0
19/19 passed
```

## 10. 핵심 정리

- 신뢰성 = CRC(검출) + ACK(확인, **확인하는 seq 를 싣는다**) + 타이머(재전송) + seq(중복 제거). 수신은 새 것만 전달하고 **중복에도 ACK**, 손상은 ACK 없이 버린다.
- 재시도는 "전송 횟수" 로 센다: max_retries = 4 → 정확히 5번, FIXED 30 이면 t = 150 에 FAIL. 분류: 전부 거부 → LINK_DOWN(확실히 안 감), 아니면 TIMEOUT_EXHAUSTED(**모름** — 손실 50% 에서 FAIL 의 79% 가 실제로 전달됨).
- FAIL 뒤에도 seq 를 넘긴다. FAIL 을 재시도하는 상위 계층은 멱등 명령 또는 op_id 중복 제거가 필요하다.
- 1비트 seq 는 FIFO 링크에서만 맞다. 필요한 폭: ⌈(dmax_f + dmax_r)/RTTmin⌉ < 2^b, ⌈(dmax_f − dmin_f)/RTTmin⌉ < 2^(b−1). 깨지면 "가끔 하나" 가 아니라 연쇄로 수천 개.
- goodput = P(성공)/E[주기], q = (1−p)², 주기 = S_{n−1} + RTT 또는 S_M — 실측과 1% 이내. FIXED 30 은 p = 0.3 에서 19.5, EXP 는 10.0.
- 지수 백오프는 혼잡·단절용(단절 3000 틱: FAIL 14 → 2), 랜덤 손실에는 손해. 지터는 여러 노드의 동기화를 깨려고.
- RTO = SRTT + 4·RTTVAR (8·SRTT, 4·RTTVAR 정수 스케일), Karn: 재전송분은 샘플 제외 + 백오프 유지. 랜덤 손실 30% 에서는 그 유지가 붕괴를 부른다(goodput 0.57) — 링크 성격을 보고 정책을 고른다.

## 참고자료

- RFC 6298, *Computing TCP's Retransmission Timer* (2011) — SRTT/RTTVAR, α = 1/8, β = 1/4, K = 4, 초기 1 s, 백오프, Karn 규칙 적용: <https://www.rfc-editor.org/rfc/rfc6298> (조항 번호·하한 문구는 원문 확인 권장)
- V. Jacobson, "Congestion Avoidance and Control", SIGCOMM 1988 — 평균·편차 RTT 추정과 정수 스케일 구현.
- P. Karn, C. Partridge, "Improving Round-Trip Time Estimates in Reliable Transport Protocols", SIGCOMM 1987 — 재전송 모호성과 백오프 유지.
- RFC 7323, *TCP Extensions for High Performance* — 타임스탬프로 재전송분 RTT 측정(RTTM): <https://www.rfc-editor.org/rfc/rfc7323>
- K. Bartlett, R. Scantlebury, P. Wilkinson, "A Note on Reliable Full-Duplex Transmission over Half-Duplex Links", CACM 1969 — 교대 비트 프로토콜.
- D. Bertsekas, R. Gallager, *Data Networks*, 2nd ed., 2장 (ARQ: stop-and-wait, Go-Back-N, Selective Repeat, 정확성 증명과 seq 모듈러 조건).
- RFC 1982, *Serial Number Arithmetic* — 모듈러 seq 의 "앞/뒤" 비교 (이 레슨의 반-공간 규칙과 같은 생각).
- Marc Brooker, "Exponential Backoff And Jitter", AWS Architecture Blog (2015) — full/equal/decorrelated jitter 비교.
- Bluetooth Core Specification, Link Layer 의 SN/NESN 확인 방식; IEEE 802.15.4 MAC 의 DSN + ACK + macMaxFrameRetries — 세부 값은 각 규격 확인 권장.
- 관련 레슨: COMM-B08 (CRC 검출력), COMM-B10 (프레임 + CRC-16/CCITT-FALSE), COMM-B13 (Modbus RTU — 마스터 타임아웃·재시도), COMM-M07 (CAN bus-off = 링크 down), COMM-M10 (견고한 파서), COMM-M11 (DMA + IDLE 수신), COMM-M13 (비동기 큐·완료 콜백), COMM-A02 (Go-Back-N vs Selective Repeat), COMM-A11 (통신 스택 종합·세션 재동기화), DIST-B05 (멱등성·재시도·중복 제거), NET-M03 (RTO 추정 + fast retransmit), TEST-A05 (결정적 시뮬레이션·가상 시간), RTOS-B 큐·뮤텍스 레슨, BM-A05 (YMODEM 부트로더), SOCX-A02 (D2D 링크 replay)
