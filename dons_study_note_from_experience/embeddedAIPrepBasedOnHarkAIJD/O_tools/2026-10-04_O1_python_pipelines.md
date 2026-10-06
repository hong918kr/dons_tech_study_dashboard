# O1. Python 파이프라인 — export·양자화·벤치·리포트를 명령 한 줄로, 몇 번을 돌려도 같은 결과로

> **이 노트를 다 읽으면**: PyTorch 모델 → ONNX → int8 → 지연 측정 → 리포트를 click CLI 하나로 묶고 YAML 설정으로 실험을 바꿀 수 있다 · 입력 내용 hash로 stage를 건너뛰는 make식 캐시와 manifest(버전·git hash·설정)를 직접 만들 수 있다 · 텍스트 로그를 정규식으로, binary telemetry를 `struct`/numpy dtype으로 읽어 DataFrame으로 만들고 "어디서부터 느려졌나"를 숫자로 말할 수 있다 · 외부 툴(벤더 converter, llama-bench)을 timeout·return code 검사와 함께 안전하게 호출할 수 있다
> **JD 연결**: 일상 업무 전반 — "convert, quantize, and benchmark models for on-device deployment" 류의 반복 작업을 자동화하는 능력. study_prep_list **O1** 행: 스크립트로 변환·양자화·벤치 자동화 / CLI 도구, 로그 파싱 (P1, Don 출발점: 🟡)
> **Don 기준 난이도**: SSD 펌웨어 시절 빌드→다운로드→fio 워크로드→로그 파싱→QoS 비교 스크립트를 이미 돌려 봤다 / 새로 배울 것은 ML 산출물(ONNX, calibration, 정확도 지표)이 끼어든 파이프라인의 모양과, 그걸 "재현 가능하게" 만드는 습관(캐시 key, manifest, seed)
> **선행 노트**: C2(PTQ·ORT `quantize_static`·QDQ 해부), F5(ONNX·ORT 구조, 9절 양자화 도구), F3(llama.cpp·`llama-bench`), K2(프로파일링 방법론), C8(최적화 후 검증 지표). 이 노트는 그 이론을 다시 설명하지 않고 **자동화·툴링**만 다룬다. 병행 노트: O2(adb·Android·NPU 드라이버 — 기기 위 실행), O3(ML CI — 회귀 gating·device farm)

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 edge ML 엔지니어의 하루는 "같은 일을 조금씩 바꿔서 또 하기"다

모델 하나를 기기에 올리기까지 같은 순서를 수십 번 반복한다.

1. 학습팀이 준 PyTorch 체크포인트를 ONNX로 **export**한다.
2. calibration 데이터를 넣어 int8로 **quantize**한다 (C2).
3. 정확도가 얼마나 떨어졌는지 **eval**한다 (C8).
4. 지연·메모리를 **bench**한다 (K2).
5. 표로 정리해서 **report**를 팀에 공유한다.

그리고 내일은 calibration 샘플 수를 바꾸고, 모레는 opset을 바꾸고, 다음 주에는 학습팀이 새 체크포인트를 준다. 손으로 하면 매번 어디선가 한 단계를 빼먹거나, "어제 그 숫자가 어떤 설정이었지?"를 아무도 대답하지 못하게 된다.

Don에게 익숙한 그림으로 바꾸면 이렇다. SSD 펌웨어 nightly를 떠올리자: **FW 빌드 → 드라이브에 다운로드 → fio 워크로드 → 로그 수집 → IOPS·QoS(p99.99 latency) 비교 → 메일 리포트**. ML 파이프라인은 여기서 "빌드"가 "export + quantize"로, "fio"가 "추론 벤치 + 정확도 eval"로 바뀐 것뿐이다. 펌웨어에서 "빌드 산출물에 git hash를 박고, 결과에 펌웨어 버전을 같이 적는" 습관이 그대로 필요하다.

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="24" font-size="14">pipe.py run  (click CLI · config.yaml · stage마다 .key 캐시)</text> <rect x="20" y="50" width="90" height="44" rx="6" fill="#888" fill-opacity="0.2" stroke="currentColor"/> <text x="65" y="77" font-size="13" text-anchor="middle">config</text> <rect x="130" y="50" width="90" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/> <text x="175" y="77" font-size="13" text-anchor="middle">export</text>
<rect x="240" y="50" width="90" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/> <text x="285" y="77" font-size="13" text-anchor="middle">quantize</text> <rect x="350" y="50" width="90" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/> <text x="395" y="77" font-size="13" text-anchor="middle">eval</text> <rect x="460" y="50" width="90" height="44" rx="6" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/>
<text x="505" y="77" font-size="13" text-anchor="middle">bench</text> <rect x="570" y="50" width="90" height="44" rx="6" fill="#888" fill-opacity="0.2" stroke="currentColor"/> <text x="615" y="77" font-size="13" text-anchor="middle">report</text> <line x1="110" y1="72" x2="126" y2="72" stroke="currentColor"/><polygon points="126,68 130,72 126,76" fill="currentColor"/> <line x1="220" y1="72" x2="236" y2="72" stroke="currentColor"/><polygon points="236,68 240,72 236,76" fill="currentColor"/>
<line x1="330" y1="72" x2="346" y2="72" stroke="currentColor"/><polygon points="346,68 350,72 346,76" fill="currentColor"/> <line x1="440" y1="72" x2="456" y2="72" stroke="currentColor"/><polygon points="456,68 460,72 456,76" fill="currentColor"/> <line x1="550" y1="72" x2="566" y2="72" stroke="currentColor"/><polygon points="566,68 570,72 566,76" fill="currentColor"/> <text x="65" y="120" font-size="12" text-anchor="middle">config.yaml</text> <text x="65" y="136" font-size="12" text-anchor="middle">--set 덮어쓰기</text>
<text x="175" y="120" font-size="12" text-anchor="middle">model_fp32</text> <text x="175" y="136" font-size="12" text-anchor="middle">.onnx</text> <text x="285" y="120" font-size="12" text-anchor="middle">model_int8</text> <text x="285" y="136" font-size="12" text-anchor="middle">.onnx</text> <text x="395" y="120" font-size="12" text-anchor="middle">eval.json</text>
<text x="395" y="136" font-size="12" text-anchor="middle">acc · agree</text> <text x="505" y="120" font-size="12" text-anchor="middle">bench.json</text> <text x="505" y="136" font-size="12" text-anchor="middle">p50 · p99</text> <text x="615" y="120" font-size="12" text-anchor="middle">report.md</text> <text x="615" y="136" font-size="12" text-anchor="middle">csv · manifest</text>
<line x1="130" y1="160" x2="550" y2="160" stroke="#d0564a" stroke-dasharray="4 3"/> <text x="340" y="180" font-size="12" text-anchor="middle">각 산출물 옆 .key 파일 = sha256(입력 파일 내용 + 이 stage의 설정)</text> <text x="340" y="198" font-size="12" text-anchor="middle">key가 같으면 skip → make처럼 바뀐 곳부터 다시</text> <text x="340" y="216" font-size="12" text-anchor="middle">manifest.json = git hash + 라이브러리 버전 + 전체 설정 + stage key</text>
</svg>
```

그림 1 — 이 노트에서 만드는 파이프라인. 위 줄은 stage, 가운데 줄은 각 stage가 남기는 산출물, 아래는 재현성을 위한 장치(캐시 key와 manifest).

### 0.2 이 노트가 만드는 것

1~6절에서 stage와 도구를 하나씩 따로 만들고(`ex1_export.py`, `ex2_quantize.py`, `ex3_bench.py`, `cli_click.py`, `runner.py`, `ex5_cache.py`), 7절에서 `config.yaml` + `pipe.py`(160줄) 하나로 합친다. 9~12절은 로그 쪽(`logparse.py`, `tlm_writer.c`, llama-bench 호출), 13절은 펌웨어로 넘기는 마지막 stage다. 모든 파일은 scratch 폴더에서 실행했다.

### 0.3 좋은 파이프라인의 다섯 가지 성질

| 성질 | 뜻 | 펌웨어 쪽 대응 |
|---|---|---|
| 재현성 (reproducible) | 같은 입력·설정이면 같은 산출물 | 같은 커밋이면 같은 바이너리 (reproducible build) |
| 멱등성 (idempotent) | 두 번 돌려도 결과가 같고 부작용이 쌓이지 않음 | 같은 FW를 두 번 flash해도 상태가 같음 |
| 증분 (incremental) | 바뀐 stage부터만 다시 | make의 의존성 추적 |
| 추적성 (traceable) | 숫자마다 "어떤 버전·설정으로 나왔나"가 붙어 있음 | 로그 헤더의 FW 버전·빌드 ID |
| 실패가 시끄러움 (fail loud) | 툴이 실패하면 즉시 멈추고 이유를 보여 줌 | assert, 에러 코드 전파 |

이 다섯 개가 이 노트 전체의 체크리스트다.

---

## 1. 재료 준비 — 작은 모델과 ONNX export

### 1.1 모델과 데이터

파이프라인 자체가 주제이므로 모델은 일부러 작게 한다. 예를 들어 Hark 같은 웨어러블이라면 손목 IMU(가속도 xyz + 자이로 xyz = 6채널)로 제스처 4종을 구분하는 모델을 생각할 수 있다(추정 시나리오). 32샘플 창 하나 = 입력 `(6, 32)`.

데이터는 합성이다: 클래스 k는 주파수 (k+1) Hz의 사인파에 잡음을 더한 것. 학습·검증·calibration이 **서로 다른 seed**를 쓰는 점을 봐 두자. 같은 데이터로 calibration하고 평가하면 숫자가 부풀려진다(C2 참고).

```python
# imu_common.py — 파이프라인 예제들이 공유하는 작은 모델 + 합성 IMU 데이터
import numpy as np, torch, torch.nn as nn

C, T, K = 6, 32, 4                      # 채널(acc xyz + gyro xyz), 샘플 수, 제스처 클래스 수

def make_data(n, seed):
    """클래스 k = 주파수 (k+1) Hz 사인 + 잡음. shape (n, 6, 32) float32"""
    rng = np.random.default_rng(seed)
    y = rng.integers(0, K, n)
    t = np.arange(T) / T
    phase = rng.uniform(0, 2 * np.pi, (n, C, 1))
    x = np.sin(2 * np.pi * (y[:, None, None] + 1) * t + phase)
    x = x + 1.0 * rng.standard_normal((n, C, T))
    return x.astype(np.float32), y.astype(np.int64)

class GestureNet(nn.Module):
    def __init__(self):
        super().__init__()
        self.net = nn.Sequential(nn.Flatten(), nn.Linear(C * T, 64), nn.ReLU(),
                                 nn.Linear(64, 32), nn.ReLU(), nn.Linear(32, K))
    def forward(self, x):
        return self.net(x)

def train(seed=0, steps=300):
    torch.manual_seed(seed)
    x, y = make_data(2048, seed=1)
    m = GestureNet()
    opt = torch.optim.Adam(m.parameters(), lr=1e-3)
    xt, yt = torch.from_numpy(x), torch.from_numpy(y)
    for i in range(steps):
        idx = torch.randint(0, len(xt), (128,))
        loss = nn.functional.cross_entropy(m(xt[idx]), yt[idx])
        opt.zero_grad(); loss.backward(); opt.step()
    return m.eval()
```

파라미터 수를 손으로 세 보자. Linear(192→64) = 192·64 + 64 = 12,352, Linear(64→32) = 2,048 + 32 = 2,080, Linear(32→4) = 128 + 4 = 132. 합계 14,564개. fp32면 14,564 × 4 = 58,256 B다. 아래 ONNX 파일이 59,175 B이니 **약 900 B가 그래프 구조·이름 같은 메타데이터**이고 나머지는 가중치다.

### 1.2 export stage — 한 번 돌려서 확인하기

무엇을 확인하는 코드인지: 학습 → `torch.onnx.export` → `onnx.checker` → ORT로 같은 입력을 넣어 PyTorch 출력과 비교. export stage가 해야 할 최소한의 자기 검증이다.

```python
# ex1_export.py — 학습 → ONNX export → ORT로 열어서 PyTorch와 출력 비교
import warnings; warnings.filterwarnings("ignore", category=DeprecationWarning)
import numpy as np, onnx, onnxruntime as ort, torch
from pathlib import Path
from imu_common import train, make_data, C, T

out = Path("artifacts"); out.mkdir(exist_ok=True)
m = train(seed=0)
dummy = torch.zeros(1, C, T)
torch.onnx.export(m, dummy, out / "gesture_fp32.onnx", opset_version=17,
                  input_names=["imu"], output_names=["logits"],
                  dynamic_axes={"imu": {0: "batch"}, "logits": {0: "batch"}}, dynamo=False)
onnx.checker.check_model(str(out / "gesture_fp32.onnx"))

xv, yv = make_data(512, seed=2)                       # 검증셋 (학습과 다른 seed)
with torch.no_grad():
    ref = m(torch.from_numpy(xv)).numpy()
sess = ort.InferenceSession(str(out / "gesture_fp32.onnx"), providers=["CPUExecutionProvider"])
got = sess.run(None, {"imu": xv})[0]
print("onnx file   :", (out / "gesture_fp32.onnx").stat().st_size, "bytes")
print("max |diff|  :", f"{np.abs(ref - got).max():.2e}")
print("torch acc   :", f"{(ref.argmax(1) == yv).mean():.4f}")
print("ort acc     :", f"{(got.argmax(1) == yv).mean():.4f}")
np.save(out / "val_x.npy", xv); np.save(out / "val_y.npy", yv)
```

```text
onnx file   : 59175 bytes
max |diff|  : 2.38e-06
torch acc   : 0.9551
ort acc     : 0.9551
```

(잡음 크기는 일부러 1.0으로 골랐다. 0.6으로 두면 정확도가 1.0000이 되어 양자화 손실이 보이지 않는다.)

출력에서 볼 것: PyTorch와 ORT의 출력 차이가 2.4e-6 — fp32 연산 순서 차이 수준이다(F5 8절). 정확도도 같다. export stage는 이 "max diff가 1e-4보다 작다" 같은 조건을 **assert로 박아 두어야** 한다. 그래야 exporter 버전이 바뀌어 그래프가 이상하게 나올 때 파이프라인이 그 자리에서 멈춘다.

> 실행하면 PyTorch 2.8이 "legacy TorchScript exporter를 쓰고 있다, 2.9부터 dynamo가 기본"이라는 DeprecationWarning을 낸다. 코드 첫 줄에서 그 경고를 숨겼다. 파이프라인에서는 `dynamo=` 를 **명시적으로** 적어 두는 게 좋다 — 라이브러리 기본값이 바뀌면 같은 스크립트가 다른 그래프를 만든다. export 두 경로의 차이는 F5 3절 참고.

---

## 2. quantize stage — dynamic과 static을 같은 스크립트에서

양자화 이론(scale·zero point, per-channel, calibration 방법)은 C1·C2에, ORT의 QDQ 그래프 해부는 C2 5절과 F5 9절에 있다. 여기서는 **stage로서** 필요한 것만 본다.

- 입력: fp32 ONNX + calibration 데이터 + 설정(mode, 샘플 수, 방법).
- 출력: int8 ONNX.
- 두 함수: `quantize_dynamic`(가중치만 미리 int8, activation은 실행 중에 scale 계산 — calibration 불필요)과 `quantize_static`(activation scale을 calibration으로 미리 정함 — NPU가 원하는 형식, C2·F4).
- `CalibrationDataReader`: `get_next()`가 `{"입력이름": ndarray}`를 하나씩 돌려주고, 다 주면 `None`을 돌려주는 iterator. Python generator와 같은 개념이다.

무엇을 확인하는 코드인지: 같은 fp32 모델에서 두 int8 모델을 만들고 크기·정확도를 한 표로 본다.

```python
# ex2_quantize.py — 같은 fp32 ONNX에서 dynamic / static(QDQ) 두 int8 모델 만들기
import logging; logging.getLogger().setLevel(logging.ERROR)   # ORT 권고 로그 숨김
import numpy as np, onnxruntime as ort
from pathlib import Path
from onnxruntime.quantization import (quantize_dynamic, quantize_static, QuantType,
                                      QuantFormat, CalibrationDataReader, CalibrationMethod)
from imu_common import make_data

A = Path("artifacts"); fp32 = A / "gesture_fp32.onnx"

class ImuCalib(CalibrationDataReader):
    """calibration 샘플을 dict로 하나씩 넘겨주는 iterator. 끝나면 None."""
    def __init__(self, x):
        self.it = iter([{"imu": s[None]} for s in x])
    def get_next(self):
        return next(self.it, None)

quantize_dynamic(fp32, A / "gesture_dyn.onnx", weight_type=QuantType.QInt8)
xc, _ = make_data(200, seed=3)                       # calibration 전용 seed
quantize_static(fp32, A / "gesture_qdq.onnx", ImuCalib(xc), quant_format=QuantFormat.QDQ,
                activation_type=QuantType.QInt8, weight_type=QuantType.QInt8,
                calibrate_method=CalibrationMethod.MinMax)

xv, yv = np.load(A / "val_x.npy"), np.load(A / "val_y.npy")
for name in ["gesture_fp32", "gesture_dyn", "gesture_qdq"]:
    s = ort.InferenceSession(str(A / f"{name}.onnx"), providers=["CPUExecutionProvider"])
    acc = (s.run(None, {"imu": xv})[0].argmax(1) == yv).mean()
    print(f"{name:13s} {(A / f'{name}.onnx').stat().st_size:7d} B  acc={acc:.4f}")
```

```text
gesture_fp32    59175 B  acc=0.9551
gesture_dyn     18997 B  acc=0.9551
gesture_qdq     19410 B  acc=0.9512
```

출력에서 볼 것: 크기가 59 KB → 19 KB로 약 1/3. 4배가 아닌 이유는 bias(int32)·scale·zero point·QDQ 노드 같은 오버헤드 때문이다(작은 모델일수록 비율이 크다). static은 activation까지 양자화해서 정확도가 0.39%p(512개 중 2개) 떨어졌다.

### 2.1 stage로 만들 때 챙길 것

| 항목 | 이유 |
|---|---|
| calibration 데이터의 seed·출처를 설정에 기록 | 같은 모델이라도 calibration이 바뀌면 int8 가중치·scale이 바뀐다 → 캐시 key에 들어가야 한다 |
| 루트 logger 소음 처리 | ORT 양자화 모듈은 "pre-processing을 먼저 하라"는 권고를 **루트 logger**로 `WARNING` 출력한다. 내 로그와 섞이지 않게 레벨을 분리 (5.2절) |
| reader는 매번 새로 만들기 | iterator라서 한 번 다 읽으면 비어 있다. 같은 reader 객체를 두 번 넘기면 두 번째 calibration은 샘플 0개 |
| 출력 경로를 임시 파일로 | 양자화 도중 죽으면 반쯤 쓴 `.onnx`가 남는다 → 다음 실행이 그걸 "있음"으로 착각 (6절) |

> 마지막 행의 iterator 함정은 펌웨어 링버퍼와 같다: read pointer가 끝까지 간 버퍼를 다시 읽으면 빈 것처럼 보인다. "rewind"를 하거나 새로 만들어야 한다.

---

## 3. bench stage — 지연을 믿을 수 있게 재기

측정 방법론 자체(warmup, 반복, 분산, 노이즈 원인)는 K2 9절에 있다. 여기서는 그걸 **코드 한 함수**로 굳히는 법을 본다.

### 3.1 백분위수를 손으로

10번 잰 지연(us)이 `[5, 13, 5, 6, 5, 7, 5, 6, 5, 5]`라고 하자. 정렬하면 `[5,5,5,5,5,5,6,6,7,13]`.

```
mean = (5×6 + 6×2 + 7 + 13) / 10 = 62 / 10 = 6.2
numpy 기본(linear): 위치 = q × (n−1)
  p50: 0.50 × 9 = 4.5  → 정렬[4]=5 와 정렬[5]=5 사이 → 5.0
  p90: 0.90 × 9 = 8.1  → 정렬[8]=7 + 0.1 × (13 − 7)  = 7.6
  p99: 0.99 × 9 = 8.91 → 7 + 0.91 × 6                 = 12.46
```

말로 하면: 평균 6.2는 한 번 튄 13 때문에 "보통"보다 높게 나왔고, p50(중앙값)은 그 튐에 흔들리지 않는다. p99는 꼬리를 본다. 그래서 리포트에는 평균 대신 **p50(보통)과 p99(꼬리)**를 같이 적는다. SSD QoS에서 평균 latency 대신 p99.99를 보던 것과 같은 이유다.

`np.percentile(t, [50, 90, 99])`로 확인하면 정확히 `5.00, 7.60, 12.46`이 나온다. 그런데 `method="higher"`(가까운 위 값)로 정의를 바꾸면 p90은 **13**이 된다. 샘플이 적을수록 정의에 따른 차이가 크다. 다른 팀·다른 툴과 숫자를 비교할 때 **백분위수 정의와 샘플 수를 리포트에 같이 적어야** 하는 이유다.

### 3.2 bench 함수

무엇을 확인하는 코드인지: warmup → 고정 스레드 → `perf_counter_ns`로 한 번씩 재기 → 백분위수. 세 모델을 batch 1로 잰다.

```python
# ex3_bench.py — ORT 지연 측정: 1스레드 고정, warmup 후 N회, perf_counter_ns, 백분위수
import time, numpy as np, onnxruntime as ort
from pathlib import Path

def bench(path, x, warmup=50, iters=1000):
    so = ort.SessionOptions()
    so.intra_op_num_threads = 1                    # 기기(작은 코어 1개)와 비슷하게 고정
    so.inter_op_num_threads = 1
    s = ort.InferenceSession(str(path), so, providers=["CPUExecutionProvider"])
    feed = {"imu": x}
    for _ in range(warmup):                        # 캐시·allocator·lazy init 데우기
        s.run(None, feed)
    t = np.empty(iters, dtype=np.int64)
    for i in range(iters):
        t0 = time.perf_counter_ns()
        s.run(None, feed)
        t[i] = time.perf_counter_ns() - t0
    us = t / 1e3
    return {"p50": np.percentile(us, 50), "p90": np.percentile(us, 90),
            "p99": np.percentile(us, 99), "mean": us.mean(), "max": us.max()}

A = Path("artifacts"); x = np.load(A / "val_x.npy")[:1]   # batch 1 = 기기 실사용 모양
for name in ["gesture_fp32", "gesture_dyn", "gesture_qdq"]:
    r = bench(A / f"{name}.onnx", x)
    print(f"{name:13s} " + "  ".join(f"{k}={v:6.1f}us" for k, v in r.items()))
```

같은 스크립트를 연달아 두 번 돌린 실제 출력 (Apple M2):

```text
gesture_fp32  p50=   5.0us  p90=   5.1us  p99=   6.6us  mean=   5.0us  max=  13.6us
gesture_dyn   p50=   5.7us  p90=   6.0us  p99=   7.9us  mean=   5.8us  max=   8.8us
gesture_qdq   p50=   5.2us  p90=   5.4us  p99=   7.2us  mean=   5.3us  max=  13.5us
gesture_fp32  p50=   4.9us  p90=   5.7us  p99=   7.2us  mean=   5.1us  max=   8.2us
gesture_dyn   p50=   5.8us  p90=   6.3us  p99=   8.3us  mean=   6.0us  max=  14.5us
gesture_qdq   p50=   5.5us  p90=   7.2us  p99=  12.6us  mean=   6.1us  max=  49.9us
```

출력에서 볼 것이 세 가지다.

- **int8이 fp32보다 빠르지 않다.** 14K 파라미터 모델은 연산이 몇 us뿐이라 ORT의 호출 오버헤드(입력 검사, 노드 디스패치)가 지배한다. dynamic은 실행 중에 activation scale을 계산하는 노드(`DynamicQuantizeLinear`)가 추가돼 오히려 느리다. 데스크톱 CPU의 결과로 기기(NPU·DSP)의 결과를 예측하면 안 된다는 증거이고, 그래서 bench stage는 결국 **실기기에서** 돌아야 한다(O2, O3).
- **p50은 두 번 실행에서 ±0.3 us로 안정적이지만 p99와 max는 크게 흔들린다** (qdq p99 7.2 → 12.6, max 13.5 → 49.9). OS 스케줄링·인터럽트 때문이다. 회귀 판단은 p50으로, 꼬리는 여러 번 돌린 분포로 본다(10절).
- 리포트에 "M2, 1 thread, batch 1, warmup 50, iters 1000"을 같이 적지 않으면 이 숫자는 의미가 없다 → manifest(7절).

```svg
<svg viewBox="0 0 600 310" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="260" x2="580" y2="260" stroke="currentColor"/> <line x1="60" y1="40" x2="60" y2="260" stroke="currentColor"/> <text x="52" y="264" font-size="12" text-anchor="end">0</text> <text x="52" y="220" font-size="12" text-anchor="end">2</text> <text x="52" y="176" font-size="12" text-anchor="end">4</text>
<text x="52" y="132" font-size="12" text-anchor="end">6</text> <text x="52" y="88" font-size="12" text-anchor="end">8</text> <text x="52" y="44" font-size="12" text-anchor="end">10</text> <line x1="60" y1="216" x2="580" y2="216" stroke="#888" stroke-dasharray="2 4"/> <line x1="60" y1="172" x2="580" y2="172" stroke="#888" stroke-dasharray="2 4"/>
<line x1="60" y1="128" x2="580" y2="128" stroke="#888" stroke-dasharray="2 4"/> <line x1="60" y1="84" x2="580" y2="84" stroke="#888" stroke-dasharray="2 4"/> <text x="20" y="150" font-size="12" transform="rotate(-90 20 150)" text-anchor="middle">지연 (us)</text> <rect x="100" y="150" width="36" height="110" fill="#4a7bd0"/> <rect x="144" y="147.8" width="36" height="112.2" fill="#e08a3c"/>
<rect x="188" y="114.8" width="36" height="145.2" fill="#d0564a"/> <rect x="270" y="134.6" width="36" height="125.4" fill="#4a7bd0"/> <rect x="314" y="128" width="36" height="132" fill="#e08a3c"/> <rect x="358" y="86.2" width="36" height="173.8" fill="#d0564a"/> <rect x="440" y="145.6" width="36" height="114.4" fill="#4a7bd0"/>
<rect x="484" y="141.2" width="36" height="118.8" fill="#e08a3c"/> <rect x="528" y="101.6" width="36" height="158.4" fill="#d0564a"/> <text x="162" y="280" font-size="13" text-anchor="middle">fp32</text> <text x="332" y="280" font-size="13" text-anchor="middle">dynamic int8</text> <text x="502" y="280" font-size="13" text-anchor="middle">static QDQ int8</text>
<text x="206" y="108" font-size="12" text-anchor="middle">6.6</text> <text x="376" y="80" font-size="12" text-anchor="middle">7.9</text> <text x="546" y="95" font-size="12" text-anchor="middle">7.2</text> <rect x="380" y="20" width="12" height="12" fill="#4a7bd0"/><text x="397" y="31" font-size="12">p50</text> <rect x="440" y="20" width="12" height="12" fill="#e08a3c"/><text x="457" y="31" font-size="12">p90</text>
<rect x="500" y="20" width="12" height="12" fill="#d0564a"/><text x="517" y="31" font-size="12">p99</text> <text x="80" y="302" font-size="12">M2 · 1 thread · batch 1 · iters 1000 (첫 번째 실행)</text>
</svg>
```

그림 2 — ex3 첫 실행의 p50·p90·p99. 이 크기의 모델에서는 int8이 이득이 없고, 꼬리(p99)는 p50보다 30~40% 길다.

### 3.3 측정 함수에서 자주 틀리는 것

- `time.time()`을 쓰면 벽시계라 NTP 보정에 흔들리고 해상도가 낮다. 구간 측정은 단조 증가 시계 `time.perf_counter_ns()`.
- 루프 전체 시간을 재서 iters로 나누면 평균만 남고 분포가 사라진다. **한 번씩** 재서 배열에 넣는다.
- 결과 배열을 Python list에 `append`하면 측정 루프 안에서 메모리 할당이 일어난다. `np.empty`로 미리 잡는다 — 펌웨어에서 ISR 안에서 malloc 하지 않는 것과 같은 원리.
- 세션 생성 시간(그래프 최적화·메모리 계획)을 측정에 섞지 않는다. 그건 따로 "cold start"로 잰다.

---

## 4. CLI 도구 — argparse와 click

### 4.1 왜 CLI로 만드나

스크립트 상단의 `MODEL = "a.onnx"`를 손으로 고쳐 가며 돌리면 (1) 어떤 값으로 돌렸는지 기록이 안 남고 (2) 다른 사람이 못 쓰고 (3) 자동화(CI, sweep)에서 부를 수 없다. 명령행 인자로 받으면 **명령 한 줄 = 실험 하나의 완전한 기록**이 된다. 펌웨어 쪽 비유: UART 디버그 셸에 `perf run --qd 32 --bs 4k` 같은 명령을 만들어 두던 것.

subcommand 구조(`git commit`, `git push`처럼)가 파이프라인과 잘 맞는다: `mlpipe export`, `mlpipe quantize`, `mlpipe bench`, `mlpipe report`, 그리고 전부를 부르는 `mlpipe run`.

### 4.2 argparse — 표준 라이브러리

표준 라이브러리 `argparse`로도 같은 구조를 만든다: `p.add_subparsers(dest="cmd", required=True)`로 하위 명령을 만들고 `sub.add_parser("quantize")`에 `add_argument("--calib-n", type=int)`, `choices=["dynamic", "static"]`를 붙인다. `--iters x`처럼 잘못된 값을 주면 `error: argument --iters: invalid int value: 'x'`를 찍고 **return code 2**로 끝나서 셸·CI가 실패를 알아챈다. `--calib-n`은 `calib_n`으로 바뀌어 들어온다. 의존성이 없어서 기기 위 Python이나 최소 Docker 이미지에서도 돈다.

### 4.3 click — 데코레이터로

무엇을 확인하는 코드인지: 같은 CLI를 click으로. 공통 옵션(`-v`)을 하위 명령에 넘기는 `pass_context`/`pass_obj`, 파일 존재 검사(`click.Path(exists=True)`).

```python
# cli_click.py — 같은 CLI를 click으로
import click

@click.group()
@click.option("-v", "--verbose", is_flag=True)
@click.pass_context
def cli(ctx, verbose):
    """model pipeline"""
    ctx.obj = {"verbose": verbose}               # 하위 명령에 공통 상태 전달

@cli.command()
@click.argument("model", type=click.Path(exists=True, dir_okay=False))
@click.option("--mode", type=click.Choice(["dynamic", "static"]), default="static")
@click.option("--calib-n", type=int, default=200, show_default=True)
@click.pass_obj
def quantize(obj, model, mode, calib_n):
    """fp32 ONNX -> int8 ONNX"""
    click.echo(f"quantize {model} mode={mode} calib_n={calib_n} verbose={obj['verbose']}")

@cli.command()
@click.argument("model")
@click.option("--iters", type=int, default=1000)
def bench(model, iters):
    """latency benchmark"""
    click.echo(f"bench {model} iters={iters}")

if __name__ == "__main__":
    cli()
```

```sh
python cli_click.py --help
python cli_click.py -v quantize artifacts/gesture_fp32.onnx --calib-n 64
python cli_click.py quantize nope.onnx; echo "rc=$?"
```

```text
Usage: cli_click.py [OPTIONS] COMMAND [ARGS]...

  model pipeline

Options:
  -v, --verbose
  --help         Show this message and exit.

Commands:
  bench     latency benchmark
  quantize  fp32 ONNX -> int8 ONNX
quantize artifacts/gesture_fp32.onnx mode=static calib_n=64 verbose=True
Usage: cli_click.py quantize [OPTIONS] MODEL
Try 'cli_click.py quantize --help' for help.

Error: Invalid value for 'MODEL': File 'nope.onnx' does not exist.
rc=2
```

출력에서 볼 것: 함수 docstring이 그대로 도움말이 된다. 없는 파일은 **실제 작업을 시작하기 전에** 거부된다 — 10분짜리 calibration을 돌린 뒤에 저장 경로 오타로 죽는 일을 막는다.

### 4.4 무엇을 고르나

| 기준 | argparse | click |
|---|---|---|
| 의존성 | 없음 (표준) | 패키지 하나 (여기선 8.1.8) |
| subcommand | `add_subparsers` + 수동 dispatch | `@group` / `@command` 데코레이터 |
| 공통 상태 전달 | Namespace를 직접 넘김 | `ctx.obj`, `pass_obj` |
| 검증 | `type=`, `choices=` | `click.Path(exists=)`, `Choice`, `IntRange` 등 |
| 테스트 | `main(argv=[...])` 호출 | `click.testing.CliRunner` |
| 어울리는 곳 | 기기 위 스크립트, 단일 파일 툴 | 팀 공용 파이프라인 CLI |

---

## 5. 설정·로깅·외부 툴 호출

### 5.1 YAML 설정 — 실험 하나 = 파일 하나

인자가 열 개를 넘으면 명령행이 길어지고 기록하기 어렵다. 실험의 **기본값은 YAML 파일**에 두고, 바꾸고 싶은 몇 개만 명령행에서 덮어쓴다(`--set quantize.calib_n=100`). Hydra 같은 프레임워크가 하는 일을 20줄로 흉내 내는 것이다.

```yaml
# config.yaml — 실험 하나 = 설정 파일 하나
seed: 0
workdir: runs/exp1
export:
  opset: 17
  train_steps: 300
quantize:
  mode: static          # static | dynamic
  calib_n: 200
  calib_method: MinMax  # MinMax | Entropy | Percentile
bench:
  warmup: 50
  iters: 500
  threads: 1
```

덮어쓰기 함수는 7.1절 `load_cfg`에 있다. 요점 두 가지:

- 반드시 `yaml.safe_load`를 쓴다. `yaml.load`(전체 로더)는 YAML 태그로 임의 Python 객체를 만들 수 있어서 남이 준 설정 파일로 코드가 실행될 수 있다.
- `--set` 값도 `yaml.safe_load`로 해석하면 `"100"` → `100`, `"true"` → `True`, `"dynamic"` → `"dynamic"`으로 타입이 알아서 맞는다.

### 5.2 logging — print 대신

`print`는 레벨이 없고, 누가 찍었는지 모르고, 끌 수 없다. `logging`은 logger가 **이름으로 된 트리**라는 점만 기억하면 된다.

```
root  (level=ERROR)          ← 라이브러리들이 logging.warning(...)으로 쓰는 곳
 ├── mlpipe  (level=INFO)    ← 내 파이프라인 = logging.getLogger("mlpipe")
 └── runner  (level 미지정 → 부모 root의 ERROR를 따름)
```

ORT 양자화 모듈은 이름 없는 `logging.warning(...)`으로 "Please consider to run pre-processing before quantization" 권고를 찍는데, 이게 루트 logger로 간다. 처음 `pipe.py`를 만들 때 이걸 끄려고 `logging.getLogger("root").setLevel(logging.WARNING)`을 썼더니 그 경고 두 줄은 그대로 나오고 내 `INFO` 로그는 하나도 안 나왔다. `getLogger("root")`는 **루트 logger 자신**을 돌려주므로, 루트 레벨을 올린 것이 자식 `mlpipe`의 유효 레벨까지 올렸기 때문이다. 고친 방법은 "루트는 ERROR, 내 logger만 INFO"로 레벨을 따로 주는 것이다(7.4절 코드).

펌웨어 비유: 모듈별 log level 마스크(`LOG_MASK_NVME | LOG_MASK_FTL`)를 두던 것과 같다. 시끄러운 모듈은 끄고 보고 싶은 모듈만 켠다.

### 5.3 subprocess — 외부 툴을 부를 때의 네 가지 규칙

파이프라인은 결국 남의 바이너리를 부른다: 벤더 NPU converter, `llama-quantize`, 크로스 컴파일러, `adb`(O2). 규칙은 넷이다.

1. **인자는 list로**, `shell=True` 금지. 파일 이름에 공백이나 `;`가 있어도 안전하고, 셸 인젝션이 없다.
2. **timeout을 항상 건다.** 멈춘 툴 하나가 nightly 전체를 영원히 붙잡는다. 펌웨어의 watchdog과 같다.
3. **return code를 검사한다.** 0이 아니면 즉시 멈춘다. stdout만 보고 "뭔가 출력됐으니 성공"이라고 판단하지 않는다.
4. **실패하면 stderr 꼬리를 예외에 담는다.** 로그 파일을 따로 열지 않아도 원인이 보이게.

```python
# runner.py — 외부 툴(컴파일러, 벤더 converter, llama-bench, adb …) 호출 래퍼
import logging, shlex, subprocess, time
log = logging.getLogger("runner")

class ToolError(RuntimeError):
    pass

def run(cmd, timeout_s, cwd=None, env=None):
    """list 인자로 실행(shell=False). 실패·타임아웃이면 stderr 꼬리를 담아 예외."""
    t0 = time.perf_counter()
    try:
        p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, timeout=timeout_s)
    except subprocess.TimeoutExpired:
        raise ToolError(f"TIMEOUT after {timeout_s}s: {shlex.join(cmd)}") from None
    dt = time.perf_counter() - t0
    if p.returncode != 0:
        tail = "\n".join(p.stderr.strip().splitlines()[-3:])
        raise ToolError(f"rc={p.returncode} ({dt:.2f}s): {shlex.join(cmd)}\n  stderr: {tail}")
    log.debug("ok %.2fs: %s", dt, shlex.join(cmd))
    return p.stdout
```

시험용 `fake_convert.py`는 벤더 converter 흉내다: 인자로 받은 op이 `GridSample`이면 stderr에 `[cvt] parsing graph...`와 `[cvt] ERROR: unsupported op GridSample`을 쓰고 `sys.exit(3)`, 아니면 `[cvt] Conv: converted ok`를 출력한다.

무엇을 확인하는 코드인지: 성공·실패(rc≠0)·멈춤(timeout) 세 경우가 각각 어떻게 드러나는지.

```python
# ex11_subprocess.py — 성공 / 0이 아닌 return code / 타임아웃 세 경우
import sys
from runner import run, ToolError

cases = [
    [sys.executable, "fake_convert.py", "Conv"],
    [sys.executable, "fake_convert.py", "GridSample"],
    [sys.executable, "-c", "import time; time.sleep(5)"],     # 멈춘 툴 흉내
]
for cmd in cases:
    try:
        print("stdout:", run(cmd, timeout_s=1.0).strip())
    except ToolError as e:
        print("ToolError:", str(e).replace(sys.executable, "python"))
```

```text
stdout: [cvt] Conv: converted ok
ToolError: rc=3 (0.01s): python fake_convert.py GridSample
  stderr: [cvt] parsing graph...
[cvt] ERROR: unsupported op GridSample
ToolError: TIMEOUT after 1.0s: python -c 'import time; time.sleep(5)'
```

출력에서 볼 것: 실패한 명령이 **복사해서 바로 다시 돌릴 수 있는 형태**(`shlex.join`)로 찍힌다. 5초짜리 sleep은 1초에 잘렸다. `subprocess.run`은 timeout이 나면 자식 프로세스를 kill한 뒤 `TimeoutExpired`를 던진다. 다만 그 자식이 또 손자 프로세스를 띄웠다면 손자는 남을 수 있다 — 그런 툴에는 `start_new_session=True`로 프로세스 그룹을 따로 만들고 그룹 전체를 kill하는 처리가 필요하다.

---

## 6. 캐시와 멱등성 — make처럼 바뀐 곳부터만

### 6.1 직관

export는 학습까지 포함하면 수 분~수 시간, calibration은 수 분이 걸린다. bench 설정 하나 바꿨는데 export부터 다시 하면 하루가 간다. make는 "출력 파일이 입력 파일보다 새 것이면 건너뛴다"(mtime 비교)로 이 문제를 푼다. ML 파이프라인에서는 mtime 대신 **내용 hash**를 쓰는 편이 낫다.

- 같은 내용을 다시 써도 mtime은 바뀐다 → make는 쓸데없이 다시 돈다.
- `git checkout`으로 옛 파일을 되돌리면 mtime이 새로워진다 → 역시 오판.
- 설정(파라미터)은 파일이 아니라서 mtime이 없다 → hash에 같이 넣으면 된다.

정의: **stage key = sha256(입력 파일들의 내용 + 이 stage 설정을 정렬된 JSON으로)**. 산출물 옆에 `.key` 파일로 key를 저장하고, 다음 실행에서 key가 같고 산출물이 있으면 건너뛴다.

### 6.2 코드

무엇을 확인하는 코드인지: 두 stage(upper → count)를 네 번 돌리며, 설정이 같을 때·바뀔 때·입력을 **같은 내용으로 다시 쓸 때**(mtime만 바뀜) 각각 무엇이 다시 도는지.

```python
# ex5_cache.py — make처럼: (입력 파일 내용 + 설정)의 hash가 같으면 stage를 건너뛴다
import hashlib, json
from pathlib import Path

def stage_key(inputs, params):
    h = hashlib.sha256()
    for p in sorted(map(Path, inputs)):
        h.update(p.name.encode()); h.update(p.read_bytes())   # 파일 "내용"을 해시 (mtime 아님)
    h.update(json.dumps(params, sort_keys=True).encode())       # dict 순서 무관하게
    return h.hexdigest()[:12]

def run_stage(name, inputs, params, out, fn):
    key = stage_key(inputs, params)
    stamp = Path(str(out) + ".key")
    if Path(out).exists() and stamp.exists() and stamp.read_text() == key:
        print(f"[skip] {name:8s} key={key}"); return key
    fn(inputs, params, out)
    stamp.write_text(key)                          # 성공한 뒤에만 stamp 기록
    print(f"[run ] {name:8s} key={key}"); return key

d = Path("cache_demo"); d.mkdir(exist_ok=True)
(d / "a.txt").write_text("raw sensor log v1\n")
def upper(inp, prm, out): Path(out).write_text(Path(inp[0]).read_text().upper() * prm["n"])
def count(inp, prm, out): Path(out).write_text(str(len(Path(inp[0]).read_text())))

for trial, n in [(1, 2), (2, 2), (3, 3), (4, 3)]:
    if trial == 4: (d / "a.txt").write_text("raw sensor log v1\n")   # 같은 내용 재기록 (mtime만 바뀜)
    print(f"-- trial {trial} (n={n})")
    run_stage("upper", [d / "a.txt"], {"n": n}, d / "b.txt", upper)
    run_stage("count", [d / "b.txt"], {}, d / "c.txt", count)
```

```text
-- trial 1 (n=2)
[run ] upper    key=452aa098f969
[run ] count    key=077d986c8ebe
-- trial 2 (n=2)
[skip] upper    key=452aa098f969
[skip] count    key=077d986c8ebe
-- trial 3 (n=3)
[run ] upper    key=6af79d9c76ba
[run ] count    key=96734c5cd921
-- trial 4 (n=3)
[skip] upper    key=6af79d9c76ba
[skip] count    key=96734c5cd921
```

출력에서 볼 것: trial 3에서 upper의 설정 `n`만 바꿨는데 count도 다시 돌았다 — count는 설정이 그대로지만 **입력 파일 b.txt의 내용**이 바뀌었으니 key가 바뀐 것이다. 의존성을 따로 선언하지 않아도 "입력 내용 hash"가 연쇄 무효화를 자동으로 해 준다. trial 4는 mtime 기반 make라면 둘 다 다시 돌았을 상황이다.

### 6.3 멱등성을 지키는 작은 규칙

- **stamp는 성공한 뒤에만** 쓴다. 중간에 죽으면 stamp가 없으니 다음에 다시 돈다.
- **임시 파일에 쓰고 rename**한다(`tmp.replace(out)`). 같은 파일시스템 안의 rename은 원자적이라, 반쯤 쓴 산출물이 정식 이름으로 보이는 순간이 없다. 펌웨어의 "새 이미지를 다른 슬롯에 쓰고 검증 후 포인터만 바꾸는" A/B 업데이트와 같은 생각이다.
- **key에 코드도 넣는다.** export stage는 `imu_common.py`(모델 정의·학습 코드) 내용을 입력으로 hash한다. 모델 코드가 바뀌었는데 캐시가 옛 ONNX를 돌려주면 가장 찾기 힘든 버그가 된다.
- **비결정적 stage는 캐시를 의심한다.** bench는 같은 입력이라도 매번 숫자가 다르다. 캐시해 두면 "다시 재고 싶을 때" 못 잰다 → `--force bench` 같은 탈출구를 둔다(7절).

---

## 7. 전부 묶기 — `pipe.py`

지금까지의 조각(1~6절)을 CLI 하나로 합친다. 전체 160줄을 네 조각으로 나눠 보인다. 앞 절과 같은 내용인 stage 본문 몇 개는 생략했다.

### 7.1 조각 1 — 설정과 캐시

```python
# pipe.py — export → quantize → eval → bench → report 를 한 CLI로
import warnings; warnings.filterwarnings("ignore", category=DeprecationWarning)
import hashlib, json, logging, platform, subprocess, sys, time
from pathlib import Path
import click, numpy as np, yaml

log = logging.getLogger("mlpipe")
HERE = Path(__file__).resolve().parent

# ---------- 조각 1: 설정 + 캐시 ----------
def load_cfg(path, overrides):
    cfg = yaml.safe_load(Path(path).read_text())
    for ov in overrides:                                   # --set quantize.calib_n=100
        k, v = ov.split("=", 1)
        *parents, leaf = k.split(".")
        d = cfg
        for p in parents:
            d = d[p]
        d[leaf] = yaml.safe_load(v)                        # "100" → 100, "true" → True
    return cfg

def cached(name, inputs, params, out, fn, force=False):
    key, stamp = key_of(inputs, params), Path(str(out) + ".key")
    if not force and out.exists() and stamp.exists() and stamp.read_text() == key:
        log.info("%-8s skip (key=%s)", name, key); return key
    t0 = time.perf_counter()
    tmp = out.with_name(out.stem + ".tmp" + out.suffix)
    fn(tmp)                                                # 임시 파일에 쓰고
    tmp.replace(out)                                       # 성공하면 원자적으로 rename
    stamp.write_text(key)
    log.info("%-8s done %.2fs (key=%s)", name, time.perf_counter() - t0, key)
    return key
```

`key_of`(생략)는 6절의 `stage_key`와 같다(파일 이름은 빼고 내용 + 정렬된 JSON만 hash). `cached`는 6절의 `run_stage`에 임시 파일 + rename과 `force`를 더한 것이다.

### 7.2 조각 2 — stage 구현

1~3절 코드를 함수로 옮긴 것이다. 함수마다 "출력 경로 하나를 받아서 거기에 쓴다"는 같은 모양을 지킨다. 그래야 `cached`가 어떤 stage든 감쌀 수 있다. 무거운 import(torch, onnxruntime)는 **함수 안에서** 한다 — `--help`만 칠 때 torch를 2초씩 로드하지 않게.

`do_export`·`do_quantize`·`do_bench`는 ex1·ex2·ex3의 본문을 그대로 옮기고 경로와 숫자만 `cfg`에서 읽게 바꾼 것이라 생략한다(예: `make_data(q["calib_n"], seed=cfg["seed"] + 3)`, `getattr(CalibrationMethod, q["calib_method"])`). `sess(path, threads=1)`는 ex3처럼 스레드를 고정한 `InferenceSession`을 만드는 3줄짜리 helper다. 새로 생긴 것은 eval stage다.

```python
def do_eval(cfg, fp32, int8, out):
    from imu_common import make_data
    xv, yv = make_data(1000, seed=cfg["seed"] + 2)
    a, b = sess(fp32).run(None, {"imu": xv})[0], sess(int8).run(None, {"imu": xv})[0]
    res = {"acc_fp32": float((a.argmax(1) == yv).mean()), "acc_int8": float((b.argmax(1) == yv).mean()),
           "top1_agree": float((a.argmax(1) == b.argmax(1)).mean()),
           "max_abs_logit_diff": float(np.abs(a - b).max())}
    out.write_text(json.dumps(res, indent=2))
```

eval이 정확도 하나만이 아니라 `top1_agree`(fp32와 int8의 예측이 같은 비율)와 `max_abs_logit_diff`도 남기는 점을 보자. 정확도는 정답 라벨이 있어야 하지만, agree와 logit 차이는 **라벨 없는 실기기 로그 데이터로도** 잴 수 있다(C8 2절).

### 7.3 조각 3 — manifest와 report

```python
# ---------- 조각 3: manifest + report ----------
def git_rev():
    try:
        r = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=HERE,
                           capture_output=True, text=True, timeout=5)
        dirty = subprocess.run(["git", "status", "--porcelain"], cwd=HERE,
                               capture_output=True, text=True, timeout=5).stdout.strip()
        return r.stdout.strip() + ("-dirty" if dirty else "") if r.returncode == 0 else "nogit"
    except (OSError, subprocess.TimeoutExpired):
        return "nogit"

def manifest(cfg, keys):
    import torch, onnx, onnxruntime
    return {"git": git_rev(), "python": platform.python_version(), "machine": platform.machine(),
            "versions": {"torch": torch.__version__, "onnx": onnx.__version__,
                         "onnxruntime": onnxruntime.__version__, "numpy": np.__version__},
            "config": cfg, "stage_keys": keys}
```

`git_rev`에 `-dirty`가 붙는 점이 중요하다. 커밋하지 않은 수정이 있는 상태로 낸 숫자는 그 hash로 재현할 수 없다. 펌웨어에서 dirty 빌드를 양산에 올리지 않는 것과 같은 규칙이다. git 호출 자체에도 timeout을 걸었다(5.3절 규칙 2).

`do_report`(생략)는 eval.json·bench.json을 pandas로 합쳐 `results.csv`, `manifest.json`, `report.md`를 쓴다. pandas의 `to_markdown()`은 `tabulate` 패키지가 필요한데 이 환경에는 없어서 markdown 표는 문자열로 직접 만들었다. 리포트 형식이 복잡해지면 Jinja2 템플릿으로 옮긴다.

### 7.4 조각 4 — CLI

```python
# ---------- 조각 4: CLI ----------
@click.group()
@click.option("-c", "--config", "cfg_path", default="config.yaml", show_default=True)
@click.option("--set", "overrides", multiple=True, help="key.sub=value (여러 번 가능)")
@click.option("-v", "--verbose", is_flag=True)
@click.pass_context
def cli(ctx, cfg_path, overrides, verbose):
    logging.basicConfig(level=logging.ERROR,                 # root(=라이브러리들)는 ERROR만
                        format="%(levelname)-5s %(name)s: %(message)s")
    log.setLevel(logging.DEBUG if verbose else logging.INFO)  # 내 logger만 INFO
    cfg = load_cfg(cfg_path, overrides)
    wd = HERE / cfg["workdir"]; wd.mkdir(parents=True, exist_ok=True)
    ctx.obj = {"cfg": cfg, "wd": wd}

@cli.command()
@click.option("--force", multiple=True, help="이 stage는 캐시 무시")
@click.pass_obj
def run(obj, force):
    """모든 stage를 순서대로 (캐시된 건 skip)"""
    cfg, wd = obj["cfg"], obj["wd"]
    np.random.seed(cfg["seed"])
    fp32, int8 = wd / "model_fp32.onnx", wd / "model_int8.onnx"
    k = {}
    k["export"] = cached("export", [HERE / "imu_common.py"], {"seed": cfg["seed"], **cfg["export"]},
                         fp32, lambda o: do_export(cfg, o), "export" in force)
    k["quantize"] = cached("quantize", [fp32], cfg["quantize"], int8,
                           lambda o: do_quantize(cfg, fp32, o), "quantize" in force)
    k["eval"] = cached("eval", [fp32, int8], {"seed": cfg["seed"]}, wd / "eval.json",
                       lambda o: do_eval(cfg, fp32, int8, o), "eval" in force)
    k["bench"] = cached("bench", [fp32, int8], cfg["bench"], wd / "bench.json",
                        lambda o: do_bench(cfg, {"fp32": fp32, "int8": int8}, o), "bench" in force)
    do_report(wd, k, cfg)
    click.echo((wd / "report.md").read_text(), nl=False)

if __name__ == "__main__":
    cli()
```

여기서는 지면 때문에 `run` 하나만 만들었다. 실무에서는 `export`·`quantize`·`bench`·`report`를 각각 `@cli.command()`로 노출해서 한 stage만 따로 돌릴 수 있게 한다 — `run`은 그 함수들을 순서대로 부를 뿐이다.

### 7.5 세 번 돌려 보기

scratch 폴더를 git 저장소로 만들어 커밋한 뒤(`f53a027`) 실행했다. 무엇을 확인하는 실행인지: 첫 실행은 전부, 두 번째는 전부 skip, 세 번째는 quantize 설정만 바꿨을 때 무엇이 다시 도는지.

```sh
rm -rf runs
python pipe.py run                                  # 1회차
python pipe.py run                                  # 2회차 (아무것도 안 바꿈)
python pipe.py --set quantize.mode=dynamic run      # 3회차 (quantize 설정만 바꿈)
```

```text
=== run 1
INFO  mlpipe: export   done 1.31s (key=a0faca854706)
INFO  mlpipe: quantize done 0.09s (key=8e5750669f7a)
INFO  mlpipe: eval     done 0.01s (key=711f571dc656)
INFO  mlpipe: bench    done 0.01s (key=82e38e3fda1a)
| model | size_B | acc | p50_us | p99_us |
|---|---|---|---|---|
| fp32 | 59167 | 0.9610 | 4.88 | 6.42 |
| int8 | 19382 | 0.9610 | 5.21 | 6.92 |

top1_agree=0.9990, git=f53a027
=== run 2
INFO  mlpipe: export   skip (key=a0faca854706)
INFO  mlpipe: quantize skip (key=8e5750669f7a)
INFO  mlpipe: eval     skip (key=711f571dc656)
INFO  mlpipe: bench    skip (key=82e38e3fda1a)
(report.md 내용은 run 1과 같아서 생략)
=== run 3
INFO  mlpipe: export   skip (key=a0faca854706)
INFO  mlpipe: quantize done 0.82s (key=0ae702121747)
INFO  mlpipe: eval     done 0.01s (key=7b6aa61a0a45)
INFO  mlpipe: bench    done 0.01s (key=4ae2b278fd44)
| model | size_B | acc | p50_us | p99_us |
|---|---|---|---|---|
| fp32 | 59167 | 0.9610 | 4.79 | 5.04 |
| int8 | 18957 | 0.9610 | 5.58 | 5.83 |

top1_agree=0.9980, git=f53a027
```

3회차의 int8은 dynamic 모델이라 static QDQ(19,382 B)보다 425 B 작은 18,957 B다.

출력에서 볼 것:

- 1회차 4 stage 합계 약 1.4 s, 2회차는 0 s(전부 skip) — 리포트는 매번 새로 만든다(싸고, git hash를 갱신해야 하니까).
- 3회차는 export만 skip이고 그 아래가 전부 다시 돌았다. quantize 설정이 바뀌어 int8 파일 내용이 바뀌었고, eval과 bench는 그 파일 내용을 입력으로 hash하니 자동으로 무효화됐다.
- bench의 p50(4.88 → 4.79)은 같은 fp32 모델인데도 실행마다 조금씩 다르다. 그래서 bench 결과를 캐시하는 것은 "재측정을 원할 땐 `--force bench`"와 짝이어야 한다.

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<text x="245" y="22" font-size="13" text-anchor="middle">--set quantize.mode=dynamic</text> <line x1="245" y1="30" x2="245" y2="104" stroke="#d0564a" stroke-width="2"/> <polygon points="240,102 245,110 250,102" fill="#d0564a"/> <rect x="30" y="110" width="110" height="40" rx="6" fill="#888" fill-opacity="0.2" stroke="#888"/> <text x="85" y="135" font-size="13" text-anchor="middle">export · skip</text>
<rect x="190" y="110" width="110" height="40" rx="6" fill="#e08a3c" fill-opacity="0.3" stroke="currentColor"/> <text x="245" y="135" font-size="13" text-anchor="middle">quantize · run</text> <rect x="360" y="60" width="110" height="40" rx="6" fill="#e08a3c" fill-opacity="0.3" stroke="currentColor"/> <text x="415" y="85" font-size="13" text-anchor="middle">eval · run</text> <rect x="360" y="160" width="110" height="40" rx="6" fill="#e08a3c" fill-opacity="0.3" stroke="currentColor"/>
<text x="415" y="185" font-size="13" text-anchor="middle">bench · run</text> <rect x="530" y="110" width="120" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/> <text x="590" y="135" font-size="13" text-anchor="middle">report · 항상</text> <line x1="140" y1="130" x2="184" y2="130" stroke="currentColor"/><polygon points="184,126 190,130 184,134" fill="currentColor"/> <line x1="300" y1="130" x2="355" y2="84" stroke="currentColor"/><polygon points="351,81 360,80 355,88" fill="currentColor"/>
<line x1="300" y1="130" x2="355" y2="176" stroke="currentColor"/><polygon points="351,179 360,180 355,172" fill="currentColor"/> <line x1="470" y1="80" x2="525" y2="126" stroke="currentColor"/><polygon points="520,128 530,130 526,121" fill="currentColor"/> <line x1="470" y1="180" x2="525" y2="134" stroke="currentColor"/><polygon points="520,132 530,130 526,139" fill="currentColor"/> <text x="322" y="98" font-size="12" text-anchor="middle">int8 내용</text> <text x="322" y="172" font-size="12" text-anchor="middle">int8 내용</text>
<text x="85" y="172" font-size="12" text-anchor="middle">key 그대로</text> <text x="85" y="188" font-size="12" text-anchor="middle">a0faca854706</text> <text x="245" y="172" font-size="12" text-anchor="middle">8e5750669f7a</text> <text x="245" y="188" font-size="12" text-anchor="middle">→ 0ae702121747</text> <text x="340" y="236" font-size="12" text-anchor="middle">설정은 quantize 한 곳만 바꿨지만, 아래 stage들은 "입력 파일 내용"이 바뀌어서 함께 무효화된다</text>
</svg>
```

그림 3 — 3회차 실행에서의 캐시 무효화. 회색은 skip, 주황은 다시 실행, 파랑은 매번 생성.

### 7.6 manifest와 결정성

`runs/exp1/manifest.json`의 앞부분이다(`head -12`; 뒤에는 config 전체와 `stage_keys` 네 개가 이어진다).

```text
{
  "git": "f53a027",
  "python": "3.9.6",
  "machine": "arm64",
  "versions": {
    "torch": "2.8.0",
    "onnx": "1.19.1",
    "onnxruntime": "1.19.2",
    "numpy": "2.0.2"
  },
  "config": {
    "seed": 0,
```

이 파일 하나가 있으면 6개월 뒤에도 "이 숫자는 어떤 코드·라이브러리·설정에서 나왔나"에 답할 수 있다. 같이 저장된 `results.csv`는 이렇다.

```text
model,p50_us,p90_us,p99_us,size_B,acc
fp32,5.0,5.08,5.17,59167.0,0.961
int8,5.12,5.46,6.21,19382.0,0.961
```

(`size_B`가 `59167.0`으로 float이 된 것은 `pd.DataFrame(be).T`가 한 열에 int와 float이 섞인 dict를 전치하면서 dtype을 float으로 합쳤기 때문이다. CSV를 읽는 쪽 스크립트가 정수를 기대하면 깨진다 — 리포트 스키마는 명시적으로 고정하는 게 좋다.)

**결정성 확인.** export를 강제로 다시 돌려서 ONNX 파일 hash를 전후로 비교했다.

```sh
shasum -a 256 runs/exp1/model_fp32.onnx | cut -c1-16
python pipe.py run --force export | head -2
shasum -a 256 runs/exp1/model_fp32.onnx | cut -c1-16
```

```text
dc12cc51394e27df
INFO  mlpipe: export   done 1.11s (key=a0faca854706)
INFO  mlpipe: quantize done 0.04s (key=8e5750669f7a)
dc12cc51394e27df
```

출력에서 볼 것: 학습부터 다시 했는데 **바이트 단위로 같은 ONNX**가 나왔다. `torch.manual_seed`와 데이터 seed를 고정했고, CPU 단일 프로세스라서 가능한 결과다. (quantize가 다시 돈 것은 3회차에서 dynamic으로 바꿔 둔 stamp가 남아 있어 key가 달랐기 때문이다.) GPU 학습·멀티스레드 reduction은 덧셈 순서가 바뀌어 비트 단위 결정성이 깨질 수 있다 — 그때는 "bit-exact" 대신 "허용 오차 안"으로 기준을 바꾸고, 필요하면 `torch.use_deterministic_algorithms(True)`를 쓴다.

---

## 8. sweep — 자동화가 돈을 버는 순간

CLI와 캐시가 있으면 "설정 공간을 훑기"가 for 루프 하나가 된다. 무엇을 확인하는 코드인지: calibration 방법 2가지 × 샘플 수 3가지 = 6개 실험을 `pipe.py`를 subprocess로 불러 돌리고 한 표로 모은다.

```python
# ex13_sweep.py — pipe.py를 설정만 바꿔 여러 번 호출 → 결과를 한 표로 (sweep)
import itertools, json, sys
from pathlib import Path
from runner import run

rows = []
for method, n in itertools.product(["MinMax", "Percentile"], [2, 16, 200]):
    wd = f"runs/sweep_{method}_{n}"
    run([sys.executable, "pipe.py", "--set", f"workdir={wd}", "--set", f"quantize.calib_n={n}",
         "--set", f"quantize.calib_method={method}", "run"], timeout_s=300)
    ev = json.loads(Path(wd, "eval.json").read_text())
    rows.append((method, n, ev["acc_int8"], ev["top1_agree"], ev["max_abs_logit_diff"]))
print(f"{'method':10s} {'calib_n':>7s} {'acc_int8':>8s} {'agree':>6s} {'max|dlogit|':>11s}")
for m, n, a, g, d in rows:
    print(f"{m:10s} {n:7d} {a:8.4f} {g:6.4f} {d:11.3f}")
print("fp32 acc:", json.loads(Path(wd, "eval.json").read_text())["acc_fp32"])
```

```text
method     calib_n acc_int8  agree max|dlogit|
MinMax           2   0.9620 0.9990       3.352
MinMax          16   0.9600 0.9990       2.162
MinMax         200   0.9610 0.9990       0.677
Percentile       2   0.9620 0.9990       3.353
Percentile      16   0.9610 1.0000       2.163
Percentile     200   0.9620 0.9980       0.742
fp32 acc: 0.961
```

출력에서 볼 것: **정확도는 6개 실험이 0.960~0.962로 사실상 같다.** 그런데 logit 최대 오차는 calibration 샘플 2개일 때 3.35, 200개일 때 0.68로 약 5배 차이다. 샘플 2개로 잡은 activation 범위가 좁아서 실제 입력이 범위를 넘으면 포화(clipping)되는 것이다(C1의 포화 오차). 이 쉬운 4클래스 문제에서는 argmax가 그 오차를 흡수했지만, 클래스 경계가 가까운 실제 모델에서는 정확도로 터진다. **정확도 하나만 보는 sweep은 위험 신호를 놓친다** — 그래서 eval stage가 logit 차이를 같이 남긴다.

실행 시간은 6개 합쳐 약 12초였다. 그런데 이 sweep은 workdir가 실험마다 달라서 **export(학습)를 6번 반복**했다. 캐시가 workdir 안에 있기 때문이다. 개선안은 산출물을 key 이름으로 공용 저장소에 두는 것이다: `cache/export/a0faca854706.onnx`. 그러면 6개 실험이 같은 fp32 모델을 공유한다 — git의 object store, Bazel·DVC의 content-addressed cache와 같은 아이디어다.

---

## 9. 로그 파싱 1 — 텍스트 로그를 표로

### 9.1 왜

기기 위 추론 결과는 대개 로그로 나온다: UART 콘솔, Android logcat(O2), 혹은 런타임이 찍는 성능 줄(llama.cpp의 `llama_perf_context_print`, F3). 이걸 사람이 눈으로 보면 100줄이 한계다. 파이프라인에서는 **로그 → DataFrame → 통계 → 판정**으로 간다. Don이 SSD 펌웨어 로그(이벤트 로그, SMART, 내부 trace)를 스크립트로 파싱하던 일과 같다.

### 9.2 시뮬레이션 로그 만들기

실기기가 없으니 `ex7_gen_log.py`(seed 고정)로 로그를 생성한다. 규칙은 이렇다.

- 약 5 Hz(간격 0.18~0.22 s 균등분포)로 60초 동안 추론 줄을 찍는다. 70%는 `kws`(기준 820 us), 30%는 `gesture`(기준 310 us), 지연은 기준 × lognormal(0, 0.05).
- 온도 `temp_c`는 36 + t/20으로 천천히 오른다.
- 줄마다 1% 확률로 `E/npu: job timeout code=0x..` 줄, 또 1% 확률로 `lat_us=` 뒤가 잘린 줄(UART 버퍼가 넘쳐 줄 끝이 날아간 것)을 덧붙인다.
- 후보 빌드(cand, seed 11)는 **30초 뒤부터 지연 × 1.18**. 열 스로틀링이나 백그라운드 작업이 시작된 상황의 흉내다. 기준 빌드(base)는 seed 10.

생성된 cand.log의 앞 세 줄(실제 출력):

```text
[00:00.185] I/infer: model=kws lat_us=872 temp_c=36.0
[00:00.371] I/infer: model=gesture lat_us=319 temp_c=36.0
[00:00.589] I/infer: model=kws lat_us=887 temp_c=36.0
```

(`wc -l`로 세면 base.log 306줄, cand.log 310줄이다.)

### 9.3 파서 — 정규식 두 개

한 줄의 구조는 `[분:초.밀리초] 레벨/태그: 메시지`이고, 메시지 안은 `key=value`가 공백으로 나열된다. 그래서 정규식을 두 단계로 나눈다: 줄 틀(`LINE`)과 key=value(`KV`). 한 정규식으로 모든 필드를 잡으려 하면 필드가 하나 추가될 때마다 깨진다.

정규식 조각을 손으로 읽어 보자.

```
\[(?P<m>\d{2}):(?P<s>\d{2}\.\d{3})\]   "[00:01.250]"  → m="00", s="01.250"
 (?P<lvl>[IWE])/(?P<tag>\w+):          " I/infer:"    → lvl="I", tag="infer"
 (?P<msg>.*)                            나머지 전부    → msg="model=kws lat_us=812 temp_c=36.1"
(\w+)=(\S+)  를 msg에 findall           → [("model","kws"), ("lat_us","812"), ("temp_c","36.1")]
```

`(?P<이름>...)`는 named group이다. 번호(`m.group(3)`) 대신 이름(`m["lvl"]`)으로 꺼내니 필드 순서가 바뀌어도 코드가 안전하다.

```python
# logparse.py — 정규식으로 로그 한 줄 → dict. 못 읽은 줄은 세어서 보고 (조용히 버리지 않는다)
import re
import pandas as pd

LINE = re.compile(r"\[(?P<m>\d{2}):(?P<s>\d{2}\.\d{3})\] (?P<lvl>[IWE])/(?P<tag>\w+): (?P<msg>.*)")
KV = re.compile(r"(\w+)=(\S+)")

def parse(path):
    rows, bad = [], []
    for n, line in enumerate(open(path), 1):
        m = LINE.match(line.rstrip("\n"))
        if not m:
            bad.append(n); continue
        kv = dict(KV.findall(m["msg"]))
        if m["tag"] == "infer" and not kv.get("lat_us", "").isdigit():
            bad.append(n); continue                       # 형식은 맞는데 값이 잘린 줄
        rows.append({"t_s": int(m["m"]) * 60 + float(m["s"]), "lvl": m["lvl"],
                     "tag": m["tag"], **kv})
    df = pd.DataFrame(rows)
    df["lat_us"] = pd.to_numeric(df.get("lat_us"), errors="coerce")
    df["temp_c"] = pd.to_numeric(df.get("temp_c"), errors="coerce")
    return df, bad
```

핵심 설계는 **못 읽은 줄을 버리지 않고 줄 번호로 돌려주는 것**이다. 조용히 버리면 "에러 줄이 0개"와 "파서가 에러 줄을 못 읽음"을 구별할 수 없다. 잘린 줄 비율이 갑자기 늘면 그것 자체가 신호다(UART 속도·버퍼 크기 문제).

### 9.4 표로 보기

무엇을 확인하는 코드인지: 두 로그를 파싱해서 모델별 개수·p50·p99와 깨진 줄·에러 수를 비교.

```python
# ex8_logstats.py — 두 빌드 로그를 표로 만들고, 모델별 p50/p99 + 에러 수 비교
from logparse import parse

for name in ["base", "cand"]:
    df, bad = parse(f"{name}.log")
    inf = df[df.tag == "infer"]
    s = inf.groupby("model")["lat_us"].describe(percentiles=[0.5, 0.99])[["count", "50%", "99%"]]
    print(f"== {name}: rows={len(df)} bad_lines={len(bad)} first_bad={bad[:3]} "
          f"npu_errors={int((df.lvl == 'E').sum())}")
    print(s.round(1).to_string())
```

```text
== base: rows=303 bad_lines=3 first_bad=[140, 177, 219] npu_errors=2
         count    50%    99%
model                       
gesture   95.0  309.0  345.2
kws      206.0  818.0  912.8
== cand: rows=304 bad_lines=6 first_bad=[13, 81, 120] npu_errors=2
         count    50%     99%
model                        
gesture   89.0  332.0   394.4
kws      213.0  899.0  1082.0
```

출력에서 볼 것: 행 수를 맞춰 보자 — base는 306줄 = 정상 303 + 깨진 줄 3. 숫자가 맞아떨어져야 파서를 믿을 수 있다. cand의 kws p50은 818 → 899 us(+9.9%)로 올랐다. 그런데 넣은 회귀는 18%였다. 왜 절반만 보일까? 10절에서 답한다.

### 9.5 파서도 테스트한다

로그 형식은 펌웨어 버전마다 조금씩 바뀐다. 파서가 조용히 틀리기 시작하면 그 뒤 모든 리포트가 틀린다. 경계 케이스를 pytest로 고정해 둔다.

```python
# test_logparse.py — 파서도 코드다: 경계 케이스를 pytest로 고정
import pytest
from logparse import parse

@pytest.mark.parametrize("line,ok", [
    ("[00:01.250] I/infer: model=kws lat_us=812 temp_c=36.1", True),
    ("[00:01.250] I/infer: model=kws lat_us=", False),          # 잘린 값
    ("[00:01.250] E/npu: job timeout code=0x0a", True),          # infer가 아니면 lat 없어도 OK
    ("garbage \x00\x13 from uart", False),                        # 깨진 줄
    ("[01:00.000] I/infer: model=gesture lat_us=300 temp_c=39.0", True),
])
def test_line(tmp_path, line, ok):
    f = tmp_path / "x.log"; f.write_text(line + "\n")
    df, bad = parse(f)
    assert (len(df) == 1) == ok and (len(bad) == 0) == ok

def test_minutes_to_seconds(tmp_path):            # "[01:02.500]" → 62.5 s
    f = tmp_path / "x.log"; f.write_text("[01:02.500] I/infer: model=kws lat_us=1 temp_c=1\n")
    assert parse(f)[0].t_s.iloc[0] == pytest.approx(62.5)
```

```sh
python -m pytest -q test_logparse.py
```

```text
......                                                                   [100%]
6 passed in 0.31s
```

출력에서 볼 것: parametrize 5개 + 단위 변환 1개 = 6개. `tmp_path`는 pytest가 테스트마다 만들어 주는 임시 폴더라 테스트끼리 파일이 섞이지 않는다. 이 테스트를 CI에 거는 것은 O3의 영역이다.

---

## 10. 회귀 찾기 — "느려졌나?"를 숫자로

### 10.1 전체 median 비교의 함정

9.4절에서 kws가 +9.9%였는데 넣은 회귀는 +18%였다. 이유는 간단하다: 60초 로그 중 **뒤 30초만** 느려졌다. 전체 median은 느린 절반과 빠른 절반을 섞은 값이라 효과가 희석된다. 손으로 근사하면, 앞 절반 820, 뒤 절반 820 × 1.18 = 968이 섞인 분포의 중앙값은 두 덩어리의 경계 근처(약 880~900)에 놓인다. 출력의 899와 맞는다.

그래서 두 가지를 같이 본다.

- **신뢰구간**: 비율이 우연히 1.05를 넘은 건지, 확실히 넘은 건지. 여기서는 bootstrap을 쓴다 — 표본에서 복원 추출로 같은 크기의 표본을 2000번 다시 만들어 median 비율을 계산하고, 그 분포의 2.5%~97.5% 구간을 본다. 분포 가정이 필요 없어서 지연처럼 꼬리가 긴 데이터에 맞다.
- **시간 창(window)**: 10초 단위로 잘라 창마다 비율을 보면 **언제부터** 느려졌는지 보인다.

판정 규칙(예시): 구간의 아래 끝이 1.05보다 크면 REGRESSION, 위 끝이 1.05보다 작으면 ok, 걸쳐 있으면 unclear(더 측정).

```python
# ex9_regress.py — "느려졌나?"를 숫자로: median 비율 + bootstrap 95% 구간 + 10초 창별 위치
import numpy as np
from logparse import parse

b, _ = parse("base.log"); c, _ = parse("cand.log")
rng = np.random.default_rng(0)
for model in ["kws", "gesture"]:
    x = b[(b.tag == "infer") & (b.model == model)].lat_us.to_numpy()
    y = c[(c.tag == "infer") & (c.model == model)].lat_us.to_numpy()
    boot = [np.median(rng.choice(y, len(y))) / np.median(rng.choice(x, len(x))) for _ in range(2000)]
    lo, hi = np.percentile(boot, [2.5, 97.5])
    ratio = np.median(y) / np.median(x)
    verdict = "REGRESSION" if lo > 1.05 else ("ok" if hi < 1.05 else "unclear")
    print(f"{model:8s} median ratio={ratio:.3f}  95% CI=[{lo:.3f}, {hi:.3f}]  -> {verdict}")

w = lambda df: df[(df.tag == "infer") & (df.model == "kws")].groupby(df.t_s // 10 * 10).lat_us.median()
print("kws median by 10 s window (cand/base):")
print((w(c) / w(b)).dropna().round(3).to_string())
```

```text
kws      median ratio=1.099  95% CI=[1.061, 1.127]  -> REGRESSION
gesture  median ratio=1.074  95% CI=[1.039, 1.143]  -> unclear
kws median by 10 s window (cand/base):
t_s
0.0     1.005
10.0    0.991
20.0    0.979
30.0    1.225
40.0    1.190
50.0    1.191
```

출력에서 볼 것:

- kws는 샘플이 많아(약 210개) 구간이 좁고 아래 끝 1.061 > 1.05 → REGRESSION.
- gesture는 같은 회귀를 겪었지만 샘플이 90개뿐이라 구간이 넓어(1.039~1.143) unclear다. "회귀 없음"이 아니라 "**판단하기엔 데이터가 부족**"이다. 측정 횟수를 늘리라는 신호다.
- 창별 비율은 0~20초 구간에서 1.0 근처, 30초 창부터 1.19~1.23으로 점프한다. 넣은 회귀(30초부터 ×1.18)와 정확히 맞는다. 이 정보가 있으면 "빌드가 느리다"가 아니라 "**30초 뒤 무언가가 시작되면서** 느려진다 → 온도? 백그라운드 작업?"으로 질문이 좁혀진다. 실제로 이 로그의 온도는 t/20씩 오르게 만들었으니, 온도 열과 같이 그려 보는 것이 다음 단계다(K4).

```svg
<svg viewBox="0 0 600 300" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="250" x2="570" y2="250" stroke="currentColor"/> <line x1="60" y1="40" x2="60" y2="250" stroke="currentColor"/> <text x="52" y="254" font-size="12" text-anchor="end">0.9</text> <text x="52" y="204" font-size="12" text-anchor="end">1.0</text> <text x="52" y="154" font-size="12" text-anchor="end">1.1</text>
<text x="52" y="104" font-size="12" text-anchor="end">1.2</text> <text x="52" y="54" font-size="12" text-anchor="end">1.3</text> <line x1="60" y1="200" x2="570" y2="200" stroke="#888" stroke-dasharray="2 4"/> <line x1="60" y1="175" x2="570" y2="175" stroke="#d0564a" stroke-dasharray="6 4"/> <text x="566" y="170" font-size="12" text-anchor="end">임계 1.05</text>
<rect x="350" y="40" width="220" height="210" fill="#e08a3c" fill-opacity="0.12"/> <text x="460" y="58" font-size="12" text-anchor="middle">cand에 넣은 감속 구간 (t ≥ 30 s)</text> <polyline points="80,197.5 170,204.5 260,210.5 350,87.5 440,105 530,104.5" fill="none" stroke="#4a7bd0" stroke-width="2"/> <circle cx="80" cy="197.5" r="4" fill="#4a7bd0"/> <circle cx="170" cy="204.5" r="4" fill="#4a7bd0"/>
<circle cx="260" cy="210.5" r="4" fill="#4a7bd0"/> <circle cx="350" cy="87.5" r="4" fill="#4a7bd0"/> <circle cx="440" cy="105" r="4" fill="#4a7bd0"/> <circle cx="530" cy="104.5" r="4" fill="#4a7bd0"/> <text x="350" y="80" font-size="12" text-anchor="middle">1.225</text>
<text x="260" y="228" font-size="12" text-anchor="middle">0.979</text> <text x="80" y="270" font-size="12" text-anchor="middle">0</text> <text x="170" y="270" font-size="12" text-anchor="middle">10</text> <text x="260" y="270" font-size="12" text-anchor="middle">20</text> <text x="350" y="270" font-size="12" text-anchor="middle">30</text>
<text x="440" y="270" font-size="12" text-anchor="middle">40</text> <text x="530" y="270" font-size="12" text-anchor="middle">50</text> <text x="315" y="292" font-size="12" text-anchor="middle">10초 창 시작 시각 (s)</text> <text x="20" y="145" font-size="12" transform="rotate(-90 20 145)" text-anchor="middle">kws median 비율 cand/base</text>
</svg>
```

그림 4 — ex9의 창별 median 비율. 전체 median(1.099)으로는 "약간 느려짐"이지만, 창으로 자르면 30초부터 약 1.2배로 점프한 계단이 보인다.

### 10.2 이걸 CI 판정으로 만들려면

판정 함수를 CI에 걸어 PR을 막는 일, 기준선(baseline)을 어디에 저장하고 언제 갱신하는지, 여러 기기에서 돌린 결과를 모으는 일은 O3에서 다룬다. 여기서 기억할 것은 판정이 **세 값(REGRESSION / ok / unclear)**이어야 한다는 점이다. 두 값이면 데이터가 부족할 때 거짓 경보나 놓침 중 하나를 고르게 된다.

---

## 11. 로그 파싱 2 — binary telemetry를 `struct`로

### 11.1 펌웨어가 binary로 남기는 이유

Don이 잘 아는 영역이다. 펌웨어는 텍스트 printf 대신 **고정 크기 binary 레코드**로 telemetry를 남긴다: 포맷팅 비용이 없고(ISR 안에서도 안전), 크기가 작고(flash·대역폭 절약), 길이가 고정이라 링버퍼에 넣기 쉽다. 대신 호스트 쪽에서 **레이아웃을 정확히 알아야** 읽을 수 있다.

### 11.2 펌웨어 쪽 — C로 레코드 쓰기

무엇을 확인하는 코드인지: 헤더(magic·version·레코드 크기·개수) + 12바이트 레코드 1000개를 little-endian으로 직렬화하고, 레코드마다 CRC-16을 붙인다. 500번 레코드는 전송 오류를 흉내 내어 비트 하나를 깨뜨린다. 600번부터는 throttle 플래그를 켜고 지연을 +150 us 올린다.

```c
/* tlm_writer.c — 펌웨어가 flash/UART로 내보내는 binary telemetry 흉내 (little-endian 직렬화) */
#include <stdint.h>
#include <stdio.h>

static uint16_t crc16_ccitt(const uint8_t *p, int n) {        /* CRC-16/CCITT-FALSE */
    uint16_t c = 0xFFFF;
    for (int i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)v); put16(p + 2, (uint16_t)(v >> 16)); }

int main(void) {
    FILE *f = fopen("tlm.bin", "wb");
    if (!f) return 1;
    uint8_t hdr[12] = {'T', 'L', 'M', '1'};
    const uint32_t N = 1000;
    put16(hdr + 4, 1); put16(hdr + 6, 12); put32(hdr + 8, N);   /* version, rec_size, count */
    fwrite(hdr, 1, sizeof hdr, f);
    uint32_t seed = 12345;
    for (uint32_t i = 0; i < N; i++) {
        uint8_t r[12];
        seed = seed * 1664525u + 1013904223u;                   /* LCG 난수 */
        uint16_t lat = (uint16_t)(800 + (seed >> 24) % 64 + (i >= 600 ? 150 : 0));
        int16_t temp = (int16_t)(3600 + (int)(i / 4));           /* 0.01 °C 단위 */
        put32(r, i * 200u); put16(r + 4, lat); put16(r + 6, (uint16_t)temp);
        r[8] = (uint8_t)(i % 3 == 0); r[9] = (uint8_t)(i >= 600 ? 0x01 : 0x00);  /* model, flags(throttle) */
        put16(r + 10, crc16_ccitt(r, 10));
        if (i == 500) r[4] ^= 0x10;                              /* 비트 하나 깨뜨리기 (전송 오류 흉내) */
        fwrite(r, 1, sizeof r, f);
    }
    fclose(f);
    printf("wrote %u records, %u bytes\n", N, 12u + 12u * N);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 tlm_writer.c -o tlm_writer && ./tlm_writer && xxd -l 36 tlm.bin
```

```text
wrote 1000 records, 12012 bytes
00000000: 544c 4d31 0100 0c00 e803 0000 0000 0000  TLM1............
00000010: 2503 100e 0100 754a c800 0000 2403 100e  %.....uJ....$...
00000020: 0000 81e0                                ....
```

경고 0개로 컴파일됐다. 구조체를 `fwrite(&rec, sizeof rec, ...)`로 통째로 쓰지 않고 `put16`/`put32`로 바이트를 직접 배치한 이유: 구조체는 컴파일러가 padding을 넣을 수 있고, CPU의 endian을 따른다. 바이트 단위 직렬화는 **어떤 컴파일러·CPU에서도 같은 파일**을 만든다.

### 11.3 hex를 손으로 읽기

```
offset 0x00  54 4c 4d 31        magic "TLM1"
       0x04  01 00              version   = 0x0001 = 1
       0x06  0c 00              rec_size  = 0x000c = 12
       0x08  e8 03 00 00        count     = 0x000003e8 = 1000
record 0 (offset 0x0c):
       00 00 00 00              t_ms      = 0
       25 03                    lat_us    = 0x0325 = 805
       10 0e                    temp_cx100= 0x0e10 = 3600 → 36.00 °C
       01                       model     = 1  (i % 3 == 0)
       00                       flags     = 0
       75 4a                    crc       = 0x4a75
record 1 (offset 0x18):
       c8 00 00 00              t_ms      = 0xc8 = 200
       24 03                    lat_us    = 0x0324 = 804
```

little-endian이라 `25 03`은 낮은 바이트가 먼저 → 0x0325다. 말로 하면: "바이트를 거꾸로 붙여서 읽는다."

```svg
<svg viewBox="0 0 640 250" xmlns="http://www.w3.org/2000/svg">
<text x="40" y="24" font-size="13">레코드 12바이트 (little-endian, padding 없음)</text> <rect x="40" y="40" width="176" height="40" fill="#4a7bd0" fill-opacity="0.3" stroke="currentColor"/> <rect x="216" y="40" width="88" height="40" fill="#e08a3c" fill-opacity="0.3" stroke="currentColor"/> <rect x="304" y="40" width="88" height="40" fill="#3f9a6b" fill-opacity="0.3" stroke="currentColor"/> <rect x="392" y="40" width="44" height="40" fill="#888" fill-opacity="0.3" stroke="currentColor"/>
<rect x="436" y="40" width="44" height="40" fill="#888" fill-opacity="0.3" stroke="currentColor"/> <rect x="480" y="40" width="88" height="40" fill="#d0564a" fill-opacity="0.3" stroke="currentColor"/> <text x="128" y="65" font-size="13" text-anchor="middle">t_ms (u32)</text> <text x="260" y="65" font-size="13" text-anchor="middle">lat_us u16</text> <text x="348" y="65" font-size="13" text-anchor="middle">temp i16</text>
<text x="414" y="65" font-size="12" text-anchor="middle">model</text> <text x="458" y="65" font-size="12" text-anchor="middle">flags</text> <text x="524" y="65" font-size="13" text-anchor="middle">crc u16</text> <text x="40" y="96" font-size="12" text-anchor="middle">0</text> <text x="216" y="96" font-size="12" text-anchor="middle">4</text>
<text x="304" y="96" font-size="12" text-anchor="middle">6</text> <text x="392" y="96" font-size="12" text-anchor="middle">8</text> <text x="436" y="96" font-size="12" text-anchor="middle">9</text> <text x="480" y="96" font-size="12" text-anchor="middle">10</text> <text x="568" y="96" font-size="12" text-anchor="middle">12</text>
<line x1="40" y1="108" x2="480" y2="108" stroke="#d0564a"/> <line x1="40" y1="102" x2="40" y2="114" stroke="#d0564a"/> <line x1="480" y1="102" x2="480" y2="114" stroke="#d0564a"/> <text x="260" y="128" font-size="12" text-anchor="middle">CRC-16/CCITT-FALSE 계산 범위 = 바이트 0..9</text> <polygon points="236,34 240,26 244,34" fill="#d0564a"/>
<text x="250" y="31" font-size="12">레코드 500: 바이트 4의 bit4 뒤집음</text> <text x="40" y="164" font-size="13">같은 필드라도 struct 형식 문자에 따라 크기가 다르다</text> <rect x="40" y="176" width="176" height="28" fill="#4a7bd0" fill-opacity="0.3" stroke="currentColor"/> <rect x="216" y="176" width="44" height="28" fill="#888" fill-opacity="0.3" stroke="currentColor"/> <rect x="260" y="176" width="44" height="28" fill="none" stroke="currentColor" stroke-dasharray="3 3"/>
<rect x="304" y="176" width="88" height="28" fill="#e08a3c" fill-opacity="0.3" stroke="currentColor"/> <text x="128" y="195" font-size="12" text-anchor="middle">I</text> <text x="238" y="195" font-size="12" text-anchor="middle">B</text> <text x="282" y="195" font-size="12" text-anchor="middle">pad</text> <text x="348" y="195" font-size="12" text-anchor="middle">H</text>
<text x="410" y="195" font-size="12">"@IBH" = 8 B (네이티브 정렬)</text> <rect x="40" y="212" width="176" height="28" fill="#4a7bd0" fill-opacity="0.3" stroke="currentColor"/> <rect x="216" y="212" width="44" height="28" fill="#888" fill-opacity="0.3" stroke="currentColor"/> <rect x="260" y="212" width="88" height="28" fill="#e08a3c" fill-opacity="0.3" stroke="currentColor"/> <text x="128" y="231" font-size="12" text-anchor="middle">I</text>
<text x="238" y="231" font-size="12" text-anchor="middle">B</text> <text x="304" y="231" font-size="12" text-anchor="middle">H</text> <text x="410" y="231" font-size="12">"&lt;IBH" = 7 B (패킹, LE)</text>
</svg>
```

그림 5 — 위: 레코드 레이아웃과 CRC 범위, 깨뜨린 비트 위치. 아래: `struct` 형식 문자열의 첫 글자(`@` 네이티브 정렬 vs `<` little-endian 패킹)에 따른 padding 차이.

### 11.4 호스트 쪽 — Python으로 읽기

두 가지 도구를 쓴다.

- `struct.Struct("<4sHHI")`: 헤더처럼 **한 번** 읽는 것. 형식 문자 `<`=little-endian·패킹, `4s`=4바이트 문자열, `H`=u16, `I`=u32, `h`=i16, `B`=u8.
- numpy structured dtype + `np.frombuffer`: 레코드 **수천~수백만 개**를 복사 없이 한 번에. C 구조체 배열을 포인터 캐스팅으로 보는 것과 같다.

무엇을 확인하는 코드인지: 헤더 검증(magic·version·크기) → 레코드를 dtype으로 view → CRC 검사로 깨진 레코드를 걸러냄 → throttle 플래그별 통계.

```python
# ex10_struct.py — binary telemetry 파싱: header 검증 → numpy structured dtype → CRC 검사
import binascii, struct
import numpy as np, pandas as pd

HDR = struct.Struct("<4sHHI")                          # magic, version, rec_size, count (12 B)
REC = np.dtype([("t_ms", "<u4"), ("lat_us", "<u2"), ("temp_cx100", "<i2"),
                ("model", "u1"), ("flags", "u1"), ("crc", "<u2")])
raw = open("tlm.bin", "rb").read()
magic, ver, rsz, n = HDR.unpack_from(raw, 0)
assert magic == b"TLM1" and ver == 1, (magic, ver)
assert rsz == REC.itemsize == 12 and len(raw) == HDR.size + n * rsz, "size mismatch"

rec = np.frombuffer(raw, dtype=REC, count=n, offset=HDR.size)   # 복사 없이 view
body = np.frombuffer(raw, np.uint8, n * rsz, HDR.size).reshape(n, rsz)
crc_ok = np.array([binascii.crc_hqx(r[:10].tobytes(), 0xFFFF) for r in body]) == rec["crc"]
print("header:", magic, ver, rsz, n, "| bad crc at:", np.flatnonzero(~crc_ok).tolist())

df = pd.DataFrame(rec[crc_ok])
df["temp_c"] = df["temp_cx100"] / 100
df["throttle"] = (df["flags"] & 0x01).astype(bool)   # df.flags 는 pandas 속성이라 금지
print(df.groupby("throttle").agg(n=("lat_us", "size"), lat_p50=("lat_us", "median"),
                                 temp_mean=("temp_c", "mean")).round(2).to_string())
print("struct '@IBH' =", struct.calcsize("@IBH"), "B,  '<IBH' =", struct.calcsize("<IBH"), "B")
```

```text
header: b'TLM1' 1 12 1000 | bad crc at: [500]
            n  lat_p50  temp_mean
throttle                         
False     599    831.0      36.74
True      400    982.0      37.99
struct '@IBH' = 8 B,  '<IBH' = 7 B
```

출력에서 볼 것:

- CRC가 정확히 500번 레코드 하나를 잡았다. C 쪽 CRC-16/CCITT-FALSE(다항식 0x1021, 초기값 0xFFFF, 반사 없음)는 Python 표준 `binascii.crc_hqx(data, 0xFFFF)`와 같은 계산이라 999개가 일치했다. 펌웨어 CRC 변형(초기값·반사·최종 XOR)이 하나라도 다르면 **전부** 불일치가 나온다 — 그럴 땐 데이터가 아니라 CRC 파라미터를 의심한다.
- 깨진 1개를 빼고 999개 = throttle 아님 599 + throttle 400. 600번부터 throttle을 켰으니 0~599 중 500번이 빠져 599, 600~999가 400. 숫자가 맞는다.
- throttle 구간 p50이 831 → 982(+151 us). C에서 넣은 +150과 맞는다.
- 처음 이 코드를 짰을 때 `df.flags & 0x01`이라고 썼다가 `TypeError: unsupported operand type(s) for &: 'Flags' and 'int'`가 났다. `DataFrame.flags`는 pandas가 이미 쓰는 속성 이름이라 열 이름 `flags`를 점 표기로 꺼낼 수 없다. **열은 `df["이름"]`으로 꺼내는 습관**이 안전하다.

---

## 12. 외부 툴 출력 파싱 — llama-bench를 subprocess로

F3에서 llama.cpp 구조와 `llama-bench` 지표(pp = prompt processing, tg = token generation, tokens/s)를 다뤘다. 여기서는 그 툴을 **파이프라인의 한 stage로 부르는 법**만 본다. 요령은 사람이 보는 표(`-o md`, 기본값) 대신 **기계가 읽는 형식**(`-o json`)을 고르는 것이다. 표 형식을 정규식으로 파싱하면 툴 버전이 열 하나만 추가해도 깨진다.

무엇을 확인하는 코드인지: SmolLM2-135M Q8_0으로 짧게 한 번(프롬프트 64, 생성 16, 반복 3, 4스레드, `-ngl 0`) 돌리고 JSON을 DataFrame으로.

```python
# ex12_llama_bench.py — llama-bench를 subprocess로 한 번 돌려 JSON 결과를 표로
import json
from pathlib import Path
import pandas as pd
from runner import run

ROOT = Path("/Users/donh/workspace/dons_tech_study_dashboard/dons_study_note_from_experience/"
            "embeddedAIPrepBasedOnHarkAIJD/.tools")
cmd = [str(ROOT / "llama.cpp/build/bin/llama-bench"),
       "-m", str(ROOT / "models/smollm2-135m-Q8_0.gguf"),
       "-p", "64", "-n", "16", "-r", "3", "-t", "4", "-ngl", "0", "-o", "json"]
out = run(cmd, timeout_s=120)               # stdout = JSON만 (Metal 초기화 로그는 stderr)
rows = json.loads(out)
df = pd.DataFrame(rows)
df["test"] = df.apply(lambda r: f"pp{r.n_prompt}" if r.n_gen == 0 else f"tg{r.n_gen}", axis=1)
print(df[["model_type", "n_threads", "test", "avg_ts", "stddev_ts"]].round(1).to_string(index=False))
print("build:", rows[0]["build_commit"], "| keys:", len(rows[0]))
```

```text
     model_type  n_threads test  avg_ts  stddev_ts
llama 256M Q8_0          4 pp64   498.9      141.0
llama 256M Q8_0          4 tg16   240.9       16.2
build: f7b384c | keys: 44
```

출력에서 볼 것:

- `llama-bench`는 Metal 초기화 로그 수십 줄을 **stderr**로, 결과 JSON만 **stdout**으로 낸다. stdout과 stderr를 분리해서 받았기 때문에 `json.loads`가 바로 된다. `2>&1`로 합쳤다면 파싱이 깨진다.
- pp64의 표준편차가 평균의 28%(141/499)다. 반복 3회 중 첫 회가 느렸을 가능성이 크다(캐시·스레드 풀 기동). 이렇게 흔들리는 숫자로 회귀 판정을 하면 안 된다 — 반복 수를 늘리거나 10절의 구간 판정을 쓴다.
- JSON 한 행에 44개 키가 있고 `build_commit`이 들어 있다. 이걸 그대로 manifest에 넣으면 "어떤 llama.cpp 빌드로 잰 숫자인지"가 자동으로 기록된다.
- 이 머신에서는 다른 작업도 같이 돌고 있었으므로 절대값보다 **방법**을 보면 된다.

---

## 13. 임베디드 관점에서 다시 보기

### 13.1 파이프라인의 마지막 stage는 펌웨어 산출물이다

MCU(TFLite Micro, F2)나 RTOS 위 런타임은 파일시스템이 없어서 모델을 **C 배열로 펌웨어에 링크**한다. 흔히 `xxd -i model.tflite`로 만드는데, 파이프라인에서는 이것도 stage로 만들고 **무결성 정보(길이·CRC·hash)와 정렬**을 같이 넣는다.

무엇을 확인하는 코드인지: int8 ONNX 바이트를 16바이트 정렬된 C 배열 헤더로 만들고, 길이·CRC-32·sha256 앞부분을 매크로와 주석으로 박는다.

```python
# ex14_gen_header.py — 파이프라인 마지막 stage: 모델 바이트 → 펌웨어용 C 헤더 (xxd -i 대체)
import hashlib, zlib
from pathlib import Path

src = Path("runs/exp1/model_int8.onnx")
b = src.read_bytes()
crc, sha = zlib.crc32(b), hashlib.sha256(b).hexdigest()[:16]
rows = [", ".join(f"0x{x:02x}" for x in b[i:i + 12]) for i in range(0, len(b), 12)]
Path("model_int8.h").write_text(
    "/* generated by ex14_gen_header.py — DO NOT EDIT */\n"
    f"/* source={src.name} sha256[:16]={sha} */\n"
    "#include <stdint.h>\n"
    f"#define G_MODEL_LEN {len(b)}u\n#define G_MODEL_CRC32 0x{crc:08x}u\n"
    "static const uint8_t g_model[G_MODEL_LEN] __attribute__((aligned(16))) = {\n  "
    + ",\n  ".join(rows) + "\n};\n")
print(f"len={len(b)} crc32=0x{crc:08x} sha256[:16]={sha}")
```

```text
len=19382 crc32=0x697222c0 sha256[:16]=c86fbae0429dbead
```

펌웨어 쪽에서는 부팅 때(또는 OTA로 모델을 바꾼 뒤) CRC를 다시 계산해 비교한다.

```c
/* check_model.c — 부팅 시 모델 blob 무결성 확인 (펌웨어 쪽) */
#include <stdio.h>
#include "model_int8.h"

static uint32_t crc32_sw(const uint8_t *p, uint32_t n) {     /* zlib과 같은 CRC-32 (reflected 0xEDB88320) */
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

int main(void) {
    uint32_t c = crc32_sw(g_model, G_MODEL_LEN);
    printf("len=%u crc=0x%08x expect=0x%08x %s align16=%s\n", G_MODEL_LEN, c, G_MODEL_CRC32,
           c == G_MODEL_CRC32 ? "OK" : "MISMATCH", ((uintptr_t)g_model % 16u) == 0 ? "yes" : "no");
    return c == G_MODEL_CRC32 ? 0 : 1;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 check_model.c -o check_model && ./check_model; echo rc=$?
```

```text
len=19382 crc=0x697222c0 expect=0x697222c0 OK align16=yes
rc=0
```

출력에서 볼 것: Python `zlib.crc32`와 C의 비트 단위 CRC-32가 같은 값 0x697222c0을 냈다(경고 0개). 헤더 주석의 sha256 앞부분은 manifest의 산출물 hash와 대조할 수 있어서, **기기에 올라간 모델 → 파이프라인의 어느 실행에서 나왔나**를 추적하는 고리가 된다. `aligned(16)`은 SIMD 커널이나 DMA가 정렬된 주소를 요구할 때 필요하다(J 모듈, E 모듈).

### 13.2 호스트 파이프라인과 기기의 경계

| 단계 | 어디서 | 이 노트 | 다른 노트 |
|---|---|---|---|
| export·quantize·변환 | 호스트 (Linux/macOS) | 1·2·7절 | C2, F4, F5 |
| 바이너리 배포 | 호스트 → 기기 | 13.1절 (C 헤더) | O2 (adb push), J 모듈 (OTA) |
| 실기기 bench | 기기 | 3절 방법만 | O2 (기기에서 실행), K2, K3 |
| 로그 수집 | 기기 → 호스트 | 9·11절 (파싱) | O2 (logcat), J 모듈 (telemetry) |
| 판정·gating | 호스트 CI | 10절 (판정 함수) | O3 |

기기 쪽 사정이 설계에 주는 영향: 기기 위 bench runner는 Python이 아니라 C/C++ 바이너리(`benchmark_model`, `onnxruntime_perf_test`, `llama-bench`)이고 Python은 호스트에서 그걸 부르고(5.3절) 출력을 파싱(12절)한다 → runner 출력은 JSON/CSV 같은 기계 형식이어야 한다. 양산 telemetry는 11절 같은 binary 레코드이고, 형식이 바뀌면 `version` 필드를 올려 파서가 분기하게 한다. 기기 로그의 시각은 부팅 후 ms라 호스트 시계와 어긋나므로, 두 로그를 맞출 때는 양쪽에 동기화 마커("bench start")를 남긴다.

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 캐시 key에 코드를 안 넣음 | 모델 코드를 고쳤는데 결과가 그대로 | 설정만 hash해서 옛 산출물 재사용 | 모델·전처리 소스 파일 내용을 key 입력에 포함 (7.4절 export) |
| mtime 기반 캐시 | git checkout 뒤 전부 다시 돌거나, 반대로 안 돎 | mtime은 내용과 무관 | 내용 hash (6절) |
| 산출물을 바로 정식 이름으로 씀 | 중간에 죽은 뒤 다음 실행이 깨진 `.onnx`를 skip | 반쯤 쓴 파일이 "존재" | 임시 파일 → `replace` 원자적 rename, stamp는 마지막 |
| `subprocess`에 timeout 없음 | nightly가 아침까지 안 끝남 | 툴이 입력 대기·데드락 | 모든 호출에 `timeout=`, 그룹 kill |
| return code 미검사 | 실패한 변환의 옛 파일로 bench → "좋아졌다" | stdout만 보고 성공 판단 | rc ≠ 0이면 예외 (5.3절 `run`) |
| stderr와 stdout 합침 | JSON 파싱 에러 | 진단 로그가 결과에 섞임 | `capture_output=True`로 분리, 기계 형식은 stdout만 |
| 파서가 못 읽은 줄을 버림 | 에러 0건 리포트인데 실제로는 에러 다수 | 포맷 변경을 조용히 무시 | bad line 수를 리포트하고 비율에 경보 (9.3절) |
| `df.flags` 같은 점 표기 열 접근 | `TypeError` 또는 엉뚱한 값 | pandas 속성과 이름 충돌 | `df["flags"]` (11.4절) |
| 평균·한 번 측정으로 회귀 판정 | 같은 빌드인데 리포트마다 회귀/개선이 오감 | p99·max는 실행마다 크게 흔들림 | p50 + 반복 + 구간(bootstrap), 세 값 판정 (10절) |
| 루트 logger 레벨을 올림 | 내 INFO 로그가 사라짐 | 자식 logger가 루트 레벨을 상속 | 루트는 ERROR, 내 logger에만 레벨 지정 (5.2절) |

---

## 15. 면접에서 이렇게 말한다

**Q.** How would you automate converting, quantizing, and benchmarking a model for a new device?

**A.** stage를 export → quantize → eval → bench → report로 나누고, 각 stage가 "입력 파일 + 설정 → 출력 파일 하나"라는 같은 모양을 갖게 만든다. 그 위에 click CLI와 YAML 설정을 얹어서 명령 한 줄이 실험 하나의 완전한 기록이 되게 한다. 각 stage는 입력 내용과 설정의 hash를 key로 캐시해서 바뀐 곳부터만 다시 돈다. 결과에는 git hash·라이브러리 버전·설정 전체를 담은 manifest를 붙인다. SSD 펌웨어에서 빌드-다운로드-워크로드-QoS 비교 nightly를 돌리던 것과 같은 구조다.

> I split it into stages — export, quantize, evaluate, benchmark, report — and make every stage the same shape: input files plus a config section in, one output file out. A click CLI and a YAML config sit on top, so a single command line fully describes an experiment. Each stage is cached on a hash of its input contents and its parameters, so changing the calibration settings only reruns quantization and everything downstream. Every result ships with a manifest: git hash with a dirty flag, library versions, and the full config. It's the same structure as the SSD firmware nightly I used to run — build, flash, workload, QoS comparison — with export and quantize replacing the build.

**Q.** Why hash file contents instead of using timestamps like make?

**A.** mtime은 내용과 무관하다. 같은 내용을 다시 써도, git checkout을 해도 바뀐다. 그리고 설정 파라미터는 파일이 아니라서 mtime이 없다. 입력 파일 내용 + 정렬된 JSON 설정을 sha256으로 묶으면 둘 다 해결되고, 위 stage의 출력이 같은 바이트면 아래 stage가 자동으로 skip된다. 실제로 seed를 고정해서 export를 다시 돌렸을 때 바이트 단위로 같은 ONNX가 나와서 아래 stage가 유지됐다.

> Timestamps aren't content. Rewriting the same bytes or checking out an old branch changes the mtime, and config parameters don't have an mtime at all. Hashing input contents plus the sorted-JSON config handles both, and it gives you early cutoff for free: if an upstream stage reproduces identical bytes, every downstream stage is still a cache hit. With fixed seeds, re-running my export produced a byte-identical ONNX file, so nothing downstream reran.

**Q.** Your benchmark says the new build is 8% slower. How do you decide if it's real?

**A.** 먼저 측정 조건이 같은지 manifest로 확인한다(스레드 수, 기기, 온도, 빌드). 그다음 평균이 아니라 p50을 보고, 반복 측정에서 bootstrap으로 비율의 신뢰구간을 낸다. 구간 아래 끝이 임계값을 넘으면 회귀, 위 끝이 아래면 ok, 걸치면 unclear로 더 측정한다. 그리고 시간 창으로 잘라 본다. 내 시뮬레이션에서 전체 median은 +10%였지만 창으로 보니 30초부터 +20%로 계단이 생겼다 — 빌드 문제가 아니라 열이나 백그라운드 작업일 수 있다는 단서다.

> First I check the manifests to make sure the conditions match — threads, device, build, thermal state. Then I compare medians rather than means, and bootstrap a confidence interval on the ratio. If the lower bound is above the threshold it's a regression, if the upper bound is below it's fine, and if it straddles I collect more data — a three-way verdict, not a boolean. I also slice by time window: in one simulated log, the overall median was up ten percent, but windowing showed a step to plus twenty percent starting at thirty seconds, which points at thermals or a background task rather than the model.

**Q.** How do you call external vendor tools safely from Python?

**A.** 네 가지 규칙이다. 인자를 list로 넘기고 `shell=True`는 안 쓴다. 모든 호출에 timeout을 건다(멈춘 툴이 nightly 전체를 잡지 않게, watchdog과 같다). return code가 0이 아니면 즉시 예외를 던지고, stderr 마지막 몇 줄과 복사해서 재실행할 수 있는 명령을 같이 담는다. 결과는 사람용 표 대신 JSON 같은 기계 형식을 stdout으로 받고, 진단 로그는 stderr로 분리한다.

> Four rules. Pass arguments as a list, never shell=True. Always set a timeout — a hung converter shouldn't hold the whole nightly; it's a watchdog. Check the return code and raise immediately on non-zero, including the last few lines of stderr and a copy-pasteable command line. And ask the tool for machine-readable output on stdout — JSON rather than its pretty table — while keeping diagnostics on stderr, so parsing doesn't break when the tool adds a column.

**Q.** You get a binary telemetry dump from a device. How do you parse it?

**A.** 펌웨어 쪽 레이아웃 정의(헤더 + 고정 크기 레코드, endian, CRC 정의)부터 확보한다. 헤더는 `struct`로 읽어 magic·version·레코드 크기·개수를 검증하고, 파일 크기가 그것과 맞는지 assert한다. 레코드는 numpy structured dtype으로 `frombuffer` 해서 복사 없이 본다. 레코드마다 CRC를 다시 계산해 깨진 것을 걸러내고 그 개수를 보고한다. 그리고 `@`(네이티브 정렬)가 아니라 `<`(LE 패킹)를 써서 padding 함정을 피한다.

> I start from the firmware's layout definition: header, fixed-size records, endianness, and the exact CRC variant. I read the header with struct and assert the magic, version, record size, and that the file length matches the count. Records go through a numpy structured dtype with frombuffer, so it's a zero-copy view — like casting a pointer to an array of structs. I recompute the CRC per record, drop and report the bad ones, and I always use explicit little-endian packed formats rather than native alignment, because native mode inserts padding the firmware didn't.

---

## 16. 직접 해보기

1. **손계산** — 지연 측정값 `[9, 10, 10, 11, 10, 30, 10, 9, 11, 10]` (us)의 mean, p50, p90(numpy linear)을 구하라. 정답: 정렬 `[9,9,10,10,10,10,10,11,11,30]`, mean = 120/10 = 12.0, p50 = 위치 4.5 → 10.0, p90 = 위치 8.1 → 11 + 0.1 × 19 = 12.9.
2. **손계산** — 바이트열 `90 01 00 00 52 03 c4 0d 00 01 ..`를 11절 레코드 형식으로 읽어 t_ms, lat_us, temp_c, model, flags를 구하라. 정답: t_ms = 0x0190 = 400, lat_us = 0x0352 = 850, temp = 0x0dc4 = 3524 → 35.24 °C, model = 0, flags = 1(throttle).
3. **코드** — `pipe.py`에 `export`, `quantize`, `bench` 하위 명령을 각각 추가해 한 stage만 돌릴 수 있게 하라. `run`은 그 함수들을 순서대로 부르게 고친다. 힌트: stage 본문을 `def stage_quantize(cfg, wd, force)` 같은 일반 함수로 빼고, `@cli.command()` 함수와 `run`이 둘 다 그걸 부르게 한다.
4. **코드** — 8절에서 지적한 "sweep마다 export 반복"을 고쳐라: 산출물을 `cache/<stage>/<key>.<ext>`에 저장하고 workdir에는 링크(또는 복사)만 둔다. 6개 실험에서 export가 몇 번 도는지 로그로 확인하라. 정답: 1번 (seed·export 설정이 같으므로 같은 key).
5. **코드** — 9절 로그에 `W/thermal: level=N` 줄을 추가로 생성하고, 파서가 thermal 이벤트 시각을 뽑아 10절 창별 비율과 같은 축에 놓이게 하라. 회귀 시작 시각과 thermal level 변화 시각의 차이를 출력하라. 힌트: `df[df.tag == "thermal"]`의 `t_s`와 `level`을 `pd.to_numeric`으로 바꾸고, 창 비율이 처음 1.05를 넘는 창 시작 시각과 비교.
6. **코드** — 11절 C 코드의 CRC 초기값을 0x0000으로 바꾸고(XMODEM 변형) Python 파서를 그대로 돌려 보라. 무엇이 보이나? 파서를 어떻게 고치나? 정답: 1000개 전부 bad crc가 된다(데이터가 아니라 CRC 정의 불일치의 전형적 증상). `crc_hqx(data, 0x0000)`으로 고치고, 헤더 `version`에 CRC 변형을 묶어 버전별로 분기한다.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| pipeline stage | 파이프라인 단계 | 입력 파일 + 설정 → 출력 파일 하나를 만드는 단위 |
| idempotent | 멱등 | 여러 번 실행해도 결과·상태가 한 번 실행과 같음 |
| content hash | 내용 해시 | 파일 바이트로 계산한 sha256. mtime과 달리 내용이 같으면 같다 |
| cache key | 캐시 키 | 이 노트에서는 sha256(입력 내용 + 정렬된 설정 JSON) 앞 12자 |
| early cutoff | 조기 차단 | 위 stage 출력이 같은 바이트면 아래 stage가 다시 돌지 않는 성질 |
| content-addressed storage | 내용 주소 저장소 | 산출물을 hash 이름으로 저장 (git object, Bazel·DVC 캐시) |
| atomic rename | 원자적 이름 변경 | 같은 파일시스템에서 `replace`는 중간 상태 없이 한 번에 바뀜 |
| manifest | 실행 기록 | git hash·버전·설정·key를 담은 JSON. 숫자의 출생증명서 |
| dirty | 미커밋 수정 있음 | git hash로 재현할 수 없는 상태 표시 |
| click | Python CLI 라이브러리 | 데코레이터로 명령·옵션·하위 명령 정의 |
| argparse | 표준 CLI 파서 | 의존성 없음, `add_subparsers`로 하위 명령 |
| `yaml.safe_load` | 안전한 YAML 로더 | 기본 타입만 만들고 임의 객체 생성 불가 |
| logger hierarchy | logger 트리 | 이름의 점으로 부모·자식, 레벨은 상속 |
| return code | 종료 코드 | 0 = 성공, 그 외 = 실패 (argparse·click 사용 오류는 2) |
| `TimeoutExpired` | 타임아웃 예외 | `subprocess.run(timeout=)` 초과 시 자식 kill 후 발생 |
| named group | 이름 붙은 정규식 그룹 | `(?P<name>...)`, `m["name"]`으로 꺼냄 |
| bootstrap | 부트스트랩 | 복원 추출 재표본으로 통계량의 신뢰구간을 구하는 방법 |
| structured dtype | 구조화 dtype | numpy에서 C 구조체 배열처럼 필드를 정의한 dtype |
| `struct` 형식 문자 | `<` `@` `H` `I` 등 | `<` = LE 패킹, `@` = 네이티브 정렬(padding 있음) |
| CRC-16/CCITT-FALSE | CRC 변형 | 다항식 0x1021, 초기값 0xFFFF, 반사 없음 = `binascii.crc_hqx(d, 0xFFFF)` |
| calibration reader | 교정 데이터 공급자 | ORT `CalibrationDataReader`, `get_next()`가 dict 또는 None |
| sweep | 설정 훑기 | 설정 조합마다 파이프라인을 돌려 결과를 한 표로 |

---

## 18. 요약 & 체크리스트

edge ML의 반복 작업(export → quantize → eval → bench → report)은 각 stage를 "입력 파일 + 설정 → 출력 파일 하나"로 통일하면 CLI 하나로 묶인다. click과 YAML로 명령 한 줄이 실험 하나의 완전한 기록이 되게 하고, 입력 내용과 설정의 hash로 make처럼 바뀐 곳부터만 다시 돌리며, 임시 파일 + 원자적 rename과 "성공 후 stamp"로 멱등성을 지킨다. 모든 결과에는 git hash·버전·설정을 담은 manifest를 붙인다. 외부 툴은 list 인자·timeout·return code 검사·stderr 분리로 부르고, 결과는 기계 형식으로 받는다. 로그는 정규식(텍스트)이나 `struct`·structured dtype(binary)으로 DataFrame에 넣고, 못 읽은 줄과 CRC 실패를 반드시 센다. 회귀는 p50 비율의 신뢰구간으로 세 값 판정을 하고, 시간 창으로 잘라 "언제부터"를 찾는다. 이 노트의 실측: int8 크기 59,175 → 19,410 B, 정확도 0.9551 → 0.9512(static), 작은 모델에서는 int8이 데스크톱 CPU에서 빠르지 않음(p50 5.0 vs 5.2 us), calibration 2개 vs 200개에서 정확도는 같지만 logit 최대 오차는 3.35 vs 0.68, 창별 분석으로 30초 이후 1.2배 계단 발견.

- [ ] 1.1절 모델의 파라미터 수와 fp32 바이트 수를 손으로 계산할 수 있다
- [ ] 10개 측정값의 p50·p90·p99(linear 보간)를 손으로 계산하고, 평균 대신 p50·p99를 보는 이유를 말할 수 있다
- [ ] argparse와 click으로 하위 명령이 있는 CLI를 만들고, 잘못된 인자가 rc=2로 끝나는 것을 확인할 수 있다
- [ ] 내용 hash 캐시가 mtime 캐시보다 나은 세 가지 이유와 early cutoff를 설명할 수 있다
- [ ] 임시 파일 + `replace` + 성공 후 stamp로 stage를 멱등하게 만들 수 있다
- [ ] manifest에 들어갈 항목을 대고, dirty 플래그가 왜 필요한지 말할 수 있다
- [ ] subprocess 호출의 네 가지 규칙(list, timeout, rc, stderr 분리)을 코드로 쓸 수 있다
- [ ] 로그 한 줄을 named group 정규식으로 파싱하고, 못 읽은 줄 수를 보고하는 파서를 만들 수 있다
- [ ] bootstrap 구간으로 REGRESSION / ok / unclear 세 값 판정을 하고, 시간 창으로 회귀 시작 시점을 찾을 수 있다
- [ ] binary 레코드 hex를 손으로 읽고, `struct`·numpy dtype·CRC 검사로 파싱할 수 있다

---

## 참고 자료

- Python 공식 문서 — [argparse](https://docs.python.org/3/library/argparse.html), [subprocess](https://docs.python.org/3/library/subprocess.html), [logging](https://docs.python.org/3/library/logging.html), [Logging HOWTO](https://docs.python.org/3/howto/logging.html), [struct](https://docs.python.org/3/library/struct.html), [re](https://docs.python.org/3/library/re.html), [pathlib](https://docs.python.org/3/library/pathlib.html), [hashlib](https://docs.python.org/3/library/hashlib.html)
- [Click documentation](https://click.palletsprojects.com/) — groups, options, `CliRunner` 테스트
- [PyYAML documentation](https://pyyaml.org/wiki/PyYAMLDocumentation) — `safe_load`와 `load`의 차이
- [ONNX Runtime — Quantize ONNX models](https://onnxruntime.ai/docs/performance/model-optimizations/quantization.html) — `quantize_dynamic`, `quantize_static`, `CalibrationDataReader`
- [PyTorch — torch.onnx](https://pytorch.org/docs/stable/onnx.html) — TorchScript exporter와 dynamo exporter
- [numpy — Structured arrays](https://numpy.org/doc/stable/user/basics.rec.html), [numpy.percentile](https://numpy.org/doc/stable/reference/generated/numpy.percentile.html) — `method` 인자
- [pandas — User Guide](https://pandas.pydata.org/docs/user_guide/index.html) — groupby, `to_numeric`
- [pytest documentation](https://docs.pytest.org/) — parametrize, `tmp_path`
- [llama.cpp](https://github.com/ggml-org/llama.cpp) — `tools/llama-bench` README (출력 형식 `-o json`)
- Efron & Tibshirani, "An Introduction to the Bootstrap" (Chapman & Hall, 1993) — bootstrap 신뢰구간
- [DVC](https://dvc.org/doc) — ML 파이프라인의 내용 hash 기반 캐시를 제품화한 예 (개념 비교용)
- 이 노트 시리즈: C2(PTQ·QDQ), C8(검증 지표), F3(llama.cpp), F5(ONNX·ORT), K2(프로파일링 방법론), K4(지속 성능·열), O2(온디바이스 Linux/Android), O3(ML CI)
