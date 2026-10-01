# D1. FLOPs · MACs 계산 — 레이어별 공식과 모델 전체 손계산 드릴

> **이 노트를 다 읽으면**: MAC·FLOP·OPS·TOPS를 섞지 않고 단위를 맞출 수 있다 · conv·depthwise·FC·attention·RNN·transformer 층의 MAC 수를 공식 하나("출력 원소 수 × 누적 길이")로 손계산할 수 있다 · forward hook과 `FlopCounterMode`로 모델 전체를 세고, 도구마다 무엇을 빠뜨리는지 설명할 수 있다 · MAC 수에서 첫 latency 하한과 MCU 프레임 예산을 뽑을 수 있다
> **JD 연결**: (우대) "Understand the performance characteristics of edge AI models, including CNN, RNN, transformers, KV-cache behavior, and their memory bandwidth requirements" · study_prep_list D1 — conv, depthwise, FC, attention 레이어별 MAC 공식, 모델 전체 연산량 손계산 · 4절 back-of-envelope 드릴("conv 레이어 MAC 수·파라미터 수", "16 kHz 오디오, 10 ms hop일 때 프레임당 연산 예산")
> **Don 기준 난이도**: MAC 루프·사이클 예산·"명령어 수로 시간 추정"의 감각은 이미 있음 / 레이어 종류별 공식을 한 체계로 외우는 것, 도구·논문·벤더마다 다른 ×2 관례, transformer 전체 층과 LLM 전체의 손계산은 새로 배움
> **선행 노트**: A1(GEMM 비용 M·N·K), B1(MLP·정규화·BN folding), B2(conv 출력 크기·MAC·DS-conv), B3(LSTM/GRU), B4(attention·GQA·12d²), B6(해상도 스케일링), B8(LLM 파라미터 수), C7(FLOPs ≠ latency)

---

## 0. 큰 그림 — 이게 왜 필요한가

edge ML 엔지니어가 받는 질문의 절반은 "이 모델이 이 칩에서 돌아가나?"다. 이 질문에 답하려면 숫자 세 개가 필요하다.

```
① 모델이 추론 한 번에 하는 일의 양      → MAC 수        (이 노트, D1)
② 모델이 차지하고 옮기는 바이트          → 파라미터·activation·KV (D2)
③ 칩이 1초에 할 수 있는 일과 옮길 수 있는 바이트 → MAC/s, GB/s (D3 roofline, E 모듈)
```

①이 없으면 나머지 계산이 시작도 안 된다. 그래서 면접에서 화이트보드에 "3×3 conv 64→128, 56×56이면 MAC이 몇 개죠?", "1B 모델 토큰 하나에 FLOP이 얼마죠?"를 묻는다. 정답 숫자보다 **공식을 어디서 꺼내 오는지, 단위를 어떻게 맞추는지**를 본다.

Don에게 익숙한 말로 바꾸면 이렇다. 펌웨어에서 "이 루프가 몇 사이클 걸리나?"를 볼 때 먼저 **루프 반복 횟수 × 반복당 명령어 수**를 센다. MAC 세기는 정확히 그 일이다. 모든 레이어는 결국 `acc += w · x`를 몇 번 도는 중첩 루프이고, D1은 그 반복 횟수를 레이어 종류별로 공식화한 것이다.

```
펌웨어                                  ML 성능 모델
────────────────────────────────       ─────────────────────────────────────────
루프 반복 수 × 반복당 명령어            MAC 수 (= 곱셈-누산 반복 수)
CPU MHz × IPC                           칩의 MAC/s (= MAC 유닛 수 × 클럭 × 이용률)
명령어 수 ÷ (MHz × IPC) = 시간 하한     MAC ÷ (MAC/s × 이용률) = latency 하한
캐시 미스·버스 대기 때문에 더 걸림      메모리 대역폭 때문에 더 걸림 (D3)
```

이미 여러 노트에서 조각을 셌다. A1은 GEMM 비용 M·N·K, B1은 MLP, B2는 conv·DS-conv와 hook 카운터, B4는 attention과 12d², B6은 해상도 스케일링, B8은 LLM 파라미터 수, C7은 "FLOPs가 latency를 못 맞추는 이유"를 다뤘다. **D1은 그 조각들을 하나의 공식 시트와 드릴 북으로 묶는다.** 유도는 짧게 복습하고 해당 노트 ID를 가리킨다.

이 노트의 순서: 단위와 ×2 함정(1절) → 공식 시트(2절, 핵심) → 카운터 만들기와 도구 비교(3절) → 모델 전체 손계산 드릴 5개(4절) → 경험칙(5절) → MAC에서 latency 첫 추정(6절) → 인터뷰 퀵 드릴 10문제(7절) → MCU 예산(8절).

> 실행 환경: 모든 Python 예제는 이 폴더의 `.venv/bin/python` (Python 3.9, torch 2.8.0, torchvision 0.23.0, transformers 4.57)로 실제 실행한 출력이다. HF 모델은 로컬 캐시에서만 읽었다(`local_files_only=True`). C 예제는 `cc -std=c11 -Wall -Wextra -O2 ... -lm`으로 경고 0개로 컴파일했다. `formulas.py`(2절), `maccount.py`·`block.py`(3절)는 같은 디렉터리에 저장해 두고 뒤 예제에서 import 한다.

---

## 1. 단위부터 — MAC, FLOP, OPS, TOPS와 ×2 함정

### 1.1 직관: "곱하고 더하기" 한 번이 기본 단위다

신경망 연산의 99%는 내적이다. 내적은 이렇게 생긴 루프다.

```
acc = 0;
for (k = 0; k < K; k++) acc += w[k] * x[k];   /* 곱셈 1 + 덧셈 1 = MAC 1 */
```

- **MAC** (multiply-accumulate): `acc += w · x` 한 번. DSP의 MAC 유닛, ARM의 `SMLAD`(16bit 두 쌍), FPU의 FMA 명령 하나가 여기에 해당한다.
- **FLOP** (floating-point operation): 부동소수점 연산 하나. 관례상 **MAC 1 = FLOP 2**(곱셈 1 + 덧셈 1)로 센다. FMA 명령 하나가 "2 FLOP"으로 계산되는 이유다.
- **OP**: 정수 연산까지 포함한 연산 하나. int8 가속기는 FLOP이 아니라 OP로 광고한다. 역시 MAC 1 = OP 2가 표준 관례다.

말로 하면: **MAC은 "몇 번 곱해서 쌓았나", FLOP/OP은 "산술 연산 몇 개였나"**이고 둘은 정확히 2배 차이다. 단, 이 "2"는 관례다. 누가 어느 쪽을 썼는지 확인하지 않으면 두 배가 틀린다.

### 1.2 양(per inference)과 속도(per second)

두 번째 혼동은 대소문자 하나다.

- 모델의 **GFLOPs** (소문자 s = 복수형): 추론 1회에 필요한 연산의 **양**. 단위는 "개".
- 하드웨어의 **GFLOPS** (대문자 S = per Second): 1초에 할 수 있는 연산의 **속도**. 단위는 "개/초".
- **TOPS**: Tera Operations Per Second. 보통 int8 기준, MAC 1 = 2 OP로 센 **피크** 속도다. 벤더 숫자는 보통 `MAC 유닛 수 × 2 × 클럭`이다. 예: MAC 1024개 × 2 × 1 GHz = 2.048 TOPS.

글로 쓸 때는 헷갈리지 않게 "GFLOP/inference", "GFLOP/s", "GMAC", "GMAC/s"처럼 **슬래시로 단위를 명시**하는 습관을 들인다. 이 노트는 기본 단위를 **MAC**으로 쓰고, FLOP이 필요할 때만 ×2를 붙인다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs> <marker id="d1m0" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"> <path d="M0,0 L8,4 L0,8 z" fill="currentColor"/> </marker> </defs> <text x="160" y="24" font-size="13" text-anchor="middle">모델의 양 (추론 1회당)</text> <text x="520" y="24" font-size="13" text-anchor="middle">하드웨어의 속도 (초당)</text> <rect x="30" y="40" width="120" height="56" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="90" y="64" font-size="13" text-anchor="middle">MAC</text> <text x="90" y="84" font-size="12" text-anchor="middle">예: 0.30 G</text> <rect x="190" y="40" width="130" height="56" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<text x="255" y="64" font-size="13" text-anchor="middle">FLOPs (소문자 s)</text> <text x="255" y="84" font-size="12" text-anchor="middle">예: 0.60 G</text> <line x1="150" y1="68" x2="188" y2="68" stroke="currentColor" stroke-width="1.5" marker-end="url(#d1m0)"/> <text x="169" y="60" font-size="12" text-anchor="middle">×2</text> <rect x="390" y="40" width="120" height="56" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/> <text x="450" y="64" font-size="13" text-anchor="middle">MAC/s</text> <text x="450" y="84" font-size="12" text-anchor="middle">예: 2 T</text> <rect x="550" y="40" width="120" height="56" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="610" y="64" font-size="13" text-anchor="middle">FLOPS · TOPS</text> <text x="610" y="84" font-size="12" text-anchor="middle">예: 4 TOPS</text> <line x1="510" y1="68" x2="548" y2="68" stroke="currentColor" stroke-width="1.5" marker-end="url(#d1m0)"/> <text x="529" y="60" font-size="12" text-anchor="middle">×2</text> <rect x="215" y="150" width="250" height="56" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="340" y="174" font-size="13" text-anchor="middle">이상적 시간 = 양 ÷ 속도</text> <text x="340" y="194" font-size="12" text-anchor="middle">0.60 G ÷ 4 T = 0.15 ms (하한)</text>
<line x1="255" y1="96" x2="300" y2="148" stroke="currentColor" stroke-width="1.5" marker-end="url(#d1m0)"/> <line x1="610" y1="96" x2="420" y2="148" stroke="currentColor" stroke-width="1.5" marker-end="url(#d1m0)"/> <text x="340" y="236" font-size="12" text-anchor="middle">분자와 분모를 같은 단위(둘 다 MAC, 또는 둘 다 FLOP/OP)로 맞추는 것이 전부다</text>
</svg>
```

그림 1 — 양(왼쪽)과 속도(오른쪽)는 각각 MAC 단위와 FLOP/OP 단위가 있고 둘 사이는 ×2다. 시간 하한은 양 ÷ 속도인데, 분자·분모의 단위만 맞으면 어느 쪽을 써도 같다. 0.30 GMAC 모델을 4 TOPS(= 2 TMAC/s) NPU에서 돌리면 0.15 ms가 이론 하한이다.

### 1.3 단위 표

| 용어 | 뜻 | 단위 | 예 | 함정 |
|---|---|---|---|---|
| MAC (MAdds, multiply-adds) | 곱셈-누산 1회 | 개 | MobileNetV2 @224 ≈ 300M MAC | 논문이 "FLOPs"라고 쓰고 MAC을 적는 경우가 많다 |
| FLOP | 부동소수점 연산 1개 | 개 | 같은 모델 ≈ 0.60 GFLOP | MAC 1 = FLOP 2 관례. 도구마다 다름 |
| GFLOPs (모델) | 추론 1회 연산량 | 10⁹ 개 | "0.6 GFLOPs" | 속도가 아니라 양이다 |
| GFLOPS (HW) | 초당 부동소수점 연산 | 10⁹ 개/s | CPU 코어 수십 GFLOPS | fp32인지 fp16인지 명시 필요 |
| OPS / TOPS | 초당 연산(정수 포함) | 10¹² 개/s | 모바일 NPU 수 TOPS~수십 TOPS | int8 기준, 피크, 2 OP/MAC. sparse TOPS는 dense의 2배로 적기도 한다 |
| MAC/s | 초당 MAC | 개/s | 2 TOPS NPU = 1 TMAC/s | TOPS를 MAC/s로 바꿀 때 ÷2를 잊는다 |
| 이용률 (utilization) | 실효 속도 ÷ 피크 속도 | % | 작은 레이어는 10~30%도 흔함 | 피크 TOPS로 나누면 latency를 과소평가 |

### 1.4 ×2 혼동의 실제 사례

같은 모델을 두고 숫자가 두 배 다른 일이 실제로 흔하다. 알려진 사례를 모으면:

- **ResNet 논문**(He et al. 2016)은 "Our 34-layer baseline has 3.6 billion FLOPs (multiply-adds)"라고 쓴다. 단어는 FLOPs인데 센 것은 multiply-add, 즉 MAC이다.
- **MobileNetV2 논문**은 "MAdds 300M"이라고 명시한다 — MAC 단위.
- **torchvision** 모델 문서 표의 "GFLOPS" 열과 weights `meta["_ops"]` 값은, 아래 코드에서 보듯 `FlopCounterMode`의 정확히 절반이다. 즉 이름은 GFLOPS인데 값은 GMAC이다.
- **`torch.utils.flop_counter.FlopCounterMode`**는 행렬곱 [M,K]×[K,N]을 `2·M·N·K` FLOP으로 센다(MAC × 2).
- 서드파티 카운터(fvcore, thop, ptflops 등)는 문서 기준으로 MAC 수를 돌려주거나 "fused multiply-add 1개 = flop 1개"로 센다고 밝힌다. 버전마다 확인할 것 — 이 노트에서는 설치하지 않았다.

**torchvision 표 숫자와 FlopCounterMode가 정확히 2배 차이 나는지 확인하는 코드**다.

```python
# 같은 MobileNetV2를 "GFLOPs"라고 부르는 두 숫자 — torchvision 표 vs FlopCounterMode
import torch
from torch.utils.flop_counter import FlopCounterMode
from torchvision.models import mobilenet_v2, MobileNet_V2_Weights, resnet50, ResNet50_Weights
for ctor, w in ((mobilenet_v2, MobileNet_V2_Weights.IMAGENET1K_V1), (resnet50, ResNet50_Weights.IMAGENET1K_V1)):
    m = ctor(weights=None).eval()                      # 구조만 (가중치 다운로드 없음)
    with torch.no_grad(), FlopCounterMode(display=False) as fc:
        m(torch.randn(1, 3, 224, 224))
    g = fc.get_total_flops() / 1e9
    print(f"{ctor.__name__:12s} torchvision meta _ops = {w.meta['_ops']:.3f}"
          f" | FlopCounterMode = {g:.3f} GFLOP (= {g/2:.3f} GMAC)")
```

```text
mobilenet_v2 torchvision meta _ops = 0.301 | FlopCounterMode = 0.602 GFLOP (= 0.301 GMAC)
resnet50     torchvision meta _ops = 4.089 | FlopCounterMode = 8.178 GFLOP (= 4.089 GMAC)
```

출력에서 볼 것: `_ops` 0.301은 FlopCounterMode 0.602 GFLOP의 정확히 절반, 즉 **GMAC**이다. ResNet-50의 4.089도 같다. 누군가 "MobileNetV2는 0.3 GFLOPs"라고 말하면 MAC을 FLOP이라고 부른 것이다. 면접에서 숫자를 말할 때는 **"300M MACs, i.e. about 0.6 GFLOPs"처럼 둘 다** 말하면 안전하다.

### 1.5 함정

- 모델 쪽은 MAC, 칩 쪽은 TOPS(= 2 OP/MAC)로 나눠 버리면 latency가 **2배 낙관적**으로 나온다. 반대로 둘 다 FLOP이라 생각하고 MAC을 FLOP으로 나누면 2배 비관적이다.
- "int8 모델이라 MAC이 줄었다"는 틀린 말이다. 양자화는 MAC **수**를 바꾸지 않는다. MAC **하나의 비용**(에너지·면적·처리량)을 바꾼다(C1, D7).
- sparse TOPS, fp16 TOPS, int4 TOPS는 서로 다른 숫자다. 칩 비교표에서는 같은 정밀도·dense 기준으로 맞춘다(M 모듈).

---

## 2. 공식 시트 — 모든 레이어를 한 문장으로

### 2.1 하나의 규칙

레이어가 달라도 세는 방법은 하나다.

```
MAC = (출력 원소 수) × (출력 원소 하나를 만들 때 누적하는 길이 K)
```

말로 하면: 출력 텐서의 칸 수를 세고, 칸 하나를 채우려고 `acc +=`를 몇 번 도는지 곱한다. Linear면 K = d_in, conv면 K = (C_in/g)·k_h·k_w, attention 점수면 K = d_h다. 이 규칙만 기억하면 처음 보는 레이어도 셀 수 있다.

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="22" font-size="14" text-anchor="middle">MAC = (출력 원소 수) × (출력 원소 하나를 만들 때 누적하는 길이 K)</text> <text x="120" y="52" font-size="13" text-anchor="middle">Linear / GEMM</text> <rect x="50" y="66" width="140" height="96" fill="none" stroke="#4a7bd0" stroke-width="2"/> <line x1="78" y1="66" x2="78" y2="162" stroke="#4a7bd0" stroke-width="0.7"/> <line x1="106" y1="66" x2="106" y2="162" stroke="#4a7bd0" stroke-width="0.7"/> <line x1="134" y1="66" x2="134" y2="162" stroke="#4a7bd0" stroke-width="0.7"/> <line x1="162" y1="66" x2="162" y2="162" stroke="#4a7bd0" stroke-width="0.7"/> <line x1="50" y1="90" x2="190" y2="90" stroke="#4a7bd0" stroke-width="0.7"/>
<line x1="50" y1="114" x2="190" y2="114" stroke="#4a7bd0" stroke-width="0.7"/> <line x1="50" y1="138" x2="190" y2="138" stroke="#4a7bd0" stroke-width="0.7"/> <rect x="106" y="90" width="28" height="24" fill="#e08a3c"/> <text x="120" y="182" font-size="12" text-anchor="middle">출력 Y: T × d_out 칸</text> <text x="120" y="200" font-size="12" text-anchor="middle">칸 하나 = 길이 d_in 내적</text> <text x="120" y="226" font-size="13" text-anchor="middle">T · d_in · d_out</text> <text x="340" y="52" font-size="13" text-anchor="middle">Conv2d (grouped 포함)</text> <polygon points="280,86 300,66 420,66 400,86" fill="none" stroke="#3f9a6b" stroke-width="1.5"/>
<polygon points="400,86 420,66 420,142 400,162" fill="none" stroke="#3f9a6b" stroke-width="1.5"/> <rect x="280" y="86" width="120" height="76" fill="none" stroke="#3f9a6b" stroke-width="2"/> <line x1="310" y1="86" x2="310" y2="162" stroke="#3f9a6b" stroke-width="0.7"/> <line x1="340" y1="86" x2="340" y2="162" stroke="#3f9a6b" stroke-width="0.7"/> <line x1="370" y1="86" x2="370" y2="162" stroke="#3f9a6b" stroke-width="0.7"/> <line x1="280" y1="105" x2="400" y2="105" stroke="#3f9a6b" stroke-width="0.7"/> <line x1="280" y1="124" x2="400" y2="124" stroke="#3f9a6b" stroke-width="0.7"/> <line x1="280" y1="143" x2="400" y2="143" stroke="#3f9a6b" stroke-width="0.7"/>
<rect x="340" y="105" width="30" height="19" fill="#e08a3c"/> <text x="340" y="182" font-size="12" text-anchor="middle">출력: Ho × Wo × C_out 원소</text> <text x="340" y="200" font-size="12" text-anchor="middle">원소 하나 = (C_in/g) · k_h · k_w</text> <text x="340" y="226" font-size="13" text-anchor="middle">Ho · Wo · C_out · (C_in/g) · k²</text> <text x="570" y="52" font-size="13" text-anchor="middle">Attention (head 하나)</text> <rect x="490" y="66" width="96" height="96" fill="none" stroke="#d0564a" stroke-width="2"/> <line x1="514" y1="66" x2="514" y2="162" stroke="#d0564a" stroke-width="0.7"/> <line x1="538" y1="66" x2="538" y2="162" stroke="#d0564a" stroke-width="0.7"/>
<line x1="562" y1="66" x2="562" y2="162" stroke="#d0564a" stroke-width="0.7"/> <line x1="490" y1="90" x2="586" y2="90" stroke="#d0564a" stroke-width="0.7"/> <line x1="490" y1="114" x2="586" y2="114" stroke="#d0564a" stroke-width="0.7"/> <line x1="490" y1="138" x2="586" y2="138" stroke="#d0564a" stroke-width="0.7"/> <rect x="514" y="90" width="24" height="24" fill="#e08a3c"/> <rect x="606" y="66" width="44" height="96" fill="none" stroke="#d0564a" stroke-width="2"/> <line x1="628" y1="66" x2="628" y2="162" stroke="#d0564a" stroke-width="0.7"/> <line x1="606" y1="90" x2="650" y2="90" stroke="#d0564a" stroke-width="0.7"/>
<line x1="606" y1="114" x2="650" y2="114" stroke="#d0564a" stroke-width="0.7"/> <line x1="606" y1="138" x2="650" y2="138" stroke="#d0564a" stroke-width="0.7"/> <text x="538" y="182" font-size="12" text-anchor="middle">S = QKᵀ: T×T, 칸당 d_h</text> <text x="628" y="182" font-size="12" text-anchor="middle">O = PV</text> <text x="570" y="200" font-size="12" text-anchor="middle">O: T×d_h 칸, 칸당 T</text> <text x="570" y="226" font-size="13" text-anchor="middle">T·T·d_h + T·d_h·T = 2·T²·d_h</text> <line x1="20" y1="248" x2="660" y2="248" stroke="#888" stroke-width="0.8" stroke-dasharray="3 3"/>
<text x="340" y="270" font-size="12" text-anchor="middle">depthwise: g = C_in = C_out → 원소당 k² · pointwise: k = 1 → 원소당 C_in · conv1d: k² 대신 k</text> <text x="340" y="292" font-size="12" text-anchor="middle">RNN step: 게이트 수 × H 원소, 원소당 (I + H) · 주황 칸 = 출력 원소 하나 (여기에 K번 MAC)</text>
</svg>
```

그림 2 — 세 대표 레이어의 출력(격자)과 칸 하나(주황)의 누적 길이. 공식은 전부 "칸 수 × 칸당 누적 길이"다. 아래 줄은 변형들이 K만 바꾼다는 것을 보여 준다.

### 2.2 마스터 표 (batch = 1, MAC 단위)

| 레이어 | MAC 공식 | 파라미터 | 작은 예 | 예의 MAC |
|---|---|---|---|---|
| Linear / GEMM | T·d_in·d_out | d_in·d_out (+d_out bias) | T=4, 64→128 | 32,768 |
| Conv2d | Ho·Wo·C_out·C_in·k_h·k_w | C_out·C_in·k² (+C_out) | 3×3, 64→128, 56×56 same | 231,211,008 |
| Grouped conv | Ho·Wo·C_out·(C_in/g)·k² | C_out·(C_in/g)·k² | 위와 같고 g=4 | 57,802,752 |
| Depthwise | Ho·Wo·C·k² | C·k² | 3×3 s2, C=64, 56→28 | 451,584 |
| Pointwise (1×1) | H·W·C_in·C_out | C_in·C_out | 64→128, 28×28 | 6,422,528 |
| Conv1d / TCN | To·C_out·(C_in/g)·k | C_out·(C_in/g)·k | k5 dil2, 6→16, T=128 | 57,600 |
| Attention core | h·Tq·Tk·d_h·2 | 0 | h=8, T=128, d_h=32 | 8,388,608 |
| LSTM (step) | 4·H·(I+H) | 4H(I+H) + 편향 | I=40, H=64, 49 step | 1,304,576 |
| GRU (step) | 3·H·(I+H) | 3H(I+H) + 편향 | I=40, H=128, 1 step | 64,512 |
| Llama식 층 (토큰당) | d·h·d_h + 2·d·kv·d_h + h·d_h·d + 3·d·ff + 2·h·d_h·Tk | 앞 4항 | Llama-3.2-1B, Tk=512 | 62.9M |
| Embedding lookup | 0 | V·d | 토큰 1개, d=576 | 0 (d개 읽기) |
| BN / LN / RMSNorm | 0 (elementwise op만) | 2C 또는 2d 또는 d | — | folding하면 BN은 0 |
| 활성화 · softmax · pooling | 0 (elementwise / reduction op) | 0 | — | op 수는 따로 센다 |
| Upsample (nearest/bilinear) | 0 | 0 | — | ConvTranspose는 H_in·W_in·C_in·C_out·k² |

아래 소절에서 한 줄씩 푼다. 먼저 이 표의 숫자를 코드로 남긴다.

**공식 시트를 함수로 옮긴 모듈**이다 (뒤 예제들이 import 한다).

```python
# formulas.py — D1 공식 시트를 함수로 (반환값: MAC 수, batch=1)
def linear(T, d_in, d_out):              return T * d_in * d_out
def conv2d(Ho, Wo, Cin, Cout, kh, kw, g=1): return Ho * Wo * Cout * (Cin // g) * kh * kw
def depthwise(Ho, Wo, C, k):             return Ho * Wo * C * k * k
def pointwise(H, W, Cin, Cout):          return H * W * Cin * Cout
def conv1d(To, Cin, Cout, k, g=1):       return To * Cout * (Cin // g) * k
def attn_core(h, Tq, Tk, dh):            return h * Tq * Tk * dh * 2      # QKᵀ + AV
def rnn_step(gates, I, H):               return gates * H * (I + H)       # GRU 3, LSTM 4
def out_size(n, k, s=1, p=0, d=1):       return (n + 2 * p - d * (k - 1) - 1) // s + 1
def llama_layer(T, d, h, kv, dh, ff, Tk=None):
    Tk = T if Tk is None else Tk                                            # decode면 T=1, Tk=캐시 길이
    proj = T * (d * h * dh + 2 * d * kv * dh + h * dh * d)
    ffn = T * 3 * d * ff                                                    # SwiGLU: gate, up, down
    return proj, attn_core(h, T, Tk, dh), ffn
```

### 2.3 Linear / GEMM

직관: 출력 칸 하나 = 입력 벡터(길이 d_in)와 가중치 행 하나의 내적. A1 5절의 M·N·K를 그대로 쓴다: M = 토큰 수(또는 batch) T, N = d_out, K = d_in.

```
MAC = T · d_in · d_out          (bias 덧셈 T·d_out개는 보통 무시)
예: T = 4, d_in = 64, d_out = 128 → 4 · 64 · 128 = 32,768 MAC
```

말로 하면: 토큰이 T개면 같은 가중치로 T번 곱한다. 파라미터는 T와 무관하지만 MAC은 T에 비례한다. 이 차이가 D3·D5의 "가중치 재사용"과 "decode는 memory-bound"의 뿌리다.

### 2.4 Conv2d — 일반과 grouped

B2 3.2절의 공식을 그대로 쓴다. 출력 크기부터 구한다.

```
Ho = ⌊(H + 2p − d·(k − 1) − 1) / s⌋ + 1
MAC = Ho · Wo · C_out · (C_in / g) · k_h · k_w
params = C_out · (C_in / g) · k_h · k_w  (+ C_out bias)
```

손계산 (3×3, 64→128, 56×56, stride 1, padding 1):

```
Ho = (56 + 2 − 2 − 1)/1 + 1 = 56
MAC = 56 · 56 · 128 · 64 · 9 = 3,136 · 73,728 = 231,211,008 ≈ 231M MAC ≈ 462M FLOP
params = 128 · 64 · 9 + 128 = 73,856
g = 4이면 각 출력 채널이 입력 16채널만 봄 → MAC도 params도 1/4 → 57,802,752
```

말로 하면: grouped conv는 채널을 g개 묶음으로 나눠 **블록 대각 행렬**처럼 곱한다. MAC과 파라미터가 정확히 1/g이 된다.

### 2.5 Depthwise와 pointwise

depthwise는 g = C_in = C_out인 grouped conv다. 출력 원소 하나가 자기 채널의 k×k 창만 본다 → 칸당 k². pointwise는 k = 1인 일반 conv다 → 칸당 C_in. 둘을 이으면 depthwise separable이고 표준 conv 대비 비율은 `1/C_out + 1/k²`다(B2 6.3에서 유도).

```
depthwise 3×3, stride 2, C = 64, 56 → 28:   28 · 28 · 64 · 9 = 451,584
pointwise 64 → 128 @ 28×28:                   28 · 28 · 64 · 128 = 6,422,528
```

실전 감각: MobileNet 계열에서 depthwise는 MAC 몫이 5~10%밖에 안 되는데 latency 몫은 훨씬 클 때가 많다. MAC당 읽는 바이트가 많아서(arithmetic intensity가 낮아서)다. 이것은 D3·D4의 주제이고, 여기서는 "MAC 수가 작다 ≠ 빠르다"만 기억한다.

### 2.6 Conv1d / TCN

conv2d에서 공간 축 하나를 뺀 것이다. **dilation은 MAC 수를 바꾸지 않는다** — 탭 간격만 벌어질 뿐 탭 수 k는 같다. 대신 출력 길이 To가 짧아질 수 있다(padding이 없으면).

```
To = 128 + 0 − 2·(5 − 1) − 1 + 1 = 120      (k = 5, dilation 2, padding 0)
MAC = To · C_out · C_in · k = 120 · 16 · 6 · 5 = 57,600
```

스트리밍(B2 8절)에서는 새 샘플 하나마다 출력 한 칸만 새로 만든다 → **step당 C_out·C_in·k MAC**. 이 "step당 비용"이 MCU의 샘플 주기 예산과 바로 비교할 숫자다(8절).

### 2.7 Pooling과 global average pooling

MAC이 아니라 비교·덧셈이다. 그래도 op 수는 알아 둔다.

```
MaxPool k×k:          출력 원소당 k² − 1 비교
AvgPool k×k:          출력 원소당 k² − 1 덧셈 + 1 곱셈(1/k²)
Global avg pool:      H·W·C 덧셈 + C 곱셈
예: GAP 7×7×1280 (MobileNetV2 끝) = 62,720 덧셈 — 전체 300M MAC의 0.02%
```

### 2.8 정규화 — BatchNorm, LayerNorm, RMSNorm

정규화는 곱셈-누산 GEMM이 아니라 **원소별 연산 + reduction**이다. MAC 카운터는 보통 0으로 센다. 원소당 op 수를 대략 세면(B1 5~7절 복습):

| 연산 | 추론 시 식 | 원소당 op (대략) | 행당 추가 | folding |
|---|---|---|---|---|
| BatchNorm (eval) | y = a·x + b (a, b는 채널별 상수로 미리 계산) | 곱 1 + 덧 1 | 없음 | 앞 conv/linear에 **완전히** 접힘 → 0 (B1 7절) |
| LayerNorm | 평균·분산 계산 후 (x−μ)·rsqrt(σ²+ε)·γ + β | 약 7~8 | rsqrt 1 | 통계가 입력마다 달라 못 접음. γ·β만 다음 Linear로 옮길 수 있음 |
| RMSNorm | x · rsqrt(mean(x²)+ε) · γ | 약 4 | rsqrt 1 | γ는 다음 Linear 가중치에 곱해 넣을 수 있음. 정규화 자체는 남음 |

말로 하면: BN은 배포 전에 사라지고, LN/RMSNorm은 남는다. 남는 것들은 MAC 배열이 아니라 vector 유닛·DSP에서 돌고, **MAC 수에는 안 잡히는데 시간은 먹는다**. transformer에서 이것이 "MAC으로 예측한 latency < 실측"의 한 원인이다.

### 2.9 활성화 — elementwise, 그리고 LUT

ReLU는 원소당 비교 1개(int8에서는 clamp로 사실상 공짜, B1 4.2). GELU·SiLU·sigmoid·tanh는 exp와 나눗셈이 들어가서 원소당 수~수십 op다. MCU·NPU에서는 보통 **LUT**(int8 입력 256칸 표, 원소당 로드 1개)로 바꾼다.

```
SwiGLU FFN의 elementwise:  silu(gate) ⊙ up → 원소 ff개마다 silu 1 + 곱 1
SmolLM2 층 하나, 토큰 1개:   ff = 1536 → 약 3k op   vs   GEMM 3·576·1536 = 2.65M MAC
```

비율로 1000분의 1 수준이라 MAC 세기에서 빼도 되지만, 이 연산이 느린 유닛으로 떨어지면 시간 몫은 훨씬 커질 수 있다(K 모듈 프로파일링).

### 2.10 Softmax

길이 n 벡터 하나에 대해: max(n−1 비교), 빼기 n, exp n, 합 n−1, 나누기 n(또는 역수 1 + 곱 n). **MAC 0, op 약 5n**. attention에서는 이것이 점수 행렬 T×T 전체에 걸리므로 h·T² 원소 × 약 5 op다. 예를 들어 h = 8, T = 128이면 131,072 원소 × 5 ≈ 0.66M op로, 같은 attention core의 8.4M MAC보다 작지만 exp가 비싸면 무시할 수 없다.

### 2.11 Attention — QKᵀ와 AV를 따로

head 하나를 끝까지 보면(B4 1절) GEMM이 두 번이다.

```
S = Q · Kᵀ   : [Tq × d_h] · [d_h × Tk]  → 출력 Tq·Tk 칸, 칸당 d_h   → Tq·Tk·d_h
O = P · V    : [Tq × Tk] · [Tk × d_h]   → 출력 Tq·d_h 칸, 칸당 Tk   → Tq·Tk·d_h
head h개:     MAC_attn = h · Tq · Tk · d_h · 2
```

말로 하면: attention core는 **파라미터가 0인데 MAC은 T²에 비례**한다. 이것이 긴 문맥에서 attention이 주인공이 되는 이유다(5.3절).

세 가지 변형을 짚는다.

- **causal mask**: 위 삼각형은 버려지는 점수다. 마스크를 "−∞를 채우는 것"으로 구현하면 MAC은 그대로 T²이다. FlashAttention류처럼 **마스크된 블록을 건너뛰는** 커널은 약 T(T+1)/2, 즉 절반 남짓만 한다. 도구(`FlopCounterMode`)와 내 카운터는 둘 다 full T²로 센다 — 보고할 때 어느 쪽인지 밝힌다.
- **GQA / MQA**: K/V head 수(kv)를 줄여도 **query head h개가 각자 T×T 점수를 만드는 것은 그대로**라 attention core MAC은 변하지 않는다. 줄어드는 것은 K/V 투영 MAC(2·d·kv·d_h)과 KV-cache 바이트다(B4 5절, D5).
- **decode (Tq = 1)**: 새 토큰 하나가 캐시의 Tk개 토큰과 점수를 만든다 → 층당 2·h·d_h·Tk MAC. 문맥이 길어질수록 토큰당 비용이 선형으로 는다.

손계산 (h = 8, d_h = 32, T = 128, full):

```
8 · 128 · 128 · 32 · 2 = 8,388,608 MAC
```

### 2.12 Transformer 층 전체 — GQA와 SwiGLU 포함

B4 10절은 MHA + 4d FFN에서 "토큰당 12d² + 2·T·d"를 유도했다. 요즘 edge LLM(Llama·Qwen·SmolLM 계열)은 GQA와 SwiGLU를 쓰므로 일반형으로 다시 쓴다. d = hidden, h = query head 수, kv = KV head 수, d_h = head 차원, ff = FFN 중간 차원, Tk = 이 토큰이 보는 토큰 수.

```
토큰 1개, 층 1개 MAC =
    d·(h·d_h)            Q 투영
  + 2 · d·(kv·d_h)       K, V 투영          ← GQA는 여기만 줄인다
  + (h·d_h)·d            O 투영
  + 3 · d·ff             SwiGLU: gate, up, down (GELU MLP면 2·d·ff)
  + 2 · h·d_h·Tk         attention core (QKᵀ + AV)
```

MHA(kv = h, h·d_h = d)이고 GELU MLP(ff = 4d)면 `d² + 2d² + d² + 8d² = 12d²`로 B4의 공식과 같아진다. 말로 하면: **가중치가 있는 항은 토큰당 상수(= 층 파라미터 수), attention 항만 문맥 길이에 비례**한다.

Llama-3.2-1B(d = 2048, h = 32, kv = 8, d_h = 64, ff = 8192)의 토큰당 값은 4.4절 드릴에서 계산한다: 투영 10.49M + FFN 50.33M + attention 4,096·Tk MAC.

### 2.13 LSTM · GRU — step당

B3의 게이트 식을 GEMM으로 보면: 게이트마다 W·x (H×I)와 U·h (H×H)가 하나씩.

```
LSTM step: 4 게이트 · H · (I + H)   MAC      (+ 원소별 sigmoid 3H, tanh 2H, 곱 3H 정도)
GRU  step: 3 게이트 · H · (I + H)   MAC
T step:    × T,   층이 여러 개면 2층부터는 I = H
예: LSTM I = 40, H = 64, T = 49 → 49 · 4 · 64 · 104 = 1,304,576 MAC
```

RNN의 특징은 **step 사이가 순차적**이라 T개를 한 GEMM으로 묶을 수 없다는 것이다(입력 투영 W·x만 T개를 묶을 수 있다). 그래서 MAC 수가 같아도 batch 1 GEMV를 T번 하게 되고 이용률이 낮다(D4).

### 2.14 Embedding lookup

토큰 id로 표 [V × d]에서 한 행을 복사한다. **MAC 0**, 읽기 d 원소. 하지만 파라미터는 V·d로 크다. 그래서 LLM에서 "파라미터 N개 → 2N FLOP/token" 규칙을 쓸 때 입력 embedding은 빼야 맞다. 대신 출력 쪽 `lm_head`(d → V GEMM)는 진짜 MAC이고, embedding과 가중치를 공유(tied)해도 **연산은 따로** 일어난다(5.1절).

### 2.15 Upsampling · resize · transposed conv

- nearest: 복사. MAC 0.
- bilinear: 출력 원소당 4개 읽고 가중 평균 → 곱 약 4 + 덧셈 3. MAC 카운터는 0으로 센다.
- **ConvTranspose2d**: 입력 원소 하나가 k×k×C_out 출력에 뿌려진다. 그래서 공식이 **입력 기준**이다: `H_in · W_in · C_in · C_out · k² / g`.

### 2.16 코드로 확인 — 공식 = 내 카운터 = FlopCounterMode

**2.2 표의 작은 예제들을 실제 PyTorch 레이어에 넣고, 손 공식·3절의 hook 카운터·`FlopCounterMode`(÷2) 세 값이 같은지 확인하는 코드**다.

```python
# 공식 시트의 작은 숫자 예제를 PyTorch 레이어 + maccount로 교차 확인
import torch, torch.nn as nn
from torch.utils.flop_counter import FlopCounterMode
from maccount import count_macs
import formulas as f
def chk(name, hand, mod, x):
    got = sum(count_macs(mod.eval(), x).values())
    with torch.no_grad(), FlopCounterMode(display=False) as fc: mod(x)
    ok = 'OK' if hand == got == fc.get_total_flops() // 2 else 'DIFF'
    print(f"{name:27s} hand {hand:>11,} counter {got:>11,} FlopCM/2 {fc.get_total_flops()//2:>11,} {ok}")
chk("Linear 4 tok 64->128", f.linear(4, 64, 128), nn.Linear(64, 128), torch.randn(1, 4, 64))
Ho = f.out_size(56, 3, 1, 1)
chk("conv 3x3 64->128 @56", f.conv2d(Ho, Ho, 64, 128, 3, 3), nn.Conv2d(64, 128, 3, 1, 1), torch.randn(1, 64, 56, 56))
chk("grouped g=4 64->128 @56", f.conv2d(Ho, Ho, 64, 128, 3, 3, 4), nn.Conv2d(64, 128, 3, 1, 1, groups=4), torch.randn(1, 64, 56, 56))
Ho2 = f.out_size(56, 3, 2, 1)
chk("depthwise 3x3 s2 C=64 @56", f.depthwise(Ho2, Ho2, 64, 3), nn.Conv2d(64, 64, 3, 2, 1, groups=64), torch.randn(1, 64, 56, 56))
chk("pointwise 64->128 @28", f.pointwise(28, 28, 64, 128), nn.Conv2d(64, 128, 1), torch.randn(1, 64, 28, 28))
To = f.out_size(128, 5, 1, 0, 2)
chk("conv1d k5 dil2 6->16 T=128", f.conv1d(To, 6, 16, 5), nn.Conv1d(6, 16, 5, dilation=2), torch.randn(1, 6, 128))
chk("LSTM I=40 H=64 T=49", 49 * f.rnn_step(4, 40, 64), nn.LSTM(40, 64, batch_first=True), torch.randn(1, 49, 40))
print("Ho(56,k3,s1,p1) =", Ho, " Ho(56,k3,s2,p1) =", Ho2, " To(128,k5,dil2) =", To)
```

```text
Linear 4 tok 64->128        hand      32,768 counter      32,768 FlopCM/2      32,768 OK
conv 3x3 64->128 @56        hand 231,211,008 counter 231,211,008 FlopCM/2 231,211,008 OK
grouped g=4 64->128 @56     hand  57,802,752 counter  57,802,752 FlopCM/2  57,802,752 OK
depthwise 3x3 s2 C=64 @56   hand     451,584 counter     451,584 FlopCM/2     451,584 OK
pointwise 64->128 @28       hand   6,422,528 counter   6,422,528 FlopCM/2   6,422,528 OK
conv1d k5 dil2 6->16 T=128  hand      57,600 counter      57,600 FlopCM/2      57,600 OK
LSTM I=40 H=64 T=49         hand   1,304,576 counter   1,304,576 FlopCM/2   1,304,576 OK
Ho(56,k3,s1,p1) = 56  Ho(56,k3,s2,p1) = 28  To(128,k5,dil2) = 120
```

출력에서 볼 것: 7종 모두 세 값이 정확히 같다. hook 카운터는 **실제 출력 shape**을 쓰므로 출력 크기 공식(`Ho = 56, 28`, `To = 120`)도 같이 검증된다. LSTM 1,304,576 = 49 × 4 × 64 × 104로 bias가 빠져 있다는 점도 FlopCounterMode와 같다.

**업샘플 계열을 FlopCounterMode에 넣어 "입력 기준" ConvTranspose 공식을 확인하는 코드**다.

```python
# 업샘플 3종의 연산량: nearest/bilinear는 MAC 0으로 세이고, ConvTranspose는 "입력 기준" 공식
import torch, torch.nn as nn
from torch.utils.flop_counter import FlopCounterMode
x = torch.randn(1, 32, 16, 16)
for name, mod in (("nearest x2", nn.Upsample(scale_factor=2)),
                  ("bilinear x2", nn.Upsample(scale_factor=2, mode="bilinear")),
                  ("ConvT k2 s2 32->16", nn.ConvTranspose2d(32, 16, 2, 2)),
                  ("ConvT k4 s2 p1 32->16", nn.ConvTranspose2d(32, 16, 4, 2, 1))):
    with torch.no_grad(), FlopCounterMode(display=False) as fc: y = mod(x)
    k = mod.kernel_size[0] if hasattr(mod, "kernel_size") else 0
    hand = 16 * 16 * 32 * 16 * k * k                    # H_in·W_in·C_in·C_out·k²
    print(f"{name:22s} out {tuple(y.shape[1:])}  FlopCM/2 {fc.get_total_flops()//2:>8,}  hand(입력 기준) {hand:>8,}")
```

```text
nearest x2             out (32, 32, 32)  FlopCM/2        0  hand(입력 기준)        0
bilinear x2            out (32, 32, 32)  FlopCM/2        0  hand(입력 기준)        0
ConvT k2 s2 32->16     out (16, 32, 32)  FlopCM/2  524,288  hand(입력 기준)  524,288
ConvT k4 s2 p1 32->16  out (16, 32, 32)  FlopCM/2 2,097,152  hand(입력 기준) 2,097,152
```

출력에서 볼 것: nearest·bilinear는 0으로 잡힌다(원소 연산은 안 센다). ConvTranspose는 출력이 32×32인데도 공식은 입력 16×16 기준이 맞다. k4 s2 p1은 padding이 출력에서 잘려 나가도 곱셈 자체는 다 일어난 것으로 센다.

---

## 3. 카운터 만들기 — 그리고 도구마다 무엇을 빠뜨리나

### 3.1 두 가지 방법

| 방법 | 원리 | 잘 잡는 것 | 놓치는 것 |
|---|---|---|---|
| module forward hook (B2 3.3, B6 3.2) | `nn.Conv2d`, `nn.Linear` 등 모듈이 끝날 때 출력 shape을 보고 공식 적용 | 모듈로 된 레이어, 레이어별 표 | 모듈이 아닌 연산 (`q @ k.T`, `F.scaled_dot_product_attention`) |
| `torch.utils.flop_counter.FlopCounterMode` | aten 연산(mm, addmm, bmm, convolution, SDPA 커널 일부)을 dispatch 단계에서 가로채 FLOP 공식 적용 | 모듈 여부와 무관한 모든 matmul·conv | 등록 안 된 커널(아래), 원소 연산, 정규화, softmax |

`FlopCounterMode`는 torch 2.8에 있다(`from torch.utils.flop_counter import FlopCounterMode`). 등록된 aten 연산 목록은 `flop_registry`에서 볼 수 있고, torch 2.8 CPU 빌드에서는 `mm, addmm, bmm, baddbmm, convolution, _scaled_mm`과 SDPA의 flash/efficient/cudnn 커널 등이 들어 있다. **행렬곱 [M,K]×[K,N]을 2·M·N·K로 센다** — 곱셈 1 + 덧셈 1을 2 FLOP으로 본 것이고, 그래서 이 노트에서는 늘 ÷2 해서 MAC으로 비교한다. bias 덧셈(`addmm`의 +b)은 세지 않는다.

### 3.2 hook + matmul 가로채기 카운터

B2의 hook 카운터를 확장해서, 모듈 hook(Conv·Linear·RNN)에 더해 `TorchFunctionMode`로 `torch.matmul`/`@`/`bmm`과 `F.scaled_dot_product_attention`을 가로챈다. `TorchFunctionMode`는 파이썬 수준에서 torch 함수 호출을 전부 한 번 거쳐 가게 하는 PyTorch 기능이다. 펌웨어로 치면 **함수 포인터 테이블에 트램폴린을 끼워 호출 횟수를 세는 것**과 같다.

**레이어 종류별로 MAC을 세는 모듈**이다 (이후 예제들이 import 한다).

```python
# maccount.py — 레이어 종류별 MAC 카운터 (module hook + matmul/SDPA 가로채기)
import torch, torch.nn as nn, torch.nn.functional as F
from collections import defaultdict
from torch.overrides import TorchFunctionMode

def _conv(m, x, y):   # 출력 원소 하나 = (C_in/groups)·k_h·k_w MAC
    k = 1
    for s in m.kernel_size: k *= s
    return y.numel() * (m.in_channels // m.groups) * k
def _linear(m, x, y): return y.numel() * m.in_features
def _rnn(m, x, y):    # step당 gates·H·(I+H), 층마다
    g = {"RNN": 1, "GRU": 3, "LSTM": 4}[type(m).__name__]
    x0 = x[0]; T = x0.shape[1] if m.batch_first else x0.shape[0]
    B = x0.numel() // (T * m.input_size); H = m.hidden_size
    return sum(B * T * g * H * ((m.input_size if l == 0 else H) + H) for l in range(m.num_layers))
RULES = {nn.Conv1d: _conv, nn.Conv2d: _conv, nn.Linear: _linear,
         nn.GRU: _rnn, nn.LSTM: _rnn, nn.RNN: _rnn}

class _MatmulMode(TorchFunctionMode):   # nn.Module이 아닌 matmul/attention
    def __init__(self, sink): super().__init__(); self.sink = sink
    def __torch_function__(self, func, types, args=(), kwargs=None):
        out = func(*args, **(kwargs or {}))
        if func in (torch.matmul, torch.Tensor.matmul, torch.Tensor.__matmul__, torch.bmm):
            self.sink["matmul"] += out.numel() * args[0].shape[-1]
        elif func is F.scaled_dot_product_attention:
            q, k, v = args[:3]           # [B,H,Tq,d] · [B,Hkv,Tk,d] (GQA면 Hkv<H)
            self.sink["sdpa"] += q.numel() // q.shape[-1] * k.shape[-2] * (q.shape[-1] + v.shape[-1])
        return out

def count_macs(model, *inputs, per_layer=False):
    tot, rows, hooks = defaultdict(int), [], []
    for name, m in model.named_modules():
        rule = RULES.get(type(m))
        if rule:
            def h(m, x, y, rule=rule, name=name):
                y0 = y[0] if isinstance(y, tuple) else y
                n = rule(m, x, y0); tot[type(m).__name__] += n; rows.append((name, n))
            hooks.append(m.register_forward_hook(h))
    with torch.no_grad(), _MatmulMode(tot):
        model(*inputs)
    for h in hooks: h.remove()
    return (dict(tot), rows) if per_layer else dict(tot)
```

설계 포인트:

- 공식은 2절과 같고, 크기는 hook이 받은 **실제 입출력 텐서**에서 읽는다. 그래서 출력 크기 계산 실수가 없다.
- SDPA는 `q.numel() / d_h × Tk × (d_h + d_v)` = `B·h·Tq·Tk·(d_h + d_v)`. GQA(K/V head가 적음)여도 q의 head 수로 센다 — 2.11절의 "GQA는 attention core MAC을 안 줄인다"가 그대로 코드에 들어 있다.
- causal은 full T²로 센다(FlopCounterMode와 같은 관례).
- 원소 연산(정규화·활성화·softmax·pool)은 세지 않는다. 필요하면 2.7~2.10의 op 수를 따로 더한다.

### 3.3 검증 1 — MobileNetV2

**MobileNetV2(torchvision, weights=None) 전체를 내 카운터와 FlopCounterMode로 세서 비교하는 코드**다.

```python
import torch, torch.nn as nn
from torch.utils.flop_counter import FlopCounterMode
from torchvision.models import mobilenet_v2
from maccount import count_macs
torch.manual_seed(0)
m = mobilenet_v2(weights=None).eval()
x = torch.randn(1, 3, 224, 224)
mine = count_macs(m, x)
with FlopCounterMode(display=False) as f: m(x)
print("mine   :", {k: f"{v/1e6:.2f}M" for k, v in mine.items()}, f"total {sum(mine.values())/1e6:.2f}M MAC")
print("FlopCM :", f"{f.get_total_flops()/1e6:.2f}M FLOP -> /2 = {f.get_total_flops()/2e6:.2f}M MAC")
print("params :", f"{sum(p.numel() for p in m.parameters())/1e6:.3f}M")
```

```text
mine   : {'Conv2d': '299.49M', 'Linear': '1.28M'} total 300.77M MAC
FlopCM : 601.55M FLOP -> /2 = 300.77M MAC
params : 3.505M
```

출력에서 볼 것: 300.77M MAC으로 두 도구가 일치하고, 논문의 "300M MAdds"와도 맞는다. Conv2d가 299.49M, 마지막 FC가 1.28M(1280 × 1000)이다. 파라미터 3.5M과 비교하면 MAC/파라미터 ≈ 86 — 가중치 하나를 평균 86번 재사용한다는 뜻이고, CNN이 compute-bound 쪽이라는 D3·D4 이야기의 숫자 근거다.

레이어 종류별로 나누면 어디에 MAC이 몰리는지 보인다. 내 카운터의 레이어별 출력을 블록별로 묶어 그렸다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="250.0" x2="660" y2="250.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/> <text x="54" y="254.0" font-size="12" text-anchor="end">0M</text> <line x1="60" y1="183.3" x2="660" y2="183.3" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/> <text x="54" y="187.3" font-size="12" text-anchor="end">10M</text> <line x1="60" y1="116.7" x2="660" y2="116.7" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/> <text x="54" y="120.7" font-size="12" text-anchor="end">20M</text> <line x1="60" y1="50.0" x2="660" y2="50.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/> <text x="54" y="54.0" font-size="12" text-anchor="end">30M</text>
<line x1="60" y1="250" x2="660" y2="250" stroke="currentColor"/> <line x1="60" y1="250" x2="60" y2="50" stroke="currentColor"/> <rect x="64.0" y="177.7" width="22.0" height="72.3" fill="#888"/> <text x="75.0" y="266" font-size="12" text-anchor="middle">0</text> <rect x="94.0" y="225.9" width="22.0" height="24.1" fill="#e08a3c"/> <rect x="94.0" y="183.1" width="22.0" height="42.8" fill="#4a7bd0"/> <text x="105.0" y="266" font-size="12" text-anchor="middle">1</text> <rect x="124.0" y="231.9" width="22.0" height="18.1" fill="#e08a3c"/> <rect x="124.0" y="55.3" width="22.0" height="176.6" fill="#4a7bd0"/> <text x="135.0" y="266" font-size="12" text-anchor="middle">2</text>
<rect x="154.0" y="222.9" width="22.0" height="27.1" fill="#e08a3c"/> <rect x="154.0" y="78.4" width="22.0" height="144.5" fill="#4a7bd0"/> <text x="165.0" y="266" font-size="12" text-anchor="middle">3</text> <rect x="184.0" y="243.2" width="22.0" height="6.8" fill="#e08a3c"/> <rect x="184.0" y="146.9" width="22.0" height="96.3" fill="#4a7bd0"/> <text x="195.0" y="266" font-size="12" text-anchor="middle">4</text> <rect x="214.0" y="241.0" width="22.0" height="9.0" fill="#e08a3c"/> <rect x="214.0" y="176.7" width="22.0" height="64.2" fill="#4a7bd0"/> <text x="225.0" y="266" font-size="12" text-anchor="middle">5</text> <rect x="244.0" y="241.0" width="22.0" height="9.0" fill="#e08a3c"/>
<rect x="244.0" y="176.7" width="22.0" height="64.2" fill="#4a7bd0"/> <text x="255.0" y="266" font-size="12" text-anchor="middle">6</text> <rect x="274.0" y="247.7" width="22.0" height="2.3" fill="#e08a3c"/> <rect x="274.0" y="199.6" width="22.0" height="48.2" fill="#4a7bd0"/> <text x="285.0" y="266" font-size="12" text-anchor="middle">7</text> <rect x="304.0" y="245.5" width="22.0" height="4.5" fill="#e08a3c"/> <rect x="304.0" y="181.3" width="22.0" height="64.2" fill="#4a7bd0"/> <text x="315.0" y="266" font-size="12" text-anchor="middle">8</text> <rect x="334.0" y="245.5" width="22.0" height="4.5" fill="#e08a3c"/> <rect x="334.0" y="181.3" width="22.0" height="64.2" fill="#4a7bd0"/>
<text x="345.0" y="266" font-size="12" text-anchor="middle">9</text> <rect x="364.0" y="245.5" width="22.0" height="4.5" fill="#e08a3c"/> <rect x="364.0" y="181.3" width="22.0" height="64.2" fill="#4a7bd0"/> <text x="375.0" y="266" font-size="12" text-anchor="middle">10</text> <rect x="394.0" y="245.5" width="22.0" height="4.5" fill="#e08a3c"/> <rect x="394.0" y="165.2" width="22.0" height="80.3" fill="#4a7bd0"/> <text x="405.0" y="266" font-size="12" text-anchor="middle">11</text> <rect x="424.0" y="243.2" width="22.0" height="6.8" fill="#e08a3c"/> <rect x="424.0" y="98.7" width="22.0" height="144.5" fill="#4a7bd0"/> <text x="435.0" y="266" font-size="12" text-anchor="middle">12</text>
<rect x="454.0" y="243.2" width="22.0" height="6.8" fill="#e08a3c"/> <rect x="454.0" y="98.7" width="22.0" height="144.5" fill="#4a7bd0"/> <text x="465.0" y="266" font-size="12" text-anchor="middle">13</text> <rect x="484.0" y="248.3" width="22.0" height="1.7" fill="#e08a3c"/> <rect x="484.0" y="145.9" width="22.0" height="102.4" fill="#4a7bd0"/> <text x="495.0" y="266" font-size="12" text-anchor="middle">14</text> <rect x="514.0" y="247.2" width="22.0" height="2.8" fill="#e08a3c"/> <rect x="514.0" y="146.8" width="22.0" height="100.4" fill="#4a7bd0"/> <text x="525.0" y="266" font-size="12" text-anchor="middle">15</text> <rect x="544.0" y="247.2" width="22.0" height="2.8" fill="#e08a3c"/>
<rect x="544.0" y="146.8" width="22.0" height="100.4" fill="#4a7bd0"/> <text x="555.0" y="266" font-size="12" text-anchor="middle">16</text> <rect x="574.0" y="247.2" width="22.0" height="2.8" fill="#e08a3c"/> <rect x="574.0" y="96.6" width="22.0" height="150.5" fill="#4a7bd0"/> <text x="585.0" y="266" font-size="12" text-anchor="middle">17</text> <rect x="604.0" y="116.2" width="22.0" height="133.8" fill="#4a7bd0"/> <text x="615.0" y="266" font-size="12" text-anchor="middle">18</text> <rect x="634.0" y="241.5" width="22.0" height="8.5" fill="#3f9a6b"/> <text x="645.0" y="266" font-size="12" text-anchor="middle">fc</text>
<text x="360.0" y="286" font-size="12" text-anchor="middle">features 블록 번호 (0 = stem, 18 = 마지막 1×1 conv), fc = classifier</text> <text x="360.0" y="22" font-size="13" text-anchor="middle">MobileNetV2 @224 블록별 MAC (합 300.77M, 내 카운터로 측정)</text> <rect x="70" y="34" width="12" height="12" fill="#4a7bd0"/> <text x="86" y="45" font-size="12">pointwise 89.1%</text> <rect x="220" y="34" width="12" height="12" fill="#e08a3c"/> <text x="236" y="45" font-size="12">depthwise 6.9%</text> <rect x="370" y="34" width="12" height="12" fill="#888"/> <text x="386" y="45" font-size="12">일반 conv 3.6%</text> <rect x="520" y="34" width="12" height="12" fill="#3f9a6b"/>
<text x="536" y="45" font-size="12">FC 0.4%</text>
</svg>
```

그림 3 — MobileNetV2 @224의 블록별 MAC(카운터 per_layer 출력을 블록 단위로 합산). **pointwise(1×1)가 89.1%**, depthwise는 6.9%, stem의 일반 3×3 conv 3.6%, FC 0.4%. "depthwise separable"이라는 이름과 달리 MAC의 주인은 1×1 conv다. 반대로 latency 측정에서는 depthwise 몫이 MAC 몫보다 커지는 경우가 많다(D4).

### 3.4 검증 2 — Transformer block, 그리고 FlopCounterMode가 attention을 놓치는 경우

**Llama식 decoder 층 하나를 정의하는 모듈**이다. RMSNorm, GQA, SwiGLU를 넣었고, attention을 SDPA로 할지 명시적 matmul로 할지 고를 수 있다.

```python
# block.py — Llama식 decoder layer 하나 (RMSNorm, GQA, SwiGLU). 가중치만 모양대로.
import torch, torch.nn as nn, torch.nn.functional as F
class Block(nn.Module):
    def __init__(s, d=256, h=8, kv=2, ff=688, manual=False):
        super().__init__(); s.h, s.kv, s.dh, s.manual = h, kv, d // h, manual
        s.n1, s.n2 = nn.RMSNorm(d), nn.RMSNorm(d)
        s.q = nn.Linear(d, h * s.dh, bias=False); s.k = nn.Linear(d, kv * s.dh, bias=False)
        s.v = nn.Linear(d, kv * s.dh, bias=False); s.o = nn.Linear(h * s.dh, d, bias=False)
        s.gate = nn.Linear(d, ff, bias=False); s.up = nn.Linear(d, ff, bias=False)
        s.down = nn.Linear(ff, d, bias=False)
    def forward(s, x):                                   # x: [B, T, d]
        B, T, _ = x.shape; a = s.n1(x)
        q = s.q(a).view(B, T, s.h, s.dh).transpose(1, 2)
        k = s.k(a).view(B, T, s.kv, s.dh).transpose(1, 2)
        v = s.v(a).view(B, T, s.kv, s.dh).transpose(1, 2)
        if s.manual:                                     # K/V head를 복제한 뒤 명시적 matmul
            k = k.repeat_interleave(s.h // s.kv, 1); v = v.repeat_interleave(s.h // s.kv, 1)
            att = (q @ k.transpose(-1, -2)) / s.dh ** 0.5
            att = att.masked_fill(torch.ones(T, T, dtype=torch.bool).triu(1), float("-inf"))
            y = att.softmax(-1) @ v
        else:
            y = F.scaled_dot_product_attention(q, k, v, is_causal=True, enable_gqa=True)
        x = x + s.o(y.transpose(1, 2).reshape(B, T, -1)); a = s.n2(x)
        return x + s.down(F.silu(s.gate(a)) * s.up(a))
```

**같은 층을 attention 경로 3가지(SDPA MHA, SDPA GQA, 명시적 matmul)로 돌려 세 카운트를 비교하는 코드**다.

```python
# Llama식 block 하나: 손계산 vs 내 카운터 vs FlopCounterMode (attention 경로 3가지)
import torch
from torch.utils.flop_counter import FlopCounterMode
from maccount import count_macs
from block import Block
import formulas as f
torch.manual_seed(0)
d, h, ff, T = 256, 8, 688, 128
x = torch.randn(1, T, d)
for kv, manual in ((8, False), (2, False), (2, True)):
    proj, att, ffn = f.llama_layer(T, d, h, kv, d // h, ff)
    blk = Block(d, h, kv, ff, manual).eval()
    mine = count_macs(blk, x)
    with torch.no_grad(), FlopCounterMode(display=False) as fc: blk(x)
    path = "matmul" if manual else ("SDPA GQA" if kv < h else "SDPA MHA")
    print(f"kv={kv} {path:8s} hand {(proj+att+ffn)/1e6:7.3f}M (attn {att/1e6:.3f}M) | "
          f"mine {sum(mine.values())/1e6:7.3f}M | FlopCM/2 {fc.get_total_flops()/2e6:7.3f}M")
```

```text
kv=8 SDPA MHA hand 109.576M (attn 8.389M) | mine 109.576M | FlopCM/2 101.188M
kv=2 SDPA GQA hand  96.993M (attn 8.389M) | mine  96.993M | FlopCM/2  96.993M
kv=2 matmul   hand  96.993M (attn 8.389M) | mine  96.993M | FlopCM/2  96.993M
```

출력에서 볼 것: 첫 줄에서 FlopCounterMode가 **8.389M MAC(attention core 전부)을 놓쳤다**. 원인을 추적하면, CPU에서 GQA가 아닌 SDPA는 `aten._scaled_dot_product_flash_attention_for_cpu`라는 커널로 가는데 이 커널이 torch 2.8의 `flop_registry`에 없다. `enable_gqa=True`인 둘째 줄은 CPU에서 math 경로로 떨어져 `bmm`으로 풀리기 때문에 잡힌다. 명시적 matmul(셋째 줄)은 당연히 잡힌다. 내 카운터는 Python 수준에서 `F.scaled_dot_product_attention` 호출 자체를 세므로 세 경우 모두 같다. 교훈: **도구의 숫자는 "그 도구가 아는 커널"의 합**이다. 같은 모델도 디바이스·백엔드·옵션에 따라 도구 출력이 바뀐다.

**원소 연산과 특수 레이어를 하나씩 넣어 FlopCounterMode가 무엇을 0으로 세는지 보는 코드**다.

```python
# FlopCounterMode가 세는 것과 무시하는 것 — 레이어 하나씩 넣어 보기
import torch, torch.nn as nn, torch.nn.functional as F
from torch.utils.flop_counter import FlopCounterMode
torch.manual_seed(0)
x4, x3 = torch.randn(1, 16, 32, 32), torch.randn(1, 64, 128)
cases = {
    "Embedding(1000,128)":  (nn.Embedding(1000, 128), torch.arange(64).unsqueeze(0)),
    "BatchNorm2d(16)":      (nn.BatchNorm2d(16).eval(), x4),
    "LayerNorm(128)":       (nn.LayerNorm(128), x3),
    "GELU":                 (nn.GELU(), x3),
    "Softmax(-1)":          (nn.Softmax(-1), x3),
    "MaxPool2d(2)":         (nn.MaxPool2d(2), x4),
    "Upsample x2 bilinear": (nn.Upsample(scale_factor=2, mode="bilinear"), x4),
    "SDPA MHA (CPU)":       (lambda q: F.scaled_dot_product_attention(q, q, q, is_causal=True), torch.randn(1, 4, 64, 32)),
    "SDPA GQA (CPU)":       (lambda q: F.scaled_dot_product_attention(q, q[:, :2], q[:, :2], is_causal=True, enable_gqa=True), torch.randn(1, 4, 64, 32)),
    "Linear(128,128)":      (nn.Linear(128, 128), x3),
}
for name, (mod, x) in cases.items():
    with torch.no_grad(), FlopCounterMode(display=False) as fc: mod(x)
    print(f"{name:22s} FLOP = {fc.get_total_flops():>9,}")
```

```text
Embedding(1000,128)    FLOP =         0
BatchNorm2d(16)        FLOP =         0
LayerNorm(128)         FLOP =         0
GELU                   FLOP =         0
Softmax(-1)            FLOP =         0
MaxPool2d(2)           FLOP =         0
Upsample x2 bilinear   FLOP =         0
SDPA MHA (CPU)         FLOP =         0
SDPA GQA (CPU)         FLOP = 2,097,152
Linear(128,128)        FLOP = 2,097,152
```

출력에서 볼 것: Embedding·BN·LN·GELU·Softmax·Pool·Upsample은 모두 0이다(설계상 원소 연산은 안 센다). SDPA는 MHA 경로면 0, GQA 경로면 2,097,152 FLOP(= 4 head × 64 × 64 × 32 × 2 GEMM × 2 FLOP, causal인데도 full). `Linear(128,128)`이 우연히 같은 2,097,152 FLOP(= 64 × 128 × 128 × 2)이다.

### 3.5 검증 3 — GRU

**2층 GRU KWS 모델(49 프레임 × 40 mel → GRU 64 → 12 class)을 손계산·내 카운터·FlopCounterMode로 비교하는 코드**다.

```python
import torch, torch.nn as nn
from torch.utils.flop_counter import FlopCounterMode
from maccount import count_macs
torch.manual_seed(0)
class KWSGRU(nn.Module):                       # 49 frames × 40 mel → GRU 64 → 12 classes
    def __init__(s): super().__init__(); s.gru = nn.GRU(40, 64, num_layers=2, batch_first=True); s.fc = nn.Linear(64, 12)
    def forward(s, x): y, _ = s.gru(x); return s.fc(y[:, -1])
m = KWSGRU().eval(); x = torch.randn(1, 49, 40)
hand = 49 * 3 * 64 * ((40 + 64) + (64 + 64)) + 64 * 12
mine = count_macs(m, x)
with FlopCounterMode(display=False) as f: m(x)
print("hand  :", hand)
print("mine  :", mine, sum(mine.values()))
print("FlopCM:", f.get_total_flops() // 2, "(FLOP/2)")
print("params:", sum(p.numel() for p in m.parameters()))
```

```text
hand  : 2183424
mine  : {'GRU': 2182656, 'Linear': 768} 2183424
FlopCM: 2183424 (FLOP/2)
params: 46092
```

출력에서 볼 것: 셋 다 2,183,424 MAC. 손계산은 `49 × 3 × 64 × ((40+64) + (64+64)) + 64 × 12`다. CPU의 `nn.GRU`는 내부적으로 `addmm`으로 풀리기 때문에 FlopCounterMode가 잡는다(bias와 게이트 원소 연산은 빠짐). 파라미터 46,092개에 MAC 2.18M — 가중치 하나를 49 step 동안 재사용하지만 step마다 GEMV라 D4에서 보듯 이용률은 낮다.

### 3.6 도구 비교 요약

| 항목 | 손 공식 (2절) | 내 hook + matmul 카운터 | FlopCounterMode (torch 2.8) |
|---|---|---|---|
| 단위 | MAC | MAC | FLOP (= 2 × MAC) |
| Conv / Linear / GRU / LSTM | O | O | O (aten으로 풀리는 경우) |
| 모듈 아닌 `@`, `bmm` | O | O | O |
| SDPA | O | O (함수 호출 수준) | 커널에 따라 0 (CPU MHA) |
| causal 절약 | 선택 | full T² | full T² |
| 원소 연산·정규화·softmax | 따로 셈 | X | X |
| bias 덧셈 | 보통 무시 | X | X |
| Embedding | 0 | X (0) | 0 |
| 레이어별 표 | 손으로 | O (per_layer) | 모듈별 dict (`get_flop_counts()`) |

---

## 4. 모델 전체 손계산 드릴

각 드릴은 **손으로 먼저 풀고**, 코드로 확인한다. 화이트보드에서는 표를 그리고 레이어마다 "출력 shape → 칸당 K → 곱"을 한 줄씩 채우면 된다.

### 4.1 드릴 (a) — KWS DS-CNN, 입력 49×10

설정: Hello Edge(Zhang et al. 2017)의 DS-CNN-S와 같은 모양. MFCC 49 프레임 × 10 계수, conv1 64채널 kernel 10×4 stride 2×2, 그 뒤 DS 블록 4개(64채널, 3×3), GAP, FC 12 class. padding은 conv1이 (5, 1), 나머지는 same.

손풀이:

```
conv1 출력: H = ⌊(49 + 10 − 10)/2⌋ + 1 = 25,  W = ⌊(10 + 2 − 4)/2⌋ + 1 = 5  → 25×5×64 = 8,000 원소
conv1     : 8,000 원소 × (1 · 10 · 4 = 40)       =   320,000
DS 블록 1개:
  depthwise 3×3 : 8,000 × 9                      =    72,000
  pointwise 64→64: 8,000 × 64                    =   512,000
  블록 합                                         =   584,000
DS × 4                                            = 2,336,000
FC 64 → 12                                        =       768
합계                                              = 2,656,768 MAC ≈ 2.66M MAC ≈ 5.3M FLOP
```

말로 하면: 해상도(25×5)가 처음부터 끝까지 그대로라 **모든 층의 출력 원소가 8,000개**다. 그러면 층별 MAC은 "8,000 × 칸당 K"로 한 줄이다. pointwise가 전체의 77%다.

**같은 모양의 모델을 PyTorch로 만들어 층별 MAC을 세는 코드**다.

```python
# 드릴 (a) KWS DS-CNN (Hello Edge DS-CNN-S 모양): 49×10 MFCC → 12 class
import torch, torch.nn as nn
from maccount import count_macs
def ds(c): return nn.Sequential(nn.Conv2d(c, c, 3, 1, 1, groups=c, bias=False), nn.BatchNorm2d(c), nn.ReLU(),
                                nn.Conv2d(c, c, 1, bias=False), nn.BatchNorm2d(c), nn.ReLU())
net = nn.Sequential(nn.Conv2d(1, 64, (10, 4), (2, 2), (5, 1), bias=False), nn.BatchNorm2d(64), nn.ReLU(),
                    ds(64), ds(64), ds(64), ds(64),
                    nn.AdaptiveAvgPool2d(1), nn.Flatten(), nn.Linear(64, 12)).eval()
x = torch.randn(1, 1, 49, 10)
print("conv1 out:", tuple(net[:3](x).shape))
tot, rows = count_macs(net, x, per_layer=True)
for name, n in rows: print(f"  {name:6s} {n:>9,}")
print(f"total {sum(tot.values()):,} MAC, params {sum(p.numel() for p in net.parameters()):,}")
```

```text
conv1 out: (1, 64, 25, 5)
  0        320,000
  3.0       72,000
  3.3      512,000
  4.0       72,000
  4.3      512,000
  5.0       72,000
  5.3      512,000
  6.0       72,000
  6.3      512,000
  9            768
total 2,656,768 MAC, params 23,180
```

출력에서 볼 것: 층별 값이 손풀이 표와 한 줄씩 같다. 파라미터 23,180개(BN 포함, BN은 folding하면 사라짐). 원소 연산을 더하면 ReLU 9층 × 8,000 = 72,000 비교, GAP 8,000 덧셈 정도로 MAC의 3% 수준이다.

### 4.2 드릴 (b) — MobileNetV2 stem + 앞 3개 블록, 입력 96×96

설정: torchvision MobileNetV2의 `features[0:4]`. 96×96은 Visual Wake Words 같은 MCU 사람 감지 과제에서 흔한 크기다.

손풀이 (출력 shape → 칸당 K):

```
stem  3×3 s2, 3→32      : 48·48·32 = 73,728 원소 × 27     = 1,990,656
block1 (t=1)
  dw 3×3, 32             : 48·48·32 × 9                    =   663,552
  pw 32→16               : 48·48·16 × 32                   = 1,179,648
block2 (t=6, stride 2)
  pw 확장 16→96          : 48·48·96 × 16                   = 3,538,944
  dw 3×3 s2, 96          : 24·24·96 × 9                    =   497,664
  pw 투영 96→24          : 24·24·24 × 96                   = 1,327,104
block3 (t=6, stride 1)
  pw 확장 24→144         : 24·24·144 × 24                  = 1,990,656
  dw 3×3, 144            : 24·24·144 × 9                   =   746,496
  pw 투영 144→24         : 24·24·24 × 144                  = 1,990,656
합계                                                       = 13,925,376 MAC
```

**torchvision 구조 그대로 층별 출력 shape과 MAC을 출력하는 코드**다.

```python
# 드릴 (b) MobileNetV2 stem + 앞 3개 inverted residual block @ 96×96
import torch
from torchvision.models import mobilenet_v2
from maccount import count_macs
head = mobilenet_v2(weights=None).features[:4].eval()
x = torch.randn(1, 3, 96, 96)
shapes = {}
hooks = [m.register_forward_hook(lambda m, i, o, n=n: shapes.__setitem__(n, tuple(o.shape[1:])))
         for n, m in head.named_modules() if isinstance(m, torch.nn.Conv2d)]
tot, rows = count_macs(head, x, per_layer=True)
for name, n in rows:
    m = head.get_submodule(name)
    kind = "dw" if m.groups > 1 else ("pw" if m.kernel_size == (1, 1) else "std")
    print(f"{name:10s} {kind:3s} k{m.kernel_size[0]} s{m.stride[0]} out {str(shapes[name]):15s} {n:>10,}")
print(f"total {sum(tot.values()):,} MAC  (전체 모델 @96 = 56,289,152)")
```

```text
0.0        std k3 s2 out (32, 48, 48)     1,990,656
1.conv.0.0 dw  k3 s1 out (32, 48, 48)       663,552
1.conv.1   pw  k1 s1 out (16, 48, 48)     1,179,648
2.conv.0.0 pw  k1 s1 out (96, 48, 48)     3,538,944
2.conv.1.0 dw  k3 s2 out (96, 24, 24)       497,664
2.conv.2   pw  k1 s1 out (24, 24, 24)     1,327,104
3.conv.0.0 pw  k1 s1 out (144, 24, 24)    1,990,656
3.conv.1.0 dw  k3 s1 out (144, 24, 24)      746,496
3.conv.2   pw  k1 s1 out (24, 24, 24)     1,990,656
total 13,925,376 MAC  (전체 모델 @96 = 56,289,152)
```

출력에서 볼 것: 13.93M MAC으로 손풀이와 같다. 앞 4개 features가 전체 56.29M의 **25%**다. 해상도가 큰 앞쪽 층이 MAC을 많이 먹는 것은 CNN의 일반적 모양이고, 동시에 activation 메모리 peak도 앞쪽에서 생긴다(D2, C7 7절 MCUNet). block2의 확장 pw(3.54M)가 이 구간의 최대 — 48×48 해상도에서 채널을 6배로 불리기 때문이다.

### 4.3 드릴 (c) — IMU 1D-CNN

설정: 예를 들어 Hark 같은 웨어러블의 손목 제스처 인식(추정 시나리오). 6축 × 128 샘플(50 Hz면 2.56 s) → Conv1d 6→16 k5 → MaxPool 2 → Conv1d 16→32 k5 → MaxPool 2 → Conv1d 32→64 k3 → GAP → FC 64→8.

손풀이:

```
conv1: To = 128 (same) → 128 · 16 · 6 · 5    =  61,440
pool → 64
conv2: 64 · 32 · 16 · 5                      = 163,840
pool → 32
conv3: 32 · 64 · 32 · 3                      = 196,608
FC   : 64 · 8                                =     512
합계                                          = 422,400 MAC
```

**같은 모델을 세는 코드**다.

```python
# 드릴 (c) IMU 1D-CNN: 6축 × 128 샘플 → 8 제스처
import torch, torch.nn as nn
from maccount import count_macs
net = nn.Sequential(
    nn.Conv1d(6, 16, 5, padding=2), nn.ReLU(), nn.MaxPool1d(2),     # 128 → 64
    nn.Conv1d(16, 32, 5, padding=2), nn.ReLU(), nn.MaxPool1d(2),    # 64 → 32
    nn.Conv1d(32, 64, 3, padding=1), nn.ReLU(),                     # 32
    nn.AdaptiveAvgPool1d(1), nn.Flatten(), nn.Linear(64, 8)).eval()
tot, rows = count_macs(net, torch.randn(1, 6, 128), per_layer=True)
for name, n in rows: print(f"layer {name:2s} {n:>8,}")
print(f"total {sum(tot.values()):,} MAC, params {sum(p.numel() for p in net.parameters()):,}")
```

```text
layer 0    61,440
layer 3   163,840
layer 6   196,608
layer 10      512
total 422,400 MAC, params 9,816
```

출력에서 볼 것: 422,400 MAC, 파라미터 9,816개. 창 하나(2.56 s)에 0.42M MAC이면 50% 겹치는 창을 1.28 s마다 돌려도 초당 0.33M MAC이다. Cortex-M4급에서도 1% 미만의 CPU로 충분한 크기다(8절). IMU 모델은 **연산이 아니라 센서 전력과 항상 켜진 시간**이 예산을 정한다(D7).

### 4.4 드릴 (d) — Llama-3.2-1B 층 하나: decode(T=1) vs prefill(T=512)

설정: config를 로컬 캐시에서 읽는다(`unsloth/Llama-3.2-1B`). d = 2048, h = 32, kv = 8, d_h = 64, ff = 8192, 16층, vocab 128,256.

손풀이 (토큰 1개, 층 1개):

```
Q    : 2048 · 2048              =  4,194,304
K, V : 2 · 2048 · 512           =  2,097,152      (kv·d_h = 8·64 = 512)
O    : 2048 · 2048              =  4,194,304
proj 합                          = 10,485,760     ≈ 10.49M
FFN  : 3 · 2048 · 8192          = 50,331,648     ≈ 50.33M
attn : 2 · 32 · 64 · Tk         =  4,096 · Tk

decode  (T = 1, 캐시 Tk = 512): 10.49M + 50.33M + 2.10M = 62.9M MAC  = 0.126 GFLOP
prefill (T = 512, full 512×512): 512 · 60.82M + 32 · 512² · 64 · 2
                               = 31.14G + 1.07G = 32.21 GMAC = 64.42 GFLOP
```

말로 하면: prefill은 decode 한 번의 **정확히 512배**다(full T² 관례에서는 prefill 토큰 하나하나가 Tk = 512를 본다고 센 셈이다). causal로 건너뛰는 커널이면 attention 항이 절반 남짓(약 0.54G)으로 준다. 어느 쪽이든 T = 512에서 attention은 3%다 — **이 크기의 LLM에서 짧은 문맥의 FLOP은 거의 전부 가중치 GEMM**이다.

**config를 읽어 공식으로 계산하고, 같은 모양의 층(랜덤 가중치)으로 prefill을 FlopCounterMode로 확인하는 코드**다.

```python
# 드릴 (d) Llama-3.2-1B 층 하나: decode(T=1, 캐시 512) vs prefill(T=512)
import torch, warnings; warnings.filterwarnings("ignore")
from transformers import AutoConfig
from torch.utils.flop_counter import FlopCounterMode
from block import Block
import formulas as f
c = AutoConfig.from_pretrained("unsloth/Llama-3.2-1B", local_files_only=True)
d, h, kv, dh, ff = c.hidden_size, c.num_attention_heads, c.num_key_value_heads, c.head_dim, c.intermediate_size
print(f"config: d={d} h={h} kv={kv} dh={dh} ff={ff} layers={c.num_hidden_layers} vocab={c.vocab_size}")
for label, T, Tk in (("decode ", 1, 512), ("prefill", 512, 512)):
    proj, att, ffn = f.llama_layer(T, d, h, kv, dh, ff, Tk)
    tot = proj + att + ffn
    print(f"{label} T={T:3d}: proj {proj/1e6:9.2f}M  attn {att/1e6:8.2f}M  ffn {ffn/1e6:9.2f}M"
          f"  = {tot/1e9:.4f} GMAC = {2*tot/1e9:.4f} GFLOP  (attn {att/tot:.1%})")
blk = Block(d, h, kv, ff, manual=True).eval()          # 같은 모양, 랜덤 가중치
with torch.no_grad(), FlopCounterMode(display=False) as fc: blk(torch.randn(1, 512, d))
print(f"FlopCounterMode prefill T=512: {fc.get_total_flops()/1e9:.4f} GFLOP")
```

```text
config: d=2048 h=32 kv=8 dh=64 ff=8192 layers=16 vocab=128256
decode  T=  1: proj     10.49M  attn     2.10M  ffn     50.33M  = 0.0629 GMAC = 0.1258 GFLOP  (attn 3.3%)
prefill T=512: proj   5368.71M  attn  1073.74M  ffn  25769.80M  = 32.2123 GMAC = 64.4245 GFLOP  (attn 3.3%)
FlopCounterMode prefill T=512: 64.4245 GFLOP
```

출력에서 볼 것: prefill 64.4245 GFLOP이 공식과 소수 넷째 자리까지 같다. decode의 MAC 0.063G는 작지만, 그 0.063G를 위해 **층 가중치 약 60.8M개(bf16이면 약 122 MB)를 전부 읽어야** 한다. 연산 0.126 GFLOP ÷ 122 MB ≈ 1 FLOP/byte — decode가 memory-bound인 이유다(D3, D5). 전체 모델로 넓히면 16층 × 60.82M + lm_head 2048 × 128,256 = 262.7M → 토큰당 약 **1.236 GMAC ≈ 2.47 GFLOP**(+ 층당 4,096·Tk).

### 4.5 드릴 (e) — SmolLM2-135M 전체, "2 · N" 규칙을 실제 모델로 검증

설정: 로컬 캐시의 `HuggingFaceTB/SmolLM2-135M-Instruct` 실제 가중치. d = 576, h = 9, kv = 3, d_h = 64, ff = 1536, 30층, vocab 49,152, embedding과 lm_head 공유(tied).

손풀이 (토큰 1개):

```
층 하나: Q 576·576 + K,V 2·576·192 + O 576·576 + FFN 3·576·1536
       = 331,776 + 221,184 + 331,776 + 2,654,208 = 3,538,944
30층                                             = 106,168,320
lm_head 576 · 49,152                            =  28,311,552
토큰당 선형 합                                    = 134,479,872 MAC ≈ 0.269 GFLOP
파라미터 N                                        = 134,515,008 (norm 가중치 35,136개 포함)
→ 토큰당 FLOP ≈ 2 · N  (차이는 norm 가중치뿐)
attention: 30 · 9 · 64 · 2 · T 칸 per 토큰 = 34,560 · T MAC  (T=256이면 8.8M, 선형의 6.6%)
```

**실제 모델을 T = 8과 T = 256으로 돌려 2·N·T, 손계산, FlopCounterMode, 내 카운터를 비교하는 코드**다.

```python
# 드릴 (e) SmolLM2-135M 전체 forward: 2·N·T 규칙 vs 손계산 vs FlopCounterMode (실제 가중치)
import torch, warnings; warnings.filterwarnings("ignore")
from transformers import AutoModelForCausalLM
from torch.utils.flop_counter import FlopCounterMode
from maccount import count_macs
import formulas as f
m = AutoModelForCausalLM.from_pretrained("HuggingFaceTB/SmolLM2-135M-Instruct", local_files_only=True).eval()
c = m.config; d, V, L = c.hidden_size, c.vocab_size, c.num_hidden_layers
N = sum(p.numel() for p in m.parameters())               # tied: embedding = lm_head 한 벌
print(f"N = {N:,}  embedding {V*d:,} ({V*d/N:.1%})")
for T in (8, 256):
    ids = torch.randint(0, V, (1, T), generator=torch.Generator().manual_seed(0))
    with torch.no_grad(), FlopCounterMode(display=False) as fc: m(ids)
    mine = count_macs(m, ids)
    proj, att, ffn = f.llama_layer(T, d, c.num_attention_heads, c.num_key_value_heads, 64, c.intermediate_size)
    hand = L * (proj + att + ffn) + T * d * V
    print(f"T={T:3d}: 2·N·T {2*N*T/1e9:7.3f} | hand {2*hand/1e9:7.3f} | FlopCM {fc.get_total_flops()/1e9:7.3f} GFLOP"
          f" | mine: Linear {2*mine['Linear']/1e9:.3f} + sdpa {2*mine['sdpa']/1e9:.3f}")
```

```text
N = 134,515,008  embedding 28,311,552 (21.0%)
T=  8: 2·N·T   2.152 | hand   2.156 | FlopCM   2.156 GFLOP | mine: Linear 2.152 + sdpa 0.004
T=256: 2·N·T  68.872 | hand  73.384 | FlopCM  73.384 GFLOP | mine: Linear 68.854 + sdpa 4.530
```

출력에서 볼 것:

- T = 8: 2·N·T = 2.152, 실제 2.156 GFLOP. 차이 0.004는 attention core다. **짧은 입력에서 "토큰당 2N FLOP"은 0.2% 안으로 맞는다.**
- T = 256: 2·N·T = 68.87인데 실제는 73.38 GFLOP. attention이 4.53 GFLOP(6.6%)을 더한다. 문맥이 길수록 2N 규칙은 **과소평가**한다.
- 내 카운터의 Linear 합 68.854는 2·N·T 68.872보다 살짝 작다. N에 들어 있는 norm 가중치(곱셈 GEMM이 아님)만큼이다.
- 이 모델은 HF의 SDPA 경로가 `enable_gqa=True`로 호출돼 CPU에서 `bmm`으로 풀리므로 FlopCounterMode가 attention도 잡았다(3.4절과 대조).

---

## 5. 경험칙 — 외워서 바로 쓰는 것

### 5.1 LLM: 토큰당 forward ≈ 2·N FLOP — 그런데 N이 무엇인가

```
forward FLOP / token ≈ 2 · N_matmul + 2 · n_layer · h · d_h · Tk · 2
                       └ 가중치 GEMM ┘   └ attention core (문맥 길이에 비례) ┘
```

N_matmul은 "행렬곱에 실제로 쓰이는 가중치 수"다. 뉘앙스 세 가지:

- **입력 embedding은 빼야 한다.** lookup이라 MAC이 0이다(2.14절).
- **lm_head는 넣어야 한다.** tied 모델(SmolLM2, Llama-3.2-1B)은 embedding 표가 lm_head로 한 번 더 쓰이므로 "전체 N"이 우연히 거의 맞는다(4.5절: 0.2% 차이). untied 모델은 N_total에서 입력 embedding만큼 빼야 한다.
- Kaplan et al. 2020(Scaling Laws)의 표는 N을 **embedding을 뺀** 파라미터로 정의하고 C_forward ≈ 2N + 2·n_layer·n_ctx·d_attn로 쓴다. 큰 모델에서는 embedding·lm_head 몫이 작아 문제없지만, **작은 edge LLM은 embedding이 전체의 20% 이상**이라 차이가 크다. SmolLM2-135M에 "embedding 뺀 N = 106.2M"을 넣으면 토큰당 0.212 GFLOP으로 실제 0.269의 79%밖에 안 나온다 — lm_head 28.3M MAC을 빠뜨린 탓이다.

말로 하면: **"2 × (곱셈에 쓰이는 가중치 수) + 문맥 비례 항"**이 정확한 규칙이고, 2·N은 그 짧은 문맥 근사다. 학습은 forward + backward ≈ 6·N FLOP/token이라는 짝 규칙도 같이 기억해 두면 좋다(backward가 forward의 약 2배).

### 5.2 CNN: MAC ∝ H·W — 단, stride 누적의 배수에서만 정확

conv 층의 MAC은 출력 원소 수에 비례하고, 출력 원소 수는 입력 해상도의 제곱에 비례한다. B6 3.4절에서 유도했다. 여기서는 "얼마나 정확한가"를 실측한다.

**MobileNetV2를 64~256 해상도로 세서 conv MAC/r²이 상수인지 보는 코드**다.

```python
# MobileNetV2 MACs vs 입력 해상도 — 제곱 법칙 확인
import torch
from torchvision.models import mobilenet_v2
from maccount import count_macs
m = mobilenet_v2(weights=None).eval()
base = None
for r in (64, 96, 128, 160, 192, 224, 256):
    tot = count_macs(m, torch.randn(1, 3, r, r))
    conv, fc = tot["Conv2d"], tot["Linear"]
    base = base or conv / 64**2
    print(f"{r:3d}: conv {conv/1e6:7.2f}M + fc {fc/1e6:.2f}M = {(conv+fc)/1e6:7.2f}M"
          f" | conv/r² = {conv/r**2:,.0f} | (r/64)² 예측 {base*r*r/1e6:7.2f}M")
```

```text
 64: conv   24.45M + fc 1.28M =   25.73M | conv/r² = 5,969 | (r/64)² 예측   24.45M
 96: conv   55.01M + fc 1.28M =   56.29M | conv/r² = 5,969 | (r/64)² 예측   55.01M
128: conv   97.79M + fc 1.28M =   99.07M | conv/r² = 5,969 | (r/64)² 예측   97.79M
160: conv  152.80M + fc 1.28M =  154.08M | conv/r² = 5,969 | (r/64)² 예측  152.80M
192: conv  220.04M + fc 1.28M =  221.32M | conv/r² = 5,969 | (r/64)² 예측  220.04M
224: conv  299.49M + fc 1.28M =  300.77M | conv/r² = 5,969 | (r/64)² 예측  299.49M
256: conv  391.18M + fc 1.28M =  392.46M | conv/r² = 5,969 | (r/64)² 예측  391.18M
```

출력에서 볼 것: 32의 배수인 해상도에서는 conv MAC/r² = 5,969로 정확히 상수다. FC(1.28M)는 GAP 뒤라 해상도와 무관하다. 그래서 작은 해상도일수록 FC 몫이 커진다(224에서 0.4% → 64에서 5%).

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<polyline points="70.0,256.1 90.0,254.3 110.0,252.1 130.0,249.5 150.0,246.5 170.0,243.1 190.0,239.3 210.0,235.1 230.0,230.4 250.0,225.4 270.0,220.0 290.0,214.2 310.0,208.0 330.0,201.4 350.0,194.3 370.0,186.9 390.0,179.1 410.0,170.9 430.0,162.3 450.0,153.2 470.0,143.8 490.0,134.0 510.0,123.8 530.0,113.1 550.0,102.1 570.0,90.7 590.0,78.8 610.0,66.6 630.0,54.0" fill="none" stroke="#4a7bd0" stroke-width="2"/> <polyline points="70.0,237.4 630.0,79.5" fill="none" stroke="#888" stroke-width="1.5" stroke-dasharray="5 4"/> <circle cx="150.0" cy="246.5" r="4" fill="#e08a3c"/> <text x="144.0" y="238.5" font-size="12" text-anchor="end">26M</text> <circle cx="230.0" cy="230.4" r="4" fill="#e08a3c"/>
<text x="224.0" y="222.4" font-size="12" text-anchor="end">56M</text> <circle cx="310.0" cy="208.0" r="4" fill="#e08a3c"/> <text x="304.0" y="200.0" font-size="12" text-anchor="end">99M</text> <circle cx="390.0" cy="179.1" r="4" fill="#e08a3c"/> <text x="384.0" y="171.1" font-size="12" text-anchor="end">154M</text> <circle cx="470.0" cy="143.8" r="4" fill="#e08a3c"/> <text x="464.0" y="135.8" font-size="12" text-anchor="end">221M</text> <circle cx="550.0" cy="102.1" r="4" fill="#e08a3c"/> <text x="544.0" y="94.1" font-size="12" text-anchor="end">301M</text> <circle cx="630.0" cy="54.0" r="4" fill="#e08a3c"/> <text x="624.0" y="46.0" font-size="12" text-anchor="end">392M</text>
<line x1="70" y1="260" x2="630" y2="260" stroke="currentColor"/> <line x1="70" y1="260" x2="70" y2="50" stroke="currentColor"/> <text x="64" y="264.0" font-size="12" text-anchor="end">0M</text> <text x="64" y="211.5" font-size="12" text-anchor="end">100M</text> <text x="64" y="159.0" font-size="12" text-anchor="end">200M</text> <text x="64" y="106.5" font-size="12" text-anchor="end">300M</text> <text x="64" y="54.0" font-size="12" text-anchor="end">400M</text> <text x="70.0" y="278" font-size="12" text-anchor="middle">32</text> <text x="150.0" y="278" font-size="12" text-anchor="middle">64</text> <text x="230.0" y="278" font-size="12" text-anchor="middle">96</text>
<text x="310.0" y="278" font-size="12" text-anchor="middle">128</text> <text x="390.0" y="278" font-size="12" text-anchor="middle">160</text> <text x="470.0" y="278" font-size="12" text-anchor="middle">192</text> <text x="550.0" y="278" font-size="12" text-anchor="middle">224</text> <text x="630.0" y="278" font-size="12" text-anchor="middle">256</text> <text x="350.0" y="300" font-size="12" text-anchor="middle">입력 해상도 r (r×r)</text> <text x="350.0" y="24" font-size="13" text-anchor="middle">MobileNetV2 MAC vs 해상도: 측정값(점)과 5,969·r² + 1.28M(실선)</text> <line x1="90" y1="44" x2="112" y2="44" stroke="#888" stroke-width="1.5" stroke-dasharray="5 4"/>
<text x="116" y="48" font-size="12">만약 선형이었다면 (r에 비례)</text>
</svg>
```

그림 4 — MobileNetV2 MAC vs 입력 해상도. 주황 점은 카운터로 센 값, 파란 실선은 5,969·r² + 1.28M, 회색 점선은 "선형이었다면". 해상도를 224 → 96으로 줄이면 MAC은 (96/224)² ≈ 0.18배가 된다.

함정: **32의 배수가 아니면 제곱 법칙이 어긋난다.** 112×112를 넣으면 MobileNetV2의 마지막 단계 feature map이 3.5×3.5가 아니라 올림으로 4×4가 되어, 제곱 법칙 예측 76.2M 대신 실제 **82.18M**이 나온다(7절 Q6에서 실행). 해상도를 바꿀 때는 네트워크 전체 stride(보통 32)의 배수로 고르는 것이 MAC·메모리 모두에 좋다.

### 5.3 Transformer: attention vs FFN 몫은 T에 따라 바뀐다

2.12절 공식에서 attention core 몫이 선형 항과 같아지는 교차점은

```
T⋆ = (proj + ffn per token) / (2 · h · d_h)
```

B4 10절은 Llama-3.2-1B 기준으로 T² 항 비율을 그렸다. 여기서는 d가 작은 SmolLM2와 나란히 놓고 **세 덩어리(proj, attention core, FFN)의 몫**을 본다.

**두 모델의 층 하나에서 T별 MAC 구성비를 계산하는 코드**다.

```python
# 층 하나의 prefill MAC 중 proj / attention core / FFN 비율 vs T (full T×T 기준)
import formulas as f
models = {"SmolLM2-135M": (576, 9, 3, 64, 1536), "Llama-3.2-1B": (2048, 32, 8, 64, 8192)}
for name, (d, h, kv, dh, ff) in models.items():
    p1, _, f1 = f.llama_layer(1, d, h, kv, dh, ff)
    print(f"{name}: 교차점 T* = (proj+ffn)/(2·h·dh) = {(p1 + f1) / (2 * h * dh):,.0f}")
    for T in (128, 512, 2048, 8192, 32768):
        p, a, fn = f.llama_layer(T, d, h, kv, dh, ff); s = p + a + fn
        print(f"  T={T:5d}: proj {p/s:5.1%}  attn {a/s:5.1%}  ffn {fn/s:5.1%}")
```

```text
SmolLM2-135M: 교차점 T* = (proj+ffn)/(2·h·dh) = 3,072
  T=  128: proj 24.0%  attn  4.0%  ffn 72.0%
  T=  512: proj 21.4%  attn 14.3%  ffn 64.3%
  T= 2048: proj 15.0%  attn 40.0%  ffn 45.0%
  T= 8192: proj  6.8%  attn 72.7%  ffn 20.5%
  T=32768: proj  2.1%  attn 91.4%  ffn  6.4%
Llama-3.2-1B: 교차점 T* = (proj+ffn)/(2·h·dh) = 14,848
  T=  128: proj 17.1%  attn  0.9%  ffn 82.1%
  T=  512: proj 16.7%  attn  3.3%  ffn 80.0%
  T= 2048: proj 15.2%  attn 12.1%  ffn 72.7%
  T= 8192: proj 11.1%  attn 35.6%  ffn 53.3%
  T=32768: proj  5.4%  attn 68.8%  ffn 25.8%
```

출력에서 볼 것: SmolLM2는 T ≈ 3k에서, Llama-3.2-1B는 T ≈ 15k에서 attention core가 선형 항과 같아진다. **d가 작은 모델일수록 attention이 빨리 주인공이 된다** — 교차점이 (선형 항 ∝ d²) ÷ (attention ∝ d)라서 대략 d에 비례하기 때문이다. edge용 작은 모델이 긴 문맥에서 예상보다 느려지는 한 이유다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">

<polygon points="70.0,255.9 85.6,255.2 101.1,254.3 116.7,253.2 132.2,252.0 147.8,250.6 163.3,248.9 178.9,246.9 194.4,244.6 210.0,242.0 225.6,238.9 241.1,235.4 256.7,231.4 272.2,226.9 287.8,221.9 303.3,216.2 318.9,210.0 334.4,203.2 350.0,195.9 365.6,188.2 381.1,180.0 396.7,171.6 412.2,162.9 427.8,154.3 443.3,145.7 458.9,137.4 474.4,129.3 490.0,121.7 505.6,114.5 521.1,107.9 536.7,101.9 552.2,96.5 567.8,91.6 583.3,87.2 598.9,83.4 614.4,80.1 630.0,77.1 630.0,260.0 614.4,260.0 598.9,260.0 583.3,260.0 567.8,260.0 552.2,260.0 536.7,260.0 521.1,260.0 505.6,260.0 490.0,260.0 474.4,260.0 458.9,260.0 443.3,260.0 427.8,260.0 412.2,260.0 396.7,260.0 381.1,260.0 365.6,260.0 350.0,260.0 334.4,260.0 318.9,260.0 303.3,260.0 287.8,260.0 272.2,260.0 256.7,260.0 241.1,260.0 225.6,260.0 210.0,260.0 194.4,260.0 178.9,260.0 163.3,260.0 147.8,260.0 132.2,260.0 116.7,260.0 101.1,260.0 85.6,260.0 70.0,260.0" fill="#e08a3c" fill-opacity="0.75"/>
<polygon points="70.0,206.9 85.6,206.4 101.1,205.7 116.7,204.9 132.2,204.0 147.8,202.9 163.3,201.7 178.9,200.2 194.4,198.5 210.0,196.5 225.6,194.2 241.1,191.6 256.7,188.6 272.2,185.2 287.8,181.4 303.3,177.2 318.9,172.5 334.4,167.4 350.0,161.9 365.6,156.1 381.1,150.0 396.7,143.7 412.2,137.2 427.8,130.7 443.3,124.3 458.9,118.0 474.4,112.0 490.0,106.3 505.6,100.9 521.1,96.0 536.7,91.4 552.2,87.3 567.8,83.7 583.3,80.4 598.9,77.6 614.4,75.0 630.0,72.9 630.0,77.1 614.4,80.1 598.9,83.4 583.3,87.2 567.8,91.6 552.2,96.5 536.7,101.9 521.1,107.9 505.6,114.5 490.0,121.7 474.4,129.3 458.9,137.4 443.3,145.7 427.8,154.3 412.2,162.9 396.7,171.6 381.1,180.0 365.6,188.2 350.0,195.9 334.4,203.2 318.9,210.0 303.3,216.2 287.8,221.9 272.2,226.9 256.7,231.4 241.1,235.4 225.6,238.9 210.0,242.0 194.4,244.6 178.9,246.9 163.3,248.9 147.8,250.6 132.2,252.0 116.7,253.2 101.1,254.3 85.6,255.2 70.0,255.9" fill="#888" fill-opacity="0.75"/>
<polygon points="70.0,60.0 85.6,60.0 101.1,60.0 116.7,60.0 132.2,60.0 147.8,60.0 163.3,60.0 178.9,60.0 194.4,60.0 210.0,60.0 225.6,60.0 241.1,60.0 256.7,60.0 272.2,60.0 287.8,60.0 303.3,60.0 318.9,60.0 334.4,60.0 350.0,60.0 365.6,60.0 381.1,60.0 396.7,60.0 412.2,60.0 427.8,60.0 443.3,60.0 458.9,60.0 474.4,60.0 490.0,60.0 505.6,60.0 521.1,60.0 536.7,60.0 552.2,60.0 567.8,60.0 583.3,60.0 598.9,60.0 614.4,60.0 630.0,60.0 630.0,72.9 614.4,75.0 598.9,77.6 583.3,80.4 567.8,83.7 552.2,87.3 536.7,91.4 521.1,96.0 505.6,100.9 490.0,106.3 474.4,112.0 458.9,118.0 443.3,124.3 427.8,130.7 412.2,137.2 396.7,143.7 381.1,150.0 365.6,156.1 350.0,161.9 334.4,167.4 318.9,172.5 303.3,177.2 287.8,181.4 272.2,185.2 256.7,188.6 241.1,191.6 225.6,194.2 210.0,196.5 194.4,198.5 178.9,200.2 163.3,201.7 147.8,202.9 132.2,204.0 116.7,204.9 101.1,205.7 85.6,206.4 70.0,206.9" fill="#4a7bd0" fill-opacity="0.75"/>
<polyline points="70.0,259.1 85.6,259.0 101.1,258.8 116.7,258.6 132.2,258.3 147.8,258.0 163.3,257.6 178.9,257.1 194.4,256.6 210.0,256.0 225.6,255.2 241.1,254.4 256.7,253.3 272.2,252.1 287.8,250.7 303.3,249.0 318.9,247.1 334.4,244.8 350.0,242.2 365.6,239.2 381.1,235.8 396.7,231.8 412.2,227.4 427.8,222.3 443.3,216.8 458.9,210.6 474.4,203.9 490.0,196.6 505.6,188.9 521.1,180.8 536.7,172.3 552.2,163.7 567.8,155.1 583.3,146.5 598.9,138.1 614.4,130.0 630.0,122.4" fill="none" stroke="#d0564a" stroke-width="2" stroke-dasharray="6 4"/> <line x1="70" y1="260" x2="630" y2="260" stroke="currentColor"/> <line x1="70" y1="260" x2="70" y2="60" stroke="currentColor"/>
<text x="64" y="264.0" font-size="12" text-anchor="end">0%</text> <text x="64" y="214.0" font-size="12" text-anchor="end">25%</text> <text x="64" y="164.0" font-size="12" text-anchor="end">50%</text> <text x="64" y="114.0" font-size="12" text-anchor="end">75%</text> <text x="64" y="64.0" font-size="12" text-anchor="end">100%</text> <line x1="70.0" y1="260" x2="70.0" y2="264" stroke="currentColor"/> <text x="70.0" y="278" font-size="12" text-anchor="middle">64</text> <line x1="132.2" y1="260" x2="132.2" y2="264" stroke="currentColor"/> <text x="132.2" y="278" font-size="12" text-anchor="middle">128</text> <line x1="194.4" y1="260" x2="194.4" y2="264" stroke="currentColor"/>
<text x="194.4" y="278" font-size="12" text-anchor="middle">256</text> <line x1="256.7" y1="260" x2="256.7" y2="264" stroke="currentColor"/> <text x="256.7" y="278" font-size="12" text-anchor="middle">512</text> <line x1="318.9" y1="260" x2="318.9" y2="264" stroke="currentColor"/> <text x="318.9" y="278" font-size="12" text-anchor="middle">1k</text> <line x1="381.1" y1="260" x2="381.1" y2="264" stroke="currentColor"/> <text x="381.1" y="278" font-size="12" text-anchor="middle">2k</text> <line x1="443.3" y1="260" x2="443.3" y2="264" stroke="currentColor"/> <text x="443.3" y="278" font-size="12" text-anchor="middle">4k</text> <line x1="505.6" y1="260" x2="505.6" y2="264" stroke="currentColor"/>
<text x="505.6" y="278" font-size="12" text-anchor="middle">8k</text> <line x1="567.8" y1="260" x2="567.8" y2="264" stroke="currentColor"/> <text x="567.8" y="278" font-size="12" text-anchor="middle">16k</text> <line x1="630.0" y1="260" x2="630.0" y2="264" stroke="currentColor"/> <text x="630.0" y="278" font-size="12" text-anchor="middle">32k</text> <line x1="417.5" y1="260" x2="417.5" y2="60" stroke="currentColor" stroke-dasharray="2 3"/> <text x="421.5" y="74" font-size="12">교차 T = 3072</text> <text x="350.0" y="300" font-size="12" text-anchor="middle">시퀀스 길이 T (log₂ 축), prefill, full T×T 기준</text>
<text x="350.0" y="24" font-size="13" text-anchor="middle">SmolLM2-135M 층 하나의 MAC 구성비 vs T</text> <rect x="70" y="36" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75"/> <text x="86" y="47" font-size="12">FFN (SwiGLU)</text> <rect x="190" y="36" width="12" height="12" fill="#888" fill-opacity="0.75"/> <text x="206" y="47" font-size="12">Q·K·V·O proj</text> <rect x="320" y="36" width="12" height="12" fill="#e08a3c" fill-opacity="0.75"/> <text x="336" y="47" font-size="12">attention core (QKᵀ+AV)</text> <line x1="495" y1="42" x2="517" y2="42" stroke="#d0564a" stroke-width="2" stroke-dasharray="6 4"/> <text x="521" y="47" font-size="12">Llama-3.2-1B attn 비율</text>
</svg>
```

그림 5 — SmolLM2-135M 층 하나의 prefill MAC 구성비(파랑 FFN, 회색 proj, 주황 attention core) vs 시퀀스 길이(log 축, 1/4 옥타브 간격으로 37점 계산). 빨간 점선은 Llama-3.2-1B의 attention core 비율. 같은 T에서 작은 모델의 attention 몫이 훨씬 크다. causal 절약 커널이면 주황 영역이 약 절반으로 줄고 교차점은 약 2배 뒤로 밀린다.

### 5.4 경험칙 한 장

| 경험칙 | 식 | 쓰는 곳 |
|---|---|---|
| MAC ↔ FLOP | FLOP = 2 · MAC | 모든 단위 변환 |
| TOPS ↔ MAC/s | MAC/s = TOPS / 2 × 10¹² | 칩 스펙 해석 |
| conv 해상도 | MAC ∝ H·W (stride 누적 배수에서) | 입력 크기 결정 |
| conv 폭 | MAC ∝ C_in · C_out (폭 α배면 α²배) | width multiplier |
| DS-conv 절감 | 1/C_out + 1/k² | 블록 선택 (B2 6.3) |
| LLM forward | ≈ 2 · N_matmul FLOP/token + 문맥 항 | tokens/s 계산 (D5) |
| LLM 학습 | ≈ 6 · N FLOP/token | 학습 비용 |
| 층당 transformer (MHA, 4d FFN) | 12d² + 2·T·d MAC/token | 화이트보드 (B4) |
| attention 교차점 | T⋆ = 선형 MAC/token ÷ (2·h·d_h) | 긴 문맥 판단 |
| RNN | step당 gates · H · (I+H) | 스트리밍 오디오 |

---

## 6. MAC에서 latency 첫 추정

### 6.1 공식 — 그리고 왜 하한일 뿐인가

```
latency ≥ MAC / (피크 MAC/s)                       ← 절대 하한 (이용률 100%)
latency ≈ MAC / (피크 MAC/s × 이용률 u)             ← 첫 추정, u는 경험값
```

말로 하면: 칩이 MAC만 쉬지 않고 한다고 가정한 시간이 하한이다. 실제로는 (1) 메모리 대역폭이 데이터를 못 대 주거나(memory-bound, D3), (2) 레이어 모양이 MAC 배열에 안 맞거나(작은 채널, depthwise), (3) MAC이 아닌 연산(softmax·norm·resize)이 느린 유닛에서 돌거나, (4) 레이어 호출·DMA 설정 오버헤드가 있다. C7 2절이 "같은 MAC, 다른 latency"를 측정으로 보여 줬다.

예: MobileNetV2 @224(300.8M MAC)를 2 TOPS int8 NPU(= 1 TMAC/s)에서.

```
하한     : 300.8M / 1T        = 0.30 ms
u = 30%  : 0.30 / 0.3         = 1.0 ms
u = 10%  : 0.30 / 0.1         = 3.0 ms
```

화이트보드에서는 "0.3 ms가 하한이고, 이용률 10~50%를 가정하면 0.6~3 ms, 정확한 숫자는 벤더 프로파일러로 확인"이라고 말하면 된다.

### 6.2 코드로 확인 — 같은 CPU, 레이어 모양에 따라 실효 MAC/s가 20배 차이

**세 워크로드를 이 Mac(Apple M2) CPU 1 스레드에서 재고, MAC ÷ 시간 = 실효 MAC/s를 계산하는 코드**다.

```python
# MAC 수 ÷ 실측 시간 = 실효 MAC/s — 이 Mac CPU, 1 thread, fp32, batch 1
import time, torch, torch.nn as nn
from torchvision.models import mobilenet_v2
from maccount import count_macs
torch.set_num_threads(1); torch.manual_seed(0)
def bench(model, x, n=30):
    with torch.no_grad():
        for _ in range(5): model(x)
        ts = []
        for _ in range(n):
            t0 = time.perf_counter(); model(x); ts.append(time.perf_counter() - t0)
    return sorted(ts)[n // 2]
mv2 = mobilenet_v2(weights=None).eval()
big = nn.Sequential(nn.Conv2d(256, 256, 3, padding=1), nn.Conv2d(256, 256, 3, padding=1)).eval()
for name, model, x in (("MobileNetV2 @224", mv2, torch.randn(1, 3, 224, 224)),
                       ("MobileNetV2 @96 ", mv2, torch.randn(1, 3, 96, 96)),
                       ("2× conv3x3 256ch @28", big, torch.randn(1, 256, 28, 28))):
    macs = sum(count_macs(model, x).values()); t = bench(model, x)
    print(f"{name:21s} {macs/1e6:8.1f} M MAC  {t*1e3:7.2f} ms  → {macs/t/1e9:6.1f} GMAC/s")
```

```text
MobileNetV2 @224         300.8 M MAC    14.58 ms  →   20.6 GMAC/s
MobileNetV2 @96           56.3 M MAC     4.89 ms  →   11.5 GMAC/s
2× conv3x3 256ch @28     924.8 M MAC     2.13 ms  →  433.5 GMAC/s
```

출력에서 볼 것: 같은 기계에서 실효 속도가 약 11 GMAC/s(MobileNetV2 @96)부터 수백 GMAC/s(큰 3×3 conv)까지 벌어진다. MobileNetV2를 224 → 96으로 줄이면 MAC은 5.3배 줄지만 시간은 약 3배만 준다 — 작은 레이어일수록 오버헤드와 낮은 이용률이 커지기 때문이다. 마지막 줄은 실행마다 흔들렸다(같은 스크립트를 여러 번 돌리면 약 180~430 GMAC/s). 이 PyTorch 빌드는 `BLAS_INFO=accelerate`라 큰 GEMM이 Apple Accelerate로 가는데, 그 내부 경로(전용 행렬 유닛 사용 여부, 스레드)는 공개 문서가 적어 여기서는 추정만 한다. 요점은 **MAC 수가 같아도 모양과 백엔드가 실효 MAC/s를 바꾼다**는 것이고, 그래서 MAC 추정은 "하한 + 이용률 가정"으로만 쓴다.

### 6.3 첫 추정을 쓸 때의 체크리스트

- 분자·분모 단위 일치(MAC vs OP)
- 칩 숫자가 dense·같은 정밀도(int8)인지
- 레이어별로 compute-bound인지 memory-bound인지(D3 roofline에서 AI = MAC·2 / 바이트)
- MAC에 안 잡히는 연산(softmax, LN, resize, NMS 후처리)의 몫
- batch 1 스트리밍이면 레이어 호출 오버헤드(D6)

---

## 7. 인터뷰 퀵 드릴 10문제

화이트보드에서 1~2분 안에 풀어야 하는 유형이다. 답을 가리고 먼저 풀어 본다. 계산 과정은 "출력 원소 수 × 칸당 K"로 쓴다.

**Q1.** 3×3 conv, 64→128 채널, 56×56 입력, stride 1, same padding. MAC과 파라미터는?

정답: 출력 56·56·128 원소 × 칸당 64·9 → **231,211,008 MAC(≈ 231M, 462M FLOP)**. 파라미터 128·64·9 + 128 = **73,856**.

**Q2.** Q1을 depthwise separable로 바꾸면?

정답: dw 56·56·64·9 = 1,806,336 + pw 56·56·64·128 = 25,690,112 → **27,496,448 MAC, 약 8.4배 감소**. 공식 1/128 + 1/9 ≈ 0.119와 일치.

**Q3.** Linear 1024→4096을 토큰 128개에 적용하면?

정답: 128 · 1024 · 4096 = **536,870,912 MAC(≈ 0.54 GMAC)**. 파라미터 4.19M은 T와 무관.

**Q4.** Transformer 층(MHA, FFN 4d, GELU) d = 2048, T = 1024의 forward FLOPs는?

정답: 토큰당 12d² + 2·T·d = 50,331,648 + 4,194,304 = 54.5M MAC, × 1024 → **55.83 GMAC = 111.67 GFLOP**(full T²). causal 건너뛰기 커널이면 attention 항이 반으로 줄어 **107.37 GFLOP**.

**Q5.** 행렬곱에 쓰이는 가중치가 약 1B개인 LLM이 초당 20 토큰을 decode하려면 연산이 얼마 필요한가? 병목은?

정답: 토큰당 약 2 GFLOP → **약 40 GFLOP/s**. 모바일 NPU의 수 TOPS에 비하면 작다. 병목은 매 토큰 가중치 전체(int8이면 약 1 GB)를 읽는 **메모리 대역폭**이다: 20 tok/s × 1 GB = 20 GB/s(D5).

**Q6.** MobileNetV2가 224에서 300M MAC이면 112에서는?

정답: 제곱 법칙으로 약 75M + FC 1.28M ≈ 76M이라고 답하고 싶지만, 실제로는 **82.18M**이다. 112가 32의 배수가 아니라서 마지막 단계가 4×4로 올림된다. "대략 1/4, 단 stride 정렬 때문에 조금 더"가 모범 답이다.

**Q7.** GRU, 입력 40, hidden 128, 10 ms마다 1 step. 초당 MAC은?

정답: step당 3·128·(40+128) = **64,512 MAC**, × 100 step/s = **6.45M MAC/s**. 80 MHz M4에서 0.5 MAC/cycle이면 약 16% CPU.

**Q8.** 1000-class softmax의 MAC은?

정답: **0 MAC**. 대신 max 999 + 빼기 1000 + exp 1000 + 합 999 + 나누기 1000 ≈ 5k op. MCU에서는 exp가 비싸니 argmax만 필요하면 softmax 자체를 생략하거나 LUT를 쓴다.

**Q9.** 1 GMAC 모델, 4 TOPS(int8) NPU, 이용률 30%. latency 추정은?

정답: 4 TOPS = 2 TMAC/s. 하한 1G / 2T = **0.5 ms**, 이용률 30%면 **약 1.7 ms**. 흔한 실수: 1G를 4T로 나눠 0.25 ms라고 답하는 것(MAC을 OP로 나눔).

**Q10.** 어휘 49,152, d = 576 모델에서 토큰 하나의 embedding lookup과 lm_head 비용은?

정답: lookup은 **0 MAC**, 읽기 576 원소(fp16 1,152 B). lm_head는 **576 · 49,152 = 28.3M MAC** — 층 하나(3.54M)의 8배. 작은 LLM에서는 vocab이 연산에서도 큰 몫이다.

**퀵 드릴 답 가운데 계산이 긴 것들(Q1~Q4, Q6, Q7)을 한 번에 검산하는 코드**다.

```python
# 인터뷰 퀵 드릴 정답을 한 번에 검산
import torch, torch.nn as nn
from torchvision.models import mobilenet_v2
from maccount import count_macs
import formulas as f
c = nn.Conv2d(64, 128, 3, 1, 1)
print("Q1 conv MAC", f.conv2d(56, 56, 64, 128, 3, 3), "params", sum(p.numel() for p in c.parameters()))
dw, pw = f.depthwise(56, 56, 64, 3), f.pointwise(56, 56, 64, 128)
print("Q2 DS-conv", dw, "+", pw, "=", dw + pw, f"-> {f.conv2d(56,56,64,128,3,3)/(dw+pw):.2f}x less")
print("Q3 linear", f.linear(128, 1024, 4096))
d, T = 2048, 1024
lin, att = T * 12 * d * d, T * 2 * T * d            # MHA + FFN(4d, 2 matrices)
print(f"Q4 layer: {lin+att:,} MAC = {2*(lin+att)/1e9:.2f} GFLOP (causal skip: {2*(lin+att/2)/1e9:.2f})")
m = mobilenet_v2(weights=None).eval()
print("Q6 MobileNetV2 @112:", sum(count_macs(m, torch.randn(1, 3, 112, 112)).values()))
print("Q7 GRU step", f.rnn_step(3, 40, 128), "-> per s @100 steps", 100 * f.rnn_step(3, 40, 128))
```

```text
Q1 conv MAC 231211008 params 73856
Q2 DS-conv 1806336 + 25690112 = 27496448 -> 8.41x less
Q3 linear 536870912
Q4 layer: 55,834,574,848 MAC = 111.67 GFLOP (causal skip: 107.37)
Q6 MobileNetV2 @112: 82183808
Q7 GRU step 64512 -> per s @100 steps 6451200
```

출력에서 볼 것: 위 답안의 숫자와 같다. 특히 Q6의 82,183,808은 제곱 법칙 예측(약 76M)과 다르다는 점.

---

## 8. 임베디드 관점에서 다시 보기 — MCU의 MAC 예산

### 8.1 루프 카운터 = 공식 (C로 확인)

펌웨어 엔지니어에게 가장 설득력 있는 검증은 **실제 루프에 카운터를 다는 것**이다. B2 5절의 naive conv 루프에 grouped를 넣고, 곱셈 자리에서 카운터를 올린다.

**grouped conv2d의 중첩 루프가 도는 횟수와 공식을 비교하는 C 코드**다.

```c
/* conv_count.c — naive grouped conv2d 루프에 MAC 카운터를 달아 공식과 비교 */
#include <stdio.h>
typedef struct { int H, W, Cin, Cout, k, s, p, g; } Cfg;
static long long conv_loop(Cfg c) {             /* 실제 곱셈은 생략, 루프 구조만 */
    int Ho = (c.H + 2 * c.p - c.k) / c.s + 1, Wo = (c.W + 2 * c.p - c.k) / c.s + 1;
    int cin_g = c.Cin / c.g, cout_g = c.Cout / c.g;
    long long macs = 0;
    for (int oc = 0; oc < c.Cout; oc++) {
        int ic0 = (oc / cout_g) * cin_g;          /* 이 출력 채널이 보는 입력 채널 그룹 */
        for (int oy = 0; oy < Ho; oy++)
            for (int ox = 0; ox < Wo; ox++)
                for (int ic = ic0; ic < ic0 + cin_g; ic++)
                    for (int ky = 0; ky < c.k; ky++)
                        for (int kx = 0; kx < c.k; kx++)
                            macs++;               /* acc += x[..] * w[..] 자리 (패딩 포함) */
    }
    return macs;
}
static long long conv_formula(Cfg c) {
    long long Ho = (c.H + 2 * c.p - c.k) / c.s + 1, Wo = (c.W + 2 * c.p - c.k) / c.s + 1;
    return Ho * Wo * c.Cout * (c.Cin / c.g) * c.k * c.k;
}
int main(void) {
    Cfg t[] = {{56, 56, 64, 128, 3, 1, 1, 1}, {56, 56, 64, 128, 3, 1, 1, 4},
               {56, 56, 64, 64, 3, 2, 1, 64}, {28, 28, 64, 128, 1, 1, 0, 1}};
    const char *name[] = {"std 3x3", "grouped g=4", "depthwise s2", "pointwise"};
    for (int i = 0; i < 4; i++)
        printf("%-13s loop %11lld  formula %11lld\n", name[i], conv_loop(t[i]), conv_formula(t[i]));
    return 0;
}
```

```text
std 3x3       loop   231211008  formula   231211008
grouped g=4   loop    57802752  formula    57802752
depthwise s2  loop      451584  formula      451584
pointwise     loop     6422528  formula     6422528
```

출력에서 볼 것: 네 경우 모두 루프 횟수 = 공식이고, 2.2절 표·Python 카운터의 값과도 같다. padding 칸도 "곱셈이 일어난 것"으로 센다는 점에 주의 — 실제 커널은 경계에서 0 곱을 건너뛸 수 있어 조금 덜 한다. 공식은 늘 "밀집 계산 기준"이다.

### 8.2 10 ms hop당 예산

always-on 오디오(16 kHz, 10 ms hop = 160 샘플)에서는 **hop마다 새 특징 프레임 하나**가 들어온다. 시스템은 다음 hop 전에 그 프레임을 처리해야 한다(D6 deadline). 예산은:

```
hop당 사이클   = f_clk × 0.010 s
hop당 ML 사이클 = 위 × ML에 줄 CPU 비율 (나머지는 오디오 front-end, BLE, OS)
hop당 MAC 예산 = ML 사이클 × 실효 MAC/cycle
```

아래 숫자는 **모두 가정값**이다. Cortex-M4의 `SMLAD`는 16bit 곱 두 개를 한 명령으로 누산하므로 피크 2 MAC/cycle이지만, 데이터 로드·재배치 오버헤드로 실효는 그보다 훨씬 낮게 잡는다. Helium(M55)과 Ethos-U55 같은 microNPU(MAC 유닛 수가 설정에 따라 수십~수백 개)는 몇 배~수십 배 높다. 실제 값은 해당 칩·커널을 측정해서 넣는다(K 모듈).

**hop당 MAC 예산과, 드릴 (a) DS-CNN(2.66M MAC)을 매번 새로 돌리려면 몇 hop이 필요한지 계산하는 C 코드**다.

```c
/* budget.c — 10 ms hop마다 쓸 수 있는 MAC 예산 (모든 HW 숫자는 가정값) */
#include <stdio.h>
#include <math.h>
typedef struct { const char *name; double mhz, mac_per_cycle; } Core;
int main(void) {
    const double hop_s = 0.010, ml_share = 0.5;         /* CPU 시간의 50%만 ML에 */
    const double model_macs = 2656768.0;                 /* 드릴 (a) DS-CNN 1회 */
    Core c[] = {{"M4F  80MHz, 0.5 MAC/cyc", 80, 0.5},
                {"M33 160MHz, 0.5 MAC/cyc", 160, 0.5},
                {"M55 200MHz, 2 MAC/cyc  ", 200, 2.0},
                {"U55 200MHz, 64 MAC/cyc ", 200, 64.0}};
    printf("%-26s %12s %10s %12s\n", "core (assumed)", "MAC/hop", "hops/inf", "inf period");
    for (int i = 0; i < 4; i++) {
        double per_hop = c[i].mhz * 1e6 * hop_s * ml_share * c[i].mac_per_cycle;
        double hops = ceil(model_macs / per_hop);
        printf("%-26s %12.0f %10.0f %9.0f ms\n", c[i].name, per_hop, hops, hops * hop_s * 1e3);
    }
    return 0;
}
```

```text
core (assumed)                  MAC/hop   hops/inf   inf period
M4F  80MHz, 0.5 MAC/cyc          200000         14       140 ms
M33 160MHz, 0.5 MAC/cyc          400000          7        70 ms
M55 200MHz, 2 MAC/cyc           2000000          2        20 ms
U55 200MHz, 64 MAC/cyc         64000000          1        10 ms
```

출력에서 볼 것: 80 MHz M4에서 hop당 예산은 20만 MAC이라, 2.66M MAC짜리 DS-CNN을 통째로 돌리면 14 hop(140 ms)에 한 번밖에 못 돈다. 선택지는 세 가지다.

- **덜 자주 돌린다**: 1 s 창을 100~150 ms마다 평가. KWS는 단어 길이가 수백 ms라 흔히 허용된다(대신 검출 지연이 는다).
- **스트리밍으로 바꾼다**: conv1이 시간 stride 2, 이후 층은 stride 1이므로 새 입력 2프레임마다 층마다 **출력 시간 행 1개(5×64 원소)만** 새로 계산하면 된다. 행 하나 비용 = conv1 5·64·40 = 12,800 + DS 4개 × (5·64·9 + 5·64·64) = 93,440 → 약 **106k MAC / 20 ms = 53k MAC/hop**. 통째 재계산(25행)의 1/25로 예산 안에 들어온다. 대신 same padding 대신 causal padding과 층별 링버퍼(B2 8절)가 필요하다.
- **더 센 코어·NPU로 옮긴다**: M55(Helium)나 microNPU면 한 hop 안에 통째로 돈다. 대신 전력·면적·BOM이 는다(I 모듈 co-design).

### 8.3 두 단계 파이프라인 예산 (예를 들어 Hark 같은 웨어러블이라면 — 추정)

```
단계                  돌아가는 곳(추정)         주기        MAC/회       MAC/s
────────────────────────────────────────────────────────────────────────────
VAD / 착용 감지       always-on MCU           10 ms       ~10k         ~1M
Wake word (스트리밍)  always-on MCU/DSP       10~20 ms    ~50–100k     ~5M
명령어 인식 / ASR     SoC DSP·NPU (깨어난 뒤)   요청 시      수백M~수G     버스트
LLM 응답 (1B, decode) SoC NPU/CPU 또는 클라우드  토큰당       ~1.2 GMAC    tok/s × 1.2G
```

위 표의 wake word와 LLM 행은 이 노트의 드릴(DS-CNN 스트리밍 약 53k MAC/hop, 1B decode 1.24 GMAC/token)에서 가져온 규모이고, VAD와 ASR 행은 규모 감각을 위한 가정값이다. 요점: **always-on 경로는 초당 수백만 MAC, 깨어난 경로는 초당 수십억 MAC**으로 세 자릿수 차이가 나고, 이 차이가 "어느 칩에서 무엇을 돌리나"를 정한다(I1, L 모듈 hybrid).

### 8.4 정리 — 펌웨어 관점

- MAC 수는 **루프 반복 수**다. 공식은 루프 경계의 곱이다.
- hop당 MAC 예산은 **인터럽트 주기당 사이클 예산**과 같은 계산이다.
- MAC 수가 예산 안이어도 SRAM(activation peak, D2)과 flash(가중치)가 먼저 막힐 수 있다. D1은 필요조건만 준다.
- int8 양자화는 MAC 수를 안 바꾸고 MAC/cycle을 올린다(SIMD 폭 2~4배). "MAC 수 × MAC당 비용"으로 나눠 생각한다.

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| MAC을 TOPS로 바로 나눔 | latency 추정이 실측의 절반보다도 낙관적 | TOPS는 2 OP/MAC | MAC/s = TOPS/2로 바꾼 뒤 나눈다 |
| 논문 "FLOPs"를 FLOP으로 믿음 | 다른 표와 비교하면 모델이 2배 싸 보이거나 비싸 보임 | 많은 논문·torchvision 표가 MAC을 FLOPs라 부름 | 기준 모델(MobileNetV2 = 300M MAC)로 관례를 역산 |
| FlopCounterMode 숫자를 그대로 신뢰 | transformer의 attention 몫이 0 | CPU MHA SDPA 커널이 registry에 없음 | 명시적 matmul로 검증하거나 SDPA를 직접 세는 카운터 병행 |
| grouped conv에서 g를 빼먹음 | depthwise MAC이 C배로 과대 | 공식에 C_in 전체를 넣음 | 칸당 K = (C_in/g)·k² |
| attention을 파라미터로만 판단 | 긴 문맥에서 예상보다 느림 | attention core는 파라미터 0, MAC ∝ T² | 2·h·d_h·T 항을 따로 더함, 교차점 T⋆ 계산 |
| LLM 2N에서 N에 입력 embedding 포함(untied) | 토큰당 FLOP 과대 | lookup은 MAC 0 | N_matmul = 층 가중치 + lm_head |
| LLM 2N에서 lm_head 누락 (embedding 뺀 N) | 작은 모델 FLOP 20% 이상 과소 | Kaplan식 N은 embedding 제외 | 작은 모델은 lm_head(d·V)를 따로 더함 |
| 해상도 제곱 법칙을 아무 크기에 적용 | 112에서 76M 예상, 실제 82M | stride 누적에서 올림 | 네트워크 stride 배수로 해상도 선택, 카운터로 확인 |
| 원소 연산을 0으로 두고 끝 | NPU에서 LN·softmax·resize가 시간 대부분 | MAC 배열 밖 유닛에서 느리게 돎 | op 수를 따로 세고 프로파일러로 확인 (K 모듈) |
| MAC만으로 latency 결론 | 같은 MAC 두 모델의 속도가 3배 차이 | memory-bound·이용률 | D3 roofline으로 레이어별 판정, C7 LUT |

---

## 10. 면접에서 이렇게 말한다

**Q.** How do you estimate the compute cost of a convolution layer?

**A.** 출력 원소 수에 원소 하나당 누적 길이를 곱한다. conv면 Ho·Wo·C_out 곱하기 (C_in/g)·k². 예로 3×3, 64→128, 56×56이면 231M MAC이고 FLOP으로는 두 배다. 파라미터는 해상도와 무관하게 73.9K라서, MAC/파라미터 비율이 재사용 정도를 알려 준다고 덧붙인다.

> I count output elements times the accumulation length per element. For a conv that is Ho times Wo times C_out, times C_in over groups times k squared. A 3x3 conv from 64 to 128 channels on 56 by 56 is about 231 million MACs, or 462 million FLOPs if you count multiply and add separately. The weights are only about 74 thousand, so each weight is reused thousands of times, which is why convs tend to be compute-bound.

**Q.** What's the difference between MACs, FLOPs and TOPS, and where do people get confused?

**A.** MAC은 곱셈-누산 한 번, 관례상 2 FLOP이다. 모델의 GFLOPs는 추론 한 번의 양, 하드웨어의 GFLOPS·TOPS는 초당 속도이고 TOPS는 보통 int8 피크에 MAC당 2 OP로 센다. 혼동의 대표 예는 torchvision 표가 "GFLOPS"라고 쓴 값이 실제로는 GMAC이라는 것. 그래서 저는 항상 "300M MACs, about 0.6 GFLOPs"처럼 둘 다 말한다.

> A MAC is one multiply-accumulate, which by convention is two FLOPs. A model's GFLOPs is an amount per inference, while a chip's GFLOPS or TOPS is a rate, and TOPS is usually peak int8 counting two ops per MAC. The classic confusion is that many papers and even the torchvision tables call MACs "FLOPs", so numbers differ by exactly two. I always state both, for example 300 million MACs, about 0.6 GFLOPs, and I convert TOPS to MACs per second before dividing.

**Q.** Roughly how many FLOPs does a 1B-parameter LLM need per generated token, and is that the bottleneck?

**A.** 행렬곱에 쓰이는 가중치 N에 대해 약 2N FLOP, 즉 2 GFLOP 정도에 문맥 길이에 비례하는 attention 항이 붙는다. 입력 embedding은 lookup이라 빼고 lm_head는 넣는다. 그런데 decode에서는 토큰마다 가중치 전체를 읽어야 해서 연산이 아니라 메모리 대역폭이 병목이다. 20 tok/s면 40 GFLOP/s로 NPU엔 작지만 int8 1 GB를 20번 읽는 20 GB/s가 필요하다.

> About two FLOPs per weight that participates in a matmul, so roughly 2 GFLOPs per token, plus an attention term that grows with context length. You exclude the input embedding lookup but include the LM head. But compute is rarely the bottleneck in decode: every token has to stream all the weights from memory, so at 20 tokens per second an int8 1B model needs about 20 GB/s of bandwidth while the compute is only about 40 GFLOP/s.

**Q.** When does attention dominate over the feed-forward layers in a transformer?

**A.** 토큰당 선형 항은 d²에 비례하고 attention core는 2·h·d_h·T라 T에 비례한다. 둘이 같아지는 T는 선형 MAC을 2·h·d_h로 나눈 값이다. Llama-3.2-1B는 약 15k 토큰, SmolLM2-135M은 약 3k 토큰으로, 작은 모델일수록 빨리 attention이 지배한다. 그 전에는 FFN이 가장 큰 덩어리다.

> Per token, the weight matmuls scale with d squared while the attention core scales with 2 times heads times head dim times the context length. The crossover is the linear cost per token divided by that coefficient. For Llama 3.2 1B it's around 15 thousand tokens, for SmolLM2 135M only about 3 thousand, so smaller models hit the attention-dominated regime sooner. Below that, the FFN is the biggest chunk.

**Q.** Your model is 300M MACs and the NPU is rated 2 TOPS. What latency do you expect?

**A.** 2 TOPS는 1 TMAC/s이므로 하한은 0.3 ms. 실제로는 이용률이 10~50%라 0.6~3 ms로 잡고, 레이어별로 memory-bound인 것(depthwise, 작은 채널)과 NPU가 지원하지 않아 CPU로 떨어지는 연산을 확인한 뒤 벤더 프로파일러로 측정한다. MAC 추정은 하한과 우선순위를 주지 정답을 주지 않는다.

> Two TOPS is one tera-MAC per second, so the floor is 0.3 milliseconds. Real utilization is often 10 to 50 percent, so I'd budget roughly 0.6 to 3 milliseconds, then check which layers are memory-bound, like depthwise or thin layers, and which ops fall back to the CPU, and confirm with the vendor profiler. The MAC estimate gives a floor and a priority list, not the answer.

**Q.** How would you verify a FLOP counter's output?

**A.** 공식으로 손계산한 작은 레이어들, 그리고 알려진 기준 모델(MobileNetV2 300M MAC)과 맞춰 본다. 도구마다 세는 커널이 달라서, 예를 들어 PyTorch FlopCounterMode는 CPU의 비-GQA SDPA 커널을 세지 않아 attention이 0으로 나왔다. 그래서 명시적 matmul 버전과 교차 검증했다. 펌웨어식으로 naive 루프에 카운터를 달아 공식과 비교하는 것도 한다.

> I check it against hand formulas on small layers and against a reference model, like MobileNetV2 at 300 million MACs. Tools only count the kernels they know about: PyTorch's FlopCounterMode, for example, reported zero for attention because the CPU SDPA kernel it dispatched to isn't in its registry, so I cross-checked with an explicit matmul version. I also like the firmware approach of instrumenting a naive loop with a counter and comparing it to the formula.

---

## 11. 직접 해보기

1. 5×5 conv, 32→64, 입력 40×40, stride 2, padding 2의 출력 크기·MAC·파라미터를 손으로 구하라. 정답: Ho = ⌊(40+4−5)/2⌋+1 = 20 → 20·20·64·32·25 = 20,480,000 MAC, 파라미터 51,200 + 64 = 51,264.
2. d = 768, h = 12, MHA, FFN 3072(GELU), T = 512인 encoder 층 하나의 MAC을 proj·attention·FFN으로 나눠 구하고 attention 몫(%)을 말하라. 정답: proj 4·768²·512 = 1.208G, FFN 2·768·3072·512 = 2.416G, attention 2·512²·768 = 0.403G, 합 4.027G MAC → attention 약 10%.
3. `maccount.py`에 `nn.ConvTranspose2d`(2.15절의 입력 기준 공식)와 `nn.MultiheadAttention`을 추가하고 FlopCounterMode(÷2)와 비교하라. 힌트: MultiheadAttention은 내부에서 `F.multi_head_attention_forward`를 부른다 — 모듈 hook에서 in/out 투영 4·d²·T + 2·T²·d를 직접 더하는 편이 간단하다.
4. 드릴 (a) DS-CNN의 채널을 64 → 96으로 늘리면 MAC이 몇 배가 되나? 손으로 먼저, 코드로 확인. 정답: conv1 ×1.5, dw ×1.5, pw ×2.25 → 합 480,000 + 4·(108,000 + 1,152,000) + 1,152 = 5,521,152 MAC(약 2.08배).
5. Qwen2.5-0.5B의 config(로컬 캐시 `Qwen/Qwen2.5-0.5B`)를 읽어 토큰당 MAC(Tk = 0)을 2.12절 공식으로 구하고, 파라미터 수 N과 비교해 "2·N 규칙"이 몇 % 맞는지 보라. 힌트: 이 모델은 Q·K·V 투영에 bias가 있고(MAC에는 영향 없음) embedding이 tied다. 정답은 직접 실행해서 확인.
6. 8.2절 스트리밍 계산을 드릴 (c) IMU 1D-CNN에 적용하라: 50 Hz로 샘플이 하나 들어올 때마다 층별로 새로 계산할 MAC은? 힌트: MaxPool 2 때문에 conv2는 2샘플마다, conv3은 4샘플마다 한 칸 → 16·6·5 + (32·16·5)/2 + (64·32·3)/4 = 480 + 1,280 + 1,536 = 3,296 MAC/샘플 평균.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| MAC | multiply-accumulate | `acc += w·x` 한 번. 이 노트의 기본 단위 |
| MAdds / multiply-adds | MAC의 다른 이름 | MobileNet·ResNet 논문 표기 |
| FLOP | floating-point operation | 관례상 1 MAC = 2 FLOP |
| GFLOPs | 모델 연산량 | 추론 1회당 10⁹ FLOP (양) |
| GFLOPS | 하드웨어 속도 | 초당 10⁹ FLOP (속도) |
| TOPS | tera ops per second | 보통 int8 dense 피크, 2 OP/MAC |
| 이용률 (utilization) | 실효/피크 | 레이어 모양·메모리에 따라 크게 변함 |
| 누적 길이 K | reduction length | 출력 원소 하나를 만들 때 도는 MAC 수 |
| grouped conv | 채널을 g묶음으로 나눈 conv | MAC·파라미터 1/g |
| depthwise / pointwise | g = C인 conv / 1×1 conv | 칸당 k² / 칸당 C_in |
| attention core | QKᵀ와 AV | 파라미터 0, MAC ∝ T² |
| GQA | grouped-query attention | K/V head 공유. core MAC 불변, K/V 투영·캐시 감소 |
| SwiGLU | gate·up·down 3행렬 FFN | FFN MAC = 3·d·ff |
| prefill / decode | 프롬프트 일괄 처리 / 토큰 하나씩 생성 | 같은 MAC/token, 다른 병목 |
| FlopCounterMode | PyTorch FLOP 카운터 | aten 연산을 가로챔, 2 FLOP/MAC |
| forward hook | 모듈 출력 시 호출되는 콜백 | 레이어별 MAC 표 만들기 |
| TorchFunctionMode | torch 함수 호출 가로채기 | 모듈이 아닌 matmul·SDPA 세기 |
| hop | 스트리밍 프레임 간격 | 10 ms hop = 16 kHz에서 160 샘플 |

---

## 13. 요약 & 체크리스트

모든 레이어의 연산량은 "출력 원소 수 × 원소당 누적 길이"로 센다. Linear는 T·d_in·d_out, conv는 Ho·Wo·C_out·(C_in/g)·k², attention core는 h·T²·d_h·2, RNN은 step당 gates·H·(I+H)이고, 정규화·활성화·softmax·pool·embedding은 MAC 0(원소 연산은 따로)이다. MAC 1 = FLOP 2 = OP 2가 관례지만 논문·표·도구가 섞어 쓰므로 기준 모델로 역산해 확인한다. 도구는 "아는 커널"만 센다 — PyTorch `FlopCounterMode`는 CPU의 비-GQA SDPA를 0으로 셌다. LLM은 토큰당 약 2·N_matmul FLOP에 문맥 비례 attention 항이 붙고, 작은 모델일수록 attention이 빨리 커진다. CNN MAC은 해상도 제곱에 비례하되 stride 배수에서만 정확하다. MAC ÷ 실효 MAC/s는 latency의 하한일 뿐이고, 실측 이용률은 같은 기계에서도 레이어 모양에 따라 수십 배 달라진다. MCU에서는 이 숫자를 hop당 사이클 예산과 비교하고, 부족하면 빈도·스트리밍·코어 선택으로 맞춘다.

- [ ] MAC, FLOP, OPS, TOPS, GFLOPs(양), GFLOPS(속도)를 구분해서 단위 변환을 할 수 있다
- [ ] conv·grouped·depthwise·pointwise·conv1d의 MAC과 파라미터를 손으로 계산할 수 있다
- [ ] attention core를 QKᵀ와 AV로 나눠 세고, causal·GQA·decode에서 무엇이 바뀌는지 말할 수 있다
- [ ] GQA + SwiGLU transformer 층의 토큰당 MAC을 config 값으로 계산할 수 있다
- [ ] LSTM/GRU step당 MAC과 초당 MAC을 계산할 수 있다
- [ ] forward hook + matmul 가로채기 카운터를 만들고 FlopCounterMode와 비교해 차이를 설명할 수 있다
- [ ] KWS DS-CNN, MobileNetV2 앞부분, IMU 1D-CNN의 전체 MAC을 층별 표로 손계산할 수 있다
- [ ] LLM의 "2·N" 규칙에서 embedding·lm_head·attention 항의 뉘앙스를 설명할 수 있다
- [ ] MAC과 TOPS로 latency 하한을 구하고, 왜 하한일 뿐인지 네 가지 이유를 말할 수 있다
- [ ] MCU 클럭·MAC/cycle 가정에서 10 ms hop당 MAC 예산을 계산하고 모델이 들어가는지 판단할 수 있다

---

## 참고 자료

- He, Zhang, Ren, Sun, "Deep Residual Learning for Image Recognition", CVPR 2016 (arXiv:1512.03385) — "3.6 billion FLOPs (multiply-adds)" 표기.
- Sandler et al., "MobileNetV2: Inverted Residuals and Linear Bottlenecks", CVPR 2018 (arXiv:1801.04381) — MAdds 단위 표.
- Howard et al., "MobileNets: Efficient Convolutional Neural Networks for Mobile Vision Applications", 2017 (arXiv:1704.04861) — depthwise separable의 Mult-Adds 계산.
- Zhang, Suda, Lai, Chandra, "Hello Edge: Keyword Spotting on Microcontrollers", 2017 (arXiv:1711.07128) — DS-CNN KWS와 MCU 연산·메모리 예산.
- Kaplan et al., "Scaling Laws for Neural Language Models", 2020 (arXiv:2001.08361) — C_forward ≈ 2N + 2·n_layer·n_ctx·d_attn.
- Vaswani et al., "Attention Is All You Need", NeurIPS 2017 (arXiv:1706.03762).
- Dao et al., "FlashAttention: Fast and Memory-Efficient Exact Attention with IO-Awareness", NeurIPS 2022 (arXiv:2205.14135) — causal 블록 건너뛰기, IO 관점.
- PyTorch 문서: `torch.utils.flop_counter.FlopCounterMode`, `torch.overrides.TorchFunctionMode`, `Module.register_forward_hook` — pytorch.org/docs.
- torchvision 모델 문서 표(Params, GFLOPS 열)와 weights `meta` — pytorch.org/vision.
- MIT 6.5940 "TinyML and Efficient Deep Learning Computing" (Song Han) — MAC·메모리·latency를 함께 보는 강의.
- Arm CMSIS-NN 문서 — Cortex-M용 int8 conv/FC 커널(SIMD `SMLAD` 활용).
