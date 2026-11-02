# N07H · 모의 디버그 드릴 — Q 뱅크 · 채점표 · 플래시카드
<!-- theme: pastel -->

> **묶음**: N07 인터페이스 디버그 — 마지막 페이지 (N07A–G 종합 연습) · **우선**: P1 (D-6 이후 모의용) · **목록**: [N07 인덱스](2026-11-01_N07_interface_debug_scenarios.html) · **이전/다음**: [N07G 카메라 디버그](2026-11-01_N07G_mipi_csi2_camera_debug.html) / 없음 (N07 마지막) → [온사이트 플랜](2026-10-31_rivian_onsite_plan.html) · **관련**: [N06I 10분 Q 뱅크](2026-11-01_N06I_mock_10min_qbank.html), [BV16 보드 Q 뱅크](2026-11-01_BV16_mock_interview_qbank.html), [LP15 PCIe 디버그](2026-10-31_LP15_debug_scenarios.html), [BV13 보드 디버그](2026-11-01_BV13_board_debug_scenarios.html), [deck 7절 16선](2026-10-25_rivian_hm_screen_prep.html), [N07A 틀과 장비](2026-11-01_N07A_debug_framework_instruments.html) · **코드**: 없음 (말하기 연습 페이지)

## 0. TL;DR · 사용법

- 이 페이지 = Technical 2(도메인) 라운드의 **"this interface fails — debug it"** 화이트보드 질문 대비 **프롬프트 30개** (H01–H30): I2C 4 · SPI 3 · UART/GPIO 3 · DDR 5 · Ethernet 5 · CSI-2 5 · PCIe 3 · 혼합/시스템 2. 각 프롬프트는 N07B–G 시나리오 ID(예: **B3**, **F3**)와 LP15/BV13 시나리오에 묶여 있다 — 답이 막히면 그 페이지의 같은 ID로 간다.
- **모든 답은 같은 6칸** (N07 인덱스 한 줄 = [N07A](2026-11-01_N07A_debug_framework_instruments.html) 틀): **① Clarify**(빈도·언제부터·무엇이 바뀌었나·몇 대) → **② Layer**(전기 → 타이밍 → 프로토콜 → 설정/드라이버 → 시스템, 어느 층부터 의심하나) → **③ Counters first**(공짜 증거: 상태 비트·에러 카운터·Linux 도구) → **④ Measure + rule out**(어느 핀을 어떤 장비로, 결과가 A면 무엇 배제) → **⑤ Root cause · fix** → **⑥ Regress**(같은 고장을 잡는 테스트 + 보드 팜 전체).
- 펼친 뼈대 = **60–90초 압축판**. 실전은 3–5분: 칸마다 한두 문장 더하고, 면접관이 끊으면 꼬리로 간다. 꼬리 답은 30–45초.
- **연습 루프 (프롬프트 하나 12분)**: ① 질문 + 꼬리만 보고 녹음 시작 → ② 4분 말하기 (종이에 그림 1장) → ③ 꼬리 3개 각 45초 → ④ 녹음 1.5배속으로 듣고 §1 채점 (/16) → ⑤ `<details>` 펼쳐 비교 → ⑥ §13 기록.
- **★ 먼저 할 8개** (면접관 Manjiang 분야 = Ethernet PHY · 카메라/센서 sync · bring-up [추정]): **H16** link up no traffic · **H17** T1 링크 안 뜸 · **H20** PTP bias · **H21** 카메라 프레임 0 · **H23** 4대 동시 drop · **H11** DDR bit 13 · **H01** I2C 400 kHz · **H30** 처음 보는 인터페이스 1/10k.
- **플래시카드 50장**(§11)은 이동 중 5분용: 증상 한 줄을 가리고 "첫 확인 1개 + 이유"를 소리 내서. **모의 세트 3개**(§12)는 각 45분 = 인터페이스 2 + 혼합 1.
- **중복 피함**: 테스트 플랜 말하기는 [N06I](2026-11-01_N06I_mock_10min_qbank.html) (그곳 D01–D06 디버그 6개도 여기와 안 겹치게 골랐다), 보드 bring-up 30문항은 [BV16](2026-11-01_BV16_mock_interview_qbank.html), deck §7 16선은 [deck](2026-10-25_rivian_hm_screen_prep.html). 여기는 **인터페이스별 증상 → 원인 → 확인 방법**만.
- 숫자 규칙: 스펙 숫자는 §14 출처로 확인한 것만 [확인됨], Rivian 구성(카메라 수·해상도·SerDes 종류)은 전부 [추정]. Don 경험 숫자는 `[숫자 채우기]`.

## 1. 채점표

녹음을 다시 들으면서 항목마다 **0 / 1 / 2**. 애매하면 낮은 점수. **8항목, 16점 만점** — 앞 7개가 N07 스펙의 기본 루브릭, 8번은 Staff 시그널 보너스 항목.

| # | 항목 | 0점 | 1점 | 2점 | 녹음에서 확인할 것 |
|---|---|---|---|---|---|
| 1 | 계층 순서 | 바로 "driver bug" 또는 "SI issue"로 단정 | 계층 단어는 나오지만 순서 없음 | 전기 → 타이밍 → 프로토콜 → 설정 → 시스템 중 **어느 층부터 왜** 를 말하고, 층마다 "OK 증거" 1개 | 첫 1분 |
| 2 | Clarify | 질문 없음 | 1개 | 2–4개 (빈도 · 언제부터/무엇이 바뀌었나 · 몇 대/몇 칩 · 조건: 속도·온도·부하) + "I'll assume …" | 첫 30초 |
| 3 | 측정 지점 · 장비 | "I'd check the signals" | 장비 이름만 | **어느 핀을, 어느 쪽 끝(수신단)에서, 어떤 장비·트리거로** (scope 대역폭, LA 샘플링, protocol analyzer, 내장 카운터) | 장비 + 위치 + 트리거 3개 다 있나 |
| 4 | 배제 논리 | 가설 1개에 꽂힘 | 가설 2–3개 나열 | "If X, it's A; if not, A is ruled out" 형태로 **실험 하나가 가설을 반으로** 자름 (loopback 사다리 · 속도 낮추기 · known-good swap) | "ruled out" / "that tells me" 횟수 |
| 5 | 간헐 · 통계 | 간헐 언급 없음 | "run it many times" | 실패율 p 추정 → **rule of three** (무실패 n회 ≈ 95%로 p < 3/n) + 증폭 손잡이(온도·전압·부하)로 재현율 올리기 | 숫자 1개 이상 |
| 6 | Fix 후 회귀 | "it works now" | 테스트 추가 | 테스트 추가 + **fix를 되돌리면 다시 실패** 확인 + 보드 팜/다른 보드 rev에 적용 | 마지막 30초 |
| 7 | 시간 | 2분 미만 또는 7분 초과 | 3–6분이지만 한 칸에 치우침 | 6칸 모두 + 4분 안팎 + 꼬리 시간 남김 | 타임스탬프 |
| 8 | Staff 시그널 (보너스) | 없음 | "tell the team" | 누구에게 어떤 증거(파형·카운터·재현 스크립트)로 · errata/workaround · 같은 버그류를 막는 체크리스트 | Don 훅 1개 있으면 +0 (과장 없음) |

- **판정**: 13–16 = 온사이트 OK · 9–12 = 가장 낮은 항목 1개만 다음 회차 목표 · 8 이하 = [N07A](2026-11-01_N07A_debug_framework_instruments.html) 5계층 표 + 해당 인터페이스 페이지 §4 한 장 표를 다시 보고 같은 프롬프트 재도전.
- **녹음 표시 약어** (§13 기록용): **CL** clarify · **LY** layer · **CT** counters · **MS** measure · **RO** rule-out · **ST** statistics · **RG** regression.

```text
 4분 시간표 (디버그 프롬프트 하나)
 0:00 ─ ① clarify (20 s)       "How often? Since when? How many boards? Which speed/temp?"
 0:20 ─ ② layer (30 s)         그림: SoC ctrl ─ pins/PHY ─ board ─ device, 5층 중 어디부터
 0:50 ─ ③ counters (40 s)      상태 비트·에러 카운터·Linux 도구 — 공짜 증거 먼저
 1:30 ─ ④ measure + rule out (1 min 30 s)   장비·위치·트리거, "if A then ... ruled out"
 3:00 ─ ⑤ root cause · fix (30 s)
 3:30 ─ ⑥ regress (30 s)       같은 고장 잡는 테스트 + 되돌리면 재실패 + 팜 전체
 4:00 ─ 면접관 꼬리
```

## 2. 질문 목록 한눈에

| ID | Q (요약) | 인터페이스 | 링크 |
|---|---|---|---|
| [H01 ★](#h01-i2c-fails-only-at-400-khz) | Sensor works at 100 kHz, intermittent NACK at 400 kHz | I2C | [N07B](2026-11-01_N07B_i2c_smbus_debug.html) B3 · [BV10](2026-11-01_BV10_board_interface_tests.html) |
| [H02](#h02-register-reads-return-the-wrong-register) | Register reads return another register's value | I2C | [N07B](2026-11-01_N07B_i2c_smbus_debug.html) B4 |
| [H03](#h03-whole-bus-dies-when-one-module-is-powered-off) | Whole bus dies when the camera module is powered off | I2C | [N07B](2026-11-01_N07B_i2c_smbus_debug.html) B6 · [BV13](2026-11-01_BV13_board_debug_scenarios.html) S11 |
| [H04](#h04-arbitration-lost-on-a-single-master-bus-at-hot) | "Arbitration lost" on a single-master bus, only in the hot chamber | I2C | [N07B](2026-11-01_N07B_i2c_smbus_debug.html) B5 · B7 · B8 |
| [H05](#h05-spi-data-shifted-by-one-bit) | SPI flash JEDEC ID comes back shifted by one bit | SPI | [N07C](2026-11-01_N07C_spi_qspi_debug.html) C1 · C3 |
| [H06](#h06-flash-above-16-mb-reads-back-the-start) | Data above 16 MB reads back the first 16 MB | SPI | [N07C](2026-11-01_N07C_spi_qspi_debug.html) C4 · C6 |
| [H07](#h07-boot-rom-cannot-find-flash-after-warm-reset) | Boot ROM sometimes can't find flash after warm reset | SPI | [N07C](2026-11-01_N07C_spi_qspi_debug.html) C2 · C5 · C7 |
| [H08](#h08-console-dies-when-linux-takes-over) | Bootloader console fine, garbage/silence once Linux starts | UART | [N07D](2026-11-01_N07D_uart_gpio_irq_debug.html) D1 · D4 |
| [H09](#h09-3-mbaud-link-drops-bytes) | 3 Mbaud sensor UART drops bytes; 115200 fine | UART | [N07D](2026-11-01_N07D_uart_gpio_irq_debug.html) D2 · D3 |
| [H10](#h10-imu-data-ready-interrupt-missed-or-storming) | IMU data-ready IRQ: sometimes missed, sometimes a storm | GPIO/IRQ | [N07D](2026-11-01_N07D_uart_gpio_irq_debug.html) D5–D8 |
| [H11 ★](#h11-memtest-fails-only-on-bit-13) | Memtest log: every failure is bit 13 | DDR | [N07E](2026-11-01_N07E_ddr_memory_debug.html) E1 · E2 |
| [H12](#h12-upper-half-of-dram-mirrors-the-lower-half) | Upper 2 GB is a copy of the lower 2 GB | DDR | [N07E](2026-11-01_N07E_ddr_memory_debug.html) E3 |
| [H13](#h13-ecc-correctable-errors-climb-at-105-c) | ECC CE count climbs only at 105 °C | DDR | [N07E](2026-11-01_N07E_ddr_memory_debug.html) E4 · E7 · [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html) |
| [H14](#h14-corruption-after-suspend-and-resume) | Data corrupted after suspend/resume | DDR | [N07E](2026-11-01_N07E_ddr_memory_debug.html) E5 |
| [H15](#h15-training-passes-but-linux-panics-weekly) | Training + memtest pass, kernel panics once a week | DDR | [N07E](2026-11-01_N07E_ddr_memory_debug.html) E6 · E8 · [N06I](2026-11-01_N06I_mock_10min_qbank.html) D05 |
| [H16 ★](#h16-link-is-up-but-no-packets-pass) | Link is up but no packets pass | Ethernet | [N07F](2026-11-01_N07F_ethernet_debug.html) F3 · F4 · [BV10](2026-11-01_BV10_board_interface_tests.html) 체인 C |
| [H17 ★](#h17-1000base-t1-link-never-comes-up) | 1000BASE-T1 link never comes up | Ethernet | [N07F](2026-11-01_N07F_ethernet_debug.html) F1 · F2 |
| [H18](#h18-link-flaps-every-few-minutes-in-the-vehicle) | Link flaps every few minutes; SQI drops at hot/EMI | Ethernet | [N07F](2026-11-01_N07F_ethernet_debug.html) F5 · F10 |
| [H19](#h19-ping-works-but-iperf-drops-and-big-frames-fail) | Ping works; iperf shows RX drops; 1500 B pings fail | Ethernet | [N07F](2026-11-01_N07F_ethernet_debug.html) F6 · F7 · F8 |
| [H20 ★](#h20-ptp-offset-has-a-constant-bias) | PTP locked, but a constant 400 ns bias vs reference | Ethernet | [N07F](2026-11-01_N07F_ethernet_debug.html) F9 · [L4](hmScreenPrep/problems/L4_pps_time_discipline/index.html) |
| [H21 ★](#h21-stream-started-zero-frames) | Camera stream started, zero frames arrive | CSI-2 | [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G1 · G2 · G3 |
| [H22](#h22-image-sheared-diagonally-or-green-and-pink) | Image sheared diagonally / colors green-pink | CSI-2 | [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G4 · G5 |
| [H23 ★](#h23-four-cameras-drop-frames-one-camera-is-fine) | Four cameras drop frames, one alone is fine | CSI-2 | [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G6 · G7 |
| [H24](#h24-configured-30-fps-measured-15-fps) | Configured 30 fps, measured 15 fps | CSI-2 | [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G8 · G10 |
| [H25](#h25-two-cameras-are-exactly-one-frame-apart) | Two cameras' timestamps exactly one frame apart | CSI-2 | [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G9 · [V12](hmScreenPrep/problems/V12_frame_timestamp_check/index.html) |
| [H26](#h26-pcie-endpoint-trains-gen3-x2-instead-of-gen4-x4) | Endpoint trains Gen3 x2 instead of Gen4 x4 | PCIe | [LP15](2026-10-31_LP15_debug_scenarios.html) S2 · S3 · [N01](2026-10-31_N01_pcie_lspci_ltssm.html) |
| [H27](#h27-aer-rxerr-bursts-only-with-aspm-l1) | AER RxErr bursts only when ASPM L1 is enabled | PCIe | [LP15](2026-10-31_LP15_debug_scenarios.html) S7 · S8 |
| [H28](#h28-bare-metal-config-read-hangs-the-core) | Bare-metal: first config read hangs the core | PCIe | [LP15](2026-10-31_LP15_debug_scenarios.html) S10 · S9 · [N01](2026-10-31_N01_pcie_lspci_ltssm.html) |
| [H29](#h29-several-interfaces-go-flaky-together-on-a-new-board-rev) | New board rev: I2C, Ethernet and camera all flaky together | 혼합 | [BV13](2026-11-01_BV13_board_debug_scenarios.html) S10 · S12 · [N07A](2026-11-01_N07A_debug_framework_instruments.html) |
| [H30 ★](#h30-an-interface-you-have-never-seen-fails-1-in-10000) | An interface you've never seen fails 1 in 10,000 | 혼합 | [N07A](2026-11-01_N07A_debug_framework_instruments.html) · [N06H](2026-11-01_N06H_debug_methodology_triage.html) |

- deck §7 #4(인터럽트 누락) · #8(카메라 ECC drop) · #10(FCS loopback) · #14(64 B 반토막)는 이미 deck에 답이 있어서 H10 · H21 · H16 · H19의 **꼬리**로만 넣었다.

## 3. I2C · SMBus 4

> I2C 답의 첫 줄은 항상 **"Which line, what level, what does the analyzer say on the ACK bit?"** — 아날로그(scope)와 디코드(analyzer)를 같이 본다. 기초: [N07B](2026-11-01_N07B_i2c_smbus_debug.html) §1–2, [BV10 §2.1](2026-11-01_BV10_board_interface_tests.html).

### H01 · I2C fails only at 400 kHz ★

- **Q.** "A temperature sensor on our board works fine at 100 kHz. At 400 kHz about one transaction in a thousand returns a NACK or wrong data. Debug it."
- **꼬리1.** "The pull-up is 4.7 kΩ to 3.3 V and you estimate 200 pF on the bus. Is that OK? What value would you use?"
- **꼬리2.** "We can't respin the board this quarter. What are your options?"
- **꼬리3.** "You measured rise time and it's within spec, but it still fails at 400 kHz. Now what?"
- **시나리오**: [N07B](2026-11-01_N07B_i2c_smbus_debug.html) B3 (rise time · pull-up 계산)

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Is it one device or every device on that bus? Same failure on every board? Does it change with temperature? And is the failing byte the address or data?"
- **② Layer** — "Speed-dependent with the same software means electrical or timing first: I2C is open-drain, so the rising edge is set by the pull-up and the bus capacitance, and 400 kHz gives it a third of the time 100 kHz does."
- **③ Counters** — "The controller's NACK and timeout status, and in Linux the errno: ENXIO is an address NACK, ETIMEDOUT a timeout, EAGAIN lost arbitration."
- **④ Measure + rule out** — "Scope SCL and SDA at the far device, 10x probe with a short ground. Measure rise time between 30 and 70 percent of VDD: Fast-mode allows 300 ns, Standard-mode 1000 ns. Also measure the actual tLOW and tHIGH the controller generates; Fast-mode needs tLOW at least 1.3 µs. If rise time is over 300 ns, that's my cause; if it's fine, I've ruled out RC and move to clock timing and the spike filter."
- **⑤ Fix** — "Pick a stronger pull-up between the sink-current minimum and the rise-time maximum, or reduce capacitance by splitting the bus."
- **⑥ Regress** — "A board test that measures rise time on every bus at 400 kHz, plus a million-transaction soak; one in a thousand before means I want zero in at least 3000, realistically a million."
- **꼬리1** — "Rise time is about 0.85 RC from 30 to 70 percent, so 4.7 k times 200 pF gives roughly 800 ns: OK for 100 kHz, far over 300 ns for 400 kHz. The maximum is 300 ns over 0.85 times 200 pF, about 1.8 kΩ. The minimum is set by the 3 mA sink current: 3.3 minus 0.4 volts over 3 mA, about 1 kΩ. So 1.5 kΩ."
- **꼬리2** — "Run that bus at a lower speed, which costs configuration time; add an active rise-time accelerator or buffer that splits the capacitance; increase the controller's SCL low time so data has longer to settle. I'd quantify each and pick with the system team."
- **꼬리3** — "Then probe at the receiver, not the controller: ringing or a slow edge at the far end matters. Check tLOW, tHIGH and data setup on the decoded waveform against the Fast-mode table, glitches that the 50 ns spike filter doesn't reject, and level shifters, which often add their own edge-rate limits."
- **한국어 핵심**: 속도 의존 = 전기/타이밍 · rise time 30→70% 측정, Fm 300 ns / Sm 1000 ns · tr ≈ 0.8473·Rp·Cb → 4.7k·200 pF ≈ 800 ns ❌ · Rp 범위 ≈ 1 kΩ–1.8 kΩ → 1.5 kΩ · 수신단에서 측정 · 회귀 = rise time 측정 + 100만 회 soak.
- **보러 갈 곳**: [N07B](2026-11-01_N07B_i2c_smbus_debug.html) B3 · [BV10 §2.1.1 풀업 계산](2026-11-01_BV10_board_interface_tests.html) · [BV11 장비](2026-11-01_BV11_lab_instruments_measurement.html)

</details>

### H02 · Register reads return the wrong register

- **Q.** "Reading register 0x0F of a PMIC sometimes returns the value of register 0x00, and sometimes another register. Writes look fine. What's going on?"
- **꼬리1.** "How do you prove what's on the wire?"
- **꼬리2.** "The kernel driver and a userspace tool both talk to that PMIC. Does that matter?"
- **꼬리3.** "The device datasheet says it doesn't support repeated START. Now what?"
- **시나리오**: [N07B](2026-11-01_N07B_i2c_smbus_debug.html) B4 (repeated START vs STOP+START)

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Always wrong or intermittent? Single-byte or multi-byte reads? Who issues the read: kernel driver, i2c-tools, or bare-metal code?"
- **② Layer** — "Writes work and the device ACKs, so electrical is probably OK. This smells like protocol: a register read is a write of the register pointer followed by a read, and how those two are glued matters."
- **③ Counters** — "No error is reported, which itself is a clue: every byte is ACKed, the data is just from the wrong place."
- **④ Measure + rule out** — "Protocol analyzer on SCL and SDA. A correct read is START, address+W, 0x0F, then a repeated START, address+R, data, NACK, STOP. If I see a STOP between the pointer write and the read, the device may reset its pointer on STOP, or another transaction can slip in between. If the sequence is right, I check whether the device auto-increments and whether a multi-byte value must be read in one burst."
- **⑤ Fix** — "Issue the pointer write and read as one combined transfer, i2c_transfer with two messages, or i2ctransfer from userspace, so the adapter emits a repeated START."
- **⑥ Regress** — "A read-back test of every documented register against known reset values, run while another thread hammers the same bus."
- **꼬리1** — "Analyzer decode marks Sr versus P followed by S. I'd also toggle a GPIO at the start of each software call so I can line up the wire with the code."
- **꼬리2** — "Yes. Two separate calls, write then read, are two transactions; the bus lock is per transaction, so the driver's access can land between them and move the pointer. Combined transfers, or not touching a device a kernel driver owns, fixes it."
- **꼬리3** — "Then STOP plus START is correct for that device and I make sure nobody else can access it in between: one owner, or a lock around the pair. Linux adapters even have a quirk flag for controllers that can't do repeated START, so the core knows."
- **한국어 핵심**: 쓰기 OK + ACK 정상 + 값만 엉뚱 = 프로토콜(포인터) · 정상 = START·W·reg·**Sr**·R·data·NACK·STOP · 두 번 호출 = 두 트랜잭션 → 사이에 끼어듦 · fix = combined transfer(i2c_transfer 2 msg / i2ctransfer) · 회귀 = 리셋값 대조 + 동시 접근.
- **보러 갈 곳**: [N07B](2026-11-01_N07B_i2c_smbus_debug.html) B4 · [V8](hmScreenPrep/problems/V8_i2c_scan_recover/index.html)

</details>

### H03 · Whole bus dies when one module is powered off

- **Q.** "When we power down one camera module to save power, every device on that I2C bus stops responding, including the PMIC. Power the module back on and everything works. Debug it."
- **꼬리1.** "What would you see on the scope?"
- **꼬리2.** "How do you find which device is doing it without removing parts?"
- **꼬리3.** "How do you make sure the next board rev doesn't have this?"
- **시나리오**: [N07B](2026-11-01_N07B_i2c_smbus_debug.html) B6 (back-power) · 비교: [BV13](2026-11-01_BV13_board_debug_scenarios.html) S11 NACK

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Is the module on the same bus segment, or behind a mux or a buffer? Where are the pull-ups powered from? Does it happen instantly at power-off or later?"
- **② Layer** — "Everything fails at once, so it's electrical on the shared lines, not one device's protocol."
- **③ Counters** — "Every transaction times out or NACKs on address; recovery with nine clocks doesn't help, which already says it's not a stuck state machine."
- **④ Measure + rule out** — "Scope both lines with the module off. If they sit at a low but nonzero level, a few hundred millivolts to about 0.7 V, an unpowered device's ESD diodes are clamping SDA and SCL to its dead supply: the pull-ups are back-powering it. I also measure the module's dead rail: if it rises to a volt or so with the module 'off', that's confirmation."
- **⑤ Fix** — "Put the module behind a bus switch, mux, or buffer that isolates when unpowered, or keep its I/O supply on, or choose parts whose I/O tolerates being powered off."
- **⑥ Regress** — "A board test that powers each switchable module off and on while exercising every other device on the bus."
- **꼬리1** — "Instead of clean 0 to 3.3 V edges, the high level collapses to roughly a diode drop and the edges get very slow; the master can't reach a valid high."
- **꼬리2** — "Lift segments: disable the mux channel, or remove a zero-ohm link if the layout has one; measure the dead rail voltage on each powered-off device; or measure current flowing through the pull-ups."
- **꼬리3** — "Add it to the schematic review checklist: every I2C device on a switchable rail must be isolated or I/O-tolerant, and pull-ups must be on the right domain. That catches the class of bug, not just this one."
- **한국어 핵심**: 전부 동시 실패 = 공유선 전기 문제 · 9-clock 안 통함 = 상태기계 아님 · 낮은 비0 레벨 + 죽은 rail 전압 상승 = **back-power(ESD 다이오드)** · fix = 분리 스위치/mux/버퍼 · 회귀 = 모듈별 전원 on/off 중 버스 시험 + 회로도 체크리스트.
- **보러 갈 곳**: [N07B](2026-11-01_N07B_i2c_smbus_debug.html) B6 · [BV10 체인 B 꼬리4](2026-11-01_BV10_board_interface_tests.html)

</details>

### H04 · Arbitration lost on a single-master bus at hot

- **Q.** "In the 105 °C chamber, roughly one I2C transaction in ten thousand fails with 'arbitration lost'. There's only one master on the bus. At room temperature, zero failures in a day. Debug it."
- **꼬리1.** "How do you capture a one-in-ten-thousand event on a scope?"
- **꼬리2.** "The sensor datasheet says it can stretch the clock up to 40 ms during conversion. Problem?"
- **꼬리3.** "After your fix, how long do you run before you say it's fixed?"
- **시나리오**: [N07B](2026-11-01_N07B_i2c_smbus_debug.html) B5 · B7 · B8

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Is it really a single master, nothing like a PMIC, BMC or a serializer back-channel that can drive the bus? Which device and which byte when it fails? All boards at hot, or some?"
- **② Layer** — "Arbitration lost means the controller drove SDA high but read it back low. With one master, that's a glitch or slow edge on SDA, or a hidden second driver. Temperature-dependent points to electrical margin: leakage up, edges slower, thresholds moved."
- **③ Counters** — "Linux returns EAGAIN for lost arbitration. I log the count with temperature, and which transaction failed."
- **④ Measure + rule out** — "Scope SDA and SCL at the controller with segmented memory, triggered by a GPIO the driver toggles when it sees EAGAIN, with a long pre-trigger so I capture the bad bit. A runt pulse or slow rise at the sample point means electrical; a clean low pulse driven during a high bit means a second driver. A 1-in-10k event at, say, 100 transactions per second shows up every couple of minutes, so this is quick."
- **⑤ Fix** — "Electrical: pull-up strength, filtering, ground. Hidden master: arbitrate ownership or separate buses."
- **⑥ Regress** — "Hot-chamber soak in the nightly farm with failure counts per temperature, not just pass/fail."
- **꼬리1** — "Don't trigger on the bus, trigger on the software symptom: a GPIO edge on error, pre-trigger to see the transaction. Segmented memory keeps hundreds of these without filling memory; infinite persistence also shows outlier edges."
- **꼬리2** — "Clock stretching is legal I2C, but my controller's timeout and the driver's timeout must be longer than 40 ms, and if this is an SMBus-mode controller, SMBus times out at 25 to 35 ms, so a 40 ms stretch violates SMBus. Either poll a ready bit instead of reading during conversion, or configure the timeout."
- **꼬리3** — "Rule of three: zero failures in n trials bounds the rate at about 3 over n with 95 percent confidence. To claim better than one in ten thousand I need 30,000 clean transactions at hot; I'd run ten times that, and revert the fix once to confirm it fails again."
- **한국어 핵심**: 단일 master arbitration lost = SDA 되읽기 불일치 → 글리치/느린 에지 또는 숨은 driver · 고온 = 마진 · SW 에러 시 GPIO 토글 → segmented capture · SMBus timeout 25–35 ms vs 40 ms stretch · rule of three → 3만 회 무실패 이상.
- **보러 갈 곳**: [N07B](2026-11-01_N07B_i2c_smbus_debug.html) B5 · B7 · B8 · [N07A](2026-11-01_N07A_debug_framework_instruments.html) (segmented memory) · [BV13](2026-11-01_BV13_board_debug_scenarios.html) S7

</details>

## 4. SPI · QSPI flash 3

> SPI 답의 첫 질문: **"All speeds or only fast? All bytes or a pattern?"** — 모든 속도에서 = 모드/설정, 빠를 때만 = 샘플 타이밍. 기초: [N07C](2026-11-01_N07C_spi_qspi_debug.html) §1–2, [BV10 체인 E](2026-11-01_BV10_board_interface_tests.html).

### H05 · SPI data shifted by one bit

- **Q.** "We read the JEDEC ID of a NOR flash. The datasheet says EF 40 18. We get DE 80 30. Debug it."
- **꼬리1.** "What if we'd gotten F7 A0 0C instead?"
- **꼬리2.** "It's correct at 25 MHz, shifted only at 100 MHz. Different answer?"
- **꼬리3.** "What regression would you leave behind?"
- **시나리오**: [N07C](2026-11-01_N07C_spi_qspi_debug.html) C1 (모드) · C3 (100 MHz 샘플 지연)

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "At every clock rate? Every read, or intermittent? Is the command itself received correctly, for example does a status read work?"
- **② Layer** — "A consistent, deterministic transformation is protocol or configuration, not noise. Write the bits out: EF 40 18 shifted left by one bit is exactly DE 80 30. So the controller samples one bit late."
- **③ Counters** — "Nothing errors on SPI; there's no ACK or CRC on a basic read. That's why a known pattern like the JEDEC ID is the first test."
- **④ Measure + rule out** — "Logic analyzer on SCLK, CS, MOSI and MISO, sampled at least four to ten times the clock. Check CPOL and CPHA: flash usually supports mode 0 and 3, sampling on the rising edge. If the controller is set to sample on the falling edge, or its read-capture delay is set one cycle, I get a one-bit shift at every speed. If the mode is correct, then it's sample timing."
- **⑤ Fix** — "Set the controller mode to match the flash, and reset the sample-delay setting to the documented default."
- **⑥ Regress** — "Read the ID and a CRC of a known region at every supported speed and mode in board test."
- **꼬리1** — "F7 A0 0C is EF 40 18 shifted right with a leading 1: sampling one bit early, so the first bit is whatever MISO floats to before the flash drives it. Same class of cause, opposite direction."
- **꼬리2** — "Yes. Then the mode is right and the round trip is the problem: clock out, flash output delay, trace back, and the controller samples before the data arrives. I sweep the read-capture delay taps at 100 MHz, find the passing window, and set the middle. That's a mini shmoo; I'd repeat at hot and cold."
- **꼬리3** — "The sample-delay sweep itself: record window width per board and temperature. A window shrinking over board revs is a warning long before it fails."
- **한국어 핵심**: 비트를 써 보기 — EF4018 ≪1 = DE8030 → **한 비트 늦게 샘플** · 반대(≫1 + 선행 1) = 일찍 · 모든 속도 = CPOL/CPHA/설정, 고속만 = 왕복 지연 → read-capture delay sweep(미니 shmoo) · 회귀 = 창 폭 기록.
- **보러 갈 곳**: [N07C](2026-11-01_N07C_spi_qspi_debug.html) C1 · C3 · [BV10 체인 E](2026-11-01_BV10_board_interface_tests.html)

</details>

### H06 · Flash above 16 MB reads back the start

- **Q.** "We have a 64 MB QSPI flash. We programmed a full image, but anything above 16 MB reads back as the first 16 MB. Debug it."
- **꼬리1.** "Linux reads it fine, U-Boot doesn't. What does that tell you?"
- **꼬리2.** "Single-wire reads are fine, quad reads return garbage. Why?"
- **꼬리3.** "What test catches this kind of problem generally?"
- **시나리오**: [N07C](2026-11-01_N07C_spi_qspi_debug.html) C4 (4-byte 주소) · C6 (quad)

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Exactly at 16 MB? Read and write both, or only read? Which software stack?"
- **② Layer** — "Exactly 16 MB is 2 to the 24: 3-byte addressing. That's configuration or protocol, not electrical."
- **③ Counters** — "Nothing errors. The flash happily wraps a 24-bit address."
- **④ Measure + rule out** — "Logic analyzer on a read at 20 MB: count address bytes after the opcode. Three bytes means the top byte is lost. The flash needs either 4-byte opcodes, for example 13h for read instead of 03h, or an enter-4-byte-mode command. If the analyzer shows four bytes and it still wraps, I check that the flash is really in 4-byte mode and the controller's address-length setting."
- **⑤ Fix** — "Use 4-byte opcodes, which don't change persistent state, rather than a mode switch, or make sure the mode is set consistently by every stage."
- **⑥ Regress** — "An address-signature test: write a unique value at every 4 KB across the whole device, read all back. Any aliasing shows as a repeated signature."
- **꼬리1** — "The hardware is fine and the drivers differ: Linux spi-nor reads SFDP and picks 4-byte opcodes; the bootloader is configured for 3-byte. Fix the bootloader config, and the boot ROM's behavior matters too."
- **꼬리2** — "Quad needs the Quad Enable bit in a status register set; without it IO2 and IO3 still act as write-protect and hold. Also dummy cycles must match the flash's setting at that frequency. And the board needs IO2/IO3 routed, not tied off as WP# and HOLD#."
- **꼬리3** — "Address-signature tests catch aliasing on any memory, flash or DRAM: same idea as an address-bus test in a memtest."
- **한국어 핵심**: 정확히 16 MB = 2^24 = 3-byte 주소 → 4-byte opcode(13h 등) 또는 4-byte 모드 · Linux만 OK = SFDP vs 부트로더 설정 차이 · quad 실패 = QE 비트·dummy cycle·IO2/3 배선 · 회귀 = 4 KB마다 고유 서명.
- **보러 갈 곳**: [N07C](2026-11-01_N07C_spi_qspi_debug.html) C4 · C6 · [V1 주소 테스트](hmScreenPrep/problems/V1_march_memtest/index.html)

</details>

### H07 · Boot ROM cannot find flash after warm reset

- **Q.** "Cold boots are fine. After a software warm reset, about one boot in fifty the boot ROM reports 'no boot device', and a debugger shows the flash ID read back as FF FF FF. Debug it."
- **꼬리1.** "How do you prove your theory on the bench?"
- **꼬리2.** "Separately: DMA reads from the flash are corrupt, PIO reads are fine."
- **꼬리3.** "What goes into the reset test?"
- **시나리오**: [N07C](2026-11-01_N07C_spi_qspi_debug.html) C2 · C5 · C7

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Only after warm reset, never cold? What was the system doing before reset: writing, erasing, or running in quad or 4-byte mode? Does the flash's reset pin connect to the SoC reset?"
- **② Layer** — "Cold fine and warm intermittent says the SoC got reset but the flash didn't: it's still in a state the boot ROM doesn't expect. That's state and sequencing, not signal integrity."
- **③ Counters** — "Boot ROM status or boot-error code if it exposes one; FF FF FF means MISO isn't driven in the way the ROM expects."
- **④ Measure + rule out** — "Capture SCLK, CS and IO lines on the first boot ROM access after warm reset. If the ROM sends a single-wire 03h read but the flash is still in QPI mode, 4-byte mode, mid-erase, or deep power-down, it ignores or misinterprets it. One in fifty fits 'reset arrived during an erase or while in quad mode'. I also check power-up timing on cold boot: first access before the flash's ready time can give FFs too."
- **⑤ Fix** — "Route a reset to the flash's RESET# pin on every SoC reset, or issue the software reset sequence 66h then 99h before the warm reset, and avoid persistent mode switches."
- **⑥ Regress** — "A warm-reset loop of thousands of resets, injecting the reset in each flash state: idle, mid-erase, mid-program, quad mode, 4-byte mode."
- **꼬리1** — "Force the state on purpose: put the flash in QPI or start a long sector erase, then warm-reset. If that fails every time, the theory is confirmed and the 1-in-50 was just how often the reset hit that window."
- **꼬리2** — "Different path, different cause: DMA problems with PIO OK are usually cache maintenance, buffer alignment, or the FIFO threshold and burst size. Invalidate the destination buffer after DMA, align to cache lines, and compare with the cache off."
- **꼬리3** — "Every reset type the product has, warm, watchdog, cold, crossed with every flash state, counted in the farm."
- **한국어 핵심**: cold OK / warm 간헐 = SoC만 리셋, flash는 이전 상태(QPI·4-byte·erase 중·deep power-down) · 66h→99h 소프트 리셋 또는 RESET# 배선 · 상태를 강제로 만들어 재현 = 증명 · DMA만 깨짐 = 캐시·정렬·FIFO.
- **보러 갈 곳**: [N07C](2026-11-01_N07C_spi_qspi_debug.html) C2 · C5 · C7 · [BV13](2026-11-01_BV13_board_debug_scenarios.html) S9 (warm reset)

</details>

## 5. UART · GPIO · 인터럽트 3

> UART 첫 측정은 **비트 폭 하나** (115200 → 8.68 µs). GPIO/IRQ 첫 확인은 **/proc/interrupts 카운트 vs 기대 rate**. 기초: [N07D](2026-11-01_N07D_uart_gpio_irq_debug.html), [BV10 체인 F](2026-11-01_BV10_board_interface_tests.html).

### H08 · Console dies when Linux takes over

- **Q.** "The bootloader prints fine on the debug UART. As soon as the kernel's serial driver takes over, the console prints garbage, and on some builds nothing at all. Debug it."
- **꼬리1.** "You measure the bit width at 10.4 µs instead of 8.68. What does that tell you?"
- **꼬리2.** "How do you see where in the boot it goes wrong?"
- **꼬리3.** "What's the regression?"
- **시나리오**: [N07D](2026-11-01_N07D_uart_gpio_irq_debug.html) D1 · D4 · [BV13](2026-11-01_BV13_board_debug_scenarios.html) S3

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Garbage or silence, which build gives which? Did earlycon work? What changed: device tree, kernel version, clock driver?"
- **② Layer** — "Bootloader works, so pins, level and cable are fine. What changes at the handover is configuration: baud, input clock, pinmux, clock gating. Garbage usually means wrong baud; silence means the UART lost its clock or its pins."
- **③ Counters** — "The serial driver's framing-error counter on the far side, and dmesg for the driver's probe line and the console= handover."
- **④ Measure + rule out** — "Scope the TX pin, measure one bit. If the bit width is wrong by a fixed ratio, the driver thinks the input clock is a different frequency than it is. If TX stops toggling, check whether the kernel turned off a clock it thinks is unused, or a pinmux in the device tree took the pin; booting with clk_ignore_unused is a quick test of the clock theory."
- **⑤ Fix** — "Correct the clock in the device tree or clock driver, or fix the pinmux, and make console= and stdout-path consistent."
- **⑥ Regress** — "The farm checks for an expected string at each boot stage, ROM, bootloader, kernel, login, so the failing stage is reported, not just 'no console'."
- **꼬리1** — "One over 10.4 µs is about 96,000 baud, a ratio of 1.2 to 115,200. That's a clock mismatch, for example the driver assumes a 24 MHz input and the UART really gets 20 MHz. Fix the clock description, not the divisor."
- **꼬리2** — "earlycon with the UART's address keeps output alive through early boot, so I see the last message before it breaks. Combined with initcall_debug I can see the driver that changed the clock."
- **꼬리3** — "Boot-stage string checks with per-stage timeouts in every nightly run, across every device tree variant."
- **한국어 핵심**: 부트로더 OK = 전기 OK → 넘겨받을 때 바뀌는 것(입력 클럭·분주·pinmux·clock gating) · 쓰레기 = baud, 무출력 = 클럭/핀 · 비트 폭 비율 1.2 = 입력 클럭 설명 오류 · clk_ignore_unused · earlycon.
- **보러 갈 곳**: [N07D](2026-11-01_N07D_uart_gpio_irq_debug.html) D1 · D4 · [BV10 체인 F](2026-11-01_BV10_board_interface_tests.html)

</details>

### H09 · 3 Mbaud link drops bytes

- **Q.** "A sensor streams over UART at 3 Mbaud into the SoC. At 115200 everything is fine. At 3 Mbaud we lose a few bytes per minute. Debug it."
- **꼬리1.** "How do you tell overrun from a baud-rate error?"
- **꼬리2.** "At 105 °C framing errors appear too. Why?"
- **꼬리3.** "How much baud mismatch can 8N1 tolerate?"
- **시나리오**: [N07D](2026-11-01_N07D_uart_gpio_irq_debug.html) D2 · D3

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Lost bytes or corrupted bytes? Interrupt-driven or DMA? Is flow control wired? What's the UART input clock?"
- **② Layer** — "Two very different causes give 'missing bytes': timing at the bit level, which is baud error, or the system not emptying the FIFO in time, which is overrun. The counters separate them."
- **③ Counters** — "The 16550-style line status register has separate overrun, parity, framing and break bits; Linux 8250 shows oe, fe, pe and brk counts in /proc/tty/driver/serial. Overrun means software latency; framing means bit timing."
- **④ Measure + rule out** — "If overrun: at 3 Mbaud a byte arrives every 3.3 µs, so a 64-byte FIFO fills in about 200 µs; I measure interrupt latency with a GPIO toggled in the ISR, look at the FIFO trigger level, and move to DMA or enable RTS/CTS. If framing: compute the real divisor. 48 MHz over 16 gives exactly 3 Mbaud; 50 MHz over 16 gives 3.125 Mbaud, 4 percent off, which is at the edge."
- **⑤ Fix** — "Overrun: DMA, lower trigger level, flow control. Baud: pick a UART clock that divides exactly, or a fractional divider."
- **⑥ Regress** — "A long-run byte-counted stream with a sequence pattern at max baud and hot, failing on any oe or fe increment."
- **꼬리1** — "The counters: oe goes up for overrun, fe for framing. Also the pattern: overrun loses whole bytes in bursts under CPU load; baud error corrupts bytes, usually high bits or the stop bit, independent of load."
- **꼬리2** — "Something drifts with temperature: an internal RC oscillator on one side is typical. With a marginal static error, a percent of drift tips it over. Measure the bit width at hot."
- **꼬리3** — "The receiver samples the stop bit at about 9.5 bit times after the start edge, and with 16x oversampling it loses up to a sixteenth of a bit finding the start. So total mismatch budget is roughly (0.5 minus 1/16) over 9.5, about 4.6 percent, split between both ends: keep each side within about 2 percent."
- **한국어 핵심**: 빠진 바이트 = overrun(SW 지연) vs framing(baud) → LSR OE/FE · /proc/tty/driver/serial oe·fe · 3 Mbaud = 바이트당 3.3 µs, 64B FIFO ≈ 200 µs · 50 MHz/16 → 4% 오차 · 8N1 허용 ≈ 4.6% 합 (각 ±2%) [계산].
- **보러 갈 곳**: [N07D](2026-11-01_N07D_uart_gpio_irq_debug.html) D2 · D3 (baud 계산기 코드)

</details>

### H10 · IMU data-ready interrupt missed or storming

- **Q.** "An IMU raises a data-ready interrupt at 1 kHz on a GPIO. Sometimes we miss samples. On one build the system locks up with an interrupt storm. Debug both."
- **꼬리1.** "How do you measure interrupt latency and catch a missed one?"
- **꼬리2.** "Some interrupts are counted twice."
- **꼬리3.** "It stops waking the system from suspend."
- **시나리오**: [N07D](2026-11-01_N07D_uart_gpio_irq_debug.html) D5 · D6 · D7 · D8 · deck §7 #4

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Is the IMU output a pulse or a latched level until read? Edge or level configured? Push-pull or open-drain, and active high or low?"
- **② Layer** — "This is mostly configuration and protocol between device and interrupt controller: trigger type, polarity, and the clear order. Electrical only if the edge is slow or noisy."
- **③ Counters** — "/proc/interrupts for that line over 10 seconds should read 10,000. Fewer means missed; a runaway count means storm. The IMU's own sample counter or FIFO overflow flag tells me what it sent."
- **④ Measure + rule out** — "Scope the IMU pin and a GPIO toggled in the ISR. Missed: if the pulse is shorter than the controller's detection window, or a new edge arrives while the line is masked and the controller only latches one, edges get lost; with a level-latched IMU, I configure level and clear at the source. Storm: level-triggered with the source not cleared, or the polarity inverted so the line is permanently 'active'. Also check write-1-to-clear status bits aren't being cleared by read-modify-write, which clears other pending bits."
- **⑤ Fix** — "Match trigger type and polarity to the device, clear the device source before ending the interrupt, use W1C writes correctly."
- **⑥ Regress** — "Compare IRQ count with the IMU's sample counter for an hour under CPU load; any mismatch fails."
- **꼬리1** — "Trigger on the IMU edge, measure the delay to the ISR GPIO, and use a pulse-width or timeout trigger: 'IMU edge with no ISR toggle within 500 µs' captures exactly the missed case. Persistence mode gives the latency distribution."
- **꼬리2** — "A slow or ringing edge crosses the threshold twice; check for Schmitt input or enable debounce. Or the line is configured for both edges."
- **꼬리3** — "The wake source must be enabled as a wake interrupt and its domain kept powered; the pin's pad may need retention, and the device might assert before suspend completes, so the edge is consumed. Check the wake-enable path and whether it's level or edge in the always-on domain."
- **한국어 핵심**: 기대 rate(1 kHz) vs /proc/interrupts · 누락 = 펄스 짧음/마스크 중 edge 손실/edge-level 불일치 · 폭주 = level 미해제·극성 반대 · W1C를 RMW로 · ISR GPIO + "edge 후 응답 없음" 트리거 · 두 번 = 느린 에지·both-edge · wake = wake enable·always-on 도메인.
- **보러 갈 곳**: [N07D](2026-11-01_N07D_uart_gpio_irq_debug.html) D5–D8 · [deck §7 #4](2026-10-25_rivian_hm_screen_prep.html)

</details>

## 6. DDR · LPDDR 5

> DDR 답의 무기 = **expected XOR actual**. 어떤 비트 · 어떤 byte lane · 0→1인가 1→0인가 · 주소 비트와 상관있나 · 온도/패턴/시간 의존. 기초·training: [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html), 실패 해석: [N07E](2026-11-01_N07E_ddr_memory_debug.html).

### H11 · Memtest fails only on bit 13

- **Q.** "Here's a memtest log: thousands of failures, different addresses, but expected XOR actual is always 0x00002000. Where's the fault?"
- **꼬리1.** "The flips go both directions, and only at the top data rate."
- **꼬리2.** "Is it the board or the chip?"
- **꼬리3.** "Different log: errors in all eight bits of byte lane 2, random, intermittent."
- **시나리오**: [N07E](2026-11-01_N07E_ddr_memory_debug.html) E1 · E2

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Is the data bus 64 or 32 bits per channel? Does the bit flip 0 to 1, 1 to 0, or both? All ranks and channels or one? All boards?"
- **② Layer** — "A single bit across all addresses is one physical DQ path: one pin, one trace, one solder ball, or one bit's timing. Bit 13 sits in byte lane 1, bits 8 to 15, sharing that lane's strobe."
- **③ Counters** — "If ECC is on, the CE syndrome should point to the same bit. Training log: that bit's per-bit deskew and eye width compared with its neighbours."
- **④ Measure + rule out** — "First, map logical bit 13 to a physical pin: boards swizzle DQ bits within a byte, so I need the swizzle table before looking at the layout. If it's always 0 to 1, stuck-at-high: a short or open. If both directions and rate-dependent, it's timing margin on that one bit. Then compare boards: same bit on every board means design or layout; one board means assembly, a cracked joint or a bad DRAM."
- **⑤ Fix** — "Rework or replace for a single board; for a design-wide issue, layout or a training setting for that bit."
- **⑥ Regress** — "Per-bit eye width from training logged on every board in the farm, with a minimum threshold, plus a data-bus walking-ones test."
- **꼬리1** — "Bidirectional and rate-dependent on one bit is a timing problem: that bit's eye is narrow. Shmoo data rate or VREF and look at its eye width versus the others; check the per-bit deskew landed sensibly."
- **꼬리2** — "Move the evidence: same bit on several boards points to design; one board points to that board. On one board, X-ray the balls or heat and press to see if it's mechanical. Swapping a BGA DRAM is a rework, so I compare across boards first."
- **꼬리3** — "Eight bits of one lane together means something shared by the lane: its DQS strobe, data mask, lane VREF, or the gate training of that byte. Look at that byte's DQS training result and margin."
- **한국어 핵심**: XOR 상수 = 한 DQ 경로 · bit 13 = byte lane 1 (8–15) · **swizzle 표로 물리 핀 변환 먼저** · 한 방향 = stuck(단락/개방), 양방향+속도 의존 = 그 비트 타이밍 마진 · 여러 보드 같은 비트 = 설계, 한 보드 = 조립 · lane 전체 = DQS/DM/VREF/gate.
- **보러 갈 곳**: [N07E](2026-11-01_N07E_ddr_memory_debug.html) E1 · E2 (실패 로그 디코더 코드) · [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html)

</details>

### H12 · Upper half of DRAM mirrors the lower half

- **Q.** "The board has 4 GB of LPDDR. Linux boots and runs, but a test shows that writing to address 0x8000_0000 plus 2 GB changes the data at 0x8000_0000. Debug it."
- **꼬리1.** "Why didn't the normal memtest catch it?"
- **꼬리2.** "How exactly does an address test find the bad bit?"
- **꼬리3.** "How do you tell a configuration error from a broken address line?"
- **시나리오**: [N07E](2026-11-01_N07E_ddr_memory_debug.html) E3

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Exactly 2 GB apart? Every board? Which memory configuration: ranks, density, channels?"
- **② Layer** — "Aliasing at exactly a power of two means one address bit doesn't reach the DRAM: either the controller isn't driving it, by configuration, or the pin is open or stuck. Data bits are fine, since everything reads back."
- **③ Counters** — "No errors at all, which is the point: aliasing is silent. Check the controller configuration against the part number: density, row bits, rank count."
- **④ Measure + rule out** — "Run an address-bit test: write a unique value at base plus each power of two, then check whether base changed. The first power of two that overwrites base names the bit. Map that system address bit to the controller's rank, bank, row or column bit using the address map. If the configuration says that bit exists but the part has fewer rows, or the controller is told one rank when there are two, it's software. If config is right, look at that pin, chip select or the top row address."
- **⑤ Fix** — "Correct the controller configuration, or repair the pin."
- **⑥ Regress** — "Address-bit test across the full physical range, per rank, in the bring-up suite."
- **꼬리1** — "Most memtests check data patterns within the region they allocate; if they never write both aliases, they never see a conflict. Linux working just means it hasn't noticed yet."
- **꼬리2** — "Write pattern A at base, pattern B at base plus 2 to the k for each k, then read base. If base now reads B, bit k is not decoded."
- **꼬리3** — "Compare boards and configurations. Config errors are on every board with that config and go away with the right density setting. A broken line is on one board, or both directions of failure show up at the pin with a scope."
- **한국어 핵심**: 2의 거듭제곱 간격 aliasing = 주소 비트 1개 미도달 · 에러 없음(조용함) · **설정(밀도·row 비트·rank 수) 먼저** · 주소 비트 테스트(base + 2^k) · 주소 맵으로 rank/bank/row/col 변환 · 회귀 = 전 범위 주소 테스트.
- **보러 갈 곳**: [N07E](2026-11-01_N07E_ddr_memory_debug.html) E3 · [V1](hmScreenPrep/problems/V1_march_memtest/index.html)

</details>

### H13 · ECC correctable errors climb at 105 °C

- **Q.** "In the chamber at 105 °C, the DRAM ECC correctable-error count rises steadily. At 25 °C it's zero. No uncorrectable errors yet. Debug it, and tell me whether we can ship."
- **꼬리1.** "The errors are spread across addresses. Versus: thousands per day at one address."
- **꼬리2.** "What does Linux give you to track this?"
- **꼬리3.** "Correctable means corrected. Why care?"
- **시나리오**: [N07E](2026-11-01_N07E_ddr_memory_debug.html) E4 · E7 · deck §7 #3

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Spread or concentrated addresses? One bit or many? Is that 105 °C ambient or DRAM case temperature? All boards?"
- **② Layer** — "Temperature-dependent on DRAM is margin: either retention, cells leak faster at hot and need more refresh, or timing drifts from the values trained at room temperature."
- **③ Counters** — "EDAC counters per memory controller and per rank or DIMM, with the address and syndrome. That gives bit position and address in one go."
- **④ Measure + rule out** — "Check the refresh rate: DRAM needs faster refresh above 85 °C; LPDDR reports its temperature in a mode register the controller should poll to adjust refresh. If refresh is fixed at the room-temperature rate, that's the bug. If refresh is right, check retraining: periodic ZQ calibration and drift tracking. Then shmoo: errors versus temperature and refresh interval; if halving the interval clears it, it's retention."
- **⑤ Fix** — "Temperature-compensated refresh, periodic retraining, or a lower data rate at hot."
- **⑥ Regress** — "Chamber soak at the top of the temperature range with a CE-rate limit, not just zero UE."
- **꼬리1** — "Spread across addresses and bits is margin in the interface or refresh. Concentrated at one address is a hard fault, a weak or stuck cell: the ECC corrects it every read. That's a RMA or page-retirement case, not a tuning case."
- **꼬리2** — "The EDAC sysfs counters, ce_count and ue_count per controller, and rasdaemon, which logs each event with location. Linux can also soft-offline a page so a weak cell isn't used."
- **꼬리3** — "SECDED corrects one bit per word and only detects two. A rising single-bit rate means double-bit events in the same word become likely, and those are uncorrectable. In a safety system, a rising CE rate is a warning to act on before it becomes a UE."
- **한국어 핵심**: 고온 의존 = 마진(retention → refresh, 또는 training drift) · 85 °C 이상 refresh 증가, LPDDR 온도 MR polling · 퍼짐 = 마진, 한 주소 = 하드 셀(page offline/RMA) · EDAC ce_count/ue_count · rasdaemon · SECDED = 1 정정/2 검출 → CE 증가 = UE 전조.
- **보러 갈 곳**: [N07E](2026-11-01_N07E_ddr_memory_debug.html) E4 · E7 · [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html) (ECC) · [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html)

</details>

### H14 · Corruption after suspend and resume

- **Q.** "After suspend-to-RAM and resume, some data in DRAM is corrupted. Without suspend, memtest is clean for days. Debug it."
- **꼬리1.** "How do you tell the three kinds of failure apart from the corruption pattern?"
- **꼬리2.** "It only happens after suspends longer than ten minutes, at hot."
- **꼬리3.** "What's the regression?"
- **시나리오**: [N07E](2026-11-01_N07E_ddr_memory_debug.html) E5

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "How much is corrupted: all of memory, scattered words, or cache-line-sized chunks? Every resume or some? What gets powered off in suspend: controller, PHY, the CPU cluster?"
- **② Layer** — "The DRAM must be in self-refresh before its controller and PHY lose clocks or power, and on resume the PHY needs its training restored. A missing step in that sequence is configuration and firmware, not the DRAM itself."
- **③ Counters** — "ECC events right after resume; the controller's self-refresh status; resume logs showing whether training was restored or rerun."
- **④ Measure + rule out** — "Write a known pattern over all memory, suspend with a short wake timer, resume, verify. Then read the pattern of damage: everything random means self-refresh wasn't entered or the DRAM reset line glitched; scattered single bits on certain lanes means training values weren't restored; cache-line-sized stale chunks means dirty CPU cache lines weren't flushed before suspend. Scope CKE or the reset line during suspend for glitches."
- **⑤ Fix** — "Fix the ordering: flush caches, enter self-refresh, then gate; on exit restore training or retrain before traffic."
- **⑥ Regress** — "A thousand-cycle suspend/resume loop with a full-memory CRC check, at temperature corners."
- **꼬리1** — "All memory random: self-refresh or reset. Lane-specific bit errors: training restore. Cache-line chunks with old data: cache flush. Each points at a different owner."
- **꼬리2** — "Long and hot means retention during self-refresh: the DRAM refreshes itself, but at hot it needs the right self-refresh temperature setting. Also the I/O retention or reset line may leak or droop over time."
- **꼬리3** — "rtcwake can loop suspends on Linux; in bare metal, the same sequence driven by a timer. Check CRC every cycle, log temperature and duration."
- **한국어 핵심**: 순서 = 캐시 flush → self-refresh 진입 → 게이트, 복귀 = training 복원 후 트래픽 · 손상 패턴으로 주인 구분: 전체 랜덤 = SR/리셋 글리치, lane 비트 = training 복원, cache line 크기 = flush 누락 · 길고 뜨거운 suspend = SR retention 설정.
- **보러 갈 곳**: [N07E](2026-11-01_N07E_ddr_memory_debug.html) E5 · [BV13](2026-11-01_BV13_board_debug_scenarios.html) S9 · [deck §7 #1 DMA/캐시](2026-10-25_rivian_hm_screen_prep.html)

</details>

### H15 · Training passes but Linux panics weekly

- **Q.** "DDR training passes, a 12-hour memtest passes, yet three of forty boards in the farm get a random kernel panic about once a week, in different places each time. ECC is off on these boards. Debug it."
- **꼬리1.** "How do you turn once a week into once an hour?"
- **꼬리2.** "One panic says 'unable to handle kernel paging request' at an address that's one bit off a valid pointer."
- **꼬리3.** "It only appears when NPU and cameras run together."
- **시나리오**: [N07E](2026-11-01_N07E_ddr_memory_debug.html) E8 · E6 · 비교: [N06I](2026-11-01_N06I_mock_10min_qbank.html) D05

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Always the same three boards? Same workload? Different code paths in each panic? Any temperature correlation?"
- **② Layer** — "Random locations and specific boards point to hardware margin rather than one software bug. Training passing just means the margin is positive at training time, not large."
- **③ Counters** — "Keep every panic: pstore or ramoops, so the trace survives the reboot. Without ECC I have no CE counters, so the crash signatures are my counters."
- **④ Measure + rule out** — "Swap a bad board's workload to a good board and vice versa: does it follow the board? Run a stress tool that hammers memory from all cores with copy and invert patterns, which is harsher than a single-threaded memtest. Then amplify: lower DRAM voltage a step, raise temperature, raise data rate. If the failure rate climbs with each knob, it's margin and I can measure how much."
- **⑤ Fix** — "Board-level: rework or screen. Design-level: training settings, VREF, or a lower data rate. And enable ECC if the SoC supports it."
- **⑥ Regress** — "A multi-core memory stress at reduced margin in the farm, so a board with low margin fails in hours, not in the field."
- **꼬리1** — "Stress plus corners: all-core memory stress, DRAM voltage down, temperature up, refresh interval longer. Measure failure rate per knob setting; a steep curve means you're near the edge."
- **꼬리2** — "XOR the bad pointer with the likely good one: a single-bit difference strongly suggests a memory bit flip, not a software bug, because software bugs rarely flip exactly one bit. Collect several and see if it's the same bit."
- **꼬리3** — "Then it's the system under load: simultaneous switching noise and power droop with high DRAM traffic, or QoS. That's like a corruption-under-concurrency case; I'd scope the DRAM rail during the combined load and repeat the stress with only one engine at a time."
- **한국어 핵심**: 랜덤 위치 + 특정 보드 = HW 마진 · pstore/ramoops로 증거 보존 · 보드 따라가나 swap · 전 코어 stress + 전압↓ 온도↑ 속도↑로 증폭 → 실패율 곡선 · 포인터 XOR 한 비트 = 메모리 비트 플립 · 동시 부하 = SSN/rail droop/QoS.
- **보러 갈 곳**: [N07E](2026-11-01_N07E_ddr_memory_debug.html) E6 · E8 · [BV12 shmoo](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · [BV13](2026-11-01_BV13_board_debug_scenarios.html) S8 · S10

</details>

## 7. Ethernet 5

> Ethernet 답의 틀: **관리 경로(MDIO PHY ID) → 링크(BMSR, T1 master/slave) → MAC↔PHY(RGMII delay) → 카운터(ethtool -S) → loopback 사다리 → 상위 스택**. 면접관이 Marvell Ethernet PHY 출신이라 [확인됨: deck §1] 이 묶음이 가장 깊게 파일 확률이 높다. 기초: [LP00A §4](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html), [BV10 §2.5 · 체인 C](2026-11-01_BV10_board_interface_tests.html), [N07F](2026-11-01_N07F_ethernet_debug.html).

### H16 · Link is up but no packets pass

- **Q.** "Our SoC's MAC connects to a gigabit PHY over RGMII. ethtool says link up, 1000 Mb/s full duplex. But not a single ping gets through. Debug it."
- **꼬리1.** "At 100 Mb/s it works. What does that tell you?"
- **꼬리2.** "How do you tell whether TX or RX is broken?"
- **꼬리3.** "We see FCS errors only in loopback, not zero traffic. Where do you start?"
- **시나리오**: [N07F](2026-11-01_N07F_ethernet_debug.html) F3 · F4 · deck §7 #10

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "New board or new software? Do any frames show up in counters on either side? Does a lower speed work?"
- **② Layer** — "Link up means the PHY and the cable side are fine: MDIO works and the PHYs agreed on a link. So the break is between MAC and PHY, the RGMII interface, or MAC configuration."
- **③ Counters** — "ethtool -S on our side and the partner's counters. RX CRC or alignment errors climbing at the MAC means frames arrive but are garbled: RX timing. Zero RX counters but partner sees bad frames means TX. Nothing anywhere means pinmux or clock."
- **④ Measure + rule out** — "RGMII at gigabit runs a 125 MHz clock with data on both edges, and it needs about 1.5 to 2 ns of delay between clock and data. Exactly one party must add it: the PHY, the MAC, or the PCB. In Linux the phy-mode in the device tree says which: rgmii-id means the PHY adds both delays. If the MAC also adds delay, it's doubled; if nobody does, it's zero. Then the loopback ladder: MAC internal loopback passes, PHY loopback fails, so it's the RGMII hop. Scope TX_CLK against TXD at the PHY to see the actual skew."
- **⑤ Fix** — "Set delays in exactly one place, matching phy-mode, and verify the skew."
- **⑥ Regress** — "Board test runs the loopback ladder at every speed, and a check that phy-mode matches the MAC's delay setting."
- **꼬리1** — "At 100 Mb/s the RGMII clock is 25 MHz, a 40 ns period, so a 2 ns delay error is irrelevant; at 1000 the period is 8 ns. Speed-dependent failure that disappears at 10/100 is the textbook delay mistake."
- **꼬리2** — "Counters on both ends, or tcpdump on the partner. If the partner receives our frames, TX is good. PHY loopback isolates the RX hop."
- **꼬리3** — "Same ladder, walk outward: MAC loopback, PHY digital loopback, PHY external loopback or a plug, real partner. The first loop that shows FCS errors bounds the location. PRBS in the PHY checks the line side alone."
- **한국어 핵심**: link up = PHY/선로 OK → **MAC↔PHY(RGMII)** · ethtool -S 양쪽: RX CRC = RX 타이밍, 상대가 깨진 프레임 = TX · RGMII 1G = 125 MHz DDR, 1.5–2 ns 지연을 **한 곳에서만** · phy-mode rgmii-id = PHY가 양쪽 · 100M에선 됨 = 지연 실수의 교과서 증상 · loopback 사다리.
- **보러 갈 곳**: [N07F](2026-11-01_N07F_ethernet_debug.html) F3 · F4 · [BV10 체인 C](2026-11-01_BV10_board_interface_tests.html) · [V10 FCS](hmScreenPrep/problems/V10_eth_frame_fcs/index.html)

</details>

### H17 · 1000BASE-T1 link never comes up

- **Q.** "Our ECU connects to a camera-side switch over 1000BASE-T1. The link never comes up. Debug it."
- **꼬리1.** "Reading PHY ID over MDIO returns 0xFFFF."
- **꼬리2.** "What's different about T1 compared with office Ethernet?"
- **꼬리3.** "How would you validate this port on every board?"
- **시나리오**: [N07F](2026-11-01_N07F_ethernet_debug.html) F1 · F2 · [BV10 체인 C 꼬리3](2026-11-01_BV10_board_interface_tests.html)

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Can we talk to our PHY over MDIO? Is the partner a known-good device? Which side is configured master? Same speed on both: 100 or 1000?"
- **② Layer** — "Management first, then link configuration, then the physical channel. On T1, the most common no-link cause is configuration, not the cable."
- **③ Counters** — "PHY ID registers 2 and 3 to prove the PHY is alive; then the link status register, read twice because link-down is latched; then the T1-specific status: master/slave resolved, signal quality."
- **④ Measure + rule out** — "1000BASE-T1 normally has no autonegotiation: one end must be master and the other slave, by strap or register. Two masters or two slaves gives no link. ethtool shows the master-slave config and status and can force a role. Then speed: both forced to the same speed. Then the channel: ethtool's cable test reports open or short and distance if the PHY supports it. If all that is right, test against a known-good T1 partner or media converter to split our side from theirs."
- **⑤ Fix** — "Correct the role strap or register, or the speed setting."
- **⑥ Regress** — "Board test brings up the port in both roles at each speed against a reference partner, plus the cable test."
- **꼬리1** — "All ones means nobody answered on MDIO: the line floats high through its pull-up. Check PHY reset is released, its supplies and 25 MHz reference clock, the MDIO address straps, and that MDC is within the Clause 22 limit of 2.5 MHz. Scan all 32 addresses to see if it's at a different one."
- **꼬리2** — "One twisted pair, full duplex on the same pair, PAM3 signaling, fixed master/slave roles, and a signal-quality indicator for monitoring. Many T1 PHYs keep their T1 registers in Clause 45 space. And automotive requirements on link-up time and EMC."
- **꼬리3** — "MDIO ID check, role and speed matrix against a reference partner, cable test, PRBS or a traffic test with a BER target, and signal quality logged across temperature."
- **한국어 핵심**: MDIO PHY ID → BMSR 2번 읽기 → T1 master/slave(둘 다 master = no link) → 속도 강제 일치 → cable test · 0xFFFF = 응답 없음(리셋·전원·25 MHz·strap·MDC ≤ 2.5 MHz·주소 스캔) · ethtool -s … master-slave forced-master/forced-slave · ethtool --cable-test.
- **보러 갈 곳**: [N07F](2026-11-01_N07F_ethernet_debug.html) F1 · F2 · [N06E](2026-11-01_N06E_plans_highspeed_io_sensor.html) E2

</details>

### H18 · Link flaps every few minutes in the vehicle

- **Q.** "On the bench the automotive Ethernet link is solid. In the vehicle it drops for a second every few minutes. On the bench at hot, the signal quality indicator drops from 7 to 4. Debug it."
- **꼬리1.** "How do you tell if the PHY lost the link or the driver reset the MAC?"
- **꼬리2.** "The drops correlate with the HVAC blower turning on."
- **꼬리3.** "What do you leave behind?"
- **시나리오**: [N07F](2026-11-01_N07F_ethernet_debug.html) F5 · F10

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "How long is each drop, and is it periodic? Does it correlate with anything: temperature, vibration, other loads switching? One vehicle or several?"
- **② Layer** — "Bench-solid but vehicle-flaky means the environment: noise coupling, harness and connectors, temperature. But first I must confirm it's really a physical link loss."
- **③ Counters** — "Kernel log 'Link is Down' and 'Link is Up' timestamps; PHY latched link-down bit; the PHY's signal quality index over time; ethtool -S error counters before each drop."
- **④ Measure + rule out** — "Log SQI, temperature and link events once a second in the car. If SQI falls before each drop, it's the channel: harness, connector, EMI. If SQI is fine and the driver logs a transmit timeout before the link goes down, it's software resetting the MAC. On the bench, reproduce with temperature, then with injected noise on the harness, and compare cable and connector variants."
- **⑤ Fix** — "Channel: connector, harness routing, common-mode choke or termination. Software: the stall that triggers the watchdog."
- **⑥ Regress** — "Long soak with SQI logging and link-drop count as a pass criterion, plus an EMC immunity run on the board."
- **꼬리1** — "The order of evidence: a PHY link loss shows the latched link-down bit and SQI degradation first; a driver reset shows a 'transmit queue timed out' message followed by the link going down because the driver reset the PHY. Timestamps settle it."
- **꼬리2** — "Classic coupled noise from a motor load: the blower's switching couples into the harness or the ground. Confirm by turning it on and off on purpose, and on the bench with bulk current injection on the cable. Then shielding, routing, or filtering."
- **꼬리3** — "An SQI log tool in the farm and the vehicle image, an SQI threshold as a test limit, and the noise injection case in the EMC test plan."
- **한국어 핵심**: 벤치 OK / 차량 flap = 환경(노이즈·하네스·온도) · 먼저 진짜 PHY 링크 손실인지: SQI 먼저 하락 = 채널, TX timeout 후 down = 드라이버 리셋 · 1초 로그(SQI·온도·이벤트) · 블로어 상관 = 모터 노이즈 → on/off 재현 · BCI · 회귀 = SQI 한계.
- **보러 갈 곳**: [N07F](2026-11-01_N07F_ethernet_debug.html) F5 · F10 · [BV15 자동차 보드](2026-11-01_BV15_automotive_board_considerations.html)

</details>

### H19 · Ping works but iperf drops and big frames fail

- **Q.** "Ping works. iperf at line rate shows a few percent receive loss. And pings with 1472-byte payload and don't-fragment set fail, while small ones pass. Debug both."
- **꼬리1.** "Where in the stack can a received packet be dropped, and how do you see each?"
- **꼬리2.** "At 64-byte frames throughput is half of line rate."
- **꼬리3.** "What's the regression?"
- **시나리오**: [N07F](2026-11-01_N07F_ethernet_debug.html) F6 · F7 · F8 · deck §7 #14

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "TCP or UDP iperf? What's the MTU on both ends and any switch in between? Are offloads enabled?"
- **② Layer** — "Two separate problems. The loss under load is usually system: buffers, interrupts, CPU. The big-frame failure is configuration: MTU or offloads, unless FCS errors climb with frame length, which is timing margin."
- **③ Counters** — "ethtool -S for NIC-level misses and FIFO overflows, ip -s link for drops, /proc/net/softnet_stat for kernel backlog drops and squeezes, and netstat -s for socket buffer errors."
- **④ Measure + rule out** — "For the drops: find the layer whose counter moves. NIC misses mean the ring is too small or refilled too slowly: ethtool -g and -G to grow it, -c to tune interrupt coalescing. Softnet drops mean CPU budget. Socket errors mean the app is too slow. For big frames: 1472 plus 28 bytes of headers is exactly 1500; if that fails, check MTU on every hop, then disable checksum and segmentation offloads with ethtool -K to see if the hardware is mishandling large frames. If FCS errors scale with length, go back to the RGMII timing."
- **⑤ Fix** — "Ring size, coalescing, IRQ affinity; MTU consistency or an offload fix."
- **⑥ Regress** — "RFC 2544-style throughput and loss at each standard frame size, in the farm."
- **꼬리1** — "NIC: rx missed or FIFO errors in ethtool -S. Driver: no-buffer counters. Kernel: softnet_stat dropped column. Socket: receive buffer errors in netstat -s. Each has its own counter, which is why I look before I tune."
- **꼬리2** — "That's a packets-per-second limit, not bytes. Gigabit at 64-byte frames is about 1.49 million frames per second; per-packet costs, interrupts, descriptor refill, dominate. Coalescing, NAPI budget, multiple queues."
- **꼬리3** — "Throughput and loss at 64, 128, 256, 512, 1024, 1280 and 1518 bytes, and an MTU-boundary ping test, on every build."
- **한국어 핵심**: 부하 손실 = 시스템(링·인터럽트·CPU), 큰 프레임 = MTU/offload (길이 따라 FCS↑면 타이밍) · 계층별 카운터: ethtool -S / ip -s link / softnet_stat / netstat -s · 1472+28 = 1500 · ethtool -g/-G, -c, -K · 64 B = 약 149만 fps → pps 한계.
- **보러 갈 곳**: [N07F](2026-11-01_N07F_ethernet_debug.html) F6–F8 · [L3 RFC 2544](hmScreenPrep/problems/L3_rfc2544_throughput/index.html)

</details>

### H20 · PTP offset has a constant bias

- **Q.** "Two ECUs run PTP over automotive Ethernet. ptp4l reports the slave locked with offsets of a few nanoseconds. But comparing PPS outputs on a scope, the slave is consistently 400 ns behind the master. Debug it."
- **꼬리1.** "The bias changes when we switch the link from 1000 to 100 Mb/s."
- **꼬리2.** "Does 400 ns even matter for camera fusion?"
- **꼬리3.** "How do you check hardware timestamping is actually being used?"
- **시나리오**: [N07F](2026-11-01_N07F_ethernet_debug.html) F9 · [L4 PPS](hmScreenPrep/problems/L4_pps_time_discipline/index.html) · [N06E](2026-11-01_N06E_plans_highspeed_io_sensor.html) E4

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Is the bias constant across reboots and temperature? Is there a switch in between? Where are timestamps taken: MAC or PHY?"
- **② Layer** — "A stable bias with low reported offset is a measurement-model problem, not noise: PTP computes the offset assuming the path delay is the same in both directions. Any asymmetry shows up as a constant error of half the asymmetry, invisible to ptp4l itself."
- **③ Counters** — "ptp4l's offset and path delay; ethtool -T to confirm hardware timestamping; the PHY and MAC datasheet latencies."
- **④ Measure + rule out** — "The PPS comparison on the scope is the ground truth. Sources of asymmetry: timestamps taken in the MAC while the PHY's transmit and receive latencies differ, different latencies in a switch, or a cable delay calibration. So 400 ns of offset means an 800 ns asymmetry. I compute it from the documented PHY latencies; if they account for it, that's the cause."
- **⑤ Fix** — "Configure ingress and egress latency corrections, or the delay-asymmetry parameter, in ptp4l per link speed, or take timestamps closer to the wire."
- **⑥ Regress** — "A PPS comparison rig on the bench with a pass limit, per link speed, and after every PHY firmware change."
- **꼬리1** — "PHY latency depends on speed, so the asymmetry does too. That confirms the PHY latency theory: the correction must be per speed."
- **꼬리2** — "It depends on the requirement. At 30 frames per second, a frame is 33 ms, so 400 ns doesn't matter for frame pairing. It matters for fusing with radar or lidar at microsecond-level and when error budgets add up over several hops. I'd ask what the fusion requirement is and give the budget."
- **꼬리3** — "ethtool -T lists the hardware transmit and receive timestamping capabilities and the PHC index. ptp4l must run with hardware timestamping; software timestamping shows microsecond-level jitter, which is itself a clue."
- **한국어 핵심**: 일정한 bias + 작은 offset = **경로 비대칭** (PTP는 대칭 가정, 오차 = 비대칭/2 → 400 ns = 800 ns 비대칭) · PPS scope 비교가 진실 · PHY TX/RX 지연·스위치 · ptp4l ingressLatency/egressLatency/delayAsymmetry (ns) · 속도별 보정 · 요구사항 기준으로 중요도.
- **보러 갈 곳**: [N07F](2026-11-01_N07F_ethernet_debug.html) F9 · [L4](hmScreenPrep/problems/L4_pps_time_discipline/index.html) · [deck §7 #13](2026-10-25_rivian_hm_screen_prep.html)

</details>

## 8. MIPI CSI-2 카메라 5

> 카메라 답의 틀: **경로를 따라간다 — 센서(I2C 설정·PLL) → SerDes(있으면 lock) → D-PHY(LP→HS, settle) → CSI-2 RX(패킷·ECC·CRC·VC/DT) → ISP → DMA·메모리 → V4L2**. "처음 틀린 단계"를 카운터로 찾는다. 면접관 Baidu 멀티카메라/sync 특허 [확인됨: deck §1]. 기초: [LP00A §5](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html), [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html).

### H21 · Stream started, zero frames

- **Q.** "We bring up a new camera. The sensor answers on I2C with the right chip ID, we start streaming, and zero frames arrive. Debug it."
- **꼬리1.** "The receiver now reports only SoT errors."
- **꼬리2.** "Frames arrive, but the ECC-corrected counter climbs with temperature."
- **꼬리3.** "The camera is behind a serializer and deserializer. What changes?"
- **시나리오**: [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G1 · G2 · G3 · deck §7 #8

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Does stream-on succeed or return an error? Any receiver counters moving at all? Lane count and data rate on both sides? Test pattern or real image?"
- **② Layer** — "I2C works, so power, clock and reset are probably fine. Now I find the first stage that sees nothing: is the sensor transmitting, does the PHY see start-of-transmission, does the receiver see packets, does the packet's data type pass the filter."
- **③ Counters** — "Receiver packet and frame counters plus error status: SoT, SoT sync, ECC, CRC, frame sync. In Linux, media-ctl -p to see that every link is enabled and the format matches on every pad; a mismatch makes stream-on fail with a pipe error."
- **④ Measure + rule out** — "If the receiver counts nothing, scope the clock lane: it should leave the 1.2 V low-power state and switch to a small high-speed swing. No transition means the sensor isn't streaming, often a missing stream-on register or a PLL setting. Transitions but no packets: lane count or clock mode mismatch, continuous versus non-continuous clock, or the receiver enabled after the sensor started. Packets but no frames: the data type or virtual channel filter doesn't match what the sensor sends."
- **⑤ Fix** — "Match lane count, rate, clock mode, data type and VC on both sides; fix start order."
- **⑥ Regress** — "Bring-up test streams the sensor's test pattern and compares to golden, checks frame rate and all error counters zero."
- **꼬리1** — "SoT errors mean the PHY sees the high-speed start but can't find the sync byte cleanly. The usual cause is the receiver's HS-settle time programmed for the wrong data rate. Set it for the actual lane rate, inside the D-PHY window, and check lane skew."
- **꼬리2** — "Correctable header errors that scale with temperature mean signal margin is shrinking: the payload CRC will follow. Check per-lane errors, termination, settle and skew; drop the data rate one step to see if it disappears."
- **꼬리3** — "Two more links to prove first: talk to the deserializer locally, confirm forward link lock, reach the serializer and sensor over the back channel. The deserializer's lock and error counters and its own pattern generator split the problem."
- **한국어 핵심**: "처음 아무것도 안 보이는 단계" 찾기 · RX 카운터·에러 상태 · media-ctl -p 패드 포맷 불일치 = stream-on EPIPE · 클럭 레인 LP(1.2 V) → HS 전환 확인 · 레인 수·continuous clock·시작 순서·DT/VC 필터 · SoT 에러 = HS-settle · 온도 따라 ECC↑ = 마진 · SerDes = lock 먼저.
- **보러 갈 곳**: [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G1–G3 · [BV10 체인 D](2026-11-01_BV10_board_interface_tests.html) · [V9](hmScreenPrep/problems/V9_csi2_packet_parse/index.html)

</details>

### H22 · Image sheared diagonally or green and pink

- **Q.** "Frames arrive at the right rate, but the image is sheared diagonally. After we fix that, the colors are green and pink. Debug both."
- **꼬리1.** "How do you check without the ISP in the way?"
- **꼬리2.** "What exactly is RAW10 packing?"
- **꼬리3.** "We changed the crop by one pixel and the colors went wrong."
- **시나리오**: [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G4 · G5

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Constant shear or changing? Which format: RAW Bayer or YUV? Any CRC errors?"
- **② Layer** — "Clean data with wrong geometry is configuration: the transport is fine, but sender and receiver disagree about line length or format."
- **③ Counters** — "The receiver reports the word count of each long packet. Compare it with width times bits per pixel over 8. For 1920 RAW10 pixels, that's 2400 bytes."
- **④ Measure + rule out** — "Diagonal shear means every line is offset by a fixed amount from the line above: the line stride used to write or read the buffer doesn't match the real line length. Check sensor width, receiver width, DMA stride and the V4L2 bytesperline. Green and pink is a format order problem: YUV with the bytes swapped, UYVY versus YUYV, or the wrong Bayer order on raw."
- **⑤ Fix** — "Make width, stride and pixel format agree from sensor to application."
- **⑥ Regress** — "Golden comparison of the sensor test pattern, color bars, which catches both geometry and color order."
- **꼬리1** — "Stream the sensor's test pattern, dump the raw buffer, and check bytes directly: the first line's word count, where the second line starts, and whether the color bar values are in the expected positions."
- **꼬리2** — "RAW10 packs four pixels into five bytes: four bytes with the upper 8 bits of each pixel, then one byte with the four pairs of low bits. The CSI-2 data type is 0x2B."
- **꼬리3** — "An odd crop offset shifts the Bayer phase: RGGB becomes GRBG. The format code must follow the crop, or crop by even amounts."
- **한국어 핵심**: 깨끗한 데이터 + 기하 이상 = 설정 · WC = 폭 × bpp/8 (1920 RAW10 = 2400 B) · 대각선 = stride 불일치(bytesperline) · 녹색/분홍 = YUV 바이트 순서(UYVY/YUYV) 또는 Bayer 순서 · RAW10 = 4픽셀 5바이트, DT 0x2B · 홀수 crop = Bayer 위상 변경.
- **보러 갈 곳**: [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G4 · G5 · [V9 패킷 파서](hmScreenPrep/problems/V9_csi2_packet_parse/index.html)

</details>

### H23 · Four cameras drop frames, one camera is fine

- **Q.** "Each camera streams fine alone. With four cameras running together through one deserializer into one CSI-2 receiver, we drop frames. Debug it."
- **꼬리1.** "Do the math: four 8-megapixel RAW12 cameras at 30 fps on one 4-lane port at 2.5 Gbps per lane."
- **꼬리2.** "Frames from camera 1 show up in camera 0's buffer."
- **꼬리3.** "Which counters show where the drop happens?"
- **시나리오**: [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G6 · G7 · deck §7 #8

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Where are drops counted: receiver, ISP, or application? All four cameras equally, or one? Does adding DRAM load make it worse?"
- **② Layer** — "Works alone, fails together means system: shared bandwidth, link capacity, buffers, or virtual-channel configuration."
- **③ Counters** — "Per-VC frame counters and error flags in the receiver, receiver FIFO overflow, V4L2 sequence-number gaps per camera, and buffer queue depth."
- **④ Measure + rule out** — "First the arithmetic: is the aggregated CSI-2 link fast enough with blanking? If not, it's a configuration impossibility. Then where the frame dies: receiver FIFO overflow means downstream latency spikes, NoC or DRAM QoS; V4L2 sequence gaps with clean receiver counters mean the application isn't returning buffers in time. Then VC mapping: four cameras need four distinct virtual channels end to end."
- **⑤ Fix** — "Reduce rate or split across receivers; tune QoS; add buffers; fix VC mapping."
- **⑥ Regress** — "Four-camera soak at worst-case mode with DRAM stress, failing on any sequence gap."
- **꼬리1** — "3840 by 2160 is about 8.3 megapixels; times 12 bits times 30 is about 3 Gbps per camera before blanking, so 12 Gbps for four. A 4-lane port at 2.5 Gbps per lane carries 10 Gbps raw. It can't fit: lower frame rate, use two ports, or more lanes."
- **꼬리2** — "Two streams with the same virtual channel, usually a deserializer mapping left at default, or the receiver's VC filter routing two VCs into one buffer. Read the VC field in the packet headers."
- **꼬리3** — "Receiver per-VC frame start and end counts, FIFO overflow flag, DMA buffer-underrun or no-buffer events, V4L2 sequence gaps. The first counter that disagrees with the sensor's frame count is the location."
- **한국어 핵심**: 혼자 OK / 같이 실패 = 시스템 · **먼저 산수**: 8 MP × 12 bit × 30 ≈ 3 Gbps/대 → 4대 12 Gbps > 4레인 × 2.5 = 10 Gbps ❌ · RX FIFO overflow = 하류 지연(QoS) · sequence gap + RX 깨끗 = 앱 버퍼 반환 지연 · VC 중복 = deser 매핑 · 첫 번째로 어긋나는 카운터가 위치.
- **보러 갈 곳**: [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G6 · G7 · [N06D](2026-11-01_N06D_plans_dma_memory.html) (QoS) · [N06I](2026-11-01_N06I_mock_10min_qbank.html) D05

</details>

### H24 · Configured 30 fps, measured 15 fps

- **Q.** "The sensor is configured for 30 fps, but we measure 15 fps. Occasionally a frame comes out twice as tall as it should. Debug it."
- **꼬리1.** "How do you compute the frame rate the sensor should produce?"
- **꼬리2.** "In a dark room it drops to 15, in a bright room it's 30."
- **꼬리3.** "What's the regression check?"
- **시나리오**: [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G8 · G10

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Measured where: sensor output, receiver, or V4L2 timestamps? Exactly half? Any frame-end errors?"
- **② Layer** — "Exactly half is either the sensor really running at 15, a configuration issue, or the receiver merging or skipping every other frame, a protocol issue."
- **③ Counters** — "Receiver frame-start and frame-end counts, lines per frame, frame-sync errors. If frame starts equal 30 per second but completed frames are 15, frames are being merged or dropped."
- **④ Measure + rule out** — "Measure the sensor's own frame timing: a frame-start or strobe output on a scope, or the interval between frame-start packets. If the sensor emits 15, check its clocking. If it emits 30 but we get 15 with double-height frames, a frame-end packet is missing, so the receiver glues two frames together."
- **⑤ Fix** — "Sensor: PLL, line length, frame length. Receiver: frame-end handling or the error that loses it."
- **⑥ Regress** — "A frame-interval histogram and lines-per-frame check on every captured frame."
- **꼬리1** — "Frame rate equals pixel clock divided by line length in pixel clocks times frame length in lines. A wrong input clock, 24 versus 27 MHz, or doubled frame length gives the wrong rate."
- **꼬리2** — "Auto-exposure: in the dark it extends exposure beyond the frame time, and the sensor stretches the frame. Set a maximum exposure or fixed frame rate mode."
- **꼬리3** — "Every frame: interval within tolerance, line count equal to height, frame-start and frame-end counts equal."
- **한국어 핵심**: 정확히 절반 = 센서가 15로 동작(설정) vs RX가 2개를 합침(FE 누락 → 2배 높이) · FS 수 vs FE/완성 프레임 수 · fps = pclk/(HTS × VTS) · 어두우면 15 = AE가 노출 늘려 프레임 늘림 · 회귀 = 프레임 간격 히스토그램 + 라인 수.
- **보러 갈 곳**: [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G8 · G10

</details>

### H25 · Two cameras are exactly one frame apart

- **Q.** "Two cameras are supposed to be synchronized. Their frame timestamps differ by exactly one frame period, 33 ms. Debug it."
- **꼬리1.** "Now the offset drifts slowly instead of being exactly one frame."
- **꼬리2.** "Which clock should camera timestamps use if we fuse them with lidar?"
- **꼬리3.** "How would you measure true capture skew?"
- **시나리오**: [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G9 · [V12](hmScreenPrep/problems/V12_frame_timestamp_check/index.html) · deck §7 #13

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Are the sensors triggered by a shared frame-sync line or free-running? Where is the timestamp taken: start of exposure, frame start, frame end, or DMA completion? Constant or does it vary?"
- **② Layer** — "Exactly one frame and constant is almost never physics: it's a pairing or bookkeeping error. One pipeline holds an extra frame, or frames are paired by sequence number while one camera dropped or discarded its first frame."
- **③ Counters** — "V4L2 sequence numbers per camera, dropped-frame counts, and the timestamp-source flag, which says whether it marks start of exposure or end of frame."
- **④ Measure + rule out** — "Scope the frame-sync input and each sensor's frame-start or strobe output: if the hardware edges line up, the cameras are synchronized and the bug is in timestamping or pairing. If one camera's ISP path adds a frame of latency, timestamp at the receiver's frame start rather than at buffer completion. If the first-frame discard differs per driver, pairing by sequence is off by one."
- **⑤ Fix** — "Timestamp at the same point for every camera, pair by timestamp not sequence, consistent warm-up handling."
- **⑥ Regress** — "An LED rig both cameras see, checking capture skew per frame against a limit."
- **꼬리1** — "Drift means the cameras run on separate clocks without a shared trigger: their frame rates differ slightly. Wire the frame-sync line or slave one sensor to the other."
- **꼬리2** — "A clock shared across the vehicle network, the PTP-disciplined clock, not just the local monotonic clock, so camera and lidar timestamps are comparable."
- **꼬리3** — "A blinking LED with a known timing seen by both cameras, or a scope on both sensors' strobe outputs. Compare the frame where the LED turns on, and the strobe edges, against the reported timestamps."
- **한국어 핵심**: 정확히 1 프레임 + 일정 = 물리 아님 → **짝짓기/장부 오류**(첫 프레임 버림·파이프라인 1 프레임 지연·sequence로 짝지음) · TSTAMP_SRC(SOE/EOF) 확인 · scope로 FSYNC·strobe 정렬 확인 · 드리프트 = 공유 트리거 없음 · lidar 융합 = PTP 시계.
- **보러 갈 곳**: [N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html) G9 · [V12](hmScreenPrep/problems/V12_frame_timestamp_check/index.html) · [L4](hmScreenPrep/problems/L4_pps_time_discipline/index.html)

</details>

## 9. PCIe 3

> PCIe는 [LP15](2026-10-31_LP15_debug_scenarios.html)에 시나리오 12개가 이미 깊게 있다 — 여기선 **말하기 압축판**만. 틀: 전원·refclk·PERST# → LTSSM/L0 · DLActive → 열거·자원 → 드라이버 → IRQ/DMA → 성능. 증거는 `lspci -vvv` LnkCap/LnkSta, AER 카운터. 기초: [N01](2026-10-31_N01_pcie_lspci_ltssm.html).

### H26 · PCIe endpoint trains Gen3 x2 instead of Gen4 x4

- **Q.** "An accelerator card on our board should train at Gen4 x4. lspci shows Gen3 x2, downgraded. On the reference board it trains Gen4 x4. Debug it."
- **꼬리1.** "Which side is limiting?"
- **꼬리2.** "Only lanes 2 and 3 are missing."
- **꼬리3.** "What goes into the regression?"
- **시나리오**: [LP15](2026-10-31_LP15_debug_scenarios.html) S2 · S3

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Same card on both boards? Every boot or some? Temperature? Any retimer or switch?"
- **② Layer** — "Works on the reference board, so the card and software are mostly fine; speed and width both reduced points to the physical layer on our board."
- **③ Counters** — "lspci LnkCap versus LnkSta on both root port and endpoint, LnkCtl2 target speed, LnkSta2 equalization phases, and AER correctable errors."
- **④ Measure + rule out** — "Rule out software: is the target speed set to Gen4, and the device-tree max link speed? Then width: lanes that fail receiver detection drop out, missing AC-coupling caps, an open, or a lane polarity or ordering issue. Then speed: equalization phases incomplete or high errors means channel loss. Bisect: force Gen1 to see if width recovers; lane margining shows per-lane margin."
- **⑤ Fix** — "Assembly or layout for missing lanes, equalization presets for speed."
- **⑥ Regress** — "A retrain loop, thousands of times, recording speed and width each time, in board test."
- **꼬리1** — "Expected is the lower LnkCap of the two ends. The kernel's bandwidth message names the link that limits it."
- **꼬리2** — "Specific lanes means physical: check those lanes' AC-coupling caps and connectors; receiver detection failing on them."
- **꼬리3** — "Speed and width check every boot, retrain loop, and margining at temperature corners."
- **한국어 핵심**: 레퍼런스 보드 OK = 우리 보드 물리층 · LnkCap vs LnkSta(양쪽), LnkCtl2 target, LnkSta2 EQ · 폭 = 수신기 검출 실패(AC 캡·개방·극성) · 속도 = EQ/채널 손실 · Gen1 강제 bisect · 회귀 = retrain 루프 분포.
- **보러 갈 곳**: [LP15](2026-10-31_LP15_debug_scenarios.html) S2 · S3 · [N01](2026-10-31_N01_pcie_lspci_ltssm.html) · [LP13 도구](2026-10-31_LP13_linux_pcie_tools.html)

</details>

### H27 · AER RxErr bursts only with ASPM L1

- **Q.** "With ASPM L1 enabled, the endpoint logs bursts of correctable Receiver Errors after idle periods. With ASPM disabled, zero errors. Debug it."
- **꼬리1.** "Which side reports the error, and why does it matter?"
- **꼬리2.** "Why not just ship with ASPM off?"
- **꼬리3.** "Replay timer timeouts appear too. What layer is that?"
- **시나리오**: [LP15](2026-10-31_LP15_debug_scenarios.html) S7 · S8

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "L1 or L1 substates? Errors right after idle, at the first transaction? Every board?"
- **② Layer** — "Errors only around low-power exit is timing of the exit: the receiver's clock recovery and the reference clock coming back."
- **③ Counters** — "AER correctable counters in sysfs, RxErr versus BadTLP or BadDLLP, and the ASPM state in LnkCtl."
- **④ Measure + rule out** — "Correlate error bursts with L1 exit. If the reference clock is gated through CLKREQ# in L1 substates, check refclk is stable before data, and that the advertised exit latencies and power-on timing are correct. Disable only L1 substates first, keeping L1: if errors stop, it's the substate exit."
- **⑤ Fix** — "Correct exit latency or power-on timing values, refclk control, or a known erratum workaround."
- **⑥ Regress** — "An idle-burst traffic pattern with ASPM on, counting correctable errors."
- **꼬리1** — "The receiver reports. Endpoint reports means the downstream direction is bad; root port means upstream. It tells me which transmitter and channel to look at."
- **꼬리2** — "Power. In a car, standby and idle power matter. Disabling it is a workaround with a cost, not a root cause."
- **꼬리3** — "Data Link Layer: the transmitter didn't get an ACK in time and replayed. Often a symptom of receiver errors at the other end."
- **한국어 핵심**: L1 탈출 시점에만 = 탈출 타이밍(CDR 재잠금·refclk 복귀) · L1 substate만 끄기로 bisect · CLKREQ#·exit latency · 보고한 쪽 = 수신기 → 방향 · ASPM off = 전력 비용이 있는 workaround · Replay timeout = DLL.
- **보러 갈 곳**: [LP15](2026-10-31_LP15_debug_scenarios.html) S7 · S8 · [BV16](2026-11-01_BV16_mock_interview_qbank.html) Q24

</details>

### H28 · Bare-metal config read hangs the core

- **Q.** "In our bare-metal validation firmware, the first config read of the downstream device hangs the core. The debugger shows the CPU stuck on a load. Debug it."
- **꼬리1.** "After a suspend cycle the device returns 0xFFFF for vendor ID."
- **꼬리2.** "Why does a load hang the CPU instead of returning an error?"
- **꼬리3.** "How do you write the enumeration so it never hangs?"
- **시나리오**: [LP15](2026-10-31_LP15_debug_scenarios.html) S10 · S9 · [N01](2026-10-31_N01_pcie_lspci_ltssm.html) §6

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Is the link up when we read? Root port registers readable? Which address is the load: config window or BAR?"
- **② Layer** — "A config read is a memory load that the controller turns into a TLP. If the link is down or the address window isn't set, it may never get a completion."
- **③ Counters** — "Controller link-up bit and LTSSM state, the outbound window setup, and the interconnect error registers."
- **④ Measure + rule out** — "With JTAG, read the PC and the faulting load address: is it inside the config window? Then read the controller's link-up bit and LTSSM state without touching downstream space. If the link isn't up, that's the answer: we accessed too early, so it's a sequencing bug in my firmware. If the link is up, check the outbound address-translation window for config covers that address and size. If reading the controller's own registers already hangs, its clock or reset was never released. Last, scope PERST# and refclk to confirm the endpoint was out of reset long enough."
- **⑤ Fix** — "Guard downstream accesses on link-up, set up the window, configure a completion timeout."
- **⑥ Regress** — "Enumeration test with the endpoint absent, held in reset, and present."
- **꼬리1** — "All-ones means no response: the link is down or the device is in D3cold. Check link state and PERST# on resume."
- **꼬리2** — "Some interconnects wait for the response indefinitely unless a timeout is configured. Others return all-ones or an abort."
- **꼬리3** — "Check link-up first, read vendor ID with a timeout, treat all-ones as absent, and handle the configuration-retry status while the device is still initializing."
- **한국어 핵심**: config read = CPU load → TLP · link up 확인 전 다운스트림 접근 금지 · outbound 창·컨트롤러 클럭/리셋 · 0xFFFF = 무응답(D3cold·링크 down) · completion timeout · CRS 처리.
- **보러 갈 곳**: [LP15](2026-10-31_LP15_debug_scenarios.html) S9 · S10 · [N01](2026-10-31_N01_pcie_lspci_ltssm.html) §6 · [deck §7 #11](2026-10-25_rivian_hm_screen_prep.html)

</details>

## 10. 혼합 · 시스템 2

> 혼합 프롬프트는 **"공통 원인 찾기"** 와 **"처음 보는 것"** — 인터페이스 지식보다 틀(N07A)과 통계가 점수.

### H29 · Several interfaces go flaky together on a new board rev

- **Q.** "On board rev C, I2C NACKs, Ethernet CRC errors and camera ECC errors all started at once, intermittently. Rev B was clean. Same SoC, same software. Debug it."
- **꼬리1.** "The errors line up in time with a motor PWM turning on."
- **꼬리2.** "The power rail looks clean on the scope. Are you done?"
- **꼬리3.** "How do you present this to the hardware team?"
- **시나리오**: [BV13](2026-11-01_BV13_board_debug_scenarios.html) S10 · S12 · [N07A](2026-11-01_N07A_debug_framework_instruments.html) · [N06I](2026-11-01_N06I_mock_10min_qbank.html) D06

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "What changed between rev B and C: layout, BOM, PMIC, clock source? All rev C boards? Do the three errors happen at the same moments?"
- **② Layer** — "Three unrelated interfaces failing together means a shared cause: a power rail, ground, a reference clock, reset, or temperature. Debugging each interface separately would waste time."
- **③ Counters** — "Put all three error counters on one timeline, timestamped from the same clock, together with temperature and load. If the errors bunch at the same instants, it's a common cause; if they're independent, it's three bugs and I triage separately."
- **④ Measure + rule out** — "Diff rev B against rev C: BOM changes, PMIC settings, layout near shared rails, the reference oscillator, grounding. Then instrument the shared resources with a trigger on the moment of error: a GPIO toggled when any counter increments triggers the scope on the I/O rail, the core rail and the reference clock. If a rail dips at each error, that's the cause; if rails are clean, move to the clock and then to ground. The proof is a swap: put the rev B part or setting into a rev C board and the errors go away, put it back and they return."
- **⑤ Fix** — "Whatever the rev C change was: decoupling removed in a cost-down, a regulator's compensation or mode, a cheaper oscillator, a ground return cut by a new routing."
- **⑥ Regress** — "A multi-interface stress test with time-aligned error logging on every board rev, and a rev-to-rev checklist item for shared rails and clocks."
- **꼬리1** — "Then it's coupled noise from the motor drive. I turn it on and off on purpose, scope the rails and ground at the SoC with the motor running, and compare rev B and C routing near the motor connector and return path. The correlation plus a reproducible on-off is the evidence."
- **꼬리2** — "Not yet. I check the measurement: a long ground lead adds ringing and hides fast droop, a 20 MHz bandwidth limit hides fast transients, and measuring at the regulator instead of at the SoC hides the drop across the plane. Short spring ground, measure at the load, trigger on the error moment, not on free-run."
- **꼬리3** — "One slide: the time-aligned error plot, the scope capture at the error moment, the rev B to C diff, and the swap that turns the failure on and off. That's evidence they can act on, not an opinion."
- **한국어 핵심**: 서로 무관한 인터페이스 동시 실패 = **공통 원인**(rail·ground·refclk·reset·온도) · 한 타임라인 로그 · rev B↔C diff · 에러 GPIO 트리거로 rail 캡처 · 측정 셋업 확인 · on/off 되는 swap = 증거.
- **보러 갈 곳**: [BV13](2026-11-01_BV13_board_debug_scenarios.html) S10 · S12 · [BV12 SI/PI](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · [BV11 장비](2026-11-01_BV11_lab_instruments_measurement.html)

</details>

### H30 · An interface you have never seen fails 1 in 10000

- **Q.** "Here's an interface you've never used, a vendor's proprietary sensor link. It fails about one transaction in ten thousand. Debug it."
- **꼬리1.** "The protocol analyzer decodes everything fine, but the device still fails."
- **꼬리2.** "Which instrument do you pick first?"
- **꼬리3.** "You think you fixed it. How many runs do you need?"
- **시나리오**: [N07A](2026-11-01_N07A_debug_framework_instruments.html) · [N06H](2026-11-01_N06H_debug_methodology_triage.html) · [BV13](2026-11-01_BV13_board_debug_scenarios.html) §4.3

<details><summary>답 뼈대 펼치기 (60–90초)</summary>

- **① Clarify** — "Is there a spec or at least a datasheet and a reference design? What does a failure look like to software: an error code, bad data, a timeout? Since when, how many boards, which conditions? Is there a board where it never fails?"
- **② Layer** — "I spend the first hour reading: electrical levels, clocking, framing, error detection, and what the link reports. Then I capture a known-good baseline, waveforms and counters on a good board, because I can't recognize abnormal if I don't know normal. Then bottom-up: electrical, timing, protocol, configuration, system."
- **③ Counters** — "Whatever the link exposes: CRC or parity counters, retry counts, status bits, the driver's error codes. They're free and they tell me which layer is complaining."
- **④ Measure + rule out** — "One in ten thousand is too rare to stare at, so first I amplify: speed up, lower voltage, raise temperature, add traffic, until the rate goes up. The knob that moves the rate names the layer: speed or voltage means margin, traffic means system. Then bisect one knob at a time against a known-good partner. To capture it, trigger on the failure, a GPIO from software on the error, with segmented memory and pre-trigger history, so I see the bad transaction, not a random one."
- **⑤ Fix** — "Fix the cause and show the failure follows it: with the fix the amplified rate drops to zero; without it, it comes back."
- **⑥ Regress** — "The amplified stress condition goes into the nightly farm, with failure counts, not just pass/fail, so a slow margin loss shows up as a trend."
- **꼬리1** — "Decode is a digital interpretation with its own thresholds and sampling; it can call a marginal edge clean. I look at the analog waveform at the receiver pin, not at the transmitter, and check that the analyzer's probe isn't loading the line. If the wire is truly clean, the problem is inside the device: its state machine or its configuration."
- **꼬리2** — "Counters first, because they're free. Then a logic or protocol analyzer to see what was said, then a scope to see how it was said electrically, at the receiver. Pick the scope bandwidth from the edge rate: about 0.35 over the rise time."
- **꼬리3** — "Rule of three: with zero failures in n trials, the rate is below about 3 over n at 95 percent confidence. For one in ten thousand that's 30,000 clean transactions as a minimum; I'd run ten times that at the amplified condition, and revert the fix once to confirm the failure returns."
- **한국어 핵심**: 처음 보는 것 = 스펙 → golden 기준선 → 5계층 bottom-up · 증폭 · 손잡이 하나씩 · 실패 트리거 + pre-trigger · 디코드 OK ≠ 아날로그 OK · rule of three(1/10k → 3만 회 이상, 실전 30만) · 되돌리면 재실패.
- **보러 갈 곳**: [N07A](2026-11-01_N07A_debug_framework_instruments.html) · [N06H](2026-11-01_N06H_debug_methodology_triage.html) · [BV11](2026-11-01_BV11_lab_instruments_measurement.html)

</details>

## 11. 플래시카드 50장

> 사용법: "증상" 칸만 보고 **첫 확인 1개 + 이유**를 5초 안에 소리 내서. 틀린 카드 번호는 §13 기록에 적고 다음 날 그것만. 인터페이스 순: I2C 1–8 · SPI 9–13 · UART/GPIO 14–19 · DDR 20–27 · Ethernet 28–36 · CSI-2 37–44 · PCIe 45–48 · 공통 49–50.

| # | 증상 | 첫 확인 | 이유 |
|---|---|---|---|
| 1 | I2C 장치가 주소에서 NACK | `i2cdetect -y <bus>`로 주소 스캔 + 7-bit vs 8-bit 표기 확인 | 데이터시트의 0x90은 8-bit(=7-bit 0x48)인 경우가 흔하다 · 응답 자체가 없는지 먼저 |
| 2 | SDA가 low로 고정 (SCL은 high) | GPIO로 SCL 최대 9펄스 + STOP, 매 펄스 후 SDA 확인 | slave가 바이트 중간에 멈춘 것 — 최장 점유 = 8 data + 1 ACK |
| 3 | 9-clock 후에도 SDA low | 전원 끄고 DMM으로 SDA–GND 저항 | 프로토콜이 아니라 하드웨어: 단락 또는 무전원 장치의 back-power |
| 4 | 100 kHz OK, 400 kHz 간헐 실패 | scope로 수신단 rise time (30→70%) | Fast-mode tr ≤ 300 ns · tr ≈ 0.85·Rp·Cb |
| 5 | Linux가 -ENXIO 반환 | 주소 단계 ACK 여부 (analyzer) | kernel fault-codes: ENXIO = 주소 단계 NACK |
| 6 | 단일 master인데 -EAGAIN | SDA 글리치/숨은 두 번째 driver (segmented capture) | EAGAIN = arbitration lost = 내가 낸 1을 0으로 되읽음 |
| 7 | 레지스터 읽기 값이 엉뚱한 레지스터 것 | 포인터 쓰기와 읽기 사이에 Sr인지 STOP인지 | STOP+START면 사이에 다른 접근이 끼거나 포인터가 리셋 |
| 8 | SMBus 장치가 가끔 timeout | clock low 지속 시간 vs 25–35 ms | SMBus TTIMEOUT — 그보다 긴 stretching은 SMBus 위반 |
| 9 | SPI 데이터가 한 비트씩 밀림 | 알려진 값(JEDEC ID 9Fh)을 비트로 써서 방향 확인 | ≪1 = 늦게 샘플, ≫1 = 일찍 → CPOL/CPHA 또는 샘플 지연 |
| 10 | 25 MHz OK, 100 MHz 깨짐 | read-capture 샘플 지연 탭 sweep | 왕복 지연(clock out → flash tCO → trace) > 샘플 창 |
| 11 | JEDEC ID가 FF FF FF | CS·SCLK가 실제로 나가는지 + flash 전원/리셋 상태 | MISO가 구동 안 됨 = 선택 안 됨, 전원 미준비, 다른 모드(QPI) |
| 12 | 16 MB 넘으면 앞부분이 보임 | analyzer로 주소 바이트 수 (3 vs 4) | 2^24 = 16 MB — 3-byte 주소 wrap |
| 13 | quad read만 실패 | QE 비트 + dummy cycle 수 | QE가 없으면 IO2/IO3가 WP#/HOLD#로 동작 |
| 14 | 콘솔에 일정한 쓰레기 문자 | scope로 비트 폭 1개 (115200 → 8.68 µs) | 고정 비율 오차 = 입력 클럭 설명 오류, 소폭 = 분주 반올림 |
| 15 | UART 출력 전혀 없음 | pinmux + UART 클럭 gate + TX 핀 토글 여부 | 쓰레기 = baud, 무출력 = 클럭/핀/인스턴스 |
| 16 | 고속 baud에서 바이트 누락 | /proc/tty/driver/serial의 oe vs fe | overrun = SW 지연(FIFO), framing = 비트 타이밍 |
| 17 | 고온에서만 framing error | 고온에서 비트 폭 재측정 | RC 오실레이터 drift — 8N1 허용 합 ≈ 4.6% [계산] |
| 18 | 인터럽트 폭주 | trigger type(level) + 소스 clear 여부 + 극성 | level 소스를 안 지우면 즉시 재진입 |
| 19 | 인터럽트 가끔 누락 | /proc/interrupts 카운트 vs 기대 rate | 펄스가 감지 창보다 짧음, edge/level 불일치, W1C RMW |
| 20 | memtest 실패가 항상 같은 비트 | expected XOR actual → DQ 비트 → **swizzle 표** | 한 비트 = 한 물리 경로 · 논리 비트 ≠ 물리 핀 |
| 21 | 한 byte lane 8비트 전체 랜덤 | 그 lane의 DQS/gate training 결과 | lane 공유 자원 = DQS · DM · VREF |
| 22 | 주소 X와 X+2^k가 같은 데이터 | 컨트롤러 설정(밀도·row 비트·rank 수) vs 부품 | aliasing = 주소 비트 1개 미도달, 설정이 제일 싼 원인 |
| 23 | 고온에서만 ECC CE 증가 | refresh rate가 온도 보정되는지 (85 °C 이상) | 고온 = 셀 누설↑ → refresh 2배 필요 |
| 24 | 같은 주소 CE가 하루 수천 번 | EDAC 이벤트 주소·syndrome 분포 | 한 주소 집중 = 하드 셀 → page offline / RMA |
| 25 | CE가 주소 전반에 퍼짐 | CE 비트 위치 히스토그램 + 온도/속도 상관 | 퍼짐 = 인터페이스/refresh 마진 |
| 26 | suspend/resume 후 데이터 손상 | 손상 패턴 크기 (전체 · lane 비트 · cache line) | 전체 = SR/리셋, 비트 = training 복원, cache line = flush 누락 |
| 27 | training 통과, 주 1회 랜덤 패닉 | 패닉 포인터를 정상 값과 XOR (1비트 차?) | 1비트 차 = 메모리 비트 플립 강력 증거 |
| 28 | MDIO 읽기가 0xFFFF | PHY 리셋 해제 · 전원 · 25 MHz refclk · 주소 strap | all-ones = 아무도 응답 안 함 (MDIO pull-up) |
| 29 | link up, 트래픽 0 | ethtool -S 양쪽 + DT phy-mode vs MAC 지연 설정 | RGMII 1.5–2 ns 지연이 중복(≈4 ns)이거나 누락(0) |
| 30 | 100M OK, 1G 실패 | RGMII delay 설정 | 25 MHz(40 ns) vs 125 MHz(8 ns) — 지연 실수는 1G에서만 |
| 31 | T1 링크가 안 뜸 | `ethtool <if>`의 master-slave cfg/status | T1은 autoneg 대신 역할 고정 — 둘 다 master = 무링크 |
| 32 | BMSR link 비트가 이상하게 0 | BMSR를 두 번 읽기 | link status는 latched-low |
| 33 | 링크 flap | 커널 로그 순서: "transmit queue timed out"이 먼저인가 | 먼저면 드라이버가 리셋한 것, SQI 하락이 먼저면 채널 |
| 34 | ping OK, iperf RX drop | ethtool -S rx missed/fifo → softnet_stat → netstat -s | 어느 층 카운터가 움직이나로 위치 확정 |
| 35 | 1472 B DF ping만 실패 | 경로 전 구간 MTU + ethtool -K offload off | 1472 + 28 = 1500, offload 버그 분리 |
| 36 | PTP 잠금인데 PPS가 일정하게 어긋남 | PHY TX/RX 지연 비대칭 계산 | offset 오차 = 비대칭/2 · ingress/egressLatency 보정 |
| 37 | 카메라 스트림 프레임 0 | RX 카운터 + `media-ctl -p` 패드 포맷 | 어느 단계에서 처음 아무것도 안 보이나 · 포맷 불일치 = EPIPE |
| 38 | SoT 에러만 증가 | RX HS-settle 설정 vs 실제 레인 rate | settle 창은 data rate에 따라 달라짐 |
| 39 | 온도 따라 ECC 정정 증가 | 레인별 에러 + 레인 rate 한 단계 낮추기 | header ECC 정정 증가 = 신호 마진 감소 |
| 40 | 이미지가 대각선으로 찢어짐 | long packet WC vs 폭×bpp/8, DMA stride | stride 불일치 = 줄마다 일정량 밀림 |
| 41 | 색이 녹색/분홍 | 픽셀 포맷 (UYVY vs YUYV, Bayer 순서) | 바이트 순서/위상 오류 |
| 42 | 4대 동시면 drop | 집계 대역폭 산수 + per-VC 카운터 | 8 MP RAW12 30 fps ≈ 3 Gbps/대 → 4대 > 10 Gbps |
| 43 | 30 fps 설정, 15 fps 측정 | FS 수 vs FE 수 + 라인 수/프레임 | FE 누락 = 두 프레임 합침, 아니면 센서 PLL/VTS/AE |
| 44 | 두 카메라 timestamp가 정확히 1 프레임 차이 | sequence 짝짓기 + 첫 프레임 버림 + TSTAMP_SRC | 정확히 1 프레임 = 물리가 아니라 장부 오류 |
| 45 | lspci에 장치 없음 | root port LnkSta (DLActive) + PERST#·refclk | L0/DL_Active 전에는 열거 불가 |
| 46 | Gen4 기대, Gen3 (downgraded) | 양쪽 LnkCap vs LnkSta + LnkSta2 EQ phase | 기대값 = 작은 쪽 LnkCap · EQ 실패면 속도 하락 |
| 47 | AER RxErr 폭증 | aer_dev_correctable 카운터 + 보고한 쪽 | 수신기가 보고 → 어느 방향 신호가 나쁜지 |
| 48 | 첫 config read에서 hang | link-up 비트 확인 후 접근하나 | config read = CPU load, 링크 down이면 completion 없음 |
| 49 | 1/10,000 간헐 실패 | 실패를 증폭하는 손잡이 찾기 (속도·전압·온도·부하) | 재현율이 올라야 bisect 가능 · rule of three로 종료 |
| 50 | 여러 인터페이스가 동시에 불안정 | 모든 에러 카운터를 한 타임라인에 | 동시 발생 = 공통 원인(rail·ground·refclk·reset·온도) |

## 12. 모의 세트 3개

각 세트 **45분 = 인터페이스 디버그 2 + 혼합 1** + 채점. 혼자면 휴대폰 녹음 + 타이머, 파트너(또는 Claude)가 면접관이면 아래 대본을 건넨다.

| 시간 | 할 것 |
|---|---|
| 0:00–0:02 | 녹음 시작 · 종이 1장 (그림: SoC ─ 핀/PHY ─ 보드 ─ 장치) |
| 0:02–0:14 | **인터페이스 1**: 4–5분 답 + 꼬리 3개 (각 45초) + 여유 |
| 0:14–0:26 | **인터페이스 2**: 같은 방식 |
| 0:26–0:34 | **혼합 1**: 5분 + 꼬리 2개 |
| 0:34–0:45 | 1.5배속 재생 · §1 채점 (프롬프트마다 /16) · §13 기록 · 펼쳐 비교 |

| 세트 | 인터페이스 1 | 인터페이스 2 | 혼합 | 노리는 것 | 교체용 (2회차) |
|---|---|---|---|---|---|
| **세트 1 · 기본기** | H01 I2C 400 kHz | H16 link up no traffic | H30 처음 보는 인터페이스 | 6칸 틀 · 계층 순서 · rule of three 숫자 | H05 SPI · H11 DDR · H29 |
| **세트 2 · 데이터 경로** | H21 카메라 프레임 0 | H11 DDR bit 13 | H29 rev C 동시 불안정 | "처음 틀린 단계" 찾기 · 공통 원인 · 산수(H23 꼬리1) | H23 · H13 · H30 |
| **세트 3 · Manjiang 맞춤** | H17 T1 링크 안 뜸 | H25 카메라 1 프레임 차이 | H15 주 1회 패닉 (시스템) | PHY · 센서 sync · 마진 증폭 · Staff 시그널 | H20 PTP bias · H18 flap · H26 PCIe |

- 순서: 세트 1 → 2 → 3, 하루 간격 ([N07 인덱스 §4](2026-11-01_N07_interface_debug_scenarios.html) 6일차 · D-2와 맞춤). 13점 미만이면 **교체용 프롬프트로** 다시 — 같은 질문을 반복하면 외운 답만 는다.
- 60분 버전 (D-1): 세트 3 + [N06I](2026-11-01_N06I_mock_10min_qbank.html) D01 1개 (테스트 플랜 ↔ 디버그 전환 연습).
- **면접관 대본** (파트너용): ① 질문만 읽고 조용히 듣는다. ② Don이 가설을 말하면 하나를 골라 **"That's ruled out — the counters are clean"** 또는 **"The scope shows nothing wrong"** 으로 막는다 (배제 후 다음 층으로 넘어가는 연습). ③ 3분쯤 꼬리1, 이후 꼬리2·3. ④ 숫자를 말하지 않으면 "How many runs?" / "What's the spec number?"를 묻는다. ⑤ 끝나면 §1 표로 같이 채점.

## 13. 연습 기록 표

약어: CL clarify · LY layer · CT counters · MS measure · RO rule-out · ST statistics · RG regression. 점수는 프롬프트당 /16. 카드 칸 = 틀린 플래시카드 번호.

| 날짜 | 세트 · 프롬프트 | 걸린 시간 | 점수 | 빠진 칸 | 틀린 카드 | 다음에 고칠 한 가지 |
|---|---|---|---|---|---|---|
| 11-__ | 예시: 세트 1 · H01 | 4:40 | 11/16 | ST · RG | 4 · 8 | rise time 숫자(300 ns)와 rule of three를 꼭 말하기 |
| 11-__ | | | | | | |
| 11-__ | | | | | | |
| 11-__ | | | | | | |
| 11-__ | | | | | | |
| 11-__ | | | | | | |
| 11-__ | | | | | | |
| 11-__ | | | | | | |
| 11-__ | | | | | | |
| 11-__ | | | | | | |

- 같은 칸이 세 번 빠지면 → [N07A](2026-11-01_N07A_debug_framework_instruments.html) 5계층 표를 종이에 쓰는 것부터. 예상되는 약점 순서 [추정]: ST(통계 숫자) → RG(되돌리면 재실패) → MS(수신단·트리거).
- 같은 프롬프트 2회 연속 13점 이상 = 졸업, 교체용으로.
- Don 경험을 넣을 자리 (숫자는 `[숫자 채우기]`): H04·H30 꼬리 = Apple 무선 통합의 DSO + protocol analyzer root cause 1건 · H11·H15 = SK hynix NAND/PCIe 인터페이스 신뢰성 에러 패턴 · H05·H07 = Solidigm FPGA에서 SPI/I2C 검증 중 잡은 버그 ([N07 인덱스 §6](2026-11-01_N07_interface_debug_scenarios.html)).

## 14. 출처

이 Q 뱅크는 N07 인덱스 §2 시나리오와 기존 노트를 말하기용으로 재배열한 것이다. 외부 수치는 아래에서 확인한 것만 [확인됨], 나머지는 본문에 [추정]/[계산].

1. NXP UM10204 I2C-bus specification and user manual — https://www.nxp.com/docs/en/user-guide/UM10204.pdf (Standard tr ≤ 1000 ns · Fast tr ≤ 300 ns, tLOW ≥ 1.3 µs · Fm+ tr ≤ 120 ns · Cb ≤ 400 pF · tr 기준 0.3–0.7 VDD · Rp(max) = tr/(0.8473·Cb) · Rp(min) = (VDD − VOL)/IOL · spike 50 ns · bus clear 9 clocks; [N06I](2026-11-01_N06I_mock_10min_qbank.html) 출처 1에서 2026-11-01 확인한 값 재사용)
2. Linux kernel — I2C/SMBus fault codes — https://docs.kernel.org/i2c/fault-codes.html (ENXIO = 주소 단계 NACK · EAGAIN = arbitration lost · ETIMEDOUT = clock stretching 과다 등 · EBADMSG = PEC; 2026-11-01 확인)
3. Linux kernel — PHY Abstraction Layer, RGMII delay — https://docs.kernel.org/networking/phy.html (1.5–2 ns clock-to-data 지연 · rgmii/rgmii-id/rxid/txid는 PHY 관점 · PHY가 넣는 것을 선호 · 지연 오류 증상 = FCS 에러, 10/100에서는 사라짐; 2026-11-01 확인)
4. ethtool(8) man page — https://man7.org/linux/man-pages/man8/ethtool.8.html (`-s … master-slave preferred-master/preferred-slave/forced-master/forced-slave` · `--cable-test` · `-S` · `-T` · `-g/-G` · `-c/-C` · `-K`; SQI 표기는 man page에 없음 → 본문 "SQI 7/7" 표기는 [추정]; 2026-11-01 확인)
5. ptp4l(8) man page — https://manpages.debian.org/testing/linuxptp/ptp4l.8.en.html (delayAsymmetry · egressLatency · ingressLatency, 단위 ns; 2026-11-01 확인)
6. Linux kernel — V4L2 buffers — https://docs.kernel.org/userspace-api/media/v4l/buffer.html (V4L2_BUF_FLAG_TSTAMP_SRC_EOF / SOE · sequence는 dropped frame 포함해 증가 → gap = drop; 2026-11-01 확인)
7. Linux kernel — include/media/mipi-csi2.h — https://raw.githubusercontent.com/torvalds/linux/master/include/media/mipi-csi2.h (DT: FS 0x00 · FE 0x01 · LS 0x02 · LE 0x03 · YUV422 8-bit 0x1E · RAW10 0x2B · RAW12 0x2C; 2026-11-01 확인)
8. [N07 인덱스](2026-11-01_N07_interface_debug_scenarios.html) — §2 시나리오 ID(B1–B8 · C1–C7 · D1–D8 · E1–E8 · F1–F10 · G1–G10), §5 겹침 표
9. [LP15 PCIe 디버그 12선](2026-10-31_LP15_debug_scenarios.html), [N01 lspci · LTSSM](2026-10-31_N01_pcie_lspci_ltssm.html) — H26–H28 근거 (LnkCap/LnkSta · LnkSta2 EQ · AER 비트 · config read hang)
10. [BV10 보드 인터페이스 테스트](2026-11-01_BV10_board_interface_tests.html) (체인 B–F), [BV13 보드 디버그 12선](2026-11-01_BV13_board_debug_scenarios.html), [BV09 DDR](2026-11-01_BV09_ddr_bringup_training_memtest.html), [deck §1 · §7](2026-10-25_rivian_hm_screen_prep.html) — 중복 확인 기준 · 면접관 경력
11. [N06I 10분 Q 뱅크](2026-11-01_N06I_mock_10min_qbank.html), [BV16 보드 Q 뱅크](2026-11-01_BV16_mock_interview_qbank.html) — 형식 · 겹치지 않게 고른 기준

- **[계산]/[추정]으로 남긴 것**: UART 8N1 허용 오차 ≈ 4.6% (중앙 샘플 9.5비트 + 1/16 비트 시작 검출 오차로 계산) · 64 B gigabit ≈ 1.49 Mpps (64 + 20 B preamble/IFG 기준 계산) · 8 MP RAW12 30 fps ≈ 3 Gbps (blanking 제외 계산) · D-PHY 레인당 2.5 Gbps(D-PHY v2.x 상한으로 알려진 값, 미확인) · DRAM 85 °C 이상 refresh 2배(DDR4 확장 온도 범위 기준, JEDEC 원문 미확인) · SMBus TTIMEOUT 25–35 ms (SMBus 3.x 사양으로 알려진 값, 원문 미확인) · MDC ≤ 2.5 MHz (IEEE 802.3 Clause 22, 원문 미확인) · Rivian 카메라 수 · 해상도 · SerDes 구성 전부.
