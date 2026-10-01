# F5. ONNX와 ONNX Runtime — 교환 포맷, opset, export, execution provider

> **이 노트를 다 읽으면**: `.onnx` 파일을 protobuf 구조(ModelProto → GraphProto → Node/Initializer/ValueInfo)로 열어 읽고, `onnx.helper`로 그래프를 직접 만들고 고칠 수 있다 · opset·IR version이 왜 벤더 툴체인 호환성을 좌우하는지 실제 에러로 설명하고, legacy/dynamo 두 exporter의 차이와 흔한 export 실패(데이터 의존 분기, 미지원 op, batch-1 특수화)를 재현·수정할 수 있다 · ONNX Runtime의 InferenceSession·SessionOptions·execution provider·그래프 partitioning을 이해하고, verbose 로그·프로파일링 JSON으로 "어느 노드가 어느 EP에서 얼마나 돌았나"를 숫자로 뽑을 수 있다 · 벤더 NPU 컴파일러에 넘길 "deployable ONNX" 체크리스트를 코드로 점검할 수 있다
> **JD 연결**: "Embedded ML runtimes (TFLite, llama.cpp, QNN)", "Work with platform vendors to bring up toolchains, SDKs and new accelerator", "Deploying workloads on NPUs or specialized accelerators" · study_prep_list F5 행 — PyTorch → ONNX export, opset, execution provider (QNN EP 등), 그래프 확인 도구 (Netron) · "벤더 간 공통 교환 포맷"
> **Don 기준 난이도**: 바이너리 포맷 파싱, 툴체인 버전 매트릭스, 드라이버/백엔드 추상화, "fallback 경로를 찾아 없애는 디버깅"은 이미 강함 / protobuf 스키마로 된 ML 그래프, opset 의미론, PyTorch exporter 내부, ORT의 EP partitioning은 새로 배움
> **선행 노트**: A5 (9절 export 형식 비교), C2 (5절 ORT `quantize_static`·QDQ), C6 (2절 ONNX 그래프 열기, 6절 ORT 최적화 레벨, 12절 CPU fallback), C8 (4.3절 ORT 중간 출력, 2절 비교 지표). 병행 노트: F4 (Qualcomm QNN), F6 (그 외 런타임)

---

## 0. 큰 그림 — 이게 왜 필요한가

PyTorch로 학습한 모델을 기기로 옮길 때, 거의 모든 벤더 툴체인이 공통으로 받아 주는 입력이 하나 있다. **ONNX(Open Neural Network Exchange)** 파일이다. Qualcomm QNN converter, NVIDIA TensorRT, Intel OpenVINO, 많은 NPU 스타트업의 컴파일러가 `.onnx`를 받는다. 그리고 그 ONNX 파일을 PC·폰·임베디드 Linux에서 바로 돌려 보는 레퍼런스 런타임이 **ONNX Runtime(ORT)** 이다.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f5m1" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <rect x="10" y="20" width="170" height="56" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="95" y="44" font-size="13" text-anchor="middle">PyTorch nn.Module</text> <text x="95" y="62" font-size="12" text-anchor="middle">학습 끝난 eval 모델</text> <rect x="250" y="20" width="170" height="56" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<text x="335" y="44" font-size="13" text-anchor="middle">torch.onnx.export</text> <text x="335" y="62" font-size="12" text-anchor="middle">legacy 또는 dynamo (3절)</text> <rect x="490" y="20" width="180" height="56" rx="6" fill="none" stroke="#e08a3c" stroke-width="2.5"/> <text x="580" y="44" font-size="13" text-anchor="middle">model.onnx</text> <text x="580" y="62" font-size="12" text-anchor="middle">protobuf · 표준 op · opset</text> <line x1="180" y1="48" x2="248" y2="48" stroke="currentColor" stroke-width="1.5" marker-end="url(#f5m1)"/>
<line x1="420" y1="48" x2="488" y2="48" stroke="currentColor" stroke-width="1.5" marker-end="url(#f5m1)"/> <rect x="250" y="96" width="170" height="40" rx="6" fill="none" stroke="#888" stroke-width="1.5" stroke-dasharray="5 3"/> <text x="335" y="121" font-size="12" text-anchor="middle">tf2onnx · skl2onnx 등</text> <line x1="420" y1="116" x2="520" y2="78" stroke="currentColor" stroke-width="1.2" marker-end="url(#f5m1)"/> <line x1="560" y1="76" x2="87" y2="178" stroke="currentColor" stroke-width="1.2" marker-end="url(#f5m1)"/>
<line x1="570" y1="76" x2="257" y2="178" stroke="currentColor" stroke-width="1.2" marker-end="url(#f5m1)"/> <line x1="585" y1="76" x2="427" y2="178" stroke="currentColor" stroke-width="1.2" marker-end="url(#f5m1)"/> <line x1="597" y1="76" x2="597" y2="178" stroke="currentColor" stroke-width="1.2" marker-end="url(#f5m1)"/> <rect x="10" y="180" width="155" height="64" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="87" y="202" font-size="13" text-anchor="middle">ONNX Runtime</text> <text x="87" y="219" font-size="12" text-anchor="middle">EP: CPU · CoreML</text>
<text x="87" y="235" font-size="12" text-anchor="middle">QNN · XNNPACK · NNAPI</text> <rect x="180" y="180" width="155" height="64" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="257" y="202" font-size="13" text-anchor="middle">Qualcomm QNN</text> <text x="257" y="219" font-size="12" text-anchor="middle">converter → HTP</text> <text x="257" y="235" font-size="12" text-anchor="middle">context binary (F4)</text> <rect x="350" y="180" width="155" height="64" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<text x="427" y="202" font-size="13" text-anchor="middle">TensorRT · OpenVINO</text> <text x="427" y="219" font-size="12" text-anchor="middle">GPU · Intel NPU</text> <text x="427" y="235" font-size="12" text-anchor="middle">벤더 NPU 컴파일러</text> <rect x="520" y="180" width="150" height="64" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="595" y="202" font-size="13" text-anchor="middle">변환 도구 경유</text> <text x="595" y="219" font-size="12" text-anchor="middle">→ TFLite (F1)</text> <text x="595" y="235" font-size="12" text-anchor="middle">→ 기타 포맷 (F6)</text>
<text x="10" y="272" font-size="12">파랑: 모델을 만드는 쪽 · 주황: 교환 파일 · 초록: 그 파일을 받아 기기에서 돌리는 쪽 (F4/F6/F8에서 각각 자세히)</text>
</svg>
```

그림 1 — ONNX는 "만드는 쪽"과 "돌리는 쪽" 사이의 교환 포맷이다. 한쪽에 프레임워크가 여럿, 다른 쪽에 런타임·컴파일러가 여럿 있을 때, N×M개의 변환기 대신 N+M개만 있으면 된다.

Don에게 가까운 비유는 두 가지다.

| Don이 아는 것 | ONNX 세계 |
|---|---|
| EDIF·Verilog 넷리스트 — 합성 툴이 만들고 P&R 툴이 받는 교환 형식 | ONNX 그래프 — exporter가 만들고 런타임·NPU 컴파일러가 받는 교환 형식 |
| 셀 라이브러리 버전 (같은 게이트 이름이라도 라이브러리 버전마다 핀·타이밍이 다름) | **opset** 버전 (같은 op 이름이라도 opset마다 입력·속성·의미가 다름, 4절) |
| ELF 파일 — 섹션 헤더 + 코드 + 데이터, 로더가 읽는 바이너리 규격 | `.onnx` — protobuf로 직렬화된 그래프 + 가중치(initializer), ORT가 읽는 규격 |
| HAL + 드라이버 백엔드 (같은 API, SoC별 구현) | ORT의 **execution provider** (같은 `session.run()`, 하드웨어별 구현, 6절) |
| 하드웨어 가속기가 못 하는 명령을 소프트웨어 에뮬레이션으로 처리 | EP가 못 받는 노드를 **CPU EP로 fallback** (6절, C6 12절) |

순서: 1절 파일 해부(protobuf 바이트까지) → 2절 손으로 만들고 고치기 → 3절 PyTorch export와 실패 사례 → 4절 opset·IR version → 5–7절 ORT 구조, EP partitioning(실제 CoreML EP 로그), IOBinding, 프로파일링 → 8절 PyTorch vs ORT 수치 → 9절 양자화 복습, `.ort`, minimal build, genai → 10절 deployable ONNX 체크리스트와 lint 스크립트.

이미 앞 노트에서 ONNX를 많이 썼다. 겹치는 부분은 짧게 복습하고 ID로 가리킨다: export 형식 비교(A5 9절), ORT `quantize_static`과 QDQ 해부(C2 5절), 노드·initializer 나열과 exporter `optimize=True`(C6 2절), ORT 최적화 레벨 4개와 `optimized_model_filepath`(C6 6절), 중간 텐서를 그래프 출력으로 승격(C8 4.3절). F5는 이것들을 "ONNX라는 포맷과 ORT라는 런타임" 관점에서 체계적으로 다시 묶는 레퍼런스다.

> 실행 환경: 모든 Python 예제는 이 폴더의 `.venv/bin/python` (Python 3.9.6, torch 2.8.0, onnx 1.19.1, onnxscript 0.7.2, onnxruntime 1.19.2, numpy 2.0.2, macOS arm64, 8코어)로 실제 실행한 출력이다. 예제는 `.onnx` 파일을 현재 디렉터리에 쓰므로 **노트 폴더가 아닌 임시 디렉터리**(예: `mkdir -p /tmp/f5 && cd /tmp/f5`)에서 순서대로 실행한다(앞 예제가 만든 파일을 뒤 예제가 읽는다). `torch.onnx.export`가 찍는 `[torch.onnx] ...` 진행 메시지, "torchvision is not installed" 경고, ORT의 색깔 로그는 생략했다. **시간 측정값은 다른 작업이 함께 돌던 노트북(load average ≈ 20)에서 잰 것이라 노이즈가 크다** — 절대값이 아니라 비율과 방향만 읽는다.

---

## 1. ONNX는 스펙이다 — `.onnx` 파일 안에 무엇이 있나

### 1.1 직관

ONNX는 라이브러리 이름이 아니라 **파일 형식 규격 + 연산자(op) 사전**이다. 규격은 세 가지를 정한다.

1. **직렬화 형식**: Google protobuf. 스키마 파일(`onnx.proto`)에 메시지 타입이 정의돼 있고, `.onnx` 파일은 `ModelProto` 메시지 하나를 바이트로 쓴 것이다.
2. **그래프 의미론**: 노드(op)와 텐서 이름으로 연결된 DAG(방향 비순환 그래프). 노드는 위상 정렬(topological order)된 순서로 저장된다.
3. **연산자 사전(opset)**: `Conv`, `Gemm`, `Relu` 같은 op의 입력·출력·속성·수학적 의미. 사전에는 버전이 있다(4절).

말로 하면: ONNX 파일은 "넷리스트 + 가중치 ROM 이미지"를 protobuf로 포장한 것이고, 각 게이트(op)의 의미는 버전 붙은 셀 라이브러리(opset)를 참조한다.

### 1.2 구조 — 메시지 네 개만 알면 된다

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<rect x="10" y="10" width="660" height="380" rx="8" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="22" y="32" font-size="14">ModelProto (파일 전체)</text> <text x="22" y="58" font-size="12">ir_version (1) = 10</text> <text x="22" y="78" font-size="12">opset_import (8)</text> <text x="34" y="96" font-size="12">= [("", 18)]</text> <text x="22" y="116" font-size="12">producer_name (2)</text> <text x="34" y="134" font-size="12">= "pytorch"</text> <text x="22" y="154" font-size="12">producer_version (3)</text> <text x="22" y="174" font-size="12">metadata_props (14)</text>
<text x="22" y="194" font-size="12">functions (25)</text> <text x="22" y="226" font-size="12">괄호 = protobuf</text> <text x="22" y="244" font-size="12">필드 번호 (1.4절)</text> <rect x="180" y="40" width="478" height="338" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="192" y="60" font-size="13">graph (7): GraphProto</text> <rect x="192" y="70" width="220" height="44" rx="4" fill="none" stroke="#888"/> <text x="200" y="88" font-size="12">input (11): ValueInfoProto</text> <text x="200" y="105" font-size="12">imu: float[1,6,50]</text>
<rect x="426" y="70" width="220" height="44" rx="4" fill="none" stroke="#888"/> <text x="434" y="88" font-size="12">output (12): ValueInfoProto</text> <text x="434" y="105" font-size="12">logits: float[1,4]</text> <rect x="192" y="124" width="220" height="62" rx="4" fill="none" stroke="#888"/> <text x="200" y="142" font-size="12">initializer (5): TensorProto</text> <text x="200" y="159" font-size="12">conv1.weight [16,6,5] FLOAT</text> <text x="200" y="176" font-size="12">raw_data 또는 external data</text> <rect x="426" y="124" width="220" height="62" rx="4" fill="none" stroke="#888"/>
<text x="434" y="142" font-size="12">value_info (13)</text> <text x="434" y="159" font-size="12">중간 텐서의 dtype·shape</text> <text x="434" y="176" font-size="12">(선택, shape inference가 채움)</text> <rect x="192" y="198" width="454" height="168" rx="4" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="200" y="218" font-size="13">node (1): NodeProto 리스트 — 위상 정렬 순서</text> <rect x="206" y="230" width="200" height="124" rx="4" fill="none" stroke="#3f9a6b"/> <text x="214" y="249" font-size="12">op_type (4) = "Conv"</text> <text x="214" y="267" font-size="12">domain (7) = "" (ai.onnx)</text>
<text x="214" y="285" font-size="12">input (1) = [imu,</text> <text x="226" y="302" font-size="12">conv1.weight, conv1.bias]</text> <text x="214" y="320" font-size="12">output (2) = [getitem]</text> <text x="214" y="338" font-size="12">attribute (5) = pads, strides…</text> <text x="424" y="252" font-size="12">노드끼리는 포인터가 아니라</text> <text x="424" y="270" font-size="12">텐서 "이름"으로 연결된다.</text> <text x="424" y="296" font-size="12">Conv의 output "getitem"이</text> <text x="424" y="314" font-size="12">Relu의 input "getitem"이면</text> <text x="424" y="332" font-size="12">둘 사이에 엣지가 있다.</text>
</svg>
```

그림 2 — ONNX 파일의 메시지 구조. 실제 값은 예제 1의 `imunet.onnx`에서 가져왔다. 괄호 안 숫자는 `onnx.proto`의 protobuf 필드 번호다(`onnx.ModelProto.DESCRIPTOR`로 확인한 값).

| 메시지 | 역할 | 펌웨어 비유 |
|---|---|---|
| `ModelProto` | 파일 전체. IR version, opset 선언, 생산자 정보, 메타데이터, 그래프 | ELF 헤더 + 섹션 테이블 |
| `GraphProto` | 노드 리스트, 그래프 입력·출력, initializer(가중치), value_info | 넷리스트 본체 |
| `NodeProto` | op 하나: `op_type`, `domain`, 입력·출력 텐서 이름, 속성(attribute) | 게이트 인스턴스 하나 |
| `TensorProto` | 상수 텐서: dims, data_type, 데이터(`raw_data` 또는 외부 파일 참조) | `.rodata`의 const 배열 |
| `ValueInfoProto` | 텐서 이름 + 타입(dtype, shape). shape 칸에는 숫자(`dim_value`) 또는 기호(`dim_param`, 예: `"batch"`) | 포트 선언 (폭 고정 또는 parameter) |

알아 둘 규칙 세 가지.

- **엣지는 이름이다.** NodeProto에는 "다음 노드" 포인터가 없다. 출력 이름과 입력 이름이 같으면 연결된 것이다. 그래서 그래프 수술(2.3절)에서 이름 하나를 바꾸면 그 이름을 쓰는 모든 곳을 같이 바꿔야 한다 — 링커 심볼 rename과 같다.
- **initializer는 "이름이 붙은 상수"다.** 노드 입력 이름이 initializer 이름과 같으면 그 입력은 상수다. 가중치뿐 아니라 shape 벡터, 축 번호, √2 같은 스칼라도 initializer로 들어간다(예제 1).
- **domain이 op의 네임스페이스다.** 빈 문자열 `""`(= `ai.onnx`)이 표준 op, `ai.onnx.ml`은 전통 ML op(트리, SVM 등), `com.microsoft`는 ORT 전용 contrib op(`FusedConv`, `QLinearConv` 일부 변형, attention 계열 등). **표준이 아닌 domain의 op는 다른 런타임·벤더 컴파일러가 모를 가능성이 높다** — C6 6.1절에서 ORT_ENABLE_EXTENDED가 만든 `FusedConv`(com.microsoft)가 그 예다.

### 1.3 이 노트의 공용 모델과 예제 1 — 파일 열어 보기

공용 모델은 합성 IMU 윈도우(6채널 × 50샘플)를 4개 제스처로 분류하는 작은 1D CNN이다. 예를 들어 Hark 같은 웨어러블이라면 손목 IMU로 "탭/스와이프/흔들기/정지"를 구분하는 모델을 상상하면 된다(가상의 예). 일부러 GELU를 하나 넣었다 — 이 op가 opset·EP 지원에서 문제를 일으키는 모습을 보여 주기 위해서다.

```python
# f5_model.py — F5 공용 모델: 합성 IMU 윈도우(6채널 x 50샘플) → 4-class 1D CNN
import torch, torch.nn as nn
class IMUNet(nn.Module):
    def __init__(self):
        super().__init__()
        self.conv1 = nn.Conv1d(6, 16, 5, padding=2); self.bn1 = nn.BatchNorm1d(16)
        self.conv2 = nn.Conv1d(16, 32, 3, padding=1); self.bn2 = nn.BatchNorm1d(32)
        self.pool = nn.AdaptiveAvgPool1d(1); self.fc = nn.Linear(32, 4)
    def forward(self, x):                       # x: [N, 6, 50]
        x = torch.relu(self.bn1(self.conv1(x)))
        x = nn.functional.gelu(self.bn2(self.conv2(x)))
        return self.fc(self.pool(x).flatten(1)) # [N, 4] logits
def make():
    torch.manual_seed(0); m = IMUNet()
    for bn in (m.bn1, m.bn2):                   # BN 통계를 기본값(0,1)이 아닌 값으로
        bn.running_mean.uniform_(-0.5, 0.5); bn.running_var.uniform_(0.5, 2.0)
    return m.eval()
```

무엇을 확인하는 코드인지: dynamo exporter로 `imunet.onnx`를 만들고, 그림 2의 각 칸(ir_version, opset_import, 입력·출력, initializer, value_info, node)을 실제로 꺼내 본다.

```python
import torch, onnx, os
from f5_model import make
m = make(); x = torch.randn(1, 6, 50)
torch.onnx.export(m, (x,), "imunet.onnx", input_names=["imu"], output_names=["logits"],
                  dynamo=True, external_data=False, verbose=False)
mp = onnx.load("imunet.onnx")
print("file bytes:", os.path.getsize("imunet.onnx"), "| ir_version:", mp.ir_version,
      "| producer:", mp.producer_name, mp.producer_version)
print("opset_import:", [(o.domain or "ai.onnx", o.version) for o in mp.opset_import])
g = mp.graph
print("inputs :", [(i.name, [d.dim_value or d.dim_param for d in i.type.tensor_type.shape.dim]) for i in g.input])
print("outputs:", [(o.name, [d.dim_value or d.dim_param for d in o.type.tensor_type.shape.dim]) for o in g.output])
print("initializers:", len(g.initializer), "| params:", sum(int(torch.tensor(t.dims).prod()) for t in g.initializer))
for t in g.initializer[:3]: print("  init", t.name, list(t.dims), onnx.TensorProto.DataType.Name(t.data_type))
print("value_info:", len(g.value_info))
for n in g.node: print(f"  {n.op_type:<10} in={list(n.input)} out={list(n.output)}")
```

```text
file bytes: 21468 | ir_version: 10 | producer: pytorch 2.8.0
opset_import: [('ai.onnx', 18)]
inputs : [('imu', [1, 6, 50])]
outputs: [('logits', [1, 4])]
initializers: 12 | params: 2204
  init conv1.weight [16, 6, 5] FLOAT
  init conv1.bias [16] FLOAT
  init conv2.weight [32, 16, 3] FLOAT
value_info: 24
  Conv       in=['imu', 'conv1.weight', 'conv1.bias'] out=['getitem']
  Relu       in=['getitem'] out=['relu']
  Conv       in=['relu', 'conv2.weight', 'conv2.bias'] out=['getitem_3']
  Div        in=['getitem_3', 'val_14'] out=['val_15']
  Erf        in=['val_15'] out=['val_16']
  Add        in=['val_16', 'val_17'] out=['val_18']
  Mul        in=['val_19', 'val_18'] out=['val_20']
  Mul        in=['getitem_3', 'val_20'] out=['gelu']
  Unsqueeze  in=['gelu', 'val_21'] out=['unsqueeze']
  ReduceMean in=['unsqueeze', 'val_24'] out=['mean']
  Squeeze    in=['mean', 'val_21'] out=['squeeze']
  Reshape    in=['squeeze', 'val_28'] out=['view']
  Gemm       in=['view', 'fc.weight', 'fc.bias'] out=['logits']
```

출력에서 볼 것:

- **BatchNormalization 노드가 없다.** torch 2.8의 dynamo exporter는 기본값 `optimize=True`로 onnxscript optimizer를 돌려 BN을 앞 Conv의 weight·bias에 접었다(B1의 BN folding, C6 2.2절). 그래서 `conv1.weight`는 이미 BN이 섞인 값이다.
- **GELU가 5개 노드(Div, Erf, Add, Mul, Mul)로 풀렸다.** 식으로 `gelu(x) = x · 0.5 · (1 + erf(x / √2))`. opset 18에는 `Gelu` op가 없어서 exporter가 기본 op로 분해했다(opset 20부터 `Gelu`가 생긴다 — 3.2절에서 확인).
- `AdaptiveAvgPool1d(1)`이 `Unsqueeze → ReduceMean → Squeeze`로, `flatten`이 `Reshape`으로 lowering 됐다. `Reshape`의 shape 입력 `val_28`은 상수 `[1, 32]`다 — **batch가 1로 박혔다**는 뜻이고, 3.3절에서 바로 문제가 된다.

### 1.4 손계산 — 파라미터 수와 protobuf 첫 바이트

**파라미터 수.** 출력의 `params: 2204`를 손으로 맞춰 본다.

```
conv1.weight 16·6·5 = 480     conv1.bias 16
conv2.weight 32·16·3 = 1536   conv2.bias 32
fc.weight    4·32 = 128       fc.bias    4
                              합계 = 2196
상수 initializer: val_24 [-1,-2] (2) + val_28 [1,32] (2) + val_14 √2 (1)
                + val_17 1.0 (1) + val_19 0.5 (1) + val_21 [-2] (1) = 8
2196 + 8 = 2204 ✓
```

말로 하면: initializer 개수에는 가중치뿐 아니라 exporter가 만든 작은 상수(축 번호, shape, GELU의 √2·0.5·1.0)도 들어간다. float32 가중치 2196개 = 8,784바이트인데 파일은 21,468바이트다. 나머지는 텐서 이름, value_info 24개, torch exporter가 넣은 메타데이터(`pkg.torch...` 키들) 같은 문자열이다. 작은 모델일수록 "포장"이 차지하는 비율이 크다.

**protobuf 첫 바이트 읽기.** 9.3절 예제에서 `imunet.onnx`의 첫 8바이트를 찍으면 `b'\x08\n\x12\x07pyto'`가 나온다. protobuf의 각 필드는 `tag = (필드번호 << 3) | wire_type` 바이트로 시작한다.

```
0x08 = 0000 1000 → 필드 1, wire type 0 (varint)       → ModelProto.ir_version
0x0A = 10                                            → ir_version = 10
0x12 = 0001 0010 → 필드 2, wire type 2 (길이+바이트)   → ModelProto.producer_name
0x07 = 길이 7 → 다음 7바이트 "pytorch"
```

Don이 SSD 시절 NVMe 로그 페이지를 hex dump로 읽던 것과 똑같다. 규격(스키마)이 있으면 바이너리는 사람이 읽을 수 있다. 실무에서 손으로 읽을 일은 드물지만, "파일이 깨졌나? 다른 포맷인가?"를 첫 바이트로 가르는 데는 유용하다(9.3절의 `.ort` 파일은 첫 바이트가 전혀 다르다).

### 1.5 2GB 한계와 external data

protobuf 메시지 하나는 2GB를 넘을 수 없다. 그래서 큰 모델(LLM 등)은 가중치를 `.onnx` 밖의 파일로 빼고, TensorProto에는 "어느 파일의 몇 바이트 오프셋에 있다"는 참조만 남긴다(`data_location = EXTERNAL`). torch 2.8의 `torch.onnx.export`는 `external_data=True`가 **기본값**이라 작은 모델도 `model.onnx.data`를 따로 만든다(A5 9.2절에서 처음 봤다). 그래서 이 노트의 예제는 `external_data=False`를 명시한다.

함정: `.onnx`만 복사하고 `.onnx.data`를 빠뜨리면 로드가 실패한다. 더 교묘한 문제도 있다 — 6.4절에서 **external data 모델은 CoreML EP가 가중치를 못 읽어서 Conv가 전부 CPU로 떨어지는** 실측을 본다.

---

## 2. 그래프를 손으로 만들고 고치기 — `onnx.helper`, checker, shape inference

### 2.1 왜 손으로 만들어 보나

벤더 bring-up에서 가장 자주 하는 일은 "이 op 하나가 이 툴에서 되나?"를 확인하는 것이다. 큰 모델을 export해서 넘기면 실패 원인이 여러 겹이다. **op 하나짜리 최소 ONNX**를 직접 만들어 넘기면 원인을 하나로 좁힐 수 있다 — 벤더 FAE에게 버그를 보고할 때 붙이는 재현 케이스(minimal repro)도 이렇게 만든다. C6 3.3절에서 constant folding 실험용 그래프를 이렇게 만들었다.

### 2.2 예제 2 — 작은 MLP를 손으로 만들고 검증

무엇을 확인하는 코드인지: 노드 4개짜리 MLP(`MatMul → Add → Relu → Gemm`)를 `onnx.helper`로 만들고, `checker`로 규격 검사, `shape_inference`로 중간 shape 추론, ORT로 실행해 numpy와 비교한다. 그리고 **onnx 1.19가 기본으로 쓰는 IR version을 ORT 1.19가 못 읽는** 실제 버전 불일치를 본다.

```python
import numpy as np, onnx, onnxruntime as ort
from onnx import helper as h, TensorProto as T, numpy_helper as nh
rng = np.random.default_rng(0)
W1 = rng.standard_normal((6, 8)).astype(np.float32); b1 = np.zeros(8, np.float32)
W2 = rng.standard_normal((8, 4)).astype(np.float32); b2 = np.zeros(4, np.float32)
nodes = [h.make_node("MatMul", ["x", "W1"], ["mm1"], name="fc1_mm"),
         h.make_node("Add", ["mm1", "b1"], ["h1"], name="fc1_add"),
         h.make_node("Relu", ["h1"], ["a1"], name="relu1"),
         h.make_node("Gemm", ["a1", "W2", "b2"], ["y"], name="fc2")]
graph = h.make_graph(nodes, "tiny_mlp",
    inputs=[h.make_tensor_value_info("x", T.FLOAT, ["N", 6])],
    outputs=[h.make_tensor_value_info("y", T.FLOAT, ["N", 4])],
    initializer=[nh.from_array(a, n) for a, n in [(W1, "W1"), (b1, "b1"), (W2, "W2"), (b2, "b2")]])
model = h.make_model(graph, opset_imports=[h.make_opsetid("", 17)], producer_name="don-handmade")
print("default ir_version:", model.ir_version)
onnx.checker.check_model(model, full_check=True); print("checker: OK")
inf = onnx.shape_inference.infer_shapes(model)
for vi in inf.graph.value_info:
    print("  inferred", vi.name, [d.dim_value or d.dim_param for d in vi.type.tensor_type.shape.dim])
onnx.save(model, "mlp_ir12.onnx")
try: ort.InferenceSession("mlp_ir12.onnx", providers=["CPUExecutionProvider"])
except Exception as e: print("ORT 1.19: Unsupported" + str(e).split("Unsupported")[1].rstrip())
model.ir_version = 10; onnx.save(model, "mlp.onnx")
x = rng.standard_normal((2, 6)).astype(np.float32)
y = ort.InferenceSession("mlp.onnx", providers=["CPUExecutionProvider"]).run(None, {"x": x})[0]
print("ORT vs numpy:", np.abs(y - (np.maximum(x @ W1 + b1, 0) @ W2 + b2)).max())
print(onnx.printer.to_text(model.graph).split("\n")[0])
print("\n".join(onnx.printer.to_text(model.graph).split("\n")[-6:]))
```

```text
default ir_version: 12
checker: OK
  inferred mm1 ['N', 8]
  inferred h1 ['N', 8]
  inferred a1 ['N', 8]
ORT 1.19: Unsupported model IR version: 12, max supported IR version: 10
ORT vs numpy: 0.0
tiny_mlp (float[N,6] x) => (float[N,4] y) 
{
   [fc1_mm] mm1 = MatMul (x, W1)
   [fc1_add] h1 = Add (mm1, b1)
   [relu1] a1 = Relu (h1)
   [fc2] y = Gemm (a1, W2, b2)
}
```

출력에서 볼 것:

- **checker는 통과했는데 ORT가 거부했다.** `onnx` 패키지 1.19는 IR version 12로 저장하고, ORT 1.19는 IR 10까지만 읽는다. checker는 "ONNX 규격에 맞나"를 보지 "이 런타임이 읽을 수 있나"를 보지 않는다. 툴체인 버전 매트릭스 문제의 가장 작은 예다(4절). 해결은 `ir_version`을 런타임이 아는 값으로 낮추는 것(`make_model(..., ir_version=10)`도 된다).
- shape inference가 `mm1`, `h1`, `a1`의 shape를 `['N', 8]`로 채웠다. 기호 차원 `N`이 그대로 전파된다. NPU 컴파일러는 이런 기호 차원을 싫어한다(3.3절, C6 9절).
- `onnx.printer.to_text`는 그래프를 사람이 읽는 텍스트로 찍는다(`[노드이름] 출력 = Op (입력)`). 가운데의 initializer 줄(가중치 숫자 48개 등)은 길어서 첫 줄과 마지막 6줄만 출력했다. 바이너리 diff 대신 이 텍스트를 diff하면 두 그래프 비교가 쉽다.
- 가중치를 그대로 쓰니 ORT와 numpy가 비트 단위로 같다(0.0). 작은 행렬이라 합산 순서 차이도 없었다.

### 2.3 예제 3 — 그래프 수술: 이름 바꾸기, 출력 추가, op 교체

무엇을 확인하는 코드인지: 예제 1의 `imunet.onnx`에 세 가지 수술을 한다. (1) 입력 이름 `imu` → `imu_window`, (2) 중간 텐서 `relu`를 그래프 출력으로 추가(C8 4.3절의 레이어별 diff용), (3) `Gemm`을 `MatMul + Add`로 교체(어떤 NPU 툴이 Gemm의 transB 형태를 못 받는다고 가정). 수술 후에도 logits가 같아야 한다.

```python
import numpy as np, onnx, onnxruntime as ort
from onnx import helper as h, numpy_helper as nh
m = onnx.load("imunet.onnx"); g = m.graph
# (1) 입력 이름 바꾸기: graph.input과 그 이름을 쓰는 모든 node.input을 같이 바꾼다
for n in g.node:
    n.input[:] = ["imu_window" if i == "imu" else i for i in n.input]
g.input[0].name = "imu_window"
# (2) 중간 텐서 'relu'를 그래프 출력으로 승격 (C8 4.3절과 같은 기법)
g.output.append([v for v in g.value_info if v.name == "relu"][0])   # shape 정보가 있는 value_info를 복사
# (3) Gemm(transB=1) → MatMul(W^T) + Add 로 교체
i = [k for k, n in enumerate(g.node) if n.op_type == "Gemm"][0]; gemm = g.node[i]
attrs = {a.name: h.get_attribute_value(a) for a in gemm.attribute}
print("Gemm attrs:", attrs)
inits = {t.name: t for t in g.initializer}
W = nh.to_array(inits[gemm.input[1]]); Wt = W.T.copy() if attrs.get("transB", 0) else W
g.initializer.append(nh.from_array(Wt, "fc.weight_T"))
new = [h.make_node("MatMul", [gemm.input[0], "fc.weight_T"], ["fc_mm"], name="fc_matmul"),
       h.make_node("Add", ["fc_mm", gemm.input[2]], [gemm.output[0]], name="fc_bias")]
nodes = list(g.node); nodes[i:i + 1] = new        # protobuf repeated 필드는 slice 대입 불가
del g.node[:]; g.node.extend(nodes)
g.initializer.remove(inits[gemm.input[1]])             # 안 쓰는 원래 W 제거
onnx.checker.check_model(m); onnx.save(m, "imunet_edit.onnx")
x = np.random.default_rng(0).standard_normal((1, 6, 50)).astype(np.float32)
a = ort.InferenceSession("imunet.onnx", providers=["CPUExecutionProvider"]).run(None, {"imu": x})
b = ort.InferenceSession("imunet_edit.onnx", providers=["CPUExecutionProvider"]).run(None, {"imu_window": x})
print("outputs:", [o.name for o in m.graph.output], "| relu shape:", b[1].shape)
print("ops:", [n.op_type for n in m.graph.node][-4:], "| logits diff:", np.abs(a[0] - b[0]).max())
```

```text
Gemm attrs: {'beta': 1.0, 'transB': 1, 'alpha': 1.0, 'transA': 0}
outputs: ['logits', 'relu'] | relu shape: (1, 16, 50)
ops: ['Squeeze', 'Reshape', 'MatMul', 'Add'] | logits diff: 0.0
```

출력에서 볼 것:

- `Gemm`의 속성 `transB=1`: `Y = alpha · A · Bᵀ + beta · C`. PyTorch `nn.Linear`의 weight가 `[out, in]` 모양이라 exporter가 전치 플래그로 처리했다. MatMul로 바꿀 때는 **가중치를 미리 전치해서** 새 initializer로 넣어야 한다(`[4,32]` → `[32,4]`). 이걸 빼먹으면 shape 에러가 나거나, 정사각 행렬이면 **에러 없이 틀린 답**이 나온다.
- 수술 후 logits 차이 0.0 — 같은 수학이다. 수술 뒤에는 반드시 원본과 비교한다.
- 실제로 만난 함정 두 개(코드를 고치기 전 실행에서 나온 에러):
  - `g.node[i:i] = new` 같은 slice 대입은 protobuf repeated 필드에서 `TypeError: does not support assignment`가 난다. 파이썬 리스트로 꺼내서 고친 뒤 `del` + `extend`로 다시 넣는다.
  - 출력을 `make_tensor_value_info("relu", FLOAT, None)`(shape 없음)로 추가하면 checker가 `Field 'shape' of 'type' is required but missing`으로 거부했다. 그래서 `value_info`에 이미 있는 shape 정보를 복사했다.

수술 도구 선택지: 이 예제처럼 `onnx.helper`로 직접 하거나, `onnx-graphsurgeon`(NVIDIA), onnxscript의 rewriter 같은 도구를 쓴다. 원리는 같다 — 노드 리스트, 이름 연결, initializer를 일관되게 바꾸는 것. 그래프를 눈으로 볼 때는 **Netron**(netron.app, 데스크톱 앱도 있다)에 `.onnx`를 끌어다 놓으면 노드·속성·initializer shape를 클릭해서 볼 수 있다. 벤더와 이슈를 주고받을 때 "Netron 스크린샷 + 노드 이름"이 공용어다.

---

## 3. PyTorch에서 ONNX로 — export의 두 경로와 실패 사례

### 3.1 두 exporter

torch 2.8에는 `torch.onnx.export` 하나에 두 개의 엔진이 들어 있다. 인자 `dynamo`가 고른다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f5m4" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="10" y="22" font-size="13">legacy (dynamo=False, torch 2.8의 기본값) — TorchScript 기반</text> <rect x="10" y="34" width="120" height="50" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="70" y="56" font-size="12" text-anchor="middle">nn.Module</text> <text x="70" y="73" font-size="12" text-anchor="middle">예제 입력 1개</text>
<rect x="165" y="34" width="150" height="50" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="240" y="56" font-size="12" text-anchor="middle">torch.jit.trace</text> <text x="240" y="73" font-size="12" text-anchor="middle">한 번 실행하며 기록</text> <rect x="350" y="34" width="150" height="50" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="425" y="56" font-size="12" text-anchor="middle">symbolic 함수표</text> <text x="425" y="73" font-size="12" text-anchor="middle">aten op → ONNX op</text>
<rect x="535" y="34" width="135" height="50" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="602" y="56" font-size="12" text-anchor="middle">ONNX</text> <text x="602" y="73" font-size="12" text-anchor="middle">opset 7–20</text> <line x1="130" y1="59" x2="163" y2="59" stroke="currentColor" stroke-width="1.5" marker-end="url(#f5m4)"/> <line x1="315" y1="59" x2="348" y2="59" stroke="currentColor" stroke-width="1.5" marker-end="url(#f5m4)"/> <line x1="500" y1="59" x2="533" y2="59" stroke="currentColor" stroke-width="1.5" marker-end="url(#f5m4)"/>
<text x="165" y="104" font-size="12">✗ Python if는 기록 안 됨 → 분기 고정</text> <text x="350" y="104" font-size="12">✗ 표에 없는 op → UnsupportedOperatorError</text> <text x="10" y="150" font-size="13">dynamo (dynamo=True) — torch.export 기반</text> <rect x="10" y="162" width="120" height="50" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="70" y="184" font-size="12" text-anchor="middle">nn.Module</text> <text x="70" y="201" font-size="12" text-anchor="middle">+ dynamic_shapes</text> <rect x="150" y="162" width="130" height="50" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<text x="215" y="184" font-size="12" text-anchor="middle">torch.export</text> <text x="215" y="201" font-size="12" text-anchor="middle">FX graph · guard</text> <rect x="300" y="162" width="110" height="50" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="355" y="184" font-size="12" text-anchor="middle">decomposition</text> <text x="355" y="201" font-size="12" text-anchor="middle">ATen → core</text> <rect x="430" y="162" width="110" height="50" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="485" y="184" font-size="12" text-anchor="middle">torchlib</text>
<text x="485" y="201" font-size="12" text-anchor="middle">(onnxscript)</text> <rect x="560" y="162" width="110" height="50" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="615" y="184" font-size="12" text-anchor="middle">optimizer</text> <text x="615" y="201" font-size="12" text-anchor="middle">fold · BN 접기</text> <line x1="130" y1="187" x2="148" y2="187" stroke="currentColor" stroke-width="1.5" marker-end="url(#f5m4)"/> <line x1="280" y1="187" x2="298" y2="187" stroke="currentColor" stroke-width="1.5" marker-end="url(#f5m4)"/>
<line x1="410" y1="187" x2="428" y2="187" stroke="currentColor" stroke-width="1.5" marker-end="url(#f5m4)"/> <line x1="540" y1="187" x2="558" y2="187" stroke="currentColor" stroke-width="1.5" marker-end="url(#f5m4)"/> <text x="150" y="232" font-size="12">✗ 데이터 의존 분기 → GuardOnDataDependentSymNode</text> <text x="150" y="250" font-size="12">  → torch 2.8은 draft export로 재시도해 분기를 고정 (경고만)</text> <text x="150" y="268" font-size="12">✗ 크기 1인 차원은 static으로 특수화될 수 있다 (3.3절)</text> <text x="10" y="292" font-size="12">공통: export는 "예제 입력으로 한 번 따라가 본 경로"를 그래프로 굳히는 일이다.</text>
</svg>
```

그림 3 — 두 export 경로와 각자 실패하는 지점. opset 범위는 torch 2.8의 상수(`ONNX_TORCHSCRIPT_EXPORTER_MAX_OPSET = 20`, `ONNX_DEFAULT_OPSET = 18`)와 이 환경의 실측(4.3절)에서 가져왔다.

| | legacy (`dynamo=False`) | dynamo (`dynamo=True`) |
|---|---|---|
| 그래프 포착 | `torch.jit.trace`: 예제 입력으로 한 번 실행하며 텐서 연산을 기록 | `torch.export`: Python 바이트코드를 분석해 FX graph + guard(가정 조건) 생성 |
| op 변환 | op별 "symbolic 함수" 표 (opset별 구현) | ATen decomposition → torchlib(onnxscript로 쓴 변환 함수) |
| 후처리 | `do_constant_folding=True` 기본, eval 모드 BN 접기 | onnxscript optimizer (`optimize=True` 기본): constant folding, BN 접기, 불필요 노드 제거 |
| dynamic shape 지정 | `dynamic_axes={"imu": {0: "batch"}}` | `dynamic_shapes={"x": {0: Dim("batch")}}` (forward 인자 이름 기준) |
| 상태 (torch 2.8) | 기본값이지만 유지보수 모드. PyTorch 문서는 dynamo 경로를 권장 | 권장 경로. 향후 기본값이 될 예정이라고 안내됨(버전별 확인) |

말로 하면: 두 경로 모두 "예제 입력으로 모델을 한 번 따라가며 경로를 기록"하는 일이다. 차이는 legacy가 **실행된 텐서 연산만** 보고, dynamo는 **코드를 분석해서 가정(guard)을 명시적으로 관리**한다는 점이다. 그래서 dynamo는 "이 분기는 데이터에 따라 바뀐다"를 알아챌 수 있다(3.4절).

### 3.2 예제 5 — 같은 모델, 네 가지 export 결과 비교

무엇을 확인하는 코드인지: 공용 모델을 legacy(opset 17), dynamo(opset 18), dynamo 최적화 끔, dynamo(opset 20)로 export하고 노드 종류·개수와 PyTorch 대비 오차를 비교한다.

```python
import torch, onnx, collections, numpy as np, onnxruntime as ort, warnings
from f5_model import make
warnings.filterwarnings("ignore")
m = make(); x = torch.randn(1, 6, 50); ref = m(x).detach().numpy()
cfg = {"legacy_op17":   dict(dynamo=False, opset_version=17),
       "dynamo_op18":   dict(dynamo=True,  opset_version=18),
       "dynamo_op18_noopt": dict(dynamo=True, opset_version=18, optimize=False),
       "dynamo_op20":   dict(dynamo=True,  opset_version=20)}
for name, kw in cfg.items():
    f = name + ".onnx"
    torch.onnx.export(m, (x,), f, input_names=["imu"], output_names=["logits"],
                      external_data=False, verbose=False, **kw)
    g = onnx.load(f).graph
    y = ort.InferenceSession(f, providers=["CPUExecutionProvider"]).run(None, {"imu": x.numpy()})[0]
    ops = collections.Counter(n.op_type for n in g.node)
    print(f"{name:<18} opset={onnx.load(f).opset_import[0].version} nodes={len(g.node):>2} "
          f"diff={np.abs(y-ref).max():.1e} {dict(ops)}")
```

```text
legacy_op17        opset=17 nodes=14 diff=1.7e-08 {'Conv': 2, 'Relu': 1, 'Constant': 3, 'Div': 1, 'Erf': 1, 'Add': 1, 'Mul': 2, 'GlobalAveragePool': 1, 'Flatten': 1, 'Gemm': 1}
dynamo_op18        opset=18 nodes=13 diff=1.5e-08 {'Conv': 2, 'Relu': 1, 'Div': 1, 'Erf': 1, 'Add': 1, 'Mul': 2, 'Unsqueeze': 1, 'ReduceMean': 1, 'Squeeze': 1, 'Reshape': 1, 'Gemm': 1}
dynamo_op18_noopt  opset=18 nodes=25 diff=1.5e-08 {'Conv': 2, 'BatchNormalization': 2, 'Relu': 1, 'Constant': 8, 'Div': 1, 'Erf': 1, 'Add': 1, 'Mul': 2, 'Unsqueeze': 1, 'Reshape': 2, 'ReduceMean': 1, 'Squeeze': 1, 'Concat': 1, 'Gemm': 1}
dynamo_op20        opset=20 nodes= 9 diff=1.7e-08 {'Conv': 2, 'Relu': 1, 'Gelu': 1, 'Unsqueeze': 1, 'ReduceMean': 1, 'Squeeze': 1, 'Reshape': 1, 'Gemm': 1}
```

출력에서 볼 것 — 같은 모델이 네 가지 다른 그래프가 됐다. 모두 수치는 같다(1e-8 수준).

| 차이 | legacy op17 | dynamo op18 | 의미 |
|---|---|---|---|
| AdaptiveAvgPool1d | `GlobalAveragePool` + `Flatten` | `Unsqueeze → ReduceMean → Squeeze` + `Reshape` | 같은 수학, 다른 op 조합. 어떤 NPU는 GlobalAveragePool만 빠르게 지원할 수 있다 |
| 상수 | `Constant` 노드 3개 | initializer로 흡수 | `Constant` 노드가 남으면 "접히지 않은 그래프"로 보이고 일부 툴이 싫어한다 |
| BN | 접힘 | 접힘 (`optimize=True`) | `optimize=False`면 BN 2개 + Constant 8개 + Concat이 남아 25노드 |
| GELU | Div/Erf/Add/Mul/Mul | 같음 | opset 20에서만 `Gelu` 단일 노드 |

- **opset만 18 → 20으로 바꿨더니 GELU가 노드 1개가 됐다.** 노드 수는 줄었지만, opset 20을 모르는 툴(4절)은 이 파일을 아예 못 읽는다. 반대로 opset 18 파일은 어디서나 읽히지만 `Erf`를 지원하지 않는 EP에서는 그 노드가 fallback 된다(6.3절에서 실측). "op가 적을수록 좋다"가 항상 맞지 않는다 — **타깃이 지원하는 op·opset 조합**이 기준이다.
- C6 6.2절의 결론과 같다: exporter 정리(optimize)를 **켜고** 넘겨라. 접히지 않은 BN·Constant는 뒤의 도구가 fusion을 못 하게 막는다.

### 3.3 예제 6 — dynamic shape vs static shape, 그리고 batch-1 특수화

무엇을 확인하는 코드인지: (a) static export, (b) legacy `dynamic_axes`, (c)·(d) dynamo `dynamic_shapes`를 예제 입력 batch=1과 batch=2로 각각 export하고, batch=3 입력으로 돌려 본다.

```python
import torch, onnx, numpy as np, onnxruntime as ort, warnings
from f5_model import make
warnings.filterwarnings("ignore")
m = make(); kw = dict(input_names=["imu"], output_names=["logits"], external_data=False, verbose=False)
batch = torch.export.Dim("batch", min=1, max=64)
torch.onnx.export(m, (torch.randn(1, 6, 50),), "dyn_legacy.onnx", dynamo=False, opset_version=17,
                  dynamic_axes={"imu": {0: "batch"}, "logits": {0: "batch"}}, **kw)
for B in (1, 2):                            # 예제 입력의 batch 크기만 다르게
    torch.onnx.export(m, (torch.randn(B, 6, 50),), f"dyn_dynamo_ex{B}.onnx", dynamo=True,
                      dynamic_shapes={"x": {0: batch}}, **kw)
def dims(vi): return [d.dim_value if d.HasField("dim_value") else d.dim_param for d in vi.type.tensor_type.shape.dim]
x3 = np.random.default_rng(0).standard_normal((3, 6, 50)).astype(np.float32)
ref = m(torch.from_numpy(x3)).detach().numpy()
for f in ["imunet.onnx", "dyn_legacy.onnx", "dyn_dynamo_ex1.onnx", "dyn_dynamo_ex2.onnx"]:
    g = onnx.load(f).graph
    print(f"{f:<20} in={dims(g.input[0])} out={dims(g.output[0])}", end="  ")
    try:
        y = ort.InferenceSession(f, providers=["CPUExecutionProvider"]).run(None, {"imu": x3})[0]
        print(f"batch=3 OK diff={np.abs(y - ref).max():.0e}")
    except Exception as e:
        print("batch=3 FAIL:", str(e).split(" : ")[2][:20], "...", str(e)[-48:].strip())
```

```text
imunet.onnx          in=[1, 6, 50] out=[1, 4]  batch=3 FAIL: INVALID_ARGUMENT ... ease fix either the inputs/outputs or the model.
dyn_legacy.onnx      in=['batch', 6, 50] out=['batch', 4]  batch=3 OK diff=3e-08
dyn_dynamo_ex1.onnx  in=['batch', 6, 50] out=[1, 4]  batch=3 FAIL: RUNTIME_EXCEPTION ... e. Input shape:{3,32,1}, requested shape:{1,32}
dyn_dynamo_ex2.onnx  in=['batch', 6, 50] out=['batch', 4]  batch=3 OK diff=4e-08
```

출력에서 볼 것 — 이 노트에서 가장 교묘한 실측이다.

- static 모델(`imunet.onnx`)에 batch 3을 넣으면 ORT가 **로드 단계가 아니라 입력 검증 단계에서** 거부한다(INVALID_ARGUMENT). 정상 동작이다.
- `dyn_dynamo_ex1.onnx`는 **입력은 `'batch'`로 dynamic인데 출력은 `[1, 4]`로 고정**됐고, 실행 중 `Reshape`에서 터졌다(`{3,32,1}`을 `{1,32}`로 못 바꿈). 원인: `torch.export`는 예제 입력에서 **크기가 0 또는 1인 차원을 상수로 특수화**하는 규칙이 있다(0/1 specialization). batch=1 예제로 export하니 `flatten` 뒤의 shape가 `[1, 32]` 상수로 박혔다(예제 1의 `val_28`). export 자체는 **에러도 경고도 없이** 성공했다.
- 같은 코드에 예제 입력만 batch=2로 바꾸면 정상이다. 규칙: **dynamic으로 둘 차원은 예제 입력에서 2 이상으로 준다.**
- legacy의 `dynamic_axes`는 batch=1 예제로도 잘 됐다(tracing이 shape 계산을 다르게 기록한다).

그럼 edge에서는 dynamic이 좋은가? 대부분 **반대**다. NPU 컴파일러는 shape가 고정돼야 tiling·메모리 배치·명령 스트림을 미리 만들 수 있다(C6 9절). 웨어러블의 IMU 모델이라면 batch=1, 윈도우 길이 고정이 자연스럽다. dynamic export는 "서버·PC에서 여러 batch로 검증할 때" 쓰고, 기기로 넘길 때는 static으로 다시 export하거나 `onnxruntime.tools.make_dynamic_shape_fixed` 같은 도구로 고정한다(도구 이름·옵션은 ORT 버전별 문서 확인). 중요한 건 **"dynamic이라고 써 있는 것"과 "실제로 dynamic하게 동작하는 것"이 다를 수 있으니 다른 batch로 꼭 돌려 본다**는 것이다.

### 3.4 예제 7 — 데이터 의존 분기: 조용히 틀린 모델이 나온다

무엇을 확인하는 코드인지: "IMU 에너지가 작으면 분류기를 건너뛰고 0을 낸다"는 흔한 패턴(전력 절약용 gating)을 export한다. legacy·dynamo가 이걸 어떻게 처리하는지, 그리고 `torch.where`, `torch.cond`로 고친 버전을 큰 입력(loud)과 작은 입력(quiet) 둘 다로 검증한다.

```python
import torch, torch.nn as nn, numpy as np, onnxruntime as ort, warnings, onnx
class Gate(nn.Module):                      # 에너지가 작으면 분류기 결과 대신 0 (데이터 의존 분기)
    def __init__(self): super().__init__(); self.fc = nn.Linear(6, 4)
    def forward(self, x):                   # x: [1, 6]
        if x.abs().mean() > 0.5:
            return self.fc(x)
        return torch.zeros(1, 4)
class GateWhere(Gate):                      # 고친 버전 1: 두 갈래를 다 계산하고 고른다
    def forward(self, x):
        return torch.where(x.abs().mean() > 0.5, self.fc(x), torch.zeros(1, 4))
class GateCond(Gate):                       # 고친 버전 2: torch.cond → ONNX If
    def forward(self, x):
        return torch.cond(x.abs().mean() > 0.5, lambda x: self.fc(x), lambda x: x[:, :4] * 0, (x,))
torch.manual_seed(0); sd = Gate().state_dict()
loud, quiet = torch.full((1, 6), 2.0), torch.full((1, 6), 0.1)
try:
    torch.export.export(Gate().eval(), (loud,), strict=False)
except Exception as e:
    print("torch.export:", type(e).__name__, "|", str(e).splitlines()[0][:90])
for name, cls, dyn in [("legacy", Gate, False), ("dynamo", Gate, True),
                       ("where", GateWhere, True), ("cond", GateCond, True)]:
    m = cls().eval(); m.load_state_dict(sd)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        torch.onnx.export(m, (loud,), f"gate_{name}.onnx", dynamo=dyn, input_names=["x"],
                          external_data=False, verbose=False)
    s = ort.InferenceSession(f"gate_{name}.onnx", providers=["CPUExecutionProvider"])
    ok = all(np.allclose(m(v).detach().numpy(), s.run(None, {"x": v.numpy()})[0], atol=1e-6) for v in (loud, quiet))
    print(f"{name:<7} nodes={[n.op_type for n in onnx.load(f'gate_{name}.onnx').graph.node]}  loud&quiet match={ok}")
```

```text
torch.export: GuardOnDataDependentSymNode | Could not guard on data-dependent expression Eq(u0, 1) (unhinted: Eq(u0, 1)).  (Size-like 
legacy  nodes=['Gemm']  loud&quiet match=False
dynamo  nodes=['Gemm']  loud&quiet match=False
where   nodes=['Abs', 'ReduceMean', 'Squeeze', 'Greater', 'Gemm', 'Where']  loud&quiet match=True
cond    nodes=['Abs', 'ReduceMean', 'Squeeze', 'Greater', 'If']  loud&quiet match=True
```

출력에서 볼 것:

- `torch.export`를 직접 부르면 **정직하게 실패한다**: "데이터에 따라 달라지는 조건에 guard를 걸 수 없다." 이게 가장 좋은 동작이다.
- 그런데 `torch.onnx.export`는 legacy·dynamo **둘 다 성공했고 결과가 틀렸다.** 그래프에는 `Gemm` 하나뿐 — 예제 입력(loud)이 탄 분기만 굳었다. quiet 입력에도 분류기 출력을 낸다.
  - legacy는 `TracerWarning`을 낸다(경고를 숨기지 않은 실행에서 확인).
  - dynamo는 torch 2.8에서 strict export가 실패하면 **draft export로 재시도**하고, 실제 값으로 분기를 정해 그래프를 만든 뒤 stderr에 `WARNING: 1 issue(s) found during export, and it was not able to soundly produce a graph.`라는 경고만 남겼다. `fallback=False`여도 그랬다.
- 고치는 법 두 가지:
  - `torch.where`: 두 갈래를 **모두 계산**하고 고른다. ONNX `Where` 하나로 끝나고 NPU 친화적이지만, 전력 절약(분류기 건너뛰기)이라는 원래 목적은 사라진다.
  - `torch.cond`: ONNX `If` 노드(서브그래프 두 개를 품은 제어 흐름 op)가 된다. 의미는 정확하지만, **많은 NPU 컴파일러가 `If`/`Loop`를 지원하지 않거나 CPU로 보낸다**(10절 lint에서 걸린다).
- 임베디드 정답은 보통 세 번째다: **분기를 모델 밖으로 뺀다.** 에너지 계산과 threshold 비교는 펌웨어(MCU의 C 코드)가 하고, 모델은 분기 없는 순수 분류기로 export한다. wake-word의 2단 구조(작은 always-on 검출기 → 큰 모델 깨우기)가 정확히 이 패턴이다.

교훈: **export 성공 ≠ 올바른 그래프.** export 후에는 반드시 학습 분포를 대표하는 여러 입력(여기서는 loud와 quiet)으로 원본과 비교한다(8절, C8).

### 3.5 예제 8 — 미지원 op와 opset 부족

무엇을 확인하는 코드인지: 오디오 전처리의 `torch.fft.rfft`와 이미지 warping의 `grid_sample`을 export해 본다. 어떤 exporter·opset 조합에서 실패하는지, 에러 메시지가 무엇을 말하는지 본다.

```python
import torch, torch.nn as nn, numpy as np, onnxruntime as ort, warnings, onnx, collections
warnings.filterwarnings("ignore")
class PowerSpec(nn.Module):                 # 오디오 프레임 → 파워 스펙트럼 (wake word 전처리 일부)
    def forward(self, x):                   # x: [1, 256]
        return torch.fft.rfft(x).abs() ** 2 # [1, 129]
class Warp(nn.Module):                      # grid_sample: opset 16에 GridSample이 처음 생겼다
    def forward(self, img, grid): return nn.functional.grid_sample(img, grid, align_corners=False)
x = torch.randn(1, 256); img, grid = torch.randn(1, 1, 8, 8), torch.rand(1, 4, 4, 2) * 2 - 1
cases = [("rfft legacy op17", PowerSpec(), (x,), dict(dynamo=False, opset_version=17)),
         ("rfft dynamo op18", PowerSpec(), (x,), dict(dynamo=True)),
         ("grid legacy op15", Warp(), (img, grid), dict(dynamo=False, opset_version=15)),
         ("grid legacy op16", Warp(), (img, grid), dict(dynamo=False, opset_version=16))]
for name, m, args, kw in cases:
    f = name.replace(" ", "_") + ".onnx"
    try:
        torch.onnx.export(m, args, f, external_data=False, verbose=False, **kw)
    except Exception as e:
        print(f"{name}: FAIL {type(e).__name__}: {str(e).splitlines()[0][:110]}"); continue
    s = ort.InferenceSession(f, providers=["CPUExecutionProvider"])
    y = s.run(None, {i.name: a.numpy() for i, a in zip(s.get_inputs(), args)})[0]
    ops = dict(collections.Counter(n.op_type for n in onnx.load(f).graph.node))
    print(f"{name}: ok {ops} maxdiff={np.abs(y - m(*args).numpy()).max():.1e}")
```

```text
rfft legacy op17: FAIL UnsupportedOperatorError: Exporting the operator 'aten::fft_rfft' to ONNX opset version 17 is not supported
rfft dynamo op18: ok {'Unsqueeze': 1, 'DFT': 1, 'ReduceL2': 1, 'Pow': 1} maxdiff=3.7e-04
grid legacy op15: FAIL UnsupportedOperatorError: Exporting the operator 'aten::grid_sampler' to ONNX opset version 15 is not supported. Support for this operat
grid legacy op16: ok {'GridSample': 1} maxdiff=1.2e-07
```

(legacy exporter가 실패할 때 stdout에 찍는 `Torch IR graph at exception: ...` 덤프는 길어서 생략했다. 실패한 aten op가 그래프의 어디에 있는지 보여 주므로 디버깅할 때는 읽어 볼 가치가 있다.)

출력에서 볼 것:

- 에러 메시지에 **aten op 이름**과 **opset 번호**가 같이 나온다. `grid_sampler`는 "opset 15에서는 미지원" → opset 16으로 올리니 `GridSample` 노드 하나로 성공. 에러가 opset을 언급하면 먼저 opset을 올려 본다.
- `fft_rfft`는 legacy symbolic 표에 아예 없다. dynamo 경로는 ONNX `DFT` op(opset 17에 추가)로 변환했다. maxdiff 3.7e-4는 커 보이지만 파워 스펙트럼 값이 수백~천 단위라(별도 확인: 최대 약 1158) 상대 오차로는 1e-6 수준이다.
- 그런데 **NPU 입장에서 `DFT`는 지원 안 될 가능성이 높은 op**다. 해결책은 다음 예제다.

### 3.6 예제 9 — 미지원 op를 지원되는 op로 다시 쓰기: rfft → MatMul

무엇을 확인하는 코드인지: DFT의 정의 `X[k] = ∑ₙ x[n] · e^(−j2πkn/N)`를 실수부·허수부 행렬 두 개로 쓰면, rfft는 상수 행렬과의 `MatMul` 두 번이 된다. NPU가 가장 잘하는 op다.

```python
import torch, torch.nn as nn, numpy as np, onnx, onnxruntime as ort
class PowerSpecMM(nn.Module):               # rfft를 상수 cos/sin 행렬곱 2개로 (NPU가 아는 MatMul)
    def __init__(self, n=256):
        super().__init__()
        k = torch.arange(n // 2 + 1).float()[:, None]; t = torch.arange(n).float()[None, :]
        ang = 2 * torch.pi * k * t / n
        self.register_buffer("C", torch.cos(ang).T.contiguous())   # [256, 129]
        self.register_buffer("S", -torch.sin(ang).T.contiguous())
    def forward(self, x):                   # x: [1, 256]
        re, im = x @ self.C, x @ self.S
        return re * re + im * im
x = torch.randn(1, 256); m = PowerSpecMM()
torch.onnx.export(m, (x,), "pspec_mm.onnx", dynamo=True, external_data=False, verbose=False)
print("ops:", [n.op_type for n in onnx.load("pspec_mm.onnx").graph.node])
y = ort.InferenceSession("pspec_mm.onnx", providers=["CPUExecutionProvider"]).run(None, {"x": x.numpy()})[0]
ref = (torch.fft.rfft(x).abs() ** 2).numpy()
print(f"vs torch.fft: max_abs={np.abs(y - ref).max():.1e}  max_rel={(np.abs(y - ref) / ref.max()).max():.1e}")
print("MACs: matmul", 2 * 256 * 129, "vs FFT ~", int(256 * np.log2(256) * 2))
```

```text
ops: ['MatMul', 'MatMul', 'Mul', 'Mul', 'Add']
vs torch.fft: max_abs=1.2e-02  max_rel=1.1e-05
MACs: matmul 66048 vs FFT ~ 4096
```

출력에서 볼 것:

- 그래프가 `MatMul`·`Mul`·`Add`만 남았다 — 어느 NPU든 받는다. 정확도는 상대 1e-5(float32 합산 순서 차이).
- 대가는 연산량: 행렬곱은 2 × 256 × 129 = 66,048 MAC, FFT는 대략 N·log₂N 규모(여기서는 어림 4,096). **약 16배 더 많은 MAC**을 쓴다. 그래도 NPU에서 66K MAC은 마이크로초 단위이고, CPU fallback 경계 두 번(C6 12.2절)보다 거의 항상 싸다. D1의 MAC 세기, D3의 roofline으로 판단하는 전형적인 trade-off다.
- 또 다른 선택지: 전처리(FFT·mel)를 아예 모델 밖 DSP 코드(CMSIS-DSP, Hexagon DSP 라이브러리)로 빼는 것. 웨어러블 오디오 파이프라인에서는 이쪽이 흔하다(G 모듈).

### 3.7 export 실패 디버깅 순서

| 순서 | 할 일 | 보는 것 |
|---|---|---|
| 1 | `model.eval()` 했나, 입력 dtype·shape가 실제 배포와 같은가 | Dropout·BN이 train 모드면 그래프가 다르다 |
| 2 | 에러의 aten op 이름과 opset 번호 읽기 | "opset N is not supported"면 opset을 올려 본다 (4절의 툴 지원 범위 안에서) |
| 3 | `torch.export.export(model, args)`를 먼저 단독 실행 | graph break, data-dependent guard가 여기서 정직하게 드러난다 |
| 4 | legacy ↔ dynamo 바꿔 보기 | 한쪽만 되는 op가 있다 (예제 8의 rfft, 4.3절의 opset 17) |
| 5 | 문제 op를 등가 op로 다시 쓰기 | 예제 9처럼. C6 8절의 lowering 공식 |
| 6 | 분기·후처리를 모델 밖으로 | 예제 7. 펌웨어가 할 일은 펌웨어로 |
| 7 | 성공 후 반드시 수치 비교 | 여러 입력으로 PyTorch vs ORT (8절). 성공이 정답을 보장하지 않는다 |

---

## 4. Opset과 IR version — 숫자 하나가 배포를 막는 이유

### 4.1 정의 — 버전이 세 종류다

| 버전 | 어디에 적히나 | 무엇을 정하나 | 이 환경의 실측 |
|---|---|---|---|
| **IR version** | `ModelProto.ir_version` | 파일 형식 자체(protobuf 스키마의 어떤 필드를 쓸 수 있나) | onnx 1.19는 기본 12로 저장, ORT 1.19는 10까지 읽음 (예제 2) |
| **opset version** | `ModelProto.opset_import` (domain별) | op 사전의 버전. 이 모델의 op 의미를 어느 판으로 해석하나 | onnx 1.19는 ai.onnx opset 24까지 정의, ORT 1.19는 21까지 (아래) |
| **op의 since_version** | ONNX 스펙 문서 | 각 op가 마지막으로 바뀐 opset | 예: Conv는 11, ReduceMean은 18 (9.3절의 config 파일) |

opset의 규칙은 이렇다. 모델이 `ai.onnx` opset 18을 선언하면, 그래프의 각 op는 "18 이하에서 가장 최근에 정의된 버전"으로 해석된다. Conv는 opset 11 이후 바뀌지 않았으니 opset 18 모델의 Conv는 Conv-11이다. ReduceMean은 18에서 바뀌었으니 ReduceMean-18이다.

말로 하면: opset은 "셀 라이브러리 릴리스 번호"이고, 각 셀(op)은 그 릴리스 시점의 최신 리비전을 쓴다. 릴리스 번호를 올리면 일부 셀의 핀 배치가 바뀔 수 있다.

ORT 1.19가 받아들이는 최대 opset은 별도로 확인했다. Relu 노드 하나짜리 모델을 opset 21, 22, 23으로 만들어 세션을 열면 21은 성공, 22와 23은 `Current official support for domain ai.onnx is till opset 21.`이라는 메시지와 함께 실패한다.

### 4.2 op 의미가 opset마다 바뀐 대표 사례

| op | 바뀐 opset | 무엇이 바뀌었나 | 실무 영향 |
|---|---|---|---|
| Softmax | 13 | 입력을 2D로 접어 계산하던 방식 → 지정한 축 하나만 정규화 | 옛 opset 모델을 새 의미로 해석하면 3D 이상 입력에서 결과가 다르다 |
| GridSample | 16 (신설) | op 자체가 생김 | opset 15 이하로는 export 불가 (예제 8) |
| LayerNormalization, DFT | 17 (신설) | 단일 op로 표현 가능해짐 | 이전에는 여러 기본 op로 분해됨 (C6 8.2절) |
| ReduceMean 등 Reduce 계열 | 18 | `axes`가 **속성 → 입력**으로 이동 | 툴이 속성 방식만 알면 opset 18 모델을 못 읽는다 (아래 예제 4) |
| Gelu | 20 (신설) | 단일 op | 예제 5: 5노드 → 1노드, 대신 opset 20 필요 |
| QuantizeLinear / DequantizeLinear | 21 | int4/uint4 타입, blocked quantization 추가 | 4-bit 가중치 QDQ 모델은 opset 21 이상 필요 (C3) |

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13">ai.onnx opset 지원 범위 (실측·상수 기준) — 막대가 겹치는 구간만 같이 쓸 수 있다</text> <line x1="150" y1="250" x2="643" y2="250" stroke="currentColor"/> <text x="150" y="268" font-size="12" text-anchor="middle">7</text> <text x="324" y="268" font-size="12" text-anchor="middle">13</text> <text x="469" y="268" font-size="12" text-anchor="middle">18</text> <text x="527" y="268" font-size="12" text-anchor="middle">20</text> <text x="556" y="268" font-size="12" text-anchor="middle">21</text> <text x="643" y="268" font-size="12" text-anchor="middle">24</text>
<line x1="469" y1="36" x2="469" y2="250" stroke="#888" stroke-dasharray="3 3"/> <line x1="324" y1="36" x2="324" y2="250" stroke="#888" stroke-dasharray="3 3"/> <text x="10" y="56" font-size="12">onnx 1.19 스펙</text> <rect x="150" y="42" width="493" height="20" fill="#4a7bd0" fill-opacity="0.5"/> <text x="10" y="92" font-size="12">ORT 1.19 (실측)</text> <rect x="150" y="78" width="406" height="20" fill="#3f9a6b" fill-opacity="0.6"/> <text x="10" y="128" font-size="12">torch legacy</text> <rect x="150" y="114" width="377" height="20" fill="#4a7bd0" fill-opacity="0.35"/>
<text x="10" y="164" font-size="12">torch dynamo</text> <rect x="469" y="150" width="174" height="20" fill="#4a7bd0" fill-opacity="0.35"/> <text x="290" y="164" font-size="12" text-anchor="middle">17 이하: Conv 변환 실패 (4.3절)</text> <text x="10" y="200" font-size="12">가상 NPU 툴</text> <rect x="324" y="186" width="174" height="20" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c" stroke-dasharray="4 3"/> <text x="10" y="236" font-size="12">공통 구간</text> <rect x="469" y="222" width="29" height="20" fill="#d0564a" fill-opacity="0.6"/>
<text x="506" y="237" font-size="12">= opset 18만 (dynamo를 쓴다면)</text> <text x="10" y="292" font-size="12">torch legacy 상한 20은 torch 상수 ONNX_TORCHSCRIPT_EXPORTER_MAX_OPSET. 주황(13–18)은 설명용 가상 범위다.</text>
</svg>
```

그림 4 — 툴마다 지원하는 opset 범위가 다르고, 파이프라인 전체가 쓸 수 있는 건 **교집합**뿐이다. 가상의 NPU 툴이 13–18을 지원한다면, dynamo exporter와의 교집합은 opset 18 하나다. 실제 벤더 툴(QNN converter, TensorRT, OpenVINO 등)의 지원 범위는 **SDK 버전마다 다르므로** 릴리스 노트의 op·opset 지원표로 확인해야 한다(F8 "버전 매트릭스").

### 4.3 예제 4 — version converter는 믿을 수 없다. 다시 export하라

무엇을 확인하는 코드인지: 이미 만든 ONNX의 opset을 `onnx.version_converter`로 낮춰 본다(벤더 툴이 낮은 opset만 받는 상황). 그리고 대안인 "원하는 opset으로 다시 export"와 비교한다.

```python
import onnx, numpy as np, onnxruntime as ort, torch, warnings
from onnx import version_converter
from f5_model import make
warnings.filterwarnings("ignore")
x = np.random.default_rng(0).standard_normal((1, 6, 50)).astype(np.float32)
ref = make()(torch.from_numpy(x)).detach().numpy()
def check(f, tag):
    try:
        y = ort.InferenceSession(f, providers=["CPUExecutionProvider"]).run(None, {"imu": x})[0]
        print(f"{tag:<26} ORT ok, diff={np.abs(y - ref).max():.1e}")
    except Exception as e:
        print(f"{tag:<26} ORT FAIL: {str(e).split('Error ')[-1][:70]}")
for src, target in [("imunet.onnx", 17), ("imunet.onnx", 13), ("dynamo_op20.onnx", 18)]:
    try:
        mt = version_converter.convert_version(onnx.load(src), target)
        onnx.save(mt, f"conv_{target}.onnx"); check(f"conv_{target}.onnx", f"convert {src[:6]}->{target}")
    except Exception as e:
        print(f"convert {src[:6]}->{target:<12} FAIL: {str(e).splitlines()[0].split(': ')[-1][:70]}")
for op in (17, 13):                                # 해결: 원하는 opset으로 "다시 export"
    torch.onnx.export(make(), (torch.from_numpy(x),), f"re_{op}.onnx", input_names=["imu"],
                      dynamo=False, opset_version=op, verbose=False)
    check(f"re_{op}.onnx", f"re-export legacy op{op}")
try:
    torch.onnx.export(make(), (torch.from_numpy(x),), "re_dyn17.onnx", input_names=["imu"],
                      dynamo=True, opset_version=17, verbose=False)
    check("re_dyn17.onnx", "re-export dynamo op17")
except Exception as e:
    print("re-export dynamo op17      FAIL:", type(e).__name__, str(e).split("Failure message")[0][-62:])
```

```text
convert imunet->17         ORT FAIL: Unrecognized attribute: noop_with_empty_axes for operator ReduceMean
convert imunet->13           FAIL: No Adapter From Version $14 for Relu
convert dynamo->18           FAIL: No Previous Version of Gelu exists
re-export legacy op17      ORT ok, diff=3.0e-08
re-export legacy op13      ORT ok, diff=3.0e-08
re-export dynamo op17      FAIL: ConversionError  for <OpOverload(op='aten.convolution', overload='default')>. 
```

출력에서 볼 것 — version converter의 세 가지 실패가 모두 다르다.

- **18 → 17**: 변환은 "성공"했지만 결과 파일이 깨졌다. ReduceMean-18의 `axes`는 initializer 입력이었는데, 변환기가 그걸 속성으로 옮기지 못하고(따로 확인했을 때 `axes: []`로 비어 있었다) opset 18에만 있는 `noop_with_empty_axes` 속성은 그대로 남겼다. ORT가 로드 단계에서 거부해서 다행이지, 관대한 툴이었다면 **"모든 축 평균"이라는 다른 계산**이 됐을 수 있다.
- **18 → 13**: Relu-14 → Relu-13 어댑터가 없어서 실패.
- **20 → 18**: `Gelu`는 opset 20에 처음 생긴 op라 "이전 버전이 없다". 하위 opset으로 내리려면 분해가 필요한데 변환기는 그걸 하지 않는다.
- 해결: **원하는 opset으로 처음부터 다시 export한다.** legacy exporter는 17, 13 모두 정상이었다. 반면 dynamo exporter는 이 환경(torch 2.8 + onnxscript 0.7.2)에서 **opset 17 요청 시 Conv 변환부터 실패**했다 — torchlib 변환 함수가 opset 18 이상 기준으로 쓰여 있는 것으로 보인다(원인 추정). 그래서 "opset 17 이하만 받는 툴"이 있다면 현실적인 선택은 legacy exporter다.

규칙: **opset은 export 시점에 타깃 툴의 지원 범위 안에서 고른다. 사후 변환은 최후의 수단이고, 변환했다면 반드시 수치 비교까지 한다.**

---

## 5. ONNX Runtime 구조 — 세션 하나가 하는 일

### 5.1 직관

ORT는 "ONNX 파일을 읽어서, 그래프를 최적화하고, 노드를 하드웨어 백엔드들에 나눠 주고, 실행 계획과 메모리를 미리 잡아 둔 뒤, `run()`이 불릴 때마다 그 계획대로 돌리는" 라이브러리다. 무거운 일은 전부 **세션을 만들 때** 한다. `run()`은 가볍다 — 펌웨어의 init 단계에서 DMA descriptor와 버퍼를 다 잡아 두고, 인터럽트 핸들러에서는 정해진 일만 하는 구조와 같다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="f5m5" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="10" y="20" font-size="13">InferenceSession(model, SessionOptions, providers=[EP1, EP2, ..., CPU])</text> <rect x="10" y="34" width="120" height="58" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="70" y="56" font-size="12" text-anchor="middle">① 로드·검증</text> <text x="70" y="74" font-size="12" text-anchor="middle">IR·opset 확인</text>
<rect x="145" y="34" width="120" height="58" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="205" y="56" font-size="12" text-anchor="middle">② BASIC 최적화</text> <text x="205" y="74" font-size="12" text-anchor="middle">fold · BN · DCE</text> <rect x="280" y="34" width="140" height="58" rx="6" fill="none" stroke="#e08a3c" stroke-width="2.5"/> <text x="350" y="56" font-size="12" text-anchor="middle">③ partitioning</text> <text x="350" y="74" font-size="12" text-anchor="middle">EP 순서대로 GetCapability</text>
<rect x="435" y="34" width="110" height="58" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="490" y="56" font-size="12" text-anchor="middle">④ EP별 최적화</text> <text x="490" y="74" font-size="12" text-anchor="middle">EXTENDED·ALL</text> <rect x="560" y="34" width="110" height="58" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="615" y="56" font-size="12" text-anchor="middle">⑤ 실행 계획</text> <text x="615" y="74" font-size="12" text-anchor="middle">kernel · arena</text>
<line x1="130" y1="63" x2="143" y2="63" stroke="currentColor" stroke-width="1.5" marker-end="url(#f5m5)"/> <line x1="265" y1="63" x2="278" y2="63" stroke="currentColor" stroke-width="1.5" marker-end="url(#f5m5)"/> <line x1="420" y1="63" x2="433" y2="63" stroke="currentColor" stroke-width="1.5" marker-end="url(#f5m5)"/> <line x1="545" y1="63" x2="558" y2="63" stroke="currentColor" stroke-width="1.5" marker-end="url(#f5m5)"/> <rect x="250" y="120" width="200" height="40" rx="5" fill="none" stroke="#e08a3c"/> <text x="350" y="137" font-size="12" text-anchor="middle">EP1 (예: QNN · CoreML)</text>
<text x="350" y="153" font-size="12" text-anchor="middle">"내가 받을 수 있는 노드 묶음"</text> <rect x="250" y="170" width="200" height="40" rx="5" fill="none" stroke="#e08a3c"/> <text x="350" y="187" font-size="12" text-anchor="middle">EP2 (예: XNNPACK)</text> <text x="350" y="203" font-size="12" text-anchor="middle">남은 노드 중에서 고름</text> <rect x="250" y="220" width="200" height="40" rx="5" fill="none" stroke="#888"/> <text x="350" y="237" font-size="12" text-anchor="middle">CPU EP (항상 마지막)</text> <text x="350" y="253" font-size="12" text-anchor="middle">나머지 전부 = fallback</text>
<line x1="350" y1="92" x2="350" y2="118" stroke="currentColor" stroke-width="1.2" marker-end="url(#f5m5)"/> <line x1="350" y1="160" x2="350" y2="168" stroke="currentColor" stroke-width="1.2" marker-end="url(#f5m5)"/> <line x1="350" y1="210" x2="350" y2="218" stroke="currentColor" stroke-width="1.2" marker-end="url(#f5m5)"/> <text x="470" y="140" font-size="12">EP가 받은 연속 구간은 하나의</text> <text x="470" y="157" font-size="12">"fused node"로 바뀌어 EP가</text> <text x="470" y="174" font-size="12">통째로 컴파일·실행한다</text> <text x="470" y="191" font-size="12">(CoreML 모델, QNN graph 등)</text>
<text x="10" y="140" font-size="12">run(): 계획대로</text> <text x="10" y="157" font-size="12">노드 순서대로 kernel 호출,</text> <text x="10" y="174" font-size="12">EP 경계에서 데이터 복사</text> <text x="10" y="191" font-size="12">(6.4절 비용)</text> <text x="10" y="300" font-size="12">비용이 큰 일(①–⑤)은 세션 생성 때 한 번. 펌웨어 init에서 버퍼·descriptor를 미리 잡는 것과 같다.</text> <text x="10" y="318" font-size="12">②와 ④의 경계(어떤 최적화가 어떤 EP 노드에 적용되나)는 ORT 버전·EP마다 다르다 — 결과 파일로 확인 (C6 6절).</text>
</svg>
```

그림 5 — InferenceSession 생성 과정. ③이 이 노트의 핵심인 **graph partitioning**이다. providers 리스트의 앞에 있는 EP가 먼저 노드를 고르고, 아무도 안 가져간 노드는 CPU EP가 맡는다.

### 5.2 SessionOptions에서 알아야 할 것

| 옵션 | 뜻 | 언제 바꾸나 |
|---|---|---|
| `graph_optimization_level` | `ORT_DISABLE_ALL` / `ENABLE_BASIC` / `ENABLE_EXTENDED` / `ENABLE_ALL`(기본) | 디버깅·비교 시 끔. 결과 그래프를 다른 툴에 넘길 거면 BASIC까지만 (C6 6.1절) |
| `optimized_model_filepath` | 최적화된 그래프를 파일로 저장 | 오프라인 최적화, 어떤 fusion이 일어났는지 확인 |
| `intra_op_num_threads` | op 하나 안에서 쓰는 스레드 수 (예: Conv를 여러 코어로) | 코어 수·전력 예산에 맞춤. 웨어러블 AP에서는 big 코어 수 이하로 |
| `inter_op_num_threads` + `execution_mode` | `ORT_SEQUENTIAL`(기본) / `ORT_PARALLEL`: 독립 분기를 동시에 | 분기가 많은 그래프에서만 의미. 직렬 CNN에는 효과 없음 |
| `enable_profiling` | 실행 trace를 JSON으로 저장 | 7.2절 |
| `log_severity_level` | 0=VERBOSE … 3=ERROR | 0으로 하면 EP별 노드 지원 여부가 찍힌다 (6.3절) |
| `add_session_config_entry(key, value)` | 문자열 키로 세부 설정 | 예: `session.disable_cpu_ep_fallback` (6.5절) |

### 5.3 예제 10 — 세션 들여다보기와 스레드 수

무엇을 확인하는 코드인지: 비교용 2D CNN(`bigcnn`, 96×96 RGB, depthwise-separable 4블록, 약 6.7만 파라미터)을 export하고, 세션의 입출력·메타데이터·EP를 확인한 뒤, 스레드 수와 최적화 레벨, 실행 모드에 따른 지연시간을 잰다.

```python
# bigcnn.py — 비교용 2D CNN (96x96 RGB, ReLU만, MobileNet풍 depthwise-separable 4블록)
import torch, torch.nn as nn
def block(ci, co, s):
    return [nn.Conv2d(ci, ci, 3, s, 1, groups=ci), nn.ReLU(), nn.Conv2d(ci, co, 1), nn.ReLU()]
def make_big():
    torch.manual_seed(0)
    layers = [nn.Conv2d(3, 32, 3, 2, 1), nn.ReLU()] + block(32, 64, 1) + block(64, 128, 2) \
             + block(128, 128, 1) + block(128, 256, 2) + [nn.AdaptiveAvgPool2d(1), nn.Flatten(), nn.Linear(256, 10)]
    return nn.Sequential(*layers).eval()
```

```python
import onnxruntime as ort, numpy as np, time, torch
from bigcnn import make_big
torch.onnx.export(make_big(), (torch.randn(1, 3, 96, 96),), "bigcnn.onnx", input_names=["img"],
                  dynamo=True, external_data=False, verbose=False)
print("available:", ort.get_available_providers())
s = ort.InferenceSession("bigcnn.onnx", providers=["CPUExecutionProvider"])
i, o = s.get_inputs()[0], s.get_outputs()[0]
print("input :", i.name, i.shape, i.type, "| output:", o.name, o.shape, o.type)
meta = s.get_modelmeta(); print("producer:", meta.producer_name, "| graph:", meta.graph_name)
print("session providers:", s.get_providers())
x = np.random.default_rng(0).standard_normal((1, 3, 96, 96)).astype(np.float32)
def bench(**opt):
    so = ort.SessionOptions()
    for k, v in opt.items(): setattr(so, k, v)
    s = ort.InferenceSession("bigcnn.onnx", so, providers=["CPUExecutionProvider"])
    for _ in range(10): s.run(None, {"img": x})
    t = []
    for _ in range(100):
        t0 = time.perf_counter(); s.run(None, {"img": x}); t.append(time.perf_counter() - t0)
    return np.median(t) * 1e6
L = ort.GraphOptimizationLevel
for th in (1, 2, 4):
    print(f"intra_op_num_threads={th}: {bench(intra_op_num_threads=th):7.1f} us")
print(f"ORT_DISABLE_ALL, 4 threads: {bench(intra_op_num_threads=4, graph_optimization_level=L.ORT_DISABLE_ALL):7.1f} us")
print(f"ORT_PARALLEL, 4 threads   : {bench(intra_op_num_threads=4, execution_mode=ort.ExecutionMode.ORT_PARALLEL):7.1f} us")
```

```text
available: ['CoreMLExecutionProvider', 'AzureExecutionProvider', 'CPUExecutionProvider']
input : img [1, 3, 96, 96] tensor(float) | output: linear [1, 10] tensor(float)
producer: pytorch | graph: main_graph
session providers: ['CPUExecutionProvider']
intra_op_num_threads=1:  3004.1 us
intra_op_num_threads=2:  2552.3 us
intra_op_num_threads=4:  3150.2 us
ORT_DISABLE_ALL, 4 threads:  2325.5 us
ORT_PARALLEL, 4 threads   :  3112.6 us
```

출력에서 볼 것:

- 이 macOS용 pip 패키지에는 CPU, CoreML, Azure(클라우드 호출용) EP만 들어 있다. QNN·NNAPI·TensorRT는 없다 — **EP는 빌드 옵션**이고, 패키지마다 들어간 EP가 다르다(`onnxruntime-qnn`, `onnxruntime-gpu` 같은 별도 패키지·빌드가 있다).
- 출력 이름이 `linear`다. dynamo exporter가 `output_names`를 안 주면 마지막 모듈 이름을 쓴다. 펌웨어·앱 코드가 이름으로 출력을 찾는다면 export 때 이름을 고정해 두자.
- 시간 숫자는 **이 실행에서는 거의 의미가 없다.** 스레드 4개가 2개보다 느리고, 최적화를 끈 쪽이 더 빠르게 나왔다. 측정 당시 load average가 약 20(8코어)이라 다른 프로세스와 코어를 다투었기 때문이다. 같은 코드를 더 한가할 때 돌렸을 때는 1 → 2 → 4 스레드가 1573 → 1215 → 1062 µs로 줄었다(입력 이름만 다른 동일 모델, 별도 실행). 교훈은 수치가 아니라 **방법**이다: 측정 조건(부하, 코어 고정, 클럭)을 통제하지 않은 벤치마크는 결론을 뒤집는다. 기기에서는 DVFS·thermal도 같은 일을 한다(D6, E9).
- `ORT_PARALLEL`은 직렬 CNN에서 이득이 없다(독립 분기가 없다).

### 5.4 예제 11 — 없는 EP를 요청하면? 조용히 CPU로 돈다

무엇을 확인하는 코드인지: ORT가 "이름을 아는" EP와 "이 빌드에 실제로 들어 있는" EP의 차이, 그리고 없는 EP(QNN)를 요청했을 때의 동작.

```python
import onnxruntime as ort, warnings
known = ort.get_all_providers()                     # 이 ORT 소스가 "이름을 아는" EP 전체
print("known:", len(known), [p for p in known if p.startswith(("QNN", "Nnapi", "Xnn", "Tensorrt", "CUDA", "CoreML", "OpenVINO"))])
print("available (이 빌드에 실제로 들어간 것):", ort.get_available_providers())
with warnings.catch_warnings(record=True) as w:
    warnings.simplefilter("always")
    s = ort.InferenceSession("imunet.onnx", providers=["QNNExecutionProvider"])
print("requested QNN only -> session providers:", s.get_providers())
print("warning:", str(w[0].message)[:75])
assert_ok = s.get_providers()[0] == "QNNExecutionProvider"
print("assert first EP is QNN:", assert_ok)
```

```text
known: 22 ['TensorrtExecutionProvider', 'CUDAExecutionProvider', 'OpenVINOExecutionProvider', 'QNNExecutionProvider', 'NnapiExecutionProvider', 'CoreMLExecutionProvider', 'XnnpackExecutionProvider']
available (이 빌드에 실제로 들어간 것): ['CoreMLExecutionProvider', 'AzureExecutionProvider', 'CPUExecutionProvider']
requested QNN only -> session providers: ['CPUExecutionProvider']
warning: Specified provider 'QNNExecutionProvider' is not in available provider name
assert first EP is QNN: False
```

출력에서 볼 것:

- QNN EP만 요청했는데 **세션은 에러 없이 만들어졌고 CPU로 돈다.** Python `UserWarning` 한 줄이 전부다. 앱 로그에 경고가 묻히면 "NPU에서 돈다고 생각했는데 사실 CPU"라는 상황이 생긴다 — 전력·지연 측정이 전부 틀어진다.
- 그래서 배포 코드에는 **세션 생성 직후 `get_providers()[0]`가 기대한 EP인지 assert**하는 줄을 넣는다. 펌웨어로 치면 "DMA 엔진이 enable 됐는지 레지스터를 읽어 확인"하는 것이다.
- 6.5절의 `session.disable_cpu_ep_fallback`은 "노드 일부가 CPU로 떨어지는 것"을 막는 다른 장치다.

---

## 6. Execution provider와 graph partitioning

### 6.1 EP 지도

| EP | 대상 하드웨어 | 받는 모델 (대표) | 메모 (세부는 ORT 문서·버전별 확인) |
|---|---|---|---|
| CPU | 모든 CPU | fp32, int8 (QDQ·QOperator) | 기본값이자 최종 fallback. MLAS 커널 |
| XNNPACK | ARM/x86 CPU (모바일 최적화) | fp32 위주, 일부 int8 | 모바일 CPU에서 CPU EP보다 빠를 수 있음 |
| CoreML | Apple CPU/GPU/Neural Engine | fp32 (내부적으로 fp16 실행 가능) | 이 노트에서 실측 (6.3–6.5절) |
| NNAPI | Android NNAPI 드라이버 | fp32, int8 | Android가 NNAPI를 deprecated 처리하는 추세라 신규 작업에는 다른 경로가 권장되는 것으로 안다 |
| **QNN** | Qualcomm CPU/GPU/**HTP(Hexagon NPU)** | HTP는 주로 QDQ int8/int16 모델 | Snapdragon 기기의 NPU 경로. backend 라이브러리 경로 지정, context binary 캐시(EPContext) 같은 옵션이 있다 (F4) |
| CUDA / TensorRT | NVIDIA GPU (Jetson 포함) | fp32/fp16/int8 | TensorRT EP는 지원 부분을 TRT 엔진으로 빌드, 나머지는 CUDA/CPU |
| OpenVINO | Intel CPU/GPU/NPU | fp32/int8 | Intel 플랫폼 |

EP가 하는 일은 두 종류로 나뉜다. CPU·XNNPACK·CUDA 같은 EP는 **op별 커널**을 제공한다(노드 하나 = 커널 하나). CoreML·QNN·TensorRT 같은 EP는 받은 **노드 묶음을 통째로 자기 형식으로 컴파일**한다(CoreML 모델, QNN graph, TRT 엔진). 후자를 "compiling EP"라고 부르며, 묶음 안에서는 벤더 컴파일러가 fusion·layout·메모리 배치를 다시 한다. 그래서 묶음이 **크고 적을수록** 유리하다.

### 6.2 partitioning 알고리즘 — 말로 하면

1. providers 리스트 순서대로 각 EP에게 "그래프를 보고 네가 받을 수 있는 노드를 골라라"(`GetCapability`)를 묻는다.
2. EP는 자기가 지원하는 노드 중 **연결된 연속 구간**들을 묶어서 돌려준다. 지원 여부는 op 종류뿐 아니라 dtype, 속성, shape(dynamic인가), 가중치가 상수인가 등에 달렸다.
3. 이미 앞 EP가 가져간 노드는 뒤 EP가 못 가져간다. 마지막에 남은 노드는 전부 CPU EP.
4. EP 경계를 지나는 텐서에는 필요하면 메모리 복사 노드가 들어간다.

이건 C6 12.2절의 "CPU fallback이 왜 성능을 죽이나"를 ORT 안에서 그대로 볼 수 있는 구조다.

### 6.3 예제 12 — verbose 로그로 노드별 지원 여부 보기

무엇을 확인하는 코드인지: `log_severity_level = 0`(VERBOSE)으로 CoreML EP 세션을 만들면, EP가 노드마다 "지원/미지원"을 찍고 partition 수를 요약한다. 출력은 C++ 로그(stderr)라서 셸에서 `grep`으로 골랐다.

```python
# ex10a.py
import onnxruntime as ort
so = ort.SessionOptions(); so.log_severity_level = 0          # 0 = VERBOSE: EP가 노드별로 "되나/안 되나"를 찍는다
s = ort.InferenceSession("imunet.onnx", so, providers=["CoreMLExecutionProvider", "CPUExecutionProvider"])
```

```sh
.venv/bin/python ex10a.py 2>&1 | grep -E "GetSupportedNodes|GetCapability" \
  | sed -E 's/^.*(Operator type|CoreMLExecutionProvider::GetCapability)/\1/; s/\x1b\[[0-9;]*m//g'
```

```text
Operator type: [Conv] index: [0] name: [node_Conv_29] supported: [1]
Operator type: [Relu] index: [1] name: [node_relu] supported: [1]
Operator type: [Conv] index: [2] name: [node_Conv_30] supported: [1]
Operator type: [Div] index: [3] name: [node_Div_15] supported: [1]
Operator type: [Erf] index: [4] name: [node_Erf_16] supported: [0]
Operator type: [Add] index: [5] name: [node_Add_18] supported: [1]
Operator type: [Mul] index: [6] name: [node_Mul_20] supported: [1]
Operator type: [Mul] index: [7] name: [node_gelu] supported: [1]
Operator type: [Unsqueeze] index: [8] name: [node_unsqueeze] supported: [0]
Operator type: [ReduceMean] index: [9] name: [node_mean] supported: [1]
Operator type: [Squeeze] index: [10] name: [node_squeeze] supported: [1]
Operator type: [Reshape] index: [11] name: [node_view] supported: [1]
Operator type: [Gemm] index: [12] name: [node_linear] supported: [1]
CoreMLExecutionProvider::GetCapability, number of partitions supported by CoreML: 3 number of nodes in the graph: 13 number of nodes supported by CoreML: 11
```

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13">imunet.onnx 13노드의 실제 배정 (ORT 1.19 CoreML EP, 예제 12 로그)</text> <rect x="10" y="40" width="44" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="32" y="62" font-size="12" text-anchor="middle">Conv</text> <rect x="58" y="40" width="44" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="80" y="62" font-size="12" text-anchor="middle">Relu</text>
<rect x="106" y="40" width="44" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="128" y="62" font-size="12" text-anchor="middle">Conv</text> <rect x="154" y="40" width="44" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="176" y="62" font-size="12" text-anchor="middle">Div</text> <rect x="208" y="40" width="44" height="34" rx="4" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><text x="230" y="62" font-size="12" text-anchor="middle">Erf</text>
<rect x="262" y="40" width="44" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="284" y="62" font-size="12" text-anchor="middle">Add</text> <rect x="310" y="40" width="44" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="332" y="62" font-size="12" text-anchor="middle">Mul</text> <rect x="358" y="40" width="44" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="380" y="62" font-size="12" text-anchor="middle">Mul</text>
<rect x="412" y="40" width="64" height="34" rx="4" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><text x="444" y="62" font-size="12" text-anchor="middle">Unsqz</text> <rect x="486" y="40" width="44" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="508" y="62" font-size="12" text-anchor="middle">RMean</text> <rect x="534" y="40" width="44" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="556" y="62" font-size="12" text-anchor="middle">Sqz</text>
<rect x="582" y="40" width="44" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="604" y="62" font-size="12" text-anchor="middle">Rshp</text> <rect x="630" y="40" width="44" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="652" y="62" font-size="12" text-anchor="middle">Gemm</text> <line x1="10" y1="88" x2="198" y2="88" stroke="#4a7bd0" stroke-width="3"/><text x="104" y="106" font-size="12" text-anchor="middle">CoreML partition 0 (4노드)</text>
<line x1="208" y1="88" x2="252" y2="88" stroke="#e08a3c" stroke-width="3"/><text x="230" y="106" font-size="12" text-anchor="middle">CPU</text> <line x1="262" y1="88" x2="402" y2="88" stroke="#4a7bd0" stroke-width="3"/><text x="332" y="106" font-size="12" text-anchor="middle">partition 1 (3노드)</text> <line x1="412" y1="88" x2="476" y2="88" stroke="#e08a3c" stroke-width="3"/><text x="444" y="106" font-size="12" text-anchor="middle">CPU</text>
<line x1="486" y1="88" x2="674" y2="88" stroke="#4a7bd0" stroke-width="3"/><text x="580" y="106" font-size="12" text-anchor="middle">partition 2 (4노드)</text> <text x="10" y="140" font-size="12">경계(EP가 바뀌는 곳)가 4번 생긴다: CoreML → CPU → CoreML → CPU → CoreML.</text> <text x="10" y="160" font-size="12">GELU 분해 중 Erf 하나, AdaptiveAvgPool 분해 중 Unsqueeze 하나가 그래프를 세 조각으로 잘랐다.</text> <text x="10" y="186" font-size="12">고친 모델 (예제 13의 imunet_relu): GELU → ReLU, pool → x.mean(dim=2)</text> <line x1="10" y1="202" x2="674" y2="202" stroke="#3f9a6b" stroke-width="3"/>
<text x="10" y="222" font-size="12">→ 전 노드가 CoreML partition 하나 (경계 0번)</text>
</svg>
```

그림 6 — 노드 13개 중 11개를 CoreML이 받았지만 partition은 3개로 쪼개졌다. "지원 비율 85%"라는 숫자보다 **partition 개수와 경계 위치**가 성능을 정한다.

출력에서 볼 것:

- `Erf`가 미지원이다. opset 18 export의 GELU 분해가 정확히 이 op를 만든다(3.2절). 어떤 런타임에선 `Gelu`(opset 20)가, 어떤 런타임에선 `Erf`가 문제다 — **같은 수학이 export 방식에 따라 다른 지원 문제를 만든다.**
- `Unsqueeze`가 미지원으로 나온 이유는 로그에 없다. ORT 1.19 CoreML EP의 Unsqueeze 지원 조건(축 입력 형태 등) 때문으로 추정하지만 확인하지 않았다. 확인할 수 없는 건 원인을 추측하기보다 **우회 패턴**(그 op가 안 생기게 export)을 찾는 게 빠르다.
- 미지원 이유를 남겨 주는 경우도 있다. 예를 들어 6.4절의 external data 모델에서는 VERBOSE 로그에 `Initializers with external data location are not currently supported`가 Conv마다 찍혔다.

### 6.4 예제 13 — CPU vs CoreML: partition 개수, 지연시간, 수치

무엇을 확인하는 코드인지: 네 모델을 CPU EP만으로, 그리고 CoreML EP + CPU로 돌려 지연시간 중앙값, 프로파일링에서 EP별 kernel 개수, 두 결과의 최대 차이를 비교한다. 모델: (1) `imunet` (GELU-erf), (2) `imunet_relu` (GELU → ReLU, pool → `mean`), (3) `bigcnn`, (4) `bigcnn_ext` (같은 bigcnn을 `external_data` 기본값으로 export).

```python
import onnxruntime as ort, numpy as np, json, time, collections, torch, warnings
from f5_model import IMUNet, make
from bigcnn import make_big
warnings.filterwarnings("ignore")
class IMUNetRelu(IMUNet):                     # GELU → ReLU 로 바꾼 "NPU 친화" 변형
    def forward(self, x):
        x = torch.relu(self.bn1(self.conv1(x))); x = torch.relu(self.bn2(self.conv2(x)))
        return self.fc(x.mean(dim=2))
r = IMUNetRelu(); r.load_state_dict(make().state_dict()); r.eval()
kw = dict(dynamo=True, verbose=False)
torch.onnx.export(r, (torch.randn(1, 6, 50),), "imunet_relu.onnx", input_names=["imu"], external_data=False, **kw)
torch.onnx.export(make_big(), (torch.randn(1, 3, 96, 96),), "bigcnn_ext.onnx", input_names=["img"], **kw)  # 가중치 → .onnx.data
def run(f, shape, eps):
    so = ort.SessionOptions(); so.enable_profiling = True; so.log_severity_level = 3
    so.profile_file_prefix = f"prof_{f[:-5]}_{eps[0][:3]}"
    s = ort.InferenceSession(f, so, providers=eps)
    x = {s.get_inputs()[0].name: np.random.default_rng(0).standard_normal(shape).astype(np.float32)}
    for _ in range(10): y = s.run(None, x)[0]          # warm-up
    t = []
    for _ in range(200):
        t0 = time.perf_counter(); s.run(None, x); t.append(time.perf_counter() - t0)
    ev = [e for e in json.load(open(s.end_profiling())) if e["name"].endswith("_kernel_time")]
    nodes = {e["name"]: e["args"]["provider"][:6] for e in ev}
    return y, np.median(t) * 1e6, collections.Counter(nodes.values())
for f, shape in [("imunet.onnx", (1, 6, 50)), ("imunet_relu.onnx", (1, 6, 50)), ("bigcnn.onnx", (1, 3, 96, 96)),
                 ("bigcnn_ext.onnx", (1, 3, 96, 96))]:
    yc, tc, _ = run(f, shape, ["CPUExecutionProvider"])
    ym, tm, cnt = run(f, shape, ["CoreMLExecutionProvider", "CPUExecutionProvider"])
    print(f"{f:<17} CPU {tc:7.1f} us | CoreML+CPU {tm:7.1f} us | kernels by EP {dict(cnt)} | max|diff| {np.abs(yc-ym).max():.1e}")
```

```text
imunet.onnx       CPU   173.8 us | CoreML+CPU   240.0 us | kernels by EP {'CoreML': 3, 'CPUExe': 2} | max|diff| 3.0e-08
imunet_relu.onnx  CPU    72.8 us | CoreML+CPU   205.3 us | kernels by EP {'CoreML': 1} | max|diff| 1.9e-04
bigcnn.onnx       CPU  2968.0 us | CoreML+CPU   270.3 us | kernels by EP {'CoreML': 1} | max|diff| 1.2e-04
bigcnn_ext.onnx   CPU  2515.5 us | CoreML+CPU  5937.4 us | kernels by EP {'CPUExe': 10, 'CoreML': 9} | max|diff| 8.0e-06
```

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="20" font-size="13">예제 13 실측 중앙값 (µs, 로그 축) — 파랑: CPU EP만 · 주황: CoreML EP + CPU</text> <line x1="170.0" y1="36" x2="170.0" y2="268" stroke="#888" stroke-width="0.6" stroke-dasharray="3 3"/> <text x="170.0" y="284" font-size="12" text-anchor="middle">10</text> <line x1="290.0" y1="36" x2="290.0" y2="268" stroke="#888" stroke-width="0.6" stroke-dasharray="3 3"/> <text x="290.0" y="284" font-size="12" text-anchor="middle">100</text> <line x1="410.0" y1="36" x2="410.0" y2="268" stroke="#888" stroke-width="0.6" stroke-dasharray="3 3"/>
<text x="410.0" y="284" font-size="12" text-anchor="middle">1,000</text> <line x1="530.0" y1="36" x2="530.0" y2="268" stroke="#888" stroke-width="0.6" stroke-dasharray="3 3"/> <text x="530.0" y="284" font-size="12" text-anchor="middle">10,000</text> <line x1="170" y1="268" x2="530.0" y2="268" stroke="currentColor"/> <text x="10" y="64" font-size="12">imunet (GELU-erf)</text> <rect x="170" y="44" width="148.8" height="18" fill="#4a7bd0" fill-opacity="0.7"/> <text x="322.8" y="57" font-size="12">174</text> <rect x="170" y="66" width="165.6" height="18" fill="#e08a3c" fill-opacity="0.7"/>
<text x="339.6" y="79" font-size="12">240 · 3 CoreML + 2 CPU</text> <text x="10" y="120" font-size="12">imunet_relu</text> <rect x="170" y="100" width="103.5" height="18" fill="#4a7bd0" fill-opacity="0.7"/> <text x="277.5" y="113" font-size="12">73</text> <rect x="170" y="122" width="157.5" height="18" fill="#e08a3c" fill-opacity="0.7"/> <text x="331.5" y="135" font-size="12">205 · CoreML 1개</text> <text x="10" y="176" font-size="12">bigcnn</text> <rect x="170" y="156" width="296.7" height="18" fill="#4a7bd0" fill-opacity="0.7"/> <text x="470.7" y="169" font-size="12">2,968</text>
<rect x="170" y="178" width="171.8" height="18" fill="#e08a3c" fill-opacity="0.7"/> <text x="345.8" y="191" font-size="12">270 · CoreML 1개</text> <text x="10" y="232" font-size="12">bigcnn_ext (.data)</text> <rect x="170" y="212" width="288.1" height="18" fill="#4a7bd0" fill-opacity="0.7"/> <text x="462.1" y="225" font-size="12">2,516</text> <rect x="170" y="234" width="332.8" height="18" fill="#e08a3c" fill-opacity="0.7"/> <text x="506.8" y="247" font-size="12">5,937 · 9 CoreML + 10 CPU</text> <text x="10" y="306" font-size="12">작은 모델은 EP 호출 오버헤드가 이득보다 크고, 큰 모델은 한 덩어리로 위임될 때만 빨라진다.</text>
<text x="10" y="323" font-size="12">부하가 큰 노트북(load avg ≈ 20)에서 잰 값이라 절대값보다 비율과 방향만 본다.</text>
</svg>
```

그림 7 — 예제 13의 지연시간(로그 축). 막대 끝의 글자는 프로파일링에서 센 EP별 kernel 개수다.

출력에서 볼 것 — 네 줄이 네 가지 교훈이다.

- **imunet**: CoreML 3조각 + CPU 2노드. 경계 비용 때문에 CPU만 쓰는 것보다 느리다. 수치 차이 3e-8은 CPU와 거의 같아서, 이 조각들은 fp32로 실행된 것으로 보인다(CoreML이 어느 장치를 골랐는지는 이 로그로는 알 수 없다 — 추정).
- **imunet_relu**: 전체가 CoreML 한 덩어리가 됐다(그림 6 아래 줄). 그런데도 CPU보다 느리다(73 vs 205 µs). 모델이 너무 작아서(MAC 수만 개) CoreML 호출·동기화 고정비가 계산 이득보다 크다. **작은 모델을 가속기로 보내면 손해일 수 있다** — 웨어러블의 작은 IMU 모델이 NPU가 아니라 MCU/DSP에서 도는 이유 중 하나다(E8). 수치 차이 1.9e-4는 fp16 실행을 시사한다(8절).
- **bigcnn**: 한 덩어리 위임 → CPU 대비 약 11배 빠르다(이 측정에서). 가속기가 이득인 건 "충분히 큰 일을 한 번에 넘길 때"다.
- **bigcnn_ext**: **같은 모델인데 가중치가 external data라는 이유만으로** CoreML EP가 Conv 9개를 전부 거부했다(로그: `Initializers with external data location are not currently supported`). 결과는 CoreML 9조각 + CPU 10노드, CPU 단독보다도 2배 이상 느림. 1.5절에서 말한 `external_data=True` 기본값이 이렇게 성능 문제로 돌아온다. 수치 차이가 오히려 작은(8e-6) 건 무거운 Conv가 전부 CPU에서 fp32로 돌았기 때문이다.

### 6.5 예제 14 — fallback 금지 스위치

무엇을 확인하는 코드인지: `session.disable_cpu_ep_fallback = 1`을 켜고 CoreML EP만 지정하면, CPU로 떨어지는 노드가 하나라도 있을 때 세션 생성이 실패하는지 본다. CI에서 "모델이 NPU에 전부 올라가는가"를 자동으로 막는 장치로 쓸 수 있다.

```python
import onnxruntime as ort
for f in ["imunet.onnx", "imunet_relu.onnx"]:
    so = ort.SessionOptions(); so.log_severity_level = 3
    so.add_session_config_entry("session.disable_cpu_ep_fallback", "1")   # CPU로 떨어지면 실패시켜라
    try:
        s = ort.InferenceSession(f, so, providers=["CoreMLExecutionProvider"])
        print(f"{f:<17} OK   -> {s.get_providers()}")
    except Exception as e:
        print(f"{f:<17} FAIL -> {str(e).split(' : ')[-1][:70]}")
```

```text
imunet.onnx       FAIL -> This session contains graph nodes that are assigned to the default CPU
imunet_relu.onnx  OK   -> ['CoreMLExecutionProvider', 'CPUExecutionProvider']
```

출력에서 볼 것:

- `imunet`은 Erf·Unsqueeze 때문에 실패, `imunet_relu`는 통과. 회귀 테스트에 넣으면 "누군가 모델에 GELU를 넣어서 NPU 경로가 깨졌다"를 merge 전에 잡는다.
- 통과한 세션도 `get_providers()`에는 CPU가 함께 보인다. CPU EP는 목록에 항상 붙어 있다 — 실제로 노드를 받았는지는 프로파일(예제 13)이나 이 스위치로 확인한다.
- 처음에는 providers에 CPU를 명시한 채 이 옵션을 켰더니 "CPU EP를 명시하면서 fallback을 끄는 건 모순"이라는 취지의 에러가 났다. 이 스위치를 쓸 때는 CPU EP를 리스트에서 뺀다.
- CoreML EP 옵션 주의: ORT 1.19 Python에서 CoreML EP에 `{"MLComputeUnits": "CPUOnly"}`나 아예 존재하지 않는 키 `{"Bogus": "1"}`를 넘겨도 에러 없이 세션이 만들어졌고 `get_provider_options()`는 빈 dict였다. 즉 **이 버전에서는 그 옵션들이 반영되지 않았을 가능성이 크다**(이름 있는 provider option들은 이후 버전에서 추가된 것으로 알고 있다 — 버전별 문서 확인). 옵션이 "먹었는지"는 항상 결과(로그, 프로파일, 수치)로 확인한다.

### 6.6 QNN EP는 어떻게 다른가 (F4 예고)

이 맥에는 QNN EP가 없어서 실행은 못 했다. 개념만 정리한다(세부 옵션 이름은 ORT의 QNN EP 문서와 QNN SDK 버전으로 확인).

- QNN EP는 Qualcomm QNN SDK의 backend 라이브러리(CPU, GPU, **HTP** = Hexagon NPU)를 불러서, 받은 partition을 QNN graph로 만들어 실행한다.
- HTP backend는 기본적으로 **양자화된 QDQ 모델**(int8/int16 activation)을 기대한다. fp32 모델을 그대로 넣으면 HTP가 받지 않거나(또는 fp16 경로 지원 여부가 SoC·버전에 따라 다르거나) CPU로 떨어질 수 있다. 그래서 흐름이 "ONNX export → ORT quantize_static으로 QDQ(C2) → QNN EP"가 된다.
- HTP용 그래프 컴파일은 오래 걸릴 수 있어서, 컴파일 결과(context binary)를 ONNX 안에 `EPContext` 노드로 저장해 두고 다음부터 바로 로드하는 기능이 있다 — 펌웨어로 치면 부팅마다 FPGA bitstream을 합성하지 않고 미리 만든 이미지를 로드하는 것.
- Snapdragon 기기에서의 실측(CPU vs HTP, fallback op 목록)은 F4와 프로젝트 PJ4(Qualcomm AI Hub)에서 한다.

---

## 7. 실행을 다루는 도구 — IOBinding, RunOptions, 프로파일링

### 7.1 예제 15 — IOBinding: 버퍼를 미리 잡아 두고 재사용

`session.run(None, {"img": x})`는 편하지만 매번 출력 numpy 배열을 새로 만든다. 펌웨어 감각으로는 "프레임마다 malloc"이다. **IOBinding**은 입력·출력 버퍼를 세션에 미리 묶어 두고, 그 메모리를 그대로 쓰게 한다. GPU/NPU EP에서는 "장치 메모리에 둔 텐서를 host로 복사하지 않고 다음 모델에 넘기는" 용도가 핵심이다.

무엇을 확인하는 코드인지: 출력 버퍼를 미리 할당해 바인딩하고, ORT가 정말 그 주소에 썼는지, 입력 버퍼 내용만 바꿔 다시 실행해도 맞는지, 그리고 CPU에서 속도 이득이 있는지 본다.

```python
import onnxruntime as ort, numpy as np, time
s = ort.InferenceSession("bigcnn.onnx", providers=["CPUExecutionProvider"])
x = np.random.default_rng(0).standard_normal((1, 3, 96, 96)).astype(np.float32)
y_buf = np.empty((1, 10), np.float32)                 # 출력 버퍼를 미리 잡아 둔다 (펌웨어의 static buffer)
io = s.io_binding()
io.bind_cpu_input("img", x)
io.bind_output("linear", "cpu", 0, np.float32, list(y_buf.shape), y_buf.ctypes.data)
ro = ort.RunOptions(); ro.logid = "frame-42"; ro.log_severity_level = 3
s.run_with_iobinding(io, ro)
ref = s.run(None, {"img": x})[0]
print("same buffer address:", y_buf.ctypes.data == io.get_outputs()[0].data_ptr(), "| diff:", np.abs(y_buf - ref).max())
x[:] = np.random.default_rng(1).standard_normal(x.shape)   # 입력 버퍼 내용만 갈아끼우고 다시 실행
s.run_with_iobinding(io, ro)
print("after new frame, matches run():", np.allclose(y_buf, s.run(None, {"img": x})[0]))
xo = ort.OrtValue.ortvalue_from_numpy(x)               # OrtValue: ORT가 아는 텐서 핸들
print("OrtValue:", xo.device_name(), xo.shape(), xo.data_type())
def bench(f, n=300):
    for _ in range(20): f()
    t = []
    for _ in range(n): t0 = time.perf_counter(); f(); t.append(time.perf_counter() - t0)
    return np.median(t) * 1e6
print(f"run(dict)          : {bench(lambda: s.run(None, {'img': x})):7.1f} us")
print(f"run_with_iobinding : {bench(lambda: s.run_with_iobinding(io, ro)):7.1f} us")
```

```text
same buffer address: True | diff: 0.0
after new frame, matches run(): True
OrtValue: cpu [1, 3, 96, 96] tensor(float)
run(dict)          :  2409.0 us
run_with_iobinding :  2579.7 us
```

출력에서 볼 것:

- ORT가 우리가 준 주소(`y_buf`)에 그대로 썼다. 펌웨어의 "정적 출력 버퍼 + DMA 목적지 주소 고정"과 같은 패턴이다.
- 입력 numpy 배열의 **내용만** 바꾸고 다시 실행해도 새 결과가 나왔다. 즉 이 CPU 경로에서 `bind_cpu_input`은 numpy 메모리를 복사해 두지 않고 참조한 것으로 보인다(새 프레임 결과가 `run()`과 일치). 링버퍼 슬롯을 바꿔 가며 쓸 때는 이 동작을 전제로 해도 되는지 버전·EP별로 확인해야 한다.
- CPU EP에서는 속도 이득이 없었다(노이즈 범위 안). 출력 10개짜리 배열 할당은 Conv 연산에 비하면 무시할 수준이다. IOBinding의 진짜 이득은 **장치 메모리가 따로 있는 EP**(CUDA, 일부 NPU)에서 host↔device 복사를 없앨 때다.
- `RunOptions`는 `run()` 한 번에 붙는 옵션이다: 로그 태그(`logid`), 로그 레벨, 그리고 다른 스레드에서 실행을 중단시키는 `terminate` 플래그 등. 프레임 번호를 `logid`로 달아 두면 로그에서 어느 프레임의 문제인지 추적할 수 있다.

### 7.2 예제 16 — 프로파일링 JSON을 op별 표로

`enable_profiling = True`로 세션을 만들면 ORT가 Chrome trace 형식의 JSON을 쓴다(`chrome://tracing`이나 Perfetto에 열 수 있다). 세션 생성 단계, `model_run`, 노드별 `..._kernel_time` 이벤트가 들어 있고, 노드 이벤트의 `args`에 `op_name`, `provider`, `output_size` 등이 있다.

무엇을 확인하는 코드인지: bigcnn을 스레드 1개로 50번 돌린 trace를 파싱해 op 종류별 시간 표와 가장 느린 노드 3개를 뽑는다.

```python
import onnxruntime as ort, numpy as np, json, collections
so = ort.SessionOptions(); so.enable_profiling = True; so.profile_file_prefix = "prof_bigcnn"
so.intra_op_num_threads = 1                                 # 스레드 1개: op 시간이 겹치지 않게
s = ort.InferenceSession("bigcnn.onnx", so, providers=["CPUExecutionProvider"])
x = np.random.default_rng(0).standard_normal((1, 3, 96, 96)).astype(np.float32)
for _ in range(50): s.run(None, {"img": x})
path = s.end_profiling(); ev = json.load(open(path))
k = [e for e in ev if e.get("cat") == "Node" and e["name"].endswith("_kernel_time")]
print("trace file:", path, "| events:", len(ev), "| kernel events:", len(k))
print("one event:", {key: k[0][key] for key in ("name", "dur")}, {a: k[0]["args"][a] for a in ("op_name", "provider", "output_size")})
runs = sum(1 for e in ev if e["name"] == "model_run")
tot = collections.defaultdict(float); cnt = collections.Counter()
for e in k: tot[e["args"]["op_name"]] += e["dur"]; cnt[e["args"]["op_name"]] += 1
all_us = sum(tot.values()) / runs
print(f"runs={runs}  sum of kernels per run = {all_us:.0f} us")
print(f"{'op':<20}{'nodes':>6}{'us/run':>9}{'share':>8}")
for op, d in sorted(tot.items(), key=lambda t: -t[1]):
    print(f"{op:<20}{cnt[op] // runs:>6}{d / runs:>9.1f}{d / runs / all_us:>8.1%}")
convs = collections.defaultdict(float)
for e in k:
    if e["args"]["op_name"] in ("Conv", "FusedConv"): convs[e["name"].replace("_kernel_time", "")] += e["dur"] / runs
print("top-3 nodes:", [(n, round(d)) for n, d in sorted(convs.items(), key=lambda t: -t[1])[:3]])
```

```text
trace file: prof_bigcnn_2026-09-30_18-47-25.json | events: 1902 | kernel events: 600
one event: {'name': 'node_conv2d_kernel_time', 'dur': 324} {'op_name': 'FusedConv', 'provider': 'CPUExecutionProvider', 'output_size': '294912'}
runs=50  sum of kernels per run = 4181 us
op                   nodes   us/run   share
FusedConv                9   4150.8   99.3%
ReduceMean               1     16.5    0.4%
Gemm                     1      7.8    0.2%
Reshape                  1      5.4    0.1%
top-3 nodes: [('node_conv2d_6', 710), ('node_conv2d_2', 625), ('node_conv2d_3', 525)]
```

출력에서 볼 것:

- 원래 ONNX에는 `Conv`와 `Relu`가 있었는데 프로파일에는 `FusedConv`만 있다. 기본 레벨(ENABLE_ALL)에서 ORT가 Conv+Relu를 contrib op `FusedConv`로 합쳤다(C6 6.2절). 그래서 **프로파일의 op 이름은 원본 그래프의 op 이름과 다를 수 있다.** 원본과 대응시키려면 노드 이름(`node_conv2d_6`)을 쓴다.
- `output_size: 294912` = 첫 Conv 출력 1×32×48×48×4바이트. 손으로 맞춰진다.
- 99%가 Conv다. 그럼 어느 Conv가 왜 느린가? 손계산해 본다. 입력 96 → stem stride 2 → 48, 블록 2의 depthwise stride 2 → 24.

| 노드 | 종류 | 출력 크기 | MAC | 이 측정의 µs |
|---|---|---|---|---|
| `node_conv2d_6` | pointwise 128→128 | 24×24 | 24·24·128·128 = 9.44M | 710 |
| `node_conv2d_2` | pointwise 32→64 | 48×48 | 48·48·32·64 = 4.72M | 625 |
| `node_conv2d_3` | depthwise 64, stride 2 | 24×24 | 24·24·64·9 = 0.33M | 525 |

- `node_conv2d_3`(depthwise)은 MAC이 pointwise의 **1/14 ~ 1/28인데 시간은 비슷하다.** depthwise conv는 출력 하나당 MAC 9개뿐이라 연산 대비 메모리 접근이 많은 memory-bound op다(D3 roofline, B9). MAC 수만 세서 지연을 예측하면 틀리는 전형적인 예다.
- 숫자 자체는 5.3절처럼 부하 때문에 흔들린다(앞선 다른 실행에서는 kernel 합계가 1585 µs였고 top-3 순서도 달랐다). 프로파일은 **같은 조건에서 before/after를 비교**할 때 쓴다.
- 기기에서도 같은 일을 한다: ORT on Android라면 같은 JSON이 나오고, QNN·TFLite·Vela는 각자의 프로파일러가 비슷한 per-op 표를 준다(F8).

---

## 8. PyTorch vs ORT 수치 비교 — "같은 모델"은 얼마나 같은가

C8에서 "기준이 여러 개"라고 했다: PyTorch float, ORT float, 가속기 실행. ONNX로 넘긴 직후의 첫 검증은 **PyTorch(golden) vs ORT CPU**, 그다음이 **ORT CPU vs 가속기 EP**다.

### 8.1 예제 17 — 64개 입력으로 지표 4개

무엇을 확인하는 코드인지: bigcnn에 랜덤 입력 64개를 넣어 PyTorch 출력을 기준으로 ORT CPU와 ORT CoreML의 max abs error, cosine, SQNR, top-1 일치율을 계산한다(C8 2절의 지표, float64로 계산).

```python
import onnxruntime as ort, numpy as np, torch
from bigcnn import make_big
m = make_big()
X = np.random.default_rng(0).standard_normal((64, 1, 3, 96, 96)).astype(np.float32)
with torch.no_grad():
    ref = np.concatenate([m(torch.from_numpy(x)).numpy() for x in X])          # PyTorch fp32 = golden
def metrics(r, t):
    r, t = r.astype(np.float64), t.astype(np.float64); e = t - r
    return dict(max_abs=np.abs(e).max(), cos=(r * t).sum() / np.linalg.norm(r) / np.linalg.norm(t),
                sqnr_db=10 * np.log10((r ** 2).sum() / (e ** 2).sum()),
                top1=(r.argmax(1) == t.argmax(1)).mean())
for name, eps in [("ORT CPU", ["CPUExecutionProvider"]),
                  ("ORT CoreML", ["CoreMLExecutionProvider", "CPUExecutionProvider"])]:
    so = ort.SessionOptions(); so.log_severity_level = 3
    s = ort.InferenceSession("bigcnn.onnx", so, providers=eps)
    out = np.concatenate([s.run(None, {"img": x})[0] for x in X])
    k = metrics(ref, out)
    print(f"{name:<11} max_abs={k['max_abs']:.1e} cos={k['cos']:.8f} SQNR={k['sqnr_db']:5.1f} dB top1_agree={k['top1']:.3f}")
print("logit range:", ref.min().round(3), ref.max().round(3))
```

```text
ORT CPU     max_abs=3.0e-08 cos=1.00000000 SQNR=133.1 dB top1_agree=1.000
ORT CoreML  max_abs=1.4e-04 cos=0.99999885 SQNR= 56.2 dB top1_agree=1.000
logit range: -0.054 0.059
```

출력에서 볼 것:

- **ORT CPU vs PyTorch: SQNR 133 dB.** float32의 반올림 수준(약 24비트 ≈ 144 dB에서 연산 누적으로 조금 깎임)이다. 커널·합산 순서가 달라도 fp32끼리는 이 정도로 맞아야 정상이다. 여기서 1e-3 같은 차이가 나면 export가 뭔가 바꾼 것이다(전처리, layout, 분기 고정 — 3.4절).
- **ORT CoreML: SQNR 56 dB.** fp16의 가수는 11비트(≈ 66 dB)라, 레이어를 거치며 조금 깎인 56 dB는 "CoreML이 내부적으로 fp16으로 계산했다"는 해석과 맞는다(추정 — CoreML이 GPU/Neural Engine을 골랐을 가능성. 이 실험만으로 장치를 확정할 수 없다).
- top-1은 둘 다 100% 일치. 단, 이 모델은 랜덤 초기화라 logit 범위가 ±0.06으로 작다. 실제 학습된 모델에서는 결정 경계 근처 샘플에서 fp16 오차가 top-1을 뒤집을 수 있으므로 **실제 데이터로** 다시 본다(C8 2.4절).
- 기준선 정하기: fp32 EP는 1e-5 수준 tolerance, fp16 EP는 SQNR ≥ 40–50 dB 같은 기준, int8은 C2·C8처럼 task 지표(정확도)로. 어느 쪽이든 "기준과 tolerance를 숫자로 적어 두는 것"이 bring-up 문서의 핵심이다(C8 3절).
- 레이어별로 어디서 갈라지는지 찾으려면 2.3절(예제 3)처럼 중간 텐서를 출력으로 승격해서 C8 4절의 절차를 그대로 돌린다.

---

## 9. ORT의 나머지 도구 — 양자화, `.ort` 포맷, minimal build, genai

### 9.1 예제 18 — 양자화 복습: QDQ 모델 한 번 더 (C2 5절)

C2 5절에서 `quantize_static`으로 QDQ 모델을 만들고 해부했다. 여기서는 "NPU에 넘길 int8 ONNX"가 어떤 모양인지만 다시 확인한다.

무엇을 확인하는 코드인지: 6.4절의 `imunet_relu.onnx`(CoreML에 통째로 올라간 모델)를 calibration 32개로 static int8 QDQ로 바꾸고, 노드·initializer dtype·fp32 대비 오차를 본다.

```python
import numpy as np, onnx, onnxruntime as ort, collections
from onnxruntime.quantization import quantize_static, CalibrationDataReader, QuantFormat, QuantType
rng = np.random.default_rng(0)
class Calib(CalibrationDataReader):                 # 대표 입력 32개 (실제론 센서 로그에서 뽑는다)
    def __init__(self): self.it = iter([{"imu": rng.standard_normal((1, 6, 50)).astype(np.float32)} for _ in range(32)])
    def get_next(self): return next(self.it, None)
quantize_static("imunet_relu.onnx", "imunet_relu_qdq.onnx", Calib(), quant_format=QuantFormat.QDQ,
                activation_type=QuantType.QInt8, weight_type=QuantType.QInt8, per_channel=True)
g = onnx.load("imunet_relu_qdq.onnx").graph
print(dict(collections.Counter(n.op_type for n in g.node)))
print("init dtypes:", dict(collections.Counter(onnx.TensorProto.DataType.Name(t.data_type) for t in g.initializer)))
x = rng.standard_normal((16, 1, 6, 50)).astype(np.float32)
f = ort.InferenceSession("imunet_relu.onnx", providers=["CPUExecutionProvider"])
q = ort.InferenceSession("imunet_relu_qdq.onnx", providers=["CPUExecutionProvider"])
a = np.concatenate([f.run(None, {"imu": v})[0] for v in x]); b = np.concatenate([q.run(None, {"imu": v})[0] for v in x])
print(f"fp32 vs int8-QDQ: max_abs={np.abs(a-b).max():.3f} SQNR={10*np.log10((a**2).sum()/((a-b)**2).sum()):.1f} dB "
      f"top1_agree={(a.argmax(1)==b.argmax(1)).mean():.3f}")
```

```text
{'DequantizeLinear': 11, 'QuantizeLinear': 5, 'Conv': 2, 'ReduceMean': 1, 'Gemm': 1}
init dtypes: {'INT64': 1, 'INT8': 11, 'FLOAT': 11, 'INT32': 6}
fp32 vs int8-QDQ: max_abs=0.002 SQNR=48.8 dB top1_agree=1.000
```

(quantize_static이 내는 "pre-processing을 먼저 하라"는 권고 로그는 C2와 같은 이유로 생략했다.)

출력에서 볼 것:

- QDQ 형식에서 연산 노드(`Conv`, `Gemm`, `ReduceMean`)는 **여전히 float op**이고, 앞뒤에 `QuantizeLinear`/`DequantizeLinear`가 붙어 "여기서 int8로 양자화된다"는 정보를 표현한다. 벤더 컴파일러는 `DQ → Conv → Q` 패턴을 보고 "int8 Conv 하나"로 바꾼다. 그래서 QDQ가 **런타임 중립적인 양자화 표현**으로 쓰인다(C2).
- `Relu` 노드가 사라졌다. Conv 출력의 양자화 범위가 ReLU 이후 값(0 이상)으로 잡히면 ReLU가 Q 노드의 clamp에 흡수될 수 있어서, quantizer가 제거한 것으로 보인다(ORT quantizer의 동작으로 추정).
- initializer를 이름으로 확인하면: INT8은 가중치 3개와 zero-point들, FLOAT은 scale들(가중치 scale은 per-channel이라 길이 16/32/4), INT32는 bias 3개와 그 zero-point 3개, INT64는 ReduceMean의 axes다.
- QNN HTP처럼 int8 NPU로 갈 모델은 이 형태로 넘긴다. QDQ 노드를 **어디에 두느냐**(입력 직후, 모든 Conv 앞뒤, 출력 직전)가 벤더 컴파일러가 int8 구간을 끊김 없이 만들 수 있는지를 정한다 — 10.2절 체크리스트.

### 9.2 `.ort` 포맷과 minimal build — 개념

ORT는 원래 서버·PC용으로 크다(이 맥의 Python 패키지만 해도 수십 MB). 모바일·임베디드 Linux용으로는 **minimal build**가 있다.

- **ORT format(`.ort`)**: ONNX(protobuf)를 ORT 내부 형식으로 미리 변환한 flatbuffer 파일. protobuf 파서와 그래프 최적화 코드가 런타임에 필요 없어진다. flatbuffer는 TFLite와 같은 방식이라 파일을 메모리에 매핑한 채 바로 읽을 수 있다(F1).
- **minimal build**: `.ort`만 읽고, **이 모델들에 필요한 op 커널만** 컴파일해 넣은 ORT. 변환 도구가 만든 "필요 op 목록" config를 빌드 스크립트에 넘긴다(빌드 옵션 이름은 ORT 빌드 문서 확인 — 변환 도구의 도움말은 `--include_ops_by_config`를 언급한다). 펌웨어로 치면 "링커의 `--gc-sections`를 모델 기준으로 미리 하는 것"이다.
- ORT Mobile 패키지(Android/iOS용 사전 빌드)도 이 개념의 연장이다. 실제 바이너리 크기 절감폭은 op 수와 빌드 옵션에 따라 다르므로 직접 빌드해서 재야 한다(여기서는 빌드하지 않았다).

### 9.3 예제 19 — `.ort` 변환과 필요 op 목록

무엇을 확인하는 코드인지: ORT에 들어 있는 변환 도구로 `imunet.onnx`, `bigcnn.onnx`를 `.ort`로 바꾸고, minimal build용 config 파일 내용과 파일 첫 바이트를 본다.

```sh
mkdir -p ortfmt && cp imunet.onnx bigcnn.onnx ortfmt/ && cd ortfmt
../.venv/bin/python -m onnxruntime.tools.convert_onnx_models_to_ort . \
    --optimization_style Fixed --enable_type_reduction
ls -l | awk 'NR>1{print $5, $9}'
grep -v "^#" required_operators_and_types.config
```

```text
Converted 2/2 models successfully.
297345 bigcnn.onnx
283200 bigcnn.ort
21468 imunet.onnx
17928 imunet.ort
425 required_operators_and_types.config
ai.onnx;11;Conv{"inputs": {"0": ["float"]}}
ai.onnx;13;Erf,Gemm{"inputs": {"0": ["float"]}},Squeeze,Unsqueeze
ai.onnx;14;Add{"inputs": {"0": ["float"]}},Div{"inputs": {"0": ["float"]}},Mul{"inputs": {"0": ["float"]}},Reshape
ai.onnx;18;ReduceMean{"inputs": {"0": ["float"]}}
com.microsoft;1;FusedConv
```

(변환 도구의 진행 로그 중 "Converted" 줄만 남겼다. `.venv` 경로는 실행 위치에 맞게 바꾼다.)

```python
import onnxruntime as ort, numpy as np
x = np.random.default_rng(0).standard_normal((1, 6, 50)).astype(np.float32)
a = ort.InferenceSession("imunet.onnx", providers=["CPUExecutionProvider"]).run(None, {"imu": x})[0]
b = ort.InferenceSession("imunet.ort", providers=["CPUExecutionProvider"]).run(None, {"imu": x})[0]
print(".ort vs .onnx max diff:", np.abs(a - b).max())
print("first 8 bytes of .ort :", open("imunet.ort", "rb").read(8))
print("first 8 bytes of .onnx:", open("imunet.onnx", "rb").read(8))
```

```text
.ort vs .onnx max diff: 0.0
first 8 bytes of .ort : b'\x14\x00\x00\x00ORTM'
first 8 bytes of .onnx: b'\x08\n\x12\x07pyto'
```

출력에서 볼 것:

- config의 각 줄은 `domain;op가 정의된 opset 버전;op 목록{dtype}`이다. Conv는 opset 11 정의, ReduceMean은 18 정의 — 4.1절의 since_version이 바로 이것이다. `--enable_type_reduction`을 주면 "float 입력용 커널만" 같은 dtype 정보까지 붙어서 minimal build가 더 작아질 수 있다.
- 목록에 `Relu`가 없고 `com.microsoft;1;FusedConv`가 있다. `Fixed` 스타일은 변환 시점에 최적화를 끝내 굳히므로(Conv+Relu → FusedConv), minimal build에는 FusedConv 커널만 있으면 된다. 반대로 말하면 **변환한 머신과 다른 하드웨어에 맞는 최적화가 굳어질 수 있다**. 도구 도움말은 NNAPI·CoreML 같은 EP를 쓸 때는 `Runtime` 스타일을 권장한다.
- `.ort`의 첫 4바이트 `0x14`는 flatbuffer의 루트 테이블 오프셋(20), 다음 4바이트 `ORTM`은 파일 식별자다. `.onnx`는 1.4절에서 해독한 protobuf 태그로 시작한다. 결과는 bit-exact(0.0) — 같은 커널이 같은 순서로 돈다.

### 9.4 onnxruntime-genai — LLM용 껍데기 (개념만)

LLM 추론은 "모델 한 번 실행"이 아니라 **토큰마다 반복 실행 + KV cache 관리 + 샘플링 + 토크나이저**다(D5). ORT 본체는 그래프 한 번 실행만 담당하므로, Microsoft는 그 위에 생성 루프를 얹은 `onnxruntime-genai` 라이브러리를 따로 낸다(모델 폴더에 ONNX 모델과 생성 설정 파일을 두는 방식으로 알고 있다). 이 환경에는 설치하지 않았고 돌려 보지 않았다. 온디바이스 SLM 경로 비교(llama.cpp F3, Qualcomm Genie F4, ExecuTorch F6)에서 "ONNX 쪽 선택지"로 기억해 두면 된다 — 세부 API와 지원 EP는 저장소 문서로 확인.

---

## 10. ONNX를 NPU 툴체인에 넘기기 — deployable ONNX

### 10.1 왜 ONNX가 hand-off 포맷인가

- **중립성**: 모델팀은 PyTorch, 벤더는 자기 컴파일러. 둘 사이 계약서가 필요하고, 표준 op 사전과 버전(opset)이 있는 ONNX가 그 역할을 한다.
- **검사 가능성**: protobuf라 어떤 언어로든 열 수 있고, `onnx.checker`, shape inference, Netron 같은 공용 도구가 있다. 벤더에게 "이 노드 이름의 이 op"라고 정확히 말할 수 있다.
- **레퍼런스 런타임**: ORT CPU라는 "정답 비교 대상"이 함께 온다. 벤더 결과가 이상하면 ORT CPU 결과와 비교하면 된다(8절).
- **양자화 정보의 표준 표현**: QDQ(9.1절)로 scale·zero-point를 그래프에 실어 보낼 수 있다.

한계도 기억한다: 벤더 툴은 ONNX op의 **일부**만, opset의 **일부 범위**만, 속성 조합의 **일부**만 지원한다. 그 교집합을 찾는 일이 F8의 "op 지원표 읽기"다. 그리고 TFLite(F1)처럼 ONNX가 아닌 포맷을 주 입력으로 받는 스택도 많다.

### 10.2 deployable ONNX 체크리스트

| 항목 | 왜 | 확인 방법 | 이 노트의 예 |
|---|---|---|---|
| 입력·출력 shape가 전부 숫자 (static) | NPU는 컴파일 시점에 tiling·메모리를 고정 | `dim_value`만 있는지 | 예제 6: batch-1 특수화 함정 |
| opset이 타깃 툴 범위 안 | 범위 밖이면 파싱 자체가 실패 | `opset_import` vs 벤더 지원표 | 예제 4: 변환기 대신 재-export |
| IR version이 타깃 런타임 이하 | 파일 형식 자체를 못 읽음 | `ir_version` | 예제 2: IR 12 vs ORT 1.19의 10 |
| 표준 domain만 (`com.microsoft` 등 없음) | contrib op는 ORT 전용 | `opset_import`의 domain | ORT EXTENDED 이상 최적화 결과를 넘기지 말 것 (C6 6절) |
| 타깃 지원 op만 | 미지원 op = fallback 또는 컴파일 실패 | 벤더 op 지원표, `disable_cpu_ep_fallback` | 예제 12–14: Erf·Unsqueeze |
| 상수 접힘 (BN·Constant·Shape 계산 없음) | fusion 패턴이 깨짐 | `optimize=True`, ORT BASIC, onnxscript optimizer | 예제 5: noopt 25노드 |
| 학습 전용 op 없음 (Dropout, training 모드 BN) | 추론 의미가 다름 | `model.eval()` 후 export, op 목록 | A5 9.2절 |
| 제어 흐름 없음 (If·Loop) | 많은 NPU가 미지원 | op 목록 | 예제 7: 분기는 펌웨어로 |
| 가중치 내장 (external data 없음, 크기가 허용하면) | 일부 EP·툴이 external data를 못 읽음 | `data_location` | 예제 13: CoreML 전면 fallback |
| QDQ가 연산 앞뒤에 일관되게 (int8 타깃일 때) | int8 구간이 끊기면 중간에 float 변환 | DQ→Op→Q 패턴 확인 | 예제 18, C2 5절 |
| 수치가 golden과 일치 | export가 의미를 바꿀 수 있다 | 여러 입력으로 PyTorch vs ORT | 예제 7, 17 |

### 10.3 예제 20 — 체크리스트를 코드로: deploy lint

무엇을 확인하는 코드인지: 위 체크리스트 일부(external data, dynamic shape, opset 범위, domain, op 화이트리스트)를 자동 점검하는 작은 lint를 짜서 이 노트에서 만든 모델들에 돌린다. op 화이트리스트와 opset 범위(13–18)는 **가상의 NPU 기준**이다 — 실제로는 벤더 지원표에서 채운다.

```python
# deploy_lint.py — "벤더 NPU 컴파일러에 넘겨도 되는 ONNX인가" 자동 점검 (규칙은 가상의 NPU 기준)
import onnx, sys
NPU_OPS = {"Conv", "Relu", "Clip", "Add", "Mul", "Gemm", "MatMul", "ReduceMean", "GlobalAveragePool",
           "Reshape", "Flatten", "Squeeze", "Unsqueeze", "MaxPool", "AveragePool", "Concat", "Transpose",
           "Softmax", "Sigmoid", "QuantizeLinear", "DequantizeLinear"}
OPSET_RANGE = (13, 18)                     # 가상의 벤더 컨버터 지원 범위
def lint(path):
    m = onnx.load(path, load_external_data=False); g = m.graph; issues = []
    if any(t.data_location == onnx.TensorProto.EXTERNAL for t in g.initializer): issues.append("external-data")
    for vi in list(g.input) + list(g.output):
        if any(not d.HasField("dim_value") for d in vi.type.tensor_type.shape.dim): issues.append(f"dynamic:{vi.name}")
    for o in m.opset_import:
        if o.domain in ("", "ai.onnx") and not OPSET_RANGE[0] <= o.version <= OPSET_RANGE[1]:
            issues.append(f"opset{o.version}")
        elif o.domain not in ("", "ai.onnx"): issues.append(f"domain:{o.domain}")
    bad = sorted({n.op_type for n in g.node} - NPU_OPS)
    if bad: issues.append("ops:" + ",".join(bad))
    nq = sum(n.op_type == "QuantizeLinear" for n in g.node)
    return issues, ("int8-QDQ" if nq else "float")
for p in sys.argv[1:]:
    issues, kind = lint(p)
    print(f"{p:<24} {kind:<8} {'DEPLOYABLE' if not issues else 'FIX: ' + ' | '.join(issues)}")
```

```sh
.venv/bin/python deploy_lint.py imunet.onnx dynamo_op18_noopt.onnx dynamo_op20.onnx dyn_dynamo_ex2.onnx \
    gate_cond.onnx bigcnn_ext.onnx imunet_relu.onnx imunet_relu_qdq.onnx
```

```text
imunet.onnx              float    FIX: ops:Div,Erf
dynamo_op18_noopt.onnx   float    FIX: ops:BatchNormalization,Constant,Div,Erf
dynamo_op20.onnx         float    FIX: opset20 | ops:Gelu
dyn_dynamo_ex2.onnx      float    FIX: dynamic:imu | dynamic:logits | ops:Div,Erf
gate_cond.onnx           float    FIX: ops:Abs,Greater,If
bigcnn_ext.onnx          float    FIX: external-data
imunet_relu.onnx         float    DEPLOYABLE
imunet_relu_qdq.onnx     int8-QDQ DEPLOYABLE
```

출력에서 볼 것:

- 이 노트에서 만난 문제들이 한 화면에 모였다: GELU 분해(Div·Erf), 접히지 않은 BN·Constant, opset 20의 Gelu, dynamic shape, 제어 흐름(If), external data.
- 최종 후보는 `imunet_relu.onnx`(fp32)와 `imunet_relu_qdq.onnx`(int8 QDQ). 실제 NPU가 int8만 받는다면 후자가 넘길 파일이다.
- lint는 **필요조건**일 뿐이다. 통과해도 벤더 툴이 특정 속성 조합(예: depthwise의 특정 stride, 채널 수 정렬 — C6 7.5절)을 거부할 수 있다. 실제 판정은 벤더 컴파일러 리포트와 `disable_cpu_ep_fallback` 같은 강제 장치로 한다(F8). 이 스크립트는 CI의 첫 관문으로 둔다.

---

## 11. 임베디드 관점에서 다시 보기

ONNX·ORT가 웨어러블급 기기에서 어디에 놓이는지 정리한다(예를 들어 Hark 같은 기기라면 — 구조는 추정).

| 층 | 무엇이 도나 | ONNX의 역할 |
|---|---|---|
| always-on MCU (Cortex-M) | TFLite Micro + CMSIS-NN, 또는 손으로 쓴 C | ONNX는 **중간 산출물**. MCU에는 ORT가 올라가지 않는다(OS·malloc·C++ 런타임 요구). ONNX → (변환) → TFLite/C 배열 (F1, F2) |
| DSP / NPU (예: Hexagon HTP) | 벤더 런타임 (QNN 등) | ONNX가 벤더 컴파일러의 **입력**. 오프라인 컴파일 → context binary를 펌웨어/앱 이미지에 포함 (F4) |
| 응용 프로세서 (Android/Linux) | ORT (CPU/XNNPACK/QNN EP), 또는 TFLite/ExecuTorch | ONNX(또는 `.ort`)를 **직접 실행**. minimal build로 크기 절감 |
| 개발 PC / CI | ORT CPU | golden 비교, lint, 회귀 테스트 |

펌웨어 엔지니어가 챙길 숫자들:

- **바이너리 크기**: full ORT는 MB 단위다. 앱 프로세서에서는 minimal build + 필요한 op만(9.2절). MCU에는 처음부터 다른 런타임.
- **세션 생성 시간**: 프로파일 JSON(예제 13, 16)에는 `session_initialization` 이벤트가 있다. CoreML EP로 imunet을 열었을 때 별도로 한 번 본 값이 약 230 ms였다(부하 있는 환경의 단일 관찰). 부팅 시간 예산에 들어간다. QNN의 context binary 캐시(6.6절)나 `.ort` 사전 변환이 이걸 줄이는 장치다.
- **메모리**: ORT는 arena allocator로 중간 텐서를 재사용한다(C6 10절의 memory planning과 같은 생각). 기기에서는 peak RSS를 실측한다.
- **스레드**: `intra_op_num_threads`를 기본값(코어 수)으로 두면 다른 실시간 작업(오디오 파이프라인)과 코어를 다툰다. 웨어러블에서는 명시적으로 작게, 가능하면 코어 affinity까지.
- **결정성**: 같은 입력에 같은 출력이 나오는가? fp32 CPU EP는 스레드 수에 따라 합산 순서가 바뀔 수 있다. 회귀 테스트는 tolerance로 비교한다(C8 3절).

C로 ORT를 쓰는 모습도 알아 두면 좋다. ORT는 C API(`onnxruntime_c_api.h`)를 제공하고, 앱 프로세서 쪽 C/C++ 코드는 대략 "환경 생성 → 세션 옵션 → 세션 생성(모델 경로 또는 메모리 버퍼) → 입력 텐서 생성(사전 할당 버퍼 래핑) → Run → 출력 읽기"의 순서다. 메모리 버퍼에서 세션을 만들 수 있으므로, 모델을 펌웨어 이미지의 읽기 전용 섹션에 넣고 그 주소를 넘기는 구성도 가능하다(F7의 링커 배치와 연결). 이 노트에서는 C API 코드를 빌드·실행하지 않았으므로 함수 이름은 ORT C API 문서로 확인한다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| batch=1 예제 입력으로 dynamic export | 입력은 dynamic인데 batch 3에서 Reshape 에러 | `torch.export`의 0/1 특수화로 shape 상수가 박힘 | dynamic 차원은 예제 입력에서 2 이상. export 후 다른 batch로 실행 확인 (예제 6) |
| 데이터 의존 `if`가 있는 모델 export | export 성공, 일부 입력에서 출력이 틀림 | 예제 입력이 탄 분기만 그래프에 고정 (TracerWarning·draft export 경고뿐) | `torch.where`/`torch.cond`, 또는 분기를 펌웨어로 (예제 7) |
| opset을 `version_converter`로 낮춤 | 로드 실패 또는 조용히 다른 계산 | 어댑터 없음·속성↔입력 변환 누락 | 타깃 opset으로 다시 export, 그 후 수치 비교 (예제 4) |
| 최신 `onnx`로 만든 파일을 구버전 런타임에 | `Unsupported model IR version` | IR version이 런타임보다 높음 | `ir_version`을 낮추거나 런타임 업그레이드, 버전 매트릭스 문서화 (예제 2) |
| `external_data` 기본값 그대로 배포 | `.data` 누락으로 로드 실패, 또는 EP가 노드를 거부해 느려짐 | torch 2.8 기본 `external_data=True` | 작은 모델은 `external_data=False`. 큰 모델은 두 파일을 함께 관리 (예제 13) |
| 없는 EP를 providers에 지정 | 에러 없이 CPU로 실행, 전력·지연이 예상과 다름 | 미설치 EP는 경고 후 무시 | `get_providers()[0]` assert, `disable_cpu_ep_fallback` (예제 11, 14) |
| 지원 비율만 보고 EP 성능 판단 | 85% 지원인데 CPU보다 느림 | partition이 여러 조각, 경계 비용 | verbose 로그로 partition 수 확인, 미지원 op 제거 (예제 12, 13) |
| 작은 모델을 무조건 가속기로 | 가속기 경로가 더 느림 | 호출·동기화 고정비 > 계산 이득 | 모델 크기별로 실측, 작은 모델은 CPU/DSP (예제 13의 imunet_relu) |
| ORT EXTENDED 최적화 결과를 벤더 툴에 전달 | 벤더 툴이 `FusedConv` 등을 모름 | `com.microsoft` contrib op | 넘길 파일은 exporter 정리 또는 ORT BASIC까지만 (C6 6절) |
| export 성공을 검증 완료로 착각 | 기기에서 정확도 하락 | 분기 고정, 전처리·layout 차이 | 대표 입력 여러 개로 PyTorch vs ORT, 지표 기준 문서화 (8절, C8) |

---

## 13. 면접에서 이렇게 말한다

**Q.** "What's inside an ONNX file?"

**A.** protobuf로 직렬화된 ModelProto 하나다. 안에 IR version, domain별 opset 선언, 생산자 정보가 있고, GraphProto에 위상 정렬된 노드 리스트, 그래프 입력·출력의 타입과 shape, 가중치(initializer), 선택적으로 중간 텐서 shape(value_info)가 들어 있다. 노드끼리는 포인터가 아니라 텐서 이름으로 연결된다. 2GB를 넘는 가중치는 external data 파일로 뺀다.

> An ONNX file is a single protobuf ModelProto. It declares the IR version and an opset per domain, and contains a GraphProto: a topologically sorted list of nodes, typed graph inputs and outputs, initializers holding the weights, and optional value_info for intermediate shapes. Edges are just tensor names — a node's output name matching another node's input name. Large weights can live in an external data file because protobuf messages cap at 2 GB.

**Q.** "Why does the opset version matter?"

**A.** opset은 op 사전의 버전이라 같은 op 이름이라도 입력·속성·의미가 다르다. 예를 들어 opset 18에서 ReduceMean의 axes가 속성에서 입력으로 바뀌었고, Gelu는 opset 20에 처음 생겼다. 벤더 컨버터는 특정 opset 범위만 지원하니, export 시점에 타깃 툴과의 교집합에서 opset을 골라야 한다. 사후 변환기는 실제로 깨진 모델을 만드는 걸 본 적이 있어서, 다시 export하고 수치를 비교한다.

> The opset versions the operator dictionary, so the same op name can have different inputs, attributes or semantics — ReduceMean moved axes from an attribute to an input at opset 18, and Gelu only exists from opset 20. Every converter supports a range, so I pick the opset at export time inside the intersection of all tools in the pipeline. I avoid after-the-fact version conversion — I've seen it produce a model that fails to load — and I always re-check numerics after any change.

**Q.** "What is an execution provider, and how does partitioning work?"

**A.** EP는 ORT의 하드웨어 백엔드다. 세션을 만들 때 providers 리스트 순서대로 각 EP가 자기가 받을 수 있는 노드의 연속 구간을 고르고, compiling EP는 그 구간을 통째로 자기 형식으로 컴파일한다. 남은 노드는 CPU EP로 간다. 중요한 건 지원 비율이 아니라 partition 개수와 경계 위치다. 실제로 노드 13개 중 11개가 CoreML에 올라갔지만 Erf와 Unsqueeze 때문에 세 조각이 나서 CPU보다 느렸고, 그 op들을 없애자 한 덩어리가 됐다. 배포 전에는 verbose 로그와 fallback 금지 옵션으로 확인한다.

> An execution provider is a hardware backend in ONNX Runtime. At session creation, each EP in priority order claims the connected subgraphs it can run; compiling EPs like CoreML or QNN turn each claimed subgraph into one fused node they compile as a whole, and whatever is left goes to the CPU EP. What matters is the number of partitions and where the boundaries fall, not the coverage percentage — I had 11 of 13 nodes on CoreML, but Erf and Unsqueeze split it into three partitions and it was slower than CPU. Rewriting those ops gave one partition. I verify with verbose logs, profiling, and the disable-CPU-fallback session option in CI.

**Q.** "Your PyTorch-to-ONNX export failed — or succeeded but gives wrong results. How do you debug?"

**A.** 먼저 eval 모드와 입력 shape·dtype을 확인한다. 에러면 메시지의 aten op 이름과 opset을 읽고, opset을 올려 보거나 exporter를 바꿔 보고, `torch.export`를 단독으로 돌려 데이터 의존 분기를 찾는다. 미지원 op는 등가 op로 다시 쓴다 — 예를 들어 rfft를 상수 cos/sin 행렬의 MatMul로. 성공했는데 틀리면 대부분 분기가 고정됐거나 batch-1 특수화 같은 shape 문제라서, 여러 입력으로 PyTorch와 ORT를 비교하고 중간 텐서를 출력으로 승격해 처음 갈라지는 레이어를 찾는다.

> First I check eval mode and the exact input shapes and dtypes. For a hard failure I read the aten op and opset in the error, try a higher opset or the other exporter, and run torch.export alone to surface data-dependent control flow. Unsupported ops get rewritten into equivalent supported ones — for example rfft as two matmuls with constant cosine and sine matrices. A silent success is more dangerous: branches get baked in, or a batch-1 example specializes a dynamic dimension. So I always compare PyTorch and ORT on several representative inputs, and bisect by promoting intermediate tensors to graph outputs.

**Q.** "Why use ONNX as the interchange format for NPU toolchains?"

**A.** 프레임워크와 벤더 컴파일러 사이의 중립적인 계약서이기 때문이다. 표준 op 사전과 버전이 있고, 어떤 언어로든 열어 검사할 수 있고, ORT CPU라는 레퍼런스 런타임이 있어서 벤더 결과를 비교할 기준이 생긴다. QDQ로 양자화 파라미터도 표준 방식으로 실어 보낼 수 있다. 다만 벤더는 op·opset·속성 조합의 일부만 지원하므로, 넘기기 전에 static shape, opset 범위, 상수 접힘, 제어 흐름 없음, external data 여부를 lint하고 벤더 리포트로 fallback을 확인한다.

> It's a neutral contract between the training framework and the vendor compiler: a standard, versioned operator set, a format any tool can parse and inspect, and a reference runtime — ORT on CPU — to compare the vendor's output against. Quantization parameters travel in a standard way via QDQ. The catch is that each vendor supports only a subset of ops, opsets and attribute combinations, so before hand-off I lint for static shapes, opset range, folded constants, no control flow and no external data, and then confirm zero fallback from the vendor's compile report.

**Q.** "When would you NOT run a model on the accelerator?"

**A.** 모델이 아주 작으면 가속기 호출·동기화 고정비가 계산 이득보다 크다. 실제로 수만 MAC짜리 IMU 모델은 CoreML 한 덩어리로 올려도 CPU보다 3배 가까이 느렸고, 9백만 MAC급 Conv가 여러 개인 CNN은 11배 빨랐다. 그래서 always-on 작은 모델은 MCU나 DSP에, 큰 모델은 NPU에 두고, 판단은 항상 기기 실측으로 한다.

> When the model is tiny, the fixed cost of dispatching to and synchronizing with the accelerator outweighs the compute savings. In my measurements a tiny IMU classifier was almost 3x slower fully offloaded to CoreML than on CPU, while a small CNN with several multi-million-MAC convolutions was about 11x faster. So always-on tiny models belong on the MCU or DSP, bigger ones on the NPU — and I decide by measuring on the target, not by assumption.

---

## 14. 직접 해보기

1. **손계산**: `nn.Conv1d(8, 16, 3)` + `nn.Linear(16, 4)`를 export했을 때 initializer에 들어갈 가중치·bias 원소 수 합계는? (BN 없음, 상수 initializer 제외)
   정답: 8·16·3 + 16 + 16·4 + 4 = 384 + 16 + 64 + 4 = 468.

2. **바이트 해독**: protobuf 바이트 `0x08 0x07`이 ModelProto 맨 앞에 있다면 무슨 뜻인가? `0x3A`로 시작하는 필드는 ModelProto의 몇 번 필드이고 wire type은?
   정답: 필드 1(ir_version) = 7. `0x3A` = 0011 1010 → 필드 7(graph), wire type 2(길이 구분).

3. **코드**: 예제 2의 MLP에서 `Relu`를 `Clip(min=0, max=6)`(ReLU6)으로 바꾸는 그래프 수술을 하라. opset 11 이후 Clip의 min/max는 속성이 아니라 입력이다 — initializer 두 개를 추가해야 한다. ORT로 numpy `np.clip(h, 0, 6)` 결과와 비교하라.
   힌트: `h.make_node("Clip", ["h1", "clip_min", "clip_max"], ["a1"])`, 스칼라 initializer는 `nh.from_array(np.array(0, np.float32), "clip_min")`.

4. **코드**: 예제 13의 `imunet.onnx`를 opset 20으로 export(예제 5의 `dynamo_op20.onnx`)해서 CoreML EP verbose 로그를 다시 보라. `Gelu` 노드는 지원되는가? partition 수는 몇 개인가? 결과를 6.3절 그림과 비교해 "같은 수학, 다른 export → 다른 fallback"을 직접 확인하라.
   힌트: 예제 12의 코드에서 파일 이름만 바꾼다. 결과는 ORT 버전에 따라 다를 수 있으니 직접 본 것을 기록한다.

5. **설계**: 웨어러블 wake-word 모델(오디오 1초 → mel → 작은 CNN)을 Qualcomm NPU에 올린다고 하자. deploy lint 규칙(10.3절)에 무엇을 추가하겠는가? 3개 이상.
   정답 예: `DFT`·`STFT` 금지(전처리는 DSP로), `If`/`Loop` 금지, QDQ 노드가 첫 Conv 앞과 마지막 Gemm 뒤에 있는지, 입력 shape가 정확히 `[1, 1, n_mels, frames]`인지, int16 activation이 필요한 레이어 표시.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| ONNX | Open Neural Network Exchange | 모델 그래프 + 가중치를 담는 교환 포맷 규격과 op 사전 |
| initializer | 그래프에 내장된 상수 텐서 | 가중치, shape 벡터, 스칼라 상수 |
| IR version | 파일 형식 버전 | 런타임이 읽을 수 있는 상한이 있다 (ORT 1.19 = 10) |
| opset | op 사전의 버전 (domain별) | 각 op는 opset 이하의 최신 정의로 해석된다 |
| domain | op 네임스페이스 | `ai.onnx`(표준), `ai.onnx.ml`, `com.microsoft`(ORT contrib) |
| contrib op | ORT 전용 op | `FusedConv` 등. 다른 툴은 모를 수 있다 |
| external data | 가중치를 별도 파일에 저장 | protobuf 2GB 한계 회피. torch 2.8 export 기본값 |
| legacy exporter | TorchScript 기반 export | `dynamo=False`, tracing + symbolic 함수 |
| dynamo exporter | `torch.export` 기반 export | `dynamo=True`, FX graph + torchlib + optimizer |
| 0/1 specialization | 크기 0·1 차원 상수화 | batch-1 예제로 dynamic export 시 함정 |
| InferenceSession | ORT 실행 객체 | 생성 시 최적화·partition·계획, `run()`은 가볍다 |
| execution provider (EP) | ORT 하드웨어 백엔드 | CPU, CoreML, QNN, XNNPACK, NNAPI, TensorRT 등 |
| partitioning | 노드를 EP에 배정 | providers 순서대로 GetCapability, 남은 건 CPU |
| fallback | 가속기가 못 받은 노드를 CPU로 | 경계마다 복사·동기화 비용 |
| IOBinding | 입출력 버퍼 사전 바인딩 | 할당·복사 제거, 장치 메모리 EP에서 효과 큼 |
| ORT format (`.ort`) | ORT 내부 flatbuffer 모델 | minimal build가 읽는 형식 |
| minimal build | 필요한 op만 넣은 ORT 빌드 | 바이너리 크기 절감 |
| QDQ | QuantizeLinear/DequantizeLinear 쌍 | 런타임 중립적인 양자화 표현 (C2) |
| EPContext | EP 컴파일 결과를 담은 노드 | QNN context binary 캐시 등 |

---

## 16. 요약 & 체크리스트

ONNX는 protobuf로 직렬화된 ModelProto 하나에 IR version, domain별 opset, 이름으로 연결된 노드 리스트, initializer(가중치)를 담는 교환 포맷이다. opset은 op 사전의 버전이라 같은 op 이름도 의미가 바뀌고(ReduceMean-18, Gelu-20), 파이프라인의 모든 툴이 지원하는 opset의 교집합에서 export해야 한다 — 사후 version converter는 이 노트에서 세 번 모두 실패했다. PyTorch export는 예제 입력으로 한 번 따라간 경로를 굳히는 일이라, 데이터 의존 분기와 batch-1 특수화가 **에러 없이** 틀린 그래프를 만든다. ONNX Runtime은 세션 생성 때 최적화·partitioning·실행 계획을 끝내고, providers 순서대로 EP가 노드 구간을 가져가며 남은 건 CPU로 간다. 성능은 지원 비율이 아니라 partition 수와 경계 위치가 정하고(Erf 하나가 그래프를 세 조각 냄), 작은 모델은 가속기로 보내면 오히려 느리고, external data 같은 사소한 차이도 전면 fallback을 부른다. 확인은 항상 verbose 로그, 프로파일 JSON, fallback 금지 옵션, PyTorch 대비 수치 지표로 한다. 벤더 NPU로 넘길 ONNX는 static shape, 범위 안 opset, 표준 domain, 접힌 상수, 제어 흐름 없음, 내장 가중치, 일관된 QDQ를 갖춰야 하고, 이걸 lint로 자동화해 CI 첫 관문에 둔다.

- [ ] `.onnx` 파일을 열어 ir_version, opset_import, 입력·출력 shape, initializer, 노드 리스트를 출력할 수 있다
- [ ] protobuf 태그 바이트(`0x08`, `0x12`, `0x3A`)를 필드 번호와 wire type으로 해독할 수 있다
- [ ] `onnx.helper`로 작은 그래프를 만들고 checker·shape inference·ORT로 검증할 수 있다
- [ ] 그래프 수술(이름 변경, 출력 추가, op 교체) 후 원본과 수치를 비교할 수 있다
- [ ] legacy와 dynamo exporter의 차이, 그리고 데이터 의존 분기·batch-1 특수화 함정을 설명하고 재현할 수 있다
- [ ] opset과 IR version의 차이를 설명하고, 타깃 툴 범위에 맞춰 opset을 고를 수 있다
- [ ] SessionOptions의 최적화 레벨·스레드·프로파일링·config entry를 설정할 수 있다
- [ ] EP partitioning을 verbose 로그와 프로파일로 확인하고, fallback을 만드는 op를 찾아 없앨 수 있다
- [ ] PyTorch vs ORT CPU vs 가속기 EP의 수치를 max abs, cosine, SQNR, top-1로 비교하고 기준을 정할 수 있다
- [ ] deployable ONNX 체크리스트를 말하고 lint 스크립트로 점검할 수 있다

---

## 참고 자료

- ONNX 공식 문서와 연산자 레퍼런스: https://onnx.ai/onnx/ (Operators 페이지에서 op별 버전 변경 이력 확인)
- ONNX 저장소의 버전 규칙 문서(`docs/Versioning.md`)와 `onnx/onnx.proto` 스키마: https://github.com/onnx/onnx
- ONNX Runtime 문서 (세션 옵션, 그래프 최적화, execution provider별 페이지, ORT format, 모바일 빌드): https://onnxruntime.ai/docs/
- ONNX Runtime 저장소: https://github.com/microsoft/onnxruntime
- onnxruntime-genai 저장소: https://github.com/microsoft/onnxruntime-genai
- PyTorch ONNX export 문서 (`torch.onnx.export`, dynamo exporter): https://pytorch.org/docs/stable/onnx.html
- PyTorch `torch.export` 문서 (dynamic shapes, 0/1 specialization, `torch.cond`): https://pytorch.org/docs/stable/export.html
- Protocol Buffers 인코딩 설명 (태그, varint, wire type): https://protobuf.dev/programming-guides/encoding/
- Netron (그래프 뷰어): https://netron.app
- 관련 노트: A5 9절, C2 5절, C6 2·6·12절, C8 2·4절, F1, F4, F6, F8
