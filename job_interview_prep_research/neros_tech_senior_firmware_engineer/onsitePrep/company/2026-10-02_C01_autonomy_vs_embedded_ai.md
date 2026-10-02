# 🤖 C01 · Neros의 "autonomy"는 무슨 뜻이고, Hark Embedded AI와 비슷한가?

> **질문 (2026-10-02)**: Neros 공고에 나오는 autonomy가 무슨 뜻인지, 혹시 Hark의 Embedded AI Engineer와 비슷한 일인지, Hark 쪽도 임베디드에서 AI를 돌리는 건지. **답**: 둘 다 임베디드에서 AI를 돌린다. 다만 Neros의 autonomy는 드론이 스스로 나는 기능 **전체**(인식·항법·유도·제어·런타임)이고 AI는 그중 한 부품이다. Hark Embedded AI는 **온디바이스 AI 자체가 일의 전부**다. Neros에서 가장 비슷한 자리는 **Autonomy Platform & Runtime**(특히 Senior Platform Engineer)이다.

## 1. 근거로 쓴 자료

- Neros autonomy 계열 공고 10개 (Greenhouse, 2026-10-02 수집): `research/job_board_snapshots/2026-10-02_autonomy_jds.md`
- Hark Embedded AI Engineer: https://job-boards.greenhouse.io/hark/jobs/4392090009 (2026-09-25 수정본 기준)
- 이전 분석: [Hark Embedded AI 컨텍스트 파일](../../../hark_ai_embedded_ai/hark_ai_embedded_ai_context.html)

## 2. Neros의 "autonomy"란

조종사 개입을 줄이고 드론이 **스스로 판단해서 나는 기능 전체**를 가리킨다. 공고에 나오는 제품 기능은 다음과 같다 [확인됨: Autonomy Lead, 각 팀 공고].

- **terminal guidance**: 표적까지 마지막 구간을 스스로 유도
- **position hold**: 제자리 유지
- **GPS-denied 항법**: GPS가 재밍당해도 위치를 추정 (visual-inertial, terrain-relative navigation)
- **visual navigation**, **swarming**(군집)
- 전제 조건: "contested environments where GPS and communications are denied" (Evaluation 팀 공고)

### 데이터가 흐르는 순서

```
카메라(EO/열화상)·IMU·GNSS
        │
   Perception ──── 표적 탐지·분류·추적        ← AI 비중 높음 (learned detection)
        │
   State Estimation ── 내 위치·표적 위치 + 불확실성  ← EKF 등 고전 추정
        │
   Guidance & Control ── 유도 법칙, 제어 명령   ← 유도·제어 이론
        │
   Flight Controller (STM32, Betaflight/PX4)   ← 펌웨어 조직
        
   이 모든 SW가 도는 바닥 = Autonomy Platform & Runtime (embedded Linux 컴패니언, Jetson급 [추정])
   옆에서 지켜보는 것 = Evaluation · Data · Test (회귀, 시뮬레이션, HIL, 비행 로그)
```

### 팀별로 본 AI 비중

| 팀 (공고) | 하는 일 | AI 비중 | 밴드 |
|---|---|---|---|
| **Perception** (Lead, Senior) | EO·열화상으로 작은 표적을 먼 거리에서 탐지·분류·추적, 아군·민간 구별 | **높음**: "progression from classical computer vision to learned detection", 임베디드 가속기에 양자화 배포 | Lead $201–281K, Senior $163.5–228.5K |
| **State Estimation & Navigation** (Lead) | GPS 없이 위치 추정, VIO, terrain-relative nav, EKF/UKF/factor graph | 낮음: 고전 추정 이론 | $191.5–268K |
| **GNC** (Senior) | terminal homing guidance (proportional navigation), 제어 경로 | 거의 없음 | $163.5–228.5K |
| **Autonomy Platform & Runtime** (Lead, Senior Platform Engineer) | 온보드 런타임(C++ on embedded Linux, ROS 2/DDS/Zenoh), 실시간 스케줄링, 지연 시간 측정, **ring buffer + triggered capture 로깅**, PTP/PPS 시간 동기, **신경망을 임베디드 가속기에 배포**(Jetson/TensorRT, Qualcomm, TI, Hailo) | **중간**: 모델을 만들지 않고 **돌아가게 만든다** | $163.5–228.5K |
| **Evaluation, Data & Test** (Manager), **Data Platform** (Senior) | 회귀 게이트, 시뮬레이션, HIL, 비행 로그 수집·검색·재생, 학습 데이터셋 버전 관리 | 간접적 | $191.5–268K / $163.5–228.5K |
| **Autonomy Lead** | 차기 autonomy 제품을 처음부터: terminal guidance, position hold, visual nav, swarming | 혼합 | $190–266K |
| Autonomy Mechatronics (Hardware & Test) | autonomy를 위한 기구·센서 하드웨어와 시험 | 없음 | $121–169.5K |

- 펌웨어 조직(Adam)과 autonomy 조직은 **별개**다 [추정: 공고 구조]. 둘이 만나는 경계는 "mission computer ↔ flight controller" 인터페이스(MAVLink 등)와 HIL 벤치다
- AI 모델은 주로 **Linux 컴패니언 컴퓨터**에서 돈다. STM32 비행 제어기는 비행 제어를 맡는다

## 3. Hark Embedded AI도 임베디드에서 AI를 돌리나?

**그렇다. 그게 이 자리의 정의다.** 공고 문구 [확인됨]:

- "responsible for the **full AI stack on the device**, including data ingestion, model development, optimization, and deployment on embedded devices"
- "Integrate **ML inference into embedded firmware** written in C, C++, or Rust"
- "optimizing models for deployment on **microcontrollers and edge processors such as ARM Cortex-M/A, RISC-V, and DSPs**"
- "deploying workloads on **NPUs** or specialized accelerators"
- "Evaluate and select **silicon platforms** (GPUs, NPUs etc.)"
- 보너스: 오디오·음성·비전 모델, **소형 LLM**, KV-cache 동작과 메모리 대역폭, hybrid edge-LLM 파이프라인

즉 Hark가 직접 만드는 차세대 하드웨어(웨어러블·컴패니언 기기류 [추정])에서 **센서 데이터 수집부터 모델 설계, 최적화, 펌웨어 통합까지** 전부 하는 자리다.

## 4. 나란히 비교

| 항목 | Hark Embedded AI | Neros Autonomy Platform / Perception |
|---|---|---|
| 목적 | 자체 기기에서 멀티모달 AI (음성, 비전, 센서, 소형 LLM) | 드론 자율비행: 표적 탐지·추적, 항법, 유도 |
| AI의 위치 | **일의 중심**: 데이터 → 모델 co-design → 최적화 → 배포 | 여러 구성요소 중 하나. 고전 CV, 추정, 제어 비중이 더 큼 |
| 하드웨어 | MCU(Cortex-M)부터 NPU까지 폭넓게. **실리콘 플랫폼을 고른다** | Jetson급 SoM + 가속기(후보: Jetson, Qualcomm, TI, Hailo), STM32 FC |
| 런타임 | TFLite, llama.cpp, QNN | TensorRT 등 + ROS 2/DDS/Zenoh/LCM 미들웨어 |
| 언어 | C, C++, Rust (+ ML 툴링) | 현대 C++ on embedded Linux, Python |
| 핵심 요구 | **ML 엔지니어링 5년 (edge 2년)**, IMU·마이크 센서 파이프라인, NPU 배포 | 실시간 시스템, 지연 시간 예산, 시간 동기, 고속 로깅, 실기 비행 경험 |
| 제약 | 전력·메모리 (배터리 기기) | 지연 시간·열·무게 (비행체) |
| 밴드 | $200–450K | Platform $163.5–228.5K, Perception Lead $201–281K |

### 가장 비슷한 Neros 자리

**Autonomy Platform & Runtime Lead / Senior Platform Engineer.** 공통 핵심은 "**모델을 제약된 하드웨어에 올리고 latency·memory·power 예산을 맞춘다**"다.

- Neros Platform 공고: "deployment of learned components to the selected embedded accelerator, including quantization and characterization of the resulting accuracy and latency trade off", "specify compute, memory, power, thermal and mass requirements for an embedded compute payload … system on module or carrier board selection"
- Hark 공고: "co-design model architectures that meets the required latency, memory, power, and bandwidth", "Evaluate and select silicon platforms"
- **차이**: Hark는 **모델과 데이터까지** 직접 한다(ML 엔지니어). Neros Platform은 런타임·로깅·시간 동기 같은 **인프라**를 하고, 모델은 Perception 팀이 만든다

## 5. Don 입장에서

| 관점 | 판단 |
|---|---|
| 커리어 방향 (AI 가속기·robotics) | Neros 안에서는 **FW Test보다 Autonomy Platform이 훨씬 가깝다**. Hark Embedded AI는 방향이 가장 직접적이다 |
| 겹치는 강점 | ring buffer 로깅·텔레메트리(NVMe telemetry 디버그 기능), 실시간 제약, 멀티코어 FW, 하드웨어 bring-up, 실리콘 평가 감각(Apple 새 칩 통합) |
| Neros Autonomy Platform 갭 | 현대 C++ on embedded Linux, ROS 2/DDS, 신경망 배포·양자화, 실기 비행 경험 |
| Hark Embedded AI 갭 | **ML 엔지니어링 5년** 요건이 가장 크다. 모델 개발·최적화 경험 |
| 온사이트 활용 | 누가 "autonomy 쪽 관심 있나?"라고 물으면: "Yes — especially the platform side: logging, time sync, and getting models onto embedded accelerators within a latency budget." 지금 트랙은 Test라서 **먼저 꺼낼 필요는 없다** |

## 6. 이어서 확인할 만한 것

- 온사이트 역질문: "How does the firmware team interface with the autonomy platform — is the HIL bench shared?" (두 조직의 경계와 이동 가능성 파악)
- Neros autonomy 컴퓨터가 실제로 어떤 SoM인지 (공고에는 후보만 나온다)

## 체크

- [ ] Neros autonomy = 인식·추정·유도·제어·런타임 전체, AI는 주로 perception이라는 걸 한 문장으로 말할 수 있다
- [ ] Hark Embedded AI와 Neros Autonomy Platform의 공통점과 차이를 각각 한 줄로 말할 수 있다
- [ ] 온사이트에서 autonomy 질문이 오면 쓸 답 한 문장을 정했다
