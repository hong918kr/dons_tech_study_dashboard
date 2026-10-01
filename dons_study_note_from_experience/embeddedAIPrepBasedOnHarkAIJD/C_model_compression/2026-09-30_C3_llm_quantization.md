# C3. LLM 양자화 — W4A16, W8A8, outlier, SmoothQuant, GPTQ, AWQ, KV-cache 양자화

> **이 노트를 다 읽으면**: W4A16·W8A8 같은 형식 이름을 보고 메모리·속도·정확도 trade-off를 숫자로 말할 수 있다 · 실제 SLM에 RTN·per-group 양자화를 걸고 perplexity로 평가할 수 있다 · activation outlier가 왜 W8A8을 깨뜨리는지, SmoothQuant·GPTQ·AWQ가 각각 무엇을 고치는지 직접 구현해서 설명할 수 있다 · KV-cache를 int8/int4로 줄였을 때의 메모리 이득과 품질 영향을 계산하고 측정할 수 있다
> **JD 연결**: "(우대) Lightweight LLM models", "Optimizing models for MCUs & edge processors", "Co-design model architectures that meet latency, memory, power, bandwidth", "(우대) … transformers, KV-cache, memory bandwidth" — study_prep_list **C3**: weight-only(W4A16) vs W8A8 · activation outlier 문제, SmoothQuant · GPTQ, AWQ · KV-cache 양자화 (연결: D5 LLM 추론 성능, F3 llama.cpp/GGUF)
> **Don 기준 난이도**: 고정소수점·비트 패킹·대역폭 계산·측정 기반 디버깅은 이미 강한 부분 / perplexity 평가, outlier의 정체, Hessian 기반 오차 보상(GPTQ), activation-aware scaling(AWQ)은 새로 배울 부분
> **선행 노트**: B4 (attention, KV-cache 공식), B8 (SLM 구조, 메모리·tokens/s 계산, SmolLM2), C1 (scale·zero-point, per-channel/per-group 양자화 수식)

---

## 0. 큰 그림 — 이게 왜 필요한가

B8에서 본 핵심 관찰을 한 줄로 다시 쓰면 이렇다. **LLM decode는 token 하나를 만들 때마다 가중치 전부를 DRAM에서 한 번씩 읽는다.** 그래서 decode 속도의 상한은 연산기가 아니라 메모리 대역폭이 정한다(B8 1.5절, D5).

```
decode tokens/s 상한 ≈ 메모리 대역폭 ÷ (가중치 바이트 + KV-cache 바이트)
```

말로 하면: 가중치를 절반으로 줄이면 token/s 상한이 거의 두 배가 된다. CNN 양자화(C1, C2)에서는 "INT8 MAC이 FP32보다 빠르다"가 주된 이유였지만, LLM에서는 **"읽을 바이트가 줄어든다"**가 주된 이유다. 이 차이 때문에 LLM 양자화는 독특한 방향으로 발전했다.

- **가중치만 줄여도(weight-only) 대부분의 이득을 얻는다.** decode의 활성값은 벡터 하나(`[1, d]`)라서 작다. 가중치만 4-bit로 저장하고, 계산할 때 레지스터 안에서 FP16으로 풀어서(dequant) 곱하면 된다. 이것이 **W4A16**이다.
- **활성값까지 정수로 바꾸려면(W8A8) outlier라는 벽이 있다.** LLM의 활성값에는 몇몇 채널만 수십~수천 배 큰 값이 있어서, 텐서 전체에 scale 하나를 쓰는 int8이 깨진다. SmoothQuant가 이 문제를 푼다.
- **4-bit에서는 단순 반올림(RTN)으로는 정확도가 떨어진다.** GPTQ(오차 보상)와 AWQ(중요 채널 보호)가 calibration 데이터를 써서 이를 줄인다.
- **context가 길어지면 KV-cache가 가중치만큼 커진다.** KV-cache도 int8/int4로 줄인다.

펌웨어 비유: SSD 펌웨어에서 "NAND 읽기 대역폭이 병목이면 압축해서 저장하고 컨트롤러에서 풀면 빨라진다"는 발상과 같다. W4A16은 **압축 저장 + on-the-fly 해제**이고, W8A8은 **연산 경로 자체를 정수로 바꾸는 것**이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300"><text x="20" y="28" font-size="14">W4A16 (weight-only) — decode에서 주력</text><rect x="20" y="45" width="130" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/><text x="85" y="66" font-size="12" text-anchor="middle">DRAM</text><text x="85" y="84" font-size="12" text-anchor="middle">int4 W + FP16 scale</text><line x1="150" y1="70" x2="215" y2="70" stroke="currentColor" stroke-width="1.5"/><polygon points="215,65 225,70 215,75" fill="currentColor"/><text x="187" y="60" font-size="12" text-anchor="middle">4.5 bit/w</text><rect x="225" y="45" width="140" height="50" rx="6" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/><text x="295" y="66" font-size="12" text-anchor="middle">레지스터에서 dequant</text><text x="295" y="84" font-size="12" text-anchor="middle">q·s → FP16</text><line x1="365" y1="70" x2="415" y2="70" stroke="currentColor" stroke-width="1.5"/><polygon points="415,65 425,70 415,75" fill="currentColor"/><rect x="425" y="45" width="110" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/><text x="480" y="66" font-size="12" text-anchor="middle">FP16 MAC</text><text x="480" y="84" font-size="12" text-anchor="middle">(FP32 누적)</text><line x1="535" y1="70" x2="585" y2="70" stroke="currentColor" stroke-width="1.5"/><polygon points="585,65 595,70 585,75" fill="currentColor"/><text x="630" y="74" font-size="12" text-anchor="middle">y FP16</text><text x="480" y="125" font-size="12" text-anchor="middle">x (FP16) 그대로</text><line x1="480" y1="112" x2="480" y2="100" stroke="currentColor" stroke-width="1.5"/><polygon points="475,100 480,95 485,100" fill="currentColor"/><text x="20" y="168" font-size="14">W8A8 — prefill·NPU 정수 경로</text><rect x="20" y="185" width="130" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/><text x="85" y="206" font-size="12" text-anchor="middle">DRAM</text><text x="85" y="224" font-size="12" text-anchor="middle">int8 W + 채널 scale</text><line x1="150" y1="210" x2="215" y2="210" stroke="currentColor" stroke-width="1.5"/><polygon points="215,205 225,210 215,215" fill="currentColor"/><text x="187" y="200" font-size="12" text-anchor="middle">8 bit/w</text><rect x="225" y="185" width="200" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/><text x="325" y="206" font-size="12" text-anchor="middle">int8 × int8 → int32 MAC</text><text x="325" y="224" font-size="12" text-anchor="middle">(DSP/NPU 정수 유닛)</text><line x1="425" y1="210" x2="465" y2="210" stroke="currentColor" stroke-width="1.5"/><polygon points="465,205 475,210 465,215" fill="currentColor"/><rect x="475" y="185" width="110" height="50" rx="6" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/><text x="530" y="206" font-size="12" text-anchor="middle">× s_x·s_w</text><text x="530" y="224" font-size="12" text-anchor="middle">requant (C1)</text><text x="325" y="272" font-size="12" text-anchor="middle">x (FP16) → int8 양자화 ← 여기서 outlier가 문제 (4절)</text><line x1="325" y1="258" x2="325" y2="240" stroke="#d0564a" stroke-width="1.5"/><polygon points="320,240 325,235 330,240" fill="#d0564a"/></svg>
```

그림 1 — 두 가지 LLM 양자화 경로. 위(W4A16)는 **메모리에서 읽는 바이트만 줄이고** 계산은 FP16으로 한다. decode처럼 memory-bound인 구간에서 이득이 거의 그대로 속도가 된다. 아래(W8A8)는 **계산까지 정수로** 한다. 활성값 x를 int8로 바꾸는 순간(빨간 화살표)이 LLM에서 가장 까다로운 지점이다.

이 노트의 지도:

| 절 | 질문 | 도구 |
|---|---|---|
| 1 | W4A16, W8A8 같은 이름은 무슨 뜻이고 몇 바이트인가 | 형식표, bit/weight 계산 |
| 2 | 양자화한 LLM이 얼마나 나빠졌는지 어떻게 재나 | perplexity |
| 3 | 가장 단순한 방법(RTN)은 어디까지 되나 | int8/int4, per-channel/per-group |
| 4 | 활성값까지 int8로 하면 왜 깨지나 | activation outlier |
| 5 | outlier를 어떻게 피하나 | SmoothQuant |
| 6, 7 | int4 가중치의 오차를 어떻게 줄이나 | GPTQ, AWQ |
| 8 | 실제 파일·커널은 어떻게 생겼나 | GGUF Q4_0/Q8_0/Q4_K, nibble packing |
| 9 | 긴 context의 KV-cache는 | KV-cache 양자화 |
| 10 | 기기에서는 무엇이 달라지나 | dequant 커널, 대역폭 계산 |

모든 실험은 로컬에 캐시된 **SmolLM2-135M-Instruct**(B8에서 쓴 모델, 30층, d = 576)로 한다. 이 모델은 작아서 CPU에서 몇 초 만에 실험이 끝나지만, **작은 모델은 큰 모델보다 양자화에 더 민감하다**는 점을 기억하자. 숫자의 절대값보다 **방법 간의 순서와 크기 차이**를 보는 것이 목적이다.

---

## 1. 형식 이름 읽기 — WxAy와 바이트 계산

### 1.1 표기법

LLM 양자화 문헌은 형식을 W(가중치 bit 수)A(활성값 bit 수)로 쓴다. KV-cache까지 말할 때는 `KV8`, `KV4`를 덧붙이기도 한다.

| 표기 | 가중치 | 활성값 | 행렬곱을 무엇으로 하나 | 주 용도 |
|---|---|---|---|---|
| W16A16 | FP16/BF16 | FP16/BF16 | FP16 MAC | 기준, GPU 서버 |
| W8A16 | INT8 | FP16 | dequant 후 FP16 MAC | 품질 우선 weight-only |
| W4A16 | INT4 (+group scale) | FP16 | dequant 후 FP16 MAC | **온디바이스 LLM의 기본값** |
| W8A8 | INT8 | INT8 | INT8 MAC, INT32 누적 | 정수 NPU/DSP, prefill 가속 |
| W4A8 | INT4 | INT8 | INT4를 INT8로 풀어 INT8 MAC | 둘의 절충 (커널이 복잡) |

C1에서 배운 양자화 기본식을 한 줄로 복습한다(자세한 유도는 C1).

```
양자화:   q = clamp(round(x / s) + z, qmin, qmax)
복원:     x̂ = s · (q − z)
대칭(symmetric): z = 0,  s = max|x| / qmax        (int4: qmax = 7, int8: qmax = 127)
비대칭(asymmetric): s = (max − min) / (2^b − 1),  z = round(−min / s)
```

말로 하면: 실수 구간을 2^b개 칸으로 자르고, 각 값을 가장 가까운 칸 번호로 바꾼다. scale s는 칸 하나의 폭, zero-point z는 "실수 0이 몇 번 칸인가"다. **scale 하나를 몇 개의 값이 공유하느냐**가 per-tensor / per-channel / per-group의 차이다.

### 1.2 bit/weight — scale도 메모리를 먹는다

per-group 양자화에서는 g개 가중치마다 FP16 scale(16 bit)이 하나 붙는다. 비대칭이면 zero-point도 붙는다.

```
대칭 group g:            bit/w = b + 16/g
비대칭 group g (z는 b-bit): bit/w = b + (16 + b)/g
```

손으로 계산:

- int4, g = 128, 대칭: `4 + 16/128 = 4.125`
- int4, g = 32, 대칭: `4 + 16/32 = 4.5` ← llama.cpp Q4_0과 같은 크기 (8절)
- int4, g = 64, 비대칭(z 4-bit): `4 + 20/64 = 4.3125`
- int8 per-channel (행 576개마다 scale 하나): `8 + 16/576 ≈ 8.03` → 사실상 8

말로 하면: group이 작을수록 정확도는 좋아지지만 scale 오버헤드가 커진다. g = 32면 12.5%가 scale이다.

### 1.3 코드로 확인: 1B 모델의 형식별 크기와 속도 상한 [독립]

Llama-3.2-1B(파라미터 1,235,814,400개, B8)를 형식별로 저장하면 몇 MiB이고, B8에서 쓴 웨어러블급 가정 대역폭(17.1 GB/s)에서 decode 상한이 몇 tok/s인지 계산하는 코드다(KV-cache는 9절에서 따로 더한다).

```python
P = 1_235_814_400                         # Llama-3.2-1B 파라미터 수 (B8 예제 3의 실측값)
BW = 17.1e9                               # 웨어러블급 가정 대역폭 (LPDDR4X-4266 x32, B8 4.5절)
fmts = [  # (이름, weight bit/param, activation 형식, 비고)
    ("W16A16 FP16",     16,           "FP16", "기준"),
    ("W8A16 per-ch",     8,           "FP16", "행마다 scale → 무시 가능"),
    ("W8A8 per-ch",      8,           "INT8", "정수 MAC 가능"),
    ("W4A16 g128",       4 + 16/128,  "FP16", "그룹 128개마다 FP16 scale"),
    ("W4A16 g32",        4 + 16/32,   "FP16", "= llama.cpp Q4_0 크기"),
    ("W4A8 g128",        4 + 16/128,  "INT8", "가중치는 W4A16과 같다"),
]
print(f"{'format':14s} {'bit/w':>6s} {'weights':>9s} {'vs FP16':>8s} {'tok/s 상한':>10s}  act")
for name, b, act, note in fmts:
    byt = P * b / 8
    print(f"{name:14s} {b:6.3f} {byt/2**20:6.0f} MiB {16/b:7.2f}x {BW/byt:9.1f}   {act}  ({note})")
```

```text
format          bit/w   weights  vs FP16   tok/s 상한  act
W16A16 FP16    16.000   2357 MiB    1.00x       6.9   FP16  (기준)
W8A16 per-ch    8.000   1179 MiB    2.00x      13.8   FP16  (행마다 scale → 무시 가능)
W8A8 per-ch     8.000   1179 MiB    2.00x      13.8   INT8  (정수 MAC 가능)
W4A16 g128      4.125    608 MiB    3.88x      26.8   FP16  (그룹 128개마다 FP16 scale)
W4A16 g32       4.500    663 MiB    3.56x      24.6   FP16  (= llama.cpp Q4_0 크기)
W4A8 g128       4.125    608 MiB    3.88x      26.8   INT8  (가중치는 W4A16과 같다)
```

출력에서 볼 것:

- 가중치 bit 수만 속도 상한을 정한다. **W8A16과 W8A8은 읽는 바이트가 같으므로 decode 상한도 같다.** W8A8의 장점은 memory-bound가 아닌 곳(prefill, 큰 batch)에서 정수 MAC으로 계산이 빨라지는 것이다.
- W4A16 g128은 FP16 대비 3.88배 작고, 상한이 6.9 → 26.8 tok/s가 된다. 이것이 **"W4A16이 온디바이스 LLM의 기본값"**인 이유다. 사람이 읽는 속도(초당 5~10 token) 근처에서 3~4배는 "못 쓴다"와 "쓸 만하다"의 차이다.
- W4A8은 가중치 크기가 W4A16과 같다. decode 속도에는 이득이 없고, prefill 연산이 정수가 되는 것이 이득이다.

### 1.4 prefill과 decode는 원하는 형식이 다르다

B8 7절의 prefill/decode 구분을 양자화 관점으로 다시 보면:

| 단계 | 병목 | 도움 되는 것 | 이유 |
|---|---|---|---|
| decode (token 1개씩) | 메모리 대역폭 | **weight bit 줄이기** (W4A16) | GEMV: 가중치 1바이트당 2 FLOP |
| prefill (프롬프트 한꺼번에) | 연산량 | **정수 MAC** (W8A8, W4A8) | GEMM: 가중치를 S번 재사용 |
| 긴 context decode | 대역폭 (가중치 + KV) | **KV-cache 양자화** | KV가 가중치에 버금감 (9절) |

음성 비서처럼 프롬프트가 짧고 답을 길게 생성하는 제품은 decode가 대부분이므로 W4A16이 가장 효과가 크다. 반대로 긴 문서를 요약하는(prefill이 긴) 작업은 W8A8의 정수 연산이 TTFT(첫 token까지 시간)를 줄인다.

---

## 2. 평가 — perplexity로 "얼마나 나빠졌나" 재기

### 2.1 정의

LLM은 매 위치에서 다음 token의 확률분포를 낸다. 정답 token에 준 확률을 p₁, p₂, …, p_N이라 하면:

```
NLL  = −(1/N) · ∑ᵢ ln pᵢ          (평균 negative log-likelihood = cross-entropy loss)
PPL  = exp(NLL)
```

말로 하면: perplexity는 "모델이 매번 평균 몇 개의 후보 사이에서 헷갈리는가"다. 매번 정답에 확률 1/4을 준다면 PPL = 4, 즉 4지선다를 찍는 수준이다. **낮을수록 좋다.**

손으로 계산: 정답 token 확률이 [0.5, 0.25, 0.125, 0.5]인 4-token 문장.

```
−ln 0.5 = 0.693,  −ln 0.25 = 1.386,  −ln 0.125 = 2.079,  −ln 0.5 = 0.693
NLL = (0.693 + 1.386 + 2.079 + 0.693) / 4 = 4.852 / 4 = 1.213
PPL = e^1.213 ≈ 3.36
```

양자화 평가에서는 **같은 텍스트에 대해 FP 모델과 양자화 모델의 PPL을 비교**한다. 절대값은 텍스트와 토크나이저에 따라 달라서 모델 간 비교에는 쓰면 안 된다.

### 2.2 이 노트의 실험 환경 [모델 필요]

아래 파일을 `c3_common.py`로 저장해 두고 이후 예제들이 import한다. eval 텍스트와 calibration 텍스트는 이 노트를 위해 직접 쓴 짧은 영어 글이다. **eval 522 token, calibration 483 token의 아주 작은 세트**라서 결과에 잡음이 있다(2.4절). calibration(양자화 파라미터를 정하는 데 쓰는 데이터)과 eval(평가 데이터)은 **반드시 다른 텍스트**를 쓴다. 같은 텍스트로 튜닝하고 평가하면 테스트 벡터에만 맞춘 펌웨어 튜닝과 같은 착시가 생긴다.

```python
import os, warnings
os.environ["HF_HUB_OFFLINE"] = "1"; warnings.filterwarnings("ignore")
import torch
from transformers import AutoTokenizer, AutoModelForCausalLM
torch.manual_seed(0); torch.set_num_threads(4)
MID = "HuggingFaceTB/SmolLM2-135M-Instruct"

EVAL_TEXT = """The lighthouse keeper woke before dawn, as he had done every morning for eleven years. He climbed the spiral stairs, counted the steps out of habit, and checked the lamp. The glass was clean, the oil was full, and the sea below was calm and grey. Far away, a fishing boat was already moving toward the harbor, its engine too distant to hear.
In the small town beside the lighthouse, the baker had started work even earlier. Bread needs time, she liked to say, and time cannot be rushed. She mixed flour, water, salt, and yeast, then left the dough to rest while she swept the floor. By six o'clock the first loaves were in the oven, and the smell of warm bread drifted down the narrow street.
A school teacher walked past the bakery on her way to work. She was thinking about the lesson she would give that day: how rivers carry sand and stones to the sea, and how, over thousands of years, those small grains build new land. Children often find geology boring, she thought, until they learn that the ground under their feet is still moving.
At the harbor, the fishing boat arrived and the crew began to unload their catch. The market opened at seven. Buyers from nearby restaurants argued about prices, checked the eyes of the fish for freshness, and carried boxes of ice to their vans. By eight o'clock most of the fish had been sold, and the crew went home to sleep.
Computers are now part of almost every job in the town. The baker uses a small tablet to track orders, the fishermen use radar and satellite navigation, and the teacher prepares her lessons on a laptop. Yet the basic work has not changed very much. Bread still needs flour and heat, fish still need to be caught, and children still need someone patient to explain the world to them.
In the afternoon the wind grew stronger. Clouds moved in from the west, and the sea turned dark. The lighthouse keeper watched the weather carefully, because storms can arrive quickly on this part of the coast. He wrote the wind speed and the air pressure in his notebook, as keepers have done for more than a hundred years.
When evening came, he lit the lamp. The beam swept across the water, once every ten seconds, warning ships away from the rocks. Somewhere out in the dark, a captain saw the light, checked his chart, and turned his ship a few degrees to the north. He would never meet the keeper, but tonight the keeper had helped him get home safely."""

CALIB_TEXT = """A modern battery management system measures the voltage of each cell many times per second. If one cell becomes weaker than the others, the system can reduce the charging current or warn the user. Engineers test these systems in hot and cold chambers, because chemical reactions inside the battery change with temperature.
The history of the printing press shows how a single technology can change a whole society. Before printing, books were copied by hand, which made them rare and expensive. After printing spread across Europe, ideas could travel faster than ever before, and more people learned to read.
Gardeners know that healthy soil is full of life. Worms, fungi, and tiny bacteria break down dead leaves and turn them into food for plants. A spoonful of good soil may contain more living things than there are people on Earth. Adding compost is one of the easiest ways to improve a garden.
When you travel by train, you can watch the landscape change slowly outside the window. Cities give way to farms, farms give way to forests, and sometimes the tracks follow a river for many miles. Many people prefer trains to planes because they can read, work, or simply rest during the journey.
Coffee plants grow best in mountains near the equator, where the days are warm and the nights are cool. The red fruit is picked by hand, dried in the sun, and the seeds inside are roasted until they turn brown. A light roast keeps more of the fruity taste, while a dark roast tastes bitter and smoky.
Software teams often write automated tests before they change old code. A good test runs quickly, checks one clear behavior, and fails with a message that explains what went wrong. When a test fails only sometimes, engineers call it flaky, and they usually treat it as a bug that must be fixed rather than ignored.
The moon has no air and no weather, so the footprints left by astronauts may stay there for millions of years. Its surface is covered with fine grey dust and craters made by ancient impacts. Scientists study rocks from the moon to learn how the Earth and the moon were formed.
Many birds fly thousands of kilometers every year between their summer and winter homes. They use the sun, the stars, and even the magnetic field of the Earth to find their way. Some small birds double their body weight before the journey, storing fat as fuel for the long flight over the sea."""

def load():
    tok = AutoTokenizer.from_pretrained(MID)
    model = AutoModelForCausalLM.from_pretrained(MID, dtype=torch.float32).eval()
    return tok, model

@torch.no_grad()
def ppl(model, ids, win=1024):
    """perplexity = exp(평균 NLL). 텍스트를 win 길이 창으로 잘라 각 창 안에서 next-token 예측."""
    nll, n = 0.0, 0
    for s in range(0, ids.shape[1] - 1, win):
        chunk = ids[:, s:s + win + 1]
        logits = model(chunk[:, :-1]).logits
        loss = torch.nn.functional.cross_entropy(logits[0], chunk[0, 1:], reduction="sum")
        nll, n = nll + loss.item(), n + chunk.shape[1] - 1
    return float(torch.exp(torch.tensor(nll / n)))

def linears(model):
    """디코더 블록 안의 Linear 7종 (q,k,v,o,gate,up,down). lm_head는 embedding과 공유라 제외."""
    return {n: m for n, m in model.model.layers.named_modules() if isinstance(m, torch.nn.Linear)}
```

기준 perplexity를 재고, 양자화 대상인 Linear 층이 파라미터의 몇 %인지 보는 코드다.

```python
import torch
from c3_common import load, ppl, linears, EVAL_TEXT, CALIB_TEXT
tok, model = load()
ids = tok(EVAL_TEXT, return_tensors="pt").input_ids
print("eval tokens:", ids.shape[1], " calib tokens:", len(tok(CALIB_TEXT).input_ids))
p = ppl(model, ids)
print(f"FP32 perplexity: {p:.3f}")
L = linears(model)
nw = sum(m.weight.numel() for m in L.values())
print(f"Linear {len(L)}개, 가중치 {nw:,}개 = 전체 파라미터의 {nw/sum(p.numel() for p in model.parameters()):.1%}")
```

```text
eval tokens: 522  calib tokens: 483
FP32 perplexity: 18.101
Linear 210개, 가중치 106,168,320개 = 전체 파라미터의 78.9%
```

출력에서 볼 것:

- FP32 기준 PPL은 **18.101**이다. 이후 모든 결과를 이 숫자와 비교한다.
- 디코더 블록 안의 Linear 210개(30층 × q, k, v, o, gate, up, down)가 파라미터의 78.9%다. 나머지 21%는 embedding 표(lm_head와 공유, B8 3.4절)다. 이 노트의 실험은 **Linear 210개만 양자화하고 embedding/lm_head는 FP32로 둔다.** 실제 배포에서는 embedding도 8-bit나 6-bit 정도로 줄이는 경우가 많다.

### 2.3 fake quantization — 정수 커널 없이 정확도만 보기

실험에서는 진짜 int4 커널을 쓰지 않는다. 가중치를 `양자화 → 즉시 복원(dequant)`해서 FP32 텐서에 다시 넣는다. 이것을 **fake quantization**(simulated quantization)이라 한다. 수치적으로는 "int4 격자 위의 값만 쓰는 FP32 모델"이므로 **정확도 영향은 진짜 int4 커널과 거의 같다**(누적 순서 차이 정도). 속도와 메모리 이득은 fake quant로는 볼 수 없다. 속도는 8절의 커널, 10절의 대역폭 계산으로 따로 본다.

### 2.4 perplexity의 함정

- **작은 eval 세트는 잡음이 크다.** 3절에서 int8 PPL(18.063)이 FP32(18.101)보다 **낮게** 나온다. int8이 더 좋아진 것이 아니라 522 token 위에서의 우연이다. 이 노트에서는 0.1~0.5 정도 차이는 무시하고, 수 단위 이상의 차이만 해석한다.
- 논문은 보통 WikiText-2나 C4에서 2048-token 창으로 PPL을 잰다. 논문 숫자와 이 노트 숫자를 직접 비교하면 안 된다.
- PPL이 거의 같아도 **특정 능력(수학, 코드, 다국어, 긴 문맥)**이 먼저 망가질 수 있다. 실무에서는 PPL + 과제 정확도(예: lm-evaluation-harness의 과제들) + 제품 고유 테스트(예: 음성 명령 파싱 정답률)를 같이 본다(C8).
- greedy 생성 결과를 FP 모델과 비교하는 것도 싸고 유용한 신호다(9절에서 해 본다). 단, 확률이 비슷한 두 token 사이에서는 작은 오차로도 갈라지므로 "처음 몇 token이 같은가"는 거친 지표다.

---

## 3. RTN weight-only 양자화 — 가장 단순한 기준선

### 3.1 직관과 정의

**RTN(round-to-nearest)**은 C1에서 배운 그대로다. 각 가중치를 scale로 나누고 가장 가까운 정수로 반올림한다. 데이터(calibration)가 필요 없고, 가중치만 보고 끝난다. LLM 양자화 논문들은 모두 RTN을 기준선으로 놓고 "우리 방법이 RTN보다 얼마나 나은가"를 보인다.

Linear 가중치 `W: [out, in]`에서 scale을 나누는 단위:

| 단위 | scale 하나를 공유하는 값 | scale 개수 (576×576 층) | 비고 |
|---|---|---|---|
| per-tensor | 행렬 전체 | 1 | LLM 가중치에는 너무 거칠다 |
| per-channel | 한 행(출력 채널) 576개 | 576 | int8에서는 충분 |
| per-group g | 한 행 안의 연속 g개 | 576 × 576/g | int4의 표준 (g = 32~128) |

### 3.2 손으로 계산: 한 행 8개, per-channel vs group 4 [독립]

행 하나 `w = [0.12, −0.05, 0.30, 0.02, −0.90, 0.07, 0.01, −0.04]`를 int4 대칭(q ∈ [−7, 7])으로 양자화한다.

- per-channel: s = 0.90/7 = 0.1286. 0.12 → round(0.93) = 1, −0.05 → round(−0.39) = 0, 0.30 → round(2.33) = 2, 0.02 → 0. **앞 4개 중 두 개가 0이 됐다.** −0.90 하나가 scale을 크게 만들어서 작은 값들이 칸 하나(0.1286)보다 작아졌기 때문이다.
- group 4: 앞 4개는 s = 0.30/7 = 0.0429 → q = [3, −1, 7, 0], 뒤 4개는 s = 0.1286 그대로. 앞 group은 칸이 3배 촘촘해져서 −0.05도 살아남는다.

```python
import numpy as np
w = np.array([0.12, -0.05, 0.30, 0.02, -0.90, 0.07, 0.01, -0.04])   # 한 행의 weight 8개
def q4(v):                                   # int4 대칭: scale = max|v| / 7, q ∈ [-7, 7]
    s = np.abs(v).max() / 7
    q = np.clip(np.round(v / s), -7, 7)
    return q.astype(int), s, q * s
q, s, dq = q4(w)                             # per-channel: 행 전체에 scale 1개
print(f"per-channel  s={s:.4f} q={q.tolist()}  |err| 합={np.abs(w - dq).sum():.4f}")
parts = [q4(w[i:i + 4]) for i in (0, 4)]     # group 4: 4개마다 scale 1개
for i, (q, s, dq) in enumerate(parts):
    print(f"group {i}      s={s:.4f} q={q.tolist()}")
dq_g = np.concatenate([p[2] for p in parts])
print(f"group=4 |err| 합={np.abs(w - dq_g).sum():.4f}")
print("앞 4개 복원값  per-ch:", np.round(q4(w)[2][:4], 4) + 0.0, " group:", np.round(dq_g[:4], 4) + 0.0)
```

```text
per-channel  s=0.1286 q=[1, 0, 2, 0, -7, 1, 0, 0]  |err| 합=0.2300
group 0      s=0.0429 q=[3, -1, 7, 0]
group 1      s=0.1286 q=[-7, 1, 0, 0]
group=4 |err| 합=0.1443
앞 4개 복원값  per-ch: [0.1286 0.     0.2571 0.    ]  group: [ 0.1286 -0.0429  0.3     0.    ]
```

출력에서 볼 것: 절대 오차 합이 0.230 → 0.144로 줄었다. 큰 값 하나가 scale을 결정하는 문제를 **scale을 공유하는 범위를 좁혀서** 푸는 것이 per-group의 원리다. 이것은 4절 activation outlier 문제의 가중치 버전이기도 하다.

### 3.3 코드로 확인: SmolLM2 전체에 RTN [모델 필요]

Linear 210개의 가중치를 int8 per-channel, int4 per-channel, int4 per-group(g = 128/64/32), 대칭/비대칭으로 바꿔 가며 PPL을 잰다.

```python
import torch
from c3_common import load, ppl, linears, EVAL_TEXT
tok, model = load(); ids = tok(EVAL_TEXT, return_tensors="pt").input_ids
orig = {n: m.weight.data.clone() for n, m in linears(model).items()}

def rtn(w, bits, gs=None, sym=True):
    """RTN fake-quant. 행(출력 채널)마다 또는 행 안의 gs개씩 scale 하나. 결과는 dequant된 fp32."""
    rows, cols = w.shape; gs = gs or cols
    g = torch.nn.functional.pad(w, (0, (-cols) % gs)).reshape(rows, -1, gs)  # 576은 128로 안 나눠져 0 패딩
    if sym:                                              # 대칭: q ∈ [-(2^(b-1)-1), 2^(b-1)-1], zero-point 없음
        qmax = 2 ** (bits - 1) - 1
        s = g.abs().amax(-1, keepdim=True).clamp(min=1e-8) / qmax
        dq = torch.clamp(torch.round(g / s), -qmax, qmax) * s
    else:                                                # 비대칭: q ∈ [0, 2^b-1], zero-point z
        lo, hi = g.amin(-1, keepdim=True).clamp(max=0), g.amax(-1, keepdim=True).clamp(min=0)  # 0 포함
        s = ((hi - lo) / (2 ** bits - 1)).clamp(min=1e-8); z = torch.round(-lo / s)
        dq = (torch.clamp(torch.round(g / s) + z, 0, 2 ** bits - 1) - z) * s
    return dq.reshape(rows, -1)[:, :cols]

print(f"{'format':24s} {'bits/w':>6s} {'ppl':>8s}")
print(f"{'FP32 (기준)':24s} {32:6.2f} {ppl(model, ids):8.3f}")
for bits, gs, sym in [(8, None, True), (4, None, True), (4, 128, True), (4, 64, True), (4, 32, True),
                      (4, None, False), (4, 128, False), (4, 64, False), (4, 32, False)]:
    for n, m in linears(model).items():
        m.weight.data = rtn(orig[n], bits, gs, sym)
    bpw = bits + ((16 if sym else 16 + bits) / gs if gs else 0)  # FP16 scale (+ b-bit zero-point) / 그룹
    name = f"int{bits} {'g' + str(gs) if gs else 'per-channel'} {'sym' if sym else 'asym'}"
    print(f"{name:24s} {bpw:6.3f} {ppl(model, ids):8.3f}")
```

```text
format                   bits/w      ppl
FP32 (기준)                 32.00   18.101
int8 per-channel sym      8.000   18.063
int4 per-channel sym      4.000   44.663
int4 g128 sym             4.125   27.628
int4 g64 sym              4.250   25.437
int4 g32 sym              4.500   25.611
int4 per-channel asym     4.000   33.144
int4 g128 asym            4.156   24.679
int4 g64 asym             4.312   22.663
int4 g32 asym             4.625   21.286
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 400"><text x="182" y="35" font-size="12" text-anchor="end">FP32 (기준)</text><rect x="190" y="20" width="148.4" height="20" fill="#888" fill-opacity="0.8"/><text x="344.4" y="35" font-size="12">18.10  (32 bit/w)</text><text x="182" y="67" font-size="12" text-anchor="end">int8 per-channel sym</text><rect x="190" y="52" width="148.1" height="20" fill="#3f9a6b" fill-opacity="0.8"/><text x="344.1" y="67" font-size="12">18.06  (8 bit/w)</text><text x="182" y="99" font-size="12" text-anchor="end">int4 per-channel sym</text><rect x="190" y="84" width="366.2" height="20" fill="#d0564a" fill-opacity="0.8"/><text x="562.2" y="99" font-size="12">44.66  (4 bit/w)</text><text x="182" y="131" font-size="12" text-anchor="end">int4 g128 sym</text><rect x="190" y="116" width="226.5" height="20" fill="#e08a3c" fill-opacity="0.8"/><text x="422.5" y="131" font-size="12">27.63  (4.125 bit/w)</text><text x="182" y="163" font-size="12" text-anchor="end">int4 g64 sym</text><rect x="190" y="148" width="208.6" height="20" fill="#e08a3c" fill-opacity="0.8"/><text x="404.6" y="163" font-size="12">25.44  (4.25 bit/w)</text><text x="182" y="195" font-size="12" text-anchor="end">int4 g32 sym</text><rect x="190" y="180" width="210.0" height="20" fill="#e08a3c" fill-opacity="0.8"/><text x="406.0" y="195" font-size="12">25.61  (4.5 bit/w)</text><text x="182" y="227" font-size="12" text-anchor="end">int4 per-channel asym</text><rect x="190" y="212" width="271.8" height="20" fill="#d0564a" fill-opacity="0.8"/><text x="467.8" y="227" font-size="12">33.14  (4 bit/w)</text><text x="182" y="259" font-size="12" text-anchor="end">int4 g128 asym</text><rect x="190" y="244" width="202.4" height="20" fill="#4a7bd0" fill-opacity="0.8"/><text x="398.4" y="259" font-size="12">24.68  (4.156 bit/w)</text><text x="182" y="291" font-size="12" text-anchor="end">int4 g64 asym</text><rect x="190" y="276" width="185.8" height="20" fill="#4a7bd0" fill-opacity="0.8"/><text x="381.8" y="291" font-size="12">22.66  (4.312 bit/w)</text><text x="182" y="323" font-size="12" text-anchor="end">int4 g32 asym</text><rect x="190" y="308" width="174.5" height="20" fill="#4a7bd0" fill-opacity="0.8"/><text x="370.5" y="323" font-size="12">21.29  (4.625 bit/w)</text><line x1="190" y1="344" x2="600" y2="344" stroke="currentColor"/><line x1="190.0" y1="344" x2="190.0" y2="349" stroke="currentColor"/><text x="190.0" y="363" font-size="12" text-anchor="middle">0</text><line x1="272.0" y1="344" x2="272.0" y2="349" stroke="currentColor"/><text x="272.0" y="363" font-size="12" text-anchor="middle">10</text><line x1="354.0" y1="344" x2="354.0" y2="349" stroke="currentColor"/><text x="354.0" y="363" font-size="12" text-anchor="middle">20</text><line x1="436.0" y1="344" x2="436.0" y2="349" stroke="currentColor"/><text x="436.0" y="363" font-size="12" text-anchor="middle">30</text><line x1="518.0" y1="344" x2="518.0" y2="349" stroke="currentColor"/><text x="518.0" y="363" font-size="12" text-anchor="middle">40</text><line x1="600.0" y1="344" x2="600.0" y2="349" stroke="currentColor"/><text x="600.0" y="363" font-size="12" text-anchor="middle">50</text><line x1="338.4" y1="14" x2="338.4" y2="344" stroke="#888" stroke-dasharray="4 3"/><text x="395" y="382" font-size="12" text-anchor="middle">perplexity (낮을수록 좋음) — SmolLM2-135M-Instruct, 522-token 미니 eval</text></svg>
```

그림 2 — RTN 형식별 perplexity (실측, FP32 기준 18.10은 점선). int8은 기준과 구별되지 않고, int4 per-channel은 크게 망가지며, group을 작게 할수록, 비대칭일수록 회복된다.

출력에서 볼 것:

- **int8 per-channel은 사실상 무손실**(18.06 vs 18.10, 차이는 잡음). LLM 가중치의 int8 weight-only는 거의 공짜다.
- **int4 per-channel은 크게 망가진다**(대칭 44.7, 비대칭 33.1). 한 행 576개 중 가장 큰 값 하나가 칸 폭을 정하므로 대부분의 작은 가중치가 몇 칸에 몰린다(3.2절의 현상).
- **group을 줄이면 회복된다.** 비대칭 기준 g128 24.7 → g64 22.7 → g32 21.3. bit/weight는 4.16 → 4.31 → 4.63으로 늘어난다. 정확도와 저장 크기의 trade-off다.
- **비대칭이 대칭보다 낫다.** 대칭 int4는 −8을 안 쓰고 [−7, 7]의 15칸만 쓰며, 가중치 분포가 group 안에서 한쪽으로 치우치면 절반 가까이를 낭비한다. 비대칭은 16칸을 [min, max]에 딱 맞춘다. 대가는 zero-point 저장과 커널에서의 뺄셈 한 번이다.
- 대칭 g32(25.6)가 g64(25.4)보다 약간 나쁘게 나온 것은 작은 eval 세트의 잡음 범위다.
- **그래도 int4 RTN은 FP32보다 확실히 나쁘다**(가장 좋은 21.3도 +18%). 135M 모델은 큰 모델보다 4-bit에 민감하다. 이 차이를 calibration 데이터로 줄이는 것이 6절(GPTQ)과 7절(AWQ)이다.

### 3.4 임베디드 연결

- **group 크기는 커널의 SIMD 폭·메모리 정렬과 맞물린다.** group 32 = int4 32개 = 16바이트 = NEON 레지스터 하나(128-bit)다. scale을 group마다 한 번 곱하므로 group이 너무 작으면 곱셈 오버헤드가, 너무 크면 정확도가 문제가 된다.
- **나눠떨어지지 않는 차원.** SmolLM2의 d = 576은 128로 나눠지지 않는다(576 = 4.5 × 128). 위 코드는 마지막 group을 0으로 패딩했다. 실제 도구들은 이런 경우 다른 형식으로 fallback하거나 오류를 낸다. 예를 들어 llama.cpp의 K-quant(Q4_K 등)는 256개 단위 super-block을 쓰므로, 행 길이가 256의 배수가 아닌 텐서는 다른 양자화 형식으로 대체하는 것으로 알고 있다(버전별 동작은 F3에서 직접 확인). **모델 차원을 64/128/256의 배수로 고르는 것**이 HW-friendly 설계의 한 요소다(C6, I).
- 펌웨어 비유: per-group scale은 **블록 부동소수점(block floating point)**과 같다. DSP에서 FFT 버퍼 블록마다 공통 지수를 하나 두는 것처럼, 가중치 32개마다 공통 scale을 하나 둔다.

### 3.5 함정

- lm_head/embedding을 같이 int4로 만들면 PPL이 추가로 크게 나빠지는 경우가 많다. 어느 층을 양자화했는지 항상 명시한다.
- int4 per-channel 결과만 보고 "int4는 안 된다"고 결론 내리면 안 된다. group과 비대칭, 그리고 GPTQ/AWQ까지 가야 int4의 실력이 나온다.

---

## 4. Activation outlier — W8A8이 어려운 이유

### 4.1 직관: 한 채널이 ADC 범위를 잡아먹는다

Don이 RF 칩에서 본 상황을 떠올리자. 수신 신호에 강한 간섭(blocker) 하나가 섞이면 AGC가 그 간섭에 맞춰 gain을 낮추고, 원하는 약한 신호는 ADC의 하위 몇 비트에 묻힌다. LLM 활성값이 정확히 이렇다. 576개 채널 중 몇 개가 다른 채널보다 수십~수천 배 크면, **텐서 전체에 scale 하나(per-tensor)**를 쓰는 int8 양자화는 그 큰 값에 맞춰 칸 폭을 잡고, 나머지 채널은 0 근처 몇 칸에 뭉개진다.

LLM.int8() 논문(Dettmers et al., 2022)은 모델이 약 6.7B 규모를 넘으면 **특정 feature 차원(채널)에 체계적으로 큰 값**이 나타나고, 이것이 int8 추론을 깨뜨린다고 보고했다. 작은 모델에서도 비슷한 현상이 있는지 직접 보자.

### 4.2 코드로 확인: 실제 활성값에서 outlier 찾기 [모델 필요]

forward hook으로 Linear 층의 **입력** 활성값을 잡아, 채널별 최대 절댓값을 본다. 두 층은 자세히 보고, 30개 층 q_proj 입력에서는 가장 큰 채널이 어디인지 센다.

```python
import torch
from collections import Counter
from c3_common import load, linears, CALIB_TEXT
tok, model = load(); ids = tok(CALIB_TEXT, return_tensors="pt").input_ids
L = linears(model); acts = {}
for n, m in L.items():                                  # 모든 q_proj와 11층 down_proj의 '입력'을 잡는다
    if n.endswith("q_proj") or n == "11.mlp.down_proj":
        m.register_forward_hook(lambda m, i, o, n=n: acts.__setitem__(n, i[0][0].detach()))
with torch.no_grad(): model(ids)

for n in ("11.mlp.down_proj", "12.self_attn.q_proj"):
    x = acts[n]                                         # x: [tokens, in_features]
    cmax = x.abs().amax(dim=0)                          # 채널(열)별 |x| 최대
    top = torch.topk(cmax, 3)
    print(f"{n}: x {tuple(x.shape)}, 채널별 max의 중앙값 {cmax.median():.2f}")
    print("   top-3 채널:", [(int(c), round(float(v), 1)) for c, v in zip(top.indices, top.values)])
    t = int(x[:, top.indices[0]].abs().argmax())
    print(f"   최대값 위치: token #{t} {tok.decode(ids[0, t])!r}, 이 채널의 token별 |x| 중앙값 "
          f"{x[:, top.indices[0]].abs().median():.2f}")
    s = x.abs().max() / 127                             # per-tensor int8 scale
    alive = int((x.abs() >= s / 2).sum())               # 0이 아닌 정수로 남는 원소 수
    print(f"   per-tensor int8 scale = {s:.4f} → 0이 아닌 값으로 남는 원소 {alive:,} / {x.numel():,}")
tops = Counter(int(acts[f"{i}.self_attn.q_proj"].abs().amax(0).argmax()) for i in range(30))
print("30개 층 q_proj 입력의 top-1 채널 빈도:", tops.most_common())
```

```text
11.mlp.down_proj: x (483, 1536), 채널별 max의 중앙값 0.89
   top-3 채널: [(1229, 3286.1), (1487, 1234.4), (213, 10.0)]
   최대값 위치: token #0 'A', 이 채널의 token별 |x| 중앙값 0.00
   per-tensor int8 scale = 25.8744 → 0이 아닌 값으로 남는 원소 2 / 741,888
12.self_attn.q_proj: x (483, 576), 채널별 max의 중앙값 0.39
   top-3 채널: [(247, 11.9), (308, 2.9), (507, 2.4)]
   최대값 위치: token #449 ' the', 이 채널의 token별 |x| 중앙값 8.14
   per-tensor int8 scale = 0.0937 → 0이 아닌 값으로 남는 원소 185,135 / 278,208
30개 층 q_proj 입력의 top-1 채널 빈도: [(247, 16), (507, 5), (17, 5), (260, 3), (471, 1)]
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 420"><text x="70" y="25" font-size="13">layers.12.self_attn.q_proj 입력 — 채널별 max|x| (576채널, 선형 축) · 중앙값 0.39</text><line x1="70" y1="170" x2="660" y2="170" stroke="currentColor"/><line x1="70" y1="40" x2="70" y2="170" stroke="currentColor"/><line x1="66" y1="170.0" x2="70" y2="170.0" stroke="currentColor"/><text x="62" y="174.0" font-size="12" text-anchor="end">0</text><line x1="66" y1="126.7" x2="70" y2="126.7" stroke="currentColor"/><text x="62" y="130.7" font-size="12" text-anchor="end">4</text><line x1="66" y1="83.3" x2="70" y2="83.3" stroke="currentColor"/><text x="62" y="87.3" font-size="12" text-anchor="end">8</text><line x1="66" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="62" y="44.0" font-size="12" text-anchor="end">12</text><path d="M70.5 170.0V166.2M71.5 170.0V164.8M72.6 170.0V165.8M73.6 170.0V166.0M74.6 170.0V166.3M75.6 170.0V165.5M76.7 170.0V164.6M77.7 170.0V166.6M78.7 170.0V157.6M79.7 170.0V166.6M80.8 170.0V165.4M81.8 170.0V164.1M82.8 170.0V165.7M83.8 170.0V165.1M84.9 170.0V145.1M85.9 170.0V166.6M86.9 170.0V165.0M87.9 170.0V163.4M88.9 170.0V165.3M90.0 170.0V166.0M91.0 170.0V165.6M92.0 170.0V165.0M93.0 170.0V165.3M94.1 170.0V164.7M95.1 170.0V165.4M96.1 170.0V163.1M97.1 170.0V164.1M98.2 170.0V166.4M99.2 170.0V167.0M100.2 170.0V165.4M101.2 170.0V162.1M102.3 170.0V166.2M103.3 170.0V166.8M104.3 170.0V164.3M105.3 170.0V165.1M106.4 170.0V164.9M107.4 170.0V165.6M108.4 170.0V166.2M109.4 170.0V163.8M110.5 170.0V164.7M111.5 170.0V165.6M112.5 170.0V165.3M113.5 170.0V165.9M114.6 170.0V166.4M115.6 170.0V166.2M116.6 170.0V164.4M117.6 170.0V164.3M118.7 170.0V165.1M119.7 170.0V166.4M120.7 170.0V167.1M121.7 170.0V162.7M122.8 170.0V166.1M123.8 170.0V166.5M124.8 170.0V167.2M125.8 170.0V165.4M126.8 170.0V166.3M127.9 170.0V162.5M128.9 170.0V164.7M129.9 170.0V163.7M130.9 170.0V165.2M132.0 170.0V165.8M133.0 170.0V165.5M134.0 170.0V166.0M135.0 170.0V166.2M136.1 170.0V166.6M137.1 170.0V165.4M138.1 170.0V166.1M139.1 170.0V165.7M140.2 170.0V165.5M141.2 170.0V165.7M142.2 170.0V164.7M143.2 170.0V167.5M144.3 170.0V165.5M145.3 170.0V165.9M146.3 170.0V165.0M147.3 170.0V165.8M148.4 170.0V166.6M149.4 170.0V164.5M150.4 170.0V166.5M151.4 170.0V165.7M152.5 170.0V166.3M153.5 170.0V166.2M154.5 170.0V163.5M155.5 170.0V164.2M156.6 170.0V166.3M157.6 170.0V165.7M158.6 170.0V166.3M159.6 170.0V165.8M160.7 170.0V165.8M161.7 170.0V166.0M162.7 170.0V165.6M163.7 170.0V166.1M164.7 170.0V166.1M165.8 170.0V167.1M166.8 170.0V166.1M167.8 170.0V166.3M168.8 170.0V164.4M169.9 170.0V162.9M170.9 170.0V166.7M171.9 170.0V165.4M172.9 170.0V159.0M174.0 170.0V165.6M175.0 170.0V164.3M176.0 170.0V166.3M177.0 170.0V165.2M178.1 170.0V166.2M179.1 170.0V167.1M180.1 170.0V164.8M181.1 170.0V167.1M182.2 170.0V167.4M183.2 170.0V165.7M184.2 170.0V166.4M185.2 170.0V166.2M186.3 170.0V166.3M187.3 170.0V165.6M188.3 170.0V165.8M189.3 170.0V165.4M190.4 170.0V165.7M191.4 170.0V159.1M192.4 170.0V164.9M193.4 170.0V166.2M194.5 170.0V166.8M195.5 170.0V166.0M196.5 170.0V164.0M197.5 170.0V164.5M198.6 170.0V164.8M199.6 170.0V165.7M200.6 170.0V167.3M201.6 170.0V165.3M202.6 170.0V163.6M203.7 170.0V165.8M204.7 170.0V165.6M205.7 170.0V166.2M206.7 170.0V166.4M207.8 170.0V165.1M208.8 170.0V163.9M209.8 170.0V165.6M210.8 170.0V165.5M211.9 170.0V166.1M212.9 170.0V164.6M213.9 170.0V164.1M214.9 170.0V165.1M216.0 170.0V166.0M217.0 170.0V166.4M218.0 170.0V165.5M219.0 170.0V167.8M220.1 170.0V164.7M221.1 170.0V166.4M222.1 170.0V165.1M223.1 170.0V165.9M224.2 170.0V166.7M225.2 170.0V164.8M226.2 170.0V166.3M227.2 170.0V166.3M228.3 170.0V165.5M229.3 170.0V166.0M230.3 170.0V166.0M231.3 170.0V159.7M232.4 170.0V166.5M233.4 170.0V166.2M234.4 170.0V165.7M235.4 170.0V163.7M236.4 170.0V167.1M237.5 170.0V165.6M238.5 170.0V165.7M239.5 170.0V166.1M240.5 170.0V166.3M241.6 170.0V165.8M242.6 170.0V165.7M243.6 170.0V165.4M244.6 170.0V161.8M245.7 170.0V166.1M246.7 170.0V164.2M247.7 170.0V166.4M248.7 170.0V166.9M249.8 170.0V165.5M250.8 170.0V164.5M251.8 170.0V165.3M252.8 170.0V162.1M253.9 170.0V165.7M254.9 170.0V166.3M255.9 170.0V164.9M256.9 170.0V165.5M258.0 170.0V165.1M259.0 170.0V165.9M260.0 170.0V165.9M261.0 170.0V165.2M262.1 170.0V167.3M263.1 170.0V165.5M264.1 170.0V165.5M265.1 170.0V166.4M266.2 170.0V166.1M267.2 170.0V166.7M268.2 170.0V164.5M269.2 170.0V166.2M270.3 170.0V165.9M271.3 170.0V165.4M272.3 170.0V166.4M273.3 170.0V165.4M274.3 170.0V165.5M275.4 170.0V165.1M276.4 170.0V165.4M277.4 170.0V167.0M278.4 170.0V166.3M279.5 170.0V165.6M280.5 170.0V163.8M281.5 170.0V164.9M282.5 170.0V165.9M283.6 170.0V164.9M284.6 170.0V165.8M285.6 170.0V166.1M286.6 170.0V160.1M287.7 170.0V164.7M288.7 170.0V165.9M289.7 170.0V166.4M290.7 170.0V164.2M291.8 170.0V166.4M292.8 170.0V166.0M293.8 170.0V166.1M294.8 170.0V164.5M295.9 170.0V165.2M296.9 170.0V165.7M297.9 170.0V165.7M298.9 170.0V166.3M300.0 170.0V164.3M301.0 170.0V165.3M302.0 170.0V166.3M303.0 170.0V164.7M304.1 170.0V164.8M305.1 170.0V165.1M306.1 170.0V149.2M307.1 170.0V165.8M308.2 170.0V164.4M309.2 170.0V165.8M310.2 170.0V166.3M311.2 170.0V166.2M312.2 170.0V167.7M313.3 170.0V167.3M314.3 170.0V163.8M315.3 170.0V166.8M316.3 170.0V166.8M317.4 170.0V165.9M318.4 170.0V164.1M319.4 170.0V160.5M320.4 170.0V166.7M321.5 170.0V164.5M322.5 170.0V165.2M323.5 170.0V41.1M324.5 170.0V154.7M325.6 170.0V165.9M326.6 170.0V166.2M327.6 170.0V166.1M328.6 170.0V166.2M329.7 170.0V165.3M330.7 170.0V166.2M331.7 170.0V166.0M332.7 170.0V165.6M333.8 170.0V166.8M334.8 170.0V167.1M335.8 170.0V165.0M336.8 170.0V166.9M337.9 170.0V152.9M338.9 170.0V164.8M339.9 170.0V165.4M340.9 170.0V165.2M342.0 170.0V165.3M343.0 170.0V166.0M344.0 170.0V165.9M345.0 170.0V163.8M346.1 170.0V166.1M347.1 170.0V165.3M348.1 170.0V166.3M349.1 170.0V165.7M350.1 170.0V165.8M351.2 170.0V165.9M352.2 170.0V165.2M353.2 170.0V166.0M354.2 170.0V166.3M355.3 170.0V166.2M356.3 170.0V165.7M357.3 170.0V166.0M358.3 170.0V165.0M359.4 170.0V166.9M360.4 170.0V166.8M361.4 170.0V163.0M362.4 170.0V160.7M363.5 170.0V165.0M364.5 170.0V165.4M365.5 170.0V166.5M366.5 170.0V164.3M367.6 170.0V165.3M368.6 170.0V167.5M369.6 170.0V166.7M370.6 170.0V166.2M371.7 170.0V165.4M372.7 170.0V165.7M373.7 170.0V166.1M374.7 170.0V166.5M375.8 170.0V164.1M376.8 170.0V163.5M377.8 170.0V165.4M378.8 170.0V164.1M379.9 170.0V165.8M380.9 170.0V166.4M381.9 170.0V165.5M382.9 170.0V166.3M383.9 170.0V163.8M385.0 170.0V164.5M386.0 170.0V139.1M387.0 170.0V165.2M388.0 170.0V165.1M389.1 170.0V167.0M390.1 170.0V165.2M391.1 170.0V166.9M392.1 170.0V166.4M393.2 170.0V166.8M394.2 170.0V165.9M395.2 170.0V165.3M396.2 170.0V164.9M397.3 170.0V166.1M398.3 170.0V164.9M399.3 170.0V164.2M400.3 170.0V165.5M401.4 170.0V164.5M402.4 170.0V165.2M403.4 170.0V165.9M404.4 170.0V165.4M405.5 170.0V166.1M406.5 170.0V166.1M407.5 170.0V166.5M408.5 170.0V166.1M409.6 170.0V165.6M410.6 170.0V165.4M411.6 170.0V166.4M412.6 170.0V163.8M413.7 170.0V166.4M414.7 170.0V166.9M415.7 170.0V166.3M416.7 170.0V166.5M417.8 170.0V165.7M418.8 170.0V166.0M419.8 170.0V163.7M420.8 170.0V167.4M421.8 170.0V166.2M422.9 170.0V164.8M423.9 170.0V167.7M424.9 170.0V165.7M425.9 170.0V166.7M427.0 170.0V165.8M428.0 170.0V165.3M429.0 170.0V159.2M430.0 170.0V164.7M431.1 170.0V166.4M432.1 170.0V165.7M433.1 170.0V165.7M434.1 170.0V165.6M435.2 170.0V165.5M436.2 170.0V166.1M437.2 170.0V166.2M438.2 170.0V165.0M439.3 170.0V165.4M440.3 170.0V166.6M441.3 170.0V165.9M442.3 170.0V165.7M443.4 170.0V166.2M444.4 170.0V166.6M445.4 170.0V165.8M446.4 170.0V166.4M447.5 170.0V165.8M448.5 170.0V166.1M449.5 170.0V166.8M450.5 170.0V164.3M451.6 170.0V166.1M452.6 170.0V165.9M453.6 170.0V166.3M454.6 170.0V165.5M455.7 170.0V165.7M456.7 170.0V165.2M457.7 170.0V165.8M458.7 170.0V165.3M459.7 170.0V166.0M460.8 170.0V166.3M461.8 170.0V166.1M462.8 170.0V166.0M463.8 170.0V164.9M464.9 170.0V166.2M465.9 170.0V166.6M466.9 170.0V165.8M467.9 170.0V166.2M469.0 170.0V164.8M470.0 170.0V165.6M471.0 170.0V166.2M472.0 170.0V166.1M473.1 170.0V165.4M474.1 170.0V165.4M475.1 170.0V166.6M476.1 170.0V164.1M477.2 170.0V167.2M478.2 170.0V165.2M479.2 170.0V165.4M480.2 170.0V166.8M481.3 170.0V164.6M482.3 170.0V166.2M483.3 170.0V165.0M484.3 170.0V161.6M485.4 170.0V167.0M486.4 170.0V165.7M487.4 170.0V164.5M488.4 170.0V165.9M489.5 170.0V166.5M490.5 170.0V165.0M491.5 170.0V165.3M492.5 170.0V166.0M493.6 170.0V165.4M494.6 170.0V166.2M495.6 170.0V165.5M496.6 170.0V165.3M497.6 170.0V165.3M498.7 170.0V165.3M499.7 170.0V165.0M500.7 170.0V166.0M501.7 170.0V161.2M502.8 170.0V165.8M503.8 170.0V166.3M504.8 170.0V168.3M505.8 170.0V164.7M506.9 170.0V164.5M507.9 170.0V164.1M508.9 170.0V166.4M509.9 170.0V166.3M511.0 170.0V165.2M512.0 170.0V166.1M513.0 170.0V166.3M514.0 170.0V167.2M515.1 170.0V165.2M516.1 170.0V165.0M517.1 170.0V166.4M518.1 170.0V165.5M519.2 170.0V164.4M520.2 170.0V166.3M521.2 170.0V166.3M522.2 170.0V166.1M523.3 170.0V165.1M524.3 170.0V166.4M525.3 170.0V166.4M526.3 170.0V165.5M527.4 170.0V144.3M528.4 170.0V165.7M529.4 170.0V166.2M530.4 170.0V164.9M531.4 170.0V165.9M532.5 170.0V167.2M533.5 170.0V166.1M534.5 170.0V166.2M535.5 170.0V166.1M536.6 170.0V166.2M537.6 170.0V164.5M538.6 170.0V165.8M539.6 170.0V165.6M540.7 170.0V166.0M541.7 170.0V166.2M542.7 170.0V165.6M543.7 170.0V164.9M544.8 170.0V165.1M545.8 170.0V166.5M546.8 170.0V165.9M547.8 170.0V165.7M548.9 170.0V165.8M549.9 170.0V164.7M550.9 170.0V166.8M551.9 170.0V167.3M553.0 170.0V167.6M554.0 170.0V165.0M555.0 170.0V165.9M556.0 170.0V165.8M557.1 170.0V166.0M558.1 170.0V165.7M559.1 170.0V165.9M560.1 170.0V165.6M561.2 170.0V164.4M562.2 170.0V164.9M563.2 170.0V166.1M564.2 170.0V166.0M565.3 170.0V166.2M566.3 170.0V166.1M567.3 170.0V165.9M568.3 170.0V160.2M569.3 170.0V164.0M570.4 170.0V166.5M571.4 170.0V166.0M572.4 170.0V167.5M573.4 170.0V164.7M574.5 170.0V166.3M575.5 170.0V166.3M576.5 170.0V165.0M577.5 170.0V166.1M578.6 170.0V165.6M579.6 170.0V166.1M580.6 170.0V166.5M581.6 170.0V165.6M582.7 170.0V166.6M583.7 170.0V165.6M584.7 170.0V166.0M585.7 170.0V166.2M586.8 170.0V166.3M587.8 170.0V166.2M588.8 170.0V163.1M589.8 170.0V143.6M590.9 170.0V164.9M591.9 170.0V163.6M592.9 170.0V164.2M593.9 170.0V166.1M595.0 170.0V165.5M596.0 170.0V165.1M597.0 170.0V165.3M598.0 170.0V166.4M599.1 170.0V165.5M600.1 170.0V166.5M601.1 170.0V166.4M602.1 170.0V165.6M603.2 170.0V165.4M604.2 170.0V164.3M605.2 170.0V166.4M606.2 170.0V166.3M607.2 170.0V165.5M608.3 170.0V166.1M609.3 170.0V166.5M610.3 170.0V164.9M611.3 170.0V166.8M612.4 170.0V166.0M613.4 170.0V167.2M614.4 170.0V165.1M615.4 170.0V164.8M616.5 170.0V167.5M617.5 170.0V166.0M618.5 170.0V165.8M619.5 170.0V163.7M620.6 170.0V167.1M621.6 170.0V163.8M622.6 170.0V164.6M623.6 170.0V165.1M624.7 170.0V164.1M625.7 170.0V161.0M626.7 170.0V165.6M627.7 170.0V166.0M628.8 170.0V166.0M629.8 170.0V166.8M630.8 170.0V165.5M631.8 170.0V167.3M632.9 170.0V165.5M633.9 170.0V166.3M634.9 170.0V166.2M635.9 170.0V166.0M637.0 170.0V165.4M638.0 170.0V164.8M639.0 170.0V164.3M640.0 170.0V165.7M641.1 170.0V163.6M642.1 170.0V165.7M643.1 170.0V167.2M644.1 170.0V166.6M645.1 170.0V166.6M646.2 170.0V166.6M647.2 170.0V165.2M648.2 170.0V165.9M649.2 170.0V166.9M650.3 170.0V165.7M651.3 170.0V165.5M652.3 170.0V165.2M653.3 170.0V166.4M654.4 170.0V166.0M655.4 170.0V165.1M656.4 170.0V163.3M657.4 170.0V164.6M658.5 170.0V165.0M659.5 170.0V165.3" stroke="#4a7bd0" stroke-width="1"/><text x="329.5" y="51.1" font-size="12">ch 247 = 11.9</text><text x="70" y="215" font-size="13">layers.11.mlp.down_proj 입력 — 채널별 max|x| (1536채널, log 축) · 중앙값 0.89</text><line x1="70" y1="370" x2="660" y2="370" stroke="currentColor"/><line x1="70" y1="230" x2="70" y2="370" stroke="currentColor"/><line x1="66" y1="370.0" x2="70" y2="370.0" stroke="currentColor"/><text x="62" y="374.0" font-size="12" text-anchor="end">0.01</text><line x1="66" y1="346.7" x2="70" y2="346.7" stroke="currentColor"/><text x="62" y="350.7" font-size="12" text-anchor="end">0.1</text><line x1="66" y1="323.3" x2="70" y2="323.3" stroke="currentColor"/><text x="62" y="327.3" font-size="12" text-anchor="end">1</text><line x1="66" y1="300.0" x2="70" y2="300.0" stroke="currentColor"/><text x="62" y="304.0" font-size="12" text-anchor="end">10</text><line x1="66" y1="276.7" x2="70" y2="276.7" stroke="currentColor"/><text x="62" y="280.7" font-size="12" text-anchor="end">100</text><line x1="66" y1="253.3" x2="70" y2="253.3" stroke="currentColor"/><text x="62" y="257.3" font-size="12" text-anchor="end">1000</text><line x1="66" y1="230.0" x2="70" y2="230.0" stroke="currentColor"/><text x="62" y="234.0" font-size="12" text-anchor="end">10⁴</text><path d="M70.6 370.0V323.6M71.7 370.0V316.9M72.9 370.0V321.1M74.0 370.0V317.3M75.2 370.0V319.0M76.3 370.0V314.8M77.5 370.0V317.2M78.6 370.0V313.6M79.8 370.0V322.0M80.9 370.0V324.1M82.1 370.0V318.9M83.3 370.0V320.7M84.4 370.0V315.6M85.6 370.0V328.1M86.7 370.0V316.0M87.9 370.0V320.2M89.0 370.0V318.3M90.2 370.0V326.0M91.3 370.0V318.9M92.5 370.0V323.8M93.6 370.0V317.9M94.8 370.0V318.9M95.9 370.0V317.4M97.1 370.0V323.4M98.2 370.0V324.6M99.4 370.0V319.7M100.5 370.0V319.7M101.7 370.0V315.7M102.8 370.0V322.8M104.0 370.0V309.5M105.1 370.0V322.4M106.3 370.0V326.1M107.5 370.0V319.9M108.6 370.0V321.9M109.8 370.0V317.6M110.9 370.0V324.1M112.1 370.0V319.2M113.2 370.0V318.3M114.4 370.0V316.9M115.5 370.0V316.8M116.7 370.0V316.6M117.8 370.0V327.8M119.0 370.0V326.5M120.1 370.0V326.7M121.3 370.0V322.5M122.4 370.0V317.2M123.6 370.0V313.8M124.7 370.0V323.1M125.9 370.0V326.5M127.0 370.0V318.9M128.2 370.0V315.0M129.3 370.0V325.9M130.5 370.0V320.4M131.7 370.0V318.3M132.8 370.0V312.5M134.0 370.0V322.0M135.1 370.0V313.8M136.3 370.0V320.6M137.4 370.0V322.6M138.6 370.0V322.8M139.7 370.0V314.2M140.9 370.0V322.1M142.0 370.0V300.4M143.2 370.0V326.4M144.3 370.0V316.4M145.5 370.0V306.5M146.6 370.0V328.7M147.8 370.0V319.7M148.9 370.0V318.9M150.1 370.0V322.8M151.2 370.0V318.8M152.4 370.0V300.0M153.5 370.0V320.8M154.7 370.0V318.1M155.8 370.0V319.6M157.0 370.0V320.8M158.2 370.0V324.6M159.3 370.0V320.8M160.5 370.0V314.9M161.6 370.0V325.9M162.8 370.0V316.9M163.9 370.0V322.9M165.1 370.0V326.8M166.2 370.0V322.1M167.4 370.0V325.3M168.5 370.0V313.1M169.7 370.0V317.8M170.8 370.0V315.1M172.0 370.0V312.3M173.1 370.0V313.7M174.3 370.0V320.9M175.4 370.0V324.1M176.6 370.0V323.5M177.7 370.0V317.8M178.9 370.0V320.4M180.0 370.0V316.5M181.2 370.0V308.4M182.4 370.0V319.5M183.5 370.0V320.3M184.7 370.0V305.9M185.8 370.0V322.0M187.0 370.0V320.0M188.1 370.0V319.2M189.3 370.0V321.8M190.4 370.0V316.0M191.6 370.0V314.7M192.7 370.0V319.8M193.9 370.0V320.1M195.0 370.0V310.3M196.2 370.0V316.9M197.3 370.0V316.5M198.5 370.0V323.1M199.6 370.0V319.1M200.8 370.0V321.5M201.9 370.0V316.0M203.1 370.0V324.5M204.2 370.0V305.8M205.4 370.0V321.4M206.6 370.0V317.7M207.7 370.0V316.3M208.9 370.0V312.7M210.0 370.0V325.9M211.2 370.0V320.1M212.3 370.0V319.5M213.5 370.0V317.3M214.6 370.0V317.1M215.8 370.0V325.6M216.9 370.0V316.5M218.1 370.0V319.7M219.2 370.0V320.9M220.4 370.0V318.6M221.5 370.0V322.7M222.7 370.0V321.4M223.8 370.0V322.9M225.0 370.0V313.9M226.1 370.0V320.3M227.3 370.0V319.7M228.4 370.0V322.6M229.6 370.0V315.8M230.8 370.0V318.6M231.9 370.0V327.3M233.1 370.0V323.1M234.2 370.0V322.7M235.4 370.0V326.0M236.5 370.0V312.4M237.7 370.0V310.4M238.8 370.0V314.9M240.0 370.0V318.7M241.1 370.0V321.7M242.3 370.0V319.6M243.4 370.0V323.1M244.6 370.0V315.3M245.7 370.0V323.3M246.9 370.0V309.6M248.0 370.0V319.9M249.2 370.0V322.7M250.3 370.0V321.8M251.5 370.0V323.0M252.6 370.0V321.4M253.8 370.0V327.5M255.0 370.0V326.1M256.1 370.0V321.3M257.3 370.0V322.3M258.4 370.0V319.2M259.6 370.0V325.7M260.7 370.0V315.9M261.9 370.0V322.7M263.0 370.0V313.1M264.2 370.0V322.8M265.3 370.0V311.8M266.5 370.0V322.4M267.6 370.0V308.3M268.8 370.0V311.2M269.9 370.0V319.0M271.1 370.0V321.3M272.2 370.0V321.7M273.4 370.0V319.8M274.5 370.0V320.7M275.7 370.0V319.0M276.8 370.0V318.9M278.0 370.0V322.7M279.2 370.0V327.7M280.3 370.0V321.5M281.5 370.0V320.7M282.6 370.0V310.7M283.8 370.0V312.9M284.9 370.0V318.7M286.1 370.0V317.1M287.2 370.0V322.2M288.4 370.0V318.6M289.5 370.0V317.6M290.7 370.0V312.4M291.8 370.0V324.7M293.0 370.0V325.6M294.1 370.0V324.5M295.3 370.0V308.7M296.4 370.0V309.5M297.6 370.0V318.6M298.7 370.0V320.5M299.9 370.0V318.7M301.0 370.0V324.1M302.2 370.0V315.5M303.3 370.0V320.0M304.5 370.0V325.1M305.7 370.0V325.9M306.8 370.0V316.9M308.0 370.0V319.9M309.1 370.0V307.6M310.3 370.0V322.8M311.4 370.0V317.9M312.6 370.0V323.0M313.7 370.0V321.9M314.9 370.0V318.9M316.0 370.0V323.9M317.2 370.0V313.4M318.3 370.0V314.7M319.5 370.0V313.6M320.6 370.0V312.6M321.8 370.0V327.0M322.9 370.0V322.4M324.1 370.0V318.2M325.2 370.0V319.5M326.4 370.0V321.9M327.5 370.0V319.2M328.7 370.0V321.0M329.9 370.0V321.2M331.0 370.0V322.7M332.2 370.0V313.6M333.3 370.0V324.6M334.5 370.0V315.6M335.6 370.0V316.3M336.8 370.0V320.9M337.9 370.0V319.3M339.1 370.0V320.1M340.2 370.0V323.5M341.4 370.0V321.1M342.5 370.0V324.6M343.7 370.0V326.8M344.8 370.0V315.2M346.0 370.0V320.8M347.1 370.0V318.4M348.3 370.0V315.5M349.4 370.0V325.3M350.6 370.0V319.8M351.7 370.0V313.6M352.9 370.0V323.4M354.1 370.0V316.8M355.2 370.0V311.3M356.4 370.0V308.0M357.5 370.0V313.7M358.7 370.0V312.6M359.8 370.0V322.9M361.0 370.0V316.2M362.1 370.0V323.3M363.3 370.0V321.0M364.4 370.0V319.5M365.6 370.0V320.0M366.7 370.0V323.6M367.9 370.0V320.7M369.0 370.0V326.8M370.2 370.0V322.1M371.3 370.0V320.2M372.5 370.0V319.6M373.6 370.0V325.1M374.8 370.0V318.5M375.9 370.0V315.8M377.1 370.0V321.0M378.3 370.0V325.1M379.4 370.0V321.0M380.6 370.0V316.5M381.7 370.0V314.0M382.9 370.0V319.3M384.0 370.0V323.5M385.2 370.0V318.6M386.3 370.0V318.1M387.5 370.0V318.6M388.6 370.0V324.2M389.8 370.0V322.7M390.9 370.0V319.8M392.1 370.0V325.7M393.2 370.0V321.1M394.4 370.0V321.9M395.5 370.0V317.7M396.7 370.0V318.7M397.8 370.0V322.3M399.0 370.0V310.7M400.1 370.0V313.9M401.3 370.0V317.3M402.5 370.0V324.2M403.6 370.0V322.8M404.8 370.0V319.2M405.9 370.0V307.6M407.1 370.0V324.0M408.2 370.0V323.9M409.4 370.0V310.7M410.5 370.0V317.7M411.7 370.0V318.0M412.8 370.0V317.3M414.0 370.0V324.1M415.1 370.0V321.9M416.3 370.0V324.7M417.4 370.0V320.6M418.6 370.0V320.6M419.7 370.0V310.8M420.9 370.0V318.7M422.0 370.0V315.2M423.2 370.0V321.0M424.3 370.0V318.1M425.5 370.0V305.3M426.7 370.0V314.6M427.8 370.0V318.2M429.0 370.0V322.2M430.1 370.0V319.6M431.3 370.0V322.2M432.4 370.0V319.4M433.6 370.0V318.7M434.7 370.0V315.4M435.9 370.0V313.7M437.0 370.0V316.3M438.2 370.0V320.5M439.3 370.0V321.1M440.5 370.0V316.5M441.6 370.0V322.0M442.8 370.0V320.8M443.9 370.0V317.1M445.1 370.0V320.7M446.2 370.0V313.5M447.4 370.0V305.8M448.5 370.0V315.7M449.7 370.0V320.6M450.8 370.0V314.3M452.0 370.0V320.2M453.2 370.0V315.2M454.3 370.0V316.8M455.5 370.0V320.9M456.6 370.0V318.1M457.8 370.0V320.4M458.9 370.0V324.5M460.1 370.0V314.7M461.2 370.0V318.9M462.4 370.0V318.9M463.5 370.0V321.8M464.7 370.0V311.4M465.8 370.0V316.0M467.0 370.0V323.6M468.1 370.0V314.7M469.3 370.0V322.4M470.4 370.0V323.6M471.6 370.0V312.9M472.7 370.0V326.0M473.9 370.0V321.0M475.0 370.0V318.1M476.2 370.0V325.0M477.4 370.0V320.4M478.5 370.0V313.5M479.7 370.0V325.3M480.8 370.0V316.0M482.0 370.0V317.9M483.1 370.0V318.6M484.3 370.0V319.8M485.4 370.0V325.1M486.6 370.0V316.0M487.7 370.0V317.5M488.9 370.0V322.1M490.0 370.0V324.5M491.2 370.0V316.6M492.3 370.0V311.5M493.5 370.0V317.4M494.6 370.0V314.9M495.8 370.0V323.9M496.9 370.0V329.0M498.1 370.0V318.2M499.2 370.0V315.0M500.4 370.0V320.8M501.6 370.0V303.4M502.7 370.0V319.7M503.9 370.0V317.5M505.0 370.0V319.5M506.2 370.0V325.3M507.3 370.0V320.3M508.5 370.0V307.3M509.6 370.0V320.5M510.8 370.0V317.2M511.9 370.0V319.3M513.1 370.0V321.5M514.2 370.0V317.3M515.4 370.0V317.6M516.5 370.0V326.9M517.7 370.0V323.6M518.8 370.0V321.3M520.0 370.0V319.7M521.1 370.0V322.0M522.3 370.0V323.0M523.4 370.0V316.3M524.6 370.0V319.2M525.8 370.0V322.2M526.9 370.0V317.4M528.1 370.0V318.8M529.2 370.0V320.8M530.4 370.0V321.7M531.5 370.0V325.6M532.7 370.0V312.1M533.8 370.0V319.7M535.0 370.0V318.3M536.1 370.0V326.7M537.3 370.0V319.1M538.4 370.0V325.1M539.6 370.0V322.5M540.7 370.0V304.8M541.9 370.0V241.3M543.0 370.0V319.4M544.2 370.0V317.7M545.3 370.0V316.5M546.5 370.0V315.6M547.6 370.0V320.0M548.8 370.0V319.3M550.0 370.0V307.3M551.1 370.0V321.7M552.3 370.0V322.9M553.4 370.0V313.7M554.6 370.0V318.3M555.7 370.0V308.6M556.9 370.0V322.0M558.0 370.0V328.7M559.2 370.0V319.0M560.3 370.0V320.3M561.5 370.0V326.9M562.6 370.0V324.8M563.8 370.0V316.1M564.9 370.0V320.7M566.1 370.0V318.6M567.2 370.0V312.2M568.4 370.0V318.7M569.5 370.0V323.3M570.7 370.0V323.6M571.8 370.0V320.5M573.0 370.0V319.7M574.2 370.0V324.6M575.3 370.0V318.2M576.5 370.0V326.1M577.6 370.0V325.6M578.8 370.0V315.0M579.9 370.0V321.7M581.1 370.0V325.7M582.2 370.0V313.1M583.4 370.0V313.5M584.5 370.0V319.4M585.7 370.0V326.9M586.8 370.0V303.7M588.0 370.0V309.5M589.1 370.0V312.1M590.3 370.0V317.2M591.4 370.0V319.1M592.6 370.0V323.2M593.7 370.0V319.3M594.9 370.0V308.1M596.0 370.0V300.7M597.2 370.0V325.1M598.3 370.0V319.8M599.5 370.0V312.0M600.7 370.0V324.1M601.8 370.0V317.2M603.0 370.0V316.6M604.1 370.0V318.2M605.3 370.0V317.8M606.4 370.0V302.9M607.6 370.0V325.6M608.7 370.0V305.9M609.9 370.0V310.6M611.0 370.0V325.0M612.2 370.0V317.7M613.3 370.0V317.4M614.5 370.0V327.9M615.6 370.0V321.2M616.8 370.0V315.1M617.9 370.0V322.6M619.1 370.0V325.3M620.2 370.0V314.3M621.4 370.0V320.3M622.5 370.0V325.6M623.7 370.0V314.8M624.9 370.0V313.5M626.0 370.0V325.4M627.2 370.0V320.2M628.3 370.0V319.0M629.5 370.0V325.5M630.6 370.0V323.5M631.8 370.0V317.1M632.9 370.0V323.9M634.1 370.0V320.6M635.2 370.0V316.8M636.4 370.0V310.4M637.5 370.0V313.7M638.7 370.0V329.0M639.8 370.0V317.1M641.0 370.0V251.2M642.1 370.0V319.5M643.3 370.0V316.4M644.4 370.0V328.2M645.6 370.0V325.4M646.7 370.0V316.9M647.9 370.0V322.4M649.1 370.0V319.2M650.2 370.0V319.4M651.4 370.0V326.2M652.5 370.0V319.3M653.7 370.0V321.1M654.8 370.0V313.5M656.0 370.0V318.1M657.1 370.0V324.0M658.3 370.0V322.2M659.4 370.0V322.6" stroke="#4a7bd0" stroke-width="1"/><text x="536.3" y="245.3" font-size="12" text-anchor="end">ch 1229 = 3286</text><text x="635.4" y="265.2" font-size="12" text-anchor="end">ch 1487 = 1234</text><text x="365" y="400" font-size="12" text-anchor="middle">입력 채널 번호 (왼쪽 0 → 오른쪽 끝)</text></svg>
```

그림 3 — 실제 SmolLM2 활성값의 채널별 최대 절댓값 (calibration 483 token). 위: 12층 q_proj 입력, 채널 247 하나가 중앙값의 30배. 아래(log 축): 11층 down_proj 입력, 채널 1229와 1487이 중앙값의 수천 배.

출력에서 볼 것 — **두 종류의 outlier**가 보인다.

1. **체계적 채널 outlier (12층 q_proj, 채널 247).** 이 채널은 **거의 모든 token에서** 크다(token별 중앙값 8.14, 다른 채널의 최대값 중앙값은 0.39). 즉 "이 채널은 늘 크다"는 구조적 성질이다. 마지막 줄을 보면 30개 층 중 16개 층에서 채널 247이, 나머지 층에서도 507, 17, 260 같은 소수의 채널이 최대다. **같은 채널이 층을 건너 계속 크다.** RMSNorm 뒤의 hidden state에서 특정 차원이 늘 큰 현상으로, SmoothQuant가 겨냥하는 것이 이것이다.
2. **특정 token의 거대 활성값 (11층 down_proj, token #0).** 채널 1229의 값 3286은 **첫 번째 token에서만** 나오고, 다른 token에서 이 채널의 중앙값은 0.00이다. 첫 token이나 구두점 같은 몇몇 token에 극단적으로 큰 활성값이 생기는 현상은 "massive activations"(Sun et al., 2024)로 보고되었고, attention이 첫 token에 몰리는 attention sink 현상(StreamingLLM, Xiao et al., 2023)과 관련이 있다고 알려져 있다.

per-tensor int8의 결과는 참혹하다. 11층 down_proj는 scale이 25.87이 되어 **741,888개 원소 중 2개만 0이 아닌 값으로 남는다.** 12층 q_proj도 원소의 1/3이 0이 된다.

### 4.3 왜 activation은 per-channel scale을 못 쓰나 — 행렬곱의 구조

가중치에서는 per-channel(행마다)이 잘 됐다. 활성값도 채널(열)마다 scale을 주면 되지 않을까? **정수 행렬곱에서는 안 된다.** 출력 하나를 풀어 쓰면:

```
y[t, o] = ∑ⱼ x[t, j] · W[o, j]                   (t: token, o: 출력 채널, j: 입력 채널 = 합산 축)

x[t, j] ≈ s_x[t] · qx[t, j],  W[o, j] ≈ s_w[o] · qw[o, j]  이면
y[t, o] ≈ s_x[t] · s_w[o] · ∑ⱼ qx[t, j] · qw[o, j]     ← 정수 dot product 하나 + 곱셈 한 번
```

말로 하면: scale이 **합산 축(j)과 무관**하면(token마다 s_x[t], 출력 채널마다 s_w[o]) 합 바깥으로 빠져서, 안쪽은 순수한 int8 × int8 → int32 누적이 된다. 그런데 활성값 scale이 **입력 채널 j마다** 다르면 `∑ⱼ s_x[j] · qx · qw`가 되어 합 안에서 매번 다른 scale을 곱해야 한다. 정수 MAC 배열의 장점이 사라진다.

그래서 정수 GEMM이 허용하는 조합은:

| 대상 | 허용되는 scale 단위 | 이유 |
|---|---|---|
| 가중치 | per-tensor, per-output-channel | 합산 축 밖 |
| 활성값 | per-tensor, per-token(행) | 합산 축 밖 |
| 활성값 | per-input-channel (불가) | 합산 축 안 |

outlier는 하필 **입력 채널 방향**으로 몰려 있다(채널 247). 가장 필요한 단위의 scale을 쓸 수 없다는 것이 W8A8의 근본 문제다.

### 4.4 코드로 확인: naive W8A8 vs W8A16 [모델 필요]

가중치는 int8 per-channel로 고정하고, 모든 Linear의 입력을 int8로 fake-quant한다(forward pre-hook). 활성값 scale은 매 호출마다 실제 값으로 정하는 **dynamic** 방식이다.

```python
import torch
from c3_common import load, ppl, linears, EVAL_TEXT
tok, model = load(); ids = tok(EVAL_TEXT, return_tensors="pt").input_ids
L = linears(model)

def fq(x, s):                                           # int8 대칭 fake-quant (scale s는 broadcast)
    return torch.clamp(torch.round(x / s), -127, 127) * s

for m in L.values():                                    # W8: 가중치 int8 per-channel (행마다 scale)
    m.weight.data = fq(m.weight.data, m.weight.data.abs().amax(1, keepdim=True) / 127)
print(f"W8A16 (가중치만 int8)           ppl {ppl(model, ids):8.3f}")

def act_hook(mode):                                     # Linear 입력 x를 int8로 fake-quant
    def pre(m, inp):
        x = inp[0]
        if mode == "per-tensor": s = x.abs().amax() / 127                      # 텐서 전체에 scale 1개
        else:                    s = x.abs().amax(-1, keepdim=True) / 127      # token(행)마다 scale 1개
        return (fq(x, s.clamp(min=1e-8)),)
    return pre

for mode in ("per-tensor", "per-token"):
    hs = [m.register_forward_pre_hook(act_hook(mode)) for m in L.values()]
    print(f"W8A8  activation {mode:10s}    ppl {ppl(model, ids):8.3f}")
    for h in hs: h.remove()
```

```text
W8A16 (가중치만 int8)           ppl   18.063
W8A8  activation per-tensor    ppl   35.603
W8A8  activation per-token     ppl   18.542
```

출력에서 볼 것:

- **W8A16(18.06)은 무손실, W8A8 per-tensor(35.60)는 PPL이 두 배.** 가중치 8-bit는 문제없고, 활성값 8-bit가 문제라는 것이 분리되어 보인다.
- **per-token dynamic(18.54)은 거의 회복된다.** token마다 scale을 따로 잡으면 첫 token의 거대 활성값(4.2절의 2번 종류)이 다른 token에 피해를 주지 않는다. 채널 247 같은 체계적 outlier는 여전히 각 token의 scale을 키우지만, 이 작은 모델에서는 그 피해가 크지 않았다.
- 그렇다면 항상 per-token dynamic을 쓰면 될까? 실제 NPU/DSP에서는 **static per-tensor**(calibration으로 scale을 미리 고정)를 요구하는 경우가 많다. dynamic은 매 token마다 max를 구하는 reduction이 필요하고, per-token scale은 requantization 경로를 복잡하게 만든다. 이런 제약에서 쓰는 해법이 SmoothQuant다.

### 4.5 함정

- outlier 채널은 **calibration 텍스트에 따라 크기가 다르다.** static scale을 너무 짧은 calibration으로 정하면 실제 입력에서 clipping이 생긴다(C1의 percentile calibration 참고).
- outlier를 "잡음"으로 보고 clipping하면 안 된다. LLM.int8() 논문은 이 outlier 채널을 0으로 만들면 성능이 크게 무너진다고 보고했다. **크지만 중요한 값**이다.

---

## 5. SmoothQuant — 어려움을 활성값에서 가중치로 옮기기

### 5.1 아이디어

Xiao et al.(2022)의 관찰: 활성값은 채널 간 크기 차이가 크고(양자화 어려움), 가중치는 비교적 고르다(양자화 쉬움). 그렇다면 **입력 채널 j마다 활성값을 s_j로 나누고, 같은 채널의 가중치 열에 s_j를 곱하면** 곱은 그대로인 채 어려움을 옮길 수 있다.

```
Y = X · Wᵀ = (X · diag(s)⁻¹) · (W · diag(s))ᵀ = X̂ · Ŵᵀ

s_j = max|X_j|^α / max|W_j|^(1−α)          (X_j: 활성값의 j번 채널, W_j: 가중치의 j번 열)
```

말로 하면: 채널 j의 활성값이 크면 s_j도 커서 그 채널을 줄여 준다. 대신 가중치의 j번 열이 s_j배 커진다. α는 "얼마나 옮길까"를 정하는 손잡이다. α = 0.5면 채널마다 활성값 최대와 가중치 최대가 **정확히 같아진다**:

```
α = 0.5:  max|X̂_j| = max|X_j| / s_j = √(max|X_j| · max|W_j|) = max|Ŵ_j|
```

그리고 **s는 추론 때 공짜다.** X를 s로 나누는 연산은 앞 층에 흡수(fold)할 수 있다. q, k, v의 입력은 RMSNorm 출력이므로 RMSNorm의 weight γ를 γ/s로 바꾸면 끝이다. 이 작업은 오프라인에서 한 번 하고, 추론 그래프에는 아무 연산도 추가되지 않는다.

### 5.2 손으로 계산: 채널 2개

활성값 채널 최대 = [2, 100], 가중치 열 최대 = [1, 0.5], α = 0.5.

```
s₀ = √(2 / 1)   = 1.414       s₁ = √(100 / 0.5) = 14.14
smoothing 후 X 최대: [2/1.414, 100/14.14] = [1.414, 7.07]
smoothing 후 W 최대: [1·1.414, 0.5·14.14] = [1.414, 7.07]
```

per-tensor int8 scale로 보면: 전에는 X scale이 100/127 = 0.787이라 채널 0(최대 2)은 최대 약 2.5칸만 썼다. 후에는 7.07/127 = 0.0557이라 채널 0(최대 1.414)이 약 25칸을 쓴다. **작은 채널의 해상도가 10배 좋아졌다.** 대신 W의 열 최대가 0.5 → 7.07로 커졌지만, 가중치는 per-channel(행마다) scale을 쓰고 원래 고른 분포였으므로 감당할 수 있다.

### 5.3 코드로 확인: 한 층에서 동치성과 효과 [모델 필요]

12층 q_proj(채널 247 outlier가 있는 층)에서 smoothing 전후의 결과가 같은지, 채널별 최대가 어떻게 바뀌는지, W8A8 출력 오차가 얼마나 줄어드는지 본다.

```python
import torch
from c3_common import load, linears, CALIB_TEXT
tok, model = load(); m = linears(model)["12.self_attn.q_proj"]
out = {}; h = m.register_forward_hook(lambda mod, i, o: out.__setitem__("x", i[0][0].detach()))
with torch.no_grad(): model(tok(CALIB_TEXT, return_tensors="pt").input_ids)
X, W = out["x"].double(), m.weight.data.double()        # X: [483, 576], W: [576 out, 576 in]
fq = lambda t, s: torch.clamp(torch.round(t / s), -127, 127) * s
def w8a8_err(X, W):                                     # 활성값 per-tensor, 가중치 per-channel int8
    Y = X @ W.T
    Yq = fq(X, X.abs().max() / 127) @ fq(W, W.abs().amax(1, keepdim=True) / 127).T
    return float((Yq - Y).norm() / Y.norm())

alpha = 0.5
s = X.abs().amax(0) ** alpha / W.abs().amax(0) ** (1 - alpha)   # 입력 채널 j마다 s_j
Xs, Ws = X / s, W * s                                   # X·diag(s)⁻¹ , W·diag(s)
print("수학적 동치: max|X̂Ŵᵀ − XWᵀ| =", f"{float((Xs @ Ws.T - X @ W.T).abs().max()):.2e}")
for tag, A, B in (("before", X, W), ("after ", Xs, Ws)):
    xm, wm = A.abs().amax(0), B.abs().amax(0)
    print(f"{tag}: X 채널 max  최대 {xm.max():6.2f} 중앙 {xm.median():5.2f} (비 {xm.max()/xm.median():5.1f}) | "
          f"W 채널 max 최대 {wm.max():5.2f} 중앙 {wm.median():5.2f} (비 {wm.max()/wm.median():4.1f})")
print(f"채널 247: s = {s[247]:.2f}, X max {X[:,247].abs().max():.2f} → {Xs[:,247].abs().max():.2f}, "
      f"W max {W[:,247].abs().max():.3f} → {Ws[:,247].abs().max():.3f}")
print(f"W8A8 출력 상대오차: smoothing 전 {w8a8_err(X, W):.4f} → 후 {w8a8_err(Xs, Ws):.4f}")
```

```text
수학적 동치: max|X̂Ŵᵀ − XWᵀ| = 3.55e-15
before: X 채널 max  최대  11.90 중앙  0.39 (비  30.3) | W 채널 max 최대  3.14 중앙  0.95 (비  3.3)
after : X 채널 max  최대   3.53 중앙  0.62 (비   5.7) | W 채널 max 최대  3.53 중앙  0.62 (비  5.7)
채널 247: s = 3.37, X max 11.90 → 3.53, W max 1.047 → 3.530
W8A8 출력 상대오차: smoothing 전 0.0737 → 후 0.0152
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 380"><text x="50" y="40" font-size="13">X 채널별 max — 전</text><line x1="50" y1="160" x2="320" y2="160" stroke="currentColor"/><line x1="50" y1="50" x2="50" y2="160" stroke="currentColor"/><line x1="46" y1="160.0" x2="50" y2="160.0" stroke="currentColor"/><text x="43" y="164.0" font-size="12" text-anchor="end">0</text><line x1="46" y1="123.3" x2="50" y2="123.3" stroke="currentColor"/><text x="43" y="127.3" font-size="12" text-anchor="end">4</text><line x1="46" y1="86.7" x2="50" y2="86.7" stroke="currentColor"/><text x="43" y="90.7" font-size="12" text-anchor="end">8</text><line x1="46" y1="50.0" x2="50" y2="50.0" stroke="currentColor"/><text x="43" y="54.0" font-size="12" text-anchor="end">12</text><path d="M50.5 160V155.6M51.4 160V156.5M52.3 160V156.2M53.3 160V155.5M54.2 160V149.5M55.2 160V155.0M56.1 160V155.9M57.0 160V139.0M58.0 160V154.4M58.9 160V156.1M59.8 160V155.8M60.8 160V155.5M61.7 160V154.2M62.7 160V155.0M63.6 160V156.1M64.5 160V153.3M65.5 160V155.2M66.4 160V155.7M67.3 160V156.3M68.3 160V154.8M69.2 160V156.1M70.2 160V156.5M71.1 160V155.3M72.0 160V155.2M73.0 160V156.9M73.9 160V153.9M74.8 160V157.0M75.8 160V156.1M76.7 160V153.6M77.7 160V154.7M78.6 160V156.2M79.5 160V156.6M80.5 160V156.1M81.4 160V156.3M82.3 160V156.2M83.3 160V155.5M84.2 160V156.2M85.2 160V155.8M86.1 160V155.4M87.0 160V156.3M88.0 160V156.7M88.9 160V154.5M89.8 160V156.3M90.8 160V156.5M91.7 160V156.5M92.7 160V156.3M93.6 160V156.7M94.5 160V156.7M95.5 160V154.0M96.4 160V156.1M97.3 160V150.7M98.3 160V155.2M99.2 160V156.0M100.2 160V155.6M101.1 160V157.6M102.0 160V156.3M103.0 160V156.8M103.9 160V156.3M104.8 160V156.1M105.8 160V150.8M106.7 160V156.8M107.7 160V154.9M108.6 160V155.3M109.5 160V156.3M110.5 160V154.6M111.4 160V156.3M112.3 160V156.8M113.3 160V154.9M114.2 160V156.2M115.2 160V155.4M116.1 160V155.0M117.0 160V156.6M118.0 160V156.2M118.9 160V155.5M119.8 160V155.9M120.8 160V155.6M121.7 160V156.9M122.7 160V156.2M123.6 160V151.3M124.5 160V156.8M125.5 160V154.7M126.4 160V156.3M127.3 160V156.3M128.3 160V156.4M129.2 160V156.1M130.2 160V153.0M131.1 160V155.1M132.0 160V156.2M133.0 160V155.3M133.9 160V153.3M134.8 160V155.7M135.8 160V155.8M136.7 160V156.5M137.7 160V155.9M138.6 160V156.2M139.5 160V156.7M140.5 160V155.4M141.4 160V156.5M142.3 160V156.1M143.3 160V156.1M144.2 160V155.8M145.2 160V156.9M146.1 160V154.7M147.0 160V155.7M148.0 160V155.7M148.9 160V151.7M149.8 160V155.5M150.8 160V155.1M151.7 160V156.6M152.7 160V155.3M153.6 160V156.0M154.5 160V156.3M155.5 160V155.2M156.4 160V155.5M157.3 160V155.6M158.3 160V142.4M159.2 160V155.3M160.2 160V156.8M161.1 160V157.7M162.0 160V154.8M163.0 160V156.5M163.9 160V152.0M164.8 160V155.3M165.8 160V50.9M166.7 160V147.1M167.7 160V156.7M168.6 160V156.0M169.5 160V156.6M170.5 160V156.2M171.4 160V155.8M172.3 160V145.5M173.3 160V155.6M174.2 160V155.9M175.2 160V156.5M176.1 160V154.7M177.0 160V156.0M178.0 160V156.3M178.9 160V156.0M179.8 160V156.6M180.8 160V156.3M181.7 160V155.8M182.7 160V157.3M183.6 160V152.1M184.5 160V155.8M185.5 160V155.1M186.4 160V156.0M187.3 160V156.7M188.3 160V156.1M189.2 160V156.7M190.2 160V154.5M191.1 160V155.0M192.0 160V156.4M193.0 160V156.2M193.9 160V154.7M194.8 160V133.9M195.8 160V155.8M196.7 160V155.9M197.7 160V157.0M198.6 160V156.0M199.5 160V155.7M200.5 160V155.1M201.4 160V155.3M202.3 160V156.0M203.3 160V156.1M204.2 160V156.7M205.2 160V156.3M206.1 160V156.1M207.0 160V154.7M208.0 160V156.9M208.9 160V156.3M209.8 160V154.6M210.8 160V156.8M211.7 160V155.6M212.7 160V156.4M213.6 160V156.0M214.5 160V150.8M215.5 160V156.4M216.4 160V156.3M217.3 160V156.2M218.3 160V155.8M219.2 160V156.1M220.2 160V156.4M221.1 160V156.8M222.0 160V156.4M223.0 160V156.4M223.9 160V155.2M224.8 160V156.5M225.8 160V156.2M226.7 160V155.9M227.7 160V156.0M228.6 160V156.6M229.5 160V156.6M230.5 160V155.7M231.4 160V156.4M232.3 160V155.6M233.3 160V156.2M234.2 160V156.1M235.2 160V156.1M236.1 160V155.0M237.0 160V155.9M238.0 160V155.4M238.9 160V155.8M239.8 160V152.9M240.8 160V155.3M241.7 160V156.5M242.7 160V155.8M243.6 160V156.1M244.5 160V156.2M245.5 160V156.0M246.4 160V155.8M247.3 160V152.5M248.3 160V156.4M249.2 160V155.6M250.2 160V155.0M251.1 160V156.9M252.0 160V156.0M253.0 160V156.8M253.9 160V155.8M254.8 160V156.2M255.8 160V155.2M256.7 160V156.7M257.7 160V155.9M258.6 160V156.2M259.5 160V138.3M260.5 160V155.7M261.4 160V156.6M262.3 160V156.7M263.3 160V156.7M264.2 160V155.4M265.2 160V156.3M266.1 160V156.3M267.0 160V155.7M268.0 160V156.5M268.9 160V156.4M269.8 160V155.5M270.8 160V157.7M271.7 160V155.8M272.7 160V156.5M273.6 160V156.4M274.5 160V155.3M275.5 160V155.7M276.4 160V156.7M277.3 160V156.6M278.3 160V151.7M279.2 160V156.6M280.2 160V155.5M281.1 160V156.8M282.0 160V155.8M283.0 160V156.2M283.9 160V156.3M284.8 160V156.3M285.8 160V156.6M286.7 160V156.7M287.7 160V137.7M288.6 160V154.6M289.5 160V155.1M290.5 160V155.8M291.4 160V156.0M292.3 160V156.2M293.3 160V156.2M294.2 160V155.2M295.2 160V156.8M296.1 160V156.2M297.0 160V155.7M298.0 160V156.6M298.9 160V155.8M299.8 160V155.6M300.8 160V156.5M301.7 160V154.6M302.7 160V154.7M303.6 160V155.0M304.5 160V152.3M305.5 160V156.6M306.4 160V156.2M307.3 160V156.2M308.3 160V156.8M309.2 160V156.1M310.2 160V155.2M311.1 160V154.6M312.0 160V156.3M313.0 160V157.1M313.9 160V156.0M314.8 160V156.6M315.8 160V156.2M316.7 160V155.9M317.7 160V155.9M318.6 160V154.3M319.5 160V155.8" stroke="#4a7bd0" stroke-width="0.8"/><text x="320" y="54" font-size="12" text-anchor="end">max 11.9 / 중앙 0.39</text><text x="390" y="40" font-size="13">X 채널별 max — 후 (X÷s)</text><line x1="390" y1="160" x2="660" y2="160" stroke="currentColor"/><line x1="390" y1="50" x2="390" y2="160" stroke="currentColor"/><line x1="386" y1="160.0" x2="390" y2="160.0" stroke="currentColor"/><text x="383" y="164.0" font-size="12" text-anchor="end">0</text><line x1="386" y1="123.3" x2="390" y2="123.3" stroke="currentColor"/><text x="383" y="127.3" font-size="12" text-anchor="end">4</text><line x1="386" y1="86.7" x2="390" y2="86.7" stroke="currentColor"/><text x="383" y="90.7" font-size="12" text-anchor="end">8</text><line x1="386" y1="50.0" x2="390" y2="50.0" stroke="currentColor"/><text x="383" y="54.0" font-size="12" text-anchor="end">12</text><path d="M390.5 160V154.1M391.4 160V153.8M392.3 160V154.3M393.3 160V154.3M394.2 160V150.3M395.2 160V153.8M396.1 160V153.1M397.0 160V145.8M398.0 160V153.1M398.9 160V154.5M399.8 160V153.7M400.8 160V153.4M401.7 160V153.0M402.7 160V153.8M403.6 160V154.7M404.5 160V152.4M405.5 160V153.2M406.4 160V154.1M407.3 160V154.5M408.3 160V153.1M409.2 160V154.1M410.2 160V153.6M411.1 160V154.2M412.0 160V153.6M413.0 160V154.4M413.9 160V153.5M414.8 160V153.4M415.8 160V153.8M416.7 160V152.8M417.7 160V152.7M418.6 160V154.0M419.5 160V154.8M420.5 160V154.0M421.4 160V154.3M422.3 160V154.0M423.3 160V153.7M424.2 160V154.2M425.2 160V154.3M426.1 160V153.6M427.0 160V154.9M428.0 160V154.7M428.9 160V152.6M429.8 160V154.0M430.8 160V154.9M431.7 160V153.2M432.7 160V153.6M433.6 160V154.8M434.5 160V153.2M435.5 160V152.3M436.4 160V154.1M437.3 160V143.6M438.3 160V154.4M439.2 160V154.1M440.2 160V153.3M441.1 160V154.6M442.0 160V154.3M443.0 160V154.8M443.9 160V153.8M444.8 160V153.6M445.8 160V150.1M446.7 160V154.6M447.7 160V153.9M448.6 160V152.8M449.5 160V155.1M450.5 160V153.1M451.4 160V154.6M452.3 160V153.8M453.3 160V153.5M454.2 160V154.7M455.2 160V153.1M456.1 160V153.3M457.0 160V153.5M458.0 160V154.0M458.9 160V153.3M459.8 160V154.5M460.8 160V154.7M461.7 160V154.2M462.7 160V154.6M463.6 160V151.5M464.5 160V154.7M465.5 160V153.0M466.4 160V154.8M467.3 160V154.4M468.3 160V154.3M469.2 160V154.1M470.2 160V152.3M471.1 160V153.9M472.0 160V154.4M473.0 160V153.4M473.9 160V152.0M474.8 160V154.5M475.8 160V154.2M476.7 160V153.4M477.7 160V153.6M478.6 160V154.3M479.5 160V154.1M480.5 160V154.3M481.4 160V154.7M482.3 160V153.7M483.3 160V154.3M484.2 160V154.2M485.2 160V155.2M486.1 160V153.4M487.0 160V153.5M488.0 160V154.3M488.9 160V151.7M489.8 160V154.3M490.8 160V154.1M491.7 160V153.3M492.7 160V154.0M493.6 160V154.3M494.5 160V154.3M495.5 160V153.2M496.4 160V154.3M497.3 160V153.5M498.3 160V149.8M499.2 160V153.2M500.2 160V155.3M501.1 160V155.1M502.0 160V153.6M503.0 160V154.3M503.9 160V151.2M504.8 160V152.9M505.8 160V127.6M506.7 160V147.4M507.7 160V153.8M508.6 160V154.6M509.5 160V154.7M510.5 160V154.7M511.4 160V153.4M512.3 160V147.8M513.3 160V153.2M514.2 160V154.1M515.2 160V154.1M516.1 160V152.0M517.0 160V152.5M518.0 160V154.1M518.9 160V154.1M519.8 160V153.6M520.8 160V153.7M521.7 160V154.0M522.7 160V154.9M523.6 160V151.4M524.5 160V153.1M525.5 160V152.6M526.4 160V153.7M527.3 160V154.1M528.3 160V153.9M529.2 160V154.5M530.2 160V153.0M531.1 160V153.2M532.0 160V154.8M533.0 160V154.2M533.9 160V154.0M534.8 160V147.6M535.8 160V154.0M536.7 160V154.7M537.7 160V154.7M538.6 160V154.0M539.5 160V153.8M540.5 160V153.5M541.4 160V153.0M542.3 160V153.7M543.3 160V154.4M544.2 160V154.4M545.2 160V154.1M546.1 160V154.9M547.0 160V152.9M548.0 160V154.8M548.9 160V154.7M549.8 160V153.1M550.8 160V155.2M551.7 160V153.2M552.7 160V154.3M553.6 160V154.2M554.5 160V151.1M555.5 160V154.7M556.4 160V153.7M557.3 160V154.2M558.3 160V154.0M559.2 160V154.4M560.2 160V154.2M561.1 160V155.3M562.0 160V154.3M563.0 160V153.5M563.9 160V153.4M564.8 160V154.8M565.8 160V154.3M566.7 160V153.7M567.7 160V154.0M568.6 160V154.0M569.5 160V154.2M570.5 160V153.7M571.4 160V154.3M572.3 160V154.2M573.3 160V153.6M574.2 160V154.2M575.2 160V153.4M576.1 160V153.7M577.0 160V153.9M578.0 160V153.2M578.9 160V153.0M579.8 160V151.6M580.8 160V152.7M581.7 160V154.5M582.7 160V153.8M583.6 160V154.1M584.5 160V154.4M585.5 160V154.3M586.4 160V153.2M587.3 160V152.3M588.3 160V154.2M589.2 160V154.0M590.2 160V153.0M591.1 160V155.1M592.0 160V153.9M593.0 160V154.0M593.9 160V152.8M594.8 160V154.6M595.8 160V153.5M596.7 160V154.6M597.7 160V153.1M598.6 160V154.2M599.5 160V148.6M600.5 160V154.3M601.4 160V154.2M602.3 160V154.8M603.3 160V154.4M604.2 160V153.9M605.2 160V153.6M606.1 160V153.9M607.0 160V153.5M608.0 160V154.1M608.9 160V154.2M609.8 160V153.3M610.8 160V155.0M611.7 160V154.1M612.7 160V154.3M613.6 160V154.3M614.5 160V153.2M615.5 160V153.9M616.4 160V153.7M617.3 160V154.4M618.3 160V151.5M619.2 160V154.7M620.2 160V154.2M621.1 160V154.5M622.0 160V153.9M623.0 160V154.5M623.9 160V154.8M624.8 160V154.4M625.8 160V154.7M626.7 160V154.3M627.7 160V145.0M628.6 160V153.4M629.5 160V154.1M630.5 160V152.9M631.4 160V153.4M632.3 160V152.9M633.3 160V154.4M634.2 160V153.9M635.2 160V153.9M636.1 160V153.8M637.0 160V153.5M638.0 160V154.3M638.9 160V154.7M639.8 160V153.8M640.8 160V154.1M641.7 160V153.1M642.7 160V152.3M643.6 160V152.4M644.5 160V152.3M645.5 160V154.9M646.4 160V153.8M647.3 160V154.5M648.3 160V154.4M649.2 160V154.8M650.2 160V153.4M651.1 160V153.5M652.0 160V154.1M653.0 160V154.7M653.9 160V154.2M654.8 160V154.5M655.8 160V153.7M656.7 160V153.6M657.7 160V153.7M658.6 160V153.0M659.5 160V154.0" stroke="#e08a3c" stroke-width="0.8"/><text x="660" y="54" font-size="12" text-anchor="end">max 3.5 / 중앙 0.62</text><text x="50" y="210" font-size="13">W 열별 max — 전</text><line x1="50" y1="330" x2="320" y2="330" stroke="currentColor"/><line x1="50" y1="220" x2="50" y2="330" stroke="currentColor"/><line x1="46" y1="330.0" x2="50" y2="330.0" stroke="currentColor"/><text x="43" y="334.0" font-size="12" text-anchor="end">0</text><line x1="46" y1="293.3" x2="50" y2="293.3" stroke="currentColor"/><text x="43" y="297.3" font-size="12" text-anchor="end">4</text><line x1="46" y1="256.7" x2="50" y2="256.7" stroke="currentColor"/><text x="43" y="260.7" font-size="12" text-anchor="end">8</text><line x1="46" y1="220.0" x2="50" y2="220.0" stroke="currentColor"/><text x="43" y="224.0" font-size="12" text-anchor="end">12</text><path d="M50.5 330V320.5M51.4 330V319.0M52.3 330V319.4M53.3 330V320.9M54.2 330V321.0M55.2 330V320.1M56.1 330V318.5M57.0 330V320.5M58.0 330V319.4M58.9 330V321.5M59.8 330V320.5M60.8 330V320.4M61.7 330V321.6M62.7 330V321.3M63.6 330V320.5M64.5 330V321.3M65.5 330V320.4M66.4 330V321.7M67.3 330V322.0M68.3 330V320.9M69.2 330V321.1M70.2 330V318.2M71.1 330V319.4M72.0 330V321.6M73.0 330V319.9M73.9 330V322.1M74.8 330V315.2M75.8 330V317.8M76.7 330V321.0M77.7 330V320.0M78.6 330V320.0M79.5 330V321.5M80.5 330V320.8M81.4 330V320.1M82.3 330V320.5M83.3 330V320.8M84.2 330V321.3M85.2 330V320.9M86.1 330V321.1M87.0 330V322.2M88.0 330V321.3M88.9 330V319.9M89.8 330V320.3M90.8 330V321.7M91.7 330V316.5M92.7 330V319.2M93.6 330V320.3M94.5 330V315.2M95.5 330V317.3M96.4 330V320.9M97.3 330V301.2M98.3 330V320.0M99.2 330V321.4M100.2 330V318.8M101.1 330V317.1M102.0 330V321.0M103.0 330V321.3M103.9 330V319.3M104.8 330V319.6M105.8 330V319.3M106.7 330V320.7M107.7 330V321.5M108.6 330V318.8M109.5 330V320.2M110.5 330V321.3M111.4 330V322.2M112.3 330V317.4M113.3 330V319.8M114.2 330V322.3M115.2 330V319.5M116.1 330V321.1M117.0 330V317.7M118.0 330V320.5M118.9 330V320.0M119.8 330V321.2M120.8 330V320.1M121.7 330V319.2M122.7 330V321.5M123.6 330V321.7M124.5 330V321.2M125.5 330V320.8M126.4 330V322.7M127.3 330V321.5M128.3 330V320.4M129.2 330V321.2M130.2 330V321.0M131.1 330V321.5M132.0 330V321.8M133.0 330V320.4M133.9 330V320.3M134.8 330V321.1M135.8 330V321.9M136.7 330V317.3M137.7 330V320.1M138.6 330V321.4M139.5 330V319.6M140.5 330V320.6M141.4 330V321.2M142.3 330V316.8M143.3 330V321.4M144.2 330V321.6M145.2 330V322.5M146.1 330V321.7M147.0 330V320.0M148.0 330V321.4M148.9 330V321.2M149.8 330V322.3M150.8 330V319.5M151.7 330V316.9M152.7 330V322.3M153.6 330V321.1M154.5 330V321.2M155.5 330V320.5M156.4 330V322.3M157.3 330V320.3M158.3 330V322.7M159.2 330V319.5M160.2 330V322.7M161.1 330V319.8M162.0 330V322.1M163.0 330V317.8M163.9 330V320.3M164.8 330V319.1M165.8 330V317.7M166.7 330V317.6M167.7 330V318.2M168.6 330V321.6M169.5 330V321.8M170.5 330V319.6M171.4 330V319.7M172.3 330V319.7M173.3 330V319.5M174.2 330V321.6M175.2 330V319.9M176.1 330V317.9M177.0 330V316.0M178.0 330V320.4M178.9 330V320.3M179.8 330V317.0M180.8 330V319.0M181.7 330V320.5M182.7 330V319.9M183.6 330V317.6M184.5 330V318.9M185.5 330V318.8M186.4 330V320.1M187.3 330V319.2M188.3 330V319.7M189.2 330V319.6M190.2 330V320.9M191.1 330V320.7M192.0 330V322.4M193.0 330V321.2M193.9 330V323.1M194.8 330V318.0M195.8 330V321.1M196.7 330V320.3M197.7 330V319.5M198.6 330V321.2M199.5 330V320.7M200.5 330V320.4M201.4 330V319.5M202.3 330V320.1M203.3 330V322.1M204.2 330V320.5M205.2 330V320.5M206.1 330V323.1M207.0 330V319.0M208.0 330V321.5M208.9 330V321.6M209.8 330V320.5M210.8 330V320.0M211.7 330V318.5M212.7 330V320.5M213.6 330V320.3M214.5 330V321.2M215.5 330V320.9M216.4 330V319.3M217.3 330V321.2M218.3 330V320.7M219.2 330V321.2M220.2 330V320.6M221.1 330V322.3M222.0 330V319.3M223.0 330V318.3M223.9 330V320.9M224.8 330V322.2M225.8 330V320.9M226.7 330V319.3M227.7 330V321.0M228.6 330V319.5M229.5 330V320.2M230.5 330V320.9M231.4 330V318.6M232.3 330V321.8M233.3 330V319.0M234.2 330V321.3M235.2 330V314.8M236.1 330V315.2M237.0 330V320.5M238.0 330V319.8M238.9 330V318.3M239.8 330V320.0M240.8 330V318.5M241.7 330V320.7M242.7 330V320.9M243.6 330V320.5M244.5 330V321.3M245.5 330V321.8M246.4 330V318.4M247.3 330V321.6M248.3 330V319.3M249.2 330V320.5M250.2 330V320.3M251.1 330V322.3M252.0 330V320.7M253.0 330V318.5M253.9 330V317.3M254.8 330V321.6M255.8 330V321.2M256.7 330V320.8M257.7 330V318.5M258.6 330V320.8M259.5 330V322.6M260.5 330V320.9M261.4 330V319.2M262.3 330V321.6M263.3 330V320.3M264.2 330V321.8M265.2 330V318.9M266.1 330V319.8M267.0 330V320.3M268.0 330V320.1M268.9 330V320.5M269.8 330V320.1M270.8 330V317.9M271.7 330V321.2M272.7 330V320.4M273.6 330V320.7M274.5 330V318.9M275.5 330V318.8M276.4 330V318.0M277.3 330V320.5M278.3 330V321.3M279.2 330V320.5M280.2 330V319.3M281.1 330V320.3M282.0 330V321.3M283.0 330V321.7M283.9 330V322.2M284.8 330V321.5M285.8 330V321.2M286.7 330V319.6M287.7 330V320.0M288.6 330V321.6M289.5 330V319.9M290.5 330V317.9M291.4 330V319.1M292.3 330V316.8M293.3 330V321.6M294.2 330V320.9M295.2 330V318.3M296.1 330V319.9M297.0 330V320.0M298.0 330V320.3M298.9 330V322.7M299.8 330V318.0M300.8 330V320.0M301.7 330V317.3M302.7 330V318.8M303.6 330V318.3M304.5 330V322.3M305.5 330V322.3M306.4 330V318.6M307.3 330V318.6M308.3 330V320.1M309.2 330V323.1M310.2 330V321.0M311.1 330V321.7M312.0 330V320.4M313.0 330V320.2M313.9 330V321.7M314.8 330V321.2M315.8 330V319.7M316.7 330V320.0M317.7 330V318.3M318.6 330V320.8M319.5 330V321.4" stroke="#4a7bd0" stroke-width="0.8"/><text x="320" y="224" font-size="12" text-anchor="end">max 3.1 / 중앙 0.95</text><text x="390" y="210" font-size="13">W 열별 max — 후 (W×s)</text><line x1="390" y1="330" x2="660" y2="330" stroke="currentColor"/><line x1="390" y1="220" x2="390" y2="330" stroke="currentColor"/><line x1="386" y1="330.0" x2="390" y2="330.0" stroke="currentColor"/><text x="383" y="334.0" font-size="12" text-anchor="end">0</text><line x1="386" y1="293.3" x2="390" y2="293.3" stroke="currentColor"/><text x="383" y="297.3" font-size="12" text-anchor="end">4</text><line x1="386" y1="256.7" x2="390" y2="256.7" stroke="currentColor"/><text x="383" y="260.7" font-size="12" text-anchor="end">8</text><line x1="386" y1="220.0" x2="390" y2="220.0" stroke="currentColor"/><text x="383" y="224.0" font-size="12" text-anchor="end">12</text><path d="M390.5 330V324.1M391.4 330V323.8M392.3 330V324.3M393.3 330V324.3M394.2 330V320.3M395.2 330V323.8M396.1 330V323.1M397.0 330V315.8M398.0 330V323.1M398.9 330V324.5M399.8 330V323.7M400.8 330V323.4M401.7 330V323.0M402.7 330V323.8M403.6 330V324.7M404.5 330V322.4M405.5 330V323.2M406.4 330V324.1M407.3 330V324.5M408.3 330V323.1M409.2 330V324.1M410.2 330V323.6M411.1 330V324.2M412.0 330V323.6M413.0 330V324.4M413.9 330V323.5M414.8 330V323.4M415.8 330V323.8M416.7 330V322.8M417.7 330V322.7M418.6 330V324.0M419.5 330V324.8M420.5 330V324.0M421.4 330V324.3M422.3 330V324.0M423.3 330V323.7M424.2 330V324.2M425.2 330V324.3M426.1 330V323.6M427.0 330V324.9M428.0 330V324.7M428.9 330V322.6M429.8 330V324.0M430.8 330V324.9M431.7 330V323.2M432.7 330V323.6M433.6 330V324.8M434.5 330V323.2M435.5 330V322.3M436.4 330V324.1M437.3 330V313.6M438.3 330V324.4M439.2 330V324.1M440.2 330V323.3M441.1 330V324.6M442.0 330V324.3M443.0 330V324.8M443.9 330V323.8M444.8 330V323.6M445.8 330V320.1M446.7 330V324.6M447.7 330V323.9M448.6 330V322.8M449.5 330V325.1M450.5 330V323.1M451.4 330V324.6M452.3 330V323.8M453.3 330V323.5M454.2 330V324.7M455.2 330V323.1M456.1 330V323.3M457.0 330V323.5M458.0 330V324.0M458.9 330V323.3M459.8 330V324.5M460.8 330V324.7M461.7 330V324.2M462.7 330V324.6M463.6 330V321.5M464.5 330V324.7M465.5 330V323.0M466.4 330V324.8M467.3 330V324.4M468.3 330V324.3M469.2 330V324.1M470.2 330V322.3M471.1 330V323.9M472.0 330V324.4M473.0 330V323.4M473.9 330V322.0M474.8 330V324.5M475.8 330V324.2M476.7 330V323.4M477.7 330V323.6M478.6 330V324.3M479.5 330V324.1M480.5 330V324.3M481.4 330V324.7M482.3 330V323.7M483.3 330V324.3M484.2 330V324.2M485.2 330V325.2M486.1 330V323.4M487.0 330V323.5M488.0 330V324.3M488.9 330V321.7M489.8 330V324.3M490.8 330V324.1M491.7 330V323.3M492.7 330V324.0M493.6 330V324.3M494.5 330V324.3M495.5 330V323.2M496.4 330V324.3M497.3 330V323.5M498.3 330V319.8M499.2 330V323.2M500.2 330V325.3M501.1 330V325.1M502.0 330V323.6M503.0 330V324.3M503.9 330V321.2M504.8 330V322.9M505.8 330V297.6M506.7 330V317.4M507.7 330V323.8M508.6 330V324.6M509.5 330V324.7M510.5 330V324.7M511.4 330V323.4M512.3 330V317.8M513.3 330V323.2M514.2 330V324.1M515.2 330V324.1M516.1 330V322.0M517.0 330V322.5M518.0 330V324.1M518.9 330V324.1M519.8 330V323.6M520.8 330V323.7M521.7 330V324.0M522.7 330V324.9M523.6 330V321.4M524.5 330V323.1M525.5 330V322.6M526.4 330V323.7M527.3 330V324.1M528.3 330V323.9M529.2 330V324.5M530.2 330V323.0M531.1 330V323.2M532.0 330V324.8M533.0 330V324.2M533.9 330V324.0M534.8 330V317.6M535.8 330V324.0M536.7 330V324.7M537.7 330V324.7M538.6 330V324.0M539.5 330V323.8M540.5 330V323.5M541.4 330V323.0M542.3 330V323.7M543.3 330V324.4M544.2 330V324.4M545.2 330V324.1M546.1 330V324.9M547.0 330V322.9M548.0 330V324.8M548.9 330V324.7M549.8 330V323.1M550.8 330V325.2M551.7 330V323.2M552.7 330V324.3M553.6 330V324.2M554.5 330V321.1M555.5 330V324.7M556.4 330V323.7M557.3 330V324.2M558.3 330V324.0M559.2 330V324.4M560.2 330V324.2M561.1 330V325.3M562.0 330V324.3M563.0 330V323.5M563.9 330V323.4M564.8 330V324.8M565.8 330V324.3M566.7 330V323.7M567.7 330V324.0M568.6 330V324.0M569.5 330V324.2M570.5 330V323.7M571.4 330V324.3M572.3 330V324.2M573.3 330V323.6M574.2 330V324.2M575.2 330V323.4M576.1 330V323.7M577.0 330V323.9M578.0 330V323.2M578.9 330V323.0M579.8 330V321.6M580.8 330V322.7M581.7 330V324.5M582.7 330V323.8M583.6 330V324.1M584.5 330V324.4M585.5 330V324.3M586.4 330V323.2M587.3 330V322.3M588.3 330V324.2M589.2 330V324.0M590.2 330V323.0M591.1 330V325.1M592.0 330V323.9M593.0 330V324.0M593.9 330V322.8M594.8 330V324.6M595.8 330V323.5M596.7 330V324.6M597.7 330V323.1M598.6 330V324.2M599.5 330V318.6M600.5 330V324.3M601.4 330V324.2M602.3 330V324.8M603.3 330V324.4M604.2 330V323.9M605.2 330V323.6M606.1 330V323.9M607.0 330V323.5M608.0 330V324.1M608.9 330V324.2M609.8 330V323.3M610.8 330V325.0M611.7 330V324.1M612.7 330V324.3M613.6 330V324.3M614.5 330V323.2M615.5 330V323.9M616.4 330V323.7M617.3 330V324.4M618.3 330V321.5M619.2 330V324.7M620.2 330V324.2M621.1 330V324.5M622.0 330V323.9M623.0 330V324.5M623.9 330V324.8M624.8 330V324.4M625.8 330V324.7M626.7 330V324.3M627.7 330V315.0M628.6 330V323.4M629.5 330V324.1M630.5 330V322.9M631.4 330V323.4M632.3 330V322.9M633.3 330V324.4M634.2 330V323.9M635.2 330V323.9M636.1 330V323.8M637.0 330V323.5M638.0 330V324.3M638.9 330V324.7M639.8 330V323.8M640.8 330V324.1M641.7 330V323.1M642.7 330V322.3M643.6 330V322.4M644.5 330V322.3M645.5 330V324.9M646.4 330V323.8M647.3 330V324.5M648.3 330V324.4M649.2 330V324.8M650.2 330V323.4M651.1 330V323.5M652.0 330V324.1M653.0 330V324.7M653.9 330V324.2M654.8 330V324.5M655.8 330V323.7M656.7 330V323.6M657.7 330V323.7M658.6 330V323.0M659.5 330V324.0" stroke="#e08a3c" stroke-width="0.8"/><text x="660" y="224" font-size="12" text-anchor="end">max 3.5 / 중앙 0.62</text><text x="340" y="365" font-size="12" text-anchor="middle">가로축: 입력 채널 0 … 575 (layers.12.self_attn.q_proj, α = 0.5), 세로축 공통 0~12</text></svg>
```

그림 4 — SmoothQuant 전(파랑)과 후(주황)의 채널별 최대 절댓값 (12층 q_proj, α = 0.5, 실측). 활성값의 뾰족한 채널 247(11.9)이 3.5로 눌리고, 그만큼 가중치의 같은 열이 커진다. 후에는 X와 W의 채널별 최대가 똑같다(5.1절의 α = 0.5 성질).

출력에서 볼 것:

- 수학적으로 같은 계산이다(차이 3.55e-15 = double 정밀도의 반올림 수준).
- 활성값의 채널 간 비(max/중앙값)가 30.3 → 5.7로 줄었다. 가중치는 3.3 → 5.7로 늘었다. **어려움을 반씩 나눠 가졌다.**
- 이 층 하나의 W8A8 출력 오차가 7.4% → 1.5%로 5배 줄었다.

### 5.4 코드로 확인: 모델 전체에 SmoothQuant [모델 필요]

모든 Linear에 α를 바꿔 가며 SmoothQuant를 적용하고 W8A8 PPL을 잰다. 활성값은 per-tensor로, **dynamic**(매번 max)과 **static**(calibration으로 고정한 scale) 두 가지를 본다. 채널별 max|X|는 calibration 텍스트로만 구한다.

```python
import torch
from c3_common import load, ppl, linears, EVAL_TEXT, CALIB_TEXT
tok, model = load(); ids = tok(EVAL_TEXT, return_tensors="pt").input_ids
cal = tok(CALIB_TEXT, return_tensors="pt").input_ids
L = linears(model); W0 = {n: m.weight.data.clone() for n, m in L.items()}
fq = lambda x, s: torch.clamp(torch.round(x / s), -127, 127) * s

xmax = {}                                               # 1) calibration: 채널별 max|X|
hs = [m.register_forward_hook(lambda m, i, o, n=n: xmax.__setitem__(n, i[0][0].abs().amax(0)))
      for n, m in L.items()]
with torch.no_grad(): model(cal)
for h in hs: h.remove()

def run(alpha, act):                                    # alpha=None → smoothing 없음
    hs = []
    for n, m in L.items():
        W = W0[n]
        s = torch.ones(W.shape[1]) if alpha is None else \
            (xmax[n].clamp(min=1e-5) ** alpha / W.abs().amax(0).clamp(min=1e-5) ** (1 - alpha))
        Ws = W * s                                      # W·diag(s): 열 j에 s_j를 곱함
        m.weight.data = fq(Ws, Ws.abs().amax(1, keepdim=True) / 127)
        static = (xmax[n] / s).max() / 127              # calibration으로 정한 고정 scale
        def pre(m, inp, s=s, static=static):
            x = inp[0] / s                              # X·diag(s)⁻¹  (실제로는 앞 RMSNorm weight에 fold)
            sc = static if act == "static" else x.abs().amax() / 127
            return (fq(x, sc),)
        hs.append(m.register_forward_pre_hook(pre))
    p = ppl(model, ids)
    for h in hs: h.remove()
    return p

for act in ("dynamic", "static"):
    print(f"W8A8 per-tensor {act:7s}: 없음 {run(None, act):7.2f} | " +
          " ".join(f"α={a}: {run(a, act):6.2f}" for a in (0.3, 0.5, 0.7, 0.85)))
```

```text
W8A8 per-tensor dynamic: 없음   35.60 | α=0.3:  31.88 α=0.5:  20.83 α=0.7:  19.43 α=0.85:  19.43
W8A8 per-tensor static : 없음   36.87 | α=0.3:  32.71 α=0.5:  21.48 α=0.7:  20.25 α=0.85:  21.66
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 350"><line x1="70" y1="290" x2="620" y2="290" stroke="currentColor"/><line x1="70" y1="30" x2="70" y2="290" stroke="currentColor"/><line x1="66" y1="290.0" x2="70" y2="290.0" stroke="currentColor"/><text x="62" y="294.0" font-size="12" text-anchor="end">15</text><line x1="66" y1="238.0" x2="70" y2="238.0" stroke="currentColor"/><text x="62" y="242.0" font-size="12" text-anchor="end">20</text><line x1="66" y1="186.0" x2="70" y2="186.0" stroke="currentColor"/><text x="62" y="190.0" font-size="12" text-anchor="end">25</text><line x1="66" y1="134.0" x2="70" y2="134.0" stroke="currentColor"/><text x="62" y="138.0" font-size="12" text-anchor="end">30</text><line x1="66" y1="82.0" x2="70" y2="82.0" stroke="currentColor"/><text x="62" y="86.0" font-size="12" text-anchor="end">35</text><line x1="66" y1="30.0" x2="70" y2="30.0" stroke="currentColor"/><text x="62" y="34.0" font-size="12" text-anchor="end">40</text><line x1="120.0" y1="290" x2="120.0" y2="295" stroke="currentColor"/><text x="120.0" y="309" font-size="12" text-anchor="middle">없음</text><line x1="232.5" y1="290" x2="232.5" y2="295" stroke="currentColor"/><text x="232.5" y="309" font-size="12" text-anchor="middle">α=0.3</text><line x1="345.0" y1="290" x2="345.0" y2="295" stroke="currentColor"/><text x="345.0" y="309" font-size="12" text-anchor="middle">α=0.5</text><line x1="457.5" y1="290" x2="457.5" y2="295" stroke="currentColor"/><text x="457.5" y="309" font-size="12" text-anchor="middle">α=0.7</text><line x1="570.0" y1="290" x2="570.0" y2="295" stroke="currentColor"/><text x="570.0" y="309" font-size="12" text-anchor="middle">α=0.85</text><line x1="70" y1="257.7" x2="620" y2="257.7" stroke="#888" stroke-dasharray="4 3"/><text x="76" y="252.7" font-size="12">FP32 18.10</text><line x1="70" y1="258.1" x2="620" y2="258.1" stroke="#3f9a6b" stroke-dasharray="4 3"/><text x="76" y="271.1" font-size="12">W8A16 18.06</text><polyline points="120.0,75.8 232.5,114.4 345.0,229.4 457.5,243.9 570.0,243.9" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="120.0" cy="75.8" r="3.5" fill="#4a7bd0"/><circle cx="232.5" cy="114.4" r="3.5" fill="#4a7bd0"/><circle cx="345.0" cy="229.4" r="3.5" fill="#4a7bd0"/><circle cx="457.5" cy="243.9" r="3.5" fill="#4a7bd0"/><circle cx="570.0" cy="243.9" r="3.5" fill="#4a7bd0"/><polyline points="120.0,62.6 232.5,105.8 345.0,222.6 457.5,235.4 570.0,220.7" fill="none" stroke="#e08a3c" stroke-width="2"/><circle cx="120.0" cy="62.6" r="3.5" fill="#e08a3c"/><circle cx="232.5" cy="105.8" r="3.5" fill="#e08a3c"/><circle cx="345.0" cy="222.6" r="3.5" fill="#e08a3c"/><circle cx="457.5" cy="235.4" r="3.5" fill="#e08a3c"/><circle cx="570.0" cy="220.7" r="3.5" fill="#e08a3c"/><text x="128.0" y="54.6" font-size="12">35.60 / 36.87</text><text x="240.5" y="97.8" font-size="12">31.88 / 32.71</text><text x="353.0" y="214.6" font-size="12">20.83 / 21.48</text><text x="465.5" y="227.4" font-size="12">19.43 / 20.25</text><text x="578.0" y="212.7" font-size="12">19.43 / 21.66</text><rect x="380" y="50" width="14" height="12" fill="#4a7bd0"/><text x="400" y="61" font-size="12">활성값 per-tensor dynamic</text><rect x="380" y="72" width="14" height="12" fill="#e08a3c"/><text x="400" y="83" font-size="12">활성값 per-tensor static</text><text x="345" y="330" font-size="12" text-anchor="middle">W8A8 perplexity — SmoothQuant 이동 강도 α별 (가중치는 모두 int8 per-channel)</text></svg>
```

그림 5 — SmoothQuant α에 따른 W8A8 perplexity (실측). smoothing 없이는 35~37로 두 배가 되지만, α = 0.5~0.85에서 19~22로 FP32(18.10)에 크게 다가간다.

출력에서 볼 것:

- **naive W8A8 35.60 → SmoothQuant(α = 0.7) 19.43.** 가장 까다로운 조건인 static per-tensor에서도 36.87 → 20.25다. 이 작은 모델에서도 효과가 분명하다.
- α가 너무 작으면(0.3) 옮긴 양이 부족하고, 너무 크면(0.85, static) 가중치 쪽이 어려워지기 시작한다. **α는 모델마다 튜닝하는 하이퍼파라미터**다. 원 논문은 α = 0.5를 좋은 기본값으로 제시했고, activation outlier가 더 심한 모델에는 더 큰 α를 썼다.
- 그래도 per-token dynamic(4.4절, 18.54)보다는 나쁘다. SmoothQuant는 **채널 방향** outlier를 푸는 방법이고, 첫 token의 거대 활성값 같은 **token 방향** outlier는 per-tensor scale을 여전히 키운다. 실무에서는 SmoothQuant + per-token activation을 함께 쓰기도 한다(HW가 허용하면).

### 5.5 실제 적용할 때 — 어디에 fold하나

위 코드는 이해를 위해 Linear마다 s를 따로 계산하고 입력을 hook에서 나눴다. 실제 구현은 다르다.

| Linear | 입력을 만드는 앞 연산 | s를 흡수하는 곳 | 주의 |
|---|---|---|---|
| q, k, v_proj | input RMSNorm | RMSNorm의 γ ÷ s | q, k, v가 입력을 공유하므로 s도 하나 (세 가중치의 max로 계산) |
| gate, up_proj | post-attention RMSNorm | RMSNorm의 γ ÷ s | 같은 이유로 s 하나 |
| o_proj | attention 출력 (V의 가중합) | v_proj 가중치의 출력 행 ÷ s | attention은 V에 대해 선형이라 가능 |
| down_proj | SiLU(gate) ⊙ up | up_proj 가중치의 출력 행 ÷ s | 곱이 원소별이라 up 쪽에 흡수 가능 |

원 논문은 주로 attention의 입력 쪽(q, k, v)과 FFN의 첫 Linear(gate/up)에 적용했다. 흡수할 곳이 애매한 층은 W8A16으로 남기는 선택도 흔하다.

### 5.6 함정

- s를 RMSNorm에 fold한 뒤 **KV-cache나 다른 경로가 그 RMSNorm 출력을 공유하는지** 확인한다. 공유하는 소비자가 모두 같은 s를 알아야 한다.
- calibration의 max|X|가 작게 잡히면(짧은 텍스트) 실제 입력에서 s가 부족해 static scale이 clipping된다.

---

## 6. GPTQ — 양자화 오차를 남은 가중치가 보상한다

### 6.1 문제 정의: 층 단위 재구성

GPTQ(Frantar et al., 2022)는 "가중치를 원래와 가깝게"가 아니라 **"층의 출력을 원래와 가깝게"**를 목표로 한다. calibration 입력 X(`[token 수, in]`)가 주어지면:

```
min_Ŵ  ‖X·Wᵀ − X·Ŵᵀ‖²        (Ŵ의 각 원소는 int4 격자 위의 값)

행 하나(출력 채널 하나) w에 대해 풀면:
‖X(w − ŵ)‖² = (w − ŵ)ᵀ · (XᵀX) · (w − ŵ)       →   H = 2·XᵀX  (in × in)
```

말로 하면: 출력 오차는 가중치 오차 벡터 d = w − ŵ를 H로 잰 "길이"다. H는 **입력 채널 간의 상관**을 담는다. 두 입력 채널이 늘 같이 움직이면(상관이 크면) 한 채널 가중치의 오차를 다른 채널 가중치로 상쇄할 수 있다. RTN은 이 정보를 전혀 쓰지 않는다. 또 **모든 행이 같은 H를 공유**한다는 점이 중요하다. H는 입력에만 의존하기 때문이다.

### 6.2 직관: error diffusion

GPTQ는 가중치 열(입력 채널)을 **왼쪽부터 하나씩** 양자화한다. 열 i를 반올림해서 생긴 오차를, 아직 양자화하지 않은 오른쪽 열들에 **H가 알려 주는 비율대로 나눠 얹는다**. 오른쪽 열들은 그 오차를 상쇄하는 방향으로 미리 움직인 뒤에 자기 차례에 양자화된다.

펌웨어/신호처리 비유: 이미지 dithering의 **Floyd–Steinberg error diffusion**, 또는 오디오 DAC의 **sigma-delta noise shaping**과 같은 발상이다. 지금 샘플의 양자화 오차를 버리지 않고 다음 샘플들에 넘겨서, 전체 출력(여기서는 X·Wᵀ)의 오차를 줄인다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 260"><text x="20" y="24" font-size="13">가중치 W [out 행 × in 열] — 열(입력 채널) 순서대로 처리</text><g stroke="currentColor" stroke-width="0.8"><rect x="60" y="40" width="50" height="36" fill="#4a7bd0" fill-opacity="0.45"/><rect x="110" y="40" width="50" height="36" fill="#4a7bd0" fill-opacity="0.45"/><rect x="160" y="40" width="50" height="36" fill="#4a7bd0" fill-opacity="0.45"/><rect x="210" y="40" width="50" height="36" fill="#e08a3c" fill-opacity="0.6"/><rect x="260" y="40" width="50" height="36" fill="#3f9a6b" fill-opacity="0.25"/><rect x="310" y="40" width="50" height="36" fill="#3f9a6b" fill-opacity="0.25"/><rect x="360" y="40" width="50" height="36" fill="#3f9a6b" fill-opacity="0.25"/><rect x="410" y="40" width="50" height="36" fill="#3f9a6b" fill-opacity="0.25"/><rect x="60" y="76" width="50" height="36" fill="#4a7bd0" fill-opacity="0.45"/><rect x="110" y="76" width="50" height="36" fill="#4a7bd0" fill-opacity="0.45"/><rect x="160" y="76" width="50" height="36" fill="#4a7bd0" fill-opacity="0.45"/><rect x="210" y="76" width="50" height="36" fill="#e08a3c" fill-opacity="0.6"/><rect x="260" y="76" width="50" height="36" fill="#3f9a6b" fill-opacity="0.25"/><rect x="310" y="76" width="50" height="36" fill="#3f9a6b" fill-opacity="0.25"/><rect x="360" y="76" width="50" height="36" fill="#3f9a6b" fill-opacity="0.25"/><rect x="410" y="76" width="50" height="36" fill="#3f9a6b" fill-opacity="0.25"/><rect x="60" y="112" width="50" height="36" fill="#4a7bd0" fill-opacity="0.45"/><rect x="110" y="112" width="50" height="36" fill="#4a7bd0" fill-opacity="0.45"/><rect x="160" y="112" width="50" height="36" fill="#4a7bd0" fill-opacity="0.45"/><rect x="210" y="112" width="50" height="36" fill="#e08a3c" fill-opacity="0.6"/><rect x="260" y="112" width="50" height="36" fill="#3f9a6b" fill-opacity="0.25"/><rect x="310" y="112" width="50" height="36" fill="#3f9a6b" fill-opacity="0.25"/><rect x="360" y="112" width="50" height="36" fill="#3f9a6b" fill-opacity="0.25"/><rect x="410" y="112" width="50" height="36" fill="#3f9a6b" fill-opacity="0.25"/></g><text x="135" y="168" font-size="12" text-anchor="middle">① 이미 양자화됨 (고정)</text><text x="235" y="186" font-size="12" text-anchor="middle">② 열 i: 반올림, 오차 e</text><text x="360" y="168" font-size="12" text-anchor="middle">③ 남은 열 −= e ⊗ (H⁻¹ 행 i 비율)</text><path d="M 250 30 C 290 5, 380 5, 430 34" fill="none" stroke="#d0564a" stroke-width="1.8"/><polygon points="426,36 436,38 432,28" fill="#d0564a"/><text x="345" y="14" font-size="12" text-anchor="middle">오차를 오른쪽으로 전파</text><text x="490" y="64" font-size="12">모든 행이 같은 H를 쓰므로</text><text x="490" y="82" font-size="12">열 i의 처리는 행 전체를</text><text x="490" y="100" font-size="12">한 번에 벡터로 한다</text><text x="490" y="130" font-size="12">열 순서는 모든 행이 공통</text><text x="20" y="220" font-size="12">비용: 열 하나당 (out × 남은 열) 갱신 → 층 하나 O(out·in²). H⁻¹는 Cholesky로 한 번만 계산.</text><text x="20" y="242" font-size="12">원 논문은 128열 블록 단위로 갱신을 모아서(lazy batch) GPU 메모리 대역폭 병목을 줄였다.</text></svg>
```

그림 6 — GPTQ의 처리 순서. 파랑은 이미 int4로 고정된 열, 주황은 지금 반올림하는 열, 초록은 오차 보상을 받아 값이 바뀌는 중인 열이다.

### 6.3 손으로 계산: 가중치 2개 [독립]

입력 채널 2개가 양의 상관을 가진다고 하자: H = XᵀX = [[2, 1], [1, 2]]. 가중치 w = [0.62, 0.20]을 아주 거친 격자(0.5 간격)로 양자화한다.

- RTN: [0.5, 0.0]. 오차 d = [0.12, 0.20]. 출력 오차 dᵀHd = 2(0.0144) + 2(0.12)(0.20) + 2(0.04) = 0.0288 + 0.048 + 0.08 = **0.1568**.
- GPTQ: 먼저 w₁ = 0.62 → 0.5, 오차 e₁ = 0.12. 두 번째 가중치가 이것을 보상하도록 옮긴다. d₁ = e₁가 고정됐을 때 dᵀHd를 d₂로 미분해서 0으로 두면 d₂ = −e₁·H₁₂/H₂₂이므로, 목표값은 w₂ + e₁·H₁₂/H₂₂ = 0.20 + 0.12 × 1/2 = **0.26**. 이것을 반올림하면 0.5. 결과 [0.5, 0.5], d = [0.12, −0.30], 출력 오차 = 0.0288 − 0.072 + 0.18 = **0.1368**.

w₂ 혼자 보면 0.20 → 0.5는 RTN(0.0)보다 **더 먼** 값이다. 그런데 출력 오차는 더 작다. x₁과 x₂가 같이 움직이므로 w₁이 모자란 만큼(0.12) w₂가 더 받아 주는 편이 출력에는 낫다. **가중치 공간의 오차가 아니라 출력 공간의 오차를 줄인다**는 것이 GPTQ의 핵심이다.

```python
import numpy as np
H = np.array([[2.0, 1.0], [1.0, 2.0]])      # H = XᵀX : 두 입력 채널이 양의 상관
w = np.array([0.62, 0.20])                  # 한 출력 행의 weight 2개
q = lambda v: np.round(v / 0.5) * 0.5       # 격자 0.5 간격으로 반올림 (아주 거친 양자화)
err = lambda wq: float((w - wq) @ H @ (w - wq))   # 출력 오차 ||X(w - ŵ)||² = dᵀHd

rtn = q(w)
print("RTN :", rtn, " 출력 오차 =", round(err(rtn), 4))

q1 = q(w[0]); e1 = w[0] - q1                # 1) 첫 열 양자화, 오차 e1
w2 = w[1] + e1 * H[0, 1] / H[1, 1]          # 2) 남은 열이 오차를 보상 (2×2에선 이 식이 OBS 갱신과 같다)
gptq = np.array([q1, q(w2)])
print(f"GPTQ: e1 = {e1:.2f}, w2 갱신 {w[1]:.2f} → {w2:.2f} → 양자화 {q(w2):.1f}")
print("GPTQ:", gptq, " 출력 오차 =", round(err(gptq), 4))
Hi = np.linalg.inv(H)                       # 일반식: δ = -(e1 / [H⁻¹]₁₁)·[H⁻¹]₁₂
print("OBS 일반식으로 본 w2 갱신량:", round(-e1 / Hi[0, 0] * Hi[0, 1], 4))
```

```text
RTN : [0.5 0. ]  출력 오차 = 0.1568
GPTQ: e1 = 0.12, w2 갱신 0.20 → 0.26 → 양자화 0.5
GPTQ: [0.5 0.5]  출력 오차 = 0.1368
OBS 일반식으로 본 w2 갱신량: 0.06
```

출력에서 볼 것: 2×2에서 쓴 직관적인 식(e₁·H₁₂/H₂₂)과 OBS 일반식(−e₁·[H⁻¹]₁₂/[H⁻¹]₁₁)이 같은 갱신량 0.06을 준다. 일반식은 남은 열이 여러 개일 때도 그대로 쓸 수 있다.

### 6.4 알고리즘

GPTQ는 OBS(Optimal Brain Surgeon)/OBQ 계열의 식을 쓰되, 큰 LLM에서 돌아가도록 세 가지를 바꿨다.

```
입력: W [out, in], calibration 입력 X [N, in]
1. H = 2·XᵀX + λ·I              (λ = damping, 보통 mean(diag H)의 1%)
2. U = chol(H⁻¹)ᵀ  (upper)       (H⁻¹ = UᵀU. 매 단계 역행렬을 새로 구하지 않기 위해)
3. for i = 0 … in−1:             (모든 행에 대해 같은 순서)
       q_i = quant(W[:, i])                          (group 시작이면 현재 W로 scale 결정)
       e   = (W[:, i] − q_i) / U[i, i]
       W[:, i+1:] −= e ⊗ U[i, i+1:]                  (남은 열 보상)
```

말로 하면: 열 하나를 반올림하고, 그 오차를 H⁻¹의 Cholesky 인수가 알려 주는 비율대로 남은 열에 뺀다. 원 논문의 세 가지 변경은 (1) 모든 행이 같은 열 순서를 써서 H⁻¹를 공유, (2) 128열씩 갱신을 모아서 처리(lazy batch), (3) Cholesky 형태로 수치 안정성 확보다. 원 논문은 C4 데이터셋의 무작위 2048-token 구간 128개를 calibration에 썼다.

실무 옵션 두 가지:

- **act-order (desc_act)**: H의 대각(입력 채널 에너지)이 큰 열부터 처리한다. 중요한 열을 먼저, 보상 여유가 많을 때 양자화하므로 정확도가 좋아진다. 대신 group과 열 순서가 어긋나서, 커널이 "이 열은 몇 번 group의 scale을 쓰나"(g_idx) 표를 따라가야 한다. 이것이 추론 커널을 느리게 만들 수 있어서 기기 배포에서는 끄기도 한다.
- **damping λ**: H가 거의 특이(singular)하면 H⁻¹이 폭발한다. calibration token 수가 in 차원보다 적으면 XᵀX의 rank가 부족하므로 damping이 필수다.

### 6.5 코드로 확인: 한 층에 GPTQ 구현 [모델 필요]

위 알고리즘을 30줄로 구현한 `c3_gptq.py`다. 양자화기는 int4 비대칭 min-max(범위에 0 포함)이고, 3절의 비대칭 RTN과 같은 격자다. 수치 안정을 위해 double로 계산한다.

```python
import torch
from c3_common import load, linears, EVAL_TEXT, CALIB_TEXT
tok, model = load(); L = linears(model)
def grab(name, text):                                   # 한 Linear의 입력 X [tokens, in]
    out = {}; h = L[name].register_forward_hook(lambda m, i, o: out.__setitem__("x", i[0][0].detach()))
    with torch.no_grad(): model(tok(text, return_tensors="pt").input_ids)
    h.remove(); return out["x"].double()

def qparams(w, bits=4):                                 # 비대칭 min-max, w: [rows, k] → 행마다 scale, zero
    lo, hi = w.amin(1, keepdim=True).clamp(max=0), w.amax(1, keepdim=True).clamp(min=0)
    s = ((hi - lo) / (2**bits - 1)).clamp(min=1e-8); return s, torch.round(-lo / s)
def quant(w, s, z, bits=4):
    return (torch.clamp(torch.round(w / s) + z, 0, 2**bits - 1) - z) * s

def rtn(W, gs):
    Q = W.clone()
    for c in range(0, W.shape[1], gs):
        s, z = qparams(W[:, c:c+gs]); Q[:, c:c+gs] = quant(W[:, c:c+gs], s, z)
    return Q

def gptq(W, X, gs, damp=0.01):
    W = W.clone(); Q = torch.zeros_like(W); n = W.shape[1]
    H = 2 * X.T @ X                                     # Hessian of ||XWᵀ - XŴᵀ||²  (행마다 같다)
    H += damp * H.diag().mean() * torch.eye(n)          # 수치 안정용 damping
    U = torch.linalg.cholesky(torch.cholesky_inverse(torch.linalg.cholesky(H)), upper=True)  # H⁻¹ = UᵀU
    for i in range(n):
        if i % gs == 0: s, z = qparams(W[:, i:i+gs])    # 그룹에 들어설 때 (갱신된) W로 scale 결정
        Q[:, i] = quant(W[:, i:i+1], s, z)[:, 0]
        err = (W[:, i] - Q[:, i]) / U[i, i]
        W[:, i+1:] -= err[:, None] * U[i, i+1:][None, :]  # 남은 열들이 오차를 떠안는다
    return Q
```

세 층에서 RTN과 GPTQ의 **출력 상대오차** ‖X(W − Ŵ)ᵀ‖ / ‖XWᵀ‖를 calibration 텍스트(GPTQ가 본 데이터)와 eval 텍스트(처음 보는 데이터)에서 각각 잰다.

```python
from c3_gptq import *                                   # grab, rtn, gptq, L, model, tok
for name in ("12.self_attn.q_proj", "20.mlp.up_proj", "5.mlp.down_proj"):
    W = L[name].weight.data.double()
    Xc, Xe = grab(name, CALIB_TEXT), grab(name, EVAL_TEXT)   # calib 483 token, eval 522 token
    rel = lambda X, Q: float((X @ (W - Q).T).norm() / (X @ W.T).norm())   # 출력 상대오차
    for gs in (W.shape[1], 64):
        Qr, Qg = rtn(W, gs), gptq(W, Xc, gs)
        tag = "per-ch" if gs == W.shape[1] else f"g{gs}"
        print(f"{name:19s} in={W.shape[1]:4d} {tag:6s} | RTN calib {rel(Xc, Qr):.4f} eval {rel(Xe, Qr):.4f}"
              f" | GPTQ calib {rel(Xc, Qg):.4f} eval {rel(Xe, Qg):.4f}")
```

```text
12.self_attn.q_proj in= 576 per-ch | RTN calib 0.1264 eval 0.1279 | GPTQ calib 0.0285 eval 0.0499
12.self_attn.q_proj in= 576 g64    | RTN calib 0.0973 eval 0.0985 | GPTQ calib 0.0206 eval 0.0368
20.mlp.up_proj      in= 576 per-ch | RTN calib 0.1418 eval 0.1411 | GPTQ calib 0.0535 eval 0.1291
20.mlp.up_proj      in= 576 g64    | RTN calib 0.1029 eval 0.1032 | GPTQ calib 0.0392 eval 0.0946
5.mlp.down_proj     in=1536 per-ch | RTN calib 0.1344 eval 0.1350 | GPTQ calib 0.0473 eval 0.1808
5.mlp.down_proj     in=1536 g64    | RTN calib 0.0903 eval 0.0907 | GPTQ calib 0.0326 eval 0.1225
```

출력에서 볼 것:

- **12층 q_proj: GPTQ가 크게 이긴다.** eval 기준 12.8% → 5.0%(per-channel), 9.9% → 3.7%(g64). 처음 보는 텍스트에서도 오차가 절반 이하다.
- **20층 up_proj: 이기지만 차이가 줄어든다.** calibration에서는 14.2% → 5.4%인데 eval에서는 14.1% → 12.9%다.
- **5층 down_proj: 과적합(overfitting).** calibration에서는 13.4% → 4.7%로 좋아 보이지만, eval에서는 13.5% → **18.1%로 RTN보다 나빠진다.** calibration 483 token에 맞춰 오차를 "보상"한 방향이 다른 텍스트에서는 오히려 오차를 키웠다.

이 차이는 어디서 올까? 다음 예제가 답이다.

### 6.6 코드로 확인: 언제 GPTQ가 일반화되나 [모델 필요]

세 층의 H(입력 상관 행렬)를 비교한다. 고유값 분포로 본 "유효 차원 수", 채널별 첨도(kurtosis, 꼬리가 두꺼운 정도, Gaussian = 3), 그리고 calibration 텍스트의 H와 eval 텍스트의 H가 얼마나 다른지를 잰다.

```python
from c3_gptq import *
def eff_rank(H):                                        # 고유값 분포의 '유효 차원 수' = exp(entropy)
    ev = torch.linalg.eigvalsh(H).clamp(min=1e-12); p = ev / ev.sum()
    return float(torch.exp(-(p * p.log()).sum()))
for name in ("12.self_attn.q_proj", "20.mlp.up_proj", "5.mlp.down_proj"):
    Xc, Xe = grab(name, CALIB_TEXT), grab(name, EVAL_TEXT)
    Hc, He = Xc.T @ Xc, Xe.T @ Xe
    Hc, He = Hc / Hc.trace(), He / He.trace()           # 크기 정규화 후 모양만 비교
    kurt = ((Xc - Xc.mean(0)) ** 4).mean(0) / Xc.var(0) ** 2   # 채널별 첨도 (Gaussian = 3)
    print(f"{name:19s} 유효 차원 {eff_rank(Hc):6.1f} / {Xc.shape[1]:4d} | 첨도 평균 {kurt.mean():5.1f}"
          f" | H(calib) vs H(eval) 차이 {float((Hc - He).norm() / He.norm()):.3f}")
```

```text
12.self_attn.q_proj 유효 차원    2.7 /  576 | 첨도 평균   3.4 | H(calib) vs H(eval) 차이 0.088
20.mlp.up_proj      유효 차원   35.5 /  576 | 첨도 평균   3.2 | H(calib) vs H(eval) 차이 0.534
5.mlp.down_proj     유효 차원  231.2 / 1536 | 첨도 평균  20.4 | H(calib) vs H(eval) 차이 1.141
```

출력에서 볼 것:

- **q_proj의 H는 유효 차원이 2.7밖에 안 되고, 텍스트가 바뀌어도 거의 같다(차이 0.088).** 입력 에너지가 outlier 채널 247 같은 몇 방향에 몰려 있고, 그 구조가 텍스트와 무관하게 늘 같다. 그래서 483 token으로 추정한 H가 eval에서도 맞다.
- **down_proj의 H는 유효 차원이 231이고 꼬리가 두꺼우며(첨도 20), calibration과 eval의 H가 크게 다르다(차이 1.14).** 1536차원 공간의 수백 방향을 483 token으로 추정해야 하니 H가 calibration 텍스트 고유의 잡음을 담고, GPTQ는 그 잡음에 맞춰 보상한다.

이것을 확인하는 대조 실험: down_proj 입력과 **같은 공분산을 갖는 Gaussian 가짜 데이터**로 calibration과 test를 모두 만들면(= calibration이 test를 대표하는 이상적 상황), GPTQ는 어떻게 될까?

```python
from c3_gptq import *
torch.manual_seed(0)
name = "5.mlp.down_proj"; W = L[name].weight.data.double(); X = grab(name, CALIB_TEXT)
C = X.T @ X / X.shape[0]                                # 실제 활성값의 공분산 (1536×1536)
Lc = torch.linalg.cholesky(C + 1e-6 * torch.eye(C.shape[0]))
sample = lambda n: torch.randn(n, C.shape[0], dtype=torch.float64) @ Lc.T   # 같은 통계의 가짜 X
Xtest = sample(5000)
rel = lambda X, Q: float((X @ (W - Q).T).norm() / (X @ W.T).norm())
Qr = rtn(W, 64); print(f"RTN g64          test {rel(Xtest, Qr):.4f}")
for n in (500, 2000, 8000, 32000):
    print(f"GPTQ g64 calib {n:5d} token → test {rel(Xtest, gptq(W, sample(n), 64)):.4f}")
```

```text
RTN g64          test 0.0903
GPTQ g64 calib   500 token → test 0.0406
GPTQ g64 calib  2000 token → test 0.0338
GPTQ g64 calib  8000 token → test 0.0327
GPTQ g64 calib 32000 token → test 0.0326
```

출력에서 볼 것: calibration이 test와 같은 분포에서 오면 **500 token만으로도 GPTQ가 RTN 오차를 절반 이하로(9.0% → 4.1%) 줄인다.** 알고리즘은 맞게 동작한다. 문제는 **calibration 데이터가 실제 입력 분포를 대표하느냐**다. 원 논문이 다양한 웹 텍스트(C4)에서 뽑은 128 × 2048 ≈ 26만 token을 calibration에 쓴 이유이고, 실무에서 "calibration set을 제품 도메인에 맞춰 구성하라"고 하는 이유다. 예를 들어 음성 비서라면 실제 사용자 발화와 비슷한 텍스트를 calibration에 넣어야 한다.

### 6.7 코드로 확인: 모델 전체에 GPTQ [모델 필요]

위 관찰에 따라 입력이 576차원인 층(q, k, v, o, gate, up)에만 GPTQ를 쓰고 down_proj는 RTN으로 둔다. 원 논문은 층을 순서대로 양자화하면서 **이미 양자화된 앞 층의 출력**을 다음 층의 calibration 입력으로 쓰지만, 여기서는 단순화를 위해 FP 모델에서 한 번에 모은 입력을 쓴다.

```python
import time
from c3_gptq import *
from c3_common import ppl
ids = tok(EVAL_TEXT, return_tensors="pt").input_ids; cal = tok(CALIB_TEXT, return_tensors="pt").input_ids
W0 = {n: m.weight.data.clone() for n, m in L.items()}
X = {}                                                  # 모든 Linear의 calibration 입력을 한 번에 수집
hs = [m.register_forward_hook(lambda m, i, o, n=n: X.__setitem__(n, i[0][0].double())) for n, m in L.items()]
with torch.no_grad(): model(cal)
for h in hs: h.remove()
t = time.perf_counter()
for gs in (None, 64):
    for mode in ("RTN", "GPTQ"):
        for n, m in L.items():
            W = W0[n].double(); g = gs or W.shape[1]
            use = mode == "GPTQ" and W.shape[1] == 576  # down_proj(in=1536)은 과적합이라 RTN 유지
            m.weight.data = (gptq(W, X[n], g) if use else rtn(W, g)).float()
        print(f"int4 {'g64' if gs else 'per-channel':11s} {mode:4s}: ppl {ppl(model, ids):7.3f}  ({time.perf_counter()-t:.0f} s)")
```

```text
int4 per-channel RTN : ppl  33.142  (1 s)
int4 per-channel GPTQ: ppl  29.488  (20 s)
int4 g64         RTN : ppl  22.660  (21 s)
int4 g64         GPTQ: ppl  22.553  (42 s)
```

출력에서 볼 것:

- per-channel에서는 33.14 → 29.49로 분명히 좋아졌다.
- g64에서는 22.66 → 22.55로 잡음 수준이다. group이 작아지면 RTN 자체의 오차가 작아져서 보상할 여지가 줄고, down_proj(RTN 유지)의 오차가 남는다.
- **정직한 결론: 이 작은 모델과 483-token calibration에서는 GPTQ의 전체 모델 이득이 제한적이다.** 층 단위 오차(6.5절)에서 보인 큰 이득이 PPL로 다 옮겨지지 않았다. 충분히 크고 대표성 있는 calibration, 순차적(sequential) 양자화, act-order를 쓰는 실제 구현은 더 나은 결과를 낸다고 보고되어 있다.

### 6.8 함정

- calibration 텍스트를 eval과 같게 두면 GPTQ의 과적합을 못 본다. 반드시 분리한다.
- calibration token 수가 in_features보다 적으면 XᵀX가 rank 부족이다. damping 없이는 Cholesky가 실패한다.
- GPTQ 결과물은 "가중치 값"만 바뀐 평범한 int4 텐서다. **추론 커널은 RTN과 같다**(act-order의 g_idx만 예외). 즉 GPTQ의 비용은 오프라인 양자화 시간뿐이다.

---

## 7. AWQ — 중요한 채널을 키워서 보호한다

### 7.1 아이디어

Lin et al.(2023)의 관찰: 가중치 중 **약 1%만 "중요(salient)"하게 FP16으로 남겨 두면** int4의 정확도 손실이 크게 준다. 그런데 어떤 가중치가 중요한지는 가중치 크기가 아니라 **그 가중치와 곱해지는 활성값의 크기**가 알려 준다(activation-aware). 4절의 채널 247처럼 늘 큰 입력 채널에 붙은 가중치는 작은 오차도 크게 증폭되기 때문이다.

하지만 일부만 FP16으로 두는 mixed precision은 커널을 복잡하게 만든다. AWQ의 해법은 SmoothQuant와 같은 등가 변환이다.

```
y = ∑ⱼ xⱼ · wⱼ = ∑ⱼ (xⱼ / sⱼ) · (sⱼ · wⱼ)
Ŵ = Q(W · diag(s)) · diag(s)⁻¹      (가중치 열 j를 sⱼ배 키워서 양자화하고, 다시 나눈다)
sⱼ = (mean|Xⱼ|)^α,  α ∈ [0, 1]을 격자 탐색해서 층 출력 오차가 최소인 값 선택
```

말로 하면: 중요한 채널의 가중치를 s배 키우면 양자화 격자에 비해 상대적으로 커져서 **상대 오차가 1/s로 준다**(group의 최대값, 즉 칸 폭이 크게 변하지 않는다면). 추론 때는 활성값을 s로 나눠야 하는데, 이것은 SmoothQuant처럼 앞 층(RMSNorm γ 등)에 흡수한다. 결과물은 **여전히 평범한 W4A16 텐서**다.

손으로 계산: group 안의 가중치 최대가 0.9라서 int4 대칭 칸 폭 Δ = 0.9/7 = 0.1286이다. 중요 채널의 가중치 w = 0.20, 입력 평균 크기 10.

- 그냥 양자화: 0.20/0.1286 = 1.56 → 2 → 0.2571, 오차 0.0571, 출력 오차 기여 ≈ 10 × 0.0571 = 0.571
- s = 2로 키움: 0.40/0.1286 = 3.11 → 3 → 0.3857, 다시 ÷2 → 0.1929, 오차 0.0071, 출력 오차 기여 ≈ 0.071

오차 상한으로 보면 Δ/2 = 0.064에서 Δ/(2s) = 0.032로 절반이 된다. 대가는 활성값을 s로 나눠야 한다는 것(흡수하면 공짜)과, s를 너무 키우면 그 열이 group 최대값이 되어 **Δ 자체가 커진다**는 것이다. 그래서 α를 탐색한다.

### 7.2 코드로 확인: 한 층에서 RTN vs AWQ vs GPTQ [모델 필요]

`c3_awq.py`: α를 0부터 1까지 격자로 바꿔 가며 calibration 출력 오차가 가장 작은 s를 고른다. s의 크기 정규화(최대·최소의 기하평균으로 나누기)는 공개 AWQ 구현의 방식을 따랐다.

```python
from c3_gptq import *
def awq(W, X, gs, grid=20):
    """s_j = mean|X_j|^α 로 중요한 입력 채널을 키워서 양자화 → 다시 나눈다. α는 격자 탐색."""
    xmean = X.abs().mean(0)                              # 입력 채널별 평균 크기 (salient 지표)
    Y = X @ W.T; best = (float("inf"), None, None)
    for a in [i / grid for i in range(grid + 1)]:
        s = xmean.clamp(min=1e-4) ** a
        s = s / (s.max() * s.min()).sqrt()               # scale 크기 정규화 (AWQ 구현 방식)
        Q = rtn(W * s, gs) / s                           # Q(W·diag(s))·diag(s)⁻¹
        err = float((X @ Q.T - Y).norm())
        if err < best[0]: best = (err, a, Q)
    return best[2], best[1]
```

```python
from c3_awq import *                                    # awq + (c3_gptq의) grab, rtn, gptq
for name in ("12.self_attn.q_proj", "20.mlp.up_proj", "5.mlp.down_proj"):
    W = L[name].weight.data.double(); Xc, Xe = grab(name, CALIB_TEXT), grab(name, EVAL_TEXT)
    rel = lambda X, Q: float((X @ (W - Q).T).norm() / (X @ W.T).norm())
    for gs in (W.shape[1], 64):
        Qa, a = awq(W, Xc, gs)
        tag = "per-ch" if gs == W.shape[1] else f"g{gs}"
        print(f"{name:19s} {tag:6s} eval 오차 | RTN {rel(Xe, rtn(W, gs)):.4f} | AWQ {rel(Xe, Qa):.4f} "
              f"(α={a:.2f}) | GPTQ {rel(Xe, gptq(W, Xc, gs)):.4f}")
```

```text
12.self_attn.q_proj per-ch eval 오차 | RTN 0.1279 | AWQ 0.0599 (α=0.35) | GPTQ 0.0499
12.self_attn.q_proj g64    eval 오차 | RTN 0.0985 | AWQ 0.0398 (α=0.45) | GPTQ 0.0368
20.mlp.up_proj      per-ch eval 오차 | RTN 0.1411 | AWQ 0.1143 (α=0.40) | GPTQ 0.1291
20.mlp.up_proj      g64    eval 오차 | RTN 0.1032 | AWQ 0.0795 (α=0.45) | GPTQ 0.0946
5.mlp.down_proj     per-ch eval 오차 | RTN 0.1350 | AWQ 0.1352 (α=0.05) | GPTQ 0.1808
5.mlp.down_proj     g64    eval 오차 | RTN 0.0907 | AWQ 0.0906 (α=0.35) | GPTQ 0.1225
```

출력에서 볼 것:

- **AWQ는 세 층 모두에서 RTN보다 나쁘지 않다.** q_proj에서 12.8% → 6.0%, up_proj에서 14.1% → 11.4%, down_proj에서는 RTN과 같다(α ≈ 0 근처를 골랐다 = "이 층은 건드리지 마라").
- **GPTQ는 q_proj에서 AWQ보다 조금 낫지만, down_proj에서는 과적합으로 RTN보다 나쁘다.** AWQ는 층마다 α 하나(스칼라 1개)만 고르므로 calibration에 과적합할 자유도가 거의 없다. GPTQ는 가중치 전체를 calibration에 맞춰 움직인다.
- 이것이 흔히 말하는 "AWQ는 calibration에 덜 민감하다"의 실측 버전이다.

### 7.3 코드로 확인: 모델 전체에 AWQ [모델 필요]

모든 Linear 210개에 AWQ를 적용한다(시간을 줄이려 α 격자는 11단계).

```python
import time
from c3_awq import *
from c3_common import ppl
ids = tok(EVAL_TEXT, return_tensors="pt").input_ids; cal = tok(CALIB_TEXT, return_tensors="pt").input_ids
W0 = {n: m.weight.data.clone() for n, m in L.items()}
X = {}; hs = [m.register_forward_hook(lambda m, i, o, n=n: X.__setitem__(n, i[0][0].double())) for n, m in L.items()]
with torch.no_grad(): model(cal)
for h in hs: h.remove()
t = time.perf_counter()
for gs in (None, 64):
    for mode in ("RTN", "AWQ"):
        for n, m in L.items():
            W = W0[n].double(); g = gs or W.shape[1]
            m.weight.data = (awq(W, X[n], g, grid=10)[0] if mode == "AWQ" else rtn(W, g)).float()
        print(f"int4 {'g64' if gs else 'per-channel':11s} {mode}: ppl {ppl(model, ids):7.3f}  ({time.perf_counter()-t:.0f} s)")
```

```text
int4 per-channel RTN: ppl  33.142  (1 s)
int4 per-channel AWQ: ppl  23.596  (8 s)
int4 g64         RTN: ppl  22.660  (10 s)
int4 g64         AWQ: ppl  20.578  (20 s)
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 340"><line x1="70" y1="270" x2="620" y2="270" stroke="currentColor"/><line x1="70" y1="30" x2="70" y2="270" stroke="currentColor"/><line x1="66" y1="270.0" x2="70" y2="270.0" stroke="currentColor"/><text x="62" y="274.0" font-size="12" text-anchor="end">0</text><line x1="66" y1="235.7" x2="70" y2="235.7" stroke="currentColor"/><text x="62" y="239.7" font-size="12" text-anchor="end">5</text><line x1="66" y1="201.4" x2="70" y2="201.4" stroke="currentColor"/><text x="62" y="205.4" font-size="12" text-anchor="end">10</text><line x1="66" y1="167.1" x2="70" y2="167.1" stroke="currentColor"/><text x="62" y="171.1" font-size="12" text-anchor="end">15</text><line x1="66" y1="132.9" x2="70" y2="132.9" stroke="currentColor"/><text x="62" y="136.9" font-size="12" text-anchor="end">20</text><line x1="66" y1="98.6" x2="70" y2="98.6" stroke="currentColor"/><text x="62" y="102.6" font-size="12" text-anchor="end">25</text><line x1="66" y1="64.3" x2="70" y2="64.3" stroke="currentColor"/><text x="62" y="68.3" font-size="12" text-anchor="end">30</text><line x1="66" y1="30.0" x2="70" y2="30.0" stroke="currentColor"/><text x="62" y="34.0" font-size="12" text-anchor="end">35</text><rect x="110" y="42.7" width="56" height="227.3" fill="#4a7bd0" fill-opacity="0.85"/><text x="138" y="37.7" font-size="12" text-anchor="middle">33.14</text><rect x="180" y="67.8" width="56" height="202.2" fill="#e08a3c" fill-opacity="0.85"/><text x="208" y="62.8" font-size="12" text-anchor="middle">29.49</text><rect x="250" y="108.2" width="56" height="161.8" fill="#3f9a6b" fill-opacity="0.85"/><text x="278" y="103.2" font-size="12" text-anchor="middle">23.60</text><text x="208" y="290" font-size="13" text-anchor="middle">int4 per-channel (asym)</text><rect x="380" y="114.6" width="56" height="155.4" fill="#4a7bd0" fill-opacity="0.85"/><text x="408" y="109.6" font-size="12" text-anchor="middle">22.66</text><rect x="450" y="115.4" width="56" height="154.6" fill="#e08a3c" fill-opacity="0.85"/><text x="478" y="110.4" font-size="12" text-anchor="middle">22.55</text><rect x="520" y="128.9" width="56" height="141.1" fill="#3f9a6b" fill-opacity="0.85"/><text x="548" y="123.9" font-size="12" text-anchor="middle">20.58</text><text x="478" y="290" font-size="13" text-anchor="middle">int4 g64 (asym)</text><line x1="70" y1="145.9" x2="620" y2="145.9" stroke="#888" stroke-dasharray="4 3"/><text x="76" y="140.9" font-size="12">FP32 18.10</text><rect x="90" y="308" width="14" height="12" fill="#4a7bd0"/><text x="110" y="319" font-size="12">RTN</text><rect x="260" y="308" width="14" height="12" fill="#e08a3c"/><text x="280" y="319" font-size="12">GPTQ (in=576 층만)</text><rect x="430" y="308" width="14" height="12" fill="#3f9a6b"/><text x="450" y="319" font-size="12">AWQ (전체 층)</text></svg>
```

그림 7 — 모델 전체 int4 perplexity 비교 (실측). AWQ가 두 설정 모두에서 가장 좋다. GPTQ는 과적합 때문에 in = 576인 층에만 적용했다.

출력에서 볼 것:

- **per-channel int4: RTN 33.14 → AWQ 23.60.** 4-bit per-channel만으로 g64 RTN(22.66) 근처까지 온다. group scale을 저장하지 않고도(4.0 bit/w) 거의 같은 품질이다.
- **g64: RTN 22.66 → AWQ 20.58.** FP32(18.10)와의 격차가 4.56 → 2.48로 절반 가까이 줄었다.
- AWQ는 전체 모델에 적용해도 설정당 10초 안팎이다. calibration 입력의 채널별 평균만 있으면 되고 H(in × in)를 만들 필요가 없다.

### 7.4 GPTQ vs AWQ 정리

| 항목 | GPTQ | AWQ |
|---|---|---|
| 핵심 아이디어 | 열마다 반올림 오차를 남은 열에 보상 (2차 정보 H) | 활성값이 큰 채널의 가중치를 키워 상대 오차 축소 |
| calibration에서 쓰는 것 | XᵀX 전체 (in × in) | 채널별 평균 크기 + 층 출력 오차 |
| 자유도 | 가중치 전부 | 층마다 α 하나 (스칼라) |
| calibration 과적합 위험 | 있다 (이 노트 6.5절) | 작다 |
| 양자화 시간 | 길다 (층당 O(out·in²)) | 짧다 |
| 결과 형식 | 일반 int4 (+ act-order면 g_idx) | 일반 int4 (s는 앞 층에 흡수) |
| 추론 커널 | RTN과 같음 | RTN과 같음 |
| 함께 쓰기 | AWQ 스케일링 후 GPTQ를 거는 조합도 쓰인다 | |

두 방법 모두 **추론 비용은 0**이고 오프라인 도구의 선택이라는 점이 중요하다. 기기 쪽 엔지니어 입장에서는 "우리 커널이 받는 형식(group 크기, 대칭 여부, zero-point 형식, g_idx 지원 여부)"이 도구 선택을 제한한다.

---

## 8. 실무 형식 — GGUF 양자화 타입, int4 packing, NPU

### 8.1 llama.cpp GGUF의 양자화 타입

llama.cpp(F3)는 모델을 GGUF 파일 하나에 담고, 텐서마다 양자화 타입을 가진다. 이름은 `Q` + bit 수 + `_` + 변형 이름이다(예: `Q4_0`, `Q4_K`). 아래는 블록 구조가 비교적 확실한 것들이다(세부는 ggml 소스와 버전에 따라 다를 수 있으니 F3에서 직접 확인한다).

| 타입 | 블록 | 블록 내용 | bit/weight | 복원식 |
|---|---|---|---|---|
| Q8_0 | 32개 | FP16 scale d + int8 32개 = 34 B | 8.5 | x = d·q |
| Q4_0 | 32개 | FP16 scale d + 4-bit 32개(16 B) = 18 B | 4.5 | x = d·(q − 8) |
| Q4_1 | 32개 | FP16 d + FP16 min m + 16 B = 20 B | 5.0 | x = d·q + m |
| Q4_K | 256개 super-block | 32개짜리 sub-block 8개, sub-block마다 6-bit scale·min + super-block의 FP16 d, dmin | 4.5 | 2단계 scale |
| Q6_K | 256개 super-block | 6-bit 값 + sub-block scale | 약 6.56 | |

**Q4_K_M**에서 `_M`(medium)은 "모든 텐서를 Q4_K로" 가 아니라 **텐서 종류에 따라 섞는 레시피**다. 대부분은 Q4_K를 쓰고, 민감한 일부 텐서(예: attention의 V 가중치, FFN down 가중치의 일부)는 Q6_K 같은 더 높은 형식을 쓰는 것으로 알려져 있다. 그래서 파일 전체의 평균 bit/weight는 4.5보다 약간 크다. 정확한 레시피는 llama.cpp 버전에 따라 바뀌므로 `llama-quantize`의 출력 로그로 확인하는 것이 확실하다. 또 llama.cpp에는 calibration 텍스트로 채널 중요도(importance matrix, imatrix)를 계산해서 양자화에 반영하는 옵션이 있다. AWQ와 비슷한 "activation-aware" 발상이다.

### 8.2 int4 packing — 한 바이트에 두 개

int4 값 두 개를 한 바이트에 담는다. Q4_0 참조 구현은 블록 32개 중 **앞 16개를 low nibble, 뒤 16개를 high nibble**에 넣는 배치를 쓴다(짝수/홀수 인접 배치가 아니다). 커널이 low nibble 16개를 한 번에, high nibble 16개를 한 번에 SIMD로 풀기 좋은 배치다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 230"><text x="20" y="22" font-size="13">Q4_0 스타일 블록 (weight 32개 → 18 바이트)</text><rect x="20" y="40" width="80" height="40" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor"/><text x="60" y="58" font-size="12" text-anchor="middle">d (FP16)</text><text x="60" y="74" font-size="12" text-anchor="middle">2 B</text><g stroke="currentColor" stroke-width="0.8"><rect x="110" y="40" width="30" height="40" fill="#4a7bd0" fill-opacity="0.35"/><rect x="140" y="40" width="30" height="40" fill="#3f9a6b" fill-opacity="0.35"/><rect x="180" y="40" width="30" height="40" fill="#4a7bd0" fill-opacity="0.35"/><rect x="210" y="40" width="30" height="40" fill="#3f9a6b" fill-opacity="0.35"/><rect x="250" y="40" width="30" height="40" fill="#4a7bd0" fill-opacity="0.35"/><rect x="280" y="40" width="30" height="40" fill="#3f9a6b" fill-opacity="0.35"/><rect x="380" y="40" width="30" height="40" fill="#4a7bd0" fill-opacity="0.35"/><rect x="410" y="40" width="30" height="40" fill="#3f9a6b" fill-opacity="0.35"/></g><text x="125" y="65" font-size="12" text-anchor="middle">q0</text><text x="155" y="65" font-size="12" text-anchor="middle">q16</text><text x="195" y="65" font-size="12" text-anchor="middle">q1</text><text x="225" y="65" font-size="12" text-anchor="middle">q17</text><text x="265" y="65" font-size="12" text-anchor="middle">q2</text><text x="295" y="65" font-size="12" text-anchor="middle">q18</text><text x="345" y="65" font-size="14" text-anchor="middle">…</text><text x="395" y="65" font-size="12" text-anchor="middle">q15</text><text x="425" y="65" font-size="12" text-anchor="middle">q31</text><text x="140" y="98" font-size="12" text-anchor="middle">qs[0]</text><text x="210" y="98" font-size="12" text-anchor="middle">qs[1]</text><text x="280" y="98" font-size="12" text-anchor="middle">qs[2]</text><text x="410" y="98" font-size="12" text-anchor="middle">qs[15]</text><rect x="470" y="40" width="14" height="12" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor"/><text x="490" y="51" font-size="12">low nibble: q[j] (j = 0…15)</text><rect x="470" y="62" width="14" height="12" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><text x="490" y="73" font-size="12">high nibble: q[j + 16]</text><text x="20" y="135" font-size="12">풀기:  lo = (qs[j] &amp; 0x0F) − 8,   hi = (qs[j] &gt;&gt; 4) − 8,   w = d · lo 또는 d · hi</text><text x="20" y="160" font-size="12">SIMD 한 번에: 16바이트 load → AND 0x0F로 low 16개, 4-bit shift로 high 16개 → −8 → 곱셈-누적</text><text x="20" y="190" font-size="12">저장: 원래 q ∈ [−8, 7] → +8 해서 [0, 15]의 부호 없는 nibble로 저장 (offset binary)</text><text x="20" y="215" font-size="12">bit/weight = (2 + 16) × 8 / 32 = 4.5</text></svg>
```

그림 8 — Q4_0 스타일 블록의 메모리 배치. 바이트 j의 아래 4비트는 j번 가중치, 위 4비트는 j+16번 가중치다.

### 8.3 코드로 확인: C로 pack → dequant-on-the-fly dot product [C]

Q4_0 스타일 양자화(블록 32, d = "절댓값이 가장 큰 값 ÷ −8", q+8을 nibble로 저장)와, 블록 안은 정수 q로 곱-누적하고 scale은 블록마다 한 번만 곱하는 dot product를 C로 짠다. `cc -std=c11 -Wall -Wextra -O2 q4.c -lm`으로 경고 0개를 확인했다.

```c
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#define QK 32                                   /* 블록 = weight 32개 (Q4_0 스타일) */
typedef struct { float d; uint8_t qs[QK / 2]; } block_q4;   /* 실제 GGUF는 d가 fp16(2 B) */

static void quantize_block(const float *x, block_q4 *b) {
    float amax = 0.0f, maxv = 0.0f;             /* 절댓값이 가장 큰 원소(부호 포함) */
    for (int i = 0; i < QK; i++) if (fabsf(x[i]) > amax) { amax = fabsf(x[i]); maxv = x[i]; }
    b->d = maxv / -8.0f;                        /* 가장 큰 값이 -8에 딱 맞게 */
    const float id = b->d != 0.0f ? 1.0f / b->d : 0.0f;
    for (int j = 0; j < QK / 2; j++) {          /* 앞 16개 → low nibble, 뒤 16개 → high nibble */
        int q0 = (int)roundf(x[j] * id) + 8, q1 = (int)roundf(x[j + QK / 2] * id) + 8;
        q0 = q0 < 0 ? 0 : (q0 > 15 ? 15 : q0);  q1 = q1 < 0 ? 0 : (q1 > 15 ? 15 : q1);
        b->qs[j] = (uint8_t)(q0 | (q1 << 4));
    }
}
static float dot_q4(const block_q4 *b, const float *a, int nblk) {   /* dequant-on-the-fly */
    float sum = 0.0f;
    for (int k = 0; k < nblk; k++, a += QK) {
        float part = 0.0f;                      /* 블록 안은 정수 q로 누적, scale은 끝에 1번 */
        for (int j = 0; j < QK / 2; j++) {
            part += (float)((b[k].qs[j] & 0x0F) - 8) * a[j];
            part += (float)((b[k].qs[j] >> 4) - 8) * a[j + QK / 2];
        }
        sum += b[k].d * part;
    }
    return sum;
}
int main(void) {
    enum { N = 128 };
    float w[N], a[N]; block_q4 blk[N / QK];
    uint32_t s = 12345u;                        /* 재현 가능한 LCG 난수 */
    for (int i = 0; i < N; i++) {
        s = s * 1664525u + 1013904223u; w[i] = ((float)(s >> 8) / 16777216.0f - 0.5f) * 0.2f;
        s = s * 1664525u + 1013904223u; a[i] = ((float)(s >> 8) / 16777216.0f - 0.5f) * 4.0f;
    }
    w[5] = 0.31f;                               /* 블록 0에 큰 weight 하나 */
    for (int k = 0; k < N / QK; k++) quantize_block(&w[k * QK], &blk[k]);
    float ref = 0.0f, ref_dq = 0.0f, sig = 0.0f, noise = 0.0f;
    for (int i = 0; i < N; i++) {
        const block_q4 *b = &blk[i / QK]; int j = i % QK;          /* 참조용: 먼저 전부 dequant */
        int qv = (j < QK / 2 ? (b->qs[j] & 0x0F) : (b->qs[j - QK / 2] >> 4)) - 8;
        float wd = b->d * (float)qv;
        ref += w[i] * a[i]; ref_dq += wd * a[i];
        sig += w[i] * w[i]; noise += (w[i] - wd) * (w[i] - wd);
    }
    float q = dot_q4(blk, a, N / QK);
    printf("block_q4 in this demo = %zu B (GGUF Q4_0: fp16 d 2 B + 16 B = 18 B -> %.2f bit/weight)\n",
           sizeof(block_q4), 18.0 * 8 / QK);
    printf("block scale d: blk0 %.5f (w[5]=0.31 outlier)  blk1 %.5f  blk2 %.5f\n", blk[0].d, blk[1].d, blk[2].d);
    printf("qs[0] of blk1 = 0x%02X -> low %d, high %d\n", blk[1].qs[0], (blk[1].qs[0] & 15) - 8, (blk[1].qs[0] >> 4) - 8);
    printf("dot: fp32 %.6f | dequant-first %.6f | on-the-fly %.6f\n", ref, ref_dq, q);
    printf("weight SQNR = %.1f dB\n", 10.0f * log10f(sig / noise));
    printf("bytes for %d weights: fp32 %d, fp16 %d, q4_0 %d\n", N, N * 4, N * 2, (N / QK) * 18);
    return 0;
}
```

```text
block_q4 in this demo = 20 B (GGUF Q4_0: fp16 d 2 B + 16 B = 18 B -> 4.50 bit/weight)
block scale d: blk0 -0.03875 (w[5]=0.31 outlier)  blk1 -0.01250  blk2 0.01218
qs[0] of blk1 = 0xC5 -> low -3, high 4
dot: fp32 -1.126943 | dequant-first -1.346945 | on-the-fly -1.346945
weight SQNR = 19.2 dB
bytes for 128 weights: fp32 512, fp16 256, q4_0 72
```

출력에서 볼 것:

- **on-the-fly 커널과 "먼저 전부 dequant한 뒤 dot"이 같은 값(−1.346945)이다.** 커널 검증은 이렇게 한다. 양자화 오차(FP32 −1.127과의 차이)와 커널 버그를 분리해야 한다. FP32와의 차이가 커 보이는 것은 무작위 부호의 128개 항이 서로 상쇄해서 dot 자체가 작기 때문이고, 양자화 품질은 가중치 SQNR(19.2 dB, 4-bit에서 흔한 수준)로 판단한다.
- 블록 0에 큰 값(0.31)을 하나 넣었더니 **그 블록의 scale만 3배**(−0.03875 vs −0.0125)가 되고 다른 블록은 영향이 없다. per-group의 장점을 C 레벨에서 본 것이다.
- 이 데모 구조체는 `float d`라서 20 B지만 실제 GGUF는 FP16 d라서 18 B다. 128개 가중치가 FP32 512 B → Q4_0 72 B로 7.1배 줄었다.
- d가 음수인 이유: 절댓값 최대인 원소가 −8에 정확히 대응되도록 부호까지 포함해 d를 잡는다. 그러면 [−8, 7]의 비대칭 범위를 한쪽 끝까지 쓸 수 있다.

### 8.4 벤더 NPU는 무엇을 원하나

정확한 지원 형식은 SDK와 버전마다 다르므로 **반드시 해당 벤더 문서로 확인**해야 한다. 일반적인 경향만 적으면:

- 모바일 NPU의 LLM 경로는 흔히 **가중치 4-bit 또는 8-bit + 활성값 16-bit(INT16 또는 FP16)** 조합을 쓴다. 활성값을 16-bit로 두면 outlier 문제를 피하면서 가중치 대역폭 이득은 챙길 수 있다.
- 전통적인 CNN용 정수 NPU 경로는 **W8A8 per-tensor static**을 가장 잘 지원한다. 이 경로로 LLM을 돌리려면 SmoothQuant 같은 전처리가 필요하다.
- group-wise int4를 하드웨어가 직접 지원하는지(group 크기, scale 형식)는 칩마다 다르다. 지원하지 않으면 per-channel int4 + AWQ가 현실적인 대안이다(7.3절에서 per-channel AWQ가 g64 RTN에 근접했다).
- CPU 경로(llama.cpp)는 Q4_0 가중치와 곱할 때 **활성값도 블록 단위 8-bit로 바꿔 정수 dot product**를 쓰는 것으로 알고 있다. 즉 CPU에서는 사실상 W4A8에 가깝게 계산한다(구현 세부는 F3에서 확인).

---

## 9. KV-cache 양자화

### 9.1 KV-cache는 얼마나 큰가

B4 5.3절의 공식:

```
KV 바이트/token = 2 × 층 수 × KV head 수 × head_dim × (바이트/원소)
Llama-3.2-1B, FP16:  2 × 16 × 8 × 64 × 2 B = 32 KiB/token
4K context:          32 KiB × 4096 = 128 MiB
```

말로 하면: K와 V 두 개, 층마다, KV head마다 head_dim개 숫자를 token마다 쌓는다. 가중치는 context와 무관하게 고정이지만 KV는 context에 **선형으로** 자란다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 340"><line x1="80" y1="280" x2="500" y2="280" stroke="currentColor"/><line x1="80" y1="30" x2="80" y2="280" stroke="currentColor"/><line x1="76" y1="280.0" x2="80" y2="280.0" stroke="currentColor"/><text x="72" y="284.0" font-size="12" text-anchor="end">0</text><line x1="76" y1="217.5" x2="80" y2="217.5" stroke="currentColor"/><text x="72" y="221.5" font-size="12" text-anchor="end">200</text><line x1="76" y1="155.0" x2="80" y2="155.0" stroke="currentColor"/><text x="72" y="159.0" font-size="12" text-anchor="end">400</text><line x1="76" y1="92.5" x2="80" y2="92.5" stroke="currentColor"/><text x="72" y="96.5" font-size="12" text-anchor="end">600</text><line x1="76" y1="30.0" x2="80" y2="30.0" stroke="currentColor"/><text x="72" y="34.0" font-size="12" text-anchor="end">800</text><line x1="80.0" y1="280" x2="80.0" y2="285" stroke="currentColor"/><text x="80.0" y="299" font-size="12" text-anchor="middle">0</text><line x1="185.0" y1="280" x2="185.0" y2="285" stroke="currentColor"/><text x="185.0" y="299" font-size="12" text-anchor="middle">4K</text><line x1="290.0" y1="280" x2="290.0" y2="285" stroke="currentColor"/><text x="290.0" y="299" font-size="12" text-anchor="middle">8K</text><line x1="395.0" y1="280" x2="395.0" y2="285" stroke="currentColor"/><text x="395.0" y="299" font-size="12" text-anchor="middle">12K</text><line x1="500.0" y1="280" x2="500.0" y2="285" stroke="currentColor"/><text x="500.0" y="299" font-size="12" text-anchor="middle">16K</text><line x1="80.0" y1="280.0" x2="500.0" y2="120.0" stroke="#d0564a" stroke-width="2"/><text x="506.0" y="124.0" font-size="12" text-anchor="start">KV FP16: 32 KiB/token</text><line x1="80.0" y1="280.0" x2="500.0" y2="197.5" stroke="#e08a3c" stroke-width="2"/><text x="506.0" y="201.5" font-size="12" text-anchor="start">KV INT8: 16.5 KiB/token</text><line x1="80.0" y1="280.0" x2="500.0" y2="237.5" stroke="#4a7bd0" stroke-width="2"/><text x="506.0" y="241.5" font-size="12" text-anchor="start">KV INT4: 8.5 KiB/token</text><line x1="80" y1="72.8" x2="500" y2="72.8" stroke="#3f9a6b" stroke-dasharray="5 4" stroke-width="2"/><text x="86" y="66.8" font-size="12">INT4(g32) 가중치 663 MiB</text><text x="290" y="320" font-size="12" text-anchor="middle">context 길이 (token) — Llama-3.2-1B, 세로축 MiB</text></svg>
```

그림 9 — Llama-3.2-1B의 KV-cache 크기 vs context 길이. 16K context에서 FP16 KV(512 MiB)는 INT4 가중치(663 MiB)에 버금간다. INT8 KV는 절반, INT4 KV는 약 1/4이다(token·head마다 FP16 scale 포함).

decode의 매 step은 **지금까지의 KV 전체를 읽는다**(attention이 모든 과거 token의 K, V와 곱해지므로). 그래서 KV 바이트는 메모리 용량뿐 아니라 **token당 읽기 대역폭**에도 더해진다(10절에서 계산).

### 9.2 무엇을 어떤 단위로 양자화하나

K, V 텐서의 모양은 `[batch, kv_heads, seq, head_dim]`이다. scale을 나누는 방법:

| 방식 | scale을 공유하는 값 | 장점 | 단점 |
|---|---|---|---|
| per-token (per-head) | token 하나, head 하나의 64개 | 새 token이 올 때 바로 양자화해서 append 가능 | 채널 outlier에 약함 |
| per-token + group | 64개를 32개씩 | 더 촘촘 | scale 저장 2배 |
| per-channel (K) | 한 채널의 여러 token | K의 채널 outlier에 강함 | token이 쌓여야 scale 결정 → 최근 token은 FP16으로 잠시 보관 |

KIVI(Liu et al., 2024)는 **K는 per-channel, V는 per-token**으로 양자화하는 것이 좋다고 보고했다. K에는 특정 채널이 늘 큰 구조가 있고(RoPE와 관련이 있다는 분석도 있다), V에는 그런 구조가 약하기 때문이다. 우리 모델에서 확인해 보자.

### 9.3 코드로 확인: K와 V의 모양 [모델 필요]

transformers의 attention 함수 교체 기능(`AttentionInterface`)으로, attention에 들어가는 K, V(RoPE 적용 후, 즉 캐시에 저장되는 바로 그 값)를 잡는다. 각 head에서 채널 간 최대값의 흩어짐(채널별 max의 max/중앙값)과 token 간 흩어짐을 비교한다.

```python
import torch
from transformers import AttentionInterface
from transformers.integrations.sdpa_attention import sdpa_attention_forward
from c3_common import load, EVAL_TEXT
tok, model = load(); ids = tok(EVAL_TEXT, return_tensors="pt").input_ids
cap = []                                                # 층마다 (K, V) 저장: [kv_heads, seq, head_dim]
def capture(module, q, k, v, mask, **kw):
    cap.append((k[0], v[0])); return sdpa_attention_forward(module, q, k, v, mask, **kw)
AttentionInterface.register("cap", capture); model.set_attn_implementation("cap")
with torch.no_grad(): model(ids)
ratio = lambda m: [round(float(r), 1) for r in m.amax(-1) / m.median(-1).values]   # head별 max/중앙값
for layer in (3, 12, 25):
    for nm, t in zip("KV", cap[layer]):
        ch = t.abs().amax(1)                            # 채널별 max (token 방향으로 max) [heads, 64]
        tk = t.abs().amax(2)                            # token별 max (채널 방향으로 max) [heads, seq]
        print(f"layer {layer:2d} {nm}: 채널 간 max/중앙 {ratio(ch)} | token 간 max/중앙 {ratio(tk)}")
```

```text
layer  3 K: 채널 간 max/중앙 [2.9, 2.0, 4.4] | token 간 max/중앙 [1.4, 1.5, 1.3]
layer  3 V: 채널 간 max/중앙 [1.7, 1.9, 2.2] | token 간 max/중앙 [1.9, 2.3, 2.5]
layer 12 K: 채널 간 max/중앙 [3.7, 4.6, 4.2] | token 간 max/중앙 [1.3, 1.2, 1.2]
layer 12 V: 채널 간 max/중앙 [1.9, 1.8, 2.2] | token 간 max/중앙 [2.5, 2.3, 1.6]
layer 25 K: 채널 간 max/중앙 [4.3, 5.1, 4.4] | token 간 max/중앙 [1.5, 1.3, 1.3]
layer 25 V: 채널 간 max/중앙 [2.3, 1.8, 1.5] | token 간 max/중앙 [3.1, 2.3, 2.0]
```

출력에서 볼 것:

- **K는 채널 간 비가 크고(2.0~5.1), token 간 비는 작다(1.2~1.5).** 몇몇 채널이 모든 token에서 크다. per-token scale을 쓰면 그 큰 채널이 scale을 정해서 나머지 채널이 거칠어진다. per-channel scale이 맞다.
- **V는 반대로 token 간 비(1.6~3.1)가 채널 간 비(1.5~2.3)보다 크거나 비슷하다.** per-token scale이 자연스럽다.

### 9.4 코드로 확인: KV 양자화가 perplexity에 주는 영향 [모델 필요]

attention 함수에서 K, V를 fake-quant한 뒤 원래 SDPA를 호출한다. teacher forcing으로 PPL을 잴 때 모든 query가 양자화된 K, V를 보게 되므로, **양자화된 KV-cache로 decode하는 것과 같은 효과**다(per-token 양자화는 token마다 독립이라 캐시에 넣을 때 한 번 양자화하는 것과 결과가 같다). 모두 비대칭이다.

```python
import torch
from transformers import AttentionInterface
from transformers.integrations.sdpa_attention import sdpa_attention_forward
from c3_common import load, ppl, EVAL_TEXT
tok, model = load(); ids = tok(EVAL_TEXT, return_tensors="pt").input_ids

def fq(t, bits, dim=-1, gs=None):                       # 비대칭 fake-quant, dim 방향으로 묶어 scale 1개
    sh = t.shape
    if gs: t = t.reshape(*sh[:-1], -1, gs)              # head_dim 64를 gs개씩 쪼갬
    lo, hi = t.amin(dim, keepdim=True), t.amax(dim, keepdim=True)
    s = ((hi - lo) / (2**bits - 1)).clamp(min=1e-8); z = torch.round(-lo / s)
    return ((torch.clamp(torch.round(t / s) + z, 0, 2**bits - 1) - z) * s).reshape(sh)

CFG = {"k": None, "v": None}                            # k, v: [batch, kv_heads=3, seq, head_dim=64]
def kvq_attention(module, q, k, v, mask, **kw):         # RoPE 뒤, 캐시에 저장되는 바로 그 K, V
    if CFG["k"]: k = CFG["k"](k)
    if CFG["v"]: v = CFG["v"](v)
    return sdpa_attention_forward(module, q, k, v, mask, **kw)
AttentionInterface.register("kvq", kvq_attention); model.set_attn_implementation("kvq")

tests = {"FP32 (기준)":                   (None, None),
         "K,V int8 per-token":            (lambda k: fq(k, 8), lambda v: fq(v, 8)),
         "K,V int4 per-token":            (lambda k: fq(k, 4), lambda v: fq(v, 4)),
         "K,V int4 per-token g32":        (lambda k: fq(k, 4, gs=32), lambda v: fq(v, 4, gs=32)),
         "K int8 + V int4 per-token":     (lambda k: fq(k, 8), lambda v: fq(v, 4)),
         "K int4 + V int8 per-token":     (lambda k: fq(k, 4), lambda v: fq(v, 8)),
         "K int4 per-channel, V int4 tok": (lambda k: fq(k, 4, dim=-2), lambda v: fq(v, 4))}
if __name__ == "__main__":
    for name, (fk, fv) in tests.items():
        CFG["k"], CFG["v"] = fk, fv
        print(f"{name:32s} ppl {ppl(model, ids):7.3f}")
```

```text
FP32 (기준)                        ppl  18.101
K,V int8 per-token               ppl  18.080
K,V int4 per-token               ppl  20.988
K,V int4 per-token g32           ppl  19.542
K int8 + V int4 per-token        ppl  18.351
K int4 + V int8 per-token        ppl  20.165
K int4 per-channel, V int4 tok   ppl  18.841
```

출력에서 볼 것:

- **KV int8 per-token은 무손실**(18.08). KV-cache int8은 거의 공짜로 메모리를 절반으로 줄인다.
- **KV int4 per-token은 눈에 띄게 나빠진다**(20.99). group 32로 나누면 19.54로 회복된다.
- **K가 V보다 민감하다.** K int8 + V int4 = 18.35(거의 무손실), K int4 + V int8 = 20.17. 비트를 아껴야 한다면 V를 먼저 줄인다.
- **K per-channel + V per-token(KIVI 방식) int4 = 18.84.** 같은 4-bit인데 per-token(20.99)보다 훨씬 좋다. 9.3절 관찰과 맞는다.
- 주의: 이 PPL 실험의 K per-channel은 **522 token 전체**로 채널 scale을 정했다. 실제 decode에서는 미래 token을 모르므로 KIVI처럼 token을 일정 개수(예: 32~128)씩 모아서 채널 scale을 정하고, 아직 덜 찬 최근 token들은 FP16으로 두는 방식을 쓴다. 그래서 실제 결과는 이 숫자보다 약간 나쁠 수 있다.

### 9.5 코드로 확인: 생성 결과 비교 [모델 필요]

같은 질문에 greedy로 40 token을 생성해서, FP32 KV와 int8/int4 KV의 출력이 어디서 갈라지는지 본다. `generate()`는 KV-cache를 쓰는 진짜 decode 루프이고, 매 step 캐시에서 꺼낸 K, V가 위 attention 함수에서 양자화된다.

```python
import torch
from c3_kv import *                                     # 예제 18의 fq, CFG, model (kvq attention 등록됨)
prompt = tok.apply_chat_template([{"role": "user", "content": "Explain what a lighthouse does."}],
                                 add_generation_prompt=True, return_tensors="pt")
outs = {}
for name, fk, fv in (("FP32", None, None), ("int8", lambda k: fq(k, 8), lambda v: fq(v, 8)),
                     ("int4", lambda k: fq(k, 4), lambda v: fq(v, 4))):
    CFG["k"], CFG["v"] = fk, fv
    with torch.no_grad():
        o = model.generate(prompt, attention_mask=torch.ones_like(prompt), max_new_tokens=40, do_sample=False)
    outs[name] = o[0, prompt.shape[1]:]
for name in ("int8", "int4"):
    same = int((outs[name] == outs["FP32"]).cumprod(0).sum())
    print(f"{name} KV: 처음 {same}개 token이 FP32와 같음 →", repr(tok.decode(outs[name][:22])))
print("FP32 KV:", repr(tok.decode(outs["FP32"][:22])))
```

```text
int8 KV: 처음 40개 token이 FP32와 같음 → 'A lighthouse is a beacon that serves as a landmark, a symbol of a place, and a beacon of hope'
int4 KV: 처음 4개 token이 FP32와 같음 → 'A lighthouse is a type of lighthouse that is designed to provide light for ships and sailors to navigate the dark waters'
FP32 KV: 'A lighthouse is a beacon that serves as a landmark, a symbol of a place, and a beacon of hope'
```

출력에서 볼 것:

- **int8 KV는 40 token 전부 FP32와 같다.**
- int4 KV는 4 token 뒤에 갈라진다("A lighthouse is a" 다음). 갈라진 뒤의 문장도 말은 되지만, 내용이 약간 이상하다("a type of lighthouse"). PPL 20.99라는 숫자가 실제 생성에서는 이런 모습이다.
- greedy 비교는 싸고 직관적이지만, 확률이 비슷한 후보 사이에서는 작은 오차로도 갈라진다. 갈라진 위치 하나로 품질을 판단하지 말고 PPL·과제 정확도와 함께 본다.

### 9.6 실무 연결

- llama.cpp는 KV-cache 타입을 K, V 따로 고르는 옵션(예: `--cache-type-k q8_0`, `--cache-type-v q8_0`)을 제공한다. V를 양자화하려면 flash attention 경로가 필요하다는 제약이 있는 것으로 알고 있다(버전별로 F3에서 확인).
- 펌웨어 관점: KV-cache는 **append-only 링버퍼**에 가깝다. per-token 양자화는 새 항목을 쓸 때 한 번 양자화하고 끝이라 구현이 쉽다. per-channel K는 "32개가 찰 때까지 FP16 staging 버퍼에 모았다가 한 번에 양자화해서 본 버퍼로 flush"하는 구조가 된다. DMA 버스트 단위로 모아서 쓰는 것과 같은 패턴이다.

---

## 10. 임베디드 관점에서 다시 보기

### 10.1 dequant-on-the-fly 커널

W4A16이 속도로 이어지려면 **int4를 FP16으로 푼 결과를 메모리에 다시 쓰지 않아야** 한다. 풀어서 DRAM에 쓰면 읽는 바이트가 오히려 늘어난다. 커널의 한 루프는 이렇다.

```
for 블록 in 행:                          (블록 = 32개, 18 B)
    16 B load → 레지스터 하나              ← DRAM 읽기는 이것뿐
    low = v & 0x0F ; high = v >> 4        ← 레지스터 안에서 nibble 분리
    (int8로) −8 → FP16 변환 또는 int8 그대로
    acc_blk = dot(q, x_블록)               ← FP16 FMA 또는 int8 dot(sdot 등)
    acc += d × acc_blk                      ← scale은 블록당 1번
```

말로 하면: DRAM에서 4.5 bit/weight만 읽고, 나머지는 전부 레지스터 안의 ALU 작업이다. decode는 memory-bound라 ALU가 놀고 있으므로 **nibble 분리와 변환 비용은 거의 숨겨진다.** 반대로 prefill(compute-bound)에서는 이 변환 비용이 드러나서 W4A16이 FP16보다 느릴 수도 있다. 그래서 prefill에는 dequant한 가중치 타일을 재사용하거나 정수 경로(W4A8)를 쓴다.

펌웨어 비유: 압축된 펌웨어 이미지를 flash에서 읽어 SRAM에서 풀면서 실행하는 XIP + 압축 해제와 같다. 버스가 느리면 압축이 이득이고, CPU가 병목이면 압축이 손해다.

### 10.2 코드로 확인: 웨어러블 대역폭에서 가중치 + KV 조합 [독립]

Llama-3.2-1B, 4K context가 가득 찬 상태에서 가중치 형식과 KV 형식 조합별로 token당 읽는 바이트와 decode 상한을 계산한다. 대역폭 17.1 GB/s는 B8에서 쓴 **가정값**이다(Hark 같은 기기의 실제 메모리 사양은 모른다).

```python
P, Lyr, KVH, HD = 1_235_814_400, 16, 8, 64      # Llama-3.2-1B (B4 5.4절, B8 4.2절)
BW = 17.1e9                                      # 웨어러블급 가정 대역폭
w_bits  = {"FP16": 16, "INT8": 8, "INT4 g32": 4.5}
kv_bits = {"FP16": 16, "INT8": 8 + 16/64, "INT4": 4 + 16/64}   # token·head(64개)마다 FP16 scale
ctx = 4096
print(f"KV/token: " + ", ".join(f"{k} {2*Lyr*KVH*HD*b/8/1024:.2f} KiB" for k, b in kv_bits.items()))
print(f"\n{'weights':9s} {'KV':5s} {'W MiB':>6s} {'KV@4K MiB':>9s} {'MB/token':>9s} {'tok/s 상한':>10s}")
for wn, wb in w_bits.items():
    for kn, kb in kv_bits.items():
        if wn == "FP16" and kn != "FP16": continue
        W = P * wb / 8; KV = 2 * Lyr * KVH * HD * kb / 8 * ctx
        print(f"{wn:9s} {kn:5s} {W/2**20:6.0f} {KV/2**20:9.0f} {(W+KV)/1e6:9.0f} {BW/(W+KV):10.1f}")
for c in (512, 4096, 16384):                     # context가 길어지면 KV가 주인공이 된다
    kv16 = 2 * Lyr * KVH * HD * 2 * c; w4 = P * 4.5 / 8
    print(f"ctx {c:5d}: INT4 가중치 {w4/2**20:.0f} MiB vs FP16 KV {kv16/2**20:.0f} MiB ({kv16/(w4+kv16):.0%} of bytes/token)")
```

```text
KV/token: FP16 32.00 KiB, INT8 16.50 KiB, INT4 8.50 KiB

weights   KV     W MiB KV@4K MiB  MB/token   tok/s 상한
FP16      FP16    2357       128      2606        6.6
INT8      FP16    1179       128      1370       12.5
INT8      INT8    1179        66      1305       13.1
INT8      INT4    1179        34      1271       13.4
INT4 g32  FP16     663       128       829       20.6
INT4 g32  INT8     663        66       764       22.4
INT4 g32  INT4     663        34       731       23.4
ctx   512: INT4 가중치 663 MiB vs FP16 KV 16 MiB (2% of bytes/token)
ctx  4096: INT4 가중치 663 MiB vs FP16 KV 128 MiB (16% of bytes/token)
ctx 16384: INT4 가중치 663 MiB vs FP16 KV 512 MiB (44% of bytes/token)
```

출력에서 볼 것:

- **가중치 FP16 → INT4가 가장 큰 이득**(6.6 → 20.6 tok/s, 3.1배).
- INT4 가중치 위에서 KV를 FP16 → INT4로 바꾸면 20.6 → 23.4 tok/s(+14%). 4K에서는 KV가 token당 바이트의 16%라 이득이 제한적이다.
- **context가 길수록 KV 양자화가 중요해진다.** 16K에서는 FP16 KV가 token당 바이트의 44%다. 긴 대화 기록이나 긴 문서를 다루는 기능이 있다면 KV 양자화는 선택이 아니라 필수다.
- 이 숫자는 이론 상한이다. 실제로는 메모리 효율(50~80%), 커널 효율, attention·norm·sampler 오버헤드로 더 낮다(B8 4.6절).

### 10.3 정확도 vs 속도 정리 (이 노트의 SmolLM2-135M 실측 + 1B 계산)

| 설정 | PPL (135M 실측) | 가중치 bit/w | 1B decode 상한 @17.1 GB/s, 4K | 비고 |
|---|---|---|---|---|
| FP32/FP16 | 18.10 | 16 | 6.6 tok/s | 기준 |
| W8A16 RTN | 18.06 | 8 | 12.5 | 무손실, 가장 안전 |
| W8A8 naive per-tensor | 35.60 | 8 | 12.5 | outlier로 붕괴 |
| W8A8 SmoothQuant (α 0.7, static) | 20.25 | 8 | 12.5 | 정수 NPU 경로 |
| W4A16 RTN g64 | 22.66 | 4.31 | 약 21 | 기준선 |
| W4A16 AWQ g64 | 20.58 | 4.31 | 약 21 | 추천 출발점 |
| W4A16 AWQ per-channel | 23.60 | 4.0 | 약 22 | group 미지원 HW용 |
| + KV int8 | 18.08 (KV만) | | +1~2 tok/s | 거의 공짜 |
| + KV int4 KIVI 방식 | 18.84 (KV만) | | +3 tok/s | 긴 context에서 중요 |

(1B의 tok/s는 형식별 bit 수로 계산한 값이고, PPL은 135M에서 잰 값이다. 두 열은 서로 다른 모델이라 직접 곱해서 해석하지 않는다.)

### 10.4 Hark 같은 웨어러블이라면 (추정 예시)

예를 들어 Hark 같은 기기가 Qualcomm 계열 SoC에서 1B급 SLM을 돌린다고 **가정**하면:

1. 출발점은 **W4A16 (AWQ 또는 GPTQ, g32~g128) + KV int8**이다. 가중치 대역폭을 4배 가까이 줄이고, KV는 거의 무손실로 절반.
2. NPU 경로가 정수 활성값만 받는다면 **W8A8 + SmoothQuant** 또는 벤더가 제공하는 W4A16/W8A16 LLM 경로를 확인한다.
3. calibration은 **제품 도메인 텍스트**(음성 비서 대화, 명령어)로 구성한다. 6.6절에서 본 것처럼 대표성 없는 calibration은 GPTQ를 오히려 나쁘게 만든다.
4. 평가는 PPL + 제품 과제(명령 파싱 정답률, 요약 품질) + FP 모델과의 greedy 일치율을 **회귀 테스트**로 자동화한다(C8). 펌웨어의 golden vector 비교와 같은 구조다.
5. 배터리: DRAM 읽기 바이트가 줄면 DRAM 접근 에너지도 비례해서 준다. token당 바이트를 줄이는 것은 속도와 전력을 동시에 개선한다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| int4 per-channel RTN만 해 보고 포기 | PPL 2배 이상 | 행마다 scale 하나로는 int4 칸이 너무 거칠다 | group(32~128) + 비대칭 + AWQ/GPTQ |
| 활성값 per-tensor int8을 LLM에 그대로 | PPL 급등, 출력이 반복되거나 엉뚱함 | 채널 outlier가 scale을 잡아먹음 | per-token dynamic, SmoothQuant, 또는 A16 유지 |
| calibration과 eval에 같은 텍스트 | 양자화 결과가 너무 좋아 보임 | 과적합을 못 봄 | 반드시 분리, 제품 도메인 held-out 세트 |
| GPTQ calibration이 작거나 도메인이 다름 | 층 오차는 줄었는데 PPL이 안 줄거나 나빠짐 | H가 실제 입력을 대표하지 못함 (6.6절) | calibration 확대·다양화, damping 증가, AWQ로 대체 |
| lm_head/embedding까지 int4 | PPL이 추가로 크게 악화 | 출력 logits에 직접 영향 | 8-bit나 6-bit로 따로 둔다 |
| KV를 K, V 같은 방식으로 int4 | 긴 생성에서 품질 저하 | K는 채널 outlier가 있음 | K int8 + V int4, 또는 K per-channel |
| 행 길이가 group/super-block 배수가 아님 | 변환 도구 오류 또는 조용한 fallback | 576처럼 128·256으로 안 나눠지는 차원 | 모델 차원 선택 단계에서 정렬 고려, 로그 확인 |
| act-order GPTQ 모델을 g_idx 미지원 커널에 | 출력이 완전히 깨짐 | 열 순서와 group 매핑 불일치 | 커널 지원 확인 또는 act-order 끄고 재양자화 |
| 작은 PPL 차이를 결론으로 | 0.1 차이로 형식을 선택 | 작은 eval 세트의 잡음 | 큰 eval 세트, 여러 지표, 반복 측정 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "Why is W4A16 popular for on-device LLMs?"

**A.** decode는 token마다 가중치 전부를 한 번 읽는 GEMV라서 memory-bound다. 속도 상한이 대역폭 ÷ 가중치 바이트이므로, 가중치를 4-bit로 줄이면 FP16 대비 3.5~4배 빨라질 여지가 생긴다. 활성값은 decode에서 벡터 하나라 작으므로 FP16으로 둬도 비용이 없고, outlier 문제도 피한다. 커널은 int4를 레지스터 안에서 FP16으로 풀어 곱하므로 추가 메모리 트래픽이 없다. 정확도는 group scale과 AWQ/GPTQ로 대부분 회복된다.

> Decode is a matrix-vector product per token, so it's memory-bound: the ceiling is bandwidth divided by weight bytes. Cutting weights to four bits gives roughly a three-and-a-half to four-times higher ceiling than FP16. Activations in decode are a single vector, so keeping them in FP16 costs almost nothing and sidesteps the activation outlier problem. The kernel unpacks int4 to FP16 in registers, so there's no extra memory traffic, and group-wise scales plus AWQ or GPTQ recover most of the accuracy.

**Q.** "What are activation outliers and how does SmoothQuant fix them?"

**A.** LLM 활성값에는 몇몇 입력 채널이 다른 채널보다 수십~수천 배 큰 값을 체계적으로 가진다. per-tensor int8이면 그 채널이 scale을 정해서 나머지가 0 근처로 뭉개진다. 입력 채널 방향은 GEMM의 합산 축이라 activation per-channel scale은 정수 행렬곱에서 쓸 수 없다. SmoothQuant는 채널 j의 활성값을 s_j로 나누고 가중치 열 j에 s_j를 곱하는 등가 변환으로 어려움을 가중치 쪽으로 옮긴다. s는 앞의 RMSNorm에 흡수하므로 추론 비용이 없다. 제가 SmolLM2-135M으로 해 보니 W8A8 per-tensor PPL이 35.6에서 19.4로 돌아왔다(FP32 18.1).

> A few hidden channels in LLM activations are consistently tens to thousands of times larger than the rest. With a per-tensor int8 scale, those channels set the range and everything else collapses to a few levels. You can't use per-input-channel activation scales in an integer GEMM because that's the reduction axis. SmoothQuant divides each activation channel by a factor and multiplies the matching weight column by the same factor, which is mathematically equivalent, and folds the factor into the preceding norm. On a 135M model I measured W8A8 perplexity going from about 35.6 back to 19.4, against 18.1 for FP32.

**Q.** "GPTQ vs AWQ?"

**A.** 둘 다 calibration 데이터를 쓰는 weight-only PTQ이고 결과물은 일반 int4 텐서라 추론 커널은 같다. GPTQ는 층 출력 오차를 목표로 열을 하나씩 반올림하면서 오차를 입력 공분산의 역행렬로 남은 열에 보상한다(2차 정보). AWQ는 활성값이 큰 채널의 가중치를 스케일 업해서 상대 오차를 줄이고, 층마다 스케일 지수 하나만 탐색한다. GPTQ는 자유도가 커서 calibration이 대표성이 없으면 과적합할 수 있고(제 실험에서 down_proj가 그랬다), AWQ는 더 견고하고 빠르다. 둘을 조합하기도 한다.

> Both are weight-only post-training methods that use calibration data, and both produce ordinary int4 tensors, so the inference kernel is the same. GPTQ quantizes columns one at a time and pushes each rounding error onto the remaining columns using the inverse of the input covariance, so it minimizes layer output error with second-order information. AWQ scales up weights attached to high-magnitude activation channels and searches a single exponent per layer. GPTQ has far more freedom, so it can overfit an unrepresentative calibration set, which I actually saw on an MLP down projection. AWQ is faster and more robust, and people sometimes combine them.

**Q.** "How much memory does int4 KV save at 4K context?"

**A.** Llama-3.2-1B 기준 FP16 KV는 `2 × 16층 × 8 KV head × 64 × 2 B = 32 KiB/token`, 4K면 128 MiB다. int4에 token·head마다 FP16 scale을 붙이면 4.25 bit라 8.5 KiB/token, 34 MiB — 약 94 MiB 절약, 3.8배다. int8이면 66 MiB다. 속도로는 INT4 가중치 위에서 token당 읽기 바이트가 829 MB에서 731 MB로 줄어 상한이 약 14% 오른다. 16K context면 KV 비중이 44%라 효과가 훨씬 크다. 품질은 K가 V보다 민감해서 K는 8-bit나 per-channel로 두는 것이 안전하다.

> For Llama 3.2 1B, FP16 KV is two times 16 layers times 8 KV heads times 64 dims times 2 bytes, 32 KiB per token, so 128 MiB at 4K. Int4 with an FP16 scale per token and head is about 4.25 bits, 8.5 KiB per token or 34 MiB, so you save about 94 MiB, roughly 3.8x. For decode speed on top of int4 weights, bytes per token go from about 829 to 731 MB, around 14 percent faster, and much more at 16K context. Keys are more sensitive than values, so I'd keep keys at int8 or quantize them per channel.

**Q.** "How do you evaluate a quantized LLM?"

**A.** 세 층으로 본다. 첫째 perplexity를 FP 모델과 같은 텍스트·같은 창 길이로 비교한다(held-out, calibration과 분리). 둘째 과제 정확도 — 표준 벤치마크와 제품 고유 과제(예: 명령 파싱 정답률). 셋째 층 단위 진단 — 층별 출력 SQNR/상대오차로 어느 층이 문제인지 찾는다. 추가로 FP 모델과 greedy 출력 일치율을 회귀 테스트에 넣는다. 작은 eval 세트의 PPL 차이 0.1은 잡음일 수 있어서, 형식 선택은 여러 지표를 함께 본다.

> I look at three levels. First, perplexity on held-out text with the same tokenizer and window length as the FP baseline, never the calibration text. Second, task accuracy, both standard benchmarks and product-specific ones like command-parsing accuracy. Third, per-layer diagnostics such as output SQNR to find which layers break. I also track greedy-output agreement with the FP model as a cheap regression test. Small perplexity differences on a small eval set can be noise, so I don't pick a format on one number.

**Q.** "Why can't you use per-channel scales for activations in an int8 GEMM?"

**A.** y = ∑ⱼ xⱼ·wⱼ에서 scale이 합산 축 j와 무관해야 합 밖으로 빠져서 안쪽이 순수 정수 dot product가 된다. 가중치는 출력 채널마다, 활성값은 token마다 scale을 두면 둘 다 합산 축 밖이다. 활성값의 입력 채널마다 scale을 두면 합 안에서 항마다 다른 실수를 곱해야 해서 정수 MAC 배열의 이점이 사라진다. 그래서 SmoothQuant가 그 채널별 차이를 가중치 쪽으로 옮기는 것이다.

> In y equals the sum over j of x_j times w_j, a scale can only be factored out if it doesn't depend on j, the reduction axis. Per-output-channel weight scales and per-token activation scales both factor out, leaving a pure integer dot product. A per-input-channel activation scale would require a different real multiply inside the sum, which defeats the integer MAC array. That's exactly why SmoothQuant moves the per-channel variation into the weights instead.

---

## 13. 직접 해보기

1. (손계산) Qwen 계열 0.5B 모델(파라미터 약 5억 개라고 하자)을 int4 g128 대칭과 g32 대칭으로 저장하면 각각 몇 MiB인가? — 정답: 5×10⁸ × 4.125/8 = 257.8 MB ≈ 246 MiB, 5×10⁸ × 4.5/8 = 281.25 MB ≈ 268 MiB.
2. (손계산) 활성값 채널 최대 [4, 64], 가중치 열 최대 [0.5, 1]에서 SmoothQuant α = 0.5의 s와 smoothing 후 채널별 최대를 구하라. — 정답: s = [√(4/0.5), √(64/1)] = [2.83, 8]. 후 X 최대 = [1.41, 8], W 최대 = [1.41, 8].
3. (손계산) 6.3절의 예에서 w = [0.62, 0.40]이면 RTN과 GPTQ의 결과와 출력 오차는? — 정답: RTN [0.5, 0.5], d = [0.12, −0.10], 오차 = 0.0288 − 0.024 + 0.02 = 0.0248. GPTQ 목표 w₂ = 0.40 + 0.06 = 0.46 → 0.5, 결과가 같다(보상이 반올림 경계를 넘지 못하면 RTN과 같아진다).
4. (코드) 3.3절 코드에 lm_head(= embedding)도 int4 g64 비대칭으로 양자화하는 줄을 추가하고 PPL 변화를 재라. embedding 표 `[49152, 576]`은 행마다 group을 적용하면 된다. — 힌트: `model.lm_head.weight`와 `model.model.embed_tokens.weight`는 같은 텐서다(B8). 한 번만 바꾸면 둘 다 바뀐다.
5. (코드) 5.4절의 SmoothQuant를 q, k, v가 **같은 s를 공유**하도록 바꿔라(s = max|X|^α / max(max|W_q|, max|W_k|, max|W_v|)^(1−α)). PPL이 어떻게 달라지는가? 이것이 RMSNorm에 fold할 수 있는 형태다. — 힌트: `linears(model)`의 이름에서 층 번호로 묶는다. 결과는 직접 확인할 것.
6. (코드) 9.4절에 "최근 32 token은 FP32로 두고 나머지만 K per-channel int4" 방식을 구현해 teacher-forcing PPL을 비교하라(더 현실적인 KIVI). — 힌트: seq 축을 32개 단위 블록으로 나눠 블록마다 채널 scale을 구하고, 마지막 불완전 블록은 양자화하지 않는다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| W4A16 / W8A8 | 가중치 bit / 활성값 bit | 형식 표기. KV4, KV8을 붙이기도 함 |
| weight-only quantization | 가중치만 양자화 | 계산은 FP16, 메모리 대역폭 이득 |
| RTN | round-to-nearest | calibration 없이 가장 가까운 격자로 반올림하는 기준선 |
| per-group | 연속 g개마다 scale 하나 | int4 정확도의 핵심, bit/w = b + 16/g |
| bit/weight (bpw) | 가중치당 평균 bit | scale·zero-point 오버헤드 포함한 실제 저장 비용 |
| perplexity (PPL) | exp(평균 NLL) | 평균 몇 개 후보 사이에서 헷갈리는가, 낮을수록 좋음 |
| fake quantization | 양자화 후 즉시 복원 | 정수 커널 없이 정확도만 시뮬레이션 |
| calibration set | 양자화 파라미터 결정용 데이터 | eval과 분리, 제품 도메인을 대표해야 함 |
| activation outlier | 일부 채널의 거대 활성값 | per-tensor 활성값 양자화를 망가뜨림 |
| massive activation | 특정 token의 극단적 활성값 | 첫 token 등, attention sink와 관련 |
| SmoothQuant | 활성값 ÷ s, 가중치 × s | 양자화 어려움을 활성값에서 가중치로 이동 |
| migration strength α | SmoothQuant의 이동 비율 | 0.5면 채널별 X, W 최대가 같아짐 |
| GPTQ | Hessian 기반 순차 양자화 | 열마다 오차를 남은 열에 보상 |
| Hessian H (여기서) | 2·XᵀX | 층 출력 오차의 2차 형식, 입력 채널 상관 |
| damping | H에 λI 더하기 | H⁻¹ 수치 안정화 |
| act-order (desc_act) | 중요한 열부터 양자화 | 정확도↑, g_idx로 커널 복잡↑ |
| AWQ | activation-aware weight quantization | 활성값이 큰 채널의 가중치를 키워 보호 |
| salient weight | 중요한 가중치 | 큰 활성값과 곱해지는 가중치 |
| GGUF | llama.cpp 모델 파일 형식 | 텐서마다 양자화 타입(Q4_0, Q4_K 등) |
| Q4_0 / Q8_0 | 블록 32, FP16 scale | 4.5 / 8.5 bit/w |
| Q4_K_M | K-quant 혼합 레시피 | 대부분 Q4_K, 일부 텐서 더 높은 형식 |
| nibble | 4비트 | 1바이트에 int4 두 개 |
| dequant-on-the-fly | 레지스터에서 복원 후 곱셈 | DRAM에 FP16을 다시 쓰지 않음 |
| KV-cache quantization | K, V를 int8/int4로 저장 | 긴 context의 메모리·대역폭 절감 |
| KIVI | K per-channel, V per-token | KV-cache 2~4bit 양자화 방법 |

---

## 15. 요약 & 체크리스트

LLM decode는 memory-bound라서 가중치 바이트가 곧 속도다. 그래서 **weight-only(W4A16)**가 온디바이스 LLM의 기본이고, int8 weight-only는 거의 무손실, int4는 group scale·비대칭·calibration 기반 방법이 필요하다. 활성값까지 int8로 하는 **W8A8**은 몇몇 채널의 거대 활성값(outlier) 때문에 per-tensor scale이 깨지며, 합산 축 문제로 per-channel 활성값 scale을 쓸 수 없다. **SmoothQuant**는 이 어려움을 등가 변환으로 가중치에 옮긴다(SmolLM2에서 W8A8 PPL 35.6 → 19.4). **GPTQ**는 H = 2XᵀX를 이용해 반올림 오차를 남은 열에 보상하지만 대표성 없는 calibration에는 과적합할 수 있고, **AWQ**는 층마다 스케일 지수 하나만 골라 견고하게 중요 채널을 보호한다(int4 g64 PPL 22.66 → 20.58). context가 길어지면 **KV-cache**가 가중치만큼 커지므로 int8(거의 무손실) 또는 K를 더 조심한 int4로 줄인다. 모든 결과는 held-out PPL, 과제 정확도, 층별 오차로 검증한다.

- [ ] W4A16, W8A8, W4A8의 뜻과 decode/prefill에서 각각의 이득을 설명할 수 있다
- [ ] group 크기와 대칭/비대칭에 따른 bit/weight를 손으로 계산할 수 있다 (4.125, 4.5, 4.3125)
- [ ] perplexity를 정답 확률로부터 손으로 계산하고, 작은 eval 세트의 잡음을 고려해 해석할 수 있다
- [ ] 정수 GEMM에서 활성값 per-input-channel scale이 안 되는 이유를 수식으로 설명할 수 있다
- [ ] forward hook으로 활성값 outlier 채널을 찾고 per-tensor int8의 피해를 수치로 보일 수 있다
- [ ] SmoothQuant의 s 공식과 α = 0.5에서 X, W 최대가 같아지는 이유를 설명하고, s를 어디에 fold하는지 말할 수 있다
- [ ] GPTQ의 2-weight 예제를 손으로 풀고, 왜 calibration 대표성이 중요한지 설명할 수 있다
- [ ] AWQ가 상대 오차를 1/s로 줄이는 원리와 GPTQ 대비 장단점을 말할 수 있다
- [ ] Q4_0/Q8_0 블록 구조와 nibble 배치를 그리고, C로 dequant-on-the-fly dot product를 짤 수 있다
- [ ] KV-cache 크기를 context별로 계산하고, K와 V를 다르게 양자화하는 이유를 설명할 수 있다

## 참고 자료

- Xiao et al., "SmoothQuant: Accurate and Efficient Post-Training Quantization for Large Language Models", 2022 — [arXiv:2211.10438](https://arxiv.org/abs/2211.10438)
- Frantar et al., "GPTQ: Accurate Post-Training Quantization for Generative Pre-trained Transformers", 2022 — [arXiv:2210.17323](https://arxiv.org/abs/2210.17323)
- Lin et al., "AWQ: Activation-aware Weight Quantization for LLM Compression and Acceleration", 2023 — [arXiv:2306.00978](https://arxiv.org/abs/2306.00978)
- Dettmers et al., "LLM.int8(): 8-bit Matrix Multiplication for Transformers at Scale", 2022 — [arXiv:2208.07339](https://arxiv.org/abs/2208.07339)
- Liu et al., "KIVI: A Tuning-Free Asymmetric 2bit Quantization for KV Cache", 2024 — [arXiv:2402.02750](https://arxiv.org/abs/2402.02750)
- Sun et al., "Massive Activations in Large Language Models", 2024 — [arXiv:2402.17762](https://arxiv.org/abs/2402.17762)
- Xiao et al., "Efficient Streaming Language Models with Attention Sinks" (StreamingLLM), 2023 — [arXiv:2309.17453](https://arxiv.org/abs/2309.17453)
- llama.cpp (GGUF, 양자화 타입, KV-cache 옵션) — [github.com/ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp)
- MIT 6.5940 TinyML and Efficient Deep Learning (Song Han) — LLM 양자화 강의 — [efficientml.ai](https://efficientml.ai)
- SmolLM2-135M-Instruct 모델 카드 — [huggingface.co/HuggingFaceTB/SmolLM2-135M-Instruct](https://huggingface.co/HuggingFaceTB/SmolLM2-135M-Instruct)
- transformers Attention Interface 문서 — [huggingface.co/docs/transformers](https://huggingface.co/docs/transformers)
- 이 노트와 연결: B4(KV-cache 공식), B8(메모리·tokens/s), C1(양자화 수식), C2(PTQ/QAT), C8(검증), D5(LLM 추론 성능), F3(llama.cpp)
