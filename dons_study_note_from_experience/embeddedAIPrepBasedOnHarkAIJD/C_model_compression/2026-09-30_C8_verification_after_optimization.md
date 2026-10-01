# C8. 최적화 후 검증 — golden reference, 레이어별 비교, 정확도 회귀 테스트

> **이 노트를 다 읽으면**: float 모델과 int8/NPU 결과를 max abs error·cosine·SQNR·top-1 agreement·KL로 비교하고 각 지표가 무엇을 숨기는지 설명할 수 있다 · bit-exact로 검증할 곳과 tolerance로 검증할 곳을 근거를 들어 가를 수 있다 · forward hook과 ONNX 중간 출력으로 레이어별 diff를 떠서 "처음 무너지는 레이어"를 찾는 root-cause 절차를 돌릴 수 있다 · golden vector·C harness·HIL 루프·bootstrap 신뢰구간이 들어간 회귀 테스트를 직접 만들 수 있다
> **JD 연결**: "Integrate ML inference into embedded firmware in C, C++, or Rust", "optimize models for … power, latency, memory" — 최적화한 모델이 **여전히 같은 답을 내는지** 보증하는 일 · study_prep_list C8 — golden reference, bit-exact vs tolerance 비교, layer별 diff, SQNR/cosine 유사도, 정확도 회귀 테스트 · J6 — golden vector, bit-exact 테스트, HIL, 정확도 회귀 CI
> **Don 기준 난이도**: golden model 대비 검증, 테스트 벡터, bring-up 중 bisect로 root cause 찾기, factory test 합격 기준은 이미 강함 / "정답이 하나가 아닌" 비교 지표, float 비결합성, 레이어 이름 정렬, 통계적 합격 판정은 새로 배움
> **선행 노트**: A2(분포·신뢰구간), A4(평가 지표·사용자별 breakdown), A5(PyTorch·C 헤더 export), B1·B2(C 구현을 torch와 비교하는 방법), C1(quantization 수식·requantization), C2(PTQ/QAT·레이어별 sensitivity)

---

## 0. 큰 그림 — 왜 ML 검증은 펌웨어 검증보다 까다로운가

Don이 해 온 검증은 대부분 **정답이 하나**였다. NVMe 명령의 completion status, 레지스터 값, CRC, 데이터 패턴 — 기대값과 비트 단위로 같으면 PASS, 아니면 FAIL이다. silicon validation도 비슷하다. RTL을 기준(golden model)으로 두고, 실리콘이나 FPGA 결과가 cycle-accurate 모델과 일치하는지 본다.

ML 모델을 최적화(양자화 C1·C2, 그래프 최적화 C6, 벤더 컴파일러 F 모듈)하면 사정이 달라진다.

- **정답 출력이 하나가 아니다.** int8 모델의 logit이 float 모델과 0.17 다르다면 버그인가? 대개 아니다. 양자화는 원래 값을 조금씩 바꾸는 것이 목적이다.
- **"기준" 자체도 여러 개다.** PyTorch float, ONNX Runtime float, 양자화 시뮬레이션(fake quant), 레퍼런스 int8 커널, NPU 실행 — 각각 조금씩 다른 숫자를 낸다.
- **진짜 판정 기준은 숫자가 아니라 과제 성능이다.** 사용자가 느끼는 것은 logit이 아니라 "손목을 들었을 때 화면이 켜지는가", "wake word 오작동이 하루에 몇 번인가"이다.

그래서 ML 검증은 "비트 비교" 한 단계가 아니라 **여러 층의 비교를 각 층에 맞는 허용 기준으로** 쌓는 일이다. 좋은 소식은 뼈대가 Don이 아는 silicon validation과 똑같다는 점이다.

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="c8m0" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="20" y="24" font-size="14">Silicon validation (Don이 아는 것)</text>
<rect x="20" y="36" width="150" height="46" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="95" y="56" font-size="13" text-anchor="middle">RTL / C model</text><text x="95" y="73" font-size="12" text-anchor="middle">golden reference</text>
<rect x="265" y="36" width="150" height="46" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="340" y="56" font-size="13" text-anchor="middle">FPGA · silicon</text><text x="340" y="73" font-size="12" text-anchor="middle">device under test</text>
<rect x="510" y="36" width="150" height="46" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="585" y="56" font-size="13" text-anchor="middle">비교</text><text x="585" y="73" font-size="12" text-anchor="middle">bit-exact, 끝</text>
<line x1="170" y1="59" x2="263" y2="59" stroke="currentColor" stroke-width="1.5" marker-end="url(#c8m0)"/><text x="216" y="52" font-size="12" text-anchor="middle">test vector</text>
<line x1="415" y1="59" x2="508" y2="59" stroke="currentColor" stroke-width="1.5" marker-end="url(#c8m0)"/>
<text x="20" y="124" font-size="14">Edge ML 검증 (이 노트)</text>
<rect x="20" y="136" width="150" height="46" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="95" y="156" font-size="13" text-anchor="middle">PyTorch float</text><text x="95" y="173" font-size="12" text-anchor="middle">golden reference</text>
<rect x="265" y="136" width="150" height="46" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="340" y="156" font-size="13" text-anchor="middle">int8 · 컴파일 · NPU</text><text x="340" y="173" font-size="12" text-anchor="middle">최적화된 모델</text>
<rect x="510" y="136" width="150" height="46" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="585" y="156" font-size="13" text-anchor="middle">다층 비교</text><text x="585" y="173" font-size="12" text-anchor="middle">지표 + tolerance</text>
<line x1="170" y1="159" x2="263" y2="159" stroke="currentColor" stroke-width="1.5" marker-end="url(#c8m0)"/><text x="216" y="152" font-size="12" text-anchor="middle">golden vector</text>
<line x1="415" y1="159" x2="508" y2="159" stroke="currentColor" stroke-width="1.5" marker-end="url(#c8m0)"/>
<rect x="265" y="206" width="150" height="46" rx="6" fill="none" stroke="#888" stroke-width="2" stroke-dasharray="5 3"/><text x="340" y="226" font-size="13" text-anchor="middle">중간 기준들</text><text x="340" y="243" font-size="12" text-anchor="middle">ONNX RT · fake quant · ref C</text>
<line x1="95" y1="182" x2="265" y2="229" stroke="#888" stroke-width="1.2" marker-end="url(#c8m0)"/><line x1="415" y1="229" x2="585" y2="182" stroke="#888" stroke-width="1.2" marker-end="url(#c8m0)"/>
<text x="20" y="274" font-size="12">초록: 기준 · 주황: 검증 대상 · 파랑: 판정. ML에서는 판정이 "같다/다르다"가 아니라 "얼마나, 어디서, 과제에 영향이 있나"다.</text>
</svg>
```

그림 1 — 위: RTL golden model 대비 silicon 검증. 아래: float 모델 대비 최적화 모델 검증. 구조는 같고, 판정 단계가 bit-exact 하나에서 "지표 + 허용 기준" 여러 층으로 바뀐다. 중간 기준(점선)을 두면 차이가 어느 단계에서 생겼는지 bisect할 수 있다.

### 0.1 불일치의 출처 — 증상으로 원인을 추정한다

숫자가 다를 때 원인은 크게 두 부류다. **예상된 차이**(정밀도가 줄어서 생기는 것 — 허용 기준 안에 있으면 OK)와 **버그**(구현이 틀린 것). 아래 표는 Don이 bring-up에서 쓰던 "증상 → 의심 부위" 표의 ML 버전이다. 이 노트 전체가 이 표를 채우는 과정이다.

| 불일치 출처 | 예상된 차이인가 | 전형적 증상 (signature) | 어느 층에서 보이나 |
|---|---|---|---|
| 양자화 (C1·C2) | 예 | 모든 레이어 SQNR 30~40 dB, 출력이 LSB 계단 위에만 놓임 | 레이어별 diff 전체에 고르게 |
| float 합산 순서 (SIMD, 다른 BLAS) | 예 | 1e-6 수준 차이, SQNR 100 dB 이상 | 커널 단위 |
| FMA contraction | 예 | 1 ulp 수준 차이, 컴파일 옵션 따라 달라짐 | 커널 단위 |
| op fusion (conv+BN+ReLU, C6) | 대개 예 | 중간 텐서가 없어지거나 이름이 바뀜, 값은 거의 같음 | 레이어 이름 정렬 단계 |
| rounding mode 차이 (half-even vs half-away, truncation) | 규격 문제 | ±1 LSB 불일치가 규격에 따라 0.01%~수십 % 원소에서 발생 | 첫 requant 레이어부터 |
| 전처리 불일치 (정규화, 채널 순서, 단위, 윈도 길이) | 아니오 (버그) | 입력부터 다름, 모든 레이어 망가짐, 과제 정확도 급락 | input |
| zero-point / scale 오류 | 아니오 | 첫 소비 레이어에서 SQNR이 음수로 추락 | 해당 레이어 |
| layout 오류 (NCHW ↔ NHWC) | 아니오 | 특정 레이어부터 SQNR 0 dB 근처, cosine 낮음 | layout이 바뀌는 지점 |
| 채널 하나의 scale 오류 | 아니오 | 전체 SQNR 소폭 하락, 정확도는 거의 그대로, 채널별 SQNR에서만 튐 | 채널 단위 diff |
| 데이터 분포 차이 (실기기 센서 vs 실험실) | 아니오 (시스템 문제) | 기준 모델 자체의 정확도가 기기 데이터에서 떨어짐 | 과제 지표, 입력 통계 |

이 노트의 순서: 검증의 층(1절) → 비교 지표(2절) → bit-exact vs tolerance(3절) → 레이어별 diff와 root cause(4절) → golden vector와 C harness(5절) → 정확도 회귀 테스트(6절) → 온디바이스 검증·HIL·CI(7절) → 디버깅 체크리스트(8절).

> 실행 환경: 모든 Python 예제는 `.venv/bin/python` (Python 3.9, torch 2.8.0, numpy 2.0.2, onnx 1.19.1, onnxruntime 1.19.2)로 실제 실행한 출력이다. C 예제는 `cc -std=c11 -Wall -Wextra -O2 ... -lm` (Apple clang 21, arm64)으로 경고 0개로 컴파일했다. 예제들은 한 디렉터리에서 순서대로 실행하며, 앞에서 저장한 모듈(`c8_model.py`, `c8_metrics.py` 등)을 뒤에서 import 한다.

### 0.2 이 노트의 공용 모델 — 합성 IMU 4-class 1D CNN

모든 예제는 같은 작은 모델을 쓴다. 예를 들어 Hark 같은 웨어러블이라면(추정) 손목 IMU로 "가만히 / 걷기 / 손목 들기 / 흔들기"를 구분하는 모델을 생각할 수 있다. 사용자마다 센서 gain과 offset이 조금씩 다르게 합성했다(A4의 사용자별 분할과 같은 발상). 학습은 사용자 0~19, 테스트는 처음 보는 사용자 20~25다. 양자화는 `torch.ao.quantization`의 eager-mode PTQ(qnnpack 엔진)로 한다 — 절차 자체는 C2에서 다룬다.

```python
# c8_model.py — 합성 IMU 4-class 데이터 + 작은 1D CNN (C8 예제 공용)
import copy, os, numpy as np, torch, torch.nn as nn
from torch.ao import quantization as tq
CLASSES = ["idle", "walk", "raise", "shake"]

def make_data(n_per_user, users, seed):          # 반환: X [N,6,50] float32, y 클래스, u 사용자 ID
    rng, t = np.random.default_rng(seed), np.arange(50) / 50.0
    X, y, u = [], [], []
    for user in users:
        g = 1.0 + 0.10 * np.random.default_rng(100 + user).standard_normal()  # 사용자별 gain
        off = 0.15 * np.random.default_rng(200 + user).standard_normal(6)      # 사용자별 offset
        for _ in range(n_per_user):
            c, x, f = rng.integers(4), 0.9 * rng.standard_normal((6, 50)), rng.uniform(1.5, 2.5)
            if c == 1: x[:3] += np.sin(2 * np.pi * f * t + rng.uniform(0, 6))          # walk
            if c == 2: x[3:] += 1.2 * np.exp(-((t - rng.uniform(.3, .7)) / .1) ** 2)  # raise
            if c == 3: x[:3] += 0.8 * np.sin(2 * np.pi * 3 * f * t)                    # shake
            X.append(g * x + off[:, None]); y.append(c); u.append(user)
    return np.array(X, np.float32), np.array(y), np.array(u)

class Net(nn.Module):
    def __init__(self):
        super().__init__()
        self.quant, self.dequant = tq.QuantStub(), tq.DeQuantStub()
        self.conv1 = nn.Conv1d(6, 16, 5, padding=2);            self.relu1 = nn.ReLU()
        self.conv2 = nn.Conv1d(16, 32, 5, stride=2, padding=2); self.relu2 = nn.ReLU()
        self.conv3 = nn.Conv1d(32, 32, 3, padding=1);           self.relu3 = nn.ReLU()
        self.pool, self.fc = nn.AdaptiveAvgPool1d(1), nn.Linear(32, 4)
    def forward(self, x):
        x = self.quant(x)
        x = self.relu1(self.conv1(x)); x = self.relu2(self.conv2(x)); x = self.relu3(self.conv3(x))
        return self.dequant(self.fc(self.pool(x).flatten(1)))

def train(seed=0, epochs=8):                      # 사용자 0~19, 2400개로 학습
    torch.manual_seed(seed)
    X, y, _ = make_data(120, range(20), seed=1)
    m = Net(); opt = torch.optim.Adam(m.parameters(), 3e-3)
    Xt, yt = torch.from_numpy(X), torch.from_numpy(y)
    for ep in range(epochs):
        perm = torch.randperm(len(Xt))
        for i in range(0, len(Xt), 64):
            idx = perm[i:i + 64]
            loss = nn.functional.cross_entropy(m(Xt[idx]), yt[idx])
            opt.zero_grad(); loss.backward(); opt.step()
    return m.eval()

def quantize_ptq(m_float, calib_X):               # eager-mode PTQ (절차는 C2)
    torch.backends.quantized.engine = "qnnpack"
    m = copy.deepcopy(m_float).eval()
    m.qconfig = tq.get_default_qconfig("qnnpack")
    tq.fuse_modules(m, [["conv1", "relu1"], ["conv2", "relu2"], ["conv3", "relu3"]], inplace=True)
    tq.prepare(m, inplace=True)
    with torch.no_grad(): m(torch.from_numpy(calib_X))     # calibration: observer가 min/max 수집
    return tq.convert(m)

def load():                                       # 한 번 학습해 캐시, 모든 예제가 같은 모델 사용
    m = Net()
    if os.path.exists("c8_float.pt"):
        m.load_state_dict(torch.load("c8_float.pt")); return m.eval()
    m = train(); torch.save(m.state_dict(), "c8_float.pt"); return m
```

파라미터는 6,324개, float 정확도는 테스트 3000개에서 96.6% 정도다(2절 출력 참고). 이 모델이 "PyTorch golden"이다.

---

## 1. 검증의 층 — 피라미드

Don이 SSD 펌웨어에서 쓰던 검증 층을 떠올려 보자. 함수 단위 unit test → 모듈(FTL, NVMe 핸들러) 통합 테스트 → 시스템 레벨 성능·신뢰성 테스트 → 필드 텔레메트리. 아래로 갈수록 싸고 빠르고 원인을 정확히 짚지만, 위로 갈수록 "사용자에게 정말 괜찮은가"에 가깝다. ML도 똑같다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<polygon points="340,20 395,70 285,70" fill="none" stroke="#d0564a" stroke-width="2"/>
<polygon points="285,70 395,70 450,120 230,120" fill="none" stroke="#e08a3c" stroke-width="2"/>
<polygon points="230,120 450,120 505,170 175,170" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<polygon points="175,170 505,170 560,220 120,220" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<polygon points="120,220 560,220 615,270 65,270" fill="none" stroke="#888" stroke-width="2"/>
<text x="340" y="60" font-size="12" text-anchor="middle">field</text>
<text x="340" y="100" font-size="13" text-anchor="middle">task metric</text>
<text x="340" y="150" font-size="13" text-anchor="middle">model output</text>
<text x="340" y="200" font-size="13" text-anchor="middle">layer (중간 activation)</text>
<text x="340" y="250" font-size="13" text-anchor="middle">unit: op · kernel</text>
<text x="470" y="40" font-size="12">FA/hour, 사용자 불만, 텔레메트리</text>
<text x="470" y="98" font-size="12">accuracy, 클래스·사용자별 drop</text>
<text x="515" y="150" font-size="12">cosine, SQNR, top-1 agree</text>
<text x="570" y="200" font-size="12">레이어별 SQNR</text>
<text x="20" y="200" font-size="12">bit-exact 또는</text><text x="20" y="216" font-size="12">tolerance</text>
<text x="20" y="250" font-size="12">bit-exact (정수)</text><text x="20" y="266" font-size="12">ulp (float)</text>
<line x1="650" y1="265" x2="650" y2="30" stroke="currentColor" stroke-width="1.5"/><path d="M645,38 L650,26 L655,38" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="642" y="290" font-size="12" text-anchor="end">위로 갈수록: 느리고 비싸지만 사용자 체감에 가깝다 · 아래로 갈수록: 빠르고 원인이 정확하다</text>
</svg>
```

그림 2 — ML 검증 피라미드. 아래 층은 커밋마다 돌리고, 위 층은 릴리스마다 돌린다. 문제가 위 층에서 발견되면 아래 층으로 내려가며 원인을 좁힌다(4절).

| 층 | 무엇을 비교 | 기준(reference) | 판정 방식 | 소요 시간 | Don 경험 대응 |
|---|---|---|---|---|---|
| unit (op·kernel) | conv 하나, requant 하나 | numpy 정수 시뮬레이션, 레퍼런스 C | 정수는 bit-exact, float은 ulp·상대오차 | ms | 함수 unit test |
| layer | 모든 중간 activation | float 모델의 같은 레이어 | SQNR·cosine, 기준 대비 변화 | 초 | 블록별 register dump 비교 |
| model output | logits, 확률 | float 모델 출력 | cosine, SQNR, top-1 agree, KL | 초 | end-to-end 데이터 비교 |
| task metric | 정확도, F1, FA/hour | float 모델의 같은 지표 | 통계적 허용 기준 (6절) | 분 | 시스템 성능 sign-off |
| field | 실사용 트리거율, 불만 | 이전 펌웨어 버전 | 추세, A/B | 일~주 | 필드 텔레메트리, RMA 분석 |

핵심 원칙 두 가지. 첫째, **아래 층이 통과해도 위 층은 꼭 따로 본다** — 모든 레이어 SQNR이 35 dB여도 특정 클래스(예: "손목 들기")만 무너질 수 있다. 둘째, **위 층에서 실패하면 아래 층으로 내려가 원인을 찾는다** — 정확도 숫자 하나로는 어디가 틀렸는지 알 수 없다.

---

## 2. 비교 지표 — 공식, 손계산, 언제 무엇을 쓰나

### 2.1 정의

reference(기준, 보통 float 모델) 출력을 `r`, test(검증 대상, 예: int8) 출력을 `t`, 오차를 `e = t − r`이라 하자. 원소 수는 n.

```
max abs error   = max_i |e_i|
mean abs error  = (1/n) ∑ |e_i|
max rel error   = max_i |e_i| / (|r_i| + ε)
cosine          = (r · t) / (‖r‖ · ‖t‖)
SQNR (dB)       = 10 · log10( ∑ r_i² / ∑ e_i² )
top-1 agreement = (1/N) ∑_samples [ argmax(r) == argmax(t) ]
KL(P‖Q)         = ∑_k P_k · log(P_k / Q_k),   P = softmax(r), Q = softmax(t)
```

말로 하면 이렇다.

- **max abs**: "가장 나쁜 원소 하나가 얼마나 틀렸나". 단위가 있다(logit 단위). 스케일에 따라 좋고 나쁨이 바뀐다.
- **mean abs**: 평균적으로 얼마나 틀렸나. outlier 하나에는 둔감하다.
- **max rel**: 상대 오차. `r`이 0 근처면 폭발하므로 ReLU 뒤처럼 0이 많은 텐서에서는 거의 쓸모없다.
- **cosine**: 두 벡터의 방향이 얼마나 같은가. 크기 차이(전체 스케일 오류)는 못 잡는다. 1에 아주 가까운 값끼리 비교하게 되므로 "0.9999 vs 0.99999"처럼 읽기가 어렵다.
- **SQNR** (signal-to-quantization-noise ratio): 신호 전력 대 오차 전력 비를 dB로. **스케일에 무관**하고 dB라 읽기 쉽다. RF의 SNR과 같은 개념이라 Don에게 가장 익숙할 지표다. 10 dB 오를 때마다 오차 전력이 1/10이 된다.
- **top-1 agreement**: 두 모델이 같은 클래스를 고른 비율. 정답 레이블이 필요 없다는 게 장점이다 — 기기에서 녹음한, 레이블 없는 데이터에도 쓸 수 있다.
- **KL**: 두 확률 분포가 얼마나 다른가. confidence(확신도)의 변화까지 본다. threshold로 판정하는 wake word처럼 **확률 값 자체가 중요할 때** 유용하다.

### 2.2 손계산 — 원소 4개

`r = [2.0, −1.0, 0.5, 4.0]`, `t = [2.1, −1.0, 0.4, 3.8]`이면 `e = [0.1, 0, −0.1, −0.2]`.

```
max abs  = 0.2                        mean abs = (0.1+0+0.1+0.2)/4 = 0.1
max rel  = max(0.1/2, 0/1, 0.1/0.5, 0.2/4) = 0.2   ← 가장 작은 값(0.5)에서 나온다
∑ r²     = 4 + 1 + 0.25 + 16 = 21.25
∑ e²     = 0.01 + 0 + 0.01 + 0.04 = 0.06
SQNR     = 10 · log10(21.25 / 0.06) = 10 · log10(354.2) ≈ 25.49 dB
r · t    = 4.2 + 1.0 + 0.2 + 15.2 = 20.6
‖r‖ ‖t‖  = √21.25 · √20.01 = 4.6098 · 4.4733 ≈ 20.621
cosine   = 20.6 / 20.621 ≈ 0.99899
```

이 지표들을 한 모듈로 만들어 두고 계속 쓴다.

```python
# c8_metrics.py — reference(r)와 test(t)를 비교하는 지표 모음
import numpy as np

def max_abs(r, t):  return float(np.max(np.abs(t - r)))
def mean_abs(r, t): return float(np.mean(np.abs(t - r)))
def max_rel(r, t, eps=1e-6):
    return float(np.max(np.abs(t - r) / (np.abs(r) + eps)))
def cosine(r, t):
    r, t = r.ravel().astype(np.float64), t.ravel().astype(np.float64)
    return float(r @ t / (np.linalg.norm(r) * np.linalg.norm(t) + 1e-30))
def sqnr_db(r, t):                      # 신호 전력 / 오차 전력
    r, t = r.astype(np.float64), t.astype(np.float64)
    return float(10 * np.log10(np.sum(r ** 2) / (np.sum((t - r) ** 2) + 1e-30)))
def softmax(z):
    e = np.exp(z - z.max(-1, keepdims=True)); return e / e.sum(-1, keepdims=True)
def top1_agree(r, t): return float(np.mean(r.argmax(-1) == t.argmax(-1)))
def kl(r_logits, t_logits):             # 행(샘플)별 KL(P_ref || P_test)의 평균
    p, q = softmax(r_logits), softmax(t_logits)
    return float(np.mean(np.sum(p * (np.log(p + 1e-12) - np.log(q + 1e-12)), -1)))

def report(r, t, name=""):
    return (f"{name:10s} maxabs={max_abs(r,t):.4f} meanabs={mean_abs(r,t):.4f} "
            f"cos={cosine(r,t):.6f} SQNR={sqnr_db(r,t):6.2f} dB")
```

손계산과 같은 숫자를 넣어 확인한다. 끝줄은 입력·출력을 10배로 키웠을 때 지표가 어떻게 변하는지 본다.

```python
# 손계산 예제와 같은 4개 값으로 지표를 확인한다
import numpy as np
from c8_metrics import *
r = np.array([2.0, -1.0, 0.5, 4.0])     # reference (float 모델 출력)
t = np.array([2.1, -1.0, 0.4, 3.8])     # test (int8 모델 출력)
print("error      =", t - r)
print("max_abs    =", round(max_abs(r, t), 4), " mean_abs =", round(mean_abs(r, t), 4))
print("max_rel    =", round(max_rel(r, t), 4))
print("cosine     =", round(cosine(r, t), 6))
print("SQNR (dB)  =", round(sqnr_db(r, t), 2))
print("top1 agree =", top1_agree(r, t), " KL =", round(kl(r, t), 6))
# 스케일을 10배 키우면? 절대 오차는 10배, SQNR·cosine은 그대로
print("x10: max_abs =", round(max_abs(10*r, 10*t), 4), " SQNR =", round(sqnr_db(10*r, 10*t), 2))
```

```text
error      = [ 0.1  0.  -0.1 -0.2]
max_abs    = 0.2  mean_abs = 0.1
max_rel    = 0.2
cosine     = 0.998997
SQNR (dB)  = 25.49
top1 agree = 1.0  KL = 0.005056
x10: max_abs = 2.0  SQNR = 25.49
```

출력에서 볼 것: 손계산과 모두 일치한다. 그리고 전체를 10배 키우면 max abs는 10배가 되지만 SQNR은 그대로다. **서로 다른 레이어(스케일이 다름)를 한 표에서 비교할 때 SQNR·cosine을 쓰는 이유**가 이것이다.

### 2.3 SQNR과 cosine의 관계 — 둘 중 하나만 봐도 되나

오차가 신호와 상관이 없고 작다면 다음 근사가 성립한다.

```
cosine ≈ 1 − 1 / (2 · SNR),    SNR = 10^(SQNR/10)
예) SQNR 30 dB → SNR 1000 → cosine ≈ 0.9995
    SQNR 40 dB → SNR 10000 → cosine ≈ 0.99995
```

말로 하면, 둘은 거의 같은 정보를 다른 눈금으로 보여 준다. cosine은 1 근처에 몰려서 차이가 안 보이고, SQNR은 dB라서 "30 → 40 dB = 오차 전력 1/10"처럼 읽힌다. 그래서 레이어별 표는 SQNR로 그리는 경우가 많다. 다만 cosine은 **전체 스케일이 틀린 버그**(예: 모든 출력이 1.5배)를 못 잡으므로(방향은 같으니 cosine = 1) SQNR이나 max abs와 같이 본다.

### 2.4 실제 모델에서 — float vs int8

이제 0.2절의 모델을 PTQ로 int8로 만들고 테스트 3000개의 logits를 비교한다. 대조군으로, 진폭이 1/4인 잘못된 calibration 데이터로 양자화한 "나쁜 int8"도 만든다(실험실 데이터와 기기 데이터 스케일이 다른 상황 — 7.3절).

```python
# 실제 모델: float vs int8(PTQ) logits를 여러 지표로 비교한다
import torch, numpy as np
from c8_model import load, make_data, quantize_ptq
from c8_metrics import *
torch.manual_seed(0)
m = load()
X, y, u = make_data(500, range(20, 26), seed=2)          # 테스트 3000개
Xc, _, _ = make_data(20, range(20), seed=3)              # calibration 400개
q_good = quantize_ptq(m, Xc)
q_bad = quantize_ptq(m, 0.25 * Xc)                       # 진폭이 1/4인 잘못된 calib 데이터
with torch.no_grad():
    lf = m(torch.from_numpy(X)).numpy()
    lg = q_good(torch.from_numpy(X)).numpy()
    lb = q_bad(torch.from_numpy(X)).numpy()
for name, lq in [("int8 good", lg), ("int8 bad", lb)]:
    print(report(lf, lq, name))
    print(f"{'':10s} max_rel={max_rel(lf, lq):.1f} top1_agree={top1_agree(lf, lq):.4f} "
          f"KL={kl(lf, lq):.5f} acc: float={np.mean(lf.argmax(1)==y):.4f} int8={np.mean(lq.argmax(1)==y):.4f}")
np.save("logits_float.npy", lf); np.save("logits_int8.npy", lg)
```

```text
int8 good  maxabs=0.3359 meanabs=0.0650 cos=0.999915 SQNR= 37.67 dB
           max_rel=14.2 top1_agree=0.9980 KL=0.00014 acc: float=0.9663 int8=0.9650
int8 bad   maxabs=16.8873 meanabs=2.1439 cos=0.920138 SQNR=  6.43 dB
           max_rel=467.4 top1_agree=0.8663 KL=0.24715 acc: float=0.9663 int8=0.8350
```

출력에서 볼 것은 네 가지다.

1. 정상 int8은 SQNR 37.7 dB, cosine 0.99991, top-1 agreement 99.8%, 정확도 drop 0.13%p. 2.3절 근사식으로 계산하면 37.67 dB → SNR ≈ 5848 → cosine ≈ 1 − 1/11696 ≈ 0.999915로 실제값과 같다.
2. max abs 0.336은 커 보이지만 출력 레이어의 양자화 한 칸(LSB)이 약 0.174라서 **2 LSB 정도**다. 절대 오차는 항상 LSB 단위로 환산해서 읽는다.
3. max rel이 14.2로 터무니없이 크다. logit이 0 근처인 원소에서 나누었기 때문이다 — max rel을 합격 기준으로 쓰면 안 되는 이유다.
4. 나쁜 int8은 SQNR 6.4 dB, top-1 agreement 86.6%, 정확도 83.5%. 모든 지표가 같은 방향으로 망가졌다.

그림으로 보면 정상 int8의 출력은 y = x 선 위에 있되 **계단(LSB 간격) 위에만** 놓인다.

```svg
<svg viewBox="0 0 600 340" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="290" x2="390" y2="290" stroke="currentColor"/> <line x1="70" y1="290" x2="70" y2="30" stroke="currentColor"/> <line x1="80.0" y1="290" x2="80.0" y2="295" stroke="currentColor"/><text x="80.0" y="309" font-size="12" text-anchor="middle">-1.5</text> <line x1="65" y1="281.9" x2="70" y2="281.9" stroke="currentColor"/><text x="61" y="285.9" font-size="12" text-anchor="end">-1.5</text> <line x1="130.0" y1="290" x2="130.0" y2="295" stroke="currentColor"/><text x="130.0" y="309" font-size="12" text-anchor="middle">-1</text> <line x1="65" y1="241.2" x2="70" y2="241.2" stroke="currentColor"/><text x="61" y="245.2" font-size="12" text-anchor="end">-1</text> <line x1="180.0" y1="290" x2="180.0" y2="295" stroke="currentColor"/><text x="180.0" y="309" font-size="12" text-anchor="middle">-0.5</text> <line x1="65" y1="200.6" x2="70" y2="200.6" stroke="currentColor"/><text x="61" y="204.6" font-size="12" text-anchor="end">-0.5</text> <line x1="230.0" y1="290" x2="230.0" y2="295" stroke="currentColor"/><text x="230.0" y="309" font-size="12" text-anchor="middle">0</text> <line x1="65" y1="160.0" x2="70" y2="160.0" stroke="currentColor"/><text x="61" y="164.0" font-size="12" text-anchor="end">0</text> <line x1="280.0" y1="290" x2="280.0" y2="295" stroke="currentColor"/><text x="280.0" y="309" font-size="12" text-anchor="middle">0.5</text> <line x1="65" y1="119.4" x2="70" y2="119.4" stroke="currentColor"/><text x="61" y="123.4" font-size="12" text-anchor="end">0.5</text> <line x1="330.0" y1="290" x2="330.0" y2="295" stroke="currentColor"/><text x="330.0" y="309" font-size="12" text-anchor="middle">1</text> <line x1="65" y1="78.8" x2="70" y2="78.8" stroke="currentColor"/><text x="61" y="82.8" font-size="12" text-anchor="end">1</text>
<line x1="380.0" y1="290" x2="380.0" y2="295" stroke="currentColor"/><text x="380.0" y="309" font-size="12" text-anchor="middle">1.5</text> <line x1="65" y1="38.1" x2="70" y2="38.1" stroke="currentColor"/><text x="61" y="42.1" font-size="12" text-anchor="end">1.5</text> <line x1="70.0" y1="290.0" x2="390.0" y2="30.0" stroke="#888" stroke-dasharray="4 3"/> <circle cx="121.5" cy="258.9" r="3" fill="#4a7bd0"/> <circle cx="283.1" cy="117.6" r="3" fill="#4a7bd0"/> <circle cx="338.3" cy="75.3" r="3" fill="#4a7bd0"/> <circle cx="134.1" cy="244.7" r="3" fill="#4a7bd0"/> <circle cx="156.4" cy="216.5" r="3" fill="#4a7bd0"/> <circle cx="216.2" cy="160.0" r="3" fill="#4a7bd0"/> <circle cx="183.4" cy="202.4" r="3" fill="#4a7bd0"/> <circle cx="363.8" cy="61.1" r="3" fill="#4a7bd0"/> <circle cx="282.6" cy="103.5" r="3" fill="#4a7bd0"/> <circle cx="120.7" cy="258.9" r="3" fill="#4a7bd0"/> <circle cx="219.9" cy="160.0" r="3" fill="#4a7bd0"/>
<circle cx="281.6" cy="117.6" r="3" fill="#4a7bd0"/> <circle cx="179.3" cy="188.2" r="3" fill="#4a7bd0"/> <circle cx="172.2" cy="202.4" r="3" fill="#4a7bd0"/> <circle cx="131.9" cy="230.6" r="3" fill="#4a7bd0"/> <circle cx="356.4" cy="61.1" r="3" fill="#4a7bd0"/> <circle cx="210.6" cy="174.1" r="3" fill="#4a7bd0"/> <circle cx="138.3" cy="230.6" r="3" fill="#4a7bd0"/> <circle cx="367.0" cy="47.0" r="3" fill="#4a7bd0"/> <circle cx="364.6" cy="47.0" r="3" fill="#4a7bd0"/> <circle cx="331.0" cy="75.3" r="3" fill="#4a7bd0"/> <circle cx="273.1" cy="117.6" r="3" fill="#4a7bd0"/> <circle cx="242.8" cy="145.9" r="3" fill="#4a7bd0"/> <circle cx="211.7" cy="174.1" r="3" fill="#4a7bd0"/> <circle cx="218.7" cy="174.1" r="3" fill="#4a7bd0"/>
<circle cx="227.2" cy="160.0" r="3" fill="#4a7bd0"/> <circle cx="285.8" cy="117.6" r="3" fill="#4a7bd0"/> <circle cx="245.7" cy="145.9" r="3" fill="#4a7bd0"/> <circle cx="333.3" cy="75.3" r="3" fill="#4a7bd0"/> <circle cx="322.4" cy="75.3" r="3" fill="#4a7bd0"/> <circle cx="145.3" cy="230.6" r="3" fill="#4a7bd0"/> <circle cx="101.0" cy="258.9" r="3" fill="#4a7bd0"/> <circle cx="201.7" cy="174.1" r="3" fill="#4a7bd0"/> <circle cx="295.1" cy="117.6" r="3" fill="#4a7bd0"/> <circle cx="187.0" cy="188.2" r="3" fill="#4a7bd0"/> <circle cx="217.8" cy="174.1" r="3" fill="#4a7bd0"/> <circle cx="282.7" cy="117.6" r="3" fill="#4a7bd0"/> <circle cx="171.6" cy="202.4" r="3" fill="#4a7bd0"/> <circle cx="264.5" cy="117.6" r="3" fill="#4a7bd0"/>
<circle cx="310.4" cy="89.4" r="3" fill="#4a7bd0"/> <circle cx="243.0" cy="145.9" r="3" fill="#4a7bd0"/> <circle cx="138.0" cy="244.7" r="3" fill="#4a7bd0"/> <circle cx="94.6" cy="273.0" r="3" fill="#4a7bd0"/> <circle cx="133.5" cy="230.6" r="3" fill="#4a7bd0"/> <circle cx="226.2" cy="145.9" r="3" fill="#4a7bd0"/> <circle cx="227.7" cy="160.0" r="3" fill="#4a7bd0"/> <circle cx="268.5" cy="131.8" r="3" fill="#4a7bd0"/> <circle cx="272.1" cy="117.6" r="3" fill="#4a7bd0"/> <circle cx="198.1" cy="188.2" r="3" fill="#4a7bd0"/> <circle cx="228.8" cy="160.0" r="3" fill="#4a7bd0"/> <circle cx="112.1" cy="244.7" r="3" fill="#4a7bd0"/> <circle cx="81.0" cy="273.0" r="3" fill="#4a7bd0"/> <circle cx="121.2" cy="244.7" r="3" fill="#4a7bd0"/>
<circle cx="149.5" cy="230.6" r="3" fill="#4a7bd0"/> <circle cx="337.9" cy="75.3" r="3" fill="#4a7bd0"/> <circle cx="124.7" cy="244.7" r="3" fill="#4a7bd0"/> <circle cx="89.7" cy="273.0" r="3" fill="#4a7bd0"/> <circle cx="242.4" cy="145.9" r="3" fill="#4a7bd0"/> <circle cx="205.0" cy="188.2" r="3" fill="#4a7bd0"/> <circle cx="84.8" cy="287.1" r="3" fill="#4a7bd0"/> <circle cx="206.4" cy="188.2" r="3" fill="#4a7bd0"/> <circle cx="346.5" cy="75.3" r="3" fill="#4a7bd0"/> <circle cx="147.3" cy="230.6" r="3" fill="#4a7bd0"/> <circle cx="91.8" cy="273.0" r="3" fill="#4a7bd0"/> <circle cx="104.0" cy="258.9" r="3" fill="#4a7bd0"/> <circle cx="328.6" cy="75.3" r="3" fill="#4a7bd0"/> <circle cx="368.6" cy="47.0" r="3" fill="#4a7bd0"/>
<circle cx="248.2" cy="131.8" r="3" fill="#4a7bd0"/> <circle cx="197.1" cy="188.2" r="3" fill="#4a7bd0"/> <circle cx="176.9" cy="202.4" r="3" fill="#4a7bd0"/> <circle cx="104.7" cy="258.9" r="3" fill="#4a7bd0"/> <circle cx="167.2" cy="202.4" r="3" fill="#4a7bd0"/> <circle cx="234.6" cy="160.0" r="3" fill="#4a7bd0"/> <circle cx="324.1" cy="89.4" r="3" fill="#4a7bd0"/> <circle cx="376.3" cy="47.0" r="3" fill="#4a7bd0"/> <circle cx="166.7" cy="216.5" r="3" fill="#4a7bd0"/> <circle cx="83.2" cy="287.1" r="3" fill="#4a7bd0"/> <circle cx="233.6" cy="145.9" r="3" fill="#4a7bd0"/> <circle cx="229.7" cy="160.0" r="3" fill="#4a7bd0"/> <circle cx="184.7" cy="188.2" r="3" fill="#4a7bd0"/> <text x="230" y="332" font-size="13" text-anchor="middle">float logit (class idle)</text>
<text x="18" y="160" font-size="13" text-anchor="middle" transform="rotate(-90 18 160)">int8 logit</text> <text x="410" y="60" font-size="13">점선: y = x (완벽 일치)</text> <text x="410" y="82" font-size="13">점: 실제 test 샘플 80개</text> <text x="410" y="104" font-size="13">int8 값은 0.174 간격의</text> <text x="410" y="122" font-size="13">계단(출력 LSB)에만 놓인다</text> <text x="410" y="150" font-size="13">SQNR 37.7 dB · cos 0.99991</text>
</svg>
```

그림 3 — 실제 테스트 샘플 80개의 float logit(가로) 대 int8 logit(세로), idle 클래스, |logit| < 1.5 구간 확대. 세로 방향으로 0.174 간격의 가로줄 위에만 점이 있다 — 출력 양자화의 흔적이다. 점이 대각선에서 한두 칸 이상 벗어나지 않으면 "양자화 잡음 수준"이다.

### 2.5 어떤 지표를 언제 쓰나

| 상황 | 추천 지표 | 이유 |
|---|---|---|
| 정수 커널 대 정수 레퍼런스 | 불일치 개수, max abs (LSB) | bit-exact가 기대값이므로 0이어야 함 |
| float 커널 대 float 레퍼런스 (다른 BLAS·SIMD) | max abs, max rel (0 근처 제외), SQNR 100 dB 이상 | 합산 순서 차이만 있어야 함 |
| 레이어별 float 대 int8 | SQNR, cosine | 레이어마다 스케일이 달라서 스케일 무관 지표 필요 |
| 모델 출력 (분류) | top-1 agreement, cosine | 결정이 같은지가 핵심 |
| 모델 출력 (threshold 판정, wake word) | KL, 확률 max abs, 운영점의 FA·recall | 확률 값 자체가 판정에 쓰임 |
| 레이블 없는 기기 데이터 | top-1 agreement, KL | 정답 없이 두 모델만 비교 가능 |
| 최종 합격 | 과제 지표 + 신뢰구간 (6절) | 사용자가 느끼는 것 |

SQNR의 "좋은 값"은 절대 규격이 아니라 경험칙이다. 이상적인 균일 양자화기에 full-scale 사인파를 넣으면 SQNR ≈ 6.02·b + 1.76 dB, 즉 8 bit에서 약 50 dB다. 실제 activation은 분포가 치우쳐 있고 range를 다 쓰지 못하므로 int8 레이어는 보통 **30~40 dB**가 나온다(위 예제도 37~39 dB). 필자의 경험칙은 "30 dB 이상 대체로 정상, 20~30 dB 들여다볼 것, 20 dB 미만 또는 음수는 거의 확실히 버그나 심한 outlier"인데, 모델·레이어마다 다르므로 **절대값보다 같은 모델의 정상 기준선 대비 변화**를 보는 것이 더 믿을 만하다(4.3절).

### 2.6 함정

- 배치 전체를 한 벡터로 펴서 cosine을 구하면 샘플 하나가 완전히 틀려도 평균에 묻힌다. 샘플별 cosine의 **최솟값**도 같이 본다.
- KL은 비대칭이다. `KL(P_ref ‖ Q_test)`로 방향을 고정하고 기록한다.
- 정확도가 같다고 모델이 같은 것은 아니다. 위 예제에서도 float은 맞고 int8은 틀린 샘플, 그 반대 샘플이 섞여 있다(6절에서 센다).

---

## 3. bit-exact인가, tolerance인가

### 3.1 판단 기준 한 줄

**두 구현이 같은 정수 연산 규격(spec)을 따르면 bit-exact를 요구하고, 하나라도 float 연산 순서가 자유로우면 tolerance를 쓴다.** 정수 덧셈·곱셈은 overflow만 없으면 결합법칙이 성립하므로 순서가 달라도 결과가 같다. float은 매 연산마다 반올림하므로 순서가 바뀌면 결과가 바뀐다.

| 비교 쌍 | 기대 | 근거 |
|---|---|---|
| numpy 정수 시뮬레이션 ↔ 레퍼런스 int8 C 커널 | bit-exact | 같은 requant 규격, 정수만 사용 |
| 레퍼런스 int8 C ↔ SIMD 최적화 int8 C (CMSIS-NN 류) | bit-exact | 누산 순서만 다르고 정수 |
| TFLite 레퍼런스 커널 ↔ NPU int8 | 대개 ±1 LSB tolerance | NPU가 requant rounding을 다르게 구현하는 경우가 흔함 (벤더 문서 확인) |
| PyTorch float ↔ ONNX Runtime float | tolerance (1e-5 수준) | 다른 커널, 다른 합산 순서 |
| float ↔ int8 | 지표 기반 (SQNR 등) | 정밀도가 다름 — 비교가 아니라 "품질 측정" |
| 같은 바이너리, 같은 입력, 두 번 실행 | bit-exact | 결정성(determinism) — 아니면 버그 (7.2절) |

### 3.2 float 합산 순서 — numpy로 확인

같은 float32 숫자 10만 개를 순차, 역순, numpy pairwise, 8-lane 부분합(SIMD 흉내) 순서로 더해 본다.

```python
# 같은 float32 숫자들을 순서만 바꿔 더하면 결과가 달라지는지 확인한다
import numpy as np
rng = np.random.default_rng(0)
x = (rng.standard_normal(100_000) * 100).astype(np.float32)
exact = np.sum(x.astype(np.float64))                          # float64 기준값
def seq_sum(v):                                               # C의 for 루프와 같은 순차 합
    s = np.float32(0)
    for e in v: s += e
    return s
res = {"sequential": seq_sum(x), "reverse": seq_sum(x[::-1]), "numpy pairwise": np.sum(x),
       "8-lane": seq_sum(x.reshape(-1, 8).sum(axis=0, dtype=np.float32))}  # SIMD 8-lane 부분합 흉내
for name, s in res.items():
    print(f"{name:15s} {s:.6f}  err vs fp64 = {float(s) - exact:+.6f}")
a, b, c = np.float32(1e8), np.float32(-1e8), np.float32(1.0)  # 결합법칙이 깨지는 가장 작은 예
print("(a+b)+c =", (a + b) + c, "  a+(b+c) =", a + (b + c))
```

```text
sequential      -9082.465820  err vs fp64 = +0.041756
reverse         -9082.592773  err vs fp64 = -0.085197
numpy pairwise  -9082.508789  err vs fp64 = -0.001212
8-lane          -9082.616211  err vs fp64 = -0.108634
(a+b)+c = 1.0   a+(b+c) = 0.0
```

출력에서 볼 것: 네 방법이 모두 다른 값을 내고, float64 기준 대비 오차도 제각각이다. numpy의 `np.sum`은 pairwise(블록 트리) 합이라 가장 정확하다. 마지막 줄은 결합법칙이 깨지는 최소 예다 — `1e8 + 1`은 float32로 표현하면 `1e8`이 되어(ulp가 8) 1이 사라진다. 커널을 SIMD로 바꾸거나 다른 BLAS를 쓰면 합산 트리가 바뀌므로, **float 결과의 bit-exact 비교는 원래 성립하지 않는다.**

### 3.3 FMA contraction — 컴파일 옵션 하나로 결과가 바뀐다

FMA(fused multiply-add)는 `a·b + c`를 **반올림 한 번**으로 계산하는 명령이다(ARM `fmadd`, `fmla`). 곱셈 결과를 반올림하지 않으니 더 정확하지만, 반올림을 두 번 하는 코드와는 결과가 다르다. C 컴파일러는 `-ffp-contract` 옵션에 따라 소스의 `a*b + c`를 FMA로 합칠지 정한다.

손계산: `a = 1 + 2⁻¹²`이면 `a² = 1 + 2⁻¹¹ + 2⁻²⁴`. float32의 가수는 23 bit라 1 근처의 ulp는 2⁻²³이고, 2⁻²⁴는 정확히 반 ulp다. 곱셈을 먼저 반올림하면 round-half-even으로 `1 + 2⁻¹¹`이 되어 `a·a − (1 + 2⁻¹¹) = 0`. FMA는 곱셈 결과를 반올림하지 않으므로 `2⁻²⁴ ≈ 5.96e−8`이 남는다.

```c
/* fma.c — 같은 소스, 컴파일 옵션(-ffp-contract)만 바꿔서 결과가 달라지는지 본다 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

int main(void) {
    volatile float va = 1.0f + 0x1p-12f, vc = -(1.0f + 0x1p-11f);  /* volatile: 상수 접기 방지 */
    float a = va, c = vc;
    float r = a * a + c;          /* FMA로 합쳐지면 곱셈 결과를 반올림하지 않는다 */
    printf("a*a + c  = %.10e (0x%08x)\n", (double)r, (unsigned)bits(r));

    volatile int k0 = 37, k1 = 53;
    int p = k0, q = k1;
    float x[1000], w[1000];
    for (int i = 0; i < 1000; i++) {
        x[i] = (float)((i * p) % 101 - 50) / 7.0f;
        w[i] = (float)((i * q) % 97 - 48) / 11.0f;
    }
    float seq = 0.0f, lane[4] = {0};
    for (int i = 0; i < 1000; i++) seq += x[i] * w[i];          /* 순차 합 */
    for (int i = 0; i < 1000; i++) lane[i % 4] += x[i] * w[i];  /* 4-lane 부분합 (SIMD 흉내) */
    float simd = (lane[0] + lane[1]) + (lane[2] + lane[3]);
    printf("dot seq  = %.6f (0x%08x)\n", (double)seq, (unsigned)bits(seq));
    printf("dot simd = %.6f (0x%08x)\n", (double)simd, (unsigned)bits(simd));
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 -ffp-contract=off  fma.c -o fma_off  -lm && ./fma_off
cc -std=c11 -Wall -Wextra -O2 -ffp-contract=fast fma.c -o fma_fast -lm && ./fma_fast
cc -std=c11 -Wall -Wextra -O2                    fma.c -o fma_def  -lm && ./fma_def
```

```text
== -ffp-contract=off
a*a + c  = 0.0000000000e+00 (0x00000000)
dot seq  = -30.766449 (0xc1f621b0)
dot simd = -30.766174 (0xc1f62120)
== -ffp-contract=fast
a*a + c  = 5.9604644775e-08 (0x33800000)
dot seq  = -30.766449 (0xc1f621b0)
dot simd = -30.766102 (0xc1f620fa)
== default (옵션 없음)
a*a + c  = 5.9604644775e-08 (0x33800000)
dot seq  = -30.766449 (0xc1f621b0)
dot simd = -30.766102 (0xc1f620fa)
```

출력에서 볼 것은 세 가지다.

1. `a*a + c`가 off에서는 0, fast에서는 2⁻²⁴(= 0x33800000)다. 손계산과 같다.
2. 이 컴파일러(Apple clang 21)의 기본값은 fast와 같은 결과를 냈다 — **기본값은 컴파일러·버전마다 다르므로** 레퍼런스 빌드에서는 옵션을 명시해야 한다.
3. 순차 내적(`dot seq`)은 두 옵션에서 같았고 4-lane 내적만 달랐다. 디스어셈블해 보면(`objdump -d`) fast 빌드의 4-lane 루프에는 `fmla`가 있고, 순차 루프는 곱셈을 먼저 벡터로(`fmul.4s`) 한 뒤 덧셈을 순서대로 해서 FMA가 생기지 않았다. **같은 옵션이라도 코드 모양에 따라 FMA가 생기기도 안 생기기도 한다.** 그래서 float 커널 비교는 항상 tolerance다.

Don의 경험과 연결하면: 펌웨어에서 "같은 소스인데 컴파일러 버전을 올렸더니 결과가 1 LSB 달라졌다"를 겪었다면 대개 이런 원인이다. float 경로에서 bit-exact를 원하면 `-ffp-contract=off`, 같은 합산 순서, 같은 libm을 모두 고정해야 하고, 그래도 다른 하드웨어(NPU)와는 맞출 수 없다.

### 3.4 정수 커널은 bit-exact가 된다 — requantization 규격

int8 추론의 핵심 정수 연산은 C1에서 다룬 requantization이다. 누산기(int32) 값을 출력 int8로 바꿀 때 실수 배율 `M = s_x·s_w / s_y`를 곱해야 하는데, 정수만 쓰려고 `M = M0 · 2^shift` (M0는 Q31 정수)로 쪼갠다. TFLite·gemmlowp 계열의 규격은 다음 두 함수로 반올림 방식까지 정한다.

```
SRDHM(a, M0)   = round( a · M0 / 2³¹ )      ← 64-bit 곱, 절반은 0에서 먼 쪽으로
RDBPOT(x, e)   = round( x / 2^e )           ← 산술 shift + 나머지로 반올림
y = clamp( RDBPOT(SRDHM(acc, M0), −shift) + zp_out, −128, 127 )
```

손계산: `M = 0.02·0.005/0.05 = 0.002 = 0.512 · 2⁻⁸` → `M0 = round(0.512·2³¹) = 1099511628`, `shift = −8`. 누산기 `acc = 12345`라면 실수로는 12345·0.002 = 24.69 → 25. 정수로는 `SRDHM = round(12345·0.512) = round(6320.64) = 6321`, `RDBPOT(6321, 8) = round(6321/256) = round(24.69) = 25`. 같다.

numpy 참조 구현과 C 구현을 같은 규칙으로 쓴다.

```python
# c8_int.py — 정수 전용 requantization (gemmlowp/TFLite 방식)의 numpy 참조 구현
import numpy as np

def quantize_multiplier(M):
    """실수 M(0<M<1) → (M0: Q31 정수, shift: 음수면 오른쪽 shift)"""
    q, e = np.frexp(M)                          # M = q · 2^e, 0.5 <= q < 1
    M0 = np.round(q * (1 << 31)).astype(np.int64)
    e = np.where(M0 == (1 << 31), e + 1, e); M0 = np.where(M0 == (1 << 31), M0 // 2, M0)
    return M0.astype(np.int64), e.astype(np.int64)

def srdhm(a, b):                                # SaturatingRoundingDoublingHighMul
    ab = a.astype(np.int64) * b
    nudge = np.where(ab >= 0, 1 << 30, 1 - (1 << 30))
    s = ab + nudge
    return np.where(s >= 0, s >> 31, -((-s) >> 31))   # C처럼 0 방향으로 자르는 나눗셈

def rdbpot(x, e):                               # RoundingDivideByPOT (e >= 0)
    mask = (np.int64(1) << e) - 1
    rem = x & mask
    thr = (mask >> 1) + (x < 0)
    return (x >> e) + (rem > thr)

def requant(acc, M0, shift, zp_out, lo=-128, hi=127):
    y = rdbpot(srdhm(acc, M0), -shift)          # shift <= 0 가정 (M < 1)
    return np.clip(y + zp_out, lo, hi).astype(np.int8)
```

```c
/* qkernel.h — c8_int.py와 같은 규칙의 정수 requantization (C) */
#include <stdint.h>
static inline int32_t srdhm(int32_t a, int32_t b) {
    if (a == INT32_MIN && b == INT32_MIN) return INT32_MAX;
    int64_t ab = (int64_t)a * b;
    int64_t nudge = ab >= 0 ? (1LL << 30) : (1 - (1LL << 30));
    return (int32_t)((ab + nudge) / (1LL << 31));        /* C 나눗셈은 0 방향 */
}
static inline int32_t rdbpot(int32_t x, int e) {
    int32_t mask = (int32_t)((1LL << e) - 1);
    int32_t rem = x & mask, thr = (mask >> 1) + (x < 0);
    return (x >> e) + (rem > thr);
}
static inline int8_t requant(int32_t acc, int32_t M0, int shift, int32_t zp, int32_t lo, int32_t hi) {
    int32_t y = rdbpot(srdhm(acc, M0), -shift) + zp;
    return (int8_t)(y < lo ? lo : (y > hi ? hi : y));
}
```

int8 dense 레이어(입력 32, 출력 16, 채널별 scale)를 C로 쓰고, 인자로 누산 순서를 뒤집을 수 있게 한다.

```c
/* dense.c — int8 dense + requant. argv[1]="rev"이면 누산 순서를 뒤집는다 */
#include <stdio.h>
#include <string.h>
#include "qkernel.h"
#define N_IN 32
#define N_OUT 16
struct { int8_t x[N_IN], w[N_OUT][N_IN]; int32_t bias[N_OUT], M0[N_OUT], shift[N_OUT], zp_x, zp_y; } p;

int main(int argc, char **argv) {
    FILE *f = fopen("dense_in.bin", "rb");          /* Python이 같은 순서로 채운 파일 */
    if (!f || fread(&p, 1, sizeof p, f) != sizeof p) return 1;
    fclose(f);
    int rev = argc > 1 && strcmp(argv[1], "rev") == 0;
    int8_t y[N_OUT];
    for (int o = 0; o < N_OUT; o++) {
        int32_t acc = p.bias[o];
        for (int k = 0; k < N_IN; k++) {
            int i = rev ? N_IN - 1 - k : k;
            acc += (int32_t)(p.x[i] - p.zp_x) * p.w[o][i];
        }
        y[o] = requant(acc, p.M0[o], p.shift[o], p.zp_y, -128, 127);
    }
    fwrite(y, 1, sizeof y, stdout);
    return 0;
}
```

Python이 입력을 이진 파일로 쓰고, C 결과(정순·역순)를 numpy 정수 참조와 비교한다. 마지막에는 "같은 수학이지만 float로 requant하는" 구현을 10만 개 누산값에 대해 비교한다.

```python
# 정수 커널은 구현(numpy vs C)과 누산 순서가 달라도 비트 단위로 같아야 한다
import numpy as np, subprocess, hashlib
from c8_int import quantize_multiplier, requant
rng = np.random.default_rng(0)
x = rng.integers(-128, 128, 32).astype(np.int8)
w = rng.integers(-127, 128, (16, 32)).astype(np.int8)
bias = rng.integers(-2000, 2000, 16).astype(np.int32)
s_x, s_w, s_y, zp_x, zp_y = 0.02, rng.uniform(0.002, 0.01, 16), 0.05, -3, 5
M0, shift = quantize_multiplier(s_x * s_w / s_y)          # 채널별 실수 배율 → (Q31, shift)
with open("dense_in.bin", "wb") as f:
    for a in (x, w, bias, M0.astype(np.int32), shift.astype(np.int32),
              np.int32([zp_x]), np.int32([zp_y])):
        f.write(a.tobytes())
acc = bias.astype(np.int64) + (w.astype(np.int64) @ (x.astype(np.int64) - zp_x))
y_ref = requant(acc, M0, shift, zp_y)                       # numpy 정수 참조
y_c = np.frombuffer(subprocess.run(["./dense"], capture_output=True).stdout, np.int8)
y_rev = np.frombuffer(subprocess.run(["./dense", "rev"], capture_output=True).stdout, np.int8)
print("M0[:3] =", M0[:3], " shift[:3] =", shift[:3])
print("numpy int ref :", y_ref[:8])
print("C forward     :", y_c[:8])
print("sha1 ref/C/rev:", *[hashlib.sha1(a.tobytes()).hexdigest()[:10] for a in (y_ref, y_c, y_rev)])
print("bit-exact C vs ref:", np.array_equal(y_ref, y_c), " C rev vs ref:", np.array_equal(y_ref, y_rev))
# 같은 수학이라도 "float로 requant"하면 규격이 다르다: 누산값 10만 개로 비교
A = rng.integers(-300_000, 300_000, (100_000, 1)); Mf = s_x * s_w[:1] / s_y
y_f = np.clip(np.round(A * Mf) + zp_y, -128, 127).astype(np.int8)       # float32/64 + round-half-even
d = y_f.astype(int) - requant(A, M0[:1], shift[:1], zp_y)
print("float requant vs int requant: mismatches =", np.count_nonzero(d), "/ 100000, max |diff| =", np.abs(d).max())
```

```sh
cc -std=c11 -Wall -Wextra -O2 dense.c -o dense -lm && .venv/bin/python ex5.py
```

```text
M0[:3] = [1325882564 1765281819 2047444878]  shift[:3] = [-8 -8 -8]
numpy int ref : [   5    0    7    3   16 -128   13   12]
C forward     : [   5    0    7    3   16 -128   13   12]
sha1 ref/C/rev: 12694640e7 12694640e7 12694640e7
bit-exact C vs ref: True  C rev vs ref: True
float requant vs int requant: mismatches = 35 / 100000, max |diff| = 1
```

출력에서 볼 것: numpy 정수 참조, C 정순, C 역순의 SHA-1이 같다 — **정수 커널은 구현·누산 순서와 무관하게 bit-exact**다. 반면 float로 requant(곱한 뒤 round-half-even)한 구현은 10만 개 중 35개가 1 LSB 다르다. 수학적으로 "같은" 연산도 반올림 규격이 다르면 bit-exact가 깨진다. NPU나 DSP 벤더 커널이 레퍼런스와 ±1 LSB 다를 때 가장 먼저 의심할 곳이 이 부분이다.

### 3.5 결정 절차

```
두 구현 모두 정수 연산만?  ── 예 ──▶ 같은 requant 규격(rounding, saturation)?
        │                                  ├─ 예 ──▶ bit-exact 요구. 1 LSB라도 다르면 버그.
        아니오                              └─ 아니오 ──▶ ±1 LSB tolerance + 불일치 "비율" 상한
        ▼                                                 (예: 0.5% 이하) + 편향(평균 오차 ≈ 0) 확인
float 연산이 섞임
        ├─ 같은 코드·같은 컴파일 옵션·같은 HW ──▶ bit-exact (결정성 테스트)
        └─ 그 외 ──▶ tolerance: max abs ≤ k·ulp·√n 수준, 또는 SQNR ≥ 100 dB
정밀도 자체가 다름 (float ↔ int8) ──▶ 비교가 아니라 품질 측정: SQNR·top-1·과제 지표
```

tolerance를 쓸 때의 함정: ±1 LSB를 허용하면 **"항상 1 LSB씩 한쪽으로 치우친" 버그**도 통과한다. 5.3절에서 실제로 보인다. 그래서 tolerance에는 항상 "불일치 비율"과 "평균 오차(편향)" 조건을 같이 건다.

---

## 4. 레이어별 diff — 처음 무너지는 레이어를 찾는다

### 4.1 직관 — bring-up의 bisect와 같다

출력이 틀렸을 때 Don은 bring-up에서 데이터 경로를 따라 중간 지점의 값을 찍어 보고, 처음으로 기대값과 달라지는 블록을 찾았을 것이다(DMA 전후, FIFO 전후, 레지스터 dump). ML도 똑같다. 모델의 모든 중간 activation을 기준 모델과 비교해 **SQNR이 처음으로 뚝 떨어지는 레이어**를 찾는다. 그 레이어 또는 그 레이어의 입력 해석이 원인이다.

필요한 것은 세 가지다: (1) 기준 모델의 중간값, (2) 검증 대상의 중간값, (3) 둘의 **이름 정렬**.

### 4.2 PyTorch forward hook으로 중간값 뽑기

`register_forward_hook`은 모듈의 forward가 끝날 때마다 호출되는 콜백이다(펌웨어의 trace point). 양자화 모델의 출력은 quantized tensor이므로 `dequantize()`해서 float로 비교한다.

```python
# c8_capture.py — forward hook으로 모듈별 출력을 dict에 모은다
import torch

def capture(model, x, names):
    acts, hooks = {}, []
    mods = dict(model.named_modules())
    for n in names:
        def hook(mod, inp, out, n=n):
            acts[n] = (out.dequantize() if out.is_quantized else out).detach().numpy().copy()
        hooks.append(mods[n].register_forward_hook(hook))
    with torch.no_grad(): model(x)
    for h in hooks: h.remove()
    return acts
```

float 모델과 int8 모델의 레이어를 짝지어 비교한다. **주의: fusion 때문에 이름이 어긋난다.** int8 모델의 `conv1`은 conv+ReLU가 합쳐진 모듈이라 출력이 ReLU 뒤 값이다. 그래서 float 쪽은 `conv1`이 아니라 `relu1`과 짝지어야 한다.

```python
# float 모델과 int8 모델의 중간 activation을 레이어별로 비교한다
import torch, numpy as np
from c8_model import load, make_data, quantize_ptq
from c8_capture import capture
from c8_metrics import sqnr_db, cosine, max_abs
m = load()
X, _, _ = make_data(50, range(20, 26), seed=2); x = torch.from_numpy(X)   # 300개
Xc, _, _ = make_data(20, range(20), seed=3)
q = quantize_ptq(m, Xc)
# fusion 때문에 이름이 어긋난다: int8의 conv1(=conv+relu) ↔ float의 relu1
pairs = [("quant", "quant"), ("relu1", "conv1"), ("relu2", "conv2"),
         ("relu3", "conv3"), ("pool", "pool"), ("fc", "fc")]
fa = capture(m, x, [p[0] for p in pairs])
qa = capture(q, x, [p[1] for p in pairs])
print(f"{'float':6s} {'int8':6s} {'shape':12s} {'SQNR dB':>8s} {'cosine':>9s} {'maxabs':>7s}")
for fn, qn in pairs:
    r, t = fa[fn], qa[qn]
    print(f"{fn:6s} {qn:6s} {str(r.shape):12s} {sqnr_db(r, t):8.2f} {cosine(r, t):9.6f} {max_abs(r, t):7.4f}")
```

```text
float  int8   shape         SQNR dB    cosine  maxabs
quant  quant  (300, 6, 50)    38.99  0.999937  0.0170
relu1  conv1  (300, 16, 50)    38.79  0.999934  0.1164
relu2  conv2  (300, 32, 25)    39.77  0.999947  0.3180
relu3  conv3  (300, 32, 25)    38.67  0.999932  0.3112
pool   pool   (300, 32, 1)    36.91  0.999898  0.1340
fc     fc     (300, 4)        37.40  0.999910  0.2797
```

출력에서 볼 것: 정상 PTQ 모델은 모든 레이어가 37~40 dB로 고르다. 오차가 레이어를 지나며 조금씩 쌓이지만 폭발하지 않는다. 이것이 **정상 기준선(baseline)**이고, 나중에 버그를 찾을 때 이 표와 비교한다. 만약 float `conv1`(ReLU 전)과 int8 `conv1`(ReLU 후)을 짝지었다면 음수 부분이 전부 틀린 것으로 나와 SQNR이 약 4.0 dB로 나온다(같은 데이터로 직접 돌려 본 값) — 이름 정렬 실수가 "가짜 버그"를 만든다.

### 4.3 ONNX Runtime에서 중간값 뽑기 — 그래프 출력으로 승격

배포 경로가 ONNX → 벤더 컴파일러라면 ONNX Runtime 결과도 기준 중 하나다. ONNX Runtime은 그래프 출력만 돌려주므로, 보고 싶은 중간 텐서를 **그래프 출력 목록에 추가**한다. 텐서 이름은 exporter가 `"/relu1/Relu_output_0"`처럼 모듈 경로로 짓기 때문에 PyTorch 모듈 이름과 정렬할 수 있다.

```python
# ONNX Runtime에서도 중간 텐서를 꺼내 PyTorch hook 결과와 레이어별로 맞춰 본다
import torch, numpy as np, onnx, onnxruntime as ort, warnings
warnings.filterwarnings("ignore", category=DeprecationWarning)   # legacy exporter 안내 문구 숨김
from c8_model import load, make_data
from c8_capture import capture
from c8_metrics import sqnr_db, max_abs
m = load()
X, _, _ = make_data(50, range(20, 26), seed=2)
torch.onnx.export(m, torch.from_numpy(X[:1]), "c8.onnx", input_names=["x"], output_names=["logits"],
                  dynamic_axes={"x": {0: "N"}}, dynamo=False)
g = onnx.load("c8.onnx")
# 1) 중간 텐서를 그래프 출력으로 승격 (ValueInfo만 추가하면 된다)
want = {n.output[0]: n.op_type for n in g.graph.node if n.op_type in ("Relu", "GlobalAveragePool", "ReduceMean")}
for name in want:
    g.graph.output.append(onnx.helper.make_empty_tensor_value_info(name))
onnx.save(g, "c8_debug.onnx")
sess = ort.InferenceSession("c8_debug.onnx", providers=["CPUExecutionProvider"])
outs = dict(zip([o.name for o in sess.get_outputs()], sess.run(None, {"x": X})))
# 2) 이름 정렬: ONNX 텐서 이름 → PyTorch 모듈 이름 (export가 "/relu1/Relu_output_0" 식으로 짓는다)
ta = capture(m, torch.from_numpy(X), ["relu1", "relu2", "relu3", "pool", "fc"])
align = {n: n.split("/")[1] for n in want}; align["logits"] = "fc"
for on, tn in align.items():
    r, t = ta[tn], outs[on].reshape(ta[tn].shape)
    print(f"{on:32s} ↔ {tn:5s} maxabs={max_abs(r, t):.2e} SQNR={sqnr_db(r, t):6.1f} dB bitexact={np.array_equal(r, t)}")
```

```text
/relu1/Relu_output_0             ↔ relu1 maxabs=1.43e-06 SQNR= 137.4 dB bitexact=False
/relu2/Relu_output_0             ↔ relu2 maxabs=2.38e-06 SQNR= 139.6 dB bitexact=False
/relu3/Relu_output_0             ↔ relu3 maxabs=1.53e-05 SQNR= 135.0 dB bitexact=False
/pool/GlobalAveragePool_output_0 ↔ pool  maxabs=2.86e-06 SQNR= 139.9 dB bitexact=False
logits                           ↔ fc    maxabs=3.81e-06 SQNR= 139.1 dB bitexact=False
```

출력에서 볼 것: 같은 float 모델이지만 PyTorch와 ONNX Runtime은 bit-exact가 아니다(`bitexact=False`). 차이는 1e−6~1e−5, SQNR 135~140 dB로 **합산 순서 차이 수준**이다(3.2절). float ↔ float 비교에서 이 정도면 정상이고, 만약 60 dB 같은 값이 나오면 export 과정에서 뭔가(예: 다른 padding 모드, 다른 epsilon) 바뀐 것이다.

실무 팁: 벤더 NPU 툴체인도 대부분 "레이어별 출력 dump" 기능이 있다(예: Qualcomm QNN이나 TFLite의 중간 텐서 보존 옵션 — 이름·옵션은 툴 버전마다 다르므로 문서를 확인). 핵심은 이 절과 같다: 중간 텐서를 꺼내고, 이름을 맞추고, 레이어별 SQNR 표를 만든다.

### 4.4 버그 주입 실험 — per-layer diff가 원인을 짚는가

실제 root-cause 연습을 위해 numpy로 int8 추론을 흉내 내는(fake quant: 양자화 후 바로 역양자화) 시뮬레이터를 만들고, 흔한 버그 세 가지를 스위치로 주입한다.

- `zp`: 입력의 zero-point를 빼는 것을 잊음 (펌웨어가 int8 입력을 zp 없이 해석)
- `layout`: conv1 출력 버퍼는 [N, L, C] (채널 마지막, NHWC 계열)로 썼는데 conv2는 [N, C, L]로 읽음
- `chscale`: conv3의 채널별 weight scale 표에서 채널 7 자리에 채널 6의 값을 씀 (오프바이원)

```python
# c8_sim.py — numpy로 쓴 "int8 흉내(fake quant)" 추론 + 버그 주입 스위치
import numpy as np
from numpy.lib.stride_tricks import sliding_window_view as swv

def conv1d(x, w, b, stride, pad):                 # x [N,C,L], w [O,C,K]
    xp = np.pad(x, ((0, 0), (0, 0), (pad, pad)))
    win = swv(xp, w.shape[2], axis=2)[:, :, ::stride]  # [N,C,Lo,K]
    return np.einsum("nclk,ock->nol", win, w) + b[None, :, None]

def fq(x, scale, zp, lo=0, hi=255):               # quantize → dequantize (uint8 activation)
    return (np.clip(np.round(x / scale) + zp, lo, hi) - zp) * scale

def wq(w):                                        # int8 symmetric per-output-channel weight
    s = np.abs(w).reshape(w.shape[0], -1).max(1) / 127
    return np.round(w / s.reshape(-1, *[1] * (w.ndim - 1))), s

def forward(P, x, qp=None, bug=None):
    """P: float 가중치 dict, qp: 레이어별 activation (scale, zp). qp=None이면 float 기준 모델."""
    acts = {}
    def act(name, v):
        if qp is not None: v = fq(v, *qp[name])
        acts[name] = v; return v
    def W(name, ch_bug=False):
        if qp is None: return P[name + ".weight"]
        q, s = wq(P[name + ".weight"])
        if ch_bug: s = s.copy(); s[7] = s[6]      # 버그: 채널 7에 채널 6의 scale을 씀
        return q * s[:, None, None] if q.ndim == 3 else q * s[:, None]
    h = act("input", x)
    if bug == "zp": h = h + qp["input"][0] * qp["input"][1]   # 버그: dequant 때 zero-point 누락
    h = act("conv1", np.maximum(conv1d(h, W("conv1"), P["conv1.bias"], 1, 2), 0))
    if bug == "layout": h = np.ascontiguousarray(h.transpose(0, 2, 1)).reshape(h.shape)  # NLC 버퍼를 NCL로 읽음
    h = act("conv2", np.maximum(conv1d(h, W("conv2"), P["conv2.bias"], 2, 2), 0))
    h = act("conv3", np.maximum(conv1d(h, W("conv3", bug == "chscale"), P["conv3.bias"], 1, 1), 0))
    h = act("pool", h.mean(2))
    act("fc", h @ W("fc").T + P["fc.bias"])
    return acts

def calibrate(P, Xc):                             # float activation의 min/max → (scale, zp)
    qp = {}
    for n, v in forward(P, Xc).items():
        lo, hi = min(v.min(), 0.0), max(v.max(), 0.0)
        s = (hi - lo) / 255; qp[n] = (s, int(np.round(-lo / s)))
    return qp
```

각 버그로 추론을 돌리고, float 기준과 레이어별 SQNR 표를 만든다.

```python
# 버그 3종을 주입하고, 레이어별 SQNR로 "처음 무너지는 레이어"를 찾는다
import numpy as np
from c8_model import load, make_data
from c8_sim import forward, calibrate
from c8_metrics import sqnr_db
P = {k: v.numpy().astype(np.float64) for k, v in load().state_dict().items()}
X, y, _ = make_data(50, range(20, 26), seed=2)
Xc, _, _ = make_data(20, range(20), seed=3)
qp = calibrate(P, Xc.astype(np.float64))
ref = forward(P, X.astype(np.float64))                       # float 기준
print(f"{'layer':7s}" + "".join(f"{b:>10s}" for b in ["clean", "zp", "layout", "chscale"]))
res = {b: forward(P, X.astype(np.float64), qp, None if b == "clean" else b)
       for b in ["clean", "zp", "layout", "chscale"]}
for n in ref:
    print(f"{n:7s}" + "".join(f"{sqnr_db(ref[n], res[b][n]):10.2f}" for b in res))
print(f"{'acc':7s}" + "".join(f"{np.mean(res[b]['fc'].argmax(1) == y):10.3f}" for b in res))
# chscale: 정확도는 거의 그대로인데 conv3에서 SQNR이 11 dB 떨어졌다 → 채널별로 쪼개 본다
def per_ch(b): return np.array([sqnr_db(ref["conv3"][:, c], res[b]["conv3"][:, c]) for c in range(32)])
ch_clean, ch_bug = per_ch("clean"), per_ch("chscale")
print("clean  : worst ch =", np.argsort(ch_clean)[:2], np.round(np.sort(ch_clean)[:2], 1),
      " ch30 power =", round(float(np.mean(ref["conv3"][:, 30] ** 2)), 6))
delta = ch_bug - ch_clean                                   # 절대값 대신 "깨끗한 기준 대비 변화"
print("chscale: SQNR 변화가 가장 큰 채널 =", delta.argmin(), f"({delta.min():+.1f} dB), 나머지 최대 변화 {np.sort(delta)[1]:+.1f} dB")
```

```text
layer       clean        zp    layout   chscale
input       38.17     38.17     38.17     38.17
conv1       38.03    -15.13     38.03     38.03
conv2       38.89    -14.05      2.40     38.89
conv3       37.70    -15.59      2.06     26.82
pool        42.83    -10.84      3.94     31.54
fc          39.00    -10.11     -1.48     32.78
acc         0.960     0.260     0.273     0.957
clean  : worst ch = [30 27] [12.7 32.5]  ch30 power = 1.6e-05
chscale: SQNR 변화가 가장 큰 채널 = 7 (-31.4 dB), 나머지 최대 변화 +0.0 dB
```

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<line x1="80" y1="270" x2="560" y2="270" stroke="currentColor"/> <line x1="80" y1="270" x2="80" y2="30" stroke="currentColor"/> <line x1="75" y1="270.0" x2="80" y2="270.0" stroke="currentColor"/><text x="71" y="274.0" font-size="12" text-anchor="end">-20</text> <line x1="75" y1="235.7" x2="80" y2="235.7" stroke="currentColor"/><text x="71" y="239.7" font-size="12" text-anchor="end">-10</text> <line x1="75" y1="201.4" x2="80" y2="201.4" stroke="currentColor"/><text x="71" y="205.4" font-size="12" text-anchor="end">0</text> <line x1="75" y1="167.1" x2="80" y2="167.1" stroke="currentColor"/><text x="71" y="171.1" font-size="12" text-anchor="end">10</text> <line x1="75" y1="132.9" x2="80" y2="132.9" stroke="currentColor"/><text x="71" y="136.9" font-size="12" text-anchor="end">20</text> <line x1="75" y1="98.6" x2="80" y2="98.6" stroke="currentColor"/><text x="71" y="102.6" font-size="12" text-anchor="end">30</text> <line x1="75" y1="64.3" x2="80" y2="64.3" stroke="currentColor"/><text x="71" y="68.3" font-size="12" text-anchor="end">40</text> <line x1="75" y1="30.0" x2="80" y2="30.0" stroke="currentColor"/><text x="71" y="34.0" font-size="12" text-anchor="end">50</text> <line x1="80" y1="98.6" x2="560" y2="98.6" stroke="#888" stroke-dasharray="4 3"/> <text x="80.0" y="289" font-size="12" text-anchor="middle">input</text> <text x="176.0" y="289" font-size="12" text-anchor="middle">conv1</text> <text x="272.0" y="289" font-size="12" text-anchor="middle">conv2</text>
<text x="368.0" y="289" font-size="12" text-anchor="middle">conv3</text> <text x="464.0" y="289" font-size="12" text-anchor="middle">pool</text> <text x="560.0" y="289" font-size="12" text-anchor="middle">fc</text> <polyline points="80.0,70.6 176.0,71.0 272.0,68.1 368.0,72.2 464.0,54.6 560.0,67.7" fill="none" stroke="#3f9a6b" stroke-width="2"/> <circle cx="80.0" cy="70.6" r="3.5" fill="#3f9a6b"/> <circle cx="176.0" cy="71.0" r="3.5" fill="#3f9a6b"/> <circle cx="272.0" cy="68.1" r="3.5" fill="#3f9a6b"/> <circle cx="368.0" cy="72.2" r="3.5" fill="#3f9a6b"/> <circle cx="464.0" cy="54.6" r="3.5" fill="#3f9a6b"/> <circle cx="560.0" cy="67.7" r="3.5" fill="#3f9a6b"/> <polyline points="80.0,70.6 176.0,253.3 272.0,249.6 368.0,254.9 464.0,238.6 560.0,236.1" fill="none" stroke="#d0564a" stroke-width="2"/> <circle cx="80.0" cy="70.6" r="3.5" fill="#d0564a"/> <circle cx="176.0" cy="253.3" r="3.5" fill="#d0564a"/> <circle cx="272.0" cy="249.6" r="3.5" fill="#d0564a"/>
<circle cx="368.0" cy="254.9" r="3.5" fill="#d0564a"/> <circle cx="464.0" cy="238.6" r="3.5" fill="#d0564a"/> <circle cx="560.0" cy="236.1" r="3.5" fill="#d0564a"/> <polyline points="80.0,70.6 176.0,71.0 272.0,193.2 368.0,194.4 464.0,187.9 560.0,206.5" fill="none" stroke="#e08a3c" stroke-width="2"/> <circle cx="80.0" cy="70.6" r="3.5" fill="#e08a3c"/> <circle cx="176.0" cy="71.0" r="3.5" fill="#e08a3c"/> <circle cx="272.0" cy="193.2" r="3.5" fill="#e08a3c"/> <circle cx="368.0" cy="194.4" r="3.5" fill="#e08a3c"/> <circle cx="464.0" cy="187.9" r="3.5" fill="#e08a3c"/> <circle cx="560.0" cy="206.5" r="3.5" fill="#e08a3c"/> <polyline points="80.0,70.6 176.0,71.0 272.0,68.1 368.0,109.5 464.0,93.3 560.0,89.0" fill="none" stroke="#4a7bd0" stroke-width="2"/> <circle cx="80.0" cy="70.6" r="3.5" fill="#4a7bd0"/> <circle cx="176.0" cy="71.0" r="3.5" fill="#4a7bd0"/> <circle cx="272.0" cy="68.1" r="3.5" fill="#4a7bd0"/>
<circle cx="368.0" cy="109.5" r="3.5" fill="#4a7bd0"/> <circle cx="464.0" cy="93.3" r="3.5" fill="#4a7bd0"/> <circle cx="560.0" cy="89.0" r="3.5" fill="#4a7bd0"/> <text x="20" y="150" font-size="13" text-anchor="middle" transform="rotate(-90 20 150)">SQNR (dB) vs float</text> <text x="566" y="96" font-size="12">30 dB 경험선</text> <line x1="90" y1="301" x2="112" y2="301" stroke="#3f9a6b" stroke-width="3"/><text x="118" y="305" font-size="12">정상 int8</text> <line x1="380" y1="301" x2="402" y2="301" stroke="#4a7bd0" stroke-width="3"/><text x="408" y="305" font-size="12">conv3 채널 7 scale 버그</text> <line x1="90" y1="319" x2="112" y2="319" stroke="#e08a3c" stroke-width="3"/><text x="118" y="323" font-size="12">conv1→conv2 layout 버그</text> <line x1="380" y1="319" x2="402" y2="319" stroke="#d0564a" stroke-width="3"/><text x="408" y="323" font-size="12">입력 zero-point 누락</text>
</svg>
```

그림 4 — 위 표의 실제 값. 초록(정상)은 모든 레이어가 38 dB 근처다. 빨강(zero-point)은 conv1에서, 주황(layout)은 conv2에서, 파랑(채널 scale)은 conv3에서 처음 떨어진다. **처음 떨어지는 레이어가 원인의 위치**다.

출력에서 볼 것을 버그별로 읽으면 이렇다.

1. `zp`: input 행은 38 dB로 정상인데 conv1부터 −15 dB. 입력 텐서 자체(캡처된 값)는 맞고, **그 입력을 소비하는 첫 레이어가 해석을 잘못**했다는 뜻이다. SQNR이 음수라는 것은 오차 전력이 신호보다 크다는 뜻 — 거의 쓰레기 값이다.
2. `layout`: conv1은 정상, conv2부터 2 dB. conv1과 conv2 사이의 버퍼 해석이 원인이다. layout 버그는 값의 "모음"은 같고 "배치"만 다르므로, 해당 텐서의 히스토그램은 기준과 똑같다 — 분포만 보면 못 잡고 원소별 비교에서만 잡힌다.
3. `chscale`: **정확도는 96.0% → 95.7%로 거의 그대로**다. 과제 지표만 봤다면 통과시켰을 버그다. 그러나 conv3의 SQNR이 37.7 → 26.8 dB로 11 dB 떨어졌다. 채널별로 쪼개면, 기준선 대비 변화가 채널 7만 −31.4 dB이고 나머지는 0이다. 그런데 채널 SQNR의 **절대값**만 보면 채널 30도 12.7 dB로 나쁘다 — 채널 30은 거의 죽은 채널(신호 전력 1.6e−5)이라 원래 SQNR이 의미가 없다. 그래서 **절대값이 아니라 정상 기준선 대비 변화**를 본다.

### 4.5 root-cause 워크플로 정리

```
1. 같은 입력(golden vector)으로 기준 모델과 대상의 모든 중간 텐서를 dump
2. 이름 정렬: fusion·rename·layout 변환을 반영한 매핑 표 작성 (conv1 ↔ relu1 같은 것)
3. 레이어별 SQNR / cosine 표 → 정상 기준선(같은 툴체인의 정상 빌드, 또는 fake-quant 시뮬레이션)과 비교
4. 처음으로 기준선 대비 크게(예: 10 dB 이상) 떨어진 레이어 L을 찾는다
5. L의 입력은 맞는가?  ── 아니오 ──▶ L−1 과 L 사이의 버퍼 해석(layout, zp, scale, dtype) 문제
        │ 예
        ▼
6. L 하나만 떼어 커널 unit test (5절 golden vector) ──▶ 커널 버그 / 파라미터(scale, zp, M0, shift) 버그
7. 채널별·위치별로 쪼개서 패턴 확인 (한 채널? 가장자리 padding? 특정 값 범위에서 saturation?)
```

Don의 bring-up 경험과 같은 원칙이다: **증상이 처음 나타나는 경계를 찾고, 그 경계의 입력과 출력을 따로 검증한다.**

---

## 5. Golden vector — 기준 모델에서 테스트 벡터를 만들어 펌웨어로

### 5.1 golden vector란

golden vector는 "이 입력을 넣으면 이 출력이 나와야 한다"는 쌍이다. factory test의 기대값 테이블, silicon validation의 test vector와 같다. ML에서는 기준 모델(보통 정수 시뮬레이션 또는 레퍼런스 커널)로 만든다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="c8m5" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<rect x="10" y="30" width="150" height="56" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="85" y="53" font-size="13" text-anchor="middle">PyTorch 모델</text><text x="85" y="72" font-size="12" text-anchor="middle">+ calib · 양자화 파라미터</text>
<rect x="210" y="30" width="160" height="56" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="290" y="53" font-size="13" text-anchor="middle">numpy 정수 참조</text><text x="290" y="72" font-size="12" text-anchor="middle">gen_golden.py</text>
<line x1="160" y1="58" x2="208" y2="58" stroke="currentColor" stroke-width="1.5" marker-end="url(#c8m5)"/>
<rect x="420" y="10" width="120" height="34" rx="5" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="480" y="32" font-size="12" text-anchor="middle">.npy (host용)</text>
<rect x="420" y="52" width="120" height="34" rx="5" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="480" y="74" font-size="12" text-anchor="middle">.h (펌웨어용)</text>
<rect x="420" y="94" width="120" height="34" rx="5" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="480" y="116" font-size="12" text-anchor="middle">tolerance .json</text>
<line x1="370" y1="50" x2="418" y2="27" stroke="currentColor" stroke-width="1.2" marker-end="url(#c8m5)"/><line x1="370" y1="62" x2="418" y2="69" stroke="currentColor" stroke-width="1.2" marker-end="url(#c8m5)"/><line x1="370" y1="74" x2="418" y2="111" stroke="currentColor" stroke-width="1.2" marker-end="url(#c8m5)"/>
<rect x="210" y="170" width="160" height="56" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="290" y="193" font-size="13" text-anchor="middle">C harness (self-test)</text><text x="290" y="212" font-size="12" text-anchor="middle">host 빌드 · 기기 빌드</text>
<rect x="470" y="170" width="190" height="56" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="565" y="193" font-size="13" text-anchor="middle">HIL: host ↔ 기기</text><text x="565" y="212" font-size="12" text-anchor="middle">UART/USB로 벡터 송수신</text>
<line x1="480" y1="86" x2="300" y2="168" stroke="currentColor" stroke-width="1.2" marker-end="url(#c8m5)"/><line x1="480" y1="44" x2="565" y2="168" stroke="currentColor" stroke-width="1.2" marker-end="url(#c8m5)"/>
<text x="10" y="150" font-size="12">5.2 생성</text><text x="10" y="200" font-size="12">5.3 C harness</text><text x="10" y="218" font-size="12">7.1 HIL</text>
<text x="10" y="244" font-size="12">초록: 기준 생성 · 파랑: 저장 형식 · 주황: 소비자. 같은 벡터를 host 테스트, 기기 self-test, HIL이 모두 공유한다.</text>
</svg>
```

그림 5 — golden vector의 흐름. 한 번 만든 벡터를 여러 소비자가 공유한다는 점이 중요하다. 벡터 파일은 모델 파일과 같은 버전으로 묶어서 관리한다.

저장 형식은 소비자에 맞춘다.

| 형식 | 소비자 | 장점 | 주의 |
|---|---|---|---|
| `.npy` | host 테스트 (Python) | dtype·shape 보존, 로드 한 줄 | 펌웨어에서 직접 읽기 번거로움 |
| raw `.bin` + 헤더 구조체 | 기기 파일시스템, HIL 스트리밍 | 크기 작음, C에서 `fread` | endianness·정렬·버전 필드를 명시 |
| C 헤더 (`static const` 배열) | 기기 self-test, 펌웨어 unit test | 빌드에 포함, 로더 불필요 | flash 사용량, 큰 모델은 레이어 일부만 |
| tolerance `.json` | 모든 비교 코드 | 합격 기준을 코드 밖에서 버전 관리 | 기준 변경은 리뷰 대상으로 |

### 5.2 생성 — conv1 레이어의 int8 golden

실제 모델의 conv1(6→16채널, kernel 5, padding 2, ReLU)을 TFLite 방식 정수 규격으로 양자화한다: 입력은 int8 비대칭(scale, zero-point), weight는 채널별 대칭 int8, bias는 int32(scale = s_x·s_w), 출력은 ReLU 범위 [0, max]를 int8 [−128, 127]에 매핑(zp = −128). 주의할 점 하나: padding은 "실수 0"이어야 하므로 정수 영역에서는 **zero-point 값으로 채운다**(zp를 뺀 뒤 0).

```python
# gen_golden.py — conv1 레이어의 int8 golden vector를 만들어 npy + C 헤더 + tolerance 파일로 저장
import numpy as np, json
from c8_model import load, make_data
from c8_int import quantize_multiplier, requant
P = {k: v.numpy().astype(np.float64) for k, v in load().state_dict().items()}
Xc, _, _ = make_data(20, range(20), seed=3)
s_x = (Xc.max() - Xc.min()) / 255; zp_x = int(np.round(-128 - Xc.min() / s_x))   # int8 asym input
w = P["conv1.weight"]; s_w = np.abs(w).reshape(16, -1).max(1) / 127
w_q = np.round(w / s_w[:, None, None]).astype(np.int8)                     # [16,6,5] per-channel
b_q = np.round(P["conv1.bias"] / (s_x * s_w)).astype(np.int32)
y_f = np.maximum(np.einsum("nclk,ock->nol", np.lib.stride_tricks.sliding_window_view(
      np.pad(Xc, ((0, 0), (0, 0), (2, 2))), 5, axis=2), w) + P["conv1.bias"][None, :, None], 0)
s_y = y_f.max() / 255; zp_y = -128                                         # ReLU 출력: [0, max]
M0, sh = quantize_multiplier(s_x * s_w / s_y)
X, _, _ = make_data(1, range(20, 24), seed=7)                              # golden 입력 4개
x_q = np.clip(np.round(X / s_x) + zp_x, -128, 127).astype(np.int8)
xp = np.pad(x_q.astype(np.int64) - zp_x, ((0, 0), (0, 0), (2, 2)))        # padding = "실수 0" = zp
acc = np.einsum("nclk,ock->nol", np.lib.stride_tricks.sliding_window_view(xp, 5, axis=2),
                w_q.astype(np.int64)) + b_q[None, :, None]
y_q = requant(acc, M0[None, :, None], sh[None, :, None], zp_y, lo=zp_y)   # ReLU = clamp at zp_y
np.save("golden_x.npy", x_q); np.save("golden_y.npy", y_q)
json.dump({"layer": "conv1", "mode": "bit_exact", "max_lsb_diff": 0, "zp_x": zp_x, "zp_y": zp_y,
           "s_x": float(s_x), "s_y": float(s_y)}, open("golden_tol.json", "w"), indent=1)
def arr(name, a, ct): return f"static const {ct} {name}[{a.size}] = {{{','.join(map(str, a.ravel()))}}};\n"
with open("golden_conv1.h", "w") as f:
    f.write(f"#define N_VEC {len(X)}\n#define ZP_X {zp_x}\n#define ZP_Y {zp_y}\n")
    f.write(arr("W", w_q, "int8_t") + arr("B", b_q, "int32_t") + arr("M0", M0, "int32_t")
            + arr("SHIFT", sh, "int32_t") + arr("GX", x_q, "int8_t") + arr("GY", y_q, "int8_t"))
print("s_x=%.5f zp_x=%d s_y=%.5f  M0[0]=%d shift[0]=%d" % (s_x, zp_x, s_y, M0[0], sh[0]))
print("golden x", x_q.shape, x_q.dtype, " y", y_q.shape, " y[0,0,:8] =", y_q[0, 0, :8])
print("header bytes:", len(open("golden_conv1.h").read()), " zero outputs(ReLU):", np.mean(y_q == zp_y).round(3))
```

```text
s_x=0.03735 zp_x=7 s_y=0.03113  M0[0]=1647684885 shift[0]=-8
golden x (4, 6, 50) int8  y (4, 16, 50)  y[0,0,:8] = [-128 -128 -128  -81  -92 -110 -110 -128]
header bytes: 21241  zero outputs(ReLU): 0.49
```

생성된 tolerance 파일과 C 헤더 앞부분(긴 배열 줄은 100자에서 `…`로 잘랐다):

```text
{
 "layer": "conv1",
 "mode": "bit_exact",
 "max_lsb_diff": 0,
 "zp_x": 7,
 "zp_y": -128,
 "s_x": 0.03735431283712387,
 "s_y": 0.031130165912722024
}
#define N_VEC 4
#define ZP_X 7
#define ZP_Y -128
static const int8_t W[480] = {72,48,-75,-120,-102,93,43,-28,-101,-75,80,42,-96,-127,-127,77,86,109,3 …
static const int32_t B[16] = {-1241,-583,-1177,1580,-1039,1037,2885,3177,1056,298,-1514,1252,38,-490 …
static const int32_t M0[16] = {1647684885,1688103746,1915182105,2060680933,1543992251,2134147001,184 …
static const int32_t SHIFT[16] = {-8,-8,-8,-8,-8,-8,-8,-8,-8,-8,-8,-8,-8,-8,-8,-7};
static const int8_t GX[1200] = {13,13,6,12,-12,-3,17,-24,-16,23,31,28,-3,3,7,-45,-19,-39,-12,-16,18, …
static const int8_t GY[3200] = {-128,-128,-128,-81,-92,-110,-110,-128,-128,-128,-128,-105,-86,-106,- …
```

출력에서 볼 것: 출력의 49%가 −128(= ReLU로 잘린 0)이다. golden vector를 고를 때는 이렇게 **saturation·clamp 경로를 실제로 타는 입력**이 섞여야 커버리지가 생긴다. 모두 0이 나오는 입력 네 개로는 requant 버그를 못 잡는다.

### 5.3 C harness — 로드, 실행, 비교

펌웨어 쪽 커널과 비교 로직이다. `-DBUG_TRUNC`를 주면 흔한 구현 실수(반올림 없이 shift만 하는 requant)로 빌드된다.

```c
/* harness.c — golden vector로 int8 conv1d(6→16, k5, pad2) 커널을 검증한다 */
#include <stdio.h>
#include <stdlib.h>
#include "qkernel.h"
#include "golden_conv1.h"
#define CI 6
#define CO 16
#define L 50
#define K 5
#define PAD 2

static void conv1d_s8(const int8_t *x, int8_t *y) {
    for (int o = 0; o < CO; o++)
        for (int t = 0; t < L; t++) {
            int32_t acc = B[o];
            for (int c = 0; c < CI; c++)
                for (int k = 0; k < K; k++) {
                    int i = t + k - PAD;
                    if (i < 0 || i >= L) continue;          /* padding: (x - zp) = 0 */
                    acc += (int32_t)(x[c * L + i] - ZP_X) * W[(o * CI + c) * K + k];
                }
#ifdef BUG_TRUNC                                             /* 흔한 버그: 반올림 없이 shift */
            int32_t v = (int32_t)(((int64_t)acc * M0[o]) >> 31) >> -SHIFT[o];
            v += ZP_Y; y[o * L + t] = (int8_t)(v < ZP_Y ? ZP_Y : (v > 127 ? 127 : v));
#else
            y[o * L + t] = requant(acc, M0[o], SHIFT[o], ZP_Y, ZP_Y, 127);
#endif
        }
}

#ifndef HIL_DEVICE
int main(void) {
    int8_t y[CO * L];
    int fails = 0, max_diff = 0;
    for (int n = 0; n < N_VEC; n++) {
        conv1d_s8(&GX[n * CI * L], y);
        int bad = 0;
        for (int i = 0; i < CO * L; i++) {
            int d = abs(y[i] - GY[n * CO * L + i]);
            if (d) bad++;
            if (d > max_diff) max_diff = d;
        }
        printf("vec %d: %s (%d/%d mismatches)\n", n, bad ? "FAIL" : "PASS", bad, CO * L);
        fails += bad != 0;
    }
    printf("bit-exact: %d/%d vectors pass, max |diff| = %d LSB, within +-1 LSB: %s\n",
           N_VEC - fails, N_VEC, max_diff, max_diff <= 1 ? "yes" : "no");
    return fails != 0;
}
#endif
```

```sh
cc -std=c11 -Wall -Wextra -O2 harness.c -o harness -lm && ./harness
cc -std=c11 -Wall -Wextra -O2 -DBUG_TRUNC harness.c -o harness_bug -lm && ./harness_bug
```

```text
vec 0: PASS (0/800 mismatches)
vec 1: PASS (0/800 mismatches)
vec 2: PASS (0/800 mismatches)
vec 3: PASS (0/800 mismatches)
bit-exact: 4/4 vectors pass, max |diff| = 0 LSB, within +-1 LSB: yes
--- BUG_TRUNC 빌드 ---
vec 0: FAIL (221/800 mismatches)
vec 1: FAIL (212/800 mismatches)
vec 2: FAIL (188/800 mismatches)
vec 3: FAIL (222/800 mismatches)
bit-exact: 0/4 vectors pass, max |diff| = 1 LSB, within +-1 LSB: yes
```

출력에서 볼 것: 정상 빌드는 4개 벡터 모두 bit-exact다(프로세스 종료 코드 0 → CI가 PASS로 인식). BUG_TRUNC 빌드는 출력의 약 26%(843/3200)가 틀렸는데 **최대 차이는 1 LSB**다. 만약 합격 기준을 "±1 LSB 허용"으로 잡았다면 이 버그는 통과했다(`within +-1 LSB: yes`). 반올림 대신 버림을 하면 오차가 항상 한쪽(음의 방향)으로 치우치고, 이런 편향은 레이어를 지나며 누적된다. **정수 커널 대 정수 레퍼런스는 bit-exact로 묶어야 하는 이유**가 이것이다.

(참고: 버그 경로의 `>>`는 음수에 대해 구현 정의 동작(implementation-defined)이다. arm64 clang은 산술 shift를 한다. 정상 경로의 `rdbpot`도 음수 `>>`를 쓰는데, 이는 TFLite·gemmlowp 레퍼런스 코드가 산술 shift를 전제로 하는 것과 같다.)

### 5.4 golden vector 설계 체크

- 개수보다 **다양성**: 클래스별 대표 입력, 0 입력, 최대 진폭(saturation 유도), 경계 근처(패딩이 영향을 주는 짧은 입력), 랜덤.
- 레이어 단위 golden(커널 unit test용)과 모델 단위 golden(end-to-end용)을 둘 다 둔다. end-to-end가 틀리면 레이어 golden으로 내려간다.
- 파일에 **모델 해시·양자화 파라미터 버전·생성 스크립트 커밋**을 같이 기록한다. 모델만 바꾸고 golden을 안 바꾸면 "가짜 FAIL"이, golden을 대충 재생성하면 "가짜 PASS"가 난다.
- float golden(기준 모델의 float 출력)도 같이 저장해 두면 기기 출력의 SQNR을 바로 계산할 수 있다.

---

## 6. 정확도 회귀 테스트 — "모델을 바꿔도 되는가"를 통계로 판정

### 6.1 규칙 — 무엇을 고정하나

모델 업데이트(재학습, 양자화 방식 변경, 컴파일러 버전 업)마다 돌리는 테스트다. SSD 펌웨어 릴리스의 성능 회귀 테스트와 같은 역할이다.

- **같은 테스트 세트**: 버전을 매긴 고정 세트. 학습·calibration에 쓰지 않은 사용자들(A4의 사용자별 분할). 테스트 세트를 바꾸면 이전 결과와 비교할 수 없다.
- **같은 전처리**: 학습 때와 정확히 같은 정규화·윈도잉·특징 추출. 가능하면 **기기 펌웨어의 전처리 코드로 만든 입력**을 쓴다(8절 1번 항목).
- **샘플별 결과를 저장**: 정확도 숫자 하나가 아니라 샘플별 정답 여부를 저장해야 paired 비교와 breakdown이 된다.
- **breakdown**: 전체, 클래스별, 사용자별(A4). 평균이 같아도 한 클래스·한 사용자가 무너질 수 있다.
- **합격 기준을 미리 정한다**: 예를 들어 "전체 top-1 drop의 95% 신뢰구간 상한 ≤ 0.5%p, 어떤 클래스도 drop > 2%p 없음". 결과를 보고 기준을 정하면 안 된다.

### 6.2 신뢰구간 — 0.13%p drop은 진짜인가

테스트 3000개에서 정확도가 0.13%p 떨어졌다면 그건 4개 샘플 차이다. 이게 진짜 열화인지 우연인지 판단하려면 신뢰구간이 필요하다(A2). 가장 쉬운 방법이 **paired bootstrap**이다.

```
1. 테스트 세트에서 n개를 복원 추출(같은 샘플이 여러 번 뽑혀도 됨)
2. 뽑힌 샘플들에서 float 정확도 − int8 정확도를 계산   ← 두 모델에 같은 인덱스 사용 (paired)
3. 1~2를 B번(예: 2000) 반복 → drop 값 B개의 분포
4. 2.5 백분위 ~ 97.5 백분위 = 95% 신뢰구간
```

말로 하면, "테스트 세트를 여러 번 새로 뽑았다면 drop이 얼마나 흔들렸을까"를 컴퓨터로 흉내 내는 것이다. paired로 해야 하는 이유: 두 모델이 대부분 같은 샘플에서 맞고 틀리므로, 같은 인덱스로 뽑으면 공통 변동이 상쇄되어 구간이 훨씬 좁아진다.

### 6.3 코드 — breakdown + bootstrap + 판정

정상 int8과 나쁜 calibration int8 두 후보를 같은 절차로 판정한다.

```python
# 정확도 회귀 테스트: 전체 / 클래스별 / 사용자별 + paired bootstrap 신뢰구간 + 합격 판정
import torch, numpy as np
from c8_model import load, make_data, quantize_ptq, CLASSES
m = load()
X, y, u = make_data(500, range(20, 26), seed=2)                 # 고정된 test set (3000개)
Xc, _, _ = make_data(20, range(20), seed=3)
def correct(model):
    with torch.no_grad(): return model(torch.from_numpy(X)).argmax(1).numpy() == y
cf = correct(m)                                                 # 샘플별 정답 여부 (baseline)
rng = np.random.default_rng(0)
idx = rng.integers(0, len(y), (2000, len(y)))                   # paired bootstrap: 같은 인덱스로 재표본
for name, cand in [("int8", quantize_ptq(m, Xc)), ("int8-badcal", quantize_ptq(m, 0.25 * Xc))]:
    cq = correct(cand)
    print(f"== {name}: float={cf.mean():.4f} cand={cq.mean():.4f} drop={100*(cf.mean()-cq.mean()):+.2f}%p")
    drops = [cf[y == c].mean() - cq[y == c].mean() for c in range(4)]
    print("   class drop %p:", {CLASSES[c]: float(round(100 * d, 2)) for c, d in enumerate(drops)})
    print("   user  drop %p:", {int(s): float(round(100 * (cf[u == s].mean() - cq[u == s].mean()), 2)) for s in np.unique(u)})
    print("   flips: O→X", np.sum(cf & ~cq), " X→O", np.sum(~cf & cq))
    diff = cf[idx].mean(1) - cq[idx].mean(1)
    lo, hi = np.percentile(diff, [2.5, 97.5])
    ok = hi <= 0.005 and max(drops) <= 0.02                     # 기준: CI 상한 ≤0.5%p, 클래스 drop ≤2%p
    print(f"   drop 95% CI = [{100*lo:.3f}, {100*hi:.3f}] %p →", "ACCEPT" if ok else "REJECT")
    if name == "int8": np.save("boot_diff.npy", diff)
```

```text
== int8: float=0.9663 cand=0.9650 drop=+0.13%p
   class drop %p: {'idle': -0.13, 'walk': 0.0, 'raise': 0.54, 'shake': 0.13}
   user  drop %p: {20: 0.0, 21: 0.0, 22: 0.8, 23: -0.2, 24: 0.0, 25: 0.2}
   flips: O→X 5  X→O 1
   drop 95% CI = [-0.001, 0.300] %p → ACCEPT
== int8-badcal: float=0.9663 cand=0.8350 drop=+13.13%p
   class drop %p: {'idle': -0.39, 'walk': 7.29, 'raise': 41.14, 'shake': 5.44}
   user  drop %p: {20: 7.8, 21: 10.4, 22: 17.4, 23: 11.4, 24: 20.2, 25: 11.6}
   flips: O→X 397  X→O 3
   drop 95% CI = [11.900, 14.367] %p → REJECT
```

```svg
<svg viewBox="0 0 640 310" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="250" x2="590" y2="250" stroke="currentColor"/> <line x1="70" y1="250" x2="70" y2="30" stroke="currentColor"/> <line x1="102.5" y1="250" x2="102.5" y2="255" stroke="currentColor"/><text x="102.5" y="269" font-size="12" text-anchor="middle">-0.2</text> <line x1="167.5" y1="250" x2="167.5" y2="255" stroke="currentColor"/><text x="167.5" y="269" font-size="12" text-anchor="middle">-0.1</text> <line x1="232.5" y1="250" x2="232.5" y2="255" stroke="currentColor"/><text x="232.5" y="269" font-size="12" text-anchor="middle">+0.0</text> <line x1="297.5" y1="250" x2="297.5" y2="255" stroke="currentColor"/><text x="297.5" y="269" font-size="12" text-anchor="middle">+0.1</text> <line x1="362.5" y1="250" x2="362.5" y2="255" stroke="currentColor"/><text x="362.5" y="269" font-size="12" text-anchor="middle">+0.2</text> <line x1="427.5" y1="250" x2="427.5" y2="255" stroke="currentColor"/><text x="427.5" y="269" font-size="12" text-anchor="middle">+0.3</text> <line x1="492.5" y1="250" x2="492.5" y2="255" stroke="currentColor"/><text x="492.5" y="269" font-size="12" text-anchor="middle">+0.4</text> <line x1="557.5" y1="250" x2="557.5" y2="255" stroke="currentColor"/><text x="557.5" y="269" font-size="12" text-anchor="middle">+0.5</text> <line x1="65" y1="250.0" x2="70" y2="250.0" stroke="currentColor"/><text x="61" y="254.0" font-size="12" text-anchor="end">0</text> <line x1="65" y1="187.1" x2="70" y2="187.1" stroke="currentColor"/><text x="61" y="191.1" font-size="12" text-anchor="end">100</text> <line x1="65" y1="124.3" x2="70" y2="124.3" stroke="currentColor"/><text x="61" y="128.3" font-size="12" text-anchor="end">200</text> <line x1="65" y1="61.4" x2="70" y2="61.4" stroke="currentColor"/><text x="61" y="65.4" font-size="12" text-anchor="end">300</text>
<rect x="93.8" y="249.4" width="17.3" height="0.6" fill="#888"/> <rect x="137.4" y="249.4" width="17.3" height="0.6" fill="#888"/> <rect x="158.8" y="249.4" width="17.3" height="0.6" fill="#888"/> <rect x="180.3" y="240.6" width="17.3" height="9.4" fill="#888"/> <rect x="202.4" y="229.9" width="17.3" height="20.1" fill="#888"/> <rect x="223.8" y="204.7" width="17.3" height="45.3" fill="#4a7bd0"/> <rect x="245.3" y="163.3" width="17.3" height="86.7" fill="#4a7bd0"/> <rect x="267.4" y="77.1" width="17.3" height="172.9" fill="#4a7bd0"/> <rect x="288.8" y="40.7" width="17.3" height="209.3" fill="#4a7bd0"/> <rect x="310.3" y="41.3" width="17.3" height="208.7" fill="#4a7bd0"/> <rect x="332.4" y="72.7" width="17.3" height="177.3" fill="#4a7bd0"/> <rect x="353.8" y="113.0" width="17.3" height="137.0" fill="#4a7bd0"/> <rect x="375.3" y="163.9" width="17.3" height="86.1" fill="#4a7bd0"/> <rect x="397.4" y="190.9" width="17.3" height="59.1" fill="#4a7bd0"/>
<rect x="418.8" y="224.9" width="17.3" height="25.1" fill="#4a7bd0"/> <rect x="440.3" y="238.7" width="17.3" height="11.3" fill="#888"/> <rect x="462.4" y="246.9" width="17.3" height="3.1" fill="#888"/> <rect x="483.8" y="248.1" width="17.3" height="1.9" fill="#888"/> <rect x="505.3" y="248.1" width="17.3" height="1.9" fill="#888"/> <line x1="231.8" y1="250" x2="231.8" y2="30" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 3"/> <line x1="427.5" y1="250" x2="427.5" y2="30" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5 3"/> <line x1="557.5" y1="250" x2="557.5" y2="30" stroke="#d0564a" stroke-width="2"/> <text x="330" y="292" font-size="13" text-anchor="middle">정확도 drop (float − int8), %p — bootstrap 2000회</text> <text x="20" y="140" font-size="13" text-anchor="middle" transform="rotate(-90 20 140)">횟수</text> <text x="420" y="50" font-size="12">주황 점선: 95% CI [−0.00, +0.30]</text> <text x="420" y="68" font-size="12">빨강 실선: 허용 한계 +0.5%p</text> <text x="420" y="86" font-size="12">CI 상한 &lt; 한계 → ACCEPT</text>
</svg>
```

그림 6 — 정상 int8 후보의 bootstrap drop 분포(실제 2000회). 테스트 세트가 3000개라 drop은 1/3000 = 0.033%p 단위로만 나온다. 95% 구간(주황)이 허용 한계(빨강, +0.5%p) 안에 완전히 들어오므로 ACCEPT.

출력에서 볼 것은 네 가지다.

1. 정상 int8: drop 0.13%p, 95% CI [−0.001, 0.300]%p. 상한이 0.5%p 아래라 ACCEPT. 가장 나쁜 클래스(raise)도 0.54%p.
2. **flips**: float은 맞고 int8은 틀린 샘플 5개, 반대 1개. 정확도 차이(4개)는 이 둘의 차이다. 회귀 리포트에는 flip된 샘플 ID를 남겨 두면 나중에 그 샘플만 모아 레이어별 diff(4절)를 돌릴 수 있다.
3. 사용자 22의 drop이 0.8%p로 가장 크다. 사용자당 500개라 1 %p도 5개 차이일 뿐이지만, 필드에서 특정 사용자 불만으로 나타나는 것이 이런 패턴이다. 사용자별 신뢰구간은 넓으므로 경고로만 쓰고, 여러 버전에 걸쳐 같은 사용자가 반복되면 조사한다.
4. 나쁜 calibration 후보: 전체 13%p drop인데 클래스별로 보면 raise가 41%p 무너졌고 idle은 오히려 조금 좋아졌다. 평균만 보면 "13% 나빠짐"이지만 실제로는 "손목 들기 기능이 절반 가까이 고장남"이다.

### 6.4 합격 기준 설계

| 기준 | 예시 값 | 왜 |
|---|---|---|
| 전체 drop의 95% CI 상한 | ≤ 0.5%p | 점추정이 아니라 최악 쪽 구간으로 판정 |
| 클래스별 drop | 어떤 클래스도 > 2%p 아님 | 평균에 묻히는 기능 고장 방지 |
| 사용자별 drop | 경고: > 3%p인 사용자 | 표본이 작아 hard fail로는 부적합 |
| top-1 agreement | ≥ 99% | 레이블 오류와 무관한 순수 모델 차이 |
| 운영점 지표 (wake word) | FA/hour, recall을 같은 threshold에서 재측정 | threshold 판정 모델은 확률 이동에 민감 |
| 지연·메모리 (K1·K2) | 이전 대비 +5% 이내 | 정확도만 보는 회귀 테스트는 반쪽 |

숫자는 예시다. 제품팀과 합의하되, **한번 정하면 코드(tolerance 파일)로 고정**하고 변경은 리뷰를 거친다.

### 6.5 wake word — FA/hour를 다시 잰다

wake word나 제스처 트리거처럼 threshold로 판정하는 모델은 정확도보다 **운영점(operating point)** 지표가 중요하다: 음성이 없는 긴 녹음에서 시간당 오작동 수(FA/hour)와 실제 발화의 인식률(recall). int8 변환으로 점수 분포가 조금만 이동해도 FA/hour가 크게 바뀔 수 있다. 아래는 합성 점수로 만든 예시다(점수 분포와 이동량은 가정).

```python
# wake word: int8 변환 후 같은 threshold에서 FA/hour와 recall이 어떻게 바뀌는지 다시 잰다 (합성 점수)
import numpy as np
from scipy.stats import chi2
rng = np.random.default_rng(0)
hours, hop = 24, 0.1                                             # 음성 없는 24시간 녹음, 100 ms마다 점수
neg_f = rng.beta(1.2, 30, int(hours * 3600 / hop))               # float 모델의 negative 점수
pos_f = rng.beta(5, 3, 2000)                                  # 2000개 wake word 발화의 peak 점수
def to_int8(s):                                                  # int8 출력(scale 1/255) + 작은 bias
    return np.clip(np.round((s + 0.008 + 0.01 * rng.standard_normal(s.shape)) * 255), 0, 255) / 255
neg_q, pos_q = to_int8(neg_f), to_int8(pos_f)
def fa_per_hour(s, th, refractory=10):                          # 연속 초과는 1회로, 1 s 불응기
    n, last = 0, -10**9
    for i in np.flatnonzero(s >= th):
        if i - last > refractory: n += 1
        last = i
    return n
th = 0.30
for name, neg, pos in [("float", neg_f, pos_f), ("int8", neg_q, pos_q)]:
    n = fa_per_hour(neg, th)
    lo, hi = chi2.ppf(0.025, 2 * n) / 2, chi2.ppf(0.975, 2 * n + 2) / 2   # Poisson 정확 95% CI
    print(f"{name:5s} th={th}: FA={n:3d} → {n/hours:.2f}/h (95% CI {lo/hours:.2f}–{hi/hours:.2f}), recall={np.mean(pos >= th):.3f}")
for t in np.arange(0.30, 0.40, 0.005):                           # int8에서 FA/h를 float 수준으로 되돌리는 threshold
    if fa_per_hour(neg_q, t) <= fa_per_hour(neg_f, th):
        print(f"int8 retuned th={t:.3f}: FA={fa_per_hour(neg_q, t)/hours:.2f}/h, recall={np.mean(pos_q >= t):.3f}"); break
```

```text
float th=0.3: FA= 43 → 1.79/h (95% CI 1.30–2.41), recall=0.962
int8  th=0.3: FA= 58 → 2.42/h (95% CI 1.84–3.12), recall=0.967
int8 retuned th=0.310: FA=1.58/h, recall=0.960
```

출력에서 볼 것: 같은 threshold 0.30에서 FA/hour가 1.79 → 2.42로 35% 늘었다. 정확도 표로는 거의 안 보이는 변화다. FA 수는 Poisson 분포를 따르므로 43건이면 95% 구간이 1.30~2.41/h로 넓다 — **FA/hour를 비교하려면 수십 시간 이상의 negative 녹음**이 필요하다. threshold를 0.310으로 다시 맞추면 FA는 1.58/h로 돌아오고 recall 손실은 0.2%p다. 모델을 바꾸면 threshold도 다시 튜닝하고, 그 threshold를 모델과 **같은 버전으로 묶어** 배포한다.

---

## 7. 온디바이스 검증 — HIL, 결정성, 실기기 데이터, CI

### 7.1 HIL 루프 — host가 벡터를 보내고 기기가 답한다

HIL(hardware-in-the-loop)은 실제 기기에서 추론을 돌리고 host PC가 입력을 공급·결과를 비교하는 구성이다. Don의 factory test 스테이션과 같다: 치구(fixture) PC가 명령을 보내고, DUT가 응답하고, PC가 합격 여부를 판정한다. 프로토콜은 단순하게 — sync byte, 길이, payload, CRC.

여기서는 UART 대신 파이프(stdin/stdout)로 "기기" 프로그램과 통신한다. 기기 쪽은 5.3절의 커널을 그대로 재사용한다(`harness.c` 끝의 `main`은 `#ifndef HIL_DEVICE`로 감싸 두었다).

```c
/* device.c — "기기" 쪽 HIL 루프: 프레임 수신 → 추론 → 프레임 송신 (stdin/stdout = UART 대용) */
#define HIL_DEVICE
#include "harness.c"                       /* conv1d_s8 커널과 가중치를 그대로 재사용 */

static uint8_t crc8(const uint8_t *p, int n) {          /* CRC-8, poly 0x07 */
    uint8_t c = 0;
    while (n--) { c ^= *p++; for (int i = 0; i < 8; i++) c = (uint8_t)(c & 0x80 ? (c << 1) ^ 0x07 : c << 1); }
    return c;
}
static int get(void) { int c = getchar(); if (c == EOF) exit(0); return c; }

int main(void) {
    uint8_t in[CI * L], out[CO * L];
    for (;;) {
        while (get() != 0xA5) { }                          /* sync byte */
        int len = get(); len |= get() << 8;
        if (len != CI * L) continue;
        for (int i = 0; i < len; i++) in[i] = (uint8_t)get();
        uint8_t ok = (uint8_t)get() == crc8(in, len);
        conv1d_s8((const int8_t *)in, (int8_t *)out);
        putchar(0xA5); putchar(ok);                         /* 상태: 1 = CRC OK */
        fwrite(out, 1, CO * L, stdout); putchar(crc8(out, CO * L));
        fflush(stdout);
    }
}
```

host 쪽 러너. 실제 기기라면 `subprocess` 대신 `pyserial`로 포트를 연다.

```python
# host 쪽 HIL 러너: golden 입력을 프레임으로 보내고, 기기 응답을 golden 출력과 비교 + 반복 실행 결정성 확인
import numpy as np, subprocess, struct, hashlib
def crc8(b):
    c = 0
    for x in b:
        c ^= x
        for _ in range(8): c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c
gx, gy = np.load("golden_x.npy"), np.load("golden_y.npy")
dev = subprocess.Popen(["./device"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)  # 실제로는 pyserial
def infer(x, corrupt=False):
    p = x.tobytes(); crc = crc8(p) ^ (0xFF if corrupt else 0)
    dev.stdin.write(b"\xA5" + struct.pack("<H", len(p)) + p + bytes([crc])); dev.stdin.flush()
    hdr = dev.stdout.read(2); body = dev.stdout.read(800); rcrc = dev.stdout.read(1)[0]
    assert hdr[0] == 0xA5 and rcrc == crc8(body), "link error"
    return hdr[1], np.frombuffer(body, np.int8).reshape(16, 50)
hashes = set()
for run in range(3):                                   # 같은 입력을 3번 돌려 결정성 확인
    outs = [infer(x)[1] for x in gx]
    hashes.add(hashlib.sha1(np.stack(outs).tobytes()).hexdigest()[:12])
    print(f"run {run}: bit-exact {sum(np.array_equal(o, g) for o, g in zip(outs, gy))}/{len(gx)}")
print("determinism: unique output hashes =", hashes)
status, _ = infer(gx[0], corrupt=True)
print("corrupted frame → device CRC status =", status, "(0 = 기기가 오류를 보고)")
dev.stdin.close(); dev.wait()
```

```sh
cc -std=c11 -Wall -Wextra -O2 device.c -o device -lm && .venv/bin/python ex13.py
```

```text
run 0: bit-exact 4/4
run 1: bit-exact 4/4
run 2: bit-exact 4/4
determinism: unique output hashes = {'4cbc4c3b2edf'}
corrupted frame → device CRC status = 0 (0 = 기기가 오류를 보고)
```

출력에서 볼 것: 세 번 실행 모두 golden과 bit-exact이고, 출력 해시가 하나뿐이다(결정적). 일부러 CRC를 망가뜨린 프레임에는 기기가 상태 0을 돌려준다 — **링크 오류와 추론 오류를 구분**할 수 있어야 HIL 결과를 믿을 수 있다. 실제 기기에서는 여기에 지연 시간(K2)과 전류(K3) 측정을 같은 루프에 붙인다.

### 7.2 결정성 검사

같은 바이너리, 같은 입력이면 출력은 매번 같아야 한다. 다르다면 거의 항상 버그다.

- 초기화 안 된 메모리(arena의 이전 추론 잔여값, 스택 쓰레기) — 첫 실행과 두 번째 실행이 다르면 의심
- DMA와 연산의 경쟁(캐시 flush/invalidate 누락) — 부하에 따라 가끔 다름
- 멀티코어·멀티스레드 reduction 순서 (float) — 스레드 수에 따라 다름
- 상태를 가진 모델(스트리밍 conv, RNN)의 상태 리셋 누락 — 입력 순서에 따라 다름

검사 방법: 같은 벡터를 N번, 다른 벡터 사이에 섞어서, 전원 재투입 후에도 돌리고 해시를 비교한다. Don이 SSD에서 하던 "power cycle 후 데이터 무결성" 테스트와 같은 발상이다.

### 7.3 실기기 데이터 vs 실험실 데이터 — 분포 차이

HIL이 bit-exact여도 제품이 망가질 수 있다. **기기의 센서가 실험실 데이터와 다른 분포를 내면** 기준 모델 자체가 틀린다. 예를 들어 IMU full-scale 설정, 센서 축 방향, 샘플링 레이트, 필터 설정이 데이터 수집 때와 다르면 생긴다. 그래서 기기에서 녹음한 raw 센서 데이터로 입력 통계를 먼저 비교한다.

```python
# 실기기 녹음 vs 실험실 데이터: 입력 분포가 같은지 먼저 확인하고, 정확도 영향도 본다
import torch, numpy as np
from scipy.stats import ks_2samp
from c8_model import load, make_data
m = load()
X_lab, y_lab, _ = make_data(200, range(20, 26), seed=4)
X_dev, y_dev, _ = make_data(200, range(20, 26), seed=5)
X_dev = X_dev.copy(); X_dev[:, 3:] = 1.6 * X_dev[:, 3:] + 0.4     # 기기: 자이로 축 gain/offset이 다르다고 가정
print("ch  lab mean/std     dev mean/std     KS stat")
for c in range(6):
    a, b = X_lab[:, c].ravel(), X_dev[:, c].ravel()
    print(f"{c}  {a.mean():+.2f} / {a.std():.2f}    {b.mean():+.2f} / {b.std():.2f}    {ks_2samp(a, b).statistic:.3f}")
with torch.no_grad():
    for name, X, y in [("lab", X_lab, y_lab), ("device", X_dev, y_dev)]:
        print(f"acc on {name:6s} = {np.mean(m(torch.from_numpy(X)).argmax(1).numpy() == y):.3f}")
```

```text
ch  lab mean/std     dev mean/std     KS stat
0  +0.04 / 0.90    +0.04 / 0.91    0.007
1  -0.04 / 0.91    -0.04 / 0.91    0.004
2  -0.06 / 0.91    -0.06 / 0.91    0.004
3  -0.07 / 0.83    +0.28 / 1.33    0.202
4  +0.15 / 0.84    +0.64 / 1.34    0.233
5  +0.07 / 0.82    +0.50 / 1.33    0.227
acc on lab    = 0.965
acc on device = 0.565
```

출력에서 볼 것: 채널 3~5만 평균·표준편차가 다르고 KS 통계량(두 분포의 누적분포 최대 차이, 0이면 동일)이 0.2 이상이다. 같은 float 모델의 정확도가 96.5% → 56.5%로 무너졌다 — **양자화도 커널도 아닌 입력 분포 문제**다. 이 경우 레이어별 diff는 전혀 도움이 안 된다(기준 모델도 같이 틀리니까). 추가로, 입력 범위가 커지면 calibration 때 정한 입력 scale을 넘어 int8 입력이 saturation되므로 양자화 모델은 더 나빠진다. 입력 채널별 통계 비교는 HIL 첫날에 할 일이다.

### 7.4 CI 통합 (O3로 이어짐)

위의 모든 것을 자동화하면 이런 파이프라인이 된다. 층마다 도는 주기가 다르다.

```
커밋마다 (분 단위)
  ├─ 커널 unit test: golden vector로 bit-exact (host 빌드, 5.3절)
  ├─ 레이어별 diff 표: fake-quant 시뮬레이션 대비 SQNR, 기준선에서 3 dB 이상 하락 시 FAIL
  └─ 결정성: 같은 입력 3회 해시 동일
모델·툴체인 변경마다 (수십 분)
  ├─ 정확도 회귀: 고정 test set, breakdown, bootstrap CI, tolerance 파일로 판정 (6절)
  ├─ wake word 운영점: FA/hour, recall 재측정
  └─ 지연·메모리 회귀 (K1, K2)
야간 / 릴리스 (시간 단위)
  ├─ HIL 실기기 farm: golden 벡터 bit-exact + 지연·전류 (예: 클라우드 실기기 서비스 또는 사내 보드 랙)
  └─ 실기기 녹음 데이터로 입력 분포 검사 + top-1 agreement
```

결과물은 PASS/FAIL 한 줄이 아니라 **리포트**여야 한다: 레이어별 SQNR 표, 클래스·사용자별 drop, flip 샘플 목록, 이전 버전과의 추세. Don이 NPI에서 쓰던 margin sign-off 리포트와 같은 형태다.

---

## 8. 숫자가 안 맞을 때 — 디버깅 체크리스트 (순서대로)

"int8 모델이 NPU에서 PyTorch와 다른 답을 낸다"를 받았을 때 이 순서로 본다. 싸고 흔한 원인부터, 위에서 아래로.

1. **전처리**: 기기에 들어가는 입력 텐서를 dump해서 host 전처리 결과와 원소별로 비교한다. 정규화 상수, 채널 순서(RGB/BGR, 가속도/자이로 축), 단위(g vs m/s², int16 raw vs float), 윈도 길이·hop, 스펙트로그램의 log/epsilon. 경험상 가장 흔한 원인이다.
2. **입력 양자화 파라미터**: 입력 scale·zero-point가 모델 파일의 값과 펌웨어 코드의 값이 같은가. uint8 vs int8(zero-point 128 차이), signed/unsigned 캐스트.
3. **layout**: NCHW vs NHWC, [N, C, L] vs [N, L, C], 채널 padding(8·16 배수 정렬, C6)으로 생긴 stride 차이. 히스토그램은 같고 원소별 비교만 틀리면 layout을 의심.
4. **첫 레이어 출력**: 입력이 맞다면 첫 conv 출력만 비교한다. 여기서 틀리면 weight layout, bias scale, padding 값(zero-point로 채웠나), requant 파라미터.
5. **레이어별 diff**: 4.5절 워크플로로 처음 무너지는 레이어를 찾는다. 기준선 대비 변화로 판정.
6. **커널 unit test**: 그 레이어 하나를 golden vector로 떼어 bit-exact 검사(5.3절). rounding 규격, saturation, 채널별 파라미터 인덱싱.
7. **예상된 차이인지 확인**: 모든 레이어가 기준선과 비슷하게 낮다면 버그가 아니라 양자화 자체의 한계다 → C2의 레이어별 sensitivity 분석, per-channel 양자화, 더 나은 calibration, QAT, 일부 레이어 fp16 유지.

```
증상                                          먼저 볼 곳
─────────────────────────────────────         ─────────────────────────────
정확도가 거의 chance 수준                      1 전처리, 2 입력 양자화
모든 출력이 거의 같은 값 (상수 출력)            2 zero-point, 스케일, saturation
특정 레이어부터 SQNR 0 dB 근처                 3 layout, 4·5 해당 레이어
정확도 조금 하락, 한 클래스만 크게 하락         5 레이어별 → 채널별 diff, 6 파라미터 표
±1 LSB 불일치가 넓게 퍼짐                      6 rounding 규격 (3.4절)
가끔만 다름, 재실행하면 사라짐                  7.2 결정성 (메모리 초기화, 캐시, DMA)
host는 맞는데 실기기 데이터에서만 틀림          7.3 입력 분포 (센서 설정)
```

---

## 9. 임베디드 관점에서 다시 보기

- **검증 코드도 펌웨어 자원을 쓴다.** 기기 self-test용 golden vector는 flash를 먹는다. 위 conv1 golden 4개가 C 헤더로 21 KB였다(텍스트 기준, 바이너리로는 입력 1.2 KB + 출력 3.2 KB + 파라미터 약 0.7 KB). 모델 전체의 golden은 레이어 하나만 넣거나, 출력의 CRC만 저장하는 식으로 줄인다: "입력 X에 대한 출력 텐서의 CRC32 = 0x…"는 bit-exact 검증에 충분하다.
- **중간 텐서 dump는 arena를 건드린다.** 텐서 arena(J1)는 레이어가 끝나면 버퍼를 재사용하므로, dump는 레이어 직후 콜백에서 복사해야 한다. 벤더 런타임의 "디버그 모드"는 버퍼 재사용을 끄므로 메모리 사용량과 타이밍이 달라진다 — 디버그 빌드에서만 재현되는 버그와 반대로 디버그 빌드에서 사라지는 버그(heisenbug)에 주의.
- **HIL 링크 대역폭**: 115200 baud UART는 초당 약 11 KB다. 1초짜리 16 kHz 오디오(int16, 32 KB)를 보내는 데 3초가 걸린다. 벡터가 많으면 USB CDC나 SWD/JTAG 메모리 직접 쓰기(Trace32로 메모리 로드 후 실행 — Don이 익숙한 방식)가 빠르다.
- **공장 테스트와의 연결**: 양산 기기에 "알려진 입력 → 출력 CRC" self-test를 넣으면 NPU·메모리 불량을 factory test에서 잡을 수 있다. Don의 factory test 경험이 바로 쓰이는 지점이다.
- **정수 경로 규격을 문서로 고정한다**: rounding 방식, saturation, zero-point 처리, bias 스케일. 이 문서가 있어야 NPU 벤더 커널과 레퍼런스가 다를 때 "누가 규격을 어겼나"를 판정할 수 있다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| fused 모듈을 원래 이름끼리 비교 | 첫 레이어부터 SQNR이 낮게 나옴 (가짜 버그) | int8 `conv1` = conv+ReLU인데 float `conv1`(ReLU 전)과 짝지음 | fusion 매핑 표를 만들고 ReLU 뒤 값과 비교 |
| float 비교에 bit-exact 요구 | 컴파일러·HW 바뀔 때마다 FAIL | 합산 순서, FMA contraction | ulp/상대 오차 tolerance, SQNR ≥ 100 dB |
| 정수 커널에 ±1 LSB 허용 | 반올림 버그가 통과, 레이어 지날수록 편향 누적 | tolerance가 체계적 오차를 가림 | 정수 대 정수는 bit-exact, 아니면 불일치 비율·평균 오차 조건 추가 |
| max rel error를 합격 기준으로 | 멀쩡한 모델이 FAIL | 0 근처 값으로 나눔 | SQNR, cosine, LSB 단위 max abs 사용 |
| 정확도 숫자 하나로 판정 | 한 클래스·한 사용자 고장을 놓침 | 평균에 묻힘 | 클래스·사용자별 breakdown, flip 샘플 목록 |
| 신뢰구간 없이 0.1%p 차이로 판단 | 버전마다 PASS/FAIL이 뒤집힘 | 표본 잡음 | paired bootstrap CI로 판정 |
| 모델만 바꾸고 threshold 유지 | FA/hour 증가, 사용자 불만 | 점수 분포 이동 | 운영점 재측정·재튜닝, threshold를 모델과 같이 버전 관리 |
| golden vector를 모델과 따로 관리 | 가짜 FAIL 또는 가짜 PASS | 버전 불일치 | 모델 해시·파라미터 버전을 golden 파일에 기록 |
| 채널 SQNR 절대값으로 판단 | 죽은 채널을 버그로 오인 | 신호 전력이 0에 가까움 | 정상 기준선 대비 변화로 판정 |
| 기기 입력 분포 확인 생략 | HIL은 PASS인데 필드 정확도 급락 | 센서 설정·전처리 차이 | 기기 녹음 데이터로 채널별 통계·KS 검사, agreement 측정 |

---

## 11. 면접에서 이렇게 말한다

**Q.** Your int8 model on the NPU disagrees with the PyTorch model. How do you debug it?

**A.** 먼저 "얼마나 다른가"를 정량화한다: 출력 SQNR, top-1 agreement, 과제 정확도. 그리고 싼 원인부터 본다. 기기에 들어간 입력 텐서를 dump해서 host 전처리와 원소별로 비교하고, 입력 scale·zero-point와 layout을 확인한다. 입력이 맞으면 모든 중간 텐서를 dump해서 이름을 맞춘 뒤 레이어별 SQNR 표를 만들고, 정상 기준선보다 처음 크게 떨어지는 레이어를 찾는다. 그 레이어의 입력이 맞으면 커널 문제이므로 golden vector로 떼어 bit-exact 테스트를 하고, 입력부터 틀리면 앞 레이어와의 버퍼 해석(layout, zero-point)을 본다. 모든 레이어가 고르게 낮다면 버그가 아니라 양자화 한계이므로 sensitivity 분석으로 넘어간다.

> "First I quantify the gap — output SQNR, top-1 agreement against the float model, and task accuracy. Then I go from cheap to expensive. I dump the actual input tensor on the device and diff it against the host preprocessing, and check the input scale, zero-point and layout, because that's where most of these bugs live. If the input matches, I dump every intermediate tensor, build a name mapping that accounts for fusion, and compute per-layer SQNR against a known-good baseline. The first layer where it drops sharply tells me where to look: if that layer's input is already wrong, it's a buffer interpretation problem between layers — layout or zero-point; if the input is right, I pull that single layer out and run it against golden vectors bit-exactly. If every layer is uniformly degraded, it's not a bug but quantization error, and I switch to sensitivity analysis. It's the same bisect I used in silicon bring-up."

**Q.** Bit-exact or tolerance — how do you decide?

**A.** 두 구현이 같은 정수 연산 규격(requant rounding, saturation)을 따르면 bit-exact를 요구한다. 정수 덧셈은 결합법칙이 성립해서 SIMD든 루프 순서든 결과가 같아야 하고, 1 LSB라도 다르면 규격 위반이다. float이 끼면 합산 순서와 FMA contraction 때문에 bit-exact가 원래 성립하지 않으므로 ulp나 SQNR 기반 tolerance를 쓴다. 벤더 NPU처럼 rounding 규격이 다른 게 확인된 경우에만 ±1 LSB를 허용하되, 불일치 비율과 평균 오차 조건을 같이 건다. ±1 LSB만 허용하면 항상 한쪽으로 버리는 버그가 통과하기 때문이다.

> "If both sides implement the same integer spec — same requantization rounding and saturation — I require bit-exact. Integer accumulation is associative, so vectorization or loop order can't change the result; a single LSB difference means someone violated the spec. As soon as floating point is involved, summation order and FMA contraction make bit-exactness meaningless, so I use ulp- or SQNR-based tolerances. For a vendor kernel with a documented different rounding mode, I allow plus-or-minus one LSB, but I also bound the fraction of mismatches and the mean error, because a truncation bug is always within one LSB yet biased, and that bias accumulates across layers."

**Q.** What is SQNR and what's a good value?

**A.** 신호 전력 대 양자화 오차 전력의 비를 dB로 나타낸 것이다: 10·log10(∑r² / ∑(t−r)²). 스케일에 무관해서 레이어끼리 비교할 수 있다. 이상적인 8-bit 양자화기는 full-scale 사인파에서 약 50 dB지만 실제 int8 activation은 보통 30~40 dB다. 20 dB 아래나 음수면 거의 확실히 버그나 심한 outlier다. 다만 절대값보다 같은 모델의 정상 빌드 대비 변화를 보는 게 더 믿을 만하고, 과제 지표와 반드시 같이 본다 — 채널 하나의 scale 버그는 정확도는 거의 그대로인데 레이어 SQNR만 11 dB 떨어뜨린다.

> "Signal-to-quantization-noise ratio: ten log ten of the reference signal power over the error power. Because it's scale-invariant, I can put every layer in one table. The theoretical number for an ideal 8-bit quantizer with a full-scale sine is about 50 dB; real int8 activations usually land around 30 to 40. Below 20 dB, or negative, almost always means a bug or a severe outlier problem. But I care more about the change versus a known-good baseline than the absolute value, and I always pair it with the task metric, since some bugs barely move accuracy but show up clearly as a drop in one layer's SQNR."

**Q.** How do you build a regression test for a model update?

**A.** 버전을 매긴 고정 test set과 학습과 동일한 전처리(가능하면 기기 전처리 코드)를 쓰고, 샘플별 결과를 저장한다. 전체·클래스별·사용자별 정확도와 flip 샘플을 보고, paired bootstrap으로 drop의 신뢰구간을 구한다. 합격 기준은 미리 tolerance 파일로 고정한다 — 예를 들어 CI 상한 0.5%p, 클래스별 2%p. threshold 모델이면 FA/hour와 recall을 재측정하고, 지연·메모리 회귀도 같이 본다. 아래 층으로는 golden vector bit-exact, 레이어별 SQNR 기준선 비교를 커밋마다 돌린다.

> "I freeze a versioned test set, held out by user, and run it through exactly the training preprocessing — ideally the firmware's own preprocessing code. I store per-sample correctness, not just the accuracy number, so I can report overall, per-class and per-user accuracy, list the flipped samples, and compute a paired bootstrap confidence interval for the drop. The acceptance criteria are written down before the run and live in a tolerance file — for example, the upper bound of the 95% interval under half a percent, and no class dropping more than two points. For trigger-style models I re-measure false accepts per hour and recall at the operating point and retune the threshold. Below that, every commit runs golden-vector bit-exact tests and a per-layer SQNR check against a baseline."

**Q.** The accuracy is the same after quantization. Are you done?

**A.** 아니다. 전체 정확도는 클래스·사용자별 고장을 가리고, flip이 양방향으로 상쇄될 수 있다. top-1 agreement와 flip 샘플을 보고, 클래스·사용자별 drop과 신뢰구간을 본다. threshold 모델이면 운영점을 재측정한다. 그리고 실기기에서 같은 golden vector로 bit-exact인지, 기기 녹음 데이터의 입력 분포가 학습 데이터와 같은지 확인한다. 이 노트의 예에서도 채널 scale 버그는 정확도를 0.3%p만 떨어뜨렸지만 레이어 SQNR은 11 dB 떨어졌다.

> "Not yet. Aggregate accuracy can hide a broken class or user, and flips in both directions can cancel out. I check top-1 agreement and the flipped samples, the per-class and per-user breakdown with confidence intervals, and for threshold-based models the false-accept rate at the operating point. Then I confirm the device is bit-exact on golden vectors and that on-device sensor data matches the training distribution. I've seen a per-channel scale bug that moved accuracy by only 0.3 points while one layer's SQNR dropped by 11 dB — it would have shipped if we only looked at accuracy."

**Q.** How would you set up hardware-in-the-loop testing for an ML model?

**A.** host가 golden vector를 sync·길이·CRC가 있는 프레임으로 UART나 USB로 보내고, 기기는 추론 결과와 상태 코드를 같은 형식으로 돌려준다. host는 golden과 bit-exact 비교를 하고, 같은 벡터를 여러 번·전원 재투입 후에도 돌려 결정성을 확인한다. 링크 오류와 추론 오류를 구분하도록 CRC 상태를 따로 둔다. 같은 루프에서 지연 시간과 전류를 측정하고, 결과를 CI 리포트로 남긴다. factory test 스테이션과 같은 구조다.

> "The host streams golden vectors to the device over UART or USB in framed packets — sync byte, length, payload, CRC — and the device returns the output tensor plus a status code. The host compares bit-exactly against the golden outputs, repeats the same vectors several times and across power cycles to check determinism, and keeps link errors separate from inference errors via the CRC status. The same loop captures latency and current, and everything lands in a CI report. It's essentially the factory test station pattern I've built before, with a model as the DUT."

---

## 12. 직접 해보기

1. (손계산) `r = [1, 2, 3, 4]`, `t = [1, 2, 3, 5]`의 max abs, SQNR(dB), cosine을 구하라.
   정답: max abs 1, SQNR = 10·log10(30/1) ≈ 14.77 dB, cosine = 34/(√30·√39) ≈ 0.9940.
2. (손계산) 어떤 레이어의 SQNR이 30 dB에서 40 dB로 올랐다. 오차 전력은 몇 배가 되었나? 2.3절 근사로 cosine은 대략 얼마에서 얼마로 바뀌나?
   정답: 1/10. cosine ≈ 0.9995 → 0.99995.
3. (손계산) `M = 0.003`, `acc = −5000`일 때 3.4절 규격으로 int8 출력(zp = 0)을 구하라. 힌트: 0.003 = 0.768 × 2⁻⁸.
   정답: 실수 결과 −15.0 → −15. (SRDHM = round(−5000·0.768) = −3840, RDBPOT(−3840, 8) = −15.)
4. (코드) `ex8.py`의 `forward`에 "conv2의 bias를 s_x·s_w가 아니라 s_w로만 스케일한" 버그를 흉내 내는 스위치를 추가하고, 레이어별 SQNR 표에서 어디가 처음 무너지는지 예측한 뒤 확인하라.
   정답/힌트: conv2 행부터 떨어져야 한다. bias는 채널별 상수 offset이므로 채널별 평균 오차(편향)가 크게 나오는 것도 함께 확인.
5. (코드) `harness.c`에 tolerance 모드를 추가해 "불일치 비율 ≤ 0.5% 그리고 평균 오차의 절댓값 ≤ 0.05 LSB"를 판정하게 하라. BUG_TRUNC 빌드가 이 기준에서는 FAIL하는지 확인하라.
   정답/힌트: BUG_TRUNC는 불일치 약 26%이고, 버림은 항상 반올림보다 작거나 같으므로 틀린 원소가 모두 −1 LSB라 평균 오차 약 −0.26 LSB — 둘 다 실패한다.
6. (코드) `ex11.py`에 McNemar 검정(flip 수 5 대 1)을 추가하고 bootstrap 결론과 비교하라.
   정답/힌트: `scipy.stats.binomtest(1, 6, 0.5)`의 양측 p-value는 약 0.22 — 유의한 차이 아님, bootstrap CI가 0을 포함하는 것과 같은 결론.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| golden reference | 기준 구현 | 검증 대상과 비교할 "맞다고 믿는" 모델·커널 (보통 PyTorch float 또는 정수 시뮬레이션) |
| golden vector | 기준 입출력 쌍 | 기준 구현으로 만든 입력과 기대 출력, 허용 기준과 함께 저장 |
| bit-exact | 비트 단위 일치 | 모든 출력 바이트가 같음. 같은 정수 규격을 따르는 구현끼리 기대 |
| tolerance | 허용 오차 | bit-exact가 불가능할 때의 합격 범위 (ulp, LSB, SQNR 등) |
| ulp | unit in the last place | 해당 float 값에서 가수 최하위 비트 하나의 크기 |
| LSB | least significant bit | 정수 양자화에서 한 칸 = scale |
| SQNR | signal-to-quantization-noise ratio | 10·log10(신호 전력 / 오차 전력), dB |
| cosine similarity | 코사인 유사도 | 두 벡터 방향의 일치도, 스케일 오류는 못 잡음 |
| top-1 agreement | 결정 일치율 | 두 모델이 같은 클래스를 고른 비율, 레이블 불필요 |
| KL divergence | 분포 차이 | 확률 분포 P 대비 Q가 얼마나 다른가, 비대칭 |
| FMA | fused multiply-add | a·b + c를 반올림 한 번으로 계산하는 명령 |
| FP contraction | 연산 합치기 | 컴파일러가 곱셈+덧셈을 FMA로 합치는 것 (`-ffp-contract`) |
| requantization | 재양자화 | int32 누산값을 출력 int8로 바꾸는 정수 곱셈+shift |
| forward hook | 순전파 콜백 | 모듈 출력이 나올 때 호출되는 PyTorch 콜백 |
| per-layer diff | 레이어별 비교 | 모든 중간 텐서를 기준과 비교해 오차가 생긴 위치를 찾음 |
| flip | 판정 뒤집힘 | 한 모델은 맞고 다른 모델은 틀린 샘플 |
| paired bootstrap | 짝지은 재표본 | 같은 인덱스로 두 모델을 재표본해 차이의 신뢰구간을 구함 |
| operating point | 운영점 | 배포할 threshold와 그때의 FA/hour·recall |
| HIL | hardware-in-the-loop | 실기기를 테스트 루프에 넣고 host가 입력 공급·결과 판정 |
| determinism | 결정성 | 같은 입력이면 매번 같은 출력 |
| distribution shift | 분포 이동 | 배포 환경 입력 분포가 학습 데이터와 다른 것 |
| KS statistic | Kolmogorov–Smirnov 통계량 | 두 표본의 누적분포 함수 최대 차이 |

---

## 14. 요약 & 체크리스트

최적화한 모델의 검증은 silicon validation처럼 golden reference와 비교하는 일이지만, 정답이 하나가 아니라서 **층마다 다른 비교 방식**을 쓴다. 정수 커널은 같은 requant 규격이면 bit-exact로 묶고, float은 합산 순서·FMA 때문에 tolerance로, float ↔ int8은 SQNR·cosine·top-1 agreement로 품질을 잰다. 숫자가 안 맞으면 전처리 → 입력 양자화 → layout → 첫 레이어 → 레이어별 diff → 커널 unit test 순으로 좁히고, 레이어별 diff에서는 이름 정렬과 정상 기준선 대비 변화가 핵심이다. 마지막 판정은 고정 test set의 과제 지표를 클래스·사용자별로 쪼개고 bootstrap 신뢰구간으로 미리 정한 기준과 비교해 내리며, 실기기에서는 HIL로 bit-exact와 결정성을, 기기 녹음 데이터로 입력 분포를 확인한다.

- [ ] max abs, mean abs, SQNR, cosine을 원소 4개짜리 예제로 손계산할 수 있다
- [ ] SQNR과 cosine의 근사 관계를 쓰고, cosine이 못 잡는 버그(전체 스케일)를 말할 수 있다
- [ ] float 합산 순서와 FMA contraction 때문에 bit-exact가 깨지는 예를 코드로 보일 수 있다
- [ ] requant(M0, shift)를 손으로 계산하고, 정수 커널이 누산 순서와 무관하게 bit-exact인 이유를 설명할 수 있다
- [ ] forward hook과 ONNX 중간 출력으로 레이어별 SQNR 표를 만들고 fusion 때문에 이름을 맞추는 법을 안다
- [ ] 레이어별 표에서 처음 무너지는 레이어로 zero-point·layout·채널 scale 버그를 구분할 수 있다
- [ ] golden vector를 npy·C 헤더·tolerance 파일로 만들고 C harness로 bit-exact 검사를 할 수 있다
- [ ] ±1 LSB tolerance가 반올림 버그를 숨기는 이유를 설명할 수 있다
- [ ] paired bootstrap으로 정확도 drop의 신뢰구간을 구하고 미리 정한 기준으로 판정할 수 있다
- [ ] HIL 루프, 결정성 검사, 기기 데이터 분포 검사를 CI 층에 배치할 수 있다

## 참고 자료

- B. Jacob et al., "Quantization and Training of Neural Networks for Efficient Integer-Arithmetic-Only Inference", CVPR 2018 (arXiv:1712.05877) — 정수 전용 추론과 requantization 규격의 출발점
- M. Nagel et al., "A White Paper on Neural Network Quantization" (arXiv:2106.08295) — PTQ/QAT와 레이어별 디버깅 절차
- gemmlowp — https://github.com/google/gemmlowp (`SaturatingRoundingDoublingHighMul`, `RoundingDivideByPOT`의 원본)
- TensorFlow Lite 8-bit quantization specification (TFLite/LiteRT 문서) — int8 비대칭 activation, 채널별 대칭 weight 규격
- PyTorch Quantization 문서 — https://pytorch.org/docs/stable/quantization.html
- PyTorch `register_forward_hook` — https://pytorch.org/docs/stable/generated/torch.nn.Module.html
- ONNX Runtime 문서 — https://onnxruntime.ai/docs/
- D. Goldberg, "What Every Computer Scientist Should Know About Floating-Point Arithmetic", ACM Computing Surveys, 1991 — 합산 순서와 반올림
- B. Efron, R. Tibshirani, "An Introduction to the Bootstrap", Chapman & Hall, 1993
- MIT 6.5940 TinyML and Efficient Deep Learning (Song Han) — 양자화와 배포 전반
