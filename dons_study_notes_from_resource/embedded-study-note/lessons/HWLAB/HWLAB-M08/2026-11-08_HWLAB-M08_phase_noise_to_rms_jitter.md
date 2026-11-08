---
id: HWLAB-M08
time: 80분
updated: 2026-11-08
---
# HWLAB-M08 위상 잡음 → RMS 지터 적분과 레퍼런스 클럭 판정

## 학습 목표

- 데이터시트 위상 잡음 표 L(f) [dBc/Hz]를 **로그-로그 보간(구간마다 멱법칙)** 하고, 구간별 **닫힌식**으로 적분해 σ_φ = √(2∫10^(L/10) df), σ_t = σ_φ/(2πf0)를 계산한다. 평탄·−20 dB/dec·−10 dB/dec 표처럼 손으로 정답을 낼 수 있는 표로 검증한다.
- 순진한 적분(점 사이 선형 사다리꼴, dB를 선형 f로 보간, "L × 대역폭")이 **기울기가 가파른 구간에서 지터를 수십~백 % 부풀리는 이유**를 숫자로 보인다. 스퍼(dBc)는 dBc/Hz 적분에 섞지 않고 2·10^(S/10)로 따로 더한다.
- PCIe common-clock 형 지터 전달함수 |H(f)|² = |H1·e^(−sT) − H2|²·|H_CDR|²를 직접 구현하고, peaking → ζ, BW → ωn 변환과 16개 모서리 조합 최악값 탐색을 이해한다.
- 같은 두 오실레이터가 **12 kHz–20 MHz 헤드라인 숫자와 PCIe 필터 숫자에서 순위가 뒤집히는** 상황을 재현하고, "어디에 잡음이 있느냐"로 설명하는 판정 리포트를 쓴다.

## 1. 문제

레퍼런스 클럭을 고를 때 데이터시트 첫 장의 "12 kHz–20 MHz RMS phase jitter 0.9 ps" 만 보고 고르면 PCIe 링크 컴플라이언스에서 떨어질 수 있다 — Rivian·NVIDIA·Apple 보드/실리콘 검증 면접의 단골 질문 "이 위상 잡음 표를 RMS 지터로 바꿔 보고, 그게 PCIe refclk 스펙을 만족하는지 말해 보라"에 답하려면 적분과 필터를 직접 해 봐야 한다.

위상 잡음 L(f)는 반송파 f0에서 오프셋 f만큼 떨어진 곳의 **한쪽 측파대(SSB) 잡음 전력 / 반송파 전력**을 1 Hz 대역폭으로 정규화한 값이다(단위 dBc/Hz). 작은 위상 편차에서 위상 PSD S_φ(f) [rad²/Hz] = 2·L(f)(선형)이므로 오프셋 구간 [f1, f2]의 위상 분산은 2∫L df, 시간으로 바꾸면 σ_t = σ_φ/(2πf0)다. 이 레슨은 이 한 줄을 **정확하게** 계산하는 일과, 그 숫자가 **스펙이 요구하는 숫자인지**를 따지는 일이다.

```svg
<svg viewBox="0 0 760 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">두 후보 오실레이터의 위상 잡음 L(f) (f0 = 100 MHz, 데이터시트 표 7점, 로그-로그 직선 연결)</text>
  <rect x="308" y="34" width="355" height="282" class="f-mut" opacity=".35"/>
  <text x="400" y="50" fill="currentColor" font-size="12">헤드라인 적분 구간 12 kHz – 20 MHz</text>
  <g stroke="currentColor" stroke-dasharray="3 4" opacity=".35">
    <line x1="80" y1="100" x2="710" y2="100"/><line x1="80" y1="160" x2="710" y2="160"/>
    <line x1="80" y1="220" x2="710" y2="220"/><line x1="80" y1="280" x2="710" y2="280"/>
  </g>
  <line x1="80" y1="316" x2="710" y2="316" stroke="currentColor"/>
  <line x1="80" y1="34" x2="80" y2="316" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="74" y="44">−80</text><text x="74" y="104">−100</text><text x="74" y="164">−120</text>
    <text x="74" y="224">−140</text><text x="74" y="284">−160</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="80" y="332">100</text><text x="190" y="332">1k</text><text x="300" y="332">10k</text>
    <text x="410" y="332">100k</text><text x="520" y="332">1M</text><text x="630" y="332">10M</text>
    <text x="400" y="352">오프셋 주파수 f [Hz] (로그) · 세로축 L(f) [dBc/Hz]</text>
  </g>
  <polyline points="80,46 190,91 300,124 410,163 520,235 630,274 707,280" fill="none" stroke="currentColor" stroke-width="2.5"/>
  <polyline points="80,85 190,154 300,190 410,214 520,220 630,223 707,226" fill="none" stroke="currentColor" stroke-width="2.5" stroke-dasharray="8 5"/>
  <g fill="currentColor" font-size="12">
    <text x="200" y="76">Y2 크리스털 XO (실선): close-in 높음</text>
    <text x="545" y="292">Y2 플로어 −158…−160</text>
    <text x="96" y="250">Y3 PLL 기반 (점선): close-in 낮음</text>
    <text x="440" y="208">Y3 플로어 −140…−142</text>
  </g>
  <line x1="607" y1="222" x2="607" y2="140" stroke="currentColor" stroke-width="2"/>
  <polygon points="602,146 607,134 612,146" class="f-warn" stroke="currentColor"/>
  <text x="540" y="128" fill="currentColor" font-size="12">Y3 스퍼 6.25 MHz −80 dBc</text>
  <text x="540" y="114" fill="currentColor" font-size="12">(dBc — 이 축의 dBc/Hz 아님)</text>
</svg>
```

가상의 보드 **SSDB-4 rev A** (PCIe 4.0 x4 NVMe SSD 컨트롤러 평가 보드). 100 MHz HCSL `PCIE_REFCLK` 오실레이터 자리에 두 후보가 있다: **Y2**(저가 크리스털 XO — close-in은 나쁘지만 플로어가 −160 dBc/Hz로 깨끗)와 **Y3**(분수-N PLL 기반 프로그래머블 오실레이터 — close-in은 좋지만 플로어 −141 dBc/Hz, 6.25 MHz 분수 스퍼 −80 dBc). 표 숫자는 데이터시트 typical 표 모양을 흉내 낸 **합성값**이고, PCIe 필터·한도는 **대표값(PCI-SIG Base Spec / CEM 확인 필요)** 이다. 테스트는 레슨 디렉터리의 `refclk_spec.json` 과 자기 일관적이다.

### Part A — 보간·닫힌식 적분·σ_t (정답을 아는 표로)

- `interp_L(table, f)` — 두 점 사이 L은 log10(f)에 대해 직선(= 선형 전력은 f^b 멱법칙). 표 범위 밖이면 `ValueError` (외삽 금지).
- `band_integral(table, f1, f2)` — ∫10^(L/10) df. 구간 [fa, fb]마다 b = log(Sb/Sa)/log(fb/fa)를 구해 닫힌식으로(b = −1이면 ln 분기). 구간 끝(12 kHz, 20 MHz)이 표 점이 아니면 보간한 값에서 시작한다.
- `rms_jitter(table, f0, f1, f2)` → (σ_φ [rad], σ_t [s]).
- 정답: 평탄 −150 dBc/Hz → ∫ = 10⁻¹⁵·(20 MHz − 12 kHz), σ_t = 318.2 fs @100 MHz. −20 dB/dec 표(S = 10⁻²/f²) → 10⁻²·(1/f1 − 1/f2). −10 dB/dec 표(S = 10⁻⁷/f) → 10⁻⁷·ln(f2/f1).

### Part B — 순진한 적분의 거짓말, 스퍼, PCIe 형 필터

- 주어진 `integrate_naive(..., 'trapz' | 'db_linf' | 'rect')` 세 방법의 오차를 −20 dB/dec 정답과 비교한다(정답이 닫힌식이므로 오차가 그대로 보인다).
- `spur_phase_var(spurs, f1, f2, w=None)` — 스퍼 S dBc 하나의 위상 분산 2·10^(S/10) (필터가 있으면 × |H(f_spur)|²).
- `integrate_weighted(table, f1, f2, w)` — ∫10^(L/10)·w(f) df를 u = ln f 축 Simpson으로(표의 꺾인 점에서 구간 분할). w ≡ 1이면 닫힌식과 10⁻⁶ 이내, w = 1차 HPF면 해석해와 10⁻⁶ 이내.
- `pcie_mag2(f, c)` — |H(f)|² = |H1(s)·e^(−sT) − H2(s)|² · |s/(s + 2π·f_CDR)|². ζ·ωn 변환(`zeta_from_peaking`, `wn_from_bw`)은 주어진다. 저주파 점근선 (2πfT · f/f_CDR)², PLL1 = PLL2·T = 0이면 0인지 확인.

### Part C — SSDB-4 PCIE_REFCLK: Y2 vs Y3 판정 리포트

- `filtered_jitter(osc, spec)` — 16개 (BW, peaking) 모서리 조합마다 1 kHz–50 MHz에서 σ_t(필터 적용, 스퍼 포함), 최악 조합.
- `judge(osc, spec)` — 오실레이터 배정 한도 0.25 ps(링크 한도 0.5 ps 중 osc 몫)로 PASS/FAIL, 한 줄 리포트. `decide()`(주어짐)는 PASS 중 최선을 고르고 헤드라인 순위와 다르면 경고한다.

## 2. Problem (English)

> **Integrate an oscillator phase-noise table into RMS jitter and qualify a PCIe reference clock.**
>
> Using only the Python 3 standard library:
>
> 1. `interp_L(table, f)`: given `[(f_offset_Hz, L_dBc_per_Hz), ...]`, interpolate L linearly in log10(f)
>    (power-law segments). Raise `ValueError` outside the table.
> 2. `band_integral(table, f1, f2)`: return ∫ 10^(L/10) df over [f1, f2] using the exact closed form of each power-law
>    segment. `rms_jitter(table, f0, f1, f2)` returns `(sigma_phi_rad, sigma_t_s)` with
>    σ_φ = √(2·∫L df) and σ_t = σ_φ / (2π f0).
> 3. Spurs are given separately in dBc: each contributes 2·10^(S/10) rad² (pure PM assumption).
> 4. `pcie_mag2(f, c)`: |H(f)|² for a common-clock jitter filter
>    H(s) = [H1(s)·e^(−sT) − H2(s)]·s/(s + ω_CDR), with H_i(s) = (2ζω_n s + ω_n²)/(s² + 2ζω_n s + ω_n²).
>    `filtered_jitter` evaluates all 16 (BW, peaking) corner combinations and returns the worst σ_t.
> 5. For two 100 MHz candidates, report the 12 kHz–20 MHz jitter and the filtered jitter, PASS/FAIL against a
>    0.25 ps allocation, and pick one.
>
> Example: a flat −150 dBc/Hz floor at 100 MHz integrated 12 kHz–20 MHz gives σ_t = 318.2 fs. A −60 dBc spur alone gives
> 2.2508 ps. With the filter, moving the lower integration limit from 1 kHz to 12 kHz must change the result by < 0.1 %.

## 3. 시작 코드

<!-- code: starter.py -->

데이터(결정적 생성기 — 다시 돌려도 JSON이 바이트 단위로 같다) 와 판정 스펙:

<!-- code: gen_tables.py -->

<!-- code: refclk_spec.json -->

```bash
cd lessons/HWLAB/HWLAB-M08
python3 -I starter.py ; echo "exit=$?"      # TODO 상태: 2/25 passed, exit=1
```

실제 출력 (일부):

```text
-- Part A: log-log interpolation, closed-form segment integral, sigma_phi -> sigma_t --
FAIL  interp_L at a table point == table value: NotImplementedError: interp_L
...
FAIL  same L(f) table at 2*f0: sigma_phi same, sigma_t halves: NotImplementedError: rms_jitter
-- Part B: naive integration error, spurs, weighted integral, PCIe-style filter --
      (setup failed: NotImplementedError: interp_L)
...
PASS  given PLL helpers: |H(j2pi BW)|^2 = 0.5 and peaking(zeta) == spec   [|H(BW)|^2=0.500000 peak=2.0000dB (target 2.00) zeta=0.7292]
FAIL  pcie_mag2 low-f asymptote ~ (2pi f T * f/fcdr)^2 within 5%: NotImplementedError: pcie_mag2
...
-- Part C: SSDB-4 rev A PCIE_REFCLK, Y2 vs Y3 --
PASS  spec + 2 oscillator tables loaded (100MHz, 7 points each)
      (setup failed: NotImplementedError: judge)
      (setup failed: NotImplementedError: judge)
      DECISION: none - all candidates FAIL
FAIL  Y2 -> PASS (filtered < 0.25ps alloc): KeyError: 'Y2'
...
2/25 passed
exit=1
```

> 눈여겨볼 것: 순진한 적분 3종(`integrate_naive`)은 주어졌지만 끝점 L을 `interp_L`로 구하므로 starter에선 같이 실패한다. 보간 규칙 하나가 모든 숫자의 바닥이다.

## 4. 힌트

<details>
<summary>힌트 1 — "로그 보간"은 정확히 무엇을 직선으로 잇나</summary>

데이터시트 그래프는 x축이 log f, y축이 dB다. 그 그래프에서 직선이면 L = La + (Lb − La)·log10(f/fa)/log10(fb/fa). 선형 전력으로는 S(f) = Sa·(f/fa)^b, b = (Lb − La)/10 / log10(fb/fa) = 기울기[dB/dec]/10. −20 dB/dec면 b = −2(1/f²), −10 dB/dec면 b = −1(1/f), 평탄이면 b = 0.

</details>

<details>
<summary>힌트 2 — 멱법칙 한 구간의 적분</summary>

∫_{fa}^{fb} Sa·(f/fa)^b df = Sa·fa/(b+1)·((fb/fa)^(b+1) − 1). b = −1이면 0/0이 되므로 Sa·fa·ln(fb/fa). 부동소수로 b가 정확히 −1이 안 나오므로 `abs(b + 1) < 1e-9` 로 분기한다(−1에 아주 가까운 b에서는 두 식이 연속이라 어느 쪽이든 결과가 같다).

</details>

<details>
<summary>힌트 3 — 왜 ×2 인가, 그리고 f0는 어디서 들어가나</summary>

L(f)는 한쪽 측파대만 센다. 위상 변조는 위·아래 두 측파대를 같은 크기로 만들기 때문에 위상 PSD는 S_φ = 2L. 그래서 σ_φ² = 2∫L df. f0는 마지막에만: 위상 1 rad = 주기의 1/(2π) → σ_t = σ_φ/(2πf0). 같은 L(f) 표를 2배 주파수 클럭에 붙이면 σ_t는 절반이다.

</details>

<details>
<summary>힌트 4 — 가중 적분은 log 축에서</summary>

L이 log f 축에서 매끈하므로 u = ln f로 바꾸면 df = f·du, ∫S·w·f du. 구간을 decade 당 100칸 Simpson으로. 표의 꺾인 점(기울기가 바뀌는 점)이 Simpson 노드 가운데 오면 오차가 커지니 `breakpoints()` 로 나눈 구간마다 따로 적분한다.

</details>

<details>
<summary>힌트 5 — PCIe 필터 한 줄</summary>

`s = complex(0, 2πf)`, `h1 = pll_H(s, c['wn1'], c['z1'])`, `h2 = pll_H(s, c['wn2'], c['z2'])`, `h3 = s/(s + 2π·fcdr)`, 결과 `abs((h1*cmath.exp(-s*T) - h2)*h3)**2`. 저주파에서 H1 ≈ H2 ≈ 1이고 e^(−sT) ≈ 1 − sT라 |H1e^(−sT) − H2| ≈ ωT — 이게 테스트의 점근선이다.

</details>

## 5. 풀이

### Part A — L(f) 표에서 σ_t까지

**단계별:**

| 단계 | 계산 | 왜 / 주의 |
|---|---|---|
| ① 표 읽기 | (f_i, L_i) 오름차순 | 데이터시트는 decade 점만 준다 — 점 사이 모양은 "로그-로그 직선" 이 관례 |
| ② 구간 나누기 | [f1, 표 점들…, f2] | 적분 끝 12 kHz, 20 MHz는 보통 표 점이 아니다 → 보간한 L에서 시작 |
| ③ 구간 지수 | b = log(Sb/Sa)/log(fb/fa) | b = 기울기/10. 1/f³(−30), 1/f²(−20), 1/f(−10), 플로어(0) |
| ④ 닫힌식 | Sa·fa/(b+1)·((fb/fa)^(b+1) − 1) | b = −1 이면 Sa·fa·ln(fb/fa) |
| ⑤ 위상 분산 | σ_φ² = 2·Σ ④ | ×2: SSB → 위상 PSD |
| ⑥ 시간 | σ_t = σ_φ/(2πf0) | 같은 L이면 f0가 높을수록 σ_t는 작다 |

**검증 표 (실제 출력):**

| 표 | 정답(손계산) | 코드 | 비고 |
|---|---|---|---|
| 평탄 −150, 12k–20M | 1.998800e-08 | 1.998800e-08 | σ_φ = 1.9994e-4 rad, **σ_t = 318.21 fs** |
| −20 dB/dec (10⁻²/f²), 12k–10M | 8.323333e-07 | 8.323333e-07 | σ_t = 2053.45 fs @100 MHz, 1026.72 fs @200 MHz |
| −10 dB/dec (10⁻⁷/f), 12k–1M | 4.422849e-07 | 4.422849e-07 | b = −1 분기 |

**외워 둘 감각 — 플로어 하나의 기여:** 평탄 L0가 대역 B에 걸쳐 있으면 σ_t = √(2·10^(L0/10)·B)/(2πf0). −150 dBc/Hz × 20 MHz → 318 fs @100 MHz. 플로어 10 dB 개선 = 지터 1/√10. close-in의 −20 dB/dec 구간은 ∫ ≈ S(f1)·f1 이라 **적분 하한 근처 값이 지배**한다(1/f² 면 하한을 1 kHz → 12 kHz로 바꾸는 것만으로 분산이 12배 차이).

**L(f) 기울기가 말해 주는 잡음 원인 (Leeson 모델 관점):**

| 기울기 | 성분 | 주로 어디서 |
|---|---|---|
| −30 dB/dec | flicker FM | 공진기 근처 능동소자의 1/f 잡음이 위상으로 변환 |
| −20 dB/dec | white FM | 공진기 대역폭(Leeson 주파수 f0/2Q) 안의 백색 잡음 |
| −10 dB/dec | flicker PM | 버퍼·출력단 1/f |
| 0 dB/dec (플로어) | white PM | 출력 버퍼·분주기·열잡음 — kT 기준 −174 dBm/Hz 대비 신호 레벨 |
| PLL 기반의 평탄 구간 + 혹 | 루프 대역 안 레퍼런스/PFD 잡음, 루프 peaking | 분수-N, 프로그래머블 오실레이터 |

### Part B — 순진한 적분은 왜 거짓말하나

데이터시트 점은 decade 간격이다. 그 사이를 **선형 f 축에서 직선**으로 이으면, 가파르게 떨어지는 구간에서 실제 곡선(log 축의 직선 = 볼록한 멱법칙)보다 훨씬 위를 지나간다.

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 decade 구간 (10 kHz → 100 kHz, −20 dB/dec): 선형 f 축에서 본 S(f)</text>
  <line x1="70" y1="230" x2="720" y2="230" stroke="currentColor"/>
  <line x1="70" y1="40" x2="70" y2="230" stroke="currentColor"/>
  <path d="M 70 50 C 110 160, 200 215, 720 228" fill="none" stroke="currentColor" stroke-width="2.5"/>
  <path d="M 70 50 L 720 228 L 720 230 L 70 230 Z" class="f-warn" opacity=".35"/>
  <line x1="70" y1="50" x2="720" y2="228" stroke="currentColor" stroke-dasharray="6 4"/>
  <g fill="currentColor" font-size="12">
    <text x="78" y="46">Sa = 10⁻¹⁰ (−100 dBc/Hz @10 kHz)</text>
    <text x="560" y="218">Sb = 10⁻¹² (−120 @100 kHz)</text>
    <text x="300" y="120">점선 = 'trapz' (선형 사다리꼴): 면적 4.5×10⁻⁶</text>
    <text x="150" y="190">실선 = 실제 1/f²: 면적 9×10⁻⁷</text>
    <text x="80" y="250">10 kHz</text><text x="680" y="250">100 kHz</text>
    <text x="150" y="250">선형 주파수 축 → 같은 decade 안에서도 대부분의 폭이 '이미 떨어진' 영역</text>
  </g>
</svg>
```

**실제 출력 (`demo_bands.py` 1번, σ_t [ps], 괄호는 닫힌식 대비 σ 오차):**

```text
1) integration method, 12k-20M random part only [ps rms]  (error vs closed form)
   table                           closed             trapz           db_linf              rect
   -20dB/dec S=1e-2/f^2 (12k-10M)     2.053   4.270 (  +108%)   2.876 (   +40%)   5.998 (  +192%)
   Y2 100 MHz HCSL 크리스털 XO          1.226   2.206 (   +80%)   1.612 (   +32%)   3.058 (  +149%)
   Y3 100 MHz HCSL 분수-N PL          0.916   0.935 (    +2%)   0.931 (    +2%)   0.984 (    +7%)
```

(solution의 테스트는 **적분값** 오차를 본다: trapz +332.5 %, db_linf +96.2 %, rect +753.2 % — σ는 그 제곱근이라 +108 / +40 / +192 %.)

| 방법 | 무엇을 가정하나 | 오차 방향·크기 |
|---|---|---|
| `trapz` | 선형 S를 선형 f에서 직선 | 볼록 곡선의 현(chord) → 과대. −20 dB/dec decade 구간에서 적분 5배 |
| `db_linf` | dB를 선형 f에서 직선 | log 축 직선보다 오래 높게 머묾 → 과대 |
| `rect` | 왼쪽 점 L × 구간 폭 ("L 곱하기 대역폭") | 떨어지는 구간이면 가장 큰 과대 |
| **닫힌식** | log-log 직선 = 데이터시트 그래프 그대로 | 표의 정의와 일치 (오차 0) |

핵심: **평탄한 표(Y3)에서는 방법 차이가 2 %, 가파른 표(Y2)에서는 80 %**. 그래서 "엑셀로 사다리꼴 했더니 맞던데요" 는 플로어 위주 오실레이터에서만 우연히 맞은 것이다. 벤더 계산기도 이 log-log 구간 적분을 쓴다(구간 사이 모양을 모르는 이상, 데이터시트 그래프를 그린 방식과 같은 가정이 유일하게 일관적이다).

**스퍼는 dBc, 잡음은 dBc/Hz.** 순수 위상 변조 φ(t) = β·sin(2πf_m t)는 양쪽 측파대를 각 (β/2)² 로 만든다. 한쪽 스퍼 S dBc = 20·log10(β/2) → 위상 분산 β²/2 = 2·10^(S/10). −60 dBc 하나 = √2·10⁻³ rad = **2.2508 ps @100 MHz** (실제 출력). Y3의 −80 dBc 스퍼 하나가 0.225 ps — 랜덤 0.916 ps에 RSS로 더해 0.943 ps가 된다. 스퍼를 표의 한 점(dBc/Hz)처럼 넣으면 결과가 보간 구간 폭(=분해능 대역폭, RBW 가정)에 따라 마음대로 변하므로 틀린다. 스퍼는 결정적(deterministic) 지터이므로 TJ 예산에선 RMS가 아니라 피크(√2·RMS)로 따로 다룬다(HWLAB-M10 참고).

**적분 구간이 숫자를 정한다 (실제 출력, `demo_bands.py` 2번):**

```text
2) integration band vs RMS jitter [ps]  (random + spurs in band, no filter)
   osc         12k-20M       1k-20M     100k-20M       12k-5M      12k-50M       1M-50M   PCIe filt
   Y2            1.226        1.930        0.557        1.220        1.233        0.226       0.046
   Y3            0.943        0.951        0.936        0.500        1.380        1.355       0.311
```

Y2는 하한에 민감(1k–20M 1.93 ps vs 100k–20M 0.56 ps), Y3는 상한에 민감(12k–5M 0.50 ps vs 12k–50M 1.38 ps). 같은 "RMS 지터" 라는 이름이라도 **대역이 다르면 다른 양**이다. 12 kHz–20 MHz는 SONET OC-48 지터 생성(jitter generation) 측정 대역에서 온 데이터시트 관행일 뿐, PCIe가 보는 대역이 아니다.

**PCIe common-clock 형 필터.** 송신기(Tx)와 수신기(Rx)가 같은 refclk를 받는다. 두 PLL이 같은 refclk 지터를 따라가면 데이터와 수신 클럭이 **같이 흔들려서 상쇄**된다. 상쇄가 깨지는 원인은 ① 두 PLL의 대역폭·peaking 차이(H1 ≠ H2), ② 두 경로의 지연 차 T, 그리고 ③ 수신 CDR은 자기 대역(여기서 10 MHz) 아래 지터를 추적해 버리므로 남는 건 고역 성분이다.

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">PCIe common-clock 지터 전달 모델 (대표형)</text>
  <rect x="20" y="105" width="110" height="44" class="f-acc" stroke="currentColor"/>
  <text x="34" y="125" fill="currentColor">REFCLK Y2/Y3</text><text x="38" y="141" fill="currentColor" font-size="12">L(f) dBc/Hz</text>
  <rect x="200" y="45" width="130" height="44" class="f-mut" stroke="currentColor"/>
  <text x="212" y="65" fill="currentColor">Tx PLL  H1(s)</text><text x="212" y="81" fill="currentColor" font-size="12">BW 2–4 MHz, pk ≤ 2 dB</text>
  <rect x="200" y="165" width="130" height="44" class="f-mut" stroke="currentColor"/>
  <text x="212" y="185" fill="currentColor">Rx PLL  H2(s)</text><text x="212" y="201" fill="currentColor" font-size="12">BW 2–5 MHz, pk ≤ 1 dB</text>
  <rect x="380" y="45" width="110" height="44" class="f-mut" stroke="currentColor"/>
  <text x="392" y="65" fill="currentColor">지연 e^(−sT)</text><text x="392" y="81" fill="currentColor" font-size="12">T = 12 ns</text>
  <circle cx="545" cy="127" r="18" class="f-warn" stroke="currentColor"/>
  <text x="537" y="132" fill="currentColor">Σ</text>
  <text x="510" y="98" fill="currentColor" font-size="12">+</text><text x="510" y="165" fill="currentColor" font-size="12">−</text>
  <rect x="595" y="105" width="140" height="44" class="f-mut" stroke="currentColor"/>
  <text x="604" y="125" fill="currentColor">CDR  s/(s+ω_CDR)</text><text x="604" y="141" fill="currentColor" font-size="12">1차 HPF 10 MHz</text>
  <g stroke="currentColor" fill="none">
    <path d="M 130 120 L 165 120 L 165 67 L 200 67"/><path d="M 165 120 L 165 187 L 200 187"/>
    <path d="M 330 67 L 380 67"/><path d="M 490 67 L 545 67 L 545 109"/>
    <path d="M 330 187 L 545 187 L 545 145"/><path d="M 563 127 L 595 127"/>
  </g>
  <text x="20" y="236" fill="currentColor" font-size="12">|H(f)|² = |H1·e^(−sT) − H2|² · |s/(s+ω_CDR)|²  →  σ_φ² = 2∫L(f)|H(f)|² df + Σ 스퍼 2·10^(S/10)·|H(f_sp)|²</text>
  <text x="20" y="256" fill="currentColor" font-size="12">판정 = 16개 (BW, peaking) 모서리 조합 중 최악값</text>
</svg>
```

PLL 전달함수 H(s) = (2ζω_n s + ω_n²)/(s² + 2ζω_n s + ω_n²) (type-2, 2차). 스펙은 ζ·ω_n이 아니라 **−3 dB 대역폭과 peaking**으로 주므로 변환이 필요하다(코드에 주어짐, 유도는 이렇다):

- x = (ω/ω_n)², a = 4ζ² 로 두면 |H|² = (1 + a·x)/((1 − x)² + a·x).
- peaking: d|H|²/dx = 0 → a·x² + 2x − 2 = 0 → x* = (√(1+2a) − 1)/a. 이 값의 |H|²가 peaking. ζ에 대해 단조 감소라 이분법으로 역산: **0.01 dB → ζ = 14.387, 1 dB → 1.153, 2 dB → 0.729** (실제 출력).
- −3 dB: |H|² = 1/2 → x² − (2 + a)x − 1 = 0 → (ω_3dB/ω_n)² = (1 + 2ζ²) + √((1 + 2ζ²)² + 1). 테스트에서 |H(j2π·BW)|² = 0.500000 확인.

**|H(f)|² 모양 (Y3 최악 조합, 실제 출력 `demo_bands.py` 4번):**

```text
4) |H(f)|^2 of Y3 worst combo (PLL1 4MHz/2dB, PLL2 5MHz/0.01dB, T=12ns, CDR 10MHz)
   f            10k      100k        1M        2M        5M       10M       20M       50M
   |H|^2   1.25e-12  1.53e-08  4.58e-04  9.76e-03  5.70e-02  7.85e-02  6.84e-02  2.07e-02
   dB        -119.0     -78.2     -33.4     -20.1     -12.4     -11.1     -11.6     -16.8
```

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">PCIe 형 필터 |H(f)|² [dB] — 대역통과: close-in은 −80~−120 dB, 5–20 MHz만 −11 dB</text>
  <rect x="80" y="36" width="340" height="244" class="f-ok" opacity=".25"/>
  <rect x="420" y="36" width="221" height="244" class="f-warn" opacity=".3"/>
  <text x="100" y="300" fill="currentColor" font-size="12">Y2 지터² 98 %(12k–1M) → 걸러짐</text>
  <text x="440" y="300" fill="currentColor" font-size="12">Y3 지터² 92 %(1M–20M) → 통과</text>
  <g stroke="currentColor" stroke-dasharray="3 4" opacity=".35">
    <line x1="80" y1="80" x2="710" y2="80"/><line x1="80" y1="120" x2="710" y2="120"/>
    <line x1="80" y1="200" x2="710" y2="200"/>
  </g>
  <line x1="80" y1="280" x2="710" y2="280" stroke="currentColor"/>
  <line x1="80" y1="36" x2="80" y2="280" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="74" y="44">0 dB</text><text x="74" y="84">−20</text><text x="74" y="124">−40</text><text x="74" y="204">−80</text><text x="74" y="276">−120</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="80" y="318">10k</text><text x="250" y="318">100k</text><text x="420" y="318">1M</text><text x="590" y="318">10M</text><text x="706" y="318">50M</text>
  </g>
  <polyline points="80,278 250,196 420,107 471,80 539,65 590,62 641,63 709,74" fill="none" stroke="currentColor" stroke-width="2.5"/>
  <g fill="currentColor" font-size="12">
    <text x="260" y="250">저주파: ≈ (2πfT · f/f_CDR)² — 40 dB/dec 상승</text>
    <text x="560" y="52">CDR이 못 따라가는 대역</text>
  </g>
</svg>
```

"(2πfT)·(f/f_CDR)" 점근선은 테스트로 확인했다: 1 kHz에서 5.687e-17 vs 근사 5.685e-17. 그래서 **필터를 적용하면 적분 하한(1 kHz vs 12 kHz)이 사실상 무의미** — 상대 차 6.8e-10(실제 출력), 반대로 필터 없이 Y2는 하한만 바꿔도 분산이 148 % 바뀐다.

**16개 조합 (Y3, 실제 출력 일부, `demo_bands.py` 3번):**

```text
3) Y3 per PCIe-filter combo [ps]  (z = zeta from peaking)
   PLL1 BW  pk1 dB   z1      PLL2 BW  pk2 dB   z2         sigma
   4MHz     2        0.729   5MHz     0.01     14.387     0.311
   4MHz     0.01     14.387  5MHz     0.01     14.387     0.309
   2MHz     2        0.729   5MHz     0.01     14.387     0.295
   ...
   2MHz     0.01     14.387  2MHz     1        1.153      0.122
   2MHz     2        0.729   2MHz     1        1.153      0.111
```

최악/최선이 0.311 / 0.111 ps — 조합에 따라 3배. 스펙은 "허용된 Tx/Rx PLL 중 어떤 조합에서도" 만족을 요구하므로 **최악값**으로 판정한다. 대역폭이 가장 다른(2 vs 5 MHz, 4 vs 5 MHz에서 peaking이 다른) 조합이 최악이 되는 이유: H1 − H2의 차가 클수록 상쇄가 깨진다. 실제 컴플라이언스 도구는 BW/peaking 범위를 더 촘촘히 훑는다 — 모서리 16개는 학습용 단순화다.

### Part C — 두 후보, 두 숫자, 하나의 결정

실제 출력 (`python3 -I solution.py --report`):

```text
PASS net=PCIE_REFCLK osc=Y2 filt=0.046ps (PCIe형, worst of 16: PLL1 2MHz/2dB PLL2 5MHz/0.01dB) <= 0.25ps alloc; 12k-20M=1.226ps
FAIL net=PCIE_REFCLK osc=Y3 filt=0.311ps (PCIe형, worst of 16: PLL1 4MHz/2dB PLL2 5MHz/0.01dB) > 0.25ps alloc; 12k-20M=0.943ps
DECISION: Y2 (filt 0.046ps); WARNING 12k-20M headline would pick Y3 (0.943ps) - wrong metric for PCIe
```

| | Y2 (크리스털 XO) | Y3 (PLL 기반) |
|---|---|---|
| 12k–20M (헤드라인) | 1.226 ps — 더 나빠 보임 | 0.943 ps — 더 좋아 보임 |
| 지터² 분포 (연습 2) | 12k–100k 79.4 %, 100k–1M 18.4 % | 1M–10M 49.6 %, 10M–20M 42.8 % |
| PCIe 형 필터 최악 | **0.046 ps** (26.4배 감소) | **0.311 ps** (3.0배 감소) |
| 판정 (osc 배정 0.25 ps) | PASS | FAIL |

해석: 필터는 CDR이 추적하는 저주파를 버리고 5–20 MHz를 통과시킨다. Y2의 잡음은 거의 전부 버려지는 쪽에, Y3의 잡음(플로어 + 6.25 MHz 스퍼)은 통과하는 쪽에 있다. **"어느 오프셋에 잡음이 있느냐"가 "적분값이 얼마냐"보다 중요**하다. 0.25 ps 배정은 링크 한도 0.5 ps(Gen4 대표값) 중 오실레이터 몫이다 — 나머지는 fanout buffer의 additive 지터, 전원 잡음, 크로스토크가 RSS로 먹는다(독립 랜덤 성분은 분산이 더해진다).

하드웨어 엔지니어에게 보낼 한 줄:
`FAIL net=PCIE_REFCLK osc=Y3 filt=0.311ps (worst PLL1 4MHz/2dB vs PLL2 5MHz/0.01dB) > 0.25ps alloc — 플로어 −141 dBc/Hz + 6.25 MHz −80 dBc 스퍼가 PCIe 통과대역(5–20 MHz)에 있음; Y2 0.046ps PASS → Y2 채택, 실측(위상 잡음 분석기, 실장 상태)으로 확인 예정`

**실장 후 실측(장비가 있을 때):** 신호원 분석기/위상 잡음 분석기(cross-correlation 방식)로 HCSL 차동 출력을 balun 또는 차동 프로브로 받아 L(f)를 측정 → 같은 코드에 표로 넣는다. 데이터시트 typical과 실장 상태는 다르다 — 전원 리플이 위상 잡음에 스퍼로 올라오고(HWLAB-B08 참고), fanout buffer가 플로어를 올린다. 실시간 스코프로 TIE를 떠서 같은 필터를 적용하는 방법(PCI-SIG가 배포하는 Clock Jitter Tool 류)도 있지만, 스코프 자체 지터 바닥이 수십~수백 fs 수준이라(모델마다 다름) 0.05 ps 급은 위상 잡음 분석기가 맞다. 스코프 TIE·period jitter 정의는 HWLAB-M07 참고.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"     # 25/25 passed, exit=0 (약 0.1 s)
python3 -I solution.py --report             # Part C 리포트만
python3 -I -W error -X dev solution.py       # 경고 없음 (exit 0)
python3 -I gen_tables.py                    # JSON 재생성 → sha256 동일 (결정적)
python3 -I demo_bands.py                    # §5 의 표 4개
```

실제 출력 (`python3 -I solution.py`):

```text
-- Part A: log-log interpolation, closed-form segment integral, sigma_phi -> sigma_t --
PASS  interp_L at a table point == table value
PASS  interp_L at log-midpoint sqrt(1e4*1e5) == -110 (straight line in log f)   [-110.000000]
PASS  interp_L outside table -> ValueError (no extrapolation)
PASS  flat -150dBc/Hz, 12k-20M: integral == 1e-15*(20e6-12e3)   [1.998800e-08 vs 1.998800e-08]
PASS  -20dB/dec (S=1e-2/f^2), 12k-10M: integral == 1e-2*(1/12k - 1/10M)   [8.323333e-07 vs 8.323333e-07]
PASS  -10dB/dec (S=1e-7/f, b=-1 branch), 12k-1M: integral == 1e-7*ln(1M/12k)   [4.422849e-07 vs 4.422849e-07]
PASS  rms_jitter flat -150, 12k-20M @100MHz: sigma_phi=sqrt(2*int), sigma_t=318.2fs   [sigma_phi=1.9994e-04 rad sigma_t=318.21 fs]
PASS  same L(f) table at 2*f0: sigma_phi same, sigma_t halves   [2053.45fs @100M vs 1026.72fs @200M]
-- Part B: naive integration error, spurs, weighted integral, PCIe-style filter --
      -20dB/dec truth=8.3233e-07 | trapz +332.5% | db_linf +96.2% | rect +753.2% (integral error)
PASS  naive trapezoid (linear S between table points) overestimates > +100%   [+332.5%]
PASS  naive dB-linear-in-f overestimates > +50%   [+96.2%]
PASS  naive rect (L x bandwidth, left point) overestimates > +300%   [+753.2%]
PASS  spur -60dBc @1MHz in band: sigma_t = sqrt(2)*10^(-60/20)/(2pi f0) = 2.2508ps   [2.2508 ps]
PASS  spur outside band (30MHz vs 12k-20M) contributes 0
PASS  integrate_weighted(w=1) == closed form within 1e-6 (Simpson in ln f, split at table points)   [rel err 1.11e-09]
PASS  flat -150 x 1st-order HPF |f^2/(f^2+fc^2)| == analytic (f2-f1) - fc*(atan(f2/fc)-atan(f1/fc))   [rel err 2.16e-09]
PASS  given PLL helpers: |H(j2pi BW)|^2 = 0.5 and peaking(zeta) == spec   [|H(BW)|^2=0.500000 peak=2.0000dB (target 2.00) zeta=0.7292]
PASS  pcie_mag2 low-f asymptote ~ (2pi f T * f/fcdr)^2 within 5%   [|H|^2(1kHz)=5.687e-17 approx (wT*f/fcdr)^2=5.685e-17]
PASS  pcie_mag2 == 0 when PLL1 == PLL2 and T = 0 (common clock jitter cancels)
PASS  filtered integral: lower bound 1kHz vs 12kHz changes < 0.1% (filter kills close-in)   [1k vs 12k lower bound: rel diff 6.8e-10 (unfiltered: 148%)]
-- Part C: SSDB-4 rev A PCIE_REFCLK, Y2 vs Y3 --
PASS  spec + 2 oscillator tables loaded (100MHz, 7 points each)
      PASS net=PCIE_REFCLK osc=Y2 filt=0.046ps (PCIe형, worst of 16: PLL1 2MHz/2dB PLL2 5MHz/0.01dB) <= 0.25ps alloc; 12k-20M=1.226ps
      FAIL net=PCIE_REFCLK osc=Y3 filt=0.311ps (PCIe형, worst of 16: PLL1 4MHz/2dB PLL2 5MHz/0.01dB) > 0.25ps alloc; 12k-20M=0.943ps
      DECISION: Y2 (filt 0.046ps); WARNING 12k-20M headline would pick Y3 (0.943ps) - wrong metric for PCIe
PASS  Y2 -> PASS (filtered < 0.25ps alloc)
PASS  Y3 -> FAIL (filtered > 0.25ps alloc)
PASS  headline 12k-20M ranks them the other way (Y2 > Y3)   [Y2 1.226 ps, Y3 0.943 ps]
PASS  filter shrinks Y2 by > 10x but Y3 by < 4x (where the noise sits matters)   [Y2 x26.4, Y3 x3.0]
PASS  decision picks Y2 and warns the headline metric would pick Y3
25/25 passed
```

<!-- code: demo_bands.py -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `phi = sqrt(integral)` (×2 빠짐) | L은 SSB, 위상 PSD는 2L → σ가 1/√2배로 과소 | `phi = math.sqrt(2.0 * band_integral(...))` |
| 데이터시트 점 사이를 선형 사다리꼴 | 볼록한 멱법칙의 현 → −20 dB/dec 구간 적분 5배 과대 | 구간별 멱법칙 닫힌식 (`band_integral`) |
| `b == -1` 분기 없음 | 정확히 −10 dB/dec면 0으로 나눔 | `if abs(b + 1) < 1e-9: Sa*fa*ln(fb/fa)` |
| 12 kHz를 표의 10 kHz 점에서 시작 | 1/f² 영역이면 하한 값이 지배 → 수십 % 차이 | 끝점 L을 보간하고 [f1, f2]만 적분 |
| 스퍼를 dBc/Hz 점으로 표에 삽입 | 결과가 임의의 "폭"에 비례, 단위가 다름 | 스퍼 분산 2·10^(S/10) 를 따로 더함 |
| 표 범위 밖을 외삽 | 데이터시트가 말하지 않은 잡음을 지어냄 | `ValueError`; 필요하면 범위·가정을 리포트에 명시 |
| f0를 체배/분주 후에도 원래 L 그대로 | ×N이면 L + 20·log10 N (σ_t는 그대로), 플로어는 따로 | 연습 1 `scale_table` |
| 12k–20M 값으로 PCIe 판정 | PCIe는 필터 적용 대역통과 숫자를 본다 | `filtered_jitter` 최악 조합 |
| 필터 조합 하나(공칭)만 계산 | 스펙은 허용 범위 전체에서 만족 요구, 조합 간 3배 차 | 16 모서리(또는 더 촘촘한) 스윕 최악값 |
| 필터 적분을 선형 f 격자로 | 1 kHz–50 MHz를 균일 격자로 하면 저주파가 몇 점뿐 | ln f 축 Simpson, 표 꺾인 점에서 분할 |
| 데이터시트 typical로 최종 결정 | 실장 후 전원 리플 스퍼·버퍼 플로어가 더해짐 | 실장 보드를 위상 잡음 분석기로 측정해 같은 코드로 재판정 |

## 8. 면접 Q&A

<details>
<summary>Q1. 위상 잡음 표를 RMS 지터로 바꾸는 식을 쓰고, 각 항의 의미를 설명해 보세요.</summary>

σ_t = √(2∫_{f1}^{f2} 10^(L(f)/10) df) / (2πf0). L은 1 Hz 대역의 SSB 잡음/반송파(dBc/Hz) → 선형으로 바꿔 적분하면 SSB 잡음 전력비, ×2는 위상 변조가 양쪽 측파대를 대칭으로 만들기 때문(S_φ = 2L, 작은 각 근사), √ 하면 rad rms, 2πf0로 나누면 초. 적분은 log-log 직선 구간마다 멱법칙 닫힌식.
꼬리질문: "작은 각 근사가 깨지는 경우는?" → σ_φ가 0.1 rad 수준을 넘거나, 아주 가까운 오프셋에서 그 구간까지 적분한 위상이 0.1 rad 수준에 이를 만큼 잡음이 크면 L(f) ≈ S_φ/2 관계가 깨진다(잡음 측파대가 반송파 전력을 눈에 띄게 빼앗음). 그래서 적분 하한을 0으로 두지 않는다. 보통의 refclk는 σ_φ ~ 10⁻⁴ rad이라 문제없다.

</details>

<details>
<summary>Q2. 왜 하필 12 kHz–20 MHz인가요? 그 숫자로 PCIe refclk를 판정해도 되나요?</summary>

12 kHz–20 MHz는 SONET OC-48 지터 생성(jitter generation) 측정 대역에서 온 데이터시트 관행이다(OC-192는 50 kHz–80 MHz). 오실레이터끼리 비교하는 공통 척도일 뿐, PCIe는 Tx/Rx PLL과 CDR 전달함수로 만든 대역통과 필터를 적용한 숫자를 본다. 이 레슨에서 12k–20M로는 Y3(0.943 ps)가 Y2(1.226 ps)보다 좋아 보이지만, 필터 적용 후엔 Y2 0.046 ps vs Y3 0.311 ps로 뒤집힌다.
꼬리질문: "그럼 데이터시트에 PCIe 지터 숫자가 따로 적혀 있으면?" → 어떤 세대(Gen)·어떤 아키텍처(common clock / SRIS)·어떤 조건(SSC on/off)의 필터인지 확인한다. 보드에선 fanout buffer와 전원 잡음이 더해지므로 결국 실장 측정으로 닫는다.

</details>

<details>
<summary>Q3. 점 7개짜리 표를 엑셀 사다리꼴로 적분했더니 벤더 계산기보다 80 % 크게 나왔습니다. 누가 맞나요?</summary>

벤더 계산기. 데이터시트 그래프는 log-log 축에서 점을 직선으로 이은 것이므로 그 사이는 멱법칙(S ∝ f^b)이다. 선형 f 축에서 선형 S를 사다리꼴로 이으면 볼록한 곡선의 현을 적분하게 되어 가파른 구간(−20, −30 dB/dec)에서 크게 과대가 된다. −20 dB/dec decade 구간 하나면 적분 5배, 이 레슨의 Y2 전체로는 σ +80 %. 평탄한 플로어 구간에선 차이가 작아서(Y3 +2 %) "맞는 것처럼" 보일 수 있다.
꼬리질문: "점이 더 촘촘하면?" → 점 간격이 좁을수록 현과 곡선의 차가 줄어 사다리꼴도 수렴한다. 하지만 표가 decade 점뿐이라면 log-log 가정이 유일하게 일관적이고, 그 가정 하의 정답은 닫힌식이다.

</details>

<details>
<summary>Q4. −80 dBc 스퍼 하나는 지터로 얼마인가요? 랜덤 지터와 어떻게 합치나요?</summary>

순수 PM 가정에서 한쪽 스퍼 S dBc → 위상 분산 2·10^(S/10) → √(2·10⁻⁸) = 1.414×10⁻⁴ rad → 100 MHz에서 0.225 ps rms. 사인파 위상 변조이므로 피크는 √2배(0.318 ps) — 결정적 지터(PJ)다. RMS 끼리는 독립이면 분산을 더할 수 있어서 Y3의 0.916 ps 랜덤과 합쳐 0.943 ps. 하지만 BER 기반 TJ 예산에선 랜덤(가우스, Q·σ로 늘어남)과 스퍼(유계, 피크)를 따로 다뤄 dual-Dirac 식으로 합친다(HWLAB-M10).
꼬리질문: "스펙트럼에 스퍼가 한쪽만 보이면?" → AM과 PM이 섞이면 측파대가 비대칭이 된다. 위상 잡음 분석기는 AM을 억제하고 위상만 재므로, 스펙트럼 분석기의 직접 측정과 다를 수 있다. 위 식은 순수 PM 가정이다.

</details>

<details>
<summary>Q5. PCIe common clock에서 refclk 지터가 왜 "필터를 거쳐" 판정되나요? 필터 모양을 그려 보세요.</summary>

Tx와 Rx가 같은 refclk를 쓰므로 저주파 지터는 데이터 에지와 수신 샘플링 클럭에 똑같이 실려 상쇄된다. 상쇄를 깨는 것은 두 PLL의 BW·peaking 차(H1 ≠ H2)와 경로 지연 차 T, 그리고 CDR이 추적하지 못하는 고주파. 그래서 H = (H1·e^(−sT) − H2)·s/(s + ω_CDR). 모양은 대역통과: 저주파에서 (2πfT·f/f_CDR)² 로 40 dB/dec 상승, 5–20 MHz 부근에서 약 −11 dB 최대, PLL 대역 위에서 다시 감소(이 레슨 최악 조합: 10 kHz −119 dB, 10 MHz −11.1 dB, 50 MHz −16.8 dB).
꼬리질문: "SRIS(Separate Refclk with Independent SSC)면?" → Tx와 Rx refclk가 독립이라 H1 − H2 상쇄가 없다. 각 refclk 지터가 거의 그대로 CDR 고역통과만 거쳐 들어오므로 요구가 훨씬 엄격하고, SSC 추적 부담(CDR 대역·버퍼)이 커진다. 세대별 필터·한도는 Base Spec에서 확인해야 한다 — 여기 숫자는 대표값이다.

</details>

<details>
<summary>Q6. 스펙이 PLL을 ζ·ωn이 아니라 "BW 2–4 MHz, peaking 0.01–2 dB"로 주는 이유와, 그걸 전달함수로 바꾸는 방법은?</summary>

설계자가 측정·보장할 수 있는 양이 −3 dB 대역폭과 peaking이기 때문이다. 2차 type-2 PLL에서 peaking은 ζ만의 함수: x = (ω/ωn)², a = 4ζ²면 최대점 x* = (√(1+2a) − 1)/a, 그 |H|²가 peaking — ζ에 단조 감소라 이분법으로 역산(0.01 dB → ζ 14.4, 1 dB → 1.15, 2 dB → 0.73). 그다음 (ω_3dB/ωn)² = (1+2ζ²) + √((1+2ζ²)²+1)로 ωn.
꼬리질문: "왜 모서리 조합의 최악을 보나?" → 실리콘마다 PLL BW·peaking이 허용 범위 안에서 흩어진다. 어떤 Tx와 어떤 Rx가 만나도 링크가 돌아야 하므로 범위 전체의 최악으로 판정한다. 이 레슨에서 같은 Y3도 조합에 따라 0.111–0.311 ps.

</details>

<details>
<summary>Q7. 100 MHz XO를 25 MHz로 분주하면 위상 잡음과 지터는 어떻게 되나요?</summary>

이상적 분주(÷N)는 위상 편차를 1/N로 줄이므로 L은 20·log10 N = 12.04 dB 내려가고, f0도 1/N이라 σ_t는 그대로다(연습 1: 1.2257 ps → 1.2257 ps). 실제로는 분주기·출력 버퍼에 자체 플로어(예: −155 dBc/Hz)가 있어 12 dB 내려간 플로어가 그 아래로 못 가므로 σ_t가 오히려 커질 수 있다(연습 1: +15.2 %). 체배(×N, PLL)는 반대로 +20·log10 N.
꼬리질문: "그럼 지터를 줄이려면 높은 주파수로 분배하고 끝에서 분주하는 게 좋은가?" → 이상적이면 σ_t는 불변이니 분주 자체가 지터를 줄이지는 않는다. 플로어 지배(white PM) 영역에서는 같은 dBc/Hz 플로어라면 f0가 높을수록 σ_t가 작다(÷2πf0) — 그래서 분배 경로의 버퍼 플로어 dBc/Hz가 핵심 사양이 된다.

</details>

<details>
<summary>Q8. 위상 잡음으로 계산한 RMS 지터와, 스코프에서 잰 TIE·period jitter가 다른 이유는?</summary>

① 대역: 스코프 TIE는 레코드 길이 T_rec의 역수(그보다 느린 변동은 주파수 오차로 흡수)부터 샘플링으로 정해지는 상한까지를 본다 — 12k–20M과 다른 대역. ② 지표: period jitter는 한 주기 차분이라 위상 PSD에 4·sin²(πf/f0) 가중이 붙어 close-in을 거의 버린다(연습 3: Y2 period 88.6 fs vs phase 1226 fs). ③ 측정 바닥: 실시간 스코프의 자체 지터·수직 잡음이 수십~수백 fs 수준(모델마다 다름)이라 sub-100 fs 클럭엔 분해능이 부족하다(RSS로 빼는 보정은 가능하지만 한계가 있음). ④ 위상 잡음 분석기는 AM을 억제하고 cross-correlation으로 자체 잡음을 낮춘다.
꼬리질문: "그럼 스코프는 언제 쓰나?" → 데이터 패턴 의존 지터(DDJ), 크로스토크, 전원 결합 같은 시간 영역 현상, 그리고 실장 상태의 PCIe 필터 지터를 Clock Jitter Tool 류로 볼 때. 정의와 분해는 HWLAB-M07/M10.

</details>

## 9. 추가 연습

### 연습 1 — 분주·체배와 분주기 플로어

`scale_table(table, n, floor_db=None)`: L + 20·log10(n), 플로어가 있으면 그 아래로 못 내려감. Y2를 ÷4(25 MHz)하면 σ_t가 같고 σ_φ가 1/4인지, 분주기 플로어 −155 dBc/Hz면 σ_t가 10 % 넘게 커지는지 확인.

<details>
<summary>답 보기</summary>

위상 편차 φ가 1/n이면 L은 20·log10(n) dB 이동, 2πf0도 1/n이라 σ_t 불변. 실제 출력: `100M 1.2257ps / 25M 1.2257ps, sigma_phi ratio 4.0000`. 플로어 −155: Y2 원래 플로어 −158/−160이 ÷4로 −170/−172가 돼야 하지만 분주기가 −155에서 막는다 → `1.2257ps -> 1.4123ps (+15.2%)`. 25 MHz에서 −155 dBc/Hz 플로어는 100 MHz 기준으로 −143 dBc/Hz와 같은 지터 기여다 — 저주파 클럭일수록 같은 dBc/Hz 플로어가 더 아프다.

</details>

### 연습 2 — decade 별 지터 예산

`decade_budget(osc, f1, f2)`: [f1, f2]를 10^k 경계로 잘라 구간별 σ_φ²(랜덤 + 그 구간 스퍼)와 비율. 합이 전체와 같고, Y2는 12k–100k가 70 % 이상, Y3는 1M–20M이 60 % 이상인지.

<details>
<summary>답 보기</summary>

독립 성분의 분산은 더해지므로 구간별 비율이 곧 "지터² 점유율"이다. 실제 출력:
`Y2: 12k-100k 79.4%  100k-1M 18.4%  1M-10M 1.8%  10M-20M 0.5%`,
`Y3: 12k-100k 1.6%  100k-1M 6.0%  1M-10M 49.6%  10M-20M 42.8%`.
이 표가 Part C 판정의 "왜"다: PCIe 필터는 1 MHz 아래를 −33 dB 이하로 버리니 Y2의 98 %가 사라진다. 개선 방향도 정해진다 — Y3라면 출력 플로어(버퍼/분주기)와 분수-N 스퍼(정수 모드 주파수 계획, 스퍼 회피)를 봐야 하고, close-in을 아무리 좋게 해도 PCIe 숫자는 안 변한다.

</details>

### 연습 3 — 위상 잡음 → period jitter

`period_jitter(table, f0, f1, f2)` = √(2∫L·4sin²(πf/f0) df)/(2πf0). 평탄 플로어·f ≪ f0에서 작은 각 근사 2L0·(2π/f0)²·(f2³ − f1³)/3 와 1 % 이내, 그리고 Y2/Y3의 period jitter vs phase jitter 비교.

<details>
<summary>답 보기</summary>

TIE x(t)의 한 주기 차분 x(t+τ) − x(t)의 전달함수는 |1 − e^(−j2πfτ)|² = 4sin²(πfτ), τ = 1/f0. 작은 f에서 ≈ (2πf/f0)²이므로 close-in은 거의 버리고 먼 오프셋을 키운다. 실제 출력: `28.7964 fs vs 28.8675 fs` (12k–5 MHz, 근사는 sin x ≈ x 때문에 0.25 % 큼), `Y2 per 88.6fs vs phase 1226fs | Y3 per 609.9fs vs phase 916fs`. 플로어 지배 Y3는 period jitter도 크다. 주의: 실제 period jitter는 20 MHz가 아니라 f0/2 근처까지의 잡음을 다 보므로 이 값은 20 MHz까지만의 부분값이다. cycle-to-cycle은 차분을 한 번 더 해서 가중이 (4sin²)²이 된다(HWLAB-M07의 c2c = period 차분 관계).

</details>

### 연습 4 — 단위 환산

`units(phi_rad, f0, bitrate)`: σ_φ → 도(°), σ_t, UI(= σ_t × bitrate), 적분 위상 잡음 IPN = 10·log10(σ_φ²) dBc. 평탄 −150, 12k–20M에서 0.01146°, −73.98 dBc, 16 GT/s(UI 62.5 ps)에서 0.00509 UI.

<details>
<summary>답 보기</summary>

σ_φ = 1.9994×10⁻⁴ rad → ×180/π = 0.01146°, IPN = 10·log10(3.998×10⁻⁸) = −73.98 dBc, σ_t 318.2 fs / 62.5 ps = 0.00509 UI. 실제 출력: `deg=0.01146 ipn=-73.98dBc ui=0.00509`. RF 쪽(LO, ADC 클럭)은 IPN dBc나 °rms로, SerDes 쪽은 ps rms나 UI로 말한다. ADC 클럭 지터가 만드는 SNR 한계 SNR = −20·log10(2π·f_in·σ_t)도 같은 σ_t에서 나온다(DSP 쪽 응용).

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"      # 8/8 passed, exit=0
```

## 10. 핵심 정리

- σ_t = √(2∫10^(L/10) df)/(2πf0). ×2는 SSB → 위상 PSD, f0는 마지막에만. 평탄 −150 dBc/Hz × 20 MHz → 318 fs @100 MHz.
- 데이터시트 점 사이는 **log-log 직선 = 멱법칙**. 구간별 닫힌식 Sa·fa/(b+1)·((fb/fa)^(b+1) − 1), b = −1이면 ln. 선형 사다리꼴은 가파른 구간에서 σ +80~100 %.
- 1/f² 영역은 적분 하한이, 플로어는 상한(대역폭)이 지배한다. "RMS 지터" 는 대역과 같이 말한다.
- 스퍼는 dBc: 분산 2·10^(S/10)(순수 PM), 결정적 지터 — TJ에선 피크로 따로.
- PCIe common clock: |H|² = |H1·e^(−sT) − H2|²·|s/(s+ω_CDR)|² — 대역통과(저주파 (2πfT·f/f_CDR)²). peaking → ζ, BW → ωn, 모서리 조합의 **최악값**으로 판정.
- 같은 두 오실레이터가 12k–20M(Y2 1.226 > Y3 0.943 ps)과 PCIe 필터(Y2 0.046 < Y3 0.311 ps)에서 순위가 뒤집힌다 — 잡음이 **어느 오프셋에** 있는지가 결정한다.
- 분주 ÷N: L −20·log10 N, σ_t 불변(플로어가 막으면 증가). 독립 성분은 분산 합(RSS) — osc 배정 = 링크 한도 − buffer·전원·크로스토크 몫.
- 데이터시트 typical로 고르고, 실장 보드를 위상 잡음 분석기로 재서 같은 코드로 닫는다.

## 참고자료

- PCI-SIG, *PCI Express Base Specification* (Rev 3.0 이후 refclk jitter 절) / *CEM Specification* — common clock·SRIS refclk 지터 필터(H1, H2, 지연, CDR)와 세대별 한도. 이 레슨의 BW/peaking/지연/CDR/0.5 ps는 **대표값 — 스펙 원문 확인 필요**
- IEEE Std 1139, *Standard Definitions of Physical Quantities for Fundamental Frequency and Time Metrology — Random Instabilities* — L(f), S_φ(f) 정의와 L = S_φ/2 관계
- D. B. Leeson, "A Simple Model of Feedback Oscillator Noise Spectrum," *Proc. IEEE*, 1966 — 1/f³·1/f²·플로어 영역
- Analog Devices, MT-008 *Converting Oscillator Phase Noise to Time Jitter* — 구간 적분 절차, SSB×2 규약
- Silicon Labs AN256 *Integrated Phase Noise* 및 PCIe 클럭 지터 애플리케이션 노트 / Renesas(IDT) PCIe refclk 지터 계산 애플리케이션 노트 — 벤더 계산기의 log-log 적분과 PCIe 필터 적용 방식
- Telcordia GR-253-CORE (SONET) — OC-48 지터 생성 측정 대역(12 kHz–20 MHz)의 유래
- Keysight / Rohde & Schwarz 위상 잡음 측정 애플리케이션 노트 (cross-correlation 위상 잡음 분석기, 스펙트럼 분석기 직접 측정의 한계)
- 관련 레슨: HWLAB-M07(period·c2c·TIE), HWLAB-M10(RJ/DJ·dual-Dirac), HWLAB-M09(golden PLL), HWLAB-B08(전원 리플 → 스퍼), HWLAB-B05(스코프 BW·프로브)
