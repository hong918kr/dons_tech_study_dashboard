# F1. TFLite / LiteRT — 변환기, flatbuffer 모델, interpreter, delegate, int8 변환

> **이 노트를 다 읽으면**: Keras 모델을 `TFLiteConverter`로 float·dynamic·float16·int8·16x8 다섯 가지 `.tflite`로 바꾸고 크기·정확도·SQNR을 직접 비교할 수 있다 · `.tflite` flatbuffer를 Analyzer, Python schema, C 코드로 열어 subgraph·tensor·buffer·operator·opcode를 읽을 수 있다 · int8 모델의 scale/zero-point를 꺼내 입력을 손으로 양자화하고 conv 한 층을 정수 연산으로 비트 단위까지 재현할 수 있다 · builtin/Flex/custom op의 차이, delegate partitioning, XNNPACK의 효과를 실제 출력으로 설명하고 면접에서 "int8 TFLite 변환 절차"를 막힘없이 말할 수 있다
> **JD 연결**: "Embedded ML runtimes (TFLite, llama.cpp, QNN)", "Work with platform vendors to bring up toolchains, SDKs and new accelerator" — study_prep_list F1 행: flatbuffer 모델 포맷, converter / interpreter, **delegate** (XNNPACK, GPU, Hexagon/QNN) / quantized 모델 변환 흐름 (P0)
> **Don 기준 난이도**: 바이너리 포맷 파싱, offset·정렬, 고정소수점 scale, "골든 대비 비트 비교", 벤더 드라이버를 끼우는 구조(HAL)는 이미 강함 / TensorFlow·Keras API, 변환기 옵션, delegate라는 개념, op 호환성 문제는 새로 배움
> **선행 노트**: C1(양자화 수식·TFLite int8 관례 11.1절), C2(PTQ 절차·representative/calibration), C6(그래프 최적화·CPU fallback 12.2절), D2(arena·메모리), B7(IMU 모델)

---

## 0. 큰 그림 — 이게 왜 필요한가

C 모듈에서 "모델을 int8로 줄이는 수학"을, D 모듈에서 "그게 얼마나 빠르고 얼마나 메모리를 먹는지"를 배웠다. 그런데 학습된 모델은 Python 안의 객체일 뿐이다. 기기에서 돌리려면 **(1) 기기가 읽을 수 있는 파일로 굳히고, (2) 그 파일을 실행하는 작은 엔진을 펌웨어/앱에 넣어야** 한다. 이 "파일 + 엔진" 조합을 **런타임(runtime)** 이라고 부른다. 모듈 F는 런타임 이야기이고, F1은 그중 가장 널리 쓰이는 **TensorFlow Lite(TFLite)** — 2024년에 Google이 **LiteRT**라는 이름으로 바꾸기 시작한 것 — 를 다룬다.

JD에 "TFLite"가 이름으로 나온다. 예를 들어 Hark 같은 웨어러블이라면(추정), Android급 SoC 쪽에서는 TFLite/LiteRT interpreter + delegate(Qualcomm NPU 등)로, always-on MCU 쪽에서는 TFLite Micro(F2)로 같은 `.tflite` 파일을 돌리는 구성이 흔하다. 한 포맷이 양쪽을 다 덮는다는 것이 TFLite의 가장 큰 장점이다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f1a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="160" y="18" font-size="13" text-anchor="middle">개발 PC (Python)</text>
<text x="595" y="18" font-size="13" text-anchor="middle">타깃 기기</text>
<line x1="495" y1="25" x2="495" y2="300" stroke="#888" stroke-dasharray="5,4"/>
<rect x="10" y="30" width="140" height="46" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="80" y="50" font-size="13" text-anchor="middle">Keras 모델</text><text x="80" y="67" font-size="12" text-anchor="middle">from_keras_model</text>
<rect x="10" y="100" width="140" height="46" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="80" y="120" font-size="13" text-anchor="middle">TF SavedModel</text><text x="80" y="137" font-size="12" text-anchor="middle">from_saved_model</text>
<rect x="10" y="170" width="140" height="46" rx="6" fill="none" stroke="#888" stroke-dasharray="4,3"/><text x="80" y="190" font-size="13" text-anchor="middle">PyTorch 모델</text><text x="80" y="207" font-size="12" text-anchor="middle">ai-edge-torch (미설치)</text>
<line x1="150" y1="53" x2="188" y2="110" stroke="currentColor" marker-end="url(#f1a)"/><line x1="150" y1="123" x2="188" y2="130" stroke="currentColor" marker-end="url(#f1a)"/><line x1="150" y1="193" x2="188" y2="150" stroke="currentColor" marker-end="url(#f1a)"/>
<rect x="190" y="85" width="140" height="90" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="260" y="110" font-size="13" text-anchor="middle">TFLiteConverter</text><text x="260" y="130" font-size="12" text-anchor="middle">MLIR 변환·op 매핑</text><text x="260" y="147" font-size="12" text-anchor="middle">fusion·constant fold</text><text x="260" y="164" font-size="12" text-anchor="middle">양자화 (PTQ)</text>
<text x="260" y="200" font-size="12" text-anchor="middle">optimizations</text><text x="260" y="216" font-size="12" text-anchor="middle">representative_dataset</text><text x="260" y="232" font-size="12" text-anchor="middle">supported_ops</text>
<line x1="330" y1="130" x2="368" y2="130" stroke="currentColor" marker-end="url(#f1a)"/>
<rect x="370" y="105" width="100" height="50" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="420" y="127" font-size="13" text-anchor="middle">.tflite</text><text x="420" y="144" font-size="12" text-anchor="middle">flatbuffer</text>
<line x1="470" y1="120" x2="518" y2="62" stroke="currentColor" marker-end="url(#f1a)"/><line x1="470" y1="140" x2="518" y2="248" stroke="currentColor" marker-end="url(#f1a)"/>
<rect x="520" y="32" width="150" height="58" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="595" y="55" font-size="13" text-anchor="middle">LiteRT Interpreter</text><text x="595" y="74" font-size="12" text-anchor="middle">Android · Linux · iOS</text>
<line x1="595" y1="90" x2="595" y2="106" stroke="currentColor" marker-end="url(#f1a)"/>
<rect x="520" y="108" width="150" height="80" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/><text x="595" y="128" font-size="13" text-anchor="middle">delegate</text><text x="595" y="146" font-size="12" text-anchor="middle">XNNPACK (CPU)</text><text x="595" y="163" font-size="12" text-anchor="middle">GPU · NNAPI</text><text x="595" y="180" font-size="12" text-anchor="middle">QNN (Qualcomm NPU)</text>
<rect x="520" y="220" width="150" height="58" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="595" y="243" font-size="13" text-anchor="middle">TFLite Micro (F2)</text><text x="595" y="262" font-size="12" text-anchor="middle">Cortex-M · DSP</text>
<text x="420" y="320" font-size="12" text-anchor="middle">같은 .tflite 파일이 큰 SoC와 MCU 양쪽에서 쓰인다 (MCU는 지원 op·int8 제약이 더 강하다)</text>
</svg>
```

그림 1 — TFLite/LiteRT 배포 경로. 왼쪽(PC)에서 모델을 변환기에 넣어 `.tflite` flatbuffer 파일 하나를 만들고, 오른쪽(기기)에서 interpreter가 그 파일을 실행한다. 큰 SoC에서는 interpreter가 일부 연산을 delegate(가속기 드라이버)에 넘기고, MCU에서는 TFLite Micro가 같은 포맷을 읽는다.

펌웨어 비유로 보면 구조가 바로 보인다.

| TFLite 구성요소 | 펌웨어 세계의 비슷한 것 | 한 줄 설명 |
|---|---|---|
| `TFLiteConverter` | 컴파일러 + 링커 | 고수준 그래프를 "기기가 실행할 수 있는 op 목록"으로 낮추고 상수를 박아 넣는다 |
| `.tflite` 파일 | 링크된 바이너리 이미지 (`.bin`) | 코드(op 순서) + 데이터(가중치) + 메타데이터(텐서 shape, scale)를 한 파일에 |
| Interpreter | 부트로더 + 작은 스케줄러 | 파일을 읽고, op 구현(커널)을 찾아 연결하고, 메모리를 잡고, 순서대로 실행 |
| OpResolver | 함수 포인터 테이블 (vector table) | "opcode 3 = CONV_2D → 이 함수" 매핑 |
| Delegate | HAL 아래의 하드웨어 가속 드라이버 | 지원하는 부분 그래프를 통째로 가져가 GPU/NPU/SIMD로 실행 |
| Tensor arena | 링커 스크립트로 잡은 정적 RAM 영역 | 모든 중간 텐서를 한 덩어리 메모리에서 재사용 (D2, C6 10절) |

이 노트의 순서: TFLite/LiteRT가 무엇인지(1절) → 실습 모델 준비(2절) → float 변환과 Interpreter 기본 루프(3절) → `.tflite` 파일 해부(4절) → PTQ 다섯 가지 비교(5절) → int8 모델 해부와 정수 산술 검증(6절) → op 호환성·signature·dynamic shape(7절) → delegate와 partitioning(8절) → 벤치마크(9절) → Android/C++ 통합 스케치(10절).

---

## 1. TFLite / LiteRT란 무엇인가

### 1.1 이름 정리 — TFLite, LiteRT, TFLM

- **TensorFlow Lite (TFLite)**: 2017년 무렵 TensorFlow Mobile의 후속으로 나온 온디바이스 추론 런타임. 모델 포맷(`.tflite`), 변환기, interpreter, delegate로 구성된다.
- **LiteRT**: 2024년 9월에 Google이 "TensorFlow Lite를 LiteRT(Lite Runtime)로 이름을 바꾼다"고 발표했다. TensorFlow뿐 아니라 PyTorch·JAX에서 온 모델도 같은 런타임으로 돌린다는 방향을 이름에 반영한 것이다. 새 Python 패키지는 `ai-edge-litert`(`from ai_edge_litert.interpreter import Interpreter`)이고, Android 쪽 패키지 이름도 바뀌는 중이다. 이름·패키지 경계는 아직 이동 중이므로 **실제 프로젝트에서는 그 시점의 공식 문서(ai.google.dev/edge/litert)를 확인**하자. 파일 포맷(`.tflite`)과 핵심 개념은 그대로다.
- **TensorFlow Lite for Microcontrollers (TFLM)**: 같은 `.tflite`를 malloc·OS 없이 돌리는 MCU용 별도 구현. F2에서 다룬다.

이 노트의 환경에서는 `ai_edge_litert` 패키지가 TF와 함께 로드되지 않아, TF 2.20 안의 `tf.lite.Interpreter`를 쓴다. 실행하면 TF가 이런 경고를 stderr로 낸다 (실제 출력 일부).

```text
UserWarning:     Warning: tf.lite.Interpreter is deprecated and is scheduled for deletion in
    TF 2.20. Please use the LiteRT interpreter from the ai_edge_litert package.
INFO: Created TensorFlow Lite XNNPACK delegate for CPU.
```

말로 하면: "Python용 interpreter는 앞으로 `ai_edge_litert`로 옮겨 가지만, API(`allocate_tensors`, `set_tensor`, `invoke`, `get_tensor`)는 같다." 두 번째 줄은 8절에서 다룰 **XNNPACK delegate가 기본으로 켜졌다**는 뜻이다. 이 노트의 모든 예제는 이런 stderr 잡음을 `TF_CPP_MIN_LOG_LEVEL=3`, `python -W ignore`, `2>/dev/null`로 숨기고 stdout만 보여 준다.

### 1.2 PyTorch에서 오는 길 — ai-edge-torch (개념만)

Hark 같은 팀의 모델 연구자는 PyTorch를 쓸 가능성이 높다. PyTorch 모델을 `.tflite`로 바꾸는 길은 대략 세 가지다.

| 경로 | 흐름 | 비고 |
|---|---|---|
| ai-edge-torch | `torch.export` 그래프 → TFLite flatbuffer 직접 | Google의 공식 경로 (2024~). 이 환경에는 **설치되어 있지 않아 실행하지 않았다** |
| ONNX 경유 | PyTorch → ONNX(F5) → onnx2tf 등 서드파티 → TFLite | 레이아웃(NCHW→NHWC) 변환이 끼어 transpose가 남기 쉽다 (C6 7절) |
| Keras로 재작성 | 구조를 Keras로 다시 짜고 가중치 복사 | 작은 모델(IMU, KWS)이면 의외로 가장 확실 |

어느 길이든 마지막은 이 노트의 3~8절과 같다: `.tflite`가 나오면 op 목록과 양자화 파라미터를 확인하고, 원본과 숫자를 비교하고, 타깃에서 delegate가 얼마나 가져가는지 본다.

### 1.3 다른 런타임과의 자리 비교

| 런타임 | 모델 파일 | 주 타깃 | 이 노트와의 관계 |
|---|---|---|---|
| TFLite / LiteRT | `.tflite` (flatbuffer) | Android, Linux, iOS, MCU(TFLM) | 이 노트 |
| TFLite Micro | 같은 `.tflite` | Cortex-M, DSP, RISC-V MCU | F2 |
| llama.cpp | `.gguf` | CPU/GPU LLM | F3 |
| Qualcomm QNN | context binary | Hexagon NPU(HTP), Adreno GPU | F4 — TFLite에서는 QNN **delegate**로 붙는다 |
| ONNX Runtime | `.onnx` (protobuf) | 서버~모바일, execution provider | F5 — "EP"가 TFLite의 delegate와 같은 개념 |
| ExecuTorch, Core ML | `.pte`, `.mlpackage` | 모바일 | F6 |

---

## 2. 실습 준비 — 합성 IMU 제스처와 두 개의 Keras 모델

C2에서 쓴 것과 비슷한 **합성 IMU 제스처** 데이터를 TensorFlow 쪽에서 다시 만든다. 3클래스(0=정지, 1=흔들기, 2=두드리기), 50 Hz로 64샘플(1.28초) × 3축(가속도) 창이다. 웨어러블에서 "손목을 흔들었나 / 톡톡 쳤나"를 구분하는 상황을 흉내 낸 것이다.

모든 실습 파일은 스크래치 폴더 `/private/tmp/claude-501/f1/`에서 만들고 실행했다 (`.tflite` 파일도 거기 둔다). 실행 명령은 다음 형태다.

```sh
cd /private/tmp/claude-501/f1
TF_CPP_MIN_LOG_LEVEL=3 <노트폴더>/.venv-tf/bin/python -W ignore ex1_train.py 2>/dev/null
```

TensorFlow 2.20은 메인 `.venv`(PyTorch)와 충돌을 피하려고 **별도 venv `.venv-tf`** 에 설치되어 있다 (Python 3.9.6, TF 2.20.0, Keras 3.10.0, numpy 2.0.2).

데이터 생성 코드 — 공용 모듈 `f1_data.py`.

```python
# 합성 IMU 제스처 데이터: 3클래스 (0=정지, 1=흔들기, 2=두드리기), 창 64샘플 x 3축
import numpy as np

def make_imu(n, seed=0):
    rng = np.random.default_rng(seed)
    t = np.arange(64) / 50.0                      # 50 Hz, 1.28 s 창
    X = np.zeros((n, 64, 3), np.float32)
    y = rng.integers(0, 3, n)
    for i in range(n):
        x = 0.25 * rng.standard_normal((64, 3))
        x[:, 2] += 1.0                              # 중력 (z축 1 g)
        if y[i] == 1:                               # 흔들기: 2~4 Hz 사인
            f = rng.uniform(2, 4); a = rng.uniform(0.3, 0.8)
            x[:, 0] += a * np.sin(2 * np.pi * f * t + rng.uniform(0, 6.28))
        elif y[i] == 2:                             # 두드리기: 짧은 펄스 2번
            for k in rng.choice(np.arange(5, 59), 2, replace=False):
                x[k:k + 3, 2] += rng.uniform(0.8, 1.6)
        X[i] = x
    return X, y.astype(np.int64)
```

이제 두 개의 모델을 학습한다. **small**(579 파라미터)은 MCU급 크기이고, **big**(6만 4천 파라미터)은 5절에서 "양자화하면 파일이 얼마나 줄어드나"를 보기 위한 것이다. small은 파라미터가 너무 적어서 파일 크기의 대부분이 메타데이터가 되기 때문이다(3절에서 확인).

예제 1 — 두 Keras 모델을 학습하고 저장한다 (`ex1_train.py`).

```python
import os; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import numpy as np, tensorflow as tf
from f1_data import make_imu
L = tf.keras.layers
Xtr, ytr = make_imu(1200, seed=1); Xte, yte = make_imu(300, seed=2)

def small():   # 579 params — MCU급
    return [L.Conv1D(8, 5, activation="relu", name="conv1"), L.MaxPooling1D(2, name="pool1"),
            L.Conv1D(16, 3, activation="relu", name="conv2"),
            L.GlobalAveragePooling1D(name="gap"), L.Dense(3, name="logits")]
def big():     # 수만 params — 크기 비교용
    return [L.Conv1D(32, 5, activation="relu", name="conv1"), L.MaxPooling1D(2, name="pool1"),
            L.Conv1D(64, 5, activation="relu", name="conv2"), L.MaxPooling1D(2, name="pool2"),
            L.Flatten(name="flat"), L.Dense(64, activation="relu", name="fc1"),
            L.Dense(3, name="logits")]

for name, layers in [("imu_cnn", small), ("imu_big", big)]:
    tf.keras.utils.set_random_seed(0)
    model = tf.keras.Sequential([tf.keras.Input(shape=(64, 3), name="imu")] + layers())
    model.compile("adam", tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True),
                  metrics=["accuracy"])
    model.fit(Xtr, ytr, epochs=40, batch_size=32, verbose=0)
    acc = model.evaluate(Xte, yte, verbose=0)[1]
    print(f"{name}: params={model.count_params()}  test acc={acc:.4f}")
    model.save(f"{name}.keras")
np.save("Xte.npy", Xte); np.save("yte.npy", yte); np.save("Xcal.npy", Xtr[:200])
```

```text
imu_cnn: params=579  test acc=0.9567
imu_big: params=64323  test acc=0.9933
```

출력에서 볼 것: small은 95.67%, big은 99.33%. 이 숫자가 이후 모든 TFLite 변환의 **골든(기준값)** 이다. `Xcal.npy`(학습 데이터 앞 200개)는 5절에서 representative dataset으로 쓴다 — 테스트 데이터가 아니라 학습 쪽 데이터에서 뽑는 것이 원칙이다(테스트셋을 calibration에 쓰면 평가가 오염된다, A4의 data leakage).

big의 파라미터 64,323개를 손으로 나눠 보면 이렇다 (5절 크기 계산에 쓴다).

```
conv1 : 5·3·32  + 32 =    512
conv2 : 5·32·64 + 64 = 10,304
fc1   : 832·64  + 64 = 53,312    (832 = 13 시간스텝 × 64 채널, Flatten)
logits: 64·3    + 3  =    195
합계                  = 64,323
```

말로 하면: 파라미터의 83%가 `fc1` 하나에 있다. Flatten 뒤의 Dense가 큰 모델의 전형적인 "가중치 덩어리"다 (B2, D2와 같은 이야기).

---

## 3. 변환과 실행 — float `.tflite`와 Interpreter의 기본 루프

### 3.1 Interpreter의 일생

`.tflite`를 실행하는 절차는 어느 언어(Python, C++, Java, TFLM의 C++)든 똑같은 다섯 단계다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f1b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="40" width="110" height="64" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="65" y="62" font-size="13" text-anchor="middle">① 로드</text><text x="65" y="80" font-size="12" text-anchor="middle">flatbuffer를</text><text x="65" y="96" font-size="12" text-anchor="middle">mmap / 배열로</text>
<rect x="140" y="40" width="120" height="64" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="200" y="62" font-size="13" text-anchor="middle">② OpResolver</text><text x="200" y="80" font-size="12" text-anchor="middle">opcode → 커널</text><text x="200" y="96" font-size="12" text-anchor="middle">함수 포인터</text>
<rect x="280" y="40" width="120" height="64" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/><text x="340" y="62" font-size="13" text-anchor="middle">③ delegate 적용</text><text x="340" y="80" font-size="12" text-anchor="middle">지원 부분 그래프를</text><text x="340" y="96" font-size="12" text-anchor="middle">노드 하나로 교체</text>
<rect x="420" y="40" width="120" height="64" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="480" y="62" font-size="13" text-anchor="middle">④ allocate</text><text x="480" y="80" font-size="12" text-anchor="middle">shape 전파·Prepare</text><text x="480" y="96" font-size="12" text-anchor="middle">arena 계획·할당</text>
<line x1="120" y1="72" x2="138" y2="72" stroke="currentColor" marker-end="url(#f1b)"/><line x1="260" y1="72" x2="278" y2="72" stroke="currentColor" marker-end="url(#f1b)"/><line x1="400" y1="72" x2="418" y2="72" stroke="currentColor" marker-end="url(#f1b)"/>
<rect x="560" y="20" width="110" height="34" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="615" y="42" font-size="12" text-anchor="middle">⑤ set_tensor</text>
<rect x="560" y="78" width="110" height="34" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="615" y="100" font-size="12" text-anchor="middle">⑥ invoke</text>
<rect x="560" y="136" width="110" height="34" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="615" y="158" font-size="12" text-anchor="middle">⑦ get_tensor</text>
<line x1="540" y1="60" x2="558" y2="40" stroke="currentColor" marker-end="url(#f1b)"/><line x1="615" y1="54" x2="615" y2="76" stroke="currentColor" marker-end="url(#f1b)"/><line x1="615" y1="112" x2="615" y2="134" stroke="currentColor" marker-end="url(#f1b)"/>
<path d="M 670,153 C 679,153 679,37 671,37" fill="none" stroke="currentColor" marker-end="url(#f1b)"/>
<text x="615" y="190" font-size="12" text-anchor="middle">매 추론마다 반복</text>
<text x="200" y="150" font-size="12" text-anchor="middle">①~④는 한 번 (부팅/앱 시작 시)</text>
<text x="200" y="168" font-size="12" text-anchor="middle">resize_tensor_input 후에는 ④를 다시</text>
<path d="M 480,104 L 480,215 L 200,215 L 200,180" fill="none" stroke="#888" stroke-dasharray="4,3"/>
<text x="340" y="235" font-size="12" text-anchor="middle">TFLM도 같은 단계: MicroMutableOpResolver → MicroInterpreter(arena) → AllocateTensors → Invoke</text>
</svg>
```

그림 2 — Interpreter의 일생. 로드·op 연결·delegate 적용·메모리 할당은 시작할 때 한 번만 하고, 추론마다 "입력 복사 → invoke → 출력 복사"만 반복한다. 펌웨어로 치면 ①~④는 init 함수, ⑤~⑦은 주기 태스크다.

각 단계의 뜻:

1. **로드**: 파일 바이트를 메모리에 올린다. flatbuffer는 **파싱 없이 그 자리에서 읽히는(zero-copy)** 포맷이라, mmap하거나 flash의 const 배열을 그대로 가리키면 된다 (4절).
2. **OpResolver**: 파일에는 "CONV_2D" 같은 **opcode 번호만** 있다. 그 번호를 실제 C 함수(커널)와 연결하는 테이블이 resolver다. 전체 커널을 다 넣는 `BuiltinOpResolver`가 기본이고, TFLM에서는 필요한 op만 등록해 코드 크기를 줄인다 (F2).
3. **delegate 적용**: delegate가 "이 노드들은 내가 하겠다"고 하면 그 부분 그래프가 노드 하나(`DELEGATE`)로 바뀐다 (8절).
4. **allocate_tensors**: 모든 텐서의 shape을 확정하고, 각 커널의 `Prepare`를 호출하고(scratch 크기 계산 등), 텐서 생존 기간을 보고 **arena** 안에 배치한다 (C6 10절, D2).
5. **set_tensor**: 입력 텐서 버퍼에 데이터를 복사한다.
6. **invoke**: 실행 계획(execution plan)의 노드를 순서대로 실행한다.
7. **get_tensor**: 출력 텐서를 복사해 온다.

### 3.2 float 변환과 실행 — Keras와 숫자가 같은가

예제 2 — small 모델을 float `.tflite`로 변환하고, Interpreter로 300개 테스트 샘플을 돌려 Keras 출력과 비교한다 (`ex2_float.py`).

```python
import os; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import numpy as np, tensorflow as tf
model = tf.keras.models.load_model("imu_cnn.keras")
Xte = np.load("Xte.npy"); yte = np.load("yte.npy")

conv = tf.lite.TFLiteConverter.from_keras_model(model)   # ① 변환기
tfl = conv.convert()                                       # ② bytes (flatbuffer)
open("imu_f32.tflite", "wb").write(tfl)
print("tflite bytes:", len(tfl), " params x 4 =", model.count_params() * 4)

it = tf.lite.Interpreter(model_path="imu_f32.tflite")     # ③ interpreter
it.allocate_tensors()                                       # ④ arena 계획/할당
inp = it.get_input_details()[0]; out = it.get_output_details()[0]
print("input :", inp["name"], inp["shape"], inp["dtype"].__name__)
print("output:", out["name"], out["shape"], out["dtype"].__name__)

y_tfl = np.zeros((len(Xte), 3), np.float32)
for i in range(len(Xte)):                                   # batch 1로 한 개씩
    it.set_tensor(inp["index"], Xte[i:i+1])                 # ⑤ 입력 복사
    it.invoke()                                             # ⑥ 실행
    y_tfl[i] = it.get_tensor(out["index"])[0]               # ⑦ 출력 복사
y_k = model.predict(Xte, verbose=0)
print("max |keras - tflite| = %.3e" % np.abs(y_k - y_tfl).max())
print("argmax agree: %d / %d" % ((y_k.argmax(1) == y_tfl.argmax(1)).sum(), len(Xte)))
print("tflite acc: %.4f" % (y_tfl.argmax(1) == yte).mean())
```

```text
Saved artifact at '/var/folders/l7/71gn9zsx7m90tv0d5wgw_fsc0000gn/T/tmppndc5e33'. The following endpoints are available:

* Endpoint 'serve'
  args_0 (POSITIONAL_ONLY): TensorSpec(shape=(None, 64, 3), dtype=tf.float32, name='imu')
Output Type:
  TensorSpec(shape=(None, 3), dtype=tf.float32, name=None)
Captures:
  4656746336: TensorSpec(shape=(), dtype=tf.resource, name=None)
  4656861952: TensorSpec(shape=(), dtype=tf.resource, name=None)
  4657102160: TensorSpec(shape=(), dtype=tf.resource, name=None)
  4657125680: TensorSpec(shape=(), dtype=tf.resource, name=None)
  4657235040: TensorSpec(shape=(), dtype=tf.resource, name=None)
  4657234160: TensorSpec(shape=(), dtype=tf.resource, name=None)
tflite bytes: 6360  params x 4 = 2316
input : serving_default_imu:0 [ 1 64  3] float32
output: StatefulPartitionedCall_1:0 [1 3] float32
max |keras - tflite| = 5.245e-06
argmax agree: 300 / 300
tflite acc: 0.9567
```

출력에서 볼 것:

- 맨 위의 `Saved artifact at ...` 덩어리는 오류가 아니다. Keras 3에서 `from_keras_model`은 **내부적으로 모델을 임시 SavedModel로 export한 뒤** 그걸 변환한다. Captures 6개는 가중치 변수 6개(conv1 W·b, conv2 W·b, dense W·b)다. 이후 예제에서는 `contextlib.redirect_stdout`으로 숨긴다.
- `max |keras − tflite| = 5.2e-06`: float끼리인데 0이 아닌 이유는 **덧셈 순서**가 다르기 때문이다 (Keras는 TF 커널, TFLite는 XNNPACK 커널. float 덧셈은 결합법칙이 성립하지 않는다 — A1·C8). 1e-5 수준이면 "같은 모델"로 본다. 1e-2 이상이면 변환이나 전처리에 버그가 있다는 신호다.
- 정확도 0.9567, argmax 300/300 일치 — 골든과 같다.
- **파일 6,360 바이트인데 가중치는 2,316 바이트(579×4)뿐**이다. 나머지 4 KB는 텐서 이름, shape, op 목록, signature 같은 메타데이터다. 작은 MCU 모델에서는 이 "메타데이터 세금"이 무시 못 할 비율이 된다 (4절 Analyzer가 정확히 나눠 준다).
- 입력 이름 `serving_default_imu:0`, shape `[1, 64, 3]`: Keras의 batch 축 `None`이 **기본 1로 고정**되어 할당된다. 다른 batch는 7.3절의 `resize_tensor_input`으로 바꾼다.

### 3.3 함정

- `set_tensor`에 넘기는 배열은 dtype·shape이 **정확히** 맞아야 한다. float64 numpy 배열을 넣으면 `ValueError: Cannot set tensor: Got value of type FLOAT64 but expected type FLOAT32`가 난다 (이 환경에서 확인. numpy 기본이 float64라 흔히 걸린다).
- `get_tensor`는 복사본을 준다. 대신 `it.tensor(idx)()`는 **내부 버퍼를 가리키는 view**라 복사가 없지만, 그 view를 들고 있는 동안 `allocate_tensors`나 `resize`를 하면 "There is at least 1 reference to internal data" `RuntimeError`가 난다 (이 환경에서 확인). 펌웨어로 치면 DMA가 아직 버퍼를 쥐고 있는데 그 메모리를 재할당하려는 것과 같다.
- 변환된 모델의 출력 이름(`StatefulPartitionedCall_1:0`)은 읽기 어렵다. 이름 대신 **signature**(`'imu'` → `'output_0'`)를 쓰는 습관이 좋다 (7.3절).

---

## 4. `.tflite` 파일 해부 — flatbuffer 안에 무엇이 있나

### 4.1 flatbuffer란 — 파싱 없는 바이너리

**FlatBuffers**는 Google의 직렬화 라이브러리다. protobuf(ONNX가 쓰는 포맷)는 읽을 때 메모리에 객체를 새로 만드는 **파싱**이 필요하지만, flatbuffer는 **파일 안에 offset으로 연결된 테이블**이 들어 있어서 바이트 배열을 그대로 두고 offset을 따라가며 읽는다. 그래서:

- 로드 시 추가 RAM이 거의 없다 — MCU에서 모델을 **flash에 둔 채(XIP)** 실행할 수 있는 이유다 (D2 1.3절).
- 가중치 배열은 파일 안의 바이트를 커널이 **그대로 포인터로** 읽는다.
- 대신 읽기 전용에 가깝고, 정렬과 엔디언(little-endian)이 포맷에 박혀 있다.

Don이 SSD 펌웨어에서 다뤘을 "헤더 + offset 테이블 + payload" 구조의 메타데이터 블록(예: 부트 이미지 헤더, 로그 페이지)과 같은 발상이다.

스키마(`tensorflow/lite/schema/schema.fbs`)의 핵심 테이블만 추리면 다음과 같다.

| 테이블 | 주요 필드 | 뜻 |
|---|---|---|
| `Model` (root) | `version`, `operator_codes`, `subgraphs`, `description`, `buffers`, `metadata`, `signature_defs` | 파일 전체. version은 현재 3 |
| `OperatorCode` | `builtin_code`, `custom_code`, `version` | 이 모델이 쓰는 op 종류 목록 (중복 없는 표) |
| `SubGraph` | `tensors`, `inputs`, `outputs`, `operators`, `name` | 실행 그래프 하나. 보통 `main` 하나, control flow가 있으면 여러 개 |
| `Tensor` | `shape`, `shape_signature`, `type`, `buffer`, `name`, `quantization` | 텐서 메타데이터. 데이터는 `buffer` 번호로 간접 참조 |
| `QuantizationParameters` | `scale[]`, `zero_point[]`, `quantized_dimension`, `min[]`, `max[]` | per-tensor면 길이 1, per-channel이면 채널 수 |
| `Operator` | `opcode_index`, `inputs[]`, `outputs[]`, `builtin_options` | 노드 하나. 입출력은 **텐서 번호**, 옵션은 stride·padding·fused activation 등 |
| `Buffer` | `data[]` (+ 큰 모델용 `offset`, `size`) | raw 바이트. buffer 0은 관례상 **빈 sentinel** |

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f1c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="20" width="170" height="200" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="95" y="42" font-size="13" text-anchor="middle">Model (root table)</text>
<text x="22" y="68" font-size="12">version = 3</text><text x="22" y="92" font-size="12">operator_codes [6]</text><text x="22" y="116" font-size="12">subgraphs [1]</text><text x="22" y="140" font-size="12">buffers [26]</text><text x="22" y="164" font-size="12">signature_defs [1]</text><text x="22" y="188" font-size="12">metadata, description</text>
<rect x="240" y="20" width="190" height="90" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="335" y="40" font-size="13" text-anchor="middle">operator_codes</text><text x="250" y="60" font-size="12">0 EXPAND_DIMS  1 CONV_2D</text><text x="250" y="78" font-size="12">2 RESHAPE  3 MAX_POOL_2D</text><text x="250" y="96" font-size="12">4 MEAN  5 FULLY_CONNECTED</text>
<rect x="240" y="130" width="190" height="150" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="335" y="150" font-size="13" text-anchor="middle">subgraphs[0] "main"</text><text x="250" y="174" font-size="12">tensors [23]</text><text x="262" y="191" font-size="12">shape · type · buffer · quant</text><text x="250" y="215" font-size="12">operators [11]</text><text x="262" y="232" font-size="12">opcode_index · inputs · outputs</text><text x="250" y="256" font-size="12">inputs [0] → outputs [22]</text>
<rect x="490" y="20" width="180" height="260" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/><text x="580" y="40" font-size="13" text-anchor="middle">buffers</text><text x="500" y="66" font-size="12">[0] 빈 sentinel</text><text x="500" y="90" font-size="12">[1] 입력 T#0 (빈 buffer)</text><text x="500" y="114" font-size="12">[2] conv2 bias 64 B</text><text x="500" y="138" font-size="12">[8] conv2 W 1536 B</text><text x="500" y="162" font-size="12">[9] conv1 W 480 B</text><text x="500" y="186" font-size="12">  = 파일 offset 792</text><text x="500" y="210" font-size="12">...</text><text x="500" y="240" font-size="12">raw 바이트, 커널이</text><text x="500" y="258" font-size="12">포인터로 직접 읽음</text>
<line x1="180" y1="88" x2="238" y2="65" stroke="currentColor" marker-end="url(#f1c)"/><line x1="180" y1="112" x2="238" y2="160" stroke="currentColor" marker-end="url(#f1c)"/><path d="M 180,136 C 330,300 420,300 488,240" fill="none" stroke="currentColor" marker-end="url(#f1c)"/>
<line x1="430" y1="186" x2="488" y2="158" stroke="#d0564a" stroke-dasharray="4,3" marker-end="url(#f1c)"/><text x="459" y="200" font-size="12" text-anchor="middle">buffer</text>
<line x1="335" y1="208" x2="335" y2="113" stroke="#e08a3c" stroke-dasharray="4,3" marker-end="url(#f1c)"/><text x="380" y="124" font-size="12">opcode_index</text>
<rect x="10" y="330" width="60" height="34" fill="none" stroke="currentColor"/><text x="40" y="351" font-size="12" text-anchor="middle">root=28</text>
<rect x="70" y="330" width="60" height="34" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="100" y="351" font-size="12" text-anchor="middle">"TFL3"</text>
<rect x="130" y="330" width="250" height="34" fill="none" stroke="currentColor"/><text x="255" y="351" font-size="12" text-anchor="middle">vtable · table · vector (offset으로 연결)</text>
<rect x="380" y="330" width="290" height="34" fill="none" stroke="#d0564a" stroke-width="2"/><text x="525" y="351" font-size="12" text-anchor="middle">buffer 데이터 (가중치) · 메타데이터</text>
<text x="10" y="322" font-size="12">바이트 0</text><text x="70" y="322" font-size="12">4</text><text x="130" y="322" font-size="12">8</text><text x="640" y="322" font-size="12">6360</text>
<text x="340" y="390" font-size="12" text-anchor="middle">small float 모델 (imu_cnn_f32.tflite) 기준 실제 값</text>
</svg>
```

그림 3 — `.tflite` 파일 구조. root `Model` 테이블이 opcode 표, subgraph, buffer 목록을 가리키고, subgraph 안의 operator는 opcode를 **번호(opcode_index)** 로, tensor는 데이터를 **buffer 번호**로 간접 참조한다. 아래 띠는 파일 바이트 배치: 앞 4바이트가 root offset, 그다음 4바이트가 파일 식별자 `TFL3`.

### 4.2 Analyzer로 사람이 읽을 수 있게 보기

TF 2.20에는 `tf.lite.experimental.Analyzer`가 있다 (`hasattr` 확인: True). 파일을 텍스트로 풀어 준다.

예제 3 — small float 모델을 Analyzer로 덤프한다 (`ex3_analyze.py`).

```python
import os; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import tensorflow as tf
tf.lite.experimental.Analyzer.analyze(model_path="imu_f32.tflite")
```

```text
=== imu_f32.tflite ===

Your TFLite model has '1' subgraph(s). In the subgraph description below,
T# represents the Tensor numbers. For example, in Subgraph#0, the EXPAND_DIMS op takes
tensor #0 and tensor #11 as input and produces tensor #12 as output.

Subgraph#0 main(T#0) -> [T#22]
  Op#0 EXPAND_DIMS(T#0, T#11[-3]) -> [T#12]
  Op#1 CONV_2D(T#12, T#8, T#2) -> [T#13]
  Op#2 RESHAPE(T#13, T#6[-1, 60, 8]) -> [T#14]
  Op#3 EXPAND_DIMS(T#14, T#10[1]) -> [T#15]
  Op#4 MAX_POOL_2D(T#15) -> [T#16]
  Op#5 RESHAPE(T#16, T#5[-1, 30, 8]) -> [T#17]
  Op#6 EXPAND_DIMS(T#17, T#11[-3]) -> [T#18]
  Op#7 CONV_2D(T#18, T#7, T#1) -> [T#19]
  Op#8 RESHAPE(T#19, T#4[-1, 28, 16]) -> [T#20]
  Op#9 MEAN(T#20, T#10[1]) -> [T#21]
  Op#10 FULLY_CONNECTED(T#21, T#9, T#3) -> [T#22]

Tensors of Subgraph#0
  T#0(serving_default_imu:0) shape_signature:[-1, 64, 3], type:FLOAT32
  T#1(arith.constant) shape:[16], type:FLOAT32 RO 64 bytes, buffer: 2, data:[-0.0309749, 0.194996, 0.219947, 0.182453, 0.159688, ...]
  T#2(arith.constant1) shape:[8], type:FLOAT32 RO 32 bytes, buffer: 3, data:[-0.162487, 0.178688, 0.452862, -0.0388063, 0.161691, ...]
  T#3(arith.constant2) shape:[3], type:FLOAT32 RO 12 bytes, buffer: 4, data:[0.199025, -0.156413, -0.059518]
  T#4(arith.constant3) shape:[3], type:INT32 RO 12 bytes, buffer: 5, data:[-1, 28, 16]
  T#5(arith.constant4) shape:[3], type:INT32 RO 12 bytes, buffer: 6, data:[-1, 30, 8]
  T#6(arith.constant5) shape:[3], type:INT32 RO 12 bytes, buffer: 7, data:[-1, 60, 8]
  T#7(arith.constant6) shape:[16, 1, 3, 8], type:FLOAT32 RO 1536 bytes, buffer: 8, data:[-0.122103, -0.278758, 0.118233, -0.292175, -0.308886, ...]
  T#8(arith.constant7) shape:[8, 1, 5, 3], type:FLOAT32 RO 480 bytes, buffer: 9, data:[-0.539693, 0.0129603, 0.0160085, -0.639624, -0.0505464, ...]
  T#9(arith.constant8) shape:[3, 16], type:FLOAT32 RO 192 bytes, buffer: 10, data:[0.15628, 0.201661, 0.344208, 0.132418, 0.209709, ...]
  T#10(arith.constant9) shape:[], type:INT32 RO 4 bytes, buffer: 11, data:[1]
  T#11(arith.constant10) shape:[], type:INT32 RO 4 bytes, buffer: 12, data:[-3]
  T#12(sequential_1/conv1_1/convolution/ExpandDims) shape_signature:[-1, 1, 64, 3], type:FLOAT32
  T#13(sequential_1/conv1_1/Relu;sequential_1/conv1_1/BiasAdd;sequential_1/conv1_1/convolution/Squeeze;;sequential_1/conv1_1/convolution) shape_signature:[-1, 1, 60, 8], type:FLOAT32
  T#14(sequential_1/conv1_1/Relu;sequential_1/conv1_1/BiasAdd;sequential_1/conv1_1/convolution/Squeeze;) shape_signature:[-1, 60, 8], type:FLOAT32
  T#15(sequential_1/pool1_1/MaxPool1d/ExpandDims) shape_signature:[-1, 1, 60, 8], type:FLOAT32
  T#16(sequential_1/pool1_1/MaxPool1d) shape_signature:[-1, 1, 30, 8], type:FLOAT32
  T#17(sequential_1/pool1_1/MaxPool1d/Squeeze) shape_signature:[-1, 30, 8], type:FLOAT32
  T#18(sequential_1/conv2_1/convolution/ExpandDims) shape_signature:[-1, 1, 30, 8], type:FLOAT32
  T#19(sequential_1/conv2_1/Relu;sequential_1/conv2_1/BiasAdd;sequential_1/conv2_1/convolution/Squeeze;;sequential_1/conv2_1/convolution) shape_signature:[-1, 1, 28, 16], type:FLOAT32
  T#20(sequential_1/conv2_1/Relu;sequential_1/conv2_1/BiasAdd;sequential_1/conv2_1/convolution/Squeeze;) shape_signature:[-1, 28, 16], type:FLOAT32
  T#21(sequential_1/gap_1/Mean) shape_signature:[-1, 16], type:FLOAT32
  T#22(StatefulPartitionedCall_1:0) shape_signature:[-1, 3], type:FLOAT32

---------------------------------------------------------------
Your TFLite model has '1' signature_def(s).

Signature#0 key: 'serving_default'
- Subgraph: Subgraph#0
- Inputs: 
    'imu' : T#0
- Outputs: 
    'output_0' : T#22

---------------------------------------------------------------
              Model size:       6360 bytes
    Non-data buffer size:       3888 bytes (61.13 %)
  Total data buffer size:       2472 bytes (38.87 %)
    (Zero value buffers):          0 bytes (00.00 %)

* Buffers of TFLite model are mostly used for constant tensors.
  And zero value buffers are buffers filled with zeros.
  Non-data buffers area are used to store operators, subgraphs and etc.
  You can find more details from https://github.com/tensorflow/tensorflow/blob/master/tensorflow/lite/schema/schema.fbs
```

이 출력 하나에서 배울 것이 많다. 하나씩 읽자.

1. **Conv1D는 TFLite에 없다.** Keras의 `Conv1D`가 `EXPAND_DIMS → CONV_2D → RESHAPE` 세 op으로 바뀌었다. 입력 `[N, 64, 3]`을 `[N, 1, 64, 3]`(높이 1인 이미지)로 늘려서 2D conv로 계산하고 다시 접는다. `MaxPooling1D`도 같은 식으로 `MAX_POOL_2D`가 됐다. 이 "접었다 폈다"가 8절에서 delegate를 세 조각으로 쪼개는 원인이 된다.
2. **fusion이 이미 되어 있다.** Keras의 conv(+bias)+ReLU가 `CONV_2D` 하나다. 텐서 13의 이름 `.../Relu;.../BiasAdd;...`가 "이 텐서는 여러 원래 op가 합쳐진 결과"라는 흔적이다. 커널의 `builtin_options`에 `fused_activation_function = RELU`가 들어간다 (C6 4절의 fusion을 변환기가 해 준 것).
3. **가중치 layout은 OHWI다.** conv1 filter가 `[8, 1, 5, 3]` = [출력 채널, 높이, 너비, 입력 채널]. Keras 커널은 `(5, 3, 8)` = [너비, 입력, 출력]이었다(예제 4에서 비트 단위로 확인). TFLite의 activation은 **NHWC**다 (C6 7절).
4. **GlobalAveragePooling1D → MEAN**, Dense → `FULLY_CONNECTED` (가중치 `[3, 16]` = [출력, 입력]).
5. **`shape_signature: [-1, 64, 3]`**: −1은 batch 축이 동적이라는 표시다. 실제 할당된 `shape`은 `[1, 64, 3]`.
6. **RO 상수** (`arith.constant…`): 가중치뿐 아니라 reshape의 목표 shape(`[-1, 60, 8]`) 같은 작은 int32 상수도 buffer를 하나씩 차지한다.
7. **크기 분해**: 데이터 2,472 B(가중치 2,316 B + shape 상수들) vs 비데이터 3,888 B (61%). small 모델은 파일의 대부분이 "구조 설명"이다.

### 4.3 스키마를 직접 따라가기 — Python

Analyzer는 요약일 뿐이다. 펌웨어 엔지니어라면 바이트가 실제로 어디 있는지 보고 싶다. TF 패키지 안에 스키마에서 생성된 Python 클래스(`schema_py_generated`)가 들어 있다 — **공개 API가 아닌 내부 모듈**이라 버전마다 경로가 바뀔 수 있지만, 공부용으로는 훌륭하다.

예제 4 — flatbuffer를 스키마 클래스로 따라가며 opcode 표, 첫 CONV_2D의 입출력, filter 바이트의 **파일 offset**을 찾는다 (`ex4_flatbuf.py`).

```python
import os; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import struct, numpy as np
from tensorflow.lite.python import schema_py_generated as fb   # 내부 모듈 (공개 API 아님)
buf = open("imu_f32.tflite", "rb").read()
root_off, = struct.unpack_from("<I", buf, 0)
print("root table offset:", root_off, " file_identifier:", buf[4:8])
m = fb.Model.GetRootAsModel(buf, 0)
print("schema version:", m.Version(), " subgraphs:", m.SubgraphsLength(),
      " buffers:", m.BuffersLength(), " opcodes:", m.OperatorCodesLength())
names = {v: k for k, v in fb.BuiltinOperator.__dict__.items() if not k.startswith("_")}
print("opcode table:", [names[m.OperatorCodes(i).BuiltinCode()] for i in range(m.OperatorCodesLength())])
sg = m.Subgraphs(0)
print("tensors:", sg.TensorsLength(), " operators:", sg.OperatorsLength())
op = sg.Operators(1)                                   # Op#1 = 첫 CONV_2D
print("Op#1 opcode_index:", op.OpcodeIndex(), " inputs:", op.InputsAsNumpy().tolist(),
      " outputs:", op.OutputsAsNumpy().tolist())
t = sg.Tensors(op.Inputs(1))                           # 두 번째 입력 = filter
b = m.Buffers(t.Buffer())
w = b.DataAsNumpy()                                     # 파일 안 바이트를 그대로 가리키는 view
off = buf.find(w.tobytes())
print("filter tensor:", t.Name().decode(), t.ShapeAsNumpy().tolist(), "-> buffer", t.Buffer(),
      " bytes:", w.size, " file offset:", off, " %4 =", off % 4, " %16 =", off % 16)
print("first 3 floats:", np.frombuffer(w.tobytes(), "<f4")[:3])
print("buffer 0 size:", m.Buffers(0).DataLength(), "(빈 sentinel)")
import tensorflow as tf
k = tf.keras.models.load_model("imu_cnn.keras").get_layer("conv1").kernel.numpy()  # (k=5, in=3, out=8)
tfl_w = np.frombuffer(w.tobytes(), "<f4").reshape(8, 1, 5, 3)                       # OHWI
print("keras kernel", k.shape, "== tflite OHWI 변환?",
      np.array_equal(k.transpose(2, 0, 1)[:, None], tfl_w))
```

```text
root table offset: 28  file_identifier: b'TFL3'
schema version: 3  subgraphs: 1  buffers: 26  opcodes: 6
opcode table: ['EXPAND_DIMS', 'CONV_2D', 'RESHAPE', 'MAX_POOL_2D', 'MEAN', 'FULLY_CONNECTED']
tensors: 23  operators: 11
Op#1 opcode_index: 1  inputs: [12, 8, 2]  outputs: [13]
filter tensor: arith.constant7 [8, 1, 5, 3] -> buffer 9  bytes: 480  file offset: 792  %4 = 0  %16 = 8
first 3 floats: [-0.53969336  0.01296028  0.01600846]
buffer 0 size: 0 (빈 sentinel)
keras kernel (5, 3, 8) == tflite OHWI 변환? True
```

출력에서 볼 것:

- 파일 바이트 4~7이 `TFL3` — flatbuffer의 **file identifier**다. 펌웨어에서 모델 blob을 받으면 이 4바이트부터 검사하는 것이 첫 번째 sanity check다.
- operator는 op 이름을 직접 갖지 않고 `opcode_index: 1`만 갖는다 → opcode 표의 1번 = `CONV_2D`. 11개 op이 6종류 opcode를 공유한다 (문자열 중복 저장을 피하는 **간접 참조**).
- op 입력 `[12, 8, 2]` = [입력 activation, filter, bias]의 **텐서 번호**. CONV_2D는 항상 이 순서다.
- filter 480 B가 파일 offset 792에 있다. 4바이트 정렬은 되어 있지만 16바이트 정렬은 아니다(`%16 = 8`). SIMD 커널이 정렬을 요구하는 환경이라면 이 점을 의식해야 한다 — TFLM에서 모델 배열에 `alignas(16)`을 붙이는 관례는 F2에서 다룬다.
- buffer는 26개인데 텐서는 23개다. buffer 0은 빈 sentinel이고, 텐서 i는 buffer i+1을 가리킨다(이 파일 기준). 데이터가 없는 activation 텐서들도 각자 **빈** buffer를 갖는다. 남는 buffer 24·25(16 B, 96 B)는 텐서가 아니라 `metadata`(최소 런타임 버전 같은 변환 정보)가 쓴다.
- Keras 커널 `(5, 3, 8)`을 `transpose(2, 0, 1)` 후 높이 축 1을 끼우면 TFLite filter와 **비트 단위로 같다**. 변환기는 가중치를 재배열만 했지 값을 바꾸지 않았다 (float 변환이니까).

### 4.4 C로 같은 일을 — 펌웨어가 모델 blob을 검사하는 방법

flatbuffer의 규칙은 단순하다: (1) 파일 첫 4바이트 = root 테이블까지의 offset. (2) 테이블 첫 4바이트 = **vtable까지의 부호 있는 거리**(table − vtable). (3) vtable = `[vtable 크기 u16][table 크기 u16][필드0 offset u16][필드1 offset u16]...`, offset 0이면 필드 없음. (4) vector 필드는 자기 위치 기준 offset을 따라가면 `[길이 u32][원소...]`. 이걸로 Model의 필드 0(version), 1(operator_codes), 2(subgraphs), 4(buffers)를 읽어 보자.

예제 5 — C로 `.tflite`의 식별자·version·vector 길이를 읽는다 (`tfl_peek.c`, `cc -std=c11 -Wall -Wextra -O2`, 경고 0개).

```c
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t blob[1 << 20];                      /* 펌웨어라면 flash 상의 const 배열 */
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; } /* LE */
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

/* table의 field id번째 필드 위치 (없으면 NULL) */
static const uint8_t *field(const uint8_t *table, int id) {
    const uint8_t *vt = table - (int32_t)rd32(table);   /* soffset: table - vtable */
    uint16_t vt_size = rd16(vt);
    if (4 + 2 * id >= vt_size) return NULL;
    uint16_t off = rd16(vt + 4 + 2 * id);
    return off ? table + off : NULL;
}
static uint32_t vec_len(const uint8_t *f) { return rd32(f + rd32(f)); } /* uoffset → [len][...] */

int main(int argc, char **argv) {
    FILE *fp = fopen(argc > 1 ? argv[1] : "imu_f32.tflite", "rb");
    if (!fp) return 1;
    size_t n = fread(blob, 1, sizeof blob, fp); fclose(fp);
    if (n < 8 || memcmp(blob + 4, "TFL3", 4) != 0) { puts("not a tflite model"); return 1; }
    const uint8_t *model = blob + rd32(blob);          /* root table */
    printf("size=%zu root=%u version=%u\n", n, rd32(blob), rd32(field(model, 0)));
    printf("operator_codes=%u subgraphs=%u buffers=%u\n",
           vec_len(field(model, 1)), vec_len(field(model, 2)), vec_len(field(model, 4)));
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 tfl_peek.c -o tfl_peek
./tfl_peek imu_f32.tflite && ./tfl_peek imu_big_int8.tflite && ./tfl_peek f1_data.py
```

(`imu_big_int8.tflite`는 5절 예제 6에서 만든다. 이 출력은 5절까지 실행한 뒤 돌린 결과다.)

```text
size=6360 root=28 version=3
operator_codes=6 subgraphs=1 buffers=26
size=74344 root=28 version=3
operator_codes=5 subgraphs=1 buffers=32
not a tflite model
```

출력에서 볼 것: Python 예제 4와 숫자(version 3, opcode 6, buffer 26)가 정확히 같다. 50줄도 안 되는 C로 모델 blob을 **malloc 없이, 복사 없이** 검사할 수 있다는 것이 flatbuffer의 요점이다. 실제 TFLM은 이 일을 FlatBuffers 라이브러리가 생성한 헤더(`schema_generated.h`)로 하고, 추가로 **verifier**(offset이 범위 밖을 가리키지 않는지 검사)를 돌릴 수 있다. 신뢰할 수 없는 경로(OTA)로 받은 모델은 verifier를 통과시키는 것이 안전하다 — 펌웨어 이미지의 CRC·서명 검사와 같은 이유다.

함정: `memcpy`로 읽은 이유는 **비정렬 접근** 때문이다. Cortex-M0+처럼 비정렬 load가 fault를 내는 코어에서 `*(uint32_t*)p`로 읽으면 HardFault가 난다. flatbuffer 내부 필드는 대개 정렬되어 있지만, blob 자체를 비정렬 주소에 두면 다 깨진다.

---

## 5. Post-training quantization — 다섯 가지 변환을 나란히

### 5.1 TFLite 변환기의 양자화 메뉴

C2에서 배운 PTQ를 TFLite 변환기에서는 **변환기 속성 몇 개의 조합**으로 고른다. 이 조합을 외워 두면 면접의 "int8 TFLite 변환 절차" 질문에 바로 답할 수 있다.

| 이름 | 변환기 설정 | 가중치 | activation | 입출력 | 대표 데이터 | 언제 |
|---|---|---|---|---|---|---|
| float32 (기본) | 아무것도 안 함 | fp32 | fp32 | fp32 | 불필요 | 골든, GPU |
| dynamic-range | `optimizations=[Optimize.DEFAULT]` | int8 (대칭) | fp32 저장, 일부 op이 실행 중 int8로 동적 양자화 | fp32 | 불필요 | CPU에서 크기 ¼, 쉬운 첫 단계 |
| float16 | 위 + `target_spec.supported_types=[tf.float16]` | fp16 | fp32 (GPU delegate는 fp16 가능) | fp32 | 불필요 | GPU delegate, 크기 ½ |
| full-integer int8 | 위 + `representative_dataset` + `supported_ops=[TFLITE_BUILTINS_INT8]` + `inference_input_type=tf.int8` (+ output) | int8 per-channel 대칭 | int8 per-tensor 비대칭 | int8 | **필요** | **MCU, NPU, DSP — edge 배포의 기본** |
| 16x8 (실험적) | 위에서 `supported_ops=[EXPERIMENTAL_TFLITE_BUILTINS_ACTIVATIONS_INT16_WEIGHTS_INT8]` | int8 | **int16 대칭** | 기본은 fp32 (int16 지정 가능) | 필요 | int8로 정확도가 모자랄 때(오디오 등), 지원 op 제한 |

각 설정의 뜻:

- `optimizations = [tf.lite.Optimize.DEFAULT]`: "양자화를 해도 된다"는 스위치. 다른 정보가 없으면 dynamic-range가 된다.
- `representative_dataset`: **calibration 데이터 생성기**. 매번 `[입력 배열]` 리스트를 yield하는 함수다. 변환기는 이 입력들로 float 모델을 돌려 **모든 activation 텐서의 min/max를 기록**하고, 그 범위로 activation의 scale·zero-point를 정한다 (C1 9절의 calibration). 라벨은 필요 없다. 100~500개가 흔하다.
- `target_spec.supported_ops = [TFLITE_BUILTINS_INT8]`: "int8 커널이 없는 op이 있으면 **float으로 남기지 말고 변환을 실패시켜라**." 이걸 빼면 변환기는 양자화 못 한 op를 조용히 float으로 두고 그 앞뒤에 QUANTIZE/DEQUANTIZE를 끼운다 — PC에서는 돌지만 int8 전용 NPU/MCU에서는 못 돈다. **MCU/NPU 타깃이면 반드시 켠다.**
- `inference_input_type / inference_output_type = tf.int8`: 모델의 입출력 자체를 int8로. 안 하면 입력은 float이고 첫 op이 QUANTIZE다. MCU에서는 센서 데이터를 펌웨어가 직접 int8로 만들어 넣는 편이 싸다.

### 5.2 손으로 먼저 — big 모델의 크기 예상

2절의 파라미터 64,323개로 예상해 보자.

```
float32 : 64,323 × 4 B = 257,292 B ≈ 251 KB
float16 : 64,323 × 2 B = 128,646 B ≈ 126 KB
int8    : 가중치 64,323 − 163(bias) = 64,160 × 1 B
          + bias 163 × 4 B (int32)   =   652 B
          + per-channel scale (채널당 float 1개 + zp)  ≈ 수백 B
          ≈ 65 KB
```

말로 하면: int8은 float의 약 ¼, float16은 ½. 여기에 3절에서 본 메타데이터 몇 KB가 더해진다.

### 5.3 코드 — 다섯 가지 변환과 정확도·SQNR

예제 6 — 두 모델 × 다섯 모드로 변환하고, 파일 크기·입력 dtype·정확도·Keras와의 argmax 일치율·출력 SQNR을 잰다 (`ex5_ptq.py`).

```python
import os, io, contextlib; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import numpy as np, tensorflow as tf
Xte, yte, Xcal = np.load("Xte.npy"), np.load("yte.npy"), np.load("Xcal.npy")

def rep_data():                       # representative dataset: 대표 입력 100개
    for i in range(100):
        yield [Xcal[i:i+1]]

def convert(model, mode):
    c = tf.lite.TFLiteConverter.from_keras_model(model)
    if mode != "f32":
        c.optimizations = [tf.lite.Optimize.DEFAULT]
    if mode == "f16":
        c.target_spec.supported_types = [tf.float16]
    if mode in ("int8", "16x8"):
        c.representative_dataset = rep_data
    if mode == "int8":
        c.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
        c.inference_input_type = tf.int8; c.inference_output_type = tf.int8
    if mode == "16x8":
        c.target_spec.supported_ops = [tf.lite.OpsSet.
            EXPERIMENTAL_TFLITE_BUILTINS_ACTIVATIONS_INT16_WEIGHTS_INT8]
    with contextlib.redirect_stdout(io.StringIO()):   # Keras 3의 "Saved artifact" 출력 숨김
        return c.convert()

def run(tfl):
    it = tf.lite.Interpreter(model_content=tfl); it.allocate_tensors()
    i, o = it.get_input_details()[0], it.get_output_details()[0]
    si, zi = i["quantization"]; so, zo = o["quantization"]
    ys = []
    for x in Xte:
        x = x[None]
        if i["dtype"] == np.int8:                         # 입력 양자화 (C1: q = round(x/s)+z)
            x = np.clip(np.round(x / si) + zi, -128, 127).astype(np.int8)
        it.set_tensor(i["index"], x); it.invoke()
        y = it.get_tensor(o["index"])[0].astype(np.float32)
        if o["dtype"] == np.int8:                         # 출력 역양자화: x = s(q-z)
            y = so * (y - zo)
        ys.append(y)
    return np.array(ys), i["dtype"].__name__

def sqnr(ref, y):   # C1 3절: 신호 전력 / 오차 전력 (dB)
    return 10 * np.log10((ref**2).mean() / ((ref - y)**2).mean() + 1e-30)

for name in ["imu_cnn", "imu_big"]:
  model = tf.keras.models.load_model(f"{name}.keras"); y_ref = model.predict(Xte, verbose=0)
  for mode in ["f32", "dynamic", "f16", "int8", "16x8"]:
    tfl = convert(model, mode); open(f"{name}_{mode}.tflite", "wb").write(tfl)
    y, dt = run(tfl)
    print(f"{name} {mode:8s} bytes={len(tfl):5d} in={dt:8s} acc={(y.argmax(1)==yte).mean():.4f} "
          f"agree={(y.argmax(1)==y_ref.argmax(1)).mean():.4f} SQNR={sqnr(y_ref, y):5.1f} dB")
```

```text
imu_cnn f32      bytes= 6360 in=float32  acc=0.9567 agree=1.0000 SQNR=133.8 dB
imu_cnn dynamic  bytes= 6360 in=float32  acc=0.9567 agree=1.0000 SQNR=133.8 dB
imu_cnn f16      bytes= 5972 in=float32  acc=0.9567 agree=1.0000 SQNR= 70.9 dB
imu_cnn int8     bytes= 5928 in=int8     acc=0.9533 agree=0.9967 SQNR= 37.5 dB
imu_cnn 16x8     bytes= 6904 in=float32  acc=0.9633 agree=0.9933 SQNR= 41.5 dB
imu_big f32      bytes=262208 in=float32  acc=0.9933 agree=1.0000 SQNR=138.4 dB
imu_big dynamic  bytes=73360 in=float32  acc=0.9933 agree=1.0000 SQNR= 54.7 dB
imu_big f16      bytes=134548 in=float32  acc=0.9933 agree=1.0000 SQNR= 76.8 dB
imu_big int8     bytes=74344 in=int8     acc=0.9933 agree=1.0000 SQNR= 29.8 dB
imu_big 16x8     bytes=77072 in=float32  acc=0.9933 agree=1.0000 SQNR= 31.4 dB
```

(예제 코드는 30줄 기준보다 길지만, 다섯 가지 설정을 한눈에 비교하려고 한 파일로 두었다.)

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<text x="230" y="22" font-size="13" text-anchor="middle">파일 크기 (KB) — imu_big</text>
<text x="555" y="22" font-size="13" text-anchor="middle">출력 SQNR (dB)</text>
<line x1="80" y1="35" x2="80" y2="240" stroke="currentColor"/><line x1="470" y1="35" x2="470" y2="240" stroke="currentColor"/>
<text x="70" y="62" font-size="12" text-anchor="end">float32</text><rect x="80" y="48" width="300" height="22" fill="#888"/><text x="386" y="64" font-size="12">256.1</text>
<text x="70" y="102" font-size="12" text-anchor="end">dynamic</text><rect x="80" y="88" width="83.9" height="22" fill="#4a7bd0"/><text x="169" y="104" font-size="12">71.6</text>
<text x="70" y="142" font-size="12" text-anchor="end">float16</text><rect x="80" y="128" width="153.9" height="22" fill="#3f9a6b"/><text x="240" y="144" font-size="12">131.4</text>
<text x="70" y="182" font-size="12" text-anchor="end">int8</text><rect x="80" y="168" width="85.1" height="22" fill="#e08a3c"/><text x="171" y="184" font-size="12">72.6</text>
<text x="70" y="222" font-size="12" text-anchor="end">16x8</text><rect x="80" y="208" width="88.2" height="22" fill="#d0564a"/><text x="174" y="224" font-size="12">75.3</text>
<rect x="470" y="48" width="148.3" height="22" fill="#888"/><text x="624" y="64" font-size="12">138.4</text>
<rect x="470" y="88" width="58.6" height="22" fill="#4a7bd0"/><text x="534" y="104" font-size="12">54.7</text>
<rect x="470" y="128" width="82.3" height="22" fill="#3f9a6b"/><text x="558" y="144" font-size="12">76.8</text>
<rect x="470" y="168" width="31.9" height="22" fill="#e08a3c"/><text x="508" y="184" font-size="12">29.8</text>
<rect x="470" y="208" width="33.6" height="22" fill="#d0564a"/><text x="510" y="224" font-size="12">31.4</text>
<text x="340" y="262" font-size="12" text-anchor="middle">정확도는 다섯 가지 모두 0.9933 (argmax 일치 100%) — SQNR이 낮아도 분류 결과는 같다</text>
</svg>
```

그림 4 — big 모델의 변환 모드별 파일 크기(왼쪽)와 Keras 대비 출력 SQNR(오른쪽). 크기는 int8·dynamic·16x8이 float의 약 28~29%, float16이 51%. SQNR은 float16이 가장 높고, activation까지 정수인 int8·16x8이 가장 낮다.

출력에서 볼 것 — 하나씩 해석하자.

1. **크기는 예상대로다.** big: f32 262,208 B(예상 257,292 + 메타데이터 약 5 KB), f16 134,548 B(51%), int8 74,344 B(28%). int8은 예상 65 KB + 메타데이터 + 양자화 파라미터.
2. **small 모델의 dynamic은 f32와 바이트까지 같다(6,360 B, SQNR 133.8 dB).** 양자화가 **아예 안 됐다.** dynamic-range 변환은 원소 수가 작은 가중치 텐서는 건너뛴다 — 기본 임계값이 1024개 원소로 알려져 있고, small 모델의 가장 큰 가중치(conv2, 16×1×3×8 = 384개)가 그보다 작다. 결과와 맞는 설명이지만, 임계값은 변환기 내부 기본값이라 버전에 따라 다를 수 있다. 교훈: **변환 후 반드시 dtype을 확인하라** (예제 7).
3. **small 모델의 f16·int8은 거의 안 줄었다** (6,360 → 5,972 / 5,928). 가중치가 2.3 KB뿐이라 줄일 게 없고, 메타데이터가 지배한다. MCU급 작은 모델에서 "int8로 하면 4배 작아진다"는 말은 **가중치에 대해서만** 맞다.
4. **SQNR 순서**: f16(77 dB) > dynamic(55 dB) > 16x8(31 dB) ≈ int8(30 dB). dynamic은 activation을 float으로 저장하므로 int8보다 오차가 작다. int8의 30~37 dB는 C1의 "비트 하나에 6 dB" 감각으로 8비트 이론치(약 50 dB)보다 낮은데, 여러 층의 오차가 쌓이고 activation 범위를 calibration으로 정한 결과다. 그래도 **분류 결과는 거의 같다**(big은 300/300, small은 299/300).
5. **16x8이 int8보다 조금 낫지만 크게 낫지 않다** (37.5 → 41.5 dB, 29.8 → 31.4 dB). 16x8은 activation만 16비트이고 가중치는 그대로 int8이라, 이 모델에서는 **가중치 오차가 지배**한다는 뜻이다. 16x8은 activation의 dynamic range가 넓은 모델(오디오 front-end, 큰 residual 합)에서 효과가 크다. 그리고 16x8 파일이 더 크다(bias가 int64, 아래 예제 7).
6. **in=int8**: int8 모델만 입력이 int8이다. 이 모델을 쓰는 쪽(앱/펌웨어)은 **입력 scale·zero-point로 직접 양자화**해야 한다 — 코드의 `run()` 안 두 줄이 바로 그 일이다. 이걸 잊고 float을 `astype(int8)`로 넣으면 정확도가 무너진다 (12절).

### 5.4 변환 결과의 dtype과 op을 확인하는 습관

예제 7 — big 모델의 다섯 변환 결과에서 `fc1`의 가중치·bias·입출력 dtype과 op 목록을 뽑는다 (`ex13_variants_ops.py`). delegate 노드가 섞이지 않도록 XNNPACK을 끈 resolver로 연다.

```python
import os; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import numpy as np, tensorflow as tf
R = tf.lite.experimental.OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES   # DELEGATE 노드 없이 원래 op만
for m in ["f32", "dynamic", "f16", "int8", "16x8"]:
    it = tf.lite.Interpreter(model_path=f"imu_big_{m}.tflite", experimental_op_resolver_type=R)
    it.allocate_tensors(); ops = it._get_ops_details(); T = it.get_tensor_details()
    fc = [o for o in ops if o["op_name"] == "FULLY_CONNECTED"][0]       # fc1 (832→64)
    w, b = T[fc["inputs"][1]], T[fc["inputs"][2]]
    names = [o["op_name"] for o in ops if o["op_name"] not in ("RESHAPE", "EXPAND_DIMS")]
    print(f"{m:8s} fc1 w={w['dtype'].__name__:7s} b={b['dtype'].__name__:7s} "
          f"in/out={T[fc['inputs'][0]]['dtype'].__name__}/{T[fc['outputs'][0]]['dtype'].__name__}  ops={names}")
```

```text
f32      fc1 w=float32 b=float32 in/out=float32/float32  ops=['CONV_2D', 'MAX_POOL_2D', 'CONV_2D', 'MAX_POOL_2D', 'FULLY_CONNECTED', 'FULLY_CONNECTED']
dynamic  fc1 w=int8    b=float32 in/out=float32/float32  ops=['CONV_2D', 'MAX_POOL_2D', 'CONV_2D', 'MAX_POOL_2D', 'FULLY_CONNECTED', 'FULLY_CONNECTED']
f16      fc1 w=float32 b=float32 in/out=float32/float32  ops=['DEQUANTIZE', 'DEQUANTIZE', 'DEQUANTIZE', 'DEQUANTIZE', 'DEQUANTIZE', 'DEQUANTIZE', 'DEQUANTIZE', 'DEQUANTIZE', 'CONV_2D', 'MAX_POOL_2D', 'CONV_2D', 'MAX_POOL_2D', 'FULLY_CONNECTED', 'FULLY_CONNECTED']
int8     fc1 w=int8    b=int32   in/out=int8/int8  ops=['CONV_2D', 'MAX_POOL_2D', 'CONV_2D', 'MAX_POOL_2D', 'FULLY_CONNECTED', 'FULLY_CONNECTED']
16x8     fc1 w=int8    b=int64   in/out=int16/int16  ops=['QUANTIZE', 'CONV_2D', 'MAX_POOL_2D', 'CONV_2D', 'MAX_POOL_2D', 'FULLY_CONNECTED', 'FULLY_CONNECTED', 'DEQUANTIZE']
```

출력에서 볼 것 — 각 모드가 파일 안에서 실제로 어떤 모양인지가 드러난다.

| 모드 | 파일 안의 모습 | 실행 시 무슨 일이 |
|---|---|---|
| dynamic | 가중치만 int8, bias·activation float. op 목록은 f32와 같다 | FC·conv의 **hybrid 커널**이 입력 activation을 그때그때 int8로 양자화해 정수 MAC 후 float으로 돌려준다 |
| f16 | 가중치 8개(4층 × W·b)가 fp16으로 저장되고 앞에 `DEQUANTIZE` 8개 | CPU에서는 로드 후 fp32로 풀어서 계산 — **RAM은 안 줄고 파일만 준다**. GPU delegate는 fp16 그대로 쓸 수 있다 |
| int8 | 가중치 int8, bias int32, activation int8, 입출력 int8. QUANTIZE/DEQUANTIZE 없음 | **전부 정수** — MCU·NPU가 원하는 모양 |
| 16x8 | 가중치 int8, **bias int64**, activation int16, 입출력이 float이라 맨 앞 `QUANTIZE`, 맨 뒤 `DEQUANTIZE` | int16 누산 범위가 커서 bias가 64비트. 지원 op·커널이 int8보다 적다 |

여기서 TFLite의 **QUANTIZE / DEQUANTIZE op**의 역할이 보인다. 이 둘은 "float ↔ 정수 경계"다. 완전 정수 모델이라면 그래프 **안쪽에** 이 op이 있으면 안 된다 — 있다면 그 사이 구간이 float으로 돈다는 뜻이고, MCU/NPU에서는 그 구간이 CPU fallback이 되거나 아예 실패한다. 변환 후 op 목록에서 이 둘을 grep하는 것이 가장 싼 검사다.

---

## 6. int8 모델 해부 — scale·zero-point를 꺼내 손으로 맞추기

### 6.1 양자화 파라미터 꺼내기

C1 11.1절에서 정리한 TFLite int8 관례(activation은 per-tensor 비대칭, weight는 per-channel 대칭 zp=0, bias는 int32에 scale = s_in × s_w)를 실제 파일에서 확인한다.

예제 8 — small int8 모델의 첫 CONV_2D에 연결된 입력·filter·bias·출력의 양자화 파라미터를 뽑고, bias scale 규칙을 검사한다 (`ex6_qparams.py`).

```python
import os; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import numpy as np, tensorflow as tf
np.set_printoptions(precision=5, suppress=False, linewidth=100)
it = tf.lite.Interpreter(model_path="imu_cnn_int8.tflite"); it.allocate_tensors()
ops = it._get_ops_details()                       # 비공개 API지만 op 목록 보기에 편하다
print("ops:", [o["op_name"] for o in ops])
conv = ops[1]; T = {t["index"]: t for t in it.get_tensor_details()}
for role, idx in zip(["input", "filter", "bias"], conv["inputs"]):
    t = T[idx]; q = t["quantization_parameters"]
    print(f"{role:6s} T#{idx:<2d} {t['dtype'].__name__:5s} shape={t['shape'].tolist()} "
          f"n_scales={len(q['scales'])} axis={q['quantized_dimension']} zp={q['zero_points'][:3].tolist()}")
s_in = T[conv["inputs"][0]]["quantization_parameters"]["scales"][0]
s_w = T[conv["inputs"][1]]["quantization_parameters"]["scales"]
s_b = T[conv["inputs"][2]]["quantization_parameters"]["scales"]
print("s_in =", s_in)
print("s_w (채널별) =", s_w)
print("s_b == s_in*s_w ?", np.allclose(s_b, s_in * s_w, rtol=1e-6))
w = it.get_tensor(conv["inputs"][1])              # int8 filter [8,1,5,3]
print("filter int8 min/max per channel:", w.reshape(8, -1).min(1).tolist(), w.reshape(8, -1).max(1).tolist())
out = T[conv["outputs"][0]]["quantization_parameters"]
print("conv1 output: scale=%.6f zp=%d" % (out["scales"][0], out["zero_points"][0]))
```

```text
ops: ['EXPAND_DIMS', 'CONV_2D', 'RESHAPE', 'EXPAND_DIMS', 'MAX_POOL_2D', 'RESHAPE', 'EXPAND_DIMS', 'CONV_2D', 'RESHAPE', 'MEAN', 'FULLY_CONNECTED', 'DELEGATE', 'DELEGATE', 'DELEGATE']
input  T#12 int8  shape=[1, 1, 64, 3] n_scales=1 axis=0 zp=[-55]
filter T#11 int8  shape=[8, 1, 5, 3] n_scales=8 axis=0 zp=[0]
bias   T#10 int32 shape=[8] n_scales=8 axis=0 zp=[0]
s_in = 0.01726983
s_w (채널별) = [0.00665 0.00718 0.00561 0.00782 0.00732 0.00376 0.00624 0.00475]
s_b == s_in*s_w ? True
filter int8 min/max per channel: [-127, -28, -125, -127, -127, -126, -127, 3] [31, 127, 127, 78, 54, 127, 75, 127]
conv1 output: scale=0.022833 zp=-128
```

출력에서 볼 것 — C1의 관례가 전부 숫자로 확인된다.

- **입력**: per-tensor(scale 1개), s = 0.01727, zp = −55. 표현 범위를 손으로 계산하면 `(−128 − (−55)) × 0.01727 = −1.26` ~ `(127 + 55) × 0.01727 = 3.14`. representative dataset의 가속도 범위(중력 1 g + 노이즈 + 두드리기 펄스)가 대략 이 정도였다는 뜻이다. 비대칭이라 zp가 0이 아니다.
- **filter**: per-channel(scale 8개, `quantized_dimension = 0` = 출력 채널 축 O), zp = 0(대칭). 채널마다 min 또는 max가 정확히 ±127에 닿아 있다 — 채널별 `s_w = max|w| / 127`로 정했다는 증거다. 범위가 −127..127이지 −128이 아니다 (C1 4.2절).
- **bias**: int32, scale 8개가 `s_in × s_w`와 정확히 같다 (`True`). 그래서 bias를 int32 누산기에 **그냥 더할 수 있다** (C1 7.1절).
- **conv1 출력**: zp = −128. 출력이 ReLU 뒤라 음수가 없으므로, 변환기가 int8 범위 전체(−128..127)를 0..`255 × 0.022833 = 5.82`에 썼다. 비대칭 양자화가 ReLU 출력에서 해상도를 2배로 쓰는 장면이다.
- **ops 목록 끝의 `DELEGATE` 3개**: XNNPACK이 원래 op 11개 중 일부를 세 덩어리로 가져갔다. 8절의 주제다.

```svg
<svg viewBox="0 0 640 290" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="230" x2="560" y2="230" stroke="currentColor"/><line x1="60" y1="40" x2="60" y2="230" stroke="currentColor"/>
<text x="54" y="234" font-size="12" text-anchor="end">0</text><text x="54" y="154" font-size="12" text-anchor="end">.004</text><text x="54" y="74" font-size="12" text-anchor="end">.008</text>
<line x1="56" y1="150" x2="60" y2="150" stroke="currentColor"/><line x1="56" y1="70" x2="60" y2="70" stroke="currentColor"/>
<rect x="80" y="97.0" width="36" height="133.0" fill="#4a7bd0"/><rect x="140" y="86.4" width="36" height="143.6" fill="#4a7bd0"/><rect x="200" y="117.8" width="36" height="112.2" fill="#4a7bd0"/><rect x="260" y="73.6" width="36" height="156.4" fill="#4a7bd0"/><rect x="320" y="83.6" width="36" height="146.4" fill="#4a7bd0"/><rect x="380" y="154.8" width="36" height="75.2" fill="#e08a3c"/><rect x="440" y="105.2" width="36" height="124.8" fill="#4a7bd0"/><rect x="500" y="135.0" width="36" height="95.0" fill="#4a7bd0"/>
<line x1="60" y1="73.6" x2="560" y2="73.6" stroke="#d0564a" stroke-dasharray="6,4"/><text x="565" y="70" font-size="12">per-tensor</text><text x="565" y="86" font-size="12">s = .00782</text>
<text x="98" y="248" font-size="12" text-anchor="middle">0</text><text x="158" y="248" font-size="12" text-anchor="middle">1</text><text x="218" y="248" font-size="12" text-anchor="middle">2</text><text x="278" y="248" font-size="12" text-anchor="middle">3</text><text x="338" y="248" font-size="12" text-anchor="middle">4</text><text x="398" y="248" font-size="12" text-anchor="middle">5</text><text x="458" y="248" font-size="12" text-anchor="middle">6</text><text x="518" y="248" font-size="12" text-anchor="middle">7</text>
<text x="310" y="270" font-size="12" text-anchor="middle">conv1 출력 채널 (per-channel scale s_w, quantized_dimension = 0)</text>
<text x="398" y="146" font-size="12" text-anchor="middle">.00376</text>
<text x="310" y="24" font-size="12" text-anchor="middle">채널 5: per-channel이면 ±127 단계, per-tensor였다면 ±61 단계만 사용</text>
</svg>
```

그림 5 — small int8 모델 conv1의 채널별 weight scale(실제 값). 빨간 점선은 per-tensor로 했다면 쓸 scale(가장 큰 채널 3의 0.00782). 채널 5(주황)는 가중치가 작아서, per-tensor라면 `127 × 0.00376 / 0.00782 ≈ 61` 단계만 쓰게 되어 해상도를 절반 넘게 잃는다. 이것이 TFLite가 conv weight를 per-channel로 하는 이유다 (C1 5절, C2 6.1절).

### 6.2 입력을 손으로 양자화하고, conv 한 층을 정수로 재현하기

이제 "TFLite가 내부에서 하는 일"을 직접 해 보고 비트 단위로 맞는지 본다. 손계산 먼저.

```
입력 양자화: q = clamp(round(x / s_in) + z_in, −128, 127)
  x = 1.0 g (중력)  →  1.0 / 0.01726983 = 57.90  →  round 58  →  58 + (−55) = 3
  역양자화: s_in × (3 − (−55)) = 0.01726983 × 58 = 1.0016   (오차 0.0016 < s/2)

conv 출력 (채널 c, 위치 t):
  acc[c,t]  = Σ_k (q_x[t+k] − z_in) · q_w[c,k] + q_b[c]          ← int32 누산 (z_w = 0)
  M[c]      = s_in · s_w[c] / s_out                              ← 실수 multiplier
  q_y[c,t]  = clamp(round(acc · M[c]) + z_out, z_out, 127)         ← ReLU = 하한 z_out
  채널 0: M = 0.01726983 × 0.00665 / 0.022833 ≈ 0.00503
```

말로 하면: 입력에서 zero-point를 빼고 int8 가중치와 곱해 int32로 더한 뒤, 채널마다 다른 아주 작은 실수 M을 곱해 출력 눈금으로 옮기고, ReLU는 "출력 zp 아래를 자른다"로 끝난다. 실제 커널은 M을 (int32 multiplier, shift)로 바꿔 정수로만 계산하지만(C1 7.3절), 여기서는 float M으로 재현해서 몇 LSB나 다른지 본다.

예제 9 — 입력을 수동 양자화해 int8 모델을 돌리고 출력을 역양자화해 Keras와 비교한 뒤, 첫 CONV_2D를 numpy 정수 연산으로 재계산해 TFLite 중간 텐서와 비교한다 (`ex7_int8_check.py`). 중간 텐서를 보려면 `experimental_preserve_all_tensors=True`와 참조 커널(`BUILTIN_REF`)을 쓴다 — 최적화 커널·delegate는 중간 버퍼를 재사용해서 덮어쓰기 때문이다.

```python
import os; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import numpy as np, tensorflow as tf
x = np.load("Xte.npy")[:1]                                    # 샘플 1개 (1, 64, 3)
R = tf.lite.experimental.OpResolverType.BUILTIN_REF            # 참조 커널, 중간 텐서 보존
it = tf.lite.Interpreter(model_path="imu_cnn_int8.tflite", experimental_op_resolver_type=R,
                         experimental_preserve_all_tensors=True); it.allocate_tensors()
i, o = it.get_input_details()[0], it.get_output_details()[0]
(s_in, z_in), (s_out, z_out) = i["quantization"], o["quantization"]
q_x = np.clip(np.round(x / s_in) + z_in, -128, 127).astype(np.int8)   # ① 입력 양자화
it.set_tensor(i["index"], q_x); it.invoke()
y_q = it.get_tensor(o["index"])                                       # ② int8 출력
y = s_out * (y_q.astype(np.float32) - z_out)                          # ③ 역양자화
y_ref = tf.keras.models.load_model("imu_cnn.keras").predict(x, verbose=0)
print("q_x[0,:2] =", q_x[0, :2].tolist(), " (s_in=%.5f z_in=%d)" % (s_in, z_in))
print("int8 out:", y_q.tolist(), " s_out=%.5f z_out=%d" % (s_out, z_out))
print("dequant :", np.round(y.astype(float), 4).tolist()); print("keras   :", np.round(y_ref.astype(float), 4).tolist())

ops = it._get_ops_details(); c = ops[1]                                # 첫 CONV_2D를 손으로 재계산
T = {t["index"]: t for t in it.get_tensor_details()}
xin, w, b = (it.get_tensor(k).astype(np.int64) for k in c["inputs"])   # [1,1,64,3] [8,1,5,3] [8]
qp = lambda k: T[k]["quantization_parameters"]
zi = qp(c["inputs"][0])["zero_points"][0]; si = qp(c["inputs"][0])["scales"][0]
sw = qp(c["inputs"][1])["scales"]; so, zo = qp(c["outputs"][0])["scales"][0], qp(c["outputs"][0])["zero_points"][0]
acc = np.stack([((xin[0, 0, t:t+5] - zi)[None] * w[:, 0]).sum((1, 2)) for t in range(60)]) + b  # int32 누산 [60,8]
mine = np.clip(np.round(acc * (si * sw / so)) + zo, zo, 127)          # requant + ReLU(clamp 하한=zo)
tfl = it.get_tensor(c["outputs"][0])[0, 0].astype(np.int64)
d = np.abs(mine - tfl)
print("conv1 손계산 vs TFLite: 일치 %d/%d, 최대 차이 %d LSB" % ((d == 0).sum(), d.size, d.max()))
```

```text
q_x[0,:2] = [[-68, -30, -1], [-39, -75, 9]]  (s_in=0.01727 z_in=-55)
int8 out: [[-33, -60, -23]]  s_out=0.10234 z_out=-20
dequant : [[-1.3304, -4.0936, -0.307]]
keras   : [[-1.2396, -4.1614, -0.3517]]
conv1 손계산 vs TFLite: 일치 480/480, 최대 차이 0 LSB
```

출력에서 볼 것:

- 입력 첫 샘플의 z축 값 −1(= `0.01727 × (−1 + 55) = 0.93 g`)과 9(= 1.10 g) — 중력 근처다. 손계산의 "1.0 g → 3"과 같은 동네.
- 최종 logits: int8 → 역양자화 `[−1.33, −4.09, −0.31]` vs Keras `[−1.24, −4.16, −0.35]`. 차이는 최대 0.09 ≈ **출력 1 LSB(s_out = 0.102)** 수준이고, argmax(클래스 2)는 같다.
- conv1 출력 60 위치 × 8 채널 = 480개가 **전부 0 LSB 차이로 일치**한다. float M으로 계산해도 이 층에서는 반올림 경계에 걸린 값이 없었다. 일반적으로는 M을 정수 multiplier로 바꾸는 과정 때문에 드물게 1 LSB 차이가 날 수 있다 — 펌웨어에서 C로 직접 커널을 짤 때는 C1 7.4절의 SRDHM·RDBPOT까지 그대로 따라야 bit-exact가 된다.

이 예제가 **"TFLite 모델이 원본과 맞는지 어떻게 검증하나"** 질문의 실전 답이다: (1) 같은 입력으로 float 골든과 출력·정확도 비교, (2) 의심 층은 중간 텐서를 꺼내 정수 연산으로 재현, (3) 타깃(C/MCU) 구현은 이 Python 정수 시뮬레이션과 bit-exact 비교 (C8).

### 6.3 층별 양자화 오차 — QuantizationDebugger

모델이 크면 층을 하나씩 손으로 볼 수 없다. TFLite에는 층별 오차 통계를 내 주는 `tf.lite.experimental.QuantizationDebugger`가 있다. 각 op의 양자화 출력을 float 계산과 비교한 통계(같은 입력 기준)를 CSV로 준다.

예제 10 — big 모델을 int8로 변환하면서 층별 오차를 **출력 scale 단위(LSB)** 로 정규화해 본다 (`ex12_qdebug.py`).

```python
import os, io, contextlib; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import numpy as np, tensorflow as tf
model = tf.keras.models.load_model("imu_big.keras"); Xcal = np.load("Xcal.npy")
def rep():
    for i in range(100): yield [Xcal[i:i+1]]
c = tf.lite.TFLiteConverter.from_keras_model(model)
c.optimizations = [tf.lite.Optimize.DEFAULT]; c.representative_dataset = rep
with contextlib.redirect_stdout(io.StringIO()):
    dbg = tf.lite.experimental.QuantizationDebugger(converter=c, debug_dataset=rep)
    dbg.run()                                   # 층마다 float 경로 vs 양자화 경로 오차 통계
buf = io.StringIO(); dbg.layer_statistics_dump(buf)
rows = [l.split(",") for l in buf.getvalue().strip().splitlines()]
h = rows[0]; g = lambda r, k: float(r[h.index(k)])
print("op_name           scale   rmse/scale  max_err/scale  mean_err/scale")
for r in rows[1:]:
    s = g(r, "scale")
    print(f"{r[0]:15s} {s:8.5f}  {np.sqrt(g(r, 'mean_squared_error')) / s:9.3f}"
          f"  {g(r, 'max_abs_error') / s:12.3f}  {g(r, 'mean_error') / s:13.3f}")
```

```text
op_name           scale   rmse/scale  max_err/scale  mean_err/scale
EXPAND_DIMS      0.01727      0.287         0.498         -0.002
CONV_2D          0.00538      0.275         1.015         -0.015
RESHAPE          0.00538      0.000         0.000          0.000
EXPAND_DIMS      0.00538      0.000         0.000          0.000
MAX_POOL_2D      0.00538      0.000         0.000          0.000
RESHAPE          0.00538      0.000         0.000          0.000
EXPAND_DIMS      0.00538      0.000         0.000          0.000
CONV_2D          0.00714      0.261         0.978         -0.023
RESHAPE          0.00714      0.000         0.000          0.000
EXPAND_DIMS      0.00714      0.000         0.000          0.000
MAX_POOL_2D      0.00714      0.000         0.000          0.000
RESHAPE          0.00714      0.000         0.000          0.000
FULLY_CONNECTED  0.03881      0.243         0.647         -0.009
FULLY_CONNECTED  0.23519      0.292         0.389          0.044
```

출력에서 볼 것:

- 연산하는 op(CONV_2D, FULLY_CONNECTED)의 `rmse/scale`이 0.24~0.29다. **이론값은 1/√12 = 0.289** — 순수한 반올림 잡음(균일 분포, C1 3.1절)과 거의 같다. 즉 이 모델의 양자화 오차는 "피할 수 없는 최소 오차" 수준이고 이상한 층이 없다.
- RESHAPE·EXPAND_DIMS·MAX_POOL은 오차 0 — 값을 옮기거나 고르기만 하는 op은 int8에서 **정확**하다 (max-pool은 단조 함수라 양자화와 순서가 바뀌어도 같다).
- `max_err/scale`이 1을 넘으면(CONV_2D 1.015) 반올림 1 LSB를 살짝 넘는 값이 있다는 뜻이다. 어떤 층이 **수 LSB 이상**이거나 `mean_err`가 0에서 크게 벗어나면(체계적 편향) 그 층이 범인이다 — C2 6~7절의 per-channel·bias correction·mixed precision으로 간다.

---

## 7. Op 호환성 — builtin, Select TF ops(Flex), custom op

### 7.1 세 종류의 op

TensorFlow에는 op가 수천 개 있지만 TFLite builtin op은 그보다 훨씬 적다(대략 백몇십 개 수준, 버전마다 늘어난다). 변환기는 TF op마다 셋 중 하나로 처리한다.

| 종류 | 무엇 | 실행에 필요한 것 | MCU(TFLM)에서 |
|---|---|---|---|
| **builtin** | TFLite가 자체 커널을 가진 op (CONV_2D, FULLY_CONNECTED, ATAN2…) | 기본 런타임 | 그중 일부만 지원 (F2) |
| **Select TF ops (Flex)** | TF 커널을 그대로 가져다 쓰는 op (`FlexAtan` 같은 이름) | **Flex delegate** 링크 — 바이너리가 수 MB 커진다 | 불가 |
| **custom op** | 사용자가 커널을 직접 등록하는 op | `allow_custom_ops=True`로 변환 + 런타임에 직접 등록 | 직접 구현하면 가능 |

변환기 기본값은 builtin만 허용이다. builtin에 없는 op을 만나면 변환이 **실패**한다. 이게 펌웨어 엔지니어에게 좋은 기본값이다 — "링크 에러는 빌드 타임에 나는 게 런타임 크래시보다 낫다."

예제 11 — builtin에 없는 `tf.math.atan`(IMU 기울기 계산에 쓸 법한 op)을 넣은 작은 함수를 네 가지 방법으로 변환·실행해 본다 (`ex8_ops.py`).

```python
import os, re; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import numpy as np, tensorflow as tf
def make(fn): return tf.function(fn, input_signature=[tf.TensorSpec([1, 4], tf.float32)])
f = make(lambda x: tf.math.atan(x) * 2.0 + 1.0)            # atan: 이 버전 TFLite builtin에 없음
x = np.array([[-1.0, 0.0, 0.5, 2.0]], np.float32)
B, S = tf.lite.OpsSet.TFLITE_BUILTINS, tf.lite.OpsSet.SELECT_TF_OPS

def conv(fn, ops=None, custom=False):
    c = tf.lite.TFLiteConverter.from_concrete_functions([fn.get_concrete_function()], fn)
    if ops: c.target_spec.supported_ops = ops
    c.allow_custom_ops = custom
    return c.convert()
def run(tfl):
    it = tf.lite.Interpreter(model_content=tfl)
    try: it.allocate_tensors()
    except RuntimeError as e:
        m = str(e).replace("\n", " ")
        return "실행 실패: " + m[:60] + " ... " + m[-30:]
    it.set_tensor(it.get_input_details()[0]["index"], x); it.invoke()
    ops = [o["op_name"] for o in it._get_ops_details()]
    return f"ops={ops} max diff={np.abs(it.get_tensor(it.get_output_details()[0]['index']) - f(x).numpy()).max():.1e}"

try: conv(f)                                                # ① builtin만
except Exception as e: print("① builtin only  ->", re.search(r"'tf\.\w+' op is [^|\n]*", str(e)).group(0))
print("② +SELECT_TF_OPS ->", run(conv(f, [B, S])))          # ② Flex 허용
print("③ custom op      ->", run(conv(f, custom=True)))     # ③ custom op으로 남김
g = make(lambda x: tf.math.atan2(x, tf.ones_like(x)) * 2.0 + 1.0)  # ④ 같은 수학, 다른 op
print("④ atan2로 재작성 ->", run(conv(g)))
```

```text
① builtin only  -> 'tf.Atan' op is neither a custom op nor a flex op
② +SELECT_TF_OPS -> 실행 실패: Select TensorFlow op(s), included in the given model, is(are ...  (FlexAtan) failed to prepare.
③ custom op      -> 실행 실패: Encountered unresolved custom op: Atan. See instructions: ht ... er 0 (Atan) failed to prepare.
④ atan2로 재작성 -> ops=['ATAN2', 'MUL', 'ADD', 'DELEGATE'] max diff=2.4e-07
```

출력에서 볼 것 — 네 줄이 op 호환성의 전부를 보여 준다.

1. **builtin만**: 변환 단계에서 `ConverterError`. stderr에는 "TF Select ops: Atan"이라는 힌트와 함께 "TF Select를 켜라"는 안내가 나온다.
2. **Flex 허용**: 변환은 **성공**한다(`FlexAtan` op이 들어간다). 그런데 이 Python interpreter에는 Flex delegate가 링크되어 있지 않아 `allocate_tensors`에서 실패한다. 에러 메시지 원문에는 "Android라면 `tensorflow-lite-select-tf-ops` 의존성을 추가하라"는 안내가 있다. 즉 **"변환이 됐다 ≠ 기기에서 돈다"**. Flex는 런타임 바이너리를 크게 키우므로 모바일에서도 가능하면 피하고, MCU에서는 선택지가 아니다.
3. **custom op**: 변환은 되지만 실행 시 "unresolved custom op: Atan". 런타임에 `Atan`이라는 이름으로 커널(Init/Prepare/Invoke 함수 묶음)을 직접 등록해야 한다. 펌웨어로 치면 "심볼은 선언됐는데 구현이 링크 안 된" 상태다.
4. **수학적으로 같은 builtin op으로 재작성**: `atan(x) = atan2(x, 1)`. builtin `ATAN2`로 바뀌어 변환·실행 모두 성공, 오차 2.4e-07. 실무에서 가장 흔한 해결책이 이것이다 — **모델 쪽에서 지원되는 op으로 바꾸기** (C6 8절 operator lowering). `DELEGATE`가 붙은 것은 XNNPACK이 MUL/ADD 등을 가져갔다는 뜻이다.

### 7.2 op 버전 — "Didn't find op for builtin opcode ... version"

op에는 **버전**도 있다. 같은 FULLY_CONNECTED라도 int8 지원, per-channel, hybrid(dynamic-range) 같은 기능이 추가될 때마다 버전이 올라간다. 파일의 `OperatorCode.version`이 런타임이 아는 것보다 높으면 로드가 실패한다. 9절 벤치마크 중 실제로 만난 메시지다 (dynamic-range 모델을 참조 커널 resolver로 열 때).

```text
ValueError: Didn't find op for builtin opcode 'FULLY_CONNECTED' version '12'. An older version of this builtin might be supported. Are you using an old TFLite binary with a newer model?
```

말로 하면: "변환기(PC의 TF 2.20)가 만든 모델이 요구하는 op 버전을 런타임이 모른다." 기기 쪽 런타임(Android 앱의 TFLite AAR, 펌웨어의 TFLM 커밋)이 PC의 변환기보다 오래되면 똑같이 터진다. 그래서 F8의 **버전 매트릭스**(변환기 버전 ↔ 런타임 버전 ↔ delegate 버전)를 문서로 관리해야 한다. 펌웨어의 "부트로더 ↔ 앱 이미지 헤더 버전 호환" 문제와 같다.

### 7.3 변환기 내부, signature, dynamic shape

- **`experimental_new_converter`**: TF 2.2~2.3 무렵부터 변환기는 MLIR 기반("새 변환기")이 기본이고, 이 속성은 기본 True다. 예전 TOCO 변환기로 되돌리는 옵션이 남아 있었지만 최신 버전에서는 사실상 쓸 일이 없다. 위 에러 메시지의 `Could not translate MLIR to FlatBuffer`, `tfl.mul` 같은 표기가 MLIR 변환기의 흔적이다. 비슷하게 `experimental_new_quantizer`(MLIR 기반 양자화기)도 있다. 이름에 experimental이 붙은 옵션은 버전에 따라 기본값이 바뀌므로, 재현이 중요한 프로젝트는 **변환 스크립트에 값을 명시**하자.
- **signature**: 모델의 "공개 함수 시그니처" — 입력·출력 텐서에 사람이 붙인 이름. 한 파일에 여러 signature(예: `train`, `infer`)를 넣을 수도 있다.
- **dynamic shape**: `shape_signature`에 −1이 있는 축은 실행 전에 `resize_tensor_input`으로 바꿀 수 있다. 바꾼 다음엔 **반드시 `allocate_tensors`를 다시** 해야 한다(arena 재계획). NPU delegate는 대개 정적 shape을 원하므로 (C6 9절) 배포용 모델은 batch 1로 고정하는 경우가 많다.

예제 12 — signature 목록, batch 1→8 resize, signature runner, 고정 축 resize 시도 (`ex10_sig_resize.py`).

```python
import os; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import numpy as np, tensorflow as tf
Xte = np.load("Xte.npy")
it = tf.lite.Interpreter(model_path="imu_cnn_f32.tflite")
print("signatures:", it.get_signature_list())
d = it.get_input_details()[0]
print("shape:", d["shape"].tolist(), " shape_signature:", d["shape_signature"].tolist())

it.resize_tensor_input(d["index"], [8, 64, 3])          # batch 1 → 8
it.allocate_tensors()                                     # resize 후엔 반드시 다시 allocate
print("after resize:", it.get_input_details()[0]["shape"].tolist(),
      "->", it.get_output_details()[0]["shape"].tolist())
it.set_tensor(d["index"], Xte[:8]); it.invoke()
y8 = it.get_tensor(it.get_output_details()[0]["index"])

run = it.get_signature_runner("serving_default")          # 이름으로 부르는 API
y_sig = run(imu=Xte[:8])["output_0"]
print("batch8 == signature runner:", np.array_equal(y8, y_sig))

it2 = tf.lite.Interpreter(model_path="imu_cnn_f32.tflite")  # 고정 크기 축(64)을 바꾸면?
try:
    it2.resize_tensor_input(d["index"], [1, 100, 3], strict=True)
except Exception as e:
    print("strict resize 64->100:", str(e).splitlines()[0][:80])
```

```text
signatures: {'serving_default': {'inputs': ['imu'], 'outputs': ['output_0']}}
shape: [1, 64, 3]  shape_signature: [-1, 64, 3]
after resize: [8, 64, 3] -> [8, 3]
batch8 == signature runner: True
strict resize 64->100: Attempting to resize dimension 1 of tensor 0 with value 64 to 100. ResizeInputTe
```

출력에서 볼 것: batch 축(−1)은 8로 바꿔져 출력도 `[8, 3]`이 되고, signature runner(`imu=` → `output_0`)와 결과가 비트 단위로 같다. 반면 `strict=True`로 시간 축 64를 100으로 바꾸려 하면 거부된다 — 그 축은 shape_signature에서 −1이 아니기 때문이다. (`strict=False`면 resize 자체는 받아들이지만, 모델 안에 `[-1, 60, 8]` 같은 상수 reshape이 박혀 있어 실행 단계에서 깨질 것이다. 시간 축을 바꾸려면 모델을 다시 변환하는 게 맞다.)

---

## 8. Delegate — 그래프의 일부를 가속기에 넘기기

### 8.1 delegate란

**Delegate**는 interpreter에 꽂는 **플러그인 백엔드**다. 동작은 세 단계다.

1. interpreter가 delegate에게 노드 목록을 보여 주며 "어떤 노드를 할 수 있니?"라고 묻는다.
2. delegate가 지원하는 노드를 고르면, interpreter가 **연결된 지원 노드 묶음(partition)** 을 찾아 각 묶음을 `DELEGATE` 노드 **하나**로 바꾼다 (`ModifyGraphWithDelegate`).
3. 실행 시 `DELEGATE` 노드는 묶음 전체를 delegate의 커널(GPU 셰이더, NPU 커맨드 버퍼, SIMD 루틴)로 실행하고, 나머지 노드는 기본 CPU 커널(builtin)이 실행한다.

펌웨어 비유: delegate는 **DMA 엔진이나 HW 가속기 드라이버**다. CPU가 "이 블록은 가속기가 해라"고 디스크립터를 넘기고, 가속기가 못 하는 일은 CPU가 직접 한다. 그리고 CPU↔가속기 경계를 넘을 때마다 동기화·캐시 flush·데이터 복사 비용이 든다.

### 8.2 실측 — Conv1D 모델은 XNNPACK이 세 조각으로 가져간다

예제 8에서 int8 small 모델의 op 목록 끝에 `DELEGATE`가 **3개** 있었다. 원래 op 11개 중 무엇이 delegate 밖에 남았는지 확인하고, 같은 계산을 처음부터 4D(Conv2D)로 짠 모델과 비교한다.

예제 13 — Conv1D 모델과 Conv2D 모델의 XNNPACK partition 수 비교 (`ex9_partition.py`). (어떤 op의 출력이 delegate 노드의 입력으로 들어가면 그 op는 delegate 밖에서 돈다는 단순한 규칙으로 판별한다.)

```python
import os, io, contextlib; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import numpy as np, tensorflow as tf
L = tf.keras.layers
def conv1d_net():   # 원래 모델과 같은 구조 (Conv1D)
    return tf.keras.Sequential([tf.keras.Input((64, 3)), L.Conv1D(8, 5, activation="relu"),
        L.MaxPooling1D(2), L.Conv1D(16, 3, activation="relu"), L.GlobalAveragePooling1D(), L.Dense(3)])
def conv2d_net():   # 같은 계산을 처음부터 4D (N, 64, 1, 3) + (5,1) 커널로
    return tf.keras.Sequential([tf.keras.Input((64, 1, 3)), L.Conv2D(8, (5, 1), activation="relu"),
        L.MaxPooling2D((2, 1)), L.Conv2D(16, (3, 1), activation="relu"), L.GlobalAveragePooling2D(), L.Dense(3)])
for name, f in [("Conv1D", conv1d_net), ("Conv2D", conv2d_net)]:
    tf.keras.utils.set_random_seed(0)
    with contextlib.redirect_stdout(io.StringIO()):
        tfl = tf.lite.TFLiteConverter.from_keras_model(f()).convert()
    it = tf.lite.Interpreter(model_content=tfl); it.allocate_tensors()
    ops = it._get_ops_details()
    orig = [o for o in ops if o["op_name"] != "DELEGATE"]
    dels = [o for o in ops if o["op_name"] == "DELEGATE"]
    # 어떤 원래 op의 출력이 delegate 노드의 입력으로 들어가면 그 op는 delegate 밖(CPU 기본 커널)에서 돈다
    outside = [o["op_name"] for o in orig if any(t in d["inputs"] for d in dels for t in o["outputs"])]
    print(f"{name}: ops={len(orig)} {[o['op_name'] for o in orig]}")
    print(f"   delegate partitions={len(dels)}  partition 밖에서 도는 op={outside}")
```

```text
Conv1D: ops=11 ['EXPAND_DIMS', 'CONV_2D', 'RESHAPE', 'EXPAND_DIMS', 'MAX_POOL_2D', 'RESHAPE', 'EXPAND_DIMS', 'CONV_2D', 'RESHAPE', 'MEAN', 'FULLY_CONNECTED']
   delegate partitions=3  partition 밖에서 도는 op=['EXPAND_DIMS', 'EXPAND_DIMS', 'EXPAND_DIMS']
Conv2D: ops=5 ['CONV_2D', 'MAX_POOL_2D', 'CONV_2D', 'MEAN', 'FULLY_CONNECTED']
   delegate partitions=1  partition 밖에서 도는 op=[]
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="22" font-size="13">Conv1D 모델 (op 11개) — XNNPACK partition 3개</text>
<rect x="70" y="35" width="122" height="60" rx="8" fill="none" stroke="#4a7bd0" stroke-dasharray="5,3" stroke-width="2"/><rect x="250" y="35" width="122" height="60" rx="8" fill="none" stroke="#4a7bd0" stroke-dasharray="5,3" stroke-width="2"/><rect x="430" y="35" width="242" height="60" rx="8" fill="none" stroke="#4a7bd0" stroke-dasharray="5,3" stroke-width="2"/>
<rect x="15" y="50" width="52" height="30" rx="4" fill="#888" fill-opacity="0.35" stroke="#888"/><text x="41" y="70" font-size="12" text-anchor="middle">EXP</text>
<rect x="75" y="50" width="52" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="101" y="70" font-size="12" text-anchor="middle">CONV</text>
<rect x="135" y="50" width="52" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="161" y="70" font-size="12" text-anchor="middle">RSHP</text>
<rect x="195" y="50" width="52" height="30" rx="4" fill="#888" fill-opacity="0.35" stroke="#888"/><text x="221" y="70" font-size="12" text-anchor="middle">EXP</text>
<rect x="255" y="50" width="52" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="281" y="70" font-size="12" text-anchor="middle">POOL</text>
<rect x="315" y="50" width="52" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="341" y="70" font-size="12" text-anchor="middle">RSHP</text>
<rect x="375" y="50" width="52" height="30" rx="4" fill="#888" fill-opacity="0.35" stroke="#888"/><text x="401" y="70" font-size="12" text-anchor="middle">EXP</text>
<rect x="435" y="50" width="52" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="461" y="70" font-size="12" text-anchor="middle">CONV</text>
<rect x="495" y="50" width="52" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="521" y="70" font-size="12" text-anchor="middle">RSHP</text>
<rect x="555" y="50" width="52" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="581" y="70" font-size="12" text-anchor="middle">MEAN</text>
<rect x="615" y="50" width="52" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="641" y="70" font-size="12" text-anchor="middle">FC</text>
<text x="131" y="112" font-size="12" text-anchor="middle">DELEGATE #1</text><text x="311" y="112" font-size="12" text-anchor="middle">DELEGATE #2</text><text x="551" y="112" font-size="12" text-anchor="middle">DELEGATE #3</text>
<path d="M 41,120 L 41,135 L 401,135 L 401,120" fill="none" stroke="#d0564a"/><line x1="221" y1="120" x2="221" y2="135" stroke="#d0564a"/>
<text x="221" y="152" font-size="12" text-anchor="middle">CPU 기본 커널 (EXPAND_DIMS 3개) — CPU ↔ delegate 전환 5번</text>
<text x="10" y="192" font-size="13">Conv2D로 다시 짠 모델 (op 5개) — partition 1개</text>
<rect x="10" y="205" width="302" height="60" rx="8" fill="none" stroke="#3f9a6b" stroke-dasharray="5,3" stroke-width="2"/>
<rect x="15" y="220" width="52" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/><text x="41" y="240" font-size="12" text-anchor="middle">CONV</text>
<rect x="75" y="220" width="52" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/><text x="101" y="240" font-size="12" text-anchor="middle">POOL</text>
<rect x="135" y="220" width="52" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/><text x="161" y="240" font-size="12" text-anchor="middle">CONV</text>
<rect x="195" y="220" width="52" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/><text x="221" y="240" font-size="12" text-anchor="middle">MEAN</text>
<rect x="255" y="220" width="52" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/><text x="281" y="240" font-size="12" text-anchor="middle">FC</text>
<text x="161" y="285" font-size="12" text-anchor="middle">DELEGATE #1 (전부) — 전환 1번</text>
<text x="340" y="230" font-size="12">같은 수학 (높이 1인 2D conv)</text><text x="340" y="248" font-size="12">EXPAND_DIMS·RESHAPE가 사라져</text><text x="340" y="266" font-size="12">그래프가 끊기지 않는다</text>
</svg>
```

그림 6 — XNNPACK delegate의 실제 partitioning(이 환경, TF 2.20 기준). 위: Keras Conv1D가 만든 EXPAND_DIMS 세 개(회색)를 이 버전의 XNNPACK delegate가 가져가지 않아 그래프가 세 조각(파란 점선)으로 끊긴다. 아래: 처음부터 4D 텐서와 (k, 1) 커널로 짜면 op 5개가 한 partition에 들어간다.

출력에서 볼 것:

- 이 환경의 XNNPACK delegate는 Conv1D가 만든 **EXPAND_DIMS를 가져가지 않았다** (이 TF 버전·이 모델 기준의 관찰이다. delegate의 op 지원 범위는 버전마다 넓어진다). 그 세 개가 경계가 되어 partition 3개가 생겼다.
- 같은 계산을 Conv2D로 짜면 **reshape류가 없어져** partition 1개.
- XNNPACK은 CPU 위의 delegate라 경계를 넘어도 메모리 복사가 거의 없어서 비용이 작다. 그러나 **GPU/NPU delegate에서 같은 일이 생기면** 경계마다 (1) 텐서를 CPU 메모리 ↔ 가속기 메모리로 복사하거나 캐시를 flush하고, (2) layout을 바꾸고(예: GPU의 PHWC4, NPU의 타일 layout), (3) 가속기에 작업을 제출하고 완료를 기다리는 **동기화 비용**이 든다. 작은 모델에서는 이 고정 비용이 계산보다 커져서 **"NPU에 올렸더니 CPU보다 느려졌다"** 가 된다. C6 12.2절의 CPU fallback 이야기 그대로다.

partial delegation이 느린 이유를 숫자 감각으로 정리하면:

```
총 지연 ≈ Σ(delegate 구간 계산) + Σ(CPU 구간 계산) + (경계 수) × (복사 + 동기화 + 제출 오버헤드)

예) NPU 구간 계산 0.2 ms × 3, CPU 구간 0.05 ms × 3,
    경계 5번 × 0.3 ms (복사·캐시 flush·커널 제출)  →  0.6 + 0.15 + 1.5 = 2.25 ms
    전부 NPU 한 덩어리면                              →  0.6 + 0 + 2 × 0.3 = 1.2 ms
```

(숫자는 감을 잡기 위한 가정이다. 실제 경계 비용은 SoC·드라이버마다 다르고, 벤더 프로파일러로 재야 한다 — F4, F8.)

### 8.3 delegate 도감

| delegate | 대상 | 상태·특징 (이 노트 작성 시점 기준, 버전·문서 확인 필요) |
|---|---|---|
| **XNNPACK** | CPU (Arm NEON, x86 SIMD) | 최근 TFLite/LiteRT 빌드에서 **기본으로 켜지는 CPU delegate** (이 환경 로그: "Created TensorFlow Lite XNNPACK delegate for CPU"). float32와 int8 모두 가속. 실측 효과는 9절 |
| **GPU delegate** | 모바일 GPU (Android: OpenCL/OpenGL ES, iOS: Metal) | float16/float32 중심. fp16 모델(5절)이 잘 맞는다. 큰 conv 모델에서 효과, 작은 모델은 오버헤드가 크다 |
| **NNAPI delegate** | Android NNAPI → 벤더 드라이버(DSP/NPU) | Android의 공통 가속 API였으나 Google이 Android 15에서 NNAPI를 deprecated로 발표했다. 신규 프로젝트는 벤더 delegate나 LiteRT의 새 가속 경로를 확인하자 |
| **Hexagon delegate** | Qualcomm Hexagon DSP (구형) | 레거시. Qualcomm은 QNN 기반 경로로 이동했다 |
| **QNN delegate** | Qualcomm HTP(NPU)·GPU·DSP | Qualcomm AI Engine Direct(QNN) SDK에 포함된 TFLite delegate. Hark 같은 Qualcomm SoC 기기라면 이게 주인공일 가능성이 높다 → **F4** |
| **Core ML delegate** | Apple Neural Engine | iOS |
| **Flex delegate** | TF 커널 | 가속이 아니라 **호환성용** (7.1절) |
| 벤더 NPU delegate | MediaTek NeuroPilot, NXP(VX/Ethos-U) 등 | 각 벤더 SDK. MCU용 Ethos-U는 TFLM에서 custom op 형태로 붙는다 (F2) |

delegate를 고를 때의 체크리스트 (F8로 이어진다):

- 벤더의 **op 지원표**에서 내 모델의 op·dtype·파라미터(stride, dilation, 채널 수)가 다 지원되는가.
- 변환 후 실제 partition 수와 CPU에 남는 op 목록 (이 절의 예제 방식 또는 `benchmark_model`의 로그).
- 양자화 형식이 맞는가 — 많은 NPU는 **full-integer int8**(5절)만 받는다. float이나 dynamic-range 모델을 넣으면 통째로 CPU fallback이 될 수 있다.
- 정확도 — delegate 커널은 CPU 참조 커널과 반올림이 다를 수 있다. CPU 결과와 비교 (C8).

---

## 9. 벤치마크 — invoke 시간 재기

### 9.1 Python에서 재기 — threads, XNNPACK on/off, 참조 커널

예제 14 — big 모델의 f32·dynamic·int8 버전을 resolver 세 가지(AUTO = XNNPACK 기본 켜짐, BUILTIN_WITHOUT_DEFAULT_DELEGATES = XNNPACK 끔, BUILTIN_REF = 최적화 안 된 참조 커널) × 스레드 1/4로 2,000번씩 invoke해 중앙값과 p99를 잰다 (`ex11_bench.py`, Apple M2 Mac).

```python
import os, time; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import numpy as np, tensorflow as tf
R = tf.lite.experimental.OpResolverType
x = np.load("Xte.npy")[:1]
def bench(path, threads, resolver, n=2000):
    it = tf.lite.Interpreter(model_path=path, num_threads=threads,
                             experimental_op_resolver_type=resolver)
    it.allocate_tensors(); d = it.get_input_details()[0]
    xin = x if d["dtype"] == np.float32 else np.zeros(d["shape"], d["dtype"])
    it.set_tensor(d["index"], xin)
    for _ in range(50): it.invoke()                      # warm-up
    t = []
    for _ in range(n):
        t0 = time.perf_counter_ns(); it.invoke(); t.append(time.perf_counter_ns() - t0)
    return np.median(t) / 1e3, np.percentile(t, 99) / 1e3
print("model     resolver                          thr  median_us  p99_us")
for m in ["f32", "dynamic", "int8"]:
    for r in [R.AUTO, R.BUILTIN_WITHOUT_DEFAULT_DELEGATES, R.BUILTIN_REF]:
        for th in [1, 4]:
            try: med, p99 = bench(f"imu_big_{m}.tflite", th, r)
            except ValueError as e: print(f"{m:8s}  {r.name:32s}  {th:3d}  로드 실패: {str(e)[:58]}"); break
            print(f"{m:8s}  {r.name:32s}  {th:3d}  {med:9.1f}  {p99:6.1f}")
```

```text
model     resolver                          thr  median_us  p99_us
f32       AUTO                                1       23.5    36.9
f32       AUTO                                4       28.1    73.8
f32       BUILTIN_WITHOUT_DEFAULT_DELEGATES    1       30.2    38.9
f32       BUILTIN_WITHOUT_DEFAULT_DELEGATES    4       24.8    54.6
f32       BUILTIN_REF                         1      323.9   421.0
f32       BUILTIN_REF                         4      289.3   372.2
dynamic   AUTO                                1        8.2     8.9
dynamic   AUTO                                4       11.8    38.1
dynamic   BUILTIN_WITHOUT_DEFAULT_DELEGATES    1       12.9    24.6
dynamic   BUILTIN_WITHOUT_DEFAULT_DELEGATES    4       14.8    36.9
dynamic   BUILTIN_REF                         1  로드 실패: Didn't find op for builtin opcode 'FULLY_CONNECTED' versio
int8      AUTO                                1        7.0     7.6
int8      AUTO                                4        8.8    26.6
int8      BUILTIN_WITHOUT_DEFAULT_DELEGATES    1       12.4    12.5
int8      BUILTIN_WITHOUT_DEFAULT_DELEGATES    4       14.8    33.7
int8      BUILTIN_REF                         1      111.8   208.6
int8      BUILTIN_REF                         4      111.8   130.0
```

```svg
<svg viewBox="0 0 640 270" xmlns="http://www.w3.org/2000/svg">
<text x="320" y="20" font-size="13" text-anchor="middle">imu_big invoke 중앙값 (µs, 1 thread, Apple M2)</text>
<line x1="110" y1="35" x2="110" y2="225" stroke="currentColor"/><line x1="110" y1="225" x2="470" y2="225" stroke="currentColor"/>
<text x="110" y="242" font-size="12" text-anchor="middle">0</text><text x="210" y="242" font-size="12" text-anchor="middle">10</text><text x="310" y="242" font-size="12" text-anchor="middle">20</text><text x="410" y="242" font-size="12" text-anchor="middle">30</text>
<line x1="210" y1="225" x2="210" y2="229" stroke="currentColor"/><line x1="310" y1="225" x2="310" y2="229" stroke="currentColor"/><line x1="410" y1="225" x2="410" y2="229" stroke="currentColor"/>
<text x="100" y="72" font-size="12" text-anchor="end">float32</text>
<rect x="110" y="45" width="235" height="20" fill="#4a7bd0"/><text x="351" y="60" font-size="12">23.5</text>
<rect x="110" y="67" width="302" height="20" fill="#888"/><text x="418" y="82" font-size="12">30.2</text>
<text x="100" y="132" font-size="12" text-anchor="end">dynamic</text>
<rect x="110" y="105" width="82" height="20" fill="#4a7bd0"/><text x="198" y="120" font-size="12">8.2</text>
<rect x="110" y="127" width="129" height="20" fill="#888"/><text x="245" y="142" font-size="12">12.9</text>
<text x="100" y="192" font-size="12" text-anchor="end">int8</text>
<rect x="110" y="165" width="70" height="20" fill="#4a7bd0"/><text x="186" y="180" font-size="12">7.0</text>
<rect x="110" y="187" width="124" height="20" fill="#888"/><text x="240" y="202" font-size="12">12.4</text>
<rect x="480" y="60" width="14" height="14" fill="#4a7bd0"/><text x="500" y="72" font-size="12">XNNPACK (AUTO)</text>
<rect x="480" y="84" width="14" height="14" fill="#888"/><text x="500" y="96" font-size="12">XNNPACK 끔</text>
<text x="480" y="130" font-size="12">참조 커널 (REF):</text><text x="480" y="148" font-size="12">f32 323.9 µs</text><text x="480" y="166" font-size="12">int8 111.8 µs</text><text x="480" y="184" font-size="12">(축 밖)</text>
<text x="320" y="262" font-size="12" text-anchor="middle">한 번 실행한 결과 — 같은 측정을 반복하면 수 µs씩 흔들린다</text>
</svg>
```

그림 7 — big 모델의 1-thread invoke 중앙값. 파랑은 XNNPACK(기본), 회색은 XNNPACK을 끈 기본 builtin 커널. int8 + XNNPACK이 7.0 µs로 가장 빠르고, 최적화 안 된 참조 커널(REF)은 14~16배 느리다.

출력에서 볼 것:

- **int8 + XNNPACK = 7.0 µs**, f32 + XNNPACK = 23.5 µs → int8이 약 3.4배 빠르다. 연산량은 같고(MAC 약 35만 개, D1 방식으로 계산) 데이터 폭이 ¼이라 SIMD 한 번에 4배 많은 원소를 처리한다 (E2의 NEON `SDOT` 같은 int8 dot-product 명령).
- **XNNPACK 끔 → 1.3~1.8배 느림**. 그래서 XNNPACK이 기본으로 켜져 있다.
- **참조 커널(REF)은 int8 111.8 µs, f32 323.9 µs** — 최적화 커널보다 14~16배 느리다. REF는 "읽기 쉬운 이중·삼중 루프" 구현으로, 6.2절처럼 **정확성 검증용**이지 성능용이 아니다. TFLM의 기본 커널도 이 참조 구현에 가깝고, 그래서 MCU에서는 CMSIS-NN 같은 최적화 커널로 바꾸는 것이 필수다 (F2).
- **스레드 4개가 1개보다 빠르지 않다.** 7~30 µs짜리 작은 모델에서는 스레드 깨우기·동기화 비용이 계산보다 크다. p99도 나빠진다(int8: 7.6 → 26.6 µs). 웨어러블에서는 전력까지 생각하면 **작은 모델 = 1 thread**가 기본이고, 큰 모델에서만 스레드 수를 sweep한다.
- **dynamic + REF는 로드조차 실패** — 7.2절의 op 버전 문제(참조 resolver에 hybrid FULLY_CONNECTED v12가 없다).
- p99가 중앙값보다 훨씬 큰 줄들이 있다 — macOS 스케줄러, 다른 프로세스, 주파수 변화 탓이다. 실시간 예산(D6)을 볼 때는 평균이 아니라 **꼬리(p99, max)** 를 본다. 기기에서는 CPU governor를 고정하고 재야 재현성이 생긴다.

### 9.2 공식 도구 `benchmark_model` (이 환경에서는 빌드하지 않음)

TFLite에는 C++ 벤치마크 바이너리 `benchmark_model`이 있다 (소스: `tensorflow/lite/tools/benchmark/`). Bazel로 빌드하거나 공식 사이트에서 플랫폼별 prebuilt(nightly) 바이너리를 받는다. Android에서는 `adb push`로 올려 기기에서 직접 돌린다. 아래는 **실행하지 않은** 사용 예시다 (플래그 이름은 공식 문서 기준, 버전에 따라 다를 수 있다).

```sh
# (예시, 미실행) Android 기기에서
adb push benchmark_model /data/local/tmp/
adb push imu_big_int8.tflite /data/local/tmp/
adb shell /data/local/tmp/benchmark_model \
    --graph=/data/local/tmp/imu_big_int8.tflite \
    --num_threads=1 --num_runs=500 --warmup_runs=50 \
    --use_xnnpack=true \
    --enable_op_profiling=true        # op별 시간 표 + 어떤 노드가 delegate됐는지
# GPU delegate 비교: --use_gpu=true   / NNAPI: --use_nnapi=true
```

`--enable_op_profiling=true`의 op별 표가 9.1절보다 훨씬 유용하다 — 어느 op이 시간을 먹는지, delegate 노드가 몇 개인지, 초기화 시간과 메모리 footprint까지 나온다. Qualcomm QNN delegate 같은 외부 delegate도 이 도구에 플래그(외부 delegate 경로 지정)로 붙여 측정할 수 있다 (F4).

---

## 10. 앱·네이티브 통합 스케치 (실행하지 않음)

### 10.1 Android — Kotlin Interpreter API

아래는 **실행·빌드하지 않은** 구조 스케치다. 클래스 이름은 TFLite Java API(`org.tensorflow.lite.Interpreter`) 기준이고, LiteRT 이름 변경에 따라 Gradle 의존성 이름(예: `org.tensorflow:tensorflow-lite` → `com.google.ai.edge.litert:litert`)이 바뀌는 중이므로 공식 문서를 확인할 것.

```text
// (스케치, 미실행) Kotlin — 앱 시작 시 1회
val model: MappedByteBuffer = loadModelFile(context, "imu_big_int8.tflite")   // assets를 mmap
val options = Interpreter.Options().apply {
    setNumThreads(1)
    // addDelegate(GpuDelegate())       // GPU를 쓰려면 (float/fp16 모델)
    // addDelegate(qnnDelegate)         // Qualcomm QNN delegate (F4)
}
val interpreter = Interpreter(model, options)

// 추론마다 — int8 입력은 직접 양자화 (s_in, z_in은 interpreter.getInputTensor(0).quantizationParams())
val input = ByteBuffer.allocateDirect(64 * 3).order(ByteOrder.nativeOrder())
for (v in imuWindow) input.put(clampToInt8(round(v / sIn) + zIn))
input.rewind()
val output = Array(1) { ByteArray(3) }
interpreter.run(input, output)          // = set_tensor + invoke + get_tensor
// logits = sOut * (output[0][k] - zOut)
interpreter.close()                     // 네이티브 메모리 해제 — 잊으면 누수
```

포인트: (1) 모델은 `MappedByteBuffer`(mmap)로 — flatbuffer라 복사 없이 쓴다. (2) `ByteBuffer.allocateDirect`로 JNI 복사를 줄인다. (3) int8 모델이면 **앱이 양자화·역양자화를 해야 한다** — 5절 `run()`의 두 줄과 같은 일이다. (4) `Interpreter`는 스레드 안전하지 않으므로 스레드마다 하나씩 또는 직렬화.

### 10.2 C++ API

Linux 기기나 Android NDK(F7)에서 네이티브로 쓰는 모양. **빌드·실행하지 않은** 스케치이고, 헤더 경로는 TensorFlow 소스 트리 기준이다.

```text
// (스케치, 미실행) C++
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model.h"

auto model = tflite::FlatBufferModel::BuildFromFile("imu_big_int8.tflite");  // mmap
tflite::ops::builtin::BuiltinOpResolver resolver;                            // ② OpResolver
std::unique_ptr<tflite::Interpreter> interp;
tflite::InterpreterBuilder(*model, resolver)(&interp);
interp->SetNumThreads(1);
// 선택: delegate — 예) GPU
//   auto opts = TfLiteGpuDelegateOptionsV2Default();
//   TfLiteDelegate* gpu = TfLiteGpuDelegateV2Create(&opts);
//   interp->ModifyGraphWithDelegate(gpu);                                   // ③
interp->AllocateTensors();                                                   // ④ arena

int8_t* in = interp->typed_input_tensor<int8_t>(0);                          // ⑤ 버퍼에 직접 쓰기
const TfLiteTensor* t = interp->input_tensor(0);
float s = t->params.scale; int zp = t->params.zero_point;
for (int i = 0; i < 64 * 3; ++i) in[i] = clamp_s8(lrintf(imu[i] / s) + zp);
interp->Invoke();                                                            // ⑥
int8_t* out = interp->typed_output_tensor<int8_t>(0);                        // ⑦ 복사 없이 읽기
```

C++에서는 `typed_input_tensor`가 interpreter 내부 버퍼 포인터를 주므로 **복사 없이** 센서 데이터를 바로 쓸 수 있다 (Python `set_tensor`는 복사). 펌웨어 감각으로는 "DMA 목적지 버퍼를 arena 안 입력 텐서로 잡는" 최적화가 가능하다는 뜻이다 — 단, `AllocateTensors`를 다시 하면 주소가 바뀌므로 포인터를 그때마다 새로 받아야 한다. TFLM의 `MicroInterpreter`도 `input(0)->data.int8`로 같은 구조다 (F2).

---

## 11. 임베디드 관점에서 다시 보기

| 항목 | 큰 SoC (Android/Linux, TFLite·LiteRT) | MCU (TFLM, F2) | Don이 확인할 것 |
|---|---|---|---|
| 모델 위치 | 파일 → mmap | flash의 `const` 배열 (XIP) | 정렬(`alignas(16)`), `TFL3` 식별자, verifier |
| 메모리 | interpreter가 arena를 heap에 할당 | **사용자가 준 고정 arena** (malloc 없음) | `arena_used_bytes`로 실측, D2 예산 |
| op 구현 | `BuiltinOpResolver` 전체 + delegate | `MicroMutableOpResolver<N>`에 필요한 op만 | 모델 op 목록 = resolver 등록 목록 |
| 양자화 | float도 OK, int8이면 빠름 | 사실상 **full-integer int8** 필수 | QUANTIZE/DEQUANTIZE가 그래프 안쪽에 없는가 |
| 가속 | delegate (XNNPACK, GPU, QNN…) | 최적화 커널 교체 (CMSIS-NN), Ethos-U custom op | partition 수, CPU fallback op |
| 버전 | 앱의 런타임 AAR/so 버전 | TFLM 커밋 | 변환기 버전 ↔ 런타임 op 버전 (7.2절) |
| 입출력 | 앱이 양자화/역양자화 | 펌웨어가 센서 raw → int8 직접 | 입력 scale·zp를 펌웨어 상수로 export |

펌웨어 쪽에서 특히 쓸모 있는 관찰 몇 가지:

- **입력 scale·zero-point는 펌웨어 상수다.** 예제 8의 s_in = 0.01727, zp = −55는 "가속도 1 LSB(int8) = 0.01727 g"라는 뜻이다. IMU가 ±4 g 범위 16비트 raw를 준다면, 펌웨어는 raw → g 변환과 int8 양자화를 **한 번의 정수 multiply-shift**로 합칠 수 있다 (C1 1.4절의 Q-format 감각). 모델을 다시 변환할 때마다 이 상수가 바뀌므로 **빌드 시스템이 `.tflite`에서 자동 추출**하게 만드는 것이 안전하다 — 예제 4/5처럼 파일에서 읽으면 된다.
- **Conv1D가 만드는 EXPAND_DIMS/RESHAPE**는 MCU에서도 비용이다. TFLM의 RESHAPE는 대개 메모리 복사가 없거나 작지만, op 하나하나가 resolver 등록·코드 크기·호출 오버헤드다. IMU·오디오 1D 모델은 처음부터 `(N, T, 1, C)` 4D로 짜는 팀이 많다 (예제 13).
- **작은 모델에서는 메타데이터가 파일의 60%** (예제 3). flash가 빠듯하면 텐서 이름을 줄이는 도구나 metadata 제거를 고려할 수 있지만, 디버깅 정보를 잃는 트레이드오프다.
- **REF 커널 ≈ TFLM 기본 커널** 감각: 예제 14에서 REF가 최적화 커널보다 14~16배 느렸다. MCU에서 "모델이 생각보다 10배 느리다"면 CMSIS-NN이 링크됐는지부터 본다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| int8 모델에 float 입력을 `astype(np.int8)`로 넣음 | 정확도가 찍기 수준, 출력이 거의 상수 | 입력 scale·zp를 무시 (1.0 g가 int8 1이 됨) | `q = round(x/s) + z` 후 clamp. 출력도 `s(q − z)` |
| `supported_ops=[TFLITE_BUILTINS_INT8]`를 빼고 int8 변환 | PC에서는 잘 도는데 NPU/MCU에서 실패 또는 매우 느림 | 일부 op이 float으로 남고 QUANTIZE/DEQUANTIZE가 끼어 있음 | INT8 ops set을 지정해 변환 실패로 드러나게 하고, op 목록을 grep |
| representative dataset에 테스트셋이나 엉뚱한 분포 사용 | 평가 수치가 좋게 나오거나, 실기기에서 정확도 급락 | calibration 범위가 실제 입력과 다름, 평가 오염 | 학습 분포에서 100~500개, 실제 전처리 그대로 |
| representative dataset에 전처리 빠짐 | 특정 층이 포화(출력이 127/−128에 몰림) | 정규화 전 raw 값으로 범위를 잡음 | 추론과 **같은 전처리 함수**를 통과시킨 데이터 사용 |
| dynamic-range 변환 후 "4배 줄었다"고 가정 | 작은 모델 파일이 그대로 | 작은 가중치 텐서는 양자화를 건너뜀 (예제 6) | 변환 후 dtype·크기를 확인 (예제 7) |
| 변환기는 최신, 기기 런타임은 구버전 | `Didn't find op for builtin opcode ... version` | op 버전 불일치 | 버전 매트릭스 관리, 런타임 업데이트 또는 변환기 고정 (F8) |
| Flex op을 허용해서 변환 성공 → 배포 | 앱에서 "Select TensorFlow op(s) ... not supported" 또는 바이너리 크기 폭증 | Flex delegate 미링크 / 링크 시 수 MB | 지원 op으로 재작성 (예제 11 ④) |
| `resize_tensor_input` 후 `allocate_tensors` 생략 | 크래시 또는 이전 shape로 실행 | arena 재계획 안 됨 | resize 후 항상 allocate |
| NPU delegate를 붙였더니 더 느림 | CPU보다 지연 증가 | partition이 여러 개로 끊겨 경계 복사·동기화 비용 | op 프로파일로 partition 확인, 미지원 op 제거·재작성 (예제 13) |
| 작은 모델에 스레드를 많이 씀 | 중앙값 비슷, p99 악화, 전력 증가 | 스레드 동기화 오버헤드 > 계산 | 1 thread부터 sweep (예제 14) |

---

## 13. 면접에서 이렇게 말한다

**Q.** Walk me through converting a model to int8 TFLite.

**A.** 학습된 float 모델(Keras나 SavedModel, PyTorch면 ai-edge-torch)을 `TFLiteConverter`에 넣고, `optimizations=DEFAULT`, `representative_dataset`(전처리된 실제 분포 입력 수백 개), `supported_ops=TFLITE_BUILTINS_INT8`, 입출력 타입 int8을 지정한다. 변환 후에는 반드시 검사한다: op 목록에 QUANTIZE/DEQUANTIZE가 그래프 안쪽에 없는지, weight가 per-channel int8·bias int32인지, 입력 scale/zero-point가 무엇인지. 그다음 float 골든과 같은 테스트셋으로 정확도와 출력 SQNR을 비교하고, 타깃에서 delegate가 그래프를 몇 조각으로 가져가는지와 지연을 잰다. 제 실험에서 64K 파라미터 IMU 모델은 파일이 256 KB에서 73 KB로 줄었고 정확도는 99.33% 그대로였다.

> "I take the trained float model, run the TFLite converter with optimizations set to default, a representative dataset of a few hundred real, preprocessed inputs, the ops set restricted to TFLITE_BUILTINS_INT8, and int8 input and output types. Then I inspect the result: no quantize or dequantize ops in the middle of the graph, per-channel int8 weights with int32 biases, and I record the input scale and zero point because the firmware has to quantize sensor data with them. Finally I compare against the float model on the same test set — accuracy, argmax agreement and output SQNR — and on the target I check how many partitions the delegate takes and the latency. On a 64K-parameter IMU model I did this with, the file went from 256 KB to 73 KB with no accuracy change."

**Q.** What is a representative dataset for?

**A.** full-integer 양자화에서 activation의 범위를 정하기 위한 calibration 입력이다. weight는 값이 고정이라 그대로 min/max를 보면 되지만, activation은 입력에 따라 달라지므로 변환기가 대표 입력으로 float 모델을 돌려 각 텐서의 min/max를 기록하고 거기서 scale과 zero-point를 정한다. 라벨은 필요 없고, 실제 배포 분포와 **같은 전처리**를 거친 데이터여야 한다. 분포가 다르면 범위가 틀려 포화나 해상도 손실이 생긴다. 테스트셋은 쓰지 않는다.

> "It's calibration data for the activations. Weights are constants, so their ranges are known, but activation ranges depend on the input. The converter runs the float model on the representative samples, records min and max for every activation tensor, and derives each tensor's scale and zero point from that. It needs no labels, but it must match the deployment distribution and go through exactly the same preprocessing; otherwise you get clipping or wasted resolution. Typically a few hundred samples from the training distribution, never the test set."

**Q.** Dynamic-range vs full-integer quantization?

**A.** dynamic-range는 weight만 int8로 저장하고 activation은 float으로 둔다. 일부 커널이 실행 중에 activation을 동적으로 양자화해 정수 MAC을 쓰지만, 그래프 입출력과 저장은 float이다. 대표 데이터가 필요 없고 CPU에서 크기 ¼과 속도 이득이 있다. full-integer는 activation까지 calibration으로 고정 scale을 정해서 모든 텐서가 int8이고, 정수 전용 하드웨어(MCU, DSP, NPU)가 받을 수 있는 형태다. edge 가속기 타깃이면 full-integer가 기본이다. 실측에서 dynamic이 int8보다 SQNR이 높았지만(55 dB vs 30 dB), NPU에 올릴 수 있는 건 int8 쪽이다. 그리고 작은 가중치 텐서는 dynamic에서 아예 양자화가 안 될 수 있어 확인이 필요하다.

> "Dynamic-range quantization stores only the weights as int8; activations stay float, and some kernels quantize them on the fly at runtime. It needs no calibration data and gives you a 4x smaller model and some CPU speedup. Full-integer quantization also fixes activation scales offline using a representative dataset, so every tensor, including inputs and outputs, is int8. That's what integer-only hardware — MCUs, DSPs, NPUs — can actually execute, so it's the default for edge accelerators. Dynamic-range is often slightly more accurate, but it's a CPU-only optimization."

**Q.** What is a delegate, and what happens with unsupported ops?

**A.** delegate는 interpreter에 꽂는 백엔드 플러그인이다. interpreter가 그래프를 보여 주면 delegate가 지원하는 노드를 고르고, 연결된 지원 노드 묶음마다 하나의 delegate 노드로 바뀌어 GPU/NPU/SIMD에서 실행된다. 지원 안 되는 op은 CPU 기본 커널에서 돌고, 그래프가 여러 partition으로 쪼개진다. 경계마다 데이터 복사·layout 변환·동기화가 생기므로 partial delegation은 전부 CPU보다 느려질 수도 있다. 실제로 Keras Conv1D 모델은 EXPAND_DIMS 때문에 XNNPACK partition이 3개로 끊겼고, Conv2D로 다시 짜니 1개가 됐다. 변환 단계에서 아예 TFLite에 없는 op은 변환이 실패하고, Flex나 custom op으로 우회할 수 있지만 edge에서는 지원되는 op으로 재작성하는 게 보통이다.

> "A delegate is a pluggable backend. The interpreter asks it which nodes it can handle, then replaces each connected group of supported nodes with a single delegate node that runs on the GPU, NPU or an optimized CPU library like XNNPACK. Unsupported ops fall back to the CPU builtin kernels, which splits the graph into partitions, and every boundary costs copies, layout conversion and synchronization — so a partially delegated model can be slower than pure CPU. I've seen a Conv1D model split into three XNNPACK partitions because of expand-dims ops; rewriting it as a Conv2D with a k-by-1 kernel gave a single partition. Ops that TFLite doesn't support at all fail at conversion time; you can use select TF ops or a custom op, but on edge devices I'd rather rewrite them with supported ops."

**Q.** How do you verify the TFLite model matches the original?

**A.** 세 단계로 한다. 첫째, float TFLite를 원본과 같은 입력으로 돌려 최대 절대 오차가 1e-5 정도인지 본다 — 여기서 크게 다르면 변환·전처리 버그다. 둘째, 양자화 모델은 같은 테스트셋에서 정확도·argmax 일치율·출력 SQNR을 비교하고, 문제가 있으면 QuantizationDebugger나 중간 텐서 보존으로 층별 오차를 본다. 셋째, 의심 층은 TFLite의 scale·zero-point를 꺼내 numpy 정수 연산으로 재현해 비트 단위로 맞추고, 타깃 C 구현도 그 정수 시뮬레이션과 bit-exact로 맞춘다. 실제로 conv 한 층 480개 출력을 0 LSB 차이로 재현해 봤다.

> "Three levels. First, the float TFLite model against the original on identical inputs — max absolute difference should be around 1e-5; anything bigger means a conversion or preprocessing bug. Second, for the quantized model: accuracy, argmax agreement and output SQNR on the same test set, and if something's off, per-layer error with the quantization debugger or by preserving intermediate tensors. Third, for suspicious layers I pull the scales and zero points out of the file and reproduce the layer with integer math in numpy — I've matched all 480 outputs of a conv layer exactly — and the target C implementation has to be bit-exact against that integer reference."

**Q.** What's inside a .tflite file?

**A.** flatbuffer 하나다. root `Model` 테이블에 schema version, 사용하는 op 종류 표(operator_codes), subgraph들, buffer들, signature가 있다. subgraph에는 텐서 목록(shape, dtype, buffer 번호, 양자화 파라미터)과 operator 목록(opcode 번호, 입출력 텐서 번호, 옵션)이 실행 순서로 있다. 가중치는 buffer에 raw 바이트로 들어 있어 파싱 없이 포인터로 읽는다. 그래서 MCU에서 flash에 둔 채 실행할 수 있다.

> "It's a single FlatBuffer. The root Model table holds the schema version, a table of operator codes, the subgraphs, the raw buffers and the signatures. Each subgraph lists tensors — shape, type, buffer index and quantization parameters — and operators in execution order, each referring to an opcode index and input and output tensor indices. Weights live in the buffers as raw bytes, and because FlatBuffers need no parsing, the runtime reads them in place — which is why a microcontroller can execute the model straight from flash."

---

## 14. 직접 해보기

1. **손계산**: int8 입력 텐서의 scale = 0.02, zero-point = 10이다. x = −1.5, 0.0, 2.57을 양자화하고 다시 역양자화하라. 정답: −1.5/0.02 = −75 → −65 → −1.50 · 0 → 10 → 0.00 · 2.57/0.02 = 128.5 → round 128(numpy는 짝수 반올림) → 138 → clamp 127 → 2.34 (포화).
2. **손계산**: conv 출력 채널의 s_in = 0.017, s_w = 0.0066, s_out = 0.023, int32 누산값 acc = 1500, z_out = −128, ReLU fused다. q_out은? 정답: M = 0.017 × 0.0066 / 0.023 = 0.004878, 1500 × M = 7.32 → 7, 7 + (−128) = −121 (≥ −128이므로 ReLU 영향 없음), 역양자화 0.023 × 7 = 0.161.
3. **손계산**: 파라미터 200,000개 모델의 float32·float16·int8 `.tflite` 크기를 예상하라 (메타데이터 5 KB 가정). 정답: 약 786 KB, 396 KB, 200 KB (+ bias를 int32로 저장하는 만큼 조금 더).
4. **코드**: 예제 6의 representative dataset 개수를 5개, 20개, 100개, 500개로 바꿔 big int8 모델의 SQNR과 입력 scale/zero-point가 어떻게 변하는지 표로 만들어라. 정답/힌트: 샘플이 적으면 min/max가 실제 분포보다 좁게 잡혀 두드리기 펄스 같은 큰 값이 포화된다. scale이 작아지고 SQNR·정확도가 흔들리는지 본다.
5. **코드**: 예제 13의 Conv2D 버전 모델을 학습시켜 int8로 변환하고, 예제 14로 Conv1D 버전과 지연을 비교하라. 정답/힌트: 학습 데이터는 `X[:, :, None, :]`로 4D로 만든다. XNNPACK partition이 1개가 되면서 지연이 줄어드는지, 정확도가 같은지 확인.
6. **코드**: 예제 9를 FULLY_CONNECTED 층(`ops[10]`)에 대해서도 해 보라. 정답/힌트: FC 가중치는 `[출력, 입력]`이고 TFLite FC는 기본적으로 per-tensor 또는 per-channel일 수 있으니 `len(scales)`를 먼저 확인하고, 입력은 MEAN 출력(int8)이다. `acc = (x − z_in) @ W.T + b`.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| TFLite / LiteRT | TensorFlow Lite / Lite Runtime | Google의 온디바이스 추론 런타임. 2024년 LiteRT로 이름 변경 진행 |
| TFLM | TensorFlow Lite for Microcontrollers | 같은 `.tflite`를 malloc 없이 MCU에서 돌리는 구현 (F2) |
| TFLiteConverter | 변환기 | TF/Keras 그래프를 TFLite op으로 낮추고 양자화해 flatbuffer로 직렬화 |
| flatbuffer | FlatBuffers 직렬화 포맷 | offset으로 연결된 테이블, 파싱 없이 그 자리에서 읽힘 (zero-copy) |
| subgraph | 부분 그래프 | 텐서·operator 목록 묶음. 보통 `main` 하나 |
| operator code (opcode) | op 종류 표의 항목 | operator는 opcode 번호로 종류를 가리킨다. 버전 정보 포함 |
| buffer | 데이터 블록 | 상수 텐서의 raw 바이트. buffer 0은 빈 sentinel |
| signature | 공개 입출력 이름 | `serving_default: imu → output_0` 같은 함수 시그니처 |
| Interpreter | 실행기 | 모델 로드, op 연결, arena 할당, 실행 |
| OpResolver | op 등록표 | opcode → 커널 함수 매핑. AUTO / WITHOUT_DEFAULT_DELEGATES / REF |
| delegate | 위임 백엔드 | 지원하는 부분 그래프를 가속기에서 실행하는 플러그인 |
| partition | 위임 구간 | delegate 노드 하나가 맡은 연결된 노드 묶음 |
| XNNPACK | CPU용 신경망 라이브러리 | TFLite의 기본 CPU delegate. float·int8 SIMD 커널 |
| Flex (Select TF ops) | TF 커널 재사용 | builtin에 없는 op을 TF 커널로 실행, 바이너리가 커짐 |
| custom op | 사용자 정의 op | 사용자가 커널을 직접 등록해야 실행 가능 |
| representative dataset | 대표 데이터셋 | activation 범위를 재는 calibration 입력 생성기 |
| dynamic-range quantization | 동적 범위 양자화 | weight만 int8, activation은 float (실행 중 일부 동적 양자화) |
| full-integer quantization | 완전 정수 양자화 | weight·activation·입출력 전부 int8 |
| 16x8 | int16 activation + int8 weight | 실험적 모드, bias int64 |
| QUANTIZE / DEQUANTIZE | 경계 op | float ↔ 정수 변환. 그래프 안쪽에 있으면 float 구간이 있다는 뜻 |
| per-channel | 채널별 양자화 | conv/FC weight의 출력 채널마다 scale 1개 (`quantized_dimension`) |
| QuantizationDebugger | 양자화 디버거 | 층별 float vs 양자화 오차 통계 |
| benchmark_model | 공식 벤치마크 도구 | op별 프로파일·delegate 확인용 C++ 바이너리 |

---

## 16. 요약 & 체크리스트

TFLite/LiteRT는 **변환기 → `.tflite` flatbuffer → interpreter(+delegate)** 세 부분으로 된 배포 경로다. 변환기는 Keras/SavedModel 그래프를 builtin op으로 낮추고(Conv1D → EXPAND_DIMS + CONV_2D, conv + ReLU fusion, OHWI layout), 설정 조합으로 float·dynamic·float16·full-int8·16x8 양자화를 고른다. edge 가속기용은 **representative dataset + TFLITE_BUILTINS_INT8 + int8 입출력**의 full-integer가 기본이며, 결과 파일에는 per-tensor 비대칭 activation, per-channel 대칭 weight, `s_in × s_w` scale의 int32 bias가 C1 관례 그대로 들어 있다. `.tflite`는 파싱 없이 읽히는 flatbuffer라 Python 스키마든 50줄짜리 C든 바로 열어 볼 수 있다. interpreter는 로드 → resolver → delegate → allocate를 한 번 하고, 추론마다 set/invoke/get만 한다. delegate는 지원 구간을 partition 단위로 가져가며, partition이 끊길수록 경계 비용이 커진다. 검증은 float 골든 비교 → 정확도·SQNR → 층별 오차 → 정수 재현의 순서로 한다.

- [ ] Keras 모델을 float `.tflite`로 변환하고 Interpreter의 다섯 단계(allocate, set, invoke, get)로 실행해 원본과 max diff를 비교할 수 있다
- [ ] `.tflite` 안의 Model / operator_codes / subgraph / tensor / operator / buffer 관계를 그림으로 설명하고, Analyzer 출력을 읽을 수 있다
- [ ] 파일 첫 8바이트(root offset, `TFL3`)와 vtable 규칙으로 C에서 모델 blob을 검사할 수 있다
- [ ] dynamic / float16 / full-int8 / 16x8 변환의 설정 차이와 결과(dtype, op, 크기)를 표로 말할 수 있다
- [ ] representative dataset의 역할과 잘못 골랐을 때의 증상을 설명할 수 있다
- [ ] int8 모델에서 입력 scale·zp를 꺼내 손으로 양자화하고, 출력을 역양자화할 수 있다
- [ ] conv 층 하나를 `acc = Σ(q_x − z)·q_w + q_b`, `round(acc·M) + z_out`으로 재현해 TFLite와 비트 단위로 비교할 수 있다
- [ ] builtin / Flex / custom op의 차이와, 지원 안 되는 op을 만났을 때의 대처(재작성)를 말할 수 있다
- [ ] delegate partitioning이 무엇이고 왜 partial delegation이 느릴 수 있는지 숫자 예로 설명할 수 있다
- [ ] XNNPACK on/off, 스레드 수, 참조 커널의 지연 차이를 재고 해석할 수 있다

## 참고 자료

- LiteRT 공식 문서 (모델 변환, 양자화, delegate, 벤치마크): https://ai.google.dev/edge/litert
- TensorFlow Lite 스키마 원본 `schema.fbs`: https://github.com/tensorflow/tensorflow/blob/master/tensorflow/lite/schema/schema.fbs
- FlatBuffers 공식 문서 (내부 포맷 설명 포함): https://flatbuffers.dev
- TensorFlow 저장소의 `tensorflow/lite/tools/benchmark/` (benchmark_model 소스와 README): https://github.com/tensorflow/tensorflow
- XNNPACK: https://github.com/google/XNNPACK
- ai-edge-torch (PyTorch → TFLite): https://github.com/google-ai-edge/ai-edge-torch
- Jacob et al., "Quantization and Training of Neural Networks for Efficient Integer-Arithmetic-Only Inference" (CVPR 2018) — TFLite int8 방식의 원 논문
- Pete Warden & Daniel Situnayake, "TinyML" (O'Reilly, 2019) — TFLite → TFLM 흐름 (F2와 함께)
- 이 저장소의 선행 노트: C1(양자화 수식·TFLite 관례), C2(PTQ·calibration), C6(fusion·layout·CPU fallback), C8(검증), D2(arena)
