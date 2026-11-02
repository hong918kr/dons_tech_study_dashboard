# N07 · 인터페이스 디버그 시나리오 — "증상 → 원인 → 확인 방법"을 인터페이스별로
<!-- theme: pastel -->

> **왜**: 온사이트 Technical 2(도메인)에서 **"I2C NACKs / camera drops frames / Ethernet link won't come up — debug it"** 류 질문이 나올 확률 높음 [추정 — 면접관 Manjiang 경력: Marvell Ethernet PHY ASIC, Baidu multi-camera/sensor sync, bring-up/post-si validation] · JD 우대사항에 **I2C, UART, SPI, Ethernet, DDR, PCIe, MIPI** 디버그 명시 [확인됨: context] · **형식**: 인덱스(이 페이지) + 하위 페이지 **N07A–N07H 8개** · **재사용**: [최종 훑어보기 §5](2026-10-30_final_skim.html) 한 줄 표를 확장, 이미 있는 [BV10 보드 인터페이스 테스트](2026-11-01_BV10_board_interface_tests.html), [BV13 보드 디버그 12선](2026-11-01_BV13_board_debug_scenarios.html), [BV09 DDR](2026-11-01_BV09_ddr_bringup_training_memtest.html), [LP15 PCIe 디버그](2026-10-31_LP15_debug_scenarios.html), [N06E 고속 I/O 테스트 플랜](2026-11-01_N06E_plans_highspeed_io_sensor.html), [N06H 디버그 방법론](2026-11-01_N06H_debug_methodology_triage.html), [deck §7 16개](2026-10-25_rivian_hm_screen_prep.html) — 여기선 **인터페이스별 증상 카탈로그 + 파형/카운터 읽기 + 새 시나리오**에 집중 · **원본**: [온사이트 플랜 N07](2026-10-31_rivian_onsite_plan.html) · **코드**: onsitePrep/n07/ · **상태**: 11-01 N07A–N07H 전부 완성 (md 7.5k줄, 코드 onsitePrep/n07/a–g 전부 실행·PASS, 링크 0 broken) · **작성 규칙**: onsitePrep/N07_NOTES_SPEC.md

## 0. TL;DR

- **8개 하위 페이지**: A 공통 디버그 틀 · 계측 장비 · 마스터 증상 색인 · B I2C/SMBus · C SPI/QSPI flash · D UART · GPIO · 인터럽트 · E DDR/LPDDR 메모리 · F Ethernet (MAC/PHY · automotive T1) · G MIPI CSI-2 카메라 · H 모의 디버그 드릴 Q 뱅크. **PCIe는 [LP15](2026-10-31_LP15_debug_scenarios.html)에 12선이 이미 있어서** A의 마스터 색인과 H의 드릴에서 링크만.
- **외울 한 줄 (모든 인터페이스 공통)**: **증상을 계층에 놓기(전기 → 타이밍 → 프로토콜 → 설정/드라이버 → 시스템) → 카운터·상태 레지스터 먼저 읽기 → loopback/속도 낮추기/상대 장치 교체로 구간 자르기 → 맞는 장비로 증거(파형·디코드) → 고치고 회귀 테스트 추가**.
- 각 인터페이스 페이지는 같은 틀: 🔰 1분 설명 → **정상 기준선(정상 파형·로그·카운터가 어떻게 생겼나)** → 관찰 지점 지도(핀·레지스터·카운터·Linux 도구·장비) → **증상 → 원인 한 장 표** → 시나리오 드릴 6–10개 → 꼬리 질문 체인 → Don 연결 → 코드 → 함정.
- **★ 먼저 할 것**: **N07A 틀·장비 선택** → **N07F Ethernet** · **N07G 카메라** (면접관 분야) → **N07E DDR 실패 패턴 읽기** → **N07B I2C** → N07H 모의 2회.
- Staff 시그널 3가지: ① **"추측 전에 카운터와 파형"** — 증거로 계층을 먼저 고정 ② **한 번에 손잡이 하나**(속도·레인·전압·온도·상대 장치) ③ 고친 뒤 **같은 고장을 잡는 테스트를 회귀에 추가**하고 보드 팜 전체에 적용.

## 0.5 🔰 처음 보는 사람을 위한 큰 그림 — "인터페이스 디버그"가 뭔가

- 비유: **전화가 안 될 때**. ① 전화선이 꽂혀 있나·신호음이 나나(전기) ② 말이 너무 빨라 못 알아듣나(타이밍) ③ 서로 같은 언어로 말하나·순서를 지키나(프로토콜) ④ 번호를 맞게 눌렀나·설정이 맞나(설정/드라이버) ⑤ 다른 사람이 동시에 쓰나·회선이 붐비나(시스템). 아래 층이 안 되면 위 층은 볼 필요도 없다.

```text
 계층                무엇이 깨지나                       무엇으로 보나
 ─────────────────────────────────────────────────────────────────────────────
 5 시스템            대역폭 경합 · 동시성 · 부하/온도      perf 카운터 · 드롭 카운터 · 장시간 로그
 4 설정/드라이버     주소·모드·pinmux·DT·클럭 설정         레지스터 덤프 · dmesg · i2cdetect/ethtool/v4l2
 3 프로토콜          ACK/NACK · CRC/ECC · 패킷 순서        protocol analyzer · 에러 카운터 · 디코드
 2 타이밍            setup/hold · skew · baud 오차 · eye   scope(측정) · LA 타이밍 · training 로그 · shmoo
 1 전기              전원·pull-up·단락·레벨·임피던스       DMM · scope(아날로그 파형) · 회로도
 ─────────────────────────────────────────────────────────────────────────────
  아래에서 위로 "이 층은 OK"라는 증거를 하나씩 쌓는다 (bottom-up) — 증상이 위층처럼 보여도 원인은 아래층인 경우가 많다
```

- **면접관이 보는 것** [추정]: 바로 "드라이버 버그"라고 단정하지 않고 **계층을 좁히는 순서**, 각 단계에서 **무엇을 측정해 무슨 결과가 나오면 무엇을 배제하는지**, 장비 선택(scope vs logic analyzer vs protocol analyzer), 간헐 실패를 다루는 통계 감각, 그리고 재발 방지.

## 1. 페이지 목록 한눈에

| ID | 페이지 | 대표 질문 | 우선 | 코드 |
|---|---|---|---|---|
| **N07A** | [공통 디버그 틀 · 계측 장비 · 마스터 증상 색인](2026-11-01_N07A_debug_framework_instruments.html) | "How do you approach debugging any interface you've never seen?" | P0 ★ | — |
| **N07B** | [I2C / SMBus 디버그](2026-11-01_N07B_i2c_smbus_debug.html) | "The I2C sensor NACKs. Debug it." | P0 ★ | n07/b |
| **N07C** | [SPI / QSPI flash 디버그](2026-11-01_N07C_spi_qspi_debug.html) | "SPI reads are shifted by one bit / fail above 50 MHz." | P1 | n07/c |
| **N07D** | [UART · GPIO · 인터럽트 디버그](2026-11-01_N07D_uart_gpio_irq_debug.html) | "The console prints garbage / an interrupt is sometimes missed." | P1 | n07/d |
| **N07E** | [DDR / LPDDR 메모리 디버그 — 실패 패턴 읽기](2026-11-01_N07E_ddr_memory_debug.html) | "Memtest reports random bit errors. Where's the fault?" | P0 ★ | n07/e |
| **N07F** | [Ethernet 디버그 (MAC/PHY · RGMII/SGMII · 100/1000BASE-T1)](2026-11-01_N07F_ethernet_debug.html) | "Link is up but no packets pass." | P0 ★ | n07/f |
| **N07G** | [MIPI CSI-2 카메라 파이프라인 디버그](2026-11-01_N07G_mipi_csi2_camera_debug.html) | "The camera stream drops frames and the RX reports ECC errors." | P0 ★ | n07/g |
| **N07H** | [모의 디버그 드릴 — Q 뱅크 · 채점표 · 플래시카드](2026-11-01_N07H_mock_debug_drills.html) | 프롬프트 30개 + 플래시카드 50장 | P1 | — |

## 2. 페이지별 상세 목차

### N07A · 공통 디버그 틀 · 계측 장비 · 마스터 증상 색인 (P0 ★)

- **인터페이스 디버그 5계층 모델**(§0.5 그림 확장): 계층마다 "OK 증거" · 대표 실패 · 측정 도구 표 — 인터페이스 6종(I2C · SPI · UART · DDR · Ethernet · CSI-2, + PCIe 한 줄) × 5계층 매트릭스 1장.
- **N06H 디버그 6단계를 인터페이스에 맞춰 적용**(Reproduce → Localize → Hypothesize → Instrument → Fix → Regress) — 방법론 자체는 [N06H](2026-11-01_N06H_debug_methodology_triage.html) 링크, 여기선 "인터페이스면 각 단계에서 구체적으로 뭘 하나".
- **"구간 자르기" 손잡이 표** (인터페이스별): 속도 낮추기 · 레인/폭 줄이기 · loopback 단계(내부/PHY/외부) · 상대 장치를 known-good으로 교체 · 보드/칩 swap · 전압/온도 · 패턴(PRBS/고정/랜덤) · SW 경로(PIO vs DMA, 폴링 vs 인터럽트).
- **계측 장비 선택표**: DMM · oscilloscope · logic analyzer · protocol analyzer(I2C/SPI/USB/CAN/Ethernet tap) · 내장 카운터/ILA · BERT/패턴 생성기 · TDR/VNA · thermal chamber/stream — "무엇을 묻는가 → 어떤 장비".
- **Scope로 디버그할 때 실전 요령**: 대역폭 ≥ 신호 최고 고조파(상승시간 기준 0.35/tr 규칙) · 프로브 접지 길이(링잉 착시) · 10x vs 1x · 트리거 종류(edge · pulse width · runt · setup/hold violation · serial decode 트리거) · **segmented memory / infinite persistence로 간헐 이벤트 잡기** · 측정할 지점(수신단 가까이).
- **Logic analyzer vs protocol analyzer**: 샘플링 레이트(신호 주파수의 4–10배), 타이밍 모드 vs 상태 모드, 디코드 결과 믿기 전 아날로그 확인.
- **"정상 기준선(golden)" 원칙**: known-good 보드/칩/로그를 먼저 캡처해 두고 diff — 정상이 어떻게 생겼는지 모르면 비정상도 모른다.
- **카운터·상태 레지스터 먼저**: 인터페이스별 "첫 5분 증거 스냅샷" 명령/레지스터 목록 (I2C 상태 · SPI FIFO · UART LSR · DDR ECC/training · ethtool -S · CSI-2 RX 에러 상태) → 각 페이지 §3으로 링크.
- **마스터 증상 색인 표 ★**: 모든 시나리오(N07B–G + 기존 BV13 S1–S12 · LP15 S1–S10/B1–B2 · deck §7 1–16 · N06H N1–N6 · BV10 체인 B–F · BV09 체인 1/6)를 **증상 문장 · 인터페이스 · 계층 · 링크** 한 표로 — "이 증상 어디서 봤지?"를 한 곳에서.
- 꼬리 질문 체인: "Debug an interface you've never used" → "Which instrument first?" → "The analyzer decodes fine but the device still fails" → "It only fails 1 in 10,000 transactions".

### N07B · I2C / SMBus 디버그 (P0 ★)

- 🔰 1분 + **정상 기준선**: START/STOP 파형, 7-bit 주소+R/W, ACK 9번째 클럭, repeated START, 100k/400k/1M 타이밍(tLOW·tHIGH·tr·tSU;DAT) 표 [UM10204 확인], 정상 rise time 모양 vs RC 느린 모양 (ASCII).
- 관찰 지점: SCL/SDA 아날로그(scope) · 디코드(analyzer) · 컨트롤러 상태 레지스터(NACK/arbitration lost/timeout 비트 — 개념명만) · Linux: i2cdetect · i2cget/i2cset/i2ctransfer · dmesg의 timeout/-ENXIO/-EREMOTEIO · bus recovery 로그.
- **증상 → 원인 표** (20행 내외): 주소 NACK · 데이터 중 NACK · SDA low stuck · SCL low stuck(slave stretching 끝없음) · 400k에서만 실패(rise time/pull-up) · 0xFF만 읽힘 · 첫 바이트만 맞음 · 레지스터 포인터 엉뚱(repeated START 미지원/STOP 삽입) · 단일 master인데 arbitration lost(글리치/노이즈/다른 master 존재) · 주소 충돌(같은 주소 장치 2개) · 레벨 시프터 문제 · 전원 꺼진 장치가 버스를 끌어내림(back-power) · 온도/진동에서 간헐 NACK · SMBus timeout(35 ms) · PEC 에러 · clock stretching 미지원 master.
- **시나리오 드릴 8개** (BV13 S11 NACK · BV10 체인 B stuck bus는 요약+링크, 나머지 새로): ① 주소 NACK(요약) ② SDA stuck low after reset(요약+9-clock) ③ 400 kHz에서만 간헐 실패 — rise time 측정·pull-up 계산 ④ 읽으면 레지스터 값이 매번 다른 레지스터 것 — repeated START vs STOP+START ⑤ 단일 master인데 arbitration lost ⑥ 카메라 모듈 전원 off 상태에서 버스 전체 다운 ⑦ 센서가 가끔 clock stretching을 길게 → master timeout ⑧ 고온 chamber에서만 NACK 증가.
- 코드: **I2C 캡처 디코더(C)** — 샘플링된 SCL/SDA 0/1 시퀀스 → START/STOP/주소/R/W/ACK/NACK/데이터/repeated START 디코드 + stuck bus · stretching 길이 · tLOW/tHIGH 위반 플래그. 시나리오 ③④⑤⑦을 재현하는 캡처 4개로 실제 출력 ([V8](hmScreenPrep/problems/V8_i2c_scan_recover/index.html)·BV10 §6.1 복구 시뮬과 다른 것).

### N07C · SPI / QSPI flash 디버그 (P1)

- 🔰 1분 + **정상 기준선**: CPOL/CPHA 4모드 파형 표, CS assert → 첫 엣지 → 샘플 엣지, full-duplex(쓰면서 읽기), QSPI 1-1-4 / 1-4-4 / 4-4-4 단계, flash 명령 예(JEDEC ID 9Fh, read 03h/0Bh, WREN 06h, RDSR 05h — 공개 데이터시트 기준 확인).
- 관찰 지점: SCLK/MOSI/MISO/CS 파형 · 컨트롤러 FIFO/상태 · DMA 완료 · Linux: spidev_test · /dev/mtd · mtd_debug · flashrom · dmesg(spi-nor JEDEC ID).
- **증상 → 원인 표**: 1비트 밀림(모드 불일치) · 첫 바이트 0xFF/0x00(MISO tri-state·CS 타이밍) · 25 MHz OK / 100 MHz 실패(입력 샘플 지연·trace 길이·read delay 보정) · 바이트 사이 CS 해제(장치가 명령 리셋) · 다중 slave 중 하나만 꽂으면 OK(MISO 경합) · 쓰기 안 됨(WREN·WP#·status BP 비트) · erase 후 0xFF가 아님 · quad 모드 안 됨(QE 비트) · DMA는 깨지고 PIO는 OK(캐시·정렬·FIFO threshold) · 워드 엔디안 뒤바뀜 · 4-byte 주소 모드 혼동(16 MB 경계).
- **시나리오 드릴 7개**: ① 데이터 1비트 shift ② JEDEC ID가 0xFFFFFF ③ 100 MHz에서만 깨짐 — 샘플 지연 sweep ④ 16 MB 넘으면 앞 데이터가 보임 ⑤ DMA 경로만 깨짐 ⑥ quad read만 실패 ⑦ 부팅 시 가끔 flash를 못 찾음(전원 시퀀스/리셋 후 대기).
- 코드: **SPI 모드 불일치 시뮬레이터(C)** — master 모드 × slave 모드 16조합에서 받은 바이트 표 + 샘플 지연(반 클럭) 효과 재현, 실제 출력.

### N07D · UART · GPIO · 인터럽트 디버그 (P1)

- 🔰 1분 + **정상 기준선**: 8N1 프레임 파형, 16x oversampling과 중앙 샘플링, baud 오차 허용(프레임 끝 비트에서 누적 오차 계산 — 양쪽 합 대략 ±3–5% 범위 [계산으로 확인]), RTS/CTS, break.
- 관찰 지점: TX/RX 파형(scope로 비트 폭 측정 → 실제 baud) · LSR 류 상태(overrun/parity/framing/break — 16550 계열 공개 문서 기준) · Linux: stty · /proc/tty/driver/* · dmesg · GPIO: gpioinfo/gpioget/gpiomon(libgpiod) · /proc/interrupts · /sys/kernel/irq.
- **증상 → 원인 표 (UART)**: 깨진 글자 · 출력 없음(pinmux·TX/RX 교차·클럭 gate·레벨 3.3 V vs 1.8 V vs RS-232) · 첫 글자만 깨짐 · 고속 baud에서만 글자 빠짐(FIFO overrun·IRQ latency) · 온도에서 framing error(RC 오실레이터) · RTS/CTS 데드락 · 부팅 로그는 되는데 Linux 콘솔 안 됨(console= · DT stdout-path · 클럭 재설정).
- **증상 → 원인 표 (GPIO/인터럽트)**: 인터럽트 누락(edge vs level·clear 순서·W1C) · 인터럽트 폭주(level 소스 미해제·unmask 순서) · 두 번 들어옴(debounce·bounce) · pinmux 충돌 · 출력 레벨 이상(drive strength·open-drain·전압 도메인) · wake 인터럽트가 suspend 중 안 됨.
- **시나리오 드릴 8개**: UART 4(깨진 글자 → 비트 폭 측정 · 115200 OK 3M 실패 · 고온 framing · 콘솔이 Linux 진입 후 사라짐) + GPIO/IRQ 4(가끔 누락 — deck §7 #4 요약 · IRQ storm · 두 번 들어옴 · suspend 중 wake 실패).
- 코드: **baud 오차 계산기 + 샘플링 시뮬레이터(C)** — 입력 클럭·목표 baud·oversampling(16/8) → 분주값·실제 baud·오차 % · 허용 한계 판정, 오차 있는 송신을 수신 측이 몇 번째 비트에서 틀리는지 재현 (실제 출력 표).

### N07E · DDR / LPDDR 메모리 디버그 — 실패 패턴 읽기 (P0 ★)

- [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html)(training·memtest 알고리즘·ECC 개념)와 [BV13 S6](2026-11-01_BV13_board_debug_scenarios.html)(training 실패)는 링크. 여기선 **"실패 로그(주소·기대값·실제값)를 보고 고장 위치를 추론"** 하는 기술.
- 🔰 1분 + **정상 기준선**: training 로그의 정상 margin, ECC CE 카운트 0 또는 매우 낮음, memtester 정상 출력.
- **XOR 분석법**: expected XOR actual → 어떤 DQ 비트 · 어떤 byte lane(DQS 그룹) · 항상 같은 비트인가 · 0→1만/1→0만(stuck-at) · 주소 비트와 상관(address line open/short → aliasing) · bank/row/column 디코드(주소 맵 가정 명시) · 시간/온도/패턴 의존성.
- **패턴 → 원인 표** (20행 내외): 한 DQ 비트 항상 1 · 한 byte lane 전체 랜덤 · 주소 X와 X+2^k가 같은 데이터(주소 라인) · 특정 row 근처만(row hammer/weak cell) · 고온에서만(refresh/tREFI·retention) · self-refresh exit 후만 · DFS 주파수 전환 후만 · CPU 테스트 OK / DMA 동시 부하에서 실패(SI·전원 노이즈·QoS) · 캐시 켜면 사라짐/생김 · ECC CE가 한 주소에서 계속(하드 고장) vs 퍼짐(마진) · UE 후 패닉 · training 통과인데 Linux 랜덤 crash(마진·특정 패턴).
- **시나리오 드릴 8개**: ① memtest가 bit 13만 실패 ② byte lane 2 전체 간헐 ③ 4 GB 중 상위 절반이 하위 절반의 복사본 ④ 105 °C에서만 ECC CE 증가 ⑤ suspend/resume 후 데이터 손상 ⑥ NPU+카메라 동시 부하에서만 비트 에러 ⑦ 같은 주소 CE가 하루 수천 번 ⑧ training은 통과, 1주일에 한 번 커널 패닉.
- 코드: **DDR 실패 로그 디코더(C)** — (주소·기대·실제) 목록 → DQ 비트 히스토그램 · byte lane · stuck-at 방향 · 주소 비트 aliasing 탐지 · 가설 출력; 결함 주입한 mock 로그 5종으로 실제 출력 ([V1](hmScreenPrep/problems/V1_march_memtest/index.html)·BV09 §6.1과 다른 것: 테스트가 아니라 **결과 해석**).

### N07F · Ethernet 디버그 (MAC/PHY · RGMII/SGMII · 100/1000BASE-T1) (P0 ★)

- [LP00A §4](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) 기초, [BV10 §2.5·체인 C](2026-11-01_BV10_board_interface_tests.html)(링크 안 뜸·loopback 사다리), [N06E E2](2026-11-01_N06E_plans_highspeed_io_sensor.html)(테스트 플랜)는 링크. 여기선 **증상별 진단 + 카운터 해석**.
- 🔰 1분 + **정상 기준선**: MAC ↔ (RGMII/SGMII) ↔ PHY ↔ MDI 그림, 정상 MDIO BMSR/PHY ID, ethtool 정상 출력, ethtool -S 카운터가 0인 것, T1은 autoneg 대신 master/slave 설정이 기본 [확인 필요 → writer 검증].
- 관찰 지점: MDIO(Clause 22/45) 레지스터 · PHY 링크/SQI(signal quality, OPEN Alliance) · ethtool / ethtool -S / ethtool -T(timestamp) / ip -s link · mii-tool/phytool · tcpdump · 링크 LED · scope로 RGMII TX_CLK vs 데이터 skew.
- **증상 → 원인 표** (25행 내외): MDIO가 0xFFFF/0x0000(PHY 주소 strap·MDC 클럭·리셋·전원) · 링크 안 뜸(T1 master/slave 둘 다 같은 역할·speed 강제 불일치·케이블/커넥터·PHY 리셋 후 대기) · 링크 up인데 트래픽 0(RGMII 내부 지연 2 ns 중복/누락·MAC 속도 설정 불일치·pinmux) · RX CRC/FCS 에러 증가 · 링크 flap · 64 B에서 throughput 반토막(pps 한계) · RX 드롭(ring 크기·IRQ coalescing·NAPI) · TX timeout watchdog · ping은 되는데 큰 패킷만 실패(MTU·jumbo·checksum offload) · VLAN 태그 패킷만 사라짐 · PTP offset이 일정하게 틀림(비대칭 지연·timestamp 지점) · 고온에서 SQI 하락 · EMI 시험에서 링크 drop.
- **시나리오 드릴 10개**: ① MDIO 0xFFFF ② T1 링크 안 뜸 ③ 링크 up · 트래픽 0 (RGMII delay) ④ FCS 에러 — loopback 사다리 요약(BV10 링크) ⑤ 링크 flap 몇 분마다 ⑥ 64 B throughput 저하(deck §7 #14 요약) ⑦ 1500 B 이상만 실패 ⑧ ping OK, iperf에서 RX drop ⑨ PTP 오프셋 고정 bias ⑩ 고온/EMI에서 SQI 하락 후 drop.
- 코드: **ethtool -S 스냅샷 diff + 진단기(Python)** — 두 스냅샷 → 0이 아닌 증가 카운터 → 계층 분류(PHY/PCS/MAC/드라이버/큐) → 가설 순위; mock 스냅샷 4종으로 실제 출력 (+ [V10 FCS](hmScreenPrep/problems/V10_eth_frame_fcs/index.html)·[V6 PRBS](hmScreenPrep/problems/V6_prbs_crc_checker/index.html)·[L3](hmScreenPrep/problems/L3_rfc2544_throughput/index.html) 링크).

### N07G · MIPI CSI-2 카메라 파이프라인 디버그 (P0 ★)

- [LP00A §5](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) 기초, [N06E E3](2026-11-01_N06E_plans_highspeed_io_sensor.html)(테스트 플랜·에러 종류), [BV10 체인 D](2026-11-01_BV10_board_interface_tests.html), [V9](hmScreenPrep/problems/V9_csi2_packet_parse/index.html)·[V12](hmScreenPrep/problems/V12_frame_timestamp_check/index.html)는 링크. 여기선 **센서 → SerDes(있으면) → D-PHY/C-PHY → CSI-2 RX → ISP → 메모리** 경로를 따라 증상별 진단.
- 🔰 1분 + **정상 기준선**: LP → HS 전환(LP-11 → LP-01 → LP-00 → HS-0 → sync), 레인 수·레인당 Mbps, short packet FS/FE/LS/LE와 long packet(DI·WC·ECC·payload·CRC), 정상 frame/line 카운터, v4l2/media-ctl 정상 출력.
- 관찰 지점: 센서 I2C(스트리밍 레지스터 개념) · 센서 PLL/레인 설정 · RX 에러 상태(SoT/SoT sync/ECC 1-bit/2-bit/CRC/frame sync/data type/VC — 공개 IP 문서 기준 개념명) · 프레임/라인 카운터 · 차량용이면 GMSL/FPD-Link SerDes lock 상태 [추정] · Linux: media-ctl -p · v4l2-ctl --stream-mmap · --list-formats-ext · dmesg · 타임스탬프.
- **증상 → 원인 표** (25행 내외): 프레임 0개(센서 미 스트리밍·레인 수 불일치·클럭 레인 continuous/non-continuous·RX 미 enable) · SoT 에러(HS settle 설정) · ECC 1-bit 정정 많음(신호 margin) · CRC 에러 · 줄이 밀림/찢어짐(WC·line length·data type 불일치) · 색이 이상(Bayer 순서·RAW10 패킹) · 화면 일부만(ROI/crop) · 프레임 drop under load(메모리 대역폭·버퍼 부족) · 다중 카메라 VC 섞임 · FS 있는데 FE 없음 · 프레임 레이트가 절반 · 타임스탬프 jitter/동기 어긋남(FSYNC) · 고온에서 SerDes lock 해제 [추정] · 첫 프레임만 깨짐.
- **시나리오 드릴 10개**: ① 스트림 시작했는데 프레임 0 ② SoT 에러만 증가 ③ ECC 정정 카운트가 온도 따라 증가 ④ 이미지가 대각선으로 찢어짐 ⑤ 색이 녹색/분홍 ⑥ 4대 동시면 drop(deck §7 #8 확장) ⑦ VC 1 카메라 프레임이 VC 0 버퍼로 ⑧ FE 누락으로 다음 프레임 합쳐짐 ⑨ 카메라 간 timestamp 1 프레임 어긋남 ⑩ 30 fps 설정인데 15 fps.
- 코드: **CSI-2 RX 이벤트 로그 분석기(C 또는 Python)** — (시각 · VC · 패킷 종류 · WC · 에러 플래그) 이벤트 열 → FS/FE 짝·라인 수·WC 일관성·프레임 간격·VC별 drop·에러 버스트 판정 → 시나리오 ④⑥⑦⑧⑩ mock 로그로 실제 출력 ([V9 패킷 파서](hmScreenPrep/problems/V9_csi2_packet_parse/index.html)는 패킷 단위 ECC/CRC — 여기선 **스트림 단위 진단**).

### N07H · 모의 디버그 드릴 — Q 뱅크 · 채점표 · 플래시카드 (P1)

- 프롬프트 30개(I2C 4 · SPI 3 · UART/GPIO 3 · DDR 5 · Ethernet 5 · CSI-2 5 · PCIe 3(→ LP15 링크) · 혼합/시스템 2), 각각: English Q → 꼬리 3단 → `<details>` 60–90초 영어 답 뼈대 + 한국어 핵심 + N07B–G/LP15/BV13 링크.
- **채점표**(계층 순서 · clarify · 측정 지점·장비 · 배제 논리 · 간헐 통계 · 고친 뒤 회귀 · 시간).
- **플래시카드 50장**: "증상 한 줄 → 첫 확인 1개 + 이유" (예: "I2C NACK → i2cdetect로 주소부터, 7-bit vs 8-bit 표기").
- 모의 세트 3개(각 45분 = 인터페이스 디버그 2 + 혼합 1) · 연습 기록 표.

## 3. 인터페이스 페이지(B–G)의 공통 틀

| 순서 | 섹션 | 내용 |
|---|---|---|
| 0 | TL;DR | 이 페이지로 답할 수 있는 것, 외울 3개, 30초 영어 답 |
| 1 | 🔰 1분 설명 | 비유 → 정확한 정의 → ASCII 연결 그림 → 용어 표 (기초는 LP00A/BV10 링크) |
| 2 | 정상 기준선 | 정상 파형(ASCII)·정상 타이밍 숫자·정상 로그/카운터 |
| 3 | 관찰 지점 지도 | 핀·레지스터/상태 비트·카운터·Linux 도구·장비, 첫 5분 증거 스냅샷 |
| 4 | 증상 → 원인 한 장 표 | 증상 · 계층 · 의심 원인 · 첫 확인 · 확정 방법 |
| 5 | 시나리오 드릴 | 시나리오마다: 증상 → clarify → 가설 순위 표 → 측정·계측 → root cause → fix·회귀 → 30초 영어 답 → 꼬리 질문 |
| 6 | 꼬리 질문 체인 | 시나리오를 넘는 체인 3–4개 |
| 7 | Don 경험 연결 | 이력 기반 영어 문장, 숫자는 `[숫자 채우기]` |
| 8 | 실습 / 코드 | 실제 실행 출력 |
| 9 | 자주 틀리는 것 | 5–10개 |
| 10 | 출처 | 실제로 본 URL |

## 4. 진행 순서 제안 (D = 온사이트)

| 시점 | 할 것 | 연습 |
|---|---|---|
| 1일차 | N07A 5계층·장비 선택·손잡이 표 | 마스터 색인에서 증상 5개 골라 계층 말하기 |
| 2일차 | N07F Ethernet ①–⑤ | "Link up, no traffic" 5분 말하기 |
| 3일차 | N07G 카메라 ①–⑥ | "Camera drops frames" 5분 말하기 |
| 4일차 | N07E DDR 실패 패턴 + 코드 출력 읽기 | 실패 로그 3개 보고 원인 말하기 |
| 5일차 | N07B I2C · N07C SPI | I2C NACK + SPI 1비트 밀림 |
| 6일차 | N07D UART/GPIO · 나머지 시나리오 훑기 | **N07H 모의 세트 1** |
| D-2 | 플래시카드 50장 · 마스터 색인 | **모의 세트 2** |

## 5. 기존 자료와 겹치는 부분 (중복 피하기)

| 주제 | 이미 있는 곳 | N07에서는 |
|---|---|---|
| 인터페이스 한 줄 디버그 | [최종 훑어보기 §5](2026-10-30_final_skim.html) | 전부 확장 (증상 카탈로그) |
| I2C NACK · stuck bus · 9-clock 복구 | [BV13 S11](2026-11-01_BV13_board_debug_scenarios.html), [BV10 §2.1·체인 B·§6.1](2026-11-01_BV10_board_interface_tests.html), [V8](hmScreenPrep/problems/V8_i2c_scan_recover/index.html) | N07B: 요약+링크, 새 시나리오 6개 + 캡처 디코더 |
| SPI 100 MHz 깨짐 · UART garbage | [BV10 체인 E·F](2026-11-01_BV10_board_interface_tests.html) | N07C·D: 요약+링크, 새 시나리오 |
| DDR training · memtest 알고리즘 · ECC | [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html), [BV13 S6·S7](2026-11-01_BV13_board_debug_scenarios.html), [V1](hmScreenPrep/problems/V1_march_memtest/index.html) | N07E: **실패 결과 해석**(XOR·lane·주소 비트) |
| Ethernet 링크·loopback 사다리·PRBS | [BV10 §2.5·체인 C](2026-11-01_BV10_board_interface_tests.html), [N06E E2](2026-11-01_N06E_plans_highspeed_io_sensor.html), [V6](hmScreenPrep/problems/V6_prbs_crc_checker/index.html), [V10](hmScreenPrep/problems/V10_eth_frame_fcs/index.html), [L3](hmScreenPrep/problems/L3_rfc2544_throughput/index.html) | N07F: 증상별 진단 + ethtool -S 해석 |
| CSI-2 에러 종류·테스트 플랜 | [N06E E3](2026-11-01_N06E_plans_highspeed_io_sensor.html), [BV10 체인 D](2026-11-01_BV10_board_interface_tests.html), [V9](hmScreenPrep/problems/V9_csi2_packet_parse/index.html), [V12](hmScreenPrep/problems/V12_frame_timestamp_check/index.html) | N07G: 경로 따라 증상별 진단 + 스트림 로그 분석 |
| PCIe 디버그 | [LP15](2026-10-31_LP15_debug_scenarios.html), [N01](2026-10-31_N01_pcie_lspci_ltssm.html) | 색인·드릴에서 링크만 |
| 디버그 방법론·간헐 실패 통계 | [N06H](2026-11-01_N06H_debug_methodology_triage.html), [BV13 §2](2026-11-01_BV13_board_debug_scenarios.html) | N07A: 인터페이스에 적용만 |
| deck 시나리오 16개 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) | 마스터 색인에 넣고 해당 페이지에서 확장 |

## 6. Don이 채울 것

- [ ] Apple 무선 통합: DSO·protocol analyzer로 PCIe/I2C/SPMI/RFFE root cause 한 사례 (증상 · 측정 · 결론 · 숫자) → N07A·N07B "Don 연결"의 `[숫자 채우기]`
- [ ] SK hynix: NAND/PCIe 인터페이스 신뢰성에서 본 에러 패턴 1개 → N07E
- [ ] Solidigm FPGA: I2C/SPI/DMA/DRAM 검증 중 잡은 버그 1개 → N07B·N07C·N07E
- [ ] N07H 모의 기록 표
