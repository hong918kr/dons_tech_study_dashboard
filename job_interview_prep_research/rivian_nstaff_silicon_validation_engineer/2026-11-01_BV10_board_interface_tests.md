# BV10 · 보드 인터페이스 테스트 — I2C · SPI · UART · GPIO · Ethernet · PCIe · MIPI를 "연결 → 기능 → 성능" 3단계로
<!-- theme: pastel -->

> **묶음**: D 메모리 · 인터페이스 · **우선**: P1 · **선행 노트**: [BV09 DDR bring-up](2026-11-01_BV09_ddr_bringup_training_memtest.html) · [BV08 부트 흐름](2026-11-01_BV08_boot_flow_bringup_firmware.html) · [LP00A I/O primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · **다음 노트**: [BV11 랩 장비](2026-11-01_BV11_lab_instruments_measurement.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html) · [LP 트랙](2026-10-31_LP00_linux_pcie_track.html) · [LP03 링크 트레이닝](2026-10-31_LP03_link_training_ltssm.html) · [LP13 Linux PCIe 도구](2026-10-31_LP13_linux_pcie_tools.html) · [LP14 검증 방법론](2026-10-31_LP14_validation_methodology.html) · **코드**: [i2c_bus_recovery_sim.c](onsitePrep/bv10/i2c_bus_recovery_sim.c) (macOS ASan/UBSan 빌드·실행 검증)

## 0. TL;DR

- 이 노트로 답할 수 있는 질문: **"How would you validate every interface on a new board?"**, "I2C bus is stuck low — what do you do?", "Ethernet link doesn't come up — how do you isolate MAC vs PHY vs cable?", "How do you validate a camera input without a real scene?"
- **외울 것 ① 3단계 사다리**: 모든 인터페이스는 **(1) 연결 증명(connectivity) → (2) 기능(function) → (3) 성능·에러율·장시간(performance / error rate / long-run)**. 각 단계의 "최소 증거"가 다르다: I2C=주소 ACK + WHO_AM_I, SPI flash=JEDEC ID `0x9F`, UART=loopback, Ethernet=MDIO PHY ID(reg 2·3), PCIe=LnkSta 속도·폭, CSI-2=센서 test pattern 프레임.
- **외울 것 ② I2C 9-clock 복구**: slave가 바이트 중간에 SDA를 잡고 있으면 master가 **SCL을 최대 9번** 토글(데이터 8 bit + ACK 1 bit) → SDA 풀리면 **STOP**. 매 펄스마다 STOP을 시도하는 방식이 안전(Linux `i2c_generic_scl_recovery()`와 같은 아이디어). 9번 후에도 low면 **보드 결함(short) 또는 slave 리셋/전원 사이클**.
- **외울 것 ③ Ethernet loopback 사다리**: MAC 내부 loopback(MAC·DMA만) → PHY loopback(BMCR bit14, MAC↔PHY 인터페이스까지) → 외부 loopback/상대 장비(PHY 아날로그·커넥터·케이블) → PRBS/BER + CRC 카운터 장시간. 자동차는 **100/1000BASE-T1, auto-neg 대신 master/slave 고정**.
- 실습: [i2c_bus_recovery_sim.c](onsitePrep/bv10/i2c_bus_recovery_sim.c) — open-drain 버스 + slave 상태 머신 시뮬레이션. 최악 케이스(ACK + 0x00 읽기)에서 **정확히 9펄스**로 복구, "SDA 풀리면 STOP 한 번" 방식이 **실패하는 케이스**까지 재현 (§6, 실제 출력).
- **30초 영어 답 (headline)**: "I validate each interface bottom-up in three steps. First prove connectivity with the cheapest possible transaction — an I2C address ACK and WHO_AM_I read, a SPI flash JEDEC ID, a UART loopback, an MDIO read of the PHY ID registers, PCIe link status. Then prove function — read/write/erase, loopback with data compare, sensor test patterns compared against golden frames. Then prove it's validated, not just working: max speed, error counters like CRC and BER over hours, temperature and voltage corners, and thousands of reset or link cycles. All of it runs as one automated suite that prints a PASS/FAIL table per board."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — 새 아파트의 설비 점검

- 새 아파트(보드)에 전기(전원)와 시계(클럭)가 들어오고 관리실(CPU)이 일하기 시작했다(BV03–BV08). 창고(DDR)도 정리했다(BV09). 이제 **각 세대로 이어진 배관·전선·인터폰·인터넷·CCTV**를 점검할 차례다.
  - **인터폰(I2C)**: 한 줄 전화선에 여러 세대가 매달려 있고, "301호 계세요?" 하고 **호수(주소)** 로 부른다. 대답(ACK)하면 연결 OK. 누군가 수화기를 안 내려놓으면(SDA를 계속 잡고 있으면) **전 건물 인터폰이 먹통** — 그 세대를 깨워서 끊게 만드는 게 9-clock 복구.
  - **전용 배관(SPI)**: 관리실과 한 세대를 잇는 전용선. "누구세요?"(JEDEC ID `0x9F`) 질문에 정해진 답(제조사 코드)이 오면 OK.
  - **방송 스피커(UART)**: 한 방향씩 말하는 단순한 선. 내 말을 내가 다시 듣게(loopback) 해 보면 선이 산 것을 안다.
  - **인터넷(Ethernet)**: 집 안 공유기(MAC) ↔ 벽의 모뎀(PHY) ↔ 바깥 케이블. 고장이 어디인지 모를 때 **어디까지 신호가 갔다 돌아오나(loopback)** 를 구간별로 잘라 본다.
  - **CCTV(MIPI CSI-2 카메라)**: 실제 풍경 대신 **카메라가 스스로 컬러바 화면(test pattern)** 을 보내게 하면, "렌즈·조명" 문제를 빼고 **선과 수신기만** 검사할 수 있다.
  - **고속도로 진입로(PCIe)**: 차선 수(width)와 제한 속도(speed)가 협상대로 나왔나 표지판(LnkSta)을 본다 → [LP 트랙](2026-10-31_LP00_linux_pcie_track.html).
- 원칙: **"가장 싼 질문으로 연결부터 증명 → 그다음 기능 → 마지막에 속도·에러율·오래 버티기"**. 연결이 증명되지 않았는데 성능 숫자를 보는 건 의미가 없다 (BV00의 "아래 계층부터" 원칙과 같음).

### 1.2 3단계 사다리 그림

```text
            (3) 성능 · 에러율 · 장시간      ← "validated"   최대 속도, BER/CRC 카운터, 온도·전압 코너, 1000회 반복
                    ▲
            (2) 기능 (function)              ← "it works"    R/W/erase + 비교, loopback 데이터 비교, test pattern vs golden
                    ▲
            (1) 연결 증명 (connectivity)     ← "it's there"  주소 ACK, ID 레지스터, link up 비트
                    ▲
    (0) 전제: 전원 rail · 클럭 · 리셋 해제 · 핀 mux(pinmux) · IO 전압 · 풀업  ← BV03/BV04/BV05, 회로도 BV02

 인터페이스      (1) 연결                      (2) 기능                              (3) 성능·에러·장시간
 ─────────────  ────────────────────────────  ────────────────────────────────────  ─────────────────────────────────
 I2C            풀업·전압, 주소 scan ACK       WHO_AM_I, 레지스터 R/W, 클럭 스트레칭  400k/1M 속도, 복구(9 clk), 장시간 폴링
 SPI (flash)    CS·CLK 파형, JEDEC ID 0x9F     read/program/erase + CRC 비교           최고 SCLK, 샘플링 지연 튜닝, 온도
 UART           TX 파형/"hello"                외부 loopback 패턴 비교                 최고 baud, baud 오차, 흐름 제어
 GPIO           출력 토글 scope                GPIO↔GPIO loopback, edge IRQ           IRQ 지연, 글리치, 장시간 토글
 Ethernet       PHY strap, MDIO PHY ID 2·3     link up, MAC/PHY/외부 loopback          PRBS/BER, CRC=0 장시간, link 1000회
 PCIe           LnkSta speed/width, 열거       BAR R/W, DMA, MSI                       AER 카운터, margining, reset 반복
 MIPI CSI-2     센서 chip ID over I2C          test pattern 프레임 vs golden          fps·drop, ECC/CRC 카운터, 24h
 eMMC/USB/CAN   ID/열거/bus 상태               R/W, 전송, 프레임 송수신                HS400 tuning, 처리량, bus-off·에러 카운터
```

### 1.3 "연결 증명" 전에 확인하는 공통 전제 5개 (실패의 절반은 여기)

1. **전원**: 그 장치의 VDD와 IO 전압(VDDIO)이 들어왔나 (BV03). 센서 전원이 별도 enable GPIO로 켜지는 보드가 많다.
2. **리셋/enable**: 장치의 RESET_N·PWDN(power-down) 핀이 해제됐나 (BV05). 카메라 센서·PHY는 거의 다 리셋 GPIO가 있다.
3. **클럭**: PHY는 25 MHz 등 기준 클럭, 카메라 센서는 MCLK(마스터 클럭)이 있어야 응답한다 (BV04).
4. **핀 mux(pinmux)**: SoC 핀 하나가 GPIO/I2C/UART 등 여러 기능 중 하나로 선택되는데, 기본값이 GPIO면 컨트롤러가 아무리 신호를 내도 핀에 안 나간다. Linux에서는 device tree의 `pinctrl` (EL [P4 회로도→DT](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/P/2026-10-18_P4_schematic_to_dt.html)).
5. **전압 레벨·풀업**: 1.8 V SoC 핀 ↔ 3.3 V 장치면 level shifter가 필요. I2C·open-drain 신호는 **풀업 저항이 없으면 절대 high가 안 된다**.

### 1.4 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| I2C | Inter-Integrated Circuit | 2선(SCL 클럭 + SDA 데이터) 공유 버스, 주소로 장치 선택 | 센서·PMIC·EEPROM·카메라 설정 전부 이걸로 함 |
| SCL / SDA | Serial Clock / Serial Data | I2C의 두 선 | 둘 다 open-drain이라 풀업 필수 |
| open-drain | — | "low로 당기기"만 할 수 있고 "high"는 풀업 저항이 만든다 | 여러 장치가 같은 선을 써도 충돌(short)이 안 남 · 대신 하나가 계속 당기면 버스 전체가 멈춤 |
| ACK / NACK | Acknowledge / Not ACK | 9번째 클럭에 수신 측이 SDA를 low로 = "받았다" | 주소 ACK = 그 장치가 버스에 살아 있다는 최소 증거 |
| clock stretching | — | slave가 "잠깐!" 하고 SCL을 low로 잡아 master를 기다리게 함 | master가 이를 지원 안 하면 데이터 깨짐 / 복구 시 SCL 읽기 필요 |
| WHO_AM_I | — | 많은 센서가 가진 "고정 ID 값" 레지스터 (이름은 칩마다 다름) | 주소 ACK보다 강한 증거: 내가 생각한 그 칩이 맞다 |
| SPI | Serial Peripheral Interface | 4선(SCLK, MOSI, MISO, CS) 전용선 버스, 빠름 | 부트 flash(QSPI NOR), ADC, 디스플레이 |
| CPOL / CPHA | Clock Polarity / Phase | SPI 클럭이 쉬는 레벨과 "어느 엣지에서 읽나" → mode 0–3 | 틀리면 1 bit 밀린 값이 나와 "가끔 맞는" 이상한 버그 |
| JEDEC ID (0x9F) | JEDEC standard ID read | SPI NOR flash에 `0x9F` 보내면 제조사 1 B + 종류/용량 2 B 응답 | SPI 연결 증명 표준 질문 |
| UART | Universal Asynchronous Receiver/Transmitter | 클럭선 없이 정한 속도(baud)로 보내는 직렬 통신 | 부트 로그 콘솔, 첫 "hello"(BV06) |
| baud error | — | 실제 생성된 baud와 목표 baud 차이(%) | 분주비가 안 나눠떨어지면 오차 → 수신 깨짐 |
| GPIO | General Purpose I/O | 소프트웨어가 0/1을 쓰고 읽는 범용 핀 | 리셋·enable·인터럽트 선, 테스트 loopback |
| MAC / PHY | Media Access Controller / Physical layer | MAC=SoC 안 디지털 패킷 엔진, PHY=선에 실제 전기 신호를 만드는 칩 | 고장 위치를 둘로 나눠 보는 기본 단위 |
| MDIO / MDC | Management Data I/O / Clock | MAC이 PHY 레지스터를 읽고 쓰는 2선 관리 버스 | PHY ID(reg 2·3) 읽기 = Ethernet 연결 증명 |
| Clause 22 | IEEE 802.3 Clause 22 | PHY 주소 5 bit + 레지스터 5 bit(32개) 기본 MDIO 규격 | BMCR/BMSR/PHYID 위치가 표준이라 어떤 PHY든 같음 |
| BMCR / BMSR | Basic Mode Control / Status Register | reg 0(제어: reset, loopback, 속도) / reg 1(상태: link, AN 완료) | link 디버그의 첫 두 레지스터 |
| autoneg | Auto-Negotiation | 양쪽 PHY가 가능한 속도를 교환하고 공통 최고로 맞춤 | 실패/불일치 시 link 안 뜨거나 duplex mismatch |
| 100/1000BASE-T1 | — | 자동차용 단일 twisted pair Ethernet (100 Mb/s / 1 Gb/s) | Rivian 같은 차량 백본 후보 [추정], master/slave 고정 |
| PRBS / BER | Pseudo-Random Bit Sequence / Bit Error Ratio | 의사난수 패턴을 보내 몇 bit 틀리나 셈 | "작동한다"가 아니라 "몇 %로 작동하나" |
| loopback | — | 보낸 신호를 어딘가에서 되돌려 받아 비교 | 구간을 잘라 고장 위치를 찾는 핵심 도구 |
| CSI-2 | Camera Serial Interface 2 (MIPI) | 카메라 센서 → SoC 영상 전송 규격 (D-PHY/C-PHY 위) | 카메라 많은 자동차 SoC의 주 입력 |
| test pattern | — | 센서가 실제 영상 대신 내보내는 컬러바·고정값 | 렌즈·조명 무관하게 데이터 경로만 검증 |
| ECC / CRC (CSI-2) | Error Correcting Code / Cyclic Redundancy Check | 패킷 헤더 ECC(1 bit 정정), 페이로드 CRC-16 | 카운터가 0이 아니면 SI 마진 부족 신호 |
| SerDes (GMSL/FPD-Link) | Serializer/Deserializer | 카메라의 CSI-2를 동축 1가닥으로 수 m 보내고 다시 복원 | 차량 카메라는 거의 이 구조 [추정: Rivian 구성 미확인] |
| LnkSta | PCIe Link Status register | 협상된 링크 속도·폭 | PCIe 연결 증명 → [N01](2026-10-31_N01_pcie_lspci_ltssm.html) |
| CAN | Controller Area Network | 차량 제어용 2선 차동 버스, 메시지 ID 기반 | 자동차 보드 단골, 에러 카운터·bus-off |

## 2. 핵심 개념 (정확한 버전)

### 2.1 I2C

#### 2.1.1 전기적 기본과 풀업 계산

- SCL·SDA 모두 open-drain + 풀업. 상승 시간은 풀업 R × 버스 커패시턴스 Cb 로 정해진다. I2C 스펙은 상승 시간을 **0.3 VDD → 0.7 VDD** 구간으로 정의 → RC 응답에서 `tr = RC · ln(0.7/0.3) ≈ 0.8473 · Rp · Cb` [1].
- 모드별 최대 상승 시간: Standard 100 kHz **1000 ns**, Fast 400 kHz **300 ns**, Fast-mode Plus 1 MHz **120 ns** [1].
- `Rp(max) = tr(max) / (0.8473 · Cb)`, `Rp(min) = (VDD − VOL(max)) / IOL` (예: 3.3 V, VOL 0.4 V, IOL 3 mA → 약 967 Ω) [1].

| 모드 · Cb | Rp(max) 계산 | 해석 |
|---|---|---|
| Standard · 400 pF | 1000 ns / (0.8473 × 400 pF) ≈ **2.95 kΩ** | 흔한 4.7 kΩ은 버스가 무거우면 너무 큼 |
| Fast · 200 pF | 300 ns / (0.8473 × 200 pF) ≈ **1.77 kΩ** | 2.2 kΩ이면 경계 → scope로 tr 확인 |
| Fast · 400 pF | ≈ **885 Ω** < Rp(min) 967 Ω (3.3 V, 3 mA) | **가능한 저항이 없음** → 버스 분할·버퍼·속도 낮추기 |
| FM+ · 200 pF | 120 ns / (0.8473 × 200 pF) ≈ **708 Ω** | FM+는 IOL 20 mA 허용이라 Rp(min)이 훨씬 낮음 [1] |

- scope로 잴 때 주의: scope 기본 rise time 측정은 **10–90%** → I2C는 **30–70%** 기준으로 바꿔서 재야 한다 [1] (BV11).

#### 2.1.2 프로토콜 한 장 요약

```text
 START: SCL high 동안 SDA ↓      STOP: SCL high 동안 SDA ↑      데이터: SCL high 동안 SDA 고정 (SCL low일 때만 바뀜)

 SCL ‾‾‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_ ... _/‾‾‾‾
 SDA ‾\__<A6><A5><A4><A3><A2><A1><A0><RW><ACK>  ...  __/‾‾
      S   └───── 7-bit 주소 + R/W ─────┘ └ slave가 low (9번째 클럭)   P

 레지스터 읽기 (흔한 형태):
 S | addr+W | ACK | reg | ACK | Sr | addr+R | ACK | data | NACK | P
                                 └ repeated START (STOP 없이 다시 START)
```

- 주소 7 bit → 128개 중 일부 예약(0x00–0x07, 0x78–0x7F) — `i2cdetect` 기본 스캔 범위가 0x08–0x77인 이유 [추정: i2c-tools 기본 범위로 알고 있음, `i2cdetect -a`로 전체].
- **Clock stretching**: slave가 SCL을 low로 잡아 둠. master는 SCL을 release한 뒤 **실제로 high가 됐는지 읽어서** 진행해야 한다. bit-bang master가 SCL을 읽지 않으면 stretching slave와 데이터가 깨진다.

#### 2.1.3 Stuck bus와 9-clock 복구 — 왜 생기고 왜 9인가

- **어떻게 생기나**: master(SoC)가 전송 도중에 리셋·watchdog·디버거 halt로 멈춘다. slave는 전원이 그대로라 **"아직 바이트 중간"** 이라고 믿고, 자기가 보낼 비트가 0이면 SDA를 계속 low로 잡는다. 이제 SCL=high, SDA=low → 아무도 START를 못 만든다 (START는 SDA가 high에서 내려가야 하므로).
- **왜 9 클럭**: slave가 SDA를 잡을 수 있는 최장 구간 = **ACK 1 bit + 데이터 8 bit** (또는 데이터 8 + ACK 슬롯). 클럭을 최대 9번 주면 slave는 어떤 위치에 있었든 바이트를 끝내고 SDA를 놓게 된다 (읽기라면 master가 ACK 슬롯에서 SDA를 high로 둠 = NACK → slave가 전송 종료) [2][3][4].
- **절차 (AN-686 / UM10204 bus clear 요약)** [2][3]:
  1. SCL이 high인지 확인 (SCL이 low로 붙어 있으면 클럭으로 해결 불가 → 하드웨어 리셋/전원 사이클).
  2. SDA가 low면 SCL을 한 번 토글하고 **매번 SDA를 확인**, 최대 9번.
  3. SDA가 high가 되면 **STOP** 생성 (SCL low → SDA low → SCL high → SDA high).
  4. 그래도 low면: slave 리셋 핀 / 전원 스위치로 slave 전원 사이클 (AN-686 Solution 2는 아날로그 스위치로 slave 전원을 끊는 방법) [3].
- **함정 — "SDA 풀리면 STOP 한 번"이 실패하는 경우**: slave가 데이터 비트 '1'을 내보내는 순간 SDA가 잠깐 high로 보일 뿐, slave는 **여전히 바이트 중간**이다. STOP을 만들려고 SCL을 내리는 순간 slave는 다음 비트(0일 수도)를 SDA에 올리고 → master가 SDA를 놓아도 low → STOP 실패. 내 시뮬레이션 S3a가 정확히 이 케이스 (§6).
- **안전한 방식 — 매 펄스가 STOP 시도**: 각 펄스를 `SCL low → SDA low → SCL high → SDA release` 로 만들면 slave가 SDA를 안 잡고 있는 첫 순간에 STOP이 성립. Linux `i2c_generic_scl_recovery()`도 `RECOVERY_CLK_CNT = 9`, SCL high 구간마다 bus free 확인, "SDA를 구동할 수 있으면 항상 STOP을 만들어 추가 펄스가 해가 없게 한다"는 주석, SCL이 low로 붙어 있으면 `"SCL is stuck low, exit recovery"` 로 중단 [5].
- **쓰기 중 복구의 위험**: Linux fault-injection 문서는 `incomplete_write_byte` 상태에서 추가 펄스가 장치에 **임의 데이터 쓰기**를 일으킬 수 있으니 복구는 SDA를 확인하거나 STOP을 추가로 보내야 한다고 경고 [6]. EEPROM·PMIC 레지스터에 쓰레기 값이 들어가는 사고 방지.
- **예방**: 부팅 초기에 무조건 bus recovery 1회 실행(이전 리셋 흔적 제거), 리셋 가능한 slave 선택/리셋 GPIO 배선, I2C 버퍼(일부 버퍼는 stuck bus 자동 복구 기능 [추정]), 트랜잭션 timeout + 복구 + 재시도, SMBus 장치는 **35 ms timeout**(SMBus 스펙의 클럭 low timeout) 활용 [추정: SMBus 2.0 기준 25–35 ms].

### 2.2 SPI

- 4선: SCLK, MOSI(master out), MISO(master in), CS_N(chip select, active low). 전이중(full duplex) — 보내는 동안 받는다. 주소 개념이 없고 CS로 장치 선택.
- **Mode = (CPOL, CPHA)**:

| Mode | CPOL (쉬는 SCLK) | CPHA | 샘플 엣지 | 흔한 장치 |
|---|---|---|---|---|
| 0 | 0 (low) | 0 | 첫 엣지 = rising | 대부분 SPI NOR flash (mode 0/3 지원) |
| 1 | 0 | 1 | falling | 일부 ADC |
| 2 | 1 (high) | 0 | falling | 드묾 |
| 3 | 1 | 1 | rising | flash도 지원 |

- **JEDEC ID**: `0x9F` 명령 → 응답 3 B = 제조사 ID(JEDEC JEP106) + memory type + capacity. 예: Winbond W25Q128JV = `EF 40 18` (EF = Winbond, 0x18 → 2^24 B = 16 MB) [7]. **`FF FF FF`** = MISO가 풀업된 채 아무도 안 구동(CS 불량·전원·핀mux), **`00 00 00`** = MISO가 low로 붙음 또는 mode 불일치.
- **기능 테스트**: sector erase → 읽어서 전부 `0xFF` 확인 → page program(패턴) → read back 비교 → CRC32 비교. status register의 BUSY(WIP) 비트 폴링, write enable(`0x06`) 잊으면 program이 조용히 무시됨.
- **고속에서 깨지는 이유**: SCLK가 빨라지면 SoC → flash → SoC **왕복 지연**(보드 trace + flash 출력 지연)이 클럭 반 주기를 넘어 MISO를 잘못된 엣지에서 샘플링. 컨트롤러의 **RX sample delay / read capture delay** 설정이나 DQS(데이터 스트로브) 지원 octal flash로 해결 → **딜레이 스윕(0..N tap) × 클럭 주파수** 로 통과 창을 찾는 게 미니 shmoo (BV12).

### 2.3 UART

- 프레임: start bit(0) + 데이터 8 + (parity) + stop bit(1). 수신기는 보통 **16× oversampling**으로 start 엣지를 찾고 비트 중앙에서 샘플.
- **baud 오차**: `divisor = f_clk / (16 × baud)`, 정수로 반올림 → 실제 baud = `f_clk / (16 × divisor)`.

| UART 클럭 | 목표 baud | divisor (실수 → 정수) | 실제 baud | 오차 |
|---|---|---|---|---|
| 24 MHz | 115200 | 13.021 → 13 | 115384.6 | **+0.16%** ✅ |
| 25 MHz | 115200 | 13.563 → 14 | 111607.1 | **−3.12%** ❌ (경계/위험) |
| 48 MHz | 3,000,000 | 1.000 → 1 | 3,000,000 | 0.00% ✅ |
| 100 MHz | 921600 | 6.782 → 7 | 892857.1 | **−3.12%** ❌ |

- 위 숫자는 이 노트 작성 중 Python으로 계산 (실제 계산값). 허용 오차는 양쪽 합으로 대략 ±3–5% 수준이지만 실무에선 한쪽 ±2% 이내를 목표로 둔다 [추정: 프레임 길이·oversampling 방식에 따라 다름]. 분수 분주기(fractional divider)가 있으면 오차를 줄일 수 있다.
- **"UART에 깨진 글자"는 클럭 문제의 1순위 증상** — 분주 설정이 맞는데 깨지면 UART 입력 클럭(PLL 미설정, 잘못된 osc) 의심 → BV04.
- 흐름 제어: RTS/CTS (하드웨어) — 고속 + 큰 전송에서 FIFO overrun 방지. 테스트는 수신 측이 일부러 느리게 읽게 해서 overrun 카운터가 0인지.

### 2.4 GPIO

- 출력: 레지스터 쓰기 → 핀 토글 → scope로 레벨(VOH/VOL)·주파수 확인. 입력: 외부에서 구동 → 레지스터 읽기.
- **GPIO↔GPIO loopback**: 테스트 지그나 점퍼로 핀 A(출력)–핀 B(입력)를 연결, walking 1s 패턴으로 쌍마다 비교 → 핀 간 short·open 검출 (boundary scan과 같은 아이디어, BV07).
- **Edge IRQ**: 출력 핀으로 엣지를 만들고 입력 핀의 rising/falling/both 인터럽트 카운트 = 토글 횟수인지. **IRQ 지연** = 출력 토글 → ISR 첫 줄에서 다른 GPIO 토글, scope 2채널로 시간차 측정 (BV11).
- 함정: 입력 핀의 내부 풀업/풀다운 설정, 디바운스 필터, 전압 도메인, open-drain 출력 모드.

### 2.5 Ethernet (기초는 [LP00A §4](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) — 여기선 보드 테스트 관점)

#### 2.5.1 PHY strap과 주소

- PHY는 리셋 해제 순간 일부 핀 레벨(strap)을 읽어 **MDIO 주소(0–31), 모드(RGMII/SGMII), master/slave(T1)** 등을 정한다. strap 핀이 LED·데이터 핀과 겸용이라 외부 회로가 strap 값을 바꿔버리는 게 단골 버그 → BV05 strap 개념과 동일.
- MDIO 주소를 모르면 0–31 전부 스캔해서 PHYID가 `0xFFFF`가 아닌 주소를 찾는다.

#### 2.5.2 Clause 22 레지스터 (Linux `include/uapi/linux/mii.h` 기준 [8])

```text
 reg 0x00 BMCR  bit15 RESET(0x8000)  bit14 LOOPBACK(0x4000)  bit13 SPEED100  bit12 ANENABLE
                bit9 ANRESTART  bit8 FULLDPLX  bit6 SPEED1000
 reg 0x01 BMSR  bit5 ANEGCOMPLETE(0x0020)  bit3 ANEGCAPABLE  bit2 LSTATUS(0x0004, latch-low)
 reg 0x02 PHYSID1  OUI 상위 비트
 reg 0x03 PHYSID2  [15:10] OUI 하위 6 bit · [9:4] model · [3:0] revision
 reg 0x04 ADVERTISE · 0x05 LPA(상대가 광고한 능력) · 0x09 CTRL1000 · 0x0A STAT1000
```

- **PHY ID 예**: TI DP83867(1000BASE-T) = `0x2000A231`, TI DP83TG720S(1000BASE-T1) = `0x2000A284` (Linux 드라이버 정의) [9][10]. PHYSID1=`0x2000`, PHYSID2=`0xA231` → model/rev 필드로 쪼개 읽음.
- **BMSR link bit는 latch-low**: 링크가 한 번이라도 끊기면 0으로 래치 → **두 번 읽어서** 두 번째 값이 현재 상태. 첫 읽기 0은 "그 사이 끊긴 적 있음"의 증거로 쓸 수 있다.
- 100/1000BASE-T1 PHY는 주요 기능이 **Clause 45** (MMD) 레지스터에 있는 경우가 많다; Linux DP83TG720 드라이버도 `genphy_c45_pma_baset1_setup_master_slave()` 같은 C45 BASE-T1 헬퍼를 쓴다 [10].

#### 2.5.3 Loopback 사다리 — 고장 위치 자르기

```text
   SoC                                                      보드                      케이블/상대
 ┌────────────────────────────┐   RGMII/SGMII   ┌──────────────────────┐  MDI    ┌─────────────┐
 │ CPU ─ DMA ─ MAC ──[①]──────┼─────────────────┼─[②]─ PCS ─ PMA ─[③]──┼────────┼──[④]────────┤
 └────────────────────────────┘                 └──────────────────────┘        └─────────────┘
  ① MAC 내부 loopback: MAC·DMA·descriptor만 검증 (PHY·보드 제외)
  ② PHY xMII/digital loopback (BMCR bit14 등): MAC↔PHY 인터페이스 타이밍(RGMII delay)까지
  ③ PHY analog/PMA loopback: PHY 아날로그 앞단까지 [PHY마다 지원 다름]
  ④ 외부 loopback 플러그 / 상대 장비(트래픽 제너레이터): 커넥터·magnetics·케이블 포함
```

- 판단 규칙: ①은 되는데 ②가 안 되면 **MAC↔PHY 구간**(RGMII TX/RX clock delay 설정 — MAC 쪽 delay와 PHY 쪽 delay를 중복/누락, pinmux, IO 전압)이 1순위. ②까지 되는데 ④가 안 되면 PHY 아날로그·MDI 배선·커넥터·케이블·master/slave 설정.
- TI DP83TG720S 예: 데이터시트에 analog / digital / PCS / xMII / reverse-xMII loopback과 내장 데이터 제너레이터(PRBS 또는 MAC 패킷), PRBS checker lock 상태·에러 바이트 카운터가 있다 [11] (레지스터 번호는 PHY마다 다르므로 데이터시트 확인).

#### 2.5.4 PRBS / BER와 통계적 의미

- BER = 틀린 bit 수 / 보낸 bit 수. "에러 0개"로 BER < 목표를 **신뢰도 CL로 주장**하려면 `N ≥ −ln(1 − CL) / BER_target` bit를 보내야 함 → 95% · 1e-12 → **약 3.0e12 bit**.
  - 1 Gb/s면 약 **50분**(2996 s), 100 Mb/s(100BASE-T1)면 약 **8.3시간** — "BER 테스트 몇 분 돌렸다"는 통계적으로 의미 없을 수 있다 (계산값).
- 실무 대안: 짧은 시간엔 **SQI**(Signal Quality Index: T1 PHY가 주는 0–7 같은 신호 품질 지표, Linux `ethtool` link 정보에 SQI 표시 가능 — DP83TG720 드라이버가 `get_sqi`/`get_sqi_max(7)` 제공 [10]) + CRC 카운터, 장시간은 야간 회귀.

#### 2.5.5 자동차 Ethernet 특이사항

- **100BASE-T1(802.3bw) / 1000BASE-T1(802.3bp)**: 단일 페어, auto-neg 대신 **master/slave 고정** — 양쪽 다 master면 link 안 뜸 ([LP00A §4.7](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html)). Linux: `ethtool -s eth0 master-slave forced-master|forced-slave` [12].
- 케이블 진단: `ethtool --cable-test eth0` (TDR 기반, PHY 드라이버가 지원해야 함 — DP83TG720은 cable_test 훅 있음) [10][12].
- link-up 시간이 요구사항 (시동 후 카메라·제어 빨리 살아야) → link up 시간 측정 반복 [추정: OEM 요구 수치 미확인].
- TSN/gPTP 하드웨어 타임스탬프 → `ethtool -T` [12], 면접관의 "sensor timestamp/sync" 배경과 연결.

### 2.6 PCIe (요약 — 상세는 LP 트랙)

- 연결 증명: 링크 파트너 양쪽 LnkSta의 **current speed/width = min(LnkCap 양쪽)**, 다운스트림 포트의 DLActive, 열거(Vendor ID ≠ 0xFFFF) → [N01](2026-10-31_N01_pcie_lspci_ltssm.html), [LP03](2026-10-31_LP03_link_training_ltssm.html).
- 기능: BAR MMIO R/W, DMA 데이터 비교, MSI/MSI-X → [LP07](2026-10-31_LP07_interrupts_intx_msi_msix.html), [LP08](2026-10-31_LP08_dma_iommu_coherency.html).
- 성능·에러: AER correctable 카운터, lane margining, link 재학습/리셋 1000회, 온도 → [LP09](2026-10-31_LP09_aer_dpc_error_handling.html), [LP14](2026-10-31_LP14_validation_methodology.html), [LP15](2026-10-31_LP15_debug_scenarios.html).

### 2.7 MIPI CSI-2 (기초는 [LP00A §5](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html))

- **제어 경로 먼저**: 센서는 I2C(MIPI에서는 CCI: Camera Control Interface)로 설정. 센서 전원 rail(보통 아날로그/디지털/IO 3개) → MCLK → RESET 해제 → chip ID 레지스터 읽기 = 연결 증명.
- **데이터 경로 기능**: 센서 **test pattern**(color bar, solid, ramp) 켜기 → SoC CSI-2 RX → DDR 프레임 → golden과 픽셀 비교 (bit depth·unpack 확인). Linux V4L2에는 표준 컨트롤 `V4L2_CID_TEST_PATTERN`(메뉴, 패턴 종류는 하드웨어마다) [13].
- **에러 카운터**: 패킷 헤더 ECC(1 bit 정정 · 2 bit 검출), long packet 페이로드 CRC-16, frame start/end 짝 불일치, word count 불일치 → RX 컨트롤러 상태 레지스터 [추정: 레지스터 이름은 IP마다].
- **프레임 카운터**: FS/FE 인터럽트 수 vs DMA 완료 수 vs 기대 fps × 시간 → drop 검출.
- **SerDes 경유(차량)**: 카메라 쪽 serializer ↔ 보드 쪽 deserializer(GMSL/FPD-Link 등). 체크 순서: deserializer I2C 응답 → **링크 lock 상태** → 역방향 채널로 원격 serializer·센서 I2C 접근 → 센서 test pattern → deserializer의 CSI-2 출력 → SoC RX. 많은 deserializer가 **자체 패턴 제너레이터**를 갖고 있어 카메라 없이 SoC RX만 검증 가능 [추정: 부품마다]. Rivian 카메라 링크가 GMSL인지 FPD-Link인지는 공개 확인 안 됨 [추정].

### 2.8 그 밖의 인터페이스 (간단히)

| 인터페이스 | (1) 연결 | (2) 기능 | (3) 성능·에러 | Linux 도구 |
|---|---|---|---|---|
| eMMC | CMD 응답·CID/CSD, 버스 폭 | 파티션 R/W + 비교 | HS200/HS400 **tuning** 통과 창, 처리량, 온도 | `mmc extcsd read /dev/mmcblk0` (mmc-utils), `dmesg` tuning 로그 [추정] |
| UFS | link startup(M-PHY), descriptor | R/W | gear(HS-G4 등) 협상, UIC 에러 카운터 | `/sys/bus/platform/.../ufshcd` [추정] |
| QSPI/OSPI NOR | JEDEC ID | erase/program/read | DTR·octal 모드, 샘플 지연 | `mtd_debug`, `flashcp` |
| USB | 열거, 속도(HS/SS) | 대용량 전송 비교 | 처리량, 재열거 1000회, 컴플라이언스 | `lsusb -t` (협상 속도 표시) |
| CAN / CAN-FD | 컨트롤러 up, 트랜시버 enable·종단저항 120 Ω | 내부/외부 loopback, 상대 노드 송수신 | 에러 카운터(TEC/REC), bus-off 복구, 버스 부하 | `ip link set can0 type can bitrate 500000 [loopback on]`, `cansend`, `candump`, `ip -details -statistics link show can0` |

### 2.9 자동 보드 테스트 스위트 구조 (→ BV14)

```text
 board_test.py --board B07 --profile evt1
   │
   ├─ precheck   : rail telemetry(hwmon), reset reason, 온도          ← 실패 시 즉시 중단(아래 계층 불신)
   ├─ connectivity: i2c scan(기대 주소 표) · WHO_AM_I · SPI JEDEC · MDIO PHYID · PCIe LnkSta · sensor chip ID
   ├─ function    : flash R/W/E · UART loopback · GPIO loopback · ETH loopback ①②④ · CSI-2 test pattern CRC
   ├─ perf/error  : PRBS N초 · iperf3 · CRC/ECC/AER 카운터 delta · 반복 N회
   └─ report      : PASS/FAIL 표 + JSON(보드 S/N, FW/SW 버전, 온도, 로그 링크) → 결과 DB
```

- 설계 원칙: 각 테스트는 **(이름, 계층, 기대값, 실측, 판정, 로그)** 를 남김 · **의존성 순서**(I2C 실패 시 카메라 테스트는 "BLOCKED"로, FAIL과 구분) · 카운터는 **절대값이 아니라 테스트 전후 delta** · 기대값은 보드 리비전별 YAML(EVT1에서 바뀐 I2C 주소 등).

## 3. 보드에서 볼 것 · 잴 것

### 3.1 인터페이스별 체크리스트

| 인터페이스 | 어디를 | 장비·설정 | 통과 기준 |
|---|---|---|---|
| I2C | SCL/SDA 풀업 근처 test point | DMM: 무전원 상태 SDA–VDD 저항 = 풀업 값 / scope: 30–70% tr, VOL | idle 시 양쪽 high, tr < 모드 최대, VOL < 0.4 V, 모든 기대 주소 ACK |
| I2C stuck | SDA·SCL 동시 | scope 2ch, SDA falling 트리거 + serial decode | 복구 후 SDA high, STOP 관측, WHO_AM_I 정상 |
| SPI | SCLK, CS_N, MISO | scope/LA 4ch, CS falling 트리거, SPI decode | JEDEC ID 기대값, MISO 엣지가 샘플 엣지 전에 안정 |
| UART | TX test point / USB-UART | scope로 비트 폭 측정 → 실제 baud | 비트 폭 오차 < 2%, loopback 0 오류 |
| GPIO | 핀 / 헤더 | scope 2ch (출력 + IRQ 응답 GPIO) | 레벨 OK, IRQ 수 일치, 지연 분포 |
| Ethernet | MDC/MDIO, RGMII TXC/RXC, PHY 25 MHz XTAL, PHY RESET_N, strap 핀 | scope: RXC 125 MHz(1G RGMII) 존재, MDIO decode | PHYID 기대값, link up, loopback 0 오류, CRC delta 0 |
| PCIe | REFCLK 100 MHz, PERST#, CLKREQ# | scope(차동 프로브), LnkSta | 기대 Gen/xN, AER delta 0 → LP 트랙 |
| CSI-2 | 센서 MCLK, RESET, I2C, D-PHY 레인(차동) | scope, 센서 I2C, RX 상태 레지스터 | chip ID, test pattern 일치, ECC/CRC 0, fps 일치 |

### 3.2 Linux에서 — I2C [예시 출력]

```bash
i2cdetect -l                    # 어떤 I2C 버스(어댑터)가 있나
i2cdetect -y -r 2               # 버스 2 스캔 (-r: read 방식 프로브 / -y: 확인 생략)
i2cget -y 2 0x48 0x0f           # 0x48 장치의 0x0F 레지스터 1 B 읽기
i2cdump -y 2 0x50 b             # EEPROM 전체 덤프
i2ctransfer -y 2 w1@0x48 0x0f r1   # "S addr+W reg Sr addr+R data P" 한 번에
```

```text
[예시 출력] (보드 없음 — 형식 학습용)
$ i2cdetect -y -r 2
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
40: -- -- -- -- -- -- -- -- 48 -- -- -- -- -- -- --
50: 50 -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- UU -- -- -- -- -- -- --
70: -- -- -- -- -- -- -- --
$ i2cget -y 2 0x48 0x0f
0xb5
```

- `48`, `50`: 해당 주소가 ACK — 연결 증명 1단계 통과. **기대 주소 표와 diff** 하는 게 자동화 포인트 (빠진 주소 = 전원/리셋/strap, 예상 밖 주소 = 주소 strap 핀이 다르게 실장).
- `UU`: 커널 드라이버가 이미 잡고 있는 주소 (예: 0x68 RTC). 프로브하지 않았다는 뜻이지 응답이 없다는 뜻이 아님.
- `--` 전부: 풀업 없음·전원 없음·pinmux 미설정·버스 stuck 의심 → `dmesg | grep -i i2c` 에서 timeout/arbitration lost 확인.
- `i2cget` = `0xb5`: WHO_AM_I가 기대값이면 2단계 시작 (값은 시뮬레이션용 가상값).
- 주의: `i2cdetect`의 프로브 자체가 일부 장치(쓰기 전용·상태 머신 민감)를 오동작시킬 수 있다 — man page 경고 [추정: i2c-tools 문서 경고 기억, 재확인 권장].

### 3.3 Linux에서 — SPI [예시 출력]

```bash
# MOSI-MISO 점퍼로 외부 loopback (spidev 노드 필요: DT에 spidev 호환 노드)
spidev_test -D /dev/spidev1.0 -s 10000000 -v
# SPI NOR는 MTD로 보통 보임
cat /proc/mtd ; dmesg | grep -i spi-nor
```

```text
[예시 출력]
spi mode: 0x0
bits per word: 8
max speed: 10000000 Hz (10000 kHz)
TX | FF FF FF FF FF FF 40 00 00 00 00 95 FF FF FF FF FF FF FF FF ...
RX | FF FF FF FF FF FF 40 00 00 00 00 95 FF FF FF FF FF FF FF FF ...
$ dmesg | grep -i spi-nor
spi-nor spi0.0: w25q128 (16384 Kbytes)
```

- TX와 RX가 같으면 loopback 통과 [14]. RX가 전부 `00`/`FF`이면 점퍼·pinmux·컨트롤러 enable·DT 확인 [14].
- `spi-nor ... w25q128`: 커널이 JEDEC ID(EF 40 18)를 읽어 테이블에서 찾았다는 뜻 → 연결 증명. "unrecognized JEDEC id bytes: ff ff ff" 류 메시지면 MISO/CS 문제 [추정: 메시지 문구는 커널 버전마다].
- `-s`를 올려 가며(1 → 50 MHz) 깨지는 지점 = 샘플 지연 튜닝 대상.

### 3.4 Linux에서 — Ethernet [예시 출력]

```bash
ethtool eth0                  # 속도/duplex/link/master-slave/SQI
ethtool -S eth0               # 드라이버·MAC 통계 (CRC/FCS 에러 등)
ethtool -S eth0 --all-groups  # 표준 그룹(eth-phy, eth-mac, eth-ctrl, rmon) [12]
ethtool -t eth0 offline       # self-test (external_lb 모드도 있음) [12]
ethtool --cable-test eth0     # TDR 케이블 진단 (PHY 지원 시)
ethtool -s eth0 master-slave forced-slave   # T1 역할 고정
phytool read eth0/1/2 ; phytool read eth0/1/3     # MDIO: PHY 주소 1의 reg 2, 3 [추정: phytool 설치 필요]
```

```text
[예시 출력]
$ phytool read eth0/1/2
0x2000
$ phytool read eth0/1/3
0xa284
$ ethtool eth0
Settings for eth0:
	Supported link modes:   1000baseT1/Full
	Speed: 1000Mb/s
	Duplex: Full
	master-slave cfg: forced slave
	master-slave status: slave
	Port: Twisted Pair
	PHYAD: 1
	Link detected: yes
	SQI: 7/7
$ ethtool -S eth0 | egrep -i 'crc|fcs|err'
     rx_crc_errors: 0
     rx_fcs_errors: 0
     tx_errors: 0
```

- `0x2000 / 0xa284`: PHYSID1/2 → DP83TG720S의 ID와 일치(Linux 드라이버 값 [10]) → MDIO 연결 증명. `0xffff`면 PHY 무응답(PHY RESET_N, 전원, 25 MHz 클럭, MDIO 주소 strap), `0x0000`이면 MDIO가 low로 붙음.
- `master-slave status: slave` + `Link detected: yes`: T1 역할 협상 대신 고정 설정이 맞음.
- `SQI: 7/7`: 신호 품질 최고 [추정: 출력 형식은 ethtool 버전마다].
- CRC 카운터: **테스트 전후 delta** 를 본다. 장시간 트래픽 후에도 0 = 3단계 근거.
- 형식 참고: 가상 NIC에서의 실제 `ethtool` 출력은 [LP00A §4.8](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) (Lima VM 실제 캡처).

### 3.5 Linux에서 — 카메라 (V4L2 / media controller) [예시 출력]

```bash
media-ctl -p -d /dev/media0                 # 파이프라인 토폴로지: sensor → csi2rx → dma
v4l2-ctl -d /dev/v4l-subdev0 --list-ctrls   # 센서 서브디바이스 컨트롤 (test_pattern 등)
v4l2-ctl -d /dev/v4l-subdev0 --set-ctrl test_pattern=1
v4l2-ctl -d /dev/video0 --set-fmt-video=width=1920,height=1080,pixelformat=RG10 \
         --stream-mmap --stream-count=300 --stream-to=/tmp/f.raw
```

```text
[예시 출력]
$ v4l2-ctl -d /dev/v4l-subdev0 --list-ctrls
...
                   test_pattern 0x009f0903 (menu)   : min=0 max=4 default=0 value=0 flags=...
$ v4l2-ctl -d /dev/video0 ... --stream-count=300 --stream-to=/tmp/f.raw
<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< 30.00 fps
<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< 30.00 fps
```

- `test_pattern ... (menu)`: 표준 컨트롤 `V4L2_CID_TEST_PATTERN` [13]. 메뉴 항목(컬러바 등)은 센서 드라이버마다.
- `<` 하나 = 프레임 1개 dequeue, fps가 기대값(30)과 일치 = 프레임 drop 없음의 1차 증거. 저장된 `/tmp/f.raw`를 golden 패턴과 비교(첫 프레임은 노출 안정화 전일 수 있으니 N개 버림).
- RX 에러 카운터는 표준 V4L2 인터페이스가 없어서 IP별 debugfs/레지스터를 본다 [추정].

### 3.6 PASS/FAIL 표 예시 [예시 출력]

```text
[예시 출력] board_test.py --board B07 --profile evt1
 #  layer  test                         expected          measured          result
 1  pre    rails (hwmon)                within ±3%        all in            PASS
 2  conn   i2c2 scan                    {0x48,0x50,0x68}  {0x48,0x50,UU68}  PASS
 3  conn   imu WHO_AM_I                 0xB5              0xB5              PASS
 4  conn   spi0 JEDEC                   EF 40 18          EF 40 18          PASS
 5  conn   eth0 PHYID                   0x2000A284        0x2000A284        PASS
 6  conn   pcie 0000:01:00.0 LnkSta     Gen4 x4           Gen4 x2           FAIL
 7  conn   cam0 deser lock / chip ID    locked / 0x....   no lock           FAIL
 8  func   cam0 test pattern CRC        golden            -                 BLOCKED(7)
 9  func   eth0 PHY loopback 10k frames 0 err             0 err             PASS
10  perf   eth0 PRBS 60 s               0 err             0 err             PASS (BER<~5e-11 @95%)
```

- 6번: width 저하 = 레인 하나 이상 불량 (AC cap, 레인 swap, 커넥터) → LP15 시나리오.
- 8번: 앞 단계 실패로 **BLOCKED** — FAIL 수를 부풀리지 않고, triage가 원인(7번)으로 바로 감.
- 10번 BER 괄호: 1 Gb/s × 60 s = 6e10 bit, 에러 0 → 95% 신뢰로 BER < 3/6e10 = 5e-11 (§2.5.4 공식).

## 4. 꼬리 질문 체인

### 체인 A — "How would you validate every interface on a new board?"

Q → 꼬리1 "What's the minimum test that proves connectivity?" → 꼬리2 "How do you go from 'it works' to 'it's validated'?" → 꼬리3 "How do you structure the suite so failures are easy to triage?"

<details>
<summary>A-Q. How would you validate every interface on a new board?</summary>

- **EN**: "Bottom-up and in three steps per interface. Before touching an interface I confirm its prerequisites — the rail, the reset or enable GPIO, the reference clock, and pinmux. Then step one is connectivity with the cheapest transaction, step two is function with data compare, and step three is performance, error rates and long-run stability across temperature and voltage. I write the expected values per board revision up front — I2C address map, PHY IDs, PCIe speed and width — so the suite can diff instead of a human eyeballing logs."
- 한국어 핵심: 전제(전원·리셋·클럭·pinmux) → 연결 → 기능 → 성능·에러·장시간, 기대값 표를 미리.
</details>

<details>
<summary>A-꼬리1. What's the minimum test that proves connectivity?</summary>

- **EN**: "One transaction that can only succeed if the device is powered, out of reset, clocked and wired. For I2C it's the address ACK plus a fixed ID register, because an ACK alone could be a different part at the same address. For SPI NOR, the JEDEC ID read with opcode 9F. For Ethernet, an MDIO read of PHY ID registers 2 and 3. For PCIe, link status speed and width on both ends. For a camera, the sensor chip ID over I2C — the data path comes later."
- 한국어 핵심: 장치가 살아 있어야만 성공하는 1 트랜잭션: ACK+ID, 0x9F, PHYID 2·3, LnkSta, 센서 chip ID.
</details>

<details>
<summary>A-꼬리2. How do you go from "it works" to "it's validated"?</summary>

- **EN**: "'It works' is one transfer at room temperature at default speed. 'Validated' means max rated speed, error counters measured over enough bits to be statistically meaningful — for BER 1e-12 at 95 percent confidence that's about three times ten to the twelve bits — across temperature and voltage corners, and thousands of reset, link-down or stream on-off cycles. And it has to be repeatable on multiple boards, not one golden board."
- 한국어 핵심: 최대 속도 + 통계적으로 충분한 bit + PVT 코너 + 반복 1000회 + 여러 보드.
</details>

<details>
<summary>A-꼬리3. How do you structure the suite so failures are easy to triage?</summary>

- **EN**: "Tests are ordered by dependency, and a test whose prerequisite failed is reported as blocked, not failed — so one dead deserializer doesn't show up as ten camera failures. Every result records expected, measured, the board serial, firmware version and temperature. Counters are read before and after and I report the delta. And the output is a PASS/FAIL table plus JSON into a database, so I can see whether a failure follows the board, the firmware build or the temperature."
- 한국어 핵심: 의존성 순서 + BLOCKED 구분 + 메타데이터 + 카운터 delta + DB로 "보드/빌드/온도 중 무엇을 따라가나".
</details>

### 체인 B — "I2C bus is stuck low. What do you do?"

Q → 꼬리1 "Why 9 clocks?" → 꼬리2 "SDA went high after 3 clocks — are you done?" → 꼬리3 "How do you prevent it?" → 꼬리4 "What if 9 clocks don't help?"

<details>
<summary>B-Q. The I2C bus is stuck low. What do you do?</summary>

- **EN**: "First figure out which line. If SDA is low with SCL high, it's almost always a slave that was mid-byte when the master reset — it thinks it's still sending a zero. I switch the pins to GPIO, toggle SCL up to nine times while checking SDA, then generate a STOP and verify both lines are high, then retry the transaction. If SCL itself is stuck low, clocking can't fix it — that's a slave holding the clock or a short, so I go to the slave's reset or power, or the board."
- 한국어 핵심: SDA low/SCL high = 바이트 중간 slave → GPIO로 SCL 최대 9회 + STOP. SCL low면 클럭으로 불가.
</details>

<details>
<summary>B-꼬리1. Why 9 clocks?</summary>

- **EN**: "The longest a slave can legally hold SDA is one byte plus the acknowledge bit — eight data bits and one ACK. Wherever it got stuck, nine clocks walk it to the end of that byte. If it was transmitting, the master leaves SDA high in the ACK slot, which is a NACK, so the slave stops. I reproduced this in a simulation: a slave that ACKed a read address and then returns 0x00 needs exactly nine pulses."
- 한국어 핵심: 8 data + 1 ACK = 최장 점유. 읽기면 ACK 슬롯 NACK로 종료. 시뮬레이션 S2 = 정확히 9.
</details>

<details>
<summary>B-꼬리2. SDA went high after 3 clocks — are you done?</summary>

- **EN**: "Not necessarily. SDA going high might just mean the slave is outputting a one bit — it's still mid-byte. If I then lower SCL to make a STOP, the slave puts the next bit on SDA, maybe a zero, and my STOP fails. In my simulation, reading 0x10, the naive 'clock until high, then one STOP' approach fails exactly that way. The robust approach makes every pulse a STOP attempt — SCL low, SDA low, SCL high, release SDA — and checks for an idle bus each time. That's the same idea Linux uses in its generic SCL recovery."
- 한국어 핵심: SDA high = 데이터 '1'일 수도. 매 펄스 STOP 시도 + idle 확인 (S3a 실패/S3b 성공).
</details>

<details>
<summary>B-꼬리3. How do you prevent it?</summary>

- **EN**: "Run bus recovery unconditionally at boot, before the first transaction, because the previous reset may have left a slave mid-byte. Wire reset pins of slaves that have them to GPIOs, or put the slave on a switchable supply. Use transaction timeouts with recover-and-retry in the driver. And on writes be careful: extra clocks during a write can commit garbage to a register, which is why the STOP-per-pulse approach matters."
- 한국어 핵심: 부팅 시 무조건 복구 1회, slave 리셋/전원 제어, timeout+재시도, 쓰기 중 쓰레기 커밋 주의.
</details>

<details>
<summary>B-꼬리4. What if 9 clocks don't help?</summary>

- **EN**: "Then it's not a protocol state problem. I'd measure the line with a DMM with the board off: low resistance to ground means a short — solder bridge, a damaged part, or an unpowered device back-feeding through its ESD diode. If it's a device, I isolate by lifting pull-ups or removing zero-ohm links per segment, or power-cycle the suspect slave. My simulation's SDA-shorted case reports SDA stuck low after nine pulses, which the suite escalates to a hardware ticket rather than retrying forever."
- 한국어 핵심: 9회 후에도 low = 하드웨어. 무전원 저항 측정 → 단락/역전원(back-power) → 구간 분리.
</details>

### 체인 C — "Ethernet link doesn't come up."

Q → 꼬리1 "What loopback modes would you use to isolate MAC vs PHY vs cable?" → 꼬리2 "How do you measure BER?" → 꼬리3 "It's automotive 1000BASE-T1 — what's different?"

<details>
<summary>C-Q. The Ethernet link doesn't come up. Where do you start?</summary>

- **EN**: "Management path first. Can I read the PHY ID registers 2 and 3 over MDIO? All-ones means the PHY isn't answering — check its reset GPIO, supply, 25 MHz reference clock, and the MDIO address strap. If the ID is right, I read BMCR and BMSR — reading BMSR twice because link status is latched low — and check whether autonegotiation completed or, for T1, whether master-slave roles are complementary. Then I look at the partner side and the cable."
- 한국어 핵심: MDIO PHYID → BMCR/BMSR(2번 읽기) → AN 또는 T1 master/slave → 상대·케이블.
</details>

<details>
<summary>C-꼬리1. What loopback modes would you use to isolate MAC vs PHY vs cable?</summary>

- **EN**: "I walk the loop outward. MAC internal loopback tests the MAC, DMA and descriptors with no board involvement. PHY loopback — BMCR bit 14, or the PHY's xMII or digital loopback — adds the MAC-to-PHY interface, which is where RGMII delay mistakes show up. PHY analog loopback if supported, then an external loopback plug or a traffic generator adds the connector, magnetics and cable. The first stage that fails tells me which segment is broken."
- 한국어 핵심: ① MAC 내부 → ② PHY(BMCR bit14) = RGMII delay → ③ 아날로그 → ④ 외부 플러그/장비. 처음 실패 구간이 범인.
</details>

<details>
<summary>C-꼬리2. How do you measure BER?</summary>

- **EN**: "Use the PHY's PRBS generator and checker, or a BERT for the raw SerDes, count errored bits over a known number of bits. The important part is statistics: with zero errors, the number of bits I need is minus ln of one minus confidence over the target BER — about three times ten to the twelfth for 1e-12 at 95 percent, which is roughly fifty minutes at 1 gigabit and over eight hours at 100 megabit. For shorter checks I use the PHY's signal quality index and CRC counters, and keep the long BER runs for nightly regressions and temperature corners."
- 한국어 핵심: PRBS/BERT, N = −ln(1−CL)/BER ≈ 3e12 bit (1G 50분, 100M 8.3시간), 짧게는 SQI+CRC.
</details>

<details>
<summary>C-꼬리3. It's automotive 1000BASE-T1 — what's different?</summary>

- **EN**: "Single twisted pair, PAM3 signaling, and usually no autonegotiation — one side is configured master and the other slave, by strap or register. Two masters means no link, which is a classic bring-up bug. Many T1 PHYs keep most of their registers in Clause 45 space and expose a signal quality index and cable diagnostics, which ethtool can show. Link-up time also matters because cameras and control need the network shortly after wake-up."
- 한국어 핵심: 단일 페어·PAM3·master/slave 고정, C45, SQI·cable test, link-up 시간 요구.
</details>

### 체인 D — "How do you validate a camera input without a real scene?"

Q → 꼬리1 "The test pattern comes through but with occasional CRC errors — what next?" → 꼬리2 "What if the camera is behind a SerDes link?"

<details>
<summary>D-Q. How do you validate a camera input without a real scene?</summary>

- **EN**: "Use the sensor's built-in test pattern — color bars or a fixed ramp — so the lens and lighting drop out and only the data path is tested. Bring-up order is sensor rails, MCLK, reset release, chip ID over I2C, then enable the pattern and stream. I capture frames into DDR and compare them to a golden pattern pixel by pixel, check the frame rate against the expected fps, and read the CSI-2 receiver's ECC, CRC and frame-sync error counters. If the SoC or deserializer has its own pattern generator, I use it to test the receiver without any sensor."
- 한국어 핵심: 센서 test pattern → golden 비교 + fps + ECC/CRC 카운터, 내부 패턴 제너레이터로 센서 제거.
</details>

<details>
<summary>D-꼬리1. Occasional CRC errors with the test pattern — what next?</summary>

- **EN**: "That's a signal-integrity margin problem until proven otherwise, since the payload is known. I check whether it's lane-specific, whether it scales with bit rate — drop the lane rate one step — and with temperature. Then the D-PHY settings: termination, settle-count timing on the receiver, deskew at high rates. And I'd look at the board: lane length matching and the connector. Header ECC corrections rising together with CRC errors points to the physical layer rather than the packet logic."
- 한국어 핵심: 레인별·비트율별·온도별 의존성 → D-PHY settle/termination/deskew → 레이아웃.
</details>

<details>
<summary>D-꼬리2. What if the camera is behind a SerDes link?</summary>

- **EN**: "Then there are two more links to prove first. I talk to the deserializer over local I2C, confirm the forward link is locked, then reach the remote serializer and sensor over the back-channel I2C. The deserializer usually has link error counters and often its own pattern generator, which lets me split serializer link problems from the CSI-2 hop into the SoC. Whether a given vehicle uses GMSL or FPD-Link I'd confirm from the schematic — I don't know Rivian's choice."
- 한국어 핵심: deser I2C → lock → 역방향 I2C로 원격 → deser 에러 카운터/패턴으로 구간 분리. Rivian 구성은 모름 [추정].
</details>

### 체인 E (추가) — "SPI flash reads are corrupted at 100 MHz but fine at 25 MHz."

<details>
<summary>E-Q. SPI flash reads are corrupted at high clock but fine at low clock. Why?</summary>

- **EN**: "Round-trip timing. The controller launches the clock, the flash responds after its output delay, and the data travels back over the board; at high frequency that round trip exceeds the sampling window, so the controller samples on the wrong edge. The fix is the controller's read-capture or sample delay — I sweep the delay taps at each frequency and pick the center of the passing window — or use a DQS-capable mode. I also confirm the flash's dummy cycles for the fast-read command match its configured frequency."
- 한국어 핵심: 왕복 지연 > 샘플 창 → 샘플 지연 탭 스윕(미니 shmoo), dummy cycle 확인.
</details>

### 체인 F (추가) — "UART prints garbage."

<details>
<summary>F-Q. The UART console prints garbage. What do you check?</summary>

- **EN**: "Garbage that's consistent usually means wrong baud: either the divisor or the UART's input clock is not what software assumes. I measure one bit width on a scope — at 115200 it should be about 8.68 microseconds — and compare. If the bit width is off by a fixed ratio, it's the clock source or PLL; if it's slightly off, it's divisor rounding, like 25 MHz giving minus three percent. Then voltage levels and whether the adapter is 1.8 or 3.3 volts."
- 한국어 핵심: scope로 비트 폭(115200 → 8.68 µs) 측정 → 비율 오차=클럭원, 소폭=분주 반올림, 그다음 전압 레벨.
</details>

## 5. Don 경험 연결

- "At Solidigm I verified the I2C, SPI and DMA blocks of our SoC on FPGA before RTL freeze, so the connectivity-then-function ladder is how I already worked: an ID read first, then data-compare, then stress. On silicon I'd add the parts FPGA can't show — analog PHY margins, speed corners and temperature."
- "At Apple I root-caused PCIe, I2C, SPMI and RFFE issues on wireless chipset integration with a scope and a protocol analyzer — for example `[숫자 채우기: 한 사례 — 증상, 측정, 원인]` — which is exactly the 'is it the device, the bus or the software' split this suite automates."
- "At SK hynix I built the chamber reliability test automation for NAND and PCIe Gen3–5 interfaces, so for me 'validated' means error counters over hours at temperature corners on many units — `[숫자 채우기: 보드/샘플 수, 온도 범위]`."
- Ethernet·MIPI는 레쥬메 근거 없음 → 솔직하게: "I haven't owned an Ethernet PHY bring-up, but the method transfers: management path, loopback ladder, BER statistics — and I've done the PCIe equivalent."

## 6. 실습 / 코드

### 6.1 I2C stuck-bus 복구 시뮬레이션 (실행 검증됨)

- 파일: [onsitePrep/bv10/i2c_bus_recovery_sim.c](onsitePrep/bv10/i2c_bus_recovery_sim.c)
- 모델:
  - 두 선 = open-drain **wired-AND** (`line = master_drive & slave_drive & !fault`; 1 = release, 0 = 당김). 아무도 1을 "구동"할 수 없다.
  - slave(주소 0x48): 실제 비트 단위 상태 머신 — SCL rising에서 SDA 샘플, SCL falling에서 출력 변경, SCL high 동안 SDA 엣지로 START/STOP 검출, 읽기 주소 ACK 후 **clock stretching 3 tick**.
  - master: GPIO bit-bang 원시 함수(`scl_lo/scl_hi/sda_lo/sda_rel`), `scl_hi()`는 SCL이 실제로 high가 될 때까지 기다림(stretching 지원, timeout 50 tick).
  - "master 리셋" = 두 핀 release, slave 상태는 유지 → 실제 stuck bus 재현.
- 시나리오: S1 쓰기 주소 ACK 중 멈춤 · **S2 읽기 주소 ACK + 데이터 0x00 (최악, ACK + 8 zero)** · S3a/S3b 데이터 0x10 bit7에서 멈춤(naive vs robust) · S4 SDA–GND 단락 · S5 SCL–GND 단락.
- 빌드·실행 (spec 명령 그대로):

```bash
cd onsitePrep/bv10
cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
   -fsanitize=address,undefined i2c_bus_recovery_sim.c -o /tmp/i2c_rec && /tmp/i2c_rec
```

- **실제 출력** (2026-11-01, macOS, ASan/UBSan 경고 없음, exit 0):

```text
I2C stuck-bus recovery simulation (open-drain, slave addr 0x48)

[S1] write, slave stuck driving ACK after address  (method: robust)
  stuck state:           SCL=1 SDA=0  slave=ADDR_ACK bit=8
  START attempt before recovery -> refused (bus not idle)
    pulse 1: SCL=1 SDA=1  slave=IDLE      bit=1
  recovery result: OK after 1 pulse(s); stretch waited 0 tick(s)
  after recovery:        SCL=1 SDA=1  slave=IDLE bit=1
  verify: WHO_AM_I(0x0F) = 0xB5  match
  slave saw START x3, STOP x2

[S2] read 0x00, stuck in addr ACK (worst case: ACK + 8 zeros)  (method: robust)
  stuck state:           SCL=1 SDA=0  slave=ADDR_ACK bit=8
  START attempt before recovery -> refused (bus not idle)
    pulse 1: SCL=1 SDA=0  slave=TX        bit=0
    pulse 2: SCL=1 SDA=0  slave=TX        bit=1
    pulse 3: SCL=1 SDA=0  slave=TX        bit=2
    pulse 4: SCL=1 SDA=0  slave=TX        bit=3
    pulse 5: SCL=1 SDA=0  slave=TX        bit=4
    pulse 6: SCL=1 SDA=0  slave=TX        bit=5
    pulse 7: SCL=1 SDA=0  slave=TX        bit=6
    pulse 8: SCL=1 SDA=0  slave=TX        bit=7
    pulse 9: SCL=1 SDA=1  slave=IDLE      bit=8
  recovery result: OK after 9 pulse(s); stretch waited 1 tick(s)
  after recovery:        SCL=1 SDA=1  slave=IDLE bit=8
  waveform during recovery ('-' high, '_' low):
    SCL -__--__--__--__--__--__--__--__--__--
    SDA _________________________________-__-
  verify: WHO_AM_I(0x0F) = 0xB5  match
  slave saw START x4, STOP x2

[S3a] read 0x10, stuck at bit7  (method: naive)
  stuck state:           SCL=1 SDA=0  slave=TX bit=0
  START attempt before recovery -> refused (bus not idle)
    naive pulse 1: SDA=0  (slave TX, bit 1)
    naive pulse 2: SDA=0  (slave TX, bit 2)
    naive pulse 3: SDA=1  (slave TX, bit 3)
    naive STOP   : SCL=1 SDA=0  (slave TX, bit 4)
  recovery result: STOP_FAILED after 3 pulse(s); stretch waited 1 tick(s)
  after recovery:        SCL=1 SDA=0  slave=TX bit=4
  slave saw START x2, STOP x0

[S3b] read 0x10, stuck at bit7  (method: robust)
  stuck state:           SCL=1 SDA=0  slave=TX bit=0
  START attempt before recovery -> refused (bus not idle)
    pulse 1: SCL=1 SDA=0  slave=TX        bit=1
    pulse 2: SCL=1 SDA=0  slave=TX        bit=2
    pulse 3: SCL=1 SDA=1  slave=IDLE      bit=3
  recovery result: OK after 3 pulse(s); stretch waited 1 tick(s)
  after recovery:        SCL=1 SDA=1  slave=IDLE bit=3
  verify: WHO_AM_I(0x0F) = 0xB5  match
  slave saw START x4, STOP x2

[S4] SDA shorted to GND (board fault)  (method: robust)
  stuck state:           SCL=1 SDA=0  slave=ADDR bit=0
  START attempt before recovery -> refused (bus not idle)
    pulse 1: SCL=1 SDA=0  slave=ADDR      bit=1
    pulse 2: SCL=1 SDA=0  slave=ADDR      bit=2
    pulse 3: SCL=1 SDA=0  slave=ADDR      bit=3
    pulse 4: SCL=1 SDA=0  slave=ADDR      bit=4
    pulse 5: SCL=1 SDA=0  slave=ADDR      bit=5
    pulse 6: SCL=1 SDA=0  slave=ADDR      bit=6
    pulse 7: SCL=1 SDA=0  slave=ADDR      bit=7
    pulse 8: SCL=1 SDA=0  slave=ADDR      bit=8
    pulse 9: SCL=1 SDA=0  slave=WAIT_STOP bit=8
  recovery result: SDA_STUCK_LOW after 9 pulse(s); stretch waited 0 tick(s)
  after recovery:        SCL=1 SDA=0  slave=WAIT_STOP bit=8
  slave saw START x1, STOP x0

[S5] SCL shorted to GND / endless stretch  (method: robust)
  stuck state:           SCL=0 SDA=1  slave=IDLE bit=0
  START attempt before recovery -> refused (bus not idle)
  recovery result: SCL_STUCK_LOW after 0 pulse(s); stretch waited 0 tick(s)
  after recovery:        SCL=0 SDA=1  slave=IDLE bit=0
  slave saw START x0, STOP x0

ID   method  expected       pulses actual                       verdict
S1   robust  OK             1      recovered, WHO_AM_I=0xB5     PASS
S2   robust  OK             9      recovered, WHO_AM_I=0xB5     PASS
S3a  naive   STOP_FAILED    3      STOP_FAILED                  PASS
S3b  robust  OK             3      recovered, WHO_AM_I=0xB5     PASS
S4   robust  SDA_STUCK_LOW  9      SDA_STUCK_LOW                PASS
S5   robust  SCL_STUCK_LOW  0      SCL_STUCK_LOW                PASS

suite: ALL AS EXPECTED
```

### 6.2 출력 줄별 해석

- **stuck state `SCL=1 SDA=0`**: 교과서적 stuck bus. `START attempt ... refused` — master는 SDA가 low인 버스에서 START를 만들 수 없다(START = SDA high→low). 실제 드라이버라면 "arbitration lost" 또는 timeout으로 나타남.
- **S1 (1펄스)**: slave가 쓰기 주소의 ACK를 잡고 있음. 첫 falling에서 ACK를 놓고 RX로 넘어가며, 같은 펄스의 `SDA release`가 STOP으로 인식되어 IDLE. 쓰기 바이트는 8 bit가 다 차야 커밋되므로 **쓰레기 쓰기 없음** (§2.1.3의 경고를 피한 이유).
- **S2 (정확히 9펄스)**: pulse 1 falling = ACK 종료 + 데이터 bit7(0) 출력, pulse 2–8 = bit6..0 (모두 0이라 SDA low 유지), pulse 9 falling에서 slave가 ACK 슬롯을 위해 SDA release → 같은 펄스의 STOP 성립. **"8 data + 1 ACK = 9"를 그대로 보여 줌.**
- **S2 파형**: SCL 행은 `-__-` 반복(펄스당 4개 동작: SCL low, SDA low, SCL high, SDA release). SDA 행은 마지막 펄스에서 `-__-` — 앞 `-`는 slave가 놓은 순간, `__`는 master가 STOP 준비로 당김, 마지막 `-`가 SCL high 중 상승 = **STOP**.
- **`stretch waited 1 tick`**: pulse 1 falling에서 slave가 읽기 데이터 준비로 SCL을 3 tick 잡음(그중 2 tick은 다른 동작 중 지나감). `scl_hi()`가 SCL을 다시 읽어 기다렸기 때문에 복구 중 clock stretching을 존중함. Linux 구현은 SCL을 짧게 한 번만 샘플한다 [5] — 긴 stretching slave와 복구가 충돌할 수 있는 지점.
- **S3a naive 실패**: pulse 3에서 SDA=1이지만 slave는 `TX bit 3` — 데이터 0x10의 bit4('1')를 내보내는 중일 뿐. STOP을 만들려고 SCL을 내리자 slave가 bit3(0)을 출력 → `naive STOP: SDA=0`, `STOP x0`. **"SDA high = 복구 완료"가 틀린 가정**이라는 증거.
- **S3b robust 성공**: 같은 상황에서 pulse 3의 STOP 시도가 성공(slave가 '1'을 내는 SCL high 구간에 master가 SDA를 low→high로 만들 수 있었음) → IDLE.
- **S4**: 단락은 slave와 무관하게 SDA=0. slave는 SDA 하강을 START로 보고 0 주소를 수신하다 `WAIT_STOP`. 9펄스 후 `SDA_STUCK_LOW` → 스위트는 재시도 대신 **하드웨어 이슈로 에스컬레이션**.
- **S5**: SCL 자체가 low → 펄스를 만들 수 없음 → 즉시 `SCL_STUCK_LOW` (Linux의 "SCL is stuck low, exit recovery"와 같은 분기 [5]).
- 맨 아래 표의 verdict는 "기대 결과와 같은가" — 실패를 **올바르게 검출한 것도 PASS**. 자동화 스위트가 negative test를 다루는 방식.

### 6.3 확장 아이디어 [미실행]

- slave를 2개로 늘려 "복구 펄스가 다른 slave를 오동작시키지 않는가" 확인 (AN-686은 다른 장치에 무해하다고 설명 [3]).
- Linux `i2c-gpio` + `i2c-fault-injector` debugfs(`incomplete_address_phase`, `incomplete_write_byte`)로 실제 커널 복구 경로 테스트 [6] — 보드나 GPIO 2개가 있는 SBC 필요.

## 7. 자주 틀리는 것

1. **I2C ACK만 보고 "센서 OK"** — 같은 주소의 다른 부품일 수 있다. ID 레지스터까지 읽어야 연결 증명.
2. **`i2cdetect`의 `UU`를 "응답 없음"으로 해석** — 커널 드라이버가 쓰는 중이라 프로브를 건너뛴 것.
3. **"SDA high가 됐으니 복구 끝"** — slave가 데이터 '1'을 내는 중일 수 있다(S3a). STOP까지 성공하고 idle을 확인해야 끝.
4. **SCL stuck low에 9-clock** — SCL이 low면 클럭을 만들 수 없다. slave 리셋/전원 또는 단락 조사.
5. **I2C rise time을 10–90%로 측정** — I2C 스펙은 30–70%. scope 기본값을 바꿀 것 [1].
6. **SPI JEDEC `FF FF FF`를 "flash 고장"으로 결론** — MISO를 아무도 구동하지 않는 상태(CS·전원·pinmux)인 경우가 대부분.
7. **BMSR link bit를 한 번만 읽기** — latch-low라 첫 읽기는 과거 이벤트. 두 번 읽기.
8. **RGMII delay 중복/누락** — MAC과 PHY 둘 다 2 ns delay를 넣거나 둘 다 안 넣으면 PHY loopback부터 깨진다. MAC loopback만 통과하는 패턴의 1순위 원인.
9. **"PRBS 1분 에러 0 = BER 1e-12"** — 1 Gb/s × 60 s = 6e10 bit로는 95% 신뢰 BER < 5e-11까지만 말할 수 있다.
10. **카메라 검증을 실제 영상으로 시작** — 렌즈·노출·ISP가 섞여 원인 분리가 안 됨. test pattern + golden 비교부터.

## 8. 출처

1. TI SLVA689, "I2C Bus Pullup Resistor Calculation" (30/70% 기준, 모드별 최대 tr, Rp(min)/Rp(max)) · https://www.ti.com/lit/SLVA689 · 관련 E2E 스레드(30–70% 측정 주의) · https://e2e.ti.com/support/microcontrollers/other/f/other-microcontrollers-forum/684939/tm4c1290ncpdt-i2c-pullup-resistor-calculations
2. NXP UM10204 I2C-bus specification §3.1.16 Bus clear (9 clock pulses) — NXP 커뮤니티 인용·AN4803 언급 · https://community.nxp.com/t5/S32K/S32K-IIC-Communication-Error-Recovery/m-p/1618817 · https://community.nxp.com/t5/S32K/Lpi2c-issues/m-p/2096071/highlight/true
3. Analog Devices AN-686, "Implementing an I2C Reset" (Solution 1 clocking through, Solution 2 slave 전원 스위치) · https://analog.com/an686
4. TI E2E, AMC6821 latched I2C bus recovery (UM10204 9-clock 인용) · https://e2e.ti.com/support/sensors-group/sensors/f/sensors-forum/1134228/amc6821-how-to-recover-from-latched-i2c-bus-line
5. Linux `drivers/i2c/i2c-core-base.c` — `i2c_generic_scl_recovery()`, `RECOVERY_CLK_CNT 9`, STOP-per-pulse 주석, "SCL is stuck low" · https://github.com/torvalds/linux/blob/master/drivers/i2c/i2c-core-base.c
6. Linux I2C GPIO fault injection (incomplete_address_phase, incomplete_write_byte 경고) · https://www.kernel.org/doc/html/latest/i2c/gpio-fault-injection.html
7. Winbond W25Q128JV 데이터시트 Rev F (§8.2.27 Read JEDEC ID 9Fh, Manufacturer EFh, ID 4018h — PDF 텍스트에서 확인) · https://cdn.sparkfun.com/assets/7/2/4/9/e/w25q128jv.pdf
8. Linux `include/uapi/linux/mii.h` (BMCR/BMSR 비트) — LP00A [4]에서 확인 · https://github.com/torvalds/linux/blob/master/include/uapi/linux/mii.h
9. Linux `drivers/net/phy/dp83867.c` (`DP83867_PHY_ID 0x2000a231`) · https://github.com/torvalds/linux/blob/master/drivers/net/phy/dp83867.c
10. Linux `drivers/net/phy/dp83tg720.c` (`DP83TG720S_PHY_ID 0x2000a284`, C45 BASE-T1 master/slave, cable test, SQI max 7) · https://github.com/torvalds/linux/blob/master/drivers/net/phy/dp83tg720.c
11. TI DP83TG720S-Q1 datasheet (SNLS604) — loopback 종류, 내장 데이터 제너레이터, PRBS checker · https://www.ti.com/document-viewer/DP83TG720S-Q1/datasheet/GUID-5524377F-E963-45D9-B50D-020FE6E8BD1C
12. ethtool(8) man page (`-S --all-groups`, `-t offline/online/external_lb`, `--cable-test`, `-T`, `master-slave forced-*`) · https://man7.org/linux/man-pages/man8/ethtool.8.html
13. Linux V4L2 Image Process Control Reference (`V4L2_CID_TEST_PATTERN`) · https://docs.kernel.org/userspace-api/media/v4l/ext-ctrls-image-process.html
14. ST wiki, "How to use SPI from Linux userland with spidev" (spidev_test, MOSI–MISO loopback) · https://wiki.st.com/stm32mpu/wiki/How_to_use_SPI_from_Linux_userland_with_spidev
15. LP00A primer (Ethernet/MIPI 기초, Clause 22 표, CSI-2 검증 표) · [2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html)
