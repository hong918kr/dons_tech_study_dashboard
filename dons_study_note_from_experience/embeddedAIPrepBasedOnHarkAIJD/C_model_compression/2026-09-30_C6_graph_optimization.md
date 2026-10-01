# C6. 그래프 최적화 — 컴파일러가 모델을 기기에 맞게 바꾸는 방법

> **이 노트를 다 읽으면**: 모델을 "op 노드 + tensor 엣지"의 dataflow graph로 읽고 `torch.export`·ONNX 그래프를 직접 열어 볼 수 있다 · constant folding, op fusion, dead code 제거, layout 변환, lowering이 그래프를 어떻게 바꾸는지 before/after로 보여 주고 수치가 같음을 검증할 수 있다 · tensor lifetime으로 arena 크기를 계산하는 작은 memory planner를 직접 짜고, 실행 순서가 peak memory를 바꾸는 이유를 설명할 수 있다 · 벤더 컴파일러 리포트(NPU vs CPU 매핑, SRAM 사용량)를 읽고 CPU fallback을 찾아낼 수 있다
> **JD 연결**: "Work with platform vendors to bring up toolchains, SDKs and new accelerator", "Deploying workloads on NPUs or specialized accelerators", "Optimizing models for MCUs & edge processors" · study_prep_list C6 행 — op fusion (conv+BN+ReLU), constant folding, layout NHWC vs NCHW, 채널 정렬(8/16/32 배수), operator lowering, unsupported op 대체 (D2 memory planner, E5 컴파일러의 역할, F8 fallback과 직결)
> **Don 기준 난이도**: 컴파일러 IR·링커·메모리 맵·DMA·"도구가 만든 결과물을 열어서 확인하는 습관"은 이미 강함 / ML 그래프 포맷(FX, ONNX), 각 최적화 패스의 이름과 조건, NPU가 원하는 모양(static shape, 채널 정렬, 지원 op)은 새로 배움
> **선행 노트**: A1 (NCHW/NHWC·stride), A5 (`torch.export`·ONNX export), B1 (BN folding 유도), B2 (conv 계산·MobileNet 블록)

---

## 0. 큰 그림 — 이게 왜 필요한가

학습이 끝난 PyTorch 모델은 "Python 코드 + 가중치"다. 기기에는 Python이 없다. 그래서 배포는 항상 **모델을 그래프로 뽑아내고(export), 그 그래프를 기기에 맞게 고쳐 쓰고(optimize/lower), 기기용 바이너리로 굳히는(compile)** 과정을 거친다. 이 노트는 가운데 단계 — **그래프를 고쳐 쓰는 일** — 을 다룬다.

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="c6m1" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <rect x="10" y="20" width="200" height="60" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="110" y="45" font-size="13" text-anchor="middle">PyTorch nn.Module</text> <text x="110" y="64" font-size="12" text-anchor="middle">eager · Python 코드 + 가중치</text> <rect x="240" y="20" width="200" height="60" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="340" y="45" font-size="13" text-anchor="middle">torch.export</text> <text x="340" y="64" font-size="12" text-anchor="middle">FX graph · ATen op (A5)</text> <rect x="470" y="20" width="200" height="60" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="570" y="45" font-size="13" text-anchor="middle">ONNX / TFLite</text>
<text x="570" y="64" font-size="12" text-anchor="middle">표준 op 그래프 + initializer</text> <line x1="210" y1="50" x2="238" y2="50" stroke="currentColor" stroke-width="1.5" marker-end="url(#c6m1)"/> <line x1="440" y1="50" x2="468" y2="50" stroke="currentColor" stroke-width="1.5" marker-end="url(#c6m1)"/> <line x1="570" y1="80" x2="570" y2="128" stroke="currentColor" stroke-width="1.5" marker-end="url(#c6m1)"/> <rect x="470" y="130" width="200" height="70" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="570" y="155" font-size="13" text-anchor="middle">그래프 최적화 (이 노트)</text> <text x="570" y="174" font-size="12" text-anchor="middle">fold · fuse · DCE · layout</text> <text x="570" y="191" font-size="12" text-anchor="middle">lowering · static shape</text> <rect x="240" y="130" width="200" height="70" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="340" y="155" font-size="13" text-anchor="middle">벤더 컴파일러</text> <text x="340" y="174" font-size="12" text-anchor="middle">partition · tiling · schedule</text> <text x="340" y="191" font-size="12" text-anchor="middle">memory plan (arena)</text> <rect x="10" y="130" width="200" height="70" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="110" y="155" font-size="13" text-anchor="middle">기기 바이너리 + 런타임</text> <text x="110" y="174" font-size="12" text-anchor="middle">Vela .tflite · QNN context</text> <text x="110" y="191" font-size="12" text-anchor="middle">TFLM · ORT · ExecuTorch</text> <line x1="470" y1="165" x2="442" y2="165" stroke="currentColor" stroke-width="1.5" marker-end="url(#c6m1)"/> <line x1="240" y1="165" x2="212" y2="165" stroke="currentColor" stroke-width="1.5" marker-end="url(#c6m1)"/>
<text x="10" y="235" font-size="12">파랑: 프레임워크가 만든 그래프 · 주황: 그래프를 기기에 맞게 고치는 단계 · 초록: 기기에서 도는 것</text>
</svg>
```

그림 1 — 배포 파이프라인. 이 노트의 대상은 주황색 두 칸이다. 실제 도구에서는 두 칸의 경계가 흐릿하다(예: TFLite converter도 fusion을 하고, Vela도 layout을 바꾼다).

Don에게 가장 가까운 비유는 **C 컴파일러**다.

| C 컴파일러 (Don이 아는 것) | ML 그래프 컴파일러 (이 노트) |
|---|---|
| 소스 → IR (GCC GIMPLE, LLVM IR) | 모델 → 그래프 IR (FX graph, ONNX, TFLite flatbuffer) |
| constant folding: `#define`·상수 식을 컴파일 타임에 계산 | constant folding: shape 계산·상수 transpose를 export 타임에 계산 |
| dead code elimination | 출력에 안 닿는 노드·Identity·Dropout 제거 |
| inlining, loop fusion | op fusion (Conv+BN+ReLU → 커널 하나) |
| instruction selection: IR → 타깃 ISA 명령 | lowering: 고수준 op → NPU가 아는 primitive op |
| register allocation (live range 분석) | memory planning (tensor lifetime → arena offset) |
| instruction scheduling | op scheduling (실행 순서 → peak memory) |
| 링커 맵 파일: 섹션별 크기 | 컴파일러 리포트: op별 NPU/CPU 매핑, SRAM 사용량 |

또 하나의 비유: **넷리스트**. 그래프는 게이트(op)와 와이어(tensor)의 넷리스트이고, 합성 툴이 게이트를 합치고(fusion), 상수 입력 게이트를 없애고(constant propagation), 타깃 셀 라이브러리로 매핑(lowering)하듯이 ML 컴파일러도 같은 일을 한다. 셀 라이브러리에 없는 게이트가 있으면 합성이 안 되듯, **NPU가 모르는 op가 있으면 그 부분은 CPU로 떨어진다(fallback)**.

왜 edge ML 엔지니어가 이걸 알아야 하나. 벤더 컴파일러는 대부분 블랙박스에 가깝고, 문제가 생기면 이런 증상으로 드러난다.

- "NPU를 쓰는데 CPU보다 느리다" → op 하나가 fallback 되어 NPU↔CPU 왕복이 생겼다 (12절).
- "SRAM에 안 들어간다고 컴파일 실패" → memory plan·실행 순서·tiling 문제 (10, 11절).
- "변환 후 정확도가 틀어졌다" → layout(NCHW/NHWC)·근사 lowering·fusion 순서 문제 (7, 8절, C8).
- "노드가 수백 개인데 왜 이렇게 많지" → export 잔재가 안 접혔다 (2, 3절).

> 실행 환경: 모든 Python 예제는 이 폴더의 `.venv/bin/python` (Python 3.9, torch 2.8.0, onnx 1.19.1, onnxscript 0.7.2, onnxruntime 1.19.2, numpy 2.0.2, macOS arm64)로 실제 실행한 출력이다. 예제는 `.onnx` 파일을 현재 디렉터리에 쓰므로 **노트 폴더가 아닌 임시 디렉터리**(예: `mkdir -p /tmp/c6 && cd /tmp/c6`)에서 실행한다. `torch.onnx.export`가 stderr/stdout에 찍는 `[torch.onnx] ...` 진행 메시지와 "torchvision is not installed" 경고는 생략했다. C 예제는 `cc -std=c11 -Wall -Wextra -O2 ... -lm`으로 경고 0개로 컴파일했다.

---

## 1. 모델은 dataflow graph다

### 1.1 직관

B2의 작은 CNN을 생각하자. `forward()`는 Python 코드지만, 실제로 일어나는 일은 "텐서가 op를 차례로 통과한다"뿐이다. 이걸 그림으로 그리면 **노드 = op(Conv, Relu, …), 엣지 = tensor(데이터)**인 방향 그래프가 된다. 사이클이 없다(DAG).

```
   x [1,1,16,16]
      │
   ┌──▼───┐   weight [8,1,3,3] (상수)
   │ Conv │◄──────────
   └──┬───┘
      │ t1 [1,8,16,16]
   ┌──▼───┐   γ, β, μ, σ² (상수)
   │  BN  │◄──────────
   └──┬───┘
      │ t2 [1,8,16,16]
   ┌──▼───┐
   │ Relu │
   └──┬───┘
      ▼  ... → Conv → BN → Relu → ReduceMean → Gemm → y [1,4]
```

용어 몇 개:

- **node (op)**: 계산 하나. `Conv`, `Relu`, `Gemm`처럼 이름(op type)과 속성(stride, padding…)을 가진다.
- **edge (tensor)**: 노드 사이를 흐르는 데이터. 이름·dtype·shape를 가진다.
- **initializer / constant**: 실행 전에 값이 정해진 텐서 = 가중치. 펌웨어로 치면 `.rodata`(flash)에 들어가는 것.
- **graph input / output**: 런타임에 채워지는 텐서(센서 입력)와 결과.
- **topological order**: 모든 엣지가 "앞 → 뒤"를 향하도록 노드를 한 줄로 세운 순서. 실행 순서의 후보다(11절에서 이 순서가 memory를 바꾼다).

C 컴파일러의 SSA IR과 같다. SSA에서 변수는 한 번만 정의되고, 여러 번 읽힌다. ONNX 텐서도 정확히 한 노드가 만들고 여러 노드가 읽는다. 그래서 컴파일러 최적화 기법(상수 전파, DCE, live range 분석)이 거의 그대로 들어온다.

### 1.2 예제 모델 — `tinynet.py`

이 노트 내내 쓰는 모델이다. 스펙트로그램 패치(1채널 16×16)를 받는 conv-BN-ReLU 두 블록 + global average pool + FC. BN 통계는 "학습된 것처럼" 무작위로 채웠다. `bias` 인자는 3·6절에서 차이를 보여 주려고 넣었다. 같은 디렉터리에 저장해 두고 뒤 예제에서 import 한다.

```python
import torch, torch.nn as nn
class TinyNet(nn.Module):
    """스펙트로그램용 작은 CNN: conv-BN-ReLU ×2 → GAP → FC"""
    def __init__(self, bias=False):
        super().__init__()
        self.conv1 = nn.Conv2d(1, 8, 3, padding=1, bias=bias)
        self.bn1 = nn.BatchNorm2d(8)
        self.conv2 = nn.Conv2d(8, 16, 3, padding=1, bias=bias)
        self.bn2 = nn.BatchNorm2d(16)
        self.fc = nn.Linear(16, 4)
    def forward(self, x):
        x = torch.relu(self.bn1(self.conv1(x)))
        x = torch.relu(self.bn2(self.conv2(x)))
        x = x.mean(dim=(2, 3))                  # global average pool
        return self.fc(x)
def make(bias=False):
    torch.manual_seed(0)
    m = TinyNet(bias)
    with torch.no_grad():                      # '학습된' BN 통계 흉내
        for bn in (m.bn1, m.bn2):
            bn.running_mean.uniform_(-0.5, 0.5); bn.running_var.uniform_(0.5, 2.0)
            bn.weight.uniform_(0.5, 1.5); bn.bias.uniform_(-0.2, 0.2)
    return m.eval()
```

### 1.3 `torch.export`로 그래프 꺼내 보기 (예제 1)

무엇을 확인하는 코드인지: `torch.export`가 만든 FX graph의 노드를 하나씩 찍어서, Python 코드가 정말 "op 노드의 나열"이 되는지 본다.

```python
import torch
from tinynet import make
m = make()
x = torch.randn(1, 1, 16, 16)
ep = torch.export.export(m, (x,))
for n in ep.graph.nodes:
    if n.op == "call_function":
        args = [a.name if hasattr(a, "name") else a for a in n.args]
        print(f"{n.name:<22} = {str(n.target):<32} {args}")
    else:
        print(f"[{n.op}] {n.name}")
print("max|export - eager| =", (ep.module()(x) - m(x)).abs().max().item())
```

```text
[placeholder] p_conv1_weight
[placeholder] p_bn1_weight
[placeholder] p_bn1_bias
[placeholder] p_conv2_weight
[placeholder] p_bn2_weight
[placeholder] p_bn2_bias
[placeholder] p_fc_weight
[placeholder] p_fc_bias
[placeholder] b_bn1_running_mean
[placeholder] b_bn1_running_var
[placeholder] b_bn1_num_batches_tracked
[placeholder] b_bn2_running_mean
[placeholder] b_bn2_running_var
[placeholder] b_bn2_num_batches_tracked
[placeholder] x
conv2d                 = aten.conv2d.default              ['x', 'p_conv1_weight', None, [1, 1], [1, 1]]
batch_norm             = aten.batch_norm.default          ['conv2d', 'p_bn1_weight', 'p_bn1_bias', 'b_bn1_running_mean', 'b_bn1_running_var', False, 0.1, 1e-05, True]
relu                   = aten.relu.default                ['batch_norm']
conv2d_1               = aten.conv2d.default              ['relu', 'p_conv2_weight', None, [1, 1], [1, 1]]
batch_norm_1           = aten.batch_norm.default          ['conv2d_1', 'p_bn2_weight', 'p_bn2_bias', 'b_bn2_running_mean', 'b_bn2_running_var', False, 0.1, 1e-05, True]
relu_1                 = aten.relu.default                ['batch_norm_1']
mean                   = aten.mean.dim                    ['relu_1', [2, 3]]
linear                 = aten.linear.default              ['mean', 'p_fc_weight', 'p_fc_bias']
[output] output
max|export - eager| = 0.0
```

출력에서 볼 것:

- 가중치(`p_...`)와 BN 버퍼(`b_...`)가 **그래프 입력(placeholder)으로 "들어 올려져(lifted)"** 있다. 입력 `x`도 placeholder다. 즉 FX graph는 "순수 함수 `f(params, buffers, x)`"다. 펌웨어로 치면 전역 변수를 전부 함수 인자로 바꾼 것.
- `aten.batch_norm`의 여섯 번째 인자 `False`가 `training=False`다. eval 모드로 export했기 때문에 running 통계를 쓰는 추론용 BN이다 — 이게 fusion(4절)의 전제 조건이다.
- `conv2d`의 bias 인자가 `None`이다(`bias=False`). 이 작은 차이가 6절에서 ORT fusion을 막는다.

### 1.4 lowering의 첫 맛 — `run_decompositions()` (예제 2)

`torch.export`의 기본 그래프는 `aten.linear`, `aten.batch_norm` 같은 비교적 **고수준 op**를 쓴다. `run_decompositions()`를 부르면 PyTorch가 정의한 **Core ATen opset**(약 180여 개의 작은 op 집합)으로 쪼갠다. 벤더 백엔드(ExecuTorch 등)는 이 작은 집합만 구현하면 된다 — 명령어 집합을 줄이는 RISC 발상이다.

무엇을 확인하는 코드인지: decomposition 전후 op 목록과 수치 동일성.

```python
import torch
from tinynet import make
m = make(); x = torch.randn(1, 1, 16, 16)
ep = torch.export.export(m, (x,))
core = ep.run_decompositions()                 # Core ATen opset으로 lowering
def ops(g): return [str(n.target) for n in g.nodes if n.op == "call_function"]
print("before:", len(ops(ep.graph)), "ops");  print("after :", len(ops(core.graph)), "ops")
for o in ops(core.graph): print("  ", o)
print("max|core - eager| =", (core.module()(x) - m(x)).abs().max().item())
```

```text
before: 8 ops
after : 11 ops
   aten.convolution.default
   aten._native_batch_norm_legit_no_training.default
   <built-in function getitem>
   aten.relu.default
   aten.convolution.default
   aten._native_batch_norm_legit_no_training.default
   <built-in function getitem>
   aten.relu.default
   aten.mean.dim
   aten.permute.default
   aten.addmm.default
max|core - eager| = 0.0
```

출력에서 볼 것: `linear`가 `permute`(weight transpose) + `addmm`(bias + 행렬곱)으로, `batch_norm`이 "추론 전용 BN"(`_no_training`)과 결과 튜플에서 첫 원소를 꺼내는 `getitem`으로 쪼개졌다. 노드 수는 8 → 11로 **늘었다**. lowering은 노드를 줄이는 게 목적이 아니라 **백엔드가 구현해야 할 op 종류를 줄이는 것**이 목적이다. 줄이는 건 그다음 fusion의 몫이다.

---

## 2. ONNX 그래프 열어 보기

ONNX는 업계 교환 형식이다(A5 9절). 파일 하나에 **graph(node 리스트) + initializer(가중치) + opset 버전**이 protobuf로 들어 있다. 벤더 NPU 컴파일러의 입력으로 가장 흔하다(QNN converter, TensorRT, 여러 NPU 툴체인). Netron으로 그림을 볼 수도 있지만, 스크립트로 여는 법을 알아 두면 bring-up 때 diff·자동 검사가 된다.

### 2.1 노드·initializer 나열 (예제 3)

무엇을 확인하는 코드인지: TinyNet을 **최적화 없이**(`optimize=False`) ONNX로 export하고 노드와 initializer를 그대로 찍는다. export 직후의 "날 것" 그래프가 어떤 모습인지 보려는 것이다.

```python
import torch, onnx
from onnx import shape_inference, numpy_helper
from tinynet import make
m = make(); x = torch.randn(1, 1, 16, 16)
torch.onnx.export(m, (x,), "tiny.onnx", input_names=["x"], output_names=["y"],
                  dynamo=True, external_data=False, optimize=False, verbose=False)
g = onnx.load("tiny.onnx").graph
print("opset:", onnx.load("tiny.onnx").opset_import[0].version)
for n in g.node:
    print(f"{n.op_type:<20} in={list(n.input)} out={list(n.output)}")
print("initializers:")
for t in g.initializer:
    print(f"  {t.name:<22} {list(t.dims)}")
```

```text
opset: 18
Shape                in=['conv1.weight'] out=['val_0']
Constant             in=[] out=['val_1']
Expand               in=['val_0', 'val_1'] out=['val_2']
Constant             in=[] out=['val_3']
CastLike             in=['val_3', 'x'] out=['val_4']
Expand               in=['val_4', 'val_2'] out=['val_5']
Conv                 in=['x', 'conv1.weight', 'val_5'] out=['conv2d']
BatchNormalization   in=['conv2d', 'bn1.weight', 'bn1.bias', 'bn1.running_mean', 'bn1.running_var'] out=['getitem']
Relu                 in=['getitem'] out=['relu']
Shape                in=['conv2.weight'] out=['val_14']
Constant             in=[] out=['val_15']
Expand               in=['val_14', 'val_15'] out=['val_16']
CastLike             in=['val_3', 'relu'] out=['val_17']
Expand               in=['val_17', 'val_16'] out=['val_18']
Conv                 in=['relu', 'conv2.weight', 'val_18'] out=['conv2d_1']
BatchNormalization   in=['conv2d_1', 'bn2.weight', 'bn2.bias', 'bn2.running_mean', 'bn2.running_var'] out=['getitem_3']
Relu                 in=['getitem_3'] out=['relu_1']
Constant             in=[] out=['val_25']
Constant             in=[] out=['val_26']
Reshape              in=['val_26', 'val_25'] out=['val_27']
ReduceMean           in=['relu_1', 'val_27'] out=['mean']
Gemm                 in=['mean', 'fc.weight', 'fc.bias'] out=['y']
initializers:
  conv1.weight           [8, 1, 3, 3]
  bn1.weight             [8]
  bn1.bias               [8]
  conv2.weight           [16, 8, 3, 3]
  bn2.weight             [16]
  bn2.bias               [16]
  fc.weight              [4, 16]
  fc.bias                [4]
  bn1.running_mean       [8]
  bn1.running_var        [8]
  bn2.running_mean       [16]
  bn2.running_var        [16]
```

출력에서 볼 것:

- 진짜 계산은 `Conv, BatchNormalization, Relu` ×2 + `ReduceMean` + `Gemm` = 8개인데 노드는 22개다. 나머지 14개(`Shape → Expand`, `CastLike → Expand`, `Constant`, `Reshape`)는 **conv에 bias가 없어서 exporter가 "0으로 채운 bias 벡터"를 런타임에 만드는 코드**다. 즉 `conv1.weight`의 shape를 읽어서 `[8]`짜리 0 벡터를 Expand로 만든다. 입력에 전혀 의존하지 않는 계산 — **constant folding의 먹잇감**이다.
- `ReduceMean`의 axes도 `Constant → Reshape`로 런타임에 만든다. 역시 상수.
- `x.mean(dim=(2,3))` → `ReduceMean`, `nn.Linear` → `Gemm`: 이미 ONNX op로 한 번 lowering 된 모습이다.

### 2.2 exporter 최적화 켜기 + shape inference (예제 4)

무엇을 확인하는 코드인지: 같은 모델을 `optimize=True`(torch 2.8 기본값)로 export하면 노드가 몇 개가 되는지, 그리고 `onnx.shape_inference`로 모든 중간 텐서의 shape를 채워 본다. 두 그래프를 ONNX Runtime으로 돌려 PyTorch 출력과도 비교한다.

```python
import torch, onnx, collections, numpy as np, onnxruntime as ort
from onnx import shape_inference
from tinynet import make
m = make(); x = torch.randn(1, 1, 16, 16)
torch.onnx.export(m, (x,), "tiny_opt.onnx", input_names=["x"], output_names=["y"],
                  dynamo=True, external_data=False, optimize=True, verbose=False)
for f in ("tiny.onnx", "tiny_opt.onnx"):
    g = onnx.load(f).graph
    y = ort.InferenceSession(f, providers=["CPUExecutionProvider"]).run(None, {"x": x.numpy()})[0]
    print(f"{f:<14} nodes={len(g.node):>2} diff={np.abs(y - m(x).detach().numpy()).max():.0e}",
          dict(collections.Counter(n.op_type for n in g.node)))
g = shape_inference.infer_shapes(onnx.load("tiny_opt.onnx")).graph
inits = {t.name for t in g.initializer}
def dims(v): return [d.dim_value for d in v.type.tensor_type.shape.dim]
for v in list(g.input) + list(g.value_info) + list(g.output):
    if v.name not in inits: print(f"  {v.name:<10} {dims(v)}")
```

```text
tiny.onnx      nodes=22 diff=6e-08 {'Shape': 2, 'Constant': 5, 'Expand': 4, 'CastLike': 2, 'Conv': 2, 'BatchNormalization': 2, 'Relu': 2, 'Reshape': 1, 'ReduceMean': 1, 'Gemm': 1}
tiny_opt.onnx  nodes= 6 diff=6e-08 {'Conv': 2, 'Relu': 2, 'ReduceMean': 1, 'Gemm': 1}
  x          [1, 1, 16, 16]
  getitem    [1, 8, 16, 16]
  relu       [1, 8, 16, 16]
  getitem_3  [1, 16, 16, 16]
  relu_1     [1, 16, 16, 16]
  mean       [1, 16]
  y          [1, 4]
```

출력에서 볼 것:

- 22 → 6 노드. bias 생성 코드가 상수로 접혔고(constant folding), **BatchNormalization까지 사라졌다**. onnxscript optimizer가 BN을 앞 Conv의 weight·bias로 접었다(B1 7절의 BN folding). 출력 차이는 6e-8 — float32 반올림 수준, 같은 모델이다.
- 접힌 Conv의 출력 이름이 여전히 `getitem`(원래 BN 출력 이름)이다. 이름은 역사를 남긴다. 디버깅할 때 "이 텐서가 어디서 왔나"를 추적하는 단서다.
- `shape_inference`가 모든 중간 텐서 shape를 채웠다. **모든 shape가 숫자로 확정** — static shape다(9절). 이러면 컴파일러가 각 텐서의 바이트 수를 미리 알고 arena를 계획할 수 있다(10절).

---

## 3. Constant folding — 실행 전에 계산할 수 있는 건 미리 계산

### 3.1 정의

**입력(런타임 데이터)에 의존하지 않는 노드는 컴파일 타임에 한 번 계산해서 결과를 상수(initializer)로 바꾼다.** C의 `int n = 4 × 1024;`가 `4096`이 되는 것과 같다.

ML 그래프에서 자주 접히는 것:

- **shape 산술**: `Shape → Gather → Concat → Reshape` 같은 "텐서 모양 계산". 입력 shape가 static이면 전부 상수다.
- **가중치 전처리**: 상수 weight에 붙은 `Transpose`, `Reshape`, `Cast`, `DequantizeLinear`(양자화된 weight를 float로 푸는 것 — 반대로 양자화 흐름에서는 일부러 안 접기도 한다, C1).
- **상수끼리의 산술**: `Mul(0.5, 4.0)`, BN folding으로 생긴 `γ/√(σ²+ε)`.

### 3.2 손계산

입력 `x`가 `[1, 8, 4, 4]`로 고정됐다고 하자. 다음 그래프를 손으로 접어 보자.

```
 shp       = Shape(x)                 = [1, 8, 4, 4]          ← x의 값이 아니라 모양만 본다 → 상수
 n         = Gather(shp, [0])         = [1]
 new_shape = Concat(n, [-1])          = [1, -1]
 flat      = Reshape(x, new_shape)    → Reshape(x, [1, -1]) → [1, 128]   (8·4·4 = 128)
 Wt        = Transpose(W)             W: [10,128] 상수 → Wt: [128,10] 상수
 scale     = Mul(0.5, 4.0)            = 2.0
 y         = Mul(MatMul(flat, Wt), scale)

 접은 결과:  y = Mul(MatMul(Reshape(x, [1,-1]), Wt′), 2.0)     ← 노드 8개 → 3개
```

말로 하면: 입력의 **값**에 닿는 노드(Reshape, MatMul, Mul)만 남기고, 모양과 상수만 보는 노드는 결과 숫자로 바꿔 initializer에 넣는다.

### 3.3 ONNX Runtime으로 확인 (예제 5)

무엇을 확인하는 코드인지: 위 그래프를 `onnx.helper`로 직접 만들고, ORT의 `ORT_DISABLE_ALL`과 `ORT_ENABLE_BASIC`로 세션을 만들면서 `optimized_model_filepath`로 **최적화된 그래프를 파일로 저장**시킨 뒤 비교한다. (ORT 1.19는 IR version 10까지 읽으므로 `ir_version=10`을 지정한다.)

```python
import numpy as np, onnx, onnxruntime as ort
from onnx import helper as h, TensorProto as T, numpy_helper as nh
rng = np.random.default_rng(0)
W = rng.standard_normal((10, 128)).astype(np.float32)      # [out, in] 로 저장된 가중치
init = [nh.from_array(W, "W"), nh.from_array(np.array([0], np.int64), "i0"),
        nh.from_array(np.array([-1], np.int64), "m1"),
        nh.from_array(np.array(0.5, np.float32), "a"), nh.from_array(np.array(4.0, np.float32), "b")]
nodes = [
  h.make_node("Shape", ["x"], ["shp"]),                       # [1,8,4,4] — 입력이 static이면 상수
  h.make_node("Gather", ["shp", "i0"], ["n"], axis=0),        # N
  h.make_node("Concat", ["n", "m1"], ["new_shape"], axis=0),  # [N, -1]
  h.make_node("Reshape", ["x", "new_shape"], ["flat"]),       # [1,128]
  h.make_node("Transpose", ["W"], ["Wt"], perm=[1, 0]),        # 상수의 transpose
  h.make_node("MatMul", ["flat", "Wt"], ["mm"]),
  h.make_node("Mul", ["a", "b"], ["scale"]),                   # 상수 × 상수
  h.make_node("Mul", ["mm", "scale"], ["y"])]
g = h.make_graph(nodes, "cf", [h.make_tensor_value_info("x", T.FLOAT, [1, 8, 4, 4])],
                 [h.make_tensor_value_info("y", T.FLOAT, [1, 10])], init)
onnx.save(h.make_model(g, opset_imports=[h.make_opsetid("", 17)], ir_version=10), "cf.onnx")
x = rng.standard_normal((1, 8, 4, 4)).astype(np.float32)
outs = {}
for name, lvl in [("DISABLE_ALL", ort.GraphOptimizationLevel.ORT_DISABLE_ALL),
                  ("ENABLE_BASIC", ort.GraphOptimizationLevel.ORT_ENABLE_BASIC)]:
    so = ort.SessionOptions(); so.graph_optimization_level = lvl
    so.optimized_model_filepath = f"cf_{name}.onnx"
    outs[name] = ort.InferenceSession("cf.onnx", so, providers=["CPUExecutionProvider"]).run(None, {"x": x})[0]
    gg = onnx.load(f"cf_{name}.onnx").graph
    print(f"{name:<12} {[n.op_type for n in gg.node]}  inits={[t.name for t in gg.initializer]}")
print("max diff:", np.abs(outs["DISABLE_ALL"] - outs["ENABLE_BASIC"]).max())
print("numpy ref diff:", np.abs(x.reshape(1, -1) @ W.T * 2.0 - outs["ENABLE_BASIC"]).max())
```

```text
DISABLE_ALL  ['Shape', 'Gather', 'Concat', 'Reshape', 'Transpose', 'MatMul', 'Mul', 'Mul']  inits=['W', 'i0', 'm1', 'a', 'b']
ENABLE_BASIC ['Reshape', 'MatMul', 'Mul']  inits=['new_shape', 'scale', 'Wt']
max diff: 0.0
numpy ref diff: 7.6293945e-06
```

출력에서 볼 것:

- 손계산과 똑같이 8 → 3 노드. initializer도 바뀌었다: 원래 `W`는 사라지고 **미리 transpose 된 `Wt`**, 계산된 `new_shape`, `scale`이 새 상수가 됐다.
- ORT 두 레벨의 출력 차이는 0.0 — 같은 커널로 같은 계산. numpy 기준과의 7.6e-6은 128개 곱의 누적 순서 차이다(출력 크기가 수십 정도라 상대 오차 1e-7급).
- 임베디드 관점: `Transpose(W)`를 매 추론마다 돌면 1280개 float(5 KB)을 매번 복사한다. 접으면 **flash에 transpose된 채로 저장**된다. 펌웨어에서 룩업 테이블을 런타임에 만들지 않고 빌드 타임에 생성해서 `.rodata`에 넣는 것과 같다.

### 3.4 함정

- **constant folding은 모델 크기를 키울 수 있다.** 작은 상수를 `Expand`로 부풀리는 노드를 접으면 큰 상수가 저장된다. 그래서 도구마다 "결과가 너무 크면 안 접는" 정책을 두기도 한다.
- **dynamic shape이면 shape 산술이 안 접힌다.** `Shape(x)`가 런타임 값이 되기 때문이다(9절). NPU 컴파일러가 "Shape op unsupported"를 뱉는 흔한 원인.
- 양자화된 모델에서 `DequantizeLinear(W_int8)`를 접으면 weight가 float로 풀려 **4배 커진다**. QDQ 형식(C1, C2)을 NPU 컴파일러에 넘길 때는 이 패턴을 보존해야 한다.

---

## 4. Op fusion — 여러 op를 커널 하나로

### 4.1 왜 fusion이 이득인가: 중간 텐서가 메모리를 왕복한다

conv → BN → ReLU를 따로 실행하면 이렇게 된다.

```
 unfused:  Conv:  x ──read──► [MAC] ──write──► t1 (DRAM/SRAM)
           BN:    t1 ─read──► [×s + b] ─write─► t2
           ReLU:  t2 ─read──► [max(0,·)] ─write► y

 fused:    FusedConv: x ──read──► [MAC → ×s+b → max(0,·)] ──write──► y
                                   (레지스터 안에서 끝남 = epilogue)
```

BN과 ReLU는 원소당 연산이 1~2개뿐이다. 이런 op는 **계산보다 메모리 이동이 비싸다**(D3의 arithmetic intensity가 극히 낮은 memory-bound op). fusion하면 중간 텐서 t1, t2가 **아예 존재하지 않는다** — 쓰지도 읽지도 않고, 버퍼도 필요 없다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="c6m2" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="10" y="20" font-size="13">before (노드 3개, 중간 텐서 2개)</text> <rect x="10" y="40" width="60" height="36" rx="18" fill="none" stroke="#888" stroke-width="2"/><text x="40" y="63" font-size="13" text-anchor="middle">x</text> <rect x="110" y="40" width="80" height="36" rx="4" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="150" y="63" font-size="13" text-anchor="middle">Conv</text> <rect x="290" y="40" width="80" height="36" rx="4" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="330" y="63" font-size="13" text-anchor="middle">BN</text> <rect x="470" y="40" width="80" height="36" rx="4" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="510" y="63" font-size="13" text-anchor="middle">ReLU</text>
<rect x="600" y="40" width="60" height="36" rx="18" fill="none" stroke="#888" stroke-width="2"/><text x="630" y="63" font-size="13" text-anchor="middle">y</text> <line x1="70" y1="58" x2="108" y2="58" stroke="currentColor" stroke-width="1.5" marker-end="url(#c6m2)"/> <line x1="190" y1="58" x2="288" y2="58" stroke="#d0564a" stroke-width="3" marker-end="url(#c6m2)"/> <line x1="370" y1="58" x2="468" y2="58" stroke="#d0564a" stroke-width="3" marker-end="url(#c6m2)"/> <line x1="550" y1="58" x2="598" y2="58" stroke="currentColor" stroke-width="1.5" marker-end="url(#c6m2)"/> <text x="240" y="96" font-size="12" text-anchor="middle">t1 8 KB</text><text x="240" y="112" font-size="12" text-anchor="middle">write + read</text> <text x="420" y="96" font-size="12" text-anchor="middle">t2 8 KB</text><text x="420" y="112" font-size="12" text-anchor="middle">write + read</text>
<text x="10" y="150" font-size="13">after (노드 1개, 중간 텐서 0개)</text> <rect x="10" y="170" width="60" height="36" rx="18" fill="none" stroke="#888" stroke-width="2"/><text x="40" y="193" font-size="13" text-anchor="middle">x</text> <rect x="150" y="165" width="360" height="46" rx="4" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="330" y="185" font-size="13" text-anchor="middle">FusedConv: W′ = s·W, b′ = s·(b − μ) + β</text> <text x="330" y="203" font-size="12" text-anchor="middle">activation = Relu (MAC 루프 끝 epilogue)</text> <rect x="600" y="170" width="60" height="36" rx="18" fill="none" stroke="#888" stroke-width="2"/><text x="630" y="193" font-size="13" text-anchor="middle">y</text> <line x1="70" y1="188" x2="148" y2="188" stroke="currentColor" stroke-width="1.5" marker-end="url(#c6m2)"/>
<line x1="510" y1="188" x2="598" y2="188" stroke="currentColor" stroke-width="1.5" marker-end="url(#c6m2)"/> <text x="10" y="238" font-size="12">빨간 엣지 = fusion으로 사라지는 중간 텐서 (TinyNet block1, fp32, [1,8,16,16] = 8,192 B)</text>
</svg>
```

그림 2 — Conv+BN+ReLU fusion. BN은 weight/bias에 접히고(B1 7절), ReLU는 conv 출력을 저장하기 직전에 적용된다. 빨간 엣지 두 개가 통째로 사라진다.

### 4.2 손계산 — TinyNet에서 사라지는 바이트

fp32, 입력 16×16 기준.

| 블록 | 텐서 shape | 텐서 1개 | 사라지는 중간 텐서 | 사라지는 메모리 트래픽 |
|---|---|---|---|---|
| block1 | `[1,8,16,16]` | 8·16·16·4 = 8,192 B | t1, t2 = 16,384 B | (write + read) × 2 = 32,768 B |
| block2 | `[1,16,16,16]` | 16,384 B | 32,768 B | 65,536 B |
| 합계 | | | 48 KB | 96 KB |

말로 하면: 이 작은 모델에서도 한 번 추론할 때 96 KB의 쓸데없는 메모리 트래픽과, 동시에 살아 있을 수 있는 중간 버퍼 수십 KB가 사라진다. int8이면 1/4이지만 비율은 같다. MCU에서 SRAM이 256 KB라면 이건 큰 숫자다. 게다가 NPU에서는 중간 텐서가 on-chip SRAM에 안 들어가면 DRAM으로 spill 되는데, D7에서 봤듯 DRAM 접근 에너지는 MAC보다 수백 배 비싸다.

### 4.3 fusion 패턴 목록

| 패턴 | 결과 | 조건 | 이득 |
|---|---|---|---|
| Conv + BN | Conv (W′, b′) | BN이 eval 모드(상수 통계), Conv의 W와 b가 상수 | 노드 1개 + 중간 텐서 1개 제거, 연산 0 추가 (B1) |
| Conv + ReLU / ReLU6 / Clip | Conv(activation=…) | activation이 원소별 | epilogue에서 처리, 중간 텐서 제거 |
| MatMul + Add | Gemm | Add의 다른 입력이 bias 모양의 상수 | 커널 1개, bias를 누산기 초기값으로 |
| Gemm + ReLU | FusedGemm / FC(activation) | 위와 같음 | 위와 같음 |
| Add + ReLU (residual) | Add(activation) | | skip connection 뒤에서 흔함 |
| Mul + Add (scale·shift) | 앞 Conv에 fold 또는 FMA | 상수 | |
| 여러 op → GELU, LayerNorm, Attention | 하나의 큰 op | 패턴이 정확히 일치 | 8절의 반대 방향 (그 op를 HW가 네이티브로 지원할 때) |

TFLite 포맷은 이게 스키마에 박혀 있다: `Conv2DOptions`, `FullyConnectedOptions`, `AddOptions` 등에 `fused_activation_function` 필드(NONE, RELU, RELU6 …)가 있어서, 변환기가 뒤따르는 활성화를 이 필드로 흡수한다.

### 4.4 C로 보면 — fusion은 루프 합치기다 (예제 6, C)

무엇을 확인하는 코드인지: 3×3 conv(8→16채널, 32×32) + BN(scale/shift) + ReLU를 (A) 레이어별 루프 3개와 중간 버퍼 2개로, (B) 한 루프 + epilogue로 구현해 결과가 같고, 중간 텐서 트래픽이 얼마나 사라지는지 센다.

```c
#include <stdio.h>
#include <string.h>
#include <math.h>
#define CI 8
#define CO 16
#define H 32
#define W 32
static float x[CI][H + 2][W + 2], w[CO][CI][3][3], b[CO], sc[CO], sh[CO];
static float t1[CO][H][W], t2[CO][H][W], yu[CO][H][W], yf[CO][H][W];
static long traffic;                                  /* 중간 텐서가 메모리를 오간 바이트 */

static float conv_at(int o, int i, int j) {
    float acc = b[o];
    for (int c = 0; c < CI; c++)
        for (int ki = 0; ki < 3; ki++)
            for (int kj = 0; kj < 3; kj++) acc += w[o][c][ki][kj] * x[c][i + ki][j + kj];
    return acc;
}
static void unfused(void) {                           /* conv → BN → ReLU, 레이어마다 루프 */
    for (int o = 0; o < CO; o++) for (int i = 0; i < H; i++) for (int j = 0; j < W; j++)
        t1[o][i][j] = conv_at(o, i, j);
    for (int o = 0; o < CO; o++) for (int i = 0; i < H; i++) for (int j = 0; j < W; j++)
        t2[o][i][j] = t1[o][i][j] * sc[o] + sh[o];
    for (int o = 0; o < CO; o++) for (int i = 0; i < H; i++) for (int j = 0; j < W; j++)
        yu[o][i][j] = fmaxf(t2[o][i][j], 0.0f);
    traffic += 2L * sizeof t1 + 2L * sizeof t2;        /* t1, t2 각각 write 1번 + read 1번 */
}
static void fused(void) {                             /* BN은 w, b에 접었다고 치고(B1), ReLU는 epilogue */
    for (int o = 0; o < CO; o++) for (int i = 0; i < H; i++) for (int j = 0; j < W; j++)
        yf[o][i][j] = fmaxf(conv_at(o, i, j) * sc[o] + sh[o], 0.0f);
}
int main(void) {
    unsigned s = 1;
    float *p = &x[0][0][0];
    for (size_t k = 0; k < sizeof x / sizeof(float); k++) { s = s * 1103515245u + 12345u; p[k] = (float)(s >> 16 & 1023) / 512.0f - 1.0f; }
    p = &w[0][0][0][0];
    for (size_t k = 0; k < sizeof w / sizeof(float); k++) { s = s * 1103515245u + 12345u; p[k] = (float)(s >> 16 & 1023) / 2048.0f - 0.25f; }
    for (int o = 0; o < CO; o++) { b[o] = 0.01f * o; sc[o] = 0.5f + 0.05f * o; sh[o] = -0.1f; }
    unfused(); fused();
    float md = 0; for (int o = 0; o < CO; o++) for (int i = 0; i < H; i++) for (int j = 0; j < W; j++)
        md = fmaxf(md, fabsf(yu[o][i][j] - yf[o][i][j]));
    printf("output tensor = %zu B, max|unfused - fused| = %g\n", sizeof yu, md);
    printf("unfused: intermediate traffic = %ld B (t1, t2 = %zu B each)\n", traffic, sizeof t1);
    printf("fused  : intermediate traffic = 0 B, extra buffers = 0 B\n");
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 fuse.c -o fuse -lm && ./fuse
```

```text
output tensor = 65536 B, max|unfused - fused| = 0
unfused: intermediate traffic = 262144 B (t1, t2 = 65536 B each)
fused  : intermediate traffic = 0 B, extra buffers = 0 B
```

출력에서 볼 것: 결과는 비트 단위로 같고(`0`), unfused는 출력 텐서(64 KB)의 **4배(256 KB)**를 중간 텐서로 쓰고 읽는다. 게다가 `t1`, `t2` 버퍼 128 KB가 SRAM에 있어야 한다. 이 예제에서 BN을 `sc`, `sh`로 epilogue에 남겨 둔 건 설명을 위해서다. 실제로는 B1처럼 오프라인에서 `w`, `b`에 접어 곱셈 하나도 없앤다. CMSIS-NN의 int8 conv 커널도 같은 구조다: 누산 → requantize(C1) → clamp(activation min/max)를 한 루프 안에서 끝낸다.

---

## 5. Dead code · Identity 제거, 그리고 MatMul+Add → Gemm

### 5.1 export 잔재들

export 된 그래프에는 추론에 필요 없는 것이 섞여 있다.

- `Identity`: 이름만 바꾸는 노드. 모듈 경계, 출력 이름 맞추기에서 생긴다.
- `Dropout`: 추론에서는 항등(A5 9.2절에서 `torch.export` 그래프에 `aten.dropout`이 남은 것을 봤다).
- **dead branch**: 디버그용 출력, 쓰이지 않는 head. 그래프 출력에 도달하지 않는 노드.

펌웨어의 `-ffunction-sections -Wl,--gc-sections`와 같은 일이다: 출력(= 링커의 entry point)에서 거꾸로 도달 가능한 것만 남긴다.

### 5.2 ORT가 무엇을 하고 무엇을 안 하나 (예제 7)

무엇을 확인하는 코드인지: 2층 MLP(`MatMul → Add → Identity → Relu → Dropout → MatMul → Add`)에 출력과 연결되지 않은 디버그 가지(`Sigmoid → ReduceMax`)를 붙여 ORT 레벨별로 그래프를 본다.

```python
import numpy as np, onnx, onnxruntime as ort
from onnx import helper as h, TensorProto as T, numpy_helper as nh
rng = np.random.default_rng(0)
W1, b1 = rng.standard_normal((64, 32)).astype(np.float32), rng.standard_normal(32).astype(np.float32)
W2, b2 = rng.standard_normal((32, 10)).astype(np.float32), rng.standard_normal(10).astype(np.float32)
init = [nh.from_array(a, n) for a, n in [(W1, "W1"), (b1, "b1"), (W2, "W2"), (b2, "b2")]]
nodes = [h.make_node("MatMul", ["x", "W1"], ["m1"]), h.make_node("Add", ["m1", "b1"], ["a1"]),
         h.make_node("Identity", ["a1"], ["id1"]),                 # export 잔재
         h.make_node("Relu", ["id1"], ["r1"]),
         h.make_node("Dropout", ["r1"], ["d1"]),                   # 추론에선 항등
         h.make_node("MatMul", ["d1", "W2"], ["m2"]), h.make_node("Add", ["m2", "b2"], ["y"]),
         h.make_node("Sigmoid", ["x"], ["dbg"]),                   # 디버그용으로 달아 둔 가지
         h.make_node("ReduceMax", ["dbg"], ["dbg_max"])]           # 출력에 연결 안 됨 = dead
g = h.make_graph(nodes, "mlp", [h.make_tensor_value_info("x", T.FLOAT, [1, 64])],
                 [h.make_tensor_value_info("y", T.FLOAT, [1, 10])], init)
onnx.save(h.make_model(g, opset_imports=[h.make_opsetid("", 17)], ir_version=10), "mlp.onnx")
x = rng.standard_normal((1, 64)).astype(np.float32)
ref = np.maximum(x @ W1 + b1, 0) @ W2 + b2
for lvl in ["DISABLE_ALL", "ENABLE_BASIC", "ENABLE_EXTENDED"]:
    so = ort.SessionOptions(); so.log_severity_level = 3
    so.graph_optimization_level = getattr(ort.GraphOptimizationLevel, "ORT_" + lvl)
    so.optimized_model_filepath = f"mlp_{lvl}.onnx"
    y = ort.InferenceSession("mlp.onnx", so, providers=["CPUExecutionProvider"]).run(None, {"x": x})[0]
    ops = [n.op_type for n in onnx.load(so.optimized_model_filepath).graph.node]
    print(f"{lvl:<15} {ops}  max|diff|={np.abs(y - ref).max():.1e}")
```

```text
DISABLE_ALL     ['MatMul', 'Add', 'Identity', 'Relu', 'Dropout', 'MatMul', 'Add', 'Sigmoid', 'ReduceMax']  max|diff|=0.0e+00
ENABLE_BASIC    ['Gemm', 'Relu', 'Gemm', 'Sigmoid', 'ReduceMax']  max|diff|=0.0e+00
ENABLE_EXTENDED ['FusedGemm', 'Gemm', 'Sigmoid', 'ReduceMax']  max|diff|=0.0e+00
```

출력에서 볼 것:

- BASIC: `Identity`, `Dropout`이 사라지고 **`MatMul + Add` → `Gemm`** 으로 합쳐졌다(`Gemm`은 `α·A·B + β·C`를 한 번에 하는 op — BLAS의 GEMM 그대로).
- EXTENDED: 첫 `Gemm + Relu`가 ORT 전용 contrib op `FusedGemm`(도메인 `com.microsoft`)으로 합쳐졌다 — activation fusion.
- **그런데 `Sigmoid → ReduceMax` dead branch는 모든 레벨에서 살아남았다.** 이 버전 ORT(1.19.2)의 그래프 최적화는 출력에 안 닿는 노드를 지우지 않았다(실측). 도구가 "당연히 해 주겠지" 하는 가정은 확인해야 한다 — 벤더 컴파일러에 넘기기 전에 직접 정리하는 이유다.

### 5.3 DCE를 직접 짜 보기 (예제 8)

무엇을 확인하는 코드인지: 출력에서 거꾸로 도달 가능한 노드만 남기는 DCE와, Identity/Dropout을 "입력 이름으로 재연결"하는 제거 패스를 30줄로 구현하고, onnxscript optimizer 결과와 비교한다.

```python
import onnx, onnxscript.optimizer as oso
m = onnx.load("mlp.onnx")
def dce(model):
    g = model.graph
    live = {o.name for o in g.output}                     # 출력에서 거꾸로 도달 가능한 텐서
    keep = []
    for n in reversed(g.node):                            # 노드는 위상 정렬돼 있다
        if any(o in live for o in n.output):
            keep.append(n); live.update(i for i in n.input if i)
    del g.node[:]; g.node.extend(reversed(keep))
    return model
def drop_identity(model):
    g = model.graph; alias = {}
    keep = []
    for n in g.node:
        n.input[:] = [alias.get(i, i) for i in n.input]
        if n.op_type in ("Identity", "Dropout") and n.output[0] not in {o.name for o in g.output}:
            alias[n.output[0]] = n.input[0]               # 출력 이름을 입력으로 재연결
        else:
            keep.append(n)
    del g.node[:]; g.node.extend(keep)
    return model
mine = drop_identity(dce(onnx.load("mlp.onnx")))
print("mine      :", [n.op_type for n in mine.graph.node])
print("onnxscript:", [n.op_type for n in oso.optimize(onnx.load("mlp.onnx")).graph.node])
onnx.checker.check_model(mine); onnx.save(mine, "mlp_clean.onnx")
import numpy as np, onnxruntime as ort
x = np.random.default_rng(3).standard_normal((1, 64)).astype(np.float32)
run = lambda f: ort.InferenceSession(f, providers=["CPUExecutionProvider"]).run(None, {"x": x})[0]
print("max|orig - clean| =", np.abs(run("mlp.onnx") - run("mlp_clean.onnx")).max())
```

```text
mine      : ['MatMul', 'Add', 'Relu', 'MatMul', 'Add']
onnxscript: ['MatMul', 'Add', 'Relu', 'MatMul', 'Add']
max|orig - clean| = 0.0
```

출력에서 볼 것: 직접 짠 패스와 onnxscript optimizer가 같은 5노드 그래프를 만든다. 알고리즘은 컴파일러 교과서의 mark-and-sweep 그대로다: "출력 → 입력 방향으로 live 집합을 넓히며 표시, 표시 안 된 노드 삭제". `Dropout`을 지울 수 있는 건 **추론 그래프라서**다 — 학습 그래프에서 지우면 안 된다. 이런 "그래프 수술" 코드를 작게라도 짤 줄 알면, 벤더 도구가 특정 패턴을 못 먹을 때 전처리 스크립트로 우회할 수 있다(8절).

---

## 6. ONNX Runtime 최적화 레벨 — 실제 export 그래프에 걸어 보기

### 6.1 레벨 정리

ORT는 세션을 만들 때 그래프를 최적화한다. `SessionOptions.graph_optimization_level`로 레벨을 고르고, `optimized_model_filepath`를 주면 결과 그래프를 파일로 저장한다(오프라인 최적화: 한 번 최적화해 두고 기기에서는 그 파일을 로드).

| 레벨 (Python enum) | 하는 일 (대표) | 결과 그래프 |
|---|---|---|
| `ORT_DISABLE_ALL` (0) | 아무것도 안 함 (Constant 노드 → initializer 변환 정도) | 원래 ONNX op |
| `ORT_ENABLE_BASIC` (1) | constant folding, 중복 노드 제거(Identity/Dropout 등), 의미 보존 fusion (Conv+BN, MatMul+Add → Gemm, 연속 transpose 정리 등) | **표준 ONNX op만** — 다른 런타임으로 가져가도 된다 |
| `ORT_ENABLE_EXTENDED` (2) | ORT 전용 contrib op로 fusion (FusedConv, FusedGemm, GELU/LayerNorm/Attention fusion 등) | `com.microsoft` op 포함 — ORT 전용 |
| `ORT_ENABLE_ALL` (99) | 위 + layout 최적화 (예: x86 CPU의 NCHWc 블록 레이아웃) | 하드웨어 종속 — 같은 환경에서만 쓰라고 경고 |

(레벨별 정확한 패스 목록은 ORT 버전마다 달라진다. 확신할 수 있는 건 "결과 파일을 열어 보는 것"뿐이다.)

### 6.2 dirty export vs clean export (예제 9)

무엇을 확인하는 코드인지: 2절의 TinyNet을 (a) `bias=False`(conv에 bias 없음 → exporter가 0 bias를 Expand로 만듦) (b) `bias=True`로 각각 **exporter 최적화 없이** export하고, ORT 4개 레벨을 걸어 노드 수·종류·PyTorch와의 차이를 본다.

```python
import numpy as np, onnx, onnxruntime as ort, collections, torch
from tinynet import make
L = ort.GraphOptimizationLevel
x = np.random.default_rng(1).standard_normal((1, 1, 16, 16)).astype(np.float32)
for bias in (False, True):
    m = make(bias); f = f"tiny_bias{int(bias)}.onnx"
    torch.onnx.export(m, (torch.from_numpy(x),), f, input_names=["x"], output_names=["y"],
                      dynamo=True, external_data=False, optimize=False, verbose=False)
    ref = m(torch.from_numpy(x)).detach().numpy()
    for lvl in ["DISABLE_ALL", "ENABLE_BASIC", "ENABLE_EXTENDED", "ENABLE_ALL"]:
        so = ort.SessionOptions(); so.log_severity_level = 3
        so.graph_optimization_level = getattr(L, "ORT_" + lvl)
        so.optimized_model_filepath = f"opt_{bias}_{lvl}.onnx"
        y = ort.InferenceSession(f, so, providers=["CPUExecutionProvider"]).run(None, {"x": x})[0]
        g = onnx.load(so.optimized_model_filepath).graph
        ops = collections.Counter(n.op_type for n in g.node)
        print(f"bias={bias!s:<5} {lvl:<15} nodes={len(g.node):>2} diff={np.abs(y-ref).max():.0e} {dict(ops)}")
for n in onnx.load("opt_True_ENABLE_ALL.onnx").graph.node:
    if n.op_type == "FusedConv":
        a = {x.name: onnx.helper.get_attribute_value(x) for x in n.attribute}
        print("FusedConv:", n.domain, list(n.input), "activation =", a["activation"])
```

```text
bias=False DISABLE_ALL     nodes=17 diff=6e-08 {'Reshape': 1, 'Cast': 2, 'Shape': 2, 'Expand': 4, 'Conv': 2, 'BatchNormalization': 2, 'Relu': 2, 'ReduceMean': 1, 'Gemm': 1}
bias=False ENABLE_BASIC    nodes=12 diff=6e-08 {'Cast': 2, 'Expand': 2, 'Conv': 2, 'BatchNormalization': 2, 'Relu': 2, 'ReduceMean': 1, 'Gemm': 1}
bias=False ENABLE_EXTENDED nodes=12 diff=6e-08 {'Cast': 2, 'Expand': 2, 'Conv': 2, 'BatchNormalization': 2, 'Relu': 2, 'ReduceMean': 1, 'Gemm': 1}
bias=False ENABLE_ALL      nodes=12 diff=6e-08 {'Cast': 2, 'Expand': 2, 'Conv': 2, 'BatchNormalization': 2, 'Relu': 2, 'ReduceMean': 1, 'Gemm': 1}
bias=True  DISABLE_ALL     nodes= 9 diff=3e-08 {'Conv': 2, 'BatchNormalization': 2, 'Relu': 2, 'Reshape': 1, 'ReduceMean': 1, 'Gemm': 1}
bias=True  ENABLE_BASIC    nodes= 6 diff=3e-08 {'Conv': 2, 'Relu': 2, 'ReduceMean': 1, 'Gemm': 1}
bias=True  ENABLE_EXTENDED nodes= 4 diff=3e-08 {'FusedConv': 2, 'ReduceMean': 1, 'Gemm': 1}
bias=True  ENABLE_ALL      nodes= 4 diff=3e-08 {'FusedConv': 2, 'ReduceMean': 1, 'Gemm': 1}
FusedConv: com.microsoft ['x', 'ConvBnFusion_W_conv1.weight', 'ConvBnFusion_BN_B_bn1.bias'] activation = b'Relu'
FusedConv: com.microsoft ['relu', 'ConvBnFusion_W_conv2.weight', 'ConvBnFusion_BN_B_bn2.bias'] activation = b'Relu'
```

출력에서 볼 것 — 이 노트에서 가장 중요한 실측 중 하나다.

- **bias=True (clean)**: BASIC에서 BN이 Conv에 접히고(`ConvBnFusion_W_...`라는 새 initializer 이름이 증거), EXTENDED에서 Conv+Relu가 `FusedConv(activation=Relu)`가 됐다. 9 → 6 → 4 노드. 그림 2가 실제로 일어났다.
- **bias=False (dirty)**: 어떤 레벨에서도 **BN이 접히지 않았고 FusedConv도 안 생겼다.** 원인: Conv의 세 번째 입력(bias)이 initializer가 아니라 `Cast → Expand`의 출력이다. Conv+BN fusion 패턴은 "Conv의 W와 B가 상수"를 요구하는데 그 조건이 깨졌다. 그리고 BN이 Conv와 Relu 사이에 남아 있으니 Conv+Relu 패턴도 성립하지 않는다 — **fusion 하나가 막히면 연쇄적으로 다음 fusion도 막힌다.**
- 왜 ORT의 constant folding이 이 `Cast → Expand`를 접지 않았는지는 추적하지 않았다(같은 모양의 작은 그래프를 따로 만들면 ORT가 잘 접는다 — 실측. 이 export 그래프에서만 안 접혔다). 결론만 기억하자: **어떤 도구가 어떤 패턴을 접는지는 버전·그래프 모양에 따라 다르니, 최적화된 파일을 열어 노드를 세어 보는 게 유일하게 확실한 방법**이다.
- 해결책은 간단하다: exporter 최적화(`optimize=True`, 2.2절)나 onnxscript/onnx-simplifier로 **먼저 정리(cleanup)한 다음** 벤더 도구에 넘긴다. 패스 순서가 결과를 바꾼다 — C 컴파일러에서 inlining을 먼저 해야 constant propagation이 먹히는 것과 같다.
- 모든 레벨에서 PyTorch와의 차이는 1e-7 이하. 그래프 최적화는 **의미를 바꾸지 않아야 한다**(float 반올림 순서 차이는 예외). 차이가 크게 나면 그 패스는 버그다(C8).
- macOS arm64에서는 ENABLE_ALL이 EXTENDED와 같았다. NCHWc 레이아웃 변환은 x86 CPU용이라 여기선 적용되지 않은 것으로 보인다(x86에서 돌리면 `ReorderInput`/`ReorderOutput` 같은 노드가 보일 수 있다 — 확인은 안 했다).

---

## 7. Layout 변환 — NCHW ↔ NHWC와 transpose 제거

### 7.1 왜 layout이 문제인가 (A1 7절 복습)

같은 4차원 텐서를 메모리에 어떤 순서로 까느냐다.

```
 NCHW  (PyTorch 기본):  주소 = ((n·C + c)·H + h)·W + w     → 한 채널의 평면이 연속
 NHWC  (TFLite, 대부분 NPU/DSP):  주소 = ((n·H + h)·W + w)·C + c  → 한 픽셀의 모든 채널이 연속
```

NHWC가 edge 하드웨어에서 선호되는 이유: conv의 내적은 **채널 축**으로 누산한다(`Σ_c w[o][c]·x[c]`). NHWC면 한 픽셀의 채널 값들이 연속이라 SIMD 로드 한 번(예: int8 16개 = 128-bit)으로 가져올 수 있다. 1×1 conv(pointwise)는 NHWC에서 그냥 `[H·W, C_in] × [C_in, C_out]` GEMM이 된다.

PyTorch 모델(NCHW)을 NHWC 런타임으로 옮기면 변환기는 경계마다 `Transpose`를 끼워 넣는다. 순진하게 하면 op마다 앞뒤로 transpose가 붙는다. transpose는 계산이 0이고 **메모리 복사만** 한다 — 순수한 낭비다. 게다가 많은 NPU는 Transpose 자체를 지원하지 않거나 느리게 해서 fallback을 부른다.

### 7.2 transpose 제거의 두 규칙

```
 규칙 1 (상쇄):   Transpose(perm=[0,2,3,1]) ∘ Transpose(perm=[0,3,1,2]) = Identity
                 → 연달아 붙은 역변환 쌍은 둘 다 지운다

 규칙 2 (밀어내기): Relu, Add(같은 layout끼리), Mul(스칼라), Clip, Sigmoid …
                 원소별 op는 layout과 무관하다:  Relu(Transpose(x)) = Transpose(Relu(x))
                 → transpose를 원소별 op 너머로 밀어서 다른 transpose와 만나게 한 뒤 규칙 1
```

말로 하면: transpose를 그래프 위아래로 밀어서 짝을 만나게 하고, 짝끼리 소멸시킨다. 남는 건 **입력 쪽 하나, 출력 쪽 하나**뿐이다(그마저도 입력을 처음부터 NHWC로 받으면 사라진다).

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="c6m3" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="10" y="20" font-size="13">before: op마다 NHWC ↔ NCHW 변환 (Transpose 4개)</text> <rect x="6" y="40" width="54" height="32" rx="16" fill="none" stroke="#888" stroke-width="2"/><text x="33" y="61" font-size="12" text-anchor="middle">NHWC</text> <rect x="80" y="40" width="54" height="32" rx="4" fill="none" stroke="#d0564a" stroke-width="2"/><text x="107" y="61" font-size="12" text-anchor="middle">T</text> <rect x="154" y="40" width="54" height="32" rx="4" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="181" y="61" font-size="12" text-anchor="middle">Conv</text>
<rect x="228" y="40" width="54" height="32" rx="4" fill="none" stroke="#d0564a" stroke-width="2"/><text x="255" y="61" font-size="12" text-anchor="middle">T</text> <rect x="302" y="40" width="54" height="32" rx="4" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="329" y="61" font-size="12" text-anchor="middle">Relu</text> <rect x="376" y="40" width="54" height="32" rx="4" fill="none" stroke="#d0564a" stroke-width="2"/><text x="403" y="61" font-size="12" text-anchor="middle">T</text> <rect x="450" y="40" width="54" height="32" rx="4" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="477" y="61" font-size="12" text-anchor="middle">Conv</text> <rect x="524" y="40" width="54" height="32" rx="4" fill="none" stroke="#d0564a" stroke-width="2"/><text x="551" y="61" font-size="12" text-anchor="middle">T</text>
<rect x="598" y="40" width="70" height="32" rx="16" fill="none" stroke="#888" stroke-width="2"/><text x="633" y="61" font-size="12" text-anchor="middle">NHWC</text> <line x1="60" y1="56" x2="78" y2="56" stroke="currentColor" marker-end="url(#c6m3)"/> <line x1="134" y1="56" x2="152" y2="56" stroke="currentColor" marker-end="url(#c6m3)"/> <line x1="208" y1="56" x2="226" y2="56" stroke="currentColor" marker-end="url(#c6m3)"/> <line x1="282" y1="56" x2="300" y2="56" stroke="currentColor" marker-end="url(#c6m3)"/> <line x1="356" y1="56" x2="374" y2="56" stroke="currentColor" marker-end="url(#c6m3)"/> <line x1="430" y1="56" x2="448" y2="56" stroke="currentColor" marker-end="url(#c6m3)"/> <line x1="504" y1="56" x2="522" y2="56" stroke="currentColor" marker-end="url(#c6m3)"/> <line x1="578" y1="56" x2="596" y2="56" stroke="currentColor" marker-end="url(#c6m3)"/>
<path d="M255,76 Q329,112 403,76" fill="none" stroke="#d0564a" stroke-dasharray="4 3"/> <text x="329" y="112" font-size="12" text-anchor="middle">Relu 너머로 밀면 [0,2,3,1]∘[0,3,1,2] = 항등 → 둘 다 삭제</text> <text x="10" y="150" font-size="13">after: 경계에만 Transpose 2개 (입력을 NHWC로 계속 쓰는 런타임이면 0개)</text> <rect x="80" y="170" width="54" height="32" rx="16" fill="none" stroke="#888" stroke-width="2"/><text x="107" y="191" font-size="12" text-anchor="middle">NHWC</text> <rect x="154" y="170" width="54" height="32" rx="4" fill="none" stroke="#d0564a" stroke-width="2"/><text x="181" y="191" font-size="12" text-anchor="middle">T</text> <rect x="228" y="170" width="54" height="32" rx="4" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="255" y="191" font-size="12" text-anchor="middle">Conv</text>
<rect x="302" y="170" width="54" height="32" rx="4" fill="none" stroke="#3f9a6b" stroke-width="2"/><text x="329" y="191" font-size="12" text-anchor="middle">Relu</text> <rect x="376" y="170" width="54" height="32" rx="4" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="403" y="191" font-size="12" text-anchor="middle">Conv</text> <rect x="450" y="170" width="54" height="32" rx="4" fill="none" stroke="#d0564a" stroke-width="2"/><text x="477" y="191" font-size="12" text-anchor="middle">T</text> <rect x="524" y="170" width="70" height="32" rx="16" fill="none" stroke="#888" stroke-width="2"/><text x="559" y="191" font-size="12" text-anchor="middle">NHWC</text> <line x1="134" y1="186" x2="152" y2="186" stroke="currentColor" marker-end="url(#c6m3)"/> <line x1="208" y1="186" x2="226" y2="186" stroke="currentColor" marker-end="url(#c6m3)"/>
<line x1="282" y1="186" x2="300" y2="186" stroke="currentColor" marker-end="url(#c6m3)"/> <line x1="356" y1="186" x2="374" y2="186" stroke="currentColor" marker-end="url(#c6m3)"/> <line x1="430" y1="186" x2="448" y2="186" stroke="currentColor" marker-end="url(#c6m3)"/> <line x1="504" y1="186" x2="522" y2="186" stroke="currentColor" marker-end="url(#c6m3)"/>
</svg>
```

그림 3 — transpose 밀어내기와 상쇄. 빨간 T = Transpose(메모리 복사만 하는 노드). 예제 10의 실제 결과와 같은 모양이다.

### 7.3 ORT로 확인 (예제 10)

무엇을 확인하는 코드인지: "NHWC 입력 → NCHW conv"를 순진하게 이은 그래프(Transpose 4개)를 만들고, ORT BASIC이 transpose를 얼마나 지우는지 본다.

```python
import numpy as np, onnx, onnxruntime as ort
from onnx import helper as h, TensorProto as T, numpy_helper as nh
rng = np.random.default_rng(0)
Wa = rng.standard_normal((8, 8, 3, 3)).astype(np.float32) * 0.2
Wb = rng.standard_normal((8, 8, 1, 1)).astype(np.float32) * 0.2
TO_NCHW, TO_NHWC = [0, 3, 1, 2], [0, 2, 3, 1]
nodes = [h.make_node("Transpose", ["x"], ["t0"], perm=TO_NCHW),     # NHWC 입력 → NCHW conv
         h.make_node("Conv", ["t0", "Wa"], ["c0"], pads=[1, 1, 1, 1]),
         h.make_node("Transpose", ["c0"], ["t1"], perm=TO_NHWC),      # NHWC로 되돌림
         h.make_node("Relu", ["t1"], ["r"]),                          # 레이아웃 무관 op
         h.make_node("Transpose", ["r"], ["t2"], perm=TO_NCHW),       # 다시 NCHW
         h.make_node("Conv", ["t2", "Wb"], ["c1"]),
         h.make_node("Transpose", ["c1"], ["y"], perm=TO_NHWC)]
g = h.make_graph(nodes, "layout", [h.make_tensor_value_info("x", T.FLOAT, [1, 16, 16, 8])],
                 [h.make_tensor_value_info("y", T.FLOAT, [1, 16, 16, 8])],
                 [nh.from_array(Wa, "Wa"), nh.from_array(Wb, "Wb")])
onnx.save(h.make_model(g, opset_imports=[h.make_opsetid("", 17)], ir_version=10), "layout.onnx")
x = rng.standard_normal((1, 16, 16, 8)).astype(np.float32)
res = {}
for lvl in ["DISABLE_ALL", "ENABLE_BASIC"]:
    so = ort.SessionOptions(); so.log_severity_level = 3
    so.graph_optimization_level = getattr(ort.GraphOptimizationLevel, "ORT_" + lvl)
    so.optimized_model_filepath = f"layout_{lvl}.onnx"
    res[lvl] = ort.InferenceSession("layout.onnx", so, providers=["CPUExecutionProvider"]).run(None, {"x": x})[0]
    print(f"{lvl:<13}", [n.op_type for n in onnx.load(so.optimized_model_filepath).graph.node])
print("max diff:", np.abs(res["DISABLE_ALL"] - res["ENABLE_BASIC"]).max())
print("bytes moved by one Transpose [1,16,16,8] fp32:", 2 * 16 * 16 * 8 * 4, "B (read+write)")
```

```text
DISABLE_ALL   ['Transpose', 'Conv', 'Transpose', 'Relu', 'Transpose', 'Conv', 'Transpose']
ENABLE_BASIC  ['Transpose', 'Conv', 'Relu', 'Conv', 'Transpose']
max diff: 0.0
bytes moved by one Transpose [1,16,16,8] fp32: 16384 B (read+write)
```

출력에서 볼 것: 가운데 Transpose 두 개가 Relu 너머로 밀려 상쇄됐다(그림 3과 같은 결과). 출력은 비트 단위로 같다. 이 작은 텐서에서도 transpose 하나가 16 KB를 옮긴다. 실제 변환 모델에서 transpose가 수십 개 남아 있으면 NPU 시간의 상당 부분이 "데이터 재배치"에 쓰인다. **컴파일된 모델에 Transpose가 몇 개 남았는지 세는 것**은 bring-up 체크리스트의 한 줄이다.

### 7.4 transpose가 안 지워지는 경우

- layout에 **의존하는** op 사이에 끼면 못 민다: `Reshape`/`Flatten`(NCHW로 flatten한 순서와 NHWC flatten 순서가 다르다 — A1의 stride 이야기), `Concat`/`Split`/`Reduce`(축 번호를 바꿔 줘야 함 — 도구가 해 주기도, 못 하기도 한다), `Softmax(axis=1)`.
- 가장 흔한 사고: **Flatten → FC**. PyTorch에서 NCHW로 flatten해서 학습한 FC weight를, NHWC 런타임이 NHWC 순서로 flatten한 벡터에 곱하면 숫자는 나오는데 **틀린 값**이다. 변환기는 이걸 알고 FC 앞에 transpose를 남기거나 FC weight의 행 순서를 재배열(permute)한다. 수동 변환 코드를 짤 때 잘 틀리는 곳이다(F8 "정확도 불일치: layout").

### 7.5 채널 정렬 — 8/16/32의 배수로 패딩

NPU MAC array와 SIMD 레지스터는 "채널 몇 개를 한 번에" 처리한다. 예: 128-bit SIMD는 int8 16개, 어떤 NPU는 채널을 16이나 32 단위 블록(brick)으로 묶어 저장한다(예: Arm Ethos-U의 Vela는 내부적으로 16채널 단위 brick 형식 NHCWB16을 쓴다고 문서화돼 있다). 채널 수가 그 배수가 아니면 **0으로 패딩**해서 맞춘다. 패딩된 칸도 메모리를 먹고, MAC 유닛도 0을 곱하느라 시간을 쓴다.

```
 낭비율 = (padded − C) / padded,    padded = ⌈C / A⌉ · A

 예: C = 6 (IMU 6축 입력), A = 16  →  padded = 16,  낭비 = 10/16 = 62.5 %
     1×1 conv 6→16, 입력·출력 채널 둘 다 16 배수로 패딩 → 유용한 MAC = 6·16 = 96,
     실제 MAC = 16·16 = 256 → 낭비 1 − 96/256 = 62.5 %
```

무엇을 확인하는 코드인지 (예제 11): 흔한 채널 수에서 8/16/32 정렬 시 낭비율과, 1×1 conv에서의 MAC 낭비(입력·출력 채널이 둘 다 패딩된다는 단순 모델).

```python
import math
def pad(c, a): return math.ceil(c / a) * a
chans = [3, 6, 10, 24, 40, 96, 112, 144, 320]
print("  C  " + "".join(f"| pad{a:<2} waste " for a in (8, 16, 32)))
for c in chans:
    row = "".join(f"| {pad(c,a):>4} {100*(pad(c,a)-c)/pad(c,a):5.1f}% " for a in (8, 16, 32))
    print(f"{c:>4} {row}")
# 1x1 conv MAC 낭비: 입력·출력 채널이 둘 다 패딩된다
for ci, co in [(6, 16), (24, 40), (40, 240), (3, 32)]:
    for a in (16, 32):
        useful, total = ci * co, pad(ci, a) * pad(co, a)
        print(f"1x1 conv {ci:>3}->{co:<3} align {a}: MAC 낭비 {100*(1-useful/total):5.1f}%")
```

```text
  C  | pad8  waste | pad16 waste | pad32 waste 
   3 |    8  62.5% |   16  81.2% |   32  90.6% 
   6 |    8  25.0% |   16  62.5% |   32  81.2% 
  10 |   16  37.5% |   16  37.5% |   32  68.8% 
  24 |   24   0.0% |   32  25.0% |   32  25.0% 
  40 |   40   0.0% |   48  16.7% |   64  37.5% 
  96 |   96   0.0% |   96   0.0% |   96   0.0% 
 112 |  112   0.0% |  112   0.0% |  128  12.5% 
 144 |  144   0.0% |  144   0.0% |  160  10.0% 
 320 |  320   0.0% |  320   0.0% |  320   0.0% 
1x1 conv   6->16  align 16: MAC 낭비  62.5%
1x1 conv   6->16  align 32: MAC 낭비  90.6%
1x1 conv  24->40  align 16: MAC 낭비  37.5%
1x1 conv  24->40  align 32: MAC 낭비  53.1%
1x1 conv  40->240 align 16: MAC 낭비  16.7%
1x1 conv  40->240 align 32: MAC 낭비  41.4%
1x1 conv   3->32  align 16: MAC 낭비  81.2%
1x1 conv   3->32  align 32: MAC 낭비  90.6%
```

```svg
<svg viewBox="0 0 640 270" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="220" x2="620" y2="220" stroke="currentColor"/> <line x1="60" y1="220" x2="60" y2="40" stroke="currentColor"/> <text x="54" y="224" font-size="12" text-anchor="end">0%</text> <text x="54" y="179" font-size="12" text-anchor="end">25%</text> <line x1="60" y1="175" x2="620" y2="175" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/> <text x="54" y="134" font-size="12" text-anchor="end">50%</text> <line x1="60" y1="130" x2="620" y2="130" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/> <text x="54" y="89" font-size="12" text-anchor="end">75%</text> <line x1="60" y1="85" x2="620" y2="85" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/> <text x="54" y="44" font-size="12" text-anchor="end">100%</text> <line x1="60" y1="40" x2="620" y2="40" stroke="#888" stroke-width="0.5" stroke-dasharray="3 3"/>
<rect x="70" y="73.8" width="24" height="146.2" fill="#4a7bd0"/> <text x="82" y="69.8" font-size="11" text-anchor="middle">81</text> <rect x="98" y="56.9" width="24" height="163.1" fill="#e08a3c"/> <text x="110" y="52.9" font-size="11" text-anchor="middle">91</text> <text x="96" y="238" font-size="13" text-anchor="middle">C=3</text> <rect x="150" y="107.5" width="24" height="112.5" fill="#4a7bd0"/> <text x="162" y="103.5" font-size="11" text-anchor="middle">62</text> <rect x="178" y="73.8" width="24" height="146.2" fill="#e08a3c"/> <text x="190" y="69.8" font-size="11" text-anchor="middle">81</text> <text x="176" y="238" font-size="13" text-anchor="middle">C=6</text> <rect x="230" y="152.5" width="24" height="67.5" fill="#4a7bd0"/> <text x="242" y="148.5" font-size="11" text-anchor="middle">38</text> <rect x="258" y="96.2" width="24" height="123.8" fill="#e08a3c"/>
<text x="270" y="92.2" font-size="11" text-anchor="middle">69</text> <text x="256" y="238" font-size="13" text-anchor="middle">C=10</text> <rect x="310" y="175.0" width="24" height="45.0" fill="#4a7bd0"/> <text x="322" y="171.0" font-size="11" text-anchor="middle">25</text> <rect x="338" y="175.0" width="24" height="45.0" fill="#e08a3c"/> <text x="350" y="171.0" font-size="11" text-anchor="middle">25</text> <text x="336" y="238" font-size="13" text-anchor="middle">C=24</text> <rect x="390" y="190.0" width="24" height="30.0" fill="#4a7bd0"/> <text x="402" y="186.0" font-size="11" text-anchor="middle">17</text> <rect x="418" y="152.5" width="24" height="67.5" fill="#e08a3c"/> <text x="430" y="148.5" font-size="11" text-anchor="middle">38</text> <text x="416" y="238" font-size="13" text-anchor="middle">C=40</text> <rect x="470" y="220.0" width="24" height="0.8" fill="#4a7bd0"/>
<text x="482" y="216.0" font-size="11" text-anchor="middle">0</text> <rect x="498" y="197.5" width="24" height="22.5" fill="#e08a3c"/> <text x="510" y="193.5" font-size="11" text-anchor="middle">12</text> <text x="496" y="238" font-size="13" text-anchor="middle">C=112</text> <rect x="550" y="220.0" width="24" height="0.8" fill="#4a7bd0"/> <text x="562" y="216.0" font-size="11" text-anchor="middle">0</text> <rect x="578" y="202.0" width="24" height="18.0" fill="#e08a3c"/> <text x="590" y="198.0" font-size="11" text-anchor="middle">10</text> <text x="576" y="238" font-size="13" text-anchor="middle">C=144</text> <rect x="380" y="16" width="12" height="12" fill="#4a7bd0"/><text x="398" y="27" font-size="12">16 배수로 정렬</text> <rect x="500" y="16" width="12" height="12" fill="#e08a3c"/><text x="518" y="27" font-size="12">32 배수로 정렬</text>
<text x="60" y="258" font-size="12">채널 수 C → 패딩 후 낭비 비율 (padded − C) / padded, 막대 위 숫자 = %</text>
</svg>
```

그림 4 — 채널 수별 패딩 낭비율(예제 11의 숫자). 채널이 적은 **첫 레이어(RGB 3, IMU 6)**와 **마지막 분류 레이어(클래스 10)**에서 낭비가 가장 크다.

출력에서 볼 것과 교훈:

- 첫 레이어는 원래 NPU 효율이 나쁘다(입력 3~6채널). 그래서 벤더들은 첫 conv용 특수 모드를 두거나, 입력을 space-to-depth로 채널을 늘려 넣기도 한다(일반론 — 구체 구현은 벤더마다 다르다).
- MobileNet의 채널 수(16, 24, 32, 64, 96, 160, 320)가 8의 배수인 건 우연이 아니다. **모델을 설계할 때 채널을 HW 정렬 단위의 배수로 잡는 것**이 가장 싼 최적화다 — C7(HW-aware 설계)과 I(co-design)의 출발점. 채널 40을 32 정렬 NPU에 올리면 37.5 %가 낭비되니, 48이나 32로 바꿔 보는 게 낫다.
- 위 MAC 낭비 계산은 "입력·출력 채널이 모두 같은 단위로 패딩된다"는 단순 모델이다. 실제로는 HW마다 어느 축을 몇 단위로 묶는지 다르다 — 벤더 문서/성능 모델로 확인해야 한다.

---

## 8. Operator lowering과 unsupported op 대체

### 8.1 두 방향

- **lowering(decomposition)**: 큰 op를 작은 primitive로 쪼갠다. HW가 큰 op를 모르면 이렇게 해야 실행된다. 1.4절의 `linear → permute + addmm`이 예.
- **fusion(pattern → big op)**: 반대로 작은 op 묶음을 큰 op로 합친다. HW가 큰 op(예: LayerNorm, GELU, Attention)를 네이티브로 지원하면 이게 빠르다.

어느 쪽이 맞는지는 **타깃 HW의 op 지원표**가 결정한다. 같은 모델이 NPU A용으로는 LayerNorm을 쪼개고, NPU B용으로는 합친다. 펌웨어로 치면 "하드웨어 CRC 엔진이 있으면 쓰고, 없으면 소프트웨어 테이블 CRC로 lowering"이다.

### 8.2 자주 쪼개는 op들의 정확한 식

```
 GELU (정확, erf):     GELU(x) = 0.5·x·(1 + erf(x / √2))
 GELU (tanh 근사):     ≈ 0.5·x·(1 + tanh(√(2/π)·(x + 0.044715·x³)))
 GELU (sigmoid 근사):  ≈ x·σ(1.702·x)

 hard-swish:           hswish(x) = x · ReLU6(x + 3) / 6        ← Add, Clip(0,6), Mul, Mul(1/6)
 SiLU (swish):         silu(x)   = x · σ(x)                     ← Sigmoid, Mul

 LayerNorm (마지막 축, 길이 D):
     μ   = ReduceMean(x)
     d   = x − μ
     var = ReduceMean(d · d)
     y   = d / Sqrt(var + ε) · γ + β                            ← primitive 7~9개
```

말로 하면: erf가 없는 HW에선 tanh나 sigmoid로 근사하고(근사 오차가 생긴다), hard-swish와 LayerNorm은 근사 없이 정확히 기본 op로 쪼갤 수 있다(float 반올림 차이만 생긴다).

### 8.3 코드로 등가성 확인 (예제 12)

무엇을 확인하는 코드인지: 위 분해식들이 PyTorch 내장 op와 얼마나 같은지, 최대 절대 오차로 잰다.

```python
import torch, math
import torch.nn.functional as F
torch.manual_seed(0)
x = torch.linspace(-6, 6, 10001)
gelu = F.gelu(x)                                              # 기준: erf 버전
tanh_ap = 0.5 * x * (1 + torch.tanh(math.sqrt(2 / math.pi) * (x + 0.044715 * x**3)))
sig_ap = x * torch.sigmoid(1.702 * x)
print(f"GELU tanh approx  max err {(gelu - tanh_ap).abs().max():.2e}")
print(f"GELU sigmoid appr max err {(gelu - sig_ap).abs().max():.2e}")
print(f"torch gelu(approximate='tanh') vs mine {(F.gelu(x, approximate='tanh') - tanh_ap).abs().max():.2e}")
hs_prim = x * torch.clamp(x + 3, 0, 6) / 6                   # Add → Clip → Mul → Div
print(f"hardswish = x*relu6(x+3)/6   max err {(F.hardswish(x) - hs_prim).abs().max():.2e}")
# LayerNorm → ReduceMean, Sub, Mul, ReduceMean, Add, Sqrt, Div, Mul, Add
ln = torch.nn.LayerNorm(64).eval()
with torch.no_grad():
    ln.weight.uniform_(0.5, 1.5); ln.bias.uniform_(-0.1, 0.1)
    h = torch.randn(4, 10, 64) * 3 + 1
    mu = h.mean(-1, keepdim=True); d = h - mu
    var = (d * d).mean(-1, keepdim=True)
    prim = d / torch.sqrt(var + ln.eps) * ln.weight + ln.bias
    print(f"LayerNorm primitives max err {(ln(h) - prim).abs().max():.2e}")
```

```text
GELU tanh approx  max err 4.73e-04
GELU sigmoid appr max err 2.03e-02
torch gelu(approximate='tanh') vs mine 0.00e+00
hardswish = x*relu6(x+3)/6   max err 0.00e+00
LayerNorm primitives max err 4.77e-07
```

출력에서 볼 것:

- **정확한 분해**(hard-swish, LayerNorm): 오차 0 또는 float 반올림 수준(4.8e-7). 모델 정확도에 영향 없다.
- **근사 분해**(GELU): tanh 근사는 최대 4.7e-4, sigmoid 근사는 2e-2. int8 양자화 스텝(보통 1e-2 ~ 1e-1 수준)보다 작으면 대개 문제없지만, 0 근처 값이 중요한 레이어라면 C8처럼 레이어별 비교로 확인한다. 가장 좋은 건 **학습 때부터 배포 때 쓸 버전으로 학습**하는 것이다(PyTorch `approximate='tanh'`). 학습과 추론의 수식이 같으면 근사 오차라는 개념 자체가 사라진다.
- LayerNorm을 쪼개면 노드가 1개 → 9개, 중간 텐서가 여러 개 생긴다. 쪼개는 건 "실행은 된다"를 사는 것이지 빠르게 만드는 게 아니다. 그리고 `ReduceMean`의 분산 계산은 int8에서 정밀도가 약하다(분산이 제곱이라 범위가 넓다) — LayerNorm이 NPU에서 16-bit나 float로 남는 흔한 이유다.

### 8.4 unsupported op를 지원되는 패턴으로 바꾸기 (예제 13)

상황: 변환한 모델에 `HardSwish` op가 있는데, 가상의 NPU 컴파일러의 op 지원표에 HardSwish가 없고 Add/Clip/Mul은 있다고 하자. 선택지는 (1) CPU fallback을 받아들인다, (2) 모델을 바꾼다(ReLU로 재학습), (3) **그래프 수술로 등가 패턴을 넣는다**. (3)이 가장 싸다.

무엇을 확인하는 코드인지: Conv + Hardswish를 ONNX로 export하고, `HardSwish` 노드를 `Add → Clip → Mul → Mul(1/6)`로 바꾼 뒤 두 모델을 ORT로 돌려 PyTorch와 비교한다.

```python
import torch, onnx, numpy as np, onnxruntime as ort
from onnx import helper as h, numpy_helper as nh
torch.manual_seed(0)
m = torch.nn.Sequential(torch.nn.Conv2d(3, 8, 3, padding=1), torch.nn.Hardswish()).eval()
x = torch.randn(1, 3, 8, 8)
torch.onnx.export(m, (x,), "hs.onnx", input_names=["x"], output_names=["y"],
                  dynamo=True, external_data=False, verbose=False)
model = onnx.load("hs.onnx"); g = model.graph
print("before:", [n.op_type for n in g.node])
g.initializer.extend([nh.from_array(np.array(v, np.float32), k)
                      for k, v in [("c3", 3.0), ("c0", 0.0), ("c6", 6.0), ("c1_6", 1 / 6)]])
new = []
for n in g.node:
    if n.op_type != "HardSwish": new.append(n); continue
    i, o = n.input[0], n.output[0]                 # NPU가 HardSwish를 모른다고 가정
    new += [h.make_node("Add", [i, "c3"], [o + "_p3"]),
            h.make_node("Clip", [o + "_p3", "c0", "c6"], [o + "_r6"]),   # = ReLU6
            h.make_node("Mul", [i, o + "_r6"], [o + "_xm"]),
            h.make_node("Mul", [o + "_xm", "c1_6"], [o])]                 # ÷6 → ×(1/6)
del g.node[:]; g.node.extend(new); onnx.checker.check_model(model)
onnx.save(model, "hs_lowered.onnx")
print("after :", [n.op_type for n in g.node])
xs = x.numpy(); ref = m(x).detach().numpy()
for f in ("hs.onnx", "hs_lowered.onnx"):
    y = ort.InferenceSession(f, providers=["CPUExecutionProvider"]).run(None, {"x": xs})[0]
    print(f"{f:<16} max|y - torch| = {np.abs(y - ref).max():.1e}")
```

```text
before: ['Conv', 'HardSwish']
after : ['Conv', 'Add', 'Clip', 'Mul', 'Mul']
hs.onnx          max|y - torch| = 2.4e-07
hs_lowered.onnx  max|y - torch| = 1.2e-07
```

출력에서 볼 것: 수술 전후 모두 PyTorch와 1e-7 수준으로 일치한다. 노드는 1 → 4개로 늘었지만 **전부 NPU 위에 머무른다**. 12절에서 보겠지만 fallback 한 번의 비용(데이터를 CPU로 넘겼다가 다시 가져오는 것)은 원소별 op 몇 개보다 훨씬 비싸다.

실무 팁:

- `÷6`을 `×(1/6)`로 바꾼 건 의도적이다. 많은 NPU/DSP에 나눗셈 유닛이 없거나 느리다. `1/6`은 float로 정확히 표현되지 않아 반올림 오차가 생기지만 1e-7급이다.
- `Clip`의 min/max가 **입력 텐서**(opset 11+)인지 **속성**(opset 6)인지 op 지원표가 opset 버전까지 따지는 경우가 있다. "op 이름은 지원되는데 이 opset 형태는 안 된다"는 흔한 bring-up 이슈다.
- 교체 패턴이 양자화 이후라면 각 중간 텐서에 scale/zero-point가 필요하다(C1). int8에서 `x+3`, `×1/6` 단계마다 requantize가 들어가 정확도가 달라질 수 있으니 C8 방식으로 비교한다.
- 대체가 불가능한 op(예: 데이터 의존적인 제어 흐름, NMS, TopK 같은 정렬류)는 **그래프를 잘라** NPU 부분 + CPU 후처리로 나누는 게 정석이다. 모델 끝의 후처리는 CPU로 떨어져도 경계가 한 번뿐이라 괜찮다.

---

## 9. Static shape vs dynamic shape

### 9.1 NPU는 왜 static shape을 원하나

컴파일러가 하는 일 대부분이 **shape을 숫자로 알아야** 가능하다.

| 컴파일 단계 | shape이 필요한 이유 |
|---|---|
| memory planning (10절) | 텐서 바이트 수 = 원소 수 × dtype 크기. 모르면 arena 크기를 못 정한다 |
| tiling | on-chip SRAM(수백 KB)에 맞게 텐서를 자르는 크기를 계산 |
| DMA descriptor 생성 | 주소·길이·stride를 컴파일 타임에 고정해 명령 스트림에 굽는다 |
| constant folding (3절) | `Shape → Gather → Reshape`가 접히려면 shape이 상수여야 |
| 채널 정렬·layout 결정 (7절) | 패딩량을 정하려면 크기를 알아야 |
| 성능 예측 | 컴파일러가 cycle 추정·SRAM 사용량 리포트를 내려면 |

펌웨어 비유: DMA descriptor 체인을 부팅 때 한 번 만들어 두고 계속 재사용하는 것 vs 매 전송마다 길이를 계산해 descriptor를 새로 쓰는 것. NPU 명령 스트림은 전자처럼 "미리 구운" 것이라 크기가 바뀌면 다시 컴파일해야 한다. 그래서 많은 NPU 툴체인은 dynamic dim이 있으면 거부하거나, 특정 크기로 고정하라고 요구하거나, 몇 개 크기별로 따로 컴파일한 그래프를 들고 다니게 한다(LLM의 prefill/decode 그래프를 따로 만드는 것이 대표 예 — L 모듈).

### 9.2 export 시 static/dynamic 차이 (예제 14)

무엇을 확인하는 코드인지: 같은 모델을 static으로, 그리고 batch 차원을 `Dim("batch")`로 dynamic하게 export해서 shape inference 결과를 비교하고, ORT 도구로 dynamic dim을 고정한다.

```python
import torch, onnx, collections
from onnx import shape_inference
class Net(torch.nn.Module):
    def __init__(self):
        super().__init__(); self.c = torch.nn.Conv2d(1, 4, 3, padding=1); self.fc = torch.nn.Linear(4 * 8 * 8, 3)
    def forward(self, x):
        y = torch.relu(self.c(x))
        return self.fc(y.reshape(y.shape[0], -1))          # batch 크기를 런타임에 읽는다
m, x = Net().eval(), torch.randn(1, 1, 8, 8)
torch.onnx.export(m, (x,), "static.onnx", input_names=["x"], dynamo=True, external_data=False, verbose=False)
torch.onnx.export(m, (x,), "dynamic.onnx", input_names=["x"], dynamo=True, external_data=False, verbose=False,
                  dynamic_shapes={"x": {0: torch.export.Dim("batch")}})
def dims(v): return [d.dim_param or d.dim_value for d in v.type.tensor_type.shape.dim]
for f in ("static.onnx", "dynamic.onnx"):
    g = shape_inference.infer_shapes(onnx.load(f)).graph
    print(f, "ops:", dict(collections.Counter(n.op_type for n in g.node)))
    print("   x:", dims(g.input[0]), " relu out:", [dims(v) for v in g.value_info if v.name.startswith("relu")],
          " output:", dims(g.output[0]))
from onnxruntime.tools.onnx_model_utils import make_dim_param_fixed, fix_output_shapes
fixed = onnx.load("dynamic.onnx")
make_dim_param_fixed(fixed.graph, "batch", 1); fix_output_shapes(fixed)
g = shape_inference.infer_shapes(fixed).graph
print("fixed  x:", dims(g.input[0]), " output:", dims(g.output[0]))
```

```text
static.onnx ops: {'Conv': 1, 'Relu': 1, 'Reshape': 1, 'Gemm': 1}
   x: [1, 1, 8, 8]  relu out: [[1, 4, 8, 8]]  output: [1, 3]
dynamic.onnx ops: {'Conv': 1, 'Relu': 1, 'Reshape': 1, 'Gemm': 1}
   x: ['batch', 1, 8, 8]  relu out: [['batch', 4, 8, 8]]  output: ['batch', 3]
fixed  x: [1, 1, 8, 8]  output: [1, 3]
```

출력에서 볼 것:

- dynamic export에서는 shape에 숫자 대신 **기호 `'batch'`**가 들어간다. `relu` 출력 크기가 `batch × 4 × 8 × 8 × 4 B` — 컴파일러 입장에선 "arena를 얼마나 잡아야 할지 모름"이다.
- 이 모델은 exporter 최적화 덕분에 op 목록은 같다(`Reshape`의 목표 shape가 `[-1, 256]` 상수로 정리됐다). 더 복잡한 모델에서는 dynamic dim이 `Shape/Gather/Concat` 같은 런타임 shape 계산 노드를 남기고, 그게 NPU 미지원 op가 된다.
- `make_dim_param_fixed`로 `'batch' = 1`을 박으면 모든 shape이 숫자로 돌아온다. ORT에는 같은 기능의 CLI도 있다: `python -m onnxruntime.tools.make_dynamic_shape_fixed --dim_param batch --dim_value 1 in.onnx out.onnx`.

### 9.3 실무 규칙

- **export할 때부터 고정한다**: `torch.export`에 기기에서 쓸 정확한 예제 입력 크기(예: 오디오 1초 = 49 프레임 × 40 mel)를 주고 `dynamic_shapes`를 주지 않는다. 가장 깔끔하다.
- 스트리밍 모델(wake word, IMU)은 **고정 크기 창(window) + 상태(state) 입력/출력**으로 설계한다. 가변 길이 대신 "매 20 ms마다 같은 크기 청크"를 넣는다. B2 8절의 링버퍼 스트리밍 conv가 이 구조다.
- 가변 길이가 꼭 필요하면 몇 개 bucket(예: 길이 64/128/256)으로 각각 컴파일하고, 입력을 가장 가까운 bucket으로 패딩한다. LLM의 prefill 길이가 대표 예.
- batch 차원은 edge에서 거의 항상 1이다. `batch = 1`로 고정하는 데 망설일 이유가 없다.

---

## 10. Memory planning — tensor lifetime과 arena 재사용

### 10.1 직관: 레지스터 할당과 같다

MCU에는 `malloc`을 안 쓴다. TFLite Micro는 사용자가 준 정적 배열 하나(`uint8_t tensor_arena[N]`)에 모든 중간 텐서를 배치한다(F2). 질문은 **N을 얼마로 잡아야 하나**다.

- **naive**: 텐서마다 자기 버퍼 → 모든 활성값 크기의 합.
- **재사용**: 텐서 A가 마지막으로 읽힌 뒤에는 A의 자리를 다른 텐서가 써도 된다. 동시에 살아 있지 않은 텐서끼리 주소를 공유한다.

이건 C 컴파일러의 **register allocation**과 같은 문제다: 변수의 live range가 겹치지 않으면 같은 레지스터를 쓴다. 차이는 레지스터는 크기가 같고 텐서는 크기가 제각각이라, "구간이 겹치면 주소 범위도 겹치면 안 되는" **2차원 패킹**(시간 × 주소) 문제가 된다는 것이다. 최적해는 NP-hard라 실제 도구는 greedy 휴리스틱을 쓴다.

```
 정의:  tensor t의 lifetime = [생성 step, 마지막 사용 step]
        두 텐서 a, b가 lifetime이 겹치면 → 주소 범위 [off, off+size)도 겹치면 안 된다
        arena 크기 = max_t (offset_t + size_t)
        하한(lower bound) = max_step Σ(그 step에 살아 있는 텐서 크기)
```

말로 하면: 어느 순간이든 동시에 살아 있는 텐서들은 전부 arena 안에 따로 있어야 하므로 arena는 "가장 붐비는 순간의 합"보다 작을 수 없다. 좋은 planner는 이 하한에 가깝게 배치한다.

### 10.2 손계산 — MobileNetV2 블록 하나

96×96 RGB 입력, int8 활성값. conv1(stride 2) → dw0 → [expand(1×1, 16→48) → dw → project(48→16)] + skip → conv_s2 → GAP → FC.

```
 step  op        생성 텐서  크기                    살아 있는 텐서 (그 step)
  1    conv1     a1         48·48·16 = 36,864 B    in(27,648) + a1               =  64,512
  2    dw0       a2         36,864 B               a1 + a2                        =  73,728
  3    expand    a3         48·48·48 = 110,592 B   a2 + a3                        = 147,456
  4    dw        a4         110,592 B              a2 + a3 + a4                   = 258,048  ← peak
  5    project   a5         36,864 B               a2 + a4 + a5                   = 184,320
  6    add       a6         36,864 B               a2 + a5 + a6                   = 110,592
  7    conv_s2   a7         24·24·32 = 18,432 B    a6 + a7                        =  55,296
```

peak = 258,048 B = 252 KB (step 4). **skip connection 때문에 `a2`가 step 2부터 6까지 살아 있다** — 그래서 가장 큰 두 텐서(a3, a4)가 동시에 살아 있는 step 4에 a2가 얹힌다. residual 블록이 메모리를 더 먹는 이유다.

### 10.3 greedy planner 구현 (예제 15 — `mem_planner.py`)

무엇을 확인하는 코드인지: lifetime 계산, "큰 텐서부터 겹치지 않는 가장 낮은 offset에 놓는" greedy 배치(TFLite Micro의 `GreedyMemoryPlanner`와 같은 발상), 하한 계산을 구현한다. 11절에서도 import 하므로 파일로 저장한다.

```python
def lifetimes(order, graph, inputs):
    """order: 실행할 노드 이름 순서. graph: {node: (입력 텐서들, 출력 텐서)}. 반환: {tensor: [first, last]}"""
    life = {t: [0, 0] for t in inputs}
    for step, node in enumerate(order, start=1):
        ins, out = graph[node]
        for t in ins: life[t][1] = step              # 마지막 사용 시점 갱신
        life[out] = [step, step]                     # 생성 시점
    life[graph[order[-1]][1]][1] = len(order) + 1    # 최종 출력은 끝까지 살아 있다
    return life
def overlap(a, b): return not (a[1] < b[0] or b[1] < a[0])
def greedy_by_size(life, size):
    """TFLM GreedyMemoryPlanner와 같은 발상: 큰 텐서부터, 수명이 겹치는 것과 주소가 안 겹치는 가장 낮은 offset"""
    placed = {}
    for t in sorted(life, key=lambda t: -size[t]):
        clash = sorted((placed[u], placed[u] + size[u]) for u in placed if overlap(life[t], life[u]))
        off = 0
        for lo, hi in clash:
            if off + size[t] <= lo: break            # 이 틈에 들어간다
            off = max(off, hi)
        placed[t] = off
    return placed, max(placed[t] + size[t] for t in placed)
def peak_live(life, size):
    steps = range(0, max(l[1] for l in life.values()) + 1)
    return max(sum(size[t] for t in life if life[t][0] <= s <= life[t][1]) for s in steps)
```

(출력 없음 — 모듈이다.)

### 10.4 MobileNetV2 블록에 적용 (예제 16)

무엇을 확인하는 코드인지: 10.2절 그래프에 planner를 돌려 naive / 하한 / greedy arena를 비교한다.

```python
from mem_planner import lifetimes, greedy_by_size, peak_live
KB = 1024  # MobileNetV2식 블록 하나 + head, int8 활성값 (바이트)
size = {"in": 96*96*3, "a1": 48*48*16, "a2": 48*48*16, "a3": 48*48*48, "a4": 48*48*48,
        "a5": 48*48*16, "a6": 48*48*16, "a7": 24*24*32, "a8": 32, "out": 2}
graph = {"conv1": (["in"], "a1"), "dw0": (["a1"], "a2"),
         "expand": (["a2"], "a3"), "dw": (["a3"], "a4"), "project": (["a4"], "a5"),
         "add": (["a5", "a2"], "a6"),                    # skip connection: a2가 오래 산다
         "conv_s2": (["a6"], "a7"), "gap": (["a7"], "a8"), "fc": (["a8"], "out")}
order = list(graph)
life = lifetimes(order, graph, ["in"])
offs, arena = greedy_by_size(life, size)
for t in sorted(life, key=lambda t: life[t][0]):
    print(f"{t:<4} {size[t]:>7,d} B  life={str(life[t]):<7}  offset={offs[t]/KB:6.1f} KB")
print(f"naive (tensor마다 버퍼) = {sum(size.values())/KB:6.1f} KB")
print(f"peak live (하한)        = {peak_live(life, size)/KB:6.1f} KB")
print(f"greedy arena            = {arena/KB:6.1f} KB")
```

```text
in    27,648 B  life=[0, 1]   offset=  36.0 KB
a1    36,864 B  life=[1, 2]   offset=   0.0 KB
a2    36,864 B  life=[2, 6]   offset= 216.0 KB
a3   110,592 B  life=[3, 4]   offset=   0.0 KB
a4   110,592 B  life=[4, 5]   offset= 108.0 KB
a5    36,864 B  life=[5, 6]   offset=   0.0 KB
a6    36,864 B  life=[6, 7]   offset=  36.0 KB
a7    18,432 B  life=[7, 8]   offset=   0.0 KB
a8        32 B  life=[8, 9]   offset=  18.0 KB
out        2 B  life=[9, 10]  offset=   0.0 KB
naive (tensor마다 버퍼) =  405.0 KB
peak live (하한)        =  252.0 KB
greedy arena            =  252.0 KB
```

```svg
<svg viewBox="0 0 660 340" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="290" x2="642" y2="290" stroke="currentColor"/> <line x1="70" y1="290" x2="70" y2="28" stroke="currentColor"/> <text x="64" y="294" font-size="12" text-anchor="end">0</text> <text x="64" y="244" font-size="12" text-anchor="end">50</text> <text x="64" y="194" font-size="12" text-anchor="end">100</text> <text x="64" y="144" font-size="12" text-anchor="end">150</text> <text x="64" y="94" font-size="12" text-anchor="end">200</text> <text x="64" y="42" font-size="12" text-anchor="end">252</text> <text x="14" y="160" font-size="12" transform="rotate(-90 14 160)" text-anchor="middle">arena offset (KB)</text> <text x="96" y="306" font-size="12" text-anchor="middle">0</text> <text x="148" y="306" font-size="12" text-anchor="middle">1</text> <text x="200" y="306" font-size="12" text-anchor="middle">2</text> <text x="252" y="306" font-size="12" text-anchor="middle">3</text>
<text x="304" y="306" font-size="12" text-anchor="middle">4</text> <text x="356" y="306" font-size="12" text-anchor="middle">5</text> <text x="408" y="306" font-size="12" text-anchor="middle">6</text> <text x="460" y="306" font-size="12" text-anchor="middle">7</text> <text x="512" y="306" font-size="12" text-anchor="middle">8</text> <text x="564" y="306" font-size="12" text-anchor="middle">9</text> <text x="616" y="306" font-size="12" text-anchor="middle">10</text> <text x="356" y="324" font-size="12" text-anchor="middle">실행 step (1 = conv1, 4 = dw, 6 = add, 9 = fc)</text> <line x1="70" y1="38" x2="642" y2="38" stroke="#d0564a" stroke-dasharray="4 3"/> <text x="638" y="34" font-size="12" text-anchor="end">arena = 252 KB</text> <rect x="71" y="227.0" width="102" height="27.0" fill="#888" fill-opacity="0.55" stroke="#888"/>
<text x="122" y="244.5" font-size="12" text-anchor="middle">in 27K</text> <rect x="123" y="254.0" width="102" height="36.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/> <text x="174" y="276.0" font-size="12" text-anchor="middle">a1 36K</text> <rect x="175" y="38.0" width="258" height="36.0" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/> <text x="304" y="60.0" font-size="12" text-anchor="middle">a2 36K</text> <rect x="227" y="182.0" width="102" height="108.0" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/> <text x="278" y="240.0" font-size="12" text-anchor="middle">a3 108K</text> <rect x="279" y="74.0" width="102" height="108.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/> <text x="330" y="132.0" font-size="12" text-anchor="middle">a4 108K</text> <rect x="331" y="254.0" width="102" height="36.0" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="382" y="276.0" font-size="12" text-anchor="middle">a5 36K</text> <rect x="383" y="218.0" width="102" height="36.0" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/> <text x="434" y="240.0" font-size="12" text-anchor="middle">a6 36K</text> <rect x="435" y="272.0" width="102" height="18.0" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/> <text x="486" y="285.0" font-size="12" text-anchor="middle">a7 18K</text> <rect x="487" y="269.0" width="102" height="3.0" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/> <rect x="539" y="287.0" width="102" height="3.0" fill="#888" fill-opacity="0.55" stroke="#888"/>
</svg>
```

그림 5 — 예제 16의 배치를 "시간(step) × 주소(offset)" 평면에 그린 것. 사각형 하나 = 텐서 하나(가로 = lifetime, 세로 = 크기, 위치 = offset). 가로로 겹치는 사각형끼리는 세로로 겹치지 않는다. 빨간 점선이 arena 끝(252 KB). 오른쪽 아래의 작은 사각형들(a8, out)은 너무 작아 선으로만 보인다.

출력에서 볼 것:

- naive 405 KB → 재사용 252 KB (38 % 절약). greedy가 하한과 **정확히 같은** 252 KB를 찾았다. 이 그래프는 쉬운 편이다. 일반적으로 greedy는 단편화(fragmentation) 때문에 하한보다 조금 클 수 있다.
- 그림 5에서 a3(0~108 KB)와 a5(0~36 KB), a7(0~18 KB)이 같은 주소를 시간차로 재사용한다. 펌웨어의 overlay 섹션과 같은 발상이다.
- 이 숫자가 곧 **"이 모델을 이 해상도로 SRAM 256 KB MCU에 올릴 수 있나?"**의 답이다: arena 252 KB + 런타임 오버헤드(TFLM은 텐서 메타데이터·스크래치 버퍼도 arena에 넣는다) → 빠듯하거나 넘친다. 해결책: 입력 해상도를 줄이거나, expand 비율을 낮추거나(48 → 32), **patch 기반 실행**(MCUNetV2처럼 초반 레이어를 공간 타일로 나눠 실행), in-place 연산(아래).
- 여기서 빠진 최적화: **in-place**. `add`는 a5 자리에 결과를 써도 된다(원소별 op는 입력을 다 읽은 칸에 바로 쓸 수 있다). ReLU 같은 원소별 op도 마찬가지. 실제 planner는 이런 "출력이 입력 버퍼를 재사용 가능" 정보를 op마다 가진다. depthwise conv는 이웃 픽셀을 읽으므로 단순 in-place가 안 된다.

### 10.5 실제 런타임은 arena 크기를 어떻게 정하나

- **TFLite Micro**: `MicroInterpreter` 생성 후 `AllocateTensors()`를 부르면 planner가 lifetime을 계산해 arena 안에 배치한다. arena가 모자라면 실패하고 로그를 남긴다. 성공하면 `arena_used_bytes()`로 실제 사용량을 알 수 있다. 실무 절차는 "넉넉히 잡고 → 호스트나 보드에서 한 번 돌려 `arena_used_bytes()` 확인 → 여유분을 더해 확정"이다.
- **NPU 컴파일러**(예: Vela, QNN): 컴파일 타임에 텐서 배치와 SRAM/DRAM 사용량을 계산해 리포트로 준다(13절). on-chip SRAM이 모자라면 일부 텐서를 외부 메모리로 spill 하거나 tiling을 더 잘게 한다 — 성능이 떨어진다.
- 여기에 **가중치**는 보통 포함되지 않는다: 가중치는 flash/DRAM(`.rodata`)에 두고 읽는다. MCU에서 XIP flash 읽기가 느리면 핫한 레이어 weight를 SRAM에 복사하는 선택도 있다(F7 링커 스크립트 배치).

---

## 11. 실행 순서(scheduling)가 peak memory를 바꾼다

### 11.1 직관

그래프가 한 줄(linear chain)이면 실행 순서는 하나뿐이다. **가지(branch)가 있으면** 위상 정렬이 여러 개 가능하고, 어떤 순서를 고르느냐에 따라 동시에 살아 있는 텐서 집합이 달라진다. Inception·NAS 모델, 멀티헤드 구조, 여러 센서 입력을 합치는 모델에서 흔하다.

예: 입력 x에서 세 가지(A, B, C)가 나가서 각각 크게 펼쳤다가(expand) 작게 줄인(reduce) 뒤 concat.

- 나쁜 순서: A, B, C를 **먼저 다 펼친다** → 큰 텐서 셋이 동시에 살아 있다.
- 좋은 순서: A를 펼치고 바로 줄이고, 그다음 B … → 큰 텐서는 한 번에 하나만.

### 11.2 모든 순서를 다 세어 보기 (예제 17)

무엇을 확인하는 코드인지: 7노드 그래프의 모든 유효한 위상 정렬을 나열하고, 각 순서에서 greedy arena와 peak를 구해 최선/최악을 비교한다.

```python
import itertools
from mem_planner import lifetimes, greedy_by_size, peak_live
KB = 1024  # 두 가지(branch)가 각각 크게 펼쳤다가(expand) 줄이고(reduce) 마지막에 concat
size = {"x": 16*KB, "A1": 64*KB, "A2": 8*KB, "B1": 64*KB, "B2": 8*KB, "C1": 48*KB, "C2": 8*KB, "y": 24*KB}
graph = {"A_exp": (["x"], "A1"), "A_red": (["A1"], "A2"),
         "B_exp": (["x"], "B1"), "B_red": (["B1"], "B2"),
         "C_exp": (["x"], "C1"), "C_red": (["C1"], "C2"),
         "concat": (["A2", "B2", "C2"], "y")}
def topo_orders(graph):
    producer = {out: n for n, (_, out) in graph.items()}
    deps = {n: {producer[t] for t in ins if t in producer} for n, (ins, _) in graph.items()}
    for perm in itertools.permutations(graph):
        pos = {n: i for i, n in enumerate(perm)}
        if all(pos[d] < pos[n] for n in graph for d in deps[n]): yield list(perm)
res = []
for order in topo_orders(graph):
    life = lifetimes(order, graph, ["x"])
    res.append((greedy_by_size(life, size)[1], peak_live(life, size), order))
res.sort()
print("valid orders:", len(res))
for tag, (arena, peak, order) in (("best ", res[0]), ("worst", res[-1])):
    print(f"{tag} arena={arena//KB:>3} KB peak={peak//KB:>3} KB", " → ".join(order))
```

```text
valid orders: 90
best  arena= 96 KB peak= 96 KB A_exp → A_red → B_exp → B_red → C_exp → C_red → concat
worst arena=192 KB peak=192 KB C_exp → B_exp → A_exp → C_red → B_red → A_red → concat
```

```svg
<svg viewBox="0 0 640 280" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="230" x2="602" y2="230" stroke="currentColor"/> <line x1="70" y1="230" x2="70" y2="25" stroke="currentColor"/> <text x="64" y="234" font-size="12" text-anchor="end">0</text> <text x="64" y="184" font-size="12" text-anchor="end">50</text> <text x="64" y="134" font-size="12" text-anchor="end">100</text> <text x="64" y="84" font-size="12" text-anchor="end">150</text> <text x="64" y="34" font-size="12" text-anchor="end">200</text> <text x="16" y="130" font-size="12" transform="rotate(-90 16 130)" text-anchor="middle">live KB</text> <text x="70" y="246" font-size="12" text-anchor="middle">0</text> <text x="134" y="246" font-size="12" text-anchor="middle">1</text> <text x="198" y="246" font-size="12" text-anchor="middle">2</text> <text x="262" y="246" font-size="12" text-anchor="middle">3</text> <text x="326" y="246" font-size="12" text-anchor="middle">4</text>
<text x="390" y="246" font-size="12" text-anchor="middle">5</text> <text x="454" y="246" font-size="12" text-anchor="middle">6</text> <text x="518" y="246" font-size="12" text-anchor="middle">7</text> <text x="582" y="246" font-size="12" text-anchor="middle">8</text> <polyline points="70,214 134,166 198,102 262,38 326,46 390,86 454,142 518,182 582,206" fill="none" stroke="#d0564a" stroke-width="2.5"/> <circle cx="70" cy="214" r="3" fill="#d0564a"/> <circle cx="134" cy="166" r="3" fill="#d0564a"/> <circle cx="198" cy="102" r="3" fill="#d0564a"/> <circle cx="262" cy="38" r="3" fill="#d0564a"/> <circle cx="326" cy="46" r="3" fill="#d0564a"/> <circle cx="390" cy="86" r="3" fill="#d0564a"/> <circle cx="454" cy="142" r="3" fill="#d0564a"/> <circle cx="518" cy="182" r="3" fill="#d0564a"/> <circle cx="582" cy="206" r="3" fill="#d0564a"/>
<polyline points="70,214 134,150 198,142 262,142 326,134 390,150 454,158 518,182 582,206" fill="none" stroke="#3f9a6b" stroke-width="2.5"/> <circle cx="70" cy="214" r="3" fill="#3f9a6b"/> <circle cx="134" cy="150" r="3" fill="#3f9a6b"/> <circle cx="198" cy="142" r="3" fill="#3f9a6b"/> <circle cx="262" cy="142" r="3" fill="#3f9a6b"/> <circle cx="326" cy="134" r="3" fill="#3f9a6b"/> <circle cx="390" cy="150" r="3" fill="#3f9a6b"/> <circle cx="454" cy="158" r="3" fill="#3f9a6b"/> <circle cx="518" cy="182" r="3" fill="#3f9a6b"/> <circle cx="582" cy="206" r="3" fill="#3f9a6b"/> <line x1="330" y1="30" x2="355" y2="30" stroke="#d0564a" stroke-width="2.5"/><text x="362" y="34" font-size="12">worst: 세 가지를 먼저 다 펼침 → 192 KB</text> <line x1="330" y1="50" x2="355" y2="50" stroke="#3f9a6b" stroke-width="2.5"/><text x="362" y="54" font-size="12">best: 한 가지씩 끝냄 → 96 KB</text>
<text x="326" y="266" font-size="12" text-anchor="middle">실행 step (0 = 입력만, 7 = concat, 8 = 출력만)</text>
</svg>
```

그림 6 — 최선/최악 순서에서 step별 살아 있는 메모리(KB). 같은 그래프, 같은 연산량인데 peak가 **2배** 차이 난다.

손으로 확인: 최악 순서의 step 3(A_exp 직후) = x 16 + C1 48 + B1 64 + A1 64 = 192 KB. 최선 순서의 peak는 step 4(B_red) = x 16 + A2 8 + B1 64 + B2 8 = 96 KB.

출력에서 볼 것과 교훈:

- 가능한 순서는 90개, peak는 96 KB ~ 192 KB. **연산은 하나도 안 바뀌고 순서만 바꿔서** SRAM 요구량이 절반이 된다. 펌웨어에서 큰 로컬 배열을 쓰는 함수 호출 순서를 바꿔 최대 스택 깊이를 줄이는 것과 같다.
- 원칙: "만든 큰 텐서는 빨리 소비해서 죽인다(depth-first)". 실제 도구의 스케줄러는 이런 휴리스틱을 쓰거나, 작은 그래프면 탐색한다. 모든 순서를 나열하는 건 노드 수에 대해 지수적이라 이 예제처럼 작을 때만 가능하다.
- 주의: 순서는 memory만이 아니라 **병렬성**에도 영향을 준다. NPU와 DSP가 동시에 돌 수 있다면 A와 B를 겹쳐 실행하는 게 latency에는 좋지만 memory에는 나쁘다. latency와 SRAM의 trade-off다.
- 스케줄링과 memory planning은 서로 얽혀 있다. 그래서 컴파일러 리포트의 SRAM 숫자가 모델 구조만 보고 계산한 값과 다를 수 있다.

---

## 12. 벤더 컴파일러는 이걸 어떻게 쓰나 — 그리고 CPU fallback

### 12.1 세 도구의 대략적인 모습 (일반론 — 세부는 버전·문서로 확인)

| 도구 | 입력 → 출력 | 그래프 최적화에서 하는 일 (대표) | unsupported op 처리 |
|---|---|---|---|
| **TFLite converter** (LiteRT) | TF/Keras SavedModel(또는 PyTorch → ai-edge-torch 경유) → `.tflite` flatbuffer | constant folding, BN folding, activation을 `fused_activation_function`으로 흡수, NHWC layout, 양자화(PTQ) | TFLite builtin op에 없으면 변환 실패 또는 Select TF ops(용량 큼, MCU 불가) |
| **Arm Vela** (Ethos-U) | int8/int16 양자화 `.tflite` → 최적화된 `.tflite` | 지원되는 연속 구간을 **`ethos-u` 커스텀 op 하나**로 묶고, 그 안에서 스케줄링·tiling·SRAM 배치·명령 스트림 생성 | 지원 안 되는 op는 원래 TFLite op로 남아 TFLite Micro가 Cortex-M CPU(CMSIS-NN 커널)에서 실행 |
| **Qualcomm QNN** (AI Engine Direct) | ONNX/TFLite/PyTorch → converter → QNN 모델 → (HTP용) context binary | op 변환·fusion·layout·양자화 인코딩 적용, backend(CPU/GPU/HTP)별 컴파일 | HTP backend가 모르는 op는 컴파일 실패하거나, ORT QNN EP·TFLite delegate 같은 상위 런타임이 그 부분을 CPU로 분할 실행 |

공통점: 모두 이 노트의 패스(fold, fuse, layout, lower, plan)를 내부에서 돌리고, **입력 그래프가 깨끗할수록** 잘 된다. 벤더가 "모델을 넘기기 전에 onnx-simplifier / onnxscript optimizer를 돌려라"라고 권하는 이유다.

### 12.2 CPU fallback — 왜 op 하나가 성능을 죽이나

그래프 중간의 op 하나를 NPU가 지원하지 않으면 그래프가 쪼개진다: NPU 구간 1 → CPU op → NPU 구간 2. 경계마다 이런 일이 생긴다.

- NPU 출력을 CPU가 읽을 수 있는 메모리로 옮김(on-chip SRAM → DDR, 또는 캐시 flush/invalidate).
- layout·양자화 형식 변환(NPU 내부 blocked layout → NHWC, 경우에 따라 dequantize/quantize).
- NPU 작업 완료 대기(interrupt/폴링) → CPU op 실행 → 다시 NPU에 작업 제출(드라이버 호출, 명령 스트림 시작 오버헤드).
- CPU op 자체가 느리다(NPU 대비 수십 배 느린 경우도 흔하다).
- NPU 구간이 쪼개지면 두 구간 사이의 **fusion·tiling·SRAM 재사용 기회도 사라진다**.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="c6m4" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="10" y="20" font-size="13">op 매핑: 파랑 = NPU 지원, 주황 = 미지원 → CPU fallback</text> <rect x="10" y="36" width="62" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0" stroke-width="2"/><text x="41" y="57" font-size="12" text-anchor="middle">Conv</text> <rect x="82" y="36" width="62" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0" stroke-width="2"/><text x="113" y="57" font-size="12" text-anchor="middle">DWConv</text> <rect x="154" y="36" width="62" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0" stroke-width="2"/><text x="185" y="57" font-size="12" text-anchor="middle">Conv</text>
<rect x="226" y="36" width="86" height="32" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c" stroke-width="2"/><text x="269" y="57" font-size="12" text-anchor="middle">미지원 op</text> <rect x="322" y="36" width="62" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0" stroke-width="2"/><text x="353" y="57" font-size="12" text-anchor="middle">Conv</text> <rect x="394" y="36" width="62" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0" stroke-width="2"/><text x="425" y="57" font-size="12" text-anchor="middle">DWConv</text> <rect x="466" y="36" width="62" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0" stroke-width="2"/><text x="497" y="57" font-size="12" text-anchor="middle">Conv</text>
<rect x="538" y="36" width="62" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0" stroke-width="2"/><text x="569" y="57" font-size="12" text-anchor="middle">FC</text> <text x="10" y="110" font-size="13">실행 타임라인 (숫자는 설명용 가상 값)</text> <text x="10" y="143" font-size="12">NPU</text> <text x="10" y="183" font-size="12">CPU</text> <line x1="50" y1="200" x2="660" y2="200" stroke="currentColor"/> <rect x="50" y="126" width="90" height="26" fill="#4a7bd0" fill-opacity="0.6"/><text x="95" y="143" font-size="12" text-anchor="middle">구간 1</text> <rect x="140" y="126" width="40" height="26" fill="#d0564a" fill-opacity="0.5"/> <rect x="180" y="166" width="230" height="26" fill="#e08a3c" fill-opacity="0.6"/><text x="295" y="183" font-size="12" text-anchor="middle">미지원 op (CPU, 느림)</text> <rect x="410" y="126" width="40" height="26" fill="#d0564a" fill-opacity="0.5"/>
<rect x="450" y="126" width="120" height="26" fill="#4a7bd0" fill-opacity="0.6"/><text x="510" y="143" font-size="12" text-anchor="middle">구간 2</text> <line x1="160" y1="152" x2="182" y2="166" stroke="currentColor" marker-end="url(#c6m4)"/> <line x1="410" y1="166" x2="428" y2="152" stroke="currentColor" marker-end="url(#c6m4)"/> <text x="10" y="225" font-size="12">빨강 = 경계 비용: 메모리 복사 · layout/양자화 변환 · 동기화 · NPU 재시작. 경계는 fallback 하나당 2번 생긴다.</text> <text x="10" y="243" font-size="12">NPU가 할 일은 그대로인데 전체 latency는 CPU 구간과 경계 비용 때문에 몇 배로 늘 수 있다.</text>
</svg>
```

그림 7 — 그래프 중간의 op 하나가 fallback 되면 NPU 구간이 둘로 쪼개지고 경계 비용이 두 번 든다(시간 값은 모양을 보여 주기 위한 가상 값).

그래서 규칙: **fallback은 개수보다 위치가 중요하다.** 모델 끝의 후처리(softmax, argmax, NMS) 하나가 CPU로 가는 건 경계가 한 번이라 대개 괜찮다. 백본 한가운데의 op 하나는 치명적이다. 해결 순서는 대략 이렇다(F8에서 자세히).

1. 컴파일러 리포트에서 CPU로 간 op 목록과 **이유**(op 미지원, 이 파라미터 조합 미지원 — 예: stride/kernel 크기 제한, dtype 미지원, 이 opset 형태 미지원, dynamic shape)를 뽑는다.
2. 등가 패턴으로 교체한다(8.4절 그래프 수술) — 가장 싸다.
3. 파라미터를 지원 범위로 바꾼다(예: 커널 크기, 채널 수, 16-bit → 8-bit, dynamic → static).
4. 모델 설계를 바꾸고 재학습한다(예: hard-swish → ReLU6, GELU → ReLU, 지원 안 되는 attention 변형 교체) — C7/I 모듈.
5. 불가능하면 그래프를 잘라 CPU 부분을 끝으로 몰고, 벤더 FAE에 op 지원 요청(재현 가능한 최소 모델 첨부 — Don이 SSD 시절 벤더와 이슈 주고받던 방식 그대로).

---

## 13. 임베디드 관점에서 다시 보기 — 컴파일러 리포트 읽기

벤더마다 형식은 다르지만, NPU 컴파일러는 보통 "요약 + op별 표"를 준다. 예를 들어 Vela는 컴파일 후 SRAM/Flash 사용량, NPU/CPU op 개수와 비율, 추정 cycle 같은 요약을 출력하고, 옵션으로 op별·레이어별 상세를 CSV 등으로 낼 수 있다. QNN 쪽은 컨버터 로그와 프로파일러 출력으로 비슷한 정보를 본다(정확한 필드 이름은 버전별 문서 확인). 무엇을 볼지는 공통이다.

| 확인 항목 | 볼 곳 | 나쁜 신호 | 조치 |
|---|---|---|---|
| NPU vs CPU op 개수·비율 | 요약, op별 매핑 표 | CPU op가 백본 중간에 있음 | 12.2절 순서대로 제거 |
| CPU로 간 **이유** | op별 표의 사유 칸, 컴파일러 경고 | "unsupported"만 있고 사유 없음 | 지원표에서 파라미터 조건 확인 |
| on-chip SRAM 사용량 | 요약 | 한도에 딱 붙음, 또는 spill 발생 | 해상도·채널·scheduling 조정 (10, 11절) |
| off-chip(DRAM/flash) 트래픽 | 요약, 레이어별 | 특정 레이어에 몰림 | 그 레이어 tiling·weight 배치 확인 |
| 가중치 크기 (flash) | 요약 | 예상(파라미터 수 × 바이트)보다 큼 | 안 접힌 상수, 풀린 양자화 weight (3.4절) |
| 남은 Transpose/Reshape/Cast 개수 | op 표 | 수십 개 | layout 정리 (7절), 입력 형식 NHWC로 |
| 추정 cycle/latency (레이어별) | 요약·상세 | 한 레이어가 전체의 대부분 | 그 레이어 모양(채널 정렬 7.5절, depthwise 효율 B2) |
| 정밀도 | 텐서 dtype | 의도와 다른 int16/float 구간 | 양자화 설정·calibration (C1, C2) |
| shape | 입력·중간 텐서 | 기호 dim 남음 | static으로 고정 (9절) |

펌웨어 비유: 이건 **링커 맵 파일 + 타이밍 리포트**를 읽는 일이다. "어느 섹션이 어느 메모리에 갔나, 제일 큰 심볼은 뭔가, 예상과 다른 게 있나"를 보는 눈이 그대로 쓰인다. 좋은 습관 하나: 리포트를 **CI에 넣어 diff**한다. 모델이 바뀔 때 CPU op 수·SRAM 사용량·latency 추정이 회귀하면 빌드를 실패시킨다(C8 회귀 테스트와 짝).

bring-up 때 첫날 체크리스트:

- [ ] 모델을 static shape으로 export했고 shape inference가 모든 텐서를 숫자로 채운다.
- [ ] export 후 cleanup(onnxscript optimizer 등)을 돌려 Constant/Shape/Identity 잔재를 정리했다.
- [ ] BN이 conv에 접혔고 activation이 fusion됐다 (노드 종류를 세어 확인).
- [ ] 벤더 컴파일러 리포트에서 CPU op 0개, 또는 끝부분에만 있다.
- [ ] SRAM 사용량과 arena 크기가 예산 안이다 (여유 10~20 %).
- [ ] 컴파일된 모델 출력을 원래 PyTorch/ONNX 출력과 비교했다 (C8: cosine, max abs diff, 레이어별).

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| train 모드로 export | BN이 안 접힘, 출력이 배치마다 다름 | BN이 batch 통계 사용, Dropout 활성 | `model.eval()` 후 export, 그래프에 training 플래그 확인 |
| cleanup 없이 벤더 도구에 투입 | fusion 안 됨, Shape/Expand "unsupported" | bias 없는 conv 등 export 잔재 (6.2절) | onnxscript optimizer/onnx-simplifier 먼저, 노드 수 비교 |
| dynamic shape 그대로 투입 | 컴파일 실패, Shape op fallback, SRAM 추정 불가 | 기호 dim | export 시 고정 또는 `make_dim_param_fixed` (9절) |
| NCHW flatten 후 FC를 NHWC로 실행 | 출력은 나오는데 정확도 폭락 | flatten 순서 불일치 | 변환기에 맡기거나 FC weight 행을 permute (7.4절) |
| transpose가 수십 개 남음 | NPU 시간 상당 부분이 데이터 이동 | layout 경계가 op마다 생김 | 입력을 NHWC로, transpose 밀어내기 패스, reshape 위치 정리 |
| 채널 수를 HW 정렬과 무관하게 설계 | MAC 이용률 낮음, 예상보다 느림 | 패딩 낭비 (7.5절) | 채널을 8/16/32 배수로 설계 (C7) |
| 근사 lowering을 모르고 적용 | 레이어별 비교에서 작은 오차 누적 | GELU tanh/sigmoid 근사 등 | 학습 때부터 같은 근사 사용, C8로 레이어별 확인 |
| 백본 중간 op 하나 fallback 방치 | NPU인데 CPU보다 느림 | 경계 비용 2번 (12.2절) | 등가 패턴 교체, 파라미터 조정, 재설계 |
| arena를 감으로 잡음 | 기기에서 `AllocateTensors` 실패 또는 SRAM 낭비 | lifetime 무시 | planner/런타임으로 실측 (`arena_used_bytes()`) 후 여유 추가 |
| 가지 많은 그래프의 실행 순서 무시 | peak SRAM이 예상의 2배 | 나쁜 scheduling (11절) | depth-first 순서, 도구 옵션 확인 |

---

## 15. 면접에서 이렇게 말한다

**Q.** "What does operator fusion buy you?"

**A.** 중간 텐서를 없애는 게 핵심이다. conv 뒤의 BN·ReLU는 원소당 연산이 한두 개라 memory-bound인데, 따로 돌리면 중간 텐서를 한 번 쓰고 한 번 읽는다. fusion하면 그 트래픽과 버퍼가 사라지고, 커널 launch/NPU 명령도 줄고, 양자화에서는 requantize 단계도 줄어든다. BN은 weight/bias에 접혀서 연산 자체가 0이 되고, ReLU는 epilogue에서 공짜로 처리된다. 작은 CNN에서 세어 보면 conv-BN-ReLU 한 블록당 출력 텐서의 4배 트래픽이 사라진다.

> "Fusion removes intermediate tensors. Ops like BatchNorm and ReLU do one or two operations per element, so unfused they are pure memory traffic — you write the conv output, read it back, write again. Folding BN into the conv weights makes it free, and applying ReLU in the conv's epilogue means the intermediate never leaves registers. You save bandwidth, SRAM for the intermediate buffers, kernel-launch overhead, and in int8 an extra requantization step. On a small conv-BN-ReLU block I measured the eliminated traffic at four times the output tensor size."

**Q.** "Why do NPUs need static shapes?"

**A.** NPU 컴파일러는 실행 전에 거의 모든 걸 결정한다: 텐서 크기로 SRAM 배치와 arena 크기를 정하고, 크기에 맞춰 tiling을 계산하고, 주소·길이·stride가 박힌 DMA 명령 스트림을 굽는다. shape을 모르면 이게 하나도 안 된다. 그래서 edge에서는 batch 1, 고정 입력 길이로 export하고, 스트리밍은 고정 청크 + state로 설계하고, 가변 길이는 몇 개 bucket으로 따로 컴파일한다.

> "Because the compiler does all the planning ahead of time. Memory placement, tiling to fit on-chip SRAM, and the DMA descriptors in the command stream all depend on concrete tensor sizes. With a symbolic dimension it can't size the arena or pre-compute tiles, and shape-computation ops often end up unsupported and fall back to the CPU. So I export with fixed shapes — batch one, fixed window length — design streaming models as fixed chunks plus explicit state, and if I truly need variable length, compile a few bucketed sizes."

**Q.** "An op isn't supported by the NPU — what do you do?"

**A.** 먼저 리포트에서 어떤 op가 왜(op 자체, 파라미터 조합, dtype, opset 형태, dynamic shape) 떨어졌는지, 그리고 그래프의 어디인지 본다. 끝부분 후처리면 CPU에 둬도 된다. 백본 중간이면 경계 비용이 두 번 들어서 치명적이니, 등가 패턴으로 그래프 수술(예: HardSwish → Add/Clip/Mul)을 하고 수치 등가성을 검증한다. 그게 안 되면 파라미터를 지원 범위로 바꾸거나 지원되는 op로 재학습하고, 최후에 벤더에 최소 재현 모델과 함께 요청한다.

> "First I find out exactly why it fell back — the op itself, a parameter combination, the data type, or a dynamic shape — and where it sits in the graph. A post-processing op at the end can stay on the CPU; one in the middle of the backbone splits the NPU graph and you pay the handoff twice, which can erase the whole speedup. My first fix is a graph rewrite into an equivalent supported pattern, for example hard-swish into add, clip and multiply, verified numerically against the original. If that's not possible I change the layer to a supported configuration or retrain with a supported op, and I file it with the vendor with a minimal reproducer."

**Q.** "How does a runtime decide tensor arena size?"

**A.** 실행 순서에서 각 텐서의 lifetime(생성 step ~ 마지막 사용 step)을 구하고, lifetime이 겹치는 텐서끼리 주소가 안 겹치게 offset을 배정한다. TFLite Micro의 greedy planner는 큰 텐서부터 들어갈 수 있는 가장 낮은 offset에 놓는다. arena는 가장 높은 끝 주소이고, 하한은 가장 붐비는 step의 live 텐서 합이다. MobileNetV2 블록 예에서 naive 405 KB가 252 KB로 줄었다. 실제로는 `AllocateTensors()` 후 `arena_used_bytes()`로 재고 여유를 더한다. 실행 순서도 peak를 바꾼다 — 가지가 있는 그래프에서 2배 차이를 본 적이 있다.

> "It's register allocation with variable-size registers. From the execution order you get each tensor's lifetime, then you assign offsets so that tensors with overlapping lifetimes don't overlap in address. TFLite Micro's greedy planner places the largest tensors first at the lowest offset that fits. The arena is the highest end address, and the lower bound is the sum of live tensors at the busiest step. In practice I allocate generously, call AllocateTensors, read arena_used_bytes, and add margin. Execution order matters too — on a branchy graph, reordering alone halved peak memory in my experiment."

**Q.** "NCHW vs NHWC transposes — why do they appear and how do you remove them?"

**A.** PyTorch는 NCHW, TFLite와 대부분의 NPU/DSP는 채널이 연속인 NHWC를 쓴다. 채널 축으로 내적을 하니 SIMD 로드가 쉽기 때문이다. 변환기가 op마다 경계에 transpose를 넣으면 계산 0에 메모리 복사만 하는 노드가 쌓인다. 지우는 법은 두 규칙: 역변환 쌍은 상쇄하고, 원소별 op 너머로는 transpose를 밀어서 짝을 만나게 한다. 그러면 입출력 경계에 하나씩만 남고, 입력을 NHWC로 주면 그것도 사라진다. 조심할 곳은 flatten → FC로, flatten 순서가 layout마다 달라 weight를 재배열해야 한다.

> "PyTorch is NCHW; TFLite and most NPUs and DSPs prefer NHWC because the conv reduction runs over channels, so channels-contiguous means a single SIMD load. A naive converter inserts transposes around every op, and each one is pure data movement. The optimizer pushes transposes through layout-agnostic elementwise ops and cancels inverse pairs, so you end up with at most one at the input and one at the output — zero if the runtime takes NHWC input. The classic trap is flatten followed by a fully connected layer: the flatten order differs between layouts, so the FC weights must be permuted or you get plausible-looking but wrong outputs."

**Q.** "Walk me through what a model compiler does to a graph."

**A.** export된 그래프를 받아 cleanup(constant folding, dead code·Identity 제거), fusion(Conv+BN+ReLU, MatMul+Add → Gemm), layout 변환과 transpose 제거, 타깃 op 집합으로의 lowering·패턴 교체, 지원 여부에 따른 partition(NPU/CPU), 그다음 tiling·scheduling·memory planning을 하고 명령 스트림을 만든다. C 컴파일러의 IR 패스 → instruction selection → register allocation → scheduling과 같은 구조다. 내가 확인하는 것은 결과 그래프의 노드 수·종류, CPU op 목록, SRAM 사용량, 그리고 원래 모델과의 수치 비교다.

> "It's the same structure as a C compiler. Cleanup passes — constant folding, dead-code and identity removal. Then fusion, like conv-BN-ReLU into one kernel and MatMul-plus-Add into Gemm. Layout conversion to NHWC with transpose elimination. Lowering to the target's op set, replacing unsupported patterns. Partitioning between NPU and CPU. Then tiling, scheduling and memory planning, and finally emitting the command stream. After every compile I check node counts, the list of CPU-fallback ops, SRAM usage, and a numerical comparison against the original model."

---

## 16. 직접 해보기

1. **손계산 — constant folding.** 입력 `x: [1, 3, 32, 32]`에 대해 `Reshape(x, Concat(Gather(Shape(x), 0), Mul(Gather(Shape(x), 1), 1024)))`를 접으면 `Reshape`의 목표 shape는? 결과 텐서 shape는?
정답: Gather(Shape, 0) = 1, Gather(Shape, 1) = 3 → 3·1024 = 3072 → 목표 `[1, 3072]`, 결과 `[1, 3072]` (3·32·32 = 3072와 맞는다).

2. **손계산 — fusion 트래픽.** int8, conv 출력 `[1, 32, 24, 24]`에 BN과 ReLU6이 따로 붙어 있다. fusion으로 사라지는 중간 텐서 바이트와 메모리 트래픽은?
정답: 텐서 하나 32·24·24 = 18,432 B. 중간 텐서 2개(conv 출력, BN 출력) = 36,864 B, 트래픽(각각 write+read) = 73,728 B.

3. **손계산 — 채널 패딩.** 채널 40짜리 텐서 `[1, 12, 12, 40]` int8를 32 정렬 NPU에 두면 저장 바이트와 낭비율은?
정답: 64채널로 패딩 → 12·12·64 = 9,216 B (원래 5,760 B), 낭비 24/64 = 37.5 %.

4. **코드 — memory planner 확장.** 예제 16에서 `add`를 in-place(출력이 `a5` 자리를 재사용)로 처리하도록 `graph`/`size`를 바꾸는 방법을 생각하고, peak가 줄어드는지 확인하라. 힌트: in-place면 a6이 새 텐서가 아니라 a5의 연장이다 — `add`의 출력을 a5로 두고 a5의 lifetime을 step 7까지 늘리는 것과 같다.
정답: step 6의 live 집합이 a2 + a5 + a6(110,592 B)에서 a2 + a5(73,728 B)로 준다. 하지만 peak는 step 4(252 KB)라 **arena는 그대로**다. peak step이 아닌 곳을 줄여 봐야 소용없다 — 최적화는 peak가 생기는 곳을 겨냥해야 한다.

5. **코드 — 그래프 수술.** 예제 13을 바꿔 `Sigmoid`를 지원하지 않는 NPU를 가정하고, SiLU(`x·σ(x)`)를 hard-swish로 바꾼 버전의 오차를 재라. 이건 등가 교체인가?
정답/힌트: 등가가 아니다 — hard-swish는 SiLU의 근사다(실측 최대 오차 약 0.14, x = −3 부근). 이런 교체는 **재학습(또는 fine-tune)**이 필요한 모델 변경이지 그래프 최적화가 아니다. 등가 교체와 근사 교체를 구분하는 게 이 문제의 요점이다.

6. **코드 — ORT 리포트 흉내.** 예제 9의 `opt_True_ENABLE_ALL.onnx`와 `tiny_bias0.onnx`를 읽어 "op 종류별 개수, initializer 총 바이트, 가장 큰 중간 텐서"를 표로 찍는 스크립트를 짜라. 힌트: `numpy_helper.to_array(t).nbytes`, `shape_inference.infer_shapes`의 `value_info`.
정답: 결과 해석의 핵심 — BN이 접힌 쪽은 initializer에 BN 파라미터 4종이 없고, 가장 큰 중간 텐서는 두 파일 모두 `[1,16,16,16]` fp32 = 16,384 B다.

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| dataflow graph | 데이터 흐름 그래프 | 노드 = op, 엣지 = tensor인 DAG. 모델의 IR |
| FX graph | PyTorch 그래프 IR | `torch.export`가 만드는 ATen op 그래프 (`ExportedProgram.graph`) |
| Core ATen opset | PyTorch 핵심 op 집합 | `run_decompositions()`로 쪼갠 뒤의 작은 op 집합, 백엔드 구현 대상 |
| initializer | ONNX 상수 텐서 | 가중치 등 실행 전에 값이 정해진 텐서 |
| opset | op 버전 집합 | 같은 op 이름도 opset마다 입력/속성 형태가 다를 수 있다 |
| shape inference | shape 추론 | 입력 shape에서 모든 중간 텐서 shape를 계산해 채우는 것 |
| constant folding | 상수 접기 | 입력과 무관한 계산을 컴파일 타임에 해서 상수로 바꿈 |
| op fusion | 연산 융합 | 여러 op를 커널 하나로 합쳐 중간 텐서를 없앰 |
| epilogue | 후처리 단계 | 누산이 끝난 값을 저장하기 직전에 적용하는 bias·activation·requantize |
| BN folding | BN 접기 | eval BN을 앞 conv의 W, b에 합침 (B1) |
| DCE | dead code elimination | 출력에 기여하지 않는 노드 제거 |
| lowering / decomposition | 하강·분해 | 큰 op를 타깃이 아는 작은 op들로 쪼갬 |
| layout | 메모리 배치 순서 | NCHW, NHWC, 블록 형식(NCHWc, NHCWB16 등) |
| transpose elimination | transpose 제거 | 원소별 op 너머로 밀고 역변환 쌍을 상쇄 |
| channel alignment | 채널 정렬 | 채널 수를 HW 단위(8/16/32)의 배수로 패딩 |
| static / dynamic shape | 고정/가변 shape | 컴파일 타임에 크기가 숫자로 정해졌는지 여부 |
| tensor lifetime | 텐서 수명 | 생성 step부터 마지막 사용 step까지 |
| tensor arena | 텐서 작업 공간 | 모든 중간 텐서를 담는 정적 메모리 블록 (TFLM) |
| memory planner | 메모리 계획기 | lifetime을 보고 arena 안 offset을 배정하는 패스 |
| scheduling | 실행 순서 결정 | 위상 정렬 중 하나를 고르는 것, peak memory·병렬성에 영향 |
| partition | 그래프 분할 | 지원 여부에 따라 NPU 구간과 CPU 구간으로 나눔 |
| CPU fallback | CPU 대체 실행 | 가속기가 모르는 op를 CPU에서 실행, 경계 비용 발생 |
| execution provider (EP) | ORT 백엔드 | CPU, CoreML, QNN 등 ORT가 그래프 일부를 맡기는 대상 |
| contrib op | ORT 전용 op | `com.microsoft` 도메인의 FusedConv, FusedGemm 등 |

---

## 18. 요약 & 체크리스트

모델은 op 노드와 tensor 엣지로 된 dataflow graph이고, 배포는 이 그래프를 기기에 맞게 고쳐 쓰는 컴파일 과정이다. constant folding은 입력과 무관한 shape 산술·상수 변환을 미리 계산하고, fusion은 Conv+BN+ReLU나 MatMul+Add 같은 묶음을 커널 하나로 합쳐 중간 텐서의 메모리 트래픽과 버퍼를 없앤다. DCE는 출력에 닿지 않는 노드를, layout 패스는 NCHW↔NHWC transpose를 밀어내고 상쇄해 지운다. NPU는 채널 정렬 단위와 지원 op 집합, static shape을 요구하므로 lowering·그래프 수술·shape 고정이 필요하다. 마지막으로 memory planner는 tensor lifetime으로 arena를 재사용하며, 실행 순서만 바꿔도 peak memory가 두 배까지 달라진다. 실측에서 본 교훈은 "도구가 해 줄 거라 가정하지 말고, 최적화된 그래프를 열어 노드를 세고 수치를 비교하라"다 — export 잔재 하나가 BN folding과 FusedConv를 연쇄적으로 막았고, ORT는 dead branch를 지우지 않았다.

- [ ] `torch.export`와 ONNX 그래프를 스크립트로 열어 노드·initializer·shape를 나열할 수 있다
- [ ] 주어진 shape 산술 서브그래프를 손으로 constant folding 할 수 있다
- [ ] Conv+BN+ReLU fusion이 없애는 중간 텐서 바이트와 트래픽을 계산할 수 있다
- [ ] ORT 최적화 레벨 네 개의 차이를 말하고, `optimized_model_filepath`로 결과를 확인할 수 있다
- [ ] export 잔재가 fusion을 막는 이유를 설명하고 cleanup 순서를 제시할 수 있다
- [ ] transpose 밀어내기·상쇄 규칙과 flatten→FC 함정을 설명할 수 있다
- [ ] 채널 정렬 패딩 낭비율을 계산할 수 있다
- [ ] GELU·hard-swish·LayerNorm의 분해식을 쓰고 정확/근사 교체를 구분할 수 있다
- [ ] tensor lifetime으로 peak memory 하한과 greedy arena를 계산할 수 있다
- [ ] CPU fallback의 비용 구조를 설명하고 컴파일러 리포트에서 확인할 항목을 나열할 수 있다

## 참고 자료

- ONNX Runtime 문서 — Graph Optimizations: https://onnxruntime.ai/docs/performance/model-optimizations/graph-optimizations.html
- ONNX 공식 문서 (Operators, Python API, shape inference): https://onnx.ai/onnx/
- PyTorch `torch.export` 문서: https://pytorch.org/docs/stable/export.html
- PyTorch ONNX exporter (dynamo) 문서: https://pytorch.org/docs/stable/onnx_dynamo.html
- TensorFlow Lite for Microcontrollers (tflite-micro) 저장소 — memory planner, `MicroInterpreter`: https://github.com/tensorflow/tflite-micro
- Arm Ethos-U Vela 컴파일러 (문서·지원 op 목록은 저장소의 문서 참조): https://pypi.org/project/ethos-u-vela/
- Qualcomm AI Engine Direct (QNN) SDK 문서 — Qualcomm 개발자 사이트 (버전별 converter/backend 문서)
- Netron — 모델 그래프 뷰어: https://netron.app
- MIT 6.5940 TinyML and Efficient Deep Learning (Song Han) — 그래프 최적화, MCUNet/MCUNetV2 memory scheduling
- Sze, Chen, Yang, Emer, "Efficient Processing of Deep Neural Networks" — dataflow, 메모리 계층과 에너지
- Lin et al., "MCUNetV2: Memory-Efficient Patch-based Inference for Tiny Deep Learning" (NeurIPS 2021) — patch 기반 실행으로 peak memory 줄이기
