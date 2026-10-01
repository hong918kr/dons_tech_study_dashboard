# A5. PyTorch 기본 — 텐서부터 학습 루프, 펌웨어로 내보내기까지

> **이 노트를 다 읽으면**: 텐서의 shape·dtype·stride를 C 포인터 산술로 설명할 수 있다 · `nn.Module`로 IMU 1D-CNN을 만들고 학습 루프를 처음부터 쓸 수 있다 · 학습한 가중치를 C 헤더로 뽑아 펌웨어 C 코드와 PyTorch 출력이 1e-5 안에서 일치함을 검증할 수 있다 · `torch.export`/ONNX가 배포 경로에서 어디에 놓이는지 말할 수 있다
> **JD 연결**: "5 yrs ML engineering" 요건을 프로젝트로 증명하는 출발점, "Integrate ML inference into embedded firmware in C, C++" · study_prep_list A5 — tensor, autograd, `nn.Module`, `DataLoader`, 학습 루프, checkpoint 저장, `torch.export` / ONNX export
> **Don 기준 난이도**: 메모리 레이아웃·stride·C 추론 코드는 이미 강함 / PyTorch 관용구(autograd, Module, 학습 루프, train/eval 모드)는 새로 배움
> **선행 노트**: A0, A1, A2, A3, A4

---

## 0. 큰 그림 — 이게 왜 필요한가

edge ML 엔지니어가 받는 모델은 거의 항상 **PyTorch**로 만들어져 있다. 모델팀(연구자·ML 엔지니어)은 PyTorch로 학습하고, 펌웨어·배포 쪽은 그 결과물(`.pt` 체크포인트, ONNX 파일, `torch.export` 그래프)을 받아 MCU·DSP·NPU로 옮긴다. 그래서 Don이 PyTorch를 "쓸 줄" 알아야 하는 이유는 세 가지다.

- 모델팀이 넘겨준 코드를 읽고, shape·dtype·레이어 구성을 스스로 확인하기 위해
- 작은 모델(IMU 제스처, 착용 감지 등)은 직접 학습시켜서 end-to-end 데모를 만들기 위해
- 배포 후 결과가 이상할 때 **PyTorch 출력(golden reference)과 펌웨어 출력을 비교**하기 위해 — 이게 Don의 검증 경험이 바로 먹히는 자리다

펌웨어 비유로 말하면 PyTorch는 "모델용 시뮬레이터 + 자동 튜너"다. 알고리즘을 호스트에서 float로 빠르게 돌려 보고(시뮬레이터), 파라미터를 데이터에 맞게 자동으로 맞추고(튜너), 그 결과를 타깃 코드로 넘긴다.

```svg
<svg viewBox="0 0 680 255" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="a5m0" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <rect x="10" y="30" width="150" height="50" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="85" y="52" font-size="13" text-anchor="middle">센서 로그</text> <text x="85" y="70" font-size="12" text-anchor="middle">numpy [N,6,50]</text>
<rect x="180" y="30" width="150" height="50" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="255" y="52" font-size="13" text-anchor="middle">Dataset</text> <text x="255" y="70" font-size="12" text-anchor="middle">DataLoader (배치)</text> <rect x="350" y="30" width="150" height="50" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="425" y="52" font-size="13" text-anchor="middle">nn.Module</text> <text x="425" y="70" font-size="12" text-anchor="middle">+ 학습 루프</text> <rect x="520" y="30" width="150" height="50" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="595" y="52" font-size="13" text-anchor="middle">state_dict</text> <text x="595" y="70" font-size="12" text-anchor="middle">model.pt (가중치)</text>
<line x1="160" y1="55" x2="178" y2="55" stroke="currentColor" stroke-width="1.5" marker-end="url(#a5m0)"/> <line x1="330" y1="55" x2="348" y2="55" stroke="currentColor" stroke-width="1.5" marker-end="url(#a5m0)"/> <line x1="500" y1="55" x2="518" y2="55" stroke="currentColor" stroke-width="1.5" marker-end="url(#a5m0)"/> <rect x="180" y="130" width="220" height="50" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<text x="290" y="152" font-size="13" text-anchor="middle">경로 A: C 헤더 + 직접 쓴 C</text> <text x="290" y="170" font-size="12" text-anchor="middle">작은 MLP (이 노트 8절)</text> <rect x="430" y="130" width="240" height="50" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="550" y="152" font-size="13" text-anchor="middle">경로 B: torch.export / ONNX</text>
<text x="550" y="170" font-size="12" text-anchor="middle">→ TFLite · ExecuTorch · 벤더 컴파일러</text> <line x1="580" y1="80" x2="330" y2="128" stroke="currentColor" stroke-width="1.5" marker-end="url(#a5m0)"/> <line x1="600" y1="80" x2="560" y2="128" stroke="currentColor" stroke-width="1.5" marker-end="url(#a5m0)"/> <rect x="180" y="205" width="490" height="40" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/>
<text x="425" y="230" font-size="13" text-anchor="middle">MCU · DSP · NPU 펌웨어 (출력은 PyTorch golden과 비교)</text> <line x1="290" y1="180" x2="290" y2="203" stroke="currentColor" stroke-width="1.5" marker-end="url(#a5m0)"/> <line x1="550" y1="180" x2="550" y2="203" stroke="currentColor" stroke-width="1.5" marker-end="url(#a5m0)"/> <text x="10" y="150" font-size="12">호스트(맥북)</text>
<text x="10" y="230" font-size="12">타깃 기기</text>
</svg>
```

그림 1 — 이 노트가 따라가는 경로. 파란 박스는 데이터, 주황 박스는 PyTorch 학습, 초록 박스는 내보내기, 빨간 박스는 타깃. 이 노트에서는 합성 IMU 데이터로 전 구간을 실제로 돌린다.

이 노트의 예제는 한 줄기 이야기다. 예를 들어 Hark 같은 손목·귀 착용 기기라면(추정), IMU 6축(가속도 3 + 자이로 3)을 50 Hz로 1초씩 잘라 "가만히(still) / 흔들기(shake) / 원 그리기(circle)" 제스처를 구분하는 모델을 생각할 수 있다. 이 데이터를 numpy로 합성하고, PyTorch로 1D-CNN을 학습하고, 작은 MLP를 C로 옮겨 출력이 같은지 확인한다.

> 실행 환경: 모든 Python 예제는 이 폴더의 `.venv/bin/python` (Python 3.9, torch 2.8.0, numpy 2.0.2)로 실제 실행한 출력이다. 뒤 절의 예제는 `imu_data.py`, `models.py` 두 파일을 같은 디렉터리에 저장해 두고 import 한다.

---

## 1. PyTorch란 — numpy + GPU + 자동 미분

### 1.1 직관

A1에서 numpy로 행렬곱을 했다. PyTorch의 `torch.Tensor`는 numpy `ndarray`와 거의 같은 물건이다. 차이는 딱 세 가지다.

| 기능 | numpy `ndarray` | PyTorch `Tensor` |
|---|---|---|
| 다차원 배열, broadcasting, 인덱싱 | O | O (문법 거의 같음) |
| GPU/가속기에서 실행 | X | O (`device="cuda"`, 맥은 `"mps"`) |
| 자동 미분 (gradient 계산) | X | O (`requires_grad=True`, autograd) |
| 신경망 부품 (레이어, loss, optimizer) | X | O (`torch.nn`, `torch.optim`) |

말로 하면, PyTorch = "미분이 되는 numpy + 신경망 부품 상자"다. 업계 모델팀의 사실상 표준(default)이라서, 논문 코드·오픈 모델(Llama, Whisper 등)·벤더 예제의 대부분이 PyTorch로 나온다.

### 1.2 텐서 만들기, dtype, device

- **dtype**: 원소 하나의 자료형. C의 `float`, `int8_t`와 같다.
- **device**: 텐서의 메모리가 어디 있는지 (`cpu`, `cuda`, `mps`). 연산은 같은 device의 텐서끼리만 된다.
- **shape**: 각 차원의 크기. C의 `float x[2][3]`에서 `[2][3]`.

무엇을 확인하는 코드인지: numpy와 메모리를 공유하는 경우/복사하는 경우, dtype별 바이트 수와 정밀도 차이.

```python
import numpy as np, torch
torch.manual_seed(0)
a = np.array([[1., 2., 3.], [4., 5., 6.]], dtype=np.float32)
t = torch.from_numpy(a)          # 메모리 공유
t2 = torch.tensor(a)             # 복사
a[0, 0] = 100.0
print("from_numpy :", t[0, 0].item(), "| tensor(copy):", t2[0, 0].item())
x = torch.zeros(2, 3); y = torch.ones(2, 3, dtype=torch.int8)
r = torch.randn(2, 3)
print(x.dtype, y.dtype, r.dtype, "| shape", tuple(r.shape), "| device", r.device)
for dt in [torch.float32, torch.float16, torch.bfloat16, torch.int8]:
    e = torch.empty((), dtype=dt).element_size()
    print(f"{str(dt):15s} {e} byte/elem")
v = torch.tensor(0.1, dtype=torch.float32)
print("0.1 as f32 :", f"{v.item():.10f}")
print("0.1 as f16 :", f"{v.half().item():.10f}")
print("0.1 as bf16:", f"{v.bfloat16().item():.10f}")
print("300 -> int8:", torch.tensor([300]).to(torch.int8).item())
print("mps?", torch.backends.mps.is_available(), "cuda?", torch.cuda.is_available())
```

```text
from_numpy : 100.0 | tensor(copy): 1.0
torch.float32 torch.int8 torch.float32 | shape (2, 3) | device cpu
torch.float32   4 byte/elem
torch.float16   2 byte/elem
torch.bfloat16  2 byte/elem
torch.int8      1 byte/elem
0.1 as f32 : 0.1000000015
0.1 as f16 : 0.0999755859
0.1 as bf16: 0.1000976562
300 -> int8: 44
mps? True cuda? False
```

출력에서 볼 것: `from_numpy`는 같은 버퍼를 가리키는 포인터라서 numpy 쪽 수정이 그대로 보인다(`100.0`). float16은 mantissa 10비트, bfloat16은 mantissa 7비트라 0.1의 오차가 bf16에서 더 크다. `300 → int8`이 `44`가 된 것은 **saturation이 아니라 wraparound**(300 − 256 = 44)다. C에서 `(int8_t)300`과 같은 동작이고, 양자화(C1)에서 반드시 `clamp`를 먼저 해야 하는 이유다.

### 1.3 dtype 정리 — 임베디드 관점

| dtype | 바이트 | 구성 (부호·지수·가수) | 어디서 쓰나 |
|---|---|---|---|
| `float32` | 4 | 1·8·23 | 학습 기본값, MCU FPU(Cortex-M4F/M7) |
| `float16` | 2 | 1·5·10 | GPU/NPU 추론, 범위 좁음(최대 65504) |
| `bfloat16` | 2 | 1·8·7 | 학습 가속, float32와 같은 범위·낮은 정밀도 |
| `int8` | 1 | 정수 −128~127 | 양자화 추론 (CMSIS-NN, NPU) — C1에서 |
| `int64` (`long`) | 8 | 정수 | 분류 label, 인덱스 (`CrossEntropyLoss`가 요구) |

> Don 연결: dtype은 레지스터 폭 선택과 같다. 학습은 float32로, 배포는 int8로 — 이 사이를 잇는 게 모듈 C(양자화)다.

---

## 2. 텐서의 메모리 — shape, stride, view, permute

이 절은 Don이 가장 빨리 이해할 부분이다. PyTorch 텐서는 **"연속된 1차원 버퍼 + 해석 규칙(shape, stride, offset)"**이다. C 다차원 배열과 똑같다.

### 2.1 stride = C 포인터 산술

C에서 `float x[2][3]`의 `x[i][j]` 주소는 `base + (i×3 + j)×sizeof(float)`이다. 여기서 `3`과 `1`이 **stride**다. 즉 stride = "그 차원의 인덱스를 1 올릴 때 버퍼에서 몇 원소 건너뛰나".

```
x[i][j]의 원소 오프셋 = i × stride[0] + j × stride[1]
shape (2,3), row-major → stride (3, 1)
x[1][2] → 1×3 + 2×1 = 5 번째 원소
```

말로 하면: shape는 "모양", stride는 "모양을 메모리에 펴는 방법"이다. 둘을 분리해 두면 **데이터를 복사하지 않고** 모양만 바꿔 보일 수 있다.

### 2.2 view vs reshape vs contiguous

- `view(shape)`: 메모리를 **절대 복사하지 않고** 새 shape로 해석. 현재 stride로 표현이 불가능하면 에러.
- `reshape(shape)`: 가능하면 view, 불가능하면 **조용히 복사**.
- `contiguous()`: stride가 row-major가 아니면 새 버퍼에 row-major로 복사. 이미 contiguous면 그대로.
- `t()` / `transpose` / `permute`: **stride만 바꾼다**. 메모리는 그대로.

무엇을 확인하는 코드인지: transpose가 메모리를 안 건드린다는 것(`data_ptr` 동일)과, 그 때문에 `view`가 실패하는 상황.

```python
import torch
x = torch.arange(6, dtype=torch.float32).reshape(2, 3)
print(x)
print("shape", tuple(x.shape), "stride", x.stride(), "contig", x.is_contiguous())
# 원소 [i][j]의 주소 = base + (i*stride0 + j*stride1) * 4 bytes
i, j = 1, 2
print("x[1,2] =", x[i, j].item(), "offset(elem) =", i * x.stride(0) + j * x.stride(1))
xt = x.t()                       # transpose: 메모리 그대로, stride만 교환
print("xt shape", tuple(xt.shape), "stride", xt.stride(), "contig", xt.is_contiguous())
print("same memory?", xt.data_ptr() == x.data_ptr())
try:
    xt.view(6)
except RuntimeError as e:
    print("view error:", str(e)[:60], "...")
r = xt.reshape(6)                # 필요하면 복사
print("reshape  ->", r.tolist(), "same memory?", r.data_ptr() == x.data_ptr())
c = xt.contiguous()
print("contig   ->", c.view(6).tolist(), "stride", c.stride(), "same memory?", c.data_ptr() == x.data_ptr())
v = x.view(3, 2)                 # contiguous라 view OK, 메모리 공유
v[0, 0] = 99
print("after v[0,0]=99, x[0,0] =", x[0, 0].item())
```

```text
tensor([[0., 1., 2.],
        [3., 4., 5.]])
shape (2, 3) stride (3, 1) contig True
x[1,2] = 5.0 offset(elem) = 5
xt shape (3, 2) stride (1, 3) contig False
same memory? True
view error: view size is not compatible with input tensor's size and str ...
reshape  -> [0.0, 3.0, 1.0, 4.0, 2.0, 5.0] same memory? False
contig   -> [0.0, 3.0, 1.0, 4.0, 2.0, 5.0] stride (2, 1) same memory? False
after v[0,0]=99, x[0,0] = 99.0
```

출력에서 볼 것: `xt`의 stride가 `(3,1)` → `(1,3)`으로 바뀌었을 뿐 `data_ptr`은 같다. `xt`를 1차원으로 펴려면 순서가 `0,3,1,4,2,5`여야 하는데 원래 버퍼는 `0,1,2,3,4,5`라서 복사 없이는 불가능 → `view` 에러, `reshape`/`contiguous`는 새 버퍼를 만든다(`same memory? False`). 마지막 줄은 `view`가 메모리를 공유하므로 한쪽 수정이 다른 쪽에 보인다는 것.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="45" font-size="13">원본 버퍼</text> <text x="20" y="62" font-size="12">(data_ptr 하나)</text> <g font-size="13" text-anchor="middle"> <rect x="170" y="35" width="46" height="32" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="193" y="56">0</text> <rect x="220" y="35" width="46" height="32" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="243" y="56">1</text>
<rect x="270" y="35" width="46" height="32" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="293" y="56">2</text> <rect x="320" y="35" width="46" height="32" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="343" y="56">3</text> <rect x="370" y="35" width="46" height="32" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="393" y="56">4</text>
<rect x="420" y="35" width="46" height="32" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="443" y="56">5</text> </g> <text x="170" y="25" font-size="12">offset 0 → 5 (float 4 B씩)</text> <g font-size="13" text-anchor="middle"> <rect x="60" y="120" width="36" height="36" fill="none" stroke="currentColor"/><text x="78" y="143">0</text>
<rect x="96" y="120" width="36" height="36" fill="none" stroke="currentColor"/><text x="114" y="143">1</text> <rect x="132" y="120" width="36" height="36" fill="none" stroke="currentColor"/><text x="150" y="143">2</text> <rect x="60" y="156" width="36" height="36" fill="none" stroke="currentColor"/><text x="78" y="179">3</text>
<rect x="96" y="156" width="36" height="36" fill="none" stroke="currentColor"/><text x="114" y="179">4</text> <rect x="132" y="156" width="36" height="36" fill="none" stroke="currentColor"/><text x="150" y="179">5</text> <rect x="250" y="110" width="36" height="36" fill="none" stroke="currentColor"/><text x="268" y="133">0</text>
<rect x="286" y="110" width="36" height="36" fill="none" stroke="currentColor"/><text x="304" y="133">3</text> <rect x="250" y="146" width="36" height="36" fill="none" stroke="currentColor"/><text x="268" y="169">1</text> <rect x="286" y="146" width="36" height="36" fill="none" stroke="currentColor"/><text x="304" y="169">4</text>
<rect x="250" y="182" width="36" height="36" fill="none" stroke="currentColor"/><text x="268" y="205">2</text> <rect x="286" y="182" width="36" height="36" fill="none" stroke="currentColor"/><text x="304" y="205">5</text> </g> <line x1="114" y1="118" x2="300" y2="70" stroke="#4a7bd0" stroke-width="1.5" stroke-dasharray="5,4"/>
<line x1="286" y1="108" x2="310" y2="70" stroke="#4a7bd0" stroke-width="1.5" stroke-dasharray="5,4"/> <text x="40" y="215" font-size="12">x: shape (2,3)</text> <text x="40" y="232" font-size="12">stride (3,1) contiguous</text> <text x="230" y="240" font-size="12">x.t(): shape (3,2)</text> <text x="230" y="257" font-size="12">stride (1,3) — 복사 없음</text>
<text x="400" y="120" font-size="12">x.t().contiguous() → 새 버퍼</text> <g font-size="13" text-anchor="middle"> <rect x="400" y="135" width="40" height="32" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="420" y="156">0</text> <rect x="440" y="135" width="40" height="32" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="460" y="156">3</text>
<rect x="480" y="135" width="40" height="32" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="500" y="156">1</text> <rect x="520" y="135" width="40" height="32" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="540" y="156">4</text> <rect x="560" y="135" width="40" height="32" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="580" y="156">2</text>
<rect x="600" y="135" width="40" height="32" fill="none" stroke="#e08a3c" stroke-width="2"/><text x="620" y="156">5</text> </g> <text x="400" y="190" font-size="12">stride (2,1), data_ptr 다름</text> <text x="400" y="207" font-size="12">= memcpy + 재배치 비용 발생</text> <text x="20" y="288" font-size="12">점선: 두 텐서가 같은 버퍼를 가리킴 (xt.data_ptr() == x.data_ptr())</text>
</svg>
```

그림 2 — transpose는 stride만 바꾼 "다른 시각"이고, contiguous()를 불러야 실제로 메모리가 재배치된다. C로 치면 `x[j][i]`로 읽는 매크로를 만든 것과, 실제로 transpose 루프를 돌려 새 배열에 쓴 것의 차이다.

### 2.3 permute로 NCHW ↔ NHWC, 그리고 IMU [N, L, C] ↔ [N, C, L]

레이어는 입력 **layout**(차원 순서)을 정해 두고 기대한다. PyTorch의 `Conv2d`는 NCHW(배치·채널·높이·너비), `Conv1d`는 NCL(배치·채널·길이)을 기대한다. 반면 TFLite·CMSIS-NN·많은 NPU는 NHWC(채널이 가장 안쪽)를 선호한다(C6에서 자세히). 센서 로그는 보통 "시간마다 6축 한 줄"로 쌓이므로 `[N, L, C]`이다.

무엇을 확인하는 코드인지: `permute`는 stride만 바꾸고, `contiguous()` 후에야 메모리 순서가 NHWC가 된다는 것.

```python
import torch
# NCHW: 배치 1, 채널 2 (R,G 같은), 높이 2, 너비 2
x = torch.arange(8).reshape(1, 2, 2, 2)
print("NCHW stride", x.stride(), "memory:", x.flatten().tolist())
nhwc_view = x.permute(0, 2, 3, 1)          # N,H,W,C 로 '보이게'만
print("permute shape", tuple(nhwc_view.shape), "stride", nhwc_view.stride(),
      "contig", nhwc_view.is_contiguous(), "same ptr", nhwc_view.data_ptr() == x.data_ptr())
nhwc = nhwc_view.contiguous()              # 실제 메모리 재배치
print("NHWC stride", nhwc.stride(), "memory:", nhwc.flatten().tolist())
# IMU: [N, L, C] (시간 우선 로그) -> Conv1d가 원하는 [N, C, L]
log = torch.arange(12).reshape(1, 4, 3)   # 4 샘플 × 3축
ncl = log.permute(0, 2, 1)
print("IMU [N,L,C] stride", log.stride(), "-> [N,C,L] stride", ncl.stride())
```

```text
NCHW stride (8, 4, 2, 1) memory: [0, 1, 2, 3, 4, 5, 6, 7]
permute shape (1, 2, 2, 2) stride (8, 2, 1, 4) contig False same ptr True
NHWC stride (8, 4, 2, 1) memory: [0, 4, 1, 5, 2, 6, 3, 7]
IMU [N,L,C] stride (12, 3, 1) -> [N,C,L] stride (12, 1, 3)
```

출력에서 볼 것: NHWC로 바꾼 뒤의 메모리 `[0, 4, 1, 5, ...]`는 "채널 0 픽셀, 채널 1 픽셀"이 번갈아(interleave) 놓인 모양이다. C 펌웨어에서 `uint8_t rgb[H][W][3]` 이미지 버퍼가 바로 NHWC다. `permute` 자체는 공짜지만, 다음 연산이 contiguous를 요구하면 그 자리에서 **숨은 복사**가 생긴다.

### 2.4 IMU 윈도우 텐서 모양 — [N, C, L]

이 노트 내내 쓰는 입력 모양이다.

```svg
<svg viewBox="0 0 660 270" xmlns="http://www.w3.org/2000/svg">
<rect x="92" y="48" width="300" height="120" fill="none" stroke="#888" stroke-width="1.5"/> <rect x="76" y="64" width="300" height="120" fill="none" stroke="#888" stroke-width="1.5"/> <rect x="60" y="80" width="300" height="120" fill="none" stroke="#4a7bd0" stroke-width="2"/> <g stroke="#4a7bd0" stroke-width="0.8"> <line x1="60" y1="100" x2="360" y2="100"/><line x1="60" y1="120" x2="360" y2="120"/>
<line x1="60" y1="140" x2="360" y2="140"/><line x1="60" y1="160" x2="360" y2="160"/> <line x1="60" y1="180" x2="360" y2="180"/> </g> <g font-size="12" text-anchor="end"> <text x="54" y="95">ax</text><text x="54" y="115">ay</text><text x="54" y="135">az</text> <text x="54" y="155">gx</text><text x="54" y="175">gy</text><text x="54" y="195">gz</text> </g>
<rect x="120" y="80" width="30" height="120" fill="none" stroke="#e08a3c" stroke-width="2.5"/> <text x="135" y="222" font-size="12" text-anchor="middle">kernel 5</text> <line x1="60" y1="240" x2="360" y2="240" stroke="currentColor" stroke-width="1.5"/> <text x="210" y="258" font-size="13" text-anchor="middle">L = 50 샘플 (1 s @ 50 Hz) — 시간축</text>
<line x1="40" y1="200" x2="40" y2="80" stroke="currentColor" stroke-width="1.5"/> <text x="14" y="145" font-size="13">C=6</text> <line x1="365" y1="75" x2="395" y2="45" stroke="currentColor" stroke-width="1.5"/> <text x="400" y="45" font-size="13">N = 배치 (창 여러 개)</text> <text x="420" y="100" font-size="13">x.shape = [N, 6, 50]</text> <text x="420" y="122" font-size="13">x.stride() = (300, 50, 1)</text>
<text x="420" y="150" font-size="12">x[n][c][t] 오프셋</text> <text x="420" y="168" font-size="12">= n·300 + c·50 + t</text> <text x="420" y="196" font-size="12">주황 상자: Conv1d 커널이</text> <text x="420" y="214" font-size="12">6채널 × 5샘플을 한 번에 보고</text> <text x="420" y="232" font-size="12">시간축으로 미끄러진다</text>
</svg>
```

그림 3 — IMU 입력 텐서. 한 창(window)은 `float win[6][50]`와 같은 1200 B 블록이고, 배치는 그런 블록을 N개 이어 붙인 것이다.

---

## 3. Autograd — 미분을 자동으로

### 3.1 직관

A3에서 역전파를 손으로 했다. PyTorch는 forward를 실행하면서 **계산 그래프**(어떤 텐서가 어떤 연산으로 만들어졌는지)를 기록하고, `backward()`를 부르면 chain rule로 거꾸로 따라가며 각 파라미터의 gradient를 채운다. 펌웨어로 비유하면 trace 버퍼에 연산 로그를 남겨 두고, 끝에서 역순으로 replay하는 것이다.

- `requires_grad=True`: "이 텐서에 대한 gradient가 필요하다"(학습할 파라미터 표시)
- `loss.backward()`: 그래프를 역방향으로 돌며 `.grad`에 ∂loss/∂파라미터를 **더한다(누적)**
- `torch.no_grad()`: 그 블록 안에서는 그래프 기록을 끈다 (추론·평가 시 메모리 절약)
- `detach()`: 그래프에서 떼어 낸 텐서를 돌려준다 (값은 같고, gradient는 흐르지 않음)

### 3.2 손계산

예측 `ŷ = w·x + b`, loss `L = (ŷ − y)²`, 값은 `x=2, w=3, b=1, y=5`.

```
ŷ = 3·2 + 1 = 7
L = (7 − 5)² = 4
∂L/∂ŷ = 2(ŷ − y) = 4
∂L/∂w = ∂L/∂ŷ · ∂ŷ/∂w = 4 · x = 8
∂L/∂b = ∂L/∂ŷ · ∂ŷ/∂b = 4 · 1 = 4
SGD 한 스텝 (lr = 0.1):  w ← 3 − 0.1·8 = 2.2
```

말로 하면: w를 조금 올리면 loss가 기울기 8의 비율로 커지니, 반대 방향으로 내린다.

### 3.3 코드로 확인

무엇을 확인하는 코드인지: autograd가 손계산(8, 4)과 같은 값을 내는지, 그리고 grad가 누적된다는 함정.

```python
import torch
x, y = torch.tensor(2.0), torch.tensor(5.0)
w = torch.tensor(3.0, requires_grad=True)
b = torch.tensor(1.0, requires_grad=True)
pred = w * x + b                 # 7
L = (pred - y) ** 2              # (7-5)^2 = 4
print("pred", pred.item(), "L", L.item(), "| L.grad_fn:", type(L.grad_fn).__name__)
L.backward()
print("dL/dw", w.grad.item(), "dL/db", b.grad.item())      # 손계산: 8, 4
L2 = (w * x + b - y) ** 2
L2.backward()                    # zero_grad 안 하면 누적된다
print("2nd backward w/o zero -> dL/dw", w.grad.item())
w.grad.zero_(); b.grad.zero_()
with torch.no_grad():
    p = w * x + b
print("no_grad: requires_grad =", p.requires_grad, "grad_fn =", p.grad_fn)
d = (w * x).detach()
print("detach : requires_grad =", d.requires_grad)
with torch.no_grad():
    w -= 0.1 * 8.0               # 수동 SGD 한 스텝: w ← w − lr·grad
print("w after manual step", round(w.item(), 4))
```

```text
pred 7.0 L 4.0 | L.grad_fn: PowBackward0
dL/dw 8.0 dL/db 4.0
2nd backward w/o zero -> dL/dw 16.0
no_grad: requires_grad = False grad_fn = None
detach : requires_grad = False
w after manual step 2.2
```

출력에서 볼 것: `dL/dw 8.0 dL/db 4.0`은 손계산과 정확히 같다. 두 번째 `backward()` 후 grad가 `16.0`이 된 것이 **누적** 동작이다 — 레지스터를 clear하지 않고 계속 accumulate하는 MAC과 같다. 그래서 학습 루프 맨 앞에 `zero_grad()`가 있다. 파라미터를 직접 갱신할 때는 `no_grad()` 안에서 해야 그 갱신 자체가 그래프에 기록되지 않는다.

> 임베디드 연결: 추론 펌웨어에는 autograd가 없다. 배포 모델에는 gradient도 그래프 기록도 필요 없다 — 그래서 PyTorch에서도 추론할 때 `torch.no_grad()`(또는 더 강한 `torch.inference_mode()`)를 쓰고, export 도구는 forward만 뽑아낸다.

---

## 4. nn.Module — 모델을 클래스로 정의하기

### 4.1 직관

`nn.Module`은 "파라미터를 가진 함수"를 담는 클래스다. 펌웨어로 치면 **드라이버 구조체 + 처리 함수**다. `__init__`에서 레이어(= 가중치 버퍼를 가진 하위 모듈)를 만들고, `forward`에 데이터 흐름을 쓴다. `model(x)`라고 부르면 내부적으로 `forward(x)`가 실행된다.

- `nn.Linear(in, out)`: `y = x·Wᵀ + b`, W의 shape는 `[out, in]` (C로 `W[out][in]`)
- `nn.Conv1d(C_in, C_out, k)`: 시간축으로 커널을 미끄러뜨리는 합성곱, 가중치 shape `[C_out, C_in, k]`
- `nn.MaxPool1d(2)`: 2개씩 묶어 최대값 → 길이 절반
- `nn.AdaptiveAvgPool1d(1)`: 시간축 전체 평균 → 길이 1 (global average pooling)
- `nn.Dropout(p)`: 학습 중에만 무작위로 p 비율을 0으로 (5.4절)

### 4.2 모델 정의 — `models.py`

아래를 `models.py`로 저장한다. 1D-CNN은 원시 IMU 창을 받고, TinyMLP는 창에서 뽑은 12개 특징(축별 평균·표준편차)을 받는다. TinyMLP는 8절에서 C로 옮길 모델이다.

```python
import torch
from torch import nn

class IMUNet1D(nn.Module):
    def __init__(self, n_ch=6, n_cls=3):
        super().__init__()
        self.conv1 = nn.Conv1d(n_ch, 16, kernel_size=5, padding=2)   # [N,6,50]→[N,16,50]
        self.pool = nn.MaxPool1d(2)                                   # →[N,16,25]
        self.conv2 = nn.Conv1d(16, 32, kernel_size=5, padding=2)     # →[N,32,25]
        self.gap = nn.AdaptiveAvgPool1d(1)                            # →[N,32,1]
        self.drop = nn.Dropout(0.2)
        self.fc = nn.Linear(32, n_cls)                                # →[N,3] logits
    def forward(self, x):
        x = self.pool(torch.relu(self.conv1(x)))
        x = torch.relu(self.conv2(x))
        x = self.gap(x).flatten(1)                                    # [N,32]
        return self.fc(self.drop(x))

class TinyMLP(nn.Module):
    def __init__(self, n_in=12, n_hidden=16, n_cls=3):
        super().__init__()
        self.fc1 = nn.Linear(n_in, n_hidden)
        self.fc2 = nn.Linear(n_hidden, n_cls)
    def forward(self, x):
        return self.fc2(torch.relu(self.fc1(x)))
```

### 4.3 shape와 파라미터 수 손계산

Conv1d 출력 길이 공식 (stride s, padding p, kernel k):

```
L_out = (L_in + 2p − k) / s + 1
conv1: (50 + 4 − 5)/1 + 1 = 50   → [N,16,50]
pool : 50 / 2 = 25               → [N,16,25]
conv2: (25 + 4 − 5)/1 + 1 = 25   → [N,32,25]
gap  : 평균 → [N,32,1] → flatten → [N,32]
fc   : [N,32] → [N,3]
```

파라미터 수 = 가중치 + bias:

```
conv1: 16·6·5 + 16   = 480 + 16  = 496
conv2: 32·16·5 + 32  = 2560 + 32 = 2592
fc   : 3·32 + 3      = 96 + 3    = 99
합계                              = 3187
TinyMLP: (12·16 + 16) + (16·3 + 3) = 208 + 51 = 259
```

연산량(MAC)도 D1 예고편으로 계산해 두면: conv1 = 16·50·(6·5) = 24,000, conv2 = 32·25·(16·5) = 64,000, fc = 96 → 약 **88k MAC/추론**. Cortex-M4F에서 float MAC 하나를 거칠게 1~3 사이클로 잡으면 100 MHz에서 약 1~3 ms 크기다(실측은 K 모듈에서).

### 4.4 코드로 확인 — 레이어별 파라미터 표, state_dict

무엇을 확인하는 코드인지: 출력 shape와 손계산한 파라미터 수가 맞는지, `state_dict`에 무엇이 들어 있는지.

```python
import torch
from models import IMUNet1D, TinyMLP
torch.manual_seed(0)
m = IMUNet1D()
x = torch.randn(4, 6, 50)                         # 배치 4개 창
print("out shape", tuple(m(x).shape))
print(f"{'name':14s} {'shape':14s} {'numel':>6s}")
for name, p in m.named_parameters():
    print(f"{name:14s} {str(tuple(p.shape)):14s} {p.numel():6d}")
total = sum(p.numel() for p in m.parameters())
print("total params", total, "| TinyMLP", sum(p.numel() for p in TinyMLP().parameters()))
sd = m.state_dict()
print("state_dict keys:", list(sd.keys())[:4], "...", len(sd), "tensors")
```

```text
out shape (4, 3)
name           shape           numel
conv1.weight   (16, 6, 5)        480
conv1.bias     (16,)              16
conv2.weight   (32, 16, 5)      2560
conv2.bias     (32,)              32
fc.weight      (3, 32)            96
fc.bias        (3,)                3
total params 3187 | TinyMLP 259
state_dict keys: ['conv1.weight', 'conv1.bias', 'conv2.weight', 'conv2.bias'] ... 6 tensors
```

출력에서 볼 것: 손계산 3187, 259와 일치한다. `state_dict()`는 **"이름 → 텐서" 딕셔너리**이고, 저장·로드·C 헤더 생성의 입력이 된다. 파라미터가 아닌 상태(BatchNorm의 `running_mean` 같은 buffer)도 `state_dict`에 들어간다 — 이 모델엔 BN이 없어 6개뿐이다.

| 개념 | PyTorch | 펌웨어 대응 |
|---|---|---|
| 모델 구조 | `nn.Module` 클래스의 코드 | 처리 함수 코드 (`.c`) |
| 학습된 값 | `state_dict()` | calibration 테이블 / `const` 배열 (`.h`, flash) |
| 저장 | `torch.save(model.state_dict(), "m.pt")` | NVM에 테이블 쓰기 |
| 로드 | `model.load_state_dict(torch.load("m.pt"))` | 부팅 시 테이블 로드 |

> 핵심: `.pt`(state_dict)에는 **코드가 없다**. 구조를 정의하는 클래스가 있어야 다시 불러올 수 있다. 그래서 배포용으로는 구조와 가중치를 한 파일에 담는 export(9절)가 따로 있다.

---

## 5. Loss, Optimizer, 학습 루프, train/eval 모드

### 5.1 Loss — `nn.CrossEntropyLoss`는 logits를 받는다

A2에서 본 cross-entropy를 PyTorch에서는 `nn.CrossEntropyLoss()`로 쓴다. 중요한 규칙:

- 입력은 softmax 전의 **logits** `[N, 클래스 수]` (float)
- 정답은 클래스 **인덱스** `[N]` (int64 = `long`)
- 내부에서 `log_softmax + NLL`을 수치적으로 안정하게 한 번에 계산한다

말로 하면: 모델 마지막에 softmax를 붙이지 않는다. softmax는 추론 결과를 확률로 보고 싶을 때만 따로 쓴다. 손계산: logits `[4, 0, 0]`, 정답 0이면 `L = −log(e⁴/(e⁴+2)) = log(1 + 2e⁻⁴) ≈ 0.0360` (11절 예제에서 확인).

### 5.2 Optimizer

`torch.optim.SGD(model.parameters(), lr=0.01)` 또는 `torch.optim.Adam(model.parameters(), lr=1e-3)`. optimizer는 파라미터 목록(포인터 목록)을 쥐고 있다가 `step()` 때 각 `.grad`를 읽어 A3의 갱신 규칙을 적용한다. Adam은 파라미터마다 이동 평균 2개를 더 저장하므로 **학습 시 메모리는 파라미터의 약 4배**(파라미터 + grad + Adam 상태 2개)가 든다 — 추론 기기에는 없는 비용이다.

### 5.3 학습 루프 — 다섯 줄이 전부다

```svg
<svg viewBox="0 0 660 290" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="a5m1" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <rect x="245" y="18" width="150" height="44" rx="8" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="320" y="37" font-size="13" text-anchor="middle">1. opt.zero_grad()</text>
<text x="320" y="54" font-size="12" text-anchor="middle">grad = 0 (누적 방지)</text> <rect x="435" y="94" width="150" height="44" rx="8" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="510" y="113" font-size="13" text-anchor="middle">2. model(x)</text> <text x="510" y="130" font-size="12" text-anchor="middle">forward → logits</text>
<rect x="362" y="217" width="150" height="44" rx="8" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="437" y="236" font-size="13" text-anchor="middle">3. loss_fn(logits, y)</text> <text x="437" y="253" font-size="12" text-anchor="middle">스칼라 loss</text> <rect x="127" y="217" width="150" height="44" rx="8" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="202" y="236" font-size="13" text-anchor="middle">4. loss.backward()</text> <text x="202" y="253" font-size="12" text-anchor="middle">∂L/∂w → p.grad</text> <rect x="55" y="94" width="150" height="44" rx="8" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="130" y="113" font-size="13" text-anchor="middle">5. opt.step()</text>
<text x="130" y="130" font-size="12" text-anchor="middle">w ← w − lr·grad</text> <line x1="395" y1="48" x2="478" y2="92" stroke="currentColor" stroke-width="1.5" marker-end="url(#a5m1)"/> <line x1="500" y1="138" x2="456" y2="215" stroke="currentColor" stroke-width="1.5" marker-end="url(#a5m1)"/> <line x1="362" y1="239" x2="279" y2="239" stroke="currentColor" stroke-width="1.5" marker-end="url(#a5m1)"/>
<line x1="180" y1="217" x2="142" y2="140" stroke="currentColor" stroke-width="1.5" marker-end="url(#a5m1)"/> <line x1="170" y1="94" x2="243" y2="50" stroke="currentColor" stroke-width="1.5" marker-end="url(#a5m1)"/> <text x="320" y="140" font-size="13" text-anchor="middle">배치 1개마다 1바퀴 = 1 step</text> <text x="320" y="160" font-size="12" text-anchor="middle">DataLoader가 (x, y) 배치를 공급</text>
<text x="320" y="178" font-size="12" text-anchor="middle">전체 데이터 1회 = 1 epoch</text>
</svg>
```

그림 4 — PyTorch 학습 루프의 한 바퀴. 순서가 틀리면(특히 1번 누락) 조용히 잘못 학습된다.

펌웨어 비유: 제어 루프다. 센서 읽기(배치) → 모델 출력 → 오차 측정(loss) → 오차의 민감도 계산(backward) → gain(lr)만큼 보정(step). 매 주기 시작에 적분기가 아닌 "이번 주기 기울기" 레지스터를 clear하는 게 `zero_grad()`다.

### 5.4 `model.train()` vs `model.eval()` — 두 레이어만 달라진다

`train()`/`eval()`은 모듈 트리 전체에 "지금 학습 중인가" 플래그를 세운다. 이 플래그를 보는 대표 레이어는 두 개다.

| 레이어 | train 모드 | eval 모드 |
|---|---|---|
| `Dropout(p)` | 원소를 확률 p로 0, 나머지는 `1/(1−p)`배 | 아무것도 안 함 (항등) |
| `BatchNorm` | **현재 배치**의 평균·분산으로 정규화, running 통계 갱신 | 저장된 **running** 평균·분산 사용, 갱신 안 함 |

무엇을 확인하는 코드인지: 같은 입력이 모드에 따라 다른 출력을 낸다는 것.

```python
import torch
from torch import nn
torch.manual_seed(0)
drop = nn.Dropout(p=0.5)
x = torch.ones(8)
drop.train(); print("Dropout train:", drop(x).tolist())
drop.eval();  print("Dropout eval :", drop(x).tolist())
bn = nn.BatchNorm1d(1)                      # 채널 1개
batch = torch.tensor([[1.0], [3.0], [5.0], [7.0]])   # 평균 4, 분산 5
bn.train(); out = bn(batch)
print("BN train out :", [round(v, 3) for v in out.flatten().tolist()])
print("running_mean :", round(bn.running_mean.item(), 3), "running_var:", round(bn.running_var.item(), 3))
bn.eval()
print("BN eval out  :", [round(v, 3) for v in bn(batch).flatten().tolist()])
one = torch.tensor([[5.0]])
print("BN eval batch=1:", round(bn(one).item(), 3))
```

```text
Dropout train: [0.0, 0.0, 2.0, 0.0, 0.0, 0.0, 2.0, 2.0]
Dropout eval : [1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0]
BN train out : [-1.342, -0.447, 0.447, 1.342]
running_mean : 0.4 running_var: 1.567
BN eval out  : [0.479, 2.077, 3.675, 5.273]
BN eval batch=1: 3.675
```

출력에서 볼 것 (손계산 포함):

- Dropout train: 살아남은 값이 `1/(1−0.5) = 2.0`배. 평균 기댓값을 맞추기 위한 스케일이다.
- BN train: 배치 평균 4, 분산(모집단) = (9+1+1+9)/4 = 5, `(1−4)/√5 = −1.342`.
- running 통계: momentum 0.1로 `0.9·0 + 0.1·4 = 0.4`, 분산은 표본분산 20/3 ≈ 6.667로 `0.9·1 + 0.1·6.667 = 1.567`.
- BN eval: `(1 − 0.4)/√1.567 = 0.479`. 학습을 한 스텝만 해서 running 통계가 아직 덜 수렴한 상태라 train 출력과 전혀 다르다.
- eval 모드에서는 배치 크기 1이어도 결과가 결정적이다(5.0 → 3.675). 기기 추론은 거의 항상 batch 1이므로 **eval 모드가 기기의 동작**이다.

> 임베디드 연결: 배포 모델은 항상 eval 상태로 export한다. BN은 eval에서 `y = a·x + c` 꼴의 고정 affine이 되므로 앞의 conv 가중치에 합쳐 버릴 수 있다(BN folding, B1·C6). Dropout은 그냥 사라진다.

---

## 6. Dataset과 DataLoader — 데이터를 배치로 공급하기

### 6.1 역할 분담

- `Dataset`: "i번째 샘플을 달라"에 답하는 객체. `__len__`과 `__getitem__(i)` 두 메서드만 있으면 된다. 펌웨어로 치면 **인덱스로 접근하는 로그 저장소**.
- `DataLoader`: Dataset에서 샘플을 뽑아 **배치로 묶고(stack), 섞고(shuffle), 필요하면 병렬로 읽는** 반복자. DMA 디스크립터 체인이 버퍼를 묶어 주는 것과 비슷하다.

`shuffle=True`는 epoch마다 순서를 섞는다. 섞지 않으면 한 배치가 전부 같은 클래스가 되어 gradient가 한쪽으로 치우친다. 검증(val) 로더는 섞을 필요가 없다.

### 6.2 합성 IMU 데이터 — `imu_data.py`

실제 센서 대신 물리적으로 그럴듯한 신호를 numpy로 만든다. still은 중력(z축 1 g) + 노이즈, shake는 6~10 Hz 사인파를 x가속도·y자이로에, circle은 1~2 Hz의 cos/sin을 x·y가속도에 + z자이로 오프셋. 진폭·주파수·위상을 창마다 무작위로 하고 노이즈(σ=0.35)를 크게 넣어 너무 쉽지 않게 했다. 아래를 `imu_data.py`로 저장한다.

```python
import numpy as np, torch
from torch.utils.data import Dataset

CLASSES = ["still", "shake", "circle"]
FS, L = 50, 50                   # 50 Hz, 1초 창 → 50 샘플

def make_window(label, rng):
    t = np.arange(L) / FS
    x = np.zeros((6, L), dtype=np.float32)          # [C=6, L=50]: ax ay az gx gy gz
    x[2] = 1.0                                      # 중력 1 g (z축)
    if label == 1:                                  # shake: 6~10 Hz 빠른 흔들기
        f, A = rng.uniform(6, 10), rng.uniform(0.3, 1.5)
        s = A * np.sin(2 * np.pi * f * t + rng.uniform(0, 2 * np.pi))
        x[0] += s; x[4] += 3 * s
    elif label == 2:                                # circle: 1~2 Hz 원 그리기
        f, A = rng.uniform(1, 2), rng.uniform(0.2, 0.8)
        ph = rng.uniform(0, 2 * np.pi)
        x[0] += A * np.cos(2 * np.pi * f * t + ph)
        x[1] += A * np.sin(2 * np.pi * f * t + ph)
        x[5] += 0.4 * f                              # 천천히 도는 z축 각속도
    x += rng.normal(0, 0.35, size=x.shape).astype(np.float32)   # 센서 노이즈
    return x

class IMUWindows(Dataset):
    def __init__(self, n_per_class, seed):
        rng = np.random.default_rng(seed)
        ys = np.repeat(np.arange(3), n_per_class)
        self.x = torch.from_numpy(np.stack([make_window(y, rng) for y in ys]))
        self.y = torch.from_numpy(ys).long()
    def __len__(self):
        return len(self.y)
    def __getitem__(self, i):
        return self.x[i], self.y[i]
```

주의: `np.zeros(..., dtype=np.float32)`로 만든 것과 노이즈에 `.astype(np.float32)`를 붙인 것 — numpy 기본 dtype은 float64라서, 빼먹으면 11절의 dtype 불일치 에러가 난다. label은 `.long()`(int64)이어야 `CrossEntropyLoss`가 받는다.

### 6.3 코드로 확인 — 샘플 모양, 클래스별 통계, 배치

무엇을 확인하는 코드인지: Dataset이 `[6, 50]` float32 샘플과 label을 주는지, DataLoader가 섞어서 `[B, 6, 50]` 배치를 만드는지.

```python
import torch
from torch.utils.data import DataLoader
from imu_data import IMUWindows, CLASSES
torch.manual_seed(0)
ds = IMUWindows(n_per_class=4, seed=1)            # 12개 창
x0, y0 = ds[0]
print("len", len(ds), "| one sample", tuple(x0.shape), x0.dtype, "label", y0.item(), CLASSES[y0])
for cls in range(3):
    xs = ds.x[ds.y == cls]
    print(f"{CLASSES[cls]:6s} ax std={xs[:, 0].std():.2f}  gy std={xs[:, 4].std():.2f}  gz mean={xs[:, 5].mean():.2f}")
dl = DataLoader(ds, batch_size=5, shuffle=True)
for k, (xb, yb) in enumerate(dl):
    print(f"batch {k}: x {tuple(xb.shape)} y {yb.tolist()}")
```

```text
len 12 | one sample (6, 50) torch.float32 label 0 still
still  ax std=0.33  gy std=0.33  gz mean=-0.01
shake  ax std=0.92  gy std=2.48  gz mean=-0.00
circle ax std=0.44  gy std=0.33  gz mean=0.62
batch 0: x (5, 6, 50) y [1, 2, 2, 2, 0]
batch 1: x (5, 6, 50) y [0, 1, 1, 0, 2]
batch 2: x (2, 6, 50) y [1, 0]
```

출력에서 볼 것: shake는 자이로 y의 표준편차가 크고, circle은 z자이로 평균이 0에서 벗어나 있다 — 모델이 배울 "특징"이 데이터에 실제로 있다. 12개를 5개씩 묶으니 마지막 배치는 2개다(`drop_last=True`를 주면 버린다). label 순서가 섞여 나온다.

---

## 7. End-to-end — 학습, 평가, 저장, 로드, 추론 시간

### 7.1 전체 학습 스크립트

학습 900창(클래스당 300, seed 1), 검증 300창(클래스당 100, seed 2), Adam lr 1e-3, 배치 32, 8 epoch.

무엇을 확인하는 코드인지: 5절의 다섯 줄 루프로 1D-CNN이 실제로 학습되는지, epoch마다 train/val이 어떻게 변하는지.

```python
import time, torch
from torch import nn
from torch.utils.data import DataLoader
from imu_data import IMUWindows
from models import IMUNet1D
torch.manual_seed(0)
train_dl = DataLoader(IMUWindows(300, seed=1), batch_size=32, shuffle=True)
val_dl = DataLoader(IMUWindows(100, seed=2), batch_size=128)
model = IMUNet1D()
loss_fn = nn.CrossEntropyLoss()                        # logits를 받는다
opt = torch.optim.Adam(model.parameters(), lr=1e-3)

def evaluate(dl):
    model.eval(); correct = n = 0; tot = 0.0
    with torch.no_grad():
        for xb, yb in dl:
            logits = model(xb)
            tot += loss_fn(logits, yb).item() * len(yb)
            correct += (logits.argmax(1) == yb).sum().item(); n += len(yb)
    return tot / n, correct / n

t0 = time.perf_counter()
for epoch in range(1, 9):
    model.train(); tot = correct = n = 0
    for xb, yb in train_dl:
        opt.zero_grad()                  # 1. 이전 grad 비우기
        logits = model(xb)               # 2. forward
        loss = loss_fn(logits, yb)       # 3. loss
        loss.backward()                  # 4. backward (grad 계산)
        opt.step()                       # 5. 파라미터 갱신
        tot += loss.item() * len(yb); n += len(yb)
        correct += (logits.argmax(1) == yb).sum().item()
    vl, va = evaluate(val_dl)
    print(f"epoch {epoch}  train loss {tot/n:.3f} acc {correct/n:.3f} | val loss {vl:.3f} acc {va:.3f}")
print(f"train time {time.perf_counter() - t0:.1f} s")
torch.save(model.state_dict(), "imunet1d.pt")
```

```text
epoch 1  train loss 1.013 acc 0.331 | val loss 0.877 acc 0.333
epoch 2  train loss 0.737 acc 0.809 | val loss 0.528 acc 1.000
epoch 3  train loss 0.358 acc 0.987 | val loss 0.177 acc 1.000
epoch 4  train loss 0.128 acc 0.984 | val loss 0.073 acc 1.000
epoch 5  train loss 0.064 acc 0.988 | val loss 0.035 acc 1.000
epoch 6  train loss 0.035 acc 0.999 | val loss 0.019 acc 1.000
epoch 7  train loss 0.024 acc 0.997 | val loss 0.017 acc 1.000
epoch 8  train loss 0.018 acc 0.999 | val loss 0.010 acc 1.000
train time 0.8 s
```

출력에서 볼 것:

- epoch 1의 정확도 0.33은 3클래스 무작위 추측 수준이다. 초기 loss 1.013은 `ln 3 ≈ 1.099`에 가깝다 — "아무것도 모르는 모델"의 cross-entropy (A2).
- val loss/acc가 train보다 좋아 보이는 것은 버그가 아니다. train 수치는 **dropout이 켜진 상태**에서, **epoch 도중 계속 바뀌는 모델**로 잰 평균이고, val은 epoch 끝의 모델을 eval 모드로 잰 것이다.
- val 100%는 합성 데이터가 쉬워서다. 같은 생성기에서 나온 데이터라 실제 사용자 간 차이(착용 위치, 사람마다 다른 동작)가 없다. 실제 제품이라면 A4의 **사용자 단위 split**을 해야 이런 숫자가 현실적으로 떨어진다.
- CPU에서 0.8초. 모델이 작으면 GPU가 필요 없다.

```svg
<svg viewBox="0 0 660 290" xmlns="http://www.w3.org/2000/svg">
<g stroke="currentColor" stroke-width="1"> <line x1="50" y1="230" x2="300" y2="230"/><line x1="50" y1="30" x2="50" y2="230"/> <line x1="370" y1="230" x2="620" y2="230"/><line x1="370" y1="30" x2="370" y2="230"/> </g> <g stroke="#888" stroke-width="0.5" stroke-dasharray="3,3"> <line x1="50" y1="163.3" x2="300" y2="163.3"/><line x1="50" y1="96.7" x2="300" y2="96.7"/><line x1="50" y1="30" x2="300" y2="30"/>
<line x1="370" y1="180" x2="620" y2="180"/><line x1="370" y1="130" x2="620" y2="130"/><line x1="370" y1="80" x2="620" y2="80"/><line x1="370" y1="30" x2="620" y2="30"/> </g> <g font-size="12" text-anchor="end"> <text x="45" y="234">0</text><text x="45" y="167">0.4</text><text x="45" y="100">0.8</text><text x="45" y="34">1.2</text>
<text x="365" y="234">0</text><text x="365" y="184">0.25</text><text x="365" y="134">0.5</text><text x="365" y="84">0.75</text><text x="365" y="34">1.0</text> </g> <g font-size="12" text-anchor="middle"> <text x="50" y="247">1</text><text x="121.4" y="247">3</text><text x="192.9" y="247">5</text><text x="264.3" y="247">7</text><text x="300" y="247">8</text>
<text x="370" y="247">1</text><text x="441.4" y="247">3</text><text x="512.9" y="247">5</text><text x="584.3" y="247">7</text><text x="620" y="247">8</text> <text x="175" y="266">epoch</text><text x="495" y="266">epoch</text> <text x="175" y="20">loss (cross-entropy)</text><text x="495" y="20">accuracy</text> </g>
<polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="50.0,61.2 85.7,107.2 121.4,170.3 157.1,208.7 192.9,219.3 228.6,224.2 264.3,226.0 300.0,227.0"/> <polyline fill="none" stroke="#e08a3c" stroke-width="2" stroke-dasharray="6,3" points="50.0,83.8 85.7,142.0 121.4,200.5 157.1,217.8 192.9,224.2 228.6,226.8 264.3,227.2 300.0,228.3"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="370.0,163.8 405.7,68.2 441.4,32.6 477.1,33.2 512.9,32.4 548.6,30.2 584.3,30.6 620.0,30.2"/> <polyline fill="none" stroke="#e08a3c" stroke-width="2" stroke-dasharray="6,3" points="370.0,163.4 405.7,30.0 441.4,30.0 477.1,30.0 512.9,30.0 548.6,30.0 584.3,30.0 620.0,30.0"/>
<g fill="#4a7bd0"><circle cx="50" cy="61.2" r="2.5"/><circle cx="85.7" cy="107.2" r="2.5"/><circle cx="121.4" cy="170.3" r="2.5"/><circle cx="370" cy="163.8" r="2.5"/><circle cx="405.7" cy="68.2" r="2.5"/></g> <line x1="190" y1="60" x2="215" y2="60" stroke="#4a7bd0" stroke-width="2"/><text x="220" y="64" font-size="12">train</text>
<line x1="190" y1="80" x2="215" y2="80" stroke="#e08a3c" stroke-width="2" stroke-dasharray="6,3"/><text x="220" y="84" font-size="12">val</text> <text x="400" y="210" font-size="12">epoch 1 ≈ 0.33 (무작위 추측)</text>
</svg>
```

그림 5 — 위 실행의 실제 epoch별 loss와 accuracy(파랑 train, 주황 점선 val). epoch 3에서 거의 수렴한다. 실제 센서 데이터라면 val 곡선이 train 아래에서 멈추거나 다시 올라가는(overfitting, A4) 모습이 보통이다.

### 7.2 체크포인트 로드, 추론, 지연시간, 메모리

`torch.save(model.state_dict(), path)`로 저장한 것은 **같은 클래스의 새 객체**를 만든 뒤 `load_state_dict`로 채운다. 학습을 이어서 할 거라면 optimizer의 `state_dict()`와 epoch 번호도 함께 저장하는 게 관례다(`{"model": ..., "opt": ..., "epoch": ...}` 딕셔너리를 `torch.save`).

무엇을 확인하는 코드인지: 저장한 가중치를 다시 불러와 새 데이터에서 맞히는지, batch 1 CPU 지연시간과 파라미터 메모리.

```python
import time, torch
from imu_data import IMUWindows, CLASSES
from models import IMUNet1D
torch.manual_seed(0)
model = IMUNet1D()
model.load_state_dict(torch.load("imunet1d.pt", map_location="cpu"))
model.eval()
ds = IMUWindows(2, seed=3)
with torch.inference_mode():
    probs = torch.softmax(model(ds.x), dim=1)
for p, y in zip(probs, ds.y):
    print(f"true {CLASSES[y]:6s} pred {CLASSES[p.argmax()]:6s} p={[round(v, 3) for v in p.tolist()]}")
x1 = ds.x[:1]                                   # batch 1: [1, 6, 50]
torch.set_num_threads(1)
with torch.inference_mode():
    for _ in range(50): model(x1)               # warm-up
    ts = []
    for _ in range(1000):
        t0 = time.perf_counter(); model(x1); ts.append(time.perf_counter() - t0)
ts.sort()
print(f"batch-1 latency: median {ts[500]*1e6:.0f} us, p99 {ts[990]*1e6:.0f} us")
n = sum(p.numel() for p in model.parameters())
print(f"params {n} -> float32 {n*4} B ({n*4/1024:.1f} KiB), int8 {n} B ({n/1024:.1f} KiB)")
```

```text
true still  pred still  p=[0.955, 0.008, 0.037]
true still  pred still  p=[0.99, 0.004, 0.006]
true shake  pred shake  p=[0.008, 0.992, 0.0]
true shake  pred shake  p=[0.03, 0.97, 0.001]
true circle pred circle p=[0.0, 0.0, 1.0]
true circle pred circle p=[0.002, 0.0, 0.998]
batch-1 latency: median 49 us, p99 58 us
params 3187 -> float32 12748 B (12.4 KiB), int8 3187 B (3.1 KiB)
```

출력에서 볼 것:

- 학습에 안 쓴 seed 3 데이터도 모두 맞힌다. softmax는 **보여 줄 때만** 붙였다.
- 맥북 CPU(단일 스레드)에서 batch 1 중앙값 49 µs. 지연시간은 한 번만 재지 말고 warm-up 후 분포(중앙값, p99)를 본다 — 펌웨어 타이밍 측정과 같은 원칙(D6). 숫자 자체는 실행할 때마다 몇 µs씩 흔들리고, 88k MAC짜리 모델에선 대부분이 Python/프레임워크 오버헤드다.
- 파라미터 메모리: float32 12,748 B, int8이면 3,187 B(scale 몇 개 추가 제외)로 4배 준다 — C1의 예고편. activation(중간 텐서) 메모리는 따로다: conv1 출력 `16×50×4 B = 3.2 KB`가 이 모델의 가장 큰 activation이다(D2).

---

## 8. 펌웨어로 내보내기 ① — 가중치를 C 헤더로, 추론은 C로

이 절이 Don에게 핵심 다리다. 런타임(TFLite Micro 등) 없이 **가장 원시적인 배포**를 직접 해 본다. 작은 MLP는 실제로 이렇게 배포하는 경우도 많다(always-on MCU의 착용 감지, 간단한 제스처).

### 8.1 특징 + MLP 모델 학습

1D-CNN 대신, 창에서 축별 평균·표준편차 12개를 뽑고 TinyMLP(12→16→3)로 분류한다. 특징 추출은 C로 옮기기 쉽고, 고전적인 MCU 방식(B7)이다.

무엇을 확인하는 코드인지: 특징 12개만으로도 분류가 되는지, 그리고 C로 옮길 가중치를 저장.

```python
import torch
from torch import nn
from imu_data import IMUWindows
from models import TinyMLP
torch.manual_seed(0)
def features(x):                                  # [N,6,50] → [N,12]: 축별 평균·표준편차
    return torch.cat([x.mean(dim=2), x.std(dim=2, correction=0)], dim=1)
tr, va = IMUWindows(300, seed=1), IMUWindows(100, seed=2)
Xtr, Xva = features(tr.x), features(va.x)
mlp = TinyMLP()
opt = torch.optim.Adam(mlp.parameters(), lr=1e-2)
loss_fn = nn.CrossEntropyLoss()
for step in range(300):                           # 전체 배치(900개) 학습
    opt.zero_grad(); loss = loss_fn(mlp(Xtr), tr.y); loss.backward(); opt.step()
mlp.eval()
with torch.no_grad():
    acc = (mlp(Xva).argmax(1) == va.y).float().mean().item()
print(f"final train loss {loss.item():.4f} | val acc {acc:.3f}")
torch.save(mlp.state_dict(), "tinymlp.pt")
```

```text
final train loss 0.0078 | val acc 1.000
```

출력에서 볼 것: 데이터가 900개뿐이라 DataLoader 없이 전체를 한 배치로 300 step 돌렸다(full-batch). `correction=0`은 N으로 나누는 모집단 표준편차 — C 쪽도 똑같이 `/50`으로 계산해야 한다. **전처리까지 bit 단위로 같은 정의**여야 출력이 맞는다.

### 8.2 state_dict → C 헤더 생성기

`nn.Linear`의 `weight`는 `[out, in]`이고 row-major contiguous다. 그러니 C의 `float W[out][in]`에 그대로 쏟으면 된다. 숫자는 `%.9g`로 찍는다 — float32는 유효숫자 9자리면 10진 문자열 → float 변환이 원래 비트로 **정확히 왕복**한다. 다차원 배열은 중첩 중괄호로 써야 `-Wall`에서 `-Wmissing-braces` 경고가 안 난다(처음에 평평한 초기화로 썼다가 clang이 경고를 냈다).

무엇을 확인하는 코드인지: 가중치·테스트 입력·PyTorch 기준 출력(golden)을 하나의 헤더로 뽑기.

```python
import torch
from imu_data import IMUWindows
from models import TinyMLP
mlp = TinyMLP(); mlp.load_state_dict(torch.load("tinymlp.pt")); mlp.eval()

def c_init(t):                                    # 다차원이면 중첩 중괄호 {{..},{..}}
    if t.dim() == 1:                              # %.9g = float32 왕복(round-trip) 보장
        return "{" + ", ".join(f"{v:.9g}f" for v in t.tolist()) + "}"
    return "{\n  " + ",\n  ".join(c_init(r) for r in t) + "}"

def c_array(name, t):
    dims = "".join(f"[{d}]" for d in t.shape)
    return f"static const float {name}{dims} = {c_init(t)};\n"

sd = mlp.state_dict()
test = IMUWindows(1, seed=7).x                    # 테스트 창 3개 [3,6,50]
with torch.no_grad():                             # PyTorch 기준값(golden) 먼저 계산
    feats = torch.cat([test.mean(dim=2), test.std(dim=2, correction=0)], dim=1)
    logits = mlp(feats)
with open("tinymlp_weights.h", "w") as f:
    f.write("/* auto-generated from tinymlp.pt - do not edit */\n#pragma once\n")
    f.write(f"#define N_IN {sd['fc1.weight'].shape[1]}\n#define N_HID {sd['fc1.weight'].shape[0]}\n")
    f.write(f"#define N_OUT {sd['fc2.weight'].shape[0]}\n#define N_TEST {len(test)}\n")
    for k, v in sd.items():
        f.write(c_array(k.replace(".", "_"), v))  # fc1.weight → fc1_weight[16][12]
    f.write(c_array("test_x", test))              # [3][6][50] 원시 IMU 창
    f.write(c_array("ref_logits", logits))        # [3][3] golden 출력
for i, row in enumerate(logits):
    print(f"torch logits[{i}] = " + " ".join(f"{v:+.6f}" for v in row.tolist()))
```

```text
torch logits[0] = +5.178848 -0.058473 -0.901736
torch logits[1] = -9.951276 +12.811002 -2.079831
torch logits[2] = -3.422510 -0.425375 +7.922180
```

생성된 `tinymlp_weights.h`(약 17 KB, 대부분 테스트 입력)는 `#define N_IN 12` 같은 크기 매크로 뒤에 `static const float fc1_weight[16][12] = {{-0.00216125883f, 0.154857919f, ...}, ...};` 꼴의 중첩 배열이 이어지는 모양이다. 손으로 고치지 않는 **생성 파일**이고, 모델을 다시 학습하면 스크립트로 다시 만든다 — 펌웨어의 register map 헤더 생성기와 같은 취급이다.

### 8.3 C 추론 코드

`static const`라서 MCU에서는 flash(.rodata)에 놓이고 SRAM을 쓰지 않는다. 런타임 SRAM은 `feat[12] + h[16] + out[3]` = 31 float = 124 B뿐이다.

```c
#include <stdio.h>
#include <math.h>
#include "tinymlp_weights.h"

/* [6][50] 원시 창 → 12개 특징 (축별 평균, 모집단 표준편차) */
static void extract_features(const float win[6][50], float feat[N_IN]) {
    for (int c = 0; c < 6; c++) {
        float sum = 0.0f, sq = 0.0f;
        for (int t = 0; t < 50; t++) sum += win[c][t];
        float mean = sum / 50.0f;
        for (int t = 0; t < 50; t++) { float d = win[c][t] - mean; sq += d * d; }
        feat[c] = mean;
        feat[6 + c] = sqrtf(sq / 50.0f);
    }
}

/* nn.Linear: y[o] = b[o] + Σ_i W[o][i]·x[i]   (W는 [out][in], row-major) */
static void mlp_forward(const float x[N_IN], float logits[N_OUT]) {
    float h[N_HID];
    for (int o = 0; o < N_HID; o++) {
        float acc = fc1_bias[o];
        for (int i = 0; i < N_IN; i++) acc += fc1_weight[o][i] * x[i];
        h[o] = acc > 0.0f ? acc : 0.0f;                    /* ReLU */
    }
    for (int o = 0; o < N_OUT; o++) {
        float acc = fc2_bias[o];
        for (int i = 0; i < N_HID; i++) acc += fc2_weight[o][i] * h[i];
        logits[o] = acc;
    }
}

int main(void) {
    float max_diff = 0.0f;
    for (int n = 0; n < N_TEST; n++) {
        float feat[N_IN], out[N_OUT];
        extract_features(test_x[n], feat);
        mlp_forward(feat, out);
        int best = 0;
        for (int k = 1; k < N_OUT; k++) if (out[k] > out[best]) best = k;
        for (int k = 0; k < N_OUT; k++) max_diff = fmaxf(max_diff, fabsf(out[k] - ref_logits[n][k]));
        printf("C     logits[%d] = %+.6f %+.6f %+.6f  -> class %d\n", n, out[0], out[1], out[2], best);
    }
    printf("max |C - torch| = %.3g\n", max_diff);
    printf("weights: %zu bytes\n", sizeof fc1_weight + sizeof fc1_bias + sizeof fc2_weight + sizeof fc2_bias);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 mlp_infer.c -o mlp_infer -lm && ./mlp_infer
```

```text
C     logits[0] = +5.178848 -0.058473 -0.901736  -> class 0
C     logits[1] = -9.951276 +12.811003 -2.079831  -> class 1
C     logits[2] = -3.422509 -0.425376 +7.922182  -> class 2
max |C - torch| = 1.91e-06
weights: 1036 bytes
```

출력에서 볼 것: 경고 0개로 컴파일되고, 세 창 모두 같은 클래스(0 still, 1 shake, 2 circle), 최대 차이 **1.91e-6** — 목표 1e-5 안이다. 가중치 1036 B = 259 × 4 B로 파라미터 수 손계산과 맞는다.

### 8.4 왜 0이 아니라 1.9e-6인가

float 덧셈은 결합법칙이 성립하지 않는다. PyTorch는 평균·분산·행렬곱을 SIMD로 여러 부분합을 나눠 더하거나(합산 순서 다름), FMA(곱셈-덧셈 한 번 반올림)를 쓸 수 있고, C 컴파일러도 `-O2`에서 FMA로 합칠 수 있다(clang은 기본 `-ffp-contract=on`). 값 크기가 12 정도이고 float32 ulp가 약 1e-6이니, 몇 ulp 차이는 정상이다. 그래서 float 비교는 **bit-exact가 아니라 tolerance**로 한다(C8). 반대로 int8 양자화 커널은 정수 연산이라 bit-exact 비교가 가능하고, 그게 기준이 된다.

> Don 연결: 이것이 바로 golden reference 검증이다. SSD 펌웨어에서 시뮬레이터 출력과 실리콘 출력을 비교하던 흐름과 같다 — "모델팀의 PyTorch = 시뮬레이터(golden), 내 C 코드 = DUT". 실무에선 같은 입력 벡터 수백 개를 헤더/파일로 넣고 레이어별로 diff를 찍어 어느 레이어부터 어긋나는지 bisect한다.

---

## 9. 펌웨어로 내보내기 ② — 표준 export 형식들

모델이 커지면 C를 손으로 쓸 수 없다. 표준 경로는 "PyTorch → **그래프 + 가중치**를 한 파일로 → 런타임/컴파일러"다.

### 9.1 형식 비교

| 형식 | 만드는 API | 무엇이 들어 있나 | 다음 단계 | 상태 |
|---|---|---|---|---|
| state_dict `.pt` | `torch.save(m.state_dict())` | 가중치만 | Python 클래스 필요 | 학습 체크포인트용 |
| `torch.export` | `torch.export.export(m, args)` | ATen op 그래프 + 가중치 (`ExportedProgram`) | ExecuTorch, ONNX 변환, 벤더 도구 | PyTorch 2.x 표준 |
| ONNX `.onnx` | `torch.onnx.export(...)` | 표준 op(Conv, Gemm…) 그래프 + 가중치 | ONNX Runtime, TensorRT, 여러 NPU 컴파일러, TFLite 변환 도구 | 업계 교환 형식 |
| ExecuTorch `.pte` | `torch.export` → `executorch` 패키지 | 기기용 직렬화 프로그램 | Cortex-M·Hexagon·Ethos-U 등 백엔드 위임 | PyTorch의 온디바이스 경로 (F 모듈) |
| TFLite `.tflite` | (변환 도구 경유, 예: ai-edge-torch) | flatbuffer 그래프 + int8 가중치 | TFLite Micro + CMSIS-NN | MCU 사실상 표준 (F2) |

### 9.2 코드로 확인 — torch.export, ONNX, TorchScript

ONNX export에는 `onnx`, `onnxscript` 패키지가 필요해서 `.venv/bin/pip install onnx onnxscript`로 설치했다(Python 3.9에서 onnx 1.19.1, onnxscript 0.7.2 설치 성공). torch 2.8의 `dynamo=True` 경로는 내부적으로 `torch.export`를 먼저 부른다.

무엇을 확인하는 코드인지: 세 가지 export가 실제로 되고, 결과 그래프가 원래 모델과 같은 값을 내는지.

```python
import torch, onnx
from models import IMUNet1D
model = IMUNet1D(); model.load_state_dict(torch.load("imunet1d.pt")); model.eval()
example = (torch.randn(1, 6, 50),)
ep = torch.export.export(model, example)          # 1) torch.export → ExportedProgram
ops = sorted({str(n.target) for n in ep.graph.nodes if n.op == "call_function"})
print("torch.export ops:", ops)
torch.onnx.export(model, example, "imunet1d.onnx", input_names=["imu"],
                  output_names=["logits"], dynamo=True,
                  external_data=False, verbose=False)   # 2) ONNX (파일 1개)
m = onnx.load("imunet1d.onnx"); onnx.checker.check_model(m)
print("onnx opset:", m.opset_import[0].version, "| nodes:", [n.op_type for n in m.graph.node])
ts = torch.jit.trace(model, example)             # 3) TorchScript (legacy)
x = torch.randn(1, 6, 50)
with torch.no_grad():
    print("max diff exported vs eager:", (ep.module()(x) - model(x)).abs().max().item(),
          "| torchscript:", (ts(x) - model(x)).abs().max().item())
```

```text
torch.export ops: ['aten.adaptive_avg_pool1d.default', 'aten.conv1d.default', 'aten.dropout.default', 'aten.flatten.using_ints', 'aten.linear.default', 'aten.max_pool1d.default', 'aten.relu.default']
onnx opset: 18 | nodes: ['Conv', 'Relu', 'MaxPool', 'Conv', 'Relu', 'Unsqueeze', 'ReduceMean', 'Squeeze', 'Reshape', 'Gemm']
max diff exported vs eager: 0.0 | torchscript: 0.0
```

(stderr로 "torchvision is not installed" 경고 몇 줄이 나오는데 이 모델과 무관해서 생략했다.)

출력에서 볼 것:

- `torch.export` 그래프에 `aten.dropout`이 **남아 있다**. eval 모드라 실행 시엔 항등이지만, 그래프 수준에서는 아직 노드다. ONNX 쪽에서는 Dropout 노드가 사라졌다. 이런 "그래프에 남은 학습용 잔재"는 이후 변환 단계(decomposition, 그래프 최적화, C6)에서 정리된다.
- ONNX에서 `AdaptiveAvgPool1d(1)`이 `Unsqueeze → ReduceMean → Squeeze`로, `Linear`가 `Gemm`으로 **lowering** 됐다. NPU 컴파일러가 어떤 op를 지원하느냐는 이 단계의 op 이름으로 따진다 — "unsupported op" 문제가 여기서 생긴다(F·C6).
- `external_data=False`를 안 주면 가중치가 `imunet1d.onnx.data`라는 별도 파일로 빠졌다(처음 실행에서 확인). 파일 하나만 옮기다가 가중치를 빠뜨리는 실수가 흔하다.
- export된 그래프와 원래 모델의 출력 차이가 0.0 — 같은 CPU 커널을 쓰니 당연하다. 진짜 검증은 타깃 런타임에서 돌린 출력과 비교할 때다.

### 9.3 배포 경로 한눈에

```
                         ┌─► ExecuTorch (.pte) ─► Cortex-M / Hexagon / Ethos-U 백엔드
PyTorch ─► torch.export ─┤
  (eval)                 └─► ONNX ─► ONNX Runtime / 벤더 NPU 컴파일러 (QNN 등)
                                  └─► (변환) TFLite ─► TFLite Micro + CMSIS-NN (MCU)
   └─► state_dict ─► C 헤더 + 손 코드 (아주 작은 모델, 이 노트 8절)
```

양자화(C1, C2)는 보통 이 경로 **중간**(export 후 또는 export 전 QAT)에 들어간다. 세부 런타임은 F 모듈에서 다룬다.

---

## 10. 임베디드 관점에서 다시 보기

| PyTorch 개념 | MCU/DSP/NPU에서 드러나는 모습 |
|---|---|
| dtype `float32` | Cortex-M4F/M7 FPU로 가능, 하지만 int8(CMSIS-NN, Helium, NPU)이 수 배 빠르고 메모리 1/4 |
| shape `[N,C,L]`, stride | 레이어 커널이 기대하는 layout. NCHW↔NHWC 변환은 memcpy+재배치 비용 — 가능하면 export 단계에서 한 번에 |
| `permute`는 공짜 | 호스트에선 공짜처럼 보여도 contiguous 필요 시 숨은 복사. 기기에선 명시적인 transpose 커널 |
| `state_dict` | flash의 `const` 배열. SRAM이 아니라 XIP flash에서 읽을지, TCM으로 복사할지가 설계 선택 |
| `model.eval()` | 기기는 항상 eval. BN은 conv에 folding, Dropout은 삭제 |
| batch 크기 | 기기 추론은 batch 1 스트리밍. 호스트 batch 처리량 숫자는 기기 지연시간과 무관 |
| autograd, optimizer | 기기에 없음(온디바이스 학습 제외). 학습 메모리(파라미터×4) 걱정은 호스트 몫 |
| golden 출력 | PyTorch eval 출력을 헤더/파일로 떠서 펌웨어 테스트 벡터로 사용 |

예를 들어 Hark 같은 웨어러블의 always-on MCU에 이 노트의 IMUNet1D를 올린다고 가정하면(추정 시나리오): 가중치 int8 약 3 KB(flash), activation 최대 약 3.2 KB(float) 또는 0.8 KB(int8), 연산 88k MAC — 1초에 한 번 돌면 MCU 부하가 1% 미만일 크기다. 이런 back-of-envelope을 PyTorch 모델 정의만 보고 할 수 있어야 한다(D1, D2).

---

## 11. 흔한 실수와 증상

### 11.1 에러 메시지로 배우기

무엇을 확인하는 코드인지: 가장 흔한 실수 다섯 가지가 실제로 어떤 에러·수치를 내는지.

```python
import numpy as np, torch
from torch import nn
from models import IMUNet1D
torch.manual_seed(0)
m = IMUNet1D()
def err(f):
    try: f(); return "OK"
    except Exception as e: return type(e).__name__ + ": " + str(e).splitlines()[0][:70]
x64 = torch.from_numpy(np.zeros((1, 6, 50)))          # numpy 기본 float64!
print("1 dtype  :", x64.dtype, "->", err(lambda: m(x64)))
nlc = torch.zeros(1, 50, 6)                          # [N,L,C]로 넣음
print("2 layout :", err(lambda: m(nlc)))
print("3 no batch:", err(lambda: m(torch.zeros(6, 50))))
logits = torch.tensor([[4.0, 0.0, 0.0]]); y = torch.tensor([0])
ce = nn.CrossEntropyLoss()
print(f"4 CE(logits)={ce(logits, y).item():.4f}  CE(softmax)={ce(torch.softmax(logits, 1), y).item():.4f}")
print("5 label dtype:", err(lambda: ce(logits, y.float())))
```

```text
1 dtype  : torch.float64 -> RuntimeError: Input type (double) and bias type (float) should be the same
2 layout : RuntimeError: Given groups=1, weight of size [16, 6, 5], expected input[1, 50, 6] to
3 no batch: RuntimeError: mat1 and mat2 shapes cannot be multiplied (32x1 and 32x3)
4 CE(logits)=0.0360  CE(softmax)=0.5743
5 label dtype: RuntimeError: expected scalar type Long but found Float
```

출력에서 볼 것:

- 3번이 교묘하다. `Conv1d`는 배치 차원 없는 `[6, 50]`을 "배치 없는 입력"으로 받아 주기 때문에 conv는 통과하고, 한참 뒤 `flatten(1)`에서 shape이 꼬여 Linear에서 터진다. **에러 위치 ≠ 버그 위치**다.
- 4번은 에러가 안 나서 더 위험하다. softmax를 두 번 적용하면 loss가 0.036 → 0.574로 뭉개지고 gradient가 작아져 학습이 느려진다. 정답이 확실한데도 loss가 안 내려가는 증상.

### 11.2 정리 표

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `zero_grad()` 누락 | loss가 들쭉날쭉, 발산 | grad가 배치마다 누적(3.3절의 16.0) | 루프 맨 앞에 `opt.zero_grad()` |
| 평가·export 때 `eval()` 누락 | 같은 입력에 출력이 매번 다름, 기기와 불일치 | Dropout 켜짐, BN이 배치 통계 사용 | 평가·export 전 `model.eval()`, 학습 재개 시 `model.train()` |
| numpy float64 입력 | `Input type (double) and bias type (float)` | numpy 기본 dtype이 float64 | `.astype(np.float32)` 또는 `.float()` |
| label이 float | `expected scalar type Long` | CE는 클래스 인덱스(int64)를 원함 | `.long()` |
| 모델 끝에 softmax + CE | 에러 없이 학습이 느림 | softmax 이중 적용 | 모델은 logits 출력, 확률은 추론 시에만 |
| layout 착각 `[N,L,C]` | `expected input[1, 50, 6] to have 6 channels` | Conv1d는 `[N,C,L]` | `x.permute(0, 2, 1)` |
| 배치 차원 누락 | 엉뚱한 곳에서 shape 에러 | 단일 샘플 `[C,L]`을 그대로 넣음 | `x.unsqueeze(0)`로 `[1,C,L]` |
| non-contiguous에 `view` | `view size is not compatible` | permute/transpose 후 stride가 row-major 아님 | `reshape` 또는 `.contiguous().view(...)` |
| 추론 시 `no_grad` 누락 | 메모리 사용 증가, 느림 | 그래프를 계속 기록 | `torch.no_grad()` / `torch.inference_mode()` |
| C와 전처리 정의 불일치 | C 출력이 PyTorch와 크게 다름 | std의 N vs N−1, 축 순서, 단위(g vs m/s²) | 특징 추출까지 golden 비교, 레이어별 diff |

---

## 12. 면접에서 이렇게 말한다

**Q.** Walk me through a PyTorch training loop.

**A.** DataLoader가 배치를 주면, 매 배치마다 zero_grad로 이전 grad를 비우고, forward로 logits를 얻고, loss를 계산하고, backward로 gradient를 채우고, optimizer.step으로 파라미터를 갱신한다. epoch 끝에는 eval 모드와 no_grad로 검증 세트를 돌리고, 좋은 체크포인트를 state_dict로 저장한다.

> "For each batch from the DataLoader: optimizer.zero_grad() because gradients accumulate, forward pass to get logits, compute the loss — CrossEntropyLoss on raw logits — then loss.backward() to populate .grad, and optimizer.step() to update the weights. After each epoch I switch to model.eval() under torch.no_grad() to measure validation loss and accuracy, and save the state_dict of the best checkpoint."

**Q.** What does model.eval() change?

**A.** 모드 플래그만 바꾼다. Dropout은 꺼지고, BatchNorm은 배치 통계 대신 저장된 running mean/var를 쓴다. gradient 기록을 끄는 건 eval이 아니라 no_grad다 — 둘은 별개라 추론 때는 둘 다 쓴다.

> "It flips the training flag on every submodule. Dropout becomes identity and BatchNorm uses its running mean and variance instead of batch statistics, so the output becomes deterministic and batch-size independent — which is exactly how the model behaves on device. It does not disable autograd; for that you use torch.no_grad() or inference_mode(), so in inference I use both."

**Q.** How would you get a PyTorch model onto an MCU?

**A.** 먼저 모델이 MCU 예산에 맞는지 파라미터·activation·MAC을 계산한다. 그다음 eval 모드로 torch.export 또는 ONNX로 그래프를 뽑고, int8 양자화(PTQ, 필요하면 QAT)를 해서 TFLite Micro+CMSIS-NN이나 ExecuTorch 백엔드, 혹은 벤더 NPU 컴파일러로 넘긴다. 아주 작은 모델은 가중치를 C 헤더로 떠서 직접 C로 짤 수도 있다. 어느 경우든 PyTorch 출력을 golden으로 삼아 같은 입력에서 레이어별로 비교한다 — 저는 12→16→3 MLP를 C로 옮겨 1.9e-6 안에서 맞춰 봤다.

> "First I budget it: parameter bytes, peak activation memory and MACs against the MCU's flash, SRAM and latency target. Then I export in eval mode with torch.export or ONNX, quantize to int8 — PTQ first, QAT if accuracy drops — and hand it to TFLite Micro with CMSIS-NN, an ExecuTorch backend, or the vendor's NPU compiler. For a tiny model I can dump the state_dict into a C header and write the kernels myself; I did that for a small MLP and matched PyTorch within 2e-6. In every case the PyTorch output is my golden reference, compared layer by layer on the same input vectors."

**Q.** What's the difference between view and reshape?

**A.** view는 절대 복사하지 않고 같은 메모리를 새 shape로 해석하며, stride가 맞지 않으면(예: transpose 후) 에러를 낸다. reshape는 가능하면 view, 안 되면 복사한다. 그래서 reshape 결과가 원본과 메모리를 공유하는지는 보장되지 않는다.

> "Both change the shape without changing the element order. view never copies — it reinterprets the same storage with new strides, so it fails on non-contiguous tensors like a transpose. reshape returns a view when possible and silently copies otherwise. Coming from C, a tensor is just a buffer plus shape and strides, exactly like computing base + i times stride0 + j times stride1."

**Q.** Your C port's output differs from PyTorch by 1e-6. Is that a bug?

**A.** float32에서는 정상 범위다. 합산 순서, SIMD 부분합, FMA 때문에 몇 ulp 차이가 난다. 그래서 float는 tolerance로, 정수 양자화 커널은 bit-exact로 비교한다. 1e-3 이상이거나 argmax가 달라지면 레이어별 diff로 어디서 갈라지는지 찾는다.

> "Not by itself. Float addition isn't associative, so different accumulation order, SIMD partial sums or FMA contraction give a few ULPs of difference. I compare floats with a tolerance relative to the output scale and integer kernels bit-exactly. If the error is large or the argmax flips, I dump intermediate tensors and bisect layer by layer to find where they diverge."

---

## 13. 직접 해보기

1. 손계산: shape `(4, 6, 50)`인 contiguous float32 텐서의 stride와, 원소 `[2][3][10]`의 바이트 오프셋은?
정답: stride `(300, 50, 1)`, 오프셋 = (2·300 + 3·50 + 10)·4 = 760·4 = 3040 B.

2. 손계산: 위 텐서를 `permute(0, 2, 1)`하면 shape와 stride는? `data_ptr`은 바뀌는가?
정답: shape `(4, 50, 6)`, stride `(300, 1, 50)`, `data_ptr` 동일(복사 없음).

3. 손계산: `nn.Conv1d(6, 8, kernel_size=3, padding=0)`에 `[1, 6, 50]`을 넣으면 출력 shape, 파라미터 수, MAC 수는?
정답: `[1, 8, 48]`, 8·6·3 + 8 = 152개, 8·48·18 = 6,912 MAC.

4. 코드: 7.1절 루프에서 `opt.zero_grad()`를 지우고 다시 돌려 epoch별 loss를 비교하라. 같은 seed로 비교할 것.
힌트: Adam은 grad 크기에 둔감한 편이라 SGD(`lr=0.05`)로 바꿔서 보면 차이가 더 뚜렷하다.

5. 코드: IMUNet1D에 `nn.BatchNorm1d(16)`을 conv1 뒤에 넣고 학습한 뒤, 평가 시 `model.eval()`을 빼먹으면 batch 1 추론 결과가 어떻게 되는지 확인하라.
힌트: train 모드 BN에 batch 1, 길이 50 입력이면 시간축 50개로 통계를 내서 돌아가긴 하지만, 창마다 정규화 기준이 달라진다. running 통계 기반 eval 출력과 비교해 보라.

6. 코드 + C: 8절 흐름을 IMUNet1D의 마지막 `fc`(32→3)에만 적용해 보라. PyTorch에서 `gap` 출력 `[1,32]`를 뽑아(forward hook 또는 forward를 나눠서) 헤더로 넣고, C로 `fc`만 계산해 logits가 1e-5 안에서 맞는지 확인.
힌트: `model.fc.weight`는 `[3, 32]`, C 배열 `float fc_w[3][32]`. 입력은 `model.gap(torch.relu(model.conv2(model.pool(torch.relu(model.conv1(x)))))).flatten(1)`.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| tensor | 다차원 배열 | 버퍼 + shape + stride + dtype + device |
| dtype | 원소 자료형 | float32, float16, bfloat16, int8, int64 등 |
| device | 메모리 위치 | `cpu`, `cuda`, `mps` — 같은 device끼리만 연산 |
| stride | 차원별 건너뛰기 폭 | 인덱스 1 증가 시 버퍼에서 건너뛰는 원소 수 |
| contiguous | 연속 row-major | stride가 표준 row-major 순서와 일치하는 상태 |
| view / reshape | shape 재해석 | view는 복사 불가, reshape는 필요 시 복사 |
| permute | 차원 순서 변경 | stride만 바꿈. NCHW↔NHWC, [N,L,C]↔[N,C,L] |
| autograd | 자동 미분 | forward 기록 → backward에서 chain rule |
| `.grad` | gradient 저장소 | backward가 **누적**해서 채움 |
| `no_grad` / `inference_mode` | 기록 끄기 | 추론·평가에서 메모리·속도 절약 |
| `nn.Module` | 모델 클래스 | `__init__`에 레이어, `forward`에 흐름 |
| `state_dict` | 가중치 딕셔너리 | 이름 → 텐서, 저장·로드·C 헤더 생성의 입력 |
| logits | softmax 전 점수 | CrossEntropyLoss의 입력 |
| optimizer | 갱신 규칙 | `step()`이 `.grad`로 파라미터를 바꿈 (SGD, Adam) |
| `train()` / `eval()` | 모드 플래그 | Dropout, BatchNorm 동작이 바뀜 |
| Dataset / DataLoader | 데이터 공급 | 인덱스 접근 / 배치·셔플 반복자 |
| `torch.export` | 그래프 추출 | ATen op 그래프 + 가중치, PyTorch 2.x 표준 |
| ONNX | 모델 교환 형식 | 표준 op 그래프, 여러 런타임·컴파일러 입력 |
| ExecuTorch | PyTorch 온디바이스 런타임 | `torch.export` → `.pte` → 백엔드 위임 |
| golden reference | 기준 출력 | PyTorch eval 출력, 펌웨어 검증의 정답 |

---

## 15. 요약 & 체크리스트

PyTorch 텐서는 C 배열과 같은 "버퍼 + shape + stride"이고, `permute`/`transpose`는 stride만 바꾸며 `contiguous()`가 실제 복사를 한다. autograd는 forward를 기록해 `backward()`로 `.grad`를 **누적**하므로 학습 루프는 zero_grad → forward → loss → backward → step 다섯 줄이 전부다. `nn.Module`은 구조(코드)이고 `state_dict`는 값(가중치)이며, `eval()`은 Dropout과 BatchNorm을 기기와 같은 결정적 동작으로 바꾼다. 합성 IMU 데이터로 1D-CNN(3187 파라미터, 약 88k MAC)을 CPU에서 1초 안에 학습했고, 12→16→3 MLP는 가중치를 C 헤더로 뽑아 C 코드가 PyTorch와 1.9e-6 안에서 일치함을 확인했다. 큰 모델은 `torch.export`/ONNX로 그래프를 뽑아 런타임·컴파일러로 넘기며, 어느 경로든 PyTorch 출력이 golden reference다.

- [ ] 텐서의 shape로부터 stride를 손으로 쓰고, 원소 바이트 오프셋을 계산할 수 있다
- [ ] `view`/`reshape`/`contiguous`/`permute`가 메모리를 복사하는지 안 하는지 말할 수 있다
- [ ] `y = w·x + b`, MSE loss의 gradient를 손으로 계산하고 autograd로 확인할 수 있다
- [ ] `Conv1d`/`Linear` 레이어의 출력 shape, 파라미터 수, MAC 수를 손으로 계산할 수 있다
- [ ] 학습 루프 다섯 줄을 보지 않고 쓰고, 각 줄이 빠지면 무슨 일이 생기는지 설명할 수 있다
- [ ] `train()`과 `eval()`에서 Dropout·BatchNorm이 어떻게 다른지 숫자로 설명할 수 있다
- [ ] 커스텀 `Dataset`을 만들고 `DataLoader`로 배치·셔플을 할 수 있다
- [ ] state_dict를 저장·로드하고, batch 1 지연시간을 warm-up 후 분포로 잴 수 있다
- [ ] 작은 MLP의 가중치를 C 헤더로 뽑아 C 추론 결과를 PyTorch와 tolerance로 비교할 수 있다
- [ ] `torch.export`, ONNX, TorchScript, ExecuTorch, TFLite가 배포 경로 어디에 있는지 말할 수 있다

---

## 참고 자료

- PyTorch 공식 튜토리얼 "Learn the Basics" (Tensors, Datasets & DataLoaders, Autograd, Optimization, Save & Load): [pytorch.org/tutorials/beginner/basics/intro.html](https://pytorch.org/tutorials/beginner/basics/intro.html)
- PyTorch 문서 — Tensor views(stride, view, contiguous): [pytorch.org/docs/stable/tensor_view.html](https://pytorch.org/docs/stable/tensor_view.html)
- PyTorch 문서 — `torch.export`: [pytorch.org/docs/stable/export.html](https://pytorch.org/docs/stable/export.html)
- PyTorch 문서 — ONNX export: [pytorch.org/docs/stable/onnx.html](https://pytorch.org/docs/stable/onnx.html)
- ExecuTorch 문서: [pytorch.org/executorch](https://pytorch.org/executorch/)
- Dive into Deep Learning (d2l.ai) — PyTorch 판, 2~5장: [d2l.ai](https://d2l.ai)
- Andrej Karpathy — Neural Networks: Zero to Hero (micrograd로 autograd 직접 만들기): [karpathy.ai/zero-to-hero.html](https://karpathy.ai/zero-to-hero.html)
- Goodfellow, Bengio, Courville, "Deep Learning" (MIT Press, 2016) — 6~8장: [deeplearningbook.org](https://www.deeplearningbook.org)
- MIT 6.5940 TinyML and Efficient Deep Learning (Song Han): [efficientml.ai](https://efficientml.ai)
