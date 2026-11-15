---
id: COMM-B02
time: 60분
updated: 2026-11-11
---
# COMM-B02 UART 수신기 모델: 16x 오버샘플링과 에러 검출

## 학습 목표

- UART 수신기가 클럭 없이 바이트를 건지는 방법 — **falling edge 로 start 를 잡고, 각 비트의 중앙 샘플 3개(8/9/10번)를 다수결(majority vote)** — 을 틱 단위 상태 머신으로 구현하고, 'A'(0x41) 한 문자의 판정 시각을 틱 단위로 설명한다.
- 글리치(glitch)·stop 비트 0·break·읽기 지연을 직접 주입해 **NF(noise) / PE(parity) / FE(framing) / BRK(break) / ORE(overrun)** 가 각각 언제, 어떤 데이터와 함께 올라오는지 테스트로 확인한다.
- 다수결이 막는 것(1샘플 스파이크)과 못 막는 것(샘플 창 전체를 덮는 펄스 → 조용한 오류, parity 가 필요한 이유)을 경계값 표로 말한다.
- 1단 RDR 의 ISR 예산이 **정확히 1문자 시간**(8N1 = 10비트)이라는 것을 경계 테스트(159 vs 160 틱)로 보이고, HW overrun 과 SW 링 버퍼 오버플로를 구분한다.

## 1. 문제

브링업 첫날 UART 콘솔에 깨진 문자가 섞이거나 `ORE` 가 뜨면 "왜?"를 설명할 수 있어야 한다 — 펌웨어 면접의 "UART 수신기를 설계해 보세요 / 에러 플래그는 각각 언제 서나요?" 가 바로 이 모델이다.

UART 는 클럭 선이 없다. 송신기와 수신기는 **같은 baud 를 약속**만 하고 각자 자기 클럭으로 돈다. 그래서 수신기는 라인을 baud 의 16배(16x oversampling)로 계속 샘플링하다가, idle(1) 에서 0 으로 떨어지는 순간(falling edge)을 start 비트의 시작으로 보고, 거기서부터 16샘플마다 한 비트씩 **비트 중앙 근처**를 읽는다. 비트 경계 근처는 상승·하강 시간과 클럭 오차 때문에 불확실하므로 중앙을 읽는 것이 핵심이고, 중앙 샘플을 하나가 아니라 셋(8·9·10번) 읽어 다수결을 하면 짧은 스파이크 하나는 걸러진다. 셋이 서로 다르면 "값은 골랐지만 잡음이 있었다"는 표시로 **NF** 를 세운다. STM32 USART 가 16x 모드에서 샘플 8/9/10 을 쓰는 것이 대표적인 예다 (대표값 — RM 의 "receiver: noise error" 절 확인 권장).

프레임 형식(start 0, data LSB first, parity, stop 1)과 송신 쪽 비트열은 COMM-B01 에서 만들었다. 이 레슨은 그 비트열을 **16배 샘플 스트림**으로 펼쳐 수신기에 한 틱씩 먹인다. 아래 그림은 테스트가 실제로 쓰는 'A' 프레임이다.

```svg
<svg viewBox="0 0 740 266" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">'A' = 0x41 = 0100 0001, 8N1 → 라인: idle 1 | start 0 | 1 0 0 0 0 0 1 0 (LSB first) | stop 1 | idle 1</text>
  <g fill="currentColor" font-size="12">
    <text x="56" y="44">idle</text><text x="118" y="44">S</text><text x="172" y="44">d0</text><text x="228" y="44">d1</text>
    <text x="284" y="44">d2</text><text x="340" y="44">d3</text><text x="396" y="44">d4</text><text x="452" y="44">d5</text>
    <text x="508" y="44">d6</text><text x="564" y="44">d7</text><text x="612" y="44">stop</text><text x="670" y="44">idle</text>
  </g>
  <g stroke="currentColor" stroke-dasharray="2 3" opacity="0.5">
    <line x1="96" y1="50" x2="96" y2="112"/><line x1="152" y1="50" x2="152" y2="112"/><line x1="208" y1="50" x2="208" y2="112"/>
    <line x1="264" y1="50" x2="264" y2="112"/><line x1="320" y1="50" x2="320" y2="112"/><line x1="376" y1="50" x2="376" y2="112"/>
    <line x1="432" y1="50" x2="432" y2="112"/><line x1="488" y1="50" x2="488" y2="112"/><line x1="544" y1="50" x2="544" y2="112"/>
    <line x1="600" y1="50" x2="600" y2="112"/><line x1="656" y1="50" x2="656" y2="112"/>
  </g>
  <path d="M40 62 H96 V100 H152 V62 H208 V100 H488 V62 H544 V100 H600 V62 H712" fill="none" stroke="currentColor" stroke-width="2"/>
  <g class="f-acc" stroke="currentColor">
    <circle cx="125.75" cy="100" r="2.5"/><circle cx="181.75" cy="62" r="2.5"/><circle cx="237.75" cy="100" r="2.5"/>
    <circle cx="293.75" cy="100" r="2.5"/><circle cx="349.75" cy="100" r="2.5"/><circle cx="405.75" cy="100" r="2.5"/>
    <circle cx="461.75" cy="100" r="2.5"/><circle cx="517.75" cy="62" r="2.5"/><circle cx="573.75" cy="100" r="2.5"/>
    <circle cx="629.75" cy="62" r="2.5"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="120" y="126">0</text><text x="176" y="126">1</text><text x="232" y="126">0</text><text x="288" y="126">0</text>
    <text x="344" y="126">0</text><text x="400" y="126">0</text><text x="456" y="126">0</text><text x="512" y="126">1</text>
    <text x="568" y="126">0</text><text x="624" y="126">1</text>
  </g>
  <text x="10" y="126" fill="currentColor" font-size="12">판정</text>
  <line x1="96" y1="150" x2="152" y2="150" stroke="currentColor"/>
  <g stroke="currentColor">
    <line x1="96" y1="146" x2="96" y2="154"/><line x1="99.5" y1="147" x2="99.5" y2="153"/><line x1="103" y1="147" x2="103" y2="153"/>
    <line x1="106.5" y1="147" x2="106.5" y2="153"/><line x1="110" y1="147" x2="110" y2="153"/><line x1="113.5" y1="147" x2="113.5" y2="153"/>
    <line x1="117" y1="147" x2="117" y2="153"/><line x1="120.5" y1="147" x2="120.5" y2="153"/><line x1="124" y1="147" x2="124" y2="153"/>
    <line x1="127.5" y1="147" x2="127.5" y2="153"/><line x1="131" y1="147" x2="131" y2="153"/><line x1="134.5" y1="147" x2="134.5" y2="153"/>
    <line x1="138" y1="147" x2="138" y2="153"/><line x1="141.5" y1="147" x2="141.5" y2="153"/><line x1="145" y1="147" x2="145" y2="153"/>
    <line x1="148.5" y1="147" x2="148.5" y2="153"/><line x1="152" y1="146" x2="152" y2="154"/>
  </g>
  <rect x="120.5" y="142" width="10.5" height="16" class="f-warn" stroke="currentColor" opacity="0.7"/>
  <text x="160" y="155" fill="currentColor" font-size="12">← start 비트 한 칸 = 샘플 16개, 판정은 8·9·10번 (색칠) 의 다수결</text>
  <path d="M96 100 L96 176" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="40" y="192" fill="currentColor" font-size="12">falling edge: 이 샘플이 start 의 1번 (t=21)</text>
  <path d="M629.75 62 L629.75 206" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="380" y="222" fill="currentColor" font-size="12">stop 의 10번 샘플 (t=174): 문자 완료 → RDR, RXNE</text>
  <text x="10" y="244" fill="currentColor" font-size="12">점 = 각 비트에서 실제로 읽는 위치(샘플 8~10). 비트 경계 근처는 한 번도 읽지 않는다.</text>
  <text x="10" y="260" fill="currentColor" font-size="12">문자 하나 = 10비트 = 160 틱 (샘플 i 는 틱 t = i + 1 에 처리).</text>
</svg>
```

### Part A — 다수결과 수신 상태 머신

- `vote3(smp, &noisy)` — `smp` 의 bit0/1/2 = 샘플 8/9/10. 다수결 값을 돌려주고, 셋이 모두 같지 않으면 `noisy = 1`.
- `rx_tick(r, sample)` — 샘플 하나 = 한 틱. 상태 `IDLE → START → DATA ×n → [PARITY] → STOP → IDLE`.
  - IDLE: 직전 샘플 1, 지금 0 이면 그 샘플을 start 의 **1번**으로 삼는다 (`ph = 1`).
  - 매 틱 `ph++`, 샘플 8~10 을 모으고 10번에서 판정. `ph == 16` 이면 다음 비트로.
  - START 판정이 1 이면 **false start**(글리치) → IDLE, 그리고 `prev = 1` 로 **재무장** (5. 풀이 "삼켜진 edge").
  - STOP 판정(10번)에서 프레임 완료: 이벤트 로그에 남기고 `rx_deliver` 로 RDR 에 넘긴 뒤 즉시 IDLE — stop 의 나머지 6샘플 동안 이미 다음 start 를 기다린다.
- 테스트: vote3 진리표, 'A' 의 start 틱 21 / 완료 틱 174, **설정 6개(8N1·8E1·8O1·7E1·7O2·8N2) × 256바이트 전수**(문자 사이 gap 0~3비트), xorshift32 고정 시드 랜덤 4000문자(앞 idle 1~50샘플, gap 0~5비트).

### Part B — 에러 플래그: 글리치, framing error, break, overrun

- `frame_flags(cfg, data, par, stop, noisy)` — NF: 이 프레임 어느 비트에서든 3샘플 불일치 / PE: parity 불일치 / FE: stop 판정 0 / BRK: FE 이면서 data = 0 이고 parity 비트도 0.
- `rx_deliver(r, data, flags)` — RXNE 가 이미 1 이면 **overrun**: ORE 를 세우고 **옛 바이트를 유지, 새 바이트(와 그 플래그)는 버린다**. 아니면 RDR ← data, SR |= RXNE | flags.
- `uart_read(r, &data)` — SR 스냅샷을 돌려주고, RXNE 면 data = RDR. 읽으면 모든 플래그가 지워진다 (우리 모델의 정의. 실제 칩의 클리어 순서는 5. 풀이).
- 주입 테스트: idle 중 low 글리치 L = 1~16 샘플(8N1/8E1), 'A' 의 data 비트 안 스파이크 4종 + start 비트 안 스파이크, start edge 직전 1샘플 글리치 k = 1~18, stop 비트 0, 30비트 길이 break(8N1/8O1)와 0x00 바이트 구분, 3바이트 연속 무읽기 overrun, 읽으면 플래그가 지워지는지.

### Part C — ISR + 링 버퍼: 인터럽트 지연이 얼마까지 괜찮나

- `ring_push / ring_pop` — 16칸, 2의 거듭제곱 마스크 + free-running `uint32_t` 카운터 (DS-B04 방식, 카운터가 2^32 를 넘어도 동작).
- `uart_isr(r, q, st)` — RXNE 인터럽트: `uart_read` → ORE/NF 는 세고, BRK/FE/PE 문자는 버리고 세고, 정상 바이트만 링에. 링이 차면 `ring_drop++`.
- 하네스 `run_link(latency, drain)` 가 주어진다: 8N1 연속 송신, RXNE 가 선 뒤 `latency` 틱 후에 ISR 실행(더 높은 우선순위 ISR·임계 구역을 흉내), `drain` 틱마다 main 루프가 링을 비움.
- 테스트: latency 159 → 무손실, 160 → overrun, 200 → **짝수 번째 바이트만** 남음(옛 것 유지 정책의 지문), drain 2400 → 무손실, 4000 → ORE 0 인데 링 드롭 발생, 에러 섞인 스트림 → 링엔 "Hi!" 만.

## 2. Problem (English)

You are writing the receive path of a UART on a part whose peripheral you must model in software first.
The RX line is sampled at 16× the baud rate; `rx_tick()` receives one sample (0/1) per call.

```c
typedef enum { PAR_NONE, PAR_EVEN, PAR_ODD } parity_t;
typedef struct { uint8_t data_bits; parity_t parity; uint8_t stop_bits; } uart_cfg_t; /* 7/8 data bits */
enum { SR_RXNE = 1, SR_ORE = 2, SR_NF = 4, SR_FE = 8, SR_PE = 16, SR_BRK = 32 };

int     vote3(uint8_t smp, uint8_t *noisy);        /* majority of samples 8,9,10 */
void    rx_tick(uart_rx_t *r, int sample);         /* one 16x sample per call */
uint8_t frame_flags(const uart_cfg_t *c, uint16_t data, uint8_t par, uint8_t stop, uint8_t noisy);
void    rx_deliver(uart_rx_t *r, uint8_t data, uint8_t flags);  /* single-entry RDR, overrun policy */
uint8_t uart_read(uart_rx_t *r, uint8_t *data);    /* returns SR snapshot, clears flags */
void    uart_isr(uart_rx_t *r, ring_t *q, link_stats_t *st);    /* RXNE ISR -> 16-entry ring */
```

Requirements:

- Start = first 0 sample after a 1 (falling edge). Validate it with the majority of samples 8/9/10 of the start bit;
  if the majority is 1, it was a glitch: return to idle without producing a character.
- Each subsequent bit is decided at its sample 10 from samples 8/9/10. Set NF if the three samples of any bit
  in the frame disagree. The character is complete at sample 10 of the (first) stop bit.
- FE if stop = 0. Break = FE with all data bits 0 (and parity bit 0). PE on parity mismatch.
- Overrun: if a character completes while RXNE is still set, set ORE, **keep the old byte**, drop the new one.
- No dynamic allocation; deterministic; the model must terminate on any input.

Examples (8N1, 16 samples/bit):

```text
idle x20, then 'A' 0x41 -> start edge at tick 21, char complete at tick 21 + 9*16 + 9 = 174, data 0x41, flags 0
low glitch of 8 samples in idle  -> rejected (false start), no character
low glitch of 9 samples in idle  -> phantom 0xFF with NF  (8E1: 0xFF with NF|PE)
'A' with samples 8..10 of d1 forced high -> 0x43, no flags in 8N1; PE in 8E1
line low for 30 bit times -> exactly one character: 0x00 with FE|BRK
three back-to-back chars, never read -> SR = RXNE|ORE, RDR = first char
```

Follow-ups: Why sample in the middle? Why majority of three? What does NF tell firmware? How long may the RX ISR be delayed at 115200 8N1? What changes with a hardware FIFO or DMA?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여덟 군데 (Part A 2개, Part B 3개, Part C 3개). 송신 파형 생성기 `put_char`(COMM-B01 모델 축약판), 이벤트 로그, 하네스 `run_link` 는 주어진다. 스텁은 "아무 일도 안 함"이라 시뮬레이터 루프는 항상 끝난다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:215  ok
FAIL starter.c:220  g_rx.nlog == 1 && g_rx.log[0].kind == EV_FRAME
…
exhaustive 6 cfgs x 256 bytes: bad=1536
FAIL starter.c:238  bad == 0
random 4000 chars: bad=4000
…
FAIL starter.c:358  ok && !ring_push(&q, 99)
…
2/35 passed
```

(exit 1. 통과한 2개는 "읽은 뒤 SR == 0", "빈 RDR 읽기 == 0" — 스텁의 0 이 우연히 맞는 경우.)

## 4. 힌트

<details><summary>힌트 1 — 틱마다 하는 일은 세 가지뿐</summary>

`ph++` → (ph 가 8~10 이면) 샘플 모으기 → (ph == 10 이면) 판정 → (ph == 16 이면) 다음 비트. 상태는 "지금 어느 비트를 받는 중인가"만 기억한다. 샘플 3개는 `smp |= s << (ph - 8)` 로 3비트에 모으면 `vote3` 가 진리표 하나로 끝난다.

</details>

<details><summary>힌트 2 — 문자 완료는 stop 의 끝이 아니라 중앙</summary>

stop 을 10번에서 판정했으면 거기서 바로 IDLE 로 간다. stop 의 나머지 6샘플(11~16)은 1 이므로 다음 start 의 falling edge 를 정확히 잡는다. stop 끝(16번)까지 기다리면, 송신 클럭이 조금 빠를 때 다음 start edge 를 놓친다 (COMM-B03 의 허용 오차).

</details>

<details><summary>힌트 3 — false start 뒤 prev 를 무엇으로 둘까</summary>

글리치를 거부하는 순간(start 의 10번 샘플)에 진짜 start 가 이미 시작돼 라인이 0 이라면? `prev = s = 0` 으로 두면 다음 0 은 edge 가 아니다. 거부할 때는 `prev = 1` 로 두고 return 하라. 반대로 그 순간 라인이 1 이면 어차피 prev = 1 이라 달라지는 것이 없다.

</details>

<details><summary>힌트 4 — break 는 "0x00 + FE" 의 특별한 경우</summary>

break 동안 라인은 계속 0 이다. 수신기는 start(0)·data(전부 0)·stop(0!) 을 읽고 FE 를 낸 뒤 IDLE 로 간다. 이때 `prev = 0` 이라 라인이 1 로 돌아올 때까지 새 start 가 없다 — 그래서 아무리 긴 break 도 문자 하나다. 0x00 바이트(stop = 1)는 break 가 아니다.

</details>

<details><summary>힌트 5 — overrun 경계는 "다음 문자의 stop 10번 샘플"</summary>

8N1 연속 수신이면 RXNE 는 160 틱마다 선다. ISR 이 RXNE 후 L 틱에 돈다면, 같은 틱 안에서는 하드웨어(rx_tick)가 먼저 다음 문자를 넘기려 한다. L = 159 와 L = 160 중 어디서 깨지는지 손으로 먼저 예측해 보라.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 범위 확인 | 오버샘플링 배수(16x? 8x?), 판정 샘플(8/9/10), data 7/8, parity, stop 몇 개 검사, RDR 깊이(1단? FIFO?), overrun 정책(옛 것 유지? 덮어쓰기?), 플래그 클리어 방법 |
| 2. 단순 해법 | edge 에서 1.5비트 뒤, 그다음 1비트마다 샘플 1개 — 맞지만 스파이크 하나에 비트가 뒤집힌다 |
| 3. 개선 + 불변식 | 샘플 3개 다수결 + NF. 불변식: "ph = 현재 비트 안 샘플 번호, 판정은 ph = 10 에서 정확히 한 번" |
| 4. 코드 | 아래 Part A/B/C |
| 5. 소리 내어 말할 테스트 | 'A' 의 판정 틱, 전수 왕복, 글리치 길이 경계(8 vs 9), 샘플 창 안/밖 스파이크, stop = 0, break, 무읽기 overrun, ISR 지연 159/160 |
| 6. 비용 | 틱당 O(1), 상태 ~20바이트 + 1바이트 RDR. 실제로는 하드웨어가 하고, 펌웨어는 RXNE/에러 플래그 처리만 |

### Part A — 규칙과 'A' 트레이스

규칙을 정확히 쓰면:

1. IDLE 에서 `prev == 1 && s == 0` → START, 이 샘플이 start 의 1번 (`ph = 1`, `t_start = t`).
2. 이후 매 틱 `ph++`. `8 ≤ ph ≤ 10` 이면 `smp |= s << (ph − 8)`.
3. `ph == 10` 에서 `v = vote3(smp)`, `noisy |= nz`. START 면 v = 1 일 때 false start. DATA 면 `shift |= v << nbit`(LSB first). PARITY 면 `par = v`. STOP 이면 플래그 계산 → 로그 → RDR → IDLE.
4. `ph == 16` 이면 `ph = 0`, 다음 비트 (START → DATA, data 다 받으면 PARITY 또는 STOP, PARITY → STOP).

'A' 를 idle 20샘플 뒤에 보내면 (테스트 그대로) — 샘플 i(0부터)는 틱 t = i + 1 에 처리된다. 비트 j 의 샘플 ph 는 틱 `21 + 16j + ph − 1`:

| j | 비트 | 라인 | 샘플 8/9/10 의 틱 | 판정 | 그 뒤 상태 | shift |
|---|---|---|---|---|---|---|
| 0 | start | 0 | 28 / 29 / 30 | 0 → 진짜 start | DATA (t=36 에서 전환) | 0x00 |
| 1 | d0 | 1 | 44 / 45 / 46 | 1 | DATA | 0x01 |
| 2 | d1 | 0 | 60 / 61 / 62 | 0 | DATA | 0x01 |
| 3–6 | d2–d5 | 0 | 76 … 126 | 0 | DATA | 0x01 |
| 7 | d6 | 1 | 140 / 141 / 142 | 1 | DATA | 0x41 |
| 8 | d7 | 0 | 156 / 157 / 158 | 0 | STOP (t=164, nbit = 8) | 0x41 |
| 9 | stop | 1 | 172 / 173 / 174 | 1 → 완료 | IDLE, RXNE | 0x41 |

테스트가 `t_start == 21 && t_done == 174` 를 확인한다 (실행 출력 `'A' 8N1: start edge t=21, char done (stop sample 10) t=174`). 일반식: 완료 틱 = t_start + (프레임 비트 수 − 1)·16 + 9.

**왜 샘플 8/9/10 인가.** 송신 edge 는 두 샘플 사이 어딘가에서 일어나고 수신기는 그 뒤 첫 샘플에서야 0 을 본다 — 그래서 우리의 "1번 샘플"은 진짜 edge 보다 0~1 샘플 늦다. 진짜 비트 중앙은 edge 뒤 8샘플이므로 우리 번호로는 7.5~8.5 근처이고, 8/9/10 은 그 중앙과 그 바로 뒤를 덮는다. 우리 모델은 송수신 클럭이 같고 edge 가 샘플에 정렬돼 있어 이 0~1 샘플 불확실성은 없다. 클럭이 어긋나면 샘플 위치가 비트마다 밀리는데, 그 한계가 COMM-B03 의 주제다.

**왜 stop 중앙에서 IDLE 로 가나.** 매 문자의 start edge 에서 위상을 다시 맞추는(resync) 것이 UART 가 클럭 선 없이 동작하는 이유다. 오차는 한 문자(10~11비트) 안에서만 쌓이고, 다음 문자에서 0 으로 리셋된다. stop 10번에서 바로 IDLE 로 가야 송신기가 조금 빨라 다음 start 가 일찍 와도 잡는다.

```svg
<svg viewBox="0 0 740 236" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="c2a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <g stroke="currentColor">
    <rect x="20" y="80" width="90" height="40" rx="8" class="f-mut"/>
    <rect x="170" y="80" width="90" height="40" rx="8" class="f-acc"/>
    <rect x="320" y="80" width="90" height="40" rx="8" class="f-acc"/>
    <rect x="470" y="80" width="90" height="40" rx="8" class="f-acc"/>
    <rect x="620" y="80" width="100" height="40" rx="8" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="45" y="105">IDLE</text><text x="190" y="105">START</text><text x="345" y="105">DATA</text>
    <text x="482" y="105">PARITY</text><text x="650" y="105">STOP</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#c2a)">
    <line x1="110" y1="100" x2="168" y2="100"/>
    <line x1="260" y1="100" x2="318" y2="100"/>
    <line x1="410" y1="100" x2="468" y2="100"/>
    <line x1="560" y1="100" x2="618" y2="100"/>
    <path d="M410 88 C 470 40, 600 40, 650 78"/>
    <path d="M345 80 C 330 50, 395 50, 380 78"/>
    <path d="M215 120 C 190 175, 90 175, 65 122"/>
    <path d="M670 120 C 640 215, 120 215, 50 122"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="100" y="72">1→0 (ph=1)</text>
    <text x="264" y="94">ph=16</text>
    <text x="402" y="140">n 비트 끝,</text><text x="402" y="154">parity 있음</text>
    <text x="564" y="116">ph=16</text>
    <text x="500" y="36">n 비트 끝, parity 없음</text>
    <text x="290" y="44">비트마다 ph=10 판정</text>
    <text x="70" y="172">ph=10 다수결 = 1 → false start,</text>
    <text x="70" y="186">prev = 1 로 재무장</text>
    <text x="250" y="226">ph=10: 플래그 계산 → 로그 → rx_deliver(RDR, RXNE / ORE) → 즉시 IDLE</text>
  </g>
  <text x="20" y="22" fill="currentColor" font-weight="bold">수신 상태 머신 — 모든 판정은 각 비트의 샘플 10번에서 한 번</text>
</svg>
```

### Part A — "삼켜진 edge": false start 뒤 재무장

아래 잡음 실험(`demo_noise.c`)을 처음 돌렸을 때, 다수결 수신기의 오류율이 이론보다 **30배 넘게** 높았다(p = 0.001 에서 0.1% vs 0.003%). 원인을 찾으려고 'A' 의 진짜 start edge 보다 k 샘플 앞에 1샘플짜리 low 글리치를 하나 넣어 봤다 (연습 5, 실제 출력):

| k | 무슨 일이 일어나나 | 결과 (재무장 없음) |
|---|---|---|
| 1–7 | 글리치에서 start 를 일찍 잡음. 샘플 8–10 이 진짜 start 안 → 인정, 이후 샘플 위치가 k 만큼 앞당겨지지만 여전히 비트 안 | 'A' 정상 |
| 8 | 샘플 8 은 idle(1), 9·10 은 start(0) → 인정 + 불일치 | 'A' + NF |
| 9 | 샘플 8·9 는 idle(1), 10 은 진짜 start 의 첫 0 → **거부**. 그런데 이 순간 `prev = s = 0` | edge 를 놓침 → d0→d1 의 1→0 을 start 로 오인 → **0xD0, 플래그 없음** |
| ≥10 | 거부가 진짜 edge 전에 끝남, prev = 1 | 'A' 정상 |

k = 9 는 "거부 판정 샘플 = 진짜 start 의 첫 샘플" 인 딱 한 위치다. 그래서 샘플마다 확률 p 로 뒤집히는 잡음에서는 이 사고가 프레임당 약 p 확률로 생기고, 그것이 실험의 초과분과 맞는다. 고치는 법은 한 줄: false start 를 거부할 때 `prev = 1` 로 두고 return. 지금 샘플이 0 이면 다음 틱의 0 이 edge 가 되어 start 를 1샘플 늦게 잡는다(샘플 8–10 → 9–11, 여전히 비트 안). 지금 샘플이 1 이면 원래 코드와 같다. solution.c 의 테스트가 k = 1~18 전부에서 'A' 'B' 가 맞는지(k = 8 만 NF) 확인한다. 실제 칩이 이 경우를 어떻게 처리하는지는 RM 의 start bit detection 절 확인 권장 — STM32 는 start 판정에 샘플 3/5/7 과 8/9/10 을 함께 본다고 설명한다 (대표값).

### Part B — 플래그 표

| 플래그 | 조건 (우리 모델) | RDR 의 데이터 | 드라이버가 할 일 | STM32 / 16550 / nRF (대표값, 문서 확인 권장) |
|---|---|---|---|---|
| NF | 어느 비트든 샘플 8/9/10 불일치 | 다수결 값 (아마 맞음) | 받되 센다. 많으면 배선·접지·baud 의심 | STM32 NE/NF. ONEBIT 모드에선 샘플 1개, NF 없음 |
| PE | parity 비트 ≠ 기대값 | 그대로 (틀림) | 버리고 센다 | STM32 PE, 16550 LSR PE, nRF ERRORSRC.PARITY |
| FE | stop 판정 = 0 | 그대로 (의심) | 버리고 센다. 연속이면 baud 불일치 | STM32 FE, 16550 LSR FE, nRF FRAMING |
| BRK | FE + data 0 + parity 비트 0 | 0x00 | 데이터 아님 → 리셋·주의 신호로 | 16550 LSR BI, nRF BREAK, STM32 는 일반 모드에서 FE + 0x00 (LIN 모드 LBD 는 별도) |
| ORE | 문자 완료 시 RXNE 가 아직 1 | **옛 바이트 유지** | 바이트 유실. ISR 지연/우선순위 점검 | STM32: RDR 유지·시프트 레지스터 덮어씀, 새 패밀리는 OVRDIS 로 비활성 가능 |

에러 플래그는 **그 문자와 함께** RXNE 와 동시에 선다 (STM32 도 FE/NF/PE 가 RXNE 와 같은 때 선다고 설명 — RM 확인 권장). 그래서 ISR 은 "상태 레지스터 먼저, 데이터 나중" 순서로 읽는다. 우리 모델에서는 `uart_read` 한 번이 모든 플래그를 지우지만, 실제 칩은 다르다: STM32F1/F4 는 "SR 읽기 → DR 읽기" 순서로 PE/FE/NE/ORE 가 지워지고, F0/F3/F7/L4/G4/H7 계열은 ISR 을 읽고 ICR 에 써서 지운다 (RM 확인 권장). ORE 를 안 지우면 RXNE 인터럽트가 계속 다시 들어오는 "인터럽트 폭풍"이 흔한 버그다.

**글리치 길이 L (idle 중 low 펄스, 실제 출력).**

```text
glitch L | 8N1            | 8E1
    1    | false start    | false start
   ...   (2–7 도 같음)
    8    | false start    | false start
    9    | 0xFF NF        | 0xFF NF PE
   10    | 0xFF           | 0xFF PE
   ...   (11–16 도 같음)
```

L ≤ 8 이면 샘플 8/9/10 중 0 이 많아야 하나라 거부된다. L = 9 부터는 다수결이 0 → start 인정 → 그 뒤 라인은 계속 1 이니 data = 0xFF, stop = 1 → **유령 바이트 0xFF**. L = 9 는 "8·9 는 0, 10 은 1" 이라 NF 가 서지만 L ≥ 10 은 플래그 없이 조용하다. 8E1 이면 0xFF 의 짝수 parity 비트는 0 이어야 하는데 라인은 1 이라 PE 가 잡는다. 실무에서 보드 전원 투입·케이블 연결 순간에 0xFF(또는 0x00 + FE)가 하나 들어오는 현상이 바로 이것이다 — 부트로더 프로토콜이 첫 바이트를 버리거나 동기 바이트를 기다리는 이유.

**data 비트 안 스파이크 ('A' 의 d1 = 0 에 1 을 덮음, 테스트 그대로).**

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">비트 하나(d1, 값 0)의 샘플 16개 — 색칠 = 1 로 덮인 샘플, 점선 = 판정 창 8·9·10</text>
  <g fill="currentColor" font-size="12">
    <text x="176" y="42">1</text><text x="202" y="42">2</text><text x="228" y="42">3</text><text x="254" y="42">4</text>
    <text x="280" y="42">5</text><text x="306" y="42">6</text><text x="332" y="42">7</text><text x="358" y="42">8</text>
    <text x="384" y="42">9</text><text x="406" y="42">10</text><text x="432" y="42">11</text><text x="458" y="42">12</text>
    <text x="484" y="42">13</text><text x="510" y="42">14</text><text x="536" y="42">15</text><text x="562" y="42">16</text>
  </g>
  <rect x="350" y="48" width="82" height="170" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <g fill="currentColor" font-size="12">
    <text x="10" y="68">스파이크 {9}</text><text x="10" y="102">스파이크 {3,4,5}</text><text x="10" y="136">스파이크 {8,9}</text>
    <text x="10" y="170">스파이크 {8,9,10}</text><text x="10" y="204">같은 것, 8E1</text>
  </g>
  <g stroke="currentColor" class="f-mut">
    <rect x="170" y="54" width="416" height="22"/><rect x="170" y="88" width="416" height="22"/>
    <rect x="170" y="122" width="416" height="22"/><rect x="170" y="156" width="416" height="22"/>
    <rect x="170" y="190" width="416" height="22"/>
  </g>
  <g stroke="currentColor" class="f-warn">
    <rect x="378" y="54" width="26" height="22"/>
    <rect x="222" y="88" width="78" height="22"/>
    <rect x="352" y="122" width="52" height="22"/>
    <rect x="352" y="156" width="78" height="22"/>
    <rect x="352" y="190" width="78" height="22"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="596" y="70">0,1,0 → 0 · NF</text>
    <text x="596" y="104">창 밖 → 0 (안 보임)</text>
    <text x="596" y="138">1,1,0 → 1 · NF</text>
    <text x="596" y="160">1,1,1 → 1</text><text x="596" y="174">조용한 오류</text>
    <text x="596" y="208">→ 1 · PE</text>
  </g>
  <text x="10" y="240" fill="currentColor" font-size="12">결과 바이트: 0x41 / 0x41 / 0x43 + NF / 0x43 / 0x43 + PE</text>
  <text x="10" y="258" fill="currentColor" font-size="12">창 전체를 덮는 펄스는 다수결이 못 막는다 → 1비트 오류를 잡는 parity 가 마지막 방어선</text>
</svg>
```

다수결이 막는 것은 "3샘플 중 1개"까지다. 2개가 뒤집히면 비트가 틀리지만 NF 로는 보이고, 3개 다 뒤집히면(≥ 3/16 비트 길이의 펄스가 정확히 중앙에) 수신기는 아무것도 모른다. 그래서 잡음 많은 링크는 parity 또는 상위 계층 CRC(COMM-B08, COMM-B10)가 필요하다.

**framing error 와 break.** 'A' 의 stop 을 0 으로 바꾸면 0x41 + FE, 다음 'B' 는 정상. 라인을 30비트 동안 0 으로 두면 **문자는 정확히 하나** (0x00, FE|BRK): stop 판정 뒤 IDLE 로 가지만 `prev = 0` 이라 라인이 1 로 돌아오기 전엔 새 edge 가 없다. 8O1 에서는 0x00 의 홀수 parity 비트가 1 이어야 하므로 PE 도 함께 선다 (테스트 `FE|BRK|PE`). 우리 BRK 정의는 "문자 하나 길이의 all-zero" 이고, LIN 처럼 **low 시간 길이**로 break 를 판정하는 방식은 연습 3 에서 따로 만든다.

**overrun.** 0x11 0x22 0x33 을 연달아 보내고 한 번도 안 읽으면: 수신기 로그엔 세 문자가 다 있지만(수신기는 받았다), SR = RXNE|ORE, RDR = 0x11, `n_overrun = 2`. 읽으면 0x11 과 ORE 가 함께 나오고, 그 뒤 0x44 는 정상. "옛 것을 지키고 새 것을 버리는" 정책은 STM32 RM 의 설명과 같은 방향이다 (RDR 내용은 유지, 시프트 레지스터가 덮어써짐 — RM 확인 권장). 반대 정책(새 것으로 덮어쓰기, ORE 없음)은 연습 1.

### Part C — ISR 예산 = 1문자 시간

RDR 이 1단이면, RXNE 가 선 순간부터 **다음 문자가 완료될 때까지**(연속 수신이면 정확히 1프레임 = 160 틱) 안에 읽어야 한다. 같은 틱에 둘이 겹치면 하드웨어가 먼저다. 실제 출력:

```text
isr latency (ticks) | bytes in ring / 64 | ORE seen | lost (n_overrun)
     0 ( 0.00 bit)  |        64          |    0     |    0
    80 ( 5.00 bit)  |        64          |    0     |    0
   159 ( 9.94 bit)  |        64          |    0     |    0
   160 (10.00 bit)  |        32          |   32     |   32
   200 (12.50 bit)  |        32          |   32     |   32
   320 (20.00 bit)  |        22          |   21     |   42
   479 (29.94 bit)  |        22          |   21     |   42
   480 (30.00 bit)  |        16          |   16     |   48
```

경계가 칼같다: 159 틱이면 무손실, 160 틱이면 절반을 잃는다. 지연이 m 프레임(160m ≤ L < 160(m+1))이면 ISR 한 번 사이에 m 문자가 버려져 **(m+1) 개 중 1개만** 남는다 — 64 × 1/2 = 32, ⌈64/3⌉ = 22, 64/4 = 16. "옛 것 유지" 정책이라 남는 것은 0, 2, 4, … 번째 바이트이고 테스트가 그 지문까지 확인한다.

```svg
<svg viewBox="0 0 740 220" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="c2b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">ISR 지연 200 틱 (12.5 비트), 8N1 연속 수신 — 문자 완료는 160 틱마다</text>
  <line x1="40" y1="120" x2="720" y2="120" stroke="currentColor"/>
  <g stroke="currentColor">
    <rect x="40" y="70" width="120" height="30" class="f-ok"/><rect x="160" y="70" width="120" height="30" class="f-warn"/>
    <rect x="280" y="70" width="120" height="30" class="f-ok"/><rect x="400" y="70" width="120" height="30" class="f-warn"/>
    <rect x="520" y="70" width="120" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="70" y="90">char0 → RDR</text><text x="185" y="90">char1 유실</text><text x="305" y="90">char2 → RDR</text>
    <text x="425" y="90">char3 유실</text><text x="545" y="90">char4 → RDR</text>
  </g>
  <g stroke="currentColor"><line x1="160" y1="114" x2="160" y2="126"/><line x1="280" y1="114" x2="280" y2="126"/>
    <line x1="400" y1="114" x2="400" y2="126"/><line x1="520" y1="114" x2="520" y2="126"/><line x1="640" y1="114" x2="640" y2="126"/></g>
  <g fill="currentColor" font-size="12">
    <text x="140" y="140">RXNE</text><text x="262" y="140">ORE</text><text x="380" y="140">RXNE</text><text x="502" y="140">ORE</text><text x="620" y="140">RXNE</text>
    <text x="146" y="62">0</text><text x="262" y="62">160</text><text x="382" y="62">320</text><text x="502" y="62">480</text><text x="622" y="62">640</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#c2b)">
    <path d="M160 150 L310 150"/><path d="M400 150 L550 150"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="170" y="168">+200 틱: char0 + ORE 읽음</text>
    <text x="420" y="168">+200: char2 + ORE</text>
    <text x="10" y="192">위 숫자 = 문자 완료 시각(틱). 그 순간 RXNE 가 아직 1 이면 새 문자를 버린다</text>
    <text x="10" y="210">→ 짝수 번째만 남는다 (테스트가 out[i] == tx[2i] 를 확인)</text>
  </g>
</svg>
```

실제 숫자로 바꾸면: 1프레임 = 10비트 / baud. 115200 baud 에서 86.8 µs, 921600 에서 10.85 µs, 3 Mbaud 에서 3.33 µs. Cortex-M 의 하드웨어 진입 지연(12사이클 안팎, 대표값 — ARM-A08, BM-A10)은 문제가 아니고, **더 높은 우선순위 ISR 과 인터럽트를 막은 임계 구역의 최악 길이**가 이 예산을 넘는지가 문제다. 넘는다면 HW FIFO(16550 은 16바이트, STM32 일부 패밀리는 FIFO 모드 — 데이터시트 확인 권장)나 DMA(COMM-M11)로 깊이를 늘린다.

**링 버퍼는 다른 문제를 푼다.** ISR 이 제때 돌면 HW overrun 은 없다. 하지만 main 루프가 링을 늦게 비우면 **SW 링 오버플로**가 난다: 16칸 링에 160 틱마다 1바이트가 들어오므로 비우는 주기가 2400 틱(15문자)이면 무손실, 4000 틱(최대 25문자)이면 `kept 167, ring_drop 89, ORE 0` (실제 출력). 두 손실은 원인도 대책도 다르다 — ORE 는 ISR 지연(우선순위·임계 구역), ring_drop 은 소비자 처리량(링 크기·태스크 주기). 카운터를 따로 두어야 현장에서 구분된다. 링의 ISR↔main 공유는 단일 생산자·단일 소비자라 head 는 ISR 만, tail 은 main 만 쓴다 (DS-B04, DS-A02; 실제 코드에선 volatile 과 메모리 순서 — C-B09, C-A02).

`uart_isr` 의 분류 순서도 의미가 있다: BRK 는 FE 의 특수형이라 먼저 본다. NF 바이트는 다수결 값이 맞을 확률이 높으므로 받되 세고, FE/PE 바이트는 버린다 (정책이다 — 상위 계층에 CRC 가 있으면 다 넘기고 프레이밍 레이어가 판단해도 된다).

### 실험 — 샘플 잡음 vs 다수결 (`demo_noise.c`)

모든 샘플(idle 포함)을 독립적으로 확률 p 로 뒤집고, idle 2비트 + 랜덤 1문자 + idle 3비트를 행마다 200,000번 보낸다. 수신기 세 가지: **maj3**(solution 그대로), **single**(샘플 9 하나만), **naive**(maj3 에서 재무장을 뺀 것). 이론값은 비트 하나가 틀릴 확률 q (single: q = p, maj3: q = 3p²(1−p) + p³) 로 1 − (1 − q)^(프레임 비트 수). 실제 출력에서 발췌:

| cfg | p | 수신기 | 프레임 오류 (실측) | 이론 | 그중 조용한 오류 (FE/PE·NF 모두 없음) | 정상 프레임 중 NF 비율 |
|---|---|---|---|---|---|---|
| 8N1 | 0.001 | maj3 | 0.0050% | 0.0030% | 0.0000% | 3.01% |
| 8N1 | 0.001 | single | 0.9670% | 0.9955% | 0.8500% | 0% |
| 8N1 | 0.001 | naive | 0.1165% | 0.0030% | 0.0530% | 3.02% |
| 8N1 | 0.01 | maj3 | 0.4015% | 0.2976% | 0.0200% | 26.51% |
| 8N1 | 0.01 | single | 9.6995% | 9.5618% | 8.5045% | 0% |
| 8N1 | 0.01 | naive | 1.2835% | 0.2976% | 0.3925% | 26.53% |
| 8E1 | 0.01 | maj3 | 0.4270% | 0.3273% | 0.0000% | 28.70% |
| 8E1 | 0.01 | single | 10.7640% | 10.4662% | 0.4265% | 0% |
| 8N1 | 0.1 | maj3 | 29.3240% | 24.7229% | 0.1155% | 94.63% |

읽는 법:

- **다수결은 오류율을 p → 약 3p² 로 바꾼다.** p = 1% 에서 프레임 오류 9.7% → 0.40% (약 24배). single 은 이론과 거의 같고, maj3 가 이론보다 조금 높은 것은 edge 검출 자체도 샘플 하나(1→0)에 의존해 잡음이 start 위치를 몇 샘플 옮기기 때문이다 (k = 8 처럼 경계에 걸치는 경우).
- **NF 는 쓸모 있는 경보다.** maj3 의 조용한 오류 중 NF 조차 없는 것은 p = 1% 에서 0.02% 뿐이고, 정상 프레임의 NF 비율은 "검사한 샘플 30개 중 하나라도 뒤집힘" = 1 − 0.99³⁰ = 26.0% 와 맞는다 (8E1 은 33개 → 28.2%, 실측 28.7%). NF 카운터가 오르면 링크 품질이 나빠지는 중이다.
- **parity 는 조용한 오류를 오류 보고로 바꾼다.** single 수신기 8N1 의 조용한 오류 8.5% 가 8E1 에서는 0.43% 로 줄고 나머지는 PE 로 보고된다.
- **naive 의 초과분 ≈ p.** p = 0.001 에서 naive − maj3 ≈ 0.11% ≈ p — "삼켜진 edge" 위치가 정확히 한 샘플이라는 분석과 맞는다. 재무장 한 줄의 효과가 다수결 자체만큼 크다.

`cc -std=c11 -Wall -Wextra -Werror -O2 demo_noise.c -o demo && ./demo` — 이 맥에서 약 3.5초 (30행 × 200,000 시행).

### 복잡도와 실제 하드웨어

틱당 O(1), 분기 몇 개. 상태는 ph·smp·nbit·shift·prev 등 20바이트 남짓 — 실제 USART 에선 이 전부가 하드웨어이고 펌웨어는 RXNE/에러 플래그 처리, 즉 Part B 의 `uart_read` 와 Part C 의 ISR 만 쓴다. 이 모델이 쓸모 있는 곳: 소프트웨어 UART(GPIO 타이머 샘플링), FPGA/RTL 수신기의 레퍼런스 모델(SIV 트랙의 골든 모델), 로직 애널라이저 디코더(COMM-A10, HWLAB-M06), 그리고 "플래그가 왜 섰나"를 설명하는 면접.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution      # 35/35 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan
```

실행 시간 < 0.01초. ASan/UBSan 경고 없음.

## 7. 자주 하는 실수

1. **edge 에서 바로 샘플링을 시작.** "edge 뒤 16틱마다 읽기"로 짜면 각 비트의 **경계**를 읽는다. → 첫 판정은 edge 뒤 반 비트(샘플 8~10), 그 뒤 16샘플 간격. 우리 코드에선 `ph` 를 1부터 세고 판정을 `ph == 10` 에 두는 것으로 해결된다.
2. **false start 거부 후 `prev = s`.** 거부 시점에 진짜 start 가 이미 시작돼 있으면 edge 를 영영 놓치고, 데이터 중간의 1→0 을 start 로 읽어 **플래그 없는 쓰레기**(실험: 'A' → 0xD0)가 나온다. → 거부할 때 `prev = 1; return;`.
3. **stop 비트 끝까지 기다렸다가 IDLE.** 송신 클럭이 빠르면 다음 start edge 를 놓친다. → stop 판정(10번)에서 바로 IDLE.
4. **break 를 매 문자마다 보고.** IDLE 진입 시 prev 를 1 로 리셋하면(혹은 "라인이 0 이면 start" 로 짜면) 긴 break 가 0x00+FE 문자 수십 개가 된다. → 수신기는 1→0 **edge** 에서만 시작하고, stop 판정 뒤엔 실제 라인 값(0)을 prev 로 둔다.
5. **overrun 때 RDR 를 덮어쓰고도 ORE 를 세움.** 드라이버는 "ORE 와 함께 온 바이트 = 유실 직전의 옛 바이트" 라고 가정하는데 실제론 새 바이트라면 어느 바이트가 사라졌는지 틀리게 기록된다. → 정책을 하나로 정하고(옛 것 유지 + ORE, 또는 덮어쓰기 + ORE 없음) 문서화. 실제 칩에서는 RM 의 설명을 따른다.
6. **ISR 에서 데이터만 읽고 상태를 안 봄.** FE/PE 문자를 정상으로 링에 넣고, ORE 를 안 지워 RXNE 인터럽트가 무한 반복된다(실제 칩). → 상태 먼저, 분류, 지우기.
7. **링 크기를 늘려 ORE 를 고치려 함.** 링은 ISR 이 제때 돈 뒤의 문제다. ORE 는 1단 RDR 의 예산(1문자 시간) 문제라 링 크기와 무관하다 (Part C 표: 링이 비어 있어도 L = 160 이면 절반 유실). → 우선순위·임계 구역 단축, FIFO, DMA.

## 8. 면접 Q&A

<details><summary>Q1. 왜 16배로 오버샘플링하나요? 1배로는 안 되나요?</summary>

수신기는 송신 클럭의 위상을 모른다. 1배로 샘플링하면 샘플이 비트의 어디에 떨어질지 모르고, 경계 근처면 반은 틀린다. 16배면 start edge 위치를 1/16 비트 정밀도로 알 수 있어 이후 비트의 중앙을 ±1/16 비트 오차로 겨냥할 수 있다. 배수가 클수록 edge 위치 불확실성이 줄어 클럭 오차 허용이 커지고(COMM-B03), 다수결용 샘플을 여러 개 뽑을 수 있다. 대가는 샘플 클럭 = 16 × baud — 같은 주변장치 클럭으로 낼 수 있는 최대 baud 가 1/16 로 준다. 그래서 STM32 는 OVER8(8배) 모드를 두어 최대 baud 를 두 배로 하고 허용 오차를 줄인다 (연습 2, 대표값).

</details>

<details><summary>Q2. 샘플 3개 다수결은 무엇을 막고 무엇을 못 막나요? NF 는 어떻게 쓰나요?</summary>

판정 창(샘플 8~10, 3/16 비트) 안의 1샘플 스파이크는 막고 NF 로 보고한다. 창 밖 스파이크는 원래 보이지 않는다. 2샘플이 뒤집히면 비트가 틀리지만 NF 는 선다. 창 전체를 덮는 펄스는 다수결이 못 보고 parity/CRC 의 몫이다. 실험(p = 1%)에서 프레임 오류는 9.7% → 0.4%, 조용한 오류 중 NF 조차 없는 것은 0.02%. NF 바이트는 대개 값이 맞으므로 받되 카운터를 올리고, 카운터 추세로 링크 품질(케이블·접지·EMI·baud 오차)을 모니터링한다.

</details>

<details><summary>Q3. framing error 와 break 는 어떻게 다르고, 각각 보이면 무엇을 의심하나요?</summary>

FE = stop 위치에서 0. 가끔이면 잡음, **연속이면 baud 불일치**(샘플 위치가 밀려 stop 자리에서 다음 data 를 읽음)나 설정 불일치(7E1 송신을 8N1 로 수신). break = 라인이 1문자 시간 넘게 0: 데이터 0x00 + FE (16550 은 BI 비트, nRF 는 BREAK). 원인은 의도된 break(LIN 헤더, 일부 부트로더의 "리셋/동기" 신호, 콘솔의 SysRq) 또는 **선이 끊기거나 TX 쪽이 리셋 중**(RS-232 트랜시버는 미연결 입력을 보통 1 로 끌어 올리지만 TTL 레벨 라인이 GND 로 떨어져 있으면 계속 0). 긴 break 가 문자 하나로만 보고되는 것은 수신기가 edge 에서만 시작하기 때문이다.

</details>

<details><summary>Q4. 115200 8N1 에서 RX ISR 은 얼마나 늦어도 되나요? 꼬리: 3 Mbaud 라면?</summary>

1단 RDR 이면 RXNE 부터 다음 문자 완료까지 = 1프레임 = 10/115200 = 86.8 µs (시뮬레이션에서 159 틱 OK, 160 틱 유실). 이 안에 더 높은 우선순위 ISR 들의 최악 실행 시간 + 인터럽트 금지 구간 최댓값이 들어가야 한다. 3 Mbaud 면 3.33 µs — 80 MHz 코어로 약 266 사이클이라 다른 ISR 하나만 있어도 위험하다. 그때는 HW FIFO + 임계값 인터럽트, 또는 circular DMA + IDLE line 인터럽트(COMM-M11)로 바이트당 인터럽트를 없앤다. 검증은 GPIO 토글 + 로직 애널라이저로 ISR 최악 지연을 재거나 DWT CYCCNT(BM-A10).

</details>

<details><summary>Q5. overrun 이 나면 어떤 바이트가 사라지나요? 드라이버는 어떻게 복구하나요?</summary>

칩의 정책에 따라 다르다. 우리 모델과 STM32 설명은 "RDR 의 옛 바이트 유지, 새로 들어온 것 유실"(RM 확인 권장) — 지연 200 틱 실험에서 짝수 번째만 남았다. 덮어쓰기 정책이면 홀수 번째가 남고 ORE 조차 없다(연습 1). 어느 쪽이든 바이트 스트림에 **구멍**이 났다는 사실만 확실하므로, 드라이버는 ORE 를 지우고 카운트한 뒤 상위 프레이밍 레이어에 "재동기 필요"를 알린다 — 길이+CRC 프레임(COMM-B10)이나 COBS(COMM-B12)처럼 구분자로 재동기할 수 있는 형식이 그래서 필요하다.

</details>

<details><summary>Q6. 링 버퍼가 있는데도 바이트가 사라집니다. ORE 카운터는 0 입니다. 어디를 보나요?</summary>

HW 는 제때 읽혔고(ORE 0) 링에서 넘쳤다 — 소비자(main 루프/태스크)가 느리다. 실험: 16칸 링, 160 틱마다 1바이트, 비우는 주기 2400 틱이면 무손실, 4000 틱이면 89바이트 드롭. 대책: 링을 (최악 소비 지연 / 문자 시간) 이상으로 키우기, 소비 태스크 우선순위·주기 조정, 링이 반쯤 차면 RTS 를 내려 흐름 제어(RTS/CTS), 또는 ISR 에서 세마포어로 태스크를 깨우기(RTOS-B08). ring_drop 과 ORE 를 따로 세는 것 자체가 디버깅 설계다.

</details>

<details><summary>Q7. 전원을 켜거나 케이블을 꽂으면 0xFF 나 0x00 이 하나 들어옵니다. 왜죠?</summary>

글리치 실험 그대로다: idle 라인에 9샘플(0.56비트) 이상 low 펄스가 생기면 start 로 인정되고, 이후 라인이 1 이므로 0xFF 가 된다 (8E1 이면 PE). 반대로 TX 핀이 리셋 중 low 로 떠 있다가 올라가면 0x00 + FE(break 형태). 대책: 부트로더/프로토콜은 동기 바이트나 프레임 구분자를 기다리고 그 전 바이트를 버린다, 초기화 직후 RX 를 한 번 비우고 에러 플래그를 지운다, TX 핀에 풀업을 두어 리셋 중에도 idle(1)이 되게 한다.

</details>

<details><summary>Q8. 이 수신기를 RTL 이나 소프트웨어 UART 로 옮기면 무엇이 달라지나요?</summary>

RTL 은 거의 그대로다: 16x 틱 enable, 4비트 샘플 카운터(ph), 비트 카운터, 시프트 레지스터, 3비트 다수결, 상태 레지스터 — 이 C 모델을 골든 레퍼런스로 두고 같은 샘플 스트림을 시뮬레이터와 C 에 넣어 로그를 비교한다(SIV 트랙). 소프트웨어 UART 는 16x 타이머 인터럽트가 CPU 를 너무 많이 먹으므로 보통 start edge 를 GPIO 인터럽트로 잡고 1.5비트 뒤 타이머로 한 번, 이후 1비트마다 1샘플 — 즉 single 수신기라 잡음에 약하다(실험: p = 1% 에서 9.7% vs 0.4%).

</details>

## 9. 추가 연습

### 연습 1 — overrun 덮어쓰기 정책

RXNE 중에 완료된 새 문자가 RDR 를 **덮어쓰고 ORE 를 세우지 않는** 정책(STM32 새 패밀리의 OVRDIS = 1 이 이런 동작이라고 설명됨 — RM 확인 권장)으로 `rx_deliver` 를 바꾸고, 지연 200 틱에서 어떤 바이트가 남는지 예측·확인하라.

<details><summary>답 보기</summary>

```c
if (r->sr & SR_RXNE) {
    r->n_overrun++;
    if (!r->overwrite) { r->sr |= SR_ORE; return; }   /* 본문: 옛 것 유지 + ORE */
    r->rdr = data;                                     /* 덮어쓰기: 새 것, ORE 없음 */
    r->sr = (uint8_t)(SR_RXNE | flags);
    return;
}
```

char1 이 160 틱에 char0 을 덮고, 200 틱 ISR 이 char1 을 읽는다 → **홀수 번째** 32개, ORE 는 한 번도 안 보인다. 본문 정책은 짝수 번째 32개 + ORE 32번. 지연 159 틱이면 두 정책 모두 64개 무손실. 덮어쓰기는 "최신 값이 중요한 센서 스트림"엔 낫지만, 유실을 알릴 방법이 사라지므로 상위 계층에 시퀀스 번호나 CRC 가 있어야 한다.

</details>

### 연습 2 — 8x 오버샘플링 (OVER8)

비트당 8샘플, 판정 샘플 4/5/6 으로 수신기를 일반화하라 (STM32 OVER8 = 1 의 대표값). 256바이트 × 8N1/8E1 전수 왕복과, idle 글리치 L = 1~8 의 결과를 확인하라.

<details><summary>답 보기</summary>

`ovs` 와 첫 판정 샘플 `sa` 를 매개변수로: `ph` 가 `sa..sa+2` 일 때 모으고 `sa+2` 에서 판정, `ph == ovs` 에서 다음 비트. 실제 출력 `ex2: OVER8 glitch L: 1=rej 2=rej 3=rej 4=rej 5=FF+NF 6=FF 7=FF 8=FF` — 거부 경계가 16x 의 8샘플(0.5비트)에서 4샘플(0.5비트)로, **비트 단위로는 같고** 시간 해상도만 절반이다. 대가: edge 위치 불확실성이 1/8 비트로 커져 클럭 오차 허용이 준다(COMM-B03 에서 정량). 이득: 같은 주변장치 클럭으로 최대 baud 2배.

</details>

### 연습 3 — LIN 식 break 검출

라인이 연속 low 인 시간을 세어 **11비트 시간 이상**이면 break 를 한 번 보고하는 검출기를 만들라 (LIN 2.x: 마스터는 13비트 이상 break 를 보내고 슬레이브는 11비트 문턱으로 검출 — 대표값, AUTO-B06). 0x00(8N1), 0x00(8E1), 13비트 break 를 순서대로 보내고, 문턱 11 과 10 의 결과를 비교하라.

<details><summary>답 보기</summary>

```c
static void brk_tick(brk_det_t *b, int s) {
    if (s) { b->low_run = 0; b->fired = false; return; }
    if (++b->low_run >= b->thr && !b->fired) { b->fired = true; b->n_break++; }
}
```

0x00 8N1 의 low 는 start + data 8 = **9비트**, 8E1 은 parity 0 까지 **10비트**. 문턱 11 → break 1번(진짜 break 만), 문턱 10 → 2번(8E1 의 0x00 을 break 로 오인). 100비트짜리 low 도 `fired` 덕분에 한 번만. STM32 LIN 모드의 LBDL(10/11비트 선택)이 이 문턱이다 (RM 확인 권장). 본문 BRK(문자 단위 all-zero + FE)와 달리 길이로 판정하므로 "0x00 데이터 + FE"와 "진짜 break" 를 구분한다.

</details>

### 연습 4 — IDLE line 검출

문자가 완료된(stop 판정) 뒤 라인이 **1문자 시간(8N1 = 160샘플)** 동안 계속 high 이면 IDLE 이벤트를 한 번 내고, 다음 문자가 완료될 때 다시 무장하라. "HELLO" 연속 송신 뒤 IDLE 시각, 그리고 문자 사이 gap 9비트 / 10비트에서 IDLE 횟수를 확인하라.

<details><summary>답 보기</summary>

high 샘플 수를 세다가 0 이면 리셋, stop 판정 순간 0 으로 리셋하고 무장. 'O' 의 완료 틱 = 20 + 4·160 + 1 + 9·16 + 9 = 814 → IDLE = 814 + 160 = **974** (테스트). gap 9비트면 문자 사이 high = stop 의 나머지 6샘플 + 144 = 150 < 160 → 버스트 끝에만 IDLE 1번. gap 10비트면 166 ≥ 160 → 문자마다 5번. 이것이 circular DMA 수신에서 "패킷 끝"을 아는 방법(COMM-M11)이고, 문턱(1문자 시간)보다 문자 간 간격이 긴 송신기를 만나면 패킷이 쪼개진다 — 실제 칩의 정확한 IDLE 정의는 RM 확인 권장.

</details>

### 연습 5 — 재무장을 빼면: "삼켜진 edge" 재현

본문 수신기에서 false start 거부 시의 `prev = 1; return;` 을 빼고(naive), 'A' 'B' 앞 k = 1~18 샘플 위치에 1샘플 low 글리치를 넣어 어느 k 에서 무엇이 나오는지 확인하라.

<details><summary>답 보기</summary>

실제 출력 `ex5: naive bad k=9 ('A' -> 0xD0 flags 0x00), fixed bad=0`. k = 9 에서만, 플래그 없이 0xD0. 이유: 거부 판정(샘플 10)이 진짜 start 의 첫 0 샘플과 겹쳐 `prev = 0` 이 되고, 다음 1→0 은 d0(1)→d1(0) 경계다. 거기서 읽은 비트는 d2..d7, stop, idle = 0,0,0,0,1,0,1,1 → LSB first 로 0xD0, stop 자리는 idle(1)이라 FE 도 없다. 수정판은 18개 위치 전부 정상(k = 8 만 NF). 잡음 실험에서 이 한 위치가 p = 0.1% 일 때 오류의 95% 를 차지했다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 13/13 passed
```

## 10. 핵심 정리

- 16x 수신기: idle 의 1→0 이 start 의 1번 샘플, 각 비트의 샘플 8/9/10 다수결, 판정은 ph = 10 에서 한 번, ph = 16 에서 다음 비트. 'A' 8N1: edge t = 21 → 완료 t = 174 (= 21 + 9·16 + 9).
- start 확인이 1 이면 false start: idle 글리치 ≤ 8샘플은 거부, 9샘플은 0xFF+NF, ≥ 10 은 조용한 0xFF (8E1 이면 PE). 거부할 땐 `prev = 1` 로 재무장 — 안 하면 k = 9 글리치가 'A' 를 플래그 없는 0xD0 으로 만든다.
- 다수결은 1/3 스파이크를 막고 NF 로 알린다. 2/3 은 틀리지만 NF, 3/3 은 조용한 오류 → parity/CRC 의 몫. 실측 p = 1%: 9.7% → 0.4%.
- FE = stop 0, BRK = FE + all-zero (긴 break 도 문자 하나: edge 에서만 시작하니까), PE = parity, ORE = RXNE 중에 다음 문자 완료.
- 1단 RDR 의 ISR 예산 = 1문자 시간 (8N1 = 10비트, 115200 에서 86.8 µs). 159 틱 OK / 160 틱 절반 유실. 옛 것 유지 정책이면 짝수 번째만 남는다.
- ORE(ISR 지연)와 ring_drop(소비자 처리량)은 다른 병이다 — 따로 세라. 깊이가 필요하면 FIFO / DMA + IDLE(COMM-M11).
- 실제 칩의 샘플 위치·플래그 클리어·overrun 정책은 RM 이 정답: 이 모델은 동작 원리와 면접 설명용.

## 참고자료

- STMicroelectronics, RM0090 (STM32F405/407 … Reference Manual), USART 장: "Receiver" — start bit detection, 16x/8x 오버샘플링(OVER8), 샘플 8/9/10 다수결과 NE, ONEBIT, ORE/FE/PE 와 SR→DR 클리어 순서, LIN break 검출(LBDL). 이 레슨의 STM32 세부는 기억에 의존 — 확인 권장. <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>
- STMicroelectronics, RM0351 (STM32L4) 등 새 패밀리 USART 장 — ISR/ICR 레지스터, OVRDIS, FIFO 모드(지원 패밀리 한정). 확인 권장.
- Texas Instruments, PC16550D UART datasheet — LSR 의 OE/PE/FE/BI 비트, 16바이트 FIFO, 수신 샘플링(16x 클럭). <https://www.ti.com/product/PC16550D>
- Nordic Semiconductor, nRF52840 Product Specification — UARTE 의 ERRORSRC (OVERRUN / PARITY / FRAMING / BREAK) 와 EasyDMA 수신. <https://docs.nordicsemi.com/>
- Maxim Integrated (ADI), Application Note 2141 "Determining Clock Accuracy Requirements for UART Communications" — 샘플링 위치와 클럭 오차 허용 (COMM-B03 에서 정량). 번호·제목은 기억 — 확인 권장.
- LIN Specification Package 2.x — break field(≥ 13비트 dominant)와 슬레이브 검출 문턱 11비트. 대표값.
- ARM, Cortex-M3/M4 Technical Reference Manual — 예외 진입 지연(12사이클 안팎). 대표값.
- 관련 레슨: COMM-B01 (UART 프레임), COMM-B03 (baud 오차·허용 한계), COMM-B08 (체크섬 vs CRC), COMM-B10 (길이+CRC 프레임 재동기), COMM-B12 (COBS), COMM-B14 (CLI/AT 파서), COMM-M11 (DMA + IDLE), COMM-A10 (LA 디코더), HWLAB-M05 (UART 타이밍 실측), HWLAB-M06 (디코더), DS-B04 (마스크 링 버퍼), DS-A02 (ISR↔태스크 링), C-B09 / C-A02 (volatile·메모리 순서), BM-B12 / BM-M04 (폴링·인터럽트 UART 드라이버), ARM-A08 / BM-A10 (인터럽트 지연), RTOS-B08 (ISR → 태스크 신호), AUTO-B06 (LIN), SIV-M03 (인터럽트 유실 검출)
