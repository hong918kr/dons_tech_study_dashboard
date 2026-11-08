---
id: HWLAB-M11
time: 85분
updated: 2026-11-08
---
# HWLAB-M11 BER Bathtub과 Q-scale 외삽

## 학습 목표

- BERT(bit error ratio tester)가 샘플링 위상을 UI 전체에 걸쳐 옮기며 BER을 재는 **bathtub 곡선**을 직접 만든다. PRBS7 checker(정렬 → 잠금 → 오류 카운트)로 센 오류 수가 "에지 지터가 샘플링 위상을 넘은 횟수"와 **정확히 같다**는 것을 보이고, 해석식 BER(x) = ρT·[P(J&gt;x) + P(J&gt;1−x)] 로 검증한다.
- 1e-6 깊이까지만 잰 bathtub을 **Q-scale**(Q = √2·erfc⁻¹(2·BER/ρ))로 펴서 가중 최소제곱 직선을 맞추고, 1e-12 eye opening(EO)과 그 **표준오차**, dual-Dirac 성분(RJ, DJδδ, TJ)을 외삽한다.
- 외삽이 비틀리는 지점을 수치로 안다: 정규화 ρ 관례(7.034 vs 6.840), 피팅 창에 DJ 영역을 넣는 실수, 정현 PJ처럼 dual-Dirac이 아닌 DJ, 측정 깊이, 그리고 **드문 큰 지터**(low-probability DJ) — 짧은 측정이 절대 못 보는 꼬리.
- 측정 시간·신뢰수준 trade-off(0 오류로 1e-12를 95 % 신뢰로 말하려면 3.0e12 비트 = 10 Gb/s에서 300 s)를 표로 말하고, "외톨이 오류(outlier) 검사 + 마스크 위상 장시간 측정"으로 외삽을 검증하는 판정 흐름을 만든다.

## 1. 문제

SerDes 링크 검증에서 "1e-12에서 eye가 몇 UI 열려 있나?"는 매일 묻는 질문이고, 그 답은 거의 항상 **외삽**이다 — Rivian/NVIDIA/Apple 실리콘·보드 검증 면접은 "그 1e-12 숫자를 몇 분 측정으로 어떻게 냈고, 그 외삽은 언제 거짓말하나?"를 꼭 묻는다.

BERT는 패턴 발생기(PG)가 PRBS를 보내고, 오류 검출기(ED)가 받은 비트를 **샘플링 위상 x**에서 판정해 기대 패턴과 비교한다. x를 UI 왼쪽 끝에서 오른쪽 끝까지 옮기며 위상마다 BER을 찍으면 욕조 모양 곡선이 나온다. 벽은 지터 때문에 생긴다: 왼쪽 벽은 **비트 시작 에지가 늦게 와서**(J &gt; x) 이전 비트를 읽는 오류, 오른쪽 벽은 **다음 에지가 일찍 와서**(J &lt; x − 1) 다음 비트를 읽는 오류다. 오류는 전이(transition)가 있는 비트 경계에서만 생기므로 전이 밀도 ρT(PRBS7 = 64/127)가 곱해진다.

  BER(x) = ρT · [ P(J &gt; x) + P(J &lt; x − 1) ] = ρT · [ P(J &gt; x) + P(J &gt; 1 − x) ]   (J가 0 대칭일 때)

```svg
<svg viewBox="0 0 760 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">eye 의 교차 지터 → 위상 x 마다 BER → bathtub (log BER)</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M60 40 L100 110 L140 40 M60 110 L100 40 L140 110"/>
    <path d="M620 40 L660 110 L700 40 M620 110 L660 40 L700 110"/>
    <line x1="140" y1="40" x2="620" y2="40"/><line x1="140" y1="110" x2="620" y2="110"/>
  </g>
  <path class="f-warn" opacity=".55" d="M78 140 Q86 118 94 140 Z M106 140 Q114 118 122 140 Z"/>
  <path class="f-warn" opacity=".55" d="M638 140 Q646 118 654 140 Z M666 140 Q674 118 682 140 Z"/>
  <text x="70" y="156" fill="currentColor" font-size="12">dual-Dirac ±DJ/2 + RJ</text>
  <line x1="240" y1="32" x2="240" y2="146" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="246" y="62" fill="currentColor" font-size="12">샘플링 위상 x</text>
  <path d="M250 80 L330 80" stroke="currentColor" marker-end="url(#a1)"/>
  <text x="250" y="98" fill="currentColor" font-size="12">0 → 1 UI sweep</text>
  <text x="96" y="30" fill="currentColor" font-size="12">0</text><text x="652" y="30" fill="currentColor" font-size="12">1 UI</text>
  <defs><marker id="a1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 Z" fill="currentColor"/></marker></defs>
  <g stroke="currentColor" opacity=".35" stroke-dasharray="3 4">
    <line x1="100" y1="180" x2="660" y2="180"/><line x1="100" y1="260" x2="660" y2="260"/>
    <line x1="100" y1="305" x2="660" y2="305"/><line x1="100" y1="350" x2="660" y2="350"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="184">BER 1</text><text x="20" y="264">1e-6</text><text x="20" y="309">1e-9</text><text x="20" y="354">1e-12</text>
  </g>
  <path d="M100 179 L128 179.3 L156 183.5 L172.8 196.6 L184 212 L195.2 233.4 L206.4 261" stroke="currentColor" stroke-width="2.5" fill="none"/>
  <path d="M206.4 261 L217.6 294.8 L233.3 350" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4" fill="none"/>
  <path d="M660 179 L632 179.3 L604 183.5 L587.2 196.6 L576 212 L564.8 233.4 L553.6 261" stroke="currentColor" stroke-width="2.5" fill="none"/>
  <path d="M553.6 261 L542.4 294.8 L526.7 350" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4" fill="none"/>
  <rect x="100" y="179" width="560" height="82" class="f-ok" opacity=".12"/>
  <text x="300" y="226" fill="currentColor" font-size="12">측정한 영역 (1e7 비트 → 1e-6 깊이)</text>
  <line x1="236" y1="340" x2="524" y2="340" stroke="currentColor" marker-start="url(#a1)" marker-end="url(#a1)"/>
  <text x="300" y="333" fill="currentColor" font-size="12" class="f-acc">EO(1e-12) = 외삽 (점선)</text>
  <line x1="209" y1="252" x2="551" y2="252" stroke="currentColor" opacity=".6"/>
  <text x="330" y="248" fill="currentColor" font-size="12">EO(1e-6)</text>
</svg>
```

이 레슨이 다른 레슨에서 빌려 오는 것 (전부 이 디렉터리 안에 축약해서 넣었다 — 다른 레슨을 import 하지 않는다):

- **SIV-M06 PRBS checker** → `prbs7()` + `prbs7_align()` + `prbs7_errors()` (잠금 checker, 오류 증식 없음). 자기동기 checker의 오류 증식은 연습 1.
- **HWLAB-M10 dual-Dirac** → `tie_dual_dirac()`: scope TIE 1e5 에지의 꼬리를 Q-scale로 피팅하는 축약판. TJ = DJδδ + 2·Q·RJ.
- 지터 모델: dual-Dirac DJ + 가우시안 RJ, 정현 PJ + RJ, 그리고 "확률 p로 ±s가 더해지는" 드문 큰 지터. 모두 **주입한 정답**이 있다.

"장시간 직접 측정"을 어떻게 했는지 정직하게 적는다. 1e-12를 직접 세려면 1e13 비트 정도가 필요한데 순수 Python으로는 불가능하다(이 레슨의 에지 카운터는 에지 하나에 약 0.3 µs → 1e13 비트면 약 18일). 그래서 비교 기준을 둘로 나눴다:

| 비교 기준 | 무엇을 했나 | 깊이 |
|---|---|---|
| **해석 정답** (`ber_analytic`, `eye_analytic`) | 주입한 지터 분포로 BER(x)를 정확히 계산 = 무한히 긴 측정과 같은 값 | 1e-12 이하까지 |
| **직접 카운트** (`demo_bathtub.py` §4) | 4e7 비트 레코드 하나를 끝까지 세서 해석식과 비교 | ~1e-7 (런타임 한계) |
| **마스크 위상 장시간 측정** (Part C `long`) | 위상 0.30 / 0.70 UI에서 2.5e7 비트씩 직접 카운트 | 오류가 나면 FAIL 증명, 0이면 ~1e-7까지만 말함 |

실제 BERT라면 3e12 비트는 10 Gb/s에서 5분이다 — 그 "5분 측정"을 대신하는 것이 해석 정답이다.

### Part A — PRBS7 checker, 비트 단위 시뮬레이션 == 에지 카운트, 해석 bathtub

1. `prbs7_align(rx)`: 받은 비트열이 PRBS7 주기(127)의 어느 위치 a에 맞는지 64비트 창으로 찾는다. 창 안에 오류가 있으면 다음 창으로(= BERT의 sync). `prbs7_errors(rx, a)`: 로컬 기준 패턴과 비교해 오류 수.
2. `bathtub_counts(jitters, P)`: 에지 지터 목록만으로 위상 k/P마다 오류 수 = #(j &gt; x) + #(j &lt; x − 1). 히스토그램 + 누적합으로 **위상 수와 무관하게 O(에지 수)**.
3. `simulate_link()`(주어짐)로 16 384비트를 비트 단위로 보내 13개 위상에서 checker로 센 오류가 2의 결과와 **한 개도 다르지 않음**을 확인한다.
4. `tail_prob(model, x)` = P(J &gt; x)를 dual-Dirac, 정현 PJ(아크사인 ⊛ 가우시안, 수치 적분), rare 혼합에 대해 구현하고, 1e7비트 Monte Carlo bathtub과 Poisson 4.5σ 이내로 맞춘다.

### Part B — Q-scale 피팅과 외삽, 그리고 외삽이 비틀리는 곳

1. `q_of_ber(ber, ρ)`(이분법 erfc⁻¹), `qscale_fit()`: 한쪽 꼬리에서 BER ≤ 1e-3, 오류 ≥ 5인 점만 골라 Q = m·x + c 를 **Poisson 역분산 가중**으로 피팅. σ = 1/|m|, μ = −c/m.
2. `extrapolate()`: Q_t = Q(1e-12)까지 두 직선을 연장 → EO, 델타 방법 표준오차, RJ = (σL+σR)/2, DJδδ = 1 − (μR − μL), TJ = 1 − EO.
3. 주입 정답과 비교: DD(DJδδ 0.20, RJ 0.020 UI) 1e7비트에서 EO 오차, ρ = 1로 잘못 정규화했을 때 RJ/DJ 분해가 얼마나 틀어지는지, TIE 1e5 에지(M10 방식)와 BERT 외삽의 불확도 차이, 정현 PJ에서 DJδδ &lt; DJpp, 피팅 창을 DJ 영역(BER 0.1)까지 넓혔을 때의 오차.

### Part C — VALBRD-B U7 리타이머 lane0 / lane3 판정

`bert_sweep.csv`(gen_bert.py가 만든 BERT export): lane마다 255위상 × 1e7비트 sweep + 마스크 위상(0.30, 0.70 UI) 2.5e7비트 장시간 측정. 스펙(`spec.json`): 1e-12에서 EO ≥ 0.40 UI(자체 대표값). `find_outliers()`로 "피팅이 오류 0.01개 미만을 예상하는 위상에서 난 오류"를 찾고, `judge_lane()`이 ① 마스크 장시간 오류 → FAIL ② 외톨이 오류 → INCONCLUSIVE ③ EO − 2·se ≥ 스펙 → PASS 순으로 판정한다. lane3에는 확률 1e-6으로 ±0.20 UI가 더해지는 드문 큰 지터를 심어 두었다 — **외삽만 보면 PASS(0.528 UI), 정답은 0.227 UI**.

## 2. Problem (English)

> **Build a BER bathtub from a BERT phase sweep and extrapolate the 1e-12 eye opening on the Q-scale.**
>
> Units are UI (10 Gb/s → 1 UI = 100 ps). Python 3 standard library only.
>
> 1. PRBS7 (`x^7 + x^6 + 1`, `b[t] = b[t-7] ^ b[t-6]`) checker: `prbs7_align(rx, nverify=64)` returns the offset `a` such that
>    `rx[t] == PRBS7[(t+a) % 127]` over an error-free 64-bit window (slide the window if needed, `None` if never);
>    `prbs7_errors(rx, a)` counts mismatches against the local reference.
> 2. `bathtub_counts(jitters, P)`: given the TIE of every data transition (UI, positive = late), return the error count at sampling
>    phases `k/P`, `k = 1..P-1`: `#(j > x) + #(j < x-1)`, in O(len(jitters) + P). It must equal, exactly, the error count a
>    bit-level simulation + PRBS checker produces with the same jitter draws.
> 3. `tail_prob(model, x) = P(J > x)` for dual-Dirac + Gaussian, sinusoidal PJ + Gaussian, and a rare-event mixture.
> 4. `qscale_fit(points, side, rho, ber_max, min_err)`: weighted least squares of `Q = sqrt(2)*erfcinv(2*BER/rho)` versus phase on one
>    side of the eye using only tail points (`BER <= ber_max`, `errors >= min_err`), weights `n_err * (phi(Q)/Qbar(Q))^2`.
>    `extrapolate(fitL, fitR, ber, rho)` -> eye opening at `ber`, its standard error, RJ, DJ(δδ), TJ.
> 5. Given a BERT export for two lanes (sweep to 1e-6 depth + long dwell at mask phases), decide PASS / INCONCLUSIVE / FAIL against
>    `EO(1e-12) >= 0.40 UI`, flagging errors that the fitted tails say should not exist.
>
> Example: dual-Dirac DJ = 0.20 UI, RJ = 0.020 UI, PRBS7, 1e7 bits per phase → fit gives RJ ≈ 0.020, DJ(δδ) ≈ 0.20,
> EO(1e-12) ≈ 0.526 UI (analytic truth 0.5264). A lane with a 1e-6-probability ±0.20 UI jitter event still extrapolates to
> ≈ 0.53 UI, while its true EO(1e-12) is 0.227 UI.

## 3. 시작 코드

<!-- code: starter.py -->

Part C 데이터: 스펙(채점용 주입 정답 포함)과 BERT export 생성기 (결정적 — 다시 돌려도 CSV가 바이트 단위로 같다, ~23 s):

<!-- code: spec.json -->

<!-- code: gen_bert.py -->

```bash
cd lessons/HWLAB/HWLAB-M11
python3 -I starter.py ; echo "exit=$?"      # TODO 상태: 2/24 passed, exit=1
```

실제 출력 (일부):

```text
-- Part A: PRBS7 checker / bit-level == edge count / analytic bathtub --
PASS  PRBS7 period 127, 64 ones, 64 transitions per period
FAIL  prbs7_align finds the offset (rx started 31 bits late -> a=31): a=None
FAIL  align skips an errored window; locked checker counts 4 injected errors exactly once: a=None errors=0
      bit-level 16384 bits, 8256 edges (rho_T=0.5039), lock a=None
      ...
FAIL  checker errors == edge-jitter count at every phase (exact): sum=0
FAIL  tail_prob(dd) symmetric pieces: P(J>DJ/2) = 1/4 + tiny: NotImplementedError: tail_prob
...
FAIL  L3 -> FAIL from mask long-run errors (BER at mask >> 1e-12): KeyError: 'long_err'
2/24 passed
exit=1
```

> 눈여겨볼 것: 시뮬레이션 레코드 자체(8 256 전이 / 16 384 비트 = 0.5039)는 starter에서도 나온다. PRBS7의 전이 밀도 64/127 = 0.5039가 BER 식의 ρT다. "PRBS31이면 ρT ≈ 0.5, 8b/10b 코드라면 다르다" — 패턴이 바뀌면 같은 지터에서도 BER이 바뀐다.

## 4. 힌트

<details>
<summary>힌트 1 — 정렬(lock)은 127가지 위치만 보면 된다</summary>

PRBS7은 주기 127이다. `PRBS7_PERIOD[(t + a) % 127]` 의 a를 0..126 중에서 찾으면 된다. 창 64비트가 전부 맞으면 오탐 확률은 2⁻⁶⁴ 수준. 창 안에 오류가 하나라도 있으면 어떤 a도 안 맞으니 창을 옮긴다. 실제 하드웨어 checker(SIV-M06)는 받은 비트로 LFSR 상태를 채우고 32번 연속 예측이 맞으면 LOCKED로 넘어간다 — 같은 일을 스트리밍으로 하는 것.

</details>

<details>
<summary>힌트 2 — "오류 = 에지가 샘플링 위상을 넘은 사건"</summary>

비트 k를 위상 x에서 읽는다. 왼쪽 경계(전이가 있을 때)의 지터 j_k가 x보다 크면 아직 이전 비트다 → 오류. 오른쪽 경계의 j_{k+1}이 x − 1보다 작으면 이미 다음 비트다 → 오류. 지터가 ±0.5 UI 안이면 두 사건이 한 비트에 겹치지 않는다. 그러니 checker 없이도 `#(j > x) + #(j < x − 1)` 이 정확한 오류 수다.

</details>

<details>
<summary>힌트 3 — 모든 위상을 한 번에: floor(j·P) 히스토그램</summary>

b = floor(j·P)로 칸에 넣으면 j &gt; k/P ⟺ b ≥ k, j &lt; k/P − 1 ⟺ b ≤ k − P − 1 (경계에 정확히 걸릴 확률 0). b는 −P..P 범위이니 인덱스 b + P. 뒤에서부터 누적합 `ge[i] = Σ hist[i:]` 을 만들면 late = ge[k+P], early = total − ge[k].

</details>

<details>
<summary>힌트 4 — Q-scale 변환과 가중치</summary>

`Q = −Φ⁻¹(BER/ρ)` 를 `tail(Q) = BER/ρ` 의 이분법으로. 가우시안 꼬리면 Q는 x에 대해 직선이다. 점마다 신뢰도가 다르다: 오류 n개의 상대오차는 1/√n이고, δQ = δBER / (ρ·φ(Q)) = Q̄(Q)/(φ(Q)·√n) ≈ 1/(Q·√n). 그래서 가중치 w = n·(φ(Q)/Q̄(Q))². 오류 0개 점은 Q = ∞ — 절대 넣지 않는다.

</details>

<details>
<summary>힌트 5 — 외삽 표준오차 (델타 방법)</summary>

가중 최소제곱의 공분산은 (XᵀWX)⁻¹: D = S·Sxx − Sx², Var m = S/D, Var c = Sxx/D, Cov = −Sx/D. x(Q_t) = (Q_t − c)/m 이므로 ∂x/∂c = −1/m, ∂x/∂m = −x/m → Var x = (x²·Var m + Var c + 2x·Cov)/m². 두 꼬리는 서로 다른 데이터라 EO의 분산은 합.

</details>

<details>
<summary>힌트 6 — 외톨이 오류</summary>

피팅 직선으로 그 위상의 기대 오류 = n·ρ·Q̄(m·x + c). 이게 0.01개보다 작은데 오류가 났다면, 그 오류는 피팅한 가우시안 꼬리에서 나올 수 없다 — 다른 메커니즘이 꼬리를 차지하고 있다는 신호다.

</details>

## 5. 풀이

### Part A — checker가 센 오류 = 에지가 넘은 횟수

실제 출력 (solution.py):

```text
      bit-level 16384 bits, 8256 edges (rho_T=0.5039), lock a=0
      phase : 0.0312 0.0625 0.0938 0.1250 0.1562 0.1875 0.5000 0.8125 0.8438 0.8750 0.9062 0.9375 0.9688
      checker:   4185   4054   2612    403      9      1      0      0     15    457   2568   3934   4069
      edges  :   4185   4054   2612    403      9      1      0      0     15    457   2568   3934   4069
PASS  checker errors == edge-jitter count at every phase (exact)   [sum=22307]
```

정렬은 eye 중앙(x = 0.5, 오류 0)에서 한 번 하고, 그 기준을 유지한 채 위상을 옮긴다(BERT가 실제로 하는 방식 — 가장자리 위상에서는 BER이 25 %라 거기서 sync를 잡을 수 없다). 그다음 13개 위상의 checker 결과가 에지 카운트와 **한 개도 다르지 않다**. 이 동치 덕분에 1e7비트 bathtub을 비트 단위로 돌리지 않고 에지 지터만 세서 만들 수 있다(5e6 에지 ≈ 1.5 s).

x = 1/32에서 BER = 0.2554: 왼쪽 에지의 "+DJ/2 Dirac"(전이의 절반)은 거의 전부 x보다 늦으므로 BER ≈ ρT·½ = 0.252, 나머지는 −DJ/2 쪽 꼬리. **벽 근처의 BER은 DJ 구조가, 깊은 꼬리는 RJ가 정한다** — 이 구분이 Part B의 핵심이다.

해석 꼬리 P(J &gt; x):

| 모델 | P(J &gt; x) | 비고 |
|---|---|---|
| dual-Dirac (dd) | ½·Q̄((x−DJ/2)/σ) + ½·Q̄((x+DJ/2)/σ) | Q̄(z) = erfc(z/√2)/2 |
| 정현 PJ (pj) | (1/M)·Σ Q̄((x − a·sin θᵢ)/σ), θᵢ = 2π(i+½)/M | 주기 함수의 중점 규칙은 지수적으로 수렴: M = 360·720·4000의 결과가 상대 ~1e-15 이내로 같음(직접 확인) |
| rare 혼합 | (1−p)·P0(x) + p/2·P0(x−s) + p/2·P0(x+s) | 드문 사건이 전체 분포를 ±s 옮긴다 |

검증: σ → 0이면 PJ 꼬리는 아크사인 CDF라 P(a·sin θ &gt; a/2) = 1/3 (실제 0.33333), 1e7비트 Monte Carlo는 오류 ≥ 100인 90개 위상에서 Poisson |z| 최대 2.23.

### Part B — Q-scale: 가우시안 꼬리를 직선으로 편다

dual-Dirac 한쪽 꼬리는 BER_L(x) = ρT·½·Q̄((x − μL)/σ). 그러므로 ρ = ρT/2로 정규화한 Q = Q̄⁻¹(BER/ρ) = (x − μL)/σ — **기울기 1/σ(RJ), Q = 0 절편이 Dirac 위치 μ**인 직선이다. 1e-12까지 연장하는 건 이 직선 위에서 Q_t까지 가는 것뿐이다.

```svg
<svg viewBox="0 0 760 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">Q-scale 왼쪽 꼬리: 측정점 → 직선 외삽 vs lane3 의 진짜 꼬리 (rare DJ)</text>
  <line x1="80" y1="300" x2="720" y2="300" stroke="currentColor"/>
  <line x1="80" y1="300" x2="80" y2="40" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="70" y="318">0.10</text><text x="237" y="318">0.19</text><text x="306" y="318">0.238</text>
    <text x="400" y="318">0.29</text><text x="570" y="318">0.386</text><text x="660" y="318">x [UI]</text>
    <text x="40" y="304">Q 0</text><text x="12" y="45">Q (ρT/2)</text>
  </g>
  <g stroke="currentColor" stroke-dasharray="3 4" opacity=".45">
    <line x1="80" y1="214" x2="720" y2="214"/><line x1="80" y1="155" x2="720" y2="155"/>
    <line x1="80" y1="112.5" x2="720" y2="112.5"/><line x1="80" y1="77.7" x2="720" y2="77.7"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="672" y="210">1e-3</text><text x="672" y="151">1e-6</text><text x="672" y="108">1e-9</text><text x="672" y="73">1e-12</text>
  </g>
  <rect x="179" y="155" width="56" height="59" class="f-ok" opacity=".18"/>
  <text x="140" y="236" fill="currentColor" font-size="12">피팅 창 (BER ≤ 1e-3, 오류 ≥ 5)</text>
  <line x1="80" y1="300" x2="324" y2="77.7" stroke="currentColor" stroke-width="2" stroke-dasharray="7 4"/>
  <g class="f-acc">
    <circle cx="110.5" cy="272" r="3"/><circle cx="124.3" cy="259.4" r="3"/><circle cx="138.1" cy="246.7" r="3"/>
    <circle cx="151.9" cy="234" r="3"/><circle cx="165.8" cy="221.4" r="3"/><circle cx="179.5" cy="208.7" r="4"/>
    <circle cx="193.5" cy="196" r="4"/><circle cx="207.3" cy="183" r="4"/><circle cx="221.2" cy="171.3" r="4"/>
    <circle cx="235" cy="158.9" r="4"/>
  </g>
  <path d="M80 300 L239.4 153.8 Q262 143 292.5 142.4 L416.5 139.1 Q445 136 469.6 129.4 L505 118 L540.5 102.7 L586.5 77.7" stroke="currentColor" stroke-width="2.5" fill="none"/>
  <circle cx="324" cy="77.7" r="5" class="f-ok"/>
  <circle cx="586.5" cy="77.7" r="5" class="f-warn"/>
  <text x="250" y="66" fill="currentColor" font-size="12">외삽 xL = 0.238 (lane3 fit EO 0.528)</text>
  <text x="470" y="66" fill="currentColor" font-size="12">진짜 xL = 0.386 (EO 0.227)</text>
  <text x="300" y="132" fill="currentColor" font-size="12">rare 지터 평탄부: BER ≈ ρT·p/4 ≈ 1.3e-7</text>
  <text x="300" y="172" fill="currentColor" font-size="12">1e7 비트로는 1~3개 '외톨이 오류'로만 보인다</text>
</svg>
```

#### Part B 결과 (실제 출력)

```text
      DD truth: EO(1e-12)=0.5264 UI | fit: EO=0.5258 +-0.0010 (1se), RJ=0.0201 DJdd=0.1995, pts L/R=10/10, depth=5.0e-07
PASS  fit recovers injected RJ 0.020 (+-10%) and Dirac positions 0.10 / 0.90 (+-0.005)   [RJ=0.0201 muL=0.0995 muR=0.8999]
PASS  EO(1e-12) extrapolated from 1e-6 depth within 3 se of analytic truth   [err=-0.0006 UI, 3se=0.0029]
      rho=1 (no rho_T, no Dirac 1/2): RJ=0.0221 DJdd=0.1699 EO=0.5195  vs rho_T/2: RJ=0.0201 DJdd=0.1995 EO=0.5258
      M10-style TIE (1e5 edges): RJ=0.0198 DJdd=0.2017 EO=0.5274 +-0.0055
      PJ truth EO(1e-12)=0.5380 | window 1e-3: EO=0.5320 DJdd=0.1672 RJ=0.0220 | window 0.1: EO=0.4815 DJdd=0.1455 RJ=0.0273
```

`demo_bathtub.py` §1 — 왼쪽 꼬리의 측정 Q와 피팅 직선 (`*` = 피팅에 쓴 점, 일부 행):

```text
  x[UI]   errors   BER meas   BER true  Q meas   Q fit used
 0.1484    19439   1.94e-03   1.95e-03   2.422   2.426 
 0.1562     6211   6.21e-04   6.19e-04   2.812   2.812 *
 0.1641     1736   1.74e-04   1.71e-04   3.199   3.199 *
 0.1719      397   3.97e-05   4.11e-05   3.603   3.586 *
 0.1797       95   9.50e-06   8.52e-06   3.959   3.973 *
 0.1875       18   1.80e-06   1.53e-06   4.339   4.360 *
 0.1953        3   3.00e-07   2.37e-07   4.718   4.747 
```

**① 정규화 ρ — 숫자 7.034는 어디서 왔나.** Q_t는 "1e-12"를 어떤 확률로 읽느냐에 따라 다르다. 같은 함수로 계산한 값:

| ρ | Q(1e-12) | 의미 |
|---|---|---|
| 1 | 7.0345 | 꼬리 확률 자체가 1e-12 (모든 비트가 전이, 한 꼬리가 전부 가우시안) — 스펙 관례 TJ = DJ + 2·7.034·RJ (FC-MJSQ의 14.069 = 2 × 7.034) |
| ρT = 0.504 | 6.938 | 전이 밀도만 반영 |
| ρT/2 = 0.252 | 6.8397 | 비트당 BER, dual-Dirac 한 Dirac의 가중치 ½까지 반영 (이 레슨의 기준) |

RJ = 0.02 UI면 2·(7.034 − 6.840)·0.02 = 0.0078 UI(0.78 ps @10G) 차이. 리포트에 **어느 관례인지** 적어야 하는 이유다. 더 흥미로운 것: ρ = 1로 피팅하면(Q 변환과 외삽 둘 다 ρ = 1) RJ 0.0221(+10 %), DJδδ 0.170(−15 %)로 **분해가 틀어지지만 EO는 0.0064 UI만 움직인다** — 깊은 꼬리에선 Q̄⁻¹(BER/ρ) ≈ √(Q² + 2 ln(1/ρ))라 ρ 오차가 기울기·절편에 나눠 흡수되고, 같은 ρ로 외삽하면 상당 부분 상쇄된다. 교훈: **RJ/DJ 숫자는 방법·관례 의존, TJ(BER)·EO(BER)가 물리량이다.** (ρ를 자유 변수로 맞추는 법은 연습 2.)

**② 측정 깊이와 불확도.** `demo_bathtub.py` §2·§3:

```text
   bits/pt      floor  pts L/R       EO      err       se
     1e+05    5.0e-05    5/6     0.5182  -0.0083   0.0110
     1e+06    5.0e-06    8/7     0.5207  -0.0057   0.0034
     1e+07    5.0e-07   10/10    0.5258  -0.0006   0.0010

EO per seed: 0.5261 0.5218 0.5315 0.5277 0.5213 0.5175 0.5301 0.5294
mean=0.5257 (truth 0.5264) std across seeds=0.0050  mean fit se=0.0034
```

깊이가 10배 깊어질 때마다 se가 약 3배 준다(외삽 거리가 짧아지고, 꼬리 점이 늘어난다). 시드 8개 반복의 실제 흩어짐(0.0050)이 피팅 se(0.0034)보다 1.5배 크다 — 정직하게 이유를 적으면: 이 시뮬레이션은 **지터 레코드 하나를 모든 위상에 재사용**해서 위상별 카운트가 누적 관계(서로 상관)인데, 최소제곱 se는 점들이 독립이라고 가정한다. 실제 BERT는 위상마다 따로 측정하므로 점들이 독립에 더 가깝다. 그래서 판정에는 **EO − 2·se** 처럼 여유를 둔다.

**③ TIE 기반 dual-Dirac(HWLAB-M10 방식)과의 비교.** 스코프 TIE 1e5 에지의 꼬리는 ρT = 1인 bathtub과 같은 모양이라 같은 코드로 피팅된다. 결과 EO 0.5274 ± 0.0055 — 맞지만 **se가 BERT 외삽의 5.5배**다. 1e5 에지의 CDF는 ~5e-5까지만 내려가서 외삽 거리가 길다. 실무 차이도 있다: 스코프 TIE에는 스코프 자체의 잡음·타임베이스 지터가 섞이고, 기준 클럭(golden PLL, HWLAB-M09)의 정의에 따라 저주파 지터가 빠지거나 남는다. BERT는 실제 수신기(CDR)가 따라가지 못한 지터만 오류로 본다(jitter tolerance는 HWLAB-A01).

**④ 정현 PJ — dual-Dirac이 아닌 DJ.** PJ 0.20 UIpp의 진짜 분포는 아크사인(양 끝에 몰림) ⊛ 가우시안이다. 꼬리 창(BER ≤ 1e-3)만 쓰면 DJδδ 0.167(&lt; 0.20 pp), RJ 0.022(&gt; 0.020)로 "효과적인" dual-Dirac이 나오고 EO 오차는 −0.006 UI(보수적). 창을 BER 0.1까지 넓혀 DJ 영역을 넣으면 −0.057 UI — 10배 나빠진다. **DJδδ ≤ DJpp는 dual-Dirac의 정의상 성질**이다: 꼬리를 가장 잘 설명하는 두 Dirac 간격이지 실제 최대 편차가 아니다.

| 함정 | 무엇이 틀어지나 | 이 레슨의 수치 |
|---|---|---|
| ρ 관례 혼동 | RJ/DJ 분해 (EO는 덜) | RJ +10 %, DJδδ −15 %, EO −0.0064 UI |
| 피팅 창에 DJ 영역 포함 | 기울기·절편 모두 | PJ: EO 오차 −0.006 → −0.057 UI |
| 얕은 측정(1e-4) | se, 편향 | 1e5비트: se 0.011 UI |
| 오류 0개 점 포함 | Q = ∞ → 피팅 붕괴 | `min_err=5`로 제외 |
| 꼬리가 바뀌는 메커니즘 | **EO 자체** (낙관) | lane3: 0.528 vs 정답 0.227 |

### Part C — 외삽이 PASS라고 말할 때 FAIL을 잡는 법

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">BERT 측정 체인과 판정 흐름</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="35" width="120" height="44" rx="5"/><rect x="160" y="35" width="130" height="44" rx="5"/>
    <rect x="320" y="35" width="150" height="44" rx="5"/><rect x="500" y="35" width="120" height="44" rx="5"/>
    <rect x="640" y="35" width="110" height="44" rx="5"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="54">PG: PRBS7</text><text x="20" y="70">(U7 TX lane)</text>
    <text x="170" y="54">채널 + SFP+</text><text x="170" y="70">loopback</text>
    <text x="330" y="54">ED 샘플러: CDR +</text><text x="330" y="70">위상 오프셋 x</text>
    <text x="510" y="54">align → lock</text><text x="510" y="70">→ 오류 카운트</text>
    <text x="650" y="54">(x, bits,</text><text x="650" y="70">errors) CSV</text>
  </g>
  <g stroke="currentColor"><line x1="130" y1="57" x2="160" y2="57"/><line x1="290" y1="57" x2="320" y2="57"/>
    <line x1="470" y1="57" x2="500" y2="57"/><line x1="620" y1="57" x2="640" y2="57"/></g>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="120" width="150" height="50" rx="5"/><rect x="190" y="120" width="160" height="50" rx="5"/>
    <rect x="380" y="120" width="170" height="50" rx="5"/><rect x="580" y="120" width="170" height="50" rx="5"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="140">① Q-scale 피팅</text><text x="20" y="157">L/R, EO(1e-12) ± se</text>
    <text x="200" y="140">② 마스크 장시간</text><text x="200" y="157">오류 &gt; 0 ?</text>
    <text x="390" y="140">③ 외톨이 오류?</text><text x="390" y="157">fit 기대 &lt; 0.01 인데 오류</text>
    <text x="590" y="140">④ EO − 2·se ≥ 0.40?</text><text x="590" y="157">PASS / FAIL</text>
  </g>
  <g stroke="currentColor"><line x1="160" y1="145" x2="190" y2="145"/><line x1="350" y1="145" x2="380" y2="145"/>
    <line x1="550" y1="145" x2="580" y2="145"/><line x1="695" y1="79" x2="695" y2="100"/><line x1="85" y1="100" x2="695" y2="100"/>
    <line x1="85" y1="100" x2="85" y2="120"/></g>
  <rect x="190" y="190" width="160" height="40" rx="5" class="f-warn" opacity=".35"/>
  <text x="200" y="215" fill="currentColor" font-size="12">예 → FAIL (L3)</text>
  <rect x="380" y="190" width="170" height="40" rx="5" class="f-mut" opacity=".5"/>
  <text x="390" y="215" fill="currentColor" font-size="12">예 → INCONCLUSIVE</text>
  <rect x="580" y="190" width="170" height="40" rx="5" class="f-ok" opacity=".35"/>
  <text x="590" y="215" fill="currentColor" font-size="12">예 → PASS (L0)</text>
  <g stroke="currentColor"><line x1="270" y1="170" x2="270" y2="190"/><line x1="465" y1="170" x2="465" y2="190"/><line x1="665" y1="170" x2="665" y2="190"/></g>
</svg>
```

실제 출력 (solution.py, Part C):

```text
      PASS lane=L0(U7.TX0->J3) EO_fit(1e-12)=0.574UI±0.001 (57.4ps) RJ=0.0180 DJdd=0.180 | EO-2se=0.572 >= 0.40UI, no outliers, mask long-run 0 err / 5.0e+07 bits
      FAIL lane=L3(U7.TX3->J6) EO_fit(1e-12)=0.528UI±0.001 (52.8ps) RJ=0.0198 DJdd=0.201 | mask long-run 6 err / 5.0e+07 bits (BER=1.2e-07 >> 1e-12) ; 55 outlier phase(s) inside fitted eye
      analytic truth EO(1e-12): L0=0.5738 L3=0.2274 (spec >= 0.40)
```

lane3를 `demo_bathtub.py` §5로 들여다보면 무슨 일인지 보인다 (일부 행):

```text
  x[UI]   errors   fit expect  true expect
 0.1719      389     3.87e+02     4.12e+02
 0.1875       11     1.34e+01     1.66e+01
 0.2031        3     2.54e-01     1.58e+00
 0.2188        3     2.61e-03     1.26e+00
 0.2500        3     4.33e-08     1.25e+00
 0.2812        2     6.00e-14     1.04e+00
 0.3125        1     6.86e-21     3.35e-01
 0.3281        0     9.08e-25     1.01e-01
long x=0.30: 3 err / 2.5e+07 bits, truth expects 1.57
long x=0.70: 3 err / 2.5e+07 bits, truth expects 1.57
L3 EO(1e-12): fit 0.5278 +-0.0010, truth 0.2274 (xL=0.3863 xR=0.6137)
```

- 피팅 창(1e-3 ~ 5e-7)은 주 지터(DD 0.20 / 0.02)만 보고 있다. 드문 지터의 평탄부 BER ≈ ρT·p/4 ≈ 1.3e-7은 피팅에 쓴 최하점(오류 ≥ 5 → 5e-7)보다 아래다. 그래서 **피팅 결과(RJ 0.0198, DJδδ 0.201)는 완벽하게 정상**이고 EO 0.528 → 스펙 0.40을 넉넉히 PASS.
- 하지만 1e-12에서는 확률 1e-6짜리 메커니즘이 꼬리를 지배한다. 정답 xL = 0.386 → EO 0.227 → FAIL. 외삽은 **꼬리 메커니즘이 측정 깊이 아래에서 바뀌지 않는다**고 가정하는데, 그 가정이 깨진 것.
- 단서 1 — 외톨이 오류: x = 0.22~0.31에서 피팅은 오류 2.6e-3 ~ 7e-21개를 예상하는데 1~3개가 났다(55개 위상). 하나의 레코드를 모든 위상에 재사용했기 때문에 몇 개의 드문 에지가 여러 위상에 계단처럼 나타난다. 실제 BERT에서는 위상마다 흩어진 1~2개 오류로 보인다 — **"eye 한가운데의 오류 1개"를 잡음으로 무시하면 안 되는 이유**.
- 단서 2 — 마스크 위상 장시간 측정: 0.30/0.70에서 각 3개(정답 기대값 1.57개). 오류가 하나라도 나면 그 위상의 BER은 1e-12보다 훨씬 크다는 게 통계적으로 확정된다(6 err / 5e7 비트의 95 % 구간 [4.4e-8, 2.6e-7], 연습 3).
- lane0의 "0 err / 5e7 비트"는 반대로 **아무것도 증명하지 않는다**: 95 % 상한이 ~7e-8이다. lane0의 PASS 근거는 외삽 + 외톨이 없음이고, 출하 판정이라면 마스크 위상에서 3e12비트(10 Gb/s에서 5분)를 추가로 돌려야 한다 — 실제 BERT에선 싸다.

**측정 시간·신뢰도 trade-off** (`python3 -I solution.py --table`, 실제 출력):

```text
method / depth (95% CL, 0 err)        bits/pt        t@10G        t@25G
Q-scale sweep, 1e-6 depth (~10 err)   1.00e+07        1.0ms      400.0us
direct, BER<1e-9 claim               3.00e+09      299.6ms      119.8ms
direct, BER<1e-12 claim              3.00e+12       299.6s       119.8s
direct, BER<1e-15 claim              3.00e+15         3.5d        33.3h
  full bathtub 64 pts @1e-6          6.40e+08       64.0ms       25.6ms
  full bathtub 64 pts @1e-12         1.92e+14         5.3h         2.1h
```

| 방법 | 시간 (10G) | 무엇을 말할 수 있나 | 못 하는 것 |
|---|---|---|---|
| Q-scale 외삽 (1e-6 깊이, 64위상) | ~64 ms | EO(1e-12) ± se, RJ/DJ 분해 | 측정 깊이 아래의 다른 메커니즘 |
| TIE dual-Dirac (스코프 1e5 에지) | 레코드 1개 | 같은 것, se 5배 | 수신기 CDR이 거르는 지터 구분, 깊은 꼬리 |
| 마스크 위상 직접 (1e-12, 95 %) | 300 s/점 | "이 위상에서 BER &lt; 1e-12" 직접 증거 | eye 전체 모양 |
| 전체 bathtub 직접 (1e-12) | 5.3 h | 모델 없이 전부 | 시간 (lane × 코너 × 보드 수를 곱하면 불가능) |

실무 결론: **sweep + 외삽으로 빠르게 전 lane을 스크리닝 → 외톨이 오류/마진 부족 lane과 대표 lane만 마스크 위상 장시간 측정**. 리포트 한 줄:

`FAIL VALBRD-B U7.TX3->J6 EO_fit(1e-12)=0.528UI but 6 err/5e7b @mask 0.30/0.70 (BER≈1.2e-7) + 55 outlier phases → low-probability DJ (~1e-6 of edges, ±0.20UI); true EO≈0.23UI < 0.40 — probe U7 PLL supply for bursts, compare TX0 (PASS 0.574UI)`

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"     # 24/24 passed, exit=0 (7.0 s)
python3 -I solution.py --table             # 측정 시간·신뢰도 표
python3 -I -W error -X dev solution.py      # 경고 없음 (exit 0)
python3 -I gen_bert.py                      # bert_sweep.csv 재생성 → shasum 동일 (결정적, ~23 s)
python3 -I demo_bathtub.py                  # §5의 표들 (~15 s)
```

<!-- code: demo_bathtub.py -->

`demo_bathtub.py` §4 — "긴 직접 측정"을 런타임이 허락하는 깊이(4e7비트 레코드 하나, ~1e-7)까지 해석식과 비교한 실제 출력 (일부 행):

```text
  x[UI]   errors   BER meas   BER true  expected
 0.1719     1610   4.03e-05   4.11e-05   1642.60
 0.1797      314   7.85e-06   8.52e-06    340.95
 0.1875       57   1.42e-06   1.53e-06     61.19
 0.1953        6   1.50e-07   2.37e-07      9.49
 0.2031        1   2.50e-08   3.17e-08      1.27
 0.2070        1   2.50e-08   1.10e-08      0.44
```

이 레코드는 모든 위상이 같은 지터를 공유해서 카운트가 누적 관계다 — 꼬리 쪽에서 한 번 적게 나온 요동이 아래 행들에 같이 묻어 있어(1610/1643, 314/341, 57/61) 행마다 독립인 오차처럼 읽으면 안 된다. 1e-7 아래는 오류 1~6개라 해석식과 "모순 없음" 이상을 말할 수 없다 — 이게 직접 측정의 한계이고, 1e-12의 기준값은 해석 정답을 썼다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| bathtub 전체(BER 0.25부터)를 직선 피팅 | 벽 근처는 DJ 구조(Dirac/아크사인)라 가우시안이 아님 → 기울기·절편 편향 | `ber_max=1e-3` 꼬리 창만 |
| 오류 0개 위상을 BER = 0으로 피팅에 넣음 | Q = ∞, 또는 임의로 1e-15를 넣어 직선을 끌어내림 | `min_err=5` 미만 제외, 0개는 상한으로만 사용 |
| 모든 점 같은 가중치 | 오류 5개 점과 5만 개 점의 신뢰도가 100배 다름 | w = n·(φ(Q)/Q̄(Q))² |
| Q(1e-12) = 7.034를 관례 확인 없이 사용 | ρ = 1 관례. 비트 BER·전이 밀도·Dirac ½을 넣으면 6.84 | `q_of_ber(ber, RHO_DD)` 로 계산하고 관례를 리포트에 명시 |
| DJδδ를 DJpp로 보고 | dual-Dirac은 꼬리 피팅 모델, DJδδ ≤ DJpp | "DJ(δδ)"로 표기, pp는 따로 측정 |
| 자기동기 checker 카운트를 그대로 BER로 | x⁷+x⁶+1은 오류 1개를 3번 셈 (패턴 의존) | 잠금(locked) checker, 또는 증식 계수로 보정 (연습 1) |
| 가장자리 위상에서 checker sync | BER 25 %에서 sync 실패/오정렬 | eye 중앙에서 lock 후 유지 |
| eye 안쪽 오류 1개를 "잡음"으로 무시 | 꼬리를 바꾸는 저확률 메커니즘의 유일한 흔적일 수 있음 | `find_outliers` → 마스크 위상 장시간 측정 |
| 0 오류 / 1e8 비트로 "1e-12 PASS" | 95 % 상한은 3e-8 | N ≥ 3/BER (0 오류), 4.74/BER (1 오류 허용) |
| 외삽 EO만 보고 판정 | se·관례·모델 가정이 숨음 | EO − 2·se, 방법·깊이·ρ 관례를 같이 기록 |

## 8. 면접 Q&A

<details>
<summary>Q1. bathtub 곡선이 뭐고, 왜 BER이 전이 밀도에 비례하나요?</summary>

샘플링 위상을 UI 안에서 옮기며 잰 BER(x). 왼쪽 벽은 비트 시작 에지가 x보다 늦게 오는 확률, 오른쪽 벽은 다음 에지가 x보다 일찍 오는 확률이다. 에지가 없으면(같은 비트 연속) 늦을 에지도 없으니 오류가 없다 → BER = ρT·[P(J&gt;x) + P(J&gt;1−x)]. 가운데의 평평한 바닥 폭이 eye opening, 1 − EO가 그 BER에서의 TJ다.
꼬리질문: "PRBS7과 PRBS31의 bathtub이 같은 지터에서 다른 이유?" → ρT는 둘 다 ≈ 0.5지만, 긴 run length(31은 최대 31개 연속)가 채널 ISI → 패턴 의존 지터(DDJ)를 키운다. 같은 칩·채널이라도 PRBS31 eye가 더 닫히는 게 정상이고, 스펙이 요구하는 패턴으로 재야 한다.

</details>

<details>
<summary>Q2. Q-scale은 무엇이고 왜 직선이 되나요? 기울기와 절편은 무슨 뜻이죠?</summary>

BER을 Q = √2·erfc⁻¹(2·BER/ρ)로 바꾼 축. 꼬리가 가우시안 ρ·Q̄((x−μ)/σ)이면 Q = (x − μ)/σ — 직선이다. 기울기의 역수가 RJ(σ), Q = 0 절편이 그 꼬리를 만드는 Dirac 위치(μ). 양쪽 μ 간격으로 DJδδ = 1 − (μR − μL). 1e-12까지 외삽 = 직선을 Q_t까지 연장.
꼬리질문: "어떤 점을 피팅에 쓰나?" → 가우시안이 지배하는 꼬리(대략 BER ≤ 1e-3)이면서 오류가 충분한 점(≥ 5~10개). 벽 근처를 넣으면 DJ 모양이 섞여 PJ 예에서 오차가 −0.006 → −0.057 UI로 10배 커졌다.

</details>

<details>
<summary>Q3. 1e-12를 직접 측정하려면 얼마나 걸리나요?</summary>

오류 0개로 "BER &lt; 1e-12"를 95 % 신뢰로 말하려면 N = −ln(0.05)/1e-12 = 3.0e12 비트 → 10 Gb/s 300 s, 25 Gb/s 120 s. 오류 1개를 허용하면 4.74e12, 2개면 6.30e12. 1e-15면 10 Gb/s에서 3.5일. bathtub 64위상 전부를 1e-12까지 직접 재면 5.3 시간.
꼬리질문: "그럼 외삽은 왜 믿나?" → RJ가 가우시안이고 DJ가 유계(bounded)라는 모델 가정 + 측정 깊이까지의 데이터가 직선 위에 있다는 확인. 믿는 범위를 넓히려면 마스크 위상에서만 장시간 측정한다(점 1~2개 × 5분).

</details>

<details>
<summary>Q4. dual-Dirac의 DJδδ가 실제 DJ pk-pk보다 작은 이유는?</summary>

dual-Dirac은 "꼬리를 가장 잘 설명하는 두 Dirac + 가우시안"이다. 정현 PJ(아크사인)처럼 확률이 양 끝에 몰리지만 연속인 분포를 두 점으로 근사하면, 가우시안과 합성된 꼬리를 맞추는 Dirac 간격이 pp보다 안쪽으로 온다. 이 레슨: PJ 0.20 UIpp → DJδδ 0.167, 대신 RJ가 0.022로 부풀어 차이를 흡수한다.
꼬리질문: "그러면 TJ도 틀린가?" → TJ(BER)는 측정 깊이 근처에선 데이터가 고정하므로 상대적으로 강건하다(EO 오차 −0.006 UI). RJ/DJ 분해 숫자는 방법(스펙트럼, 꼬리 피팅, ρ 관례)에 따라 달라지므로 장비 간 비교는 TJ@BER로 한다.

</details>

<details>
<summary>Q5. Q-scale 외삽이 틀리는 대표적인 경우와, 그걸 어떻게 잡나요?</summary>

측정 깊이 아래에서 꼬리 메커니즘이 바뀌는 경우: 전원 버스트·부하 스텝이 PLL을 흔드는 드문 사건, 가끔만 정렬되는 크로스토크, CDR이 놓치는 저주파 wander, 비가우시안 잡음. 이 레슨 lane3(확률 1e-6, ±0.20 UI): 피팅 EO 0.528 vs 정답 0.227. 잡는 법: ① 피팅이 오류를 거의 예상하지 않는 위상의 외톨이 오류 검사, ② 마스크 위상 장시간 측정, ③ Q-scale 그래프에서 꺾임(knee) 확인, ④ 같은 lane 반복·다른 lane 비교(lane0 0.574 대비).
꼬리질문: "외톨이 오류가 없으면 안전한가?" → 아니다. 1e7비트에서 기대 개수가 ~1개 수준이면 0개일 확률이 크다(Poisson e⁻¹ ≈ 37 %). 없음은 증거가 아니다 — 마스크 위상 장시간 측정이 최종.

</details>

<details>
<summary>Q6. TJ = DJ + 14.069·RJ의 14.069는 어디서 왔나요? 이 레슨의 6.84와 왜 다르죠?</summary>

14.069 = 2 × 7.034, 7.034 = Q̄⁻¹(1e-12) — 꼬리 확률 자체가 1e-12인 점(ρ = 1 관례, Fibre Channel MJSQ 등). 비트당 BER로 읽으면 전이 밀도 ρT(≈ 0.5)와 dual-Dirac 한쪽 가중치 ½이 곱해져 Q̄⁻¹(1e-12/0.252) = 6.84. RJ 0.02 UI에서 TJ 차이 0.0078 UI.
꼬리질문: "리포트에는 무엇을 쓰나?" → 스펙이 정한 관례. 스펙이 TJ = DJ + 14.069·RJ로 정의하면 그걸로, 장비가 내부적으로 ρT를 넣는지 확인해서 "어느 Q로 계산했는지"를 적는다. 관례가 다른 두 숫자를 비교하면 1 ps 가까이 차이 날 수 있다.

</details>

<details>
<summary>Q7. 스코프 TIE로 낸 TJ와 BERT bathtub에서 낸 TJ가 다릅니다. 어느 쪽이 맞나요?</summary>

둘은 보는 것이 다르다. 스코프 TIE는 기준 클럭(이상 클럭 또는 golden PLL, HWLAB-M09) 대비 에지 시각이고, 스코프 자체 잡음·타임베이스 지터가 RJ에 더해진다. 표본도 1e5~1e6 에지라 꼬리가 얕다(이 레슨: se 0.0055 vs BERT 0.0010). BERT는 실제 수신 경로(CDR)가 따라가지 못한 지터만 오류로 센다. 저주파 지터는 CDR이 추적해 BERT에선 사라지고, 스코프에선 golden PLL 대역폭에 따라 남거나 빠진다.
꼬리질문: "스코프 쪽 RJ가 항상 더 크면?" → 스코프 노이즈 플로어(TIE floor)를 제곱합으로 빼는 보정(RJ_dut² ≈ RJ_meas² − RJ_scope²), 프로브·케이블 대역폭 확인. 그리고 스펙이 어느 측정 방법(어떤 CDR 대역폭)을 정의하는지 맞춘다.

</details>

<details>
<summary>Q8. PRBS checker가 오류를 과대 계수할 수 있나요?</summary>

자기동기(self-synchronizing) checker는 예측 비트를 받은 비트로 만든다: pred = rx[t−7] ⊕ rx[t−6]. 오류 1개는 자기 자리 + 6, 7비트 뒤 예측에서 다시 걸려 3번 세어진다(탭 2개 다항식). 연습 1: 4개 주입 → 12. 그런데 6비트 간격 두 오류는 서로 상쇄돼 6이 아니라 4로 세어진다 — 계수 배율이 패턴 의존이라 단순히 3으로 나누면 안 된다. 잠금(locked) checker는 로컬 LFSR로 기대 비트를 만들어 1번만 센다.
꼬리질문: "그럼 자기동기를 왜 쓰나?" → 시작·재동기가 자동이다. 실무 checker는 HUNT(자기동기로 lock) → LOCKED(로컬 생성) 두 모드로 동작하고, 오류율이 높으면 sync loss로 다시 HUNT (SIV-M06).

</details>

## 9. 추가 연습

### 연습 1 — 자기동기 checker의 오류 증식

`selfsync_errors(rx)`: 예측 = rx[t−7] ⊕ rx[t−6]. 서로 떨어진 단일 오류 4개를 넣은 2000비트 PRBS7에서 잠금 checker는 4, 자기동기는 12. 6비트 간격 오류 두 개는 몇으로 세어지나?

<details>
<summary>답 보기</summary>

실제 출력: `locked=4 selfsync=12`, 6비트 간격 두 오류는 `selfsync=4`. t = 500, 506에 오류가 있으면 t = 506의 예측 rx[499] ⊕ rx[500]이 틀리고(오류 500의 영향) 받은 rx[506]도 틀려서 두 오류가 상쇄 → 그 자리는 "맞음". 남는 건 500(자기), 507(500+7), 512(506+6), 513(506+7) 4개. 그래서 자기동기 카운트를 3으로 나누는 보정은 오류가 드물고 흩어져 있을 때만 근사로 맞는다. BER 측정에는 잠금 모드를 쓴다.

</details>

### 연습 2 — ρ를 자유 변수로 피팅

`fit_rho(points, side, bmax)`: log 격자 41점에서 가중 χ²가 최소인 ρ. ① DD, 창 BER ≤ 5e-2 → ρ가 ρT/2의 10 % 이내 ② DD, 창 ≤ 1e-3 → χ²가 ρT/2와 1에서 거의 같음(식별 불가) ③ PJ, 창 ≤ 5e-2 → ρ_eff ≠ ρT/2, 그리고 고정 ρ보다 EO 오차가 3배 이상 작다.

<details>
<summary>답 보기</summary>

실제 출력: DD 넓은 창 best ρ = 0.2597 (ρT/2 = 0.2520), 꼬리 창 χ² 7.23 vs 6.04 — 깊은 꼬리만으로는 Q̄⁻¹(BER/ρ)의 ρ 의존이 기울기·절편에 흡수돼 구별이 안 된다. ρ를 정하려면 꼬리가 시작되는 어깨(BER 1e-2 근처)가 필요하다. PJ: ρ_L = ρ_R = 0.1324 — 아크사인 분포의 "효과적인 Dirac 가중치"는 ½이 아니다. 같은 넓은 창에서 EO 오차가 고정 ρ −0.0335 → 맞춘 ρ −0.0066 UI. 넓은 창을 쓰려면 ρ도 같이 맞춰야 한다는 것이 Stephens식 Q-scale 방법의 요점이다.

</details>

### 연습 3 — Poisson 신뢰도

`bits_for_cl_k(ber, cl, k)`(오류 k개 허용 시 필요한 비트), `poisson_bounds(k, n)`(정확 양측 구간). 95 % 배수 0/1/2개 = 2.996 / 4.744 / 6.296을 재현하고, lane3 마스크(6 err / 5e7 비트)와 lane0(0 / 5e7)의 구간을 구한다.

<details>
<summary>답 보기</summary>

P(X ≤ k | λ) = 1 − CL을 이분법으로 → 2.996 / 4.744 / 6.296 (χ²(2k+2) 분위수의 절반과 같다). lane3: BER ∈ [4.40e-8, 2.61e-7] — 하한이 1e-12보다 4자릿수 이상 위라 FAIL이 통계적으로 확정. lane0: 상한 7.38e-8(양측 97.5 % 단측 = −ln 0.025 / n = 3.689/n) — "PASS"를 말하기엔 4~5자릿수 부족. 오류가 나는 건 짧은 측정으로도 증명되지만, 안 나는 건 오래 재야만 증명된다.

</details>

### 연습 4 — RJ 예산

`rj_budget(eo_min, djdd, ber)`: EO = 1 − DJδδ − 2·Q·RJ ≥ eo_min → RJ_max. EO ≥ 0.40 UI, DJδδ 0.18에서 RJ_max는 몇 UI, 10G/25G에서 몇 ps?

<details>
<summary>답 보기</summary>

RJ_max = (1 − 0.40 − 0.18)/(2 × 6.8397) = 0.0307 UI = 3.07 ps @10G, 1.23 ps @25G. 같은 UI 예산이라도 라인 레이트가 올라가면 ps 예산이 비례해서 줄어든다 — 25G 이상에서 refclk·PLL 위상잡음(HWLAB-M08)이 갑자기 중요해지는 이유. ρ = 1 관례(Q = 7.034)로 계산하면 0.0299 UI로 약 3 % 더 빡빡하다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"      # 9/9 passed, exit=0 (~7 s)
```

## 10. 핵심 정리

- bathtub: BER(x) = ρT·[P(J&gt;x) + P(J&gt;1−x)]. 오류는 에지가 샘플링 위상을 넘은 사건 — checker 카운트와 에지 카운트가 정확히 같다.
- Q-scale: Q = √2·erfc⁻¹(2·BER/ρ). 가우시안 꼬리면 직선, 기울기 1/RJ, 절편 = Dirac 위치. EO(BER) = 두 직선을 Q_t까지 연장한 간격, TJ = 1 − EO = DJδδ + 2·Q_t·RJ.
- Q(1e-12) = 7.034는 ρ = 1 관례(14.069 = 2×7.034). 비트 BER + ρT + Dirac ½ → 6.84. 관례를 리포트에 적는다.
- 피팅은 꼬리 창(BER ≤ 1e-3, 오류 ≥ 5)만, Poisson 역분산 가중, 오류 0 점 제외. se는 델타 방법, 판정은 EO − 2·se.
- RJ/DJ 분해는 방법 의존(ρ=1: DJδδ −15 %), EO/TJ@BER가 물리량. DJδδ ≤ DJpp (PJ 0.20pp → 0.167).
- 1e-12 직접: 0 오류 95 % → 3.0e12 비트 = 10G 300 s. 1e-6 깊이 sweep 64점은 64 ms.
- 외삽은 "꼬리 메커니즘이 깊이 아래에서 안 바뀐다"는 가정. 드문 큰 지터(lane3: 확률 1e-6)는 외삽 EO 0.528 → 정답 0.227.
- 잡는 법: eye 안쪽 외톨이 오류 검사 → 마스크 위상 장시간 측정. 오류 0은 아무것도 증명하지 않고, 오류 1개는 많은 것을 증명한다.

## 참고자료

- Ransom Stephens, *Jitter Analysis: The dual-Dirac Model, RJ/DJ, and Q-Scale*, Agilent Technologies white paper (2004) — Q-scale 정규화 ρ, dual-Dirac 꼬리 피팅, DJδδ vs DJpp
- INCITS TR-35-2004, *Fibre Channel — Methodologies for Jitter and Signal Quality Specification (MJSQ)* — TJ(1e-12) = DJ + 14.069·RJ 관례, bathtub·지터 측정 방법론
- Mike Peng Li, *Jitter, Noise, and Signal Integrity at High-Speed*, Prentice Hall (2007) — 지터 성분 분해, BER/CDF 관계, 측정 장비 비교
- Dennis Derickson, Marcus Müller (eds.), *Digital Communications Test and Measurement*, Prentice Hall (2007) — BERT 구조, bathtub, 신뢰수준과 측정 시간
- ITU-T Recommendation O.150 — PRBS 테스트 패턴(PRBS7/15/23/31 다항식) 정의
- AMD/Xilinx UG578 *UltraScale Architecture GTY Transceivers User Guide* — RX margin analysis(온다이 eye scan): 위상·전압 오프셋별 오류 카운트로 bathtub/2D eye를 만드는 실제 하드웨어 예
- 관련 레슨: SIV-M06(PRBS 생성·checker·BER 신뢰구간), HWLAB-M07(period/c2c/TIE), HWLAB-M08(위상잡음 → RJ), HWLAB-M09(eye·golden PLL), HWLAB-M10(RJ/DJ 분해·dual-Dirac), HWLAB-M12(ISI·equalizer), HWLAB-A01(CDR/JTOL), HWLAB-A02(lane margining 2D eye)
