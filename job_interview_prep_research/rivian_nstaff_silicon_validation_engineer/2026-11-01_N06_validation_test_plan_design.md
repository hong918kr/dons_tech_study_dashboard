# N06 · 검증 방법론 · 테스트 플랜 설계 — "이 블록 테스트 플랜 짜 봐"를 10분에 말하기
<!-- theme: pastel -->

> **왜**: 온사이트 Technical 2(도메인)에서 **"Design a test plan for X"** · "검증 끝난 걸 어떻게 아나" · "FPGA에선 되는데 실리콘에선 안 된다" 류 화이트보드 질문이 나올 확률 높음 [추정 — 면접관 Manjiang 경력: FPGA/HAPS 프로토타이핑 · post-si validation · PVT · production SLT] · **형식**: 인덱스(이 페이지) + 하위 페이지 **N06A–N06I 9개** · **재사용**: 이미 있는 [LP14 PCIe 검증 방법론](2026-10-31_LP14_validation_methodology.html) · [BV14 보드 검증 계획·보드 팜](2026-11-01_BV14_board_validation_plan_automation.html) · [BV01 큰 그림](2026-11-01_BV01_si_board_validation_big_picture.html) · [BV12 shmoo/PVT](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · [deck §5.1·§6·§7](2026-10-25_rivian_hm_screen_prep.html) — 여기선 **블록 무관 템플릿 + 블록별 연습 + 방법론 Staff 시그널**에 집중 · **원본**: [온사이트 플랜 N06](2026-10-31_rivian_onsite_plan.html) · **코드**: onsitePrep/n06/ · **상태**: 11-01 N06A–N06I 전부 완성 (md 7.3k줄, 코드 onsitePrep/n06/a–h 전부 실행·PASS, 링크 0 broken) · **작성 규칙**: onsitePrep/N06_NOTES_SPEC.md

## 0. TL;DR

- **9개 하위 페이지**: A 방법론 기초·공통 템플릿 · B 커버리지·통계·종료 기준 · C 연습① 저속 peripheral·SoC 기반 블록 · D 연습② 데이터 이동·메모리 · E 연습③ 고속 I/O·센서 · F 연습④ AI 가속기·SoC 레벨·안전 · G 플랫폼·회귀·자동화(pre-si → silicon 재사용) · H 디버그 방법론·실패 triage · I 모의 면접 10분 말하기 Q 뱅크.
- **외울 한 줄 (모든 테스트 플랜 공통 뼈대)**: **Scope·clarify → 기능 목록(spec → feature → requirement ID) → 카테고리 매트릭스(레지스터 · 기능 · 인터럽트 · 에러/negative · 리셋/전원 · 성능 · PVT/margin · stress/concurrency · 시스템) → 우선순위(P0 bring-up / P1 기능 / P2 특성화) → 플랫폼(FPGA / 실리콘 / 둘 다) → 통과 기준·커버리지 → 자동화·회귀 → 리스크**.
- **블록별 연습(C–F)** 은 모두 같은 틀: 🔰 블록이 뭔지 → clarify 질문 → **테스트 매트릭스 표** → P0/P1/P2 → 장비·셋업 → 통과 기준 → 꼬리 질문 → 10분 영어 답 뼈대. 블록을 바꿔도 **틀은 같고 칸만 바뀐다**는 걸 몸에 익히는 게 목적.
- **★ 먼저 할 것** (온사이트 확률 높음): **N06A 템플릿** → **N06C I2C 컨트롤러** · **N06D DMA 엔진 · DDR 컨트롤러** · **N06E PCIe(요약) · MIPI CSI-2 카메라 파이프라인** · **N06F NPU** → **N06H 디버그 6단계** → **N06I 모의 3회**.
- Staff 시그널 3가지: ① **"DV가 이미 시뮬레이션에서 덮은 것은 반복하지 않고, 실리콘에서만 볼 수 있는 것에 집중"** ② **종료 기준을 시작 전에 합의** ③ **테스트를 한 번 쓰고 FPGA·에뮬·실리콘·SLT에서 재사용**.

## 0.5 🔰 처음 보는 사람을 위한 큰 그림 — "테스트 플랜"이 뭔가

- 비유: **신차 출시 전 점검표**. 엔진·브레이크·에어백(블록)마다 "무엇을, 어떤 조건(더위·추위·빗길)에서, 몇 대로, 어디까지 되면 합격"을 미리 적어 둔 문서. 점검표 없이 차를 몰아 보는 건 "테스트"가 아니라 "구경".
- 칩에서도 같다: 설계 스펙(spec) → **기능(feature) 목록** → 기능마다 **테스트 항목(ID)** → 어디서 돌릴지(FPGA 프로토타입 / 실제 실리콘 / 양산 SLT) → 합격 기준 → 결과를 모아 **커버리지**(스펙 중 몇 %가 증거로 확인됐나).

```text
 spec / 설계 문서 ──► feature 목록 ──► requirement ID ──► test 항목 (카테고리 × 조건)
                                                              │
        FPGA/에뮬 (기능)  ◄── 같은 C 테스트 (HAL) ──►  실리콘 (타이밍·아날로그·PVT)  ──► SLT (양산)
                                                              │
                        결과 DB (chip ID · board · V/F/T · build hash · pass/fail · log)
                                                              │
                 커버리지 · 트렌드 · 버그 → triage (HW? SW? board? test?) → 종료 기준 → sign-off
```

- **면접관이 보는 것** [추정]: 빠짐없는 카테고리(특히 에러·리셋·PVT를 빼먹지 않는지), 우선순위 판단, "끝났다"의 정의, 실패했을 때 HW/SW/테스트를 가르는 방법, 그리고 자동화로 규모를 키우는 생각.

## 1. 페이지 목록 한눈에

| ID | 페이지 | 핵심 질문 | 우선 | 코드 |
|---|---|---|---|---|
| **N06A** | [방법론 기초 · 공통 테스트 플랜 템플릿](2026-11-01_N06A_methodology_test_plan_template.html) | "How do you write a validation plan for a new IP block?" | P0 ★ | n06/a |
| **N06B** | [커버리지 · 메트릭 · 통계 · 종료 기준](2026-11-01_N06B_coverage_metrics_exit_criteria.html) | "How do you know you're done?" | P0 | n06/b |
| **N06C** | [연습① 저속 peripheral · SoC 기반 블록](2026-11-01_N06C_plans_peripherals_soc_infra.html) | "Test plan for an I2C controller / timer / interrupt controller" | P0 ★ | n06/c |
| **N06D** | [연습② 데이터 이동 · 메모리](2026-11-01_N06D_plans_dma_memory.html) | "Test plan for a DMA engine / DDR controller" | P0 ★ | n06/d |
| **N06E** | [연습③ 고속 I/O · 센서](2026-11-01_N06E_plans_highspeed_io_sensor.html) | "Test plan for PCIe / Ethernet / camera pipeline" | P0 ★ | n06/e |
| **N06F** | [연습④ AI 가속기 · SoC 레벨 · 안전](2026-11-01_N06F_plans_npu_soc_safety.html) | "How would you validate our NPU?" | P0 ★ | n06/f |
| **N06G** | [플랫폼 · 회귀 · 자동화 — pre-si에서 silicon까지 재사용](2026-11-01_N06G_platforms_regression_automation.html) | "Same test on FPGA, emulator, silicon — how?" | P1 | n06/g |
| **N06H** | [디버그 방법론 · 실패 triage](2026-11-01_N06H_debug_methodology_triage.html) | "A test fails 1 in 1000 on silicon. Walk me through it." | P0 | n06/h |
| **N06I** | [모의 면접 — 10분 말하기 Q 뱅크](2026-11-01_N06I_mock_10min_qbank.html) | 프롬프트 30개 + 채점표 | P1 | — |

## 2. 페이지별 상세 목차

### N06A · 방법론 기초 · 공통 테스트 플랜 템플릿 (P0 ★)

- 검증 용어 5개 정확히: **verification(DV, 설계가 스펙대로인가 — 시뮬레이션) vs validation(실리콘이 실제 시스템에서 의도대로인가) vs characterization(얼마나 여유가 있나 — PVT/shmoo) vs qualification(신뢰성 — 수명·온도 스트레스, AEC-Q100) vs production test(ATE·SLT — 이 개체가 불량인가)** → [BV01 §1.3](2026-11-01_BV01_si_board_validation_big_picture.html) 링크, 여기선 "테스트 플랜에서 무엇이 달라지나"만.
- 칩 일정 위에서 검증 단계: 스펙 리뷰 → pre-si(시뮬·에뮬·FPGA) 테스트 작성 → A0 bring-up → 기능 validation → 특성화(PVT) → stepping(B0)·errata → SLT 콘텐츠 이관.
- **공통 테스트 플랜 템플릿** (문서 목차): 범위/비범위 · 참고 문서 · feature 목록 + requirement ID · 테스트 카테고리 매트릭스 · 조건(전압·주파수·온도·샘플 수) · 플랫폼 매핑 · 우선순위 · 통과 기준 · 장비/셋업 · 의존성(FW·보드·툴) · 리스크·완화 · 일정 · 종료 기준 · 리포팅.
- **카테고리 12종 체크리스트** (블록 무관): ① 레지스터(reset 값·RW·RO·W1C·reserved) ② 기본 기능(normal path) ③ 설정 조합(모드·폭·속도 매트릭스) ④ 경계·corner(최소/최대 크기, wrap, 0) ⑤ 인터럽트(발생·mask·clear·coalescing) ⑥ 에러·negative(잘못된 설정, 오류 주입, timeout) ⑦ 리셋·전원 상태(동작 중 reset, suspend/resume, clock gating) ⑧ 동시성·stress(다른 블록과 동시, 버스 경합) ⑨ 성능(throughput·latency·utilization) ⑩ PVT·margin ⑪ 장시간 soak·reliability ⑫ 시스템/interop(실제 SW 스택·실제 상대 장치).
- **우선순위 정하는 법**: bring-up 차단 여부 → 고객 영향 → 새로 바뀐 RTL(변경 diff) → DV가 못 덮는 영역(아날로그·타이밍·전원) → 과거 errata 이력.
- **"DV와 중복 피하기"** 표: DV가 잘하는 것 vs 실리콘에서만 보는 것.
- **10분 말하기 7단계 프레임** (영어 문장 템플릿 포함): ① clarify 30초 ② 블록 그림 1분 ③ 카테고리 매트릭스 3분 ④ 우선순위·플랫폼 1분 ⑤ 통과 기준·커버리지 1분 ⑥ 자동화·회귀 1분 ⑦ 리스크·"what I'd do first on A0" 1분 + 남은 시간 꼬리 질문.
- 꼬리 질문 체인: "How do you write a validation plan?" → "How do you prioritize with 3 weeks?" → "What do you NOT test?" → "How do you get the designer to review it?"
- 코드(선택): 테스트 플랜 매트릭스 생성기 — feature 목록 → (feature × 카테고리 × 조건) 테스트 ID 표 자동 생성 (Python).

### N06B · 커버리지 · 메트릭 · 통계 · 종료 기준 (P0)

- 커버리지 종류: **requirement/feature coverage**(요구사항 중 통과 증거가 있는 비율) · **register coverage**(필드별 R/W 확인) · **condition/cross coverage**(feature × PVT 코너 × 모드) · **code coverage(테스트 FW·드라이버)** — 실리콘 검증에서는 functional이 주, code는 보조 · DV의 functional coverage(covergroup)와의 관계(이름만, UVM 몰라도 됨).
- **Traceability**: spec 문단 → requirement ID → test ID → 최신 결과(chip·board·조건) — "어떤 요구사항이든 증거까지 3클릭".
- 통계: **rule of three**(n번 0 실패 → 95% 신뢰 상한 ≈ 3/n), 실패율 추정, 몇 번/몇 개 칩/몇 장 보드, 코너 칩(SS/FF), 연속 통과 vs 무작위 표본 → [LP14 §2.5](2026-10-31_LP14_validation_methodology.html) 링크하고 여기선 **계산 연습 문제 4개**.
- 메트릭: pass rate per build, 버그 발견/수정 추세(find rate가 꺾였나), open 버그 severity 분포, 테스트 실행 시간, flaky 비율, 블록별 커버리지 히트맵, 보드 가동률.
- **종료 기준(exit criteria) 예시**: P0/P1 요구사항 100% 실행·통과(또는 waiver), open S1/S2 버그 0, 모든 errata에 workaround 확인, PVT 코너 전부 margin 기준 충족, 회귀 N일 연속 안정, 리포트 리뷰 sign-off.
- "커버리지 100%인데 버그가 나왔다" — 커버리지가 거짓말하는 경우(체크가 실제로 HW를 안 읽음, 조건 조합 누락, 테스트가 실패를 못 함) → negative control.
- 주간 리포트 1장 템플릿 → [BV14 체인 E](2026-11-01_BV14_board_validation_plan_automation.html) 링크.
- 코드: mock 결과 CSV → 블록별 requirement coverage · pass rate · rule-of-three 상한 · 종료 기준 판정 리포트 (Python stdlib, 실제 출력).

### N06C · 연습① 저속 peripheral · SoC 기반 블록 (P0 ★)

블록마다 같은 틀(§3 참고). 블록 6개:
- **C1 I2C controller** ★ (master/slave, 100k/400k/1M, clock stretching, NACK, arbitration lost, bus stuck recovery, 10-bit 주소, FIFO, 인터럽트, 실제 센서/EEPROM interop, protocol analyzer로 타이밍 tHD;STA 등 측정)
- **C2 SPI controller** (CPOL/CPHA 4모드, CS 타이밍, FIFO depth, DMA 연동, 최대 클럭, loopback, flash interop)
- **C3 UART** (baud 매트릭스·오차 허용, parity/stop, framing/overrun 에러, flow control, FIFO 트리거, loopback)
- **C4 GPIO · pinmux** (방향·pull·drive, 인터럽트 edge/level, debounce, pinmux 충돌, 전원 도메인 별 IO 전압)
- **C5 Timer · watchdog · RTC** (주기 정확도 vs 레퍼런스, wrap, prescaler, watchdog 만료 → reset reason, 저전력 중 동작)
- **C6 Interrupt controller (GIC 류)** ★ (라우팅·우선순위·mask, edge vs level, SPI/PPI/SGI, 동시 다발, latency 측정, affinity)

### N06D · 연습② 데이터 이동 · 메모리 (P0 ★)

- **D1 DMA engine** ★ (deck §6.2 확장: 레지스터 → single → 크기·정렬 매트릭스 → descriptor chain·scatter-gather → ring wrap → 채널 동시성·우선순위 → 에러(잘못된 주소·abort·정렬 위반) → 인터럽트/coalescing → cache coherency → bandwidth/latency → PVT 데이터 무결성) → [V3](hmScreenPrep/problems/V3_dma_desc_ring/index.html)
- **D2 DDR/LPDDR memory controller** ★ (init·training 성공률, 메모리 테스트 패턴, refresh·self-refresh, 주파수 변경(DFS), ECC, 온도 보정, bandwidth 효율, 동시 master QoS, margin shmoo) → [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html) 링크하고 여기선 **테스트 플랜 형태로 재배열**
- **D3 On-chip SRAM · ECC · parity** (MBIST vs SW 테스트, ECC 1-bit 정정/2-bit 검출, 오류 주입 레지스터, scrubbing, 에러 카운터·인터럽트)
- **D4 Cache · coherency · interconnect (NoC/AXI)** (coherent vs non-coherent master, cache maintenance, ordering, QoS/arbitration, timeout/decode error, 대역폭 경합)
- **D5 IOMMU/SMMU · 메모리 보호** (매핑·fault 보고·권한, 보안 영역 접근 거부) — 짧게

### N06E · 연습③ 고속 I/O · 센서 (P0 ★)

- **E1 PCIe controller (RC/EP)** — [LP14](2026-10-31_LP14_validation_methodology.html)의 12영역을 **한 장 테스트 매트릭스 + 10분 답**으로 압축만 (새 내용 최소)
- **E2 Ethernet MAC + PHY (automotive: 100BASE-T1/1000BASE-T1, RGMII/SGMII)** ★ (링크 업·autoneg/master-slave, MDIO PHY 레지스터, loopback 단계(MAC/PCS/PHY/외부), PRBS·BER, RFC 2544 throughput/latency/frame loss, 64B~jumbo, checksum offload, VLAN, TSN/PTP 타임스탬프 정확도, 케이블 진단)
- **E3 MIPI CSI-2 receiver + ISP 카메라 파이프라인** ★ (D-PHY/C-PHY 레인·속도, HS settle, virtual channel·data type, 프레임/라인 카운터, CRC/ECC 에러, 다중 카메라 동시, 버퍼 언더런, ISP 출력 golden 비교, 프레임 drop·latency, 실제 센서 vs 패턴 생성기)
- **E4 시간 동기 · 센서 timestamp** (PTP/gPTP, PPS discipline, 카메라 FSYNC, 하드웨어 timestamp 정확도 측정, 카메라-LiDAR 정렬) — 면접관 특허 분야(sensor timestamp/sync, clock sync) [확인됨: deck §1.2] → [L4](hmScreenPrep/problems/L4_pps_time_discipline/index.html)

### N06F · 연습④ AI 가속기 · SoC 레벨 · 안전 (P0 ★)

- **F1 NPU / AI accelerator** ★ (기능: 연산자별 golden 비교(INT8/FP16 bit-exact vs 허용 오차), 타일 경계, 메모리 레이아웃; 성능: MAC utilization·TOPS/W·roofline; 동시성: 여러 job·DDR 경합; 전원: DVFS·thermal throttling; 오류: ECC·watchdog·hang 복구; 실제 네트워크 end-to-end 정확도) → deck §6.3, [V11](hmScreenPrep/problems/V11_npu_int8_golden/index.html)
- **F2 전원 관리 · DVFS · thermal** (전원 도메인 on/off 시퀀스, retention, DVFS 전환 중 트래픽, thermal sensor 정확도, throttling 동작, suspend/resume N천 회)
- **F3 Secure boot · crypto engine** (서명 검증 성공/실패 경로, 키 폐기, 디버그 포트 lock, 암호 엔진 KAT(known-answer test), 성능)
- **F4 Functional safety (ISO 26262) 안전 메커니즘 검증** (ECC·lockstep·BIST·watchdog·CRC의 **fault injection으로 "고장을 잡는지"** 확인, 진단 커버리지, 안전 반응 시간) — 이름·개념 수준 [추정 깊이]
- **F5 SoC 레벨 시스템 시나리오 · SLT 콘텐츠** (카메라 N개 → ISP → NPU → 출력 end-to-end, 최대 부하 동시성, 부팅 N천 회, SLT로 줄여 넣을 테스트 고르기)

### N06G · 플랫폼 · 회귀 · 자동화 (P1)

- 플랫폼 4종(시뮬레이션·에뮬레이터·FPGA 프로토타입·실리콘) × 무엇을 검증하나 → [deck §5.1 Q2·Q3](2026-10-25_rivian_hm_screen_prep.html) 링크, 여기선 **"테스트를 한 번 쓰고 모든 플랫폼에서"** 구현 방법: HAL, 플랫폼별 timeout 스케일, 주소 맵 차이, FPGA에 없는 블록 stub.
- **bare-metal 테스트 프레임워크 구조 (C)**: 테스트 등록 테이블, setup/run/teardown, timeout, 결과 라인 포맷(`RESULT <id> PASS ...`), seed, 반복, 결과를 UART/공유 메모리로.
- 회귀 계층: smoke(빌드마다 10분) · nightly · weekly full · PVT 캠페인 · ECO 회귀 범위.
- flaky 정책, 자동 bisect, 실패 signature dedup, "보드 고장이 테스트 실패처럼 보이면 안 된다"(health check) → [BV14 §2.10–2.11](2026-11-01_BV14_board_validation_plan_automation.html) 링크.
- FPGA vs silicon 차이 표(타이밍·아날로그·메모리 모델·리셋·초기값) + "FPGA는 통과, 실리콘은 실패" 사고 순서 (deck §6.4 확장).
- **Test of the test**: negative control, 오류 주입, mutation(체크를 일부러 틀리게), golden 모델 신뢰성.
- 코드: 미니 bare-metal 스타일 C 테스트 러너(테이블 등록·timeout·결과 라인·fault injection 플래그) + Python 결과 파서·flaky 분류기 (실제 출력).

### N06H · 디버그 방법론 · 실패 triage (P0)

- **디버그 6단계**: Reproduce → Localize(어느 계층·칩·조건) → Hypothesize → Instrument(scope·analyzer·카운터·GPIO 트리거·trace) → Fix/workaround → Regress(재발 방지 테스트 추가).
- **HW vs SW vs board vs test 가르기** 판단표(칩 swap, 보드 swap, 전압/주파수 의존성, FPGA 재현, 이전 stepping 비교).
- **간헐 실패(1-in-1000)**: 빈도 올리기(stress), 상관관계(칩·보드·온도·빌드), 실패 순간 상태 캡처, 이분 탐색, 통계로 "고쳐졌다" 증명(rule of three).
- **버그 리포트 잘 쓰기**(제목·환경·재현 단계·기대/실제·빈도·로그·가설) + 영어 예시 1개.
- **Errata 수명주기**: 발견 → root cause → workaround(FW/드라이버) → 문서화 → 다음 stepping 수정 확인.
- 크로스팀 escalation(design·DV·board·SW·product) — Staff 시그널 문장.
- 시나리오 드릴 6개는 **새로운 것만** (기존 [deck §7](2026-10-25_rivian_hm_screen_prep.html) 16개 · [BV13](2026-11-01_BV13_board_debug_scenarios.html) 12개 · [LP15](2026-10-31_LP15_debug_scenarios.html)와 안 겹치게 — 예: NPU 결과가 온도 따라 달라짐, 회귀가 빌드 X부터 깨짐(bisect), 한 보드에서만 부팅 시간 2배, 측정값이 장비마다 다름, 테스트가 디버그 빌드에선 통과·릴리스 빌드에선 실패, 동시 실행 시만 데이터 손상).
- 코드(선택): 빌드 bisect 시뮬레이터 또는 실패율 확인용 계산 (Python, 실제 출력).

### N06I · 모의 면접 — 10분 말하기 Q 뱅크 (P1)

- 프롬프트 30개(블록 테스트 플랜 12 · 방법론 8 · 디버그 6 · Staff/리더십 4), 각각: English Q → 꼬리 3단 → `<details>` 60–90초 영어 답 뼈대 + 링크(N06A–H).
- **채점표**(카테고리 누락 체크 · 우선순위 · 통과 기준 · 플랫폼 · 자동화 · 리스크 · 시간).
- 모의 세트 3개(각 45분 = 테스트 플랜 1 + 방법론 1 + 디버그 1).
- 연습 기록 표(날짜 · 프롬프트 · 걸린 시간 · 빠진 카테고리).

## 3. 블록 연습 페이지(C–F)의 공통 틀

각 블록은 `## <ID> · <블록 이름>` 하나이고 안의 순서는 항상 같다 (자세한 건 spec):

| 순서 | 섹션 | 내용 |
|---|---|---|
| 0 | 🔰 이 블록이 뭔가 | 비유 → 정확한 정의 → ASCII 블록 그림 → 용어 표 |
| 1 | 먼저 물어볼 것 (clarify) | 영어 질문 4–6개 + 가정한 답 |
| 2 | 테스트 매트릭스 | 카테고리 12종 중 해당 행 · 테스트 항목 · 조건 · 플랫폼(FPGA/Si) · 통과 기준 · 우선순위 |
| 3 | 우선순위 · A0 첫 주 | P0 bring-up 3–5개, P1, P2 |
| 4 | 장비 · 셋업 · 관찰 지점 | analyzer/scope/loopback/상대 장치, 레지스터·카운터·로그 |
| 5 | 이 블록에서 잘 터지는 버그 | 실리콘/통합에서 실제로 흔한 실패 유형 |
| 6 | 10분 영어 답 뼈대 | 7단계 프레임에 맞춘 문장 |
| 7 | 꼬리 질문 | `<details>` 영어 답 + 한국어 핵심 |
| 8 | Don 연결 | 이력 기반 문장, 숫자는 `[숫자 채우기]` |

## 4. 진행 순서 제안 (D = 온사이트)

| 시점 | 할 것 | 연습 |
|---|---|---|
| 1일차 | N06A 템플릿·7단계 프레임 외우기 | I2C(C1)로 10분 말하기 1회 (녹음) |
| 2일차 | N06D D1 DMA · D2 DDR | DMA 10분 말하기 |
| 3일차 | N06E E3 카메라 · E2 Ethernet, E1 PCIe 요약 | 카메라 파이프라인 10분 |
| 4일차 | N06F F1 NPU · F5 SoC 레벨 | NPU 10분 |
| 5일차 (주말) | N06B 커버리지·종료 기준 · N06G 플랫폼·회귀 · N06H 디버그 6단계 | **N06I 모의 세트 1** |
| 6–7일차 | 남은 블록(C2–C6, D3–D5, E4, F2–F4) 매트릭스만 훑기 | **모의 세트 2·3** |
| D-2 | 7단계 프레임 + 카테고리 12종 한 장 요약만 | 아무 블록 1개 즉석 10분 |

## 5. 기존 자료와 겹치는 부분 (중복 피하기)

| 주제 | 이미 있는 곳 | N06에서는 |
|---|---|---|
| 검증 용어·칩 일정·ATE/SLT | [BV01](2026-11-01_BV01_si_board_validation_big_picture.html), [deck §5.1 Q1–Q5](2026-10-25_rivian_hm_screen_prep.html) | N06A: 테스트 플랜에 미치는 영향만 |
| PCIe 검증 12영역·통계·EP framework·하네스 | [LP14](2026-10-31_LP14_validation_methodology.html) | N06E E1: 한 장 압축 + 10분 답 |
| 보드 검증 계획·보드 팜·HW/SW triage·ECO·주간 리포트 | [BV14](2026-11-01_BV14_board_validation_plan_automation.html) | N06G·N06B: 블록 무관 일반화, 링크 |
| shmoo·PVT·margin | [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html), [V7](hmScreenPrep/problems/V7_shmoo_margin/index.html) | 각 블록 매트릭스의 PVT 행만 |
| DDR training·memtest | [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html), [V1](hmScreenPrep/problems/V1_march_memtest/index.html) | N06D D2: 테스트 플랜 형태로 재배열 |
| 인터페이스 테스트(I2C/SPI/UART/Ethernet) | [BV10](2026-11-01_BV10_board_interface_tests.html), [LP00A](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) | N06C·N06E: 보드 측정이 아니라 **IP 블록 검증** 관점 |
| 디버그 시나리오 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) 16개, [BV13](2026-11-01_BV13_board_debug_scenarios.html), [LP15](2026-10-31_LP15_debug_scenarios.html) | N06H: 방법론 + 새 시나리오 6개만 |
| 테스트 러너·커버리지 리포트 코드 | [V5](hmScreenPrep/problems/V5_test_runner_coverage/index.html) | N06G: 플랫폼 HAL·timeout·fault injection 쪽 |
| 벤치마크·성능 메트릭 | [성능 메트릭](2026-10-30_perf_metrics_ssd_vs_soc.html), [V4](hmScreenPrep/problems/V4_perf_bench_stats/index.html) | 블록 매트릭스의 성능 행만 |

## 6. Don이 채울 것

- [ ] Solidigm FPGA 블록 검증: 블록 이름·테스트 수·RTL freeze 전 버그 수 → N06A·N06G "Don 연결"의 `[숫자 채우기]`
- [ ] SK hynix chamber 플랫폼: chamber 수·동시 DUT·회귀 주기 → N06G
- [ ] Apple 통합: 실패를 HW/SW로 가른 사례 1개 (조건·도구·결론) → N06H
- [ ] N06I 모의 세트 기록 표
