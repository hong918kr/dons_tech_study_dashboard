<!-- theme: pastel -->
# Scout AI — Senior Embedded Software Engineer · Context

> **최종 갱신**: 2026-10-13 · **상태**: 💻 Technical screen 일정 선택 중 (리크루터 콜 10-12 통과 · ⚠ Ashby로 10-14 11:19 AM PT 전까지 가능 시간 제출)
> **⚡ 테크니컬 스크린 준비**: [2026-10-12_scout_technical_screen_prep.html](2026-10-12_scout_technical_screen_prep.html) — 문제 은행 Tier1 8 · Tier2 10 · Debug 8 · D-5 플랜
> **JD**: https://job-boards.greenhouse.io/scoutai/jobs/5387985008 · **위치**: Sunnyvale, CA / onsite [추정] · **연봉 밴드**: $210,000 – $265,000 base + bonus + equity
> **사용 레쥬메**: `Resume_Firmware_Engineer_2026_Sep_DonHong.pdf`
> 신뢰도: `[확인됨]` 공식/복수 출처 · `[추정]` 단일·2차 출처 또는 추론. 리크루터 공식 안내가 항상 우선.

<!-- 상태 값: 🔍 조사중 → 📨 지원완료 → 📞 리크루터 → 💻 폰스크린 → 🏢 온사이트 → 🎉 오퍼 | ❌ 불합격 | ⏸ 보류 -->

---

## 0. TL;DR

- **회사**: 국방용 **robotic foundation model "Fury"**(camera-only VLA)와 C2 오케스트레이터 "Ox"를 만드는 Sunnyvale 스타트업. 2024년 8월 설립, 2026-04 **$100M Series A**(미국 국방 테크 역대 최대 Series A), Army UxS 계약(최대 $150M 후속 조달 옵션) [확인됨][1][2][4]
- **역할**: UGV·드론 플랫폼에 들어가는 **센서(IMU·카메라·LiDAR·환경센서) 드라이버 개발과 bring-up**, I2C/SPI/UART/CAN 구현, 센서 데이터 품질 검증 툴 제작. Embedded Linux(컴퓨트 보드)와 MCU 둘 다 다룸 [JD]
- **적합도**: ⭐⭐⭐½ — bring-up·버스 디버그·datasheet→driver는 강하지만, **"Strong experience in embedded linux"가 필수**인데 레쥬메는 bare-metal·RTOS 쪽이라 갭이 핵심 · **커리어 방향**: ⭐⭐⭐⭐ — physical AI·robotics 회사 + edge 추론 컴퓨트 바로 옆에서 일함
- **핵심 어필**: ① Apple에서 신규 칩 통합 시 PCIe/I2C/SPMI/RFFE 인터페이스 루트코즈 ② SK hynix에서 FPGA/SoC bring-up (I2C·SPI·DMA·SRAM/DRAM) ③ DSO·protocol analyzer·JTAG·logic analyzer로 신호/버스 단위 디버그 ④ bring-up → NPI → MP 전 주기, factory test 설계 (= "센서 데이터 품질 검증 툴")
- **최대 갭/리스크**: ① Embedded Linux 실무(kernel driver, device tree, V4L2, IIO, Yocto/Buildroot) ② CAN 경험 없음 ③ 카메라 파이프라인(MIPI CSI-2 / GMSL) ④ 34명 규모 초기 스타트업 + 국방 미션(TS clearance 가능성) ⑤ 상한 $265K는 Hark 구두 $250K와 비슷하지만 Apple 대비 total comp 비교 필요
- **인터뷰 포맷** [확인됨 — 리크루터 콜·메일 10-12]: 리크루터 → **Technical screen**(CoderPad, 언어 자유, 45분 = 경험 15 + 코딩 30, 센서·과열 감지류 · 메일상 범위: practical technical exercises + **experience building and scaling hardware systems**) → **Final 4라운드**(CEO 30 · CTO 30 · HW 엔지니어 45 · 임베디드 엔지니어 45)
- **다음 액션**: ⓪ **Ashby 링크로 스크린 일정 제출 — 10-14(수) 11:19 AM PT 마감**, 추천 10-19~10-20 (Neros 온사이트 10-22/23 주 피하기) ① **Technical screen 준비** — [준비 문서](2026-10-12_scout_technical_screen_prep.html)의 D-5 플랜, T1-1·T1-2·T1-5 최우선 ② 스크린 날짜 확정되면 플랜 역산 ③ 통과 시 Final 4라운드(CEO·CTO·HW·임베디드) 준비

---

## 1. 포지션 요약 (JD 핵심)

| 항목 | 내용 |
|---|---|
| 팀 / 조직 | **회사 63명, 그중 엔지니어 25명. 임베디드 팀은 현재 2명이고 둘 다 조지아(Georgia) 근무 → 이 자리는 세 번째 임베디드 엔지니어이자 Sunnyvale 오피스의 첫 임베디드 엔지니어** [확인됨 — 리크루터 10-12]. hardware·robotics·AI 엔지니어와 협업 [JD] |
| 레벨 | Senior (연차 미명시 — 밴드로 보면 5~10년차 IC) [추정] |
| 위치 / 근무형태 | Sunnyvale, CA. 공고에 remote 표기 없음 → onsite [추정]. 필드 테스트는 Paso Robles 인근(중부 CA 군 기지)에서 진행 [추정][3][8] |
| 연봉 밴드 / Equity | **$210K–$265K base** + bonus + "meaningful equity", 의료보험 본인부담 $0, 점심 제공, 이전 지원(조건부) [확인됨][JD] |
| 비자 · US Person · 클리어런스 | **U.S. Person 필수**(export controlled 정보 접근), **U.S. Top Secret clearance 취득을 요구받을 수 있음** [JD] |
| 게시일 | 2026-08-12 최초 게시, 2026-10-01 갱신 [확인됨][JD] |

**주요 업무**
- 임베디드 플랫폼용 센서 드라이버 개발·디버그
- 신규 센서 bring-up: **IMU, camera interface, LiDAR module, environmental sensor**
- HW 엔지니어와 함께 board bring-up·validation
- I2C / SPI / UART / **CAN** 통신 구현·테스트
- MCU와 embedded processor용 펌웨어 작성
- 센서 데이터 품질·신뢰성 검증용 툴·테스트 제작
- HW-SW 통합 이슈 트러블슈팅
- 펌웨어 아키텍처·문서화 기여

**필수 자격**
- 임베디드용 C/C++ 기본기
- **Embedded Linux 실무 경험 (strong)**
- MCU·임베디드 개발 워크플로 이해
- 기본 전자회로 개념(signal, timing, 디버깅)
- SPI·I2C·UART 경험
- datasheet를 읽고 드라이버 구현
- 디버깅 마인드·디테일
- robotics·임베디드·하드웨어 인접 SW에 대한 관심
- U.S. Person, 미국·동맹국 방위에 대한 열정, TS clearance 가능

**우대 사항**
- 별도 명시 없음. 업무 내용상 **CAN, 카메라 인터페이스(MIPI CSI-2/GMSL), LiDAR(Ethernet/UDP), 시간 동기화(PPS/PTP)** 경험이 사실상 우대 [추정]

**기술 키워드**: `C` `C++` `Embedded Linux` `device driver` `MCU` `I2C` `SPI` `UART` `CAN` `IMU` `camera` `LiDAR` `board bring-up` `datasheet` `test tooling`

---

## 2. 회사 분석

### 2.1 무엇을 하는 회사인가
"전쟁용 AI 랩(warfare AI lab)"을 표방하며, 무인 체계의 **AI reasoning layer**에만 집중한다. 지휘관의 의도를 자연어로 받아 지상·공중·해상·우주의 이종 무인 체계 함대가 협동 행동하도록 바꾸는 것이 목표 [확인됨][1][2].

- **Fury**: camera-only, end-to-end learned **Vision-Language-Action(VLA)** foundation model. 영상·위성사진·텔레메트리·자연어를 입력받아 주행·ISR·다중 로봇 기동을 수행하며, COTS 하드웨어에서 엣지로 돈다. GPS/통신 거부 환경에서도 동작하는 것을 목표로 한다 [확인됨][4][6][7]
- **Fury Autonomous Vehicle Orchestrator / Ox**: C2 시스템과 무인 자산 사이의 "agentic interoperability layer". 각 기체 API에 맞는 구조화된 JSON 명령을 생성하며, **기존 flight controller나 autonomy SW는 수정하지 않는다**. 2026-02 중부 CA에서 UGV 1대와 드론 여러 대로 자연어 명령 기반 실사격(strike) end-to-end 데모를 했다 [확인됨][5][10]
- 하드웨어: 자체 UGV(G01·A01), 민수용 ATV에서 군용 ATV로 전환, Textron ISV(Army 계약), Hendrick Motorsports NOMAD UGV. 탑재물은 카메라·GPU·통신 장비 [확인됨][3][4][7][11]

### 2.2 제품 · 고객 · 비즈니스 모델
- 고객: 미 육군(Army UxS 계약, xTechOverwatch 우승), DARPA, Army Applications Laboratory 등. **첫해 DoD 계약 $11M** [확인됨][2][3]
- Army UxS: 16개월 계약으로 Army가 제공한 **Infantry Squad Vehicle(ISV)**에 Fury를 통합한다. 이후 **최대 $150M** 추가 조달이 가능하다. Textron Systems와 독점 차량 통합, Edge Case Research가 safety validation을 맡는다 [확인됨][4]
- 1st Cavalry Division(Fort Hood) 훈련에 쓰인 autonomy 업체 20곳 중 하나이며, 부대 배치 목표는 2027년 [확인됨][3]
- 비즈니스 모델: 플랫폼에 구애받지 않는 "AI 두뇌 + 오케스트레이터" 소프트웨어 라이선스·통합 계약. 하드웨어는 COTS나 파트너 차량을 쓴다 [추정]

### 2.3 단계 · 펀딩 · 규모
| 설립 | 펀딩 총액 / 최근 라운드 | 밸류에이션 | 주요 투자자 | 인원 | HQ |
|---|---|---|---|---|---|
| 2024-08 | ~$115M / **$100M Series A (2026-04-29)**, Seed $15M (2025-01 클로즈, 04-16 발표) | 미공개 | Align Ventures·Draper Associates(공동 리드), Booz Allen Ventures, Decisive Point, Perot Jain 등 | Series A 발표 때 **34명**. 현재 오픈 포지션 28개 [확인됨][2][8] | Sunnyvale, CA (+Paso Robles 필드 조직) |

### 2.4 최근 뉴스 (최근 12개월)
- **2026-04-29** $100M Series A. 오버서브스크라이브됐고, 자금은 foundation model과 multi-agent collaboration 확장에 쓴다 [확인됨][1][2]
- **2026-04** TechCrunch가 중부 CA 군 기지의 훈련장 "Foundry"를 취재했다. 퇴역 군인이 이끄는 ops 팀이 ATV를 8시간 교대로 몰며 데이터를 모으고 RL 피드백을 기록한다. 처음엔 물류 지원용이고, 이후 자율 무기로 확장할 계획이다 [확인됨][3]
- **2026-02-18** Fury Autonomous Vehicle Orchestrator 공개, 실사격 데모 [확인됨][5][10]
- **2025-12-02** Army xTechOverwatch 우승(600개 이상 지원 → 40개 결선) [확인됨][11]
- **2025-09-02** Hendrick Motorsports Technical Solutions의 NOMAD UGV와 파트너십 [확인됨][12]
- **2025-08-29** Army UxS autonomy 계약 [확인됨][4]
- 2026년 5월 이후 공식 보도자료는 없다(10-03 기준). 레이오프 보도도 찾지 못했다 [확인됨][12]

### 2.5 경쟁사 · 시장 포지션
- **Autonomy SW / foundation model**: Shield AI(Hivemind), Applied Intuition(defense), Overland AI(지상), Forterra, Field AI, Anduril(Lattice) [추정]
- 차별점: "camera-only + VLA + 자연어 지휘 + 플랫폼 무관". LiDAR 중심 스택보다 하드웨어가 싸고 신호 노출(signature)이 적다는 게 메시지다 [확인됨][4][7]
- 연결고리: CEO Colby Adcock은 Figure AI 창업자 Brett Adcock의 형제로 Figure 이사회 멤버다. CTO Collin Otis는 Kodiak Robotics 창립 엔지니어(Director of Autonomy & AI) 출신이고 Uber ATG를 거쳤다 [확인됨][3][6]

### 2.6 엔지니어링 문화 · 평판 · 보상
- JD 문구: "urgency, precision, and relentless work", "moving fast, context-switching daily", "help define the culture and process". **고강도 초기 스타트업** 톤이다 [JD]
- Glassdoor 평이 거의 없다. 같은 이름의 채용 플랫폼 "Scout AI"와 섞여 있어 신뢰도가 낮다 [추정][9]
- 보상: base $210–265K는 Hark BSP(구두 ~$250K)와 비슷하고, Neros FW Test($145–204K)·Verkada($180–300K)와 견줄 만하다. Series A 후라 equity 상승 여력은 크지만 유동성은 불확실하다 [추정]

### 2.7 이 포지션이 실제로 할 일 (추론) `[추정]`
Fury가 **camera-only**이므로 센서 스택의 중심은 카메라다. 여기에 IMU·GNSS·차량 텔레메트리가 붙고, LiDAR는 데이터 수집이나 ground truth용일 가능성이 크다.
1. **엣지 컴퓨트 보드(Embedded Linux)**: Jetson Orin/Thor급 GPU 모듈일 가능성이 높다[추정]. 여기서 multi-camera(MIPI CSI-2 / GMSL SerDes, thermal) 드라이버, device tree, V4L2 파이프라인, 카메라 간 **하드웨어 트리거·타임스탬프 동기화**를 맡는다
2. **MCU 펌웨어**: IMU(SPI) 고속 샘플링, PPS/GNSS 시간 동기, 전원·환경 센서(I2C), 차량 drive-by-wire와 **CAN** 연동
3. **센서 데이터 품질 툴**: frame drop·지연·타임스탬프 jitter·IMU 노이즈/bias 측정, 학습 데이터 수집 파이프라인(rosbag 유사) 검증. Don의 factory test·stress 설계 경험과 그대로 겹친다
4. **차량 통합**: ATV·ISV·NOMAD·드론마다 센서 키트를 장착하고 bring-up한다. Sunnyvale ↔ 필드(Paso Robles) 왕복이 있을 수 있다

### 2.8 리스크
- **미션**: 자율 "strike" 데모까지 한 회사라 공격용 자율 무기에 대한 개인적 입장을 정리해 둬야 한다(Anduril·Neros 준비 때와 같은 질문)
- **클리어런스**: TS 취득 요구 가능성이 있다. 해외 체류 이력이나 가족 관계에 따라 수개월~1년 이상 걸린다
- **스타트업 리스크**: 인원 34명 → 빠른 확장 중이다. 매출은 대부분 정부 계약이라 예산 사이클에 좌우된다
- **Embedded Linux 필수 조건**: 레쥬메만으로는 스크리닝에서 걸릴 수 있다 → 레쥬메 키워드와 스토리를 미리 보강해야 한다

### 2.9 가설 검증 — 이 자리가 Hark Embedded AI 역량을 원하나? (2026-10-04)
**Don의 가설**: Scout가 임베디드 공고를 하나만 냈고, Fury·Orchestrator 제품군이 edge AI 작업을 반드시 필요로 하므로, 이 자리는 Hark Embedded AI Engineer와 같은 역량을 원한다.

**결론: 절반만 맞다.** 회사 전체로 보면 edge AI 역량이 필요하다는 건 맞다. 하지만 Scout는 그 일을 **AI 팀 공고에 명시적으로 배정**했고, 이 임베디드 자리는 그 아래층(센서·드라이버·펌웨어)을 맡는다 [확인됨][8][14].

| 근거 | 내용 |
|---|---|
| JD 문구 | 임베디드 JD에는 inference·model·GPU·NPU·quantization·latency budget 같은 ML 단어가 **하나도 없다**. 반복되는 단어는 sensor·driver·protocol·bring-up이다. Hark AI JD는 TFLite·llama.cpp·QNN과 ML 경력 5년을 요구했다 [JD] |
| edge 추론 담당 | AI Engineer / Senior AI Engineer(**Fury Orchestration Team**) 공고의 업무 범위에 **"edge inference optimization"**이 들어 있다. 자격은 Python·PyTorch·SFT/DPO/RLVR이다 [확인됨][14] |
| 센서 통합 담당 | Robotics Software Engineer가 "cameras, LiDAR, IMUs를 로봇 시스템에 통합"하고 perception·localization을 맡는다. 즉 autonomy·ROS 레벨이다 [확인됨][14] |
| Orchestrator 구조 | 각 기체 API에 JSON 명령을 보내고, **flight controller나 autonomy SW는 수정하지 않는다**. Orchestrator는 LLM/SW 층이지 임베디드 층이 아니다 [확인됨][5] |
| 공고가 하나뿐인 이유 | 역할이 층별로 나뉘어 있다: AI 팀(모델 + edge 최적화) / Robotics SWE(autonomy) / **Embedded(센서·FW)** / Vehicle Integration Tech(배선·장착). 임베디드는 맨 아래층의 1인 또는 소수 자리로 보인다. AI까지 흡수하는 만능 자리라는 신호는 없다 [추정] |

**가설에서 맞는 부분 — 실제 연결고리**
- Fury는 운전병 시연 데이터로 학습한다(ATV 8시간 교대 주행) [3]. 그래서 **"센서 데이터가 정확하고 안정적"이어야 한다는 것 = VLA 학습 데이터 품질**이다. Hark AI JD의 1번 업무("센서 데이터 수집·ingestion 파이프라인")와 IMU 요건이 바로 이 절반에 해당한다
- 엣지 GPU 보드 위에서 카메라 → GPU 경로(zero-copy·dmabuf·동기화)는 임베디드와 AI 팀의 경계다. 작은 회사라 이 경계를 넘을 기회는 있다 [추정]

**Hark Embedded AI 노트(A–O, 97편) 재사용도**
| 재사용도 | 모듈 | 이유 |
|---|---|---|
| 높음 | G1·G2·G3·G6·G7(센서·시간 동기), H1·H7(로깅·데이터 품질), J6(테스트), K2(프로파일링), N3(로보틱스 센싱), O2(Linux 온디바이스) | JD 업무와 직접 일치 |
| 중간 (대화 소재·성장 경로) | D(성능 모델), E6·E7(GPU·메모리 시스템), H3–H5(학습 데이터), I1(예산 설정), M4(데이터시트 해석) | 엣지 GPU에 대한 이해와 "왜 데이터 품질이 모델에 중요한가"를 설명할 때 |
| 낮음 | A–C(ML 기초·모델·경량화), F(TFLite Micro·llama.cpp·QNN — Qualcomm 중심, Scout는 NVIDIA 계열 추정), L(온디바이스 LLM·음성), N1·N2·N4(웨어러블) | AI 팀 담당이거나 도메인이 다름 |
| **Hark 노트에 없음 (Scout 필수)** | Embedded Linux kernel driver(device tree·IIO·V4L2), CAN, MIPI CSI-2/GMSL, PTP | 4.8 A–E가 메워야 할 부분 |

**전략적 시사점**
- 준비 비중: **Linux 드라이버·센서 bring-up 70% / edge AI 연결 30%**. edge AI를 앞세워 준비하면 필수 조건(embedded Linux) 준비가 부족해진다
- 인터뷰 프레이밍: "ML 하고 싶다"고 하면 "AI 팀으로 옮겨 갈 사람"으로 보일 수 있다. 대신 **"센서 데이터는 Fury의 입력이고, 나는 그걸 믿을 수 있게 만드는 사람"**이라는 메시지로 가고, edge 추론은 성장 관심사로 덧붙인다
- 가설 확인 질문(역질문에 추가): "Fury를 차량 컴퓨트에 배포·최적화하는 건 누가 맡나요? 임베디드가 카메라 → GPU 경로(캡처·zero-copy·동기화)까지 담당하나요?"
- 커리어: Hark Embedded AI로 가는 **징검다리로는 유효하다**. AI 팀 옆에서 엣지 GPU·센서 데이터를 다루는 경험이 쌓이기 때문이다. 다만 "이 자리 = embedded AI"는 아니다

---

## 3. 적합도 분석 (Don ↔ JD)

### 3.1 요구사항 매칭표
| JD 요구사항 | Don 경험 (레쥬메 근거) | 매칭 |
|---|---|---|
| Strong C/C++ for embedded | "Developed using embedded C and C++ programming on bare-metal (pre/post-silicon)" | ✅ 강함 |
| **Strong experience in embedded Linux** | 툴 목록에 "Linux"만 있음. 실무는 bare-metal(Cortex-R/M, Xtensa) 중심 | ❌ 갭 (핵심) |
| MCU·임베디드 개발 워크플로 | "FPGA Image/FW bring-up ARM Cortex R8/R82/M0+", Trace32 | ✅ 강함 |
| 기본 전자회로(signal, timing, debug) | "Debug at signal, bus, and system level with DSOs and protocol Analyzer", "hardware safety margins" | ✅ 강함 |
| SPI·I2C·UART | "multi-core interconnection I2C, SPI, DMA … bring-up", Apple "I2C, SPMI, RFFE" 루트코즈, UART 기반 테스트 자동화 | ✅ 강함 |
| datasheet → driver 구현 | 신규 실리콘·IP bring-up(PCIe/NVMe 서브시스템, NAND 인터페이스 레이어) | ✅ 강함 (센서 datasheet는 아님) |
| 센서 드라이버 (IMU/camera/LiDAR) | 직접 경험 없음. RF 칩셋 통합은 "주변 칩을 시스템에 붙이는" 같은 패턴 | 🟡 전이 가능 |
| Board bring-up·validation (HW 협업) | "Silicon/system bring up -> NPI -> MP", "Worked closely with the design engineer to perform root cause analysis" | ✅ 강함 |
| CAN | 경험 없음 | ❌ 갭 |
| 센서 데이터 품질 검증 툴·테스트 | factory test-node 아키텍처, stress 케이스 설계, Python 테스트 플랫폼 SDK, chip reliability system | ✅ 강함 |
| HW-SW 통합 트러블슈팅 | Apple 직무 자체가 "new chip meets the full HW/SW system" 루트코즈 | ✅ 강함 |
| Robotics 관심 | 커리어 피벗 목표(physical AI·robotics). 레쥬메에는 아직 근거 없음 | 🟡 스토리로 보완 |
| U.S. Person / TS 가능 | 확인 필요 | 🟡 확인 필요 |

### 3.2 강점 — 어필 포인트
1. **"새 칩이 시스템을 만날 때 깨지는 것"을 고치는 사람**: Apple RF 통합의 PCIe/I2C/SPMI/RFFE 루트코즈 ↔ JD "Troubleshoot hardware-software integration issues", "Work with hardware engineers during board bring-up"
2. **pre-silicon부터 MP까지**: FPGA에서 SoC 버스(I2C/SPI/DMA)를 bring-up하고 NPI·MP까지 가 본 경험 ↔ 새 센서 키트를 차량마다 붙이는 반복적 bring-up
3. **계측 장비 실력**: DSO·logic analyzer·protocol analyzer·JTAG/Trace32 ↔ "basic electronics (signals, timing, debugging)"를 넘어서는 수준
4. **검증 툴 빌더**: factory test-node·stress 시나리오·reliability system·UART 자동화 SDK ↔ "Build tools and tests to validate sensor data quality and reliability"
5. **고객 요구를 FW 기능으로**: Google/Meta NVMe 2.0, telemetry 디버그 기능 ↔ 스타트업에서 필드 피드백을 빠르게 기능으로 만드는 능력

### 3.3 갭 & 보완 전략
| 갭 | 인터뷰 전 할 일 | 답변 프레이밍 |
|---|---|---|
| **Embedded Linux 실무** | Raspberry Pi/BeagleBone 또는 Jetson Orin Nano에서 I2C IMU를 **kernel driver(IIO)**로 작성하고 device tree overlay로 바인딩. Buildroot/Yocto로 이미지 빌드. U-Boot → kernel → rootfs 부트 흐름 정리 | "제 bare-metal 경험은 driver의 아래 반(레지스터·DMA·IRQ)입니다. Linux는 그 위에 프레임워크(regmap, IIO, V4L2)를 얹는 것이고, 최근 X를 직접 만들었습니다" |
| CAN | MCP2515 + SocketCAN(`candump`/`cansend`)으로 실습. CAN 프레임·arbitration·bit timing·error frame·bus-off 정리 | "물리층 디버그 방법(스코프로 차동 신호 보기)은 이미 익숙합니다" |
| 카메라 파이프라인 | MIPI CSI-2 기본(D-PHY, lane, virtual channel), GMSL/FPD-Link SerDes, V4L2/media controller, 하드웨어 트리거 동기화 | PCIe 같은 고속 SerDes 링크 bring-up 경험과 연결 |
| 센서 도메인 (IMU/LiDAR) | IMU FIFO·ODR·interrupt·온도 드리프트, LiDAR UDP 패킷·PTP 시간 동기 | factory calibration·reliability 관점으로 "데이터 품질" 이야기 |
| Robotics 근거 | 위 실습을 GitHub에 공개(작은 sensor kit 프로젝트) | 피벗 동기를 스토리로 |

### 3.4 종합 평가 · 커리어 방향 정합성
- **적합도 ⭐⭐⭐½**: 업무 8개 중 6개(bring-up·버스·HW 통합·툴·MCU·디버그)는 강하게 맞는다. 다만 필수 조건 "strong embedded Linux"가 레쥬메에 안 드러나 서류·스크린 단계 리스크가 있다. 실습 프로젝트 하나로 🟡까지 끌어올리면 ⭐4가 가능하다.
- **커리어 방향 ⭐⭐⭐⭐**: robotics foundation model 회사에서 엣지 GPU 컴퓨트와 센서 사이의 SW를 맡는다. physical AI 피벗의 직접 경로다. AI accelerator 칩 설계 쪽은 아니고, "AI 모델이 돌아가는 로봇 하드웨어" 쪽이다.
- 다른 파이프라인과 비교: Verkada(Embedded Linux)·Hark BSP 준비가 Linux 갭 보완에 그대로 재사용된다 → **준비 비용이 낮다**.

### 3.5 레쥬메 튜닝 제안 (이 JD용)
- Apple bullet을 앞세우고 "sensor/peripheral" 언어로 바꾼다: *"Led bring-up and root-cause of new peripheral silicon on shipping platforms, debugging I2C/SPMI/RFFE/PCIe interfaces at signal level with DSOs and protocol analyzers alongside hardware engineers."*
- 검증 툴 강조: *"Architected factory test nodes and stress-based test suites to validate data integrity and catch latent HW/integration defects before mass production."*
- Linux 키워드 (실제로 한 만큼만): *"Built Linux-based test infrastructure and automation (Python, UART/serial control) for large-scale device reliability testing."* + 실습 후 *"Side project: Linux IIO kernel driver + device-tree overlay for SPI IMU on Raspberry Pi/Jetson (Buildroot image)."*
- SUMMARY의 Technical Knowledge에 `Embedded Linux (drivers, device tree)`, `I2C/SPI/UART`, `Board bring-up`, `Sensor integration`을 앞쪽에 배치

---

## 4. 예상 인터뷰

### 4.1 프로세스
**[확인됨 — 리크루터 Bobby, 2026-10-12]**
```
리크루터 콜 (10-12 완료)
→ Technical screen: CoderPad, C 또는 C++, 45분
     15분 = 경험 이야기 / 30분 = 코딩 1문제 (엣지 케이스 테스트, 임베디드 관점)
     리크루터 키워드: Array · Item · State · Edge · debug · embedded constraint
     주제: 센서, time·temp, 과열 감지 ("return overheating")
→ Final 4라운드: CEO 30분 · CTO 30분 · HW 엔지니어 45분 · 임베디드 엔지니어 45분
```
- 리크루터 피드백: "it goes well". Don은 스크린을 최대한 빨리 잡아 달라고 요청
- **공식 안내 메일 (Bobby, 2026-10-12 11:19 AM)** [확인됨]: 45분 · Virtual · CoderPad · Focus = *practical technical exercises, problem solving, and discussion of your experience building and scaling hardware systems*. 일정은 Ashby 링크로 선택, **48시간 안에 가능 시간 제출**("moving quickly", 면접관 일정 확보 목적). 언어는 가장 편한 것 자유 선택. Tips: 사고 과정 말하기 · 질문하기 · 간결하게 · 협업하듯 tradeoff 논의 · 막혀도 계속 말하기 · 서두르지 않기
- 콜 메모와 비교해 **새로 생긴 것**: ① 경험 파트 주제가 "building and scaling hardware systems"로 구체화 → 준비 문서 §7 스토리 A·B·C ② "exercises" 복수형 → 본 문제 + 변형 가능성 ③ 언어 자유 ④ 48시간 마감
- 준비 문서: [2026-10-12_scout_technical_screen_prep.html](2026-10-12_scout_technical_screen_prep.html)

~~이전 추정: 리크루터 → HM → 기술 스크린 60분 → 온사이트(발표 + 실습)~~ — 리크루터 안내로 교정. HM 콜 단계는 없고, 발표·실습 과제 언급도 없었다
- 이전 근거: Glassdoor "Scout AI" 페이지 요약에 "3라운드, 온사이트에서 formal presentation과 hands-on practical exam"이 있다. 다만 직군이 미상이고 동명 채용 플랫폼과 섞여 있다 [추정][9]
- 초기 국방 하드웨어 스타트업(Anduril·Neros) 패턴으로 보면 실습은 "datasheet 주고 드라이버/레지스터 시퀀스 작성", "logic analyzer 캡처 해석", "보드에서 센서 읽기"일 가능성이 있다 [추정]

### 4.2 실제 보고된 질문
- 공개 후기는 아직 없음(2026-10-03 기준).
- [확인됨 — 리크루터 10-12] 코딩 주제 힌트: "Leetcode-ish, Scout engineering type of problem", 센서 시계열(time, temp)에서 과열 감지·반환. 엣지 케이스 테스트와 임베디드 제약을 본다

### 4.3 JD 기반 예상 기술 질문
| 카테고리 | 질문 | 왜 나올지 (JD 근거) |
|---|---|---|
| C/C++ 코딩 | IMU 샘플용 lock-free SPSC ring buffer (ISR producer / task consumer) | 센서 드라이버·데이터 품질 |
| C/C++ 코딩 | 레지스터 비트필드 read-modify-write 매크로, endian 변환, 16-bit 2's complement 센서값 → 물리 단위 | datasheet → driver |
| C/C++ 코딩 | UART 바이트 스트림에서 프레이밍된 패킷 파싱(헤더·길이·CRC) — 상태 머신 | UART·LiDAR/GNSS 프로토콜 |
| 임베디드 개념 | volatile·memory barrier·ISR에서 하면 안 되는 것·DMA와 cache coherency | MCU·embedded processor |
| Embedded Linux | 부트 흐름(BootROM → SPL/U-Boot → kernel → init), device tree가 하는 일, `compatible` 매칭과 probe | **필수 요건** |
| Embedded Linux | I2C 센서를 Linux에 붙이는 방법: userspace(`/dev/i2c-N`, i2c-tools) vs kernel driver(i2c_driver, regmap, IIO) — 언제 무엇을 쓰나 | 센서 드라이버 |
| Embedded Linux | kernel IRQ 처리: top half/bottom half, threaded IRQ, workqueue, 지연 원인 분석, PREEMPT_RT | 실시간 센서 샘플링 |
| Embedded Linux | 카메라 스택: V4L2 / media controller, MIPI CSI-2 lane·VC, 버퍼 관리(mmap/dmabuf), frame drop 디버그 | camera interfaces |
| 버스 / 프로토콜 | I2C clock stretching·NACK·bus hang 복구(9 clocks), pull-up 선택 / SPI mode(CPOL/CPHA) / UART baud 오차 | I2C/SPI/UART |
| 버스 / 프로토콜 | CAN: 차동 신호·arbitration·bit stuffing·error frame·bus-off·종단 저항 120Ω, CAN FD, SocketCAN | CAN |
| 도메인 | 여러 카메라·IMU·LiDAR 타임스탬프를 어떻게 맞추나(HW trigger, PPS, PTP/gPTP, 단조 클럭) | 센서 퓨전 데이터 품질 |
| 도메인 | IMU: ODR·FIFO watermark interrupt·노이즈/bias/온도 드리프트·anti-aliasing | IMU bring-up |
| 디버깅 시나리오 | "새 보드에서 I2C 센서가 응답하지 않는다" — 전원·리셋·주소·pull-up·스코프 단계별 접근 | board bring-up |
| 디버깅 시나리오 | "필드에서 차량 진동 중 카메라 프레임이 간헐적으로 깨진다" — SerDes 링크·케이블·EMI·버퍼 오버런 구분 | HW-SW 통합 |
| 시스템 설계 | 차량용 센서 키트 설계: 카메라 N대 + IMU + GNSS + CAN을 엣지 GPU에 연결, 로깅·동기·health monitoring·장애 시 degrade | firmware architecture |
| 시스템 설계 | 센서 데이터 품질 자동 검증 파이프라인(지표·임계값·CI/HIL) | build tools & tests |

### 4.4 Don 경험 딥다이브 (레쥬메 bullet별)
| 레쥬메 bullet | 예상 꼬리질문 | STAR 소재 |
|---|---|---|
| Apple: PCIe/I2C/SPMI/RFFE 루트코즈 | 가장 어려웠던 인터페이스 실패는? 어떻게 HW와 SW 원인을 분리했나? | 신규 칩 통합 중 간헐 실패 → 스코프/analyzer 캡처 → 원인 → 수정과 검증 |
| Apple: factory test-node 아키텍처 | 어떤 지표로 latent defect를 잡나? false positive는 어떻게 관리하나? | stress 시나리오 설계 → MP 전에 잡은 결함 사례 |
| SK hynix: FPGA bring-up (Cortex-R8/R82/M0+) | 첫 부팅이 안 될 때 순서는? I2C/SPI 컨트롤러 검증은 어떻게 했나? | RTL freeze 전 FPGA에서 SRAM/DRAM·버스 bring-up |
| SK hynix: error reporting/handling | 오류 분류·복구 정책, 로그가 시스템에 주는 영향 | → 센서 health monitoring 설계로 연결 |
| SK hynix: NVMe telemetry 디버그 기능 | 필드 이슈를 원격으로 진단할 수 있게 한 설계 포인트 | → 필드 배치된 로봇의 진단 로그와 연결 |
| 테스트 자동화 SDK (UART 시퀀스) | 동시 다수 디바이스 제어 구조, 실패 처리 | → 센서 검증 툴 설계와 연결 |

### 4.5 행동 · 동기 질문
- Why Scout AI? → physical AI의 최전선(foundation model이 실제 차량을 움직인다) + 엣지 하드웨어와 센서가 모델 품질을 결정한다는 점에서 내 bring-up·검증 경험이 바로 모델 성능으로 이어진다 + 초기 단계라 아키텍처를 정할 수 있다
- Why leave Apple (짧은 재직 Dec 2025–)? → 커리어 방향(robotics/physical AI)을 명확히 하고, 소비자 기기 통합에서 자율 시스템 하드웨어로 옮기려 한다. 짧은 재직 기간에 대한 질문을 대비해 둘 것
- 국방·자율 무기 미션에 대한 생각은? → 입장을 미리 정리 (Anduril·Neros 답변 재활용)
- 빠른 컨텍스트 스위칭·애매한 요구사항에서 일한 경험은?
- 필드(테스트 사이트)에 자주 나가는 것에 대해서는?

### 4.6 Tell me about yourself (영어 초안, 60~90초)
> I'm an embedded systems engineer who lives at the hardware–software boundary. Right now at Apple I work in the wireless chipset integration group, where I lead bring-up of new silicon into shipping platforms and root-cause the failures that show up when a new chip meets the full system — PCIe, I2C, SPMI, RFFE — down at the signal level with scopes and protocol analyzers. I also designed factory test nodes that catch latent hardware and integration defects before mass production.
> Before that I spent about seven years at SK hynix and Solidigm on SSD firmware: bringing up new FPGA images and Cortex-R and M cores, verifying I2C, SPI, DMA and memory subsystems before RTL freeze, and shipping production C/C++ firmware for Google and Meta.
> What ties it together is making new hardware trustworthy — getting it to talk, measuring whether the data is right, and building the tools to prove it. Recently I've been going deeper into embedded Linux driver work — device tree, IIO and V4L2 — because I want to move into robotics. Scout is exactly where that matters: Fury's quality depends on clean, synchronized sensor data from real vehicles, and that's the layer I want to own.

### 4.7 역질문
1. 현재 센서 키트 구성과 컴퓨트 플랫폼(Jetson 계열?)은 무엇이고, 차량(ATV·ISV·NOMAD·드론)마다 얼마나 다른가요?
2. 카메라·IMU·GNSS 타임 동기화와 데이터 품질에서 지금 가장 아픈 문제는 무엇인가요?
3. 이 역할은 Linux 커널·BSP 쪽과 MCU 펌웨어 쪽 비중이 어떻게 되나요? 임베디드 팀 규모와 구성은요?
4. 필드 테스트(중부 CA)에는 얼마나 자주 나가나요? 첫 90일에 기대하는 결과물은요?
5. 클리어런스 스폰서 일정과, 클리어런스가 나오기 전에 할 수 있는 업무 범위는요?

### 4.8 준비 체크리스트
**지원 전**
- [ ] U.S. Person 여부·TS 클리어런스 가능성 스스로 점검 (해외 체류·가족 사항)
- [ ] 레쥬메 튜닝 (3.5) — Linux·sensor·bring-up 키워드, 실습 프로젝트 bullet 추가
- [ ] 국방/자율 무기 입장 정리 (Anduril·Neros 답변 재사용)

**스터디 후보 목록 (초안 — 다음 단계에서 확정)**
- [ ] **A. Embedded Linux 기초 (최우선)**: 부트 흐름(BootROM → U-Boot → kernel → init), device tree 문법·overlay·`compatible`/probe, kernel 모듈 빌드, sysfs/devfs, Buildroot vs Yocto
  - 재사용: `hark_ai_embedded_swe/2026-09-19_hark_study_notes/bsp/B03_linux_android_bsp.md`, `B01_bsp_anatomy.md`, `verkada_sr_embedded_linux_engineer_connectivity/notes_site/`
- [ ] **B. Linux 드라이버 모델**: platform/i2c/spi driver, regmap, **IIO 서브시스템**(IMU·환경센서), IRQ(threaded IRQ·workqueue), GPIO/pinctrl, DMA API
- [ ] **C. 카메라 스택**: MIPI CSI-2/D-PHY, GMSL·FPD-Link SerDes, V4L2·media controller, dmabuf, multi-camera HW trigger 동기화, (Jetson이면) Argus/NVMM [추정]
- [ ] **D. CAN**: 물리층·프레임·arbitration·error handling·bus-off, CAN FD, SocketCAN 실습(`candump`/`cansend`, MCP2515 또는 vcan)
- [ ] **E. 센서 도메인**: IMU(ODR·FIFO·interrupt·bias/노이즈), LiDAR(Ethernet UDP 패킷·포인트클라우드), GNSS(NMEA/UBX·PPS), 시간 동기(PTP/gPTP·PPS·단조 타임스탬프)
- [ ] **F. 버스 디버그 복습**: I2C(clock stretching·bus recovery), SPI mode, UART 오차 — 이미 강점이므로 "말로 설명"만 정리
  - 재사용: `hark_ai_embedded_swe/2026-09-19_hark_study_notes/concepts/C03_bsp_peripheral_drivers.md`, `C10_debugging_bringup_schematics.md`
- [ ] **G. C 코딩**: ring buffer·비트조작·패킷 파서·센서값 변환
  - 재사용: `anduril_firmware_engineer/` 111문제 은행, `don-c-prac-master/`, Verkada `concurrency_practice/`
- [ ] **H. 실습 프로젝트 (갭 메우기 핵심)**: Raspberry Pi 또는 Jetson Orin Nano + SPI/I2C IMU → IIO kernel driver + DT overlay → 샘플레이트·jitter 측정 툴(Python) → GitHub 공개
- [ ] **I. 시스템 설계**: 차량용 센서 키트(카메라 N + IMU + GNSS + CAN → 엣지 GPU), 로깅·health monitoring·degrade 모드
- [ ] **Hark Embedded AI 노트 재사용** (2.9 표): G1·G2·G3·G6·G7, H1·H7, J6, K2, N3, O2 — `dons_study_note_from_experience/embeddedAIPrepBasedOnHarkAIJD/`
- [ ] **J. 회사·제품 이해**: Fury VLA 개념, Ox 오케스트레이터, Army UxS·ISV, ROS 2 기초(센서 메시지·타임스탬프) [추정]

### 4.9 리크루터 콜 준비 — 2026-10-12(월) 10:00 · Bobby Celestin
**콜의 목적은 세 가지다**
1. **탈락 조건 통과**: U.S. Person, 클리어런스 의향, Sunnyvale onsite, 국방 미션, Embedded Linux — 짧고 분명하게 "Yes"
2. **정보 얻기**: 프로세스·타임라인, HM·팀 구성, Linux와 MCU 비중, 레벨, 보상 구조
3. **보상 포지셔닝**: 숫자는 먼저 말하지 않는다. 말해야 하면 "공고 밴드의 상단"

리크루터 콜은 기술 면접이 아니다. 20~30분 안에 **"말이 잘 통하고, 필수 조건이 맞고, 진짜 관심 있는 사람"**으로 보이면 된다 [추정].

**예상 흐름과 답변 (영어 초안)**

| 질문 | 답변 요지 |
|---|---|
| Tell me about yourself (60초, 리크루터용) | 아래 초안 ① |
| Why Scout? | 아래 초안 ② |
| Why leave Apple so soon? (Dec 2025 입사) | 아래 초안 ③ — Apple 험담 금지, "방향"으로 답한다 |
| **Embedded Linux 경험은?** (가장 중요) | 아래 초안 ④ — 사실대로 말하되 강점부터 |
| U.S. Person인가? | "Yes." (사실 확인 후 답할 것) |
| 클리어런스 받을 의향은? | "Yes, I'm willing to go through the TS process." |
| Sunnyvale onsite 가능? | "Yes — I'm already in the Bay Area, so the commute is easy." 필드 테스트(Paso Robles 인근) 출장도 괜찮다고 말할 것 |
| 국방·자율 무기 미션에 대해 | "I'm comfortable with and motivated by the defense mission — I've been focusing my search on defense and robotics." |
| 다른 진행 중인 프로세스? | 회사명은 말하지 않는다. "I'm in later stages with a couple of other companies, so I'd like to understand Scout's timeline." — 너무 세게 압박하지 않는다 |
| 희망 연봉? | 아래 초안 ⑤ |
| 현재 연봉? | 말하지 않는다. CA Labor Code 432.3상 고용주는 이전 급여를 물을 수 없다 → "I'd prefer to focus on the value for this role." |
| 입사 가능 시점 | "Typically two weeks' notice after an offer." (본인 상황에 맞게) |
| 비자 스폰서 필요? | "No." |

**① Tell me about yourself (리크루터용, 60초)**
> I'm an embedded systems engineer who works right at the hardware–software boundary. At Apple I'm in the wireless chipset integration group — I bring up new silicon on shipping platforms and root-cause interface failures like I2C, SPMI and PCIe, down to the signal level with scopes and protocol analyzers. Before that I spent about seven years at SK hynix and Solidigm on SSD firmware: bringing up new chips on FPGA, verifying buses like I2C and SPI, and shipping production C/C++ firmware for customers like Google and Meta. What I enjoy most is getting new hardware to talk and proving the data coming out of it is right — and that's exactly what this role is about: bringing up sensors on new platforms.

**② Why Scout?**
> Two reasons. First, I want to move into robotics and physical AI, and Scout is one of the few places where a foundation model is actually driving real vehicles in the field. Second, Fury is camera-first and learns from real driving data — so the quality of sensor data directly decides how good the model is. That's the layer I'm strongest at, and at a team this size I could own it end to end.

**③ Why leave Apple?**
> Apple has been great experience for silicon integration at scale. But my long-term goal is robotics and autonomous systems, and I'd rather make that move now and build sensor and platform software for robots than stay on consumer devices.

**④ Embedded Linux — 정직하게, 강점부터**
> My production work has mostly been bare-metal and RTOS on Cortex-R/M and Xtensa — the "bottom half" of a driver: registers, interrupts, DMA, and bring-up with a scope. Linux has been my daily environment for tooling and test infrastructure. Over the past few weeks I've been going deep on the Linux kernel side — device tree, platform/I2C/SPI drivers, IIO, V4L2, and CAN with SocketCAN — building and booting kernels in a QEMU lab. So the hardware side is where I'm strongest, and I'm ramping fast on the kernel framework side.

- 주의: **Linux 드라이버를 제품에 출시했다고 말하지 않는다**. 실제로 직접 해 본 범위(QEMU 랩, 모듈 빌드 등)만 말하고, 본인 경험에 맞게 문장을 고친다
- 리크루터가 깊게 파고들지는 않는다. "HM 단계에서 더 자세히 이야기할 수 있다"로 넘긴다

**⑤ 희망 연봉**
> I'd like to learn more about the level and the full package first. I saw the posted range of $210K to $265K — given my experience with bring-up and integration, I'd expect to be toward the upper end of that range, and I'm also interested in understanding the equity component.

- 근거: 다른 진행 건의 Hark 구두 base ~$250K + 사이닝 $100K. 하한선을 먼저 말하지 않는다
- 레벨이 Senior가 아니라 Staff로 열릴 수 있는지도 물어볼 것

**리크루터에게 물을 질문 (우선순위 순, 5~6개만)**
1. What does the interview process look like from here, and what's the typical timeline? (단계 수, 기술 스크린 형식, 온사이트에 **발표 + 실습 과제**가 있는지 [9])
2. Who is the hiring manager, and how big is the embedded / hardware team today?
3. How is the role split between embedded Linux on the compute side and MCU firmware? Which sensors and compute platform are you using today?
4. Is this a Senior-level role, or is there flexibility on level? How is the compensation structured — base, bonus, and equity (options or RSUs, vesting)?
5. What does onsite vs. field time look like — how often do engineers go to the test site?
6. (시간이 남으면) Who owns deploying Fury onto vehicle compute — does embedded work on the camera-to-GPU path? → 2.9 가설 확인

**하지 말 것**
- edge AI·ML을 앞세우지 않는다. 메시지는 **"센서 데이터를 믿을 수 있게 만드는 사람"**이다 (2.9)
- 숫자를 먼저 말하지 않는다. 현재 연봉을 공개하지 않는다
- 다른 회사 이름을 말하지 않는다. Apple을 험담하지 않는다
- 너무 길게 답하지 않는다. 한 답변에 30~60초면 충분하다

**콜 전 체크리스트**
- [ ] U.S. Person 여부 최종 확인 (답이 확실해야 함)
- [ ] 이 페이지 · 레쥬메 PDF · JD(부록 A)를 화면에 띄워 두기
- [ ] ①~⑤ 소리 내서 2번 읽기 (특히 ④ Embedded Linux)
- [ ] 조용한 장소, 이어폰, 메모장 · 9:55에 입장
- [ ] 콜 직후 메모: 프로세스·HM 이름·밴드·타임라인 → 5. 진행 로그에 기록

**콜 후 감사 메일 (짧게, 당일)**
> Hi Bobby, thanks for taking the time today and for walking me through the role and the process. I'm even more excited about the sensor bring-up work on Fury's platforms. Looking forward to the next steps. Best, Don

---

## 5. 진행 로그

| 날짜 | 이벤트 | 메모 |
|---|---|---|
| 2026-10-03 | 컨텍스트 파일 생성 (JD 조사) | Greenhouse 공고 5387985008 (최초 08-12, 갱신 10-01). 적합도 ⭐3.5 / 방향 ⭐4. 미지원. Embedded Linux 필수 → 스터디 목록 확정 예정 |
| 2026-10-04 | 가설 검증: "이 자리 = Hark Embedded AI 역량?" | 결론 절반만 맞음 — edge inference는 AI Engineer 공고에 배정, 임베디드는 센서·FW 층. Hark 노트 재사용도 표 추가(2.9). 준비 비중 Linux/센서 70 : edge AI 30 |
| 2026-10-08 | **리크루터 연락** (Bobby Celestin, bobby@scoutco.ai) | 지원 후 리크루터 콜 요청 메일. ModernLoop 링크로 일정 선택 |
| 2026-10-11 | 리크루터 콜 일정 확정 + 준비 | **2026-10-12(월) 10:00**. 4.9 준비 섹션 작성. 공고 여전히 열려 있음(10-01 갱신), 4월 이후 새 보도자료 없음 |
| 2026-10-12 | **리크루터 콜 완료** (Bobby) — "it goes well" | 다음: Technical screen (CoderPad C/C++ 45분, 센서·과열 감지류). Final 4라운드(CEO·CTO·HW·임베디드). 회사 63명/엔지니어 25명, 임베디드 2명(조지아) → Sunnyvale 첫 임베디드. 스크린 ASAP 요청 |
| 2026-10-12 | **리크루터 공식 메일** — Technical interview 초대 | Ashby 일정 링크, 48시간 안에 가능 시간 제출(→ 10-14 11:19 AM PT). Focus에 "experience building and scaling hardware systems" 추가 확인. 언어 자유 |
| 2026-10-12 | Technical screen 준비 문서 작성 | `2026-10-12_scout_technical_screen_prep.md/.html` — Tier1 8 · Tier2 10 · Debug 8 · D-5 플랜. 테스트 기대값은 참조 C 코드로 검증(ASan/UBSan ALL PASS) |
| 2026-10-12 | 연습 사이트 구축 | `technicalScreenPrep/` — 18문제 + Debug 드릴, 문제 영어·풀이 한국어·SVG 차트, stub/solution 전부 ASan/UBSan 검증 |
| 2026-10-13 | 메일 반영 업데이트 | 준비 문서에 §2.6 Tips→행동 규칙, §6.0 날짜 추천, §7 scaling 스토리 A·B·C 추가 |

---

## 6. 인터뷰 노트 & 회고

<!-- 라운드마다 추가:
### YYYY-MM-DD · <라운드명> · 면접관 <역할>
- 받은 질문:
- 내 답 / 결과:
- 잘한 점 / 아쉬운 점:
- 다음 라운드에 반영할 것:
-->

---

## 7. 출처

1. https://www.govconwire.com/articles/scout-ai-100m-series-a-fury-ai — Series A $100M, Fury 개요 (확인일 2026-10-03)
2. https://www.prnewswire.com/news-releases/scout-ai-raises-100m-series-a-to-build-the-ai-brain-for-unmanned-warfare-302756871.html — Series A 공식 보도자료: 투자자, 34명, $11M 계약 (2026-10-03)
3. https://techcrunch.com/2026/04/29/coby-adcocks-scout-ai-raises-100-million-to-train-models-for-war-we-visited-its-bootcamp/ — 훈련장 Foundry, 창업자 배경, Fort Hood, Ox (2026-10-03)
4. https://www.prnewswire.com/news-releases/scout-ai-awarded-army-uxs-autonomy-contract-302542164.html — Army UxS 계약, ISV, $150M, Textron·Edge Case Research (2026-10-03)
5. https://www.prnewswire.com/news-releases/scout-ai-introduces-fury-autonomous-vehicle-orchestrator-302691787.html — Fury Orchestrator, JSON API 레이어 (2026-10-03)
6. https://oodaloop.com/company-profiles/defense-tech/scout-ai/ — 설립 2024-08, Collin Otis(Kodiak·Uber ATG) (2026-10-03)
7. https://www.scoutco.ai — 공식 사이트: camera-only, edge, COTS, UGV G01/A01 (2026-10-03)
8. https://boards-api.greenhouse.io/v1/boards/scoutai/jobs — 오픈 포지션 28개, Paso Robles 필드 조직 (2026-10-03)
9. https://www.glassdoor.com/Interview/Scout-AI-Interview-Questions-E10120070.htm — 인터뷰 후기 요약(검색 스니펫, 직군 미상·동명 회사 혼재 가능) (2026-10-03)
10. https://thedefensepost.com/2026/02/20/ai-drone-strike/ — 자연어 명령 실사격 데모 (2026-10-03)
11. https://www.prnewswire.com/news-releases/scout-ai-selected-as-a-winner-of-xtechoverwatch-competition-302630034.html — xTechOverwatch 우승, NOMAD 데모 (2026-10-03)
12. https://www.prnewswire.com/news/scout-ai-inc./ — 보도자료 목록(최신 2026-04-29) (2026-10-03)
13. https://job-boards.greenhouse.io/scoutai/jobs/5387985008 — JD 원문 (2026-10-03)
14. https://boards-api.greenhouse.io/v1/boards/scoutai/jobs/5309913008 — Senior AI Engineer JD("edge inference optimization", Fury Orchestration Team). AI Engineer 4643324008, Robotics SWE 5137118008도 함께 확인 (2026-10-04)

---

## 부록 A. JD 원문 (2026-10-03 수집)

<details>
<summary>펼치기</summary>

**Senior Embedded Software Engineer** — Sunnyvale, CA (Greenhouse 5387985008, first published 2026-08-12, updated 2026-10-01)

The future of defense will be decided by those who field intelligent machines at scale. At Scout AI, we’re developing Fury, the first robotic foundation model for defense, to give U.S. forces overwhelming, adaptable, and autonomous power across every domain. Fury enables human operators to command fleets of robots through natural language, and empowers those machines to sense, decide, and act together as one. This mission will ask everything of us: urgency, precision, and relentless work.

**The Role**

We’re looking for a Senior Embedded Software Engineer who is excited about working close to the hardware layer, especially bringing up and validating sensors on new platforms. This role focuses on developing low-level firmware and drivers that enable sensors to function reliably in embedded and robotics systems.

You’ll work with experienced engineers across hardware, robotics, and AI to ensure sensor data is accurate, stable, and production-ready. We’re a startup. You’ll be moving fast, context-switching daily, and helping define the culture and process as we go. This is a rare opportunity to come in early and architect the future of defense.

**Responsibilities**

- Develop and debug sensor drivers for embedded platforms
- Bring up new sensors (IMU, camera interfaces, LiDAR modules, environmental sensors)
- Work with hardware engineers during board bring-up and validation
- Implement and test communication protocols (I2C, SPI, UART, CAN)
- Write firmware for microcontrollers and embedded processors
- Build tools and tests to validate sensor data quality and reliability
- Troubleshoot hardware-software integration issues
- Contribute to firmware architecture and documentation

**Qualifications**

- Strong fundamentals in C or C++ for embedded systems
- Strong experience in embedded linux
- Understanding of microcontrollers and embedded development workflows
- Familiarity with basic electronics concepts (signals, timing, debugging)
- Experience working with hardware interfaces like SPI, I2C, or UART
- Ability to read datasheets and implement drivers from documentation
- Strong debugging mindset and attention to detail
- Interest in robotics, embedded systems, or hardware-adjacent software
- Must be a U.S. Person due to required access to U.S. export controlled information or facilities
- Passion about defense of the United States and its allies
- May be expected to obtain and hold a U.S. Top Secret security clearance

**Why Join Scout**

- Work on the world’s most important frontier, ensuring U.S. and allied dominance in the age of intelligent machines
- Be a core part of a team building the first defense-specific robotic foundation model
- Collaborate with some of the top engineers in autonomy, AI, and national security
- See your work deployed on real systems
- Help define the future of intelligent defense systems
- Backed by Draper Associates, Booz Allen Ventures, and other top investors

**Benefits**

- Competitive base salary and bonus
- Meaningful equity
- Premium medical, dental, and vision plans with $0 paycheck contribution
- Competitive PTO and company holiday calendar
- Catered lunch daily and fully stocked kitchen
- EV charging
- Relocation assistance (depending on role eligibility)

The stated salary range below represents an estimated base pay only and reflects consideration of multiple compensation factors. Final salary offers may differ depending on factors including, but not limited to, relevant experience or training background, specialized skills, and business needs. Most full-time positions also include highly competitive equity awards, which form part of Scout AI's overall compensation package. In addition, Scout AI provides comprehensive, top-tier benefits to full-time employees.

US Salary Range: $210,000—$265,000 USD

</details>
