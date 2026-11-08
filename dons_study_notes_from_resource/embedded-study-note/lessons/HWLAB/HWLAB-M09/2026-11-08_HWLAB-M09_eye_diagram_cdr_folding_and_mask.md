---
id: HWLAB-M09
time: 90분
updated: 2026-11-08
---
# HWLAB-M09 Eye Diagram 생성: CDR 기준 폴딩·Eye Height/Width·마스크

## 학습 목표

- 긴 직렬 캡처를 **복원 클럭 경계마다 잘라 겹치는(folding)** 과정을 직접 구현한다: 샘플 시각 → UI 내 위상 → crossing 원형 평균(circular mean)으로 eye 중심 정렬 → 2D 히스토그램.
- 스코프의 NRZ eye 지표를 정의대로 계산한다: eye height(3σ·inner), eye width(6σ·pp), crossing %, 폴리곤 마스크 hit 수·마스크 여유(margin). 정답을 아는 파형(무ISI 램프, RJ만, 1차 채널 ISI, DCD)으로 검증한다.
- **"어떤 클럭으로 접었는가"가 eye를 바꾼다**는 것을 수치로 보인다: 공칭 클럭(EW_pp 0) vs 최소제곱 이상 클럭(0.525 UI) vs golden PLL(0.870 UI). PLL이 eye에 남기는 지터 = |1−H(f)| 고역 통과임을 주입한 SJ로 확인한다.
- 레인 3개의 마스크 판정 리포트를 만들고 T-스텁 echo(ISI)와 DCD(crossing %)를 잡아낸다. 같은 정상 레인을 이상 클럭으로 접으면 **거짓 FAIL**이 난다는 것도 보인다.

## 1. 문제

SerDes 레인 bring-up과 컴플라이언스의 첫 화면은 eye diagram이고, NVIDIA/Apple/Rivian 실리콘·보드 검증 면접은 "eye를 직접 만들어 보라 — 클럭은 어디서 오나?"를 묻는다. 스코프 앱 버튼을 누르는 것과 그 숫자가 어떤 클럭·어떤 정의로 나왔는지 설명하는 것은 다른 능력이다.

eye diagram은 고속 직렬 파형을 **1 UI(unit interval, 비트 하나의 시간)마다 잘라 겹친 그림**이다. 자르는 기준이 클럭인데, 데이터 레인에는 클럭이 따로 오지 않는 경우가 많다(embedded clock). 그래서 수신기는 CDR(clock and data recovery)로 데이터 에지에서 클럭을 뽑고, CDR은 **느린 지터(주파수 오프셋, SSC, 저주파 wander)를 따라간다**. 측정 장비도 수신기처럼 접어야 수신기가 실제로 보는 eye가 나온다. 그래서 표준은 "기준 CDR(golden PLL)로 클럭을 복원해 접어라"라고 정한다. 이 레슨은 Fibre Channel 계열의 대표값인 **1차 PLL, BW = bitrate/1667**(대표값 — 데이터시트/스펙 확인 필요)을 쓴다.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">캡처 → 복원 클럭 경계마다 자르기 → 겹치기(folding) → eye 지표·마스크</text>
  <g stroke="currentColor" stroke-dasharray="3 4" opacity=".55">
    <line x1="30" y1="45" x2="30" y2="125"/><line x1="100" y1="45" x2="100" y2="125"/><line x1="170" y1="45" x2="170" y2="125"/>
    <line x1="240" y1="45" x2="240" y2="125"/><line x1="310" y1="45" x2="310" y2="125"/><line x1="380" y1="45" x2="380" y2="125"/>
    <line x1="450" y1="45" x2="450" y2="125"/><line x1="520" y1="45" x2="520" y2="125"/><line x1="590" y1="45" x2="590" y2="125"/>
    <line x1="660" y1="45" x2="660" y2="125"/><line x1="730" y1="45" x2="730" y2="125"/>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="30,60 163,60 177,110 233,110 247,60 303,60 317,110 513,110 527,60 653,60 667,110 730,110"/>
  <text x="30" y="142" fill="currentColor" font-size="12">점선 = 복원 클럭 경계 clk[j] (golden PLL), 간격 1 UI = 200 ps @ 5 Gb/s · 파형 = PRBS7 NRZ ±0.4 V</text>
  <line x1="380" y1="150" x2="380" y2="168" stroke="currentColor"/><path d="M375 162 L380 170 L385 162" fill="none" stroke="currentColor"/>
  <rect x="250" y="175" width="260" height="110" class="f-mut" stroke="currentColor" opacity=".35"/>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <line x1="275" y1="180" x2="485" y2="180"/><line x1="275" y1="280" x2="485" y2="280"/>
    <path d="M250 230 L275 180 M250 230 L275 280 M485 180 L510 230 M485 280 L510 230"/>
  </g>
  <g stroke="currentColor" fill="none" opacity=".45">
    <path d="M254 230 L281 180 M246 230 L271 280 M479 180 L506 230 M489 280 L514 230"/>
  </g>
  <polygon points="302,230 341,211 419,211 458,230 419,249 341,249" class="f-warn" stroke="currentColor" opacity=".7"/>
  <line x1="380" y1="175" x2="380" y2="285" stroke="currentColor" stroke-dasharray="4 3"/>
  <g stroke="currentColor"><line x1="236" y1="186" x2="236" y2="274"/><line x1="231" y1="186" x2="241" y2="186"/><line x1="231" y1="274" x2="241" y2="274"/>
    <line x1="262" y1="300" x2="498" y2="300"/><line x1="262" y1="295" x2="262" y2="305"/><line x1="498" y1="295" x2="498" y2="305"/></g>
  <g fill="currentColor" font-size="12">
    <text x="196" y="234">EH</text><text x="372" y="318">EW</text>
    <text x="40" y="184">μ1 ('1' 레벨)</text><text x="40" y="284">μ0 ('0' 레벨)</text><text x="40" y="234">crossing (x = ±0.5)</text>
    <text x="522" y="186">x=0: eye 중심 (crossing+0.5UI)</text>
    <text x="522" y="206">EH=(μ1−3σ1)−(μ0+3σ0)</text>
    <text x="522" y="222">   중심 ±0.1 UI 창에서</text>
    <text x="522" y="242">EW = 1 UI − 6σx (Vx 레벨)</text>
    <text x="522" y="262">cross% = (Vx−μ0)/(μ1−μ0)</text>
    <text x="522" y="282">육각형 = 마스크, 안 = hit</text>
  </g>
</svg>
```

캡처 조건 (모든 Part 공통, 코드 안에서 결정적으로 생성 — 파일로 저장하지 않는다):

| 항목 | 값 | 비고 |
|---|---|---|
| 비트레이트 / UI | 5 Gb/s / 200 ps | `eye_spec.json` |
| 샘플링 | 16 샘플/UI, dt = 12.5 ps | 80 GS/s 실시간 스코프 상당 |
| 패턴 / 진폭 | PRBS7 (x⁷+x⁶+1), ±0.4 V (800 mVppd) | 주기 127, 최장 런 1×7 / 0×6 |
| 채널 | FIR h[m] = rᵐ (1차 RC 손실, τ = 0.28 UI) + 선택적 echo 탭 | NRZ 미분(에지 임펄스) × step 응답 G로 합성 |
| 지터 (Part B/C) | +100 ppm, RJ 1.5 ps rms(0.0075 UI), SJ 500 kHz 0.2 UI 피크 | 수직 잡음 5 mV rms |
| 레코드 | 16384 비트 = 3.28 µs, 262,166 샘플 | PLL 락 전 2000 UI는 버림 → 229,972 점 |

### Part A — 폴딩·2D 히스토그램·eye 지표

- `phases(times, clk, skip_ui)` — 정렬된 시각들을 클럭 경계 목록 `clk`로 접어 UI 내 위상 u ∈ [0,1)로. 포인터 하나로 O(N).
- `eye_hist(xs, vs, nx, ny, vlo, vhi)` — eye 좌표(x ∈ [−0.5, 0.5) UI, v) 점들 → 2D 히스토그램 (row 0 = 맨 위).
- `eye_metrics(v, dt, clk, skip_ui, win)` — 0 V crossing의 **원형 평균**으로 eye 중심을 잡고 EH(3σ), EH_inner, crossing 레벨·%, crossing 레벨에서의 σx·pp → EW(6σ), EW_pp.
- 정답 확인: 송신 경계(지터 없는 정답 클럭)로 접어서 — 램프 무ISI: EH_inner = 0.800 V, EW_pp = 1.000 UI, 50 %; RJ 0.01 UI만: σx ≈ 0.01, EW = 0.94; 1차 채널: EH_inner = 해석식 2A(1 − 2e^(−s/τ)); DCD 0.05 UI(램프 0.5 UI): cross% = 55 %.

### Part B — 공칭 클럭 vs 이상 클럭(최소제곱) vs golden PLL

- `ideal_clock_ls(edges, ui_nom)` — 에지에 UI 번호를 붙이고(인접 간격 누적) t ≈ a + T·n 최소제곱. 주입 +100 ppm을 0.5 ppm 이내로.
- `golden_pll(edges, ui, bw, n_ui)` — 1차 PLL: 경계마다 φ += UI + kp·Σe, kp = 2π·bw·UI/D. 500 kHz SJ의 잔류가 해석값 A·f/√(f²+bw²) = 0.0329 UI와 5 % 이내.
- 같은 캡처를 세 클럭으로 접어 비교: 공칭(닫힘) / 이상 클럭(SJ가 그대로 남음) / golden PLL. 수평이 번지면 중심 창 EH도 나빠지는 것까지.

### Part C — VALBRD-S rev A, J7 세 레인 마스크 판정

가상의 보드 **VALBRD-S rev A**. SoC SerDes TX 3 레인이 커넥터 J7로 나간다. 사내 스펙(`eye_spec.json`): EH ≥ 300 mV, EW ≥ 0.70 UI, crossing 45–55 %, 육각형 마스크(±0.30 UI / ±0.15 V) hit 0. 심어 둔 결함:

- **TX1**: alt-BOM(대체 부품) 패드로 가는 **2.2 cm T-스텁** → 스텁 끝 반사가 1.5 UI 뒤에 +25 % echo로 도착 (FIR echo 탭).
- **TX2**: TX 드라이버 트림 로드 누락 → 상승 에지 −0.05 UI / 하강 +0.05 UI, **DCD 0.10 UI**.

`point_in_poly` (ray casting), `judge`를 구현해 레인별 한 줄 리포트를 낸다. 같은 TX0 캡처를 이상 클럭으로 접으면 어떻게 판정되는지도 같이 찍는다.

## 2. Problem (English)

> **Build an eye diagram from a long serial-data capture and run a mask test.**
>
> You get a uniformly sampled NRZ waveform `v` (volts, `dt` = 12.5 ps, 5 Gb/s PRBS7 through a lossy channel with RJ, SJ,
> frequency offset and noise). Using only the Python 3 standard library:
>
> 1. `phases(times, clk, skip_ui=0) -> list[float|None]`: fold sorted time stamps onto a recovered clock given as a list of
>    UI boundary times; return the phase in `[0, 1)` or `None` outside the clock or before `skip_ui`. O(N) total.
> 2. `eye_metrics(v, dt, clk, skip_ui, win=0.2) -> dict`: align the eye so that `x = 0` is the circular mean of the 0 V
>    crossing phases plus 0.5 UI; compute eye height `(mu1 - 3 s1) - (mu0 + 3 s0)` in the center `win` window, inner height,
>    crossing level and crossing percentage, and eye width `1 - 6*sigma_x` (and `1 - pp`) from crossings at the crossing level.
> 3. `ideal_clock_ls(edges, ui_nom) -> (a, T)` (constant-frequency least-squares clock) and
>    `golden_pll(edges, ui, bw, n_ui) -> clk` (first-order PLL, loop bandwidth `bw = bitrate/1667`).
> 4. `point_in_poly(x, y, poly)` by ray casting, then count mask hits and judge each lane against a JSON spec.
>
> Example: a jitter-free ramp eye must give EH_inner = 0.800 V, EW_pp = 1.000 UI, crossing 50 %. With 500 kHz SJ of
> 0.2 UI peak, the golden PLL must leave `0.2 * f / sqrt(f^2 + bw^2) = 0.0329 UI` in the eye, and the eye folded with the
> PLL must be at least 0.25 UI wider than the one folded with the least-squares ideal clock.

## 3. 시작 코드

<!-- code: starter.py -->

스펙 파일 (판정 한계와 마스크 폴리곤 — 테스트는 이 파일과 자기일관적이다):

<!-- code: eye_spec.json -->

```bash
cd lessons/HWLAB/HWLAB-M09
python3 -I starter.py ; echo "exit=$?"      # TODO 상태: 2/21 passed, exit=1
```

실제 출력 (일부):

```text
-- Part A: folding / 2D histogram / eye metrics (truth clock) --
PASS  prbs7: period 127, 64 ones, longest run 1x7 / 0x6
FAIL  phases(): clk [0,1,2,3], t=[-.5,0,.25,1.5,2.999,3.2] -> [None,0,.25,.5,.999,None]: returned False
FAIL  phases(skip_ui=2) drops UI 0,1: returned False
      (setup failed: NotImplementedError: eye_metrics)
...
FAIL  ideal_clock_ls recovers +100ppm within 0.5ppm (from waveform crossings): RJ only: +0.00ppm (injected +100) | with 500kHz SJ: +0.00ppm
FAIL  golden PLL residual of 500kHz SJ == A*f/sqrt(f^2+BW^2) within 5%: residual SJ=0.1636UI analytic |1-H|*A=0.0329UI (397.6%)
PASS  golden PLL passes 30MHz SJ (10x BW) almost untouched   [30MHz SJ passes: 0.0500UI analytic 0.0498UI]
...
2/21 passed  (1.4 s)
exit=1
```

> 눈여겨볼 것: starter의 `golden_pll` 은 추적 없는 고정 클럭인데도 "30 MHz SJ 통과" 테스트는 PASS다. 대역 밖(10×BW) 지터는 PLL이든 고정 클럭이든 그대로 eye에 남기 때문이다. **고주파 SJ만으로는 CDR을 검증할 수 없다** — 대역 안(500 kHz) 잔류를 봐야 PLL이 일을 하는지 드러난다.

## 4. 힌트

<details>
<summary>힌트 1 — 폴딩은 "포인터 하나"</summary>

샘플 시각도 클럭 경계도 정렬돼 있다. 매 샘플마다 이분 탐색할 필요 없이 `while j < last and clk[j+1] <= t: j += 1` 로 포인터만 앞으로 민다. 26만 샘플 × O(1). u = (t − clk[j]) / (clk[j+1] − clk[j]) — 경계 간격으로 나누므로 PLL이 UI를 늘였다 줄였다 해도 u ∈ [0,1)이다.

</details>

<details>
<summary>힌트 2 — crossing 위치는 원형 평균으로</summary>

crossing 위상은 0 근처에 몰리는데 0.98과 0.02가 섞여 있다. 산술 평균은 0.5(정반대!)를 준다. 위상을 단위원 위 각도 2πu로 보고 `atan2(Σsin, Σcos)/2π` 를 쓰면 0.0이 나온다. 그 다음 x = ((u − c) mod 1) − 0.5 로 바꾸면 crossing이 x = ±0.5, eye 중심이 x = 0에 온다.

</details>

<details>
<summary>힌트 3 — eye width는 어느 레벨에서?</summary>

먼저 중심 창에서 μ1, μ0을 구하고, crossing 근처(|x| ≥ 0.48)에서 μ0 + amp/4 … μ1 − amp/4 띠 안의 점 평균을 crossing 레벨 Vx로 잡는다. 그 다음 `edge_times(v, dt, Vx)` 로 **Vx 레벨 교차**를 다시 뽑아 접는다. DCD가 있으면 0 V 교차는 두 봉우리로 갈라지지만 Vx에서는 한 점에 모인다.

</details>

<details>
<summary>힌트 4 — 1차 PLL 이득</summary>

연속시간 1차 루프 H(s) = ω_L/(s + ω_L). 이산으로는 UI마다 φ ← φ + UI + kp·(이 UI 에지들의 오차 합). 에지는 평균 D(PRBS7 ≈ 0.504)개/UI라서 UI당 평균 보정 = kp·D·e. 이것이 ω_L·UI·e와 같아야 하므로 kp = 2π·bw·UI / D. 이득이 0.0075 정도로 작아서 이산/연속 차이는 무시할 만하다.

</details>

<details>
<summary>힌트 5 — 에지에 UI 번호 붙이기</summary>

`round((t_k − t_0)/UI)` 는 +100 ppm × 16384 UI = 1.6 UI 누적 때문에 번호가 밀린다. 인접 에지 간격(PRBS7이면 최대 7 UI)을 반올림해 누적하면 오차가 7 × 1e-4 UI 수준이라 안전하다. 최소제곱은 정규방정식 두 줄: T = Σ(n−n̄)(t−t̄)/Σ(n−n̄)², a = t̄ − T·n̄.

</details>

## 5. 풀이

### Part A — 폴딩과 eye 지표의 정의

**합성 방법(주어진 코드).** NRZ 파형의 미분은 에지마다 크기 Δ = ±2A인 임펄스다. 채널이 FIR h이면 출력 = Σ_k Δ_k·G(t − t_k), G = h의 누적합(step 응답). 그래서 컨볼루션을 샘플 수 × 탭 수(26만 × 80)가 아니라 **에지 수 × G 길이**(약 8천 × 80)만큼만 하면 된다. 에지 시각 t_k가 샘플 격자에 없으므로 G를 선형 보간한다 — 이렇게 해야 1.5 ps RJ가 12.5 ps 샘플 격자에 뭉개지지 않는다.

**폴딩 → eye 좌표.** 샘플마다 u(UI 내 위상)를 구하고, 0 V crossing 위상들의 원형 평균 c를 기준으로 x = ((u − c) mod 1) − 0.5. 이렇게 하면 클럭 경계가 crossing과 얼마나 떨어져 있든(공칭 클럭처럼 표류하든) 같은 코드로 eye 중심이 정렬된다.

| 지표 | 계산 | 의미 / 주의 |
|---|---|---|
| μ1, σ1 / μ0, σ0 | 중심 창 \|x\| ≤ 0.1 UI에서 v > 0 / v ≤ 0 점들의 평균·표준편차 | 창 폭은 이 레슨 설정(벤더 기본값은 매뉴얼 확인) |
| EH (3σ) | (μ1 − 3σ1) − (μ0 + 3σ0) | 벤더 정의에 가까운 형태. ISI가 있으면 분포가 여러 봉우리라 σ가 부풀어 보수적 |
| EH_inner | min(1) − max(0) | 실제로 관측된 최소 개방. 점 수가 많을수록 작아짐 (통계량!) |
| crossing % | (Vx − μ0)/(μ1 − μ0)·100 | 50 %가 아니면 DCD·상승/하강 비대칭 |
| EW (6σ) | 1 − 6σx, σx = crossing 레벨 교차 위치의 표준편차 | (X2 − 3σ) − (X1 + 3σ). 가우스 가정 |
| EW_pp | 1 − (max − min) | 관측된 최소 수평 개방 |

**정답이 있는 파형으로 검증 (solution 실제 출력).**

```text
PASS  clean ramp eye: EH_inner == 2A = 0.800V, EW_pp == 1.000UI, cross 50%   [EH_inner=0.8000V EW_pp=1.0000UI cross=50.00%]
PASS  RJ only (0.010UI rms): crossing sigma within 8%, EW = 1 - 6sigma   [sigma_x=0.01002UI EW=0.9399UI (truth 0.94)]
PASS  exp channel tau=0.28UI ISI: inner EH == analytic worst pattern (+-2mV)   [EH_inner=0.6627V analytic=0.6627V (center s=0.6905UI, sample phase [11]/16) EH_3sigma=0.5307V]
PASS  DCD 0.05UI on 0.5UI ramp: cross% = 50(1 + dcd/ramp) = 55%   [cross=55.00%]
PASS  DCD measured at crossing level: EW_pp stays ~1 (DCD moved to vertical)   [EW_pp=1.0000]
```

**1차 채널 ISI의 해석해.** step 응답 G(t) = 1 − e^(−t/τ), 1비트 pulse 응답 p(t) = G(t) − G(t − 1) (시간 단위 UI). 송신 경계 후 위상 s에서 샘플할 때 커서 p(s+m), m ≥ 1 은 전부 양수이고 PRBS7은 6비트 이하 모든 패턴을 담으므로 최악 패턴(앞의 비트가 모두 반대)이 나타난다:

- EH_inner(s) = 2A·(p(s) − Σ_{m≥1} p(s+m)). 1차 채널은 Σ_{m≥1} p(s+m) = e^(−s/τ)(e^(1/τ) − 1)·e^(−1/τ)/(1 − e^(−1/τ)) = **e^(−s/τ)** 로 깔끔하게 닫히고, p(s) = 1 − e^(−s/τ) 이므로 **EH_inner(s) = 2A(1 − 2e^(−s/τ))**.
- 측정된 eye 중심은 송신 경계 후 s = 0.6905 UI, 그 창 안의 샘플 위상은 11/16 = 0.6875 → 0.8·(1 − 2e^(−0.6875/0.28)) = 0.6627 V — 측정과 0.1 mV 이내로 같다.
- 같은 식으로 s → 1이면 0.755 V. **1차 채널에서는 늦게 샘플할수록 수직 개방이 커진다**(다음 에지 직전까지). 즉 "eye 중심(crossing + 0.5 UI)"은 수직 최적점이 아니다. 실제 CDR은 수평 지터 여유와 수직 여유를 같이 봐야 하고, DDR은 이걸 트레이닝으로 찾는다(MEM-A04 참고).
- 같은 캡처의 EH(3σ) = 0.531 V < EH_inner 0.663 V: ISI 때문에 '1' 레벨이 여러 값으로 갈라져 σ가 커지고, 3σ가 실제 최악보다 더 깎는다. 정의를 리포트에 적어야 하는 이유다.

**DCD와 crossing %.** 상승 에지를 −δ/2, 하강 에지를 +δ/2 옮기면(1이 넓어짐) 두 궤적이 0 V가 아니라 그 위에서 만난다. 전체 전이 시간 T_r인 직선 램프면 교차점 높이 = A·δ/T_r → **cross% = 50·(1 + δ/T_r)** = 50·(1 + 0.05/0.5) = 55 %. 그리고 Vx 레벨에서 잰 crossing은 한 점에 모여 EW_pp = 1.000 그대로다 — DCD는 **수직축(crossing %)으로 옮겨 간다.** 0 V에서 재면 반대로 수평 폭을 δ만큼 깎는다(연습 4).

```svg
<svg viewBox="0 0 760 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">crossing 위치 히스토그램 — σ 기반 EW 는 분포 모양을 가정한다</text>
  <g stroke="currentColor"><line x1="20" y1="170" x2="240" y2="170"/><line x1="270" y1="170" x2="490" y2="170"/><line x1="520" y1="170" x2="740" y2="170"/></g>
  <path d="M40 169 C90 168 105 60 130 60 C155 60 170 168 220 169" class="f-ok" stroke="currentColor" opacity=".8"/>
  <path d="M300 169 L300 70 C320 140 350 155 380 158 C410 155 440 140 460 70 L460 169 Z" class="f-acc" stroke="currentColor" opacity=".7"/>
  <rect x="580" y="70" width="10" height="99" class="f-warn" stroke="currentColor"/>
  <rect x="670" y="70" width="10" height="99" class="f-warn" stroke="currentColor"/>
  <rect x="625" y="40" width="10" height="129" class="f-ok" stroke="currentColor" opacity=".6"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="130" y="45">(a) RJ: 가우스</text><text x="130" y="190">꼬리 무한: pp 는 표본 수와 함께 커짐</text>
    <text x="130" y="208">σ 는 안정 → 6σ(±3σ) 관례가 맞는 경우</text>
    <text x="380" y="45">(b) SJ: 아크사인(양 끝 뿔)</text><text x="380" y="190">σ = A/√2 → 6σ = 4.24A &gt; pp = 2A</text>
    <text x="380" y="208">이상 클럭: EW(6σ) 0.158 vs EW_pp 0.525</text>
    <text x="630" y="32">(c) DCD</text><text x="630" y="190">0 V 에서: 두 델타, 간격 δ</text>
    <text x="630" y="208">Vx 에서: 한 점(가운데) → cross% 로</text>
  </g>
</svg>
```

### Part B — 클럭을 무엇으로 잡느냐

**세 가지 클럭.**

| 클럭 | 만드는 법 | 따라가는 것 | 남기는 것 |
|---|---|---|---|
| 공칭(nominal) | 첫 에지 + j·200 ps | 없음 | 주파수 오프셋 누적(+100 ppm × 16384 UI = 1.64 UI), 모든 지터 |
| 이상 클럭(ideal, 최소제곱) | 레코드 전체에 맞춘 일정 주파수 | 평균 주파수·위상 | 레코드 안의 모든 위상 변동 (SJ, wander, SSC) |
| golden PLL (1차, 3.0 MHz) | 에지마다 비례 보정 | BW 아래 저주파 지터 | 1−H(f) 고역 통과한 지터 |

**최소제곱 이상 클럭.** 주입 +100 ppm을 RJ만 있는 캡처에서 +100.01 ppm으로 복원한다. 그런데 500 kHz SJ가 있는 같은 길이 레코드에서는 +105.71 ppm이 나온다 — 레코드가 SJ 1.64 주기라서 사인파의 일부가 "기울기"로 보이고 주파수 추정에 흡수된다. **이상 클럭의 결과는 레코드 길이에 의존한다.** 이것만으로도 컴플라이언스 측정이 CDR 기준을 쓰는 이유가 된다 (TIE·LS 주파수 추정 일반론은 HWLAB-M07 참고).

**1차 golden PLL.** 위상 오차 e = t_edge − φ를 비례 이득 kp로 되먹이는 type-1 루프다.

```svg
<svg viewBox="0 0 760 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwm09-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">1차 golden PLL 과 지터 전달: CDR 은 H 를 따라가고, eye 에는 1−H 가 남는다</text>
  <g fill="none" stroke="currentColor">
    <circle cx="95" cy="80" r="16"/><rect x="140" y="62" width="70" height="36" rx="6" class="f-mut"/>
    <rect x="240" y="62" width="120" height="36" rx="6" class="f-acc"/>
    <path d="M20 80 L77 80" marker-end="url(#hwm09-arr)"/><path d="M111 80 L138 80" marker-end="url(#hwm09-arr)"/>
    <path d="M210 80 L238 80" marker-end="url(#hwm09-arr)"/><path d="M300 98 L300 135 L95 135 L95 98" marker-end="url(#hwm09-arr)"/>
    <path d="M360 80 L385 80" marker-end="url(#hwm09-arr)"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="72">t_k</text><text x="90" y="85">Σ</text><text x="100" y="112">−</text>
    <text x="150" y="85">× kp</text><text x="250" y="78">NCO</text><text x="250" y="93">φ += UI + kp·Σe</text>
    <text x="364" y="72">clk[j]</text><text x="150" y="150">e = t_k − φ (이 UI 의 에지)</text>
    <text x="20" y="180">kp = 2π·bw·UI / D,  D = 0.504</text><text x="20" y="198">→ kp ≈ 0.0075, 루프 시정수 1/(2π·bw) = 53 ns = 265 UI</text>
    <text x="20" y="216">H(s) = ω_L/(s + ω_L),  |1 − H(jf)| = f/√(f² + bw²)</text>
    <text x="20" y="234">락 전 2000 UI(≈ 7.5 시정수)는 eye 에서 뺀다</text>
  </g>
  <g stroke="currentColor"><line x1="420" y1="40" x2="420" y2="220"/><line x1="420" y1="220" x2="745" y2="220"/></g>
  <line x1="577.6" y1="36" x2="577.6" y2="220" stroke="currentColor" stroke-dasharray="4 3"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" opacity=".55" points="420,40.1 452.1,40.4 494.6,42.4 526.7,49.2 558.8,70.2 577.6,92.7 601.3,127.4 633.4,168.3 665.5,193.3 708,209.2 740,214.6"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2.5" points="420,214 452.1,208 494.6,190.4 526.7,163.1 558.8,120.2 577.6,92.7 601.3,65.7 633.4,47.6 665.5,42 708,40.3 740,40.1"/>
  <circle cx="494.6" cy="190.4" r="4" class="f-warn" stroke="currentColor"/><circle cx="684.3" cy="40.9" r="4" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="404" y="44">1</text><text x="404" y="223">0</text>
    <text x="410" y="236">0.1</text><text x="518" y="236">1</text><text x="623" y="236">10</text><text x="700" y="236">100 MHz</text>
    <text x="583" y="110">bw = 3.0 MHz</text>
    <text x="436" y="62">H: CDR 이 따라감</text><text x="610" y="86">1 − H: eye 에 남음</text>
    <text x="440" y="178">500 kHz → 0.164</text><text x="600" y="34">30 MHz → 0.995</text>
  </g>
</svg>
```

**실제 출력 (`python3 -I solution.py` Part B):**

```text
PASS  ideal_clock_ls recovers +100ppm within 0.5ppm (from waveform crossings)   [RJ only: +100.01ppm (injected +100) | with 500kHz SJ: +105.71ppm]
PASS  golden PLL residual of 500kHz SJ == A*f/sqrt(f^2+BW^2) within 5%   [residual SJ=0.0329UI analytic |1-H|*A=0.0329UI (0.0%)]
PASS  golden PLL passes 30MHz SJ (10x BW) almost untouched   [30MHz SJ passes: 0.0499UI analytic 0.0498UI]
      clock     EH(3s)    EH_inner  EW(6s)   EW_pp    sigma_x   pp_x    mask_hits
      nominal    134.8mV     0.3mV  -0.404UI   0.000UI  0.2340UI  1.000UI   7464
      ideal      437.1mV   373.1mV   0.158UI   0.525UI  0.1403UI  0.475UI    620
      pll        511.6mV   551.3mV   0.854UI   0.870UI  0.0244UI  0.130UI      0
```

읽는 법:

1. **공칭 클럭**: 1.64 UI 표류로 crossing이 UI 전체에 퍼진다(pp = 1.000) → eye 없음. "스코프 eye가 완전히 닫혔다"의 첫 번째 원인은 신호가 아니라 **클럭 복원 설정**(데이터레이트 오입력, CDR 미사용)인 경우가 많다.
2. **이상 클럭**: ppm은 제거됐지만 SJ 0.4 UIpp가 그대로 남아 EW_pp 0.525. 마스크 620 hit — 수신기는 이 SJ를 CDR로 따라가므로 실제 링크엔 문제가 없는데 측정은 FAIL을 낸다.
3. **golden PLL**: SJ 잔류 0.0329 UI 피크. crossing σ 예산 √(RJ² + (0.0329/√2)²) = √(0.0075² + 0.0233²) = 0.0244 UI — 측정 0.0244와 일치(1차 채널 DDJ σ ≈ 0.004 UI는 제곱합으로 거의 안 보인다).
4. **수평이 번지면 수직도 나빠진다**: EH(3σ) 437 → 512 mV. 중심 ±0.1 UI 창에 번진 에지 샘플이 섞여 σ가 커지기 때문이다. 클럭 복원은 EW만의 문제가 아니다.
5. **EW(6σ) vs EW_pp**: 이상 클럭에서 0.158 vs 0.525. SJ는 유계(bounded) 지터라 σ = A/√2, 6σ = 4.24A로 실제 pp(2A)보다 훨씬 크다. σ 기반 정의는 가우스(RJ)에 맞춘 것이다 — DJ/RJ 분리와 dual-Dirac은 HWLAB-M10, BER 외삽은 HWLAB-M11.

**SSC가 있으면?** 실제 PCIe/SATA류 링크는 30–33 kHz 삼각파 SSC(최대 −5000 ppm 다운스프레드)를 쓰기도 한다. 평균 주파수 대비 ±2500 ppm 삼각파이므로 위상 편차 pp ≈ 0.0025 × 주기/4 = 0.0025 × 30 µs / 4 ≈ 19 ns ≈ 94 UI라 이상 클럭으로는 eye가 아예 없다. 이 레슨의 레코드(3.3 µs)는 SSC 한 주기(30 µs)보다 짧아서 직접 넣지 않았고, 대신 같은 성질의 저주파 SJ(500 kHz)로 보였다. 또한 1차 PLL은 주파수 램프(SSC)를 정상 상태 오차 없이 따라가지 못한다 — 그래서 SSC를 쓰는 표준은 2차(type-2) CDR을 기준으로 정의하는 경우가 많다(HWLAB-A01 참고, 각 표준의 CDR 차수·BW·peaking은 스펙 확인 필요).

### Part C — 세 레인 마스크 판정

**실제 출력:**

```text
      PASS lane=TX0 clk=pll EH=511mV EW=0.854UI cross=50.0% mask=0   (정상)
      FAIL lane=TX1 clk=pll EH=118mV EW=0.556UI cross=50.0% mask=884  BAD=EH,EW,mask   (alt-BOM 패드로 가는 2.2cm T-스텁 → echo +25% @1.5UI)
      FAIL lane=TX2 clk=pll EH=507mV EW=0.853UI cross=59.1% mask=0  BAD=cross%   (TX 드라이버 트림 로드 누락 → 상승/하강 불균형 DCD 0.10UI)
      FAIL lane=TX0 clk=ideal EH=437mV EW=0.158UI cross=49.9% mask=662  BAD=EW,mask   (같은 TX0 캡처, 이상 클럭으로 접으면)
```

`python3 -I demo_eye.py` 실제 출력 (2D 히스토그램 60 × 18, 가로 1 UI — 가운데가 eye 중심, 양 끝이 crossing; 세로 −0.5 … +0.5 V; `o` = 빈 마스크 칸, `X` = hit가 있는 마스크 칸):

```text
FAIL lane=TX0 clk=ideal EH=437mV EW=0.158UI cross=49.9% mask=662  BAD=EW,mask
|                                                            |
|%%%%@@%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%@@@@@%@@@@%@%%|
|**++========-----==++++*****######%%%%%@%@@@@%@%%%%%#######*|
|:--:..:-===+++*******++++++***********++==----:-::::::::::-:|
|===++++++++=+===+=+=+++++++++=--:.  .:--:::::::::::::::-:---|
|++++++=============+++==-:.           .-----:--:-:-:----==++|
|==+=================XXXooooooooooooooooX:--:----:-:--=======|
|===+=+=++=======-XXooooooooooooooooooooooXX-------==========|
|=========++==-XXoooooooooooooooooooooooooooXXX==============|
|========+++==-XXoooooooooooooooooooooooooooXXX==============|
|=====+=+========-XXooooooooooooooooooooooXX------===========|
|====================XXXooooooooooooooooX:---::--:-:--==+====|
|++++++==============++==--.           .---:----------:--==++|
|===++++=++==+=====+=+++++++++==-:.. .:--::-::::::::::::-:-:=|
|:--:..:-===+++*******+++++************++==----:-::::::::::-:|
|*++++======------===+++****#######%%%%%%%@@@@%@%%%%%%#####**|
|%%%%%%%@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@%%%%%%%%%%%%%%%|
|                                                            |
PASS lane=TX0 clk=pll EH=511mV EW=0.854UI cross=50.0% mask=0
|                                                            |
|#%%%#%@%#@@#%@@#%@@#@@%#@@#%@@#@@@#@@%#@@#%@@#@@%*##**#%*#%%|
|====-=---------:-::::::::::-+*#%%@%@@%%@@%%@@#@@@#%%*+**++++|
|                .-=*#%%%@@#%%%===-...          :=+=+= .     |
|          .-+*##@@%+*+- .                       .-++=:+:    |
|      :=*#%###*.:.                               .-+*+++:   |
|  .-+###+#+:        oooooooooooooooooooo           .=+*+---.|
|-+##**+=         oooooooooooooooooooooooooo          :=**+*+|
|##*+.:.       oooooooooooooooooooooooooooooooo         :=*#%|
|##*+..        oooooooooooooooooooooooooooooooo         :=*#%|
|-+##**+- .       oooooooooooooooooooooooooo         .:=**+++|
|  .-+###+#+:        oooooooooooooooooooo           .-**+---.|
|      :=*#%###*.:.                               .:+*+++:   |
|          .:+*##@@%+*+-..                      ..=++=:=:    |
|                .:=*#%%%@@#%%#===-..           :=+++= .     |
|====-==----::--::::::::::::-+*#%%@%@@%%@@%%@@#@@@#%#*+**++++|
|#%%%#%%%#@@#%@@#%@@#@@%#@@#%@@#%@@#@@%#@@#%@@#%%%*##**##*#%%|
|                                                            |
```

`python3 -I demo_eye.py TX1 pll` 실제 출력 — '1'/'0' 레일이 여러 줄로 갈라지고(echo가 1.5 UI 뒤 비트에 ±를 더함) 마스크 위·아래 변의 왼쪽 부분에 `X`가 찍힌다:

```text
FAIL lane=TX1 clk=pll EH=118mV EW=0.556UI cross=50.0% mask=884  BAD=EH,EW,mask
|                                                            |
|+**+***+****#***##+*##**##*##**##**##**##*##**##++++=+++=+++|
|===----:---:-:::::::::::::.:::-=++*##*###*##**#%****=+++=+==|
|            .:=+****##*###*##*+**---:         .:====::.     |
|*##**##*%%%%@@%#%%**##+***+*#**##**##*###*####%%*#%%####*##*|
|..:-++*+*+=.:.   . ..:-=+**##**##**##*###+#**+**+++++++=:-:.|
|=+*+++=    .:=++*#**XXXXXXXXXXoooooooooo       :-===:==+++==|
|+#+. .:=+***#*=:=XXoooooooooooooooooooooooo     .:=++====*##|
|+****##++=- . oooooooooooooooooooooooooooooooo     :-+**+*++|
|+****##++=- . oooooooooooooooooooooooooooooooo    .:=+**+*++|
|+#+. .:=+***#*=:=Xooooooooooooooooooooooooo     .:=+=====*##|
|=+*+++=    .:=++*#**XXXXXXXXXXoooooooooo       .-===:-=+++==|
|..:-++*+*+=.... .  ..:-=+**##**##**##*###*#**+**+++++++=:-:.|
|*##**##*%%%%@@%#%%**##+***+****##**##*###*####%%*#%%####*##*|
|            .:-++***##*###*##*+**---: .       .:====.:.     |
|-==--=-:---:::::::::::.:::::.:-=++*##*###*##**##****=+++====|
|+**++**+***+****##+*##****+##**##+*##*****#***#*=+++=+++=+++|
|                                                            |
```

두 번째 eye를 보면 1차 채널의 비대칭이 그대로 보인다: crossing(왼쪽 끝) 직후 빠르게 올라가고, 오른쪽에서는 다음 송신 에지가 crossing보다 τ·ln2 ≈ 0.19 UI 먼저 시작해 궤적이 일찍 휜다.

**레인별 해석과 행동 가능한 리포트:**

- `FAIL VALBRD-S TX1@J7: EH 118 mV < 300, EW 0.556 UI < 0.70, mask 884 hits (margin −11.6 %); cross 50.0 % OK. '1'/'0' 레벨이 두 갈래(σ1 78 mV vs TX0 36 mV) → 결정적 ISI. golden PLL 폴딩, TX0 동일 셋업 PASS(margin +42.8 %) → 측정 체인 아님. 1.5 UI(300 ps) 왕복 echo ≈ 편도 150 ps ≈ 2.2 cm(εeff ≈ 4 가정) 스텁 의심: alt-BOM 패드 분기 확인, TDR로 위치 확인(HWLAB-M01), 리워크 시 스텁 제거/0 Ω 위치 변경.`
- `FAIL VALBRD-S TX2@J7: crossing 59.1 % (45–55 %), EH/EW/mask OK. DCD 의심 — 0 V 기준 EW_pp 0.779 vs crossing 레벨 0.879 UI(차 0.100 UI). TX 드라이버/DCD 트림 레지스터 값 확인(부트 시 eFuse 트림 로드 여부), 상승·하강 시간 개별 측정(HWLAB-B04).`
- TX0은 PASS. 그런데 **같은 캡처를 이상 클럭으로 접으면 EW 0.158 UI, mask 662 hits로 FAIL**이다. 리포트에는 반드시 클럭 복원 방식(1st-order PLL, 3.0 MHz)을 적는다. 스코프 설정이 "constant frequency"로 남아 있으면 정상 보드를 리워크하게 된다.

| 원인 | eye에서의 증상 | 드러내는 측정 |
|---|---|---|
| 클럭 복원 설정 오류 (공칭/이상 클럭, 데이터레이트 오입력) | 수평으로 완전히 번짐, 정상 레인도 마스크 FAIL | CDR 방식·BW를 바꿔 재폴딩, TIE 스펙트럼 |
| 채널 손실·스텁·반사 (ISI, DDJ) | 레일이 여러 줄로 갈라짐, EH(3σ) 급감, 마스크 꼭짓점 hit | pulse 응답·TDR(HWLAB-M01, M12), 패턴 의존 확인 |
| DCD·상승/하강 비대칭 | crossing ≠ 50 %, 0 V 기준 EW 감소 | Vx vs 0 V EW 비교, rise/fall 개별 측정 |
| 저주파 wander·SSC | 이상 클럭에서만 EW 감소 | PLL BW 스윕(연습 1), TIE의 저주파 성분 |
| RJ (PLL·클럭 소스 잡음) | crossing이 가우스로 두꺼움 | σx, 위상잡음 적분(HWLAB-M08) |
| 측정 체인 (프로브·픽스처·스코프 잡음) | 레일 두께(σ1, σ0) 증가, 모든 레인 동일 | 입력 단락 잡음 바닥, de-embedding |

**장비가 있을 때 (이 레슨에선 실행하지 않음):** 실시간 스코프의 지터/eye 앱(예: Keysight EZJIT·SDA, Tektronix DPOJET)에서 Clock Recovery를 "Constant Frequency / 1st Order PLL / 2nd Order PLL" 중 고르고 loop BW(또는 bitrate/1667 같은 비율)를 설정한 뒤 eye·mask를 켠다. 같은 파형을 PyVISA로 가져와(`:WAVeform:DATA?` 류 — 명령은 벤더 매뉴얼 확인) 이 레슨의 `eye_metrics()` 에 넣으면 스코프 앱의 클럭 복원 설정이 의도대로인지 교차 검증할 수 있다. 차동 신호는 차동 프로브나 SMA 픽스처로 받고, 픽스처 손실은 de-embedding한다(스코프 BW·프로브 일반론은 HWLAB-B05).

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"         # 21/21 passed (약 3 s), exit=0
python3 -I -W error -X dev solution.py           # 경고 없음 (exit 0)
python3 -I demo_eye.py                           # §5 Part C 의 ASCII eye (TX0 ideal / TX0 pll / TX1 pll)
python3 -I demo_eye.py TX2 pll                   # 레인·클럭 지정
```

크기와 실행 시간: Part B/C 캡처는 각 16384 비트 × 16 샘플 = 262,166 샘플(락 전 2000 UI 제외 229,972 점). 합성은 에지 기반이라 캡처 하나 약 0.2 s, 전체 solution 2.7–3.1 s (이 Mac, Python 3.9.6에서 여러 번 측정). 데이터 파일은 커밋하지 않고 고정 시드로 메모리에서 생성한다.

<!-- code: demo_eye.py -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 공칭 비트레이트로 `t % UI` 접기 | ±100 ppm만 있어도 1만 UI면 1 UI 표류 → eye 닫힘 | CDR(golden PLL) 경계로 `phases()` |
| 레코드 전체 LS 클럭을 "이상적"이라고 믿기 | 수신기가 따라가는 저주파 지터까지 eye에 남겨 거짓 FAIL, 결과가 레코드 길이에 의존 | 스펙의 기준 CDR(차수·BW)로 폴딩, 리포트에 명시 |
| crossing 위치를 산술 평균 | 0.98과 0.02가 섞이면 0.5 — eye 중심이 crossing에 놓임 | 원형 평균 `atan2(Σsin, Σcos)` |
| 에지 UI 번호 = `round((t − t0)/UI)` | ppm 누적으로 번호가 밀림 → LS가 엉뚱한 주파수 | 인접 간격 반올림을 누적 |
| PLL 이득에 에지 밀도를 안 넣음 | PRBS7은 UI당 에지 ≈ 0.5개 → 실제 BW가 절반 | kp = 2π·bw·UI / D |
| 락 전 구간까지 접기 | 초기 위상 오차 과도응답이 eye에 들어감 | `skip_ui` (루프 시정수의 5배 이상) |
| EW를 0 V에서만 재기 | DCD가 EW를 깎는지 crossing %로 가는지 정의가 섞임 | crossing 레벨 Vx에서 재고, 0 V 값은 별도 보고 |
| σ 기반 EW·EH만 보고 | SJ·ISI(유계, 다봉 분포)에서 6σ가 실제 pp와 크게 다름 | EW_pp·EH_inner·마스크를 같이, DJ/RJ 분리는 M10 |
| 마스크 hit 0 = BER 1e-12 OK | 23만 점은 1e-12 사건을 볼 수 없음 | 마스크 여유(margin) + bathtub 외삽(M11) |
| 점-다각형 판정에서 수평 변 처리 누락 | `(y1 > y) != (y2 > y)` 없이 나누면 0으로 나눔·중복 계산 | 반열린 구간 조건 그대로 |

## 8. 면접 Q&A

<details>
<summary>Q1. 스코프 없이 샘플 데이터만 있습니다. eye diagram을 어떻게 만듭니까?</summary>

① 데이터 에지(임계 교차)를 보간으로 뽑는다. ② 에지로 클럭을 복원한다 — 표준이 정한 기준 CDR(예: 1차 PLL, BW = bitrate/1667)로. ③ 샘플마다 복원 클럭 경계 기준 UI 내 위상을 구해 접는다. ④ crossing 원형 평균으로 eye 중심을 맞추고 2D 히스토그램을 만든다. ⑤ 중심 창에서 레벨 분포(EH), crossing 레벨에서 시간 분포(EW), crossing %, 마스크 hit을 계산한다. 락 전 구간은 버린다.
꼬리질문: "샘플 대신 등가시간(sampling) 스코프라면?" → 등가시간 스코프는 트리거(보통 하드웨어 CDR이나 기준 클럭)에 대해 여러 획을 쌓는다. 클럭 복원이 트리거 경로에 있으므로 CDR BW 설정이 같은 의미를 갖고, 트리거 지터가 측정 지터에 더해진다.

</details>

<details>
<summary>Q2. 왜 golden PLL로 접습니까? BW = bitrate/1667은 무슨 뜻입니까?</summary>

수신기 CDR은 BW 아래의 느린 위상 변동(주파수 오프셋, SSC, wander)을 따라가므로 그 지터는 비트 오류를 만들지 않는다. 측정도 같은 고역 통과(1−H)를 거쳐야 수신기가 보는 eye가 된다. bitrate/1667은 Fibre Channel·이더넷 계열에서 쓰인 기준 CDR 코너의 대표값이다(5 Gb/s → 3.0 MHz). 다른 표준은 2차 CDR, 다른 BW·peaking을 정한다(대표값 — 스펙 확인 필요).
꼬리질문: "BW를 너무 넓게 잡으면?" → 측정이 실제 수신기보다 많은 지터를 지워 eye가 좋아 보인다(낙관적). 너무 좁으면 비관적. 이 레슨의 500 kHz SJ는 3 MHz에서 0.164만 남지만 BW 300 kHz면 0.857이 남는다. "1차 PLL에 +100 ppm이면?" → 정상 상태 위상 오차 = −Δf/f / (2π·bw·UI) = −0.0265 UI (연습 2). eye 모양은 그대로 이동만 한다.

</details>

<details>
<summary>Q3. Eye height와 eye width의 정의를 말해 보세요. 3σ 정의의 함정은?</summary>

EH = (μ1 − 3σ1) − (μ0 + 3σ0) — 중심 창(이 레슨 ±0.1 UI)의 레벨 분포에서. EW = (X2 − 3σ) − (X1 + 3σ) = UI − 6σx — crossing 레벨에서의 시간 분포에서. crossing % = (Vx − μ0)/(μ1 − μ0). (벤더마다 창 폭·정의 세부가 다르니 매뉴얼 확인.)
함정: σ는 분포가 가우스일 때만 꼬리를 대표한다. SJ(아크사인 분포)는 σ = A/√2라 6σ = 4.24A > pp 2A (이 레슨 이상 클럭: EW 0.158 vs pp 0.525). ISI는 레일을 여러 줄로 갈라 σ를 부풀린다(1차 채널: EH(3σ) 0.531 vs inner 0.663 V). 반대로 pp/inner는 관측 시간이 길수록 작아지는 통계량이다.
꼬리질문: "그럼 무엇을 스펙으로 쓰나?" → 목표 BER에서의 개방(eye opening @ 1e-12)을 쓴다. RJ/DJ 분리(dual-Dirac, HWLAB-M10)와 bathtub Q-scale 외삽(HWLAB-M11)으로 구한다.

</details>

<details>
<summary>Q4. crossing %가 59 %입니다. 무엇을 의심하고, eye width는 어느 레벨에서 재야 합니까?</summary>

상승/하강 비대칭(DCD) — 1이 0보다 넓다. 원인: TX 드라이버 pull-up/pull-down 불균형, 트림 미적용, 상승·하강 시간 차이, 수신단 임계 오프셋. 직선 램프 근사로 cross% ≈ 50(1 + δ/T_r) — δ를 거꾸로 추정할 수 있다.
EW는 관례상 crossing 레벨에서 재서 두 궤적이 만나는 점 기준으로 본다(이 경우 DCD가 EW를 거의 안 깎는다). 하지만 수신기 결정 임계가 0 V라면 수신기에게는 0 V에서의 폭이 현실이다: TX2는 0.879 → 0.779 UI. 리포트에 두 값을 같이 적는다.
꼬리질문: "half-rate 클럭 TX의 DCD는?" → 짝수/홀수 UI 폭이 번갈아 달라지는 형태(even/odd jitter)로, 상승/하강 비대칭과는 다른 DJ 성분이다(분해는 HWLAB-M10).

</details>

<details>
<summary>Q5. 마스크 hit 0이면 그 레인은 BER 1e-12를 만족합니까?</summary>

아니다. 이 레슨의 캡처는 1.4만 UI, 23만 점이다. 1e-12 확률 사건은 볼 수 없다 — 가우스 꼬리로 1e-12는 약 ±7σ인데 짧은 캡처의 pp는 대략 ±4σ 언저리에서 멈춘다. 그래서 ① 마스크 **여유(margin)**를 보고(TX0 +42.8 %, TX2 +29.7 %), ② 긴 캡처·다수 획을 누적하거나, ③ RJ/DJ 분해 후 TJ(1e-12)를 외삽한다(HWLAB-M10/M11). 실제 BER 확인은 링크의 PRBS checker로 한다(SIV-M06 참고).
꼬리질문: "마스크 여유는 어떻게 계산?" → 마스크를 중심 기준으로 s배 키워 첫 hit이 나는 s. 볼록 다각형이면 점마다 게이지 함수 γ(p) = max_i (a_i x + b_i y)/c_i 를 구해 min γ − 1이 여유다(연습 3, O(N)).

</details>

<details>
<summary>Q6. TX1 eye는 레일이 두 줄로 갈라지고 마스크 꼭짓점에 hit이 있습니다. 어떻게 원인을 좁힙니까?</summary>

레일이 이산적으로 갈라지는 것은 랜덤이 아니라 패턴 의존(ISI)이다. ① 같은 셋업의 이웃 레인(TX0)이 정상이면 측정 체인이 아니라 그 레인의 채널 문제. ② 패턴 동기 평균(PRBS7 주기 127 기준)으로 pulse 응답을 뽑으면 echo의 지연(1.5 UI = 300 ps)과 크기(+25 %)가 나온다. ③ 왕복 300 ps → 편도 150 ps → εeff ≈ 4이면 약 2.2 cm. 레이아웃에서 그 거리의 분기·패드를 찾고 TDR로 확인(HWLAB-M01).
꼬리질문: "등화기(DFE)로 고칠 수 있나?" → 1.5 UI 뒤의 echo는 DFE 탭 범위 안이면 상당 부분 지울 수 있지만, 스텁은 주파수 영역에서 노치(notch)를 만들고 레이트가 올라가면 더 나빠진다. 보드에서 스텁을 없애는 게 정답이다(등화는 HWLAB-M12).

</details>

<details>
<summary>Q7. 5 Gb/s eye를 보려면 스코프 대역폭과 샘플링률은 얼마나 필요합니까?</summary>

경험칙으로 NRZ 기본파는 bitrate/2 = 2.5 GHz, 에지 모양을 보려면 3차 고조파(7.5 GHz) 근처까지 — 즉 8 GHz급 이상, 신호 상승 시간에 따라 더(대역폭·상승 시간 관계는 HWLAB-B05). 샘플링은 실시간 스코프에서 UI당 수 샘플 이상이 필요하고 보간(sin(x)/x)에 의존한다. 이 레슨은 16 샘플/UI(80 GS/s 상당)로 합성했다.
꼬리질문: "스코프 자체 지터는?" → 스코프 타임베이스 지터·잡음이 eye에 더해진다. RJ는 제곱합으로 빠지므로 스코프 고유 지터 바닥(데이터시트)을 알고 있어야 1 ps급 RJ를 판정할 수 있다.

</details>

## 9. 추가 연습

### 연습 1 — golden PLL의 지터 전달 스윕

SJ(0.05 UI)만 넣은 송신 에지를 PLL에 통과시켜, 주파수 f = 0.3, 1, 3, 10, 30 MHz에서 eye에 남는 비율 |1 − H(f)|를 측정하고 해석값 f/√(f² + bw²)와 비교한다. f = bw에서 1/√2(−3 dB)인지도 확인.

<details>
<summary>답 보기</summary>

TIE를 sin/cos/상수 3-파라미터 최소제곱으로 맞춰 진폭을 구하고 주입 진폭으로 나눈다. 실제 출력:

```text
      f_SJ      measured |1-H|   analytic f/sqrt(f^2+bw^2)   err
        0.3MHz   0.0995           0.0995                       +0.0%
        1.0MHz   0.3164           0.3163                       +0.0%
        3.0MHz   0.7085           0.7072                       +0.2%
       10.0MHz   0.9611           0.9578                       +0.3%
       30.0MHz   0.9982           0.9950                       +0.3%
```

고주파 쪽의 +0.3 %는 루프가 에지가 있는 UI에서만 갱신되는 이산 효과다. 이 표가 곧 "CDR jitter transfer" 측정이고, 수신기 쪽에선 반대로 JTOL(jitter tolerance)을 본다(HWLAB-A01).

</details>

### 연습 2 — 1차 PLL의 정적 위상 오차

PLL 중심 주파수를 공칭 5.000 Gb/s로 두고 송신이 +100 ppm일 때, 락 후 TIE 평균이 이론값 −(Δf/f)/(2π·bw·UI)와 3 % 이내로 맞는지 확인한다.

<details>
<summary>답 보기</summary>

type-1 루프는 주파수 오차를 위상 오차로만 상쇄할 수 있다: UI당 표류 1e-4·UI = kp·D·e → e = −1e-4 / (2π·bw·UI) = −1e-4·1667/(2π) = −0.0265 UI. 실제 출력: `mean TIE=-0.0265UI theory=-0.0265UI`. eye는 그만큼 옆으로 밀릴 뿐 열림은 같다. 램프(SSC)는 이 오차가 시간에 따라 커지므로 type-2 CDR이 필요하다.

</details>

### 연습 3 — 마스크 여유(margin)

마스크를 중심 기준으로 s배 키울 때 첫 hit이 나는 s를 구해 여유 % = (s − 1)·100으로 보고한다. 이진 탐색 대신 볼록 다각형의 게이지 함수로 한 번에.

<details>
<summary>답 보기</summary>

각 변을 a·x + b·y = c (c > 0, 원점이 안쪽)로 쓰면 점 p가 s배 마스크 안 ⇔ max_i (a_i x + b_i y)/c_i < s. 그러니 여유 = min_p γ(p) − 1. 실제 출력: TX0 +42.8 %, TX1 −11.6 %(100 %에서 884 hits), TX2 +29.7 %. 검증으로 TX0 마스크를 ×1.426로 키우면 0 hit, ×1.430이면 1 hit. 여유는 "얼마나 아슬아슬한가"를 숫자로 주므로 보드 간 비교·온도 코너 비교에 hit 수보다 유용하다.

</details>

### 연습 4 — DCD 레인의 0 V 기준 eye width

TX2(DCD 0.10 UI)에서 crossing 레벨 Vx와 0 V 각각에서 교차를 접어 EW_pp를 구하고, 차이가 DCD와 같은지 확인한다.

<details>
<summary>답 보기</summary>

실제 출력: `EW_pp@Vx(73mV)=0.879  EW_pp@0V=0.779  diff=0.100UI`. 0 V에서는 상승 교차가 −0.05, 하강이 +0.05 UI로 갈라져 crossing 분포 폭이 정확히 DCD만큼 넓어진다. crossing % 59.1 %와 Vx = +73 mV(진폭의 약 9 %)가 같은 현상의 수직 표현이다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"      # 11/11 passed, exit=0 (약 3.7 s)
```

## 10. 핵심 정리

- eye = 복원 클럭 경계마다 UI 단위로 접은 2D 히스토그램. **어떤 클럭으로 접었는지가 결과의 절반**이다 — 공칭 EW_pp 0, 이상 클럭 0.525, golden PLL 0.870 UI (같은 캡처).
- 기준 CDR은 저주파 지터를 따라간다: eye에 남는 지터 = |1 − H(f)| = f/√(f² + bw²) (1차, bw = bitrate/1667 대표값). 500 kHz SJ → 0.164만 남음.
- 1차 PLL: kp = 2π·bw·UI/D (D = 에지 밀도), 주파수 오프셋은 정적 위상 오차(−Δf/f/(2π·bw·UI))로, SSC는 type-2가 필요.
- crossing 위치는 원형 평균, eye 중심 = crossing + 0.5 UI. 1차 채널에서는 수직 최적점이 중심보다 늦다: EH_inner(s) = 2A(1 − 2e^(−s/τ)).
- EH = (μ1 − 3σ1) − (μ0 + 3σ0), EW = UI − 6σx (crossing 레벨). σ 정의는 SJ·ISI에서 틀린다 → pp/inner, 마스크, M10/M11 외삽과 같이 본다.
- crossing % ≠ 50 → DCD/비대칭 (램프 근사 50(1 + δ/T_r)). 0 V 기준 EW는 DCD만큼 줄어든다.
- 마스크: ray casting으로 hit 수, 게이지 함수로 여유 %. hit 0 ≠ BER 1e-12.
- 리포트에 클럭 복원 방식·BW·창·레벨을 적는다. 정상 레인도 이상 클럭으로 접으면 거짓 FAIL(TX0: mask 662 hits).

## 참고자료

- Fibre Channel — Methodologies for Jitter and Signal Quality Specification (FC-MJSQ, INCITS TR-35) — golden PLL(bitrate/1667), eye·지터 측정 방법론
- Keysight, *Infiniium Serial Data Analysis / EZJIT* 사용자 설명서 — Clock Recovery(constant frequency, 1st/2nd order PLL), Eye Height/Width/Crossing % 정의, mask test
- Tektronix, *DPOJET Jitter and Eye Diagram Analysis* 매뉴얼 — clock recovery 설정, eye 측정 정의
- Eric Bogatin, *Signal and Power Integrity — Simplified* — 반사·스텁·ISI와 eye
- Floyd M. Gardner, *Phaselock Techniques*, 3rd ed. — 1차/2차 PLL 전달 함수, 정적 위상 오차
- Mike Peng Li, *Jitter, Noise, and Signal Integrity at High-Speed* — 지터 전달, golden PLL, eye·BER
- 관련 레슨: HWLAB-B04(교차 보간·상태 레벨), HWLAB-B05(스코프 BW), HWLAB-M01(TDR·반사), HWLAB-M07(TIE·LS 주파수), HWLAB-M08(위상잡음→RJ), HWLAB-M10(RJ/DJ·dual-Dirac), HWLAB-M11(bathtub), HWLAB-M12(등화), HWLAB-A01(CDR/JTOL), MEM-A04(DDR DQ eye 트레이닝), SIV-M06(PRBS checker)
