---
id: COMM-M06
time: 90분
updated: 2026-11-11
---
# COMM-M06 CAN 비트 타이밍 계산기: TQ, 세그먼트, 샘플 포인트

## 학습 목표

- tq = BRP / f_can, NBT = 1 + TSEG1 + TSEG2, SP = (1 + TSEG1) / NBT 로 한 비트를 쪼개고, 주어진 클럭·bitrate·목표 샘플 포인트(SP)에 대해 **모든 BRP/TSEG 조합을 열거**해 명시한 규칙(오차 → SP 거리 → tq 수 → BRP)으로 최선을 고른다. 비교는 부동소수 없이 정수 교차 곱으로.
- 25 MHz 의 1 Mbit/s(68% 강제), 42 MHz 의 21 tq(TSEG1 한계로 탈락), 14.7456 MHz(UART 크리스털 −1.70%) 처럼 **안 되는 클럭**을 계산기로 걸러내고 이유를 설명한다.
- PROP_SEG / PS1 분할과 왕복 전파 지연, 발진기 허용 오차 두 조건 `min(PS1,PS2)/(2·(13·NBT−PS2))`, `SJW/(20·NBT)` 를 계산하고, **재동기화 시뮬레이션으로 조건 2 를 0.1% 안에서 확인**한다.
- 결과를 STM32 bxCAN `CAN_BTR` 값과 Linux `ip link set can0 type can ...` 명령으로 내보내고, 레지스터 덤프를 거꾸로 해석해 bring-up 버그를 찾는다.

## 1. 문제

"CAN 이 안 붙어요" — bring-up 에서 첫 번째로 의심하는 것이 비트 타이밍이고, 면접에서는 "16 MHz 에서 500k, 샘플 포인트 87.5% 로 설정해 보세요", "SJW 는 뭐고 왜 필요해요?", "크리스털 대신 RC 발진기 써도 돼요?" 로 들어온다. 숫자 하나 틀리면 버스 전체가 error frame 으로 덮인다(COMM-M07).

UART 는 프레임마다 start 에지에서 한 번 맞추고 끝까지 혼자 간다(COMM-B03). CAN 은 프레임이 최대 백수십 비트라 그렇게 못 한다. 대신 **비트 하나를 시간 양자(time quantum, tq)** 여러 개로 쪼개고, 열성(recessive, 1)→우성(dominant, 0) 에지가 올 때마다 위상을 조금씩 고친다(재동기화, resynchronization). 비트 스터핑(COMM-M04)이 같은 값이 5개를 넘지 않게 해 주니, 에지는 최악이라도 10 비트마다 온다.

한 비트(nominal bit time, NBT)는 네 구간이다 (ISO 11898-1 / Bosch CAN 2.0 용어):

| 구간 | 길이 | 역할 |
|---|---|---|
| SYNC_SEG | 1 tq 고정 | 에지가 여기 오길 기대한다 |
| PROP_SEG | 1~ tq | 버스 왕복 지연(드라이버 → 선 → 먼 노드 → 선 → 수신기)을 흡수 |
| PHASE_SEG1 (PS1) | 1~ tq | 재동기화 때 **늘어날** 수 있다 |
| PHASE_SEG2 (PS2) | 1~ tq | 재동기화 때 **줄어들** 수 있다 |

샘플 포인트(sample point)는 PS1 끝 = PS2 시작이다. STM32 bxCAN 처럼 많은 컨트롤러는 PROP 와 PS1 을 합쳐 **TSEG1(TS1)** 하나로, PS2 를 **TSEG2(TS2)** 로 설정한다. 그림은 16 MHz 에서 BRP 2, TS1 13, TS2 2 (500 kbit/s, 87.5%)이고, 왕복 지연 700 ns 를 가정하면 PROP = ⌈700/125⌉ = 6 tq 이다 (값은 모두 solution.c 가 계산한 것).

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">1 비트 = 16 tq, tq = BRP / f_can = 2 / 16 MHz = 125 ns → 2.000 µs = 500 kbit/s</text>
  <text x="10" y="48" fill="currentColor" font-size="12">ISO 관점</text>
  <rect x="40" y="56" width="42.5" height="36" class="f-mut" stroke="currentColor"/>
  <rect x="82.5" y="56" width="255" height="36" class="f-warn" stroke="currentColor"/>
  <rect x="337.5" y="56" width="297.5" height="36" class="f-acc" stroke="currentColor"/>
  <rect x="635" y="56" width="85" height="36" class="f-ok" stroke="currentColor"/>
  <text x="44" y="79" fill="currentColor" font-size="12">SYNC</text>
  <text x="140" y="79" fill="currentColor">PROP = 6 tq (750 ns)</text>
  <text x="430" y="79" fill="currentColor">PS1 = 7 tq</text>
  <text x="645" y="79" fill="currentColor">PS2 = 2</text>
  <g stroke="currentColor" stroke-width="1">
    <line x1="40" y1="96" x2="40" y2="104"/><line x1="82.5" y1="96" x2="82.5" y2="104"/><line x1="125" y1="96" x2="125" y2="104"/>
    <line x1="167.5" y1="96" x2="167.5" y2="104"/><line x1="210" y1="96" x2="210" y2="104"/><line x1="252.5" y1="96" x2="252.5" y2="104"/>
    <line x1="295" y1="96" x2="295" y2="104"/><line x1="337.5" y1="96" x2="337.5" y2="104"/><line x1="380" y1="96" x2="380" y2="104"/>
    <line x1="422.5" y1="96" x2="422.5" y2="104"/><line x1="465" y1="96" x2="465" y2="104"/><line x1="507.5" y1="96" x2="507.5" y2="104"/>
    <line x1="550" y1="96" x2="550" y2="104"/><line x1="592.5" y1="96" x2="592.5" y2="104"/><line x1="635" y1="96" x2="635" y2="104"/>
    <line x1="677.5" y1="96" x2="677.5" y2="104"/><line x1="720" y1="96" x2="720" y2="104"/>
  </g>
  <text x="36" y="117" fill="currentColor" font-size="12">0</text><text x="330" y="117" fill="currentColor" font-size="12">7</text>
  <text x="624" y="117" fill="currentColor" font-size="12">14</text><text x="710" y="117" fill="currentColor" font-size="12">16 tq</text>
  <text x="10" y="146" fill="currentColor" font-size="12">bxCAN</text>
  <rect x="40" y="132" width="42.5" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="82.5" y="132" width="552.5" height="30" class="f-acc" stroke="currentColor"/>
  <rect x="635" y="132" width="85" height="30" class="f-ok" stroke="currentColor"/>
  <text x="250" y="152" fill="currentColor">TS1 = PROP + PS1 = 13 tq (BTR.TS1 필드 = 12)</text>
  <text x="640" y="152" fill="currentColor" font-size="12">TS2 = 2 (필드 1)</text>
  <line x1="635" y1="40" x2="635" y2="178" stroke="currentColor" stroke-width="2.5"/>
  <path d="M629,178 L635,190 L641,178 Z" fill="currentColor"/>
  <text x="470" y="205" fill="currentColor">샘플 포인트 = (1 + 13) / 16 = 87.5%</text>
  <text x="40" y="232" fill="currentColor" font-size="12">PS1 은 재동기화로 최대 SJW 만큼 늘어나고, PS2 는 최대 SJW 만큼 줄어든다 (SJW ≤ TS2 = 2).</text>
</svg>
```

### Part A — 계산기 (열거 + 선택)

`bt_make()` 로 조합 하나를 검사·계산하고(한계, SJW ≤ TSEG2, NBT 8–25), `bt_enumerate()` 로 목표 bitrate 오차 한도 안의 모든 조합을 캐퍼시티가 있는 버퍼에 나열하고, `bt_better()` 규칙으로 `bt_best()` 가 최선을 고른다. 규칙: **(1) bitrate 오차 최소 (2) |SP − 목표| 최소 (3) tq 많이 (4) BRP 작게 (5) 같은 거리면 낮은 SP.** 모델 한계는 bxCAN 류(BRP 1–1024, TSEG1 1–16, TSEG2 1–8, SJW 1–4 — RM 확인 권장). 결과를 브루트포스와 랜덤 3000 케이스에서 비교한다.

### Part B — 경계와 실패, 허용 오차

목표를 못 맞추는 클럭(TSEG1 한계, NBT 범위, 안 나누어지는 크리스털)은 오류 코드(`BT_ERATE`: 최선을 채워 주되 오차가 한도 초과 / `BT_ENOSOL` / `BT_EARG`)로 구분한다. 왕복 지연 → PROP(`bt_prop_tq`), PS1 = TSEG1 − PROP(< 1 이면 `BT_EPROP`), 발진기 허용 오차 두 조건(`bt_tolerance`)을 ppm 으로 계산한다. 그리고 **클럭 편차를 주입**한 재동기화 시뮬레이션(`resync_on_edge` + `rx_sim`)으로 조건 2 의 경계 안(0.95×)에서는 오류 0, 밖(1.10×)에서는 오류가 나는 것을 5개 설정에서 확인한다.

### Part C — 레지스터 값과 Linux 명령

`bxcan_btr()` / `bxcan_btr_decode()` 로 CAN_BTR 를 만들고 되읽는다 (전수 52만 조합 왕복). `linux_ip_cmd()` 는 `bitrate + sample-point` 줄과, tq 가 정수 ns 일 때만 `tq + prop-seg + phase-seg1 + phase-seg2 + sjw` 줄을 만든다. 마지막으로 42 MHz(STM32F4 APB1)와 16 MHz 두 노드를 500k 로 맞추는 bring-up 요약을 출력한다.

## 2. Problem (English)

> **Part A.** A CAN controller divides its clock `f_can` by a prescaler `BRP` to get the time quantum `tq = BRP/f_can`. A bit is `NBT = 1 + TSEG1 + TSEG2` quanta; the sample point is `SP = (1+TSEG1)/NBT`. Limits (bxCAN-like): BRP 1–1024, TSEG1 1–16, TSEG2 1–8, SJW 1–4 with SJW ≤ TSEG2, NBT 8–25.
> Implement `int bt_best(uint32_t f_can, uint32_t bitrate, uint32_t sp_permille, const bt_limits *lim, uint32_t max_err_ppm, can_bt *out)` that picks: minimum |bitrate error|, then minimum |SP − target|, then more quanta, then smaller BRP, then lower SP; SJW = min(SJW_max, TSEG2). Use integer math only (no floats in comparisons). Also `bt_enumerate(..., can_bt *out, size_t cap, size_t *total)` listing every valid combo within `max_err_ppm` (report `BT_ESPACE` if `cap` is too small).
> Examples: 16 MHz, 500 kbit/s, 875 ‰ → BRP 2, TSEG1 13, TSEG2 2 (16 tq, 87.5 %). 42 MHz, 500k → BRP 6, 1+11+2 (85.7 %). 25 MHz, 1M → only 1+16+8 (68 %). 14.7456 MHz, 500k → best is −1.696 % → `BT_ERATE`.
>
> **Part B.** Given a round-trip bus delay, compute `PROP = ceil(t_rt/tq)` and `PS1 = TSEG1 − PROP` (must be ≥ 1). Compute the per-node oscillator tolerance `df = min( min(PS1,PS2)/(2·(13·NBT − PS2)), SJW/(20·NBT) )` in ppm. Implement the resynchronization rule of a receiver (phase error e from the slot where a recessive→dominant edge is seen; lengthen PS1 or shorten PS2 by at most SJW) and show, with an injected clock mismatch, that sampling stays correct below `2·df2` and fails above it.
>
> **Part C.** Encode/decode the STM32 bxCAN `CAN_BTR` register (SJW−1 [25:24], TS2−1 [22:20], TS1−1 [19:16], BRP−1 [9:0], LBKM [30], SILM [31]) and generate the Linux `ip link set <if> type can ...` command(s) into a caller buffer.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열 군데: Part A `bt_make`, `bt_better`, `bt_best`, `bt_enumerate` / Part B `bt_prop_tq`, `bt_tolerance`, `resync_on_edge` / Part C `bxcan_btr`, `bxcan_btr_decode`, `linux_ip_cmd`. 도우미(`rate_within`, `args_ok`), 시뮬레이터 루프 `rx_sim`(시간으로 끝나는 루프 — 스텁이어도 멈추지 않는다), 하네스와 브루트포스 기준은 주어진다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:248  bt_make(16000000, 500000, 2, 13, 2, 2, &BXCAN, &b) == BT_OK && b.ntq == 16 && b.err_ppm == 0 && b.sp_permille == 875
…
random: bt_best == brute force in 0/3000, best found by enumerate in 0/0
…
resync NBT  0 SJW 0: 2*df2 =  0.00%  errors @0.95x: -20/-20  @1.10x: -20/-20
FAIL starter.c:389  rs_ok == 5
…
BTR encode/decode: 0 valid round trips, 425984 mismatches
…
15/72 passed
```

(exit 1. 통과한 15개는 주어진 도우미 검사와, 스텁의 "안전한 오답"이 우연히 기대값과 같은 경우 — 인자 오류 → `BT_EARG`, 예약 비트 → `BT_EARG`, 한도 안 조합 0개 등.)

## 4. 힌트

<details><summary>힌트 1 — 오차를 분수로 두고 교차 곱</summary>

실제 bitrate 는 f/(BRP·NBT). 목표 R 이 같은 두 후보 a, b 의 |오차| 비교는 |f − R·d_a| / d_a vs |f − R·d_b| / d_b (d = BRP·NBT) — 양변에 d_a·d_b 를 곱하면 정수 비교다. SP 도 |1000(1+TSEG1) − T·NBT| / (1000·NBT) 로 같은 방법. f ≤ 1 GHz, d ≤ 25600 이면 uint64 로 넉넉하다.

</details>

<details><summary>힌트 2 — BRP 를 전부 돌 필요 없다</summary>

NBT 를 고정하면 실제 bitrate f/(BRP·NBT) 는 BRP 에 대해 단조 감소다. 그러면 |오차| 가 최소인 BRP 는 x = f/(R·NBT) 의 floor 아니면 ceil 이다 (한계 밖이면 한계 끝). NBT 18개 × BRP 후보 2~4개 × TSEG2 8개 = 수백 번이면 끝난다. 열거(`bt_enumerate`)는 과제가 "전부"라서 1024 × 18 을 돌되, (BRP, NBT) 의 오차를 먼저 걸러 TSEG 루프를 줄인다.

</details>

<details><summary>힌트 3 — PROP 는 올림, PS1 ≥ 1</summary>

PROP 는 지연을 "덮어야" 하므로 ⌈t_rt / tq⌉. 700 ns / 125 ns = 5.6 → 6. 딱 나누어지면(750 ns) 6 그대로. PS1 = TSEG1 − PROP 가 0 이하면 이 설정으로는 그 버스 길이를 못 쓴다 — 오류.

</details>

<details><summary>힌트 4 — 허용 오차 식의 숫자들</summary>

조건 2: 재동기화 에지 사이 최대 10 비트 동안 두 노드가 (2·df)·10·NBT tq 만큼 어긋나고, 에지 하나가 고칠 수 있는 양은 SJW tq. 조건 1: 에러 플래그 뒤처럼 13 비트(정확히는 13·NBT − PS2 tq) 동안 재동기화 없이 버텨야 하는 구간에서, 남은 여유는 min(PS1, PS2). 둘 다 "노드당" df 라서 분모에 2 가 있다.

</details>

<details><summary>힌트 5 — 재동기화의 부호</summary>

에지가 SYNC 뒤(샘플 전)에 보이면 송신기가 늦은 것(e > 0) → 내 비트를 늘려 샘플 포인트를 뒤로. 샘플 뒤에 보이면 송신기의 다음 비트가 일찍 시작한 것(e < 0) → 내 비트를 줄인다. 줄일 양 |e| 이 SJW 이하면 "지금 이 slot 이 다음 비트의 SYNC" 로 끝나고, 아니면 SJW 만큼만 줄인다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 입력 확인 | f_can (어느 클럭? APB1 분주까지), 목표 bitrate, 목표 SP(누가 정했나 — CiA/OEM 규격), 버스 길이·트랜시버 지연, 컨트롤러 필드 범위 |
| 2. 정수 분해 | f/R = BRP·NBT 가 정수인가. NBT 8–25 의 약수 중 TSEG 한계 안에서 SP 가 목표에 가까운 것 |
| 3. 선택 규칙 | 오차 → SP → tq 많이 → BRP 작게. 규칙을 **말로** 정하고 코드가 그대로 따른다 |
| 4. 검증 | PROP 가 지연을 덮는가(PS1 ≥ 1), 허용 오차 df 가 발진기 ppm 예산을 넘는가 |
| 5. 출력 | 레지스터 값(필드 − 1!), Linux 명령, 스코프로 비트 폭 측정해 확인 |

### Part A — 분해와 선택

**식.** tq = BRP/f_can, NBT = 1 + TSEG1 + TSEG2, bitrate = f_can/(BRP·NBT), SP = (1+TSEG1)/NBT. 즉 f_can/R = BRP·NBT 가 정수가 되어야 오차 0 이다.

**손 계산 예: 42 MHz, 500k, 목표 87.5%.** 42e6/500e3 = 84 = BRP·NBT. NBT ∈ [8, 25] 의 84 약수: 12, 14, 21.

| NBT | BRP | (1+TSEG1)/NBT 후보 | 87.5% 에 가장 가까운 것 | 비고 |
|---|---|---|---|---|
| 12 | 7 | 10/12 = 83.3, 11/12 = 91.7 | 둘 다 4.17 차이 → 규칙 5 로 83.3 | |
| 14 | 6 | 12/14 = 85.7, 13/14 = 92.9 | **85.7 (1.79 차이)** | 선택 |
| 21 | 4 | 18/21 = 85.7 이 이상적이지만 TSEG1 = 17 > 16 | 17/21 = 81.0 | TSEG1 한계 |

→ BRP 6, TS1 11, TS2 2, SJW 2, SP 85.7%. 코드 출력 `42.00 MHz  500k @875     BRP    6  NBT 14 (1+11+2)  SJW 2  SP 85.7%  err +0 ppm` 와 같고, 분수(Fraction)로 따로 짠 python 브루트포스(개인 검산용)도 같은 답을 냈다.

**정수 비교.** `bt_better` 는 |f − R·d_a|·d_b vs |f − R·d_b|·d_a 와 |1000(1+T1_a) − T·N_a|·N_b vs (b 쪽) 를 uint64 로 비교한다. 부동소수로 "87.5 == 87.5" 를 하면 14/16 와 7/8 이 같은지조차 반올림에 맡기게 된다. 입력 범위를 f ≤ 1 GHz, 1 kbit/s ≤ R ≤ 10 Mbit/s 로 정했으므로 곱이 2^63 안에 들어간다(모델 정의, 범위 밖은 `BT_EARG`).

**탐색량.** `bt_best` 는 NBT 18개 × BRP 후보 4개(floor, ceil, 양 끝) × TSEG2 최대 8개 ≈ 576 번 `bt_make`. 브루트포스(1024 × 18 × 16)와 3000 랜덤 케이스(f 4–100 MHz, R 1k–1M, SP 60–92%)에서 **3000/3000 일치**, 그리고 최선의 오차를 한도로 열거하면 최선이 목록 안에 있다(233/233).

**열거.** 16 MHz, 500k, 오차 0: (BRP, NBT) = (2, 16), (4, 8) → TSEG 분할이 각각 8개(TSEG2 1–8), 6개(TSEG1 1–6) → **14개**. 캐퍼시티 5 로 부르면 앞 5개를 채우고 `BT_ESPACE` + total 14. 순서는 BRP↑, NBT↑, TSEG1↑ 로 고정해 테스트가 결정적이다.

**규칙 순서가 결과를 바꾼다.** 36 MHz 500k 87.5% 는 72 = 8·9 = 18·4 라서 8 tq 로 정확히 87.5% 가 나오고, 18 tq 는 88.9% 다. 이 레슨 규칙(SP 거리 먼저)은 **8 tq** 를 고른다. "tq 많이"를 앞에 두는 도구는 18 tq 를 고를 수 있다 — 어느 쪽도 틀리지 않지만, 규칙을 명시하지 않으면 두 팀이 다른 BTR 을 들고 와서 싸운다.

### Part B — 안 되는 클럭, PROP, 허용 오차

**안 되는 클럭** (solution.c 실제 출력):

```text
25.00 MHz 1000k @875     BRP    1  NBT 25 (1+16+8)  SJW 4  SP 68.0%  err +0 ppm
14.75 MHz  500k @875     BRP    2  NBT 15 (1+12+2)  SJW 2  SP 86.7%  err -16960 ppm  (BT_ERATE)
 4.00 MHz 1000k @875     BRP    1  NBT  8 (1+ 6+1)  SJW 1  SP 87.5%  err -500000 ppm  (BT_ERATE)
```

- 25 MHz/1M = 25 → NBT 25 BRP 1 뿐(NBT 5 는 8 미만). TSEG1 ≤ 16, TSEG2 ≤ 8 이면 25 = 1+16+8 한 가지 → **68% 강제**. 오차 0 인데 쓸 수 없는 설정이다.
- 14.7456 MHz(COMM-B03 의 "UART 매직 크리스털")/500k = 29.49 — 29 는 소수라 NBT ≤ 25 로 못 만들고, 30(BRP 2 × 15)이 최선 −1.696%. CAN 허용 오차(아래 0.485%)보다 훨씬 크다.
- 4 MHz/1M = 4 < 8 tq → 할 수 있는 최선이 500k.

**PROP 와 PS1.** 아비트레이션(COMM-M05) 동안 송신기는 자기가 보낸 비트를 샘플 포인트에서 다시 읽는다. 가장 먼 노드의 우성 비트가 되돌아오기 전에 샘플하면 중재가 깨진다. 그래서 샘플 포인트 앞에 왕복 지연 t_rt = 2·(선 지연 + 트랜시버·컨트롤러 지연)을 넣어야 하고, 그게 PROP 다. bxCAN 에는 PROP 필드가 없으니 TS1 안에서 PROP 를 빼고 남은 것이 PS1 이다. 16 MHz 500k(TS1 13) 에서 t_rt 700 ns → PROP 6, PS1 7. PROP 12 → PS1 1 (허용 오차 반토막: df1 4854 → 2427 ppm), PROP 13 → `BT_EPROP`.

**허용 오차 두 조건** (Bosch "The Configuration of the CAN Bit Timing" / ISO 11898-1 — 식은 기억에서, 확인 권장. df 는 노드 하나의 |Δf/f|):

- 조건 2: df ≤ SJW / (20·NBT). 에지 사이 최대 10 비트 동안 상대 편차 2·df 가 쌓는 위상 2·df·10·NBT tq 를 에지 하나가 SJW 만큼 고칠 수 있어야 한다.
- 조건 1: df ≤ min(PS1, PS2) / (2·(13·NBT − PS2)). 에러 플래그(우성 6비트 + 다른 노드의 플래그가 겹쳐 최대 13 비트 근처) 동안처럼 재동기화를 기대할 수 없는 구간에서 남은 여유가 min(PS1, PS2).

| 설정 (PROP) | PS1 | PS2 | SJW | df1 | df2 | df | 묶는 조건 |
|---|---|---|---|---|---|---|---|
| 16 MHz 500k, 1+13+2 (PROP 6) | 7 | 2 | 2 | 4854 ppm | 6250 ppm | **0.485%** | 1 |
| 같은 설정, SJW 1 | 7 | 2 | 1 | 4854 | 3125 | 0.313% | 2 |
| 같은 설정, PROP 12 | 1 | 2 | 2 | 2427 | 6250 | 0.243% | 1 |
| 8 MHz 1M, 1+6+1 (PROP 4) | 2 | 1 | 1 | 4854 | 6250 | 0.485% | 1 |
| 25 MHz 1M, 1+16+8 (PROP 4) | 12 | 8 | 4 | 12618 | 8000 | 0.800% | 2 |

16 tq 와 8 tq 가 **같은 비율이면 같은 허용치**(4854 ppm) — tq 를 늘리는 이득은 허용 오차가 아니라 SP·SJW 를 더 잘게 고를 수 있는 해상도다. 68% 설정은 PS2 가 커서 오히려 관대하지만 PROP 자리가 좁다(트레이드오프).

**재동기화 규칙과 시뮬레이션.** 수신기는 tq 마다 버스를 보고, 열성→우성 에지가 보인 slot 으로 위상 오차 e 를 정한다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">재동기화 (16 tq, 1+13+2, SJW 2) — 수신기 관점</text>
  <text x="10" y="48" fill="currentColor">e &gt; 0: 에지가 slot 3 (샘플 전) → 송신기가 늦다 → PS1 을 min(3, SJW) = 2 tq 늘린다</text>
  <text x="10" y="76" fill="currentColor" font-size="12">원래</text>
  <rect x="60" y="62" width="30" height="22" class="f-mut" stroke="currentColor"/>
  <rect x="90" y="62" width="390" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="480" y="62" width="60" height="22" class="f-ok" stroke="currentColor"/>
  <line x1="480" y1="56" x2="480" y2="92" stroke="currentColor" stroke-width="2.5"/>
  <path d="M165,58 L165,90" stroke="currentColor" stroke-width="2" stroke-dasharray="4 2"/>
  <text x="170" y="100" fill="currentColor" font-size="12">에지 (slot 3)</text>
  <text x="10" y="128" fill="currentColor" font-size="12">고친 뒤</text>
  <rect x="60" y="114" width="30" height="22" class="f-mut" stroke="currentColor"/>
  <rect x="90" y="114" width="450" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="540" y="114" width="60" height="22" class="f-ok" stroke="currentColor"/>
  <line x1="540" y1="108" x2="540" y2="144" stroke="currentColor" stroke-width="2.5"/>
  <text x="480" y="158" fill="currentColor" font-size="12">샘플 +2 tq →</text>
  <text x="610" y="130" fill="currentColor" font-size="12">비트 18 tq</text>
  <text x="10" y="190" fill="currentColor" font-size="12">e &lt; 0: 에지가 slot 15 (샘플 뒤), e = 15 − 16 = −1 → |e| ≤ SJW 라 이 slot 이 다음 비트 SYNC</text>
  <text x="10" y="218" fill="currentColor" font-size="12">원래</text>
  <rect x="60" y="204" width="30" height="22" class="f-mut" stroke="currentColor"/>
  <rect x="90" y="204" width="390" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="480" y="204" width="60" height="22" class="f-ok" stroke="currentColor"/>
  <line x1="480" y1="198" x2="480" y2="234" stroke="currentColor" stroke-width="2.5"/>
  <path d="M525,200 L525,232" stroke="currentColor" stroke-width="2" stroke-dasharray="4 2"/>
  <text x="10" y="262" fill="currentColor" font-size="12">고친 뒤</text>
  <rect x="60" y="248" width="30" height="22" class="f-mut" stroke="currentColor"/>
  <rect x="90" y="248" width="390" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="480" y="248" width="30" height="22" class="f-ok" stroke="currentColor"/>
  <rect x="510" y="248" width="30" height="22" class="f-warn" stroke="currentColor"/>
  <text x="545" y="264" fill="currentColor" font-size="12">← 다음 SYNC (비트 15 tq)</text>
  <text x="10" y="292" fill="currentColor" font-size="12">|e| &gt; SJW 이면 PS2 를 SJW 만큼만 줄인다. 샘플 포인트 사이에 재동기화는 한 번.</text>
</svg>
```

`rx_sim` 은 송신기 비트 폭을 1/(1+δ) 배로 바꿔(δ = TX bitrate / RX bitrate − 1) "00000 11111" 반복 — 스터핑이 허용하는 가장 드문 열성→우성 에지(10 비트마다) — 을 보내고, 수신기의 n 번째 샘플이 TX 비트 n 안에 같은 값으로 떨어지는지 센다. 두 노드가 각자 ±df 면 최악 상대 편차가 2·df 이므로, 조건 2 의 경계는 |δ| = 2·df2 = SJW/(10·NBT). solution.c 는 0.95× 에서 오류 0, 1.10× 에서 오류 > 0 을 5개 설정에서 확인한다:

```text
resync NBT 16 SJW 2: 2*df2 =  1.25%  errors @0.95x: 0/0  @1.10x: 38614/29961
resync NBT 16 SJW 1: 2*df2 =  0.62%  errors @0.95x: 0/0  @1.10x: 36342/17210
resync NBT  8 SJW 1: 2*df2 =  1.25%  errors @0.95x: 0/0  @1.10x: 38382/30829
resync NBT 25 SJW 4: 2*df2 =  1.60%  errors @0.95x: 0/0  @1.10x: 36840/34822
resync NBT 16 SJW 4: 2*df2 =  2.50%  errors @0.95x: 0/0  @1.10x: 38406/37254
```

`demo_resync.c` 는 δ 를 0.01% 씩 키워 첫 오류 지점(onset, 위상 40개 × 2000 비트)을 찾는다 (실제 출력, Apple clang 21, 약 10 초):

```text
NBT TSEG1 TSEG2 SJW | theory SJW/(10*NBT) | onset 5/5 (fast TX / slow TX) | onset random runs
 16    13     2   1 |        0.62%        |     +0.64% / -0.67%         |  +0.86% / -1.02%
 16    13     2   2 |        1.25%        |     +1.27% / -1.27%         |  +1.27% / -1.92%
 16    11     4   4 |        2.50%        |     +2.57% / -2.46%         |  +2.57% / -3.38%
  8     6     1   1 |        1.25%        |     +1.28% / -1.27%         |  +1.28% / -1.88%
 10     7     2   2 |        2.00%        |     +2.05% / -1.99%         |  +2.05% / -2.84%
 25    16     8   4 |        1.60%        |     +1.64% / -1.60%         |  +2.10% / -2.30%
 25    16     8   1 |        0.40%        |     +0.42% / -0.43%         |  +0.66% / -0.67%
```

- 빠른 TX 쪽 onset 은 정확히 SJW/(10·NBT − SJW) 근처다 (예: 2/158 = 1.266% → 실측 1.27%). 식의 "10·NBT·δ ≤ SJW" 를 TX 비트 단위로 쓰면 1/(1+δ) 보정이 붙기 때문. 느린 쪽 첫 오류가 이론보다 약간 바깥(−1.27% vs 2/162 = 1.235%)인 것은 2000 비트라는 유한 길이 때문이다 — 느린 TX 에서는 샘플 포인트 앞 여유(PROP+PS1 ≈ 13 tq)가 커서 누적이 그만큼 쌓이기까지 오래 걸리고, 빠른 TX 는 여유가 PS2 = 2 tq 뿐이라 곧바로 깨진다.
- 랜덤 런(1–5 비트)이면 에지가 더 자주 와서 더 버틴다. 조건 2 는 **최악 패턴 기준**이다.
- 16 tq SJW 2 고정점: +1.26% 오류 0, +1.30% 75402 개, −1.26% 0, −1.30% 38020 개.

### Part C — BTR 레지스터와 Linux

```svg
<svg viewBox="0 0 760 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">STM32 bxCAN CAN_BTR (RM0008/RM0090 기준 — RM 확인 권장), 필드 = 실제 값 − 1</text>
  <g stroke="currentColor">
    <rect x="28" y="40" width="22" height="34" class="f-warn"/><rect x="50" y="40" width="22" height="34" class="f-warn"/>
    <rect x="72" y="40" width="88" height="34" class="f-mut"/><rect x="160" y="40" width="44" height="34" class="f-acc"/>
    <rect x="204" y="40" width="22" height="34" class="f-mut"/><rect x="226" y="40" width="66" height="34" class="f-ok"/>
    <rect x="292" y="40" width="88" height="34" class="f-acc"/><rect x="380" y="40" width="132" height="34" class="f-mut"/>
    <rect x="512" y="40" width="220" height="34" class="f-ok"/>
  </g>
  <g font-size="12" fill="currentColor">
    <text x="30" y="61">SI</text><text x="52" y="61">LB</text><text x="100" y="61">res</text><text x="168" y="61">SJW</text>
    <text x="207" y="61">r</text><text x="246" y="61">TS2</text><text x="322" y="61">TS1</text><text x="432" y="61">res</text>
    <text x="608" y="61">BRP</text>
    <text x="30" y="88">31</text><text x="52" y="88">30</text><text x="74" y="88">29:26</text><text x="162" y="88">25:24</text>
    <text x="206" y="88">23</text><text x="228" y="88">22:20</text><text x="294" y="88">19:16</text><text x="382" y="88">15:10</text>
    <text x="514" y="88">9:0</text>
  </g>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="10" y="116">16 MHz 500k: SJW 2, TS2 2, TS1 13, BRP 2</text>
    <text x="34" y="136">0</text><text x="56" y="136">0</text><text x="98" y="136">0000</text><text x="174" y="136">01</text>
    <text x="210" y="136">0</text><text x="248" y="136">001</text><text x="320" y="136">1100</text><text x="424" y="136">000000</text>
    <text x="580" y="136">0000000001</text>
    <text x="10" y="162">= 0x011C0001   (LBKM+SILM 자가 테스트 모드: 0xC11C0001)</text>
    <text x="10" y="184">42 MHz 500k: SJW 2, TS2 2, TS1 11, BRP 6 → 0x011A0005</text>
  </g>
</svg>
```

**인코드/디코드.** 필드는 "값 − 1" 이다. 디코드는 예약 비트가 1 이면 거부하고(덤프가 다른 레지스터일 수 있다), SJW > TS2 도 거부한다. 52만 4288 조합(BRP × TS1 × TS2 × SJW) 중 SJW ≤ TS2 인 425984 개는 왕복이 정확하고, 나머지는 모두 `BT_EARG` — 테스트 출력 `BTR encode/decode: 425984 valid round trips, 0 mismatches`. 디코드는 계산기 한계(NBT ≥ 8)가 아니라 레지스터가 표현하는 전부(`BXCAN_HW`, NBT ≥ 3)로 받는다: 덤프는 있는 그대로 읽어야 하니까.

**Linux.** SocketCAN(COMM-M09)에서 실제 컨트롤러(can0)는 `ip link` 로 비트 타이밍을 준다 (iproute2 문법 — `ip link help can` 으로 확인 권장):

```text
ip link set can0 type can bitrate 500000 sample-point 0.875
ip link set can0 type can tq 125 prop-seg 6 phase-seg1 7 phase-seg2 2 sjw 2
```

첫 줄은 커널이 **자기 알고리즘으로 다시 고른다**. 둘째 줄은 tq(ns)와 세그먼트를 직접 준다 — tq 가 정수 ns 일 때만 정확하다. 42 MHz 의 tq = 6/42 MHz = 142.86 ns, 36 MHz BRP 4 는 111.1 ns 라 `linux_ip_cmd` 는 첫 줄만 낸다. 커널 쪽 선택 규칙(연습 1에서 기억으로 모사): 목표 SP 를 안 주면 CiA 식 기본값(> 800k → 75%, > 500k → 80%, 그 외 87.5%), 목표 **이하**의 SP 만 고르고, 오차가 같으면 큰 NBT 를 먼저 찾은 것을 유지한다 (`drivers/net/can/dev/calc_bittiming.c` — 커널 버전별 확인 권장). 실제로 무엇이 잡혔는지는 `ip -details link show can0` 의 `tq ... prop-seg ... phase-seg1 ... sp ...` 줄로 확인한다.

**bring-up 요약** (solution.c 마지막 출력, 왕복 지연 600 ns 가정):

```text
node A (42 MHz APB1)     BRP    6  NBT 14 (1+11+2)  SJW 2  SP 85.7%  err +0 ppm
  BTR=0x011A0005  PROP 5 + PS1 6, PS2 2  df 5555 ppm (cond 1)
  ip link set can0 type can bitrate 500000 sample-point 0.857
node B (16 MHz)          BRP    2  NBT 16 (1+13+2)  SJW 2  SP 87.5%  err +0 ppm
  BTR=0x011C0001  PROP 5 + PS1 8, PS2 2  df 4854 ppm (cond 1)
  ip link set can0 type can tq 125 prop-seg 5 phase-seg1 8 phase-seg2 2 sjw 2
```

두 노드의 SP 가 85.7% / 87.5% 로 달라도 bitrate 가 같으면 classic CAN 은 통신한다 — 다만 네트워크의 여유는 가장 나쁜 노드가 정한다 (Q5).

**표 — `demo_table.c` 실제 출력.** f_can 7종 × bitrate 4종 × 목표 SP 2종, 규칙은 Part A 그대로, df 는 PROP = TSEG1 − TSEG2 (PS1 = PS2 가 되도록 PROP 를 최대로 줬을 때), t_rt max 는 그 PROP 가 덮는 왕복 지연. 87.5% 는 CANopen(CiA 301) 권장값, 80% 근처는 자동차(SAE J2284 류) 쪽에서 자주 보이는 값이다 (둘 다 문서 확인 권장).

| f_can | bitrate | BRP | NBT | TS1 | TS2 | SJW | SP | BTR | df (cond) | t_rt max |
|---|---|---|---|---|---|---|---|---|---|---|
| 8 MHz | 125k / 250k / 500k | 4 / 2 / 1 | 16 | 13 | 2 | 2 | 87.5% | 0x011C0003 / 01 / 00 | 0.485% (1) | 5500 / 2750 / 1375 ns |
| 8 MHz | 1000k | 1 | 8 | 6 | 1 | 1 | 87.5% | 0x00050000 | 0.485% (1) | 625 ns |
| 16 MHz | 125k / 250k / 500k / 1000k | 8 / 4 / 2 / 1 | 16 | 13 | 2 | 2 | 87.5% | 0x011C0007 / 03 / 01 / 00 | 0.485% (1) | 5500 / 2750 / 1375 / 688 ns |
| 36 MHz | 125k / 250k | 18 / 9 | 16 | 13 | 2 | 2 | 87.5% | 0x011C0011 / 08 | 0.485% (1) | 5500 / 2750 ns |
| 36 MHz | 500k | 9 | 8 | 6 | 1 | 1 | 87.5% | 0x00050008 | 0.485% (1) | 1250 ns |
| 36 MHz | 1000k | 2 | 18 | 15 | 2 | 2 | 88.9% | 0x011E0001 | 0.431% (1) | 722 ns |
| 42 MHz | 125k | 21 | 16 | 13 | 2 | 2 | 87.5% | 0x011C0014 | 0.485% (1) | 5500 ns |
| 42 MHz | 250k | 21 | 8 | 6 | 1 | 1 | 87.5% | 0x00050014 | 0.485% (1) | 2500 ns |
| 42 MHz | 500k / 1000k | 6 / 3 | 14 | 11 | 2 | 2 | 85.7% | 0x011A0005 / 02 | 0.555% (1) | 1286 / 643 ns |
| 45 MHz | 125k | 45 | 8 | 6 | 1 | 1 | 87.5% | 0x0005002C | 0.485% (1) | 5000 ns |
| 45 MHz | 250k / 500k / 1000k | 12 / 6 / 3 | 15 | 12 | 2 | 2 | 86.7% | 0x011B000B / 05 / 02 | 0.518% (1) | 2667 / 1333 / 667 ns |
| 48 MHz | 125k / 250k / 500k / 1000k | 24 / 12 / 6 / 3 | 16 | 13 | 2 | 2 | 87.5% | 0x011C0017 / 0B / 05 / 02 | 0.485% (1) | 5500 / 2750 / 1375 / 688 ns |
| 80 MHz | 125k / 250k / 500k / 1000k | 40 / 20 / 10 / 5 | 16 | 13 | 2 | 2 | 87.5% | 0x011C0027 / 13 / 09 / 04 | 0.485% (1) | 5500 / 2750 / 1375 / 688 ns |

(같은 NBT/TS 인 행을 합쳤다. BTR 의 "/ 03" 은 마지막 바이트만 바뀐 것 — BRP 필드.) 목표 80% 의 실제 출력:

| f_can | bitrate | BRP | NBT | TS1 | TS2 | SJW | SP | BTR | df (cond) |
|---|---|---|---|---|---|---|---|---|---|
| 8 / 16 / 36 / 48 MHz | 125k–500k (16 MHz·48 MHz 는 1M 포함) | f/(16·R) | 16 | 12 | 3 | 3 | 81.3% | 0x022B00xx | 0.732% (1) |
| 8 MHz | 1000k | 1 | 8 | 5 | 2 | 2 | 75.0% | 0x01140000 | 0.980% (1) |
| 36 MHz | 500k / 1000k | 4 / 2 | 18 | 13 | 4 | 4 | 77.8% | 0x033C0003 / 01 | 0.870% (1) |
| 42 MHz | 125k / 250k / 500k / 1000k | 16 / 8 / 4 / 2 | 21 | 16 | 4 | 4 | 81.0% | 0x033F000F / 07 / 03 / 01 | 0.743% (1) |
| 45 MHz | 125k / 250k | 18 / 9 | 20 | 15 | 4 | 4 | 80.0% | 0x033E0011 / 08 | 0.781% (1) |
| 45 MHz | 500k / 1000k | 6 / 3 | 15 | 11 | 3 | 3 | 80.0% | 0x022A0005 / 02 | 0.781% (1) |
| 80 MHz | 125k / 250k / 500k / 1000k | 32 / 16 / 8 / 4 | 20 | 15 | 4 | 4 | 80.0% | 0x033E001F / 0F / 07 / 03 | 0.781% (1) |

읽을 거리: (1) 87.5% 에서 df 는 거의 늘 0.485% 다 — 비율이 같으면 허용치가 같다. 80% 로 내리면 PS2 가 커져 0.73–0.98% 로 늘지만, PROP 자리(t_rt max)는 줄어든다(16 MHz 1M: 688 → 562 ns). 샘플 포인트는 **버스 길이와 클럭 허용치를 맞바꾸는 손잡이**다. (2) 36 MHz 1M 은 87.5% 를 정확히 못 내고 88.9% 다. (3) "표준" 값과의 비교: 16 MHz 500k = BRP 2, 16 tq, 87.5% 는 흔히 쓰이는 설정과 같다. 42/45 MHz 처럼 2^n 이 아닌 클럭은 도구마다 14 tq / 21 tq 등 다른 답을 낸다 — 규칙이 다르기 때문이고, vendor 계산기 표는 각자 규칙으로 확인해야 한다.

### 복잡도와 메모리

`bt_best` O(NBT 범위 × 4 × TSEG2 범위) ≈ 600 번, 스택에 `can_bt` 두 개. `bt_enumerate` O(BRP × NBT) 비교 + 통과한 것만 TSEG 루프, 출력은 호출자 버퍼. 둘 다 malloc 없음, 부동소수 없음 — FPU 없는 Cortex-M0 부트 코드에서도 그대로 돈다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution      # 72/72 passed, 약 0.1 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_san && ./sol_san   # 깨끗
cc -std=c11 -Wall -Wextra -O2 demo_table.c -o demo_table && ./demo_table        # 위 표
cc -std=c11 -Wall -Wextra -O2 demo_resync.c -o demo_resync && ./demo_resync     # onset 표 (약 10 초)
```

## 7. 자주 하는 실수

**1. BTR 필드에 실제 값을 그대로 쓴다.** BRP 2, TS1 13, TS2 2, SJW 2 를 −1 없이 넣으면 0x022D0002 → 하드웨어는 BRP 3, 1+14+3 = 18 tq 로 읽어 16 MHz 에서 **296296 bit/s (−40.74%)**, SP 83.3%. 고친 코드: `((b->tseg1 - 1) & 15u) << 16` 처럼 필드마다 −1 (bxcan_btr).

**2. f_can 을 잘못 안다.** 계산기는 맞는데 클럭 트리가 다르다. STM32F4 에서 APB1 = 42 MHz 로 계산한 BTR 0x011A0005 를 APB1 이 21 MHz 로 도는 보드에 넣으면 250k, 84 MHz 면 1M 이다. 공식은 맞고 입력이 틀렸다 — `HAL_RCC_GetPCLK1Freq()` 같은 실측값을 계산기 입력으로 쓰고 부트 로그에 찍는다.

**3. SP 를 부동소수 반올림으로 비교한다.** `fabs(sp - 0.875) < best` 를 float 로 하면 동점(12 tq 의 83.3 vs 91.7)이 반올림 순서로 갈린다. 정수 교차 곱 + 명시적 동점 규칙(규칙 5).

**4. 한계를 빼먹는다.** 42 MHz 500k 에서 21 tq 의 "이상적" 18/21 = 85.7% 은 TSEG1 = 17 이라 bxCAN 에서 표현이 안 된다. SJW > TSEG2(0x03000000 = SJW 4, TS2 1)도 거부해야 한다.

**5. PROP 를 무시한다.** "TS1 13 이면 충분" — 긴 버스에서 왕복 지연이 TS1 을 다 먹으면 아비트레이션이 깨진다 (PS1 = TSEG1 − PROP < 1 → `BT_EPROP`). 연습 5: 16 MHz 1M 87.5% 는 가정한 지연 모델로 약 38 m 에서 PS1 < PS2 가 된다.

**6. "오차 0 이면 끝" 이라고 생각한다.** 25 MHz 1M 은 오차 0 이지만 SP 68% 강제. 14.7456 MHz 는 −1.70% 라서 다른 노드와 섞이면 허용 오차 0.485% 를 이미 넘는다. 계산 오차는 발진기 ppm 과 **같은 예산**을 먹는다.

**7. Linux 에 bitrate 만 주고 확인하지 않는다.** 커널은 SP 를 목표 이하에서 고르고 기본값이 bitrate 에 따라 75/80/87.5% 로 달라진다. 연습 1의 모사에서 42 MHz 1M 기본값(75%)은 커널 71.4%, 이 레슨 76.2% 로 갈렸다. `ip -details link show can0` 로 실제 tq/세그먼트를 읽어 BTR 과 맞춰 본다.

## 8. 면접 Q&A

<details><summary>Q1. 16 MHz 클럭에서 500 kbit/s, 샘플 포인트 87.5% 로 설정해 보세요.</summary>

16e6 / 500e3 = 32 = BRP × NBT. NBT 8–25 에서 32 의 약수는 8, 16. 87.5% = 7/8 = 14/16 이라 둘 다 정확하다. tq 가 많은 16 을 고른다: BRP 2 → tq 125 ns, 1 + 13 + 2 → TSEG1 13, TSEG2 2, SP 14/16. SJW 는 TSEG2 이하에서 2. bxCAN BTR = SJW−1 << 24 | TS2−1 << 20 | TS1−1 << 16 | BRP−1 = 0x011C0001. 그다음 반드시 말할 것: PROP — 버스 왕복 지연이 700 ns 면 6 tq, PS1 7 이 남는다 — 와 허용 오차 0.485%(크리스털 필수), 그리고 스코프로 비트 폭 2.000 µs 를 확인.

</details>

<details><summary>Q2. 샘플 포인트를 왜 비트 뒤쪽(75–87.5%)에 두나요? 너무 뒤로 가면요?</summary>

앞쪽에는 PROP 가 있어야 한다: 아비트레이션과 ACK 에서 송신기는 가장 먼 노드의 우성 비트가 **되돌아온 뒤** 샘플해야 한다(왕복 지연). 뒤로 갈수록 긴 버스를 쓸 수 있다 (표: 87.5% 의 t_rt max 1375 ns vs 81.3% 의 1125 ns, 500k). 대신 PS2 가 줄어 SJW 상한과 조건 1 의 min(PS1, PS2)가 작아진다 — 87.5% 의 df 0.485% vs 80% 의 0.73–0.78%. 그래서 CANopen 은 87.5%, 일부 자동차 규격은 80% 근처처럼 용도마다 다르다 (규격 문서 확인). 극단적으로 25 MHz 1M 의 68% 는 PROP 가 좁아 짧은 버스에서만 된다.

</details>

<details><summary>Q3. 같은 bitrate 에서 tq 를 많이 쓰는 것과 적게 쓰는 것은 무엇이 다른가요?</summary>

같은 비율(SP)이면 허용 오차는 같다 — 16 tq 1+13+2 SJW 2 와 8 tq 1+6+1 SJW 1 둘 다 4854 ppm. 차이는 **해상도**다: tq 가 많으면 SP 를 6.25% 단위(16 tq)로 고를 수 있고 SJW 를 더 크게 줄 수 있으며, PROP 를 지연에 더 촘촘히 맞출 수 있다. 대가는 높은 f_can(16 tq × 1M = 16 MHz 이상)이다. 반대로 tq 가 너무 적으면(NBT < 8) 에지 위치를 1 tq 단위로만 알아 재동기화가 거칠어진다 — ISO 는 8–25 tq 를 지원 범위로 정한다(확인 권장).

</details>

<details><summary>Q4. SJW 는 무엇이고, 크게 하면 좋은 건가요?</summary>

재동기화 한 번에 PS1 을 늘리거나 PS2 를 줄일 수 있는 최대 tq 다. 조건 2(df ≤ SJW/(20·NBT))를 직접 키운다 — 이 레슨의 시뮬레이션에서 16 tq SJW 1 은 상대 편차 0.64% 에서, SJW 2 는 1.27% 에서 처음 깨졌다. 한계: SJW ≤ PS2(PS2 보다 많이 줄일 수 없다), 그리고 노이즈 에지(글리치) 하나가 위상을 SJW 만큼 잘못 끌고 갈 수 있다. 실무에서는 SJW = PS2 또는 그 근처를 많이 쓰고, Linux 의 기본값은 min(PS1, PS2/2) 계열이다(커널 버전별 확인 권장).

</details>

<details><summary>Q5. 노드마다 샘플 포인트가 달라도 되나요?</summary>

classic CAN 은 bitrate(정확히는 비트 시간)만 맞으면 통신한다 — 각 노드는 자기 SP 로 샘플하고 에지로 재동기화한다. 이 레슨의 bring-up 요약이 85.7% 와 87.5% 노드다. 하지만 네트워크 여유는 가장 나쁜 노드가 정한다: SP 가 이른 노드는 PROP 자리가 좁아 버스 길이를 제한하고, PS2 가 작은 노드는 발진기 허용치를 제한한다. CAN FD 의 데이터 위상(COMM-M08)은 TDC(transmitter delay compensation)와 함께 SP 를 네트워크 전체에서 맞추라는 권고가 있다(CiA 601 계열 — 확인 권장).

</details>

<details><summary>Q6. 크리스털 대신 내부 RC 발진기로 CAN 을 써도 되나요?</summary>

계산해 보면 거의 안 된다. 87.5% 표준 설정의 허용치는 노드당 0.485% 다. 연습 2: 16 MHz 500k 의 정확한 14개 조합 중 허용치 최대는 지연 0 일 때 1.5%(SP 50%, 8 tq), 왕복 지연 400 ns 만 넣어도 1.24%(SP 62.5%) — 비현실적인 SP 에서나 ±1% 근처가 된다. 공장 보정 HSI 가 상온 ±1% 수준이라도(데이터시트 확인) 온도·전압 전 범위와 상대 노드 오차까지 더하면 부족하다. 그래서 CAN 노드는 HSE 크리스털(±50 ppm 이하)을 쓰고, 계산기의 bitrate 오차도 같은 예산에서 빠진다. (Bosch 논문의 "최대 1.58%" 같은 숫자는 특정 조건의 이론 상한 — 확인 권장.)

</details>

<details><summary>Q7. 1 Mbit/s 버스에서 특정 노드만 가끔 error frame 을 냅니다. 어떻게 보시겠어요?</summary>

(1) 그 노드의 BTR 을 덤프해 디코드 — f_can 을 실측 클럭(APB 분주 포함)으로 넣어 실제 bitrate·SP 계산 (연습 3: 36 MHz 보드에 32 MHz 용 BTR 0x001C0003 → +12.5%). (2) 스코프/LA 로 그 노드가 보내는 비트 폭을 직접 잰다 (1 µs ± 0.5% 안인가). (3) 계산 오차 + 크리스털 ppm 이 df 안인가, SJW 가 1 로 남아 있지 않은가 (조건 2 반토막). (4) 버스 길이·스텁·종단(120 Ω × 2): 1M 에서 PROP 여유가 수백 ns 뿐이라 긴 스텁이 반사로 에지를 흐린다. (5) 에러 카운터(TEC/REC, COMM-M07)와 Linux `ip -details -statistics link show can0` 로 어느 쪽(TX/RX)에서 쌓이는지 본다.

</details>

<details><summary>Q8. Linux 에서 `bitrate 500000` 만 주면 커널은 어떻게 고르나요? 왜 tq 형식을 쓰나요?</summary>

`drivers/net/can/dev/calc_bittiming.c` 의 알고리즘(기억 기반 요약): 목표 SP 가 없으면 CiA 식 기본값(> 800k 75%, > 500k 80%, 그 외 87.5%), 가장 큰 TSEG 부터 내려가며 BRP 를 내림/올림으로 맞춰 bitrate 오차가 최소인 것, 그중 SP 가 목표 **이하**이면서 가장 가까운 것. 오차가 0.1% 단위로 한도(5% 로 기억 — 확인 권장)를 넘으면 거부한다. 이 레슨 규칙과 대부분 같지만(연습 1: 28개 중 27개), 목표보다 높은 SP 는 고르지 않는다는 점이 다르다. 결정론적으로 같은 BTR 을 원하면 `tq ... prop-seg ... phase-seg1 ... phase-seg2 ... sjw` 로 직접 주고(정수 ns 일 때), 어느 쪽이든 `ip -details link show` 로 확인한다.

</details>

## 9. 추가 연습

### 연습 1 — Linux 식 선택과 비교

커널의 `can_calc_bittiming` 을 (기억으로) 모사한 `linux_like()` 를 짜서, 7 클럭 × 4 bitrate 를 CiA 기본 SP 로 고르게 하고 이 레슨의 `bt_best` 와 비교하라. 또 우리가 내는 명령 줄 1 (`sample-point` = 우리 SP)을 커널이 받으면 같은 조합이 나오는지 확인하라.

<details><summary>답 보기</summary>

tseg = (16+8)·2+1 부터 내려가며 NBT = 1 + tseg/2, BRP = f/(NBT·R) + (tseg 홀수 ? 1 : 0) — 같은 NBT 로 내림/올림을 한 번씩. SP 후보는 TSEG2 = NBT − ⌊SP·NBT/1000⌋ 와 그 −1, 목표 이하 중 가까운 것. 결과(practice.c 실제 출력): **28개 중 27개 일치**, 다른 하나는 `42 MHz 1000k SP 750: linux BRP 2 NBT 21 (1+14+6) SP 71.4% | ours BRP 2 NBT 21 (1+15+5) SP 76.2%` — 커널은 75% 를 넘지 못하고, 우리는 1.2% 위를 고른다. 명령 줄 1 은 **28/28** 에서 커널이 우리 조합을 재현한다 (우리 SP 를 목표로 주면 "목표 이하에서 가장 가까운 것" = 우리 것). 커널 모사는 기억 기반이므로 실제 커널에서 `ip -details` 로 확인할 것.

</details>

### 연습 2 — 내부 RC 발진기로 CAN?

16 MHz, 500k 의 정확한(오차 0) 조합 14개 중 허용 오차 df 가 가장 큰 것을 찾아라. 왕복 지연 0 ns 와 400 ns 두 경우.

<details><summary>답 보기</summary>

`bt_enumerate(…, 0, …)` → 각 조합에 PROP = ⌈t_rt/tq⌉ 로 `bt_tolerance`. 지연 0: **1.500%** (BRP 4, 8 tq, 1+3+4, SJW 4, SP 50%). 지연 400 ns: **1.2376%** (BRP 2, 16 tq, 1+9+6, SJW 4, SP 62.5%). 즉 ±1% RC 를 견디려면 SP 를 50–62% 로 내려야 하고, 그러면 PROP 자리가 없어 버스가 짧아진다. 표준 87.5% 의 0.485% 와 비교하면 크리스털이 왜 필수인지 보인다.

</details>

### 연습 3 — BTR 덤프 감식

36 MHz(STM32F1 APB1) 보드가 500k 버스에서 아무와도 통신이 안 된다. BTR 덤프는 0x001C0003. 실제 bitrate, 이 값이 원래 어떤 클럭용이었는지, 고친 BTR 을 구하라.

<details><summary>답 보기</summary>

디코드: BRP 4, TS1 13, TS2 2, SJW 1 → 16 tq, 36e6/64 = 562.5k, **+125000 ppm (+12.5%)**. 500k 가 되는 클럭 = 500k × 4 × 16 = **32 MHz** — 다른 보드의 설정을 복사한 것. 36 MHz 용 최선(87.5%)은 BRP 9, 8 tq, 1+6+1 → **0x00050008**. 덤프 디코드가 bring-up 의 첫 단계인 이유: 계산식은 맞는데 입력 클럭이 틀린 경우가 제일 흔하다.

</details>

### 연습 4 — 모든 표준 bitrate 를 정확히 내는 클럭

8–80 MHz 정수 MHz 중 125k/250k/500k/1M 넷 모두를 오차 0, SP 정확히 87.5% 로 내는 f_can 을 찾아라. 규칙이 보이는가?

<details><summary>답 보기</summary>

`bt_best(f, R, 875, &BXCAN, 0, …)` 가 넷 다 OK 이고 8·(1+TSEG1) == 7·NBT 인 f: **8 16 24 32 40 48 56 64 72 80 MHz (10개) — 8 MHz 의 배수**. 이유: 87.5% = 7/8 이라 NBT 는 8 의 배수여야 하고, 24 tq 는 TSEG1 = 20 > 16 이라 불가 → NBT ∈ {8, 16}. 1M 에서 f/1M = BRP·NBT 가 8 의 배수 → f 가 8 MHz 의 배수. 낮은 bitrate 는 BRP 가 커서 자동으로 맞는다. 그래서 CAN 보드는 8/16 MHz 크리스털이나 그 배수 PLL 을 좋아한다 — UART 의 14.7456 MHz 와 정반대 (COMM-B03 연습 4).

</details>

### 연습 5 — 버스 길이 예산

16 MHz, 87.5% 설정에서 왕복 지연 t_rt = 2·(L × 5 ns/m) + 300 ns (두 노드의 트랜시버·컨트롤러 지연 합 — **가정값**, 실제는 트랜시버 데이터시트의 loop delay 로)일 때, PS1 ≥ PS2 (허용치 유지)와 PS1 ≥ 1 (최소 동작)을 만족하는 최대 L 을 bitrate 별로 구하라.

<details><summary>답 보기</summary>

PROP = ⌈t_rt/tq⌉, PS1 = 13 − PROP. 실제 출력:

| bitrate | tq | PS1 ≥ PS2 까지 | PS1 ≥ 1 까지 |
|---|---|---|---|
| 1000k | 62.5 ns | 38 m | 45 m |
| 500k | 125 ns | 107 m | 120 m |
| 250k | 250 ns | 245 m | 270 m |
| 125k | 500 ns | 520 m | 570 m |

흔히 인용되는 "1M 40 m / 500k 100 m / 250k 250 m / 125k 500 m" 와 비슷한 크기가 이 단순 모델에서 나온다 (그 표의 출처·가정은 확인 권장). bitrate 를 반으로 줄이면 tq 가 두 배라 PROP 가 덮는 지연도 두 배 — 길이 × bitrate ≈ 일정.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 16/16 passed
```

## 10. 핵심 정리

- tq = BRP/f_can, NBT = 1 + TSEG1 + TSEG2, bitrate = f_can/(BRP·NBT), SP = (1+TSEG1)/NBT. 오차 0 ⇔ f_can/R = BRP·NBT 가 정수.
- 선택 규칙을 말로 정하고 정수로 구현: 오차 → |SP−목표| → tq 많이 → BRP 작게 → 낮은 SP. NBT 마다 BRP 는 floor/ceil 두 개만 보면 된다.
- 16 MHz 500k 87.5% = BRP 2, 1+13+2, SJW 2, BTR 0x011C0001. BTR 필드는 **값 − 1**.
- 안 되는 클럭: 25 MHz 1M(68% 강제), 42 MHz 의 21 tq(TSEG1 > 16), 14.7456 MHz 500k(−1.70%). 87.5% 를 모든 표준 bitrate 에서 정확히 → 8 MHz 의 배수.
- PROP ≥ 왕복 지연(⌈t_rt/tq⌉), PS1 = TSEG1 − PROP ≥ 1. 샘플 포인트는 버스 길이 ↔ 클럭 허용치를 맞바꾸는 손잡이.
- df ≤ min(PS1,PS2)/(2(13·NBT−PS2)) 와 SJW/(20·NBT). 87.5% 표준 설정은 노드당 약 0.49% → 크리스털 필수. 같은 비율이면 tq 수와 무관.
- 재동기화: 에지가 샘플 전이면 PS1 += min(e, SJW), 샘플 뒤면 PS2 −= min(|e|, SJW). 시뮬레이션 onset 이 SJW/(10·NBT) 와 0.07% 안에서 맞았다.
- Linux: `bitrate + sample-point` 는 커널이 다시 고른다(목표 이하 SP), `tq + segs` 는 정확 지정. 항상 `ip -details link show can0` 로 확인.

## 참고자료

- Robert Bosch GmbH, *CAN Specification 2.0* (1991) — Bit Timing Requirements: SYNC_SEG / PROP_SEG / PHASE_SEG1/2, hard sync / resynchronization, SJW.
- ISO 11898-1:2015 *Road vehicles — Controller area network (CAN) — Part 1* — 비트 타이밍, 동기화 규칙, 8–25 tq (조항 번호·FD 쪽 추가 조건은 문서 확인 권장).
- Florian Hartwig, Armin Bassemir, "The Configuration of the CAN Bit Timing", 6th iCC 1999 (Bosch) — 발진기 허용 오차 두 조건, PROP 계산, 최대 허용치 논의 (식은 기억에서 옮김 — 확인 권장).
- STMicroelectronics RM0008 (STM32F1) / RM0090 (STM32F4) — bxCAN "Bit timing" 과 CAN_BTR 레지스터 (SILM, LBKM, SJW, TS2, TS1, BRP 위치는 기억 — 확인 권장). FDCAN(G4/H7)은 NBTP 레지스터로 범위가 다르다.
- CiA 301 (CANopen application layer) — 권장 bit timing 표와 87.5% 샘플 포인트 (표 내용 확인 권장). SAE J2284 — 자동차 500k 네트워크 비트 타이밍 (확인 권장).
- Linux kernel `drivers/net/can/dev/calc_bittiming.c` (`can_calc_bittiming`, `can_update_sample_point`), `Documentation/networking/can.rst`, iproute2 `ip-link(8)` 의 `type can` 옵션 — https://www.kernel.org/doc/html/latest/networking/can.html
- 관련 레슨: COMM-B03 (UART baud 오차 — 같은 문제를 start 에지 한 번으로 푸는 방법), COMM-M04 (스터핑·CRC: 에지가 10 비트마다 오는 이유), COMM-M05 (중재: PROP 가 필요한 이유), COMM-M07 (에러 카운터·bus-off), COMM-M08 (CAN FD 데이터 위상 타이밍), COMM-M09 (SocketCAN, `ip link`), HWLAB-M05 (LA 로 비트 폭 측정).
