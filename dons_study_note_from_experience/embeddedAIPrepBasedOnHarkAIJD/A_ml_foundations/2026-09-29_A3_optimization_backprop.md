# A3. 최적화와 역전파 — 경사하강법이 모델을 학습시키는 원리

> **이 노트를 다 읽으면**: 경사하강법 한 step을 손으로 계산할 수 있다 · 작은 2층 네트워크의 backprop을 숫자로 끝까지 따라가고 수치 미분·PyTorch로 검증할 수 있다 · SGD/momentum/Adam과 learning rate schedule을 설명할 수 있다 · "학습이 추론보다 메모리를 훨씬 많이 먹는 이유"를 바이트 단위로 말할 수 있다
> **JD 연결**: "5 yrs ML engineering" 요건을 경력 대신 증명해야 하는 부분, 그리고 "Optimizing models for MCUs & edge processors"의 전제 지식 — study_prep_list A3 행: 미분·chain rule, backprop 계산 그래프 / SGD, momentum, Adam, learning rate schedule / vanishing·exploding gradient
> **Don 기준 난이도**: 미분·편미분·chain rule은 Berkeley에서 배운 것(복습) / "그 미분을 수백만 개 파라미터에 기계적으로 적용하는 방법(backprop)"과 optimizer의 실무 감각은 새로 배움
> **선행 노트**: A0, A1, A2

---

## 0. 큰 그림 — 이게 왜 필요한가

edge ML 엔지니어는 대부분 **추론(inference)** 을 기기에 올리는 일을 한다. 그런데 왜 학습(training)의 원리를 알아야 할까? 이유는 세 가지다.

- **양자화(QAT)와 fine-tuning을 이해하려면**: C2에서 다룰 quantization-aware training은 "학습 도중에 양자화 효과를 흉내 내는" 기법이다. backprop이 어떻게 흐르는지 모르면 fake quant나 STE(straight-through estimator)가 왜 필요한지 설명할 수 없다.
- **모델 팀과 대화하려면**: "이 레이어를 INT8로 바꾸면 정확도가 떨어져요" → "그럼 QAT를 몇 epoch 돌려 보죠", "learning rate를 낮춰서 fine-tuning 하죠" 같은 대화가 매일 나온다.
- **"모델을 직접 학습해 봤다"를 증명하려면**: Don은 ML 실무 경력이 없다. 면접에서 backprop을 1분 안에 숫자로 설명하고, 직접 짠 학습 루프를 보여 주는 것이 가장 빠른 신뢰 확보 방법이다.

학습이란 결국 아래 루프다. 펌웨어의 **폐루프 제어(closed-loop control)** 와 똑같은 모양이다.

```
        ┌──────────────────────────────────────────────────────────┐
        │                                                          │
 data ──►  forward: ŷ = f(x; W)  ──► loss L(ŷ, y) ──► backward: ∂L/∂W │
 (x, y) │     (추론과 같은 계산)        (오차 측정)       (각 W의 민감도)   │
        │                                                  │       │
        │           update: W ← W − η · ∂L/∂W  ◄───────────┘       │
        │           (η = learning rate = 루프 gain)                  │
        └──────────── 반복 (수천~수백만 step) ─────────────────────────┘

 제어 루프 대응:  setpoint−측정값 = error  ↔  loss
                 plant 입력 조정       ↔  weight update
                 controller gain Kp    ↔  learning rate η
```

말로 하면: 모델에 데이터를 넣어 답을 내 보고(forward), 정답과 얼마나 틀렸는지 재고(loss), "각 weight를 어느 방향으로 얼마나 움직이면 틀린 정도가 줄어드는지"를 계산해서(backward), 조금 움직인다(update). 이걸 수없이 반복한다.

이 노트의 순서는 이렇다: 미분 직관(1절) → 경사하강법(2절) → loss 지형(3절) → chain rule과 계산 그래프(4절) → 2층 네트워크 손 backprop(5절) → numpy로 학습 루프(6절) → optimizer와 schedule(7절) → gradient 소실·폭주(8절) → 학습 메모리 비용과 QAT 예고(9절).

---

## 1. 미분 직관 — "살짝 건드리면 얼마나 변하나"

### 1.1 기울기 = 민감도

펌웨어에서 전압 레귤레이터 출력을 튜닝할 때 "trim 코드를 1 LSB 올리면 출력이 몇 mV 변하지?"를 먼저 잰다. 그게 바로 **미분(derivative)** 이다.

```
 f′(w) = lim_{ε→0} ( f(w+ε) − f(w) ) / ε

 실용 버전:  f(w+ε) ≈ f(w) + f′(w) · ε      (ε가 작을 때)
```

말로 하면: w를 ε만큼 살짝 건드리면 f는 대략 "기울기 × ε"만큼 변한다. 기울기가 −4면, w를 0.01 올릴 때 f는 약 0.04 **줄어든다**.

**손계산 예제.** `L(w) = (w − 3)²`, `w = 1`에서:

- 해석적 미분: `L′(w) = 2(w − 3) = 2 · (1 − 3) = −4`
- 확인: `L(1.01) = (−1.99)² = 3.9601`, `L(1) = 4`. 차이 −0.0399 ≈ −4 × 0.01 = −0.04. 맞다.
- 부호의 의미: 기울기가 음수 → w를 **늘리면** loss가 줄어든다 → 최소점(w=3)은 오른쪽에 있다.

### 1.2 편미분 — 변수가 여러 개일 때

모델에는 weight가 수천~수십억 개 있다. **편미분(partial derivative)** `∂f/∂x`는 "다른 변수는 모두 고정하고 x 하나만 살짝 건드렸을 때의 기울기"다. 레지스터 필드 여러 개 중 하나만 바꿔 가며 측정하는 것과 같다.

```
 f(x, y) = x² + 3xy

 ∂f/∂x = 2x + 3y      (y를 상수 취급)
 ∂f/∂y = 3x           (x를 상수 취급)

 (x, y) = (2, 1)  →  ∂f/∂x = 4 + 3 = 7,   ∂f/∂y = 6
```

### 1.3 gradient = 편미분을 모은 벡터 = 오르막 방향

모든 편미분을 벡터로 모은 것이 **gradient** `∇f`다.

```
 ∇f(x, y) = ( ∂f/∂x , ∂f/∂y )        예: ∇f(2, 1) = (7, 6)
```

말로 하면: gradient는 "지금 위치에서 f가 **가장 가파르게 증가하는 방향**"을 가리키는 화살표이고, 길이는 그 가파른 정도다. 그래서 loss를 **줄이려면 gradient의 반대 방향(−∇L)** 으로 가면 된다. 이것이 경사하강법의 전부다.

왜 가장 가파른 방향인가? 작은 이동 `Δ = (Δx, Δy)`에 대해 `Δf ≈ ∇f · Δ`(내적)이다. 이동 길이가 같다면 내적은 Δ가 ∇f와 같은 방향일 때 최대(A1의 코사인 유사도 참고), 반대 방향일 때 최소다.

### 1.4 코드로 확인 — 수치 미분

수치 미분(numerical derivative)이 해석적 미분과 맞는지 확인하는 코드다. 이 "직접 건드려 보기"가 뒤에서 backprop을 검증하는 **gradient check**의 기초가 된다.

```python
# 수치 미분으로 "살짝 건드리면 loss가 얼마나 변하나"를 확인
def L(w):
    return (w - 3.0) ** 2

w = 1.0
for eps in [0.1, 0.01, 0.001]:
    dL = L(w + eps) - L(w)
    print(f"eps={eps:<6} ΔL={dL:+.6f}  ΔL/eps={dL/eps:+.4f}")
print("해석적 미분 2(w-3) =", 2 * (w - 3.0))

# 편미분: f(x, y) = x² + 3xy
def f(x, y):
    return x**2 + 3 * x * y
x, y, h = 2.0, 1.0, 1e-5
dfdx = (f(x + h, y) - f(x - h, y)) / (2 * h)
dfdy = (f(x, y + h) - f(x, y - h)) / (2 * h)
print(f"∂f/∂x ≈ {dfdx:.4f} (정답 2x+3y = {2*x+3*y})")
print(f"∂f/∂y ≈ {dfdy:.4f} (정답 3x = {3*x})")
```

```text
eps=0.1    ΔL=-0.390000  ΔL/eps=-3.9000
eps=0.01   ΔL=-0.039900  ΔL/eps=-3.9900
eps=0.001  ΔL=-0.003999  ΔL/eps=-3.9990
해석적 미분 2(w-3) = -4.0
∂f/∂x ≈ 7.0000 (정답 2x+3y = 7.0)
∂f/∂y ≈ 6.0000 (정답 3x = 6.0)
```

출력에서 볼 것: ε를 줄일수록 `ΔL/ε`가 −4에 가까워진다. 편미분은 **중앙 차분** `(f(x+h) − f(x−h)) / 2h`로 계산했는데, 한쪽 차분보다 오차가 훨씬 작다(오차가 h가 아니라 h²에 비례).

### 1.5 함정

- 수치 미분의 h를 너무 작게(예: 1e-12) 잡으면 float 반올림 오차 때문에 오히려 틀린다. float64에서는 1e-5 ~ 1e-7 정도가 적당하다. ADC 분해능보다 작은 변화를 측정하려는 것과 같다.
- 미분은 **국소적(local)** 정보다. 기울기 −4는 "w=1 근처에서만" 유효하다. 멀리 점프하면 선형 근사가 깨진다 → 그래서 learning rate가 너무 크면 안 된다(2절).

---

## 2. 경사하강법 (Gradient Descent)

### 2.1 정의

```
 w ← w − η · ∂L/∂w           (파라미터 1개)
 W ← W − η · ∇L(W)           (파라미터 여러 개, 벡터로 한 번에)
```

말로 하면: 지금 위치의 기울기를 보고, 내리막 방향으로 η(learning rate, 학습률)만큼 비례해서 한 걸음 옮긴다. 기울기가 크면(가파르면) 크게, 작으면(평평하면) 작게 움직인다.

### 2.2 1D 손계산 — 3 step

`L(w) = (w − 3)²`, 시작점 `w₀ = 0`, `η = 0.1`. gradient는 `2(w − 3)`.

| step | w | L(w) = (w−3)² | gradient 2(w−3) | 업데이트 w − 0.1 × grad |
|---|---|---|---|---|
| 0 | 0 | 9.00 | −6.0 | 0 − 0.1×(−6.0) = 0.6 |
| 1 | 0.6 | 5.76 | −4.8 | 0.6 + 0.48 = 1.08 |
| 2 | 1.08 | 3.6864 | −3.84 | 1.08 + 0.384 = 1.464 |
| 3 | 1.464 | 2.3593 | −3.072 | … |

관찰할 점: 최소점(w=3)에 가까워질수록 기울기가 작아지고, 그래서 **걸음도 저절로 작아진다**. 오차 `w − 3`은 매 step `−3 → −2.4 → −1.92 → −1.536`으로 정확히 0.8배씩 줄어든다. 왜 0.8인가는 2.4절에서 본다.

```svg
<svg viewBox="0 0 640 330" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="280" x2="580" y2="280" stroke="currentColor"/><line x1="97.1" y1="280" x2="97.1" y2="20" stroke="currentColor"/>
<polyline points="60.0,30.0 74.9,57.8 89.7,83.9 104.6,108.4 119.4,131.2 134.3,152.4 149.1,172.0 164.0,190.0 178.9,206.3 193.7,221.0 208.6,234.1 223.4,245.5 238.3,255.3 253.1,263.5 268.0,270.0 282.9,274.9 297.7,278.2 312.6,279.8 327.4,279.8 342.3,278.2 357.1,274.9 372.0,270.0 386.9,263.5 401.7,255.3 416.6,245.5 431.4,234.1 446.3,221.0 461.1,206.3 476.0,190.0 490.9,172.0 505.7,152.4 520.6,131.2 535.4,108.4 550.3,83.9 565.1,57.8 580.0,30.0" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<line x1="97.1" y1="280" x2="97.1" y2="285" stroke="currentColor"/><text x="97.1" y="300" font-size="12" text-anchor="middle">0</text><line x1="171.4" y1="280" x2="171.4" y2="285" stroke="currentColor"/><text x="171.4" y="300" font-size="12" text-anchor="middle">1</text>
<line x1="245.7" y1="280" x2="245.7" y2="285" stroke="currentColor"/><text x="245.7" y="300" font-size="12" text-anchor="middle">2</text><line x1="320.0" y1="280" x2="320.0" y2="285" stroke="currentColor"/><text x="320.0" y="300" font-size="12" text-anchor="middle">3</text>
<line x1="394.3" y1="280" x2="394.3" y2="285" stroke="currentColor"/><text x="394.3" y="300" font-size="12" text-anchor="middle">4</text><line x1="468.6" y1="280" x2="468.6" y2="285" stroke="currentColor"/><text x="468.6" y="300" font-size="12" text-anchor="middle">5</text>
<line x1="542.9" y1="280" x2="542.9" y2="285" stroke="currentColor"/><text x="542.9" y="300" font-size="12" text-anchor="middle">6</text><text x="89.1" y="284.0" font-size="12" text-anchor="end">0</text><text x="89.1" y="222.8" font-size="12" text-anchor="end">3</text><text x="89.1" y="161.6" font-size="12" text-anchor="end">6</text>
<text x="89.1" y="100.3" font-size="12" text-anchor="end">9</text><text x="89.1" y="39.1" font-size="12" text-anchor="end">12</text><circle cx="97.1" cy="96.3" r="5" fill="#e08a3c"/><text x="105.1" y="88.3" font-size="12">w0=0, L=9.00</text><line x1="97.1" y1="96.3" x2="141.7" y2="162.4" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="4 3"/>
<circle cx="141.7" cy="162.4" r="5" fill="#e08a3c"/><text x="149.7" y="154.4" font-size="12">w1=0.6, L=5.76</text><line x1="141.7" y1="162.4" x2="177.4" y2="204.8" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="4 3"/><circle cx="177.4" cy="204.8" r="5" fill="#e08a3c"/><text x="185.4" y="196.8" font-size="12">w2=1.08, L=3.69</text>
<line x1="177.4" y1="204.8" x2="205.9" y2="231.9" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="4 3"/><circle cx="205.9" cy="231.9" r="5" fill="#e08a3c"/><text x="213.9" y="223.9" font-size="12">w3=1.464, L=2.36</text><line x1="78.6" y1="65.7" x2="190.0" y2="249.4" stroke="#d0564a" stroke-width="1.5"/><text x="67.4" y="269.8" font-size="12" fill="#d0564a">접선 기울기 −6 (w0에서)</text>
<circle cx="320.0" cy="280.0" r="5" fill="#3f9a6b"/><text x="320.0" y="270.0" font-size="12" text-anchor="middle">최소점 w=3</text><text x="580" y="300" font-size="13" text-anchor="end">w</text><text x="103.1" y="28" font-size="13">L(w) = (w−3)²</text>
</svg>
```

그림 1 — `L(w) = (w−3)²` 위에서 η=0.1 경사하강법 3 step. 빨간 선은 w₀에서의 접선(기울기 −6). 점 사이 간격이 점점 줄어드는 것은 기울기가 작아지기 때문이다.

### 2.3 learning rate — 너무 작게 / 적당히 / 너무 크게

같은 문제를 learning rate 3개로 8 step 돌려 궤적을 출력하는 코드다.

```python
# 1D 경사하강법: L(w) = (w-3)², 학습률 3개 비교
import numpy as np

def run(lr, w0=0.0, steps=8):
    w, ws = w0, [w0]
    for _ in range(steps):
        grad = 2 * (w - 3.0)
        w = w - lr * grad
        ws.append(w)
    return np.array(ws)

for lr in [0.02, 0.3, 1.05]:
    ws = run(lr)
    losses = (ws - 3.0) ** 2
    print(f"lr={lr:<5} w: " + " ".join(f"{v:6.2f}" for v in ws))
    print(f"         L: " + " ".join(f"{v:6.2f}" for v in losses))
```

```text
lr=0.02  w:   0.00   0.12   0.24   0.35   0.45   0.55   0.65   0.75   0.84
         L:   9.00   8.29   7.64   7.04   6.49   5.98   5.51   5.08   4.68
lr=0.3   w:   0.00   1.80   2.52   2.81   2.92   2.97   2.99   3.00   3.00
         L:   9.00   1.44   0.23   0.04   0.01   0.00   0.00   0.00   0.00
lr=1.05  w:   0.00   6.30  -0.63   6.99  -1.39   7.83  -2.31   8.85  -3.43
         L:   9.00  10.89  13.18  15.94  19.29  23.34  28.25  34.18  41.35
```

출력에서 볼 것: `lr=0.02`는 8 step 후에도 w=0.84로 한참 멀다(느림). `lr=0.3`은 5 step 만에 거의 3.00에 도착한다. `lr=1.05`는 w가 6.30 → −0.63 → 6.99 …로 **최소점을 넘어 반대편으로 튀면서 점점 멀어지고**, loss가 9 → 41로 폭증한다(발산, divergence).

```svg
<svg viewBox="0 0 640 330" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="280" x2="600" y2="280" stroke="currentColor"/><line x1="70" y1="280" x2="70" y2="30" stroke="currentColor"/><text x="70.0" y="298" font-size="12" text-anchor="middle">0</text><text x="136.2" y="298" font-size="12" text-anchor="middle">1</text><text x="202.5" y="298" font-size="12" text-anchor="middle">2</text><text x="268.8" y="298" font-size="12" text-anchor="middle">3</text>
<text x="335.0" y="298" font-size="12" text-anchor="middle">4</text><text x="401.2" y="298" font-size="12" text-anchor="middle">5</text><text x="467.5" y="298" font-size="12" text-anchor="middle">6</text><text x="533.8" y="298" font-size="12" text-anchor="middle">7</text><text x="600.0" y="298" font-size="12" text-anchor="middle">8</text>
<text x="62" y="284.0" font-size="12" text-anchor="end">0</text><line x1="70" y1="280.0" x2="600" y2="280.0" stroke="#888" stroke-opacity="0.3"/><text x="62" y="228.4" font-size="12" text-anchor="end">10</text><line x1="70" y1="224.4" x2="600" y2="224.4" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="172.9" font-size="12" text-anchor="end">20</text><line x1="70" y1="168.9" x2="600" y2="168.9" stroke="#888" stroke-opacity="0.3"/><text x="62" y="117.3" font-size="12" text-anchor="end">30</text><line x1="70" y1="113.3" x2="600" y2="113.3" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="61.8" font-size="12" text-anchor="end">40</text><line x1="70" y1="57.8" x2="600" y2="57.8" stroke="#888" stroke-opacity="0.3"/><polyline points="70.0,230.0 136.2,233.9 202.5,237.5 268.8,240.9 335.0,243.9 401.2,246.8 467.5,249.4 533.8,251.8 600.0,254.0" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="70.0" cy="230.0" r="3" fill="#4a7bd0"/>
<circle cx="136.2" cy="233.9" r="3" fill="#4a7bd0"/><circle cx="202.5" cy="237.5" r="3" fill="#4a7bd0"/><circle cx="268.8" cy="240.9" r="3" fill="#4a7bd0"/><circle cx="335.0" cy="243.9" r="3" fill="#4a7bd0"/><circle cx="401.2" cy="246.8" r="3" fill="#4a7bd0"/><circle cx="467.5" cy="249.4" r="3" fill="#4a7bd0"/><circle cx="533.8" cy="251.8" r="3" fill="#4a7bd0"/>
<circle cx="600.0" cy="254.0" r="3" fill="#4a7bd0"/><polyline points="70.0,230.0 136.2,272.0 202.5,278.7 268.8,279.8 335.0,280.0 401.2,280.0 467.5,280.0 533.8,280.0 600.0,280.0" fill="none" stroke="#3f9a6b" stroke-width="2"/><circle cx="70.0" cy="230.0" r="3" fill="#3f9a6b"/><circle cx="136.2" cy="272.0" r="3" fill="#3f9a6b"/><circle cx="202.5" cy="278.7" r="3" fill="#3f9a6b"/>
<circle cx="268.8" cy="279.8" r="3" fill="#3f9a6b"/><circle cx="335.0" cy="280.0" r="3" fill="#3f9a6b"/><circle cx="401.2" cy="280.0" r="3" fill="#3f9a6b"/><circle cx="467.5" cy="280.0" r="3" fill="#3f9a6b"/><circle cx="533.8" cy="280.0" r="3" fill="#3f9a6b"/><circle cx="600.0" cy="280.0" r="3" fill="#3f9a6b"/>
<polyline points="70.0,230.0 136.2,219.5 202.5,206.8 268.8,191.4 335.0,172.8 401.2,150.3 467.5,123.1 533.8,90.1 600.0,50.3" fill="none" stroke="#d0564a" stroke-width="2"/><circle cx="70.0" cy="230.0" r="3" fill="#d0564a"/><circle cx="136.2" cy="219.5" r="3" fill="#d0564a"/><circle cx="202.5" cy="206.8" r="3" fill="#d0564a"/><circle cx="268.8" cy="191.4" r="3" fill="#d0564a"/>
<circle cx="335.0" cy="172.8" r="3" fill="#d0564a"/><circle cx="401.2" cy="150.3" r="3" fill="#d0564a"/><circle cx="467.5" cy="123.1" r="3" fill="#d0564a"/><circle cx="533.8" cy="90.1" r="3" fill="#d0564a"/><circle cx="600.0" cy="50.3" r="3" fill="#d0564a"/><text x="523.8" y="68.9" font-size="12" fill="#d0564a" text-anchor="end">lr=1.05 발산 (41.35)</text>
<text x="595.0" y="246.0" font-size="12" fill="#4a7bd0" text-anchor="end">lr=0.02 너무 느림 (4.68)</text><text x="208.5" y="272.0" font-size="12" fill="#3f9a6b">lr=0.3 빠르게 수렴 (0.00)</text><text x="600" y="314" font-size="13" text-anchor="end">step</text><text x="76" y="34" font-size="13">loss L=(w−3)²</text>
</svg>
```

그림 2 — 위 코드의 실제 loss 값(step 0~8). 파랑 lr=0.02(느림), 초록 lr=0.3(빠른 수렴), 빨강 lr=1.05(발산).

### 2.4 제어 루프 gain과 똑같다 — 안정 조건 유도

2차 함수에서는 경사하강법을 정확히 풀 수 있다. `L(w) = (λ/2)(w − w*)²`라 하면 gradient는 `λ(w − w*)`, 여기서 λ는 곡률(2차 미분)이다. 오차를 `e = w − w*`로 쓰면:

```
 e_{k+1} = e_k − η · λ · e_k = (1 − ηλ) · e_k

 → e_k = (1 − ηλ)^k · e_0
```

말로 하면: 매 step 오차에 `(1 − ηλ)`가 곱해진다. 이건 **이산시간 1차 시스템의 pole**이다. Don이 P 제어기를 튜닝할 때 보던 것과 정확히 같은 식이다.

| η · λ 범위 | pole (1 − ηλ) | 동작 | 제어 루프 비유 |
|---|---|---|---|
| 0 < ηλ < 1 | 0 ~ 1 | 단조 수렴 (overshoot 없음) | gain 작음, overdamped |
| ηλ = 1 | 0 | 1 step에 정확히 도착 | deadbeat |
| 1 < ηλ < 2 | −1 ~ 0 | 진동하면서 수렴 | underdamped, ringing |
| ηλ > 2 | < −1 | 진동하며 발산 | gain 과다, 불안정 |

우리 예제 `L = (w−3)²`는 λ = 2다. 그래서:

- η = 0.1 → pole 0.8 (2.2절의 0.8배!)
- η = 0.02 → pole 0.96 (느림)
- η = 0.3 → pole 0.4 (빠름)
- η = 1.05 → pole −1.1 (|pole| > 1 → 발산, 부호가 바뀌니 좌우로 튄다)

**안정 조건: η < 2/λ.** 곡률이 큰(가파른) 방향일수록 허용 learning rate가 작다. 실제 신경망에서는 λ를 모르니 learning rate를 실험으로 찾는다. PID 튜닝에서 Ziegler–Nichols처럼 "진동이 시작하는 gain을 찾고 그보다 낮춘다"는 감각이 그대로 통한다: **loss가 튀기 시작하는 lr을 찾고, 그 1/3~1/10 정도를 쓴다.**

### 2.5 2D 그릇 — 방향마다 곡률이 다르면

이번엔 파라미터 2개: `L(x, y) = x² + 5y²`. 등고선이 옆으로 길쭉한 타원이다. gradient는 `(2x, 10y)`, 곡률은 x방향 λ=2, y방향 λ=10. 안정 조건은 **가장 가파른 방향**이 결정한다: η < 2/10 = 0.2.

```python
# 2D 그릇 L(x,y) = x² + 5y² 위에서 경사하강법 경로 추적
import numpy as np

def grad(p):
    x, y = p
    return np.array([2 * x, 10 * y])   # (∂L/∂x, ∂L/∂y)

for lr in [0.05, 0.18, 0.21]:
    p = np.array([-4.0, 1.5])
    print(f"lr={lr}")
    for k in range(7):
        L = p[0]**2 + 5 * p[1]**2
        print(f"  step {k}: x={p[0]:+.3f} y={p[1]:+.3f} L={L:8.3f}")
        p = p - lr * grad(p)
```

```text
lr=0.05
  step 0: x=-4.000 y=+1.500 L=  27.250
  step 1: x=-3.600 y=+0.750 L=  15.773
  step 2: x=-3.240 y=+0.375 L=  11.201
  step 3: x=-2.916 y=+0.188 L=   8.679
  step 4: x=-2.624 y=+0.094 L=   6.931
  step 5: x=-2.362 y=+0.047 L=   5.590
  step 6: x=-2.126 y=+0.023 L=   4.522
lr=0.18
  step 0: x=-4.000 y=+1.500 L=  27.250
  step 1: x=-2.560 y=-1.200 L=  13.754
  step 2: x=-1.638 y=+0.960 L=   7.292
  step 3: x=-1.049 y=-0.768 L=   4.049
  step 4: x=-0.671 y=+0.614 L=   2.338
  step 5: x=-0.429 y=-0.492 L=   1.392
  step 6: x=-0.275 y=+0.393 L=   0.849
lr=0.21
  step 0: x=-4.000 y=+1.500 L=  27.250
  step 1: x=-2.320 y=-1.650 L=  18.995
  step 2: x=-1.346 y=+1.815 L=  18.282
  step 3: x=-0.780 y=-1.996 L=  20.539
  step 4: x=-0.453 y=+2.196 L=  24.320
  step 5: x=-0.263 y=-2.416 L=  29.249
  step 6: x=-0.152 y=+2.657 L=  35.331
```

출력에서 볼 것: `lr=0.05`는 y가 매번 절반(pole 1−0.5=0.5)으로 부드럽게 줄지만 x는 0.9배씩만 줄어 느리다. `lr=0.18`은 y의 부호가 매 step 바뀐다(pole 1−1.8=−0.8, 지그재그) — 그래도 수렴. `lr=0.21`은 x는 잘 줄어드는데(pole 0.58) **y가 발산**한다(pole −1.1). 한 방향이라도 불안정하면 전체가 망가진다.

```svg
<svg viewBox="0 0 640 335" xmlns="http://www.w3.org/2000/svg">
<line x1="30" y1="165" x2="610" y2="165" stroke="currentColor" stroke-opacity="0.4"/><line x1="320" y1="20" x2="320" y2="310" stroke="currentColor" stroke-opacity="0.4"/><ellipse cx="320" cy="165" rx="55.0" ry="24.6" fill="none" stroke="#888" stroke-width="1"/><text x="324" y="137.4" font-size="12">L=1</text>
<ellipse cx="320" cy="165" rx="110.0" ry="49.2" fill="none" stroke="#888" stroke-width="1"/><ellipse cx="320" cy="165" rx="165.0" ry="73.8" fill="none" stroke="#888" stroke-width="1"/><text x="324" y="88.2" font-size="12">L=9</text><ellipse cx="320" cy="165" rx="220.0" ry="98.4" fill="none" stroke="#888" stroke-width="1"/>
<ellipse cx="320" cy="165" rx="287.1" ry="128.4" fill="none" stroke="#888" stroke-width="1"/><text x="324" y="33.6" font-size="12">L=27.25</text>
<polyline points="100.0,82.5 122.0,123.8 141.8,144.4 159.6,154.7 175.7,159.8 190.1,162.4 203.1,163.7 214.8,164.4 225.3,164.7 234.8,164.8 243.3,164.9 251.0,165.0 257.9,165.0 264.1,165.0 269.7,165.0 274.7,165.0 279.2,165.0 283.3,165.0 287.0,165.0 290.3,165.0 293.3,165.0 295.9,165.0 298.3,165.0 300.5,165.0 302.5,165.0 304.2,165.0" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="100.0" cy="82.5" r="3" fill="#4a7bd0"/><circle cx="122.0" cy="123.8" r="3" fill="#4a7bd0"/><circle cx="141.8" cy="144.4" r="3" fill="#4a7bd0"/><circle cx="159.6" cy="154.7" r="3" fill="#4a7bd0"/><circle cx="175.7" cy="159.8" r="3" fill="#4a7bd0"/><circle cx="190.1" cy="162.4" r="3" fill="#4a7bd0"/><circle cx="203.1" cy="163.7" r="3" fill="#4a7bd0"/>
<circle cx="214.8" cy="164.4" r="3" fill="#4a7bd0"/><polyline points="100.0,82.5 179.2,231.0 229.9,112.2 262.3,207.2 283.1,131.2 296.4,192.0 304.9,143.4 310.3,182.3 313.8,151.2 316.0,176.1 317.5,156.1 318.4,172.1 319.0,159.3" fill="none" stroke="#e08a3c" stroke-width="2"/><circle cx="100.0" cy="82.5" r="3" fill="#e08a3c"/><circle cx="179.2" cy="231.0" r="3" fill="#e08a3c"/>
<circle cx="229.9" cy="112.2" r="3" fill="#e08a3c"/><circle cx="262.3" cy="207.2" r="3" fill="#e08a3c"/><circle cx="283.1" cy="131.2" r="3" fill="#e08a3c"/><circle cx="296.4" cy="192.0" r="3" fill="#e08a3c"/><circle cx="304.9" cy="143.4" r="3" fill="#e08a3c"/><circle cx="310.3" cy="182.3" r="3" fill="#e08a3c"/>
<circle cx="100.0" cy="82.5" r="5" fill="none" stroke="currentColor"/><text x="90.0" y="72.5" font-size="12">시작 (−4, 1.5)</text><text x="430" y="306" font-size="12" fill="#4a7bd0">lr=0.05: 느리지만 부드럽게</text><text x="430" y="324" font-size="12" fill="#e08a3c">lr=0.18: y방향 지그재그</text><text x="605" y="181" font-size="13" text-anchor="end">x</text><text x="306" y="30" font-size="13">y</text>
</svg>
```

그림 3 — `L = x² + 5y²`의 등고선(회색 타원)과 경사하강 경로(실제 계산값). 파랑 lr=0.05는 25 step 동안 부드럽지만 x방향 진행이 느리고, 주황 lr=0.18은 좁은 y방향으로 지그재그하며 중심에 도달한다.

이것이 **ill-conditioning(나쁜 조건수)** 문제다. 곡률 비(여기선 10/2 = 5)가 클수록 "가파른 방향 때문에 lr을 못 올리고, 완만한 방향은 느리게 기어간다". 7절의 momentum과 Adam은 바로 이 문제를 완화하려고 나왔다. 또 입력 feature의 스케일을 맞추는(normalization) 이유도 이것이다 — 센서 채널 하나는 ±2000 dps(자이로), 다른 하나는 ±2 g(가속도)라면 loss 지형이 극단적으로 길쭉해진다.

### 2.6 함정

- "loss가 NaN이 됐다" → 거의 항상 learning rate가 너무 크거나(발산) 입력 스케일이 크다. 첫 번째 조치는 lr을 1/10로.
- "loss가 거의 안 줄어든다" → lr이 너무 작거나, 8절의 gradient 소실.
- lr은 **모델·데이터·optimizer마다 다르다**. 다른 프로젝트의 값을 그대로 쓰지 말 것.

---

## 3. Loss 지형(landscape) — 실제 신경망은 그릇이 아니다

### 3.1 직관

2절의 그릇은 최소점이 하나뿐인 **convex(볼록)** 함수였다. 실제 신경망의 loss는 수백만 차원의 울퉁불퉁한 지형이다. 그래도 알아 둘 것은 세 종류의 "평평한 점(gradient = 0)"뿐이다.

| 점 | 모양 (2D 예) | 의미 | 실무에서 |
|---|---|---|---|
| global minimum | 그릇 바닥 | 전체에서 가장 낮은 곳 | 찾을 필요 없음(보통 불가능) |
| local minimum | 작은 웅덩이 | 주변보다만 낮음 | 큰 네트워크에선 대부분 global과 성능 비슷 |
| saddle point (안장점) | `f = x² − y²`의 원점: x방향은 바닥, y방향은 꼭대기 | 한 방향으로는 내려갈 수 있음 | 고차원에서 훨씬 흔함. gradient가 작아 학습이 "정체"되어 보임 |

말로 하면: 차원이 많으면 "모든 방향으로 동시에 오르막"인 진짜 웅덩이보다 "어느 한 방향은 내리막"인 안장점이 훨씬 많다. 그래서 실무의 문제는 "local minimum에 갇힌다"보다 "평평한 구간에서 느려진다"인 경우가 많다.

### 3.2 mini-batch 잡음이 오히려 도움이 된다

실제 학습에서는 전체 데이터(수만~수억 개)로 gradient를 계산하지 않고, 매 step 무작위 **mini-batch**(예: 32개)로 계산한다. 그러면 gradient가 **잡음 섞인 추정치**가 된다. 얼마나 잡음이 섞이는지 재 보는 코드다.

```python
# mini-batch gradient는 full-batch gradient의 "잡음 섞인 추정치" — batch 크기별 잡음 측정
import numpy as np
rng = np.random.default_rng(0)
N = 10_000
x = rng.normal(0, 1, N); y = 2.0 * x + rng.normal(0, 0.5, N)   # 정답 기울기 w=2
w = 0.5                                                          # 현재 파라미터
g_each = -2 * x * (y - w * x)          # 샘플별 d/dw (y - w·x)²
print(f"full-batch gradient = {g_each.mean():+.4f}")
for bs in [1, 8, 64, 512]:
    est = [g_each[rng.choice(N, bs, replace=False)].mean() for _ in range(2000)]
    print(f"batch {bs:4d}: 평균 {np.mean(est):+.4f}  표준편차 {np.std(est):.4f}")
```

```text
full-batch gradient = -2.9742
batch    1: 평균 -2.8541  표준편차 4.0544
batch    8: 평균 -2.9554  표준편차 1.5277
batch   64: 평균 -2.9499  표준편차 0.5227
batch  512: 평균 -2.9717  표준편차 0.1832
```

출력에서 볼 것: 평균은 batch 크기와 상관없이 full-batch 값(−2.97) 근처다(**unbiased 추정**). 표준편차는 batch가 8배 커질 때마다 약 √8 ≈ 2.8배 줄어든다(4.05 → 1.53 → 0.52 → 0.18). A2에서 본 "표본평균의 표준오차 ∝ 1/√n" 그대로다.

이 잡음의 장단점:

- 장점 1: step 하나가 싸다. 32개로 방향을 대충 잡고 자주 움직이는 것이 전체를 다 보고 한 번 움직이는 것보다 보통 빠르다.
- 장점 2: 잡음이 안장점·평평한 구간에서 **흔들어 빠져나오게** 돕고, 날카로운 웅덩이보다 넓고 평평한 최소점에 머무는 경향이 있다(넓은 최소점이 일반화에 유리하다는 경험적 관찰이 많다).
- 단점: 끝까지 잡음 때문에 최소점 근처에서 떨린다 → 7.4절의 learning rate schedule로 후반에 lr을 줄여 해결한다.

펌웨어 비유: ADC 샘플 하나로 제어하면 잡음이 크지만 반응이 빠르고, 1000개 평균을 내면 정확하지만 루프가 느려진다. mini-batch 크기는 이 트레이드오프의 손잡이다.

---

## 4. Chain rule과 계산 그래프

### 4.1 chain rule — 민감도는 곱해진다

```
 y = f(u),  u = g(w)   이면   dy/dw = (dy/du) · (du/dw)
```

말로 하면: w가 u를 3배 민감하게 움직이고, u가 y를 4배 민감하게 움직이면, w는 y를 3 × 4 = 12배 민감하게 움직인다. 신호 체인의 **gain을 곱하는 것**과 같다 — LNA gain × mixer gain × IF amp gain = 전체 gain. Don이 RF 체인에서 dB를 더하던(= 배수를 곱하던) 바로 그 계산이다.

### 4.2 계산 그래프 (computational graph)

모든 수식은 덧셈·곱셈·제곱 같은 **기본 연산 노드**의 그래프로 쪼갤 수 있다. 예: 선형 모델 하나와 제곱 오차.

```
 L = (w·x + b − y)²       w=2, x=3, b=1, y=5

 쪼개기:  u = w·x = 6
         z = u + b = 7
         e = z − y = 2
         L = e²    = 4
```

**forward pass**는 왼쪽에서 오른쪽으로 값을 계산하면서 **중간값(u, z, e)을 저장**한다. **backward pass**는 오른쪽 끝(∂L/∂L = 1)에서 시작해 왼쪽으로 가면서 각 노드의 **local gradient**를 곱한다.

| 노드 | 연산 | local gradient | 들어온 upstream | 곱한 결과 (downstream) |
|---|---|---|---|---|
| 제곱 | L = e² | ∂L/∂e = 2e = 4 | 1 | ∂L/∂e = 4 |
| 뺄셈 | e = z − y | ∂e/∂z = 1, ∂e/∂y = −1 | 4 | ∂L/∂z = 4, ∂L/∂y = −4 |
| 덧셈 | z = u + b | ∂z/∂u = 1, ∂z/∂b = 1 | 4 | ∂L/∂u = 4, ∂L/∂b = 4 |
| 곱셈 | u = w·x | ∂u/∂w = x = 3, ∂u/∂x = w = 2 | 4 | ∂L/∂w = 12, ∂L/∂x = 8 |

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="a3arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><circle cx="60" cy="70" r="24" fill="none" stroke="currentColor"/><text x="60" y="75" font-size="13" text-anchor="middle">w=2</text>
<circle cx="60" cy="170" r="24" fill="none" stroke="currentColor"/><text x="60" y="175" font-size="13" text-anchor="middle">x=3</text><circle cx="190" cy="120" r="24" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="190" y="125" font-size="14" text-anchor="middle">×</text>
<circle cx="190" cy="240" r="24" fill="none" stroke="currentColor"/><text x="190" y="245" font-size="13" text-anchor="middle">b=1</text><circle cx="330" cy="120" r="24" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="330" y="125" font-size="14" text-anchor="middle">+</text>
<circle cx="330" cy="240" r="24" fill="none" stroke="currentColor"/><text x="330" y="245" font-size="13" text-anchor="middle">y=5</text><circle cx="470" cy="120" r="24" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="470" y="125" font-size="14" text-anchor="middle">−</text>
<circle cx="600" cy="120" r="24" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="600" y="125" font-size="13" text-anchor="middle">( )²</text><line x1="82" y1="79" x2="166" y2="112" stroke="currentColor" marker-end="url(#a3arr)"/><line x1="82" y1="161" x2="166" y2="129" stroke="currentColor" marker-end="url(#a3arr)"/>
<line x1="214" y1="120" x2="304" y2="120" stroke="currentColor" marker-end="url(#a3arr)"/><line x1="207" y1="223" x2="312" y2="138" stroke="currentColor" marker-end="url(#a3arr)"/><line x1="354" y1="120" x2="444" y2="120" stroke="currentColor" marker-end="url(#a3arr)"/><line x1="347" y1="223" x2="452" y2="138" stroke="currentColor" marker-end="url(#a3arr)"/>
<line x1="494" y1="120" x2="574" y2="120" stroke="currentColor" marker-end="url(#a3arr)"/><line x1="624" y1="120" x2="664" y2="120" stroke="currentColor" marker-end="url(#a3arr)"/><text x="259" y="110" font-size="13" text-anchor="middle" fill="#4a7bd0">u=6</text><text x="399" y="110" font-size="13" text-anchor="middle" fill="#4a7bd0">z=7</text>
<text x="534" y="110" font-size="13" text-anchor="middle" fill="#4a7bd0">e=2</text><text x="650" y="105" font-size="13" text-anchor="middle" fill="#4a7bd0">L=4</text><text x="650" y="150" font-size="13" text-anchor="middle" fill="#d0564a">1</text><text x="534" y="142" font-size="13" text-anchor="middle" fill="#d0564a">∂L/∂e=4</text>
<text x="399" y="142" font-size="13" text-anchor="middle" fill="#d0564a">∂L/∂z=4</text><text x="259" y="142" font-size="13" text-anchor="middle" fill="#d0564a">∂L/∂u=4</text><text x="60" y="36" font-size="13" text-anchor="middle" fill="#d0564a">∂L/∂w=4·x=12</text><text x="60" y="212" font-size="13" text-anchor="middle" fill="#d0564a">∂L/∂x=4·w=8</text>
<text x="190" y="282" font-size="13" text-anchor="middle" fill="#d0564a">∂L/∂b=4</text><text x="330" y="282" font-size="13" text-anchor="middle" fill="#d0564a">∂L/∂y=−4</text><text x="470" y="70" font-size="12" text-anchor="middle">local: ∂e/∂z=1</text><text x="600" y="70" font-size="12" text-anchor="middle">local: ∂L/∂e=2e</text>
<text x="190" y="70" font-size="12" text-anchor="middle">local: ∂u/∂w=x</text>
</svg>
```

그림 4 — 계산 그래프. 파랑 = forward 값(왼→오), 빨강 = backward gradient(오→왼). 각 노드는 자기 local gradient만 알면 되고, 오른쪽에서 온 값에 곱해서 왼쪽으로 넘긴다.

검산: `L(w) = (3w + 1 − 5)²`이니 `dL/dw = 2(3w − 4) · 3 = 2 · 2 · 3 = 12`. 맞다.

### 4.3 노드별 규칙 — 외워 둘 4개

| 연산 | backward에서 하는 일 | 기억법 |
|---|---|---|
| 덧셈 `z = a + b` | upstream을 **그대로 복사**해서 a, b 둘 다에 전달 | gradient 분배기 |
| 곱셈 `z = a · b` | a에는 `upstream × b`, b에는 `upstream × a` | 상대방 값을 곱한다 (그래서 forward 값을 저장해야 함!) |
| ReLU `z = max(a, 0)` | a > 0이면 통과, 아니면 0 | 스위치 (on/off) |
| 분기(한 값이 두 곳에 쓰임) | 두 경로에서 돌아온 gradient를 **더한다** | 합류점 |

두 번째 행이 핵심이다. 곱셈 노드의 backward에 **forward 때의 입력값**이 필요하다. 그래서 forward pass는 중간값을 전부 메모리에 들고 있어야 한다. 이것이 9절 "학습은 메모리를 많이 먹는다"의 뿌리다.

### 4.4 backprop = chain rule + 재사용

backprop(역전파, backpropagation)은 새로운 수학이 아니다. chain rule을 **출력 쪽에서부터** 적용해서, 한 번 계산한 upstream gradient(`∂L/∂z` 같은 것)를 여러 파라미터가 **공유**하게 하는 계산 순서일 뿐이다. 파라미터가 N개일 때 수치 미분은 forward를 N번(중앙 차분이면 2N번) 돌려야 하지만, backprop은 forward 1번 + backward 1번(비용은 forward의 약 2배)으로 N개 gradient를 전부 얻는다. 펌웨어로 치면 "공통 부분식을 한 번만 계산하고 캐시하는 최적화"다.

---

## 5. 2층 네트워크 backprop — 숫자로 끝까지

### 5.1 네트워크와 숫자

입력 2개 → hidden 2개(ReLU) → 출력 1개(sigmoid), loss는 BCE(binary cross-entropy, A2 참고).

```
  x₁ ─┐                       ┌──►  h₁ ─┐
      ├─► z1 = W1·x + b1 ─ReLU─┤          ├─► z2 = W2·h + b2 ─σ─► p ─► L = −[y·ln p + (1−y)·ln(1−p)]
  x₂ ─┘                       └──►  h₂ ─┘

  x  = [1.0, 2.0]          y  = 1 (정답: 양성)
  W1 = [[ 0.2, 0.4],       b1 = [0.1, 0.0]
        [−0.5, 0.1]]
  W2 = [0.6, −0.7]         b2 = 0.05
```

### 5.2 forward — 값을 계산하고 **저장**

```
 z1₁ = 0.2·1 + 0.4·2 + 0.1   =  1.1
 z1₂ = −0.5·1 + 0.1·2 + 0.0  = −0.3
 h   = ReLU(z1) = [1.1, 0]            ← h₂는 꺼졌다 (음수라서)
 z2  = 0.6·1.1 + (−0.7)·0 + 0.05 = 0.71
 p   = σ(0.71) = 1 / (1 + e^(−0.71)) = 0.6704
 L   = −ln(0.6704) = 0.3999           (y = 1이므로 첫 항만 남음)
```

말로 하면: 모델은 "양성일 확률 67%"라고 답했다. 정답은 양성이니 틀리진 않았지만 자신감이 부족하다. loss 0.40은 그 부족함의 크기다.

저장해 둔 것: `x, z1, h, z2, p`. backward에서 전부 쓰인다.

### 5.3 backward — 출력 쪽부터 local gradient를 곱한다

**① sigmoid + BCE를 한 번에.** 따로 미분하면 복잡하지만 합치면 놀랍게 단순해진다.

```
 ∂L/∂p  = −y/p + (1−y)/(1−p)
 ∂p/∂z2 = p(1 − p)                         (sigmoid의 미분)
 ∂L/∂z2 = ∂L/∂p · ∂p/∂z2 = p − y           (정리하면 이렇게 된다)
        = 0.6704 − 1 = −0.3296
```

말로 하면: 출력층의 gradient는 그냥 "예측 − 정답"이다. 음수 → z2를 **키우면** loss가 준다(확률을 1 쪽으로 밀어야 하니까). 그래서 PyTorch에서는 sigmoid와 BCE를 합친 `binary_cross_entropy_with_logits`를 쓴다 — 계산도 싸고 수치적으로도 안전하다.

**② 출력층 파라미터.** `z2 = W2·h + b2`는 곱셈과 덧셈이다.

```
 ∂L/∂W2 = ∂L/∂z2 · h  = −0.3296 · [1.1, 0] = [−0.3626, 0]
 ∂L/∂b2 = ∂L/∂z2      = −0.3296
```

**③ hidden으로 전달.** 곱셈 노드는 "상대방 값"을 곱한다 → h 쪽에는 W2를 곱한다.

```
 ∂L/∂h = ∂L/∂z2 · W2 = −0.3296 · [0.6, −0.7] = [−0.1978, 0.2307]
```

**④ ReLU 통과.** z1₁ = 1.1 > 0이라 통과, z1₂ = −0.3 < 0이라 차단.

```
 ∂L/∂z1 = ∂L/∂h ⊙ [z1 > 0] = [−0.1978, 0.2307] ⊙ [1, 0] = [−0.1978, 0]
```

**⑤ 입력층 파라미터.** `z1 = W1·x + b1`이니 각 행 i, 열 j에 대해 `∂L/∂W1[i,j] = ∂L/∂z1[i] · x[j]` — 즉 **외적(outer product)** 이다.

```
 ∂L/∂W1 = outer(∂L/∂z1, x) = [[−0.1978·1, −0.1978·2],    = [[−0.1978, −0.3955],
                              [   0·1,       0·2   ]]      [  0,       0     ]]
 ∂L/∂b1 = ∂L/∂z1 = [−0.1978, 0]
```

**정리 표**

| 파라미터 | 값 | gradient | 해석 |
|---|---|---|---|
| W2 | [0.6, −0.7] | [−0.3626, 0] | W2₁을 키우면 loss 감소. h₂=0이라 W2₂는 영향 없음 |
| b2 | 0.05 | −0.3296 | 키우면 감소 |
| W1 | [[0.2, 0.4], [−0.5, 0.1]] | [[−0.1978, −0.3955], [0, 0]] | 두 번째 행은 ReLU가 꺼져서 gradient 0 ("dead" 경로) |
| b1 | [0.1, 0.0] | [−0.1978, 0] | 〃 |

### 5.4 update 한 번 — loss가 정말 줄어드나

η = 0.5로 한 step 업데이트하면(`새 값 = 값 − 0.5 × gradient`):

```
 W1 첫 행 = [0.2 + 0.0989, 0.4 + 0.1978] = [0.2989, 0.5978]    b1₁ = 0.1989
 W2₁ = 0.6 + 0.1813 = 0.7813                                   b2  = 0.2148

 다시 forward:  h₁ = 0.2989 + 1.1955 + 0.1989 = 1.6933
               z2 = 0.7813 · 1.6933 + 0.2148 = 1.5377
               p  = σ(1.5377) = 0.8231,   L = −ln 0.8231 = 0.1946
```

loss가 0.3999 → 0.1946으로 절반 가까이 줄었다. 확률은 67% → 82%.

### 5.5 검증 (a) — 수치 미분(gradient check)

손으로 한 backward를 numpy로 옮기고, W1의 각 원소를 ±ε 흔들어 얻은 수치 미분과 비교하는 코드다. 직접 backward를 짠 코드는 **반드시 이렇게 검증**한다(단위 테스트의 golden reference).

```python
# 손으로 한 backprop을 numpy로 재현 + 수치 미분(finite difference)으로 검증
import numpy as np
x  = np.array([1.0, 2.0]); y = 1.0
W1 = np.array([[0.2, 0.4], [-0.5, 0.1]]); b1 = np.array([0.1, 0.0])
W2 = np.array([0.6, -0.7]);               b2 = 0.05

def forward(W1, b1, W2, b2):
    z1 = W1 @ x + b1; h = np.maximum(z1, 0)
    z2 = W2 @ h + b2; p = 1 / (1 + np.exp(-z2))
    L = -(y * np.log(p) + (1 - y) * np.log(1 - p))
    return z1, h, z2, p, L

z1, h, z2, p, L = forward(W1, b1, W2, b2)
print("z1", z1, "h", h, f"z2 {z2:.4f} p {p:.4f} L {L:.4f}")
# --- backward (손계산과 같은 순서) ---
dz2 = p - y                      # BCE + sigmoid 합친 미분
dW2 = dz2 * h;  db2 = dz2
dh  = dz2 * W2
dz1 = dh * (z1 > 0)              # ReLU: 양수면 1, 아니면 0
dW1 = np.outer(dz1, x); db1 = dz1
print(f"dz2 {dz2:.4f}  dW2 {dW2}  dh {dh}")
print("dW1\n", dW1)
# --- 수치 미분: W1 각 원소를 ±eps 흔들어 본다 ---
eps, num = 1e-5, np.zeros_like(W1)
for i in range(2):
    for j in range(2):
        Wp = W1.copy(); Wp[i, j] += eps
        Wm = W1.copy(); Wm[i, j] -= eps
        num[i, j] = (forward(Wp, b1, W2, b2)[-1] - forward(Wm, b1, W2, b2)[-1]) / (2 * eps)
print("numerical dW1\n", num)
print("max |analytic - numerical| =", np.abs(dW1 - num).max())
```

```text
z1 [ 1.1 -0.3] h [1.1 0. ] z2 0.7100 p 0.6704 L 0.3999
dz2 -0.3296  dW2 [-0.36255872 -0.        ]  dh [-0.1977593   0.23071919]
dW1
 [[-0.1977593  -0.39551861]
 [ 0.          0.        ]]
numerical dW1
 [[-0.1977593  -0.39551861]
 [ 0.          0.        ]]
max |analytic - numerical| = 5.033196082138147e-12
```

출력에서 볼 것: 손계산 값(−0.1978, −0.3955)과 정확히 같고, 해석적 vs 수치 gradient 차이가 5e-12로 사실상 0이다. `-0.`은 음수 × 0의 IEEE 754 부호 비트일 뿐 0과 같다(Don에게 익숙한 그것).

### 5.6 검증 (b) — PyTorch autograd

같은 네트워크를 PyTorch로 만들고 `L.backward()` 한 줄로 gradient를 얻는 코드다.

```python
# 같은 네트워크를 PyTorch autograd로 — 손계산 결과와 일치하는지
import torch
x  = torch.tensor([1.0, 2.0]); y = torch.tensor(1.0)
W1 = torch.tensor([[0.2, 0.4], [-0.5, 0.1]], requires_grad=True)
b1 = torch.tensor([0.1, 0.0], requires_grad=True)
W2 = torch.tensor([0.6, -0.7], requires_grad=True)
b2 = torch.tensor(0.05, requires_grad=True)

h = torch.relu(W1 @ x + b1)
z2 = W2 @ h + b2
L = torch.nn.functional.binary_cross_entropy_with_logits(z2, y)
L.backward()                         # 역전파 한 줄
print(f"L = {L.item():.4f}")
print("dW1 =", W1.grad)
print("db1 =", b1.grad)
print("dW2 =", W2.grad)
print("db2 =", b2.grad)
```

```text
L = 0.3999
dW1 = tensor([[-0.1978, -0.3955],
        [ 0.0000,  0.0000]])
db1 = tensor([-0.1978,  0.0000])
dW2 = tensor([-0.3626, -0.0000])
db2 = tensor(-0.3296)
```

출력에서 볼 것: 손계산·numpy·PyTorch 세 가지가 소수 넷째 자리까지 일치한다. PyTorch는 forward 때 연산마다 계산 그래프 노드를 기록해 두고(`requires_grad=True`인 텐서가 관여한 연산만), `backward()`가 4절의 규칙으로 그 그래프를 거꾸로 훑는다. 이것을 **reverse-mode automatic differentiation**이라고 부른다.

### 5.7 행렬 형태로 일반화 (batch 포함)

샘플이 N개인 batch, `X: [N, D]`, `W1: [D, H]` 형태(PyTorch/numpy 관례)로 쓰면:

```
 forward                         backward
 Z1 = X @ W1 + b1   [N,H]        dZ2 = (P − Y) / N              [N,1]
 H  = ReLU(Z1)      [N,H]        dW2 = Hᵀ @ dZ2                 [H,1]
 Z2 = H @ W2 + b2   [N,1]        db2 = sum(dZ2, axis=0)
 P  = σ(Z2)                      dH  = dZ2 @ W2ᵀ                [N,H]
                                 dZ1 = dH ⊙ (Z1 > 0)
                                 dW1 = Xᵀ @ dZ1                 [D,H]
                                 db1 = sum(dZ1, axis=0)
```

말로 하면: backward도 **GEMM(행렬곱) 두 개**가 레이어마다 나온다 — 입력 쪽으로 넘길 `dH = dZ @ Wᵀ`와 weight gradient `dW = Xᵀ @ dZ`. forward GEMM이 1개니까 **backward 연산량 ≈ forward의 2배**, 학습 1 step ≈ 추론 3번이라는 경험칙이 여기서 나온다. shape 검산 요령: `dW`는 항상 `W`와 shape이 같아야 한다.

---

## 6. numpy만으로 MLP 학습 루프 짜기

5.7의 행렬 식을 그대로 옮겨, 2→16→1 MLP를 **two moons**(서로 맞물린 초승달 두 개, 직선으로는 못 나누는 2D 분류 데이터)에 학습시키는 코드다. forward·backward·update 세 블록이 전부다.

```python
# numpy만으로 2→16→1 MLP를 two-moons 데이터에 학습 (forward·backward·update 직접)
import numpy as np
rng = np.random.default_rng(0)
n = 200; t = rng.uniform(0, np.pi, n)
X = np.vstack([np.c_[np.cos(t[:100]), np.sin(t[:100])],             # 위 초승달 (label 0)
               np.c_[1 - np.cos(t[100:]), 0.5 - np.sin(t[100:])]])  # 아래 초승달 (label 1)
X += rng.normal(0, 0.1, X.shape); y = np.r_[np.zeros(100), np.ones(100)]

H = 16
W1 = rng.normal(0, np.sqrt(2 / 2), (2, H)); b1 = np.zeros(H)   # He 초기화
W2 = rng.normal(0, np.sqrt(1 / H), (H, 1)); b2 = np.zeros(1)
lr = 0.5
for step in range(2001):
    z1 = X @ W1 + b1; h = np.maximum(z1, 0)                  # forward
    z2 = (h @ W2 + b2).ravel(); p = 1 / (1 + np.exp(-z2))
    loss = -np.mean(y * np.log(p + 1e-12) + (1 - y) * np.log(1 - p + 1e-12))
    dz2 = ((p - y) / n)[:, None]                              # backward
    dW2 = h.T @ dz2;  db2 = dz2.sum(0)
    dz1 = (dz2 @ W2.T) * (z1 > 0)
    dW1 = X.T @ dz1;  db1 = dz1.sum(0)
    W1 -= lr * dW1; b1 -= lr * db1; W2 -= lr * dW2; b2 -= lr * db2   # update
    if step % 400 == 0:
        acc = np.mean((p > 0.5) == y)
        print(f"step {step:4d}  loss {loss:.4f}  acc {acc:.3f}")
```

```text
step    0  loss 0.8282  acc 0.595
step  400  loss 0.1514  acc 0.940
step  800  loss 0.0306  acc 0.985
step 1200  loss 0.0168  acc 1.000
step 1600  loss 0.0115  acc 1.000
step 2000  loss 0.0087  acc 1.000
```

출력에서 볼 것: loss가 0.83 → 0.009로 줄고, 정확도가 59.5% → 100%가 된다. 직선 하나로는 안 되는 모양을 hidden 16개의 ReLU 조각들이 꺾은선 경계로 나눈 것이다. (이 정확도는 학습 데이터 기준이다. 따로 떼어 둔 test set으로 재야 진짜 성능이다 — A4 참고.)

코드 읽는 요령:

- `dz2 = (p − y) / n`: 5.3절의 "예측 − 정답", loss가 평균이라 n으로 나눈다.
- `h.T @ dz2`, `dz2 @ W2.T`, `X.T @ dz1`: 5.7절의 GEMM 세 개.
- `(z1 > 0)`: forward 때 저장한 z1을 backward의 ReLU 스위치로 재사용한다.
- `+ 1e-12`: log(0) = −∞를 막는 가드. 펌웨어의 0 나눗셈 가드와 같은 습관이다.
- `np.sqrt(2 / 2)`: He 초기화(8.3절). fan-in(입력 개수)이 2라서.

---

## 7. Batch, epoch, optimizer, learning rate schedule

### 7.1 batch vs mini-batch vs SGD, 그리고 epoch

| 방식 | 한 step에 쓰는 샘플 | 장점 | 단점 |
|---|---|---|---|
| (full) batch GD | 전체 N개 | gradient 정확, 재현성 | step이 비쌈, 메모리 큼, 큰 데이터엔 불가능 |
| mini-batch SGD | B개 (보통 16~512) | GPU/NPU 병렬성 활용 + 적당한 잡음 | batch 크기·lr을 같이 튜닝해야 함 |
| (순수) SGD | 1개 | 메모리 최소, 잡음 최대 | 병렬성 없음, 진동 큼 |

실무에서 "SGD"라고 하면 거의 항상 **mini-batch SGD**를 뜻한다(PyTorch의 `torch.optim.SGD`도 넘겨준 batch가 몇 개든 상관없다).

- **step (iteration)**: 파라미터 업데이트 1번.
- **epoch**: 학습 데이터 전체를 한 번 다 본 것. N=200, B=20이면 1 epoch = 10 step.
- 매 epoch 데이터 순서를 섞는다(shuffle) — 순서가 고정되면 gradient 잡음에 편향이 생긴다.

### 7.2 momentum — 관성을 달아 준다

```
 v ← β · v + ∇L(W)          (β ≈ 0.9)
 W ← W − η · v
```

말로 하면: 이번 gradient만 보지 않고 **과거 gradient의 누적(속도)** 방향으로 움직인다. 공이 굴러 내려가며 관성을 얻는 것과 같다. 2.5절의 길쭉한 그릇에서 y방향은 부호가 매번 바뀌니 누적하면 서로 **상쇄**되고, x방향은 부호가 일정하니 **가속**된다 → 지그재그가 줄고 완만한 방향으로 빨라진다.

펌웨어 비유: v는 gradient의 1차 IIR low-pass filter(β가 필터 계수)다. 잡음(mini-batch 잡음, 진동)은 걸러지고 일관된 성분(DC)은 쌓인다. PID로 치면 과거 오차를 누적한다는 점에서 I항과 느낌이 비슷하지만, 엄밀히는 "질량이 있는 2차 시스템"에 가깝다 — 그래서 너무 크면 overshoot가 생긴다.

(위 식은 PyTorch `SGD(momentum=0.9)` 방식이다. `v ← β·v + (1−β)·∇L`로 쓰는 교재도 있는데, 그러면 η의 스케일만 달라진다.)

### 7.3 Adam — 파라미터마다 자동으로 step 크기 조절

```
 g  = ∇L(W)
 m ← β₁ · m + (1 − β₁) · g          1차 모멘트: gradient의 이동평균 (방향)      β₁ = 0.9
 v ← β₂ · v + (1 − β₂) · g²         2차 모멘트: gradient 제곱의 이동평균 (크기)  β₂ = 0.999
 m̂ = m / (1 − β₁ᵗ),  v̂ = v / (1 − β₂ᵗ)   bias correction (t = step 번호)
 W ← W − η · m̂ / (√v̂ + ε)           ε ≈ 1e-8
```

말로 하면: momentum(m)으로 방향을 잡고, 그 방향으로 가는 거리를 "이 파라미터의 gradient가 평소에 얼마나 큰지(√v)"로 **나눠서 정규화**한다. gradient가 늘 큰 파라미터는 조금씩, 늘 작은 파라미터는 상대적으로 크게 움직인다. 결과적으로 한 step의 이동량이 대략 η 정도로 묶인다 → learning rate에 덜 민감하다.

각 항의 역할:

- **m (β₁)**: 잡음 평균화 + 관성. momentum과 같은 역할.
- **v (β₂)**: 파라미터별 "gain 자동 조절". AGC(automatic gain control)와 같은 발상 — 신호가 크면 gain을 낮추고 작으면 올린다.
- **bias correction**: m, v를 0으로 초기화했기 때문에 초반 몇 step은 값이 0 쪽으로 치우친다. 그걸 `1/(1−βᵗ)`로 보정한다. t=1이면 `m̂ = g`, `v̂ = g²`가 되어 첫 step은 정확히 `η · sign(g)`만큼 움직인다.
- **ε**: 0 나눗셈 방지.
- 메모리 비용: 파라미터마다 m, v 두 개를 더 저장한다 → optimizer 상태만 weight의 2배(9절).

### 7.4 직접 구현해서 비교 — 길쭉한 그릇

GD, momentum, Adam의 업데이트 식을 numpy로 그대로 옮겨, 2.5절의 `L = x² + 5y²`에서 learning rate 3개씩 100 step 돌린 코드다.

```python
# momentum·Adam 업데이트 식을 직접 구현 — 길쭉한 그릇 L = x² + 5y², 시작점 (-4, 1.5)
import numpy as np
grad = lambda p: np.array([2 * p[0], 10 * p[1]])
loss = lambda p: p[0]**2 + 5 * p[1]**2

def run(opt, lr, steps=100, beta=0.9, b1=0.9, b2=0.999, eps=1e-8):
    p, m, v = np.array([-4.0, 1.5]), np.zeros(2), np.zeros(2)
    for t in range(1, steps + 1):
        g = grad(p)
        if opt == "gd":
            step = lr * g
        elif opt == "momentum":
            m = beta * m + g                       # 속도 = 과거 기울기의 누적
            step = lr * m
        else:  # adam
            m = b1 * m + (1 - b1) * g              # 1차 모멘트: 기울기 평균(방향)
            v = b2 * v + (1 - b2) * g**2           # 2차 모멘트: 기울기 제곱 평균(크기)
            m_hat, v_hat = m / (1 - b1**t), v / (1 - b2**t)   # bias correction
            step = lr * m_hat / (np.sqrt(v_hat) + eps)
        if t == 1 and lr == 0.1:
            print(f"  [{opt:8s} lr=0.1] grad={g}  첫 step 이동량={np.round(step, 4)}")
        p = p - step
    return loss(p)

for opt in ["gd", "momentum", "adam"]:
    res = [run(opt, lr) for lr in [0.01, 0.1, 0.3]]
    print(f"{opt:8s} 100 step 후 L  lr=0.01: {res[0]:.2e}  lr=0.1: {res[1]:.2e}  lr=0.3: {res[2]:.2e}")
```

```text
  [gd       lr=0.1] grad=[-8. 15.]  첫 step 이동량=[-0.8  1.5]
gd       100 step 후 L  lr=0.01: 2.81e-01  lr=0.1: 6.64e-19  lr=0.3: 1.81e+61
  [momentum lr=0.1] grad=[-8. 15.]  첫 step 이동량=[-0.8  1.5]
momentum 100 step 후 L  lr=0.01: 4.43e-04  lr=0.1: 1.51e-04  lr=0.3: 1.79e-03
  [adam     lr=0.1] grad=[-8. 15.]  첫 step 이동량=[-0.1  0.1]
adam     100 step 후 L  lr=0.01: 1.13e+01  lr=0.1: 6.77e-04  lr=0.3: 2.59e-04
```

출력에서 볼 것:

- 첫 step 이동량: GD와 momentum은 gradient에 비례(`[−0.8, 1.5]`), Adam은 gradient 크기와 무관하게 **두 좌표 모두 정확히 0.1 = η**다(bias correction 덕분에 `η·sign(g)`).
- GD는 lr=0.3에서 1.8e+61로 **발산**했다(y방향 pole 1−3 = −2). Adam은 같은 lr에서도 안정적이다.
- 반대로 lr=0.01의 Adam은 100 step × 최대 0.01 이동 ≈ 1 거리밖에 못 가서 L이 11.3에 머문다. Adam도 lr이 너무 작으면 느리다.
- 공정하게 말하면, 이렇게 완벽한 2차 그릇에서는 **잘 튜닝된 GD(lr=0.1)가 가장 빠르다**(6.6e−19). optimizer의 장점은 "튜닝을 덜 해도 잘 된다", "파라미터마다 스케일이 달라도 된다", "잡음이 있는 실제 신경망에서 강하다"는 데 있다.

### 7.5 PyTorch로 실제 네트워크에서 비교

같은 모델·같은 초기값·같은 mini-batch 순서로 SGD, SGD+momentum, Adam을 30 epoch(300 step) 학습해 loss와 정확도를 비교하는 코드다.

```python
# 같은 모델·같은 초기값에서 SGD / SGD+momentum / Adam을 mini-batch로 비교
import numpy as np, torch
rng = np.random.default_rng(0); t = rng.uniform(0, np.pi, 200)
X = np.vstack([np.c_[np.cos(t[:100]), np.sin(t[:100])],
               np.c_[1 - np.cos(t[100:]), 0.5 - np.sin(t[100:])]]) + rng.normal(0, 0.1, (200, 2))
X = torch.tensor(X, dtype=torch.float32); y = torch.cat([torch.zeros(100), torch.ones(100)])

def train(make_opt, epochs=30, bs=20):
    torch.manual_seed(0)
    model = torch.nn.Sequential(torch.nn.Linear(2, 16), torch.nn.ReLU(), torch.nn.Linear(16, 1))
    opt = make_opt(model.parameters())
    g = torch.Generator().manual_seed(1)
    for ep in range(epochs):
        for idx in torch.randperm(200, generator=g).split(bs):   # 1 epoch = 10 mini-batch
            loss = torch.nn.functional.binary_cross_entropy_with_logits(model(X[idx]).squeeze(1), y[idx])
            opt.zero_grad(); loss.backward(); opt.step()
    with torch.no_grad():
        logits = model(X).squeeze(1)
        full = torch.nn.functional.binary_cross_entropy_with_logits(logits, y).item()
        acc = ((logits > 0).float() == y).float().mean().item()
    return full, acc

for name, mk in [("SGD      lr=0.05", lambda p: torch.optim.SGD(p, lr=0.05)),
                 ("Momentum lr=0.05", lambda p: torch.optim.SGD(p, lr=0.05, momentum=0.9)),
                 ("Adam     lr=0.01", lambda p: torch.optim.Adam(p, lr=0.01))]:
    loss, acc = train(mk)
    print(f"{name}: 30 epochs(300 steps) 후 loss {loss:.4f}  acc {acc:.3f}")
```

```text
SGD      lr=0.05: 30 epochs(300 steps) 후 loss 0.3206  acc 0.850
Momentum lr=0.05: 30 epochs(300 steps) 후 loss 0.1872  acc 0.935
Adam     lr=0.01: 30 epochs(300 steps) 후 loss 0.1616  acc 0.935
```

출력에서 볼 것: 같은 step 수에서 momentum과 Adam이 plain SGD보다 loss가 훨씬 낮다. PyTorch 학습 루프의 기본형 `opt.zero_grad(); loss.backward(); opt.step()` 세 줄을 기억하자 — `zero_grad()`를 빼먹으면 gradient가 step마다 **누적**된다(PyTorch는 `.grad`에 더하는 방식이다).

### 7.6 Adam vs SGD — 실무 감각

| 항목 | SGD (+momentum) | Adam / AdamW |
|---|---|---|
| lr 민감도 | 높음 (잘 찾아야 함) | 낮음 (1e-3이 자주 통하는 출발점) |
| 수렴 속도 (초반) | 느린 편 | 빠른 편 |
| optimizer 메모리 | momentum 1개 (weight의 1배) | m, v 2개 (weight의 2배) |
| 많이 쓰는 곳 | CNN 이미지 분류(ResNet 류) 장기 학습 | Transformer/LLM, 빠른 프로토타입, 대부분의 fine-tuning |
| 비고 | 일반화가 조금 더 좋다는 보고가 있음 | weight decay를 분리한 AdamW가 사실상 표준 |

면접용 한 줄: "Adam은 기본값이 잘 작동해서 빠르게 반복하기 좋고, 잘 튜닝된 SGD+momentum은 비전 모델에서 비슷하거나 약간 나은 일반화를 보이기도 한다. 메모리가 빠듯하면 SGD가 optimizer 상태를 덜 쓴다."

### 7.7 learning rate schedule

learning rate를 학습 내내 고정하지 않고 바꾼다. 초반엔 크게(빨리 이동), 후반엔 작게(3.2절의 잡음 떨림을 줄이고 정밀하게 정착).

| schedule | 모양 | 언제 |
|---|---|---|
| step decay | N epoch마다 γ배(예: 0.1배)로 계단식 감소 | 고전적인 CNN 학습 |
| cosine annealing | 코사인 반주기로 부드럽게 0 근처까지 | 현재 가장 흔한 기본값 |
| warmup | 처음 몇백~몇천 step은 작은 lr에서 선형으로 올림 | Adam + 큰 모델. 초반 m, v 통계가 불안정할 때 폭주 방지 |

PyTorch 스케줄러가 step마다 lr을 어떻게 바꾸는지 출력하는 코드다.

```python
# PyTorch 학습률 스케줄러 3종이 step마다 lr을 어떻게 바꾸는지 출력
import torch, warnings
warnings.filterwarnings("ignore")   # SequentialLR 내부의 deprecation 경고 숨김
from torch.optim.lr_scheduler import StepLR, CosineAnnealingLR, LinearLR, SequentialLR

def trace(make_sched, steps=20):
    p = torch.nn.Parameter(torch.zeros(1))
    opt = torch.optim.SGD([p], lr=0.1)
    sched = make_sched(opt)
    lrs = []
    for _ in range(steps):
        lrs.append(opt.param_groups[0]["lr"])
        opt.step(); sched.step()           # 순서: optimizer.step() → scheduler.step()
    return " ".join(f"{v:.3f}" for v in lrs[::2])   # 짝수 step만 표시

print("step  :", trace(lambda o: StepLR(o, step_size=6, gamma=0.5)))
print("cosine:", trace(lambda o: CosineAnnealingLR(o, T_max=20)))
print("warmup:", trace(lambda o: SequentialLR(o, [LinearLR(o, start_factor=0.1, total_iters=4),
                                                 CosineAnnealingLR(o, T_max=16)], milestones=[4])))
```

```text
step  : 0.100 0.100 0.100 0.050 0.050 0.050 0.025 0.025 0.025 0.013
cosine: 0.100 0.098 0.090 0.079 0.065 0.050 0.035 0.021 0.010 0.002
warmup: 0.010 0.055 0.100 0.096 0.085 0.069 0.050 0.031 0.015 0.004
```

출력에서 볼 것(짝수 step만 표시): step은 6 step마다 절반, cosine은 0.1에서 0 근처까지 곡선으로, warmup은 0.01에서 4 step 동안 0.1까지 올라간 뒤 cosine으로 내려간다. 호출 순서는 `optimizer.step()` 다음에 `scheduler.step()`이다.

펌웨어 비유: 모터 제어의 **soft start**(warmup) + 목표 근처에서 gain을 줄이는 **gain scheduling**(decay)이다.

---

## 8. Vanishing / Exploding gradient

### 8.1 원인 — 곱이 길어지면

4.1절에서 chain rule은 **곱**이라고 했다. 레이어가 L개면 입력 쪽 gradient는 레이어별 local gradient를 L번 곱한 것이다.

```
 ∂L/∂W₁ ∝ (layer 20의 gain) × (layer 19의 gain) × … × (layer 2의 gain)

 gain이 매번 0.25 → 0.25¹⁹ ≈ 3.6e−12   (vanishing, 소실)
 gain이 매번 1.5  → 1.5¹⁹  ≈ 2.2e+3    (exploding, 폭주)
```

말로 하면: 앰프를 20단 직렬로 연결했는데 각 단 gain이 1보다 조금만 작아도 신호는 사라지고, 조금만 커도 포화된다. 딥러닝 초창기에 깊은 네트워크를 못 학습한 주된 이유다.

### 8.2 sigmoid는 왜 문제인가

```svg
<svg viewBox="0 0 640 310" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="260" x2="600" y2="260" stroke="currentColor"/><line x1="330.0" y1="260" x2="330.0" y2="30" stroke="currentColor"/>
<polyline points="60.0,259.9 76.9,259.9 93.8,259.8 110.6,259.7 127.5,259.5 144.4,259.1 161.2,258.5 178.1,257.6 195.0,256.0 211.9,253.6 228.8,249.6 245.6,243.3 262.5,233.8 279.4,219.9 296.2,200.8 313.1,176.9 330.0,150.0 346.9,123.1 363.8,99.2 380.6,80.1 397.5,66.2 414.4,56.7 431.2,50.4 448.1,46.4 465.0,44.0 481.9,42.4 498.8,41.5 515.6,40.9 532.5,40.5 549.4,40.3 566.2,40.2 583.1,40.1 600.0,40.1" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<polyline points="60.0,259.9 76.9,259.9 93.8,259.8 110.6,259.7 127.5,259.5 144.4,259.1 161.2,258.5 178.1,257.6 195.0,256.1 211.9,253.7 228.8,250.1 245.6,244.6 262.5,236.9 279.4,227.2 296.2,216.7 313.1,208.3 330.0,205.0 346.9,208.3 363.8,216.7 380.6,227.2 397.5,236.9 414.4,244.6 431.2,250.1 448.1,253.7 465.0,256.1 481.9,257.6 498.8,258.5 515.6,259.1 532.5,259.5 549.4,259.7 566.2,259.8 583.1,259.9 600.0,259.9" fill="none" stroke="#d0564a" stroke-width="2"/>
<text x="60.0" y="278" font-size="12" text-anchor="middle">-8</text><text x="195.0" y="278" font-size="12" text-anchor="middle">-4</text><text x="330.0" y="278" font-size="12" text-anchor="middle">0</text><text x="465.0" y="278" font-size="12" text-anchor="middle">4</text><text x="600.0" y="278" font-size="12" text-anchor="middle">8</text>
<line x1="60" y1="205.0" x2="600" y2="205.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/><text x="54" y="209.0" font-size="12" text-anchor="end">0.25</text><line x1="60" y1="150.0" x2="600" y2="150.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/><text x="54" y="154.0" font-size="12" text-anchor="end">0.5</text>
<line x1="60" y1="40.0" x2="600" y2="40.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/><text x="54" y="44.0" font-size="12" text-anchor="end">1.0</text><text x="404.2" y="55.4" font-size="12" fill="#4a7bd0">σ(z)</text><text x="350.2" y="199.0" font-size="12" fill="#d0564a">σ′(z) 최대 0.25</text><line x1="195.0" y1="40" x2="195.0" y2="260" stroke="#888" stroke-dasharray="4 4"/>
<line x1="465.0" y1="40" x2="465.0" y2="260" stroke="#888" stroke-dasharray="4 4"/><text x="127.5" y="128.0" font-size="12" text-anchor="middle">포화 영역</text><text x="127.5" y="144.0" font-size="12" text-anchor="middle">σ′ &lt; 0.018</text><text x="532.5" y="128.0" font-size="12" text-anchor="middle">포화 영역</text><text x="532.5" y="144.0" font-size="12" text-anchor="middle">σ′ &lt; 0.018</text>
<text x="600" y="294" font-size="13" text-anchor="end">z</text>
</svg>
```

그림 5 — 파랑 σ(z), 빨강 σ′(z) = σ(z)(1−σ(z)). 미분의 최댓값이 z=0에서 0.25뿐이고, |z| > 4인 포화 영역에서는 0.018보다 작다.

sigmoid를 거칠 때마다 gradient가 **최대 1/4로** 줄어든다. 입력이 포화 영역이면 거의 0이 된다. 반면 ReLU는 켜져 있으면 미분이 정확히 1이라 곱해도 줄지 않는다. 이것이 hidden layer에 ReLU(와 그 변형 GELU, SiLU 등)를 쓰는 가장 큰 이유다. (sigmoid는 5절처럼 **출력층**에서 확률을 낼 때만 쓴다.)

### 8.3 초기화 — 각 층의 gain을 1 근처로

weight를 무작위로 초기화할 때 분산을 잘 고르면 층마다 신호(와 gradient)의 크기가 유지된다. 입력 개수(fan-in)를 n이라 할 때:

| 방법 | weight 분산 | 짝이 되는 활성함수 | 아이디어 |
|---|---|---|---|
| Xavier / Glorot | 1/n (원 논문은 2/(n_in + n_out)) | tanh, (sigmoid) | n개를 더하면 분산이 n배 → 1/n로 상쇄 |
| He / Kaiming | 2/n | ReLU | ReLU가 절반을 0으로 만들어 분산이 1/2 → 2배로 보상 |

말로 하면: 입력 n개에 weight를 곱해서 더하면 출력 분산이 대략 `n × Var(w) × Var(입력)`이다. `Var(w) = 1/n`이면 출력 분산 = 입력 분산, 즉 **층의 gain이 1**이 된다.

깊이 20층, 폭 128의 MLP에서 활성함수와 초기화 표준편차를 바꿔 가며 입력 쪽(layer1)·가운데(layer10)·출력 쪽(layer20) weight의 gradient 크기를 재는 코드다.

```python
# 깊이 20층 MLP에서 활성함수·초기화에 따라 층별 gradient 크기가 어떻게 달라지나
import torch, math
torch.manual_seed(0)
x = torch.randn(64, 128)                       # batch 64, 폭 128

def grad_norms(act, std_fn, depth=20, width=128):
    layers = []
    for _ in range(depth):
        lin = torch.nn.Linear(width, width, bias=False)
        torch.nn.init.normal_(lin.weight, 0.0, std_fn(width))
        layers += [lin, act()]
    net = torch.nn.Sequential(*layers)
    net(x).pow(2).mean().backward()
    g = [m.weight.grad.norm().item() for m in net if isinstance(m, torch.nn.Linear)]
    return g[0], g[depth // 2], g[-1]           # 입력쪽 / 가운데 / 출력쪽

cases = [("sigmoid, std=0.01      ", torch.nn.Sigmoid, lambda n: 0.01),
         ("sigmoid, Xavier        ", torch.nn.Sigmoid, lambda n: math.sqrt(1 / n)),
         ("ReLU,    std=0.01      ", torch.nn.ReLU,    lambda n: 0.01),
         ("ReLU,    He sqrt(2/n)  ", torch.nn.ReLU,    lambda n: math.sqrt(2 / n)),
         ("ReLU,    std=sqrt(4/n) ", torch.nn.ReLU,    lambda n: math.sqrt(4 / n))]
print("                         layer1     layer10    layer20")
for name, act, std in cases:
    a, b, c = grad_norms(act, std)
    print(f"{name}  {a:9.2e}  {b:9.2e}  {c:9.2e}")
```

```text
                         layer1     layer10    layer20
sigmoid, std=0.01         0.00e+00   1.97e-15   1.25e-01
sigmoid, Xavier           3.76e-14   2.91e-07   1.27e-01
ReLU,    std=0.01         0.00e+00   0.00e+00   0.00e+00
ReLU,    He sqrt(2/n)     9.37e-01   3.48e+00   1.38e+00
ReLU,    std=sqrt(4/n)    3.99e+05   8.56e+05   4.34e+05
```

출력에서 볼 것:

- sigmoid는 Xavier로 초기화해도 layer1의 gradient가 3.8e−14 — 출력 쪽보다 **13자릿수** 작다. 층마다 σ′ ≤ 0.25가 곱해지니 초기화로는 못 막는다.
- ReLU + std 0.01(너무 작음)은 forward 신호 자체가 20층을 지나며 0으로 사라져 gradient가 전부 0이다.
- ReLU + He는 세 층 모두 같은 자릿수(0.9 ~ 3.5)다. **건강한 상태**.
- ReLU + std √(4/n)(He의 √2배)은 3.99e+05로 **폭주**한다. 층 gain이 √2만 커도 20층이면 이렇게 된다.

### 8.4 gradient clipping과 그 밖의 도구

- **gradient clipping**: 전체 gradient 벡터의 norm이 임계값을 넘으면 방향은 유지하고 길이만 잘라 낸다. RNN·Transformer 학습에서 표준. 펌웨어의 **출력 slew-rate limit / saturation** 과 같다.
- **정규화 레이어(BatchNorm, LayerNorm)**: 층마다 activation 분포를 평균 0·분산 1 근처로 다시 맞춘다. 초기화에 덜 민감해진다.
- **residual connection (y = x + f(x))**: 덧셈 노드는 gradient를 그대로 복사한다(4.3절) → 입력 쪽까지 "우회로"가 생겨 소실이 크게 줄어든다. ResNet과 Transformer가 깊어질 수 있는 이유.

clipping과, 9절에서 쓸 STE를 함께 확인하는 코드다.

```python
# (1) gradient clipping: norm이 max_norm을 넘으면 방향은 유지하고 길이만 줄인다
import torch
p = torch.nn.Parameter(torch.zeros(2))
p.grad = torch.tensor([30.0, 40.0])                      # norm = 50 (폭주한 gradient)
total = torch.nn.utils.clip_grad_norm_([p], max_norm=5.0)
print("clip 전 norm:", total.item(), "→ clip 후 grad:", p.grad, "norm:", p.grad.norm().item())

# (2) round()는 미분이 거의 모든 곳에서 0 → 학습 신호가 끊긴다
w = torch.tensor([0.3, 1.7, -0.6], requires_grad=True)
torch.round(w).sum().backward()
print("round의 grad        :", w.grad)

# (3) STE(straight-through estimator): forward는 round, backward는 항등(1)으로 통과
w.grad = None
w_q = w + (torch.round(w) - w).detach()                  # 값 = round(w), 미분 = 1
w_q.sum().backward()
print("STE forward 값      :", w_q.detach(), " grad:", w.grad)
```

```text
clip 전 norm: 50.0 → clip 후 grad: tensor([3.0000, 4.0000]) norm: 5.0
round의 grad        : tensor([0., 0., 0.])
STE forward 값      : tensor([ 0.,  2., -1.])  grad: tensor([1., 1., 1.])
```

출력에서 볼 것: (1) norm 50인 gradient `[30, 40]`이 방향은 그대로 norm 5인 `[3, 4]`로 줄었다. (2)(3)은 9.3절에서 설명한다.

---

## 9. 학습 vs 추론의 메모리 — 그리고 QAT 예고

### 9.1 학습은 무엇을 더 들고 있어야 하나

4.3절: 곱셈 노드의 backward에는 forward 입력값이 필요하다. 그래서 학습 중에는 **모든 레이어의 activation을 backward가 끝날 때까지** 메모리에 들고 있어야 한다. 추론은 레이어 i의 출력을 레이어 i+1에 넘기면 바로 버릴 수 있다(TFLite Micro의 tensor arena가 버퍼를 재사용하는 이유, F2 참고).

| 항목 | 추론 | 학습 (Adam, FP32) |
|---|---|---|
| weight | 1× (INT8이면 FP32의 1/4) | 1× (FP32) |
| gradient | 없음 | 1× |
| optimizer 상태 | 없음 | 2× (Adam의 m, v) |
| activation | 가장 큰 연속 2개 층 정도 (ping-pong) | **모든 층 × batch 크기** |
| 정밀도 | INT8 가능 | 보통 FP32/BF16 (작은 gradient가 INT8에선 0으로 사라짐) |

PyTorch의 `saved_tensors_hooks`로 autograd가 backward용으로 저장하는 텐서를 실제로 세어 보는 코드다.

```python
# backward를 위해 autograd가 "저장해 두는" activation이 몇 바이트인지 직접 세어 본다
import torch
torch.manual_seed(0)
model = torch.nn.Sequential(torch.nn.Linear(256, 512), torch.nn.ReLU(),
                            torch.nn.Linear(512, 512), torch.nn.ReLU(),
                            torch.nn.Linear(512, 10))
params = list(model.parameters())
param_ptrs = {p.data_ptr() for p in params}
W = sum(p.numel() * p.element_size() for p in params) / 1024   # KiB

saved = []
def pack(t):                                   # 저장되는 텐서마다 호출됨
    if t.data_ptr() not in param_ptrs:         # weight는 이미 메모리에 있으니 제외
        saved.append(t.numel() * t.element_size())
    return t

for batch in [1, 32, 256]:
    saved.clear()
    x = torch.randn(batch, 256)
    with torch.autograd.graph.saved_tensors_hooks(pack, lambda t: t):
        loss = model(x).logsumexp(1).mean()    # 학습 모드 forward
    print(f"batch {batch:3d}: saved activations {sum(saved)/1024:7.1f} KiB ({len(saved)} tensors)")

print(f"weights {W:.1f} KiB | grads {W:.1f} KiB | Adam m,v {2*W:.1f} KiB")
saved.clear()
with torch.no_grad(), torch.autograd.graph.saved_tensors_hooks(pack, lambda t: t):
    model(torch.randn(32, 256))
print("no_grad(추론)에서 저장된 텐서 수:", len(saved))
```

```text
batch   1: saved activations     9.0 KiB (7 tensors)
batch  32: saved activations   289.4 KiB (7 tensors)
batch 256: saved activations  2315.0 KiB (7 tensors)
weights 1560.0 KiB | grads 1560.0 KiB | Adam m,v 3120.1 KiB
no_grad(추론)에서 저장된 텐서 수: 0
```

출력에서 볼 것: weight가 1560 KiB(약 40만 파라미터 × 4 B)인데 학습에는 gradient 1560 KiB + Adam 상태 3120 KiB가 더 붙어 **weight만 4배**가 된다. 저장된 activation은 batch에 **비례**한다(batch 256이면 2.3 MiB). `no_grad()`(추론)에서는 저장되는 텐서가 0개다. 합치면 batch 32 학습에 약 6.4 MiB, 추론에는 weight 1.5 MiB(INT8이면 0.4 MiB) + 작은 버퍼면 된다.

### 9.2 그래서 on-device 학습은 드물다

- **메모리**: 위처럼 추론의 수 배~수십 배. Cortex-M의 SRAM은 수백 KB다.
- **연산**: backward ≈ forward의 2배(5.7절) → 학습 1 step ≈ 추론 3번 + optimizer 업데이트.
- **정밀도**: NPU/DSP는 INT8 추론에 최적화되어 있고 backward 연산을 지원하지 않는 경우가 많다.
- **라벨**: 기기에서는 정답(y)을 얻기 어렵다.
- **전력·열**: 웨어러블 배터리 예산에 치명적이다.

그래서 보통은 "클라우드/워크스테이션에서 학습 → 양자화 → 기기에서 추론"이다. 예외는 **개인화(personalization)**: 예를 들어 Hark 같은 웨어러블이라면 사용자 목소리·제스처에 맞추려고 마지막 층(또는 bias)만 소량 fine-tuning하는 방식이 연구되고 있다(예: MIT Han Lab의 "On-Device Training Under 256KB Memory"). 이것도 "대부분 층을 freeze해서 activation 저장과 gradient 계산을 없앤다"는 발상이다.

### 9.3 QAT와 STE 미리보기 (C2에서 자세히)

INT8로 양자화하면 정확도가 떨어질 수 있다. **QAT(quantization-aware training)** 는 학습(보통 fine-tuning) 도중 forward에 "양자화했다가 다시 float로 되돌리는" **fake quant**를 넣어서, 모델이 양자화 오차에 적응하게 만드는 방법이다.

문제는 양자화의 핵심인 `round()`의 미분이 거의 모든 곳에서 **0**이라는 것이다(계단 함수는 평평하다). 그러면 chain rule로 곱해지는 순간 gradient가 전부 0이 되어 학습이 멈춘다. 8.4절 코드의 (2)가 그걸 보여 준다: `round`의 grad가 `[0, 0, 0]`.

**STE(straight-through estimator)** 는 "forward에서는 round를 적용하되, backward에서는 round가 없었던 것처럼(미분 = 1) gradient를 그냥 통과시킨다"는 근사다. 코드 (3)의 `w + (round(w) − w).detach()` 트릭이 그것이다 — `.detach()`된 부분은 backward에서 상수 취급되므로 값은 `round(w)`인데 미분은 `∂w/∂w = 1`이 된다. 출력의 `[0, 2, −1]`과 grad `[1, 1, 1]`을 확인하자. 이 한 줄이 QAT의 핵심이다.

---

## 10. 임베디드 관점에서 다시 보기

### 10.1 학습 루프도 결국 C 루프다

PyTorch의 마법처럼 보이는 학습도 벗겨 보면 "forward 누적 → gradient 누적 → 빼기"의 이중 루프다. 뉴런 1개(logistic regression)를 AND 게이트 4개 샘플로 학습하는 C 코드다. 5.3절의 `dz = p − y`가 그대로 나온다.

```c
/* 뉴런 1개(logistic regression)를 C로 학습: AND 게이트 4샘플 */
#include <stdio.h>
#include <math.h>

static const float X[4][2] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};
static const float Y[4]    = {0, 0, 0, 1};

int main(void) {
    float w0 = 0.0f, w1 = 0.0f, b = 0.0f, lr = 1.0f;
    for (int step = 0; step <= 2000; step++) {
        float gw0 = 0, gw1 = 0, gb = 0, loss = 0;
        for (int i = 0; i < 4; i++) {                    /* forward + backward */
            float z = w0 * X[i][0] + w1 * X[i][1] + b;
            float p = 1.0f / (1.0f + expf(-z));
            loss += -(Y[i] * logf(p) + (1 - Y[i]) * logf(1 - p));
            float dz = p - Y[i];                         /* BCE+sigmoid 미분 */
            gw0 += dz * X[i][0]; gw1 += dz * X[i][1]; gb += dz;
        }
        w0 -= lr * gw0 / 4; w1 -= lr * gw1 / 4; b -= lr * gb / 4;  /* update */
        if (step % 500 == 0)
            printf("step %4d loss %.4f  w=(%.3f, %.3f) b=%.3f\n", step, loss / 4, w0, w1, b);
    }
    for (int i = 0; i < 4; i++)
        printf("x=(%g,%g) p=%.3f\n", X[i][0], X[i][1],
               1.0f / (1.0f + expf(-(w0 * X[i][0] + w1 * X[i][1] + b))));
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 gd.c -o gd -lm && ./gd
```

```text
step    0 loss 0.6931  w=(0.000, 0.000) b=-0.250
step  500 loss 0.0346  w=(6.021, 6.021) b=-9.209
step 1000 loss 0.0174  w=(7.416, 7.416) b=-11.297
step 1500 loss 0.0116  w=(8.239, 8.239) b=-12.529
step 2000 loss 0.0087  w=(8.822, 8.822) b=-13.404
x=(0,0) p=0.000
x=(0,1) p=0.010
x=(1,0) p=0.010
x=(1,1) p=0.986
```

출력에서 볼 것: loss가 0.69(= ln 2, 아무것도 모를 때 50% 확률의 BCE) → 0.0087로 줄고, (1,1)만 확률 0.986, 나머지는 0.01 이하가 된다. w0 = w1인 것은 데이터가 대칭이기 때문이다. 경고 0개로 컴파일된다.

### 10.2 수치 감각 — Don의 경험과 연결

| 학습 개념 | 펌웨어/하드웨어 대응 | 배포 시 의미 |
|---|---|---|
| learning rate | 제어 루프 gain, 안정 조건 η < 2/λ | 모델 팀의 lr 튜닝 = PID 튜닝과 같은 사고방식 |
| mini-batch 잡음 | ADC 샘플 평균 개수 | batch 크기 ↑ = 메모리 ↑ (activation ∝ batch) |
| momentum | IIR low-pass filter | optimizer 상태 메모리 1× |
| Adam의 √v 정규화 | AGC (자동 이득 제어) | optimizer 상태 메모리 2× |
| gradient clipping | saturation / slew-rate limit | 학습 안정성 |
| vanishing gradient | 다단 앰프에서 gain < 1이 누적 | 깊은 모델은 residual·norm이 필요 → NPU 연산자 지원 확인 |
| backward = GEMM 2개 | forward의 2배 MAC | 학습 FLOPs ≈ 3 × 추론 FLOPs |
| activation 저장 | DMA 버퍼를 해제 못 하고 전부 들고 있기 | on-device 학습이 드문 이유 |
| STE | 양자화기 앞뒤를 "투명"하게 모델링 | QAT로 INT8 정확도 회복 (C2) |

### 10.3 배포 엔지니어가 학습에서 챙길 것

- **양자화 친화적 학습**: 극단적인 weight/activation 값(outlier)은 INT8 범위를 낭비한다. weight decay, clipping, 적절한 활성함수(ReLU6 등)는 나중에 양자화를 쉽게 한다.
- **NPU 연산자 지원**: 학습에서 편하다고 쓴 연산(특이한 normalization, 동적 shape)이 NPU에서 지원되지 않으면 CPU fallback으로 느려진다. 학습 전에 타깃 연산자 목록을 모델 팀과 공유한다(I 모듈).
- **재현성**: seed 고정, 데이터 순서 고정. 펌웨어 회귀 테스트처럼 "같은 입력 → 같은 결과"가 보장되어야 양자화 전후 비교가 의미 있다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| learning rate 과대 | loss가 튀다가 NaN/inf | pole 절댓값 > 1, 발산 | lr 1/10로, warmup 추가, gradient clipping |
| learning rate 과소 | loss가 아주 천천히만 감소 | pole ≈ 1 | lr을 3~10배씩 올려 보는 lr sweep |
| `zero_grad()` 누락 | loss가 이상하게 튀거나 발산 | PyTorch는 `.grad`에 누적 | 매 step `opt.zero_grad()` |
| 입력 정규화 안 함 | 특정 방향만 지그재그, lr을 못 올림 | feature 스케일 차이 → ill-conditioning | 채널별 평균 0·분산 1로 정규화 (통계는 학습 데이터로) |
| sigmoid/tanh를 깊은 hidden에 사용 | 입력 쪽 layer의 gradient ≈ 0, 학습 정체 | σ′ ≤ 0.25 누적 곱 | ReLU/GELU, residual, normalization |
| 초기화 스케일 오류 | 처음부터 loss가 안 변하거나 inf | 층 gain ≠ 1 | Xavier/He 초기화 (PyTorch 기본값을 함부로 바꾸지 않기) |
| 직접 짠 backward 검증 안 함 | 학습은 되는데 성능이 이상하게 낮음 | 부호·전치·축 실수 | 작은 입력으로 수치 gradient check (5.5절) |
| sigmoid와 BCE를 따로 계산 | 큰 logit에서 log(0) → inf/NaN | 수치 불안정 | `binary_cross_entropy_with_logits` 사용 |
| 학습 정확도만 보고 판단 | 기기에서 정확도가 훨씬 낮음 | overfitting (A4) | validation/test split, 조기 종료 |
| round를 그냥 넣고 QAT | 양자화 파라미터가 전혀 학습되지 않음 | round의 미분 0 | STE 또는 프레임워크 QAT API 사용 (C2) |

---

## 12. 면접에서 이렇게 말한다

**Q.** "Explain backpropagation in one minute."

**A.** 학습 목표는 loss를 줄이는 방향으로 모든 weight를 움직이는 것이고, 그러려면 각 weight에 대한 loss의 편미분이 필요하다. 네트워크는 곱셈·덧셈·활성함수 같은 단순 연산의 그래프다. forward에서 중간값을 저장하고, backward에서 출력 쪽부터 chain rule로 각 노드의 local gradient를 곱해 가며 입력 쪽으로 전파한다. 중간 gradient를 공유하기 때문에 forward 약 1번 + backward 약 2번의 비용으로 모든 파라미터의 gradient를 한 번에 얻는다.

> Backprop is just the chain rule applied efficiently. The network is a graph of simple ops; in the forward pass we compute and cache intermediate activations, and in the backward pass we start from dL/dL = 1 at the output and multiply by each op's local gradient as we move toward the inputs — for a linear layer that's two matmuls, one for dW and one for the gradient to pass upstream. Because upstream gradients are reused, we get every parameter's gradient for roughly two to three forward passes' worth of compute, instead of one forward pass per parameter with finite differences.

**Q.** "Why does training use much more memory than inference?"

**A.** 세 가지다. 첫째, backward에 forward의 activation이 필요해서 모든 층의 중간값을 batch 크기만큼 들고 있어야 한다. 추론은 층을 지나면 바로 버릴 수 있다. 둘째, weight마다 gradient가 하나씩 더 있고, Adam이면 m, v 두 개가 더 있어서 weight 관련 메모리만 4배다. 셋째, 학습은 보통 FP32/BF16이고 추론은 INT8까지 내릴 수 있다. 그래서 on-device 학습은 드물고, 한다면 대부분 층을 freeze하고 마지막 층만 fine-tuning한다.

> Inference can free each activation as soon as the next layer consumes it, but training has to keep every layer's activations — times the batch size — until the backward pass, because the gradient of a multiply needs the forward inputs. On top of that you store a gradient per weight and, with Adam, two more moment buffers, so weight-related memory is about 4x, and it's usually FP32 or BF16 rather than INT8. In a small experiment I measured it with PyTorch's saved-tensor hooks: 1.5 MB of weights turned into about 6.4 MB of training state at batch 32. That's why on-device training is rare, and when it's done it's usually last-layer fine-tuning with everything else frozen.

**Q.** "What happens if the learning rate is too high? Too low?"

**A.** 2차 근사에서 경사하강법은 매 step 오차에 (1 − η·곡률)을 곱하는 이산 시스템이다. η가 2/곡률을 넘으면 |pole| > 1이라 최소점을 넘어 반대편으로 튀면서 발산하고, 실무에선 loss가 튀다가 NaN이 된다. 너무 작으면 pole이 1에 가까워 수렴이 매우 느리다. 가장 가파른 방향이 상한을 정하므로, 발산 직전 lr을 찾고 그보다 몇 배 낮게 쓰고, warmup과 decay schedule을 쓴다.

> Near a minimum, gradient descent behaves like a discrete-time loop where the error gets multiplied by one minus the learning rate times the curvature each step. If the learning rate exceeds two over the largest curvature, that factor's magnitude goes above one and the updates overshoot and diverge — you see the loss spike and turn into NaN. Too low and the factor is close to one, so progress crawls. It's exactly like loop gain in a controller: I'd find where it starts to oscillate, back off by several x, and use warmup plus a decay schedule like cosine.

**Q.** "Adam vs SGD — which would you use and why?"

**A.** Adam은 momentum(1차 모멘트)과 파라미터별 gradient 크기 정규화(2차 모멘트)를 합쳐서, step 크기가 대략 lr 정도로 묶이고 lr에 덜 민감하다. 그래서 프로토타입이나 Transformer, fine-tuning 기본값으로 쓴다. SGD+momentum은 잘 튜닝하면 비전 모델에서 일반화가 비슷하거나 약간 낫다는 결과가 있고, optimizer 상태가 weight의 1배라 메모리가 적다. 보통 AdamW로 시작하고, 메모리나 최종 정확도가 문제면 SGD를 비교한다.

> Adam keeps a running mean of the gradient, which acts like momentum, and a running mean of the squared gradient, which normalizes the step per parameter — so the effective step size is roughly the learning rate regardless of gradient scale, and it's much less sensitive to tuning. That makes AdamW my default for prototyping, transformers and fine-tuning. Well-tuned SGD with momentum can match or slightly beat it on vision models and only needs one state buffer instead of two, so if optimizer memory or final accuracy matters, I'd compare both.

**Q.** "What are vanishing and exploding gradients, and how do you fix them?"

**A.** backprop은 층마다 local gradient를 곱하므로, 층 gain이 1보다 작으면 입력 쪽 gradient가 지수적으로 사라지고 크면 폭주한다. sigmoid는 미분 최대 0.25라 특히 심하다. 해결책은 ReLU 계열 활성함수, He/Xavier 초기화로 층 gain을 1 근처로 맞추기, BatchNorm/LayerNorm, residual connection(덧셈이 gradient를 그대로 통과시킴), 그리고 폭주에는 gradient clipping이다.

> Because backprop multiplies one local gradient per layer, if the typical per-layer gain is below one the gradient reaching early layers shrinks exponentially, and above one it blows up. Sigmoid is the classic culprit since its derivative is at most 0.25. The fixes are ReLU-style activations, He or Xavier initialization so each layer's gain is about one, normalization layers, residual connections that give the gradient an identity path, and gradient-norm clipping for the exploding case.

**Q.** "How can you train through a quantizer if rounding has zero gradient?"

**A.** round는 계단 함수라 미분이 거의 모든 곳에서 0이고, 그대로면 QAT에서 gradient가 끊긴다. STE는 forward에서는 양자화를 그대로 적용하고 backward에서는 항등 함수처럼 gradient를 통과시키는 근사다(보통 클리핑 범위 밖은 0). PyTorch에선 `x + (q(x) − x).detach()`로 구현할 수 있다.

> The rounding step has zero gradient almost everywhere, so a naive fake-quant would stop learning. The straight-through estimator keeps the quantize-dequantize in the forward pass but treats it as identity in the backward pass — typically passing the gradient through inside the clipping range and zeroing it outside. In PyTorch it's the detach trick: x plus the detached difference between q(x) and x.

---

## 13. 직접 해보기

1. **손계산**: `L(w) = 2(w − 1)²`, `w₀ = 3`, `η = 0.1`로 경사하강법 2 step을 계산하라. 그리고 이 문제에서 발산하지 않는 η의 범위를 구하라.
정답: gradient 4(w−1), w₁ = 3 − 0.1·8 = 2.2, w₂ = 2.2 − 0.1·4.8 = 1.72 (오차가 매번 0.6배). λ = 4이므로 0 < η < 0.5.

2. **손계산**: 4.2절 그래프에서 `w=1, x=−2, b=0, y=1`일 때 `∂L/∂w`, `∂L/∂b`를 구하라.
정답: u=−2, z=−2, e=−3, L=9 → ∂L/∂e=−6, ∂L/∂b=−6, ∂L/∂w=−6·x=12.

3. **손계산**: 5절 네트워크에서 입력만 `x = [2.0, 0.0]`으로 바꿨을 때 h와 ∂L/∂W1을 구하라(y=1).
정답: z1=[0.5, −1.0], h=[0.5, 0], z2=0.35, p=σ(0.35)≈0.5866, ∂L/∂z2≈−0.4134, ∂L/∂z1=[−0.2480, 0], ∂L/∂W1≈[[−0.4961, 0], [0, 0]] (x₂=0이라 두 번째 열도 0). 코드로 검산할 것.

4. **코드**: 6절 numpy MLP의 ReLU를 tanh로 바꿔라(backward의 `(z1 > 0)`을 `1 − h²`로). 학습 곡선이 어떻게 달라지는지 보고, 5.5절 방식의 gradient check로 backward가 맞는지 확인하라.
힌트: tanh′(z) = 1 − tanh²(z). 초기화는 Xavier(`np.sqrt(1/2)`)가 짝이다.

5. **코드**: 7.4절 코드에 "momentum with β = 0.99"를 추가하고 lr=0.01에서 결과를 비교하라. 왜 overshoot가 커지는지 7.2절의 "질량이 있는 2차 시스템" 관점에서 설명하라.
힌트: β가 1에 가까울수록 감쇠(damping)가 작아진다. v는 과거 gradient를 약 1/(1−β) = 100 step 누적한다.

6. **코드**: 9.1절 코드에서 모델을 `Linear(256, 1024)`로 넓히고 batch를 1, 32, 256으로 바꿔 가며, "weight 관련 메모리 vs activation 메모리"가 역전되는 batch 크기를 찾아라.
힌트: weight 관련(weight+grad+Adam)은 batch와 무관, activation은 batch에 비례한다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| derivative (미분) | 순간 변화율 | 입력을 살짝 바꿀 때 출력이 변하는 비율 = 민감도 |
| partial derivative (편미분) | 한 변수에 대한 미분 | 나머지 변수는 고정 |
| gradient | 편미분 벡터 `∇L` | 가장 가파른 오르막 방향. 반대로 가면 내리막 |
| gradient descent (경사하강법) | `W ← W − η∇L` | gradient 반대 방향으로 조금씩 이동 |
| learning rate (η, 학습률) | step 크기 배율 | 제어 루프 gain과 같은 역할. η < 2/λ |
| loss landscape | loss를 파라미터 공간 위의 지형으로 본 것 | 그릇, 웅덩이, 안장점 |
| saddle point (안장점) | 어떤 방향은 최소, 어떤 방향은 최대인 점 | 고차원에서 흔하고 학습을 정체시킴 |
| chain rule | 합성함수 미분 = 미분의 곱 | 신호 체인 gain을 곱하는 것 |
| computational graph | 연산 노드의 그래프 | forward는 값, backward는 gradient를 흘림 |
| backpropagation | 출력부터 chain rule을 적용하는 계산 순서 | 모든 파라미터 gradient를 forward 약 3번 비용으로 |
| autograd | 자동 미분 | PyTorch가 그래프를 기록하고 backward를 자동 수행 |
| gradient check | 수치 미분과 비교 검증 | 직접 짠 backward의 단위 테스트 |
| epoch | 학습 데이터 전체 1회 순회 | N/B step |
| mini-batch | 한 step에 쓰는 샘플 묶음 | 잡음 ∝ 1/√B |
| SGD | stochastic gradient descent | 보통 mini-batch GD를 뜻함 |
| momentum | 과거 gradient 누적 | IIR low-pass + 관성 |
| Adam | 1·2차 모멘트 기반 optimizer | 파라미터별 step 크기 자동 조절 |
| lr schedule | 학습 중 lr 변경 규칙 | step decay, cosine, warmup |
| vanishing / exploding gradient | gradient 소실 / 폭주 | 층별 gain의 누적 곱 |
| Xavier / He init | 층 gain을 1로 맞추는 초기화 | 분산 1/n, 2/n |
| gradient clipping | gradient norm 상한 | 폭주 방지. saturation |
| activation memory | backward용으로 저장한 중간값 | batch에 비례, 학습 메모리의 큰 부분 |
| QAT | quantization-aware training | 학습 중 fake quant로 양자화 오차에 적응 |
| STE | straight-through estimator | round의 backward를 항등으로 근사 |

---

## 15. 요약 & 체크리스트

학습은 "forward → loss → backward → update"를 반복하는 폐루프다. gradient는 각 파라미터에 대한 loss의 민감도 벡터이고, 경사하강법은 그 반대 방향으로 learning rate만큼 움직인다. 2차 근사에서 이것은 pole이 `1 − ηλ`인 이산 시스템이라, lr이 `2/λ`를 넘으면 발산하고 너무 작으면 느리다. backprop은 chain rule을 출력부터 적용해 중간 gradient를 공유하는 계산 순서로, 곱셈 노드가 forward 입력을 필요로 하기 때문에 activation을 저장해야 한다 — 이것이 학습이 추론보다 메모리를 훨씬 많이 쓰는 근본 이유다. mini-batch 잡음은 비용을 줄이고 안장점 탈출을 돕는다. momentum은 gradient를 low-pass 필터링하고, Adam은 여기에 파라미터별 크기 정규화를 더한다. 깊은 네트워크에서는 층별 gain의 곱이 gradient를 소실·폭주시키므로 ReLU, He/Xavier 초기화, normalization, residual, clipping을 쓴다. 양자화의 round는 미분이 0이라 QAT는 STE로 gradient를 통과시킨다.

- [ ] `L(w)=(w−3)²`에서 경사하강법 3 step을 손으로 계산할 수 있다
- [ ] "w를 ε 건드리면 L은 약 기울기×ε 변한다"를 예로 설명할 수 있다
- [ ] 안정 조건 η < 2/λ를 pole 관점에서 유도할 수 있다
- [ ] 계산 그래프에서 덧셈·곱셈·ReLU 노드의 backward 규칙을 말할 수 있다
- [ ] 2층 네트워크의 dW1, dW2를 손으로 계산하고 수치 미분으로 검증할 수 있다
- [ ] numpy로 forward·backward·update 학습 루프를 처음부터 짤 수 있다
- [ ] momentum과 Adam의 업데이트 식과 각 항의 역할을 쓸 수 있다
- [ ] step / cosine / warmup schedule을 그림으로 그릴 수 있다
- [ ] vanishing gradient의 원인과 해결책 4개를 말할 수 있다
- [ ] 학습 메모리(weight, grad, optimizer, activation)를 바이트로 추정하고 STE를 설명할 수 있다

---

## 참고 자료

- Goodfellow, Bengio, Courville, **Deep Learning** (MIT Press, 2016) — 4장(Numerical Computation, gradient 기반 최적화), 6.5절(Back-Propagation), 8장(Optimization for Training Deep Models). [deeplearningbook.org](https://www.deeplearningbook.org/)
- **Dive into Deep Learning** (d2l.ai) — "Optimization Algorithms" 장(SGD, momentum, Adam, schedule을 코드와 함께). [d2l.ai](https://d2l.ai/)
- Stanford **CS231n** 강의 노트 — "Backpropagation, Intuitions" (계산 그래프 게이트별 규칙). [cs231n.github.io/optimization-2](https://cs231n.github.io/optimization-2/)
- **3Blue1Brown**, Neural Networks 시리즈 — gradient descent와 backpropagation 편. [3blue1brown.com/topics/neural-networks](https://www.3blue1brown.com/topics/neural-networks)
- Andrej Karpathy, **Neural Networks: Zero to Hero** — 첫 강의 "The spelled-out intro to neural networks and backpropagation: building micrograd". [karpathy.ai/zero-to-hero.html](https://karpathy.ai/zero-to-hero.html)
- PyTorch 문서 — [Autograd mechanics](https://pytorch.org/docs/stable/notes/autograd.html), [torch.optim (optimizer와 lr_scheduler)](https://pytorch.org/docs/stable/optim.html)
- Kingma & Ba, "Adam: A Method for Stochastic Optimization" (ICLR 2015). [arXiv:1412.6980](https://arxiv.org/abs/1412.6980)
- Glorot & Bengio, "Understanding the difficulty of training deep feedforward neural networks" (AISTATS 2010) — Xavier 초기화
- He et al., "Delving Deep into Rectifiers" (ICCV 2015) — He 초기화. [arXiv:1502.01852](https://arxiv.org/abs/1502.01852)
- Bengio, Léonard, Courville, "Estimating or Propagating Gradients Through Stochastic Neurons for Conditional Computation" (2013) — STE. [arXiv:1308.3432](https://arxiv.org/abs/1308.3432)
- Lin et al., "On-Device Training Under 256KB Memory" (NeurIPS 2022) — MCU에서의 학습
- MIT **6.5940 TinyML and Efficient Deep Learning** (Song Han) — 양자화·QAT·on-device training 강의. [efficientml.ai](https://efficientml.ai/)
