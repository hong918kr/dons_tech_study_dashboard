---
id: HWLAB-M01
time: 80분
updated: 2026-11-08
---
# HWLAB-M01 전송선로 반사: Bounce Diagram·TDR 해석·종단

## 학습 목표

- 반사계수 ρ = (ZL − Z0)/(ZL + Z0) 하나로 **bounce(lattice) 다이어그램**을 손으로 풀고, 같은 물리를 선로를 칸으로 나눈 **격자 시뮬레이터**로 구현해 두 결과가 1e-9 V까지 같음을 확인한다 (15 Ω 드라이버 + 50 Ω + 열린 끝 → 5.077 V, overshoot 53.8 %).
- TDR 파형을 임피던스 프로파일로 바꾸고(Z = Z0·(1+ρ)/(1−ρ)), 거리 = v·t/2, v = c/√εeff 로 불연속 위치를 찾는 **해석기**를 작성한다.
- 순진한 Z(t) 읽기가 **어디서 왜 틀리는지** 숫자로 보인다: 앞 불연속의 가림(75 Ω가 67.9 Ω로), 다중 반사 유령(50 Ω가 54.4 Ω로) → layer peeling으로 0.01 Ω 이내 복원, 상승시간이 짧은 구간을 얕게 만드는 정도(erf 모델), εeff 가정 오차(+15 %).
- 15 Ω 드라이버 + 50 Ω 트레이스에서 직렬 종단 R을 sweep해 **overshoot 최소점(35 Ω = Z0 − Rdrv → E24 33 Ω)** 을 찾고, 직렬/병렬/Thevenin/AC 종단의 전력·지연·레벨 trade-off 표를 시뮬레이션 값으로 만든다.
- 두 보드의 TDR을 비교해 "`Z=68.0Ω @ 11.81cm len 1.50cm, golden에 없음`" 같은 **행동 가능한 한 줄 리포트**를 낸다.

## 1. 문제

보드 bring-up에서 클럭이 링잉하거나 한 보드만 링크가 불안정할 때, "TDR에서 이 dip은 뭐고 몇 cm 지점인가, 종단은 왜 그 값인가" 는 Rivian/NVIDIA/Apple 보드·신호무결성(SI) 검증 면접의 단골 질문이다 — 이 레슨은 그 답을 계산과 코드로 만든다.

선로의 편도 지연 td가 에지 시간 tr에 비해 무시할 수 없으면(대표 경험칙: td가 tr의 1/6~1/4를 넘으면) 배선은 더 이상 "전선" 이 아니라 **전송선로(transmission line)** 다. 에지는 선로를 따라 v = c/√εeff 로 진행하고, 임피던스가 바뀌는 곳마다 일부가 되돌아온다. 이번 레슨의 넷은 15 cm FR4 스트립라인(εeff ≈ 4.0 → 6.67 ps/mm, td ≈ 1.0 ns)에 0.5 ns 에지라서 확실히 전송선로다.

```svg
<svg viewBox="0 0 760 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwm01-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">Bounce(lattice) 다이어그램: 3.3 V 계단, Rdrv 15 Ω, Z0 50 Ω, td 1 ns, 열린 끝</text>
  <line x1="170" y1="40" x2="170" y2="335" stroke="currentColor" stroke-width="2"/>
  <line x1="490" y1="40" x2="490" y2="335" stroke="currentColor" stroke-width="2"/>
  <text x="170" y="36" fill="currentColor" text-anchor="middle" font-size="12">소스 ρS = −0.538</text>
  <text x="490" y="36" fill="currentColor" text-anchor="middle" font-size="12">부하(열림) ρL = +1</text>
  <g stroke="currentColor" fill="none" marker-end="url(#hwm01-a)">
    <line x1="170" y1="50" x2="488" y2="80"/><line x1="490" y1="80" x2="172" y2="110"/>
    <line x1="170" y1="110" x2="488" y2="140"/><line x1="490" y1="140" x2="172" y2="170"/>
    <line x1="170" y1="170" x2="488" y2="200"/><line x1="490" y1="200" x2="172" y2="230"/>
    <line x1="170" y1="230" x2="488" y2="260"/><line x1="490" y1="260" x2="172" y2="290"/>
    <line x1="170" y1="290" x2="488" y2="320"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="330" y="60">+2.538</text><text x="330" y="91">+2.538</text>
    <text x="330" y="120">−1.367</text><text x="330" y="151">−1.367</text>
    <text x="330" y="180">+0.736</text><text x="330" y="211">+0.736</text>
    <text x="330" y="240">−0.396</text><text x="330" y="271">−0.396</text>
    <text x="330" y="300">+0.213</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="160" y="54">0 ns  2.538</text><text x="160" y="114">2 ns  3.710</text><text x="160" y="174">4 ns  3.079</text>
    <text x="160" y="234">6 ns  3.419</text><text x="160" y="294">8 ns  3.236</text>
  </g>
  <g font-size="12">
    <text x="500" y="84" class="f-warn" fill="currentColor">1 ns  5.077 V (+53.8 %)</text>
    <text x="500" y="144" fill="currentColor">3 ns  2.343 V</text><text x="500" y="204" fill="currentColor">5 ns  3.815 V</text>
    <text x="500" y="264" fill="currentColor">7 ns  3.023 V</text><text x="500" y="324" fill="currentColor">9 ns  3.449 V → 3.3 V</text>
  </g>
  <text x="10" y="356" fill="currentColor" font-size="12">launch = 3.3·50/(15+50) = 2.538 V (선로는 반사가 돌아올 때까지 Z0 로 보인다) · 각 끝: 새 전압 = 이전 + 도착파 + 반사파</text>
  <text x="10" y="374" fill="currentColor" font-size="12">왼쪽 = 소스 노드 전압, 오른쪽 = 부하 전압. 오차는 |ρS·ρL| = 0.538 배씩 줄어든다 (왕복 2 ns 마다)</text>
</svg>
```

### Part A — ρ, bounce 다이어그램, 격자 시뮬레이터

- `rho(zl, z0)` — 열림(inf) +1, 단락 −1.
- `bounce_diagram(vs, rs, z0, rl, td, t_end)` — 균일 선로 하나, 저항 소스·저항 부하. 시간순 이벤트 `dict(t, end, wave, refl, v)`.
- `simulate(z, dt, vs_fn, rs, load, n_steps)` — 선로를 편도 지연 dt인 칸으로 나누고, 칸마다 다른 임피던스 z[i]를 허용하는 격자(lattice) 시뮬레이터. 끝에는 주어진 `Load`(저항 shunt, 수신 핀 Cin, AC RC)를 단다.
- 확인: 위 그림의 숫자, 수렴값 vs·RL/(Rs+RL), 20칸 시뮬레이터 == 해석 lattice, 직렬 정합 시 소스 노드의 **Vs/2 선반(shelf)**, 정합 소스 + 용량 부하의 RC 응답.

### Part B — TDR 해석기와 "순진한 Z(t)" 의 한계

- `z_profile(v, vinc)` — TDR 전압 → 겉보기 임피던스, `tdr_distance(t, εeff)` — v·t/2.
- `layer_peel(v_rt, vinc, z_ref)` — 다중 반사를 한 칸씩 벗겨 실제 임피던스 프로파일 복원.
- `find_events(t, v, vinc, εeff, tr)` — |Z − 50| > 4 Ω 인 구간을 'segment' / 'short' / 'open' / 'end-C' 로 분류하고 위치·Z·길이(+ 끝 Cin)를 낸다.
- 정답이 알려진 합성 선로로 검증: 50|30|75|50 Ω 계단(가림과 유령), 3 mm 35 Ω 패드를 tr = 20/100 ps로(해상도), 10.00 cm 쿠폰으로 εeff 교정.

### Part C — VALBRD-A `FPGA_CLK_50M`: TDR 판정 + 종단 설계

가상의 보드 **VALBRD-A rev B**. 클럭 버퍼 U12(LVCMOS33, 출력 임피던스 15 Ω — 대표값, IBIS로 확인 필요) → L3 스트립라인 50 Ω 설계, 15.0 cm → FPGA U1 클럭 핀(Cin 4 pF 대표값). 6.0 cm에 레이어 전환 비아.

1. U12 미실장 패드에 50 Ω TDR 프로브를 대고 찍은 CSV 두 장(`SN0041_fpga_clk_tdr.csv` golden, `SN0042_fpga_clk_tdr.csv` 의심)을 `find_events` → `compare_tdr` 로 비교해 리포트한다. **SN0042에는 11.8 cm부터 1.5 cm 길이의 68 Ω 구간을 심어 두었다**(기준면 void/split 위를 지나가거나 트레이스 neck-down이면 이렇게 높아진다). 판정 스펙: Z0 = 50 Ω ±10 %.
2. 직렬 종단 R을 0~60 Ω sweep해 overshoot가 바닥에 처음 닿는 R(knee)을 찾고 E24 값을 고른다.
3. 50 MHz 클럭으로 none / series 33 Ω / parallel 50 Ω / Thevenin 100‖100 Ω / AC 50 Ω+100 pF 를 시뮬레이션해 레벨·overshoot·지연·전원 전력·드라이버 DC 전류 표를 만든다.

## 2. Problem (English)

> **Reflections on a lossless transmission line: simulate, invert TDR, choose a termination.** Python 3 stdlib only.
>
> 1. `rho(zl, z0)` and `bounce_diagram(vs, rs, z0, rl, td, t_end)` for a single uniform line with resistive source and
>    load: list every arrival `(t, end, wave, refl, v)`. Example: `vs=3.3, rs=15, z0=50, rl=inf, td=1ns` gives load
>    voltages 5.077, 2.343, 3.815, 3.023, 3.449 V at 1, 3, 5, 7, 9 ns.
> 2. `simulate(z, dt, vs_fn, rs, load, n_steps)`: a lattice (scattering) simulator over cells of equal one-way delay
>    `dt` and arbitrary impedances `z[i]`; junction scattering `f' = (1+ρ)f − ρb`, `b' = ρf + (1−ρ)b`. Must reproduce (1).
> 3. TDR: with a matched source and incident step `vinc`, convert voltage to apparent impedance
>    `Z = Z0·(v/vinc)/(2 − v/vinc)`, distance `x = (c/√εeff)·t/2`. Write `layer_peel` that recovers the true stepped
>    profile from an ideal TDR (show the naive reading is wrong after the first discontinuity), and `find_events` that
>    reports segments (Z, position, length), short features, and a capacitive line end (with Cin from the area).
> 4. Driver `Rdrv = 15 Ω`, 50 Ω × 15 cm (εeff = 4), receiver `Cin = 4 pF`, 0.5 ns edge: sweep a series resistor
>    0–60 Ω and return the smallest R at which overshoot reaches its floor (expected `Z0 − Rdrv = 35 Ω`).
>    Tabulate power/delay/levels for series, parallel, Thevenin and AC terminations on a 50 MHz clock.
> 5. Compare a suspect board's TDR with a golden one and print a one-line actionable verdict.

## 3. 시작 코드

<!-- code: starter.py -->

Part C TDR 캡처 생성기 (결정적 — 다시 돌려도 CSV가 바이트 단위로 같다. 정답 생성기라 solution.py를 import하지 않고 같은 물리를 따로 구현):

<!-- code: gen_tdr.py -->

```bash
cd lessons/HWLAB/HWLAB-M01
python3 -I starter.py ; echo "exit=$?"     # TODO 상태: 1/26 passed, exit=1
```

실제 출력 (일부):

```text
-- Part A: rho / bounce diagram / lattice simulator --
FAIL  rho: open=+1, short=-1, 75 on 50 = +0.2, 15 on 50 = -0.5385: returned False
FAIL  bounce 15R drv/50R/open: load = 5.077, 2.343, 3.815, 3.023, 3.449 V: load=[]
...
FAIL  simulate(20 cells) == bounce lattice at t = 1,3,5,7,9 ns: sim=['0.0000', '0.0000', '0.0000', '0.0000', '0.0000']
...
-- Part C: VALBRD-A FPGA_CLK_50M: TDR golden vs suspect, series-R sweep, termination table --
PASS  vinc estimated 0.250 V +-1 mV from the cable plateau   [0.2500 V]
...
1/26 passed
exit=1
```

> 주어진 것: `Load`(선로 끝 회로 — 사다리꼴 companion model), `dc_level`, 소스 파형(`step_fn`/`ramp_fn`/`clock_fn`), `gauss_filter`(TDR 상승시간), `cross_time`(선형 보간 교차, HWLAB-B04), `find_events` 의 구간(run) 찾기, `series_sweep`, `clock_metrics` 의 레벨·지연 측정. TODO: `rho`, `bounce_diagram`, `simulate`, `z_profile`, `layer_peel`, `tdr_distance`, `find_events` 분류, `compare_tdr`, `os_knee`, `clock_metrics` 전력 계산.

## 4. 힌트

<details>
<summary>힌트 1 — 소스가 처음 내보내는 파는 왜 vs·Z0/(Rs+Z0) 인가</summary>

반사가 돌아오기 전(2·td 동안) 드라이버는 선로의 끝을 모른다. 선로 입력은 그냥 저항 Z0로 보인다 → 전압 분배. 그 뒤로 각 끝에서 "도착파 + 반사파" 가 그 끝의 전압 변화다. 열린 끝은 도착파를 그대로 되돌려(ρL = +1) 전압이 두 배로 뛴다.

</details>

<details>
<summary>힌트 2 — 접합 산란 두 줄</summary>

왼쪽 Z1 → 오른쪽 Z2, ρ = (Z2 − Z1)/(Z2 + Z1). 왼쪽에서 오는 파 f는 ρf 반사, (1+ρ)f 투과. 오른쪽에서 오는 파 b가 보는 반사계수는 −ρ, 투과는 1−ρ. 그래서 `새 fwd[i+1] = (1+ρ)f − ρb`, `새 bwd[i] = ρf + (1−ρ)b`. 접합 전압은 양쪽에서 같다: f + 새 bwd[i] = 새 fwd[i+1] + b.

</details>

<details>
<summary>힌트 3 — 소스·부하 노드는 "2f 를 Z0 뒤에 둔 Thevenin"</summary>

도착파 g가 있는 선로는 노드에서 보면 전압원 2g + 직렬 Z0 이다. 그래서 소스 노드 V0 = (vs/rs + 2g/Z0)/(1/rs + 1/Z0), 새로 나가는 파 = V0 − g. 부하도 같고, `Load.solve` 가 이미 해 준다.

</details>

<details>
<summary>힌트 4 — layer peeling: 한 칸 내려갈 때 시간 기준을 옮긴다</summary>

맨 위에서 아래로 가는 파 D = δ(1, 0, 0…), 위로 오는 파 U = 측정 반사 임펄스 h. 한 칸 내려가면 아래로 가는 파는 한 칸 늦게, 위로 오는 파는 한 칸 일찍 보인다 → 첫 도착을 0으로 다시 잡으면 U만 왕복 1칸 앞당기면 된다(`U = U[1:]`). 그러면 U[0]/D[0] 이 바로 그 접합의 ρ 다. 접합 아래쪽 파는 산란식을 거꾸로 풀어 얻는다.

</details>

<details>
<summary>힌트 5 — segment 와 short 를 가르는 기준</summary>

run(|Z − 50| > 4 Ω 연속 구간)의 시간 길이가 2·tr 보다 길면 평탄부가 있다 → run 가운데 절반의 중앙값 전압으로 Z, 시작·끝은 "vinc 와 평탄 전압의 중간 레벨" 교차. 짧으면 평탄부가 없다 → 극값만 있고 그 Z는 상승시간 때문에 실제보다 얕다(`z_reliable=False`). 열린 끝 바로 앞에 붙은 dip은 수신 핀 C다 — 둘을 합쳐 'end-C'.

</details>

<details>
<summary>힌트 6 — 종단 전력은 어디서 재나</summary>

이상 전압원 Vs(t) + 직렬 Rsum 모델에서 드라이버가 VDD에서 끌어오는 전력 ≈ Vs·(Vs − Vsrc)/Rsum (Vs = 0인 동안은 GND로 흘리므로 0). Thevenin 풀업(R1 → 3V3)은 드라이버와 따로 VDD에서 Vt·(Vt − V)/R1 을 끌어온다. 측정 주기(두 번째 주기 20 ns) 평균을 낸다.

</details>

## 5. 풀이

### Part A — 반사계수와 lattice

**반사계수.** 선로 끝에서 전압 V = V⁺ + V⁻, 전류 I = (V⁺ − V⁻)/Z0 = V/ZL 이므로 V⁻/V⁺ = (ZL − Z0)/(ZL + Z0) = ρ. 투과계수 τ = 1 + ρ (전압은 접합 양쪽에서 같다).

| ZL | ρ | 끝 전압 (도착파 V⁺ 기준) | 보이는 것 |
|---|---|---|---|
| 열림 ∞ | +1 | 2V⁺ | 전압 두 배 — 수신 핀(CMOS 입력)은 거의 이것 |
| 단락 0 | −1 | 0 | |
| Z0 | 0 | V⁺ | 반사 없음 (종단의 목표) |
| 15 Ω (드라이버 쪽) | −0.538 | — | 돌아온 파를 반대 부호로 54 % 되돌림 → 링잉 |
| 용량 C | −1 → +1 (τ = Z0·C) | 처음엔 단락, 나중엔 열림 | TDR에서 dip 후 상승 |

**lattice 읽기.** `python3 -I demo_bounce.py` 의 1) 이 위 그림의 숫자다(실제 출력):

```text
   rhoS=-0.5385 rhoL=1 launch=2.5385V
   t(ns) end   arriving   reflected   node V
      0  src    +2.5385    +0.0000    2.5385
      1  load   +2.5385    +2.5385    5.0769
      2  src    +2.5385    -1.3669    3.7101
      3  load   -1.3669    -1.3669    2.3432
      4  src    -1.3669    +0.7360    3.0792
      5  load   +0.7360    +0.7360    3.8152
      ...
      9  load   +0.2134    +0.2134    3.4494
```

부하 전압은 V_load(n번째 도착) = Vfinal·(1 − (ρS·ρL)ⁿ) 꼴이라 오차가 |ρS·ρL|ⁿ 로 줄고, 무한히 합하면 Vs·RL/(Rs+RL) (열린 끝이면 Vs)로 간다 — DC에선 선로가 그냥 전선이라는 것과 일치한다. 15 Ω 드라이버는 Z0보다 **강해서**(Rdrv < Z0) 처음 내보내는 파가 2.538 V로 크고, 열린 끝에서 두 배가 되어 5.08 V(+53.8 %), 수신 칩 절대 최대 정격(대개 VDDIO + 0.3 V)을 크게 넘는다.

**격자 시뮬레이터.** 선로를 편도 지연이 같은 칸(dt = 칸 길이/v)으로 자르면 모든 파가 한 스텝에 한 칸씩 움직인다. 매 스텝 ① 소스 노드, ② 모든 접합(위 힌트 2의 두 줄), ③ 부하 노드를 갱신한다. 칸마다 다른 Z를 허용하므로 비아·void·커넥터를 "짧은 다른 Z 구간" 으로 넣을 수 있다 — Part B·C의 TDR도 이걸로 만든다. 균일 50 Ω 20칸 시뮬레이션이 lattice와 **1e-9 V 이내로 같다**(테스트).

부하 C는 `Load` 가 사다리꼴(trapezoidal) companion model로 푼다: i_{n+1} = (2C/dt)(v_{n+1} − v_n) − i_n. 사다리꼴은 샘플 사이를 직선으로 보므로 샘플 사이에서 뛰는 계단을 "반 샘플 일찍 시작한 램프" 로 본다. 그래서 테스트는 해석해 Vs(1 − e^{−t/Z0C})를 t = (k + 0.5)·dt 에서 비교하고, 오차는 0.253 mV다(반 샘플 보정을 빼면 40.7 mV로 1 %를 넘는다 — 이산화 시각 기준을 맞추는 것도 검증의 일부다).

**직렬 정합의 선반(shelf).** demo 2)의 실제 출력:

```text
   Rs= 0 (Rsum=15): load 5.077 2.343 3.815 3.023 | src 2.538 3.710
   Rs=35 (Rsum=50): load 3.300 3.300 3.300 3.300 | src 1.650 3.300
   Rs=60 (Rsum=75): load 2.640 3.168 3.274 3.295 | src 1.320 2.904
```

Rdrv + Rs = Z0 이면 소스는 절반(1.65 V)을 내보내고, 열린 끝에서 두 배가 되어 **수신단은 한 번에 3.3 V**, 돌아온 반사는 정합 소스에서 흡수되어 끝난다. 대신 소스 근처 선로는 2·td 동안 **1.65 V 선반**에 머문다 — 그래서 직렬 종단은 한 끝에 수신기가 하나인 point-to-point 전용이다(선로 중간의 수신기는 임계 근처에서 2 ns를 머문다). Rsum > Z0(60 Ω)이면 계단식으로 느리게 올라간다(2.640 → 3.168 → …): overshoot는 없지만 첫 입사가 모자라다.

### Part B — TDR: 전압을 임피던스와 거리로

TDR은 정합(50 Ω) 소스로 빠른 계단(대표값: 수백 mV, 상승시간 수십 ps)을 보내고 **같은 노드**에서 V(t) = Vinc·(1 + ρ(t)) 를 본다. ρ를 거꾸로 풀면 Z = Z0·(1+ρ)/(1−ρ) = Z0·(V/Vinc)/(2 − V/Vinc). 시간은 왕복이므로 거리 x = v·t/2, v = c/√εeff. 스트립라인은 유전체가 균일해 εeff = εr, 마이크로스트립은 공기와 나눠 가져 εeff < εr (FR4 εr ≈ 4.0, 50 Ω 마이크로스트립이면 Hammerstad 근사로 εeff ≈ 3.0 정도).

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwm01-b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">가림(masking)과 유령(ghost): 50 | 30 | 75 Ω, 입사 1.000 (Vinc 기준)</text>
  <rect x="40" y="40" width="210" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="250" y="40" width="210" height="34" class="f-ok" stroke="currentColor"/>
  <rect x="460" y="40" width="260" height="34" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle"><text x="145" y="62">50 Ω (기준)</text><text x="355" y="62">30 Ω · 3 cm</text><text x="590" y="62">75 Ω · 3 cm</text></g>
  <g stroke="currentColor" fill="none" marker-end="url(#hwm01-b)">
    <line x1="60" y1="96" x2="246" y2="96"/><line x1="246" y1="114" x2="70" y2="114"/>
    <line x1="254" y1="96" x2="456" y2="96"/><line x1="456" y1="114" x2="258" y2="114"/>
    <line x1="246" y1="138" x2="70" y2="138"/><line x1="254" y1="160" x2="456" y2="160"/>
    <line x1="464" y1="96" x2="700" y2="96"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="70" y="92">입사 1.000</text><text x="80" y="128">J1 반사 ρ1 = −0.250 (정확히 30 Ω 로 읽힘)</text>
    <text x="270" y="92">투과 0.750</text><text x="270" y="110">J2 반사 0.750 × 0.4286 = 0.321</text>
    <text x="80" y="152">위로 투과 0.321 × (1−ρ1) = 0.402 ← TDR 이 보는 J2</text>
    <text x="270" y="176">J1 아래면에서 되반사 0.321 × 0.25 = 0.080 → 다시 J2 … (유령)</text>
    <text x="480" y="92">투과 1.071</text>
  </g>
  <text x="10" y="206" fill="currentColor" font-size="12">V/Vinc = 1 − 0.250 + 0.402 = 1.152 → 순진한 Z = 50·1.152/0.848 = 67.9 Ω (실제 75 Ω)</text>
  <text x="10" y="226" fill="currentColor" font-size="12">J2 반사는 (1+ρ1)(1−ρ1) = 1 − ρ1² = 0.9375 배로 줄어 0.402 로 보이고, 기준선도 0.75 로 내려가 있었다</text>
  <text x="10" y="244" fill="currentColor" font-size="12">순진한 변환은 "반사가 하나뿐" 이라고 가정한다 — 첫 불연속 뒤로는 그 가정이 깨진다</text>
</svg>
```

**순진한 Z(t)가 틀리는 두 방식.** `demo_bounce.py` 3)의 실제 출력(1 mm 칸, 이상 스텝, 정합 끝):

```text
   cell(mm)  truth   naive Z(t)   peeled
      25     30.0      30.00      30.00
      55     75.0      67.89      75.00
      85     50.0      54.36      50.00
```

1. **가림(masking)**: 첫 불연속(30 Ω)은 정확하다. 75 Ω 구간은 67.9 Ω로 읽힌다 — 그 접합에서 실제로 반사된 파가 두 번 1±ρ1 을 곱받아 줄었고, 기준 전압도 0.75로 내려가 있었다(위 그림).
2. **유령(ghost)**: 마지막 50 Ω 구간이 54.4 Ω로 읽힌다. 분해하면 75→50 접합의 실제 반사는 −0.1531(이것만이면 V/Vinc = 0.9987 → 49.9 Ω, 거의 맞음), 그런데 30 Ω 구간 안에서 한 번 더 왕복한 파 +0.0430이 **정확히 같은 시각**(30 Ω 구간 왕복 = 30 칸)에 도착해 1.0417 → 54.4 Ω. 존재하지 않는 "4 Ω 높은 구간" 을 만든다.

**layer peeling.** 칸마다 왕복 지연이 같은 매질(Goupillaud 모델)이면 맨 위 접합부터 하나씩 ρ를 확정하고, 그 접합의 산란을 거꾸로 풀어 "그 아래에서 본 파" 로 바꿔 다음 접합으로 내려간다. 첫 불연속을 정확히 아는 상태에서 다음 것의 가림·유령을 정확히 빼므로 **모든 칸이 2.84e-14 Ω 오차**로 복원된다(테스트). 산란을 거꾸로 푼 식:

| 위쪽(알고 있음) | 아래쪽(구함) |
|---|---|
| D(내려가는 파), U(올라오는 파) | D' = (D − ρU)/(1−ρ), U' = (U − ρD)/(1−ρ) |
| ρ = U[0]/D[0] (시간 기준을 옮긴 뒤의 첫 샘플) | Z_j = Z_{j−1}·(1+ρ)/(1−ρ) |

한계(면접에서 꼭 덧붙일 것): 이상 스텝·무손실·같은 칸 지연을 가정한다. 실제 측정은 대역 제한(상승시간)·손실·잡음이 있어 그대로 넣으면 오차가 아래로 갈수록 누적·증폭된다. 그래서 실무에선 측정 TDR을 정규화(deconvolution, 필터 상승시간 지정)한 뒤 peeling하거나, 상용 TDR 해석 소프트웨어의 다중 반사 보정 기능("true impedance profile" 류)을 쓰고, 무엇보다 **첫 불연속 뒤의 값은 순진하게 읽으면 틀린다**는 것을 알고 읽는다. 첫 불연속이 작을수록(|ρ1| 작음) 가림 오차는 ρ1² 수준으로 작다 — 50 ± 10 % 보드에서 순진한 읽기가 대체로 통하는 이유다.

**상승시간과 해상도.** 짧은 구간(왕복 T_rt)의 반사는 높이 ρ, 폭 T_rt 인 사각 펄스다. TDR 시스템 응답을 가우시안(10–90 % = tr = 2.563σ)으로 보면 펄스 꼭대기는 ρ·erf(T_rt/(2√2·σ)) 로 깎인다. `demo_bounce.py` 4)의 실제 출력(3 mm 35 Ω 패드, T_rt = 40 ps):

```text
   tr(ps)  min Z(ohm)  rho_app  rho*erf(Trt/(2*sqrt2*sigma))
      10      35.00    -0.1765   -0.1765
      20      35.14    -0.1745   -0.1746
      35      36.85    -0.1514   -0.1513
      60      40.24    -0.1082   -0.1072
     100      43.41    -0.0705   -0.0692
     150      45.38    -0.0484   -0.0472
```

모델과 시뮬레이션이 0.002 이내로 맞는다(남는 차이는 패드 안 다중 반사). **tr = 100 ps TDR로 보면 35 Ω 패드가 43 Ω로 보인다** — "dip이 얕다 = 불연속이 작다" 가 아니라 "불연속이 짧다" 일 수 있다. 흔히 쓰는 기준: 두 불연속이 왕복 시간으로 tr/2보다 가까우면 구분이 어렵고, 왕복 시간이 tr보다 짧은 구간의 Z는 믿지 않는다. 그런 짧은 특징은 Z 대신 **면적 → 등가 C(또는 L)** 로 보고한다(연습 3).

**εeff 가정.** 10.00 cm 쿠폰 끝(열림)의 1.5·Vinc 교차가 왕복 1.334 ns → εeff = (c·t/(2L))² = 4.000. 같은 파형을 마이크로스트립이라 착각해 εeff = 3.0을 넣으면 11.55 cm(+15.5 % = √(4/3) − 1). 대책: 길이를 아는 특징(쿠폰, 넷 끝, 커넥터)으로 v를 교정하고, 리포트에 εeff를 적는다. FR4의 εr은 주파수·수지 함량에 따라 달라서(대표값 3.6~4.5) 스택업 문서 값도 교정 대상이다.

| 원인 | 증상 | 드러내는 측정/대책 |
|---|---|---|
| 앞 불연속의 가림 | 뒤 구간 Z가 기준 쪽으로 덜 보임 (75 → 67.9) | layer peeling / 양쪽에서 TDR 해서 비교 |
| 다중 반사 유령 | 없는 계단이 보임 (50 → 54.4) | 유령 시각 = 앞 구간 왕복 시간의 배수인지 확인 |
| 상승시간 부족 | 짧은 비아·패드가 얕게 보임 (35 → 43 Ω) | 빠른 TDR, 면적 → C_eq 로 보고 |
| εeff 오가정 | 위치가 √(ε가정/ε실제) 배 | 길이를 아는 특징으로 교정 |
| 손실(도체·유전) | 긴 선로에서 기준선이 서서히 오름 | 짧은 구간끼리 비교, 손실 보정 |
| 기준면(t=0) 불명확 | 모든 위치가 일정량 밀림 | 프로브 open/short로 기준면 잡기 |

### Part C — VALBRD-A 판정과 종단 설계

**TDR 리포트.** `python3 -I solution.py` 의 실제 출력:

```text
SN0042 FPGA_CLK_50M TDR -> FAIL
  short    @   6.06cm  Z_app=42.0ohm (short feature: depth limited by tr)
  segment  @  11.81cm  Z=68.0ohm over 1.50cm   <-- NEW
  end-C    @  14.98cm  line end, Cin~3.93pF
  FAIL net=FPGA_CLK_50M Z=68.0ohm (spec 50+-10%) @ 11.81cm len 1.50cm, not in golden SN0041 -> plane void/split or neck-down: check L2/L4 artwork + X-section there
```

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">FPGA_CLK_50M TDR 임피던스 프로파일 (스케치): SN0042 실선 vs golden SN0041 점선</text>
  <line x1="60" y1="270" x2="740" y2="270" stroke="currentColor"/>
  <line x1="60" y1="60" x2="60" y2="270" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="end"><text x="54" y="152">68 Ω</text><text x="54" y="224">50 Ω</text><text x="54" y="268">32 Ω</text><text x="54" y="72">open</text></g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="60" y="288">0</text><text x="315" y="288">6 cm</text><text x="562" y="288">11.8</text><text x="626" y="288">13.3</text><text x="697" y="288">15 cm</text></g>
  <polyline fill="none" stroke="currentColor" stroke-dasharray="5 4" points="560,220 697,220"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="60,220 310,220 316,242 322,220 556,220 566,148 620,148 630,220 694,220 700,262 706,236 712,150 720,80 740,64"/>
  <g class="f-warn" stroke="currentColor"><rect x="566" y="148" width="54" height="72" opacity=".35"/></g>
  <g fill="currentColor" font-size="12">
    <text x="190" y="262">via @6.06 cm: Z_app 42 Ω (실제 1 mm·32 Ω)</text>
    <text x="190" y="278">→ tr 보다 짧아 얕다. C_eq 0.123 pF (연습 3)</text>
    <text x="440" y="120">NEW: 68.0 Ω × 1.50 cm @ 11.81 cm</text>
    <text x="440" y="136">(golden 은 50 Ω — 점선)</text>
    <text x="545" y="58">U1 핀: dip → open, Cin ≈ 4 pF</text>
  </g>
  <text x="70" y="44" fill="currentColor" font-size="12">x = v·t/2, v = c/√4.0 = 15.0 cm/ns · t=0 은 프로브 팁 기준면</text>
</svg>
```

| 이벤트 | 측정 | 생성기 참값 | 읽는 법 |
|---|---|---|---|
| 비아 (short) | 6.06 cm, Z_app 42.3 Ω (golden) | 6.00–6.10 cm, 32 Ω | 왕복 13 ps < tr 35 ps → Z 신뢰 불가, 위치만. 용량성(dip) |
| 68 Ω 구간 (SN0042만) | 11.81 cm, 1.50 cm, 67.97 Ω | 11.80 cm, 1.50 cm, 68 Ω | 스펙(50 ± 10 %) 밖 + golden에 없음 → **결함** |
| 선로 끝 end-C | 14.98 cm, Cin 4.06 pF (golden) / 3.93 pF | 15.00 cm, 4 pF | ∫(2Vinc − V)dt = 2Vinc·Z0·C (끝 C의 충전 면적) |

end-C의 Cin 추정: 끝에 C만 있으면 V(t) = 2Vinc − 2Vinc·e^{−t/(Z0C)} 꼴이라 "열림까지 모자란 면적" 이 2Vinc·Z0·C다. SN0042는 68 Ω 구간과 끝 사이를 오가는 다중 반사가 적분 구간에 섞여 3.93 pF로 나온다(±10 % 안).

**68 Ω는 무엇일 수 있나.** 스트립라인 Z는 트레이스 폭·유전체 두께·기준면에 달려 있다. 기준면에 void(비아 antipad 군집, split)가 있으면 그 구간의 C가 줄고 L이 늘어 Z가 오른다. 트레이스가 좁게 식각되었거나(neck-down, 부품 밑 BGA breakout 규칙이 잘못 적용) 다른 레이어로 잘못 라우팅돼도 같다. 리포트는 위치(11.8 cm)와 길이(1.5 cm)를 주므로 하드웨어 엔지니어가 아트워크의 그 구간을 바로 열어 볼 수 있다. 같은 패널의 임피던스 쿠폰과 cross-section으로 확인한다. 이 결함 하나로 수신단에서 생기는 반사는 ρ = 0.15 정도라 50 MHz 클럭에선 기능 문제가 안 될 수 있지만, **같은 넷 패턴이 고속 레인에 있으면** 문제다 — 그래서 "PASS/FAIL + 위치" 를 남긴다.

**직렬 R sweep.** `python3 -I solution.py --tables` 의 실제 출력:

```text
series R sweep @FPGA_CLK_50M (Rdrv=15ohm, Z0=50ohm, 15cm, Cin=4pF, edge 0.5ns):
   R    OS%     tpd(ns)  tsettle5%(ns)
    0   59.61    1.073    12.78
   10   36.42    1.104     6.07
   20   19.09    1.134     3.84
   25   11.96    1.148     3.75
   30    5.64    1.163     3.49
   33    2.18    1.171     1.83
   34    1.08    1.174     1.85
   35    0.00    1.177     1.88
   36    0.00    1.180     1.93
   39    0.00    1.188     2.17
   47    0.00    1.211     3.84
   60    0.00    1.246     4.32
  knee (OS floor reached) R=35ohm (Z0-Rdrv=35), E24 below -> 33ohm
```

- overshoot는 Rsum < Z0 동안 줄어들다가 **Rsum = Z0(R = 35 Ω)에서 바닥(0 %)** 에 닿고, 그 뒤로는 0인 채 지연만 늘어난다. 그래서 "최소점" 은 평탄한 바닥이 시작되는 knee다 — 그보다 큰 R은 얻는 것 없이 에지만 느리게 한다(60 Ω: tpd +70 ps, 정착 4.3 ns).
- ±5 % 정착 시간은 33~35 Ω에서 가장 짧다(1.83~1.88 ns): 작으면 링잉이 길고, 크면 계단이 길다.
- R = 0에서 OS가 해석값 53.8 %보다 큰 **59.6 %** 인 이유: Cin은 처음에 단락처럼 보여 음(−)의 반사 스파이크를 돌려보내고, 15 Ω 소스가 이걸 −0.538배로 되반사해 **양(+)의 스파이크** 로 바꿔 3·td에 다시 보낸다. Cin = 0이면 정확히 53.85 %(테스트로 확인).
- 실무 선택: **E24 33 Ω**(OS 2.2 %). Rdrv는 PVT에 따라 변하므로(대표값 10~25 Ω) 코너를 넣어 다시 고른다 — 연습 1에서 창이 [30.9, 36.1] Ω으로 나오고 33 Ω이 모든 코너를 통과한다. Rdrv 값 자체는 IBIS 모델의 V-I 곡선(pull-up/pull-down) 기울기에서 읽는다.

```svg
<svg viewBox="0 0 760 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">종단 방식 (U12 → 50 Ω · 15 cm → U1) 과 시뮬레이션 결과</text>
  <g stroke="currentColor">
    <polygon points="20,48 20,72 42,60" class="f-mut"/><line x1="42" y1="60" x2="130" y2="60"/><rect x="130" y="55" width="160" height="10" class="f-acc"/><line x1="290" y1="60" x2="330" y2="60"/><polygon points="330,48 330,72 352,60" class="f-mut"/>
    <polygon points="20,118 20,142 42,130" class="f-mut"/><line x1="42" y1="130" x2="70" y2="130"/><rect x="70" y="124" width="40" height="12" class="f-ok"/><line x1="110" y1="130" x2="130" y2="130"/><rect x="130" y="125" width="160" height="10" class="f-acc"/><line x1="290" y1="130" x2="330" y2="130"/><polygon points="330,118 330,142 352,130" class="f-mut"/>
    <polygon points="20,188 20,212 42,200" class="f-mut"/><line x1="42" y1="200" x2="130" y2="200"/><rect x="130" y="195" width="160" height="10" class="f-acc"/><line x1="290" y1="200" x2="330" y2="200"/><polygon points="330,188 330,212 352,200" class="f-mut"/>
    <line x1="310" y1="200" x2="310" y2="206"/><rect x="304" y="206" width="12" height="18" class="f-ok"/><line x1="300" y1="228" x2="320" y2="228"/><line x1="304" y1="232" x2="316" y2="232"/>
    <polygon points="20,258 20,282 42,270" class="f-mut"/><line x1="42" y1="270" x2="130" y2="270"/><rect x="130" y="265" width="160" height="10" class="f-acc"/><line x1="290" y1="270" x2="330" y2="270"/><polygon points="330,258 330,282 352,270" class="f-mut"/>
    <rect x="304" y="243" width="12" height="18" class="f-ok"/><line x1="310" y1="261" x2="310" y2="270"/><line x1="310" y1="270" x2="310" y2="276"/><rect x="304" y="276" width="12" height="16" class="f-ok"/><line x1="300" y1="296" x2="320" y2="296"/>
    <polygon points="20,328 20,352 42,340" class="f-mut"/><line x1="42" y1="340" x2="130" y2="340"/><rect x="130" y="335" width="160" height="10" class="f-acc"/><line x1="290" y1="340" x2="330" y2="340"/><polygon points="330,328 330,352 352,340" class="f-mut"/>
    <line x1="310" y1="340" x2="310" y2="345"/><rect x="304" y="345" width="12" height="14" class="f-ok"/><line x1="310" y1="359" x2="310" y2="363"/><line x1="300" y1="363" x2="320" y2="363"/><line x1="300" y1="368" x2="320" y2="368"/><line x1="310" y1="368" x2="310" y2="372"/><line x1="302" y1="376" x2="318" y2="376"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="40">none</text><text x="20" y="110">series 33 Ω @U12</text><text x="20" y="180">parallel 50 Ω → GND @U1</text>
    <text x="20" y="250">Thevenin 100 Ω → 3V3 ‖ 100 Ω → GND</text><text x="20" y="320">AC 50 Ω + 100 pF → GND</text>
    <text x="324" y="242">3V3</text><text x="326" y="372">100 pF</text>
    <text x="380" y="64">OS 74.9 % (정착 안 됨) · 10 mW · Idc 0</text>
    <text x="380" y="134">OS 2.2 % · 12.1 mW · Idc 0 · 소스 쪽 선반</text>
    <text x="380" y="204">0..2.54 V · OS 10.3 % · 83.6 mW · Idc 50.8 mA</text>
    <text x="380" y="274">0.38..2.92 V · OS 10.3 % · 96.2 mW · Idc 25.4 mA</text>
    <text x="380" y="344">0.08..3.22 V · OS 2.6 % · 45.8 mW · Idc 0</text>
  </g>
</svg>
```

**종단 trade-off 표** (`--tables` 실제 출력, 50 MHz, 두 번째 주기에서 측정):

```text
  scheme    parts                         Vlo..Vhi(V)    OS%   tpd(ns)  P_supply(mW) P_term(mW) Idrv_dc(mA)
  none      -                             0.22..3.08    74.9   1.081       10.3        0.0       0.0
  series    Rs=33R @U12                   0.00..3.30     2.2   1.171       12.1        8.3       0.0
  parallel  50R->GND @U1                  0.00..2.54    10.3   1.174       83.6       63.9      50.8
  thevenin  100R->3V3 + 100R->GND @U1     0.38..2.92    10.3   1.097       96.2       86.1      25.4
  ac        50R+100pF->GND @U1            0.08..3.22     2.6   1.160       45.8       31.5       0.0
```

| 방식 | 장점 | 대가 (이 넷의 숫자) | 언제 |
|---|---|---|---|
| 직렬 (소스) | 전력 최소(12 mW는 선로 20 pF + Cin 4 pF 충방전 C·V²·f), 레벨 full swing | 소스 근처 2·td 동안 Vs/2 선반 → 수신기는 끝에 하나만. Rdrv PVT 변동에 민감 | point-to-point 클럭·SPI·LVCMOS |
| 병렬 (끝, GND) | 한 번에 정착, 다중 수신기 가능 | **Vhi 2.54 V**(15 Ω와 50 Ω 분압: VIH 2.0 V 대비 여유 0.54 V), 84 mW, 하이에서 51 mA DC — 대부분 LVCMOS 드라이버 IOH 정격을 넘는다(데이터시트 확인) | 거의 안 씀(LVCMOS) — 버스·저전압 차동 쪽 |
| Thevenin (100‖100) | 드라이버 DC 전류 절반(25 mA), Vth 1.65 V 중심 | 상태와 무관하게 분압기로 전류가 흐름 → 96 mW로 오히려 최대, Vlo 0.38 V | 레거시 버스, 바이어스 중심이 필요한 곳 |
| AC (R + C) | DC 전력 없음, 끝 정합 | 전력 ∝ C·V²·f — 50 MHz에선 46 mW, C 선택에 민감(아래) | 저속·저듀티 클럭, 끝 정합이 꼭 필요할 때 |

- **병렬/Thevenin의 OS 10.3 %** 는 종단이 틀려서가 아니다 — Cin 4 pF가 끝에서 음의 반사를 만들고, **15 Ω 드라이버가 그걸 되반사** 해서 3·td에 혹으로 돌아온다. Cin = 0으로 돌리면 OS 0.00 %(테스트). 끝 정합만으로는 "드라이버 쪽 부정합" 이 남는다는 것, 그리고 직렬 정합은 돌아오는 모든 반사를 소스에서 흡수한다는 것이 이 표의 핵심 교훈이다.
- **AC 종단의 C.** 실제 출력:

```text
AC termination C sweep (R=50ohm): RC vs 2*td = 2.00ns
   C(pF)  RC(ns)  Vlo..Vhi(V)   OS%   P_supply(mW)
     22    1.10   0.05..3.25   32.2     23.2
     47    2.35   -0.02..3.32   10.8     36.7
    100    5.00   0.08..3.22    2.6     45.8
    220   11.00   0.22..3.04    4.0     50.5
```

  RC가 왕복 지연(2·td = 2 ns)보다 충분히 크지 않으면 C가 에지 도중에 차서 끝이 다시 열림처럼 보인다(22 pF: OS 32 %). 너무 크면 반주기(10 ns) 안에 다 못 차서 레벨이 줄고 전력이 는다(220 pF). 대표 경험칙 "RC ≥ 2·td 의 몇 배, 반주기보다는 충분히 작게" 를 이 넷에선 100 pF로 만족시켰다.
- **지연(tpd)** 은 방식 간 차이가 수십 ps다: 1차적으로 선로 지연(1.0 ns) + 에지 반(0.25 ns 기준점 차이) + Cin 충전. Thevenin이 가장 빠른 건 로 레벨이 0.38 V에서 출발해 1.65 V 교차가 빨리 오기 때문이다(측정 기준을 같이 적어야 비교가 된다).
- **판정 문장:** `FPGA_CLK_50M: no-term OS 75% (Vmax > abs max) → series 33Ω @U12 (knee 35Ω = Z0−Rdrv): OS 2.2%, tpd 1.17ns, 12mW; parallel 50Ω would add +71mW, Vhi 2.54V, 51mA DC.`

**장비/도구가 있을 때 (이 레슨에선 실행하지 않음):** 샘플링 스코프 TDR 모듈이나 VNA(S11 → 역 FFT로 TDR, 상승시간은 최대 주파수·윈도우가 정함)로 찍고, 위 CSV와 같은 (t, V) 형식으로 내보내 `find_events` 에 넣는다. 기준면은 프로브를 허공(open)에 두고 한 번 찍어 그 반사 에지를 t = 0으로 잡는다. 실장 보드에서 TDR을 할 땐 드라이버를 빼거나(이 레슨의 가정) 전원을 끈 상태에서 ESD 다이오드 영향을 감안한다. ngspice 같은 회로 시뮬레이터의 무손실 선로(T 소자)로도 같은 bounce를 재현할 수 있다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"        # 26/26 passed, exit=0  (실측 0.81 s)
python3 -I solution.py --tables                # §5 Part C 의 sweep 표·종단 표·AC C 표
python3 -I -W error -X dev solution.py         # 경고 없음 (exit 0)
python3 -I gen_tdr.py                          # CSV 재생성 → sha 동일 (결정적, 0.2 s)
python3 -I demo_bounce.py                      # §5 의 lattice 표·peeling 표·해상도 표
```

<!-- code: demo_bounce.py -->

데이터 크기: TDR CSV 2장 각 1119 샘플(dt = 3.336 ps, 0.5 mm 칸), 약 24 KB. 종단 시뮬레이션은 2.5 mm 칸(dt = 16.7 ps, 60칸); 1.25 mm 칸으로 줄여도 sweep 값(59.61 / 27.17 / 2.18 / 0.00 %)이 같게 나와 칸 크기에 수렴했다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드/행동 |
|---|---|---|
| 드라이버가 처음 내보내는 파를 Vs로 | 반사가 오기 전 선로는 Z0 → 분압 | `w = vs*z0/(rs+z0)` |
| 수렴값을 lattice 마지막 값으로 | 유한 항에서 끊으면 아직 진동 중 | DC 분압 Vs·RL/(Rs+RL) 로 검증 |
| 접합에서 오른쪽→왼쪽 반사도 +ρ | 반대 방향은 −ρ, 투과 1−ρ | `nb[i] = r*f + (1-r)*b` |
| TDR 시간을 거리로 v·t | 왕복이다 | `v*t/2` |
| 스트립라인에 마이크로스트립 εeff(또는 반대) | 위치가 √비율만큼 틀림 (+15 %) | 길이 아는 특징으로 v 교정 |
| 첫 불연속 뒤 Z를 그대로 믿음 | 가림·유령 (75 → 67.9, 50 → 54.4) | layer peeling, 양방향 TDR, 유령 시각 확인 |
| 짧은 비아 dip 깊이로 Z 보고 | tr이 깎는다 (32 Ω → 42 Ω) | 면적 → C_eq, "short feature" 표시 |
| 끝의 dip을 별도 결함으로 보고 | 수신 핀 C의 정상 모습 | 열린 끝과 붙은 dip은 end-C로 합침 |
| 직렬 종단을 Rs = Z0로 | 드라이버 출력 임피던스를 빼야 | Rs = Z0 − Rdrv (IBIS V-I 기울기) |
| 직렬 종단 넷 중간에 수신기 추가 | 2·td 동안 Vs/2 선반 → 이중 클러킹 위험 | point-to-point만, 다중이면 끝 종단 |
| 병렬 종단을 레벨 확인 없이 | 15 Ω:50 Ω 분압 → Vhi 2.54 V, 51 mA | VIH/IOH 정격 대조, 직렬 종단 고려 |
| 시뮬레이터 이산화 시각을 무시 | 사다리꼴은 계단을 반 샘플 일찍 본다 (40.7 mV 오차) | 비교 시각을 (k+0.5)·dt 로, 칸 크기 수렴 확인 |

## 8. 면접 Q&A

<details>
<summary>Q1. TDR에서 6 cm 지점에 dip이 보입니다. 무엇이고, 거리는 어떻게 구했나요?</summary>

dip(Z 감소)은 용량성 불연속이다 — 비아 패드, 커넥터 핀, 부품 패드, 스텁. 거리는 기준면(프로브 팁)에서 잰 왕복 시간 t에 대해 x = v·t/2, v = c/√εeff. 스트립라인이면 εeff = εr(FR4 대표 4.0 → 6.67 ps/mm, 약 170 ps/inch), 마이크로스트립은 그보다 작다. εeff는 길이를 아는 특징(넷 끝, 쿠폰)으로 교정한다.
꼬리질문: "dip이 42 Ω로 보이면 비아가 42 Ω인가?" → 아니다. 비아의 왕복 시간(약 13 ps)이 TDR 상승시간(35 ps)보다 짧으면 깊이가 ρ·erf(T_rt/(2√2σ)) 로 깎인다. 이 레슨의 실제 비아는 32 Ω이었다. 짧은 특징은 면적으로 등가 C를 보고한다: C_eq = −2∫(V − Vinc)dt/(Z0·Vinc) = 0.123 pF.

</details>

<details>
<summary>Q2. 직렬 종단 저항값은 어떻게 정하나요? 왜 보통 22~33 Ω인가요?</summary>

Rs = Z0 − Rdrv. 드라이버 출력 임피던스는 IBIS 모델의 pull-up/pull-down V-I 곡선 기울기(동작점 근처)로 얻는다. LVCMOS 드라이버가 대략 15~30 Ω(드라이브 강도 설정에 따라 다름)이라 50 Ω 선로에서 22~33 Ω가 흔하다. 이 레슨에선 sweep으로 overshoot가 바닥에 닿는 knee가 정확히 35 Ω, E24로 33 Ω(OS 2.2 %)를 골랐다.
꼬리질문: "Rs를 더 크게 하면 더 안전하지 않나?" → overshoot는 이미 0이고 지연만 는다(60 Ω: tpd +70 ps, 정착 4.3 ns). 첫 입사가 2Z0/(Rsum+Z0)·VDD 로 줄어 느린 코너에서 VIH를 첫 스텝에 못 넘으면 에지가 임계 근처에서 머문다. PVT 코너를 넣어 창을 구한다(연습 1: [30.9, 36.1] Ω).

</details>

<details>
<summary>Q3. 수신단에 50 Ω 병렬 종단을 달았는데도 overshoot가 10 % 나옵니다. 왜죠?</summary>

끝은 R만 있는 게 아니라 R‖Cin이다. Cin이 에지 순간 음의 반사를 만들고, 그 반사가 드라이버에 도착했을 때 15 Ω 드라이버(ρS = −0.54)가 그걸 양의 혹으로 되돌린다. 시뮬레이션에서 Cin을 0으로 하면 OS 0.00 %, 4 pF면 10.3 %. 해결은 소스도 정합(직렬 R)하거나 에지를 느리게(슬루율 설정) 하는 것.
꼬리질문: "그럼 양끝을 다 정합하면?" → 반사는 최소지만 수신단 진폭이 절반이 된다(분압) — 저전압 차동 표준처럼 그걸 전제로 설계된 인터페이스가 아니면 LVCMOS에선 레벨이 부족하다.

</details>

<details>
<summary>Q4. TDR 임피던스 프로파일에서 두 번째 불연속 뒤의 값은 믿을 수 있나요?</summary>

그대로는 아니다. Z = Z0·(1+ρ)/(1−ρ) 변환은 반사가 하나라고 가정한다. 첫 불연속이 지나가는 파와 돌아오는 파를 (1 ± ρ1) 만큼 바꿔서(가림), 이 레슨의 50|30|75 Ω에서 75 Ω가 67.9 Ω로 읽혔다. 또 앞 구간 안에서 한 번 더 왕복한 파가 뒤늦게 도착해 없는 계단을 만든다(유령: 50 Ω가 54.4 Ω). 이상 데이터면 layer peeling이 정확히 복원한다(오차 1e-14 Ω).
꼬리질문: "실측에 layer peeling을 바로 쓰면?" → 대역 제한·손실·잡음 때문에 아래 칸으로 갈수록 오차가 누적된다. 상승시간을 정하고 정규화한 뒤 쓰거나, 반대쪽 끝에서도 TDR을 찍어 비교한다. |ρ1|이 작으면(±10 % 보드) 가림 오차는 ρ1² 수준이라 순진한 읽기도 대체로 쓸 만하다.

</details>

<details>
<summary>Q5. 언제 배선을 전송선로로 봐야 하나요? 클럭 주파수가 낮으면 괜찮나요?</summary>

주파수가 아니라 **에지 시간** 이 기준이다. 편도 지연이 상승시간의 일정 비율(대표 경험칙 1/6~1/4)을 넘으면 반사가 에지 위에 보인다. 1 MHz 클럭이라도 에지가 0.5 ns면 15 cm 선로(1 ns)는 전송선로다 — 이 레슨의 R = 0 결과(OS 59.6 %)가 그 상황이다.
꼬리질문: "에지를 느리게 하면?" → 드라이버 슬루율/드라이브 강도를 낮추면 반사가 에지 안에 묻혀 링잉이 줄고 EMI도 준다. 대가는 지연·지터 민감도(느린 에지는 잡음이 시간 오차로 더 크게 바뀐다).

</details>

<details>
<summary>Q6. 스코프로 본 링잉이 반사 때문인지 프로브 때문인지 어떻게 가르나요?</summary>

링잉 주기를 본다. 선로 반사라면 주기가 왕복 지연과 묶여 있다(열린 끝 + 강한 드라이버면 4·td 주기 — 이 넷은 약 4 ns, 250 MHz). 프로브 그라운드 리드 공진이면 1/(2π√(L_lead·C_tip))로 리드 길이에 따라 바뀐다(HWLAB-B05). 스프링 팁으로 바꿔도 같으면 신호다. 측정점도 중요하다: 수신 핀 바로 옆과 소스 쪽 TP의 파형은 반사 때문에 다르다(직렬 종단 넷의 소스 쪽은 원래 선반이 보인다).
꼬리질문: "그 overshoot 수치는 어떻게 계산된 값인가?" → top/base 상태 레벨을 히스토그램으로 잡고 (max − top)/A — HWLAB-B04.

</details>

<details>
<summary>Q7. AC 종단의 C는 어떻게 고르고, 전력이 정말 0인가요?</summary>

DC 전력만 0이다. 매 에지마다 C를 충방전하므로 전력 ≈ C·V²·f 수준이고 50 MHz에선 46 mW(100 pF)로 병렬 종단(84 mW)의 절반이나 된다. C는 RC가 왕복 지연보다 충분히 커야(22 pF는 RC 1.1 ns < 2·td라 OS 32 %) 에지 동안 R로 보이고, 반주기보다 충분히 작아야 레벨이 유지된다(220 pF는 Vhi 3.04 V로 감소).
꼬리질문: "데이터 라인에 AC 종단을 쓰면?" → 같은 비트가 오래 이어지면 C가 그 레벨로 차서 다음 에지에 종단이 제 역할을 못 하고(패턴 의존), 평균 레벨이 데이터 패턴을 따라 움직인다. 50 % duty 클럭에 더 맞는 이유.

</details>

<details>
<summary>Q8. 같은 넷이 보드마다 TDR이 다르면 무엇부터 의심하고, 리포트에 무엇을 적나요?</summary>

같은 셋업(프로브, 기준면, 상승시간)으로 golden과 비교해 **새 이벤트** 를 찾는다. 위치·길이·Z를 리포트하고 원인 후보(기준면 void/split, neck-down, 잘못된 레이어, 프리프레그 두께)를 적어 아트워크와 cross-section, 같은 패널의 임피던스 쿠폰으로 확인한다. 리포트 예: `FAIL net=FPGA_CLK_50M Z=68.0ohm (spec 50+-10%) @ 11.81cm len 1.50cm, not in golden SN0041`.
꼬리질문: "쿠폰은 PASS인데 보드가 FAIL이면?" → 쿠폰은 레이어 스택(두께·εr·식각)을 대표할 뿐 라우팅 국부 문제(void 위 통과, BGA 밑 neck-down)는 못 잡는다. 국부 결함이 위치를 가진다는 게 TDR의 가치다.

</details>

## 9. 추가 연습

### 연습 1 — PVT 코너에 강한 직렬 R

드라이버 출력 임피던스가 코너에 따라 10 Ω(빠름)~25 Ω(느림)이다. 빠른 코너에서 overshoot ≤ 10 %, 느린 코너에서 첫 입사 ≥ 0.9·VDD를 동시에 만족하는 R의 창을 열린 끝 저항 모델로 구하고, E24 값 하나를 골라 Cin 4 pF 시뮬레이션으로 세 코너를 확인한다.

<details>
<summary>답 보기</summary>

첫 입사 = 2Z0/(Rsum+Z0)·VDD. OS ≤ 0.1 → Rsum ≥ 2·50/1.1 − 50 = 40.9 → R ≥ 30.9 Ω (Rdrv 10). 첫 입사 ≥ 0.9 → Rsum ≤ 100/0.9 − 50 = 61.1 → R ≤ 36.1 Ω (Rdrv 25). E24 후보 33, 36 중 창 가운데에 가까운 33 Ω. 실제 출력: `Rdrv=10: OS=8.08% first=1.073 | Rdrv=15: OS=2.18% first=1.018 | Rdrv=25: OS=0.00% first=0.924` — Cin 때문에 빠른 코너 OS가 저항 모델(7.5 %)보다 조금 크지만 10 % 안이다. 이렇게 "코너별 창" 을 리포트에 남기면 드라이브 강도 레지스터를 바꿀 때 다시 계산할 수 있다.

</details>

### 연습 2 — TDR 해상도: 몇 mm부터 Z를 믿나

35 Ω 패드를 tr = 35 ps TDR로 볼 때 겉보기 Z가 실제와 1 Ω 이내가 되는 최소 길이를 erf 모델로 구하고, 0.5 mm 칸 시뮬레이션(3.0 mm vs 4.0 mm)으로 확인한다. tr = 100 ps면?

<details>
<summary>답 보기</summary>

36 Ω의 ρ = −0.1628, 35 Ω의 ρ = −0.1765 → 필요한 비율 0.9225 = erf(x) → x = 1.24 → T_rt = 2√2·σ·x, σ = 35/2.563 = 13.7 ps → T_rt ≈ 48 ps → L = v·T_rt/2 = **3.61 mm**. tr = 100 ps면 비례해서 10.32 mm. 시뮬레이션 실제 출력: 3.0 mm → 36.85 Ω(1 Ω 넘게 틀림), 4.0 mm → 35.65 Ω. 즉 이 TDR로 BGA 패드·비아(1~2 mm)의 Z는 못 읽는다 — 그 범위는 등가 C/L 이나 3D 전자기 해석으로 다룬다.

</details>

### 연습 3 — 비아의 등가 C를 dip 면적으로

golden CSV의 5.5~6.6 cm 구간에서 C_eq = −2∫(V − Vinc)dt / (Z0·Vinc)를 구하고, 생성기의 참값(1 mm, 32 Ω)과 비교한다.

<details>
<summary>답 보기</summary>

짧은 구간의 저주파 반사계수 Γ(s) ≈ s·Td·(Zv − Z0²/Zv)/(2Z0) 이므로 반사 계단 응답의 면적이 Vinc·Td·(Zv − Z0²/Zv)/(2Z0), 즉 C_eq = Td·(1/Zv − Zv/Z0²) = 6.671 ps × (1/32 − 32/2500) = **0.1231 pF**. 이는 "초과 C − 초과 L/Z0²" 이다(짧은 저Z 구간은 C가 늘고 L이 준다). 측정 실제 출력: `meas=0.1231pF truth=0.1231pF (-0.0%)`. 면적은 가우시안 필터가 보존하므로 상승시간과 무관하다 — 그래서 dip 깊이(42 Ω, 틀림) 대신 면적을 보고한다. 잡음 0.3 mV가 적분 구간(약 147 ps, 44 샘플)에서 평균돼 면적 오차는 1 % 미만이다.

</details>

### 연습 4 — bounce 정착 시간

15 Ω 드라이버 + 50 Ω + 열린 끝에서 부하가 ±5 % 안에 들어오는 시각을 |ρS·ρL|ⁿ ≤ ε 로 구하고 `bounce_diagram` 으로 확인한다.

<details>
<summary>답 보기</summary>

부하 오차 = Vfinal·|ρSρL|ⁿ (n = 부하 도착 횟수). n ≥ ln 0.05 / ln 0.538 = 4.84 → n = 5 → t = (2n − 1)·td = 9 ns. 실제 출력: `n=5 arrivals -> settled at 9 ns`, 오차 열 0.538, 0.290, 0.156, 0.084, 0.045. 50 MHz(반주기 10 ns)에선 종단 없이 반주기의 90 %를 링잉으로 보낸다는 뜻 — 종단 없음 행의 Vhi(3.08 V, 정착 안 됨)와 같은 이야기다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"      # 6/6 passed, exit=0  (0.2 s)
```

## 10. 핵심 정리

- ρ = (ZL − Z0)/(ZL + Z0), τ = 1 + ρ. 드라이버는 처음에 Vs·Z0/(Rs+Z0)를 보낸다. 열린 끝 +1, 단락 −1, C는 −1 → +1.
- bounce: 각 끝에서 새 전압 = 이전 + 도착파 + 반사파. 오차는 왕복마다 |ρSρL| 배. 15 Ω/50 Ω/열림 → 5.077 V(+53.8 %).
- TDR: Z = Z0·(V/Vinc)/(2 − V/Vinc), 거리 = v·t/2, v = c/√εeff (스트립라인 εeff = εr). εeff는 길이 아는 특징으로 교정.
- 첫 불연속 뒤 Z(t)는 가림(1 − ρ1²)과 유령 때문에 틀린다(75 → 67.9, 50 → 54.4) → layer peeling. 짧은 특징은 ρ·erf(T_rt/(2√2σ))로 얕아진다 → 면적으로 C_eq.
- 직렬 종단 Rs = Z0 − Rdrv(IBIS) — overshoot 바닥이 시작되는 knee(35 Ω → E24 33 Ω). point-to-point 전용(소스 쪽 Vs/2 선반).
- 병렬은 레벨·DC 전류(2.54 V, 51 mA), Thevenin은 상시 전력(96 mW), AC는 C·V²·f와 RC 선택, 끝 정합만으론 Cin 반사가 강한 드라이버에서 되돌아온다(OS 10 %).
- 리포트 = 위치·길이·Z·golden 대비·원인 후보 한 줄: `Z=68.0Ω @ 11.81cm len 1.50cm, golden에 없음 → void/neck-down 확인`.

## 참고자료

- Eric Bogatin, *Signal and Power Integrity — Simplified* (3rd ed.) — 반사, bounce diagram, TDR 해석(가림·다중 반사), 종단 전략
- Howard Johnson & Martin Graham, *High-Speed Digital Design: A Handbook of Black Magic* — 전송선로 판정 기준, 직렬/병렬/Thevenin/AC 종단
- Stephen H. Hall & Howard L. Heck, *Advanced Signal Integrity for High-Speed Digital Designs* — 전송선로 이론, 반사 계산
- Agilent/Keysight Application Note 1304-2, *Time Domain Reflectometry Theory* — TDR 원리, 상승시간과 해상도
- Tektronix, *TDR Impedance Measurements: A Foundation for Signal Integrity* — TDR 측정 실무(기준면, 다중 반사)
- M. N. Goupillaud, "An approach to inverse filtering of near-surface layer effects from seismic records", *Geophysics*, 1961 — 같은 지연 층 매질 모델(layer peeling의 바탕)
- IPC-2141A, *Design Guide for High-Speed Controlled Impedance Circuit Boards* — 마이크로스트립/스트립라인 임피던스·εeff
- IBIS Open Forum, *IBIS Specification* — 드라이버 V-I/V-t 곡선(출력 임피던스 추정)
- 관련 레슨: HWLAB-B04(교차 보간·overshoot 측정), HWLAB-B05(프로브 그라운드 리드 링잉), HWLAB-B02(로직 레벨·VIH), HWLAB-B09(golden 보드 비교), HWLAB-M03(PDN 임피던스), HWLAB-M12(채널 ISI·이퀄라이저), HWLAB-M09(eye diagram)
