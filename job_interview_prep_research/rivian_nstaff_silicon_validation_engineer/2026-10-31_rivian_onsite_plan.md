# Rivian 온사이트 준비 플랜 + 스터디노트 로드맵
<!-- theme: pastel -->

> **상태**: 기술 스크린 완료 (10-30, 45분) → **리크루터 연락 대기** (면접관이 "리쿠르터가 연락할 것"이라고 함) · **온사이트 형식**: 4라운드 = Technical 2 + Behavioral 2 [확인됨: 리크루터 10-17] · **작성**: 2026-10-31 · **원본 컨텍스트**: [context](rivian_nstaff_silicon_validation_engineer_context.html)

## 0. TL;DR

- **스크린에서 확인된 것** [확인됨 10-30]
  - 이 포지션 = **silicon validation + board validation**, 언어는 **주로 C**. **RTL은 필요 없음** (Don이 직접 질문해서 확인).
  - 기술 질문 = **PCIe를 Linux 도구 수준에서** (lspci로 뭘 보나, L0 전 과정, lspci로 L0 확인, L0 실패 시 lspci) + **비트 조작 C 코딩**(니블 뒤집기, 로직 중심).
- **가장 큰 갭** = Linux 쪽 PCIe/보드 디버그 도구(lspci·setpci·sysfs·dmesg) → **N01** 작성 완료. 그리고 **board validation**(전원·클럭·리셋·스트랩·회로도) → [BV 트랙](2026-11-01_BV00_board_validation_track.html).
- **전략**: 온사이트 Technical 2개는 ① **C 코딩(비트·버퍼·레지스터)** ② **도메인 딥다이브(PCIe·보드 bring-up·검증 방법론·디버그 시나리오)** 로 예상. Behavioral 2개는 **Staff 시그널(오너십·크로스팀·모호함)** + 레벨 앵커링.
- **오늘 할 것**: N01·N02 읽기 → 리크루터 콜 질문 준비(§3) → 일정 나오면 §4 일정표에 날짜 넣기.

## 1. 기술 스크린에서 배운 것 (회고 요약)

| 항목 | 내용 | 다음에 |
|---|---|---|
| PCIe / lspci | "lspci 잘 모른다, Mac에서 썼다"고 답함. 면접관은 **링크 상태를 어디서 보는지**(LnkSta, DLActive, LTSSM)를 본 것 [추정] | 도구 이름이 낯설어도 **레지스터 수준으로 브릿지** → [N01](2026-10-31_N01_pcie_lspci_ltssm.html) |
| 코딩 | 니블 뒤집기, **로직 정답 ✅**. 이름·trace 단위가 섞임, 테스트·확장 미제시 | clarify(단위) → trace 표 → 테스트 4개 → 빠른 버전/버퍼 버전 제안 → [N02](2026-10-31_N02_screen_coding_retro.html) |
| RTL | 필요 없다고 확인 [확인됨] | §0.5 RTL 준비는 "읽을 줄 알면 플러스" 수준으로만 유지 |
| 역할 | silicon + **board** validation, C | board bring-up 이야기(Solidigm FPGA 보드, Apple 시스템 통합)를 더 앞에 |

## 2. 온사이트 4라운드 예상 [추정]

| 라운드 | 예상 면접관 | 예상 내용 | 준비 노트 | 비중 |
|---|---|---|---|---|
| **T1 · C 코딩 / 임베디드** | 검증팀 시니어 엔지니어 | 비트 조작(뒤집기·필드·엔디안), 버퍼/링버퍼, 레지스터 드라이버(poll+timeout), 메모리 테스트, 2×32→64 타이머. CoderPad/화이트보드, **로직 중심** | N02 · N05 · [C 정리](2026-10-29_c_basics_cram.html) · [hmScreenPrep V1–V3](hmScreenPrep/index.html) | ★★★ |
| **T2 · 도메인 / 시스템** | Manjiang 또는 시스템/보드 쪽 | **PCIe 깊게**(LTSSM·열거·AER·디버그 시나리오), 보드 bring-up 순서, 검증 계획 설계("이 블록 테스트 플랜 짜 봐"), DDR/I2C/Ethernet 디버그, FPGA vs silicon, PVT/shmoo, 벤치마크 | N01 · [LP 트랙](2026-10-31_LP00_linux_pcie_track.html) · [BV 트랙](2026-11-01_BV00_board_validation_track.html) · N06 · N07 · [최종 훑어보기](2026-10-30_final_skim.html) · [성능 메트릭](2026-10-30_perf_metrics_ssd_vs_soc.html) | ★★★ |
| **B1 · HM / 매니저** | HM(Manjiang 또는 상위 매니저) | 경력 딥다이브, why Rivian / why leave Apple, 오너십, 실패, 우선순위, **레벨(Staff) 판단** | N08 · N09 | ★★ |
| **B2 · 크로스펑셔널** | 다른 팀(DV/RTL, SW, 시스템) | HW·설계팀과 협업/갈등, 데이터로 설득, 모호한 요구사항, 멘토링/툴로 팀 생산성 | N08 | ★★ |

- 리크루터 콜에서 **각 라운드 주제·면접관 이름·코딩 환경**을 물어보면 이 표를 [확인됨]으로 바꿀 수 있음 (§3).

## 3. 리크루터 콜 준비 (연락 오면 바로)

### 3.1 물어볼 것

| 질문 (English) | 왜 |
|---|---|
| "Could you share the onsite format — in person in Palo Alto or virtual, and how long each round is?" | 일정·이동 |
| "What topics does each of the four rounds focus on, and who are the interviewers?" | §2 표 확정, LinkedIn **열람만** 해서 분석 |
| "For the coding round, is it a shared editor, whiteboard, or do I need to compile and run?" | 연습 방식 |
| "Is the role still targeted at Staff level? Any feedback from the technical screen I should prepare for?" | **레벨 앵커링** + 스크린 피드백 |
| "What's the expected timeline after the onsite?" | 다른 프로세스와 조율 (Neros 결과 대기 중) |

### 3.2 레벨 한 문장 (다시 물어보면)

> "At Solidigm I was a Staff Engineer leading pre-silicon SoC bring-up for the PCIe Gen6 controller. I joined Apple at ICT3 as a deliberate move into the wireless integration domain, and my compensation is well above that band, so Staff is the right level for this role."

## 4. 스터디노트 로드맵 (N01–N10)

| ID | 노트 | 내용 | 우선 | 상태 |
|---|---|---|---|---|
| **N01** | [PCIe 링크 디버그 — lspci · LTSSM · L0](2026-10-31_N01_pcie_lspci_ltssm.html) | 스크린 질문 4개 모범 답, lspci/setpci/sysfs 치트시트, LnkSta 비트, LTSSM 단계, "L0 실패 시 lspci" 경우표, 디버그 플로우, bare-metal ECAM 디코드 C | P0 | ✅ 작성 |
| **N02** | [스크린 코딩 회고 — 니블 뒤집기](2026-10-31_N02_screen_coding_retro.html) | Don 답 평가, 올바른 trace, 4가지 버전(루프/O(log n)/버퍼/일반화), 변형 문제 리스트 | P0 | ✅ 작성 |
| N03 | **→ [LP 트랙: Linux에서의 PCIe](2026-10-31_LP00_linux_pcie_track.html)** (LP01–LP18로 분리, 10-31) | 개발(드라이버)·통합(SoC host controller·DT)·검증(테스트 플랜·자동화)·디버그 시나리오 10선, 노트마다 꼬리 질문 체인. P0 = LP03·LP13·LP14·LP15·LP05·LP06·LP04 | P0 | 📋 리스트 완성, 본문 예정 |
| N04 | **→ [BV 트랙: Board validation / bring-up](2026-11-01_BV00_board_validation_track.html)** (BV01–BV16로 분리, 11-01) | 큰 그림(pre-si → A0 → PVT → SLT)·회로도·전원 시퀀싱/PMIC·클럭/PLL·리셋/strap·첫 전원 인가 체크리스트·JTAG/boundary scan·부트/테스트 FW·DDR training + 메모리 테스트·인터페이스 테스트·랩 장비·SI/PI/shmoo/PVT·디버그 시나리오 12선·검증 계획/보드 팜 자동화·자동차. P0 = BV01·BV02·BV03·BV05·BV06·BV07·BV09·BV13·BV14 | P0 | ✅ BV01–BV15 본문 완성 (11-01), BV16 Q 뱅크 완성 |
| N05 | **→ [N05 C 코딩 종이 드릴 뱅크](2026-11-01_N05_c_paper_drill_bank.html)** (N05A–N05I 9페이지 · 74문제, 11-01) | A 비트 기본 · B 엔디안/패킹 · C 레지스터/MMIO · D 버퍼/문자열 · E 링버퍼/ISR · F 메모리 테스트/CRC/ECC · G 상태 머신/파서 · H C 함정(출력 맞히기·버그 찾기) · I 검증형 C(타임스탬프·통계·shmoo) — 문제마다 **종이 20분** 루틴, 인쇄용 문제지 + 힌트/모범 답/trace/꼬리 질문 | P0 | ✅ 9페이지 74문제 완성 (11-01, 전부 컴파일·테스트 PASS) |
| N06 | **→ [N06 검증 방법론 · 테스트 플랜 설계](2026-11-01_N06_validation_test_plan_design.html)** (N06A–N06I 9페이지, 11-01) | A 방법론·공통 템플릿(카테고리 12종·10분 7단계 프레임) · B 커버리지·통계·종료 기준 · C 연습① I2C/SPI/UART/GPIO/타이머/GIC · D 연습② DMA/DDR/SRAM-ECC/interconnect/SMMU · E 연습③ PCIe/Ethernet/CSI-2+ISP/시간 동기 · F 연습④ NPU/DVFS/secure boot/ISO 26262/SLT · G 플랫폼·회귀·자동화 · H 디버그 6단계·triage · I 모의 10분 Q 뱅크 30개 | P0 | ✅ 9페이지 완성 (11-01, 코드 n06/a–h 전부 실행 OK, 링크 0 broken) |
| N07 | **→ [N07 인터페이스 디버그 시나리오](2026-11-01_N07_interface_debug_scenarios.html)** (N07A–N07H 8페이지, 11-01) | A 공통 틀·계측 장비·**마스터 증상 색인** · B I2C/SMBus · C SPI/QSPI flash · D UART/GPIO/인터럽트 · E DDR 실패 패턴 읽기 · F Ethernet(MAC/PHY·RGMII·T1) · G MIPI CSI-2 카메라 · H 모의 드릴 Q 30 + 플래시카드 50 — 각 "증상 → 원인 → 확인 방법", 정상 기준선, 관찰 지점, 시나리오 드릴 | P0 | ✅ 8페이지 완성 (11-01, 시나리오 51개 + 마스터 색인 104행 + Q 30·플래시카드 50, 코드 n07/a–g 전부 PASS) |
| N08 | **Behavioral STAR 뱅크** | 8개 스토리 × 질문 매핑(오너십·갈등·실패·모호함·우선순위·멘토링·영향력·빠른 학습), 영어 2분 버전, Staff 시그널 문장 | P0 | ⏳ 예정 (deck §4 S1–S8 소재 활용) |
| N09 | 동기·레벨·보상 | Why Rivian / why leave Apple (~1년) / why silicon validation, 레벨 앵커링, 보상 질문 대응 | P1 | 🔁 context §4.5 재사용 |
| N10 | Linux on board (Zynq/PetaLinux) | device tree, UIO/devmem, 커널 로그로 보드 디버그, sysfs — JD 우대사항 | P2 | 🔁 [L1](hmScreenPrep/problems/L1_uio_mmio_poll/index.html) + EmbeddedLinux 노트 재사용 |

- 원칙: 새로 쓰는 건 **LP 트랙(N03 대체) · BV 트랙(N04 대체) · N08** (스크린에서 드러난 갭 + 아직 없는 behavioral). 나머지는 이미 만든 자료를 **온사이트용으로 재배열**.

## 5. 일정표 (온사이트까지 약 2주 가정 — 날짜 정해지면 조정)

- 가정: 평일은 Apple 근무 → **하루 1.5–2시간**, 주말 **4–5시간**. 온사이트 날짜를 D로 둠.

| 시점 | 기술 | Behavioral | 산출물 |
|---|---|---|---|
| **지금 ~ 리크루터 콜** | N01·N02 2회 읽기, lspci 질문 4개 영어로 소리 내 답하기 | 리크루터 콜 질문(§3) | 콜 메모 → context 갱신 |
| **D-14 ~ D-11** | N03 PCIe 심화, N05 비트 조작 5문제/일 (종이 20분) | N08 스토리 8개 뼈대(STAR 키워드만) | N03 · N08 초안 |
| **D-10 ~ D-8** (주말 포함) | **BV 트랙(board bring-up)** 집중 — BV06·BV03·BV05·BV07·BV09·BV13, N07 시나리오 | N08 스토리 4개 영어 2분 완성 | BV P0 읽기 |
| **D-7 ~ D-5** | N06 테스트 플랜 설계 2개 연습 ("PCIe 컨트롤러", "I2C 컨트롤러" 테스트 플랜을 10분에 말하기), N05 버퍼/레지스터 문제 | N08 나머지 4개, N09 why 질문 | 연습 녹음 |
| **D-4 ~ D-3** | **모의 기술 면접 2회** (C 45분 + 도메인 45분), 틀린 것만 노트에 추가 | **모의 behavioral 1회** | 오답 목록 |
| **D-2** | N01·N03 PCIe 훑기, 숫자(100 ms, 20 ms, speed 코드) 확인 | 스토리 8개 1줄 요약 카드 | 치트시트 1장 |
| **D-1** | 가볍게: C 정리 C4·C5·C6, N02 | TMAY 2회, 역질문 5개 | 잠 |
| **D** | 아침 30분: 최종 치트시트 | | |

## 6. 라운드별 핵심 체크 (D-2에 다시 보기)

### 6.1 Technical — 코딩

- 시작 전: **단위·입력 형태·메모리 표현 clarify** ("bits, nibbles, or bytes? array or integer? endianness?").
- 쓰는 동안: `volatile` · `1u` · `uint64_t` 캐스트 · timeout 있는 polling.
- 끝나면: trace 표 2–3스텝 → 테스트 4개 → 확장 제안(빠른 버전, 버퍼 버전, ISR-safe 버전).

### 6.2 Technical — 도메인

- PCIe: **LTSSM 순서**, **LnkCap vs LnkSta**, DLActive, "L0 실패 시 root port만 보임", CRS·100 ms, AER 분류, 디버그 플로우 → N01·N03.
- Board: **전원 → 클럭 → 리셋 → JTAG → 메모리 → 인터페이스** 순서와 각 단계 확인 방법 → [BV 트랙](2026-11-01_BV00_board_validation_track.html).
- Validation: 테스트 플랜 구조, 커버리지, FPGA vs silicon, PVT/shmoo, 실패 시 HW/SW/테스트 가르기 → N06.

### 6.3 Behavioral

- 스토리마다 **숫자 1개 + 내 결정 1개 + 크로스팀 1개** 포함 (Staff 시그널).
- 레벨 문장(§3.2)과 why Rivian 한 문장은 외워 두기.

## 7. 다음 액션

- [ ] N01·N02 읽고 lspci 질문 4개 영어로 답해 보기 (오늘)
- [ ] 리크루터 연락 오면 §3.1 질문 → 결과를 Claude에게 전달 → context·이 플랜 갱신
- [ ] Claude에게 **N03 · N04 · N08 작성** 요청 (일정 확정 전에 미리 써 둘지 결정)
- [ ] deck §4 S7/S8 의 `[ ]` 숫자 채우기 (behavioral·도메인 둘 다 씀)
