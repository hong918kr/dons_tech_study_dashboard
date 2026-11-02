# BV15 · 자동차 보드 고려사항 — 온도 등급 · 12 V 전원 이벤트 · EMC · 기능 안전
<!-- theme: pastel -->

> **묶음**: F 디버그·계획·실전 · **우선**: P2 (용어와 "검증 계획이 어떻게 달라지나"를 말할 수 있으면 충분) · **선행 노트**: [BV12 SI/PI · shmoo · PVT](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html), [BV14 검증 계획 · 자동화](2026-11-01_BV14_board_validation_plan_automation.html), [BV03 전원](2026-11-01_BV03_power_tree_sequencing_pmic.html) · **다음 노트**: [BV16 모의 면접 Q 뱅크](2026-11-01_BV16_mock_interview_qbank.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html), [LP17 자동차 PCIe · 기능 안전](2026-10-31_LP17_gen5_gen6_retimer_automotive.html), [LP00A I/O primer (Ethernet · MIPI)](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html), [BV10 인터페이스 테스트](2026-11-01_BV10_board_interface_tests.html) · **코드**: 없음 (랩 레시피 `[미실행]`)

## 0. TL;DR

- 이 노트로 답할 수 있는 것: "**What's different about validating an automotive board vs a consumer one?**", "**How would the temperature grade change your test plan?**", "**How do you validate a safety mechanism like ECC or a watchdog?**", "**What does cold crank / load dump mean for your board?**"
- **외울 것 ① — 온도 등급 (AEC-Q100, 주변 온도 Ta)**: Grade 0 = −40 ~ +150 °C · **Grade 1 = −40 ~ +125 °C** · Grade 2 = −40 ~ +105 °C · Grade 3 = −40 ~ +85 °C [1][2]. 칩 내부 Tj는 이보다 높다.
- **외울 것 ② — 12 V 전원은 "12 V"가 아니다**: 시동 시 **cold crank**로 수 V까지 떨어지고(ISO 16750-2 starting profile), 배터리가 끊기면 **load dump**로 순간 수십~100 V까지 튄다(ISO 16750-2 Test A 79–101 V / 억제된 Test B 35 V) [3][4][5]. 보드 앞단(보호 회로·pre-regulator)이 흡수하고, **검증은 "그동안 SoC가 안전하게 리셋되거나 버티는가, 데이터가 안 깨지는가"** 를 본다.
- **외울 것 ③ — 기능 안전 (ISO 26262)**: ASIL A–D. 칩 안 **safety mechanism**(ECC, lockstep, LBIST/MBIST, watchdog, 전압·클럭·온도 모니터)이 고장을 **감지 → 보고 → 안전 상태 진입을 FTTI 안에** 하는지 **fault injection으로 증명**하는 게 검증 엔지니어의 일. ASIL D 하드웨어 지표 SPFM ≥ 99 %, LFM ≥ 90 % [6][7].
- EMC는 **CISPR 25**(차량 부품 방출, 150 kHz–2.5 GHz) [8] — 검증 SW 쪽 역할은 "EMC 시험 중 최대 활동을 만들고 에러를 감시하는 테스트 FW".
- 차 안 연결: 백본·센서는 **자동차 Ethernet**(100BASE-T1, 1000BASE-T1, 2.5/5/10GBASE-T1, 10BASE-T1S), 카메라는 **SerDes**(GMSL2/3, FPD-Link, MIPI A-PHY) [9][10].
- **30초 영어**: "Three things change. The environment: parts are graded by ambient temperature — grade 1 is minus 40 to 125 — so characterization and stress run across that whole range, plus power transients like cold crank and load dump, EMC, and vibration. Lifetime: the board has to stay within margin for well over a decade, so we keep more guardband and full traceability — chip ID, lot, board serial, firmware version — for every test. And functional safety: every safety mechanism — ECC, lockstep, BIST, watchdogs, voltage and clock monitors — has to be proven by fault injection: detected, reported and reaching a safe state within the fault-tolerant time interval."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — 사무실 컴퓨터 vs 군용 무전기

노트북은 **실내(0–35 °C)** 에서, **깨끗한 콘센트 전원**으로, **3–5년** 쓰고, 고장 나면 **재부팅**하면 된다.
자동차 컴퓨터는 **알래스카 겨울 아침(−40 °C)** 에 시동을 걸어야 하고, **사막 여름 대시보드 아래(+85 °C 이상)** 에서 돌아야 하고, 전원은 **시동 모터가 배터리를 확 빨아들이는 순간** 반토막이 나며, 옆에서 모터·인버터가 **전자파**를 뿜고, **15년 넘게** 진동 속에서 버텨야 하고, 고장 나면 **사람이 다칠 수 있다**.

그래서 같은 SoC 보드라도 자동차용은:
1. **더 넓은 온도**에서 마진을 재고 (→ [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) PVT의 T 축이 넓어짐)
2. **나쁜 전원 이벤트**를 일부러 만들어 보고
3. **전자파**를 덜 내고 더 잘 견디는지 보고
4. **고장이 나도 스스로 알아채고 안전하게 멈추는지**(기능 안전) 증명하고
5. **어느 차의 어느 칩이 어느 lot인지** 15년간 추적할 수 있게 기록한다.

### 1.2 차량 전원 그림 — 12 V가 보드까지 오는 길

```text
 [12 V 납축/리튬 배터리] ── (EV에서는 고전압 배터리 → DC-DC가 12 V 계통을 충전)
        │   ← 여기서 이벤트 발생: cold crank(전압↓), load dump(전압↑↑), 역접속, 점프 스타트, 스위칭 스파이크
        ▼
 ┌──────────────────────── ECU 보드 앞단 (보호) ─────────────────────────┐
 │ 역접속 보호(다이오드/ideal-diode) → TVS(과전압 클램프) → 필터(EMC)      │
 │ → pre-regulator (넓은 입력 범위 buck: 크랭크 저전압·덤프 고전압 견딤)   │
 └──────────────────────────────────┬──────────────────────────────────┘
                                    ▼ 5 V / 3.3 V 중간 rail
 ┌──────────── PMIC (자동차·기능안전용: 전압 모니터 + watchdog 내장 흔함) ────┐
 │ Vcore 0.75 V · DDR 1.1/0.6 V · IO 1.8/3.3 V · PHY 0.8 V ...               │
 │ UV/OV 모니터 → 범위 벗어나면 RESET / 안전 신호(nRESET, ERR 핀)             │
 └──────────────────────────────────┬──────────────────────────────────┘
                                    ▼
                         [ SoC: lockstep CPU · ECC · BIST · 온도/클럭 모니터 ]
```

- 검증 엔지니어 관점: 위쪽 이벤트(크랭크·덤프)는 **보드 HW 설계**가 막는다. SoC 쪽 검증은 **"막지 못한 만큼"이 PMIC 모니터 → 리셋/안전 상태로 올바르게 이어지는가**, 그리고 **그 와중에 데이터(플래시 쓰기, 로그)가 깨지지 않는가**.
- EV(Rivian 같은)는 시동 모터가 없어 전통적 cold crank가 덜 중요할 수 있지만, OEM 전원 스펙에는 여전히 **저전압 dip·재시작 프로파일**이 들어간다 [추정: OEM 스펙 비공개].

### 1.3 소비자 보드 vs 자동차 보드 한눈에

| 항목 | 소비자/데이터센터 | 자동차 |
|---|---|---|
| 온도 | 0–70 °C (commercial) 또는 −40–85 °C (industrial) [2] | AEC-Q100 Grade 1/2 (−40–125 / −40–105 °C), 엔진룸은 Grade 0 |
| 전원 | 안정된 5 V/12 V, ATX/PSU | 9–16 V 정상 범위 + crank·load dump·역접속·스파이크 [추정: 정상 범위 수치는 OEM별] |
| EMC | FCC/CISPR 32 (방출 위주) | **CISPR 25** (차 안 라디오 보호) + 내성 시험 (BCI 등) [8] |
| 수명 | 3–5년 | 10–15년 이상, 공급 장기 보장 [추정: OEM별] |
| 고장 시 | 재부팅, 사용자 불편 | **안전 상태**(기능 저하·운전자 경고), ISO 26262 |
| 부품 | 일반 부품 | AEC-Q100(IC) / Q101(개별 소자) / Q200(수동 소자) 인증 부품 [1] |
| 추적성 | 보통 lot 수준 | 칩 고유 ID·lot·보드 SN·FW 버전·테스트 결과를 장기 보관 |
| 검증 증거 | 테스트 PASS | 요구사항 ↔ 테스트 추적표, 안전 메커니즘별 fault injection 결과 |

### 1.4 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| AEC | Automotive Electronics Council | 자동차 부품 신뢰성 기준을 만든 업계 단체 | 부품 선택·등급 |
| AEC-Q100 | (IC 스트레스 시험 인증) | 집적회로가 자동차용으로 쓸 만한지 보는 스트레스 시험 묶음 | Grade = 온도 범위 |
| Grade 0/1/2/3 | 온도 등급 | 동작 주변 온도 범위 등급 | 특성화 온도 범위 결정 |
| Ta / Tj | Ambient / Junction temperature | 주변 공기 / 칩 내부 온도 | 등급은 Ta, 칩 한계는 Tj |
| HTOL | High Temperature Operating Life | 고온에서 오래 돌려 수명 열화 확인 | 부품 공급사 인증 항목 |
| cold crank | 냉간 시동 | 추운 날 시동 모터가 배터리 전압을 크게 끌어내림 | 저전압 리셋·brown-out |
| load dump | 부하 덤프 | 발전기가 충전 중인데 배터리가 끊기면 생기는 큰 과전압 | 보호 회로·과전압 |
| ISO 16750-2 | (차량 전기 부하 환경 조건) | 공급 전압·과도·역접속 등 전기 환경 시험 | 전원 이벤트 시험 근거 |
| ISO 7637-2 | (전원선 전도성 과도) | 스위칭·유도성 부하로 생기는 짧은 스파이크 펄스 정의 | 전원선 과도 시험 |
| EMC / EMI | Electromagnetic Compatibility / Interference | 전자파를 덜 내고(방출) 잘 견디는(내성) 능력 | 인증·라디오 간섭 |
| CISPR 25 | (차량 탑재 수신기 보호용 방출 규격) | 차 안 라디오·GNSS를 방해하지 않게 방출 한계 | 방출 시험 |
| ALSE | Absorber-Lined Shielded Enclosure | 전파 흡수재를 붙인 차폐실 | 복사 방출 시험 장소 |
| BCI | Bulk Current Injection | 하네스에 RF 전류를 주입해 내성 시험 | 시험 중 에러 감시 FW 필요 [추정: ISO 11452-4] |
| ISO 26262 | (도로 차량 기능 안전) | 전기/전자 고장이 위험으로 이어지지 않게 하는 개발·검증 규칙 | 안전 메커니즘 검증 |
| ASIL A–D | Automotive Safety Integrity Level | 위험도 등급 (D가 가장 엄격) | 요구 진단 커버리지 결정 |
| safety mechanism | 안전 메커니즘 | 고장을 감지·제어하는 HW/SW 장치 | fault injection 대상 |
| SPFM / LFM | Single-Point / Latent Fault Metric | 단일 고장·잠재 고장을 얼마나 잡나 (%) | ASIL별 목표치 |
| PMHF | Probabilistic Metric for random HW Failures | 위험한 무작위 HW 고장 확률 (FIT) | ASIL D < 10 FIT [7] |
| FIT | Failures In Time | 10⁹ 시간당 고장 수 | 신뢰도 단위 |
| FTTI | Fault Tolerant Time Interval | 고장 발생부터 위험이 될 때까지 허용 시간 | 감지+반응이 이 안에 끝나야 |
| lockstep | 락스텝 | 같은 코드를 두 코어가 동시에 돌려 결과 비교 | CPU 고장 감지 |
| LBIST / MBIST | Logic / Memory Built-In Self-Test | 로직/메모리를 스스로 검사하는 내장 회로 | 시동 시 잠재 고장 검출 |
| ECC (SECDED) | Error Correcting Code (Single-Error Correct, Double-Error Detect) | 1비트 고치고 2비트 감지 | 메모리 고장 감지·정정 |
| windowed watchdog | 윈도우 워치독 | 너무 늦게도, **너무 일찍도** kick하면 리셋 | 폭주한 SW 감지 |
| 100BASE-T1 / 1000BASE-T1 | 자동차 Ethernet | **한 쌍의 꼬임선**으로 100 Mb/s / 1 Gb/s | 차량 백본·센서 |
| GMSL / FPD-Link | 카메라 SerDes | 동축 케이블 한 가닥으로 영상 + 제어 + 전원 | 카메라 링크 |
| PoC | Power over Coax | 동축 케이블로 카메라에 전원 공급 | 카메라 전원 이벤트 |
| gPTP | generalized Precision Time Protocol (IEEE 802.1AS) | Ethernet으로 장치 간 시계를 맞춤 | 센서 타임스탬프 동기 |
| traceability | 추적성 | 어떤 칩·보드·FW·테스트 결과인지 끝까지 기록 | 필드 불량 분석·리콜 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 AEC-Q100 온도 등급과 인증

| Grade | 주변 온도 범위 (Ta) | 흔한 위치 예 [추정] |
|---|---|---|
| 0 | −40 ~ +150 °C | 엔진룸, 파워트레인 근처 |
| 1 | −40 ~ +125 °C | 차체 내부 고온 영역, 많은 ADAS/컴퓨트 부품 |
| 2 | −40 ~ +105 °C | 실내 인포테인먼트·일부 ECU |
| 3 | −40 ~ +85 °C | 온도가 온화한 실내 위치 |

- 범위는 Renesas·Infineon 등 공개 표에서 확인 [1][2]. **등급은 주변 온도(Ta) 기준**이고, 칩 데이터시트의 Tj 한계는 따로다 — NXP 포럼에서도 이 차이를 지적 [11].
- **인증(qualification) ≠ 보드 검증**: AEC-Q100 스트레스 시험(예: HTOL — Grade 1이면 Tj 125 °C에서 1000시간, 3 lot × 77개 0 fail; ELFR 48시간; 온도 사이클)은 **부품 공급사**가 하는 일 [12]. 보드 검증 엔지니어는 **그 등급 범위에서 보드가 기능·마진을 유지하는지**를 본다 (특성화, [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html)).
- 수명 요구는 **mission profile**(차량이 평생 몇 시간을 어떤 온도에서 보내나)에서 출발해 시험 시간을 정한다 [13].

### 2.2 12 V 시스템 전원 이벤트 (ISO 16750-2 / ISO 7637-2 요약)

> 수치는 공개 2차 자료(벤더 앱노트·시험소 요약) 기준. 표준 원문과 **OEM별 수정 프로파일**이 우선한다.

- **정상 공급**: 12 V 시스템의 시험 nominal은 **13.5 V**(엔진 작동 중 충전 상태 가정) [5].
- **Cold crank (starting profile)**: 시동 모터가 돌 때 배터리 전압이 짧게 깊이 떨어졌다가 일정 저전압에서 머문 뒤 회복. ISO 16750-2(2012판) 12 V 표 요약: 최저점 Us6 = **8 / 4.5 / 3 / 6 V**(레벨 I/II/III/IV), 유지 전압 Us = 9.5 / 6.5 / 5 / 6.5 V, 최저점 유지 t6 = 15 ms, 유지 구간 t8 = 1 s 또는 10 s [14] [추정: 2차 요약 판독, 2023판은 미확인]. 벤더 자료는 "최악 **3 V 아래로 15–50 ms**" 정도로 요약 [3].
- **Warm crank / start-stop**: 덜 깊음(약 5–8 V 수준으로 짧게) — start-stop 차량은 하루에도 수십 번 [3].
- **Load dump**: 발전기가 충전 중 배터리 연결이 끊기면 큰 과전압. **Test A(억제 없음)**: 12 V 시스템에서 피크 **79–101 V**, 지속 **최대 약 400 ms** · **Test B(발전기 내부 클램프 있음)**: **35 V**로 제한 [4][5]. 실제로는 많은 OEM이 자체 한계를 씀 [4].
- **ISO 7637-2 펄스 (전원선 전도성 과도)** [15]:

| 펄스 | 무엇을 흉내 내나 | 특징 |
|---|---|---|
| 1 | 병렬 유도성 부하가 있을 때 **전원이 끊기는 순간**의 음의 스파이크 | 중간 속도·에너지, 최소 500회 반복 |
| 2a | 병렬 장치 전류가 하네스 인덕턴스로 끊길 때 양의 스파이크 | 짧은 양의 펄스 |
| 2b | 시동 끈 뒤 **DC 모터가 발전기처럼** 도는 경우 | 양의 펄스 |
| 3a / 3b | 스위치·릴레이 아크로 생기는 빠른 음/양 스파이크 열 | 고속·저에너지, **마이크로프로세서 오동작**의 흔한 원인, 1시간 반복 |

- **기타**: 역접속(reverse battery), 점프 스타트(24 V 급), 중첩 AC 리플, 장시간 과전압, 개방 회로, **reset behavior at voltage drop**(전압이 조금씩 떨어지다 회복할 때 리셋 동작이 정의대로인가) [5] [추정: 점프 스타트 수치는 OEM별].
- **검증 SW 관점에서 볼 것**:
  1. 크랭크 dip 동안 PMIC UV 모니터가 **언제** 리셋을 거는가, 리셋 후 **깨끗하게 재부팅**하는가 (reset reason = brown-out/UV로 기록되나 → [BV05](2026-11-01_BV05_reset_strap_boot_mode.html))
  2. 리셋까지 안 가는 얕은 dip에서 **기능이 유지**되는가 (카메라 프레임 드롭, Ethernet 링크 끊김, DDR 에러 카운터)
  3. dip 중에 **플래시 쓰기**(로그·설정)가 진행 중이면 데이터가 깨지지 않는가 — SSD의 **PLP(power-loss protection)** 테스트와 같은 문제
  4. 반복 이벤트(start-stop 수천 회)에서 **누적 문제**(부팅 실패율, 플래시 마모)

### 2.3 EMC — CISPR 25 개요와 검증 SW의 역할

- **CISPR 25**: 차량과 차량 탑재 부품의 **방출(emission)** 을 제한해 차 안 수신기(라디오, GNSS, 셀룰러 등)를 보호. 현행 Ed. 5 (2021), 범위 **150 kHz – 2.5 GHz** [8].
  - 부품 시험: **전도 방출**(전원선의 전압법·하네스 전류 프로브법)과 **복사 방출**(ALSE 차폐실 안에서 부품 + 하네스 + 시험대 배치로 측정) [8][16].
  - **Class 1–5** 한계 등급 — 5가 가장 엄격, OEM이 장착 위치·수신기 민감도에 따라 지정 [추정: 2차 자료] [17].
- **내성(immunity)**: 별도 표준 계열(예: ISO 11452 — BCI, ALSE 복사 내성) 및 ESD(ISO 10605) [추정: 원문 미확인]. 시험 중 장치의 **기능 상태**를 판정 기준으로 씀.
- **검증 SW 엔지니어가 하는 일** [추정: 업계 일반 관행]:
  - **EMC 테스트 FW**: 시험 동안 **최악 방출 조건**(모든 인터페이스 최대 속도·최대 활동, CPU/NPU 최대 부하, 카메라 스트리밍)을 재현하고, 동시에 **에러를 감시·기록**(CRC 에러, 링크 다운, 프레임 드롭, 리셋, ECC 이벤트)해서 외부로 보고(시험실 밖에서 볼 수 있게 광 Ethernet/CAN 등).
  - 방출 줄이는 SW 손잡이: **클럭 SSC(spread spectrum)** 켜기, 클럭 주파수 계획(라디오 대역 피하기), PHY 출력 세기/slew rate 설정, 안 쓰는 클럭·인터페이스 끄기.
  - 결과 재현: EMC 랩에서 실패하면 **어느 주파수·어느 모드**에서인지 → FW 설정 하나씩 바꿔 원인 블록 찾기.

### 2.4 ISO 26262 — ASIL과 안전 메커니즘, 그리고 검증 방법

- **ASIL 결정**: 위험 상황별 **S**(Severity, 심각도 S0–S3) × **E**(Exposure, 노출 빈도 E0–E4) × **C**(Controllability, 운전자 제어 가능성 C0–C3) 조합 → QM(일반 품질) 또는 ASIL A–D [추정: 표준 일반 지식, 원문 표 미확인].
- **하드웨어 지표 목표** (ISO 26262-5 표를 인용한 Infineon/공개 자료) [6][7]:

| 지표 | ASIL B | ASIL C | ASIL D |
|---|---|---|---|
| SPFM (단일점 고장 커버리지) | ≥ 90 % | ≥ 97 % | ≥ 99 % |
| LFM (잠재 고장 커버리지) | ≥ 60 % | ≥ 80 % | ≥ 90 % |
| PMHF (무작위 HW 고장률) | < 100 FIT [추정] | < 100 FIT [추정] | < 10 FIT [7] |

- 이 숫자들은 **FMEDA**(고장 모드·영향·진단 분석)로 계산하고, 그 안에 들어가는 "이 메커니즘이 고장의 몇 %를 잡는가(diagnostic coverage)"를 **fault injection 결과로 뒷받침**한다 [추정: 일반 관행].
- **FTTI 사슬**: 고장 발생 → **감지(diagnostic test interval 안)** → **반응(fault reaction time)** → **안전 상태** 도달이 FTTI보다 짧아야 함.

```text
 고장 발생 ─┬─ 감지 시간 ─┬─ 반응 시간 ─┬─▶ 안전 상태 (기능 저하·경고·재시작)
            │             │             │
            └─────────── 이 합이 FTTI(위험까지의 허용 시간)보다 작아야 ──────────┘
 검증: 각 메커니즘에 고장을 '주입'하고 이 세 구간을 실제로 잰다
```

- **SoC/보드의 대표 안전 메커니즘과 검증 방법**:

| 메커니즘 | 무엇을 잡나 | 어떻게 고장을 주입하나 (검증) | 확인할 것 |
|---|---|---|---|
| **ECC (SECDED)** — SRAM, DDR, 캐시, 버스 | 메모리 비트 플립 | 컨트롤러의 **ECC error injection 레지스터**(체크 비트 뒤집기), 또는 ECC 끄고 데이터 쓰고 → 한 비트 바꿔 쓰고 → ECC 켜고 읽기 | 1비트: 정정 + correctable 카운터 + (임계값) 인터럽트 · 2비트: 정정 불가 감지 + 안전 반응(버스 에러·리셋) · 주소/신드롬 로그 |
| **Lockstep CPU** (예: Arm Cortex-R52 DCLS, Cortex-A78AE split-lock) [추정: Arm 공개 제품 정보] | CPU 로직 고장 | 비교기 **self-test/주입 기능**, 벤더 제공 fault injection 모드 | 불일치 감지 → 안전 관리자에 신호 → 반응 시간 |
| **LBIST / MBIST** | 잠재 고장(평소 안 쓰는 로직·메모리) | 시동 시 BIST 실행, 일부러 결함 있는 시그니처/패턴을 주입하는 테스트 모드 [추정] | BIST 결과 레지스터, 실행 시간(부팅 시간 예산), 실패 시 부팅 차단 |
| **Watchdog** (windowed, 외부 PMIC/내부) | SW 폭주·멈춤 | kick **중단**, **너무 이른** kick, 인터럽트 끈 채 무한 루프 | 리셋/안전 신호까지 시간, reset reason 기록, 디버거 halt 시 동작 설정 |
| **전압 모니터** (PMIC/SoC UV·OV) | 전원 이상 | PMIC margining으로 **임계값 너머**로 rail 이동 (→ [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html)), 프로그래머블 PSU로 입력 dip | 임계값 정확도, 감지 지연, 리셋 순서 |
| **클럭 모니터** | 클럭 손실·주파수 이탈 | 외부 osc 끄기/주파수 바꾸기, PLL 설정 바꾸기 (테스트 모드) | 감지 → 백업 클럭 전환 또는 안전 상태 |
| **온도 모니터** | 과열 | thermostream으로 가열, 임계값 낮춰서 트리거 | throttling/셧다운 동작 |
| **버스/인터커넥트 보호** (parity, timeout, E2E CRC) | 전송 손상, 응답 없음 | 에러 주입 레지스터, 응답 안 하는 장치 모의 | 타임아웃·에러 응답·로그. PCIe 쪽 메커니즘은 [LP17 §2.10](2026-10-31_LP17_gen5_gen6_retimer_automotive.html) |

- **Fault injection 캠페인의 형태** (검증 계획 관점):
  1. 안전 메커니즘 목록 ↔ 안전 요구사항 ↔ 테스트 케이스 **추적표**
  2. 각 메커니즘마다: 주입 방법 · 기대 감지 · 기대 보고(레지스터·인터럽트·핀) · 기대 반응 · **시간 측정 방법**(scope로 주입 트리거 GPIO ↔ 안전 신호 핀)
  3. **진짜 하드웨어 경로**로 주입할수록 증거가 강하다 (레지스터 주입 < 실제 전압·클럭·온도 조작)
  4. 반대 방향도 검증: **오탐(false positive)** 없음 — 정상 동작 중 PVT 전 범위에서 모니터가 쓸데없이 트리거되지 않는가 (예: UV 임계값이 droop 바닥보다 충분히 아래인가)

### 2.5 자동차 Ethernet과 카메라 SerDes 개요

- **자동차 Ethernet = 한 쌍의 꼬임선(single pair), 짧은 거리(약 15 m), point-to-point 위주** [9][19]:

| 표준 | 이름 | 속도 | 특징 |
|---|---|---|---|
| IEEE 802.3bw (2015) | 100BASE-T1 | 100 Mb/s | BroadR-Reach 기원, PAM3, 전이중 |
| IEEE 802.3bp (2016) | 1000BASE-T1 | 1 Gb/s | 같은 단일 쌍 철학, 10배 속도 |
| IEEE 802.3ch (2020) | 2.5/5/10GBASE-T1 | 2.5/5/10 Gb/s | PAM4, 약 15 m |
| IEEE 802.3cg (2019) | 10BASE-T1S / T1L | 10 Mb/s | T1S = **멀티드롭 버스**(CAN/LIN 대체 후보), T1L = 장거리 |

  - 일반 Ethernet과 다른 점: **master/slave 역할**을 정해야 링크가 뜸(autoneg 없이 강제 설정 흔함), 케이블 진단(TDR 기반 cable test), 시간 동기 **gPTP(802.1AS)**·TSN — 센서 타임스탬프 정렬에 중요 [추정: Rivian 구성 미공개]. 기본 개념은 [LP00A](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html), 보드 테스트는 [BV10](2026-11-01_BV10_board_interface_tests.html).
- **카메라 SerDes**: 카메라 센서의 MIPI CSI-2는 수십 cm밖에 못 가므로, 카메라 쪽 **serializer**가 동축 케이블로 보내고 SoC 쪽 **deserializer**가 다시 CSI-2로 풀어 준다 [10].
  - **GMSL**(ADI/구 Maxim): GMSL2 최대 6 Gb/s, GMSL3 최대 12 Gb/s, 약 15 m, **PoC**(동축으로 전원), 역방향 채널로 **I2C·GPIO 터널링** [10][18].
  - **FPD-Link**(TI): FPD-Link III 4.16 Gb/s 순방향 / 50 Mb/s 역방향 [18]. GMSL과 **상호 호환 안 됨**.
  - **MIPI A-PHY**(IEEE 2977, 최대 15 m) — [LP17 §2.9](2026-10-31_LP17_gen5_gen6_retimer_automotive.html) 표 참고.
  - 검증 포인트: 링크 lock 상태·**CRC/에러 카운터**·PRBS BER(SerDes 내장 기능) at 온도·케이블 길이, PoC 전압이 **크랭크 dip 동안** 유지되나, 역방향 I2C로 센서 설정이 되는가, 프레임 카운트·드롭, 여러 카메라 **프레임 동기(FSYNC)** [추정: 구성별].

### 2.6 장수명 · 추적성

- **수명**: 차량은 10–15년 이상 운용 → 노화(aging: NBTI/HCI로 Vth 상승 → 시간이 지나면 느려짐)를 고려해 **처음부터 guardband**를 더 남긴다 (→ BV12의 AVS/스크린 guardband) [추정: 정량은 공정별].
- **부품 장기 공급**: 같은 부품을 10년 이상 구매 가능해야 함, 부품 대체(alternate) 시 재검증 범위를 정하는 게 보드 검증 일 [추정].
- **추적성**: 칩 고유 ID(ECID/eFuse)·wafer lot·보드 SN·BOM 리비전·rework 이력·FW/테스트 버전·시험 조건·결과를 **연결해 보관** → 필드 불량이 오면 "같은 lot/같은 조건의 검증 데이터"를 찾아 비교. SK hynix·Solidigm의 **양산 테스트 로그 체계**와 같은 사고방식.
- 품질 체계 용어로 IATF 16949(자동차 품질경영), PPAP(양산 부품 승인) 등이 등장할 수 있음 [추정: 이름만 알아 둘 것].

### 2.7 "검증 계획에서 무엇이 바뀌나" — 한 장 요약

| 검증 계획 항목 | 소비자 보드 | 자동차 보드에서 추가/변경 |
|---|---|---|
| 온도 | 상온 + 몇 점 | **등급 전 범위**(−40 ~ +105/125 °C Ta), Tj 실측, 냉·온 시동, 온도 사이클 중 동작, 결로 |
| 전압 | rail ± margin | rail margin + **12 V 입력 이벤트**(crank·start-stop·load dump·스파이크) 중 동작/리셋/데이터 무결성 |
| 마진 기준 | 스펙 통과 | **더 큰 guardband**(노화·15년), 코너 칩 다수, Cpk 1.33–1.67 |
| EMC | 방출 사전 점검 | CISPR 25 방출 + 내성 시험용 **EMC 테스트 FW**, SSC·클럭 계획 검증 |
| 안전 | 없음/간단 | 안전 메커니즘별 **fault injection** + 반응 시간 측정 + 오탐 검증, 요구사항 추적 |
| 인터페이스 | USB/PCIe/Wi-Fi | **T1 Ethernet**(master/slave, cable test, gPTP), **카메라 SerDes**(PoC, 역채널, CRC), CAN |
| 수명/스트레스 | 짧은 soak | 장시간 soak at hot, 전원 사이클 수천 회, 진동/열충격 시험 중 모니터링 [추정: 시험 자체는 HW 팀] |
| 기록 | 결과 위주 | **전 항목 추적성**(칩 ID·lot·보드·FW·조건), 장기 보관 |

## 3. 보드에서 볼 것 · 잴 것

### 3.1 크랭크/전원 이벤트 시험 셋업 `[미실행]`

```text
 [프로그래머블 PSU 또는 자동차 과도 발생기]──12 V 입력──▶[ECU 보드]
        │ 크랭크 프로파일을 시퀀스로 재생 (SCPI list mode)
        │                                       │
 scope CH1: 12 V 입력   CH2: 5 V 중간 rail   CH3: Vcore   CH4: nRESET (또는 PMIC ERR 핀)
 트리거: CH1 하강 엣지 (예: 9 V 아래)            UART 로그 + reset reason 레지스터 + 에러 카운터 수집
```

| 확인 항목 | 통과 기준 (예시, 실제는 OEM 스펙) |
|---|---|
| 얕은 dip (레벨 I 수준) | 리셋 없음, 링크·카메라 유지, 에러 카운터 증가 없음 |
| 깊은 dip (레벨 III 수준) | 정의된 리셋 발생 → **reset reason = UV/brown-out** → 재부팅 시간 ≤ 요구치 |
| dip 중 플래시 쓰기 | 재부팅 후 파일시스템/설정 무결성 OK (쓰기 원자성) |
| 반복 1000회 | 부팅 실패 0, 누적 에러 없음 |
| load dump (보호 회로 경유) | 보드 손상 없음, 과전압 보호 후 정상 복귀 |

**예시 로그와 해석** `[예시 출력]` (가상의 bring-up FW 로그)

```text
[ 12.031] pmic: VIN_UV warning (vin=6.1V)            ← 입력이 6.1 V로 떨어짐 — 중간 rail은 아직 정상
[ 12.033] cam2: link lock lost                         ← 카메라 SerDes 링크 끊김 → PoC 전압도 떨어졌을 가능성
[ 12.045] pmic: VIN_UV cleared                         ← 회복 (14 ms dip)
[ 12.180] cam2: link lock regained, frames dropped=4   ← 135 ms 후 복구, 프레임 4개 손실 → 요구사항과 비교
--- reboot ---
[  0.002] boot: reset_reason=PMIC_UV (0x04) boot_count=118
```

- 해석: 얕은 dip에서 SoC는 버텼지만 **카메라 링크가 끊김** → PoC 경로의 전원 홀드업 부족 또는 serializer UV 임계값 문제 → 보드 팀과 같이 scope로 PoC 전압 확인. 마지막 줄은 다른 회차의 깊은 dip에서 **정의된 리셋**이 났다는 증거(boot_count로 반복 횟수 추적, [BV05](2026-11-01_BV05_reset_strap_boot_mode.html) 패턴).

### 3.2 안전 메커니즘 fault injection 체크리스트 `[예시]`

| # | 메커니즘 | 주입 | 측정 | 기대 결과 |
|---|---|---|---|---|
| 1 | SRAM ECC 1비트 | ECC inject 레지스터 | 인터럽트 latency, 카운터 | 정정, CE 카운터 +1, 주소 로그 |
| 2 | SRAM ECC 2비트 | ECC inject (2비트) | 반응 신호까지 시간 | UE 감지 → 안전 반응 (버스 에러/리셋), 시간 ≤ 예산 |
| 3 | DDR ECC | 동일 (DDR 컨트롤러) | 동일 + 스크러빙 동작 | 동일 + scrub이 정정 데이터 다시 씀 |
| 4 | Watchdog — kick 중단 | FW에서 kick 중지 | GPIO 마커 ↔ 리셋 핀 scope | 타임아웃 시 리셋, reason=WDT |
| 5 | Watchdog — 조기 kick | 윈도우 열리기 전 kick | 동일 | 윈도우 위반으로 리셋 |
| 6 | Vcore UV 모니터 | PMIC로 임계값 아래로 margin | 임계 전압 실측, 지연 | 리셋/안전 신호, 임계값 ± 정확도 |
| 7 | 클럭 모니터 | 테스트 모드로 PLL 주파수 이탈 | 감지 지연 | 경보 + 백업 클럭/안전 상태 |
| 8 | Lockstep | 벤더 주입 모드 | 불일치 신호 | 안전 관리자 경보, 반응 |
| 9 | 시동 MBIST/LBIST | 정상 + 주입 결함 모드 | BIST 시간, 결과 레지스터 | 정상: PASS, 결함: 부팅 차단 |
| 10 | 오탐 검증 | 주입 없음, PVT 전 범위 + 크랭크 | 모니터 이벤트 수 | **0** (정상 조건에서 트리거 안 됨) |

### 3.3 Linux에서 볼 수 있는 것 (자동차 Ethernet / 에러 카운터) `[미실행]`

```bash
ethtool eth1                              # 속도, 링크, (지원 시) master-slave 상태
ethtool -s eth1 master-slave forced-master # T1 PHY 역할 강제 (드라이버 지원 시)
ethtool --cable-test eth1                  # 케이블 진단 (PHY·드라이버 지원 시)
ethtool -S eth1 | grep -i -E 'crc|err'     # MAC/PHY 에러 카운터
cat /sys/devices/system/edac/mc/mc0/ce_count /sys/devices/system/edac/mc/mc0/ue_count  # DDR ECC (EDAC 드라이버 있을 때)
```

- master-slave·cable-test는 커널/ethtool 버전과 PHY 드라이버 지원에 따라 다름 [추정: 미실행]. EDAC 카운터는 ECC 주입 시험의 "보고 경로" 확인에 씀 (CE = corrected, UE = uncorrected).

## 4. 꼬리 질문 체인

### 4.1 "What's different about validating an automotive board vs a consumer one?"

<details>
<summary>Q → 꼬리1 (temperature grade → test plan) → 꼬리2 (how many temperatures) → 꼬리3 (lifetime)</summary>

- **Q.** — "The environment, the lifetime and safety. Environment means the full temperature grade, supply transients like cold crank and load dump, EMC and vibration. Lifetime means more guardband and full traceability, because the board has to keep its margin for over a decade. And safety means every safety mechanism has to be proven by fault injection, with timing, against the safety requirements — not just 'it works'." · 한국어: 환경(온도·전원 이벤트·EMC·진동) · 수명(guardband·추적성) · 안전(fault injection으로 증명).
- **꼬리1. How would the temperature grade change your test plan?** — "It sets the characterization range — for grade 1, minus 40 to 125 ambient — and because the grade is ambient, I'd also log junction temperature from on-die sensors, which is higher under load. Every margin item — the voltage-frequency shmoo, DDR eye, link margining — runs at cold, room and hot at minimum, plus cold boot and hot boot, and temperature cycling while the system is running to catch intermittent connections." · 한국어: 등급 = 특성화 온도 범위, Ta vs Tj 기록, 모든 마진 항목을 저·상·고온, 냉/온 시동, 온도 사이클 중 동작.
- **꼬리2. Which temperatures exactly, and why not just the extremes?** — "The extremes plus room, and usually a point in between, because the worst case isn't always at an extreme — at low voltage, temperature inversion can make cold the slowest, DRAM refresh requirements change above 85 °C, and some failures are in transitions, like a PLL losing lock while the temperature ramps. So I test at fixed points and during ramps." · 한국어: 극단+상온+중간, 최악이 꼭 극단 아님(temperature inversion, refresh), 램프 중 실패도 있음.
- **꼬리3. How does a 15-year lifetime show up in validation?** — "Mostly as guardband and data. Transistors slow down with aging, so I don't accept a part that only just passes at the corner; the spec and production screen keep extra margin. And I keep traceable records — chip ID, lot, board, firmware, conditions — so a field return years later can be compared with its characterization data." · 한국어: 노화 대비 guardband + 추적 가능한 기록.

</details>

### 4.2 "How do you validate a safety mechanism like ECC or a watchdog?"

<details>
<summary>Q → 꼬리1 (FTTI) → 꼬리2 (latent faults / BIST) → 꼬리3 (false positives)</summary>

- **Q.** — "By fault injection through the most realistic path available, and by checking the whole chain: detection, reporting and reaction. For ECC I use the controller's error-injection feature — a single-bit error must be corrected, counted and logged with the address; a double-bit error must be detected and trigger the defined safe reaction. For a watchdog I stop kicking it, and for a windowed watchdog I also kick too early, and I measure the time from a GPIO marker at the injection point to the reset or safety pin with a scope, and confirm the reset reason." · 한국어: 현실적 경로로 주입 → 감지·보고·반응 전체 확인, ECC 1/2비트, WDT 중단/조기 kick, GPIO 마커↔리셋 핀 시간 측정.
- **꼬리1. What's FTTI and how do you show you meet it?** — "The fault-tolerant time interval is how long a fault can exist before it can lead to a hazard. The detection time plus the reaction time must fit inside it. I measure both directly on the bench — the injection instant is marked with a GPIO, the safety output or reset is another channel — and I report the worst case across temperature and load, not one measurement." · 한국어: 위험까지 허용 시간, 감지+반응 ≤ FTTI, GPIO 마커로 직접 측정, PVT 최악값 보고.
- **꼬리2. How are latent faults handled?** — "A latent fault is one in a safety mechanism or in rarely used logic that doesn't show up until a second fault happens. That's why there are startup tests — logic and memory BIST — and periodic self-tests. I validate that BIST actually runs, fits the boot-time budget, and blocks boot or raises an error when it finds a fault, using whatever fault-injection test mode the vendor provides." · 한국어: 잠재 고장 → 시동/주기 BIST, 실행·시간 예산·실패 시 차단을 주입 모드로 검증.
- **꼬리3. What about false positives?** — "Equally important: a voltage monitor that trips during normal droop or cold crank shuts the system down for no reason. So I run the full PVT and power-event matrix with no injection and require zero safety events, and I check the margin between the monitor thresholds and the real worst-case rail behavior I measured during characterization." · 한국어: 오탐도 검증 — 주입 없이 PVT·크랭크 전체에서 이벤트 0, 임계값과 실측 최악 rail 사이 여유 확인.

</details>

### 4.3 (추가) "What happens to your board during cold crank, and how would you test it?"

<details>
<summary>Q → 꼬리1 (data integrity) → 꼬리2 (EV)</summary>

- **Q.** — "The 12-volt input can drop to a few volts for tens of milliseconds and then sit low for up to seconds. The front-end regulator should ride through shallow dips; for deep ones the PMIC's undervoltage monitors should reset the SoC cleanly. I'd replay the standard and OEM crank profiles with a programmable supply, probe the input, an intermediate rail, the core rail and reset together, and log reset reasons, link status and error counters over hundreds of repetitions." · 한국어: 입력 수 V로 dip → 얕으면 버티고 깊으면 깨끗한 리셋, 프로그래머블 PSU로 프로파일 재생, 4채널 + 로그, 수백 회 반복.
- **꼬리1. What about data being written during the dip?** — "That's the same problem as power-loss protection in SSDs, which I've tested: writes must be atomic or journaled, and the system must boot to a consistent state. I'd inject the dip at random points during writes and verify integrity after every reboot." · 한국어: SSD PLP와 같은 문제 — 쓰기 중 무작위 dip 주입 후 무결성 확인.
- **꼬리2. Does cold crank matter for an EV?** — "Less in the classic sense, because there's no starter motor and the 12-volt bus is fed by a DC-DC from the high-voltage pack. But the OEM's low-voltage power spec still includes dips, restarts and transients, so I'd test to that spec rather than assume." · 한국어: 시동 모터 없어 덜하지만 OEM 저전압 스펙의 dip·재시작 시험은 그대로 [추정].

</details>

### 4.4 (추가) "How would you validate a camera SerDes link on this board?"

<details>
<summary>Q → 꼬리1 → 꼬리2</summary>

- **Q.** — "Bottom up: power-over-coax voltage at the camera, serializer and deserializer lock status, then the back-channel — can I reach the sensor's registers over I2C through the link — then the sensor test pattern through CSI-2 into the SoC, checking frame counts and CRC or ECC error counters. Then margin: the SerDes' own PRBS and error counters across cable lengths and temperature, and behavior during supply dips." · 한국어: PoC 전압 → lock → 역채널 I2C → 테스트 패턴·프레임/CRC 카운터 → PRBS·케이블·온도·전원 dip.
- **꼬리1. One camera out of eight drops frames at hot.** — "Swap the camera and the cable to the other port to see if the problem follows the camera, the cable or the SoC port. Then compare the SerDes error counters and link margin of that port at room and hot, and check its PoC voltage under load." · 한국어: 카메라·케이블 swap으로 따라가는 쪽 확인 → 포트별 카운터·margin·PoC 비교.
- **꼬리2. How do you keep eight cameras time-aligned?** — "A frame sync signal distributed to all sensors, often through the SerDes GPIO tunnel, and timestamps from a common time base — on the SoC side that might be tied to gPTP time from automotive Ethernet. Validation measures frame-start skew across cameras with a scope or timestamp logs." · 한국어: FSYNC 분배 + 공통 시간축(gPTP) → 카메라 간 skew 측정 [추정: 구성별].

</details>

## 5. Don 경험 연결

- **Card (30 s)**: "My background is datacenter SSDs, which share a lot with automotive: high reliability expectations, full temperature characterization in chambers — I automated that at SK hynix across [숫자 채우기: 온도 범위·챔버 수] — power-loss protection testing, which is the same problem as cold crank and brown-out, and ECC with error injection to prove the correction and reporting paths. What's new for me is the formal ISO 26262 process, and I'd map it onto the same discipline: requirements traced to tests, fault injection with measured reaction times, and traceable records."
- **연결 포인트 (Don이 채울 것)**:
  - SSD **PLP 테스트**: 전원 차단 시점 무작위화, 몇 회, 어떤 무결성 체크 → [숫자 채우기]
  - SSD **ECC/LDPC 에러 주입** 경험 여부와 방법 → [숫자 채우기]
  - SK hynix **챔버 온도 범위**·사이클 → [숫자 채우기]
- **정직 라인**: ISO 26262 인증 프로젝트 경험은 없다면 "I haven't owned an ISO 26262 work product, but I've validated the same kinds of mechanisms in storage" 로.

## 6. 실습 / 코드

- 이 노트는 코드 없음 (spec상 선택). 대신 **랩 레시피** `[미실행]`:
  1. 프로그래머블 PSU(SCPI list mode)로 ISO 16750-2 레벨 I–IV 형태의 크랭크 프로파일을 시퀀스로 만들고, 아무 개발 보드(예: 라즈베리 파이 + 5 V 입력 앞단에 buck)에 적용 → scope 4채널(입력·중간 rail·코어 rail·리셋) 캡처 → 어느 레벨에서 리셋이 나는지 표로.
  2. 위 실험을 Python 오케스트레이터(→ [BV14](2026-11-01_BV14_board_validation_plan_automation.html))로 1000회 반복, UART 로그에서 reset reason·boot count 파싱.
  3. Linux EDAC가 있는 보드에서 `ce_count`/`ue_count` 관찰 (ECC 주입 기능이 있는 컨트롤러라면 debugfs inject 인터페이스 확인).

## 7. 자주 틀리는 것

1. **AEC-Q100 Grade를 Tj 범위로 말함** — 등급은 **주변 온도(Ta)** 기준. Tj는 그보다 높다.
2. **"AEC-Q100 부품을 썼으니 보드는 자동차 OK"** — 부품 인증과 **보드/시스템 검증**은 별개. 보드의 마진·전원 이벤트·EMC·안전은 따로 증명.
3. **12 V = 12 V** — 정상 시험 nominal 13.5 V, 크랭크 때 수 V, load dump 때 수십 V. 그리고 OEM 스펙이 표준보다 우선.
4. **ECC 검증 = "에러가 안 나면 OK"** — 안 나는 건 아무것도 증명 못 함. **주입해서 감지·보고·반응을 보는 것**이 검증.
5. **watchdog을 kick 중단으로만 시험** — windowed watchdog은 **너무 이른 kick**도 시험해야 하고, 디버거 halt 중 동작 설정도 확인.
6. **반응 시간 측정 없음** — 기능 안전은 "감지했다"가 아니라 "**FTTI 안에** 안전 상태로 갔다". 시간을 재야 함.
7. **오탐 검증 누락** — UV 모니터가 정상 droop/크랭크에서 트리거되면 그것도 안전 문제(가용성).
8. **EMC는 HW 팀 일** — 시험 중 최악 활동을 만들고 에러를 감시하는 **테스트 FW**, SSC·클럭 설정은 SW 일.
9. **카메라 링크 문제를 SoC부터 의심** — PoC 전압·케이블·serializer lock부터 아래 계층 증명 (BV 트랙 원칙).

## 8. 출처

1. Infineon Community, "What is the meaning and purpose of AEC-Q100?" (grades 0–3) — https://community.infineon.com/t5/Knowledge-Base-Articles/Meaning-of-AEC-Q100-KBA220733/ta-p/248018
2. Renesas, Temperature Ranges (AEC-Q100 grades, commercial/industrial ranges) — https://www.renesas.com/en/support/technical-resources/temperature-ranges
3. Monolithic Power Systems, "From Cold Crank to Load Dump: A Primer on Automotive Transients" — https://www.monolithicpower.com/en/learning/resources/from-cold-crank-to-load-dump-a-primer-on-automotive-transients
4. Analog Devices, "Load-Dump Protection for 24V Automotive Applications" (Test A/B shape; 12 V 79–101 V / 35 V context) — https://www.analog.com/en/resources/technical-articles/loaddump-protection-for-24v-automotive-applications.html
5. Intertek, ISO 16750-2 Electrical Load Conditions Testing (13.5 V nominal, test list, load dump ≥ 35 V) — https://w3inte.intertek.ch/automotive/standards/iso-16750-2/
6. Infineon Community, AURIX MCU FMEDA (SPFM/LFM per ASIL from ISO 26262-5) — https://community.infineon.com/t5/Knowledge-Base-Articles/AURIX-MCU-FMEDA-KBA236341/ta-p/372906
7. Renesas blog, functional safety support (PMHF < 10 FIT for ASIL D) — https://www.renesas.com/blogs/renesas-functional-safety-support-automotive-2-customizable-option-precise-safety-analysis
8. ANSI blog, CISPR 25 Ed. 4.0/5.0 overview (150 kHz–2.5 GHz, scope) — https://blog.ansi.org/ansi/cispr-25-ed-40-b2016-corrigendum-standard/
9. PEAK-System, "100BASE-T1 and 1000BASE-T1 – What Engineers Need to Know" — https://www.peak-system.com/know-how/blog/100base-t1-and-1000base-t1-what-engineers-need-to-know/
10. Basler, GMSL interface white paper (GMSL generations 3/6/12 Gbps, 15 m, PoC) — https://www.baslerweb.com/en/learning/gmsl-interface/wp/
11. NXP Community, AEC-Q100 for i.MX 6SoloX (ambient vs junction discussion) — https://community.nxp.com/t5/-/-/m-p/429425
12. Power Integrations, SCALE-iDriver AEC-Q100 qualification report (HTOL 1000 h, 3 lots × 77) — https://motor-driver.power.com/sites/default/files/documents/scale-idriver_sic11xxkq_aec-q100_qualification_report.pdf
13. Monolithic Power Systems, "Automotive Electronics Reliability Testing Starts and Ends with the Mission Profile" — https://www.monolithicpower.com/en/learning/resources/automotive-electronics-reliability-testing-starts-and-ends-with-the-mission-profile
14. Noiseken, ISO 16750-2 Ed.4 (2012) 시험 개요 (starting profile 레벨 표, 일본어) — https://noiseken.co.jp/home/wp-content/uploads/2023/02/ISO16750-2Ed.42012の試験概要.pdf
15. AllPCB, "Overview of Automotive EMI/EMC Standard ISO 7637-2" (pulse 1/2a/2b/3a/3b descriptions) — https://www.allpcb.com/allelectrohub/overview-of-automotive-emiemc-standard-iso-76372
16. Clemson CVEL, APEMC 2010 paper on CISPR 25 component test methods (ALSE) — https://cecas.clemson.edu/cvel/pdf/APEMC2010-692.pdf
17. RF Essentials glossary, CISPR 25 (classes 1–5, antennas) — https://rfessentials.com/resources/rf-glossary/cispr-25/
18. The Imaging Source, "GMSL2 vs. FPD-Link III" (6 Gbps/187 Mbps vs 4.16 Gbps/50 Mbps) — https://www.theimagingsource.com/en-us/resource/glossary/fpd-link-iii-vs-gmsl2/
19. Semtech blog, evolution of automotive Ethernet (802.3ch multi-gig, 802.3cg) — https://blog.semtech.com/evolution-and-protection-of-automotive-ethernet-meeting-modern-vehicle-data-demands
