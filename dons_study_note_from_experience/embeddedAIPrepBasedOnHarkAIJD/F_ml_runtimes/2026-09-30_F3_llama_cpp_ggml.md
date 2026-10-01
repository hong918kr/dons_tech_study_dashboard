# F3. llama.cpp / GGML — GGUF 변환, 양자화, 백엔드, 벤치마크를 직접

> **이 노트를 다 읽으면**: Hugging Face 모델을 GGUF로 바꾸고 Q8_0·Q4_0·Q4_K_M 등으로 양자화해서 직접 돌릴 수 있다 · GGUF 파일 안에 무엇이 어떤 순서로 들어 있는지 바이트 단위로 읽고, Q4_K_M이 텐서마다 실제로 어떤 형식을 썼는지 확인할 수 있다 · `llama-bench`의 pp/tg 숫자를 대역폭·연산량 이론(D5)으로 설명하고 CPU와 GPU 결과의 차이를 해석할 수 있다 · `llama.h` C API로 앱에 넣는 흐름과 Android/Snapdragon 빌드 경로를 말할 수 있다
> **JD 연결**: JD의 임베디드 ML 런타임 요구 "**TFLite, llama.cpp, QNN**" 중 llama.cpp, 그리고 우대 "lightweight LLM", "KV-cache behavior, and their memory bandwidth requirements" — study_prep_list **F3**: GGUF 포맷, 양자화 타입(Q4_0, Q4_K_M, Q8_0) · 백엔드: CPU NEON, Metal, Vulkan, OpenCL(Adreno) · mmap 로딩, KV-cache 옵션, `llama-bench` (연결: L2 온디바이스 LLM 스택)
> **Don 기준 난이도**: CMake 빌드, C API 링크, mmap, 바이트 레이아웃 읽기, 측정 노이즈 다루기는 이미 강한 부분 / GGUF 메타데이터와 텐서 이름 규칙, k-quant 레시피, pp와 tg의 병목 차이, sampler chain, 백엔드 scheduler는 새로 배울 부분
> **선행 노트**: B8 (SLM 구조, SmolLM2), C3 (bit/weight, GGUF 양자화 타입 블록 구조 §8, KV 양자화 §9), D5 (prefill/decode 해석 모델, 이 Mac의 대역폭 실측)

---

## 0. 큰 그림 — 이게 왜 필요한가

B8·C3·D5에서는 PyTorch로 SmolLM2를 열어 보고, 양자화를 직접 구현하고, 성능을 계산기로 예측했다. 그런데 실제 기기에 LLM을 올릴 때 PyTorch를 그대로 쓰는 일은 거의 없다. Python 인터프리터, 수백 MB의 라이브러리, 동적 메모리 할당이 모두 기기에는 부담이다. 그래서 **추론만 하는 전용 런타임**을 쓴다. 그 중 가장 널리 쓰이는 오픈소스가 **llama.cpp**다.

llama.cpp를 한 문장으로 말하면 이렇다. **"C/C++로 짠 LLM 추론 엔진 + 그 밑의 텐서 라이브러리 ggml + 모델 파일 포맷 GGUF + 양자화·벤치마크 도구 모음"**. 의존성이 거의 없어서 노트북, 서버, 스마트폰, 라즈베리 파이 같은 SBC에서 같은 코드가 빌드된다. JD가 이름을 콕 집어 말하는 이유도 여기에 있다. 웨어러블 회사가 "기기에서 작은 LLM을 돌려 보자"고 할 때 가장 먼저 손대는 도구이기 때문이다.

이 노트에서는 이 Mac(Apple M2)에서 실제로 전 과정을 돌린다. 모든 출력은 실제 실행 결과다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 320"><text x="10" y="14" font-size="12">PC에서 한 번 (오프라인)</text><g stroke="currentColor" stroke-width="1"><rect x="10" y="22" width="112" height="58" rx="6" fill="#888" fill-opacity="0.15"/><rect x="144" y="22" width="112" height="58" rx="6" fill="#4a7bd0" fill-opacity="0.2"/><rect x="278" y="22" width="112" height="58" rx="6" fill="#888" fill-opacity="0.15"/><rect x="412" y="22" width="112" height="58" rx="6" fill="#4a7bd0" fill-opacity="0.2"/><rect x="546" y="22" width="112" height="58" rx="6" fill="#3f9a6b" fill-opacity="0.25"/></g><text x="66" y="45" font-size="12" text-anchor="middle">HF 체크포인트</text><text x="66" y="62" font-size="12" text-anchor="middle">safetensors+json</text><text x="200" y="45" font-size="12" text-anchor="middle">convert_hf_</text><text x="200" y="62" font-size="12" text-anchor="middle">to_gguf.py</text><text x="334" y="45" font-size="12" text-anchor="middle">GGUF F16</text><text x="334" y="62" font-size="12" text-anchor="middle">942 MiB</text><text x="468" y="45" font-size="12" text-anchor="middle">llama-quantize</text><text x="468" y="62" font-size="12" text-anchor="middle">(C++)</text><text x="602" y="45" font-size="12" text-anchor="middle">GGUF Q4_K_M</text><text x="602" y="62" font-size="12" text-anchor="middle">374 MiB</text><g stroke="currentColor" stroke-width="1.5"><line x1="122" y1="51" x2="138" y2="51"/><line x1="256" y1="51" x2="272" y2="51"/><line x1="390" y1="51" x2="406" y2="51"/><line x1="524" y1="51" x2="540" y2="51"/><line x1="602" y1="80" x2="602" y2="114"/><line x1="380" y1="162" x2="380" y2="184"/></g><g fill="currentColor"><polygon points="138,47 144,51 138,55"/><polygon points="272,47 278,51 272,55"/><polygon points="406,47 412,51 406,55"/><polygon points="540,47 546,51 540,55"/><polygon points="598,114 602,120 606,114"/><polygon points="376,184 380,190 384,184"/></g><text x="612" y="102" font-size="12">파일 1개 복사</text><text x="10" y="138" font-size="12">기기에서</text><text x="10" y="154" font-size="12">(런타임)</text><rect x="100" y="120" width="558" height="42" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/><text x="379" y="138" font-size="13" text-anchor="middle">libllama — 모델 로드(mmap) · tokenizer · KV-cache · batch · sampler</text><text x="379" y="155" font-size="12" text-anchor="middle">src/llama-*.cpp, include/llama.h</text><rect x="100" y="190" width="558" height="42" rx="6" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="379" y="208" font-size="13" text-anchor="middle">ggml — 텐서 · 계산 그래프 · 양자화 커널 · backend scheduler</text><text x="379" y="225" font-size="12" text-anchor="middle">ggml/include/ggml.h, ggml/src/</text><g stroke="currentColor" stroke-width="1"><line x1="146" y1="232" x2="146" y2="262"/><line x1="239" y1="232" x2="239" y2="262"/><line x1="332" y1="232" x2="332" y2="262"/><line x1="425" y1="232" x2="425" y2="262"/><line x1="518" y1="232" x2="518" y2="262"/><line x1="611" y1="232" x2="611" y2="262"/></g><g stroke="currentColor" stroke-width="1"><rect x="102" y="262" width="88" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.25"/><rect x="195" y="262" width="88" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.25"/><rect x="288" y="262" width="88" height="44" rx="5" fill="#888" fill-opacity="0.15"/><rect x="381" y="262" width="88" height="44" rx="5" fill="#888" fill-opacity="0.15"/><rect x="474" y="262" width="88" height="44" rx="5" fill="#d0564a" fill-opacity="0.2"/><rect x="567" y="262" width="88" height="44" rx="5" fill="#d0564a" fill-opacity="0.2"/></g><text x="146" y="281" font-size="12" text-anchor="middle">CPU</text><text x="146" y="298" font-size="12" text-anchor="middle">NEON/AVX</text><text x="239" y="281" font-size="12" text-anchor="middle">Metal</text><text x="239" y="298" font-size="12" text-anchor="middle">(Apple GPU)</text><text x="332" y="281" font-size="12" text-anchor="middle">CUDA</text><text x="332" y="298" font-size="12" text-anchor="middle">/ HIP</text><text x="425" y="281" font-size="12" text-anchor="middle">Vulkan</text><text x="425" y="298" font-size="12" text-anchor="middle">/ SYCL …</text><text x="518" y="281" font-size="12" text-anchor="middle">OpenCL</text><text x="518" y="298" font-size="12" text-anchor="middle">(Adreno)</text><text x="611" y="281" font-size="12" text-anchor="middle">Hexagon</text><text x="611" y="298" font-size="12" text-anchor="middle">(실험적)</text></svg>
```

그림 1 — llama.cpp로 모델을 기기에 올리는 전체 흐름. 위 줄은 PC에서 한 번 하는 일(변환·양자화), 아래 세 층은 기기에서 도는 런타임이다. 초록 백엔드는 이 노트에서 직접 측정한 것, 빨강은 Hark 같은 Qualcomm 기반 기기와 관련 있는 것이다(Hexagon 백엔드는 이 커밋에 소스가 있지만 성숙도는 확인하지 않았다).

펌웨어 비유로 보면 구조가 익숙하다.

| llama.cpp | 펌웨어에서 비슷한 것 |
|---|---|
| GGUF 파일 | 헤더 + 메타데이터 + 정렬된 payload를 가진 펌웨어 이미지 (예: 부트로더가 읽는 이미지 포맷) |
| `convert_hf_to_gguf.py` | 빌드 산출물을 이미지 포맷으로 감싸는 패키징 스크립트 |
| `llama-quantize` | 압축/패킹 도구 (같은 데이터를 더 적은 비트로) |
| libllama | 애플리케이션 계층 (모델 구조, 상태 관리, API) |
| ggml backend | HAL / 드라이버 계층 (같은 연산을 CPU, GPU, DSP 별로 구현) |
| `llama-bench` | 성능 회귀 테스트 하네스 |

이 노트의 지도:

| 절 | 질문 | 직접 하는 것 |
|---|---|---|
| 1 | llama.cpp와 ggml은 무엇이고 저장소는 어떻게 생겼나 | 디렉터리 확인, 백엔드 목록 |
| 2 | HF 모델을 어떻게 GGUF로 바꾸나 | `convert_hf_to_gguf.py` |
| 3 | 양자화하면 크기가 얼마나 되고, Q4_K_M은 실제로 무엇을 하나 | `llama-quantize`, fallback 경고, 텐서별 형식 |
| 4 | GGUF 파일 안에는 무엇이 있나 | 바이트 직접 파싱, `gguf.GGUFReader` |
| 5 | 텍스트는 어떻게 생성하나 | `llama-cli`, `llama-simple`, sampler |
| 6 | 품질은 얼마나 떨어지나 | `llama-perplexity` |
| 7 | 속도는 얼마나 나오나, 왜 그런가 | `llama-bench` pp/tg, CPU vs Metal, 이론과 비교 |
| 8 | 런타임 안에서는 무슨 일이 일어나나 | ggml 그래프 C 예제, mmap, KV 옵션, thread, batch |
| 9 | 앱에 어떻게 넣나 | `llama.h` C 예제, Android/Snapdragon 빌드 |

**실험 환경** (모든 숫자의 전제):

- Apple M2 MacBook Air 15인치(`hw.model = Mac14,15`, 팬 없음). 성능 코어 4 + 효율 코어 4, GPU 10코어, 통합 메모리 24 GB, 사양상 메모리 대역폭 100 GB/s. D5 4.1절에서 CPU 4 thread로 잰 읽기 대역폭은 약 60~64 GB/s였다.
- llama.cpp 커밋 `f7b384c` (2026-09-30), Release 빌드, 백엔드 Metal + CPU + BLAS(Accelerate).
- 모델 3개: **SmolLM2-135M-Instruct** (B8·C3·D5에서 쓴 것), **Qwen2.5-0.5B-Instruct** (이 노트를 위해 받음, 약 1 GB), **Pythia-160M** (약 375 MB, 3절에서 이유가 나온다).
- **측정 주의**: 측정하는 동안 이 Mac에서는 다른 작업이 병렬로 돌았다(load average 4~47로 크게 흔들림). GPU도 다른 프로세스가 간헐적으로 썼다(`ioreg`의 GPU Device Utilization이 측정하지 않는 순간에도 12~92%). 그래서 모든 속도는 `llama-bench`의 평균 ± 표준편차로 보이고, 여러 번 반복한 결과를 같이 적는다. **절대값보다 비율과 경향**을 보자. 팬이 없는 노트북이라 지속 부하에서 성능이 떨어지는 현상도 있었다(7.6절).

---

## 1. llama.cpp와 ggml은 무엇인가

### 1.1 두 개의 층

llama.cpp 저장소에는 사실 두 개의 라이브러리가 들어 있다.

- **ggml**: 텐서 라이브러리. C로 짠 "작은 PyTorch"라고 생각하면 된다. 텐서를 만들고, 연산을 이어 붙여 **계산 그래프**를 만들고, 그 그래프를 CPU·GPU 등 **backend**에서 실행한다. 양자화 형식(Q4_0, Q4_K …)과 그 형식을 위한 dot product 커널도 ggml 안에 있다. 학습 기능도 일부 있지만 주 용도는 추론이다.
- **libllama**: ggml 위에서 LLM을 돌리는 부분. 모델 구조별로 그래프를 조립하는 코드(Llama, Qwen2, GPT-NeoX, Gemma … 수십 종), GGUF 로더, tokenizer, KV-cache 관리, batch 처리, sampler가 여기 있다. 공개 API는 C 헤더 하나(`include/llama.h`)다.

그 위에 실행 파일들(tools)이 있다. `llama-cli`(대화형), `llama-server`(HTTP 서버), `llama-bench`(벤치마크), `llama-quantize`, `llama-perplexity` 등이다.

### 1.2 저장소 구조 — 직접 보기

무엇을 확인하나: 클론한 저장소의 최상위 구성과, ggml이 지원하는 backend 디렉터리를 실제로 본다.

```sh
cd .tools/llama.cpp
ls ggml/src | grep '^ggml-' | grep -v '\.' | tr '\n' ' '; echo   # 디렉터리 = backend
ls tools | tr '\n' ' '; echo
```

```text
ggml-blas ggml-cann ggml-cpu ggml-cuda ggml-et ggml-hexagon ggml-hip ggml-metal ggml-musa ggml-opencl ggml-openvino ggml-rpc ggml-sycl ggml-virtgpu ggml-vulkan ggml-webgpu ggml-zdnn ggml-zendnn 
batched-bench cli CMakeLists.txt completion cvector-generator export-lora fit-params gguf-split imatrix llama-bench mtmd perplexity quantize results rpc server tokenize tts tuning ui 
```

출력에서 볼 것: backend가 18개나 된다. 이 목록은 커밋마다 늘고 줄어든다(이 노트의 커밋 `f7b384c` 기준). 하나하나의 성숙도는 다르므로 "지원한다"와 "제품에 쓸 만하다"는 다른 말이다.

| 디렉터리 | 무엇이 있나 |
|---|---|
| `ggml/include/ggml.h` | 텐서·연산·그래프 API (3,000줄 남짓) |
| `ggml/src/ggml-cpu/` | CPU backend. ARM NEON/SVE, x86 AVX2/AVX-512 커널, 양자화 dot product, repack |
| `ggml/src/ggml-metal/` 등 | GPU·가속기 backend |
| `ggml/src/ggml-common.h` | 양자화 블록 구조체 정의 (`block_q4_0` 등) |
| `src/` | libllama: `llama-model.cpp`(구조별 그래프), `llama-kv-cache.cpp`, `llama-quant.cpp`(양자화 레시피), `llama-mmap.cpp`, `llama-sampler.cpp` … |
| `include/llama.h` | 앱이 쓰는 C API |
| `tools/` | `llama-cli`, `llama-server`, `llama-bench`, `llama-quantize`, `llama-perplexity` … |
| `examples/` | 최소 예제(`simple`), Android 앱(`llama.android`), Swift 예제 … |
| `convert_hf_to_gguf.py`, `gguf-py/` | HF → GGUF 변환기와 GGUF Python 라이브러리 |
| `docs/` | 빌드(`build.md`), Android(`android.md`), backend별 문서(`docs/backend/`) |

### 1.3 backend 목록 — 무엇이 어디를 담당하나

| backend | 대상 하드웨어 | 메모 |
|---|---|---|
| CPU | ARM (NEON, dotprod, i8mm, SVE, SME), x86 (AVX2, AVX-512, AMX) | 항상 있다. 다른 backend가 못 하는 연산의 fallback 장소 |
| Metal | Apple GPU (M 시리즈, A 시리즈) | 이 노트에서 측정 |
| BLAS | Accelerate, OpenBLAS 등 | 큰 행렬곱을 BLAS 라이브러리에 넘김 |
| CUDA / HIP / MUSA | NVIDIA / AMD / Moore Threads GPU | 서버·PC |
| Vulkan | Vulkan을 지원하는 거의 모든 GPU (모바일 포함) | 이식성이 장점 |
| OpenCL | Qualcomm **Adreno** GPU가 1차 목표 (`docs/backend/OPENCL.md`) | Snapdragon 8 Gen 3, 8 Elite 등이 문서에 나온다 |
| Hexagon | Qualcomm Hexagon NPU/DSP (`ggml/src/ggml-hexagon/`, `docs/backend/snapdragon/`) | 이 커밋에 존재. 성숙도는 직접 확인 필요 |
| SYCL, CANN, OpenVINO, WebGPU, zDNN … | Intel GPU, Huawei Ascend, Intel NPU/CPU, 브라우저, IBM Z … | |
| RPC | 다른 기기의 backend를 네트워크로 사용 | |

Hark 같은 Qualcomm SoC 기반 웨어러블이라면(추정 예시) 후보 경로는 **CPU(NEON) → OpenCL(Adreno GPU) → Hexagon(NPU)** 순서로 성숙도가 낮아진다. 그리고 Qualcomm 자체 스택(QNN, Genie — F4)과 비교해야 한다.

### 1.4 왜 edge에 잘 맞나

- **의존성이 거의 없다**: C/C++ 표준 라이브러리와 (선택적으로) 플랫폼 API만 쓴다. 정적 링크하면 실행 파일 하나로 끝난다. 펌웨어 엔지니어 입장에서는 "BSP에 Python 없이 넣을 수 있다"가 가장 큰 장점이다.
- **mmap 로딩**: GGUF는 텐서 데이터가 정렬된 채로 파일에 들어 있어서, 파일을 `mmap`하면 복사 없이 바로 가중치로 쓸 수 있다(4절, 8.3절). 로딩이 빠르고, OS가 필요할 때 페이지를 들인다.
- **양자화 형식이 풍부하다**: 1.5 bit부터 8 bit까지 수십 가지 형식과, 각 형식에 맞춘 SIMD 커널이 있다(3절).
- **같은 모델 파일이 모든 backend에서 돈다**: GGUF 하나로 Mac의 Metal, 안드로이드의 CPU/OpenCL, 서버의 CUDA에서 실행된다.
- **도구가 같이 온다**: 변환, 양자화, perplexity, 벤치마크가 모두 같은 저장소에 있어서 "모델 바꾸기 → 측정"의 반복이 빠르다.

반대로 약점도 알아 두자. NPU 전용 컴파일러(QNN, TFLite delegate)처럼 그래프 전체를 가속기용으로 미리 컴파일하는 방식은 아니다. 연산 하나하나를 backend 커널로 실행하므로, NPU에서 최고 효율을 내는 데는 벤더 스택이 유리할 수 있다. 면접에서는 "llama.cpp는 이식성과 반복 속도, 벤더 스택은 특정 칩에서의 효율"이라고 정리하면 된다.

---

## 2. HF 모델 → GGUF 변환

### 2.1 무엇을 하나

Hugging Face 체크포인트는 보통 세 종류의 파일로 이루어진다. 가중치(`model.safetensors`), 구조 설명(`config.json`), tokenizer(`tokenizer.json`, `tokenizer_config.json`). `convert_hf_to_gguf.py`는 이 셋을 읽어서 **GGUF 파일 하나**로 합친다. 하는 일은 네 가지다.

1. `config.json`을 읽어 구조(architecture)를 판별하고, 하이퍼파라미터(층 수, hidden 크기, head 수 …)를 GGUF 메타데이터로 쓴다.
2. 텐서 이름을 llama.cpp 규칙으로 바꾼다. 예: `model.layers.0.self_attn.q_proj.weight` → `blk.0.attn_q.weight`.
3. dtype을 바꾼다. `--outtype f16`이면 큰 행렬은 F16으로, norm 가중치처럼 작은 1차원 텐서는 F32로 둔다.
4. tokenizer의 어휘, merge 규칙, 특수 토큰, chat template까지 메타데이터에 넣는다. 그래서 GGUF 파일 하나만 있으면 tokenizer까지 다 된다.

### 2.2 직접 해 보기

무엇을 확인하나: 로컬 HF 캐시의 SmolLM2-135M-Instruct를 F16 GGUF로 변환하고, 로그에서 이름 변환·dtype 변환·메타데이터를 확인한다.

```sh
SNAP=$(ls -d ~/.cache/huggingface/hub/models--HuggingFaceTB--SmolLM2-135M-Instruct/snapshots/*)
cd .tools/llama.cpp
../../.venv/bin/python convert_hf_to_gguf.py $SNAP --outtype f16 \
    --outfile ../models/smollm2-135m-f16.gguf
```

```text
INFO:hf-to-gguf:Model architecture: LlamaForCausalLM
INFO:hf-to-gguf:Exporting model...
INFO:hf-to-gguf:token_embd.weight,           torch.bfloat16 --> F16, shape = {576, 49152}
INFO:hf-to-gguf:blk.0.attn_norm.weight,      torch.bfloat16 --> F32, shape = {576}
INFO:hf-to-gguf:blk.0.ffn_down.weight,       torch.bfloat16 --> F16, shape = {1536, 576}
INFO:hf-to-gguf:blk.0.attn_k.weight,         torch.bfloat16 --> F16, shape = {576, 192}
INFO:hf-to-gguf:blk.0.attn_q.weight,         torch.bfloat16 --> F16, shape = {576, 576}
  ... (blk.0의 나머지 5개, blk.1 ~ blk.29 반복)
INFO:hf-to-gguf:output_norm.weight,          torch.bfloat16 --> F32, shape = {576}
INFO:hf-to-gguf:gguf: context length = 8192
INFO:hf-to-gguf:gguf: embedding length = 576
INFO:hf-to-gguf:gguf: feed forward length = 1536
INFO:hf-to-gguf:gguf: head count = 9
INFO:hf-to-gguf:gguf: key-value head count = 3
INFO:hf-to-gguf:gguf: file type = 1
INFO:gguf.vocab:Adding 48900 merge(s).
INFO:gguf.vocab:Setting special token type bos to 1
INFO:gguf.vocab:Setting special token type eos to 2
INFO:gguf.gguf_writer:../models/smollm2-135m-f16.gguf: n_tensors = 272, total_size = 269.1M
INFO:hf-to-gguf:Model successfully exported to ../models/smollm2-135m-f16.gguf
```

출력에서 볼 것:

- **원본은 bf16**(`torch.bfloat16`)이다. `--outtype f16`으로 F16이 되었다. bf16 → f16은 지수 범위가 좁아지는 변환이라 아주 큰 값이 있으면 문제가 될 수 있다. `--outtype bf16`이나 `f32`도 고를 수 있다(C1에서 본 형식 차이).
- **norm 가중치는 F32**로 남는다. 원소 576개짜리라 크기에 거의 영향이 없고, 정밀도를 지키는 쪽이 낫다.
- **shape 표기가 `{576, 49152}`** 로, PyTorch의 `[49152, 576]`과 순서가 반대다. ggml은 **가장 빨리 변하는(연속된) 차원을 먼저** 쓴다(`ne[0]`). C 배열로 치면 `float w[49152][576]`에서 안쪽 차원 576이 `ne[0]`이다. 8.1절에서 C로 확인한다.
- **텐서 개수 272를 손으로 맞춰 보자**: 층마다 attn_norm, attn_q, attn_k, attn_v, attn_output, ffn_norm, ffn_gate, ffn_up, ffn_down = 9개. 9 × 30층 = 270. 여기에 token_embd, output_norm을 더하면 272. **output.weight(lm_head)가 없다**. SmolLM2는 입력 embedding과 출력 projection이 같은 행렬을 쓰는 **tied embedding**이기 때문이다(B8). 이 사실이 3절과 7절에서 중요해진다.
- `file type = 1`은 "대부분 F16"이라는 뜻이다(나중에 Q4_K_M 파일은 15).

같은 방법으로 Qwen2.5-0.5B와 Pythia-160M도 변환했다. 파일 크기는 다음과 같다.

| 모델 | 구조 (`general.architecture`) | 파라미터 | GGUF F16 | 비고 |
|---|---|---|---|---|
| SmolLM2-135M-Instruct | llama | 135 M | 271 MB (F32로는 540 MB) | tied embedding, d = 576 |
| Qwen2.5-0.5B-Instruct | qwen2 | 494 M | 994 MB | tied embedding, d = 896, vocab 151,936 |
| Pythia-160M | gptneox | 162 M | 327 MB | 출력 행렬 따로 있음, d = 768 |

### 2.3 함정

- **새 구조는 변환기가 모를 수 있다.** `config.json`의 `architectures`가 변환기에 등록되어 있지 않으면 실패한다. 최신 모델을 쓰려면 llama.cpp도 최신으로 받아야 한다.
- **tokenizer의 pre-tokenizer**: BPE tokenizer는 텍스트를 먼저 정규식으로 쪼개는 단계(pre-tokenization)가 모델마다 다르다. 변환기는 tokenizer 출력의 해시로 이것을 알아내서 `tokenizer.ggml.pre`(SmolLM2는 `smollm`)에 적는다. 모르는 tokenizer면 경고를 내거나 실패한다. 이걸 무시하면 **추론은 되는데 출력이 미묘하게 나빠지는** 최악의 버그가 된다. 펌웨어로 치면 "통신은 되는데 엔디안이 틀린" 상황이다.
- **F16 오버플로**: bf16 원본에 F16 범위(약 ±65504)를 넘는 값이 있으면 inf가 된다. 의심되면 `--outtype bf16` 또는 `f32`로 변환해서 perplexity를 비교한다.

---

## 3. 양자화 — `llama-quantize`

### 3.1 형식 목록 읽기

`llama-quantize`는 F16(또는 F32) GGUF를 읽어서 텐서별로 양자화한 새 GGUF를 쓴다. 지원 형식은 `--help`에 나온다. 아래는 이 빌드의 출력 중 일부다. 오른쪽 숫자는 Llama-3-8B 기준 파일 크기와 perplexity 증가량으로, 프로젝트가 참고용으로 적어 둔 값이다.

```text
   2  or  Q4_0    :  4.34G, +0.4685 ppl @ Llama-3-8B
   3  or  Q4_1    :  4.78G, +0.4511 ppl @ Llama-3-8B
   8  or  Q5_0    :  5.21G, +0.1316 ppl @ Llama-3-8B
  10  or  Q2_K    :  2.96G, +3.5199 ppl @ Llama-3-8B
  12  or  Q3_K_M  :  3.74G, +0.6569 ppl @ Llama-3-8B
  25  or  IQ4_NL  :  4.50 bpw non-linear quantization
  30  or  IQ4_XS  :  4.25 bpw non-linear quantization
  14  or  Q4_K_S  :  4.37G, +0.2689 ppl @ Llama-3-8B
  15  or  Q4_K_M  :  4.58G, +0.1754 ppl @ Llama-3-8B
  17  or  Q5_K_M  :  5.33G, +0.0569 ppl @ Llama-3-8B
  18  or  Q6_K    :  6.14G, +0.0217 ppl @ Llama-3-8B
   7  or  Q8_0    :  7.96G, +0.0026 ppl @ Llama-3-8B
   1  or  F16     : 14.00G, +0.0020 ppl @ Mistral-7B
```

이름 읽는 법은 C3 8.1절에서 봤다. 다시 짧게 정리하면:

| 계열 | 예 | 블록 | 특징 |
|---|---|---|---|
| legacy | Q4_0, Q4_1, Q5_0, Q8_0 | 32개 | 블록마다 FP16 scale 1개(`_0`) 또는 scale + min(`_1`). 단순하고 빠르다 |
| k-quant | Q2_K, Q3_K, Q4_K, Q5_K, Q6_K | **256개 super-block** | super-block 안에 sub-block별 작은 scale을 또 둔다(2단계 scale). 같은 bit에서 오차가 작다 |
| k-quant 레시피 | Q4_K_S, **Q4_K_M**, Q5_K_M … | | 텐서 종류·층마다 형식을 **섞는** 규칙. `_S` < `_M` < `_L` 순으로 높은 bit를 더 많이 쓴다 |
| i-quant | IQ2_XXS … IQ4_XS | | 코드북·비선형 양자화. 2~3 bit대에서 강하다. imatrix(중요도 행렬)와 같이 쓰는 것이 보통 |

블록 하나의 바이트 수는 `ggml/src/ggml-common.h`의 `static_assert`에 그대로 적혀 있다. 손으로 bit/weight를 계산해 보자(QK_K = 256).

```
Q8_0 : 2 B(FP16 d) + 32 B            = 34 B / 32  → 8.5   bit/w
Q4_0 : 2 B + 16 B                    = 18 B / 32  → 4.5   bit/w
Q5_0 : 2 B + 4 B(5번째 bit) + 16 B   = 22 B / 32  → 5.5   bit/w
Q4_K : 2·2 B(d, dmin) + 12 B(scale) + 128 B = 144 B / 256 → 4.5    bit/w
Q6_K : 2 B + 16 B + 192 B            = 210 B / 256 → 6.5625 bit/w
Q3_K : 2 B + 64 B + 32 B + 12 B      = 110 B / 256 → 3.4375 bit/w
Q2_K : 2·2 B + 16 B + 64 B           = 84 B / 256  → 2.625  bit/w
```

말로 하면: "4-bit"라고 불러도 scale 때문에 실제로는 4.5 bit이고, Q2_K도 2.6 bit다. 이 숫자가 아래 실측 파일 크기를 설명하는 출발점이다.

### 3.2 직접 해 보기 — 5가지 형식으로 양자화

무엇을 확인하나: 세 모델을 Q8_0, Q6_K, Q4_K_M, Q4_0, Q2_K로 양자화하고, 로그 마지막 줄의 크기·평균 bit/weight를 모은다.

```sh
cd .tools/models
for q in Q8_0 Q6_K Q4_K_M Q4_0 Q2_K; do
  ../llama.cpp/build/bin/llama-quantize qwen2.5-0.5b-f16.gguf qwen2.5-0.5b-$q.gguf $q
done
```

```text
(Qwen2.5-0.5B, Q4_K_M 실행의 마지막 부분)
llama_model_quantize_impl: model size  =   942.43 MiB (16.00 BPW)
llama_model_quantize_impl: quant size  =   373.71 MiB (6.35 BPW)
llama_model_quantize_impl: WARNING: 144 of 290 tensor(s) required fallback quantization
llama_quantize: quantize time =  4389.33 ms
```

세 모델 × 다섯 형식의 결과를 표로 모았다(`quant size` 줄, MiB = 2²⁰ 바이트. BPW = 파일 안 텐서들의 평균 bit/weight).

| 형식 | 이론 bit/w | SmolLM2-135M | Qwen2.5-0.5B | Pythia-160M |
|---|---|---|---|---|
| F16 | 16 | 256.6 MiB (16.00) | 942.4 MiB (16.00) | 309.8 MiB (16.01) |
| Q8_0 | 8.5 | 136.4 MiB (8.51) | 500.8 MiB (8.50) | 164.8 MiB (8.52) |
| Q6_K | 6.56 | 130.3 MiB (**8.12**) | 476.6 MiB (**8.09**) | 127.4 MiB (6.58) |
| Q4_K_M | 4.5 + 일부 6.56 | 98.9 MiB (**6.17**) | 373.7 MiB (**6.35**) | 103.1 MiB (5.33) |
| Q4_0 | 4.5 | 85.8 MiB (5.35) | 330.2 MiB (5.61) | 97.0 MiB (5.01) |
| Q2_K | 2.6 + 일부 3.4 | 82.4 MiB (**5.14**) | 317.3 MiB (**5.39**) | 74.8 MiB (3.87) |
| fallback 경고 | | 180 / 272 텐서 | 144 / 290 텐서 | 0 |

표에서 이상한 점이 두 가지 보인다.

1. **Q4_0인데도 5.35~5.61 bit**다. 4.5여야 하는데?
2. **SmolLM2와 Qwen의 Q6_K·Q4_K_M·Q2_K가 이론보다 훨씬 크다.** Q2_K가 Q4_0과 거의 같은 크기다. Pythia는 이론과 잘 맞는다.

이 둘을 차례로 풀어 보자. 둘 다 실무에서 "4-bit로 줄였는데 왜 생각보다 크지?"라는 질문의 정답이다.

### 3.3 첫 번째 수수께끼 — tied embedding이 Q8_0으로 남는다

Q4_0 파일의 텐서별 형식을 세어 보면(4.4절의 스크립트) 바로 보인다.

```text
(qwen2.5-0.5b-Q4_0.gguf)
token_embd.weight  Q8_0  [896, 151936]
Q4_0  텐서 168개  원소  357.8M   192.0 MiB   4.50 bit/w
Q8_0  텐서   1개  원소  136.1M   137.9 MiB   8.50 bit/w
F32   텐서 121개  원소    0.1M     0.3 MiB  32.00 bit/w
합계 330.2 MiB, 평균 5.61 bit/weight
```

**embedding 행렬 하나가 파일의 42%(137.9 / 330.2 MiB)**를 차지하고, 그것이 Q8_0이다. 이유는 `src/llama-quant.cpp`에 적혀 있다. 주석을 그대로 옮기면 "for arches that share the same tensor between the token embeddings and the output, we quantize the token embeddings with the quantization of the output tensor". 출력 projection은 품질에 민감해서 llama.cpp는 기본적으로 Q6_K 이상을 쓰는데, tied embedding이면 token_embd가 곧 출력 행렬이므로 같은 대접을 받는다. 그런데 Q6_K는 256 블록이라 d = 896에 맞지 않아 Q8_0으로 내려간다(다음 절).

손으로 확인: Qwen2.5-0.5B의 vocab × d = 151,936 × 896 = 136.1 M 원소. 전체 494 M 파라미터의 **28%**가 embedding이다. 작은 LLM일수록 vocab이 차지하는 비율이 크다(B8). 그래서 "파라미터 × 4.5 bit"로 어림하면 494 M × 4.5 / 8 = 278 MB인데 실제는 346 MB(330 MiB)다.

웨어러블 관점의 교훈: **작은 모델에서는 embedding/출력 행렬이 메모리와 decode 대역폭의 큰 몫**이다. tied embedding이면 decode 때마다 출력 projection으로 이 행렬 전체를 읽는다(7절). `--token-embedding-type`, `--output-tensor-type` 옵션으로 이 텐서만 따로 형식을 정할 수 있다.

### 3.4 두 번째 수수께끼 — 256으로 안 나눠지면 fallback

Q4_K_M 로그의 경고 줄을 보자.

```text
(SmolLM2-135M, Q4_K_M)
warning: blk.29.attn_k.weight                 - ncols    576 not divisible by 256 (required for type    q4_K) -> falling back to    q5_0
warning: blk.29.attn_v.weight                 - ncols    576 not divisible by 256 (required for type    q6_K) -> falling back to    q8_0
warning: blk.29.ffn_gate.weight               - ncols    576 not divisible by 256 (required for type    q4_K) -> falling back to    q5_0
llama_model_quantize_impl: quant size  =    98.87 MiB (6.17 BPW)
llama_model_quantize_impl: WARNING: 180 of 272 tensor(s) required fallback quantization
```

k-quant는 **행(row) 하나를 256개짜리 super-block으로 자른다**. 행 길이(ne[0] = ncols)가 256의 배수가 아니면 블록이 행 경계를 넘어가므로 쓸 수 없다. SmolLM2의 d = 576 = 2.25 × 256, Qwen2.5-0.5B의 d = 896 = 3.5 × 256이라 **입력 차원이 d인 모든 행렬**(q, k, v, o, gate, up)이 k-quant를 쓸 수 없다. 반면 ffn_down은 입력 차원이 FFN 크기(SmolLM2 1536 = 6 × 256, Qwen 4864 = 19 × 256)라서 k-quant가 들어간다.

fallback 규칙은 `src/llama-quant.cpp`의 `tensor_type_fallback()`에 있다(이 커밋 기준).

| 원래 목표 | fallback | bit/w 변화 |
|---|---|---|
| Q2_K, Q3_K | Q4_0 | 2.6~3.4 → 4.5 (더 커짐) |
| Q4_K | Q5_0 | 4.5 → 5.5 |
| Q5_K | Q5_1 | 5.5 → 6.0 |
| Q6_K | Q8_0 | 6.56 → 8.5 |
| IQ 계열 | IQ4_NL | |
| 32로도 안 나눠지면 | F16 | |

그래서 이 두 모델에서 "Q2_K"는 사실상 "Q4_0 + ffn_down만 Q3_K"이고, "Q4_K_M"은 사실상 "Q5_0 + 일부 Q6_K/Q8_0"이다. 이름만 보고 판단하면 틀린다. **Pythia-160M을 추가로 받은 이유가 이것이다.** d = 768 = 3 × 256, FFN 3072 = 12 × 256이라 k-quant가 원래 설계대로 들어간다. 진짜 Q4_K_M과 Q4_0을 비교하려면 이런 모델이 필요하다.

펌웨어 비유: DMA 엔진이 "전송 길이는 64바이트의 배수"를 요구하는데 버퍼가 그렇지 않으면 드라이버가 조용히 PIO로 fallback하는 것과 같다. 동작은 하지만 성능 특성이 바뀌고, 로그를 보지 않으면 모른다. **양자화 로그의 fallback 경고 수는 반드시 확인하는 숫자**다.

### 3.5 Q4_K_M의 실제 레시피 — 어떤 텐서가 더 많은 bit를 받나

`Q4_K_M`의 기본 형식은 Q4_K다. 여기에 `src/llama-quant.cpp`의 규칙이 텐서 종류별로 형식을 올린다. 이 커밋에서 확인한 주요 규칙은 이렇다(아키텍처·전문가 수에 따른 예외는 생략).

- 출력 행렬(`output.weight`, 또는 tied면 `token_embd`): Q6_K.
- `attn_v`: `use_more_bits(i, n)`인 층이면 Q6_K.
- `ffn_down`: `use_more_bits(i, n)`인 층이면 Q6_K.
- 나머지: Q4_K.

`use_more_bits`는 짧은 람다다. 그대로 Python으로 옮겨서 어떤 층이 뽑히는지 계산해 보자.

무엇을 확인하나: 층 수에 따라 "더 많은 bit를 받는 층"이 어디인지 계산한다.

```python
def use_more_bits(i, n):           # src/llama-quant.cpp 의 람다를 그대로 옮김 (정수 나눗셈)
    return i < n // 8 or i >= 7 * n // 8 or (i - n // 8) % 3 == 2
for name, n in [("pythia-160m", 12), ("qwen2.5-0.5b", 24), ("smollm2-135m", 30)]:
    sel = [i for i in range(n) if use_more_bits(i, n)]
    print(f"{name:13s} n_layer={n:2d}  더 많은 bit 받는 층 {len(sel):2d}개: {sel}")
```

```text
pythia-160m   n_layer=12  더 많은 bit 받는 층  6개: [0, 3, 6, 9, 10, 11]
qwen2.5-0.5b  n_layer=24  더 많은 bit 받는 층 12개: [0, 1, 2, 5, 8, 11, 14, 17, 20, 21, 22, 23]
smollm2-135m  n_layer=30  더 많은 bit 받는 층 14개: [0, 1, 2, 5, 8, 11, 14, 17, 20, 23, 26, 27, 28, 29]
```

출력에서 볼 것: **처음 1/8 층, 마지막 1/8 층, 그리고 가운데는 3층마다 하나**다. 대략 절반의 층에서 attn_v와 ffn_down이 Q6_K가 된다. 첫·마지막 층이 양자화에 민감하다는 경험칙을 코드로 박아 둔 것이다(C3에서 본 "층마다 민감도가 다르다"의 실무판).

이제 실제 파일과 맞는지 GGUF를 열어 텐서별 형식을 보자(스크립트는 4.4절). 아래 그림은 두 모델의 Q4_K_M 파일에서 층 × 텐서마다 실제 형식을 칠한 것이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 352"><text x="10" y="20" font-size="13">SmolLM2-135M Q4_K_M (d=576 → 256으로 안 나눠짐 → fallback)</text><text x="105" y="45" font-size="12" text-anchor="end">attn_q</text><rect x="110" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="128" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="146" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="164" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="182" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="200" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="218" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="236" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="254" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="272" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="290" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="308" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="326" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="344" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="362" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="380" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="398" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="416" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="434" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="452" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="470" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="488" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="506" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="524" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="542" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="560" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="578" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="596" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="614" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="632" y="32" width="16" height="16" fill="#888" fill-opacity="0.8"/><text x="105" y="63" font-size="12" text-anchor="end">attn_k</text><rect x="110" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="128" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="146" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="164" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="182" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="200" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="218" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="236" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="254" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="272" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="290" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="308" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="326" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="344" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="362" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="380" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="398" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="416" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="434" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="452" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="470" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="488" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="506" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="524" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="542" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="560" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="578" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="596" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="614" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="632" y="50" width="16" height="16" fill="#888" fill-opacity="0.8"/><text x="105" y="81" font-size="12" text-anchor="end">attn_v</text><rect x="110" y="68" width="16" height="16" fill="#d0564a" fill-opacity="0.8"/><rect x="128" y="68" width="16" height="16" fill="#d0564a" fill-opacity="0.8"/><rect x="146" y="68" width="16" height="16" fill="#d0564a" fill-opacity="0.8"/><rect x="164" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="182" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="200" y="68" width="16" height="16" fill="#d0564a" fill-opacity="0.8"/><rect x="218" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="236" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="254" y="68" width="16" height="16" fill="#d0564a" fill-opacity="0.8"/><rect x="272" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="290" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="308" y="68" width="16" height="16" fill="#d0564a" fill-opacity="0.8"/><rect x="326" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="344" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="362" y="68" width="16" height="16" fill="#d0564a" fill-opacity="0.8"/><rect x="380" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="398" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="416" y="68" width="16" height="16" fill="#d0564a" fill-opacity="0.8"/><rect x="434" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="452" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="470" y="68" width="16" height="16" fill="#d0564a" fill-opacity="0.8"/><rect x="488" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="506" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="524" y="68" width="16" height="16" fill="#d0564a" fill-opacity="0.8"/><rect x="542" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="560" y="68" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="578" y="68" width="16" height="16" fill="#d0564a" fill-opacity="0.8"/><rect x="596" y="68" width="16" height="16" fill="#d0564a" fill-opacity="0.8"/><rect x="614" y="68" width="16" height="16" fill="#d0564a" fill-opacity="0.8"/><rect x="632" y="68" width="16" height="16" fill="#d0564a" fill-opacity="0.8"/><text x="105" y="99" font-size="12" text-anchor="end">attn_output</text><rect x="110" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="128" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="146" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="164" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="182" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="200" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="218" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="236" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="254" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="272" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="290" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="308" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="326" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="344" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="362" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="380" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="398" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="416" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="434" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="452" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="470" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="488" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="506" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="524" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="542" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="560" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="578" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="596" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="614" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="632" y="86" width="16" height="16" fill="#888" fill-opacity="0.8"/><text x="105" y="117" font-size="12" text-anchor="end">ffn_gate</text><rect x="110" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="128" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="146" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="164" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="182" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="200" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="218" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="236" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="254" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="272" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="290" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="308" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="326" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="344" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="362" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="380" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="398" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="416" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="434" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="452" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="470" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="488" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="506" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="524" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="542" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="560" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="578" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="596" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="614" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="632" y="104" width="16" height="16" fill="#888" fill-opacity="0.8"/><text x="105" y="135" font-size="12" text-anchor="end">ffn_up</text><rect x="110" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="128" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="146" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="164" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="182" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="200" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="218" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="236" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="254" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="272" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="290" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="308" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="326" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="344" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="362" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="380" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="398" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="416" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="434" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="452" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="470" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="488" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="506" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="524" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="542" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="560" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="578" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="596" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="614" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><rect x="632" y="122" width="16" height="16" fill="#888" fill-opacity="0.8"/><text x="105" y="153" font-size="12" text-anchor="end">ffn_down</text><rect x="110" y="140" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="128" y="140" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="146" y="140" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="164" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="182" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="200" y="140" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="218" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="236" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="254" y="140" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="272" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="290" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="308" y="140" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="326" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="344" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="362" y="140" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="380" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="398" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="416" y="140" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="434" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="452" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="470" y="140" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="488" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="506" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="524" y="140" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="542" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="560" y="140" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="578" y="140" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="596" y="140" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="614" y="140" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="632" y="140" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><text x="118" y="172" font-size="12" text-anchor="middle">0</text><text x="208" y="172" font-size="12" text-anchor="middle">5</text><text x="298" y="172" font-size="12" text-anchor="middle">10</text><text x="388" y="172" font-size="12" text-anchor="middle">15</text><text x="478" y="172" font-size="12" text-anchor="middle">20</text><text x="568" y="172" font-size="12" text-anchor="middle">25</text><text x="10" y="202" font-size="13">Pythia-160M Q4_K_M (d=768 = 3×256 → K-quant 그대로)</text><text x="105" y="227" font-size="12" text-anchor="end">attn_qkv</text><rect x="110" y="214" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="128" y="214" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="146" y="214" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="164" y="214" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="182" y="214" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="200" y="214" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="218" y="214" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="236" y="214" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="254" y="214" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="272" y="214" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="290" y="214" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="308" y="214" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><text x="105" y="245" font-size="12" text-anchor="end">attn_output</text><rect x="110" y="232" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="128" y="232" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="146" y="232" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="164" y="232" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="182" y="232" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="200" y="232" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="218" y="232" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="236" y="232" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="254" y="232" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="272" y="232" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="290" y="232" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="308" y="232" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><text x="105" y="263" font-size="12" text-anchor="end">ffn_up</text><rect x="110" y="250" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="128" y="250" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="146" y="250" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="164" y="250" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="182" y="250" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="200" y="250" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="218" y="250" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="236" y="250" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="254" y="250" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="272" y="250" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="290" y="250" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="308" y="250" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><text x="105" y="281" font-size="12" text-anchor="end">ffn_down</text><rect x="110" y="268" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="128" y="268" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="146" y="268" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="164" y="268" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="182" y="268" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="200" y="268" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="218" y="268" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="236" y="268" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="254" y="268" width="16" height="16" fill="#4a7bd0" fill-opacity="0.8"/><rect x="272" y="268" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="290" y="268" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><rect x="308" y="268" width="16" height="16" fill="#e08a3c" fill-opacity="0.8"/><text x="118" y="300" font-size="12" text-anchor="middle">0</text><text x="136" y="300" font-size="12" text-anchor="middle">1</text><text x="154" y="300" font-size="12" text-anchor="middle">2</text><text x="172" y="300" font-size="12" text-anchor="middle">3</text><text x="190" y="300" font-size="12" text-anchor="middle">4</text><text x="208" y="300" font-size="12" text-anchor="middle">5</text><text x="226" y="300" font-size="12" text-anchor="middle">6</text><text x="244" y="300" font-size="12" text-anchor="middle">7</text><text x="262" y="300" font-size="12" text-anchor="middle">8</text><text x="280" y="300" font-size="12" text-anchor="middle">9</text><text x="298" y="300" font-size="12" text-anchor="middle">10</text><text x="316" y="300" font-size="12" text-anchor="middle">11</text><rect x="110" y="326" width="14" height="14" fill="#4a7bd0" fill-opacity="0.8"/><text x="130" y="338" font-size="12">Q4_K</text><rect x="200" y="326" width="14" height="14" fill="#888" fill-opacity="0.8"/><text x="220" y="338" font-size="12">Q5_0</text><rect x="290" y="326" width="14" height="14" fill="#e08a3c" fill-opacity="0.8"/><text x="310" y="338" font-size="12">Q6_K</text><rect x="380" y="326" width="14" height="14" fill="#d0564a" fill-opacity="0.8"/><text x="400" y="338" font-size="12">Q8_0</text><text x="480" y="338" font-size="12">가로축 = layer 번호</text></svg>
```

그림 2 — Q4_K_M 파일의 실제 텐서별 형식(GGUFReader로 읽어서 그림). 위: SmolLM2는 d = 576이 256으로 나눠지지 않아 Q4_K 자리가 Q5_0(회색)으로, Q6_K 자리가 Q8_0(빨강)으로 바뀌었다. k-quant가 살아남은 것은 ffn_down(입력 1536)뿐이다. 아래: Pythia는 레시피 그대로 — 층 0, 3, 6, 9, 10, 11의 attn_qkv와 ffn_down이 Q6_K(주황), 나머지는 Q4_K(파랑). Pythia는 q, k, v가 하나로 합쳐진 `attn_qkv` 텐서인데, 이 빌드에서는 attn_v와 같은 규칙을 받았다.

Pythia의 형식별 합계도 계산과 맞는다. Q6_K 텐서 13개 = attn_qkv 6 + ffn_down 6 + output 1.

```text
(pythia-160m-Q4_K_M.gguf)
output.weight      Q6_K  [768, 50304]
token_embd.weight  Q4_K  [768, 50304]
Q4_K  텐서  37개  원소   98.8M    53.0 MiB   4.50 bit/w
Q6_K  텐서  13개  원소   63.4M    49.6 MiB   6.56 bit/w
F32   텐서  98개  원소    0.1M     0.5 MiB  32.00 bit/w
합계 103.1 MiB, 평균 5.33 bit/weight
```

Pythia는 tied가 아니어서 입력 embedding은 Q4_K, 출력은 Q6_K다. 출력 행렬 하나(38.6 M 원소)가 Q6_K 전체 바이트의 60%를 차지한다. 결국 **"Q4_K_M = 4.5 bit"가 아니라, 작은 모델에서는 5.3~6.4 bit**가 된다.

### 3.6 imatrix — 한 단계 더

`llama-imatrix` 도구로 calibration 텍스트를 돌려서 채널별 중요도(입력 활성값의 제곱 평균)를 모으고, `llama-quantize --imatrix 파일`로 넘기면 양자화가 그 중요도를 가중치로 써서 오차를 줄인다. C3의 AWQ와 같은 "activation-aware" 발상이다. 2~3 bit대(IQ2, IQ3, Q2_K)에서는 사실상 필수이고, 4 bit 이상에서는 이득이 작다. 이 노트에서는 실행하지 않았다.

### 3.7 함정

- **이미 양자화된 파일을 다시 양자화하지 않는다.** `--allow-requantize`가 있지만 help에 "this can severely reduce quality"라고 경고한다. 항상 F16/BF16/F32에서 출발한다.
- **파일 이름의 형식 이름을 믿지 말고 로그를 본다.** fallback 경고 수와 `quant size`의 BPW를 확인한다.
- **embedding/출력 형식이 크기를 좌우할 수 있다.** 작은 모델일수록 그렇다.

---

## 4. GGUF 파일 포맷 — 바이트 단위로 읽기

### 4.1 직관: 펌웨어 이미지와 같은 구조

GGUF("GGML Universal File"이라고 흔히 풀어 쓴다)는 **자기 설명적인(self-describing) 바이너리 파일**이다. 파일 하나 안에 "이 모델이 무엇인지"(메타데이터)와 "가중치 텐서들이 어디에 어떤 형식으로 있는지"(텐서 목록)와 실제 가중치 바이트가 모두 들어 있다. 펌웨어 엔지니어에게 익숙한 이미지 포맷 — magic, 버전, 헤더 테이블, 정렬된 섹션 — 과 같은 설계다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 330"><text x="20" y="20" font-size="13">smollm2-135m-Q4_K_M.gguf (105,454,080 B) — 폭은 비례가 아님</text><g stroke="currentColor" stroke-width="1"><rect x="20" y="32" width="56" height="44" fill="#d0564a" fill-opacity="0.25"/><rect x="76" y="32" width="230" height="44" fill="#e08a3c" fill-opacity="0.25"/><rect x="306" y="32" width="110" height="44" fill="#3f9a6b" fill-opacity="0.25"/><rect x="416" y="32" width="10" height="44" fill="#888" fill-opacity="0.3"/><rect x="426" y="32" width="234" height="44" fill="#4a7bd0" fill-opacity="0.25"/></g><text x="48" y="52" font-size="12" text-anchor="middle">header</text><text x="48" y="68" font-size="12" text-anchor="middle">24 B</text><text x="191" y="52" font-size="12" text-anchor="middle">메타데이터 KV 31개</text><text x="191" y="68" font-size="12" text-anchor="middle">1.77 MB (대부분 tokenizer)</text><text x="361" y="52" font-size="12" text-anchor="middle">tensor info</text><text x="361" y="68" font-size="12" text-anchor="middle">272개, 16 KB</text><text x="543" y="52" font-size="12" text-anchor="middle">tensor data</text><text x="543" y="68" font-size="12" text-anchor="middle">103.7 MB, 32 B 정렬</text><text x="20" y="92" font-size="12">0</text><text x="76" y="92" font-size="12">24</text><text x="306" y="92" font-size="12" text-anchor="middle">1,769,432</text><text x="430" y="106" font-size="12">1,785,600 = data_offset (32의 배수)</text><text x="421" y="92" font-size="12">↑ pad</text><text x="20" y="140" font-size="12">header</text><g stroke="currentColor" stroke-width="1"><rect x="90" y="124" width="110" height="26" fill="#d0564a" fill-opacity="0.15"/><rect x="200" y="124" width="110" height="26" fill="#d0564a" fill-opacity="0.15"/><rect x="310" y="124" width="170" height="26" fill="#d0564a" fill-opacity="0.15"/><rect x="480" y="124" width="170" height="26" fill="#d0564a" fill-opacity="0.15"/></g><text x="145" y="141" font-size="12" text-anchor="middle">magic "GGUF"</text><text x="255" y="141" font-size="12" text-anchor="middle">version u32 = 3</text><text x="395" y="141" font-size="12" text-anchor="middle">n_tensors u64 = 272</text><text x="565" y="141" font-size="12" text-anchor="middle">n_kv u64 = 31</text><text x="20" y="196" font-size="12">KV 1개</text><g stroke="currentColor" stroke-width="1"><rect x="90" y="180" width="120" height="26" fill="#e08a3c" fill-opacity="0.15"/><rect x="210" y="180" width="170" height="26" fill="#e08a3c" fill-opacity="0.15"/><rect x="380" y="180" width="120" height="26" fill="#e08a3c" fill-opacity="0.15"/><rect x="500" y="180" width="150" height="26" fill="#e08a3c" fill-opacity="0.15"/></g><text x="150" y="197" font-size="12" text-anchor="middle">key 길이 u64=20</text><text x="295" y="197" font-size="12" text-anchor="middle">"general.architecture"</text><text x="440" y="197" font-size="12" text-anchor="middle">type u32 = 8</text><text x="575" y="197" font-size="12" text-anchor="middle">값: 길이 + "llama"</text><text x="20" y="252" font-size="12">tensor</text><text x="20" y="268" font-size="12">info 1개</text><g stroke="currentColor" stroke-width="1"><rect x="90" y="240" width="150" height="26" fill="#3f9a6b" fill-opacity="0.15"/><rect x="240" y="240" width="90" height="26" fill="#3f9a6b" fill-opacity="0.15"/><rect x="330" y="240" width="130" height="26" fill="#3f9a6b" fill-opacity="0.15"/><rect x="460" y="240" width="80" height="26" fill="#3f9a6b" fill-opacity="0.15"/><rect x="540" y="240" width="110" height="26" fill="#3f9a6b" fill-opacity="0.15"/></g><text x="165" y="257" font-size="12" text-anchor="middle">이름 (길이+문자열)</text><text x="285" y="257" font-size="12" text-anchor="middle">n_dims u32</text><text x="395" y="257" font-size="12" text-anchor="middle">ne[0..n-1] u64</text><text x="500" y="257" font-size="12" text-anchor="middle">type u32</text><text x="595" y="257" font-size="12" text-anchor="middle">offset u64</text><text x="90" y="296" font-size="12">offset은 data_offset 기준 상대 위치이고 alignment(기본 32)의 배수 → mmap한 주소 + offset이</text><text x="90" y="314" font-size="12">곧 텐서 포인터. 복사·파싱 없이 바로 쓸 수 있다 (little-endian, 버전 3 기준)</text></svg>
```

그림 3 — GGUF 파일의 배치. header(빨강) → 메타데이터 KV(주황) → tensor info 목록(초록) → padding → 정렬된 tensor data(파랑). 아래 세 줄은 각 영역의 레코드 하나를 확대한 것이다. 숫자는 이 노트의 SmolLM2 Q4_K_M 파일에서 4.2~4.4절로 실제로 읽은 값이다.

### 4.2 코드로 확인: header를 `struct`로 직접 파싱

무엇을 확인하나: 라이브러리 없이 파일 앞 64바이트만 읽어서 magic, 버전, 텐서 수, KV 수, 그리고 첫 번째 KV의 key를 꺼낸다. 펌웨어에서 이미지 헤더를 파싱하던 것과 똑같다.

```python
import struct
path = ".tools/models/smollm2-135m-Q4_K_M.gguf"
with open(path, "rb") as f:
    raw = f.read(64)
print("첫 24바이트:", raw[:24].hex(" "))
magic, ver, n_tensors, n_kv = struct.unpack_from("<4sIQQ", raw, 0)
print(f"magic={magic!r} version={ver} n_tensors={n_tensors} n_kv={n_kv}")
# 첫 KV: key = (uint64 길이 + UTF-8 문자열), value_type = uint32
klen, = struct.unpack_from("<Q", raw, 24)
key = raw[32:32 + klen].decode()
vtype, = struct.unpack_from("<I", raw, 32 + klen)
print(f"첫 KV: key_len={klen} key={key!r} value_type={vtype} (8 = STRING)")
```

```text
첫 24바이트: 47 47 55 46 03 00 00 00 10 01 00 00 00 00 00 00 1f 00 00 00 00 00 00 00
magic=b'GGUF' version=3 n_tensors=272 n_kv=31
첫 KV: key_len=20 key='general.architecture' value_type=8 (8 = STRING)
```

출력에서 볼 것:

- `47 47 55 46`은 ASCII "GGUF". little-endian u32로 읽으면 `0x46554747`이고, `gguf-py/gguf/constants.py`의 `GGUF_MAGIC = 0x46554747`과 같다.
- `10 01 00 00 …` = 0x0110 = **272** 텐서, `1f 00 …` = **31** KV. 2절에서 손으로 센 272와 맞는다.
- 값 타입 번호 8이 STRING이라는 것은 `constants.py`의 `GGUFValueType`(UINT8 = 0 … STRING = 8, ARRAY = 9, UINT64 = 10 …)에서 확인했다.

### 4.3 코드로 확인: `gguf.GGUFReader`로 메타데이터 전체 보기

무엇을 확인하나: llama.cpp가 함께 배포하는 `gguf` Python 패키지(여기서는 0.18.0)로 메타데이터 KV를 전부 출력한다. 배열은 길이만 보인다.

```python
from gguf import GGUFReader, GGUFValueType
r = GGUFReader(".tools/models/smollm2-135m-Q4_K_M.gguf")
print(f"alignment={r.alignment}  tensor data 시작 offset={r.data_offset:,} B")
for name, f in r.fields.items():
    if name.startswith("GGUF."):            # reader가 만든 가상 필드(헤더) 건너뜀
        continue
    t = f.types[0]
    if t == GGUFValueType.ARRAY:
        v = f"[array of {f.types[1].name}, len={len(f.data)}]"
    else:
        v = f.contents()
        if isinstance(v, str) and len(v) > 40:
            v = v[:40].replace("\n", "\\n") + "..."
    print(f"{name:42s} {t.name:8s} {v}")
```

```text
alignment=32  tensor data 시작 offset=1,785,600 B
general.architecture                       STRING   llama
general.type                               STRING   model
general.name                               STRING   12fd25f77366fa6b3b4b768ec3050bf629380bac
general.finetune                           STRING   12fd25f77366fa6b3b4b768ec3050bf629380bac
general.size_label                         STRING   135M
llama.block_count                          UINT32   30
llama.context_length                       UINT32   8192
llama.embedding_length                     UINT32   576
llama.feed_forward_length                  UINT32   1536
llama.attention.head_count                 UINT32   9
llama.attention.head_count_kv              UINT32   3
llama.rope.freq_base                       FLOAT32  100000.0
llama.attention.layer_norm_rms_epsilon     FLOAT32  9.999999747378752e-06
llama.attention.key_length                 UINT32   64
llama.attention.value_length               UINT32   64
llama.vocab_size                           UINT32   49152
llama.rope.dimension_count                 UINT32   64
tokenizer.ggml.add_space_prefix            BOOL     False
tokenizer.ggml.model                       STRING   gpt2
tokenizer.ggml.pre                         STRING   smollm
tokenizer.ggml.tokens                      ARRAY    [array of STRING, len=49152]
tokenizer.ggml.token_type                  ARRAY    [array of INT32, len=49152]
tokenizer.ggml.merges                      ARRAY    [array of STRING, len=48900]
tokenizer.ggml.bos_token_id                UINT32   1
tokenizer.ggml.eos_token_id                UINT32   2
tokenizer.ggml.unknown_token_id            UINT32   0
tokenizer.ggml.padding_token_id            UINT32   2
tokenizer.chat_template                    STRING   {% for message in messages %}{% if loop....
tokenizer.ggml.add_bos_token               BOOL     False
general.quantization_version               UINT32   2
general.file_type                          UINT32   15
```

출력에서 볼 것:

- **key 이름 규칙**: `general.*`(공통), `<arch>.*`(구조별 하이퍼파라미터, 여기서는 `llama.`), `tokenizer.*`. 런타임은 `general.architecture`를 먼저 읽고 그 이름을 접두어로 나머지 키를 찾는다.
- **context 길이 8192, head 9개, KV head 3개(GQA 3:1), head_dim 64** — B8에서 config.json으로 본 값이 그대로 들어 있다. D5의 KV-cache 크기 계산에 필요한 숫자가 전부 여기 있다.
- `tokenizer.ggml.model = gpt2`는 "GPT-2 방식 byte-level BPE"라는 뜻이다(모델 이름이 아니다). 어휘 49,152개, merge 규칙 48,900개, chat template(Jinja 문자열)까지 들어 있어서 **파일 하나로 tokenize부터 대화 형식까지 해결**된다.
- `general.file_type = 15`는 Q4_K_M(help 목록의 15번), `general.name`은 변환기가 HF 스냅샷 해시를 그대로 이름으로 썼다.
- **메타데이터만 1.78 MB**다(tensor data 시작 offset). 대부분 tokenizer 배열이다. MCU라면 무시 못 할 크기지만 SoC에서는 문제가 아니다.

### 4.4 코드로 확인: 텐서별 형식과 바이트 수

무엇을 확인하나: 텐서 목록을 돌면서 형식별 개수·원소 수·바이트를 합치고, 층 × 텐서 표를 만든다. 3절의 표와 그림 2를 만든 스크립트다.

```python
import sys, re, collections
from gguf import GGUFReader
r = GGUFReader(sys.argv[1])
by_type = collections.defaultdict(lambda: [0, 0, 0])      # type -> [개수, 원소, 바이트]
grid = collections.defaultdict(dict)                       # layer -> {이름: type}
for t in r.tensors:
    s = by_type[t.tensor_type.name]; s[0] += 1; s[1] += t.n_elements; s[2] += t.n_bytes
    m = re.match(r"blk\.(\d+)\.(\w+)\.weight", t.name)
    if m: grid[int(m.group(1))][m.group(2)] = t.tensor_type.name
    elif t.n_elements > 10000: print(f"{t.name:18s} {t.tensor_type.name:5s} {[int(x) for x in t.shape]}")
tot_e = sum(v[1] for v in by_type.values()); tot_b = sum(v[2] for v in by_type.values())
for k, (n, e, b) in sorted(by_type.items(), key=lambda kv: -kv[1][2]):
    print(f"{k:5s} 텐서 {n:3d}개  원소 {e/1e6:6.1f}M  {b/2**20:6.1f} MiB  {b*8/e:5.2f} bit/w")
print(f"합계 {tot_b/2**20:.1f} MiB, 평균 {tot_b*8/tot_e:.2f} bit/weight")
cols = [c for c in grid[0] if not c.endswith("norm") and grid[0][c] != "F32"]
print("layer " + " ".join(f"{c[:11]:>11s}" for c in cols))
for L in sorted(grid)[:4] + sorted(grid)[-2:]:
    print(f"{L:5d} " + " ".join(f"{grid[L][c]:>11s}" for c in cols))
```

```text
(인자: .tools/models/smollm2-135m-Q4_K_M.gguf)
token_embd.weight  Q8_0  [576, 49152]
Q5_0  텐서 166개  원소   78.1M    51.2 MiB   5.50 bit/w
Q8_0  텐서  15개  원소   29.9M    30.3 MiB   8.50 bit/w
Q6_K  텐서  14개  원소   12.4M     9.7 MiB   6.56 bit/w
Q4_K  텐서  16개  원소   14.2M     7.6 MiB   4.50 bit/w
F32   텐서  61개  원소    0.0M     0.1 MiB  32.00 bit/w
합계 98.9 MiB, 평균 6.17 bit/weight
layer      attn_k attn_output      attn_q      attn_v    ffn_down    ffn_gate      ffn_up
    0        Q5_0        Q5_0        Q5_0        Q8_0        Q6_K        Q5_0        Q5_0
    1        Q5_0        Q5_0        Q5_0        Q8_0        Q6_K        Q5_0        Q5_0
    2        Q5_0        Q5_0        Q5_0        Q8_0        Q6_K        Q5_0        Q5_0
    3        Q5_0        Q5_0        Q5_0        Q5_0        Q4_K        Q5_0        Q5_0
   28        Q5_0        Q5_0        Q5_0        Q8_0        Q6_K        Q5_0        Q5_0
   29        Q5_0        Q5_0        Q5_0        Q8_0        Q6_K        Q5_0        Q5_0
```

출력에서 볼 것:

- **Q4_K는 고작 16개**(use_more_bits가 아닌 층의 ffn_down)다. "Q4_K_M 파일"인데 4-bit 텐서는 전체 바이트의 8%뿐이다.
- Q8_0 15개 = token_embd 1 + use_more_bits 층의 attn_v 14(Q6_K → Q8_0 fallback). Q6_K 14개 = 같은 층들의 ffn_down. 3.5절의 계산(14개 층)과 정확히 맞는다.
- **"출력에서 볼 것"의 결론**: 한 번 GGUF를 열어 보면 이름과 실제가 얼마나 다른지 바로 보인다. 면접에서 "Q4_K_M이 뭐냐"고 물으면 이 표를 근거로 답할 수 있다.

### 4.5 손으로 확인: offset과 바이트 수

같은 파일에서 처음 세 텐서의 위치를 읽으면 이렇다(`t.data_offset`, `t.n_bytes`).

```text
output_norm.weight       type=F32   ne=[576] 절대 offset=1,785,600 (÷32 나머지 0) 2,304 B
token_embd.weight        type=Q8_0  ne=[576, 49152] 절대 offset=1,787,904 (÷32 나머지 0) 30,081,024 B
blk.0.attn_k.weight      type=Q5_0  ne=[576, 192] 절대 offset=31,868,928 (÷32 나머지 0) 76,032 B
```

손으로 맞춰 보자.

```
output_norm  : 576 × 4 B                         =      2,304 B
token_embd   : 576 × 49152 / 32 블록 × 34 B      = 884,736 × 34 = 30,081,024 B
attn_k (Q5_0): 576 × 192 / 32 블록 × 22 B        =   3,456 × 22 =     76,032 B
다음 offset  : 1,785,600 + 2,304 = 1,787,904      (이미 32의 배수라 padding 0)
              1,787,904 + 30,081,024 = 31,868,928 (역시 32의 배수)
```

말로 하면: **텐서 데이터는 "블록 수 × 블록 바이트"이고, 텐서끼리 빈틈 없이(정렬 padding만 두고) 이어진다.** 그래서 런타임은 `mmap` 기준 주소 + offset으로 바로 포인터를 만든다. 32 B 정렬은 SIMD load(NEON 16 B, AVX2 32 B)가 정렬된 주소를 받게 하려는 것이다. 펌웨어에서 DMA 버퍼를 캐시 라인에 맞추던 것과 같은 이유다.

### 4.6 함정

- **GGUF 버전**: 현재는 버전 3이다. 아주 오래된 GGML/GGJT 포맷 파일은 그대로 못 읽는다(변환 스크립트가 따로 있다).
- **little-endian 전제**: 변환 로그에 "This GGUF file is for Little Endian only"가 찍혔다. 빅엔디안 시스템용은 따로 만든다(드물다).
- **메타데이터가 곧 설정이다**: context 길이, RoPE 파라미터, 특수 토큰 ID가 틀리면 추론 결과가 조용히 나빠진다. 의심되면 4.3절 스크립트로 열어서 원본 `config.json`과 대조한다.

---

## 5. 텍스트 생성 — `llama-cli`와 `llama-simple`

### 5.1 대화형 한 턴 — `llama-cli`

무엇을 확인하나: Qwen2.5-0.5B-Instruct Q4_K_M으로 질문 하나에 답하게 한다. `-st`(single turn)는 한 턴 후 종료, `--temp 0`은 항상 확률이 가장 높은 token을 고르는 greedy 선택이다. 이 빌드의 `llama-cli`는 GGUF 안의 chat template을 자동으로 적용한다.

```sh
llama-cli -m models/qwen2.5-0.5b-Q4_K_M.gguf -p "In one sentence, what is a ring buffer?" \
          -st --temp 0 --seed 42 -n 60 -ngl 99 --simple-io < /dev/null
```

```text
(로고 배너와 명령 안내는 생략)
build      : b1-f7b384c
model      : models/qwen2.5-0.5b-Q4_K_M.gguf
ftype      : Q4_K - Medium
modalities : text

> In one sentence, what is a ring buffer?
A ring buffer is a data structure that stores elements in a circular manner, allowing efficient insertion and deletion of elements at both ends.

[ Prompt: 94.8 t/s | Generation: 119.2 t/s ]
```

출력에서 볼 것: 374 MiB 모델이 그럴듯한 답을 낸다("both ends"는 정확하지 않다 — ring buffer는 보통 한쪽에 쓰고 다른 쪽에서 읽는다. 0.5B 모델의 한계). 마지막 줄의 `Prompt`(prefill 속도)와 `Generation`(decode 속도)이 7절에서 `llama-bench`로 정식으로 재는 **pp**와 **tg**다.

### 5.2 양자화 수준별 출력 비교 — `llama-simple`

`llama-simple`은 `examples/simple/simple.cpp`를 빌드한 것으로, chat template 없이 프롬프트를 그대로 이어 쓰고 **greedy**로 고른다. 결정적(deterministic)이라 형식 간 비교에 좋다.

무엇을 확인하나: 같은 프롬프트에서 F16, Q8_0, Q4_K_M, Q4_0, Q2_K의 이어 쓰기가 어떻게 달라지는지 본다.

```sh
for q in f16 Q8_0 Q4_K_M Q4_0 Q2_K; do
  echo "== $q"
  llama-simple -m models/qwen2.5-0.5b-$q.gguf -n 40 -ngl 99 \
    "The three most important rules for writing interrupt service routines are" 2>/dev/null
  echo
done
```

```text
== f16
The three most important rules for writing interrupt service routines are: (1) the interrupt service routine must be called by the interrupt vector table; (2) the interrupt service routine must be called in the interrupt vector table; (3) the interrupt service routine

== Q8_0
The three most important rules for writing interrupt service routines are: (1) the interrupt service routine must be called by the interrupt vector table; (2) the interrupt service routine must be called in the interrupt vector table; (3) the interrupt service routine

== Q4_K_M
The three most important rules for writing interrupt service routines are: ① the interrupt service routine should be as short as possible; ② the interrupt service routine should be as efficient as possible; ③ the interrupt service routine should be as general

== Q4_0
The three most important rules for writing interrupt service routines are ____.
A. The interrupt service routine must be called when an interrupt occurs
B. The interrupt service routine must be called when an interrupt occurs
C. The interrupt service routine must be called when

== Q2_K
The three most important rules for writing interrupt service routines are: ① ② ③
A. ①②③
B. ①③②
C. ③①②
```

출력에서 볼 것:

- **Q8_0은 F16과 40 token 전부가 같다.** 8-bit 양자화의 오차가 greedy 선택(1등 token)을 한 번도 뒤집지 못했다.
- **Q4_K_M부터는 첫 몇 token에서 갈라진다.** 갈라진 뒤에는 완전히 다른 문장이 된다. greedy 생성은 한 token만 달라져도 그 뒤가 전부 바뀌는 **혼돈계**라서, 문장 비교는 품질 측정으로는 거칠다. Q4_K_M의 답("as short as possible")이 오히려 F16보다 그럴듯한 것도 우연이다. 그래서 6절의 perplexity처럼 **많은 token에 대한 평균**으로 재야 한다.
- **Q2_K는 무너진다**(빈칸 문제 형식으로 빠짐). 3.4절에서 봤듯이 이 모델의 "Q2_K"는 대부분 Q4_0 + ffn_down Q3_K인데도 그렇다.

### 5.3 sampling — sampler chain

`--temp 0`을 빼면 확률적으로 token을 고른다. llama.cpp는 이것을 **sampler chain**으로 구현한다. logits(어휘 크기의 점수 벡터)를 sampler들이 차례로 걸러서 마지막 sampler가 token 하나를 고른다. 이 빌드의 `llama-cli --help`에서 확인한 기본값은 다음과 같다.

```text
(--help 출력에서 발췌, 줄바꿈된 설명을 한 줄로 합침)
--samplers SAMPLERS   (default: penalties;dry;top_n_sigma;top_k;typ_p;top_p;min_p;xtc;temperature)
--temp N              temperature (default: 0.80)
--top-k N             top-k sampling (default: 40, 0 = disabled)
--top-p N             top-p sampling (default: 0.95, 1.0 = disabled)
--min-p N             min-p sampling (default: 0.05, 0.0 = disabled)
--repeat-penalty N    penalize repeat sequence of tokens (default: 1.00, 1.0 = disabled)
-s, --seed SEED       RNG seed (default: -1, use random seed for -1)
```

손으로 계산해 보자. 어휘가 4개이고 logits가 [2.0, 1.0, 0.5, −1.0]이라고 하자.

```
softmax(T=1)  : e^2=7.39, e^1=2.72, e^0.5=1.65, e^-1=0.37 → 합 12.13
              → p = [0.609, 0.224, 0.136, 0.030]
top_k = 2     → [0.609, 0.224] 만 남김 → 재정규화 [0.731, 0.269]
min_p = 0.05  → 1등 확률 × 0.05 = 0.0305 보다 작은 후보 제거 → 4번째(0.0303)가 빠진다
temperature   → logits ÷ T. T → 0 이면 1등만 남아 greedy, T > 1 이면 평평해진다
dist(seed)    → 남은 분포에서 난수로 하나 뽑기
```

말로 하면: top-k/top-p/min-p는 "후보를 몇 개로 줄일지", temperature는 "남은 후보 사이를 얼마나 고르게 할지", seed는 "같은 입력에서 같은 결과를 재현할지"를 정한다. 기기에서 음성 비서라면 재현성과 안정성이 중요하니 낮은 temperature + 고정 seed로 테스트하고, 제품에서는 약간의 무작위성을 주는 식으로 쓴다(추정 예시). sampler는 어휘 크기 벡터 하나를 다루므로 연산량은 작지만, vocab이 15만 개인 Qwen에서는 정렬(top-k)을 잘못 구현하면 token당 수 ms가 될 수 있다.

---

## 6. 품질 — `llama-perplexity`

### 6.1 무엇을 재나

perplexity(PPL)는 C3 2절에서 정의했다. "모델이 정답 다음 token에 준 확률의 기하평균의 역수" — 작을수록 좋다. `llama-perplexity`는 텍스트 파일을 `-c` 길이의 chunk로 자르고, 각 chunk의 **뒤쪽 절반 token**만 채점한다(앞 절반은 문맥으로만 쓴다).

평가 텍스트는 이 노트를 위해 직접 쓴 영어 산문 2,407 단어다(펌웨어, 전력, 칩 bring-up, 음성 비서에 대한 일반적인 설명). Qwen tokenizer로 약 2,600 token이고, `-c 256`이면 chunk 10개 × 채점 128 token = **1,280 token만 채점하는 아주 작은 평가**다. 공식 비교에 쓰는 wikitext-2(약 30만 token)의 0.5%도 안 된다. 그래서 절대값은 의미가 약하고, **같은 텍스트에서 형식 간 상대 비교**만 본다.

### 6.2 직접 해 보기

무엇을 확인하나: 3개 모델 × 6개 형식의 PPL을 같은 텍스트로 잰다(Metal로 실행, 결과는 backend와 무관하게 거의 같다).

```sh
for m in smollm2-135m qwen2.5-0.5b pythia-160m; do
  for q in f16 Q8_0 Q6_K Q4_K_M Q4_0 Q2_K; do
    r=$(llama-perplexity -m models/$m-$q.gguf -f eval.txt -c 256 -ngl 99 2>&1 \
        | grep Final | sed 's/.*PPL/PPL/')
    echo "$m $q $r"
  done
done
```

```text
smollm2-135m f16 PPL = 20.9792 +/- 1.58690
smollm2-135m Q8_0 PPL = 21.0487 +/- 1.58862
smollm2-135m Q6_K PPL = 21.2153 +/- 1.59925
smollm2-135m Q4_K_M PPL = 22.7891 +/- 1.69676
smollm2-135m Q4_0 PPL = 26.4311 +/- 2.11191
smollm2-135m Q2_K PPL = 29.3609 +/- 2.16350
qwen2.5-0.5b f16 PPL = 16.4754 +/- 1.15032
qwen2.5-0.5b Q8_0 PPL = 16.5300 +/- 1.15513
qwen2.5-0.5b Q6_K PPL = 16.5519 +/- 1.15532
qwen2.5-0.5b Q4_K_M PPL = 16.9234 +/- 1.17887
qwen2.5-0.5b Q4_0 PPL = 18.6716 +/- 1.34885
qwen2.5-0.5b Q2_K PPL = 20.8463 +/- 1.54651
pythia-160m f16 PPL = 29.7258 +/- 2.24704
pythia-160m Q8_0 PPL = 29.8575 +/- 2.25348
pythia-160m Q6_K PPL = 31.3646 +/- 2.39450
pythia-160m Q4_K_M PPL = 34.1587 +/- 2.62813
pythia-160m Q4_0 PPL = 37.7129 +/- 2.99438
pythia-160m Q2_K PPL = 95.2859 +/- 7.95474
```

### 6.3 크기 대 품질

3절의 파일 bit/weight와 묶어서 그리면 이렇다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 340"><line x1="70" y1="290" x2="600" y2="290" stroke="currentColor"/><line x1="70" y1="290" x2="70" y2="30" stroke="currentColor"/><line x1="118.2" y1="290" x2="118.2" y2="295" stroke="currentColor"/><text x="118.2" y="309" font-size="12" text-anchor="middle">4</text><line x1="214.5" y1="290" x2="214.5" y2="295" stroke="currentColor"/><text x="214.5" y="309" font-size="12" text-anchor="middle">5</text><line x1="310.9" y1="290" x2="310.9" y2="295" stroke="currentColor"/><text x="310.9" y="309" font-size="12" text-anchor="middle">6</text><line x1="407.3" y1="290" x2="407.3" y2="295" stroke="currentColor"/><text x="407.3" y="309" font-size="12" text-anchor="middle">7</text><line x1="503.6" y1="290" x2="503.6" y2="295" stroke="currentColor"/><text x="503.6" y="309" font-size="12" text-anchor="middle">8</text><line x1="600.0" y1="290" x2="600.0" y2="295" stroke="currentColor"/><text x="600.0" y="309" font-size="12" text-anchor="middle">9</text><line x1="65" y1="290.0" x2="600" y2="290.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="294.0" font-size="12" text-anchor="end">0</text><line x1="65" y1="232.2" x2="600" y2="232.2" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="236.2" font-size="12" text-anchor="end">10</text><line x1="65" y1="174.4" x2="600" y2="174.4" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="178.4" font-size="12" text-anchor="end">20</text><line x1="65" y1="116.7" x2="600" y2="116.7" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="120.7" font-size="12" text-anchor="end">30</text><line x1="65" y1="58.9" x2="600" y2="58.9" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="62.9" font-size="12" text-anchor="end">40</text><text x="335.0" y="326" font-size="12" text-anchor="middle">파일 평균 bit/weight (llama-quantize 보고값)</text><text x="18" y="160.0" font-size="12" text-anchor="middle" transform="rotate(-90 18 160.0)">PPL 증가 % (f16 대비)</text><polyline points="228.0,59.2 248.3,139.9 327.3,240.2 515.2,283.5 552.8,288.1" fill="none" stroke="#4a7bd0" stroke-width="1.5"/><circle cx="228.0" cy="59.2" r="4" fill="#4a7bd0"/><text x="236.0" y="63.2" font-size="12" text-anchor="start">Q2_K +40%</text><circle cx="248.3" cy="139.9" r="4" fill="#4a7bd0"/><text x="242.3" y="155.9" font-size="12" text-anchor="end">Q4_0 +26%</text><circle cx="327.3" cy="240.2" r="4" fill="#4a7bd0"/><text x="335.3" y="234.2" font-size="12" text-anchor="start">Q4_K_M +9%</text><circle cx="515.2" cy="283.5" r="4" fill="#4a7bd0"/><circle cx="552.8" cy="288.1" r="4" fill="#4a7bd0"/><circle cx="470" cy="50" r="4" fill="#4a7bd0"/><text x="480" y="54" font-size="12">SmolLM2-135M (f16 PPL 21.0)</text><polyline points="252.1,136.7 273.3,213.0 344.6,274.3 512.3,287.3 551.8,288.1" fill="none" stroke="#3f9a6b" stroke-width="1.5"/><circle cx="252.1" cy="136.7" r="4" fill="#3f9a6b"/><text x="260.1" y="132.7" font-size="12" text-anchor="start">Q2_K +27%</text><circle cx="273.3" cy="213.0" r="4" fill="#3f9a6b"/><text x="281.3" y="219.0" font-size="12" text-anchor="start">Q4_0 +13%</text><circle cx="344.6" cy="274.3" r="4" fill="#3f9a6b"/><text x="352.6" y="270.3" font-size="12" text-anchor="start">Q4_K_M +3%</text><circle cx="512.3" cy="287.3" r="4" fill="#3f9a6b"/><circle cx="551.8" cy="288.1" r="4" fill="#3f9a6b"/><circle cx="470" cy="68" r="4" fill="#3f9a6b"/><text x="480" y="72" font-size="12">Qwen2.5-0.5B (f16 PPL 16.5)</text><polyline points="105.7,30.0 215.5,134.8 246.3,203.8 366.8,258.1 553.7,287.4" fill="none" stroke="#e08a3c" stroke-width="1.5"/><circle cx="105.7" cy="30.0" r="4" fill="#e08a3c"/><text x="113.7" y="34.0" font-size="12" text-anchor="start">Q2_K +221% (축 밖)</text><circle cx="215.5" cy="134.8" r="4" fill="#e08a3c"/><text x="207.5" y="128.8" font-size="12" text-anchor="end">Q4_0 +27%</text><circle cx="246.3" cy="203.8" r="4" fill="#e08a3c"/><text x="238.3" y="207.8" font-size="12" text-anchor="end">Q4_K_M +15%</text><circle cx="366.8" cy="258.1" r="4" fill="#e08a3c"/><circle cx="553.7" cy="287.4" r="4" fill="#e08a3c"/><circle cx="470" cy="86" r="4" fill="#e08a3c"/><text x="480" y="90" font-size="12">Pythia-160M (f16 PPL 29.7)</text></svg>
```

그림 4 — 평균 bit/weight(가로) 대 PPL 증가율(세로, F16 대비). 점 위 라벨은 그 색 모델의 형식과 증가율이다. 오른쪽 아래(Q8_0, Q6_K)는 세 모델 모두 거의 0이고, 왼쪽으로 갈수록 가파르게 올라간다. Pythia Q2_K(+221%)는 축 밖이라 위쪽 끝에 붙여 그렸다.

출력과 그림에서 볼 것:

- **Q8_0은 세 모델 모두 +0.3~0.4%**. 사실상 무손실이다. 메모리가 허락하면 기준선으로 쓴다.
- **Q4_K_M vs Q4_0**: Qwen +2.7% vs +13.3%, SmolLM2 +8.6% vs +26.0%, Pythia +14.9% vs +26.9%. 크기 차이는 Qwen 기준 13%(374 vs 330 MiB)인데 품질 차이는 훨씬 크다. 단 SmolLM2·Qwen의 Q4_K_M은 3.4절대로 "대부분 Q5_0"이라 비교가 공정하지 않다. **진짜 k-quant가 들어간 Pythia에서도** Q4_K_M(5.33 bit)이 Q4_0(5.01 bit)보다 분명히 낫다. 0.3 bit를 더 쓰고 PPL 증가를 거의 절반으로 줄였다.
- **Q2_K는 작은 모델에서 쓰면 안 된다.** Pythia에서 진짜 Q2_K/Q3_K 혼합은 PPL이 3배가 됐다. 2-bit대는 수십억 파라미터 이상의 큰 모델 + imatrix에서나 쓸 만하다.
- **모델이 작을수록 양자화에 민감하다**: 같은 Q4_0에서 Qwen(0.5B) +13%, SmolLM2(0.135B) +26%. C3에서 본 경향과 같다. 웨어러블에 들어갈 1B 이하 모델은 "Q4면 충분"이라는 큰 모델의 상식이 그대로 통하지 않는다.
- **`+/-`를 해석하는 법**: 이 값은 chunk 사이의 흩어짐에서 나온 불확실성이라 꽤 크다(±7%). 하지만 모든 형식이 **같은 텍스트, 같은 chunk**를 채점하므로 형식 간 차이는 이 ± 보다 훨씬 확실하다(짝지은 비교). 더 엄밀하게는 `--kl-divergence-base`로 F16의 logits를 저장해 두고 `--kl-divergence`로 양자화 모델의 분포 차이(KL divergence, 1등 token 일치율)를 재는 방법이 있다(이 빌드의 `common/arg.cpp`에서 옵션 확인. 이 노트에서는 실행하지 않음).

### 6.4 함정

- **평가 텍스트가 작으면 순위가 뒤집힐 수 있다.** 1,280 token은 작다. 결정을 내리기 전에는 수만 token 이상, 그리고 제품 도메인(예: 음성 비서 대화 로그)에 가까운 텍스트로 다시 잰다.
- **PPL은 "다음 token 예측"만 본다.** 지시 따르기, 사실성, 긴 대화 일관성은 따로 평가해야 한다(C8).
- **context 길이를 바꾸면 PPL 값 자체가 바뀐다.** `-c`를 고정하고 비교한다.

---

## 7. 속도 — `llama-bench`로 pp와 tg 재기

### 7.1 pp와 tg — 두 숫자가 따로 있는 이유

D5에서 LLM 추론을 두 단계로 나눴다. llama.cpp의 벤치마크 용어로 다시 쓰면 이렇다.

| 이름 | 무엇 | 한 번에 처리하는 token | 병목 (D5) | 사용자 체감 |
|---|---|---|---|---|
| **pp** (prompt processing) = prefill | 프롬프트 전체를 한꺼번에 넣어 KV-cache를 채움 | 수백 개 (`-p 512`) | **연산량** (가중치 한 번 읽고 512번 재사용) | TTFT ≈ 프롬프트 길이 ÷ pp |
| **tg** (token generation) = decode | token을 하나씩 생성 | 1개 | **메모리 대역폭** (token마다 가중치 전체를 읽음) | TPOT ≈ 1 ÷ tg |

말로 하면: pp는 "행렬 × 행렬"(GEMM)이라 연산기가 바쁘고, tg는 "행렬 × 벡터"(GEMV)라 메모리 버스가 바쁘다. 그래서 **같은 하드웨어라도 pp와 tg는 전혀 다른 이유로 빠르거나 느리다.** 웨어러블 음성 비서라면 사용자가 말을 끝낸 뒤 첫 소리까지의 시간(TTFT)은 pp가, 답을 읽어 주는 속도는 tg가 정한다.

### 7.2 직접 해 보기 — `llama-bench`

무엇을 확인하나: Qwen2.5-0.5B의 Q4_0과 F16을 CPU만(`-dev none -t 4`)과 Metal(`-ngl 99`)로 재서 출력 형식을 본다. 기본값은 pp512, tg128, 반복 5회(`-r 5`)이고 결과는 평균 ± 표준편차다.

```sh
llama-bench -m models/qwen2.5-0.5b-Q4_0.gguf -m models/qwen2.5-0.5b-f16.gguf -dev none -t 4 -p 512 -n 128 -r 5
llama-bench -m models/qwen2.5-0.5b-Q4_0.gguf -m models/qwen2.5-0.5b-f16.gguf -ngl 99 -p 512 -n 128 -r 5
```

```text
| model                          |       size |     params | backend    | threads | dev          |            test |                  t/s |
| ------------------------------ | ---------: | ---------: | ---------- | ------: | ------------ | --------------: | -------------------: |
| qwen2 1B Q4_0                  | 330.17 MiB |   494.03 M | MTL,BLAS   |       4 | none         |           pp512 |      1123.33 ± 11.69 |
| qwen2 1B Q4_0                  | 330.17 MiB |   494.03 M | MTL,BLAS   |       4 | none         |           tg128 |        164.06 ± 3.94 |
| qwen2 1B F16                   | 942.43 MiB |   494.03 M | MTL,BLAS   |       4 | none         |           pp512 |        993.77 ± 7.19 |
| qwen2 1B F16                   | 942.43 MiB |   494.03 M | MTL,BLAS   |       4 | none         |           tg128 |         59.36 ± 2.43 |

build: f7b384c (1)
| model                          |       size |     params | backend    | threads |            test |                  t/s |
| ------------------------------ | ---------: | ---------: | ---------- | ------: | --------------: | -------------------: |
| qwen2 1B Q4_0                  | 330.17 MiB |   494.03 M | MTL,BLAS   |       4 |           pp512 |     3060.28 ± 228.98 |
| qwen2 1B Q4_0                  | 330.17 MiB |   494.03 M | MTL,BLAS   |       4 |           tg128 |        176.37 ± 5.52 |
| qwen2 1B F16                   | 942.43 MiB |   494.03 M | MTL,BLAS   |       4 |           pp512 |       3513.27 ± 4.72 |
| qwen2 1B F16                   | 942.43 MiB |   494.03 M | MTL,BLAS   |       4 |           tg128 |         84.69 ± 0.08 |
```

출력에서 볼 것:

- `size`는 텐서 바이트(MiB), `params`는 파라미터 수, `backend`는 빌드에 들어 있는 backend 목록이다(`MTL,BLAS`는 "Metal과 BLAS backend가 있는 빌드"라는 뜻이지, 이 테스트가 그것을 썼다는 뜻이 아니다). `dev none`이 실제로 GPU를 끈 것이다. `model` 칸의 "qwen2 1B"는 llama.cpp가 크기 분류를 거칠게 붙인 이름이다.
- 이 실행은 **이 Mac이 비교적 조용하던 순간**(load average 3.8)에 잰 것이다. 특히 Metal F16 tg는 84.69 ± 0.08로 흔들림이 거의 없다.
- **pp**: Metal이 CPU의 약 2.7~3.5배(3060 vs 1123, 3513 vs 994). **tg**: Q4_0은 CPU 164 vs Metal 176으로 거의 같고, F16은 Metal이 1.4배다.
- **F16의 tg를 손으로 확인**: 가중치 988 MB(= 942.43 MiB)를 token마다 읽는다고 하면 84.69 t/s × 988 MB = **83.7 GB/s**. M2 사양 100 GB/s의 84%다. CPU는 59.36 × 988 MB = **58.6 GB/s**로 D5 4.1절에서 STREAM으로 잰 CPU 읽기 대역폭(약 60~64 GB/s)과 거의 같다. decode가 대역폭 병목이라는 D5의 결론을 llama.cpp에서도 그대로 확인한 것이다.

### 7.3 형식 6가지 × CPU/Metal 전체 결과

같은 명령을 6개 형식 전부에 대해 실행한 결과다(`-o jsonl`로 받아서 정리). 이 실행 때는 다른 프로세스가 GPU를 간헐적으로 썼다(실행 전후 `ioreg`의 GPU Device Utilization 76%, 41%, 97% — 이 측정과 무관한 사용량 포함). 그래서 Metal 쪽 표준편차가 크다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 420"><text x="60" y="22" font-size="13">pp512 (prompt 처리, tokens/s)</text><line x1="60" y1="170" x2="660" y2="170" stroke="currentColor"/><line x1="60" y1="30" x2="60" y2="170" stroke="currentColor"/><line x1="56" y1="170.0" x2="660" y2="170.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="53" y="174.0" font-size="12" text-anchor="end">0</text><line x1="56" y1="131.1" x2="660" y2="131.1" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="53" y="135.1" font-size="12" text-anchor="end">1000</text><line x1="56" y1="92.2" x2="660" y2="92.2" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="53" y="96.2" font-size="12" text-anchor="end">2000</text><line x1="56" y1="53.3" x2="660" y2="53.3" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="53" y="57.3" font-size="12" text-anchor="end">3000</text><rect x="78.0" y="131.9" width="30" height="38.1" fill="#4a7bd0" fill-opacity="0.75"/><line x1="93.0" y1="133.3" x2="93.0" y2="130.6" stroke="currentColor" stroke-width="1.2"/><line x1="84.0" y1="131.3" x2="102.0" y2="131.3" stroke="currentColor" stroke-width="2"/><text x="93.0" y="126.6" font-size="12" text-anchor="middle">979</text><rect x="112.0" y="54.8" width="30" height="115.2" fill="#e08a3c" fill-opacity="0.75"/><line x1="127.0" y1="63.6" x2="127.0" y2="46.1" stroke="currentColor" stroke-width="1.2"/><line x1="118.0" y1="42.1" x2="136.0" y2="42.1" stroke="currentColor" stroke-width="2"/><text x="127.0" y="38.1" font-size="12" text-anchor="middle">2961</text><text x="110.0" y="186" font-size="12" text-anchor="middle">f16</text><rect x="178.0" y="134.2" width="30" height="35.8" fill="#4a7bd0" fill-opacity="0.75"/><line x1="193.0" y1="134.3" x2="193.0" y2="134.0" stroke="currentColor" stroke-width="1.2"/><line x1="184.0" y1="134.0" x2="202.0" y2="134.0" stroke="currentColor" stroke-width="2"/><text x="193.0" y="130.0" font-size="12" text-anchor="middle">921</text><rect x="212.0" y="111.5" width="30" height="58.5" fill="#e08a3c" fill-opacity="0.75"/><line x1="227.0" y1="112.5" x2="227.0" y2="110.5" stroke="currentColor" stroke-width="1.2"/><line x1="218.0" y1="110.4" x2="236.0" y2="110.4" stroke="currentColor" stroke-width="2"/><text x="227.0" y="106.4" font-size="12" text-anchor="middle">1504</text><text x="210.0" y="186" font-size="12" text-anchor="middle">Q8_0</text><rect x="278.0" y="148.9" width="30" height="21.1" fill="#4a7bd0" fill-opacity="0.75"/><line x1="293.0" y1="150.5" x2="293.0" y2="147.3" stroke="currentColor" stroke-width="1.2"/><line x1="284.0" y1="147.4" x2="302.0" y2="147.4" stroke="currentColor" stroke-width="2"/><text x="293.0" y="143.3" font-size="12" text-anchor="middle">544</text><rect x="312.0" y="114.8" width="30" height="55.2" fill="#e08a3c" fill-opacity="0.75"/><line x1="327.0" y1="116.2" x2="327.0" y2="113.5" stroke="currentColor" stroke-width="1.2"/><line x1="318.0" y1="113.5" x2="336.0" y2="113.5" stroke="currentColor" stroke-width="2"/><text x="327.0" y="109.5" font-size="12" text-anchor="middle">1419</text><text x="310.0" y="186" font-size="12" text-anchor="middle">Q6_K</text><rect x="378.0" y="148.3" width="30" height="21.7" fill="#4a7bd0" fill-opacity="0.75"/><line x1="393.0" y1="148.4" x2="393.0" y2="148.3" stroke="currentColor" stroke-width="1.2"/><line x1="384.0" y1="148.3" x2="402.0" y2="148.3" stroke="currentColor" stroke-width="2"/><text x="393.0" y="144.3" font-size="12" text-anchor="middle">557</text><rect x="412.0" y="118.5" width="30" height="51.5" fill="#e08a3c" fill-opacity="0.75"/><line x1="427.0" y1="120.1" x2="427.0" y2="116.9" stroke="currentColor" stroke-width="1.2"/><line x1="418.0" y1="116.0" x2="436.0" y2="116.0" stroke="currentColor" stroke-width="2"/><text x="427.0" y="112.0" font-size="12" text-anchor="middle">1324</text><text x="410.0" y="186" font-size="12" text-anchor="middle">Q4_K_M</text><rect x="478.0" y="145.3" width="30" height="24.7" fill="#4a7bd0" fill-opacity="0.75"/><line x1="493.0" y1="145.9" x2="493.0" y2="144.6" stroke="currentColor" stroke-width="1.2"/><line x1="484.0" y1="144.5" x2="502.0" y2="144.5" stroke="currentColor" stroke-width="2"/><text x="493.0" y="140.5" font-size="12" text-anchor="middle">636</text><rect x="512.0" y="100.5" width="30" height="69.5" fill="#e08a3c" fill-opacity="0.75"/><line x1="527.0" y1="102.8" x2="527.0" y2="98.1" stroke="currentColor" stroke-width="1.2"/><line x1="518.0" y1="96.6" x2="536.0" y2="96.6" stroke="currentColor" stroke-width="2"/><text x="527.0" y="92.6" font-size="12" text-anchor="middle">1788</text><text x="510.0" y="186" font-size="12" text-anchor="middle">Q4_0</text><rect x="578.0" y="146.9" width="30" height="23.1" fill="#4a7bd0" fill-opacity="0.75"/><line x1="593.0" y1="148.1" x2="593.0" y2="145.8" stroke="currentColor" stroke-width="1.2"/><line x1="584.0" y1="145.3" x2="602.0" y2="145.3" stroke="currentColor" stroke-width="2"/><text x="593.0" y="141.3" font-size="12" text-anchor="middle">593</text><rect x="612.0" y="112.2" width="30" height="57.8" fill="#e08a3c" fill-opacity="0.75"/><line x1="627.0" y1="113.5" x2="627.0" y2="111.0" stroke="currentColor" stroke-width="1.2"/><line x1="618.0" y1="110.4" x2="636.0" y2="110.4" stroke="currentColor" stroke-width="2"/><text x="627.0" y="106.4" font-size="12" text-anchor="middle">1486</text><text x="610.0" y="186" font-size="12" text-anchor="middle">Q2_K</text><text x="60" y="227" font-size="13">tg128 (token 생성, tokens/s)</text><line x1="60" y1="365" x2="660" y2="365" stroke="currentColor"/><line x1="60" y1="235" x2="60" y2="365" stroke="currentColor"/><line x1="56" y1="365.0" x2="660" y2="365.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="53" y="369.0" font-size="12" text-anchor="end">0</text><line x1="56" y1="321.7" x2="660" y2="321.7" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="53" y="325.7" font-size="12" text-anchor="end">50</text><line x1="56" y1="278.3" x2="660" y2="278.3" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="53" y="282.3" font-size="12" text-anchor="end">100</text><line x1="56" y1="235.0" x2="660" y2="235.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="53" y="239.0" font-size="12" text-anchor="end">150</text><rect x="78.0" y="310.7" width="30" height="54.3" fill="#4a7bd0" fill-opacity="0.75"/><line x1="93.0" y1="311.5" x2="93.0" y2="310.0" stroke="currentColor" stroke-width="1.2"/><line x1="84.0" y1="310.1" x2="102.0" y2="310.1" stroke="currentColor" stroke-width="2"/><text x="93.0" y="306.0" font-size="12" text-anchor="middle">63</text><rect x="112.0" y="321.4" width="30" height="43.6" fill="#e08a3c" fill-opacity="0.75"/><line x1="127.0" y1="334.8" x2="127.0" y2="308.0" stroke="currentColor" stroke-width="1.2"/><line x1="118.0" y1="297.7" x2="136.0" y2="297.7" stroke="currentColor" stroke-width="2"/><text x="127.0" y="293.7" font-size="12" text-anchor="middle">50</text><text x="110.0" y="381" font-size="12" text-anchor="middle">f16</text><rect x="178.0" y="270.8" width="30" height="94.2" fill="#4a7bd0" fill-opacity="0.75"/><line x1="193.0" y1="275.7" x2="193.0" y2="266.0" stroke="currentColor" stroke-width="1.2"/><line x1="184.0" y1="266.0" x2="202.0" y2="266.0" stroke="currentColor" stroke-width="2"/><text x="193.0" y="262.0" font-size="12" text-anchor="middle">109</text><rect x="212.0" y="319.4" width="30" height="45.6" fill="#e08a3c" fill-opacity="0.75"/><line x1="227.0" y1="325.7" x2="227.0" y2="313.2" stroke="currentColor" stroke-width="1.2"/><line x1="218.0" y1="308.6" x2="236.0" y2="308.6" stroke="currentColor" stroke-width="2"/><text x="227.0" y="304.6" font-size="12" text-anchor="middle">53</text><text x="210.0" y="381" font-size="12" text-anchor="middle">Q8_0</text><rect x="278.0" y="269.9" width="30" height="95.1" fill="#4a7bd0" fill-opacity="0.75"/><line x1="293.0" y1="272.1" x2="293.0" y2="267.8" stroke="currentColor" stroke-width="1.2"/><line x1="284.0" y1="267.9" x2="302.0" y2="267.9" stroke="currentColor" stroke-width="2"/><text x="293.0" y="263.8" font-size="12" text-anchor="middle">110</text><rect x="312.0" y="316.2" width="30" height="48.8" fill="#e08a3c" fill-opacity="0.75"/><line x1="327.0" y1="321.3" x2="327.0" y2="311.1" stroke="currentColor" stroke-width="1.2"/><line x1="318.0" y1="308.0" x2="336.0" y2="308.0" stroke="currentColor" stroke-width="2"/><text x="327.0" y="304.0" font-size="12" text-anchor="middle">56</text><text x="310.0" y="381" font-size="12" text-anchor="middle">Q6_K</text><rect x="378.0" y="288.6" width="30" height="76.4" fill="#4a7bd0" fill-opacity="0.75"/><line x1="393.0" y1="292.0" x2="393.0" y2="285.1" stroke="currentColor" stroke-width="1.2"/><line x1="384.0" y1="282.6" x2="402.0" y2="282.6" stroke="currentColor" stroke-width="2"/><text x="393.0" y="278.6" font-size="12" text-anchor="middle">88</text><rect x="412.0" y="303.0" width="30" height="62.0" fill="#e08a3c" fill-opacity="0.75"/><line x1="427.0" y1="310.5" x2="427.0" y2="295.5" stroke="currentColor" stroke-width="1.2"/><line x1="418.0" y1="291.6" x2="436.0" y2="291.6" stroke="currentColor" stroke-width="2"/><text x="427.0" y="287.6" font-size="12" text-anchor="middle">72</text><text x="410.0" y="381" font-size="12" text-anchor="middle">Q4_K_M</text><rect x="478.0" y="237.7" width="30" height="127.3" fill="#4a7bd0" fill-opacity="0.75"/><line x1="493.0" y1="242.9" x2="493.0" y2="232.5" stroke="currentColor" stroke-width="1.2"/><line x1="484.0" y1="234.2" x2="502.0" y2="234.2" stroke="currentColor" stroke-width="2"/><text x="493.0" y="228.5" font-size="12" text-anchor="middle">147</text><rect x="512.0" y="287.4" width="30" height="77.6" fill="#e08a3c" fill-opacity="0.75"/><line x1="527.0" y1="299.9" x2="527.0" y2="275.0" stroke="currentColor" stroke-width="1.2"/><line x1="518.0" y1="268.2" x2="536.0" y2="268.2" stroke="currentColor" stroke-width="2"/><text x="527.0" y="264.2" font-size="12" text-anchor="middle">89</text><text x="510.0" y="381" font-size="12" text-anchor="middle">Q4_0</text><rect x="578.0" y="262.2" width="30" height="102.8" fill="#4a7bd0" fill-opacity="0.75"/><line x1="593.0" y1="266.8" x2="593.0" y2="257.7" stroke="currentColor" stroke-width="1.2"/><line x1="584.0" y1="257.7" x2="602.0" y2="257.7" stroke="currentColor" stroke-width="2"/><text x="593.0" y="253.7" font-size="12" text-anchor="middle">119</text><rect x="612.0" y="293.9" width="30" height="71.1" fill="#e08a3c" fill-opacity="0.75"/><line x1="627.0" y1="301.0" x2="627.0" y2="286.8" stroke="currentColor" stroke-width="1.2"/><line x1="618.0" y1="282.3" x2="636.0" y2="282.3" stroke="currentColor" stroke-width="2"/><text x="627.0" y="278.3" font-size="12" text-anchor="middle">82</text><text x="610.0" y="381" font-size="12" text-anchor="middle">Q2_K</text><rect x="420" y="392" width="14" height="12" fill="#4a7bd0" fill-opacity="0.75"/><text x="440" y="403" font-size="12">CPU 4 thread</text><rect x="530" y="392" width="14" height="12" fill="#e08a3c" fill-opacity="0.75"/><text x="550" y="403" font-size="12">Metal (GPU)</text><text x="60" y="403" font-size="12">막대 = 평균, 세로선 = ±표준편차, 굵은 가로선 = 최고 샘플</text></svg>
```

그림 5 — Qwen2.5-0.5B, 형식별 pp512(위)와 tg128(아래). 파랑 = CPU 4 thread, 주황 = Metal. 막대는 5회 평균, 가는 세로선은 ±표준편차, 굵은 가로선은 5회 중 최고값. CPU 쪽은 표준편차가 작고(조용했다), Metal 쪽은 GPU 공유 때문에 크다.

| 형식 | 가중치 | CPU pp512 | CPU tg128 | Metal pp512 | Metal tg128 |
|---|---|---|---|---|---|
| F16 | 942 MiB | 979 ± 34 | 62.6 ± 0.9 | 2961 ± 225 | 50.3 ± 15.5 |
| Q8_0 | 501 MiB | 921 ± 3 | 108.7 ± 5.6 | 1504 ± 26 | 52.6 ± 7.2 |
| Q6_K (대부분 Q8_0) | 477 MiB | 544 ± 41 | 109.7 ± 2.5 | 1419 ± 35 | 56.3 ± 5.9 |
| Q4_K_M (대부분 Q5_0) | 374 MiB | 557 ± 1 | 88.2 ± 4.0 | 1324 ± 41 | 71.5 ± 8.7 |
| Q4_0 | 330 MiB | 636 ± 16 | 146.9 ± 6.0 | 1788 ± 60 | 89.5 ± 14.3 |
| Q2_K (대부분 Q4_0) | 317 MiB | 594 ± 30 | 118.6 ± 5.2 | 1486 ± 32 | 82.0 ± 8.2 |

표에서 볼 것:

- **tg는 대체로 바이트가 작을수록 빠르다** — F16 63 → Q8_0 109 → Q4_0 147 (CPU). 바이트가 942 → 501 → 330 MiB로 줄어든 것과 같은 방향이다.
- **그런데 정확히 비례하지는 않는다.** Q4_K_M(374 MiB)은 Q8_0(501 MiB)보다 느리다. 7.4절에서 이유를 본다.
- **pp는 바이트 순서를 따르지 않는다.** CPU에서는 F16(979)이 가장 빠르다. pp는 바이트가 아니라 **커널과 연산 경로**가 정하기 때문이다(7.5절). 양자화 형식끼리의 CPU pp 순서는 믿기 어렵다 — 바로 뒤에 같은 sweep을 한 번 더 돌렸을 때 Q8_0은 921이 아니라 640이었다. sweep 안에서 몇 번째로 측정됐는지(그때까지 칩이 얼마나 달궈졌는지)에 따라 바뀐 것으로 보인다.
- 같은 Q4_0 CPU pp가 7.2절에서는 1123, 이 표에서는 636이다. 다른 프로세스와의 경쟁과 7.6절의 지속 부하 감속이 겹친 것으로 보인다.

### 7.4 tg를 대역폭으로 설명하기

D5의 decode 식은 `TPOT ≈ (가중치 바이트 + KV 바이트) ÷ 대역폭`이었다. 거꾸로 쓰면 **실효 대역폭 = tg × token당 읽는 바이트**다. tg128은 context가 짧아(최대 128 token) KV는 무시할 만하고, Qwen은 tied embedding이라 출력 projection으로 embedding 행렬 전체를 token마다 읽으므로 **token당 바이트 ≈ 파일의 텐서 바이트 전체**로 잡는다.

무엇을 확인하나: 7.3절의 jsonl 결과에서 형식별 실효 대역폭을 계산한다.

```python
import json
B = "/private/tmp/claude-501/f3/bench5_{}.jsonl"
print("형식    가중치MB  CPU tg(평균/최고)  →실효 GB/s(최고) | Metal tg(평균/최고) →실효 GB/s(최고)")
for lc, lm in zip(open(B.format("cpu")), open(B.format("metal"))):
    c, m = json.loads(lc), json.loads(lm)
    if c["n_gen"] == 0: continue                      # pp 줄은 건너뜀
    q = c["model_filename"].split("-")[-1][:-5]
    mb = c["model_size"] / 1e6                         # token 하나당 읽는 가중치 바이트 (tied → 출력 행렬 포함)
    cm, mm = max(c["samples_ts"]), max(m["samples_ts"])
    print(f"{q:7s} {mb:7.0f}   {c['avg_ts']:5.0f} / {cm:5.0f}   → {mb*cm/1e3:5.1f}      |"
          f"   {m['avg_ts']:5.0f} / {mm:5.0f}   → {mb*mm/1e3:5.1f}")
```

```text
형식    가중치MB  CPU tg(평균/최고)  →실효 GB/s(최고) | Metal tg(평균/최고) →실효 GB/s(최고)
f16         988      63 /    63   →  62.6      |      50 /    78   →  76.7
Q8_0        525     109 /   114   →  60.0      |      53 /    65   →  34.2
Q6_K        500     110 /   112   →  56.0      |      56 /    66   →  32.9
Q4_K_M      392      88 /    95   →  37.2      |      72 /    85   →  33.2
Q4_0        346     147 /   151   →  52.3      |      89 /   112   →  38.7
Q2_K        333     119 /   124   →  41.2      |      82 /    95   →  31.7
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 320"><line x1="60" y1="260" x2="660" y2="260" stroke="currentColor"/><line x1="60" y1="30" x2="60" y2="260" stroke="currentColor"/><text x="53" y="264.0" font-size="12" text-anchor="end">0</text><line x1="56" y1="260.0" x2="60" y2="260.0" stroke="currentColor"/><text x="53" y="222.2" font-size="12" text-anchor="end">20</text><line x1="56" y1="218.2" x2="60" y2="218.2" stroke="currentColor"/><text x="53" y="180.4" font-size="12" text-anchor="end">40</text><line x1="56" y1="176.4" x2="60" y2="176.4" stroke="currentColor"/><text x="53" y="138.5" font-size="12" text-anchor="end">60</text><line x1="56" y1="134.5" x2="60" y2="134.5" stroke="currentColor"/><text x="53" y="96.7" font-size="12" text-anchor="end">80</text><line x1="56" y1="92.7" x2="60" y2="92.7" stroke="currentColor"/><text x="53" y="54.9" font-size="12" text-anchor="end">100</text><line x1="56" y1="50.9" x2="60" y2="50.9" stroke="currentColor"/><line x1="60" y1="50.9" x2="660" y2="50.9" stroke="#d0564a" stroke-dasharray="6 4"/><text x="660" y="45.9" font-size="12" text-anchor="end">M2 사양 100 GB/s</text><line x1="60" y1="130.4" x2="660" y2="130.4" stroke="#3f9a6b" stroke-dasharray="6 4"/><text x="660" y="125.4" font-size="12" text-anchor="end">D5 CPU STREAM 실측 약 60~64 GB/s</text><rect x="80.0" y="129.1" width="28" height="130.9" fill="#4a7bd0" fill-opacity="0.75"/><text x="94.0" y="125.1" font-size="12" text-anchor="middle">63</text><rect x="112.0" y="99.6" width="28" height="160.4" fill="#e08a3c" fill-opacity="0.75"/><text x="126.0" y="95.6" font-size="12" text-anchor="middle">77</text><text x="110.0" y="276" font-size="12" text-anchor="middle">f16</text><rect x="180.0" y="134.5" width="28" height="125.5" fill="#4a7bd0" fill-opacity="0.75"/><text x="194.0" y="130.5" font-size="12" text-anchor="middle">60</text><rect x="212.0" y="188.5" width="28" height="71.5" fill="#e08a3c" fill-opacity="0.75"/><text x="226.0" y="184.5" font-size="12" text-anchor="middle">34</text><text x="210.0" y="276" font-size="12" text-anchor="middle">Q8_0</text><rect x="280.0" y="142.9" width="28" height="117.1" fill="#4a7bd0" fill-opacity="0.75"/><text x="294.0" y="138.9" font-size="12" text-anchor="middle">56</text><rect x="312.0" y="191.2" width="28" height="68.8" fill="#e08a3c" fill-opacity="0.75"/><text x="326.0" y="187.2" font-size="12" text-anchor="middle">33</text><text x="310.0" y="276" font-size="12" text-anchor="middle">Q6_K</text><rect x="380.0" y="182.2" width="28" height="77.8" fill="#4a7bd0" fill-opacity="0.75"/><text x="394.0" y="178.2" font-size="12" text-anchor="middle">37</text><rect x="412.0" y="190.6" width="28" height="69.4" fill="#e08a3c" fill-opacity="0.75"/><text x="426.0" y="186.6" font-size="12" text-anchor="middle">33</text><text x="410.0" y="276" font-size="12" text-anchor="middle">Q4_K_M</text><rect x="480.0" y="150.6" width="28" height="109.4" fill="#4a7bd0" fill-opacity="0.75"/><text x="494.0" y="146.6" font-size="12" text-anchor="middle">52</text><rect x="512.0" y="179.1" width="28" height="80.9" fill="#e08a3c" fill-opacity="0.75"/><text x="526.0" y="175.1" font-size="12" text-anchor="middle">39</text><text x="510.0" y="276" font-size="12" text-anchor="middle">Q4_0</text><rect x="580.0" y="173.9" width="28" height="86.1" fill="#4a7bd0" fill-opacity="0.75"/><text x="594.0" y="169.9" font-size="12" text-anchor="middle">41</text><rect x="612.0" y="193.7" width="28" height="66.3" fill="#e08a3c" fill-opacity="0.75"/><text x="626.0" y="189.7" font-size="12" text-anchor="middle">32</text><text x="610.0" y="276" font-size="12" text-anchor="middle">Q2_K</text><text x="20" y="145.0" font-size="12" text-anchor="middle" transform="rotate(-90 20 145.0)">실효 GB/s = tg × 가중치 바이트</text><rect x="60" y="292" width="14" height="12" fill="#4a7bd0" fill-opacity="0.75"/><text x="80" y="303" font-size="12">CPU 4 thread (최고 샘플)</text><rect x="260" y="292" width="14" height="12" fill="#e08a3c" fill-opacity="0.75"/><text x="280" y="303" font-size="12">Metal (최고 샘플)</text></svg>
```

그림 6 — 형식별 실효 대역폭(tg 최고 샘플 × 가중치 바이트). 초록 점선은 D5에서 STREAM으로 잰 CPU 읽기 대역폭, 빨강 점선은 M2 사양. CPU의 F16·Q8_0은 초록 선에 거의 붙어 있다 = 대역폭 병목. Q4_K_M·Q2_K는 한참 아래 = 대역폭이 아닌 다른 것(커널의 dequant 연산)이 병목.

출력에서 볼 것:

- **CPU F16·Q8_0·Q6_K는 56~63 GB/s**로, D5의 STREAM 측정(60~64 GB/s)과 같다. 즉 이 형식들의 tg는 **메모리 대역폭이 상한을 정확히 정하고 있다.** 계산기(D5)로 예측한 값이 거의 그대로 나온다.
- **Q4_0은 52 GB/s**로 조금 떨어지고, **Q4_K_M(대부분 Q5_0)은 37 GB/s, Q2_K는 41 GB/s**까지 떨어진다. 바이트는 적지만 **풀어내는(dequant) 일이 복잡해서** CPU 4코어의 연산이 대역폭을 다 쓰기 전에 먼저 바닥난다. Q5_0은 5번째 bit를 따로 모아 둔 비트맵을 다시 끼워 넣어야 하고, CPU repack(8.3절)도 Q5_0에는 적용되지 않았다(로그에서 repack된 형식은 q4_0_4x8, q4_K_8x8, q6_K_8x8, q8_0_4x8 뿐이었다). 그래서 **"bit가 적으면 빠르다"는 대역폭 병목일 때만 맞다.**
- **Metal은 F16만 77 GB/s(조용할 때는 7.2절의 83.7 GB/s)이고 양자화 형식은 32~39 GB/s**에 머문다. 바이트가 1/3로 줄어도 tg가 그만큼 오르지 않는다는 뜻이다. 가능한 원인은 두 가지다. (1) 이 측정 동안 다른 프로세스가 GPU를 같이 썼다(7.3절). (2) 작은 모델에서는 token마다 수백 개의 작은 GPU kernel을 띄우는 **고정 오버헤드**가 바이트 전송 시간과 비슷해진다. 조용할 때 잰 7.2절에서도 Q4_0 Metal tg(176)가 CPU(164)와 거의 같았던 것을 보면 (2)의 비중도 작지 않다고 추정한다. 정확히 가르려면 GPU를 아무도 안 쓰는 상태에서 Xcode Instruments(Metal System Trace)로 kernel 사이의 빈틈을 봐야 한다.
### 7.5 pp를 연산량으로 설명하기

무엇을 확인하나: token당 FLOP을 세어서 pp 실측을 TFLOP/s로 바꾸고, pp 동안 가중치 트래픽이 대역폭의 몇 %인지 본다.

```python
P, P_emb, L, d = 494.03e6, 151936 * 896, 24, 896      # Qwen2.5-0.5B (GGUF 메타데이터 값)
S = 512
flop_lin = 2 * (P - P_emb)                            # token당 Linear MAC×2 (embedding 조회는 MAC 아님)
flop_att = 4 * d * (S / 2) * L                        # QKᵀ + AV, 평균 위치 S/2
f = flop_lin + flop_att
print(f"token당 FLOP ≈ {flop_lin/1e9:.3f} (Linear) + {flop_att/1e9:.3f} (attention) = {f/1e9:.3f} GFLOP")
for name, tps in [("CPU f16", 979), ("CPU Q4_0", 636), ("Metal f16", 2961), ("Metal Q4_0", 1788)]:
    print(f"{name:10s} pp512 {tps:5d} t/s → {tps*f/1e12:5.2f} TFLOP/s")
w_bytes = 346.2e6                                     # Q4_0 가중치
print(f"pp에서 가중치 읽기: ubatch 512 token마다 {w_bytes/1e6:.0f} MB 한 번 → token당 {w_bytes/512/1e3:.0f} KB")
print(f"  CPU Q4_0 636 t/s일 때 가중치 트래픽 {636*w_bytes/512/1e9:.2f} GB/s (대역폭 60 GB/s의 {636*w_bytes/512/60e9*100:.1f}%)")
```

```text
token당 FLOP ≈ 0.716 (Linear) + 0.022 (attention) = 0.738 GFLOP
CPU f16    pp512   979 t/s →  0.72 TFLOP/s
CPU Q4_0   pp512   636 t/s →  0.47 TFLOP/s
Metal f16  pp512  2961 t/s →  2.18 TFLOP/s
Metal Q4_0 pp512  1788 t/s →  1.32 TFLOP/s
pp에서 가중치 읽기: ubatch 512 token마다 346 MB 한 번 → token당 676 KB
  CPU Q4_0 636 t/s일 때 가중치 트래픽 0.43 GB/s (대역폭 60 GB/s의 0.7%)
```

출력에서 볼 것:

- pp 중 가중치 트래픽은 대역폭의 **0.7%**뿐이다. 가중치를 한 번 읽어 512 token에 재사용하기 때문이다(arithmetic intensity가 높다 — D3). 그러니 **pp는 연산량이 정한다.**
- **Metal F16 2.18 TFLOP/s**는 M2 10코어 GPU의 공개 FP32 성능(약 3.6 TFLOPS)의 60% 정도다. GEMM으로서 괜찮은 효율이다.
- **CPU에서 양자화가 pp를 빠르게 해 주지 않는다.** 위 계산에서는 F16(0.72)이 Q4_0(0.47)보다 빨랐지만, 조용할 때 단독으로 잰 7.2절에서는 Q4_0 1123 vs F16 994로 비슷했다(→ 0.83 vs 0.73 TFLOP/s). 즉 둘은 같은 급이고 순서는 측정 조건에 따라 바뀐다. 경로는 다르다: 이 빌드의 CPU 경로에서 F16 행렬곱은 BLAS backend(Apple Accelerate)로 넘어갈 수 있고(`ggml/src/ggml-blas/ggml-blas.cpp`의 `supports_op`: batch ≥ 32이고 float로 풀 수 있는 형식. F16 가중치는 repack되지 않고 `CPU_Mapped` 버퍼에 남는다), Accelerate는 D5에서 1.2~1.4 TFLOP/s GEMM을 냈다. 양자화 형식은 `CPU_REPACK` 버퍼에 들어가 ggml 자체의 정수 dot product 커널로 계산된다(verbose 로그의 `repack: repack tensor … with q4_0_4x8`). 바이트가 1/3이어도 pp가 3배가 되지 않는 것은 pp를 바이트가 아니라 연산이 정하기 때문이다.
- 웨어러블에 주는 교훈: **prefill은 NPU/GPU 같은 연산 가속기로, decode는 대역폭 싸움**이다. 프롬프트가 긴 음성 비서(시스템 프롬프트 + 대화 기록)라면 TTFT를 줄이기 위해 pp를 가속기에 맡기는 것이 우선이다(D5 7절).

### 7.6 측정 노이즈와 thermal — 같은 명령, 다른 숫자

같은 Q4_0 CPU 벤치마크를 이 노트를 쓰는 동안 여러 번 돌린 결과다(모두 `-dev none -t 4`).

| 언제 | 상황 | pp512 | tg128 |
|---|---|---|---|
| 첫 시도 | load average 45 (다른 작업 다수) | 254 ± 31 (`-ngl 0`) | 37 ± 14 |
| sweep 1~2 | load 8~16 | 311~409 | 94~96 |
| sweep 3 (`-r 15`) | load 5~10 | 544 ± 33 | 111 ± 15 |
| sweep 5 | load 3~5 | 636 ± 16 | 147 ± 6 |
| Q4_0 단독 실행 | load 3.8, 직전 휴식 | 1123 ± 12 | 164 ± 4 |

tg는 부하가 내려가면서 37 → 164로 4배, pp는 254 → 1123으로 4배 넘게 달라졌다. 다른 작업과의 CPU 경쟁이 첫째 이유다. 그런데 부하가 낮아진 뒤에도 sweep 안의 pp(636)와 단독 실행의 pp(1123)가 크게 다르다. 원인을 확인해 보자.

무엇을 확인하나: pp512를 40번 연속으로 돌리면서 매 회의 속도를 시간 순서로 본다.

```sh
llama-bench -m models/qwen2.5-0.5b-Q4_0.gguf -dev none -t 4 -p 512 -n 0 -r 40 -o jsonl 2>/dev/null | python3 -c "
import json,sys
d=json.loads(sys.stdin.readline()); s=d['samples_ts']; ns=d['samples_ns']
t=0; out=[]
for v,n in zip(s,ns):
    t+=n/1e9; out.append(f'{t:4.1f}s:{v:.0f}')
print(' '.join(out))"
```

```text
 0.4s:1140  0.9s:1132  1.4s:1105  1.8s:1069  2.3s:1067  2.8s:1019  3.3s:1028  3.8s:1005  4.4s:991  4.9s:982  5.4s:964  6.2s:663  6.8s:790  7.4s:912  8.0s:781  8.6s:887  9.2s:876  9.8s:867 10.4s:869 11.0s:861 11.6s:866 12.2s:852 12.8s:817 13.4s:844 14.0s:832 14.6s:835 15.2s:836 15.9s:832 16.5s:816 17.1s:828 17.7s:827 18.3s:827 19.0s:817 19.6s:819 20.2s:817 20.9s:738 21.5s:810 22.2s:817 22.8s:814 23.4s:810
```

출력에서 볼 것: **처음 0.4초 1140 t/s → 20초 뒤 약 815 t/s**로 단조롭게 28% 떨어진다. 이 Mac은 `hw.model = Mac14,15`(팬이 없는 15인치 MacBook Air M2)다. 4코어를 계속 돌리면 온도가 올라가 클럭이 내려가는 것으로 보인다(`pmset -g therm`에는 기록이 없어서 직접 증거는 아니다 — 추정). 6초 근처의 663 같은 튀는 값은 다른 프로세스와의 경쟁이다.

이것이 웨어러블에서 그대로 벌어지는 일이다. 손목이나 귀에 있는 기기는 노트북보다 열을 버릴 곳이 훨씬 적다. **짧은 벤치마크(llama-bench 기본값은 테스트당 몇 초)는 지속 성능을 과대평가한다.** 음성 비서가 한 번에 몇 초만 돌면 짧은 측정이 맞고, 긴 대화를 계속하면 지속 성능이 맞다. 제품 평가 때는 두 숫자를 모두 적는다(E9).

### 7.7 함정

- **`-ngl 0`은 "CPU만"이 아닐 수 있다.** GPU 장치가 열려 있으면 큰 batch 연산을 GPU로 보내는 op offload가 일어날 수 있다(`llama-bench --help`의 `-nopo, --no-op-offload`). 순수 CPU 측정은 `-dev none`으로 한다.
- **`backend` 칸을 실행 장치로 오해하지 않는다.** 빌드에 포함된 backend 목록이다.
- **pp 숫자는 프롬프트 길이와 ubatch에 따라 바뀐다.** `-p 512`와 실제 제품의 프롬프트 길이(예: 200 token)가 다르면 다시 잰다.
- **tg는 context 깊이에 따라 바뀐다.** tg128은 KV가 거의 비어 있는 상태다. 대화가 길어진 상태는 `-d`(depth)로 잰다(8.4절).
- **평균 ± 표준편차가 크면 숫자를 쓰지 않는다.** 원인(다른 프로세스, thermal, 전원 모드)을 먼저 없앤다. 이 노트의 Metal tg가 그런 상태다.

---

## 8. 런타임 내부 — 개념과 측정

### 8.1 ggml 계산 그래프 — C로 직접

llama.cpp는 token(또는 ubatch)을 처리할 때마다 **ggml 계산 그래프**를 만든다. 그래프는 "어떤 텐서에 어떤 연산을 어떤 순서로"의 목록이고, 만드는 순간에는 계산하지 않는다. 다 만든 뒤 backend에 넘기면 그때 계산한다. PyTorch의 eager 실행과 달리 **먼저 전체를 보고 나서 실행**하므로 메모리를 미리 계획할 수 있다(TFLite Micro의 tensor arena와 비슷한 발상 — F2).

무엇을 확인하나: ggml로 `y = W·x`, `z = relu(y)` 그래프를 만들고 CPU에서 실행한다. 2×3 행렬이라 손으로 맞춰 볼 수 있다.

```c
#include <stdio.h>
#include "ggml.h"
#include "ggml-cpu.h"

int main(void) {
    struct ggml_init_params ip = { 16 * 1024 * 1024, NULL, false };  /* 16 MB arena */
    struct ggml_context *ctx = ggml_init(ip);
    /* W: 2행 × 3열. ggml shape은 [ne0=3(행 길이, 연속), ne1=2(행 수)] */
    struct ggml_tensor *W = ggml_new_tensor_2d(ctx, GGML_TYPE_F32, 3, 2);
    struct ggml_tensor *x = ggml_new_tensor_2d(ctx, GGML_TYPE_F32, 3, 1);
    const float w[6] = { 1, 2, 3,  -4, 5, -6 }, xv[3] = { 1, 0, 1 };
    for (int i = 0; i < 6; i++) ggml_set_f32_1d(W, i, w[i]);
    for (int i = 0; i < 3; i++) ggml_set_f32_1d(x, i, xv[i]);

    struct ggml_tensor *y = ggml_mul_mat(ctx, W, x);   /* 아직 계산 안 함: 노드만 만든다 */
    struct ggml_tensor *z = ggml_relu(ctx, y);
    struct ggml_cgraph *gf = ggml_new_graph(ctx);
    ggml_build_forward_expand(gf, z);                    /* z까지 가는 그래프 수집 */
    printf("graph nodes = %d, y shape = [%lld, %lld]\n", ggml_graph_n_nodes(gf),
           (long long)y->ne[0], (long long)y->ne[1]);
    ggml_graph_compute_with_ctx(ctx, gf, 1);             /* 여기서 실제 계산 */
    printf("y = [%g, %g]  relu(y) = [%g, %g]\n", ggml_get_f32_1d(y, 0),
           ggml_get_f32_1d(y, 1), ggml_get_f32_1d(z, 0), ggml_get_f32_1d(z, 1));
    ggml_free(ctx);
    return 0;
}
```

```sh
L=.tools/llama.cpp
cc -std=c11 -Wall -Wextra -O2 -I$L/ggml/include g.c -L$L/build/bin \
   -lggml -lggml-base -lggml-cpu -Wl,-rpath,$L/build/bin -o g && ./g
```

```text
graph nodes = 2, y shape = [2, 1]
y = [4, -10]  relu(y) = [4, 0]
```

손으로 맞추기: W의 첫 행 [1, 2, 3]·[1, 0, 1] = 4, 둘째 행 [−4, 5, −6]·[1, 0, 1] = −10. relu는 음수를 0으로. 경고 0개로 컴파일됐다.

출력에서 볼 것:

- **`ne[0]`은 연속된 차원(행 길이)**이다. 2절의 `{576, 49152}` 표기가 바로 이것이다. C 배열 `float W[2][3]`의 주소 계산과 같다.
- **`ggml_mul_mat(a, b)`는 "a의 각 행과 b의 각 행의 내적"**이다. 둘 다 `ne[0]`(= K)이 같아야 하고 결과는 `[a의 행 수, b의 행 수]`. 그래서 가중치 W(`[K, M]`)와 활성값 x(`[K, N]`)를 넣으면 `[M, N]`이 나온다. 가중치를 전치하지 않고 행 단위로 읽으므로, **양자화 블록(행 방향 32·256개)을 그대로 dot product에 쓸 수 있다.** 3.4절의 "행 길이가 256의 배수여야 한다"는 제약이 여기서 나온다.
- **노드 2개짜리 그래프**: 실제 LLM 한 층은 RMSNorm, Q/K/V 행렬곱, RoPE, attention, 출력 행렬곱, 잔차, FFN 등으로 수십 노드이고, Qwen2.5-0.5B 24층이면 token 하나에 수백~천 개 노드다. 9.2절 C API 예제 출력의 `graphs reused = 22`는 decode 때 그래프 구조가 같으면 다시 만들지 않고 재사용했다는 뜻이다.

decode 한 step 동안 일어나는 일을 그림으로 정리하면:

```
llama_decode(ctx, batch: token 1개, pos = n)
  │
  ├─ 1. 그래프 만들기 (또는 재사용)        src/llama-model.cpp: 구조별 build 함수
  │      embd = get_rows(token_embd, id)      ← embedding 행 1개만 읽음
  │      for 층 l in 0..L-1:
  │         x = rms_norm(x) · attn_norm
  │         q,k,v = mul_mat(W_q|k|v, x)       ← 가중치 읽기 (양자화 블록 dot)
  │         q,k = rope(q,k, pos)
  │         KV-cache[l][pos] ← k, v           ← 미리 할당된 버퍼에 쓰기 (복사·cat 없음)
  │         a = attention(q, K[0..pos], V[0..pos])   ← -fa 면 융합 커널 하나
  │         x = x + mul_mat(W_o, a)
  │         x = x + ffn_down(silu(ffn_gate(x)) · ffn_up(x))
  │      logits = mul_mat(output 또는 token_embd, rms_norm(x))  ← vocab 크기만큼
  │
  ├─ 2. scheduler: 노드마다 backend 배정 (가중치가 있는 곳 기준), 필요하면 split·복사
  ├─ 3. backend 실행: CPU thread pool / Metal command buffer …
  └─ 4. logits → sampler chain → 다음 token id
```

D5 4.5절에서 PyTorch(HF) 구현은 KV를 step마다 `cat`하고 GQA용으로 복제해서 트래픽이 이상 모델의 8배였다. llama.cpp는 **KV-cache를 처음에 context 크기만큼 할당**하고 그 자리에 쓴다(8.4절 로그의 `llama_kv_cache: size = …`). 펌웨어의 정적 링버퍼와 같다.

### 8.2 backend와 scheduler

- **장치 목록**: `llama-bench --list-devices`의 출력은 이 빌드에서 다음과 같았다.

```text
Available devices:
  MTL0: Apple M2 (18186 MiB, 18185 MiB free)
  BLAS: Accelerate (0 MiB, 0 MiB free)
```

- **`-ngl N`(n_gpu_layers)**: N개 층의 가중치를 GPU 쪽 버퍼에 둔다. 이 커밋의 `src/llama-model.cpp`를 보면 `i_gpu_start = max(n_layer + 1 - n_gpu_layers, 0)`으로, **뒤쪽 층부터**(출력 층 포함) GPU로 간다. 남은 앞쪽 층은 CPU에서 돈다. 통합 메모리인 Apple Silicon이나 모바일 SoC에서도 "어느 backend의 버퍼에 두나"가 곧 "어느 backend가 계산하나"를 정한다.
- **scheduler**(`ggml_backend_sched`)는 그래프의 노드마다 실행할 backend를 고르고, backend가 바뀌는 지점에서 그래프를 **split**으로 나누고 텐서를 복사한다. **split이 많으면 복사와 동기화가 늘어 느려진다.** NPU에 연산 하나가 지원되지 않아 CPU로 갔다 오는 "fallback"(F8)과 같은 문제다.
- **CPU_REPACK**: CPU backend는 로드할 때 양자화 가중치를 SIMD에 맞는 배치로 다시 배열한다(이 Mac에서 `q4_0_4x8`: 4개 행을 8바이트씩 interleave). 같은 블록 내용을 "SIMD 한 번 load로 여러 행을 처리하기 좋은 순서"로 바꾸는 것이다. 펌웨어에서 DMA descriptor나 행렬을 하드웨어가 좋아하는 interleave 포맷으로 바꿔 두는 것과 같다. 대가는 메모리(다음 절).

### 8.3 mmap 로딩 — 측정

GGUF의 텐서 데이터는 정렬된 채로 파일에 있으므로(4.5절) `mmap`하면 파일 페이지가 곧 가중치다. 복사가 없어 로드가 빠르고, OS가 쓰는 페이지만 메모리에 올린다. 그리고 **파일에서 온 깨끗한(clean) 페이지**라서 메모리가 부족하면 OS가 그냥 버렸다가 필요할 때 파일에서 다시 읽을 수 있다. 반대로 `malloc`해서 읽어 들인 메모리는 버릴 수 없는 **익명(anonymous) 메모리**다.

이 빌드에서 로딩 방식은 `-lm, --load-mode`(auto, none, mmap, mlock, mmap+mlock, dio)로 고른다. repack은 `--repack` / `--no-repack`이다.

무엇을 확인하나: `llama-cli`로 1 token만 생성하고 끝내면서, `/usr/bin/time -l`로 실행 시간, 최대 RSS, macOS의 "peak memory footprint"(이 프로세스만의 익명·더티 메모리)를 잰다. 각 3회, 모두 파일이 이미 page cache에 있는 따뜻한(warm) 상태다(캐시를 비우려면 관리자 권한이 필요해서 cold 측정은 하지 않았다).

```sh
/usr/bin/time -l llama-cli -m models/$m.gguf -p "Hi" -n 1 -st --temp 0 -dev none -c 512 \
    --simple-io $opt < /dev/null
```

```text
qwen2.5-0.5b-f16 | -lm mmap | run2 | real 0.60s | maxRSS 1088 MiB | footprint 89 MiB
qwen2.5-0.5b-f16 | -lm none | run2 | real 0.82s | maxRSS 1086 MiB | footprint 1015 MiB
qwen2.5-0.5b-Q4_0 | -lm mmap | run2 | real 0.51s | maxRSS 801 MiB | footprint 402 MiB
qwen2.5-0.5b-Q4_0 | -lm none | run2 | real 0.75s | maxRSS 752 MiB | footprint 681 MiB
qwen2.5-0.5b-Q4_0 | -lm mmap --no-repack | run2 | real 0.57s | maxRSS 475 MiB | footprint 88 MiB
```

(각 설정 3회 중 두 번째 실행. 3회는 서로 1~2% 이내로 같았고, 첫 실행만 Q4_0 mmap이 0.72초였다.)

출력에서 볼 것:

- **F16, mmap**: RSS는 1088 MiB(가중치 페이지를 다 만졌으니 RSS에 잡힌다)지만 **footprint는 89 MiB**뿐이다. 가중치 942 MiB가 전부 파일에 기반한 clean 페이지라서 "이 프로세스만의 메모리"가 아니다. 같은 모델을 여는 다른 프로세스와 페이지를 공유할 수도 있다.
- **F16, `-lm none`**: footprint가 **1015 MiB**로 뛰고, 로드가 0.60 → 0.82초로 느려졌다. 파일을 malloc 버퍼로 복사했기 때문이다.
- **Q4_0, mmap(기본, repack 켜짐)**: footprint **402 MiB**. repack이 330 MiB짜리 가중치를 **익명 메모리에 다시 써서** mmap의 이점이 사라졌다. RSS 801 MiB는 원본 mmap 페이지 + repack 사본이 둘 다 잡힌 것이다(로드 때 원본 페이지를 읽어야 하므로).
- **Q4_0, `--no-repack`**: footprint가 **88 MiB**로 돌아온다. 대신 속도는 아래처럼 tg가 19% 느려진다.

```text
(llama-bench -dev none -t 4 -p 256 -n 64 -r 5 --repack 0,1, Qwen2.5-0.5B Q4_0)
repack 0: pp256  702.7 ± 17.4   tg64 127.2 ± 2.6
repack 1: pp256  718.9 ± 27.0   tg64 156.9 ± 0.7
```

임베디드 연결: Android에는 보통 swap이 없고(zram 압축 정도), 메모리가 부족하면 Low Memory Killer가 **익명 메모리를 많이 쓰는 프로세스부터** 죽인다. 그래서 웨어러블·폰에서는 "mmap된 clean 가중치 + 작은 익명 메모리(KV, compute buffer)"가 생존에 유리하다. 반면 clean 페이지는 OS가 버릴 수 있으니, 메모리 압박 중에는 decode 도중 가중치 페이지를 flash에서 다시 읽느라 **tg가 갑자기 느려지는** 일이 생긴다. 이것을 막는 것이 `mlock`(페이지 고정)이다. **repack(속도) vs mmap 공유(메모리) vs mlock(지연 안정성)**은 제품마다 고르는 trade-off다.

### 8.4 KV-cache 옵션 — `-c`, `-ctk`/`-ctv`, `-fa`

**크기 계산 (D5 공식)**: KV 바이트 = 2(K, V) × 층 수 × KV head 수 × head_dim × context × 원소 바이트. Qwen2.5-0.5B는 24층, KV head 2개, head_dim 64다.

```
F16 : 2 × 24 × 2 × 64 × 2 B = 12,288 B/token  → × 8192 = 100,663,296 B = 96.0 MiB
Q8_0: head_dim 64 = 32 × 2 블록 × 34 B = 68 B (F16은 128 B)
      2 × 24 × 2 × 68 B = 6,528 B/token        → × 8192 =  53,477,376 B = 51.0 MiB
```

`llama-cli -c 8192 -fa on -v`의 로그와 비교하면 정확히 맞는다.

```text
llama_kv_cache: size =   96.00 MiB (  8192 cells,  24 layers,  1/1 seqs), K (f16):   48.00 MiB, V (f16):   48.00 MiB
sched_reserve:        CPU compute buffer size =    43.51 MiB
(-ctk q8_0 -ctv q8_0)
llama_kv_cache: size =   51.00 MiB (  8192 cells,  24 layers,  1/1 seqs), K (q8_0):   25.50 MiB, V (q8_0):   25.50 MiB
llama_kv_cache: attn_rot_k = 1, n_embd_head_k_all = 64
sched_reserve:        CPU compute buffer size =    41.79 MiB
```

출력에서 볼 것:

- **KV-cache는 `-c`만큼 처음에 통째로 할당된다.** context를 32768(모델 최대)로 두면 F16 KV만 384 MiB로, 가중치(330 MiB)보다 커진다. 기기에서는 **제품에 필요한 최대 context로 `-c`를 줄이는 것**이 가장 큰 메모리 절약이다.
- **compute buffer 43.5 MiB**: 중간 활성값을 담는 작업 메모리. ubatch 크기에 비례한다(8.6절).
- 양자화 KV에서 `attn_rot_k = 1`이 켜졌다. 이름으로 보아 K를 회전(Hadamard 계열 변환)해서 outlier를 펴고 양자화 오차를 줄이는 기능으로 보인다(이 커밋의 새 기능으로 추정, 소스로 세부 확인은 하지 않음). C3 4절의 outlier 문제를 KV에 적용한 것이다.

**V를 양자화하려면 flash attention이 필요하다.** `-fa off -ctv q8_0`으로 실행하면 이렇게 거부된다.

```text
E llama_init_from_model: quantized V cache requires flash_attn to be enabled
```

flash attention(FA)은 attention 점수 행렬을 메모리에 다 만들지 않고 블록 단위로 softmax와 가중합을 융합하는 커널이다(B4, D5). 융합 커널 안에서 V 블록을 바로 풀어 쓰기 때문에 양자화 V가 가능해진다.

무엇을 확인하나: context가 이미 4096 token 차 있을 때(`-d 4096`) tg가 FA·KV 형식에 따라 어떻게 바뀌는지 CPU와 Metal에서 잰다(Qwen2.5-0.5B Q4_0, `-n 64 -r 3`).

```sh
llama-bench -m models/qwen2.5-0.5b-Q4_0.gguf -dev none -t 4 -p 0 -n 64 -d 0,4096 -r 3 -fa 0
llama-bench -m models/qwen2.5-0.5b-Q4_0.gguf -dev none -t 4 -p 0 -n 64 -d 0,4096 -r 3 -fa 1
llama-bench -m models/qwen2.5-0.5b-Q4_0.gguf -dev none -t 4 -p 0 -n 64 -d 0,4096 -r 3 -fa 1 -ctk q8_0 -ctv q8_0
(같은 세 줄을 -ngl 99로)
```

| backend | FA | KV 형식 | tg64 @ depth 0 | tg64 @ depth 4096 | depth 4096에서의 변화 |
|---|---|---|---|---|---|
| CPU | off | f16 | 120.5 ± 10.3 | 69.5 ± 16.0 | −42% |
| CPU | on | f16 | 131.3 ± 35.5 | 75.1 ± 7.5 | −43% |
| CPU | on | q8_0 | 154.9 ± 10.0 | **42.9 ± 4.1** | −72% |
| Metal | off | f16 | 112.3 ± 12.2 | 28.9 ± 3.7 | −74% |
| Metal | on | f16 | 63.4 ± 32.8 | 46.1 ± 12.9 | |
| Metal | on | q8_0 | 50.7 ± 31.1 | **60.8 ± 4.8** | |

(측정 중 load average 3~7. Metal depth 0 줄은 GPU 공유로 표준편차가 커서 믿기 어렵다. depth 4096 줄들은 비교적 안정적이다.)

표에서 볼 것:

- **context가 차면 tg가 크게 떨어진다.** KV 4096 token은 F16으로 48 MiB, 가중치 346 MB의 14%뿐인데 CPU tg는 42% 떨어졌다. 대역폭만으로는 설명이 안 된다 — attention은 KV를 읽는 것 외에 token마다 4096개 위치에 대한 내적·softmax 연산을 하고, 이 부분의 CPU 효율이 행렬곱보다 낮다. D5 4.4절에서 HF 구현의 기울기가 이상 모델의 8배였던 것과 같은 종류의 "이상 모델 × 트래픽·효율 배수" 문제다(llama.cpp는 배수가 훨씬 작지만 0은 아니다).
- **CPU에서 q8_0 KV는 오히려 느려졌다**(75 → 43). 메모리는 절반이 되지만, CPU의 FA 경로에서 K·V 블록을 풀어 쓰는 연산이 더해진다. **KV 양자화는 메모리 절약이지 속도 향상이 아닐 수 있다.**
- **Metal에서는 FA와 q8_0 KV가 depth 4096의 tg를 29 → 46 → 61로 끌어올렸다.** GPU에서는 FA 커널이 KV 읽기를 줄이고, 바이트가 줄어든 효과가 그대로 속도가 된다.
- 결론: **KV 옵션의 효과는 backend마다 반대 방향일 수 있다.** 그래서 반드시 목표 기기에서, 목표 context 깊이에서 잰다.

### 8.5 thread 수 `-t` — P코어와 E코어

무엇을 확인하나: CPU thread를 1~8개로 바꾸며 pp256과 tg64를 잰다(load average 5~6).

```sh
llama-bench -m models/qwen2.5-0.5b-Q4_0.gguf -dev none -t 1,2,4,6,8 -p 256 -n 64 -r 5
```

| thread | pp256 (t/s) | tg64 (t/s) | tg 실효 대역폭 (× 346 MB) |
|---|---|---|---|
| 1 | 328.0 ± 2.8 | 106.4 ± 4.3 | 36.8 GB/s |
| 2 | 639.5 ± 0.7 | 161.0 ± 1.4 | 55.7 GB/s |
| 4 | 1116.5 ± 50.8 | 166.7 ± 4.3 | 57.7 GB/s |
| 6 | 1045.1 ± 20.3 | 147.7 ± 9.9 | 51.1 GB/s |
| 8 | 880.2 ± 48.4 | 118.0 ± 4.3 | 40.9 GB/s |

표에서 볼 것:

- **pp는 4 thread까지 거의 선형으로 는다**(328 → 640 → 1117). 연산 병목이니 코어를 더 주면 빨라진다.
- **tg는 2 thread에서 이미 포화**된다(161 → 167). 2개 코어로 이미 대역폭(약 56~58 GB/s)을 다 쓴다. 대역폭 병목이면 코어를 더 줘도 소용없다 — D5 이론의 가장 깔끔한 확인이다. 기기에서는 **decode에 코어를 적게 쓰고 나머지는 재우는 것**이 전력상 이득이다.
- **6, 8 thread는 오히려 느리다.** M2는 성능 코어 4개 + 효율 코어 4개다. 4개를 넘으면 느린 효율 코어가 일을 나눠 받고, 매 연산 끝의 barrier에서 모두가 **가장 느린 thread를 기다린다.** 펌웨어로 치면 멀티코어 작업을 균등 분할했는데 한 코어만 클럭이 낮아서 전체가 그 코어에 맞춰지는 것이다. 모바일 SoC(big.LITTLE, 예: Cortex-X + A7xx + A5xx)에서도 똑같다. llama.cpp는 `-C, --cpu-mask`, `--cpu-strict`로 코어를 고정할 수 있다.

### 8.6 batch와 ubatch — `-b`, `-ub`

- **`-b` (n_batch)**: `llama_decode()` 한 번에 넣을 수 있는 최대 token 수(논리적 크기). `include/llama.h`의 주석은 "logical maximum batch size that can be submitted to llama_decode".
- **`-ub` (n_ubatch)**: 실제로 그래프 한 번에 계산하는 token 수(물리적 크기, "physical maximum batch size"). 프롬프트 1000 token을 `-ub 512`로 넣으면 512 + 488 두 번에 나눠 계산한다. compute buffer(8.4절의 43.5 MiB)는 ubatch 크기에 맞춰 잡힌다.

무엇을 확인하나: ubatch를 16~512로 바꾸며 pp512를 잰다.

| ubatch | CPU pp512 | Metal pp512 |
|---|---|---|
| 16 | 525.2 ± 5.7 | 642.0 ± 70.7 |
| 64 | 586.7 ± 3.9 | 1258.2 ± 56.5 |
| 256 | 556.6 ± 106.9 | 1613.5 ± 43.3 |
| 512 | 384.1 ± 160.6 (최고 556) | 1774.1 ± 38.7 |

표에서 볼 것: **GPU는 ubatch가 클수록 빠르다**(642 → 1774). 행렬이 커야 GPU의 많은 연산기를 채울 수 있다. **CPU는 64 정도면 이미 충분**하다(ub 512의 큰 표준편차는 측정 중 경쟁·thermal로 보인다). 메모리가 빠듯한 기기라면 CPU에서는 ubatch를 줄여 compute buffer를 줄여도 손해가 작다.

### 8.7 context가 가득 차면 — context shift

KV-cache가 `-c` 크기까지 차면 더 생성할 자리가 없다. 선택지는 두 가지다.

- **멈춘다**: 이 빌드의 기본값이다(`--context-shift` 기본 disabled, `llama-cli --help`).
- **context shift**: `--keep N`개의 앞쪽 token(예: system prompt)은 남기고 그 뒤의 오래된 token 일부를 버린 다음, 남은 KV의 위치를 앞으로 당긴다. RoPE는 위치를 회전으로 넣는 방식이라 K를 "위치 차이만큼 다시 회전"시키면 재계산 없이 당길 수 있다. 대신 버린 부분의 정보는 사라진다. D5 5.4절의 sliding window와 비슷한 효과다.

음성 비서라면(추정 예시) 대화가 길어질 때 **오래된 턴을 요약해서 다시 넣거나, system prompt만 keep하고 shift**하는 정책을 앱 쪽에서 정해야 한다. 런타임이 알아서 해 주지 않는다.

---

## 9. 앱에 넣기 — C API, 서버, Android

### 9.1 C API의 흐름

`include/llama.h`가 공개 API 전부다. 이 커밋에서 grep으로 확인한 핵심 함수와 줄 번호는 다음과 같다.

| 단계 | 함수 (`include/llama.h` 줄) | 하는 일 |
|---|---|---|
| 초기화 | `llama_backend_init` (486), `llama_backend_free` (489) | backend 등록 |
| 모델 | `llama_model_default_params` (479), `llama_model_load_from_file` (522), `llama_model_free` (547) | GGUF 로드(mmap), `n_gpu_layers`, `load_mode` 설정 |
| 어휘 | `llama_model_get_vocab` (593), `llama_tokenize` (1273), `llama_token_to_piece` (1287), `llama_vocab_is_eog` (1206) | 텍스트 ↔ token |
| 컨텍스트 | `llama_context_default_params` (480), `llama_init_from_model` (549), `llama_free` (559) | KV-cache, `n_ctx`, `n_batch`, `n_ubatch`, `n_threads`, `type_k`/`type_v`, `flash_attn_type` |
| 실행 | `llama_batch_get_one` (962), `llama_batch_init` (973), `llama_decode` (1003) | token 묶음 처리 → logits |
| 샘플링 | `llama_sampler_chain_init` (1448), `llama_sampler_chain_add` (1451), `llama_sampler_init_greedy` (1468), `_top_k` (1475), `_top_p` (1478), `_min_p` (1481), `_temp` (1487), `_dist` (1471), `llama_sampler_sample` (1641) | 5.3절의 sampler chain |
| 메모리 | `llama_get_memory` (590), `llama_memory_clear` (752) | KV-cache 비우기 등 |
| 성능 | `llama_perf_context_print` (1694) | load/prompt/eval 시간 |

API는 빠르게 변한다. 이 커밋에는 `llama_batch_ext_*`(1011줄 이후)라는 새 batch API가 들어와 있고, `examples/simple/simple.cpp`는 이미 그것과 `llama_process()`를 쓴다. `llama_batch_get_one`의 주석에는 "helper function to facilitate transition to the new batch API - avoid using it"이라고 적혀 있다. 아래 예제는 아직 남아 있는 `llama_decode` 경로로 짰다. **실제 제품에서는 llama.cpp 버전을 고정(pin)하고, 올릴 때 API 변경을 확인**해야 한다. 벤더 SDK 버전 매트릭스를 관리하던 것(F8)과 같다.

```
앱 시작:  backend_init → model_load_from_file(mmap) → init_from_model(KV 할당)
                                      │
요청마다:  tokenize(prompt) ──► decode(batch: 프롬프트 전체) = prefill (pp)
                                      │
           ┌──────────────► sampler_sample → token_to_piece → 화면/TTS로 스트리밍
           │                          │
           └── decode(batch: token 1개) = decode (tg) ◄── eog면 종료
대화 리셋: memory_clear      앱 종료: free → model_free → backend_free
```

### 9.2 코드로 확인: 40줄짜리 C 프로그램

무엇을 확인하나: `llama.h`만으로 모델을 로드하고 greedy로 24 token을 생성한다. 5.2절의 `llama-simple` Q4_K_M 출력과 같은지 본다.

```c
#include <stdio.h>
#include <string.h>
#include "llama.h"

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s model.gguf prompt [gpu]\n", argv[0]); return 1; }
    const char *path = argv[1], *prompt = argv[2];
    llama_backend_init();
    struct llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = (argc > 3) ? 99 : 0;              /* 0 = CPU만, 99 = 전부 GPU */
    struct llama_model *model = llama_model_load_from_file(path, mp);
    const struct llama_vocab *vocab = llama_model_get_vocab(model);

    llama_token toks[256];                               /* 1) tokenize */
    int n = llama_tokenize(vocab, prompt, (int)strlen(prompt), toks, 256, true, true);

    struct llama_context_params cp = llama_context_default_params();
    cp.n_ctx = 512; cp.n_batch = 512; cp.n_threads = 4; cp.no_perf = false;
    struct llama_context *ctx = llama_init_from_model(model, cp);

    struct llama_sampler *smpl =                          /* 2) sampler chain */
        llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(smpl, llama_sampler_init_greedy());

    struct llama_batch batch = llama_batch_get_one(toks, n);  /* 3) prefill */
    for (int i = 0; i < 24; i++) {
        if (llama_decode(ctx, batch) != 0) return 1;      /* 그래프 실행, KV에 추가 */
        llama_token t = llama_sampler_sample(smpl, ctx, -1);
        if (llama_vocab_is_eog(vocab, t)) break;
        char buf[64];
        int len = llama_token_to_piece(vocab, t, buf, sizeof buf, 0, true);
        printf("%.*s", len, buf); fflush(stdout);
        toks[0] = t; batch = llama_batch_get_one(toks, 1); /* 4) decode: 1 token씩 */
    }
    printf("\n");
    llama_perf_context_print(ctx);
    llama_sampler_free(smpl); llama_free(ctx); llama_model_free(model);
    llama_backend_free();
    return 0;
}
```

```sh
L=.tools/llama.cpp
cc -std=c11 -Wall -Wextra -O2 -I$L/include -I$L/ggml/include mini.c -L$L/build/bin -lllama \
   -Wl,-rpath,$L/build/bin -o mini
./mini .tools/models/qwen2.5-0.5b-Q4_K_M.gguf \
   "The three most important rules for writing interrupt service routines are" 2>err.txt
grep llama_perf_context_print err.txt
```

```text
: ① the interrupt service routine should be as short as possible; ② the interrupt service routine should be
llama_perf_context_print:        load time =    1078.20 ms
llama_perf_context_print: prompt eval time =     558.93 ms /    11 tokens (   50.81 ms per token,    19.68 tokens per second)
llama_perf_context_print:        eval time =     668.62 ms /    23 runs   (   29.07 ms per token,    34.40 tokens per second)
llama_perf_context_print:       total time =    1755.07 ms /    34 tokens
llama_perf_context_print:    graphs reused =         22
```

출력에서 볼 것:

- **생성된 텍스트가 5.2절 `llama-simple` Q4_K_M(Metal)의 앞부분과 글자 그대로 같다.** 이 예제는 CPU(`n_gpu_layers = 0`)로 돌았는데도 greedy 결과가 같았다. backend가 달라도 수치 차이가 1등 token을 뒤집지 않은 것이다(항상 보장되지는 않는다).
- 컴파일 경고 0개. 링크는 `libllama` 하나(+ rpath)로 끝난다. 정적 빌드(`-DBUILD_SHARED_LIBS=OFF`)면 실행 파일 하나가 된다.
- **prompt 11 token에 559 ms**는 pp치고 매우 느리다. 첫 `llama_decode`에 CPU repack·warm-up과 thread pool 시작이 들어갔기 때문으로 보인다(`llama-bench`는 측정 전에 warm-up을 한다). 앱에서는 **모델 로드 직후 빈 decode로 미리 데워 두면** 첫 응답의 TTFT가 줄어든다. 펌웨어의 "부팅 시 캐시·클럭 미리 올려 두기"와 같은 습관이다.
- `graphs reused = 22`: 23번의 decode 중 22번은 그래프를 새로 만들지 않았다(8.1절).

### 9.3 `llama-server` — OpenAI 호환 HTTP

`tools/server`의 `llama-server`는 모델을 띄워 두고 HTTP로 요청을 받는다. `tools/server/README.md`에 따르면 `/v1/chat/completions`, `/v1/completions`, `/v1/embeddings` 같은 **OpenAI 호환 엔드포인트**를 제공해서, 기존 OpenAI 클라이언트 코드의 base URL만 바꾸면 로컬 모델을 쓸 수 있다. 동시 요청을 `-np`(parallel slot)로 나눠 처리하고 slot마다 KV를 관리한다. 이 노트에서는 빌드·실행하지 않았다. 웨어러블 기기 안에서 HTTP 서버를 띄우는 일은 드물지만, **동반 폰 앱이나 개발용 테스트 하네스**로는 유용하다(L3 hybrid 라우팅 실험 등).

### 9.4 Android와 Snapdragon — 빌드 경로 (문서 기준, 직접 실행하지 않음)

`docs/android.md`의 NDK 크로스 컴파일 명령은 다음과 같다(이 커밋 기준).

```sh
cmake \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-28 \
  -DGGML_NATIVE=OFF \
  -DGGML_OPENMP=OFF \
  -DGGML_LLAMAFILE=OFF \
  -DLLAMA_OPENSSL=OFF \
  -B build-android
```

문서가 설명하는 각 옵션의 이유:

- `GGML_NATIVE=OFF`: 호스트(빌드 PC)의 CPU 기능으로 최적화하지 않는다. 크로스 컴파일의 기본(F7). `-march`를 전역으로 올리지 말라는 경고도 있다 — 오래된 기기에서 illegal instruction으로 죽는다.
- `GGML_OPENMP=OFF`: NDK의 OpenMP를 CMake가 의존성으로 잡지 못해서. ggml 자체 thread pool을 쓴다.
- `LLAMA_OPENSSL=OFF`: OpenSSL이 NDK의 안정 API가 아니라서.

GPU/NPU 경로(직접 검증하지 않았으니 문서의 주장으로만 읽을 것):

| 경로 | CMake 옵션 | 문서 | 메모 |
|---|---|---|---|
| Adreno GPU (OpenCL) | `-DGGML_OPENCL=ON` (Adreno 최적화 커널 `GGML_OPENCL_USE_ADRENO_KERNELS` 기본 ON) | `docs/backend/OPENCL.md` | 지원 표에 Adreno 750(8 Gen 3), 830(8 Elite) 등. Qualcomm 배포 바이너리 커널 라이브러리 옵션도 있다 |
| Hexagon NPU | `GGML_HEXAGON=ON` + Hexagon SDK | `docs/backend/snapdragon/README.md` | Qualcomm의 Docker 툴체인 이미지(Android NDK, OpenCL SDK, Hexagon SDK 포함)와 `scripts/snapdragon/build.py`로 빌드·adb push |
| CPU (ARM) | 기본 | `docs/build.md`의 KleidiAI 절 | Arm의 최적화 마이크로커널(KleidiAI)을 선택적으로 켤 수 있다 |
| Android 앱 | `examples/llama.android` | `docs/android.md` | Android Studio 프로젝트. Kotlin에서 GGUF 메타데이터 읽기·추론 |

웨어러블에서라면(추정 예시) NDK CPU 빌드로 `llama-bench` 기준선(thread 수·코어 affinity별)을 먼저 잡고, OpenCL·Hexagon 빌드와 벤더 스택(QNN/Genie, F4)을 같은 모델·같은 측정 조건으로 비교한다. 각 단계에서 verbose 로그의 split(어떤 op가 CPU로 떨어지나), 지속 부하 thermal(7.6절), 메모리 압박(8.3절)을 같이 본다.

### 9.5 기기 메모리 예산 — 숫자로

Qwen2.5-0.5B Q4_0, `-c 8192`, CPU 기준으로 이 노트에서 잰 항목을 더하면:

| 항목 | 크기 | 성격 |
|---|---|---|
| 가중치 (mmap) | 330 MiB | clean, 파일 기반 (OS가 버릴 수 있음) |
| repack 사본 (켜면) | 약 330 MiB | 익명 (버릴 수 없음) |
| KV-cache F16 / Q8_0 | 96 / 51 MiB | 익명, `-c`에 비례 |
| compute buffer | 43.5 MiB | 익명, ubatch에 비례 |
| 출력 버퍼 (logits) | 0.58 MiB | vocab 151,936 × 4 B |
| 기타 (코드, tokenizer 등) | 약 87 MiB의 footprint 안 | |

말로 하면: 같은 모델이라도 설정에 따라 익명 메모리가 **약 130 MiB(no-repack, Q8_0 KV, 작은 ubatch)부터 약 470 MiB(repack, F16 KV)**까지 달라진다. 웨어러블 SoC의 RAM이 1~2 GB이고 OS·오디오·UI가 같이 산다면(추정) 이 차이가 "들어간다/안 들어간다"를 가른다.

---

## 10. 임베디드 관점에서 다시 보기

### 10.1 웨어러블에서 tg 상한을 미리 계산하기

이 Mac에서 확인한 사실 — **CPU tg ≈ 실효 대역폭 ÷ (가중치 + KV 바이트)**, 실효 대역폭은 사양의 약 60% — 를 그대로 웨어러블 후보 SoC에 옮겨 보자. 메모리 대역폭 17 GB/s는 LPDDR4X 2채널급을 가정한 숫자다(실제 Hark 기기의 사양은 모른다 — 추정 예시).

무엇을 확인하나: 이 노트에서 만든 실제 GGUF 파일 크기와 모델별 KV 바이트로 token당 읽는 바이트를 계산해서, 가정한 기기에서의 tg 상한을 구한다.

```python
import os
bw_spec, eff = 17.0e9, 0.6                   # 가정: 사양 17 GB/s(LPDDR4X 2채널급), 효율 60% (이 Mac CPU: 60 / 100 GB/s)
kv_tok = {"qwen2.5-0.5b": 12288, "smollm2-135m": 2 * 30 * 3 * 64 * 2}   # F16 KV 바이트/token
for m in ("smollm2-135m", "qwen2.5-0.5b"):
    for q in ("Q8_0", "Q4_K_M", "Q4_0"):
        w = os.path.getsize(f".tools/models/{m}-{q}.gguf")
        for ctx in (512, 2048):
            byt = w + kv_tok[m] * ctx               # decode 1 token당 읽는 바이트 (가중치 + KV 전체)
            tg = bw_spec * eff / byt
            print(f"{m:13s} {q:6s} 파일 {w/2**20:5.0f} MiB  ctx {ctx:4d}: KV {kv_tok[m]*ctx/2**20:4.0f} MiB"
                  f"  → tg 상한 {tg:5.1f} t/s ({1000/tg:5.1f} ms/token)")
```

```text
smollm2-135m  Q8_0   파일   138 MiB  ctx  512: KV   11 MiB  → tg 상한  65.1 t/s ( 15.4 ms/token)
smollm2-135m  Q8_0   파일   138 MiB  ctx 2048: KV   45 MiB  → tg 상한  53.1 t/s ( 18.8 ms/token)
smollm2-135m  Q4_K_M 파일   101 MiB  ctx  512: KV   11 MiB  → tg 상한  87.0 t/s ( 11.5 ms/token)
smollm2-135m  Q4_K_M 파일   101 MiB  ctx 2048: KV   45 MiB  → tg 상한  66.8 t/s ( 15.0 ms/token)
smollm2-135m  Q4_0   파일    87 MiB  ctx  512: KV   11 MiB  → tg 상한  98.5 t/s ( 10.1 ms/token)
smollm2-135m  Q4_0   파일    87 MiB  ctx 2048: KV   45 MiB  → tg 상한  73.4 t/s ( 13.6 ms/token)
qwen2.5-0.5b  Q8_0   파일   506 MiB  ctx  512: KV    6 MiB  → tg 상한  19.0 t/s ( 52.7 ms/token)
qwen2.5-0.5b  Q8_0   파일   506 MiB  ctx 2048: KV   24 MiB  → tg 상한  18.3 t/s ( 54.5 ms/token)
qwen2.5-0.5b  Q4_K_M 파일   379 MiB  ctx  512: KV    6 MiB  → tg 상한  25.2 t/s ( 39.6 ms/token)
qwen2.5-0.5b  Q4_K_M 파일   379 MiB  ctx 2048: KV   24 MiB  → tg 상한  24.1 t/s ( 41.5 ms/token)
qwen2.5-0.5b  Q4_0   파일   336 MiB  ctx  512: KV    6 MiB  → tg 상한  28.5 t/s ( 35.1 ms/token)
qwen2.5-0.5b  Q4_0   파일   336 MiB  ctx 2048: KV   24 MiB  → tg 상한  27.0 t/s ( 37.0 ms/token)
```

출력에서 볼 것:

- **Qwen2.5-0.5B Q4_0이 약 27~28 t/s**. 사람이 말하는 속도(초당 2~3단어 ≈ 3~4 token)보다 훨씬 빠르니 TTS로 읽어 주기에는 충분하다. Q8_0이면 18~19 t/s로 떨어진다.
- **SmolLM2-135M은 KV가 의외로 크다**: token당 23,040 B로 Qwen(12,288 B)의 거의 두 배다(30층 × KV head 3 vs 24층 × 2). context 2048이면 KV 45 MiB가 가중치(87 MiB)의 절반이라 tg가 25% 깎인다. **작은 모델일수록 KV 비중이 커진다** — D5 2.2절의 "교차점"이 빨리 온다.
- 파일 크기에는 메타데이터(tokenizer 등 수 MB)가 포함되어 있어 약간 과대평가다. 그리고 이것은 **상한**이다. 7.6절의 thermal, 8.4절의 attention 효율, 8.5절의 코어 배치가 모두 이 숫자를 깎는다. 실제 기기에서 `llama-bench`로 확인해서 "상한 대비 몇 %"를 건강 지표로 쓴다(D5 4.6절).

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 형식 이름만 보고 크기·품질 예상 | "Q4_K_M인데 6 bit가 넘는다", "Q2_K가 Q4_0과 크기가 같다" | hidden 크기가 256의 배수가 아니라 k-quant fallback | 양자화 로그의 fallback 수와 BPW 확인, GGUFReader로 텐서별 형식 확인 |
| 작은 모델에 Q2_K / 3-bit | 출력이 무너짐, PPL 몇 배 | 작은 모델은 양자화에 민감 | Q4_K_M 이상, 필요하면 imatrix |
| 이미 양자화된 GGUF를 다시 양자화 | 품질이 이유 없이 나쁨 | 오차가 두 번 쌓임 | 항상 F16/BF16에서 출발 |
| `-ngl 0`을 CPU 전용으로 착각 | CPU pp가 이상하게 빠르거나 들쭉날쭉 | op offload로 GPU가 일부 계산 | 순수 CPU는 `-dev none` |
| thread를 코어 수만큼 다 줌 | thread를 늘렸는데 느려짐 | 효율 코어가 barrier에서 전체를 붙잡음, decode는 대역폭 포화 | 성능 코어 수 이하로, 코어 마스크 고정 |
| 짧은 벤치마크만 보고 결정 | 제품에서 몇 분 뒤 느려짐 | thermal throttling | `-r`을 늘려 시간 순서 샘플을 보고, 실제 사용 시간만큼 연속 측정 |
| `-c`를 모델 최대로 둠 | 메모리 부족, 앱이 죽음 | KV를 처음에 통째로 할당 | 제품 최대 대화 길이로 `-c` 고정 |
| KV를 q8_0으로 바꾸면 빨라질 거라 기대 | CPU에서 depth가 깊을 때 오히려 느림 | 풀어 쓰는 연산 추가 | 목표 backend·context에서 측정 후 결정 |
| `-fa off`에 `-ctv q8_0` | 컨텍스트 생성 실패 | 양자화 V는 FA가 필요 | `-fa on` |
| 측정 조건을 기록하지 않음 | 같은 명령인데 숫자가 4배 다름 | 다른 프로세스, thermal, GPU 공유, 커밋 차이 | load·온도·커밋·플래그를 결과와 함께 저장, 표준편차 확인 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "Walk me through deploying a Hugging Face model with llama.cpp."

**A.** 네 단계다. (1) `convert_hf_to_gguf.py`로 safetensors + config + tokenizer를 GGUF 하나로 만든다(F16). (2) `llama-quantize`로 Q8_0, Q4_K_M 등을 만들고 로그의 fallback 경고와 BPW를 확인한다. (3) `llama-perplexity`(가능하면 KL divergence)로 품질을, `llama-bench`로 pp/tg를 목표 기기에서 잰다. (4) `llama.h` C API로 앱에 넣는다 — model load(mmap), context(KV 크기 고정), decode 루프, sampler chain. 직접 해 보니 0.5B 모델의 hidden 크기가 256의 배수가 아니라 Q4_K_M이 대부분 Q5_0으로 fallback하는 것을 로그에서 발견했다.

> "Convert the safetensors checkpoint to an F16 GGUF with convert_hf_to_gguf, quantize with llama-quantize while checking the fallback warnings and the reported bits per weight, validate quality with llama-perplexity or KL divergence and speed with llama-bench on the target device, then embed it through the llama.h C API: load the model with mmap, create a context with a fixed KV size, run the decode loop, and sample through a sampler chain. When I did this with Qwen2.5-0.5B, I found its hidden size of 896 isn't a multiple of 256, so most Q4_K tensors silently fell back to Q5_0 — you only see that in the log."

**Q.** "pp vs tg — what limits each?"

**A.** pp(prefill)는 프롬프트 token 수백 개를 한 번에 처리하는 GEMM이라 가중치를 한 번 읽어 수백 번 재사용한다 — 연산량이 병목이다. tg(decode)는 token마다 가중치 전체를 읽는 GEMV라 메모리 대역폭이 병목이다. M2에서 재 보니 CPU F16 tg × 가중치 바이트가 58~63 GB/s로 STREAM 대역폭과 같았고, tg는 2 thread에서 이미 포화됐다. pp는 4 thread까지 선형으로 늘었고 GPU가 CPU의 약 3배였다.

> "Prompt processing is a batched GEMM: weights are read once and reused across hundreds of tokens, so it's compute-bound — it scaled linearly with cores on my M2 and the GPU was about 3x faster. Token generation is a GEMV per token that streams every weight, so it's bandwidth-bound: tg times model bytes came out at about 60 GB/s on the CPU, matching a STREAM measurement, and it saturated at just two threads."

**Q.** "Q4_K_M vs Q4_0 — what's the difference and which would you pick?"

**A.** Q4_0은 32개 블록마다 FP16 scale 하나(4.5 bit/w)인 단순 형식이다. Q4_K는 256개 super-block 안에 sub-block별 6-bit scale·min을 두는 2단계 scale(역시 4.5 bit/w)이라 같은 bit에서 오차가 작다. Q4_K_M은 "레시피"로, 출력 행렬과 일부 층의 attn_v·ffn_down을 Q6_K로 올린다(첫·끝 1/8 층과 가운데 3층마다). 내 측정에서 Pythia-160M은 Q4_K_M이 5.33 bit로 PPL +15%, Q4_0이 5.01 bit로 +27%였다. 보통 Q4_K_M을 고르지만, k-quant는 행 길이가 256의 배수여야 해서 작은 모델에서는 fallback을 확인해야 하고, 커널 속도도 형식마다 달라 tg를 같이 재야 한다.

> "Q4_0 is one FP16 scale per 32 weights. Q4_K uses 256-weight super-blocks with 6-bit sub-block scales and mins, so it has lower error at the same 4.5 bits. Q4_K_M is a mixing recipe on top: the output matrix and attn_v / ffn_down in roughly half the layers — first and last eighth plus every third layer — go to Q6_K. On Pythia-160M I measured +15% perplexity for Q4_K_M at 5.3 bits versus +27% for Q4_0 at 5.0 bits. I'd default to Q4_K_M, but check the quantize log for fallbacks on small models and measure tg, because kernel efficiency differs per format."

**Q.** "What is in a GGUF file?"

**A.** little-endian 바이너리로, 24바이트 header(magic "GGUF", 버전 3, 텐서 수, KV 수) → 메타데이터 KV(architecture, 층 수·head 수·context 길이·RoPE 같은 하이퍼파라미터, tokenizer 어휘·merge·특수 토큰·chat template, file type) → tensor info(이름, shape, ggml 타입, offset) → 32바이트 정렬된 텐서 데이터다. 정렬 덕분에 mmap 주소 + offset이 바로 텐서 포인터가 된다. 직접 struct로 파싱해 보니 SmolLM2 Q4_K_M은 텐서 272개, KV 31개, 메타데이터만 1.78 MB(대부분 tokenizer)였다.

> "It's a self-describing little-endian container: a 24-byte header with the GGUF magic, version, tensor count and KV count; then typed key-value metadata — architecture, hyperparameters like layer and head counts and context length, the full tokenizer with merges, special tokens and chat template, and the file type; then per-tensor info with name, shape, ggml type and offset; then tensor data aligned to 32 bytes, so the runtime can mmap the file and use base plus offset directly as tensor pointers."

**Q.** "How would you run llama.cpp on an Android-based wearable?"

**A.** NDK로 arm64-v8a 크로스 빌드(`GGML_NATIVE=OFF`, OpenMP off)부터 해서 CPU 기준선을 잡는다. big.LITTLE이니 thread 수와 코어 affinity를 먼저 정한다 — decode는 대역폭 병목이라 성능 코어 2개면 포화될 수 있다. 그다음 OpenCL(Adreno) backend, Hexagon backend 또는 벤더 스택(QNN/Genie)과 비교한다. 메모리는 mmap한 clean 가중치 + 고정 크기 KV + compute buffer로 예산을 세우고, repack이 익명 메모리를 늘리는지, Low Memory Killer에 걸리지 않는지 본다. 그리고 몇 분 연속 부하로 thermal throttling을 잰다 — 노트북에서도 20초 만에 pp가 28% 떨어지는 것을 봤다.

> "Cross-compile with the NDK for arm64-v8a with native tuning off, and get a CPU baseline with llama-bench over adb. Pin threads to the big cores — decode is bandwidth-bound and can saturate with two cores. Then try the OpenCL backend for Adreno and the Hexagon path, and compare against Qualcomm's own stack. Budget memory as mmapped clean weights plus a fixed KV cache and compute buffer, watch whether weight repacking adds anonymous memory that the low-memory killer would count, and run sustained tests for thermals — even on a fanless laptop I saw prefill throughput drop 28% within 20 seconds."

**Q.** "Your tokens/s on device is half of what you predicted. How do you debug it?"

**A.** 먼저 이상 모델(가중치 + KV 바이트 ÷ 대역폭)과의 비율을 본다. 그다음 하나씩 가른다: thread 수를 1, 2, 4로 바꿔 포화 지점을 보고(대역폭 병목인지), 형식을 Q8_0/Q4_0으로 바꿔 커널 효율 문제인지 보고(Q5_0·Q2_K는 dequant가 무거웠다), `-d`로 context 깊이를 바꿔 attention 비용을 보고, verbose 로그에서 scheduler split과 fallback op를 확인하고, 시간 순서 샘플로 thermal을 확인한다. 측정 조건(커밋, 플래그, 온도, 다른 부하)을 같이 기록한다.

> "First compare against the ideal: bytes per token over measured bandwidth. Then bisect: sweep threads to see where it saturates, switch to a simpler format like Q8_0 or Q4_0 to rule out dequant-heavy kernels, sweep context depth to isolate attention cost, check the verbose log for graph splits and ops falling back to the CPU, and look at per-run samples over time for thermal throttling — always recording commit, flags and conditions."

---

## 13. 직접 해보기

1. **손계산**: Q5_0 블록(32개)은 FP16 scale 2 B + 5번째 bit 4 B + 하위 4 bit 16 B다. bit/weight는? 그리고 SmolLM2의 `blk.0.attn_q.weight`(576 × 576)를 Q5_0으로 저장하면 몇 바이트인가?
   정답: 22 B / 32 = 5.5 bit/w. 576 × 576 / 32 = 10,368 블록 × 22 B = 228,096 B (약 0.22 MiB. quantize 로그의 `blk.0.attn_q.weight … converting to q5_0 .. size = 0.63 MiB -> 0.22 MiB` 줄과 일치).

2. **손계산**: Qwen2.5-0.5B(24층, KV head 2, head_dim 64)의 KV-cache를 `-c 4096`, K는 q8_0, V는 f16으로 두면 몇 MiB인가? 이 조합을 쓰려면 어떤 플래그가 필요한가?
   정답: K = 24 × 2 × 68 B × 4096 = 13,369,344 B = 12.75 MiB, V = 24 × 2 × 128 B × 4096 = 24 MiB, 합 36.75 MiB. V가 f16이므로 FA 없이도 되지만, K만 양자화할 때 backend 지원은 직접 확인한다(V 양자화는 `-fa on` 필수).

3. **코드**: 4.4절 스크립트를 Qwen2.5-0.5B의 Q2_K 파일에 돌려서 Q2_K, Q3_K, Q4_0 텐서가 각각 몇 개인지 세고, 3.4절의 fallback 표로 설명하라.
   힌트: d = 896을 입력으로 받는 행렬은 Q2_K/Q3_K → Q4_0으로, ffn_down(입력 4864)만 k-quant가 남는다.

4. **코드**: `llama-quantize --tensor-type` 옵션으로 SmolLM2의 `ffn_down`만 q8_0, 나머지는 Q4_0으로 만든 파일을 만들고, 6절과 같은 텍스트로 perplexity를 재서 Q4_0(26.43)과 비교하라. 크기는 얼마나 늘었나?
   힌트: `llama-quantize --tensor-type ffn_down=q8_0 in.gguf out.gguf Q4_0`. ffn_down은 층당 1536 × 576 = 0.88 M 원소 × 30층이다.

5. **측정**: `llama-bench -t 1,2,3,4 -n 64 -p 0`으로 Q8_0과 Q4_0의 tg를 재고, 각 thread 수에서 "tg × 가중치 바이트"를 계산하라. 어느 형식이 먼저 대역폭에 포화되는가? 왜 그런가?
   힌트: 바이트당 dequant 일이 적은 형식(Q8_0)이 더 적은 코어로 포화된다. 8.5절에서 Q4_0은 2 thread에서 이미 56 GB/s였다.

6. **C**: 9.2절 예제의 sampler chain을 `top_k(40) → temp(0.7) → dist(seed 42)`로 바꾸고, 같은 seed로 두 번 실행해 출력이 같은지, seed를 바꾸면 달라지는지 확인하라.
   힌트: `llama_sampler_init_top_k(40)`, `llama_sampler_init_temp(0.7f)`, `llama_sampler_init_dist(42)`를 순서대로 `llama_sampler_chain_add`. 마지막에 실제로 뽑는 sampler(`dist` 또는 `greedy`)가 있어야 한다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| llama.cpp | C/C++ LLM 추론 런타임 | ggml + libllama + 도구들. 의존성이 거의 없어 기기 이식이 쉽다 |
| ggml | 텐서 라이브러리 | 계산 그래프, 양자화 커널, backend 추상화 |
| GGUF | 모델 파일 포맷 | header + 메타데이터 KV + tensor info + 정렬된 데이터. mmap 친화적 |
| backend | 실행 장치 구현 | CPU, Metal, CUDA, Vulkan, OpenCL, Hexagon … |
| `ne[0]` | ggml의 첫 차원 | 메모리에서 연속된 차원(행 길이). PyTorch shape의 마지막 차원 |
| legacy quant | Q4_0, Q5_0, Q8_0 … | 32개 블록, 블록당 scale(과 min) |
| k-quant | Q2_K … Q6_K | 256개 super-block, 2단계 scale. 행 길이가 256의 배수여야 함 |
| Q4_K_M | k-quant 레시피 | Q4_K 기본 + 출력·일부 attn_v/ffn_down을 Q6_K |
| fallback (양자화) | 형식 대체 | 블록 크기가 안 맞으면 Q4_K → Q5_0, Q6_K → Q8_0 등 |
| tied embedding | 입력·출력 행렬 공유 | token_embd가 출력 projection을 겸함. 출력 형식을 따른다 |
| imatrix | importance matrix | calibration으로 모은 채널 중요도. 저비트 양자화 품질 향상 |
| pp | prompt processing | prefill 처리 속도(tokens/s). 연산 병목 |
| tg | token generation | decode 속도(tokens/s). 대역폭 병목 |
| `-ngl` | n_gpu_layers | GPU 버퍼에 둘 층 수 |
| CPU_REPACK | 가중치 재배열 | SIMD용 interleave(q4_0_4x8 등). 빠르지만 익명 메모리 사본 |
| mmap / mlock | 파일 매핑 / 페이지 고정 | clean 페이지로 로드 / 메모리에서 쫓겨나지 않게 |
| footprint | 프로세스 고유 메모리 | macOS의 익명·더티 메모리 합. mmap clean 페이지는 제외 |
| KV-cache | 과거 token의 K, V | `-c`만큼 미리 할당. 형식 `-ctk`/`-ctv` |
| flash attention (`-fa`) | 융합 attention 커널 | 점수 행렬을 만들지 않음. 양자화 V에 필요 |
| batch / ubatch | `-b` / `-ub` | decode 호출당 최대 token / 그래프 한 번에 계산하는 token |
| context shift | KV 밀어내기 | 가득 차면 오래된 token을 버리고 RoPE로 위치를 당김 |
| sampler chain | 샘플링 단계 묶음 | penalties → top-k → top-p → min-p → temperature → dist/greedy |
| perplexity | 품질 지표 | 정답 token 확률의 기하평균 역수. 작을수록 좋음 |
| thermal throttling | 열에 의한 감속 | 지속 부하에서 클럭이 내려감. 짧은 벤치마크는 과대평가 |

---

## 15. 요약 & 체크리스트

llama.cpp는 ggml 텐서 라이브러리 위의 LLM 런타임으로, GGUF 파일 하나(메타데이터 + tokenizer + 정렬된 텐서)를 mmap해서 CPU부터 Metal, OpenCL(Adreno), Hexagon까지 여러 backend에서 돌린다. HF 모델은 `convert_hf_to_gguf.py` → `llama-quantize` 두 단계로 기기용 파일이 되지만, 형식 이름이 실제 내용을 보장하지 않는다 — hidden 크기가 256의 배수가 아닌 작은 모델(SmolLM2 d = 576, Qwen2.5-0.5B d = 896)에서는 k-quant가 대부분 Q5_0/Q8_0/Q4_0으로 fallback했고, tied embedding은 출력 행렬 대접을 받아 Q8_0으로 남아 파일의 42%를 차지했다. 품질은 Q8_0이 사실상 무손실(+0.3%), Q4_K_M이 Q4_0보다 분명히 좋았고(Pythia +15% vs +27%), Q2_K는 작은 모델을 무너뜨렸다. 속도는 D5 이론대로였다 — CPU tg × 가중치 바이트가 STREAM 대역폭(약 60 GB/s)과 같았고 2 thread에서 포화됐으며, pp는 연산량을 따라 코어 수에 비례하고 GPU가 약 3배였다. 동시에 dequant가 무거운 형식, KV 양자화, 효율 코어, thermal, GPU 공유가 이론값을 깎는 것도 직접 봤다. 앱에는 `llama.h`의 model → context → decode → sampler 흐름으로 넣고, 기기에서는 mmap·repack·KV·compute buffer로 메모리 예산을 세운다.

- [ ] HF 모델을 F16 GGUF로 변환하고, 로그에서 텐서 이름·dtype·tied embedding을 읽을 수 있다
- [ ] `llama-quantize` 로그의 BPW와 fallback 경고를 보고 "왜 이론보다 큰지" 설명할 수 있다
- [ ] Q8_0, Q4_0, Q4_K, Q6_K의 블록 바이트와 bit/weight를 손으로 계산할 수 있다
- [ ] `use_more_bits` 규칙으로 Q4_K_M에서 어떤 층이 Q6_K를 받는지 계산할 수 있다
- [ ] GGUF header를 struct로 파싱하고, GGUFReader로 메타데이터와 텐서별 형식을 출력할 수 있다
- [ ] `llama-perplexity`로 형식별 품질을 비교하고 작은 평가의 한계를 말할 수 있다
- [ ] `llama-bench`의 pp/tg를 연산량·대역폭으로 환산해서 어느 쪽 병목인지 판정할 수 있다
- [ ] thread 수, KV 형식, FA, ubatch, repack, mmap 옵션이 속도·메모리에 주는 영향을 측정으로 보일 수 있다
- [ ] `llama.h`로 로드 → tokenize → decode → sample 루프를 C로 짤 수 있다
- [ ] Android/Snapdragon 빌드 경로와 기기 메모리·thermal 체크 항목을 말할 수 있다

---

## 참고 자료

- llama.cpp 저장소: [github.com/ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp) — 이 노트는 커밋 `f7b384c` 기준. 저장소 안 문서: `docs/build.md`, `docs/android.md`, `docs/backend/OPENCL.md`, `docs/backend/snapdragon/README.md`, `tools/server/README.md`, `tools/llama-bench/README.md`
- ggml 저장소: [github.com/ggml-org/ggml](https://github.com/ggml-org/ggml) — GGUF 명세 문서 `docs/gguf.md`
- Hugging Face Hub의 GGUF 문서: [huggingface.co/docs/hub/gguf](https://huggingface.co/docs/hub/gguf)
- 소스에서 직접 확인한 곳: `src/llama-quant.cpp`(Q4_K_M 레시피, `tensor_type_fallback`), `ggml/src/ggml-common.h`(블록 구조체), `ggml/src/ggml-blas/ggml-blas.cpp`(BLAS offload 조건), `src/llama-context.cpp`(양자화 V와 FA), `include/llama.h`(C API), `examples/simple/simple.cpp`
- 이 노트 세트: B8(SLM 구조), C3(양자화 이론, GGUF 블록 §8, KV 양자화 §9), D3(roofline), D5(prefill/decode 해석 모델, 이 Mac의 대역폭), E9(전력·thermal), F4(Qualcomm 스택), F7(크로스 빌드), F8(벤더 SDK bring-up), L2(온디바이스 LLM 스택)
