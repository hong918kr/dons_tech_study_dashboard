# Hark — Embedded Software Engineer · Context

> **최종 갱신**: 2026-09-29 · **상태**: 📨 지원완료 (9/21) · 리크루터 회신 대기 — "이번 주 안에 연락" 확약(9/29)
> **📅 진행 중 계획**: [P06 7일 계획](2026-09-19_hark_study_notes/site/plan/P06_7day_plan.html) (9/30~10/6, Zephyr 실습 포함) · 급하면 [P04 4일 압축판](2026-09-19_hark_study_notes/site/plan/P04_4day_sprint.html)
> **🗺 자리 지도**: [P03 임베디드 7자리](2026-09-19_hark_study_notes/site/plan/P03_embedded_role_map.html) — Embedded Software 팀 5공고(BSP·Systems·Platform·UX·DevOps) + On-Device Models 2공고 비교. BSP 1순위, Systems 2순위
> **⚡ JD 개정 2026-09-23**: [P02 개정 분석](2026-09-19_hark_study_notes/site/plan/P02_jd_change_2026-09-23.html) — OTA·온디바이스AI·factory test 항목 삭제, FPGA·vendor 통합 신설, 3+ → 5+ yrs, RTOS → eLinux/AOSP 포함
> **🧱 BSP 집중**: [B00~B06](2026-09-19_hark_study_notes/site/bsp/B00_bsp_overview.html) — 공고 제목이 BSP로 바뀐 뒤 추가. 기초·Zephyr 실전·Linux/Android·면접 41문항
> **🧩 코딩 세션**: [Coding Session](2026-09-19_hark_study_notes/site/coding.html) — **문제 21개**(드릴 5 + 레퍼런스 뱅크 16: L0 비트 → L1 메모리 → L2 관용구 → L3 임베디드 C++) · 답안/뼈대 HTML · `make` 채점
> **🎯 마스터 플랜**: [1차 tech session D-7 역산 계획](2026-09-19_hark_study_notes/site/plan/P01_tech_session_master_plan.html) — 매일 여는 페이지
> **📚 스터디 노트**: [2026-09-19_hark_study_notes/site/index.html](2026-09-19_hark_study_notes/site/index.html) — 마스터 플랜 1 + 코딩 연습 5문제(문제·답안·해설) + JD 리서치 15 + 개념 노트 11 + 스터디 노트 6 (총 56편). JD 문장별로 읽으려면 J01~J14, 확인 필요 항목은 J00
> **JD**: https://job-boards.greenhouse.io/hark/jobs/4186968009 · **위치**: San Jose, CA / onsite (Adcock 캠퍼스, 근무형태 JD 미기재 → onsite 추정) · **연봉 밴드**: $120,000 – $300,000 base (+ equity 가능)
> **사용 레쥬메**: `Resume_Firmware_Engineer_2026_Sep_DonHong.pdf`
> 신뢰도: `[확인됨]` 공식/복수 출처 · `[추정]` 단일·2차 출처 또는 추론. 리크루터 공식 안내가 항상 우선.

<!-- 상태 값: 🔍 조사중 → 📨 지원완료 → 📞 리크루터 → 💻 폰스크린 → 🏢 온사이트 → 🎉 오퍼 | ❌ 불합격 | ⏸ 보류 -->

---

## 0. TL;DR

- **채용 상황**: `[확인됨 2026-09-20]` 임베디드 약 5명 추가 채용 중. 오디오·햅틱 펌웨어 공고는 주말 사이 마감(충원 추정), 대신 Audio DSP·특허·통신사 파트너십 법무 자리가 신설 → **1세대 기기가 출시 준비 단계로 이동 중** [추정]
- **회사**: Figure AI 창업자 Brett Adcock가 2025년 말 자기 돈 $100M으로 세운 **"personal intelligence" AI 랩 + 컨슈머 하드웨어 회사**. 자체 multimodal 모델(speech·vision·persistent memory) + 전용 디바이스를 수직 통합으로 만든다. 2026-05 Series A **$700M, post-money $6B** (Parkway 리드, NVIDIA·AMD·Intel·Qualcomm Ventures 참여) [확인됨][1][2][3]. 첫 제품은 2026-08 발표된 웹 브라우징 에이전트 **Handoff**(소프트웨어), 하드웨어는 아직 미공개 [확인됨][5][6]
- **역할**: 첫 세대 컨슈머 디바이스의 **MCU/SoC 펌웨어 스택**(board bring-up, BSP, SPI/I2C/UART/I2S 드라이버, RTOS 스케줄링, 저전력, OTA, factory test/calibration)을 맡고, on-device AI 팀의 모델 추론을 메모리·레이턴시 예산 안에 넣도록 지원 [확인됨][JD]
- **적합도**: ⭐⭐⭐⭐½ (4.5/5) `[2026-09-23 JD 개정 반영]` ~~⭐⭐⭐⭐~~. Cortex-M/R bare-metal C/C++, 버스 bring-up, JTAG/스코프 디버깅, **Apple에서 새 무선 실리콘 bring-up → NPI → MP, factory test-node 설계**가 JD 핵심과 거의 1:1로 맞음. 개정 JD에서 OTA·온디바이스 AI·factory test 항목이 빠지고 **FPGA-based system integration**과 **vendor code integration**이 들어와 매칭이 더 좋아짐. 남은 갭은 **임베디드 OS 폭(eLinux/AOSP/RTOS)과 무선 프로토콜 스택**
- **커리어 방향**: ⭐⭐⭐⭐ (4/5). **on-device AI + 새 AI 하드웨어**를 0→1로 만드는 자리라 피벗 목표(AI accel·on-device·physical AI)에 잘 맞음. 가속기 칩을 직접 설계하는 건 아니지만 "모델을 배터리 기기에 올리는" 제약을 가장 가까이에서 다룸. Adcock 네트워크(Figure 로보틱스)와 연결되는 것도 장점
- **핵심 어필**: ① Apple Wireless 칩셋 통합: 새 실리콘을 출하 플랫폼에 올리며 PCIe/I2C/SPMI/RFFE 장애 root cause 분석. Hark 첫 기기는 **Cellular·Wi-Fi·BT·GNSS·NFC·UWB**가 다 들어가는 웨어러블급 기기 [확인됨][11] ② bring-up → NPI → MP 풀사이클, **factory test-node 아키텍처** 설계 ↔ JD의 "factory test & calibration firmware", "EVT/DVT/PVT" ③ ARM Cortex R8/R82/M0+ pre/post-silicon bring-up, bare-metal C/C++, JTAG·Trace32·스코프·LA·Power Analyzer
- **최대 갭/리스크**: 레쥬메에 **RTOS 이름(FreeRTOS/Zephyr)과 BLE/Wi-Fi 스택**이 없음(레쥬메 키워드 문제 + 실제 갭 일부). 회사 리스크: 하드웨어 미출시 초기 단계, **Adcock식 고강도 문화**(Figure 평판·소송 이력), Apple 입사 약 9개월 만의 이직 설명 필요
- **인터뷰 포맷**: `[확인됨 2026-09-20]` HM 검토 → intro session → tech session → 온사이트. 3단계뿐이라 **tech session이 관문**
- **다음 액션**: ① **즉시 지원** — 자리 5개 + 지원자 없음 + base $250K 구두 안내 = 지금이 레버리지 최대. 미니 프로젝트 완료를 기다리지 말 것 ② 리크루터에 확인: equity 규모·베스팅, 레벨/타이틀, 면접관 이름, 5개 자리의 세부 분야 ③ tech session 대비(라운드가 3개뿐이라 이 한 판이 관문) — J11 RTOS 정직 스크립트 + S01 코딩 드릴

---

## 1. 포지션 요약 (JD 핵심)

| 항목 | 내용 |
|---|---|
| 직함 | **2026-09-21 변경**: `Embedded Software Engineer` → **`Embedded Software Engineer, BSP`** (Greenhouse id 4186968009, 본문 무변경). 같은 날 다른 공고(4201692009, 구 Embedded Application Engineer)가 `Embedded Software Engineer` 이름을 가져감 → 5개 자리를 역할축으로 구분 시작 [확인됨][11] |
| 팀 / 조직 | Firmware 팀으로 추정. Hardware 팀(새 실리콘·센서 통합), **Agent 팀**(모델 실행·메모리 제약), Product 팀과 직접 협업 [확인됨][JD] |
| 레벨 | JD에 레벨 없음. 리크루터가 **base $250K 수준 가능**이라고 구두 안내 (2026-09-20) → 밴드 상단 구간 [확인됨·리크루터]. 타이틀/레벨 매핑은 미확인 |
| 위치 / 근무형태 | San Jose, CA. Adcock의 다른 회사(Figure)와 같은 캠퍼스 [확인됨][4]. 근무형태 미기재, 하드웨어 bring-up 특성상 **onsite 주 5일** 가능성 높음 [추정] |
| 연봉 밴드 / Equity | 공고 base **$120,000 – $300,000** [확인됨][JD]. 리크루터 구두 제시: **base $250K + 사이닝 $100K + equity 약 100(단위 미확인)** [확인됨·리크루터 2026-09-20]. equity가 총액인지 연간인지, 주식 수인지 달러인지 확인 필요. $6B 밸류 비상장 주식 → 유동성 불확실 [추정] |
| 비자 · US Person · 클리어런스 | 요건 없음(컨슈머 제품, ITAR 무관) [확인됨][JD] |

**주요 업무** [확인됨][JD v2 · 2026-09-23 개정]
- ARM SoC와 MCU를 타깃으로 C/C++ 임베디드 펌웨어 개발·유지보수
- **BSP 개발, board bring-up**, 주변장치 드라이버(SPI, I2C, UART, I2S 등), **시스템 통합**
- **벤더와 긴밀히 협업해 시스템 통합·검증**
- **HW/SW 팀과 함께 하드웨어가 스펙대로 제작·동작하는지 확인**
- always-on 배터리 기기의 전력 소모·발열 최적화
- logic analyzer, oscilloscope, JTAG로 복잡한 HW-SW 상호작용 디버깅

인트로 문장(신규): "from **board bring-up, vendor code integrations**, to custom peripheral drivers and integrations, **FPGA-based system integrations**, and system power and performance improvements"

<details>
<summary>구 JD(2026-08-26판)에서 삭제된 항목</summary>

- ~~Build and maintain OTA update infrastructure for reliable field updates~~
- ~~Collaborate with the on-device AI team to support model inference within memory and latency budgets~~
- ~~Develop factory test and calibration firmware for manufacturing~~
- ~~"RTOS task scheduling" 표현~~

</details>

**필수 자격** [확인됨][JD v2]
- **5년 이상**(구 3년) 펌웨어/임베디드 개발
- 자원이 제한된 환경에서 C 및/또는 C++ 능숙
- ARM Cortex-M 또는 Cortex-A 경험 (구 JD의 "associated toolchains" 문구는 삭제)
- **임베디드 OS 실무 경험 — eLinux, AOSP, VxWorks, RTOS 등** (구 JD는 "RTOS (FreeRTOS, Zephyr)"만)
- 무선 프로토콜(BLE, Wi-Fi, Thread) 친숙
- 회로도를 읽고 board bring-up 때 HW 엔지니어와 함께 일하는 데 익숙
- 임베디드 디버깅 툴과 워크플로 경험

**우대 사항** [확인됨][JD v2] — 4개 모두 변경 없음
- 배터리 컨슈머 기기 전력 최적화
- secure boot, firmware signing, hardware root of trust
- 임베디드 ML 추론 런타임 경험
- EVT/DVT/PVT를 거쳐 컨슈머 전자제품 출시

**기술 키워드** (v2): `C` `C++` `ARM Cortex-M/A` `BSP` `board bring-up` `vendor integration` `FPGA-based system integration` `SPI` `I2C` `UART` `I2S` `eLinux` `AOSP` `VxWorks` `RTOS` `BLE` `Wi-Fi` `Thread` `low power` `thermal` `JTAG` `logic analyzer` `secure boot` `EVT/DVT/PVT`

---

## 2. 회사 분석

### 2.1 무엇을 하는 회사인가
"proactive, multimodal, persistent memory" 개인 AI를 만들고, 그걸 담을 **전용 하드웨어를 모델·SW·인터페이스와 같이 설계**하는 수직 통합 AI 랩 [확인됨][1][3]. Adcock: "Today's AI models … feel quite dumb … a world that looks more like Jarvis or Her" [확인됨][4]. 목표는 "사람과 기계 사이의 universal interface" [확인됨][JD].

### 2.2 제품 · 고객 · 비즈니스 모델
- **모델**: 2026년 여름 첫 multimodal 모델 발표 예정, **speech와 memory** 중심 [확인됨][2][7]
- **Handoff** (2026-08-05 technical preview): 사용자 대신 실제 웹사이트(DoorDash, OpenTable, LinkedIn 등)를 조작하는 브라우저/컴퓨터 사용 에이전트. Online-Mind2Web 97.7점 주장(자체 평가 포함이라 논란 있음), 경쟁 모델보다 토큰 단가 1/10 이하 주장. 세션마다 전용 VM. "end of summer" 출시 목표 [확인됨][5][6]
- **하드웨어**: "a family of AI devices, for yourself and for the home", 스마트폰·웨어러블·스마트글래스와 다른 폼팩터 [확인됨][10]. 채용 공고로 본 단서:
  - RF Antenna 공고: "**first-generation consumer wearable/device**", **Cellular, Wi-Fi, BT, GNSS, NFC, UWB**를 모두 담는 기기, EVT/DVT/PVT [확인됨][11]
  - System Test 공고: "Working knowledge with **Qualcomm chipsets**", RTOS/Linux/**Android** [확인됨][11]
  - On-Device AI 공고: "**Hexagon DSP, Ambiq-class MCUs**", INT8/INT4, speech 추론 [확인됨][11]
  - Embedded Application 공고: 기기 위 메인 앱이 **Android 기반** [확인됨][11]
  - Power 공고: Qualcomm/TI/Nordic/Maxim PMIC, Li-ion, USB-C PD/무선 충전 [확인됨][11]
  - → **구조 추정**: Qualcomm SoC(Cortex-A, Android) + 상시 켜져 있는 저전력 MCU(Ambiq Apollo 계열 Cortex-M, RTOS) 조합. 이 포지션은 **MCU 쪽 RTOS 펌웨어 + SoC BSP 일부**일 가능성이 큼 [추정]
- **비즈니스 모델**: 컨슈머 구독 + 하드웨어 판매 추정. 아직 매출 없음 [추정]

### 2.3 단계 · 펀딩 · 규모
| 설립 | 펀딩 총액 / 최근 라운드 | 밸류에이션 | 주요 투자자 | 인원 | HQ |
|---|---|---|---|---|---|
| 2025년 말 (2026-03-24 stealth 해제) [확인됨][3][12] | Adcock 개인 $100M + **Series A $700M+** (2026-05-21) [확인됨][1][2] | **$6B post-money** [확인됨][1][2] | Parkway VC(리드), NVIDIA, AMD Ventures, Intel Capital, **Qualcomm Ventures**, ARK, Brookfield, Salesforce Ventures, Greycroft, Prime Movers Lab 등 [확인됨][1][2] | 45명(2026-03) → 약 70명(2026-05) [확인됨][4][2]. 현재 공고 57개 오픈 [확인됨][11] | San Jose, CA [확인됨][11] |

- 컴퓨트: NVIDIA B200 데이터센터 운영 [확인됨][2], NVIDIA와 gigawatt급 Vera Rubin 다년 파트너십 [추정·2차 출처][6]

### 2.4 최근 뉴스 (최근 12개월)
- 2025 말: Adcock가 $100M 개인 자금으로 Hark 설립 [확인됨][3]
- 2026-03-24: stealth 해제. Apple iPhone Air 디자인 리드 **Abidur Chowdhury**를 Director of Design으로 영입, "family of AI devices" 계획 발표 [확인됨][4][10]
- 2026-05-21: $700M Series A, $6B [확인됨][1][2]
- 2026-08-05: 첫 제품 **Handoff** technical preview, waitlist 오픈 [확인됨][5]
- 2026-09 현재: 하드웨어 공개 전. FW·RF·전력·오디오·햅틱·열 설계 공고가 대거 열려 있음 → **1세대 기기 EVT 전후 단계**로 추정 [추정][11]

### 2.5 경쟁사 · 시장 포지션
- **OpenAI**(io/Jony Ive 디바이스, 화면 없는 AI 기기, 2027 초 예정 보도) — 가장 직접적인 경쟁 [확인됨][1]
- Meta(Ray-Ban AI 글래스), Apple/Google(폰 기반 에이전트), Humane(실패)·Rabbit(부진) 선례, Amazon Alexa+
- 차별점: 자체 frontier 모델 + 하드웨어 + 막대한 자금 + 스타 디자이너. 약점: Humane/Rabbit 사례처럼 **AI 전용 기기의 시장성 자체가 아직 증명 안 됨** [추정]

### 2.6 엔지니어링 문화 · 평판 · 보상
- Hark 자체의 Glassdoor/Blind 후기는 거의 없음 (회사가 너무 신생) [확인됨·검색 결과 기준]
- **Adcock 회사(Figure) 평판** 참고: 보상은 강하지만 **장시간·고강도 근무**가 꾸준한 단점으로 언급됨, 면접관도 바빠 보인다는 후기. Glassdoor 인터뷰 긍정 36.8% [추정][8][9]
- Figure 전 안전 책임자가 2025-11 부당해고 소송 제기(안전 우려 묵살 주장, 회사는 부인) → **리더십이 속도를 강하게 우선시한다**는 신호로 참고 [확인됨][13]
- 팀 구성: Apple, Meta, Google, Tesla 출신 [확인됨][4][12] → Apple 출신 Don에게는 문화적 연결고리
- 보상: 이 JD base $120K–$300K [확인됨][JD]. 비교: On-Device AI Inference Engineer $200K–$450K, Tech Lead $300K–$500K [확인됨][11]

### 2.6.1 Hiring Manager 추정 [추정 2026-09-21]
- 지인 경로로 파악. 리크루터는 아직 이름을 밝히지 않았고, 힌트로 추정한 인물은 **Xiao Qin** (LinkedIn `in/xqinx`) [추정]
- 경력: Fitbit **Sr. Embedded Linux → Staff Firmware Engineer**(2017~2021) → Google **Pixel Watch**(2021~2022) → **X, the moonshot factory** 로봇(2022~2023) → **Sesame** Engineer(2024~, 프로필 기준 현재) · Acomni 공동창업(스마트 온도조절기, 2012~2017) · **애리조나대 ECE 박사 — 임베디드 + ML** [확인됨·LinkedIn 공개 프로필]
- 프로필상 소속이 아직 Sesame다. Hark 이직 시 미갱신일 수 있어 **본인 확인 전까지 추정**으로 둔다
- 앞서 들은 "Sesame와 Oculus 창업자"는 **Sesame가 Oculus 창업자들이 세운 회사**라는 뜻으로 전달된 것으로 보인다 [추정]
- 시사점: JD의 always-on 배터리·OTA·"model inference within memory and latency budgets" 항목은 이 사람의 실제 이력과 정확히 겹친다. **배터리 웨어러블 + 온디바이스 AI + 오디오**가 면접의 무게 중심일 가능성이 높다. 반대로 Don의 차별점은 이 팀에 흔치 않은 **새 실리콘 bring-up · 신호/버스 레벨 디버깅 · factory test 체계**다

### 2.7 이 포지션이 실제로 할 일 (추론)
- 1세대 기기의 **always-on MCU** 펌웨어: 센서 허브, 마이크/오디오 전처리(I2S), wake word 파이프라인 연결, 전원 상태 관리(sleep/deep-sleep), SoC와의 IPC [추정]
- **BSP & 드라이버**: 새 보드(EVT)마다 bring-up, PMIC/fuel gauge/센서/햅틱 드라이버, 회로도 보면서 HW팀과 디버깅 [추정]
- **무선**: BLE(폰 페어링·provisioning), Wi-Fi 연결 관리 — 칩셋 벤더 SDK 통합 수준 [추정]
- **OTA + secure boot**: MCU·SoC 이미지 A/B 업데이트, 서명 검증 (Embedded DevOps 공고에 A/B, delta update 언급) [추정][11]
- **Factory**: 양산 라인용 test firmware, 센서/마이크 calibration, 결과 로깅 — Don의 Apple factory test-node 경험과 가장 잘 맞는 부분 [추정]
- 초기 팀이라 범위가 넓고 소유권이 큼 = 성장 기회이자 업무량 리스크

### 2.8 리스크
- **실행 리스크**: AI 전용 하드웨어 시장은 Humane·Rabbit이 실패한 영역. 하드웨어 일정 지연 가능성 [추정]
- **문화 리스크**: Adcock 스타일의 고강도·빠른 템포. 워라밸 기대치는 낮게 [추정][8]
- **보상 리스크**: base 밴드가 넓음(하단 $120K). Apple 현재 수준 이상인지 확인 필요. $6B 밸류 비상장 equity → 상승 여력은 있지만 희석·유동성 불확실
- **커리어 스토리 리스크**: Apple 2025-12 입사 → 1년 미만 이직 → "Why leave Apple so soon?" 대비 필수
- 장점: 자금이 매우 넉넉함($800M+), 최소 수년 runway [추정]

---

## 3. 적합도 분석 (Don ↔ JD)

### 3.1 요구사항 매칭표
| JD 요구사항 | Don 경험 (레쥬메 근거) | 매칭 |
|---|---|---|
| 3+ yrs firmware/embedded | 2021~ SSD FW(Senior→Staff) + Apple Embedded Systems, 앞선 3년 SW 포함 약 8년 | ✅ 강함 |
| 자원 제한 환경 C/C++ | "Developed using embedded C and C++ programming on bare-metal (pre/post-silicon)" | ✅ 강함 |
| ARM Cortex-M/A + 툴체인 | "ARM Cortex R8/R82/M0+ … FW bring-up", Xtensa, Trace32 | ✅ (Cortex-A/Linux는 약함) |
| RTOS (FreeRTOS/Zephyr) | bare-metal·SSD 자체 스케줄러 중심. 상용 RTOS 이름이 레쥬메에 없음 | 🟡 부분·전이 가능 |
| BLE / Wi-Fi / Thread | Apple **RF-Hardware(Wireless) Chipset Integration** — 무선 칩셋 HW/인터페이스 수준. 프로토콜 스택 경험은 없음 | 🟡 부분 |
| 회로도 읽기 + board bring-up | "Silicon/system bring up -> NPI -> MP", "SoC verification … I2C, SPI, DMA, PCIe, SRAM/DRAM bring-up" | ✅ 강함 |
| 임베디드 디버깅 툴 | "JTAG, Oscilloscope, Logic Analyzer, Power Analyzer", "DSOs and protocol Analyzer", Trace32 | ✅ 강함 |
| 주변장치 드라이버 SPI/I2C/UART/I2S | I2C/SPI/UART/SPMI/RFFE ✅, **I2S/오디오 없음** | 🟡 부분 |
| 전력·발열 최적화 | "sign off on hardware safety margins (reliability vs performance/power)", Power Analyzer | 🟡 부분 |
| OTA 업데이트 인프라 | 레쥬메 명시 없음 (SSD FW download/commit·NVMe 흐름은 전이 가능) | 🟡 부분 |
| AI 팀 추론 지원 (메모리/레이턴시) | "firmware performance tuning", 성능 중심 NVMe 기능. ML 런타임 경험 없음 | ❌ 갭 (우대 사항) |
| Factory test & calibration FW | "designing and leading **factory test-node architecture**", "SSD/MFG Firmware" | ✅ 강함 |
| HW-SW 상호작용 디버깅 | "root-cause analysis of fundamental and interface level failures … when a new chip meets the full HW/SW system" | ✅ 강함 |
| 우대: secure boot / signing | 레쥬메 명시 없음 (엔터프라이즈 SSD FW에서 흔함 → 경험 있으면 추가) | 🟡 확인 필요 |
| 우대: EVT/DVT/PVT 출시 | Apple bring-up → NPI → MP | ✅ 강함 |

### 3.2 강점 — 어필 포인트
1. **"새 실리콘이 전체 시스템을 만날 때"의 root cause 분석** ↔ JD "work directly with the hardware team on new silicon and sensor integrations" + "Debug complex hardware-software interactions". 1세대 기기에서 가장 필요한 역량.
2. **무선 칩셋 통합 도메인** ↔ Hark 1세대 기기의 Cellular/Wi-Fi/BT/GNSS/NFC/UWB 멀티 라디오 [11]. 프로토콜 스택은 아니어도 PCIe/SPMI/RFFE 제어 경로, coexistence·통합 이슈를 아는 FW 엔지니어는 드묾.
3. **NPI → MP + factory test-node 아키텍처** ↔ "factory test and calibration firmware", "EVT/DVT/PVT". 스타트업은 이 경험이 약한 경우가 많아 차별화 포인트.
4. **Pre-silicon/FPGA bring-up + Cortex-R/M bare-metal** ↔ BSP·드라이버를 처음부터 올려본 경험.
5. **성능·에러 처리·telemetry를 양산 FW에 넣은 경험** (NVMe telemetry 디버그 기능, error reporting) ↔ OTA로 운영되는 fleet의 observability·신뢰성.

### 3.3 갭 & 보완 전략
| 갭 | 인터뷰 전 할 일 | 답변 프레이밍 |
|---|---|---|
| 상용 RTOS (FreeRTOS/Zephyr) | FreeRTOS 핵심(task/queue/semaphore/mutex, priority inversion, tickless idle) 정리 + Zephyr 샘플을 nRF52840 DK로 실행(devicetree, Kconfig, west) | "SSD FW에서 멀티코어 task 스케줄링·ISR·동기화를 직접 다뤘다. RTOS API는 그 위의 추상화" + 사이드 프로젝트 언급 |
| BLE / Wi-Fi 스택 | BLE GAP/GATT, advertising/connection interval과 전력의 trade-off, Wi-Fi power save(DTIM), provisioning 흐름 1장 요약 | "Apple Wireless 칩셋 통합 그룹 — 라디오를 호스트에 붙이는 HW/FW 경계를 매일 디버깅했다" |
| I2S / 오디오 경로 | I2S 프레임(BCLK/LRCLK), DMA ping-pong 버퍼, PDM 마이크 개념 | DMA·링버퍼 경험(SSD 데이터 경로)으로 연결 |
| ML 추론 런타임 | TFLite Micro / CMSIS-NN 튜토리얼 1개(keyword spotting) — Ambiq Apollo나 nRF에서 실행 | "메모리 맵·SRAM 예산·DMA 스케줄링을 엄격히 관리해 본 경험이 모델 residency 문제와 같은 종류" |
| OTA / secure boot | MCUboot(A/B 슬롯, 이미지 서명, rollback) 구조 학습 | SSD FW 업데이트/검증 흐름이 있으면 그것과 비교해서 설명 |
| 저전력 | sleep mode, clock gating, 전류 프로파일링(Nordic PPK2) 기초 | Power Analyzer 사용 경험 + Apple 전력/성능 margin sign-off |

### 3.4 종합 평가 · 커리어 방향 정합성
- **적합도 ⭐⭐⭐⭐ (4/5)**: 필수 요건 7개 중 5개 ✅, 2개(RTOS·무선 프로토콜)는 🟡 + 레쥬메 표현 보강으로 상당 부분 커버 가능. 우대 사항 중 EVT/DVT/PVT는 강점. 3+ yrs 요구 대비 경력이 많아 **레벨·밴드 상단 협상** 여지가 있음.
- **커리어 방향 ⭐⭐⭐⭐ (4/5)**: on-device AI 제품을 0→1로 만드는 자리. "모델 실행 + 메모리 제약"을 AI 팀과 직접 다루므로 이후 **On-Device AI Inference** 쪽으로 옮겨 갈 수 있는 경로가 보임(같은 회사 공고 존재 [11]). AI accel 칩 설계와는 한 단계 떨어짐.
- **대안 포지션**(같은 회사, 참고): **System Test & Validation Engineer**(Qualcomm, 전력·레이턴시 프로파일링 — Apple 업무와 거의 같음), Audio/Haptics Firmware. 1순위는 이 Embedded SWE 유지 권장.

### 3.5 레쥬메 튜닝 제안 (이 JD용)
- Summary에 키워드 추가: `Embedded C/C++, ARM Cortex-M/R, RTOS (FreeRTOS/Zephyr), BLE/Wi-Fi chipset integration, board bring-up, low-power, factory test/calibration, JTAG/Trace32` (RTOS는 사이드 프로젝트를 한 뒤에만 쓸 것)
- Apple bullet을 이렇게 다듬기:
  - "Integrated new wireless (Wi-Fi/BT) silicon into shipping Apple consumer platforms, owning root-cause of host-interface failures (PCIe, I2C, SPMI, RFFE) from first power-on through EVT/DVT/PVT and mass production."
  - "Architected factory test-node flows and stress scenarios that caught latent silicon/integration defects before MP, balancing reliability against power and performance margins."
- SSD bullet 추가·수정:
  - "Brought up firmware on ARM Cortex-R8/R82/M0+ cores and peripheral IP (I2C, SPI, DMA, SRAM/DRAM) on FPGA pre-silicon, then carried drivers to production silicon."
  - (해당 경험이 있다면) "Implemented firmware image update / signature verification flow in production SSD firmware." → OTA·secure boot 우대 사항에 대응

---

## 4. 예상 인터뷰

### 4.1 프로세스
```
[확인됨 — 리크루터 안내 2026-09-20]
지원 → HM 검토 → ① intro session (1회)
                 → ② tech session (1회)
                 → 통과 시 ③ 온사이트
```
- 라운드 수가 적다(3단계). 즉 **tech session 하나가 사실상 관문**이다.
- 온사이트 구성·기간은 미확인 [추정: Figure 패턴이면 반나절 루프, 전체 2~3주]
- 추가 확인됨: **임베디드 엔지니어를 약 5명 더 채용 중**, 공고는 "지난주 수요일에 올렸고 아직 지원자가 없다"고 안내받음 [확인됨·리크루터]
- 다만 Greenhouse 보드 기준으로 이 공고(id 4186968009)는 **8월 26일 이전부터 게시되어 있었다**. 9월 20일 갱신 타임스탬프는 56개 공고가 전부 동일해 보드 전체 일괄 갱신이다. 즉 "지난주 수요일 게시"는 LinkedIn 재게시이거나 req 재오픈/헤드카운트 추가를 뜻할 가능성이 크다 [추정]
- Figure 후기: 평균 약 13일, 리크루터를 건너뛰고 엔지니어/HM으로 바로 가기도 함, **case study 발표**가 특징 [추정][8][9]

### 4.2 실제 보고된 질문
- Hark 펌웨어 인터뷰 후기는 아직 찾지 못함 (2026-09-18 기준).
- 참고 [추정][9]: Figure 기술 1라운드 약 30분 개념 질문(코딩 없음), 이후 30분 기술 2회 + 3인 패널 case study("카메라 브래킷 설계" 같은 설계 사고 문제).

### 4.3 JD 기반 예상 기술 질문
| 카테고리 | 질문 | 왜 나올지 (JD 근거) |
|---|---|---|
| C/C++ 코딩 | ISR과 task 사이 lock-free SPSC ring buffer 구현 (head/tail, 경계, `volatile` vs memory barrier) | 드라이버·RTOS·오디오 스트리밍 |
| C/C++ 코딩 | 비트 조작: 레지스터 필드 read-modify-write 매크로, 비트 카운트, endian 변환 | BSP·주변장치 드라이버 |
| C/C++ 코딩 | 고정 크기 memory pool allocator (malloc 없이) | "resource-constrained", 모델 메모리 예산 |
| C/C++ 코딩 | 센서 스트림 moving average / debounce / 패킷 파서 FSM | 센서 통합, UART 프로토콜 |
| 임베디드 개념 | `volatile`의 의미와 한계, ISR에서 하면 안 되는 일, critical section 구현 | 기본기 |
| 임베디드 개념 | 부트 시퀀스: reset vector → startup → `main`, 링커 스크립트(.data/.bss), vector table | BSP |
| 임베디드 개념 | RTOS: priority inversion과 priority inheritance, mutex vs semaphore, stack overflow 감지, tickless idle | RTOS task 스케줄링 |
| 버스 / 프로토콜 | I2C clock stretching, NACK, 버스 hang 복구(SCL 9 pulse) / SPI mode 0~3 / I2S 프레임 구조 | SPI, I2C, UART, I2S |
| 버스 / 프로토콜 | BLE: advertising vs connection, connection interval·latency와 전력, GATT 구조 | 무선 요건 |
| 도메인 (저전력) | always-on 기기에서 평균 전류 1mA를 줄이는 방법? (sleep states, clock gating, DMA, wake source, 폴링 제거) | 전력·발열 최적화 |
| 도메인 (OTA) | 배터리가 끊겨도 벽돌이 되지 않는 OTA 설계: A/B 슬롯, 서명 검증, rollback, bootloader 역할 | OTA + secure boot |
| 도메인 (AI) | MCU에서 wake word 모델 실행: SRAM 배치, DMA double buffering, INT8 양자화, NPU/DSP로 offload | on-device AI 협업 |
| 도메인 (Factory) | 양산 라인 factory test 펌웨어 설계: 테스트 모드 진입, 센서 calibration 저장(OTP/flash), 결과 로깅, 시간 제약 | factory test & calibration |
| 디버깅 시나리오 | "EVT 보드 100대 중 5대가 I2C 센서를 못 읽는다" — 어떻게 좁혀 가나? (전원 시퀀스, pull-up, 주소 충돌, LA 캡처, 온도) | HW-SW 디버깅 |
| 디버깅 시나리오 | 필드에서 드물게 hard fault — 재현 안 됨. crash dump(CFSR/HFSR, stack), watchdog, telemetry 설계 | 현장 운영 |
| 시스템 설계 | SoC(Android) + always-on MCU 기기의 전원/wake 아키텍처와 IPC 설계 | Hark 기기 구조 [추정] |

### 4.4 Don 경험 딥다이브 (레쥬메 bullet별)
| 레쥬메 bullet | 예상 꼬리질문 | STAR 소재 |
|---|---|---|
| Apple: 새 실리콘 통합, PCIe/I2C/SPMI/RFFE root cause | 가장 어려웠던 인터페이스 버그는? 어떻게 HW 문제와 FW 문제를 구분했나? | 특정 버스 장애 1건: 증상 → 가설 → 측정(DSO/프로토콜 분석기) → 원인 → 수정 → 재발 방지 |
| Apple: bring-up → NPI → MP, factory test-node 설계 | test-node는 무엇을 검사하나? 커버리지와 테스트 시간을 어떻게 맞췄나? 양산에서 뭘 잡았나? | 스트레스 시나리오로 latent defect를 MP 전에 잡은 사례 |
| Apple: 신뢰성 vs 성능/전력 margin sign-off | margin 기준은 어떻게 정했나? 전력 측정은 어떻게 했나? | 전력/성능 trade-off 결정 1건 |
| Solidigm: Cortex R8/R82/M0+ FPGA bring-up | 첫 부팅이 안 될 때 제일 먼저 보는 것? pre-silicon과 post-silicon 차이? | FPGA 이미지에서 부팅 실패를 디버깅한 사례 |
| Solidigm: error reporting/handling 설계 | 에러 분류, 복구 전략, 로깅 오버헤드는? | 양산 FW 에러 처리 체계 → OTA fleet observability로 연결 |
| SK hynix: NVMe telemetry 디버그 기능 | 무엇을 기록했고 저장 공간·성능 영향은? | 고객(MS/DELL/HPE) 요구사항 기한 맞춘 출시 |
| SK hynix: shmoo·health monitoring (SI 협업) | shmoo 결과를 FW 설정에 어떻게 반영했나? | 고속 인터페이스 margin 튜닝 |

### 4.5 행동 · 동기 질문
- **Why Hark?** → "AI가 채팅창을 벗어나 기기에 들어가는 첫 세대를 만들고 싶다. 모델·HW·FW를 한 팀에서 같이 설계하는 곳은 드물다. 내 경력은 새 실리콘을 실제 제품으로 만드는 일이었고, 그걸 on-device AI에 쓰고 싶다."
- **Why leave Apple (9개월 만에)?** → 부정적 이유 대신 방향으로 설명: "Apple에서 무선 칩셋 통합을 하며 bring-up→MP를 끝까지 봤다. 다음 단계로 AI가 들어간 제품의 **펌웨어 전체를 소유**하고 싶고, 대기업에서는 그 범위가 나뉘어 있다." (현재 프로그램 사이클 진행 상황과 연결)
- **고강도 환경 괜찮은가?** → SSD 고객 기한(MS/Meta/Google) 대응 경험 + 솔직한 기대치
- **모호함 속에서 결정한 경험**, **HW팀과 의견 충돌**, **실패한 프로젝트와 배운 점**

### 4.6 Tell me about yourself (영어 초안, 60~90초)
> I'm an embedded firmware engineer, and for about seven years my work has been taking new silicon and making it ship. At SK hynix and Solidigm I wrote production firmware in C and C++ for enterprise SSDs — bringing up ARM Cortex-R and Cortex-M cores on FPGA before tape-out, integrating peripherals like I2C, SPI and DMA, and shipping features like telemetry and error handling for customers such as Microsoft, Google and Meta. Since last December I've been at Apple in the wireless chipset integration group, where I own root-causing interface failures — PCIe, I2C, SPMI, RFFE — when a new radio chip meets the full system, and I designed the factory test architecture that catches latent defects before mass production. What draws me to Hark is that you're building the model, the hardware and the firmware together for a first-generation device. That is exactly the kind of work I want: owning the firmware from board bring-up to factory to the field, and working with the AI team to make on-device intelligence actually fit inside the power and memory budget.

### 4.7 역질문
1. 1세대 기기의 컴퓨트 구조는 어떤가요? (SoC + always-on MCU 분리인지) 이 역할은 어느 쪽을 주로 맡나요?
2. 지금 하드웨어는 어느 단계인가요 (proto/EVT)? 첫 양산까지 펌웨어 팀의 가장 큰 리스크는 무엇인가요?
3. 펌웨어 팀 규모와 구성, 그리고 on-device AI 팀과는 어떤 인터페이스로 일하나요 (메모리 예산 협상 등)?
4. RTOS·OTA·secure boot는 이미 정해졌나요, 아니면 이 역할이 결정하나요?
5. 입사 후 90일 동안 성공이란 무엇인가요? 그리고 팀의 평소 근무 템포는 어떤가요?

### 4.8 준비 체크리스트
- [ ] **J00 확인 필요 항목 72건 채우기** (우선 5건: factory test-node 성격 · SSD 스케줄러 방식 · FW 서명 경험 · Trace32 사용 범위 · 현재 TC)
- [ ] 스터디 노트 C00 로드맵의 2주 계획대로 진행 (`2026-09-19_hark_study_notes/site/index.html`)
- [ ] 레쥬메 튜닝(3.5) 후 지원 — RTOS·BLE는 사이드 프로젝트를 한 뒤에만 기재
- [ ] Apple 9개월 이직 사유 + Why Hark 스토리 1분 버전
- [ ] STAR 3개 영어로 다듬기: 인터페이스 root cause / factory test-node / FPGA bring-up
- [ ] C 코딩 드릴: ring buffer, bit manipulation, memory pool, FSM — `../anduril_firmware_engineer/`(01_bit_basics, 02_bit_advanced, 04_memory, 05_circular_buffer, 07_fsm, 09_rtos_embedded) 재사용
- [ ] 동시성 기초: `../verkada_sr_embedded_linux_engineer_connectivity/verkada_concurrency_top10.md` 중 SPSC queue·double buffering 재사용
- [ ] RTOS 1장 요약(FreeRTOS + Zephyr): scheduling, priority inversion, ISR→task 전달, tickless idle
- [ ] 미니 프로젝트: nRF52840 DK + Zephyr — BLE 센서 advertising + deep sleep 전류 측정 + MCUboot OTA (1주)
- [ ] BLE/Wi-Fi 저전력 1장 요약 (connection interval, DTIM, coexistence)
- [ ] OTA/secure boot 1장 요약 (A/B, rollback, 서명, root of trust)
- [ ] TFLite Micro keyword spotting 예제 실행 (on-device AI 대화용)
- [ ] 디버깅 시나리오 2개 답변 연습 (I2C 일부 보드 실패, 필드 hard fault)
- [ ] 보상 기준선: Apple 현재 TC 정리 → 밴드 상단 + equity 협상 기준 설정

---

## 5. 진행 로그

| 날짜 | 이벤트 | 메모 |
|---|---|---|
| 2026-09-18 | 컨텍스트 파일 생성 (JD·회사 조사) | Greenhouse API로 JD 원문 확보(JD 갱신일 2026-08-26). 57개 공고 스캔해 하드웨어 구조 단서 수집 |
| 2026-09-19 | 스터디 노트 사이트 생성 | `2026-09-19_hark_study_notes/` — C00~C10 개념 + S01~S06 드릴, 약 14,600줄. 빌드: `python3 build_notes_site.py` |
| 2026-09-29 | 리크루터 답장 | "apologies for the delay, should be hearing back this week. Im speaking to them later on this week due to them being OOO" → 침묵 원인은 팀 OOO. 이번 주 회신 예정. 재촉 대신 대기 |
| 2026-09-30 | **코딩 레퍼런스 뱅크 + 스토리 예시** | `coding/` 문제 06~21 추가(L0~L3, 총 21문제 · 27,000줄 · 전부 경고 0 테스트 통과, L0·L1은 ASan/UBSan까지) · `study/S07` 스토리 A·B·C 예시(지어낸 견본, 6단 뼈대) · 빌더가 C++ 답안도 HTML 렌더 |
| 2026-09-30 | 7일 계획 작성 | `plan/P06` — D1 기준 맞추기(J00 3건) → 스토리 A/C → BSP 해부 → **D5 Zephyr 실습 반나절** → OS 갭 방어 → 리허설 |
| 2026-09-29 | **노트 감사 + BSP 시리즈 확장** | `plan/P05` 감사(현행 JD 24줄 ↔ 노트 커버리지, 낡은 인용 32건, 과잉 주장 6건) · 신규 `bsp/B05`(벤더·FPGA 통합 856줄) `bsp/B06`(임베디드 OS 지형도 702줄) · OTA/AI/factory 6개 노트에 하향 배너 · **S06의 가짜 JD 인용문 제거** · B03 §8.1 낡은 결론 재작성 |
| 2026-09-29 | 4일 스프린트 작성 | `plan/P04` — 하루 2시간 × 4일. OTA·AI·factory 버리고 디버깅·BSP·벤더/FPGA·코딩 2문제로 압축 |
| 2026-09-21 | **지원 제출** | 제목이 BSP로 바뀐 날 Greenhouse로 지원 |
| 2026-09-23 | 임베디드 7자리 비교 분석 | `plan/P03` — Embedded Software 팀이 BSP/Systems/Platform/UX/DevOps 5축으로 확정. 밴드 3형제 동일($120–300K), 리크루터 제시 $250K는 상단 구간 |
| 2026-09-23 | **JD 본문 전면 개정** (12:38 ET) | OTA·온디바이스AI·factory test 삭제 · vendor 통합·FPGA 통합·board bring-up 신설 · 3+→5+ yrs · RTOS→eLinux/AOSP 포함. 적합도 ⭐4 → ⭐4.5. 분석: `plan/P02` · 같은 날 On-Device AI Inference Engineer → Embedded AI Engineer로 개편 |
| 2026-09-22 | 임베디드 팀 공고 재편 | ESE(시스템)·ESE Platform(구 OS Architect)·Frontier UX 본문 재작성. 네 공고가 모두 "The Embedded Software team owns..."로 시작 → **팀 구조 = BSP / Platform / Systems / UX** |
| 2026-09-21 | **공고 제목 BSP로 변경** + BSP 시리즈 5편 작성 | 제목만 변경, 본문 동일. `bsp/B00~B04` 약 2,900줄 — 공식 문서 28건 확인. 마스터 플랜 D-5/D-4/D-1을 BSP 중심으로 교체 |
| 2026-09-21 | 코딩 세션 HTML 인덱스 | `site/coding.html` — 문제·해설·답안·뼈대 링크 한 페이지. 답안 .c도 HTML로 렌더 |
| 2026-09-21 | 코딩 연습 세트 5문제 | `coding/` — problems·starters·solutions·notes + Makefile. 모범답안 5개 전부 경고 0개·테스트 통과 (`make all`) |
| 2026-09-21 | 마스터 플랜 작성 | `plan/P01` — tech session D-7 역산 계획. HM 추정(Xiao Qin, Fitbit/Pixel Watch/Sesame) 반영, 오디오·온디바이스 AI 우선순위 상향 |
| 2026-09-20 | 리크루터 콜 | 임베디드 약 5명 추가 채용 · 지원자 아직 없음(리크루터 말) · 프로세스 intro → tech → 온사이트 · **base ~$250K 가능** · 오디오/햅틱 FW 공고는 주말 사이 내려감(채용 종료 추정), Audio DSP·Patent Agent·Carrier Partnerships 법무 신규 게시 |
| 2026-09-20 | JD 리서치 노트 15편 추가 | `jd/J01~J14` — JD 문장별 해석·실무·면접 질문 약 200문항. `jd/J00`에 확인 필요 항목 72건 수집 |

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

1. https://siliconangle.com/2026/05/21/hark-raises-700m-build-personalized-intelligence-devices/ — Series A $700M, 투자자, 70명, B200, OpenAI 경쟁 (확인일 2026-09-18)
2. https://techcrunch.com/2026/05/21/hark-raises-700m-series-a-for-its-secretive-universal-ai-interface/ — $700M/$6B, 투자자, 70명, 모델→하드웨어 순서 (확인일 2026-09-18)
3. https://www.intelcapital.com/hark-raises-700m-series-a-at-a-6b-valuation/ — Intel Capital 공식 발표, 수직 통합 전략, Adcock 인용 (확인일 2026-09-18)
4. https://techcrunch.com/2026/03/24/meet-the-former-apple-designer-building-a-new-ai-interface-at-hark/ — Chowdhury 영입, 45명, 같은 캠퍼스, Adcock 인용 (확인일 2026-09-18)
5. https://www.businesswire.com/news/home/20260805041028/en/Hark-Announces-Handoff-the-Worlds-Best-Web-Browsing-AI-Agent — Handoff 발표 (2026-08-05) (확인일 2026-09-18)
6. https://alphasignal.ai/news/figure-ai-s-brett-adcock-bets-gigawatt-nvidia-deal-on-hark-handoff — NVIDIA Vera Rubin 파트너십, Handoff 벤치마크 논란 (2차 출처) (확인일 2026-09-18)
7. https://theaiinsider.tech/2026/03/26/hark-unveils-vision-for-integrated-ai-models-and-hardware-to-redefine-personal-intelligence/ — 모델·하드웨어 통합 비전 (확인일 2026-09-18)
8. https://dataford.io/interview-guides/figure-ai/software-engineer — Figure 인터뷰 프로세스·문화 (2차 출처) (확인일 2026-09-18)
9. https://www.glassdoor.com/Interview/Figure-AI-Interview-Questions-E9642582.htm — Figure 인터뷰 후기 (확인일 2026-09-18)
10. https://www.bloomberg.com/news/articles/2026-03-24/figure-ai-founder-s-new-startup-hark-is-latest-to-plan-family-of-ai-devices — "family of AI devices" (확인일 2026-09-18)
11. https://boards-api.greenhouse.io/v1/boards/hark/jobs?content=true — Hark 전체 공고 57개 (RF Antenna, System Test, On-Device AI, Power, Embedded DevOps 등) (확인일 2026-09-18)
12. https://www.businesswire.com/news/home/20260324789327/en/Hark-Launches-AI-Lab-Building-Futuristic-Interface-to-Artificial-Intelligence — stealth 해제 공식 발표 (확인일 2026-09-18)
13. https://www.cnbc.com/2025/11/21/figure-ai-sued.html — Figure 전 안전 책임자 소송 (확인일 2026-09-18)

---

## 부록 A. JD 원문 (2026-09-18 수집, Greenhouse updated_at 2026-08-26)

<details>
<summary>펼치기</summary>

**Embedded Software Engineer** — San Jose

**About Hark**

Hark is an artificial intelligence company building advanced, personalized intelligence. One that is proactive, multimodal, and capable of interacting with the world through speech, text, vision, and persistent memory.

We're pairing that intelligence with next-generation hardware to create a universal interface between humans and machines. While today's AI largely operates through chat boxes and decade-old devices, Hark is focused on what comes next: agentic systems that interact naturally with people and the real world.

To get there, we're developing multimodal models and next-generation AI hardware together - designed from the ground up as a single, unified interface for a new era of intelligent systems.

**About the Role**

You'll own critical pieces of the firmware stack that powers Hark's consumer products — from board bring-up and peripheral drivers to the runtime environment that hosts on-device intelligence. This isn't firmware in a vacuum. You'll work directly with the hardware team on new silicon and sensor integrations, with the agent team on model execution and memory constraints, and with products on experiences that ship to real users. The problems are real, the constraints are tight, and the work matters immediately.

**Responsibilities**

- Develop and maintain embedded firmware in C/C++ targeting ARM-based SoCs and microcontrollers
- Own BSP development, peripheral driver integration (SPI, I2C, UART, I2S), and RTOS task scheduling
- Optimize power consumption and thermal performance for always-on, battery-powered operation
- Build and maintain OTA update infrastructure for reliable field updates
- Collaborate with the on-device AI team to support model inference within memory and latency budgets
- Develop factory test and calibration firmware for manufacturing
- Debug complex hardware-software interactions using logic analyzers, oscilloscopes, and JTAG

**Requirements**

- 3+ years of professional firmware or embedded systems development
- Strong proficiency in C and/or C++ in resource-constrained environments
- Experience with ARM Cortex-M or Cortex-A processors and associated toolchains
- Hands-on experience with RTOS (FreeRTOS, Zephyr, or similar)
- Familiarity with wireless protocols (BLE, Wi-Fi, or Thread)
- Comfort reading schematics and working alongside hardware engineers during board bring-up
- Experience with embedded debugging tools and workflows

**Bonus Qualifications**

- Experience with power optimization for battery-powered consumer devices
- Familiarity with secure boot, firmware signing, or hardware root of trust
- Exposure to ML inference runtimes on embedded platforms
- Experience shipping consumer electronics through EVT/DVT/PVT milestones

**Compensation**

The US base salary range for this full-time position is between $120,000 - $300,000 annually.

The pay offered for this position may vary based on several individual factors, including job-related knowledge, skills, and experience. The total compensation package may also include additional components/benefits depending on the specific role. This information will be shared if an employment offer is extended.

</details>

---

## 부록 B. JD 원문 v2 (2026-09-23 개정판, 수집 2026-09-23)

<details>
<summary>펼치기</summary>

About the Role
The Embedded Software team owns the software running on Hark’s next generation of AI hardware, and we are looking for an Embedded BSP Engineer to join the team.
You will own critical pieces of the firmware across the stack that powers Hark's consumer products, from board bring-up, vendor code integrations, to custom peripheral drivers and integrations, FPGA-based system integrations, and system power and performance improvements. 
Responsibilities
- Develop and maintain embedded firmware in C/C++ targeting ARM-based SoCs and microcontrollers
- Own BSP development, board bring up, peripheral drivers (SPI, I2C, UART, I2S etc.), and system integration
- Working closely with vendors on system integration and validation
- Working with Hardware and Software team to ensure our hardware are built and functioning to the specifications
- Optimize power consumption and thermal performance for always-on, battery-powered operation
- Debug complex hardware-software interactions using logic analyzers, oscilloscopes, and JTAG
Requirements
- 5+ years of professional firmware or embedded systems development
- Strong proficiency in C and/or C++ in resource-constrained environments
- Experience with ARM Cortex-M or Cortex-A processors
- Hands-on experience with embedded operating systems, such as eLinux, AOSP, VxWorks and RTOSes
- Familiarity with wireless protocols (BLE, Wi-Fi, or Thread)
- Comfort reading schematics and working alongside hardware engineers during board bring-up
- Experience with embedded debugging tools and workflows
Bonus Qualifications
- Experience with power optimization for battery-powered consumer devices
- Familiarity with secure boot, firmware signing, or hardware root of trust
- Exposure to ML inference runtimes on embedded platforms
- Experience shipping consumer electronics through EVT/DVT/PVT milestones
Compensation
The US base salary range for this full-time position is between $120,000 - $300,000 annually.
The pay offered for this position may vary based on several individual factors, including job-related knowledge, skills, and experience. The total compensation package may also include additional components/benefits depending on the specific role. This information will be shared if an employment offer is extended.

</details>
