# Hark — Embedded AI Engineer · Context

> **최종 갱신**: 2026-09-27 · **상태**: 🔍 조사중
> **JD**: https://job-boards.greenhouse.io/hark/jobs/4392090009 · **위치**: San Jose, CA / onsite 추정 · **연봉 밴드**: $200,000 – $450,000 base (+ equity 가능)
> **사용 레쥬메**: `Resume_Firmware_Engineer_2026_Sep_DonHong.pdf`
> **관련 포지션**: 같은 회사 [Embedded SWE, BSP](../hark_ai_embedded_swe/hark_ai_embedded_swe_context.md) (📞 리크루터 진행 중, 1순위) · 자리 비교는 [P03 임베디드 7자리 지도](../hark_ai_embedded_swe/2026-09-19_hark_study_notes/site/plan/P03_embedded_role_map.html)
> **🧭 스터디 나침반**: [study_prep_list — 이 직군 공부 목록 (15모듈, 약 100개념)](../../dons_study_note_from_experience/embeddedAIPrepBasedOnHarkAIJD/study_prep_list.html) — 2026-09-28 `dons_study_note_from_experience/embeddedAIPrepBasedOnHarkAIJD/`로 이동
> 신뢰도: `[확인됨]` 공식/복수 출처 · `[추정]` 단일·2차 출처 또는 추론. 리크루터 공식 안내가 항상 우선.

<!-- 상태 값: 🔍 조사중 → 📨 지원완료 → 📞 리크루터 → 💻 폰스크린 → 🏢 온사이트 → 🎉 오퍼 | ❌ 불합격 | ⏸ 보류 -->

---

## 0. TL;DR

- **회사**: Figure AI 창업자 Brett Adcock가 세운 "personal intelligence" AI 랩 + 컨슈머 하드웨어 회사. 자체 multimodal 모델(speech·vision·memory)과 전용 기기를 같이 만든다. Series A $700M, post-money $6B (2026-05) [확인됨][1][2]. 첫 제품은 웹 에이전트 Handoff(2026-08), 하드웨어는 미공개 [확인됨][4]
- **역할**: Hark 기기 위의 **AI 스택 전체**를 맡는다. 센서 데이터 수집 파이프라인 → 모델팀과 모델 구조 co-design → 벤더 툴체인/SDK/**새 가속기 bring-up** → C/C++/Rust 펌웨어에 추론 통합 → 메모리·전력·실시간 성능 프로파일링 → **실리콘 플랫폼(GPU/NPU) 선정** [확인됨][JD]
- **적합도**: ⭐⭐ (2/5). 역할의 절반인 **임베디드 시스템·가속기 bring-up·성능/전력 프로파일링**은 Don의 본진이다. 하지만 필수 요건 1번이 **"5 years of ML engineering, 2+ years edge ML"**이고, TFLite/llama.cpp/QNN·모델 최적화·IMU/마이크 실무가 전부 없다. 필수 7개 중 ✅ 1 · 🟡 2 · ❌ 4
- **커리어 방향**: ⭐⭐⭐⭐⭐ (5/5). "on-device AI + NPU 선정·bring-up + 모델-하드웨어 co-design"은 Don의 피벗 목표(AI accel·on-device·physical AI)에 정확히 맞는다. **목표 자리이지 지금 붙을 자리는 아니다**
- **핵심 어필**: ① **새 실리콘/IP를 벤더와 함께 bring-up**(FPGA pre-silicon Cortex-R/M, PCIe/NVMe IP, Apple 새 무선 칩 통합) ↔ "bring up toolchains, SDKs and new accelerator" ② **Xtensa 펌웨어** — Cadence Tensilica는 HiFi/Vision DSP의 기반 코어라 "CPU/DSP 아키텍처"에 근거가 됨 ③ 성능 튜닝·Power Analyzer·DRAM/PCIe 대역폭 감각 ↔ "memory, power, real-time performance", "KV-cache·memory bandwidth"
- **최대 갭/리스크**: ML 엔지니어링 경력 0년(필수 요건 탈락 가능성 높음). **같은 회사 BSP 자리를 진행 중** — 동시에 지원하면 신호가 흐려질 수 있다
- **예상 인터뷰 포맷**: [추정] BSP와 같은 HM 검토 → intro → tech session → 온사이트. tech session에 **ML 기본(양자화·모델 구조·메모리 계산) + 임베디드 코딩**이 섞일 가능성
- **다음 액션**: ① **지금은 단독 지원하지 않는다** — BSP 트랙 유지 ② 리크루터 대화 때 "Embedded AI 자리에도 관심 있다, 장기적으로 그쪽으로 성장하고 싶다"만 전달하고 HM이 firmware-heavy 프로필을 받아줄지 물어본다 ③ 4~6주 edge ML 포트폴리오(아래 3.3)를 만들어 갭을 줄인다

---

## 1. 포지션 요약 (JD 핵심)

| 항목 | 내용 |
|---|---|
| 직함 | **Embedded AI Engineer** (Greenhouse id 4392090009). 2026-09-01 **On-Device AI Inference Engineer**로 첫 게시 → 2026-09-23 제목 변경 + 본문 재작성 → 2026-09-25 17:15 ET 개별 수정(보드 일괄 갱신 15:18 이후) [확인됨][5][6] |
| 팀 / 조직 | "On-Device Models" 그룹으로 보임. **AI research 팀과 긴밀 협업**, 모델팀·플랫폼 벤더와 co-design [확인됨][JD]. 같은 그룹의 **Technical Lead, On-Device AI Inference** 공고(8–12+ yrs HPC, $300–500K)는 2026-09-27 보드에서 사라짐 → 충원 또는 보류 [추정][6] |
| 레벨 | JD에 레벨 없음. 필수 5 yrs ML(2 yrs edge) → **Senior IC 수준** [추정] |
| 위치 / 근무형태 | San Jose, CA. 근무형태 미기재, 하드웨어·센서 작업이라 onsite 가능성 높음 [추정] |
| 연봉 밴드 / Equity | base **$200,000 – $450,000** [확인됨][JD]. BSP 자리($120–300K)보다 하한 $80K, 상한 $150K 높음. $6B 비상장 equity [추정] |
| 비자 · US Person · 클리어런스 | 요건 없음 (컨슈머 제품) [확인됨][JD] |

**주요 업무** [확인됨][JD]
- 여러 센서가 달린 임베디드 시스템용 **데이터 수집·ingestion 파이프라인을 대규모로** 구축
- 모델팀과 함께 latency·memory·power·bandwidth 요구를 맞추는 **모델 구조 co-design**
- **플랫폼 벤더와 툴체인·SDK·새 가속기 bring-up** → 효율적 모델 배포·최적화
- C, C++, Rust로 된 임베디드 펌웨어에 **ML 추론 통합**
- 메모리 사용량·전력·실시간 성능 **프로파일링·최적화**
- 차세대 on-device/edge 배포용 **실리콘 플랫폼(GPU, NPU 등) 평가·선정**

**필수 자격** [확인됨][JD]
- **ML 엔지니어링 5년, 그중 embedded/edge ML 최소 2년**
- 임베디드 시스템과 CPU/DSP/NPU 하드웨어 아키텍처 이해
- **IMU 등 센서(가속도계·자이로·마이크) 실무**
- 센서 데이터 수집 파이프라인 구축 경험
- 임베디드 ML 런타임(**TFLite, llama.cpp, QNN**) 경험
- MCU·edge 프로세서(ARM Cortex-M/A, RISC-V, DSP)용 모델 최적화 경험
- NPU·전용 가속기에 워크로드를 배포해 본 경험

**우대 사항** [확인됨][JD]
- Audio/Voice/Vision 모델
- 경량 LLM
- CNN·RNN·transformer·**KV-cache 동작과 메모리 대역폭 요구**에 대한 이해
- hybrid edge-LLM 파이프라인, 소형 언어모델 온디바이스 통합
- 웨어러블·로보틱스·산업 센싱·IoT 제품 경험

**기술 키워드**: `C` `C++` `Rust` `TFLite` `llama.cpp` `QNN` `NPU` `DSP` `ARM Cortex-M/A` `RISC-V` `IMU` `microphone` `sensor pipeline` `quantization` `KV-cache` `memory bandwidth` `SLM` `hybrid edge-cloud`

**하드웨어 단서** (구 On-Device AI Inference JD, 2026-09-18 확인): "**Hexagon DSP, Ambiq-class MCUs**", INT8/INT4, speech 추론 [확인됨][6]. 새 JD의 **QNN**(Qualcomm AI Engine Direct)과 맞물림 → Qualcomm SoC(Hexagon NPU) + Ambiq 계열 초저전력 MCU 조합일 가능성이 크다 [추정]

---

## 2. 회사 분석

회사 기본 조사(설립·펀딩·투자자·뉴스·문화·HM 추정)는 [BSP 컨텍스트 2장](../hark_ai_embedded_swe/hark_ai_embedded_swe_context.md)에 정리되어 있다. 여기서는 요약과 이 자리에 관련된 부분만 적는다.

### 2.1 무엇을 하는 회사인가
"proactive, multimodal, persistent memory" 개인 AI와 그 전용 하드웨어를 수직 통합으로 만드는 AI 랩 [확인됨][1][3]. 목표는 "universal interface between humans and machines" [확인됨][JD].

### 2.2 제품 · 고객 · 비즈니스 모델
- **모델**: speech·memory 중심 multimodal 모델 [확인됨][2]
- **Handoff** (2026-08-05 preview): 실제 웹사이트를 조작하는 에이전트. Online-Mind2Web 97.7 주장, 토큰 단가 경쟁사 1/10 이하 주장 [확인됨][4]. Adcock는 이후 입력 96%·출력 92% 비용 절감을 언급 [추정·X 게시물][7]
- **하드웨어**: "family of AI devices, for yourself and for the home" [확인됨][8]. 채용 공고상 **Cellular·Wi-Fi·BT·GNSS·NFC·UWB**를 담은 1세대 웨어러블/기기, Qualcomm 칩셋, Android 기반 메인 앱 [확인됨][6]
- **이 자리와 연결**: 서버 쪽 모델(Handoff 등)은 이미 있고, 기기 쪽에는 **항상 켜진 센서·음성 처리 + 소형 모델 + 클라우드 LLM으로 넘기는 hybrid 구조**가 필요하다. JD 우대 사항의 "hybrid edge-LLM pipelines"가 이것 [추정]

### 2.3 단계 · 펀딩 · 규모
| 설립 | 펀딩 총액 / 최근 라운드 | 밸류에이션 | 주요 투자자 | 인원 | HQ |
|---|---|---|---|---|---|
| 2025년 말 (2026-03 stealth 해제) [확인됨][3] | Adcock 개인 $100M + Series A $700M (2026-05-21) [확인됨][1][2] | $6B post-money [확인됨][1][2] | Parkway(리드), NVIDIA, AMD, Intel Capital, **Qualcomm Ventures** 등 [확인됨][1] | 약 70명(2026-05) [확인됨][2] · 공고 55개(2026-09-27) [확인됨][6] | San Jose, CA |

### 2.4 최근 뉴스 (최근 12개월)
- 2026-03-24: stealth 해제, Apple 디자이너 Abidur Chowdhury 영입 [확인됨][3][8]
- 2026-05-21: $700M Series A [확인됨][1][2]
- 2026-08-05: Handoff technical preview [확인됨][4]
- 2026-09-22~23: 임베디드 공고 전면 재편. On-Device AI Inference Engineer → **Embedded AI Engineer**로 개편 [확인됨][5]
- 2026-09-27: TL, On-Device AI Inference 공고가 보드에서 빠짐 [확인됨][6] → 리드가 채워졌다면 이 자리는 그 리드 밑 첫 IC들 중 하나 [추정]

### 2.5 경쟁사 · 시장 포지션
- OpenAI(io/Jony Ive 기기), Meta Ray-Ban AI 글래스, Apple/Google 폰 에이전트. Humane·Rabbit 실패 선례 [확인됨][1]
- 이 자리 관점: 경쟁사의 on-device AI 팀(Apple Neural Engine, Meta Reality Labs, Google Pixel/Tensor)과 **같은 인재 풀**에서 뽑는다 → 밴드가 높은 이유 [추정]

### 2.6 엔지니어링 문화 · 평판 · 보상
- Hark 자체 후기는 거의 없음. Adcock의 Figure는 고보상·고강도 문화로 알려짐 [추정][9]
- 보상: base $200–450K [확인됨][JD]. 같은 회사 BSP $120–300K, TL On-Device AI $300–500K였음 [확인됨][6]

### 2.7 이 포지션이 실제로 할 일 (추론)
- **Always-on 센서 AI**: IMU(제스처·착용 감지·활동), 마이크(wake word·VAD·화자) 모델을 Ambiq급 MCU나 Hexagon DSP에서 저전력으로 상시 실행 [추정]
- **Wake cascade 설계**: MCU 저전력 감지 → DSP/NPU 소형 모델 → SoC·클라우드 LLM으로 단계적으로 깨우는 구조. 단계마다 전력·지연 예산 배분 [추정]
- **온디바이스 SLM**: QNN/llama.cpp로 소형 언어모델을 NPU에 올리고 KV-cache·메모리 대역폭 병목을 관리 [추정]
- **데이터 수집 기기·파이프라인**: 사내 dogfooding 기기에서 센서 데이터 로깅 → 업로드 → 학습 데이터셋 구성 [추정]
- **실리콘 선정**: 차세대 기기용 NPU/SoC 벤더 평가, 벤치마크, 벤더 SDK bring-up — **Don 경험과 가장 닿는 부분** [추정]

### 2.8 리스크
- **요건 리스크**: ML 경력 필수라 서류에서 걸러질 가능성이 높다
- **내부 신호 리스크**: BSP 트랙이 진행 중인데 요건이 안 맞는 자리에 따로 지원하면 "포커스가 없다"로 읽힐 수 있다
- 회사 리스크(하드웨어 미출시, 고강도 문화, Apple 1년 미만 이직 설명)는 BSP와 동일

---

## 3. 적합도 분석 (Don ↔ JD)

### 3.1 요구사항 매칭표
| JD 요구사항 | Don 경험 (레쥬메 근거) | 매칭 |
|---|---|---|
| 5 yrs ML engineering, 2+ yrs edge ML | 레쥬메에 ML 경험 없음. 약 8년 FW/임베디드 | ❌ 갭 (핵심 요건) |
| 임베디드 시스템 + CPU/DSP/NPU 아키텍처 | "ARM Cortex R8/R82/M0+ … FW bring-up", "Xtensa-based FW of the primary data path" (Xtensa = Tensilica DSP 계열 코어), multi-core SoC | ✅ CPU/DSP 강함 · NPU 없음 |
| IMU·가속도계·자이로·마이크 실무 | I2C/SPI 버스 bring-up은 있으나 센서 자체 경험 없음 | ❌ 갭 |
| 센서 데이터 수집 파이프라인 | "test platform SDK … monitored and scheduled all test scenarios and client (eSSD) status" (대규모 챔버 데이터 수집), NVMe telemetry, "module health monitoring sequence" | 🟡 부분·전이 가능 |
| 임베디드 ML 런타임 (TFLite, llama.cpp, QNN) | 없음 | ❌ 갭 |
| MCU·edge 프로세서용 모델 최적화 | "firmware performance tuning with the silicon validation team" — 펌웨어 성능 튜닝은 있으나 모델 최적화 없음 | ❌ 갭 |
| NPU·가속기 워크로드 배포 | "new FPGA Image/FW bring-up … PCIe/NVMe subsystem IP", "Lead integration of new silicon" — 새 IP bring-up은 강함, ML 워크로드 배포 없음 | 🟡 부분 |
| (업무) 벤더와 툴체인·SDK·새 가속기 bring-up | pre/post-silicon bring-up, "cross-functional collaboration with external vendors", Apple 새 칩 통합 | ✅ 강함 |
| (업무) C/C++/Rust 펌웨어에 추론 통합 | "embedded C and C++ programming on bare-metal" | 🟡 C/C++ ✅ · 추론 ❌ |
| (업무) 메모리·전력·실시간 프로파일링 | 성능 튜닝, Power Analyzer, "reliability vs performance/power" margin sign-off, Trace32 | ✅ 강함 |
| (업무) 실리콘 플랫폼 평가·선정 | 실리콘 validation 협업, safety margin sign-off. GPU/NPU 벤치마킹은 없음 | 🟡 부분 |
| 우대: Audio/Voice/Vision 모델 | 없음 | ❌ |
| 우대: 경량 LLM · hybrid edge-LLM | 없음 | ❌ |
| 우대: CNN/RNN/transformer·KV-cache·메모리 대역폭 | DRAM/SRAM bring-up, PCIe 대역폭·데이터 경로 경험 → 대역폭 병목 감각은 전이 가능. 모델 구조 지식은 없음 | 🟡 부분 |
| 우대: 웨어러블·로보틱스·IoT | Apple 컨슈머 플랫폼 무선 칩 통합 | 🟡 부분 |
| 기초 수학 | UC Berkeley 응용수학 (선형대수·수치해석) | 🟡 전이 가능 |

### 3.2 강점 — 어필 포인트
1. **"새 가속기 bring-up"은 이미 해 본 일이다**: FPGA 위에서 Cortex-R/M·PCIe/NVMe IP를 RTL freeze 전에 올렸고, Apple에서는 새 무선 칩을 출하 플랫폼에 붙였다. NPU도 결국 새 IP + 벤더 SDK + 드라이버 + 성능 검증이다.
2. **DSP 계열 코어 경험**: Xtensa 기반 NAND 데이터 경로 펌웨어. HiFi 오디오 DSP·Vision DSP와 같은 코어 계열이라 툴체인·메모리 구조를 안다.
3. **메모리·대역폭 병목 감각**: SSD 데이터 경로, DRAM/SRAM bring-up, PCIe 세대별 대역폭. LLM decode가 **memory-bound**라는 우대 사항의 핵심과 같은 종류의 사고다.
4. **측정과 margin 결정**: Power Analyzer, DSO, Trace32로 측정하고 reliability vs performance/power를 sign-off. "profile and optimize memory, power, real-time performance"에 바로 닿는다.
5. **대규모 데이터 수집 인프라**: 챔버 수십 대의 eSSD 상태·시나리오를 자동 수집하던 테스트 플랫폼 SDK → 센서 데이터 수집 파이프라인의 인프라 쪽과 같은 구조.

### 3.3 갭 & 보완 전략
| 갭 | 인터뷰 전 할 일 | 답변 프레이밍 |
|---|---|---|
| ML 엔지니어링 경력 (필수) | 단기에 메울 수 없다. **포트폴리오 2개**로 "배포 쪽 ML"을 증명: (a) IMU 제스처 + 키워드 스포팅을 MCU에서 TFLite Micro로 실행, (b) 소형 LLM을 llama.cpp로 양자화·프로파일링 | "학습 쪽 연구자가 아니라, 모델을 실리콘 위에서 돌아가게 만드는 쪽이다. 이 JD의 절반인 bring-up·프로파일링·벤더 협업은 8년 해 온 일" |
| 임베디드 ML 런타임 (TFLite/llama.cpp/QNN) | TFLite Micro(arena, op resolver, CMSIS-NN) · llama.cpp(GGUF, Q4_K_M, KV-cache) · **Qualcomm AI Hub**로 QNN 모델 컴파일·실기기 프로파일링 1회 | 직접 해 본 수치로 말한다 (모델 크기, 지연, 피크 메모리) |
| 모델 최적화 | PTQ vs QAT, per-channel INT8, 프루닝, distillation, op fusion을 1장으로 정리 → 위 프로젝트에 PTQ 적용 전후 비교 | "양자화는 비트폭과 오차를 맞바꾸는 것 — 펌웨어에서 고정소수점 산술을 다뤄 봤다" (근거 있을 때만) |
| IMU·마이크 센서 | IMU+마이크가 한 보드에 있는 개발보드(예: Arduino Nano 33 BLE Sense, ESP32-S3-BOX)로 ODR·FIFO·타임스탬프 동기화·PDM→PCM 실습 | I2C/SPI bring-up 경험 + 실습 결과 |
| NPU 배포 | Qualcomm AI Hub 무료 실기기 프로파일링 또는 Apple Neural Engine(Core ML) 실험 | "벤더 SDK bring-up과 op fallback 디버깅은 새 IP bring-up과 같은 문제" |
| transformer·KV-cache | KV-cache 크기 계산, decode tokens/s ≈ 메모리 대역폭 / 모델 바이트 식을 손으로 풀 수 있게 | SSD 데이터 경로 대역폭 계산 경험과 연결 |

### 3.4 종합 평가 · 커리어 방향 정합성
- **적합도 ⭐⭐ (2/5)**: 업무 6개 중 3개(가속기 bring-up·프로파일링·실리콘 평가)는 강하지만, **필수 요건 7개 중 4개가 ❌**이고 그중 1번이 경력 연수 조건이다. 서류 통과 가능성이 낮다.
- **커리어 방향 ⭐⭐⭐⭐⭐ (5/5)**: NPU 선정·bring-up·모델 co-design은 AI accelerator·on-device 피벗의 정확한 목적지다.
- **판단**: P03의 결론(BSP 1순위, Embedded AI는 2~3년 목표)을 유지한다. 다만 이 JD는 "embedded system software development **and** AI model deployment"를 함께 요구하므로, 포트폴리오를 만든 뒤에는 **ML 쪽이 강한 지원자들 사이에서 임베디드 쪽이 강한 후보**로 설 여지가 있다. 리크루터에게 관심만 알리고 HM 판단을 물어보는 것이 비용 대비 가장 좋다.

### 3.5 레쥬메 튜닝 제안 (이 JD용 — 포트폴리오 완료 후에만)
- Summary에 추가: `Silicon/IP bring-up with vendors, CPU/DSP (Cortex-R/M, Xtensa), memory/power/performance profiling; edge ML deployment (TFLite Micro, llama.cpp) — personal projects`
- "Brought up new silicon IP (ARM Cortex-R8/R82/M0+, PCIe/NVMe subsystem) on FPGA before RTL freeze, working with IP vendors on toolchains, drivers and performance validation."
- "Profiled and tuned firmware for memory footprint, bandwidth and power on production SoCs using Trace32, protocol analyzers and power analyzers."
- (프로젝트 섹션 신설) "Deployed INT8 keyword-spotting and IMU gesture models on a Cortex-M4F MCU with TFLite Micro + CMSIS-NN; reduced latency X ms → Y ms and peak RAM by Z% via quantization and arena tuning." ← 실제 수치로 채울 것

---

## 4. 예상 인터뷰

### 4.1 프로세스
```
[추정 — BSP 트랙 리크루터 안내(2026-09-20) 기준]
지원 → HM 검토 → ① intro session
                 → ② tech session (ML 배포 개념 + 임베디드/C 코딩 혼합 추정)
                 → ③ 온사이트 (모델 최적화 case study / 시스템 설계 추정)
```
- Hark 전 직군 공통 3단계로 보인다. 이 자리는 **ML 팀 HM**이 볼 가능성이 커서 BSP와 면접관이 다를 것 [추정]
- Figure 패턴: 개념 위주 1차, 이후 case study 발표 [추정][9]

### 4.2 실제 보고된 질문
- 이 자리의 후기는 아직 찾지 못함 (2026-09-27 기준).

### 4.3 JD 기반 예상 기술 질문
| 카테고리 | 질문 | 왜 나올지 (JD 근거) |
|---|---|---|
| C/C++ 코딩 | INT8 행렬곱/1D conv를 C로 구현 (zero-point, scale, requantize, saturation) | "Integrate ML inference into embedded firmware" |
| C/C++ 코딩 | 센서 스트림 sliding window + 특징 추출(RMS, FFT bin 등) — 링버퍼로 | 센서 파이프라인 |
| C/C++ 코딩 | DMA ping-pong 버퍼로 마이크 PCM을 받아 추론 스레드에 넘기기 | 실시간 성능 |
| ML 배포 개념 | PTQ vs QAT, per-tensor vs per-channel, 캘리브레이션 데이터, INT4 weight-only | 모델 최적화 |
| ML 배포 개념 | TFLite Micro의 tensor arena, op resolver, 지원 안 되는 op 처리 | 런타임 요건 |
| ML 배포 개념 | NPU에서 op가 CPU로 fallback될 때 성능이 무너지는 이유와 해결(그래프 분할, op 교체) | NPU 배포 |
| LLM/메모리 | 1B 모델 INT4, 32층·hidden 2048·context 4K일 때 KV-cache 크기? LPDDR5 50 GB/s에서 decode tokens/s 상한? | 우대: KV-cache·대역폭 |
| LLM/메모리 | prefill은 compute-bound, decode는 memory-bound인 이유. roofline으로 설명 | 우대 사항 |
| 센서 | IMU ODR·FIFO·watermark 인터럽트, 가속도/자이로 타임스탬프 동기화, 안티에일리어싱 | IMU 요건 |
| 센서 | PDM 마이크 → decimation → PCM, 샘플레이트·비트폭 선택과 전력 | 마이크 요건 |
| 데이터 파이프라인 | 사내 기기 수백 대에서 센서 데이터 수집: 로깅 포맷, 온디바이스 저장, 업로드, 라벨·메타데이터, 프라이버시 | "data collection … at scale" |
| 시스템 설계 | 웨어러블 always-on 음성 비서: MCU(VAD) → DSP(wake word) → SoC NPU(SLM) → 클라우드 LLM 단계별 전력·지연 예산 | hybrid edge-LLM |
| 시스템 설계 | 차세대 기기용 NPU 벤더 2곳을 비교해 고르는 방법 (TOPS/W 말고 무엇을 볼지: 메모리 대역폭, op 커버리지, 툴체인 성숙도, SRAM) | "Evaluate and select silicon platforms" |
| 디버깅 시나리오 | 벤더 SDK로 컴파일한 모델이 PC 결과와 다르다 — 어디부터 보나 (전처리, 양자화 스케일, layout NHWC/NCHW, op 구현 차이) | 벤더 bring-up |

### 4.4 Don 경험 딥다이브 (레쥬메 bullet별)
| 레쥬메 bullet | 예상 꼬리질문 | STAR 소재 |
|---|---|---|
| FPGA에서 Cortex-R/M·PCIe/NVMe IP bring-up | 새 IP bring-up 체크리스트는? 벤더 SDK/RTL 문제와 FW 문제를 어떻게 나눴나? | RTL freeze 전 FPGA에서 잡은 IP 버그 1건 → NPU bring-up과 같은 절차라고 연결 |
| Xtensa NAND 데이터 경로 FW | 명령어 캐시/로컬 메모리 배치, 성능 병목을 어떻게 찾았나? | 핫 루프를 로컬 메모리로 옮기거나 DMA로 겹친 사례 (있다면) |
| 펌웨어 성능 튜닝 (silicon validation 협업) | 무엇을 측정했고 어떤 지표로 개선을 증명했나? | 측정 → 병목 → 수정 → 수치 |
| Apple 새 실리콘 통합, margin sign-off | 성능·전력 trade-off를 어떻게 결정했나? | margin 결정 1건 → 모델 지연·전력 예산 협상과 연결 |
| 테스트 플랫폼 SDK·챔버 자동화 | 규모(기기 수, 데이터량), 실패 처리, 데이터 구조는? | 대규모 수집 인프라 → 센서 데이터 파이프라인과 연결 |

### 4.5 행동 · 동기 질문
- **Why this role (FW 엔지니어가 왜 AI 자리)?** → "지난 8년 동안 새 실리콘을 제품으로 만드는 일을 했다. 다음 10년의 새 실리콘은 NPU이고, 그 위에서 모델이 제약을 지키며 돌게 만드는 것이 내가 가려는 방향이다. 개인 프로젝트로 그 갭을 먼저 메웠다."
- **ML 경력이 없는데 왜 뽑아야 하나?** → "이 JD 업무 6개 중 가속기 bring-up, 프로파일링, 실리콘 평가는 내가 이미 가장 잘하는 일이다. 모델 구조는 모델팀과 co-design하고, 내가 가져오는 건 하드웨어가 실제로 어디서 막히는지 아는 것."
- **BSP 자리와의 관계?** → 솔직하게: "BSP가 지금 가장 맞는 자리라는 건 안다. 팀이 필요하다면 어느 쪽이든 좋고, 장기적으로는 이쪽으로 성장하고 싶다."
- Why leave Apple (약 10개월)? → BSP 컨텍스트 4.5 답변 재사용

### 4.6 Tell me about yourself (영어 초안, 60~90초)
> I'm an embedded firmware engineer, and for about eight years my work has been taking new silicon and making it perform in a real product. At SK hynix and Solidigm I brought up ARM Cortex-R and Cortex-M cores and PCIe/NVMe IP on FPGA before RTL freeze, wrote data-path firmware on Xtensa cores, and tuned firmware performance with the silicon validation team. At Apple I work in the wireless chipset integration group, bringing new silicon into shipping consumer platforms and signing off on the trade-off between reliability, performance and power. Over the past few months I've been applying that to edge ML: I deployed quantized keyword-spotting and IMU gesture models on a Cortex-M MCU with TFLite Micro, and profiled small LLMs with llama.cpp to see where memory bandwidth limits decode. What draws me to this role is that it sits exactly where I want to grow: bringing up new accelerators with vendors, and making models fit the compute, memory and power budget of a first-generation device.

(※ 가운데 edge ML 문장은 **포트폴리오를 실제로 끝낸 뒤에만** 쓴다.)

### 4.7 역질문
1. 이 자리는 on-device 스택 중 어디에 무게가 있나요 — always-on 센서 모델(MCU/DSP) 쪽인가요, SoC NPU 위 SLM 쪽인가요?
2. 1세대 기기의 NPU/SoC는 이미 정해졌나요? "evaluate and select silicon"은 차세대 기기 얘기인가요?
3. 모델팀과 co-design은 어떻게 돌아가나요? 지연·메모리 예산은 누가 정하고 어떻게 협상하나요?
4. 센서 데이터 수집은 지금 어느 단계인가요 (dogfood 기기 수, 인프라 유무)?
5. 이 팀에서 임베디드 배경이 강하고 ML 배포 경력이 짧은 사람이 90일 안에 기여할 수 있는 일은 무엇인가요?

### 4.8 준비 체크리스트
- [ ] **BSP 트랙 우선 유지** — 이 자리는 리크루터 대화 때 관심 표명 + HM이 firmware-heavy 프로필을 볼지 확인만
- [ ] TFLite Micro 키워드 스포팅 + IMU 제스처 모델을 MCU에서 실행하고 지연·RAM·전류 수치 기록 (BSP 체크리스트의 "TFLite Micro keyword spotting 예제"와 같이 진행)
- [ ] llama.cpp로 소형 LLM(0.5–1B) Q4/Q8 양자화 → Mac에서 tokens/s·메모리 측정, 대역폭 식과 비교
- [ ] Qualcomm AI Hub로 모델 1개를 QNN 컴파일·실기기 프로파일링
- [ ] 양자화 1장 요약 (PTQ/QAT, per-channel, zero-point, INT4 weight-only)
- [ ] transformer 추론 1장 요약 (prefill vs decode, KV-cache 크기 식, roofline)
- [ ] IMU/마이크 센서 1장 요약 (ODR, FIFO, 동기화, PDM→PCM)
- [ ] C 코딩: INT8 matmul/conv, 링버퍼 sliding window — `../anduril_firmware_engineer/`의 bit·circular buffer 문제 재사용
- [ ] 기존 Hark 노트 재사용: `../hark_ai_embedded_swe/2026-09-19_hark_study_notes/site/index.html` (저전력·오디오·온디바이스 관련 항목)
- [ ] 포트폴리오 완료 후 3.5 레쥬메 bullet을 실제 수치로 채움

---

## 5. 진행 로그

| 날짜 | 이벤트 | 메모 |
|---|---|---|
| 2026-09-23 | (BSP 트랙 P03에서 1차 검토) | On-Device AI Inference Engineer → Embedded AI Engineer 개편 확인, 적합도 ⭐2 · "2~3년 목표 자리"로 분류 |
| 2026-09-27 | 컨텍스트 파일 생성 (JD·회사 조사) | Greenhouse API로 JD 원문 확보(updated_at 2026-09-25 17:15 ET). 보드 공고 55개, TL On-Device AI Inference 공고 사라짐 확인 |
| 2026-09-28 | 스터디 나침반 작성 | `study_notes/study_prep_list.md` — 모듈 A~O, JD→모듈 매핑, 프로젝트 PJ1~PJ8, 인터뷰 드릴. 빌드: `python3 study_notes/build_study_notes.py` |
| 2026-09-28 | 스터디 노트 위치 이동 | `hark_ai_embedded_ai/study_notes/` → **`dons_study_note_from_experience/embeddedAIPrepBasedOnHarkAIJD/`** (Don이 직접 이동). 상호 링크·빌드 스크립트 경로 수정. 빌드: `python3 dons_study_note_from_experience/embeddedAIPrepBasedOnHarkAIJD/build_study_notes.py`. 모듈별 노트 작성(리서치)은 아직 시작 전 |

---

## 6. 인터뷰 노트 & 회고

<!-- 라운드마다 추가:
### YYYY-MM-DD · <라운드명> · 면접관 <역할>
- 받은 질문:
- 내 답 / 결과:
- 잘한 점 / 아쉬운 점:
- 다음 라운드에 반영할 것:
-->

(아직 없음)

---

## 7. 출처

1. https://techcrunch.com/2026/05/21/hark-raises-700m-series-a-for-its-secretive-universal-ai-interface/ — $700M Series A, $6B, 투자자, OpenAI 경쟁 (확인일 2026-09-27)
2. https://thenextweb.com/news/hark-series-a-funding-adcock-ai-hardware — Series A, speech·memory 모델, 인원 (확인일 2026-09-27)
3. https://techcrunch.com/2026/03/24/meet-the-former-apple-designer-building-a-new-ai-interface-at-hark/ — stealth 해제, Chowdhury 영입 (확인일 2026-09-27)
4. https://www.businesswire.com/news/home/20260805041028/en/Hark-Announces-Handoff-the-Worlds-Best-Web-Browsing-AI-Agent — Handoff 발표 (확인일 2026-09-27)
5. ../hark_ai_embedded_swe/2026-09-19_hark_study_notes/site/plan/P03_embedded_role_map.html — 2026-09-23 임베디드 공고 재편 기록 (자체 노트)
6. https://boards-api.greenhouse.io/v1/boards/hark/jobs — Hark 전체 공고 (2026-09-27 기준 55개), 이 JD id 4392090009 (확인일 2026-09-27)
7. https://x.com/adcock_brett/status/2088273955941716100 — Adcock, Handoff 비용 절감 언급 (확인일 2026-09-27)
8. https://www.eweek.com/news/brett-adcock-hark-ai-devices/ — "family of AI devices" (확인일 2026-09-27)
9. https://www.glassdoor.com/Interview/Figure-AI-Interview-Questions-E9642582.htm — Figure 인터뷰 후기 (참고) (확인일 2026-09-18)

---

## 부록 A. JD 원문 (2026-09-27 수집, Greenhouse updated_at 2026-09-25T17:15 ET · first_published 2026-09-01)

<details>
<summary>펼치기</summary>

**Embedded AI Engineer** — San Jose

**About Hark**

Hark is an artificial intelligence company building advanced, personalized intelligence. One that is proactive, multimodal, and capable of interacting with the world through speech, text, vision, and persistent memory.

We're pairing that intelligence with next-generation hardware to create a universal interface between humans and machines. While today's AI largely operates through chat boxes and decade-old devices, Hark is focused on what comes next: agentic systems that interact naturally with people and the real world.

To get there, we're developing multimodal models and next-generation AI hardware together - designed from the ground up as a single, unified interface for a new era of intelligent systems.

**About the Role**

As an Embedded AI Engineer, you will work closely with the AI research team to bring AI to Hark's next-gen hardware. You will be responsible for the full AI stack on the device, including data ingestion, model development, optimization, and deployment on embedded devices. You should have deep understanding of the constraints of an embedded system (compute, memory, power etc) and leverage your expertise in both embedded system software development and AI model deployment to deliver production-ready ML solutions on hardware.

**Responsibilities**

- Build data collection and ingestion pipelines for an embedded system including various sensors, at scale
- Work closely with model teams to co-design model architectures that meets the required latency, memory, power, and bandwidth
- Work with platform vendors to bring up toolchains, SDKs and new accelerator to ensure efficient model deployment and optimization
- Integrate ML inference into embedded firmware written in C, C++, or Rust
- Profile and optimize memory usage, power consumption, and real-time performance
- Evaluate and select silicon platforms (GPUs, NPUs etc.) for Hark's next gen on-device and edge deployment of a wide range of models

**Requirements**

- 5 years of experience in machine learning engineering, with at least 2 years focused on embedded or edge ML
- Familiarity with embedded systems, and CPU/DSP/NPU HW architectures
- Hands-on experience with IMUs and other sensor types including accelerometers, gyroscopes, and microphones
- Experience building sensor data collection pipelines
- Familiarity and experience with embedded ML run times (e.g. TFLite, llamacpp, QNN)
- Experience optimizing models for deployment on microcontrollers and edge processors such as ARM Cortex-M/A, RISC-V, and DSPs
- Experience deploying workloads on NPUs or specialized accelerators for embedded systems

**Bonus Qualifications**

- Experience with Audio/Voice/Vision models
- Experience with light weight LLM models
- Understand the performance characteristics of edge AI models, including CNN, RNN, transformers, KV-cache behavior, and their memory bandwidth requirements
- Experience designing hybrid edge-LLM pipelines or integrating small language models on device
- Prior work on products in wearables, robotics, industrial sensing, or IoT

**Compensation**

The US base salary range for this full-time position is between $200,000 - $450,000 annually.

The pay offered for this position may vary based on several individual factors, including job-related knowledge, skills, and experience. The total compensation package may also include additional components/benefits depending on the specific role. This information will be shared if an employment offer is extended.

</details>
