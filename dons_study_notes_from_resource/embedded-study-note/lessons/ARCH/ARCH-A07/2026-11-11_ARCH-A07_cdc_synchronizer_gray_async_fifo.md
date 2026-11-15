---
id: ARCH-A07
time: 120분
updated: 2026-11-11
---
# ARCH-A07 클럭 도메인 교차: 동기화기와 Gray 코드 FIFO

## 학습 목표

- 두 클럭(100 MHz → 77 MHz처럼 정수비와 거리가 먼 비율, 빠름→느림, 느림→빠름, 거의 같은 주파수)을 ps 단위 **이산 사건 시뮬레이션**으로 돌리고, "창 안에서 바뀐 비트는 무작위로 결정된다"는 **메타안정성 모델**을 직접 짠다. 이 모델이 무엇을 보여 주고 무엇을 못 보여 주는지 말할 수 있다.
- 2-FF 동기화기의 지연이 **1~2 목적지 주기**(정확히는 `Td + tcq − th` 이상, `2Td + tcq + tsu` 미만)라는 것을 유도하고 시뮬레이션으로 확인한다. 창 적중률이 `T_W / Td` 로 수렴하는 것을 보고, 그것이 MTBF 식의 분모 `T_W · f_clk · f_data` 그 자체라는 것을 연결한다.
- 깊이 2^N 비동기 FIFO 를 (N+1)비트 포인터로 만들고, **이진 포인터를 비트마다 동기화**하면 동기화된 값이 "있을 수 없는 값"이 되는 것을 스코어보드로 잡는다. 그것이 언제 실제 overflow/underflow 로 번지는지(레벨·버스트로 쓸 때)까지 구분한다.
- Gray full 조건 `wg == {~rq2[N:N-1], rq2[N-2:0]}` 를 한 줄로 유도하고, 이진 차이 정의와 **전수 비교**(N = 1..7, 87 376 쌍)한다. full/empty 가 **보수적(늦게 풀림)이지만 절대 틀리지 않는** 이유를 증명하고, 그 비관성이 FIFO 깊이 결정에 어떻게 들어가는지 숫자로 본다.
- 펄스(토글) 동기화기, 4-phase / 2-phase req-ack 버스 핸드셰이크를 만들고 "버스 비트마다 2-FF" 가 왜 틀리는지 섞인 단어 개수로 보인다. 리셋 동기화기(비동기 assert · 동기 deassert)는 RTLDV-B06 으로 넘긴다.

## 1. 문제

"비동기 FIFO 포인터를 왜 Gray 코드로 넘기나요?" — SoC·FPGA·실리콘 검증 면접의 단골 질문이고, 실제 칩에서 가장 재현하기 어려운 버그(가끔, 특정 보드에서만, 온도 따라)가 여기서 나온다.

ARCH-B13 에서 setup/hold 를 지키지 못하면 플롭이 **준안정(metastability)** 에 빠질 수 있고 2-FF 동기화기로 확률을 관리한다는 것까지 봤다 (MTBF 2-FF vs 3-FF 계산은 B13 연습 5). 이 레슨은 그 다음 단계 — **값 하나가 아니라 여러 비트, 펄스, 데이터 스트림**을 두 클럭 사이로 넘기는 설계를 시뮬레이터로 검증한다. 펌웨어에서도 매일 만난다: 버스 클럭과 따로 도는 주변장치 커널 클럭(UART 보레이트 클럭, ADC 클럭), 32.768 kHz 로 도는 RTC 레지스터를 APB 로 읽을 때의 shadow 레지스터 동기화(예: STM32 RTC 의 RSF 플래그 — 계열마다 세부가 다르다), 카메라 픽셀 클럭 → ISP 클럭의 비동기 FIFO, 다른 클럭으로 도는 64-bit 타이머를 CPU 가 읽을 때의 "두 번 읽어 같을 때까지" 패턴.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">비동기 FIFO — 포인터만 도메인을 건너고, 데이터는 메모리에 머문다</text>
  <g stroke="currentColor">
    <rect x="10" y="34" width="230" height="250" rx="6" class="f-ok"/>
    <rect x="500" y="34" width="230" height="250" rx="6" class="f-acc"/>
    <rect x="290" y="60" width="160" height="70" rx="4" class="f-mut"/>
    <rect x="30" y="70" width="190" height="40" rx="4" class="f-mut"/>
    <rect x="30" y="200" width="190" height="40" rx="4" class="f-mut"/>
    <rect x="520" y="70" width="190" height="40" rx="4" class="f-mut"/>
    <rect x="520" y="200" width="190" height="40" rx="4" class="f-mut"/>
    <rect x="300" y="160" width="140" height="34" rx="4" class="f-warn"/>
    <rect x="300" y="220" width="140" height="34" rx="4" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="54">쓰기 도메인 (wclk)</text>
    <text x="510" y="54">읽기 도메인 (rclk)</text>
    <text x="300" y="82">메모리 2^N 칸</text>
    <text x="300" y="100">쓰기: wclk 에지</text>
    <text x="300" y="118">읽기: rclk 에서 바로</text>
    <text x="40" y="88" font-size="12">wbin (N+1 비트) → wgray</text>
    <text x="40" y="104" font-size="12">플롭 출력만 내보낸다</text>
    <text x="40" y="218" font-size="12">full = wgray == rq2 ^ 0b11..</text>
    <text x="40" y="234" font-size="12">rq2 = 2-FF 거친 rgray</text>
    <text x="530" y="88" font-size="12">rbin (N+1 비트) → rgray</text>
    <text x="530" y="104" font-size="12">플롭 출력만 내보낸다</text>
    <text x="530" y="218">empty = rgray == wq2</text>
    <text x="530" y="234" font-size="12">wq2 = 2-FF 거친 wgray</text>
    <text x="310" y="182">2-FF (rclk 쪽)</text>
    <text x="310" y="242">2-FF (wclk 쪽)</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5" fill="none">
    <path d="M220 95 L290 95"/><path d="M282 90 L290 95 L282 100"/>
    <path d="M450 95 L520 95"/><path d="M512 90 L520 95 L512 100"/>
    <path d="M220 104 L270 104 L270 177 L300 177"/><path d="M292 172 L300 177 L292 182"/>
    <path d="M440 177 L600 177 L600 200"/><path d="M595 192 L600 200 L605 192"/>
    <path d="M610 110 L610 140 L470 140 L470 237 L440 237"/><path d="M448 232 L440 237 L448 242"/>
    <path d="M300 237 L150 237 L150 240"/>
  </g>
  <text x="250" y="276" fill="currentColor" font-size="12">wgray → (rclk 2-FF) → wq2 → empty   ·   rgray → (wclk 2-FF) → rq2 → full</text>
</svg>
```

**모델 (교육용 가정, 실제 셀 값 아님):**

| 항목 | 값 | 의미 |
|---|---|---|
| `tsu`, `th` | 150 ps, 100 ps | 창(aperture) `T_W = tsu + th = 250 ps`. 변화 시각이 `(t − tsu, t + th]` 안이면 그 비트는 0/1 무작위 |
| `tcq` | 200 ps | 소스 에지 t 에 잡은 값이 와이어에서 바뀌는 시각 = t + tcq (+ 비트별 skew) |
| skew | 포인터 30 ps/bit, 버스 40 ps/bit | 배선 길이 차. 여러 비트가 "동시에" 바뀌어도 도착은 제각각 |
| 지터 | 0 (Part A) / ±40 ps (FIFO, 핸드셰이크) | 에지마다 균등 분포, 고정 시드 xorshift32 |
| 클럭 | 100→77, 200→31, 31→200, 100→99.97 MHz | 주기 10000/12987, 5000/32258, 32258/5000, 10000/10003 ps |

FF2 는 FF1 을 한 목적지 주기 뒤에 잡으므로 **결정된 값을 본다고 가정**한다. 즉 이 모델의 MTBF 는 무한대다 — 이것이 모델의 가장 큰 한계이고, 실제 수치는 MTBF 식으로 따로 계산한다.

### Part A — 단일 비트와 2-FF 동기화기

- `sync2_clock(s, w, t, …)`: 목적지 에지에서 `q2 ← q1`, `q1 ← sample(wire, t)` 를 **동시에**. 레벨 신호(소스에서 ≥ 2Td 유지)를 2만 번 토글해 하나도 안 잃는지, 지연이 `[Td + tcq − th, 2Td + tcq + tsu)` 안인지, 창 적중률(`meta/전이`)이 `T_W/Td` 의 ±25 % 안인지 CHECK.
- 손으로 위상을 맞춘 한 장면(`./so trace`): 1000 ps 쓰기 에지, 1250 ps 읽기 에지 → 창 적중 → 새 값/옛 값으로 결정될 때 지연이 1.02 Tr / 2.02 Tr.
- 거의 같은 주파수(100 vs 99.97 MHz)에서 창 적중이 **연속으로 몰려 오는** 것(max run ≥ 5)을 CHECK.

### Part B — 비동기 FIFO: 이진 포인터 vs Gray 포인터

- `bin2gray`, `gray2bin` 전수 검사(0..65535 왕복, 폭 1..16 에서 wrap 포함 이웃 1 비트 차).
- `gray_full`, `gray_empty` 를 이진 정의 `(w − r) mod 2^(N+1) == 2^N`, `w == r` 와 N = 1..7 전수 비교.
- 깊이 8 FIFO, 4 클럭 × {이진, Gray} × {B = 1: 플래그로 한 칸씩, B = 4: 동기화된 포인터로 **개수(level/free)를 계산해 최대 4 칸 버스트**}. 스코어보드: 소스는 0,1,2,… 순번을 쓰고, 싱크는 `ok / dup(옛 값) / lost(건너뜀) / corrupt(쓴 적 없는 값)` 로 분류. 전지적 시점(진짜 포인터)으로 overflow/underflow, 동기화된 포인터가 "있을 수 없는 값"인 횟수, 플래그가 위험한 쪽으로 틀린 횟수(`!F&TF`: full 아닌데 진짜 full), 비관적인 비율(`F_pess`: full 인데 진짜는 아님)을 센다.
- Gray 장기 랜덤: N ∈ {1,2,3,5} × B ∈ {1,4} × 클럭 4 × 시드 3 = 96 회 → 오류 0, 위험 플래그 0, 데이터 경로 메타 0, 모두 전달 + 커버리지(창 적중·진짜 full·진짜 empty 가 실제로 일어남).

### Part C — 다른 CDC 패턴

- 펄스: 소스 1 주기 펄스를 (a) q2 그대로, (b) q2 상승 에지, (c) 토글 동기화기(`toggle_src` / `toggle_dst`)로 넘겨 개수 비교. 간격 규칙 `S ≥ ⌈(Td + T_W)/Ts⌉` 을 지키면 토글은 0 유실, 어기면 토글 둘이 서로 지워 유실.
- 16-bit 버스: "비트마다 2-FF" vs 4-phase req/ack (`hs_src_step` / `hs_dst_step`). 섞인 단어 수, 데이터를 잡는 순간의 메타 0, 단어당 ns.
- 리셋 동기화기는 개념만 (풀이) → RTLDV-B06. HDL 버전은 RTLDV-M05/M06, 정적 CDC 검사는 RTLDV-M07.

## 2. Problem (English)

> **Clock-domain crossing in a discrete-event model.** Simulate two free-running clocks in picoseconds (period, phase, optional jitter). A signal launched by a flop in domain A changes at `t_edge + tcq (+ per-bit skew)`. A flop in domain B sampling at edge `t` gets the new value if the change happened at or before `t − tsu`, the old value if it happens after `t + th`, and a *random* value per bit otherwise (metastability model, fixed-seed xorshift32). The second flop of a synchronizer is assumed to see a resolved value.
>
> A. Implement `void sync2_clock(sync2_t *s, const wire_t *w, int64_t t, ...)` (2-FF synchronizer). Pass a level signal for 100→77, 200→31, 31→200 and 100→99.97 MHz. Show: no transition lost, latency in `[Td + tcq − th, 2Td + tcq + tsu)`, window-hit fraction ≈ `(tsu + th)/Td`. Relate it to `MTBF = e^(t_r/τ) / (T_W · f_clk · f_data)` and explain why 2 or 3 stages.
>
> B. Implement `bin2gray`, `gray2bin`, `bool gray_full(uint32_t wg, uint32_t rq2, int N)`, `bool gray_empty(uint32_t rg, uint32_t wq2)` for a depth-2^N asynchronous FIFO with (N+1)-bit pointers. Verify full/empty exhaustively against `(w − r) mod 2^(N+1) == 2^N` and `w == r`. With a scoreboard, show what goes wrong when binary pointers are synchronized bit by bit, and that Gray pointers + 2-FF are clean over long random runs. Show that the flags are conservative but never wrong.
>
> C. Implement a toggle pulse synchronizer and a 4-phase req/ack handshake for a 16-bit bus. Show why synchronizing each bit of a bus with its own 2-FF is wrong.
>
> Follow-ups: Can the Gray pointer be generated by combinational logic right before the synchronizer? Why must the depth be a power of two? How deep must the FIFO be to sustain full throughput? How would you read a 64-bit counter that runs on another clock from firmware?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
```

TODO 는 작은 함수 9 개다: `sync2_clock`, `bin2gray`, `gray2bin`, `gray_full`, `gray_empty`, `toggle_src`, `toggle_dst`, `hs_src_step`, `hs_dst_step`. 시뮬레이션 커널(클럭·와이어·메타안정 샘플러), FIFO 모델과 스코어보드, 펄스·버스 실험은 starter 와 solution 이 같다. 처음 실행 결과 (실제 출력 일부, exit 1):

```text
  100->77 MHz     20000      0      0   0.0000   0.0193      0  0.00 / 0.00 / 0.00          0/0
FAIL starter.c:250  r.recv == r.sent && r.extra == 0
FAIL starter.c:253  rate > 0.75 * expect && rate < 1.25 * expect
...
  100->77 MHz  gray  1  121944  7817  9162 59332 37546    45 187082     0 16024 12478   0.0%   0.0%
...
  runs=96 writes=1023460 | bad runs=48 unsafe flags=4531167 impossible ptr=2802109 data-meta=2854 undelivered=96
...
12/41 passed
```

스텁 동기화기는 아무것도 잡지 않으니 목적지는 영원히 0 을 보고(`recv 0`), 스텁 `gray_empty` 가 늘 "안 비었다"를 돌려주니 Gray FIFO 가 underflow 9162 번, overflow 7817 번을 낸다. 통과하는 12 개는 하네스 자체의 검사(예: 스텁 identity 변환도 왕복은 성립)와 "이진 포인터" 행처럼 TODO 를 안 쓰는 경로다.

## 4. 힌트

<details><summary>힌트 1 — 동기화기는 "시프트 레지스터"다</summary>

같은 에지에서 두 플롭이 동시에 갱신된다. C 로는 **뒤 플롭부터** 대입하면 된다(q2 가 에지 직전의 q1 을 받도록). 순서를 바꾸면 q2 가 이번 에지에 막 잡은 값을 받아 1-FF 와 같아진다 — ARCH-B06 의 "제자리 갱신 버그" 와 같은 모양이다.
</details>

<details><summary>힌트 2 — Gray 변환</summary>

`g = b ^ (b >> 1)`. 거꾸로는 비트 i 가 Gray 의 비트 i 와 그 위 모든 비트의 XOR — 위에서 아래로 누적 XOR (prefix XOR). 루프로 `b ^= g >> k` 를 k = 1, 2, … 하거나, `b ^= b >> 1; b ^= b >> 2; b ^= b >> 4; …` 로 log 단계에.
</details>

<details><summary>힌트 3 — full 은 "w = r + 2^N"</summary>

이진에서 2^N 을 더하면 (N+1)비트 포인터의 **최상위 비트 하나만** 뒤집힌다. 그때 `g = b ^ (b >> 1)` 의 어느 비트들이 바뀌나? 최상위 Gray 비트(= b_N)와 바로 아래 Gray 비트(= b_N ^ b_{N−1})뿐이다. 나머지는 그대로.
</details>

<details><summary>힌트 4 — 토글 동기화기</summary>

펄스는 짧아서 느린 도메인이 못 볼 수 있다. 펄스마다 레벨을 **뒤집으면** 그 레벨은 다음 펄스까지 유지된다. 목적지는 동기화된 레벨이 바뀌었는지(q2 와 한 주기 전 값 q3 의 XOR)만 보면 된다.
</details>

<details><summary>힌트 5 — 4-phase 핸드셰이크</summary>

소스: data 를 먼저 싣고(같은 에지에서 req=1 도 됨 — 데이터는 req 가 2-FF 를 건너는 동안 이미 안정), ack 가 보이면 req=0, ack 가 내려가면 다음 단어. 목적지: req 가 보이고 아직 ack 안 했으면 버스를 **그 에지에 직접** 잡고 ack=1, req 가 내려가면 ack=0. 데이터 버스에는 동기화기가 필요 없다 — 왜?
</details>

## 5. 풀이

### 0. 메타안정성 "모델" 이 정확히 무엇인가

실제 플롭은 창 안에서 입력이 바뀌면 출력이 중간 전압에 머물다가 **확률적인 시간**(시상수 τ 의 지수 분포) 뒤에 0 이나 1 로 떨어진다. 이 시뮬레이터는 그것을 두 가지로 단순화한다:

1. 창 안이면 **샘플 순간에 바로** 0/1 중 하나로 결정 (비트마다 독립, xorshift32). → 어느 쪽으로 결정되는지의 무작위성은 표현하지만, "결정에 걸리는 시간"은 표현하지 않는다.
2. FF2 는 항상 결정된 FF1 을 본다. → 동기화기 자체의 실패(MTBF)는 0. 실제 실패율은 MTBF 식으로 따로 계산한다(아래, 연습 2).

이 모델이 **못 보여 주는 것**: 중간 전압이 팬아웃마다 다르게 해석되는 문제(1-FF 의 진짜 위험), 동기화기 앞 조합 논리의 글리치(RTLDV-M07 이 넷리스트에서 찾는다), 서로 따로 동기화된 신호의 재수렴(reconvergence). **보여 주는 것**: 어느 클럭 위상에서 창에 걸리는가, 여러 비트가 서로 다르게 결정되는 결과, 그 결과가 프로토콜(FIFO, 핸드셰이크)을 통과해 데이터 오류가 되는가.

와이어 샘플러 (하네스, 비트마다 세 경우):

```c
int64_t tcb = w->tc + w->skew[b];
if (tcb <= t - TM.tsu) v |= w->cur & m;              /* setup 전에 이미 바뀜 → 새 값 */
else if (tcb > t + TM.th) v |= w->old & m;            /* hold 끝난 뒤에 바뀜 → 옛 값 */
else { meta = true; ...; if (xs32(rs) & 1u) v |= m; } /* 창 안 → 무작위 결정 */
```

시간 순서대로 처리하는 사건 시뮬레이터에서 "미래에 바뀔 값"(hold 위반)을 어떻게 아나? 소스 에지 t 의 변화는 t + tcq 에 나타나고 `tcq (200) > th (100)` 이므로, 목적지 에지 t' 의 hold 창 `(t', t' + th]` 에 들어올 변화는 반드시 t' **이전** 소스 에지에서 이미 예약돼 있다. 그래서 와이어는 `{old, cur, tc}` 만 들고 있으면 된다(같은 시각 에지는 쓰기 도메인 먼저 처리해도, 반대로 해도 결과가 같다). 설정 검사 `T_src > tcq + skew_max + tsu + 2J` 는 직전 변화가 다음 소스 에지 전에 끝난다는 보장이다.

### Part A — 2-FF 동기화기: 지연, 창 적중률, MTBF 의 분모

**정답 코드** (뒤 플롭부터):

```c
s->q2 = s->q1;
s->q1 = wire_sample(w, t, rs, st);
```

**한 장면** — `./so trace` 실제 출력 (쓰기 100 MHz 위상 1000 ps, 읽기 77 MHz 위상 1250 ps):

```text
resolves to new:
  t=  1000 ps  W edge : d 0->1 (wire changes at 1200 ps)
  t=  1250 ps  R edge : q1=1 q2=0  <- META (change inside (t-tsu, t+th]) resolved randomly
  t= 14237 ps  R edge : q1=1 q2=1  -> out=1, latency 13237 ps = 1.02 Tr
resolves to old:
  t=  1000 ps  W edge : d 0->1 (wire changes at 1200 ps)
  t=  1250 ps  R edge : q1=0 q2=0  <- META (change inside (t-tsu, t+th]) resolved randomly
  t= 14237 ps  R edge : q1=1 q2=0
  t= 27224 ps  R edge : q1=1 q2=1  -> out=1, latency 26224 ps = 2.02 Tr
```

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">위: 900–1500 ps 확대 (1 ps ≈ 1 px)  ·  아래: 0–30000 ps (위 trace 의 실제 시각)</text>
  <rect x="287" y="30" width="207" height="104" class="f-warn" stroke="currentColor" stroke-dasharray="3 2"/>
  <g fill="currentColor">
    <text x="10" y="56">wclk</text><text x="10" y="86">d (wire)</text><text x="10" y="116">rclk</text>
    <text x="300" y="46">창 (1100, 1350]</text>
    <text x="160" y="46">1000</text><text x="372" y="70">1200</text><text x="430" y="130">1250</text>
    <text x="300" y="148">← tsu 150 →</text><text x="445" y="148">th 100 →</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5" fill="none">
    <path d="M80 62 L183 62 L183 48 L700 48"/>
    <path d="M80 92 L390 92 L390 78 L700 78"/>
    <path d="M80 122 L442 122 L442 108 L700 108"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="186">rclk</text><text x="10" y="216">q1 (new)</text><text x="10" y="242">q2 (new)</text>
    <text x="10" y="272">q1 (old)</text><text x="10" y="298">q2 (old)</text>
    <text x="96" y="176">1250</text><text x="366" y="176">14237</text><text x="640" y="176">27224</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5" fill="none">
    <path d="M80 192 L107 192 L107 180 L113 180 L113 192 L384 192 L384 180 L390 180 L390 192 L661 192 L661 180 L667 180 L667 192 L720 192"/>
    <path d="M80 222 L107 222 L107 208 L720 208"/>
    <path d="M80 248 L384 248 L384 234 L720 234"/>
    <path d="M80 278 L384 278 L384 264 L720 264"/>
    <path d="M80 304 L661 304 L661 290 L720 290"/>
  </g>
  <rect x="104" y="204" width="8" height="22" class="f-warn"/>
  <rect x="104" y="260" width="8" height="22" class="f-warn"/>
  <g fill="currentColor">
    <text x="400" y="230">out=1, 13237 ps = 1.02 Tr</text>
    <text x="500" y="286">26224 ps = 2.02 Tr</text>
    <text x="118" y="270">META → 0</text><text x="118" y="218">META → 1</text>
    <text x="80" y="324">창에 걸린 FF1 이 옛 값으로 결정되면 한 주기 더 늦을 뿐, 값이 틀리지는 않는다 (단일 비트일 때).</text>
  </g>
</svg>
```

**지연 범위 유도.** 소스 에지 ts, 변화 tc = ts + tcq. 새 값을 잡을 수 있는 첫 목적지 에지 e1 은 `e1 ≥ tc − th` (hold 창 안의 에지는 "변화 직전" 이지만 새 값으로 결정될 수 있다!) 이고 `e1 < tc − th + Td`. q2 는 e2 = e1 + Td 에 바뀌고, e1 이 창 안이었고 옛 값으로 결정됐다면 e3 = e1 + 2Td. e3 경우는 e1 < tc + tsu 일 때만이므로:

`Td + tcq − th  ≤  지연  <  2Td + tcq + tsu`  → 이 모델에서 1.01 Td ~ 2.03 Td (77 MHz 기준).

**4 클럭 쌍, 각 2만 번 토글** (실제 출력):

```text
  clocks           sent   recv   meta  meta/tx   T_W/Td maxrun  latency min/avg/max (Td) hist[1Td,2Td)
  100->77 MHz     20000  20000    391   0.0196   0.0193      1  1.01 / 1.52 / 2.03      19626/374
  [MTBF 분모] 측정 meta/s = 3.563e+05, f_clk*f_data*T_W = 3.508e+05 (f_data = 1.822e+07 /s)
  200->31 MHz     20000  20000    171   0.0086   0.0078      1  1.00 / 1.51 / 2.01      19851/149
  31->200 MHz     20000  20000   1008   0.0504   0.0500      1  1.02 / 1.54 / 2.07      19118/882
  100->99.97      20000  20000    499   0.0249   0.0250     17  1.01 / 1.52 / 2.03      19566/434
```

| 관찰 | 설명 |
|---|---|
| `recv == sent` | 레벨을 ≥ 2Td 유지하면 레벨 동기화는 하나도 안 잃는다. 창에 걸려도 "늦게" 보일 뿐 |
| 평균 지연 ≈ 1.5 Td | e1 이 [0, Td) 에 균등하게 놓이니 평균 Td/2 + 한 주기. `hist` 의 ≥ 2Td 는 대부분 "창에 걸려 옛 값으로 결정"(약 meta/2) + e1 이 경계 근처였던 경우 |
| `meta/tx ≈ T_W/Td` | 비율이 유리수와 멀면 변화 시각이 목적지 격자에 대해 균등 분포 → 창(250 ps)에 에지가 걸릴 확률 = 250/Td. 31→200 은 Td 가 짧아 5 % |
| `maxrun 17` (100 vs 99.97) | 위상이 소스 주기당 3 ps 씩만 미끄러진다 → 창에 들어가면 수십 번 연속 적중. **평균 비율은 같지만 몰려서 온다.** 실리콘에서 "같은 주파수인데 다른 크리스털" 인 두 도메인이 가장 까다로운 이유 |

**MTBF 와의 연결.** 측정한 "초당 창 적중 수" 3.56e5/s 가 `f_clk · f_data · T_W` = 77e6 × 1.82e7 × 250e-12 = 3.51e5/s 와 같다. 이게 MTBF 식의 **분모**다:

`MTBF = e^(t_r/τ) / (T_W · f_clk · f_data)`  (Ginosar 2011 등 표준 형태)

창 적중 하나하나가 "FF1 이 준안정에 빠질 기회" 이고, 그 기회가 FF2 의 샘플 순간까지 결정되지 않을 확률이 `e^(−t_r/τ)` 다. 2-FF 의 결정 시간은 `t_r = Td − tcq − tsu` (FF1 출력이 FF2 의 setup 전까지 결정돼야), 단을 하나 늘리면 t_r 이 한 주기 더 → **지수에** 들어가므로 한 단이 e^(Td/τ) 배를 준다. 이 시뮬레이터는 분모를 재고, 분자는 "충분히 크다" 고 가정한 것이다. 대표값(τ = 20 ps, T_W = 250 ps)으로 계산하면 77 MHz 2-FF 는 사실상 무한, 1 GHz 에서는 2-FF ≈ 5.2e6 s(약 60 일)로 불합격, 3-FF 면 6.8e20 s (연습 2). τ·T_W 는 공정·셀 특성값이라 라이브러리 데이터를 써야 하고, 동기화기 플롭 사이에 논리를 넣으면 t_r 이 줄어든다. B13 연습 5 와 같은 결론 — **2 단이 기본, 클럭이 빠르거나 f_data 가 높거나 제품 수명·대수가 크면 3 단**.

**왜 1-FF 로는 안 되나.** 모델에서 1-FF 의 값도 0/1 이라 멀쩡해 보인다 — 바로 이 모델의 한계다. 실제로는 결정 안 된 FF1 출력이 같은 주기 안에 여러 논리로 퍼져, 한 팬아웃은 0, 다른 팬아웃은 1 로 읽는다(상태 기계가 불가능한 상태로). FF2 는 FF1 에 **한 주기 통째의 결정 시간**을 주고 팬아웃을 하나로 만든다. FPGA 툴은 동기화기 플롭을 가까이 붙여 배치하도록 속성(예: Xilinx `ASYNC_REG`)을 둔다 — 결정 시간을 배선 지연으로 깎아 먹지 않으려는 것이다.

### Part B — 비동기 FIFO

**Gray 변환.**

```c
static uint32_t bin2gray(uint32_t b) { return b ^ (b >> 1); }
static uint32_t gray2bin(uint32_t g) { uint32_t b = g; for (uint32_t s = g >> 1; s; s >>= 1) b ^= s; return b; }
```

Gray 의 핵심 성질 두 개: (1) 이웃한 수(그리고 2^k 에서 0 으로 감쌀 때도)의 코드가 정확히 1 비트 다르다 → 폭 1..16 전체 131 070 쌍 CHECK. (2) 일대일 → 같음 비교는 Gray 끼리 해도 된다. 산술(뺄셈)은 안 되므로 개수(level)가 필요하면 `gray2bin` 을 거친다.

**왜 (N+1) 비트 포인터인가.** 깊이 2^N 이면 칸 번호는 N 비트로 충분하지만, w 와 r 의 아래 N 비트가 같은 상태가 두 개다 — 비었음(같은 바퀴)과 가득 참(w 가 한 바퀴 앞섬). 한 비트 더 = "바퀴 수의 홀짝" 으로 둘을 가른다.

**full 공식 유도.** 이진으로 full ⇔ `w = r + 2^N (mod 2^(N+1))` ⇔ **최상위 비트만 다르고 나머지 같다**. Gray `g_i = b_i ^ b_{i+1}` (최상위는 `g_N = b_N`) 에서 b_N 하나만 뒤집으면 바뀌는 Gray 비트는 `g_N` 과 `g_{N−1} = b_N ^ b_{N−1}` 둘뿐. 따라서

`full ⇔ wg == rq2 ^ (0b11 << (N−1))`  즉 `{~rq2[N:N−1], rq2[N−2:0]}` (Cummings 2002 의 식과 같다),  `empty ⇔ rg == wq2`.

```c
static bool gray_full(uint32_t wg, uint32_t rq2, int N) { return wg == (rq2 ^ (3u << (N - 1))); }
static bool gray_empty(uint32_t rg, uint32_t wq2) { return rg == wq2; }
```

N = 1..7 의 모든 (w, r) 87 376 쌍에서 이진 정의와 같다 (CHECK). "MSB 만 비교" 처럼 Gray 에 이진 규칙을 그대로 쓰면 틀린다 — 두 번째 비트까지 뒤집어야 한다 (7. 실수 3).

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 번의 증가가 창에 걸렸을 때 FF1 이 잡을 수 있는 값 (4-bit 포인터, 깊이 8)</text>
  <g stroke="currentColor">
    <rect x="10" y="34" width="350" height="150" rx="6" class="f-warn"/>
    <rect x="380" y="34" width="350" height="150" rx="6" class="f-ok"/>
    <rect x="10" y="200" width="720" height="70" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="22" y="56" font-weight="bold">이진 7 → 8 : 0111 → 1000 (4 비트 변화)</text>
    <text x="22" y="80">비트마다 옛/새 중 무작위 → 2^4 = 16 가지 전부 가능</text>
    <text x="22" y="102" font-family="monospace">0000 0001 … 1111 = 0 .. 15</text>
    <text x="22" y="126">예: 1111 = 15 → 읽기 쪽 level = 15 − r</text>
    <text x="22" y="148">skew 가 있으면 창 밖에서도 섞인다</text>
    <text x="22" y="172">실측(B=1, 깊이 8): 있을 수 없는 값 426 번</text>
    <text x="392" y="56" font-weight="bold">Gray 7 → 8 : 0100 → 1100 (1 비트 변화)</text>
    <text x="392" y="80">바뀌는 비트가 하나 → 옛 값 7 또는 새 값 8</text>
    <text x="392" y="102" font-family="monospace">0100 (=7) | 1100 (=8)</text>
    <text x="392" y="126">어느 쪽이든 "언젠가 실제로 가졌던 값"</text>
    <text x="392" y="148">→ 동기화된 포인터는 늦을 수는 있어도</text>
    <text x="392" y="172">앞서거나 엉뚱할 수 없다 (실측 0 번)</text>
    <text x="22" y="222" font-weight="bold">full: w = r + 8 ⇒ 이진은 MSB 만 다름 ⇒ Gray 는 위 두 비트가 다름</text>
    <text x="22" y="244" font-family="monospace">r = 3: bin 0011  gray 0010     w = 11: bin 1011  gray 1110 = 0010 ^ 1100</text>
    <text x="22" y="262" font-family="monospace">full = (wg == rq2 ^ 0b1100)    empty = (rg == wq2)</text>
  </g>
</svg>
```

**FIFO 실험** — 깊이 8, 400k 에지, 4000 에지마다 "채우기 압력(쓰기 0.9 · 읽기 0.5 × 느린 쪽 속도)" ↔ "비우기 압력" 교대 (실제 출력):

```text
  clocks       ptr   B  writes   ovf   unf   dup  lost  corr    imp  meta !F&TF !E&TE F_pess E_pess
  100->77 MHz  bin   1   87838     0     0     0     0     0    426  4313     0     0  66.7%  72.3%
  100->77 MHz  bin   4  144907   198   100   163  1059     0    576  7069   172   123  74.5%  74.1%
  100->77 MHz  gray  1   87370     0     0     0     0     0      0  3986     0     0  65.6%  72.7%
  100->77 MHz  gray  4  143704     0     0     0     0     0      0  6352     0     0  73.6%  72.2%
  200->31 MHz  bin   1   27241     0     0     0     0     0    201  1691     0     0  19.8%  65.8%
  200->31 MHz  bin   4   46832    70    25   164   916     0    353  2911    42    25  40.1%  68.3%
  200->31 MHz  gray  1   26904     0     0     0     0     0      0  1538     0     0  19.7%  65.3%
  200->31 MHz  gray  4   46068     0     0     0     0     0      0  2588     0     0  40.4%  64.4%
  31->200 MHz  bin   1   26893     0     0     0     0     0    203  1732     0     0  64.9%  19.5%
  31->200 MHz  bin   4   45924    32    69   745    57     0    386  2938    30    47  66.5%  39.7%
  31->200 MHz  gray  1   26968     0     0     0     0     0      0  1611     0     0  64.8%  19.8%
  31->200 MHz  gray  4   45954     0     0     0     0     0      0  2615     0     0  66.9%  39.9%
  100->99.97   bin   1   99895     0     0     0     0     0    464  5498     0     0  75.3%  75.3%
  100->99.97   bin   4  158161   246   244   395   500     9    610  8533   259   260  75.1%  76.6%
  100->99.97   gray  1  100047     0     0     0     0     0      0  4990     0     0  75.3%  75.7%
  100->99.97   gray  4  157506     0     0     0     0     0      0  7779     0     0  75.6%  75.0%
```

(`imp` = 동기화된 포인터가 진짜 포인터보다 앞서거나 DEPTH 넘게 뒤처진 "있을 수 없는 값" 을 본 에지 수, `meta` = 포인터 샘플이 창에 걸린 횟수, `!F&TF` = full 플래그가 꺼져 있는데 진짜 full, `F_pess` = full 플래그가 켜진 에지 중 진짜로는 full 이 아닌 비율.)

이 표가 레슨의 핵심이다. 세 줄로 읽는다:

1. **이진 포인터의 동기화 값은 엉뚱하다** (`imp` 201~610, Gray 는 모든 줄에서 0). 4 클럭 쌍 모두에서.
2. **그런데 B = 1(플래그로 한 칸씩)이면 데이터 오류가 0 이다.** 운이 아니라 구조 때문이다 — 아래 증명. 면접에서 "이진 포인터면 FIFO 가 무조건 깨진다" 고만 답하면 staff 레벨 꼬리질문("정말? 어떤 경우에?")에 막힌다.
3. **B = 4(동기화된 포인터로 개수를 계산해 그만큼 연속 읽기/쓰기)면 이진은 모든 클럭 쌍에서 깨진다**: overflow 32~246, underflow 25~244, 그 뒤 dup/lost 가 연쇄. Gray 는 같은 조건에서 0.

**왜 B = 1 이진은 살아남나 (증명).** 읽기 쪽을 보자. 쓰레기 값은 쓰기 포인터가 p → p+1 로 바뀌는 순간을 FF1 이 창 안에서 잡을 때만 생기고, 그 값은 p 와 p+1 의 비트별 혼합이며, q2 에 딱 한 주기 머문다(다음 에지엔 q1 이 깨끗한 값을 잡아 놓았다). 그 쓰레기를 쓰는 에지 시점에 리더는 아직 p+1 을 본 적이 없으므로 `r ≤ p`, 반면 실제 `w ≥ p+1` → **칸이 최소 하나는 있다**. B = 1 이면 그 에지에 기껏해야 한 칸 읽으니 안전하다. 쓰기 쪽도 대칭(리더가 막 한 칸 비웠으므로 빈칸이 최소 하나). 즉 "포인터가 움직이는 중" 이라는 사실 자체가 한 칸의 여유를 보장한다. 그래서 `!F&TF`, `!E&TE` 도 0 이다.

**B = 4 에서 깨지는 이유.** 쓰레기 `wq2 = 1111` 이고 `r = 7` 이면 `level = 15 − 7 = 8`, 리더는 4 칸 버스트를 시작한다 — 실제로는 1 칸뿐. 증명의 "한 칸 여유" 가 "네 칸" 을 감당하지 못한다. 실제 설계에서 이 상황은 흔하다: almost_full / almost_empty 임계값, DMA 의 버스트 요청(ARCH-M11 의 DREQ 를 "≥ 버스트 크기" 로 정할 때), 펌웨어가 읽는 FIFO level 레지스터(일부 UART IP 가 제공)로 "그만큼 읽기". **포인터를 개수로 쓰는 순간 이진 포인터는 틀린다.** 게다가 실제 실리콘에서는 FF2 까지 준안정이 번지는 드문 경우, skew 가 창보다 큰 경우, 정적 CDC 검사기가 "Gray/핸드셰이크 없는 다비트 교차" 로 무조건 위반 처리하는 문제까지 있다 — B = 1 의 생존은 설계 규칙으로 기대면 안 되는 성질이다.

**Gray 가 "보수적이지만 틀리지 않는" 이유.** Gray 포인터는 한 번에 1 비트만 바뀌므로 동기화된 값은 항상 소스 포인터가 **실제로 가졌던 값**(옛 값 또는 새 값)이다. 포인터는 증가만 하므로 `rq2 ≤ r(진짜)`, `wq2 ≤ w(진짜)` (바퀴를 펼친 의미에서). 그러면

- 쓰기 쪽 `used = w − rq2 ≥ w − r` → full 은 진짜보다 **일찍** 켜지고 **늦게** 꺼진다 (`F_pess` 20~76 %), 진짜 full 인데 꺼져 있는 일은 없다 (`!F&TF` = 0).
- 읽기 쪽 `level = wq2 − r ≤ w − r` → empty 도 같은 방향. 그래서 Gray + level 버스트(B = 4)도 안전하다.

`F_pess` 가 66 % 나 되는 건 "채우기 압력" 구간에서 FIFO 가 full 경계에 붙어 있고, 리더가 한 칸 비울 때마다 그 소식이 2~3 wclk 늦게 도착하기 때문이다. 틀린 게 아니라 **처리량 비용**이다 — 연습 5 에서 깊이별로 잰다:

```svg
<svg viewBox="0 0 740 220" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Gray FIFO 깊이 vs 처리량 (100 vs 99.97 MHz, 양쪽 매 에지 시도, practice.c 연습 5 실측)</text>
  <g stroke="currentColor">
    <path d="M80 180 L700 180" />
    <path d="M80 180 L80 34" />
    <path d="M80 40 L700 40" stroke-dasharray="3 3"/>
    <rect x="110" y="124" width="70" height="56" class="f-warn"/>
    <rect x="230" y="69" width="70" height="111" class="f-warn"/>
    <rect x="350" y="40" width="70" height="140" class="f-ok"/>
    <rect x="470" y="40" width="70" height="140" class="f-ok"/>
    <rect x="590" y="40" width="70" height="140" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="40" y="44">1.0</text><text x="40" y="184">0</text>
    <text x="120" y="118">0.396</text><text x="240" y="63">0.791</text>
    <text x="360" y="56">1.000</text><text x="480" y="56">1.000</text><text x="600" y="56">1.000</text>
    <text x="125" y="198">깊이 2</text><text x="245" y="198">4</text><text x="365" y="198">8</text><text x="485" y="198">16</text><text x="605" y="198">32</text>
    <text x="80" y="214">읽기 에지당 읽은 수 ≈ min(1, 깊이 / 왕복 지연). 이 모델의 왕복 ≈ 5 주기 → 2/5 = 0.4, 4/5 = 0.8</text>
  </g>
</svg>
```

**데이터 경로는 왜 동기화기가 없나.** 메모리 칸도 와이어로 모델링해 읽을 때 `wire_sample` 로 잡았다. Gray FIFO 96 회 스트레스에서 데이터 샘플의 창 적중은 **0** (`data-meta=0`). 이유: 칸을 쓰고 → 포인터가 바뀌고 → 그 포인터가 2-FF 를 건너 → 그 다음 에지에야 리더가 읽는다. 데이터는 이미 최소 한 목적지 주기 넘게 안정돼 있다. 포인터 동기화가 데이터의 "유효" 신호 역할을 하는 것 — Part C 의 핸드셰이크와 같은 원리다. 반대로 이진 B = 4 의 underflow 는 막 쓰이는 칸을 읽을 수 있어 `corr 9` (100 vs 99.97) 같은 쓴 적 없는 값이 나온다.

**장기 스트레스** (실제 출력):

```text
  runs=96 writes=1589325 | bad runs=0 unsafe flags=0 impossible ptr=0 data-meta=0 undelivered=0
  coverage: ptr meta events=78391 true-full edges=767280 true-empty edges=674344 | pessimistic full=693431 empty=618523
```

"위반 0" 은 커버리지와 함께일 때만 의미가 있다(ARCH-A04 와 같은 원칙): 포인터 샘플이 창에 걸린 횟수 78 391, 진짜 full/empty 에지 수십만 — 위험한 순간을 실제로 지나갔다. 깊이 2(N = 1, top2 마스크 = 포인터 전체)도 포함된다.

### Part C — 펄스, 버스, 리셋

**펄스** (5000 개, 간격 = ⌈(Td + T_W)/Ts⌉ + 0..2, 실제 출력):

```text
  clocks       spacing     sent    level     edge   toggle
  100->77 MHz        2     5000     3823     3426     5000
  200->31 MHz        7     5000      778      670     5000
  31->200 MHz        1     5000    32266     3339     5000
  toggle, 200->31 MHz, spacing 2 src cycles (< Td): sent 5000 recv 788 (lost 4212)
```

- **빠름→느림**: 1 주기(5 ns) 펄스는 32 ns 주기의 목적지 에지 사이로 빠져나간다 → 상승 에지 670/5000. 100→77 처럼 비슷한 속도여도 10 ns 펄스를 13 ns 에지가 놓친다.
- **느림→빠름**: q2 를 그대로 펄스로 쓰면 한 펄스(32 ns)가 6.45 개로 불어난다(32266). 상승 에지 검출은 1 개로 세지만 간격 1 이면 연속 펄스가 하나의 긴 1 로 합쳐져 3339 개만 센다.
- **토글**: 펄스마다 레벨을 뒤집어 펄스를 "레벨 변화" 로 바꾼다. 목적지는 `q2 ^ q3`. 세 경우 모두 5000/5000.

```c
static uint32_t toggle_src(uint32_t tog, bool pulse) { return tog ^ (pulse ? 1u : 0u); }
static bool toggle_dst(uint32_t q2, uint32_t *q3) { bool p = ((q2 ^ *q3) & 1u) != 0; *q3 = q2; return p; }
```

**간격 규칙.** 토글 레벨 하나가 목적지에 한 번은 깨끗하게 잡혀야 한다: 레벨 유지 시간 `S·Ts` 에서 창 `T_W` 를 뺀 구간에 목적지 에지가 반드시 있으려면 `S·Ts − T_W ≥ Td` → `S ≥ ⌈(Td + T_W)/Ts⌉`. 어기면 토글 둘이 목적지가 보기 전에 서로 지운다 → 200→31 간격 2 에서 4212 개 유실. 연습 4 에서 시뮬레이션 최소 간격이 이 식과 **4 클럭 쌍 모두 같다**. 간격을 보장할 수 없으면 ack 를 돌려받는 핸드셰이크나, 소스 쪽 카운터 + Gray 동기화(= 작은 비동기 FIFO)로 개수를 넘긴다.

**버스** (16-bit, 5000 단어, 실제 출력):

```text
  clocks       scheme                  sent    recv corrupt  d-meta    ns/word
  100->77 MHz  2-FF per bit            5000    5242     242     245          -
               4-phase req/ack         5000    5000       0       0      129.0
  200->31 MHz  2-FF per bit            5000    5095      95     114          -
               4-phase req/ack         5000    5000       0       0      193.5
  31->200 MHz  2-FF per bit            5000    5646     646     757          -
               4-phase req/ack         5000    5000       0       0      225.8
  100->99.97   2-FF per bit            5000    5337     337     407          -
               4-phase req/ack         5000    5000       0       0      110.8
```

"비트마다 2-FF" 는 각 비트를 안전하게(준안정 확률을 낮게) 만들지만, **비트들끼리 같은 주기에 넘어온다는 보장이 없다**. 한 단어 변화가 창에 걸리면 일부 비트는 새 값, 일부는 옛 값 → 보낸 적 없는 단어가 한 주기 동안 보인다 (`recv − sent = corrupt`, 100→77 에서 242 개). 목적지는 그게 중간값인지 알 길이 없다.

**4-phase req/ack**: 데이터는 그대로 두고 **제어 신호 하나(req)만** 동기화한다. req 가 2-FF 를 건너는 동안 데이터는 이미 안정 → 목적지는 데이터를 동기화기 없이 직접 잡는다(`d-meta 0`). 대가는 왕복 두 번: req↑ 건너기 → ack↑ 건너기 → req↓ → ack↓, 각 2~3 주기 → 단어당 110~226 ns. 2-phase(토글 req/ack)는 왕복 한 번 → 연습 3 에서 2.0~2.3 배 빠르다. 이 "안정된 다비트 + 동기화된 한 비트 자격 신호" 패턴을 MCP(multi-cycle path) 공식이라고도 부른다. 처리량이 필요하면 비동기 FIFO 다.

**리셋 동기화기 (개념만 → RTLDV-B06).** 리셋 해제(deassert)도 비동기 신호다. 플롭마다 다른 주기에 리셋이 풀리면 상태 기계가 반쯤 깨어난 상태로 시작한다(recovery/removal 타이밍 위반 — 데이터 setup/hold 의 리셋판). 그래서 **assert 는 비동기**(클럭이 없어도 즉시 리셋), **deassert 는 2-FF 를 거쳐 동기**로 한다. 도메인마다 리셋 동기화기가 하나씩 필요하고, 그 클럭이 돌고 있어야 리셋이 풀린다 — 전원·클럭 시퀀싱(ARCH-A09)과 맞물리는 지점.

**다음 단계.** RTLDV-M05(2-FF·펄스·req/ack RTL + 메타 주입 TB), RTLDV-M06(비동기 FIFO RTL, cocotb, 이진 버그 버전이 깨지는 시드 찾기 — 이 레슨의 표가 "어떤 사용 방식에서 깨지는가" 의 답), RTLDV-M07(넷리스트에서 동기화기 없는 교차·동기화 전 조합 논리 찾기).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so          # 41/41 passed, 약 0.3 초
./so trace                                                              # Part A 장면 (파형 그림의 원본)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san   # 깨끗
```

(Apple M6, Apple clang 21.0.0. 단일 스레드 결정적 모델이라 타이밍 측정 없음, TSan 해당 없음. 측정 시점 `uptime` load average 60~115 — 공유 머신이지만 출력은 시뮬레이션 시간(ps)이라 영향 없고, 세 번 실행한 출력의 md5 가 같다.)

바뀐 곳은 TODO 9 개의 본문뿐이고 전부 1~10 줄이다. CDC 회로 자체는 작다 — 어려운 건 **어디에 무엇을 쓰느냐**와, 그것이 맞다는 것을 보이는 테스트벤치다(나머지 560 줄).

설계 메모:
- **사건 순서 고정**: 같은 시각 에지는 쓰기/소스 쪽 먼저. tcq > th 덕에 순서가 결과를 바꾸지 않지만, 결정성(같은 시드 → 같은 출력)을 위해 규칙을 정했다.
- **결정은 에지 직전 값으로, 동기화기는 결정 뒤에 클럭**: `full`/`empty` 를 `q2` 로 계산하고 쓰기/읽기를 한 다음 `sync2_clock`. RTL 의 "모든 플롭이 같은 에지에서 동시에 갱신" 을 순차 C 로 옮기는 방법(ARCH-B06 의 2-phase 갱신).
- **스코어보드는 순번 데이터**: 소스가 0,1,2,… 를 쓰면 싱크는 값 하나로 ok / dup(작음) / lost(큼) / corrupt(칸 번호와 안 맞거나 아직 안 쓴 값)를 가른다. 첫 오류 뒤의 숫자는 연쇄라 "오류가 있었다" 이상의 의미는 약하다.
- **전지적 시점 검사(ghost)**: 진짜 포인터로 overflow/underflow 와 "위험한 플래그" 를 따로 센다. 스코어보드만 보면 B = 1 이진의 `imp 426` 같은 **잠복 결함**을 못 본다.

## 7. 자주 하는 실수

1. **동기화기를 앞 플롭부터 갱신** — `s->q1 = sample(); s->q2 = s->q1;` 이면 q2 가 방금 잡은 (준안정일 수 있는) 값을 같은 에지에 받는다 = 1-FF. 지연 하한 CHECK(`≥ Td + tcq − th`)가 깨진다. 고친 코드: `s->q2 = s->q1; s->q1 = wire_sample(...);`
2. **Gray 포인터를 조합 논리로 만들어 바로 동기화기에 넣는다** — `wire = bin2gray(wbin_next)` 를 플롭 없이 내보내면, XOR 게이트 지연 차이로 1 비트 변화 중간에 다른 비트가 잠깐 튀는 글리치가 생겨 Gray 의 "1 비트만" 이 깨진다. 이 모델은 와이어를 항상 플롭 출력으로 두었기에 이 실수를 표현하지 못한다(RTLDV-M07 이 넷리스트에서 찾는다). 규칙: **소스 도메인에서 Gray 를 플롭에 담아 그 출력만 건넨다.**
3. **Gray full 을 MSB 만 비교** — 이진 규칙(`MSB 다르고 나머지 같음`)을 Gray 에 쓰면 N = 1..7 전수 검사에서 바로 틀린다. 위 **두** 비트를 뒤집어야 한다: `wg == (rq2 ^ (3u << (N - 1)))`.
4. **Gray 끼리 뺄셈으로 level 계산** — Gray 는 산술 코드가 아니다. `gray2bin(wq2) - rbin` 처럼 이진으로 돌린 뒤 빼라 (그래도 보수적이므로 안전).
5. **깊이를 2^N 이 아닌 값(예: 6)으로** — 0..5 를 그냥 Gray 로 세면 5 → 0 감쌀 때 `0111 → 0000` 3 비트 변화. 짝수 깊이면 "가운데 M 개 코드" 트릭, 홀수는 불가능 (연습 1).
6. **"비트마다 2-FF 니까 안전"** — 각 비트의 준안정은 줄지만 비트들 사이의 정합(coherency)은 보장 안 된다 → 섞인 단어 242 개 (Part C). 다비트는 Gray(카운터) / 핸드셰이크 / FIFO 셋 중 하나.
7. **펄스를 레벨 동기화기로** — 빠름→느림에서 유실(670/5000), 느림→빠름에서 증식(32266/5000). 토글로 바꾸고 간격 규칙을 지킨다.
8. **"시뮬레이션에서 통과했으니 CDC 문제 없음"** — 일반 RTL 시뮬레이터는 준안정을 모델링하지 않는다(창 안이어도 X 나 정해진 값). 이 레슨처럼 무작위 결정을 **주입**하거나(RTLDV-M05), 정적 CDC 검사(RTLDV-M07)를 돌려야 잡힌다.

## 8. 면접 Q&A

<details><summary>Q1. 비동기 FIFO 포인터를 왜 Gray 코드로 넘기나?</summary>

포인터는 비트마다 2-FF 로 동기화되는데, 여러 비트가 동시에 바뀌는 순간을 목적지가 잡으면 비트마다 옛/새 값이 섞여 **소스가 가진 적 없는 값**이 될 수 있다(이진 0111→1000 이면 16 가지 전부). Gray 는 증가할 때 1 비트만 바뀌므로 동기화된 값은 반드시 옛 값 또는 새 값 — 즉 늦을 수는 있어도 틀릴 수는 없다. 이 레슨 실측: 이진은 "있을 수 없는 값" 201~610 번, Gray 0 번.

꼬리질문: "그럼 Gray 면 동기화기가 필요 없나?" → 아니다. 바뀌는 그 1 비트도 준안정이 될 수 있으므로 2-FF 는 여전히 필요하다. Gray 는 "어느 쪽으로 결정되든 괜찮게" 만들고, 2-FF 는 "결정될 시간을 주는" 역할 — 둘은 다른 문제를 푼다. 또 Gray 값은 소스 도메인 **플롭 출력**이어야 한다(조합 논리 글리치, 실수 2).
</details>

<details><summary>Q2. 2-FF 면 충분한가? 3 단은 언제 쓰나?</summary>

`MTBF = e^(t_r/τ) / (T_W · f_clk · f_data)`. 단을 하나 더하면 t_r 이 대략 한 주기(`T − tcq − tsu`) 늘고 그것이 지수에 들어간다. 대표값 τ = 20 ps 로 77 MHz 2-FF 는 사실상 무한, 1 GHz 2-FF 는 약 60 일(불합격) → 3-FF 6.8e20 s, 2 GHz 는 7 단이 필요(연습 2, 대표값 기준). 판단 요소: 목적지 클럭(빠를수록 t_r 이 짧음), 데이터 변화율, 제품 대수 × 수명(백만 대 × 10 년이면 개당 MTBF 가 훨씬 커야 함), 공정의 τ. 이 시뮬레이터에서 측정한 초당 창 적중 3.56e5/s 가 분모 3.51e5/s 와 일치한다 — 분모는 "준안정 기회의 수" 다.

꼬리질문: "동기화기 두 플롭 사이에 인버터 하나 넣으면?" → t_r 에서 그 지연만큼 빠진다(지수에서 빠지므로 생각보다 크다). 동기화기 사이엔 아무 논리도 두지 않고, 배치도 붙인다(FPGA 의 `ASYNC_REG` 류 속성).
</details>

<details><summary>Q3. full/empty 조건을 유도하라. 포인터는 왜 N+1 비트인가?</summary>

N 비트 칸 번호만 쓰면 w == r 이 "비었음" 과 "가득 참" 둘 다를 뜻한다. 한 비트 더 = 바퀴 수 홀짝. 이진으로 empty ⇔ w == r, full ⇔ w − r ≡ 2^N ⇔ MSB 만 다름. Gray `g_i = b_i ^ b_{i+1}` 에서 MSB(b_N) 하나를 뒤집으면 `g_N` 과 `g_{N−1}` 만 바뀐다 → `full ⇔ wg == {~rq2[N:N−1], rq2[N−2:0]}`. Gray 는 일대일이므로 empty 는 그냥 같음 비교. N = 1..7 전수 87 376 쌍 CHECK.

꼬리질문: "almost_full (빈칸 ≤ k) 은?" → Gray 에서 직접 못 한다. `gray2bin(rq2)` 로 돌려 `w − r_sync ≥ D − k` — rq2 가 늦으므로 이 값도 보수적(일찍 켜짐)이라 안전하다. 이진 포인터로 같은 계산을 하면 바로 그것이 깨지는 경로다(Part B 표의 B = 4).
</details>

<details><summary>Q4. full/empty 가 늦게 풀리는데, 그건 틀린 것 아닌가?</summary>

틀린 게 아니라 보수적이다. 동기화된 포인터는 진짜 포인터의 **과거 값**이라 쓰기 쪽은 리더가 덜 읽었다고, 읽기 쪽은 라이터가 덜 썼다고 본다 → full 은 일찍 켜지고 늦게 꺼지며, empty 도 같다. 위험한 방향(진짜 full 인데 full 아님)은 96 회 스트레스에서 0, 비관적인 방향은 수십만 에지. 비용은 처리량: 양쪽이 매 에지 쓰고 읽으면 깊이 2 → 0.40, 4 → 0.79, 8 이상 → 1.00 (연습 5). 처리량 ≈ min(1, 깊이 / 왕복 지연), 이 모델의 왕복 ≈ 5 주기.

꼬리질문: "그럼 FIFO 깊이는 어떻게 정하나?" → 두 항의 합: (1) 왕복 동기화 지연을 덮는 몫(위, 실제 RTL 은 플래그를 레지스터로 내면 한두 주기 더), (2) 버스트 흡수 몫 = 버스트 길이 × (1 − 소비율/생산율) 같은 속도 차 × 지속 시간. 빠름→느림 지속 스트림은 깊이로 해결 안 된다 — 평균 속도가 맞아야 한다.
</details>

<details><summary>Q5. 이진 포인터를 비트마다 동기화하면 정확히 무엇이 깨지나? (staff 꼬리질문)</summary>

동기화된 값이 아무 값이나 될 수 있다 — 여기까지는 누구나 말한다. 이 레슨의 실측은 더 미묘하다: **한 에지에 한 칸씩, 플래그(==/≠ 비교)로만 동작하면 데이터 오류가 0** 이었다(4 클럭 쌍, 각 400k 에지). 증명: 쓰레기 값은 포인터가 p→p+1 로 움직이는 바로 그 한 주기에만 생기고, 그때 상대편은 아직 p+1 을 못 봤으므로 r ≤ p < p+1 ≤ w — 한 칸의 여유가 있다. 하지만 **포인터를 개수로 쓰면**(level·almost_full·버스트 길이·펌웨어가 읽는 FIFO level 레지스터) 쓰레기 `wq2 = 15` 가 "8 칸 있음" 이 되어 4 칸 버스트가 1 칸짜리 FIFO 를 underflow 시킨다 → 모든 클럭 쌍에서 overflow/underflow.

꼬리질문: "그럼 B = 1 설계면 이진도 괜찮다고 할 건가?" → 아니다. (1) FF2 까지 번지는 드문 준안정, (2) 나중에 누가 level 출력을 추가하는 순간 깨지는 잠복 결함(`imp` 426 번이 그 흔적), (3) 정적 CDC 검사가 다비트 교차로 위반 처리, (4) 검증 비용. Gray 는 비용이 XOR 몇 개다.
</details>

<details><summary>Q6. 빠른 도메인의 1 주기 펄스를 느린 도메인으로 넘기려면?</summary>

레벨 동기화기로는 펄스가 에지 사이로 빠진다(200→31 에서 670/5000). 토글 동기화기: 소스에서 펄스마다 레벨을 뒤집고(`tog ^= pulse`), 목적지는 2-FF 뒤 `q2 ^ q3` 로 변화를 펄스로 되돌린다 → 5000/5000. 조건: 펄스 간격 `≥ ⌈(Td + T_W)/Ts⌉` 소스 주기(200→31 이면 7). 어기면 토글 두 번이 서로 지운다 → 간격 2 에서 4212 개 유실. 연습 4 에서 이 식이 시뮬레이션 최소 간격과 정확히 같았다.

꼬리질문: "간격을 보장 못 하면?" → ack 를 돌려받아 다음 펄스를 막는 핸드셰이크(소스는 busy 동안 펄스를 모아 둠), 또는 이벤트 수를 카운터로 세고 그 카운터를 Gray 로 넘긴다(= 개수만 넘기는 비동기 FIFO). 느림→빠름은 간격 문제가 없지만, q2 를 그대로 쓰면 펄스가 6 배로 불어난다 — 에지 검출은 필수.
</details>

<details><summary>Q7. 16-bit 설정값(예: 타이머 비교값)을 다른 클럭 도메인 블록으로 넘기려면?</summary>

데이터는 그대로 두고 **한 비트 자격 신호(req)만** 동기화한다. 4-phase: 소스가 데이터를 싣고 req↑ → 목적지가 동기화된 req 를 보고 그 에지에 데이터를 직접 잡고 ack↑ → 소스가 ack 를 보고 req↓ → 목적지 ack↓. 데이터는 req 가 2-FF 를 건너는 동안 이미 안정이라 목적지의 데이터 샘플이 창에 걸린 횟수 0 (실측). 단어당 110~226 ns(왕복 두 번). 2-phase(토글 req/ack)는 왕복 한 번이라 2.0~2.3 배 빠르다(연습 3). "비트마다 2-FF" 는 섞인 단어를 만든다(242/5000).

꼬리질문: "설정값이 거의 안 바뀌는 정적 레지스터라면?" → 흔한 실무 타협: 블록을 끄고(또는 클럭 게이트) 쓰고 다시 켜서 "바뀌는 순간 아무도 안 본다" 를 보장하거나, quasi-static 신호로 문서화하고 CDC 검사에서 예외 처리. 근거 없는 예외 처리가 CDC 사고의 단골이다.
</details>

<details><summary>Q8. 펌웨어가 다른 클럭(예: 32 kHz 저전력 타이머)으로 도는 64-bit 카운터를 읽는다. 무엇이 문제이고 어떻게 읽나?</summary>

하드웨어가 동기화를 해 주지 않는 레지스터라면 읽는 순간 카운터가 바뀌어 비트가 섞일 수 있다(이 레슨의 "비트마다 2-FF" 와 같은 문제의 소프트웨어 판). 32-bit 버스로 두 번 나눠 읽으면 하위가 감싸는 순간 상위/하위가 어긋난다(0x0000_0001_FFFF_FFFF → 0x0000_0002_0000_0000 사이). 패턴: `do { hi = HI; lo = LO; } while (hi != HI);` 또는 같은 값이 두 번 나올 때까지 반복 읽기. 많은 MCU 는 이를 하드웨어로 해 준다 — 읽기 동기화 플래그(예: STM32 RTC 의 RSF: 저속 클럭 도메인 값이 shadow 레지스터로 복사됐는지), 쓰기 동기화 대기 비트. 해당 칩 레퍼런스 매뉴얼의 "synchronization" 절을 꼭 읽어야 한다 (세부는 계열마다 다름).

꼬리질문: "카운터를 Gray 로 내보내는 하드웨어라면?" → 한 번 읽어 `gray2bin` 하면 된다(어느 순간 읽어도 옛 값 또는 새 값). 일부 위치 엔코더·타이머 IP 가 이 방식을 쓴다.
</details>

<details><summary>Q9. RTL 시뮬레이션이 다 통과했는데 실리콘에서 FIFO 가 가끔 깨진다. 어떻게 접근하나?</summary>

일반 RTL 시뮬레이션은 준안정을 표현하지 않아 CDC 버그를 거의 못 잡는다. (1) 정적 CDC 검사: 동기화기 없는 교차, 동기화 전 조합 논리, 다비트 교차, 재수렴 — RTLDV-M07. (2) 메타 주입 시뮬레이션: 동기화기 출력에 무작위 1 주기 지연/값을 주입(이 레슨의 모델이 바로 그것, RTLDV-M05). (3) 클럭 비율·위상·지터 스윕 — 특히 거의 같은 주파수(창 적중이 17 번 연속으로 몰린다). (4) 실리콘에서는 온도·전압 스윕으로 재현율을 올리고, FIFO 에 overflow/underflow sticky 플래그와 포인터 스냅숏을 남긴다. 실리콘 검증 관점에서 "가끔" 은 확률 문제이므로 시간당 발생률을 재서 MTBF 식과 비교한다.
</details>

<details><summary>Q10. 리셋 신호도 CDC 인가?</summary>

리셋 assert 는 비동기여도 괜찮지만(클럭 없이도 즉시 안전 상태), **deassert** 는 클럭 에지 근처에서 일어나면 recovery/removal 위반 → 플롭마다 다른 주기에 리셋이 풀린다. 그래서 "async assert, sync deassert" 리셋 동기화기(2-FF, D 에 1, 비동기 리셋 핀에 외부 리셋)를 도메인마다 둔다. 목적지 클럭이 돌고 있어야 리셋이 풀리므로 PLL lock → 클럭 enable → 리셋 해제 순서가 필요하다 (ARCH-A09, RTLDV-B06).
</details>

## 9. 추가 연습

1. **깊이가 2^N 이 아니면.** 2..64 의 M 중 0..M−1 을 그냥 Gray 로 셀 때 감싸기(M−1 → 0)까지 1 비트인 것은? 짝수 M 의 해결책을 만들고 2..64 전부 검사하라. 홀수 M 은 왜 불가능한가?
   <details><summary>답</summary>

   그냥 Gray 는 M = 2, 4, 8, 16, 32, 64 에서만 순환한다 (M = 6: gray(5) = 7 → 0 이 3 비트). 짝수 M: 2^n 개 반사 Gray 수열의 **가운데 M 개** `gray(i + 2^(n−1) − M/2)` 를 쓴다. 반사 대칭이라 첫 코드와 끝 코드가 최상위 비트 하나만 다르다 — M = 6 이면 오프셋 1, 코드 1 3 2 6 7 5. 2..64 의 모든 짝수에서 CHECK. 홀수 M: 1 비트 변화마다 popcount 의 홀짝이 바뀌므로 M 번 바뀐 뒤 제자리로 오려면 M 이 짝수여야 한다(하이퍼큐브는 이분 그래프). 4 비트 큐브에서 길이 3, 5, 7, 9 순환 전수 탐색 → 없음. 그래도 실무에서는 대개 깊이를 2^N 으로 올린다 — 메모리 몇 칸이 검증보다 싸다.
   </details>
2. **동기화기 단 수 고르기.** 대표값 τ = 20 ps, T_W = 250 ps, tcq = 200, tsu = 150, f_data = f_clk/10 으로 MTBF ≥ 1000 년이 되는 최소 단 수를 77 MHz, 500 MHz, 1 GHz, 2 GHz 에서 구하라.
   <details><summary>답</summary>

   t_r = (k − 1)(T − 350 ps). 실제 출력: 77 MHz 2 단(2-FF MTBF 1.7e269 s), 500 MHz 2 단(1.1e29 s), 1 GHz **3 단**(2-FF 5.2e6 s ≈ 60 일 → 3-FF 6.8e20 s), 2 GHz **7 단**(단당 t_r = 150 ps 뿐 → 단당 e^7.5 ≈ 1800 배). 손 계산 1 GHz 2-FF: e^(650/20) = 1.30e14, 분모 250e-12 × 1e9 × 1e8 = 2.5e7 → 5.2e6 s. 고속 도메인에서는 단을 늘리기보다 느린 클럭 쪽에서 동기화하거나, 데이터 변화율을 낮추는(Gray 카운터를 몇 주기마다만 갱신) 설계를 먼저 본다. 수치는 대표값이며 실제는 셀 라이브러리의 τ, T_W 를 쓴다.
   </details>
3. **2-phase 핸드셰이크.** req 와 ack 를 레벨이 아니라 토글로 쓰는 2-phase 버전을 만들어 4-phase 와 처리량(ns/word)을 비교하라.
   <details><summary>답</summary>

   소스: `req == ack_q2` 이면(이전 전송 완료) 데이터 싣고 `req ^= 1`. 목적지: `req_q2 != ack` 이면 데이터 잡고 `ack = req_q2`. 실제 출력 4ph/2ph ns/word: 100→77 129.0/55.6 (2.32 배), 200→31 193.5/96.8 (2.00), 31→200 225.8/96.8 (2.33), 100→99.97 110.8/50.4 (2.20). 왕복이 둘에서 하나로 줄었으니 약 2 배. 대가: 양쪽에 "지난 레벨" 상태가 필요하고 리셋 후 req/ack 레벨을 맞춰야 한다. 5000 단어 모두 정확, 데이터 메타 0.
   </details>
4. **토글 펄스 최소 간격.** 4 클럭 쌍에서 간격 S..S+2 로 5000 펄스 × 시드 3 을 보내 유실 0 인 최소 S 를 찾고 식 `⌈(Td + T_W)/Ts⌉` 와 비교하라.
   <details><summary>답</summary>

   실제 출력: 100→77 식 2 / 시뮬 2 (S = 1 이면 2360 유실), 200→31 7 / 7 (S = 6 이면 770 유실), 31→200 1 / 1, 100→99.97 2 / 2 (S = 1 이면 44 유실 — 위상이 천천히 미끄러져 유실이 드물지만 0 은 아니다). 식은 충분조건인데 이 모델에서는 최소값과 일치한다. 실제 설계에서는 지터·duty·여유를 더해 한 칸 더 잡는다.
   </details>
5. **FIFO 깊이 vs 처리량.** 거의 같은 주파수(100 vs 99.97 MHz)에서 양쪽이 매 에지 쓰고 읽으려 할 때, 깊이 2..32 의 읽기 에지당 처리량을 재고 필요한 최소 깊이를 설명하라.
   <details><summary>답</summary>

   실제 출력: 깊이 2 → 0.396, 4 → 0.791, 8/16/32 → 1.000, 오류 0. 라이터가 쓴 칸이 리더에게 보이기까지 약 2~3 rclk, 리더가 비운 칸이 라이터에게 보이기까지 약 2~3 wclk → 왕복 ≈ 5 주기 동안 라이터는 새 빈칸을 못 본다. 처리량 ≈ 깊이 / 왕복 → 2/5, 4/5. 깊이 ≥ 왕복이면 포화. 플래그를 레지스터로 내는 실제 RTL 은 왕복이 한두 주기 더 길어 최소 깊이도 커진다 — 이것이 "같은 주파수인데도 비동기 FIFO 깊이를 8 이상" 으로 잡는 이유다.
   </details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr      # solution.c 를 포함하므로 같은 폴더에서
```

실제 출력 (PASS 줄 생략):

```text
== 연습 1: 깊이 M 의 순환 Gray (마지막 → 처음 도 1 비트) ==
  plain binary-reflected Gray 0..M-1 cyclic for M = 2 4 8 16 32 64 
  e.g. M=6: gray(5)=7 -> gray(0)=0 : 3 bits change
  M=6 middle codes: 1 3 2 6 7 5  (offset 1)
  odd M = 3,5,7,9 in 4-bit cube: no cycle (bipartite)
== 연습 2: MTBF (대표값 tau=20 ps, T_W=250 ps, tcq=200, tsu=150, f_data = f_clk/10) ==
       f_clk  MTBF 2-FF (s)  MTBF 3-FF (s)   stages
       77 MHz     1.731e+269            inf        2
      500 MHz      1.080e+29      7.290e+64        2
     1000 MHz      5.208e+06      6.780e+20        3
     2000 MHz      1.808e-05      3.269e-02        7
== 연습 3: 2-phase vs 4-phase req/ack (16-bit, 5000 words) ==
  clocks        4ph ns/word  2ph ns/word   ratio
  100->77 MHz         129.0         55.6    2.32
  200->31 MHz         193.5         96.8    2.00
  31->200 MHz         225.8         96.8    2.33
  100->99.97          110.8         50.4    2.20
== 연습 4: 토글 펄스 동기화기 최소 간격 (소스 주기, 간격 = S..S+2) ==
  clocks          formula    sim min lost at S=formula-1
  100->77 MHz           2          2               2360
  200->31 MHz           7          7                770
  31->200 MHz           1          1                  -
  100->99.97            2          2                 44
== 연습 5: Gray FIFO 깊이 vs 처리량 (쓰기·읽기 모두 매 에지 시도, 100->99.97 MHz) ==
   depth reads/edge     errors
       2      0.396          0
       4      0.791          0
       8      1.000          0
      16      1.000          0
      32      1.000          0
10/10 passed
```

(77 MHz 3-FF 의 `inf` 는 e^1264 가 double 범위를 넘은 것 — "사실상 무한" 의 문자 그대로의 출력.)

## 10. 핵심 정리

- 비동기 입력은 setup/hold 를 보장할 수 없다 → 창 적중은 **반드시** 일어난다(초당 `T_W · f_clk · f_data` 번). 2-FF 는 결정 시간을 한 주기 주고 팬아웃을 하나로 만든다. MTBF 는 t_r 에 지수적 → 기본 2 단, 고속·고대수면 3 단 이상.
- 2-FF 지연 = 1~2 목적지 주기 (`Td + tcq − th` ~ `2Td + tcq + tsu`). 거의 같은 주파수면 창 적중이 몰려 온다.
- 다비트는 비트마다 동기화하면 섞인다. 해법 셋: **Gray**(한 번에 1 비트 변하는 카운터/포인터), **핸드셰이크**(데이터는 안정, 한 비트 자격 신호만 동기화), **비동기 FIFO**(둘의 조합).
- 비동기 FIFO: 깊이 2^N, 포인터 N+1 비트, Gray 로 플롭에서 내보내 2-FF. `empty = rg == wq2`, `full = wg == rq2 ^ 0b11<<(N−1)`. 플래그는 보수적(늦게 풀림)이지만 절대 틀리지 않는다. 깊이 ≥ 왕복 동기화 지연이어야 처리량이 찬다.
- 이진 포인터 동기화는 "있을 수 없는 값" 을 만든다. 한 칸씩 플래그로만 쓰면 우연히(구조적으로) 가려지지만, level·버스트로 쓰는 순간 overflow/underflow — 설계 규칙은 그냥 Gray.
- 펄스는 토글로 레벨화, 간격 `≥ ⌈(Td + T_W)/Ts⌉`. 리셋은 async assert / sync deassert.
- 일반 RTL 시뮬레이션은 CDC 를 못 본다 → 메타 주입 + 정적 CDC 검사 + 클럭 비율 스윕.

## 참고자료

- C. E. Cummings, "Simulation and Synthesis Techniques for Asynchronous FIFO Design", SNUG San Jose 2002 — Gray 포인터 비동기 FIFO, full 조건 `{~wq2_rptr[MSB:MSB-1], wq2_rptr[MSB-2:0]}` 형태.
- C. E. Cummings, "Clock Domain Crossing (CDC) Design & Verification Techniques Using SystemVerilog", SNUG Boston 2008 — 2-FF, 펄스 동기화, 다비트 교차(MCP 공식, 핸드셰이크), 재수렴.
- R. Ginosar, "Metastability and Synchronizers: A Tutorial", IEEE Design & Test of Computers, 2011 — MTBF = e^(S/τ)/(T_W · F_C · F_D), 동기화기 단 수 선택.
- W. J. Dally, J. W. Poulton, *Digital Systems Engineering*, Cambridge Univ. Press, 1998 — 10 장 동기화(준안정, 동기화기 실패 확률).
- D. M. Harris, S. L. Harris, *Digital Design and Computer Architecture* — 3.5 Timing of Sequential Logic (준안정, 동기화기).
- STMicroelectronics 레퍼런스 매뉴얼의 RTC 절 (예: RM0090 STM32F4) — shadow 레지스터와 RSF 동기화 플래그 (계열마다 세부 다름, 해당 RM 확인).
- 이 노트의 관련 레슨: ARCH-B06 (2-phase 갱신, 플롭), ARCH-B13 (setup/hold, MTBF 2-FF vs 3-FF), ARCH-B14 (리셋 원인), ARCH-M11 (DMA DREQ/버스트), ARCH-A04 (커버리지와 함께 보는 "위반 0"), ARCH-A09 (전원·클럭·리셋 시퀀싱), 다음 단계 RTLDV-B06 (리셋 동기화기 RTL), RTLDV-M05 (CDC 1 RTL), RTLDV-M06 (비동기 FIFO RTL), RTLDV-M07 (정적 CDC 검사).
