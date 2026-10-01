# F6. 그 밖의 런타임과 ML 컴파일러 — Core ML, ExecuTorch, MediaPipe, TVM·MLIR·IREE, Edge Impulse, STM32Cube.AI

> **이 노트를 다 읽으면**: 프레임워크 → 교환 포맷 → 컴파일러·런타임 → 하드웨어로 이어지는 edge ML 지형도를 그리고 각 도구가 어느 칸에 있는지 말할 수 있다 · PyTorch 모델을 Core ML로 변환해 compute unit(CPU/GPU/Neural Engine)별 latency·정밀도를 직접 재고, weight palettization·int8 압축의 크기/정확도 trade-off를 숫자로 보일 수 있다 · `torch.export` → lowering → backend delegation으로 이어지는 ExecuTorch 흐름과 TVM·MLIR·IREE 같은 ML 컴파일러가 interpreter와 무엇이 다른지 설명할 수 있다 · 새 기기(예: Qualcomm SoC + MCU 웨어러블)의 엔진별 런타임을 기준표로 골라 근거를 댈 수 있다
> **JD 연결**: "Evaluate and select silicon platforms", "Work with platform vendors to bring up toolchains, SDKs and new accelerator", "Experience with TFLite, llamacpp, QNN or similar" — 여기서 **"or similar"**가 이 노트의 범위다 · study_prep_list **F6** 행: ExecuTorch, Core ML, MediaPipe, TVM / MLIR / IREE 개념, Edge Impulse, STM32Cube.AI — "플랫폼 평가 시 비교 대상" (M1 평가 기준표와 직결)
> **Don 기준 난이도**: 툴체인(컴파일러·링커·벤더 SDK)을 고르고 버전 매트릭스를 관리하고, 도구가 만든 결과물을 열어 검증하는 습관은 이미 강함 / 각 런타임의 이름·포맷·위치, ML 컴파일러의 IR 계층(MLIR dialect), Core ML의 compute unit 개념과 Apple식 압축 API는 새로 배움
> **선행 노트**: A5 (`torch.export`·ONNX export), C1·C2 (양자화), C6 (graph 최적화·lowering·fusion), E5 (NPU 컴파일러·Vela·CPU fallback). 병렬 작성 중인 F1 (TFLite/LiteRT), F2 (TFLM), F3 (llama.cpp), F4 (QNN), F5 (ONNX Runtime)는 ID로만 가리킨다

---

## 0. 큰 그림 — 이게 왜 필요한가

F1~F5는 JD에 이름이 박힌 런타임(TFLite, TFLM, llama.cpp, QNN, ONNX Runtime)을 하나씩 깊게 판다. 그런데 실제 일에서는 "어떤 런타임을 쓸지"부터 정해야 하는 순간이 온다. 새 칩을 평가할 때, 모델팀이 "우리는 PyTorch만 쓴다"고 할 때, 벤더가 "우리 NPU는 TVM 기반 컴파일러를 쓴다"고 할 때, iPhone 동반 앱에서 같은 모델을 돌려야 할 때. 이 노트는 그 순간을 위한 **지도**다.

Don에게 익숙한 말로 바꾸면 이렇다. 펌웨어 팀이 새 SoC를 받으면 "어느 컴파일러(GCC/Clang/IAR/벤더 xt-clang)로, 어느 RTOS(Zephyr/FreeRTOS/bare-metal)로, 어느 HAL 위에서 짤까"를 먼저 정한다. ML 배포도 똑같이 "어느 포맷으로 내보내고, 어느 컴파일러·런타임으로, 어느 가속기에서 돌릴까"를 정해야 한다. 선택지가 훨씬 많고, 서로 겹치고, 이름이 자주 바뀐다는 점만 다르다.

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f6a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs><rect x="10" y="60" width="120" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="70.0" y="87.0" font-size="12" text-anchor="middle">PyTorch</text><rect x="10" y="170" width="120" height="46" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="70.0" y="189.5" font-size="12" text-anchor="middle">TensorFlow</text><text x="70.0" y="204.5" font-size="12" text-anchor="middle">/ Keras</text><rect x="10" y="280" width="120" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="1.5"/><text x="70.0" y="307.0" font-size="12" text-anchor="middle">JAX</text><rect x="175" y="40" width="140" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="245.0" y="59.5" font-size="12" text-anchor="middle">torch.export</text><text x="245.0" y="74.5" font-size="12" text-anchor="middle">ExportedProgram</text><rect x="175" y="115" width="140" height="46" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="245.0" y="142.0" font-size="12" text-anchor="middle">ONNX (.onnx)</text><rect x="175" y="190" width="140" height="46" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="245.0" y="209.5" font-size="12" text-anchor="middle">TFLite flatbuffer</text><text x="245.0" y="224.5" font-size="12" text-anchor="middle">(.tflite)</text><rect x="175" y="265" width="140" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="1.5"/><text x="245.0" y="284.5" font-size="12" text-anchor="middle">StableHLO</text><text x="245.0" y="299.5" font-size="12" text-anchor="middle">(MLIR)</text><rect x="365" y="20" width="150" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="440.0" y="44.0" font-size="12" text-anchor="middle">ExecuTorch (.pte)</text><rect x="365" y="70" width="150" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="440.0" y="94.0" font-size="12" text-anchor="middle">Core ML (.mlpackage)</text><rect x="365" y="120" width="150" height="40" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="440.0" y="144.0" font-size="12" text-anchor="middle">ONNX Runtime</text><rect x="365" y="170" width="150" height="40" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="440.0" y="194.0" font-size="12" text-anchor="middle">TVM (Relax)</text><rect x="365" y="220" width="150" height="40" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="440.0" y="244.0" font-size="12" text-anchor="middle">LiteRT / TFLM</text><rect x="365" y="270" width="150" height="40" rx="5" fill="#d0564a" fill-opacity="0.15" stroke="#d0564a" stroke-width="1.5"/><text x="440.0" y="294.0" font-size="12" text-anchor="middle">vendor SDK</text><rect x="365" y="320" width="150" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="1.5"/><text x="440.0" y="344.0" font-size="12" text-anchor="middle">IREE / XLA</text><rect x="565" y="40" width="110" height="46" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="620.0" y="59.5" font-size="12" text-anchor="middle">Apple</text><text x="620.0" y="74.5" font-size="12" text-anchor="middle">CPU·GPU·ANE</text><rect x="565" y="115" width="110" height="46" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="620.0" y="134.5" font-size="12" text-anchor="middle">폰·웨어러블 SoC</text><text x="620.0" y="149.5" font-size="12" text-anchor="middle">CPU·GPU·DSP·NPU</text><rect x="565" y="190" width="110" height="46" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="620.0" y="209.5" font-size="12" text-anchor="middle">MCU</text><text x="620.0" y="224.5" font-size="12" text-anchor="middle">Cortex-M·Ethos-U</text><rect x="565" y="265" width="110" height="46" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="620.0" y="284.5" font-size="12" text-anchor="middle">서버·PC</text><text x="620.0" y="299.5" font-size="12" text-anchor="middle">CPU·GPU</text><line x1="130.0" y1="83.0" x2="175.0" y2="63.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="130.0" y1="83.0" x2="175.0" y2="138.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="130.0" y1="193.0" x2="175.0" y2="213.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="130.0" y1="193.0" x2="175.0" y2="288.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="130.0" y1="303.0" x2="175.0" y2="288.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="130.0" y1="83.0" x2="175.0" y2="213.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="315.0" y1="63.0" x2="365.0" y2="40.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="315.0" y1="63.0" x2="365.0" y2="90.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="315.0" y1="138.0" x2="365.0" y2="140.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="315.0" y1="138.0" x2="365.0" y2="190.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="315.0" y1="213.0" x2="365.0" y2="240.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="315.0" y1="213.0" x2="365.0" y2="290.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="315.0" y1="138.0" x2="365.0" y2="290.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="315.0" y1="288.0" x2="365.0" y2="340.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="515.0" y1="40.0" x2="565.0" y2="138.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="515.0" y1="90.0" x2="565.0" y2="63.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="515.0" y1="40.0" x2="565.0" y2="63.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="515.0" y1="140.0" x2="565.0" y2="138.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="515.0" y1="140.0" x2="565.0" y2="288.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="515.0" y1="190.0" x2="565.0" y2="288.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="515.0" y1="190.0" x2="565.0" y2="213.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="515.0" y1="240.0" x2="565.0" y2="138.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="515.0" y1="240.0" x2="565.0" y2="213.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="515.0" y1="290.0" x2="565.0" y2="138.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="515.0" y1="290.0" x2="565.0" y2="213.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="515.0" y1="340.0" x2="565.0" y2="288.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><line x1="515.0" y1="340.0" x2="565.0" y2="138.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6a)"/><text x="70" y="14" font-size="13" text-anchor="middle" font-weight="bold">1 프레임워크</text><text x="245" y="14" font-size="13" text-anchor="middle" font-weight="bold">2 교환 포맷</text><text x="440" y="14" font-size="13" text-anchor="middle" font-weight="bold">3 컴파일러·런타임</text><text x="620" y="14" font-size="13" text-anchor="middle" font-weight="bold">4 하드웨어</text>
</svg>
```

그림 1 — edge ML 배포의 네 층. 왼쪽에서 오른쪽으로 모델이 흘러간다. 같은 모델이 여러 길로 하드웨어에 닿을 수 있고, 어느 길이 좋은지는 하드웨어 벤더 지원과 op 커버리지가 결정한다. (화살표는 대표 경로만 그렸다.)

| 층 | 하는 일 | 예 | 펌웨어 비유 |
|---|---|---|---|
| 1 프레임워크 | 모델을 정의하고 학습 | PyTorch, TensorFlow/Keras, JAX | 알고리즘을 MATLAB/Python으로 검증하는 단계 |
| 2 교환 포맷 | 학습 코드에서 "계산 그래프 + 가중치"만 뽑아 파일로 | `torch.export` ExportedProgram, ONNX, `.tflite`, StableHLO | 소스 → 오브젝트 파일(`.o`) 같은 중간 산출물 |
| 3 컴파일러·런타임 | 그래프를 기기에 맞게 바꾸고(최적화·lowering) 실행 | Core ML, ExecuTorch, LiteRT/TFLM, ORT, TVM, IREE, 벤더 SDK | 컴파일러 + 링커 + RTOS/HAL |
| 4 하드웨어 | 실제 MAC을 수행 | CPU(NEON/Helium), GPU, DSP(Hexagon), NPU(ANE, Ethos-U, HTP) | 실리콘 |

이 노트에서 다루는 것:

- **Core ML** — Apple 기기 전용. 이 Mac(M2)에 Neural Engine이 있으니 **실제로 변환·측정**한다 (4절).
- **ExecuTorch** — PyTorch 공식 온디바이스 런타임. 설치되어 있지 않아 흐름은 "실행 안 함" 코드로, 첫 단계인 `torch.export`는 실제로 돌린다 (1·3·5절).
- **MediaPipe, LiteRT Next** — TFLite 위의 상위 계층 (6절).
- **ML 컴파일러** — TVM, MLIR, IREE, XLA/StableHLO. "lowering"과 "tiling·fusion pass"를 직접 돌려 본다 (2·3·7절).
- **MCU 도구** — Edge Impulse, STM32Cube.AI/ST Edge AI, NXP eIQ, Arm Vela, microTVM (8절).
- **런타임 고르는 법** — 기준표와 Hark 같은 웨어러블 시나리오 (9절).

> 주의: 이 분야 도구들은 이름·API·지원 범위가 1년 단위로 바뀐다. 이 노트에서 직접 돌린 것(torch 2.8, coremltools 9.0, Apple M2, macOS 26.4)은 "실측"이라고 쓰고, 돌리지 못한 것은 "문서 기준 / 확인 필요"로 표시한다. 면접에서도 이 구분을 그대로 말하는 게 신뢰를 준다.

---

## 1. 교환 포맷 — 모델을 넘겨주는 파일

### 1.1 직관: 모델 = Python 코드 + 가중치, 기기에는 Python이 없다

학습이 끝난 PyTorch 모델은 `forward()`라는 **Python 함수**와 가중치 텐서 묶음이다. 기기에는 Python 인터프리터가 없으니, 누군가 "이 함수가 실제로 어떤 연산을 어떤 순서로 하는지"를 **그래프로 떠내야** 한다. 이걸 export(또는 capture, tracing)라고 한다. C6에서 본 것처럼 결과는 "op 노드 + tensor 엣지"의 dataflow graph다.

### 1.2 포맷 비교

| 포맷 | 만드는 쪽 | 담는 것 | 주로 가는 곳 | 메모 |
|---|---|---|---|---|
| ExportedProgram (`torch.export`) | PyTorch 2.x | ATen op 그래프 + 파라미터 + 입력 signature | ExecuTorch, Core ML(coremltools), TVM Relax, ONNX(dynamo exporter) | 파일로는 `.pt2`. PyTorch 쪽 "공식" 출발점 |
| ONNX (`.onnx`) | PyTorch·TF·sklearn 등 | 표준 op set(opset 버전) + protobuf | ORT, TensorRT, 벤더 SDK, TVM | F5. 벤더 간 공통어 |
| TFLite flatbuffer (`.tflite`) | TF/Keras, (PyTorch는 변환 도구 경유) | TFLite op + 양자화 파라미터 | LiteRT, TFLM, Vela, 벤더 delegate | F1·F2. MCU 생태계의 표준 |
| StableHLO (MLIR) | JAX, TF, PyTorch/XLA | 버전 호환이 보장되는 HLO op set | XLA, IREE | OpenXLA 프로젝트 |
| `.mlpackage` | coremltools | MIL 프로그램 + weight.bin | Core ML (Apple 기기) | 4절 |
| `.pte` | ExecuTorch | 실행 계획 + delegate blob(flatbuffer) | ExecuTorch runtime | 5절 |
| GGUF | llama.cpp 도구 | LLM 가중치 + 메타데이터 | llama.cpp | F3 |

말로 하면: **왼쪽 셋(ExportedProgram, ONNX, TFLite)이 "교환용"**, 아래 셋(mlpackage, pte, GGUF)은 **특정 런타임 전용 실행 파일**에 가깝다. 펌웨어로 치면 앞의 것은 ELF, 뒤의 것은 특정 부트로더가 읽는 이미지 포맷이다.

### 1.3 코드로 확인 — `torch.export`가 만든 그래프 열어 보기

작은 KWS(keyword spotting) 풍 CNN을 `torch.export.export`로 떠내고, 그래프에 무엇이 들어 있는지 본다. ExecuTorch, coremltools, TVM 모두 이 객체에서 출발하므로 이게 "실제로 돌아가는 첫 단계"다.

```python
import torch, torch.nn as nn
torch.manual_seed(0)
class TinyKWS(nn.Module):              # 작은 keyword-spotting 풍 CNN
    def __init__(self):
        super().__init__()
        self.conv = nn.Conv2d(1, 4, 3, padding=1)
        self.bn = nn.BatchNorm2d(4)
        self.fc = nn.Linear(4, 3)
    def forward(self, x):
        y = torch.relu(self.bn(self.conv(x)))
        y = y.mean(dim=(2, 3))           # global average pool
        return self.fc(y)
m = TinyKWS().eval()
x = torch.randn(1, 1, 8, 8)
ep = torch.export.export(m, (x,))
print(type(ep).__name__)
for n in ep.graph.nodes:
    if n.op == "call_function":
        print(f"{n.name:<16} {str(n.target):<36} {tuple(n.meta['val'].shape)}")
from collections import Counter
print("inputs by kind:", dict(Counter(s.kind.name for s in ep.graph_signature.input_specs)))
print("max diff vs eager:", (ep.module()(x) - m(x)).abs().max().item())
```

```text
ExportedProgram
conv2d           aten.conv2d.default                  (1, 4, 8, 8)
batch_norm       aten.batch_norm.default              (1, 4, 8, 8)
relu             aten.relu.default                    (1, 4, 8, 8)
mean             aten.mean.dim                        (1, 4)
linear           aten.linear.default                  (1, 3)
inputs by kind: {'PARAMETER': 6, 'BUFFER': 3, 'USER_INPUT': 1}
max diff vs eager: 0.0
```

출력에서 볼 것: (1) Python `forward`가 ATen op 5개짜리 직선 그래프가 됐고, 각 노드에 **출력 shape이 정적으로** 붙어 있다 (NPU 컴파일러가 가장 좋아하는 정보). (2) 그래프의 "입력"은 사용자 입력 1개만이 아니라 파라미터 6개(conv/bn/fc의 weight·bias)와 버퍼 3개(BN의 running_mean·running_var·num_batches_tracked)까지 10개다 — 가중치도 그래프 입력으로 명시된다. (3) eager와 결과가 비트 단위로 같다.

### 1.4 임베디드 연결

- ExportedProgram은 **Python 제어 흐름을 다 펴 버린** 결과다. `if`·`for`가 입력값에 따라 달라지면 export가 실패하거나(그래프 break) 특정 경로로 고정된다. 펌웨어로 치면 "런타임 분기가 없는 straight-line 코드"만 남는 것이다. 그래서 NPU용 모델은 처음부터 정적인 구조로 짜는 게 편하다 (C7).
- 포맷을 고르는 건 **누가 그 포맷을 받아 주느냐**의 문제다. MCU 생태계(TFLM, Vela, CMSIS-NN, Cube.AI)는 아직 `.tflite`가 중심이고, PyTorch 중심 팀은 ExportedProgram → ExecuTorch, 벤더 SDK는 ONNX를 많이 받는다.

### 1.5 함정

- `torch.export`는 `torch.jit.trace`/`torch.jit.script`(TorchScript)와 다르다. TorchScript는 유지보수 모드이고 새 도구들은 `torch.export`를 받는다. 다만 coremltools는 아직 둘 다 받는다 (4절에서 둘 다 해 본다).
- 위 그래프에는 아직 `batch_norm`이 따로 있다. "BN이 conv에 접혔다"고 가정하면 안 된다. 접는 건 **다음 단계(컴파일러)**의 일이다 — 4절에서 Core ML 변환 후 BN이 사라지는 걸 확인한다.

---

## 2. Interpreter vs Compiler — ML 컴파일러는 무엇을 더 하나

### 2.1 직관

런타임은 크게 두 부류다.

- **Interpreter형**: 모델 파일을 런타임에 읽고, op 리스트를 순서대로 돌면서 op마다 커널 함수를 호출한다. TFLite/TFLM, ONNX Runtime의 기본 경로가 이렇다. 장점은 모델만 바꿔 끼우면 된다는 것(OTA로 `.tflite`만 교체), 단점은 op 경계마다 dispatch 비용과 중간 tensor 메모리 왕복이 생긴다는 것.
- **Compiler형**: 빌드 타임(AOT, ahead-of-time)에 그래프 전체를 보고 op를 합치고(fusion), 메모리에 맞게 쪼개고(tiling), 그 모델 전용 코드나 command stream을 생성한다. TVM, IREE, XLA, Arm Vela, QNN context binary, Edge Impulse EON, ST Edge AI가 이쪽이다.

```svg
<svg viewBox="0 0 680 255" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f6d" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs><rect x="10" y="40" width="150" height="46" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="85.0" y="59.5" font-size="12" text-anchor="middle">모델 파일</text><text x="85.0" y="74.5" font-size="12" text-anchor="middle">(.tflite / .onnx)</text><rect x="190" y="40" width="170" height="46" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="275.0" y="59.5" font-size="12" text-anchor="middle">interpreter 루프</text><text x="275.0" y="74.5" font-size="12" text-anchor="middle">op마다 dispatch</text><rect x="390" y="40" width="130" height="46" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="455.0" y="59.5" font-size="12" text-anchor="middle">커널 라이브러리</text><text x="455.0" y="74.5" font-size="12" text-anchor="middle">conv · add · relu …</text><rect x="550" y="40" width="120" height="46" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="610.0" y="59.5" font-size="12" text-anchor="middle">중간 tensor</text><text x="610.0" y="74.5" font-size="12" text-anchor="middle">매번 메모리 왕복</text><rect x="10" y="170" width="150" height="46" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="85.0" y="197.0" font-size="12" text-anchor="middle">모델 그래프</text><rect x="190" y="170" width="170" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="275.0" y="189.5" font-size="12" text-anchor="middle">컴파일러 (AOT)</text><text x="275.0" y="204.5" font-size="12" text-anchor="middle">fusion · tiling · codegen</text><rect x="390" y="170" width="130" height="46" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="455.0" y="189.5" font-size="12" text-anchor="middle">모델 전용 코드</text><text x="455.0" y="204.5" font-size="12" text-anchor="middle">conv+bn+relu 1개</text><rect x="550" y="170" width="120" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="1.5"/><text x="610.0" y="189.5" font-size="12" text-anchor="middle">기기에서 실행만</text><text x="610.0" y="204.5" font-size="12" text-anchor="middle">dispatch 거의 없음</text><line x1="160.0" y1="63.0" x2="190.0" y2="63.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6d)"/><line x1="360.0" y1="63.0" x2="390.0" y2="63.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6d)"/><line x1="520.0" y1="63.0" x2="550.0" y2="63.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6d)"/><line x1="160.0" y1="193.0" x2="190.0" y2="193.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6d)"/><line x1="360.0" y1="193.0" x2="390.0" y2="193.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6d)"/><line x1="520.0" y1="193.0" x2="550.0" y2="193.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6d)"/><text x="10" y="25" font-size="13" font-weight="bold">Interpreter (TFLite·TFLM·ORT 기본 경로)</text><text x="10" y="155" font-size="13" font-weight="bold">Compiler (TVM·IREE·XLA·Vela·QNN context·EON)</text><path d="M455,86 C455,115 275,115 275,86" fill="none" stroke="currentColor" stroke-opacity="0.6" marker-end="url(#f6d)"/><text x="365" y="122" font-size="12" text-anchor="middle">다음 op로 (N번 반복)</text><text x="10" y="245" font-size="12">비유: interpreter = 런타임에 명령 테이블을 따라가는 스크립트 엔진 · compiler = 미리 -O2로 빌드한 펌웨어 이미지</text>
</svg>
```

그림 2 — interpreter는 런타임에 op 테이블을 따라가고, compiler는 빌드 타임에 모델 전용 코드를 만든다. 실제 도구는 섞여 있다: TFLite도 내부에서 일부 fusion을 하고, Core ML은 변환 시 그래프를 최적화한 뒤 기기에서 한 번 더 컴파일(`.mlmodelc`)한다.

### 2.2 컴파일러가 interpreter보다 더 하는 일

| 일 | interpreter | compiler | 효과 |
|---|---|---|---|
| op fusion (conv+BN+ReLU) | 미리 정해진 패턴 몇 개만 | 그래프 전체에서 패턴 탐색, 새 fused 커널 생성 | 중간 tensor 메모리 왕복 제거 |
| tiling | 커널 라이브러리에 고정 | 타깃 캐시/SRAM 크기에 맞게 타일 크기 결정 | 캐시·TCM 적중률 |
| layout 선택 | 포맷이 정한 대로 (TFLite는 NHWC) | 전체 그래프 기준으로 transpose 최소화 | C6 7절 |
| memory planning | 런타임 arena 할당 (TFLM) | 빌드 타임에 오프셋 확정 | peak RAM 감소, 결정적 |
| 코드 크기 | 등록된 모든 커널이 링크됨 (op resolver로 줄임) | 그 모델이 쓰는 코드만 | flash 감소 |
| 모델 교체 | 파일만 바꾸면 됨 | 다시 컴파일해야 함 (보통) | OTA 정책에 영향 |

말로 하면: **interpreter는 "범용 커널 + 런타임 결정", compiler는 "모델 전용 코드 + 빌드 타임 결정"**이다. 펌웨어로 치면 interpreter는 런타임에 명령 테이블을 해석하는 스크립트 엔진이고, compiler는 `-O2 -flto`로 미리 최적화해 굳힌 펌웨어 이미지다.

### 2.3 손으로 계산 — fusion이 아끼는 메모리 트래픽

64×64 matmul 뒤에 bias add와 ReLU가 있다고 하자. 중간 결과 `tmp`는 64×64 fp32 = 16 KB다.

```
 unfused (op 3개):
   matmul : tmp에 쓰기            16 KB  (W)
   bias   : tmp 읽고 다시 쓰기     16 KB 읽기 + 16 KB 쓰기 (R+W)
   relu   : tmp 읽고 C에 쓰기      16 KB 읽기 (R)  (+ C 쓰기는 양쪽 공통)
   → tmp 관련 추가 트래픽 = 4 × 16 KB = 64 KB

 fused (op 1개, 타일 안에서 acc + bias → relu → C):
   → tmp 트래픽 0 KB
```

말로 하면: 중간 tensor 하나가 "쓰고, 읽고-쓰고, 읽는" 네 번의 메모리 왕복을 하는데, fusion은 이걸 레지스터/타일 버퍼 안에서 끝내 버린다. MCU에서 SRAM 대역폭이 병목이면(D3 roofline) 이 차이가 그대로 latency·전력 차이다.

### 2.4 코드로 확인 — C로 쓴 tiling + fusion

컴파일러가 생성하는 코드의 모양을 C로 직접 써서, interpreter식(op 3개)과 결과가 같은지 확인한다.

```c
#include <stdio.h>
#define M 64
#define K 64
#define N 64
#define T 16                       /* 타일 크기: 16x16 fp32 = 1 KB → L1/TCM에 들어간다 */
static float A[M][K], B[K][N], bias[N], C1[M][N], C2[M][N], tmp[M][N];

int main(void) {
    for (int i = 0; i < M; i++) for (int k = 0; k < K; k++) A[i][k] = (float)((i * 7 + k * 3) % 11 - 5);
    for (int k = 0; k < K; k++) for (int j = 0; j < N; j++) B[k][j] = (float)((k * 5 + j) % 9 - 4);
    for (int j = 0; j < N; j++) bias[j] = (float)(j % 5 - 2);

    /* (1) interpreter 식: op 3개, 중간 tensor tmp를 메모리에 두 번 왕복 */
    for (int i = 0; i < M; i++) for (int j = 0; j < N; j++) {
        float s = 0; for (int k = 0; k < K; k++) s += A[i][k] * B[k][j]; tmp[i][j] = s; }
    for (int i = 0; i < M; i++) for (int j = 0; j < N; j++) tmp[i][j] += bias[j];
    for (int i = 0; i < M; i++) for (int j = 0; j < N; j++) C1[i][j] = tmp[i][j] > 0 ? tmp[i][j] : 0;

    /* (2) compiler 식: tiling + fusion (matmul+bias+relu를 타일 안에서 한 번에) */
    for (int i0 = 0; i0 < M; i0 += T) for (int j0 = 0; j0 < N; j0 += T) {
        float acc[T][T] = {{0}};
        for (int k0 = 0; k0 < K; k0 += T)
            for (int i = 0; i < T; i++) for (int k = 0; k < T; k++)
                for (int j = 0; j < T; j++) acc[i][j] += A[i0 + i][k0 + k] * B[k0 + k][j0 + j];
        for (int i = 0; i < T; i++) for (int j = 0; j < T; j++) {
            float v = acc[i][j] + bias[j0 + j]; C2[i0 + i][j0 + j] = v > 0 ? v : 0; }
    }
    int diff = 0; double sum = 0;
    for (int i = 0; i < M; i++) for (int j = 0; j < N; j++) { diff += C1[i][j] != C2[i][j]; sum += C2[i][j]; }
    printf("mismatches=%d checksum=%.1f\n", diff, sum);
    printf("unfused: tmp %d KB x 4 passes (W, R+W, R) = %d KB extra traffic\n", M * N * 4 / 1024, M * N * 4 * 4 / 1024);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 tile.c -o tile && ./tile
```

```text
mismatches=0 checksum=100947.0
unfused: tmp 16 KB x 4 passes (W, R+W, R) = 64 KB extra traffic
```

출력에서 볼 것: 두 방식의 결과가 완전히 같고(입력이 작은 정수라 fp32 누산 순서가 바뀌어도 정확히 표현된다), 손계산한 64 KB가 그대로 나온다. 실수 입력이면 누산 순서가 달라져 마지막 비트가 다를 수 있다 — 그래서 컴파일러 검증은 bit-exact가 아니라 허용 오차로 한다 (C8).

### 2.5 함정

- "컴파일러형이 항상 빠르다"는 틀렸다. 컴파일러가 타깃을 잘 모르면(튜닝 데이터 없음, 새 칩) 손으로 다듬은 커널 라이브러리(CMSIS-NN, XNNPACK)보다 느릴 수 있다. 그래서 TVM은 auto-tuning을 하고, 많은 런타임은 "잘 아는 부분은 라이브러리, 나머지는 생성 코드"로 섞는다.
- 컴파일러형은 **모델 교체 = 재빌드**인 경우가 많다. OTA로 모델만 바꾸고 싶다면 이게 제품 결정에 영향을 준다 (9절).

---

## 3. Lowering을 직접 보기 — `run_decompositions()`

### 3.1 정의

**lowering**은 높은 수준의 op(큰 의미 단위)를 낮은 수준의 op(작은 primitive)로 바꾸는 변환이다. `linear` → `permute + addmm`, `hardswish` → `add, clamp, mul, div`처럼. C6 8절에서 식으로 본 걸 PyTorch가 실제로 하는 걸 본다. 반대 방향(작은 op 묶음 → 큰 op)이 fusion이고, 어느 방향이 맞는지는 **타깃 하드웨어의 op 지원표**가 정한다.

PyTorch에는 약 2000개 op가 있지만, 그중 약 180개짜리 **Core ATen opset**이 "백엔드가 구현해야 할 최소 집합"으로 정의되어 있다 (PyTorch IR 문서 기준). ExecuTorch·coremltools 같은 백엔드는 이 집합을 받는 걸 기준으로 삼는다. `ExportedProgram.run_decompositions()`가 그 lowering을 수행한다.

### 3.2 코드로 확인 — 1절 모델을 Core ATen으로 내리기

```python
import torch, torch.nn as nn
torch.manual_seed(0)
class TinyKWS(nn.Module):
    def __init__(self):
        super().__init__()
        self.conv = nn.Conv2d(1, 4, 3, padding=1)
        self.bn = nn.BatchNorm2d(4)
        self.fc = nn.Linear(4, 3)
    def forward(self, x):
        y = torch.relu(self.bn(self.conv(x)))
        return self.fc(y.mean(dim=(2, 3)))

m = TinyKWS().eval(); x = torch.randn(1, 1, 8, 8)
ep = torch.export.export(m, (x,))
core = ep.run_decompositions()          # 기본 decomposition table = Core ATen opset
def ops(p):
    return [str(n.target) for n in p.graph.nodes if n.op == "call_function"]
print("before:", ops(ep))
print("after :")
for t in ops(core): print("   ", t)
print("max diff:", (core.module()(x) - m(x)).abs().max().item())
```

```text
before: ['aten.conv2d.default', 'aten.batch_norm.default', 'aten.relu.default', 'aten.mean.dim', 'aten.linear.default']
after :
    aten.convolution.default
    aten._native_batch_norm_legit_no_training.default
    <built-in function getitem>
    aten.relu.default
    aten.mean.dim
    aten.permute.default
    aten.addmm.default
max diff: 0.0
```

출력에서 볼 것: `conv2d`는 더 일반적인 `convolution`(stride·padding·dilation·transposed·groups를 전부 인자로 받는 op)으로, `batch_norm`은 "추론 전용 BN"으로 바뀌었다. `linear`는 `permute`(weight 전치) + `addmm`(bias + 행렬곱)으로 **쪼개졌다**. 이게 lowering이다. `getitem`은 BN op가 여러 값을 돌려줘서 그중 하나를 꺼내는 노드다.

### 3.3 코드로 확인 — "NPU에 LayerNorm 커널이 없다면?"

이번엔 `LayerNorm → hardswish` 모델을 두 단계로 내린다. 두 번째 단계는 "타깃에 layer_norm이 없다"고 가정하고 primitive까지 쪼개는 경우다.

```python
import torch, torch.nn as nn
from torch._decomp import get_decompositions
torch.manual_seed(0)
class M(nn.Module):
    def __init__(self):
        super().__init__(); self.ln = nn.LayerNorm(8)
    def forward(self, x):
        return nn.functional.hardswish(self.ln(x))
m = M().eval(); x = torch.randn(2, 8)
ep = torch.export.export(m, (x,))
ops = lambda p: [str(n.target).replace("aten.", "").replace(".default", "")
                 for n in p.graph.nodes if n.op == "call_function"]
print("export    :", ops(ep))
core = ep.run_decompositions()                       # Core ATen opset까지
print("core ATen :", ops(core))
# 가정: 타깃 NPU에 layer_norm 커널이 없다 → primitive로 한 번 더 lowering
low = core.run_decompositions(get_decompositions([torch.ops.aten.native_layer_norm]))
print("primitive :", ops(low))
print("max diff  :", (low.module()(x) - m(x)).abs().max().item())
```

```text
export    : ['layer_norm', 'hardswish']
core ATen : ['native_layer_norm', '<built-in function getitem>', 'add.Tensor', 'clamp', 'clamp', 'mul.Tensor', 'div.Tensor']
primitive : ['var_mean.dim', '<built-in function getitem>', '<built-in function getitem>', 'add.Tensor', 'rsqrt', 'sub.Tensor', 'mul.Tensor', 'mul.Tensor', 'add.Tensor', 'add.Tensor', 'clamp', 'clamp', 'mul.Tensor', 'div.Tensor']
max diff  : 5.960464477539063e-08
```

출력에서 볼 것:

- `hardswish`는 Core ATen에 없어서 기본 단계에서 바로 `add, clamp, clamp, mul, div`로 쪼개졌다. 식으로는 `hswish(x) = x · clamp(x + 3, 0, 6) / 6`이다 (clamp 두 개 = min·max).
- `native_layer_norm`은 Core ATen에 **있어서** 기본 단계에서는 살아남는다. 두 번째 단계에서 우리가 강제로 쪼개니 `var_mean → +ε → rsqrt → (x − μ) → ·rsqrt → ·γ → +β` 순서의 primitive 7개가 나왔다. C6 8.2절의 LayerNorm 식과 정확히 같은 모양이다.
- 차이 5.96e-08은 fp32 반올림 수준(약 2^-24)이다. lowering은 "같은 수학"이지만 연산 순서가 바뀌어 마지막 비트가 다를 수 있다.

### 3.4 임베디드 연결

- NPU 컴파일러의 "unsupported op" 리포트(E5 6절)는 결국 "lowering 후에도 내 op 지원표에 없는 노드"의 목록이다. 이걸 줄이는 방법은 (1) 모델에서 그 op를 다른 것으로 바꾸거나(C7), (2) 컴파일러가 더 쪼개게 하거나(위 3.3), (3) 그 부분을 CPU로 fallback하는 것이다.
- 쪼갤수록 op 수가 늘어 dispatch·메모리 왕복이 는다. LayerNorm 1개가 primitive 7개가 되면, 그 7개를 다시 하나로 fuse할 수 있는 컴파일러가 아니면 느려진다. lowering과 fusion은 짝이다.

---

## 4. Core ML 실습 — Apple Neural Engine까지

### 4.1 Core ML이란

**Core ML**은 Apple 기기(iPhone, iPad, Mac, Watch, Vision Pro)에서 모델을 돌리는 Apple의 런타임이다. 핵심 개념만 정리한다.

| 개념 | 뜻 |
|---|---|
| coremltools | Python 패키지. PyTorch/TF 모델을 Core ML 포맷으로 변환하고, 압축하고, Mac에서 예측도 돌린다 |
| `.mlmodel` | 예전 포맷("neuralnetwork"). 단일 protobuf 파일 |
| `.mlpackage` | 현재 포맷("ML Program", iOS 15·macOS 12부터). 디렉터리 안에 MIL 프로그램(`model.mlmodel`)과 가중치(`weights/weight.bin`)가 분리되어 있다 |
| MIL | Model Intermediate Language. coremltools 내부 IR이자 ML Program의 표현. op 단위 SSA 그래프 |
| `.mlmodelc` | 기기에서 쓰는 **컴파일된** 모델. 앱 빌드 시 Xcode가 만들거나, 런타임에 기기에서 컴파일된다 |
| compute units | 어느 엔진 사용을 **허락**할지: `CPU_ONLY`, `CPU_AND_GPU`, `CPU_AND_NE`, `ALL` |
| Neural Engine (ANE) | Apple SoC의 NPU. fp16 위주로 동작한다 (공개 문서 기준) |

중요한 점: compute units는 **"이 엔진들을 써도 된다"는 허가**이지 "이 엔진에서 돌려라"는 명령이 아니다. Core ML이 op마다 실제 배치를 정한다. 그래서 4.5절의 "정말 ANE에서 돌았나?"라는 질문이 생긴다.

### 4.2 모델 준비 — 랜덤 가중치의 함정

이 Mac에는 사전학습 가중치가 없고 다운로드도 하지 않는다. 그래서 `torchvision.models.mobilenet_v2(weights=None)`(랜덤 초기화)를 쓴다. 그런데 처음 그대로 돌려 보니 출력 logits의 최대 절댓값이 **3.4e-09**였다. 52층을 지나면서 activation이 사실상 0으로 사라진 것이다(BN의 running stats가 초기값 mean=0, var=1이라 실제 분포와 안 맞음). 이 상태로 "PyTorch와 Core ML 출력 차이"를 재면 차이도 0에 가깝게 나와서 **검증이 아무 의미가 없다**.

그래서 랜덤 입력으로 BN running stats만 재보정한 모델을 공용으로 쓴다. 학습이 아니라 "BN 통계만 현실적으로 맞추는" 작업이다.

```python
import torch, torchvision
def make_model():
    torch.manual_seed(0)
    m = torchvision.models.mobilenet_v2(weights=None)
    m.train()                                  # BN running stats를 랜덤 입력으로 재보정
    with torch.no_grad():
        for _ in range(20): m(torch.randn(8, 3, 224, 224))
    return m.eval()
def inputs(n=16):
    g = torch.Generator().manual_seed(1)
    return torch.randn(n, 1, 3, 224, 224, generator=g)
```

이 파일을 `common.py`로 저장해 두고 아래 예제들이 import한다. 재보정 후 logits 최대 절댓값은 0.41, 표준편차 0.12로 정상 범위가 됐다.

> 교훈(C8과 연결): **검증 대상의 출력이 0 근처면 "차이가 작다"는 아무것도 증명하지 않는다.** 상대 오차, SNR, cosine처럼 신호 크기로 정규화한 지표를 쓰고, 기준 출력의 크기부터 찍어 보는 습관이 필요하다.

### 4.3 변환 — `ct.convert`

TorchScript trace로 변환한다. fp16 계산(ML Program 기본값)과 fp32 계산 두 가지 패키지를 만든다.

```python
import torch, coremltools as ct, subprocess
from collections import Counter
from common import make_model
m = make_model(); x = torch.randn(1, 3, 224, 224)
traced = torch.jit.trace(m, x)
for prec, name in [(ct.precision.FLOAT16, "mnv2_fp16"), (ct.precision.FLOAT32, "mnv2_fp32")]:
    ml = ct.convert(traced,
                    inputs=[ct.TensorType(name="image", shape=x.shape)],
                    outputs=[ct.TensorType(name="logits")],
                    convert_to="mlprogram",             # → .mlpackage
                    compute_precision=prec,             # 기본값은 FLOAT16
                    minimum_deployment_target=ct.target.macOS14)
    ml.save(f"{name}.mlpackage")
    kb = int(subprocess.check_output(["du", "-sk", f"{name}.mlpackage"]).split()[0])
    print(f"{name}.mlpackage  {kb} KB")
print("torch params:", sum(p.numel() for p in m.parameters()))
f = ml.get_spec().mlProgram.functions["main"]
ops = [o.type for o in f.block_specializations[f.opset].operations if o.type != "const"]
print("non-const ops:", len(ops), Counter(ops).most_common(6))
```

```text
mnv2_fp16.mlpackage  6908 KB
mnv2_fp32.mlpackage  13716 KB
torch params: 3504872
non-const ops: 100 [('conv', 52), ('clip', 35), ('add', 10), ('reduce_mean', 1), ('reshape', 1), ('linear', 1)]
```

import와 변환 중에 나온 경고(진행 막대는 생략)도 정직하게 적는다:

```text
scikit-learn version 1.6.1 is not supported. Minimum required version: 0.17. Maximum required version: 1.5.1. Disabling scikit-learn conversion API.
Torch version 2.8.0 has not been tested with coremltools. You may run into unexpected errors. Torch 2.7.0 is the most recent version that has been tested.
```

출력에서 볼 것:

- **크기 손계산**: 파라미터 3,504,872개 × 2 B(fp16) = 7,009,744 B ≈ 6,845 KB. 실측 6,908 KB와 거의 같다(나머지는 MIL 프로그램과 정렬 패딩). fp32는 두 배인 13,716 KB.
- **BN이 사라졌다**: MobileNetV2에는 conv 52개와 BN 52개가 있는데, 변환된 프로그램에는 `conv` 52개만 있고 `batch_norm`이 없다. coremltools의 graph pass가 BN을 conv weight·bias에 접었다(fusion, C6 4절). ReLU6는 `clip`(0~6)이 됐다.
- **경고**: coremltools 9.0은 torch 2.7까지만 테스트됐다고 경고한다. 이번 모델은 변환·수치 모두 문제없었지만, 제품에서는 **coremltools가 테스트한 torch 버전에 맞추는 것**이 정석이다. 펌웨어로 치면 "벤더 SDK release note의 검증된 컴파일러 버전"을 지키는 것과 같다.

ExportedProgram에서 바로 변환하는 길도 있다 (coremltools 8부터 지원, 문서 기준). 실제로 해 보면:

```python
import torch, torchvision, coremltools as ct, warnings
torch.manual_seed(0)
m = torchvision.models.mobilenet_v2(weights=None).eval()
x = torch.randn(1, 3, 224, 224)
ep = torch.export.export(m, (x,)).run_decompositions({})
with warnings.catch_warnings(record=True) as w:
    warnings.simplefilter("always")
    ml = ct.convert(ep, minimum_deployment_target=ct.target.macOS14)
print("converted from ExportedProgram:", type(ml).__name__)
print("inputs :", [i.name for i in ml.get_spec().description.input])
print("warnings:", len(w), [str(x.message)[:90] for x in w][:3])
```

```text
converted from ExportedProgram: MLModel
inputs : ['x']
warnings: 206 ['Function _TORCH_OPS_REGISTRY.__contains__ is deprecated and will be removed in 7.2.; Pleas', 'Function _TORCH_OPS_REGISTRY.__contains__ is deprecated and will be removed in 7.2.; Pleas', 'Function _TORCH_OPS_REGISTRY.__contains__ is deprecated and will be removed in 7.2.; Pleas']
```

출력에서 볼 것: ExportedProgram 경로도 동작한다. 입력 이름은 `forward`의 인자 이름(`x`)을 그대로 따른다. 다만 coremltools 내부의 deprecated API 경고가 206번 찍혔다 — 기능 문제는 아니지만 "도구 자체가 과도기"라는 신호다. (`run_decompositions({})`는 빈 테이블로 호출해 op를 쪼개지 않고 inference용 functional IR로만 정리하는 관용구로, coremltools 문서 예제가 이렇게 쓴다.)

> 이 노트의 나머지 Core ML 예제는 trace로 만든 `mnv2_fp16`·`mnv2_fp32` 패키지를 쓴다. 이 예제의 MobileNetV2는 BN 재보정을 하지 않은 변환 확인용이다.

### 4.4 compute unit별 latency와 정밀도

같은 패키지를 compute unit만 바꿔 열고, 16개 입력으로 PyTorch fp32 대비 오차를, 1개 입력으로 100회 latency median을 잰다.

```python
import torch, coremltools as ct, numpy as np, time
from common import make_model, inputs
m = make_model(); X = inputs(16)
with torch.no_grad(): ref = np.stack([m(x).numpy() for x in X])
def bench(f, n=100, warm=10):
    t = []
    for i in range(n + warm):
        s = time.perf_counter(); f(); t.append(time.perf_counter() - s)
    return np.median(t[warm:]) * 1e3
with torch.no_grad(): print(f"{'torch eager':<12} {'':<12} median {bench(lambda: m(X[0]), 50):6.2f} ms")
CU = ct.ComputeUnit
for pkg in ["mnv2_fp32", "mnv2_fp16"]:
    for cu in [CU.CPU_ONLY, CU.CPU_AND_GPU, CU.CPU_AND_NE, CU.ALL]:
        s = time.perf_counter()
        ml = ct.models.MLModel(f"{pkg}.mlpackage", compute_units=cu)
        load = time.perf_counter() - s
        out = np.stack([ml.predict({"image": x.numpy()})["logits"] for x in X])
        rel = np.abs(out - ref).max() / np.abs(ref).max()
        top1 = (out.argmax(-1) == ref.argmax(-1)).mean()
        ms = bench(lambda: ml.predict({"image": X[0].numpy()}))
        print(f"{pkg:<12} {cu.name:<12} median {ms:6.2f} ms  load {load:4.2f}s  rel.err {rel:.1e}  top1 {top1:.2f}")
```

```text
torch eager               median  26.17 ms
mnv2_fp32    CPU_ONLY     median   5.35 ms  load 0.18s  rel.err 1.2e-06  top1 1.00
mnv2_fp32    CPU_AND_GPU  median   2.34 ms  load 0.21s  rel.err 2.2e-07  top1 1.00
mnv2_fp32    CPU_AND_NE   median   5.68 ms  load 0.17s  rel.err 1.2e-06  top1 1.00
mnv2_fp32    ALL          median   2.29 ms  load 0.21s  rel.err 2.2e-07  top1 1.00
mnv2_fp16    CPU_ONLY     median   3.08 ms  load 0.13s  rel.err 6.8e-03  top1 1.00
mnv2_fp16    CPU_AND_GPU  median   3.51 ms  load 0.15s  rel.err 5.3e-04  top1 1.00
mnv2_fp16    CPU_AND_NE   median   0.54 ms  load 0.43s  rel.err 1.9e-03  top1 1.00
mnv2_fp16    ALL          median   0.54 ms  load 0.39s  rel.err 1.9e-03  top1 1.00
```

```svg
<svg viewBox="0 0 640 310" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="250.0" x2="620" y2="250.0" stroke="currentColor" stroke-opacity="0.15"/><text x="62" y="254.0" font-size="12" text-anchor="end">0</text><line x1="70" y1="215.0" x2="620" y2="215.0" stroke="currentColor" stroke-opacity="0.15"/><text x="62" y="219.0" font-size="12" text-anchor="end">1</text><line x1="70" y1="180.0" x2="620" y2="180.0" stroke="currentColor" stroke-opacity="0.15"/><text x="62" y="184.0" font-size="12" text-anchor="end">2</text><line x1="70" y1="145.0" x2="620" y2="145.0" stroke="currentColor" stroke-opacity="0.15"/><text x="62" y="149.0" font-size="12" text-anchor="end">3</text><line x1="70" y1="110.0" x2="620" y2="110.0" stroke="currentColor" stroke-opacity="0.15"/><text x="62" y="114.0" font-size="12" text-anchor="end">4</text><line x1="70" y1="75.0" x2="620" y2="75.0" stroke="currentColor" stroke-opacity="0.15"/><text x="62" y="79.0" font-size="12" text-anchor="end">5</text><line x1="70" y1="40.0" x2="620" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/><text x="62" y="44.0" font-size="12" text-anchor="end">6</text><line x1="70" y1="250" x2="620" y2="250" stroke="currentColor"/><line x1="70" y1="250" x2="70" y2="40" stroke="currentColor"/><text x="20" y="145" font-size="12" text-anchor="middle" transform="rotate(-90 20 145)">median latency (ms)</text><rect x="94.8" y="62.8" width="42" height="187.2" fill="#4a7bd0" fill-opacity="0.8"/><text x="115.8" y="57.8" font-size="12" text-anchor="middle">5.35</text><rect x="140.8" y="142.2" width="42" height="107.8" fill="#e08a3c" fill-opacity="0.8"/><text x="161.8" y="137.2" font-size="12" text-anchor="middle">3.08</text><text x="138.8" y="268" font-size="12" text-anchor="middle">CPU_ONLY</text><rect x="232.2" y="168.1" width="42" height="81.9" fill="#4a7bd0" fill-opacity="0.8"/><text x="253.2" y="163.1" font-size="12" text-anchor="middle">2.34</text><rect x="278.2" y="127.2" width="42" height="122.8" fill="#e08a3c" fill-opacity="0.8"/><text x="299.2" y="122.2" font-size="12" text-anchor="middle">3.51</text><text x="276.2" y="268" font-size="12" text-anchor="middle">CPU_AND_GPU</text><rect x="369.8" y="51.2" width="42" height="198.8" fill="#4a7bd0" fill-opacity="0.8"/><text x="390.8" y="46.2" font-size="12" text-anchor="middle">5.68</text><rect x="415.8" y="231.1" width="42" height="18.9" fill="#e08a3c" fill-opacity="0.8"/><text x="436.8" y="226.1" font-size="12" text-anchor="middle">0.54</text><text x="413.8" y="268" font-size="12" text-anchor="middle">CPU_AND_NE</text><rect x="507.2" y="169.8" width="42" height="80.2" fill="#4a7bd0" fill-opacity="0.8"/><text x="528.2" y="164.8" font-size="12" text-anchor="middle">2.29</text><rect x="553.2" y="231.1" width="42" height="18.9" fill="#e08a3c" fill-opacity="0.8"/><text x="574.2" y="226.1" font-size="12" text-anchor="middle">0.54</text><text x="551.2" y="268" font-size="12" text-anchor="middle">ALL</text><rect x="400" y="14" width="12" height="12" fill="#4a7bd0" fill-opacity="0.8"/><text x="417" y="25" font-size="12">fp32 package</text><rect x="510" y="14" width="12" height="12" fill="#e08a3c" fill-opacity="0.8"/><text x="527" y="25" font-size="12">fp16 package</text><text x="80" y="25" font-size="12">PyTorch eager CPU: 26.17 ms (축 밖)</text><text x="340" y="290" font-size="12" text-anchor="middle">MobileNetV2 1×3×224×224 · Apple M2 · 100회 median (잡음 있음)</text>
</svg>
```

그림 3 — MobileNetV2(224×224) median latency. fp16 패키지는 Neural Engine을 허락하면 0.54 ms로 떨어지고, fp32 패키지는 Neural Engine을 허락해도 CPU와 같은 속도다.

같은 스크립트를 다른 작업이 병렬로 돌던 시간에 두 번 더 돌렸을 때의 범위도 적어 둔다 (측정 잡음의 크기를 보여 주려는 것):

| 패키지 · compute unit | 위 실행 | 추가 실행 2회 | 해석 |
|---|---|---|---|
| fp32 · CPU_ONLY | 5.35 ms | 27.26 / 21.23 ms | CPU는 다른 작업과 경쟁해서 크게 흔들린다 |
| fp32 · CPU_AND_NE | 5.68 ms | 36.65 / 17.35 ms | CPU와 같이 흔들린다 → **실제로는 CPU에서 돈다** |
| fp32 · ALL | 2.29 ms | 5.09 / 4.76 ms | GPU 경로 |
| fp16 · CPU_AND_GPU | 3.51 ms | 4.64 / 4.68 ms | GPU 경로 |
| fp16 · CPU_AND_NE | 0.54 ms | 0.94 / 0.96 ms | CPU 부하와 거의 무관 → **ANE에서 돈다고 추정** |
| torch eager CPU | 26.17 ms | 74.98 / 67.46 ms | 참고용 (Core ML CPU 경로가 훨씬 최적화됨) |

출력에서 볼 것:

1. **fp32 패키지 + CPU_AND_NE = CPU_ONLY와 같은 속도·같은 오차(1.2e-06)**. Neural Engine은 fp16 연산 장치라서 fp32로 계산하라는 모델은 ANE에 올라가지 못하고 CPU로 돈다. "NE를 허락했다 ≠ NE에서 돌았다"의 가장 깔끔한 증거다.
2. **fp16 + CPU_AND_NE가 0.54 ms**로 CPU fp32(5.35 ms)의 약 1/10, GPU(2.3 ms)의 약 1/4이다. ALL도 같은 값이라 Core ML이 ALL에서도 ANE를 골랐다고 추정된다.
3. **오차 패턴이 엔진마다 다르다**: fp32 GPU는 2.2e-07, fp16 GPU는 5.3e-04, fp16 ANE는 1.9e-03, fp16 CPU는 6.8e-03. 같은 "fp16"이라도 엔진마다 누산 정밀도·연산 순서가 달라 결과가 다르다. 따라서 **골든 비교는 배포할 엔진에서** 해야 한다.
4. **top1 1.00은 이 모델에서 의미가 없다.** 랜덤 가중치 모델은 16개 입력 전부에 같은 class를 내고, 1등과 2등의 logit 차이가 0.0034밖에 안 된다(별도 확인). top-1 일치율은 "학습된 모델 + 실제 데이터"에서만 의미가 있다 — 그래서 4.7절에서 작은 학습 모델로 정확도를 따로 잰다.
5. **load 시간**: NE를 허락하면 load가 0.4초 정도로 길다. 기기에서 ANE용으로 모델을 한 번 더 컴파일하기 때문으로 보인다(Apple은 결과를 캐시한다고 알려져 있다). 앱 시작 latency에 들어가는 비용이다.

### 4.5 fp16 오차의 크기 감 잡기

rel.err 1e-3 수준이 어디서 오는지 손으로 확인한다. fp16의 machine epsilon은 2^-10 ≈ 9.77e-4다. 즉 fp16으로 **저장만** 해도 값마다 상대 오차 최대 약 4.9e-4(반 ulp)가 생긴다.

```python
import numpy as np
rng = np.random.default_rng(0)
print("fp16 eps:", np.finfo(np.float16).eps, " fp32 eps:", np.finfo(np.float32).eps)
w = rng.standard_normal(1152).astype(np.float32)        # 3x3x128 conv 하나의 MAC 길이
x = rng.standard_normal(1152).astype(np.float32)
ref = np.dot(w.astype(np.float64), x.astype(np.float64))
w16, x16 = w.astype(np.float16), x.astype(np.float16)
acc32 = np.dot(w16.astype(np.float32), x16.astype(np.float32))   # fp16 저장, fp32 누산
acc16 = np.float16(0)
for a, b in zip(w16, x16):                                       # fp16 저장, fp16 누산
    acc16 = np.float16(acc16 + a * b)
for name, v in [("fp32 everything", np.dot(w, x)), ("fp16 in, fp32 acc", acc32), ("fp16 in, fp16 acc", acc16)]:
    print(f"{name:<18} {float(v):+.5f}  abs err {abs(float(v) - ref):.1e}")
```

```text
fp16 eps: 0.000977  fp32 eps: 1.1920929e-07
fp32 everything    -28.47900  abs err 3.0e-06
fp16 in, fp32 acc  -28.47460  abs err 4.4e-03
fp16 in, fp16 acc  -28.48438  abs err 5.4e-03
```

출력에서 볼 것: 내적 하나(1152 MAC)에서도 fp16 저장만으로 절대 오차 4.4e-3(상대 약 1.5e-4)이 생기고, 누산까지 fp16이면 더 커진다. 52층을 지나면 이게 쌓여 출력 상대 오차 1e-3 수준이 되는 건 자연스럽다. **분류 모델에는 대개 무해하고, 회귀·누적 계산(예: 오디오 필터 상태, 큰 dynamic range의 LLM activation)에는 문제가 될 수 있다.** 이럴 땐 그 부분만 fp32로 두는(`compute_precision`에 op 선택 함수를 넘기는) 혼합 정밀도가 있다 (coremltools 문서의 `ct.transform.FP16ComputePrecision(op_selector=...)`).

### 4.6 "정말 Neural Engine에서 돌았나?" — 확인하는 방법

4.4의 latency는 **추론**이다. 직접 확인하는 방법은 세 단계가 있다.

| 방법 | 무엇을 알려 주나 | 확실성 |
|---|---|---|
| latency 비교 (4.4) | CPU_ONLY 대비 NE 허락 시 급감 + CPU 부하와 무관 → ANE 추정 | 간접 증거 |
| `MLComputePlan` API (아래) | op마다 Core ML이 **계획한** 선호 디바이스와 지원 디바이스 | 계획(plan) — 실행 trace는 아님 |
| Xcode Core ML 성능 리포트 / Instruments의 Core ML 템플릿 | 실제 실행 시 어느 엔진에서 얼마나 돌았는지 타임라인 | 가장 확실 (이 노트에서는 GUI라 실행 안 함) |

`MLComputePlan`은 macOS 14.4·iOS 17.4부터 있는 API(Apple 문서 기준)이고 coremltools 9에서 Python으로 부를 수 있다. 실제로 돌려 본다.

```python
import coremltools as ct
from collections import Counter
from coremltools.models.compute_plan import MLComputePlan
for pkg in ["mnv2_fp16", "mnv2_fp32"]:
    ml = ct.models.MLModel(f"{pkg}.mlpackage", compute_units=ct.ComputeUnit.ALL)
    plan = MLComputePlan.load_from_path(ml.get_compiled_model_path(), compute_units=ct.ComputeUnit.ALL)
    ops = plan.model_structure.program.functions["main"].block.operations
    pref, cost = Counter(), Counter()
    for op in ops:
        u = plan.get_compute_device_usage_for_mlprogram_operation(op)
        if u is None: continue                       # const 등은 디바이스 배정 없음
        dev = type(u.preferred_compute_device).__name__.replace("ML", "").replace("ComputeDevice", "")
        pref[dev] += 1
        c = plan.get_estimated_cost_for_mlprogram_operation(op)
        cost[dev] += c.weight if c else 0
    print(pkg, "ops per preferred device:", dict(pref), " cost share:", {k: round(v, 3) for k, v in cost.items()})
    sup = Counter(tuple(sorted(type(d).__name__.replace("ML", "").replace("ComputeDevice", "")
                   for d in plan.get_compute_device_usage_for_mlprogram_operation(op).supported_compute_devices))
                   for op in ops if plan.get_compute_device_usage_for_mlprogram_operation(op))
    print("   supported sets:", dict(sup))
```

```text
mnv2_fp16 ops per preferred device: {'NeuralEngine': 100}  cost share: {'NeuralEngine': 1.0}
   supported sets: {('CPU', 'GPU', 'NeuralEngine'): 100}
mnv2_fp32 ops per preferred device: {'GPU': 100}  cost share: {'GPU': 1.0}
   supported sets: {('CPU', 'GPU'): 100}
```

출력에서 볼 것: fp16 패키지는 100개 op 전부가 Neural Engine을 선호 디바이스로 배정받았고, fp32 패키지는 **지원 디바이스 목록에 Neural Engine이 아예 없다**. 4.4의 latency 추론(fp32 + NE = CPU 속도)과 정확히 맞는다. 다만 이건 "계획"이다. 실제 실행 중 ANE가 바쁘거나 메모리 사정으로 다른 엔진이 쓰였는지까지는 Instruments 타임라인으로만 확실히 볼 수 있다. 면접에서는 이 세 단계를 순서대로 말하면 된다.

펌웨어 비유: compute plan은 **링커 map 파일**(어느 함수가 ITCM에 배치됐나), Instruments는 **실제 trace(ETM/로직 분석기)**다. map 파일이 TCM이라고 해도 실제로 그 경로가 실행됐는지는 trace로 확인한다.

### 4.7 weight 압축 — `coremltools.optimize`

coremltools 9의 `coremltools.optimize.coreml` 모듈에는 다음 함수들이 있다 (직접 `dir()`로 확인): `linear_quantize_weights`, `palettize_weights`, `prune_weights`, `linear_quantize_activations`, 그리고 설정 클래스 `OpLinearQuantizerConfig`, `OpPalettizerConfig`, `OptimizationConfig` 등. 변환이 끝난 mlpackage에 **학습 데이터 없이** 적용하는 post-training 압축이다.

- **linear quantization**: 채널별 scale로 int8 등에 저장 (C1의 대칭 양자화와 같다). `w ≈ scale · q`.
- **palettization**: weight를 k-means로 2^n개 대표값(LUT, palette)에 모으고, 각 weight는 n-bit 인덱스만 저장. `w ≈ LUT[idx]`. 4-bit면 16개 값만 쓴다.

손계산: 파라미터 3.5M개를 4-bit 인덱스로 저장하면 3.5M × 0.5 B = 1.75 MB + LUT(층마다 16개 × 2 B, 무시 가능). fp16 6.7 MB 대비 약 1/4이다.

먼저 MobileNetV2에 적용해 크기, 출력 SNR, ANE latency를 본다. SNR(dB) = 10·log10(∑ref² / ∑(out − ref)²)이다. 말로 하면 "신호 에너지가 오차 에너지의 몇 배인가"를 로그로 잰 것이고, 20 dB = 오차 에너지가 1/100, 40 dB = 1/10000이다.

```python
import torch, coremltools as ct, numpy as np, os, time
import coremltools.optimize.coreml as cto
from common import make_model, inputs
m = make_model(); X = inputs(16)
with torch.no_grad(): ref = np.stack([m(x).numpy() for x in X])
base = ct.models.MLModel("mnv2_fp16.mlpackage")
lin8 = cto.OptimizationConfig(global_config=cto.OpLinearQuantizerConfig(mode="linear_symmetric", dtype="int8"))
pal = lambda b: cto.OptimizationConfig(global_config=cto.OpPalettizerConfig(mode="kmeans", nbits=b))
variants = {"fp16": base, "w8 linear": cto.linear_quantize_weights(base, lin8),
            "w6 palette": cto.palettize_weights(base, pal(6)),
            "w4 palette": cto.palettize_weights(base, pal(4)),
            "w2 palette": cto.palettize_weights(base, pal(2))}
for name, v in variants.items():
    p = f"c_{name.replace(' ', '_')}.mlpackage"; v.save(p)
    mb = os.path.getsize(f"{p}/Data/com.apple.CoreML/weights/weight.bin") / 2**20
    v = ct.models.MLModel(p, compute_units=ct.ComputeUnit.CPU_AND_NE)
    out = np.stack([v.predict({"image": x.numpy()})["logits"] for x in X])
    snr = 10 * np.log10((ref ** 2).sum() / ((out - ref) ** 2).sum())   # 출력 SNR (dB)
    t = []
    for i in range(60):
        s = time.perf_counter(); v.predict({"image": X[0].numpy()}); t.append(time.perf_counter() - s)
    print(f"{name:<11} weights {mb:5.2f} MB  SNR {snr:5.1f} dB  NE median {np.median(t[10:])*1e3:5.2f} ms")
```

```text
fp16        weights  6.66 MB  SNR  57.4 dB  NE median  0.87 ms
w8 linear   weights  3.40 MB  SNR  38.8 dB  NE median  0.82 ms
w6 palette  weights  2.55 MB  SNR  28.5 dB  NE median  0.81 ms
w4 palette  weights  1.72 MB  SNR  16.8 dB  NE median  0.93 ms
w2 palette  weights  0.89 MB  SNR   6.3 dB  NE median  0.89 ms
```

```svg
<svg viewBox="0 0 640 250" xmlns="http://www.w3.org/2000/svg">
<line x1="120.0" y1="20" x2="120.0" y2="200" stroke="currentColor" stroke-opacity="0.15"/><text x="120.0" y="216" font-size="12" text-anchor="middle">0</text><line x1="193.3" y1="20" x2="193.3" y2="200" stroke="currentColor" stroke-opacity="0.15"/><text x="193.3" y="216" font-size="12" text-anchor="middle">10</text><line x1="266.7" y1="20" x2="266.7" y2="200" stroke="currentColor" stroke-opacity="0.15"/><text x="266.7" y="216" font-size="12" text-anchor="middle">20</text><line x1="340.0" y1="20" x2="340.0" y2="200" stroke="currentColor" stroke-opacity="0.15"/><text x="340.0" y="216" font-size="12" text-anchor="middle">30</text><line x1="413.3" y1="20" x2="413.3" y2="200" stroke="currentColor" stroke-opacity="0.15"/><text x="413.3" y="216" font-size="12" text-anchor="middle">40</text><line x1="486.7" y1="20" x2="486.7" y2="200" stroke="currentColor" stroke-opacity="0.15"/><text x="486.7" y="216" font-size="12" text-anchor="middle">50</text><line x1="560.0" y1="20" x2="560.0" y2="200" stroke="currentColor" stroke-opacity="0.15"/><text x="560.0" y="216" font-size="12" text-anchor="middle">60</text><line x1="120" y1="20" x2="120" y2="200" stroke="currentColor"/><rect x="120" y="28" width="420.9" height="22" fill="#3f9a6b" fill-opacity="0.8"/><text x="112" y="43" font-size="12" text-anchor="end">fp16</text><text x="546.9" y="43" font-size="12">57.4 dB · 6.66 MB</text><rect x="120" y="62" width="284.5" height="22" fill="#3f9a6b" fill-opacity="0.8"/><text x="112" y="77" font-size="12" text-anchor="end">w8 linear</text><text x="410.5" y="77" font-size="12">38.8 dB · 3.4 MB</text><rect x="120" y="96" width="209.0" height="22" fill="#e08a3c" fill-opacity="0.8"/><text x="112" y="111" font-size="12" text-anchor="end">w6 palette</text><text x="335.0" y="111" font-size="12">28.5 dB · 2.55 MB</text><rect x="120" y="130" width="123.2" height="22" fill="#e08a3c" fill-opacity="0.8"/><text x="112" y="145" font-size="12" text-anchor="end">w4 palette</text><text x="249.2" y="145" font-size="12">16.8 dB · 1.72 MB</text><rect x="120" y="164" width="46.2" height="22" fill="#d0564a" fill-opacity="0.8"/><text x="112" y="179" font-size="12" text-anchor="end">w2 palette</text><text x="172.2" y="179" font-size="12">6.3 dB · 0.89 MB</text><text x="340" y="240" font-size="12" text-anchor="middle">MobileNetV2 출력 SNR (dB, 높을수록 PyTorch fp32와 가깝다) · weight.bin 크기</text>
</svg>
```

그림 4 — MobileNetV2 weight 압축: 비트를 줄일수록 weight.bin이 선형으로 줄고, 출력 SNR은 비트당 약 5~10 dB씩 떨어진다. (초록 ≥ 30 dB, 주황 15~30 dB, 빨강 < 15 dB — 임의로 정한 색 구분)

출력에서 볼 것:

- **크기는 손계산대로**: 4-bit 1.72 MB ≈ 1.75 MB 예상. 2-bit 0.89 MB.
- **SNR**: fp16 자체가 57 dB(4.4절의 fp16 오차), int8 39 dB, 4-bit 17 dB. 이 숫자는 "학습되지 않은 랜덤 모델"에서 잰 것이라 정확도로 바로 번역되지 않는다. 그래서 다음 예제에서 학습된 작은 모델로 정확도를 잰다.
- **latency는 거의 그대로** (0.8~0.9 ms, 첫 줄 fp16도 다른 실행에서는 2.15 ms가 나온 적이 있어 이 범위는 잡음이다). MobileNetV2는 이 크기에서 weight 대역폭이 병목이 아니고, 압축된 weight는 로드 시점이나 연산 직전에 다시 펼쳐질 수 있기 때문으로 보인다. **weight 압축의 1차 이득은 저장 크기(앱 용량·OTA 크기·DRAM 점유)**이고, latency 이득은 weight-bound 모델(LLM decode, D5)에서 주로 나온다.

이제 정확도를 볼 수 있게 작은 모델을 학습한다. "가짜 spectrogram" 4클래스(낮은 톤, 높은 톤, 상승 chirp, 하강 chirp) + 잡음이다. KWS 모델처럼 시간 축은 평균 내고 주파수 축 정보는 남기는 구조다.

```python
import torch, torch.nn as nn
torch.manual_seed(0)
def make_data(n):                          # 가짜 spectrogram 4클래스: 낮은 톤/높은 톤/상승 chirp/하강 chirp
    y = torch.randint(0, 4, (n,)); x = torch.randn(n, 1, 32, 32) * 1.2
    t = torch.arange(32)
    for i in range(n):
        f0 = int(torch.randint(4, 28, (1,)))
        rows = {0: torch.full((32,), f0 // 2), 1: torch.full((32,), 16 + f0 // 2),
                2: (t * 0.6 + f0 // 3).long(), 3: (31 - t * 0.6 - f0 // 3).long()}[int(y[i])]
        x[i, 0, rows.clamp(0, 31), t] += 1.5
    return x, y
xtr, ytr = make_data(4000); xte, yte = make_data(1000)
m = nn.Sequential(nn.Conv2d(1, 16, 3, padding=1), nn.BatchNorm2d(16), nn.ReLU(), nn.MaxPool2d(2),
                  nn.Conv2d(16, 32, 3, padding=1), nn.BatchNorm2d(32), nn.ReLU(), nn.MaxPool2d(2),
                  nn.Conv2d(32, 64, 3, padding=1), nn.ReLU(), nn.AdaptiveAvgPool2d((4, 1)),
                  nn.Flatten(), nn.Linear(256, 4))
opt = torch.optim.Adam(m.parameters(), 3e-3)
for ep in range(5):
    for i in range(0, 4000, 64):
        loss = nn.functional.cross_entropy(m(xtr[i:i+64]), ytr[i:i+64])
        opt.zero_grad(); loss.backward(); opt.step()
m.eval()
with torch.no_grad(): print("torch fp32 test acc:", (m(xte).argmax(1) == yte).float().mean().item())
torch.save({"m": m, "xte": xte, "yte": yte}, "small.pt")
```

```text
torch fp32 test acc: 0.9760000109672546
```

참고로 처음에는 마지막 pooling을 `AdaptiveAvgPool2d(1)`(전체 평균)로 했다가 정확도가 0.83에 머물렀다. 전체 평균을 내면 "톤이 위쪽에 있나 아래쪽에 있나"라는 위치 정보가 사라지기 때문이다. 주파수 축 4칸을 남기니(`(4, 1)`) 0.976이 됐다. 모델 구조가 데이터의 어떤 정보를 버리는지가 정확도를 정한다는 B2·B5의 이야기를 그대로 겪은 셈이다.

이제 Core ML로 변환하고 1~8 bit로 압축해 test set 1000개 정확도를 Neural Engine 허락 상태에서 잰다. 이 모델은 weight가 작아서 기본 설정이면 작은 텐서가 압축에서 빠지므로 `weight_threshold=0`으로 전부 압축한다.

```python
import torch, numpy as np, coremltools as ct, os
import coremltools.optimize.coreml as cto
d = torch.load("small.pt", weights_only=False); m, xte, yte = d["m"], d["xte"], d["yte"]
B = 100
ml = ct.convert(torch.jit.trace(m, xte[:B]), inputs=[ct.TensorType(name="x", shape=(B, 1, 32, 32))],
                outputs=[ct.TensorType(name="y")], minimum_deployment_target=ct.target.macOS14)
lin = lambda b: cto.OptimizationConfig(global_config=cto.OpLinearQuantizerConfig(
          mode="linear_symmetric", dtype=f"int{b}", weight_threshold=0))
pal = lambda b: cto.OptimizationConfig(global_config=cto.OpPalettizerConfig(
          mode="kmeans", nbits=b, weight_threshold=0))   # 작은 weight도 전부 압축
variants = {"fp16": ml,
            "w8 linear":  cto.linear_quantize_weights(ml, lin(8)),
            "w6 palette": cto.palettize_weights(ml, pal(6)),
            "w4 palette": cto.palettize_weights(ml, pal(4)),
            "w2 palette": cto.palettize_weights(ml, pal(2)),
            "w1 palette": cto.palettize_weights(ml, pal(1))}
with torch.no_grad(): print(f"{'torch fp32':<11} {'':>8}  acc {(m(xte).argmax(1) == yte).float().mean():.3f}")
for name, v in variants.items():
    p = f"small_{name.replace(' ', '_')}.mlpackage"; v.save(p)
    kb = os.path.getsize(f"{p}/Data/com.apple.CoreML/weights/weight.bin") / 1024
    v = ct.models.MLModel(p, compute_units=ct.ComputeUnit.CPU_AND_NE)
    pred = np.concatenate([v.predict({"x": xte[i:i+B].numpy()})["y"].argmax(1) for i in range(0, 1000, B)])
    print(f"{name:<11} {kb:6.1f} KB  acc {(pred == yte.numpy()).mean():.3f}")
```

```text
torch fp32            acc 0.976
fp16          48.1 KB  acc 0.976
w8 linear     25.2 KB  acc 0.973
w6 palette    19.2 KB  acc 0.972
w4 palette    13.1 KB  acc 0.979
w2 palette     6.7 KB  acc 0.785
w1 palette     3.8 KB  acc 0.513
```

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="250.0" x2="570" y2="250.0" stroke="currentColor" stroke-opacity="0.15"/><text x="62" y="254.0" font-size="12" text-anchor="end">0</text><line x1="70" y1="212.4" x2="570" y2="212.4" stroke="currentColor" stroke-opacity="0.15"/><text x="62" y="216.4" font-size="12" text-anchor="end">10</text><line x1="70" y1="174.8" x2="570" y2="174.8" stroke="currentColor" stroke-opacity="0.15"/><text x="62" y="178.8" font-size="12" text-anchor="end">20</text><line x1="70" y1="137.2" x2="570" y2="137.2" stroke="currentColor" stroke-opacity="0.15"/><text x="62" y="141.2" font-size="12" text-anchor="end">30</text><line x1="70" y1="99.6" x2="570" y2="99.6" stroke="currentColor" stroke-opacity="0.15"/><text x="62" y="103.6" font-size="12" text-anchor="end">40</text><line x1="70" y1="62.0" x2="570" y2="62.0" stroke="currentColor" stroke-opacity="0.15"/><text x="62" y="66.0" font-size="12" text-anchor="end">50</text><text x="578" y="254.0" font-size="12">0.5</text><text x="578" y="216.4" font-size="12">0.6</text><text x="578" y="178.8" font-size="12">0.7</text><text x="578" y="141.2" font-size="12">0.8</text><text x="578" y="103.6" font-size="12">0.9</text><text x="578" y="66.0" font-size="12">1.0</text><line x1="70" y1="250" x2="570" y2="250" stroke="currentColor"/><line x1="70" y1="250" x2="70" y2="62" stroke="currentColor"/><line x1="570" y1="250" x2="570" y2="62" stroke="#e08a3c"/><text x="20" y="156" font-size="12" text-anchor="middle" transform="rotate(-90 20 156)">weight.bin (KB)</text><text x="620" y="145" font-size="12" text-anchor="middle" transform="rotate(90 620 145)">test accuracy</text><rect x="89.7" y="69.1" width="44" height="180.9" fill="#4a7bd0" fill-opacity="0.7"/><text x="111.7" y="244.0" font-size="12" text-anchor="middle">48.1</text><text x="111.7" y="268" font-size="12" text-anchor="middle">fp16</text><rect x="173.0" y="155.2" width="44" height="94.8" fill="#4a7bd0" fill-opacity="0.7"/><text x="195.0" y="244.0" font-size="12" text-anchor="middle">25.2</text><text x="195.0" y="268" font-size="12" text-anchor="middle">w8</text><rect x="256.3" y="177.8" width="44" height="72.2" fill="#4a7bd0" fill-opacity="0.7"/><text x="278.3" y="244.0" font-size="12" text-anchor="middle">19.2</text><text x="278.3" y="268" font-size="12" text-anchor="middle">w6</text><rect x="339.7" y="200.7" width="44" height="49.3" fill="#4a7bd0" fill-opacity="0.7"/><text x="361.7" y="244.0" font-size="12" text-anchor="middle">13.1</text><text x="361.7" y="268" font-size="12" text-anchor="middle">w4</text><rect x="423.0" y="224.8" width="44" height="25.2" fill="#4a7bd0" fill-opacity="0.7"/><text x="445.0" y="244.0" font-size="12" text-anchor="middle">6.7</text><text x="445.0" y="268" font-size="12" text-anchor="middle">w2</text><rect x="506.3" y="235.7" width="44" height="14.3" fill="#4a7bd0" fill-opacity="0.7"/><text x="510" y="244.0" font-size="12" text-anchor="end">3.8</text><text x="528.3" y="268" font-size="12" text-anchor="middle">w1</text><polyline points="111.7,71.0 195.0,72.2 278.3,72.5 361.7,69.9 445.0,142.8 528.3,245.1" fill="none" stroke="#e08a3c" stroke-width="2"/><circle cx="111.7" cy="71.0" r="4" fill="#e08a3c"/><text x="111.7" y="61.0" font-size="12" text-anchor="middle">0.976</text><circle cx="195.0" cy="72.2" r="4" fill="#e08a3c"/><text x="195.0" y="62.2" font-size="12" text-anchor="middle">0.973</text><circle cx="278.3" cy="72.5" r="4" fill="#e08a3c"/><text x="278.3" y="62.5" font-size="12" text-anchor="middle">0.972</text><circle cx="361.7" cy="69.9" r="4" fill="#e08a3c"/><text x="361.7" y="59.9" font-size="12" text-anchor="middle">0.979</text><circle cx="445.0" cy="142.8" r="4" fill="#e08a3c"/><text x="445.0" y="132.8" font-size="12" text-anchor="middle">0.785</text><circle cx="528.3" cy="245.1" r="4" fill="#e08a3c"/><text x="514" y="222" font-size="12" text-anchor="end">0.513</text><rect x="380" y="6" width="12" height="12" fill="#4a7bd0" fill-opacity="0.7"/><text x="397" y="17" font-size="12">weight 크기 (왼쪽 축)</text><line x1="380" y1="30" x2="392" y2="30" stroke="#e08a3c" stroke-width="2"/><text x="397" y="34" font-size="12">정확도 (오른쪽 축)</text><text x="320" y="292" font-size="12" text-anchor="middle">작은 CNN(24.3k params, 합성 spectrogram 4클래스) — weight 비트를 줄일 때 크기와 정확도</text>
</svg>
```

그림 5 — 학습된 작은 CNN의 weight 비트 vs 크기·정확도. 4-bit까지는 정확도가 사실상 그대로(±0.5%p는 1000개 test set의 통계 잡음 수준)이고, 2-bit에서 무너진다.

출력에서 볼 것:

- **파라미터 손계산**: conv1 1·16·9+16 = 160, conv2 16·32·9+32 = 4,640, conv3 32·64·9+64 = 18,496, fc 256·4+4 = 1,028 → 합 24,324개. fp16이면 48,648 B ≈ 47.5 KB, 실측 48.1 KB(정렬 헤더 포함). BN 파라미터는 conv에 접혀서 따로 없다.
- **4-bit가 fp16보다 0.3%p 높은 건 우연**이다. 1000개에서 3개 차이(표준오차 약 √(0.976·0.024/1000) ≈ 0.5%p)라 의미 없다. "양자화하니 정확도가 올랐다"는 말은 거의 항상 잡음이다 (A2·C8).
- **2-bit(LUT 4개)에서 0.785, 1-bit에서 0.513**: palette 값이 너무 적으면 weight 분포를 표현하지 못한다. C3의 LLM 4-bit 양자화가 group-wise scale 같은 장치를 쓰는 이유다.
- 펌웨어 관점: 4-bit palettization은 "16-entry LUT + 4-bit 인덱스"다. MCU에서 직접 구현하면 nibble unpack + LUT 조회가 MAC마다 붙는다. ANE 같은 가속기는 이걸 하드웨어/드라이버가 처리한다.

### 4.8 Core ML 정리 — 제품 관점

| 질문 | 답 |
|---|---|
| 어디서 돌아가나 | Apple 기기만. Android·MCU에는 못 쓴다 |
| 왜 알아야 하나 | iPhone 동반 앱(웨어러블의 짝 앱)에서 모델을 돌릴 때, 그리고 "벤더 전용 NPU 런타임"의 전형적인 모습을 보여 주는 예라서 |
| 입력 포맷 | PyTorch(trace 또는 ExportedProgram), TF. ONNX 변환기는 coremltools 6에서 제거됐다 (문서 기준) |
| 양자화 | weight: linear(int8/int4), palettization(1~8 bit), pruning. activation 양자화도 API가 있다 (`linear_quantize_activations`) |
| 디버깅 도구 | `MLComputePlan`, Xcode 성능 리포트, Instruments |
| 모델 업데이트 | 앱 번들에 넣거나, 런타임에 다운로드 후 기기에서 컴파일 (문서 기준 `MLModel.compileModel`) |
| ExecuTorch와의 관계 | ExecuTorch에 Core ML backend가 있어서, PyTorch → ExecuTorch → (Core ML delegate) 경로도 가능 |

---

## 5. ExecuTorch — PyTorch의 온디바이스 런타임

### 5.1 무엇인가

**ExecuTorch**는 PyTorch 팀(Meta)이 만든 온디바이스 추론 런타임이다. TorchScript/PyTorch Mobile(Lite Interpreter)의 후속이다. 2025년 하반기에 1.0이 나왔다고 발표됐다(버전·날짜는 확인 필요). 목표는 "PyTorch에서 `torch.export`로 뽑은 그래프를 **같은 경로로** 폰·웨어러블·MCU까지 보내는 것"이다.

이 환경에는 설치되어 있지 않다 (설치하지 않는다):

```python
import importlib.util, torch
for mod in ["executorch", "tvm", "iree.compiler", "mediapipe", "tensorflow"]:
    try:
        ok = importlib.util.find_spec(mod) is not None
    except ModuleNotFoundError:
        ok = False
    print(f"{mod:<15} {'설치됨' if ok else '없음'}")
print("torch.export 사용 가능:", hasattr(torch, "export"), torch.__version__)
```

```text
executorch      없음
tvm             없음
iree.compiler   없음
mediapipe       없음
tensorflow      없음
torch.export 사용 가능: True 2.8.0
```

출력에서 볼 것: ExecuTorch, TVM, IREE, MediaPipe 모두 이 venv에 없다. 그래서 이 절과 7절의 해당 코드는 **"실행 안 함"** 표시를 붙인다. 공통 출발점인 `torch.export`는 있으므로 1·3절에서 실제로 돌렸다.

### 5.2 흐름

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f6b" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs><rect x="10" y="30" width="120" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="70.0" y="51.5" font-size="12" text-anchor="middle">nn.Module</text><text x="70.0" y="66.5" font-size="12" text-anchor="middle">(Python)</text><rect x="150" y="30" width="120" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="210.0" y="51.5" font-size="12" text-anchor="middle">torch.export</text><text x="210.0" y="66.5" font-size="12" text-anchor="middle">ExportedProgram</text><rect x="290" y="30" width="120" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="350.0" y="51.5" font-size="12" text-anchor="middle">to_edge</text><text x="350.0" y="66.5" font-size="12" text-anchor="middle">Edge dialect</text><rect x="430" y="30" width="120" height="50" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="490.0" y="51.5" font-size="12" text-anchor="middle">partitioner</text><text x="490.0" y="66.5" font-size="12" text-anchor="middle">backend 위임</text><rect x="570" y="30" width="100" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="1.5"/><text x="620.0" y="51.5" font-size="12" text-anchor="middle">.pte 파일</text><text x="620.0" y="66.5" font-size="12" text-anchor="middle">(flatbuffer)</text><rect x="330" y="120" width="100" height="34" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="380.0" y="141.0" font-size="12" text-anchor="middle">XNNPACK (CPU)</text><rect x="440" y="120" width="100" height="34" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="490.0" y="141.0" font-size="12" text-anchor="middle">Core ML</text><rect x="550" y="120" width="100" height="34" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="600.0" y="141.0" font-size="12" text-anchor="middle">QNN (HTP)</text><rect x="330" y="162" width="100" height="34" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="380.0" y="183.0" font-size="12" text-anchor="middle">Arm Ethos-U</text><rect x="440" y="162" width="100" height="34" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="490.0" y="183.0" font-size="12" text-anchor="middle">MediaTek 등</text><rect x="550" y="162" width="100" height="34" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="600.0" y="183.0" font-size="12" text-anchor="middle">Cadence DSP</text><rect x="150" y="240" width="380" height="60" rx="5" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="1.5"/><text x="340.0" y="266.5" font-size="12" text-anchor="middle">기기: ExecuTorch C++ runtime</text><text x="340.0" y="281.5" font-size="12" text-anchor="middle">.pte load → delegate blob은 backend로, 나머지는 portable kernel</text><line x1="130.0" y1="55.0" x2="150.0" y2="55.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6b)"/><line x1="270.0" y1="55.0" x2="290.0" y2="55.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6b)"/><line x1="410.0" y1="55.0" x2="430.0" y2="55.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6b)"/><line x1="550.0" y1="55.0" x2="570.0" y2="55.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6b)"/><line x1="490" y1="80" x2="490" y2="118" stroke="currentColor" stroke-dasharray="4 3" stroke-opacity="0.6"/><text x="160" y="135" font-size="12">AOT (개발 PC, Python)</text><text x="160" y="152" font-size="12">— 여기까지는 빌드 타임</text><line x1="10" y1="215" x2="670" y2="215" stroke="currentColor" stroke-dasharray="6 4" stroke-opacity="0.4"/><text x="20" y="232" font-size="12">런타임 (기기, C++)</text><path d="M620,80 L620,100 L668,100 L668,270 L532,270" fill="none" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6b)"/>
</svg>
```

그림 6 — ExecuTorch 흐름. 왼쪽 위(빌드 타임, Python)에서 `.pte` 파일을 만들고, 기기(C++ runtime)는 그 파일만 읽는다. partitioner가 backend가 지원하는 부분 그래프를 떼어 delegate blob으로 굳히고, 나머지는 ExecuTorch의 portable/optimized kernel로 돈다.

| 단계 | API (문서 기준) | 하는 일 | 이 노트에서 |
|---|---|---|---|
| 1 capture | `torch.export.export(model, args)` | Python → ATen 그래프 (ExportedProgram) | 1절에서 실제 실행 |
| 2 edge 변환 | `to_edge(...)` 또는 `to_edge_transform_and_lower(...)` | Core ATen으로 lowering + dtype·shape 검증 (Edge dialect) | 3절 `run_decompositions`가 같은 종류의 일 |
| 3 delegation | `partitioner=[XnnpackPartitioner()]` 등 | backend가 지원하는 부분 그래프를 떼어 backend 컴파일러에 넘김 | 실행 안 함 |
| 4 직렬화 | `.to_executorch()` → `.buffer` | memory planning 후 `.pte`(flatbuffer)로 저장 | 실행 안 함 |
| 5 실행 | C++ `Module` / `Method` API | `.pte` 로드 → 입력 넣고 `forward` | 실행 안 함 |

### 5.3 코드 (실행 안 함 — executorch 미설치, API는 버전에 따라 다름)

```python
# 실행 안 함: executorch 미설치. ExecuTorch 공식 문서의 XNNPACK 예제 모양을 따름
import torch
from executorch.exir import to_edge_transform_and_lower
from executorch.backends.xnnpack.partition.xnnpack_partitioner import XnnpackPartitioner

model = TinyKWS().eval()                      # 1절 모델
x = torch.randn(1, 1, 8, 8)
ep = torch.export.export(model, (x,))         # 1. capture  (이 줄까지는 1절에서 실제로 돌았다)
prog = to_edge_transform_and_lower(           # 2+3. Edge dialect + XNNPACK delegation
    ep, partitioner=[XnnpackPartitioner()])
et = prog.to_executorch()                     # 4. memory planning + 직렬화
with open("tiny_kws_xnnpack.pte", "wb") as f:
    f.write(et.buffer)
```

기기 쪽 C++ (실행 안 함 — extension 모듈 API 모양, 버전마다 헤더·namespace가 바뀌어 왔다):

```c
// 실행 안 함: C++ 코드. ExecuTorch extension/module API의 대략적인 모양
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor.h>
using namespace ::executorch::extension;

int run_once(float *mel /* 1x1x8x8 */) {
    static Module module("tiny_kws_xnnpack.pte");   // 파일 로드 + method 준비
    auto input = from_blob(mel, {1, 1, 8, 8});
    auto result = module.forward(input);
    if (!result.ok()) return -1;                    // Error 코드 확인 (예외 없음)
    const float *logits = result->at(0).toTensor().const_data_ptr<float>();
    return logits[1] > logits[0] ? 1 : 0;
}
```

MCU처럼 파일 시스템·malloc이 없는 곳에서는 위의 편의 API(`Module`) 대신 더 낮은 수준 API(`Program`, `Method`, `MemoryManager`)로 **미리 잡아 둔 정적 버퍼**를 넘겨 쓰는 방식이 문서에 있다. TFLM의 tensor arena(F2)와 같은 발상이다. 실제 메모리 계획은 4단계(`to_executorch`)에서 빌드 타임에 결정되어 `.pte`에 들어간다.

### 5.4 backend (delegate) 목록 — 문서 기준, 버전마다 다름

| backend | 대상 | 메모 |
|---|---|---|
| XNNPACK | ARM/x86 CPU | 기본 CPU 가속. LiteRT도 같은 라이브러리를 쓴다 |
| Core ML | Apple ANE/GPU/CPU | 4절의 Core ML을 delegate로 사용 |
| MPS | Apple GPU | Metal Performance Shaders |
| Vulkan | 모바일 GPU | Android GPU |
| Qualcomm (QNN) | Hexagon HTP/DSP | F4의 QNN을 delegate로 사용 |
| MediaTek | MediaTek APU | NeuroPilot |
| Arm Ethos-U | Cortex-M + Ethos-U55/U65/U85 | 내부에서 Vela(E5)로 컴파일. Corstone FVP 예제가 있다 |
| Cadence | Xtensa HiFi/Fusion DSP | Don의 Xtensa 경험과 직결 |
| 그 밖 | NXP Neutron, Samsung, OpenVINO 등 | 지원 상태는 저장소 확인 |

말로 하면: ExecuTorch 자체는 "PyTorch 쪽의 공통 앞단 + 작은 런타임"이고, **실제 가속은 벤더 backend가 한다**. LiteRT가 delegate로, ONNX Runtime이 execution provider로 같은 일을 하는 것과 같은 구조다.

### 5.5 ExecuTorch vs TFLite/LiteRT

| 항목 | ExecuTorch | TFLite / LiteRT (F1) |
|---|---|---|
| 출발 프레임워크 | PyTorch (`torch.export`) | TF/Keras (PyTorch는 변환 도구 경유) |
| 모델 파일 | `.pte` (memory plan 포함) | `.tflite` |
| 가속 구조 | backend delegate (partitioner) | delegate |
| 양자화 흐름 | PT2E 양자화(`prepare_pt2e`/`convert_pt2e`, backend별 Quantizer) | converter의 PTQ/QAT, 양자화 파라미터가 파일에 |
| MCU | Ethos-U·Cortex-M 예제 있음, 성숙도는 확인 필요 | TFLM이 사실상 표준(F2), 생태계 넓음 |
| 강점 | PyTorch 모델을 재작성 없이, LLM 예제(Llama 등) 풍부 | 오래된 생태계, 벤더 지원표가 가장 많음 |
| 약점 | 비교적 새로움 → 벤더 backend 품질 편차 | PyTorch 모델은 변환 단계에서 op·layout 문제 |

### 5.6 임베디드 연결과 함정

- ExecuTorch의 `.pte`에는 **memory plan(텐서 오프셋)**이 들어간다. C6 10절의 memory planner를 빌드 타임에 끝내 놓는 것이다. 펌웨어로 치면 링커가 `.bss` 오프셋을 정해 두는 것과 같다.
- 함정: delegate가 그래프 일부만 받으면 나머지는 CPU portable kernel로 돈다. "QNN backend로 export했다"가 "전부 NPU에서 돈다"는 뜻이 아니다. partition 결과(어느 노드가 delegate됐는지)를 반드시 찍어 본다 — E5의 CPU fallback 문제와 같다.
- 함정: backend마다 요구하는 양자화 방식이 다르다(예: XNNPACK용 Quantizer와 QNN용 Quantizer가 따로). 같은 `.pte`를 여러 칩에 돌리겠다는 기대는 접는 게 좋다 — 칩마다 export를 따로 한다.

---

## 6. MediaPipe와 LiteRT Next — TFLite 위의 계층

### 6.1 MediaPipe

**MediaPipe**는 Google의 온디바이스 ML **파이프라인** 프레임워크다. 모델 하나가 아니라 "카메라/마이크 입력 → 전처리 → 모델 → 후처리 → 결과"를 그래프로 묶는다.

- 예전 "MediaPipe Solutions"(손·얼굴·포즈 추적 등)를 거쳐, 지금은 **MediaPipe Tasks**라는 API로 정리됐다: 예) object detector, hand landmarker, gesture recognizer, image/text/audio classifier, text embedder 등. Android, iOS, Web, Python에서 같은 이름으로 쓴다.
- 안에서 모델은 **TFLite(LiteRT)로 돈다**. Tasks용 모델 파일(`.task` 번들)은 TFLite 모델과 메타데이터(라벨, 전처리 정보)를 묶은 것이다.
- Model Maker로 소량 데이터에 fine-tune해서 Tasks 모델을 만들 수 있다.
- LLM용 API(온디바이스 Gemma 등)도 있었는데, 이 부분은 LiteRT 쪽 LLM 도구로 옮겨 가는 중으로 알려져 있다 — 현재 상태는 확인 필요.

edge 엔지니어 관점: MediaPipe는 **"이미 만들어진 기능(손 추적, 오디오 분류)을 빠르게 붙이는 앱 계층"**이다. MCU용이 아니고 폰/PC/Linux SoC용이다. 웨어러블에서는 동반 앱이나 Linux급 SoC에서 프로토타이핑할 때 의미가 있다. 펌웨어 비유로는 HAL 위의 "미들웨어 + 레퍼런스 앱".

### 6.2 LiteRT와 LiteRT Next

2024년 9월 Google은 TensorFlow Lite를 **LiteRT**(Lite Runtime)로 이름을 바꿨다. `.tflite` 포맷과 interpreter API는 그대로다(F1). 2025년에 "LiteRT Next"라는 이름으로 새 API(`CompiledModel` 계열)가 공개됐는데, 요점은 (문서 기준, 확인 필요):

- 모델을 열 때 가속기(CPU/GPU/NPU)를 지정하면 런타임이 미리 컴파일·배치까지 하는 **compile 단계**를 명시적으로 둔다.
- GPU·NPU 가속을 delegate 개별 설정이 아니라 통합 API로 다루고, Qualcomm·MediaTek NPU 지원을 강조한다.
- 입출력 버퍼를 하드웨어 버퍼(예: GPU 메모리)로 직접 넘겨 복사를 줄인다.

정리하면 LiteRT도 "interpreter + delegate"에서 "compile 후 실행"으로 움직이고 있다. 2절의 interpreter/compiler 경계가 흐려지는 흐름이다.

---

## 7. ML 컴파일러 — TVM, MLIR, IREE, XLA/StableHLO

### 7.1 왜 ML 컴파일러가 따로 있나

C 컴파일러는 "스칼라 연산 + 루프"를 다루지만, ML 모델은 "텐서 op의 그래프"다. conv 하나가 7중 루프다. 이걸 CPU·GPU·DSP·NPU마다 손으로 커널을 짜면 (op 수 × 정밀도 × 하드웨어) 조합이 폭발한다. ML 컴파일러는 이 조합을 **자동으로 생성**하려는 시도다.

```
 손으로 짠 커널 라이브러리:    op 200개 × dtype 3개 × HW 5종 = 3000개 커널을 사람이 유지
 ML 컴파일러:                  op 의미(수학) 200개 + HW별 lowering 규칙 5벌 → 코드는 생성
```

말로 하면: 라이브러리는 "곱하기", 컴파일러는 "더하기"로 규모가 늘어난다. 대신 생성 코드가 손 커널만큼 빠르게 하려면 튜닝이 필요하다.

### 7.2 MLIR — 컴파일러를 만드는 컴파일러 인프라

**MLIR**(Multi-Level Intermediate Representation)은 LLVM 프로젝트의 일부로, 여러 수준의 IR을 한 틀에서 정의하고 서로 변환하게 해 주는 **인프라**다. 그 자체가 ML 컴파일러는 아니다. 핵심 개념은 **dialect**(방언) — 한 수준의 op 집합이다.

```svg
<svg viewBox="0 0 680 305" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f6c" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs><rect x="20" y="20" width="330" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="30" y="37" font-size="13">tosa / stablehlo / torch</text><text x="30" y="53" font-size="12">그래프 수준: conv, matmul, add</text><line x1="50" y1="60" x2="50" y2="74" stroke="currentColor" stroke-width="1.3" marker-end="url(#f6c)"/><rect x="80" y="74" width="330" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="90" y="91" font-size="13">linalg (on tensors)</text><text x="90" y="107" font-size="12">구조화된 루프 연산: linalg.matmul, generic</text><line x1="110" y1="114" x2="110" y2="128" stroke="currentColor" stroke-width="1.3" marker-end="url(#f6c)"/><rect x="140" y="128" width="330" height="40" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="150" y="145" font-size="13">tiling · fusion · bufferization</text><text x="150" y="161" font-size="12">타일로 쪼개고, 합치고, tensor → memref</text><line x1="170" y1="168" x2="170" y2="182" stroke="currentColor" stroke-width="1.3" marker-end="url(#f6c)"/><rect x="200" y="182" width="330" height="40" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="210" y="199" font-size="13">scf / affine / vector</text><text x="210" y="215" font-size="12">명시적 루프, SIMD 벡터 연산</text><line x1="230" y1="222" x2="230" y2="236" stroke="currentColor" stroke-width="1.3" marker-end="url(#f6c)"/><rect x="260" y="236" width="330" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="1.5"/><text x="270" y="253" font-size="13">llvm / spirv / 벤더 IR</text><text x="270" y="269" font-size="12">기계 코드 직전: 레지스터·명령어</text><text x="675" y="40" font-size="12" text-anchor="end">높은 수준 (무엇을 계산하나)</text><text x="675" y="294" font-size="12" text-anchor="end">낮은 수준 (어떻게 계산하나)</text><line x1="640" y1="50" x2="640" y2="275" stroke="currentColor" stroke-width="1.3" marker-end="url(#f6c)"/><text x="632" y="120" font-size="12" text-anchor="end">lowering</text>
</svg>
```

그림 7 — MLIR에서 lowering은 dialect 계단을 내려가는 일이다. 위는 "무엇을 계산하나"(conv, matmul), 아래로 갈수록 "어떻게 계산하나"(루프, 벡터, 명령어). tiling·fusion은 중간(linalg 수준)에서 일어난다.

| dialect | 수준 | 예 |
|---|---|---|
| `stablehlo` | 그래프 (XLA 계열 op) | `stablehlo.convolution`, `stablehlo.dot_general` |
| `tosa` | 그래프 (Arm 주도의 정수·부동소수 op 표준) | `tosa.conv2d`, `tosa.rescale` — TFLite 변환에서 쓰임 |
| `linalg` | 구조화된 연산 (루프 nest를 의미로 표현) | `linalg.matmul`, `linalg.generic` |
| `tensor` / `memref` | 값 의미 텐서 / 메모리 버퍼 | bufferization이 tensor → memref |
| `scf` / `affine` | 명시적 루프 | `scf.for` |
| `vector` | SIMD 벡터 | `vector.contract`, `vector.fma` |
| `llvm` / `spirv` | 타깃 직전 | LLVM IR, Vulkan SPIR-V |

손으로 따라가 보는 lowering (개념 예, 문법은 단순화):

```
 tosa.matmul  %A[1x64x64], %B[1x64x64]          ← "행렬곱 하나"
   ↓ tosa → linalg
 linalg.matmul ins(%A, %B) outs(%C)             ← 3중 루프의 의미
   ↓ tiling (16×16×16) + fusion(bias, relu)
 scf.for i0 = 0..64 step 16
   scf.for j0 = 0..64 step 16
     scf.for k0 = 0..64 step 16
       linalg.matmul (16x16 타일)                ← 2.4절 C 코드의 (2)번 모양
     bias + relu (타일 안에서)
   ↓ vectorization
 vector.fma ... (NEON/Helium 4~8 lane)
   ↓
 llvm.* → 기계어
```

말로 하면: 2.4절에서 손으로 쓴 tiled + fused C 코드를 **컴파일러가 패스로 만들어 내는 과정**이 이 계단이다. Don이 링커 스크립트로 TCM에 맞게 버퍼를 나누던 결정을 컴파일러가 타일 크기 선택으로 대신한다.

MLIR이 중요한 이유: TFLite 변환기, IREE, torch-mlir, 많은 NPU 벤더 컴파일러가 MLIR 위에 지어졌거나 MLIR을 쓴다. 벤더 컴파일러 로그에 `linalg`, `tosa` 같은 단어가 보이면 "아 MLIR 기반이구나"를 알아보는 정도면 면접에서 충분하다.

### 7.3 TVM

**Apache TVM**은 가장 오래된 오픈소스 end-to-end ML 컴파일러 중 하나다(워싱턴대에서 시작, 2017년 전후).

- **IR**: 예전 그래프 IR이 **Relay**, 새 세대(TVM Unity 이후) 그래프 IR이 **Relax**, 커널 수준 IR이 **TensorIR(TIR)**다. 최신 버전은 Relax 중심이다.
- **튜닝**: 같은 op도 타일 크기·루프 순서·벡터화 방식에 따라 속도가 크게 달라진다. TVM은 실제 기기에서 후보를 돌려 보고 빠른 걸 고르는 auto-tuning을 한다. 세대 순서대로 **AutoTVM**(사람이 짠 템플릿 + 탐색) → **Ansor/auto-scheduler**(템플릿 없이 탐색) → **MetaSchedule**(통합 탐색 프레임워크).
- **microTVM**: MCU용 TVM(Zephyr 등에서 C 코드 생성·실행). 최근 릴리스에서 유지보수가 중단되고 코드가 정리(제거)된 것으로 알려져 있다 — **새 프로젝트에 쓰기 전에 현재 상태를 반드시 확인**해야 한다.
- 일부 NPU 벤더가 자사 컴파일러를 TVM 위에 만들었다(BYOC, Bring Your Own Codegen 구조). 벤더 SDK 안에 TVM이 숨어 있는 경우가 있다.

실행 안 함 (tvm 미설치, Relax frontend API 모양 — 버전에 따라 다름):

```python
# 실행 안 함: tvm 미설치. Relax의 torch.export frontend 사용 예의 대략적 모양
import tvm
from tvm import relax
from tvm.relax.frontend.torch import from_exported_program
mod = from_exported_program(ep)               # 1절의 ExportedProgram → Relax IRModule
ex = relax.build(mod, target="llvm")          # 그래프 최적화 + TIR lowering + 코드 생성
vm = relax.VirtualMachine(ex, tvm.cpu())
out = vm["main"](tvm.nd.array(x.numpy()))
```

### 7.4 IREE

**IREE**(Intermediate Representation Execution Environment)는 MLIR 기반의 **컴파일러 + 런타임** 묶음이다.

- **입력**: StableHLO(JAX/TF), TOSA(TFLite), PyTorch(torch-mlir / iree-turbine 경유), ONNX(import 도구 경유).
- **컴파일**: 그래프를 "dispatch region"(fusion된 커널 단위)으로 나누고 각 region을 타깃용 코드로 생성한다.
- **런타임**: HAL(Hardware Abstraction Layer)로 디바이스를 추상화한다. 펌웨어의 HAL과 같은 의미다 — 위 계층은 "버퍼 할당, 커널 dispatch, 동기화"만 요청하고, 아래에 CPU(local-task/local-sync), Vulkan, CUDA, ROCm, Metal 같은 드라이버가 붙는다.
- **임베디드**: CPU 타깃 코드를 정적 라이브러리로 만들어 bare-metal/RTOS에 링크하는 예제가 있다 (`local-sync` 드라이버, 스레드 없음). 결과물은 `.vmfb`(VM FlatBuffer) 모듈이다.

실행 안 함 (iree 미설치, 플래그 이름은 버전에 따라 바뀌어 왔다 — 예전에는 `--iree-hal-target-backends=llvm-cpu`):

```sh
# 실행 안 함: IREE 미설치
iree-compile model.mlir \
  --iree-hal-target-device=local \
  --iree-hal-local-target-device-backends=llvm-cpu \
  --iree-llvmcpu-target-cpu=host \
  -o model.vmfb
iree-run-module --module=model.vmfb --device=local-task --function=main --input="1x1x8x8xf32=0"
```

### 7.5 XLA와 StableHLO

- **XLA**는 TensorFlow·JAX의 컴파일러로, 지금은 OpenXLA 프로젝트로 독립했다. 주 무대는 GPU·TPU(서버)다.
- **StableHLO**는 XLA의 HLO op set을 **버전 호환을 보장하는** 교환 포맷으로 만든 것이다 (MLIR dialect + 직렬화). JAX/TF/PyTorch-XLA → StableHLO → XLA 또는 IREE.
- edge 관점: JAX 모델을 edge로 보낼 때 StableHLO → IREE 경로가 있고, LiteRT 쪽에도 JAX 변환 경로가 있다. Hark 같은 PyTorch 중심 팀이라면 직접 쓸 일은 적지만, "StableHLO = ML 쪽의 안정 ABI"라고 이해해 두면 된다.

### 7.6 정리 표

| 컴파일러 | 입력 | IR | 출력 | edge에서의 위치 |
|---|---|---|---|---|
| TVM | ONNX, PyTorch(ExportedProgram), TFLite 등 | Relax, TensorIR | 공유 라이브러리 / C 소스 | 벤더 컴파일러의 기반, 연구·자체 NPU |
| IREE | StableHLO, TOSA, PyTorch, ONNX | MLIR dialects | `.vmfb` + HAL 드라이버 | CPU/GPU 이식성, 임베디드 CPU |
| XLA | StableHLO (JAX/TF) | HLO | GPU/TPU/CPU 코드 | 주로 서버 |
| Arm Vela (E5) | int8 `.tflite` | 자체 | Ethos-U command stream 들어간 `.tflite` | Cortex-M + Ethos-U |
| QNN 컨버터 (F4) | ONNX, TFLite, PyTorch | 자체 | context binary | Hexagon |
| coremltools (4절) | PyTorch, TF | MIL | `.mlpackage` → 기기에서 `.mlmodelc` | Apple |

---

## 8. MCU용 도구 — Edge Impulse, ST, NXP, Arm, microTVM

MCU 쪽은 "런타임 + 컴파일러 + 데이터/학습 플랫폼"이 한 벤더 도구에 묶여 나오는 경우가 많다.

### 8.1 Edge Impulse

- **무엇**: 웹 기반 end-to-end TinyML 플랫폼. 센서 데이터 수집(기기에서 직접 업로드) → 라벨링 → 신호처리 블록(MFCC, spectral features 등 DSP 블록) → 학습(작은 NN, anomaly detection) → 기기용 배포까지 한 흐름으로 묶는다.
- **EON Compiler**: 학습된 모델을 TFLM interpreter 없이 돌도록 **모델 전용 C++ 코드**로 컴파일해 RAM·flash를 줄인다고 소개한다 (절감 비율은 모델마다 다르다는 게 공식 설명). 2절의 "compiler형"이다.
- **EON Tuner**: 기기 RAM/latency 제약 안에서 DSP 블록 + 모델 구조 조합을 자동 탐색하는 AutoML.
- **배포 출력**: C++ 라이브러리(어느 MCU에나 링크), Arduino 라이브러리, 지원 보드용 펌웨어 바이너리, Linux용 실행 모듈 등.
- **회사**: 2025년 Qualcomm이 Edge Impulse 인수를 발표했다. Qualcomm 칩을 쓸 것으로 보이는 Hark 같은 회사에서는 "툴 생태계가 칩 벤더 쪽으로 묶일 가능성"을 염두에 둘 만하다 (추정).

### 8.2 STM32Cube.AI / ST Edge AI

- **무엇**: ST의 MCU용 모델 변환·최적화 도구. 예전 이름 X-CUBE-AI(STM32Cube.AI)가 지금은 여러 ST 제품군(STM32, Stellar, 센서 내장 ML 코어)을 아우르는 **ST Edge AI Core**(CLI `stedgeai`)로 정리되는 중이다 (이름·범위는 확인 필요).
- **입력**: Keras, TFLite, ONNX(양자화 모델 포함).
- **출력**: C 라이브러리 — 네트워크 구조 코드 + weight 배열 + ST 런타임 라이브러리. 메모리 사용량(flash/RAM) 리포트와 기기 위 검증(validate) 명령이 있다.
- **STM32N6**: Neural-ART Accelerator라는 자체 NPU가 들어간 MCU. 이 NPU는 ST 도구 체인으로 컴파일한다.
- **ST Edge AI Developer Cloud**: 실제 보드 팜에서 벤치마크를 돌려 주는 웹 서비스 (Qualcomm AI Hub와 비슷한 발상, F4).

### 8.3 NXP eIQ

- **eIQ ML 소프트웨어**: NXP MCU·응용 프로세서(i.MX)용 ML 도구 묶음. 런타임은 TFLite/TFLM, ONNX Runtime 등 표준 런타임을 NXP 가속기에 붙여 제공한다.
- **eIQ Neutron NPU**: NXP 자체 NPU (MCX N 같은 MCU, i.MX 95 등). Neutron용 변환 도구가 따로 있다.
- **i.MX 93**: Arm Ethos-U65를 쓰므로 Vela 경로.
- eIQ Toolkit(모델 변환·프로파일 GUI), eIQ Time Series Studio(MCU용 시계열 모델 자동 생성) 같은 도구가 있다 (이름·범위는 확인 필요).

### 8.4 Arm Vela와 CMSIS-NN

E5에서 자세히 봤다. 요약: int8 `.tflite` → **Vela**가 Ethos-U가 돌릴 부분을 command stream으로 컴파일해 custom op 하나로 바꾼 `.tflite`를 출력 → 기기에서는 **TFLM + Ethos-U driver**가 그 op를 NPU에 넘기고, 나머지는 **CMSIS-NN** 커널로 Cortex-M에서 돈다. ExecuTorch의 Ethos-U backend도 내부에서 Vela를 쓴다.

### 8.5 정리 표 — 무엇을 받아 무엇을 내놓나

| 도구 | 대상 HW | 입력 | 출력 | 런타임 형태 | 상태 메모 |
|---|---|---|---|---|---|
| TFLM + CMSIS-NN (F2) | Cortex-M 전반 | int8 `.tflite` | 모델 C 배열 + 라이브러리 | interpreter (arena) | 사실상 표준 |
| Arm Vela | Ethos-U55/U65/U85 | int8 `.tflite` | Ethos-U op가 들어간 `.tflite` | TFLM + NPU driver | E5 |
| Edge Impulse (EON) | 대부분의 MCU, Linux | 플랫폼 내 학습 모델 (`.tflite`·ONNX import도) | C++ 라이브러리 / 바이너리 | 모델 전용 코드 | Qualcomm 인수 발표(2025) |
| ST Edge AI / Cube.AI | STM32 (N6 NPU 포함) | Keras, TFLite, ONNX | C 라이브러리 + 리포트 | ST 런타임 | 이름 변경 중 |
| NXP eIQ | NXP MCU, i.MX | TFLite, ONNX | 런타임 + Neutron/Vela 변환 결과 | TFLM/LiteRT/ORT + 가속기 | 제품군별 상이 |
| microTVM | MCU (Zephyr 등) | ONNX, TFLite | C 소스 | 생성 코드 | 최근 제거된 것으로 알려짐 — 확인 필요 |
| ExecuTorch (Arm backend) | Cortex-M + Ethos-U | PyTorch | `.pte` | ExecuTorch runtime + Vela | 비교적 새로움 |

펌웨어 비유: TFLM은 "범용 RTOS", 벤더 도구(Cube.AI, eIQ)는 "벤더 HAL + 코드 생성기(CubeMX 같은)", Edge Impulse는 "데이터부터 바이너리까지 주는 턴키 IDE"다. 장단점도 똑같다 — 턴키는 빠르지만 벤더 종속, 범용은 손이 가지만 이식성이 좋다.

---

## 9. 런타임 고르기 — 기준표와 웨어러블 시나리오

### 9.1 기준표

새 기기의 런타임을 고를 때 물어야 할 것들이다. 각 행은 "어떻게 확인하나"가 핵심이다 — 벤더 슬라이드가 아니라 **직접 돌려서** 확인한다 (M1·M2와 같은 태도).

| 기준 | 묻는 것 | 확인 방법 | 위험 신호 |
|---|---|---|---|
| 하드웨어 벤더 지원 | 이 칩의 NPU/DSP에 붙는 공식 경로가 무엇인가 | 벤더 SDK 문서, 지원 런타임 목록 | "곧 지원 예정", 커뮤니티 포크만 있음 |
| op 커버리지 | 우리 모델의 op가 가속기에 다 올라가나 | 실제 모델을 변환해 partition/fallback 리포트 확인 (E5, C6) | CPU fallback이 그래프 중간에 끼어 있음 |
| 양자화 지원 | int8? int4? per-channel? activation int16? | 변환 후 정확도·SNR 측정 (C2, C8) | 벤더 양자화기만 지원, 우리 QAT 결과를 못 받음 |
| 모델 포맷 | 모델팀의 export 경로와 맞나 (PyTorch → ?) | export → 변환 → 수치 비교 1회 성공 | 변환 중간에 수작업 그래프 수정이 필요 |
| 바이너리·메모리 | 런타임 코드 크기, arena/RAM, 모델 flash | 빈 앱 vs 런타임 링크 후 map 파일 비교 (F7) | 런타임만 수백 KB인데 MCU flash가 1 MB |
| 성능 | latency·전력·열 (sustained) | 실기기 측정, 벤더 프로파일러 (K, D6, D7) | 데이터시트 TOPS만 있고 실측 없음 |
| 업데이트 경로 | 모델만 OTA로 바꿀 수 있나, 런타임 재빌드가 필요한가 | 모델 교체 시나리오 리허설 | 모델 교체마다 펌웨어 전체 재인증 |
| 디버깅 도구 | 층별 출력 덤프, 프로파일러, 정확도 비교 도구 | 일부러 깨진 모델로 문제 찾기 연습 | 블랙박스 바이너리, 로그 없음 |
| 라이선스 | 오픈소스(Apache 2.0, MIT, BSD) vs 벤더 라이선스 | 법무 검토, 재배포 조건 | 재배포 제한, 런타임 소스 비공개 |
| 커뮤니티·지속성 | 릴리스 주기, 이슈 대응, 몇 년 갈 프로젝트인가 | GitHub 활동, 벤더 로드맵 | 이름이 자주 바뀌고 이전 API가 사라짐 (microTVM 사례) |

라이선스 참고 (각 저장소의 LICENSE 기준으로 알려진 것, 제품 적용 전 재확인): TFLite/LiteRT·TFLM·TVM·MediaPipe·CMSIS-NN·Vela = Apache 2.0, ONNX Runtime·llama.cpp = MIT, ExecuTorch = BSD 계열, IREE = Apache 2.0 with LLVM Exceptions, coremltools = BSD-3 (Core ML 프레임워크 자체는 Apple OS 구성요소), QNN SDK·ST·Edge Impulse 플랫폼 = 각 회사 약관.

### 9.2 Hark 같은 웨어러블이라면 — 엔진별 추론 (추정)

예를 들어 Hark 같은 웨어러블이 always-on MCU(Cortex-M) + Qualcomm SoC(Hexagon NPU/DSP, Kryo CPU, Adreno GPU) 구조라고 **가정**해 보자. 실제 구성은 공개되지 않았으므로 아래는 추론 연습이다.

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f6e" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs><rect x="10" y="40" width="100" height="40" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="60.0" y="64.0" font-size="12" text-anchor="middle">마이크</text><rect x="10" y="100" width="100" height="40" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="60.0" y="124.0" font-size="12" text-anchor="middle">IMU · 착용 센서</text><rect x="150" y="30" width="220" height="120" rx="5" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="1.5"/><text x="260.0" y="64.0" font-size="12" text-anchor="middle">always-on MCU (Cortex-M)</text><text x="260.0" y="79.0" font-size="12" text-anchor="middle">wake word · 제스처 · 착용 감지</text><text x="260.0" y="94.0" font-size="12" text-anchor="middle">TFLM + CMSIS-NN</text><text x="260.0" y="109.0" font-size="12" text-anchor="middle">(NPU 있으면 Vela / 벤더 툴)</text><text x="260.0" y="124.0" font-size="12" text-anchor="middle">또는 Edge Impulse EON</text><rect x="150" y="190" width="375" height="110" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><rect x="160" y="225" width="170" height="62" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="245.0" y="245.0" font-size="12" text-anchor="middle">Hexagon NPU/DSP</text><text x="245.0" y="260.0" font-size="12" text-anchor="middle">QNN / LiteRT·ORT·</text><text x="245.0" y="275.0" font-size="12" text-anchor="middle">ExecuTorch QNN delegate</text><rect x="340" y="225" width="175" height="62" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="427.5" y="245.0" font-size="12" text-anchor="middle">Kryo CPU · Adreno GPU</text><text x="427.5" y="260.0" font-size="12" text-anchor="middle">llama.cpp (LLM)</text><text x="427.5" y="275.0" font-size="12" text-anchor="middle">LiteRT / ExecuTorch XNNPACK</text><rect x="550" y="225" width="125" height="62" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="612.5" y="252.5" font-size="12" text-anchor="middle">클라우드 LLM</text><text x="612.5" y="267.5" font-size="12" text-anchor="middle">(hybrid: 무거운 요청)</text><rect x="420" y="40" width="250" height="100" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="545.0" y="71.5" font-size="12" text-anchor="middle">앱 · OS (Android 계열 가정)</text><text x="545.0" y="86.5" font-size="12" text-anchor="middle"></text><text x="545.0" y="101.5" font-size="12" text-anchor="middle">모델 업데이트(OTA)</text><text x="545.0" y="116.5" font-size="12" text-anchor="middle">런타임 버전 = 펌웨어 버전처럼 관리</text><line x1="110.0" y1="60.0" x2="150.0" y2="90.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6e)"/><line x1="110.0" y1="120.0" x2="150.0" y2="90.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6e)"/><line x1="260.0" y1="150.0" x2="337.5" y2="190.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6e)"/><line x1="515.0" y1="256.0" x2="550.0" y2="256.0" stroke="currentColor" stroke-opacity="0.6" stroke-width="1.2" marker-end="url(#f6e)"/><text x="420" y="175" font-size="12">wake 이벤트 · 오디오 버퍼 (IPC)</text><text x="165" y="212" font-size="13">Qualcomm SoC (가정)</text>
</svg>
```

그림 8 — 가정한 웨어러블 구조와 엔진별 런타임 후보. MCU는 항상 켜져 wake word·제스처를 보고, 이벤트가 나면 SoC를 깨운다. SoC 안에서는 NPU/DSP가 음성 모델, CPU/GPU가 LLM·나머지를 맡는다.

| 엔진 | 워크로드 예 | 1순위 후보 | 대안 | 근거 |
|---|---|---|---|---|
| always-on MCU | wake word, IMU 제스처, 착용 감지 (수십 KB 모델) | TFLM + CMSIS-NN (MCU에 Ethos-U가 있으면 + Vela) | Edge Impulse EON, 칩 벤더 도구(Cube.AI 등), 손으로 짠 C (A5) | 생태계·디버깅·이식성. 모델이 작아 interpreter 오버헤드보다 전력 관리가 중요 |
| Hexagon NPU/DSP | VAD, ASR encoder, speaker ID, 노이즈 억제 | QNN 직접 (context binary, F4) | LiteRT + QNN delegate, ORT + QNN EP (F5), ExecuTorch + QNN backend | NPU 성능을 끝까지 쓰려면 벤더 경로. 상위 런타임은 개발 속도·이식성 이득 |
| CPU/GPU | 온디바이스 SLM/LLM, 후처리 | llama.cpp (F3) 또는 Qualcomm Genie (F4) | ExecuTorch (LLM 예제), LiteRT 계열 LLM 도구 | LLM은 decode가 메모리 대역폭 bound (D5). 양자화 포맷(GGUF Q4 등)과 생태계 |
| 동반 iPhone 앱 (있다면) | 폰 쪽 보조 모델 | Core ML (4절) | ExecuTorch + Core ML backend | Apple 기기에서는 ANE 접근 경로가 사실상 Core ML |
| 클라우드 | 큰 LLM | 서버 런타임 | — | hybrid edge-cloud (L 모듈) |

선택 과정을 말로 정리하면:

1. **모델팀의 출발점**을 본다. PyTorch라면 `torch.export`가 깨지지 않게 모델을 짜는 규칙(정적 shape, 지원 op)을 초기에 합의한다.
2. **가속기 벤더의 1급 경로**를 기준선으로 잡는다 (Qualcomm이면 QNN). 상위 런타임(LiteRT/ORT/ExecuTorch)은 그 위에 얹었을 때 성능 손실이 얼마인지 **실측**해서 정한다.
3. **엔진마다 다른 런타임을 쓰는 걸 두려워하지 않는다.** MCU에서 ExecuTorch를, SoC NPU에서 TFLM을 쓸 이유는 없다. 대신 **전처리(특징 추출)와 검증 하네스는 공통**으로 둔다 (같은 golden vector로 모든 엔진 검증, C8).
4. **업데이트 경로**를 정한다. 모델만 OTA 하고 싶다면 런타임 버전과 모델 포맷 버전의 호환 매트릭스를 펌웨어 버전처럼 관리한다. QNN context binary는 SDK·칩 버전에 묶일 수 있으므로 특히 주의(F4).

---

## 10. 임베디드 관점에서 다시 보기

- **런타임 = 펌웨어 의존성**: 런타임 버전, 변환기 버전, 벤더 SDK 버전, 모델 파일 버전이 서로 묶인다. 4.3절의 "coremltools 9는 torch 2.7까지만 테스트" 경고가 바로 그 예다. 펌웨어에서 "컴파일러 버전 고정 + 툴체인 해시 기록"을 하듯이, ML 쪽도 **변환 환경을 lockfile로 고정**하고 산출물에 버전을 기록한다.
- **"허락"과 "실행"은 다르다**: Core ML의 compute units, ExecuTorch의 partitioner, LiteRT의 delegate, ORT의 EP 모두 "이 가속기를 써도 된다"는 설정이다. 실제로 어디서 돌았는지는 plan/리포트/trace로 확인한다 (4.6절). fp32 모델이 ANE를 못 쓰고 CPU로 돈 사례(4.4절)는 어떤 NPU에서도 생긴다 — 예: int8이 아닌 op는 Ethos-U에서 CPU로 떨어진다.
- **정밀도는 엔진마다 다르다**: 같은 fp16이라도 CPU/GPU/ANE의 오차가 6.8e-3 / 5.3e-4 / 1.9e-3로 달랐다. golden 비교는 **배포 엔진에서, 신호 크기로 정규화한 지표로** 한다.
- **압축의 1차 이득은 크기**: 4-bit palettization으로 weight를 1/4로 줄여도 MobileNetV2 latency는 그대로였다. MCU에서는 flash·OTA 크기가 곧 비용이라 이 이득이 크고, latency 이득은 weight-bound 워크로드에서 나온다 (D3, D5).
- **컴파일러 산출물을 열어 본다**: `.mlpackage`의 op 목록(BN이 접혔나), `.pte`의 delegate 목록, Vela 리포트의 NPU/CPU 비율, `.vmfb`의 dispatch 수. Don이 map 파일과 disassembly를 열어 보던 습관을 그대로 쓰면 된다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 랜덤/미학습 모델로 변환 검증 | 출력 차이가 0에 가까워 "완벽히 일치"로 보임 | 출력 자체가 0 근처 (activation 소멸) | 기준 출력 크기부터 확인, 상대 오차·SNR·cosine 사용, 가능하면 학습된 모델 |
| compute unit을 허락했으니 NPU에서 돈다고 가정 | 기대보다 느림, CPU 부하와 같이 흔들림 | 정밀도·op 미지원으로 CPU/GPU fallback | compute plan / 벤더 리포트 / 프로파일러로 배치 확인, fp16 또는 int8로 변환 |
| 변환 도구가 테스트하지 않은 프레임워크 버전 사용 | 변환은 되는데 특정 op에서 수치가 틀리거나 크래시 | 버전 미스매치 (coremltools ↔ torch 등) | release note의 검증 버전으로 환경 고정, lockfile |
| PC CPU에서 재고 기기 성능이라고 보고 | 실기기에서 몇 배 차이 | 엔진·메모리·열 조건이 다름 | 대상 엔진에서 측정, warm-up·median·반복, 부하 조건 기록 |
| 압축 후 정확도 소폭 상승을 "개선"으로 보고 | 다음 데이터에서 재현 안 됨 | test set 통계 잡음 (1000개면 ±0.5%p) | 신뢰구간 표시, 더 큰 평가셋, 여러 seed |
| 2-bit 이하 palettization을 전 층에 일괄 적용 | 정확도 급락 (0.976 → 0.785) | LUT 값이 너무 적어 분포 표현 불가 | 민감한 층은 높은 비트로, per-grouped LUT, 또는 학습 중 압축(QAT 계열) |
| delegate/partitioner 결과를 확인하지 않음 | NPU export했는데 전력·latency 개선 미미 | 그래프 일부만 delegate, 중간에 CPU 구간 | partition 리포트 확인, 미지원 op 교체 (C6, C7) |
| 모든 엔진에 한 런타임을 강제 | MCU에 거대한 런타임, NPU 성능 손실 | 엔진마다 최적 도구가 다름 | 엔진별 런타임 + 공통 전처리·검증 하네스 |
| 실험적 도구(microTVM 등)에 제품을 묶음 | 1~2년 뒤 유지보수 중단 | 프로젝트 지속성 미확인 | 커뮤니티·벤더 로드맵 확인, 탈출 경로(ONNX/TFLite 원본 보관) |

---

## 12. 면접에서 이렇게 말한다

**Q.** How would you choose an inference runtime for a new device?

**A.** 먼저 하드웨어 벤더의 1급 경로를 기준선으로 잡고(Qualcomm이면 QNN), 우리 실제 모델로 변환해서 op 커버리지·CPU fallback·정확도·latency·메모리를 잰다. 그다음 상위 런타임(LiteRT, ORT, ExecuTorch)을 얹었을 때 손실이 얼마인지 비교하고, 업데이트 경로·디버깅 도구·라이선스·지속성을 본다. 엔진마다 다른 런타임을 써도 되고, 대신 전처리와 검증 하네스는 공통으로 둔다.

> I start from the silicon vendor's first-class path as the baseline and convert our actual model, not a demo, to measure op coverage, CPU fallbacks, accuracy against a golden reference, latency, and memory on the real device. Then I check what a higher-level runtime like LiteRT, ONNX Runtime, or ExecuTorch costs on top of that, and weigh model update path, debugging tools, license, and long-term support. It's fine to use different runtimes per engine — TFLM on the always-on MCU, QNN on the NPU — as long as preprocessing and the validation harness are shared.

**Q.** What does an ML compiler do that an interpreter doesn't?

**A.** interpreter는 op 리스트를 런타임에 하나씩 dispatch하고 중간 tensor를 매번 메모리에 쓴다. 컴파일러는 빌드 타임에 그래프 전체를 보고 fusion, 타깃 캐시에 맞춘 tiling, layout 선택, 정적 메모리 계획을 해서 모델 전용 코드를 만든다. 대가는 모델 교체 시 재컴파일이 필요하다는 것과, 타깃을 잘 모르면 손 커널보다 느릴 수 있다는 것.

> An interpreter walks the op list at runtime, dispatching a generic kernel per op and materializing every intermediate tensor in memory. A compiler sees the whole graph ahead of time, so it can fuse ops, tile loops to the target's cache or SRAM, pick layouts globally, and plan memory statically, then emit model-specific code. For a 64×64 matmul plus bias and ReLU, fusion alone removes 64 KB of intermediate memory traffic. The trade-offs are recompilation when the model changes and the need for tuning to beat hand-written kernels.

**Q.** ExecuTorch vs TFLite — when would you pick which?

**A.** PyTorch 중심 팀이고 `torch.export`가 깨끗하게 되며, 타깃 backend(QNN, Core ML, XNNPACK)가 ExecuTorch에서 잘 지원되면 ExecuTorch가 변환 단계를 줄여 준다. MCU 생태계, 벤더 지원표의 넓이, 성숙한 디버깅 도구가 중요하면 TFLite/LiteRT·TFLM이 안전하다. 결정은 실제 모델로 두 경로를 다 돌려 본 결과로 한다.

> If the team is PyTorch-native, the model exports cleanly with torch.export, and the target backend — XNNPACK, Core ML, or Qualcomm — is well supported in ExecuTorch, it removes a conversion step and keeps one path from training to device. If I need the broadest vendor support, the MCU ecosystem with TFLM and CMSIS-NN, and mature tooling, TFLite or LiteRT is the safer bet. Either way I'd prototype both on the real model and decide on measured coverage and accuracy, not on slides.

**Q.** How do you know whether Core ML actually used the Neural Engine?

**A.** compute units는 허가일 뿐이다. 세 단계로 본다: (1) CPU_ONLY 대비 latency가 크게 줄고 CPU 부하에 무관한지, (2) `MLComputePlan`으로 op별 선호 디바이스와 지원 디바이스를 확인, (3) Instruments의 Core ML 트레이스나 Xcode 성능 리포트로 실제 실행 확인. 실제로 fp32 패키지는 Neural Engine을 허락해도 지원 디바이스에 ANE가 없어서 CPU 속도로 돌았다.

> Setting compute units only allows the Neural Engine; it doesn't force it. I check three things: whether latency drops sharply versus CPU-only and stays stable under CPU load, what MLComputePlan reports as the preferred and supported device per op, and finally an Instruments Core ML trace for ground truth. On an M2 I saw a fp32 MobileNetV2 run at CPU speed even with CPU_AND_NE, and the compute plan confirmed the Neural Engine wasn't in its supported set — converting to fp16 put all 100 ops on the ANE and cut latency about tenfold.

**Q.** What is MLIR, and why do so many ML compilers use it?

**A.** MLIR은 컴파일러 자체가 아니라 여러 수준의 IR(dialect)을 정의하고 서로 lowering하는 인프라다. 그래프 수준(tosa, stablehlo) → 구조화 연산(linalg)에서 tiling·fusion → 루프·벡터 → LLVM/SPIR-V로 내려간다. 벤더가 자기 NPU용 dialect와 lowering만 추가하면 앞단·최적화 패스를 재사용할 수 있어서 많이 쓴다.

> MLIR is compiler infrastructure for defining multiple levels of IR, called dialects, and lowering between them. A model enters at graph level, like TOSA or StableHLO, becomes structured ops in linalg where tiling and fusion happen, then loops and vectors, and finally LLVM or SPIR-V. Vendors can add their own accelerator dialect and reuse the front ends and passes, which is why IREE, the TFLite converter, and many NPU toolchains are built on it.

**Q.** What's the difference between weight palettization and linear quantization?

**A.** linear quantization은 `w ≈ scale · q`로 균일한 격자에 놓고, palettization은 k-means로 2^n개 대표값(LUT)을 만들어 인덱스만 저장한다. palettization은 분포가 고르지 않을 때 유리하고 크기는 비트 수로 정해진다. 실측에서 작은 CNN은 4-bit까지 정확도 유지, 2-bit에서 0.976 → 0.785로 무너졌다.

> Linear quantization maps weights to a uniform grid, w ≈ scale × q, usually per channel. Palettization clusters weights into a small lookup table of 2^n values with k-means and stores only n-bit indices, which fits non-uniform distributions better. In my test, a small CNN kept its 97.6% accuracy down to 4-bit palettes at a quarter of the fp16 size, but dropped to 78.5% at 2 bits — so I'd go mixed-precision for sensitive layers.

---

## 13. 직접 해보기

1. **손계산**: 파라미터 1.2M개 모델을 fp16, int8, 6-bit palette, 4-bit palette로 저장하면 weight 크기는 각각 몇 MB인가 (LUT·scale 무시)?
   정답: 2.4 MB, 1.2 MB, 0.9 MB, 0.6 MB (1.2M × 2, × 1, × 0.75, × 0.5 바이트).
2. **손계산**: 2.3절 방식으로, 128×128 fp32 중간 tensor 하나에 bias add와 ReLU가 따로 붙어 있을 때 fusion으로 없어지는 트래픽은?
   정답: 128·128·4 B = 64 KB, 4번 왕복이니 256 KB.
3. **코드**: 3.3절 예제에서 `nn.GELU()`를 추가하고 `run_decompositions()` 결과를 찍어 보라. GELU는 Core ATen에 남는가, 쪼개지는가? `get_decompositions([torch.ops.aten.gelu])`를 주면 어떻게 되나?
   힌트: Core ATen 목록에 gelu가 있으면 남는다. 강제 분해하면 `erf`가 들어간 primitive들로 바뀐다 (C6 8.2절 식).
4. **코드**: 4.4절 스크립트에 `compute_precision`을 fp16으로 두되 특정 op만 fp32로 남기는 변환(`ct.transform.FP16ComputePrecision(op_selector=...)`)을 시도하고, `MLComputePlan`으로 그 op들이 어느 디바이스로 가는지 보라.
   힌트: fp32 op는 ANE 지원 목록에서 빠질 것으로 예상된다 → 그래프 중간에 엔진 전환(fallback)이 생기고 latency가 늘어난다. 직접 확인할 것.
5. **조사**: 지금 쓰는(또는 관심 있는) MCU 하나를 골라 9.1절 기준표 10행을 채워 보라. "확인 방법" 칸은 반드시 직접 해 볼 수 있는 것으로 쓴다.
   정답 예: STM32N6이면 벤더 경로 = ST Edge AI, op 커버리지 = 실제 모델 변환 리포트, 업데이트 = weight 배열만 교체 가능한지 링커 섹션으로 확인 (F7).
6. **비교**: 4.7절 작은 CNN을 ONNX로 export해 ONNX Runtime CPU에서 정확도와 latency를 재고 Core ML 결과와 표로 비교하라 (F5와 연결).
   힌트: ORT CPU fp32는 PyTorch와 1e-6 수준으로 일치해야 하고, Core ML fp16과의 차이는 4.5절 크기 정도가 나와야 한다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| runtime | 런타임 | 기기에서 모델 파일을 읽어 실행하는 라이브러리 (TFLM, LiteRT, ORT, ExecuTorch, Core ML) |
| ML compiler | ML 컴파일러 | 모델 그래프를 타깃용 코드로 바꾸며 fusion·tiling·memory planning을 하는 도구 (TVM, IREE, XLA, Vela) |
| interpreter | 인터프리터 | op 리스트를 런타임에 하나씩 dispatch하는 실행 방식 |
| AOT | ahead-of-time | 빌드 타임에 미리 컴파일 |
| ExportedProgram | `torch.export` 결과 | ATen op 그래프 + 파라미터 + 입력 signature |
| Core ATen opset | 핵심 ATen op 집합 | 백엔드가 구현할 최소 op 집합 (약 180개) |
| lowering | 하향 변환 | 큰 op → 작은 primitive, 높은 IR → 낮은 IR |
| decomposition | 분해 | PyTorch에서 lowering을 부르는 이름 (`run_decompositions`) |
| delegate / backend | 위임 대상 | 그래프 일부를 받아 가속기에서 실행하는 플러그인 |
| partitioner | 분할기 | 그래프에서 backend가 지원하는 부분을 떼어 내는 단계 |
| `.pte` | ExecuTorch 프로그램 | memory plan과 delegate blob을 담은 flatbuffer |
| `.mlpackage` | Core ML ML Program | MIL 프로그램 + weight.bin 디렉터리 |
| MIL | Model Intermediate Language | coremltools/Core ML의 IR |
| compute units | 허용 엔진 | Core ML이 쓸 수 있는 엔진 집합 (CPU_ONLY 등) |
| ANE | Apple Neural Engine | Apple SoC의 NPU, fp16 중심 |
| MLComputePlan | 실행 계획 API | op별 선호·지원 디바이스와 예상 비용 |
| palettization | 팔레트화 | weight를 2^n개 LUT 값으로 묶고 인덱스만 저장 |
| linear quantization | 선형 양자화 | `w ≈ scale · q` 균일 격자 양자화 |
| SNR | signal-to-noise ratio | 10·log10(신호 에너지 / 오차 에너지), dB |
| MLIR | Multi-Level IR | 여러 수준 IR(dialect)과 변환을 만드는 컴파일러 인프라 |
| dialect | 방언 | MLIR에서 한 수준의 op 집합 (linalg, tosa, stablehlo…) |
| StableHLO | 안정 HLO | 버전 호환이 보장된 XLA 계열 op set·교환 포맷 |
| TOSA | Tensor Operator Set Architecture | Arm 주도의 표준 텐서 op 집합 |
| Relax / TensorIR | TVM IR | TVM의 그래프 IR / 커널 IR |
| MetaSchedule | TVM 튜너 | 실제 기기 측정으로 커널 스케줄을 탐색 |
| HAL (IREE) | 하드웨어 추상화 계층 | 버퍼·dispatch·동기화를 디바이스 드라이버로 추상화 |
| `.vmfb` | IREE 모듈 | IREE VM FlatBuffer |
| EON Compiler | Edge Impulse 컴파일러 | 모델을 interpreter 없는 C++ 코드로 변환 |
| ST Edge AI Core | ST 도구 | STM32Cube.AI의 후속 통합 도구 (`stedgeai`) |
| eIQ | NXP ML 도구 | NXP 칩용 런타임·변환·프로파일 도구 묶음 |
| MediaPipe Tasks | Google 파이프라인 API | 비전·텍스트·오디오 기능을 TFLite 모델로 제공 |
| LiteRT | TFLite 새 이름 | 2024년 개명, `.tflite` 그대로 |

---

## 15. 요약 & 체크리스트

edge ML 배포는 프레임워크 → 교환 포맷 → 컴파일러·런타임 → 하드웨어의 네 층으로 읽으면 정리된다. 교환 포맷(ExportedProgram, ONNX, TFLite, StableHLO)은 "누가 받아 주느냐"로 고르고, 런타임은 interpreter형(TFLite/TFLM, ORT)과 compiler형(TVM, IREE, Vela, EON, QNN context)이 섞여 있다. 컴파일러의 핵심은 lowering(큰 op → primitive, 높은 IR → 낮은 IR)과 fusion·tiling·정적 메모리 계획이고, MLIR은 이 계단을 만드는 인프라다. Core ML 실측에서는 fp16 패키지가 Neural Engine에서 0.54 ms(CPU fp32 5.35 ms)로 돌았지만 fp32 패키지는 NE를 허락해도 CPU 속도였고, `MLComputePlan`이 그 이유(ANE 미지원)를 확인해 줬다. 4-bit palettization은 weight를 1/4로 줄이면서 작은 CNN 정확도를 유지했고 2-bit에서 무너졌다. 런타임 선택은 벤더 1급 경로를 기준선으로 실제 모델을 돌려 op 커버리지·정확도·성능·업데이트 경로·도구·라이선스·지속성을 비교하는 일이며, Hark 같은 웨어러블이라면 엔진마다 다른 런타임(MCU: TFLM, NPU: QNN 계열, LLM: llama.cpp 계열)을 쓰고 검증 하네스를 공통으로 두는 그림이 자연스럽다(추정).

- [ ] edge ML 지형도(4층)를 그리고 Core ML, ExecuTorch, LiteRT, ORT, TVM, IREE, Vela, Cube.AI, Edge Impulse를 제자리에 놓을 수 있다
- [ ] `torch.export`로 ExportedProgram을 만들고 노드·shape·입력 종류(PARAMETER/BUFFER/USER_INPUT)를 읽을 수 있다
- [ ] `run_decompositions()`로 lowering을 보여 주고 `linear → permute + addmm`, LayerNorm → primitive 7개를 설명할 수 있다
- [ ] fusion이 없애는 중간 tensor 트래픽을 손으로 계산할 수 있다 (64×64 fp32 → 64 KB)
- [ ] coremltools로 PyTorch 모델을 mlpackage로 변환하고 BN folding을 op 목록에서 확인할 수 있다
- [ ] compute unit별 latency·오차를 재고, fp32 모델이 ANE에 못 올라가는 이유를 설명할 수 있다
- [ ] "ANE를 썼나?"를 latency → MLComputePlan → Instruments 세 단계로 답할 수 있다
- [ ] palettization과 linear quantization의 크기를 손계산하고 정확도 trade-off를 실측으로 보일 수 있다
- [ ] ExecuTorch 흐름(export → edge → partition → `.pte` → C++ runtime)과 TFLite와의 차이를 말할 수 있다
- [ ] 런타임 선택 기준 10개와 웨어러블 엔진별 후보를 근거와 함께 말할 수 있다

---

## 참고 자료

- PyTorch `torch.export` 문서 — https://pytorch.org/docs/stable/export.html
- PyTorch IR (Core ATen opset) 문서 — https://pytorch.org/docs/stable/torch.compiler_ir.html
- ExecuTorch 문서 — https://pytorch.org/executorch/
- ExecuTorch GitHub — https://github.com/pytorch/executorch
- coremltools 문서 (변환·최적화 가이드) — https://apple.github.io/coremltools/docs-guides/
- Apple Core ML 개발자 문서 — https://developer.apple.com/documentation/coreml
- MediaPipe 문서 — https://ai.google.dev/edge/mediapipe/solutions/guide
- LiteRT 문서 — https://ai.google.dev/edge/litert
- Apache TVM — https://tvm.apache.org/
- MLIR — https://mlir.llvm.org/
- IREE — https://iree.dev/
- OpenXLA / StableHLO — https://openxla.org/
- Edge Impulse 문서 — https://docs.edgeimpulse.com/
- ST Edge AI (STM32Cube.AI) — https://stm32ai.st.com/
- NXP eIQ — https://www.nxp.com/eiq
- Arm Vela (ethos-u-vela) — https://pypi.org/project/ethos-u-vela/
- Chen et al., "TVM: An Automated End-to-End Optimizing Compiler for Deep Learning", OSDI 2018
- Lattner et al., "MLIR: Scaling Compiler Infrastructure for Domain Specific Computation", CGO 2021
- MIT 6.5940 TinyML and Efficient Deep Learning (Song Han) — https://efficientml.ai/
