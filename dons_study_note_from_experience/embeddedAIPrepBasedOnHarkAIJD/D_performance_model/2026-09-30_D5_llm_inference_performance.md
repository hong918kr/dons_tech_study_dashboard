# D5. LLM 추론 성능 — prefill vs decode, KV-cache, 대역폭, TTFT/TPOT를 숫자로

> **이 노트를 다 읽으면**: HF config 하나로 TTFT·TPOT·KV-cache 크기를 계산기로 예측할 수 있다 · 그 예측을 실제 측정과 비교해서 "어디서 시간이 새는지" 찾을 수 있다 · KV-cache가 context에 따라 decode를 얼마나 느리게 하고 언제 가중치보다 커지는지 말할 수 있다 · 웨어러블 음성 비서 한 턴의 지연 예산을 세우고 어떤 크기의 모델이 들어가는지 판정할 수 있다
> **JD 연결**: "(우대) Understand the performance characteristics of edge AI models, including CNN, RNN, transformers, **KV-cache behavior, and their memory bandwidth requirements**" — study_prep_list **D5**: prefill(compute-bound) vs decode(memory-bound) · KV-cache 크기 = 2 × layers × kv_heads × head_dim × seq × bytes · tokens/s 상한 ≈ 대역폭 ÷ (모델 바이트 + KV 바이트) · TTFT, TPOT, context 길이 영향 · GQA, paged/sliding window KV
> **Don 기준 난이도**: 대역폭 계산·링버퍼·DMA·측정 기반 디버깅은 이미 강한 부분 / FLOP 세기, prefill·decode 두 단계의 병목 차이, KV-cache 운영 기법(paged, sliding window, prefix cache), speculative decoding은 새로 배울 부분
> **선행 노트**: B4 (attention, GQA, KV-cache 공식), B8 (SLM 구조, 메모리 계산, TTFT/TPOT 첫 측정), C3 (가중치·KV 양자화), D3 (roofline, arithmetic intensity)

---

## 0. 큰 그림 — 이게 왜 필요한가

B8에서 우리는 두 가지를 이미 봤다. 하나는 "decode tokens/s 상한 ≈ 대역폭 ÷ token당 읽는 바이트"라는 한 줄이고, 다른 하나는 SmolLM2-135M을 이 Mac에서 돌렸을 때 TPOT가 약 17.6 ms였다는 실측이다. D5는 그 두 조각을 **하나의 성능 모델**로 묶는다.

펌웨어 비유로 말하면, B8은 "NVMe read 명령 하나의 지연을 잰 것"이고 D5는 "호스트 워크로드 전체에 대해 IOPS·지연·큐 깊이를 예측하는 성능 모델을 만들고, 실측으로 검증하고, 모델과 실측의 차이에서 병목을 찾는 것"이다. SSD 펌웨어 성능 튜닝에서 늘 하던 일 — **이론 상한을 먼저 계산하고, 실측이 그 몇 %인지 보고, 차이를 설명한다** — 을 LLM에 그대로 적용한다.

LLM 추론 한 번은 두 단계로 나뉜다.

1. **prefill**: 프롬프트 token S개를 한꺼번에 넣어 KV-cache를 채우고 첫 token을 만든다. 가중치를 한 번 읽어서 S번 재사용하는 행렬 × 행렬(GEMM)이다 → 보통 **compute-bound**.
2. **decode**: 그 뒤로 token을 **하나씩** 만든다. 매 token마다 가중치 전체와 지금까지의 KV-cache 전체를 다시 읽는 행렬 × 벡터(GEMV)다 → **memory-bound**.

두 단계의 병목이 다르기 때문에 최적화 손잡이도 다르다. prefill은 연산기(NPU TOPS)와 프롬프트 길이, decode는 DRAM 대역폭과 "token당 읽는 바이트"가 지배한다. 이 노트 전체가 이 한 문장의 정량화다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 295">
<text x="60.0" y="28.0" font-size="12" text-anchor="start">Llama-3.2-1B int4, 웨어러블 가정(17 GB/s, 2 TFLOP/s), prompt 512 token — 계산기 값</text><rect x="60.0" y="50.0" width="294.1" height="38.0" fill="#4a7bd0" fill-opacity="0.7"/><text x="207.0" y="66.0" font-size="12" text-anchor="middle">prefill: 512 token 한꺼번에</text><text x="207.0" y="81.0" font-size="12" text-anchor="middle">GEMM · compute-bound</text><rect x="355.1" y="62.0" width="22.4" height="26.0" fill="#e08a3c" fill-opacity="0.8"/><rect x="379.4" y="62.0" width="22.4" height="26.0" fill="#e08a3c" fill-opacity="0.8"/><rect x="403.8" y="62.0" width="22.4" height="26.0" fill="#e08a3c" fill-opacity="0.8"/><rect x="428.1" y="62.0" width="22.4" height="26.0" fill="#e08a3c" fill-opacity="0.8"/><rect x="452.5" y="62.0" width="22.4" height="26.0" fill="#e08a3c" fill-opacity="0.8"/><rect x="476.9" y="62.0" width="22.4" height="26.0" fill="#e08a3c" fill-opacity="0.8"/><rect x="501.2" y="62.0" width="22.4" height="26.0" fill="#e08a3c" fill-opacity="0.8"/><rect x="525.6" y="62.0" width="22.4" height="26.0" fill="#e08a3c" fill-opacity="0.8"/><rect x="549.9" y="62.0" width="22.4" height="26.0" fill="#e08a3c" fill-opacity="0.8"/><rect x="574.3" y="62.0" width="22.4" height="26.0" fill="#e08a3c" fill-opacity="0.8"/><rect x="598.7" y="62.0" width="22.4" height="26.0" fill="#e08a3c" fill-opacity="0.8"/><text x="497.0" y="50.0" font-size="12" text-anchor="middle">decode: 1 token씩 · GEMV · memory-bound</text><text x="54.0" y="122.0" font-size="12" text-anchor="end">출력</text><circle cx="354.1" cy="118.0" r="4" fill="#3f9a6b"/><circle cx="378.4" cy="118.0" r="4" fill="#3f9a6b"/><circle cx="402.8" cy="118.0" r="4" fill="#3f9a6b"/><circle cx="427.1" cy="118.0" r="4" fill="#3f9a6b"/><circle cx="451.5" cy="118.0" r="4" fill="#3f9a6b"/><circle cx="475.9" cy="118.0" r="4" fill="#3f9a6b"/><circle cx="500.2" cy="118.0" r="4" fill="#3f9a6b"/><circle cx="524.6" cy="118.0" r="4" fill="#3f9a6b"/><circle cx="548.9" cy="118.0" r="4" fill="#3f9a6b"/><circle cx="573.3" cy="118.0" r="4" fill="#3f9a6b"/><circle cx="597.7" cy="118.0" r="4" fill="#3f9a6b"/><circle cx="622.0" cy="118.0" r="4" fill="#3f9a6b"/><text x="358.1" y="140.0" font-size="12" text-anchor="start">첫 token</text><line x1="60.0" y1="170.0" x2="640.0" y2="170.0" stroke="currentColor" stroke-width="1"/><line x1="60.0" y1="170.0" x2="60.0" y2="175.0" stroke="currentColor" stroke-width="1"/><text x="60.0" y="189.0" font-size="12" text-anchor="middle">0</text><line x1="176.0" y1="170.0" x2="176.0" y2="175.0" stroke="currentColor" stroke-width="1"/><text x="176.0" y="189.0" font-size="12" text-anchor="middle">200</text><line x1="292.0" y1="170.0" x2="292.0" y2="175.0" stroke="currentColor" stroke-width="1"/><text x="292.0" y="189.0" font-size="12" text-anchor="middle">400</text><line x1="408.0" y1="170.0" x2="408.0" y2="175.0" stroke="currentColor" stroke-width="1"/><text x="408.0" y="189.0" font-size="12" text-anchor="middle">600</text><line x1="524.0" y1="170.0" x2="524.0" y2="175.0" stroke="currentColor" stroke-width="1"/><text x="524.0" y="189.0" font-size="12" text-anchor="middle">800</text><line x1="640.0" y1="170.0" x2="640.0" y2="175.0" stroke="currentColor" stroke-width="1"/><text x="640.0" y="189.0" font-size="12" text-anchor="middle">1000</text><text x="640.0" y="205.0" font-size="12" text-anchor="end">ms</text><line x1="60.0" y1="225.0" x2="354.1" y2="225.0" stroke="currentColor" stroke-width="1.5"/><line x1="60.0" y1="220.0" x2="60.0" y2="230.0" stroke="currentColor" stroke-width="1"/><line x1="354.1" y1="220.0" x2="354.1" y2="230.0" stroke="currentColor" stroke-width="1"/><text x="207.0" y="242.0" font-size="12" text-anchor="middle">TTFT ≈ 507 ms (프롬프트 길이에 비례)</text><line x1="354.1" y1="225.0" x2="378.4" y2="225.0" stroke="#d0564a" stroke-width="2.5"/><text x="384.4" y="229.0" font-size="12" text-anchor="start">TPOT(=ITL) ≈ 42 ms → 23.8 tok/s</text><line x1="60.0" y1="262.0" x2="622.0" y2="262.0" stroke="currentColor" stroke-width="1.5"/><line x1="60.0" y1="257.0" x2="60.0" y2="267.0" stroke="currentColor" stroke-width="1"/><line x1="622.0" y1="257.0" x2="622.0" y2="267.0" stroke="currentColor" stroke-width="1"/><text x="341.0" y="279.0" font-size="12" text-anchor="middle">end-to-end latency = TTFT + (M − 1) × TPOT  (여기서는 M = 12, 969 ms)</text>
</svg>
```

그림 1 — Llama-3.2-1B int4를 웨어러블급 가정 하드웨어(17 GB/s, 2 TFLOP/s)에서 돌릴 때의 시간표. 숫자는 3절 계산기 값이다. 파란 블록(prefill)이 끝나야 첫 token(초록 점)이 나오고, 그 뒤로는 주황 블록 하나마다 token이 하나씩 나온다. TTFT는 프롬프트 길이에 비례하고, TPOT는 거의 일정하다.

기기 안에서 이 계산이 어디에 걸리는지 블록도로 보면 이렇다.

```
  마이크 → [VAD/ASR (DSP)] → text → tokenizer → token ids
                                                   │
                     ┌─────────────────────────────▼──────────────────────────┐
                     │ LLM runtime (CPU / GPU / NPU)                          │
                     │   prefill: S token × 가중치  ──►  KV-cache에 S칸 쓰기    │
                     │   decode : 1 token × 가중치  ──►  KV-cache 1칸 추가      │
                     └───────┬──────────────────────────────┬─────────────────┘
                             │ 매 step 가중치 전체 읽기        │ 매 step KV 전체 읽기
                     ┌───────▼──────────────────────────────▼─────────────────┐
                     │ LPDDR (가중치 수백 MB + KV-cache 수십~수백 MB)           │  ← 병목
                     └────────────────────────────────────────────────────────┘
                             │ token → detokenize → TTS → 스피커
```

이 노트의 순서:

- 1절: 지표(TTFT, TPOT, ITL, E2E, TTFA)를 정확히 정의한다.
- 2절: config에서 계산에 필요한 숫자를 뽑는다.
- 3절: 해석 모델을 Python 계산기로 만들고, 모델 4종 × 형식 3종 × 플랫폼 3종에 적용한다.
- 4절: 이 Mac에서 측정해서 계산기를 **검증**하고, 어긋나는 곳의 원인을 profiler로 찾는다.
- 5절: KV-cache를 깊게 본다 — 성장, 교차점, 단편화와 PagedAttention, sliding window, KV 양자화, prefix caching.
- 6절: batching과 speculative decoding이 roofline에서 어떻게 보이는지.
- 7절: 웨어러블 음성 비서 한 턴의 지연 예산.

---

## 1. 지표 — 무엇을 재는가

### 1.1 정의

| 지표 | 정의 | 사용자가 느끼는 것 | 주로 무엇이 정하나 |
|---|---|---|---|
| **TTFT** (Time To First Token) | 요청 도착 → 첫 token이 나올 때까지 | "반응이 빠르다/느리다" | prefill: 프롬프트 길이 × 모델 FLOP ÷ 연산 성능 |
| **TPOT** (Time Per Output Token) | 두 번째 token부터 token 하나당 평균 시간 | "말하는 속도" | decode: (가중치 + KV) 바이트 ÷ 대역폭 |
| **ITL** (Inter-Token Latency) | 연속한 두 token 사이의 시간 하나하나 | 끊김(jitter) | TPOT의 분포. 평균이 TPOT, 꼬리가 끊김 |
| **E2E latency** | 요청 → 마지막 token | "다 끝나기까지" | TTFT + (M − 1) × TPOT |
| **decode tokens/s** | 1 ÷ TPOT | 생성 속도 | 대역폭 |
| **prefill tokens/s** | S ÷ TTFT | 긴 문서 읽는 속도 | 연산 성능 |
| **TTFA** (time-to-first-audio) | 사용자가 말을 끝낸 순간 → 첫 소리 | 음성 비서의 "반응 속도" | VAD + ASR + TTFT + 첫 구절 decode + TTS 첫 청크 |

말로 하면: TTFT는 "첫 마디가 나오기까지", TPOT는 "그다음 한 단어 한 단어의 간격"이다. 텍스트 채팅에서는 TTFT가 반응성, TPOT가 읽기 편한 속도를 정한다. 음성에서는 사람이 듣는 속도보다 빨리 만들기만 하면 TPOT는 더 빨라도 체감이 없고, 대신 **TTFA**가 전부다.

```
E2E latency = TTFT + (M − 1) × TPOT
```

말로 하면: 첫 token은 prefill이 만들고, 나머지 M − 1개는 decode가 하나씩 만든다.

### 1.2 손으로 계산

TTFT = 500 ms, TPOT = 40 ms, 답 M = 50 token이면:

```
E2E = 500 + 49 × 40 = 2,460 ms
decode tokens/s = 1 / 0.040 = 25 tok/s
전체 평균 tokens/s = 50 / 2.46 = 20.3 tok/s   ← 벤치마크 숫자가 어느 것인지 꼭 확인
```

"20 tok/s"라는 숫자 하나만 보고는 decode 속도인지, prefill을 포함한 평균인지 알 수 없다. llama.cpp의 `llama-bench`는 prefill(`pp512`)과 decode(`tg128`)를 따로 보고한다. 벤치마크 숫자를 받으면 **항상 어떤 단계의 몇 token 기준인지** 먼저 묻는다(M2).

### 1.3 음성 비서의 체감 지표 — TTFA

예를 들어 Hark 같은 웨어러블 음성 비서라면(추정 예시) 사용자는 token을 보지 않는다. 소리를 듣는다. 그래서:

- 사용자가 말을 멈춘 뒤 **VAD가 "말이 끝났다"고 판정하는 데** 수백 ms가 걸린다(endpointing).
- ASR이 마지막 단어를 확정한다.
- LLM이 prefill을 끝내고 **첫 구절**(TTS가 자연스럽게 읽기 시작할 만큼, 예: 10~15 token)을 만든다.
- TTS가 첫 오디오 청크를 만든다.

```
TTFA ≈ endpoint + ASR finalize + TTFT + K × TPOT + TTS 첫 청크      (K = 첫 구절 token 수)
```

말로 하면: 음성에서는 TTFT만이 아니라 **첫 구절을 만드는 decode 시간(K × TPOT)** 도 첫 소리 지연에 들어간다. 7절에서 이 식으로 모델 크기를 판정한다.

### 1.4 함정

- **평균만 보기**: TPOT 평균이 40 ms라도 가끔 200 ms짜리 멈춤(GC, 열 스로틀링, 다른 작업의 DRAM 경쟁)이 있으면 음성이 끊긴다. ITL의 p99를 봐야 한다(D6).
- **warm-up 무시**: 첫 호출은 가중치 페이지 폴트, 커널 컴파일, 캐시 할당 때문에 몇 배 느리다. 측정 전에 반드시 warm-up한다.
- **tokenizer·detokenizer 시간 누락**: 작은 모델에서는 이것도 ms 단위다.

---

## 2. config에서 숫자 뽑기 — 가중치, KV/token, 교차점

### 2.1 필요한 숫자는 다섯 개뿐

성능 모델에 필요한 모델 정보는 config에서 바로 나온다(유도는 B4 5.3절, B8 3·4절).

| 기호 | 뜻 | 어디에 쓰나 |
|---|---|---|
| P | 전체 파라미터 수 | 가중치 메모리 = P × bit / 8 |
| Pm | token 하나를 만들 때 곱하는 가중치 수 | decode 읽기 바이트, FLOP = 2 × Pm |
| L, nkv, hd | 층 수, KV head 수, head 차원 | KV/token = 2 × L × nkv × hd × 바이트 |
| nq | query head 수 | attention FLOP |
| V × d | embedding 표 크기 | prefill에서는 lm_head를 마지막 token에만 쓴다 |

Pm이 P와 다른 이유: embedding 표는 token id로 **한 행만 읽는** lookup이라 곱셈이 아니다. 대신 출력층(lm_head, V × d)은 매 token 전부 곱한다. tie_word_embeddings(embedding과 lm_head가 같은 행렬)인 모델은 Pm = P, 아닌 모델은 Pm = P − V × d다. 이 노트의 네 모델은 모두 tied다.

### 2.2 교차점 — KV가 가중치만큼 커지는 context

decode 한 step이 읽는 바이트는 `가중치 + KV/token × context`다. 둘이 같아지는 context를 **교차점**이라고 부르자.

```
교차 context = 가중치 바이트 ÷ KV 바이트/token
```

말로 하면: 이 길이를 넘으면 decode 시간의 절반 이상이 "과거를 다시 읽는 데" 쓰인다.

손으로 계산 — Llama-3.2-1B, int4(g32 = 4.5 bit) 가중치, fp16 KV:

```
가중치 = 1,235,814,400 × 4.5 / 8 = 695,145,600 B
KV/token = 2 × 16 × 8 × 64 × 2 B = 32,768 B
교차 = 695,145,600 / 32,768 ≈ 21,214 token ≈ 20.7K (K = 1024)
```

### 2.3 코드로 확인: 모델 4종의 KV/token, GQA 효과, 교차점

무엇을 확인하나: 3절 계산기의 `shape()`로 config를 읽어 KV/token, GQA가 없었을 때(MHA)의 값, 8K context의 KV, 그리고 형식별 교차점을 계산한다. `shape()`는 3.3절 `llm_perf.py`에 있다(먼저 그 파일을 저장해 둔다).

```python
from llm_perf import shape
models = {"SmolLM2-135M": "HuggingFaceTB/SmolLM2-135M-Instruct", "Qwen2.5-0.5B": "Qwen/Qwen2.5-0.5B",
          "Llama-3.2-1B": "unsloth/Llama-3.2-1B", "Llama-3.2-3B": "unsloth/Llama-3.2-3B"}
print(f"{'model':13s}{'params':>8s}{'q:kv':>7s}{'KV/tok':>9s}{'MHA면':>8s}{'8K KV':>9s}   KV = 가중치가 되는 context")
for name, mid in models.items():
    s = shape(mid)
    kv = 2 * s["L"] * s["nkv"] * s["hd"] * 2                    # fp16 KV, 바이트/token
    mha = kv * s["nq"] // s["nkv"]                              # GQA가 없었다면
    cross = {q: s["P"] * b / 8 / kv for q, b in (("fp16", 16), ("int8", 8), ("int4", 4.5))}
    print(f"{name:13s}{s['P']/1e6:7.0f}M{s['nq']:>4d}:{s['nkv']:<2d}{kv/1024:7.1f}Ki{mha/1024:7.0f}Ki"
          f"{kv*8192/2**20:6.0f}MiB   " + "  ".join(f"W{q} {c/1024:5.1f}K" for q, c in cross.items()))
```

```text
model          params   q:kv   KV/tok    MHA면    8K KV   KV = 가중치가 되는 context
SmolLM2-135M     135M   9:3    22.5Ki     68Ki   180MiB   Wfp16  11.4K  Wint8   5.7K  Wint4   3.2K
Qwen2.5-0.5B     494M  14:2    12.0Ki     84Ki    96MiB   Wfp16  78.5K  Wint8  39.3K  Wint4  22.1K
Llama-3.2-1B    1236M  32:8    32.0Ki    128Ki   256MiB   Wfp16  73.7K  Wint8  36.8K  Wint4  20.7K
Llama-3.2-3B    3213M  24:8   112.0Ki    336Ki   896MiB   Wfp16  54.7K  Wint8  27.4K  Wint4  15.4K
```

출력에서 볼 것:

- params 열이 B8 3.3절의 실측 파라미터 수(134,515,008 / 494,032,768 / 1,235,814,400 / 3,212,749,824)와 정확히 같다. 계산기의 입력이 맞다는 확인이다.
- **GQA 효과**: Llama-3.2-1B는 query 32 : KV 8이라 token당 32 KiB, MHA였다면 128 KiB. Qwen2.5-0.5B는 14 : 2로 7배 절약해서 12 KiB/token, 네 모델 중 가장 작다(B4 5.4절).
- **Llama-3.2-1B의 8K context KV = 256 MiB** (면접 단골 질문의 답).
- **교차점**: 가중치를 int4로 줄이면 교차점이 당겨진다. SmolLM2-135M int4는 3.2K token만 넘어도 KV가 가중치보다 크다. 층이 30개로 많고 가중치는 작기 때문이다. 작은 모델일수록 "KV가 주인공"이 되는 context가 짧다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 330">
<line x1="80.0" y1="280.0" x2="500.0" y2="280.0" stroke="currentColor" stroke-width="1"/><line x1="80.0" y1="280.0" x2="80.0" y2="30.0" stroke="currentColor" stroke-width="1"/><line x1="80.0" y1="280.0" x2="80.0" y2="285.0" stroke="currentColor" stroke-width="1"/><text x="80.0" y="299.0" font-size="12" text-anchor="middle">0K</text><line x1="185.0" y1="280.0" x2="185.0" y2="285.0" stroke="currentColor" stroke-width="1"/><text x="185.0" y="299.0" font-size="12" text-anchor="middle">8K</text><line x1="290.0" y1="280.0" x2="290.0" y2="285.0" stroke="currentColor" stroke-width="1"/><text x="290.0" y="299.0" font-size="12" text-anchor="middle">16K</text><line x1="395.0" y1="280.0" x2="395.0" y2="285.0" stroke="currentColor" stroke-width="1"/><text x="395.0" y="299.0" font-size="12" text-anchor="middle">24K</text><line x1="500.0" y1="280.0" x2="500.0" y2="285.0" stroke="currentColor" stroke-width="1"/><text x="500.0" y="299.0" font-size="12" text-anchor="middle">32K</text><line x1="75.0" y1="280.0" x2="80.0" y2="280.0" stroke="currentColor" stroke-width="1"/><text x="72.0" y="284.0" font-size="12" text-anchor="end">10</text><line x1="75.0" y1="235.8" x2="80.0" y2="235.8" stroke="currentColor" stroke-width="1"/><text x="72.0" y="239.8" font-size="12" text-anchor="end">30</text><line x1="75.0" y1="187.4" x2="80.0" y2="187.4" stroke="currentColor" stroke-width="1"/><text x="72.0" y="191.4" font-size="12" text-anchor="end">100</text><line x1="75.0" y1="143.2" x2="80.0" y2="143.2" stroke="currentColor" stroke-width="1"/><text x="72.0" y="147.2" font-size="12" text-anchor="end">300</text><line x1="75.0" y1="94.7" x2="80.0" y2="94.7" stroke="currentColor" stroke-width="1"/><text x="72.0" y="98.7" font-size="12" text-anchor="end">1000</text><line x1="75.0" y1="50.5" x2="80.0" y2="50.5" stroke="currentColor" stroke-width="1"/><text x="72.0" y="54.5" font-size="12" text-anchor="end">3000</text><text x="290.0" y="318.0" font-size="12" text-anchor="middle">context 길이 (token)</text><text x="20.0" y="155.0" font-size="12" text-anchor="middle" transform="rotate(-90 20 155)">MiB (log)</text><polyline points="85.8,279.9 89.1,262.0 92.4,249.6 95.7,240.2 99.0,232.6 102.3,226.1 105.5,220.6 108.8,215.7 112.1,211.4 115.4,207.5 118.7,203.9 121.9,200.6 125.2,197.6 128.5,194.8 131.8,192.2 135.1,189.7 138.3,187.4 141.6,185.2 144.9,183.1 148.2,181.1 151.5,179.2 154.8,177.4 158.0,175.7 161.3,174.0 164.6,172.4 167.9,170.9 171.2,169.4 174.4,168.0 177.7,166.6 181.0,165.3 184.3,164.0 187.6,162.8 190.8,161.5 194.1,160.4 197.4,159.2 200.7,158.1 204.0,157.0 207.3,156.0 210.5,155.0 213.8,154.0 217.1,153.0 220.4,152.0 223.7,151.1 226.9,150.2 230.2,149.3 233.5,148.5 236.8,147.6 240.1,146.8 243.3,145.9 246.6,145.1 249.9,144.4 253.2,143.6 256.5,142.8 259.8,142.1 263.0,141.4 266.3,140.7 269.6,140.0 272.9,139.3 276.2,138.6 279.4,137.9 282.7,137.3 286.0,136.6 289.3,136.0 292.6,135.4 295.8,134.7 299.1,134.1 302.4,133.5 305.7,132.9 309.0,132.4 312.3,131.8 315.5,131.2 318.8,130.7 322.1,130.1 325.4,129.6 328.7,129.0 331.9,128.5 335.2,128.0 338.5,127.5 341.8,127.0 345.1,126.5 348.3,126.0 351.6,125.5 354.9,125.0 358.2,124.5 361.5,124.1 364.8,123.6 368.0,123.1 371.3,122.7 374.6,122.2 377.9,121.8 381.2,121.3 384.4,120.9 387.7,120.5 391.0,120.0 394.3,119.6 397.6,119.2 400.8,118.8 404.1,118.4 407.4,118.0 410.7,117.6 414.0,117.2 417.3,116.8 420.5,116.4 423.8,116.0 427.1,115.6 430.4,115.3 433.7,114.9 436.9,114.5 440.2,114.1 443.5,113.8 446.8,113.4 450.1,113.1 453.3,112.7 456.6,112.3 459.9,112.0 463.2,111.6 466.5,111.3 469.8,111.0 473.0,110.6 476.3,110.3 479.6,110.0 482.9,109.6 486.2,109.3 489.4,109.0 492.7,108.7 496.0,108.3 499.3,108.0" fill="none" stroke="#3f9a6b" stroke-width="2"/><line x1="80.0" y1="200.5" x2="500.0" y2="200.5" stroke="#3f9a6b" stroke-width="1.5" stroke-dasharray="6 4"/><circle cx="122.1" cy="200.5" r="5" fill="#3f9a6b"/><text x="508.0" y="112.0" font-size="12" text-anchor="start">SmolLM2-135M KV</text><polyline points="90.9,280.0 94.2,269.4 97.5,261.1 100.8,254.2 104.1,248.3 107.4,243.1 110.6,238.6 113.9,234.5 117.2,230.8 120.5,227.4 123.8,224.2 127.0,221.3 130.3,218.6 133.6,216.1 136.9,213.7 140.2,211.4 143.4,209.3 146.7,207.3 150.0,205.3 153.3,203.5 156.6,201.7 159.9,200.0 163.1,198.4 166.4,196.9 169.7,195.4 173.0,193.9 176.3,192.5 179.5,191.2 182.8,189.9 186.1,188.6 189.4,187.4 192.7,186.2 195.9,185.0 199.2,183.9 202.5,182.8 205.8,181.7 209.1,180.7 212.4,179.7 215.6,178.7 218.9,177.8 222.2,176.8 225.5,175.9 228.8,175.0 232.0,174.1 235.3,173.3 238.6,172.4 241.9,171.6 245.2,170.8 248.4,170.0 251.7,169.2 255.0,168.5 258.3,167.7 261.6,167.0 264.9,166.3 268.1,165.6 271.4,164.9 274.7,164.2 278.0,163.5 281.3,162.8 284.5,162.2 287.8,161.6 291.1,160.9 294.4,160.3 297.7,159.7 300.9,159.1 304.2,158.5 307.5,157.9 310.8,157.3 314.1,156.8 317.4,156.2 320.6,155.7 323.9,155.1 327.2,154.6 330.5,154.0 333.8,153.5 337.0,153.0 340.3,152.5 343.6,152.0 346.9,151.5 350.2,151.0 353.4,150.5 356.7,150.0 360.0,149.6 363.3,149.1 366.6,148.6 369.9,148.2 373.1,147.7 376.4,147.3 379.7,146.8 383.0,146.4 386.3,146.0 389.5,145.5 392.8,145.1 396.1,144.7 399.4,144.3 402.7,143.9 405.9,143.4 409.2,143.0 412.5,142.6 415.8,142.2 419.1,141.9 422.4,141.5 425.6,141.1 428.9,140.7 432.2,140.3 435.5,140.0 438.8,139.6 442.0,139.2 445.3,138.9 448.6,138.5 451.9,138.1 455.2,137.8 458.4,137.4 461.7,137.1 465.0,136.7 468.3,136.4 471.6,136.1 474.9,135.7 478.1,135.4 481.4,135.1 484.7,134.7 488.0,134.4 491.3,134.1 494.5,133.8 497.8,133.5" fill="none" stroke="#e08a3c" stroke-width="2"/><line x1="80.0" y1="148.2" x2="500.0" y2="148.2" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="6 4"/><circle cx="369.9" cy="148.2" r="5" fill="#e08a3c"/><text x="508.0" y="137.2" font-size="12" text-anchor="start">Qwen2.5-0.5B KV</text><polyline points="84.1,279.9 87.4,256.3 90.7,241.5 94.0,230.7 97.2,222.2 100.5,215.2 103.8,209.3 107.1,204.1 110.4,199.5 113.6,195.3 116.9,191.6 120.2,188.2 123.5,185.0 126.8,182.1 130.1,179.4 133.3,176.8 136.6,174.4 139.9,172.1 143.2,170.0 146.5,168.0 149.7,166.0 153.0,164.2 156.3,162.4 159.6,160.7 162.9,159.1 166.1,157.5 169.4,156.0 172.7,154.6 176.0,153.2 179.3,151.8 182.6,150.5 185.8,149.2 189.1,148.0 192.4,146.8 195.7,145.7 199.0,144.5 202.2,143.4 205.5,142.4 208.8,141.3 212.1,140.3 215.4,139.3 218.6,138.4 221.9,137.4 225.2,136.5 228.5,135.6 231.8,134.7 235.1,133.9 238.3,133.0 241.6,132.2 244.9,131.4 248.2,130.6 251.5,129.8 254.7,129.1 258.0,128.3 261.3,127.6 264.6,126.9 267.9,126.2 271.1,125.5 274.4,124.8 277.7,124.1 281.0,123.4 284.3,122.8 287.6,122.1 290.8,121.5 294.1,120.9 297.4,120.3 300.7,119.7 304.0,119.1 307.2,118.5 310.5,117.9 313.8,117.4 317.1,116.8 320.4,116.2 323.6,115.7 326.9,115.2 330.2,114.6 333.5,114.1 336.8,113.6 340.1,113.1 343.3,112.6 346.6,112.1 349.9,111.6 353.2,111.1 356.5,110.6 359.7,110.1 363.0,109.7 366.3,109.2 369.6,108.7 372.9,108.3 376.1,107.8 379.4,107.4 382.7,107.0 386.0,106.5 389.3,106.1 392.6,105.7 395.8,105.3 399.1,104.8 402.4,104.4 405.7,104.0 409.0,103.6 412.2,103.2 415.5,102.8 418.8,102.4 422.1,102.0 425.4,101.7 428.6,101.3 431.9,100.9 435.2,100.5 438.5,100.2 441.8,99.8 445.1,99.4 448.3,99.1 451.6,98.7 454.9,98.4 458.2,98.0 461.5,97.7 464.7,97.3 468.0,97.0 471.3,96.6 474.6,96.3 477.9,96.0 481.1,95.6 484.4,95.3 487.7,95.0 491.0,94.7 494.3,94.3 497.6,94.0" fill="none" stroke="#4a7bd0" stroke-width="2"/><line x1="80.0" y1="111.3" x2="500.0" y2="111.3" stroke="#4a7bd0" stroke-width="1.5" stroke-dasharray="6 4"/><circle cx="351.9" cy="111.3" r="5" fill="#4a7bd0"/><text x="508.0" y="97.8" font-size="12" text-anchor="start">Llama-3.2-1B KV</text><polyline points="81.2,279.7 84.5,226.2 87.7,204.0 91.0,189.8 94.3,179.4 97.6,171.0 100.9,164.2 104.1,158.3 107.4,153.2 110.7,148.6 114.0,144.5 117.3,140.8 120.6,137.4 123.8,134.3 127.1,131.4 130.4,128.7 133.7,126.2 137.0,123.8 140.2,121.5 143.5,119.4 146.8,117.4 150.1,115.4 153.4,113.6 156.6,111.8 159.9,110.1 163.2,108.5 166.5,107.0 169.8,105.5 173.1,104.0 176.3,102.6 179.6,101.3 182.9,100.0 186.2,98.7 189.5,97.5 192.7,96.3 196.0,95.1 199.3,94.0 202.6,92.9 205.9,91.9 209.1,90.8 212.4,89.8 215.7,88.8 219.0,87.9 222.3,86.9 225.6,86.0 228.8,85.1 232.1,84.2 235.4,83.4 238.7,82.6 242.0,81.7 245.2,80.9 248.5,80.1 251.8,79.4 255.1,78.6 258.4,77.8 261.6,77.1 264.9,76.4 268.2,75.7 271.5,75.0 274.8,74.3 278.1,73.6 281.3,73.0 284.6,72.3 287.9,71.7 291.2,71.1 294.5,70.4 297.7,69.8 301.0,69.2 304.3,68.6 307.6,68.0 310.9,67.5 314.1,66.9 317.4,66.3 320.7,65.8 324.0,65.2 327.3,64.7 330.6,64.2 333.8,63.7 337.1,63.1 340.4,62.6 343.7,62.1 347.0,61.6 350.2,61.1 353.5,60.6 356.8,60.2 360.1,59.7 363.4,59.2 366.6,58.8 369.9,58.3 373.2,57.9 376.5,57.4 379.8,57.0 383.1,56.5 386.3,56.1 389.6,55.7 392.9,55.2 396.2,54.8 399.5,54.4 402.7,54.0 406.0,53.6 409.3,53.2 412.6,52.8 415.9,52.4 419.1,52.0 422.4,51.6 425.7,51.2 429.0,50.8 432.3,50.5 435.6,50.1 438.8,49.7 442.1,49.4 445.4,49.0 448.7,48.6 452.0,48.3 455.2,47.9 458.5,47.6 461.8,47.2 465.1,46.9 468.4,46.5 471.6,46.2 474.9,45.9 478.2,45.5 481.5,45.2 484.8,44.9 488.1,44.6 491.3,44.2 494.6,43.9 497.9,43.6" fill="none" stroke="#d0564a" stroke-width="2"/><line x1="80.0" y1="72.8" x2="500.0" y2="72.8" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="6 4"/><circle cx="282.0" cy="72.8" r="5" fill="#d0564a"/><text x="508.0" y="47.4" font-size="12" text-anchor="start">Llama-3.2-3B KV</text><text x="86.0" y="34.0" font-size="12" text-anchor="start">실선: fp16 KV-cache, 점선: int4 가중치(같은 색), 점: 교차점</text>
</svg>
```

그림 2 — fp16 KV-cache 크기(실선) vs int4 가중치 크기(같은 색 점선), 세로축 log. 점이 교차점이다. SmolLM2-135M은 3.2K에서 교차하고(단, 이 모델의 최대 context는 8K이므로 그 이후 선은 가상의 연장), Llama-3.2-3B는 15.4K, Llama-3.2-1B는 20.7K, Qwen2.5-0.5B는 22.1K에서 교차한다.

### 2.4 임베디드 연결

SSD 펌웨어에서 L2P 매핑 테이블 크기를 "엔트리 크기 × 엔트리 수"로 잡고 DRAM 예산에 맞추던 것과 똑같다. 차이는 KV-cache는 **대화가 길어질수록 자란다**는 점이다. 그래서 기기 설계에서는 "최대 context"를 제품 사양으로 정해 두고, 그 길이의 KV를 **부팅 때 정적으로** 잡는다(8절의 C 코드).

---

## 3. 해석 모델 — Python 계산기

### 3.1 prefill의 식

prefill은 S개 token을 한 번에 처리한다. 필요한 연산과 바이트:

```
FLOP_prefill  ≈ 2 × (Pm − V·d) × S        ← 선형층: token마다 MAC 1회 = 2 FLOP
              + 2 × V·d                    ← lm_head는 마지막 token만 (다음 token 예측에 필요한 것)
              + 4 × L × nq × hd × S² / 2   ← causal attention: (query, key) 쌍 ≈ S²/2, 쌍마다 QK 2·hd + PV 2·hd
bytes_prefill ≈ 가중치 바이트 (1번 읽기) + KV/token × S (쓰기)
t_prefill     ≈ max(FLOP / peak FLOP/s,  bytes / BW)
```

말로 하면: prefill 시간은 "연산으로 걸리는 시간"과 "메모리로 걸리는 시간" 중 **큰 쪽**이다. 이것이 D3 roofline의 식 그대로다. 가중치를 한 번 읽고 S번 쓰므로 S가 수십만 넘어도 연산 쪽이 커진다.

### 3.2 decode의 식

context가 t인 상태에서 token 하나를 만드는 step:

```
bytes_step(t) = 가중치 바이트 + KV/token × t        ← 가중치 전체 + 과거 KV 전체를 읽는다
FLOP_step(t)  = 2 × Pm + 4 × L × nq × hd × t
t_step(t)     ≈ max(bytes / BW, FLOP / peak)
TPOT          = M개 step의 평균,   t = S … S + M − 1
```

말로 하면: decode는 token 하나에 가중치 1바이트당 곱셈을 한두 번밖에 안 한다(arithmetic intensity ≈ 1~4 FLOP/byte). 그래서 거의 항상 bytes / BW 쪽이 이긴다.

손으로 계산 — Llama-3.2-1B int4, 웨어러블 가정(2 TFLOP/s, 17 GB/s), S = 512:

```
prefill FLOP:
  선형   2 × (1,235,814,400 − 262,668,288) × 512 = 996.5 GFLOP
  lm_head 2 × 262,668,288                         =   0.5 GFLOP
  attn   4 × 16 × 32 × 64 × 512² / 2              =  17.2 GFLOP
  합계                                             ≈ 1,014 GFLOP → ÷ 2 TFLOP/s = 507 ms
prefill bytes: 695 MB + 32 KiB × 512 = 712 MB     → ÷ 17 GB/s = 42 ms   ⇒ compute-bound, TTFT ≈ 507 ms
decode step (t = 512): (695.1 + 16.8) MB ÷ 17 GB/s = 41.9 ms → 23.9 tok/s
decode FLOP: 2 × 1.236 G = 2.5 GFLOP ÷ 2 TFLOP/s = 1.2 ms   ⇒ memory-bound (약 34배 차이)
```

prefill의 arithmetic intensity는 1,014 GFLOP ÷ 712 MB ≈ **1,425 FLOP/byte**, decode는 2.5 GFLOP ÷ 712 MB ≈ **3.5 FLOP/byte**다. 같은 모델, 같은 하드웨어에서 400배 차이. D3의 roofline에서 prefill은 오른쪽 평평한 지붕 위, decode는 왼쪽 경사면 아래쪽에 찍힌다(6.1절 그림).

### 3.3 코드: `llm_perf.py`

무엇을 확인하나: 위의 식을 그대로 코드로 옮긴 계산기다. HF config + 형식(bit) + 하드웨어(peak FLOP/s, BW) + S + M을 받는다. 이후 예제들이 이 파일을 `import`한다. `eff_bw`, `eff_fl`은 "사양 대비 실효 효율" 손잡이다(기본 100% = 이론 상한).

```python
import warnings; warnings.filterwarnings("ignore")
from transformers import AutoConfig

def shape(model_id):
    """HF config → 성능 모델에 필요한 숫자만 뽑는다 (파라미터 수는 B8 3절 공식)."""
    c = AutoConfig.from_pretrained(model_id, local_files_only=True)
    L, d, nq, nkv = c.num_hidden_layers, c.hidden_size, c.num_attention_heads, c.num_key_value_heads
    hd = getattr(c, "head_dim", None) or d // nq
    ffn, V = c.intermediate_size, c.vocab_size
    bias = (nq + 2 * nkv) * hd if c.model_type == "qwen2" else 0     # Qwen2는 q,k,v에 bias
    layer = d * nq * hd + 2 * d * nkv * hd + nq * hd * d + 3 * d * ffn + 2 * d + bias
    P = V * d + L * layer + d + (0 if c.tie_word_embeddings else V * d)
    Pm = P if c.tie_word_embeddings else P - V * d   # token당 곱하는 가중치 (embedding 표 대신 lm_head)
    return dict(L=L, nq=nq, nkv=nkv, hd=hd, V=V, d=d, P=P, Pm=Pm)

def llm_perf(s, w_bits, kv_bits, hw, S, M, eff_bw=1.0, eff_fl=1.0):
    """hw = (peak FLOP/s, BW B/s). S = prompt token 수, M = 생성 token 수. 결과는 초."""
    peak, bw = hw[0] * eff_fl, hw[1] * eff_bw
    W = s["Pm"] * w_bits / 8                               # token당 읽는 가중치 바이트
    kv_tok = 2 * s["L"] * s["nkv"] * s["hd"] * kv_bits / 8 # token당 KV 바이트 (B4 5.3절)
    a = 4 * s["L"] * s["nq"] * s["hd"]                     # (query, key) 한 쌍의 FLOP: QK 2·hd + PV 2·hd
    f_pre = 2 * (s["Pm"] - s["V"] * s["d"]) * S + 2 * s["V"] * s["d"] + a * S * S / 2
    #       선형층(lm_head 제외) × S   + lm_head는 마지막 token만 + causal attention (쌍 ≈ S²/2)
    b_pre = W + kv_tok * S                                 # 가중치 1번 읽기 + KV 쓰기
    t_pre = max(f_pre / peak, b_pre / bw)
    steps = [max((W + kv_tok * t) / bw, (2 * s["Pm"] + a * t) / peak)   # decode step, context t
             for t in range(S, S + M)]
    return dict(ttft=t_pre, tpot=sum(steps) / M, total=t_pre + sum(steps), W=W, kv_tok=kv_tok,
                pre_bound="compute" if f_pre / peak > b_pre / bw else "memory")
```

(이 파일은 함수 정의만 있어서 출력이 없다. 2.3절과 아래 예제의 출력이 이 파일로 계산한 결과다.)

### 3.4 코드로 확인: 모델 4종 × 형식 3종 × 플랫폼 3종

플랫폼 숫자는 **전부 가정**이다. Hark 같은 기기의 실제 SoC·메모리 사양은 공개되어 있지 않다.

| 플랫폼 (가정) | 대역폭 | 유효 연산 | 근거 |
|---|---|---|---|
| 웨어러블 SoC | 17 GB/s | 2 TFLOP/s | LPDDR4X-4266 × 32-bit = 17.1 GB/s (B8), 연산은 소형 NPU를 가정한 값 |
| 폰 | 60 GB/s | 10 TFLOP/s | LPDDR5X 64-bit급을 보수적으로 잡은 값, NPU 유효 연산 가정 |
| M2급 노트북 | 100 GB/s | 3.6 TFLOP/s | Apple M2 사양(메모리 100 GB/s, GPU fp32 약 3.6 TFLOPS) |

무엇을 확인하나: 프롬프트 512 token, 답 64 token, KV fp16일 때 TTFT와 decode tokens/s의 **이론 상한**.

```python
from llm_perf import shape, llm_perf
models = {"SmolLM2-135M": "HuggingFaceTB/SmolLM2-135M-Instruct", "Qwen2.5-0.5B": "Qwen/Qwen2.5-0.5B",
          "Llama-3.2-1B": "unsloth/Llama-3.2-1B", "Llama-3.2-3B": "unsloth/Llama-3.2-3B"}
hws = {"wear 17GB/s": (2e12, 17e9), "phone 60GB/s": (10e12, 60e9), "M2 100GB/s": (3.6e12, 100e9)}  # 전부 가정
wbits = {"fp16": 16, "int8": 8, "int4": 4.5}
S, M = 512, 64
print(f"S={S}, M={M}, KV fp16.  셀 = TTFT ms / decode tok/s")
print(f"{'model':13s}{'W':5s}" + "".join(f"{h:>20s}" for h in hws))
for name, mid in models.items():
    s = shape(mid)
    for q, b in wbits.items():
        cells = []
        for hw in hws.values():
            r = llm_perf(s, b, 16, hw, S, M)
            cells.append(f"{r['ttft']*1e3:7.0f} / {1/r['tpot']:6.1f}")
        print(f"{name:13s}{q:5s}" + "".join(f"{c:>20s}" for c in cells))
r = llm_perf(shape(models["Llama-3.2-1B"]), 4.5, 16, hws["wear 17GB/s"], S, M)
print(f"Llama-1B int4 wear: prefill {r['pre_bound']}-bound, W={r['W']/1e6:.0f} MB, KV/token={r['kv_tok']/1024:.0f} KiB")
```

```text
S=512, M=64, KV fp16.  셀 = TTFT ms / decode tok/s
model        W             wear 17GB/s        phone 60GB/s          M2 100GB/s
SmolLM2-135M fp16          59 /   60.4         12 /  213.1         33 /  355.2
SmolLM2-135M int8          59 /  115.6         12 /  408.1         33 /  680.1
SmolLM2-135M int4          59 /  192.8         12 /  680.4         33 / 1134.0
Qwen2.5-0.5B fp16         189 /   17.1         38 /   60.3        105 /  100.5
Qwen2.5-0.5B int8         189 /   34.0         38 /  119.8        105 /  199.7
Qwen2.5-0.5B int4         189 /   59.7         38 /  210.8        105 /  351.4
Llama-3.2-1B fp16         507 /    6.8        101 /   24.1        282 /   40.2
Llama-3.2-1B int8         507 /   13.6        101 /   47.9        282 /   79.8
Llama-3.2-1B int4         507 /   23.8        101 /   84.2        282 /  140.3
Llama-3.2-3B fp16        1466 /    2.6        293 /    9.2        815 /   15.4
Llama-3.2-3B int8        1466 /    5.2        293 /   18.3        815 /   30.5
Llama-3.2-3B int4        1466 /    9.1        293 /   32.1        815 /   53.5
Llama-1B int4 wear: prefill compute-bound, W=695 MB, KV/token=32 KiB
```

출력에서 볼 것:

- **decode tok/s는 형식에 거의 정비례한다**: fp16 → int8 → int4에서 Llama-3.2-1B 웨어러블 6.8 → 13.6 → 23.8. 읽는 바이트가 16 → 8 → 4.5 bit이기 때문이다(int4가 정확히 4배가 아닌 것은 scale 오버헤드와 KV 항 때문).
- **TTFT는 형식과 무관하게 같다**: 이 계산기는 형식이 바뀌어도 peak FLOP/s를 같게 두었고, prefill은 compute-bound라 바이트가 줄어도 시간이 안 준다. 실제 NPU는 int8 TOPS가 fp16보다 2배쯤 높은 경우가 많아서 int8 경로라면 TTFT도 준다. 반대로 CPU에서 int4 가중치를 매번 풀어서 쓰면 prefill이 오히려 느려질 수 있다(C3 10.1절). 이 계산기의 한계로 기억해 둔다.
- M2급 노트북의 TTFT가 폰보다 긴 것은 가정한 연산 성능(3.6 vs 10 TFLOP/s) 때문이다. decode는 반대로 대역폭이 큰 M2가 빠르다. **prefill과 decode는 서로 다른 하드웨어 스펙에 묶인다**는 것이 한 표에서 보인다.
- 효율 100%라는 이론 상한이다. 실제로는 대역폭 50~80%, 연산 30~60% 수준을 기대한다(4절에서 이 Mac으로 확인).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 386">
<text x="270.0" y="48.0" font-size="12" text-anchor="middle">웨어러블 17 GB/s</text><text x="410.0" y="48.0" font-size="12" text-anchor="middle">폰 60 GB/s</text><text x="550.0" y="48.0" font-size="12" text-anchor="middle">M2급 100 GB/s</text><text x="192.0" y="76.0" font-size="12" text-anchor="end">SmolLM2-135M fp16</text><rect x="201.0" y="61.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.52"/><text x="270.0" y="76.0" font-size="12" text-anchor="middle">60.4</text><rect x="341.0" y="61.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.6"/><text x="410.0" y="76.0" font-size="12" text-anchor="middle">213.1</text><rect x="481.0" y="61.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.63"/><text x="550.0" y="76.0" font-size="12" text-anchor="middle">355.2</text><text x="192.0" y="100.0" font-size="12" text-anchor="end">SmolLM2-135M int8</text><rect x="201.0" y="85.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.56"/><text x="270.0" y="100.0" font-size="12" text-anchor="middle">115.6</text><rect x="341.0" y="85.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.64"/><text x="410.0" y="100.0" font-size="12" text-anchor="middle">408.1</text><rect x="481.0" y="85.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.67"/><text x="550.0" y="100.0" font-size="12" text-anchor="middle">680.1</text><text x="192.0" y="124.0" font-size="12" text-anchor="end">SmolLM2-135M int4</text><rect x="201.0" y="109.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.59"/><text x="270.0" y="124.0" font-size="12" text-anchor="middle">192.8</text><rect x="341.0" y="109.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.67"/><text x="410.0" y="124.0" font-size="12" text-anchor="middle">680.4</text><rect x="481.0" y="109.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.7"/><text x="550.0" y="124.0" font-size="12" text-anchor="middle">1134</text><line x1="50.0" y1="132.0" x2="620.0" y2="132.0" stroke="#888" stroke-width="1" stroke-dasharray="3 3"/><text x="192.0" y="148.0" font-size="12" text-anchor="end">Qwen2.5-0.5B fp16</text><rect x="201.0" y="133.0" width="138.0" height="22.0" fill="#e08a3c" fill-opacity="0.43"/><text x="270.0" y="148.0" font-size="12" text-anchor="middle">17.1</text><rect x="341.0" y="133.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.52"/><text x="410.0" y="148.0" font-size="12" text-anchor="middle">60.3</text><rect x="481.0" y="133.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.55"/><text x="550.0" y="148.0" font-size="12" text-anchor="middle">100.5</text><text x="192.0" y="172.0" font-size="12" text-anchor="end">Qwen2.5-0.5B int8</text><rect x="201.0" y="157.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.48"/><text x="270.0" y="172.0" font-size="12" text-anchor="middle">34</text><rect x="341.0" y="157.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.56"/><text x="410.0" y="172.0" font-size="12" text-anchor="middle">119.8</text><rect x="481.0" y="157.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.6"/><text x="550.0" y="172.0" font-size="12" text-anchor="middle">199.7</text><text x="192.0" y="196.0" font-size="12" text-anchor="end">Qwen2.5-0.5B int4</text><rect x="201.0" y="181.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.52"/><text x="270.0" y="196.0" font-size="12" text-anchor="middle">59.7</text><rect x="341.0" y="181.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.6"/><text x="410.0" y="196.0" font-size="12" text-anchor="middle">210.8</text><rect x="481.0" y="181.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.63"/><text x="550.0" y="196.0" font-size="12" text-anchor="middle">351.4</text><line x1="50.0" y1="204.0" x2="620.0" y2="204.0" stroke="#888" stroke-width="1" stroke-dasharray="3 3"/><text x="192.0" y="220.0" font-size="12" text-anchor="end">Llama-3.2-1B fp16</text><rect x="201.0" y="205.0" width="138.0" height="22.0" fill="#d0564a" fill-opacity="0.37"/><text x="270.0" y="220.0" font-size="12" text-anchor="middle">6.8</text><rect x="341.0" y="205.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.46"/><text x="410.0" y="220.0" font-size="12" text-anchor="middle">24.1</text><rect x="481.0" y="205.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.49"/><text x="550.0" y="220.0" font-size="12" text-anchor="middle">40.2</text><text x="192.0" y="244.0" font-size="12" text-anchor="end">Llama-3.2-1B int8</text><rect x="201.0" y="229.0" width="138.0" height="22.0" fill="#e08a3c" fill-opacity="0.42"/><text x="270.0" y="244.0" font-size="12" text-anchor="middle">13.6</text><rect x="341.0" y="229.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.5"/><text x="410.0" y="244.0" font-size="12" text-anchor="middle">47.9</text><rect x="481.0" y="229.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.54"/><text x="550.0" y="244.0" font-size="12" text-anchor="middle">79.8</text><text x="192.0" y="268.0" font-size="12" text-anchor="end">Llama-3.2-1B int4</text><rect x="201.0" y="253.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.46"/><text x="270.0" y="268.0" font-size="12" text-anchor="middle">23.8</text><rect x="341.0" y="253.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.54"/><text x="410.0" y="268.0" font-size="12" text-anchor="middle">84.2</text><rect x="481.0" y="253.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.57"/><text x="550.0" y="268.0" font-size="12" text-anchor="middle">140.3</text><line x1="50.0" y1="276.0" x2="620.0" y2="276.0" stroke="#888" stroke-width="1" stroke-dasharray="3 3"/><text x="192.0" y="292.0" font-size="12" text-anchor="end">Llama-3.2-3B fp16</text><rect x="201.0" y="277.0" width="138.0" height="22.0" fill="#d0564a" fill-opacity="0.31"/><text x="270.0" y="292.0" font-size="12" text-anchor="middle">2.6</text><rect x="341.0" y="277.0" width="138.0" height="22.0" fill="#e08a3c" fill-opacity="0.39"/><text x="410.0" y="292.0" font-size="12" text-anchor="middle">9.2</text><rect x="481.0" y="277.0" width="138.0" height="22.0" fill="#e08a3c" fill-opacity="0.43"/><text x="550.0" y="292.0" font-size="12" text-anchor="middle">15.4</text><text x="192.0" y="316.0" font-size="12" text-anchor="end">Llama-3.2-3B int8</text><rect x="201.0" y="301.0" width="138.0" height="22.0" fill="#d0564a" fill-opacity="0.36"/><text x="270.0" y="316.0" font-size="12" text-anchor="middle">5.2</text><rect x="341.0" y="301.0" width="138.0" height="22.0" fill="#e08a3c" fill-opacity="0.44"/><text x="410.0" y="316.0" font-size="12" text-anchor="middle">18.3</text><rect x="481.0" y="301.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.47"/><text x="550.0" y="316.0" font-size="12" text-anchor="middle">30.5</text><text x="192.0" y="340.0" font-size="12" text-anchor="end">Llama-3.2-3B int4</text><rect x="201.0" y="325.0" width="138.0" height="22.0" fill="#e08a3c" fill-opacity="0.39"/><text x="270.0" y="340.0" font-size="12" text-anchor="middle">9.1</text><rect x="341.0" y="325.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.48"/><text x="410.0" y="340.0" font-size="12" text-anchor="middle">32.1</text><rect x="481.0" y="325.0" width="138.0" height="22.0" fill="#3f9a6b" fill-opacity="0.51"/><text x="550.0" y="340.0" font-size="12" text-anchor="middle">53.5</text><rect x="50.0" y="362.0" width="14.0" height="12.0" fill="#d0564a" fill-opacity="0.5"/><text x="70.0" y="372.0" font-size="12" text-anchor="start">7 tok/s 미만 (말보다 느림)</text><rect x="250.0" y="362.0" width="14.0" height="12.0" fill="#e08a3c" fill-opacity="0.5"/><text x="270.0" y="372.0" font-size="12" text-anchor="start">7~20</text><rect x="330.0" y="362.0" width="14.0" height="12.0" fill="#3f9a6b" fill-opacity="0.5"/><text x="350.0" y="372.0" font-size="12" text-anchor="start">20 이상 (대화 여유)</text><text x="50.0" y="24.0" font-size="12" text-anchor="start">decode tok/s 상한 (prompt 512 + 64 생성, KV fp16, 효율 100% 가정)</text>
</svg>
```

그림 3 — decode tok/s 상한 표를 색으로. 빨강 = 7 tok/s 미만(사람 말보다 느린 수준), 주황 = 7~20, 초록 = 20 이상. 웨어러블급 대역폭에서 1B fp16은 빨강이고, 1B int4가 되어야 초록에 들어간다. 3B는 int4여도 웨어러블에서 주황이다.

### 3.5 계산기의 가정 목록 (반드시 같이 말할 것)

- 가중치는 매 decode step에 **전부 DRAM에서** 읽는다(온칩 캐시에 안 들어간다고 가정). 135M int4(76 MB)도 대부분의 SoC 캐시보다 크다.
- KV는 fp16, 매 step 전부 한 번 읽는다(이상적인 fused attention).
- 활성값·norm·RoPE·sampler의 바이트와 시간은 무시한다.
- prefill은 한 번에 처리한다(chunked prefill이면 가중치를 chunk 수만큼 다시 읽어서 바이트가 늘지만 보통 여전히 compute-bound).
- 효율 손잡이(`eff_bw`, `eff_fl`)를 1로 두면 **하한 시간 = 상한 속도**다. 실측이 이보다 빠르면 계산이 틀렸거나 캐시 효과가 있는 것이다.
- 에너지: token당 DRAM 읽기 바이트가 곧 에너지의 큰 부분이다. token당 에너지 계산은 D7에서 이 계산기의 `W + KV` 바이트에 pJ/bit를 곱하는 식으로 이어진다.

---

## 4. 실측으로 검증 — 이 Mac에서 SmolLM2-135M fp32

계산기는 믿을 수 있어야 쓸모가 있다. 이 절에서는 이 Mac(Apple M2, CPU 4 thread로 PyTorch 실행)에서 하드웨어 숫자를 먼저 재고, 그 숫자를 계산기에 넣어 SmolLM2-135M fp32의 prefill과 decode를 예측한 뒤 실측과 비교한다.

**측정 환경 주의**: 측정 당시 이 Mac에서는 다른 작업이 병렬로 돌고 있었다(load average 3~7). 모든 측정은 warm-up 후 여러 번 재서 **중앙값**을 쓰고, 같은 코드를 2~3번 실행해서 값이 크게 다르면 그 사실을 적었다. 절대값보다 **비율과 추세**를 본다.

### 4.1 하드웨어 숫자 재기 — STREAM과 GEMM

무엇을 확인하나: 계산기에 넣을 BW와 peak FLOP/s를 이 Mac에서 직접 잰다. STREAM은 큰 배열 복사·합으로 DRAM 대역폭을 재는 고전적인 벤치마크 방식이다. 배열은 256 MiB로 온칩 캐시(M2 성능 코어 L2 16 MB)보다 훨씬 크게 잡는다.

```python
import time, statistics, torch
def med(f, n=15, warm=3):                        # warm-up 후 n번 재서 중앙값
    for _ in range(warm): f()
    ts = []
    for _ in range(n):
        t = time.perf_counter(); f(); ts.append(time.perf_counter() - t)
    return statistics.median(ts)
torch.manual_seed(0)
print("torch threads:", torch.get_num_threads())
a = torch.randn(64 * 2**20); b = torch.empty_like(a)   # 256 MiB — 온칩 캐시보다 훨씬 큼
nb = a.numel() * 4
print(f"STREAM copy : {2 * nb / med(lambda: b.copy_(a)) / 1e9:5.1f} GB/s (읽기+쓰기)")
print(f"STREAM sum  : {nb / med(lambda: a.sum()) / 1e9:5.1f} GB/s (읽기만)")
for n in (512, 1024, 2048):
    A, B = torch.randn(n, n), torch.randn(n, n)
    print(f"GEMM {n:4d}³ : {2 * n**3 / med(lambda: A @ B, n=10) / 1e9:6.0f} GFLOP/s (fp32)")
x, W = torch.randn(1, 576), torch.randn(1536, 576)   # SmolLM2 MLP 모양 GEMV, 가중치 3.5 MB
t = med(lambda: x @ W.T, n=200)
print(f"GEMV 1×576·576×1536: {2*576*1536/t/1e9:5.1f} GFLOP/s, 가중치 읽기 {576*1536*4/t/1e9:5.0f} GB/s")
```

```text
torch threads: 4
STREAM copy :  72.3 GB/s (읽기+쓰기)
STREAM sum  :  64.0 GB/s (읽기만)
GEMM  512³ :   1357 GFLOP/s (fp32)
GEMM 1024³ :   1381 GFLOP/s (fp32)
GEMM 2048³ :   1175 GFLOP/s (fp32)
GEMV 1×576·576×1536:  96.2 GFLOP/s, 가중치 읽기   192 GB/s
```

출력에서 볼 것:

- **읽기 대역폭 약 64 GB/s** (copy는 읽기+쓰기를 합쳐 72 GB/s). M2 사양 100 GB/s의 약 2/3다. CPU 쪽에서 4 thread로 낼 수 있는 실효값이다. 세 번 실행에서 sum은 58~64 GB/s, GEMM 2048³은 1,050~1,210 GFLOP/s 사이에서 흔들렸다. 아래 예제에서는 대표값으로 **BW = 60 GB/s, peak = 1.2 TFLOP/s**를 쓴다.
- **GEMM이 1.2~1.4 TFLOP/s**: fp32 CPU치고 높다. macOS의 PyTorch는 Apple Accelerate를 쓰고, 이것이 M2의 행렬 가속 유닛을 쓰는 것으로 알려져 있다. 여기서 중요한 것은 이유보다 "재 보니 이만큼"이라는 숫자다.
- **GEMV가 192 GB/s**: DRAM 대역폭(64 GB/s)보다 높다! 가중치가 3.5 MB라 캐시에 들어가 있기 때문이다. **마이크로벤치마크는 working set 크기를 실제 워크로드와 맞춰야 한다**. 모델 전체(538 MB)를 도는 decode에서는 이런 숫자가 나오지 않는다. 펌웨어로 치면 "같은 LBA만 반복해서 읽으면 캐시 hit로 IOPS가 뻥튀기되는" 함정이다.

### 4.2 prefill: 예측 vs 실측

무엇을 확인하나: 프롬프트 길이 S = 64 … 2048에서 TTFT를 재서 계산기 예측과 비교한다. `logits_to_keep=1`은 lm_head를 마지막 위치에만 계산하게 하는 인자다(실제 런타임과 같은 동작). "실측 패치" 열은 attention 경로를 하나 바꾼 결과인데, 무엇을 바꿨는지는 바로 다음 4.3절에서 profiler로 밝힌다.

```python
import time, statistics, torch
import transformers.integrations.sdpa_attention as sa
from transformers import AutoModelForCausalLM
from llm_perf import shape, llm_perf
mid = "HuggingFaceTB/SmolLM2-135M-Instruct"
model = AutoModelForCausalLM.from_pretrained(mid, dtype=torch.float32, local_files_only=True).eval()
s, hw = shape(mid), (1.2e12, 60e9)                  # 4.1절에서 잰 GEMM·STREAM 대표값
ids = torch.randint(100, 40000, (1, 2048), generator=torch.Generator().manual_seed(0))
orig, patched = sa.use_gqa_in_sdpa, (lambda *a, **k: False)
def ttft(S, fn):                                    # 중앙값 (반복 5회, 긴 것은 3회)
    sa.use_gqa_in_sdpa = fn; ts = []
    for _ in range(5 if S <= 512 else 3):
        t = time.perf_counter(); model(ids[:, :S], logits_to_keep=1); ts.append(time.perf_counter() - t)
    return statistics.median(ts)
with torch.no_grad():
    model(ids[:, :64])                                                   # warm-up
    for S in (64, 128, 256, 512, 1024, 2048):
        r = llm_perf(s, 32, 32, hw, S, 1)                                # fp32 가중치·KV
        a, b = ttft(S, orig), ttft(S, patched)
        print(f"S={S:5d} 예측 {r['ttft']*1e3:6.1f} ms ({r['pre_bound'][:4]}) | 실측 기본 {a*1e3:7.1f} ms"
              f" (×{a/r['ttft']:3.1f}) | 실측 패치 {b*1e3:7.1f} ms (×{b/r['ttft']:3.1f})")
```

```text
S=   64 예측   11.5 ms (comp) | 실측 기본    60.2 ms (×5.2) | 실측 패치    62.9 ms (×5.5)
S=  128 예측   23.2 ms (comp) | 실측 기본    81.3 ms (×3.5) | 실측 패치    62.5 ms (×2.7)
S=  256 예측   47.2 ms (comp) | 실측 기본   119.8 ms (×2.5) | 실측 패치   100.6 ms (×2.1)
S=  512 예측   98.2 ms (comp) | 실측 기본   273.0 ms (×2.8) | 실측 패치   201.2 ms (×2.0)
S= 1024 예측  211.5 ms (comp) | 실측 기본   814.4 ms (×3.9) | 실측 패치   459.0 ms (×2.2)
S= 2048 예측  483.4 ms (comp) | 실측 기본  2674.3 ms (×5.5) | 실측 패치   958.3 ms (×2.0)
```

출력에서 볼 것:

- 계산기는 모든 S에서 **compute-bound**를 예측한다(가중치 538 MB ÷ 60 GB/s = 9 ms보다 연산 시간이 길다).
- **중간 구간(S = 256~2048, 패치)**: 실측이 예측의 **약 2.0~2.2배**로 일정하다. 비율이 일정하다는 것은 모델의 **모양이 맞고** 효율만 다르다는 뜻이다. 실효 연산 효율 약 50%로 읽으면 된다.
- **짧은 프롬프트(S = 64)**: 예측 11.5 ms, 실측 60 ms로 5배. 행이 64개뿐인 GEMM은 행렬 유닛을 채우지 못하고, 층 30개 × 연산 수십 개의 호출 오버헤드가 고정으로 붙는다. **작은 문제에서는 오버헤드가 지배한다.**
- **긴 프롬프트(HF 기본)**: S = 2048에서 예측의 5.5배. 비율이 S와 함께 커진다는 것은 **S²에 비례하는 무언가가 모델보다 훨씬 비싸다**는 신호다. 다음 절에서 원인을 찾는다.
- 두 번째 실행에서는 S = 2048 기본 3,154 ms(×6.5), 패치 1,171 ms(×2.4)였다. 흔들림은 있지만 결론은 같다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 320">
<line x1="80.0" y1="270.0" x2="560.0" y2="270.0" stroke="currentColor" stroke-width="1"/><line x1="80.0" y1="270.0" x2="80.0" y2="30.0" stroke="currentColor" stroke-width="1"/><line x1="80.0" y1="270.0" x2="80.0" y2="275.0" stroke="currentColor" stroke-width="1"/><text x="80.0" y="289.0" font-size="12" text-anchor="middle">64</text><line x1="176.0" y1="270.0" x2="176.0" y2="275.0" stroke="currentColor" stroke-width="1"/><text x="176.0" y="289.0" font-size="12" text-anchor="middle">128</text><line x1="272.0" y1="270.0" x2="272.0" y2="275.0" stroke="currentColor" stroke-width="1"/><text x="272.0" y="289.0" font-size="12" text-anchor="middle">256</text><line x1="368.0" y1="270.0" x2="368.0" y2="275.0" stroke="currentColor" stroke-width="1"/><text x="368.0" y="289.0" font-size="12" text-anchor="middle">512</text><line x1="464.0" y1="270.0" x2="464.0" y2="275.0" stroke="currentColor" stroke-width="1"/><text x="464.0" y="289.0" font-size="12" text-anchor="middle">1024</text><line x1="560.0" y1="270.0" x2="560.0" y2="275.0" stroke="currentColor" stroke-width="1"/><text x="560.0" y="289.0" font-size="12" text-anchor="middle">2048</text><line x1="75.0" y1="270.0" x2="80.0" y2="270.0" stroke="currentColor" stroke-width="1"/><text x="72.0" y="274.0" font-size="12" text-anchor="end">10</text><line x1="80.0" y1="270.0" x2="560.0" y2="270.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><line x1="75.0" y1="227.6" x2="80.0" y2="227.6" stroke="currentColor" stroke-width="1"/><text x="72.0" y="231.6" font-size="12" text-anchor="end">30</text><line x1="80.0" y1="227.6" x2="560.0" y2="227.6" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><line x1="75.0" y1="181.1" x2="80.0" y2="181.1" stroke="currentColor" stroke-width="1"/><text x="72.0" y="185.1" font-size="12" text-anchor="end">100</text><line x1="80.0" y1="181.1" x2="560.0" y2="181.1" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><line x1="75.0" y1="138.7" x2="80.0" y2="138.7" stroke="currentColor" stroke-width="1"/><text x="72.0" y="142.7" font-size="12" text-anchor="end">300</text><line x1="80.0" y1="138.7" x2="560.0" y2="138.7" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><line x1="75.0" y1="92.2" x2="80.0" y2="92.2" stroke="currentColor" stroke-width="1"/><text x="72.0" y="96.2" font-size="12" text-anchor="end">1000</text><line x1="80.0" y1="92.2" x2="560.0" y2="92.2" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><line x1="75.0" y1="49.7" x2="80.0" y2="49.7" stroke="currentColor" stroke-width="1"/><text x="72.0" y="53.7" font-size="12" text-anchor="end">3000</text><line x1="80.0" y1="49.7" x2="560.0" y2="49.7" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="320.0" y="308.0" font-size="12" text-anchor="middle">prompt 길이 S (token, log)</text><text x="20.0" y="150.0" font-size="12" text-anchor="middle" transform="rotate(-90 20 150)">TTFT ms (log)</text><polyline points="80.0,264.6 176.0,237.5 272.0,210.1 368.0,181.8 464.0,152.1 560.0,120.2" fill="none" stroke="#888" stroke-width="2" stroke-dasharray="6 4"/><polyline points="80.0,200.7 176.0,189.1 272.0,174.1 368.0,142.3 464.0,100.1 560.0,54.2" fill="none" stroke="#d0564a" stroke-width="2"/><circle cx="80.0" cy="200.7" r="3.5" fill="#d0564a"/><circle cx="176.0" cy="189.1" r="3.5" fill="#d0564a"/><circle cx="272.0" cy="174.1" r="3.5" fill="#d0564a"/><circle cx="368.0" cy="142.3" r="3.5" fill="#d0564a"/><circle cx="464.0" cy="100.1" r="3.5" fill="#d0564a"/><circle cx="560.0" cy="54.2" r="3.5" fill="#d0564a"/><polyline points="80.0,199.0 176.0,199.2 272.0,180.8 368.0,154.1 464.0,122.2 560.0,93.8" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="80.0" cy="199.0" r="3.5" fill="#4a7bd0"/><circle cx="176.0" cy="199.2" r="3.5" fill="#4a7bd0"/><circle cx="272.0" cy="180.8" r="3.5" fill="#4a7bd0"/><circle cx="368.0" cy="154.1" r="3.5" fill="#4a7bd0"/><circle cx="464.0" cy="122.2" r="3.5" fill="#4a7bd0"/><circle cx="560.0" cy="93.8" r="3.5" fill="#4a7bd0"/><line x1="100.0" y1="50.0" x2="130.0" y2="50.0" stroke="#888" stroke-width="2" stroke-dasharray="6 4"/><text x="136.0" y="54.0" font-size="12" text-anchor="start">예측 (1.2 TFLOP/s, 60 GB/s 측정값)</text><line x1="100.0" y1="70.0" x2="130.0" y2="70.0" stroke="#d0564a" stroke-width="2"/><text x="136.0" y="74.0" font-size="12" text-anchor="start">실측: HF 기본 (math attention)</text><line x1="100.0" y1="90.0" x2="130.0" y2="90.0" stroke="#4a7bd0" stroke-width="2"/><text x="136.0" y="94.0" font-size="12" text-anchor="start">실측: fused attention 패치</text><text x="568.0" y="58.2" font-size="12" text-anchor="start">×5.5</text><text x="568.0" y="97.8" font-size="12" text-anchor="start">×2.0</text><text x="84.0" y="222.7" font-size="12" text-anchor="start">×5 (고정 비용)</text>
</svg>
```

그림 4 — prefill TTFT, log-log. 회색 점선(예측)과 파란 선(패치된 실측)은 기울기가 거의 같다(평행 = 효율만 다름). 빨간 선(HF 기본)은 S가 커질수록 위로 휜다 — S² 항이 모델보다 비싸게 구현되어 있다는 뜻이다. 왼쪽 끝은 고정 오버헤드 때문에 평평하다.

### 4.3 profiler로 원인 찾기 — GQA와 attention 커널 경로

펌웨어에서 "예상보다 느리다"면 로직 애널라이저나 trace를 건다. PyTorch에서는 `torch.profiler`가 그 역할이다. S = 2048 prefill을 op 종류별로 묶어 본다.

무엇을 확인하나: 시간이 선형층(`aten::mm`), attention 관련 op, 기타 중 어디에 쓰이는지. 그리고 HF의 SDPA 경로를 하나 바꾸면 어떻게 달라지는지.

```python
import time, torch
import transformers.integrations.sdpa_attention as sa
from torch.profiler import profile
from transformers import AutoModelForCausalLM
model = AutoModelForCausalLM.from_pretrained("HuggingFaceTB/SmolLM2-135M-Instruct",
                                             dtype=torch.float32, local_files_only=True).eval()
ids = torch.randint(100, 40000, (1, 2048), generator=torch.Generator().manual_seed(0))
ATTN = {"aten::bmm", "aten::_softmax", "aten::all", "aten::isneginf", "aten::where", "aten::add_",
        "aten::_scaled_dot_product_flash_attention_for_cpu"}
def breakdown(tag):
    with torch.no_grad():
        model(ids[:, :256])                                          # warm-up
        with profile() as p:
            t = time.perf_counter(); model(ids, logits_to_keep=1); t = time.perf_counter() - t
    g = {"linear(mm)": 0.0, "attention": 0.0, "기타": 0.0}
    for e in p.key_averages():
        k = "linear(mm)" if e.key == "aten::mm" else "attention" if e.key in ATTN else "기타"
        g[k] += e.self_cpu_time_total / 1e3                          # µs → ms
    print(f"{tag}: wall {t*1e3:5.0f} ms | " + " | ".join(f"{k} {v:5.0f} ms" for k, v in g.items()))
breakdown("기본 (enable_gqa → math)")
sa.use_gqa_in_sdpa = lambda *a, **k: False      # K/V를 복제한 뒤 fused CPU flash kernel로 보내기
breakdown("패치 (repeat_kv → flash)")
```

```text
기본 (enable_gqa → math): wall  2460 ms | linear(mm)   415 ms | attention  1865 ms | 기타   166 ms
패치 (repeat_kv → flash): wall   987 ms | linear(mm)   421 ms | attention   413 ms | 기타   140 ms
```

출력에서 볼 것:

- **선형층은 모델대로다**: `mm` 415 ms. 계산기의 선형층 예측은 `2 × 106.2M × 2048 ÷ 1.2 TFLOP/s = 363 ms`로, 실측이 예측의 1.15배다. GEMM 부분은 거의 이상적으로 돈다.
- **attention이 범인이다**: 기본 경로에서 1,865 ms. 예측(causal FLOP 기준 121 ms)의 15배다.
- 원인: transformers 4.57(이 venv는 4.57.6)의 SDPA 통합 코드는 CUDA가 아니어도 mask가 없으면 `enable_gqa=True`로 PyTorch SDPA를 부른다. 이 Mac의 PyTorch 2.8 CPU에서는 그 조합이 fused 커널이 아니라 **math 경로**(`_scaled_dot_product_attention_math`)로 떨어진다. math 경로는 S × S 점수 행렬을 통째로 메모리에 만들고, `_safe_softmax`가 그 위에서 `isneginf`, `all`, `where`를 따로 돈다. 층당 9 head × 2048² × 4 B = 151 MB짜리 텐서를 여러 번 쓰고 읽는 셈이다(B4 10.3절의 FlashAttention 이전 상황).
- **패치**: `use_gqa_in_sdpa`가 False를 돌려주게 하면 HF가 K/V를 query head 수만큼 복제(`repeat_kv`)한 뒤 `is_causal=True`로 SDPA를 부르고, 이 조합은 CPU fused flash 커널(`_scaled_dot_product_flash_attention_for_cpu`)로 간다. attention이 1,865 → 413 ms, 전체 2,460 → 987 ms.
- 한 번은 패치 쪽이 wall 1,810 ms(mm 968 ms)로 나왔다. 다른 작업과 겹친 것으로 보이고, 재실행에서 위 값으로 돌아왔다. 한 번의 측정으로 결론 내리지 않는 이유다.

일반화된 교훈: **"같은 수학"이라도 커널 경로 하나가 4배를 바꾼다**. 온디바이스 런타임에서 "왜 벤더 데모보다 느리지?"의 흔한 원인이 이것이다 — 특정 연산 조합(GQA + mask, 특정 head_dim, 특정 dtype)이 NPU에서 지원되지 않아 CPU fallback이나 비융합 경로로 떨어진다(F8, G 모듈). 해석 모델이 있어야 "이건 모델보다 15배 느린 op"라는 걸 알아챌 수 있다.

### 4.4 decode: context에 따른 ms/token — 예측 vs 실측

무엇을 확인하나: context C = 16 … 4096에서 decode step 시간을 잰다. 각 C마다 KV-cache를 채운 뒤 20 step을 재고, 전체를 3번 반복해서 중앙값을 쓴다. 그리고 실측에 직선을 맞춰 절편(가중치 쪽)과 기울기(KV 쪽)를 이상 모델과 비교한다.

```python
import time, statistics, numpy as np, torch
import transformers.integrations.sdpa_attention as sa
from transformers import AutoModelForCausalLM
from llm_perf import shape, llm_perf
mid = "HuggingFaceTB/SmolLM2-135M-Instruct"
model = AutoModelForCausalLM.from_pretrained(mid, dtype=torch.float32, local_files_only=True).eval()
sa.use_gqa_in_sdpa = lambda *a, **k: False            # 4.3절의 패치 (fused attention 경로)
s, hw = shape(mid), (1.2e12, 60e9)
ids = torch.randint(100, 40000, (1, 4200), generator=torch.Generator().manual_seed(0))
ctxs, meas = (16, 512, 1024, 2048, 3072, 4096), []
with torch.no_grad():
    for C in ctxs:
        runs = []
        for rep in range(3):                              # 3회 반복, 각 회 20 step의 중앙값
            past, ts = model(ids[:, :C]).past_key_values, []
            for i in range(20):
                t = time.perf_counter()
                past = model(ids[:, C + i:C + i + 1], past_key_values=past).past_key_values
                ts.append(time.perf_counter() - t)
            runs.append(statistics.median(ts[2:]))
        meas.append(statistics.median(runs))
        pred = llm_perf(s, 32, 32, hw, C, 1)["tpot"]
        print(f"ctx={C:5d}  실측 {meas[-1]*1e3:5.1f} ms/token  예측(이상) {pred*1e3:5.1f} ms")
b, a = np.polyfit(ctxs, np.array(meas) * 1e3, 1)          # 실측 = a + b·ctx
kv_tok = 2 * s["L"] * s["nkv"] * s["hd"] * 4              # fp32 KV 바이트/token
ideal_a, ideal_b = s["Pm"] * 4 / hw[1] * 1e3, kv_tok / hw[1] * 1e3
print(f"직선 맞춤: {a:.1f} ms + {b*1e3:.2f} µs × ctx")
print(f"이상 모델: {ideal_a:.1f} ms + {ideal_b*1e3:.2f} µs × ctx  → 절편 ×{a/ideal_a:.1f}, 기울기 ×{b/ideal_b:.1f}")
```

```text
ctx=   16  실측  16.6 ms/token  예측(이상)   9.0 ms
ctx=  512  실측  20.8 ms/token  예측(이상)   9.4 ms
ctx= 1024  실측  23.5 ms/token  예측(이상)   9.8 ms
ctx= 2048  실측  29.4 ms/token  예측(이상)  10.5 ms
ctx= 3072  실측  37.5 ms/token  예측(이상)  11.3 ms
ctx= 4096  실측  43.2 ms/token  예측(이상)  12.1 ms
직선 맞춤: 16.8 ms + 6.49 µs × ctx
이상 모델: 9.0 ms + 0.77 µs × ctx  → 절편 ×1.9, 기울기 ×8.4
```

출력에서 볼 것:

- **TPOT는 context에 선형으로 는다**: 16.6 → 43.2 ms. 모양은 계산기 식(`절편 + 기울기 × ctx`)과 같다.
- **절편 ×1.9**: 이상 모델의 절편 9.0 ms는 가중치 538 MB ÷ 60 GB/s다. 실측 16.8 ms. B8 4.6절에서 Linear 211개의 GEMV만 따로 잰 값이 14.1 ms(실효 38 GB/s)였으니, 나머지 약 3 ms가 RMSNorm·RoPE·SiLU·잔차 덧셈 등 수백 개의 작은 op와 Python 호출 오버헤드다. SmolLM2-135M은 **층이 30개로 많고 각 층의 행렬이 작아서** 호출 오버헤드의 비중이 크다. 큰 모델일수록 이 비중은 줄어든다.
- **기울기 ×8.4**: 이상 모델은 KV를 step마다 한 번 읽는다(fp32 KV 46,080 B/token ÷ 60 GB/s = 0.77 µs/token). 실측은 6.49 µs/token이다. KV 1바이트를 처리하는 데 **약 8배의 메모리 트래픽**이 들고 있다는 뜻이다. 다음 절에서 이 8배를 회계한다.
- 다른 실행에서는 18.6 ms + 5.86 µs × ctx(절편 ×2.1, 기울기 ×7.6)였다. 역시 결론은 같다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 310">
<line x1="70.0" y1="260.0" x2="520.0" y2="260.0" stroke="currentColor" stroke-width="1"/><line x1="70.0" y1="260.0" x2="70.0" y2="30.0" stroke="currentColor" stroke-width="1"/><line x1="70.0" y1="260.0" x2="70.0" y2="265.0" stroke="currentColor" stroke-width="1"/><text x="70.0" y="279.0" font-size="12" text-anchor="middle">0</text><line x1="182.5" y1="260.0" x2="182.5" y2="265.0" stroke="currentColor" stroke-width="1"/><text x="182.5" y="279.0" font-size="12" text-anchor="middle">1024</text><line x1="295.0" y1="260.0" x2="295.0" y2="265.0" stroke="currentColor" stroke-width="1"/><text x="295.0" y="279.0" font-size="12" text-anchor="middle">2048</text><line x1="407.5" y1="260.0" x2="407.5" y2="265.0" stroke="currentColor" stroke-width="1"/><text x="407.5" y="279.0" font-size="12" text-anchor="middle">3072</text><line x1="520.0" y1="260.0" x2="520.0" y2="265.0" stroke="currentColor" stroke-width="1"/><text x="520.0" y="279.0" font-size="12" text-anchor="middle">4096</text><line x1="65.0" y1="260.0" x2="70.0" y2="260.0" stroke="currentColor" stroke-width="1"/><text x="62.0" y="264.0" font-size="12" text-anchor="end">0</text><line x1="65.0" y1="214.0" x2="70.0" y2="214.0" stroke="currentColor" stroke-width="1"/><text x="62.0" y="218.0" font-size="12" text-anchor="end">10</text><line x1="70.0" y1="214.0" x2="520.0" y2="214.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><line x1="65.0" y1="168.0" x2="70.0" y2="168.0" stroke="currentColor" stroke-width="1"/><text x="62.0" y="172.0" font-size="12" text-anchor="end">20</text><line x1="70.0" y1="168.0" x2="520.0" y2="168.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><line x1="65.0" y1="122.0" x2="70.0" y2="122.0" stroke="currentColor" stroke-width="1"/><text x="62.0" y="126.0" font-size="12" text-anchor="end">30</text><line x1="70.0" y1="122.0" x2="520.0" y2="122.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><line x1="65.0" y1="76.0" x2="70.0" y2="76.0" stroke="currentColor" stroke-width="1"/><text x="62.0" y="80.0" font-size="12" text-anchor="end">40</text><line x1="70.0" y1="76.0" x2="520.0" y2="76.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><line x1="65.0" y1="30.0" x2="70.0" y2="30.0" stroke="currentColor" stroke-width="1"/><text x="62.0" y="34.0" font-size="12" text-anchor="end">50</text><line x1="70.0" y1="30.0" x2="520.0" y2="30.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="295.0" y="298.0" font-size="12" text-anchor="middle">context 길이 (token)</text><text x="20.0" y="145.0" font-size="12" text-anchor="middle" transform="rotate(-90 20 145)">ms / token</text><polyline points="70.0,218.6 520.0,204.1" fill="none" stroke="#888" stroke-width="2" stroke-dasharray="6 4"/><polyline points="70.0,182.7 520.0,60.4" fill="none" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="3 3"/><polyline points="71.8,183.6 126.2,164.3 182.5,151.9 295.0,124.8 407.5,87.5 520.0,61.3" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="71.8" cy="183.6" r="3.5" fill="#4a7bd0"/><circle cx="126.2" cy="164.3" r="3.5" fill="#4a7bd0"/><circle cx="182.5" cy="151.9" r="3.5" fill="#4a7bd0"/><circle cx="295.0" cy="124.8" r="3.5" fill="#4a7bd0"/><circle cx="407.5" cy="87.5" r="3.5" fill="#4a7bd0"/><circle cx="520.0" cy="61.3" r="3.5" fill="#4a7bd0"/><text x="526.0" y="208.3" font-size="12" text-anchor="start">이상 모델 12.1</text><text x="526.0" y="65.3" font-size="12" text-anchor="start">실측 43.2</text><text x="103.0" y="234.6" font-size="12" text-anchor="start">절편 9.0 ms = 538 MB ÷ 60 GB/s</text><text x="103.0" y="43.8" font-size="12" text-anchor="start">실측 직선: 16.8 ms + 6.49 µs × ctx</text><text x="103.0" y="57.6" font-size="12" text-anchor="start">이상 직선: 9.0 ms + 0.77 µs × ctx → 기울기 ×8.4</text>
</svg>
```

그림 5 — decode ms/token vs context. 파란 점이 실측, 주황 점선이 실측에 맞춘 직선, 회색 점선이 이상 모델. 절편은 두 배, 기울기는 여덟 배 차이 난다. 모델이 "틀린" 것이 아니라 **이 구현이 KV를 여러 번 옮기고 있다**는 것을 모델이 알려 준다.

### 4.5 기울기 8배를 회계하기 — KV 1바이트가 몇 번 움직이나

HF의 기본 `DynamicCache` + 4.3절 패치 경로에서 decode step 한 번, 층 하나가 KV에 하는 일을 따라가 보자(fp32, SmolLM2 GQA 3:1).

| 단계 | 무엇을 하나 | KV 바이트 기준 트래픽 |
|---|---|---|
| cache update | `torch.cat([과거 K, 새 K])` — 새 텐서를 할당하고 전체 복사 | 읽기 1 + 쓰기 1 |
| repeat_kv | KV head 3개 → query head 9개로 복제해서 새 텐서 생성 | 읽기 1 + 쓰기 3 |
| attention | 복제된 K, V를 읽어 점수·가중합 | 읽기 3 |
| 합계 | | **약 9배** |

측정한 기울기 비율 7.6~8.4배와 잘 맞는다(일부 읽기는 캐시에 맞는다). 실제 온디바이스 런타임은 이렇게 하지 않는다.

- KV-cache를 **최대 context만큼 미리 할당**해 두고 write pointer만 올린다(cat 없음, 8절 C 코드).
- GQA-aware 커널이 KV head 하나를 읽어 query head 여러 개에 **레지스터에서** 재사용한다(복제 없음).
- 그러면 트래픽 배수는 1에 가까워진다. llama.cpp가 이 방식이다(F3).

이 회계가 성능 엔지니어링의 핵심 도구다. **"이상 모델 × 트래픽 배수 = 실측"**으로 설명이 되면 병목을 이해한 것이고, 배수를 줄이는 것이 최적화다. SSD 펌웨어에서 write amplification(호스트 1바이트 쓰기가 NAND에 몇 바이트 쓰기가 되나)을 회계하던 것과 정확히 같은 사고방식이다.

### 4.6 검증 결과 정리

| 구간 | 예측 대비 실측 | 지배 요인 | 모델에 반영하려면 |
|---|---|---|---|
| prefill 중간 S (256~2048, 패치) | ×2.0~2.2 | GEMM 실효 효율 약 50% | `eff_fl = 0.5` |
| prefill 짧은 S (64) | ×4~5 | 호출 오버헤드, 작은 GEMM | 고정 오버헤드 항 추가 |
| prefill 긴 S (HF 기본) | ×5.5 이상, S와 함께 증가 | 비융합 attention (S² 트래픽) | 커널을 고친다 (모델이 아니라) |
| decode 절편 | ×1.9 | GEMV 실효 BW 38 GB/s + 작은 op 오버헤드 | `eff_bw ≈ 0.6` + 고정 ms |
| decode 기울기 | ×8 | KV 복사·복제 (트래픽 배수 9) | 커널·캐시 구조를 고친다 |

결론: 해석 모델은 **하한**을 주고, 실측과의 비율이 **건강 지표**다. 비율이 일정하면 효율 문제, 비율이 입력 크기와 함께 커지면 **구조적인 낭비**(비융합 커널, 복사)다.

---

## 5. KV-cache 깊이 보기

### 5.1 성장 — token 하나에 몇 바이트씩

공식과 유도는 B4 5.3절, 모델별 값은 2.3절 출력에 있다. 여기서는 **운영 관점**의 숫자만 정리한다.

| 모델 | KV/token (fp16) | 대화 1턴 (약 100 token) | 4K context | 8K context |
|---|---|---|---|---|
| SmolLM2-135M | 22.5 KiB | 2.2 MiB | 90 MiB | 180 MiB |
| Qwen2.5-0.5B | 12 KiB | 1.2 MiB | 48 MiB | 96 MiB |
| Llama-3.2-1B | 32 KiB | 3.1 MiB | 128 MiB | 256 MiB |
| Llama-3.2-3B | 112 KiB | 10.9 MiB | 448 MiB | 896 MiB |

말로 하면: Llama-3.2-1B로 대화를 한 턴(질문+답 100 token) 할 때마다 KV-cache가 약 3 MiB씩 자란다. 40턴이면 4K context가 차고 128 MiB다.

**GQA가 decode를 돕는 방식**: GQA는 KV head 수를 줄여 (1) KV-cache **용량**을 줄이고, (2) decode step마다 읽는 **KV 바이트**를 같은 비율로 줄인다. Llama-3.2-1B에서 MHA(32 head) 대비 4배다. decode는 memory-bound이므로 KV 바이트 감소는 그대로 긴 context의 TPOT 감소가 된다. 연산량(QK, PV의 FLOP)은 query head 수 그대로라 줄지 않지만, decode에서 그건 병목이 아니다.

### 5.2 context가 길어지면 decode가 얼마나 느려지나

4.4절에서 SmolLM2 fp32로 이미 쟀다(구현 비효율 때문에 기울기 8배). 이상적인 구현이라면 얼마일까? Llama-3.2-1B int4, 웨어러블 17 GB/s, KV fp16으로 손계산:

```
ctx      0: 695.1 MB                      ÷ 17 GB/s = 40.9 ms  (24.5 tok/s)
ctx     8K: 695.1 + 268.4 = 963.5 MB      ÷ 17 GB/s = 56.7 ms  (17.6 tok/s)
ctx    32K: 695.1 + 1,073.7 = 1,768.8 MB  ÷ 17 GB/s = 104.0 ms ( 9.6 tok/s)
```

말로 하면: 이상적인 구현이라도 32K context에서는 decode가 2.5배 느려진다. 긴 대화 기록을 통째로 들고 다니는 설계는 **메모리뿐 아니라 속도도** 잃는다.

반대로 TTFT에 비해 TPOT가 context에 덜 민감한 이유도 여기서 보인다. S = 512일 때 KV는 16.8 MB로 가중치 695 MB의 2.4%에 불과하다. **TTFT는 프롬프트 길이에 정비례**하지만(모든 token을 처리해야 하므로) **TPOT는 "가중치 + 작은 KV"라 거의 일정**하다 — 면접 단골 질문의 답이다.

### 5.3 단편화와 PagedAttention

**문제**: 여러 sequence(예: 서버의 여러 사용자, 또는 기기에서 동시에 도는 여러 작업)의 KV-cache를 어떻게 할당할까? 가장 단순한 방법은 sequence마다 **최대 길이만큼 연속으로 예약**하는 것이다. 하지만 대부분의 sequence는 최대 길이보다 훨씬 짧게 끝나므로 예약만 되고 안 쓰이는 공간(내부 단편화)이 생긴다.

**PagedAttention**(Kwon et al., 2023, vLLM)의 아이디어: OS의 가상 메모리 페이징을 그대로 가져온다.

- KV-cache를 **고정 크기 block**(예: 16 token)으로 나눈다.
- sequence마다 **block table**(논리 block 번호 → 물리 block 번호)을 둔다.
- token이 block을 다 채우면 빈 물리 block 하나를 새로 배정한다. 물리적으로 연속일 필요가 없다.
- attention 커널은 block table을 따라가며 흩어진 block을 읽는다(gather).
- 덤: 같은 prefix(system prompt)를 쓰는 sequence끼리 물리 block을 **공유**할 수 있다(copy-on-write). 5.6절 prefix caching과 연결된다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 250">
<text x="20.0" y="24.0" font-size="13" text-anchor="start">(가) 연속 할당: sequence마다 최대 길이 예약</text><text x="20.0" y="57.0" font-size="12" text-anchor="start">seq A</text><rect x="70.0" y="40.0" width="28.0" height="24.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="100.0" y="40.0" width="28.0" height="24.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="130.0" y="40.0" width="28.0" height="24.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="160.0" y="40.0" width="28.0" height="24.0" fill="#888" fill-opacity="0.2"/><rect x="190.0" y="40.0" width="28.0" height="24.0" fill="#888" fill-opacity="0.2"/><rect x="220.0" y="40.0" width="28.0" height="24.0" fill="#888" fill-opacity="0.2"/><rect x="250.0" y="40.0" width="28.0" height="24.0" fill="#888" fill-opacity="0.2"/><rect x="280.0" y="40.0" width="28.0" height="24.0" fill="#888" fill-opacity="0.2"/><text x="20.0" y="91.0" font-size="12" text-anchor="start">seq B</text><rect x="70.0" y="74.0" width="28.0" height="24.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="100.0" y="74.0" width="28.0" height="24.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="130.0" y="74.0" width="28.0" height="24.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="160.0" y="74.0" width="28.0" height="24.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="190.0" y="74.0" width="28.0" height="24.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="220.0" y="74.0" width="28.0" height="24.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="250.0" y="74.0" width="28.0" height="24.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="280.0" y="74.0" width="28.0" height="24.0" fill="#888" fill-opacity="0.2"/><text x="20.0" y="125.0" font-size="12" text-anchor="start">seq C</text><rect x="70.0" y="108.0" width="28.0" height="24.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="100.0" y="108.0" width="28.0" height="24.0" fill="#4a7bd0" fill-opacity="0.7"/><rect x="130.0" y="108.0" width="28.0" height="24.0" fill="#888" fill-opacity="0.2"/><rect x="160.0" y="108.0" width="28.0" height="24.0" fill="#888" fill-opacity="0.2"/><rect x="190.0" y="108.0" width="28.0" height="24.0" fill="#888" fill-opacity="0.2"/><rect x="220.0" y="108.0" width="28.0" height="24.0" fill="#888" fill-opacity="0.2"/><rect x="250.0" y="108.0" width="28.0" height="24.0" fill="#888" fill-opacity="0.2"/><rect x="280.0" y="108.0" width="28.0" height="24.0" fill="#888" fill-opacity="0.2"/><text x="70.0" y="158.0" font-size="12" text-anchor="start">파랑: 쓰는 KV, 회색: 예약만 된 빈칸 (내부 단편화)</text><text x="360.0" y="24.0" font-size="13" text-anchor="start">(나) paged: 고정 크기 block + block table</text><text x="360.0" y="57.0" font-size="12" text-anchor="start">seq A 표</text><rect x="430" y="40" width="30" height="24" fill="none" stroke="currentColor"/><text x="445.0" y="57.0" font-size="12" text-anchor="middle">5</text><rect x="464" y="40" width="30" height="24" fill="none" stroke="currentColor"/><text x="479.0" y="57.0" font-size="12" text-anchor="middle">1</text><text x="360.0" y="91.0" font-size="12" text-anchor="start">seq B 표</text><rect x="430" y="74" width="30" height="24" fill="none" stroke="currentColor"/><text x="445.0" y="91.0" font-size="12" text-anchor="middle">0</text><rect x="464" y="74" width="30" height="24" fill="none" stroke="currentColor"/><text x="479.0" y="91.0" font-size="12" text-anchor="middle">6</text><rect x="498" y="74" width="30" height="24" fill="none" stroke="currentColor"/><text x="513.0" y="91.0" font-size="12" text-anchor="middle">3</text><text x="360.0" y="125.0" font-size="12" text-anchor="start">seq C 표</text><rect x="430" y="108" width="30" height="24" fill="none" stroke="currentColor"/><text x="445.0" y="125.0" font-size="12" text-anchor="middle">7</text><text x="360.0" y="158.0" font-size="12" text-anchor="start">물리 block pool (block = 16 token)</text><rect x="360.0" y="170.0" width="34.0" height="26.0" fill="#e08a3c" fill-opacity="0.7"/><text x="377.0" y="188.0" font-size="12" text-anchor="middle">0</text><text x="377.0" y="212.0" font-size="12" text-anchor="middle">B</text><rect x="398.0" y="170.0" width="34.0" height="26.0" fill="#4a7bd0" fill-opacity="0.7"/><text x="415.0" y="188.0" font-size="12" text-anchor="middle">1</text><text x="415.0" y="212.0" font-size="12" text-anchor="middle">A</text><rect x="436.0" y="170.0" width="34.0" height="26.0" fill="#888" fill-opacity="0.2"/><text x="453.0" y="188.0" font-size="12" text-anchor="middle">2</text><rect x="474.0" y="170.0" width="34.0" height="26.0" fill="#e08a3c" fill-opacity="0.7"/><text x="491.0" y="188.0" font-size="12" text-anchor="middle">3</text><text x="491.0" y="212.0" font-size="12" text-anchor="middle">B</text><rect x="512.0" y="170.0" width="34.0" height="26.0" fill="#888" fill-opacity="0.2"/><text x="529.0" y="188.0" font-size="12" text-anchor="middle">4</text><rect x="550.0" y="170.0" width="34.0" height="26.0" fill="#4a7bd0" fill-opacity="0.7"/><text x="567.0" y="188.0" font-size="12" text-anchor="middle">5</text><text x="567.0" y="212.0" font-size="12" text-anchor="middle">A</text><rect x="588.0" y="170.0" width="34.0" height="26.0" fill="#e08a3c" fill-opacity="0.7"/><text x="605.0" y="188.0" font-size="12" text-anchor="middle">6</text><text x="605.0" y="212.0" font-size="12" text-anchor="middle">B</text><rect x="626.0" y="170.0" width="34.0" height="26.0" fill="#3f9a6b" fill-opacity="0.7"/><text x="643.0" y="188.0" font-size="12" text-anchor="middle">7</text><text x="643.0" y="212.0" font-size="12" text-anchor="middle">C</text><text x="360.0" y="236.0" font-size="12" text-anchor="start">빈 block(2, 4)은 어느 sequence에나 바로 배정</text><text x="20.0" y="236.0" font-size="12" text-anchor="start">낭비 = 마지막 block의 빈칸뿐 (평균 block/2 token)</text>
</svg>
```

그림 6 — (가) 연속 할당은 sequence마다 최대 길이를 예약해서 회색 빈칸이 낭비된다. (나) paged 할당은 고정 크기 block을 필요할 때마다 배정하고, block table이 논리 순서를 기억한다. 낭비는 각 sequence의 마지막 block 빈칸뿐이다.

무엇을 확인하나: 길이 분포가 치우친(짧은 대화가 대부분이고 가끔 긴) sequence 10,000개에 대해 연속 예약과 paged 할당의 메모리 사용률을 비교한다. KV/token은 Llama-3.2-1B fp16(32 KiB), 풀 크기 256 MiB는 가정이다.

```python
import numpy as np
rng = np.random.default_rng(0)
KV_TOK, POOL, MAXLEN = 32 * 1024, 256 * 2**20, 4096     # Llama-3.2-1B fp16 KV, 256 MiB 풀(가정)
lens = np.minimum(rng.lognormal(mean=6.3, sigma=0.8, size=10_000).astype(int) + 1, MAXLEN)
print(f"sequence 길이: 중앙값 {np.median(lens):.0f}, 평균 {lens.mean():.0f}, 최대 {lens.max()} token")
used = lens.mean() * KV_TOK                              # 실제로 필요한 평균 바이트
for name, reserve in [("연속, 최대 길이 예약", np.full_like(lens, MAXLEN)),
                      ("paged, block 16", np.ceil(lens / 16) * 16),
                      ("paged, block 128", np.ceil(lens / 128) * 128)]:
    r = reserve.mean() * KV_TOK
    print(f"{name:18s}: 평균 예약 {r/2**20:6.1f} MiB/seq, 사용률 {used/r:5.1%}, "
          f"256 MiB에 동시 {int(POOL // r)}개")
```

```text
sequence 길이: 중앙값 541, 평균 745, 최대 4096 token
연속, 최대 길이 예약      : 평균 예약  128.0 MiB/seq, 사용률 18.2%, 256 MiB에 동시 2개
paged, block 16   : 평균 예약   23.5 MiB/seq, 사용률 99.0%, 256 MiB에 동시 10개
paged, block 128  : 평균 예약   25.2 MiB/seq, 사용률 92.3%, 256 MiB에 동시 10개
```

출력에서 볼 것:

- 최대 길이(4096) 연속 예약은 사용률 18%. 256 MiB에 sequence 2개밖에 못 넣는다.
- block 16 paged는 사용률 99%, 평균 기준 약 10개. block을 128로 키워도 92%다. block이 크면 테이블이 작고 gather가 단순해지지만 낭비가 는다 — 페이지 크기 선택과 같은 trade-off다.

**기기에서는?** batch = 1 단일 대화라면 PagedAttention의 이득은 작다. 대신 기기에서 의미 있는 경우는: 여러 기능(대화, 요약, 알림 분류)이 **KV 풀 하나를 나눠 쓸 때**, 여러 system prompt의 prefix KV를 **동시에 캐시해 둘 때**, 그리고 메모리 압박 시 오래된 block만 **골라서 버리거나 flash로 내릴 때**다. 펌웨어로 치면 SSD의 FTL이 논리 페이지를 물리 페이지에 매핑하는 것, 또는 DMA scatter-gather 리스트와 같은 구조다. Don에게는 새로운 개념이 아니라 이름만 새롭다.

### 5.4 sliding window와 attention sink

**sliding window attention**: 층이 최근 W개 token만 보게 하면 그 층의 KV-cache는 W칸에서 더 자라지 않는다. 구현은 말 그대로 **링버퍼**다(8절 C 코드의 `ring_slot`). 대가는 W보다 먼 token을 그 층이 직접 못 본다는 것이다. 그래서 최근 모델들은 **local(sliding) 층과 global(전체) 층을 섞는다**.

무엇을 확인하나: Gemma 3 1B의 config에서 층 종류와 window를 읽어, 섞은 구조의 KV 크기를 "전 층 global"과 비교한다.

```python
import warnings; warnings.filterwarnings("ignore")
from collections import Counter
from transformers import AutoConfig
c = AutoConfig.from_pretrained("unsloth/gemma-3-1b-it", local_files_only=True)
types, W = c.layer_types, c.sliding_window
per_layer_tok = 2 * c.num_key_value_heads * c.head_dim * 2           # 층 하나, token 하나, fp16
print(f"layers={c.num_hidden_layers} kv_heads={c.num_key_value_heads} head_dim={c.head_dim} "
      f"window={W} 구성={dict(Counter(types))}")
for ctx in (512, 2048, 8192, 32768):
    full = len(types) * per_layer_tok * ctx                          # 모든 층이 전체 context를 본다면
    mixed = sum(per_layer_tok * (min(ctx, W) if t == "sliding_attention" else ctx) for t in types)
    print(f"ctx {ctx:6d}: 전 층 global {full/2**20:6.1f} MiB | 실제(local+global) {mixed/2**20:6.1f} MiB"
          f" ({mixed/full:4.0%})")
```

```text
layers=26 kv_heads=1 head_dim=256 window=512 구성={'sliding_attention': 22, 'full_attention': 4}
ctx    512: 전 층 global   13.0 MiB | 실제(local+global)   13.0 MiB (100%)
ctx   2048: 전 층 global   52.0 MiB | 실제(local+global)   19.0 MiB ( 37%)
ctx   8192: 전 층 global  208.0 MiB | 실제(local+global)   43.0 MiB ( 21%)
ctx  32768: 전 층 global  832.0 MiB | 실제(local+global)  139.0 MiB ( 17%)
```

출력에서 볼 것:

- config상 26층 중 22층이 window 512의 sliding, 4층만 global이다. KV head는 1개(MQA), head_dim 256.
- 8K context에서 KV가 208 MiB → 43 MiB(21%), 32K에서 17%. **긴 context에서 KV를 줄이는 구조적 방법**이다. global 층 4개 몫은 context에 따라 계속 자라므로 0이 되지는 않는다.
- 이 구조는 모델이 **학습될 때부터** 그렇게 설계된 것이다. 이미 학습된 전체-attention 모델에 추론 때만 window를 씌우면 품질이 떨어질 수 있다.

**attention sink / StreamingLLM**(Xiao et al., 2023): 학습된 전체-attention 모델에 단순히 window만 씌우면, 처음 몇 token이 window 밖으로 밀려나는 순간 출력이 무너지는 현상이 보고되었다. 논문의 설명은 모델이 attention 확률의 "남는 몫"을 **맨 앞 몇 token에 몰아 두는 습관**(sink)이 있다는 것이다. 그래서 맨 앞 몇 token(논문에서는 4개 정도)의 KV를 영구히 보존하고 나머지는 최근 window만 유지하면 긴 스트림에서도 안정적으로 생성할 수 있다고 보고했다. 주의할 점: 이 방법은 **먼 과거의 내용을 기억하게 해 주지는 않는다**. 무한히 "말을 이어갈 수" 있을 뿐이다. 대화 기록 요약 같은 제품 기능과는 다른 문제다(L2).

펌웨어 비유: attention sink는 링버퍼를 돌리면서 **헤더 슬롯 몇 개만 pinned**로 남겨 두는 것이다. 로그 버퍼에서 부팅 로그 앞부분은 보존하고 나머지는 순환시키는 설계와 비슷하다.

### 5.5 KV 양자화가 대역폭에 주는 효과

KV를 fp16 → int8 → int4로 줄이는 방법과 정확도는 C3 9절, 가중치+KV 조합별 tok/s는 C3 10.2절에서 계산했다. 여기서는 성능 모델 관점의 요점만:

- KV 양자화는 식의 `KV/token × t` 항만 줄인다. 그래서 **context가 짧으면 효과가 작고, 길수록 크다**. C3 10.2절: Llama-3.2-1B int4, 웨어러블, 4K에서 KV fp16 → int4는 20.6 → 23.4 tok/s(+14%), 16K에서는 KV가 token당 바이트의 44%라 효과가 훨씬 크다.
- 교차점(2.2절)을 기준으로 생각하면 쉽다: **교차점보다 긴 context를 쓰는 제품이면 KV 양자화가 가중치 양자화만큼 중요**하다.
- 구현 조건: dequant가 attention 커널 안에서 on-the-fly로 일어나야 한다. KV를 풀어서 DRAM에 다시 쓰면 4.5절처럼 트래픽 배수가 늘어 이득이 사라진다.

### 5.6 prefix caching — system prompt의 KV를 재사용

음성 비서의 프롬프트는 대개 **매번 같은 긴 system prompt**(제품 규칙, 말투, 도구 설명) + 짧은 사용자 발화다. 매 턴마다 system prompt를 다시 prefill하는 것은 낭비다. KV-cache는 "앞부분 token이 같으면 앞부분 KV도 같다"(causal attention이므로 뒤 token이 앞 token의 K, V에 영향을 주지 않는다). 그래서 system prompt의 KV를 한 번 계산해 두고 매 턴 복사해서 쓰면, prefill은 **새로 붙는 부분만** 하면 된다.

무엇을 확인하나: 318 token짜리 system prompt + 18 token 질문에서 TTFT를 (1) 전체 prefill, (2) 캐시된 prefix KV + 나머지 18 token prefill로 비교하고, 두 방식의 logits가 같은지 확인한다.

```python
import copy, time, statistics, torch
import transformers.integrations.sdpa_attention as sa
from transformers import AutoTokenizer, AutoModelForCausalLM
mid = "HuggingFaceTB/SmolLM2-135M-Instruct"
tok = AutoTokenizer.from_pretrained(mid, local_files_only=True)
model = AutoModelForCausalLM.from_pretrained(mid, dtype=torch.float32, local_files_only=True).eval()
sa.use_gqa_in_sdpa = lambda *a, **k: False                          # 4.3절의 패치
def med(f, n=9):
    f(); ts = []                                                    # warm-up 1회 후 중앙값
    for _ in range(n):
        t = time.perf_counter(); f(); ts.append(time.perf_counter() - t)
    return statistics.median(ts)
system = ("You are a voice assistant on a small wearable device. Answer in one or two short "
          "sentences. Never use lists or markdown. " * 12)          # 제품 규칙이 쌓인 긴 system prompt
sys_msg = [{"role": "system", "content": system}]
user = [{"role": "user", "content": "What's the weather like for a run?"}]
full = tok.apply_chat_template(sys_msg + user, add_generation_prompt=True, return_tensors="pt")
pre = tok.apply_chat_template(sys_msg, return_tensors="pt")
P = pre.shape[1]; assert torch.equal(full[:, :P], pre)             # 앞부분 token이 정확히 같아야 재사용 가능
print(f"prompt {full.shape[1]} tok = system prefix {P} + 나머지 {full.shape[1] - P}")
with torch.no_grad():
    cache = model(pre).past_key_values                              # 부팅 때 1번만: prefix의 KV 계산
    no_cache = lambda: model(full, logits_to_keep=1).logits
    with_cache = lambda: model(full[:, P:], past_key_values=copy.deepcopy(cache),  # 원본 보존
                               logits_to_keep=1).logits
    t0, t1, tc = med(no_cache), med(with_cache), med(lambda: copy.deepcopy(cache))
    diff = (no_cache() - with_cache()).abs().max().item()
print(f"TTFT 캐시 없음 {t0*1e3:6.1f} ms | prefix 캐시 {t1*1e3:5.1f} ms (그중 KV 복사 {tc*1e3:.1f} ms)"
      f" | logits 최대 차이 {diff:.1e}")
print(f"prefix KV: {P} tok × {2*30*3*64*4} B = {P*2*30*3*64*4/2**20:.1f} MiB (fp32)")
```

```text
prompt 336 tok = system prefix 318 + 나머지 18
TTFT 캐시 없음  131.2 ms | prefix 캐시  38.7 ms (그중 KV 복사 2.6 ms) | logits 최대 차이 5.1e-05
prefix KV: 318 tok × 46080 B = 14.0 MiB (fp32)
```

출력에서 볼 것:

- **TTFT 131 → 39 ms (3.4배 단축)**. 남은 39 ms는 18 token prefill + 캐시 복사 2.6 ms + 고정 오버헤드다(4.2절에서 본 대로 짧은 prefill도 약 40~60 ms의 바닥이 있다).
- **logits 최대 차이 5e-05**: fp32 연산 순서 차이 수준으로 같은 결과다. 정확도 손해가 없는 최적화다.
- `assert`가 중요하다: 앞부분 token id가 **한 개라도 다르면** 재사용할 수 없다. system prompt에 날짜·시간 같은 변하는 값을 앞에 넣으면 캐시가 매번 깨진다. 변하는 값은 뒤쪽에 둔다.
- 대가: prefix KV 14 MiB(fp32 기준, fp16이면 7 MiB)를 상주시켜야 한다. 기기의 DRAM 예산에 들어가야 한다.

실무 연결: 웨어러블이라면 부팅 시(또는 system prompt 업데이트 시) prefix KV를 계산해서 DRAM에 두거나, 아예 **flash에 저장해 두고 부팅 때 DMA로 올리는** 설계도 가능하다. 계산 비용을 저장 공간으로 바꾸는 전형적인 펌웨어 트레이드오프다. 여러 기능의 system prompt를 각각 캐시하려면 5.3절의 paged 풀이 유용해진다.

---

## 6. batching과 speculative decoding — roofline 위에서

### 6.1 batching: decode를 compute 쪽으로 밀기

decode가 memory-bound인 이유는 가중치 1바이트로 곱셈을 한두 번만 하기 때문이다. **여러 sequence를 한 번에 decode**(batch B)하면 같은 가중치를 B번 재사용하니 arithmetic intensity가 B배 가까이 오른다. 서버가 수십~수백 사용자를 묶어 처리하는 이유다. 단, **KV는 sequence마다 따로**라 재사용이 안 된다.

```
FLOP_step = 2 × Pm × B
bytes_step = 가중치 (공유, 1번) + B × KV/token × ctx   (KV는 공유 불가)
intensity = FLOP / bytes → B가 커져도 KV 항 때문에 상한이 있다
```

무엇을 확인하나: (1) 계산기로 Llama-3.2-1B int4, 폰 가정에서 batch별 intensity와 총 tok/s, (2) 이 Mac에서 SmolLM2-135M의 batch별 step 시간 실측.

```python
import time, statistics, torch
import transformers.integrations.sdpa_attention as sa
from transformers import AutoModelForCausalLM
from llm_perf import shape
sa.use_gqa_in_sdpa = lambda *a, **k: False
s = shape("unsloth/Llama-3.2-1B")                        # (1) 계산: Llama-3.2-1B int4, phone 가정
peak, bw, W, kv_tok, ctx = 10e12, 60e9, s["Pm"] * 4.5 / 8, 32 * 1024, 512
print("B    FLOP/byte  step ms  총 tok/s   (Llama-1B int4, ctx 512, 10 TFLOP/s·60 GB/s 가정)")
for B in (1, 4, 16, 64, 256):
    fl, by = 2 * s["Pm"] * B, W + B * kv_tok * ctx       # 가중치는 공유, KV는 sequence마다 따로
    t = max(fl / peak, by / bw)
    print(f"{B:<5d}{fl/by:9.1f}{t*1e3:9.2f}{B/t:10.0f}   {'compute' if fl/peak > by/bw else 'memory'}")
model = AutoModelForCausalLM.from_pretrained("HuggingFaceTB/SmolLM2-135M-Instruct",
                                             dtype=torch.float32, local_files_only=True).eval()
print("\n(2) 실측: SmolLM2-135M fp32, context 128, 이 Mac")
with torch.no_grad():
    for B in (1, 2, 4, 8, 16):
        ids = torch.randint(100, 40000, (B, 160), generator=torch.Generator().manual_seed(B))
        past, ts = model(ids[:, :128]).past_key_values, []
        for i in range(15):
            t = time.perf_counter()
            past = model(ids[:, 128 + i:129 + i], past_key_values=past).past_key_values
            ts.append(time.perf_counter() - t)
        t = statistics.median(ts[2:])
        base = t if B == 1 else base
        print(f"B={B:2d}: step {t*1e3:5.1f} ms (B=1의 ×{t/base:3.1f}), 총 {B/t:4.0f} tok/s")
```

```text
B    FLOP/byte  step ms  총 tok/s   (Llama-1B int4, ctx 512, 10 TFLOP/s·60 GB/s 가정)
1          3.5    11.87        84   memory
4         13.0    12.70       315   memory
16        41.0    16.06       996   memory
64        89.4    29.48      2171   memory
256      126.8    83.17      3078   memory

(2) 실측: SmolLM2-135M fp32, context 128, 이 Mac
B= 1: step  18.2 ms (B=1의 ×1.0), 총   55 tok/s
B= 2: step  36.0 ms (B=1의 ×2.0), 총   56 tok/s
B= 4: step  38.3 ms (B=1의 ×2.1), 총  104 tok/s
B= 8: step  41.6 ms (B=1의 ×2.3), 총  193 tok/s
B=16: step  49.5 ms (B=1의 ×2.7), 총  323 tok/s
```

출력에서 볼 것:

- (1) B = 1 → 16에서 step 시간은 11.9 → 16.1 ms로 35%만 늘지만 총 처리량은 84 → 996 tok/s로 12배. **B = 256이 되어도 intensity가 127로 ridge(167)에 못 미친다**: ctx 512의 KV 16.8 MB가 sequence마다 붙기 때문이다. 긴 context 서빙에서 KV가 batch 이득을 갉아먹는 구조가 계산으로 보인다.
- (2) 실측도 같은 추세다: B = 16에서 step 시간은 2.7배지만 총 처리량은 55 → 323 tok/s(5.9배). 흥미로운 것은 **B = 2에서 step이 두 배**로 뛰는 점이다. 이론과 다르다. 아래 예제로 원인을 확인한다.

무엇을 확인하나: 가중치 113 MB(캐시보다 큼)에 행 M개를 곱하는 시간이 M = 1과 M ≥ 2에서 다른지.

```python
import time, statistics, torch
W = torch.randn(49152, 576)                       # SmolLM2 lm_head 크기, 113 MB (캐시보다 큼)
for M in (1, 2, 4, 8, 16):                        # M = 한 번에 곱하는 token(=batch) 수
    x = torch.randn(M, 576); x @ W.T; ts = []
    for _ in range(20):
        t = time.perf_counter(); x @ W.T; ts.append(time.perf_counter() - t)
    t = statistics.median(ts)
    print(f"M={M:2d}: {t*1e3:5.2f} ms, 가중치 읽기 {W.numel()*4/t/1e9:5.1f} GB/s")
```

```text
M= 1:  2.12 ms, 가중치 읽기  53.5 GB/s
M= 2:  5.03 ms, 가중치 읽기  22.5 GB/s
M= 4:  4.96 ms, 가중치 읽기  22.8 GB/s
M= 8:  4.79 ms, 가중치 읽기  23.6 GB/s
M=16:  5.40 ms, 가중치 읽기  21.0 GB/s
```

출력에서 볼 것: M = 1은 53.5 GB/s(STREAM 64 GB/s에 가깝다)인데, M = 2가 되는 순간 22.5 GB/s로 떨어지고 M = 16까지 거의 그대로다. M = 1은 GEMV 전용 경로, M ≥ 2는 GEMM 경로를 타는 것으로 보이고, 이 라이브러리의 작은-M GEMM은 대역폭을 잘 못 쓴다(추정 — 라이브러리 내부는 확인하지 않았다). 교훈은 4.3절과 같다: **"batch 2면 공짜"라는 이론은 커널이 그 모양을 잘 지원할 때만 성립한다.** 기기 런타임을 고를 때 batch 1 GEMV와 작은 M GEMM 둘 다 벤치마크해야 한다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 320">
<line x1="80.0" y1="270.0" x2="560.0" y2="270.0" stroke="currentColor" stroke-width="1"/><line x1="80.0" y1="270.0" x2="80.0" y2="30.0" stroke="currentColor" stroke-width="1"/><line x1="80.0" y1="270.0" x2="80.0" y2="275.0" stroke="currentColor" stroke-width="1"/><text x="80.0" y="289.0" font-size="12" text-anchor="middle">1</text><line x1="200.0" y1="270.0" x2="200.0" y2="275.0" stroke="currentColor" stroke-width="1"/><text x="200.0" y="289.0" font-size="12" text-anchor="middle">10</text><line x1="320.0" y1="270.0" x2="320.0" y2="275.0" stroke="currentColor" stroke-width="1"/><text x="320.0" y="289.0" font-size="12" text-anchor="middle">100</text><line x1="440.0" y1="270.0" x2="440.0" y2="275.0" stroke="currentColor" stroke-width="1"/><text x="440.0" y="289.0" font-size="12" text-anchor="middle">1000</text><line x1="560.0" y1="270.0" x2="560.0" y2="275.0" stroke="currentColor" stroke-width="1"/><text x="560.0" y="289.0" font-size="12" text-anchor="middle">10000</text><line x1="75.0" y1="270.0" x2="80.0" y2="270.0" stroke="currentColor" stroke-width="1"/><text x="72.0" y="274.0" font-size="12" text-anchor="end">10</text><line x1="75.0" y1="210.0" x2="80.0" y2="210.0" stroke="currentColor" stroke-width="1"/><text x="72.0" y="214.0" font-size="12" text-anchor="end">100</text><line x1="75.0" y1="150.0" x2="80.0" y2="150.0" stroke="currentColor" stroke-width="1"/><text x="72.0" y="154.0" font-size="12" text-anchor="end">1000</text><line x1="75.0" y1="90.0" x2="80.0" y2="90.0" stroke="currentColor" stroke-width="1"/><text x="72.0" y="94.0" font-size="12" text-anchor="end">10000</text><line x1="75.0" y1="30.0" x2="80.0" y2="30.0" stroke="currentColor" stroke-width="1"/><text x="72.0" y="34.0" font-size="12" text-anchor="end">100000</text><polyline points="80.0,223.3 346.6,90.0 560.0,90.0" fill="none" stroke="currentColor" stroke-width="2"/><text x="354.6" y="80.0" font-size="12" text-anchor="start">ridge 167 FLOP/B</text><text x="320.0" y="308.0" font-size="12" text-anchor="middle">arithmetic intensity (FLOP/byte, log) — 폰 가정 10 TFLOP/s, 60 GB/s</text><text x="20.0" y="150.0" font-size="12" text-anchor="middle" transform="rotate(-90 20 150)">GFLOP/s (log)</text><circle cx="144.9" cy="190.9" r="5" fill="#e08a3c"/><text x="152.9" y="208.9" font-size="12" text-anchor="start">B=1</text><circle cx="213.6" cy="156.5" r="5" fill="#e08a3c"/><text x="221.6" y="174.5" font-size="12" text-anchor="start">B=4</text><circle cx="273.6" cy="126.5" r="5" fill="#e08a3c"/><text x="281.6" y="144.5" font-size="12" text-anchor="start">B=16</text><circle cx="314.2" cy="106.2" r="5" fill="#e08a3c"/><text x="306.2" y="98.2" font-size="12" text-anchor="end">B=64</text><circle cx="332.4" cy="97.1" r="5" fill="#e08a3c"/><text x="324.4" y="75.1" font-size="12" text-anchor="end">B=256</text><circle cx="458.4" cy="90.0" r="6" fill="#4a7bd0"/><text x="458.4" y="110.0" font-size="12" text-anchor="middle">prefill S=512 (1425)</text><text x="89.5" y="113.9" font-size="12" text-anchor="start">주황: decode batch B (ctx 512)</text><text x="89.5" y="131.9" font-size="12" text-anchor="start">파랑: prefill</text>
</svg>
```

그림 7 — Llama-3.2-1B int4의 roofline(폰 가정 10 TFLOP/s, 60 GB/s, ridge 167 FLOP/byte). 주황 점은 decode batch B(ctx 512), 파란 점은 prefill S = 512. decode B = 1은 intensity 3.5로 지붕에서 한참 아래, peak의 약 2%만 쓴다. batch를 키우면 경사면을 따라 오르지만 KV 때문에 ridge에 닿지 못한다. prefill은 1,425 FLOP/byte로 지붕 위다. roofline 읽는 법은 D3.

**왜 기기에서는 batch = 1인가**:

- 사용자가 한 명이다. 기다렸다 묶을 다른 요청이 없다.
- 지연이 우선이다. batch를 만들려고 기다리면 TTFT가 는다.
- 그래서 온디바이스 decode는 구조적으로 memory-bound이고, 속도를 올리는 손잡이는 **바이트를 줄이기**(양자화, GQA, KV 양자화, 작은 모델)와 **한 step에 token을 여러 개 얻기**(speculative decoding)다.

### 6.2 speculative decoding — 한 번 읽고 여러 token

**아이디어**(Leviathan et al., 2023; Chen et al., 2023): 작은 **draft** 모델이 γ개 token을 먼저 빨리 추측한다. 큰 **target** 모델은 그 γ개를 **한 번의 forward로 동시에 검증**한다. 검증은 prefill처럼 여러 token을 한꺼번에 처리하는 것이라, memory-bound 상태에서는 **1 token decode와 거의 같은 시간**이 든다(가중치를 한 번 읽어 γ + 1개 위치에 재사용). draft가 맞힌 앞부분까지 받아들이고, 처음 틀린 위치에서는 target의 token으로 바꾼다. 수락 규칙을 제대로 쓰면(rejection sampling) **출력 분포가 target 단독과 정확히 같다**. 품질 손해가 없는 가속이다.

각 draft token이 독립적으로 확률 α로 수락된다고 단순화하면, 검증 한 번에 얻는 token 수의 기댓값은:

```
E[token / 검증] = 1 + α + α² + … + α^γ = (1 − α^(γ+1)) / (1 − α)
```

말로 하면: 첫 draft가 맞을 확률 α, 두 번째까지 맞을 확률 α², … 이고, 맨 끝에 target이 항상 1 token을 보태 준다.

한 사이클의 시간은 draft γ step + target 검증 1 step이다. draft 한 step 시간이 target의 c배라면:

```
속도 향상 ≈ E[token / 검증] ÷ (γ × c + 1)
```

손으로 계산 — α = 0.8, γ = 4, c = 0.38:

```
E = (1 − 0.8⁵) / (1 − 0.8) = (1 − 0.32768) / 0.2 = 3.36 token
속도 향상 = 3.36 / (4 × 0.38 + 1) = 3.36 / 2.52 = 1.33배
```

같은 α, γ에서 draft가 훨씬 작아 c = 0.05라면 `3.36 / 1.2 = 2.8배`다. **draft는 작고(c 작게) 잘 맞아야(α 크게)** 한다.

무엇을 확인하나: 기댓값 공식을 Monte Carlo 시뮬레이션과 비교하고, Llama-3.2-1B(draft) → Llama-3.2-3B(target), 둘 다 int4, 웨어러블 가정에서 속도 향상을 계산한다. 두 모델은 같은 tokenizer(vocab 128,256)를 써서 draft-target 짝이 될 수 있다.

```python
import numpy as np
from llm_perf import shape, llm_perf
rng = np.random.default_rng(0)
def expected_tokens(a, g):            # 한 번 검증에서 얻는 token 수의 기댓값
    return (1 - a ** (g + 1)) / (1 - a)
def simulate(a, g, n=100_000):        # draft g개를 앞에서부터 받아들이다 첫 거절에서 멈춤 (+1 token은 target이 줌)
    acc = rng.random((n, g)) < a
    return (np.cumprod(acc, axis=1).sum(axis=1) + 1).mean()
wear = (2e12, 17e9)                                           # 웨어러블 가정
tgt, drf = shape("unsloth/Llama-3.2-3B"), shape("unsloth/Llama-3.2-1B")   # 같은 tokenizer
T_t = llm_perf(tgt, 4.5, 16, wear, 256, 1)["tpot"]
T_d = llm_perf(drf, 4.5, 16, wear, 256, 1)["tpot"]
c = T_d / T_t
print(f"target 3B int4 {T_t*1e3:.0f} ms/token, draft 1B int4 {T_d*1e3:.0f} ms/token → c = {c:.2f}")
for a in (0.5, 0.7, 0.8, 0.9):
    row = []
    for g in (2, 4, 6):
        E = expected_tokens(a, g)
        row.append(f"g={g}: E={E:4.2f} (sim {simulate(a, g):4.2f}) ×{E / (g * c + 1):4.2f}")
    print(f"α={a}: " + " | ".join(row))
```

```text
target 3B int4 108 ms/token, draft 1B int4 41 ms/token → c = 0.38
α=0.5: g=2: E=1.75 (sim 1.75) ×0.99 | g=4: E=1.94 (sim 1.93) ×0.77 | g=6: E=1.98 (sim 1.98) ×0.60
α=0.7: g=2: E=2.19 (sim 2.19) ×1.24 | g=4: E=2.77 (sim 2.78) ×1.10 | g=6: E=3.06 (sim 3.07) ×0.93
α=0.8: g=2: E=2.44 (sim 2.44) ×1.38 | g=4: E=3.36 (sim 3.36) ×1.33 | g=6: E=3.95 (sim 3.96) ×1.20
α=0.9: g=2: E=2.71 (sim 2.71) ×1.53 | g=4: E=4.10 (sim 4.09) ×1.62 | g=6: E=5.22 (sim 5.21) ×1.58
```

출력에서 볼 것:

- 시뮬레이션(sim)이 공식(E)과 소수 둘째 자리까지 맞는다. 공식이 맞다.
- c = 0.38(1B가 3B의 38% 시간)은 draft치고 **비싸다**. α = 0.5면 오히려 느려지고(×0.60~0.99), α = 0.9에서도 최대 ×1.6이다.
- γ를 키운다고 좋은 게 아니다: α = 0.7에서 γ = 2가 ×1.24로 가장 좋고 γ = 6은 ×0.93으로 손해다. 뒤쪽 draft는 앞쪽이 다 맞아야 쓸모가 있어서 기여가 기하급수로 준다.
- 가정: 검증 step(γ + 1 = 7 token)이 1 token decode와 같은 시간이라고 두었다. 3B int4 웨어러블에서 7 token의 연산은 `2 × 3.21 G × 7 ÷ 2 TFLOP/s ≈ 22 ms`로 메모리 시간(108 ms)보다 작으니 성립한다. NPU가 약하거나 γ가 크면 이 가정이 깨진다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 620 310">
<line x1="70.0" y1="260.0" x2="520.0" y2="260.0" stroke="currentColor" stroke-width="1"/><line x1="70.0" y1="260.0" x2="70.0" y2="30.0" stroke="currentColor" stroke-width="1"/><line x1="70.0" y1="260.0" x2="70.0" y2="265.0" stroke="currentColor" stroke-width="1"/><text x="70.0" y="279.0" font-size="12" text-anchor="middle">0.3</text><line x1="139.2" y1="260.0" x2="139.2" y2="265.0" stroke="currentColor" stroke-width="1"/><text x="139.2" y="279.0" font-size="12" text-anchor="middle">0.4</text><line x1="208.5" y1="260.0" x2="208.5" y2="265.0" stroke="currentColor" stroke-width="1"/><text x="208.5" y="279.0" font-size="12" text-anchor="middle">0.5</text><line x1="277.7" y1="260.0" x2="277.7" y2="265.0" stroke="currentColor" stroke-width="1"/><text x="277.7" y="279.0" font-size="12" text-anchor="middle">0.6</text><line x1="346.9" y1="260.0" x2="346.9" y2="265.0" stroke="currentColor" stroke-width="1"/><text x="346.9" y="279.0" font-size="12" text-anchor="middle">0.7</text><line x1="416.2" y1="260.0" x2="416.2" y2="265.0" stroke="currentColor" stroke-width="1"/><text x="416.2" y="279.0" font-size="12" text-anchor="middle">0.8</text><line x1="485.4" y1="260.0" x2="485.4" y2="265.0" stroke="currentColor" stroke-width="1"/><text x="485.4" y="279.0" font-size="12" text-anchor="middle">0.9</text><line x1="65.0" y1="260.0" x2="70.0" y2="260.0" stroke="currentColor" stroke-width="1"/><text x="62.0" y="264.0" font-size="12" text-anchor="end">0.5</text><line x1="65.0" y1="227.1" x2="70.0" y2="227.1" stroke="currentColor" stroke-width="1"/><text x="62.0" y="231.1" font-size="12" text-anchor="end">1.0</text><line x1="65.0" y1="194.3" x2="70.0" y2="194.3" stroke="currentColor" stroke-width="1"/><text x="62.0" y="198.3" font-size="12" text-anchor="end">1.5</text><line x1="65.0" y1="161.4" x2="70.0" y2="161.4" stroke="currentColor" stroke-width="1"/><text x="62.0" y="165.4" font-size="12" text-anchor="end">2.0</text><line x1="65.0" y1="128.6" x2="70.0" y2="128.6" stroke="currentColor" stroke-width="1"/><text x="62.0" y="132.6" font-size="12" text-anchor="end">2.5</text><line x1="65.0" y1="95.7" x2="70.0" y2="95.7" stroke="currentColor" stroke-width="1"/><text x="62.0" y="99.7" font-size="12" text-anchor="end">3.0</text><line x1="65.0" y1="62.9" x2="70.0" y2="62.9" stroke="currentColor" stroke-width="1"/><text x="62.0" y="66.9" font-size="12" text-anchor="end">3.5</text><line x1="65.0" y1="30.0" x2="70.0" y2="30.0" stroke="currentColor" stroke-width="1"/><text x="62.0" y="34.0" font-size="12" text-anchor="end">4.0</text><line x1="70.0" y1="227.1" x2="520.0" y2="227.1" stroke="#888" stroke-width="1" stroke-dasharray="4 4"/><text x="295.0" y="298.0" font-size="12" text-anchor="middle">draft 수락률 α</text><text x="20.0" y="145.0" font-size="12" text-anchor="middle" transform="rotate(-90 20 145)">속도 향상 배수</text><polyline points="70.0,255.7 87.3,254.4 104.6,252.9 121.9,251.4 139.2,249.8 156.5,248.1 173.8,246.3 191.2,244.4 208.5,242.3 225.8,240.1 243.1,237.8 260.4,235.4 277.7,232.7 295.0,229.9 312.3,227.0 329.6,223.9 346.9,220.5 364.2,217.0 381.5,213.3 398.8,209.4 416.2,205.2 433.5,200.8 450.8,196.1 468.1,191.2 485.4,186.1 502.7,180.6 520.0,174.9" fill="none" stroke="#4a7bd0" stroke-width="2"/><polyline points="70.0,241.0 87.3,239.4 104.6,237.9 121.9,236.3 139.2,234.6 156.5,232.9 173.8,231.2 191.2,229.4 208.5,227.5 225.8,225.6 243.1,223.7 260.4,221.7 277.7,219.7 295.0,217.6 312.3,215.5 329.6,213.3 346.9,211.1 364.2,208.8 381.5,206.5 398.8,204.2 416.2,201.8 433.5,199.3 450.8,196.8 468.1,194.3 485.4,191.7 502.7,189.0 520.0,186.4" fill="none" stroke="#e08a3c" stroke-width="2"/><polyline points="70.0,214.8 87.3,212.0 104.6,209.1 121.9,205.9 139.2,202.5 156.5,198.9 173.8,195.1 191.2,191.1 208.5,186.8 225.8,182.2 243.1,177.3 260.4,172.1 277.7,166.6 295.0,160.8 312.3,154.5 329.6,148.0 346.9,141.0 364.2,133.6 381.5,125.8 398.8,117.5 416.2,108.8 433.5,99.5 450.8,89.8 468.1,79.5 485.4,68.6 502.7,57.2 520.0,45.1" fill="none" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="6 4"/><line x1="85.0" y1="48.0" x2="115.0" y2="48.0" stroke="#4a7bd0" stroke-width="2"/><text x="121.0" y="52.0" font-size="12" text-anchor="start">γ=4, c=0.38 (1B draft → 3B target)</text><line x1="85.0" y1="68.0" x2="115.0" y2="68.0" stroke="#e08a3c" stroke-width="2"/><text x="121.0" y="72.0" font-size="12" text-anchor="start">γ=2, c=0.38</text><line x1="85.0" y1="88.0" x2="115.0" y2="88.0" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="6 4"/><text x="121.0" y="92.0" font-size="12" text-anchor="start">γ=4, c=0.05 (아주 작은 draft, 가정)</text><text x="516.0" y="243.1" font-size="12" text-anchor="end">1× (손해 경계)</text>
</svg>
```

그림 8 — speculative decoding의 속도 향상 vs draft 수락률 α. 파랑(γ = 4)과 주황(γ = 2)은 1B draft → 3B target(c = 0.38), 초록 점선은 c = 0.05인 아주 작은 draft를 가정한 경우. 회색 점선(1×) 아래는 손해다. c = 0.38이면 γ = 4에서는 α ≈ 0.65, γ = 2에서도 α ≈ 0.5는 넘어야 이득이다.

**기기에서의 비용**:

- 메모리: draft 가중치 + draft KV-cache가 추가로 상주한다(1B int4면 663 MiB + KV — 웨어러블에는 부담).
- 대안: 별도 draft 모델 없이 target에 작은 예측 head를 붙이는 방식(Medusa 계열), 또는 프롬프트에서 n-gram을 찾아 draft로 쓰는 방식(prompt lookup)은 추가 메모리가 거의 없다. 음성 비서처럼 답이 짧고 정형화된 경우 α가 높게 나올 수 있다(추정 — 제품 데이터로 측정해야 한다).
- α는 **도메인과 sampling temperature에 크게 의존**한다. greedy에 가까울수록 높다. 반드시 제품 프롬프트로 α를 측정한 뒤 판단한다(L6).

---

## 7. 웨어러블 음성 비서 한 턴의 예산

### 7.1 문제 설정 (모든 숫자는 가정)

예를 들어 Hark 같은 웨어러블 음성 비서에서 한 턴을 설계한다고 하자.

| 항목 | 가정값 | 메모 |
|---|---|---|
| 하드웨어 | 17 GB/s, 2 TFLOP/s | 3.4절 웨어러블 가정 |
| 실효 효율 | BW 60%, 연산 30% | 4절 실측의 감각(BW 60%대, 연산 50%) + 작은 기기의 여유 |
| endpoint (말 끝 판정) | 250 ms | VAD가 침묵을 확인하는 시간 |
| ASR 마무리 | 150 ms | streaming ASR의 최종 확정 |
| TTS 첫 청크 | 120 ms | 첫 오디오 프레임까지 |
| system prompt | 300 token | prefix 캐시 대상 |
| 사용자 발화 + 기록 | 60 token | 매 턴 새로 prefill |
| 답 길이 | 40 token | 음성 답은 짧다 |
| 첫 구절 | 12 token | TTS가 읽기 시작할 단위 |
| 말하는 속도 | 3.5 token/s | 약 150 단어/분 |
| LLM DRAM 예산 | 1 GiB | 가중치 + 2K context KV |

판정 기준(가정): TTFA < 1 s, decode tok/s > 말 속도의 2배(여유 포함), 메모리 < 1 GiB.

### 7.2 코드로 확인: 모델별 TTFA와 판정

```python
from llm_perf import shape, llm_perf
models = {"SmolLM2-135M": "HuggingFaceTB/SmolLM2-135M-Instruct", "Qwen2.5-0.5B": "Qwen/Qwen2.5-0.5B",
          "Llama-3.2-1B": "unsloth/Llama-3.2-1B", "Llama-3.2-3B": "unsloth/Llama-3.2-3B"}
wear = (2e12, 17e9)                     # 가정: 웨어러블 SoC
EFF = dict(eff_bw=0.6, eff_fl=0.3)      # 가정: 실효 BW 60%, 실효 연산 30%
ENDPOINT, ASR, TTS1 = 0.25, 0.15, 0.12  # 가정(초): 말 끝 판정, ASR 마무리, TTS 첫 오디오 청크
SYS, USER, ANS, FIRST = 300, 60, 40, 12 # token: system prompt, 발화+대화 기록, 답 길이, TTS 첫 구절
SPEECH = 3.5                            # 말하는 속도 ≈ 150 단어/분 ≈ 3.5 token/s
RAM = 1.0 * 2**30                       # LLM에 줄 수 있는 DRAM (가정)
print(f"{'int4 모델':12s}{'TTFT':>7s}{'TTFT*':>7s}{'TPOT':>6s}{'TTFA*':>7s}{'tok/s':>7s}{'MiB':>6s}  판정")
for name, mid in models.items():
    s = shape(mid)
    full = llm_perf(s, 4.5, 16, wear, SYS + USER, ANS, **EFF)
    cached = llm_perf(s, 4.5, 16, wear, USER, ANS, **EFF)       # system prompt KV 재사용 (근사)
    ttfa = ENDPOINT + ASR + cached["ttft"] + FIRST * full["tpot"] + TTS1
    mem = s["P"] * 4.5 / 8 + full["kv_tok"] * 2048             # 가중치 + 2K context KV
    ok = ttfa < 1.0 and 1 / full["tpot"] > 2 * SPEECH and mem < RAM
    print(f"{name:12s}{full['ttft']*1e3:7.0f}{cached['ttft']*1e3:7.0f}{full['tpot']*1e3:6.0f}"
          f"{ttfa*1e3:7.0f}{1/full['tpot']:7.1f}{mem/2**20:6.0f}  {'OK' if ok else 'X'}")
print("(ms 단위) * = system prompt KV 캐시 사용, TTFA = 말 끝 → 첫 소리")
print("판정: TTFA < 1 s, tok/s > 2 × 말속도, 메모리 < 1 GiB")
```

```text
int4 모델        TTFT  TTFT*  TPOT  TTFA*  tok/s   MiB  판정
SmolLM2-135M    135     22     8    641  120.8   117  OK
Qwen2.5-0.5B    439     72    28    925   36.1   289  OK
Llama-3.2-1B   1183    196    69   1548   14.4   727  X
Llama-3.2-3B   3421    566   181   3263    5.5  1947  X
(ms 단위) * = system prompt KV 캐시 사용, TTFA = 말 끝 → 첫 소리
판정: TTFA < 1 s, tok/s > 2 × 말속도, 메모리 < 1 GiB
```

출력에서 볼 것:

- **prefix caching이 TTFT를 크게 바꾼다**: Qwen2.5-0.5B는 439 → 72 ms, Llama-3.2-1B는 1,183 → 196 ms. 5.6절의 실측(3.4배)과 같은 방향이다.
- **TTFA에서 decode가 TTFT보다 크다**: Llama-3.2-1B의 TTFA 1,548 ms 중 첫 구절 12 token × 약 69 ms ≈ 830 ms가 가장 큰 항목이다. 음성에서는 TPOT도 첫 반응 지연에 들어간다는 1.3절의 식이 숫자로 보인다.
- **이 가정에서는 0.5B급이 한계**다. Qwen2.5-0.5B int4는 TTFA 925 ms로 1초 안에 들어가고 decode 36 tok/s로 말 속도의 10배다. Llama-3.2-1B는 메모리(727 MiB)는 들어가지만 TTFA가 1.5초다. 3B는 모든 기준에서 탈락이다.
- 1B를 살리려면: 첫 구절 K를 줄이기(TTS가 5~6 token에서 시작), KV·가중치를 더 줄이기, NPU 대역폭 효율을 높이기(60% → 80%), 또는 speculative decoding. 혹은 **질문 종류에 따라 기기/클라우드로 나누는** hybrid 라우팅(L3).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 330">
<text x="142.0" y="56.0" font-size="12" text-anchor="end">말 끝 판정 (VAD)</text><rect x="150.0" y="40.0" width="76.6" height="22.0" fill="#888" fill-opacity="0.75"/><text x="230.6" y="56.0" font-size="12" text-anchor="start">250</text><text x="142.0" y="88.0" font-size="12" text-anchor="end">ASR 마무리</text><rect x="226.6" y="72.0" width="45.9" height="22.0" fill="#3f9a6b" fill-opacity="0.75"/><text x="276.5" y="88.0" font-size="12" text-anchor="start">150</text><text x="142.0" y="120.0" font-size="12" text-anchor="end">LLM prefill (TTFT*)</text><rect x="272.5" y="104.0" width="22.1" height="22.0" fill="#4a7bd0" fill-opacity="0.75"/><text x="298.6" y="120.0" font-size="12" text-anchor="start">72</text><text x="142.0" y="152.0" font-size="12" text-anchor="end">LLM decode 첫 구절 12 tok</text><rect x="294.6" y="136.0" width="101.8" height="22.0" fill="#e08a3c" fill-opacity="0.75"/><text x="400.4" y="152.0" font-size="12" text-anchor="start">332</text><text x="142.0" y="184.0" font-size="12" text-anchor="end">TTS 첫 청크</text><rect x="396.4" y="168.0" width="36.8" height="22.0" fill="#d0564a" fill-opacity="0.75"/><text x="437.2" y="184.0" font-size="12" text-anchor="start">120</text><text x="142.0" y="216.0" font-size="12" text-anchor="end">LLM decode 나머지 28 tok</text><rect x="396.4" y="200.0" width="237.5" height="22.0" fill="#e08a3c" fill-opacity="0.35"/><text x="142.0" y="248.0" font-size="12" text-anchor="end">스피커 재생</text><rect x="433.2" y="232.0" width="206.8" height="22.0" fill="#3f9a6b" fill-opacity="0.35"/><line x1="433.2" y1="30.0" x2="433.2" y2="270.0" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="4 3"/><text x="429.2" y="262.0" font-size="12" text-anchor="end">TTFA ≈ 925 ms</text><line x1="456.2" y1="30.0" x2="456.2" y2="270.0" stroke="currentColor" stroke-width="1" stroke-dasharray="2 3"/><text x="460.2" y="30.0" font-size="12" text-anchor="start">1 s 목표</text><line x1="150.0" y1="272.0" x2="640.0" y2="272.0" stroke="currentColor" stroke-width="1"/><line x1="150.0" y1="272.0" x2="150.0" y2="277.0" stroke="currentColor" stroke-width="1"/><text x="150.0" y="305.0" font-size="12" text-anchor="middle">0</text><line x1="272.5" y1="272.0" x2="272.5" y2="277.0" stroke="currentColor" stroke-width="1"/><text x="272.5" y="305.0" font-size="12" text-anchor="middle">400</text><line x1="395.0" y1="272.0" x2="395.0" y2="277.0" stroke="currentColor" stroke-width="1"/><text x="395.0" y="305.0" font-size="12" text-anchor="middle">800</text><line x1="517.5" y1="272.0" x2="517.5" y2="277.0" stroke="currentColor" stroke-width="1"/><text x="517.5" y="305.0" font-size="12" text-anchor="middle">1200</text><line x1="640.0" y1="272.0" x2="640.0" y2="277.0" stroke="currentColor" stroke-width="1"/><text x="640.0" y="305.0" font-size="12" text-anchor="middle">1600</text><text x="640.0" y="320.0" font-size="12" text-anchor="end">ms (말이 끝난 순간 = 0)</text>
</svg>
```

그림 9 — Qwen2.5-0.5B int4, 웨어러블 가정에서 한 턴의 타임라인. 말이 끝난 순간이 0이다. endpoint 250 + ASR 150 + prefill 72 + 첫 구절 decode 332 + TTS 첫 청크 120 = 약 925 ms에 첫 소리(빨간 점선)가 나오고, 나머지 decode는 재생과 겹쳐서 진행된다. 검은 점선이 1초 목표다.

### 7.3 목표를 거꾸로 세우기 — 예산 배분

제품 요구가 "TTFA 800 ms"라면, 고정 항목(endpoint 250 + ASR 150 + TTS 120 = 520 ms)을 빼면 LLM에 **280 ms**가 남는다.

```
TTFT + K × TPOT ≤ 280 ms
K = 12라면: TPOT ≤ (280 − TTFT) / 12
prefix 캐시로 TTFT ≈ 70 ms라면 TPOT ≤ 17.5 ms → 57 tok/s 이상
웨어러블 17 GB/s × 60% = 10.2 GB/s → token당 바이트 ≤ 10.2 GB/s × 17.5 ms ≈ 179 MB
→ int4(4.5 bit)라면 Pm ≤ 179 MB × 8 / 4.5 ≈ 318M 파라미터 (KV 무시)
```

말로 하면: 이 예산에서는 **3억 파라미터 이하의 int4 모델**이어야 한다. 이런 역산이 "모델 크기를 몇으로 할까"라는 제품 질문에 대한 엔지니어링 답이다. endpoint를 250 → 150 ms로 줄이는 것(VAD 개선)이 LLM 파라미터 수를 크게 늘려 주기도 한다 — 전체 파이프라인을 같이 봐야 한다(L4).

---

## 8. 임베디드 관점에서 다시 보기

### 8.1 KV-cache arena를 C로 — 정적 할당, 연속 레이아웃, 링버퍼

온디바이스 런타임은 KV-cache를 부팅(또는 모델 로드) 때 **최대 context만큼 한 번에** 잡는다. 4.5절에서 본 cat/복제 오버헤드를 피하고, 메모리 부족을 런타임이 아니라 설계 시점에 발견하기 위해서다.

무엇을 확인하나: Llama-3.2-1B 모양 KV arena의 크기, `[layer][K/V][kv_head][slot][head_dim]` 레이아웃의 인덱스 계산, context별 step당 읽기 바이트와 17 GB/s에서의 시간, 링버퍼 slot 계산.

```c
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define N_LAYER  16      /* Llama-3.2-1B 모양 */
#define N_KV     8
#define HEAD_DIM 64
#define MAX_CTX  4096    /* 정적으로 잡는 최대 context = ring 크기 */
typedef uint16_t f16;    /* fp16 비트 패턴 저장용 */

/* layout: [layer][K=0/V=1][kv_head][slot][head_dim]  — 한 head의 과거 전체가 연속 */
static size_t kv_index(int layer, int kv, int head, int slot) {
    return ((((size_t)layer * 2 + kv) * N_KV + head) * MAX_CTX + slot) * HEAD_DIM;
}
static int ring_slot(long n) { return (int)(n % MAX_CTX); }   /* sliding window = ring buffer */

int main(void) {
    size_t per_tok = (size_t)2 * N_LAYER * N_KV * HEAD_DIM * sizeof(f16);
    size_t arena = per_tok * MAX_CTX;
    printf("KV/token %zu B, arena %zu B = %.0f MiB\n", per_tok, arena, arena / 1048576.0);
    printf("idx(L3,V,h5,slot1000) = %zu 원소 → 바이트 오프셋 %zu\n",
           kv_index(3, 1, 5, 1000), kv_index(3, 1, 5, 1000) * sizeof(f16));
    printf("한 head의 K를 ctx t=2048까지 읽기: 연속 %zu B (DMA descriptor 1개)\n",
           (size_t)2048 * HEAD_DIM * sizeof(f16));
    for (long t = 1024; t <= 4096; t *= 2)   /* decode step마다 읽는 KV와 17 GB/s에서의 시간 */
        printf("ctx %4ld: step당 KV 읽기 %6.1f MiB → %.2f ms @17 GB/s\n",
               t, per_tok * (double)t / 1048576.0, per_tok * (double)t / 17e9 * 1e3);
    printf("token #5000은 slot %d에 덮어쓴다 (가장 오래된 #904 자리)\n", ring_slot(5000));
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 kv.c -o kv && ./kv
```

```text
KV/token 32768 B, arena 134217728 B = 128 MiB
idx(L3,V,h5,slot1000) = 16054784 원소 → 바이트 오프셋 32109568
한 head의 K를 ctx t=2048까지 읽기: 연속 262144 B (DMA descriptor 1개)
ctx 1024: step당 KV 읽기   32.0 MiB → 1.97 ms @17 GB/s
ctx 2048: step당 KV 읽기   64.0 MiB → 3.95 ms @17 GB/s
ctx 4096: step당 KV 읽기  128.0 MiB → 7.90 ms @17 GB/s
token #5000은 slot 904에 덮어쓴다 (가장 오래된 #904 자리)
```

출력에서 볼 것:

- arena 128 MiB = 32 KiB/token × 4096. 2.3절 Python 계산과 같다.
- 인덱스 손검증: `((3 × 2 + 1) × 8 + 5) × 4096 + 1000 = 250,856` slot, × 64 = 16,054,784 원소, × 2 B = 32,109,568 B. 맞다.
- 레이아웃을 `[layer][K/V][head][slot][dim]`으로 두면 **한 head의 과거 전체가 연속**이다. decode step에서 attention 커널은 head마다 연속 구간 하나(ctx 2048이면 256 KiB)를 스트리밍으로 읽는다. DMA descriptor 하나, prefetch가 잘 되는 순차 읽기다. `[slot][layer][head][dim]` 순서였다면 128 B씩 흩어진 읽기가 되어 실효 대역폭이 떨어진다.
- ctx 4096에서 step당 KV 읽기 128 MiB → 17 GB/s에서 7.9 ms. 가중치 int4 읽기(40.9 ms)에 더해지는 몫이다(5.2절 손계산과 일치).
- `ring_slot`: sliding window라면 token #5000이 slot 904(가장 오래된 #904의 자리)를 덮어쓴다. RoPE는 K를 저장하기 전에 이미 절대 위치로 회전해 두므로 slot 순서가 섞여도 attention 결과는 같다(점수는 위치 차이에만 의존 — B4 6.4절). attention sink를 쓰려면 앞쪽 slot 몇 개를 링에서 제외(pinned)하면 된다.

### 8.2 bring-up 체크리스트 — "몇 tok/s 나왔다"를 받았을 때

1. 계산기로 이론 상한을 계산한다(`W + KV` ÷ 사양 BW).
2. 같은 기기에서 STREAM류로 **실효 BW**를 잰다(사양의 60~80%가 보통).
3. 실측 decode tok/s ÷ (실효 BW ÷ token당 바이트) = **decode 효율**. 70% 이상이면 건강하다.
4. 20~40%라면: CPU fallback, 스레드 수, 비융합 attention, KV 복사(4.3~4.5절)를 의심하고 profiler를 건다.
5. context를 늘려 가며 TPOT 기울기를 잰다. 기울기 ÷ (KV/token ÷ BW) = **KV 트래픽 배수**. 1~2면 좋고, 8이면 4.5절 상황이다.
6. prefill은 S를 늘려 가며 잰다. S에 비례하면 정상, S²로 휘면 attention 커널 문제다.
7. 모든 측정은 warm-up, 반복, 중앙값, 열 상태 기록(M2).

### 8.3 Hark 같은 웨어러블이라면 (추정 예시)

- decode는 LPDDR 대역폭이 전부다. 모델을 고르기 전에 **메모리 버스 폭과 속도**를 먼저 확인하고, 같은 DRAM을 쓰는 다른 블록(카메라, 디스플레이, DSP)과의 경쟁도 고려한다.
- prefill은 NPU에, decode는 대역폭 효율이 가장 좋은 엔진(NPU든 CPU든)에 — 두 단계를 **다른 엔진에** 둘 수도 있다.
- system prompt KV는 캐시하고, 대화 기록은 요약해서 context를 짧게 유지한다(교차점보다 한참 아래로).
- 첫 구절을 빨리 만드는 것이 TTFA의 핵심이다. TTS가 짧은 구절부터 시작할 수 있게 LLM 출력 스트리밍과 TTS를 파이프라인으로 묶는다(L4).

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| tokens/s 숫자 하나로 비교 | 벤치마크끼리 숫자가 안 맞음 | prefill/decode/평균이 섞임 | pp(prefill)와 tg(decode)를 따로, 몇 token 기준인지 명시 |
| decode 예측에 KV 항을 뺌 | 긴 대화에서 예측보다 훨씬 느림 | `W / BW`만 계산 | `(W + KV/token × ctx) / BW`로 계산, 교차점 확인 |
| 작은 working set으로 BW 측정 | 사양보다 높은 대역폭이 나옴 | 캐시 hit (4.1절 GEMV 192 GB/s) | 수백 MB 배열로 STREAM, 실제 모델 크기와 맞춤 |
| 비융합 attention 경로 방치 | TTFT가 S²로 휘고 모델보다 5배 느림 | math 경로, S×S 점수 텐서 왕복 | profiler로 확인, fused 커널 경로로 (4.3절) |
| KV를 매 step cat·복제 | TPOT의 context 기울기가 이론의 8배 | 동적 할당 + repeat_kv | 정적 arena + GQA-aware 커널 (8.1절) |
| system prompt 앞부분에 가변 값 | prefix cache hit가 안 남, TTFT 그대로 | 첫 token부터 달라져 KV 재사용 불가 | 가변 값은 프롬프트 뒤쪽으로 |
| "batch 2면 거의 공짜" 가정 | B = 2에서 step 시간이 두 배 | GEMV → 작은-M GEMM 커널 전환 | 두 경로 모두 벤치마크 (6.1절) |
| speculative decoding을 무조건 적용 | 오히려 느려짐 | draft가 크거나(c 큼) α가 낮음 | α를 제품 데이터로 측정, 속도 향상 식으로 판정 |
| TTFT만 보고 음성 지연 판정 | 실제 첫 소리가 예상보다 늦음 | 첫 구절 K × TPOT 누락 | TTFA 식으로 예산 (7절) |
| 단일 측정으로 결론 | 재현이 안 됨 | 백그라운드 작업, 열, 캐시 상태 | warm-up, 반복, 중앙값, 환경 기록 |

---

## 10. 면접에서 이렇게 말한다

**Q.** "Estimate tokens/s for a 1B int4 model on a device with 17 GB/s of memory bandwidth."

**A.** decode는 매 token 가중치 전체를 읽는다. 1.24B × 4.5 bit ≈ 0.7 GB이니 17 ÷ 0.7 ≈ 24 tok/s가 이론 상한이다. 실효 대역폭 60~70%를 적용하면 15 tok/s 안팎이고, context가 길면 KV 읽기가 더해진다(4K에서 +128 MiB).

> "Decode is memory-bound: every token streams all the weights. A 1.24-billion-parameter model at about 4.5 bits per weight is roughly 0.7 GB, so 17 GB/s divided by 0.7 GB gives an upper bound of about 24 tokens per second. With a realistic 60 to 70 percent bandwidth efficiency I'd expect around 15 tokens per second, and it drops further as the KV-cache grows — at 4K context the KV-cache adds another 128 MiB to every token's reads."

**Q.** "Why does TTFT grow with prompt length but TPOT barely changes?"

**A.** TTFT는 prefill이고 prompt의 모든 token을 처리하므로 FLOP이 S에 비례한다(긴 S에서는 attention의 S² 항까지). TPOT는 token 하나의 decode로, 비용이 "가중치 + KV" 바이트다. 가중치는 고정이고 KV는 context에 비례하지만 보통 가중치보다 훨씬 작아서(1B int4, 512 token에서 2.4%) 거의 일정하다.

> "TTFT is the prefill phase, which processes every prompt token, so its compute scales linearly with prompt length — and quadratically for attention at long lengths. TPOT is a single decode step whose cost is reading the weights plus the KV-cache. The weights are fixed and the KV term is usually small compared to them — about two percent for a 1B int4 model at 512 tokens — so TPOT grows only slowly with context."

**Q.** "How big is the KV-cache for Llama-3.2-1B at 8K context?"

**A.** 2 × 16층 × 8 KV head × 64 × 2 B = 32 KiB/token. 8,192 token이면 256 MiB(fp16). GQA가 없었다면 1 GiB, int8 KV면 약 128 MiB다.

> "Per token it's 2 for K and V, times 16 layers, times 8 KV heads, times head dimension 64, times 2 bytes — 32 KiB. At 8K tokens that's 256 MiB in fp16. Without GQA it would be four times larger, about 1 GiB, and with int8 KV it's roughly halved."

**Q.** "How does GQA help decode?"

**A.** GQA는 여러 query head가 K/V head 하나를 공유한다. KV-cache 용량과 decode step마다 읽는 KV 바이트가 query:KV 비율만큼 준다(Llama-3.2-1B는 4배). decode는 memory-bound이므로 이게 그대로 긴 context의 TPOT 개선이 된다. 연산량은 그대로지만 decode에서 연산은 병목이 아니다. 단, 커널이 KV를 복제하지 않고 공유해서 읽어야 이득이 실현된다.

> "With grouped-query attention several query heads share one key-value head, so both the KV-cache footprint and the KV bytes read per decode step shrink by the group ratio — four times for Llama-3.2-1B. Since decode is bandwidth-bound, that translates directly into faster tokens at long context. The attention FLOPs don't change, but compute isn't the bottleneck in decode. The catch is the kernel must read the shared KV once instead of materializing copies — I measured an 8x KV traffic multiplier on a naive CPU path."

**Q.** "What is prefix caching?"

**A.** causal attention에서는 앞부분 token의 K/V가 뒤 token에 영향을 받지 않는다. 그래서 매 요청마다 같은 system prompt의 KV를 한 번 계산해 저장해 두고 재사용하면, prefill은 새로 붙은 부분만 하면 된다. SmolLM2-135M에서 318 token system prompt를 캐시하니 TTFT가 131 → 39 ms가 되었고 logits는 같았다. 조건은 앞부분 token이 정확히 같아야 한다는 것이다.

> "Because attention is causal, the keys and values of a prefix don't depend on anything after it. So we compute the KV-cache of a fixed system prompt once, store it, and on each request only prefill the new tokens. In my measurement on SmolLM2, caching a 318-token system prompt cut TTFT from 131 to 39 milliseconds with identical logits. The prefix must match token for token, so anything dynamic like timestamps goes at the end."

**Q.** "Prefill is compute-bound and decode is memory-bound — how would you use that when choosing hardware or partitioning work?"

**A.** prefill 속도는 NPU TOPS와 prompt 길이, decode 속도는 LPDDR 대역폭과 token당 바이트가 정한다. 음성 비서처럼 prompt가 짧고(prefix 캐시 포함) 답을 스트리밍하는 제품은 대역폭이 더 중요하다. 필요하면 prefill은 NPU, decode는 대역폭 효율이 좋은 엔진으로 나누고, 바이트를 줄이는 양자화·GQA·KV 양자화를 먼저 적용한다.

> "Prefill speed is set by compute and prompt length; decode speed by memory bandwidth and bytes per token. For a voice assistant with short prompts — especially with prefix caching — and streamed answers, bandwidth usually matters more than TOPS. So I'd look at the LPDDR bus first, cut bytes per token with weight and KV quantization and a GQA model, and if the platform allows, run prefill on the NPU and decode on whichever engine achieves the best effective bandwidth."

**Q.** "When does speculative decoding help on device?"

**A.** 검증 한 번에 얻는 token 기댓값이 `(1 − α^(γ+1)) / (1 − α)`이고 속도 향상은 그것을 `γc + 1`로 나눈 값이다. draft가 target보다 충분히 작고(c 작게) 수락률 α가 높아야 한다. 1B→3B처럼 c = 0.38이면 γ = 4에서 α ≈ 0.65, γ = 2에서도 α ≈ 0.5가 손익분기점이다. 기기에서는 draft의 메모리도 비용이라 n-gram lookup이나 작은 head 방식도 검토한다.

> "Each verification step yields on average one minus alpha to the gamma-plus-one over one minus alpha tokens, and the speedup is that divided by gamma times c plus one, where c is the draft-to-target cost ratio. It pays off only when the draft is much cheaper and the acceptance rate is high — with a 1B draft for a 3B target, c is about 0.38, so with four draft tokens you need alpha above roughly 0.65 just to break even. On device the draft's memory is also a cost, so I'd also consider prompt-lookup or small draft heads."

---

## 11. 직접 해보기

1. **손계산**: Qwen2.5-0.5B int4(4.5 bit), 폰 가정 60 GB/s, context 2K, KV fp16에서 decode 상한 tok/s는? (P = 494,032,768, KV/token = 12 KiB)
   정답: 가중치 277.9 MB + KV 12,288 × 2,048 = 25.2 MB → 303.1 MB ÷ 60 GB/s = 5.05 ms → 약 198 tok/s.
2. **손계산**: Llama-3.2-3B의 교차점을 int8 가중치, int8 KV(8 bit, scale 무시)로 다시 계산하라. fp16 KV일 때(27.4K)와 비교하면?
   정답: KV/token이 절반(56 KiB)이 되므로 교차점은 두 배, 약 54.7K token.
3. **손계산**: speculative decoding에서 α = 0.6, γ = 3, c = 0.1일 때 속도 향상은?
   정답: E = (1 − 0.6⁴)/(0.4) = (1 − 0.1296)/0.4 = 2.176, 속도 향상 = 2.176 / 1.3 ≈ 1.67배.
4. **코드 과제**: 4.4절 예제를 `use_gqa_in_sdpa` 패치 없이(HF 기본 경로) 다시 돌려 기울기 배수를 재라. 4.5절 표의 트래픽 회계로 설명이 되는가?
   힌트: 기본 경로는 math 커널이 K에 scale을 곱하는 복사까지 더해진다(profiler에서 `aten::mul`, `aten::copy_`가 context와 함께 커지는지 본다).
5. **코드 과제**: `llm_perf()`에 고정 오버헤드 항 `t_fixed`(forward 호출당 ms)를 추가하고, 4.2절 실측(S = 64의 약 60 ms)과 4.4절 절편(16.8 ms)을 동시에 설명하는 `eff_fl`, `eff_bw`, `t_fixed`를 찾아라.
   힌트: decode 절편 ≈ W / (BW × eff_bw) + t_fixed, prefill 중간 S의 기울기 ≈ 2·Pm / (peak × eff_fl).
6. **설계 과제**: 7.3절의 역산을 "TTFA 1.2 s, 첫 구절 8 token, BW 효율 75%"로 다시 해서 허용되는 int4 파라미터 수를 구하라.
   정답: LLM 몫 1,200 − 520 = 680 ms, TTFT 70 ms 가정 → TPOT ≤ 76 ms → 12.75 GB/s × 76 ms ≈ 972 MB → Pm ≤ 972 MB × 8 / 4.5 ≈ 1.7B 파라미터(KV 무시). 1B는 여유 있게, 1.5B급도 들어간다.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| prefill | 프롬프트 처리 단계 | S개 token을 한 번에 GEMM으로 처리, KV-cache를 채움, 보통 compute-bound |
| decode | 생성 단계 | token 하나씩 GEMV로 생성, 보통 memory-bound |
| TTFT | Time To First Token | 요청 → 첫 token. prefill 시간이 지배 |
| TPOT | Time Per Output Token | 두 번째 token부터의 평균 간격. 1 / TPOT = decode tok/s |
| ITL | Inter-Token Latency | 연속한 token 사이의 개별 간격. 분포의 꼬리가 끊김을 만든다 |
| E2E latency | end-to-end 지연 | TTFT + (M − 1) × TPOT |
| TTFA | time-to-first-audio | 음성 비서에서 말 끝 → 첫 소리 |
| arithmetic intensity | 연산 밀도 | FLOP ÷ 메모리 바이트. ridge보다 작으면 memory-bound (D3) |
| ridge point | 경계 intensity | peak FLOP/s ÷ BW. 이보다 크면 compute-bound |
| KV-cache | 과거 K, V 저장소 | token마다 2 × L × nkv × hd × 바이트씩 자람 |
| 교차점 | crossover context | KV 바이트 = 가중치 바이트가 되는 context 길이 |
| GQA | grouped-query attention | query head 여러 개가 KV head 하나를 공유, KV를 비율만큼 줄임 |
| PagedAttention | paged KV 관리 | KV를 고정 크기 block으로 나누고 block table로 매핑 (vLLM) |
| sliding window | 지역 attention | 최근 W token만 보는 층, KV가 W에서 멈춤 (링버퍼) |
| attention sink | 앞 token 보존 | 맨 앞 몇 token의 KV를 남겨 window 방식의 붕괴를 막는 기법 (StreamingLLM) |
| prefix caching | prefix KV 재사용 | 같은 앞부분(system prompt)의 KV를 저장해 prefill 생략 |
| chunked prefill | 분할 prefill | 긴 prompt를 조각내 처리, scratch 메모리 상한 고정 |
| speculative decoding | 추측 디코딩 | draft가 γ token 추측, target이 한 번에 검증 |
| 수락률 α | acceptance rate | draft token이 target 검증을 통과할 확률 |
| STREAM | 대역폭 벤치마크 | 큰 배열 copy/sum 등으로 실효 DRAM 대역폭 측정 |
| 트래픽 배수 | traffic multiplier | 이상 대비 실제로 움직인 바이트 비율 (write amplification과 같은 개념) |
| logits_to_keep | HF 인자 | lm_head를 마지막 몇 위치에만 계산 |

---

## 13. 요약 & 체크리스트

LLM 추론은 prefill과 decode 두 단계다. prefill은 가중치를 한 번 읽어 S번 쓰는 GEMM이라 compute-bound이고 TTFT를 정한다. decode는 매 token 가중치 전체와 KV-cache 전체를 읽는 GEMV라 memory-bound이고 TPOT를 정한다. 두 식 — `t_prefill ≈ max(FLOP/peak, bytes/BW)`, `t_step ≈ (W + KV/token × ctx)/BW` — 을 계산기로 만들면 모델·형식·하드웨어 조합의 성능을 미리 볼 수 있다. 이 Mac에서 검증해 보니 모양은 맞고, 차이는 효율(×2)과 구조적 낭비(비융합 attention의 S² 트래픽, KV 복사·복제로 인한 8배 트래픽)에서 왔다. KV-cache는 token마다 선형으로 자라 교차점을 넘으면 가중치보다 커지고, GQA·sliding window·KV 양자화·paged 관리·prefix caching이 그 크기와 트래픽을 다루는 도구다. 기기에서는 batch = 1이라 decode가 구조적으로 memory-bound이고, 속도 손잡이는 바이트 줄이기와 speculative decoding이다. 음성 비서에서는 TTFA = endpoint + ASR + TTFT + 첫 구절 × TPOT + TTS로 예산을 세우고, 역산하면 웨어러블급 대역폭에서 허용되는 모델 크기가 나온다.

- [ ] TTFT, TPOT, ITL, E2E, TTFA를 정의하고 E2E 식을 쓸 수 있다
- [ ] config에서 Pm, KV/token, 교차 context를 손으로 계산할 수 있다
- [ ] Llama-3.2-1B int4의 prefill FLOP과 decode 바이트를 손으로 계산하고 compute/memory-bound를 판정할 수 있다
- [ ] "1B int4, 17 GB/s → 약 24 tok/s 상한"을 10초 안에 말할 수 있다
- [ ] 측정값과 해석 모델의 비율로 효율 문제와 구조적 낭비를 구별할 수 있다
- [ ] decode TPOT의 context 기울기로 KV 트래픽 배수를 계산할 수 있다
- [ ] GQA, sliding window, KV 양자화가 각각 식의 어느 항을 줄이는지 말할 수 있다
- [ ] PagedAttention과 prefix caching을 펌웨어 개념(페이지 매핑, 캐시)에 빗대어 설명할 수 있다
- [ ] speculative decoding의 기댓값과 속도 향상 식으로 이득 여부를 판정할 수 있다
- [ ] 음성 비서 TTFA 예산을 세우고 허용 모델 크기를 역산할 수 있다

---

## 참고 자료

- B4 (attention, GQA, KV-cache 공식), B8 (SLM 메모리 계산, TTFT/TPOT 첫 측정), C3 (가중치·KV 양자화), D3 (roofline), D6 (latency 분포), D7 (에너지), L4 (음성 파이프라인), L6 (디코딩 가속) — 이 스터디 노트 세트
- Pope et al., "Efficiently Scaling Transformer Inference" (2022) — prefill/decode 비용 모델의 고전: [arXiv:2211.05102](https://arxiv.org/abs/2211.05102)
- Ainslie et al., "GQA: Training Generalized Multi-Query Transformer Models from Multi-Head Checkpoints" (2023): [arXiv:2305.13245](https://arxiv.org/abs/2305.13245)
- Kwon et al., "Efficient Memory Management for Large Language Model Serving with PagedAttention" (2023, vLLM): [arXiv:2309.06180](https://arxiv.org/abs/2309.06180)
- Xiao et al., "Efficient Streaming Language Models with Attention Sinks" (2023, StreamingLLM): [arXiv:2309.17453](https://arxiv.org/abs/2309.17453)
- Leviathan, Kalman, Matias, "Fast Inference from Transformers via Speculative Decoding" (2023): [arXiv:2211.17192](https://arxiv.org/abs/2211.17192)
- Chen et al., "Accelerating Large Language Model Decoding with Speculative Sampling" (2023): [arXiv:2302.01318](https://arxiv.org/abs/2302.01318)
- Dao et al., "FlashAttention" (2022): [arXiv:2205.14135](https://arxiv.org/abs/2205.14135)
- Gholami et al., "AI and Memory Wall" (2024): [arXiv:2403.14123](https://arxiv.org/abs/2403.14123)
- Williams, Waterman, Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures" (CACM, 2009)
- Gemma Team, "Gemma 3 Technical Report" (2025) — local/global attention 혼합 구조
- Hugging Face Transformers 문서 — KV cache 전략: [huggingface.co/docs/transformers/kv_cache](https://huggingface.co/docs/transformers/kv_cache)
- PyTorch 문서 — `torch.nn.functional.scaled_dot_product_attention`, `torch.profiler`: [pytorch.org/docs](https://pytorch.org/docs/stable/index.html)
- llama.cpp (GGML, `llama-bench`의 pp/tg 측정): [github.com/ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp)
