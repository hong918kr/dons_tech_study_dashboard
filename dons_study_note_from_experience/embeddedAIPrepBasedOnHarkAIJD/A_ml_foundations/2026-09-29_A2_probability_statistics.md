# A2. ML을 위한 확률·통계 — 분포, softmax, cross-entropy

> **이 노트를 다 읽으면**: 센서 noise를 평균·분산·Gaussian으로 말할 수 있다 · logits → softmax → 확률 → cross-entropy loss를 손으로 계산하고 C로 안정하게 구현할 수 있다 · wake word 오작동률을 Bayes로 "시간당 몇 번"으로 환산할 수 있다 · temperature와 샘플링이 출력 분포를 어떻게 바꾸는지 설명할 수 있다
> **JD 연결**: "5 yrs ML engineering" 요건의 기초 체력 · "(우대) Audio/Voice … Lightweight LLM models" — study_prep_list **A2**: 확률분포, 평균·분산, 정규분포, softmax, log-likelihood, cross-entropy, 베이즈 기초, 조건부 확률
> **Don 기준 난이도**: 평균·분산·정규분포·조건부 확률은 Berkeley에서 배운 것(복습) / softmax 수치 안정성, cross-entropy와 likelihood의 관계, temperature, FAR per hour 환산은 새로 배울 부분
> **선행 노트**: A0, A1

---

## 0. 큰 그림 — 이게 왜 필요한가

펌웨어 엔지니어에게 "확률·통계"는 보통 **측정값의 흔들림**을 다루는 도구였다. 전류 측정을 100번 하고 평균과 표준편차를 보고, spec margin이 3σ 밖인지 따지는 일이다. ML에서는 같은 도구가 세 군데에 더 등장한다.

1. **입력 쪽**: 센서 데이터를 모델에 넣기 전에 평균을 빼고 표준편차로 나눈다(표준화). 이 숫자를 펌웨어에 똑같이 박아 넣지 않으면 모델이 조용히 틀린다.
2. **출력 쪽**: 모델의 마지막 레이어는 확률이 아니라 **logit**(아무 실수)을 낸다. softmax가 이것을 확률로 바꾼다. wake word 모델이 "0.93"이라고 말할 때 그것이 무슨 뜻인지, threshold를 어디에 둘지가 확률 문제다.
3. **학습 쪽**: 모델을 학습시키는 loss(cross-entropy, MSE)는 전부 "데이터가 이 확률분포에서 나왔을 가능성(likelihood)을 최대로" 하는 식에서 나온다.

```
            ┌─────────────── 학습 (PC / 서버) ────────────────┐
 센서 로그 ─► 통계 계산 (μ, σ) ─► 표준화 ─► 모델 ─► logits ─► softmax ─► cross-entropy loss
                  │                                                      ▲
                  │ 같은 μ, σ를 헤더로 export                              │ 정답 label
                  ▼
            ┌─────────────── 추론 (MCU / DSP) ─────────────────┐
 IMU/Mic ─► 표준화 (같은 μ, σ!) ─► 모델 ─► logits ─► softmax/threshold ─► 판단 ─► 기상/무시
                                                              │
                                        오작동률 = P(trigger | 조용함) × 윈도우 수/시간  (Bayes)
```

이 노트의 흐름은 위 그림 그대로다. 1–3절에서 확률변수와 분포(센서 noise), 4–5절에서 전처리 통계(표준화, 상관), 6–8절에서 출력과 학습(softmax, likelihood, cross-entropy, KL), 9절에서 판단(Bayes와 오작동률), 10절에서 생성(temperature 샘플링)을 다룬다.

---

## 1. 확률변수, 기댓값, 분산 — 센서 noise로 시작하기

### 1.1 직관: 책상 위에 놓인 가속도계

손목 기기를 책상 위에 가만히 놓고 가속도계 z축을 100 Hz로 읽는다고 하자. 이상적이라면 매번 9.81 m/s²(중력)가 나와야 하지만 실제 값은 9.816, 9.803, 9.842, … 처럼 조금씩 흔들린다. 이 흔들림이 **noise**다.

- 매번 읽을 때마다 값이 달라지는 양을 **확률변수(random variable)** 라고 부른다. 보통 대문자 `X`로 쓴다. 말로 하면 "읽을 때마다 결과가 달라지는 숫자 하나"다.
- 값들의 중심이 **기댓값(expectation, 평균)** `E[X] = μ`다. 여기서는 중력 9.81이다. 펌웨어 용어로는 **DC 성분**, 또는 offset이다.
- 중심에서 얼마나 흩어지는지가 **분산(variance)** `Var(X) = σ²`와 **표준편차(standard deviation)** `σ`다. 펌웨어 용어로는 **noise의 RMS**다(평균을 뺀 뒤의 RMS).

### 1.2 이산 vs 연속, PMF vs PDF

| 구분 | 이산(discrete) | 연속(continuous) |
|---|---|---|
| 값의 종류 | 셀 수 있는 값 (0/1, class 번호, ADC 코드) | 실수 구간 (가속도, 전압) |
| 분포 표현 | **PMF** `p(x) = P(X = x)` | **PDF** `p(x)` — 밀도 |
| 합 조건 | `∑ p(x) = 1` | `∫ p(x) dx = 1` |
| 확률 구하기 | 값 하나의 확률 = `p(x)` | 구간의 확률 = 그 구간의 면적 `∫ₐᵇ p(x) dx` |
| ML 예 | 분류 모델 출력 (idle/walk/run) | 회귀 오차, 센서 noise |

PDF 값 자체는 확률이 아니다. **밀도**(단위 길이당 확률)다. 그래서 1보다 클 수 있다. σ = 0.05인 Gaussian의 최고점 높이는 `1/(0.05·√(2π)) ≈ 7.98`이다. 말로 하면 "0.01 m/s² 폭의 좁은 구간에 떨어질 확률이 약 7.98 × 0.01 ≈ 8%"라는 뜻이다.

엄밀히는 ADC가 내놓는 값은 정수 코드이므로 이산이다. 하지만 코드 간격(LSB)이 noise보다 충분히 작으면 연속처럼 다뤄도 된다. 이 "간격 vs noise" 비교는 C1 양자화에서 다시 나온다.

### 1.3 정의

```
기댓값    E[X]   = ∑ x · p(x)            (이산)      = ∫ x · p(x) dx      (연속)
분산      Var(X) = E[(X − μ)²] = E[X²] − μ²
표준편차  σ      = √Var(X)
```

말로 하면: 기댓값은 "값 × 그 값이 나올 확률"을 다 더한 무게 중심이고, 분산은 "중심에서 떨어진 거리의 제곱"의 평균이다. 제곱을 하므로 단위가 (m/s²)²가 되고, 그래서 원래 단위로 돌아오려고 √를 씌운 것이 표준편차다.

### 1.4 손으로 계산: 샘플 4개

z축을 네 번 읽어 `[9.80, 9.85, 9.75, 9.84]`를 얻었다고 하자.

```
평균   μ̂ = (9.80 + 9.85 + 9.75 + 9.84) / 4 = 39.24 / 4 = 9.81
편차         −0.01, +0.04, −0.06, +0.03
편차²        0.0001, 0.0016, 0.0036, 0.0009   → 합 0.0062
분산   σ̂² = 0.0062 / 4 = 0.00155        (n으로 나눔, ddof=0)
       s²  = 0.0062 / 3 ≈ 0.00207        (n−1로 나눔, ddof=1, "표본 분산")
표준편차 σ̂ = √0.00155 ≈ 0.0394 m/s²
```

`n`으로 나누느냐 `n−1`로 나누느냐는 샘플이 적을 때만 차이가 난다. `n−1`(Bessel 보정)은 "평균 자체도 샘플로 추정했으니 자유도가 하나 줄었다"는 보정이다. numpy의 `np.std`는 기본이 `ddof=0`, pandas의 `.std()`는 기본이 `ddof=1`이라서 둘을 섞어 쓰면 숫자가 미세하게 달라진다.

### 1.5 코드로 확인: 1000개 샘플

정지 상태 가속도계를 `N(9.81, 0.05²)`로 시뮬레이션하고 평균·분산·표준편차를 추정한다.

```python
import numpy as np

rng = np.random.default_rng(0)
g = 9.81                      # 정지 상태에서 z축이 보는 중력 [m/s²]
noise_std = 0.05              # 센서 noise density로부터 가정한 값
z = g + rng.normal(0.0, noise_std, size=1000)   # 1000개 샘플 (예: 100 Hz × 10초)

print("first 5 :", np.round(z[:5], 4))
print("mean    :", round(z.mean(), 5))
print("var     :", round(z.var(), 6))            # ddof=0 (모집단 식)
print("std     :", round(z.std(), 5))
print("std(ddof=1):", round(z.std(ddof=1), 5))   # 표본 분산 (n-1)
# 손계산과 같은 방식: E[X] = 합/개수, Var = E[(X-μ)²]
mu = z.sum() / len(z)
var = ((z - mu) ** 2).sum() / len(z)
print("manual  :", round(mu, 5), round(var, 6), round(np.sqrt(var), 5))
```

```text
first 5 : [9.8163 9.8034 9.842  9.8152 9.7832]
mean    : 9.8076
var     : 0.002385
std     : 0.04884
std(ddof=1): 0.04886
manual  : 9.8076 0.002385 0.04884
```

출력에서 볼 것: 추정 평균 9.8076은 참값 9.81과 0.0024 차이다. 평균의 흔들림(표준오차)은 `σ/√n = 0.05/√1000 ≈ 0.0016`이므로 약 1.5 표준오차 정도로 정상 범위다. `ddof=0`과 `ddof=1`은 1000개에서는 거의 같다.

### 1.6 임베디드 연결

- 데이터시트의 **noise density**(예: µg/√Hz)는 σ를 주파수 대역당으로 적은 것이다. 대략 `σ ≈ density × √(대역폭)` 로 환산한다(필터 모양에 따른 보정 계수는 무시한 근사). 이 노트의 σ = 0.05 m/s²(약 5 mg)는 설명용으로 정한 값이다.
- 정지 상태 로그의 평균이 9.81에서 벗어나 있으면 그것은 noise가 아니라 **offset(bias)** 이다. 보정(calibration)으로 빼야 하는 값이다. 분산은 offset과 무관하다.
- 긴 스트림에서 평균·분산을 실시간으로 구해야 한다면 합과 제곱합을 누적하는 방식보다 **Welford 알고리즘**이 float 정밀도 손실이 적다. `E[X²] − μ²`는 큰 두 수의 뺄셈이라(9.81² 근처 두 값) 상쇄 오차(catastrophic cancellation)가 난다.

---

## 2. 자주 만나는 분포 네 가지

### 2.1 Bernoulli — 동전 하나, 착용 여부 하나

값이 0 또는 1뿐인 분포다. `P(X=1) = p`, `P(X=0) = 1−p`.

기댓값 `E[X] = p`, 분산 `Var(X) = E[X²] − p² = p(1 − p)`. 말로 하면: 평균은 "1이 나올 확률" 그 자체이고, 분산은 p = 0.5일 때 최대(0.25), p가 0이나 1에 가까울수록 작아진다. 착용 감지(worn / not worn), wake word 있음/없음 같은 **이진 분류 모델의 출력**은 Bernoulli의 p를 예측하는 것이다.

### 2.2 Categorical — 주사위, 제스처 class

K개 값 중 하나가 나오는 분포다. 확률 벡터 `p = [p₁, …, p_K]`, `∑ pₖ = 1`. 제스처 분류(idle / walk / run), LLM의 다음 token 예측이 전부 categorical이다. 정답 class를 **one-hot** 벡터로 쓰면 `[1, 0, 0]`처럼 정답 자리만 1인 categorical 분포가 된다. 이 관점이 7절 cross-entropy의 핵심이다.

### 2.3 Uniform — 양자화 오차

구간 `[a, b]`에서 모든 값의 밀도가 같다. `p(x) = 1/(b−a)`.

```
E[X] = (a + b) / 2
Var(X) = (b − a)² / 12
```

임베디드와 바로 연결되는 예: step 크기 Δ로 반올림 양자화를 하면 오차는 대략 `[−Δ/2, +Δ/2]`의 uniform이다. 그래서 **양자화 noise의 분산 = Δ²/12**, 표준편차 = `Δ/√12 ≈ 0.289·Δ`. ADC SNR 공식 6.02·N + 1.76 dB도 이 식에서 나온다. C1(양자화)에서 SQNR을 계산할 때 그대로 다시 쓴다.

### 2.4 Gaussian (정규분포) — 68-95-99.7

```
p(x) = 1 / (σ√(2π)) · exp( −(x − μ)² / (2σ²) )
```

말로 하면: μ에서 가장 높고, μ에서 멀어질수록 거리의 **제곱**에 비례해 지수적으로 떨어지는 종 모양이다. 모양은 μ(위치)와 σ(폭) 두 숫자로 완전히 결정된다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 300">
<polygon points="175.7,250 175.7,223.0 188.6,212.8 201.4,200.3 214.3,185.2 227.1,168.0 240.0,149.0 252.9,129.0 265.7,109.0 278.6,90.3 291.4,74.0 304.3,61.3 317.1,53.3 330.0,50.5 342.9,53.3 355.7,61.3 368.6,74.0 381.4,90.3 394.3,109.0 407.1,129.0 420.0,149.0 432.9,168.0 445.7,185.2 458.6,200.3 471.4,212.8 484.3,223.0 484.3,250" fill="#4a7bd0" fill-opacity="0.18" stroke="none"/> <polygon points="252.9,250 252.9,129.0 259.3,119.0 265.7,109.0 272.1,99.4 278.6,90.3 285.0,81.7 291.4,74.0 297.9,67.1 304.3,61.3 310.7,56.7 317.1,53.3 323.6,51.2 330.0,50.5 336.4,51.2 342.9,53.3 349.3,56.7 355.7,61.3 362.1,67.1 368.6,74.0 375.0,81.7 381.4,90.3 387.9,99.4 394.3,109.0 400.7,119.0 407.1,129.0 407.1,250" fill="#4a7bd0" fill-opacity="0.35" stroke="none"/> <polyline points="60.0,249.6 69.6,249.3 79.3,249.0 88.9,248.5 98.6,247.8 108.2,246.8 117.9,245.5 127.5,243.6 137.1,241.2 146.8,238.1 156.4,234.1 166.1,229.1 175.7,223.0 185.4,215.6 195.0,206.9 204.6,196.7 214.3,185.2 223.9,172.5 233.6,158.7 243.2,144.1 252.9,129.0 262.5,114.0 272.1,99.4 281.8,85.9 291.4,74.0 301.1,64.1 310.7,56.7 320.4,52.1 330.0,50.5 339.6,52.1 349.3,56.7 358.9,64.1 368.6,74.0 378.2,85.9 387.9,99.4 397.5,114.0 407.1,129.0 416.8,144.1 426.4,158.7 436.1,172.5 445.7,185.2 455.4,196.7 465.0,206.9 474.6,215.6 484.3,223.0 493.9,229.1 503.6,234.1 513.2,238.1 522.9,241.2 532.5,243.6 542.1,245.5 551.8,246.8 561.4,247.8 571.1,248.5 580.7,249.0 590.4,249.3 600.0,249.6" fill="none" stroke="#4a7bd0" stroke-width="2.5"/> <line x1="60" y1="250" x2="600" y2="250" stroke="currentColor"/> <line x1="98.6" y1="250" x2="98.6" y2="255" stroke="currentColor"/> <text x="98.6" y="270" font-size="12" text-anchor="middle">μ−3σ</text>
<line x1="175.7" y1="250" x2="175.7" y2="255" stroke="currentColor"/> <text x="175.7" y="270" font-size="12" text-anchor="middle">μ−2σ</text> <line x1="252.9" y1="250" x2="252.9" y2="255" stroke="currentColor"/> <text x="252.9" y="270" font-size="12" text-anchor="middle">μ−1σ</text> <line x1="330.0" y1="250" x2="330.0" y2="255" stroke="currentColor"/> <text x="330.0" y="270" font-size="12" text-anchor="middle">μ</text>
<line x1="407.1" y1="250" x2="407.1" y2="255" stroke="currentColor"/> <text x="407.1" y="270" font-size="12" text-anchor="middle">μ+1σ</text> <line x1="484.3" y1="250" x2="484.3" y2="255" stroke="currentColor"/> <text x="484.3" y="270" font-size="12" text-anchor="middle">μ+2σ</text> <line x1="561.4" y1="250" x2="561.4" y2="255" stroke="currentColor"/> <text x="561.4" y="270" font-size="12" text-anchor="middle">μ+3σ</text>
<line x1="330.0" y1="250" x2="330.0" y2="50.5" stroke="currentColor" stroke-dasharray="4 3"/> <text x="330.0" y="150.0" font-size="14" text-anchor="middle">68.3%</text> <text x="214.3" y="225.0" font-size="12" text-anchor="middle">13.6%</text> <text x="445.7" y="225.0" font-size="12" text-anchor="middle">13.6%</text> <text x="121.7" y="205.0" font-size="12" text-anchor="middle">2.1%</text> <text x="538.3" y="205.0" font-size="12" text-anchor="middle">2.1%</text>
<text x="60" y="28" font-size="13">N(μ, σ²): 진한 영역 ±1σ (68.3%), 연한 영역까지 ±2σ (95.4%)</text> <text x="60" y="290" font-size="12">±3σ 밖은 0.3%뿐 — σ=0.05면 9.81±0.15 밖은 드물다</text>
</svg>
```

그림 1 — 표준 정규분포 곡선(57개 점을 실제 계산해서 그림). 진한 파랑이 μ±1σ(68.3%), 연한 파랑까지 포함하면 μ±2σ(95.4%), μ±3σ는 99.7%다.

| 구간 | 들어갈 확률 | 밖에 있을 확률 | 정지 가속도계 (σ = 0.05) |
|---|---|---|---|
| μ ± 1σ | 68.3% | 31.7% | 9.76 ~ 9.86 |
| μ ± 2σ | 95.4% | 4.6% | 9.71 ~ 9.91 |
| μ ± 3σ | 99.7% | 0.27% | 9.66 ~ 9.96 |

펌웨어 쪽 감각으로 번역하면: 100 Hz로 샘플링하면 3σ 밖 샘플이 **초당 약 0.27개**, 즉 4초에 한 번은 나온다. "3σ 밖이면 이상치"라는 규칙을 고주파 스트림에 그대로 쓰면 오검출이 생각보다 많다. 이것이 9절 wake word 오작동 문제와 같은 구조다.

### 2.5 왜 noise는 자주 Gaussian인가 — 중심극한정리(CLT)

**중심극한정리(Central Limit Theorem)**: 서로 독립인 작은 요인들이 많이 **더해지면**, 각 요인의 분포 모양과 거의 상관없이 합은 Gaussian에 가까워진다. 센서 noise는 열잡음, 전원 리플, ADC 양자화, 기계적 진동 등 여러 작은 원인의 합이라서 Gaussian으로 잘 근사된다.

직접 확인해 보자. 전혀 종 모양이 아닌 uniform(−0.5, 0.5)을 k개 더한 뒤, |z| < 1, 2, 3 안에 들어오는 비율을 Gaussian 이론값과 비교한다.

```python
import numpy as np

rng = np.random.default_rng(1)
N = 100_000
# 작은 독립 잡음원 k개를 더한다: 각각은 uniform(-0.5, 0.5) — 전혀 종 모양이 아님
for k in (1, 2, 12):
    s = rng.uniform(-0.5, 0.5, size=(N, k)).sum(axis=1)
    s = (s - s.mean()) / s.std()                 # z-score로 맞춰서 비교
    within = [np.mean(np.abs(s) < m) for m in (1, 2, 3)]
    hist, _ = np.histogram(s, bins=6, range=(-3, 3))
    print(f"k={k:2d}  |z|<1,2,3 : {within[0]:.3f} {within[1]:.3f} {within[2]:.3f}"
          f"   hist={np.round(hist / N, 3)}")
print("Gaussian 이론값  : 0.683 0.954 0.997")
```

```text
k= 1  |z|<1,2,3 : 0.578 1.000 1.000   hist=[0.    0.211 0.29  0.288 0.212 0.   ]
k= 2  |z|<1,2,3 : 0.650 0.967 1.000   hist=[0.016 0.159 0.323 0.327 0.157 0.017]
k=12  |z|<1,2,3 : 0.679 0.955 0.998   hist=[0.021 0.138 0.339 0.34  0.138 0.022]
Gaussian 이론값  : 0.683 0.954 0.997
```

출력에서 볼 것: k = 1(uniform 그대로)은 히스토그램이 평평하고 ±2σ 밖이 아예 없다. k = 2는 삼각형, k = 12면 68/95/99.7이 이론값과 소수 셋째 자리까지 거의 같다. uniform(−0.5, 0.5)의 분산이 1/12이므로 12개를 더하면 분산이 정확히 1이 된다. 옛날 임베디드 코드에서 "uniform 난수 12개 더하고 6 빼기"로 Gaussian 난수를 만들던 트릭이 바로 이것이다.

---

## 3. 히스토그램과 표본 통계

### 3.1 히스토그램 = PDF의 눈으로 보는 추정

**히스토그램(histogram)** 은 값의 범위를 bin으로 나누고 각 bin에 들어온 샘플 수를 센 막대그래프다. bin 개수 ÷ (전체 개수 × bin 폭) 을 하면 PDF의 추정값이 된다. 펌웨어 엔지니어가 latency 분포를 보거나 ADC 코드 분포를 볼 때 이미 쓰던 도구다.

1.5절의 1000개 샘플을 `np.histogram(z, bins=12, range=(9.66, 9.96))`로 센 결과가 그림 2다(범위 밖 4개 제외).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 300">
<rect x="61.0" y="243.9" width="43.0" height="6.1" fill="#e08a3c" fill-opacity="0.75"/> <text x="82.5" y="239.9" font-size="12" text-anchor="middle">7</text> <rect x="106.0" y="241.2" width="43.0" height="8.8" fill="#e08a3c" fill-opacity="0.75"/> <text x="127.5" y="237.2" font-size="12" text-anchor="middle">10</text> <rect x="151.0" y="218.5" width="43.0" height="31.5" fill="#e08a3c" fill-opacity="0.75"/> <text x="172.5" y="214.5" font-size="12" text-anchor="middle">36</text>
<rect x="196.0" y="152.9" width="43.0" height="97.1" fill="#e08a3c" fill-opacity="0.75"/> <text x="217.5" y="148.9" font-size="12" text-anchor="middle">111</text> <rect x="241.0" y="114.4" width="43.0" height="135.6" fill="#e08a3c" fill-opacity="0.75"/> <text x="262.5" y="110.4" font-size="12" text-anchor="middle">155</text> <rect x="286.0" y="64.5" width="43.0" height="185.5" fill="#e08a3c" fill-opacity="0.75"/> <text x="307.5" y="60.5" font-size="12" text-anchor="middle">212</text>
<rect x="331.0" y="95.1" width="43.0" height="154.9" fill="#e08a3c" fill-opacity="0.75"/> <text x="352.5" y="91.1" font-size="12" text-anchor="middle">177</text> <rect x="376.0" y="127.5" width="43.0" height="122.5" fill="#e08a3c" fill-opacity="0.75"/> <text x="397.5" y="123.5" font-size="12" text-anchor="middle">140</text> <rect x="421.0" y="165.1" width="43.0" height="84.9" fill="#e08a3c" fill-opacity="0.75"/> <text x="442.5" y="161.1" font-size="12" text-anchor="middle">97</text>
<rect x="466.0" y="219.4" width="43.0" height="30.6" fill="#e08a3c" fill-opacity="0.75"/> <text x="487.5" y="215.4" font-size="12" text-anchor="middle">35</text> <rect x="511.0" y="237.8" width="43.0" height="12.2" fill="#e08a3c" fill-opacity="0.75"/> <text x="532.5" y="233.8" font-size="12" text-anchor="middle">14</text> <rect x="556.0" y="248.2" width="43.0" height="1.8" fill="#e08a3c" fill-opacity="0.75"/> <text x="577.5" y="244.2" font-size="12" text-anchor="middle">2</text>
<polyline points="60.0,248.1 69.0,247.5 78.0,246.6 87.0,245.5 96.0,244.1 105.0,242.3 114.0,240.2 123.0,237.5 132.0,234.2 141.0,230.3 150.0,225.7 159.0,220.4 168.0,214.2 177.0,207.2 186.0,199.4 195.0,190.8 204.0,181.4 213.0,171.4 222.0,160.9 231.0,150.0 240.0,138.9 249.0,127.9 258.0,117.1 267.0,107.0 276.0,97.7 285.0,89.5 294.0,82.6 303.0,77.2 312.0,73.5 321.0,71.6 330.0,71.5 339.0,73.4 348.0,77.0 357.0,82.3 366.0,89.2 375.0,97.3 384.0,106.6 393.0,116.7 402.0,127.4 411.0,138.4 420.0,149.5 429.0,160.4 438.0,171.0 447.0,181.0 456.0,190.4 465.0,199.1 474.0,206.9 483.0,214.0 492.0,220.2 501.0,225.5 510.0,230.2 519.0,234.1 528.0,237.4 537.0,240.1 546.0,242.3 555.0,244.0 564.0,245.5 573.0,246.6 582.0,247.5 591.0,248.1 600.0,248.6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <line x1="60" y1="250" x2="600" y2="250" stroke="currentColor"/> <line x1="60" y1="250" x2="60" y2="40" stroke="currentColor"/> <line x1="60.0" y1="250" x2="60.0" y2="255" stroke="currentColor"/> <text x="60.0" y="270" font-size="12" text-anchor="middle">9.66</text> <line x1="150.0" y1="250" x2="150.0" y2="255" stroke="currentColor"/>
<text x="150.0" y="270" font-size="12" text-anchor="middle">9.71</text> <line x1="240.0" y1="250" x2="240.0" y2="255" stroke="currentColor"/> <text x="240.0" y="270" font-size="12" text-anchor="middle">9.76</text> <line x1="330.0" y1="250" x2="330.0" y2="255" stroke="currentColor"/> <text x="330.0" y="270" font-size="12" text-anchor="middle">9.81</text> <line x1="420.0" y1="250" x2="420.0" y2="255" stroke="currentColor"/>
<text x="420.0" y="270" font-size="12" text-anchor="middle">9.86</text> <line x1="510.0" y1="250" x2="510.0" y2="255" stroke="currentColor"/> <text x="510.0" y="270" font-size="12" text-anchor="middle">9.91</text> <line x1="600.0" y1="250" x2="600.0" y2="255" stroke="currentColor"/> <text x="600.0" y="270" font-size="12" text-anchor="middle">9.96</text> <text x="52" y="254.0" font-size="12" text-anchor="end">0</text>
<text x="52" y="166.5" font-size="12" text-anchor="end">100</text> <text x="52" y="79.0" font-size="12" text-anchor="end">200</text> <line x1="325.7" y1="250" x2="325.7" y2="40" stroke="#d0564a" stroke-dasharray="5 3"/> <text x="331.7" y="50" font-size="12">표본 평균 9.8076</text> <text x="65" y="24" font-size="13">z축 1000개 샘플 히스토그램 + 추정한 Gaussian 곡선</text> <text x="330.0" y="290" font-size="12" text-anchor="middle">가속도 z [m/s²]</text>
</svg>
```

그림 2 — 시뮬레이션한 정지 가속도계 z축 샘플 1000개의 히스토그램(주황)과, 추정한 평균·표준편차로 그린 Gaussian 기대 개수(파랑 곡선 = 1000 × 0.025 × pdf). 빨간 점선이 표본 평균.

### 3.2 추정량과 그 흔들림

샘플로 구한 평균 `μ̂`, 표준편차 `σ̂`는 **추정량(estimator)** 이다. 샘플을 다시 뽑으면 조금 다른 값이 나온다.

```
평균의 표준오차   SE(μ̂) = σ / √n
```

말로 하면: 평균의 불확실성은 샘플 수의 제곱근에 반비례해 줄어든다. 정밀도를 10배 올리려면 샘플이 100배 필요하다. 센서 보정 루틴에서 "정지 상태로 몇 초 모을까"를 정할 때 쓰는 식이다. 예: σ = 0.05, 원하는 offset 정밀도 0.001이면 `n ≈ (0.05/0.001)² = 2500` 샘플, 100 Hz라면 25초.

### 3.3 임베디드 연결

- 학습 데이터의 히스토그램은 C1 양자화 calibration에서 activation 범위를 정하는 데 그대로 쓰인다 (min-max, percentile, KL). 8절에서 KL로 다시 연결한다.

---

## 4. 표준화(z-score)와 min-max 스케일링

### 4.1 왜 필요한가

feature마다 단위와 크기가 다르다. 가속도 RMS는 10 근처에서 ±0.3 흔들리고, 자이로 RMS는 6 ~ 48 deg/s를 오간다. 이대로 모델에 넣으면 숫자가 큰 feature가 학습을 지배하고, gradient 크기가 feature마다 달라져 학습이 느려진다(A3에서 자세히). 그래서 모든 feature를 **비슷한 크기**로 맞춘다.

```
z-score (standardization)  z = (x − μ) / σ           → 평균 0, 표준편차 1
min-max scaling            x' = (x − min) / (max − min) → [0, 1] 범위
```

말로 하면: z-score는 "평균에서 표준편차 몇 개만큼 떨어졌는가"로 바꾸는 것이고, min-max는 "학습 데이터 범위에서 몇 % 위치인가"로 바꾸는 것이다.

### 4.2 손으로 계산: 윈도우 4개, feature 2개

| 윈도우 | 가속도 RMS | 자이로 RMS |
|---|---|---|
| 0 | 9.9 | 12 |
| 1 | 10.3 | 48 |
| 2 | 9.7 | 6 |
| 3 | 10.1 | 30 |

```
가속도: μ = 10.0, 편차 −0.1, 0.3, −0.3, 0.1 → 편차² 합 0.2 → σ² = 0.05, σ = 0.2236
자이로: μ = 24.0, 편차 −12, 24, −18, 6   → 편차² 합 1080 → σ² = 270, σ = 16.43
윈도우 1의 z = ((10.3 − 10.0)/0.2236, (48 − 24)/16.43) = (1.342, 1.461)
윈도우 1의 min-max = ((10.3 − 9.7)/0.6, (48 − 6)/42) = (1.0, 1.0)
```

### 4.3 코드로 확인 — 그리고 "기기 쪽 통계를 쓰면" 벌어지는 일

```python
import numpy as np

# 4개 윈도우에서 뽑은 feature 2개: [가속도 RMS (m/s²), 자이로 RMS (deg/s)]
X_train = np.array([[ 9.9,  12.0],
                    [10.3,  48.0],
                    [ 9.7,   6.0],
                    [10.1,  30.0]], dtype=np.float32)
mu = X_train.mean(axis=0)
sd = X_train.std(axis=0)
print("mean :", mu)
print("std  :", sd)
Z = (X_train - mu) / sd
print("z-score:\n", np.round(Z, 3))
lo, hi = X_train.min(axis=0), X_train.max(axis=0)
print("min-max row1:", (X_train[1] - lo) / (hi - lo))

# 기기에서 새 윈도우가 들어왔다: train 통계로 변환해야 한다
x_dev = np.array([10.0, 24.0], dtype=np.float32)
print("device z (train stats) :", np.round((x_dev - mu) / sd, 3))
# 흔한 실수: 기기 쪽에서 '지금 윈도우 몇 개'로 통계를 다시 계산
X_dev = np.array([[10.0, 24.0], [10.2, 26.0]], dtype=np.float32)
wrong = (X_dev - X_dev.mean(0)) / X_dev.std(0)
print("device z (own stats)   :", np.round(wrong[0], 3))
```

```text
mean : [10. 24.]
std  : [ 0.223607 16.431677]
z-score:
 [[-0.447 -0.73 ]
 [ 1.342  1.461]
 [-1.342 -1.095]
 [ 0.447  0.365]]
min-max row1: [1. 1.]
device z (train stats) : [0. 0.]
device z (own stats)   : [-1. -1.]
```

출력에서 볼 것: 손계산과 같은 값이 나온다. 마지막 두 줄이 핵심이다. 기기에 들어온 새 윈도우 `[10.0, 24.0]`은 학습 통계로 변환하면 `[0, 0]`(= 아주 평범한 윈도우)인데, 기기가 **자기가 본 윈도우 몇 개로 통계를 다시 계산**하면 `[−1, −1]`(= 1σ만큼 작은 윈도우)로 둔갑한다. 같은 입력인데 모델에는 전혀 다른 숫자가 들어간다.

### 4.4 규칙: 학습 때 쓴 μ, σ를 그대로 펌웨어에 박는다

- μ, σ(또는 min, max)는 **train split에서만** 계산한다. val/test/기기 데이터로 계산하면 정보가 새고(data leakage, A4), 기기에서는 애초에 믿을 만한 통계를 낼 수 없다.
- 이 숫자들은 모델 가중치와 **같은 버전**으로 관리해야 한다. 모델만 재학습하고 헤더의 μ, σ를 안 바꾸면 에러 없이 정확도만 떨어진다. 펌웨어로 치면 "보정 테이블 버전과 코드 버전이 어긋난" 버그다.
- 가능하면 표준화를 모델 그래프 안의 첫 레이어로 넣어서(상수 뺄셈·곱셈) 모델 파일과 한 몸으로 만든다. 양자화 모델이라면 입력 scale/zero-point에 합쳐지기도 한다(C1).

아래 C 코드는 Python이 출력한 μ와 1/σ를 상수로 박고, 나눗셈 대신 곱셈으로 표준화한다. MCU에서 float 나눗셈은 곱셈보다 훨씬 느리다(코어에 따라 수 사이클 vs 십수 사이클).

```c
#include <stdio.h>

/* 학습 스크립트가 X_train에서 계산해 출력한 값을 그대로 복사한 상수.
 * 모델 파일과 같은 버전으로 관리한다 (예: 빌드 때 헤더 자동 생성). */
#define N_FEAT 2
static const float FEAT_MEAN[N_FEAT]    = { 10.0f, 24.0f };
static const float FEAT_INV_STD[N_FEAT] = { 1.0f / 0.223607f, 1.0f / 16.431677f };

static void standardize(const float *x, float *out) {
    for (int i = 0; i < N_FEAT; i++)
        out[i] = (x[i] - FEAT_MEAN[i]) * FEAT_INV_STD[i];  /* 나눗셈 대신 곱셈 */
}

int main(void) {
    const float win[5][N_FEAT] = { { 9.9f, 12.0f }, { 10.3f, 48.0f }, { 9.7f, 6.0f },
                                   { 10.1f, 30.0f }, { 10.0f, 24.0f } };  /* 마지막 = 새 윈도우 */
    for (int w = 0; w < 5; w++) {
        float z[N_FEAT];
        standardize(win[w], z);
        printf("win %d: x=(%5.1f, %4.1f) -> z=(%6.3f, %6.3f)\n", w, win[w][0], win[w][1], z[0], z[1]);
    }
    return 0;
}
```

컴파일·실행: `cc -std=c11 -Wall -Wextra -O2 preprocess.c -o preprocess -lm && ./preprocess` (경고 0개)

```text
win 0: x=(  9.9, 12.0) -> z=(-0.447, -0.730)
win 1: x=( 10.3, 48.0) -> z=( 1.342,  1.461)
win 2: x=(  9.7,  6.0) -> z=(-1.342, -1.095)
win 3: x=( 10.1, 30.0) -> z=( 0.447,  0.365)
win 4: x=( 10.0, 24.0) -> z=( 0.000,  0.000)
```

출력에서 볼 것: 윈도우 0~3의 z가 Python 결과와 소수 셋째 자리까지 같다. 윈도우 4(새 입력)는 정확히 (0, 0)이다. 이렇게 **Python 출력과 C 출력을 같은 입력으로 비교하는 테스트**를 golden vector로 CI에 넣어 두면 μ, σ 버전 불일치를 잡을 수 있다(C8).

### 4.5 z-score vs min-max 선택

| | z-score | min-max |
|---|---|---|
| 결과 범위 | 대략 −3 ~ +3, 제한 없음 | 학습 데이터 기준 [0, 1] |
| 이상치 영향 | 작음 (σ가 조금 커질 뿐) | 큼 (max 하나가 전체를 눌러 버림) |
| 새 데이터가 범위 밖이면 | 그냥 큰 z | 0보다 작거나 1보다 큼 → clip 필요 |
| 자주 쓰는 곳 | 센서 feature, 대부분의 표 데이터 | 이미지 픽셀(0~255 → 0~1), 고정 범위 신호 |

---

## 5. 공분산과 상관 — 가속도 x와 y

### 5.1 직관

손목을 기울이면 중력이 x축과 y축에 동시에 나뉘어 들어간다. 그러면 x가 커질 때 y도 같이 커지는 경향이 생긴다. 두 확률변수가 "같이 움직이는 정도"를 재는 것이 **공분산(covariance)** 과 **상관계수(correlation)** 다.

```
Cov(X, Y) = E[(X − μx)(Y − μy)]
Corr(X, Y) = Cov(X, Y) / (σx · σy)        ∈ [−1, 1]
```

말로 하면: 두 편차를 곱해서 평균한다. 둘이 같은 방향으로 벗어나면 곱이 양수, 반대면 음수다. 상관계수는 그것을 각자의 σ로 나눠 단위를 없앤 것이다. +1이면 완벽한 직선 관계, 0이면 선형 관계 없음이다.

### 5.2 손으로 계산

x = `[0, 1, 2, 3]`, y = `[0.1, 0.9, 2.2, 2.8]`.

```
μx = 1.5,  x 편차 = −1.5, −0.5, 0.5, 1.5
μy = 1.5,  y 편차 = −1.4, −0.6, 0.7, 1.3
곱        =  2.10,  0.30, 0.35, 1.95   → 합 4.70 → Cov = 4.70 / 4 = 1.175
Var(x) = (2.25 + 0.25 + 0.25 + 2.25)/4 = 1.25
Var(y) = (1.96 + 0.36 + 0.49 + 1.69)/4 = 1.125
Corr = 1.175 / √(1.25 × 1.125) = 1.175 / 1.1859 ≈ 0.991
```

```python
import numpy as np

# 손계산용 4개 샘플: 손목을 기울이면 x가 늘 때 y도 같이 는다고 가정
ax = np.array([0.0, 1.0, 2.0, 3.0])
ay = np.array([0.1, 0.9, 2.2, 2.8])
cov = np.mean((ax - ax.mean()) * (ay - ay.mean()))      # 모집단 공분산
corr = cov / (ax.std() * ay.std())
print("cov  (manual) :", round(cov, 4))
print("corr (manual) :", round(corr, 4))
print("np.cov bias=True:\n", np.round(np.cov(ax, ay, bias=True), 4))
print("np.corrcoef:\n", np.round(np.corrcoef(ax, ay), 4))

# 큰 샘플: 기울기 각도 θ가 x, y 둘 다에 들어가면 상관이 생긴다
rng = np.random.default_rng(2)
theta = rng.uniform(-0.5, 0.5, 5000)                    # rad
x = 9.81 * np.sin(theta) + rng.normal(0, 0.05, 5000)
y = 0.8 * x + rng.normal(0, 0.5, 5000)                  # y가 x를 일부 따라감
z = rng.normal(0, 0.5, 5000)                            # 독립 축
print("corr(x,y) =", round(np.corrcoef(x, y)[0, 1], 3),
      " corr(x,z) =", round(np.corrcoef(x, z)[0, 1], 3))
```

```text
cov  (manual) : 1.175
corr (manual) : 0.9908
np.cov bias=True:
 [[1.25  1.175]
 [1.175 1.125]]
np.corrcoef:
 [[1.     0.9908]
 [0.9908 1.    ]]
corr(x,y) = 0.976  corr(x,z) = -0.012
```

출력에서 볼 것: `np.cov(..., bias=True)`의 대각선은 각자의 분산, 비대각선이 공분산이다(이것이 **공분산 행렬**). `bias=True`를 빼면 n−1로 나눈다. 큰 샘플에서 x와 y는 0.976으로 강하게 상관되고, 독립으로 만든 z축과의 상관은 거의 0이다.

### 5.3 왜 상관된 feature가 중요한가 (미리 보기)

- **중복 정보**: 상관이 0.98인 두 feature는 사실상 같은 정보다. MCU에서 feature 하나를 계산하는 데도 사이클과 메모리가 든다. 하나를 버려도 정확도가 거의 안 떨어진다면 버리는 것이 이득이다.
- **학습의 어려움**: 입력이 강하게 상관되면 loss 곡면이 한쪽으로 길쭉해져 gradient descent가 지그재그로 느리게 수렴한다(A3). 표준화만으로는 이것이 해결되지 않는다. PCA/whitening이 공분산 행렬의 고유벡터로 축을 돌려 상관을 없애는 방법이다(A1의 고유값 분해와 연결).

---

## 6. sigmoid와 softmax — logits를 확률로

### 6.1 logit이란

분류 모델의 마지막 선형 레이어는 `z = W·h + b`로 **아무 실수**나 낸다. 이 값을 **logit**이라고 부른다. 확률은 [0, 1]이어야 하고 합이 1이어야 하므로 변환이 필요하다.

- 이진 분류(출력 1개): **sigmoid** `σ(z) = 1 / (1 + e^(−z))`
- 다중 분류(출력 K개): **softmax** `softmax(z)ᵢ = e^(zᵢ) / ∑ⱼ e^(zⱼ)`

말로 하면: sigmoid는 실수 하나를 (0, 1)로 눌러 담는 S자 함수이고, softmax는 K개 점수를 각각 지수로 키운 뒤 전체 합으로 나눠 "몫"을 만드는 함수다. 지수를 쓰므로 모두 양수가 되고, 큰 점수가 훨씬 더 큰 몫을 가져간다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 300">
<line x1="60" y1="145.0" x2="600" y2="145.0" stroke="#888" stroke-dasharray="4 4"/> <line x1="60" y1="40.0" x2="600" y2="40.0" stroke="#888" stroke-dasharray="4 4"/> <polyline points="60.0,249.5 71.2,249.3 82.5,249.1 93.8,248.9 105.0,248.6 116.2,248.2 127.5,247.7 138.8,247.0 150.0,246.2 161.2,245.2 172.5,243.8 183.8,242.2 195.0,240.0 206.2,237.4 217.5,234.1 228.8,230.0 240.0,225.0 251.2,218.9 262.5,211.7 273.8,203.2 285.0,193.5 296.2,182.6 307.5,170.7 318.8,158.1 330.0,145.0 341.2,131.9 352.5,119.3 363.8,107.4 375.0,96.5 386.2,86.8 397.5,78.3 408.8,71.1 420.0,65.0 431.2,60.0 442.5,55.9 453.8,52.6 465.0,50.0 476.2,47.8 487.5,46.2 498.8,44.8 510.0,43.8 521.2,43.0 532.5,42.3 543.8,41.8 555.0,41.4 566.2,41.1 577.5,40.9 588.8,40.7 600.0,40.5" fill="none" stroke="#3f9a6b" stroke-width="2.5"/> <line x1="60" y1="250" x2="600" y2="250" stroke="currentColor"/> <line x1="330.0" y1="250" x2="330.0" y2="35" stroke="currentColor"/> <line x1="60.0" y1="250" x2="60.0" y2="255" stroke="currentColor"/>
<text x="60.0" y="270" font-size="12" text-anchor="middle">-6</text> <line x1="150.0" y1="250" x2="150.0" y2="255" stroke="currentColor"/> <text x="150.0" y="270" font-size="12" text-anchor="middle">-4</text> <line x1="240.0" y1="250" x2="240.0" y2="255" stroke="currentColor"/> <text x="240.0" y="270" font-size="12" text-anchor="middle">-2</text> <line x1="330.0" y1="250" x2="330.0" y2="255" stroke="currentColor"/>
<text x="330.0" y="270" font-size="12" text-anchor="middle">0</text> <line x1="420.0" y1="250" x2="420.0" y2="255" stroke="currentColor"/> <text x="420.0" y="270" font-size="12" text-anchor="middle">2</text> <line x1="510.0" y1="250" x2="510.0" y2="255" stroke="currentColor"/> <text x="510.0" y="270" font-size="12" text-anchor="middle">4</text> <line x1="600.0" y1="250" x2="600.0" y2="255" stroke="currentColor"/>
<text x="600.0" y="270" font-size="12" text-anchor="middle">6</text> <text x="322.0" y="254.0" font-size="12" text-anchor="end">0</text> <text x="322.0" y="149.0" font-size="12" text-anchor="end">0.5</text> <text x="322.0" y="44.0" font-size="12" text-anchor="end">1</text> <circle cx="240.0" cy="225.0" r="4" fill="#e08a3c"/> <text x="232.0" y="217.0" font-size="12" text-anchor="end">σ(-2) = 0.1192</text>
<circle cx="420.0" cy="65.0" r="4" fill="#e08a3c"/> <text x="428.0" y="81.0" font-size="12" text-anchor="start">σ(2) = 0.8808</text> <text x="60" y="24" font-size="13">σ(z) = 1 / (1 + e^(−z)): 실수 logit → 확률 (0, 1)</text> <text x="330.0" y="290" font-size="12" text-anchor="middle">logit z</text>
</svg>
```

그림 3 — sigmoid 곡선(49개 점을 실제 계산). z = 0에서 정확히 0.5, z = ±2에서 0.8808 / 0.1192이다. |z|가 5를 넘으면 거의 0 또는 1에 붙는다(포화).

sigmoid의 역함수가 **logit 함수** `z = log(p / (1 − p))`(log-odds)다. 그래서 "logit"이라는 이름이 붙었다. p = 0.9 ↔ z = log 9 ≈ 2.197.

### 6.2 손으로 계산: 3-class softmax

logits `z = [2.0, 1.0, 0.1]` (idle, walk, run).

```
e^2.0 = 7.389,  e^1.0 = 2.718,  e^0.1 = 1.105        합 = 11.212
p = [7.389/11.212, 2.718/11.212, 1.105/11.212] = [0.659, 0.242, 0.099]
```

성질 세 가지:

- **순서 보존**: logit이 가장 큰 class가 확률도 가장 크다. 그래서 argmax만 필요하면 softmax를 계산할 필요가 없다(11절).
- **평행이동 불변**: 모든 logit에 같은 상수 c를 더해도 결과가 같다. `e^(zᵢ+c) / ∑ e^(zⱼ+c) = e^c·e^(zᵢ) / (e^c·∑ e^(zⱼ))`. 이것이 수치 안정화의 근거다.
- **2-class softmax = sigmoid**: `softmax([z₁, z₂])₁ = 1/(1 + e^(−(z₁−z₂))) = σ(z₁ − z₂)`.

### 6.3 수치 안정성 — 왜 최댓값을 빼는가

float32가 표현하는 최댓값은 약 3.4 × 10³⁸이고, `ln(3.4 × 10³⁸) ≈ 88.72`다. 즉 **logit이 88.7을 넘으면 `exp`가 inf**가 된다. inf / inf = NaN이다. logit 100은 드문 값이 아니다(학습 초기의 폭주, 온도를 낮춘 경우, 양자화 스케일을 잘못 되돌린 경우).

해결책은 평행이동 불변성을 쓰는 것이다. `m = max(z)`를 모든 logit에서 빼면:

- 가장 큰 항은 `e^0 = 1`이 되어 overflow가 불가능하다.
- 분모는 최소 1이므로 0으로 나눌 일도 없다.
- 작은 항이 underflow로 0이 되는 것은 괜찮다. 원래도 무시할 만큼 작은 확률이었다.

```
손계산: z = [2.0, 1.0, 0.1], m = 2.0 → z − m = [0, −1.0, −1.9]
e^0 = 1, e^−1 = 0.3679, e^−1.9 = 0.1496    합 = 1.5175
p = [0.659, 0.242, 0.099]   ← 같은 결과, 중간값은 모두 ≤ 1
```

```python
import numpy as np

def sigmoid(z):
    return 1.0 / (1.0 + np.exp(-z))

def softmax_naive(z):
    e = np.exp(z)
    return e / e.sum()

def softmax_stable(z):
    e = np.exp(z - z.max())          # 최댓값을 빼면 가장 큰 항이 exp(0) = 1
    return e / e.sum()

print("sigmoid(-2,0,2) :", np.round(sigmoid(np.array([-2.0, 0.0, 2.0])), 4))
z = np.array([2.0, 1.0, 0.1])        # 3-class logits: [idle, walk, run]
print("softmax(2,1,0.1):", np.round(softmax_stable(z), 4))

big = np.array([100.0, 99.0, 90.1], dtype=np.float32)   # 스케일이 큰 logits
with np.errstate(over="ignore", invalid="ignore"):       # 경고 대신 값을 직접 본다
    print("exp(big) f32    :", np.exp(big))
    print("naive  f32 big  :", softmax_naive(big))
print("stable f32 big  :", np.round(softmax_stable(big), 4))
print("f32 max         :", np.finfo(np.float32).max, " ln(max) =", round(float(np.log(np.finfo(np.float32).max)), 2))
# 2-class softmax == sigmoid(logit 차이)
print("softmax([3,1])[0] =", round(softmax_stable(np.array([3.0, 1.0]))[0], 6),
      " sigmoid(2) =", round(sigmoid(2.0), 6))
```

```text
sigmoid(-2,0,2) : [0.1192 0.5    0.8808]
softmax(2,1,0.1): [0.659  0.2424 0.0986]
exp(big) f32    : [inf inf inf]
naive  f32 big  : [nan nan nan]
stable f32 big  : [0.731  0.2689 0.    ]
f32 max         : 3.4028235e+38  ln(max) = 88.72
softmax([3,1])[0] = 0.880797  sigmoid(2) = 0.880797
```

출력에서 볼 것: `exp(big)`이 전부 inf가 되고 naive softmax는 NaN만 낸다. 안정 버전은 `[0.731, 0.269, 0.000]`을 정상적으로 낸다. 100과 99의 차이가 1이므로 결과는 `softmax([1, 0])` ≈ `[0.731, 0.269]`과 같다. 마지막 줄은 2-class softmax와 sigmoid가 같다는 확인이다.

### 6.4 C로 구현한 안정 softmax

펌웨어에서 쓸 모양 그대로다. 세 번의 pass(max → exp와 합 → 역수 곱)로 구성된다.

```c
#include <math.h>
#include <stdio.h>

static void softmax_naive(const float *z, float *p, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) { p[i] = expf(z[i]); sum += p[i]; }
    for (int i = 0; i < n; i++) p[i] /= sum;
}

static void softmax_stable(const float *z, float *p, int n) {
    float m = z[0];
    for (int i = 1; i < n; i++) if (z[i] > m) m = z[i];                  /* pass 1: max */
    float sum = 0.0f;
    for (int i = 0; i < n; i++) { p[i] = expf(z[i] - m); sum += p[i]; } /* pass 2: exp, 합 */
    const float inv = 1.0f / sum;                                        /* 분모 ≥ 1 보장 */
    for (int i = 0; i < n; i++) p[i] *= inv;                             /* pass 3: 정규화 */
}

int main(void) {
    const float small[3] = { 2.0f, 1.0f, 0.1f }, big[3] = { 100.0f, 99.0f, 90.1f };
    float p[3];
    softmax_stable(small, p, 3); printf("stable small : %.4f %.4f %.4f\n", p[0], p[1], p[2]);
    softmax_naive(big, p, 3);    printf("naive  big   : %f %f %f\n", p[0], p[1], p[2]);
    softmax_stable(big, p, 3);   printf("stable big   : %.4f %.4f %.4f\n", p[0], p[1], p[2]);
    printf("expf(88)=%g expf(89)=%g\n", expf(88.0f), expf(89.0f));
    return 0;
}
```

컴파일·실행: `cc -std=c11 -Wall -Wextra -O2 softmax.c -o softmax -lm && ./softmax` (경고 0개)

```text
stable small : 0.6590 0.2424 0.0986
naive  big   : nan nan nan
stable big   : 0.7310 0.2689 0.0000
expf(88)=1.65164e+38 expf(89)=inf
```

출력에서 볼 것: C의 `expf(89)`도 inf이고 naive 버전은 NaN이다. 안정 버전은 Python과 같은 값이다. NaN은 이후 비교 연산(`p > threshold`)을 전부 false로 만들기 때문에 "wake word가 영원히 안 걸리는" 조용한 버그가 된다. NaN이 한 번 생기면 RNN 상태나 필터 상태를 타고 계속 전파되는 것도 주의할 점이다.

---

## 7. Likelihood, MLE, cross-entropy

### 7.1 Likelihood — "이 파라미터면 이 데이터가 나올 가능성"

확률 `p(data | θ)`를 파라미터 θ의 함수로 보면 **likelihood** `L(θ)`라고 부른다. 같은 식인데 보는 방향이 다르다.

- 확률: θ를 고정하고 "어떤 데이터가 나올까?"
- likelihood: 데이터를 고정하고 "어떤 θ가 이 데이터를 가장 잘 설명할까?"

**최대 우도 추정(MLE, Maximum Likelihood Estimation)**: likelihood를 최대로 만드는 θ를 고른다. 샘플이 독립이면 likelihood는 곱이 되고, 곱은 underflow가 나기 쉽고 미분하기 불편하므로 log를 씌워 **합**으로 바꾼다(log는 단조 증가라 최댓값 위치가 같다).

```
L(θ)      = ∏ᵢ p(xᵢ | θ)
log L(θ)  = ∑ᵢ log p(xᵢ | θ)
MLE       θ̂ = argmax log L(θ)  =  argmin ( −∑ᵢ log p(xᵢ | θ) )    ← NLL (negative log-likelihood)
```

말로 하면: ML에서 "loss를 최소화한다"는 거의 항상 "데이터의 negative log-likelihood를 최소화한다"는 뜻이다.

### 7.2 손으로 계산: 착용 감지 로그 10개

착용 여부 로그가 `[1,1,0,1,1,1,0,1,1,0]`(7번 착용)이다. Bernoulli(p)로 모델링하면 `L(p) = p⁷ (1−p)³`.

```
p = 0.5 : 0.5¹⁰            = 0.000977   log L = −6.93
p = 0.7 : 0.7⁷ × 0.3³      = 0.002224   log L = −6.11   ← 최대
p = 0.9 : 0.9⁷ × 0.1³      = 0.000478   log L = −7.65
미분:  d/dp [7 log p + 3 log(1−p)] = 7/p − 3/(1−p) = 0  →  p̂ = 7/10
```

결과 p̂ = 0.7은 표본 평균과 같다. **Bernoulli의 MLE = 표본 비율**, **Gaussian의 μ에 대한 MLE = 표본 평균**이다. 1절에서 평균을 구한 것이 사실 MLE였다.

### 7.3 Cross-entropy — 분류 loss

분류 모델은 각 샘플마다 categorical 분포 `q = softmax(z)`를 낸다. 정답이 class y일 때 그 샘플의 NLL은 `−log q_y`다. 정답을 one-hot 분포 `p`로 쓰면 이것은 **cross-entropy** `H(p, q) = −∑ₖ pₖ log qₖ`와 같다(정답 자리만 pₖ = 1이므로 한 항만 남는다).

```
categorical CE (한 샘플)   L = −log q_y = −log softmax(z)_y
binary CE (한 샘플)        L = −[ y·log σ(z) + (1 − y)·log(1 − σ(z)) ]
배치 loss                  위 값을 샘플 평균
```

말로 하면: 정답 class에 준 확률이 1이면 loss 0, 0.5면 0.693, 0.1이면 2.30이다. **정답에 확률을 적게 줄수록 loss가 log 스케일로 커진다.** 확신에 차서 틀리면 크게 벌 받는다.

### 7.4 손으로 계산

| 샘플 | logits [idle, walk, run] | 정답 | q(정답) | −log q |
|---|---|---|---|---|
| 0 | [2.0, 1.0, 0.1] | idle | 0.659 | 0.417 |
| 1 | [0.5, 2.5, 0.0] | walk | 0.821 | 0.197 |
| 2 | [0.0, 0.0, 3.0] | run | 0.909 | 0.095 |
| 3 | [1.0, 3.0, 0.2] | idle | 0.113 | 2.179 |

```
샘플 3: e^1 = 2.718, e^3 = 20.086, e^0.2 = 1.221, 합 = 24.025
        q(idle) = 2.718 / 24.025 = 0.1131 → −ln 0.1131 = 2.179
평균 CE = (0.417 + 0.197 + 0.095 + 2.179) / 4 = 0.722
```

틀린 샘플 하나(샘플 3)가 loss의 75%를 차지한다. 학습은 이 샘플의 gradient에 가장 크게 반응한다.

### 7.5 log-softmax + NLL, 그리고 PyTorch `nn.CrossEntropyLoss`

`−log softmax(z)_y`를 그대로 계산하면 softmax에서 아주 작은 확률이 0으로 underflow된 뒤 log(0) = −inf가 될 수 있다. 그래서 log를 안으로 넣어 한 번에 계산한다.

```
log softmax(z)_y = z_y − log ∑ⱼ e^(zⱼ)
                 = z_y − ( m + log ∑ⱼ e^(zⱼ − m) )      m = max(z)   ← log-sum-exp 트릭
샘플 0: logsumexp([2, 1, 0.1]) = 2 + ln(1.5175) = 2.4171 → CE = 2.4171 − 2.0 = 0.4171
```

PyTorch의 `nn.CrossEntropyLoss`는 **logits**를 받아서 내부에서 이 log-softmax + NLL을 한다(공식 문서: "equivalent to applying LogSoftmax on an input, followed by NLLLoss"). 이진 분류용 `nn.BCEWithLogitsLoss`도 sigmoid와 BCE를 합쳐 안정적으로 계산한다.

```python
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

# 4개 샘플, 3 class [idle, walk, run] — 모델이 낸 logits와 정답 label
logits = torch.tensor([[2.0, 1.0, 0.1],
                       [0.5, 2.5, 0.0],
                       [0.0, 0.0, 3.0],
                       [1.0, 3.0, 0.2]])
target = torch.tensor([0, 1, 2, 0])          # 마지막 샘플은 틀리게 예측 중

p = torch.softmax(logits, dim=1)
manual = -torch.log(p[torch.arange(4), target])   # 정답 class 확률의 -log
print("p(correct)   :", p[torch.arange(4), target].numpy().round(4))
print("per-sample CE:", manual.numpy().round(4))
print("mean CE      :", round(manual.mean().item(), 4))

print("nn.CrossEntropyLoss(logits):", round(nn.CrossEntropyLoss()(logits, target).item(), 4))
print("log_softmax + nll_loss     :", round(F.nll_loss(F.log_softmax(logits, 1), target).item(), 4))
# 흔한 실수: softmax를 먼저 하고 CrossEntropyLoss에 넣기 (softmax 두 번)
print("WRONG CE(softmax(logits))  :", round(nn.CrossEntropyLoss()(p, target).item(), 4))

# binary: wake word 여부 (logit 1개)
zb = torch.tensor([2.0, -1.0, 0.0]); yb = torch.tensor([1.0, 0.0, 1.0])
print("BCEWithLogits:", round(nn.BCEWithLogitsLoss()(zb, yb).item(), 4),
      " manual:", round((-(yb*torch.log(torch.sigmoid(zb)) + (1-yb)*torch.log(1-torch.sigmoid(zb)))).mean().item(), 4))
```

```text
p(correct)   : [0.659  0.8214 0.9094 0.1131]
per-sample CE: [0.417  0.1967 0.0949 2.1791]
mean CE      : 0.7219
nn.CrossEntropyLoss(logits): 0.7219
log_softmax + nll_loss     : 0.7219
WRONG CE(softmax(logits))  : 0.8684
BCEWithLogits: 0.3778  manual: 0.3778
```

출력에서 볼 것: 손계산, `nn.CrossEntropyLoss(logits)`, `log_softmax + nll_loss`가 모두 0.7219로 같다. **softmax를 먼저 씌운 값을 넣으면 0.8684**로 틀린 값이 나오지만 에러는 안 난다. 가장 흔한 초보 실수다. BCE도 손계산(`(0.1269 + 0.3133 + 0.6931)/3`)과 일치한다.

### 7.6 왜 분류에 MSE가 아니라 cross-entropy인가

정답이 1인데 모델이 logit −6으로 "확신에 차서 틀린" 상황을 보자. sigmoid 출력에 MSE를 쓰면 gradient는 `2(p − 1)·σ'(z)`인데, σ'(z) = p(1−p)가 포화 구간에서 거의 0이다. 반면 BCE의 logit에 대한 gradient는 정확히 `p − y`라서 포화와 무관하다.

```python
import torch

# 정답 y=1인데 모델이 logit z로 '확신에 차서 틀린' 상황을 만들어 본다
for zval in (-6.0, -2.0, 0.0, 2.0):
    z = torch.tensor(zval, requires_grad=True)
    p = torch.sigmoid(z)
    mse = (p - 1.0) ** 2
    g_mse, = torch.autograd.grad(mse, z)
    z2 = torch.tensor(zval, requires_grad=True)
    bce = torch.nn.functional.binary_cross_entropy_with_logits(z2, torch.tensor(1.0))
    g_ce, = torch.autograd.grad(bce, z2)
    print(f"z={zval:5.1f} p={p.item():.4f}  MSE={mse.item():.4f} dMSE/dz={g_mse.item():+.4f}"
          f"   CE={bce.item():.4f} dCE/dz={g_ce.item():+.4f}")
```

```text
z= -6.0 p=0.0025  MSE=0.9951 dMSE/dz=-0.0049   CE=6.0025 dCE/dz=-0.9975
z= -2.0 p=0.1192  MSE=0.7758 dMSE/dz=-0.1850   CE=2.1269 dCE/dz=-0.8808
z=  0.0 p=0.5000  MSE=0.2500 dMSE/dz=-0.2500   CE=0.6931 dCE/dz=-0.5000
z=  2.0 p=0.8808  MSE=0.0142 dMSE/dz=-0.0250   CE=0.1269 dCE/dz=-0.1192
```

출력에서 볼 것: z = −6(심하게 틀림)에서 MSE의 gradient는 −0.0049로 거의 0이다. 가장 많이 고쳐야 할 때 가장 약하게 밀어 준다. CE의 gradient는 −0.9975로 크다. 정리하면:

- **gradient 모양**: CE + softmax/sigmoid의 gradient는 `q − p`(예측 − 정답)로 깔끔하고 포화하지 않는다.
- **확률적 의미**: CE는 categorical/Bernoulli 분포의 NLL이다. 즉 "출력이 확률"이라는 가정에 맞는 loss다.
- **벌점 크기**: MSE는 오차가 최대 1로 제한되지만 CE는 확신한 오답에 무한히 큰 벌을 준다.

### 7.7 회귀의 MSE = Gaussian likelihood

반대로 회귀(예: 센서 보정값, 거리 추정)에서 오차가 `N(0, σ²)`이라고 가정하면:

```
−log p(y | ŷ) = (y − ŷ)² / (2σ²) + log(σ√(2π))
```

σ가 고정이면 두 번째 항은 상수이고 첫 항은 MSE에 상수배다. 그래서 **MSE 최소화 = Gaussian noise 가정 하의 MLE**다.

숫자로 보면: 정답 `[1, 2, 3, 4]`, 예측 A = `[1.1, 1.9, 3.2, 3.8]`, σ = 0.5일 때 MSE = (0.01 + 0.01 + 0.04 + 0.04)/4 = 0.025이고, Gaussian NLL에서 상수 `log(0.5·√(2π)) ≈ 0.2258`을 빼면 0.025 / (2 × 0.25) = 0.05다. 예측이 바뀌어도 "NLL − 상수 = MSE / (2σ²)" 관계는 그대로라서 두 loss의 최솟값 위치가 같다. 비슷하게 오차가 Laplace 분포라고 가정하면 MAE(L1) loss가 나온다. 이상치가 많은 센서라면 L1이나 Huber loss가 더 튼튼한 이유다.

---

## 8. Entropy와 KL divergence — 불확실성과 분포 간 거리

### 8.1 Entropy

```
H(p) = −∑ₖ pₖ log₂ pₖ        [bit]
```

말로 하면: 분포에서 뽑은 값을 전달하는 데 평균적으로 필요한 bit 수, 즉 **불확실성의 양**이다. 4개 값이 균등하면 2 bit(주소선 2개), 하나가 거의 확실하면 0에 가깝다. ML에서는 ln(자연로그, 단위 nat)을 주로 쓴다. 둘은 상수배(ln 2) 차이일 뿐이다.

### 8.2 KL divergence와 cross-entropy의 관계

```
KL(P ‖ Q) = ∑ₖ pₖ log (pₖ / qₖ)   ≥ 0,  P = Q일 때만 0
H(P, Q)   = H(P) + KL(P ‖ Q)
```

말로 하면: KL은 "진짜 분포가 P인데 Q라고 믿고 부호화하면 추가로 낭비되는 bit 수"다. 거리처럼 쓰지만 **대칭이 아니다**(KL(P‖Q) ≠ KL(Q‖P)). 분류 학습에서 P(정답)는 고정이므로 H(P)는 상수이고, cross-entropy를 최소화하는 것 = KL을 최소화하는 것이다.

손계산: P = [0.7, 0.2, 0.1], Q = [0.6, 0.3, 0.1]

```
KL(P‖Q) = 0.7·log₂(0.7/0.6) + 0.2·log₂(0.2/0.3) + 0.1·log₂(1)
        = 0.7·0.2224 + 0.2·(−0.5850) + 0 = 0.1557 − 0.1170 = 0.0387 bit
```

```python
import numpy as np

def entropy(p):                 # 단위: bit (log2)
    p = p[p > 0]
    return float(-(p * np.log2(p)).sum()) + 0.0   # +0.0: -0.0 표시 방지

def kl(p, q):                   # KL(P‖Q), bit
    m = p > 0
    return (p[m] * np.log2(p[m] / q[m])).sum()

uniform = np.array([0.25, 0.25, 0.25, 0.25])
peaked  = np.array([0.97, 0.01, 0.01, 0.01])
onehot  = np.array([1.0, 0.0, 0.0, 0.0])
for n, p in (("uniform", uniform), ("peaked", peaked), ("one-hot", onehot)):
    print(f"H({n:7s}) = {entropy(p):.4f} bit")

P = np.array([0.7, 0.2, 0.1])   # FP32 activation 히스토그램(정규화)이라 치자
Q = np.array([0.6, 0.3, 0.1])   # 양자화 후 히스토그램
print("KL(P||Q) =", round(kl(P, Q), 4), " KL(Q||P) =", round(kl(Q, P), 4))
print("cross-entropy H(P,Q) =", round(-(P*np.log2(Q)).sum(), 4),
      " = H(P)+KL =", round(entropy(P) + kl(P, Q), 4))
```

```text
H(uniform) = 2.0000 bit
H(peaked ) = 0.2419 bit
H(one-hot) = 0.0000 bit
KL(P||Q) = 0.0387  KL(Q||P) = 0.0421
cross-entropy H(P,Q) = 1.1955  = H(P)+KL = 1.1955
```

출력에서 볼 것: 균등 분포 2 bit, 뾰족한 분포 0.24 bit, one-hot 0 bit. KL은 방향에 따라 0.0387 vs 0.0421로 다르다. `H(P, Q) = H(P) + KL`이 수치로 성립한다.

### 8.3 어디서 다시 나오는가

- **양자화 calibration (C1)**: FP32 activation 히스토그램 P와, 어떤 clipping 범위로 양자화했을 때의 히스토그램 Q 사이의 KL을 최소화하는 범위를 고르는 방식이 있다(NVIDIA TensorRT의 entropy calibrator로 유명해진 방법). max 값 하나에 끌려가는 min-max보다 이상치에 강하다.
- **Knowledge distillation (C5)**: student의 softmax 출력이 teacher의 (온도를 높인) softmax 출력에 가까워지도록 KL을 loss로 쓴다.

---

## 9. Bayes 규칙 — wake word 오작동의 산수

### 9.1 조건부 확률과 Bayes

```
P(A | B) = P(A ∩ B) / P(B)
Bayes:  P(W | T) = P(T | W) · P(W) / P(T)
        P(T)     = P(T | W)·P(W) + P(T | ¬W)·P(¬W)     (전확률)
```

W = "이 윈도우에 wake word가 있다", T = "검출기가 trigger했다".

- `P(T | W)` = **recall**(= 1 − FRR, false reject rate) — wake word가 있을 때 잡는 비율
- `P(T | ¬W)` = **윈도우당 false positive rate** — 조용할 때 잘못 잡는 비율
- `P(W)` = **prior(사전확률, base rate)** — 윈도우 중 실제로 wake word가 있는 비율
- `P(W | T)` = **posterior** = precision — trigger가 났을 때 진짜일 확률

말로 하면: 검출기가 아무리 좋아 보여도, 찾는 사건 자체가 극도로 드물면(prior가 작으면) trigger 대부분이 가짜가 된다. 의료 검사의 "base rate fallacy"와 같은 구조다.

### 9.2 구체적으로 계산

가정(설명용): 1초 윈도우를 100 ms hop으로 매번 판정, recall 99%, 윈도우당 false positive 1%, 사용자는 하루 10번 wake word를 말한다(한 번 = 윈도우 1개로 단순화).

```
윈도우/시간 = 3600 / 0.1 = 36,000
false wakes/시간 = 0.01 × 36,000 ≈ 360     ← 10초에 한 번 기기가 깨어남
진짜 wake/시간 = 0.99 × 10/24 ≈ 0.41
P(W | T) = 0.41 / (0.41 + 360) ≈ 0.11%    ← trigger 1000번 중 1번만 진짜
```

```python
hop_s = 0.1                               # 100 ms마다 1초 윈도우를 한 번 판정
windows_per_hour = 3600 / hop_s           # 36,000
recall = 0.99                             # P(trigger | wake word)
fpr    = 0.01                             # P(trigger | no wake word), 윈도우당
ww_per_day = 10                           # 사용자가 하루 10번 부른다고 가정

pos_per_hour = ww_per_day / 24            # wake word가 든 윈도우 (1회 = 1윈도우로 단순화)
prior = pos_per_hour / windows_per_hour   # P(wake word) per window
print(f"windows/hour          = {windows_per_hour:,.0f}")
print(f"prior P(W)            = {prior:.3e}")
print(f"false wakes / hour    = {fpr * windows_per_hour * (1 - prior):,.1f}")
print(f"true wakes / hour     = {recall * pos_per_hour:.3f}")
p_trig = recall * prior + fpr * (1 - prior)
post = recall * prior / p_trig            # Bayes: P(W | trigger)
print(f"P(W | trigger)        = {post:.4%}")

target_fa_per_day = 1                     # 목표: 하루 1회 이하 오작동
fpr_needed = target_fa_per_day / (windows_per_hour * 24)
print(f"FPR needed per window = {fpr_needed:.2e}  (1 FA / 24 h)")
# 2단 cascade (MCU 1단 → DSP 2단), 두 단의 오류가 독립이라고 가정
fpr1, fpr2 = 1e-3, 1e-3
print(f"cascade FA / day      = {fpr1 * fpr2 * windows_per_hour * 24:.3f}")
```

```text
windows/hour          = 36,000
prior P(W)            = 1.157e-05
false wakes / hour    = 360.0
true wakes / hour     = 0.413
P(W | trigger)        = 0.1145%
FPR needed per window = 1.16e-06  (1 FA / 24 h)
cascade FA / day      = 0.864
```

출력에서 볼 것: "99% 정확한 검출기"가 시간당 360번 오작동한다. 하루 1회 이하로 줄이려면 윈도우당 FPR이 **1.16 × 10⁻⁶** 이어야 한다. 1%보다 약 8600배 엄격하다. 2단 cascade(각 단 FPR 10⁻³)를 쓰고 두 단의 오류가 독립이라고 가정하면 하루 0.86회까지 내려간다. 실제로는 두 단이 비슷한 소리에 같이 속으므로 독립 가정은 낙관적이다.

```
 36,000 윈도우/시간
      │
      ├── wake word 있음 (≈0.4) ──► 99% trigger ──► 진짜 wake ≈ 0.41
      │
      └── 없음 (≈36,000) ────────► 1% trigger ──► false wake ≈ 360   ← 여기가 문제
```

### 9.3 그래서 업계는 이렇게 말한다

- wake word 성능은 "정확도 %"가 아니라 **FRR(%) @ FA/hour** 쌍으로 말한다. 예: "FRR 5% at 1 false accept per 10 hours". A4의 DET/ROC 곡선에서 threshold를 옮기며 이 둘을 맞바꾼다.
- 겹치는 윈도우는 하나의 발화에서 연속으로 trigger가 나므로, 실제 시스템은 posterior를 여러 프레임에 걸쳐 평균(smoothing)하고, trigger 후 일정 시간 무시(refractory, debounce)해서 "이벤트 단위"로 센다. 위 계산은 윈도우를 독립으로 본 최악 근사다.
- 전력 관점: false wake는 곧 DSP/SoC를 깨우는 비용이다. always-on MCU 단계에서 거르면 뒤 단계가 깨어나는 횟수가 줄어든다. cascade의 이유가 정확도와 전력 둘 다다(E2, K3).

---

## 10. Temperature와 categorical 샘플링

### 10.1 온도 T

```
p = softmax(z / T)
```

말로 하면: logits를 T로 나눈 뒤 softmax한다. T < 1이면 logit 차이가 커져 분포가 뾰족해지고(더 확신), T > 1이면 차이가 줄어 평평해진다(더 무작위). T → 0이면 argmax(greedy), T → ∞면 균등 분포다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 300">
<rect x="80.5" y="80.0" width="32" height="160.0" fill="#4a7bd0"/> <text x="96.5" y="76.0" font-size="12" text-anchor="middle">0.84</text> <rect x="116.5" y="124.2" width="32" height="115.8" fill="#e08a3c"/> <text x="132.5" y="120.2" font-size="12" text-anchor="middle">0.61</text> <rect x="152.5" y="157.5" width="32" height="82.5" fill="#3f9a6b"/> <text x="168.5" y="153.5" font-size="12" text-anchor="middle">0.43</text>
<text x="132.5" y="260" font-size="13" text-anchor="middle">A (logit 2)</text> <rect x="225.5" y="218.3" width="32" height="21.7" fill="#4a7bd0"/> <text x="241.5" y="214.3" font-size="12" text-anchor="middle">0.11</text> <rect x="261.5" y="197.4" width="32" height="42.6" fill="#e08a3c"/> <text x="277.5" y="193.4" font-size="12" text-anchor="middle">0.22</text> <rect x="297.5" y="189.9" width="32" height="50.1" fill="#3f9a6b"/>
<text x="313.5" y="185.9" font-size="12" text-anchor="middle">0.26</text> <text x="277.5" y="260" font-size="13" text-anchor="middle">B (logit 1)</text> <rect x="370.5" y="232.0" width="32" height="8.0" fill="#4a7bd0"/> <text x="386.5" y="228.0" font-size="12" text-anchor="middle">0.04</text> <rect x="406.5" y="214.2" width="32" height="25.8" fill="#e08a3c"/> <text x="422.5" y="210.2" font-size="12" text-anchor="middle">0.14</text>
<rect x="442.5" y="201.0" width="32" height="39.0" fill="#3f9a6b"/> <text x="458.5" y="197.0" font-size="12" text-anchor="middle">0.21</text> <text x="422.5" y="260" font-size="13" text-anchor="middle">C (logit 0.5)</text> <rect x="515.5" y="239.6" width="32" height="0.4" fill="#4a7bd0"/> <text x="531.5" y="235.6" font-size="12" text-anchor="middle">0.00</text> <rect x="551.5" y="234.2" width="32" height="5.8" fill="#e08a3c"/>
<text x="567.5" y="230.2" font-size="12" text-anchor="middle">0.03</text> <rect x="587.5" y="221.6" width="32" height="18.4" fill="#3f9a6b"/> <text x="603.5" y="217.6" font-size="12" text-anchor="middle">0.10</text> <text x="567.5" y="260" font-size="13" text-anchor="middle">D (logit -1)</text> <line x1="60" y1="240" x2="640" y2="240" stroke="currentColor"/> <line x1="60" y1="240" x2="60" y2="40" stroke="currentColor"/>
<text x="52" y="244.0" font-size="12" text-anchor="end">0</text> <text x="52" y="149.0" font-size="12" text-anchor="end">0.5</text> <text x="52" y="54.0" font-size="12" text-anchor="end">1.0</text> <rect x="80" y="276" width="14" height="14" fill="#4a7bd0"/> <text x="100" y="288" font-size="12">T = 0.5</text> <rect x="230" y="276" width="14" height="14" fill="#e08a3c"/>
<text x="250" y="288" font-size="12">T = 1.0</text> <rect x="380" y="276" width="14" height="14" fill="#3f9a6b"/> <text x="400" y="288" font-size="12">T = 2.0</text> <text x="60" y="24" font-size="13">softmax(z / T): T가 작을수록 뾰족, 클수록 평평</text>
</svg>
```

그림 4 — logits [2, 1, 0.5, −1]에 온도 0.5 / 1 / 2를 적용한 softmax 확률. T = 0.5에서 A가 0.84를 가져가고, T = 2에서는 A가 0.43으로 내려가며 나머지가 올라온다.

### 10.2 샘플링

categorical 분포에서 샘플을 뽑는다는 것은 [0, 1)의 uniform 난수 u를 뽑고, 누적 확률(CDF)에서 u가 처음 넘는 index를 고르는 것이다. `np.random.Generator.choice(K, p=p)`가 이것을 해 준다.

```
p = [0.609, 0.224, 0.136, 0.030]
CDF = [0.609, 0.833, 0.970, 1.000]
u = 0.70 → 0.609 < 0.70 ≤ 0.833 → index 1 (B)
```

```python
import numpy as np

def softmax(z, T=1.0):
    z = np.asarray(z, dtype=np.float64) / T      # 온도로 나눈 뒤
    e = np.exp(z - z.max())                      # 안정화 softmax
    return e / e.sum()

logits = np.array([2.0, 1.0, 0.5, -1.0])         # 다음 token 후보 4개 (A, B, C, D)
for T in (0.5, 1.0, 2.0):
    print(f"T={T}: p={np.round(softmax(logits, T), 3)}")

rng = np.random.default_rng(42)
for T in (0.5, 1.0, 2.0):
    p = softmax(logits, T)
    draws = rng.choice(4, size=10_000, p=p)       # categorical에서 샘플링
    freq = np.bincount(draws, minlength=4) / 10_000
    print(f"T={T}: sampled freq={freq}  first 12 = {''.join('ABCD'[i] for i in draws[:12])}")
print("greedy (T→0) always picks:", 'ABCD'[int(np.argmax(logits))])
```

```text
T=0.5: p=[0.842 0.114 0.042 0.002]
T=1.0: p=[0.609 0.224 0.136 0.03 ]
T=2.0: p=[0.434 0.263 0.205 0.097]
T=0.5: sampled freq=[0.8448 0.1108 0.042  0.0024]  first 12 = AABAACAAAAAB
T=1.0: sampled freq=[0.5985 0.2343 0.138  0.0292]  first 12 = BBAAAAADAABA
T=2.0: sampled freq=[0.4296 0.2617 0.2145 0.0942]  first 12 = CDAABABBCABC
greedy (T→0) always picks: A
```

출력에서 볼 것: 10,000번 샘플링한 빈도가 softmax 확률과 소수 둘째 자리까지 맞는다. T = 0.5의 첫 12개는 거의 A, T = 2는 C, D까지 섞인다. greedy는 항상 A다.

### 10.3 어디서 다시 나오는가

- **LLM 생성 (B8, L)**: 매 token마다 vocab 크기(수만~십수만)의 logits에 온도를 적용하고 top-k / top-p로 후보를 자른 뒤 샘플링한다. vocab이 크므로 기기에서는 softmax 자체도 무시 못할 비용이 된다(128k 원소 exp).
- **Calibration**: 학습 후 validation set에서 최적 T 하나만 찾아 logits를 나누는 **temperature scaling**은 argmax(정확도)는 그대로 두고 확률의 과신만 고친다.
- **Distillation (C5)**: teacher 출력에 높은 T를 걸어 "틀린 class들 사이의 상대 크기(dark knowledge)"를 student에게 보여 준다.

---

## 11. 임베디드 관점에서 다시 보기

| 주제 | 기기에서의 모습 | 실무 팁 |
|---|---|---|
| 표준화 μ, σ | 상수 배열 또는 모델 첫 레이어 | 모델과 같은 버전으로 생성·배포, golden vector 테스트 |
| softmax | argmax만 필요하면 **생략** | 순서 보존이므로 `argmax(z) = argmax(softmax(z))` |
| threshold | 확률 대신 **logit에 threshold** | `σ(z) > 0.9` ⇔ `z > ln 9 ≈ 2.197`. exp 계산이 사라진다 |
| softmax 연산 비용 | K번 `expf` + 1번 나눗셈 | 작은 K는 무시 가능, LLM vocab(수만 이상)은 무시 못 함 |
| 정수 추론 | INT8 logits → 역양자화 후 softmax 또는 LUT | `z − max`가 항상 ≤ 0이므로 exp LUT의 입력 범위가 고정된다 |
| NaN 전파 | 상태(RNN, IIR)에 NaN이 들어가면 영구 오염 | 입력 검증, `isfinite` 체크, 상태 reset 경로 |
| 오작동률 | FPR × 윈도우/시간 | hop 크기를 바꾸면 FA/hour도 바뀐다 (윈도우 수에 비례) |

마지막 줄은 놓치기 쉽다. 전력을 아끼려고 hop을 100 ms에서 200 ms로 늘리면 윈도우 수가 절반이 되어 FA/hour도 대략 절반이 되지만, 짧은 wake word를 놓칠(recall 하락) 수 있다. 반대로 latency를 줄이려고 hop을 줄이면 같은 모델이라도 FA/hour가 늘어난다. **모델 지표(윈도우당 FPR)와 제품 지표(FA/hour)를 잇는 식에 시스템 파라미터(hop)가 들어 있다**는 것이 이 직군에서 쓰는 사고방식이다.

INT8 softmax에 대해 조금 더: 양자화된 모델에서 logits는 `z = scale × (q − zero_point)`로 복원된다. `q_max`를 빼면 `z − z_max = scale × (q − q_max)`이고 `q − q_max`는 −255 ~ 0 범위의 정수이므로, `exp(scale × k)`를 k = 0 … −255에 대해 미리 표로 만들어 두면 `expf` 호출 없이 softmax를 계산할 수 있다. CMSIS-NN 같은 라이브러리의 정수 softmax 구현들도 max를 빼는 같은 아이디어 위에 고정소수점 exp 근사를 얹은 구조다(F2, C1).

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 기기에서 μ, σ를 다시 계산 | 같은 입력인데 PC와 기기 출력이 다름 | 윈도우 몇 개의 통계는 학습 통계와 다름 | 학습 μ, σ를 상수로 export |
| 모델 재학습 후 μ, σ 헤더 미갱신 | 에러 없이 정확도만 하락 | 전처리 상수와 가중치 버전 불일치 | 모델 파일에 전처리 포함 또는 빌드 시 자동 생성 |
| naive softmax | NaN 출력, trigger가 영원히 안 남 | logit > 88.7에서 `expf` overflow | max 빼기 |
| `CrossEntropyLoss`에 softmax 출력을 넣음 | loss가 잘 안 줄고 값이 이상함 (0.7219 → 0.8684) | softmax 두 번 | logits를 그대로 넣기 |
| 분류에 MSE 사용 | 확신한 오답이 잘 안 고쳐짐 | 포화 구간 gradient ≈ 0 | CE 사용 |
| 윈도우당 FPR만 보고 출시 판단 | 필드에서 시간당 수백 번 오작동 | base rate 무시, 윈도우 수 곱하기 누락 | FA/hour로 환산, 긴 negative 데이터로 측정 |
| val/test 포함해서 μ, σ 계산 | 오프라인 점수는 좋고 필드는 나쁨 | data leakage | train split에서만 계산 |
| 다중 라벨에 softmax | 동시에 참인 class 확률이 서로 깎임 | softmax는 합이 1 | class별 sigmoid |
| 확률 0.9를 "90% 맞음"으로 해석 | threshold 설계가 빗나감 | 모델 과신 | temperature scaling, reliability diagram 확인 |

---

## 13. 면접에서 이렇게 말한다

**Q.** "Why do we subtract the max before computing softmax?"

**A.** softmax는 모든 logit에 같은 상수를 더해도 결과가 같다. max를 빼면 가장 큰 지수 항이 e⁰ = 1이 되어 float32에서 logit이 약 88.7을 넘을 때 생기는 overflow(inf → NaN)를 막고, 분모가 최소 1이 되어 0 나누기도 없어진다. 결과는 수학적으로 동일하다.

> Softmax is shift-invariant, so subtracting the max doesn't change the result, but it makes the largest exponent exactly e to the zero. Without it, a float32 logit above about 88.7 overflows to infinity and you get NaNs. In firmware I'd implement it as three passes: find the max, accumulate exp of the shifted values, then multiply by the reciprocal of the sum.

**Q.** "Why cross-entropy instead of MSE for classification?"

**A.** 세 가지다. CE는 categorical 분포의 negative log-likelihood라서 확률 출력에 맞는 loss이고, softmax와 합치면 gradient가 `예측 − 정답`으로 깔끔하며, 확신한 오답에 큰 gradient를 준다. sigmoid + MSE는 포화 구간에서 gradient가 거의 0이라 가장 틀린 샘플을 가장 느리게 고친다(z = −6에서 MSE gradient 0.005 vs CE 0.998).

> Cross-entropy is the negative log-likelihood of a categorical distribution, so it's the natural loss when the output is a probability. Combined with softmax, the gradient with respect to the logits is just predicted minus target, which doesn't saturate. With MSE on a sigmoid, a confidently wrong prediction sits in the flat region and gets almost no gradient. MSE is still right for regression, because it's the maximum-likelihood loss under Gaussian noise.

**Q.** "What does temperature do in sampling?"

**A.** logits를 T로 나눈 뒤 softmax한다. T < 1이면 분포가 뾰족해져 greedy에 가까워지고, T > 1이면 평평해져 다양한 출력이 나온다. argmax는 바뀌지 않는다. 같은 원리가 calibration(temperature scaling)과 distillation에도 쓰인다.

> Temperature divides the logits before the softmax. Below one it sharpens the distribution toward greedy decoding; above one it flattens it and increases diversity. It never changes the argmax. The same knob is used for calibration, where you fit a single temperature on validation data, and in distillation to expose the teacher's relative scores on wrong classes.

**Q.** "Explain false accept rate per hour for a wake word detector."

**A.** 모델 지표는 윈도우당 FPR인데, 제품 지표는 시간당 오작동 횟수다. 둘은 윈도우 수로 이어진다. 100 ms hop이면 시간당 36,000 윈도우이므로 FPR 1%는 시간당 360번 오작동이다. wake word는 매우 드문 사건이라 Bayes로 보면 trigger의 0.1%만 진짜다. 하루 1회 목표라면 윈도우당 FPR이 약 10⁻⁶이어야 하고, 그래서 cascade와 posterior smoothing, 수백 시간의 negative 데이터 평가가 필요하다.

> The model gives you a per-window false positive rate, but the product spec is false accepts per hour, and the bridge is the number of windows. With a 100 ms hop that's 36,000 windows an hour, so a 1% false positive rate means 360 false wakes an hour, even with 99% recall. Because the prior is tiny, Bayes says only about 0.1% of triggers are real. To get to one false accept per day you need roughly one in a million per window, which is why we use cascades, posterior smoothing, and evaluate on hundreds of hours of negative audio. Wake word performance is reported as FRR at a given FA per hour.

**Q.** "Your model works in Python but gives different results on the device. Where do you look first?"

**A.** 전처리 통계부터 본다. 학습 때 쓴 μ, σ(또는 min/max), 샘플레이트, 단위, 축 순서가 기기와 같은지, 기기에서 통계를 다시 계산하고 있지 않은지 확인한다. 같은 입력을 Python과 C에 넣어 전처리 출력, logits를 단계별로 diff한다. 그 다음이 양자화와 연산자 차이다.

> First I'd check preprocessing, because it fails silently: the same mean and standard deviation from the training set, same units, sample rate and axis order. Then I'd feed an identical golden input to both the Python pipeline and the firmware and diff the features and the logits layer by layer. Only after that would I look at quantization or kernel differences.

---

## 14. 직접 해보기

1. 손계산: 샘플 `[4, 6, 5, 9]`의 평균, 분산(ddof=0), 표준편차를 구하라.
정답: μ = 6, 편차² = 4, 0, 1, 9 → 합 14 → σ² = 3.5, σ ≈ 1.871.

2. 손계산: logits `[1, 1, 1]`과 `[3, 1, 1]`의 softmax를 구하라. 정답이 첫 class일 때 각각의 cross-entropy는?
정답: `[1/3, 1/3, 1/3]` → CE = ln 3 ≈ 1.099. `[3,1,1]` → e²/(e² + 2) = 7.389/9.389 ≈ 0.787 → CE ≈ 0.240.

3. 손계산: sigmoid 출력이 0.95 이상일 때 trigger하는 규칙을 logit threshold로 바꿔라.
정답: z > ln(0.95/0.05) = ln 19 ≈ 2.944.

4. 계산: hop 50 ms, 윈도우당 FPR 10⁻⁵이면 FA/hour는? 목표가 "10시간에 1번"이면 필요한 FPR은?
정답: 72,000 × 10⁻⁵ = 0.72 FA/hour. 목표 0.1 FA/hour → FPR ≈ 1.39 × 10⁻⁶.

5. 코드 과제: `softmax_stable`의 C 구현을 INT8 logits(`int8_t q[]`, `float scale`)를 받도록 바꾸고, `exp(scale × k)` (k = −255 … 0) LUT를 미리 계산해서 `expf` 호출 없이 계산하라. float 버전과 최대 오차를 비교하라.
힌트: `k = q[i] − q_max`는 항상 −255 ~ 0이다. LUT는 256개 float(1 KB)이며, 필요하면 Q15 고정소수점 표로 바꿔 절반으로 줄인다.

6. 코드 과제: 10.2절 샘플링 코드에서 `rng.choice` 대신 CDF와 `np.searchsorted`로 직접 categorical 샘플러를 구현하고 빈도가 같은지 확인하라.
힌트: `cdf = np.cumsum(p)`, `idx = np.searchsorted(cdf, rng.random(n), side="right")`. 부동소수점 때문에 `cdf[-1]`이 1보다 살짝 작을 수 있으니 1.0으로 강제한다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| random variable | 확률변수 | 측정할 때마다 값이 달라지는 수 |
| PMF / PDF | 확률질량함수 / 확률밀도함수 | 이산 값의 확률 / 연속 값의 밀도(면적이 확률) |
| expectation (μ) | 기댓값 | 확률 가중 평균, 센서로는 DC 성분 |
| variance (σ²), std (σ) | 분산, 표준편차 | 평균 주변 흩어짐, 센서로는 noise RMS |
| Bernoulli / categorical | 이진 / 다중 범주 분포 | 이진·다중 분류 출력의 분포 |
| Gaussian (normal) | 정규분포 | μ, σ로 결정되는 종 모양, 68-95-99.7 |
| CLT | 중심극한정리 | 독립 요인의 합은 Gaussian에 가까워진다 |
| covariance / correlation | 공분산 / 상관계수 | 두 변수가 같이 움직이는 정도, 상관은 −1 ~ 1 |
| logit | 로짓 | softmax/sigmoid 이전의 실수 점수, log-odds |
| log-sum-exp | | `m + log ∑ e^(z − m)`, 안정한 log 정규화 |
| likelihood | 우도 | 데이터를 고정하고 파라미터의 함수로 본 확률 |
| MLE | 최대 우도 추정 | likelihood가 최대인 파라미터 |
| cross-entropy | 교차 엔트로피 | −∑ p log q, 분류 loss |
| entropy | 엔트로피 | 분포의 불확실성(bit 또는 nat) |
| KL divergence | 쿨백-라이블러 발산 | 분포 간 비대칭 "거리", calibration·distillation에 사용 |
| prior / posterior | 사전 / 사후 확률 | 관측 전 / 관측 후의 믿음 |
| FAR, FA/hour | false accept rate | wake word 오작동 빈도 |
| temperature | 온도 | softmax 전에 logits를 나누는 값, 분포의 뾰족함 조절 |

---

## 16. 요약 & 체크리스트

센서 noise는 평균(offset, 중력)과 표준편차(noise RMS)로 요약되고, 여러 작은 원인의 합이라 Gaussian으로 잘 근사된다. 모델 입력은 **학습 데이터의** μ, σ로 표준화하며 그 숫자는 펌웨어에 그대로 들어가야 한다. 모델 출력 logits는 sigmoid/softmax로 확률이 되고, softmax는 max를 빼야 float에서 안전하다. 학습 loss는 negative log-likelihood이며 분류에서는 cross-entropy, Gaussian 회귀에서는 MSE가 된다. PyTorch `nn.CrossEntropyLoss`는 logits를 받는다. entropy와 KL은 양자화 calibration과 distillation에서 다시 쓰인다. wake word처럼 드문 사건은 Bayes로 보면 윈도우당 작은 FPR도 시간당 수백 번의 오작동이 되므로 FA/hour로 환산해서 판단한다. temperature는 분포의 뾰족함을 조절하며 LLM 샘플링의 기본 손잡이다.

- [ ] 샘플 4개의 평균·분산·표준편차를 손으로 계산하고 ddof 차이를 설명할 수 있다
- [ ] 68-95-99.7 규칙으로 "100 Hz 스트림에서 3σ 밖 샘플이 몇 초마다 나오는지" 계산할 수 있다
- [ ] 양자화 오차 분산이 Δ²/12인 이유를 uniform 분포로 설명할 수 있다
- [ ] z-score 전처리를 C로 구현하고, 왜 학습 μ, σ를 박아야 하는지 설명할 수 있다
- [ ] 3-class softmax를 손으로 계산하고, max 빼기가 결과를 안 바꾸는 이유를 증명할 수 있다
- [ ] 안정 softmax를 C로 작성하고 float32 overflow 경계(≈ 88.7)를 말할 수 있다
- [ ] 4개 샘플의 categorical cross-entropy를 손으로 계산하고 PyTorch 값과 맞출 수 있다
- [ ] 분류에 CE, 회귀에 MSE를 쓰는 이유를 likelihood로 설명할 수 있다
- [ ] 윈도우당 FPR, hop, prior로 FA/hour와 P(wake | trigger)를 계산할 수 있다
- [ ] temperature가 softmax 분포와 샘플링 결과를 어떻게 바꾸는지 숫자로 보여 줄 수 있다

## 참고 자료

- Goodfellow, Bengio, Courville, "Deep Learning" (MIT Press, 2016), 3장 Probability and Information Theory, 5.5절 Maximum Likelihood Estimation — [deeplearningbook.org](https://www.deeplearningbook.org/)
- Dive into Deep Learning — softmax regression, 정보 이론 부록 — [d2l.ai](https://d2l.ai/)
- Christopher Bishop, "Pattern Recognition and Machine Learning" (2006), 1–2장 확률분포
- PyTorch 문서: [torch.nn.CrossEntropyLoss](https://pytorch.org/docs/stable/generated/torch.nn.CrossEntropyLoss.html), [torch.nn.BCEWithLogitsLoss](https://pytorch.org/docs/stable/generated/torch.nn.BCEWithLogitsLoss.html)
- NumPy 문서: [Random Generator](https://numpy.org/doc/stable/reference/random/generator.html)
- 3Blue1Brown — Bayes theorem, central limit theorem 영상 — [3blue1brown.com](https://www.3blue1brown.com/)
- Andrej Karpathy, "Neural Networks: Zero to Hero" — makemore 편에서 NLL·softmax를 손으로 구현
- Chen, Parada, Heigold, "Small-footprint keyword spotting using deep neural networks", ICASSP 2014 — posterior smoothing, FA/hour 평가
- Guo, Pleiss, Sun, Weinberger, "On Calibration of Modern Neural Networks", ICML 2017 — temperature scaling
- Szymon Migacz, "8-bit Inference with TensorRT", GTC 2017 — KL divergence 기반 calibration
