# Embedded AI Engineer — Study Prep List (나침반)

> **작성일**: 2026-09-27 · **대상 JD**: Hark Embedded AI Engineer (id 4392090009)
> **용도**: 이 직군에서 일하려면 알아야 할 개념을 **빠짐없이 나열한 목차**. 각 항목을 하나씩 노트(쉬운 설명 · 차트 · 샘플 코드)로 채워 간다
> **컨텍스트**: [포지션 컨텍스트](../../job_interview_prep_research/hark_ai_embedded_ai/hark_ai_embedded_ai_context.html) · [BSP 자리 노트(재사용 가능)](../../job_interview_prep_research/hark_ai_embedded_swe/2026-09-19_hark_study_notes/site/index.html)

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

- [ ] A. 수학 · ML 기초 (A1–A6)
- [ ] B. 모델 아키텍처 (B1–B9)
- [ ] C. 모델 경량화 (C1–C8)
- [ ] D. 성능 모델 (D1–D7)
- [ ] E. HW 아키텍처 (E1–E9)
- [ ] F. 런타임 · 툴체인 (F1–F8)
- [ ] G. 센서 · 신호처리 (G1–G7)
- [ ] H. 데이터 파이프라인 (H1–H8)
- [ ] I. HW/SW Co-design (I1–I6)
- [ ] J. 펌웨어 통합 (J1–J6)
- [ ] K. 프로파일링 (K1–K5)
- [ ] L. 온디바이스 LLM · Hybrid (L1–L6)
- [ ] M. 실리콘 선정 (M1–M4)
- [ ] N. 도메인 (N1–N4)
- [ ] O. 툴 (O1–O3)

**P0만 먼저 하면** (tech session 대비 최소 세트): A1 A3 A4 A5 · B1 B2 B4 B5 B7 B8 · C1 C2 C3 C6 C8 · D1–D7 · E2 E4 E5 E7 E8 E9 · F1–F4 F7 F8 · G1 G2 G4 G5 G7 · H1 · I1 I2 I3 · J1 J2 J3 · K1 K2 K3 · L1–L4 · M1 · 프로젝트 PJ1–PJ4
