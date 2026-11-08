---
id: HWLAB-B04
time: 70분
updated: 2026-11-08
---
# HWLAB-B04 파형 기본 측정 알고리즘: 상태 레벨·Rise/Fall·Overshoot (IEEE 181)

## 학습 목표

- 스코프의 자동 측정(Top/Base, Rise/Fall, Overshoot, Freq, Duty, +Width)이 **어떤 순서로 무엇을 계산하는지** 직접 구현한다: 히스토그램 mode 상태 레벨(state level) → 10/50/90 % 기준 레벨(reference level) → 선형 보간 교차 시각 → 전이 시간·aberration·펄스 파라미터.
- 링잉과 잡음이 있는 파형에서 **min/max 기반 순진한(naive) 방법이 왜 거짓말을 하는지** 수치로 보인다: overshoot가 정의상 0 %, 진폭 +20 %, rise time +32 %, 그리고 overshoot > 12.5 %면 rise time이 수십 배로 망가지는 "절벽".
- 측정 자체의 한계를 설명할 수 있다: 잡음이 overshoot를 위로 편향시키는 양(≈ σ·√(2 ln N)/A), 샘플 스냅(snap) vs 보간 오차, 히스테리시스로 링잉을 에지로 오인하지 않는 법.
- 보드 두 장의 캡처로 bring-up 리포트를 만들고, "직렬 종단 저항 미실장 → overshoot 23.9 %, Vmax 4.09 V > 절대 최대 정격 3.6 V" 를 하드웨어 엔지니어가 바로 행동할 수 있는 형태로 보고한다.

## 1. 문제

첫 보드 bring-up에서 스코프 화면의 "Overshoot 0 %, Rise 61 ns" 를 그대로 믿으면 엉뚱한 곳을 디버깅하게 된다 — Rivian/NVIDIA/Apple 보드·실리콘 검증 면접의 단골 질문 "그 숫자는 스코프가 어떻게 계산한 것이고, 그 값이 무슨 의미인가?" 에 답하려면 측정 알고리즘을 직접 짜 봐야 한다.

스코프의 측정값은 전부 **두 개의 상태 레벨(base, top)** 에서 출발한다. top/base가 틀리면 진폭(amplitude)이 틀리고, 10/50/90 % 기준 레벨이 틀리고, 그 위에 쌓인 rise time·overshoot·duty가 연쇄적으로 틀린다. IEEE Std 181 (Transitions, Pulses, and Related Waveforms)은 이 용어와 계산 절차를 표준화한 문서이고, 스코프 벤더와 MATLAB `statelevels`/`risetime`/`overshoot` 같은 도구가 이 정의를 따른다(세부 옵션은 벤더마다 다름).

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">펄스 파형과 IEEE 181 측정 기준선 (링잉 있는 2차 응답)</text>
  <g stroke="currentColor" stroke-dasharray="4 4" opacity=".45">
    <line x1="110" y1="52" x2="750" y2="52"/><line x1="110" y1="80" x2="750" y2="80"/>
    <line x1="110" y1="94" x2="750" y2="94"/><line x1="110" y1="150" x2="750" y2="150"/>
    <line x1="110" y1="206" x2="750" y2="206"/><line x1="110" y1="220" x2="750" y2="220"/>
    <line x1="110" y1="248" x2="750" y2="248"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="8" y="50">max (naive top)</text><text x="8" y="76">top = mode</text>
    <text x="8" y="100">90 %</text><text x="8" y="154">50 % (mid)</text><text x="8" y="203">10 %</text>
    <text x="8" y="230">base = mode</text><text x="8" y="256">min (naive base)</text>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round" points="110,220 160,220 166,206 190,150 214,94 226,60 234,52 244,70 254,90 264,84 274,77 284,81 294,80 420,80 426,94 450,150 474,206 486,240 494,248 504,230 514,210 524,216 534,223 544,219 554,220 640,220 646,206 670,150 694,94 706,60 714,52 724,70 734,90 744,84 750,80"/>
  <g class="f-ok" stroke="currentColor"><circle cx="166" cy="206" r="4"/><circle cx="190" cy="150" r="4"/><circle cx="214" cy="94" r="4"/>
    <circle cx="450" cy="150" r="4"/><circle cx="670" cy="150" r="4"/></g>
  <g class="f-warn" stroke="currentColor"><circle cx="234" cy="52" r="4"/><circle cx="494" cy="248" r="4"/></g>
  <g stroke="currentColor"><line x1="300" y1="54" x2="300" y2="78"/><line x1="560" y1="222" x2="560" y2="246"/></g>
  <g fill="currentColor" font-size="12"><text x="306" y="70">overshoot = (max − top) / A</text>
    <text x="566" y="238">undershoot=(base−min)/A</text></g>
  <g stroke="currentColor" opacity=".6"><line x1="166" y1="206" x2="166" y2="276"/><line x1="214" y1="94" x2="214" y2="276"/></g>
  <g stroke="currentColor"><line x1="168" y1="270" x2="212" y2="270"/><line x1="192" y1="292" x2="448" y2="292"/><line x1="192" y1="314" x2="668" y2="314"/></g>
  <g fill="currentColor" font-size="12"><text x="220" y="274">t_r = t(90 %) − t(10 %)</text>
    <text x="260" y="288">+width: 50 % 상승 → 50 % 하강</text><text x="400" y="310">period: 50 % 상승 → 다음 50 % 상승</text></g>
</svg>
```

A = top − base (진폭). **top/base는 "가장 오래 머무는 전압"(히스토그램 mode)** 이지 최댓값/최솟값이 아니다. 최댓값은 overshoot의 꼭대기다.

### Part A — 상태 레벨·교차·전이 (측정 라이브러리 핵심)

- `state_levels(v, nbins=200)` — min..max 를 nbins 칸으로 나눈 히스토그램을 가운데에서 둘로 나누어, 아래 절반의 최빈 칸 → base, 위 절반의 최빈 칸 → top. 칸 중심 대신 최빈 칸 ±1 칸 샘플들의 평균으로 다듬는다.
- `cross_time(t, v, i, level, interp=True)` — `v[i]`, `v[i+1]` 사이에서 level 교차 시각을 **선형 보간**. `interp=False` 면 "넘어선 첫 샘플" 시각 (비교용).
- `transitions(t, v, base, top, lo=0.1, hi=0.9)` — 히스테리시스 상태기계로 상승/하강 전이 목록 `dict(dir, t_lo, t_mid, t_hi, i0, i1)`.
- `pulse_stats(trs)` — 50 % 교차로 period·freq·+width·duty.
- 정답이 알려진 파형으로 확인: 사다리꼴 펄스(램프 5 ns → 10–90 = 4.000 ns, 주기 100 ns, 폭 40 ns)와 RC 에지(10–90 = τ·ln 9 = 2.197τ).

### Part B — 링잉·잡음에서 IEEE 방식 vs min/max 방식

- `aberrations(v, trs, base, top)` — 에지별 post-transition 구간에서 overshoot(상승 후 top 위)와 undershoot(하강 후 base 아래)의 최악값.
- `measure(t, v, method)` 로 같은 파형을 `"ieee"`(히스토그램) / `"naive"`(base=min, top=max) 두 방식으로 재고 **해석해와 오차를 비교**한다. 파형은 2차(RLC) 계단 응답(ζ = 0.6, fn = 150 MHz) 펄스 + 10 mV rms 잡음: overshoot 참값 = exp(−πζ/√(1−ζ²)) = 9.48 %, 10–90 % 참값은 해석식을 이분법으로 푼 1.967 ns.
- 추가로: ζ = 0.45(overshoot 20.5 %)에서 naive rise time이 붕괴하는 것, 0.5 ns 샘플링에서 보간 유무의 오차, 링잉 없는 펄스에서도 잡음만으로 naive top이 50 mV 넘게 뜨는 것.

### Part C — VALBRD-A bring-up: MCO_8M 두 보드 판정 리포트

가상의 보드 **VALBRD-A rev B**. SoC의 8 MHz 클럭 출력 `MCO_8M` 이 오디오 코덱 클럭 입력으로 간다. 송신단 직렬 종단 R47(33 Ω)을 거쳐야 하는데, SN0042 보드는 BOM 오류로 **R47 자리에 0 Ω** 이 실장됐다(심어 둔 결함). TP31에서 스프링 팁(ground spring)으로, 5 GS/s, 700 mV/div, 12-bit로 1 µs씩 캡처한 CSV 두 장(`SN0041_tp31_mco8m.csv`, `SN0042_tp31_mco8m.csv`)을 읽어, 스펙(rise/fall ≤ 5 ns, overshoot/undershoot ≤ 10 %, 수신 칩 절대 최대 정격 −0.3 V … 3.6 V, freq 8 MHz ±1 %, duty 45–55 %)으로 항목별 PASS/FAIL 리포트를 낸다. 같은 캡처를 naive 방식으로 판정했다면 무엇을 오진했을지도 같이 찍는다.

## 2. Problem (English)

> **Implement a scope's automatic pulse measurements (IEEE 181 style).**
>
> Given a scope capture as two lists `t` (seconds, uniform spacing) and `v` (volts), using only the Python 3 standard library:
>
> 1. `state_levels(v, nbins=200) -> (base, top)`: histogram the samples over `[min, max]`, split the histogram at the
>    midpoint, and return the mode of the lower half and of the upper half (refine each mode by averaging the samples in the
>    mode bin ±1 bin).
> 2. `transitions(t, v, base, top, lo=0.1, hi=0.9)`: find rising/falling transitions with hysteresis (a transition is only
>    complete when the signal goes from `<= base+lo*A` to `>= base+hi*A` or vice versa). For each, return the
>    linearly-interpolated reference-level instants `t_lo`, `t_mid`, `t_hi` (last `lo` crossing before the edge, first `hi`
>    crossing for a rising edge; mirrored for falling).
> 3. `pulse_stats(transitions)` -> period, frequency, positive pulse width, duty cycle (from 50 % instants);
>    `aberrations(...)` -> worst overshoot after rising edges and worst undershoot after falling edges, in % of amplitude.
> 4. Compare against a naive implementation that uses `base=min(v), top=max(v)` on a ringing, noisy pulse train whose true
>    rise time and overshoot are known analytically; report the errors.
> 5. Produce a PASS/FAIL report for a clock net against a spec (rise/fall, overshoot, absolute-max voltage, freq, duty).
>
> Example: a trapezoid pulse 0→3.3 V with 5 ns linear ramps, period 100 ns, 50 %-to-50 % width 40 ns must measure
> `rise = fall = 4.000 ns`, `freq = 10 MHz`, `duty = 40 %`. For a second-order step response with ζ = 0.6 the overshoot must
> measure ≈ 9.5 % (the naive method reports 0 %).

## 3. 시작 코드

<!-- code: starter.py -->

Part C 캡처 생성기 (결정적 — 다시 돌려도 CSV가 바이트 단위로 같다):

<!-- code: gen_capture.py -->

```bash
cd lessons/HWLAB/HWLAB-B04
python3 -I starter.py ; echo "exit=$?"      # TODO 상태: 5/26 passed, exit=1
```

실제 출력 (일부):

```text
-- Part A: state levels / crossings / transitions --
PASS  state_levels(trapezoid) == (0, 3.3) +-1mV   [base=0.0000 top=3.3000 exp 0/3.3]
FAIL  cross_time interpolates (0V@0ns, 2V@1ns, level 0.5V -> 0.25ns): returned False
PASS  cross_time(interp=False) -> first sample past the level
FAIL  transitions: 5 rising + 5 falling, alternating: dirs=[]
...
-- Part B: ringing + noise, IEEE-181 histogram vs naive min/max --
      truth: rise=1.9672ns overshoot=9.48% | ieee: rise=None os=0.00% top=3.6343 | naive: rise=None os=0.00% top=3.6343
FAIL  ieee state levels within 10mV of 0 / 3.3V: base=-0.3315 top=3.6343
...
PASS  naive overshoot is 0 by construction (top == max)   [0.000%]
PASS  naive amplitude inflated > 15%   [amp=3.966V (+20.2%)]
...
FAIL  no ringing, 20mV noise: ieee top +-5mV, naive max > +50mV: ieee top=3.3598, naive top(max)=3.3598 (noise 20mV rms)
-- Part C: bring-up report (VALBRD-A MCO_8M @ TP31) --
PASS  captures loaded: 5000 samples @ 0.2ns each
      (setup failed: TypeError: unsupported operand type(s) for *: 'NoneType' and 'float')
...
5/26 passed
exit=1
```

> 눈여겨볼 것: starter의 `state_levels` 는 일부러 **naive(min/max)** 를 돌려준다. 그런데도 첫 테스트(이상적인 사다리꼴)는 PASS다 — 링잉·잡음이 없는 파형에선 min/max와 mode가 같기 때문이다. **깨끗한 파형으로만 측정 코드를 검증하면 naive 구현도 통과한다.** 그래서 Part B는 해석해가 있는 "더러운" 파형으로 검증한다.

## 4. 힌트

<details>
<summary>힌트 1 — 히스토그램 칸 번호와 마지막 샘플</summary>

`k = int((x - vmin) / w)` 에서 `x == vmax` 면 `k == nbins` 가 되어 범위를 넘는다. `min(k, nbins - 1)` 로 마지막 칸에 넣는다. 아래 절반은 `range(0, nbins//2)`, 위 절반은 `range(nbins//2, nbins)`. `max(range, key=counts.__getitem__)` 는 동률일 때 앞쪽 칸을 고른다.

</details>

<details>
<summary>힌트 2 — 선형 보간 한 줄</summary>

`t = t[i] + (level − v[i]) / (v[i+1] − v[i]) · (t[i+1] − t[i])`. 두 샘플이 level의 양쪽에 있을 때만 부른다. `v[i+1] == v[i]` 면 0으로 나누지 말고 `t[i]`.

</details>

<details>
<summary>힌트 3 — 왜 "마지막 10 % 교차, 첫 90 % 교차"인가</summary>

상승 에지에서 잡음 때문에 10 % 레벨을 여러 번 오르내릴 수 있다. 전이는 "10 % 아래를 **마지막으로** 떠난 순간"부터 "90 %에 **처음** 닿은 순간"까지다. 상태기계로 하면 자연스럽다: LOW 상태에서 `v <= L` 일 때마다 `last_low = i` 를 갱신하고, `v >= H` 가 되는 순간 `(last_low, last_low+1)` 사이 L 교차와 `(i−1, i)` 사이 H 교차를 계산한다.

</details>

<details>
<summary>힌트 4 — 링잉을 새 에지로 세지 않기</summary>

overshoot 뒤의 링잉은 90 % 아래로 잠깐 내려올 수 있다. 하지만 10 %까지 내려가지 않으면 상태는 계속 HIGH다. 50 % 단일 임계값으로 에지를 세면 (특히 잡음 + 느린 에지에서) 에지가 두 번 세어진다 — 히스테리시스가 필요한 이유다.

</details>

<details>
<summary>힌트 5 — aberration 구간</summary>

전이 k 의 post-transition 구간은 `trs[k]["i1"]`(전이가 끝난 샘플)부터 다음 전이의 `i0` 까지(마지막 전이면 레코드 끝). 상승이면 그 구간의 max, 하강이면 min. 에지마다 계산해 **최악값**을 보고한다 — 스펙은 최악 에지에 대해 지켜져야 한다.

</details>

## 5. 풀이

### Part A — 상태 레벨은 "가장 오래 머문 전압"

디지털 펄스의 샘플 대부분은 base 또는 top **근처에 머문다**. 에지 위의 샘플은 소수다. 그래서 전압 히스토그램은 쌍봉(bimodal)이 되고, 두 봉우리가 상태 레벨이다.

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">전압 히스토그램 (ζ=0.6 링잉 + 10 mV 잡음, 5 펄스)</text>
  <line x1="60" y1="210" x2="720" y2="210" stroke="currentColor"/>
  <g class="f-mut" stroke="currentColor" stroke-width=".5">
    <rect x="74" y="200" width="8" height="10"/><rect x="84" y="196" width="8" height="14"/><rect x="96" y="192" width="8" height="18"/>
    <rect x="106" y="170" width="8" height="40"/><rect x="126" y="170" width="8" height="40"/><rect x="136" y="196" width="8" height="14"/>
    <rect x="160" y="204" width="8" height="6"/><rect x="220" y="205" width="8" height="5"/><rect x="290" y="205" width="8" height="5"/>
    <rect x="360" y="205" width="8" height="5"/><rect x="430" y="205" width="8" height="5"/><rect x="500" y="205" width="8" height="5"/>
    <rect x="570" y="204" width="8" height="6"/><rect x="592" y="196" width="8" height="14"/><rect x="602" y="170" width="8" height="40"/>
    <rect x="622" y="170" width="8" height="40"/><rect x="634" y="192" width="8" height="18"/><rect x="646" y="196" width="8" height="14"/>
    <rect x="658" y="200" width="8" height="10"/>
  </g>
  <g class="f-ok" stroke="currentColor"><rect x="116" y="60" width="8" height="150"/><rect x="612" y="60" width="8" height="150"/></g>
  <line x1="369" y1="40" x2="369" y2="215" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="375" y="52" fill="currentColor" font-size="12">split = (min+max)/2</text>
  <g stroke="currentColor"><line x1="74" y1="215" x2="74" y2="232"/><line x1="666" y1="215" x2="666" y2="232"/></g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="120" y="54">base = mode ≈ 0.001 V</text><text x="616" y="54">top = mode ≈ 3.300 V</text>
    <text x="74" y="246">min −0.33 V</text><text x="666" y="246">max 3.63 V</text>
    <text x="370" y="264">에지 위 샘플은 적고 고르게 퍼진다 · 링잉은 봉우리 바깥 꼬리를 만든다 · min/max는 그 꼬리의 끝</text>
  </g>
</svg>
```

| 단계 | 계산 | 왜 |
|---|---|---|
| ① 히스토그램 | min..max 를 200칸 | 범위를 데이터에서 잡아야 어떤 V/div에서도 동작 |
| ② 반으로 나눔 | 칸 `0..99` / `100..199` | 쌍봉의 각 봉우리를 따로 찾기 위해. 링잉이 위아래로 대칭이면 split ≈ 실제 mid |
| ③ 각 절반의 mode | 최빈 칸 | top/base = 가장 오래 머문 전압 |
| ④ 다듬기 | 최빈 칸 ±1 칸 샘플 평균 | 칸 중심은 칸 폭/2(여기선 ~10 mV)까지 틀림. 평균은 잡음을 평균내어 mV 이하로 |

IEEE 181은 상태 레벨을 구하는 방법으로 히스토그램 기반(mode, mean)과 peak 기반(최솟값·최댓값) 등을 함께 기술하고, 측정값과 함께 **어느 방법을 썼는지** 기록해야 비교가 된다. 스코프도 메뉴에 이 선택이 있다(예: Top/Base 또는 High/Low 방법 = Auto / Histogram / Min-Max). 대개 Auto는 히스토그램 봉우리가 뚜렷하면 mode, 아니면 min/max로 후퇴한다 — 연습 2에서 그 판단을 구현한다.

**기준 레벨과 교차.** A = top − base, 기준 레벨 L = base + 0.1A, M = base + 0.5A, H = base + 0.9A. 교차 시각은 두 샘플 사이를 직선으로 잇고 푼다. 사다리꼴(직선 램프)에선 보간이 **정확**해서 `rise = 4.0000 ns` 가 그대로 나온다. RC 에지는 곡선이지만 dt = 10 ps 로 촘촘하면 τ·ln 9 = 2.1972 ns 와 맞는다:

- RC: v = 1 − e^(−t/τ) → t(10 %) = τ·ln(1/0.9), t(90 %) = τ·ln 10 → **t_r = τ·ln 9 ≈ 2.197τ**. 여기서 BW ≈ 0.35/t_r 가 나온다 (0.35 ≈ ln 9 / 2π, 1차 응답 가정 — 스코프 대역폭 이야기는 HWLAB-B05 참고).

**히스테리시스 상태기계.** 에지 검출을 50 % 하나로 하면 잡음·링잉이 50 %를 여러 번 지나며 가짜 에지를 만든다. 10 %/90 % 두 임계를 쓰면 "완전히 건너간 것"만 전이다. 이건 스코프 edge 트리거의 히스테리시스와 같은 발상이다 (트리거 상태기계는 HWLAB-B06 참고).

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwb04-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">transitions(): 10 %/90 % 히스테리시스 상태기계</text>
  <rect x="40" y="70" width="150" height="60" rx="10" class="f-mut" stroke="currentColor"/>
  <rect x="550" y="70" width="150" height="60" rx="10" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle"><text x="115" y="96" font-weight="bold">LOW</text><text x="115" y="116" font-size="12">v ≤ L : last_low = i</text>
    <text x="625" y="96" font-weight="bold">HIGH</text><text x="625" y="116" font-size="12">v ≥ H : last_high = i</text></g>
  <path d="M190 85 L550 85" stroke="currentColor" fill="none" marker-end="url(#hwb04-arr)"/>
  <path d="M550 115 L190 115" stroke="currentColor" fill="none" marker-end="url(#hwb04-arr)"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="370" y="62">v ≥ H → 상승 전이: t_lo = (last_low, +1) 의 L 교차 (마지막) · t_hi = (i−1, i) 의 H 교차 (처음)</text>
    <text x="370" y="148">v ≤ L → 하강 전이: t_hi = (last_high, +1) 의 H 교차 (마지막) · t_lo = (i−1, i) 의 L 교차 (처음)</text>
    <text x="370" y="172">L &lt; v &lt; H : 상태 유지 — 링잉이 90 % 아래로 잠깐 내려와도 새 에지가 아니다</text>
    <text x="370" y="192">t_mid = 그 구간에서 M(50 %)을 처음 지나는 보간 시각 → period·width·duty 계산에 사용</text>
  </g>
</svg>
```

**펄스 파라미터.** period = (마지막 상승 t_mid − 첫 상승 t_mid)/(상승 수 − 1) — 연속 주기들의 평균과 같다. +width = 상승 t_mid → 바로 다음 하강 t_mid, duty = width/period. 50 % 기준이 IEEE 181의 기본(pulse duration)이다. 단, 클럭 duty 스펙이 특정 전압(예: VDD/2, LVTTL 1.5 V)으로 정의돼 있으면 그 레벨로 재야 한다 — 상승/하강 시간이 다르면 기준 레벨에 따라 duty가 바뀐다.

### Part B — naive(min/max) 방법이 거짓말하는 방식

실제 출력 (ζ = 0.6, fn = 150 MHz, 10 mV rms 잡음, dt = 0.1 ns):

| 측정 | 참값(해석해) | IEEE(히스토그램) | naive(min/max) |
|---|---|---|---|
| top | 3.3000 V | 3.2996 V | 3.6343 V |
| 진폭 A | 3.300 V | 3.299 V | 3.966 V (+20.2 %) |
| rise 10–90 | 1.9672 ns | 1.9708 ns (+0.18 %) | 2.6052 ns (+32.4 %) |
| overshoot | 9.48 % | 10.15 % | **0.00 %** |

naive의 결함은 구조적이다:

1. **overshoot = (max − top)/A 인데 top = max 이므로 항상 0.** 링잉이 클수록 "깨끗해 보이는" 측정이 나온다.
2. **진폭이 overshoot + undershoot 만큼 부푼다** → 10 %/90 % 기준선이 바깥으로 밀린다. ζ = 0.6 실측에서 naive 10 % 레벨은 +0.065 V(참 진폭의 약 2 %), 90 % 레벨은 3.238 V(약 98 %) → 2→98 % 구간을 재므로 rise가 길게 나온다. (대칭 링잉 공식으로는 2.4 %/97.6 %, 잡음 꼭대기가 min/max를 조금 더 벌린다.)
3. **절벽(cliff).** overshoot·undershoot가 대칭으로 OS·A 라면 naive base = −OS·A, naive 진폭 = (1+2·OS)·A, naive 10 % 레벨 = −OS·A + 0.1(1+2·OS)·A. 이 값이 실제 base(0) 아래로 내려가는 조건은 `OS > 0.1 + 0.2·OS`, 즉 **OS > 12.5 %**. 그러면 상승 에지 직전에 신호가 그 레벨 아래에 있던 마지막 순간은 **앞 하강 에지의 undershoot 링잉**이고, rise time은 그 시점부터 잰 수십 ns가 된다.

`demo_sweep.py` 실제 출력 (ζ를 바꿔 가며, 같은 잡음):

```text
zeta  OS_true  OS_ieee  OS_naive  rise_true  rise_ieee(err)     rise_naive(err)    naive_10%lvl
0.30   37.23%   37.54%    0.00%    1.402ns   1.411ns( +0.7%)   47.839ns(+3312.2%)  -0.660V
0.40   25.38%   25.95%    0.00%    1.553ns   1.556ns( +0.2%)   47.489ns(+2958.3%)  -0.344V
0.45   20.53%   21.08%    0.00%    1.640ns   1.647ns( +0.4%)   47.232ns(+2779.4%)  -0.217V
0.50   16.30%   16.75%    0.00%    1.738ns   1.749ns( +0.6%)   46.899ns(+2599.2%)  -0.113V
0.55   12.63%   13.08%    0.00%    1.846ns   1.856ns( +0.6%)    7.320ns( +296.6%)  -0.021V
0.60    9.48%   10.15%    0.00%    1.967ns   1.971ns( +0.2%)    2.605ns(  +32.4%)  +0.065V
0.70    4.60%    4.89%    0.00%    2.256ns   2.261ns( +0.2%)    2.602ns(  +15.3%)  +0.197V
0.80    1.52%    1.99%    0.00%    2.618ns   2.627ns( +0.4%)    2.796ns(   +6.8%)  +0.278V
0.90    0.15%    1.11%    0.00%    3.059ns   3.064ns( +0.2%)    3.181ns(   +4.0%)  +0.300V
```

ζ = 0.55(OS 12.6 %)에선 naive 10 % 레벨이 −21 mV — base 바로 아래, 잡음(10 mV rms)의 2σ 자리다. 그래서 rise가 "가끔" 망가진다(7.3 ns). 경계 근처에선 측정이 **확률적으로** 깨진다는 뜻이고, 실제 스코프에서 "측정값이 획마다 튄다"의 한 원인이다.

**IEEE 방식에도 남는 오차 — 잡음이 overshoot를 위로 민다.** 위 표에서 OS_ieee − OS_true 는 0.3~1 %p로 항상 양수다. 최댓값 통계 때문이다: σ 인 가우스 잡음 N 샘플의 최댓값 기대치는 대략 σ·√(2 ln N). 평탄한 top 구간 수천 샘플이면 ≈ 4σ = 40 mV → 40 mV/3.3 V ≈ 1.2 %. 그래서 ζ = 0.9(참 OS 0.15 %)도 1.11 %로 나온다. 잡음이 큰 측정에서 "overshoot 1~2 %" 는 신호가 아니라 잡음 바닥일 수 있다.

| 원인 | 증상 | 드러내는 측정/대책 |
|---|---|---|
| top/base = min/max | overshoot 항상 0 %, 진폭 과대 | Top/Base 방법을 Histogram으로, Max·Min과 Top·Base를 같이 표시 |
| OS > 12.5 % + min/max | rise time 수십 ns (앞 에지 링잉부터 잼) | 같은 에지를 커서로 수동 측정해 비교 |
| 잡음 + max 통계 | 깨끗한 신호도 overshoot 1~2 % | 평균(averaging, 트리거 안정 시), 통계 모드의 분포 확인, 프로브 감쇠비·V/div 최적화 |
| 샘플 스냅(보간 없음) | rise가 dt 단위로 양자화, ±1 샘플 | 보간(선형 또는 sin(x)/x), 에지에 5샘플 이상 |
| 50 % 단일 임계 에지 검출 | 링잉·잡음에서 에지 2배, freq 2배 | 히스테리시스(10/90 %) |
| 히스토그램이 쌍봉 아님 | 삼각파·짧은 펄스에서 top/base 엉뚱 | 쌍봉 여부 확인 후 min/max 후퇴 (연습 2) |
| 화면 밖 클리핑 | overshoot가 화면 끝에서 잘려 과소 | V/div를 키워 전체 파형을 화면 안에 (연습 3) |

**보간 vs 스냅.** 2 GS/s(0.5 ns)로 RC 에지(t_r = 2.197 ns, 에지 위 4~5 샘플)를 재면: 보간 오차 7.5 ps, 스냅(넘어선 첫 샘플) 오차 197.2 ps. 스냅은 두 교차 각각에서 0~1 샘플 늦으므로 rise 오차가 ±dt 범위에서 에지 위상에 따라 달라진다. 실제 스코프는 sin(x)/x 보간으로 표시하고 측정에도 보간을 쓴다. 다만 보간은 **샘플이 대역 제한된 신호를 충분히 담고 있을 때만** 유효하다 — 에지에 샘플이 1~2개뿐이면 보간도 추측이다.

**링잉 없는 펄스 + 20 mV 잡음**: IEEE top 3.3002 V, naive top(max) 3.3598 V. 링잉이 없어도 min/max는 잡음 꼭대기를 상태 레벨로 착각한다.

### Part C — 두 보드 판정과 행동 가능한 리포트

`python3 -I solution.py` 의 Part C 실제 출력:

```text
VALBRD-A SN0041 MCO_8M@TP31 -> PASS  base=-0.001V top=3.301V amp=3.301V edges=16
  ok   rise       = 3.33ns     <= 5.0ns
  ok   fall       = 3.32ns     <= 5.0ns
  ok   overshoot  = 3.2%       <= 10.0%
  ok   undershoot = 3.2%       <= 10.0%
  ok   Vmax       = 3.405V     <= 3.60V(abs max)
  ok   Vmin       = -0.105V    >= -0.30V(abs max)
  ok   freq       = 7.9999MHz  7.92..8.08MHz
  ok   duty       = 49.00%     45..55%
  naive(min/max): top=3.405V overshoot=0.0% rise=3.67ns -> PASS, BAD=-
VALBRD-A SN0042 MCO_8M@TP31 -> FAIL  base=0.000V top=3.300V amp=3.300V edges=16
  ok   rise       = 1.49ns     <= 5.0ns
  ok   fall       = 1.50ns     <= 5.0ns
  BAD  overshoot  = 23.9%      <= 10.0%
  BAD  undershoot = 23.7%      <= 10.0%
  BAD  Vmax       = 4.090V     <= 3.60V(abs max)
  BAD  Vmin       = -0.781V    >= -0.30V(abs max)
  ok   freq       = 8.0001MHz  7.92..8.08MHz
  ok   duty       = 49.00%     45..55%
  naive(min/max): top=4.090V overshoot=0.0% rise=61.32ns -> FAIL, BAD=rise,fall,Vmax,Vmin
```

생성기 파라미터로 맞춰 보면: SN0041은 ζ = 0.75 / fn = 110 MHz → 해석 OS 2.84 %, rise 3.31 ns (측정 3.2 %, 3.33 ns — 잡음 편향만큼 OS가 높다). SN0042는 ζ = 0.42 / fn = 160 MHz → OS 23.37 %, rise 1.488 ns (측정 23.9 %, 1.49 ns).

**리포트를 읽는 법 (하드웨어 엔지니어에게 넘길 문장):**

- `FAIL SN0042 MCO_8M: overshoot 23.9 % / undershoot 23.7 % (spec 10 %), Vmax 4.09 V > 3.6 V, Vmin −0.78 V < −0.3 V (codec abs max). Timing OK (rise 1.49 ns, 8.0001 MHz, duty 49 %). SN0041 동일 셋업(TP31, spring tip) 3.2 % → 측정 체인이 아닌 보드 차이. R47 실장 확인 요청 (BOM: 33R, 실물 0R 의심).`
- 왜 문제인가: 절대 최대 정격 초과는 수신 핀 ESD 다이오드를 순방향으로 켜 전류를 주입한다(래치업 위험, 장기 신뢰성). 지금 기능이 되는 것은 의미가 없다 — FF 코너·저온에서 에지가 더 빨라지면 더 나빠진다. 또 rise가 1.49 ns로 SN0041(3.33 ns)의 절반: 직렬 R이 없으면 에지가 빨라지고(고조파 ↑, EMI ↑) 링잉이 커진다.
- 왜 R47(직렬 종단)이 고치나: 송신단 직렬 종단은 드라이버 출력 임피던스 + Rs ≈ 트레이스 Z0 가 되게 해서 수신단에서 돌아온 반사를 송신단에서 흡수한다. 2차 모델로는 감쇠비 ζ 가 커지는 것 — 같은 LC에 직렬 R을 더하면 ζ = (R/2)·√(C/L) 이 커진다.
- 측정 체인 배제가 먼저다: 15 cm 그라운드 리드는 그 자체로 수백 MHz 링잉을 만든다(HWLAB-B05 참고). 그래서 리포트에 "스프링 팁, 같은 셋업의 정상 보드 대비" 를 같이 적는다. golden 보드 대비 판정 일반론은 HWLAB-B09 참고.

**naive였다면?** SN0042의 naive 판정도 FAIL이지만 **이유가 틀렸다**: `BAD=rise,fall,Vmax,Vmin` — overshoot는 0 %로 사라지고 rise는 61 ns. 리포트를 받은 사람은 "드라이버가 약하거나 부하 커패시턴스가 크다" 를 의심하고 drive strength 레지스터를 올릴 것이다 — 그러면 에지가 더 빨라져 링잉이 더 커진다. **틀린 측정은 틀린 고침을 부른다.** SN0041에서는 naive도 PASS지만 rise 3.67 ns (+10 %)로 마진을 깎아 먹는다.

**장비가 있을 때 (이 레슨에선 실행하지 않음):** PyVISA로 스코프에서 파형을 받아 같은 함수에 넣는다. 명령은 벤더마다 다르다 — 예: Keysight는 `:WAVeform:SOURce CHAN1`, `:WAVeform:FORMat WORD`, `:WAVeform:PREamble?`(스케일·오프셋), `:WAVeform:DATA?`; Tektronix는 `DATa:SOUrce CH1`, `WFMOutpre?`, `CURVe?`. 프리앰블의 x/y 증분·원점으로 코드를 볼트·초로 바꾼 뒤 `measure()` 를 부르면, 스코프의 자동 측정과 **같은 파형으로 두 의견**을 비교할 수 있다 — 스코프 측정 설정(Top/Base 방법, 기준 레벨 %)이 맞는지 확인하는 좋은 방법이다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"              # 26/26 passed, exit=0
python3 -I solution.py --report SN0042_tp31_mco8m.csv  # 캡처 하나만 리포트
python3 -I -W error -X dev solution.py                # 경고 없음 (exit 0)
python3 -I gen_capture.py                             # CSV 재생성 → sha256 동일 (결정적)
python3 -I demo_sweep.py                              # §5 Part B 의 ζ 스윕 표
```

<!-- code: demo_sweep.py -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `top, base = max(v), min(v)` | overshoot가 정의상 0, 진폭·기준 레벨이 바깥으로 밀림, OS > 12.5 %면 rise 붕괴 | 히스토그램 mode (`state_levels`) |
| 히스토그램 칸 중심을 그대로 상태 레벨로 | 칸 폭/2 오차 (200칸 × 4.7 V 범위면 ±12 mV) | mode 칸 ±1 칸 샘플 평균으로 다듬기 |
| 50 % 하나로 에지 검출 | 링잉·잡음이 50 %를 여러 번 지나 에지 수가 늘어남 → freq 2배 | 10 %/90 % 히스테리시스 상태기계 |
| 상승 에지에서 10 % 교차를 "처음" 것으로 | 잡음이 10 % 근처에서 오르내리면 전이 시작이 너무 이름 | 10 % 아래를 **마지막으로** 떠난 순간 (`last_low`) |
| 90 % 교차를 "마지막" 것으로 | 링잉이 90 %를 다시 지나면 rise에 링잉이 포함됨 | 90 %에 **처음** 닿은 순간 |
| 보간 없이 샘플 인덱스로 | rise가 dt 단위 양자화, 0.5 ns 샘플에서 197 ps 오차 | `cross_time(..., interp=True)` |
| `int((x - vmin)/w)` 를 그대로 인덱스로 | `x == vmax` 에서 IndexError | `min(k, nbins - 1)` |
| overshoot를 레코드 전체 max 하나로 | 에지별 차이·마지막 불완전 에지 구분 불가, 하강 후 링잉과 섞임 | 에지별 post-transition 구간, 최악값 보고 |
| 잡음 있는 캡처의 overshoot 1 %를 신호로 해석 | max 통계 편향 ≈ σ√(2 ln N)/A | 잡음 바닥과 비교, 평균/통계 분포 확인 |
| 클리핑된 캡처로 overshoot 판정 | 화면 끝에서 잘린 값 → 과소 측정 | V/div·offset을 바꿔 파형 전체를 화면 안에, 클립 검사 |
| 측정 리포트에 방법을 안 적음 | 다른 사람이 같은 숫자를 재현 못 함 | "histogram mode, 10/90 %, spring tip, 5 GS/s" 를 함께 기록 |

## 8. 면접 Q&A

<details>
<summary>Q1. 스코프의 rise time 값은 내부적으로 어떻게 계산되나요?</summary>

① 레코드 전체로 상태 레벨(top/base)을 구한다 — 기본은 히스토그램 mode, 쌍봉이 아니면 min/max 후퇴. ② A = top − base 로 10 %/90 % 기준 레벨을 만든다. ③ 에지마다 기준 레벨 교차 시각을 샘플 사이 보간으로 구한다 (상승: 10 %를 마지막으로 떠난 시각 → 90 %에 처음 닿은 시각). ④ 차이가 rise time, 여러 에지면 통계(평균/최소/최대).
꼬리질문: "20–80 %는 언제 쓰나?" → 고속 직렬·DDR 계열 스펙이나 많은 SI 문서가 20–80 %를 쓴다. 에지 끝부분(0–10 %, 90–100 %)은 느린 꼬리·링잉·잡음 영향이 커서 반복성이 나쁘기 때문. 스펙이 정한 기준 레벨로 재야 하고, 10–90과 20–80은 서로 환산하면 안 된다(파형 모양에 따라 비율이 다름 — RC는 ln 9 / ln 4 ≈ 1.58배, 가우스 응답은 다른 값).

</details>

<details>
<summary>Q2. 화면에 overshoot 25 %가 보입니다. 신호 문제인지 측정 문제인지 어떻게 가르나요?</summary>

측정 체인부터 의심한다: 긴 그라운드 리드(10x 프로브 입력 C와 리드 인덕턴스가 LC 공진 → 수백 MHz 링잉)를 스프링 팁/그라운드 스프링으로 바꿔 다시 잰다(HWLAB-B05 참고). 링잉 주파수가 프로브 리드 LC의 1/(2π√LC)와 맞으면 측정 아티팩트일 가능성이 높다. 같은 셋업으로 정상 보드(golden)와 비교하고, 가능하면 TP 위치를 수신단 핀 가까이로 옮긴다 — 송신단 TP와 수신단 핀의 파형은 반사 때문에 다르다.
꼬리질문: "스코프 대역폭이 부족하면 overshoot는 어떻게 보이나?" → 스코프는 저역통과 필터다. 대역폭이 신호 링잉 주파수보다 낮으면 링잉이 깎여 **실제보다 작게** 보인다. 반대로 brick-wall에 가까운 응답(고성능 DSP 보정 스코프)은 아주 빠른 에지에서 스코프 자체의 작은 링잉을 보탤 수 있다. 가우스 응답은 overshoot가 거의 없다.

</details>

<details>
<summary>Q3. 왜 min/max 대신 히스토그램 mode를 쓰나요? 언제 mode가 틀리나요?</summary>

min/max는 overshoot·undershoot·잡음 꼭대기를 상태 레벨로 착각한다. 그러면 overshoot가 정의상 0이 되고 진폭이 부풀어 기준 레벨이 밀린다. 대칭 링잉 OS가 12.5 %를 넘으면 naive 10 % 레벨이 base 아래로 내려가서 rise가 앞 에지의 링잉부터 측정된다 (이 레슨에서 1.64 ns → 47 ns).
mode가 틀리는 경우: 평탄한 상태가 없는 파형(삼각파, 톱니파 — 히스토그램이 평평), 펄스가 짧아 링잉이 가라앉기 전에 다음 에지가 오는 경우, top에 기울기(droop/tilt)가 있어 봉우리가 퍼진 경우. 이때 스코프 Auto는 min/max로 후퇴하거나, 사용자가 방법을 명시해야 한다. 사인파는 히스토그램이 양 끝에 몰리는 아크사인 분포라 mode ≈ 피크가 되어 우연히 괜찮다(연습 2).

</details>

<details>
<summary>Q4. 1.5 ns 에지를 5 GS/s로 쟀습니다. 믿을 수 있나요?</summary>

샘플링 관점: 0.2 ns 간격이면 10–90 % 구간에 약 7샘플 — 보간으로 수 ps 정밀도가 나온다(이 레슨 SN0042의 에지별 rise 표준편차 3.7 ps). 하지만 샘플링률보다 먼저 **아날로그 대역폭**이 문제다: 측정 t_r ≈ √(t_sig² + t_scope² + t_probe²), 1차 근사로 t_scope ≈ 0.35/BW. 1.5 ns를 3 % 이내로 보려면 스코프+프로브 t_r이 신호의 약 1/4 이하여야 해서 수백 MHz~GHz급이 필요하다 (계산은 HWLAB-B05 참고).
꼬리질문: "0.35/BW의 가정은?" → 1차(단일 극) 가우스에 가까운 응답. brick-wall(maximally flat)에 가까운 응답의 고대역 스코프는 0.4~0.5/BW 정도로 본다. 그리고 root-sum-square 합성은 각 단이 가우스형일 때의 근사다.

</details>

<details>
<summary>Q5. 잡음 많은 신호에서 overshoot 측정값을 어떻게 믿을 만하게 만드나요?</summary>

overshoot는 최댓값 기반이라 잡음에 위로 편향된다: 기대 편향 ≈ σ·√(2 ln N)/A. 대책: ① 트리거가 안정된 반복 신호면 averaging(랜덤 잡음은 1/√n로 감소, 하지만 지터가 있으면 에지가 뭉개져 overshoot도 작아짐), ② V/div를 파형이 화면을 꽉 채우게 맞춰 ADC 양자화·잡음 비율 개선(클리핑은 금지), ③ 측정 통계로 분포를 보고 잡음 바닥(신호 없는 구간의 pk 편차)과 비교, ④ BW limit은 잡음을 줄이지만 링잉도 깎는다 — overshoot 측정에선 신중히(BW limit의 정당한 사용은 HWLAB-B08 참고).
꼬리질문: "averaging과 high-res(boxcar) 모드 차이는?" → averaging은 여러 획을 같은 시점끼리 평균(반복 신호 필요), high-res는 한 획 안에서 인접 샘플을 평균해 수직 해상도를 올리는 대신 유효 대역폭이 줄어든다.

</details>

<details>
<summary>Q6. 기능은 정상인데 overshoot 24 %, Vmax 4.09 V. 고쳐야 하나요? 무엇을 바꾸나요?</summary>

고쳐야 한다. 3.3 V 수신 핀의 절대 최대 정격(보통 VDDIO + 0.3 V)을 넘으면 ESD 보호 다이오드가 켜져 전류가 수신 칩 전원 레일로 주입된다 — 래치업, 장기 신뢰성 저하, 다른 레일 back-powering(HWLAB-B02 참고). "지금 동작" 은 상온·이 보드 하나의 결과일 뿐, FF 코너·저온에서 에지가 더 빨라지면 더 나빠진다. 대책: 송신단 직렬 종단(Rs + R_drv ≈ Z0, 보통 22~33 Ω) 실장 확인, 드라이버 drive strength/slew rate 레지스터를 낮춤, 트레이스 길이·스텁 검토. 고친 뒤 같은 셋업으로 재측정해 수치로 닫는다.
꼬리질문: "링잉이 수신 임계를 다시 넘으면?" → 에지가 임계 영역에서 비단조(non-monotonic)가 되어 이중 클러킹(double clocking)이 생긴다. 이 레슨의 SN0042는 첫 되울림이 약 0.95·A(≈ 3.12 V)라 VIH 아래로 안 내려가지만, ζ가 더 작거나 임계가 높으면 바로 기능 버그가 된다.

</details>

<details>
<summary>Q7. duty cycle과 주파수는 어느 레벨에서 재야 하나요? 스코프 주파수로 크리스털 ppm을 판정해도 되나요?</summary>

기본은 50 %(mid) 기준 레벨이지만, 데이터시트 스펙이 정의한 레벨(예: VDD/2, LVTTL 1.5 V)이 있으면 그것으로 잰다. rise와 fall이 다르면 기준 레벨에 따라 duty가 달라지기 때문이다. 주파수는 1 µs 레코드 8주기에서 평균 주기로 구하므로 스코프 타임베이스 정확도(대개 수 ppm, 모델마다 다름)와 짧은 레코드가 한계다 — 크리스털 ±20 ppm 판정은 주파수 카운터나 긴 게이트 시간으로 한다. 이 레슨의 ±1 % 스펙은 "엉뚱한 클럭 소스/분주비가 아닌지" 확인용이다.

</details>

## 9. 추가 연습

### 연습 1 — settling time

상승 에지의 50 % 시각부터, 신호가 top ± band·A 안에 **계속 머무르기 시작하는** 시각까지를 구하는 `settling_time(t, v, trs, top, base, band)`. ζ = 0.6(±2 %)와 ζ = 0.45(±5 %)에서 해석 응답을 1 ps 간격으로 훑은 값과 20 ps 이내로 맞춘다.

<details>
<summary>답 보기</summary>

post-transition 구간에서 `|v − top| > band·A` 인 **마지막** 샘플 j를 찾고, j와 j+1 사이에서 band 경계(위로 벗어났으면 top + band·A, 아래면 top − band·A) 교차를 보간한 뒤 t_mid를 뺀다. "처음 band 안에 들어온 시각" 으로 하면 링잉이 다시 band 밖으로 나가는 경우를 놓친다. 실제 출력: ζ=0.6 ±2 % → 4.8648 ns(해석 4.8640 ns), ζ=0.45 ±5 % → 4.2265 ns(해석 4.2250 ns). 2차 응답의 근사식 t_s ≈ −ln(band)/(ζωn) 은 포락선 기준이라 실제값과 다를 수 있다 — 그래서 해석 응답을 직접 훑어 비교했다.

</details>

### 연습 2 — 쌍봉이 아니면 min/max로 후퇴

`robust_levels(v)`: 아래/위 절반 각각에서 최빈 칸 개수가 그 절반 평균 칸 개수의 4배 이상이면 히스토그램, 아니면 `'minmax'`. 사다리꼴 → histogram, 삼각파 → minmax, 사인파 → histogram이지만 결과가 피크의 1 % 이내.

<details>
<summary>답 보기</summary>

삼각파는 모든 전압에 같은 시간 머물러 히스토그램이 평평하다 → mode가 무의미, 후퇴. 사인파는 확률밀도가 1/√(1−x²) 꼴(아크사인 분포)이라 양 끝 칸에 샘플이 몰려 mode가 피크 근처에 생긴다. 실제 출력: sine base=0.0107, top=3.2893 (피크 0/3.3 대비 ~11 mV). 4배라는 임계는 경험값이다 — 스코프 벤더도 이런 판단 규칙을 쓰지만 정확한 기준은 공개하지 않는 경우가 많다. 리포트에 실제 쓴 방법('histogram'/'minmax')을 같이 남기는 것이 핵심이다.

</details>

### 연습 3 — 클리핑 검사

SN0042 캡처를 500 mV/div(화면 −0.35 … 3.65 V, 8-bit)로 다시 찍었다고 가정(`requantize`)하고, 최댓값/최솟값 코드가 3샘플 이상 연속이면 잘렸다고 보는 `clip_check(v)` 를 작성한다. 원래 캡처는 `(False, False)`, 500 mV/div는 `(True, True)`.

<details>
<summary>답 보기</summary>

잘린 캡처의 overshoot는 10.1 %로 나온다(참 23.9 %) — 화면 끝 3.634 V가 "최댓값" 이 되었기 때문이다. 스펙 10 %에 아슬아슬하게 걸려서, 클리핑을 모르면 "경계선 문제" 로 오판한다. 규칙: overshoot·Vmax를 판정하는 캡처는 파형 전체가 화면 안에 있어야 하고, 리포트에 V/div와 클립 여부를 남긴다. 많은 스코프가 클리핑 시 측정값에 경고 표시를 붙이지만, PyVISA로 원시 데이터를 받아 자체 분석할 땐 이 검사를 직접 해야 한다.

</details>

### 연습 4 — 에지별 통계

스코프 측정 통계처럼 에지별 10–90 % 시간의 n/mean/std/min/max를 내는 `edge_statistics(trs, direction)`. SN0042: 상승 8개, 평균 == `measure()['rise']`, 표준편차 < 50 ps; 하강 8개, 평균 차 < 50 ps.

<details>
<summary>답 보기</summary>

`edge_durations` 결과로 표본 표준편차(n−1)를 계산한다. 실제 출력: n=8 mean=1.4934 ns std=3.7 ps min=1.4886 ns max=1.4989 ns. 에지 지터(15 ps rms)는 에지 전체를 옮길 뿐 10→90 간격은 바꾸지 않아서 rise 분산은 잡음(6 mV)·보간 오차 수준이다. 반대로 std가 크면 에지마다 모양이 다르다는 뜻 — 데이터 패턴 의존(ISI), 크로스토크, 전원 바운스를 의심한다. 스펙 판정은 평균이 아니라 **최악(max)** 으로 한다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"      # 10/10 passed, exit=0
```

## 10. 핵심 정리

- 모든 펄스 측정은 상태 레벨에서 시작한다: **top/base = 히스토그램 mode(가장 오래 머문 전압)**, min/max가 아니다.
- 기준 레벨 10/50/90 % = base + {0.1, 0.5, 0.9}·A. 교차 시각은 샘플 사이 선형 보간. 상승: 10 %를 마지막으로 떠난 시각 → 90 %에 처음 닿은 시각.
- 에지 검출은 10 %/90 % 히스테리시스 상태기계 — 링잉·잡음을 에지로 세지 않는다.
- overshoot = (post-transition max − top)/A, undershoot = (base − min)/A, 에지별 최악값. min/max 방법에선 overshoot가 **정의상 0**.
- naive 붕괴 조건: 대칭 OS > 12.5 % → naive 10 % 레벨이 base 아래 → rise가 앞 에지 링잉부터 측정(1.64 → 47 ns).
- 잡음은 overshoot를 σ·√(2 ln N)/A 만큼 위로 민다. 클리핑은 아래로 깎는다. 리포트에 방법·V/div·프로브를 적는다.
- RC 에지: t_r = τ·ln 9 ≈ 2.2τ, BW ≈ 0.35/t_r (1차 가정).
- 리포트는 "무엇이 스펙을 얼마나 넘었고, 측정 체인은 배제했고, 무엇을 바꾸면 되는지" 를 한 줄로 (R47 0 Ω → 33 Ω).

## 참고자료

- IEEE Std 181-2011, *IEEE Standard for Transitions, Pulses, and Related Waveforms* — 상태 레벨(히스토그램 기반·peak 기반), 기준 레벨, 전이 시간, aberration(overshoot/undershoot/preshoot), 펄스 지속시간 정의
- [MATLAB Signal Processing Toolbox — `statelevels`](https://www.mathworks.com/help/signal/ref/statelevels.html) · [`risetime`](https://www.mathworks.com/help/signal/ref/risetime.html) · [`overshoot`](https://www.mathworks.com/help/signal/ref/overshoot.html) — IEEE 181 기반 구현과 옵션 설명 (비교용 레퍼런스)
- Howard Johnson, Martin Graham, *High-Speed Digital Design: A Handbook of Black Magic* — 상승 시간·대역폭, 프로브 그라운드 루프, 직렬 종단
- Eric Bogatin, *Signal and Power Integrity — Simplified* — 반사, 링잉, 송신단 직렬 종단
- Horowitz & Hill, *The Art of Electronics*, 3rd ed. — RC/RLC 계단 응답, 스코프 프로브
- 스코프 벤더 측정 정의 문서: Keysight Infiniium / InfiniiVision 사용자 설명서의 Top/Base·Rise Time·Overshoot 정의, Tektronix 측정 레퍼런스의 High/Low 방법(Auto/Histogram/Min-Max)
- [PyVISA documentation](https://pyvisa.readthedocs.io/) — SCPI로 파형(프리앰블 + 데이터) 가져오기
- 관련 레슨: HWLAB-B05(스코프 BW·프로브·그라운드 리드), HWLAB-B06(트리거 상태기계), HWLAB-B08(리플·BW limit), HWLAB-B02(로직 레벨·back-powering), HWLAB-B09(golden 보드 비교)
