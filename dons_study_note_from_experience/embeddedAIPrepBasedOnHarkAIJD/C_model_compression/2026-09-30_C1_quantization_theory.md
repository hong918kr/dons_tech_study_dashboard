# C1. 양자화 이론 — scale·zero-point, 정밀도 포맷, requantization, calibration

> **이 노트를 다 읽으면**: 실수 범위 [min, max]에서 scale·zero-point를 손으로 유도하고 양자화 오차·SQNR을 예측할 수 있다 · 대칭/비대칭, per-tensor/per-channel/per-group 중 무엇을 왜 고르는지 숫자로 설명할 수 있다 · int8 dense 층이 int32 누산 → 고정소수점 multiplier(M0, shift) → int8 출력으로 가는 requantization을 유도하고 C로 bit-exact하게 구현할 수 있다 · min-max·percentile·MSE·KL calibration의 clipping/rounding trade-off를 설명할 수 있다
> **JD 연결**: "Optimizing models for MCUs & edge processors (Cortex-M/A, RISC-V, DSP)", "Co-design model architectures that meet latency, memory, power, bandwidth" — study_prep_list **C1**: scale·zero-point, symmetric vs asymmetric / per-tensor·per-channel·**per-group** / INT8·INT4·FP16·BF16·FP8 / **requantization 수식 (고정소수점 multiplier + shift)** / calibration: min-max, percentile, KL. 최적화 질문 1순위(P0).
> **Don 기준 난이도**: Q15/Q7 고정소수점, 포화 연산, 비트 packing, 반올림 시프트는 이미 손에 익은 것 / scale이 2의 거듭제곱이 아닌 "임의 실수"일 때의 처리, zero-point 보정항, calibration이라는 통계적 범위 결정은 새로 배움
> **선행 노트**: A1 (행렬곱·dense layer·숫자 타입 미리보기), A2 (uniform 분포 분산 Δ²/12, KL divergence), B1 (ReLU6·fused activation·BN folding)

---

## 0. 큰 그림 — 이게 왜 필요한가

학습은 float32로 한다. 그런데 Hark 같은 웨어러블(추정)의 always-on MCU나 Hexagon 같은 DSP/NPU는 **정수 기계**다. float 모델을 그대로 올리면 메모리가 4배, 대역폭이 4배, MAC 에너지는 한 자릿수 이상 더 든다. 그래서 배포 파이프라인 한가운데에 **양자화(quantization)** — 실수 텐서를 "정수 + 몇 개의 실수 파라미터"로 바꾸는 과정 — 가 들어간다.

```
 [학습: float32]                 [변환기 / 오프라인]                         [기기: 정수만]
 PyTorch 모델 ──► BN folding(B1) ──► calibration 데이터로 activation 범위 측정 ──► int8 weight + int32 bias
                                    (min-max / percentile / MSE / KL, 9절)          + per-channel (M0, shift)
                                   weight 범위 → per-channel scale (5절)            + 각 텐서의 (scale, zero_point)
                                                                                           │
                                                                                           ▼
                                                        int8 x ─► int32 MAC ─► requantize ─► int8 y  (7절)
```

이 노트(C1)는 위 그림의 **이론과 산수**를 다룬다. 절차와 도구는 다른 노트로 나눴다.

| 노트 | 다루는 것 |
|---|---|
| **C1 (이 노트)** | scale·zero-point 수식, 오차·SQNR, 포맷, 정수 전용 산술(requantization), calibration 알고리즘 |
| C2 | PTQ vs QAT 절차, fake quant, STE, layer별 정확도 디버깅 |
| C3 | LLM 양자화 (W4A16, SmoothQuant, GPTQ, AWQ, KV-cache) |
| C8 | 최적화 후 검증 (golden reference, bit-exact vs tolerance, layer별 SQNR) |

Don에게 좋은 소식: 이 주제의 절반은 이미 안다. **Q15는 "scale = 2⁻¹⁵, zero-point = 0인 대칭 양자화"**다. 새로운 건 두 가지뿐이다.

1. scale이 2의 거듭제곱이 아니라 **임의의 실수**다 → 곱셈 한 번 + 시프트로 흉내 낸다 (7절의 M0·2⁻ⁿ).
2. scale을 **데이터 통계로 정한다** → calibration (9절). 펌웨어에서 ADC full-scale을 신호 크기에 맞춰 gain을 정하던 것과 같은 문제다.

---

## 1. 왜 양자화하나 — 메모리·대역폭·에너지·정수 기계

### 1.1 메모리와 대역폭: 4배

weight 한 개가 float32면 4바이트, int8이면 1바이트, int4면 0.5바이트다. 파라미터 수 P인 모델의 weight 크기는 그냥 `P × 바이트/원소`다.

| 모델 (가정) | 파라미터 | float32 | float16/bf16 | int8 | int4 (+group scale) |
|---|---|---|---|---|---|
| IMU 제스처 MLP (10절) | 3.3 K | 13.2 KB | 6.6 KB | 3.3 KB (+scale) | — |
| 키워드 스포팅 소형 CNN (예: 30 K 가정) | 30 K | 120 KB | 60 KB | 30 KB | — |
| 1B 파라미터 소형 LLM | 1 × 10⁹ | 4 GB | 2 GB | 1 GB | 약 0.56 GB (4.5 bit/weight, 5.3절) |

Cortex-M 계열 MCU의 SRAM이 수백 KB, flash가 1~2 MB 수준인 경우가 많다는 걸 생각하면, 4배는 "들어가느냐 마느냐"의 차이다. LLM decode처럼 매 토큰마다 weight 전체를 DRAM에서 읽는 워크로드는 **속도가 거의 바이트 수에 반비례**한다 (D 모듈의 대역폭 식). int4면 fp16보다 약 4배 빠를 여지가 생긴다.

### 1.2 에너지: 정수 MAC이 훨씬 싸다

Horowitz(ISSCC 2014, 45nm 공정)의 유명한 표가 자주 인용된다. 공정마다 절대값은 다르니 **비율**만 기억하자.

| 연산 (45nm, 대략) | 에너지 |
|---|---|
| 8-bit int add | 0.03 pJ |
| 32-bit int add | 0.1 pJ |
| 32-bit float add | 0.9 pJ |
| 8-bit int mult | 0.2 pJ |
| 32-bit int mult | 3.1 pJ |
| 16-bit float mult | 1.1 pJ |
| 32-bit float mult | 3.7 pJ |
| 32-bit SRAM read (작은 8 KB) | 약 5 pJ |
| 32-bit DRAM read | 약 640 pJ |

말로 하면: int8 MAC(곱 0.2 + 덧셈 0.03)은 fp32 MAC(3.7 + 0.9)보다 약 20배 싸고, **DRAM 한 번 읽기는 그 MAC 수천 번**이다. 그래서 양자화는 연산 에너지보다도 "메모리 이동 바이트를 줄이는 것"이 더 큰 이득인 경우가 많다. 실리콘 면적도 비슷한 경향이다 — 같은 면적에 int8 MAC을 훨씬 많이 넣을 수 있어서 NPU의 TOPS 스펙은 거의 항상 int8 기준으로 적힌다.

### 1.3 NPU·DSP는 정수 기계다

- Cortex-M4/M7/M33 같은 DSP 확장 코어: 16-bit 두 개를 한 번에 곱해 32-bit에 누산하는 SIMD MAC(`SMLAD`)이 있다. int8은 16-bit로 넓혀서 이걸 쓴다.
- Cortex-M55/M85(Helium, MVE): 128-bit 벡터로 int8 MAC을 여러 개 동시에 한다.
- 많은 엣지 NPU: int8(일부는 int16 activation, int4 weight)을 주 데이터 타입으로 설계된다. float 지원은 없거나, 있어도 느리거나 세대별로 다르다.

그래서 edge ML 엔지니어의 질문은 "양자화를 할까?"가 아니라 "**어떻게 해야 정확도를 잃지 않을까?**"다.

### 1.4 Don이 이미 아는 것: Q-format = 2의 거듭제곱 scale 양자화

Q15에서 int16 값 `q`는 실수 `q × 2⁻¹⁵`를 뜻한다. 이 노트의 표기로 쓰면:

```
Q15:  x = s · (q − z),   s = 2⁻¹⁵,  z = 0,  q ∈ [−32768, 32767]   → x ∈ [−1, 1)
Q7 :  x = s · (q − z),   s = 2⁻⁷ ,  z = 0,  q ∈ [−128, 127]       → x ∈ [−1, 1)
일반: x = s · (q − z),   s = 아무 양의 실수,  z = 정수
```

말로 하면: Q-format은 scale을 2의 거듭제곱으로 **고정**해서, 두 Q 값을 곱한 뒤 `>> 15` 한 번으로 원래 포맷에 돌아올 수 있게 한 것이다. ML 양자화는 텐서마다 값의 범위가 제각각이라 scale을 2의 거듭제곱으로 묶어 두면 해상도를 최대 2배 가까이 버린다(범위가 2^k 경계에 딱 맞지 않으므로). 그래서 임의의 실수 scale을 쓰고, 그 대가로 "곱셈 + 시프트" requantization(7절)을 한다. 실제로 초기 CMSIS-NN은 q7/q15 함수에 bias shift·output shift만 받는 **2의 거듭제곱 scale** 방식이었고, 이후 TFLite 방식의 임의 scale(multiplier + shift) int8 커널이 주류가 되었다.

---

## 2. Affine 양자화 — `q = round(x/s) + z`

### 2.1 직관: 자와 눈금

실수 축에 눈금 간격 `s`(scale)인 자를 댄다. 실수 0이 자의 몇 번째 눈금에 오는지가 `z`(zero-point)다. 실수 x를 가장 가까운 눈금 번호로 바꾸는 것이 양자화, 눈금 번호를 다시 실수로 읽는 것이 역양자화(dequantization)다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 235">
<line x1="150" y1="60" x2="530" y2="60" stroke="currentColor"/> <line x1="60" y1="160" x2="620" y2="160" stroke="currentColor"/> <text x="10" y="64" font-size="13">실수 x</text> <text x="10" y="164" font-size="13">정수 q</text> <line x1="150.0" y1="55" x2="150.0" y2="65" stroke="currentColor"/> <text x="150.0" y="45" font-size="12" text-anchor="middle">-1</text> <line x1="150.0" y1="62" x2="60.0" y2="158" stroke="#4a7bd0" stroke-width="1.5"/> <line x1="60.0" y1="155" x2="60.0" y2="165" stroke="currentColor"/> <text x="60.0" y="182" font-size="12" text-anchor="middle">0</text> <line x1="245.0" y1="55" x2="245.0" y2="65" stroke="currentColor"/> <text x="245.0" y="45" font-size="12" text-anchor="middle">0</text> <line x1="245.0" y1="62" x2="200.5" y2="158" stroke="#d0564a" stroke-width="1.5"/> <line x1="200.5" y1="155" x2="200.5" y2="165" stroke="currentColor"/> <text x="200.5" y="182" font-size="12" text-anchor="middle">64</text>
<line x1="292.5" y1="55" x2="292.5" y2="65" stroke="currentColor"/> <text x="292.5" y="45" font-size="12" text-anchor="middle">0.5</text> <line x1="292.5" y1="62" x2="270.8" y2="158" stroke="#4a7bd0" stroke-width="1.5"/> <line x1="270.8" y1="155" x2="270.8" y2="165" stroke="currentColor"/> <text x="270.8" y="182" font-size="12" text-anchor="middle">96</text> <line x1="340.0" y1="55" x2="340.0" y2="65" stroke="currentColor"/> <text x="340.0" y="45" font-size="12" text-anchor="middle">1</text> <line x1="340.0" y1="62" x2="341.1" y2="158" stroke="#4a7bd0" stroke-width="1.5"/> <line x1="341.1" y1="155" x2="341.1" y2="165" stroke="currentColor"/> <text x="341.1" y="182" font-size="12" text-anchor="middle">128</text> <line x1="361.9" y1="55" x2="361.9" y2="65" stroke="currentColor"/> <text x="361.9" y="45" font-size="12" text-anchor="middle">1.23</text> <line x1="361.9" y1="62" x2="371.8" y2="158" stroke="#4a7bd0" stroke-width="1.5"/>
<line x1="371.8" y1="155" x2="371.8" y2="165" stroke="currentColor"/> <text x="371.8" y="182" font-size="12" text-anchor="middle">142</text> <line x1="435.0" y1="55" x2="435.0" y2="65" stroke="currentColor"/> <text x="435.0" y="45" font-size="12" text-anchor="middle">2</text> <line x1="435.0" y1="62" x2="481.6" y2="158" stroke="#4a7bd0" stroke-width="1.5"/> <line x1="481.6" y1="155" x2="481.6" y2="165" stroke="currentColor"/> <text x="481.6" y="182" font-size="12" text-anchor="middle">192</text> <line x1="530.0" y1="55" x2="530.0" y2="65" stroke="currentColor"/> <text x="530.0" y="45" font-size="12" text-anchor="middle">3</text> <line x1="530.0" y1="62" x2="620.0" y2="158" stroke="#4a7bd0" stroke-width="1.5"/> <line x1="620.0" y1="155" x2="620.0" y2="165" stroke="currentColor"/> <text x="620.0" y="182" font-size="12" text-anchor="middle">255</text> <text x="190.5" y="120" font-size="12" text-anchor="end">실수 0 ↔ q = z = 64 (정확히)</text>
<text x="540" y="64" font-size="12">× 1/s, + z</text> <text x="60" y="205" font-size="12">s = 4/255 ≈ 0.01569, z = 64 — 정수 격자 한 칸 = 실수 s 만큼</text> <text x="60" y="223" font-size="12">x = −1은 q = 0이지만 x̂ = −1.0039 (z를 정수로 반올림하면서 격자가 살짝 이동)</text>
</svg>
```

그림 1 — uint8 affine 양자화 (범위 [−1, 3]). 위 실수 축의 점이 `x/s + z`로 아래 정수 축에 옮겨진다. 실수 0(빨강)은 정확히 정수 64에 떨어진다. 변환이 affine(1차 함수)이라 선들이 한 점에서 모이는 원근 모양이 된다.

### 2.2 정의

```
양자화    q = clamp( round(x / s) + z,  q_min, q_max )
역양자화  x̂ = s · (q − z)
```

- `s` (scale): 정수 1 차이가 실수로 얼마인가. 양의 실수. "1 LSB의 크기".
- `z` (zero-point): 실수 0에 대응하는 정수. **반드시 정수**다 (4.3절에서 이유).
- `[q_min, q_max]`: uint8이면 [0, 255], int8이면 [−128, 127] (대칭 weight는 [−127, 127], 4.2절).
- `round`: 가장 가까운 정수. 동점(.5) 처리 규칙은 구현마다 다르다 — 7.5절에서 bit-exact 문제로 다시 나온다.

말로 하면: 실수를 s로 나눠 "눈금 몇 칸인지" 세고, 0점이 z에 오도록 평행이동한 뒤, 정수 타입 범위로 자른다.

### 2.3 [min, max]에서 s와 z 유도

실수 범위 [x_min, x_max]를 정수 범위 [q_min, q_max]에 딱 맞추고 싶다. 두 끝점이 서로 대응한다는 조건:

```
x_min = s · (q_min − z)
x_max = s · (q_max − z)
────────────────────────  두 식을 빼면
x_max − x_min = s · (q_max − q_min)
⇒  s = (x_max − x_min) / (q_max − q_min)
⇒  z = q_min − x_min / s          (첫 식을 z에 대해 풀기)  →  정수로 round, [q_min, q_max]로 clamp
```

그리고 한 가지 전처리: **범위가 0을 포함하도록** `x_min ← min(x_min, 0)`, `x_max ← max(x_max, 0)`로 넓힌다. 그래야 z가 정수 범위 안에 들어와 0을 표현할 수 있다.

### 2.4 손계산: 범위 [−1, 3] → uint8

```
s = (3 − (−1)) / (255 − 0) = 4/255 ≈ 0.015686
z = 0 − (−1)/s = 255/4 = 63.75  → round → 64

x = 0.5  : 0.5/s = 31.875 → 32,  q = 32 + 64 = 96,   x̂ = s·(96 − 64) = 0.50196   (오차 +0.00196)
x = 1.23 : 1.23/s = 78.41 → 78,  q = 142,            x̂ = s·78 = 1.22353          (오차 −0.00647)
x = 5.0  : 5.0/s = 318.75 → 319, q = 383 → clamp 255, x̂ = s·191 = 2.99608        (오차 −2.004, 포화!)
x = −1.0 : −63.75 → −64,         q = 0,              x̂ = s·(−64) = −1.00392       (z를 반올림해서 격자가 0.25칸 밀림)
```

int8(−128..127)로 하면 s는 같고 z만 128만큼 내려간다: `z = −128 − (−1)/s = −64.25 → −64`. 즉 uint8과 int8 비대칭은 **z가 128 차이 나는 같은 것**이다.

### 2.5 코드로 확인

아래 코드는 2.3의 공식을 그대로 구현하고 2.4의 손계산을 재현한다.

```python
import numpy as np

def qparams_asym(xmin, xmax, qmin=0, qmax=255):
    xmin, xmax = min(xmin, 0.0), max(xmax, 0.0)   # 범위가 0을 반드시 포함하게
    s = (xmax - xmin) / (qmax - qmin)
    z = int(np.clip(round(qmin - xmin / s), qmin, qmax))  # 정수로 반올림!
    return s, z

def quantize(x, s, z, qmin, qmax):
    return np.clip(np.round(x / s) + z, qmin, qmax).astype(np.int32)

def dequantize(q, s, z):
    return s * (q.astype(np.float64) - z)

s, z = qparams_asym(-1.0, 3.0)                    # uint8
print(f"s = {s:.6f}, z = {z}")
x = np.array([-1.0, 0.0, 0.5, 1.23, 3.0, 5.0])
q = quantize(x, s, z, 0, 255)
xh = dequantize(q, s, z)
for a, b, c in zip(x, q, xh):
    print(f"x={a:5.2f} -> q={b:3d} -> x_hat={c:8.5f}  err={c-a:+.5f}")
print("max |err| for in-range values:", np.abs(xh - x)[:5].max(), " s/2 =", s / 2)
```

```text
s = 0.015686, z = 64
x=-1.00 -> q=  0 -> x_hat=-1.00392  err=-0.00392
x= 0.00 -> q= 64 -> x_hat= 0.00000  err=+0.00000
x= 0.50 -> q= 96 -> x_hat= 0.50196  err=+0.00196
x= 1.23 -> q=142 -> x_hat= 1.22353  err=-0.00647
x= 3.00 -> q=255 -> x_hat= 2.99608  err=-0.00392
x= 5.00 -> q=255 -> x_hat= 2.99608  err=-2.00392
max |err| for in-range values: 0.006470588235294006  s/2 = 0.00784313725490196
```

출력에서 볼 것: 범위 안의 값은 오차가 `s/2 = 0.00784` 이하다(최대 0.00647). 범위 밖의 5.0은 255에 포화되어 오차가 2.0 — **clipping 오차는 rounding 오차와 차원이 다르게 크다.** 이 두 오차의 줄다리기가 9절 calibration의 전부다. 그리고 0.0은 정확히 0으로 돌아온다.

### 2.6 함정

- `z`를 float로 두고 반올림하지 않으면 0이 정확히 표현되지 않는다 (4.3절).
- 범위가 한쪽으로만 있는 텐서(예: 전부 양수 [2, 5])에 0 포함 규칙을 잊으면 z가 범위 밖으로 나가 clamp되면서 이상한 값이 된다. 범위를 [0, 5]로 넓혀야 한다 (해상도는 조금 손해).
- `x_max == x_min`(상수 텐서, 전부 0인 채널)이면 s = 0 → 0으로 나누기. 실제 도구는 작은 epsilon을 최소 scale로 둔다.

---

## 3. 양자화 오차와 SQNR — "비트 하나에 6 dB"

### 3.1 오차 모델: uniform noise

범위 안(clip되지 않은) 값의 양자화 오차 `e = x̂ − x`는 `[−s/2, +s/2]` 안에 있다. 값이 눈금보다 충분히 촘촘하게 흩어져 있으면 e는 이 구간의 **uniform 분포**로 잘 근사된다. A2 2.3절에서 본 대로:

```
Var(e) = (b − a)²/12 = s²/12,     std(e) = s/√12 ≈ 0.289·s
```

말로 하면: 양자화 noise의 전력은 눈금 간격의 제곱에 비례한다. 비트를 하나 늘리면 s가 절반 → noise 전력이 1/4 → 6.02 dB 좋아진다.

### 3.2 SQNR 공식 유도

SQNR(Signal-to-Quantization-Noise Ratio) = 신호 전력 / 양자화 noise 전력, dB로 `10·log10(…)`.

b비트 대칭 양자화, clip 임계값 c(= "full-scale"), 신호 표준편차 σ라고 하자. 레벨이 약 2^b개이고 범위가 2c이므로 `s ≈ 2c / 2^b`.

```
noise  = s²/12 = (2c)² / (12 · 4^b) = c² / (3 · 4^b)
SQNR   = σ² / noise = 3 · 4^b · (σ/c)²
SQNR_dB = 10·log10(3) + b · 20·log10(2) − 20·log10(c/σ)
        ≈ 4.77 + 6.02·b − 20·log10(c/σ)
```

말로 하면: **비트당 6.02 dB**, 그리고 clip 범위를 신호 σ의 몇 배로 잡느냐(c/σ, "crest factor")만큼 손해다. ADC의 유명한 `6.02·N + 1.76 dB`는 full-scale 사인파(c/σ = √2 → −3.01 dB)를 넣은 특수한 경우다: 4.77 − 3.01 = 1.76.

Gaussian 텐서를 min-max(`c = max|x|`)로 잡으면 샘플 10만 개에서 c ≈ 4.7σ라서 20·log10(4.7) ≈ 13.5 dB를 잃는다. 8비트면 `4.77 + 48.16 − 13.5 ≈ 39.4 dB`.

### 3.3 코드로 확인: 비트 수 vs SQNR

아래 코드는 N(0,1) 10만 개를 2~12비트로 양자화해서 noise 분산이 `s²/12`와 맞는지, SQNR이 공식과 맞는지 본다.

```python
import numpy as np
rng = np.random.default_rng(0)
x = rng.normal(0.0, 1.0, 100_000)            # 가짜 activation/weight 텐서

def fake_quant_sym(x, bits, clip):
    qmax = 2 ** (bits - 1) - 1                 # 예: 8bit -> 127
    s = clip / qmax
    return s * np.clip(np.round(x / s), -qmax, qmax), s

def sqnr_db(x, xh):
    return 10 * np.log10(np.sum(x ** 2) / np.sum((x - xh) ** 2))

k = np.abs(x).max()                           # min-max(대칭) → clip = max|x|
print(f"max|x| = {k:.3f} (sigma = 1)")
print("bits  s        noise_var   s^2/12      SQNR(dB)  6.02b+4.77-20log10(k)")
for b in range(2, 13):
    xh, s = fake_quant_sym(x, b, k)
    theory = 6.02 * b + 4.77 - 20 * np.log10(k)   # 신호 σ=1 기준 근사식
    print(f"{b:3d}  {s:.5f}  {np.var(x - xh):.3e}  {s*s/12:.3e}  {sqnr_db(x, xh):7.2f}   {theory:7.2f}")
```

```text
max|x| = 4.732 (sigma = 1)
bits  s        noise_var   s^2/12      SQNR(dB)  6.02b+4.77-20log10(k)
  2  4.73196  9.407e-01  1.866e+00     0.27      3.31
  3  1.57732  2.084e-01  2.073e-01     6.81      9.33
  4  0.67599  3.805e-02  3.808e-02    14.20     15.35
  5  0.31546  8.276e-03  8.293e-03    20.82     21.37
  6  0.15264  1.935e-03  1.942e-03    27.13     27.39
  7  0.07511  4.693e-04  4.701e-04    33.29     33.41
  8  0.03726  1.162e-04  1.157e-04    39.35     39.43
  9  0.01856  2.872e-05  2.870e-05    45.42     45.45
 10  0.00926  7.132e-06  7.146e-06    51.47     51.47
 11  0.00463  1.790e-06  1.783e-06    57.47     57.49
 12  0.00231  4.449e-07  4.453e-07    63.52     63.51
```

출력에서 볼 것: 4비트 이상에서 측정 noise 분산이 `s²/12`와 3자리까지 맞고, SQNR이 비트당 약 6 dB씩 오른다. 8비트에서 39.35 dB 측정 vs 39.43 dB 예측. 2~3비트에서는 눈금이 σ와 비슷할 만큼 굵어서 "uniform noise" 가정이 깨진다 — int4 이하가 어려운 이유 중 하나다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300">
<line x1="70" y1="250" x2="620" y2="250" stroke="currentColor"/> <line x1="70" y1="40" x2="70" y2="250" stroke="currentColor"/> <line x1="66" y1="250.0" x2="70" y2="250.0" stroke="currentColor"/><text x="60" y="254.0" font-size="12" text-anchor="end">0</text> <line x1="66" y1="220.0" x2="70" y2="220.0" stroke="currentColor"/><text x="60" y="224.0" font-size="12" text-anchor="end">10</text> <line x1="70" y1="220.0" x2="620" y2="220.0" stroke="#888" stroke-opacity="0.3"/> <line x1="66" y1="190.0" x2="70" y2="190.0" stroke="currentColor"/><text x="60" y="194.0" font-size="12" text-anchor="end">20</text> <line x1="70" y1="190.0" x2="620" y2="190.0" stroke="#888" stroke-opacity="0.3"/> <line x1="66" y1="160.0" x2="70" y2="160.0" stroke="currentColor"/><text x="60" y="164.0" font-size="12" text-anchor="end">30</text> <line x1="70" y1="160.0" x2="620" y2="160.0" stroke="#888" stroke-opacity="0.3"/> <line x1="66" y1="130.0" x2="70" y2="130.0" stroke="currentColor"/><text x="60" y="134.0" font-size="12" text-anchor="end">40</text>
<line x1="70" y1="130.0" x2="620" y2="130.0" stroke="#888" stroke-opacity="0.3"/> <line x1="66" y1="100.0" x2="70" y2="100.0" stroke="currentColor"/><text x="60" y="104.0" font-size="12" text-anchor="end">50</text> <line x1="70" y1="100.0" x2="620" y2="100.0" stroke="#888" stroke-opacity="0.3"/> <line x1="66" y1="70.0" x2="70" y2="70.0" stroke="currentColor"/><text x="60" y="74.0" font-size="12" text-anchor="end">60</text> <line x1="70" y1="70.0" x2="620" y2="70.0" stroke="#888" stroke-opacity="0.3"/> <line x1="66" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="60" y="44.0" font-size="12" text-anchor="end">70</text> <line x1="70" y1="40.0" x2="620" y2="40.0" stroke="#888" stroke-opacity="0.3"/> <line x1="70.0" y1="250" x2="70.0" y2="254" stroke="currentColor"/><text x="70.0" y="268" font-size="12" text-anchor="middle">2</text> <line x1="124.0" y1="250" x2="124.0" y2="254" stroke="currentColor"/><text x="124.0" y="268" font-size="12" text-anchor="middle">3</text>
<line x1="178.0" y1="250" x2="178.0" y2="254" stroke="currentColor"/><text x="178.0" y="268" font-size="12" text-anchor="middle">4</text> <line x1="232.0" y1="250" x2="232.0" y2="254" stroke="currentColor"/><text x="232.0" y="268" font-size="12" text-anchor="middle">5</text> <line x1="286.0" y1="250" x2="286.0" y2="254" stroke="currentColor"/><text x="286.0" y="268" font-size="12" text-anchor="middle">6</text> <line x1="340.0" y1="250" x2="340.0" y2="254" stroke="currentColor"/><text x="340.0" y="268" font-size="12" text-anchor="middle">7</text> <line x1="394.0" y1="250" x2="394.0" y2="254" stroke="currentColor"/><text x="394.0" y="268" font-size="12" text-anchor="middle">8</text> <line x1="448.0" y1="250" x2="448.0" y2="254" stroke="currentColor"/><text x="448.0" y="268" font-size="12" text-anchor="middle">9</text> <line x1="502.0" y1="250" x2="502.0" y2="254" stroke="currentColor"/><text x="502.0" y="268" font-size="12" text-anchor="middle">10</text>
<line x1="556.0" y1="250" x2="556.0" y2="254" stroke="currentColor"/><text x="556.0" y="268" font-size="12" text-anchor="middle">11</text> <line x1="610.0" y1="250" x2="610.0" y2="254" stroke="currentColor"/><text x="610.0" y="268" font-size="12" text-anchor="middle">12</text> <polyline points="70.0,240.1 124.0,222.0 178.0,204.0 232.0,185.9 286.0,167.8 340.0,149.8 394.0,131.7 448.0,113.7 502.0,95.6 556.0,77.5 610.0,59.5" fill="none" stroke="#e08a3c" stroke-width="2" stroke-dasharray="6 4"/> <polyline points="70.0,249.2 124.0,229.6 178.0,207.4 232.0,187.5 286.0,168.6 340.0,150.1 394.0,131.9 448.0,113.7 502.0,95.6 556.0,77.6 610.0,59.4" fill="none" stroke="#4a7bd0" stroke-width="2"/> <circle cx="70.0" cy="249.2" r="4" fill="#4a7bd0"/> <circle cx="124.0" cy="229.6" r="4" fill="#4a7bd0"/> <circle cx="178.0" cy="207.4" r="4" fill="#4a7bd0"/> <circle cx="232.0" cy="187.5" r="4" fill="#4a7bd0"/> <circle cx="286.0" cy="168.6" r="4" fill="#4a7bd0"/>
<circle cx="340.0" cy="150.1" r="4" fill="#4a7bd0"/> <circle cx="394.0" cy="131.9" r="4" fill="#4a7bd0"/> <circle cx="448.0" cy="113.7" r="4" fill="#4a7bd0"/> <circle cx="502.0" cy="95.6" r="4" fill="#4a7bd0"/> <circle cx="556.0" cy="77.6" r="4" fill="#4a7bd0"/> <circle cx="610.0" cy="59.4" r="4" fill="#4a7bd0"/> <text x="402.0" y="149.9" font-size="12">8bit: 39.4 dB</text> <text x="345" y="290" font-size="12" text-anchor="middle">bits</text> <text x="20" y="30" font-size="12">SQNR (dB)</text> <line x1="360" y1="215" x2="390" y2="215" stroke="#4a7bd0" stroke-width="2"/><text x="396" y="219" font-size="12">측정 (N(0,1) 10만 개, clip = max|x| = 4.73)</text> <line x1="360" y1="235" x2="390" y2="235" stroke="#e08a3c" stroke-width="2" stroke-dasharray="6 4"/><text x="396" y="239" font-size="12">6.02·b + 4.77 − 20·log10(4.73)</text>
</svg>
```

그림 2 — 비트 수에 따른 SQNR (위 코드의 실제 측정값). 파랑 점이 측정, 주황 점선이 `6.02·b + 4.77 − 20·log10(c/σ)` 공식. 4비트 이상에서 거의 겹치고, 기울기는 비트당 6 dB다.

### 3.4 감 잡기: 몇 dB면 충분한가

- 40 dB ≈ noise 진폭이 신호의 1%. 대부분의 CNN/MLP 층은 이 정도면 최종 정확도 차이가 거의 없다.
- 20 dB ≈ noise 진폭 10%. 층 하나만 이러면 버틸 수도 있지만 여러 층에 누적되면 정확도가 무너진다.
- C8에서 "layer별 SQNR"로 어느 층이 문제인지 찾는다. 이 절의 공식이 "이 층은 이론상 몇 dB가 나와야 정상인가"의 기준선이다.

### 3.5 Don 경험과 연결

ADC 설계에서 하던 계산 그대로다. 신호가 full-scale보다 작으면(gain이 너무 낮으면) 유효 비트(ENOB)를 잃고, 너무 크면 포화된다. 양자화의 scale 선택 = ADC 앞단 PGA gain 선택. 차이는 ML에서는 "신호 분포"를 calibration 데이터로 추정한다는 점이다.

---

## 4. 대칭 vs 비대칭, 그리고 "0은 정확히 표현되어야 한다"

### 4.1 두 방식

| | 대칭 (symmetric) | 비대칭 (asymmetric, affine) |
|---|---|---|
| zero-point | z = 0 고정 | z = 아무 정수 |
| scale | `s = max(abs(x)) / q_max` | `s = (x_max − x_min) / (q_max − q_min)` |
| 표현 범위 | [−c, +c] 원점 대칭 | [x_min, x_max] 아무 구간 |
| 장점 | 정수 산술에서 보정항이 사라진다 (7.2절) | 한쪽으로 치우친 분포에서 레벨을 낭비하지 않는다 |
| 전형적 사용처 | **weight** (대략 0 중심 분포) | **activation** (ReLU 뒤는 전부 ≥ 0) |

말로 하면: weight는 0 근처에 대칭으로 모여 있어서 대칭으로 충분하고, 대칭이면 연산이 싸다. ReLU 출력은 음수가 없어서, 대칭으로 하면 음수 절반(−127..−1)을 영영 안 쓴다 → 해상도 1비트 손해.

### 4.2 int8 범위: 왜 weight는 −127..127인가

int8은 −128..127로 비대칭이다. 대칭 양자화에서 −128을 허용하면 음수 쪽만 한 칸 더 긴 이상한 자가 된다. 그래서 TFLite 양자화 스펙은 weight를 **[−127, 127]**로 제한한다. 부수 효과로 −128 × −128 = 16384 같은 경우가 사라져서 곱이 항상 `127 × 128 = 16256` 이하가 되고, 일부 SIMD 트릭(16-bit 누산 페어 등)의 오버플로 분석이 쉬워진다. 반면 PyTorch의 `per_channel_symmetric` observer는 [−128, 127]을 쓰고 `s = max(abs(w)) / 127.5`로 계산한다(9.5절 출력). **도구마다 관례가 다르다**는 것 자체가 면접 포인트다.

### 4.3 "실수 0은 정확히 표현되어야 한다" — 왜?

ML 텐서에는 **정확히 0인 값이 엄청 많다.**

- zero padding: conv가 경계 밖을 0으로 채운다.
- ReLU: 음수가 전부 정확히 0이 된다 (보통 절반 가까이).
- dropout, masking, sparse 입력.

z가 정수면 실수 0 → `round(0/s) + z = z` → 역양자화 `s·(z − z) = 0`. **오차 0**이다. 게다가 padding을 정수 도메인에서 "z로 채우기"라는 단순한 memset으로 할 수 있다.

만약 0이 격자 위에 없으면(예: `q = round((x − x_min)/s)`로 z를 따로 두지 않는 방식), 모든 0이 같은 방향으로 조금씩 틀린다. 이 오차는 **random이 아니라 bias**라서 합칠수록 누적된다. 아래 코드로 확인한다.

### 4.4 코드로 확인

아래 코드는 (1) ReLU 출력을 대칭 int8과 비대칭 uint8로 양자화해 비교하고, (2) 0이 격자 위에 없는 잘못된 방식이 zero padding 영역의 dot product를 얼마나 틀리게 하는지 본다.

```python
import numpy as np
rng = np.random.default_rng(1)
a = np.maximum(rng.normal(0.5, 1.0, 10_000), 0)          # ReLU 출력: 0 이상, 약 31%가 정확히 0
print(f"range [{a.min():.3f}, {a.max():.3f}], zeros = {np.mean(a == 0):.1%}")

def mse(x, xh): return np.mean((x - xh) ** 2)
# (1) 대칭 int8: s = max|a|/127, z = 0 → 음수 쪽 -127..-1은 영영 안 쓰인다
s_sym = a.max() / 127
q_sym = np.clip(np.round(a / s_sym), -127, 127)
# (2) 비대칭 uint8: [0, max] → 0..255, z = 0 (min이 0이므로)
s_asym = a.max() / 255
q_asym = np.clip(np.round(a / s_asym), 0, 255)
print(f"symmetric int8 : levels used = {len(np.unique(q_sym)):3d}, MSE = {mse(a, q_sym*s_sym):.3e}")
print(f"asymmetric u8  : levels used = {len(np.unique(q_asym)):3d}, MSE = {mse(a, q_asym*s_asym):.3e}")

# (3) '0이 정확히 표현되지 않는' 잘못된 방식: q = round((x - xmin)/s), zero-point를 정수로 안 맞춤
x = np.concatenate([rng.normal(0, 1, 64), np.zeros(64)])   # 뒤 64개 = zero padding
xmin, xmax = x.min(), x.max()
s = (xmax - xmin) / 255
bad = xmin + s * np.round((x - xmin) / s)                   # 0이 격자 위에 없다
z = int(round(-xmin / s)); good = s * (np.clip(np.round(x / s) + z, 0, 255) - z)
w = rng.normal(0, 1, x.size)
print(f"dequant(0): bad = {bad[-1]:+.5f}, good = {good[-1]:+.5f}")
print(f"dot error on padded part only: bad = {np.dot(w[64:], bad[64:]):+.5f}, good = {np.dot(w[64:], good[64:]):+.5f}")
```

```text
range [0.000, 4.432], zeros = 31.5%
symmetric int8 : levels used = 116, MSE = 6.994e-05
asymmetric u8  : levels used = 208, MSE = 1.757e-05
dequant(0): bad = +0.01131, good = +0.00000
dot error on padded part only: bad = +0.04986, good = +0.00000
```

출력에서 볼 것: ReLU 출력에서 대칭 int8은 256단계 중 116개만 쓰고 MSE가 4배(= 6 dB, 1비트) 크다. 잘못된 방식에서는 0이 +0.01131로 돌아오고, 0이어야 할 padding 64개와의 dot product가 0.0499만큼 틀린다 — 입력과 상관없이 매번 같은 방향으로. 올바른 방식(정수 z)은 정확히 0이다.

### 4.5 임베디드 연결 + 함정

- **padding 값은 0이 아니라 z_x다.** C로 conv를 직접 짤 때 가장 흔한 버그: 입력 버퍼 경계 밖을 `memset(…, 0, …)`으로 채운다. int8 비대칭에서 정수 0은 실수 `−s·z`라서 전혀 0이 아니다. 경계 밖은 `z_x`로 채워야 한다(또는 7절처럼 `q_x − z_x`를 쓰는 구현이라면 그 값이 0이 되도록).
- ReLU 출력 텐서의 z는 보통 q_min(uint8이면 0, int8이면 −128)이 된다. x_min = 0이기 때문이다. 이때 "실수 0 = 정수 최솟값"이라 ReLU가 clamp의 하한과 정확히 일치한다 (8절).

---

## 5. Granularity — scale을 몇 개나 둘 것인가

### 5.1 세 단계

```
weight 행렬 W: [out_ch = 4, in = 8]

per-tensor          per-channel (axis=0)      per-group (group = 4, 행을 따라)
┌────────────────┐  ┌────────────────┐ s0      ┌───────┬────────┐ s00 s01
│ 전체에 s 하나  │  │ ───────────── │ s1      │       │        │ s10 s11
│                │  │ ───────────── │ s2      │       │        │ s20 s21
│                │  │ ───────────── │ s3      │       │        │ s30 s31
└────────────────┘  └────────────────┘         └───────┴────────┘
scale 1개            출력 채널마다 1개 (4개)       행마다 group마다 1개 (8개)
```

- **per-tensor**: 텐서 전체에 (s, z) 하나. 가장 싸고 단순. activation은 거의 항상 이것.
- **per-channel (per-axis)**: weight의 **출력 채널**마다 s 하나. conv weight `[C_out, C_in, kH, kW]`(PyTorch)면 axis 0, dense `[out, in]`이면 행마다. TFLite의 Conv2D weight `[C_out, kH, kW, C_in]`도 axis 0, depthwise conv는 채널이 마지막 축(axis 3)이다.
- **per-group (block)**: 한 행을 다시 g개(32, 64, 128 등)씩 잘라 group마다 s 하나. LLM int4 weight-only 양자화(C3)의 표준.

왜 **출력** 채널인가? 출력 채널 i의 누산은 `Σ_j q_w[i][j] · q_x[j]` 하나로 끝나고, scale `s_w[i]`는 그 합 전체에 곱해지는 상수라서 **requantize multiplier에 흡수**된다(7절: `M_i = s_w[i]·s_x/s_y`). 추가 비용은 채널마다 M0, shift 하나씩뿐이다. 반대로 입력 채널(j)마다 scale이 다르면 합 안에서 항마다 다른 scale을 곱해야 해서 정수 MAC 한 번으로 끝나지 않는다. per-group이 LLM에서 주로 **weight-only**(activation은 fp16, C3)로 쓰이는 것도 같은 이유다 — group마다 부분합을 따로 만들어 scale을 곱해야 한다.

### 5.2 코드로 확인: outlier 채널이 있는 weight

아래 코드는 [8, 64] weight에서 채널 3만 10배 크게, 채널 5에는 튀는 값 하나를 넣고, granularity별 채널별 SQNR을 비교한다.

```python
import numpy as np
rng = np.random.default_rng(2)
W = rng.normal(0, 0.05, (8, 64))            # [out_ch, in_features]
W[3] *= 10                                   # 출력 채널 3만 10배 큰 weight (outlier channel)
W[5, 17] = 0.9                               # 채널 5 안에 튀는 값 하나

def fq_sym(w, bits, axis=None, group=None):
    """대칭 fake-quant. axis=None: per-tensor, axis=1: 행(출력 채널)마다, group: 행을 group개씩 잘라서."""
    qmax = 2 ** (bits - 1) - 1
    if group:
        g = w.reshape(w.shape[0], -1, group)                      # [out, n_groups, group]
        s = np.abs(g).max(axis=2, keepdims=True) / qmax
        return (np.clip(np.round(g / s), -qmax, qmax) * s).reshape(w.shape), s.size
    amax = np.abs(w).max() if axis is None else np.abs(w).max(axis=axis, keepdims=True)
    s = amax / qmax
    return np.clip(np.round(w / s), -qmax, qmax) * s, np.size(s)

cases = [("int8 per-tensor", 8, None, None), ("int8 per-channel", 8, 1, None),
         ("int4 per-channel", 4, 1, None), ("int4 group=32", 4, None, 32), ("int4 group=16", 4, None, 16)]
print("case              #scales  per-row SQNR (dB), rows 0..7                  whole-matrix")
for name, b, ax, g in cases:
    Wq, n = fq_sym(W, b, ax, g)
    row = 10 * np.log10((W ** 2).sum(1) / ((W - Wq) ** 2).sum(1))
    tot = 10 * np.log10((W ** 2).sum() / ((W - Wq) ** 2).sum())
    print(f"{name:17s} {n:5d}   " + " ".join(f"{v:5.1f}" for v in row) + f"   {tot:5.1f}")
```

```text
case              #scales  per-row SQNR (dB), rows 0..7                  whole-matrix
int8 per-tensor       1    22.9  24.1  23.6  45.9  24.7  31.1  23.7  22.8    36.2
int8 per-channel      8    45.1  43.8  46.1  45.9  44.9  35.6  43.6  42.4    44.4
int4 per-channel      8    19.6  19.6  20.7  19.8  21.4  10.4  19.6  17.0    18.5
int4 group=32        16    19.6  19.6  20.8  21.0  21.5  13.5  19.7  17.8    20.2
int4 group=16        32    21.1  21.4  21.4  22.2  22.4  15.9  20.6  20.6    21.6
```

출력에서 볼 것:

- **int8 per-tensor**: 전체 SQNR 36.2 dB로 멀쩡해 보이지만, 채널별로는 ch3(큰 채널)만 46 dB이고 나머지 7채널은 23 dB 근처다. 큰 채널이 scale을 혼자 정해 버렸다. **전체 SQNR 하나로 보면 이 문제가 숨는다** (C8에서 채널별·층별로 보라는 이유).
- **int8 per-channel**: 모든 채널이 약 44 dB로 회복. scale 8개(= 32바이트)의 대가.
- **int4**: per-channel로도 20 dB 안팎. 채널 안에 튀는 값이 있는 ch5는 10.4 dB. group=32 → 13.5, group=16 → 15.9 dB로 group을 잘게 할수록 outlier의 피해가 그 group 안에 갇힌다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 300">
<line x1="60" y1="250" x2="620" y2="250" stroke="currentColor"/> <line x1="60" y1="50" x2="60" y2="250" stroke="currentColor"/> <line x1="56" y1="250.0" x2="60" y2="250.0" stroke="currentColor"/><text x="52" y="254.0" font-size="12" text-anchor="end">0</text> <line x1="56" y1="210.0" x2="60" y2="210.0" stroke="currentColor"/><text x="52" y="214.0" font-size="12" text-anchor="end">10</text> <line x1="56" y1="170.0" x2="60" y2="170.0" stroke="currentColor"/><text x="52" y="174.0" font-size="12" text-anchor="end">20</text> <line x1="56" y1="130.0" x2="60" y2="130.0" stroke="currentColor"/><text x="52" y="134.0" font-size="12" text-anchor="end">30</text> <line x1="56" y1="90.0" x2="60" y2="90.0" stroke="currentColor"/><text x="52" y="94.0" font-size="12" text-anchor="end">40</text> <line x1="56" y1="50.0" x2="60" y2="50.0" stroke="currentColor"/><text x="52" y="54.0" font-size="12" text-anchor="end">50</text>
<rect x="69" y="158.4" width="24" height="91.6" fill="#e08a3c"/> <rect x="95" y="69.6" width="24" height="180.4" fill="#4a7bd0"/> <text x="95" y="268" font-size="12" text-anchor="middle">ch0</text> <rect x="139" y="153.6" width="24" height="96.4" fill="#e08a3c"/> <rect x="165" y="74.8" width="24" height="175.2" fill="#4a7bd0"/> <text x="165" y="268" font-size="12" text-anchor="middle">ch1</text> <rect x="209" y="155.6" width="24" height="94.4" fill="#e08a3c"/> <rect x="235" y="65.6" width="24" height="184.4" fill="#4a7bd0"/> <text x="235" y="268" font-size="12" text-anchor="middle">ch2</text> <rect x="279" y="66.4" width="24" height="183.6" fill="#e08a3c"/> <rect x="305" y="66.4" width="24" height="183.6" fill="#4a7bd0"/> <text x="305" y="268" font-size="12" text-anchor="middle">ch3 ×10</text> <rect x="349" y="151.2" width="24" height="98.8" fill="#e08a3c"/> <rect x="375" y="70.4" width="24" height="179.6" fill="#4a7bd0"/>
<text x="375" y="268" font-size="12" text-anchor="middle">ch4</text> <rect x="419" y="125.6" width="24" height="124.4" fill="#e08a3c"/> <rect x="445" y="107.6" width="24" height="142.4" fill="#4a7bd0"/> <text x="445" y="268" font-size="12" text-anchor="middle">ch5 spike</text> <rect x="489" y="155.2" width="24" height="94.8" fill="#e08a3c"/> <rect x="515" y="75.6" width="24" height="174.4" fill="#4a7bd0"/> <text x="515" y="268" font-size="12" text-anchor="middle">ch6</text> <rect x="559" y="158.8" width="24" height="91.2" fill="#e08a3c"/> <rect x="585" y="80.4" width="24" height="169.6" fill="#4a7bd0"/> <text x="585" y="268" font-size="12" text-anchor="middle">ch7</text> <text x="20" y="36" font-size="12">채널별 SQNR (dB), int8 대칭</text> <rect x="330" y="24" width="14" height="14" fill="#e08a3c"/><text x="350" y="36" font-size="12">per-tensor (scale 1개)</text> <rect x="490" y="24" width="14" height="14" fill="#4a7bd0"/><text x="510" y="36" font-size="12">per-channel (8개)</text>
<text x="60" y="292" font-size="12">ch3은 weight가 10배 크다 → per-tensor scale을 혼자 결정하고, 나머지 7채널은 약 22 dB 손해</text>
</svg>
```

그림 3 — int8 대칭 양자화의 채널별 SQNR (위 코드의 실제 값). 주황(per-tensor)은 큰 채널 ch3을 빼고 모두 약 23 dB, 파랑(per-channel)은 약 44 dB. ch5는 채널 **안의** outlier 때문에 per-channel로도 35.6 dB에 그친다.

### 5.3 scale 저장 비용

scale 하나를 fp16(2바이트)로 저장한다고 하자.

| 방식 | weight bits | scale 오버헤드 (bit/weight) | 유효 bits/weight |
|---|---|---|---|
| int8 per-tensor | 8 | ≈ 0 | 8 |
| int8 per-channel (행 길이 1024) | 8 | 16/1024 = 0.016 | 8.02 |
| int4 group 128 | 4 | 16/128 = 0.125 | 4.125 |
| int4 group 64 | 4 | 16/64 = 0.25 | 4.25 |
| int4 group 32 | 4 | 16/32 = 0.5 | 4.5 |
| int4 group 32 + zero-point 4bit | 4 | (16 + 4)/32 = 0.625 | 4.625 |

말로 하면: group이 작을수록 정확하지만 scale이 늘어서 "int4"가 실제로는 4.5비트쯤 된다. 1B 파라미터 LLM이면 4.5비트 × 10⁹ / 8 ≈ 0.56 GB. 정수 전용 커널(7절)에서는 per-channel scale이 fp16 대신 `(int32 M0, int shift)` 쌍으로 저장되어 채널당 약 8바이트지만, 채널 수가 weight 수보다 훨씬 적어서 역시 작다 (11.3절 표).

### 5.4 함정

- per-channel은 **weight**에만 쓴다. activation을 채널별로 하면 위에서 말한 이유로 정수 MAC 하나로 안 끝난다 (예외적인 기법들은 C3의 SmoothQuant 참고).
- depthwise conv는 채널 하나가 입력 채널 하나만 보므로 per-channel이 특히 중요하다 (채널마다 weight 크기 차이가 크다). MobileNet 계열 PTQ에서 per-tensor로 하면 정확도가 무너지는 고전적 사례가 이것이다.
- BN folding(B1 7절) 뒤에는 채널마다 `γ/σ`가 곱해져 weight 크기가 채널별로 크게 달라진다. per-channel이 사실상 필수가 된 이유 중 하나다.

---

## 6. 정밀도 포맷 도감 — INT8 · UINT8 · INT4 · INT16 · FP16 · BF16 · FP8

### 6.1 한눈에 보기

| 포맷 | 비트 | 표현 | 범위 / 정밀도 | 주 용도 |
|---|---|---|---|---|
| INT8 | 8 | 정수 + (s, z) | −128..127 (대칭 weight는 −127..127) | edge 추론의 표준 (weight, activation) |
| UINT8 | 8 | 정수 + (s, z) | 0..255 | 옛 TFLite 비대칭 activation, 이미지 입력 |
| INT4 | 4 | 정수 + group scale | −8..7 (대칭이면 −7..7) | LLM weight-only (C3), 일부 NPU weight |
| INT16 | 16 | 정수 + s (보통 z = 0) | −32768..32767 | 민감한 activation (오디오 등), "16x8" 모드 |
| INT32 | 32 | 정수, s = s_w·s_x | 누산기, bias | 모든 int8 커널의 누산 |
| FP16 | 16 | 1-5-10 float | max 65504, 유효숫자 약 3자리 | GPU/NPU 추론, 모바일 GPU |
| BF16 | 16 | 1-8-7 float | fp32와 같은 범위, 유효숫자 약 2~3자리 | 학습, LLM weight/activation |
| FP8 E4M3 | 8 | 1-4-3 float | max 448 | 최신 GPU 학습·추론 (forward) |
| FP8 E5M2 | 8 | 1-5-2 float | max 57344 | 최신 GPU 학습 (gradient) |

정수 포맷은 "균일한 눈금 + 외부 scale", float 포맷은 "지수로 눈금 간격이 바뀌는 자"다. float는 0 근처는 촘촘하고 큰 값은 성기다. 그래서 outlier가 많은 분포(LLM activation)에는 FP8이 INT8보다 유리할 수 있고, 좁고 고른 분포에는 INT8이 더 정밀하다.

### 6.2 FP16 vs BF16 — 같은 16비트, 다른 선택

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 280">
<text x="10" y="48" font-size="13">FP32</text> <rect x="100" y="30" width="16" height="26" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/> <text x="108.0" y="48" font-size="12" text-anchor="middle">S</text> <rect x="116" y="30" width="128" height="26" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <text x="180.0" y="48" font-size="12" text-anchor="middle">exp 8</text> <rect x="244" y="30" width="368" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/> <text x="428.0" y="48" font-size="12" text-anchor="middle">mantissa 23</text> <text x="620" y="48" font-size="12">32 bit</text> <text x="10" y="94" font-size="13">FP16</text> <rect x="100" y="76" width="16" height="26" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/> <text x="108.0" y="94" font-size="12" text-anchor="middle">S</text> <rect x="116" y="76" width="80" height="26" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="156.0" y="94" font-size="12" text-anchor="middle">E5</text> <rect x="196" y="76" width="160" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/> <text x="276.0" y="94" font-size="12" text-anchor="middle">mantissa 10</text> <text x="364" y="94" font-size="12">16 bit · max 65504, eps 2^−10</text> <text x="10" y="140" font-size="13">BF16</text> <rect x="100" y="122" width="16" height="26" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/> <text x="108.0" y="140" font-size="12" text-anchor="middle">S</text> <rect x="116" y="122" width="128" height="26" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <text x="180.0" y="140" font-size="12" text-anchor="middle">exp 8</text> <rect x="244" y="122" width="112" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/> <text x="300.0" y="140" font-size="12" text-anchor="middle">mantissa 7</text> <text x="364" y="140" font-size="12">16 bit · max ≈ 3.4e38, eps 2^−7</text>
<text x="10" y="186" font-size="13">FP8 E4M3</text> <rect x="100" y="168" width="16" height="26" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/> <text x="108.0" y="186" font-size="12" text-anchor="middle">S</text> <rect x="116" y="168" width="64" height="26" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <text x="148.0" y="186" font-size="12" text-anchor="middle">E4</text> <rect x="180" y="168" width="48" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/> <text x="204.0" y="186" font-size="12" text-anchor="middle">M3</text> <text x="236" y="186" font-size="12">8 bit · max 448, eps 2^−3</text> <text x="10" y="232" font-size="13">FP8 E5M2</text> <rect x="100" y="214" width="16" height="26" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/> <text x="108.0" y="232" font-size="12" text-anchor="middle">S</text> <rect x="116" y="214" width="80" height="26" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="156.0" y="232" font-size="12" text-anchor="middle">E5</text> <rect x="196" y="214" width="32" height="26" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/> <text x="212.0" y="232" font-size="12" text-anchor="middle">M2</text> <text x="236" y="232" font-size="12">8 bit · max 57344, eps 2^−2</text> <text x="10" y="268" font-size="12">값 = (−1)^S × 2^(exp − bias) × 1.mantissa — exp 비트 수가 범위(dynamic range), mantissa 비트 수가 정밀도를 정한다</text>
</svg>
```

그림 4 — float 포맷의 비트 배치. 빨강 = 부호, 파랑 = 지수(exponent, 범위를 결정), 초록 = 가수(mantissa, 정밀도를 결정). BF16은 FP32의 **위쪽 16비트**와 같은 배치라서 FP32 ↔ BF16 변환이 비트 자르기(+반올림)로 끝난다.

```
FP16 : 지수 5비트 (bias 15)  → 최대 65504, 최소 정규수 2⁻¹⁴ ≈ 6.1e−5, eps = 2⁻¹⁰ ≈ 0.001
BF16 : 지수 8비트 (bias 127) → 최대 ≈ 3.4e38 (FP32와 같음),         eps = 2⁻⁷  ≈ 0.008
```

말로 하면: FP16은 **정밀도**를 택했고(가수 10비트), BF16은 **범위**를 택했다(지수 8비트). 학습에서는 gradient·activation이 가끔 65504를 넘거나 6e−5보다 작아서 FP16은 loss scaling 같은 보정이 필요하지만, BF16은 FP32처럼 그냥 된다. 대신 BF16은 1.0 근처에서 눈금이 0.0078이라 FP16보다 8배 거칠다.

아래 코드는 같은 값들을 네 포맷으로 변환했다가 다시 FP32로 읽어 본다.

```python
import torch
vals = torch.tensor([3.14159265, 1e-3, 1e-6, 70000.0, 1.0 + 2**-9], dtype=torch.float32)
print("fp32      :", [f"{v:.8g}" for v in vals.tolist()])
for dt in (torch.float16, torch.bfloat16, torch.float8_e4m3fn, torch.float8_e5m2):
    back = vals.to(dt).to(torch.float32)          # 변환 후 다시 fp32로 읽어 본다
    print(f"{str(dt)[6:]:14s}:", [f"{v:.8g}" for v in back.tolist()])
for dt in (torch.float16, torch.bfloat16, torch.float8_e4m3fn, torch.float8_e5m2):
    fi = torch.finfo(dt)
    print(f"{str(dt)[6:]:14s} bits={fi.bits:2d} max={fi.max:<12.6g} "
          f"min_normal={fi.tiny:<11.4g} eps={fi.eps:.4g}")
# 16비트 패턴 보기: 1.0 + 2^-9 는 bf16에서는 1.0 으로 뭉개진다
for dt in (torch.float16, torch.bfloat16):
    bits = torch.tensor([1.0, -2.5], dtype=dt).view(torch.int16).tolist()
    print(f"{str(dt)[6:]:9s} 1.0 -> {bits[0] & 0xFFFF:016b}, -2.5 -> {bits[1] & 0xFFFF:016b}")
```

```text
fp32      : ['3.1415927', '0.001', '1e-06', '70000', '1.0019531']
float16       : ['3.140625', '0.0010004044', '1.013279e-06', 'inf', '1.0019531']
bfloat16      : ['3.140625', '0.00099945068', '9.983778e-07', '70144', '1']
float8_e4m3fn : ['3.25', '0.001953125', '0', 'nan', '1']
float8_e5m2   : ['3', '0.0009765625', '0', 'inf', '1']
float16        bits=16 max=65504        min_normal=6.104e-05   eps=0.0009766
bfloat16       bits=16 max=3.38953e+38  min_normal=1.175e-38   eps=0.007812
float8_e4m3fn  bits= 8 max=448          min_normal=0.01562     eps=0.125
float8_e5m2    bits= 8 max=57344        min_normal=6.104e-05   eps=0.25
float16   1.0 -> 0011110000000000, -2.5 -> 1100000100000000
bfloat16  1.0 -> 0011111110000000, -2.5 -> 1100000000100000
```

출력에서 볼 것: 70000은 FP16에서 **inf**(범위 초과), BF16에서는 70144(범위는 되지만 거칠다). `1 + 2⁻⁹`는 FP16에서 정확히 남지만 BF16에서는 1로 뭉개진다. 1e−6은 FP16에서 subnormal(정규수 최솟값 6.1e−5보다 작음)로 간신히 표현된다. FP8 E4M3은 70000을 NaN으로 바꿨다 — E4M3(`fn` 변형)에는 inf가 없고 범위 밖 캐스팅이 여기서는 NaN이 되었다. 실무에서는 FP8로 보내기 전에 per-tensor scale을 곱해 범위 안에 넣고 **saturate**하는 변환을 쓴다. 비트 패턴에서 1.0이 FP16 `0x3C00`, BF16 `0x3F80`(= FP32 `0x3F800000`의 위 16비트)인 것도 확인하자.

### 6.3 FP8 — 확실히 말할 수 있는 것만

- 두 가지 변형: **E4M3**(지수 4, 가수 3, 최대 448)과 **E5M2**(지수 5, 가수 2, 최대 57344). 위 `torch.finfo` 출력이 그 값이다.
- E4M3은 범위를 조금 늘리려고 inf를 없애고 NaN 패턴 하나만 남긴 변형(`float8_e4m3fn`)이 널리 쓰인다. E5M2는 IEEE처럼 inf/NaN이 있다.
- 흔한 권장: forward(weight, activation)는 정밀도가 좋은 E4M3, backward(gradient)는 범위가 넓은 E5M2 (Micikevicius et al. 2022).
- 여전히 **scale이 필요하다**: 텐서마다(또는 블록마다) scale을 곱해 FP8 범위에 맞춘다. 즉 FP8도 "양자화"다.
- MCU/엣지 DSP에서 FP8 하드웨어 지원은 드물다(2020년대 중반 기준 주로 데이터센터 GPU·일부 최신 가속기). Hark 같은 기기에서 주력은 INT8/INT16/INT4일 가능성이 높다 — 칩 데이터시트로 확인할 것.

### 6.4 INT4 packing — 두 nibble을 한 바이트에

int4는 메모리에 1바이트당 2개씩 넣는다. 짝수 index를 하위 nibble, 홀수 index를 상위 nibble에 넣는 것이 흔하지만 **순서·배치는 프레임워크마다 다르다** (한 블록의 앞 절반은 하위 nibble, 뒤 절반은 상위 nibble에 넣는 식도 있다). 커널과 변환기가 같은 약속을 써야 한다. 아래 C 코드는 pack/unpack과 부호 확장을 확인한다.

```c
#include <stdint.h>
#include <stdio.h>

/* int4 두 개를 byte 하나에: 짝수 index -> 하위 nibble, 홀수 index -> 상위 nibble (이 노트의 약속) */
static void pack_int4(const int8_t *v, uint8_t *out, int n) {       /* n은 짝수라고 가정 */
    for (int i = 0; i < n; i += 2) {
        uint8_t lo = (uint8_t)(v[i]     & 0x0F);                     /* 2의 보수 하위 4비트 */
        uint8_t hi = (uint8_t)(v[i + 1] & 0x0F);
        out[i / 2] = (uint8_t)(lo | (hi << 4));
    }
}
static inline int8_t sext4(uint8_t nib) {                            /* 0..15 -> -8..7 */
    return (int8_t)((nib ^ 0x8) - 0x8);                              /* 이식성 있는 부호 확장 */
}
static void unpack_int4(const uint8_t *in, int8_t *v, int n) {
    for (int i = 0; i < n; i += 2) {
        v[i]     = sext4(in[i / 2] & 0x0F);
        v[i + 1] = sext4(in[i / 2] >> 4);
    }
}

int main(void) {
    const int8_t w[8] = {-8, 7, -1, 0, 3, -3, 5, -7};
    uint8_t packed[4]; int8_t back[8];
    pack_int4(w, packed, 8);
    unpack_int4(packed, back, 8);
    printf("packed bytes:");
    for (int i = 0; i < 4; i++) printf(" 0x%02X", packed[i]);
    printf("\nunpacked    :");
    int ok = 1;
    for (int i = 0; i < 8; i++) { printf(" %d", back[i]); ok &= (back[i] == w[i]); }
    printf("\nround-trip %s, %d weights in %zu bytes\n", ok ? "OK" : "FAIL", 8, sizeof packed);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 int4pack.c -o int4pack -lm && ./int4pack
```

```text
packed bytes: 0x78 0x0F 0xD3 0x95
unpacked    : -8 7 -1 0 3 -3 5 -7
round-trip OK, 8 weights in 4 bytes
```

출력에서 볼 것: −8(`0x8`)과 7(`0x7`)이 `0x78` 한 바이트가 된다. 부호 확장은 `(n ^ 8) − 8` 트릭으로 했다: nibble의 부호 비트(bit 3)를 뒤집은 뒤 8을 빼면 0..15가 −8..7로 간다. `(int8_t)(n << 4) >> 4`도 흔히 쓰지만, C 표준에서 음수의 오른쪽 시프트는 implementation-defined라 이식성을 따지면 이 방식이 깔끔하다. 커널 안에서는 unpack이 MAC 루프 안쪽에 들어가므로, 실제 최적화 커널은 SIMD 셔플·마스크로 한 번에 여러 nibble을 푼다.

### 6.5 INT16 activation — "16x8"

오디오 전처리 뒤 feature나 LSTM 상태처럼 dynamic range가 넓고 민감한 activation은 int8로 부족할 수 있다. 이때 **activation int16(대칭, z = 0) × weight int8**, 누산은 64-bit(또는 넉넉한 누산기)로 하는 "16x8" 방식이 있다. TFLite에도 이 모드가 있지만 지원 op가 int8보다 제한적이다. SQNR 공식(3.2절)으로 보면 activation 쪽 noise가 8비트 × 6 dB = 48 dB 줄어든다. 비용은 activation 메모리 2배와 느린 MAC.

### 6.6 어떤 하드웨어가 무엇을 지원하나 (일반론, 제품마다 확인)

| 타깃 (일반적인 경우) | 잘하는 것 | 메모 |
|---|---|---|
| Cortex-M0+/M3 급 | int8 스칼라 | FPU 없음, SIMD 없음 → int8 루프가 곧 최선 |
| Cortex-M4/M7/M33 (DSP 확장) | int8/int16 via 16-bit SIMD MAC | float는 단정도 FPU가 있는 경우가 많지만 int8이 훨씬 빠르고 작다 |
| Cortex-M55/M85 (Helium) | int8/int16 벡터 MAC, fp16 벡터 | CMSIS-NN이 Helium 경로를 따로 둔다 |
| Cortex-A (NEON) | int8 dot product 확장, fp16 | 확장 지원 여부는 코어 세대마다 다름 |
| DSP (예: Hexagon HVX 계열) | int8/int16 벡터 | 세대별로 fp16 지원 차이 |
| MCU·모바일 NPU | int8 (일부 int16 activation, int4 weight) | float 미지원이거나 느린 fallback이 흔함 |
| GPU | fp16/bf16, int8, (최신) fp8 | edge GPU는 fp16이 주력인 경우가 많음 |

말로 하면: **edge에서 공통 분모는 int8**이다. 그래서 C1의 나머지 절은 int8 정수 전용 추론에 집중한다.

---

## 7. 정수 전용 추론 — int32 누산과 requantization

이 절이 이 노트의 핵심이다. 목표: 실수 식 `y = W·x + b`를 **정수 연산만으로** 계산해 int8 출력을 만든다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 230">
<defs><marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="10" y="22" font-size="14">int8 dense/conv 한 출력의 데이터 경로 (정수만 사용)</text> <rect x="10" y="70" width="72" height="56" rx="6" fill="#888" fill-opacity="0.18" stroke="#888"/> <text x="46.0" y="94" font-size="12" text-anchor="middle">int8 x</text> <text x="46.0" y="112" font-size="12" text-anchor="middle">q_x</text> <line x1="82" y1="98" x2="95" y2="98" stroke="currentColor" marker-end="url(#ar)"/> <rect x="95" y="70" width="72" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/> <text x="131.0" y="94" font-size="12" text-anchor="middle">int8 MAC</text> <text x="131.0" y="112" font-size="12" text-anchor="middle">Σ q_w·q_x</text> <line x1="167" y1="98" x2="180" y2="98" stroke="currentColor" marker-end="url(#ar)"/>
<rect x="180" y="70" width="72" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/> <text x="216.0" y="94" font-size="12" text-anchor="middle">+ bias_eff</text> <text x="216.0" y="112" font-size="12" text-anchor="middle">int32 acc</text> <line x1="252" y1="98" x2="265" y2="98" stroke="currentColor" marker-end="url(#ar)"/> <rect x="265" y="70" width="72" height="56" rx="6" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/> <text x="301.0" y="94" font-size="12" text-anchor="middle">× M0 (Q31)</text> <text x="301.0" y="112" font-size="12" text-anchor="middle">SRDHM</text> <line x1="337" y1="98" x2="350" y2="98" stroke="currentColor" marker-end="url(#ar)"/> <rect x="350" y="70" width="72" height="56" rx="6" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/> <text x="386.0" y="94" font-size="12" text-anchor="middle">÷ 2^n</text> <text x="386.0" y="112" font-size="12" text-anchor="middle">RDBPOT</text>
<line x1="422" y1="98" x2="435" y2="98" stroke="currentColor" marker-end="url(#ar)"/> <rect x="435" y="70" width="72" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.18" stroke="#3f9a6b"/> <text x="471.0" y="94" font-size="12" text-anchor="middle">+ z_y</text> <text x="471.0" y="112" font-size="12" text-anchor="middle">offset</text> <line x1="507" y1="98" x2="520" y2="98" stroke="currentColor" marker-end="url(#ar)"/> <rect x="520" y="70" width="72" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.18" stroke="#3f9a6b"/> <text x="556.0" y="94" font-size="12" text-anchor="middle">clamp</text> <text x="556.0" y="112" font-size="12" text-anchor="middle">act_min..max</text> <line x1="592" y1="98" x2="605" y2="98" stroke="currentColor" marker-end="url(#ar)"/> <rect x="605" y="70" width="72" height="56" rx="6" fill="#888" fill-opacity="0.18" stroke="#888"/> <text x="641.0" y="94" font-size="12" text-anchor="middle">int8 y</text>
<text x="641.0" y="112" font-size="12" text-anchor="middle">q_y</text> <text x="95" y="58" font-size="12">int32 누산 (오버플로 여유)</text> <text x="265" y="58" font-size="12">requantize: M = s_w·s_x / s_y ≈ M0·2^−31·2^−n</text> <text x="435" y="150" font-size="12">ReLU/ReLU6 = clamp 범위만 변경</text> <text x="95" y="150" font-size="12">bias_eff = q_b − z_x·Σq_w (오프라인 계산)</text> <text x="10" y="190" font-size="12">파랑: 정수 MAC 단계 · 주황: 고정소수점 곱셈과 반올림 시프트 · 초록: 출력 정수 도메인 맞추기</text>
</svg>
```

그림 5 — int8 dense/conv 한 출력의 정수 데이터 경로. MAC은 int32로 누산하고, 실수 multiplier M은 Q31 정수 M0와 오른쪽 시프트 n으로 바꿔 적용한다. 활성화(ReLU/ReLU6)는 마지막 clamp의 경계로 흡수된다.

### 7.1 유도

각 텐서를 affine 양자화로 쓴다 (출력 채널 i, 입력 j, 입력 길이 N).

```
W[i][j] = s_w · (q_w[i][j] − z_w)
x[j]    = s_x · (q_x[j]   − z_x)
b[i]    = s_b · q_b[i]              (bias: z = 0, s_b = s_w · s_x 로 정한다 — 이유는 아래)
y[i]    = s_y · (q_y[i]   − z_y)

y[i] = Σ_j W[i][j]·x[j] + b[i]
     = s_w·s_x · [ Σ_j (q_w[i][j] − z_w)(q_x[j] − z_x) + q_b[i] ]

⇒  q_y[i] = z_y + (s_w·s_x / s_y) · acc[i]
    acc[i] = Σ_j (q_w[i][j] − z_w)(q_x[j] − z_x) + q_b[i]       ← 전부 정수!
    M      = s_w·s_x / s_y                                       ← 실수 상수 하나 (채널마다)
```

말로 하면: 모든 scale은 대괄호 밖으로 빠져 나와 **실수 상수 M 하나**로 합쳐지고, 대괄호 안은 순수한 정수 누산이다. bias의 scale을 `s_w·s_x`로 정한 이유가 여기서 보인다: 그래야 bias가 누산기와 **같은 단위**라 그냥 더할 수 있다. bias는 int32로 저장한다 (값이 누산기와 같은 크기 등급이라 int8로는 부족하다).

### 7.2 zero-point 보정항 — 왜 weight는 대칭인가

곱을 전개하면 네 항이 나온다.

```
Σ_j (q_w − z_w)(q_x − z_x) = Σ_j q_w·q_x          (1) 본체: N MAC
                           − z_x · Σ_j q_w[i][j]   (2) weight만의 함수 → 오프라인 상수
                           − z_w · Σ_j q_x[j]      (3) 입력의 함수 → 런타임에 매번 계산
                           + N · z_w · z_x         (4) 상수
```

- (2)와 (4)는 weight와 양자화 파라미터만 알면 **변환기에서 미리 계산**해 bias에 접어 넣을 수 있다: `bias_eff[i] = q_b[i] − z_x·Σ_j q_w[i][j] + N·z_w·z_x`.
- (3)은 입력마다 달라서 런타임 비용이다(입력 벡터당 N번 덧셈, 모든 출력이 공유).
- **weight를 대칭(z_w = 0)으로 하면 (3)과 (4)가 통째로 사라진다.** 남는 건 `acc = Σ q_w·q_x + bias_eff` — MAC 루프 하나 + 상수 하나. 이것이 "weight 대칭, activation 비대칭"이 표준이 된 산수적 이유다.

구현 방식은 두 가지가 흔하다: (a) 위처럼 보정항을 bias에 접는다, (b) MAC 루프 안에서 `q_w · (q_x + input_offset)`, `input_offset = −z_x`로 계산한다. (b)는 16-bit로 넓힌 뒤 더하는 SIMD 구현과 잘 맞는다. 결과는 정수적으로 동일하다.

누산기 오버플로 여유: `abs(q_w·q_x) ≤ 127 × 128 = 16256 < 2¹⁴`. int32의 여유는 2³¹이므로 최악의 경우에도 약 2¹⁷ = 131072개 항까지 안전하다. conv 3×3×256 = 2304항은 한참 여유가 있다. (입력을 z_x만큼 옮겨 16-bit로 쓰는 구현은 값이 최대 255까지 가므로 이 계산이 조금 달라진다.)

### 7.3 M을 정수로: M = M0 · 2⁻³¹ · 2^shift

M은 실수다. MCU에서 float 곱을 피하려고 **Q31 고정소수점 × 2의 거듭제곱**으로 분해한다 (gemmlowp/TFLite 방식).

```
1) frexp:  M = f · 2^shift,   f ∈ [0.5, 1)         (shift는 정수, M < 1이면 shift ≤ 0)
2) Q31  :  M0 = round(f · 2³¹)  ∈ [2³⁰, 2³¹)        (반올림이 2³¹이 되면 M0 /= 2, shift += 1)
3) 적용 :  acc · M ≈ ( (acc · M0) / 2³¹ ) / 2^(−shift)     ← 둘 다 반올림 포함
```

말로 하면: M을 "0.5 이상 1 미만의 가수 × 2의 지수"로 나누고, 가수를 Q31 정수로 만든다. 가수를 [0.5, 1)로 맞추는 이유는 M0가 항상 31비트 정밀도를 꽉 채우게 하려는 것이다 (float의 정규화와 같은 생각). 곱 `acc × M0`은 64-bit, 위쪽 32비트만 취하면 Q31 곱이 된다 — Don이 DSP에서 Q31 × Q31 곱하던 것과 똑같다.

손계산: M = 0.0072

```
0.0072 × 2⁷ = 0.9216  ∈ [0.5, 1)   →  f = 0.9216, shift = −7
M0 = round(0.9216 × 2147483648) = round(1979120929.9) = 1979120930  (0x75F6FD22)
확인: 1979120930 × 2⁻³¹ × 2⁻⁷ = 0.0072000000
acc = 12345:  12345 × 0.0072 = 88.884  →  기대 출력 89
```

### 7.4 반올림 두 번: SRDHM과 RDBPOT

gemmlowp는 두 단계를 각각 반올림한다.

- **SaturatingRoundingDoublingHighMul(a, b)** (SRDHM): `round(a·b / 2³¹)`를 int32로. "doubling high mul"은 `(a·b·2)`의 상위 32비트 = `a·b / 2³¹`이라는 뜻이다. 유일한 오버플로(`a = b = INT32_MIN`, 즉 −1 × −1 = +1이 Q31로 표현 불가)는 INT32_MAX로 포화. ARM NEON의 `SQRDMULH` 명령이 바로 이 연산을 벡터로 한다.
- **RoundingDivideByPOT(x, n)** (RDBPOT): `x / 2ⁿ`를 **half-away-from-zero**로 반올림하는 오른쪽 시프트. 단순 `x >> n`은 −∞ 쪽으로 버림이라 음수에서 bias가 생긴다.

아래 코드는 이 두 함수와 M0/shift 분해를 numpy int64로 구현한다 (정의만, 출력 없음).

```python
import math
import numpy as np
INT32_MIN, INT32_MAX = -(1 << 31), (1 << 31) - 1

def quantize_multiplier(M):
    """실수 M(>0)을 M0(Q31 정수)와 shift로: M ≈ M0 · 2^-31 · 2^shift, M0 ∈ [2^30, 2^31)."""
    frac, shift = math.frexp(M)              # M = frac · 2^shift, frac ∈ [0.5, 1)
    m0 = round(frac * (1 << 31))
    if m0 == (1 << 31):                      # frac가 1로 반올림된 경우
        m0 //= 2; shift += 1
    return m0, shift

def srdhm(a, b):
    """SaturatingRoundingDoublingHighMul: round(a·b / 2^31), int32 포화 (gemmlowp 방식)."""
    a = np.asarray(a, np.int64); b = np.asarray(b, np.int64)
    ab = a * b
    nudge = np.where(ab >= 0, 1 << 30, 1 - (1 << 30))
    v = ab + nudge
    res = np.where(v >= 0, v >> 31, -((-v) >> 31))      # C의 '0 쪽으로 자르는' 나눗셈 흉내
    return np.where((a == INT32_MIN) & (b == INT32_MIN), INT32_MAX, res)

def rdbpot(x, e):
    """RoundingDivideByPOT: x / 2^e, 반올림은 half-away-from-zero."""
    x = np.asarray(x, np.int64); e = np.asarray(e, np.int64)
    mask = (np.int64(1) << e) - 1
    rem = x & mask
    thr = (mask >> 1) + (x < 0)
    return (x >> e) + (rem > thr)

def requantize(acc, m0, shift):            # shift <= 0 (M < 1)인 경우만 다룬다
    return rdbpot(srdhm(acc, m0), -np.asarray(shift))
```

아래 코드는 (위 블록에 이어서) M0/shift 분해를 확인하고, 동점(.5)에서 반올림 규칙이 어떻게 다른지 본다.

```python
for M in (0.0072, 0.3, 0.999999999):
    m0, sh = quantize_multiplier(M)
    print(f"M={M:<12} -> M0={m0:>10d} (0x{m0:08X}), shift={sh:3d}, "
          f"M0·2^(shift-31)={m0 * 2.0 ** (sh - 31):.12f}")
acc = np.array([1000, -1000, 12345, -12345, 139, -139])
m0, sh = quantize_multiplier(0.0072)
print("acc·M (float)   :", np.round(acc * 0.0072, 4))
print("requantize (int):", requantize(acc, m0, sh))
# 동점(.5) 처리: 라이브러리마다 다르면 1 LSB 차이가 난다
ties = np.array([2, -2, 6, -6, 10])                  # ×0.25 → 0.5, -0.5, 1.5, -1.5, 2.5
m0, sh = quantize_multiplier(0.25)
print("ties x0.25      :", ties * 0.25)
print("gemmlowp style  :", requantize(ties, m0, sh), " (half away from zero)")
print("np.round        :", np.round(ties * 0.25).astype(int), " (half to even)")
print("srdhm alone, a·2^30/2^31 for a=±1:", srdhm([1, -1], [1 << 30] * 2), " (ties toward +inf)")
```

```text
M=0.0072       -> M0=1979120930 (0x75F6FD22), shift= -7, M0·2^(shift-31)=0.007200000000
M=0.3          -> M0=1288490189 (0x4CCCCCCD), shift= -1, M0·2^(shift-31)=0.300000000047
M=0.999999999  -> M0=2147483646 (0x7FFFFFFE), shift=  0, M0·2^(shift-31)=0.999999999069
acc·M (float)   : [  7.2     -7.2     88.884  -88.884    1.0008  -1.0008]
requantize (int): [  7  -7  89 -89   1  -1]
ties x0.25      : [ 0.5 -0.5  1.5 -1.5  2.5]
gemmlowp style  : [ 1 -1  2 -2  3]  (half away from zero)
np.round        : [ 0  0  2 -2  2]  (half to even)
srdhm alone, a·2^30/2^31 for a=±1: [1 0]  (ties toward +inf)
```

출력에서 볼 것: M = 0.0072가 손계산과 같은 M0 = 1979120930, shift = −7로 분해되고, 재구성 오차는 10⁻¹⁰ 이하다(M0의 상대 정밀도가 약 2⁻³¹이므로). 12345 × 0.0072 = 88.884 → 89. 동점에서는 gemmlowp 방식이 −0.5 → −1, 2.5 → 3 (0에서 멀어지는 쪽)인데 `np.round`는 0.5 → 0, 2.5 → 2 (짝수 쪽, banker's rounding)다. 그리고 SRDHM 단독으로는 −0.5가 0으로 간다(동점이 +∞ 쪽) — 두 단계가 각자 규칙을 갖는다. **float 레퍼런스와 1 LSB 차이의 대부분은 이런 반올림 규칙 차이다.**

### 7.5 손으로 따라가는 int8 dense 층 (2 출력 × 3 입력)

설정: `W = [[0.5, −0.3, 0.12], [−0.35, 0.8, 0.2]]`, `b = [0.1, −0.2]`, `x = [1.0, 1.5, −0.5]`. calibration으로 입력 범위 [−1, 3], 출력 범위 [−1.5, 2.5]를 얻었다고 하자. weight는 per-channel 대칭, activation은 int8 비대칭.

```
입력  : s_x = 4/255 = 0.015686,  z_x = round(−128 + 1/s_x) = round(−64.25) = −64
출력  : s_y = 4/255,             z_y = round(−128 + 1.5/s_y) = round(−32.375) = −32
weight: s_w0 = 0.5/127 = 0.003937,  s_w1 = 0.8/127 = 0.006299
        q_w row0 = round([0.5, −0.3, 0.12]/0.003937)  = [127, −76.2→−76, 30.48→30]
        q_w row1 = round([−0.35, 0.8, 0.2]/0.006299)  = [−55.56→−56, 127, 31.75→32]
bias  : q_b0 = round(0.1 / (s_w0·s_x))  = round(0.1 / 6.1757e−5) = 1619
        q_b1 = round(−0.2 / (s_w1·s_x)) = −2024
입력  : q_x = round(x/s_x) + z_x = [63.75→64, 95.63→96, −31.88→−32] − 64 = [0, 32, −96]

오프라인: Σq_w row0 = 127 − 76 + 30 = 81    → bias_eff0 = 1619 − (−64)·81  = 6803
          Σq_w row1 = −56 + 127 + 32 = 103  → bias_eff1 = −2024 − (−64)·103 = 4568
런타임  : acc0 = 127·0 + (−76)·32 + 30·(−96) + 6803 = −2432 − 2880 + 6803 = 1491
          acc1 = (−56)·0 + 127·32 + 32·(−96) + 4568 = 4064 − 3072 + 4568  = 5560
requant : M0 = s_w0·s_x/s_y = 0.003937 (s_x = s_y라서) → 1491 × 0.003937 = 5.870 → 6  → q_y0 = 6 − 32 = −26
          M1 = 0.006299                             → 5560 × 0.006299 = 35.02 → 35 → q_y1 = 35 − 32 = 3
결과   : y0 = s_y·(−26 + 32) = 0.0941  (float 0.09)
          y1 = s_y·(3 + 32)   = 0.5490  (float 0.55)
```

아래 코드는 (7.4의 helper에 이어서) 이 손계산을 그대로 실행한다.

```python
# (앞 코드 블록의 quantize_multiplier, requantize 에 이어서)
W = np.array([[0.5, -0.3, 0.12], [-0.35, 0.8, 0.2]]); b = np.array([0.1, -0.2])
x = np.array([1.0, 1.5, -0.5])
# 1) 양자화 파라미터 (calibration으로 x ∈ [-1, 3], y ∈ [-1.5, 2.5] 를 얻었다고 가정)
s_x = 4 / 255; z_x = int(round(-128 - (-1.0) / s_x))       # int8 비대칭
s_y = 4 / 255; z_y = int(round(-128 - (-1.5) / s_y))
s_w = np.abs(W).max(axis=1) / 127                            # per-channel 대칭, z_w = 0
q_w = np.round(W / s_w[:, None]).astype(np.int64)
q_b = np.round(b / (s_w * s_x)).astype(np.int64)             # bias: int32, scale = s_w·s_x
q_x = np.clip(np.round(x / s_x) + z_x, -128, 127).astype(np.int64)
print("z_x, z_y =", z_x, z_y, "| s_w =", np.round(s_w, 6))
print("q_w =", q_w.tolist(), "| q_b =", q_b.tolist(), "| q_x =", q_x.tolist())
# 2) 오프라인: zero-point 보정항을 bias에 접어 넣는다
bias_eff = q_b - z_x * q_w.sum(axis=1)
# 3) 런타임: int8×int8 MAC → int32 누산 → requantize → +z_y → clamp
acc = q_w @ q_x + bias_eff
q_y = np.empty(2, np.int64)
for i in range(2):
    m0, sh = quantize_multiplier(s_w[i] * s_x / s_y)
    print(f"ch{i}: M={s_w[i]*s_x/s_y:.6f} -> M0={m0}, shift={sh}, acc={acc[i]}")
    q_y[i] = np.clip(requantize(acc[i], m0, sh) + z_y, -128, 127)
print("bias_eff =", bias_eff.tolist(), "| q_y =", q_y.tolist())
print("int8 result :", np.round(s_y * (q_y - z_y), 5))
print("float result:", W @ x + b)
```

```text
z_x, z_y = -64 -32 | s_w = [0.003937 0.006299]
q_w = [[127, -76, 30], [-56, 127, 32]] | q_b = [1619, -2024] | q_x = [0, 32, -96]
ch0: M=0.003937 -> M0=1082196484, shift=-7, acc=1491
ch1: M=0.006299 -> M0=1731514374, shift=-7, acc=5560
bias_eff = [6803, 4568] | q_y = [-26, 3]
int8 result : [0.09412 0.54902]
float result: [0.09 0.55]
```

출력에서 볼 것: 손계산의 모든 중간값(z, q_w, q_b, q_x, bias_eff, acc, q_y)이 그대로 나온다. 최종 오차 0.004는 출력 LSB(s_y = 0.0157)의 1/4 정도다.

### 7.6 C로 구현 — 펌웨어가 실제로 도는 모양

아래 C 코드는 위와 같은 층을 정수만으로 계산한다. 커널(`dense_s8.c`)과, 손계산 예제 + stdin 무작위 테스트 모드를 가진 main(`dense_main.c`) 두 파일이다.

```c
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- gemmlowp/TFLite 방식 고정소수점 requantize ---- */
static int32_t srdhm(int32_t a, int32_t b) {             /* round(a·b / 2^31), 포화 */
    if (a == INT32_MIN && b == INT32_MIN) return INT32_MAX;
    int64_t ab = (int64_t)a * (int64_t)b;
    int64_t nudge = ab >= 0 ? (1LL << 30) : (1 - (1LL << 30));
    return (int32_t)((ab + nudge) / (1LL << 31));        /* C 나눗셈: 0 쪽으로 자름 */
}
static int32_t rdbpot(int32_t x, int e) {                /* x / 2^e, half away from zero */
    int32_t mask = (int32_t)((1LL << e) - 1);
    int32_t rem = x & mask;
    int32_t thr = (mask >> 1) + (x < 0 ? 1 : 0);
    return (x >> e) + (rem > thr ? 1 : 0);               /* 음수 >> 는 산술 시프트라고 가정 */
}
static int32_t requant(int32_t acc, int32_t m0, int shift) {   /* shift <= 0 */
    return rdbpot(srdhm(acc, m0), -shift);
}

/* ---- int8 dense: y[o] = clamp(z_y + requant(bias_eff[o] + Σ w·x)) ---- */
static void dense_s8(const int8_t *x, const int8_t *w, const int32_t *bias_eff,
                     const int32_t *m0, const int *shift, int n_in, int n_out,
                     int32_t z_y, int32_t act_min, int32_t act_max, int8_t *y) {
    for (int o = 0; o < n_out; o++) {
        int32_t acc = bias_eff[o];                         /* 오프라인에서 z_x 보정 완료 */
        const int8_t *row = w + o * n_in;
        for (int i = 0; i < n_in; i++) acc += (int32_t)row[i] * (int32_t)x[i];
        int32_t v = requant(acc, m0[o], shift[o]) + z_y;
        if (v < act_min) v = act_min;                      /* ReLU/ReLU6도 여기 한 줄 */
        if (v > act_max) v = act_max;
        y[o] = (int8_t)v;
    }
}

/* 오프라인(변환기) 단계: bias_eff = q_b - z_x · Σ_i w[o][i] */
static void fold_bias(const int8_t *w, const int32_t *q_b, int32_t z_x,
                      int n_in, int n_out, int32_t *bias_eff) {
    for (int o = 0; o < n_out; o++) {
        int32_t sw = 0;
        for (int i = 0; i < n_in; i++) sw += w[o * n_in + i];
        bias_eff[o] = q_b[o] - z_x * sw;
    }
}
```

```c
#include "dense_s8.c"
#include <stdlib.h>

#define MAXN 256
static int run_stdin(void) {                 /* 무작위 테스트: 파이썬이 stdin으로 넣어 준다 */
    int n_in, n_out, n_vec; int32_t z_x, z_y;
    static int8_t w[MAXN * MAXN], x[MAXN], y[MAXN];
    static int32_t q_b[MAXN], be[MAXN], m0[MAXN]; static int sh[MAXN];
    if (scanf("%d %d %d %d %d", &n_in, &n_out, &n_vec, &z_x, &z_y) != 5) return 1;
    int t;
    for (int k = 0; k < n_in * n_out; k++) { if (scanf("%d", &t) != 1) return 1; w[k] = (int8_t)t; }
    for (int o = 0; o < n_out; o++) if (scanf("%d %d %d", &q_b[o], &m0[o], &sh[o]) != 3) return 1;
    fold_bias(w, q_b, z_x, n_in, n_out, be);
    for (int v = 0; v < n_vec; v++) {
        for (int i = 0; i < n_in; i++) { if (scanf("%d", &t) != 1) return 1; x[i] = (int8_t)t; }
        dense_s8(x, w, be, m0, sh, n_in, n_out, z_y, -128, 127, y);
        for (int o = 0; o < n_out; o++) printf("%d ", y[o]);
        printf("\n");
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-") == 0) return run_stdin();
    /* 본문 손계산 예제: 값은 전부 오프라인 변환기(파이썬)가 만든 것 */
    const int8_t w[2 * 3] = {127, -76, 30, -56, 127, 32};
    const int32_t q_b[2] = {1619, -2024}, m0[2] = {1082196484, 1731514374};
    const int shift[2] = {-7, -7};
    const int8_t x[3] = {0, 32, -96};
    const int32_t z_x = -64, z_y = -32;
    int32_t be[2]; int8_t y[2];
    fold_bias(w, q_b, z_x, 3, 2, be);
    dense_s8(x, w, be, m0, shift, 3, 2, z_y, -128, 127, y);
    printf("bias_eff = [%d, %d]\n", be[0], be[1]);
    printf("q_y      = [%d, %d]\n", y[0], y[1]);
    printf("y (real) = [%.5f, %.5f]\n", (4.0 / 255) * (y[0] - z_y), (4.0 / 255) * (y[1] - z_y));
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 dense_main.c -o dense -lm && ./dense
```

```text
bias_eff = [6803, 4568]
q_y      = [-26, 3]
y (real) = [0.09412, 0.54902]
```

출력에서 볼 것: numpy 정수 시뮬레이션과 같은 `bias_eff = [6803, 4568]`, `q_y = [−26, 3]`. 커널 안에는 float가 하나도 없다. float는 오프라인(변환기)에서 M0/shift와 bias를 만들 때만 쓰인다. `rdbpot`의 `x >> e`는 음수에 대해 C11에서 implementation-defined지만, GCC/Clang과 ARM 컴파일러는 산술 시프트로 처리하고 gemmlowp/TFLite도 같은 가정을 한다.

### 7.7 bit-exact 검증: numpy 정수 시뮬레이션 vs C

한 예제로는 부족하다. 아래 코드는 (7.4의 helper에 이어서) 64 입력 × 32 출력 층과 입력 200개를 무작위로 만들고, 같은 정수 데이터를 C 프로그램(`./dense -`)의 stdin으로 넣어 결과를 **비트 단위로** 비교한다. 덤으로 "float M + np.round"로 흉내 낸 결과, float 레퍼런스와도 비교한다.

```python
# (앞의 quantize_multiplier, requantize 에 이어서) 무작위 층으로 numpy-int vs C bit-exact 검사
import subprocess
rng = np.random.default_rng(3)
n_in, n_out, n_vec = 64, 32, 200
W = rng.normal(0, 0.1, (n_out, n_in)); b = rng.normal(0, 0.1, n_out)
X = rng.normal(0.3, 0.5, (n_vec, n_in))
Yf = X @ W.T + b                                               # float 레퍼런스
s_x = (X.max() - X.min()) / 255; z_x = int(round(-128 - X.min() / s_x))
s_y = (Yf.max() - Yf.min()) / 255; z_y = int(round(-128 - Yf.min() / s_y))
s_w = np.abs(W).max(1) / 127
q_w = np.round(W / s_w[:, None]).astype(np.int64)
q_b = np.round(b / (s_w * s_x)).astype(np.int64)
q_x = np.clip(np.round(X / s_x) + z_x, -128, 127).astype(np.int64)
mq = [quantize_multiplier(s_w[o] * s_x / s_y) for o in range(n_out)]
m0 = np.array([m for m, _ in mq]); sh = np.array([s for _, s in mq])   # 채널마다 M0, shift
acc = q_x @ q_w.T + (q_b - z_x * q_w.sum(1))                   # [n_vec, n_out] int32 누산
q_np = np.clip(requantize(acc, m0, sh) + z_y, -128, 127)
txt = f"{n_in} {n_out} {n_vec} {z_x} {z_y}\n" + " ".join(map(str, q_w.ravel())) + "\n"
txt += "\n".join(f"{q_b[o]} {m0[o]} {sh[o]}" for o in range(n_out)) + "\n"
txt += "\n".join(" ".join(map(str, r)) for r in q_x) + "\n"
out = subprocess.run(["./dense", "-"], input=txt, capture_output=True, text=True, check=True).stdout
q_c = np.array([list(map(int, l.split())) for l in out.strip().splitlines()])
print("numpy-int vs C mismatches:", int((q_np != q_c).sum()), "/", q_c.size)
naive = np.clip(np.round(acc * (s_w * s_x / s_y)) + z_y, -128, 127)   # float M + np.round
print("numpy-int vs float-M np.round mismatches:", int((q_np != naive).sum()))
err_lsb = (s_y * (q_c - z_y) - Yf) / s_y
print(f"int8 vs float ref: max |err| = {np.abs(err_lsb).max():.2f} LSB, "
      f"mean |err| = {np.abs(err_lsb).mean():.3f} LSB (s_y = {s_y:.5f})")
```

```text
numpy-int vs C mismatches: 0 / 6400
numpy-int vs float-M np.round mismatches: 10
int8 vs float ref: max |err| = 1.71 LSB, mean |err| = 0.353 LSB (s_y = 0.01327)
```

출력에서 볼 것:

- numpy 정수 시뮬레이션과 C가 6400개 출력 **전부 일치**(0 mismatch). 이것이 C8의 "golden model" 방법이다: 파이썬으로 기기 커널과 **같은 정수 산술**을 흉내 내면, 기기 출력과 bit-exact로 비교할 수 있다.
- "float M에 np.round" 방식은 10개가 1 LSB 다르다. 수학적으로 거의 같은 계산이어도 반올림 위치·규칙이 다르면 bit-exact가 아니다. 레퍼런스를 만들 때는 **대상 커널의 반올림을 그대로** 흉내 내야 한다. (일부 라이브러리는 두 번 반올림 대신 64-bit에서 한 번만 반올림하는 변형도 제공한다 → 역시 1 LSB 차이의 원인.)
- float 레퍼런스와는 평균 0.35 LSB, 최대 1.71 LSB 차이. 입력·weight 양자화 오차까지 포함된 것이라 1 LSB를 넘을 수 있다. 이 차이는 bit-exact가 아니라 **tolerance**로 비교한다 (C8).

### 7.8 Don 경험과 연결 + 함정

- 펌웨어의 Q15 곱 `(int32_t)a * b >> 15` + 포화와 구조가 완전히 같다. 다른 건 "시프트 양이 고정 15"가 아니라 "채널마다 M0와 shift가 테이블로 온다"는 것뿐이다.
- per-channel이면 `m0[o]`, `shift[o]`를 배열로 들고 다닌다 (CMSIS-NN의 per-channel quant params가 이 모양이다, 11절).
- **함정 1**: bias를 `s_w·s_x`가 아닌 다른 scale로 양자화하면 누산기에 더할 수 없다. 변환기 버그의 단골.
- **함정 2**: `bias_eff`를 계산할 때 z_x 부호를 틀린다 (`q_b + z_x·Σq_w`). 증상: 모든 출력이 채널별로 일정하게 밀린다.
- **함정 3**: M ≥ 1인 경우(드물지만 가능)를 처리 안 한다. 이때 shift > 0이라 곱하기 전에 왼쪽 시프트를 해야 한다 (TFLite의 `MultiplyByQuantizedMultiplier`가 이 경우를 처리한다). 이 노트의 코드는 M < 1만 다룬다.
- **함정 4**: 누산 순서·SIMD 폭이 달라도 정수 덧셈은 결합법칙이 성립하므로(오버플로가 없는 한) 결과가 같다. float 레퍼런스와 달리 **정수 커널은 bit-exact 검증이 가능하다** — 이게 펌웨어 검증 관점에서 큰 장점이다.

---

## 8. 활성화 함수를 양자화 도메인에서 — fused clamp와 LUT

### 8.1 ReLU/ReLU6 = clamp 경계

B1 4.2절에서 "int8에서 ReLU는 공짜"라고 했다. 이제 정확히 계산할 수 있다. 출력 텐서의 (s_y, z_y)가 정해져 있으면 실수 경계를 정수로 옮기면 된다.

```
실수 0   → 정수 z_y
실수 6   → 정수 z_y + round(6 / s_y)

ReLU  : act_min = max(−128, z_y),  act_max = 127
ReLU6 : act_min = max(−128, z_y),  act_max = min(127, z_y + round(6/s_y))
없음  : act_min = −128,            act_max = 127
```

말로 하면: 7.6 C 코드의 마지막 clamp 두 줄에 이 경계를 넣으면 활성화가 끝난다. 추가 명령 0개.

손계산:

- 출력을 ReLU6 뒤에서 calibration했다면 범위가 [0, 6] → `s_y = 6/255, z_y = −128` → ReLU6 경계가 [−128, 127] = 타입 전체. clamp가 이미 하는 일이라 **완전히 공짜**.
- 같은 층을 calibration할 때 ReLU6가 빠져서 범위가 [0, 8]로 잡혔다면 → `s_y = 8/255, z_y = −128` → `act_max = −128 + round(6 × 255/8) = −128 + 191 = 63`. 64..127 구간(= 실수 6~8)을 버리는 셈이라 해상도 약 25% 낭비. **calibration은 활성화까지 포함한 텐서에서 해야 한다.**
- 10절의 end-to-end 예제는 ReLU를 `lo = max(−128, z_out)` 한 줄로 처리한다.

### 8.2 복잡한 활성화: 256칸 LUT

sigmoid, tanh, hard-swish, GELU 같은 elementwise 함수는 int8 입력이 256가지뿐이라는 점을 이용한다: 입력 (s_x, z_x)와 출력 (s_y, z_y)가 고정이면 256개 입력 각각에 대해 `q_y = quantize(f(dequantize(q_x)))`를 **오프라인에서 계산해 표로** 만든다. 런타임은 `y = lut[x + 128]` 한 번. B1 4.3의 LUT sigmoid와 같은 아이디어인데, int8 도메인에서는 근사가 아니라 "그 양자화 파라미터에서 가능한 최선의 답"과 정확히 같다. 많은 int8 런타임과 NPU가 이런 LUT 방식을 쓴다 (구체적 구현은 제품마다 다름).

### 8.3 함정

- residual add(B4, B6)는 두 입력의 scale이 다르다 → 두 입력을 각각 requantize해 공통 scale로 맞춘 뒤 더한다. "정수 둘을 그냥 더하면 된다"는 착각이 흔하다.
- concat도 입력마다 scale이 다르면 requantize가 필요하다. 변환기가 concat 입력들의 scale을 강제로 같게 맞추는 이유다.
- softmax·LayerNorm처럼 reduction이 있는 연산은 int8만으로 정확히 하기 어려워 int32/고정소수점 내부 계산이나 float fallback을 쓴다 (B1 6.6, C6).

---

## 9. Calibration — activation 범위를 어떻게 정하나

### 9.1 문제: clipping 오차 vs rounding 오차

weight는 배포 전에 값이 다 정해져 있어서 범위를 그냥 재면 된다. activation은 입력마다 다르므로 **대표 입력 수백~수천 개(calibration set)**를 모델에 흘려 각 층 출력의 분포를 모으고, 거기서 clip 임계값 c(→ scale)를 정한다. 이게 calibration이다.

c를 정하는 건 줄다리기다.

- c를 **크게**(min-max): 아무것도 안 잘리지만 눈금 s = c/127이 굵어져 **모든 값**의 rounding 오차가 커진다.
- c를 **작게**: 눈금이 촘촘해지지만 c 밖의 값은 잘려서 **드문 값**의 clipping 오차가 커진다 (2.5절의 5.0 → 3.0처럼).

전체 오차 ≈ (c 안쪽 값들의 s²/12) + (c 바깥 값들의 (x − c)² 합). 첫 항은 c²에 비례해 증가, 둘째 항은 c가 커질수록 감소 → **중간 어딘가에 최적점**이 있다. 꼬리가 두꺼운 분포일수록 min-max와 최적점이 멀어진다.

### 9.2 방법 다섯 가지

| 방법 | 어떻게 | 장점 | 단점 |
|---|---|---|---|
| min-max | 관측된 최소·최대 | 단순, 안 잘림 | outlier 하나가 scale을 망침 |
| moving-average min-max | 배치별 min/max의 지수이동평균 | 튀는 배치에 덜 민감, QAT에서 흔함 | 여전히 꼬리 무시 못 함 |
| percentile | `abs(x)`의 99.9% / 99.99% 지점 | 간단하고 효과적 | 몇 %로 할지는 hyperparameter |
| MSE 최적 | c 후보를 훑어 `mean((x − Q(x))²)` 최소 | 목표가 명확 | 탐색 비용, MSE가 정확도와 같진 않음 |
| KL (entropy) | 원래 히스토그램과 양자화 히스토그램의 KL 최소 | TensorRT가 유명하게 만든 방법, 분포 모양 보존 | 구현이 복잡, 히스토그램 해상도 의존 |

보통 weight는 min-max(per-channel)로 충분하고, activation에 percentile/MSE/KL을 쓴다. 저비트(int4) weight에서는 weight에도 MSE 탐색을 쓰기도 한다.

### 9.3 코드로 확인: 꼬리가 두꺼운 activation

아래 코드는 t 분포(자유도 4, 꼬리가 Gaussian보다 두껍다) 20만 개를 int8 대칭으로 양자화하면서 min-max, percentile, MSE 탐색으로 정한 c를 비교한다.

```python
import numpy as np
rng = np.random.default_rng(4)
x = rng.standard_t(df=4, size=200_000)            # 꼬리가 두꺼운 activation (가정)

def fq(x, c, qmax=127):                            # 대칭 int8, clip 임계값 c
    s = c / qmax
    return np.clip(np.round(x / s), -qmax, qmax) * s

def mse(c): return np.mean((x - fq(x, c)) ** 2)

a = np.abs(x)
cands = {"min-max": a.max(),
         "percentile 99.99": np.percentile(a, 99.99),
         "percentile 99.9": np.percentile(a, 99.9)}
grid = np.linspace(0.5, a.max(), 400)              # MSE 최적: 후보 c를 쭉 훑는다
cands["MSE search"] = grid[np.argmin([mse(c) for c in grid])]
for name, c in cands.items():
    xh = fq(x, c)
    clipped = np.mean(a > c)
    print(f"{name:17s} c={c:7.3f}  scale={c/127:.5f}  clipped={clipped:.4%}  "
          f"MSE={mse(c):.3e}  SQNR={10*np.log10(np.mean(x**2)/mse(c)):5.2f} dB")
```

```text
min-max           c= 38.996  scale=0.30706  clipped=0.0000%  MSE=7.892e-03  SQNR=23.97 dB
percentile 99.99  c= 13.286  scale=0.10461  clipped=0.0100%  MSE=5.906e-03  SQNR=25.23 dB
percentile 99.9   c=  8.548  scale=0.06731  clipped=0.1000%  MSE=1.479e-02  SQNR=21.24 dB
MSE search        c= 21.147  scale=0.16651  clipped=0.0010%  MSE=4.010e-03  SQNR=26.91 dB
```

출력에서 볼 것: min-max는 c = 39로 가장 큰 값 하나에 맞추느라 눈금이 0.31이나 된다(값의 99.99%가 13 이하인데). MSE 탐색은 c = 21에서 0.001%만 자르고 SQNR을 3 dB 올렸다. percentile 99.99%도 min-max보다 낫지만, 99.9%는 0.1%를 잘라서 오히려 min-max보다 **나쁘다** — percentile은 "몇 %냐"에 민감하다.

### 9.4 KL (entropy) calibration — TensorRT 방식의 뼈대

아이디어(Migacz, 2017): 원래 activation 분포 P와, 그걸 c에서 자르고 128레벨로 뭉갠 분포 Q가 **정보 관점에서 가장 비슷한** c를 고른다. 거리로 A2 8.2절의 KL divergence `KL(P‖Q) = Σ P·log(P/Q)`를 쓴다.

```
1) abs(x)의 히스토그램 (2048 bin)
2) 후보 i (128..2048): P = hist[0:i], 단 i 바깥 bin의 개수를 P의 마지막 bin에 더한다 (clip의 효과)
3) Q = hist[0:i]를 128개 덩어리로 합친 뒤, 각 덩어리의 합을 그 안의 0 아닌 bin들에 고르게 다시 펼친다 (양자화의 효과)
4) KL(P‖Q)가 최소인 i → c = i번째 bin 경계
```

아래 코드는 (9.3 코드에 이어서) 이 뼈대를 구현한다. 실제 TensorRT 구현은 세부(bin 수, 후보 간격, 0 처리 등)가 다를 수 있다.

```python
# (앞 코드 블록의 x, fq, mse 에 이어서) TensorRT 식 KL calibration의 핵심만 구현
def kl_threshold(a, n_bins=2048, n_quant=128):
    hist, edges = np.histogram(a, bins=n_bins, range=(0, a.max()))
    best_kl, best_i = np.inf, None
    for i in range(n_quant, n_bins + 1, 8):              # 후보 임계값 = edges[i]
        p = hist[:i].astype(np.float64).copy()
        p[-1] += hist[i:].sum()                            # 잘릴 값들은 마지막 bin에 몰아 넣는다
        q = np.zeros(i)
        for chunk in np.array_split(np.arange(i), n_quant):   # i개 bin → 128 레벨로 뭉갠다
            nz = chunk[hist[chunk] > 0]
            if nz.size: q[nz] = hist[chunk].sum() / nz.size    # 0이 아닌 bin에 고르게 펼침
        p /= p.sum(); q /= q.sum()
        m = p > 0
        kl = np.sum(p[m] * np.log(p[m] / np.maximum(q[m], 1e-12)))
        if kl < best_kl: best_kl, best_i = kl, i
    return edges[best_i]

c_kl = kl_threshold(np.abs(x))
print(f"KL (entropy)      c={c_kl:7.3f}  clipped={np.mean(np.abs(x) > c_kl):.4%}  "
      f"MSE={mse(c_kl):.3e}  SQNR={10*np.log10(np.mean(x**2)/mse(c_kl)):5.2f} dB")
```

```text
KL (entropy)      c= 13.100  clipped=0.0110%  MSE=6.038e-03  SQNR=25.13 dB
```

출력에서 볼 것: KL은 c = 13.1을 골랐다 — percentile 99.99%(13.3)와 거의 같은 자리. MSE 최적(21)보다 더 공격적으로 자른다. KL은 "분포 모양"을, MSE는 "값 오차의 제곱합"을 본다. 어느 쪽이 최종 정확도에 좋은지는 **모델마다 다르다**. 실무에서는 몇 가지를 돌려 보고 검증 정확도(C8)로 고른다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 310">
<line x1="70" y1="250" x2="620" y2="250" stroke="currentColor"/> <line x1="70" y1="40" x2="70" y2="250" stroke="currentColor"/> <line x1="66" y1="250.0" x2="70" y2="250.0" stroke="currentColor"/><text x="62" y="254.0" font-size="12" text-anchor="end">1e-5</text> <line x1="66" y1="208.0" x2="70" y2="208.0" stroke="currentColor"/><text x="62" y="212.0" font-size="12" text-anchor="end">1e-4</text> <line x1="66" y1="166.0" x2="70" y2="166.0" stroke="currentColor"/><text x="62" y="170.0" font-size="12" text-anchor="end">1e-3</text> <line x1="66" y1="124.0" x2="70" y2="124.0" stroke="currentColor"/><text x="62" y="128.0" font-size="12" text-anchor="end">1e-2</text> <line x1="66" y1="82.0" x2="70" y2="82.0" stroke="currentColor"/><text x="62" y="86.0" font-size="12" text-anchor="end">1e-1</text> <line x1="66" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="62" y="44.0" font-size="12" text-anchor="end">1e0</text>
<line x1="112.6" y1="250" x2="112.6" y2="254" stroke="currentColor"/><text x="112.6" y="268" font-size="12" text-anchor="middle">5</text> <line x1="183.7" y1="250" x2="183.7" y2="254" stroke="currentColor"/><text x="183.7" y="268" font-size="12" text-anchor="middle">10</text> <line x1="254.7" y1="250" x2="254.7" y2="254" stroke="currentColor"/><text x="254.7" y="268" font-size="12" text-anchor="middle">15</text> <line x1="325.8" y1="250" x2="325.8" y2="254" stroke="currentColor"/><text x="325.8" y="268" font-size="12" text-anchor="middle">20</text> <line x1="396.8" y1="250" x2="396.8" y2="254" stroke="currentColor"/><text x="396.8" y="268" font-size="12" text-anchor="middle">25</text> <line x1="467.9" y1="250" x2="467.9" y2="254" stroke="currentColor"/><text x="467.9" y="268" font-size="12" text-anchor="middle">30</text> <line x1="538.9" y1="250" x2="538.9" y2="254" stroke="currentColor"/><text x="538.9" y="268" font-size="12" text-anchor="middle">35</text>
<line x1="610.0" y1="250" x2="610.0" y2="254" stroke="currentColor"/><text x="610.0" y="268" font-size="12" text-anchor="middle">40</text> <polyline points="70.0,60.9 77.1,67.4 84.2,73.3 91.3,78.7 98.4,83.8 105.5,88.4 112.6,92.8 119.7,96.9 126.8,100.7 133.9,104.3 141.1,107.8 148.2,111.1 155.3,114.2 162.4,117.1 169.5,119.8 176.6,122.3 183.7,124.6 190.8,126.8 197.9,128.9 205.0,130.8 212.1,132.5 219.2,134.2 226.3,135.8 233.4,137.3 240.5,138.8 247.6,140.2 254.7,141.6 261.8,142.9 268.9,144.2 276.1,145.4 283.2,146.6 290.3,147.8 297.4,149.0 304.5,150.2 311.6,151.3 318.7,152.5 325.8,153.6 332.9,154.8 340.0,156.0 347.1,157.2 354.2,158.4 361.3,159.7 368.4,161.0 375.5,162.3 382.6,163.6 389.7,165.0 396.8,166.3 403.9,167.7 411.1,169.1 418.2,170.5 425.3,172.0 432.4,173.6 439.5,175.2 446.6,176.9 453.7,178.7 460.8,180.5 467.9,182.5 475.0,184.6 482.1,186.8 489.2,189.2 496.3,191.7 503.4,194.4 510.5,197.3 517.6,200.5 524.7,204.0 531.8,207.8 538.9,212.1 546.1,217.0 553.2,222.6 560.3,229.3 567.4,237.4 574.5,247.9" fill="none" stroke="#e08a3c" stroke-width="2" stroke-dasharray="6 4"/>
<polyline points="70.0,239.0 77.1,229.9 84.2,222.7 91.3,216.8 98.4,211.7 105.5,207.4 112.6,203.5 119.7,200.0 126.8,196.8 133.9,193.8 141.1,191.1 148.2,188.6 155.3,186.2 162.4,183.9 169.5,181.9 176.6,179.9 183.7,178.0 190.8,176.3 197.9,174.6 205.0,173.0 212.1,171.4 219.2,169.9 226.3,168.4 233.4,167.1 240.5,165.8 247.6,164.5 254.7,163.2 261.8,162.1 268.9,160.9 276.1,159.8 283.2,158.7 290.3,157.6 297.4,156.6 304.5,155.5 311.6,154.6 318.7,153.8 325.8,152.8 332.9,151.9 340.0,151.0 347.1,150.1 354.2,149.3 361.3,148.4 368.4,147.6 375.5,146.8 382.6,146.1 389.7,145.4 396.8,144.6 403.9,143.9 411.1,143.2 418.2,142.5 425.3,141.8 432.4,141.1 439.5,140.4 446.6,139.8 453.7,139.2 460.8,138.6 467.9,138.0 475.0,137.4 482.1,136.8 489.2,136.2 496.3,135.6 503.4,135.1 510.5,134.5 517.6,134.0 524.7,133.4 531.8,132.9 538.9,132.3 546.1,131.8 553.2,131.3 560.3,130.8 567.4,130.3 574.5,129.8 581.6,129.3 588.7,128.8 595.8,128.3" fill="none" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="2 3"/>
<polyline points="70.0,60.9 77.1,67.4 84.2,73.3 91.3,78.7 98.4,83.7 105.5,88.4 112.6,92.7 119.7,96.8 126.8,100.6 133.9,104.2 141.1,107.6 148.2,110.8 155.3,113.8 162.4,116.6 169.5,119.2 176.6,121.5 183.7,123.7 190.8,125.7 197.9,127.4 205.0,129.1 212.1,130.5 219.2,131.8 226.3,133.0 233.4,134.1 240.5,135.0 247.6,135.9 254.7,136.7 261.8,137.4 268.9,138.0 276.1,138.6 283.2,139.0 290.3,139.4 297.4,139.7 304.5,140.0 311.6,140.3 318.7,140.5 325.8,140.6 332.9,140.6 340.0,140.7 347.1,140.6 354.2,140.6 361.3,140.6 368.4,140.4 375.5,140.3 382.6,140.2 389.7,140.0 396.8,139.8 403.9,139.5 411.1,139.2 418.2,138.9 425.3,138.6 432.4,138.2 439.5,137.9 446.6,137.6 453.7,137.2 460.8,136.9 467.9,136.5 475.0,136.0 482.1,135.6 489.2,135.2 496.3,134.8 503.4,134.4 510.5,133.9 517.6,133.5 524.7,133.0 531.8,132.6 538.9,132.1 546.1,131.6 553.2,131.2 560.3,130.7 567.4,130.2 574.5,129.7 581.6,129.3 588.7,128.8 595.8,128.3" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="595.7" cy="128.3" r="4" fill="#d0564a"/> <circle cx="230.4" cy="133.6" r="4" fill="#d0564a"/> <circle cx="163.1" cy="116.9" r="4" fill="#d0564a"/> <circle cx="342.1" cy="140.7" r="4" fill="#d0564a"/> <circle cx="227.7" cy="133.2" r="4" fill="#d0564a"/> <text x="585.7" y="118.3" font-size="12" text-anchor="end">min-max</text> <text x="238.4" y="121.6" font-size="12" text-anchor="start">p99.99</text> <text x="155.1" y="108.9" font-size="12" text-anchor="end">p99.9</text> <text x="342.1" y="128.7" font-size="12" text-anchor="middle">MSE opt</text> <text x="219.7" y="149.2" font-size="12" text-anchor="end">KL</text> <text x="345" y="290" font-size="12" text-anchor="middle">clip 임계값 c (t분포 df=4, σ≈1.41)</text> <text x="20" y="30" font-size="12">MSE (log)</text> <line x1="430" y1="60" x2="460" y2="60" stroke="#4a7bd0" stroke-width="2"/><text x="466" y="64" font-size="12">전체 MSE</text> <line x1="430" y1="78" x2="460" y2="78" stroke="#e08a3c" stroke-width="2" stroke-dasharray="6 4"/><text x="466" y="82" font-size="12">clipping 오차 (|x| &gt; c)</text>
<line x1="430" y1="96" x2="460" y2="96" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="2 3"/><text x="466" y="100" font-size="12">rounding 오차 (|x| ≤ c)</text>
</svg>
```

그림 6 — clip 임계값 c에 따른 MSE (위 데이터로 실제 계산, 로그 축). 주황 점선(clipping 오차)은 c가 커질수록 줄고, 초록 점선(rounding 오차)은 c²에 비례해 는다. 파랑(합)은 c ≈ 21에서 최소. 빨간 점은 각 calibration 방법이 고른 c다.

### 9.5 moving-average min-max와 도구 확인 (PyTorch observer)

PyTorch의 `torch.ao.quantization`은 calibration 방법을 **observer**라는 모듈로 제공한다. 아래 코드는 (1) 2.4절 손계산을 `MinMaxObserver`로 확인하고, (2) 200개 배치 중 하나만 10배 튀었을 때 min-max와 moving average의 차이를 보고, (3) per-channel 대칭 observer가 7.5절 weight에 대해 무엇을 내는지 본다.

```python
import torch
from torch.ao.quantization.observer import (MinMaxObserver, MovingAverageMinMaxObserver,
                                            PerChannelMinMaxObserver)
x = torch.tensor([-1.0, 0.0, 0.5, 1.23, 3.0])
obs = MinMaxObserver(dtype=torch.quint8, qscheme=torch.per_tensor_affine)
obs(x); s, z = obs.calculate_qparams()
print(f"MinMax uint8 affine     : scale={s.item():.6f}, zero_point={z.item()}")
# moving average: 배치마다 min/max를 지수이동평균 (c=0.01) → 튀는 배치 하나에 덜 흔들린다
ma = MovingAverageMinMaxObserver(averaging_constant=0.01, dtype=torch.quint8)
torch.manual_seed(0)
for i in range(200):
    batch = torch.randn(256) * (10.0 if i == 100 else 1.0)       # 100번째 배치만 10배 튄다
    ma(batch)
mm = MinMaxObserver(dtype=torch.quint8)
torch.manual_seed(0)
for i in range(200):
    mm(torch.randn(256) * (10.0 if i == 100 else 1.0))
print(f"MinMax         range    : [{mm.min_val.item():.2f}, {mm.max_val.item():.2f}]")
print(f"MovingAvg(0.01) range   : [{ma.min_val.item():.2f}, {ma.max_val.item():.2f}]")
w = torch.tensor([[0.5, -0.3, 0.12], [-0.35, 0.8, 0.2]])
pc = PerChannelMinMaxObserver(ch_axis=0, dtype=torch.qint8, qscheme=torch.per_channel_symmetric)
pc(w); s, z = pc.calculate_qparams()
print("PerChannel int8 symmetric: scale =", [round(v, 6) for v in s.tolist()], "zp =", z.tolist())
print("  vs max|w|/127           :", [round(v, 6) for v in (w.abs().amax(1) / 127).tolist()])
```

```text
MinMax uint8 affine     : scale=0.015686, zero_point=64
MinMax         range    : [-29.56, 26.64]
MovingAvg(0.01) range   : [-2.90, 3.00]
PerChannel int8 symmetric: scale = [0.003922, 0.006275] zp = [0, 0]
  vs max|w|/127           : [0.003937, 0.006299]
```

출력에서 볼 것: `MinMaxObserver`가 손계산과 같은 s = 0.015686, z = 64를 낸다. 튀는 배치 하나 때문에 min-max 범위는 [−29.6, 26.6]으로 10배 넓어졌지만, 이동평균(계수 0.01)은 그 배치를 서서히 잊어 [−2.9, 3.0]에 머문다. PyTorch의 per-channel 대칭 scale은 `max/127.5`(범위 −128..127)라서 TFLite식 `max/127`과 0.4% 다르다 — 관례 차이(4.2절). 서로 다른 도구의 결과를 bit-exact로 맞추려면 이런 관례까지 맞춰야 한다.

### 9.6 Don 경험과 연결 + 함정

- calibration = 센서의 gain/offset 캘리브레이션과 같은 문제다. **대표성이 없는 데이터로 하면 기기에서 틀린다.** 예를 들어 wake word 모델을 조용한 실내 녹음만으로 calibration하면, 시끄러운 환경의 큰 feature 값이 전부 포화된다.
- calibration set은 보통 수백 개면 충분하지만, 기기의 실제 입력 전처리(펌웨어 feature 추출과 **같은 코드**)를 거친 데이터여야 한다.
- calibration 전에 BN folding(B1 7절)과 activation fusion을 먼저 해야 한다. 접힌 뒤의 텐서가 실제로 양자화되는 텐서다.

---

## 10. 작은 모델을 손으로 끝까지 양자화하기

지금까지의 부품(per-channel weight, calibration, bias 양자화, requantize, fused ReLU)을 모두 조립해서 학습된 모델 하나를 **정수 전용**으로 돌리고 정확도를 잰다. 도구 없이 numpy만 쓴다 (도구를 쓰는 절차는 C2).

### 10.1 모델 학습 (float)

아래 코드는 16차원 합성 feature(예: IMU 윈도우 통계라고 가정)로 4-class를 분류하는 MLP 16 → 64 → 32 → 4를 학습한다.

```python
import numpy as np, torch
torch.manual_seed(0); rng = np.random.default_rng(5)
# 합성 과제: 16차원 feature(예: IMU 윈도우 통계라고 가정) → 4 class, 비선형 경계
def make(n):
    X = rng.normal(0, 1, (n, 16)).astype(np.float32)
    score = np.stack([X[:, 0] * X[:, 1], np.sin(2 * X[:, 2]) + X[:, 3],
                      X[:, 4] ** 2 - 1, -X[:, 5] + 0.5 * X[:, 6] * X[:, 7]], 1)
    return X, score.argmax(1)
Xtr, ytr = make(4000); Xte, yte = make(2000)
model = torch.nn.Sequential(torch.nn.Linear(16, 64), torch.nn.ReLU(),
                            torch.nn.Linear(64, 32), torch.nn.ReLU(), torch.nn.Linear(32, 4))
opt = torch.optim.Adam(model.parameters(), lr=3e-3)
Xt, yt = torch.from_numpy(Xtr), torch.from_numpy(ytr)
for ep in range(300):                               # full-batch 학습 (작아서 충분)
    opt.zero_grad(); loss = torch.nn.functional.cross_entropy(model(Xt), yt); loss.backward(); opt.step()
Ws = [m.weight.detach().double().numpy() for m in model if isinstance(m, torch.nn.Linear)]
bs = [m.bias.detach().double().numpy() for m in model if isinstance(m, torch.nn.Linear)]
def float_forward(X):
    h = X.astype(np.float64)
    for k, (W, b) in enumerate(zip(Ws, bs)):
        h = h @ W.T + b
        if k < 2: h = np.maximum(h, 0)
    return h
print(f"train loss {loss.item():.3f} | float test acc = {np.mean(float_forward(Xte).argmax(1) == yte):.4f}")
```

```text
train loss 0.022 | float test acc = 0.8805
```

float 정확도 88%가 기준선이다 (train loss가 매우 낮아 약간 overfit이지만, 여기서는 양자화 전후 비교만 본다).

### 10.2 정수 전용 추론

아래 코드는 (위 모델과 7.4절 helper에 이어서) calibration → 층별 (s, z) → per-channel weight 양자화 → int32 bias → 정수 MAC → requantize → fused ReLU를 구현하고, weight 비트 수와 granularity를 바꿔 가며 정확도를 잰다. activation은 모두 int8 per-tensor 비대칭(min-max)이다.

```python
# (앞 블록의 모델 + 7절 quantize_multiplier/requantize 에 이어서) 정수 전용 추론
def act_qparams(t):                                   # int8 비대칭, min-max
    lo, hi = min(t.min(), 0.0), max(t.max(), 0.0)
    s = (hi - lo) / 255; return s, int(round(-128 - lo / s))
calib = Xtr[:500].astype(np.float64); acts = [calib]; h = calib    # 1) calibration: 500 샘플
for k, (W, b) in enumerate(zip(Ws, bs)):
    h = h @ W.T + b; h = np.maximum(h, 0) if k < 2 else h; acts.append(h)
aq = [act_qparams(t) for t in acts]                   # 층 경계마다 (s, z)

def int_forward(X, wbits=8, per_channel=True):
    qmax = 2 ** (wbits - 1) - 1
    s_in, z_in = aq[0]
    q = np.clip(np.round(X / s_in) + z_in, -128, 127).astype(np.int64)
    for k, (W, b) in enumerate(zip(Ws, bs)):
        amax = np.abs(W).max(1) if per_channel else np.full(W.shape[0], np.abs(W).max())
        s_w = amax / qmax
        q_w = np.clip(np.round(W / s_w[:, None]), -qmax, qmax).astype(np.int64)
        q_b = np.round(b / (s_w * s_in)).astype(np.int64)
        s_out, z_out = aq[k + 1]
        acc = q @ q_w.T + (q_b - z_in * q_w.sum(1))
        mq = [quantize_multiplier(m) for m in s_w * s_in / s_out]
        y = requantize(acc, np.array([m for m, _ in mq]), np.array([e for _, e in mq])) + z_out
        lo = max(-128, z_out) if k < 2 else -128      # ReLU = 하한을 z_out(실수 0)로 올린 clamp
        q, s_in, z_in = np.clip(y, lo, 127), s_out, z_out
    return s_in * (q - z_in)                          # 마지막 logits만 dequant

ref = float_forward(Xte)
print("act (s, z):", [(round(float(s), 4), z) for s, z in aq])
for wb, pc in [(8, True), (8, False), (4, True), (4, False)]:
    out = int_forward(Xte, wb, pc)
    sq = 10 * np.log10(np.sum(ref ** 2) / np.sum((ref - out) ** 2))
    print(f"W int{wb} {'per-channel' if pc else 'per-tensor '} A int8: acc = {np.mean(out.argmax(1) == yte):.4f}, "
          f"agree w/ float = {np.mean(out.argmax(1) == ref.argmax(1)):.4f}, logit SQNR = {sq:5.1f} dB")
```

```text
act (s, z): [(0.0277, -2), (0.0213, -128), (0.0548, -128), (0.3091, -26)]
W int8 per-channel A int8: acc = 0.8820, agree w/ float = 0.9885, logit SQNR =  32.5 dB
W int8 per-tensor  A int8: acc = 0.8850, agree w/ float = 0.9905, logit SQNR =  31.5 dB
W int4 per-channel A int8: acc = 0.8625, agree w/ float = 0.9220, logit SQNR =  16.6 dB
W int4 per-tensor  A int8: acc = 0.8305, agree w/ float = 0.8730, logit SQNR =  12.0 dB
```

출력에서 볼 것:

- 층 경계의 (s, z): 입력은 거의 대칭이라 z = −2, ReLU 뒤 두 hidden은 z = −128 (실수 0 = int8 최솟값, 4.5절), logits는 z = −26.
- **W int8 per-channel / A int8**: 정확도 88.2% (float 88.05%). 차이는 잡음 수준이고 float와 예측이 98.9% 일치, logit SQNR 32.5 dB. 이 크기의 모델에서 int8 PTQ는 **사실상 무손실**이라는 흔한 경험칙 그대로다.
- **W int8 per-tensor**도 이 모델에서는 멀쩡하다(31.5 dB). BN folding이나 outlier 채널이 없는 작은 MLP라 채널 간 크기 차이가 작기 때문이다. 5.2절 같은 outlier 채널, depthwise conv, BN folding 뒤의 weight에서는 per-tensor가 무너진다.
- **W int4**: per-channel 86.3%(−1.8%p, SQNR 16.6 dB), per-tensor 83.1%(−5%p, 12.0 dB). 비트 4개를 빼면 SQNR이 약 16 dB 떨어진다 — 3절의 "비트당 6 dB"와 대략 맞는다(activation noise도 섞여 있어서 정확히 24 dB는 아니다). int4에서 granularity가 중요해진다. 이 정도 손실을 되찾는 것이 QAT(C2)와 GPTQ/AWQ(C3)의 몫이다.

### 10.3 이 실험이 C8로 이어지는 방식

`int_forward`는 사실상 **기기 커널의 golden model**이다. 7.7절처럼 C 커널과 bit-exact로 맞춰 두면, 기기에서 정확도가 떨어졌을 때 "양자화 자체의 손실"(float vs int_forward)과 "구현 버그"(int_forward vs 기기)를 분리할 수 있다. 펌웨어에서 RTL 시뮬레이션 모델과 실리콘을 비교하던 것과 같은 구조다.

---

## 11. 임베디드 관점에서 다시 보기

### 11.1 TFLite int8 양자화 관례 (확실한 것만)

| 항목 | 관례 |
|---|---|
| activation | int8 비대칭, per-tensor, 범위 −128..127, zero-point는 아무 정수 |
| weight | int8 대칭, zero-point = 0, 범위 −127..127, conv/dense는 per-channel(출력 채널 축) 권장 |
| bias | int32, zero-point = 0, scale = s_input × s_weight (채널마다) |
| requantize | 실수 multiplier를 (int32 multiplier, shift)로 표현, gemmlowp식 반올림 |
| 활성화 | ReLU/ReLU6 등은 op의 fused activation 옵션 → 출력 clamp 경계 |
| 16x8 모드 | activation int16 대칭 + weight int8 (지원 op 제한) |

### 11.2 CMSIS-NN (일반적인 수준)

CMSIS-NN은 Arm Cortex-M용 신경망 커널 라이브러리다. 현재의 int8(`_s8`) 커널들(예: fully connected, convolve)은 위 TFLite 관례를 그대로 따른다. 인자 구조체에 input/output offset(zero-point의 부호 반전 또는 그 자체), activation min/max(= 8절의 clamp 경계), 그리고 per-channel 커널이면 **채널별 multiplier 배열과 shift 배열**이 들어간다. 즉 7.6절 `dense_s8`의 인자 목록과 개념적으로 같다. 코어에 DSP 확장이나 Helium(MVE)이 있으면 같은 API 아래에서 SIMD 경로가 선택된다. 정확한 함수 시그니처는 버전마다 바뀌므로 CMSIS-NN 저장소의 헤더를 보고 쓰자.

TFLite Micro가 모델을 해석하면서 이 CMSIS-NN 커널을 부르는 구조가 흔하다 (F 모듈). 변환기(TFLite converter)가 이 노트에서 손으로 한 모든 계산 — scale, zero-point, int32 bias, M0/shift — 을 오프라인에서 하고 `.tflite` 파일에 넣어 둔다.

### 11.3 메모리 예산 — 10절 MLP (16 → 64 → 32 → 4)

| 항목 | 개수 | float32 | int8 배포 |
|---|---|---|---|
| weight | 1024 + 2048 + 128 = 3200 | 12,800 B | 3,200 B (int8) |
| bias | 64 + 32 + 4 = 100 | 400 B | 400 B (int32) |
| per-channel multiplier + shift | 100 채널 | — | 800 B (int32 두 개씩이라고 가정) |
| 텐서별 (s, z) | 4 경계 | — | 수십 B |
| 합계 (flash) | | 약 13.2 KB | 약 4.4 KB |
| 가장 큰 activation 쌍 (RAM) | 64 + 32 | 384 B | 96 B |

말로 하면: weight는 4배 줄지만, 작은 모델에서는 bias(int32)와 per-channel 파라미터가 무시 못 할 비율(이 예에서 약 27%)을 차지한다. 모델이 커지면 이 비율은 금방 작아진다 (채널 수 ≪ weight 수). activation RAM도 4배 준다 — MCU에서는 이쪽(tensor arena)이 더 빡빡한 경우가 많다.

### 11.4 연산량 감각

한 출력당 N번의 int8 MAC + requantize 1회(64-bit 곱 1 + 시프트 1 + 덧셈·비교 몇 개). N이 수십 이상이면 requantize 비용은 무시할 수준이다. 병목은 여전히 MAC 루프와 메모리 접근이다 (D, K 모듈). SIMD가 있는 코어에서는 int8 두 개(또는 네 개 이상)를 한 명령으로 곱해 누산한다.

### 11.5 펌웨어 엔지니어가 확인할 체크포인트

- padding 버퍼를 z_x로 채웠는가 (4.5절).
- per-channel 배열(multiplier, shift, bias)의 채널 순서가 weight 레이아웃과 같은가.
- 오른쪽 시프트가 음수에서 산술 시프트인가, 반올림 규칙이 레퍼런스와 같은가 (7.4, 7.7절).
- 누산기 폭이 충분한가 (7.2절, 특히 int16 activation이나 아주 긴 reduction).
- 입력 전처리(feature 추출)의 출력이 학습 때 쓴 입력 scale/zero-point와 같은 도메인인가.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| padding을 정수 0으로 채움 | 이미지/신호 경계 근처 출력만 틀림 | 비대칭에서 정수 0 ≠ 실수 0 | 경계 밖을 z_x로 채우거나 `q_x − z_x` 도메인에서 0 사용 |
| bias를 int8이나 다른 scale로 양자화 | 모든 출력이 채널별로 일정하게 밀림 | bias scale ≠ s_w·s_x | bias는 int32, scale = s_w·s_x, z = 0 |
| zero-point 보정 부호 오류 | 채널별 상수 offset 오차 | `bias_eff = q_b − z_x·Σq_w` 부호 실수 | 7.1 유도를 다시 하고 numpy golden으로 비교 |
| min-max calibration + outlier | int8 정확도가 크게 떨어짐, 특정 층 SQNR 낮음 | 드문 큰 값 하나가 scale 결정 | percentile / MSE / KL, 또는 ReLU6·clipping |
| per-tensor weight + depthwise/BN folding | 작은 채널 weight가 대부분 0으로 양자화 | 큰 채널이 scale 독점 | per-channel weight |
| 대표성 없는 calibration set | PC에선 정상, 기기 실제 환경에서 정확도 하락 | 실제 입력 범위를 못 봄 | 기기 전처리 코드로 만든 다양한 데이터로 calibration |
| 반올림 규칙 불일치 | 레퍼런스와 1 LSB 차이가 드물게 발생 | round-half-even vs half-away, 1회 vs 2회 반올림 | golden model이 대상 커널의 반올림을 그대로 흉내 |
| M ≥ 1 미처리 | 특정 층 출력이 완전히 틀림 | shift > 0인 경우를 오른쪽 시프트로만 처리 | 왼쪽 시프트 경로 추가 (TFLite 방식) |
| scale = 0 (상수·죽은 채널) | NaN/inf, 0으로 나누기 | max = min | 최소 scale(epsilon) 적용 |
| residual add를 정수 그대로 더함 | skip 연결 뒤부터 오차 폭증 | 두 입력의 scale이 다름 | 공통 scale로 각각 requantize 후 덧셈 |

---

## 13. 면접에서 이렇게 말한다

**Q.** "Derive the scale and zero-point for asymmetric int8 quantization of a tensor with range [min, max]."

**A.** 범위를 0이 포함되도록 넓힌 뒤, 두 끝점이 q_min, q_max에 대응한다는 두 식을 푼다. s = (max − min)/(q_max − q_min), z = round(q_min − min/s). z를 정수로 반올림하는 게 핵심이고, 그래야 실수 0이 정확히 표현된다. 예: [−1, 3] → uint8이면 s = 4/255, z = 64.

> "I first extend the range to include zero, then map the two endpoints: scale = (max − min) / (qmax − qmin), zero-point = round(qmin − min / scale), clamped to the integer range. Rounding the zero-point to an integer guarantees that real zero is exactly representable, which matters for padding and ReLU outputs. For [−1, 3] in uint8 that gives a scale of 4/255 and a zero-point of 64."

**Q.** "Per-channel vs per-tensor quantization — when and why?"

**A.** weight는 출력 채널마다 크기가 크게 다를 수 있다(특히 BN folding, depthwise). per-tensor면 가장 큰 채널이 scale을 정해서 나머지 채널의 해상도가 무너진다. 출력 채널별 scale은 누산이 끝난 뒤 requantize multiplier에 흡수되므로 추가 비용이 채널당 multiplier 하나뿐이다. activation은 per-tensor가 표준인데, 입력 채널별 scale은 정수 MAC 하나로 계산할 수 없기 때문이다. int4처럼 비트가 적으면 per-group(32~128)까지 내려간다.

> "Weights use per-channel scales because output channels can differ in magnitude by an order of magnitude, especially after BN folding or in depthwise convs; a per-tensor scale lets the largest channel waste the resolution of all others. Per-output-channel scales are free at runtime because they fold into the per-channel requantization multiplier. Activations stay per-tensor because per-input-channel scales would break the single integer dot product. At 4 bits we go further to per-group scales, typically 32 to 128 elements."

**Q.** "Walk me through how an int8 conv or dense layer produces an int8 output."

**A.** int8 입력과 int8 weight를 곱해 int32에 누산한다. weight가 대칭이면 zero-point 보정은 z_x·Σw 하나뿐이고, 이건 오프라인에서 int32 bias(scale = s_w·s_x)에 접어 넣는다. 누산값에 실수 M = s_w·s_x/s_y를 곱해야 하는데, M을 Q31 정수 M0와 시프트 n으로 분해해서 64-bit 곱 + 반올림 상위 32비트 + 반올림 오른쪽 시프트로 계산한다. 그 다음 z_y를 더하고 [act_min, act_max]로 clamp — 이 clamp가 ReLU/ReLU6까지 겸한다.

> "Each output is an int32 accumulation of int8 times int8 products plus an int32 bias whose scale is s_w times s_x, with the zero-point correction precomputed into that bias. The accumulator is then scaled by M = s_w·s_x / s_y, which is represented as a Q31 fixed-point multiplier M0 and a right shift, using a rounding doubling high multiply followed by a rounding shift. Finally we add the output zero-point and clamp to the int8 range, and the clamp bounds also implement a fused ReLU or ReLU6. No floating point is needed at runtime."

**Q.** "Why must zero be exactly representable?"

**A.** padding, ReLU 출력, masking 때문에 텐서에 정확한 0이 매우 많다. 0이 격자 위에 없으면 모든 0이 같은 방향으로 틀려서 random noise가 아니라 bias가 되고, 합칠수록 누적된다. zero-point를 정수로 두면 0이 z로 정확히 가고, padding도 z로 채우는 memset이 된다.

> "Zero is by far the most common value in activations — padding, ReLU outputs, masks. If zero is not on the integer grid, every zero carries the same small error, so it acts as a systematic bias that accumulates in every dot product rather than averaging out. An integer zero-point makes zero map exactly to z, and zero-padding becomes simply filling with z."

**Q.** "Min-max vs percentile calibration — what's the trade-off?"

**A.** clipping 오차와 rounding 오차의 줄다리기다. min-max는 아무것도 안 자르지만 드문 outlier 하나가 scale을 키워 모든 값의 rounding 오차가 커진다. percentile은 꼬리를 조금 잘라 해상도를 얻는다. 꼬리가 두꺼운 분포에서 효과가 크지만 몇 %를 쓸지는 조정해야 하고, 너무 자르면 오히려 나빠진다. MSE 탐색이나 KL은 이 균형점을 데이터로 찾는다. 최종 선택은 검증 정확도로 한다.

> "It's a trade-off between clipping error and rounding error. Min-max never clips, but a single outlier inflates the scale and every typical value loses resolution. Percentile clipping, say at 99.99 percent, sacrifices a handful of outliers to get a much finer step. On heavy-tailed activations that usually wins, but clip too aggressively and clipping error dominates. MSE search or KL calibration find the balance point from the data, and I pick among them by validation accuracy."

**Q.** "FP16 vs BF16?"

**A.** 둘 다 16비트지만 FP16은 지수 5·가수 10비트로 정밀도를, BF16은 지수 8·가수 7비트로 FP32와 같은 범위를 택했다. FP16은 65504를 넘으면 inf라 학습에서 loss scaling이 필요하지만 추론에서 값 범위가 알려져 있으면 더 정확하다. BF16은 FP32의 상위 16비트라 변환이 쉽고 학습이 안정적이다. edge 추론에서는 하드웨어가 무엇을 지원하는지가 먼저다.

> "Both are 16 bits. FP16 spends them on precision — 5 exponent and 10 mantissa bits, max about 65504. BF16 keeps FP32's 8-bit exponent and only 7 mantissa bits, so it has FP32's dynamic range but three fewer mantissa bits, which makes its steps eight times coarser. BF16 is popular for training because it rarely overflows and converts from FP32 by truncation; FP16 gives better precision when ranges are well-behaved, which is often the case in inference. On edge devices the deciding factor is usually what the accelerator supports."

**Q.** "How do you verify that your quantized kernel on the device is correct?"

**A.** 파이썬에서 기기 커널과 같은 정수 산술(같은 M0/shift, 같은 반올림)을 흉내 내는 golden model을 만들고 기기 출력과 bit-exact로 비교한다. 정수 연산은 순서와 무관하게 결과가 같아서 bit-exact 비교가 가능하다. float 레퍼런스와는 tolerance(LSB 단위, SQNR)로 비교해서 양자화 손실과 구현 버그를 분리한다.

> "I build a bit-accurate golden model in Python that reproduces the kernel's integer arithmetic — the same multipliers, shifts and rounding — and compare device outputs against it bit-exactly; integer math is order-independent, so exact match is a fair requirement. Separately I compare against the float model with a tolerance in LSBs or per-layer SQNR. That separates quantization loss from implementation bugs."

---

## 14. 직접 해보기

1. 범위 [−0.5, 1.5]를 uint8로 양자화할 때 s와 z를 손으로 구하고, x = 0.7의 q와 x̂를 계산하라. 정답: s = 2/255 ≈ 0.007843, z = round(63.75) = 64, 0.7/s = 89.25 → 89, q = 153, x̂ = 89·s ≈ 0.69804.
2. M = 0.0123을 M0(Q31)와 shift로 분해하라. 힌트: 0.0123 × 2⁶ = 0.7872 ∈ [0.5, 1)이므로 shift = −6. 정답: M0 = round(0.7872 × 2³¹) = 1690499128 (파이썬 `quantize_multiplier(0.0123)`로 확인).
3. 입력 길이 N = 1152 (3×3×128 conv)일 때 int8 × int8 누산이 int32를 넘을 수 있는가? 정답: 최악 `1152 × 16384 ≈ 1.9 × 10⁷ ≪ 2.1 × 10⁹`, 넘지 않는다.
4. 10절 모델에서 activation calibration을 min-max 대신 99.99 percentile로 바꾸고 정확도·logit SQNR 변화를 재라. 힌트: `act_qparams`에서 `t.max()` 대신 `np.percentile(t, 99.99)`를 쓰고 범위 밖 값은 clamp된다는 점을 기억할 것. 정답: 모델·데이터에 따라 다르다 — 결과를 직접 확인하고 9.1절 trade-off로 설명해 보라.
5. 7.6절 C 커널에 ReLU6를 추가하라: 출력 scale 6/255, z_y = −128이라면 act_min/act_max는? 출력 범위가 [0, 8]로 calibration되었다면? 정답: 각각 [−128, 127], [−128, 63].
6. 7.7절 테스트에서 `rdbpot`을 그냥 `x >> e`(버림)로 바꾸면 mismatch가 얼마나 나오고, 오차의 평균 부호는 무엇인가? 힌트: 버림은 −∞ 쪽이라 평균적으로 −0.5 LSB bias가 생긴다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| quantization | 양자화 | 실수 텐서를 정수 + scale(과 zero-point)로 표현 |
| scale (s) | 눈금 간격 | 정수 1 차이가 실수로 얼마인지, "1 LSB의 크기" |
| zero-point (z) | 영점 | 실수 0에 대응하는 정수, 반드시 정수 |
| affine / asymmetric | 비대칭 | z ≠ 0을 허용, 아무 [min, max] 구간 표현 |
| symmetric | 대칭 | z = 0, [−c, c] 표현, 보정항이 사라져 싸다 |
| dequantization | 역양자화 | x̂ = s·(q − z) |
| fake quant | 가짜 양자화 | 양자화 후 바로 역양자화해 float로 오차만 흉내 (C2) |
| SQNR | 신호 대 양자화 잡음비 | 10·log10(신호 전력 / 오차 전력), 비트당 약 6 dB |
| per-tensor | 텐서 단위 | 텐서 전체에 scale 하나 |
| per-channel (per-axis) | 채널 단위 | weight 출력 채널마다 scale 하나 |
| per-group (block) | 그룹 단위 | 행을 g개씩 잘라 group마다 scale |
| requantization | 재양자화 | int32 누산값을 출력 scale의 int8로 바꾸는 단계 |
| M0, shift | 고정소수점 multiplier | M ≈ M0·2⁻³¹·2^shift, M0 ∈ [2³⁰, 2³¹) |
| SRDHM | SaturatingRoundingDoublingHighMul | round(a·b / 2³¹), Q31 곱 |
| RDBPOT | RoundingDivideByPOT | 반올림 오른쪽 시프트 (half away from zero) |
| bias_eff | 보정된 bias | q_b − z_x·Σq_w (오프라인 계산) |
| calibration | 보정 | 대표 입력으로 activation 범위(clip)를 정함 |
| clipping error | 잘림 오차 | 범위 밖 값이 포화되며 생기는 오차 |
| rounding error | 반올림 오차 | 범위 안 값의 격자 오차, 분산 s²/12 |
| percentile | 백분위 | 상위 p%를 잘라 범위를 정함 |
| KL calibration | entropy calibration | 원 분포와 양자화 분포의 KL을 최소화하는 clip |
| observer | 관찰자 | PyTorch에서 통계를 모아 (s, z)를 계산하는 모듈 |
| fused activation | 융합 활성화 | ReLU/ReLU6를 출력 clamp 경계로 흡수 |
| 16x8 | int16 activation × int8 weight | 민감한 activation용 모드 |
| BF16 | bfloat16 | 1-8-7 float, FP32와 같은 범위 |
| E4M3 / E5M2 | FP8 두 변형 | 정밀도 우선 / 범위 우선 |

---

## 16. 요약 & 체크리스트

양자화는 실수를 `x ≈ s·(q − z)`로 표현하는 것이다. scale은 범위를 정수 단계 수로 나눈 눈금이고, zero-point는 실수 0을 정확히 표현하기 위해 정수로 둔다. 오차는 범위 안에서는 분산 s²/12의 rounding noise(비트당 6 dB), 범위 밖에서는 clipping이며, calibration은 이 둘의 균형점을 찾는 일이다. weight는 대칭·per-channel(저비트면 per-group), activation은 비대칭·per-tensor가 표준인데, 그 이유는 정수 산술에서 보정항이 사라지고 scale이 requantize multiplier에 흡수되기 때문이다. int8 층은 int32 누산 → Q31 multiplier와 반올림 시프트 → zero-point 더하기 → clamp(= fused ReLU)로 끝나고, 이 과정은 float 없이 bit-exact하게 재현·검증할 수 있다.

- [ ] [min, max]에서 uint8/int8의 s와 z를 손으로 계산하고, x 하나를 양자화·역양자화할 수 있다
- [ ] 양자화 오차 분산 s²/12와 SQNR ≈ 6.02·b + 4.77 − 20·log10(c/σ)를 유도할 수 있다
- [ ] weight는 대칭, ReLU 뒤 activation은 비대칭인 이유를 zero-point 보정항으로 설명할 수 있다
- [ ] zero-point가 정수여야 하는 이유와 padding을 z_x로 채워야 하는 이유를 말할 수 있다
- [ ] per-tensor / per-channel / per-group의 정확도와 저장 비용 차이를 숫자로 말할 수 있다
- [ ] FP16, BF16, FP8(E4M3/E5M2)의 비트 배치와 범위·정밀도 차이를 설명할 수 있다
- [ ] int8 dense 층의 int32 누산, bias_eff, M = s_w·s_x/s_y를 유도할 수 있다
- [ ] M을 M0(Q31)와 shift로 분해하고, 반올림 규칙이 bit-exact에 미치는 영향을 설명할 수 있다
- [ ] ReLU/ReLU6를 clamp 경계로 바꿔 계산할 수 있다
- [ ] min-max / percentile / MSE / KL calibration의 trade-off를 그림 6으로 설명할 수 있다

## 참고 자료

- B. Jacob et al., "Quantization and Training of Neural Networks for Efficient Integer-Arithmetic-Only Inference", CVPR 2018 — [arXiv:1712.05877](https://arxiv.org/abs/1712.05877) (이 노트 7절 수식의 원전)
- R. Krishnamoorthi, "Quantizing deep convolutional networks for efficient inference: A whitepaper", 2018 — [arXiv:1806.08342](https://arxiv.org/abs/1806.08342)
- M. Nagel et al., "A White Paper on Neural Network Quantization", 2021 — [arXiv:2106.08295](https://arxiv.org/abs/2106.08295)
- A. Gholami et al., "A Survey of Quantization Methods for Efficient Neural Network Inference", 2021 — [arXiv:2103.13630](https://arxiv.org/abs/2103.13630)
- P. Micikevicius et al., "FP8 Formats for Deep Learning", 2022 — [arXiv:2209.05433](https://arxiv.org/abs/2209.05433)
- S. Migacz, "8-bit Inference with TensorRT", NVIDIA GTC 2017 (KL calibration 발표)
- M. Horowitz, "Computing's Energy Problem (and what we can do about it)", ISSCC 2014 (1.2절 에너지 표)
- gemmlowp (SaturatingRoundingDoublingHighMul, RoundingDivideByPOT) — [github.com/google/gemmlowp](https://github.com/google/gemmlowp)
- TensorFlow Lite 8-bit quantization specification — [tensorflow.org/lite/performance/quantization_spec](https://www.tensorflow.org/lite/performance/quantization_spec) (현재는 LiteRT 문서로 이전되었을 수 있음)
- CMSIS-NN — [github.com/ARM-software/CMSIS-NN](https://github.com/ARM-software/CMSIS-NN)
- PyTorch quantization 문서 — [pytorch.org/docs/stable/quantization.html](https://pytorch.org/docs/stable/quantization.html)
- MIT 6.5940 TinyML and Efficient Deep Learning (Song Han) — [efficientml.ai](https://efficientml.ai) (quantization 강의)
