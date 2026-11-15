---
id: ARCH-B13
time: 60분
updated: 2026-11-11
---
# ARCH-B13 타이밍 기초: setup/hold와 최대 클럭 주파수

## 학습 목표

- 플롭 → 조합 논리 → 플롭 경로 하나에서 **setup slack** 과 **hold slack** 식을 그림으로 유도하고, 어떤 지연(최대/최소)이 어느 식에 들어가는지 틀리지 않고 말할 수 있다.
- **Fmax = 1 / Tmin** 을 계산하고, 주파수·주기를 정수로 바꿀 때 **비관적인 방향으로 반올림** 해야 하는 이유를 안다.
- 클럭 스큐(skew)의 부호 규약을 정하고, **스큐가 setup 은 돕고 hold 는 해친다** 는 것을 스윕 테스트로 보인다. "hold 위반은 클럭을 낮춰도 안 고쳐진다" 를 식으로 설명한다.
- 여러 경로 표에서 critical path, WNS/TNS 를 뽑는 작은 타이밍 리포트를 만들고, 버퍼 삽입으로 hold 를 고칠 때 setup 이 깨질 수 있음을 확인한다.
- 실리콘·펌웨어 쪽 연결: 데이터시트의 최대 주파수, 플래시 wait state, 전압-주파수 shmoo, 비동기 입력의 동기화기(synchronizer).

## 1. 문제

"setup slack 이 음수면 어떻게 하나요? 클럭을 낮추면 hold 위반도 고쳐지나요?" — 실리콘 검증·SoC·FPGA 면접에서 첫 10분 안에 나오는 질문이고, 펌웨어 엔지니어에게도 "왜 이 MCU는 168 MHz 이상 못 올리나", "왜 오버클럭하면 가끔만 죽나" 의 답이 된다.

동기식(synchronous) 회로는 **플립플롭(flip-flop, 이하 플롭) → 조합 논리(combinational logic) → 플롭** 의 반복이다. 클럭 에지에서 앞 플롭(launch)이 새 값을 내보내면, 그 값은 논리를 지나 다음 클럭 에지 전에 뒤 플롭(capture)에 도착해야 한다. 조건은 딱 두 개다.

- **setup**: 새 데이터가 *다음* capture 에지보다 `tsu` 만큼 **먼저** 도착해야 한다 → 가장 **느린** 경로가 문제.
- **hold**: 새 데이터가 *같은* capture 에지 후 `th` 동안은 **아직 오면 안 된다** (지금 잡고 있는 옛 값을 덮으면 안 됨) → 가장 **빠른** 경로가 문제.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b13-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">경로 = launch 플롭 → 조합 논리 → capture 플롭 (클럭은 각자 다른 시각에 도착)</text>
  <rect x="150" y="50" width="100" height="90" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="200" y="80" fill="currentColor" text-anchor="middle" font-weight="bold">FF1</text>
  <text x="200" y="100" fill="currentColor" text-anchor="middle" font-size="12">launch</text>
  <text x="200" y="120" fill="currentColor" text-anchor="middle" font-size="12">tpcq / tccq</text>
  <rect x="310" y="60" width="170" height="70" rx="30" class="f-mut" stroke="currentColor"/>
  <text x="395" y="88" fill="currentColor" text-anchor="middle" font-weight="bold">조합 논리</text>
  <text x="395" y="110" fill="currentColor" text-anchor="middle" font-size="12">tpd (max) / tcd (min)</text>
  <rect x="540" y="50" width="100" height="90" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="590" y="80" fill="currentColor" text-anchor="middle" font-weight="bold">FF2</text>
  <text x="590" y="100" fill="currentColor" text-anchor="middle" font-size="12">capture</text>
  <text x="590" y="120" fill="currentColor" text-anchor="middle" font-size="12">tsu / th</text>
  <g stroke="currentColor" fill="none">
    <path d="M100 95 L150 95" marker-end="url(#b13-arr)"/>
    <path d="M250 95 L310 95" marker-end="url(#b13-arr)"/>
    <path d="M480 95 L540 95" marker-end="url(#b13-arr)"/>
    <path d="M640 95 L690 95" marker-end="url(#b13-arr)"/>
    <path d="M30 225 L590 225"/>
    <path d="M200 225 L200 140" marker-end="url(#b13-arr)"/>
    <path d="M590 225 L590 140" marker-end="url(#b13-arr)"/>
    <path d="M190 132 L200 122 L210 132"/>
    <path d="M580 132 L590 122 L600 132"/>
  </g>
  <text x="110" y="88" fill="currentColor" font-size="12">D</text>
  <text x="258" y="88" fill="currentColor" font-size="12">Q</text>
  <text x="652" y="88" fill="currentColor" font-size="12">Q</text>
  <polygon points="120,215 140,225 120,235" class="f-warn" stroke="currentColor"/>
  <polygon points="380,215 400,225 380,235" class="f-warn" stroke="currentColor"/>
  <text x="30" y="215" fill="currentColor" font-size="12">CLK 원천</text>
  <text x="210" y="190" fill="currentColor" font-size="12">도착 a_L</text>
  <text x="600" y="190" fill="currentColor" font-size="12">도착 a_C (버퍼 더 많음)</text>
  <text x="300" y="250" fill="currentColor" font-size="12">클럭 트리 버퍼 (BM-B10 은 이 클럭을 만드는 PLL/분주기 쪽)</text>
  <text x="10" y="280" fill="currentColor" font-weight="bold">skew = a_C − a_L  (capture − launch).  skew &gt; 0 → capture 클럭이 늦게 온다 → setup 여유 ↑, hold 여유 ↓</text>
</svg>
```

이 레슨의 기호 (모두 **정수 피코초 ps**, 주파수는 **정수 kHz**; 1 ps 주기 = 1e9 kHz):

| 기호 | 뜻 | 어느 검사에 쓰나 |
|---|---|---|
| `tpcq` | clk→Q **최대** 지연 (propagation) | setup |
| `tccq` | clk→Q **최소** 지연 (contamination) | hold |
| `tpd` | 조합 논리 **최대** 지연 (가장 긴 경로) | setup |
| `tcd` | 조합 논리 **최소** 지연 (가장 짧은 경로) | hold |
| `tsu`, `th` | capture 플롭의 setup / hold 요구 시간 | 각각 |
| `skew` | capture 클럭 도착 − launch 클럭 도착 | 둘 다 (부호 반대) |

(이름은 Harris & Harris *Digital Design and Computer Architecture* 의 표기를 따랐다.)

### Part A — slack 과 Fmax

`ff_path` 하나를 받아 계산하는 함수 4개를 쓴다.

- `setup_slack_ps(p, tclk)` = `Tclk + skew − (tpcq + tpd + tsu)`
- `hold_slack_ps(p)` = `tccq + tcd − th − skew` — **Tclk 인자가 없다**는 게 핵심
- `min_period_ps(p)` = setup slack 이 0 이 되는 Tclk
- `fmax_khz(p)` = `1e9 / Tmin` **내림**. Tmin ≤ 0 이면 0 ("이 경로는 주파수를 제한하지 않음")

기준 경로 P = {tpcq 100, tccq 60, tpd 1500, tcd 200, tsu 80, th 50, skew 0} 에서 Tmin = 1680 ps → 595.238 MHz. skew +150 이면 setup +150, hold −150 이 되는 것을 테스트로 확인한다.

### Part B — 면접관이 파고드는 경계

- `period_ps_from_khz(khz)` — 주파수 설정값을 setup 검사용 주기로 (어느 쪽으로 반올림?).
- `skew_window(p, tclk, &lo, &hi)` — 주어진 Tclk 에서 setup·hold 를 둘 다 만족하는 skew 구간. 비어 있으면 false.
- `hold_fix_buffers(p, buf)` — hold 를 고치려면 `buf` ps 짜리 지연 버퍼가 몇 개 필요한가 (올림 나눗셈).
- 테스트: 시프트 레지스터(플롭 직결)에서 skew 30 ps 가 hold 를 깨고, **주파수를 1 MHz 까지 낮춰도** 통과하지 못함 / 짧은 경로와 긴 경로가 같은 배선이면 hold 버퍼가 setup 을 깸 / skew 스윕에서 setup 은 정확히 +Δ, hold 는 정확히 −Δ / 결정적 랜덤 경로 2000개로 항등식 검증.

### Part C — 여러 경로 타이밍 리포트

레지스터마다 클럭 도착 시각(클럭 트리 삽입 지연)이 다르고, 경로 표 `{launch, capture, tpd, tcd}` 가 있다. `make_path` 로 skew 를 레지스터 도착 시각 차이로 만들고, `analyze` 로 STA 도구가 내는 요약(worst setup/hold 경로, **WNS**, **TNS**, 위반 수, 설계 전체 Tmin/Fmax)을 낸다. 500 / 540 / 588 MHz 에서 결과가 어떻게 바뀌는지, hold 위반은 왜 그대로인지 본다.

## 2. Problem (English)

> **Static timing of a register-to-register path.**
>
> All times are integer picoseconds. A path has launch clock-to-Q delays `tpcq` (max) and `tccq` (min), combinational delays `tpd` (max) and `tcd` (min), capture-flop `tsu` and `th`, and `skew = capture clock arrival − launch clock arrival`.
>
> 1. Implement `setup_slack_ps(p, tclk)`, `hold_slack_ps(p)`, `min_period_ps(p)` and `fmax_khz(p)` (floor of 1e9 / Tmin; 0 if Tmin ≤ 0).
> 2. Implement `period_ps_from_khz(khz)` (rounded so that a setup check is never optimistic), `skew_window(p, tclk, &lo, &hi)` returning the skew range for which both checks pass, and `hold_fix_buffers(p, buf_ps)` returning the minimum number of delay buffers that fixes hold (0 if met, −1 if `buf_ps ≤ 0`).
> 3. Given per-register clock arrival times and a list of paths, implement `make_path` and `analyze(..., tclk, &report)` producing worst setup/hold path index, WNS, TNS (sum of negative slacks), violation counts, and design-level Tmin / Fmax.
>
> Show with tests that positive skew helps setup and hurts hold, and that a hold violation cannot be fixed by lowering the clock frequency.
>
> Example: `{tpcq 100, tccq 60, tpd 1500, tcd 200, tsu 80, th 50, skew 0}` at 2000 ps → setup slack 320, hold slack 210, Tmin 1680, Fmax 595238 kHz.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨진 경우
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄이 쭉 나오고 "3/38 passed" — TODO 를 채워 38/38 을 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — slack = required − arrival</summary>

STA 도구가 보여주는 것도 결국 두 숫자의 차다. setup: 데이터 도착(arrival) = `a_L + tpcq + tpd`, 요구(required) = `Tclk + a_C − tsu`. 빼면 `a_L, a_C` 가 `skew = a_C − a_L` 로 묶인다. 양수면 여유, 음수면 위반.

</details>

<details>
<summary>힌트 2 — hold 는 "같은 에지" 끼리 비교한다</summary>

hold 는 launch 에지 0 에서 출발한 **가장 빠른** 새 데이터(`a_L + tccq + tcd`)가, **같은 시각의** capture 에지 + th (`a_C + th`) 보다 늦게 와야 한다. 두 에지가 같은 에지라서 Tclk 가 식에 들어갈 자리가 없다.

</details>

<details>
<summary>힌트 3 — 정수 반올림은 "비관적인 쪽"</summary>

Fmax 를 올림하면 실제 주기가 Tmin 보다 아주 조금 짧아진다 → setup 위반. 그래서 주파수는 **내림**. 주파수 설정값에서 주기를 구해 setup 을 검사할 땐, 실제 주기보다 길게 잡으면 낙관적 → 주기도 **내림**.

</details>

<details>
<summary>힌트 4 — skew window 는 식 두 개를 skew 에 대해 푼 것</summary>

setup ≥ 0 ⇔ `skew ≥ tpcq + tpd + tsu − Tclk`, hold ≥ 0 ⇔ `skew ≤ tccq + tcd − th`. 하한 ≤ 상한이 아니면 그 Tclk 에선 어떤 skew 로도 못 맞춘다.

</details>

<details>
<summary>힌트 5 — 리포트의 WNS 와 TNS</summary>

WNS(worst negative slack)는 "가장 작은 slack" — 다 통과하면 양수일 수 있다(도구에 따라 0 으로 표시하기도 한다; 여기선 실제 최솟값). TNS 는 **음수 slack 만** 더한 값이다. Tmin 은 경로들 Tmin 의 최댓값.

</details>

## 5. 풀이

### Part A — 타이밍도에서 식 두 개 유도

기준 경로 P 에 skew +150 ps 를 준 경우를 시간축(1 px = 4 ps)에 그리면 이렇다. launch 에지가 0, capture 에지는 150 과 2150.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b13-arr2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">P, skew = +150 ps, Tclk = 2000 ps  (1 px = 4 ps)</text>
  <text x="10" y="64" fill="currentColor">CLK launch</text>
  <path d="M90 75 L110 75 L110 45 L360 45 L360 75 L610 75 L610 45 L720 45" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <text x="10" y="119" fill="currentColor">CLK capture</text>
  <path d="M90 130 L147 130 L147 100 L397 100 L397 130 L647 130 L647 100 L720 100" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <text x="10" y="179" fill="currentColor">FF2.D</text>
  <rect x="90" y="160" width="85" height="30" class="f-ok" stroke="currentColor"/>
  <text x="96" y="180" fill="currentColor" font-size="12">옛 값</text>
  <rect x="175" y="160" width="335" height="30" class="f-warn" stroke="currentColor"/>
  <text x="250" y="180" fill="currentColor" font-size="12">바뀌는 중 (경로마다 다른 시각)</text>
  <rect x="510" y="160" width="210" height="30" class="f-ok" stroke="currentColor"/>
  <text x="560" y="180" fill="currentColor" font-size="12">새 값 (안정)</text>
  <rect x="147" y="150" width="13" height="50" class="f-acc" opacity="0.6"/>
  <rect x="627" y="150" width="20" height="50" class="f-acc" opacity="0.6"/>
  <g stroke="currentColor" fill="none" stroke-dasharray="4 3">
    <path d="M110 75 L110 300"/><path d="M147 130 L147 300"/><path d="M647 130 L647 300"/>
    <path d="M175 190 L175 260"/><path d="M510 190 L510 260"/><path d="M627 200 L627 260"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="114" y="316">0</text><text x="140" y="316">150</text><text x="630" y="316">2150</text>
    <text x="160" y="214">th</text><text x="604" y="214">tsu</text>
  </g>
  <path d="M110 230 L175 230" stroke="currentColor" marker-end="url(#b13-arr2)"/>
  <text x="180" y="234" fill="currentColor" font-size="12">tccq+tcd = 260 (가장 빠른 새 데이터)</text>
  <path d="M110 250 L510 250" stroke="currentColor" marker-end="url(#b13-arr2)"/>
  <text x="200" y="246" fill="currentColor" font-size="12">tpcq+tpd = 1600 (가장 늦은 새 데이터)</text>
  <path d="M510 272 L627 272" stroke="currentColor" marker-start="url(#b13-arr2)" marker-end="url(#b13-arr2)"/>
  <text x="520" y="290" fill="currentColor" font-size="12" font-weight="bold">setup slack 470</text>
  <text x="200" y="290" fill="currentColor" font-size="12" font-weight="bold">hold slack = 260 − (150 + 50) = 60</text>
</svg>
```

- **setup**: 가장 늦은 새 데이터 도착 `0 + tpcq + tpd = 1600`. 요구: 다음 capture 에지 `Tclk + skew = 2150` 에서 tsu 앞 = `2070`. slack = 2070 − 1600 = **470**. 일반식: `Tclk + skew − tpcq − tpd − tsu`.
- **hold**: 가장 빠른 새 데이터 도착 `tccq + tcd = 260`. 요구: 같은 capture 에지 `skew = 150` 에서 th 뒤 = `200`. slack = **60**. 일반식: `tccq + tcd − th − skew`.
- Tmin 은 setup slack 을 0 으로 만드는 Tclk: `tpcq + tpd + tsu − skew` = 1530 → `1e9 / 1530 = 653594.77` → **653594 kHz**.

hold 그림에 Tclk 가 없는 이유가 보인다: hold 는 **같은 에지** 에서 출발한 새 데이터가 **같은 에지** 에서 잡히는 옛 데이터를 덮는지의 문제다. 다음 에지까지의 거리(Tclk)는 상관이 없다.

| skew | setup slack @2000 | hold slack | Tmin | Fmax (kHz, 내림) |
|---:|---:|---:|---:|---:|
| −100 | 220 | 310 | 1780 | 561797 |
| 0 | 320 | 210 | 1680 | 595238 |
| +150 | 470 | 60 | 1530 | 653594 |
| +250 | 570 | **−40** | 1430 | (의미 없음 — hold 위반) |

### Part B — skew 창, 반올림, 고칠 수 없는 hold

**skew 창.** 두 식을 skew 에 대해 풀면 `lo = tpcq + tpd + tsu − Tclk ≤ skew ≤ tccq + tcd − th = hi`. P, Tclk 2000 에서 `[−320, 210]`. 상한 `hi` 에는 Tclk 가 없으므로, 클럭을 늦추면 **하한만** 내려가고 창은 왼쪽으로만 넓어진다. Tclk 1400 이면 lo = 280 > hi = 210 → 빈 창 (`skew_window` false).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">경로 P, Tclk 2000 ps — skew 를 옮기면 두 slack 이 반대로 움직인다 (기울기 +1 / −1)</text>
  <rect x="144" y="35" width="424" height="225" class="f-ok" opacity="0.35"/>
  <text x="250" y="52" fill="currentColor" font-size="12">둘 다 만족: skew ∈ [−320, 210]</text>
  <path d="M60 230 L660 230" stroke="currentColor"/>
  <path d="M400 35 L400 262" stroke="currentColor" stroke-dasharray="3 3" opacity="0.6"/>
  <path d="M80 254 L640 44" stroke="currentColor" stroke-width="2.5"/>
  <path d="M80 47 L640 257" stroke="currentColor" stroke-width="2.5" stroke-dasharray="8 4"/>
  <text x="520" y="40" fill="currentColor">setup slack (실선)</text>
  <text x="90" y="120" fill="currentColor">hold slack (점선)</text>
  <circle cx="356" cy="151" r="4" class="f-acc" stroke="currentColor"/>
  <text x="300" y="72" fill="currentColor" font-size="12">● 교차: skew −55 에서 둘 다 265</text>
  <rect x="80" y="35" width="64" height="225" class="f-warn" opacity="0.3"/>
  <rect x="568" y="35" width="72" height="225" class="f-warn" opacity="0.3"/>
  <text x="84" y="222" fill="currentColor" font-size="12">setup 위반</text>
  <text x="572" y="222" fill="currentColor" font-size="12">hold 위반</text>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="80" y="280">−400</text><text x="240" y="280">−200</text><text x="400" y="280">0</text><text x="560" y="280">+200</text>
    <text x="144" y="296">−320</text><text x="568" y="296">+210</text>
  </g>
  <text x="664" y="234" fill="currentColor" font-size="12">0</text>
  <text x="664" y="280" fill="currentColor" font-size="12">skew</text>
</svg>
```

실제로 돌린 스윕 (`demo_sweep.c`, Apple M6 / Apple clang 21.0.0):

```text
(1) skew sweep, Tclk = 2000 ps (500 MHz), path P: tpcq 100 tccq 60 tpd 1500 tcd 200 tsu 80 th 50
  skew | setup slack | hold slack |  Tmin |  Fmax MHz | verdict
  -400 |         -80 |        610 |  2080 |   480.769 | SETUP VIOLATED
  -350 |         -30 |        560 |  2030 |   492.610 | SETUP VIOLATED
  -300 |          20 |        510 |  1980 |   505.050 | met
  ...
     0 |         320 |        210 |  1680 |   595.238 | met
   150 |         470 |         60 |  1530 |   653.594 | met
   200 |         520 |         10 |  1480 |   675.675 | met
   250 |         570 |        -40 |  1430 |   699.300 | HOLD VIOLATED
   300 |         620 |        -90 |  1380 |   724.637 | HOLD VIOLATED
```

`solution.c` 의 체크는 이 관계를 정확한 등식으로 고정한다: skew 를 10 ps 올릴 때마다 setup 은 정확히 +10, hold 는 정확히 −10 (−400…+400 전 구간).

**반올림 방향.** Tmin 1680 → `1e9/1680 = 595238.095` kHz.

| 변환 | 올바른 방향 | 이유 | 예 |
|---|---|---|---|
| Tmin → Fmax | **내림** | 올리면 실제 주기 < Tmin | 595238 (595239 면 주기 1679.997 ps) |
| 설정 주파수 → 검사용 주기 | **내림** | 길게 잡으면 setup 을 낙관적으로 통과시킴 | 595239 kHz → 1679 ps → slack −1 |

내림-내림이면 `floor(1e9 / floor(1e9/T)) ≥ T` 가 항상 성립하므로(정수 T) "Fmax 로 돌리면 slack ≥ 0, 1 kHz 만 올려도 < 0" 이 랜덤 2000개 경로에서 성립한다 — 이게 테스트다.

**고칠 수 없는 hold.** 시프트 레지스터처럼 플롭 Q 가 다음 플롭 D 로 직결되면 `tpd = tcd ≈ 0`, hold slack = `tccq − th − skew` = 60 − 50 − 30 = **−20**. Tclk 를 200 ps 부터 1 μs 까지 두 배씩 늘려도 `path_ok` 는 한 번도 참이 안 된다. 실리콘에서 hold 위반이 무서운 이유: **주파수를 낮춰서 살릴 수 없다** (setup 위반은 "느리게 돌리면 동작" 이라는 탈출구가 있다).

고치는 법은 짧은 경로에 지연을 넣는 것 — `ceil(20 / 15) = 2` 개의 15 ps 버퍼 → hold +10. 그런데 짧은 경로와 긴 경로가 **같은 배선** 이면 버퍼는 tpd 에도 더해진다. 테스트의 `tight` 경로(tpd = tcd = 300, skew 400)는 hold −90 을 40 ps 버퍼 3개로 +30 으로 만들지만, Tclk 100 에서 setup 이 +20 → **−100** 으로 깨진다. 실제 도구가 hold 수정 버퍼를 setup 여유가 있는 지점(가지가 갈린 짧은 쪽)에만 넣는 이유다.

**스큐가 큰 경우.** skew 2000 이면 Tmin = −320 — 식은 "주기 0 이하도 OK" 라고 말하지만 의미 없는 영역이다. `fmax_khz` 는 0 을 돌려 "이 경로는 주파수를 제한하지 않음" 으로 처리했다 (실제로는 그 전에 hold 가 깨진다: 이 경로 hold = −1790).

### Part C — 리포트: arrival / required / slack

`make_path` 는 skew 를 `regs[capture].clk_arrival − regs[launch].clk_arrival` 로 만든다. 같은 두 레지스터라도 **방향에 따라 부호가 반대** 다: `opa(450) → acc(600)` 는 +150, `acc → opa` 는 −150. 한쪽 경로에 좋은 skew 는 반대쪽 경로엔 나쁘다.

`demo_sweep.c` 의 두 번째 출력은 상용·오픈소스 STA 리포트가 보여주는 **모양** (경로별 data arrival, data required, slack)을 흉내 낸 것이다. 시각은 클럭 원천 에지 기준 절대 ps:

```text
(2) path report @ Tclk = 2000 ps  (lib: tpcq 90 tccq 40 tsu 60 th 30; times are absolute ps from clock source edge)
  path                   | launch clk | arrival(max) | required(setup) | setup slack | arrival(min) | required(hold) | hold slack
  pc->ir   imem decode   |        400 |         1590 |            2360 |    770 met  |          640 |            450 |   190 met
  ir->opa  regfile read  |        420 |         1810 |            2390 |    580 met  |          610 |            480 |   130 met
  opa->acc 32b adder     |        450 |         2440 |            2540 |    100 met  |          650 |            630 |    20 met
  acc->opa forward mux   |        600 |         2190 |            2390 |    200 met  |          760 |            480 |   280 met
  acc->out output reg    |        600 |          990 |            2370 |   1380 met  |          700 |            460 |   240 met
  sh0->sh1 shift wire    |        500 |          610 |            2520 |   1910 met  |          550 |            610 |   -60 VIOL
  critical (setup) path: opa->acc 32b adder, WNS 100 ps, design Tmin 1900 ps -> Fmax 526.315 MHz (floor)
```

손으로 한 줄 확인 (`opa→acc`): arrival = 450 + 90 + 1900 = 2440, required = 2000 + 600 − 60 = 2540, slack 100. 경로별 Tmin:

| 경로 | skew | Tmin (= tpcq+tpd+tsu−skew) | 이상적 클럭(skew 0)이면 |
|---|---:|---:|---:|
| pc→ir | +20 | 1230 | 1250 |
| ir→opa | +30 | 1420 | 1450 |
| **opa→acc** | **+150** | **1900** | 2050 |
| acc→opa | −150 | 1800 | 1650 |
| acc→out | −170 | 620 | 450 |
| sh0→sh1 | +80 | 90 | 170 |

설계 Tmin = 1900 → **526315 kHz**. 클럭 트리가 acc 를 늦게 받아 critical path 를 150 ps 도와준 대신 (이상적 클럭이면 2050 → 487.8 MHz), 반대 방향 `acc→opa` 는 150 ps 손해를 봤다. 주파수를 올리면:

| Tclk | WNS setup | TNS setup | setup 위반 | WNS hold | hold 위반 |
|---:|---:|---:|---:|---:|---:|
| 2000 (500 MHz) | 100 | 0 | 0 | −60 | 1 |
| 1850 (540.5 MHz) | −50 | −50 | 1 | −60 | 1 |
| 1700 (588.2 MHz) | −200 | −300 | 2 | −60 | 1 |

hold 열은 Tclk 와 무관하게 그대로다. `sh0→sh1` 는 클럭 트리에서 sh1 가지가 80 ps 늦어 hold −60 — 스캔 체인·시프트 레지스터에서 실제로 흔한 모양이다. 25 ps 버퍼 3개로 +15 가 되고, setup 여유(1910)가 충분해 문제없다.

**펌웨어 쪽 연결.** MCU 데이터시트의 "최대 SYSCLK" 은 이런 리포트의 최악 코너(느린 공정·저전압·고온) Tmin 에 마진을 더한 값이다. 그래서 전압 범위를 낮추면 최대 주파수도 내려가고, 플래시처럼 한 주기에 못 끝나는 경로는 wait state 로 "여러 주기를 주는" 설계를 한다 (ARCH-B12, PLL 설정은 BM-B10). 오버클럭이 "가끔만" 죽는 이유는 setup 위반이 특정 데이터 패턴(가장 긴 carry chain)·온도·전압에서만 드러나기 때문이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 38/38 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 38/38, 경고 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o demo_sweep && ./demo_sweep        # 위 스윕·리포트 출력
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| skew 를 `launch − capture` 로 정의하고 식은 그대로 | setup/hold 결론이 통째로 뒤집힌다 | 규약을 먼저 쓰고 고정: `skew = a_C − a_L`, `make_path` 한 곳에서만 계산 |
| hold 식에 Tclk 를 넣음 (`Tclk + tccq + tcd − th − skew`) | hold 는 같은 에지 비교. Tclk 가 들어가면 "느리게 돌리면 hold 도 고쳐진다" 는 틀린 결론 | `tccq + tcd − th − skew` |
| hold 에 `tpcq`/`tpd` (최대 지연) 사용 | hold 는 **가장 빠른** 경로가 문제 → 최소(contamination) 지연 | setup ↔ max, hold ↔ min |
| `fmax = (1e9 + T/2) / T` (반올림) | 0.5 이상이면 올림 → 실제 주기 < Tmin | `1e9 / T` (내림) |
| 주파수 → 주기를 올림 (`ceil`) | 실제 주기보다 길게 잡아 setup 을 낙관적으로 통과 | 검사용 주기는 내림 |
| `1e9 / min_period` 를 Tmin ≤ 0 에서도 호출 | 0 나누기 / 음수를 unsigned 로 바꿔 쓰레기 값 | `if (t <= 0) return 0;` |
| TNS 에 모든 slack 을 더함 | 양수 slack 이 위반을 상쇄해 "TNS 양수 = 문제없음" 착시 | `if (s < 0) tns += s;` |
| critical path 하나 고치고 끝 | 다음 경로가 critical 이 된다 (opa→acc 를 고치면 acc→opa 1800 이 다음) | 리포트를 다시 돌려 WNS 확인 |
| hold 버퍼를 긴 경로와 공유하는 배선에 삽입 | tpd 도 같이 늘어 setup 위반 | 짧은 가지에만, 삽입 후 setup 재검사 |

## 8. 면접 Q&A

<details>
<summary>Q1. setup time 과 hold time 을 정의하고, 위반되면 실제로 무슨 일이 생기나요?</summary>

setup: 클럭 에지 **전** tsu 동안 D 가 안정, hold: 에지 **후** th 동안 안정. 위반되면 플롭이 0 도 1 도 아닌 중간 상태에 머무는 **준안정(metastability)** 에 빠질 수 있고, 정해지지 않은 시간 뒤 임의의 값으로 결정된다. 결과는 틀린 값 캡처(조용한 데이터 오류) 또는 하위 논리로 퍼지는 불확정 값.
꼬리질문 "hold time 이 음수일 수 있나?" → 있다. 라이브러리 값은 플롭 내부 클럭·데이터 경로 지연 차이로 정해지므로, 내부 클럭 경로가 데이터 경로보다 늦지 않으면 음수 hold 가 나온다 (그만큼 hold 가 쉬워짐). 테스트의 랜덤 경로도 th 를 −20 까지 넣었다.

</details>

<details>
<summary>Q2. setup 위반과 hold 위반 중 무엇이 더 위험한가요? 클럭을 낮추면 둘 다 고쳐지나요?</summary>

hold 가 더 위험하다. setup slack = `Tclk + skew − (tpcq + tpd + tsu)` 는 Tclk 를 늘리면 커지지만, hold slack = `tccq + tcd − th − skew` 에는 Tclk 가 없다. 그래서 칩이 나온 뒤 hold 위반이 발견되면 주파수로는 못 살린다 — 시프트 레지스터 테스트가 1 MHz 까지 내려도 실패하는 것.
꼬리질문 "실리콘에서 발견되면?" → 전압·온도 조건을 바꿔 피해 갈 수 있는지 보고(코너에 따라 hold 가 달라지므로), 클럭 지연을 조절하는 장치가 칩에 있으면 그것으로, 아니면 메탈 ECO 나 리스핀. 그래서 사인오프에서 hold 는 fast 코너로 반드시 깨끗해야 한다.

</details>

<details>
<summary>Q3. 클럭 스큐는 setup 에 좋은가요, 나쁜가요?</summary>

부호에 달렸다. capture 클럭이 늦게 오면(skew > 0) 데이터에 시간을 더 주므로 setup 에 +skew, 같은 에지의 hold 요구가 늦어지므로 hold 에 −skew. 그래서 일부러 skew 를 쓰는 **useful skew** 최적화가 있다 (추가 연습 1). 단 같은 두 레지스터 사이라도 반대 방향 경로에는 부호가 뒤집히고(Part C 의 opa↔acc), 다음 단 경로에는 그 레지스터가 launch 가 되어 손해가 된다.
꼬리질문 "부호를 모를 땐?" → 교과서(Harris & Harris)처럼 skew 를 최악값으로 보고 **양쪽 모두에서 뺀다**: `Tc ≥ tpcq + tpd + tsu + tskew`, `tccq + tcd ≥ th + tskew`. 보수적인 설계 규칙이다.

</details>

<details>
<summary>Q4. tpcq 100 ps, 논리 1.5 ns, tsu 80 ps 이면 최대 주파수는? 파이프라인 단을 하나 더 넣으면?</summary>

Tmin = 100 + 1500 + 80 = 1680 ps → 1/1.68 ns ≈ **595 MHz** (정수로는 내림 595238 kHz). 논리를 반으로 나눠 단을 추가하면 각 단 Tmin = 100 + 750 + 80 = 930 ps → 약 1.075 GHz. 2배가 아니라 1.8배인 이유: 단마다 **플롭 오버헤드 tpcq + tsu (180 ps)** 가 또 붙는다. 단을 계속 쪼개면 오버헤드가 지배해 이득이 줄고, 지연 시간(latency)과 분기 페널티는 늘어난다.
꼬리질문 "논리를 정확히 반으로 못 나누면?" → 가장 긴 단이 주기를 정한다. 재배치(retiming)나 useful skew 로 균형을 맞춘다.

</details>

<details>
<summary>Q5. 버튼 입력이나 다른 클럭 도메인 신호는 setup/hold 를 어떻게 보장하나요?</summary>

보장할 수 없다 — 비동기 신호는 언젠가 반드시 setup/hold 창 안에서 바뀐다. 그래서 준안정을 **확률적으로** 관리한다: 플롭 2개(또는 3개)를 직렬로 둔 **동기화기(synchronizer)** 로 첫 플롭이 결정될 시간을 한 주기 더 준다. `MTBF = e^(tr/τ) / (T0 · fclk · fdata)` 에서 tr 이 지수에 있어서 한 단 추가가 엄청난 차이를 만든다 (추가 연습 5: 설명용 값으로 4.7일 → 8e17 초).
꼬리질문 "멀티비트 카운터를 넘길 땐?" → 비트마다 따로 동기화하면 비트들이 서로 다른 주기에 잡혀 엉뚱한 값이 된다. Gray 코드(한 번에 1비트만 변함), 핸드셰이크, 비동기 FIFO 를 쓴다. 많은 MCU 는 GPIO 입력 경로에 동기화기를 하드웨어로 넣어 두고, 그 때문에 핀 읽기에 1 주기 안팎의 지연이 있다고 데이터시트에 적어 두기도 한다 (예: AVR 의 PINx 동기화기 설명).

</details>

<details>
<summary>Q6. 실리콘 검증에서 "이 칩이 목표 주파수에서 타이밍 마진이 있다" 는 걸 어떻게 확인하나요?</summary>

STA 는 설계 단계의 모델이고, 실리콘에서는 **shmoo** — 전압(축 1)과 주파수(축 2)를 쓸며 테스트 패턴의 pass/fail 지도를 그린다. 경계가 "전압을 올리면 더 빠르게 통과" 모양이면 setup 성 한계(논리가 빨라짐), 주파수와 무관하게 특정 전압·온도에서만 실패하면 hold 성 문제나 다른 원인을 의심한다. 온도(저온/고온)와 여러 칩(공정 편차)으로 반복해 마진을 본다.
꼬리질문 "펌웨어 엔지니어로서 연결점은?" → 데이터시트의 전압 범위별 최대 주파수·플래시 wait state 표가 바로 이 마진의 결과다. 클럭을 올리기 전에 wait state 와 전압 스케일링 설정을 먼저 바꿔야 하는 순서 문제(BM-B10, ARCH-B12)가 여기서 온다.

</details>

<details>
<summary>Q7. STA 리포트에서 WNS −50 ps, TNS −300 ps 이면 어떻게 읽고 무엇을 하나요?</summary>

WNS 는 가장 나쁜 한 경로의 slack, TNS 는 위반한 경로 slack 의 합 — TNS 가 WNS 보다 훨씬 크면 위반 경로가 많다는 뜻이다. setup 수정 수단: 논리 줄이기(재구성·파이프라이닝·retiming), 더 빠른 셀(크기 키우기, 낮은 Vt 셀), useful skew, 마지막으로 목표 주파수 낮추기. hold 수정: 짧은 경로에 지연 버퍼 (setup 재확인).
꼬리질문 "모든 경로가 한 주기 안에 와야 하나?" → 아니다. 설계 의도상 여러 주기를 주는 경로는 **multicycle path**, 실제로 동작에서 일어나지 않는 경로(예: 서로 배타적인 모드)는 **false path** 로 제약을 걸어 STA 가 다르게 검사하게 한다. 잘못 걸면 진짜 위반을 숨기므로 검증 대상이다.

</details>

## 9. 추가 연습

### 연습 1 — useful skew 로 2단 파이프라인 균형 잡기

R1 →(s1)→ R2 →(s2)→ R3 에서 R2 클럭만 d 만큼 늦춘다 (s1 skew +d, s2 skew −d). s1 은 tpd 1900, s2 는 1100 (tpcq 90, tsu 60). 최소 Tc 와 d 는? s1 의 tcd 가 500 일 때와 160 일 때를 비교하라.

<details>
<summary>답 보기</summary>

a1 = 90+1900+60 = 2050, a2 = 1250. 조건 `Tc ≥ a1 − d`, `Tc ≥ a2 + d` → d = (a1 − a2)/2 = 400, Tc = 1650 (이상적 클럭이면 2050). 하지만 s1 hold 는 `h1 − d ≥ 0` (h1 = tccq + tcd − th): tcd 500 → h1 = 510 이라 d = 400 OK. tcd 160 → h1 = 170 이라 d ≤ 170, Tc = max(2050−170, 1250+170) = **1880**. useful skew 도 hold 가 한계를 정한다. s2 쪽은 skew −170 이라 hold 가 오히려 좋아진다 (380).

</details>

### 연습 2 — 코너: setup 은 slow, hold 는 fast

데이터 경로 지연(tpcq, tccq, tpd, tcd)만 slow 125%, fast 70% 로 바꾸는 단순 모델에서, typical 은 통과하지만 slow 에서 setup 이 깨지는 경로와 fast 에서 hold 가 깨지는 경로를 하나씩 보여라.

<details>
<summary>답 보기</summary>

P @2000: typical setup 320 → slow: 125 + 1875 + 80 = 2080 → **−80**. 다른 경로 {tccq 60, tcd 40, th 50, skew 30}: typical hold 20 → fast: 42 + 28 − 50 − 30 = **−10**. 그래서 사인오프는 setup 을 느린 코너(SS, 저전압, 해당 온도)에서, hold 를 빠른 코너(FF, 고전압)에서 본다. 주의: 모든 지연(클럭 포함)을 똑같은 비율로 바꾸면 hold slack 은 부호가 안 바뀐다 — hold 문제는 데이터와 클럭 경로가 **다르게** 변하는 데서 나온다 (실제 도구는 OCV 디레이트로 launch/capture 를 다르게 본다). 이 모델은 그 차이를 "tsu/th/skew 고정" 으로 단순화한 것.

</details>

### 연습 3 — 클럭 불확실성(jitter)

setup 에 100 ps, hold 에 30 ps 의 uncertainty 를 준다. P 의 slack 과 Fmax 는?

<details>
<summary>답 보기</summary>

setup 320 − 100 = 220, hold 210 − 30 = 180, Fmax = 1e9 / (1680 + 100) = **561797 kHz**. 주기 사이 지터(cycle-to-cycle)는 다음 에지 위치를 흔들어 setup 에 직접 들어간다. hold 는 같은 에지 비교라 지터 영향이 원리적으로 작지만, 도구에서는 보통 작은 hold uncertainty 를 마진으로 준다.

</details>

### 연습 4 — 논리 깊이 예산

1 GHz 와 2 GHz 에서 tpcq 50, tsu 40, uncertainty 30, skew 0 일 때 조합 논리에 쓸 수 있는 최대 지연과, 게이트 한 단 15 ps (설명용 값) 이면 몇 단인가? 10 GHz 는?

<details>
<summary>답 보기</summary>

예산 = Tc − tpcq − tsu + skew − u: 1 GHz → 1000 − 120 = **880 ps → 58 단**, 2 GHz → 380 ps → **25 단**, 10 GHz → −20 ps → 0 단 (플롭 오버헤드만으로 주기를 다 씀). "고클럭 CPU 는 단당 논리가 얕다" 는 말의 숫자 버전이다.

</details>

### 연습 5 — 동기화기 MTBF: 2-FF vs 3-FF

`MTBF = e^(tr/τ) / (T0 · fclk · fdata)`, tr = (N−1)(Tc − tpcq − tsu). 1 GHz, tpcq 90, tsu 60, τ 30 ps, T0 100 ps, fdata 50 MHz (모두 설명용 값).

<details>
<summary>답 보기</summary>

tr(2-FF) = 850 ps → e^(28.3) / (100e-12 · 1e9 · 50e6) ≈ **4.04e5 s (4.7일)** — 제품으로는 불합격. 3-FF 는 tr = 1700 → **8.15e17 s** (우주 나이 수준). 한 단이 e^(850/30) 배를 준다. 실제 τ, T0 는 공정·셀마다 다르고 측정/라이브러리 값을 써야 한다. practice.c 실제 출력: `MTBF 2-FF = 4.04e+05 s (4.7 days), 3-FF = 8.15e+17 s`.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 15/15 passed
```

## 10. 핵심 정리

- setup slack = `Tclk + skew − (tpcq + tpd + tsu)` — **최대** 지연, **다음** 에지.
- hold slack = `tccq + tcd − th − skew` — **최소** 지연, **같은** 에지, **Tclk 없음** → 주파수로 못 고친다.
- skew = capture 도착 − launch 도착. +skew 는 setup +, hold −. 반대 방향 경로에는 부호가 뒤집힌다.
- Fmax = 1e9 / Tmin (kHz, **내림**). 주파수 → 검사용 주기도 **내림** (비관적으로).
- 설계 Fmax = 가장 긴 경로(critical path)의 Tmin. WNS = 최소 slack, TNS = 음수 slack 의 합.
- hold 수정 = 짧은 경로에 버퍼 → 긴 경로와 공유하면 setup 재확인.
- 비동기 입력은 setup/hold 보장 불가 → 2-FF 동기화기, MTBF 는 tr 에 지수적.

## 참고자료

- Harris & Harris, *Digital Design and Computer Architecture, ARM Edition* — 3.5 Timing of Sequential Logic (tpcq/tccq/tpd/tcd, 클럭 스큐, 준안정·동기화기)
- Patterson & Hennessy, *Computer Organization and Design, ARM Edition* — 부록 "The Basics of Logic Design" (클럭·플롭 기초)
- [OpenSTA (The OpenROAD Project) — 오픈소스 정적 타이밍 분석기](https://github.com/The-OpenROAD-Project/OpenSTA)
- Clifford E. Cummings, *Clock Domain Crossing (CDC) Design & Verification Techniques Using SystemVerilog* (SNUG 2008) — 동기화기, Gray 코드, 비동기 FIFO
- MCU 데이터시트의 "operating conditions" 표 (예: STM32 시리즈의 전압 범위별 최대 주파수, 플래시 wait state 표) — 펌웨어 쪽 연결 (BM-B10, ARCH-B12)
