# A1. ML을 위한 선형대수 — 벡터·행렬·텐서와 행렬곱의 비용

> **이 노트를 다 읽으면**: 텐서 shape를 보고 연산 결과 shape와 MAC 수를 바로 계산할 수 있다 · dense layer가 왜 `y = W·x + b` 한 줄인지 설명할 수 있다 · row-major/stride/NCHW·NHWC를 C 주소 계산으로 풀어 말할 수 있다 · 행렬곱 루프 순서가 캐시 때문에 10배 이상 속도 차이를 내는 이유를 실측으로 보여 줄 수 있다
> **JD 연결**: "Co-design model architectures that meet latency, memory, power, bandwidth", "Familiarity with … CPU/DSP/NPU HW architectures" · study_prep_list A1 행 — 벡터·행렬·텐서 shape, 행렬곱과 GEMM, `[M,K]×[K,N]` 비용 = M·N·K MAC, broadcasting·transpose·reshape, 내적·노름·코사인 유사도
> **Don 기준 난이도**: Berkeley 응용수학의 선형대수 + C 배열·캐시·DMA 감각은 이미 있다 / 새로 배울 것은 "ML에서 이 수학이 어떤 모양으로 쓰이는가"(shape 사고, broadcasting, 텐서 레이아웃, GEMM 중심 사고)
> **선행 노트**: A0

---

## 0. 큰 그림 — 이게 왜 필요한가

신경망(neural network)을 펌웨어 엔지니어 눈으로 한 줄로 요약하면 이렇다: **"큰 숫자 배열(가중치)과 입력 숫자 배열을 곱하고 더하는 일을 층층이 반복하는 프로그램"**. 곱하고 더하는 일의 정체가 바로 선형대수의 **행렬곱**이다.

예를 들어 Hark 같은 웨어러블이 손목 IMU로 제스처를 인식한다고 해 보자(구조는 추정이다). 데이터는 이렇게 흐른다.

```
 IMU 6축 @50Hz        윈도우 1초            feature / tensor           모델 (레이어 스택)             출력
 ┌──────────┐      ┌──────────────┐      ┌─────────────────┐      ┌──────────────────────┐     ┌──────────┐
 │ ax ay az │ ───► │ [50, 6] 행렬  │ ───► │ [12] 벡터 또는    │ ───► │ y = W·x + b  (GEMV)   │ ──► │ [4] 점수  │
 │ gx gy gz │      │ (시간 × 축)   │      │ [1, 50, 6] 텐서   │      │ conv → im2col → GEMM  │     │ tap/swipe│
 └──────────┘      └──────────────┘      └─────────────────┘      │ attention → QKᵀ GEMM  │     │ /none/...│
                                                                  └──────────────────────┘     └──────────┘
        센서 드라이버 · DMA · 링버퍼            shape·layout 결정                 MAC 수 = 연산 시간·전력          argmax
```

이 흐름에서 edge ML 엔지니어가 매일 하는 질문은 거의 다 선형대수 질문이다.

| 실무 질문 | 필요한 선형대수 |
|---|---|
| 이 레이어가 Cortex-M에서 몇 ms 걸리나? | 행렬곱 비용 = M·N·K MAC |
| 가중치가 SRAM에 들어가나? | 행렬 원소 수 × 바이트 |
| NPU 입력 버퍼를 어떤 순서로 DMA 하나? | row-major, stride, NCHW vs NHWC |
| 모델이 "tensor shape mismatch"로 죽는다 | shape 규칙, broadcasting |
| 음성 명령을 비슷한 의도끼리 묶고 싶다 | 내적, 코사인 유사도, 임베딩 |
| 모델을 반으로 줄이고 싶다 | rank, SVD, 저랭크 근사 |

펌웨어 비유로 말하면, 선형대수는 ML의 **레지스터 맵과 버스 프로토콜**이다. 레지스터 맵을 모르면 드라이버를 못 쓰듯, shape와 행렬곱을 모르면 모델 배포를 못 한다. 좋은 소식은 Don이 이미 아는 것(MAC, 배열 주소 계산, 캐시 라인)과 1:1로 대응된다는 점이다.

---

## 1. 스칼라 · 벡터 · 행렬 · 텐서와 shape

### 직관: 차원이 하나씩 늘어나는 숫자 상자

- **스칼라(scalar)**: 숫자 하나. 예: 중력가속도 9.81.
- **벡터(vector)**: 숫자를 한 줄로 늘어놓은 것. 예: 한 순간의 가속도 `(ax, ay, az)`.
- **행렬(matrix)**: 숫자를 2차원 표로 놓은 것. 예: 50 샘플 × 6축 IMU 윈도우.
- **텐서(tensor)**: 차원이 몇 개든 상관없는 숫자 상자의 일반 이름. 예: 윈도우 32개를 쌓은 `[32, 50, 6]`.

ML에서 "텐서"는 물리학의 엄밀한 텐서가 아니라 그냥 **n차원 배열**이다. C로 치면 `float x[32][50][6];` 이 곧 3차원 텐서다.

### 정의: shape와 rank(ndim)

- **shape**: 각 축(axis)의 길이를 나열한 튜플. `[32, 50, 6]`은 "0번 축 길이 32, 1번 축 50, 2번 축 6".
- **ndim**(numpy) 또는 **rank**(TensorFlow 문서 용어): 축의 개수. 위 예는 3. (선형대수의 "행렬 rank"와 이름만 같고 다른 개념이다 → 9절)
- 원소 수 = shape의 곱 = 32 × 50 × 6 = 9,600.
- 바이트 = 원소 수 × dtype 크기 = 9,600 × 4 = 38,400 (float32).

말로 하면: **shape만 보면 메모리 크기가 나온다.** 이게 MCU SRAM 예산 계산의 출발점이다.

### 벡터의 두 얼굴: 화살표 vs 배열

수학 시간의 벡터는 **화살표**(방향과 길이)였다. ML에서는 대부분 **특징(feature)을 담은 배열**로 쓴다. 둘은 같은 것이다. 2차원 `(3, 1)`은 화살표로 그릴 수 있고, 12차원 feature vector는 그릴 수 없을 뿐 수학은 똑같이 적용된다. "두 벡터 사이 각도", "길이" 같은 기하 개념이 12차원, 768차원에서도 그대로 통한다는 것이 ML의 핵심 트릭이다.

**IMU 윈도우의 feature vector 예**: 1초(50 샘플) 동안 6축의 평균 6개 + 표준편차 6개를 이어 붙이면 12차원 벡터 하나가 된다. 이 벡터 하나가 "이 1초 동안의 손목 움직임 요약"이다. 고전 ML(B7)은 이런 벡터를 입력으로 받는다.

아래 코드는 IMU 윈도우 → feature vector → 배치 텐서로 shape가 어떻게 변하는지 확인한다.

```python
import numpy as np
np.random.seed(0)
# 가짜 IMU 윈도우: 50 Hz로 1초 = 50 샘플, 축 6개 (ax, ay, az, gx, gy, gz)
window = np.random.randn(50, 6).astype(np.float32)
print("window shape:", window.shape, "ndim:", window.ndim)

# 축별 평균·표준편차를 이어 붙이면 12차원 feature vector
feat = np.concatenate([window.mean(axis=0), window.std(axis=0)])
print("feature shape:", feat.shape)
print("feature[:4]:", np.round(feat[:4], 3))

# 배치: 윈도우 32개를 쌓으면 3차원 텐서 [B, T, C]
batch = np.random.randn(32, 50, 6).astype(np.float32)
print("batch shape:", batch.shape, "ndim:", batch.ndim, "elements:", batch.size)
print("bytes (float32):", batch.nbytes)
scalar = np.float32(9.81)
print("scalar ndim:", np.ndim(scalar), "shape:", np.shape(scalar))
```

```text
window shape: (50, 6) ndim: 2
feature shape: (12,)
feature[:4]: [ 0.124  0.249 -0.182  0.077]
batch shape: (32, 50, 6) ndim: 3 elements: 9600
bytes (float32): 38400
scalar ndim: 0 shape: ()
```

출력에서 볼 것: `mean(axis=0)`은 **0번 축(시간)을 없애는** 연산이라 `[50, 6]` → `[6]`이 된다. 스칼라의 shape는 빈 튜플 `()`이다.

### shape 표기 관례 (외워 두면 코드 읽기가 빨라진다)

| 기호 | 뜻 | 예 |
|---|---|---|
| B 또는 N | batch (샘플 개수) | 32 |
| T 또는 L | 시간/시퀀스 길이 | 50 샘플, 토큰 256개 |
| C | channel (축, 특징 수) | IMU 6축, RGB 3 |
| H, W | 이미지/스펙트로그램 높이·너비 | 49 × 10 MFCC |
| D 또는 d | 임베딩/hidden 차원 | 64, 768 |

### 함정

- `(12,)`와 `(12, 1)`과 `(1, 12)`는 **다른 shape**다. 1차원 벡터, 열 벡터, 행 벡터. 6절의 broadcasting 함정의 주범이다.
- axis 번호는 0부터. `axis=0`이 "행 방향으로 내려가며 합친다"(= 각 열의 통계)라는 점이 처음엔 헷갈린다. "없어지는 축"으로 기억하자.

---

## 2. 내적(dot product) — ML에서 가장 많이 실행되는 연산

### 정의와 손계산

길이가 같은 두 벡터 `a`, `b`의 내적:

```
a · b = a₀b₀ + a₁b₁ + … + a₍ₙ₋₁₎b₍ₙ₋₁₎ = ∑ᵢ aᵢ bᵢ
```

말로 하면: **같은 자리끼리 곱해서 전부 더한다.** 결과는 스칼라 하나.

손계산: `a = (3, 1)`, `b = (1, 1)` → `a·b = 3·1 + 1·1 = 4`.

### 기하학적 의미: 투영과 각도

```
a · b = |a| |b| cos θ
```

말로 하면: 내적은 **두 벡터가 얼마나 같은 방향을 보는지**를 길이까지 곱해서 잰 값이다. 같은 방향이면 크고 양수, 직각이면 0, 반대면 음수.

그리고 `a·b / |b|`는 **a를 b 방향으로 비춘 그림자(투영)의 길이**다.

```svg
<svg viewBox="0 0 640 320" xmlns="http://www.w3.org/2000/svg">
  <defs> <marker id="a1v-blue" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#4a7bd0"/></marker>
  <marker id="a1v-orange" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#e08a3c"/></marker> </defs>
  <line x1="60" y1="280" x2="340" y2="280" stroke="currentColor" stroke-width="1"/> <line x1="60" y1="280" x2="60" y2="40" stroke="currentColor" stroke-width="1"/> <text x="130" y="298" font-size="12" text-anchor="middle">1</text>
  <text x="200" y="298" font-size="12" text-anchor="middle">2</text> <text x="270" y="298" font-size="12" text-anchor="middle">3</text> <text x="48" y="214" font-size="12" text-anchor="middle">1</text>
  <text x="48" y="144" font-size="12" text-anchor="middle">2</text> <text x="48" y="74" font-size="12" text-anchor="middle">3</text> <line x1="130" y1="210" x2="290" y2="50" stroke="#888" stroke-width="1" stroke-dasharray="5,4"/>
  <text x="296" y="48" font-size="12">b 방향</text> <line x1="60" y1="280" x2="200" y2="140" stroke="#3f9a6b" stroke-width="6" stroke-opacity="0.6"/>
  <line x1="60" y1="280" x2="268" y2="211" stroke="#4a7bd0" stroke-width="2.5" marker-end="url(#a1v-blue)"/> <line x1="60" y1="280" x2="128" y2="212" stroke="#e08a3c" stroke-width="2.5" marker-end="url(#a1v-orange)"/>
  <line x1="270" y1="210" x2="200" y2="140" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="4,3"/> <polyline points="192.9,147.1 200,154.1 207.1,147.1" fill="none" stroke="#d0564a" stroke-width="1"/>
  <path d="M 97.95 267.35 A 40 40 0 0 0 88.28 251.72" fill="none" stroke="currentColor" stroke-width="1"/> <text x="104" y="262" font-size="13">θ</text> <text x="278" y="216" font-size="13">a = (3, 1)</text>
  <text x="70" y="192" font-size="13">b = (1, 1)</text> <text x="192" y="130" font-size="13" text-anchor="end">투영점 (2, 2)</text> <text x="360" y="90" font-size="13">a·b = 3·1 + 1·1 = 4</text>
  <text x="360" y="114" font-size="13">|a| = √10 ≈ 3.16,  |b| = √2 ≈ 1.41</text> <text x="360" y="138" font-size="13">cos θ = 4 / (3.16 · 1.41) ≈ 0.894</text> <text x="360" y="162" font-size="13">θ ≈ 26.6°</text>
  <text x="360" y="186" font-size="13">초록 = 투영 길이 = a·b / |b| ≈ 2.83</text> <text x="360" y="210" font-size="13">빨강 점선 ⟂ b 방향</text>
</svg>
```

그림 1 — `a = (3,1)`을 `b = (1,1)` 방향으로 투영하면 점 `(2,2)`에 떨어진다. 초록 선분 길이 `a·b/|b| = 4/√2 ≈ 2.83`이 "a가 b 방향으로 얼마나 가 있는가"이다.

### 코드로 확인 + MAC 루프

아래 코드는 내적·각도·투영을 numpy로 계산하고, 같은 내적을 펌웨어식 MAC 루프로도 계산한다.

```python
import numpy as np
a = np.array([3.0, 1.0])
b = np.array([1.0, 1.0])
dot = a @ b                      # 3·1 + 1·1
cos = dot / (np.linalg.norm(a) * np.linalg.norm(b))
proj_len = dot / np.linalg.norm(b)   # a를 b 방향으로 투영한 길이
print("dot =", dot)
print("|a| =", round(np.linalg.norm(a), 4), " |b| =", round(np.linalg.norm(b), 4))
print("cos =", round(cos, 4), " angle(deg) =", round(np.degrees(np.arccos(cos)), 2))
print("projection length =", round(proj_len, 4))

# MAC 루프로 직접: 펌웨어식
acc = 0.0
for ai, bi in zip(a, b):
    acc += ai * bi               # 1 MAC
print("MAC loop =", acc)
proj_vec = (dot / (b @ b)) * b       # 투영된 점(벡터)
print("projection point =", proj_vec)
```

```text
dot = 4.0
|a| = 3.1623  |b| = 1.4142
cos = 0.8944  angle(deg) = 26.57
projection length = 2.8284
MAC loop = 4.0
projection point = [2. 2.]
```

출력에서 볼 것: numpy의 `@` 연산자와 `acc += ai * bi` 루프가 같은 값을 낸다. **길이 n 벡터의 내적 = MAC n번.**

### Don 경험과 연결: 내적 = FIR 필터 = DSP MAC 유닛

Don이 아는 FIR 필터 한 샘플 출력은 `y[n] = ∑ₖ h[k]·x[n−k]`이다. 이것은 계수 벡터 `h`와 최근 입력 벡터의 **내적**이다. DSP에 MAC 유닛(곱하고 누산기에 더하는 하드웨어)이 있는 이유가 바로 이 연산 때문이고, 신경망 가속기도 본질은 **MAC 유닛을 수백~수천 개 깔아 놓은 칩**이다. Cortex-M4/M7의 DSP 확장 `SMLAD` 명령은 16비트 곱 2개를 한 번에 누산하고, NPU의 "TOPS" 스펙도 결국 "초당 MAC 몇 번"(1 MAC = 2 ops로 세는 게 관례)이다.

### 코사인 유사도와 임베딩 — 벡터 검색이 되는 이유

길이 효과를 빼고 **방향만** 비교하고 싶으면 코사인 유사도(cosine similarity)를 쓴다.

```
cos_sim(u, v) = (u · v) / (|u| |v|)      ∈ [−1, 1]
```

말로 하면: 두 벡터를 길이 1로 정규화한 뒤 내적한 값. 1이면 같은 방향, 0이면 무관, −1이면 반대.

**임베딩(embedding)**이란 문장·음성·이미지 같은 것을 **의미가 비슷하면 방향이 비슷한 벡터**로 바꾼 결과다. 모델이 그렇게 되도록 학습된다. 그래서 "비슷한 문장 찾기"가 "코사인 유사도가 큰 벡터 찾기"로 바뀌고, 이게 벡터 검색(RAG, 음성 명령 의도 매칭, 화자 인증)의 원리다.

아래 코드는 4차원짜리 가짜 문장 임베딩으로 의도 매칭을 흉내 내고, 정규화해 두면 검색이 행렬곱 한 번이 된다는 것을 보인다.

```python
import numpy as np
# 문장 임베딩이라고 가정한 4차원 벡터 (실제는 384~4096차원)
db = {
    "turn on the light":  np.array([0.9, 0.1, 0.0, 0.2]),
    "switch lights on":   np.array([0.8, 0.2, 0.1, 0.3]),
    "what's the weather": np.array([0.0, 0.9, 0.4, 0.0]),
    "play some music":    np.array([0.1, 0.0, 0.9, 0.3]),
}
query = np.array([0.85, 0.15, 0.05, 0.25])   # "lights on please"

def cos_sim(u, v):
    return u @ v / (np.linalg.norm(u) * np.linalg.norm(v))

for text, vec in sorted(db.items(), key=lambda kv: -cos_sim(query, kv[1])):
    print(f"{cos_sim(query, vec):.4f}  {text}")

# 정규화해 두면 cosine = 그냥 dot → 행렬곱 한 번으로 전부 계산
M = np.stack(list(db.values()))
M_n = M / np.linalg.norm(M, axis=1, keepdims=True)
q_n = query / np.linalg.norm(query)
print("scores via one matmul:", np.round(M_n @ q_n, 4))
```

```text
0.9945  turn on the light
0.9939  switch lights on
0.2388  play some music
0.1749  what's the weather
scores via one matmul: [0.9945 0.9939 0.1749 0.2388]
```

출력에서 볼 것: 표현이 다른 두 "불 켜기" 문장이 모두 0.99 이상. 데이터베이스를 미리 정규화해 두면 검색 = `[문장 수, D] × [D]` 행렬-벡터 곱 한 번이다. 온디바이스에서도 명령어 수십 개 정도는 이렇게 MCU에서 충분히 돈다.

### 함정

- 정규화하지 않은 내적으로 검색하면 **길이가 긴 벡터가 무조건 이긴다**. 방향 비교가 목적이면 코사인 또는 미리 정규화.
- 0 벡터를 정규화하면 0으로 나누기 → NaN. 펌웨어에서는 `eps`를 더한다.
- int8로 양자화된 임베딩끼리 내적하면 누산기는 int32가 필요하다(곱 하나가 최대 127·127 = 16,129). 모듈 C에서 다룬다.

---

## 3. 노름(norm) — 벡터의 "크기"를 재는 여러 자

### 정의와 손계산

`x = (3, −4, 1)`에 대해:

| 노름 | 공식 | 값 | 말로 하면 |
|---|---|---|---|
| L1 | `∑ |xᵢ|` | 3 + 4 + 1 = 8 | 절댓값의 합 (맨해튼 거리) |
| L2 | `√(∑ xᵢ²)` | √(9+16+1) = √26 ≈ 5.099 | 보통의 길이 (유클리드) |
| L∞ | `max |xᵢ|` | 4 | 가장 큰 성분 하나 |

두 벡터 사이 **거리**는 차이 벡터의 노름이다: `dist(p, q) = |p − q|`.

### ML에서 나오는 곳

| 쓰임 | 노름 | 무슨 일 |
|---|---|---|
| L2 regularization (weight decay) | L2² | 가중치가 커지지 않게 벌점 (A4) |
| L1 regularization | L1 | 가중치를 0으로 몰아 sparsity 유도 (C4 pruning과 연결) |
| gradient clipping | L2 | gradient 벡터가 너무 길면 길이만 줄임 (A3) |
| 최근접 이웃·k-means | L2 거리 | 클러스터링, 이상 탐지 |
| 양자화 scale 결정 | L∞ | `max|w|`로 int8 범위를 정함 (C1) |
| 양자화 오차 평가 | L2 | SQNR = 신호 파워 / 오차 파워 (C8) |

gradient clipping 손계산: `g = (30, −40)`이면 `|g| = 50`. `max_norm = 5`로 자르면 `g × (5/50) = (3, −4)`. 비율(방향)은 그대로고 길이만 50 → 5로 줄었다. numpy로는 `np.linalg.norm(x, 1)`, `np.linalg.norm(x)`, `np.linalg.norm(x, np.inf)`가 각각 L1, L2, L∞이다(위 표의 8, 5.099, 4.0이 그대로 나온다).

### Don 경험과 연결

L∞는 펌웨어의 **saturation 검사**와 같다. "이 버퍼에서 절댓값 최대가 int16 범위를 넘나?"가 바로 L∞ 질문이다. gradient clipping은 제어 루프의 **출력 리미터**(actuator saturation)와 비슷하다. 다만 성분별로 자르지 않고 벡터 전체를 같은 비율로 줄여 방향을 보존한다는 점이 다르다.

### 함정

- L2 노름을 구할 때 `∑ xᵢ²`가 float16에서 쉽게 overflow한다(float16 최대 약 65504). 큰 벡터면 float32 누산.
- "L2 regularization"은 보통 **제곱** L2(`∑ wᵢ²`)를 쓴다. 제곱근 없는 쪽이다.

---

## 4. 행렬-벡터 곱 = dense layer 하나

### 정의

`W`가 `[m, n]` 행렬, `x`가 길이 `n` 벡터면 `y = W·x`는 길이 `m` 벡터이고

```
yᵢ = ∑ⱼ Wᵢⱼ xⱼ        (i = 0 … m−1)
```

말로 하면: **출력 하나 = W의 한 행과 x의 내적.** 출력이 m개니까 내적 m번, MAC은 m·n번.

여기에 bias `b`(길이 m)를 더한 `y = W·x + b`가 신경망의 **fully-connected(dense, linear) layer** 하나다. 이후 비선형 함수(ReLU 등, B1)를 씌우면 "뉴런 층" 하나가 완성된다.

### 손계산 (2×3 가중치)

```
W = [[1, 2,  0],     x = [1, 2, 3],   b = [0.5, −0.5]
     [0, 1, −1]]

row view   : y₀ = 1·1 + 2·2 + 0·3 + 0.5 = 5.5
             y₁ = 0·1 + 1·2 − 1·3 − 0.5 = −1.5

column view: y = 1·[1,0] + 2·[2,1] + 3·[0,−1] + b
               = [1,0] + [4,2] + [0,−3] + [0.5,−0.5] = [5.5, −1.5]
```

두 가지 보는 법이 있다.

- **row view**: 출력마다 "W의 행 · x" 내적. → "각 출력 뉴런은 입력 패턴 하나(행)와 얼마나 닮았나를 잰다"는 해석. 구현하면 GEMV의 inner loop가 내적.
- **column view**: y는 W의 **열들을 x로 가중합한 것**. → "입력 j번이 출력 공간에 기여하는 방향이 W의 j번 열"이라는 해석. 구현하면 `y += xⱼ · W[:, j]` (AXPY 반복).

두 view는 **같은 MAC을 다른 순서로 한 것**이다. 8절에서 이 순서 차이가 캐시 성능을 크게 바꾸는 것을 실측한다.

아래 코드는 두 view와 PyTorch `nn.Linear`가 같은 값을 내는지 확인한다.

```python
import numpy as np
W = np.array([[1., 2., 0.],
              [0., 1., -1.]])        # [out=2, in=3]
b = np.array([0.5, -0.5])
x = np.array([1., 2., 3.])           # 입력 3개

y = W @ x + b
print("y =", y)

# row view: 출력 하나 = W의 한 행과 x의 dot
print("row view   :", [float(W[i] @ x + b[i]) for i in range(2)])
# column view: y = x0·W[:,0] + x1·W[:,1] + x2·W[:,2] + b
print("column view:", x[0]*W[:, 0] + x[1]*W[:, 1] + x[2]*W[:, 2] + b)

# PyTorch nn.Linear도 같은 계산 (weight shape = [out, in])
import torch
lin = torch.nn.Linear(3, 2)
with torch.no_grad():
    lin.weight.copy_(torch.tensor(W)); lin.bias.copy_(torch.tensor(b))
print("nn.Linear  :", lin(torch.tensor(x, dtype=torch.float32)).detach().numpy())
print("weight shape:", tuple(lin.weight.shape), "params:", sum(p.numel() for p in lin.parameters()))
```

```text
y = [ 5.5 -1.5]
row view   : [5.5, -1.5]
column view: [ 5.5 -1.5]
nn.Linear  : [ 5.5 -1.5]
weight shape: (2, 3) params: 8
```

출력에서 볼 것: PyTorch `nn.Linear(in, out)`의 weight shape는 **`[out, in]`** 이다(`[in, out]`이 아니다). 파라미터 수 = out·in + out = 2·3 + 2 = 8.

### 행렬 = 선형 변환 (기하학적 그림)

행렬을 곱한다는 것은 공간 전체를 **늘이고, 기울이고, 돌리는** 변환이다. 핵심 사실 하나: **행렬의 j번째 열 = 기저 벡터 eⱼ가 변환 후 가는 곳**이다. (column view 그 자체다.)

```svg
<svg viewBox="0 0 640 280" xmlns="http://www.w3.org/2000/svg">
  <defs> <marker id="a1t-green" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#3f9a6b"/></marker>
  <marker id="a1t-red" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#d0564a"/></marker> </defs>
  <line x1="60" y1="230" x2="310" y2="230" stroke="currentColor" stroke-width="1"/> <line x1="60" y1="230" x2="60" y2="50" stroke="currentColor" stroke-width="1"/> <text x="130" y="248" font-size="12" text-anchor="middle">1</text>
  <text x="200" y="248" font-size="12" text-anchor="middle">2</text> <text x="270" y="248" font-size="12" text-anchor="middle">3</text> <text x="48" y="164" font-size="12" text-anchor="middle">1</text>
  <text x="48" y="94" font-size="12" text-anchor="middle">2</text> <polygon points="60,230 130,230 130,160 60,160" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0" stroke-width="1.5" stroke-dasharray="5,3"/>
  <polygon points="60,230 200,230 270,160 130,160" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c" stroke-width="2"/> <line x1="60" y1="230" x2="198" y2="230" stroke="#3f9a6b" stroke-width="2.5" marker-end="url(#a1t-green)"/>
  <line x1="60" y1="230" x2="128.6" y2="161.4" stroke="#d0564a" stroke-width="2.5" marker-end="url(#a1t-red)"/> <text x="84" y="200" font-size="12">원래</text> <text x="206" y="222" font-size="12">T·e₁ = (2, 0)</text>
  <text x="136" y="152" font-size="12">T·e₂ = (1, 1)</text> <text x="276" y="156" font-size="12">(3, 1)</text> <text x="340" y="70" font-size="13">T = [[2, 1], [0, 1]]</text> <text x="340" y="96" font-size="13">파랑 점선: 단위 정사각형 (넓이 1)</text>
  <text x="340" y="120" font-size="13">주황: T를 곱한 결과 (평행사변형)</text> <text x="340" y="146" font-size="13">e₁ = (1, 0) → T의 1열 (2, 0)</text> <text x="340" y="170" font-size="13">e₂ = (0, 1) → T의 2열 (1, 1)</text>
  <text x="340" y="196" font-size="13">넓이 배율 = |det T| = 2</text>
</svg>
```

그림 2 — 행렬 `T`는 x 방향으로 2배 늘이고 위쪽을 오른쪽으로 기울인다(shear). 꼭짓점 `(1,1)`은 `(3,1)`로 간다. 열 벡터가 곧 기저 벡터의 도착지다.

dense layer는 입력 공간(예: 12차원 IMU feature)을 출력 공간(예: 4개 클래스 점수)으로 옮기는 이런 변환이다. 학습이란 "클래스끼리 잘 갈라지도록 공간을 늘이고 기울이는 W를 찾는 일"이다. 다만 선형 변환만 여러 번 쌓으면 결국 행렬 하나로 합쳐지므로(`W₂·(W₁·x) = (W₂W₁)·x`), 층 사이에 **비선형 함수**가 꼭 들어가야 깊은 모델이 의미가 있다(B1).

### 함정

- `W @ x`에서 `x`가 `[n]`인지 `[n, 1]`인지에 따라 결과가 `[m]` 또는 `[m, 1]`이 된다. 이후 연산에서 broadcasting 사고의 원인.
- TFLite/Keras의 Dense 가중치는 저장 방향이 PyTorch와 다를 수 있다(Keras `Dense`의 kernel은 `[in, out]`). 모델 변환 시 transpose가 끼어드는 이유다.

---

## 5. 행렬곱(GEMM) — 모든 레이어의 심장

### shape 규칙

```
A [M, K]  ×  B [K, N]  →  C [M, N]
        └──── 같아야 함 ────┘
```

말로 하면: **안쪽 차원 K가 같아야 곱할 수 있고, 바깥쪽 M과 N이 결과 shape가 된다.** K는 "더해지면서 사라지는 축"이다.

```
C[i, j] = ∑ₖ A[i, k] · B[k, j]
```

말로 하면: C의 (i, j) 칸 = **A의 i행과 B의 j열의 내적**.

### 비용: M·N·K MAC

C에는 칸이 M·N개 있고, 칸마다 길이 K의 내적(K MAC)을 한다. 그래서

```
MACs  = M · N · K
FLOPs = 2 · M · N · K     (곱 1 + 덧셈 1)
메모리 = (M·K + K·N + M·N) · 바이트
```

이 공식 하나로 거의 모든 레이어의 연산량을 계산한다(D1). 예: `[64, 256] × [256, 128]` → 64·128·256 = 2,097,152 MAC ≈ 2.1 M MAC.

### 손계산: `[2,3] × [3,2]`

```
A = [[1, 2, 3],      B = [[ 7,  8],
     [4, 5, 6]]           [ 9, 10],
                          [11, 12]]

C[0,0] = 1·7 + 2·9 + 3·11  =  7 + 18 + 33 =  58
C[0,1] = 1·8 + 2·10 + 3·12 =  8 + 20 + 36 =  64
C[1,0] = 4·7 + 5·9 + 6·11  = 28 + 45 + 66 = 139
C[1,1] = 4·8 + 5·10 + 6·12 = 32 + 50 + 72 = 154

C = [[ 58,  64],
     [139, 154]]        MAC = 2·2·3 = 12
```

```svg
<svg viewBox="0 0 640 290" xmlns="http://www.w3.org/2000/svg">
  <rect x="230" y="20" width="50" height="120" fill="#4a7bd0" fill-opacity="0.3"/> <rect x="60" y="200" width="150" height="40" fill="#e08a3c" fill-opacity="0.3"/>
  <rect x="230" y="200" width="50" height="40" fill="#3f9a6b" fill-opacity="0.45"/> <g fill="none" stroke="currentColor" stroke-width="1"> <rect x="230" y="20" width="50" height="40"/><rect x="280" y="20" width="50" height="40"/>
  <rect x="230" y="60" width="50" height="40"/><rect x="280" y="60" width="50" height="40"/> <rect x="230" y="100" width="50" height="40"/><rect x="280" y="100" width="50" height="40"/>
  <rect x="60" y="160" width="50" height="40"/><rect x="110" y="160" width="50" height="40"/><rect x="160" y="160" width="50" height="40"/>
  <rect x="60" y="200" width="50" height="40"/><rect x="110" y="200" width="50" height="40"/><rect x="160" y="200" width="50" height="40"/> <rect x="230" y="160" width="50" height="40"/><rect x="280" y="160" width="50" height="40"/>
  <rect x="230" y="200" width="50" height="40"/><rect x="280" y="200" width="50" height="40"/> </g> <g font-size="14" text-anchor="middle"> <text x="255" y="45">7</text><text x="305" y="45">8</text>
  <text x="255" y="85">9</text><text x="305" y="85">10</text> <text x="255" y="125">11</text><text x="305" y="125">12</text> <text x="85" y="185">1</text><text x="135" y="185">2</text><text x="185" y="185">3</text>
  <text x="85" y="225">4</text><text x="135" y="225">5</text><text x="185" y="225">6</text> <text x="255" y="185">58</text><text x="305" y="185">64</text> <text x="255" y="225">139</text><text x="305" y="225">154</text> </g>
  <text x="220" y="84" font-size="13" text-anchor="end">B [3×2]</text> <text x="135" y="264" font-size="13" text-anchor="middle">A [2×3]</text> <text x="280" y="264" font-size="13" text-anchor="middle">C = A·B [2×2]</text>
  <text x="360" y="44" font-size="13">출력 한 칸 = A의 행 하나 · B의 열 하나</text> <text x="360" y="68" font-size="13">칸 수 M·N = 4, 칸마다 K = 3 MAC</text> <text x="360" y="92" font-size="13">→ 전체 M·N·K = 12 MAC</text>
  <text x="360" y="180" font-size="13">C[1,0] = (A 1행, 주황) · (B 0열, 파랑)</text> <text x="360" y="204" font-size="13">= 4·7 + 5·9 + 6·11</text> <text x="360" y="228" font-size="13">= 28 + 45 + 66 = 139 (초록)</text>
</svg>
```

그림 3 — 행렬곱 배치도. B를 위에, A를 왼쪽에 두면 C의 각 칸은 "그 칸의 왼쪽에 있는 A의 행"과 "그 칸 위에 있는 B의 열"의 내적이다. 주황 행 × 파랑 열 = 초록 칸.

아래 코드는 numpy 결과와 삼중 루프 결과가 같고, MAC 수가 M·N·K임을 센다.

```python
import numpy as np
A = np.array([[1, 2, 3],
              [4, 5, 6]])            # [2, 3]
B = np.array([[7,  8],
              [9, 10],
              [11, 12]])             # [3, 2]
C = A @ B                            # [2, 2]
print(C)

# 삼중 루프로 같은 계산 + MAC 수 세기
M, K = A.shape; K2, N = B.shape
assert K == K2
C2 = np.zeros((M, N), dtype=int); macs = 0
for i in range(M):
    for j in range(N):
        for k in range(K):
            C2[i, j] += A[i, k] * B[k, j]; macs += 1
print("equal:", np.array_equal(C, C2), " MACs:", macs, "= M·N·K =", M*N*K)
```

```text
[[ 58  64]
 [139 154]]
equal: True  MACs: 12 = M·N·K = 12
```

출력에서 볼 것: 손계산과 같은 `58, 64, 139, 154`. MAC 카운터가 정확히 12.

### batch 차원: 여러 샘플을 한 번에

dense layer에 샘플 하나를 넣으면 `W[out, in] · x[in]` (행렬-벡터 곱, **GEMV**). 샘플 B개를 행으로 쌓은 `X[B, in]`을 넣으면

```
Y[B, out] = X[B, in] × Wᵀ[in, out] + b[out]
```

로 **행렬-행렬 곱(GEMM)** 한 번이 된다. 결과는 샘플별로 따로 계산한 것과 똑같다. 왜 굳이 묶나? 가중치 W를 메모리에서 **한 번 읽어서 B번 재사용**하기 때문이다. GEMV는 가중치 한 원소당 MAC 1번이라 메모리 대역폭에 묶이고(memory-bound), GEMM은 재사용이 커서 연산기에 묶인다(compute-bound). 이 차이가 D3(roofline)와 D5(LLM decode가 느린 이유)의 핵심이다.

아래 코드는 dense 256→128 레이어에 batch 32를 넣어 shape·파라미터·MAC을 확인한다.

```python
import numpy as np
np.random.seed(1)
W = np.random.randn(128, 256).astype(np.float32)   # dense 256 -> 128
b = np.random.randn(128).astype(np.float32)
X = np.random.randn(32, 256).astype(np.float32)    # batch 32

Y = X @ W.T + b                  # [32,256] x [256,128] -> [32,128], b는 broadcast
print("Y shape:", Y.shape)

# 샘플 하나씩 32번 돌린 것과 같다
Y_loop = np.stack([W @ X[n] + b for n in range(32)])
print("same as per-sample loop:", np.allclose(Y, Y_loop, atol=1e-4))

M, K, N = 32, 256, 128
print("params :", W.size + b.size)
print("MACs   :", M * K * N, " (per sample:", K * N, ")")
print("weight bytes fp32/int8:", W.nbytes, "/", W.size)
```

```text
Y shape: (32, 128)
same as per-sample loop: True
params : 32896
MACs   : 1048576  (per sample: 32768 )
weight bytes fp32/int8: 131072 / 32768
```

출력에서 볼 것: dense 256→128은 파라미터 32,896개(= 256·128 + 128), 샘플당 32,768 MAC. 가중치만 float32로 128 KB, int8로 32 KB. Cortex-M 계열 MCU의 SRAM이 수백 KB인 걸 생각하면 이 레이어 하나로 벌써 예산 얘기가 나온다.

### GEMM이 모든 레이어의 핵심인 이유

| 레이어 | GEMM으로 바꾸면 | M, K, N |
|---|---|---|
| Fully-connected | `X[B, in] × Wᵀ[in, out]` | B, in, out |
| Conv (im2col) | `W[C_out, C_in·k] × cols[C_in·k, T_out]` | C_out, C_in·k, 출력 위치 수 |
| 1×1 (pointwise) conv | `W[C_out, C_in] × X[C_in, H·W]` | C_out, C_in, H·W |
| Attention 점수 | `Q[T, d] × Kᵀ[d, T]` | T, d, T |
| Attention 출력 | `P[T, T] × V[T, d]` | T, T, d |
| LSTM/GRU 게이트 | `W[4h, in+h] × [x; h]` | 4h, in+h, 1 (GEMV) |

**conv를 GEMM으로 바꾸는 im2col 미리보기** (자세한 건 B2): 커널이 한 번에 보는 조각(`C_in × k`개 숫자)을 한 열로 펼쳐 모든 출력 위치에 대해 나열하면, conv가 가중치 행렬과 이 "펼친 행렬"의 곱이 된다. 아래 코드는 IMU 6축 1D conv를 im2col + 행렬곱으로 계산하고 PyTorch `conv1d`와 비교한다.

```python
import numpy as np
import torch
rng = np.random.default_rng(0)
C_in, T, C_out, K = 6, 8, 4, 3            # IMU 6축, 8샘플, 필터 4개, 커널 3
x = rng.standard_normal((C_in, T)).astype(np.float32)
w = rng.standard_normal((C_out, C_in, K)).astype(np.float32)

# im2col: 커널이 보는 조각(C_in·K개)을 열로 펼친다 -> [C_in·K, T_out]
T_out = T - K + 1
cols = np.stack([x[:, t:t+K].reshape(-1) for t in range(T_out)], axis=1)
Wm = w.reshape(C_out, C_in * K)           # [C_out, C_in·K]
y_gemm = Wm @ cols                        # [4,18] x [18,6] -> [4,6]

y_torch = torch.nn.functional.conv1d(torch.from_numpy(x)[None], torch.from_numpy(w))[0]
print("cols shape:", cols.shape, " Wm shape:", Wm.shape, " y shape:", y_gemm.shape)
print("matches torch conv1d:", np.allclose(y_gemm, y_torch.numpy(), atol=1e-5))
print("GEMM MACs = M·N·K =", C_out * T_out * (C_in * K))
```

```text
cols shape: (18, 6)  Wm shape: (4, 18)  y shape: (4, 6)
matches torch conv1d: True
GEMM MACs = M·N·K = 432
```

출력에서 볼 것: conv1d 결과와 im2col GEMM 결과가 일치. conv의 MAC 수도 GEMM 공식 `C_out · T_out · (C_in·K)` = 4·6·18 = 432로 바로 나온다.

**attention 미리보기** (자세한 건 B4): 시퀀스 길이 T=256, head 차원 d=64면 `QKᵀ`는 `[256, 64] × [64, 256]` → 256·256·64 = 4,194,304 MAC(head 하나). T가 두 배가 되면 이 항은 **네 배**. "transformer는 시퀀스 길이 제곱"이라는 말의 출처가 이 행렬곱 shape다.

결론: NPU·DSP 벤더가 "우리 칩은 GEMM(또는 conv)을 빨리 한다"에 집중하는 이유는 신경망 연산 시간의 대부분이 결국 이 한 가지 연산이기 때문이다.

### 함정

- `A @ B`와 `B @ A`는 다르다(교환법칙 없음). shape가 맞아도 값이 다르다.
- `A * B`(numpy)는 행렬곱이 아니라 **원소별 곱**이다. 행렬곱은 `@` 또는 `np.matmul`.
- MAC 수만으로 시간을 추정하면 틀리기 쉽다. GEMV·depthwise처럼 재사용이 적은 연산은 메모리 대역폭이 병목이다(D3).

---

## 6. transpose · reshape · broadcasting · elementwise

### transpose

`Aᵀ[j, i] = A[i, j]`. 행과 열을 바꾼다. `[M, K]` → `[K, M]`. 성질: `(AB)ᵀ = BᵀAᵀ`.

### reshape / view

원소의 **순서(메모리 순서)는 그대로** 두고 shape만 바꾼다. 원소 수가 같아야 한다. `[2, 3]` → `[3, 2]` 또는 `[6]` 또는 `[1, 2, 3]`. numpy·PyTorch에서 가능하면 복사 없이 **view**(같은 메모리를 다른 shape로 보는 것)를 돌려준다. C로 치면 같은 버퍼를 다른 타입의 배열 포인터로 캐스팅하는 것과 비슷하다.

**reshape와 transpose는 다르다.** 둘 다 `[2,3]` → `[3,2]`지만 결과가 다르다(아래 코드).

### broadcasting 규칙

shape가 다른 두 텐서를 원소별 연산할 때 numpy/PyTorch는 이렇게 맞춘다.

1. 두 shape를 **오른쪽 끝부터** 정렬한다. 짧은 쪽은 왼쪽에 1을 채운다.
2. 각 축에서 길이가 **같거나, 한쪽이 1**이면 OK. 1인 쪽은 그 축으로 복사된 것처럼 취급한다(실제 복사는 안 함).
3. 둘 다 1이 아니고 다르면 오류.

| A shape | B shape | 결과 | 설명 |
|---|---|---|---|
| `[4, 3]` | `[3]` | `[4, 3]` | 특징별 평균 빼기 (정규화) |
| `[32, 128]` | `[128]` | `[32, 128]` | dense layer bias 더하기 |
| `[8, 1]` | `[1, 5]` | `[8, 5]` | 외적(outer product) 모양 |
| `[4, 1]` | `[4]` | `[4, 4]` | **함정**: 의도는 `[4]` |
| `[4, 3]` | `[4]` | 오류 | 오른쪽 끝 3 vs 4 |

**elementwise(원소별) 연산**: `+ − × ÷`, `exp`, `ReLU` 등은 같은 위치끼리만 계산한다. MAC이 아니라 원소 수만큼만 연산하므로 연산량은 작지만, 텐서 전체를 읽고 쓰므로 **메모리 트래픽**은 크다. 그래서 컴파일러가 conv + bias + ReLU를 하나로 합치는 op fusion(C6)을 한다.

아래 코드는 reshape vs transpose, IMU feature 정규화 broadcasting, 그리고 대표 함정 두 가지를 보인다.

```python
import numpy as np
X = np.arange(6).reshape(2, 3)          # [2,3]
print("X.T shape:", X.T.shape)
print("reshape(3,2):\n", X.reshape(3, 2))  # 순서 유지, 모양만 바뀜
print("X.T (다름!):\n", X.T)

# broadcasting: 오른쪽 끝 축부터 맞춰 보고, 1이거나 같으면 OK
feat = np.array([[1., 2., 3.],
                 [4., 5., 6.],
                 [7., 8., 9.],
                 [10., 11., 12.]])       # [4 샘플, 3 특징]
mu = feat.mean(axis=0)                   # [3]
sd = feat.std(axis=0)                    # [3]
z = (feat - mu) / sd                     # [4,3] - [3] -> [4,3]
print("mu:", mu, " z[0]:", np.round(z[0], 4))

# 함정: 열 벡터 [4,1] 과 [4] 를 더하면 [4,4]가 된다
col = feat[:, :1]                        # [4,1]
row = feat[:, 0]                         # [4]
print("[4,1] + [4] ->", (col + row).shape, " (원래 의도는 (4,) 였다)")
try:
    feat + np.ones(4)                    # [4,3] + [4] -> 오류
except ValueError as e:
    print("ValueError:", e)
```

```text
X.T shape: (3, 2)
reshape(3,2):
 [[0 1]
 [2 3]
 [4 5]]
X.T (다름!):
 [[0 3]
 [1 4]
 [2 5]]
mu: [5.5 6.5 7.5]  z[0]: [-1.3416 -1.3416 -1.3416]
[4,1] + [4] -> (4, 4)  (원래 의도는 (4,) 였다)
ValueError: operands could not be broadcast together with shapes (4,3) (4,) 
```

출력에서 볼 것: `reshape(3,2)`는 `0 1 / 2 3 / 4 5`(메모리 순서대로 다시 자름), `.T`는 `0 3 / 1 4 / 2 5`(행·열 교환). `[4,1] + [4]`는 **오류 없이** `[4,4]`가 된다 — 조용히 틀리는 것이 오류보다 무섭다.

### Don 경험과 연결

broadcasting 함정은 펌웨어의 **암묵적 형 변환 버그**와 성격이 같다. 컴파일은 되는데 값이 틀린다. 해결법도 같다: shape를 `assert`로 박아 둔다(`assert y.shape == (B, 128)`). 센서 파이프라인에서 채널별 offset·gain 보정(`(raw − offset[C]) × gain[C]`)이 바로 `[T, C]`와 `[C]`의 broadcasting이다.

---

## 7. 메모리 레이아웃 — row-major, stride, NCHW vs NHWC

여기부터는 Don의 홈그라운드다. 텐서는 수학적으로 n차원이지만 **메모리는 1차원**이다. n차원 인덱스를 1차원 주소로 바꾸는 규칙이 레이아웃이다.

### row-major vs column-major

- **row-major (C order)**: 마지막 축이 가장 빨리 변한다. 같은 행의 원소가 메모리에 이웃. C, numpy 기본, PyTorch.
- **column-major (Fortran order)**: 첫 축이 가장 빨리 변한다. 같은 열이 이웃. Fortran, MATLAB, 전통 BLAS 인터페이스.

`[3, 4]` float32 행렬 A가 row-major면 `A[i][j]`의 주소는 `base + (i·4 + j)·4` 바이트다.

```svg
<svg viewBox="0 0 640 250" xmlns="http://www.w3.org/2000/svg">
  <text x="120" y="20" font-size="13" text-anchor="middle">A [3, 4] float32 (논리적 모양)</text> <rect x="40" y="30" width="160" height="32" fill="#4a7bd0" fill-opacity="0.3"/>
  <rect x="40" y="62" width="160" height="32" fill="#e08a3c" fill-opacity="0.3"/> <rect x="40" y="94" width="160" height="32" fill="#3f9a6b" fill-opacity="0.3"/> <g fill="none" stroke="currentColor" stroke-width="1">
  <rect x="40" y="30" width="40" height="32"/><rect x="80" y="30" width="40" height="32"/><rect x="120" y="30" width="40" height="32"/><rect x="160" y="30" width="40" height="32"/>
  <rect x="40" y="62" width="40" height="32"/><rect x="80" y="62" width="40" height="32"/><rect x="120" y="62" width="40" height="32"/><rect x="160" y="62" width="40" height="32"/>
  <rect x="40" y="94" width="40" height="32"/><rect x="80" y="94" width="40" height="32"/><rect x="120" y="94" width="40" height="32"/><rect x="160" y="94" width="40" height="32"/> </g> <g font-size="13" text-anchor="middle">
  <text x="60" y="51">0</text><text x="100" y="51">1</text><text x="140" y="51">2</text><text x="180" y="51">3</text> <text x="60" y="83">4</text><text x="100" y="83">5</text><text x="140" y="83">6</text><text x="180" y="83">7</text>
  <text x="60" y="115">8</text><text x="100" y="115">9</text><text x="140" y="115">10</text><text x="180" y="115">11</text> </g> <text x="230" y="50" font-size="13">stride = (16, 4) byte</text>
  <text x="230" y="74" font-size="13">A[i][j] → offset = i·16 + j·4</text> <text x="230" y="98" font-size="13">j 방향(→)은 4 byte씩: 연속, 캐시 친화적</text> <text x="230" y="122" font-size="13">i 방향(↓)은 16 byte씩 점프</text>
  <text x="40" y="160" font-size="13">메모리 (row-major / C order): 행 0 → 행 1 → 행 2 순서로 이어 붙임</text> <rect x="40" y="170" width="180" height="32" fill="#4a7bd0" fill-opacity="0.3"/>
  <rect x="220" y="170" width="180" height="32" fill="#e08a3c" fill-opacity="0.3"/> <rect x="400" y="170" width="180" height="32" fill="#3f9a6b" fill-opacity="0.3"/> <g fill="none" stroke="currentColor" stroke-width="1">
  <rect x="40" y="170" width="45" height="32"/><rect x="85" y="170" width="45" height="32"/><rect x="130" y="170" width="45" height="32"/><rect x="175" y="170" width="45" height="32"/>
  <rect x="220" y="170" width="45" height="32"/><rect x="265" y="170" width="45" height="32"/><rect x="310" y="170" width="45" height="32"/><rect x="355" y="170" width="45" height="32"/>
  <rect x="400" y="170" width="45" height="32"/><rect x="445" y="170" width="45" height="32"/><rect x="490" y="170" width="45" height="32"/><rect x="535" y="170" width="45" height="32"/> </g> <g font-size="13" text-anchor="middle">
  <text x="62.5" y="191">0</text><text x="107.5" y="191">1</text><text x="152.5" y="191">2</text><text x="197.5" y="191">3</text>
  <text x="242.5" y="191">4</text><text x="287.5" y="191">5</text><text x="332.5" y="191">6</text><text x="377.5" y="191">7</text>
  <text x="422.5" y="191">8</text><text x="467.5" y="191">9</text><text x="512.5" y="191">10</text><text x="557.5" y="191">11</text> </g> <g font-size="12" text-anchor="middle">
  <text x="62.5" y="220">+0</text><text x="107.5" y="220">+4</text><text x="152.5" y="220">+8</text><text x="197.5" y="220">+12</text>
  <text x="242.5" y="220">+16</text><text x="287.5" y="220">+20</text><text x="332.5" y="220">+24</text><text x="377.5" y="220">+28</text>
  <text x="422.5" y="220">+32</text><text x="467.5" y="220">+36</text><text x="512.5" y="220">+40</text><text x="557.5" y="220">+44</text> </g> <text x="40" y="243" font-size="12">바이트 오프셋 (base 기준)</text>
</svg>
```

그림 4 — row-major 레이아웃. 논리적인 2차원 표가 행 단위로 한 줄로 이어 붙는다. 같은 색 = 같은 행 = 메모리에서 연속.

### stride — 레이아웃을 숫자로 표현하는 법

**stride**는 "그 축의 인덱스를 1 올리면 메모리에서 몇 칸(바이트) 이동하나"다. `[3, 4]` float32 row-major의 stride는 바이트로 `(16, 4)`. 일반적으로 n차원 텐서의 주소는

```
addr = base + ∑ₐ index[a] · stride[a]
```

말로 하면: **각 축의 인덱스 × 그 축의 stride를 모두 더하면 오프셋.** C 컴파일러가 `t[c][h][w]`를 보고 만드는 주소 계산이 정확히 이것이다.

transpose가 복사 없이 되는 비밀도 stride다. `.T`는 데이터를 옮기지 않고 **stride 순서만 뒤집은 view**를 만든다. 대신 결과는 더 이상 연속(contiguous)이 아니다.

아래 코드는 numpy stride, transpose view, Fortran 순서, 그리고 NCHW/NHWC 실제 메모리 순서를 출력한다.

```python
import numpy as np
A = np.arange(12, dtype=np.float32).reshape(3, 4)   # row-major (C order)
print("strides (bytes):", A.strides, " C_contiguous:", A.flags['C_CONTIGUOUS'])
At = A.T                                            # 복사 없이 stride만 바꾼 view
print("A.T strides:", At.strides, " C_contiguous:", At.flags['C_CONTIGUOUS'])
print("shares memory:", np.shares_memory(A, At))
F = np.asfortranarray(A)                            # column-major 복사본
print("F strides:", F.strides, " raw order:", F.ravel(order='K')[:6])

# NCHW vs NHWC: 같은 이미지, 메모리 순서만 다르다
N, C, H, W = 1, 3, 2, 2
nchw = np.arange(N*C*H*W).reshape(N, C, H, W)
nhwc = nchw.transpose(0, 2, 3, 1).copy()           # 실제로 재배치
print("NCHW memory:", nchw.ravel().tolist())
print("NHWC memory:", nhwc.ravel().tolist())
print("NCHW strides (elements):", [s // nchw.itemsize for s in nchw.strides])
print("NHWC strides (elements):", [s // nhwc.itemsize for s in nhwc.strides])
```

```text
strides (bytes): (16, 4)  C_contiguous: True
A.T strides: (4, 16)  C_contiguous: False
shares memory: True
F strides: (4, 12)  raw order: [0. 4. 8. 1. 5. 9.]
NCHW memory: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
NHWC memory: [0, 4, 8, 1, 5, 9, 2, 6, 10, 3, 7, 11]
NCHW strides (elements): [12, 4, 2, 1]
NHWC strides (elements): [12, 6, 3, 1]
```

출력에서 볼 것: `A.T`는 같은 메모리를 공유하며 stride만 `(16,4)` → `(4,16)`. column-major 복사본은 메모리에 `0, 4, 8`(열 0)부터 들어 있다. NHWC 메모리에서는 채널 0·1·2의 원소(`0, 4, 8`)가 **한 픽셀에 모여** 나란히 있다.

### C로 확인: 3차원 배열 주소 계산

아래 C 코드는 `int16_t t[3][2][4]`(CHW 순서)에서 `t[2][1][3]`의 오프셋을 stride 공식으로 직접 계산하고 컴파일러 주소와 비교한다.

```c
#include <stdio.h>
#include <stdint.h>

#define C_ 3
#define H_ 2
#define W_ 4
static int16_t t[C_][H_][W_];          /* CHW 순서, int16 */

int main(void) {
    for (int c = 0; c < C_; c++)
        for (int h = 0; h < H_; h++)
            for (int w = 0; w < W_; w++)
                t[c][h][w] = (int16_t)(c * 100 + h * 10 + w);

    const int16_t *base = &t[0][0][0];
    int c = 2, h = 1, w = 3;
    /* stride(요소 단위): c -> H·W, h -> W, w -> 1 */
    long off = (long)c * (H_ * W_) + (long)h * W_ + w;
    printf("offset = %d*%d + %d*%d + %d = %ld\n", c, H_ * W_, h, W_, w, off);
    printf("t[2][1][3] = %d, base[off] = %d\n", t[c][h][w], base[off]);
    printf("byte offset = %ld x sizeof(int16_t) = %ld bytes\n", off, (long)((const char *)&t[c][h][w] - (const char *)base));

    /* 같은 버퍼를 HWC(NHWC 방식)로 읽으려면 stride가 달라진다 */
    printf("HWC stride would be: h -> %d, w -> %d, c -> 1\n", W_ * C_, C_);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 addr.c -o addr && ./addr
```

```text
offset = 2*8 + 1*4 + 3 = 23
t[2][1][3] = 213, base[off] = 213
byte offset = 23 x sizeof(int16_t) = 46 bytes
HWC stride would be: h -> 12, w -> 3, c -> 1
```

출력에서 볼 것: 요소 오프셋 23 = 2·8 + 1·4 + 3, 바이트 오프셋은 여기에 `sizeof(int16_t)` = 2를 곱한 46. numpy의 `strides`는 이 "축별 곱셈 계수"를 바이트로 저장한 것일 뿐이다. (경고 0개로 컴파일됨.)

### NCHW vs NHWC

이미지·스펙트로그램 같은 4차원 activation 텐서 `[N, C, H, W]`는 두 가지 순서가 흔하다.

```svg
<svg viewBox="0 0 640 220" xmlns="http://www.w3.org/2000/svg">
  <text x="40" y="20" font-size="13">같은 [1, 3, 2, 2] 텐서 (채널 R·G·B, 픽셀 0~3)의 메모리 순서</text> <text x="40" y="44" font-size="13">NCHW</text> <rect x="40" y="52" width="180" height="32" fill="#4a7bd0" fill-opacity="0.35"/>
  <rect x="220" y="52" width="180" height="32" fill="#e08a3c" fill-opacity="0.35"/> <rect x="400" y="52" width="180" height="32" fill="#3f9a6b" fill-opacity="0.35"/> <text x="40" y="124" font-size="13">NHWC</text>
  <rect x="40" y="132" width="45" height="32" fill="#4a7bd0" fill-opacity="0.35"/><rect x="85" y="132" width="45" height="32" fill="#e08a3c" fill-opacity="0.35"/><rect x="130" y="132" width="45" height="32" fill="#3f9a6b" fill-opacity="0.35"/>
  <rect x="175" y="132" width="45" height="32" fill="#4a7bd0" fill-opacity="0.35"/><rect x="220" y="132" width="45" height="32" fill="#e08a3c" fill-opacity="0.35"/><rect x="265" y="132" width="45" height="32" fill="#3f9a6b" fill-opacity="0.35"/>
  <rect x="310" y="132" width="45" height="32" fill="#4a7bd0" fill-opacity="0.35"/><rect x="355" y="132" width="45" height="32" fill="#e08a3c" fill-opacity="0.35"/><rect x="400" y="132" width="45" height="32" fill="#3f9a6b" fill-opacity="0.35"/>
  <rect x="445" y="132" width="45" height="32" fill="#4a7bd0" fill-opacity="0.35"/><rect x="490" y="132" width="45" height="32" fill="#e08a3c" fill-opacity="0.35"/><rect x="535" y="132" width="45" height="32" fill="#3f9a6b" fill-opacity="0.35"/>
  <g fill="none" stroke="currentColor" stroke-width="1"> <rect x="40" y="52" width="45" height="32"/><rect x="85" y="52" width="45" height="32"/><rect x="130" y="52" width="45" height="32"/><rect x="175" y="52" width="45" height="32"/>
  <rect x="220" y="52" width="45" height="32"/><rect x="265" y="52" width="45" height="32"/><rect x="310" y="52" width="45" height="32"/><rect x="355" y="52" width="45" height="32"/>
  <rect x="400" y="52" width="45" height="32"/><rect x="445" y="52" width="45" height="32"/><rect x="490" y="52" width="45" height="32"/><rect x="535" y="52" width="45" height="32"/>
  <rect x="40" y="132" width="45" height="32"/><rect x="85" y="132" width="45" height="32"/><rect x="130" y="132" width="45" height="32"/><rect x="175" y="132" width="45" height="32"/>
  <rect x="220" y="132" width="45" height="32"/><rect x="265" y="132" width="45" height="32"/><rect x="310" y="132" width="45" height="32"/><rect x="355" y="132" width="45" height="32"/>
  <rect x="400" y="132" width="45" height="32"/><rect x="445" y="132" width="45" height="32"/><rect x="490" y="132" width="45" height="32"/><rect x="535" y="132" width="45" height="32"/> </g> <g font-size="13" text-anchor="middle">
  <text x="62.5" y="73">R0</text><text x="107.5" y="73">R1</text><text x="152.5" y="73">R2</text><text x="197.5" y="73">R3</text>
  <text x="242.5" y="73">G0</text><text x="287.5" y="73">G1</text><text x="332.5" y="73">G2</text><text x="377.5" y="73">G3</text>
  <text x="422.5" y="73">B0</text><text x="467.5" y="73">B1</text><text x="512.5" y="73">B2</text><text x="557.5" y="73">B3</text>
  <text x="62.5" y="153">R0</text><text x="107.5" y="153">G0</text><text x="152.5" y="153">B0</text><text x="197.5" y="153">R1</text>
  <text x="242.5" y="153">G1</text><text x="287.5" y="153">B1</text><text x="332.5" y="153">R2</text><text x="377.5" y="153">G2</text>
  <text x="422.5" y="153">B2</text><text x="467.5" y="153">R3</text><text x="512.5" y="153">G3</text><text x="557.5" y="153">B3</text> </g> <text x="40" y="104" font-size="12">채널 하나(feature map 한 장)가 통째로 연속 — "planar"</text>
  <text x="40" y="186" font-size="12">픽셀 하나의 모든 채널이 연속 — "interleaved"</text> <text x="40" y="208" font-size="12">stride (요소): NCHW = [12, 4, 2, 1],  NHWC = [12, 6, 3, 1]</text>
</svg>
```

그림 5 — NCHW는 채널별 평면을 이어 붙이고(planar), NHWC는 픽셀마다 채널을 끼워 넣는다(interleaved). 위 numpy 출력 `[0,4,8,1,5,9,…]`이 아래 줄의 순서다.

| | NCHW | NHWC |
|---|---|---|
| 연속인 것 | 한 채널의 H×W 평면 | 한 픽셀의 C개 채널 |
| 주로 쓰는 곳 | PyTorch 기본, 전통 cuDNN | TensorFlow/TFLite, TFLite Micro, CMSIS-NN, 다수 모바일 NPU |
| 유리한 연산 | 채널별 독립 처리 (depthwise, 평면 단위 DMA) | 1×1 conv·dense처럼 **채널 방향 내적** (채널이 연속이라 SIMD로 바로 로드) |
| 센서 비유 | 축별로 따로 모은 버퍼 (ax 50개, ay 50개, …) | 샘플마다 6축을 묶은 FIFO 프레임 (ax ay az gx gy gz, …) |

### 레이아웃이 NPU·DMA에서 중요한 이유

- **DMA는 연속 구간을 좋아한다.** NPU가 "채널 16개 단위 블록"을 원하는데 텐서가 NCHW로 있으면 DMA descriptor가 잘게 쪼개지거나(2D/3D stride DMA), CPU가 재배치(transpose)를 해야 한다. 이 재배치가 레이어 실행 시간보다 길어지는 경우도 흔하다.
- **SIMD/MAC 배열은 특정 축이 연속이길 원한다.** 채널이 연속이면 int8 16개를 한 번에 로드해 채널 방향 내적을 벡터 명령으로 할 수 있다. 그래서 채널 수를 8/16/32의 배수로 맞추라는 요구가 나온다(C6).
- **PyTorch(NCHW)로 학습한 모델을 TFLite(NHWC)로 변환**하면 변환기가 transpose op를 끼워 넣는다. 프로파일에서 "Transpose"가 크게 보이면 레이아웃 불일치를 의심한다.
- 벤더 NPU는 자체 블록 레이아웃(예: 채널을 몇 개씩 묶어 타일링한 형태)을 쓰기도 한다. 이름은 벤더마다 다르니 SDK 문서의 layout 절을 먼저 확인하는 습관을 들이자.

---

## 8. C로 구현하고 재 보기 — 루프 순서와 캐시

### naive 삼중 루프 두 가지

`C[i][j] = ∑ₖ A[i][k]·B[k][j]`의 루프 순서는 6가지(ijk, ikj, jik, …)가 모두 같은 결과를 낸다. 하지만 메모리 접근 패턴이 다르다.

| 순서 | 가장 안쪽 루프가 도는 것 | A 접근 | B 접근 | C 접근 |
|---|---|---|---|---|
| ijk | k | 행을 따라 연속 | **열을 따라 점프 (stride N·4 byte)** | 레지스터 누산 |
| ikj | j | 고정(스칼라) | 행을 따라 연속 | 행을 따라 연속 |

ijk에서 B를 `B[k][j]`, `B[k+1][j]`, … 로 읽으면 매번 한 행(256·4 = 1 KB)씩 점프한다. 캐시 라인(이 Mac은 128 B, 흔한 Cortex-A/M7은 32~64 B)을 가져와서 4 바이트만 쓰고 버리는 셈이다. ikj는 안쪽 루프가 B와 C의 행을 **연속으로** 훑어서 캐시 라인을 다 쓰고, 컴파일러가 NEON SIMD로 자동 벡터화하기도 쉽다.

아래 코드는 256×256 float 행렬곱을 두 순서로 구현해 5번 중 최고 시간을 잰다.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define N 256
static float A[N][N], B[N][N], C[N][N];

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static void mm_ijk(void) {            /* 교과서 순서 */
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            float acc = 0.0f;
            for (int k = 0; k < N; k++)
                acc += A[i][k] * B[k][j];     /* B를 열 방향으로 점프 */
            C[i][j] = acc;
        }
}
static void mm_ikj(void) {            /* 루프 순서만 바꿈 */
    memset(C, 0, sizeof C);
    for (int i = 0; i < N; i++)
        for (int k = 0; k < N; k++) {
            float a = A[i][k];
            for (int j = 0; j < N; j++)
                C[i][j] += a * B[k][j];       /* B, C 모두 연속 접근 */
        }
}
static double best_of(void (*f)(void), int reps) {
    double best = 1e30;
    for (int r = 0; r < reps; r++) {
        double t0 = now_ms(); f(); double dt = now_ms() - t0;
        if (dt < best) best = dt;
    }
    return best;
}
int main(void) {
    srand(0);
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) {
            A[i][j] = (float)(rand() % 1000) / 1000.0f; B[i][j] = (float)(rand() % 1000) / 1000.0f;
        }
    double macs = (double)N * N * N;
    double t1 = best_of(mm_ijk, 5); float c1 = C[17][42];
    double t2 = best_of(mm_ikj, 5); float c2 = C[17][42];
    printf("N=%d, MACs=%.0f\n", N, macs);
    printf("ijk: %7.3f ms  %6.2f GMAC/s  C[17][42]=%.4f\n", t1, macs / t1 / 1e6, c1);
    printf("ikj: %7.3f ms  %6.2f GMAC/s  C[17][42]=%.4f\n", t2, macs / t2 / 1e6, c2);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 matmul.c -o matmul && ./matmul
cc -std=c11 -Wall -Wextra -O2 -fno-vectorize -fno-slp-vectorize matmul.c -o matmul_nv && ./matmul_nv
```

Apple M2 MacBook, Apple clang, `-O2` (경고 0개). 실제 출력:

```text
N=256, MACs=16777216
ijk:  14.966 ms    1.12 GMAC/s  C[17][42]=69.0069
ikj:   1.207 ms   13.90 GMAC/s  C[17][42]=69.0069
```

자동 벡터화를 끈 빌드(`-fno-vectorize -fno-slp-vectorize`):

```text
N=256, MACs=16777216
ijk:  15.375 ms    1.09 GMAC/s  C[17][42]=69.0069
ikj:   5.677 ms    2.96 GMAC/s  C[17][42]=69.0069
```

같은 코드를 `N 512`로 바꿨을 때(`-O2`):

```text
N=512, MACs=134217728
ijk: 140.966 ms    0.95 GMAC/s  C[17][42]=127.6519
ikj:   9.794 ms   13.70 GMAC/s  C[17][42]=127.6519
```

출력에서 볼 것:

- 두 순서의 결과(`C[17][42]`)는 같고, **루프 순서만 바꿨는데 약 12배** 차이.
- 벡터화를 꺼도 ikj가 약 2.7배 빠르다 → 이게 순수한 **메모리 접근 패턴(캐시) 효과**. 나머지 약 4.7배는 연속 접근 덕분에 가능해진 **SIMD 벡터화** 효과다. (ijk의 안쪽 루프는 float 누산 순서를 바꿀 수 없어서 `-O2`에서 벡터화되지 않는다 — float 덧셈은 결합법칙이 성립하지 않기 때문.)
- N을 두 배로 하면 MAC은 8배(N³). 시간도 ikj 기준 1.2 ms → 9.8 ms로 대략 8배.
- 절대 숫자는 **이 Mac에서만 유효**하다. CPU, 컴파일러, 전력 상태, 백그라운드 작업에 따라 달라진다. 비교할 건 비율과 경향이다.

### numpy(BLAS)와 비교

아래 코드는 numpy `@`(이 환경에서는 Apple Accelerate BLAS에 연결)를 같은 방식(최고 시간)으로 잰다.

```python
import time
import numpy as np
rng = np.random.default_rng(0)
for n in (256, 512):
    A = rng.random((n, n), dtype=np.float32)
    B = rng.random((n, n), dtype=np.float32)
    best = 1e9
    for _ in range(20):
        t0 = time.perf_counter(); C = A @ B; dt = time.perf_counter() - t0
        best = min(best, dt)
    macs = n ** 3
    print(f"numpy n={n}: {best*1e3:7.3f} ms  {macs/best/1e9:7.2f} GMAC/s")
```

```text
numpy n=256:   0.028 ms   591.27 GMAC/s
numpy n=512:   0.196 ms   686.54 GMAC/s
```

같은 머신에서 정리하면 (N=256, 수치는 실행마다 수 % 흔들림):

| 구현 | 시간 | GMAC/s | naive ijk 대비 |
|---|---|---|---|
| C ijk `-O2` | 약 15 ms | 약 1.1 | 1× |
| C ikj `-O2` | 약 1.2 ms | 약 14 | 약 12× |
| numpy / Accelerate BLAS | 약 0.03 ms | 약 560~690 | 약 500× |

BLAS가 훨씬 빠른 이유: (1) **tiling/blocking** — 행렬을 L1/L2에 들어가는 작은 블록으로 잘라 한 번 가져온 데이터를 여러 번 재사용, (2) 레지스터 블로킹 + SIMD, (3) 멀티코어, (4) Apple 칩에서는 Accelerate가 CPU 옆의 전용 행렬 연산 하드웨어를 쓰는 것으로 알려져 있다(Apple이 공식 문서로 세부를 공개하지는 않음). (4)는 곧 **"행렬곱 전용 하드웨어를 두면 CPU보다 수십 배 빠르다"** — NPU가 존재하는 이유 그 자체다.

### Don 경험과 연결

- SSD 펌웨어에서 "매핑 테이블을 순차로 훑느냐, 랜덤으로 찌르느냐"로 성능이 갈렸던 것과 완전히 같은 이야기다. 행렬곱의 루프 순서 = **접근 패턴 설계**.
- tiling은 DMA 더블 버퍼링과 같은 발상이다. 큰 행렬을 SRAM/TCM에 들어가는 타일로 잘라 DMA로 가져오고, 계산하는 동안 다음 타일을 가져온다. NPU 컴파일러가 하는 일의 큰 부분이 이 타일 크기 결정이다(F·I 모듈).
- MCU에서는 캐시가 없거나 작고(TCM 사용), 대신 **메모리 계층 사이 DMA를 누가 언제 하느냐**가 같은 역할을 한다.

---

## 9. 항등행렬 · 역행렬 · rank · 고유값 · SVD (직관만)

### 항등행렬과 역행렬

- **항등행렬 I**: 대각선이 1, 나머지 0. `I·x = x`. "아무것도 안 하는 변환". residual connection(`y = x + f(x)`)은 "항등 변환 + 보정"으로 볼 수 있다(B1).
- **역행렬 A⁻¹**: `A·A⁻¹ = I`. 변환을 되돌리는 행렬. 정사각이고 det ≠ 0일 때만 존재.

추론(inference)에서 역행렬을 직접 계산할 일은 거의 없다. 다만 선형 회귀의 정규방정식, 센서 보정(가속도계 3×3 보정 행렬), 칼만 필터에서 나온다. 수치적으로 불안정하므로 실무에서는 `inv`보다 `solve`나 분해를 쓴다.

### rank

**rank** = 행렬의 서로 독립인 행(또는 열)의 개수 = 변환 결과가 차지하는 공간의 차원. `[[1,2],[2,4]]`는 둘째 행이 첫 행의 2배라 rank 1이다. 2차원 입력을 1차원 선 위로 찌그러뜨린다(det = 0, 역행렬 없음).

ML 관점에서 중요한 직관: **rank가 낮은 행렬은 정보가 적어서, 더 작은 행렬 두 개의 곱으로 쓸 수 있다.**

아래 코드는 그림 2의 변환 T로 항등·역행렬·det·rank·고유값을 확인한다.

```python
import numpy as np
T = np.array([[2., 1.],
              [0., 1.]])                     # x축 2배 + shear
square = np.array([[0, 1, 1, 0],
                   [0, 0, 1, 1]], dtype=float)  # 단위 정사각형 꼭짓점 (열 = 점)
print("transformed corners:\n", T @ square)
print("det =", np.linalg.det(T), "(넓이 배율)")

I = np.eye(2)
Tinv = np.linalg.inv(T)
print("T^-1 =\n", Tinv)
print("T @ T^-1 == I:", np.allclose(T @ Tinv, I))

S = np.array([[1., 2.],
              [2., 4.]])                     # 두 번째 행 = 첫 행 × 2
print("rank(S) =", np.linalg.matrix_rank(S), " det(S) =", np.linalg.det(S))
vals, vecs = np.linalg.eig(T)
print("eigenvalues of T:", vals)
```

```text
transformed corners:
 [[0. 2. 3. 1.]
 [0. 0. 1. 1.]]
det = 2.0 (넓이 배율)
T^-1 =
 [[ 0.5 -0.5]
 [ 0.   1. ]]
T @ T^-1 == I: True
rank(S) = 1  det(S) = 0.0
eigenvalues of T: [2. 1.]
```

출력에서 볼 것: 꼭짓점 `(0,0),(2,0),(3,1),(1,1)`이 그림 2와 일치. rank 1인 S는 det 0.

### 고유값·고유벡터 (직관)

`A·v = λ·v`를 만족하는 방향 `v`가 **고유벡터**, 배율 `λ`가 **고유값**이다. 말로 하면: **변환해도 방향이 안 바뀌고 길이만 λ배 되는 특별한 방향.** 위 T에서 x축 방향 `(1,0)`은 2배로 늘기만 한다(λ=2).

ML에서 나오는 곳: PCA(공분산 행렬의 고유벡터 = 데이터가 가장 퍼진 방향, IMU 특징 차원 축소에 쓰임), RNN에서 같은 행렬을 반복 곱할 때 고유값이 1보다 크면 폭발·작으면 소멸(A3의 exploding/vanishing gradient 직관). 제어 이론에서 "극점이 단위원 밖이면 발산"과 같은 이야기다.

### SVD — 모든 행렬을 "회전 · 늘이기 · 회전"으로

아무 `[m, n]` 행렬이나

```
W = U · Σ · Vᵀ
```

로 분해된다. U, V는 회전(직교 행렬), Σ는 대각선에 **특이값(singular value)** σ₁ ≥ σ₂ ≥ … ≥ 0이 있는 행렬. 말로 하면: **어떤 선형 변환이든 "돌리고 → 축 방향으로 늘이고 → 다시 돌리는" 세 단계**이고, 특이값은 각 방향의 중요도다.

**저랭크 근사(low-rank approximation)**: 큰 특이값 k개만 남기면

```
W  ≈  U[:, :k] · Σ[:k] · Vᵀ[:k]  =  (U_k Σ_k) · V_kᵀ
[m,n]          [m,k]              [k,n]
```

파라미터 수가 `m·n` → `k·(m+n)`으로 준다. dense layer 하나를 **폭 k짜리 dense 두 개**로 쪼개는 것과 같다. 이게 모델 압축(C 모듈), LoRA 파인튜닝(가중치 변화량을 저랭크로 학습)의 직관이다. 이 근사가 L2(Frobenius) 기준으로 최선이라는 것이 Eckart–Young 정리다.

아래 코드는 "rank 16 구조 + 잡음"을 가진 256×256 행렬을 rank k로 근사하며 오차와 파라미터 절감을 출력한다.

```python
import numpy as np
rng = np.random.default_rng(0)
# 진짜 가중치처럼 '저랭크 구조 + 약간의 잡음'을 가진 256x256 행렬
U0 = rng.standard_normal((256, 16)); V0 = rng.standard_normal((16, 256))
W = U0 @ V0 + 0.5 * rng.standard_normal((256, 256))

U, S, Vt = np.linalg.svd(W, full_matrices=False)
print("top singular values:", np.round(S[:3], 1), "...", np.round(S[15:18], 1))
full = W.size
for k in (4, 16, 64):
    Wk = (U[:, :k] * S[:k]) @ Vt[:k]            # rank-k 근사
    err = np.linalg.norm(W - Wk) / np.linalg.norm(W)
    params = 256 * k + k * 256                  # [256,k] 와 [k,256] 두 행렬
    print(f"rank {k:3d}: rel. error {err:.4f}  params {params:6d} ({params/full:.1%} of {full})")
print("rank of W (numerical):", np.linalg.matrix_rank(W))
```

```text
top singular values: [343.3 326.6 312.2] ... [180.   15.2  15. ]
rank   4: rel. error 0.7859  params   2048 (3.1% of 65536)
rank  16: rel. error 0.1157  params   8192 (12.5% of 65536)
rank  64: rel. error 0.0787  params  32768 (50.0% of 65536)
rank of W (numerical): 256
```

출력에서 볼 것:

- 특이값이 16번째(180)까지 크고 17번째부터 15 수준으로 **뚝 떨어진다**. 행렬에 숨은 구조가 rank 16이라는 신호.
- rank 16 근사는 파라미터 12.5%로 상대 오차 약 11.6%. rank 64로 올려도 오차는 7.9%로 조금밖에 안 준다(남은 건 잡음). 반대로 rank 4는 구조를 못 담아 오차 79%.
- 잡음 때문에 수치적 rank는 256(풀 랭크)이다. "rank가 꽉 찼다"와 "저랭크로 잘 근사된다"는 다른 이야기다. 실제 학습된 가중치가 얼마나 저랭크인지는 레이어마다 달라서 특이값 분포를 직접 봐야 한다.

---

## 10. 숫자 타입 미리보기 — float32 · float16 · int8

같은 행렬이라도 원소 하나를 몇 바이트로 저장하느냐에 따라 메모리·대역폭·MAC 비용이 달라진다. 자세한 이론은 **모듈 C(C1 quantization)**에서 다룬다. 여기서는 감만 잡는다.

아래 코드는 같은 값을 float32/float16/int8(대칭 양자화)로 저장했을 때의 크기와 오차를 본다.

```python
import numpy as np
w = np.array([0.1, -1.2345678, 3.14159265, 1e-5], dtype=np.float32)
for dt in (np.float32, np.float16):
    v = w.astype(dt)
    print(f"{np.dtype(dt).name:8s} bytes/elem={v.itemsize}  values={v}")
# int8 (대칭 양자화 미리보기 — 자세한 건 C1)
scale = np.abs(w).max() / 127
q = np.clip(np.round(w / scale), -127, 127).astype(np.int8)
print("int8     bytes/elem=1  q =", q, " scale =", np.float32(scale))
print("dequant  :", (q.astype(np.float32) * np.float32(scale)))
print("256x128 weights fp32/fp16/int8 bytes:", 256*128*4, 256*128*2, 256*128*1)
```

```text
float32  bytes/elem=4  values=[ 1.0000000e-01 -1.2345678e+00  3.1415927e+00  9.9999997e-06]
float16  bytes/elem=2  values=[ 9.998e-02 -1.234e+00  3.141e+00  1.001e-05]
int8     bytes/elem=1  q = [  4 -50 127   0]  scale = 0.02473695
dequant  : [ 0.0989478 -1.2368475  3.1415927  0.       ]
256x128 weights fp32/fp16/int8 bytes: 131072 65536 32768
```

출력에서 볼 것: float16은 유효숫자가 약 3~4자리로 줄고, int8은 `scale`(여기서는 L∞/127, 3절의 L∞ 노름) 하나로 모든 값을 정수에 매핑한다. 작은 값 `1e-5`는 int8에서 0이 되어 사라진다. 대신 메모리는 4 → 1 바이트.

| 타입 | 바이트 | 범위/정밀도 감 | 쓰이는 곳 |
|---|---|---|---|
| float32 | 4 | 유효숫자 약 7자리 | 학습, 레퍼런스(golden) |
| float16 / bfloat16 | 2 | 약 3자리 / 범위는 float32급(bf16) | GPU·NPU 추론, LLM 가중치 |
| int8 | 1 | 256단계 + scale | MCU·DSP·NPU 추론의 표준 |
| int4 | 0.5 | 16단계 | LLM weight-only 양자화 (C3) |

Don이 아는 **Q15 고정소수점**이 int8/int16 양자화의 친척이다. 차이는 scale이 2의 거듭제곱으로 고정이냐, 텐서(또는 채널)마다 임의 실수냐 정도다.

---

## 11. 임베디드 관점에서 다시 보기

이 노트의 모든 내용은 결국 세 숫자로 모인다: **MAC 수, 바이트 수, 레이아웃**.

### back-of-envelope: dense 256→128 한 층

```
파라미터   = 256·128 + 128               = 32,896 개
가중치     = int8이면 32,768 B ≈ 32 KB (+ bias int32 128·4 = 512 B)
MAC/추론   = 256·128                      = 32,768 MAC
시간(추정) = 32,768 MAC ÷ (코어가 사이클당 처리하는 MAC 수) ÷ 클럭
```

마지막 줄의 "사이클당 MAC 수"와 "메모리에서 가중치를 가져오는 속도" 중 느린 쪽이 실제 시간을 정한다. 배치 1 GEMV는 가중치 한 바이트당 MAC 1번이라 보통 **메모리 쪽이 병목**이다(D3). 구체 클럭·SIMD 폭은 칩마다 다르므로 데이터시트를 보고 계산한다.

### MCU에서 dense layer의 C 모양

CMSIS-NN(Arm의 Cortex-M용 NN 커널 라이브러리)의 `arm_fully_connected_s8` 같은 함수의 뼈대는 8절의 삼중 루프에서 배치 루프를 뺀 **GEMV**다: 출력 뉴런마다 int32 누산기를 bias로 초기화하고, 가중치 한 행(`[OUT, IN]` row-major라 연속)과 입력의 int8 내적을 누산한 뒤, 재스케일·saturation으로 int8 출력을 만든다(재스케일은 C1). 가중치 행이 연속이라 Cortex-M4/M7의 DSP 확장(16비트 MAC 2개 동시)이나 Cortex-M55/M85의 Helium(MVE) 벡터 명령으로 안쪽 루프를 여러 원소씩 처리할 수 있고, 실제 라이브러리는 여기에 언롤링과 출력 여러 개 동시 계산(입력 재사용)을 더한다.

### 체크포인트 표

| 질문 | 볼 것 | 이 노트 절 |
|---|---|---|
| 이 모델이 SRAM에 들어가나? | 가중치 원소 수 × dtype, activation shape × dtype | 1, 10 |
| 레이어 하나가 몇 ms? | M·N·K MAC ÷ MAC 처리율, 그리고 바이트 ÷ 대역폭 | 5, 8 |
| NPU 입력을 어떻게 준비하나? | 레이아웃(NHWC?), stride, 정렬(16 배수?) | 7 |
| 변환된 모델에 Transpose가 왜 있나? | NCHW ↔ NHWC 불일치 | 7 |
| 모델을 더 줄일 수 있나? | 특이값 분포, 저랭크 분해 | 9 |
| 정확도가 이상하다 | shape/broadcasting, 레이아웃 해석 오류 | 6, 7 |

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `A * B`를 행렬곱으로 착각 | shape 오류 또는 조용히 틀린 값 | numpy `*`는 원소별 곱 | 행렬곱은 `@` / `np.matmul` |
| `[N,1]`과 `[N]`을 더함 | 결과가 `[N,N]`, 메모리 폭증, loss 이상 | broadcasting이 두 축으로 확장 | `squeeze()`/`reshape` 후 `assert x.shape == …` |
| `reshape`를 transpose 대신 사용 | 에러 없이 데이터가 뒤섞임 | reshape는 메모리 순서를 유지 | 축 교환은 `transpose`/`permute` |
| NCHW 버퍼를 NHWC로 해석 | 추론은 도는데 정확도 바닥, 이미지가 줄무늬처럼 보임 | 레이아웃 계약 불일치 | 모델 입력 layout 명시, 변환 지점에서 테스트 벡터 비교 |
| transpose view를 C 커널/DMA에 그대로 넘김 | 값이 뒤섞이거나 느림 | `.T`는 non-contiguous view | `np.ascontiguousarray` / `.contiguous()` 후 넘김 |
| PyTorch Linear weight를 `[in,out]`으로 가정 | 변환 후 출력 전부 틀림 | `nn.Linear.weight`는 `[out,in]` | shape 출력해서 확인, 필요 시 `.T` |
| ijk 순서 naive matmul을 MCU에 그대로 | 예상보다 수 배 느림 | 열 방향 stride 접근, SIMD 불가 | 가중치 레이아웃/루프 순서를 연속 접근에 맞춤, 벤더 커널 사용 |
| MAC 수만으로 latency 추정 | 실측이 추정의 몇 배 | GEMV·elementwise는 memory-bound | 바이트 ÷ 대역폭도 함께 계산 (D3) |
| int8 내적을 int16 누산 | 큰 입력에서 값이 튐 | overflow (127·127·n) | int32 누산기 |
| 정규화 안 한 벡터로 유사도 검색 | 긴 벡터만 계속 1등 | 내적이 길이에 비례 | L2 정규화 후 내적 = 코사인 |

---

## 13. 면접에서 이렇게 말한다

**Q.** How many MACs and parameters are in a dense layer from 256 to 128?

**A.** 가중치 행렬이 128×256이라 파라미터는 256·128 = 32,768개, bias 128개를 더해 32,896개다. 샘플 하나당 MAC은 출력 128개 × 입력 256개 = 32,768. 배치 B면 MAC은 B배지만 가중치는 한 번만 읽는다. int8이면 가중치 약 32 KB라 MCU SRAM 예산에 바로 넣어 볼 수 있다.

> It has 256 × 128 = 32,768 weights plus 128 biases, so 32,896 parameters. One inference is 32,768 MACs. In int8 that's about 32 KB of weights, and at batch size one it's usually memory-bound, since each weight is used for only one MAC.

**Q.** What's the difference between NCHW and NHWC, and why does it matter?

**A.** 같은 4차원 텐서의 메모리 순서 차이다. NCHW는 채널별 평면이 연속이고, NHWC는 한 픽셀의 채널이 연속이다. PyTorch는 NCHW 기본, TFLite와 많은 모바일 NPU·CMSIS-NN은 NHWC를 쓴다. 채널 방향 내적(1×1 conv, dense)은 NHWC에서 SIMD로 연속 로드가 되고 DMA 버스트도 길어진다. 레이아웃이 안 맞으면 변환기가 transpose를 끼우고, 이게 레이어보다 오래 걸리기도 한다.

> They're two memory orderings of the same tensor. In NCHW each channel's plane is contiguous; in NHWC all channels of a pixel are contiguous. It matters because kernels and DMA engines want the reduction axis contiguous — for 1×1 convs and dense layers that's the channel axis, so NHWC gives long, aligned SIMD loads. A layout mismatch between the framework and the accelerator shows up as extra transpose ops, which I'd look for first in a profile.

**Q.** Why is matrix multiplication the core operation in neural networks?

**A.** dense layer는 그대로 GEMM이고, conv는 im2col로 GEMM이 되며, 1×1 conv는 그 자체가 GEMM, attention도 QKᵀ와 PV 두 개의 GEMM이다. 그래서 연산 시간의 대부분이 GEMM이고, 하드웨어는 MAC 배열과 데이터 재사용(tiling)으로 GEMM만 빨리 하면 대부분의 모델을 가속할 수 있다. 비용은 M·N·K MAC이라 성능 추정도 여기서 시작한다.

> Almost every layer lowers to a GEMM: fully-connected layers directly, convolutions via im2col or as 1×1 matmuls, and attention as QKᵀ and PV. So most of the runtime is GEMM, and an accelerator that's basically a big MAC array with good on-chip data reuse can speed up nearly any model. It also gives a simple cost model: M times N times K MACs.

**Q.** Why can two matmul loop orders with identical math differ by 10x in speed?

**A.** 메모리 접근 패턴 때문이다. ijk 순서는 안쪽 루프에서 B를 열 방향으로 읽어서 매번 한 행씩 점프하므로 캐시 라인의 일부만 쓰고 버린다. ikj는 B와 C를 행 방향으로 연속 접근해 캐시 라인을 다 쓰고 SIMD 벡터화도 된다. 이 Mac에서 256×256 float 기준 약 15 ms 대 1.2 ms를 측정했다. BLAS는 여기에 tiling과 레지스터 블로킹을 더해 수백 배 빠르다.

> It's the access pattern. In ijk order the inner loop walks B down a column, striding a full row each step, so you waste most of every cache line and the compiler can't vectorize. Switching to ikj makes the inner loop stream contiguously through rows of B and C. On my M2 laptop that alone took a 256×256 float matmul from about 15 ms to 1.2 ms; tuned BLAS adds tiling and register blocking on top.

**Q.** How would you use SVD to compress a layer, and when does it not help?

**A.** 가중치 W를 SVD해서 큰 특이값 k개만 남기면 `[m,n]` 하나를 `[m,k]`와 `[k,n]` 두 개로 바꿀 수 있다. 파라미터와 MAC이 m·n에서 k·(m+n)으로 준다. 특이값이 빨리 떨어지는 레이어에서만 정확도 손실이 작다. k가 크면 오히려 두 GEMM이 더 비싸고, 작은 GEMM 두 개는 NPU 효율이 떨어질 수 있으니 실측과 정확도 평가로 판단한다.

> Take the SVD of the weight matrix, keep the top-k singular values, and replace one m×n layer with m×k and k×n layers. That cuts parameters and MACs from m·n to k·(m+n). It works when the singular values decay quickly; if they don't, or if k is large, you lose accuracy or even add cost, so I'd check the spectrum per layer and measure both accuracy and latency on the target.

**Q.** What is broadcasting, and what's a bug it can cause?

**A.** shape가 다른 두 텐서를 원소별 연산할 때 오른쪽 축부터 맞추고, 길이 1인 축을 늘려서 계산하는 규칙이다. bias 더하기나 채널별 정규화에 편하다. 대표 버그는 `[N,1]`과 `[N]`을 더해 오류 없이 `[N,N]`이 되는 것이다. shape assert로 막는다.

> Broadcasting aligns shapes from the trailing axis and stretches size-1 dimensions, which is how a bias of shape [128] gets added to a [32, 128] output. The classic bug is adding an [N, 1] tensor to an [N] tensor — it silently produces [N, N]. I guard against it with explicit shape asserts at module boundaries.

---

## 14. 직접 해보기

1. 손계산: `A = [[1, 0, 2], [−1, 3, 1]]`, `B = [[3, 1], [2, 1], [1, 0]]`일 때 `A·B`와 MAC 수를 구하라.
정답: `[[5, 1], [4, 2]]`, MAC = 2·2·3 = 12.

2. 손계산: `u = (1, 2, 2)`, `v = (2, 0, 1)`의 내적, 각 L2 노름, 코사인 유사도를 구하라.
정답: 내적 4, |u| = 3, |v| = √5 ≈ 2.236, cos = 4/(3·√5) ≈ 0.596.

3. shape 추론: `X[8, 50, 6]`(배치, 시간, 축)에 `W[6, 16]`을 `X @ W` 하면 결과 shape와 MAC 수는? 이어서 `b[16]`을 더하면?
정답: `[8, 50, 16]`, MAC = 8·50·6·16 = 38,400. `b`는 broadcasting으로 마지막 축에 더해져 shape 유지.

4. 레이아웃: NHWC 텐서 `[1, 4, 4, 8]` int8에서 원소 `(n=0, h=2, w=3, c=5)`의 바이트 오프셋은?
정답: stride = `[128, 32, 8, 1]` → 2·32 + 3·8 + 5 = 93 바이트.

5. 코드 과제: 8절의 C 코드에 `mm_jki` 순서를 추가해 세 순서를 비교하라. 어느 것이 가장 느릴지 먼저 예측하라.
힌트: jki는 안쪽 루프가 i라서 A와 C를 **열 방향**으로 읽는다 → 두 행렬 모두 stride 접근이라 대개 가장 느리다. 예측 후 실측으로 확인.

6. 코드 과제: `torch.nn.Linear(256, 128)`을 만들고, SVD로 rank 32 근사한 두 개의 `Linear(256, 32)`, `Linear(32, 128)`로 교체해 같은 입력에서 출력 상대 오차와 파라미터 수를 비교하라.
힌트: `U, S, Vh = torch.linalg.svd(W)` → 첫 레이어 weight = `Vh[:32]`, 둘째 weight = `U[:, :32] * S[:32]`, bias는 둘째에만. 랜덤 초기화 가중치는 특이값이 천천히 떨어져서 오차가 크게 나온다 — 9절의 "언제 안 통하는가"를 직접 확인하는 과제다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| scalar | 스칼라 | 숫자 하나, shape `()` |
| vector | 벡터 | 1차원 배열 = 화살표 |
| matrix | 행렬 | 2차원 배열 = 선형 변환 |
| tensor | 텐서 | n차원 배열의 통칭 (ML 용법) |
| shape | 모양 | 각 축의 길이 튜플 |
| ndim / rank(텐서) | 축 개수 | `[32,50,6]`은 3 |
| dot product | 내적 | 같은 자리 곱의 합 = MAC n번 |
| MAC | multiply-accumulate | `acc += a·b` 한 번, 1 MAC = 2 FLOPs |
| cosine similarity | 코사인 유사도 | 정규화한 벡터의 내적, 방향 비교 |
| embedding | 임베딩 | 의미가 비슷하면 방향이 비슷한 벡터 표현 |
| norm (L1/L2/L∞) | 노름 | 벡터 크기를 재는 방법들 |
| dense / fully-connected / linear | 완전연결층 | `y = W·x + b` |
| GEMV | 행렬-벡터 곱 | 배치 1 dense, 주로 memory-bound |
| GEMM | 행렬-행렬 곱 | 신경망 연산의 대부분, M·N·K MAC |
| im2col | 이미지→열 변환 | conv를 GEMM으로 바꾸는 펼치기 |
| transpose | 전치 | 행·열 교환, view로 구현 가능 |
| reshape / view | 모양 바꾸기 | 메모리 순서 유지, shape만 변경 |
| broadcasting | 브로드캐스팅 | 길이 1 축을 늘려 원소별 연산 |
| elementwise | 원소별 연산 | 같은 위치끼리 계산 (ReLU, +) |
| row-major (C order) | 행 우선 | 마지막 축이 메모리에서 연속 |
| column-major (F order) | 열 우선 | 첫 축이 메모리에서 연속 |
| stride | 보폭 | 축 인덱스 1 증가 시 이동 바이트(또는 원소) |
| contiguous | 연속 | stride가 row-major 규칙과 일치 |
| NCHW / NHWC | 텐서 레이아웃 | 채널 평면 연속 / 픽셀의 채널 연속 |
| tiling / blocking | 타일링 | 캐시·SRAM에 맞는 블록으로 나눠 재사용 |
| BLAS | 선형대수 표준 라이브러리 | 최적화된 GEMM 등 제공 |
| identity / inverse | 항등 / 역행렬 | `I·x = x`, `A·A⁻¹ = I` |
| matrix rank | 행렬 rank | 독립인 행(열) 수 |
| eigenvalue / eigenvector | 고유값 / 고유벡터 | 방향 유지, 길이만 λ배 |
| SVD | 특이값 분해 | `W = UΣVᵀ`, 회전·늘이기·회전 |
| low-rank approximation | 저랭크 근사 | 큰 특이값 k개로 근사 → 파라미터 절감 |

---

## 16. 요약 & 체크리스트

신경망의 대부분은 행렬곱이다. 벡터는 feature 배열이자 화살표이고, 내적은 MAC의 연속이며 방향 유사도를 잰다. dense layer는 `y = W·x + b`, 배치를 묶으면 GEMM이고 비용은 M·N·K MAC이다. conv(im2col)와 attention(QKᵀ)도 GEMM으로 바뀐다. 텐서는 메모리에서 1차원이고 stride로 주소가 정해지며, row-major/NCHW/NHWC 같은 레이아웃 계약이 SIMD·DMA·NPU 효율과 정확도(해석 오류)를 동시에 좌우한다. 같은 수식이라도 루프 순서(접근 패턴) 하나로 이 Mac에서 약 12배, BLAS와는 수백 배 차이가 났다. SVD는 행렬의 "중요한 방향"을 보여 주고, 저랭크 근사는 모델 압축의 기본 직관이다.

- [ ] `[M,K]×[K,N]`의 결과 shape와 MAC 수를 즉시 말할 수 있다
- [ ] 2×3 × 3×2 행렬곱을 손으로 계산할 수 있다
- [ ] dense layer의 파라미터 수와 MAC 수를 계산할 수 있다 (256→128: 32,896 / 32,768)
- [ ] 내적·L2 노름·코사인 유사도를 손으로 계산하고, 임베딩 검색이 왜 되는지 설명할 수 있다
- [ ] row view와 column view로 `W·x`를 설명할 수 있다
- [ ] broadcasting 규칙을 적용해 결과 shape를 예측하고, `[N,1]+[N]` 함정을 설명할 수 있다
- [ ] 다차원 인덱스의 바이트 오프셋을 stride로 계산할 수 있다 (NCHW, NHWC 모두)
- [ ] NCHW와 NHWC의 차이와 NPU/DMA에서 중요한 이유를 30초 안에 말할 수 있다
- [ ] ijk vs ikj 속도 차이를 캐시 라인과 벡터화로 설명할 수 있다
- [ ] SVD 저랭크 근사의 파라미터 수 `k·(m+n)`과 언제 안 통하는지 설명할 수 있다

---

## 참고 자료

- Gilbert Strang, "Introduction to Linear Algebra" (Wellesley-Cambridge Press) 및 MIT OCW 18.06 강의 — [ocw.mit.edu](https://ocw.mit.edu/courses/18-06-linear-algebra-spring-2010/)
- 3Blue1Brown, "Essence of Linear Algebra" 영상 시리즈 — [3blue1brown.com](https://www.3blue1brown.com/topics/linear-algebra)
- Goodfellow, Bengio, Courville, "Deep Learning" 2장 Linear Algebra — [deeplearningbook.org](https://www.deeplearningbook.org/)
- Dive into Deep Learning, 2.3절 Linear Algebra — [d2l.ai](https://d2l.ai/)
- NumPy 문서: Broadcasting — [numpy.org/doc/stable/user/basics.broadcasting.html](https://numpy.org/doc/stable/user/basics.broadcasting.html)
- NumPy 문서: `ndarray.strides` — [numpy.org/doc/stable/reference/generated/numpy.ndarray.strides.html](https://numpy.org/doc/stable/reference/generated/numpy.ndarray.strides.html)
- PyTorch 문서: `torch.nn.Linear` — [pytorch.org/docs/stable/generated/torch.nn.Linear.html](https://pytorch.org/docs/stable/generated/torch.nn.Linear.html)
- CMSIS-NN (Arm) — [github.com/ARM-software/CMSIS-NN](https://github.com/ARM-software/CMSIS-NN)
- MIT 6.5940 TinyML and Efficient Deep Learning Computing (Song Han) — [efficientml.ai](https://efficientml.ai/)
- Ulrich Drepper, "What Every Programmer Should Know About Memory" (2007) — 캐시·접근 패턴 고전
