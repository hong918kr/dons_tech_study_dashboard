# B1. MLP · 활성화 함수 · 정규화 — 모든 모델의 부품

> **이 노트를 다 읽으면**: MLP 한 층을 손으로 계산하고 "활성화 함수가 없으면 층을 쌓아도 한 층과 같다"를 숫자로 보일 수 있다 · sigmoid부터 hard-swish까지 활성화 함수의 모양·범위·비용·양자화 친화도를 비교하고 C(LUT 포함)로 구현할 수 있다 · BatchNorm/LayerNorm/RMSNorm/GroupNorm이 어느 축으로 평균을 내는지 그리고 BN의 train/eval 차이를 설명할 수 있다 · BN folding 공식을 유도하고 코드로 검증할 수 있다
> **JD 연결**: "Co-design model architectures that meet latency, memory, power, bandwidth", "Optimizing models for MCUs & edge processors" — study_prep_list B1 행: fully-connected, ReLU/GELU/SiLU / BatchNorm, LayerNorm, RMSNorm / **BN folding** (conv에 합치기). C6(op fusion: conv+BN+ReLU)의 직접적인 전제.
> **Don 기준 난이도**: dense layer = 행렬-벡터 곱, 고정소수점 clamp·LUT는 펌웨어에서 매일 하던 것(익숙) / 활성화 함수를 고르는 이유, 정규화의 train·eval 차이, folding 유도는 새로 배움
> **선행 노트**: A1 (행렬곱·dense layer), A3 (backprop·vanishing gradient), A5 (nn.Module·train/eval 모드)

---

## 0. 큰 그림 — 이게 왜 필요한가

B 모듈에서 CNN(B2), RNN(B3), Transformer(B4)를 배운다. 이름은 다르지만 뜯어 보면 **같은 부품 세 개**가 반복된다.

- **선형 연산**: `y = W·x + b`. dense(fully-connected) layer, conv, attention의 Q·K·V projection 전부 이것이다 (A1 5절: 결국 GEMM).
- **활성화 함수(activation function)**: 선형 연산 결과에 원소별로 적용하는 비선형 함수. ReLU, GELU, SiLU 등.
- **정규화(normalization)**: 중간 값의 크기(scale)를 일정하게 맞춰 주는 층. BatchNorm, LayerNorm, RMSNorm 등.

edge ML 엔지니어 입장에서 이 세 부품이 중요한 이유는 "학습할 때의 그래프"와 "기기에서 도는 그래프"가 **다르기** 때문이다. 학습 그래프에 있던 BatchNorm은 배포 때 앞 층의 weight 안으로 녹아 사라지고(BN folding), ReLU6는 MAC 루프 끝의 clamp 한 줄이 되며, Dropout은 아예 지워진다. 변환 툴(TFLite converter, ONNX Runtime, 벤더 NPU 컴파일러)이 이 일을 자동으로 하는데, **왜 그렇게 되는지 모르면** "PyTorch와 기기 출력이 다르다" 같은 버그를 못 잡는다.

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="b1arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="30" font-size="13">학습 그래프 (PyTorch, model.train())</text>
<rect x="20" y="45" width="60" height="40" rx="6" fill="none" stroke="currentColor"/><text x="50" y="70" font-size="13" text-anchor="middle">x</text>
<rect x="110" y="45" width="90" height="40" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="155" y="70" font-size="13" text-anchor="middle">Linear</text>
<rect x="230" y="45" width="100" height="40" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="280" y="70" font-size="13" text-anchor="middle">BatchNorm</text>
<rect x="360" y="45" width="80" height="40" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="400" y="70" font-size="13" text-anchor="middle">ReLU6</text>
<rect x="470" y="45" width="90" height="40" rx="6" fill="none" stroke="#888" stroke-width="2"/><text x="515" y="70" font-size="13" text-anchor="middle">Dropout</text>
<rect x="590" y="45" width="70" height="40" rx="6" fill="none" stroke="currentColor"/><text x="625" y="70" font-size="13" text-anchor="middle">다음 층</text>
<line x1="80" y1="65" x2="108" y2="65" stroke="currentColor" marker-end="url(#b1arr)"/><line x1="200" y1="65" x2="228" y2="65" stroke="currentColor" marker-end="url(#b1arr)"/><line x1="330" y1="65" x2="358" y2="65" stroke="currentColor" marker-end="url(#b1arr)"/><line x1="440" y1="65" x2="468" y2="65" stroke="currentColor" marker-end="url(#b1arr)"/><line x1="560" y1="65" x2="588" y2="65" stroke="currentColor" marker-end="url(#b1arr)"/>
<line x1="280" y1="87" x2="280" y2="183" stroke="#e08a3c" stroke-dasharray="4 3" marker-end="url(#b1arr)"/><text x="288" y="125" font-size="12">BN folding</text><text x="288" y="141" font-size="12">(오프라인: W′, b′)</text>
<line x1="400" y1="87" x2="400" y2="183" stroke="#3f9a6b" stroke-dasharray="4 3" marker-end="url(#b1arr)"/><text x="408" y="125" font-size="12">activation</text><text x="408" y="141" font-size="12">fusion</text>
<line x1="515" y1="87" x2="515" y2="150" stroke="#888" stroke-dasharray="4 3"/><text x="523" y="125" font-size="12">eval = 항등</text><text x="523" y="141" font-size="12">→ 삭제</text>
<text x="20" y="178" font-size="13">배포 그래프 (TFLite / NPU 컴파일 후)</text>
<rect x="20" y="190" width="60" height="40" rx="6" fill="none" stroke="currentColor"/><text x="50" y="215" font-size="13" text-anchor="middle">x</text>
<rect x="110" y="190" width="450" height="40" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="335" y="215" font-size="13" text-anchor="middle">FullyConnected(W′, b′) + ReLU6 — 커널 1개, MAC 루프 1번</text>
<rect x="590" y="190" width="70" height="40" rx="6" fill="none" stroke="currentColor"/><text x="625" y="215" font-size="13" text-anchor="middle">다음 층</text>
<line x1="80" y1="210" x2="108" y2="210" stroke="currentColor" marker-end="url(#b1arr)"/><line x1="560" y1="210" x2="588" y2="210" stroke="currentColor" marker-end="url(#b1arr)"/>
<text x="20" y="262" font-size="12">op 4개 → 1개: 중간 텐서 3개가 사라지고(메모리 왕복 감소), BN 파라미터는 W′, b′ 안으로 들어간다.</text>
</svg>
```

그림 1 — 학습 그래프의 Linear → BatchNorm → ReLU6 → Dropout 네 개의 op가 배포 그래프에서는 커널 하나로 합쳐진다. BN은 오프라인에서 W′, b′로 접히고(7절), ReLU6는 출력 clamp로 흡수되며(4절), Dropout은 추론 때 아무것도 안 하므로 삭제된다(8절).

펌웨어 비유로 말하면: 학습 그래프는 **디버그 빌드**다. 계측 코드(BN 통계 수집), 랜덤 fault injection(Dropout)이 켜져 있다. 배포 그래프는 **릴리스 빌드**다. 상수 전파(constant folding)로 BN이 사라지고, 함수 인라이닝(op fusion)으로 호출 오버헤드와 중간 버퍼가 없어진다. 컴파일러 최적화를 믿으려면 그 최적화가 의미를 보존한다는 걸 알아야 하듯, BN folding도 "수학적으로 같은 결과"임을 직접 확인해 볼 것이다.

이 노트의 순서: dense layer와 MLP(1절) → 비선형이 왜 필요한가·universal approximation(2절) → 활성화 함수 도감(3절) → edge용 활성화: ReLU6·hard-swish·LUT(4절) → BatchNorm(5절) → LayerNorm·RMSNorm·GroupNorm(6절) → BN folding(7절) → Dropout과 residual 예고(8절) → 비용 계산(9절) → 임베디드 관점(10절).

---

## 1. Dense layer와 MLP — `y = W·x + b`를 쌓는다

### 1.1 dense layer 복습

A1 4절에서 본 것처럼 **dense layer**(= fully-connected layer, PyTorch의 `nn.Linear`)는 행렬-벡터 곱 하나에 bias를 더한 것이다.

```
 y = W·x + b          x ∈ ℝ^in,  W ∈ ℝ^(out×in),  b ∈ ℝ^out,  y ∈ ℝ^out

 y[o] = b[o] + ∑_i W[o][i] · x[i]      (출력 하나 = W의 한 행과 x의 dot)
```

말로 하면: 출력 뉴런 o는 입력 전부에 각자의 가중치를 곱해 더하고 bias를 얹는다. 출력마다 입력 개수(in)만큼 MAC를 하므로 층 하나의 MAC 수는 `in × out`이다.

**손계산.** `W = [[1, 2], [0, −1], [3, 1]]`, `b = [0, 1, −1]`, `x = [2, 1]`이면

- `y[0] = 0 + 1·2 + 2·1 = 4`
- `y[1] = 1 + 0·2 + (−1)·1 = 0`
- `y[2] = −1 + 3·2 + 1·1 = 6`

그래서 `y = [4, 0, 6]`. 여기에 ReLU(음수를 0으로)를 적용하면 그대로 `[4, 0, 6]`이다.

### 1.2 MLP = dense + 활성화를 쌓은 것

**MLP(multi-layer perceptron)** 는 "dense → 활성화 → dense → 활성화 → … → dense"의 사슬이다. 가운데 층의 출력을 **hidden** 이라고 부르고, 그 개수를 hidden 크기(width)라고 한다. 층을 몇 개 쌓았는지가 depth다.

```
 x[300] ─► Linear(300→64) ─► ReLU ─► Linear(64→32) ─► ReLU ─► Linear(32→5) ─► logits[5]
           └──── hidden 1 ────┘      └──── hidden 2 ────┘      └─ 출력층(활성화 없음) ─┘
```

마지막 층 뒤에는 보통 활성화를 두지 않는다. 분류라면 logits를 softmax/cross-entropy로 넘기고(A2 7절), 회귀라면 실수 그대로 쓴다.

MLP는 오늘날 단독 모델로는 작은 문제(IMU 특징 → 제스처 분류, B7)에 주로 쓰이지만, **부품으로는 어디에나 있다**: CNN 끝의 classifier head, Transformer 블록의 FFN(B4: Linear → GELU/SiLU → Linear), MobileNetV3의 squeeze-excite 블록.

### 1.3 활성화가 없으면 층을 쌓아도 한 층이다

활성화 함수를 빼면 무슨 일이 생길까? 1차원으로 손계산해 보자. 층 1: `h = 3x + 1`, 층 2: `y = 2h − 1`.

```
 y = 2(3x + 1) − 1 = 6x + 1        ← 그냥 기울기 6, 절편 1인 층 하나
```

행렬로 일반화해도 같다.

```
 y = W2·(W1·x + b1) + b2
   = (W2·W1)·x + (W2·b1 + b2)
   =    W   ·x +      b             ← W = W2·W1,  b = W2·b1 + b2
```

말로 하면: 선형 함수를 선형 함수에 넣으면 또 선형 함수다. 100층을 쌓아도 한 층과 **표현력이 똑같다** — 파라미터와 MAC만 낭비한다. 사이에 ReLU를 넣으면 `x = −1`에서 층 1 출력 `−2`가 `0`으로 잘리고 `y = 2·0 − 1 = −1`이 되어, 선형 버전의 `6·(−1) + 1 = −5`와 달라진다. 이 "잘림"이 곡선을 만드는 재료다.

아래 코드는 3→4→2 선형 2층이 3→2 한 층으로 정확히 합쳐지는지, ReLU를 넣으면 중첩 원리(superposition)가 깨지는지 확인한다.

```python
import numpy as np
np.set_printoptions(precision=4, suppress=True)
rng = np.random.default_rng(0)

# 2층짜리 '선형만' 네트워크: 3 -> 4 -> 2
W1, b1 = rng.normal(size=(4, 3)), rng.normal(size=4)
W2, b2 = rng.normal(size=(2, 4)), rng.normal(size=2)
x = np.array([1.0, -2.0, 0.5])

y_two = W2 @ (W1 @ x + b1) + b2          # 층 두 개
W, b = W2 @ W1, W2 @ b1 + b2             # 하나로 합친 층
y_one = W @ x + b
print("two linear layers :", y_two)
print("collapsed (W,b)   :", y_one, " W shape", W.shape)
print("max diff          :", np.abs(y_two - y_one).max())

# 사이에 ReLU를 넣으면 더 이상 하나로 합쳐지지 않는다
relu = lambda z: np.maximum(z, 0)
y_relu = W2 @ relu(W1 @ x + b1) + b2
print("with ReLU         :", y_relu)
# 선형성 검사: f(x1+x2) == f(x1)+f(x2)-f(0) ?
f_lin  = lambda v: W2 @ (W1 @ v + b1) + b2
f_relu = lambda v: W2 @ relu(W1 @ v + b1) + b2
x2 = np.array([-1.0, 0.5, 2.0])
print("linear  superpos err:", np.abs(f_lin(x+x2)  - (f_lin(x)+f_lin(x2)-f_lin(0*x))).max())
print("ReLU    superpos err:", np.abs(f_relu(x+x2) - (f_relu(x)+f_relu(x2)-f_relu(0*x))).max())
```

```text
two linear layers : [-0.2398  3.0556]
collapsed (W,b)   : [-0.2398  3.0556]  W shape (2, 3)
max diff          : 1.1102230246251565e-16
with ReLU         : [0.5434 1.6494]
linear  superpos err: 8.881784197001252e-16
ReLU    superpos err: 0.2996001754158908
```

출력에서 볼 것: 두 층과 합친 한 층의 차이는 1e-16(float64 반올림 수준)이다. 선형 네트워크는 `f(x1+x2) = f(x1) + f(x2) − f(0)`(affine 함수의 중첩)이 오차 1e-15로 성립하지만, ReLU를 넣으면 0.3이나 어긋난다. 즉 더 이상 선형이 아니다.

펌웨어 연결: FIR 필터 두 개를 직렬로 연결하면 계수를 convolution한 FIR 하나와 같다. 필터를 아무리 직렬로 쌓아도 LTI 시스템이라는 한계를 못 벗어나는 것과 똑같다. 비선형(클리핑, 정류)이 들어가야 새로운 주파수 성분이 생기듯, 활성화가 들어가야 새로운 모양의 함수가 생긴다.

> 참고 — 반대로 이 성질은 **배포 최적화에 쓰인다**. 사이에 비선형이 없는 연속된 선형 op(예: Linear 다음 BatchNorm, 7절)는 오프라인에서 하나로 합칠 수 있다. BN folding이 바로 이것이다.

---

## 2. 왜 비선형이 필요한가 — universal approximation 직관

### 2.1 ReLU 하나 = 꺾임 하나

`ReLU(x − k)`는 `x = k`에서 꺾이는 "하키 스틱"이다. 이런 스틱 여러 개를 서로 다른 위치 k에 두고, 각각에 기울기 가중치를 곱해 더하면 **꺾은선(piecewise-linear) 함수**가 된다. 꺾는 점을 충분히 촘촘히 두면 어떤 연속 곡선이든 원하는 만큼 가깝게 따라갈 수 있다. 이것이 **universal approximation theorem**(범용 근사 정리)의 직관이다: 은닉층 하나짜리 MLP도 hidden unit이 충분히 많으면 (유계 구간에서) 임의의 연속 함수를 원하는 정확도로 근사할 수 있다.

```
 f(x) ≈ b2 + ∑_{i=1..H} w2[i] · ReLU(x − k[i])

 hidden unit i  ↔  위치 k[i]에서 기울기를 w2[i]만큼 바꾸는 꺾임
```

말로 하면: 은닉 뉴런 하나가 꺾임 하나를 담당한다. 출력층은 그 꺾임들의 기울기 변화량을 정한다. 펌웨어로 치면 **구간별 선형 보간 테이블**(breakpoint + slope) — 센서 비선형 보정에 흔히 쓰는 그것 — 과 같은 구조다.

### 2.2 hidden을 늘리면 오차가 줄어든다 — 코드로

아래 코드는 꺾임 위치 k를 구간에 고르게 고정하고, 출력층 가중치만 최소제곱(`torch.linalg.lstsq`)으로 최적값을 구해 hidden 크기 H별 최대 오차를 잰다. 그다음 같은 1→32→1 MLP를 Adam으로 "진짜 학습"시켰을 때와 비교한다.

```python
import torch
torch.manual_seed(0)
x = torch.linspace(-3, 3, 400, dtype=torch.float64).unsqueeze(1)   # [400,1]
y = torch.sin(2 * x) + 0.3 * x                                      # 맞출 곡선

# hidden unit i = ReLU(x - k_i): 꺾이는 점(kink) k_i를 구간에 고르게 배치
# 출력층 w2, b2는 최소제곱으로 '최적값'을 바로 구한다 (학습 대신 lstsq)
for H in [1, 2, 4, 8, 16, 32]:
    k = torch.linspace(-3, 3, H + 2, dtype=torch.float64)[:-2]      # 왼쪽부터 H개
    h = torch.relu(x - k)                                           # [400, H] hidden
    A = torch.cat([h, torch.ones_like(x)], dim=1)                   # bias 열 추가
    w = torch.linalg.lstsq(A, y).solution                           # [H+1, 1]
    err = (A @ w - y).abs().max().item()
    print(f"H={H:2d}  params={3*H+1:3d}  max|err|={err:.4f}")

# 참고: 같은 모양(1→32→1)을 Adam으로 '학습'하면? seed(초기값)마다 결과가 다르다
xf, yf = (x / 3).float(), y.float()                                 # 입력을 [-1,1]로
for seed in range(4):
    torch.manual_seed(seed)
    net = torch.nn.Sequential(torch.nn.Linear(1, 32), torch.nn.ReLU(), torch.nn.Linear(32, 1))
    opt = torch.optim.Adam(net.parameters(), lr=0.01)
    for step in range(4000):
        loss = ((net(xf) - yf) ** 2).mean()
        opt.zero_grad(); loss.backward(); opt.step()
    print(f"trained H=32 seed={seed}: max|err|={(net(xf)-yf).abs().max().item():.4f}")
```

```text
H= 1  params=  4  max|err|=1.1347
H= 2  params=  7  max|err|=1.2091
H= 4  params= 13  max|err|=0.9991
H= 8  params= 25  max|err|=0.5813
H=16  params= 49  max|err|=0.1333
H=32  params= 97  max|err|=0.0252
trained H=32 seed=0: max|err|=1.2171
trained H=32 seed=1: max|err|=0.9727
trained H=32 seed=2: max|err|=0.0951
trained H=32 seed=3: max|err|=0.0705
```

출력에서 볼 것: 꺾임을 고르게 두면 H=8에서 0.58, H=16에서 0.13, H=32에서 0.025로 최대 오차가 빠르게 준다(H=2가 H=1보다 최대 오차가 조금 큰 건 lstsq가 최대 오차가 아니라 제곱 오차 평균을 줄이기 때문이다). 반면 같은 크기를 **학습**시키면 seed에 따라 1.2(실패)부터 0.07(성공)까지 갈린다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="135.0" x2="620" y2="135.0" stroke="currentColor" stroke-opacity="0.5"/><line x1="340.0" y1="20" x2="340.0" y2="250" stroke="currentColor" stroke-opacity="0.5"/><text x="335.0" y="254.0" font-size="12" text-anchor="end">-2</text><text x="335.0" y="196.5" font-size="12" text-anchor="end">-1</text><text x="335.0" y="81.5" font-size="12" text-anchor="end">1</text><text x="335.0" y="24.0" font-size="12" text-anchor="end">2</text><text x="60.0" y="150.0" font-size="12" text-anchor="middle">-3</text><text x="153.3" y="150.0" font-size="12" text-anchor="middle">-2</text><text x="246.7" y="150.0" font-size="12" text-anchor="middle">-1</text><text x="433.3" y="150.0" font-size="12" text-anchor="middle">1</text><text x="526.7" y="150.0" font-size="12" text-anchor="middle">2</text><text x="620.0" y="150.0" font-size="12" text-anchor="middle">3</text>
<polyline points="60.0,170.7 68.4,159.5 78.2,147.5 88.1,137.1 97.9,128.6 106.3,123.2 116.1,119.1 126.0,117.5 135.8,118.5 144.2,121.2 154.0,126.4 163.9,133.5 173.7,142.2 182.1,150.4 191.9,160.6 201.8,170.8 211.6,180.6 220.0,188.2 229.8,195.8 239.6,201.7 249.5,205.4 259.3,206.7 267.7,205.8 277.5,202.5 287.4,196.7 297.2,188.6 305.6,180.0 315.4,168.4 325.3,155.6 335.1,141.9 343.5,130.0 353.3,116.3 363.2,103.3 373.0,91.6 381.4,82.8 391.2,74.3 401.1,68.2 410.9,64.5 419.3,63.3 429.1,64.3 438.9,67.7 448.8,73.2 458.6,80.6 467.0,88.1 476.8,97.8 486.7,108.0 496.5,118.2 504.9,126.5 514.7,135.3 524.6,142.7 534.4,148.2 542.8,151.2 552.6,152.5 562.5,151.3 572.3,147.6 580.7,142.4 590.5,134.3 600.4,124.1 610.2,112.3 620.0,99.3" fill="none" stroke="#888" stroke-width="4" stroke-opacity="0.6"/>
<polyline points="60.0,137.5 69.3,137.5 78.7,137.5 88.0,137.5 97.3,137.5 106.7,137.5 116.0,137.5 125.3,137.6 134.7,137.6 144.0,137.6 153.3,137.6 162.7,137.6 172.0,137.6 172.0,137.6 181.3,144.2 190.7,150.7 200.0,157.3 209.3,163.8 218.7,170.4 228.0,176.9 237.3,183.5 246.7,190.0 256.0,196.6 265.3,203.1 274.7,209.6 284.0,216.2 284.0,216.2 293.3,203.5 302.7,190.8 312.0,178.1 321.3,165.4 330.7,152.6 340.0,139.9 349.3,127.2 358.7,114.5 368.0,101.8 377.3,89.1 386.7,76.4 396.0,63.7 396.0,63.7 405.3,67.6 414.7,71.4 424.0,75.3 433.3,79.2 442.7,83.1 452.0,87.0 461.3,90.8 470.7,94.7 480.0,98.6 489.3,102.5 498.7,106.3 508.0,110.2 517.3,114.1 526.7,118.0 536.0,121.9 545.3,125.7 554.7,129.6 564.0,133.5 573.3,137.4 582.7,141.3 592.0,145.1 601.3,149.0 610.7,152.9 620.0,156.8" fill="none" stroke="#e08a3c" stroke-width="2"/>
<polyline points="60.0,165.3 69.3,157.0 78.7,148.6 88.0,140.3 97.3,131.9 106.7,123.6 116.0,115.3 122.2,109.7 125.3,111.8 134.7,117.9 144.0,124.1 153.3,130.3 162.7,136.5 172.0,142.6 181.3,148.8 184.4,150.9 190.7,157.1 200.0,166.3 209.3,175.6 218.7,184.9 228.0,194.2 237.3,203.5 246.7,212.8 256.0,208.1 265.3,203.4 274.7,198.7 284.0,194.0 293.3,189.3 302.7,184.6 308.9,181.5 312.0,176.9 321.3,163.1 330.7,149.2 340.0,135.4 349.3,121.6 358.7,107.8 368.0,93.9 371.1,89.3 377.3,85.7 386.7,80.3 396.0,74.9 405.3,69.4 414.7,64.0 424.0,58.6 433.3,53.2 442.7,65.4 452.0,77.6 461.3,89.8 470.7,102.1 480.0,114.3 489.3,126.5 495.6,134.7 498.7,134.6 508.0,134.5 517.3,134.3 526.7,134.2 536.0,134.0 545.3,133.9 554.7,133.7 564.0,133.6 573.3,133.5 582.7,133.3 592.0,133.2 601.3,133.0 610.7,132.9 620.0,132.7" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<polyline points="60.0,170.5 69.3,158.4 77.0,148.6 78.7,146.8 88.0,137.3 93.9,131.2 97.3,129.0 106.7,123.1 110.9,120.3 116.0,119.3 125.3,117.4 127.9,116.9 134.7,118.5 144.0,120.7 144.8,120.9 153.3,126.2 161.8,131.5 162.7,132.3 172.0,140.7 178.8,146.9 181.3,149.6 190.7,159.3 195.8,164.6 200.0,168.9 209.3,178.5 212.7,181.9 218.7,186.9 228.0,194.8 229.7,196.2 237.3,200.2 246.7,205.1 256.0,206.2 263.6,207.1 265.3,206.6 274.7,203.5 280.6,201.5 284.0,199.0 293.3,191.9 297.6,188.7 302.7,183.1 312.0,172.7 314.5,169.9 321.3,160.8 330.7,148.2 331.5,147.1 340.0,135.0 348.5,122.9 349.3,121.8 358.7,109.2 365.5,100.1 368.0,97.3 377.3,86.9 382.4,81.3 386.7,78.1 396.0,71.0 399.4,68.5 405.3,66.5 414.7,63.4 416.4,62.9 424.0,63.8 433.3,64.9 442.7,69.8 450.3,73.8 452.0,75.2 461.3,83.1 467.3,88.1 470.7,91.5 480.0,101.1 484.2,105.4 489.3,110.7 498.7,120.4 501.2,123.1 508.0,129.3 517.3,137.7 518.2,138.5 526.7,143.8 535.2,149.1 536.0,149.3 545.3,151.5 552.1,153.2 554.7,152.6 564.0,150.6 569.1,149.5 573.3,147.0 582.7,141.5 586.1,139.5 592.0,132.7 601.3,122.1 610.7,111.4 620.0,100.8" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<line x1="60" y1="280" x2="80" y2="280" stroke="#888" stroke-width="3"/><text x="86" y="284" font-size="12">목표 sin(2x)+0.3x</text><line x1="200" y1="280" x2="220" y2="280" stroke="#e08a3c" stroke-width="3"/><text x="226" y="284" font-size="12">H=4</text><line x1="340" y1="280" x2="360" y2="280" stroke="#3f9a6b" stroke-width="3"/><text x="366" y="284" font-size="12">H=8</text><line x1="480" y1="280" x2="500" y2="280" stroke="#4a7bd0" stroke-width="3"/><text x="506" y="284" font-size="12">H=32</text><text x="60" y="310" font-size="12">ReLU hidden unit 하나 = 꺾임 하나. 꺾임이 많을수록 곡선을 더 잘 따라간다.</text>
</svg>
```

그림 2 — 실제 계산한 lstsq 근사. 회색 두꺼운 선이 목표 곡선, H=4(주황)는 꺾인 막대기 몇 개, H=8(초록)은 모양을 대강 따라가고, H=32(파랑)는 목표와 거의 겹친다.

### 2.3 이 정리가 말해 주지 않는 것

- **"표현할 수 있다" ≠ "학습으로 찾을 수 있다"**: 위 출력의 seed 0, 1이 그 예다. 초기 꺾임 위치가 한쪽에 몰리거나, 음수 영역에 갇혀 gradient가 0이 된 뉴런(dead ReLU, 3절)이 생기면 경사하강법이 좋은 해에 못 간다. 그래서 실무에선 초기화, 정규화(5~6절), 입력 스케일링이 중요하다.
- **얼마나 많이 필요한지**: 은닉층 하나로 가능하긴 하지만 필요한 H가 폭발적으로 클 수 있다. 층을 깊게 쌓으면 꺾임이 **곱으로** 늘어나서 같은 파라미터로 훨씬 복잡한 함수를 만든다. 그래서 "넓고 얕은" 대신 "좁고 깊은" 모델을 쓴다.
- **구간 밖**: 꺾은선은 학습 구간 밖에선 직선으로 뻗는다. 학습 때 본 적 없는 범위의 센서 값(예: 다른 IMU full-scale 설정)이 들어오면 예측이 엉뚱해진다(A4의 distribution shift).

---

## 3. 활성화 함수 도감

### 3.1 한눈에 보기

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
<text x="125" y="24" font-size="13" text-anchor="middle">포화형 (saturating)</text><line x1="30" y1="135.0" x2="220" y2="135.0" stroke="currentColor" stroke-opacity="0.5"/><line x1="125.0" y1="40" x2="125.0" y2="230" stroke="currentColor" stroke-opacity="0.5"/><text x="121.0" y="218.2" font-size="12" text-anchor="end">-1</text><text x="121.0" y="59.8" font-size="12" text-anchor="end">1</text><text x="30" y="246" font-size="12">-6</text><text x="220" y="246" font-size="12" text-anchor="end">6</text>
<polyline points="30.0,134.8 34.0,134.7 37.9,134.7 41.9,134.6 45.8,134.5 49.8,134.3 53.8,134.1 57.7,133.9 61.7,133.6 65.6,133.2 69.6,132.7 73.5,132.0 77.5,131.2 81.5,130.2 85.4,129.0 89.4,127.5 93.3,125.6 97.3,123.3 101.2,120.6 105.2,117.4 109.2,113.7 113.1,109.6 117.1,105.1 121.0,100.3 125.0,95.4 129.0,90.5 132.9,85.7 136.9,81.2 140.8,77.1 144.8,73.5 148.8,70.3 152.7,67.6 156.7,65.3 160.6,63.4 164.6,61.8 168.5,60.6 172.5,59.6 176.5,58.8 180.4,58.2 184.4,57.7 188.3,57.3 192.3,56.9 196.2,56.7 200.2,56.5 204.2,56.4 208.1,56.2 212.1,56.2 216.0,56.1 220.0,56.0" fill="none" stroke="#4a7bd0" stroke-width="2"/><line x1="70" y1="260" x2="90" y2="260" stroke="#4a7bd0" stroke-width="3"/><text x="96" y="264" font-size="12">sigmoid</text>
<polyline points="30.0,214.2 34.0,214.2 37.9,214.2 41.9,214.2 45.8,214.2 49.8,214.2 53.8,214.1 57.7,214.1 61.7,214.1 65.6,214.1 69.6,214.0 73.5,213.9 77.5,213.8 81.5,213.5 85.4,213.1 89.4,212.4 93.3,211.3 97.3,209.5 101.2,206.7 105.2,202.2 109.2,195.3 113.1,185.3 117.1,171.6 121.0,154.4 125.0,135.0 129.0,115.6 132.9,98.4 136.9,84.7 140.8,74.7 144.8,67.8 148.8,63.3 152.7,60.5 156.7,58.7 160.6,57.6 164.6,56.9 168.5,56.5 172.5,56.2 176.5,56.1 180.4,56.0 184.4,55.9 188.3,55.9 192.3,55.9 196.2,55.9 200.2,55.8 204.2,55.8 208.1,55.8 212.1,55.8 216.0,55.8 220.0,55.8" fill="none" stroke="#e08a3c" stroke-width="2"/><line x1="70" y1="276" x2="90" y2="276" stroke="#e08a3c" stroke-width="3"/><text x="96" y="280" font-size="12">tanh</text>
<polyline points="30.0,135.0 34.0,135.0 37.9,135.0 41.9,135.0 45.8,135.0 49.8,135.0 53.8,135.0 57.7,135.0 61.7,135.0 65.6,135.0 69.6,135.0 73.5,135.0 77.5,135.0 81.5,131.7 85.4,128.4 89.4,125.1 93.3,121.8 97.3,118.5 101.2,115.2 105.2,111.9 109.2,108.6 113.1,105.3 117.1,102.0 121.0,98.7 125.0,95.4 129.0,92.1 132.9,88.8 136.9,85.5 140.8,82.2 144.8,78.9 148.8,75.6 152.7,72.3 156.7,69.0 160.6,65.7 164.6,62.4 168.5,59.1 172.5,55.8 176.5,55.8 180.4,55.8 184.4,55.8 188.3,55.8 192.3,55.8 196.2,55.8 200.2,55.8 204.2,55.8 208.1,55.8 212.1,55.8 216.0,55.8 220.0,55.8" fill="none" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="5 3"/><line x1="70" y1="292" x2="90" y2="292" stroke="#3f9a6b" stroke-width="3" stroke-dasharray="5 3"/><text x="96" y="296" font-size="12">hard-sigmoid</text><text x="350" y="24" font-size="13" text-anchor="middle">ReLU 계열</text>
<line x1="255" y1="208.9" x2="445" y2="208.9" stroke="currentColor" stroke-opacity="0.5"/><line x1="318.3" y1="40" x2="318.3" y2="230" stroke="currentColor" stroke-opacity="0.5"/><text x="314.3" y="170.7" font-size="12" text-anchor="end">2</text><text x="314.3" y="128.4" font-size="12" text-anchor="end">4</text><text x="314.3" y="86.2" font-size="12" text-anchor="end">6</text><text x="314.3" y="44.0" font-size="12" text-anchor="end">8</text><text x="255" y="246" font-size="12">-4</text><text x="445" y="246" font-size="12" text-anchor="end">8</text>
<polyline points="255.0,208.9 259.0,208.9 262.9,208.9 266.9,208.9 270.8,208.9 274.8,208.9 278.8,208.9 282.7,208.9 286.7,208.9 290.6,208.9 294.6,208.9 298.5,208.9 302.5,208.9 306.5,208.9 310.4,208.9 314.4,208.9 318.3,208.9 322.3,203.6 326.2,198.3 330.2,193.1 334.2,187.8 338.1,182.5 342.1,177.2 346.0,171.9 350.0,166.7 354.0,161.4 357.9,156.1 361.9,150.8 365.8,145.6 369.8,140.3 373.8,135.0 377.7,129.7 381.7,124.4 385.6,119.2 389.6,113.9 393.5,108.6 397.5,103.3 401.5,98.1 405.4,92.8 409.4,87.5 413.3,82.2 417.3,76.9 421.2,71.7 425.2,66.4 429.2,61.1 433.1,55.8 437.1,50.6 441.0,45.3 445.0,40.0" fill="none" stroke="#4a7bd0" stroke-width="2"/><line x1="295" y1="260" x2="315" y2="260" stroke="#4a7bd0" stroke-width="3"/><text x="321" y="264" font-size="12">ReLU</text>
<polyline points="255.0,217.3 259.0,216.8 262.9,216.3 266.9,215.8 270.8,215.2 274.8,214.7 278.8,214.2 282.7,213.6 286.7,213.1 290.6,212.6 294.6,212.1 298.5,211.5 302.5,211.0 306.5,210.5 310.4,209.9 314.4,209.4 318.3,208.9 322.3,203.6 326.2,198.3 330.2,193.1 334.2,187.8 338.1,182.5 342.1,177.2 346.0,171.9 350.0,166.7 354.0,161.4 357.9,156.1 361.9,150.8 365.8,145.6 369.8,140.3 373.8,135.0 377.7,129.7 381.7,124.4 385.6,119.2 389.6,113.9 393.5,108.6 397.5,103.3 401.5,98.1 405.4,92.8 409.4,87.5 413.3,82.2 417.3,76.9 421.2,71.7 425.2,66.4 429.2,61.1 433.1,55.8 437.1,50.6 441.0,45.3 445.0,40.0" fill="none" stroke="#e08a3c" stroke-width="2"/><line x1="295" y1="276" x2="315" y2="276" stroke="#e08a3c" stroke-width="3"/><text x="321" y="280" font-size="12">Leaky(0.1)</text>
<polyline points="255.0,208.9 259.0,208.9 262.9,208.9 266.9,208.9 270.8,208.9 274.8,208.9 278.8,208.9 282.7,208.9 286.7,208.9 290.6,208.9 294.6,208.9 298.5,208.9 302.5,208.9 306.5,208.9 310.4,208.9 314.4,208.9 318.3,208.9 322.3,203.6 326.2,198.3 330.2,193.1 334.2,187.8 338.1,182.5 342.1,177.2 346.0,171.9 350.0,166.7 354.0,161.4 357.9,156.1 361.9,150.8 365.8,145.6 369.8,140.3 373.8,135.0 377.7,129.7 381.7,124.4 385.6,119.2 389.6,113.9 393.5,108.6 397.5,103.3 401.5,98.1 405.4,92.8 409.4,87.5 413.3,82.2 417.3,82.2 421.2,82.2 425.2,82.2 429.2,82.2 433.1,82.2 437.1,82.2 441.0,82.2 445.0,82.2" fill="none" stroke="#3f9a6b" stroke-width="2"/><line x1="295" y1="292" x2="315" y2="292" stroke="#3f9a6b" stroke-width="3"/><text x="321" y="296" font-size="12">ReLU6</text><text x="575" y="24" font-size="13" text-anchor="middle">매끄러운 ReLU (smooth)</text>
<line x1="480" y1="198.3" x2="670" y2="198.3" stroke="currentColor" stroke-opacity="0.5"/><line x1="588.6" y1="40" x2="588.6" y2="230" stroke="currentColor" stroke-opacity="0.5"/><text x="584.6" y="149.6" font-size="12" text-anchor="end">1</text><text x="584.6" y="96.8" font-size="12" text-anchor="end">2</text><text x="584.6" y="44.0" font-size="12" text-anchor="end">3</text><text x="480" y="246" font-size="12">-4</text><text x="670" y="246" font-size="12" text-anchor="end">3</text>
<polyline points="480.0,198.3 484.0,198.3 487.9,198.4 491.9,198.4 495.8,198.4 499.8,198.4 503.8,198.5 507.7,198.6 511.7,198.7 515.6,198.8 519.6,199.1 523.5,199.4 527.5,199.8 531.5,200.3 535.4,200.9 539.4,201.7 543.3,202.5 547.3,203.5 551.2,204.5 555.2,205.4 559.2,206.3 563.1,207.0 567.1,207.3 571.0,207.2 575.0,206.5 579.0,205.1 582.9,202.9 586.9,199.9 590.8,196.0 594.8,191.2 598.8,185.5 602.7,179.1 606.7,172.0 610.6,164.4 614.6,156.3 618.5,147.9 622.5,139.3 626.5,130.7 630.4,122.0 634.4,113.3 638.3,104.8 642.3,96.4 646.2,88.1 650.2,79.9 654.2,71.8 658.1,63.8 662.1,55.9 666.0,48.0 670.0,40.2" fill="none" stroke="#4a7bd0" stroke-width="2"/><line x1="520" y1="260" x2="540" y2="260" stroke="#4a7bd0" stroke-width="3"/><text x="546" y="264" font-size="12">GELU</text>
<polyline points="480.0,202.1 484.0,202.6 487.9,203.0 491.9,203.5 495.8,204.1 499.8,204.6 503.8,205.3 507.7,205.9 511.7,206.6 515.6,207.4 519.6,208.1 523.5,208.9 527.5,209.7 531.5,210.4 535.4,211.1 539.4,211.8 543.3,212.3 547.3,212.7 551.2,213.0 555.2,213.0 559.2,212.8 563.1,212.3 567.1,211.4 571.0,210.1 575.0,208.3 579.0,206.0 582.9,203.3 586.9,199.9 590.8,196.0 594.8,191.6 598.8,186.6 602.7,181.1 606.7,175.1 610.6,168.6 614.6,161.8 618.5,154.6 622.5,147.1 626.5,139.3 630.4,131.3 634.4,123.2 638.3,114.9 642.3,106.6 646.2,98.1 650.2,89.7 654.2,81.2 658.1,72.8 662.1,64.3 666.0,55.9 670.0,47.5" fill="none" stroke="#e08a3c" stroke-width="2"/><line x1="520" y1="276" x2="540" y2="276" stroke="#e08a3c" stroke-width="3"/><text x="546" y="280" font-size="12">SiLU</text>
<polyline points="480.0,198.3 484.0,198.3 487.9,198.3 491.9,198.3 495.8,198.3 499.8,198.3 503.8,198.3 507.7,198.9 511.7,202.5 515.6,205.7 519.6,208.6 523.5,211.1 527.5,213.2 531.5,214.9 535.4,216.3 539.4,217.3 543.3,217.9 547.3,218.1 551.2,218.0 555.2,217.5 559.2,216.6 563.1,215.3 567.1,213.7 571.0,211.7 575.0,209.3 579.0,206.6 582.9,203.4 586.9,199.9 590.8,196.1 594.8,191.8 598.8,187.2 602.7,182.2 606.7,176.8 610.6,171.1 614.6,165.0 618.5,158.5 622.5,151.6 626.5,144.4 630.4,136.7 634.4,128.8 638.3,120.4 642.3,111.6 646.2,102.5 650.2,93.0 654.2,83.2 658.1,73.0 662.1,62.3 666.0,51.4 670.0,40.0" fill="none" stroke="#d0564a" stroke-width="2" stroke-dasharray="5 3"/><line x1="520" y1="292" x2="540" y2="292" stroke="#d0564a" stroke-width="3" stroke-dasharray="5 3"/><text x="546" y="296" font-size="12">hard-swish</text>
</svg>
```

그림 3 — 실제로 계산한 9개 활성화 함수. 점선은 "hard" 버전(구간별 선형 근사)이다. 왼쪽: 출력이 유계인 포화형. 가운데: ReLU 계열(ReLU6는 6에서 잘린다). 오른쪽: 0 근처가 매끄러운 ReLU 변형과 그 근사인 hard-swish.

### 3.2 포화형 — sigmoid, tanh

```
 sigmoid(x) = σ(x) = 1 / (1 + e^(−x))         범위 (0, 1)     σ′(x) = σ(x)(1 − σ(x)) ≤ 0.25
 tanh(x)    = (e^x − e^(−x)) / (e^x + e^(−x)) 범위 (−1, 1)    tanh′(x) = 1 − tanh²(x) ≤ 1
            = 2σ(2x) − 1
```

말로 하면: sigmoid는 아무 실수를 0~1 사이 "확률 같은 값"으로 누르고, tanh는 −1~1로 누른다. 둘 다 입력이 크면 출력이 거의 안 변한다(포화). 포화 구간에선 미분이 0에 가까워져서, 깊게 쌓으면 gradient가 층마다 최대 0.25배씩 줄어든다 — A3 8절의 **vanishing gradient**다. 그래서 요즘 hidden layer에는 거의 안 쓴다. 대신 **출력층**(이진 분류 확률, A2 6절)과 **gate**(LSTM/GRU의 열고 닫는 문, B3)에서는 여전히 핵심이다.

### 3.3 ReLU 계열 — ReLU, Leaky ReLU, ReLU6

```
 ReLU(x)      = max(0, x)                     범위 [0, ∞)     미분: x>0이면 1, 아니면 0
 LeakyReLU(x) = x (x>0),  a·x (x≤0)           범위 (−∞, ∞)    미분: 1 또는 a  (보통 a = 0.01~0.2)
 ReLU6(x)     = min(max(0, x), 6)             범위 [0, 6]     미분: 0<x<6이면 1, 아니면 0
```

- **ReLU**: 양수 쪽은 기울기 1이라 gradient가 줄지 않는다. 계산은 비교 한 번. 깊은 네트워크 학습을 가능하게 만든 일등 공신이다. 단점: 어떤 뉴런의 입력이 항상 음수가 되면 gradient가 영원히 0이라 다시 살아나지 못한다 — **dead ReLU**.
- **Leaky ReLU**: 음수 쪽에 작은 기울기 a를 남겨 dead ReLU를 막는다. 비용은 곱셈 하나 추가.
- **ReLU6**: ReLU의 위쪽을 6에서 자른다. MobileNet v1/v2가 채택했고, MobileNetV2 논문은 이유로 "저정밀도 연산에서의 robustness"를 든다. 출력 범위가 [0, 6]으로 **고정**되면 int8 양자화 scale을 6/255로 미리 정할 수 있어서, 드문 큰 값(outlier) 하나 때문에 scale이 커져 해상도가 망가지는 일을 막는다(4.1절에서 숫자로 확인).

### 3.4 매끄러운 ReLU — GELU, SiLU(Swish)

```
 GELU(x) = x · Φ(x)                Φ = 표준정규분포의 CDF = ½(1 + erf(x/√2))
         ≈ ½x(1 + tanh(√(2/π)·(x + 0.044715x³)))       ← "tanh 근사"
 SiLU(x) = x · σ(x)                (Swish with β=1 이라고도 부른다)
```

말로 하면: 둘 다 "입력 x에 0~1 사이의 **부드러운 스위치**를 곱한다". x가 크면 스위치가 1이라 x를 그대로 통과(ReLU와 같음), x가 아주 음수면 0, 그 사이에선 부드럽게 바뀐다. ReLU와 달리 0 근처에서 미분이 연속이고, 음수 쪽에 살짝 음의 값(SiLU 최솟값 약 −0.28)이 있다.

- **GELU**: BERT, GPT-2, ViT, Whisper 같은 Transformer 계열의 FFN에서 표준이다. erf 계산이 비싸서 tanh 근사를 흔히 쓴다(아래 코드에서 차이 5e-4).
- **SiLU**: EfficientNet(Swish), YOLOv5 이후 YOLO 계열, 그리고 Llama 계열 LLM FFN의 **SwiGLU**(B4에서 다룸)에 들어간다.

### 3.5 hard 버전 — hard-sigmoid, hard-swish

```
 hard-sigmoid(x) = ReLU6(x + 3) / 6            (PyTorch 정의)   = 구간 [−3, 3]에서 x/6 + ½, 밖에선 0 또는 1
 hard-swish(x)   = x · ReLU6(x + 3) / 6        = x · hard-sigmoid(x)
```

말로 하면: sigmoid를 **직선 세 토막**으로 근사한 게 hard-sigmoid이고, SiLU의 sigmoid 자리에 그걸 끼운 게 hard-swish다. exp가 전혀 없고 덧셈·clamp·곱셈·상수 나눗셈만 쓴다. MobileNetV3가 도입했는데, 논문은 (1) 모바일에서 sigmoid 계산이 비싸고 (2) 양자화 모드에서 sigmoid 근사 구현마다 정밀도 차이가 생긴다는 점을 이유로 든다. 정확도는 swish와 거의 같고 비용은 ReLU에 가깝다.

### 3.6 값으로 비교하기

아래 코드는 같은 입력에 9개 함수를 적용해 표로 찍고, hard 근사들이 원래 함수와 최대 얼마나 다른지 잰다.

```python
import torch, torch.nn.functional as F
x = torch.tensor([-4.0, -2.0, -1.0, 0.0, 1.0, 2.0, 4.0, 8.0])
acts = {
    "sigmoid":   torch.sigmoid,
    "tanh":      torch.tanh,
    "relu":      F.relu,
    "leaky0.1":  lambda t: F.leaky_relu(t, 0.1),
    "relu6":     F.relu6,
    "gelu":      F.gelu,
    "silu":      F.silu,
    "hsigmoid":  F.hardsigmoid,
    "hswish":    F.hardswish,
}
print("x        " + " ".join(f"{v:7.2f}" for v in x.tolist()))
for name, f in acts.items():
    print(f"{name:9s}" + " ".join(f"{v:7.3f}" for v in f(x).tolist()))

# 근사 품질: 촘촘한 격자에서 최대 오차
g = torch.linspace(-8, 8, 16001)
print("max|hswish - silu|     =", round((F.hardswish(g) - F.silu(g)).abs().max().item(), 4))
print("max|hsigmoid - sigmoid|=", round((F.hardsigmoid(g) - torch.sigmoid(g)).abs().max().item(), 4))
print("max|gelu_tanh - gelu|  =", f"{(F.gelu(g, approximate='tanh') - F.gelu(g)).abs().max().item():.2e}")
print("silu == x*sigmoid(x)?  :", torch.allclose(F.silu(g), g * torch.sigmoid(g)))
```

```text
x          -4.00   -2.00   -1.00    0.00    1.00    2.00    4.00    8.00
sigmoid    0.018   0.119   0.269   0.500   0.731   0.881   0.982   1.000
tanh      -0.999  -0.964  -0.762   0.000   0.762   0.964   0.999   1.000
relu       0.000   0.000   0.000   0.000   1.000   2.000   4.000   8.000
leaky0.1  -0.400  -0.200  -0.100   0.000   1.000   2.000   4.000   8.000
relu6      0.000   0.000   0.000   0.000   1.000   2.000   4.000   6.000
gelu      -0.000  -0.046  -0.159   0.000   0.841   1.954   4.000   8.000
silu      -0.072  -0.238  -0.269   0.000   0.731   1.762   3.928   7.997
hsigmoid   0.000   0.167   0.333   0.500   0.667   0.833   1.000   1.000
hswish    -0.000  -0.333  -0.333   0.000   0.667   1.667   4.000   8.000
max|hswish - silu|     = 0.1423
max|hsigmoid - sigmoid|= 0.0692
max|gelu_tanh - gelu|  = 4.73e-04
silu == x*sigmoid(x)?  : True
```

출력에서 볼 것: x=8에서 ReLU는 8이지만 ReLU6는 6에서 멈춘다. x=−1에서 Leaky는 −0.1, SiLU는 −0.269, hard-swish는 −0.333 — 음수 쪽에 값이 조금 남는다. hard-swish와 SiLU의 최대 차이 0.14, hard-sigmoid와 sigmoid는 0.07 정도로 작고, 학습을 이 함수로 하면(근사가 아니라 그 함수 자체가 모델의 일부) 정확도 손실은 거의 없다. GELU의 tanh 근사는 5e-4로 더 정확하다.

### 3.7 비교표

| 함수 | 식 | 출력 범위 | 미분 | MCU 비용 (float / int8) | 양자화 친화도 |
|---|---|---|---|---|---|
| sigmoid | 1/(1+e^(−x)) | (0, 1) | σ(1−σ), 최대 0.25 | exp 필요 / 256B LUT | 출력 유계라 좋음. 입력 범위 넓으면 LUT 해상도 문제 |
| tanh | 2σ(2x)−1 | (−1, 1) | 1−tanh², 최대 1 | exp 필요 / 256B LUT | 출력 유계라 좋음 |
| ReLU | max(0,x) | [0, ∞) | 0 또는 1 | 비교 1회 / 공짜(clamp에 흡수) | 위가 열려 있어 outlier가 scale을 키움 |
| Leaky ReLU | x 또는 a·x | (−∞, ∞) | 1 또는 a | 비교+곱 / 곱셈+requant | 음수 쪽 별도 스케일 필요 |
| ReLU6 | min(max(0,x),6) | [0, 6] | 0 < x < 6에서 1 | clamp / 공짜(clamp에 흡수) | 매우 좋음. 범위 고정 |
| GELU | x·Φ(x) | 약 [−0.17, ∞) | Φ(x)+x·φ(x) | erf 또는 tanh / LUT | 음수 쪽 작은 꼬리. 보통 LUT |
| SiLU | x·σ(x) | 약 [−0.28, ∞) | σ(1+x(1−σ)) | exp+곱 / LUT | 보통. LUT 또는 hard-swish로 대체 |
| hard-sigmoid | ReLU6(x+3)/6 | [0, 1] | (−3,3)에서 1/6 | 덧셈+clamp+곱 / 정수 연산 | 매우 좋음 |
| hard-swish | x·ReLU6(x+3)/6 | [−0.375, ∞) | (2x+3)/6 (구간 내) | 덧셈+clamp+곱 2회 / 정수 연산 | 좋음. 정수로 정확히 계산 가능 |

말로 하면: 비용 순서는 대략 ReLU·ReLU6 < hard 계열 < LUT 한 번(int8일 땐 사실 모든 원소별 함수가 LUT 한 번) < float exp/erf. 양자화 친화도는 "출력 범위가 좁고 미리 알려져 있을수록" 좋다.

### 3.8 흔한 함정

- **출력층에 ReLU**: 회귀 목표가 음수가 될 수 있는데 ReLU를 두면 음수를 절대 못 낸다. 분류라면 logits에 활성화를 두지 않는다.
- **sigmoid 뒤에 `BCELoss`, 아니면 logits에 `BCEWithLogitsLoss`**: 둘을 섞어 sigmoid를 두 번 적용하는 실수(A2 7절의 softmax 두 번과 같은 종류).
- **dead ReLU**: 학습률이 너무 크면 한 번의 큰 update로 bias가 크게 음수가 되어 뉴런이 영구히 꺼진다. hidden 출력이 항상 0인 채널 비율을 로깅해 보면 바로 보인다.

---

## 4. Edge에서의 활성화 — ReLU6, hard-swish, LUT

### 4.1 출력 범위가 양자화 해상도를 결정한다

int8 양자화(C1에서 자세히)는 실수 범위 [min, max]를 정수 256단계로 나누는 것이다. `scale = (max − min) / 255`. 범위가 넓으면 한 단계(1 LSB)가 굵어진다. ADC의 full-scale을 너무 넓게 잡으면 작은 신호의 분해능이 떨어지는 것과 같다.

ReLU 출력은 위가 열려 있어서, 학습·calibration 데이터 중 **드문 큰 값 하나**가 max를 정한다. ReLU6는 max가 6으로 고정된다. 아래 코드는 대부분 작은 값이고 가끔(10만 개 중 20개) 20~40짜리 outlier가 섞인 pre-activation에서 두 경우의 uint8 양자화 오차를 비교한다.

```python
import numpy as np
rng = np.random.default_rng(0)
# pre-activation: 대부분 N(0, 1.5), 가끔 큰 outlier (예: 센서 충격 순간)
z = rng.normal(0, 1.5, 100_000)
z[rng.choice(z.size, 20, replace=False)] = rng.uniform(20, 40, 20)

def quant_uint8(a):                      # [0, max] 범위를 0..255로 (asymmetric, zp=0)
    scale = a.max() / 255.0
    q = np.clip(np.round(a / scale), 0, 255)
    return q * scale, scale

for name, a in [("ReLU ", np.maximum(z, 0)), ("ReLU6", np.clip(z, 0, 6))]:
    deq, s = quant_uint8(a)
    typical = a < 6                       # outlier가 아닌 99.98% 샘플
    err = np.abs(deq - a)[typical].mean()
    print(f"{name}: max={a.max():6.2f}  scale={s:.4f}  "
          f"mean|q-err| (typical)={err:.4f}  levels used below 6.0: {int(6/s)}")
```

```text
ReLU : max= 36.66  scale=0.1437  mean|q-err| (typical)=0.0180  levels used below 6.0: 41
ReLU6: max=  6.00  scale=0.0235  mean|q-err| (typical)=0.0029  levels used below 6.0: 255
```

출력에서 볼 것: ReLU는 outlier(36.66) 때문에 scale이 0.144가 되어, 정작 대부분의 값이 사는 0~6 구간에 41단계밖에 못 쓴다. ReLU6는 같은 구간에 255단계를 전부 써서 평균 오차가 약 6배 작다. 물론 ReLU6는 6 이상을 잘라 버리므로, "모델이 6 이상 값을 필요로 하지 않게" **학습 때부터** ReLU6를 써야 한다. 배포 직전에 ReLU를 ReLU6로 바꾸면 모델이 달라진다.

> 참고 — 요즘은 per-channel 양자화와 percentile calibration(C1) 덕분에 ReLU 모델도 int8에서 잘 동작하는 경우가 많다. ReLU6는 "양자화를 쉽게 만드는 한 가지 설계 선택"이지 필수는 아니다.

### 4.2 int8에서 ReLU/ReLU6는 "공짜"다

int8 dense/conv 커널은 int32 누산기 값을 requantize해서 int8로 내보낼 때 어차피 `[−128, 127]`로 clamp한다. ReLU/ReLU6는 이 clamp의 **하한·상한만 바꾸면** 된다. 예를 들어 출력 scale이 6/255, zero_point가 −128이라면 ReLU6의 [0, 6]은 정수로 [−128, 127] 전체가 되어 추가 연산이 0이다. TFLite 모델 파일의 FullyConnected·Conv2D op에 `fused_activation_function`(NONE, RELU, RELU6 등) 필드가 있는 게 이 때문이다 — 활성화가 별도 op가 아니라 **앞 op의 옵션**이 된다.

### 4.3 C로 구현: ReLU6, hard-swish, LUT sigmoid/tanh

펌웨어에서 쓰는 형태로 직접 짜 보자. 입력은 int8, `scale = 1/16`(Q4.4 고정소수점: 실수 = q/16, 범위 −8.0 ~ +7.94)이다.

- **ReLU6**: 6.0 = 96이므로 `clamp(q, 0, 96)`.
- **hard-swish**: `x·ReLU6(x+3)/6`을 정수로. `x+3` → `q + 48`, clamp [0, 96], 곱하면 scale이 1/256, 출력을 다시 1/16 scale로 만들려면 `÷(6·16) = ÷96` (반올림).
- **sigmoid / tanh**: int8 입력은 **256가지뿐**이다. 그러니 256개 출력을 미리 계산해 둔 표(LUT, look-up table)를 만들면 추론은 배열 인덱싱 한 번이다. 출력 양자화는 TFLite 관례를 따라 sigmoid는 scale 1/256·zero_point −128, tanh는 scale 1/128·zero_point 0.

```c
#include <math.h>
#include <stdint.h>
#include <stdio.h>

/* 입력 int8: real = q / 16  (scale 1/16, zero_point 0 → 범위 -8.0 ~ +7.94) */
#define IN_SCALE (1.0f / 16.0f)

static int8_t relu6_q(int8_t q) {             /* 6.0 → 96 */
    return q < 0 ? 0 : (q > 96 ? 96 : q);
}
static int8_t hswish_q(int8_t q) {            /* x·relu6(x+3)/6, 출력 scale도 1/16 */
    int32_t c = q + 48;                       /* x+3  (3.0 → 48) */
    c = c < 0 ? 0 : (c > 96 ? 96 : c);        /* relu6 */
    int32_t p = (int32_t)q * c;               /* scale 1/256 */
    int32_t y = (p >= 0 ? p + 48 : p - 48) / 96; /* ÷(6·16), 반올림 */
    return (int8_t)y;
}
static int8_t lut_sig[256], lut_tanh[256];    /* 256 B씩: int8 입력 전부 */
static void build_luts(void) {                /* 보통은 오프라인 생성 const 배열 */
    for (int i = 0; i < 256; i++) {
        float x = (float)(int8_t)i * IN_SCALE;
        long s = lroundf(256.0f / (1.0f + expf(-x))) - 128; /* out: 1/256, zp -128 */
        long t = lroundf(128.0f * tanhf(x));                /* out: 1/128, zp 0 */
        lut_sig[i]  = (int8_t)(s > 127 ? 127 : s);
        lut_tanh[i] = (int8_t)(t > 127 ? 127 : t);
    }
}
int main(void) {
    build_luts();
    const int8_t xs[] = {-128, -48, -16, 0, 16, 40, 96, 127};
    printf("  q    x    relu6  hswish(q→real)  sig_q(real)   tanh_q(real)\n");
    for (unsigned k = 0; k < sizeof xs; k++) {
        int8_t q = xs[k];
        int8_t hs = hswish_q(q), s = lut_sig[(uint8_t)q], t = lut_tanh[(uint8_t)q];
        printf("%4d %6.3f %4d %5d(%7.4f) %5d(%6.4f) %5d(%7.4f)\n", q, q * IN_SCALE,
               relu6_q(q), hs, hs * IN_SCALE, s, (s + 128) / 256.0, t, t / 128.0);
    }
    float e_hs = 0, e_s = 0;                  /* 256개 입력 전부에서 최대 오차 */
    for (int i = -128; i < 128; i++) {
        float x = i * IN_SCALE;
        float hs_ref = x * fminf(fmaxf(x + 3, 0), 6) / 6;
        e_hs = fmaxf(e_hs, fabsf(hswish_q((int8_t)i) * IN_SCALE - hs_ref));
        e_s  = fmaxf(e_s, fabsf((lut_sig[(uint8_t)i] + 128) / 256.0f - 1 / (1 + expf(-x))));
    }
    printf("max err: hswish=%.4f (1 LSB=%.4f)  sigmoid=%.4f (1 LSB=%.4f)\n",
           e_hs, IN_SCALE, e_s, 1 / 256.0);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 act.c -o act -lm && ./act
```

```text
  q    x    relu6  hswish(q→real)  sig_q(real)   tanh_q(real)
-128 -8.000    0     0( 0.0000)  -128(0.0000)  -128(-1.0000)
 -48 -3.000    0     0( 0.0000)  -116(0.0469)  -127(-0.9922)
 -16 -1.000    0    -5(-0.3125)   -59(0.2695)   -97(-0.7578)
   0  0.000    0     0( 0.0000)     0(0.5000)     0( 0.0000)
  16  1.000   16    11( 0.6875)    59(0.7305)    97( 0.7578)
  40  2.500   40    37( 2.3125)   109(0.9258)   126( 0.9844)
  96  6.000   96    96( 6.0000)   127(0.9961)   127( 0.9922)
 127  7.938   96   127( 7.9375)   127(0.9961)   127( 0.9922)
max err: hswish=0.0312 (1 LSB=0.0625)  sigmoid=0.0035 (1 LSB=0.0039)
```

출력에서 볼 것: hard-swish(−1) = −1·2/6 = −0.333을 정수 −5(= −0.3125)로 계산했다 — 1 LSB(0.0625) 안쪽. 256개 입력 전체의 최대 오차도 hard-swish 0.031(반 LSB), sigmoid 0.0035다. sigmoid 오차가 반 LSB(0.002)보다 조금 큰 이유는 int8 출력 최댓값 127이 (127+128)/256 = 0.996까지만 표현하기 때문이다 — sigmoid(7.94) = 0.9996을 담을 수 없는 **포화 오차**다.

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="b1arr2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="80" width="110" height="44" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="75" y="100" font-size="13" text-anchor="middle">int8 입력 q</text><text x="75" y="116" font-size="12" text-anchor="middle">예: q = 16</text>
<line x1="130" y1="102" x2="198" y2="102" stroke="currentColor" marker-end="url(#b1arr2)"/><text x="164" y="94" font-size="12" text-anchor="middle">(uint8)q</text>
<rect x="200" y="20" width="200" height="170" fill="none" stroke="currentColor"/>
<line x1="200" y1="44" x2="400" y2="44" stroke="currentColor" stroke-opacity="0.4"/><line x1="200" y1="68" x2="400" y2="68" stroke="currentColor" stroke-opacity="0.4"/><line x1="200" y1="92" x2="400" y2="92" stroke="currentColor" stroke-opacity="0.4"/><line x1="200" y1="116" x2="400" y2="116" stroke="currentColor" stroke-opacity="0.4"/><line x1="200" y1="140" x2="400" y2="140" stroke="currentColor" stroke-opacity="0.4"/><line x1="200" y1="164" x2="400" y2="164" stroke="currentColor" stroke-opacity="0.4"/>
<line x1="260" y1="20" x2="260" y2="190" stroke="currentColor" stroke-opacity="0.4"/>
<text x="230" y="37" font-size="12" text-anchor="middle">[0]</text><text x="330" y="37" font-size="12" text-anchor="middle">0 (x=0.0)</text>
<text x="230" y="61" font-size="12" text-anchor="middle">⋮</text><text x="330" y="61" font-size="12" text-anchor="middle">⋮</text>
<rect x="201" y="93" width="198" height="22" fill="#e08a3c" fill-opacity="0.35"/>
<text x="230" y="109" font-size="12" text-anchor="middle">[16]</text><text x="330" y="109" font-size="12" text-anchor="middle">59 (x=1.0)</text>
<text x="230" y="133" font-size="12" text-anchor="middle">⋮</text><text x="330" y="133" font-size="12" text-anchor="middle">⋮</text>
<text x="230" y="157" font-size="12" text-anchor="middle">[128]</text><text x="330" y="157" font-size="12" text-anchor="middle">−128 (x=−8.0)</text>
<text x="230" y="181" font-size="12" text-anchor="middle">[255]</text><text x="330" y="181" font-size="12" text-anchor="middle">−4 (x=−0.0625)</text>
<text x="300" y="210" font-size="12" text-anchor="middle">lut_sig[256] — 256 B, 오프라인 계산</text>
<line x1="400" y1="104" x2="468" y2="104" stroke="currentColor" marker-end="url(#b1arr2)"/>
<rect x="470" y="80" width="190" height="44" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="565" y="100" font-size="13" text-anchor="middle">int8 출력 = 59</text><text x="565" y="116" font-size="12" text-anchor="middle">(59+128)/256 = 0.7305</text>
</svg>
```

그림 4 — int8 LUT 활성화. 입력 q를 unsigned로 바꿔 인덱스로 쓰면 256바이트 표에서 출력 int8을 한 번에 읽는다. q=16(x=1.0) → 59 → 0.7305 ≈ σ(1) = 0.7311. 인덱스 128~255는 음수 입력(two's complement)이다.

### 4.4 실제 런타임·NPU는 비선형을 어떻게 처리하나 (일반론)

- **int8 원소별 함수는 결국 256-entry LUT로 만들 수 있다**: 입력이 256가지뿐이라 어떤 함수든(sigmoid, tanh, GELU, SiLU, 심지어 hard-swish도) 표 하나면 된다. 표는 모델의 입력·출력 scale/zero_point에 따라 달라지므로 op마다 따로 만든다(보통 변환 시점이나 커널 초기화 시점에 계산). 256 B × op 수라 SRAM/flash 부담도 작다.
- **int16은 표가 너무 크다**(65,536 항목). 그래서 상위 비트로 작은 표(예: 수백 항목)를 찾고 하위 비트로 **선형 보간**하는 방식이 흔하다.
- **CMSIS-NN**(Arm Cortex-M용 커널 라이브러리): 예전 q7 API에는 표 기반 sigmoid/tanh가 있었고, 최신 버전의 int16 활성화 함수도 표 + 보간 방식을 쓴다. TFLite Micro의 reference 커널은 표 대신 고정소수점 근사 계산을 쓰는 경우도 있다. **버전·커널마다 방식이 다르므로** 실제 프로젝트에선 소스를 확인해야 한다.
- **NPU**: 많은 NPU가 MAC 배열 뒤에 "activation 유닛"을 두는데, ReLU류는 clamp로, sigmoid/tanh 같은 곡선은 **LUT 또는 구간별 선형(piecewise-linear) 근사**로 처리하는 경우가 많다. 이 유닛이 지원하지 않는 함수(예: 특정 GELU 변형)를 쓰면 그 op만 CPU로 떨어지는(fallback) 일이 생긴다 → 지연·전력 급증. 모델을 설계할 때 타깃 NPU의 지원 op 목록을 먼저 확인하는 이유다(C6, I1).

---

## 5. 정규화(normalization) 왜 필요한가 — 그리고 BatchNorm

### 5.1 직관: 층마다 입력 스케일이 흔들린다

A2 4절에서 입력 특징을 z-score로 표준화하면 학습이 잘 된다는 걸 봤다. 그런데 **중간 층의 입력**(앞 층의 출력)은 누가 표준화해 주나? 학습 중엔 앞 층의 weight가 계속 바뀌므로 뒤 층이 보는 값의 평균·분산이 step마다 흔들린다. 층이 깊을수록 이 흔들림이 누적되어(A3 8절: 층마다 곱해지는 효과) 어떤 층은 값이 폭주하고 어떤 층은 0 근처로 쪼그라든다. 그러면 learning rate 하나로 모든 층을 동시에 잘 학습시키기 어렵다.

**정규화 층**은 중간 값을 매번 "평균 0, 분산 1"로 다시 맞춘 다음, 학습 가능한 scale γ와 shift β로 필요한 만큼만 다시 늘린다. 펌웨어로 치면 **AGC(automatic gain control)** 다. 다음 단계(ADC, 뒤 층)가 항상 적당한 레벨의 신호를 받게 해 준다.

> 참고 — BatchNorm 원 논문(Ioffe & Szegedy, 2015)은 이 현상을 "internal covariate shift"라고 불렀다. 이후 연구(Santurkar et al., 2018)는 BN의 진짜 효과가 loss 지형을 매끄럽게 만들어 큰 learning rate를 허용하는 것이라고 반박했다. 면접에선 "학습을 안정시키고 더 큰 learning rate를 쓸 수 있게 한다"라고 말하면 안전하다.

### 5.2 공통 공식

어떤 정규화든 모양은 같다. 다른 것은 **평균·분산을 어떤 원소들 위에서 계산하느냐**(reduction axis)뿐이다.

```
 x̂ = (x − μ) / √(σ² + ε)        μ, σ² = 정해진 원소 묶음의 평균·분산,  ε ≈ 1e-5 (0 나누기 방지)
 y = γ · x̂ + β                  γ(scale), β(shift) = 학습되는 파라미터
```

말로 하면: 묶음 안에서 평균을 빼고 표준편차로 나눠 "평균 0, 분산 1"로 만든 뒤, 층이 원하는 크기(γ)와 위치(β)로 다시 옮긴다. γ=σ, β=μ가 되면 정규화를 "되돌리는" 것도 가능하므로 표현력을 잃지 않는다.

### 5.3 BatchNorm — 채널마다, 배치 전체에서

**BatchNorm(BN)** 은 텐서 `[N, C, L]`(배치, 채널, 길이 — A5의 Conv1d 레이아웃)에서 **채널 c마다** 배치 N개와 길이 L 전체의 값을 모아 평균·분산을 낸다. 통계는 채널 수 C개만큼 나온다. dense layer 출력 `[N, C]`라면 L이 없을 뿐 같다(`BatchNorm1d`가 둘 다 받는다).

**손계산.** `N=2, C=3, L=2`:

```
 샘플 0:  c0 = [1, 3]   c1 = [2, 2]   c2 = [0, 4]
 샘플 1:  c0 = [5, 7]   c1 = [2, 6]   c2 = [4, 0]

 c0 묶음 {1, 3, 5, 7}:  μ = 4,  σ² = (9+1+1+9)/4 = 5     → 샘플0 c0: (1−4)/√5 = −1.342,  (3−4)/√5 = −0.447
 c1 묶음 {2, 2, 2, 6}:  μ = 3,  σ² = (1+1+1+9)/4 = 3     → 샘플0 c1: (2−3)/√3 = −0.577 (두 칸 모두)
 c2 묶음 {0, 4, 4, 0}:  μ = 2,  σ² = (4+4+4+4)/4 = 4     → 샘플0 c2: (0−2)/2 = −1,  (4−2)/2 = +1
```

(γ=1, β=0, ε=0으로 둔 값.) 6절 코드에서 같은 숫자가 나오는지 확인한다.

### 5.4 train 모드 vs eval 모드 — BN에서 가장 중요한 것

BN의 통계는 **배치**에서 나온다. 그런데 기기에서 추론할 때는 샘플이 한 번에 하나씩 들어온다(batch=1). 배치 통계를 쓸 수 없다. 그래서 BN은 두 가지 모드로 동작한다.

| 모드 | 정규화에 쓰는 μ, σ² | running 통계 | 언제 |
|---|---|---|---|
| train (`model.train()`) | **현재 배치**의 평균·분산 (biased, ÷n) | 매 step 갱신 | 학습 |
| eval (`model.eval()`) | 저장된 **running_mean, running_var** | 고정 | 검증·추론·배포 |

running 통계는 학습 중 배치 통계의 **지수이동평균(EMA)** 이다. PyTorch 식:

```
 running_mean ← (1 − m) · running_mean + m · batch_mean            m = momentum = 0.1 (기본값)
 running_var  ← (1 − m) · running_var  + m · batch_var_unbiased    ← 여기만 ÷(n−1)
```

말로 하면: 매 step마다 기존 값 90%에 새 배치 값 10%를 섞는다. 펌웨어의 1차 IIR 저역통과(`avg += α·(x − avg)`)와 똑같다. 초기값은 mean 0, var 1이다.

**손계산.** 위 텐서로 train forward를 한 번 하면 c0의 배치 평균 4 → `running_mean = 0.9·0 + 0.1·4 = 0.4`. 분산은 running 쪽에 **unbiased**(÷(n−1)) 값을 넣으므로 `20/3 = 6.667` → `running_var = 0.9·1 + 0.1·6.667 = 1.5667`.

아래 코드는 이 갱신을 확인하고, 같은 입력이 train/eval에서 어떻게 다르게 나오는지, batch=1 학습이 왜 막히는지 보여 준다.

```python
import torch, torch.nn as nn
torch.set_printoptions(precision=4, sci_mode=False)
x = torch.tensor([[[1., 3.], [2., 2.], [0., 4.]],
                  [[5., 7.], [2., 6.], [4., 0.]]])
bn = nn.BatchNorm1d(3)                      # momentum=0.1, eps=1e-5 (기본값)
print("init  running_mean", bn.running_mean, " running_var", bn.running_var)
bn.train(); _ = bn(x)                       # train 모드 forward 1회 → 통계 갱신
print("1 step running_mean", bn.running_mean, " running_var", bn.running_var)
print("unbiased var of batch:", x.var(dim=(0, 2), unbiased=True))

# 같은 분포의 배치를 많이 보여 주면 running 통계가 수렴한다
torch.manual_seed(0)
true_mean = torch.tensor([4., 3., 2.]).view(1, 3, 1)
for _ in range(200):
    bn(true_mean + 2.0 * torch.randn(32, 3, 16))
print("200 steps running_mean", bn.running_mean, " running_var", bn.running_var)

# train vs eval: 같은 입력, 다른 출력
probe = torch.tensor([[[4.], [3.], [2.]], [[6.], [5.], [4.]]])   # N=2, L=1
bn.train(); print("train out (batch stats):", bn(probe)[:, :, 0].detach())
bn.eval();  print("eval  out (running)    :", bn(probe)[:, :, 0].detach())
bn.train()
try:
    bn(probe[:1])                            # 샘플 1개, L=1 → 채널당 값 1개
except ValueError as e:
    print("train, batch=1:", str(e)[:60])
```

```text
init  running_mean tensor([0., 0., 0.])  running_var tensor([1., 1., 1.])
1 step running_mean tensor([0.4000, 0.3000, 0.2000])  running_var tensor([1.5667, 1.3000, 1.4333])
unbiased var of batch: tensor([6.6667, 4.0000, 5.3333])
200 steps running_mean tensor([3.9890, 2.9874, 1.9808])  running_var tensor([4.0638, 4.1191, 3.9868])
train out (batch stats): tensor([[-1.0000, -1.0000, -1.0000],
        [ 1.0000,  1.0000,  1.0000]])
eval  out (running)    : tensor([[-0.0459, -0.0448, -0.0425],
        [ 0.9724,  0.9670,  0.9851]])
train, batch=1: Expected more than 1 value per channel when training, got in
```

출력에서 볼 것:

- 1 step 뒤 running_mean = 0.1 × 배치 평균, running_var = 0.9 + 0.1 × **unbiased** 분산(6.667) — 손계산과 같다.
- 평균 [4, 3, 2], 표준편차 2(분산 4)인 배치를 200번 보여 주니 running 통계가 참값에 수렴했다.
- probe 두 샘플은 train 모드에선 **서로를 기준으로** 정규화되어 무조건 −1, +1이 된다(값 2개의 평균·분산). eval 모드에선 running 통계로 정규화되어 "평균에서 얼마나 떨어졌나"라는 의미 있는 값이 나온다.
- train 모드에서 채널당 값이 1개면 분산을 못 구하므로 PyTorch가 에러를 낸다.

### 5.5 BN의 함정 (배포 관점)

- **`model.eval()`을 잊음**: 추론·export 시 train 모드면 batch=1 입력이 자기 자신으로 정규화되어 쓰레기가 나온다. ONNX/TFLite export 전에 반드시 eval(A5 5절).
- **running 통계와 실제 데이터의 불일치**: 학습 데이터(예: 실험실에서 손목에 꽉 찬 IMU)와 필드 데이터(헐겁게 착용)의 분포가 다르면 running 통계가 틀린 기준이 된다. 흔한 처방은 소량의 필드 데이터로 running 통계만 다시 추정하는 것(BN re-calibration).
- **작은 batch로 학습**: batch 2~4면 배치 통계가 잡음투성이라 학습이 불안정하다 → GroupNorm/LayerNorm을 고려(6절).
- **momentum 관례 차이**: PyTorch `momentum=0.1`은 "새 값 비중", Keras/TensorFlow의 `momentum=0.99`는 "기존 값 비중"이다. 모델을 프레임워크 간 옮길 때 헷갈리기 쉽다.

---

## 6. LayerNorm, RMSNorm, GroupNorm — 샘플 하나로 정규화하기

### 6.1 무엇을 묶어 평균 내나

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<text x="135" y="22" font-size="14" text-anchor="middle">BatchNorm</text><text x="135" y="40" font-size="12" text-anchor="middle">채널마다: (N, L) 축 평균</text><text x="90.0" y="62" font-size="12" text-anchor="middle">n=0</text><rect x="60" y="70" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="80" y="70" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="100" y="70" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><text x="54" y="84" font-size="12" text-anchor="end">c0</text><rect x="60" y="90" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="80" y="90" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/>
<rect x="100" y="90" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><text x="54" y="104" font-size="12" text-anchor="end">c1</text><rect x="60" y="110" width="20" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="80" y="110" width="20" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="100" y="110" width="20" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><text x="54" y="124" font-size="12" text-anchor="end">c2</text><rect x="60" y="130" width="20" height="20" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="80" y="130" width="20" height="20" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/>
<rect x="100" y="130" width="20" height="20" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><text x="54" y="144" font-size="12" text-anchor="end">c3</text><text x="90.0" y="166" font-size="12" text-anchor="middle">L →</text><text x="180.0" y="62" font-size="12" text-anchor="middle">n=1</text><rect x="150" y="70" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="170" y="70" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="190" y="70" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="150" y="90" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/>
<rect x="170" y="90" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="190" y="90" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="150" y="110" width="20" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="170" y="110" width="20" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="190" y="110" width="20" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="150" y="130" width="20" height="20" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="170" y="130" width="20" height="20" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/>
<rect x="190" y="130" width="20" height="20" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><text x="180.0" y="166" font-size="12" text-anchor="middle">L →</text><text x="135" y="190" font-size="12" text-anchor="middle">통계 C=4세트</text><text x="360" y="22" font-size="14" text-anchor="middle">LayerNorm</text><text x="360" y="40" font-size="12" text-anchor="middle">샘플마다: (C, L) 축 평균</text><text x="315.0" y="62" font-size="12" text-anchor="middle">n=0</text><rect x="285" y="70" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="305" y="70" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="325" y="70" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/>
<text x="279" y="84" font-size="12" text-anchor="end">c0</text><rect x="285" y="90" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="305" y="90" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="325" y="90" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><text x="279" y="104" font-size="12" text-anchor="end">c1</text><rect x="285" y="110" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="305" y="110" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="325" y="110" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/>
<text x="279" y="124" font-size="12" text-anchor="end">c2</text><rect x="285" y="130" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="305" y="130" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="325" y="130" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><text x="279" y="144" font-size="12" text-anchor="end">c3</text><text x="315.0" y="166" font-size="12" text-anchor="middle">L →</text><text x="405.0" y="62" font-size="12" text-anchor="middle">n=1</text><rect x="375" y="70" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="395" y="70" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/>
<rect x="415" y="70" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="375" y="90" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="395" y="90" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="415" y="90" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="375" y="110" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="395" y="110" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="415" y="110" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/>
<rect x="375" y="130" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="395" y="130" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="415" y="130" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><text x="405.0" y="166" font-size="12" text-anchor="middle">L →</text><text x="360" y="190" font-size="12" text-anchor="middle">통계 N=2세트</text><text x="585" y="22" font-size="14" text-anchor="middle">GroupNorm (G=2)</text><text x="585" y="40" font-size="12" text-anchor="middle">샘플×그룹마다: (C/G, L)</text><text x="540.0" y="62" font-size="12" text-anchor="middle">n=0</text><rect x="510" y="70" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/>
<rect x="530" y="70" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="550" y="70" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><text x="504" y="84" font-size="12" text-anchor="end">c0</text><rect x="510" y="90" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="530" y="90" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="550" y="90" width="20" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><text x="504" y="104" font-size="12" text-anchor="end">c1</text><rect x="510" y="110" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/>
<rect x="530" y="110" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="550" y="110" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><text x="504" y="124" font-size="12" text-anchor="end">c2</text><rect x="510" y="130" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="530" y="130" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="550" y="130" width="20" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><text x="504" y="144" font-size="12" text-anchor="end">c3</text><text x="540.0" y="166" font-size="12" text-anchor="middle">L →</text><text x="630.0" y="62" font-size="12" text-anchor="middle">n=1</text>
<rect x="600" y="70" width="20" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="620" y="70" width="20" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="640" y="70" width="20" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="600" y="90" width="20" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="620" y="90" width="20" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="640" y="90" width="20" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="600" y="110" width="20" height="20" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/>
<rect x="620" y="110" width="20" height="20" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="640" y="110" width="20" height="20" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="600" y="130" width="20" height="20" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="620" y="130" width="20" height="20" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><rect x="640" y="130" width="20" height="20" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.8"/><text x="630.0" y="166" font-size="12" text-anchor="middle">L →</text><text x="585" y="190" font-size="12" text-anchor="middle">통계 N·G=4세트</text><text x="340" y="225" font-size="12" text-anchor="middle">텐서 [N=2, C=4, L=3]. 같은 색 칸끼리 평균·분산을 같이 계산한다.</text>
</svg>
```

그림 5 — `[N=2, C=4, L=3]` 텐서에서 같은 색 칸끼리 평균·분산을 계산한다. BatchNorm은 채널별로 **샘플을 가로질러**, LayerNorm은 **샘플 하나 안에서** 전부, GroupNorm은 샘플 하나 안에서 채널 그룹별로 묶는다.

| 정규화 | 통계를 내는 축 | 통계 개수 | 배치에 의존? | train/eval 차이 | 주로 쓰는 곳 |
|---|---|---|---|---|---|
| BatchNorm | 채널마다 (N, L) | C | 예 | 있음 (running 통계) | CNN (ResNet, MobileNet), IMU/오디오 CNN |
| LayerNorm | 샘플(토큰)마다 (특징 축 전체) | N (토큰 수) | 아니오 | 없음 | Transformer (BERT, ViT, Whisper) |
| RMSNorm | 토큰마다 (특징 축), 평균 빼기 없음 | 토큰 수 | 아니오 | 없음 | LLM (Llama, Qwen, Gemma 등) |
| GroupNorm | 샘플마다 × 채널 그룹마다 | N × G | 아니오 | 없음 | 작은 batch 학습 (검출·분할), 일부 오디오 모델 |

> 참고 — Transformer에서는 텐서가 `[N, T, D]`(배치, 토큰, 특징)이고 LayerNorm은 **토큰 하나의 D개 특징** 위에서 정규화한다(`nn.LayerNorm(D)`). 그림 5의 `[N, C, L]`에서 "LayerNorm = (C, L) 전체"는 CNN 식 레이아웃에 `nn.LayerNorm([C, L])`을 쓴 경우다. 어느 쪽이든 핵심은 "배치의 다른 샘플을 보지 않는다"는 것이다.

### 6.2 LayerNorm

```
 μ = (1/D) ∑_i x_i,    σ² = (1/D) ∑_i (x_i − μ)²
 y_i = γ_i · (x_i − μ) / √(σ² + ε) + β_i
```

말로 하면: 토큰 벡터 하나의 원소들로 평균·분산을 내서 표준화한다. 배치가 필요 없으니 batch=1 추론에서도 학습 때와 **똑같이** 동작한다(train/eval 차이 없음, running 통계 없음). 그래서 문장 길이·배치 크기가 들쭉날쭉한 Transformer와 RNN에 맞다.

**손계산.** `v = [2, 4, 6, 8]`: μ = 5, σ² = (9+1+1+9)/4 = 5, √5 = 2.236 → `[−1.342, −0.447, 0.447, 1.342]`.

### 6.3 RMSNorm

```
 rms = √( (1/D) ∑_i x_i² + ε )
 y_i = γ_i · x_i / rms            (β 없음, 평균 빼기 없음)
```

말로 하면: 평균을 빼지 않고, 제곱평균제곱근(RMS)으로만 나눈다. "크기만 맞추고 중심은 안 옮긴다". Zhang & Sennrich(2019)가 제안했고, 평균 계산 한 번과 β가 빠져서 더 싸면서 성능은 거의 같아 Llama 이후 대부분의 LLM이 쓴다(B4, B8).

**손계산.** `v = [2, 4, 6, 8]`: 제곱합 4+16+36+64 = 120, /4 = 30, √30 = 5.477 → `[0.365, 0.730, 1.095, 1.461]`. LayerNorm과 달리 결과가 전부 양수다 — 평균을 안 뺐기 때문이다. RMS 값은 AC+DC 전체 전력, 표준편차는 AC 성분만의 RMS라고 생각하면 차이가 선명하다.

### 6.4 GroupNorm (간단히)

채널 C개를 G개 그룹으로 나눠 **샘플마다, 그룹마다** 평균·분산을 낸다. G=1이면 LayerNorm([C, L])과 같고, G=C면 InstanceNorm(채널 하나씩)이다. 배치에 의존하지 않으면서 CNN의 채널 구조를 어느 정도 살리므로, GPU 메모리 때문에 batch를 2~4로밖에 못 잡는 모델에서 BN 대신 쓴다.

### 6.5 코드로 확인

아래 코드는 5.3절의 BN 손계산, LayerNorm·GroupNorm의 관계, 토큰 벡터 `[2, 4, 6, 8]`의 LN vs RMSNorm을 PyTorch 모듈로 확인한다(`nn.RMSNorm`은 PyTorch 2.4부터 있다).

```python
import torch, torch.nn as nn
torch.set_printoptions(precision=3, sci_mode=False)
x = torch.tensor([[[1., 3.], [2., 2.], [0., 4.]],      # 샘플 0: [C=3, L=2]
                  [[5., 7.], [2., 6.], [4., 0.]]])     # 샘플 1
# BatchNorm: 채널마다 (N, L) 축으로 통계 → 채널별 mean/var 3개
mu_bn  = x.mean(dim=(0, 2));  var_bn = x.var(dim=(0, 2), unbiased=False)
print("BN  mean per C:", mu_bn, " var per C:", var_bn)
bn = nn.BatchNorm1d(3, eps=0.0).train()
print("BN out[0]:\n", bn(x)[0].detach())
# LayerNorm: 샘플마다 (C, L) 축 → 샘플별 mean/var 2개
ln = nn.LayerNorm([3, 2], eps=0.0, elementwise_affine=False)
print("LN  mean per N:", x.mean(dim=(1, 2)), " var per N:", x.var(dim=(1, 2), unbiased=False))
# GroupNorm(groups=1)은 이 경우 LN([C,L])과 같다 / groups=C면 InstanceNorm
gn1 = nn.GroupNorm(1, 3, eps=0.0, affine=False)
print("GN(1 group) == LN ?", torch.allclose(gn1(x), ln(x)))

# 토큰 벡터 하나: LayerNorm vs RMSNorm (transformer 방식, 마지막 축 D)
v = torch.tensor([[2., 4., 6., 8.]])
print("LN  :", nn.LayerNorm(4, eps=0.0, elementwise_affine=False)(v))
print("RMS :", nn.RMSNorm(4, eps=0.0, elementwise_affine=False)(v))
print("RMS manual:", v / v.pow(2).mean(-1, keepdim=True).sqrt())
```

```text
BN  mean per C: tensor([4., 3., 2.])  var per C: tensor([5., 3., 4.])
BN out[0]:
 tensor([[-1.342, -0.447],
        [-0.577, -0.577],
        [-1.000,  1.000]])
LN  mean per N: tensor([2., 4.])  var per N: tensor([1.667, 5.667])
GN(1 group) == LN ? True
LN  : tensor([[-1.342, -0.447,  0.447,  1.342]])
RMS : tensor([[0.365, 0.730, 1.095, 1.461]])
RMS manual: tensor([[0.365, 0.730, 1.095, 1.461]])
```

출력에서 볼 것: BN 채널별 평균 [4, 3, 2], 분산 [5, 3, 4]와 샘플 0 출력이 손계산과 같다. LN은 샘플별 통계 2세트다. GroupNorm(G=1)은 LN([C, L])과 정확히 같다. RMSNorm은 수동 계산 `v / √mean(v²)`과 일치한다.

### 6.6 C로 보면: 추론 시점의 reduction

LayerNorm과 RMSNorm은 BN과 달리 **추론 때도 입력으로부터 통계를 계산**해야 한다. C로 쓰면 루프 구조가 드러난다.

```c
#include <math.h>
#include <stdio.h>
#define D 4
/* LayerNorm: 입력을 2번 훑는 reduction(평균, 분산) + 1번 정규화 = 3 pass */
static void layernorm(const float *x, const float *g, const float *b, float *y) {
    float mean = 0, var = 0;
    for (int i = 0; i < D; i++) mean += x[i];                  /* pass 1 */
    mean /= D;
    for (int i = 0; i < D; i++) var += (x[i] - mean) * (x[i] - mean); /* pass 2 */
    float inv = 1.0f / sqrtf(var / D + 1e-5f);
    for (int i = 0; i < D; i++) y[i] = (x[i] - mean) * inv * g[i] + b[i]; /* pass 3 */
}
/* RMSNorm: 평균 빼기 없음, β 없음 → reduction 1번 + 정규화 1번 = 2 pass */
static void rmsnorm(const float *x, const float *g, float *y) {
    float ss = 0;
    for (int i = 0; i < D; i++) ss += x[i] * x[i];             /* pass 1 */
    float inv = 1.0f / sqrtf(ss / D + 1e-5f);
    for (int i = 0; i < D; i++) y[i] = x[i] * inv * g[i];      /* pass 2 */
}
int main(void) {
    const float x[D] = {2, 4, 6, 8}, g[D] = {1, 1, 1, 1}, b[D] = {0, 0, 0, 0};
    float y1[D], y2[D];
    layernorm(x, g, b, y1); rmsnorm(x, g, y2);
    for (int i = 0; i < D; i++) printf("x=%.0f  LN=%+.4f  RMS=%+.4f\n", x[i], y1[i], y2[i]);
    return 0;
}
```

```text
x=2  LN=-1.3416  RMS=+0.3651
x=4  LN=-0.4472  RMS=+0.7303
x=6  LN=+0.4472  RMS=+1.0954
x=8  LN=+1.3416  RMS=+1.4606
```

출력에서 볼 것: 손계산과 같은 값. 구조적으로는 LayerNorm이 입력을 3번 훑고(평균 → 분산 → 정규화), RMSNorm이 2번 훑는다. 그리고 **첫 reduction이 끝나기 전엔 출력 원소를 하나도 만들 수 없다.** 9.3절에서 이게 NPU에서 왜 문제인지 다룬다. (실전 구현은 합과 제곱합을 한 번에 누적해 pass를 줄이기도 하지만, float 정밀도 문제가 있어 주의가 필요하다.)

---

## 7. BN folding — BatchNorm을 앞 층 weight 안으로 접어 넣기

### 7.1 직관

eval 모드의 BN은 running 통계와 γ, β가 **전부 상수**다. 그러면 BN은 채널마다 "상수를 곱하고 상수를 더하는" affine 변환일 뿐이다. 그리고 그 앞의 conv/linear도 affine이다. 1.3절에서 봤듯 affine 두 개는 하나로 합쳐진다. 펌웨어로 치면 센서 보정 파이프라인 `raw → (raw − offset) × gain → 단위 변환 × k`를 부팅 때 `raw × (gain·k) + (−offset·gain·k)` 한 번으로 미리 합쳐 두는 것 — A0 6절에서 입력 정규화를 가중치에 접어 넣은 것과 같은 트릭이다.

### 7.2 유도

앞 층 출력 채널 o에 대해 (conv면 W[o]는 커널 전체 `[C_in, K]`, linear면 W의 o번째 행):

```
 z[o] = W[o]·x + b[o]                                         ← conv / linear
 y[o] = γ[o] · (z[o] − μ[o]) / √(σ²[o] + ε) + β[o]            ← BN (eval: μ, σ² = running 통계)

 s[o] = γ[o] / √(σ²[o] + ε)                                    ← 채널별 스케일 (상수)

 y[o] = s[o] · (W[o]·x + b[o] − μ[o]) + β[o]
      = (s[o]·W[o])·x + ( s[o]·(b[o] − μ[o]) + β[o] )
      =     W′[o]  ·x +              b′[o]

 ∴  W′[o] = s[o] · W[o]              (출력 채널 o의 weight 전체에 같은 스케일)
     b′[o] = s[o] · (b[o] − μ[o]) + β[o]
```

말로 하면: 출력 채널마다 BN의 "나누기와 γ 곱하기"를 합친 스케일 s를 weight에 곱하고, 평균 빼기와 β 더하기를 bias에 몰아넣는다. 앞 층에 bias가 없으면(`bias=False`, BN 앞 conv에서 흔함) b = 0으로 두면 된다.

**손계산.** 1입력 1출력: `z = 2x + 1`, BN: μ = 3, σ² = 4, γ = 0.5, β = 1, ε = 0.

```
 s  = 0.5 / √4 = 0.25
 W′ = 0.25 · 2 = 0.5
 b′ = 0.25 · (1 − 3) + 1 = 0.5

 확인 x = 4:  원래  z = 9 → 0.5·(9 − 3)/2 + 1 = 2.5
             folded  0.5·4 + 0.5          = 2.5   ✓
```

### 7.3 PyTorch로 구현하고 검증

아래 코드는 IMU 6축 입력을 받는 Conv1d(6→8, kernel 5) + BatchNorm1d를 직접 fold하고, PyTorch의 `torch.nn.utils.fusion.fuse_conv_bn_eval`과도 비교해 출력이 같은지 확인한다.

```python
import torch, torch.nn as nn
from torch.nn.utils.fusion import fuse_conv_bn_eval
torch.manual_seed(0)
conv = nn.Conv1d(6, 8, kernel_size=5, padding=2)       # IMU 6축 → 8채널
bn = nn.BatchNorm1d(8)
with torch.no_grad():                                   # '학습된' 것처럼 통계·affine 채우기
    bn.running_mean.uniform_(-1, 1); bn.running_var.uniform_(0.5, 2.0)
    bn.weight.uniform_(0.5, 1.5);   bn.bias.uniform_(-0.5, 0.5)
conv.eval(); bn.eval()                                  # folding은 eval 통계로만 한다

def fold(conv, bn):
    s = bn.weight / torch.sqrt(bn.running_var + bn.eps)          # [C_out]
    W = conv.weight * s.view(-1, 1, 1)                           # 출력 채널별 스케일
    b = (conv.bias - bn.running_mean) * s + bn.bias
    f = nn.Conv1d(6, 8, 5, padding=2)
    f.weight.data, f.bias.data = W.detach(), b.detach()
    return f

x = torch.randn(4, 6, 50)                                         # [N, C, L]
with torch.no_grad():
    ref   = bn(conv(x))
    mine  = fold(conv, bn)(x)
    torch_ = fuse_conv_bn_eval(conv, bn)(x)
print("ref shape", tuple(ref.shape))
print("max|ref - my fold|    =", f"{(ref - mine).abs().max().item():.2e}")
print("max|ref - torch fuse| =", f"{(ref - torch_).abs().max().item():.2e}")
n_cb = sum(p.numel() for p in conv.parameters()) + 4 * bn.num_features  # γ,β,mean,var
print("stored numbers: conv+BN =", n_cb, " folded conv =",
      sum(p.numel() for p in fold(conv, bn).parameters()))
```

```text
ref shape (4, 8, 50)
max|ref - my fold|    = 7.15e-07
max|ref - torch fuse| = 7.15e-07
stored numbers: conv+BN = 280  folded conv = 248
```

출력에서 볼 것: 원래 conv→BN과 folded conv의 최대 차이 7e-7은 float32 반올림 수준(값 크기 대비 약 1e-7)이다. 즉 **수학적으로 같은 모델**이다. PyTorch 내장 함수와 결과가 같다. 저장할 숫자도 280 → 248개로 준다(BN의 γ, β, mean, var 32개가 사라짐).

### 7.4 C로 보면: fused 레이어 = MAC 루프 하나

같은 일을 펌웨어 코드로 보자. (A) 순진한 구현은 dense → BN → ReLU6를 세 번의 루프와 중간 버퍼 두 개로 한다. (B) 배포 구현은 오프라인에서 접은 W′, b′로 MAC 루프 하나를 돌고, ReLU6를 루프 끝(epilogue)에서 적용한다.

```c
#include <math.h>
#include <stdio.h>
#define IN 4
#define OUT 3
static const float W[OUT][IN] = {{0.5f,-1.0f,0.25f,2.0f},{1.5f,0.0f,-0.5f,1.0f},{-2.0f,1.0f,1.0f,0.5f}};
static const float B[OUT] = {0.1f, -0.2f, 0.3f};
/* BN (eval) 파라미터: gamma, beta, running mean/var */
static const float G[OUT] = {1.2f, 0.8f, 1.0f}, BE[OUT] = {0.0f, 0.5f, -0.1f};
static const float MU[OUT] = {1.0f, 2.0f, -1.0f}, VAR[OUT] = {4.0f, 0.25f, 1.0f};
#define EPS 1e-5f
static float relu6(float v) { return fminf(fmaxf(v, 0.0f), 6.0f); }

/* (A) 순진한 구현: 레이어 3개, 중간 버퍼 2개, 출력 배열을 3번 훑는다 */
static void unfused(const float *x, float *y) {
    float t1[OUT], t2[OUT];
    for (int o = 0; o < OUT; o++) { float acc = B[o];
        for (int i = 0; i < IN; i++) acc += W[o][i] * x[i];
        t1[o] = acc; }
    for (int o = 0; o < OUT; o++) t2[o] = G[o] * (t1[o] - MU[o]) / sqrtf(VAR[o] + EPS) + BE[o];
    for (int o = 0; o < OUT; o++) y[o] = relu6(t2[o]);
}
/* (B) 배포용: BN을 오프라인으로 접은 W', b' + ReLU6를 MAC 루프 끝(epilogue)에서 */
static float Wf[OUT][IN], Bf[OUT];
static void fold_bn(void) {                   /* 실제로는 변환 툴이 오프라인에서 */
    for (int o = 0; o < OUT; o++) {
        float s = G[o] / sqrtf(VAR[o] + EPS);
        for (int i = 0; i < IN; i++) Wf[o][i] = W[o][i] * s;
        Bf[o] = (B[o] - MU[o]) * s + BE[o];
    }
}
static void fused(const float *x, float *y) {
    for (int o = 0; o < OUT; o++) { float acc = Bf[o];
        for (int i = 0; i < IN; i++) acc += Wf[o][i] * x[i];
        y[o] = relu6(acc); }                  /* 한 번의 루프, 중간 버퍼 0 */
}
int main(void) {
    const float x[IN] = {1.0f, 2.0f, -1.0f, 3.0f};
    float ya[OUT], yb[OUT];
    fold_bn(); unfused(x, ya); fused(x, yb);
    for (int o = 0; o < OUT; o++)
        printf("out[%d]: unfused=%.6f fused=%.6f  W'row0=%.4f b'=%.4f\n",
               o, ya[o], yb[o], Wf[o][0], Bf[o]);
    return 0;
}
```

```text
out[0]: unfused=2.009998 fused=2.009998  W'row0=0.3000 b'=-0.5400
out[1]: unfused=4.979911 fused=4.979910  W'row0=2.4000 b'=-3.0199
out[2]: unfused=1.699991 fused=1.699991  W'row0=-2.0000 b'=1.2000
```

출력에서 볼 것: 두 구현이 float 반올림(1e-6) 안에서 같다. 채널 0을 손으로 확인하면 `z = 0.1 + 0.5·1 − 1·2 + 0.25·(−1) + 2·3 = 4.35`, BN: `1.2·(4.35 − 1)/√4 = 2.01`, ReLU6 통과 → 2.01. folded: `s = 1.2/2 = 0.6`, `W′[0][0] = 0.5·0.6 = 0.3`, `b′ = 0.6·(0.1 − 1) + 0 = −0.54` — 출력의 W′row0, b′와 같다.

### 7.5 왜 모든 배포 툴체인이 이걸 하나

- **op가 줄어든다**: BN 커널 호출이 사라진다. MCU에선 커널 호출마다 루프 셋업·포인터 계산·중간 버퍼 read/write가 드는데, 그게 통째로 없어진다. 원소별 연산(BN, ReLU)은 MAC이 적어도 **메모리 대역폭**을 먹는다 — 텐서를 한 번 더 읽고 쓰기 때문이다(D 모듈의 memory-bound 개념).
- **중간 텐서가 사라진다**: conv 출력을 저장했다가 BN이 다시 읽는 버퍼가 필요 없다 → peak SRAM 감소.
- **양자화가 쉬워진다**: 따로 두면 BN 출력도 int8로 양자화해야 하고 그때마다 반올림 오차가 한 번 더 들어간다. 접으면 conv 출력 한 번만 양자화하면 된다. 그리고 int8 커널은 "MAC → bias 더하기 → requantize → clamp" 형태가 표준인데, folded 모델은 정확히 이 모양이다.
- **NPU가 지원하는 형태**: NPU의 conv 엔진은 보통 "conv + bias + activation"을 한 번에 처리하도록 설계돼 있다. 독립 BN op는 아예 지원하지 않거나 느린 경로로 간다.

TFLite converter, ONNX Runtime의 graph optimizer, TensorRT, 모바일 NPU 벤더 컴파일러 등 대부분의 배포 툴체인이 이 folding을 자동으로 한다(C6 그래프 최적화).

### 7.6 folding의 함정

- **train 모드 BN은 접을 수 없다**: 배치 통계는 입력마다 달라서 상수가 아니다. 반드시 eval 통계로.
- **순서가 conv → BN이어야 한다**: BN이 **활성화 뒤**에 있으면(conv → ReLU → BN) ReLU가 중간에 끼어서 앞 conv로 접을 수 없다. (BN 다음 conv가 오는 경우 그 conv의 입력 쪽으로 접는 방법은 있지만, padding 경계에서 정확하지 않을 수 있어 툴이 안 해 줄 수 있다.)
- **채널별 스케일이 weight 범위를 벌린다**: s[o]는 채널마다 크게 다를 수 있다(γ가 거의 0이거나 σ²가 아주 작은 채널). 접고 나면 어떤 채널 weight는 크고 어떤 채널은 아주 작아서, **per-tensor** int8 양자화 하나로는 작은 채널이 0으로 뭉개진다. 그래서 conv weight는 보통 **per-channel** 양자화를 쓴다(C1). MobileNetV2 같은 모델에서 이 문제가 크게 드러난다는 보고가 있다(Nagel et al., 2019, "Data-Free Quantization" — cross-layer equalization).
- **QAT 중에는 folding을 흉내 낸다**: 양자화 인식 학습(C2)에서는 배포 때의 folded weight가 양자화된다는 걸 반영해야 해서, 학습 그래프 안에서 BN을 접은 weight에 fake-quant를 거는 방식을 쓴다.

---

## 8. Dropout 복습과 residual 예고

### 8.1 Dropout — 학습 때만 켜지는 랜덤 스위치

A4 5절에서 본 regularization 도구다. 학습 중 각 원소를 확률 p로 0으로 만들고, 남은 원소는 `1/(1−p)`배 키운다(PyTorch의 inverted dropout — 기대값을 유지하기 위해). eval 모드에선 **아무것도 하지 않는다**(항등 함수).

```
 train:  y_i = 0              (확률 p)
         y_i = x_i / (1 − p)  (확률 1 − p)          →  E[y_i] = x_i
 eval:   y_i = x_i
```

말로 하면: 학습 때 무작위로 뉴런을 꺼서 특정 뉴런 하나에 의존하지 못하게 한다. 추론 때는 항등이므로 배포 그래프에서 **삭제**된다(그림 1). 펌웨어 비유로는 fault injection 테스트 — 개발 중에만 켜고 양산 빌드에선 컴파일 아웃한다.

함정: `model.eval()`을 잊으면 추론 결과가 매번 달라진다(랜덤). 기기 출력과 PyTorch 출력을 비교하는데 PyTorch 쪽이 train 모드였다면, "기기가 틀렸다"가 아니라 기준이 흔들리는 것이다.

### 8.2 Residual connection 예고

깊은 네트워크에선 층의 출력에 **입력을 그대로 더하는** 지름길을 둔다.

```
 y = x + F(x)          F = (Linear/Conv → Norm → 활성화 → …) 블록
```

말로 하면: 블록은 "입력을 통째로 새로 만드는" 대신 "입력에 더할 수정분"만 배운다. backward에서 `∂y/∂x = 1 + ∂F/∂x`이므로 gradient가 최소 1의 통로로 흐르고, A3 8절의 vanishing gradient가 크게 줄어든다. ResNet·MobileNetV2(B2), Transformer(B4)의 핵심이다. Transformer에선 정규화 위치(residual 안쪽 앞: pre-norm, 더한 뒤: post-norm)가 학습 안정성에 중요하며 B4에서 다룬다.

배포 관점 한 줄: residual의 덧셈은 두 텐서가 **동시에 살아 있어야** 하므로 peak memory를 늘리고, int8에선 두 입력의 scale이 달라 덧셈 전에 rescale이 필요하다(D2, C1).

---

## 9. 비용 계산 — 파라미터, MAC, 메모리, 정규화 비용

### 9.1 MLP 하나를 끝까지 세어 보기

예를 들어 Hark 같은 웨어러블에서 IMU 6축을 50 Hz로 1초 모은 윈도우(300개 값)를 평탄화해 제스처 5종을 분류하는 MLP `300 → 64 → 32 → 5`를 생각하자(가정). 규칙은 간단하다.

```
 Linear(in → out):  params = in·out (weight) + out (bias),   MACs = in·out
 BatchNorm(C):      학습 파라미터 γ, β = 2C,  buffer μ, σ² = 2C   → folding 후 0
 활성화:            params 0, 연산은 원소 수만큼 (ReLU는 clamp에 흡수)
```

아래 코드는 표를 계산하고 PyTorch 파라미터 수와 맞는지, int8 배포 시 flash·SRAM이 얼마인지 계산한다.

```python
import torch.nn as nn
# IMU 제스처 MLP (가정): 6축 × 50샘플(1초@50Hz) 평탄화 = 300 → 64 → 32 → 5 class
dims = [300, 64, 32, 5]
layers = []
for a, b in zip(dims[:-1], dims[1:]):
    layers += [nn.Linear(a, b), nn.BatchNorm1d(b), nn.ReLU()]
net = nn.Sequential(*layers[:-2])               # 마지막 층 뒤엔 BN/ReLU 없음
print(net)
tot_p = tot_mac = 0
print(f"{'layer':>10} {'params':>7} {'MACs':>7}")
for a, b in zip(dims[:-1], dims[1:]):
    p, mac = a * b + b, a * b                  # weight + bias, 출력마다 a번 MAC
    tot_p += p; tot_mac += mac
    print(f"{a:>4}->{b:<4} {p:7d} {mac:7d}")
print(f"{'total':>10} {tot_p:7d} {tot_mac:7d}")
torch_p = sum(p.numel() for m in net if isinstance(m, nn.Linear) for p in m.parameters())
print("torch Linear params:", torch_p, "| BN params+buffers (folded away):",
      sum(4 * m.num_features for m in net if isinstance(m, nn.BatchNorm1d)))
w_bytes = sum(a * b for a, b in zip(dims[:-1], dims[1:]))       # int8 weight
b_bytes = 4 * sum(dims[1:])                                      # int32 bias
peak = max(a + b for a, b in zip(dims[:-1], dims[1:]))           # int8 in+out 동시 생존
print(f"int8 flash: weights {w_bytes} B + bias {b_bytes} B = {(w_bytes+b_bytes)/1024:.1f} KiB")
print(f"activation peak (int8, ping-pong in+out): {peak} B")
```

```text
Sequential(
  (0): Linear(in_features=300, out_features=64, bias=True)
  (1): BatchNorm1d(64, eps=1e-05, momentum=0.1, affine=True, track_running_stats=True)
  (2): ReLU()
  (3): Linear(in_features=64, out_features=32, bias=True)
  (4): BatchNorm1d(32, eps=1e-05, momentum=0.1, affine=True, track_running_stats=True)
  (5): ReLU()
  (6): Linear(in_features=32, out_features=5, bias=True)
)
     layer  params    MACs
 300->64     19264   19200
  64->32      2080    2048
  32->5        165     160
     total   21509   21408
torch Linear params: 21509 | BN params+buffers (folded away): 384
int8 flash: weights 21408 B + bias 404 B = 21.3 KiB
activation peak (int8, ping-pong in+out): 364 B
```

출력에서 볼 것:

| 층 | 파라미터 | MAC | int8 weight | int32 bias |
|---|---|---|---|---|
| 300→64 | 19,264 | 19,200 | 19,200 B | 256 B |
| 64→32 | 2,080 | 2,048 | 2,048 B | 128 B |
| 32→5 | 165 | 160 | 160 B | 20 B |
| 합계 | 21,509 | 21,408 | 21,408 B | 404 B |

- **MAC ≈ 파라미터 수**: dense layer는 weight 하나를 추론 한 번에 딱 한 번 쓴다. 그래서 MLP는 연산보다 **weight를 메모리에서 읽는 비용**이 지배한다(재사용이 없음 — conv와 대비, B2·D1). MCU라면 weight를 flash에서 직접 읽는(XIP) 속도가 병목이 될 수 있다.
- **첫 층이 90%**: 입력 차원이 크면 첫 층이 파라미터·MAC 대부분을 먹는다. 입력을 줄이거나(특징 추출, B7) conv로 바꾸는 게(B2) 가장 효과적인 다이어트다.
- **BN은 384개 숫자를 더하지만 folding 후 0**이다.
- **activation 메모리**: 추론에선 한 층의 입력과 출력만 동시에 살아 있으면 된다. int8로 `max(300+64, 64+32, 32+5) = 364 B`. weight 21 KB에 비하면 아주 작다. 학습 땐 backward용으로 모든 층의 activation을 저장해야 해서 훨씬 크다(A3 9절).

### 9.2 Cortex-M으로 감 잡기

21,408 MAC을 int8 SIMD(예를 들어 Cortex-M4/M7의 `SMLAD` — 한 명령에 16비트 곱셈 2개 누산) 기준 사이클당 2 MAC으로 잡으면 약 1만 사이클 + 오버헤드다. 64 MHz MCU라면 1 ms 미만. 이 숫자는 이상적 상한에 가까우며 실제는 메모리 대기, 루프 오버헤드, requantization 때문에 몇 배 늘어난다 — 반드시 기기에서 재야 한다(D, K 모듈).

### 9.3 정규화 층의 추론 비용 — BN vs LayerNorm

| 항목 | BatchNorm (folding 후) | LayerNorm | RMSNorm |
|---|---|---|---|
| 추론 시 연산 | 0 (weight에 흡수) | D개당: 평균·분산 reduction + 1/√ 1회 + 정규화·γ·β | D개당: 제곱합 reduction + 1/√ 1회 + 스케일·γ |
| 입력 의존 통계 | 없음 (상수) | 있음 (매 토큰) | 있음 (매 토큰) |
| 데이터 흐름 | 스트리밍 가능 (원소별) | reduction 끝날 때까지 대기 | reduction 끝날 때까지 대기 |
| int8 구현 | 자연스러움 | 어려움: 분산 범위가 넓고 1/√ 필요 | 조금 쉬움. 그래도 1/√ 필요 |
| NPU 지원 | conv에 fused | 벤더별로 다름. fallback 흔함 | 최근 NPU/LLM 런타임은 지원 확대 |

말로 하면: 연산량(FLOP) 자체는 LayerNorm도 D의 몇 배 정도로 dense layer의 D×D′ MAC에 비하면 작다. 문제는 **모양**이다.

- **reduction = 동기화 지점**: 출력 원소 하나를 만들려면 D개 원소를 다 봐야 한다. NPU의 MAC 배열은 "타일 단위로 흘려보내는" 스트리밍 구조인데, reduction은 파이프라인을 멈추고 전체를 모은 뒤 다시 한 번 훑게 만든다. 텐서를 두세 번 읽는 memory-bound op가 된다.
- **비선형 1/√**: MAC 유닛으로는 못 하고 특별 유닛·LUT·Newton 반복이 필요하다.
- **양자화가 까다로움**: 입력 분산이 토큰마다 크게 달라서 int8로 평균·분산을 정확히 내기 어렵다. 그래서 LayerNorm은 fp16/int16으로 남겨 두는 **혼합 정밀도** 배포가 흔하다.
- **BN은 이런 문제가 전혀 없다**: folding 후 상수로 사라지므로 NPU에 이상적이다. 그래서 edge용 CNN(MobileNet, DS-CNN, B5 KWS)은 BN을 쓰고, Transformer를 edge에 올릴 땐 LayerNorm/RMSNorm이 병목·fallback 후보로 늘 거론된다.

---

## 10. 임베디드 관점에서 다시 보기

**fused op가 기본 단위다.** 기기에서 "레이어 하나"는 대개 `conv/FC + bias + (folded BN) + activation`이다. 모델 그래프를 볼 때 op 수가 아니라 **fused 커널 수**와 **fusion이 깨지는 지점**(지원 안 되는 활성화, 활성화 뒤 BN, 중간에 낀 reshape)을 찾는 습관을 들이자. fusion이 깨지면 중간 텐서가 메모리를 왕복하고, NPU라면 CPU fallback으로 이어질 수 있다.

**활성화 선택 = 양자화 범위 설계.** 모델 팀과 이렇게 대화할 수 있어야 한다.

- "이 모델을 int8 NPU에 올릴 거면 ReLU6나 ReLU로 학습해 주세요. SiLU/GELU는 LUT로 되긴 하지만 우리 NPU 지원 목록을 먼저 확인할게요."
- "hard-swish는 정수 연산으로 정확히 되니까 MobileNetV3 스타일이면 괜찮습니다."
- "sigmoid 출력은 [0, 1]이라 출력 scale을 1/256로 고정할 수 있어서 좋아요. 대신 입력 범위가 ±8을 넘으면 LUT 양 끝에서 포화합니다."

**LUT는 펌웨어 엔지니어의 홈그라운드다.** 256 B 표, 인덱스는 `(uint8_t)q`, 표는 빌드 타임 생성(Python 스크립트 → `const int8_t lut[256]` 헤더, A5 8절 방식)으로 flash에 둔다. 캐시 없는 MCU에서 flash 대기 상태가 걱정되면 SRAM에 복사한다. int16 activation이면 표를 줄이고 선형 보간.

**BN folding은 기기 검증의 기준점을 바꾼다.** PyTorch의 folded 모델(eval 모드)을 "골든 레퍼런스"로 삼아 C/NPU 출력과 비교하라. 원래 모델과 folded 모델은 float 오차 1e-6 수준으로 같아야 한다(7.3절). 여기서 차이가 크면 folding 버그(train 통계 사용, ε 누락, 채널 축 착각)다.

**정규화 op는 실행 시간 프로파일에서 따로 보인다.** Transformer 계열을 edge에 올리면 MAC 대부분은 matmul이지만, 프로파일에서 LayerNorm/softmax 같은 reduction op가 의외로 큰 비율(특히 NPU에서 fallback될 때)을 차지하는 일이 흔하다. "MAC 수만 보고 지연을 예측하면 틀리는" 대표적인 경우다(D 모듈).

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 활성화 없이 Linear만 여러 개 쌓음 | 층을 늘려도 정확도가 선형 모델과 같음 | 선형 합성은 선형 (1.3절) | 층 사이에 ReLU/GELU 등 추가 |
| export 전에 `model.eval()` 안 함 | 기기 출력이 PyTorch와 딴판, batch=1에서 에러 | BN이 배치 통계 사용, Dropout 활성 | 추론·export 전 반드시 eval, 검증 스크립트에 assert |
| BN folding에 배치 통계나 ε 누락 | folded 모델 출력이 원본과 1e-2 이상 차이 | running 통계 대신 현재 배치 사용, √(σ²) vs √(σ²+ε) | eval 통계와 ε 사용, max diff로 회귀 테스트 |
| 학습은 ReLU, 배포에서 ReLU6로 교체 | 정확도 하락 | 6 이상을 쓰던 뉴런이 잘림 | 처음부터 ReLU6로 학습하거나 교체 후 fine-tune |
| 지원 안 되는 활성화(GELU 변형 등) 사용 | NPU에서 해당 op만 CPU fallback, 지연 급증 | 타깃 NPU의 op 지원 목록 미확인 | 지원 op로 교체(hard-swish, ReLU6) 또는 LUT 커스텀 op |
| BN 뒤가 아니라 활성화 뒤에 BN 배치 | folding 안 됨, BN이 별도 op로 남음 | conv → ReLU → BN 순서 | conv → BN → ReLU 순서로 설계 |
| folded weight를 per-tensor int8 양자화 | 일부 채널 출력이 0, 정확도 급락 | 채널별 스케일 s가 달라 weight 범위가 벌어짐 | per-channel 양자화 (C1), cross-layer equalization |
| batch 2~4로 BN 학습 | loss가 요동, eval 정확도 불안정 | 배치 통계가 잡음 | batch 키우기, GroupNorm/LayerNorm, BN freeze |
| LUT를 scale 무시하고 한 번만 생성 | 특정 레이어 출력이 틀림 | LUT는 입력·출력 scale/zero_point마다 다름 | op마다 해당 양자화 파라미터로 LUT 생성 |
| sigmoid 두 번 적용 (sigmoid + BCEWithLogitsLoss) | 학습이 느리고 확률이 0.5~0.73에 몰림 | 손실 함수가 내부에서 sigmoid를 또 적용 | logits에 BCEWithLogitsLoss 하나만 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "Why do we need activation functions?"

**A.** 활성화가 없으면 선형 층을 아무리 쌓아도 W2·W1 한 행렬로 합쳐져서 선형 모델 하나와 같다. 비선형이 있어야 꺾임이 생기고, 꺾임을 충분히 모으면 임의의 연속 함수를 근사할 수 있다(universal approximation). 실무에선 hidden에 ReLU 계열, Transformer에 GELU/SiLU, 출력·gate에 sigmoid/tanh를 쓴다.

> Without a nonlinearity, a stack of linear layers collapses into a single linear map — W2·W1 is just another matrix — so depth buys nothing. The nonlinearity creates kinks, and enough kinks let even a one-hidden-layer MLP approximate any continuous function. In practice I use ReLU-family activations in CNNs, GELU or SiLU in transformers, and sigmoid or tanh only at outputs and gates.

**Q.** "How does BatchNorm behave differently at training and inference time?"

**A.** 학습 때는 현재 배치의 채널별 평균·분산으로 정규화하고, 동시에 running mean/var를 momentum 0.1의 지수이동평균으로 갱신한다. 추론 때는 배치 통계 대신 저장된 running 통계를 쓰므로 BN은 채널별 상수 affine 변환이 된다. 그래서 batch=1로도 결정적으로 동작하고, 앞 conv에 접어 넣을 수 있다. eval 모드를 잊는 게 가장 흔한 배포 버그다.

> During training, BatchNorm normalizes each channel with the current batch's mean and variance and updates running estimates as an exponential moving average. At inference it uses the frozen running statistics, so it becomes a fixed per-channel scale and shift — deterministic for batch size one and foldable into the previous layer. Forgetting model.eval() before export is the classic bug.

**Q.** "How do you fold BatchNorm into a convolution?"

**A.** eval 모드 BN은 출력 채널마다 s = γ/√(σ²+ε)를 곱하고 β − s·μ를 더하는 affine이다. 그래서 conv weight의 출력 채널 o 전체에 s[o]를 곱하고, bias는 b′ = s·(b − μ) + β로 바꾸면 된다. 원본과 folded 모델의 출력을 max abs diff로 비교해서 1e-6 수준이면 맞다. op가 하나 줄고 중간 텐서와 추가 양자화 단계가 없어져서 모든 배포 툴체인이 이걸 한다. 단, 채널별 스케일이 weight 범위를 벌리므로 per-channel 양자화가 필요하다.

> At inference BN is a per-channel affine: scale s = gamma over sqrt(var + eps), then shift. I multiply each output channel's conv weights by s and set the new bias to s times (b minus the running mean) plus beta. I verify by comparing outputs — the max difference should be at float rounding level. It removes an op, an intermediate tensor and a requantization step, which is why every deployment toolchain does it; the catch is that per-channel scales widen the weight range, so per-channel weight quantization is important.

**Q.** "Why do mobile and edge models use ReLU6 or hard-swish?"

**A.** ReLU6는 출력 범위가 [0, 6]으로 고정돼서 int8 scale을 outlier와 무관하게 정할 수 있고, int8 커널에선 requantize 후 clamp 범위만 바꾸면 되니 비용이 0이다. hard-swish는 swish(x·sigmoid)의 정확도를 거의 유지하면서 exp 없이 덧셈·clamp·곱셈만 쓰는 구간별 선형 근사라 MCU/NPU에서 싸고 정수로 정확히 계산된다. 둘 다 MobileNet 계열이 도입했다.

> ReLU6 bounds activations to [0, 6], so the quantization scale is fixed and a rare outlier can't blow up the step size; in an int8 kernel it's free — just the clamp bounds after requantization. Hard-swish approximates swish with a piecewise-linear gate, so it keeps most of the accuracy benefit without any exponential, and it computes exactly in integer arithmetic. Both came from the MobileNet line for exactly these reasons.

**Q.** "LayerNorm vs RMSNorm — what's the difference and why do LLMs use RMSNorm?"

**A.** LayerNorm은 토큰 벡터에서 평균을 빼고 표준편차로 나눈 뒤 γ, β를 적용한다. RMSNorm은 평균을 빼지 않고 RMS로만 나누며 β가 없다. reduction 하나와 파라미터 일부가 빠져 더 싸고, 실험적으로 품질이 거의 같아서 Llama 이후 LLM 대부분이 쓴다. 둘 다 BN과 달리 배치에 의존하지 않고 train/eval 차이가 없지만, 추론 시 매 토큰마다 reduction과 역제곱근을 계산해야 해서 BN처럼 접어 없앨 수는 없다.

> LayerNorm subtracts the mean and divides by the standard deviation over each token's features, then applies gain and bias. RMSNorm skips the mean subtraction and the bias and just divides by the root-mean-square. It's cheaper — one fewer reduction — with about the same quality, which is why Llama-style models use it. Unlike BatchNorm, both are batch-independent, but both need a runtime reduction and a reciprocal square root per token, so they can't be folded away.

**Q.** "Why is LayerNorm harder to run on an NPU than BatchNorm?"

**A.** BN은 folding 후 사라지지만 LayerNorm은 입력 의존 통계라 매번 계산해야 한다. 출력 하나를 내려면 벡터 전체를 먼저 reduction해야 해서 스트리밍 파이프라인이 멈추고 텐서를 여러 번 읽는 memory-bound op가 되며, 1/√ 같은 비선형과 넓은 분산 범위 때문에 int8 구현이 어렵다. 그래서 fp16/int16로 남기거나 CPU fallback되는 경우가 많고, 프로파일에서 MAC 비중보다 큰 시간을 먹는다.

> BatchNorm disappears after folding, but LayerNorm depends on the input, so it runs every time. Each output needs a full reduction over the vector first, which stalls a streaming MAC pipeline and turns it into a multi-pass, memory-bound op; the reciprocal square root and the wide variance range also make int8 awkward. So it's often kept in fp16 or int16 or falls back to the CPU, and it takes more latency than its FLOP count suggests.

---

## 13. 직접 해보기

1. **손계산 — 층 합치기.** `W1 = [[1, 0], [2, 1]]`, `b1 = [1, 0]`, `W2 = [[1, −1]]`, `b2 = [2]`인 선형 2층을 한 층 `W, b`로 합쳐라. 정답: `W = W2·W1 = [[−1, −1]]`, `b = W2·b1 + b2 = [1 − 0 + 2] = [3]`.
2. **손계산 — BN folding.** Linear `z = 3x − 2`, BN(eval): μ = 4, σ² = 9, γ = 1.5, β = −1, ε = 0. W′, b′를 구하고 x = 2에서 검산하라. 정답: s = 1.5/3 = 0.5, W′ = 1.5, b′ = 0.5·(−2 − 4) − 1 = −4 → x=2: 1.5·2 − 4 = −1 (원래: z = 4 → 1.5·0/3 − 1 = −1).
3. **손계산 — LN vs RMSNorm.** `v = [1, −1, 3, −3]`에 대해 둘을 계산하라(γ=1, β=0, ε=0). 정답: 평균 0이라 둘이 같다. σ² = rms² = (1+1+9+9)/4 = 5 → `[0.447, −0.447, 1.342, −1.342]`. 평균이 0이면 LN = RMSNorm이다.
4. **코드 — int8 hard-swish를 LUT로.** 4.3절 C 코드에 `lut_hswish[256]`을 추가해 `hswish_q`와 256개 입력 전부에서 결과를 비교하라. 몇 개가 다른가? 정답: LUT를 `lroundf(16·x·ReLU6(x+3)/6)`로 만들면 0개다(직접 확인함) — 정수 식의 `±48` 후 나누기가 '0에서 먼 쪽으로 반올림'이라 `lroundf`와 같다. 반올림 없이 `p/96`로 자르면 차이가 생긴다.
5. **코드 — Linear + BN1d folding.** 7.3절 코드를 `nn.Linear(300, 64)` + `nn.BatchNorm1d(64)`로 바꿔 fold 함수를 다시 쓰고 max diff를 확인하라. 힌트: linear weight shape은 `[out, in]`이라 `s.view(-1, 1)`로 행마다 곱한다.
6. **코드 — ReLU vs ReLU6 학습.** 2.2절의 1D 곡선을 `sin(2x)+0.3x` 대신 값이 0~10인 함수(예: `5 + 5·sin(x)`)로 바꾸고, ReLU6 hidden으로 학습하면 무엇이 달라지는지 관찰하라. 힌트: hidden 출력은 6에서 잘려도 출력층 weight로 다시 키울 수 있으므로 여전히 맞출 수 있다 — ReLU6가 제한하는 것은 hidden 값 범위지 모델 출력 범위가 아니다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| dense / fully-connected layer | 완전 연결 층 | `y = W·x + b`. 모든 입력이 모든 출력에 연결 |
| MLP | 다층 퍼셉트론 | dense와 활성화를 번갈아 쌓은 네트워크 |
| hidden unit | 은닉 뉴런 | 중간 층의 출력 하나. ReLU라면 꺾임 하나 |
| activation function | 활성화 함수 | 원소별 비선형 함수. 층 합성이 선형으로 무너지는 것을 막는다 |
| universal approximation | 범용 근사 | 은닉층 하나로도 충분히 넓으면 연속 함수를 근사 가능 |
| dead ReLU | 죽은 뉴런 | 입력이 항상 음수라 출력·gradient가 영원히 0 |
| ReLU6 | 6에서 자른 ReLU | 출력 [0, 6] 고정 → 양자화 scale 고정 |
| hard-swish | 구간별 선형 swish | x·ReLU6(x+3)/6. exp 없이 정수 연산 가능 |
| GELU / SiLU | 매끄러운 ReLU | x에 부드러운 스위치(Φ 또는 σ)를 곱함. Transformer·LLM에서 표준 |
| LUT | 조회 표 | int8 입력 256가지의 출력을 미리 계산한 표 |
| normalization | 정규화 | 중간 값을 평균 0·분산 1로 맞추고 γ, β로 재조정 |
| BatchNorm | 배치 정규화 | 채널별로 배치 전체에서 통계. train/eval 다름 |
| running mean / var | 이동 평균 통계 | 학습 중 배치 통계의 EMA. 추론 때 사용 |
| momentum (BN) | EMA 계수 | PyTorch 0.1 = 새 값 비중 10% |
| LayerNorm | 층 정규화 | 샘플(토큰)마다 특징 축에서 통계. 배치 무관 |
| RMSNorm | RMS 정규화 | 평균 빼기 없이 RMS로만 나눔. LLM 표준 |
| GroupNorm | 그룹 정규화 | 샘플마다 채널 그룹별 통계. 작은 batch용 |
| BN folding | BN 접기 | eval BN을 앞 conv/linear의 W′, b′에 흡수 |
| op fusion | 연산 융합 | 여러 op를 커널 하나로 합쳐 중간 텐서 제거 |
| epilogue | 루프 끝 처리 | MAC 누산 뒤 bias·requant·활성화를 적용하는 부분 |
| inverted dropout | 역 드롭아웃 | 학습 때 1/(1−p)로 키워 추론 때 아무것도 안 해도 되게 함 |
| residual connection | 잔차 연결 | y = x + F(x). gradient 통로 확보 (B2, B4) |
| reduction | 축소 연산 | 여러 원소를 합·평균 등 하나로 모으는 연산. 동기화 지점 |
| fallback | 대체 실행 | NPU가 지원 못 하는 op를 CPU가 대신 실행 |

---

## 15. 요약 & 체크리스트

MLP는 dense layer `y = W·x + b`와 활성화 함수를 번갈아 쌓은 것이다. 활성화가 없으면 층들이 한 행렬로 합쳐져서 깊이가 의미 없고, ReLU 같은 비선형이 꺾임을 만들어야 복잡한 함수를 근사할 수 있다. 활성화는 용도에 따라 고른다: hidden에는 ReLU 계열(edge에선 양자화 범위가 고정되는 ReLU6), Transformer에는 GELU/SiLU, edge CNN에는 exp 없는 hard-swish, 출력·gate에는 sigmoid/tanh. int8에선 ReLU류는 clamp에 흡수되어 공짜이고, 나머지 원소별 함수는 256-entry LUT 한 번으로 계산된다. 정규화는 중간 값의 스케일을 맞춰 학습을 안정시키며, 어느 축으로 통계를 내느냐가 종류를 가른다: BatchNorm은 채널별·배치 전체(train은 배치 통계, eval은 running 통계), LayerNorm/RMSNorm은 토큰별로 배치와 무관하다. eval 모드 BN은 상수 affine이라 앞 conv/linear에 `W′ = s·W`, `b′ = s·(b − μ) + β`로 접혀 사라지고, 이것이 conv+BN+ReLU fusion의 핵심이다. 반면 LayerNorm/RMSNorm은 추론 때도 reduction과 1/√를 계산해야 해서 NPU에서 부담이 된다.

- [ ] dense layer 출력을 손으로 계산하고, 층의 파라미터·MAC 수를 `in·out + out`, `in·out`으로 셀 수 있다
- [ ] 선형 2층이 한 층으로 합쳐지는 식 `W = W2·W1`, `b = W2·b1 + b2`를 쓰고 설명할 수 있다
- [ ] ReLU hidden unit = 꺾임 하나라는 universal approximation 직관을 말할 수 있다
- [ ] sigmoid, tanh, ReLU, Leaky, ReLU6, GELU, SiLU, hard-sigmoid, hard-swish의 식·범위·비용을 비교할 수 있다
- [ ] ReLU6와 hard-swish가 edge에서 선호되는 이유를 양자화 범위와 연산 비용으로 설명할 수 있다
- [ ] int8 sigmoid/tanh LUT를 C로 만들고 입력·출력 scale을 반영할 수 있다
- [ ] BN의 train/eval 차이와 running 통계 갱신식(momentum, unbiased var)을 손으로 계산할 수 있다
- [ ] BatchNorm, LayerNorm, RMSNorm, GroupNorm이 어느 축으로 평균을 내는지 그릴 수 있다
- [ ] BN folding 공식을 유도하고 코드로 max diff를 확인할 수 있다
- [ ] LayerNorm이 NPU에서 BN보다 불리한 이유(runtime reduction, 1/√, int8 범위)를 말할 수 있다

---

## 참고 자료

- Goodfellow, Bengio, Courville, "Deep Learning" (MIT Press, 2016) — 6장 (feedforward networks, universal approximation), 8.7.1절 (batch normalization). https://www.deeplearningbook.org/
- Dive into Deep Learning (d2l.ai) — 5장 Multilayer Perceptrons, 8.5절 Batch Normalization. https://d2l.ai/
- Ioffe & Szegedy, "Batch Normalization: Accelerating Deep Network Training by Reducing Internal Covariate Shift" (2015). https://arxiv.org/abs/1502.03167
- Ba, Kiros, Hinton, "Layer Normalization" (2016). https://arxiv.org/abs/1607.06450
- Zhang & Sennrich, "Root Mean Square Layer Normalization" (2019). https://arxiv.org/abs/1910.07467
- Wu & He, "Group Normalization" (2018). https://arxiv.org/abs/1803.08494
- Santurkar et al., "How Does Batch Normalization Help Optimization?" (2018). https://arxiv.org/abs/1805.11604
- Hendrycks & Gimpel, "Gaussian Error Linear Units (GELUs)" (2016). https://arxiv.org/abs/1606.08415
- Ramachandran, Zoph, Le, "Searching for Activation Functions" (2017) — Swish. https://arxiv.org/abs/1710.05941
- Howard et al., "MobileNets" (2017) — ReLU6. https://arxiv.org/abs/1704.04861
- Sandler et al., "MobileNetV2: Inverted Residuals and Linear Bottlenecks" (2018) — ReLU6와 저정밀도. https://arxiv.org/abs/1801.04381
- Howard et al., "Searching for MobileNetV3" (2019) — hard-swish. https://arxiv.org/abs/1905.02244
- Nagel et al., "Data-Free Quantization Through Weight Equalization and Bias Correction" (2019). https://arxiv.org/abs/1906.04721
- PyTorch 문서: `torch.nn.BatchNorm1d`, `torch.nn.LayerNorm`, `torch.nn.RMSNorm`, `torch.nn.GroupNorm`, `torch.nn.Hardswish`. https://pytorch.org/docs/stable/nn.html
- Arm CMSIS-NN (GitHub) — Cortex-M용 int8/int16 NN 커널. https://github.com/ARM-software/CMSIS-NN
- MIT 6.5940 TinyML and Efficient Deep Learning (Song Han) — 양자화·fusion 강의. https://efficientml.ai/
