# Embedded AI Engineer — Study Prep List (나침반)

> **작성일**: 2026-09-27 · **대상 JD**: Hark Embedded AI Engineer (id 4392090009)
> **용도**: 이 직군에서 일하려면 알아야 할 개념을 **빠짐없이 나열한 목차**. 각 항목을 하나씩 노트(쉬운 설명 · 차트 · 샘플 코드)로 채워 간다
> **컨텍스트**: [포지션 컨텍스트](../../../../job_interview_prep_research/hark_ai_embedded_ai/hark_ai_embedded_ai_context.html) · [BSP 자리 노트(재사용 가능)](../../../../job_interview_prep_research/hark_ai_embedded_swe/2026-09-19_hark_study_notes/site/index.html)
> **노트 사이트**: [전체 노트 목록](../index.html) — 빌드 `python3 build_site.py` · 작성 규칙 `NOTES_SPEC.md` · 예제 실행 `.venv/bin/python`

---

## 0. 이 문서 읽는 법

**표기**
- **ID**: 모듈 알파벳 + 번호 (예: `C1`). 나중에 노트 파일 이름으로 쓴다 → `C1_quantization.md`
- **우선순위**: `P0` = tech session 전에 반드시 · `P1` = 온사이트·실무 투입 전 · `P2` = 넓게 알아 두기
- **Don 출발점** (셀 색으로 표시):
  - ✅ 이미 강함 → 복습하고 ML 맥락에 연결만 하면 됨
  - 🟡 인접 경험 있음 → 기존 지식에서 확장
  - ❌ 새로 배움 → 처음부터

**전체 그림 — 이 직군이 다루는 스택**
```
 ┌──────────── Cloud ─────────────┐
 │  Big LLM · 학습 · 데이터 레이크      │  ← L3 hybrid 라우팅, H3 ingestion
 └───────────────▲────────────────┘
                 │ BLE / Wi-Fi / Cellular (H2)
 ┌───────────────┴────── Device ──────────────────────────┐
 │  App SoC (Cortex-A, Android)                            │
 │   ├─ NPU / GPU  ← SLM, vision, ASR   (E5, E6, F3, F4, L) │
 │   └─ DSP (Hexagon/HiFi) ← 오디오 전처리, KWS (E4, G5)     │
 │  Always-on MCU (Cortex-M / RISC-V) ← VAD, IMU 모델 (E2, F2)│
 │   └─ 센서: IMU · Mic · Camera · PPG … (G1~G7)            │
 │  메모리: SRAM / TCM / LPDDR  (E7)   전원·열 (E9, K3, K4)   │
 └────────────────────────────────────────────────────────┘
      ↑ 이 모든 것을 가로지르는 것: 성능 모델(D) · 경량화(C) · Co-design(I)
```

**공부 흐름 (큰 순서)**
```
A 기초 ─► B 모델 ─► C 경량화 ─► D 성능 모델 ─┐
                                           ├─► I Co-design ─► L 온디바이스 LLM
E HW 아키텍처 ─► F 런타임 ─► J 펌웨어 통합 ─► K 프로파일링 ┘        │
G 센서·DSP ─► H 데이터 파이프라인 ──────────────────────────────────┘
M 실리콘 선정 · N 도메인 · O 툴 · P 인터뷰는 병행
```

---

## 1. JD 문장 → 모듈 매핑

| JD 문장 | 필요한 모듈 |
|---|---|
| Build data collection and ingestion pipelines … various sensors, at scale | G1–G7, H1–H8 |
| Co-design model architectures that meet latency, memory, power, bandwidth | B, C, D, I1–I6 |
| Work with platform vendors to bring up toolchains, SDKs and new accelerator | E4–E6, F1–F8 |
| Integrate ML inference into embedded firmware in C, C++, or Rust | F2, J1–J6 |
| Profile and optimize memory usage, power consumption, real-time performance | D, E7, E9, K1–K5 |
| Evaluate and select silicon platforms (GPUs, NPUs etc.) | E, D, M1–M4 |
| 5 yrs ML engineering, 2+ yrs edge ML | A1–A6, B, C (경력 대신 프로젝트로 증명 → 3장) |
| Familiarity with embedded systems, CPU/DSP/NPU HW architectures | E1–E9 |
| Hands-on with IMUs, accelerometers, gyroscopes, microphones | G1–G4, G7 |
| Experience building sensor data collection pipelines | H1–H8 |
| Embedded ML runtimes (TFLite, llama.cpp, QNN) | F1–F5 |
| Optimizing models for MCUs & edge processors (Cortex-M/A, RISC-V, DSP) | C1–C6, E2–E4, J3 |
| Deploying workloads on NPUs or specialized accelerators | E5, F4, F8, I2 |
| (우대) Audio/Voice/Vision models | B5, B6, G4, G5 |
| (우대) Lightweight LLM models | B8, F3, L1–L2 |
| (우대) CNN, RNN, transformers, KV-cache, memory bandwidth | B2–B4, D1–D7 |
| (우대) Hybrid edge-LLM pipelines, SLM on device | L1–L6 |
| (우대) Wearables, robotics, industrial sensing, IoT | N1–N4 |

---

## 2. 모듈별 개념 목록

### A. 수학 · ML 기초

| ID | 개념 | 세부 키워드 (공부할 것) | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| A1 | 선형대수 for ML | 벡터·행렬·텐서 shape<br>행렬곱과 GEMM, `[M,K]×[K,N]` 비용 = M·N·K MAC<br>broadcasting, transpose, reshape<br>내적·노름·코사인 유사도 | 모든 레이어가 결국 GEMM. 성능 계산의 출발점 | 🟡 Berkeley 응용수학 — 복습 | P0 |
| A2 | 확률 · 통계 | 확률분포, 평균·분산, 정규분포<br>softmax, log-likelihood, cross-entropy<br>베이즈 기초, 조건부 확률 | loss 함수·출력 해석·샘플링 이해 | 🟡 | P1 |
| A3 | 최적화 · 역전파 | 미분·chain rule, backprop 계산 그래프<br>SGD, momentum, Adam, learning rate schedule<br>vanishing/exploding gradient | QAT·fine-tuning 이해, "모델 학습을 해 봤다" 증명 | 🟡 수학은 있음, 실습 없음 | P0 |
| A4 | 학습 워크플로 | train/val/test split, overfitting, regularization(dropout, weight decay)<br>data augmentation (시계열·오디오·이미지)<br>metrics: accuracy, precision/recall, F1, ROC-AUC, confusion matrix, FAR/FRR(wake word)<br>시계열 **data leakage** (사용자·기기 단위 split) | 모델팀과 대화, 센서 모델 평가 | ❌ | P0 |
| A5 | PyTorch 기본 | tensor, autograd, `nn.Module`, `DataLoader`<br>학습 루프 작성, checkpoint 저장<br>`torch.export` / ONNX export | 모델을 직접 만들고 내보내야 배포를 할 수 있음 | ❌ | P0 |
| A6 | 데이터 도구 | numpy, pandas, matplotlib<br>Jupyter로 센서 로그 시각화 | 센서 데이터 분석·프로파일 결과 시각화 | 🟡 Python 있음 | P1 |

### B. 모델 아키텍처 (Edge에서 실제로 쓰는 것)

| ID | 개념 | 세부 키워드 | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| B1 | MLP · 활성화 · 정규화 | fully-connected, ReLU/GELU/SiLU<br>BatchNorm, LayerNorm, RMSNorm<br>**BN folding** (conv에 합치기) | 모든 모델의 부품. BN folding은 배포 최적화 단골 | ❌ | P0 |
| B2 | CNN | conv2d 계산법(stride, padding, dilation), receptive field<br>**depthwise separable conv**, pointwise(1×1)<br>MobileNet v1–v4, EfficientNet, ResNet(skip connection)<br>**1D conv / TCN** (시계열·오디오) | JD 우대 "CNN". 오디오·IMU·비전 모두 CNN 계열 | ❌ | P0 |
| B3 | RNN · LSTM · GRU | hidden state, gate 구조<br>**streaming/stateful inference** (프레임 단위 처리)<br>RNN이 NPU에서 불리한 이유(순차 의존성) | JD 우대 "RNN". 스트리밍 오디오·센서 | ❌ | P1 |
| B4 | Attention · Transformer | Q·K·V, scaled dot-product, softmax<br>multi-head, **MHA vs MQA vs GQA**<br>positional encoding, **RoPE**<br>FFN, SwiGLU, residual, pre-norm<br>encoder vs decoder-only, causal mask | JD 우대 "transformers, KV-cache". LLM·ASR·ViT 전부 | ❌ | P0 |
| B5 | 오디오 · 음성 모델 | KWS/wake word (DS-CNN), VAD<br>ASR: CTC, RNN-T, Conformer, **Whisper**<br>speaker verification, speech enhancement<br>TTS 기초, speech tokenizer/audio codec | JD 우대 "Audio/Voice". Hark = speech 중심 | ❌ | P0 |
| B6 | 비전 모델 | classification, detection(YOLO, SSD), segmentation<br>ViT, CLIP 계열 이미지 인코더<br>VLM(이미지+텍스트) 개요 | JD 우대 "Vision". multimodal 기기 | ❌ | P1 |
| B7 | IMU · 센서 모델 | HAR(activity recognition), gesture, wear/on-body detection, fall detection<br>anomaly detection (autoencoder)<br>**고전 ML**: 특징 추출 + decision tree / random forest / gradient boosting (MCU에서 매우 흔함) | IMU 요건. 가장 작은 모델이 always-on에서 돈다 | ❌ | P0 |
| B8 | 소형 LLM (SLM) | Llama 3.2 1B/3B, Qwen 0.5B–4B, Gemma, Phi, SmolLM<br>tokenizer: BPE, SentencePiece, vocab 크기의 비용<br>sampling: greedy, temperature, top-k/top-p<br>speech-LLM, VLM 입력 구조 | JD 우대 "lightweight LLM" | ❌ | P0 |
| B9 | 효율 아키텍처 (넓게) | Mixture-of-Experts, State Space Model(Mamba), linear attention<br>early-exit, cascade 모델 | 차세대 모델과 HW 선정 대화 | ❌ | P2 |

### C. 모델 경량화 · 최적화

| ID | 개념 | 세부 키워드 | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| C1 | Quantization 이론 | scale·zero-point, symmetric vs asymmetric<br>per-tensor / per-channel / **per-group**<br>INT8 · INT4 · FP16 · BF16 · FP8<br>**requantization 수식** (고정소수점 multiplier + shift)<br>calibration: min-max, percentile, KL/entropy | 최적화 질문 1순위. 펌웨어 고정소수점 경험과 직결 | 🟡 고정소수점·비트 조작은 강함 | P0 |
| C2 | PTQ vs QAT | post-training quantization 절차<br>quantization-aware training, fake quant, STE<br>정확도 손실 디버깅 (layer별 SQNR) | "모델 최적화 경험" 요건 | ❌ | P0 |
| C3 | LLM 양자화 | weight-only(W4A16) vs W8A8<br>activation outlier 문제, SmoothQuant<br>GPTQ, AWQ<br>KV-cache 양자화 | 경량 LLM·NPU 배포 | ❌ | P0 |
| C4 | Pruning · Sparsity | unstructured vs structured(채널) pruning<br>N:M sparsity (2:4)<br>"sparsity가 실제로 빨라지는 조건" (HW 지원) | co-design, TOPS 스펙 해석 | ❌ | P1 |
| C5 | Knowledge Distillation | teacher–student, soft label, 온도<br>대형 모델 → 온디바이스 모델 | Hark 서버 모델 → 기기 모델 경로 | ❌ | P1 |
| C6 | 그래프 최적화 | op fusion (conv+BN+ReLU), constant folding<br>layout NHWC vs NCHW, 채널 정렬(8/16/32 배수)<br>operator lowering, unsupported op 대체 | 벤더 컴파일러가 하는 일 이해 | 🟡 컴파일러·툴체인 감각 | P0 |
| C7 | HW-aware 모델 설계 | NAS, MCUNet, Once-for-All<br>latency lookup table로 모델 검색 | "co-design model architectures" | ❌ | P2 |
| C8 | 최적화 후 검증 | golden reference, bit-exact vs tolerance 비교<br>layer별 diff, SQNR/cosine 유사도<br>정확도 회귀 테스트 | 배포 품질 보증 — 펌웨어 검증 경험과 연결 | ✅ 검증·root cause 강함 | P0 |

### D. 성능 모델 (Performance characteristics) — 이 직군의 핵심 사고법

| ID | 개념 | 세부 키워드 | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| D1 | FLOPs · MACs 계산 | conv, depthwise, FC, attention 레이어별 MAC 공식<br>모델 전체 연산량 손계산 | 모든 back-of-envelope의 기본 | 🟡 | P0 |
| D2 | 메모리 계산 | parameter 크기 = 파라미터 수 × 바이트<br>activation 크기, **peak memory**, tensor lifetime<br>memory planner (arena 재사용) | MCU SRAM 수백 KB 안에 넣기 | ✅ SRAM/DRAM 예산 관리 경험 | P0 |
| D3 | Roofline · Arithmetic intensity | ops/byte, compute-bound vs memory-bound<br>roofline 그래프 그리기, ridge point | HW 선정·병목 설명의 공용어 | 🟡 대역폭 감각 있음 | P0 |
| D4 | 모델 계열별 성능 특성 | CNN: compute-bound, 재사용 높음<br>depthwise: memory-bound, NPU 효율 낮음<br>RNN: 순차성, 작은 GEMV<br>Transformer: seq 길이², attention memory | JD 우대 문장 그대로 | ❌ | P0 |
| D5 | LLM 추론 성능 | **prefill(compute-bound) vs decode(memory-bound)**<br>**KV-cache 크기** = 2 × layers × kv_heads × head_dim × seq × bytes<br>tokens/s 상한 ≈ 메모리 대역폭 ÷ (모델 바이트 + KV 바이트)<br>TTFT, TPOT, context 길이 영향<br>GQA가 KV를 줄이는 원리, paged/sliding window KV | JD 우대 "KV-cache behavior, memory bandwidth" | ❌ | P0 |
| D6 | Latency · Throughput · Real-time | 평균 vs tail latency, jitter<br>batch=1 스트리밍의 특성<br>deadline 기반 설계 (오디오 프레임 10–20 ms) | "real-time performance" | ✅ 펌웨어 성능 튜닝 | P0 |
| D7 | 에너지 모델 | 연산당 에너지 vs DRAM 접근 에너지 (DRAM이 수백 배)<br>"data movement dominates"<br>energy per inference, TOPS/W 해석 | 배터리 기기 전력 예산 | 🟡 Power Analyzer 경험 | P0 |

### E. HW 아키텍처 — CPU · DSP · NPU · GPU

| ID | 개념 | 세부 키워드 | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| E1 | CPU 마이크로아키텍처 | pipeline, superscalar, out-of-order<br>cache 계층, cache line, TLB, prefetch<br>branch prediction | 커널 최적화·프로파일 해석 | 🟡 | P1 |
| E2 | ARM Cortex-M / Cortex-A | M4F, M7, M33, **M55/M85 + Helium(MVE)**<br>A55/A7x/X 계열, big.LITTLE<br>**NEON**, SVE2, dot-product 명령(SDOT/UDOT), i8mm | JD "ARM Cortex-M/A". SIMD가 ML 커널의 핵심 | ✅ Cortex-R/M bring-up · SIMD는 🟡 | P0 |
| E3 | RISC-V | base ISA, 확장(M, F, C), **RVV vector extension**<br>custom instruction, 예: ESP32-C/P, SiFive | JD "RISC-V" | ❌ | P2 |
| E4 | DSP 아키텍처 | VLIW, SIMD, MAC 유닛, zero-overhead loop, circular addressing<br>Q-format 고정소수점<br>**Cadence Tensilica HiFi / Vision (Xtensa)**, CEVA<br>**Qualcomm Hexagon: scalar + HVX(vector) + HMX(matrix)** | JD "DSP". Hark 단서 "Hexagon DSP" | 🟡 Xtensa FW 경험 | P0 |
| E5 | NPU 아키텍처 | MAC array, **systolic array**<br>dataflow: weight-/output-/row-stationary<br>on-chip SRAM, DMA, **tiling**<br>지원 op·정밀도 제약, 컴파일러의 역할<br>예: Arm Ethos-U55/U65/U85, Qualcomm Hexagon NPU, Apple ANE, Google Edge TPU, NXP Neutron, MediaTek APU | JD "NPU", "new accelerator bring-up" | 🟡 새 IP bring-up은 강함, NPU 내부는 새로 | P0 |
| E6 | GPU | SIMT, warp/wavefront, occupancy<br>모바일 GPU(Adreno, Mali), Jetson Orin<br>GPU가 batch=1 추론에서 불리한 경우 | JD "GPUs" 선정 | ❌ | P1 |
| E7 | 메모리 시스템 | SRAM / TCM / cache / LPDDR4X·5·5X 대역폭 계산<br>DMA, scatter-gather, cache coherence<br>unified memory, 메모리 대역폭 = 버스 폭 × 전송률 | LLM 성능의 결정 요인 | ✅ DRAM/SRAM bring-up · PCIe 대역폭 | P0 |
| E8 | 이기종 SoC 구조 | always-on island, sensor hub, low-power domain<br>IPC / mailbox / shared memory<br>MCU ↔ DSP ↔ NPU ↔ AP 역할 분담 | Hark 기기 구조(Qualcomm + Ambiq급 MCU 추정) | 🟡 multi-core interconnect 경험 | P0 |
| E9 | 전력 · 열 | dynamic power ∝ C·V²·f, leakage<br>DVFS, clock/power gating, retention<br>race-to-idle, thermal throttling, 피부 온도 한계 | "power consumption" | 🟡 margin sign-off 경험 | P0 |

### F. Embedded ML 런타임 · 툴체인

| ID | 개념 | 세부 키워드 | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| F1 | TFLite / LiteRT | flatbuffer 모델 포맷, converter<br>interpreter, **delegate** (XNNPACK, GPU, Hexagon/QNN)<br>quantized 모델 변환 흐름 | JD 명시 "TFLite" | ❌ | P0 |
| F2 | TFLite Micro | **tensor arena** (malloc 없음), op resolver<br>**CMSIS-NN** 커널, Ethos-U 연동, Vela 컴파일러<br>모델을 C 배열로 flash에 넣기 | MCU 배포 표준 | ❌ (bare-metal 기반은 ✅) | P0 |
| F3 | llama.cpp / GGML | GGUF 포맷, 양자화 타입(Q4_0, Q4_K_M, Q8_0)<br>백엔드: CPU NEON, Metal, Vulkan, OpenCL(Adreno)<br>mmap 로딩, KV-cache 옵션, `llama-bench` | JD 명시 "llamacpp" | ❌ | P0 |
| F4 | Qualcomm 스택 | **QNN (AI Engine Direct)**: backend(CPU/GPU/HTP), context binary<br>SNPE(구세대), Hexagon SDK<br>**Qualcomm AI Hub** (클라우드 실기기 컴파일·프로파일)<br>Genie (온디바이스 LLM) | JD 명시 "QNN". Hark 투자사·칩 단서 | ❌ | P0 |
| F5 | ONNX / ONNX Runtime | PyTorch → ONNX export, opset<br>execution provider (QNN EP 등)<br>그래프 확인 도구 (Netron) | 벤더 간 공통 교환 포맷 | ❌ | P1 |
| F6 | 그 외 런타임 · 컴파일러 | ExecuTorch, Core ML, MediaPipe<br>TVM / MLIR / IREE 개념<br>Edge Impulse, STM32Cube.AI | 플랫폼 평가 시 비교 대상 | ❌ | P2 |
| F7 | 크로스 빌드 · 배치 | GCC/Clang 크로스 컴파일, CMake<br>**링커 스크립트로 모델·arena 배치** (flash / SRAM / TCM 섹션)<br>Android NDK 빌드 | 펌웨어에 모델 넣기 | ✅ 링커·툴체인 경험 | P0 |
| F8 | 벤더 SDK bring-up 절차 | 버전 매트릭스, op 지원표 읽기<br>CPU **fallback** 찾기·제거<br>벤더 프로파일러 사용, FAE와 이슈 주고받기<br>정확도 불일치 디버깅 (전처리, 스케일, layout) | JD "bring up toolchains, SDKs and new accelerator" | ✅ 벤더 협업·새 IP bring-up | P0 |

### G. 센서 · 신호처리

| ID | 개념 | 세부 키워드 | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| G1 | IMU 원리 | MEMS 가속도계·자이로·지자기, 6축/9축<br>full-scale range, noise density, bias, drift, ODR, bandwidth<br>예: Bosch BMI270, ST LSM6DSx, TDK ICM-4xxxx | JD "IMUs, accelerometers, gyroscopes" | ❌ | P0 |
| G2 | IMU 인터페이스 · 드라이버 | SPI/I2C 레지스터 설정, **FIFO + watermark 인터럽트**<br>wake-on-motion, step counter 등 내장 기능<br>센서 내장 ML core (ST MLC) | 저전력 수집의 핵심 | ✅ SPI/I2C bring-up | P0 |
| G3 | IMU 처리 · 센서 퓨전 | calibration (bias, scale, misalignment)<br>좌표계, 회전 행렬, **quaternion**<br>complementary filter, Madgwick/Mahony, Kalman/EKF | 전처리·특징, 로보틱스 연결 | 🟡 수학 배경 | P1 |
| G4 | 마이크 · 오디오 HW | MEMS 마이크, **PDM vs I2S/TDM**, SNR, AOP, sensitivity<br>PDM → PCM decimation (CIC 필터)<br>마이크 어레이, beamforming, AEC, noise suppression | JD "microphones", Hark speech 중심 | ❌ (I2S 개념은 BSP 노트에 일부) | P0 |
| G5 | DSP 기초 | 샘플링, Nyquist, aliasing<br>FIR/IIR 필터, windowing, **FFT/STFT**<br>**mel spectrogram, MFCC** (오디오 모델 입력)<br>고정소수점 DSP 구현, CMSIS-DSP | 모델 전처리 = DSP | 🟡 수학 배경 | P0 |
| G6 | 기타 센서 | 카메라 (MIPI CSI, ISP 파이프라인 개요)<br>PPG, ToF/근접, 기압계, 온도, 터치<br>착용 감지 | multimodal 기기 | ❌ | P2 |
| G7 | 시간 동기화 | 센서별 timestamp, 클럭 drift<br>multi-sensor alignment, resampling, interpolation<br>샘플 누락 검출 | "data collection pipelines" 품질 | 🟡 | P0 |

### H. 데이터 수집 · Ingestion 파이프라인

| ID | 개념 | 세부 키워드 | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| H1 | 온디바이스 로깅 | DMA + ring buffer → flash<br>로그 포맷 (헤더+바이너리, protobuf, FlatBuffers, CBOR)<br>압축, flash wear, 저장 용량 예산 | JD 1번 업무 | ✅ SSD·flash·링버퍼 | P0 |
| H2 | 전송 | BLE throughput(MTU, connection interval), Wi-Fi, USB<br>배치 업로드, 재개(resume), CRC 무결성 | 기기 → 서버 | 🟡 | P1 |
| H3 | 백엔드 ingestion | object storage(S3), queue(Kafka/PubSub)<br>스키마·메타데이터(기기 ID, FW 버전, 센서 설정)<br>데이터 버전 관리 | "at scale" | 🟡 테스트 플랫폼 SDK 경험 | P1 |
| H4 | 라벨링 | 수동 라벨, 동기화된 영상·ground truth<br>weak labeling, active learning | 센서 데이터셋 만들기 | ❌ | P1 |
| H5 | 데이터셋 관리 | 사용자·기기 단위 split, class imbalance<br>data versioning (DVC 등), dataset card | 모델팀과 협업 | ❌ | P1 |
| H6 | 프라이버시 · 동의 | always-listening 기기의 PII, 온디바이스 익명화<br>보관 기간, GDPR/CCPA 기초 | 컨슈머 AI 기기 필수 | ❌ | P1 |
| H7 | 데이터 품질 모니터링 | 샘플 드롭, clipping, 센서 saturation<br>분포 drift 탐지, 자동 리포트 | fleet 운영 | ✅ telemetry·health monitoring 경험 | P1 |
| H8 | Fleet 규모 수집 | 원격 로깅 설정, 샘플링 정책<br>FW 버전별 태깅, dogfood 기기 관리 | "at scale" | 🟡 챔버 자동화 경험 | P2 |

### I. HW/SW Co-design

| ID | 개념 | 세부 키워드 | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| I1 | 예산 설정 | latency · memory · power · bandwidth 예산을 숫자로 정하기<br>예산 → 모델 크기·연산량 상한으로 변환 | JD 2번 업무 그대로 | 🟡 margin sign-off | P0 |
| I2 | HW 친화 모델 설계 | NPU 지원 op만 쓰기, 채널 정렬<br>activation 크기 줄이기, 양자화 친화 구조 | 모델팀에 줄 피드백 | ❌ | P0 |
| I3 | 연산 분할 · Cascade | MCU → DSP → NPU → Cloud 단계별 역할<br>wake cascade (VAD → KWS → ASR → LLM)<br>false wake 비용과 전력 | 기기 전체 AI 아키텍처 | 🟡 | P0 |
| I4 | 전처리·후처리 배치 | 특징 추출을 DSP에, 모델은 NPU에<br>데이터 이동 최소화 | 전력 최적화 | 🟡 | P1 |
| I5 | 벤치마크 · 지표 | MLPerf Tiny, MLPerf Mobile<br>TOPS/W의 함정, sustained vs peak | HW 선정·보고 | ❌ | P1 |
| I6 | 피드백 루프 | 프로파일 → 병목 → 모델 구조 변경 → 재측정 | co-design 실무 | ✅ 측정 기반 디버깅 | P1 |

### J. 펌웨어 통합 (C / C++ / Rust)

| ID | 개념 | 세부 키워드 | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| J1 | 추론 통합 패턴 | 정적 메모리, arena 배치<br>입력 double buffering, ISR → task 전달<br>RTOS 추론 태스크와 우선순위 | JD 4번 업무 | ✅ bare-metal C/C++ | P0 |
| J2 | 실시간 설계 | deadline, WCET, jitter<br>센서 스트림과 추론 주기 맞추기 | real-time 요건 | ✅ | P0 |
| J3 | 고정소수점 · SIMD 커널 | Q-format 연산, saturation, rounding<br>CMSIS-DSP/NN, NEON·Helium intrinsics<br>INT8 matmul/conv 직접 작성 | 커널 최적화, 코딩 인터뷰 | 🟡 비트 조작 강함, SIMD는 새로 | P0 |
| J4 | 임베디드 C++ · Rust | C++: 예외/RTTI 없이, 템플릿, constexpr<br>Rust: ownership, `no_std`, embassy, C FFI | JD "C, C++, or Rust" | 🟡 C++ 있음, Rust ❌ | P2 |
| J5 | 모델 업데이트 | 모델 버전 관리, 런타임 호환성<br>A/B 슬롯·OTA로 모델 교체 | 제품 운영 | 🟡 SSD FW 업데이트 흐름 | P1 |
| J6 | 테스트 | golden vector, bit-exact 테스트<br>HIL(hardware-in-the-loop), 정확도 회귀 CI | 품질 | ✅ 검증·factory test 경험 | P1 |

### K. 프로파일링 · 최적화

| ID | 개념 | 세부 키워드 | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| K1 | 메모리 프로파일링 | map 파일 읽기, stack/heap 분석<br>arena 사용량, peak memory 측정 | JD 5번 업무 | ✅ | P0 |
| K2 | 성능 프로파일링 | DWT CYCCNT, PMU 카운터<br>Linux `perf`, Android `simpleperf`<br>Snapdragon Profiler, QNN profiler, Arm Streamline, Trace32 | "real-time performance" | ✅ Trace32 · 🟡 모바일 툴 | P0 |
| K3 | 전력 측정 | Nordic PPK2, Joulescope, Monsoon<br>energy per inference, duty-cycle 평균 전류 계산 | "power consumption" | ✅ Power Analyzer | P0 |
| K4 | 열 · 지속 성능 | sustained vs burst, throttling 곡선<br>장시간 벤치마크 방법 | 웨어러블 열 한계 | 🟡 | P1 |
| K5 | 커널 최적화 기법 | loop tiling, cache blocking, im2col<br>Winograd 개념, DMA와 연산 겹치기<br>메모리 layout 변경 | 성능 개선 실무 | 🟡 | P1 |

### L. 온디바이스 LLM · Hybrid Edge-Cloud

| ID | 개념 | 세부 키워드 | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| L1 | SLM 선택 | 크기별 품질·메모리·속도 trade-off<br>벤치마크 보는 법, 라이선스 | JD 우대 "lightweight LLM" | ❌ | P0 |
| L2 | 온디바이스 LLM 스택 | tokenizer → runtime → sampler → streaming 출력<br>KV-cache 관리, context 한계, 메모리 압박 시 동작 | "integrating small language models on device" | ❌ | P0 |
| L3 | Hybrid 라우팅 | 기기 vs 클라우드 결정 기준: 지연, 프라이버시, 비용, 연결 상태, confidence<br>오프라인 fallback | JD 우대 "hybrid edge-LLM" | ❌ | P0 |
| L4 | 음성 파이프라인 end-to-end | VAD → wake word → ASR → LLM → TTS<br>단계별 지연 예산, barge-in, streaming ASR | Hark = speech 중심 AI 기기 | ❌ | P0 |
| L5 | 온디바이스 메모리 · RAG | embedding 모델, 소형 vector search<br>개인 데이터 저장과 프라이버시 | Hark "persistent memory" | ❌ | P2 |
| L6 | 디코딩 가속 | speculative decoding (draft 모델), prompt caching<br>온디바이스 draft + 클라우드 verify | 지연 단축 | ❌ | P2 |

### M. 실리콘 평가 · 선정

| ID | 개념 | 세부 키워드 | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| M1 | 평가 기준표 | TOPS, TOPS/W, 메모리 대역폭, SRAM 크기<br>지원 정밀도(INT4/FP16), op 커버리지<br>툴체인 성숙도, 벤더 지원, 전원 상태, 가격·공급 | JD 6번 업무 | 🟡 silicon validation 경험 | P0 |
| M2 | 벤치마크 방법론 | 대표 모델 세트, end-to-end vs 커널 단위<br>sustained 열 조건, 재현성 | 공정한 비교 | ✅ 측정·검증 방법론 | P1 |
| M3 | 벤더 지형 | Qualcomm (Snapdragon, AR1/AR2, W5), MediaTek, NXP i.MX 9x<br>Ambiq Apollo4/5, Nordic nRF54, STM32N6, Alif Ensemble(Ethos-U)<br>Syntiant, Hailo, Google Tensor, Apple (참고) | 선택지를 아는 것 | ❌ | P1 |
| M4 | 데이터시트 해석 | TOPS에 숨은 조건 (INT4, sparsity, peak)<br>실측과 스펙의 차이 | 벤더 마케팅 걸러내기 | ✅ 스펙·margin 검증 | P1 |

### N. 도메인 (웨어러블 · 로보틱스 · IoT)

| ID | 개념 | 세부 키워드 | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| N1 | 웨어러블 제약 | 배터리 용량(mAh)과 always-on 전력 예산<br>폼팩터, 피부 온도 한계, 무게 | JD 우대 "wearables" | 🟡 Apple 컨슈머 플랫폼 | P1 |
| N2 | 기존 제품 사례 | Apple Watch/AirPods, Pixel Watch, Meta Ray-Ban, Oura<br>Humane Pin · Rabbit R1이 실패한 이유 | Hark 제품 이해, 인터뷰 대화 | ❌ | P1 |
| N3 | 로보틱스 센싱 | 로봇 IMU·센서 퓨전, 실시간 제어 루프와 ML<br>Figure와의 연결 | JD 우대 "robotics" | ❌ | P2 |
| N4 | always-listening UX | 프라이버시 표시, false wake 비용, 사용자 신뢰 | 컨슈머 AI 기기 | ❌ | P2 |

### O. 소프트웨어 엔지니어링 · 툴

| ID | 개념 | 세부 키워드 | 왜 필요 | Don 출발점 | 우선순위 |
|---|---|---|---|---|---|
| O1 | Python 파이프라인 | 스크립트로 변환·양자화·벤치 자동화<br>CLI 도구, 로그 파싱 | 일상 업무 | 🟡 | P1 |
| O2 | Linux · Android 온디바이스 | adb, Android 빌드 개요, NNAPI 현황<br>Linux 사용자 공간에서 NPU 드라이버 호출 | Hark 기기는 Android 기반 추정 | 🟡 Linux 있음 | P1 |
| O3 | ML CI | 모델 정확도·지연 회귀 CI<br>실기기 farm (AI Hub 등) | 품질 자동화 | 🟡 테스트 자동화 경험 | P2 |

---

## 3. 경력 갭을 메울 Hands-on 프로젝트

JD 1번 요건(ML 5년)은 공부만으로 증명이 안 된다. **수치가 남는 프로젝트**로 대신한다.

| # | 프로젝트 | 다루는 모듈 | 산출물 (인터뷰에서 말할 숫자) | 우선순위 |
|---|---|---|---|---|
| PJ1 | IMU 제스처/활동 인식을 MCU에서 실행 (IMU 내장 보드 + TFLite Micro + CMSIS-NN) | A4, A5, B7, C1, F2, G1, G2, J1, K1–K3 | 정확도, 추론 지연(ms), arena 크기(KB), 평균 전류(µA) | P0 |
| PJ2 | 키워드 스포팅: 마이크 → MFCC → DS-CNN INT8 | B5, G4, G5, C2, J3 | FAR/FRR, 프레임당 지연, PTQ vs QAT 정확도 비교 | P0 |
| PJ3 | 소형 LLM을 llama.cpp로 Q4/Q8 양자화·벤치 | B8, C3, D5, F3, L1 | tokens/s(prefill/decode), 메모리, **대역폭 식 예측 vs 실측** | P0 |
| PJ4 | Qualcomm AI Hub로 모델 1개 QNN 컴파일·실기기 프로파일 | F4, F5, F8, E4, E5 | CPU vs GPU vs NPU 지연, fallback op 목록 | P0 |
| PJ5 | 센서 데이터 수집 파이프라인 미니판: 보드 → BLE/USB → 서버 저장 → 시각화 | G7, H1–H3, H7 | 샘플 드롭률, 처리량, 스키마 설계 | P1 |
| PJ6 | 음성 cascade 데모: VAD → KWS → (PC에서) ASR → SLM | I3, L3, L4 | 단계별 지연 표, 전력 추정 | P1 |
| PJ7 | INT8 conv/matmul C 커널 직접 작성 + NEON/Helium 버전 비교 | J3, K5, D3 | 사이클 수, 속도 향상 배수 | P1 |
| PJ8 | 가상 실리콘 선정 보고서: 후보 3개 비교표 | M1–M4, D3, E5 | 1–2쪽 문서 (인터뷰 case study 소재) | P2 |

---

## 4. 인터뷰 대비 연결

**Back-of-envelope 계산 드릴 (손으로 풀 수 있어야 함)**
- [ ] conv 레이어 MAC 수·파라미터 수·activation 크기
- [ ] 1B 모델 INT4 가중치 크기, LPDDR5 대역폭에서 decode tokens/s 상한
- [ ] KV-cache 크기 (layers, kv_heads, head_dim, context, 바이트)
- [ ] roofline에서 한 레이어가 compute-bound인지 memory-bound인지
- [ ] always-on 평균 전류 = Σ(상태 전류 × 시간 비율), 배터리 수명 계산
- [ ] 16 kHz 오디오, 10 ms hop일 때 프레임당 연산 예산
- [ ] IMU 6축 × 16bit × 200 Hz × 24시간 로그 크기

**코딩 드릴 (C)**
- [ ] INT8 matmul + requantize (zero-point, multiplier, shift, saturation)
- [ ] depthwise conv 1D/2D
- [ ] 링버퍼 sliding window + RMS/특징 추출
- [ ] DMA ping-pong 입력 버퍼 → 추론 태스크 전달
- [ ] 고정소수점 FIR 필터
- [ ] softmax (수치 안정 버전), argmax, top-k

**시스템 설계 질문**
- [ ] 웨어러블 always-on 음성 비서 전체 아키텍처 (전력·지연 예산 포함)
- [ ] 수천 대 기기의 센서 데이터 수집 시스템
- [ ] 차세대 기기 NPU 선정 과정
- [ ] 모델팀이 준 모델이 NPU에서 목표 지연의 3배 — 어떻게 줄이나

**내 경험과 연결할 이야기**
- [ ] 새 IP/실리콘 bring-up → NPU·벤더 SDK bring-up (F8, E5)
- [ ] Xtensa 데이터 경로 FW → DSP 아키텍처 (E4)
- [ ] 펌웨어 성능 튜닝 → 모델 프로파일링 (K2)
- [ ] 테스트 플랫폼 SDK → 센서 데이터 파이프라인 (H3, H8)
- [ ] margin sign-off → latency/power 예산 결정 (I1)

---

## 5. 참고 자료 (각 모듈 공부 시 출발점)

- **MIT 6.5940 TinyML and Efficient Deep Learning** (Song Han) — C, D, E5, L 전반. 강의 영상·슬라이드 공개
- **TinyML** (Pete Warden, Daniel Situnayake, O'Reilly) — F2, PJ1, PJ2
- **Efficient Processing of Deep Neural Networks** (Sze, Chen, Yang, Emer) — E5 dataflow, D7 에너지
- **Andrej Karpathy — Neural Networks: Zero to Hero**, `nanoGPT`, `llama2.c` — A3, A5, B4, B8
- Roofline 논문 (Williams, Waterman, Patterson, 2009) — D3
- Horowitz, "Computing's Energy Problem" (ISSCC 2014) — D7
- "AI and Memory Wall" (Gholami et al.) — D5, E7
- GitHub: `tensorflow/tflite-micro`, `ARM-software/CMSIS-NN`, `ggml-org/llama.cpp` — F2, F3, J3
- Qualcomm AI Hub 문서, QNN SDK 문서 — F4
- Hugging Face 양자화 문서 (GPTQ, AWQ, bitsandbytes) — C3
- Arm Ethos-U / Vela 문서, Arm Helium 프로그래밍 가이드 — E2, E5
- ST·Bosch·TDK IMU 데이터시트와 앱노트 — G1, G2

---

## 6. 진행 체크 (모듈 노트 작성 현황)

**현재 위치 (2026-10-01)**: 모듈 **A–K 작성 완료** (80편 · 약 113,100줄 · SVG 584개 · 실행 검증된 예제 약 1,110개) → **다음은 모듈 L (온디바이스 LLM · Hybrid — SLM 선택, 온디바이스 스택, hybrid 라우팅, 음성 파이프라인, 메모리·RAG, 디코딩 가속)**. 읽기는 A0부터 순서대로.

**로컬 도구 추가 (모듈 J)**: `.tools/rustup` + `.tools/cargo` (Rust 1.99, Cortex-M4 target `thumbv7em-none-eabihf`) — 쓰려면 `export RUSTUP_HOME=$PWD/.tools/rustup CARGO_HOME=$PWD/.tools/cargo PATH=$PWD/.tools/cargo/bin:$PATH`

**로컬 도구 (모듈 F에서 준비, 모두 이 폴더 안 · gitignore)**: `.tools/llama.cpp` (빌드됨, Metal) · `.tools/tflite-micro` (호스트 빌드 확인) · `.tools/models/` GGUF 약 5.2 GB · `.venv-tf` (TensorFlow 2.20, TFLite 변환/실행) · `.venv`에 coremltools·cmake·gguf 추가

**모듈 A — 수학 · ML 기초** (2026-09-29, 7편 · 약 8,600줄 · SVG 34개)

| ID | 노트 | 분량 | 핵심 실습 결과 | 작성 | 읽음 |
|---|---|---|---|---|---|
| A0 | [머신러닝 한 장 지도 — 펌웨어 엔지니어를 위한 입문](../A/2026-09-29_A0_ml_map_for_firmware_engineers.html) | 917줄 · SVG 5 | 규칙 기반 0.667 → 학습 0.983 · C float 추론이 Python과 6자리 일치 | ✅ 09-29 | ⬜ |
| A1 | [ML을 위한 선형대수 — 벡터·행렬·텐서와 행렬곱의 비용](../A/2026-09-29_A1_linear_algebra_for_ml.html) | 1,321줄 · SVG 5 | 256×256 matmul: C ijk 15 ms → ikj 1.2 ms → numpy 0.03 ms | ✅ 09-29 | ⬜ |
| A2 | [ML을 위한 확률·통계 — 분포, softmax, cross-entropy](../A/2026-09-29_A2_probability_statistics.html) | 1,119줄 · SVG 4 | wake word 오경보 360회/시간 → 트리거가 진짜일 확률 ≈ 0.11% (Bayes) | ✅ 09-29 | ⬜ |
| A3 | [최적화와 역전파 — 경사하강법이 모델을 학습시키는 원리](../A/2026-09-29_A3_optimization_backprop.html) | 1,321줄 · SVG 5 | 손 역전파 = 수치 미분(차이 5e-12) = PyTorch autograd | ✅ 09-29 | ⬜ |
| A4 | [학습 워크플로와 모델 평가 — 데이터 분할부터 지표까지](../A/2026-09-29_A4_training_workflow_evaluation.html) | 1,065줄 · SVG 5 | 랜덤 윈도 분할 94.6% vs 사용자 분할 48.9% (leakage) | ✅ 09-29 | ⬜ |
| A5 | [PyTorch 기본 — 텐서부터 학습 루프, 펌웨어로 내보내기까지](../A/2026-09-29_A5_pytorch_basics.html) | 1,144줄 · SVG 5 | IMU 1D-CNN 학습 → C 헤더 export, C vs torch 오차 1.9e-6 | ✅ 09-29 | ⬜ |
| A6 | [데이터 도구 — numpy·pandas·matplotlib으로 센서 로그 다루기](../A/2026-09-29_A6_data_tools_numpy_pandas.html) | 1,699줄 · SVG 5 | C 바이너리 로그 → numpy structured dtype → pandas → [N,C,L] 윈도 | ✅ 09-29 | ⬜ |

**모듈 B — 모델 아키텍처** (2026-09-30, 9편 · 약 12,100줄 · SVG 55개)

| ID | 노트 | 분량 | 핵심 실습 결과 | 작성 | 읽음 |
|---|---|---|---|---|---|
| B1 | [MLP · 활성화 함수 · 정규화 — 모든 모델의 부품](../B/2026-09-29_B1_mlp_activation_normalization.html) | 1,225줄 · SVG 5 | Conv+BN folding 오차 7e-7 · int8 ReLU6/hard-swish/LUT sigmoid C 구현 | ✅ 09-30 | ⬜ |
| B2 | [CNN — 합성곱, depthwise separable, MobileNet, 1D conv/TCN](../B/2026-09-29_B2_cnn.html) | 1,387줄 · SVG 7 | C conv2d = torch · MobileNet v1 직접 구성 4.23M/569M MAC(논문 일치) · 링버퍼 스트리밍 conv 오차 0 | ✅ 09-30 | ⬜ |
| B3 | [RNN · LSTM · GRU — 상태를 가진 모델과 스트리밍 추론](../B/2026-09-29_B3_rnn_lstm_gru.html) | 1,152줄 · SVG 5 | C GRU = torch(1.2e-7) · toggle 과제 GRU 99.3% vs CNN 52% · int8 IIR deadband | ✅ 09-30 | ⬜ |
| B4 | [Attention과 Transformer — Q·K·V부터 GQA·RoPE·KV-cache까지](../B/2026-09-29_B4_attention_transformer.html) | 1,303줄 · SVG 7 | 손계산 = numpy = SDPA · RoPE 상대위치 수치 확인 · KV-cache 유무 출력 동일 | ✅ 09-30 | ⬜ |
| B5 | [오디오 · 음성 모델 — wake word, VAD, ASR, 화자 인식, TTS](../B/2026-09-29_B5_audio_speech_models.html) | 1,495줄 · SVG 6 | 합성 "Hey Hark" KWS 95.9% · CTC loss 손계산 = torch · Q15 DMA 핑퐁 C | ✅ 09-30 | ⬜ |
| B6 | [비전 모델 — 분류·검출·분할, ViT, CLIP, VLM](../B/2026-09-29_B6_vision_models.html) | 1,464줄 · SVG 5 | 해상도별 MAC 표 · NMS numpy = C · MobileNetV2 peak activation 1470/750/270 KB | ✅ 09-30 | ⬜ |
| B7 | [IMU · 센서 모델 — 활동 인식, 제스처, 착용 감지, 이상 탐지](../B/2026-09-29_B7_imu_sensor_models.html) | 1,663줄 · SVG 7 | 사용자 분할 0.83–0.93 vs 랜덤 0.996+ · 결정트리 → C 170 B, 540/540 일치 · hysteresis FSM | ✅ 09-30 | ⬜ |
| B8 | [소형 언어모델(SLM) — 구조, 토크나이저, 생성, 메모리 계산](../B/2026-09-29_B8_small_language_models.html) | 1,314줄 · SVG 6 | 실제 SmolLM2-135M: 파라미터 공식 9개 모델 일치 · decode 17.6 ms/token(M2 CPU) · 한국어 토큰 1.4–4.8배 | ✅ 09-30 | ⬜ |
| B9 | [효율 아키텍처 — MoE, SSM(Mamba), linear attention, early-exit, cascade](../B/2026-09-29_B9_efficient_architectures.html) | 1,144줄 · SVG 7 | linear attention 병렬=순환(4e-16) · SSM = IIR 필터 뱅크(C) · early-exit 62% MAC로 97.4% | ✅ 09-30 | ⬜ |

- [x] A. 수학 · ML 기초 (A0–A6) — 2026-09-29 작성 완료
**모듈 C — 모델 경량화** (2026-09-30, 8편 · 약 11,200줄 · SVG 53개)

| ID | 노트 | 분량 | 핵심 실습 결과 | 작성 | 읽음 |
|---|---|---|---|---|---|
| C1 | [양자화 이론 — scale·zero-point, 정밀도 포맷, requantization, calibration](../C/2026-09-30_C1_quantization_theory.html) | 1,501줄 · SVG 6 | int8 dense C 커널 = numpy 정수 시뮬 6400/6400 bit-exact · float 곱셈기면 10개 1 LSB 어긋남 | ✅ 09-30 | ⬜ |
| C2 | [PTQ와 QAT — 학습 후 양자화와 양자화 인식 학습 실전](../C/2026-09-30_C2_ptq_qat.html) | 1,361줄 · SVG 7 | torch.ao·ORT QDQ int8 무손실 · outlier 채널 per-tensor 0.346 → CLE 0.957 · W3 PTQ 0.851 → QAT 0.953 | ✅ 09-30 | ⬜ |
| C3 | [LLM 양자화 — W4A16, W8A8, SmoothQuant, GPTQ, AWQ, KV-cache](../C/2026-09-30_C3_llm_quantization.html) | 1,565줄 · SVG 9 | 실제 SmolLM2: int4 g32 PPL 21.3(FP32 18.1) · W8A8 35.6 → SmoothQuant 19.4 · AWQ 33.1 → 23.6 · int8 KV 40토큰 일치 | ✅ 09-30 | ⬜ |
| C4 | [Pruning과 Sparsity — 0을 만들면 정말 빨라지나](../C/2026-09-30_C4_pruning_sparsity.html) | 1,336줄 · SVG 6 | 90% sparse CSR이 NEON dense보다 2배 느림 · 채널 50% 제거 → 2.9배 빠름 · 90% prune + int8 → 338 KB → 15 KB | ✅ 09-30 | ⬜ |
| C5 | [Knowledge Distillation — 큰 모델의 지식을 작은 모델로](../C/2026-09-30_C5_knowledge_distillation.html) | 1,279줄 · SVG 6 | 라벨 300개: CE 77.9% → KD+비라벨 83.4% · SmolLM2 int4 KL 0.333 → 0.235 | ✅ 09-30 | ⬜ |
| C6 | [그래프 최적화 — 컴파일러가 모델을 기기에 맞게 바꾸는 방법](../C/2026-09-30_C6_graph_optimization.html) | 1,461줄 · SVG 7 | ORT 최적화 9 → 4 노드 · bias 없는 export가 fusion을 막는 실제 사례 · arena 405 → 252 KB · 실행 순서로 peak 96–192 KB | ✅ 09-30 | ⬜ |
| C7 | [HW-aware 모델 설계 — NAS, MCUNet, Once-for-All, latency LUT](../C/2026-09-30_C7_hw_aware_model_design.html) | 1,195줄 · SVG 6 | 같은 MAC인데 5–515 GMAC/s · LUT 예측 MAPE 0.2% vs MAC 비례 20.9% · patch 추론 peak 304 → 124 KB | ✅ 09-30 | ⬜ |
| C8 | [최적화 후 검증 — golden reference, 레이어별 비교, 회귀 테스트](../C/2026-09-30_C8_verification_after_optimization.html) | 1,518줄 · SVG 6 | 버그 3종을 레이어별 SQNR로 위치 특정 · golden vector C 하네스 bit-exact · bootstrap 회귀 판정 · HIL 시뮬 | ✅ 09-30 | ⬜ |

- [x] B. 모델 아키텍처 (B1–B9) — 2026-09-30 작성 완료
**모듈 D — 성능 모델** (2026-09-30, 7편 · 약 9,400줄 · SVG 51개)

| ID | 노트 | 분량 | 핵심 실습 결과 | 작성 | 읽음 |
|---|---|---|---|---|---|
| D1 | [FLOPs · MACs 계산 — 레이어별 공식과 모델 전체 손계산 드릴](../D/2026-09-30_D1_flops_macs_counting.html) | 1,475줄 · SVG 5 | torchvision "GFLOPS"는 실제로 GMAC · FlopCounterMode가 CPU SDPA를 0으로 셈 · 실제 SmolLM2에서 2·N 규칙 T=8 정확, T=256에서 6.6% 부족 | ✅ 09-30 | ⬜ |
| D2 | [메모리 계산 — 가중치·활성값·peak·arena·flash/SRAM 배치](../D/2026-09-30_D2_memory_calculation.html) | 1,378줄 · SVG 7 | torch.fx 추정기: KWS arena 15.6 KB · flash 28.4 KB · MobileNetV2-0.35 135 KB · C 스택 painting high-water 측정 | ✅ 09-30 | ⬜ |
| D3 | [Roofline과 Arithmetic Intensity — compute-bound vs memory-bound](../D/2026-09-30_D3_roofline_arithmetic_intensity.html) | 1,423줄 · SVG 7 | 이 Mac 실측 roofline: DRAM 60–77 GB/s · matmul ≈1.1 TFLOP/s · ridge 17.6 FLOP/B · int8 GEMV 4.2배(바이트 비와 일치) | ✅ 09-30 | ⬜ |
| D4 | [모델 계열별 성능 특성 — CNN, depthwise, RNN, Transformer](../D/2026-09-30_D4_performance_by_model_family.html) | 1,370줄 · SVG 8 | depthwise 10–12 vs 표준 conv 270–470 GMAC/s · MobileNetV2 MAC 6배 적은데 1.4–2.1배만 빠름 · 스트리밍 상태 GRU 128 B / TCN 1.8 KB / transformer 25 KB | ✅ 09-30 | ⬜ |
| D5 | [LLM 추론 성능 — prefill vs decode, KV-cache, 대역폭, TTFT/TPOT](../D/2026-09-30_D5_llm_inference_performance.html) | 1,279줄 · SVG 9 | 해석 모델 vs 실측 비교 · HF GQA SDPA가 CPU에서 비융합 경로(attention 1865 → 413 ms) · prefix caching TTFT 131 → 39 ms | ✅ 09-30 | ⬜ |
| D6 | [Latency · Throughput · Real-time — 평균이 아니라 꼬리를 본다](../D/2026-09-30_D6_latency_throughput_realtime.html) | 1,223줄 · SVG 7 | 평균 5.1 ms인데 10 ms 예산 0.19% miss · KWS 검출 지연 286 ms 중 270 ms가 알고리즘 지연 · batch 16에서 NNPACK 절벽 | ✅ 09-30 | ⬜ |
| D7 | [에너지 모델 — 데이터 이동이 전력을 먹는다](../D/2026-09-30_D7_energy_model.html) | 1,297줄 · SVG 8 | 1B int4 decode 1토큰 31.8 mJ 중 MAC 0.9% · DRAM 73% · "10 TOPS/W" 스펙 → 실제 11배 · 열 RC 모델 지속 267 mW | ✅ 09-30 | ⬜ |

- [x] C. 모델 경량화 (C1–C8) — 2026-09-30 작성 완료
**모듈 E — HW 아키텍처** (2026-09-30, 9편 · 약 12,500줄 · SVG 74개)

| ID | 노트 | 분량 | 핵심 실습 결과 | 작성 | 읽음 |
|---|---|---|---|---|---|
| E1 | [CPU 마이크로아키텍처 — 파이프라인, OoO, 캐시, 분기 예측](../E/2026-09-30_E1_cpu_microarchitecture.html) | 1,377줄 · SVG 7 | FMA 체인 1→16개: 0.25→3.87 FMA/cycle · 캐시 계단 L1 0.9 ns / L2 5.8 ns / DRAM ~110 ns · line 128 B 실측 · Cortex-M4 asm 읽기 | ✅ 09-30 | ⬜ |
| E2 | [ARM Cortex-M / Cortex-A와 SIMD — M4F·M55 Helium, NEON·SDOT·i8mm](../E/2026-09-30_E2_arm_cortex_m_a_simd.html) | 1,760줄 · SVG 7 | int8 GEMV scalar 3.1 → SDOT 71 GMAC/s · GEMM SDOT/SMMLA ~199 GMAC/s (결과 모두 bit 일치) · M4 SMLAD / M55 Helium asm 실제 생성 | ✅ 09-30 | ⬜ |
| E3 | [RISC-V — 오픈 ISA, 확장, RVV 벡터, 커스텀 명령](../E/2026-09-30_E3_risc_v.html) | 1,280줄 · SVG 6 | RV32I 인코더/디코더(기계어 일치) · vsetvli strip-mining 시뮬(VLEN 128: 7회 / 256: 4회) · 커스텀 dot4.i8 golden model | ✅ 09-30 | ⬜ |
| E4 | [DSP 아키텍처 — VLIW, MAC, 고정소수점, Xtensa HiFi, Hexagon](../E/2026-09-30_E4_dsp_architecture.html) | 1,250줄 · SVG 8 | Q15 누산 int32 wrap vs 40-bit guard · FIR Q15 SNR 92.7 dB · 순환 버퍼 % 75 ns vs mirror 1.5 ns · modulo scheduling II 4→1.03 | ✅ 09-30 | ⬜ |
| E5 | [NPU 아키텍처 — MAC 배열, systolic array, dataflow, tiling, 컴파일러](../E/2026-09-30_E5_npu_architecture.html) | 1,359줄 · SVG 9 | systolic 시뮬 = numpy · 16×16 활용률 1×1 conv 94.5% vs depthwise 2.8% · dataflow별 버퍼 접근 · SRAM별 DRAM 트래픽 · bring-up 체크리스트 | ✅ 09-30 | ⬜ |
| E6 | [GPU — SIMT, warp, occupancy, 모바일 GPU, batch=1의 한계](../E/2026-09-30_E6_gpu.html) | 1,301줄 · SVG 12 | M2 GPU(MPS) 실측: 작은 op 왕복 146 µs · KWS MLP CPU 30 µs vs GPU 508 µs · matmul N≈1024부터 GPU 우세 | ✅ 09-30 | ⬜ |
| E7 | [메모리 시스템 — SRAM·TCM·캐시·LPDDR, 대역폭, DMA, coherence](../E/2026-09-30_E7_memory_system.html) | 1,471줄 · SVG 9 | LPDDR 대역폭 계산기 · 랜덤 8 B 0.87 vs 순차 45.8 GB/s · DMA+write-back 캐시 stale 버그 시뮬 · MESI 시뮬 · SSD SI/shmoo ↔ LPDDR training | ✅ 09-30 | ⬜ |
| E8 | [이기종 SoC 구조 — always-on island, 센서 허브, IPC](../E/2026-09-30_E8_heterogeneous_soc.html) | 1,411줄 · SVG 8 | wake cascade 시뮬 8.1 mW, DSP 단계 없으면 33 mW · COBS+CRC 프레이밍 · SPSC+doorbell 왕복 실측 · 코어 간 시계 보정 2380 → 0.4 µs | ✅ 09-30 | ⬜ |
| E9 | [전력·열 하드웨어 — power gating, DVFS, 저전력 모드, PMIC, 열 관리](../E/2026-09-30_E9_power_thermal_hardware.html) | 1,252줄 · SVG 8 | leakage–온도 runaway · break-even 유휴 시간 계산기 · DVFS governor 시뮬 · IMU-wake µA 예산 · PI 열 제어 | ✅ 09-30 | ⬜ |

- [x] D. 성능 모델 (D1–D7) — 2026-09-30 작성 완료
**모듈 F — 런타임 · 툴체인** (2026-09-30, 8편 · 약 11,700줄 · SVG 56개)

| ID | 노트 | 분량 | 핵심 실습 결과 | 작성 | 읽음 |
|---|---|---|---|---|---|
| F1 | [TFLite / LiteRT — 변환기, flatbuffer, interpreter, delegate, int8](../F/2026-09-30_F1_tflite_litert.html) | 1,467줄 · SVG 7 | PTQ 5종 비교 · int8 conv를 정수로 재구성해 480/480 일치 · Conv1D가 XNNPACK 3분할 · int8 3.4배 빠름 | ✅ 09-30 | ⬜ |
| F2 | [TensorFlow Lite Micro — arena, op resolver, CMSIS-NN](../F/2026-09-30_F2_tflite_micro.html) | 1,440줄 · SVG 7 | Mac에서 TFLM 실제 빌드 · mini DS-CNN KWS가 reference/CMSIS-NN 모두 bit-exact · arena 8,464 B (추정 6,000 B = head와 일치) · 실제 에러 메시지 | ✅ 09-30 | ⬜ |
| F3 | [llama.cpp / GGML — GGUF 변환, 양자화, 백엔드, 벤치마크](../F/2026-09-30_F3_llama_cpp_ggml.html) | 1,515줄 · SVG 6 | 3개 모델 × 6 양자화 실측 · d=576/896은 k-quant 불가 → Q5_0 fallback 발견 · CPU tg ≈ 60–63 GB/s(D5와 일치) · 양자화 V cache는 flash attention 필요 | ✅ 09-30 | ⬜ |
| F4 | [Qualcomm 스택 — QNN, HTP, SNPE, AI Hub, Genie](../F/2026-09-30_F4_qualcomm_qnn_ai_hub.html) | 1,408줄 · SVG 7 | QNN 넘길 ONNX QDQ 산출물 실제 생성 · 16-bit activation + 범위 override 42.4 vs 8-bit 36.3 dB · 없는 EP 요청 → 조용히 CPU · SDK 명령은 미실행 표기 | ✅ 09-30 | ⬜ |
| F5 | [ONNX와 ONNX Runtime — 교환 포맷, opset, export, EP](../F/2026-09-30_F5_onnx_onnxruntime.html) | 1,481줄 · SVG 7 | dynamo export가 batch를 조용히 1로 고정 · 데이터 의존 if가 구워짐 · version_converter 3/3 실패 · CoreML EP 분할로 오히려 느려짐 | ✅ 09-30 | ⬜ |
| F6 | [그 밖의 런타임과 ML 컴파일러 — Core ML, ExecuTorch, TVM·MLIR·IREE](../F/2026-09-30_F6_other_runtimes_compilers.html) | 1,256줄 · SVG 8 | Core ML fp16 ANE 0.54 ms vs CPU 5.35 ms · MLComputePlan으로 ANE 사용 증명 · palettization 6.66 → 0.89 MB, 4-bit까지 정확도 유지 | ✅ 09-30 | ⬜ |
| F7 | [크로스 빌드와 모델 배치 — 툴체인, CMake, 링커 스크립트](../F/2026-09-30_F7_cross_build_model_placement.html) | 1,571줄 · SVG 6 | Apple ar가 ELF를 조용히 빈 .a로 · xxd -i 배열이 .data로 · 커스텀 zero-init 섹션이 공간 차지 · TFLM 16B 정렬 소스 확인 | ✅ 09-30 | ⬜ |
| F8 | [벤더 SDK·툴체인·새 가속기 bring-up](../F/2026-09-30_F8_vendor_sdk_bringup.html) | 1,539줄 · SVG 8 | op 커버리지 스윕 · fallback graph surgery · mismatch 서명 분류기 · SDK 회귀 게이트 · 실제 CoreML 백엔드 probe · STAR 답변 골격 | ✅ 09-30 | ⬜ |

- [x] E. HW 아키텍처 (E1–E9) — 2026-09-30 작성 완료
**모듈 G — 센서 · 신호처리** (2026-09-30, 7편 · 약 10,600줄 · SVG 59개)

| ID | 노트 | 분량 | 핵심 실습 결과 | 작성 | 읽음 |
|---|---|---|---|---|---|
| G1 | [IMU 원리 — MEMS 가속도계·자이로·지자기, 데이터시트 읽는 법](../G/2026-09-30_G1_imu_principles.html) | 1,322줄 · SVG 9 | noise density → RMS 손계산 · Allan deviation으로 ARW·bias instability 읽기 · 이중 적분 drift ∝ t² · ±4 g vs ±16 g 클리핑 · 자이로 = AM 변조/동기 복조(RF 연결) | ✅ 09-30 | ⬜ |
| G2 | [IMU 인터페이스와 드라이버 — SPI/I2C, FIFO + watermark, 내장 기능](../G/2026-09-30_G2_imu_interface_driver.html) | 1,519줄 · SVG 6 | 가상 IMU "FIMU-6" 모델 + 드라이버(C) · SPI mode 오류 재현(WHO_AM_I 0x53) · 3가지 데이터 경로 wake/전력 비교 · 깨진 FIFO 프레임 복구 파서 | ✅ 09-30 | ⬜ |
| G3 | [IMU 보정과 센서 퓨전 — quaternion, complementary·Mahony·Madgwick·Kalman](../G/2026-09-30_G3_imu_calibration_fusion.html) | 1,803줄 · SVG 9 | 6-position 보정 복원 · 타원체 피팅 heading 73.5° → 2.0° · tilt RMS: gyro 18.6° → complementary 0.78° → ESKF 0.06° · handedness 버그 63° · C Mahony = numpy | ✅ 09-30 | ⬜ |
| G4 | [마이크와 오디오 HW — MEMS 마이크, PDM, CIC, beamforming, AEC](../G/2026-09-30_G4_microphone_audio_hw.html) | 1,459줄 · SVG 13 | ΣΔ → PDM → CIC → 16 kHz PCM SNR 68.7 dB · CIC 27-bit에서 bit-exact, 26-bit 실패 · GCC-PHAT · 1.5 cm 배열 한계 · NLMS AEC double-talk 붕괴 | ✅ 09-30 | ⬜ |
| G5 | [DSP 기초 — aliasing, FIR/IIR, window, FFT/STFT, mel·MFCC, 고정소수점](../G/2026-09-30_G5_dsp_fundamentals.html) | 1,889줄 · SVG 8 | C FIR/biquad/FFT = scipy/numpy · Q15 FIR 87 dB · float32 8차 IIR 불안정 · Q15 limit cycle · 스트리밍 C log-mel = numpy 배치(2.9e-5) | ✅ 09-30 | ⬜ |
| G6 | [그 밖의 센서 — 카메라·ISP, PPG, ToF, 기압계, 온도, 터치, 착용 감지](../G/2026-09-30_G6_other_sensors.html) | 1,289줄 · SVG 7 | 토이 ISP · 걷는 중 PPG FFT 60 bpm(팔 흔들기) → NLMS 후 100 bpm · 착용 감지 퓨전: 근접 단독 89회 전이 → 8분 오차 | ✅ 09-30 | ⬜ |
| G7 | [시간 동기화 — timestamp, 클럭 drift, 다중 센서 정렬, resampling](../G/2026-09-30_G7_time_synchronization.html) | 1,300줄 · SVG 7 | 라벨 200 ms 밀림 → recall 97% → 0.3% · ODR 1.8% 오차: 공칭 1,242 ms vs 회귀 8 µs · tap 상호상관 0.6 ms 정렬 · time QA 리포트 | ✅ 09-30 | ⬜ |

- [x] F. 런타임 · 툴체인 (F1–F8) — 2026-09-30 작성 완료
**모듈 H — 데이터 수집 · Ingestion 파이프라인** (2026-09-30, 8편 · 약 10,600줄 · SVG 62개)

| ID | 노트 | 분량 | 핵심 실습 결과 | 작성 | 읽음 |
|---|---|---|---|---|---|
| H1 | [온디바이스 로깅 — 링버퍼에서 flash까지, 포맷, 압축, wear](../H/2026-09-30_H1_on_device_logging.html) | 1,368줄 · SVG 9 | 모드별 하루 용량 2,878 → 11 MB · C writer + Python reader(잘림·비트 플립 복구) · 전원 차단 2,000회 전부 정확 복구 · flush 250 ms WA 13.65 → 5–30 s ≈1 · 직렬화 7종 비교 | ✅ 09-30 | ⬜ |
| H2 | [기기 → 서버 전송 — BLE throughput, 배치 업로드, 재개, 무결성](../H/2026-09-30_H2_data_transport.html) | 1,376줄 · SVG 9 | BLE 2M/DLE 계산(교환 1,392 µs) · 루프백 재개형 업로드 데모(ACK 유실·손상·재부팅) · jitter backoff · ARQ 시뮬 · 업로드 스케줄러 | ✅ 09-30 | ⬜ |
| H3 | [백엔드 ingestion — object storage, 큐, 스키마, Parquet, 데이터 버전](../H/2026-09-30_H3_backend_ingestion.html) | 1,352줄 · SVG 7 | 로컬 미니 파이프라인: 1,000 업로드 → 947 OK / DLQ 56 · CSV 183 MB vs Parquet zstd 17 MB · pushdown 0.19/17.3 MB · fw 문자열 비교 함정 · parser 버그 재처리 | ✅ 09-30 | ⬜ |
| H4 | [라벨링 — 수동·ground truth·weak labeling·active learning](../H/2026-09-30_H4_labeling.html) | 1,391줄 · SVG 7 | interval → window 규칙 비교 · Cohen's kappa · 라벨 노이즈 영향 · labeling function 투표 · pseudo-label 임계값 · active learning vs random · wake word hard negative | ✅ 09-30 | ⬜ |
| H5 | [데이터셋 관리 — 분할 정책, 불균형, 버전 관리, dataset card](../H/2026-09-30_H5_dataset_management.html) | 1,306줄 · SVG 7 | 해시 기반 split: 사용자 추가 시 0명 이동 (셔플 47명) · focal(α 없이) 효과 없음 vs weighted/α/logit 조정 ~0.8 · 데이터셋 레지스트리 diff · dataset card 예시 | ✅ 09-30 | ⬜ |
| H6 | [프라이버시와 동의 — always-listening 기기 설계](../H/2026-09-30_H6_privacy_consent.html) | 1,283줄 · SVG 8 | 평문 해시 ID 0.3 s 역산 vs HMAC · consent ledger 필터 · keyword pre-roll 60 s 중 7 s만 저장 · Laplace 오차 = 1/ε · TTL/tombstone 검증 · (법률 자문 아님 명시) | ✅ 09-30 | ⬜ |
| H7 | [데이터 품질 모니터링 — 센서 이상·누락·drift 자동 탐지](../H/2026-09-30_H7_data_quality_monitoring.html) | 1,319줄 · SVG 8 | 결함 9종 주입 QA 전부 검출 · FW gain 버그: fleet 중앙값은 안 움직이고 rev B 그룹에서 당일 탐지 · PSI/KS 임계 trade-off · CUSUM vs Shewhart ARL | ✅ 09-30 | ⬜ |
| H8 | [Fleet 규모 데이터 수집 — 원격 설정, 샘플링 정책, 버전 태깅, dogfood](../H/2026-09-30_H8_fleet_scale_collection.html) | 1,206줄 · SVG 7 | config 검증·서명·capability 협상 · 결정적 cohort · 500대×14일 샘플링 정책 비교 · shadow mode · 비용 계산기 · Poisson 수집 기간 | ✅ 09-30 | ⬜ |

- [x] G. 센서 · 신호처리 (G1–G7) — 2026-09-30 작성 완료
**모듈 I — HW/SW Co-design** (2026-10-01, 6편 · 약 8,500줄 · SVG 51개) — 공통 사례: 음성+제스처 웨어러블(300 mAh, M55+micro-NPU MCU, 오디오 DSP, ~10 TOPS SoC NPU — 모두 가정)

| ID | 노트 | 분량 | 핵심 실습 결과 | 작성 | 읽음 |
|---|---|---|---|---|---|
| I1 | [예산 설정 — 요구사항을 숫자로, 그리고 모델 제약으로](../I/2026-10-01_I1_budget_setting.html) | 1,353줄 · SVG 10 | 300 mAh → 793 mWh/일 = 평균 33 mW → KWS ≤ 8 M MAC, SLM ≤ ~1B int4 · TTFA 1 s → TTFT 120 ms → int4 ≤ 0.42B · SRAM 맵(OTA A/B 포함) · Monte Carlo 마진 · `_Static_assert` 예산 | ✅ 10-01 | ⬜ |
| I2 | [HW 친화 모델 설계 — 모델팀에 주는 규칙과 근거](../I/2026-10-01_I2_hw_friendly_model_design.html) | 1,551줄 · SVG 10 | 규칙 16개 + ONNX HW 친화도 linter · naive → fixed: peak 919 → 184 KB, ORT 5.96 → 0.82 ms, CoreML 조각 38 → 1, int8 정확도 0.197 → 0.860 | ✅ 10-01 | ⬜ |
| I3 | [연산 분할과 Cascade — 단계·임계값·오프로드 공동 최적화](../I/2026-10-01_I3_partitioning_cascade.html) | 1,294줄 · SVG 8 | 상관 ρ=0.5면 결합 FPR이 독립 가정의 13배 · 공동 최적화 1.18 mW / miss 5% (20M MC 검증) · 분할점은 양 끝 · 슈퍼루프 KWS 37프레임 유실 | ✅ 10-01 | ⬜ |
| I4 | [전처리·후처리 배치 — 특징 추출은 어디서, 데이터 이동은 최소로](../I/2026-10-01_I4_pre_post_processing_placement.html) | 1,328줄 · SVG 6 | 배치 729가지 최적화(시나리오마다 승자 다름) · 정규화 folding · conv-STFT = torch.stft · log-mel 전단을 ONNX(Conv·Pow·Log)로 | ✅ 10-01 | ⬜ |
| I5 | [벤치마크와 지표 — MLPerf, TOPS 함정, 우리 제품용 벤치마크](../I/2026-10-01_I5_benchmarks_metrics.html) | 1,520줄 · SVG 9 | 4모델 × 6백엔드 하네스 + llama-bench · 60 s sustained · Pareto·가중치 민감도 · 코드 변경 없이 A/A 3/24 "회귀" | ✅ 10-01 | ⬜ |
| I6 | [Co-design 피드백 루프 — 프로파일 → 병목 → 변경 → 재측정](../I/2026-10-01_I6_codesign_feedback_loop.html) | 1,462줄 · SVG 8 | v0 → v4: latency 498 → 2.55 ms, peak 1037 → 14.4 KB, int8 정확도 52.8 → 99.1% · LayerNorm/GELU가 원인임을 2×2 A/B로 확인 · CI 예산 리포트 | ✅ 10-01 | ⬜ |

- [x] H. 데이터 파이프라인 (H1–H8) — 2026-09-30 작성 완료
**모듈 J — 펌웨어 통합** (2026-10-01, 6편 · 약 10,500줄 · SVG 51개)

| ID | 노트 | 분량 | 핵심 실습 결과 | 작성 | 읽음 |
|---|---|---|---|---|---|
| J1 | [추론 통합 패턴 — 센서에서 결과까지 펌웨어 구조, 버퍼 소유권, 생명주기](../J/2026-10-01_J1_inference_integration_patterns.html) | 1,712줄 · SVG 8 | pthread 참조 파이프라인(ISR→ring→feature→inference→decision) · 60 ms 추론: fifo는 이벤트 유실·759 ms, latest는 4/4 · 400 ms 정지: ring 16 유실 vs 64 지연 · NPU 비동기 상태기계(generation tag) · arena 공유 | ✅ 10-01 | ⬜ |
| J2 | [실시간 설계 — WCET, 응답 시간 분석, 모델 분할 실행, 다중 rate](../J/2026-10-01_J2_realtime_design.html) | 1,460줄 · SVG 8 | U 0.769(LL 초과)인데 RTA 통과, 시뮬레이터와 일치 · mutex를 추론 내내 잡으면 3개 태스크 miss · 4 chunk 분할로 feature miss 200/400 → 0 · offset으로 응답 12.5 → 8.0 ms · cyclic executive | ✅ 10-01 | ⬜ |
| J3 | [고정소수점·SIMD 커널 — int8 conv/depthwise/pool/add/softmax](../J/2026-10-01_J3_fixed_point_simd_kernels.html) | 1,820줄 · SVG 11 | **TFLite 참조 커널과 bit-exact** (랜덤 모델 1,000개 · 187,546 출력 0 불일치) · bias folding + padding 버그 172/288 · mutation 테스트 · 9.5 → 120 GMAC/s · M4 SMLAD·M55 Helium asm | ✅ 10-01 | ⬜ |
| J4 | [임베디드 C++와 Rust — 예외/RTTI 없는 C++, constexpr, ownership, no_std, FFI](../J/2026-10-01_J4_embedded_cpp_rust.html) | 1,981줄 · SVG 7 | -fno-exceptions/-fno-rtti로 451 → 250 B · static init 순서 버그 재현 · constexpr LUT = numpy · Rust 실제 컴파일 에러 · Rust int8 커널 = C 8,800/8,800 · Cortex-M4 no_std 바이너리 3.7 KB | ✅ 10-01 | ⬜ |
| J5 | [모델 업데이트와 OTA — 패키징, 호환성, A/B, 서명, 롤백, 단계 배포](../J/2026-10-01_J5_model_update_ota.html) | 1,415줄 · SVG 9 | 검증기 8개 중 7개 정확히 거부 · 전원 차단 281회 brick 0 (단일 슬롯 85/85 손실) · 모델-임계값 불일치 FA 187배 · HW rev별 감시로 1% 단계에서 정지 | ✅ 10-01 | ⬜ |
| J6 | [테스트 — 테스트 피라미드, 재생 테스트, HIL, CI, 공장 테스트](../J/2026-10-01_J6_testing_hil_ci.html) | 2,140줄 · SVG 8 | C 단위 테스트 + llvm-cov · property 테스트 shrink · ASan이 off-by-one 즉시 검출 · 재생 게이트로 v1 차단 · HIL 러너 JUnit XML · 공장 limit·Cpk · (libFuzzer는 Apple clang에서 불가) | ✅ 10-01 | ⬜ |

- [x] I. HW/SW Co-design (I1–I6) — 2026-10-01 작성 완료
**모듈 K — 프로파일링** (2026-10-01, 5편 · 약 7,400줄 · SVG 38개)

| ID | 노트 | 분량 | 핵심 실습 결과 | 작성 | 읽음 |
|---|---|---|---|---|---|
| K1 | [메모리 프로파일링 — 어디에 몇 바이트, 회귀 막기](../K/2026-10-01_K1_memory_profiling.html) | 1,547줄 · SVG 8 | rust-lld로 Cortex-M4 펌웨어 실제 링크 → map 파서(llvm-size와 바이트 일치) · size-diff · `.su`+호출 그래프 최악 스택 · 단편화로 8 KB 요청 30% 실패 · 0.3 MB 모델이 ORT footprint 227 MB · CI 메모리 게이트 | ✅ 10-01 | ⬜ |
| K2 | [성능 프로파일링 — sampling vs instrumentation, flame graph, trace, PMU](../K/2026-10-01_K2_performance_profiling.html) | 1,641줄 · SVG 6 | macOS `sample` → 직접 만든 flame graph · tail call이 프레임을 숨김 · 계측이 인라인 함수에 훅 → 90%로 왜곡 · 컴파일러가 벤치를 루프 밖으로 · proc_pid_rusage로 IPC · llama-bench 스레드 동기화 | ✅ 10-01 | ⬜ |
| K3 | [전력 측정 — 계측기 물리, 셋업, 이벤트 귀속, 불확도, 회귀 리그](../K/2026-10-01_K3_power_measurement.html) | 1,300줄 · SVG 8 | 1 kS/s 점 샘플링 −0.9~+34% 오차 · range 전환 지연 100 µs → brown-out · 이벤트 정렬 평균 ± CI · paired 설계로 3.5% 회귀 검출(비짝 검정은 놓침) | ✅ 10-01 | ⬜ |
| K4 | [열과 지속 성능 — 장시간 측정, throttling, 열 모델 피팅](../K/2026-10-01_K4_thermal_sustained_performance.html) | 1,199줄 · SVG 9 | 이 Mac 35분 실측: 17 s 후 클럭 계단식 하락 → 지속 0.82× · 동시 작업 부하 영향이 더 큼(정직하게 분리) · 배터리 온도 1차 모델 피팅 · race vs spread · LLM 속도 pacing | ✅ 10-01 | ⬜ |
| K5 | [커널 최적화 기법 — 루프 변환, 데이터 레이아웃, Winograd·FFT, DMA 겹치기](../K/2026-10-01_K5_kernel_optimization_techniques.html) | 1,709줄 · SVG 7 | interchange 33× · tap 상수화 10.3× · AoS→SoA ~7× · Winograd 1.6–2.1× · `restrict` M4 명령 54.5 → 29.75 · DMA 3중 버퍼 시뮬(손계산 일치) | ✅ 10-01 | ⬜ |

- [x] J. 펌웨어 통합 (J1–J6) — 2026-10-01 작성 완료
- [x] K. 프로파일링 (K1–K5) — 2026-10-01 작성 완료
- [ ] L. 온디바이스 LLM · Hybrid (L1–L6)
- [ ] M. 실리콘 선정 (M1–M4)
- [ ] N. 도메인 (N1–N4)
- [ ] O. 툴 (O1–O3)

**P0만 먼저 하면** (tech session 대비 최소 세트): A1 A3 A4 A5 · B1 B2 B4 B5 B7 B8 · C1 C2 C3 C6 C8 · D1–D7 · E2 E4 E5 E7 E8 E9 · F1–F4 F7 F8 · G1 G2 G4 G5 G7 · H1 · I1 I2 I3 · J1 J2 J3 · K1 K2 K3 · L1–L4 · M1 · 프로젝트 PJ1–PJ4
