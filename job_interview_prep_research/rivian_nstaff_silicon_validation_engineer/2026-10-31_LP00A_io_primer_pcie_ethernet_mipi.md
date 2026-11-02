# LP00A · I/O 입문 primer — PCIe · Ethernet · MIPI를 처음부터
<!-- theme: pastel -->

> **묶음**: 0 기초 (모든 LP 노트의 출발점) · **우선**: P0 (가장 먼저 읽기) · **선행 노트**: 없음 · **다음 노트**: [LP01 PCIe 토폴로지](2026-10-31_LP01_pcie_topology_linux_model.html) · **목록**: [LP 트랙 목록](2026-10-31_LP00_linux_pcie_track.html) · [입문 primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · [N01 lspci/LTSSM](2026-10-31_N01_pcie_lspci_ltssm.html) · [온사이트 플랜](2026-10-31_rivian_onsite_plan.html) · **코드**: 없음 (§4.6 은 Lima VM 실제 출력)

## 0. TL;DR

- 이 페이지는 **"PCIe·Ethernet·MIPI를 하나도 모른다"** 는 가정에서 시작해, 온사이트에서 세 인터페이스를 **같은 틀(물리 → 링크 → 패킷 → 소프트웨어 → 검증)** 로 설명할 수 있게 만드는 기초 노트. LP01–LP17은 전부 여기 용어를 전제로 함.
- **외울 3가지**: ① 셋 다 "**직렬 차동 신호 + 계층 프로토콜 + CRC로 에러 검출 + 링크를 먼저 맞추고(training/negotiation) 데이터를 보냄**" 이라는 같은 뼈대. ② 차이는 **거리와 상대**: PCIe = 보드 안 칩↔칩(수 cm–수십 cm, 메모리처럼 읽고 쓰기), Ethernet = 박스↔박스(수 m–100 m, 네트워크 패킷), MIPI CSI-2 = 센서→SoC 한 방향 영상(수 cm, 차에서는 serializer로 수 m). ③ 검증 엔지니어가 처음 보는 것: PCIe **LnkSta(속도/폭) + AER**, Ethernet **link up + speed/duplex + CRC 에러 카운터**, CSI-2 **프레임 카운트·fps + ECC/CRC 에러 카운터**.
- Don의 위치: PCIe ✅ 강함 · Ethernet ❌ 레쥬메 근거 없음 · MIPI CSI-2 ❌ (단 **RFFE/SPMI = MIPI Alliance 스펙**을 Apple에서 디버깅 → 다리 놓기 가능).

| | PCIe | Ethernet | MIPI CSI-2 |
|---|---|---|---|
| 무엇을 잇나 | SoC ↔ 고속 장치 (NVMe SSD, NIC, 다른 SoC/가속기) | 컴퓨터/ECU ↔ 컴퓨터/ECU (네트워크) | 카메라 이미지 센서 → SoC (ISP) |
| 비유 | **칩 사이 고속도로 + 아파트 주소 체계** (상대 장치의 레지스터를 내 메모리 주소처럼 읽고 씀) | **우체국 택배망** (주소 붙인 소포를 아무 곳으로 보냄) | **공장 컨베이어 벨트** (센서가 한 줄씩 픽셀을 계속 흘려보냄, 한 방향) |
| 속도 범위 | 레인당 2.5 GT/s (Gen1) – 64 GT/s (Gen6), x1–x16 [5] | 10 Mb/s – 400 Gb/s+, 차량은 10M/100M/1G/2.5–10G | 레인당 약 1–4.5 Gb/s (D-PHY, 버전마다 다름), 1–4 레인 [7] |
| Rivian ADAS 시스템에서 위치 [추정] | SoC ↔ SoC/가속기/SSD/Wi-Fi 같은 보드 안·근거리 고속 연결 | ECU ↔ ECU 백본 (zonal 컨트롤러, 오토노미 컴퓨터, 텔레매틱스) | 카메라 → (serializer/케이블) → deserializer → SoC CSI-2 RX → ISP |
| 검증 엔지니어가 먼저 보는 것 | 링크 L0 도달? LnkSta 속도/폭 = 기대값? AER correctable 증가? | link up? 협상된 speed/duplex? `ethtool -S` CRC/FCS 에러, drop | 프레임 들어오나? fps 맞나? ECC/CRC 에러, 프레임 drop, 테스트 패턴 픽셀 일치 |

- **30초 영어 (headline: "What's the difference between PCIe, Ethernet and MIPI CSI-2?")**: "All three are high-speed serial links built the same way — differential pairs, a physical layer that has to train or negotiate before data flows, a link layer with CRC to catch bit errors, and packets on top. The difference is what they connect. PCIe is a chip-to-chip interconnect on the board that makes a device look like memory — the CPU reads and writes its registers and the device DMAs into system memory. Ethernet is a network between boxes, with MAC addresses and frames, and in a car it's the backbone between ECUs over a single twisted pair. CSI-2 is a mostly one-way streaming link from an image sensor into the SoC's ISP, sending frames line by line. As a validation engineer I check the same things on each: did the link come up at the expected rate, are the error counters flat, and does the data match a known pattern at full throughput."

## 1. 큰 그림

### 1.1 왜 칩마다 인터페이스가 여러 개 필요한가

비유부터: 도시에 **엘리베이터(칩 내부 버스)**, **시내 도로(보드 위 PCIe)**, **고속도로·택배망(Ethernet)**, **공장 컨베이어(카메라 CSI-2)**, **인터폰(I2C/SPI/UART)** 이 따로 있는 이유와 같다. 거리, 필요한 속도, 반응 시간, 비용이 다르면 최적의 "길"이 다르다.

| 기준 | 질문 | 영향 |
|---|---|---|
| **거리** | 같은 칩 안? 같은 보드? 다른 박스(수 m)? | 거리가 길수록 신호가 약해지고 잡음이 커짐 → 강한 PHY, equalization, 에러 정정 필요 |
| **대역폭(bandwidth)** | 초당 몇 비트를 옮겨야 하나 | 카메라 8MP·30fps·12bit = 약 2.9 Gb/s, SSD = 수 GB/s, 센서 레지스터 설정 = 수 kb/s |
| **지연(latency)** | 요청 → 응답까지 몇 ns/µs/ms? | CPU가 장치 레지스터를 읽는 PCIe는 ns–µs, 네트워크는 µs–ms |
| **핀 수(pin count)** | 선 몇 가닥이 필요한가 | 칩 패키지 핀과 케이블은 비쌈 → 고속 직렬로 핀을 줄임 |
| **비용·무게** | 케이블, 커넥터, PHY 칩 가격 | 차에서는 케이블 무게도 비용 (Rivian Gen2는 zonal 구조로 배선을 줄였다고 발표 [10]) |
| **방향성** | 양방향? 한 방향? | CSI-2는 데이터가 거의 한 방향(센서 → SoC), 제어는 별도 I2C |

### 1.2 SoC 블록 다이어그램 (일반적인 ADAS SoC)

SoC (System on Chip: CPU·GPU·NPU·메모리 컨트롤러·각종 I/O 컨트롤러를 한 칩에 넣은 것) 안에서는 **NoC/AXI 같은 내부 버스(interconnect)** 가 블록들을 잇고, 바깥 세상과는 각 I/O 컨트롤러 + PHY가 잇는다.

```text
                         +-------------------------- SoC ----------------------------+
 카메라 센서              |                                                           |
 (I2C로 설정) ---I2C------+-> I2C ctrl                       CPU 클러스터 (Arm A-class) |
 영상 -----CSI-2 D-PHY----+-> D-PHY RX -> CSI-2 RX ctrl -> ISP                         |
                          |                                  |                        |
                          |   +------------------------------v--------------------+  |
                          |   |        on-chip interconnect (NoC / AXI)            |  |
                          |   +--+---------+-----------+-----------+----------+----+  |
                          |      |         |           |           |          |       |
                          |  DDR ctrl    NPU (AI     Video       PCIe RC    Ethernet  |
                          |   + DDR PHY  accelerator) encoder    + PCIe PHY  MAC      |
                          |      |                                  |          |       |
                          |  UART / SPI / GPIO / JTAG(TAP)          |          | RGMII/ |
                          +------+----------------------------------+----------+ SGMII -+
                                 |                                  |          |
                            LPDDR4/5 칩                        NVMe SSD,     Ethernet PHY 칩
                            (외부 DRAM)                         다른 SoC      (예: 1000BASE-T1)
                                                                              -> 차량 네트워크
```

- **컨트롤러(controller, MAC)** = 프로토콜 규칙을 아는 디지털 로직 (패킷 만들고 CRC 붙이고 재전송). **PHY (Physical layer)** = 실제 전압 파형을 만들고 읽는 아날로그·혼성 회로 (SerDes, 등화기). 셋 다 "controller + PHY" 쌍으로 붙는다 — 이 단어는 §2.10 에서 자세히.
- 저속 인터페이스 (JD에도 있음): **I2C** (Inter-Integrated Circuit: 2선, 수백 kHz–수 MHz, 센서/PMIC 레지스터 설정), **SPI** (Serial Peripheral Interface: 4선, 수십 MHz, 플래시·ADC), **UART** (Universal Asynchronous Receiver-Transmitter: 콘솔 로그), **JTAG** (Joint Test Action Group: CPU 디버그·바운더리 스캔). 고속 3종은 "데이터", 저속 4종은 "설정·디버그" 담당이라고 기억.

### 1.3 Rivian 스타일 ADAS 데이터 경로 [추정]

Rivian은 자체 AI 가속기 SoC **RAP1** 과 autonomy 컴퓨터 ACM3를 발표했고 (context 파일 §0) [12], Gen2 플랫폼에서 **8 MP 자체 카메라**와 **zonal 아키텍처(ECU 17 → 7)** 를 언급했다 [10]. 구체적인 인터페이스 배치는 공개되지 않았으므로 아래는 업계 일반 구조에 맞춘 **추정**이다.

```text
 [카메라 모듈]                     [수 m 케이블]                 [오토노미 컴퓨터 보드]
 이미지 센서 --CSI-2--> Serializer ==== 동축/STP ====> Deserializer --CSI-2 (4 lane)--> SoC
  (I2C 설정)            (GMSL/FPD-Link/A-PHY) [추정]     (가상 채널로 여러 카메라 합침)       |
                                                                                           v
            SoC 내부:  D-PHY RX -> CSI-2 RX -> ISP (BLC, demosaic, AWB, CCM, gamma, NR)
                                                    |
                                                    v
                                                  DDR  <---- NPU가 DDR에서 프레임 읽어 추론
                                                    |              |
                                         video encoder (H.264/265)  결과(물체 목록, 경로)
                                                    |              |
                                         녹화/텔레매틱스       Ethernet (1000BASE-T1 등)
                                                               -> zonal ECU / 차량 제어
                                       PCIe: SoC <-> 2번째 SoC/가속기, NVMe 저장장치, Wi-Fi/5G 모뎀
```

- 검증 엔지니어 관점에서 이 경로의 **각 화살표가 하나의 검증 항목**: CSI-2 RX가 4레인 최대 속도로 에러 없이 받나 → ISP 출력이 golden과 픽셀 일치하나 → DDR 대역폭이 충분하나 → NPU 처리 latency → Ethernet으로 결과가 시간 내 나가나 (TSN/gPTP 시간 동기) → PCIe로 칩 간 데이터가 처리량을 내나.

## 2. 공통 기초 개념

각 개념: **비유 → 정확한 버전**. 이 섹션을 이해하면 세 인터페이스 모두 "같은 부품의 다른 조합"으로 보인다.

### 2.1 직렬(serial) vs 병렬(parallel)

- **비유**: 병렬 = 32차선 도로에 차 32대를 **동시에 나란히** 출발시키기. 차선마다 신호등 타이밍이 조금만 달라도 도착 순서가 엉킨다. 직렬 = 1차선 고속도로에 차를 **아주 빠르게 한 줄로** 보내기.
- **정확한 버전**: 병렬 버스(옛 PCI 32-bit, DDR의 DQ 버스, MIPI DPI)는 클럭 하나에 여러 비트를 동시에 보내지만, 속도가 오르면 선 간 도착 시간 차이(**skew**)와 crosstalk 때문에 한계가 온다. 고속 직렬은 선을 적게 쓰고 각 선을 GHz급으로 돌린다. PCI(병렬, 33/66 MHz) → PCIe(직렬)로 바뀐 이유가 이것 [5]. (DDR은 아직 병렬이지만 training으로 비트마다 지연을 보정한다.)

### 2.2 차동 신호(differential pair)와 SerDes

- **비유**: 시끄러운 공사장에서 두 사람이 **같은 말을 반대 톤으로** 외치면, 듣는 사람은 둘의 **차이**만 들어서 공사 소음(둘에 똑같이 들어간 잡음)을 지운다.
- **정확한 버전**: 차동쌍은 두 선(P/N)에 반대 극성 신호를 보내고 수신기는 V(P) − V(N)만 본다 → 공통 모드 잡음 제거, 낮은 전압 스윙(수백 mV)으로 고속 가능, EMI 감소. PCIe 레인, Ethernet 페어, MIPI D-PHY 레인 모두 차동쌍.
- **SerDes (Serializer/Deserializer)**: 칩 내부의 넓은 병렬 데이터(예: 32 bit @ 250 MHz)를 한 줄의 고속 비트열(8 Gb/s)로 바꾸고 받는 쪽에서 다시 병렬로 되돌리는 회로. PCIe/Ethernet PHY의 심장.

### 2.3 레인(lane)

- **비유**: 고속도로 **차선 수**. 차선 하나 = 송신 1쌍 + 수신 1쌍 (PCIe), 또는 한 방향 1쌍 (CSI-2 데이터 레인).
- **정확한 버전**: PCIe x4 = 4 레인 = TX 4쌍 + RX 4쌍. 데이터는 레인에 바이트 단위로 **번갈아(striping)** 실린다. 레인이 하나 고장 나면 PCIe는 x2/x1로 **downtrain** 해서라도 링크를 살린다. CSI-2는 데이터 레인 1–4개 + 클럭 레인 1개 (D-PHY).

### 2.4 클럭 복원(clock recovery)과 라인 코딩 (8b/10b, 128b/130b) — 직관만

- **비유**: 메트로놈 없이 노래를 받아 적으려면 **박자를 노래 자체에서 알아내야** 한다. "음음음음…"처럼 같은 음이 오래 이어지면 박자를 놓친다 → 일부러 음이 자주 바뀌게 가사를 바꿔 쓴다.
- **정확한 버전**: PCIe·Ethernet SerDes는 별도 클럭선 없이 데이터의 0↔1 전환(edge)에서 수신 클럭을 뽑는다 (**CDR, Clock Data Recovery**). 0이나 1이 길게 이어지면 CDR이 놓치고 DC 편향이 생기므로 **라인 코딩**으로 전환을 보장한다.
  - **8b/10b**: 8비트를 10비트 코드로 → 오버헤드 20% (Gen1/Gen2, 1000BASE-X/SGMII). 그래서 Gen1 2.5 GT/s는 실제 2.0 Gb/s = 250 MB/s per lane [5].
  - **128b/130b**: 128비트마다 2비트 헤더 + **scrambling**(의사 난수 XOR로 비트 패턴을 섞어 전환 확보) → 오버헤드 약 1.5% (PCIe Gen3–Gen5).
  - **64b/66b**: 10G 이상 Ethernet (USXGMII, 10GBASE-R).
  - Gen6는 **PAM4** (한 심볼에 2비트: 전압 4단계) + **FLIT** (고정 크기 256 B 블록) + **FEC** (Forward Error Correction: 에러를 재전송 없이 고치는 부호) [5].
- **MIPI D-PHY는 예외**: 별도 **클럭 레인**이 있음(forwarded clock, 소스 동기식) → 라인 코딩 불필요. D-PHY v3.5는 embedded clock 옵션을 추가 [7]. C-PHY는 3선 trio에 클럭을 심볼 전환에 심는다.
- **GT/s vs Gb/s**: GT/s(giga-transfers/s)는 선 위의 **원시 심볼 전환 수**, 인코딩 오버헤드를 빼야 실제 데이터 비트율.

### 2.5 계층 프로토콜 (PHY / Link / Transaction)

- **비유**: 택배. ① **도로·트럭**(물리층: 실제 운반) ② **송장·파손 검사·재배송**(링크층: 한 구간에서 제대로 갔는지 확인) ③ **주문서 내용**(트랜잭션층: "주소 X에 4바이트 써줘").
- **정확한 버전**: 각 층은 아래 층을 "믿을 수 있는 파이프"로 쓰고 위 층에 서비스를 준다. 장점: 디버그할 때 **"어느 층에서 깨졌나"** 로 분리 가능 — 이게 검증 엔지니어의 기본 사고방식.

| 층 | PCIe | Ethernet | MIPI CSI-2 |
|---|---|---|---|
| 위 (트랜잭션/프로토콜) | Transaction Layer: **TLP** (메모리 읽기/쓰기, config, 메시지) | 상위 프로토콜: IP/TCP/UDP, gPTP, AVTP | Application: 픽셀 → 바이트 포장 (RAW10 packing) |
| 중간 (링크) | Data Link Layer: **DLLP**, ACK/NAK, LCRC, 재전송 | **MAC**: 프레임, MAC 주소, FCS(CRC-32) | Low Level Protocol: short/long packet, ECC, CRC-16 |
| 아래 (물리) | Physical: SerDes, 8b/10b·128b/130b, **LTSSM** | **PHY**: PCS/PMA/PMD, auto-neg, 신호 변조 | **D-PHY / C-PHY**: HS/LP 모드, 레인 분배 |

### 2.6 패킷(packet)

- **비유**: 소포 = 송장(헤더) + 내용물(payload) + 봉인 스티커(CRC).
- **정확한 버전**: 고속 링크는 데이터를 **헤더(누가/무엇/얼마나) + payload + 무결성 체크** 단위로 자른다. PCIe TLP(헤더 12–16 B + 최대 Max_Payload_Size), Ethernet 프레임(14 B 헤더 + 46–1500 B + 4 B FCS), CSI-2 long packet(4 B 헤더 + 한 줄 픽셀 + 2 B CRC).

### 2.7 CRC와 재전송(retry)

- **비유**: 소포에 "무게 1.234 kg" 스티커를 붙여 보내고, 받는 쪽이 다시 잰다. 다르면 **파손**. 어떤 택배사는 다시 보내 달라고 하고(PCIe), 어떤 곳은 버리고 위층에 맡기고(Ethernet → TCP가 재전송), 어떤 곳은 "파손됨" 표시만 하고 계속 받는다(CSI-2: 실시간 영상이라 기다릴 수 없음).
- **정확한 버전**: **CRC (Cyclic Redundancy Check)** = 다항식 나눗셈의 나머지로 만든 체크값, 버스트 에러 검출에 강함. **ECC (Error Correcting Code)** 는 검출 + 1비트 정정까지.

| | 검출 | 에러 시 동작 | 카운터 위치 |
|---|---|---|---|
| PCIe | TLP에 **LCRC 32-bit**, DLLP에 16-bit CRC, (옵션) ECRC | 수신측 **NAK** → 송신측 replay buffer에서 **재전송** (하드웨어 자동). 반복 실패 시 Recovery 진입 | AER correctable: Bad TLP, Bad DLLP, Replay Timer Timeout, Receiver Error |
| Ethernet | 프레임 끝 **FCS = CRC-32** | MAC이 프레임 **drop** (재전송 없음) → TCP가 알아서 재전송, UDP는 손실 | `ethtool -S` 의 rx_crc_errors 류, `ip -s link` errors |
| CSI-2 | 패킷 헤더 **ECC** (1비트 정정·2비트 검출), payload **CRC-16** | 재전송 없음 — 수신기가 에러 플래그/인터럽트, 프레임 drop 또는 손상 프레임 전달 | CSI-2 RX 컨트롤러 vendor 레지스터 (ECC/CRC/frame sync 에러) |

### 2.8 링크 트레이닝 / 협상 (training / negotiation)

- **비유**: 처음 만난 두 사람이 대화 전에 "**어떤 언어로? 얼마나 빨리 말할까? 잘 들려?**"를 맞추는 과정.
- **정확한 버전**:
  - **PCIe**: **LTSSM** (Link Training and Status State Machine) — Detect(상대 있나) → Polling(비트·심볼 정렬) → Configuration(레인 수·번호) → **L0**(정상 동작) → 필요시 Recovery로 속도 올리기 + Gen3+ equalization. 자세히 [LP03](2026-10-31_LP03_link_training_ltssm.html), [N01](2026-10-31_N01_pcie_lspci_ltssm.html).
  - **Ethernet**: **Auto-negotiation** (Clause 28 for BASE-T) — 양쪽 PHY가 "나는 1G full/100M full 가능" 을 광고(link pulse) → 공통 최고 모드 선택. 1000BASE-T는 master/slave(클럭 주도권)도 결정. 차량용 T1은 보통 **master/slave를 설정으로 고정**하고 auto-neg 생략 [추정: 802.3 Clause 98 auto-neg는 옵션].
  - **MIPI D-PHY**: 협상이 거의 없음 — 속도·레인 수를 **양쪽 소프트웨어가 미리 똑같이 설정**(DT `data-lanes`, `link-frequencies`). 전송 시작 시 LP→HS 전환 시퀀스와 sync 패턴만. 그래서 "설정 불일치 = 그냥 안 됨"이 흔한 버그.

### 2.9 대역폭 vs 지연 (bandwidth vs latency)

- **비유**: 대역폭 = 고속도로 **차선 수 × 제한속도** (시간당 몇 대), 지연 = 차 한 대가 **도착까지 걸리는 시간**. 차선을 늘려도 서울→부산 시간은 줄지 않는다.
- **정확한 버전**: throughput(바이트/초)과 latency(요청~응답 시간)는 독립. 작은 패킷이 많으면 헤더 오버헤드 때문에 유효 대역폭이 떨어진다 (PCIe TLP 헤더, Ethernet 38 B 프레임 오버헤드). JD의 "throughput, latency, resource utilization" 벤치마크는 이 둘을 따로 재라는 뜻.
- 숫자 감각: 1 Gb/s Ethernet에서 1500 B payload 프레임은 선 위에 1538 B (preamble 8 + 헤더 14 + FCS 4 + IFG 12) → 최대 효율 약 97.5%, TCP goodput 실측 약 940 Mb/s 수준 [추정: 일반적 계산값].

### 2.10 레지스터 / MMIO / DMA

- **비유**: **MMIO** (Memory-Mapped I/O) = 장치의 스위치판(레지스터)을 **집 주소 체계에 편입**해서, CPU가 "0x4000_1000번지에 1 쓰기" = "장치의 시작 버튼 누르기"가 되게 함. **DMA** (Direct Memory Access) = CPU가 박스를 하나하나 나르지 않고 **택배 기사(DMA 엔진)에게 "여기서 저기로 이만큼" 쪽지만 주고** 다른 일 하기.
- **정확한 버전**:
  - 레지스터 = 장치 내부 플립플롭에 주소를 붙인 것. 비트마다 RW/RO/W1C(1을 써서 지움) 같은 속성.
  - MMIO: CPU의 load/store가 interconnect를 거쳐 장치 레지스터로 감. 캐시하면 안 되므로 **device memory** 속성으로 매핑. Linux에서는 `ioremap()` + `readl()/writel()`.
  - DMA: 장치(또는 DMA 엔진)가 **descriptor(쪽지: 소스 주소, 목적지 주소, 길이, 다음 쪽지 주소)** 를 읽고 DDR에 직접 읽고 씀 → 완료 시 인터럽트. PCIe에서는 장치가 **bus master** 가 되어 메모리 쓰기 TLP를 보냄. 캐시 일관성(coherency)·IOMMU 문제는 [LP08](2026-10-31_LP08_dma_iommu_coherency.html).
  - 세 인터페이스 모두 결국 **"CPU는 MMIO로 컨트롤러를 설정하고, 데이터는 DMA로 DDR에 들어간다"**. CSI-2 RX → DDR의 프레임 버퍼, Ethernet MAC → DDR의 RX descriptor ring, PCIe 장치 → DDR.

### 2.11 PHY와 MAC/컨트롤러가 정확히 뭔가

- **비유**: **MAC/컨트롤러 = 편지를 쓰고 봉투에 주소를 적는 사무원**, **PHY = 편지를 실제로 실어 나르는 트럭과 도로 상태를 아는 운전사**.
- **정확한 버전**:
  - **MAC (Media Access Control) / controller**: 디지털 로직. 프레임·패킷 조립, CRC 생성/검사, 재전송, DMA 인터페이스, 레지스터 블록. SoC에 IP로 들어감 (예: Synopsys DesignWare PCIe, Cadence GEM Ethernet, CSI-2 RX IP).
  - **PHY (Physical layer device)**: 아날로그·혼성 신호. SerDes, 드라이버/수신기, CDR, equalizer, 라인 코딩, 링크 상태 감지. PCIe·CSI-2는 보통 **SoC 안에 PHY 내장**, Ethernet은 **별도 PHY 칩**이 흔함 (그래서 MAC↔PHY 사이 인터페이스 RGMII/SGMII와 관리 버스 MDIO가 따로 있음, §4.2).
  - 디버그 첫 질문: "**PHY까지는 됐나(링크 up, 신호 lock), MAC 위에서 깨지나(CRC, 패킷 drop)?**"

## 3. PCIe 처음부터

### 3.1 무엇인가 · 역사

- **비유**: PCIe는 칩 사이의 **"원격 메모리 통로"**. CPU 입장에서 PCIe 장치는 **주소 공간의 일부**처럼 보인다 — 특정 주소를 읽으면 그게 TLP 패킷이 되어 장치로 가고 응답이 돌아온다. 네트워크처럼 "메시지를 주고받는" 느낌이 아니라 **"멀리 있는 메모리를 읽고 쓰는" 느낌**이 핵심.
- **역사**: 1992 **PCI** (Peripheral Component Interconnect, 32-bit 병렬 공유 버스, 33 MHz, 133 MB/s) → PCI-X → 2003 **PCI Express 1.0** (직렬, 점대점, 레인 단위). 소프트웨어 호환을 위해 **PCI의 config space·BAR·열거 모델을 그대로 유지** → 그래서 Linux도 "PCI 서브시스템"이 PCIe를 다룸 [5].
- **정확한 정의**: 점대점(point-to-point) 직렬, 패킷 기반, 양방향 동시(full-duplex) 칩 간 인터커넥트. 공유 버스 대신 **스위치**로 트리를 만든다.

### 3.2 토폴로지: Root Complex · Switch · Endpoint

```text
          CPU + DDR
             |
     +-------v--------+
     |  Root Complex   |   <- SoC 안의 PCIe host (CPU 쪽 출입구)
     | RP0  RP1   RP2  |   <- Root Port: 각각 링크 하나의 위쪽 끝
     +--+----+-----+---+
        |    |     |
        |    |   [Switch] (Upstream Port 1개 + Downstream Port 여러 개)
        |    |     |    \
        | [NIC]  [SSD]  [AI 가속기 Endpoint]
      [NVMe SSD]
     Endpoint (EP) = 실제 일하는 장치, 트리의 잎
```

- **Root Complex (RC)**: CPU/메모리 쪽 대표. CPU의 load/store를 TLP로 바꾸고, 장치의 DMA TLP를 DDR 접근으로 바꾼다. **Root Port (RP)** 는 RC의 각 출구.
- **Switch**: 링크 하나를 여러 개로 가지치기 (택배 허브). 소프트웨어에는 PCI-to-PCI **bridge** 묶음으로 보임.
- **Endpoint (EP)**: 잎 장치. Don의 Solidigm SSD 컨트롤러가 EP 쪽. 반대로 Rivian SoC는 보통 **RC 쪽**(다른 장치를 붙이는 쪽)이지만, SoC 두 개를 PCIe로 붙이면 한쪽은 **EP 모드** [추정].
- 주소 체계: **BDF** (Bus:Device.Function, 예: `01:00.0`) = 아파트 "동:호:방". 자세히 [LP01](2026-10-31_LP01_pcie_topology_linux_model.html).

### 3.3 레인과 속도 (x1–x16, Gen1–Gen6)

| 세대 | 연도 | 레인당 전송률 | 인코딩 | 레인당 유효 대역폭 (한 방향) | x16 |
|---|---|---|---|---|---|
| Gen1 (1.0) | 2003 | 2.5 GT/s | 8b/10b | 약 0.25 GB/s | 약 4 GB/s |
| Gen2 (2.0) | 2007 | 5.0 GT/s | 8b/10b | 약 0.5 GB/s | 약 8 GB/s |
| Gen3 (3.0) | 2010 | 8.0 GT/s | 128b/130b | 약 0.985 GB/s | 약 15.8 GB/s |
| Gen4 (4.0) | 2017 | 16.0 GT/s | 128b/130b | 약 1.97 GB/s | 약 31.5 GB/s |
| Gen5 (5.0) | 2019 | 32.0 GT/s | 128b/130b | 약 3.94 GB/s | 약 63 GB/s |
| Gen6 (6.0) | 2022 | 64.0 GT/s | PAM4 + FEC + FLIT (1b/1b) | 약 7.56 GB/s | 약 121 GB/s |
| Gen7 (7.0) | 2025 | 128.0 GT/s | PAM4 + FLIT | 약 15.1 GB/s | 약 242 GB/s |

출처 [5]. 기억법: **세대마다 2배**, Gen3에서 인코딩이 바뀌어 "8 GT/s인데 Gen2(5 GT/s)의 거의 2배". 링크 폭은 x1, x2, x4, x8, x16. 실제 성능은 TLP 헤더·flow control 오버헤드로 이 값보다 낮다. Gen5/Gen6·retimer·차량용 PCIe는 [LP17](2026-10-31_LP17_gen5_gen6_retimer_automotive.html).

### 3.4 3개 층

```text
  소프트웨어: "주소 0x8000_0010 에 0x1 써"   (CPU store, 또는 장치의 DMA)
       |
 [Transaction Layer]  TLP 만들기: MWr(메모리 쓰기) / MRd(읽기) / CplD(읽기 응답 + 데이터)
       |               / CfgRd·CfgWr(config) / Msg(인터럽트·에러·전원 메시지)
       |               + flow control credit (상대 버퍼 남은 칸 수 확인 후에만 송신)
 [Data Link Layer]    TLP에 12-bit 시퀀스 번호 + LCRC 32-bit 붙이기, replay buffer 보관
       |               수신측: CRC OK -> ACK DLLP / 실패 -> NAK DLLP -> 재전송
 [Physical Layer]     8b/10b or 128b/130b, scrambling, 레인 striping, SerDes, LTSSM
       |
   ===== 차동쌍 (TX 1쌍 + RX 1쌍) x N 레인 =====
```

- **TLP (Transaction Layer Packet)**: 실제 일(읽기/쓰기/메시지)을 담은 패킷. **Posted**(MWr: 응답 없음, 보내고 끝) vs **Non-posted**(MRd, CfgRd: 반드시 Completion 응답). 이 차이가 "write 후 read-back으로 flush" 습관의 이유 ([LP06](2026-10-31_LP06_endpoint_driver_dev.html)).
- **DLLP (Data Link Layer Packet)**: 링크 관리용 작은 패킷 — ACK/NAK, flow control update, 전원 관리. 소프트웨어는 직접 안 봄.
- **Physical / LTSSM**: 링크를 올리고 유지하는 상태 머신 ([LP03](2026-10-31_LP03_link_training_ltssm.html)).

### 3.5 Config space · BAR · 열거(enumeration) — 쉬운 말로

- **비유**: 새 아파트에 입주자(장치)가 들어오면 관리사무소(OS)가 **각 집 현관의 명패(config space)** 를 읽는다: "제조사 누구, 장치 종류, 우리 집 창고 몇 평 필요(BAR)". 관리사무소는 전체 동 지도를 그리며 **창고 주소를 나눠 준다**. 이 과정이 **열거(enumeration)**.
- **정확한 버전**:
  - **Config space**: 함수(Function)마다 있는 표준 레지스터 공간. PCI 호환 256 B + PCIe extended 4 KiB. Vendor ID/Device ID(offset 0x00), Command/Status(0x04), Class code, Header Type, BAR0–5(0x10–0x24), Capability 포인터(0x34). 자세히 [LP02](2026-10-31_LP02_config_space_capabilities.html).
  - **BAR (Base Address Register)**: 장치가 "내 레지스터/메모리 창을 **이 크기만큼** 시스템 주소에 매핑해 달라"고 요청하는 칸. OS가 all-1s를 써서 크기를 알아내고(sizing), 실제 주소를 써 넣는다.
  - **Capability list**: PCIe 링크 정보(LnkCap/LnkSta), MSI, 전원 관리, AER 등 추가 기능이 연결 리스트로 이어짐.
  - **열거**: 버스 0부터 모든 Device/Function의 Vendor ID를 읽어 봄 → 0xFFFF면 "없음" → bridge면 아래 버스 번호 배정하고 재귀 → BAR 크기 계산 → 주소 배정 → 드라이버 매칭(probe). 자세히 [LP04](2026-10-31_LP04_enumeration_resources_hotplug.html). SoC 쪽 host controller 설정(DT, iATU, PERST#)은 [LP05](2026-10-31_LP05_soc_host_controller_integration.html).

### 3.6 인터럽트 (INTx → MSI → MSI-X)

- **비유**: 옛날엔 장치가 **공용 초인종 줄(INTx 핀)** 을 당겼다 (여러 집이 공유, 누가 눌렀는지 다 확인해야 함). **MSI** (Message Signaled Interrupt) 는 **"특정 주소에 특정 값을 쓰는 것"이 곧 초인종** — 우편함에 쪽지를 넣는 방식.
- **정확한 버전**: MSI = 장치가 OS가 지정한 주소로 MWr TLP를 보냄 → 인터럽트 컨트롤러(ARM은 GIC ITS)가 이를 CPU 인터럽트로 변환. MSI 최대 32 벡터, MSI-X 최대 2048 벡터 + 벡터별 mask. 장치가 MSI를 보내려면 Command 레지스터의 **Bus Master** 비트가 켜져야 함 (MSI도 메모리 쓰기니까). 자세히 [LP07](2026-10-31_LP07_interrupts_intx_msi_msix.html).

### 3.7 DMA / bus mastering

- 장치가 스스로 MRd/MWr TLP를 만들어 시스템 메모리를 읽고 쓰는 것 = **bus mastering**. 드라이버는 `pci_set_master()`로 허락, `dma_set_mask()`로 장치가 낼 수 있는 주소 폭을 알려 줌. ARM SoC는 **IOMMU (SMMU)** 가 장치 주소를 물리 주소로 번역·보호 → [LP08](2026-10-31_LP08_dma_iommu_coherency.html).

### 3.8 "link up / L0"이 뭔 뜻인가

- **L0** = LTSSM의 정상 동작 상태 (데이터 흐름 가능). **DL_Active** = 그 위 Data Link Layer까지 초기화 완료 (flow control 초기화 끝). "link up" 은 보통 DL_Active까지 의미.
- 저전력 상태: L0s, L1 (ASPM으로 자동 진입), L1.1/L1.2 substates, L2/L3 (전원 꺼짐) → [LP10](2026-10-31_LP10_power_mgmt_aspm.html). 리셋 종류 (PERST#, hot reset, FLR) → [LP11](2026-10-31_LP11_reset_types.html).
- **LTSSM 상태 자체는 표준 레지스터에 없다** → 소프트웨어는 root port의 LnkSta `DLActive+`, speed/width로 간접 확인. 정확한 상태는 컨트롤러 vendor 디버그 레지스터 또는 프로토콜 애널라이저 ([N01](2026-10-31_N01_pcie_lspci_ltssm.html) — 10-30 스크린에서 실제로 받은 질문).

### 3.9 Linux에서 보이는 모습

실제 출력 (Lima VM `elinux`, 2026-10-31 캡처) — 여기서 재미있는 점: **이 VM의 Ethernet 카드(virtio-net)도 PCI 장치**다. "Ethernet MAC이 PCIe에 붙어 있다"는 관계를 그대로 보여 줌.

```text
$ lspci -nn
00:00.0 Host bridge [0600]: Apple Inc. Device [106b:1a05]
00:01.0 Ethernet controller [0200]: Red Hat, Inc. Virtio 1.0 network device [1af4:1041] (rev 01)
00:05.0 Communication controller [0780]: Red Hat, Inc. Virtio 1.0 console [1af4:1043] (rev 01)
00:06.0 Mass storage controller [0180]: Red Hat, Inc. Virtio 1.0 block device [1af4:1042] (rev 01)
...
00:0c.0 USB controller [0c03]: Apple Inc. Device [106b:1a06]
```

| 부분 | 해석 |
|---|---|
| `00:01.0` | BDF: bus 0, device 1, function 0 |
| `Ethernet controller [0200]` | class code 0x0200 = Network / Ethernet. 이 값이 config space offset 0x0A–0x0B에 있음 |
| `[1af4:1041]` | Vendor ID 0x1AF4 (Red Hat/virtio) : Device ID 0x1041 (virtio 1.0 net) — 드라이버 매칭에 쓰는 키 |
| `Host bridge` | Root Complex 쪽 대표. 가상 머신이라 실제 PCIe 링크(LTSSM)는 없음 — LnkSta 실습은 QEMU `pcie-root-port` 로 ([LP13](2026-10-31_LP13_linux_pcie_tools.html)) |

- 핵심 도구: `lspci -tv`(트리), `lspci -vvv`(LnkCap/LnkSta/AER), `lspci -k`(드라이버), `/sys/bus/pci/devices/*/current_link_speed`, `dmesg | grep -i pci`. 총정리는 [LP13](2026-10-31_LP13_linux_pcie_tools.html). 에러(AER/DPC)는 [LP09](2026-10-31_LP09_aer_dpc_error_handling.html), SR-IOV/VFIO는 [LP12](2026-10-31_LP12_sriov_vfio_acs_p2p.html).

### 3.10 검증 엔지니어는 PCIe에서 무엇을 테스트하나

| 영역 | 테스트 | 노트 |
|---|---|---|
| 링크 | 각 Gen·폭에서 L0 도달, 목표 속도 도달률, 1000회 cold/warm reset 반복 후 링크 성공률, 강제 Gen1로 bisect | [LP03](2026-10-31_LP03_link_training_ltssm.html), [LP14](2026-10-31_LP14_validation_methodology.html) |
| 열거·리소스 | BAR 크기·주소, 모든 capability 정상, rescan/hotplug | [LP04](2026-10-31_LP04_enumeration_resources_hotplug.html) |
| 데이터 경로 | DMA read/write 처리량, 다양한 payload/정렬, 데이터 비교(golden), MSI 도착 | [LP06](2026-10-31_LP06_endpoint_driver_dev.html), [LP08](2026-10-31_LP08_dma_iommu_coherency.html) |
| 에러 | AER 카운터 장시간 모니터링, 에러 주입(aer-inject, 컨트롤러 기능), 복구 | [LP09](2026-10-31_LP09_aer_dpc_error_handling.html) |
| 전원·리셋 | ASPM L1 진입/탈출 반복, FLR/hot reset 후 복구 | [LP10](2026-10-31_LP10_power_mgmt_aspm.html), [LP11](2026-10-31_LP11_reset_types.html) |
| 디버그 | 링크 안 뜸, 다운그레이드, 런타임 사망 시나리오 | [LP15](2026-10-31_LP15_debug_scenarios.html) |
| 플랫폼 | bare-metal vs Linux, Zynq UltraScale+ PCIe | [LP16](2026-10-31_LP16_baremetal_vs_linux_zynq.html) |

## 4. Ethernet 처음부터

### 4.1 무엇인가

- **비유**: Ethernet은 **택배망**. 모든 소포(프레임)에 **받는 사람 주소(목적지 MAC 주소)·보낸 사람 주소·내용물 종류(EtherType)** 를 쓰고, 끝에 **파손 검사 스티커(FCS)** 를 붙인다. 중간 **허브/스위치(분류 센터)** 가 주소를 보고 맞는 길로 보낸다. PCIe처럼 "남의 메모리를 직접 읽는" 게 아니라 **메시지를 보내면 받는 쪽 소프트웨어가 해석**.
- **정확한 정의**: IEEE 802.3 표준 계열. Layer 1(PHY) + Layer 2(MAC) 를 정의. 위에 IP/TCP/UDP가 얹힘. 원래 공유 동축 케이블 + CSMA/CD였지만 지금은 **스위치 + 점대점 full-duplex** 가 기본.

### 4.2 MAC vs PHY, 그리고 그 사이 인터페이스

```text
 +----------- SoC -----------+                 +--- PHY 칩 (별도) ---+
 |  CPU  <-MMIO->  MAC        |  데이터:        |  PCS / PMA / PMD    |   케이블
 |  DDR  <--DMA--  (프레임,   |  RGMII/SGMII --> |  (부호화, 변조,     | ====== RJ45 (4쌍)
 |                  FCS 생성) |  관리:          |   echo cancel,     |   또는 T1 단일 페어
 |        MDIO ctrl ----------|  MDC + MDIO --> |   auto-neg, link)  |
 +---------------------------+                 +---------------------+
```

- **MAC**: 프레임 조립/해체, FCS(CRC-32) 생성·검사, 주소 필터, pause 흐름 제어, DMA descriptor ring. SoC 안 IP (Zynq UltraScale+는 Cadence **GEM**, Linux `macb` 드라이버 [추정]).
- **PHY**: 비트를 케이블 위 아날로그 파형으로. 하위 블록 **PCS** (Physical Coding Sublayer: 4B/5B, 8b/10b, 64b/66b 같은 부호화), **PMA** (Physical Medium Attachment: SerDes), **PMD** (Physical Medium Dependent: 실제 송수신기). auto-negotiation, link 감지도 PHY가 함.
- **MAC↔PHY 데이터 인터페이스** (이름이 많아 헷갈리는 부분):

| 이름 | 풀네임 | 신호 | 속도 | 특징 |
|---|---|---|---|---|
| MII | Media Independent Interface | 4-bit TX/RX, 25 MHz | 100 Mb/s | 원조, 핀 많음 (약 16개) |
| RMII | Reduced MII | 2-bit, 50 MHz | 10/100 Mb/s | 핀 절약, 저가 MCU |
| GMII | Gigabit MII | 8-bit, 125 MHz | 1 Gb/s | 핀 많음 |
| **RGMII** | Reduced Gigabit MII | 4-bit, 125 MHz **양 edge(DDR)** | 10/100/1000 | 12핀 [1]. **클럭-데이터 사이 1.5–2 ns 지연 필요** → 누가 넣나(PHY? MAC? PCB?) 설정 실수가 #1 버그 [1] |
| SGMII | Serial GMII | 차동 직렬 1쌍씩, 1.25 Gbaud, 8b/10b | 10/100/1000 | 핀 4개, SerDes 사용 |
| USXGMII | Universal Serial 10G MII | 직렬, 10.3125 Gbaud, 64b/66b | 10M–10G | 멀티기가 |

- RGMII 지연 모드 (Linux `phy-mode`): `rgmii`(PHY 지연 없음 — MAC/PCB가 넣어야), `rgmii-txid`, `rgmii-rxid`, `rgmii-id`(PHY가 양방향 지연). **증상: 링크는 up인데 패킷 손실·FCS 에러, 또는 10/100에서는 되고 1G에서만 안 됨** [1]. 면접에서 "Ethernet link up but no traffic" 질문의 정답 후보 1순위.

### 4.3 MDIO/MDC — PHY 관리 버스

- **비유**: 트럭(데이터 경로 RGMII)과 별도로, 운전사에게 **무전기(MDIO)** 로 "속도 1G로", "네 상태 어때?"를 묻는다. PCIe의 config space, CSI-2 센서의 I2C 설정과 같은 역할 — **"관리 경로(management path)"**.
- **정확한 버전**: MDC(클럭, MAC이 구동) + MDIO(양방향 데이터) 2선 직렬 버스. 한 버스에 PHY 여러 개(주소로 구분). Linux에서는 `mii_bus` 장치로 등록되고 `read()/write()`가 `mii_id`(PHY 주소)와 `regnum`을 받는다 [1].
  - **Clause 22**: PHY 주소 5 bit + 레지스터 주소 5 bit → PHY당 32개 16-bit 레지스터.
  - **Clause 45**: 포트 주소 + **MMD** (MDIO Manageable Device: PCS, PMA 등 하위 블록) 번호 + 16-bit 레지스터 주소 → 훨씬 큰 공간. 10G·automotive T1 PHY는 주로 C45 [추정].
- 꼭 알 Clause 22 레지스터 (`include/uapi/linux/mii.h` 기준 [4]):

| 레지스터 | 이름 | 핵심 비트 |
|---|---|---|
| 0x00 | **BMCR** (Basic Mode Control) | bit15 `BMCR_RESET` 0x8000, bit14 **`BMCR_LOOPBACK` 0x4000** (PHY loopback), bit13 `SPEED100`, bit12 `ANENABLE`, bit9 `ANRESTART`, bit8 `FULLDPLX`, bit6 `SPEED1000` |
| 0x01 | **BMSR** (Basic Mode Status) | bit2 **`BMSR_LSTATUS` 0x0004** (link status, latch-low — 한 번 읽으면 갱신), bit3 AN 가능, bit5 **`ANEGCOMPLETE` 0x0020** |
| 0x02/0x03 | PHYSID1/2 | PHY 제조사 OUI + 모델 — "PHY가 MDIO로 응답하나?" 첫 확인 |
| 0x04 | ADVERTISE | 내가 광고하는 능력 |
| 0x05 | LPA | 상대(link partner)가 광고한 능력 |

- 검증 첫 단계: MDIO로 PHYSID 읽기 → 0xFFFF면 PHY 무응답(리셋 GPIO, 전원, 클럭, MDIO 주소 strap 확인).

### 4.4 Auto-negotiation

- **비유**: 두 사람이 악수하며 "나 영어·한국어 돼" "나 한국어·일본어" → 공통인 한국어로.
- **정확한 버전**: BASE-T PHY는 링크 펄스(FLP)로 ADVERTISE 레지스터 내용을 교환 → 양쪽 공통 최고 모드 (예: 1000 full) 선택 → BMSR AN complete. 1000BASE-T는 이때 **master/slave** (누가 기준 클럭) 도 정함. 실패 패턴: 한쪽은 auto, 한쪽은 강제 100 full → **duplex mismatch** (link up, 처리량 저하, late collision).
- PCIe LTSSM과 비교: PCIe는 일단 Gen1으로 올라간 뒤 속도를 올림, Ethernet BASE-T는 협상 후 바로 결정 모드로 링크 형성.

### 4.5 프레임 구조

```text
 선 위의 순서 →
 +----------+-----+--------+--------+------------+-----------+------------------+--------+-------+
 | Preamble | SFD | Dst MAC| Src MAC| 802.1Q tag | EtherType | Payload           | FCS    | IFG   |
 | 7 B 0x55 | 0xD5| 6 B    | 6 B    | 4 B (옵션) | 2 B       | 46–1500 B         | 4 B    | 12 B  |
 +----------+-----+--------+--------+------------+-----------+------------------+--------+-------+
            PHY가 붙임   |<------------- MAC 프레임 (64–1518 B, VLAN 시 1522 B) ------->|  쉬는 시간
                         802.1Q tag = TPID 0x8100 + PCP(3 bit 우선순위) + DEI(1) + VID(12 bit VLAN ID)
```

- **MAC 주소**: 48-bit. 첫 바이트 bit0 = 1이면 multicast (`ff:ff:ff:ff:ff:ff` = broadcast).
- **EtherType**: payload 종류 — 0x0800 IPv4, 0x86DD IPv6, 0x0806 ARP, **0x88F7 PTP** (시간 동기), 0x8100 VLAN tag, 0x22F0 AVTP (IEEE 1722 오디오/비디오 전송) [추정: 차량 카메라·오디오 스트리밍에 쓰임].
- **VLAN**: 같은 물리망을 논리적으로 나눔 + **PCP 우선순위** → 차량에서 제어 트래픽과 인포테인먼트 분리 [추정].
- **FCS (Frame Check Sequence)**: Dst MAC ~ payload에 대한 **CRC-32**. 수신 MAC이 틀리면 drop하고 카운터 증가. 검증 엔지니어의 "가장 친한 카운터".
- 최소 64 B(짧으면 padding), 최대 1518 B. 9000 B **jumbo frame** 은 비표준 확장.

### 4.6 속도 계열

| 이름 | 속도 | 매체 | 메모 |
|---|---|---|---|
| 10BASE-T / 100BASE-TX | 10 / 100 Mb/s | 2쌍 Cat5 | 사무실 고전 |
| 1000BASE-T | 1 Gb/s | **4쌍** Cat5e, 100 m | PC 기본 |
| 2.5G/5GBASE-T (802.3bz) | 2.5 / 5 Gb/s | 4쌍, 100 m | [9] |
| 10GBASE-T, 10GBASE-R (광/DAC), 25G/100G/400G | 10 G 이상 | 다양 | 데이터센터 |
| **10BASE-T1S** (802.3cg) | 10 Mb/s | **단일 페어, multidrop**(버스형) | CAN 대체 후보 [추정] |
| **100BASE-T1** (802.3bw) | 100 Mb/s | **단일 페어**, 약 15 m | 원조 BroadR-Reach, PAM3 [추정] |
| **1000BASE-T1** (802.3bp-2016) | 1 Gb/s | 단일 페어, PAM3 | [9] |
| **2.5/5/10GBASE-T1** (802.3ch-2020) | 2.5–10 Gb/s | 단일 페어 15 m, PAM8 | [9] — 카메라/고속 백본용 |

### 4.7 자동차 Ethernet

- **왜 다른가**: 차는 **무게·비용·EMI** 가 중요 → 4쌍 대신 **단일 비차폐 twisted pair(UTP 1쌍)**, 15 m 정도면 충분, 온도·진동 범위가 넓음 (AEC-Q 등급 부품) [추정].
- **auto-neg 대신 master/slave 고정**: 차 안 연결은 설계 시 고정이라 협상할 필요가 없고, **링크가 빨리 올라와야** 함 (시동 후 수십–수백 ms 안에 카메라·제어가 살아야). 한쪽을 master(클럭 주도), 다른 쪽을 slave로 **strap 핀이나 레지스터로 고정**. 둘 다 master로 설정하면 링크 안 뜸 — 단골 bring-up 버그 [추정].
- **TSN (Time-Sensitive Networking)** — 802.1 표준 묶음 [추정: 차량 채택 범위는 OEM마다 다름]:
  - **802.1AS gPTP** (generalized Precision Time Protocol): 네트워크 전체 시계를 sub-µs로 맞춤. 카메라 프레임·레이더·LiDAR 데이터에 **같은 시간축 타임스탬프**를 찍어 센서 퓨전. MAC/PHY의 **하드웨어 타임스탬프** 기능이 필요 (`ethtool -T`로 확인).
  - 802.1Qav CBS (Credit-Based Shaper: 오디오·비디오 대역폭 보장), 802.1Qbv TAS (Time-Aware Shaper: 시간 슬롯 예약), 802.1CB FRER (이중 경로 중복 전송).
- **Rivian [추정]**: Gen2에서 ECU를 17 → 7로 줄이고 west/east/south **zonal 컨트롤러** 사용 [10]. 대역폭이 필요한 곳만 Ethernet, 단순 제어는 CAN/LIN 유지 [10]. 오토노미 컴퓨터 ↔ zonal ECU 연결이 Ethernet일 가능성이 높음 (공식 확인 안 됨).

### 4.8 Linux에서 보는 Ethernet

실제 출력 (Lima VM, virtio-net, 2026-10-31 캡처). 가상 NIC라 PHY가 없어 일부 필드는 "Unknown" — 그 자체가 좋은 학습 포인트.

```text
$ ip -br link
lo               UNKNOWN        00:00:00:00:00:00 <LOOPBACK,UP,LOWER_UP>
eth0             UP             52:55:55:ca:85:3c <BROADCAST,MULTICAST,UP,LOWER_UP>

$ ip -s link show eth0
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP mode DEFAULT group default qlen 1000
    link/ether 52:55:55:ca:85:3c brd ff:ff:ff:ff:ff:ff
    RX:  bytes packets errors dropped  missed   mcast
       6529599    5434      0       0       0       0
    TX:  bytes packets errors dropped carrier collsns
        164670    1528      0       0       0       0
```

| 부분 | 해석 |
|---|---|
| `UP` (flags 안) | 관리적으로 켬 (`ip link set eth0 up`) |
| `LOWER_UP` | **물리 링크(carrier) 있음** — PHY가 link up을 보고. `UP`인데 `LOWER_UP` 없음 = 케이블/PHY/협상 문제 |
| `state UP` | 운영 상태(operstate) |
| `mtu 1500` | 최대 payload 크기 |
| `link/ether 52:55:...` | MAC 주소 |
| RX/TX `errors dropped missed` | 일반화된 에러 카운터. 하드웨어 상세는 `ethtool -S` |
| `carrier` | carrier 손실 횟수 — 링크 플랩(up/down 반복) 감지 |

```text
$ ethtool eth0
Settings for eth0:
	Supported link modes:   Not reported
	Supports auto-negotiation: No
	Speed: Unknown!
	Duplex: Unknown! (255)
	Auto-negotiation: off
	Port: Other
	PHYAD: 0
	Transceiver: internal
	Link detected: yes

$ ethtool -i eth0
driver: virtio_net
version: 1.0.0
bus-info: 0000:00:01.0
supports-statistics: yes
supports-test: no
supports-eeprom-access: no
supports-register-dump: no

$ sudo ethtool -S eth0 | head
NIC statistics:
     rx_drops: 0
     rx_xdp_packets: 0
     ...
     tx_kicks: 1390
     tx_tx_timeouts: 0
```

| 필드 | 해석 (실제 하드웨어라면) |
|---|---|
| `Speed` / `Duplex` | 협상된 속도·duplex. 실제 PHY면 `1000Mb/s`, `Full`. 가상 NIC라 Unknown |
| `Auto-negotiation` | 협상 사용 여부. T1 PHY는 보통 off + master/slave 표시 [추정] |
| `PHYAD` | MDIO 상 PHY 주소 |
| `Link detected` | PHY의 BMSR link status를 커널이 보고 |
| `bus-info: 0000:00:01.0` | **이 NIC가 PCI 장치 00:01.0** — §3.9 lspci 결과와 연결 |
| `supports-test: no` | `ethtool -t` (self-test, 보통 레지스터·메모리·**loopback** 테스트) 미지원 — 실제 NIC 드라이버(e1000e, igb 등)는 지원 [추정] |
| `ethtool -S` | **드라이버별** 하드웨어 카운터. 실제 MAC은 `rx_crc_errors`, `rx_fcs_errors`, `rx_length_errors`, `rx_missed`, `tx_carrier_errors` 류 — 이름이 드라이버마다 다름 [추정] |

그 밖의 도구 (명령 위주, 출력은 장비마다 다름):

```bash
ethtool -s eth0 speed 100 duplex full autoneg off   # 강제 속도 (bisect용)
ethtool -r eth0                     # auto-neg 재시작
ethtool -t eth0 offline             # 드라이버 self-test (loopback 포함 가능)
ethtool -T eth0                     # 하드웨어 타임스탬프(PTP) 지원 여부
ethtool -d eth0                     # MAC 레지스터 덤프 (드라이버 지원 시)
phytool read eth0/1/0x01            # MDIO로 PHY 주소 1의 BMSR 읽기 (phytool 설치 필요)
iperf3 -s  /  iperf3 -c <ip> -t 60  # TCP 처리량, -u -b 900M 으로 UDP + 손실률
tcpdump -i eth0 -e -nn ether proto 0x88f7   # gPTP 프레임만 (-e: MAC 헤더 표시)
dmesg | grep -iE "eth|phy|macb|stmmac|link is"   # "Link is Up - 1Gbps/Full" 류 로그
```

Device tree (SoC MAC + 외부 PHY) — [예시]:

```text
&gem3 {                                   /* SoC 의 Ethernet MAC */
    status = "okay";
    phy-mode = "rgmii-id";                /* MAC-PHY 인터페이스 + 지연 담당 */
    phy-handle = <&phy0>;                 /* 이 MAC 의 PHY 는 아래 노드 */
    mdio {
        #address-cells = <1>;
        #size-cells = <0>;
        phy0: ethernet-phy@1 {
            reg = <1>;                    /* MDIO 주소 = PHY strap 값과 일치해야 */
            reset-gpios = <&gpio 38 1>;   /* PHY 리셋 핀 */
        };
    };
};
```

- 커널 구조: MAC 드라이버(`drivers/net/ethernet/...`) + **phylib**(PHY 추상화 계층, PHY 상태 머신, 링크 변경 시 MAC의 `adjust_link` 콜백 호출) [1] + (요즘) phylink.

### 4.9 Ethernet 검증 — 새 실리콘의 MAC을 어떻게 테스트하나

| 단계 | 테스트 | 무엇을 분리하나 |
|---|---|---|
| 1. 관리 경로 | MDIO로 PHYSID 읽기, BMCR reset 후 기본값 | PHY 전원·리셋·클럭·MDIO 배선 |
| 2. **MAC loopback** (내부) | MAC TX → MAC 안에서 RX로 되돌림, 패턴 비교 | MAC·DMA·descriptor 로직만 (PHY·보드 제외) |
| 3. **PHY loopback** (BMCR bit14) | MAC → RGMII/SGMII → PHY에서 되돌림 | **MAC↔PHY 인터페이스 타이밍 (RGMII delay)** |
| 4. 외부 loopback / 상대 장비 | loopback 플러그 또는 테스트 장비(트래픽 제너레이터)와 연결 | PHY 아날로그, 커넥터, 케이블 |
| 5. **PRBS / BER** | PHY test mode로 의사난수 패턴 송수신, 비트 에러율 (예: 1e-12 목표) | 신호 무결성(SI) 마진 [추정: T1 PHY test mode 지원은 칩마다] |
| 6. 처리량·지연 | `iperf3` TCP/UDP, **RFC 2544 개념**: 프레임 크기 64·128·256·512·1024·1280·1518 B 별 throughput / latency / frame loss / back-to-back | 작은 프레임에서 DMA·인터럽트 병목 |
| 7. 링크 안정성 | link up/down 1000회 반복 (케이블 탈착, PHY reset, 전원 사이클), link-up 시간 측정 | 상태 머신·드라이버 경쟁 조건 |
| 8. 장시간 | 24–72 h 트래픽 + `ethtool -S` CRC/FCS 카운터 0 유지, 온도 챔버 | 온도·전압 마진 (Don의 SK hynix 챔버 경험 연결) |
| 9. 기능 | VLAN, 멀티캐스트 필터, checksum offload, **PTP 타임스탬프 정확도** | 오프로드 하드웨어 |

### 4.10 Ethernet vs PCIe 비교

| 항목 | PCIe | Ethernet |
|---|---|---|
| 목적 | 칩↔칩 인터커넥트, **메모리처럼** 접근 | 박스↔박스 네트워크, **메시지** 전달 |
| 거리 | 보드 내 수십 cm (retimer로 연장) | 수 m – 100 m (광은 km) |
| 주소 | 메모리 주소 + BDF | MAC 주소 (48-bit) |
| 토폴로지 | 트리 (RC → switch → EP) | 스위치 망 (임의 연결) |
| 에러 시 | 링크층 **하드웨어 재전송** (ACK/NAK) | 프레임 **drop**, 상위 TCP가 재전송 |
| 링크 시작 | LTSSM training (Gen1 → 목표 속도) | auto-neg (BASE-T) / 고정 master-slave (T1) |
| 관리 경로 | config space (in-band, 같은 링크로) | MDIO (out-of-band, 별도 2선) |
| Linux | PCI 서브시스템, `lspci`, AER | netdev + phylib, `ip`, `ethtool` |
| 대표 카운터 | AER correctable/uncorrectable | `ethtool -S` CRC/FCS, drops |
| 관계 | — | 많은 NIC가 **PCIe 장치**로 붙음 (§3.9 실제 출력) |

## 5. MIPI 처음부터

### 5.1 MIPI Alliance 패밀리

- **비유**: MIPI는 회사 이름이 아니라 **"휴대폰 부품 연결 규격을 만드는 협회"** (MIPI = Mobile Industry Processor Interface). 휴대폰에서 시작해서 지금은 자동차·IoT까지. 협회가 만든 **여러 규격 세트**가 있고, 그중 카메라용이 CSI-2.
- 패밀리 (Don이 이미 아는 것 표시):

| 규격 | 무엇 | 메모 |
|---|---|---|
| **CSI-2** (Camera Serial Interface 2) | 카메라 센서 → SoC 영상 | 이 노트의 주인공. 최신 v4.2 (2025-12), 가상 채널 최대 32 [6] |
| DSI / DSI-2 (Display Serial Interface) | SoC → 디스플레이 | CSI-2의 반대 방향 쌍둥이 |
| **D-PHY** | CSI-2/DSI 아래 물리층: 클럭 레인 + 데이터 레인 차동쌍 | 가장 흔함. v3.0에서 9 Gb/s (standard channel) [7] |
| C-PHY | 3선 trio, 심볼 인코딩, 클럭 내장 | 핀 대비 대역폭 큼 |
| **A-PHY** | **자동차용 장거리** SerDes, 최대 15 m, 2–16 Gb/s (v1.x), v2.0 32 Gb/s [8] | IEEE 2977-2021로도 채택 [8]. CSI-2·DSI-2를 PAL(Protocol Adaptation Layer)로 실음 |
| I3C | I2C 후속 (더 빠르고 in-band 인터럽트) | 최신 카메라 제어용 |
| CCI (Camera Control Interface) | 센서 레지스터 제어 = 사실상 **I2C** | CSI-2 스펙의 일부 |
| **RFFE** (RF Front-End Control Interface) | RF 프론트엔드(PA, LNA, 스위치, 튜너) 제어 2선 버스 | **Don: Apple에서 디버깅 ✅** |
| **SPMI** (System Power Management Interface) | PMIC 제어 2선 버스 | **Don: Apple에서 디버깅 ✅** |

- **Don의 다리**: RFFE/SPMI도 MIPI Alliance 스펙 → "MIPI 스펙 문서를 읽고, 커맨드 프레임·패리티 같은 무결성 비트를 이해하고, 프로토콜 애널라이저로 디코드하며 루트코즈하는 방식"은 이미 해 봄. 다른 점은 CSI-2가 **수 Gb/s 고속 차동 + 영상 스트림**이라는 것 (RFFE/SPMI는 수십 MHz 저속 제어 버스).

### 5.2 CSI-2의 물리층: D-PHY 레인

```text
  이미지 센서 (TX)                                 SoC (RX)
  +-----------+   Clock lane (CLK_P/CLK_N) ----->  +------------+
  | CSI-2 TX  |   Data lane 0 (D0_P/D0_N) ----->  | D-PHY RX    |
  |  + D-PHY  |   Data lane 1 ----------------->  |  -> CSI-2   |
  |   TX      |   Data lane 2 ----------------->  |    RX ctrl  |
  +-----------+   Data lane 3 ----------------->  +------------+
        ^                                                |
        +------------ I2C (CCI): 센서 레지스터 설정 <------+   (SoC 가 master)
```

- **클럭 레인 1개 + 데이터 레인 1–4개**, 모두 **한 방향**(센서 → SoC). 클럭은 DDR 방식(양 edge 사용) → 레인당 비트율 = 2 × 클럭 주파수. Linux의 `link-frequencies` 는 이 **클럭 주파수** (비트율의 절반) [2].
- **두 가지 모드**:
  - **HS (High-Speed)**: 저전압 차동(약 200 mV 스윙), 수백 Mb/s – 수 Gb/s. 실제 픽셀 전송.
  - **LP (Low-Power)**: 단일 종단 1.2 V 신호, 약 10 Mb/s. 상태 표시·제어. 두 선 상태로 **LP-11 (둘 다 high = 정지/stop state)** , LP-01, LP-00 등을 표현.
  - 매 패킷 전송: LP-11 → LP-01 → LP-00 → HS 진입 + sync 바이트 → 데이터 → HS 종료 → LP-11. 라인 블랭킹 동안 LP로 돌아가 전력 절약 (또는 continuous clock 모드).
  - 커널 문서: 송신기는 HS 진입 전 잠깐 LP-11을 거치며 이 창이 100 µs만큼 짧을 수 있음, 일부 수신기는 스트리밍 전 LP-11을 봐야 해서 `.pre_streamon()` 으로 센서를 LP-11에 두는 콜백이 있음 [2]. 전원 직후 기본은 LP-00 [2].
- 레인당 속도: D-PHY v1.2 약 2.5 Gb/s, v2.x 약 4.5 Gb/s [추정: 버전별 수치는 MIPI 공개 페이지에 명시 안 됨], v3.0 9 Gb/s [7].

### 5.3 CSI-2 패킷

- **비유**: 컨베이어 벨트 위에 **"프레임 시작" 팻말 → (한 줄짜리 픽셀 상자 + 라벨) × 1080개 → "프레임 끝" 팻말**. 팻말 = short packet, 상자 = long packet.

```text
 한 프레임 (예: 1920x1080 RAW12) 의 전송 순서:
   [FS] [LS?][Long: line 0][LE?] [LS?][Long: line 1][LE?] ... [Long: line 1079] [FE]
    FS = Frame Start (short, DT 0x00)   FE = Frame End (short, DT 0x01)
    LS/LE = Line Start/End (short, DT 0x02/0x03, 옵션)

 Short packet (4 바이트):
   +---------+--------------------------+-------+
   | Data ID | 16-bit data (frame 번호)  | ECC   |
   +---------+--------------------------+-------+

 Long packet:
   +---------+-------------+------+------------------------------+---------+
   | Data ID | Word Count  | ECC  | Payload (WC 바이트 = 한 줄)   | CRC-16  |
   | 1 B     | 2 B         | 1 B  | 예: 1920 px x 12 bit = 2880 B | 2 B     |
   +---------+-------------+------+------------------------------+---------+
   |<------ packet header 32 bit ----->|                           packet footer

 Data ID (8 bit) = [7:6] Virtual Channel (VC) | [5:0] Data Type (DT)
   (CSI-2 v2.0+ 는 VCX 확장 비트로 VC 를 더 늘림 — 최대 32 VC [6])
```

- **Data Type** (Linux `include/media/mipi-csi2.h` 값 [3]):

| DT | 의미 |
|---|---|
| 0x00 / 0x01 | Frame Start / Frame End (short) |
| 0x02 / 0x03 | Line Start / Line End (short) |
| 0x12 | Embedded 8-bit data (센서 메타데이터: 노출·게인 등) |
| 0x1E / 0x1F | YUV422 8-bit / 10-bit |
| 0x24 | RGB888 |
| 0x2A / 0x2B / 0x2C / 0x2E | **RAW8 / RAW10 / RAW12** / RAW16 |
| 0x30–0x37 | User defined |

- **RAW10 packing**: 픽셀 4개(40 bit) = 5 바이트 (상위 8비트씩 4바이트 + 하위 2비트 4개를 모은 1바이트). RAW12는 픽셀 2개 = 3 바이트. Word Count가 이 packing 결과와 안 맞으면 수신기 에러 → 흔한 bring-up 버그.
- **ECC**: 헤더 24 bit를 보호, **1비트 에러 정정 + 2비트 검출**. **CRC-16**: payload 보호 (검출만). 재전송은 없다 — 영상은 실시간이라 다음 프레임이 더 중요.
- **Virtual Channel (VC)**: 하나의 물리 링크에 **여러 논리 스트림**을 섞음. 비유: 한 컨베이어에 카메라 A 상자는 빨간 라벨, B는 파란 라벨. 차량에서는 deserializer가 카메라 4대를 VC 0–3으로 묶어 SoC의 CSI-2 포트 하나로 보냄 [추정: 일반적 구성]. 같은 카메라의 영상(RAW)과 메타데이터(embedded data)를 DT로 구분하기도 함.

### 5.4 카메라 제어 (I2C/CCI)

- 센서는 CSI-2로 **데이터만** 보내고, **설정은 SoC가 I2C(CCI)로** 센서 레지스터에 씀: 해상도, 프레임레이트, 노출 시간, 아날로그 게인, 레인 수, 비트 깊이, **테스트 패턴** 선택, streaming on/off.
- 순서 (전형적 bring-up): 전원 레일 → 클럭(MCLK) → 리셋 해제 → I2C로 chip ID 읽기 → 레지스터 테이블 쓰기 → SoC CSI-2 RX 설정 (레인 수·DT·VC 일치) → 센서 stream on → 프레임 수신 확인.
- Don 연결: I2C 디버깅은 Solidigm·Apple에서 이미 함 ✅ — "센서가 I2C에 ACK 안 하면 그건 이미 내가 아는 디버그".

### 5.5 SoC 안의 수신 경로와 ISP

```text
 D-PHY RX  ->  CSI-2 RX controller  ->  ISP  ->  DDR (프레임 버퍼)  ->  NPU / video encoder / 디스플레이
 (레인 정렬,    (패킷 파싱, ECC/CRC,      |
  deskew,       VC/DT 필터, 바이트->
  HS/LP 감지)   픽셀 unpack)               v
            ISP (Image Signal Processor) 파이프라인 [추정: 블록 순서는 벤더마다 다름]
              BLC       -> 렌즈 shading -> Demosaic -> AWB    -> CCM      -> Gamma/톤맵 -> NR / sharpen -> 색공간 변환(YUV)
              (검은 레벨    (가장자리      (Bayer ->   (흰색을    (색 보정    (밝기 곡선)   (노이즈 감소)
               빼기)        어두움 보정)    RGB)        흰색으로)   행렬)
```

- **센서는 "Bayer RAW"** 를 낸다: 픽셀마다 R, G, B 중 **한 색만** 측정 (RGGB 모자이크 필터). 그래서 **demosaic** (주변 픽셀로 나머지 두 색 추정) 이 필요.
- 각 블록 비유: **BLC** (Black Level Correction: 빛이 없어도 센서가 내는 기본값을 빼기 = 저울 영점 맞추기), **AWB** (Auto White Balance: 형광등 아래 흰 종이를 흰색으로), **CCM** (Color Correction Matrix: 센서 색을 표준 색공간으로 3×3 행렬 변환), **Gamma** (사람 눈에 맞게 밝기 곡선), **NR** (Noise Reduction). HDR 센서는 여러 노출을 합치는 단계가 앞에 추가.
- ADAS에서는 NPU가 **ISP 출력(YUV/RGB) 또는 RAW에 가까운 데이터**를 받아 추론 [추정]. Video encoder(H.264/H.265)는 녹화·원격 전송용.

### 5.6 Linux에서 보는 카메라: V4L2 + media controller

- **V4L2** (Video for Linux 2): 카메라·영상 장치 API. `/dev/videoN` (프레임을 받는 DMA 출구, "video node"), `/dev/v4l-subdevN` (센서·CSI-2 RX·ISP 같은 중간 블록 = **subdev**), `/dev/mediaN` (**media controller**: 블록들이 어떻게 연결됐는지 그래프).
- 드라이버 구조: 센서 드라이버 (I2C 클라이언트, `drivers/media/i2c/`), CSI-2 RX 드라이버 (SoC 플랫폼 드라이버), 링크 주파수 전달은 `V4L2_CID_LINK_FREQ` 컨트롤 / `v4l2_get_link_freq()` [2].
- 대역폭 공식 (D-PHY) [2]: `pixel_rate = link_freq × 2 × lanes / bits_per_sample`. 예: 750 MHz × 2 × 4 / 12 = **500 Mpixel/s**. 8 MP × 30 fps = 240 Mpixel/s (+블랭킹) → 여유 있음. 이 계산을 면접에서 즉석으로 할 수 있으면 좋다.

Device tree (센서 ↔ CSI-2 RX 연결) — [예시]:

```text
&i2c1 {
    sensor@10 {                                /* I2C 주소 0x10 */
        compatible = "sony,imx219";
        clocks = <&cam_mclk>;
        reset-gpios = <&gpio 5 1>;
        port {
            sensor_out: endpoint {
                remote-endpoint = <&csi_in>;   /* 상대편 endpoint */
                data-lanes = <1 2 3 4>;        /* 사용 데이터 레인 */
                clock-lanes = <0>;
                link-frequencies = /bits/ 64 <750000000>;  /* D-PHY 클럭 Hz */
            };
        };
    };
};
&csi2rx {
    port@0 { csi_in: endpoint { remote-endpoint = <&sensor_out>; data-lanes = <1 2 3 4>; }; };
};
```

명령과 [예시 출력] (이 VM에는 카메라가 없음 — `/dev/video*` 없음을 확인함. 커널에 `vivid`(Virtual Video Test Driver) 모듈은 있음):

```bash
v4l2-ctl --list-devices                 # 카메라 장치 목록
media-ctl -d /dev/media0 -p             # 파이프라인 그래프: 엔티티, 패드, 포맷, 링크
media-ctl -d /dev/media0 -V '"imx219 1-0010":0 [fmt:SRGGB10_1X10/1920x1080]'   # 패드 포맷 설정
v4l2-ctl -d /dev/v4l-subdev0 --list-ctrls        # 센서 컨트롤 (exposure, test_pattern ...)
v4l2-ctl -d /dev/v4l-subdev0 --set-ctrl test_pattern=1
v4l2-ctl -d /dev/video0 --set-fmt-video=width=1920,height=1080,pixelformat=RG10 \
         --stream-mmap --stream-count=300 --stream-to=/tmp/frames.raw   # 300 프레임 캡처, fps 출력
```

```text
[예시 출력] media-ctl -p (일부)
- entity 1: imx219 1-0010 (1 pad, 1 link)
            type V4L2 subdev subtype Sensor
            device node name /dev/v4l-subdev0
        pad0: Source
                [fmt:SRGGB10_1X10/1920x1080 field:none]
                -> "csi2rx":0 [ENABLED]
- entity 4: csi2rx (2 pads, 2 links)
            type V4L2 subdev subtype Unknown
        pad0: Sink
                <- "imx219 1-0010":0 [ENABLED]
        pad1: Source
                -> "video-capture":0 [ENABLED]
```

| 줄 | 해석 |
|---|---|
| `entity ... Sensor` | 센서 subdev, I2C 버스 1 주소 0x10 |
| `fmt:SRGGB10_1X10/1920x1080` | 미디어 버스 포맷: Bayer RGGB 10-bit, 1080p — CSI-2 DT 0x2B (RAW10) 에 대응 |
| `-> "csi2rx":0 [ENABLED]` | 센서 출력이 CSI-2 RX 입력에 연결됨. **양 끝 패드 포맷이 일치해야** stream 시작 가능 (불일치 시 `EPIPE`) |
| `video-capture` | 최종 DMA → DDR 출구 (`/dev/video0`) |

- 실습 recipe `[미실행]`: VM에서 `sudo modprobe vivid` → `v4l2-ctl --list-devices` → `--stream-mmap` 으로 테스트 패턴 캡처. (VM 시스템 상태를 바꾸므로 이번엔 실행하지 않음.)
- Zynq UltraScale+ 에서는 AMD **MIPI CSI-2 RX Subsystem** IP + Linux `xilinx-csi2rxss` 드라이버 + Video Frame Buffer Write IP 조합 [추정] → [LP16](2026-10-31_LP16_baremetal_vs_linux_zynq.html) 에서 bare-metal 대응과 함께.

### 5.7 CSI-2 검증 — 새 실리콘의 CSI-2 RX를 어떻게 테스트하나

| 단계 | 테스트 | 무엇을 분리하나 |
|---|---|---|
| 1. 제어 경로 | I2C로 센서 chip ID, 레지스터 R/W | 전원·클럭·리셋·I2C |
| 2. **센서 테스트 패턴** (color bar, 고정 값, ramp) | 센서가 알려진 패턴을 내게 하고 DDR 프레임을 **golden과 픽셀 단위 비교** | 렌즈·조명 제외, 데이터 경로만 검증 |
| 3. (있으면) **SoC 내부 패턴 제너레이터 / loopback** 또는 **MIPI 패턴 제너레이터 장비** | 센서 없이 RX에 정확히 아는 패킷 주입 (일부러 ECC/CRC 에러 패킷 포함) | 센서 문제와 RX 문제 분리, 에러 검출 로직 검증 |
| 4. 링크 파라미터 매트릭스 | 레인 수 1/2/4 × 비트율 단계 × DT(RAW8/10/12, YUV422) × 해상도 | 설정 조합, unpack 로직 |
| 5. 에러 카운터 | ECC 정정/검출, CRC 에러, frame sync 에러(FS 없이 FE), word count 불일치 | SI 마진, 프로토콜 처리 |
| 6. **프레임 카운트·fps·drop** | FS/FE 인터럽트 카운트 vs DMA 완료 수, short packet frame number 연속성 | DDR 대역폭 부족·DMA 지연으로 인한 drop |
| 7. 레인 skew / deskew | 높은 비트율에서 레인 간 지연 보정 (D-PHY v1.2+ deskew 시퀀스) [추정] | 보드 배선 길이 차이 |
| 8. **멀티 VC / 멀티 카메라** | deserializer 통해 4대 동시, VC별 프레임 분리·동기 | VC 라우팅, 동시 DMA |
| 9. 장시간·환경 | 24 h+ 스트리밍, 온도 챔버, 전압 마진, stream on/off 반복 | 드문 에러, 상태 머신 |
| 10. 성능 | 센서 노출 → DDR 도착 latency, CSI-2 → ISP → NPU end-to-end | JD의 throughput/latency 벤치마크 |

### 5.8 차량의 카메라 링크: GMSL / FPD-Link / A-PHY [추정]

- CSI-2 D-PHY는 **수십 cm** 가 한계 → 차량 카메라는 수 m 떨어져 있음 → 카메라 모듈 안 **serializer** 가 CSI-2를 받아 동축(coax) 또는 STP 1가닥으로 고속 SerDes 전송, 컴퓨터 보드의 **deserializer** 가 다시 CSI-2로 복원.
- 대표: ADI(구 Maxim) **GMSL** (Gigabit Multimedia Serial Link) 2/3, TI **FPD-Link** III/IV — 독자 규격. 표준 대안이 **MIPI A-PHY** (15 m, 최대 16 Gb/s(v1.x)) [8].
- 이 링크는 **역방향 채널**로 I2C/GPIO도 터널링 → SoC가 원격 센서를 I2C로 설정 가능 (A-PHY도 I2C·GPIO PAL 지원 [8]).
- 검증 포인트: serializer/deserializer 링크 lock 상태 레지스터, 링크 에러 카운터, 케이블 길이별 마진, 전원(PoC: Power over Coax) [추정].

## 6. 세 인터페이스 비교표

| 항목 | PCIe | Ethernet | MIPI CSI-2 |
|---|---|---|---|
| 목적 | 칩↔칩 고속 I/O, 메모리 의미론 | 박스↔박스 네트워크 | 센서 → SoC 영상 스트림 |
| 토폴로지 | 트리 (RC / switch / EP), 점대점 링크 | 스위치 망, 점대점 (10BASE-T1S는 버스) | 점대점 단방향 (+ VC로 다중화) |
| 속도 | 2.5–64 GT/s/lane (Gen1–6) | 10 Mb/s – 400 Gb/s (차량 10M–10G) | 레인당 약 1–9 Gb/s (D-PHY 버전별) |
| 레인/선 | x1–x16, 레인당 TX+RX 2쌍 | 1쌍(T1) – 4쌍(BASE-T), 또는 SerDes | 클럭 1 + 데이터 1–4 (D-PHY), trio (C-PHY) |
| 클럭 | 내장(CDR) + refclk | 내장(CDR) | **별도 클럭 레인** (D-PHY) |
| 에러 검출 | LCRC-32, DLLP CRC-16, ECRC | FCS CRC-32 | 헤더 ECC (SEC-DED), payload CRC-16 |
| 에러 시 | HW 재전송 (ACK/NAK) | drop, 상위층 재전송 | 재전송 없음, 플래그만 |
| 링크 시작 | LTSSM training + equalization | auto-neg / 고정 master-slave | 설정 일치 + LP→HS 시퀀스 |
| 관리 경로 | config space (in-band) | MDIO (별도 2선) | I2C / CCI (별도 2선) |
| Linux 서브시스템 | PCI (`drivers/pci`) | netdev + phylib | V4L2 + media controller |
| 핵심 도구 | `lspci`, `setpci`, sysfs | `ip`, `ethtool`, `iperf3`, `tcpdump` | `media-ctl`, `v4l2-ctl` |
| 핵심 디버그 카운터 | AER (Receiver Err, Bad TLP, Replay) | CRC/FCS err, drops, carrier | ECC/CRC err, frame sync err, drop |
| 대표 검증 | 링크 반복·속도 매트릭스, DMA 처리량, AER, ASPM | loopback(MAC/PHY), PRBS/BER, iperf3/RFC 2544, link 반복 | 테스트 패턴 golden 비교, fps/drop, 멀티 VC, 장시간 |
| Don 경험 | ✅ PCIe 3/4/5/6 (SK hynix 신뢰성, Solidigm Gen6 controller, Apple 루트코즈) | ❌ 실무 없음 — 계층 모델·카운터 기반 디버그는 전이 가능 | ❌ CSI-2 실무 없음 — 🟡 MIPI RFFE/SPMI + I2C 디버그 경험이 다리 |

## 7. 면접에서 이렇게 말하기

원칙: **정직하게 갭 인정 (1문장) → 전이 가능한 경험 (구체) → 이미 한 학습 + 어떻게 빨리 올라갈지**. 변명이 아니라 "검증 방법론은 같다"로 끝내기.

### 7.1 "Have you worked with Ethernet?"

<details><summary>모범 답 (30–45초) + 꼬리 질문</summary>

**English (spoken)**: "Not at the MAC and PHY bring-up level in a product — my high-speed work has been PCIe and the MIPI control buses. But the structure is the same one I debug every day: a PHY that has to establish a link, a link layer with a CRC, and counters that tell you which layer is failing. To prepare, I went through the MAC/PHY split, RGMII and SGMII, MDIO with the Clause 22 control and status registers, and automotive single-pair Ethernet with fixed master/slave, and I looked at ethtool and the link statistics on a Linux box. If I had a new MAC on my bench, I'd start the same way I do with PCIe: confirm the management path, use MAC and PHY loopback to split digital from analog, and then run throughput with the error counters as the pass criterion."

- 한국어 핵심: "제품 레벨 실무는 없다 → 같은 계층·카운터 디버그 → 이미 공부한 범위 → 첫날 할 일".

**꼬리1: "What's the difference between a MAC and a PHY?"**
"The MAC is the digital part in the SoC — it builds frames, adds and checks the FCS, and DMAs to memory. The PHY is the analog part, often a separate chip, that turns bits into line signals, does auto-negotiation and reports link status. They talk over a data interface like RGMII or SGMII, and the SoC manages the PHY over MDIO."
- 한국어: MAC = 디지털 프레임, PHY = 아날로그 신호·link, 사이 = RGMII/SGMII + MDIO.

**꼬리2: "The link is up but no packets get through. What do you check?"**
"Link up means the PHY side is fine, so I'd suspect the MAC-to-PHY interface. The classic one on RGMII is the clock-to-data delay — if nobody or both sides add the 2 ns delay, you get CRC errors or nothing at gigabit, while 100 Mb might still work. I'd check the phy-mode in the device tree, look at the CRC counters in ethtool -S, and use PHY loopback to see if the MAC can receive its own frames through the PHY."
- 한국어: link up + 트래픽 없음 = RGMII delay (phy-mode), CRC 카운터, PHY loopback.

**꼬리3: "How is automotive Ethernet different?"**
"It runs over a single twisted pair — 100BASE-T1 or 1000BASE-T1 — to save weight and cost, and the links are usually configured as fixed master and slave instead of auto-negotiating, so the link comes up fast and deterministically. On top of that, time-sensitive networking, especially 802.1AS gPTP, keeps the clocks across ECUs aligned so sensor data can be timestamped on one timeline."
- 한국어: 단일 페어, master/slave 고정, 빠른 link-up, gPTP 시간 동기.

**꼬리4: "How would you verify PTP time sync?"**
"I'd check that the MAC supports hardware timestamping with ethtool -T, run gPTP between two nodes, and log the reported offset from the master over a long run and across temperature. Independently, I'd have both nodes toggle a pulse-per-second output and measure the skew on a scope, so I'm not just trusting the software's own number." [추정: PPS 출력 지원은 플랫폼마다]
- 한국어: HW 타임스탬프 확인 → offset 로그 장시간 → PPS를 스코프로 독립 측정.

</details>

### 7.2 "Have you worked with MIPI CSI-2?"

<details><summary>모범 답 (30–45초) + 꼬리 질문</summary>

**English (spoken)**: "Not CSI-2 directly — no camera pipelines so far. What I have done at Apple is root-cause interface failures on two other MIPI Alliance buses, RFFE and SPMI, with a protocol analyzer and a scope, so I'm comfortable working from a MIPI spec down to the waveform. And sensors are configured over I2C, which I've brought up and debugged at Solidigm and Apple. For CSI-2 I've studied the D-PHY lanes and HS/LP modes, the short and long packets with the ECC and CRC, virtual channels, and how the receiver feeds the ISP and how Linux exposes it through V4L2 and the media controller. The validation approach I'd use is the same as for any data path: a known test pattern from the sensor, compare every pixel against a golden frame, and watch the error counters and the frame count."

- 한국어 핵심: "CSI-2 직접은 없음 → MIPI RFFE/SPMI 애널라이저 디버그 + I2C → 공부한 범위 → 테스트 패턴 + golden 비교".

**꼬리1: "What's in a CSI-2 long packet?"**
"A 32-bit header with a Data Identifier — the virtual channel and the data type, like RAW10 or YUV422 — then a 16-bit word count and an ECC byte that can correct a single-bit error in the header. Then the payload, which is usually one line of pixels, and a 16-bit CRC over the payload. Frames are delimited by short packets — frame start and frame end."
- 한국어: DI(VC+DT) + WC + ECC / payload(한 줄) / CRC-16, 프레임 경계 = FS/FE short packet.

**꼬리2: "What are virtual channels for?"**
"They let several logical streams share one physical link. In a car, a deserializer can merge four cameras onto one CSI-2 port, each on its own virtual channel, and the receiver routes each VC to its own buffer. You also use the data type to separate the image from embedded metadata from the same sensor."
- 한국어: 한 링크에 여러 스트림 — 카메라 4대 → VC 0–3, DT로 메타데이터 분리.

**꼬리3: "Frames are coming in but every few seconds one is dropped. Where do you look?"**
"First I'd find where it's lost: compare the frame-start count at the CSI-2 receiver with the number of DMA completions, and check the frame numbers in the short packets for gaps. If the receiver sees every frame, it's downstream — DDR bandwidth or the DMA not getting a buffer in time — so I'd look at buffer queue depth and memory traffic from other masters like the NPU. If the receiver itself flags CRC or ECC errors, it's the physical link — lane timing or signal integrity — and I'd lower the data rate to see if it goes away."
- 한국어: RX FS 카운트 vs DMA 완료 비교로 위치 분리 → 하류(DDR/버퍼) vs 물리(CRC/ECC, 속도 낮춰 보기).

**꼬리4: "How do you calculate whether the link has enough bandwidth?"**
"Pixels per second times bits per pixel, plus blanking overhead, against lanes times the per-lane rate. For example, eight megapixels at 30 frames per second at 12 bits is about 2.9 gigabits per second before blanking; four D-PHY lanes at 1.5 gigabits each give 6, so there's margin. In Linux the receiver derives the pixel rate from the link frequency: link frequency times two times the lane count divided by bits per pixel."
- 한국어: 8MP×30×12 ≈ 2.9 Gb/s vs 4×1.5 = 6 Gb/s, Linux 공식 `link_freq×2×lanes/bpp` [2].

</details>

### 7.3 "Compare PCIe and Ethernet."

<details><summary>모범 답 + 꼬리 질문</summary>

**English (spoken)**: "Both are layered serial links with a PHY, a link layer with CRC, and packets, but they solve different problems. PCIe is a load-store interconnect: a device's registers are mapped into the CPU's address space, the device DMAs directly into system memory, and the data link layer retransmits corrupted packets in hardware with ACK and NAK, so software never sees a bit error unless it's fatal. Ethernet is a message network between boxes — frames with MAC addresses go through switches, a bad FCS just means the frame is dropped, and recovery is up to TCP. Management is different too: PCIe uses config space over the same link, Ethernet manages the PHY over a separate MDIO bus. And in practice many Ethernet controllers are themselves PCIe devices."

- 한국어 핵심: 메모리 의미론 + HW 재전송 vs 메시지 + drop / in-band config vs MDIO / NIC는 PCIe 장치.

**꼬리1: "Why does PCIe retransmit in hardware but Ethernet doesn't?"**
"PCIe is a short, controlled link inside a system where a load from the CPU must not silently fail, so each hop keeps a replay buffer and retries in nanoseconds. Ethernet links span many hops and switches, so end-to-end recovery at the transport layer is simpler, and dropping is acceptable for many traffic types."
- 한국어: PCIe = 짧은 홉·CPU load는 실패 불가 → 홉별 replay / Ethernet = 다중 홉 → 종단 간 복구.

**꼬리2: "When would you connect two SoCs with PCIe versus Ethernet?"**
"PCIe when they're on the same board and I need high bandwidth and low latency with shared-memory style DMA — one side runs as an endpoint. Ethernet when they're in different boxes, need to be separated by meters of cable, or need to be part of the vehicle network with switching and time sync."
- 한국어: 같은 보드·고대역·저지연 = PCIe (한쪽 EP 모드) / 다른 박스·거리·망 = Ethernet.

</details>

### 7.4 "How would you validate a CSI-2 receiver on new silicon?"

<details><summary>모범 답 + 꼬리 질문</summary>

**English (spoken)**: "I'd go bottom-up and isolate one layer at a time. First the control path: power, clocks, reset, and read the sensor's chip ID over I2C. Then the cleanest data source I have — a known packet stream from a MIPI pattern generator or the sensor's test pattern — at one lane and a low rate, and I check that frame start and frame end interrupts fire, the word count matches, and the frame in DDR matches the golden pixel for pixel. Then I widen the matrix: lane counts, data rates up to the max, data types like RAW10, RAW12 and YUV, multiple virtual channels, and I inject bad ECC and CRC packets to prove the error detection works. Finally long runs — hours of streaming, stream on/off cycling, temperature — with frame counts, drops and error counters as the pass criteria, all scripted so it produces a coverage report."

- 한국어 핵심: 제어 경로 → 깨끗한 소스(패턴) 1레인 저속 → 매트릭스 확장 + 에러 주입 → 장시간·환경 → 자동화·커버리지.

**꼬리1: "Why use a test pattern instead of a real image?"**
"A real image depends on the lens, lighting and the sensor's own noise, so you can't define an exact expected output. A test pattern is deterministic — every pixel has a known value — so a single bit flip shows up in a byte compare, and I can tell a receiver bug from an image-quality issue."
- 한국어: 실제 영상은 기대값 정의 불가, 패턴은 결정적 → 비트 단위 비교 가능.

**꼬리2: "Lane 3 works at low speed but fails at high speed. What do you suspect?"**
"Signal integrity or skew on that lane — trace length mismatch, a bad via, or the receiver's deskew or termination settings. I'd check the PHY's per-lane error or calibration status, try different equalization or timing settings, swap the lane mapping if the controller allows it, and put a scope or analyzer on it to look at the eye."
- 한국어: 해당 레인 SI·skew → per-lane 상태, 설정 스윕, 레인 매핑 교체, 아이 다이어그램.

**꼬리3: "How does this differ in bare metal versus Linux?"**
"In bare metal I program the D-PHY and CSI-2 controller registers directly and poll or take the interrupt myself, which is best for bring-up and error injection. In Linux the same blocks are V4L2 subdevices described in the device tree, and I'd use media-ctl and v4l2-ctl for system-level and long-run tests. I'd want both: bare metal to prove the IP, Linux to prove the integrated system." → [LP16](2026-10-31_LP16_baremetal_vs_linux_zynq.html)
- 한국어: bare-metal = 레지스터 직접, IP 증명 / Linux = V4L2·DT, 시스템 증명.

</details>

### 7.5 "How would you validate an Ethernet MAC on new silicon?"

<details><summary>모범 답 + 꼬리 질문</summary>

**English (spoken)**: "Same layered approach. Start with the management path: MDIO reads of the PHY ID and status registers. Then MAC internal loopback, which tests the MAC, DMA and descriptor rings with no analog involved — send frames of every size from 64 to 1518 bytes plus jumbo if supported, and compare payloads and check the FCS counters stay at zero. Next PHY loopback through RGMII or SGMII, which proves the interface timing, then an external link to a known-good partner or traffic generator. On that I run throughput and latency per frame size, RFC 2544 style, PRBS on the PHY for bit error rate, thousands of link up/down cycles measuring time to link, and the offload features — VLAN, checksum, and hardware timestamps for PTP. Every test is automated with pass criteria on the counters."

- 한국어 핵심: MDIO → MAC loopback(디지털만) → PHY loopback(인터페이스 타이밍) → 외부 링크 → 프레임 크기별 처리량/지연 + PRBS + link 반복 + 오프로드.

**꼬리1: "Why test small frames specifically?"**
"At 64-byte frames the packet rate is highest — about 1.49 million frames per second at gigabit — so per-packet costs like descriptor fetches and interrupts dominate. A MAC that does line rate with 1500-byte frames can still drop small ones, and that's where DMA and interrupt coalescing bugs show up." [추정: 1G 64 B 라인레이트 ≈ 1.488 Mfps = 1e9 / (84 B × 8)]
- 한국어: 작은 프레임 = 최대 패킷률 → descriptor/인터럽트 병목 노출.

**꼬리2: "What's the pass criterion for a long-run test?"**
"Zero CRC errors and zero unexplained drops over the run, link never flapped, and throughput within a defined margin of the theoretical rate. I'd define the run length from the bit error rate target — to claim 1e-12 with confidence you need on the order of 3e12 bits error-free, which is under an hour at gigabit." [추정: 95% 신뢰 ≈ 3/BER 비트 규칙]
- 한국어: CRC 0, 플랩 0, 처리량 마진; 길이는 BER 목표로 (≈3/BER 비트).

</details>

### 7.6 Don 경험 연결 (어느 질문에든 붙일 문장)

- "At Solidigm I brought up the PCIe and NVMe subsystem IP on FPGA prototypes before RTL freeze, along with I2C, SPI and DMA — that's exactly the bottom-up, isolate-one-block-at-a-time method I'd apply to a new Ethernet MAC or CSI-2 receiver."
- "At Apple I root-cause PCIe, I2C, SPMI and RFFE failures when a new chip meets the full system, using a DSO and a protocol analyzer — so reading a MIPI spec and decoding packets on an analyzer is familiar."
- "At SK hynix I automated chamber reliability testing on high-speed interfaces, including shmoo — the same long-run, temperature and margin testing I'd use for Ethernet BER or CSI-2 stability. [숫자 채우기: 챔버 수 / 테스트 케이스 수]"

## 8. 용어 사전

| 용어 | 풀네임 | 쉬운 설명 | 어느 인터페이스 |
|---|---|---|---|
| SoC | System on Chip | CPU·NPU·메모리 컨트롤러·I/O를 한 칩에 | 공통 |
| PHY | Physical layer (device) | 실제 전기 신호를 만들고 읽는 아날로그 회로 | 공통 |
| MAC | Media Access Control | 프레임/패킷 규칙을 처리하는 디지털 로직 | Ethernet (일반적으로 "컨트롤러") |
| SerDes | Serializer/Deserializer | 병렬 ↔ 고속 직렬 변환기 | PCIe, Ethernet, A-PHY |
| CDR | Clock Data Recovery | 데이터 전환에서 수신 클럭을 뽑아냄 | PCIe, Ethernet |
| 8b/10b, 128b/130b, 64b/66b | line coding | 전환 보장·DC 균형용 부호화, 오버헤드 20% / 1.5% / 3% | PCIe, Ethernet |
| PAM3/PAM4/PAM8 | Pulse Amplitude Modulation | 한 심볼에 3/4/8단계 전압 → 비트 더 실음 | PCIe Gen6, T1 Ethernet |
| FEC | Forward Error Correction | 재전송 없이 에러를 고치는 부호 | PCIe Gen6, 고속 Ethernet |
| CRC | Cyclic Redundancy Check | 데이터 무결성 체크값 (검출만) | 공통 |
| ECC | Error Correcting Code | 검출 + 정정 | CSI-2 헤더, DDR |
| Lane | — | 차선. 차동쌍 1(방향당) 묶음 | PCIe, CSI-2 |
| GT/s | Giga-Transfers per second | 선 위 원시 전송률 (인코딩 전) | PCIe |
| RC / RP | Root Complex / Root Port | CPU 쪽 PCIe 출입구 / 그 각 포트 | PCIe |
| EP | Endpoint | 실제 일하는 PCIe 장치 | PCIe |
| BDF | Bus:Device.Function | PCIe 함수의 "동:호:방" 주소 | PCIe |
| Config space | Configuration space | 장치 명패 + 설정 레지스터 (256 B / 4 KiB) | PCIe |
| BAR | Base Address Register | "내 레지스터 창을 이 크기로 매핑해 달라" 칸 | PCIe |
| TLP | Transaction Layer Packet | 읽기/쓰기/메시지를 담은 PCIe 패킷 | PCIe |
| DLLP | Data Link Layer Packet | ACK/NAK·flow control용 작은 패킷 | PCIe |
| LTSSM | Link Training and Status State Machine | 링크를 올리는 상태 머신 (Detect → L0) | PCIe |
| L0 / DL_Active | — | 정상 동작 상태 / 링크층까지 준비 완료 | PCIe |
| MSI / MSI-X | Message Signaled Interrupt | 메모리 쓰기로 보내는 인터럽트 | PCIe |
| AER | Advanced Error Reporting | PCIe 에러 기록·보고 capability | PCIe |
| ASPM | Active State Power Management | 링크 자동 저전력 (L0s/L1) | PCIe |
| PERST# | PCIe Reset (active low) | 장치를 리셋하는 사이드밴드 핀 | PCIe |
| MMIO | Memory-Mapped I/O | 레지스터를 메모리 주소로 접근 | 공통 |
| DMA | Direct Memory Access | CPU 없이 장치가 메모리 직접 읽기/쓰기 | 공통 |
| IOMMU / SMMU | I/O MMU / System MMU (Arm) | 장치 DMA 주소 번역·보호 | PCIe (공통) |
| MII/RMII/RGMII | (Reduced) (Gigabit) Media Independent Interface | MAC↔PHY 병렬 데이터 인터페이스 | Ethernet |
| SGMII / USXGMII | Serial GMII / Universal Serial 10G MII | MAC↔PHY 직렬 인터페이스 | Ethernet |
| MDIO / MDC | Management Data I/O / Clock | PHY 레지스터 관리 2선 버스 | Ethernet |
| Clause 22 / 45 | IEEE 802.3 조항 | MDIO 주소 방식 (32 레지스터 / MMD + 16-bit) | Ethernet |
| MMD | MDIO Manageable Device | Clause 45의 PHY 하위 블록 번호 | Ethernet |
| BMCR / BMSR | Basic Mode Control / Status Register | PHY 레지스터 0 / 1 (reset, loopback / link, AN 완료) | Ethernet |
| Auto-neg | Auto-negotiation | 속도·duplex 자동 협상 | Ethernet |
| FCS | Frame Check Sequence | 프레임 끝 CRC-32 | Ethernet |
| EtherType | — | payload 종류 번호 (0x0800 IPv4, 0x88F7 PTP) | Ethernet |
| VLAN / 802.1Q | Virtual LAN | 논리 망 분리 + 우선순위 태그 | Ethernet |
| T1 (100BASE-T1 등) | single twisted pair | 차량용 단일 페어 Ethernet | Ethernet |
| Master/Slave (T1) | — | 클럭 주도권을 설정으로 고정 | Ethernet |
| TSN | Time-Sensitive Networking | 시간 보장 Ethernet 표준 묶음 | Ethernet |
| gPTP / 802.1AS | generalized Precision Time Protocol | 네트워크 전체 시계 동기 | Ethernet |
| PRBS / BER | Pseudo-Random Binary Sequence / Bit Error Rate | 테스트용 난수 패턴 / 비트 에러 비율 | 공통 (검증) |
| RFC 2544 | — | 네트워크 장비 벤치마크 방법 (프레임 크기별) | Ethernet |
| MIPI | Mobile Industry Processor Interface (Alliance) | 모바일/차량 부품 인터페이스 규격 협회 | MIPI |
| CSI-2 | Camera Serial Interface 2 | 카메라 → SoC 영상 프로토콜 | MIPI |
| DSI | Display Serial Interface | SoC → 디스플레이 | MIPI |
| D-PHY / C-PHY / A-PHY | — | 클럭레인+차동 / 3선 trio / 차량 장거리 SerDes | MIPI |
| HS / LP | High-Speed / Low-Power mode | D-PHY 고속 차동 데이터 / 저속 상태 신호 | MIPI |
| LP-11 | — | D-PHY 정지 상태 (두 선 모두 high) | MIPI |
| Short / Long packet | — | 프레임·줄 경계 표시 4 B / 픽셀 줄 데이터 | MIPI |
| DI / VC / DT | Data Identifier / Virtual Channel / Data Type | 헤더 첫 바이트: 어느 스트림 / 무슨 형식 | MIPI |
| WC | Word Count | long packet payload 바이트 수 | MIPI |
| RAW10 / RAW12 | — | Bayer 픽셀 10/12 bit (DT 0x2B / 0x2C) | MIPI |
| YUV422 | — | 밝기(Y)+색차(UV), 색차를 가로 절반 샘플 | MIPI |
| CCI | Camera Control Interface | 센서 설정용 I2C 호환 버스 | MIPI |
| RFFE / SPMI | RF Front-End / System Power Management Interface | RF 부품 / PMIC 제어 2선 버스 (Don 경험) | MIPI |
| ISP | Image Signal Processor | RAW → 보기 좋은 영상으로 바꾸는 파이프라인 | MIPI 하류 |
| BLC / AWB / CCM / NR | Black Level Corr. / Auto White Balance / Color Corr. Matrix / Noise Reduction | ISP 단계들 | MIPI 하류 |
| Demosaic | — | Bayer 모자이크 → 픽셀마다 RGB | MIPI 하류 |
| Serializer / Deserializer (GMSL, FPD-Link) | — | 차량 카메라 CSI-2를 수 m 케이블로 연장 | MIPI (차량) |
| V4L2 | Video for Linux 2 | Linux 영상 장치 API | MIPI (Linux) |
| Media controller / subdev | — | 영상 블록 그래프 / 그래프의 중간 블록 | MIPI (Linux) |
| phylib | PHY Abstraction Layer | Linux의 Ethernet PHY 공통 계층 | Ethernet (Linux) |
| Device Tree (DT) | — | 하드웨어 연결을 커널에 알려 주는 기술 파일 | 공통 (Linux) |

## 9. 자주 틀리는 것

1. **"GT/s = Gb/s"** — 아니다. 인코딩 오버헤드를 빼야 함. Gen2 5 GT/s는 4 Gb/s = 500 MB/s/lane.
2. **"PCIe도 네트워크처럼 메시지를 보낸다"** — 소프트웨어 관점에서는 **메모리 읽기/쓰기**. 패킷은 하드웨어가 알아서. 이 관점 차이가 PCIe vs Ethernet 비교 답의 핵심.
3. **"Ethernet link up = 통신 OK"** — link up은 PHY만 의미. RGMII delay 불일치면 link up + CRC 에러/무통신 [1].
4. **"CSI-2도 에러 나면 재전송한다"** — 안 한다. ECC는 헤더 1비트 정정뿐, payload CRC는 검출만. 실시간 영상은 버린다.
5. **D-PHY `link-frequencies` 를 레인 비트율로 착각** — 클럭 주파수(DDR이라 비트율의 절반). 750 MHz → 1.5 Gb/s/lane [2].
6. **MIPI를 하나의 규격으로 생각** — 협회 이름. CSI-2(프로토콜)와 D-PHY(물리층)는 별도 스펙, 같은 CSI-2가 D-PHY/C-PHY/A-PHY 위에 올라감 [6].
7. **차량 T1 Ethernet에서 auto-neg 기대** — 보통 master/slave 고정. 양쪽 다 master면 링크 안 뜸 [추정].
8. **`ethtool -S` 카운터 이름이 표준이라고 가정** — 드라이버마다 다름. 검증 스크립트는 드라이버별 매핑 필요 [추정].
9. **BMSR link status를 한 번만 읽고 판단** — latch-low 비트라 한 번 끊겼으면 첫 읽기는 0. 두 번 읽어 현재 상태 확인.
10. **ISP 문제를 CSI-2 RX 문제로 오인** — 테스트 패턴 + ISP bypass(RAW 그대로 DDR)로 먼저 RX를 분리.

## 10. 출처

1. Linux kernel — PHY Abstraction Layer (phylib, MDIO bus, RGMII delay modes): https://docs.kernel.org/networking/phy.html
2. Linux kernel — Pixel data transmitter and receiver drivers (CSI-2 link frequency, pixel rate formula, LP-11, pre_streamon): https://docs.kernel.org/driver-api/media/tx-rx.html
3. Linux kernel source — include/media/mipi-csi2.h (CSI-2 Data Type values): https://github.com/torvalds/linux/blob/master/include/media/mipi-csi2.h
4. Linux kernel source — include/uapi/linux/mii.h (BMCR/BMSR bits): https://github.com/torvalds/linux/blob/master/include/uapi/linux/mii.h
5. Wikipedia — PCI Express (세대별 전송률·인코딩·대역폭 표): https://en.wikipedia.org/wiki/PCI_Express
6. MIPI Alliance — CSI-2 specification page (v4.2, 32 VC, D/C/A-PHY): https://www.mipi.org/specifications/csi-2
7. MIPI Alliance — D-PHY specification page (v3.0 9 Gb/s, v3.5 embedded clock, ALP): https://www.mipi.org/specifications/d-phy
8. MIPI Alliance — A-PHY specification page (15 m, 2–16/32 Gb/s, IEEE 2977-2021, PAL): https://www.mipi.org/specifications/a-phy
9. Wikipedia — Automotive Ethernet (802.3bp, 802.3ch): https://en.wikipedia.org/wiki/Automotive_Ethernet
10. Charged EVs — Rivian Gen2 electrical architecture (zonal, ECU 17 → 7, 8 MP cameras): https://chargedevs.com/newswire/at-rivians-electrical-hardware-lab-sandy-munro-checks-out-the-new-gen2-architecture/
11. Wikipedia — Camera Serial Interface (CSI-2 버전 이력): https://en.wikipedia.org/wiki/Camera_Serial_Interface
12. Rivian context 파일 §0–§3 (RAP1/ACM3, JD, Don 갭): [rivian_nstaff_silicon_validation_engineer_context](rivian_nstaff_silicon_validation_engineer_context.html)

- 실제 출력: §3.9 `lspci -nn`, §4.8 `ip -br link`, `ip -s link`, `ethtool`, `ethtool -i`, `ethtool -S` — Lima VM `elinux` (Ubuntu aarch64, kernel 7.0.0-38-generic, virtio-net), 2026-10-31 캡처. `media-ctl` 출력과 DT 조각은 [예시].
