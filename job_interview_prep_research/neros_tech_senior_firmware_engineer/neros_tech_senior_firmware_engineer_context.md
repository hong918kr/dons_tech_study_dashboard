# Neros Technologies — Senior Firmware Engineer, Platform · Context

> **최종 갱신**: 2026-09-18 · **상태**: 💻 폰스크린 (리크루터 통과 → HM 기술 인터뷰 당일)
> 🔥 **오늘: Adam Kibit (Director of Firmware) 45분 기술 인터뷰** → 당일 준비는 `2026-09-18_adam_interview_D-6h_prep.md`, 배경 상세는 `neros_hm_adam_technical_prep_2026-09-18.md` (둘 다 HTML 버전 있음)
> **JD**: https://job-boards.greenhouse.io/nerostechnologies/jobs/5195308007 · **위치**: Torrance, CA / onsite [추정] · **연봉 밴드**: $195,000 – $273,000 base + equity
> **사용 레쥬메**: `Resume_Firmware_Engineer_2026_Sep_DonHong.pdf`
> 신뢰도: `[확인됨]` 공식/복수 출처 · `[추정]` 단일·2차 출처 또는 추론. 리크루터 공식 안내가 항상 우선.

<!-- 상태 값: 🔍 조사중 → 📨 지원완료 → 📞 리크루터 → 💻 폰스크린 → 🏢 온사이트 → 🎉 오퍼 | ❌ 불합격 | ⏸ 보류 -->

---

## 0. TL;DR

- **회사**: 전직 드론 레이서들이 2023년에 세운 방산 드론 스타트업. 저가 소모형 FPV 공격 드론 **Archer**를 주당 ~1,200대 양산하고, 2026-08 Series C로 **$250M를 $2.5B 밸류**에 조달함. [1][2][4]
- **역할**: Flight·Ground-station·Autonomy 팀이 모두 올라타는 **공통 firmware platform**을 책임지는 자리. 범위는 logging/telemetry/IPC/config 라이브러리, SDK, **Bazel 기반 cross-compile 빌드 시스템**이고, MCU와 Linux 타깃을 모두 다룸. "고객은 다른 엔지니어" [JD]
- **적합도**: ⭐⭐⭐☆☆. embedded C, 크로스바운더리 디버깅, telemetry/SDK 경험은 잘 맞음. 다만 JD의 절반을 차지하는 **빌드 시스템(Bazel) 오너십**과 **embedded Linux**가 레쥬메에 없음.
- **커리어 방향**: ⭐⭐⭐☆☆. 드론 autonomy(Archer AI, terminal guidance)라서 physical AI·robotics와 인접함. 그래도 칩·가속기 쪽이 아니라 **소프트웨어 플랫폼 쪽으로 이동**하는 선택임.
- **핵심 어필**: ① NVMe **telemetry** 기반 디버그 기능을 설계해 출시함 (JD의 logging/telemetry 라이브러리와 직결) ② 다른 엔지니어가 쓰는 **test platform SDK/API**를 배포한 경험 ③ Apple에서 칩과 시스템의 경계(PCIe/I2C/SPMI/RFFE)에서 터지는 버그를 루트코즈해 온 경험 = "platform meets consumer" 문제 ④ RF-adjacent firmware (Nice-to-have 항목)
- **최대 갭/리스크**: (1) Bazel·빌드 시스템 오너십 ❌ (2) Embedded Linux·Yocto ❌ (3) 플랫폼 코드를 여러 팀이 쓰게 유지해 온 **명시적 증거가 약함** (4) **Torrance(LA) 이주** (5) ITAR·US Person 요건 가능성 (6) Apple 재직 ~9개월에 이직하는 걸 어떻게 설명할지
- **인터뷰 포맷** [확인됨 2026-09-16]: ✅ 리크루터 콜 → **HM Adam Kibit 45분 기술** → 온사이트 (Tour 30분 · **경력 발표 1시간** · 1:1 기술 여러 개: C/C++, **ring buffer**, generic system design)
- **다음 액션**: ① **Adam(HM) 45분 기술 인터뷰 준비** — HM 준비 노트의 스토리 A·B·C 실제 디테일 채우기 + Board B 변종 구조·MCU↔컴패니언 프로토콜 화이트보드 연습 ② 온사이트 대비: 1시간 발표 초안, ring buffer 맨손 구현, generic system design ③ ~~Bazel 미니 프로젝트~~ → 특정 JD 없는 req로 확인돼 우선순위 하향

---

## 1. 포지션 요약 (JD 핵심)

| 항목 | 내용 |
|---|---|
| 팀 / 조직 | Firmware/Embedded Engineering › **Platform team** [JD] |
| 레벨 | ~~Senior (IC) [JD]~~ → 리크루터 안내 포지션명은 **"Principal/Senior Embedded Software Engineer"**, **특정 JD 없음** [확인됨 2026-09-16]. 레벨은 HM 인터뷰에서 정해질 것으로 보임 [추정] |
| 위치 / 근무형태 | Torrance, California. JD에 remote 언급이 없고, 공고 90개 중 74개가 Torrance → **onsite** [추정][6] |
| 연봉 밴드 / Equity | Platform 공고 기준 **$195K – $273K base** + equity [JD]. **Don이 리크루터에게 "base 최소 $200K 이상"이라고 답함** [확인됨 2026-09-16] |
| 비자 · US Person · 클리어런스 | 이 JD 본문에는 ITAR 문구가 없음 [JD]. 그런데 같은 회사의 Ground Software Manager·State Estimation Lead·PCB 공고에는 **ITAR "U.S. Person"(시민권자 또는 영주권자) 요건**이 있음 [6][8] → 요구될 가능성 높음 [추정]. 클리어런스 언급은 없음 |

**주요 업무**
- 모든 firmware 팀이 쓰는 공통 runtime·라이브러리 오너십: **logging, telemetry, IPC, configuration** (MCU와 Linux 타깃 모두)
- **빌드 시스템과 cross-compile toolchain** 오너십: 모든 출하 타깃에서 빠르고 reproducible하며 hermetic한 빌드, 그리고 다른 팀이 쓰는 SDK
- 다른 팀이 올라타는 **인터페이스를 설계하고, 플랫폼이 바뀌어도 안정적으로 유지**
- 플랫폼과 consumer 팀이 만나는 지점에서 생기는 모호한 문제 해결
- 서브시스템끼리 경쟁하는 **compute/memory/bandwidth 예산** 트레이드오프 결정
- 패턴·코드리뷰·멘토링으로 firmware 엔지니어링 수준 끌어올리기

**필수 자격**
- 출하 제품에서의 embedded C **mastery**. MCU/RTOS와 embedded Linux 양쪽에 능숙할 것
- 여러 팀이 의존하는 platform code(라이브러리·SDK·프로토콜)를 만들고, 변화 속에서도 안정적으로 유지한 경험
- embedded 빌드 시스템(예: **Bazel**)을 직접 오너십으로 운영: 멀티 타깃 cross-compile, dependency 관리, reproducibility
- 팀이나 서브시스템 경계를 넘는 문제를 끝까지 책임지고 출하까지 해결한 경험
- 실제 하드웨어에서 compute·memory·bandwidth 제약을 두고 설계한 판단력
- 디버깅 mastery: 남들이 포기한 버그를 잡은 이력
- hands-on: 지금도 코드를 출하하는 사람

**우대 사항**
- Bazel 자체, 특히 embedded/cross-compiled 타깃이나 firmware 조직의 Bazel 마이그레이션
- Embedded Linux 내부 (kernel, BSP, Yocto/Buildroot)
- Flight stack (Betaflight, PX4)
- **DSP, comms, RF-adjacent firmware**
- 공개 오픈소스 시스템 작업
- FPV·RC·드론 직접 조종

**기술 키워드**: `Embedded C` `MCU/RTOS` `Embedded Linux` `Bazel` `cross-compile toolchain` `hermetic/reproducible builds` `SDK` `logging` `telemetry` `IPC` `configuration` `API stability` `resource budgets` `Yocto/Buildroot` `Betaflight` `PX4` `DSP/RF`

**같은 조직의 형제 공고 (2026-09-11 갱신)** [6]
- **Senior Embedded Linux Engineer** ($165–231K): Yocto/kernel/U-Boot/secure boot를 맡음. "common runtime and libraries … logging, telemetry, IPC, configuration" 문구가 이 공고와 **겹침**. 같은 Platform 팀에서 Linux distro 쪽 짝꿍 역할로 보임 [추정]
- **Firmware Test Engineer**: Bazel, CMake, Betaflight, ExpressLRS, HIL, GitLab CI를 언급 → **사내 빌드가 Bazel이고 flight stack이 Betaflight 계열**이라는 정황 [추정]
- **Firmware Engineer – Flight Software** ($145.5–204K): Betaflight/PX4 포팅, position/altitude hold → 이 플랫폼의 주요 consumer
- **Autonomy Platform & Runtime Lead**: autonomy 쪽 runtime/IPC/latency/logging을 맡고, embedded accelerator로 quantized 모델을 배포하는 역할. 이 포지션과 인터페이스가 겹칠 것
- 참고로 Don에게 더 직접적으로 맞을 수 있는 다른 공고: **Lead RF Test & Integration Engineer** ($162.5–227.5K). Apple RF 통합 경험과 가깝지만 base가 낮고 HW 쪽 비중이 큼

---

## 2. 회사 분석

### 2.1 무엇을 하는 회사인가
Neros Technologies는 "America's drone industrial base 재건"을 내건 방산 스타트업이다. 전직 프로 드론 레이서 **Soren Monroe-Anderson (CEO, Thiel Fellow)** 과 **Olaf Hichwa (CTO)** 가 2023년에 창업했다 [3][4]. 싸고(대당 ~$2K), 중국산 부품이 없고, 재밍에 강한 **소모형 FPV 공격 드론**을 대량 생산한다 [4][5]. 창업자들이 직접 우크라이나(키이우)에 드론을 전달한 데서 출발했다 [4].

### 2.2 제품 · 고객 · 비즈니스 모델
- **Archer**: 5~10인치 FPV strike quadcopter, 모듈형 탄두, 사거리 ~20km+, **Blue UAS 인증**, 중국 부품 없음 [4][5][9]
- **Archer AI**: Archer에 autonomy를 더한 버전. **Terminal Guidance, GPS-denied Position Hold** 탑재 [1][2]
- **Bandit**: Class 2/3 드론(Shahed급)을 잡는 **counter-UAS interceptor** [1][2]
- **Flatbow / Crossbow**: ground control station (EW 저항, 장거리 보안 C2) [10][11]
- **자체 개발 스택**: C2와 video link 라디오, 통합 flight computer, 센서, 추진계를 **in-house 설계** ("software-defined hardware") [11]
- **고객**: US Army (PBAS 프로그램, **$500M IDIQ, 2026-07 발표**, 5년 계약), USMC ($17M, ~8,000대), 우크라이나 (International Drone Coalition 통해 6,000대), 영국 자회사, 동맹국 sovereign manufacturing [4][5][9][12]
- **모델**: 하드웨어 대량 양산(자동차식 "Toyota of defense") + 교육/지원 번들 [12][13]

### 2.3 단계 · 펀딩 · 규모
| 설립 | 펀딩 총액 / 최근 라운드 | 밸류에이션 | 주요 투자자 | 인원 | HQ |
|---|---|---|---|---|---|
| 2023 | 누적 ~$370M [추정][4] / **Series C $250M (2026-08-11)** [1][2] · Series B $75M (2025-11, Sequoia 리드) [14] | **$2.5B** post (Series B 대비 약 3배) [2] | Sequoia, American Strategic Technology Fund (C 공동 리드), Valor, Thiel Capital, Spark, Allen & Co, Interlagos, Dylan Field [1] | ~~250+~~ → **약 300명, 절반 이상이 공장 operator/technician. FW 엔지니어 12명 + Director** [확인됨 2026-09-16 리크루터] | Torrance, CA. 250,000 sqft 공장 [3] |

- 기타 오피스: Ukraine Office, Washington DC, Tennessee, Swindon(UK) [6]
- 비상장. IPO 관련 공식 언급은 없음. 목표는 **2028년까지 연 100만 대 생산** [1][3]

### 2.4 최근 뉴스 (최근 12개월)
- **2026-08-11**: Series C $250M, $2.5B 밸류. Archer AI와 Bandit을 **2026년 말까지 전장 배치**할 계획 [1][2]
- **2026-07-14**: US Army PBAS **$500M IDIQ** (award 2026-06-30, 기간 ~2031-06). "수십만 대" 규모가 될 수 있다는 보도 [5][9]
- **2026-08**: 생산 속도 주당 ~1,200대 (2025-11에는 월 2,000대) [4][5]
- **2025-11~12**: USMC 계약, PBAS Tranche 1 선정 (Archer + Flatbow GCS), Series B $75M [10][12][14]
- **2024-12**: 중국 제재 명단에 오름. 회사는 "badge of honor"라고 반응 [4]
- 레이오프나 부정적 뉴스는 확인되지 않음

### 2.5 경쟁사 · 시장 포지션
- **FPV·소모형**: PBAS Tranche 1 제조사 3곳 중 하나 [12]. 경쟁 구도는 Performance Drone Works(PDW), Auterion(소프트웨어·키트), Skydio·Red Cat(정찰 SRR 쪽) 등 [15][추정]
- **대형 방산 스타트업**: Anduril (Bolt/Anvil 등 로이터링·요격), Shield AI
- **차별점**: ① US 최대 FPV 생산량 ② 중국 부품 없는 supply chain과 Blue UAS 인증 ③ 라디오부터 flight computer까지 in-house ④ 우크라이나 실전 피드백 루프 [4][11]

### 2.6 엔지니어링 문화 · 평판 · 보상
- **Glassdoor 4.9/5 (16건), 추천율 100%, WLB 4.4, 보상 4.8, 커리어 5.0** [7][16]. 표본이 작고 초기 직원 편향 가능성 있음 [추정]
- 리뷰 키워드: 강한 미션, "go the extra mile", 에너지 넘치는 오피스 문화, 명확한 가치관 [16]
- JD 톤: "move fast, extreme ownership, months not years", "hands-on" → **Anduril식 강도 높은 온사이트 문화** [추정]
- **보상**: 이 포지션 base $195–273K는 Neros firmware 공고 중 최상단 (Flight SW Manager $198–277.5K와 비슷). Equity는 $2.5B 비상장 주식이라 유동성 없음 [6]. levels.fyi 데이터 없음

### 2.7 이 포지션이 실제로 할 일 (추론)
- **MCU 쪽**: Betaflight 계열 flight controller(STM32급 Cortex-M 추정)와 라디오·GCS·핸드셋 MCU에 공통으로 들어가는 **binary logging, telemetry framing, parameter/config store, OSAL/IPC** 라이브러리를 표준화 [추정]
- **Linux 쪽**: autonomy companion computer(embedded accelerator)와 GCS에서 같은 로깅·텔레메트리 스키마를 공유하도록 C 라이브러리 + SDK 제공 [추정]
- **빌드**: 여러 제품(Archer, Archer AI, Bandit, GCS, radio)과 여러 타깃(arm-none-eabi, aarch64-linux, host)을 대상으로 한 **Bazel monorepo**. toolchain·platform 정의, remote cache, CI 통합. 일부는 CMake/Make에서 Bazel로 옮기는 **마이그레이션 작업**일 가능성 [추정: Nice-to-have 문구와 Test Engineer 공고의 "Bazel, CMake, make" 병기에서 추론]
- **예산 조정**: flight loop와 autonomy 사이의 CPU/RAM 배분, 재밍 환경의 좁은 datalink에서 telemetry와 video 대역폭 우선순위 결정 [추정]
- **실상**: 급성장(250명+) 중에 제품군이 늘면서 팀별로 fork가 늘어나는 걸 막는 "플랫폼화" 담당자. Autonomy Platform Lead 공고의 "per-program fork 금지" 문구와 같은 맥락 [추정]

### 2.8 리스크
- **이주**: San Jose → Torrance (LA South Bay). Onsite 필수 가능성이 큼
- **US Person / ITAR**: 요구될 가능성이 높음. 본인 체류 신분 확인 필요
- **미션**: 살상용 strike drone 회사. 인터뷰에서 **방산 미션에 대한 진정성**을 강하게 볼 것
- **성장통**: 1년 만에 밸류 약 3배, 인원 급증, 2026년 말 전장 배치라는 데드라인 → 업무 강도 높음
- **Equity**: 비상장이고 방산 조달 예산 사이클에 의존
- **커리어**: SW 플랫폼·빌드 인프라에 무게가 실린 역할이라, 칩·가속기 통합 트랙에서 멀어질 수 있음

---

## 3. 적합도 분석 (Don ↔ JD)

### 3.1 요구사항 매칭표
| JD 요구사항 | Don 경험 (레쥬메 근거) | 매칭 |
|---|---|---|
| Embedded C mastery on shipped products | "Developed using embedded C and C++ programming on bare-metal (pre/post-silicon)", "production firmware" (Solidigm, PCIe 6 SSD), "Xtensa-based FW of the primary data path" | ✅ |
| MCU/RTOS fluency | "ARM Cortex R8/R82/M0+", "Cortex-M, Xtensa". 단 bare-metal 위주이고 **RTOS 명시는 없음** | 🟡 |
| Embedded Linux fluency | Summary에 "Linux"만 있음. kernel/userspace 출하 경험 근거는 없음 | ❌ |
| 여러 팀이 의존한 platform code (라이브러리·SDK·프로토콜) + 변화 속 안정성 | "Deployed test platform SDK: various APIs … facilitating the use of the massive chamber systems for other engineers", "Implemented the feature of an error reporting/handling scheme in the production firmware", "debugging feature based on NVMe telemetry protocol" | 🟡 (소재는 있지만 "multi-team consumer + API 안정성 유지"로 프레이밍 필요) |
| Embedded build system 오너십 (Bazel, cross-compile, deps, reproducibility) | 레쥬메 근거 없음 (Git/Perforce만 있음) | ❌ **최대 갭** |
| 팀·서브시스템 경계를 넘는 문제를 출하까지 해결 | "Lead integration of new silicon into shipping consumer platforms – owning root-cause analysis … when a new chip meets the full HW/SW system", "Drive the full cycle: bring up → NPI → MP" | ✅ |
| Resource-constrained 설계 판단 (compute/memory/bandwidth) | "firmware performance tuning", "performance-critical customer (Google/Meta) requirement features in NVMe 2.0", "safety margins (reliability vs performance/power)" | ✅/🟡 (bandwidth 트레이드오프는 PCIe/telemetry로 전이) |
| Debugging mastery | "Debug at signal, bus, and system level with DSOs and protocol Analyzer", Trace32, JTAG/LA/Power Analyzer, "proactively [surface] latent silicon/integration defects before MP" | ✅ **최대 강점** |
| Hands-on, still ships code | Solidigm production FW 코드 출하. Apple에서는 integration/test-node 비중이 큼 → "지금도 코드를 짜는가"를 물어볼 것 | 🟡 |
| (Nice) Bazel | 없음 | ❌ |
| (Nice) Embedded Linux internals / Yocto | 없음 | ❌ |
| (Nice) Flight stack (Betaflight/PX4) | 없음 | ❌ |
| (Nice) DSP, comms, RF-adjacent firmware | Apple **RF-Hardware (Wireless) Chipset Integration**, RFFE/SPMI 버스 | ✅ |
| (Nice) Open-source | 없음 | ❌ |
| (Nice) You fly | 없음 | ❌ |

### 3.2 강점 — 어필 포인트
1. **Telemetry와 디버그 인프라 설계 경험이 JD의 logging/telemetry 라이브러리와 직결됨.** SK hynix에서 NVMe telemetry protocol 기반 time-sensitive 디버깅 기능을 설계해 출시했고, Microsoft·Dell·HPE 데이터센터 qualification에 쓰였다. 좁은 인터페이스로 필요한 상태를 뽑아내는 문제로, 드론 datalink telemetry와 본질이 같다.
2. **"Where platform meets consumer" 버그 전문가.** Apple에서의 일이 정확히 "새 칩(플랫폼)이 전체 시스템(consumer)을 만날 때 터지는 인터페이스 레벨 실패"를 루트코즈하는 것이다. JD 문구 "hardest problems … surface where their code meets the platform"과 1:1로 대응한다.
3. **다른 엔지니어가 쓰는 SDK를 배포한 경험.** 챔버 테스트 플랫폼 SDK(온도 제어·스케줄링·UART 시퀀스 자동화 API)를 다른 엔지니어들이 대규모로 썼다. "Your customers are other engineers" 마인드셋의 증거다.
4. **Bring-up부터 MP까지, pre-silicon부터 production까지 전 주기 경험.** FPGA 단계 SoC 검증(I2C, SPI, DMA, PCIe, SRAM/DRAM)부터 production FW error handling까지 해봤다. 플랫폼 인터페이스가 하드웨어 변화 속에서 어떻게 깨지는지 안다.
5. **RF-adjacent와 수학 기반.** Nice-to-have인 RF/comms를 충족하고, Berkeley 응용수학 배경은 DSP·필터·예산 모델링에 대한 신뢰를 준다.

### 3.3 갭 & 보완 전략
| 갭 | 인터뷰 전 할 일 | 답변 프레이밍 |
|---|---|---|
| **Bazel·빌드 시스템 오너십** ❌ | ① **미니 프로젝트** (주말 1~2회): Bazel(bzlmod)로 C logging/telemetry 라이브러리를 **Cortex-M4 (arm-none-eabi) + aarch64 Linux + host 테스트** 3타깃으로 cross-compile. `platforms`/`constraint_value`, `cc_toolchain` 등록 (예: `toolchains_arm_gnu`, `toolchains_llvm`), `select()`, `cc_test`, remote/disk cache, 두 번 빌드해 해시 비교로 reproducibility 확인. GitHub 공개 (Nice-to-have인 open source까지 일부 충족) ② 개념 정리: hermeticity, sandboxing, action cache/CAS, `-ffile-prefix-map`, `SOURCE_DATE_EPOCH`, sysroot, CMake→Bazel 마이그레이션 전략 | "SSD FW 조직에서는 사내 빌드 인프라를 **사용하는 쪽**이었지만, FPGA/ASIC/멀티코어(R8, R82, M0+, Xtensa) **여러 타깃 이미지를 다뤘기 때문에** toolchain과 링커 이슈에 익숙하다. 그리고 Bazel로 3-타깃 hermetic 빌드를 직접 구성해 봤다 → [repo]". **없는 경험은 절대 부풀리지 말 것** |
| **Embedded Linux** ❌ | ① Linux userspace IPC 기초 복습: POSIX shm, UNIX domain socket, `mmap`, `eventfd`, lock-free ring over shm ② Buildroot로 QEMU aarch64 이미지 한 번 빌드 ③ device tree와 U-Boot 개념 복습 | "플랫폼 라이브러리의 **MCU 쪽 코어는 즉시 기여**할 수 있고, Linux 쪽은 OSAL 추상화를 통해 같은 API를 제공하는 방식으로 접근하겠다." Senior Embedded Linux Engineer(형제 공고)와 역할이 분담될 가능성을 역질문으로 확인 |
| **RTOS 명시 없음** 🟡 | `anduril_firmware_engineer/` 09_rtos 세트 복습 (priority inversion, ISR-safe queue, stack sizing) + FreeRTOS 기본 API | bare-metal 멀티코어 SSD FW에서 겪은 동시성 이슈(IPC between cores, DMA, 인터럽트)를 RTOS 개념으로 번역해서 설명 |
| **여러 팀이 쓰는 API의 안정성 유지 증거** 🟡 | 레쥬메 소재 재구성: error reporting scheme(여러 FW 모듈이 호출), telemetry 포맷(고객과 내부 툴이 소비), test SDK(다른 엔지니어가 사용) → 각각 "누가 썼고, 어떻게 호환성을 유지했나" STAR로 준비 | "customer-facing 포맷 (NVMe telemetry log)은 **스펙 호환을 깨면 고객 qualification이 깨진다** → versioning·backward compat를 체득했다" |
| **Flight stack / 드론 도메인** ❌ | Betaflight 소스 구조 훑기 (scheduler, `pg/` parameter groups, `blackbox/`, MSP, CRSF). PX4 uORB와 MAVLink 개념. 가능하면 Betaflight SITL 빌드 1회 | "Betaflight의 blackbox/parameter group을 봤는데, 플랫폼 관점에서는 X를 이렇게 일반화하겠다" 식으로 **숙제해 온 티**를 낸다 |
| **Hands-on 코딩 최근성** 🟡 | Apple에서 작성하는 코드(test-node 스크립트, 디버그 FW 등)가 있다면 구체화. C 코딩 드릴 재가동 | "Apple에선 integration 비중이 크지만 최근까지 Solidigm에서 production C를 출하했고, 코딩으로 돌아가고 싶은 게 이직 이유 중 하나" |

### 3.4 종합 평가 · 커리어 방향 정합성
- **적합도 ⭐⭐⭐☆☆**: 필수 7개 중 ✅ 3 (C, 크로스바운더리, 디버깅) / 🟡 3 (RTOS, 플랫폼 코드, 리소스) / ❌ 1이지만 그 ❌(**빌드 시스템**)가 JD의 핵심 축이고, embedded Linux까지 빠져 있다. "디버깅·telemetry에 강한 SSD/실리콘 FW 엔지니어"가 "플랫폼·빌드 오너"로 넘어갈 수 있다는 걸 설득해야 한다. Senior 레벨이라 기대치는 합리적인 편.
- **커리어 방향 ⭐⭐⭐☆☆**: (+) 드론은 physical AI·robotics의 최전선이고, Archer AI의 autonomy와 embedded accelerator 배포(형제 공고)에 인접해 있다. 방산 드론 경력은 Anduril 등 타깃 회사로 이어진다. (−) **칩·가속기·SoC 통합**이라는 Don의 차별점(PCIe/SerDes/실리콘 bring-up)을 거의 쓰지 않는 SW 인프라 역할이다. AI 가속기 회사(NVIDIA 등)로 가는 직선 경로는 아니다.
- **리스크 요약**: 이주 (LA), US Person 요건 가능성, Apple 재직 ~9개월 후 이직 설명, 방산 미션 적합성, 비상장 equity.
- **판단**: 미션과 이주에 거부감이 없다면 **지원할 가치는 있음**. 다만 합격 가능성을 올리려면 Bazel 미니 프로젝트가 사실상 필수이고, 인터뷰에서 Flight Software 쪽으로 방향이 바뀔 가능성도 열어둘 것.

### 3.5 레쥬메 튜닝 제안 (이 JD용)
> ⚠️ **사실인 범위에서만** 사용. 없는 빌드 시스템 경험을 만들어 넣지 말 것.

- **Summary 첫 줄 교체**: `Embedded C firmware engineer (MCU/bare-metal, multi-core ARM Cortex-R/M & Xtensa) who builds the debug, telemetry, and test infrastructure other engineers depend on — and cracks the cross-boundary HW/SW bugs others can't.`
- **SK hynix telemetry bullet 강화**: `Designed and shipped an NVMe telemetry-based logging/debug feature consumed by data-center customers (Microsoft, Dell, HPE) and internal FW/validation teams; kept the log format backward-compatible across firmware releases.` (호환성 유지 부분은 사실일 때만)
- **Test SDK bullet을 "platform" 언어로**: `Built and owned a test-platform SDK (temperature control, scheduling, UART test-sequence automation APIs) used by multiple engineering teams to run large-scale chamber reliability testing.`
- **Apple bullet 앞에 JD 문구 매칭**: `Own root-cause of failures that surface where new silicon meets the full HW/SW platform (PCIe, I2C, SPMI, RFFE) — driving fixes across chip vendor, board, and firmware teams to shipped resolution.`
- **(미니 프로젝트 완료 시) Projects 섹션 추가**: `Bazel hermetic cross-compile SDK (Cortex-M4 + aarch64 Linux + host tests): C logging/telemetry library with toolchain resolution via platforms, reproducible builds verified by hash — github.com/…`
- Skills 줄에 `FreeRTOS`, `Bazel`, `CMake`, `Buildroot`는 **실제로 다뤄본 뒤에만** 추가

---

## 4. 예상 인터뷰

### 4.1 프로세스
```
✅ 리크루터 콜 30m — Devin  [확인됨 2026-09-16 완료]
     포지션: "Principal/Senior Embedded Software Engineer" — 특정 JD 없음
  → HM 기술 인터뷰 45m — Adam Kibit (Director of Firmware)  [확인됨, 2026-09-18 전후]
  → 온사이트  [확인됨 구성]
       ① Tour 30분
       ② 본인 경력 발표 1시간 (가장 큰 challenge, issue resolving/debugging, bring-up 경험)
       ③ 1:1 기술 인터뷰 여러 개: 기본 C/C++, ring buffer 구현, generic system design (드론 설계 아님)
```
- ~~이전 추정 (2026-09-14): 팀원 45분 기술 폰스크린 → Torrance 온사이트(C 코딩 / Bazel·빌드 딥다이브 / 플랫폼 설계 / 미션)~~ → 2026-09-16 리크루터 안내로 교체. **빌드 시스템 딥다이브는 확인된 온사이트 항목에 없음.**
- Glassdoor 후기: "transparent and well communicated", 포커스 영역을 미리 알려줌 [7]. 표본이 적음.

### 4.2 실제 보고된 질문
- [확인됨·단일출처] 폰스크린 45분: 과거 경험 질문 + embedded systems 기술 질문 (구체 질문 원문은 공개되지 않음) [7]
- 이 포지션(Platform)의 실제 질문은 공개된 게 없음 → 4.3의 JD 기반 예상에 의존

### 4.3 JD 기반 예상 기술 질문
| 카테고리 | 질문 | 왜 나올지 (JD 근거) |
|---|---|---|
| C 코딩 | ISR에서 호출해도 안전한 **lock-free SPSC ring buffer** 기반 logger 구현 (overflow 정책: drop-oldest vs drop-new, dropped count) | "common runtime … logging" |
| C 코딩 | **고정 크기 메모리 풀 allocator** (O(1) alloc/free, malloc 금지 환경) | MCU 리소스 제약, 공통 라이브러리 |
| C 코딩 | Telemetry 패킷 **직렬화/역직렬화**: packed struct 대신 명시적 little-endian 인코딩, **COBS/SLIP framing + CRC-16**, 버전 필드 | "telemetry", "protocols consumed by multiple teams", bandwidth |
| C 코딩 | **Config/parameter store**: key→typed value, 기본값, 범위 검증, flash에 저장 (wear, CRC, A/B 슬롯), 스키마 버전 마이그레이션 | "configuration", 인터페이스 안정성 |
| C 코딩 | 비트 조작·포인터 기본 (reverse bits, endianness swap, `container_of`, 함수 포인터 테이블로 OSAL) | 모든 embedded 인터뷰 공통 |
| 임베디드 개념 | `volatile`, critical section, ISR에서 printf를 하면 안 되는 이유, **deferred formatting** (format string ID만 전송: defmt/Trice 방식) | 고속 로깅이 flight loop 타이밍을 깨면 안 됨 |
| 임베디드 개념 | Priority inversion과 해결책, RTOS queue vs lock-free, stack 크기 산정과 overflow 탐지 | MCU/RTOS fluency |
| 임베디드 개념 | **동일한 API를 MCU(RTOS)와 Linux(POSIX)에 제공하는 OSAL 설계**: compile-time vs link-time vs runtime 추상화 | "on MCU-based systems and Linux targets" |
| 임베디드 개념 | 링커 스크립트: 로그 포맷 문자열을 non-loaded section에 두기, `.map`으로 flash/RAM 예산 추적, weak symbols | memory 예산, 빌드 |
| 임베디드 개념 | **ABI/API 안정성**: opaque handle, struct 버전·size 필드, 확장 가능한 enum, deprecation 정책, semver | "keeping them stable as the platform evolves" |
| 빌드 시스템 | **Bazel에서 cross-compile은 어떻게 동작하나?** platforms·constraints, toolchain resolution, `cc_toolchain_config`, `--platforms`, `select()` | 필수 자격 |
| 빌드 시스템 | **Hermetic·reproducible build의 정의와 달성법**: 툴체인을 repo에 고정(sha256), sandboxing, `-ffile-prefix-map`·`-fdebug-prefix-map`, 타임스탬프 제거, 정렬된 입력, 결과 해시 비교 | "fast, reproducible, hermetic builds" |
| 빌드 시스템 | CMake/Make 기반 firmware 조직을 Bazel로 **어떻게 마이그레이션**하겠나? (점진적, `rules_foreign_cc`, 양쪽 빌드 병행 기간, CI 게이트) | Nice-to-have "migrating a firmware org" |
| 빌드 시스템 | 빌드가 느리다 → 어떻게 진단하고 개선? (remote cache/execution, `--profile`, 헤더 의존성 줄이기, 타깃 세분화) | "fast builds" |
| 버스 / 프로토콜 | UART에서 telemetry를 보낼 때 프레이밍·재동기화 방법. **CRSF (ExpressLRS)**, MAVLink v2 구조 (서명, 메시지 ID, 확장 필드) | Betaflight/ExpressLRS 스택 정황 [6] |
| 버스 / 프로토콜 | IMU를 SPI로 고속 읽기 (DMA, FIFO watermark 인터럽트), I2C clock stretching·bus hang 복구 | Don의 I2C/SPI 이력 딥다이브 겸 |
| 도메인 | Flight controller의 8kHz PID loop와 로깅·텔레메트리 태스크가 경쟁할 때 **우선순위와 예산을 어떻게 나누나?** | "Drive resource trade-offs … compete for the same budget" |
| 도메인 | 재밍으로 링크 대역폭이 1/10로 떨어졌다. telemetry를 어떻게 degrade하나? (우선순위 큐, rate limit, delta 인코딩, 필수 필드만) | bandwidth, contested datalink |
| 도메인 | PX4 **uORB**(pub/sub) vs Betaflight 전역 구조체 방식 비교. 우리 IPC를 설계한다면? | IPC, flight stack Nice-to-have |
| 디버깅 시나리오 | 로깅을 켜면 버그가 사라진다 (heisenbug) → 원인 가설과 접근법 | "Debugging mastery" |
| 디버깅 시나리오 | 비행 중에만 드물게 리셋된다. 벤치에서는 재현 안 됨 → HardFault handler·crash dump를 noinit RAM에 저장, reset reason, blackbox, 전원·진동·EMI 가설 | "bugs others gave up on", field-only failures |
| 디버깅 시나리오 | 같은 커밋인데 CI 바이너리와 로컬 바이너리 해시가 다르다 | reproducibility |
| 디버깅 시나리오 | 플랫폼 라이브러리를 업데이트했더니 Autonomy 팀 기능이 깨졌다. 누구 잘못인지, 어떻게 해결하고 재발을 막나? | "where the platform meets its consumer teams" |
| 시스템 설계 | **MCU(FC) + Linux(companion) + 무선 링크 + GCS를 가로지르는 통합 logging/telemetry 시스템 설계**: 스키마 정의(IDL, 코드 생성), 타임스탬프·시간 동기, 온보드 저장 vs 실시간 전송, 버전 호환 | JD 전체 요약 문제. **가장 가능성 높음** |
| 시스템 설계 | 3개 consumer 팀이 쓰는 **SDK 릴리즈·버저닝 정책** (monorepo vs versioned SDK, breaking change 프로세스, 호환성 테스트) | "SDK other teams consume", 안정성 |

### 4.4 Don 경험 딥다이브 (레쥬메 bullet별)
| 레쥬메 bullet | 예상 꼬리질문 | STAR 소재 |
|---|---|---|
| Apple: 새 silicon 통합, PCIe/I2C/SPMI/RFFE 루트코즈 | 가장 어려웠던 버그 하나를 처음부터 끝까지 설명해 봐. 가설을 어떻게 좁혔나? 벤더·보드·FW 중 누구 책임이었고, 어떻게 설득했나? | 인터페이스 레벨 실패 1건: 증상 → 스코프·프로토콜 분석기 캡처 → 근본 원인 → 크로스팀 fix → 재발 방지 (test-node 추가) |
| Apple: factory test-node 아키텍처, stress 기반 latent defect 발굴 | test-node를 어떻게 설계했나? 커버리지는 어떻게 쟀나? 놓친 결함은? | "칩 validation이 놓친 결함을 MP 전에 잡은" 사례. Neros의 **양산 규모(주당 1,200대)** 와 연결 |
| Apple: safety margin sign-off (reliability vs perf/power) | 마진은 어떤 기준으로 정했나? 데이터가 부족할 때 어떻게 결정했나? | JD의 "resource trade-offs" 판단력 사례로 재사용 |
| Solidigm: FPGA 이미지·FW bring-up (Cortex R8/R82/M0+, PCIe/NVMe) | 멀티코어 간 통신은 어떻게 했나? (IPC, shared memory, mailbox) 부트 순서는? 빌드는 코어별로 어떻게 나뉘었나? | **멀티 타깃·멀티코어 이미지 경험 → 빌드·IPC 갭을 메우는 다리** |
| Solidigm: error reporting/handling scheme | 여러 모듈이 이 API를 어떻게 썼나? 에러 코드 체계는? 나중에 확장할 때 기존 호출자를 어떻게 안 깨뜨렸나? | **"다른 팀이 의존한 platform code"** 대표 사례로 준비 |
| Solidigm: Google/Meta NVMe 2.0 성능 critical 기능 | 성능 병목을 어떻게 측정했나? 메모리와 레이턴시 트레이드오프는? | resource-constrained 설계 |
| SK hynix: NVMe telemetry 기반 디버그 기능 (MS/Dell/HPE) | 어떤 데이터를 얼마나 자주, 어떤 포맷으로 남겼나? 성능 오버헤드는? 로그 버퍼 크기는 어떻게 정했나? FW 버전이 바뀌면 파서 호환은? | **JD 'logging/telemetry'의 핵심 증거.** 링버퍼 크기, 캡처 트리거, 오버헤드 수치까지 준비 |
| SK hynix: Xtensa NAND 데이터패스 FW + media defense 알고리즘 | 데이터패스 동시성, ISR 처리, 알고리즘 검증 방법 | embedded C mastery |
| SK hynix: shmoo·health monitoring 디버그 FW (SoC/SI 팀 협업) | SI 팀과 FW의 경계 문제는 무엇이었나? | 크로스바운더리 오너십 |
| SK hynix: test platform SDK·자동화 (다른 엔지니어가 사용) | 사용자 요구를 어떻게 수집했나? API가 바뀌면 사용자에게 어떻게 공지·호환했나? 문서화는? | **"Your customers are other engineers"** 사례 |

### 4.5 행동 · 동기 질문
- **Why Neros?** → "Capability가 수개월 안에 전장에 가는 속도와, 주당 1,200대에서 연 100만 대로 가는 규모. 그 규모에서는 **플랫폼 한 줄이 수십만 대에 곱해진다**. 저는 새 실리콘이 시스템을 만날 때 깨지는 지점을 잡아 온 사람이라, 여러 제품군이 한 플랫폼에 올라타는 지금 가장 레버리지 큰 자리라고 봤다." + 중국 부품 없는 supply chain, 우크라이나 실전 피드백 루프 언급
- **Why leave Apple (재직 ~9개월)?** → 부정적인 이야기는 피한다. "통합과 루트코즈 역량을 키웠지만, **직접 소유하는 코드와 플랫폼**을 만들고 싶다. 소비자 제품의 연 단위 사이클보다 빠른 반복을 원한다." 짧은 재직기간은 먼저 인정하고 방향성으로 설명
- **Why defense / 살상 무기에 대한 입장?** → 거부감이 없다면 명확하고 짧게: 억지력, 동맹 방어, 우크라이나. 애매하게 답하면 치명적
- **Why platform vs flight software?** → 디버깅과 인프라를 좋아하고, 여러 팀의 생산성을 곱으로 올리는 일
- **Extreme ownership 사례**: 책임 소재가 불분명한 인터페이스 버그를 끝까지 가져간 이야기
- **남들이 포기한 버그**: 4.4 Apple 사례
- **동료의 코드/설계에 반대했던 경험, 멘토링 경험** ("raise the bar")
- **FPV 조종해 봤나?** → 안 해봤다면 인터뷰 전에 **시뮬레이터(Liftoff/Velocidrone) + 입문 기체**라도 경험해 두면 문화 적합성 점수에 크게 도움 [추정]

### 4.6 Tell me about yourself (영어 초안, 60~90초)
> I'm an embedded firmware engineer, and the common thread in my career is working at the boundary where one team's code meets another team's hardware or platform.
>
> I spent about seven years at SK hynix and Solidigm building production SSD firmware in C on multi-core ARM Cortex-R and M, and Xtensa — from FPGA bring-up before RTL freeze all the way to shipped PCIe Gen5/6 drives. Two things from that era map directly to this role: I designed and shipped an NVMe telemetry-based debug and logging feature that data-center customers like Microsoft and Dell relied on, and I built a test-platform SDK that other engineering teams used to run large-scale reliability testing — so I've had other engineers as my customers.
>
> Since last December I've been at Apple on the RF chipset integration team, owning root cause when new silicon meets the full HW/SW system — PCIe, I2C, SPMI, RFFE failures that no single team owns. That's made me very good at the bugs others give up on.
>
> What draws me to Neros's Platform team is leverage: you're scaling from a thousand drones a week toward a million a year across multiple product lines, and a solid runtime, telemetry, and build foundation multiplies across every one of them. I want to own that code, not just debug around it.

### 4.7 역질문
1. 현재 빌드 시스템은 어떤 상태인가요? 이미 **Bazel monorepo**인가요, 아니면 CMake/Make에서 마이그레이션하는 중인가요? 이 역할의 첫 90일 성공 기준은 무엇인가요?
2. Platform 팀 구성과 **Senior Embedded Linux Engineer** 역할과의 경계는 어떻게 되나요? MCU 쪽 runtime과 Linux 쪽 runtime을 누가 소유하나요?
3. Flight stack은 Betaflight fork 기반인가요, 자체 스택인가요? 제품군(Archer, Archer AI, Bandit, GCS, radio)마다 fork가 갈라진 정도는 어느 정도인가요?
4. 필드(우크라이나 등)의 로그와 telemetry는 지금 어떻게 회수·분석되나요? 플랫폼 팀이 그 데이터 파이프라인에도 관여하나요?
5. 엔지니어가 flight test나 생산 라인과 얼마나 가깝게 일하나요? 온사이트 요구사항, 그리고 US Person/ITAR 요건은 어떻게 되나요? (리크루터 단계에서)

### 4.8 준비 체크리스트
- [ ] **(결정)** US Person 요건 충족 여부, Torrance 이주 가능 여부, 방산 strike drone 미션 수용 여부 → 셋 중 하나라도 NO면 ⏸
- [ ] **Bazel 3-타깃 cross-compile 미니 프로젝트** (Cortex-M4 + aarch64 Linux + host test, logging/telemetry C 라이브러리, reproducibility 해시 검증) → GitHub 공개 · 레쥬메 Projects에 추가
- [ ] 레쥬메 튜닝 (3.5) 반영한 Neros 버전 PDF 만들기
- [ ] **시스템 설계 1문항 완성본**: "MCU+Linux+radio+GCS 통합 logging/telemetry/config" 화이트보드 버전 (스키마 IDL → codegen, ring buffer, framing, 시간 동기, 버전 호환, 대역폭 degrade)
- [ ] **`practice/notes/00_drone_architecture.md` §1~§2** — 드론 전체 블록도 + 제어 루프 타이밍 예산. **가장 먼저**
- [ ] **이 포지션 전용 C 드릴 — `practice/` (7세트 42문제 · 123체크, 전부 검증됨)**
  - [ ] `make prob N=02_telemetry_framing` — COBS+CRC+재동기화 파서. **1순위**
  - [ ] `make prob N=07_sensors_actuators` — IMU 디코드·필터·CRSF 언팩·DShot·quad-X 믹서 (드론 도메인 갭)
  - [ ] `make prob N=01_ring_logging` — SPSC 링 + deferred-format 로그 레코드
  - [ ] `make prob N=05_embedded_core` / `N=06_isr_timing` — 임베디드 기본기 복습 (폰스크린 단골)
  - [ ] `make prob N=04_ipc_budget` — 풀 allocator, 메시지 큐, token bucket, 우선순위 대역폭 분배
  - [ ] `make prob N=03_config_store` — TLV, 버전 마이그레이션, A/B 슬롯 원자적 커밋
- [ ] `practice/notes/00_drone_architecture.md` §9 **Top 15 리서치 항목** 소화 (ELRS 링크버짓, RPM 필터, PID 캐스케이드, GPS-denied, Board A/B 변종 관리 등)
- [ ] C 드릴 (워크스페이스 기존 자료 재사용):
  - [ ] `anduril_firmware_engineer/` **05 circular buffer (SPSC)** · **04 memory (pool)** · **08 data processing (CRC/fixed-point)** · **09 rtos** · **10 anduril signature (UART ring buffer, CRC-8 telemetry, IMU filter)** → `make prob N=05` 등, 45분 모의고사 탭 1~2회
  - [ ] `don-c-prac-master/concurrency_prac/` (lock-free, critical section)
  - [ ] 신규 연습 3개: COBS 인코더/디코더, TLV 직렬화 + 버전 필드, flash config store (CRC + A/B)
- [ ] ABI/API 안정성 패턴 정리 (opaque handle, versioned struct, semver, deprecation)
- [ ] Linux IPC 기초 복습 (shm, UDS, `mmap`, `eventfd`) + Buildroot QEMU 이미지 1회 빌드
- [ ] Betaflight 소스 훑기: `src/main/pg/` (parameter groups), `blackbox/`, `scheduler/`, `msp/`, `rx/crsf.c` / PX4 uORB·MAVLink 개념
- [ ] 딥다이브 STAR 6개 문장화 (4.4 굵은 글씨 우선: telemetry, error handling API, test SDK, Apple 크로스바운더리 버그)
- [ ] "Why leave Apple after 9 months" · "Why defense" 답변 소리 내어 연습
- [ ] (선택) FPV 시뮬레이터 몇 시간 체험

---

## 5. 진행 로그

| 날짜 | 이벤트 | 메모 |
|---|---|---|
| 2026-09-14 | 컨텍스트 파일 생성 (JD 조사) | Greenhouse API로 JD 원문 확보 (JD 갱신일 2026-09-11). 형제 공고 90개를 스캔해 스택 정황 파악 |
| 2026-09-15 | **리크루터 Devin 연락 · 30분 폰 인터뷰 제안** | "Principal/Senior Embedded Software Engineer" 표기 (공고 미존재 → req 확인 필요). 캘린더 링크로 일정 제출. 2026-09-16 진행 예정 |
| 2026-09-17 | Gemini 아키텍처 리서치 저장 | `2026-09-17_neros_low_latency_architecture_whitepaper_gemini.md` (+html). 원문 보존 + 검토 노트: 링버퍼 코드 컴파일 불가, overwrite 정책의 SPSC 위반, ISO 26262·Board B·250km/h 미검증, 경험 범위 밖 제안 표시 |
| 2026-09-18 | **Adam 인터뷰 D-6h 준비 노트** 작성 | `2026-09-18_adam_interview_D-6h_prep.md` (+html). 6시간 타임라인, 스토리 A·B·C 채우기 체크리스트, 예상 질문 12개, 치트시트. 기간 표기 불일치 주의: 레쥬메 약 7년 1개월 vs Devin에게 말한 7년 3개월 → Adam에겐 "about seven years" |
| 2026-09-17 | Gemini 리서치 **검증판 + 한국어판** 작성 | `2026-09-17_..._whitepaper_verified.md` / `_verified_kor.md` (+html). 항목별 ✅Verified·✏️Modified·⚠️Unverified 태그, 변경 내역 13건. 링 버퍼·seqlock 코드는 md에서 추출해 `-Wpedantic`·ASan/UBSan 통과 확인 |
| 2026-09-16 | **리크루터 Devin 콜 완료 (30분)** | 회사 ~300명(절반 이상 공장 인력), FW 12명 + Director. 포지션 "Principal/Senior Embedded SWE"는 **특정 JD 없음**. Don이 base **$200K 이상** 기대 전달. 다음: Adam 45분 기술 → 온사이트(Tour 30m · 발표 1h · 1:1 C/C++·ring buffer·generic system design). Don이 말한 이직 스토리는 HM 준비 노트 1.1에 원문 보존 |
| 2026-09-16 | HM 준비 노트 작성 | `neros_hm_adam_technical_prep_2026-09-18.md`. Adam = 자동차 임베디드 플랫폼 아키텍트 출신 (JCI IPC·EOL 테스트, Visteon 모듈형 플랫폼, Faraday·Aeris OTA/보안), 2025-05 Neros Director of Firmware 합류. LinkedIn은 열람만 함 |
| 2026-09-16 | **전용 C 연습 세트 생성** | `practice/` — 4세트 24문제 70체크 (ring/logging, telemetry framing, config store, IPC/budget). Anduril 세트 포맷 그대로(`make prob/sol/test`), 전 solution 경고 0·전 체크 PASS 검증 |
| 2026-09-15 | 폰스크린 벼락치기 시트 작성 | `neros_phone_screen_prep_2026-09-16.md`. Don이 받은 CEO/CTO 영상 요약 중 **"CEO Saurin Shah"는 오류** — 실제 CEO는 Soren Monroe-Anderson (CTO Olaf Hichwa). Board B·Poka-yoke·Bandit 250km/h 등은 [영상요약·미검증]으로 분류 |

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

1. https://www.prnewswire.com/news-releases/neros-raises-250m-series-c-at-2-5b-valuation-to-scale-autonomous-and-interceptor-drone-programs-302848736.html — Series C 보도자료 (확인일 2026-09-14)
2. https://www.bloomberg.com/news/articles/2026-08-11/defense-startup-neros-triples-valuation-to-2-5-billion · https://www.therobotreport.com/neros-technologies-raises-250m-to-deploy-its-defense-drones-by-the-end-of-2026/ — Series C·배치 계획 (2026-09-14)
3. https://www.sourcery.vc/p/breaking-inside-neros-factory — Torrance 공장, 인원, 창업자 (2026-09-14)
4. https://en.wikipedia.org/wiki/Neros — 창업, 제품, 누적 펀딩, 생산량, 계약, 제재 (2026-09-14)
5. https://www.govconwire.com/articles/neros-500m-army-archer-fpv-contract · https://defence-blog.com/from-garage-to-pentagon-neros-wins-500m-u-s-army-drone-deal/ — Army $500M IDIQ, 대당 가격, 주당 1,200대 (2026-09-14)
6. https://boards-api.greenhouse.io/v1/boards/nerostechnologies/jobs?content=true — Neros 전체 공고 90개 (밴드, 위치, 스택 키워드, ITAR 문구) (2026-09-14)
7. https://www.glassdoor.com/Interview/Neros-Interview-Questions-E10732288.htm — 인터뷰 후기 (45분 기술 폰스크린). 검색 스니펫 기준, 본문은 403 (2026-09-14)
8. https://job-boards.greenhouse.io/nerostechnologies/jobs/5095368007 등 — ITAR "U.S. Person" 문구가 있는 형제 공고 (2026-09-14)
9. https://www.defensedaily.com/neros-500-million-army-deal-could-cover-hundreds-of-thousands-of-archer-fpv-drones/army/ — PBAS 규모 (2026-09-14)
10. https://www.businesswire.com/news/home/20251106525503/en/U.S.-Army-Selects-Neros-Archer-FPV-and-Flatbow-Ground-Control-System-for-Purpose-Built-Attritable-Systems-PBAS-Program — PBAS 선정, Flatbow GCS (2026-09-14)
11. https://www.neros.tech/ — 공식 사이트 (in-house 라디오, flight computer, Crossbow GCS) (2026-09-14)
12. https://www.army-technology.com/news/neros-us-army-fpv-contract/ · https://soldiersystems.net/2025/12/06/neros-secures-multi-million-marine-corps-contract-for-archer-strike-fpv-drones/ — PBAS Tranche 1, USMC 계약 (2026-09-14)
13. https://www.youtube.com/watch?v=a8sFzL-E948 — "Toyota of Defense" 인터뷰 (2026-09-14)
14. https://www.neros.tech/articles/neros-closes-75m-series-b-fundraise-led-by-sequoia-capital — Series B (2026-09-14)
15. https://www.therobotreport.com/red-cat-wins-u-s-army-next-gen-drone-contract-over-skydio/ · https://www.modalai.com/pages/2026-u-s-drone-manufacturers-comprehensive-list — 경쟁 구도 (2026-09-14)
16. https://www.glassdoor.com/Reviews/Neros-Reviews-E10732288.htm — 리뷰 4.9/5, WLB 4.4 (2026-09-14)

---

## 부록 A. JD 원문 (2026-09-14 수집 · Greenhouse API, updated_at 2026-09-11)

<details>
<summary>펼치기</summary>

**Senior Firmware Engineer, Platform** — Torrance, California, United States — Department: Firmware/Embedded Engineering

Who we are

Neros is a defense technology company rebuilding America’s drone industrial base. We design and manufacture high-performance unmanned systems that are tested in combat, iterated at startup speed, and built at massive scale. Our team culture is fast, hands-on, and obsessed with closing the gap between design and deployment.

As drones transform the character of warfare, Neros is delivering the systems the West needs to compete on the modern battlefield and deter the adversaries of democracy. We’re hiring engineers, operators, and builders who want to move fast, take on extreme ownership, and get capability into the hands of warfighters in months, not years.

What you will be doing

Neros builds first-person-view drones for the modern battlefield. As a Senior Firmware Engineer on the Platform team, you will own the common runtime, libraries, SDK, and build system that our flight, ground-station, and autonomy software teams build on — across microcontroller and Linux targets alike. Your customers are other engineers, and the hardest problems in this space are the ones that surface where their code meets the platform.

Responsibilities

- Own the common runtime and libraries every firmware team consumes — logging, telemetry, IPC, configuration — on MCU-based systems and Linux targets
- Own the build system and cross-compile toolchain: fast, reproducible, hermetic builds across every target we ship, and the SDK other teams consume
- Design and maintain the interfaces other teams build on, keeping them stable as the platform evolves underneath
- Solve the hard, ambiguous problems that surface where the platform meets its consumer teams
- Drive resource trade-offs — compute, memory, bandwidth — when subsystems compete for the same budget
- Raise the firmware engineering bar through patterns, code review, and mentoring

You should have the following

- Mastery of embedded C on shipped products, with fluency across both MCU/RTOS and embedded Linux environments
- Have built platform code that other engineers depended on — libraries, SDKs, or protocols consumed by multiple teams — and kept them stable through change
- Hands-on ownership of an embedded build system (e.g. Bazel) — cross-compilation across multiple targets, dependency management, and build reproducibility
- Track record of owning problems that spanned team or subsystem boundaries and driving them to shipped resolution
- Strong resource-constrained design judgment — compute, memory, and bandwidth trade-offs on real hardware
- Debugging mastery — a history of cracking the bugs others gave up on
- Hands-on: you still ship code, and your technical influence comes from the work

Nice to have

- Bazel specifically, especially for embedded/cross-compiled targets — or migrating a firmware org onto it
- Embedded Linux platform internals — kernel, BSPs, Yocto/Buildroot — alongside MCU work
- Flight-stack experience — Betaflight, PX4, or similar
- DSP, comms, or RF-adjacent firmware
- Visible open-source systems work
- You fly — FPV, RC, or drones generally

US Salary Range

$195,000 – $273,000 USD

The salary range for this role is an estimate based on a wide range of compensation factors, inclusive of base salary only. Actual salary may vary based on (but not limited to) work experience, education and/or training, critical skills, and/or business considerations. Highly competitive equity grants are considered part of Neros' total compensation package.

We’re an equal opportunity employer. We welcome all applicants without attention to race, color, religion, sex, sexual orientation, gender identity, national origin, veteran or disability status.

</details>
