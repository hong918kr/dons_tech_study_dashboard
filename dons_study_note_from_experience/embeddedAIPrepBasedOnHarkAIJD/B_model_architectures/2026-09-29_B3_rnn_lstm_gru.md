# B3. RNN · LSTM · GRU — 상태를 가진 모델과 스트리밍 추론

> **이 노트를 다 읽으면**: vanilla RNN·LSTM·GRU의 식을 쓰고 3 step을 손으로 계산할 수 있다 · vanishing/exploding gradient가 "같은 행렬을 계속 곱하기 때문"임을 숫자로 보일 수 있다 · LSTM/GRU 파라미터 수를 암산하고 torch와 맞출 수 있다 · RNN을 프레임 단위 stateful 추론으로 돌리고 C로 GRU cell을 구현할 수 있다 · RNN이 NPU에서 불리하고 MCU에선 유리한 이유를 설명할 수 있다
> **JD 연결**: "(우대) CNN, **RNN**, transformers, KV-cache, memory bandwidth", "Integrate ML inference into embedded firmware in C", "Hands-on with IMUs … microphones" — study_prep_list B3 행: hidden state, gate 구조 / **streaming/stateful inference** (프레임 단위 처리) / RNN이 NPU에서 불리한 이유(순차 의존성)
> **Don 기준 난이도**: 상태 머신, IIR 필터, 링버퍼, 프레임 인터럽트 처리는 이미 몸에 익은 것 / "상태 전이를 데이터로 학습한다"는 발상, gate의 의미, BPTT, 순환 구조의 양자화 함정은 새로 배움
> **선행 노트**: A1, A3, A5, B1

---

## 0. 큰 그림 — 이게 왜 필요한가

웨어러블의 센서는 **끝나지 않는 스트림**을 낸다. 마이크는 16 kHz로, IMU는 50~200 Hz로 샘플을 계속 밀어 넣는다. 지금까지 본 모델(A0의 선형 모델, B1의 MLP)은 "고정 길이 창 하나 → 답 하나"였다. 창을 만들려면 1초치 샘플을 버퍼에 모았다가 한꺼번에 계산해야 하고, 창 밖의 과거는 모델이 모른다.

펌웨어 엔지니어는 이 문제를 이미 다른 방식으로 푼다. **상태(state)를 들고 다니는 것**이다.

- 버튼 디바운스 FSM: 현재 상태 + 이번 입력 → 다음 상태.
- IIR low-pass 필터: `y[n] = a·y[n−1] + (1−a)·x[n]`. 과거 전체가 `y[n−1]` 하나에 요약돼 있다.
- 걸음 수 카운터: 누적 변수 하나가 과거 수천 샘플을 대신한다.

**RNN(recurrent neural network, 순환 신경망)** 은 바로 이 구조다. 다만 상태 전이 함수를 사람이 설계하지 않고 **데이터로 학습**한다.

```
  사람이 설계:   state ← f_설계(state, input)     (FSM 표, IIR 계수)
  RNN:           h     ← f_W(h, x)                (W는 학습으로 결정)

   IMU/마이크 ──► [프레임 x_t] ──► ┌──────────────┐ ──► 출력 y_t (매 프레임)
                                   │  h ← f_W(h,x) │
                                   └──────┬───────┘
                                          │ h: 작은 SRAM 상태 (예: 64 floats = 256 B)
                                          └── 다음 프레임 인터럽트까지 보관
```

예를 들어 Hark 같은 웨어러블이라면(추정), always-on MCU에서 10 ms마다 들어오는 오디오 특징 한 프레임을 받아 작은 GRU를 한 step 돌리고, 상태 `h`만 SRAM에 남겨 두는 식으로 VAD(voice activity detection)나 wake word 1단을 돌릴 수 있다. 창 전체를 버퍼링할 필요가 없고, 프레임당 연산이 일정하다.

그런데 같은 RNN이 Qualcomm NPU 같은 가속기에서는 오히려 **불리**하다. 시간 방향으로 병렬화가 안 되기 때문이다. 이 노트는 그 양면을 모두 다룬다.

순서: 스트림과 상태(1절) → vanilla RNN(2절) → BPTT와 gradient 소실(3절) → LSTM(4절) → GRU(5절) → 파라미터 수(6절) → 쌓기·양방향·출력 방식(7절) → 스트리밍 추론(8절) → C 구현(9절) → 작은 end-to-end 학습(10절) → 가속기에서의 RNN(11절) → 오늘날의 쓰임새(12절) → 임베디드 관점(13절).

---

## 1. 스트림과 상태 — IIR 필터는 이미 "선형 RNN"이다

### 1.1 IIR 필터를 RNN 식으로 다시 쓰기

1차 IIR low-pass를 벡터로 일반화해 보자.

```
 1차 IIR (스칼라):    y_t = a · y_{t−1} + b · x_t
 벡터 상태 공간:       h_t = A · h_{t−1} + B · x_t          (선형 시스템, 제어 수업의 x[k+1] = A x[k] + B u[k])
 vanilla RNN:         h_t = tanh( W_h · h_{t−1} + W_x · x_t + b )
```

말로 하면: RNN은 **상태 공간 모델(state-space model)에 비선형 함수 tanh를 씌운 것**이다. `A` 대신 `W_h`, `B` 대신 `W_x`라고 부르고, 이 행렬들을 설계하지 않고 학습한다. tanh가 들어가서 (1) 상태가 −1~1로 묶이고(포화), (2) FSM처럼 "조건에 따라 다르게 반응하는" 비선형 동작이 가능해진다.

| 개념 | 펌웨어/DSP 쪽 이름 | RNN 쪽 이름 |
|---|---|---|
| 과거의 요약 | FSM state, IIR delay line `y[n−1]` | hidden state `h_t` |
| 상태 갱신 규칙 | 상태 전이표, 필터 계수 | `W_h`, `W_x`, `b` (학습됨) |
| 한 샘플 처리 | 인터럽트 핸들러 1회 | time step 1회 |
| 초기 상태 | reset 값 | `h_0` (보통 0) |
| 안정성 | pole이 단위원 안 | `W_h`의 크기(특이값)와 tanh 포화 |

### 1.2 왜 "창 + MLP"로는 부족한가

창 기반 모델은 창 길이 L 밖의 과거를 볼 수 없다. "탭을 할 때마다 ON/OFF가 토글된다" 같은 규칙은 탭이 얼마나 오래전에 있었든 기억해야 하므로, 창이 아무리 길어도 창 밖으로 밀려난 탭은 잊는다. 10절에서 실제로 창 기반 CNN이 이 문제에서 동전 던지기(약 52%)에 머무는 것을 보게 된다.

반대로 창 기반 모델(1D CNN, B2)은 과거를 L개로 제한하는 대신 **병렬 계산이 쉽다**. 이 트레이드오프가 11절의 주제다.

---

## 2. Vanilla RNN — 학습된 상태 전이

### 2.1 정의

```
 h_t = tanh( W_x · x_t + W_h · h_{t−1} + b )        h_0 = 0
 y_t = W_y · h_t + b_y                               (필요하면 출력층)

 shape:  x_t ∈ ℝ^I,  h_t ∈ ℝ^H,  W_x ∈ ℝ^{H×I},  W_h ∈ ℝ^{H×H},  b ∈ ℝ^H
```

말로 하면: 매 step마다 "이번 입력을 섞은 값(`W_x·x_t`)"과 "지난 상태를 섞은 값(`W_h·h_{t−1}`)"을 더하고 tanh로 눌러서 새 상태를 만든다. **모든 step이 같은 W를 쓴다** — 이것을 weight sharing이라고 하고, 그래서 시퀀스 길이와 무관하게 파라미터 수가 고정이다. 펌웨어로 치면 같은 인터럽트 핸들러가 매 샘플마다 호출되는 것이다.

### 2.2 펼친(unrolled) 그림

```svg
<svg viewBox="0 0 660 260" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="b3u-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="30" y="100" width="90" height="50" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="75" y="130" font-size="13" text-anchor="middle">RNN cell</text>
<line x1="75" y1="210" x2="75" y2="153" stroke="currentColor" marker-end="url(#b3u-a)"/><text x="75" y="228" font-size="13" text-anchor="middle">x_t</text>
<line x1="75" y1="100" x2="75" y2="52" stroke="currentColor" marker-end="url(#b3u-a)"/><text x="75" y="42" font-size="13" text-anchor="middle">h_t</text>
<path d="M120,120 C165,120 165,80 105,80 L105,97" fill="none" stroke="#e08a3c" stroke-width="2" marker-end="url(#b3u-a)"/><text x="160" y="74" font-size="12" text-anchor="middle">z⁻¹ (1칸 지연)</text>
<text x="190" y="130" font-size="14" text-anchor="middle">≡</text>
<text x="224" y="115" font-size="12" text-anchor="middle">h₀=0</text><line x1="206" y1="125" x2="248" y2="125" stroke="#e08a3c" stroke-width="2" marker-end="url(#b3u-a)"/>
<rect x="250" y="100" width="90" height="50" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="295" y="130" font-size="13" text-anchor="middle">cell t=1</text>
<rect x="390" y="100" width="90" height="50" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="435" y="130" font-size="13" text-anchor="middle">cell t=2</text>
<rect x="530" y="100" width="90" height="50" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="575" y="130" font-size="13" text-anchor="middle">cell t=3</text>
<line x1="340" y1="125" x2="388" y2="125" stroke="#e08a3c" stroke-width="2" marker-end="url(#b3u-a)"/><text x="364" y="116" font-size="12" text-anchor="middle">h₁</text>
<line x1="480" y1="125" x2="528" y2="125" stroke="#e08a3c" stroke-width="2" marker-end="url(#b3u-a)"/><text x="504" y="116" font-size="12" text-anchor="middle">h₂</text>
<line x1="620" y1="125" x2="655" y2="125" stroke="#e08a3c" stroke-width="2" marker-end="url(#b3u-a)"/><text x="640" y="116" font-size="12" text-anchor="middle">h₃</text>
<line x1="295" y1="210" x2="295" y2="153" stroke="currentColor" marker-end="url(#b3u-a)"/><text x="295" y="228" font-size="13" text-anchor="middle">x₁</text>
<line x1="435" y1="210" x2="435" y2="153" stroke="currentColor" marker-end="url(#b3u-a)"/><text x="435" y="228" font-size="13" text-anchor="middle">x₂</text>
<line x1="575" y1="210" x2="575" y2="153" stroke="currentColor" marker-end="url(#b3u-a)"/><text x="575" y="228" font-size="13" text-anchor="middle">x₃</text>
<line x1="295" y1="100" x2="295" y2="52" stroke="currentColor" marker-end="url(#b3u-a)"/><text x="295" y="42" font-size="12" text-anchor="middle">h₁ → 출력층</text>
<line x1="435" y1="100" x2="435" y2="52" stroke="currentColor" marker-end="url(#b3u-a)"/><text x="435" y="42" font-size="12" text-anchor="middle">h₂ → 출력층</text>
<line x1="575" y1="100" x2="575" y2="52" stroke="currentColor" marker-end="url(#b3u-a)"/><text x="575" y="42" font-size="12" text-anchor="middle">h₃ → 출력층</text>
<text x="435" y="252" font-size="12" text-anchor="middle">세 칸 모두 같은 W_x · W_h · b (weight sharing)</text>
</svg>
```

그림 1 — 왼쪽은 "접힌" RNN: 출력 h_t가 1칸 지연(z⁻¹)을 거쳐 다음 step 입력으로 돌아간다. 오른쪽은 같은 것을 시간 축으로 펼친 것. 펼친 그림은 "깊이가 T인 네트워크"처럼 보이지만 모든 층이 같은 weight를 쓴다. 주황 화살표(상태 경로)가 끊기지 않는 한 줄이라는 점이 11절에서 중요해진다.

### 2.3 손계산 — 3 step

작게 잡는다: 입력 1차원(I=1), 상태 2차원(H=2).

```
 W_x = [ 0.5 ]     W_h = [ 0.8  −0.2 ]     b = [ 0.0 ]      입력 스트림 x = 1, 0, −1
       [−0.3 ]           [ 0.1   0.6 ]         [ 0.1 ]      h₀ = [0, 0]

 t=1:  a₁ = W_x·1 + W_h·h₀ + b = [0.5, −0.3] + [0, 0] + [0, 0.1] = [0.5, −0.2]
       h₁ = tanh(a₁) = [0.4621, −0.1974]

 t=2:  W_h·h₁ = [0.8·0.4621 + (−0.2)(−0.1974),  0.1·0.4621 + 0.6·(−0.1974)]
              = [0.3697 + 0.0395,  0.0462 − 0.1184] = [0.4092, −0.0722]
       a₂ = [0, 0] + [0.4092, −0.0722] + [0, 0.1] = [0.4092, 0.0278]
       h₂ = tanh(a₂) = [0.3878, 0.0278]

 t=3:  W_h·h₂ = [0.8·0.3878 − 0.2·0.0278,  0.1·0.3878 + 0.6·0.0278] = [0.3047, 0.0555]
       a₃ = [−0.5, 0.3] + [0.3047, 0.0555] + [0, 0.1] = [−0.1953, 0.4555]
       h₃ = tanh(a₃) = [−0.1929, 0.4264]
```

말로 하면: t=2에는 입력이 0인데도 상태가 0으로 돌아가지 않는다. t=1의 입력이 `W_h`를 통해 "메아리"처럼 남아 있기 때문이다 — IIR 필터의 impulse response가 한 샘플 뒤에도 0이 아닌 것과 같다.

### 2.4 코드로 확인 — numpy와 `nn.RNN`

손계산과 같은 weight를 numpy와 PyTorch `nn.RNN`에 넣고 결과가 같은지 확인한다.

```python
# vanilla RNN 3 step: 손계산 → numpy → nn.RNN (같은 weight)
import numpy as np, torch

W_x = np.array([[0.5], [-0.3]])          # [H=2, I=1]
W_h = np.array([[0.8, -0.2], [0.1, 0.6]]) # [H, H]
b   = np.array([0.0, 0.1])
xs  = [1.0, 0.0, -1.0]                    # 입력 스트림 3개 샘플

h = np.zeros(2)
for t, x in enumerate(xs, 1):
    a = W_x[:, 0] * x + W_h @ h + b       # pre-activation
    h = np.tanh(a)
    print(f"t={t} a={np.round(a, 4)} h={np.round(h, 4)}")

rnn = torch.nn.RNN(input_size=1, hidden_size=2, batch_first=True)
with torch.no_grad():
    rnn.weight_ih_l0.copy_(torch.tensor(W_x)); rnn.weight_hh_l0.copy_(torch.tensor(W_h))
    rnn.bias_ih_l0.copy_(torch.tensor(b));     rnn.bias_hh_l0.zero_()   # torch는 bias가 2개
    out, h_n = rnn(torch.tensor(xs, dtype=torch.float32).view(1, 3, 1))
print("torch out (t=1..3):\n", out.squeeze(0).numpy().round(4))
print("max |numpy - torch| =", float(np.abs(out[0, -1].numpy() - h).max()))
```

```text
t=1 a=[ 0.5 -0.2] h=[ 0.4621 -0.1974]
t=2 a=[0.4092 0.0278] h=[0.3878 0.0278]
t=3 a=[-0.1953  0.4554] h=[-0.1929  0.4264]
torch out (t=1..3):
 [[ 0.4621 -0.1974]
 [ 0.3878  0.0278]
 [-0.1929  0.4264]]
max |numpy - torch| = 1.3327018072439856e-08
```

출력에서 볼 것: 세 결과가 손계산과 일치한다(`a₃[1]`이 0.4554 vs 손계산 0.4555인 것은 중간 반올림 차이). 차이 1e-8은 torch가 float32로 계산해서 생기는 것이다. PyTorch 이름 규칙도 기억하자: `weight_ih_l0` = input→hidden(`W_x`), `weight_hh_l0` = hidden→hidden(`W_h`), `_l0` = 0번째 층.

### 2.5 torch의 bias는 두 개다

PyTorch(와 cuDNN)는 `h_t = tanh(W_ih·x + b_ih + W_hh·h + b_hh)`로 bias를 두 벌 둔다. 수학적으로는 `b = b_ih + b_hh` 하나로 합칠 수 있어서 vanilla RNN과 LSTM에서는 중복이다. 위 코드에서 `bias_hh_l0`를 0으로 둔 이유다. 펌웨어로 내보낼 때는 **두 bias를 더해서 하나로 접으면(fold)** 덧셈이 하나 줄어든다. 단, GRU는 예외다(5절) — `b_hn`은 reset gate 안쪽에 있어서 합칠 수 없다.

---

## 3. BPTT와 vanishing / exploding gradient

### 3.1 BPTT = 펼친 그래프에 backprop

RNN 학습은 특별한 알고리즘이 아니다. 그림 1처럼 시간 축으로 펼친 뒤, 그것을 "깊이 T짜리 네트워크"로 보고 A3의 backprop을 그대로 적용한다. 이것을 **BPTT(backpropagation through time)** 라고 부른다. 다른 점은 모든 층이 같은 W를 공유하므로, 각 step에서 나온 gradient를 **전부 더한다**는 것뿐이다.

문제는 먼 과거로 gradient를 보낼 때 생긴다. chain rule로 `h_t`가 `h_0`에 얼마나 민감한지 쓰면:

```
 ∂h_t/∂h_{t−1} = diag(1 − h_t²) · W_h                 (tanh′ = 1 − tanh²)

 ∂h_t/∂h_0 = ∏_{k=1..t} diag(1 − h_k²) · W_h          ← 같은 W_h를 t번 곱한다
```

말로 하면: t step 전의 입력이 지금의 loss에 주는 영향은 **Jacobian t개의 곱**이다. 같은 행렬을 반복해서 곱하면 그 크기는 대략 (특이값)^t로 변한다. 특이값이 1보다 작으면 0으로 사라지고(**vanishing**), 크면 폭발한다(**exploding**). A3 8절의 "깊은 MLP에서 층별 gain의 곱"과 같은 현상인데, RNN은 **같은** 행렬을 곱하므로 더 극단적이다.

DSP 비유: IIR 필터에서 pole 반지름 r이면 impulse response가 r^t로 변한다. r < 1이면 과거 입력의 영향이 지수적으로 사라지고, r > 1이면 발산한다. vanilla RNN의 "기억력"도 이 pole 반지름에 묶여 있다.

### 3.2 숫자로 보기 — Jacobian 곱의 크기

모든 특이값이 정확히 `gain`인 `W_h`(직교행렬 × gain)를 만들어, t step 동안 Jacobian 곱의 최대 특이값(= 최악의 증폭률)을 추적한다.

```python
# BPTT: ‖∂h_t/∂h_0‖ 가 step 수 t에 따라 어떻게 변하나 (tanh RNN vs 선형 RNN)
import numpy as np
rng = np.random.default_rng(0)
H, T = 16, 50
Q, _ = np.linalg.qr(rng.standard_normal((H, H)))   # 직교행렬: 모든 특이값 = 1
xs = rng.standard_normal((T, H)) * 0.5             # 입력 (W_x·x를 미리 곱한 것으로 취급)

def jac_norms(gain, act=True):
    W_h = gain * Q                                  # 특이값이 전부 gain
    h, J, norms = np.zeros(H), np.eye(H), []
    for t in range(T):
        a = W_h @ h + xs[t]
        h = np.tanh(a) if act else a
        D = np.diag(1 - h**2) if act else np.eye(H) # tanh'(a) = 1 - tanh(a)²
        J = D @ W_h @ J                             # chain rule: step마다 곱이 하나씩 늘어남
        norms.append(np.linalg.norm(J, 2))          # 최대 특이값 = 최악 증폭률
    return np.array(norms)

for name, g, act in [("tanh g=0.5", 0.5, 1), ("tanh g=1.0", 1.0, 1), ("tanh g=1.5", 1.5, 1),
                     ("linear g=0.9", 0.9, 0), ("linear g=1.1", 1.1, 0)]:
    n = jac_norms(g, act)
    print(f"{name:13s}" + " ".join(f"t{t}={n[t-1]:.1e}" for t in [1, 5, 10, 20, 50]))
```

```text
tanh g=0.5   t1=5.0e-01 t5=2.5e-02 t10=3.7e-04 t20=9.5e-08 t50=7.0e-19
tanh g=1.0   t1=1.0e+00 t5=7.1e-01 t10=2.7e-01 t20=2.8e-02 t50=3.2e-05
tanh g=1.5   t1=1.5e+00 t5=3.5e+00 t10=5.0e+00 t20=2.0e+00 t50=3.1e-01
linear g=0.9 t1=9.0e-01 t5=5.9e-01 t10=3.5e-01 t20=1.2e-01 t50=5.2e-03
linear g=1.1 t1=1.1e+00 t5=1.6e+00 t10=2.6e+00 t20=6.7e+00 t50=1.2e+02
```

출력에서 볼 것: 선형 RNN은 정확히 `gain^t`다(0.9⁵⁰ ≈ 5.2e−3, 1.1⁵⁰ ≈ 117). tanh RNN은 gain=1이어도 `1 − h²` < 1 인자가 매 step 곱해져서 50 step 뒤 1e−5로 줄어든다. gain=1.5는 처음엔 커지다가(t=10에서 5배) 상태가 포화되면서 tanh′이 작아져 다시 줄어든다. 즉 **tanh RNN에서는 vanishing이 기본값**이고, exploding은 상태가 선형 구간에 머무는 동안 순간적으로 튀는 형태로 나타난다.

```svg
<svg viewBox="0 0 660 345" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="300" x2="620" y2="300" stroke="currentColor"/><line x1="70" y1="30" x2="70" y2="300" stroke="currentColor"/>
<line x1="70" y1="103.6" x2="620" y2="103.6" stroke="#888" stroke-dasharray="3 3"/>
<text x="62" y="34" font-size="12" text-anchor="end">10³</text><text x="62" y="107.6" font-size="12" text-anchor="end">10⁰</text><text x="62" y="156.7" font-size="12" text-anchor="end">10⁻²</text><text x="62" y="205.8" font-size="12" text-anchor="end">10⁻⁴</text><text x="62" y="254.9" font-size="12" text-anchor="end">10⁻⁶</text><text x="62" y="304" font-size="12" text-anchor="end">10⁻⁸</text>
<line x1="70" y1="300" x2="70" y2="305" stroke="currentColor"/><text x="70" y="318" font-size="12" text-anchor="middle">1</text><line x1="171" y1="300" x2="171" y2="305" stroke="currentColor"/><text x="171" y="318" font-size="12" text-anchor="middle">10</text><line x1="283.3" y1="300" x2="283.3" y2="305" stroke="currentColor"/><text x="283.3" y="318" font-size="12" text-anchor="middle">20</text><line x1="395.5" y1="300" x2="395.5" y2="305" stroke="currentColor"/><text x="395.5" y="318" font-size="12" text-anchor="middle">30</text><line x1="507.8" y1="300" x2="507.8" y2="305" stroke="currentColor"/><text x="507.8" y="318" font-size="12" text-anchor="middle">40</text><line x1="620" y1="300" x2="620" y2="305" stroke="currentColor"/><text x="620" y="318" font-size="12" text-anchor="middle">50</text>
<polyline points="70.0,111.0 81.2,119.0 92.4,127.0 103.7,135.0 114.9,142.8 126.1,151.6 137.3,160.3 148.6,169.2 159.8,177.7 171.0,187.9 182.2,196.9 193.5,205.1 204.7,213.8 215.9,221.8 227.1,230.8 238.4,239.3 249.6,248.0 260.8,257.9 272.0,266.8 283.3,275.9 294.5,284.9 305.7,294.0" fill="none" stroke="#888" stroke-width="2"/>
<polyline points="70.0,103.6 81.2,104.3 92.4,105.1 103.7,106.4 114.9,107.3 126.1,109.1 137.3,111.5 148.6,112.4 159.8,114.9 171.0,117.5 182.2,119.1 193.5,121.3 204.7,123.5 215.9,127.2 227.1,129.8 238.4,132.0 249.6,133.8 260.8,138.7 272.0,140.6 283.3,141.7 294.5,142.2 305.7,143.6 316.9,145.5 328.2,148.2 339.4,149.3 350.6,152.6 361.8,154.2 373.1,157.1 384.3,159.8 395.5,162.0 406.7,163.7 418.0,168.2 429.2,169.4 440.4,171.9 451.6,174.2 462.9,175.2 474.1,177.0 485.3,179.4 496.5,182.9 507.8,185.0 519.0,187.8 530.2,190.6 541.4,195.7 552.7,199.2 563.9,202.6 575.1,205.4 586.3,206.6 597.6,208.7 608.8,211.4 620.0,213.9" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<polyline points="70.0,99.3 81.2,95.9 92.4,92.9 103.7,92.2 114.9,90.1 126.1,89.7 137.3,87.9 148.6,87.0 159.8,86.2 171.0,86.4 182.2,84.0 193.5,85.4 204.7,82.5 215.9,81.4 227.1,83.5 238.4,86.5 249.6,88.0 260.8,95.8 272.0,96.3 283.3,96.3 294.5,94.9 305.7,92.9 316.9,90.6 328.2,89.4 339.4,90.2 350.6,93.0 361.8,94.7 373.1,96.8 384.3,96.4 395.5,95.1 406.7,97.3 418.0,97.3 429.2,101.4 440.4,101.3 451.6,102.5 462.9,103.5 474.1,102.0 485.3,106.7 496.5,106.8 507.8,111.4 519.0,111.5 530.2,112.3 541.4,113.5 552.7,114.1 563.9,117.3 575.1,116.4 586.3,114.3 597.6,115.5 608.8,115.2 620.0,116.2" fill="none" stroke="#e08a3c" stroke-width="2"/>
<polyline points="70.0,104.8 81.2,105.9 92.4,107.0 103.7,108.1 114.9,109.3 126.1,110.4 137.3,111.5 148.6,112.6 159.8,113.7 171.0,114.9 182.2,116.0 193.5,117.1 204.7,118.2 215.9,119.4 227.1,120.5 238.4,121.6 249.6,122.7 260.8,123.9 272.0,125.0 283.3,126.1 294.5,127.2 305.7,128.3 316.9,129.5 328.2,130.6 339.4,131.7 350.6,132.8 361.8,134.0 373.1,135.1 384.3,136.2 395.5,137.3 406.7,138.5 418.0,139.6 429.2,140.7 440.4,141.8 451.6,142.9 462.9,144.1 474.1,145.2 485.3,146.3 496.5,147.4 507.8,148.6 519.0,149.7 530.2,150.8 541.4,151.9 552.7,153.1 563.9,154.2 575.1,155.3 586.3,156.4 597.6,157.5 608.8,158.7 620.0,159.8" fill="none" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="5 3"/>
<polyline points="70.0,102.6 81.2,101.6 92.4,100.6 103.7,99.6 114.9,98.6 126.1,97.5 137.3,96.5 148.6,95.5 159.8,94.5 171.0,93.5 182.2,92.5 193.5,91.4 204.7,90.4 215.9,89.4 227.1,88.4 238.4,87.4 249.6,86.4 260.8,85.3 272.0,84.3 283.3,83.3 294.5,82.3 305.7,81.3 316.9,80.3 328.2,79.3 339.4,78.2 350.6,77.2 361.8,76.2 373.1,75.2 384.3,74.2 395.5,73.2 406.7,72.1 418.0,71.1 429.2,70.1 440.4,69.1 451.6,68.1 462.9,67.1 474.1,66.0 485.3,65.0 496.5,64.0 507.8,63.0 519.0,62.0 530.2,61.0 541.4,59.9 552.7,58.9 563.9,57.9 575.1,56.9 586.3,55.9 597.6,54.9 608.8,53.9 620.0,52.8" fill="none" stroke="#d0564a" stroke-width="2" stroke-dasharray="5 3"/>
<text x="312" y="292" font-size="12">tanh g=0.5 → t=50에서 7e−19</text>
<line x1="480" y1="232" x2="500" y2="232" stroke="#888" stroke-width="2"/><text x="506" y="236" font-size="12">tanh g=0.5</text>
<line x1="480" y1="247" x2="500" y2="247" stroke="#4a7bd0" stroke-width="2"/><text x="506" y="251" font-size="12">tanh g=1.0</text>
<line x1="480" y1="262" x2="500" y2="262" stroke="#e08a3c" stroke-width="2"/><text x="506" y="266" font-size="12">tanh g=1.5</text>
<line x1="480" y1="277" x2="500" y2="277" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="5 3"/><text x="506" y="281" font-size="12">linear g=0.9</text>
<line x1="480" y1="292" x2="500" y2="292" stroke="#d0564a" stroke-width="2" stroke-dasharray="5 3"/><text x="506" y="296" font-size="12">linear g=1.1</text>
<text x="76" y="22" font-size="12">‖∂h_t/∂h_0‖ (log 눈금)</text><text x="345" y="338" font-size="12" text-anchor="middle">t (몇 step 과거로 거슬러 가나)</text>
</svg>
```

그림 2 — 위 코드가 실제로 계산한 값의 그래프(세로축 log). 점선 가로줄이 1(10⁰). 1보다 아래는 "먼 과거의 영향이 사라진다", 위는 "폭발한다". 회색(g=0.5)은 t=22쯤 이미 10⁻⁸ 아래로 내려가 그래프를 벗어난다. 실무에서 vanilla RNN이 수십 step 이상의 의존성을 잘 배우지 못하는 이유가 이 그림 하나로 설명된다.

### 3.3 대응책

- **Gradient clipping** (A3 8절): gradient norm이 임계값을 넘으면 방향은 두고 길이만 자른다. exploding에 대한 표준 대책이고, RNN 학습에서는 거의 항상 켠다(10절 코드도 `clip_grad_norm_(…, 1.0)` 사용).
- **Gate 구조 (LSTM/GRU)**: vanishing에 대한 구조적 해결책. 곱셈 대신 "덧셈으로 이어지는 경로"를 만든다(4절).
- **Truncated BPTT**: 긴 스트림을 예를 들어 100 step씩 잘라 학습하되, 앞 조각의 마지막 상태를 다음 조각의 초기 상태로 넘긴다(gradient는 조각 경계에서 끊음, `h.detach()`). 메모리를 조각 길이에 비례하게 묶는 방법이다. 대신 조각 길이보다 먼 의존성은 gradient로 직접 배우지 못한다.
- **초기화**: `W_h`를 직교행렬로 초기화하면 특이값이 1에서 출발한다.

---

## 4. LSTM — 조절 가능한 메모리 레지스터

### 4.1 직관

vanilla RNN의 문제는 상태가 **매 step 행렬곱과 tanh를 통과해야** 한다는 것이다. 정보를 100 step 보관하려면 100번 변형을 견뎌야 한다.

LSTM(Long Short-Term Memory, Hochreiter & Schmidhuber 1997; forget gate는 Gers et al. 2000에서 추가)은 **cell state `c`** 라는 별도의 레지스터를 둔다. `c`는 매 step 행렬곱을 거치지 않고, **원소별 곱 하나와 덧셈 하나**만 거친다.

```
 c_t = f_t ⊙ c_{t−1}  +  i_t ⊙ g_t
       └ 얼마나 유지할까 ┘   └ 무엇을 얼마나 쓸까 ┘
```

펌웨어 비유로 말하면 `c`는 **write-enable과 clear 신호가 있는 레지스터**다.

- forget gate `f` ≈ 1, input gate `i` ≈ 0 → "hold" (값 유지)
- `f` ≈ 0, `i` ≈ 1 → "load" (새 값으로 덮어쓰기)
- `f` ≈ 0, `i` ≈ 0 → "clear"
- output gate `o` → 레지스터 값을 버스(h)에 얼마나 내보낼지 (tri-state enable 같은 것)

gate 값은 0~1 사이의 연속값(sigmoid 출력)이라 "얼마나"를 조절할 수 있고, 그 조절 자체를 현재 입력과 상태를 보고 **학습된 규칙으로** 정한다. `f`가 1에 가까우면 `∂c_t/∂c_{t−1} = f_t` ≈ 1이라 gradient도 사라지지 않고 먼 과거까지 흐른다. 이것이 vanishing gradient에 대한 구조적 해결책이다.

### 4.2 식

```
 i_t = σ( W_ii·x_t + W_hi·h_{t−1} + b_i )        input gate   (0~1)
 f_t = σ( W_if·x_t + W_hf·h_{t−1} + b_f )        forget gate  (0~1)
 g_t = tanh( W_ig·x_t + W_hg·h_{t−1} + b_g )     candidate    (−1~1, 쓸 값)
 o_t = σ( W_io·x_t + W_ho·h_{t−1} + b_o )        output gate  (0~1)

 c_t = f_t ⊙ c_{t−1} + i_t ⊙ g_t
 h_t = o_t ⊙ tanh(c_t)
```

말로 하면: 4개의 "작은 RNN 블록"이 같은 입력 `[x_t, h_{t−1}]`을 보고 각자 값을 낸다. 셋(i, f, o)은 sigmoid로 0~1의 "밸브 개도"가 되고, 하나(g)는 tanh로 "써 넣을 후보 값"이 된다. `⊙`는 원소별 곱(elementwise product)이다. 상태는 `(h, c)` **두 개**를 들고 다녀야 한다.

PyTorch는 4개 gate의 weight를 한 행렬로 쌓아 둔다: `weight_ih_l0`의 shape은 `[4H, I]`이고 행 순서는 **i, f, g, o** 다. 다른 프레임워크(Keras는 i, f, c, o; ONNX는 i, o, f, c)와 순서가 다를 수 있어서, 가중치를 직접 옮길 때 가장 흔한 버그 원인이다.

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="b3l-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="40" y="60" font-size="13">c_{t−1}</text><text x="626" y="74" font-size="13">c_t</text>
<line x1="40" y1="70" x2="187" y2="70" stroke="currentColor" stroke-width="3"/><line x1="212" y1="70" x2="367" y2="70" stroke="currentColor" stroke-width="3"/><line x1="392" y1="70" x2="618" y2="70" stroke="currentColor" stroke-width="3" marker-end="url(#b3l-a)"/>
<text x="330" y="40" font-size="12" text-anchor="middle">cell state c = 메모리 레지스터 (곱 1번 + 덧셈 1번만 거친다)</text>
<circle cx="200" cy="70" r="12" fill="none" stroke="#d0564a" stroke-width="2"/><text x="200" y="75" font-size="14" text-anchor="middle">×</text>
<circle cx="380" cy="70" r="12" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="380" y="75" font-size="14" text-anchor="middle">+</text>
<circle cx="380" cy="125" r="12" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="380" y="130" font-size="14" text-anchor="middle">×</text>
<rect x="170" y="180" width="60" height="36" rx="5" fill="none" stroke="#d0564a" stroke-width="2"/><text x="200" y="203" font-size="13" text-anchor="middle">f  σ</text>
<rect x="270" y="180" width="60" height="36" rx="5" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="300" y="203" font-size="13" text-anchor="middle">i  σ</text>
<rect x="345" y="180" width="70" height="36" rx="5" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="380" y="203" font-size="13" text-anchor="middle">g  tanh</text>
<rect x="430" y="180" width="60" height="36" rx="5" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="460" y="203" font-size="13" text-anchor="middle">o  σ</text>
<line x1="200" y1="180" x2="200" y2="84" stroke="#d0564a" stroke-width="2" marker-end="url(#b3l-a)"/>
<polyline points="300,180 300,125 366,125" fill="none" stroke="#3f9a6b" stroke-width="2" marker-end="url(#b3l-a)"/>
<line x1="380" y1="180" x2="380" y2="139" stroke="#4a7bd0" stroke-width="2" marker-end="url(#b3l-a)"/>
<line x1="380" y1="113" x2="380" y2="84" stroke="#3f9a6b" stroke-width="2" marker-end="url(#b3l-a)"/>
<line x1="560" y1="70" x2="560" y2="116" stroke="currentColor" marker-end="url(#b3l-a)"/><ellipse cx="560" cy="130" rx="24" ry="13" fill="none" stroke="currentColor"/><text x="560" y="134" font-size="12" text-anchor="middle">tanh</text>
<line x1="560" y1="143" x2="560" y2="184" stroke="currentColor" marker-end="url(#b3l-a)"/>
<circle cx="560" cy="198" r="12" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="560" y="203" font-size="14" text-anchor="middle">×</text>
<line x1="490" y1="198" x2="546" y2="198" stroke="#e08a3c" stroke-width="2" marker-end="url(#b3l-a)"/>
<line x1="572" y1="198" x2="615" y2="198" stroke="currentColor" stroke-width="2" marker-end="url(#b3l-a)"/><text x="620" y="202" font-size="13">h_t</text>
<line x1="40" y1="280" x2="460" y2="280" stroke="currentColor" stroke-width="2"/><text x="40" y="272" font-size="13">h_{t−1}</text>
<line x1="120" y1="320" x2="120" y2="282" stroke="currentColor" marker-end="url(#b3l-a)"/><text x="132" y="318" font-size="13">x_t</text>
<text x="330" y="300" font-size="12" text-anchor="middle">[h_{t−1}, x_t] → 네 gate가 같은 입력을 본다</text>
<line x1="200" y1="280" x2="200" y2="218" stroke="currentColor" marker-end="url(#b3l-a)"/><line x1="300" y1="280" x2="300" y2="218" stroke="currentColor" marker-end="url(#b3l-a)"/><line x1="380" y1="280" x2="380" y2="218" stroke="currentColor" marker-end="url(#b3l-a)"/><line x1="460" y1="280" x2="460" y2="218" stroke="currentColor" marker-end="url(#b3l-a)"/>
<text x="140" y="130" font-size="12" text-anchor="middle">forget: 얼마나 유지</text><text x="300" y="112" font-size="12" text-anchor="middle">input: 얼마나 쓸까</text><text x="490" y="240" font-size="12" text-anchor="middle">output: 얼마나 내보낼까</text>
</svg>
```

그림 3 — LSTM cell. 위쪽 굵은 선이 cell state 고속도로다. 빨강 forget gate가 기존 값에 곱해지고(×), 초록 input gate × 파랑 후보 g가 더해진다(+). 출력 h_t는 c_t를 tanh로 누른 뒤 주황 output gate로 조절한 값이다. 네 gate 모두 아래 버스의 `[h_{t−1}, x_t]`를 입력으로 받는다.

### 4.3 손계산 — 스칼라 LSTM

H=1, I=1로 줄이고, 손계산이 쉽도록 h→gate 연결은 0으로 둔다. forget bias를 2로 줘서 `f = σ(2) ≈ 0.881`로 고정되게 한다.

```
 weight (x에 곱함): i=1.0, f=0.0, g=2.0, o=0.5      bias: f만 2.0      입력 x = 1, 0, 0, −1

 t=1 (x=1):  i = σ(1) = 0.731   f = σ(2) = 0.881   g = tanh(2) = 0.964   o = σ(0.5) = 0.622
             c₁ = 0.881·0 + 0.731·0.964 = 0.7048
             h₁ = 0.622 · tanh(0.7048) = 0.622 · 0.6075 = 0.3781

 t=2 (x=0):  i = σ(0) = 0.5    g = tanh(0) = 0    o = 0.5
             c₂ = 0.881·0.7048 + 0.5·0 = 0.6208       ← 새로 쓴 것 없이 88%만 유지
             h₂ = 0.5 · tanh(0.6208) = 0.5 · 0.5517 = 0.2758
```

말로 하면: t=1에 들어온 "1"이 cell에 0.70으로 기록되고, 입력이 없는 동안 매 step 0.881배로 천천히 새어 나간다. **forget gate가 1차 IIR의 pole 역할**을 하는 셈이다. 차이는 이 pole이 고정 상수가 아니라 입력과 상태에 따라 매 step 달라질 수 있다는 것이다(여기서는 손계산을 위해 고정했다).

```python
# 스칼라 LSTM(I=1, H=1): 식대로 손계산 → nn.LSTM과 비교. torch gate 순서 = i, f, g, o
import numpy as np, torch
sig = lambda z: 1 / (1 + np.exp(-z))
#               i     f     g     o
wx = np.array([1.0,  0.0,  2.0,  0.5])     # 입력 x에 곱하는 weight
wh = np.array([0.0,  0.0,  0.0,  0.0])     # 손계산을 쉽게: h→gate 연결 0
bb = np.array([0.0,  2.0,  0.0,  0.0])     # forget bias 2 → f = σ(2) ≈ 0.88
h, c = 0.0, 0.0
for t, x in enumerate([1.0, 0.0, 0.0, -1.0], 1):
    zi, zf, zg, zo = wx * x + wh * h + bb
    i, f, g, o = sig(zi), sig(zf), np.tanh(zg), sig(zo)
    c = f * c + i * g                       # 셀: 일부 잊고(f) 새 값 기록(i·g)
    h = o * np.tanh(c)                      # 출력: 셀을 얼마나 내보낼지(o)
    print(f"t={t} x={x:+.0f} i={i:.3f} f={f:.3f} g={g:+.3f} o={o:.3f} -> c={c:+.4f} h={h:+.4f}")

m = torch.nn.LSTM(1, 1)
with torch.no_grad():
    m.weight_ih_l0.copy_(torch.tensor(wx).view(4, 1)); m.weight_hh_l0.copy_(torch.tensor(wh).view(4, 1))
    m.bias_ih_l0.copy_(torch.tensor(bb)); m.bias_hh_l0.zero_()
    y, (hn, cn) = m(torch.tensor([1.0, 0.0, 0.0, -1.0]).view(4, 1, 1))
print(f"torch: h={hn.item():+.4f} c={cn.item():+.4f}")
```

```text
t=1 x=+1 i=0.731 f=0.881 g=+0.964 o=0.622 -> c=+0.7048 h=+0.3781
t=2 x=+0 i=0.500 f=0.881 g=+0.000 o=0.500 -> c=+0.6208 h=+0.2758
t=3 x=+0 i=0.500 f=0.881 g=+0.000 o=0.500 -> c=+0.5468 h=+0.2490
t=4 x=-1 i=0.269 f=0.881 g=-0.964 o=0.378 -> c=+0.2223 h=+0.0826
torch: h=+0.0826 c=+0.2223
```

출력에서 볼 것: c가 0.7048 → 0.6208 → 0.5468로 매번 0.881배가 된다(leaky integrator). t=4의 음수 입력은 `i·g = 0.269·(−0.964) = −0.259`를 더해 c를 0.2223으로 끌어내린다. torch의 최종 `(h, c)`가 우리 식과 소수 4자리까지 같다 — **gate 순서 i, f, g, o를 맞췄기 때문**이다.

### 4.4 forget gate bias 초기화

학습 초기에 `f`가 0.5 근처면 cell이 매 step 절반씩 잊어서 LSTM의 장점이 사라진다. 그래서 흔히 forget gate bias를 1(또는 그 이상)로 초기화해 "기본값은 기억"이 되게 한다(Jozefowicz et al. 2015가 이 효과를 보고했다). PyTorch 기본 초기화는 이것을 하지 않으므로 필요하면 `bias_ih_l0[H:2H]`를 직접 채운다.

---

## 5. GRU — gate 3개짜리 경량 LSTM

### 5.1 식 (PyTorch 형식)

GRU(Gated Recurrent Unit, Cho et al. 2014)는 LSTM을 단순화했다. cell state를 따로 두지 않고 `h` 하나만 들고 다니며, gate가 2개(reset r, update z) + 후보 1개(n)다.

```
 r_t = σ( W_ir·x_t + b_ir + W_hr·h_{t−1} + b_hr )            reset gate
 z_t = σ( W_iz·x_t + b_iz + W_hz·h_{t−1} + b_hz )            update gate
 n_t = tanh( W_in·x_t + b_in + r_t ⊙ (W_hn·h_{t−1} + b_hn) )  후보 상태
 h_t = (1 − z_t) ⊙ n_t + z_t ⊙ h_{t−1}
```

말로 하면: `z`는 "옛 상태를 얼마나 유지할지"의 **보간 비율**이다. z=1이면 `h_t = h_{t−1}` (hold), z=0이면 새 후보 n으로 교체(load). LSTM의 f와 i를 하나의 z로 묶어 `f = z`, `i = 1 − z`로 강제한 것과 비슷하다. `r`은 후보를 만들 때 "옛 상태를 얼마나 참고할지"를 정한다. r=0이면 과거를 무시하고 현재 입력만으로 후보를 만든다.

마지막 식은 펌웨어 엔지니어에게 익숙한 모양이다: `h ← h + (1−z)·(n − h)`. 즉 **적응형 1차 IIR (exponential moving average)** 에서 smoothing 계수 `1−z`를 매 step, 채널마다 네트워크가 정하는 것이다.

### 5.2 구현 차이 주의 — reset gate의 위치

원 논문(Cho et al. 2014)은 `r`을 `h`에 먼저 곱한 뒤 행렬곱을 한다: `W_hn·(r ⊙ h)`. PyTorch/cuDNN은 행렬곱을 먼저 하고 결과에 `r`을 곱한다: `r ⊙ (W_hn·h + b_hn)`. 둘은 **다른 함수**다. Keras는 `reset_after` 옵션으로 둘 다 지원하고 TF2 기본값이 `reset_after=True`(cuDNN 방식)다. 모델을 다른 런타임으로 옮기거나 C로 직접 짤 때 반드시 확인해야 한다. PyTorch 방식의 장점은 `W_hn·h`를 다른 두 gate의 행렬곱과 한꺼번에 `[3H, H]` GEMV 하나로 계산할 수 있다는 것이다(9절 C 코드가 이렇게 한다). 또 이 때문에 GRU의 `b_hn`은 `b_in`과 합칠 수 없다.

### 5.3 LSTM vs GRU

| 항목 | LSTM | GRU |
|---|---|---|
| 상태 | `h`, `c` 두 개 (2H floats) | `h` 하나 (H floats) |
| gate/블록 수 | 4 (i, f, g, o) | 3 (r, z, n) |
| 파라미터 | 4 × RNN 블록 | 3 × RNN 블록 (약 25% 적음) |
| step당 MAC | 4H(I+H) | 3H(I+H) |
| 기억 유지 | f, i 독립 (유지하면서 쓰기 가능) | z 하나로 유지·교체를 묶음 |
| 성능 | 긴 의존성·큰 데이터에서 약간 우세하다는 보고가 있음 | 작은 데이터·작은 모델에서 비슷한 경우가 많음 |
| 런타임 지원 | 대부분 런타임에 전용(fused) op가 있음 | 런타임에 따라 전용 op가 없어 기본 op로 풀어지기도 함 |

말로 하면: 결과 정확도는 과제마다 다르고(Chung et al. 2014의 비교에서도 명확한 승자가 없었다), 임베디드에서는 **메모리·연산이 25% 적고 상태가 절반인 GRU**가 출발점으로 자주 쓰인다. 다만 타깃 런타임이 LSTM만 최적화된 커널을 갖고 있다면 LSTM이 오히려 빠를 수 있다. 결정은 런타임의 op 지원 목록을 보고 한다.

---

## 6. 파라미터 수와 연산량

### 6.1 공식

vanilla RNN 블록 하나 = `W_x [H×I]` + `W_h [H×H]` + bias `[H]`.

```
 RNN 블록 = H·I + H·H + H             (torch: bias 2벌 → H·I + H·H + 2H)
 GRU  = 3 × RNN 블록
 LSTM = 4 × RNN 블록

 예: I = 40 (예: log-mel 40 band), H = 64
   RNN 블록 = 64·40 + 64·64 + 64 = 2560 + 4096 + 64 = 6720
   LSTM = 4 × 6720 = 26,880    (torch: 4 × (6720 + 64) = 27,136)
   GRU  = 3 × 6720 = 20,160    (torch: 3 × 6784 = 20,352)
```

말로 하면: 게이트 하나가 "작은 RNN 하나"이므로 LSTM은 4배, GRU는 3배다. `H·H` 항이 있어서 hidden을 2배로 키우면 파라미터는 거의 4배가 된다.

```python
# 파라미터 수: 공식(게이트 수 × RNN 블록) vs torch 실제 개수
import torch.nn as nn

def formula(I, H, gates, torch_bias=True):
    block = H * I + H * H + H * (2 if torch_bias else 1)   # W_x, W_h, bias
    return gates * block

def count(m): return sum(p.numel() for p in m.parameters())

I, H = 40, 64
for name, cls, g in [("RNN", nn.RNN, 1), ("GRU", nn.GRU, 3), ("LSTM", nn.LSTM, 4)]:
    m = cls(I, H)
    shapes = {n: tuple(p.shape) for n, p in m.named_parameters()}
    print(f"{name:4s} torch={count(m):6d} formula={formula(I, H, g):6d} "
          f"(bias 1개면 {formula(I, H, g, False)})  {shapes}")

m = nn.LSTM(I, H, num_layers=2, bidirectional=True)
print("LSTM 2층 양방향:", count(m), "  2층 입력 크기 =", m.weight_ih_l1.shape[1])
```

```text
RNN  torch=  6784 formula=  6784 (bias 1개면 6720)  {'weight_ih_l0': (64, 40), 'weight_hh_l0': (64, 64), 'bias_ih_l0': (64,), 'bias_hh_l0': (64,)}
GRU  torch= 20352 formula= 20352 (bias 1개면 20160)  {'weight_ih_l0': (192, 40), 'weight_hh_l0': (192, 64), 'bias_ih_l0': (192,), 'bias_hh_l0': (192,)}
LSTM torch= 27136 formula= 27136 (bias 1개면 26880)  {'weight_ih_l0': (256, 40), 'weight_hh_l0': (256, 64), 'bias_ih_l0': (256,), 'bias_hh_l0': (256,)}
LSTM 2층 양방향: 153600   2층 입력 크기 = 128
```

출력에서 볼 것: weight shape의 첫 축이 `gates × H` (192 = 3·64, 256 = 4·64)로 쌓여 있다. 2층 양방향 LSTM에서 2층의 입력 크기는 128 = 2 방향 × 64다. 계산: 1층 2 방향 × 27,136 = 54,272, 2층 2 방향 × 4·(64·128 + 64·64 + 128) = 99,328, 합 153,600.

### 6.2 step당 연산량과 메모리

```
 step당 MAC ≈ 파라미터 수 (weight 하나당 MAC 하나)
   GRU(I=40, H=64): 3·64·(40+64) = 19,968 MAC/step
   100 frame/s (10 ms hop) → 약 2.0 M MAC/s

 weight 메모리: 20,352 × 1 B (int8) ≈ 20 KB   /  × 4 B (fp32) ≈ 80 KB
 상태 메모리:   64 × 4 B = 256 B (fp32)       — 시퀀스 길이와 무관!
```

말로 하면: 초당 2 M MAC은 Cortex-M4/M33급 MCU의 연산 능력에 비해 작은 편이다(정확한 사이클 수는 커널과 코어에 따라 다르니 K 모듈에서 실측한다). 핵심은 **상태 메모리가 시퀀스 길이와 무관한 O(1)** 이라는 점이다. 1초 창을 버퍼링하는 CNN이나, 토큰마다 KV cache가 늘어나는 Transformer(B4)와 대조된다.

---

## 7. 쌓기 · 양방향 · 출력 방식

### 7.1 Stacked RNN

1층의 출력 시퀀스 `h¹_1..T`를 2층의 입력으로 넣는다. 층마다 자기 상태를 갖는다.

```
 x_t ──► [층1] ──h¹_t──► [층2] ──h²_t──► 출력층
          ↺ h¹             ↺ h²          상태 = 층 수 × H  (LSTM이면 × 2)
```

스트리밍에 문제없다. step t에서 층1 → 층2를 차례로 한 번씩 돌리면 된다. 상태만 층 수만큼 늘어난다.

### 7.2 Bidirectional RNN — 스트리밍 불가

정방향 RNN과 역방향 RNN(시퀀스를 뒤에서부터 읽음)을 따로 돌려서 출력을 이어 붙인다. step t의 출력이 **미래 입력 x_{t+1..T}** 에 의존한다.

```
 정방향:  x₁ → x₂ → x₃ → … → x_T
 역방향:  x₁ ← x₂ ← x₃ ← … ← x_T      ← x_T가 올 때까지 y₁을 낼 수 없다
```

따라서 지연(latency) = 시퀀스 전체 길이. 오프라인 분석(녹음 후 처리, 로그 라벨링)이나 짧은 고정 창 분류에는 좋지만, 프레임 단위 스트리밍에는 쓸 수 없다. 면접에서 "이 모델을 스트리밍으로 돌릴 수 있나?"를 물으면 가장 먼저 확인할 것이 `bidirectional=True` 여부다.

### 7.3 출력 방식

| 방식 | 모양 | 예 |
|---|---|---|
| many-to-one (시퀀스 분류) | 마지막 `h_T` 또는 `h_t`들의 평균/최대 → 분류기 | 1초 창 제스처 분류, 창 단위 KWS |
| many-to-many (동기) | 매 step `h_t` → 출력 `y_t` | 프레임 단위 VAD, 노이즈 억제 gain, 10절의 토글 |
| seq2seq (비동기) | encoder가 입력 전체를 요약 → decoder가 다른 길이의 출력 생성 | 번역, 초기 ASR (지금은 attention/Transformer로 대체) |

- **마지막 상태 vs pooling**: 마지막 상태는 스트리밍에 자연스럽지만, 창 초반 정보가 약해질 수 있다. mean pooling은 모든 step을 고르게 반영하지만 창 끝을 기다려야 한다. 스트리밍에서는 running mean(누적 합 / 개수)으로 대체할 수 있다.
- 스트리밍 many-to-many 출력은 흔히 **후처리 FSM**(연속 N 프레임 이상 ON일 때만 이벤트 발생, hangover 타이머)과 결합한다. 이 부분은 Don이 가장 잘하는 영역이다.

---

## 8. 스트리밍(stateful) 추론 — 프레임마다 한 step, 상태는 carry

### 8.1 그림으로

```svg
<svg viewBox="0 0 660 260" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="b3s-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="60" font-size="12">프레임</text><text x="10" y="130" font-size="12">GRU step</text><text x="10" y="186" font-size="12">출력</text>
<rect x="60" y="40" width="98" height="30" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="109" y="60" font-size="12" text-anchor="middle">frame 1</text>
<rect x="160" y="40" width="98" height="30" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="209" y="60" font-size="12" text-anchor="middle">frame 2</text>
<rect x="260" y="40" width="98" height="30" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="309" y="60" font-size="12" text-anchor="middle">frame 3</text>
<rect x="360" y="40" width="98" height="30" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="409" y="60" font-size="12" text-anchor="middle">frame 4</text>
<rect x="460" y="40" width="98" height="30" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="509" y="60" font-size="12" text-anchor="middle">frame 5</text>
<rect x="160" y="112" width="30" height="26" fill="none" stroke="#e08a3c" stroke-width="2"/><rect x="260" y="112" width="30" height="26" fill="none" stroke="#e08a3c" stroke-width="2"/><rect x="360" y="112" width="30" height="26" fill="none" stroke="#e08a3c" stroke-width="2"/><rect x="460" y="112" width="30" height="26" fill="none" stroke="#e08a3c" stroke-width="2"/><rect x="560" y="112" width="30" height="26" fill="none" stroke="#e08a3c" stroke-width="2"/>
<line x1="158" y1="72" x2="168" y2="110" stroke="#888" marker-end="url(#b3s-a)"/><line x1="258" y1="72" x2="268" y2="110" stroke="#888" marker-end="url(#b3s-a)"/><line x1="358" y1="72" x2="368" y2="110" stroke="#888" marker-end="url(#b3s-a)"/><line x1="458" y1="72" x2="468" y2="110" stroke="#888" marker-end="url(#b3s-a)"/><line x1="558" y1="72" x2="568" y2="110" stroke="#888" marker-end="url(#b3s-a)"/>
<line x1="110" y1="125" x2="158" y2="125" stroke="#3f9a6b" stroke-width="2" marker-end="url(#b3s-a)"/><text x="122" y="117" font-size="12" text-anchor="middle">h₀=0</text>
<line x1="190" y1="125" x2="258" y2="125" stroke="#3f9a6b" stroke-width="2" marker-end="url(#b3s-a)"/><text x="224" y="117" font-size="12" text-anchor="middle">h₁</text>
<line x1="290" y1="125" x2="358" y2="125" stroke="#3f9a6b" stroke-width="2" marker-end="url(#b3s-a)"/><text x="324" y="117" font-size="12" text-anchor="middle">h₂</text>
<line x1="390" y1="125" x2="458" y2="125" stroke="#3f9a6b" stroke-width="2" marker-end="url(#b3s-a)"/><text x="424" y="117" font-size="12" text-anchor="middle">h₃</text>
<line x1="490" y1="125" x2="558" y2="125" stroke="#3f9a6b" stroke-width="2" marker-end="url(#b3s-a)"/><text x="524" y="117" font-size="12" text-anchor="middle">h₄</text>
<line x1="175" y1="138" x2="175" y2="172" stroke="currentColor" marker-end="url(#b3s-a)"/><circle cx="175" cy="182" r="8" fill="none" stroke="currentColor"/><line x1="275" y1="138" x2="275" y2="172" stroke="currentColor" marker-end="url(#b3s-a)"/><circle cx="275" cy="182" r="8" fill="none" stroke="currentColor"/><line x1="375" y1="138" x2="375" y2="172" stroke="currentColor" marker-end="url(#b3s-a)"/><circle cx="375" cy="182" r="8" fill="none" stroke="currentColor"/><line x1="475" y1="138" x2="475" y2="172" stroke="currentColor" marker-end="url(#b3s-a)"/><circle cx="475" cy="182" r="8" fill="none" stroke="currentColor"/><line x1="575" y1="138" x2="575" y2="172" stroke="currentColor" marker-end="url(#b3s-a)"/><circle cx="575" cy="182" r="8" fill="none" stroke="currentColor"/>
<line x1="60" y1="225" x2="620" y2="225" stroke="currentColor" marker-end="url(#b3s-a)"/>
<line x1="60" y1="220" x2="60" y2="230" stroke="currentColor"/><text x="60" y="245" font-size="12" text-anchor="middle">0</text><line x1="160" y1="220" x2="160" y2="230" stroke="currentColor"/><text x="160" y="245" font-size="12" text-anchor="middle">10 ms</text><line x1="260" y1="220" x2="260" y2="230" stroke="currentColor"/><text x="260" y="245" font-size="12" text-anchor="middle">20 ms</text><line x1="360" y1="220" x2="360" y2="230" stroke="currentColor"/><text x="360" y="245" font-size="12" text-anchor="middle">30 ms</text><line x1="460" y1="220" x2="460" y2="230" stroke="currentColor"/><text x="460" y="245" font-size="12" text-anchor="middle">40 ms</text><line x1="560" y1="220" x2="560" y2="230" stroke="currentColor"/><text x="560" y="245" font-size="12" text-anchor="middle">50 ms</text>
<text x="620" y="206" font-size="12" text-anchor="end">지연 = 프레임 길이 + step 1회 연산</text>
</svg>
```

그림 4 — 스트리밍 추론 타임라인(10 ms 프레임 가정). 프레임이 끝나면(DMA 완료 인터럽트) 곧바로 GRU step 1회(주황)를 돌리고 출력 하나를 낸다. 호출과 호출 사이에 남는 것은 초록 화살표의 상태 h뿐이다. 창 방식처럼 1초를 모았다가 계산하는 burst가 없고, 매 프레임 부하가 일정하다.

### 8.2 코드로 확인 — 전체 시퀀스 = step 루프 = chunk 루프

"오프라인으로 전체 시퀀스를 한 번에 넣은 결과"와 "한 프레임씩 넣으며 상태를 넘긴 결과"가 같아야 스트리밍 배포가 학습과 일치한다. 그리고 상태를 넘기지 않으면(매번 0으로 리셋) 얼마나 틀리는지도 본다.

```python
# 전체 시퀀스 한 번 vs 한 프레임씩(상태 carry) vs 10프레임 chunk — 결과가 같은가
import torch
torch.manual_seed(0)
gru = torch.nn.GRU(input_size=6, hidden_size=16, batch_first=True).eval()
x = torch.randn(1, 100, 6)                     # IMU 6축, 100 프레임

with torch.no_grad():
    y_full, h_full = gru(x)                    # (1) 오프라인: 전체를 한 번에

    h, ys = None, []                           # (2) 스트리밍: 1 프레임씩, h를 들고 다님
    for t in range(100):
        y_t, h = gru(x[:, t:t+1, :], h)
        ys.append(y_t)
    y_step = torch.cat(ys, dim=1)

    h, ys = None, []                           # (3) 10 프레임 chunk 단위 (DMA 버퍼 단위)
    for s in range(0, 100, 10):
        y_c, h = gru(x[:, s:s+10, :], h)
        ys.append(y_c)
    y_chunk = torch.cat(ys, dim=1)

    y_reset = torch.cat([gru(x[:, t:t+1, :])[0] for t in range(100)], 1)  # (4) 버그: 매번 h=0

print("full vs step  max diff:", (y_full - y_step).abs().max().item())
print("full vs chunk max diff:", (y_full - y_chunk).abs().max().item())
print("full vs reset max diff:", (y_full - y_reset).abs().max().item())
print("state bytes (fp32):", h_full.numel() * 4)
```

```text
full vs step  max diff: 1.7881393432617188e-07
full vs chunk max diff: 8.940696716308594e-08
full vs reset max diff: 0.4398699700832367
state bytes (fp32): 64
```

출력에서 볼 것: (2)와 (3)은 float32 반올림 수준(1e−7)으로 (1)과 같다. 즉 **단방향 RNN은 상태만 넘기면 어떤 크기로 잘라 넣어도 결과가 같다**. (4)처럼 매 호출마다 상태를 새로 만들면 차이가 0.44로, 출력 범위(−1~1)에 비해 완전히 다른 값이 된다. 이 모델의 상태는 16 floats = 64 B다.

### 8.3 펌웨어 쪽 설계 포인트

- **상태의 소유권**: 상태 버퍼는 모델 밖(호출자)이 소유하는 것이 좋다. `gru_step(const float *x, float *h)`처럼. 그래야 여러 스트림(좌/우 이어버드, 여러 마이크)을 같은 weight로 처리할 수 있고, 저전력 진입 시 retention SRAM에 상태만 남길 수 있다.
- **리셋 정책**: 세션 시작, 착용 감지 변화, 긴 무음 뒤, 센서 오류 복구 뒤에 `h = 0`으로 리셋할지 정한다. 학습 때 `h_0 = 0`에서 시작하는 짧은 시퀀스로만 학습했다면, 수 시간 리셋 없이 돌 때의 동작은 **검증되지 않은 영역**이다(10절에서 길이가 늘면 정확도가 떨어지는 것을 본다).
- **Export**: ONNX/TFLite로 내보낼 때 상태를 그래프의 명시적 입력·출력으로 노출해야 한다(`(x_t, h_in) → (y_t, h_out)`). 그렇지 않으면 런타임이 매 호출마다 0으로 초기화하는 (4) 버그가 된다.
- **프레임 누락**: DMA overrun으로 프레임 하나를 놓치면, 창 모델은 그 창만 틀리지만 RNN은 상태가 어긋난 채로 계속 간다. 누락 프레임은 0 대신 직전 프레임 복제 같은 정책을 정하고 로그를 남긴다.

---

## 9. C로 GRU cell 구현 — torch와 비트 수준까지 맞추기

### 9.1 weight 내보내기

A5 8절의 방식 그대로, torch weight와 테스트 입력, **torch가 낸 기대 출력**을 C 헤더로 쓴다. C 쪽은 자기 결과를 기대 출력과 비교해 스스로 검증한다.

```python
# torch GRU weight + 테스트 입력 + 기대 출력(torch)을 C 헤더로 내보낸다
import torch
torch.manual_seed(1)
I, H, T = 6, 8, 20
gru = torch.nn.GRU(I, H, batch_first=True).eval()
x = torch.randn(1, T, I)
with torch.no_grad():
    y, _ = gru(x)

def arr(name, t):
    vals = ", ".join(f"{v:.9g}f" for v in t.flatten().tolist())
    return f"static const float {name}[{t.numel()}] = {{ {vals} }};\n"

with open("gru_weights.h", "w") as f:
    f.write(f"#define GRU_I {I}\n#define GRU_H {H}\n#define GRU_T {T}\n")
    f.write(arr("W_ih", gru.weight_ih_l0) + arr("W_hh", gru.weight_hh_l0))  # [3H, I], [3H, H] 순서 r,z,n
    f.write(arr("B_ih", gru.bias_ih_l0) + arr("B_hh", gru.bias_hh_l0))
    f.write(arr("X_in", x[0]) + arr("Y_ref", y[0]))                         # [T, I], [T, H]
print("wrote gru_weights.h:", sum(p.numel() for p in gru.parameters()), "params,", T, "frames")
```

```text
wrote gru_weights.h: 384 params, 20 frames
```

`%.9g`는 float32를 문자열로 바꿨다가 다시 읽어도 같은 비트가 되도록 하는 자릿수다(float32 왕복에는 유효숫자 9자리가 필요).

### 9.2 C 구현

```c
/* GRU cell 한 step (float, PyTorch 식 그대로). 상태 h[H]만 호출 사이에 유지한다. */
#include <math.h>
#include <stdio.h>
#include "gru_weights.h"

static float sigm(float z) { return 1.0f / (1.0f + expf(-z)); }

/* y = W[row0:row0+H, :] · v + b[row0:row0+H]  (row-major, n = 열 수) */
static void gemv(const float *W, const float *b, const float *v, int n, int row0, float *y) {
    for (int r = 0; r < GRU_H; r++) {
        float acc = b[row0 + r];
        for (int k = 0; k < n; k++) acc += W[(row0 + r) * n + k] * v[k];
        y[r] = acc;
    }
}

static void gru_step(const float *x, float *h) {          /* h: in/out */
    float xr[GRU_H], xz[GRU_H], xn[GRU_H], hr[GRU_H], hz[GRU_H], hn[GRU_H];
    gemv(W_ih, B_ih, x, GRU_I, 0, xr);  gemv(W_hh, B_hh, h, GRU_H, 0, hr);            /* r */
    gemv(W_ih, B_ih, x, GRU_I, GRU_H, xz);  gemv(W_hh, B_hh, h, GRU_H, GRU_H, hz);    /* z */
    gemv(W_ih, B_ih, x, GRU_I, 2 * GRU_H, xn); gemv(W_hh, B_hh, h, GRU_H, 2 * GRU_H, hn); /* n */
    for (int j = 0; j < GRU_H; j++) {
        float r = sigm(xr[j] + hr[j]);
        float z = sigm(xz[j] + hz[j]);
        float n = tanhf(xn[j] + r * hn[j]);                /* r은 (W_hn·h + b_hn) 전체에 곱 */
        h[j] = (1.0f - z) * n + z * h[j];
    }
}

int main(void) {
    float h[GRU_H] = {0};                                  /* 전원 투입/세션 시작 시 0 */
    float maxdiff = 0.0f;
    for (int t = 0; t < GRU_T; t++) {                      /* 프레임 인터럽트마다 1회라고 생각 */
        gru_step(&X_in[t * GRU_I], h);
        for (int j = 0; j < GRU_H; j++) {
            float d = fabsf(h[j] - Y_ref[t * GRU_H + j]);
            if (d > maxdiff) maxdiff = d;
        }
    }
    printf("h[0..3] at t=%d: %.6f %.6f %.6f %.6f\n", GRU_T - 1, h[0], h[1], h[2], h[3]);
    printf("max |C - torch| over %d frames = %.3g\n", GRU_T, maxdiff);
    printf("state = %zu bytes, weights = %zu bytes\n", sizeof h,
           sizeof W_ih + sizeof W_hh + sizeof B_ih + sizeof B_hh);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 gru_step.c -o gru_step -lm && ./gru_step
```

```text
h[0..3] at t=19: -0.239509 -0.295457 0.620079 0.422074
max |C - torch| over 20 frames = 1.19e-07
state = 32 bytes, weights = 1536 bytes
```

출력에서 볼 것: 경고 0개로 컴파일되고, 20 프레임 동안 모든 상태 원소가 torch와 1.2e−7 이내로 같다(torch의 마지막 상태 앞 4개도 `[-0.2395, -0.2955, 0.6201, 0.4221]`). 상태는 32 B, weight는 1.5 KB다.

코드에서 볼 것:

- `gru_step`은 **순수 함수 + 외부 상태**다. 전역 static 상태를 쓰지 않았기 때문에 여러 스트림을 처리할 수 있다.
- `W_ih·x` 부분(xr, xz, xn)은 `h`에 의존하지 않는다. 즉 **입력 투영은 프레임이 도착하자마자, 또는 여러 프레임을 모아 한꺼번에(GEMM으로) 미리 계산할 수 있다**. 반드시 순차적인 것은 `W_hh·h` 부분뿐이다. 11절에서 이 구분이 가속기 성능을 가른다.
- `r`은 `hn`(= `W_hn·h + b_hn`)에 곱한다 — 5.2절의 PyTorch 방식. 원 논문 방식으로 짜면 torch와 안 맞는다.
- 실제 MCU 배포에서는 `expf`/`tanhf`를 lookup table이나 구간 근사로 바꾸고, weight를 int8로 양자화한다(C1). 그때도 **이 float 버전을 golden reference로** 먼저 맞춰 두는 것이 순서다.

---

## 10. 작은 end-to-end — GRU가 T flip-flop을 배운다

### 10.1 문제

IMU 크기 신호에 잡음이 있고, 가끔 "탭" 스파이크가 온다. 목표는 **탭이 올 때마다 ON/OFF가 토글되는** 상태를 매 프레임 출력하는 것이다. 펌웨어라면 T flip-flop 하나로 끝나는 일이지만, 모델은 규칙을 모르고 (입력, 정답) 예시만 본다.

비교 대상은 과거 16 샘플만 보는 causal 1D CNN(B2 미리보기)이다. 이 과제는 탭이 16 샘플보다 오래전에 있었을 수 있으므로 창 모델로는 원리적으로 풀 수 없다.

```python
# GRU가 "tap 할 때마다 ON/OFF 토글"(T flip-flop)을 배울 수 있나 — 창 기반 CNN과 비교
import torch, torch.nn as nn, time
torch.manual_seed(0)

def make(B, T, p=0.04):
    taps = (torch.rand(B, T) < p).float()                 # tap 이벤트 (프레임당 4%)
    x = 0.3 * torch.randn(B, T) + 2.0 * taps              # noisy IMU 크기 + 스파이크
    y = torch.cumsum(taps, 1) % 2                          # 지금까지 tap 수가 홀수면 ON
    return x.unsqueeze(-1), y

class GRUNet(nn.Module):
    def __init__(s, H=16):
        super().__init__(); s.rnn = nn.GRU(1, H, batch_first=True); s.head = nn.Linear(H, 1)
    def forward(s, x, h=None):
        y, h = s.rnn(x, h); return s.head(y).squeeze(-1), h

class WinCNN(nn.Module):                                   # 과거 16샘플만 보는 causal conv
    def __init__(s, K=16):
        super().__init__(); s.K = K
        s.net = nn.Sequential(nn.Conv1d(1, 16, K), nn.ReLU(), nn.Conv1d(16, 1, 1))
    def forward(s, x):
        x = nn.functional.pad(x.transpose(1, 2), (s.K - 1, 0))   # 왼쪽만 pad = 미래 안 봄
        return s.net(x).squeeze(1), None

def train(model, steps=600):
    opt = torch.optim.Adam(model.parameters(), lr=1e-2)
    for _ in range(steps):
        x, y = make(64, 80)                                # 학습 시퀀스 길이 80
        loss = nn.functional.binary_cross_entropy_with_logits(model(x)[0], y)
        opt.zero_grad(); loss.backward()
        nn.utils.clip_grad_norm_(model.parameters(), 1.0); opt.step()   # A3의 clipping

def acc(m, x, y):
    with torch.no_grad(): return ((m(x)[0] > 0).float() == y).float().mean().item()

x80, y80 = make(200, 80); x400, y400 = make(200, 400)
for name, m in [("GRU", GRUNet()), ("WinCNN", WinCNN())]:
    t0 = time.time(); train(m); dt = time.time() - t0
    n = sum(p.numel() for p in m.parameters())
    print(f"{name:6s} params={n:4d} train {dt:4.1f}s  acc T=80: {acc(m, x80, y80):.3f}"
          f"  T=400: {acc(m, x400, y400):.3f}")
    if name == "GRU": torch.save(m.state_dict(), "gru_toggle.pt")
```

```text
GRU    params= 929 train  7.3s  acc T=80: 0.993  T=400: 0.936
WinCNN params= 289 train 15.0s  acc T=80: 0.634  T=400: 0.523
```

(학습 시간은 이 Mac의 CPU 기준이고 실행마다 조금 다르다. 정확도는 seed 고정으로 재현된다.)

출력에서 볼 것:

- **GRU 929 파라미터가 T flip-flop을 배웠다**: 학습 길이(80)에서 99.3%. 누구도 "토글"이라는 규칙을 알려 주지 않았다. GRU 상태 안에 두 개의 안정점(attractor, ON과 OFF)이 생기고, 스파이크가 한 안정점에서 다른 안정점으로 넘겨 준다 — 학습으로 만들어진 bistable latch다.
- **창 CNN은 원리적으로 실패**: T=400에서 52%, 동전 던지기 수준이다. T=80에서 63%인 것은 시퀀스 초반(첫 탭 전에는 항상 OFF)만 맞힌 덕이다.
- **길이 일반화 문제**: GRU도 학습보다 5배 긴 T=400에서는 93.6%로 떨어진다. 이 과제는 탭 하나를 놓치면 이후 전부가 반대로 뒤집히는 구조라 오류가 **영구적**이다. 배포 스트림은 학습 시퀀스보다 훨씬 길다는 점, 그래서 학습 시퀀스 길이·truncated BPTT·주기적 리셋 정책을 함께 설계해야 한다는 점을 보여 준다.

### 10.2 학습 코드에서 볼 것

- 모델 출력은 매 step `[B, T]` 로짓이고, loss도 모든 step에 대해 평균한다(many-to-many).
- `clip_grad_norm_`을 빼고 여러 seed로 돌려 보면 일부 seed에서 학습이 수렴하지 않는 것을 볼 수 있다(연습문제 4). 작은 RNN도 clipping이 안정성에 중요하다.
- 학습 때는 `gru(x)`로 80 step을 한 번에 넣지만, 배포 때는 8절처럼 한 프레임씩 넣어도 결과가 같다. 다음 절의 양자화 실험이 그렇게 돈다.

---

## 11. 왜 RNN은 NPU/GPU에서 불리하고 MCU에서는 유리한가

### 11.1 시간 축 의존성

```svg
<svg viewBox="0 0 660 235" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="b3d-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#d0564a"/></marker></defs>
<text x="115" y="22" font-size="13" text-anchor="middle">RNN (순차 체인)</text><line x1="35" y1="160" x2="35" y2="80" stroke="#888"/><line x1="43" y1="70" x2="58" y2="70" stroke="#d0564a" stroke-width="2" marker-end="url(#b3d-a)"/><line x1="67" y1="160" x2="67" y2="80" stroke="#888"/><line x1="75" y1="70" x2="90" y2="70" stroke="#d0564a" stroke-width="2" marker-end="url(#b3d-a)"/><line x1="99" y1="160" x2="99" y2="80" stroke="#888"/><line x1="107" y1="70" x2="122" y2="70" stroke="#d0564a" stroke-width="2" marker-end="url(#b3d-a)"/><line x1="131" y1="160" x2="131" y2="80" stroke="#888"/>
<line x1="139" y1="70" x2="154" y2="70" stroke="#d0564a" stroke-width="2" marker-end="url(#b3d-a)"/><line x1="163" y1="160" x2="163" y2="80" stroke="#888"/><line x1="171" y1="70" x2="186" y2="70" stroke="#d0564a" stroke-width="2" marker-end="url(#b3d-a)"/><line x1="195" y1="160" x2="195" y2="80" stroke="#888"/><circle cx="35" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="35" cy="70" r="8" fill="none" stroke="currentColor"/><circle cx="67" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="67" cy="70" r="8" fill="none" stroke="currentColor"/>
<circle cx="99" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="99" cy="70" r="8" fill="none" stroke="currentColor"/><circle cx="131" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="131" cy="70" r="8" fill="none" stroke="currentColor"/><circle cx="163" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="163" cy="70" r="8" fill="none" stroke="currentColor"/><circle cx="195" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="195" cy="70" r="8" fill="none" stroke="currentColor"/>
<text x="115" y="200" font-size="12" text-anchor="middle">입력 x₁ … x₆</text><text x="115" y="50" font-size="12" text-anchor="middle">출력 y₁ … y₆</text><text x="330" y="22" font-size="13" text-anchor="middle">1D CNN (국소 창, 병렬)</text><line x1="250" y1="162" x2="250" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/><line x1="250" y1="162" x2="282" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/><line x1="282" y1="162" x2="282" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/><line x1="250" y1="162" x2="314" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/><line x1="282" y1="162" x2="314" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/>
<line x1="314" y1="162" x2="314" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/><line x1="282" y1="162" x2="346" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/><line x1="314" y1="162" x2="346" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/><line x1="346" y1="162" x2="346" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/><line x1="314" y1="162" x2="378" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/><line x1="346" y1="162" x2="378" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/><line x1="378" y1="162" x2="378" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/><line x1="346" y1="162" x2="410" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/>
<line x1="378" y1="162" x2="410" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/><line x1="410" y1="162" x2="410" y2="78" stroke="#4a7bd0" stroke-opacity="0.7"/><circle cx="250" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="250" cy="70" r="8" fill="none" stroke="currentColor"/><circle cx="282" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="282" cy="70" r="8" fill="none" stroke="currentColor"/><circle cx="314" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="314" cy="70" r="8" fill="none" stroke="currentColor"/>
<circle cx="346" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="346" cy="70" r="8" fill="none" stroke="currentColor"/><circle cx="378" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="378" cy="70" r="8" fill="none" stroke="currentColor"/><circle cx="410" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="410" cy="70" r="8" fill="none" stroke="currentColor"/><text x="330" y="200" font-size="12" text-anchor="middle">입력 x₁ … x₆</text><text x="330" y="50" font-size="12" text-anchor="middle">출력 y₁ … y₆</text>
<text x="545" y="22" font-size="13" text-anchor="middle">Transformer (전체 과거, 병렬)</text><line x1="465" y1="162" x2="465" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="465" y1="162" x2="497" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="497" y1="162" x2="497" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="465" y1="162" x2="529" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="497" y1="162" x2="529" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="529" y1="162" x2="529" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="465" y1="162" x2="561" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/>
<line x1="497" y1="162" x2="561" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="529" y1="162" x2="561" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="561" y1="162" x2="561" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="465" y1="162" x2="593" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="497" y1="162" x2="593" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="529" y1="162" x2="593" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="561" y1="162" x2="593" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="593" y1="162" x2="593" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/>
<line x1="465" y1="162" x2="625" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="497" y1="162" x2="625" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="529" y1="162" x2="625" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="561" y1="162" x2="625" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="593" y1="162" x2="625" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><line x1="625" y1="162" x2="625" y2="78" stroke="#3f9a6b" stroke-opacity="0.55"/><circle cx="465" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="465" cy="70" r="8" fill="none" stroke="currentColor"/>
<circle cx="497" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="497" cy="70" r="8" fill="none" stroke="currentColor"/><circle cx="529" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="529" cy="70" r="8" fill="none" stroke="currentColor"/><circle cx="561" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="561" cy="70" r="8" fill="none" stroke="currentColor"/><circle cx="593" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="593" cy="70" r="8" fill="none" stroke="currentColor"/>
<circle cx="625" cy="170" r="8" fill="none" stroke="currentColor"/><circle cx="625" cy="70" r="8" fill="none" stroke="currentColor"/><text x="545" y="200" font-size="12" text-anchor="middle">입력 x₁ … x₆</text><text x="545" y="50" font-size="12" text-anchor="middle">출력 y₁ … y₆</text><text x="115" y="222" font-size="12" text-anchor="middle">출력 6개를 순서대로 6번 계산</text><text x="330" y="222" font-size="12" text-anchor="middle">6개 출력을 한 번에 (GEMM)</text><text x="545" y="222" font-size="12" text-anchor="middle">한 번에, 단 연결 수 ∝ t</text>
</svg>
```

그림 5 — 시간 축 의존성 비교. RNN(왼쪽)은 출력끼리 빨간 체인으로 묶여 있어 y₆를 계산하려면 y₁~y₅를 먼저 끝내야 한다. 1D CNN(가운데, 커널 3)은 각 출력이 가까운 입력 몇 개에만 의존하므로 모든 출력을 동시에 계산할 수 있다. Transformer(오른쪽, causal)는 각 출력이 과거 전체를 보지만 서로에게 의존하지 않아 학습·prefill 때 병렬로 계산된다. 대신 연결 수가 t에 비례해 늘어난다(B4의 KV cache).

### 11.2 작은 GEMV는 memory-bound

NPU와 GPU는 **큰 행렬곱(GEMM)** 을 위해 설계됐다. 수천 개의 MAC 유닛이 동시에 일하려면, 한 번 읽어 온 weight를 여러 입력에 재사용해야 한다. RNN의 순환 부분 `W_hh·h_{t−1}`은 매 step **벡터 하나**에 대한 행렬-벡터 곱(GEMV)이다. weight를 읽어 와서 딱 한 번 쓰고 버린다.

```
 GEMV (step 1회):  FLOP = 2·M·K,   bytes ≈ b·(M·K + K + M)    → intensity ≈ 2/b FLOP/byte
 GEMM (T step 묶음): FLOP = 2·M·K·T, bytes ≈ b·(M·K + K·T + M·T) → T가 크면 훨씬 큼
 (M = 3H 또는 4H, K = H, b = 원소당 바이트)
```

말로 하면: GEMV는 weight 1바이트를 읽을 때마다 연산을 2(fp32면 0.5)번밖에 못 한다. 이것을 **arithmetic intensity**가 낮다고 하고, 이런 연산은 MAC 유닛이 아니라 **메모리 대역폭**이 속도를 정한다(memory-bound). 자세한 roofline 분석은 D3에서 한다.

같은 FLOP을 "순차 GEMV 1000번"과 "GEMM 1번"으로 계산해 속도를 비교한다. GEMM 쪽은 GRU의 입력 투영(`W_ih·x`)처럼 시간 축으로 미리 묶을 수 있는 부분에 해당한다.

```python
# 같은 FLOP: 순차 GEMV 1000번(W_hh·h, 순환) vs GEMM 1번(W_ih·X, 시간축 병렬)
import numpy as np, time
rng = np.random.default_rng(0)
H, I, T = 64, 64, 1000
W = rng.standard_normal((3 * H, H)).astype(np.float32)   # GRU 한 층의 [3H, H]
X = rng.standard_normal((I, T)).astype(np.float32)
flops = 2 * W.shape[0] * W.shape[1] * T

def bench(f, reps=20):
    f(); t0 = time.perf_counter()
    for _ in range(reps): f()
    return (time.perf_counter() - t0) / reps

def seq():                                    # h_t가 h_{t-1}에 의존 → 하나씩
    h = X[:, 0].copy()
    for t in range(T): h = np.tanh(W @ h)[:H]
def par():                                    # 입력 투영은 모든 t를 한 번에
    np.tanh(W @ X)

for name, f in [("sequential GEMV x1000", seq), ("one GEMM [192x64]x[64x1000]", par)]:
    dt = bench(f); print(f"{name:28s} {dt*1e3:7.2f} ms  {flops/dt/1e9:6.2f} GFLOP/s")
M, K = W.shape
for b, tag in [(4, "fp32"), (1, "int8")]:
    ai_gemv = 2 * M * K / (b * (M * K + K + M))                 # step마다 W 전체를 다시 읽음
    ai_gemm = 2 * M * K * T / (b * (M * K + K * T + M * T))     # W를 T번 재사용
    print(f"arith. intensity {tag}: GEMV {ai_gemv:5.2f} FLOP/B   GEMM {ai_gemm:6.1f} FLOP/B")
```

```text
sequential GEMV x1000           2.35 ms   10.45 GFLOP/s
one GEMM [192x64]x[64x1000]     0.36 ms   68.09 GFLOP/s
arith. intensity fp32: GEMV  0.49 FLOP/B   GEMM   22.9 FLOP/B
arith. intensity int8: GEMV  1.96 FLOP/B   GEMM   91.6 FLOP/B
```

출력에서 볼 것: 같은 연산량인데 GEMM이 약 6.5배 빠르다(이 Mac CPU 기준, 실행마다 수치는 조금 다름). 여기서 `seq`는 순환 계산의 모양만 흉내 낸 것이다(`[:H]`로 잘라 다음 입력으로 씀). 차이의 원인은 두 가지다. (1) 호출 1000번의 고정 오버헤드(가속기에서는 커널 launch·DMA 설정·동기화 비용에 해당), (2) GEMM은 weight를 캐시에 두고 T번 재사용해 intensity가 약 47배 높다. NPU에서는 이 격차가 훨씬 커진다. MAC 배열이 크고 커널 launch 비용이 크기 때문이다. 그래서 NPU용 모델에서 RNN은 대개 CPU/DSP로 fallback되거나, 가속기 활용률이 한 자릿수 %에 머무는 일이 흔하다.

### 11.3 그런데 MCU에서는 오히려 좋다

MCU에는 거대한 MAC 배열이 없다. Cortex-M은 어차피 한 사이클에 MAC 1~몇 개(SIMD)를 하므로 "병렬화를 못 한다"는 약점이 거의 드러나지 않는다. 대신 RNN의 장점이 크게 보인다.

- **O(1) 상태 메모리**: 창 버퍼 대신 H개 원소. 수백 바이트.
- **균일한 부하**: 프레임마다 같은 사이클 수 → 실시간 스케줄링과 전력 예측이 쉽다.
- **지연 최소**: 프레임이 끝나면 바로 출력.
- **작은 weight**: 수~수십 KB로 SRAM/flash에 들어감. 작은 weight라면 GEMV가 memory-bound여도 on-chip SRAM이라 대역폭 문제가 작다.

### 11.4 스트리밍 관점 비교표

| 항목 | RNN (GRU/LSTM) | 1D CNN / TCN (B2) | Transformer (B4) |
|---|---|---|---|
| 새 프레임 1개당 연산 | 일정 (1 step) | 일정 (streaming conv면 창 끝부분만) | 과거 길이에 비례해 증가 (attention) |
| 스트리밍 메모리 | O(1): 상태 H (LSTM은 2H) | O(receptive field): 층별 과거 버퍼 | O(t): KV cache가 계속 늘어남 (창 제한 필요) |
| 볼 수 있는 과거 | 이론상 무한, 실제는 수십~수백 step | receptive field까지 (고정) | context 길이까지 |
| 시간축 병렬성 (학습·배치 추론) | 없음 (순차) | 높음 | 높음 |
| 가속기(NPU/GPU) 적합성 | 낮음: 작은 GEMV, memory-bound | 높음: conv/GEMM | 높음(prefill), decode는 memory-bound |
| MCU 적합성 | 높음 | 높음 | 낮음 (작은 모델 제외) |
| 양자화 이슈 | 오차가 상태에 누적·순환, sigmoid/tanh LUT, 상태 scale 고정 필요 | 층 단위로 끝남, 가장 성숙 | softmax·LayerNorm 민감, 외부 활성화 이상값 |
| 스트리밍 구현 | 상태 carry만 하면 됨 | 층마다 ring buffer 관리 | KV cache 관리 |

말로 하면: 가속기가 있고 창이 짧으면 CNN이, 긴 문맥과 큰 모델이면 Transformer가, **가장 작은 always-on 코어에서 프레임 단위로 도는 모델**이면 RNN이 여전히 강력한 후보다.

---

## 12. 오늘날 어디에 쓰이나

- **KWS / wake word**: ARM의 "Hello Edge"(Zhang et al. 2017)는 Cortex-M에서 DNN, CNN, LSTM, GRU, CRNN, DS-CNN을 같은 메모리·연산 예산으로 비교했다. 결론의 주인공은 DS-CNN이었지만 GRU/CRNN도 경쟁력 있는 결과를 냈다. 실무 KWS는 CNN이 주류이고 RNN은 CRNN(CNN 뒤에 GRU) 형태로 자주 등장한다(B5).
- **노이즈 억제**: RNNoise(Valin, 2018)는 신호처리 특징(대역 에너지 등)을 입력으로 받고 **GRU 층들**로 대역별 gain을 매 프레임 추정하는 하이브리드 구조다. 작은 CPU에서 실시간으로 도는 대표적인 스트리밍 RNN 사례다(층 크기 등 세부는 논문 확인).
- **VAD**: 프레임 단위 on/off 결정이라 작은 GRU/LSTM이 잘 맞는다. 뒤에 hangover FSM을 붙인다.
- **ASR**: RNN-T(RNN Transducer, Graves 2012)는 스트리밍 음성 인식의 표준 구조 중 하나다. Google의 on-device 스트리밍 ASR(He et al. 2019)은 LSTM 기반 encoder와 prediction network를 썼다. 최근에는 encoder를 Conformer로 바꾸고, prediction network를 LSTM이나 더 단순한 구조로 두는 경우가 많다(B5).
- **IMU**: 제스처·활동 인식에서 CNN+LSTM 조합(DeepConvLSTM 등)이 연구에 자주 쓰인다(B7).
- **현대의 부활 — 선형 RNN / SSM**: Mamba 같은 state space model, RWKV, minGRU 같은 모델은 "상태를 들고 다니는 순환 구조"를 유지하면서 순환을 **선형**으로 만들어 학습 때는 parallel scan으로 시간 축 병렬화를, 추론 때는 RNN처럼 O(1) 상태의 스트리밍을 얻는다. 이 노트의 1.1절 "IIR 필터 = 선형 RNN" 관점이 그대로 이어진다. 자세한 건 B9.

---

## 13. 임베디드 관점에서 다시 보기

### 13.1 메모리 예산 (GRU I=40, H=64, 1층 예시)

```
 flash (weight, int8)   : 20,352 B ≈ 20 KB      (fp32면 80 KB)
 SRAM (상태)            : 64 × 1~4 B = 64~256 B  ← 수명: 세션 전체
 SRAM (scratch)         : gate 중간값 3H floats ≈ 768 B  ← 수명: step 1회
 연산                   : ≈ 20 k MAC/step, 100 step/s → ≈ 2 M MAC/s
```

scratch는 step마다 재사용되는 임시 버퍼(TFLM의 arena에 해당)이고, 상태는 step 사이에 살아 있어야 하는 영속 버퍼다. 이 둘을 구분해서 링커 섹션을 나누면, 저전력 모드에서 retention이 필요한 영역이 수백 바이트로 줄어든다.

### 13.2 순환 구조의 양자화 ① — IIR 필터의 deadband가 돌아온다

CNN은 층마다 양자화 오차가 생겨도 다음 입력에서 새로 시작한다. RNN은 오차가 **상태로 되먹임**된다. 고정소수점 IIR 필터를 짜 본 사람이 아는 현상 — **deadband(limit cycle)** — 가 그대로 나타난다. pole 0.97짜리 leaky 상태를 매 step int8로 반올림해 본다.

```python
# 재귀 상태를 int8로 반올림하면? pole 0.97짜리 leaky 상태 h ← a·h + (1−a)·x  (= 1차 IIR)
import numpy as np
a, s = 0.97, 1 / 127                                  # 상태 scale: |h| ≤ 1 을 int8로
x = np.r_[np.ones(100), np.zeros(200)]                # 100프레임 켜졌다가 꺼짐
hf, hq, hist = 0.0, 0, []
for t, xt in enumerate(x):
    hf = a * hf + (1 - a) * xt                        # float 기준
    hq = int(np.round((a * hq * s + (1 - a) * xt) / s))   # 매 step 상태를 int8로 반올림
    hist.append((t, hf, hq * s))
for t, f, q in hist:
    if t in (0, 10, 50, 99, 120, 150, 200, 299):
        print(f"t={t:3d} float={f:.4f} int8={q:.4f} err={q - f:+.4f}")
print("꺼진 뒤 int8 상태가 멈춘 값:", hq, "LSB  (0.5/(1-a) =", round(0.5 / (1 - a), 1), ")")
```

```text
t=  0 float=0.0300 int8=0.0315 err=+0.0015
t= 10 float=0.2847 int8=0.2835 err=-0.0012
t= 50 float=0.7885 int8=0.7717 err=-0.0168
t= 99 float=0.9524 int8=0.8740 err=-0.0784
t=120 float=0.5024 int8=0.4646 err=-0.0378
t=150 float=0.2015 int8=0.1890 err=-0.0125
t=200 float=0.0439 int8=0.1260 err=+0.0821
t=299 float=0.0022 int8=0.1260 err=+0.1238
꺼진 뒤 int8 상태가 멈춘 값: 16 LSB  (0.5/(1-a) = 16.7 )
```

출력에서 볼 것: 입력이 꺼진 뒤 float 상태는 0으로 수렴하지만, int8 상태는 16 LSB(0.126)에서 **영원히 멈춘다**. 한 step 감소량 `(1−a)·h`가 0.5 LSB보다 작아지면 반올림이 그 감소를 지워 버리기 때문이다: `(1 − a)·h < 0.5 LSB` → `h < 0.5/(1 − a) ≈ 16.7 LSB`. 올라갈 때도 같은 이유로 0.874에서 멈췄다. **pole이 1에 가까울수록(= 기억이 길수록) deadband가 넓어진다.** LSTM의 forget gate가 0.99 근처에서 동작하는 채널이 바로 이런 위험 지대다. 그래서 실무에서는:

- 상태(특히 LSTM cell `c`)는 int16으로 두는 경우가 많다(TFLite의 int8 LSTM 양자화 사양도 cell state에 16-bit를 쓴다).
- gate 곱셈 결과의 rounding 방식, 상태 scale을 고정(tanh 출력이면 1/127 등)하는 것을 명시적으로 설계한다.
- 양자화 검증은 **긴 스트림을 프레임 단위로 돌려서** 한다. 1초 창 단위 정확도는 이 문제를 보지 못한다.

### 13.3 순환 구조의 양자화 ② — 학습된 GRU는 어떨까

10절에서 학습한 토글 GRU를 프레임 단위로 400 step 돌리면서, weight와 상태를 fake-quant(A3 8절, C1)로 반올림해 본다. `ex7_defs.py`는 10절 코드의 앞부분(`make`, `GRUNet` 정의까지)만 따로 저장한 파일이다.

```python
# (ex7 다음) 학습된 GRU를 프레임 단위로 돌리며 weight·state를 양자화 — 오차가 상태에 쌓이나
import torch
from ex7_defs import GRUNet, make                          # ex7의 클래스·데이터 함수
torch.manual_seed(123)
def fq(t, bits, amax):                                    # 대칭 fake-quant: round(t/s)·s
    q = 2 ** (bits - 1) - 1; s = amax / q
    return torch.clamp(torch.round(t / s), -q, q) * s

x, y = make(200, 400)
def stream_acc(w_bits=None, h_bits=None):
    g = GRUNet(); g.load_state_dict(torch.load("gru_toggle.pt"))
    if w_bits:
        with torch.no_grad():
            for p in g.parameters(): p.copy_(fq(p, w_bits, p.abs().max()))
    h, hit = None, torch.zeros(400)
    with torch.no_grad():
        for t in range(400):                              # 한 프레임씩, 상태 carry
            logit, h = g(x[:, t:t+1], h)
            if h_bits: h = fq(h, h_bits, 1.0)             # GRU의 |h| ≤ 1 → 고정 scale 1/q
            hit[t] = ((logit[:, 0] > 0).float() == y[:, t]).float().mean()
    return hit[:100].mean().item(), hit[300:].mean().item()

for wb, hb in [(None, None), (8, None), (8, 8), (8, 4), (8, 3), (4, None)]:
    a0, a1 = stream_acc(wb, hb)
    print(f"weight={str(wb or 'fp32'):>4} state={str(hb or 'fp32'):>4}  acc t<100: {a0:.3f}  t>=300: {a1:.3f}")
```

```text
weight=fp32 state=fp32  acc t<100: 0.997  t>=300: 0.899
weight=   8 state=fp32  acc t<100: 0.997  t>=300: 0.894
weight=   8 state=   8  acc t<100: 0.997  t>=300: 0.890
weight=   8 state=   4  acc t<100: 0.993  t>=300: 0.911
weight=   8 state=   3  acc t<100: 0.986  t>=300: 0.893
weight=   4 state=fp32  acc t<100: 0.998  t>=300: 0.897
```

출력에서 볼 것: 이 모델은 양자화에 놀랄 만큼 강하다. 상태를 3-bit까지 줄여도 정확도 변화가 1%p 안팎이고, 그 차이는 seed에 따른 잡음 수준이다. 이유는 13.2와 정반대다. 이 GRU는 ON/OFF 두 개의 **강한 attractor**를 학습했기 때문에, 작은 반올림 오차가 생겨도 다음 step에서 attractor 쪽으로 다시 끌려간다(디지털 latch가 아날로그 잡음을 복원하는 것과 같다). 반대로 13.2처럼 **느린 적분기**(pole ≈ 1) 역할을 하는 채널은 오차가 복원되지 않고 쌓인다.

또 하나: 모든 설정에서 t≥300 구간 정확도가 t<100보다 약 10%p 낮다. 이것은 양자화가 아니라 **길이 일반화**(10절) 문제다. 양자화 영향을 측정할 때 이 둘을 분리하려면, 이렇게 같은 스트림에서 fp32 기준과 나란히 비교해야 한다.

결론: RNN 양자화의 위험은 모델마다 다르다. "int8로 해도 괜찮다/안 된다"를 일반론으로 말하지 말고, **긴 스트림 · 프레임 단위 · fp32 기준과 비교**로 측정해서 말한다.

### 13.4 배포 체크 포인트

- 런타임 op 지원: TFLite/TFLM은 LSTM 전용 fused op(UnidirectionalSequenceLSTM)가 있고, CMSIS-NN에도 int8 LSTM 커널이 있다(버전별로 확인). GRU는 런타임에 따라 전용 op 없이 기본 op들로 풀려 느려질 수 있다. Qualcomm QNN 같은 NPU 스택에서 순환 op가 어느 코어로 가는지(NPU/DSP/CPU fallback)는 반드시 프로파일로 확인한다.
- 입력 투영 선계산: `W_ih·x_t`는 순환과 무관하므로, 오디오 특징 추출 직후 DSP에서 미리 계산하거나 여러 프레임을 묶어 GEMM으로 처리할 수 있다.
- 비선형 함수: sigmoid/tanh는 int8 입력이면 256 엔트리 LUT로 정확히 구현된다(입력이 256가지뿐이므로).
- 초기 상태와 리셋 이벤트를 제품 사양으로 문서화한다.

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 스트리밍 호출마다 상태를 0으로 초기화 | 오프라인 검증은 좋은데 기기 정확도 급락 | 런타임/래퍼가 `h`를 넘기지 않음 | 상태를 그래프 입출력으로 노출, 8.2절 방식의 full-vs-step 비교 테스트 |
| LSTM gate 순서 착오 | 출력이 전혀 안 맞거나 항상 포화 | torch는 i,f,g,o / Keras i,f,c,o / ONNX i,o,f,c | 스칼라 테스트(4.3절)로 gate별 검증 |
| GRU reset gate 위치 착오 | 오차가 작지만 0이 아님(1e−2 수준) | 논문식 `W·(r⊙h)` vs torch식 `r⊙(W·h+b)` | 프레임워크 사양 확인, `reset_after` 옵션 맞추기 |
| torch의 bias 2벌 중 하나 누락 | 전체 출력이 조금씩 틀림 | `b_ih`만 사용 | RNN/LSTM은 합쳐서 fold, GRU의 `b_hn`은 r 안쪽에 유지 |
| bidirectional 모델을 스트리밍 배포 | 구조적으로 불가, 또는 창 단위로 억지 실행해 지연 증가 | 역방향이 미래 입력을 요구 | 단방향으로 재학습, 또는 짧은 lookahead만 허용 |
| clipping 없이 RNN 학습 | loss가 갑자기 튀고 NaN | exploding gradient | `clip_grad_norm_`, lr 낮추기 |
| 짧은 시퀀스로만 학습 후 무한 스트림 배포 | 시간이 갈수록 정확도 저하, 상태 드리프트 | 긴 길이 분포를 학습에서 못 봄 | 긴 시퀀스·truncated BPTT로 학습, 리셋 정책, 긴 스트림 평가 |
| 상태를 int8 반올림 (pole≈1 채널) | 무입력 후에도 출력이 0으로 안 돌아옴 | IIR deadband | 상태 int16, rounding 설계, 긴 스트림 검증 |
| 창 단위로만 양자화 평가 | 실기기에서만 오류 | 누적 오차는 긴 스트림에서만 드러남 | 13.3절처럼 프레임 단위 장기 평가 |
| NPU에 RNN을 올리고 성능 기대 | 가속기 활용률 낮음, CPU fallback | 작은 GEMV, 순차 의존 | 입력 투영 분리, CNN/TCN 대안 검토, 프로파일 |

---

## 15. 면접에서 이렇게 말한다

**Q.** "LSTM vs GRU — what's the difference and which would you pick for an embedded device?"

**A.** LSTM은 cell state c와 hidden h 두 상태를 갖고 input·forget·output gate와 후보 g, 즉 4개 블록을 쓴다. GRU는 상태 h 하나에 reset·update gate와 후보, 3개 블록이다. 그래서 GRU가 파라미터와 step당 MAC이 25% 적고 상태 메모리가 절반이다. 정확도는 과제마다 비슷한 경우가 많아서 임베디드에서는 GRU로 시작하되, 타깃 런타임에 LSTM만 fused 커널이 있으면 LSTM이 실제로 더 빠를 수 있으니 op 지원과 프로파일로 결정한다.

> LSTM keeps two states — a cell state and a hidden state — and uses four blocks: input, forget and output gates plus a candidate. GRU keeps one state and uses three: reset gate, update gate and candidate. So GRU has about 25% fewer parameters and MACs per step and half the state memory. Accuracy is usually comparable, so on a small device I'd start with GRU — but I'd check the target runtime, because if only LSTM has an optimized fused kernel, LSTM can end up faster in practice. I'd decide with a profile, not by rule.

**Q.** "How do you run an RNN in streaming mode on an MCU?"

**A.** 프레임이 들어올 때마다 cell을 한 step 돌리고, 상태 (h, 그리고 LSTM이면 c)를 호출 사이에 유지한다. 상태 버퍼는 호출자가 소유하게 해서 여러 스트림을 처리하고 저전력 때 retention RAM에 남긴다. 먼저 전체 시퀀스를 한 번에 돌린 결과와 프레임 단위로 상태를 넘긴 결과가 float 오차 수준으로 같은지 테스트한다. 그다음 리셋 정책, 프레임 누락 처리, 양자화 후 긴 스트림 평가를 설계한다. 메모리는 weight 몇십 KB에 상태 수백 바이트, 부하는 프레임마다 일정하다.

> Process one frame per step and carry the state — h, plus c for an LSTM — between calls. The caller owns the state buffer, so the same weights can serve several streams and only a few hundred bytes need retention in low-power mode. My first test is that running the whole sequence at once and running frame by frame with carried state match to float precision. Then I define the reset policy, what to do on a dropped frame, and I evaluate the quantized model on long continuous streams, because recurrent quantization error only shows up over time.

**Q.** "Why are RNNs hard to accelerate on NPUs?"

**A.** 두 가지다. 첫째 순차 의존성: h_t가 h_{t−1}에 의존해서 시간 축 병렬화가 안 되고, 매 step 작은 커널을 호출하는 오버헤드가 쌓인다. 둘째 순환 부분은 벡터 하나에 대한 GEMV라 weight 1바이트당 연산이 int8에서 약 2 FLOP뿐이다. 즉 arithmetic intensity가 낮아 memory-bound이고, 큰 MAC 배열이 놀게 된다. CNN이나 Transformer의 prefill은 시간 축 전체를 GEMM으로 묶어 weight를 재사용한다. 완화책은 입력 투영을 시간 축으로 묶어 선계산하고, 배치를 늘리거나, CNN/TCN이나 선형 RNN 계열로 바꾸는 것이다.

> Two reasons. First, the sequential dependency: h_t needs h_{t−1}, so you can't parallelize across time and you pay per-step kernel launch and sync overhead. Second, the recurrent part is a matrix-vector product on a single state vector, so each weight byte is used once — about two ops per byte in int8. That's deeply memory-bound, and a big MAC array sits idle. CNNs and transformer prefill batch the whole time axis into a GEMM and reuse weights. Mitigations: precompute the input projection for many frames as one GEMM, batch multiple streams, or switch to a TCN or a linear-recurrence model that supports a parallel scan.

**Q.** "What's the parameter count of an LSTM with input size 40 and hidden size 64?"

**A.** gate 블록 하나가 W_x 64×40 = 2560, W_h 64×64 = 4096, bias 64라서 6720이고, 블록이 4개라 26,880이다. PyTorch는 bias를 두 벌 두므로 64×4 = 256을 더해 27,136이다. step당 MAC도 약 26.6k다.

> Each gate block has a 64-by-40 input matrix, a 64-by-64 recurrent matrix and a 64 bias: 2,560 plus 4,096 plus 64 is 6,720. Four blocks gives 26,880. PyTorch stores two bias vectors, so it reports 27,136. Roughly the same number of MACs per time step, about 26.6 thousand.

**Q.** "Why do vanilla RNNs have vanishing gradients, and how do LSTMs fix it?"

**A.** BPTT에서 t step 전으로 가는 gradient는 Jacobian diag(tanh′)·W_h를 t번 곱한 것이라 대략 특이값의 t제곱으로 줄거나 커진다. tanh′ ≤ 1이라 보통은 줄어든다. LSTM은 cell state를 곱셈 하나(forget gate)와 덧셈 하나로만 갱신해서, c_t에서 c_{t−1}로 가는 미분이 f_t 자체다. f가 1에 가까우면 gradient가 거의 그대로 흐른다. exploding에는 gradient clipping을 쓴다.

> In backprop through time, the gradient to a state t steps back is a product of t Jacobians, each tanh-prime times the same recurrent matrix, so it scales roughly like the singular value to the power t — and since tanh-prime is at most one, it usually vanishes. The LSTM cell state is updated only by an elementwise multiply with the forget gate and an add, so the derivative from c_t to c_{t−1} is just f_t. When the forget gate is near one, gradient flows almost unchanged. Exploding gradients are handled separately with gradient clipping.

---

## 16. 직접 해보기

1. **손계산**: 2.3절 RNN에서 입력을 `x = 0, 1, 0`으로 바꿨을 때 h₁, h₂를 구하라.
정답: h₁ = tanh([0, 0.1]) = [0, 0.0997], a₂ = [0.5, −0.3] + W_h·h₁ + b = [0.5 − 0.0199, −0.3 + 0.0598 + 0.1] = [0.4801, −0.1402] → h₂ ≈ [0.4463, −0.1393]. 코드로 검산할 것.

2. **손계산**: 입력 16, hidden 32인 GRU의 파라미터 수(bias 1벌)와 torch 파라미터 수, fp32 상태 바이트 수를 구하라. 같은 크기 LSTM이면?
정답: 블록 = 32·16 + 32·32 + 32 = 1568. GRU 3·1568 = 4704 (torch 4800), 상태 128 B. LSTM 4·1568 = 6272 (torch 6400), 상태 (h, c) 256 B.

3. **손계산**: 4.3절 스칼라 LSTM에서 입력이 계속 0이면 c가 처음 값의 절반이 되는 데 몇 step이 걸리나? forget bias를 4로 바꾸면?
정답: f = σ(2) ≈ 0.881 → n = ln 0.5 / ln 0.881 ≈ 5.5 step. f = σ(4) ≈ 0.982 → 약 38 step. bias가 "기억 시간 상수"를 정한다.

4. **코드**: 10절 학습 코드에서 `clip_grad_norm_`을 지우고 seed 0~4로 GRU를 학습해 T=80 정확도를 비교하라. 몇 개 seed에서 학습이 망가지는가?
힌트: loss를 50 step마다 출력해서 튀는 지점을 찾아라. 결과는 seed에 따라 다르므로 "평균과 최악값"으로 보고한다.

5. **코드**: 9절 C 코드를 int8 weight(per-tensor 대칭 scale) + float 누산으로 바꾸고, torch float 결과와의 최대 차이를 출력하라. 상태 h는 float로 둔다.
힌트: `W_q[i] = round(W[i]/s)`, 누산 시 `acc += (float)W_q[i] · v[k]`, 행 끝에서 `acc·s`. 오차는 1e−2 수준이 예상되지만 직접 재서 확인한다.

6. **코드**: 13.2절 코드에서 `a`를 0.9, 0.97, 0.99로 바꾸며 deadband 값(멈춘 LSB)을 표로 만들고, `0.5/(1−a)` 예측과 비교하라. 상태를 int16(scale 1/32767)으로 바꾸면 어떻게 되나?
힌트: 실측은 4, 16, 50 LSB(경계 `0.5/(1−a)` = 5, 16.7, 50 — 정확히 0.5가 되는 경계에서는 numpy의 round-half-to-even 때문에 1 LSB 차이가 날 수 있다). int16이면 같은 LSB 수라도 실제 값이 256배 작다.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| RNN (recurrent neural network) | 순환 신경망 | 상태를 다음 step으로 되먹이는 네트워크. 학습된 상태 전이 |
| hidden state `h` | 은닉 상태 | 과거 입력의 요약. IIR의 delay 레지스터 |
| BPTT | backpropagation through time | 펼친 그래프에 backprop, step별 gradient 합산 |
| truncated BPTT | 잘린 BPTT | 긴 스트림을 조각으로 학습, 상태는 넘기고 gradient는 끊음 |
| vanishing / exploding gradient | gradient 소실 / 폭주 | 같은 Jacobian의 반복 곱 → 지수적 감소·증가 |
| gradient clipping | gradient norm 상한 | exploding 대책, RNN 학습의 기본 |
| LSTM | Long Short-Term Memory | cell state + gate 4개 블록 |
| cell state `c` | LSTM의 메모리 레지스터 | 곱 1번 + 덧셈 1번만 거쳐 gradient가 잘 흐름 |
| gate | 0~1 밸브 | sigmoid 출력으로 정보 흐름의 비율을 조절 |
| GRU | Gated Recurrent Unit | 상태 하나, reset·update gate + 후보, 3개 블록 |
| update gate `z` | GRU의 보간 비율 | 옛 상태 유지(1) vs 새 후보(0). 적응형 EMA |
| reset gate `r` | GRU의 과거 참조 비율 | 후보를 만들 때 옛 상태를 얼마나 볼지 |
| bidirectional RNN | 양방향 RNN | 역방향이 미래를 봄 → 스트리밍 불가 |
| stateful / streaming inference | 상태 유지 추론 | 프레임마다 1 step, 상태를 호출 사이에 carry |
| GEMV / GEMM | 행렬-벡터 곱 / 행렬-행렬 곱 | GEMV는 weight 재사용이 없어 memory-bound |
| arithmetic intensity | 연산 / 메모리 바이트 | 낮으면 memory-bound (D3 roofline) |
| attractor | 끌개, 안정점 | 상태가 모여드는 점. 학습된 latch의 ON/OFF |
| deadband (limit cycle) | 양자화 불감대 | 반올림 때문에 재귀 상태가 목표에 도달하지 못하고 멈춤 |
| RNN-T | RNN Transducer | 스트리밍 ASR 구조. encoder + prediction network + joint |
| SSM / linear RNN | 상태공간 모델 / 선형 순환 | 학습은 병렬 scan, 추론은 O(1) 상태 (B9) |

---

## 18. 요약 & 체크리스트

RNN은 "상태 ← f(상태, 입력)"을 학습하는 모델이다. vanilla RNN은 비선형 상태공간 모델 `h_t = tanh(W_x·x_t + W_h·h_{t−1} + b)`이고, 모든 step이 같은 weight를 쓴다. 학습은 시간 축으로 펼친 그래프에 backprop(BPTT)을 하는 것인데, 같은 Jacobian을 반복해서 곱하기 때문에 먼 과거로 가는 gradient가 지수적으로 사라지거나 폭발한다. LSTM은 곱 1번 + 덧셈 1번으로만 갱신되는 cell state와 forget·input·output gate로 이것을 구조적으로 해결하고, GRU는 상태 하나와 update·reset gate로 같은 일을 25% 적은 비용으로 한다. 파라미터는 각각 RNN 블록(`H·I + H·H + H`)의 4배, 3배이고, torch는 bias를 두 벌 둔다. 단방향 RNN은 상태만 넘기면 프레임 단위로 잘라 돌려도 결과가 같아서 O(1) 메모리·일정한 부하·최소 지연의 스트리밍 추론에 이상적이고, 그래서 MCU급 always-on 코어에서 강하다. 반대로 순차 의존성과 작은 GEMV(낮은 arithmetic intensity) 때문에 NPU/GPU에서는 활용률이 낮다. 양자화 오차는 상태로 되먹임되므로, pole이 1에 가까운 채널은 IIR deadband 같은 누적 문제를 보이고, 강한 attractor를 가진 채널은 오히려 튼튼하다 — 그러니 긴 스트림에서 프레임 단위로 측정해야 한다.

- [ ] vanilla RNN 식을 쓰고 2차원 상태로 3 step을 손으로 계산할 수 있다
- [ ] RNN과 IIR 필터·FSM의 대응 관계를 설명할 수 있다
- [ ] vanishing/exploding gradient를 "Jacobian의 t제곱"으로 설명하고 clipping의 역할을 말할 수 있다
- [ ] LSTM 식 6줄과 gate 순서(torch: i, f, g, o)를 쓰고 스칼라 예제를 손으로 계산할 수 있다
- [ ] GRU 식(torch 방식의 reset gate 위치 포함)과 update gate의 EMA 해석을 설명할 수 있다
- [ ] LSTM/GRU 파라미터 수와 step당 MAC을 암산하고 torch의 bias 2벌을 설명할 수 있다
- [ ] 전체 시퀀스 추론과 프레임 단위 stateful 추론이 같음을 코드로 검증할 수 있다
- [ ] torch weight를 내보내 C로 GRU step을 구현하고 golden 출력과 맞출 수 있다
- [ ] RNN이 NPU에서 불리하고 MCU에서 유리한 이유를 arithmetic intensity와 상태 메모리로 설명할 수 있다
- [ ] 순환 상태 양자화의 deadband 위험과 긴 스트림 검증의 필요성을 설명할 수 있다

---

## 참고 자료

- Goodfellow, Bengio, Courville, **Deep Learning** (MIT Press, 2016) — 10장 "Sequence Modeling: Recurrent and Recursive Nets". [deeplearningbook.org](https://www.deeplearningbook.org/)
- **Dive into Deep Learning** (d2l.ai) — "Recurrent Neural Networks", "Modern Recurrent Neural Networks" 장(LSTM, GRU, BPTT, deep/bidirectional RNN). [d2l.ai](https://d2l.ai/)
- Christopher Olah, "Understanding LSTM Networks" (2015) — gate 그림의 고전. [colah.github.io/posts/2015-08-Understanding-LSTMs](https://colah.github.io/posts/2015-08-Understanding-LSTMs/)
- Andrej Karpathy, "The Unreasonable Effectiveness of Recurrent Neural Networks" (2015). [karpathy.github.io/2015/05/21/rnn-effectiveness](https://karpathy.github.io/2015/05/21/rnn-effectiveness/)
- PyTorch 문서 — [nn.RNN](https://pytorch.org/docs/stable/generated/torch.nn.RNN.html), [nn.LSTM](https://pytorch.org/docs/stable/generated/torch.nn.LSTM.html), [nn.GRU](https://pytorch.org/docs/stable/generated/torch.nn.GRU.html) (식과 weight 배치 순서)
- Hochreiter & Schmidhuber, "Long Short-Term Memory", Neural Computation 9(8), 1997
- Gers, Schmidhuber, Cummins, "Learning to Forget: Continual Prediction with LSTM", Neural Computation, 2000 — forget gate
- Cho et al., "Learning Phrase Representations using RNN Encoder–Decoder for Statistical Machine Translation" (EMNLP 2014) — GRU. [arXiv:1406.1078](https://arxiv.org/abs/1406.1078)
- Chung et al., "Empirical Evaluation of Gated Recurrent Neural Networks on Sequence Modeling" (2014). [arXiv:1412.3555](https://arxiv.org/abs/1412.3555)
- Pascanu, Mikolov, Bengio, "On the difficulty of training recurrent neural networks" (ICML 2013) — gradient clipping. [arXiv:1211.5063](https://arxiv.org/abs/1211.5063)
- Jozefowicz, Zaremba, Sutskever, "An Empirical Exploration of Recurrent Network Architectures" (ICML 2015) — forget bias 초기화
- Zhang et al., "Hello Edge: Keyword Spotting on Microcontrollers" (2017). [arXiv:1711.07128](https://arxiv.org/abs/1711.07128)
- Valin, "A Hybrid DSP/Deep Learning Approach to Real-Time Full-Band Speech Enhancement" (MMSP 2018) — RNNoise. [arXiv:1709.08243](https://arxiv.org/abs/1709.08243)
- Graves, "Sequence Transduction with Recurrent Neural Networks" (2012) — RNN-T. [arXiv:1211.3711](https://arxiv.org/abs/1211.3711)
- He et al., "Streaming End-to-end Speech Recognition for Mobile Devices" (ICASSP 2019). [arXiv:1811.06621](https://arxiv.org/abs/1811.06621)
- Gu & Dao, "Mamba: Linear-Time Sequence Modeling with Selective State Spaces" (2023) — B9 예고. [arXiv:2312.00752](https://arxiv.org/abs/2312.00752)
