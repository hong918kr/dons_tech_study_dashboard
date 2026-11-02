# N07A · 공통 디버그 틀 · 계측 장비 · 마스터 증상 색인
<!-- theme: pastel -->
> **묶음**: N07 인터페이스 디버그 (공통 틀) · **우선**: P0 ★ · **목록**: [N07 인덱스](2026-11-01_N07_interface_debug_scenarios.html) · **이전/다음**: (첫 페이지) / [N07B I2C, SMBus 디버그](2026-11-01_N07B_i2c_smbus_debug.html) · **관련**: [N06H 디버그 방법론](2026-11-01_N06H_debug_methodology_triage.html), [BV11 랩 장비와 측정](2026-11-01_BV11_lab_instruments_measurement.html), [BV13 §2 보드 디버그 방법](2026-11-01_BV13_board_debug_scenarios.html), [LP15 §2 PCIe 디버그 방법](2026-10-31_LP15_debug_scenarios.html), [최종 훑어보기 §5 버스 표](2026-10-30_final_skim.html), [BV12 SI, PI, shmoo](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html), [deck §7 시나리오 16개](2026-10-25_rivian_hm_screen_prep.html) · **코드**: [onsitePrep/n07/a/capture_odds.py](onsitePrep/n07/a/capture_odds.py), [run.sh](onsitePrep/n07/a/run.sh), [실제 출력](onsitePrep/n07/a/run_output.txt)

## 0. TL;DR

- 이 페이지로 답할 것: **"How do you approach debugging an interface you've never seen?"**, "Which instrument do you pick first, and how do you set it up?", "The analyzer decodes fine but the device still fails", "It fails 1 in 10,000 transactions — how do you even catch it?" + **마스터 증상 색인(§3)**: 이 트랙 전체(N07B–G 51개 + BV13 · LP15 · deck · N06H · BV10 · BV09 = **104개 증상**)를 "인터페이스 → 계층" 순서로 한 표에.
- **외울 것 ① 5계층 bottom-up**: **전기 → 타이밍 → 프로토콜 → 설정/드라이버 → 시스템**. 층마다 "OK 증거" 하나씩 쌓는다. 위층처럼 보이는 증상(CRC 에러 · NACK · 프레임 drop)의 원인이 아래층인 경우가 많다.
- **외울 것 ② 첫 5분은 장비가 아니라 카운터**: 컨트롤러 상태 비트 · 에러 카운터(I2C NACK/timeout, UART LSR, `ethtool -S`, CSI-2 RX ECC/CRC, EDAC CE/UE, AER) → 그 숫자가 **어느 층을 볼지** 정해 준다. 그다음 맞는 장비 하나.
- **외울 것 ③ 장비 3규칙**: scope는 **BW ≥ 3 × 0.35/tr** (상승 시간 오차 ~5%) [1][2] + **짧은 접지** · LA는 **신호 속도의 ≥ 4배** 샘플 [6] + 디코드를 믿기 전에 **아날로그로 한 번** · 드문 이벤트는 **persistence(운)로 기다리지 말고 이벤트 자체에 트리거**(pulse width · runt · NACK · FW GPIO) + **segmented memory**로 여러 번 [3][4]. 계산: 1,000 wfm/s scope로 10 ms마다 나는 글리치를 1초 보면 잡을 확률 **2%**(§6 실제 출력).
- **외울 것 ④ 손잡이**: 한 번에 하나 — **속도 ↓ · 레인/폭 ↓ · loopback 단계 · known-good 상대 · swap · V/T · 패턴 · SW 경로(PIO↔DMA, 폴링↔IRQ)**. 실패가 무엇을 따라가는지가 원인 위치다.
- Staff 시그널: "**정상이 어떻게 생겼는지 먼저 캡처한다(golden)**", "**측정 장비도 의심한다**(측정이 신호를 바꾼다)", "**고친 뒤 그 고장을 잡는 테스트를 팜에 넣는다**".
- **30초 영어 답 (headline)**: "For any interface, even one I haven't used, I place the symptom on five layers — electrical, timing, protocol, configuration/driver, system — and I build evidence from the bottom. In the first five minutes I read the controller's status bits and error counters, because they tell me which layer to look at; then I pick the instrument that answers that question — a scope for edges and levels, a logic analyzer for sequence, a protocol analyzer for packets. I cut the problem in half with one knob at a time: lower the speed, loop back at each stage, swap in a known-good partner. And when I find it, the exact failure becomes a regression test on the board farm."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "물이 안 나오는 아파트 수도"

- 수도꼭지에서 물이 안 나온다. 배관공은 수도꼭지부터 뜯지 않는다. ① **본관 밸브가 열려 있나, 수압은 있나**(전기: 전원 · pull-up · 레벨) ② **물이 너무 늦게/띄엄띄엄 오나**(타이밍: 상승 시간 · skew · baud 오차) ③ **집 배관 규격이 맞나 — 같은 나사산인가**(프로토콜: ACK · CRC · 패킷 형식) ④ **이 집 밸브 설정이 맞나**(설정/드라이버: 주소 · 모드 · pinmux · DT) ⑤ **아침 7시에 온 동이 동시에 쓰나**(시스템: 대역폭 경합 · 부하 · 온도).
- 배관공의 도구도 질문마다 다르다. 수압계(DMM) · 내시경 카메라(scope: 물줄기 모양) · 유량 기록계(logic analyzer: 언제 열리고 닫혔나) · 계량기 기록(카운터: 누적 사용량). **"무엇을 묻는가 → 어떤 도구"** 가 이 페이지의 절반이다.
- 그리고 아는 배관공은 **옆집(정상 집)의 수압을 먼저 잰다**. 정상값을 모르면 이상값도 모른다 — golden baseline.

### 1.2 5계층 그림 — 증거를 아래에서 위로

```text
 계층              "이 층 OK" 증거 (하나면 됨)                   여기서 깨지면 위층에 이렇게 보인다
 ───────────────────────────────────────────────────────────────────────────────────────────────────
 5 시스템          장시간 부하·온도에서 드롭/에러 카운터 0          "가끔" · "부하 걸 때만" · "4대 동시면"
 4 설정/드라이버    레지스터 덤프 = 의도한 값, probe OK, dmesg 깨끗   주소 틀림 · 모드 틀림 · "아무 반응 없음"
 3 프로토콜        analyzer 디코드 정상, CRC/ECC/ACK 카운터 0       NACK · CRC 에러 · 패킷 순서/길이 이상
 2 타이밍          setup/hold·skew·baud 오차가 spec 안, margin 있음  "고속에서만" · "고온에서만" · 1비트 밀림
 1 전기            rail·레벨·pull-up·rise time·단락 없음 (DMM/scope)  모든 위층 증상 (그래서 맨 먼저)
 ───────────────────────────────────────────────────────────────────────────────────────────────────
       ▲ 증거를 아래에서 위로 쌓는다         ▼ 증상은 위에서 보이지만 원인은 아래에 있는 경우가 많다
```

- **왜 bottom-up인가**: 전기 층이 틀리면 위 모든 층의 관찰이 오염된다(디코드도, 카운터도). 반대로 전기 OK 증거는 5분이면 얻는다(rail DMM 1회 + 파형 1장). 싸고 확실한 것부터.
- **예외 — top-down으로 시작해도 되는 경우**: 카운터가 이미 층을 가리킬 때(예: `ethtool -S`에서 `rx_crc_errors`만 증가 → 프로토콜에서 보이는 전기/타이밍 문제, MAC 설정 문제는 아님). 그래도 **결론은 아래 층 증거로 닫는다**.

### 1.3 용어 사전

| 용어 | 풀네임 | 쉬운 설명 | 디버그에서 왜 중요 |
|---|---|---|---|
| layer (계층) | — | 고장이 사는 층: 전기·타이밍·프로토콜·설정·시스템 | 층을 먼저 정해야 장비가 정해짐 |
| golden baseline | — | known-good 보드/칩/로그에서 같은 측정을 미리 해 둔 것 | "정상값"을 데이터시트가 아니라 실물로 앎 |
| bisect knob (손잡이) | — | 하나만 바꿔 실패가 따라가는지 보는 변수 | 원인 위치 = 실패가 따라가는 것 |
| loopback | — | 송신을 수신으로 되돌려 자기 자신과 통신 | 구간을 내부/PHY/외부로 자름 |
| known-good partner | — | 상대 장치를 검증된 것으로 교체 | "우리 쪽이냐 상대 쪽이냐" |
| DMM | digital multimeter | 전압·저항·도통을 한 점에서 | 전기 층의 첫 증거 |
| oscilloscope (scope) | — | 전압의 아날로그 모양을 시간축으로 | rise time·ringing·레벨·글리치 |
| MSO | mixed-signal oscilloscope | scope + 디지털 채널 16개 | 아날로그 1–2개 + 버스 디코드 동시 |
| logic analyzer (LA) | — | 많은 선을 임계값 기준 0/1로 길게 | 순서·핸드셰이크·상태 머신 |
| protocol analyzer | — | 링크 계층 패킷을 디코드(PCIe/USB/Ethernet tap, I2C/SPI 전용기) | 와이어에 실제로 무엇이 나갔나 |
| ILA | integrated logic analyzer | FPGA/SoC 내부 신호를 칩 안에서 캡처 | 핀으로 안 나오는 신호 |
| BERT | bit error rate tester | 패턴 생성 + 수신 비교로 BER·지터 허용치 | 고속 SerDes 마진 |
| TDR / VNA | time-domain reflectometry / vector network analyzer | 선로 임피던스 불연속 위치 / 주파수 특성 | 커넥터·via·끊김 위치 (보통 SI팀) |
| bandwidth (BW) | — | scope가 -3 dB로 통과시키는 최고 주파수 | `tr_scope ≈ 0.35/BW` [2] |
| rise time (tr) | — | 10→90% (I2C는 30→70%) 걸리는 시간 | 필요한 BW를 정함 |
| dead time | blind time | scope가 처리하느라 못 보는 시간 | 드문 글리치를 놓치는 이유 [3] |
| persistence | infinite persistence | 여러 캡처를 화면에 겹쳐 쌓음 | 가끔 다른 모양을 눈으로 — 단 dead time 운 |
| segmented memory | sequential single-shot | 메모리를 조각내 트리거마다 한 조각, 사이 idle은 버림 | 드문 버스트 수천 개를 시간 태그와 함께 [4] |
| timing mode / state mode | asynchronous / synchronous sampling | LA 내부 클럭으로 샘플 / DUT 클럭 엣지로 샘플 | 타이밍 측정 vs 버스 내용 [7] |
| trigger | — | 캡처 시작 조건 (edge·pulse width·runt·timeout·setup/hold·serial) | "드문 이벤트"를 확률 1로 만듦 |
| W1C | write-1-to-clear | 1을 써야 지워지는 상태 비트 | 카운터/상태 읽고 지우는 순서 실수 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 인터페이스 디버그 5계층 모델 — 계층 표 + 인터페이스 × 계층 매트릭스

**계층 표** (모든 인터페이스 공통):

| 계층 | OK 증거 | 대표 실패 | 측정 도구 | 이 층이면 보통 보이는 "냄새" |
|---|---|---|---|---|
| 1 전기 | rail 전압 spec 안, 신호 레벨 VIH/VIL 만족, 단락/개방 없음, pull-up 값 맞음 | 전원 미인가, back-power, 레벨 불일치(1.8 V vs 3.3 V), 단락, cold joint | DMM, scope(DC 레벨·ramp), 회로도 | **항상** 실패 또는 보드별 차이, 손대면 바뀜 |
| 2 타이밍 | rise time·setup/hold·skew·baud 오차 spec 안, training margin 충분 | 느린 RC edge, RGMII 지연 중복, DQS skew, baud 오차 누적, eye 닫힘 | scope(측정), LA timing mode, training/eye 로그, shmoo | **속도·온도·전압 의존**, "고속에서만" |
| 3 프로토콜 | analyzer 디코드 정상, ACK/CRC/ECC 에러 0, 패킷 순서·길이 맞음 | NACK, CRC/FCS/ECC 에러, FS/FE 짝 깨짐, repeated START 처리 오류 | protocol analyzer, scope serial decode, 에러 카운터 | 에러 **카운터가 증가**, 특정 패턴/길이에서 |
| 4 설정/드라이버 | 레지스터 덤프 = 의도값, DT/pinmux 맞음, probe 성공, dmesg 깨끗 | 7/8-bit 주소 혼동, CPOL/CPHA, lane 수 불일치, pinmux 충돌, 클럭 gate | 레지스터 덤프, dmesg, Linux 도구(i2cdetect, ethtool, media-ctl) | **결정적**(매번 같음), 빌드·설정 바꾸면 바뀜 |
| 5 시스템 | 장시간·최대 부하·온도 코너에서 드롭/에러 0, 대역폭 여유 | 메모리 대역폭 경합, IRQ latency, 버퍼 부족, 동시성 race, thermal throttling | perf/드롭 카운터, 장시간 로그, 트레이스, 온도 로그 | **"가끔"**, 부하·동시성·시간 의존 |

**인터페이스 × 계층 매트릭스** (칸 = "이 층 OK 증거 → 대표 실패"; 상세는 각 페이지 §2–§4):

| 인터페이스 | 1 전기 | 2 타이밍 | 3 프로토콜 | 4 설정/드라이버 | 5 시스템 |
|---|---|---|---|---|---|
| **I2C** ([N07B](2026-11-01_N07B_i2c_smbus_debug.html)) | idle SCL/SDA = VDD, pull-up 값 → back-power, 레벨 시프터 | tr ≤ 300 ns @400k [8] → RC 느림 | 9번째 클럭 ACK → NACK, stuck SDA | 7-bit 주소, 속도 설정 → 8-bit 표기 혼동 | 다중 master·센서 stretching → timeout |
| **SPI/QSPI** ([N07C](2026-11-01_N07C_spi_qspi_debug.html)) | flash VCC·CS idle high → MISO floating | 샘플 지연·trace 길이 → 고속만 깨짐 | JEDEC ID 정상 → 명령 리셋(CS 해제) | CPOL/CPHA, QE·4-byte 모드 → 1비트 밀림 | DMA·캐시 → DMA만 깨짐 |
| **UART** ([N07D](2026-11-01_N07D_uart_gpio_irq_debug.html)) | 레벨(3.3/1.8 V, RS-232) → 출력 없음 | 비트 폭 = 1/baud → 오차 누적 garbage | LSR FE/PE 0 → framing error | pinmux·console= → Linux 진입 후 사라짐 | FIFO overrun·IRQ latency → 고속에서 글자 빠짐 |
| **GPIO/IRQ** ([N07D](2026-11-01_N07D_uart_gpio_irq_debug.html)) | 레벨·drive·open-drain → 출력 이상 | 엣지 폭·bounce → 두 번 들어옴 | (해당 적음) | edge/level·W1C 순서 → 누락·storm | suspend 도메인 → wake 실패 |
| **DDR** ([N07E](2026-11-01_N07E_ddr_memory_debug.html)) | VDDQ·VPP·RESET_n·CKE → training 초반 실패 | training eye margin → lane 간헐, 고온 CE | ECC CE/UE 0 → CE 증가·UE 패닉 | 파트·rank·swizzle 설정 → 주소 aliasing | NPU+카메라 동시 부하 → 비트 에러 |
| **Ethernet** ([N07F](2026-11-01_N07F_ethernet_debug.html)) | PHY 전원·리셋·strap → MDIO 0xFFFF | RGMII 2 ns 지연 → link up, 트래픽 0 | FCS/CRC 카운터 0 → FCS 증가 | speed/duplex·T1 master/slave → 링크 안 뜸 | pps 한계·ring 크기 → 64 B 반토막, RX drop |
| **CSI-2** ([N07G](2026-11-01_N07G_mipi_csi2_camera_debug.html)) | 센서 전원·MCLK·reset → 프레임 0 | HS settle·lane skew → SoT 에러 | ECC/CRC 0, FS/FE 짝 → 찢어짐·합쳐짐 | lane 수·data type·VC → 색 이상, VC 섞임 | 메모리 대역폭·버퍼 → 다중 카메라 drop |
| **PCIe** ([LP15](2026-10-31_LP15_debug_scenarios.html)) | PERST#·refclk·rail → lspci 없음 | EQ·SI → Gen 다운, AER correctable | DLActive·TLP → completion timeout | BAR·MSI·IOMMU → probe 실패, MSI 안 옴 | MPS/MRRS·ASPM → throughput 낮음 |

- 매트릭스를 화이트보드에 **I2C 한 줄만** 그려도 면접관은 틀을 알아본다. 질문받은 인터페이스의 한 줄을 그리고 "I'll go left to right" 라고 말하면 된다.

### 2.2 N06H 디버그 6단계를 인터페이스에 적용

방법론 자체(종료 기준 · 통계 · 리포트)는 [N06H §2.1](2026-11-01_N06H_debug_methodology_triage.html)에 있다. 여기선 **"인터페이스 문제라면 각 단계에서 구체적으로 무엇을 하나"** 만.

| 단계 | 인터페이스면 구체적으로 | 예 (I2C) | 예 (Ethernet) | 예 (CSI-2) |
|---|---|---|---|---|
| ① Reproduce | 트랜잭션/프레임 루프 + **에러 카운터를 루프 전후로 스냅샷**, 실패율을 "트랜잭션당"으로 | `i2cget` 10만 회, NACK k/N | iperf 10분, `ethtool -S` 전후 diff | 1만 프레임 스트리밍, RX 에러 카운터 diff |
| ② Localize | 손잡이 하나씩(§2.3): 속도·레인·loopback·상대 장치·보드 | 100k에선 0 → 400k에서만 | MAC loopback OK, PHY loopback FAIL | 1-lane 저속에선 OK |
| ③ Hypothesize | 층별로 예측이 다른 가설 2–3개 | rise time(전기) vs stretching(프로토콜) | RGMII 지연(타이밍) vs 케이블(전기) | HS settle(타이밍) vs 메모리 대역폭(시스템) |
| ④ Instrument | **에러 비트/카운터 증가 순간에 FW가 GPIO 토글 → scope/LA 트리거**, 수신단에서 측정 | NACK 트리거 + SDA rise time | TX_CLK vs TXD skew 측정 | 센서 FSYNC + RX 에러 IRQ GPIO |
| ⑤ Fix | 근본 원인 고침, workaround면 문서화, **같은 루프·같은 조건**으로 통계 확인 | pull-up 4.7k→2.2k [예시] | DT `phy-mode` 수정 | `hs-settle` 값 보정 |
| ⑥ Regress | 그 고장을 잡는 테스트를 팜에 — **고친 걸 빼면 실패하는지**까지 | 400k NACK 루프 nightly | loopback 사다리 + FCS 0 판정 | N대 동시 스트리밍 1시간, drop 0 |

**인터페이스 버전 영어 문장** (N06H 문장과 다르게, "카운터·구간" 중심):

1. "I start by turning 'it fails sometimes' into a per-transaction failure rate, with the error counters snapshotted before and after each run."
2. "Then I cut the path: lower the speed, loop back at each stage, swap in a known-good partner — whichever half keeps failing is where I look next."
3. "My hypotheses sit on different layers so one measurement separates them — slow edges get worse with more capacitance, a firmware race doesn't care."
4. "To catch the bad moment I have firmware toggle a GPIO the instant the error bit sets, and I trigger the scope on that GPIO with pre-trigger, measuring at the receiver."
5. "After the fix I rerun the same loop long enough to bound the failure rate, and I check the regression test fails if I revert the fix."

### 2.3 "구간 자르기" 손잡이 표 (인터페이스별)

한 번에 하나. 결과가 **바뀌면 그 손잡이 근처**, 안 바뀌면 그 손잡이는 배제.

| 손잡이 | I2C | SPI/QSPI | UART | DDR | Ethernet | CSI-2 | PCIe |
|---|---|---|---|---|---|---|---|
| 속도 낮추기 | 1M → 400k → 100k | 100 → 25 MHz | 3M → 115200 | 최저 speed bin | 1000 → 100 Mb/s 강제 | lane rate ↓ (센서 PLL) | Target Link Speed Gen1 |
| 폭/레인 줄이기 | — | quad → single (1-1-1) | — | 채널·rank 하나만 | — | 4-lane → 2/1-lane | x4 → x1 |
| loopback 단계 | 컨트롤러 자체 테스트 모드 [추정] | MOSI–MISO 점퍼 (`spidev_test`) | 내부 loopback (16550 MCR loop) | — (BIST/MPR 패턴) | MAC → PCS → PHY → 외부 | 센서 test pattern / RX 내부 패턴 [추정] | PHY/digital loopback |
| known-good 상대 | 다른 센서, EEPROM | 다른 flash 파트 | USB-UART 어댑터 교체 | 다른 DRAM 파트(소켓/보드) | 링크 상대 스위치/PHY | 검증된 카메라 모듈 | 검증된 endpoint 카드 |
| 보드/칩 swap | 보드 swap | 보드 swap | 보드 swap | 칩 swap, 보드 swap | 보드·케이블 swap | 케이블·SerDes·보드 | 슬롯·카드·보드 |
| 전압/온도 | VDD ±, 챔버 | VCC ±, 챔버 | 챔버(RC osc) | VDDQ margin, 챔버 | 챔버, EMI | 챔버(SerDes) | 챔버, rail margin |
| 패턴 | 레지스터별, 길이별 | 0x00/0xFF/0x55/랜덤 | 0x55('U')·0x00·break | walking 1/0, March, 랜덤 | PRBS, 64 B vs 1518 B | test pattern(color bar) | PRBS / 전송 크기 |
| SW 경로 | 폴링 ↔ IRQ, 커널 ↔ bare-metal | PIO ↔ DMA | 폴링 ↔ DMA, FIFO threshold | 캐시 on/off, CPU ↔ DMA | NAPI·coalescing on/off | 버퍼 수, ISP bypass | IOMMU passthrough, ASPM off |

- 해석 규칙: **속도 ↓로 사라지면 → 타이밍/SI(전기)**, 안 사라지면 → 설정/프로토콜. **SW 경로를 바꿔 사라지면 → 설정/드라이버/시스템**. **상대 장치를 바꿔 사라지면 → 상대(또는 상호 호환)**. **보드를 따라가면 → 보드 전기**, 칩을 따라가면 → silicon (판단표는 [BV13 §2.3](2026-11-01_BV13_board_debug_scenarios.html), [N06H §2.2](2026-11-01_N06H_debug_methodology_triage.html)).
- 함정: 속도를 낮추면 **타이밍 문제도 가려지지만 시스템 문제(대역폭)도 가려진다**. "저속에서 OK"만으로 SI라고 단정하지 말 것 — 저속 + 같은 부하(더미 트래픽)로 한 번 더.

### 2.4 계측 장비 선택표 — "무엇을 묻는가 → 어떤 장비"

BV11 §1.3이 보드 전원 관점의 "질문 → 장비" 표다. 여기선 **인터페이스 디버그 질문**으로.

| 묻는 질문 | 첫 장비 | 인터페이스 예 | 한계 · 주의 |
|---|---|---|---|
| 전원이 들어가나, 단락인가, pull-up 값은? | **DMM** (전원 끄고 저항, 켜고 전압) | I2C idle 레벨, PHY rail, flash VCC | 동적 현상(droop·글리치)은 못 봄 |
| 엣지 모양·레벨·ringing·글리치는? | **scope** (+ 짧은 접지) | I2C rise time, UART 비트 폭, RGMII skew | 채널 2–8개, 메모리 한계, BW 한계 |
| 여러 선의 순서·핸드셰이크는? | **logic analyzer** (timing mode) | SPI CS→SCLK, GPIO IRQ vs ack, reset 순서 | 아날로그 마진 안 보임(임계 근처 오판) |
| 버스 내용(주소·데이터·ACK)은? | **scope serial decode** 또는 **LA decode** | I2C 주소+NACK, SPI 명령, UART 바이트 | 디코더는 이상한 아날로그도 "정상"으로 그릴 수 있음 |
| 고속 링크의 패킷·상태 머신은? | **protocol analyzer** (interposer · tap · mirror) | PCIe TLP·LTSSM, Ethernet 프레임 | interposer가 링크 마진을 바꿀 수 있음 [추정] |
| 칩 안에서 무슨 일이? | **내장 카운터 · 상태 레지스터 · ILA · trace** | CSI-2 RX 에러 상태, `ethtool -S`, EDAC, AER | 카운터 의미는 IP 문서 확인, clear-on-read 여부 |
| 비트 에러율·지터 마진은? | **BERT / 내장 PRBS** | SerDes, Ethernet PHY, PCIe | BER 1e-12 확인에 ~3e12 bit (BV10 §2.5.4) |
| 선로 어디가 끊겼나·임피던스는? | **TDR / VNA** (보통 SI팀) | 케이블·커넥터·via, T1 하네스 | 장비·전문성 — 데이터만 넘김 |
| 온도/환경 의존인가? | **thermal chamber · thermal stream(국부)** | 고온 NACK, 고온 ECC CE, SerDes lock | 온도 램프 중 결로·케이블 영향 |
| 전류·전력 변화가 원인인가? | **current probe · power analyzer** | 부하 시 droop로 링크 drop | BV11 §2.7 |

- 순서의 원칙: **싼 것 → 비싼 것, 비침습 → 침습**. 카운터(0분) → DMM(1분) → scope(5분) → LA/analyzer(15분) → BERT/TDR(다른 팀).
- 면접 문장: "**The counters tell me which layer; the instrument proves it.**"

### 2.5 Scope로 디버그할 때 실전 요령

**① 대역폭 — 0.35/tr 규칙** [1][2]

- Gaussian 응답 scope: `tr_scope ≈ 0.35 / BW`. 측정된 tr ≈ √(tr_신호² + tr_scope² + tr_probe²). **장비 tr을 신호 tr의 1/3로 → 오차 ~5%, 1/5 → ~2%** [1]. 즉 필요한 BW ≈ **3–5 × 0.35 / tr_신호**. (표 계산은 BV11 §2.4.1)
- 인터페이스별 감 (§6 실제 계산; I2C만 spec 수치, 나머지 tr은 [추정]):

| 인터페이스 | 신호 tr | 필요 BW (×3) | 결론 |
|---|---|---|---|
| I2C Fast-mode 400 kHz | ≤ 300 ns (spec 상한 [8]) | ~3.5 MHz | 어떤 scope든 OK — 문제는 BW가 아니라 **프로브 C 부하**(10× 써라) |
| UART 3 Mbaud | ~10 ns [추정] | ~105 MHz | 일반 200 MHz scope OK |
| SPI 50 MHz | ~2 ns [추정] | ~525 MHz | 500 MHz–1 GHz scope + 짧은 접지 |
| RGMII (125 MHz DDR) | ~0.75 ns [추정] | ~1.4 GHz | 1.5–2 GHz + active probe; 2 ns skew 측정은 deskew 필수 |
| D-PHY HS / PCIe / SGMII | 100 ps 수준 [추정] | 10 GHz+ | 일반 랩 scope로 eye 측정 불가 → **RX 에러 카운터·PHY 내장 eye scan·BERT** |

- 고속 차동 링크는 "scope로 보면 된다"가 **틀린 답**이다. 보는 건 PHY 내장 진단·카운터, 측정은 SI팀의 고속 scope + fixture.

**② 프로브와 접지** [1]

- 접지선 = 인덕터. 6인치 악어 클립 + ~11 pF ≈ **140 MHz에서 ringing** [1] → **화면의 ringing이 신호인지 프로브인지 모른다**. 자가 진단: 프로브 케이블을 움직여 파형이 변하면 접지 문제 [1].
- **10× vs 1×**: 10× = 10 MΩ ‖ ~10 pF, BW 수백 MHz. 1×는 BW 낮고 C 부하 큼 — **I2C처럼 RC가 느린 신호에 1× 프로브를 대면 측정이 rise time을 더 늦춘다**(측정이 증상을 만든다). ripple처럼 mV 신호만 1× 고려.
- **어디서 재나 — 수신단 가까이**: 송신단 파형은 깨끗해도 수신단에서 반사·감쇠로 깨진다. 판정은 **수신 핀(또는 그 바로 옆 via/테스트 포인트)** 기준. 다중 slave I2C면 **가장 먼 slave**.

**③ 트리거 종류 — 인터페이스별 쓰임**

| 트리거 | 무엇을 잡나 | 인터페이스 쓰임 |
|---|---|---|
| edge | 레벨·기울기 넘는 순간 | CS falling, reset 해제, FW GPIO |
| pulse width | N보다 짧은/긴 펄스 | 글리치(I2C SCL 위 수 ns 스파이크), UART 비트 폭 이상 |
| runt | 한 임계는 넘고 다른 임계는 못 넘은 불완전 스윙 | 경합(MISO 두 slave), 약한 드라이버 |
| timeout / pattern duration | N 동안 변화 없음 | I2C SCL low stuck(stretching 끝없음), 버스 멈춤 |
| setup/hold violation | 클럭 대비 데이터가 너무 가까움 | SPI MISO 샘플 지연, RGMII 데이터 vs 클럭 |
| serial decode | 디코드된 내용(주소 0x48 + NACK, 특정 바이트) | "주소 X에서만 NACK" 순간만 캡처 |
| FW GPIO (edge on aux 채널) | SW가 에러 감지한 순간 | **카운터/상태 비트 증가 순간 = 원인 몇 µs 전** (pre-trigger) |

**④ 드문 이벤트 — persistence vs segmented memory vs 이벤트 트리거** [3][4][5]

- **dead time**: scope는 한 캡처를 처리하는 동안 못 본다. `%DT = 100 × (1 − U·W)` (U = 초당 파형 수, W = 화면 시간 = timebase × 10) [3]. 1,000 wfm/s에 20 ns/div면 W = 200 ns → **99.98%를 못 본다**(§6).
- **persistence로 기다리기 = 주사위**: 이벤트 주기 / W = S면 N번 캡처 후 볼 확률 `1 − (1 − 1/S)^N` [3]. §6 실제 출력: 10 ms마다 나는 글리치를 1,000 wfm/s로 1초 보면 **1.98%**, I2C 글리치(10초에 1번)를 100 µs/div·50 wfm/s로 1분 보면 **26%**, 10분이면 95%.
- **이벤트 자체에 트리거**(pulse width · runt · NACK decode · FW GPIO): 이벤트가 날 때만 캡처 → 놓치는 건 re-arm 중에 또 날 때뿐(≈ re-arm/주기, §6에서 2.5e-7). **"Don't wait for luck — trigger on the anomaly."**
- **segmented memory**: 메모리를 N 조각으로 나눠 트리거마다 한 조각, **사이의 idle은 저장 안 함** + 조각마다 시간 태그 [4]. 예: 400 kHz I2C 트랜잭션 1,000개(10 ms 간격) — 연속 기록이면 100 MS/s × 10 s = 1,000 Mpts, segmented면 100 µs × 1,000 = **10 Mpts (100배 절약)** (§6). 시간 태그로 **"실패 트랜잭션 사이 간격"**(주기적이면 다른 소스와 상관) 분석, serial decode는 조각마다 적용 [5].
- 언제 무엇: 모양이 가끔 다른지 **눈으로 훑기** → persistence(빠른 update rate scope일 때만 의미) · **특정 이상을 확정** → 이벤트 트리거 single · **여러 건의 통계·간격** → segmented + 시간 태그 · **카운터만 오르고 파형 위치 모름** → FW GPIO 트리거.

### 2.6 Logic analyzer vs protocol analyzer

| 항목 | Logic analyzer | Protocol analyzer |
|---|---|---|
| 보는 층 | 0/1 파형 (타이밍) + 저속 디코드 | 링크/트랜잭션 계층 패킷 |
| 연결 | 핀/테스트 포인트에 flying lead | interposer, tap, mirror port, 전용 프로브 |
| 강점 | 많은 선의 순서, 핸드셰이크, 저속 버스 내용 | 고속 패킷 내용, 재전송, 상태 머신(LTSSM), 필터·트리거 on 패킷 |
| 약점 | 아날로그 마진 안 보임, 고속 링크 불가 | 전기 품질 안 보임, 비쌈, 삽입이 링크를 바꿀 수 있음 |
| 이 트랙 쓰임 | I2C/SPI/UART/GPIO, reset 순서, CS 타이밍 | PCIe(LP15), Ethernet(tap+Wireshark), USB, CAN |

- **샘플링 레이트 규칙**: 디지털 신호는 **대역폭(또는 클럭)의 최소 4배**로 샘플 [6] (Saleae: "SPI는 클럭의 최소 4배"). 4배는 **디코드 최소치**이고, **타이밍을 재려면 더 빨리** — LA 측정 불확실성은 엣지마다 ±1 샘플. §6: 25 MS/s(40 ns 간격)로 I2C Fm tSU;DAT(최소 100 ns [8])를 재면 **±40%**, 500 MS/s면 ±2%. SPI 50 MHz를 100 MS/s로 받으면 2배라 **디코드조차 믿을 수 없음**.
- **timing mode vs state mode** [7]: timing = LA 자기 클럭으로 비동기 샘플(엣지 간격·글리치·순서), state = **DUT 클럭 엣지에서** 동기 샘플(그 클럭 기준 "칩이 본 값"; 병렬 버스·상태 머신). SPI를 state mode로 SCLK 기준 받으면 "slave가 본 비트"를 정확히 재현 — 단 샘플 엣지(CPHA)를 맞게 골라야 한다.
- **디코드를 믿기 전에 아날로그 한 번**: 디코더는 임계값 하나로 0/1을 정한다. rise time이 느려 임계 근처를 오래 지나면(I2C RC) **LA는 깨끗하게 그리는데 실제 slave는 다르게 볼 수 있다**. "Analyzer decodes fine but device fails" = 첫 의심은 **analyzer 임계값 ≠ 장치 VIH/VIL**, 프로브 위치(송신단), 그리고 프로토콜 너머(장치 내부 상태·설정) — §4 체인 1.

### 2.7 "정상 기준선(golden)" 원칙

- **정상이 어떻게 생겼는지 모르면 비정상도 모른다.** bring-up 초기에 known-good 보드(또는 EVK)에서 같은 측정을 해 두고, 이후 모든 판단은 **diff**로.
- 인터페이스별로 캡처해 둘 것:

| 인터페이스 | golden에 남길 것 |
|---|---|
| I2C | `i2cdetect` 주소 맵, SCL/SDA rise time(가장 먼 slave), 대표 트랜잭션 디코드 1장 |
| SPI | JEDEC ID·SFDP 덤프, 최고 속도에서 CS/SCLK/MISO 캡처 1장, read delay 보정값 |
| UART | 실측 비트 폭(baud), `/proc/tty/driver/serial` 카운터 0 상태 |
| DDR | training 로그(lane별 margin), EDAC CE/UE 0, memtester 결과, 온도별 margin |
| Ethernet | `ethtool`·`ethtool -S` 스냅샷, PHY ID·BMSR, (T1) SQI 값, iperf 기준치 |
| CSI-2 | `media-ctl -p` 토폴로지, RX 에러 카운터 0, 프레임 간격·타임스탬프 분포 |
| PCIe | `lspci -vvv` LnkSta, AER 카운터, LTSSM 상태 ([LP15 §3.1](2026-10-31_LP15_debug_scenarios.html)) |

- golden도 **버전 관리**: 보드 rev · FW 빌드 · 온도 · 장비 설정(BW limit, 프로브 종류)을 같이 기록. "같은 양을 쟀는데 장비마다 다름"은 [N06H N4](2026-11-01_N06H_debug_methodology_triage.html).

### 2.8 카운터·상태 레지스터 먼저 — 인터페이스별 "첫 5분 증거 스냅샷"

| 인터페이스 | 첫 5분에 읽을 것 (순서대로) | 이 숫자가 말해 주는 층 | 상세 |
|---|---|---|---|
| I2C | dmesg의 -ENXIO/-EREMOTEIO/timeout · `i2cdetect -y N` 주소 맵 · 컨트롤러 상태(NACK · arbitration lost · timeout 비트, 개념명) · SCL/SDA idle 레벨 | 주소 없음=설정/전원, arb lost=전기 글리치, SDA low=프로토콜 stuck | [N07B §3](2026-11-01_N07B_i2c_smbus_debug.html) |
| SPI | dmesg spi-nor JEDEC ID 줄 · `/proc/mtd` · 컨트롤러 FIFO under/overrun · DMA 완료 상태 | ID 0xFFFFFF=전기/CS, ID 맞고 데이터 틀림=타이밍/모드 | [N07C §3](2026-11-01_N07C_spi_qspi_debug.html) |
| UART | 16550 계열 LSR(OE·PE·FE·BI 비트) · `/proc/tty/driver/serial` fe/pe/oe/brk 카운트 · `stty -a` | FE=baud/타이밍, OE=시스템(서비스 지연), 무출력=설정/전기 | [N07D §3](2026-11-01_N07D_uart_gpio_irq_debug.html) |
| GPIO/IRQ | `/proc/interrupts` 카운트 변화 · `gpioinfo` 방향/소비자 · IRQ 상태/pending 비트 | 카운트 0=설정/전기, 폭주=level 미해제 | [N07D §3](2026-11-01_N07D_uart_gpio_irq_debug.html) |
| DDR | 부트로더 training 로그(실패 단계·lane) · EDAC `ce_count`/`ue_count` · ECC 에러 주소 | 특정 lane=타이밍/전기, 한 주소 반복=하드 셀, 퍼짐=마진 | [N07E §3](2026-11-01_N07E_ddr_memory_debug.html) |
| Ethernet | `ethtool` (Link detected, speed) · `ethtool -S` diff · `ip -s link` · MDIO PHY ID·BMSR | MDIO 0xFFFF=전기/strap, CRC 증가=전기/타이밍, drop=시스템 | [N07F §3](2026-11-01_N07F_ethernet_debug.html) |
| CSI-2 | RX 에러 상태(SoT · ECC 1/2-bit · CRC · frame sync · data type · VC, 개념명) · 프레임/라인 카운터 · `media-ctl -p` · `v4l2-ctl` fps | SoT=타이밍, CRC=전기/타이밍, 카운터 정상인데 drop=시스템 | [N07G §3](2026-11-01_N07G_mipi_csi2_camera_debug.html) |
| PCIe | `lspci -vvv` LnkSta/DevSta · AER · dmesg | LP15 §2.1 층별 OK 증거 | [LP15 §3.1](2026-10-31_LP15_debug_scenarios.html) |

```bash
# [미실행] 첫 5분 스냅샷 — 보드에 붙어서 그대로 (인터페이스 번호·이름은 보드에 맞게)
dmesg -T > snap_dmesg.txt                                   # 모든 인터페이스: probe 실패·timeout·에러 문자열
cat /proc/interrupts > snap_irq_t0.txt                      # IRQ 카운트 기준점 (나중에 diff)
i2cdetect -l; i2cdetect -y 1                                 # I2C 버스 목록, 버스 1 주소 맵 (-y: 확인 질문 생략)
cat /proc/mtd; dmesg | grep -i spi-nor                       # SPI NOR 인식·JEDEC ID
cat /proc/tty/driver/serial                                  # 8250/16550 계열: tx/rx/fe/pe/brk/oe 카운트 (root)
grep . /sys/devices/system/edac/mc/mc*/ce_count /sys/devices/system/edac/mc/mc*/ue_count   # ECC CE/UE
ethtool eth0; ethtool -S eth0 > snap_eth_t0.txt; ip -s link show eth0                       # 링크·카운터
media-ctl -p -d /dev/media0; v4l2-ctl -d /dev/video0 --list-formats-ext                     # 카메라 토폴로지·포맷
lspci -vvv > snap_lspci.txt                                  # PCIe 링크 상태·AER
# 같은 명령을 부하/실패 후 다시 → diff 가 "무엇이 증가했나" = 어느 층
```

- 핵심 습관: **카운터는 "값"이 아니라 "증가량"** — 반드시 전/후 두 번 읽고 diff. clear-on-read · W1C인지 IP 문서로 확인(읽는 순간 지워 버리면 증거 소실).

## 3. 마스터 증상 색인 ★

- 이 트랙과 기존 노트의 **모든 디버그 시나리오 104개**를 인터페이스 → 계층(전기 → 타이밍 → 프로토콜 → 설정/드라이버 → 시스템) 순으로. **계층 = 가장 흔한 root cause 층 (작성자 판단 [추정])** — 실제 답에선 아래 층부터 배제한다.
- "어디" = 페이지 + 시나리오 ID. N07B–G ID는 [N07 인덱스 §2](2026-11-01_N07_interface_debug_scenarios.html) 순서(①=1). 기존 시나리오가 N07에서 확장되면 "→" 로 표시.
- 연습법 ([인덱스 §4](2026-11-01_N07_interface_debug_scenarios.html) 1일차): 증상 칸만 보고 **계층 + 첫 확인 1개**를 소리 내어 말한 뒤 계층 칸과 비교.

| # | 증상 문장 | 인터페이스 | 계층 | 어디 |
|---|---|---|---|---|
| 1 | 카메라 모듈 전원이 꺼져 있을 때 I2C 버스 전체가 다운 | I2C | 전기 | [N07B I2C](2026-11-01_N07B_i2c_smbus_debug.html) B6 |
| 2 | 단일 master인데 arbitration lost | I2C | 전기 | [N07B I2C](2026-11-01_N07B_i2c_smbus_debug.html) B5 |
| 3 | 고온 chamber에서만 NACK 증가 | I2C | 전기 | [N07B I2C](2026-11-01_N07B_i2c_smbus_debug.html) B8 |
| 4 | 400 kHz에서만 간헐 실패 — rise time·pull-up | I2C | 타이밍 | [N07B I2C](2026-11-01_N07B_i2c_smbus_debug.html) B3 |
| 5 | reset 후 SDA가 low로 stuck (9-clock 복구) | I2C | 프로토콜 | [N07B I2C](2026-11-01_N07B_i2c_smbus_debug.html) B2 |
| 6 | I2C bus is stuck low. What do you do? | I2C | 프로토콜 | [BV10 보드 인터페이스](2026-11-01_BV10_board_interface_tests.html) 체인 B → N07B B2 |
| 7 | 레지스터를 읽으면 매번 다른 레지스터 값 — repeated START vs STOP+START | I2C | 프로토콜 | [N07B I2C](2026-11-01_N07B_i2c_smbus_debug.html) B4 |
| 8 | 센서가 가끔 clock stretching을 길게 → master timeout | I2C | 프로토콜 | [N07B I2C](2026-11-01_N07B_i2c_smbus_debug.html) B7 |
| 9 | 주소 단계에서 NACK | I2C | 설정/드라이버 | [N07B I2C](2026-11-01_N07B_i2c_smbus_debug.html) B1 |
| 10 | I2C 장치가 NACK (보드 bring-up) | I2C | 설정/드라이버 | [BV13 보드 디버그](2026-11-01_BV13_board_debug_scenarios.html) S11 → N07B B1 |
| 11 | JEDEC ID가 0xFFFFFF | SPI | 전기 | [N07C SPI](2026-11-01_N07C_spi_qspi_debug.html) C2 |
| 12 | 부팅 시 가끔 flash를 못 찾음 (전원 시퀀스·리셋 후 대기) | SPI | 전기 | [N07C SPI](2026-11-01_N07C_spi_qspi_debug.html) C7 |
| 13 | 100 MHz에서만 깨짐 — 샘플 지연 sweep | SPI | 타이밍 | [N07C SPI](2026-11-01_N07C_spi_qspi_debug.html) C3 |
| 14 | SPI flash reads corrupted at 100 MHz, fine at 25 MHz | SPI | 타이밍 | [BV10 보드 인터페이스](2026-11-01_BV10_board_interface_tests.html) 체인 E → N07C C3 |
| 15 | 데이터가 1비트씩 shift | SPI | 설정/드라이버 | [N07C SPI](2026-11-01_N07C_spi_qspi_debug.html) C1 |
| 16 | 16 MB 넘으면 앞쪽 데이터가 보임 | SPI | 설정/드라이버 | [N07C SPI](2026-11-01_N07C_spi_qspi_debug.html) C4 |
| 17 | quad read만 실패 | SPI | 설정/드라이버 | [N07C SPI](2026-11-01_N07C_spi_qspi_debug.html) C6 |
| 18 | DMA 경로만 깨짐 (PIO는 OK) | SPI | 시스템 | [N07C SPI](2026-11-01_N07C_spi_qspi_debug.html) C5 |
| 19 | 깨진 글자 → 비트 폭 측정 | UART | 타이밍 | [N07D UART, GPIO, IRQ](2026-11-01_N07D_uart_gpio_irq_debug.html) D1 |
| 20 | UART prints garbage | UART | 타이밍 | [BV10 보드 인터페이스](2026-11-01_BV10_board_interface_tests.html) 체인 F → N07D D1 |
| 21 | 고온에서만 framing error | UART | 타이밍 | [N07D UART, GPIO, IRQ](2026-11-01_N07D_uart_gpio_irq_debug.html) D3 |
| 22 | JTAG는 되는데 UART 출력이 없음 | UART | 설정/드라이버 | [BV13 보드 디버그](2026-11-01_BV13_board_debug_scenarios.html) S3 |
| 23 | 부팅 로그는 나오는데 Linux 진입 후 콘솔이 사라짐 | UART | 설정/드라이버 | [N07D UART, GPIO, IRQ](2026-11-01_N07D_uart_gpio_irq_debug.html) D4 |
| 24 | 115200은 OK, 3 Mbaud에서 글자 빠짐 | UART | 시스템 | [N07D UART, GPIO, IRQ](2026-11-01_N07D_uart_gpio_irq_debug.html) D2 |
| 25 | 인터럽트가 두 번 들어옴 (bounce) | GPIO/IRQ | 전기 | [N07D UART, GPIO, IRQ](2026-11-01_N07D_uart_gpio_irq_debug.html) D7 |
| 26 | 인터럽트가 가끔 누락 | GPIO/IRQ | 설정/드라이버 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #4 → N07D D5 |
| 27 | 인터럽트 가끔 누락 — edge vs level·clear 순서·W1C | GPIO/IRQ | 설정/드라이버 | [N07D UART, GPIO, IRQ](2026-11-01_N07D_uart_gpio_irq_debug.html) D5 |
| 28 | IRQ storm (level 소스 미해제) | GPIO/IRQ | 설정/드라이버 | [N07D UART, GPIO, IRQ](2026-11-01_N07D_uart_gpio_irq_debug.html) D6 |
| 29 | suspend 중 wake 인터럽트가 안 됨 | GPIO/IRQ | 시스템 | [N07D UART, GPIO, IRQ](2026-11-01_N07D_uart_gpio_irq_debug.html) D8 |
| 30 | memtest가 bit 13만 실패 | DDR | 전기 | [N07E DDR](2026-11-01_N07E_ddr_memory_debug.html) E1 |
| 31 | 4 GB 중 상위 절반이 하위 절반의 복사본 | DDR | 전기 | [N07E DDR](2026-11-01_N07E_ddr_memory_debug.html) E3 |
| 32 | 같은 주소 ECC CE가 하루 수천 번 | DDR | 전기 | [N07E DDR](2026-11-01_N07E_ddr_memory_debug.html) E7 |
| 33 | DDR training 실패 | DDR | 타이밍 | [BV13 보드 디버그](2026-11-01_BV13_board_debug_scenarios.html) S6 |
| 34 | DDR training fails on a new board. How do you debug? | DDR | 타이밍 | [BV09 DDR](2026-11-01_BV09_ddr_bringup_training_memtest.html) Chain 1 |
| 35 | byte lane 2 전체가 간헐 실패 | DDR | 타이밍 | [N07E DDR](2026-11-01_N07E_ddr_memory_debug.html) E2 |
| 36 | 105 °C에서만 ECC CE 증가 | DDR | 타이밍 | [N07E DDR](2026-11-01_N07E_ddr_memory_debug.html) E4 |
| 37 | 고온(105 °C)에서만 DDR 에러 | DDR | 타이밍 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #3 → N07E E4 |
| 38 | suspend/resume 후 데이터 손상 | DDR | 설정/드라이버 | [N07E DDR](2026-11-01_N07E_ddr_memory_debug.html) E5 |
| 39 | NPU+카메라 동시 부하에서만 비트 에러 | DDR | 시스템 | [N07E DDR](2026-11-01_N07E_ddr_memory_debug.html) E6 |
| 40 | training은 통과, 1주일에 한 번 커널 패닉 | DDR | 시스템 | [N07E DDR](2026-11-01_N07E_ddr_memory_debug.html) E8 |
| 41 | Training passes, but Linux crashes randomly under load | DDR | 시스템 | [BV09 DDR](2026-11-01_BV09_ddr_bringup_training_memtest.html) Chain 6 → N07E E8 |
| 42 | Ethernet link doesn't come up | Ethernet | 전기 | [BV10 보드 인터페이스](2026-11-01_BV10_board_interface_tests.html) 체인 C → N07F F2 |
| 43 | 링크가 몇 분마다 flap | Ethernet | 전기 | [N07F Ethernet](2026-11-01_N07F_ethernet_debug.html) F5 |
| 44 | 고온/EMI에서 SQI 하락 후 링크 drop | Ethernet | 전기 | [N07F Ethernet](2026-11-01_N07F_ethernet_debug.html) F10 |
| 45 | 링크 up인데 트래픽 0 (RGMII delay) | Ethernet | 타이밍 | [N07F Ethernet](2026-11-01_N07F_ethernet_debug.html) F3 |
| 46 | FCS 에러 — loopback 사다리로 구간 자르기 | Ethernet | 타이밍 | [N07F Ethernet](2026-11-01_N07F_ethernet_debug.html) F4 |
| 47 | Ethernet loopback에서 FCS 에러만 증가 | Ethernet | 타이밍 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #10 → N07F F4 |
| 48 | MDIO 읽기가 0xFFFF | Ethernet | 설정/드라이버 | [N07F Ethernet](2026-11-01_N07F_ethernet_debug.html) F1 |
| 49 | 100/1000BASE-T1 링크가 안 뜸 (master/slave) | Ethernet | 설정/드라이버 | [N07F Ethernet](2026-11-01_N07F_ethernet_debug.html) F2 |
| 50 | 1500 B 이상 패킷만 실패 | Ethernet | 설정/드라이버 | [N07F Ethernet](2026-11-01_N07F_ethernet_debug.html) F7 |
| 51 | 64 B에서 throughput 저하 | Ethernet | 시스템 | [N07F Ethernet](2026-11-01_N07F_ethernet_debug.html) F6 |
| 52 | 1518 B에선 line rate인데 64 B에선 throughput 반토막 | Ethernet | 시스템 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #14 → N07F F6 |
| 53 | ping은 OK, iperf에서 RX drop | Ethernet | 시스템 | [N07F Ethernet](2026-11-01_N07F_ethernet_debug.html) F8 |
| 54 | PTP 오프셋에 고정 bias | Ethernet | 시스템 | [N07F Ethernet](2026-11-01_N07F_ethernet_debug.html) F9 |
| 55 | ECC 정정 카운트가 온도 따라 증가 | CSI-2 | 전기 | [N07G CSI-2 카메라](2026-11-01_N07G_mipi_csi2_camera_debug.html) G3 |
| 56 | SoT 에러만 증가 | CSI-2 | 타이밍 | [N07G CSI-2 카메라](2026-11-01_N07G_mipi_csi2_camera_debug.html) G2 |
| 57 | 카메라 프레임이 가끔 drop, 로그에 ECC 에러 | CSI-2 | 타이밍 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #8 → N07G G2, G6 |
| 58 | 이미지가 대각선으로 찢어짐 | CSI-2 | 프로토콜 | [N07G CSI-2 카메라](2026-11-01_N07G_mipi_csi2_camera_debug.html) G4 |
| 59 | FE 누락으로 다음 프레임과 합쳐짐 | CSI-2 | 프로토콜 | [N07G CSI-2 카메라](2026-11-01_N07G_mipi_csi2_camera_debug.html) G8 |
| 60 | How do you validate a camera input without a real scene? | CSI-2 | 프로토콜 | [BV10 보드 인터페이스](2026-11-01_BV10_board_interface_tests.html) 체인 D |
| 61 | 스트림 시작했는데 프레임 0개 | CSI-2 | 설정/드라이버 | [N07G CSI-2 카메라](2026-11-01_N07G_mipi_csi2_camera_debug.html) G1 |
| 62 | 색이 녹색/분홍 | CSI-2 | 설정/드라이버 | [N07G CSI-2 카메라](2026-11-01_N07G_mipi_csi2_camera_debug.html) G5 |
| 63 | VC 1 카메라 프레임이 VC 0 버퍼로 | CSI-2 | 설정/드라이버 | [N07G CSI-2 카메라](2026-11-01_N07G_mipi_csi2_camera_debug.html) G7 |
| 64 | 30 fps 설정인데 15 fps | CSI-2 | 설정/드라이버 | [N07G CSI-2 카메라](2026-11-01_N07G_mipi_csi2_camera_debug.html) G10 |
| 65 | 카메라 4대 동시면 프레임 drop | CSI-2 | 시스템 | [N07G CSI-2 카메라](2026-11-01_N07G_mipi_csi2_camera_debug.html) G6 |
| 66 | 카메라 간 timestamp가 1 프레임 어긋남 | CSI-2 | 시스템 | [N07G CSI-2 카메라](2026-11-01_N07G_mipi_csi2_camera_debug.html) G9 |
| 67 | LiDAR와 카메라 timestamp가 정확히 1초(또는 18초) 차이 | 센서 시간동기 | 시스템 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #13 → N07G G9, [L4](hmScreenPrep/problems/L4_pps_time_discipline/index.html) |
| 68 | 센서 로거가 부하가 몰릴 때만 프레임을 잃음 | 센서 파이프라인 | 시스템 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #16, [L5](hmScreenPrep/problems/L5_spsc_sensor_ring/index.html) |
| 69 | 장치가 lspci에 없음 | PCIe | 전기 | [LP15 PCIe 디버그](2026-10-31_LP15_debug_scenarios.html) S1 |
| 70 | x4 대신 x2/x1로 학습 | PCIe | 전기 | [LP15 PCIe 디버그](2026-10-31_LP15_debug_scenarios.html) S3 |
| 71 | 부하·고온에서 링크 끊김 | PCIe | 전기 | [LP15 PCIe 디버그](2026-10-31_LP15_debug_scenarios.html) S8 |
| 72 | 기대 Gen4/Gen5인데 Gen1/Gen3로만 학습 | PCIe | 타이밍 | [LP15 PCIe 디버그](2026-10-31_LP15_debug_scenarios.html) S2 |
| 73 | PCIe가 Gen4에서 Recovery 반복, Gen1은 OK | PCIe | 타이밍 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #5 → LP15 S2 |
| 74 | AER correctable 에러 폭증 | PCIe | 타이밍 | [LP15 PCIe 디버그](2026-10-31_LP15_debug_scenarios.html) S7 |
| 75 | 차량 온도 범위에서만 간헐 링크 drop | PCIe | 타이밍 | [LP15 PCIe 디버그](2026-10-31_LP15_debug_scenarios.html) B1 |
| 76 | warm reboot 후 endpoint가 lspci에서 사라짐 | PCIe | 타이밍 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #11 → LP15 S9 |
| 77 | 보이는데 드라이버 probe 실패 — BAR no space | PCIe | 설정/드라이버 | [LP15 PCIe 디버그](2026-10-31_LP15_debug_scenarios.html) S4 |
| 78 | MSI가 안 옴 (인터럽트 timeout) | PCIe | 설정/드라이버 | [LP15 PCIe 디버그](2026-10-31_LP15_debug_scenarios.html) S5 |
| 79 | DMA 데이터가 깨짐 (x86 OK, Arm SoC에서만) | PCIe | 설정/드라이버 | [LP15 PCIe 디버그](2026-10-31_LP15_debug_scenarios.html) S6 |
| 80 | suspend/resume 후 장치가 사라짐 / rev ff | PCIe | 설정/드라이버 | [LP15 PCIe 디버그](2026-10-31_LP15_debug_scenarios.html) S9 |
| 81 | 첫 config read에서 시스템 hang (bare-metal·부트로더·커널 초기) | PCIe | 설정/드라이버 | [LP15 PCIe 디버그](2026-10-31_LP15_debug_scenarios.html) S10 |
| 82 | NVMe/NPU throughput이 기대보다 낮음 | PCIe | 시스템 | [LP15 PCIe 디버그](2026-10-31_LP15_debug_scenarios.html) B2 |
| 83 | 전원 넣자마자 전류 제한에 걸림 | 보드 전원·리셋·부트 | 전기 | [BV13 보드 디버그](2026-11-01_BV13_board_debug_scenarios.html) S1 |
| 84 | rail은 정상인데 JTAG가 IDCODE를 못 읽음 | 보드 전원·리셋·부트 | 전기 | [BV13 보드 디버그](2026-11-01_BV13_board_debug_scenarios.html) S2 |
| 85 | 계속 재부팅 (몇 초마다 처음부터) | 보드 전원·리셋·부트 | 전기 | [BV13 보드 디버그](2026-11-01_BV13_board_debug_scenarios.html) S4 |
| 86 | 엉뚱한 boot mode로 부팅 | 보드 전원·리셋·부트 | 전기 | [BV13 보드 디버그](2026-11-01_BV13_board_debug_scenarios.html) S5 |
| 87 | 10장 중 2장만 실패 | 보드 전원·리셋·부트 | 전기 | [BV13 보드 디버그](2026-11-01_BV13_board_debug_scenarios.html) S8 |
| 88 | 부하 걸 때 랜덤 hang | 보드 전원·리셋·부트 | 전기 | [BV13 보드 디버그](2026-11-01_BV13_board_debug_scenarios.html) S10 |
| 89 | 보드 rev B에서 새로 생긴 실패 | 보드 전원·리셋·부트 | 전기 | [BV13 보드 디버그](2026-11-01_BV13_board_debug_scenarios.html) S12 |
| 90 | 상온 OK, 고온에서만 실패 | 보드 전원·리셋·부트 | 타이밍 | [BV13 보드 디버그](2026-11-01_BV13_board_debug_scenarios.html) S7 |
| 91 | cold boot OK, warm reset 후만 실패 | 보드 전원·리셋·부트 | 설정/드라이버 | [BV13 보드 디버그](2026-11-01_BV13_board_debug_scenarios.html) S9 |
| 92 | 부팅이 JTAG 붙이면 되고 떼면 안 됨 | 보드 전원·리셋·부트 | 설정/드라이버 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #2 |
| 93 | PetaLinux에서 PL 레지스터 mmap 읽으면 프로세스 멈춤 / SError | 보드 전원·리셋·부트 | 설정/드라이버 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #12, [L1](hmScreenPrep/problems/L1_uio_mmio_poll/index.html) |
| 94 | 부팅이 FSBL 메시지 후 멈춤 (U-Boot 안 뜸) | 보드 전원·리셋·부트 | 설정/드라이버 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #15 |
| 95 | 칩 A만 실패, 칩 B·C는 통과 | SoC·시스템 | 전기 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #6 |
| 96 | 같은 양을 쟀는데 장비마다 값이 다름 | SoC·시스템 (측정) | 전기 | [N06H 디버그 방법론](2026-11-01_N06H_debug_methodology_triage.html) N4 |
| 97 | NPU 결과가 온도에 따라 달라짐 | SoC·시스템 | 타이밍 | [N06H 디버그 방법론](2026-11-01_N06H_debug_methodology_triage.html) N1 |
| 98 | NPU 결과가 마지막 몇 열만 틀림 | SoC·시스템 | 프로토콜 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #7, [V11](hmScreenPrep/problems/V11_npu_int8_golden/index.html) |
| 99 | DMA로 복사한 데이터가 가끔 몇 바이트 틀림 | SoC·시스템 | 설정/드라이버 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #1 |
| 100 | 한 보드만 부팅이 2배 느림 | SoC·시스템 | 설정/드라이버 | [N06H 디버그 방법론](2026-11-01_N06H_debug_methodology_triage.html) N3 |
| 101 | debug 빌드에선 통과, release 빌드에선 실패 | SoC·시스템 | 설정/드라이버 | [N06H 디버그 방법론](2026-11-01_N06H_debug_methodology_triage.html) N5 |
| 102 | 회귀가 빌드 X부터 깨짐 — flaky 테스트로 bisect | SoC·시스템 (테스트) | 시스템 | [N06H 디버그 방법론](2026-11-01_N06H_debug_methodology_triage.html) N2 |
| 103 | 동시 실행할 때만 데이터 손상 | SoC·시스템 | 시스템 | [N06H 디버그 방법론](2026-11-01_N06H_debug_methodology_triage.html) N6 |
| 104 | 벤치마크 결과가 실행마다 20% 차이 | SoC·시스템 | 시스템 | [deck §7](2026-10-25_rivian_hm_screen_prep.html) #9 |

- **계층별 개수**(위 표 기준, 스크립트로 셈): 전기 25 · 타이밍 23 · 프로토콜 8 · 설정/드라이버 30 · 시스템 18. → 증상의 **거의 절반(48/104)이 전기·타이밍**으로 끝나고, 프로토콜 자체가 원인인 건 8개뿐 [추정: 작성자 분류 기준]. 프로토콜 에러는 "보이는 곳"이지 "원인"이 아닌 경우가 많다 — 그래서 bottom-up.
- **패턴으로 묶으면** (같은 질문의 변형 — 한 번 연습하면 여러 개):
  - "**고온에서만**" (#3, 21, 36, 37, 44, 55, 71, 75, 90, 97) → 마진/드리프트: 온도 계단 + 같은 손잡이로 margin 측정 ([BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html)).
  - "**고속에서만 / 저속 OK**" (#4, 13, 14, 24, 72, 73) → 속도 손잡이 + 수신단 rise time/샘플 지연.
  - "**부하·동시에서만**" (#39, 41, 51–53, 65, 68, 71, 88, 103) → 시스템: 대역폭·IRQ latency·droop·race.
  - "**리셋/전원 상태 전환 후만**" (#1, 5, 12, 29, 38, 76, 80, 91) → 상태 머신이 리셋 안 됨, 대기 시간, 도메인.
  - "**한 비트/한 lane/한 주소**" (#30, 31, 32, 35, 70) → 물리 위치가 있는 고장: 회로도에서 그 선을 따라간다.

## 4. 꼬리 질문 체인

### 체인 1 — "Debug an interface you've never used." → 장비 → 디코드 OK인데 실패 → 1/10,000

<details><summary>Q → 꼬리1 → 꼬리2 → 꼬리3 (답 펼치기)</summary>

**Q. Here's an interface you've never used. It doesn't work. How do you approach it?**
*"I start with what's common to every interface: it's wires, timing, a protocol, a configuration, and a system around it. I read the spec just enough to know what normal looks like — levels, timing numbers, the handshake — and I find a known-good reference, a dev kit or another board, so I can capture a golden baseline. Then I read every status bit and error counter the controller gives me, because they usually tell me which layer is broken. From there I go bottom-up: power and levels with a DMM, edges with a scope, the transaction with a decoder, and only then configuration and driver."*
- 한국어 핵심: **스펙은 "정상 모양"만큼 → golden 확보 → 카운터로 층 선택 → bottom-up**.

**꼬리1. Which instrument do you reach for first?**
*"Before any instrument, the counters — they're free and they already watched every transaction. Then the cheapest instrument that can answer the question: a DMM for 'is it powered and pulled up', a scope when I need the analog shape at the receiver, a logic analyzer when it's about the order of several signals, a protocol analyzer only for high-speed links where I need packet content. I choose the bandwidth from the rise time — about three times 0.35 over the edge — and I keep the probe ground short so I'm not measuring my own ringing."*
- 한국어 핵심: **카운터 → DMM → scope → LA → protocol analyzer, BW = 3×0.35/tr, 짧은 접지**.

**꼬리2. The analyzer decodes every transaction correctly, but the device still behaves wrong. Now what?**
*"Then I've learned the protocol on the wire looks right from where I probed, which isn't the same as what the device saw. First I check where I probed and with what threshold — a decoder uses one threshold, the device has its own VIH and VIL, and a slow edge can be read differently; I look at the analog waveform at the device pin. Second, I check what the decode doesn't show: timing between transactions, a repeated START versus STOP-START, chip-select gaps that reset the device's command. Third, if the wire really is right, the problem is above the protocol — the device's state, its configuration registers, power mode, or the order of operations — so I read back its status and compare against the golden board."*
- 한국어 핵심: **probe 위치·임계값 ≠ 장치가 본 것 → 디코드가 숨기는 타이밍(트랜잭션 간격·CS gap) → 와이어가 맞으면 장치 상태/설정**.

**꼬리3. It only fails once in 10,000 transactions. How do you even catch it?**
*"I don't stare at a persistence display hoping — with a few thousand waveforms a second and a short window, the scope is blind more than ninety-nine percent of the time. I make the failure trigger the capture: if the controller flags NACK or a CRC error, firmware toggles a GPIO on that event and I trigger the scope on it with pre-trigger, so I see the transaction that failed. If I need many examples, I use segmented memory with time tags and look at whether failures are periodic or correlate with another signal. And I measure the rate as a number first, so after the fix I know I need roughly thirty thousand clean transactions to claim it's gone."*
- 한국어 핵심: **persistence는 운 → 에러 순간 GPIO 트리거 + pre-trigger → segmented로 여러 건·시간 간격 → 실패율 숫자, 고친 뒤 ≈3/p**.

</details>

### 체인 2 — "You see CRC errors. Is it electrical or protocol?"

<details><summary>Q → 꼬리1 → 꼬리2 (답 펼치기)</summary>

**Q. A link reports CRC errors. Is it an electrical problem or a protocol problem?**
*"A CRC error is detected at the protocol layer, but it's almost always caused below it — the bits got corrupted on the way. A protocol or configuration bug usually produces a deterministic failure, like wrong length or wrong format, not random CRC errors. So I check whether the errors depend on speed, temperature, cable or lane: if lowering the speed or swapping the cable makes them go away, it's signal integrity or timing. If they're deterministic for a particular packet size or pattern, I look at configuration — framing, delays, data width."*
- 한국어 핵심: **CRC는 프로토콜에서 '보이는' 것, 원인은 대개 아래(전기/타이밍) — 조건 의존이면 SI, 결정적이면 설정**.

**꼬리1. How do you tell which segment of the path corrupts the bits?**
*"Loopback at each stage. On Ethernet I loop back inside the MAC, then at the PCS, then in the PHY, then externally with a plug; the first stage where errors appear brackets the fault. With built-in PRBS generators and checkers I can do the same without any traffic software, and I let it run long enough to make the bit error rate meaningful."*
- 한국어 핵심: **loopback 사다리 + PRBS로 구간 자르기, BER은 충분한 비트 수로** ([BV10 §2.5.3](2026-11-01_BV10_board_interface_tests.html)).

**꼬리2. The error count is low — a few per hour. Is that a problem?**
*"It depends on the spec and the trend, not just the number. I convert it to a bit error rate and compare with the link's requirement, and I check that it's flat across temperature and boards. A low rate that rises with temperature or is concentrated on one board or lane is a margin problem that will become a field failure, so I'd characterize it with a margin sweep rather than accept it."*
- 한국어 핵심: **절대값보다 BER 환산 + 추세(온도·보드·lane), 상승 추세면 마진 문제로 특성화**.

</details>

### 체인 3 — "Your scope shows ringing and a slow edge. Is the board bad?"

<details><summary>Q → 꼬리1 → 꼬리2 (답 펼치기)</summary>

**Q. You probe a signal and see a lot of ringing on the edge. Is the board bad?**
*"Maybe, but first I check my measurement. A six-inch ground lead on a passive probe resonates around a hundred and something megahertz, so with a fast edge that ringing could be the probe. I switch to a spring-tip ground at the receiver pin and see if it changes — if moving the probe cable changes the waveform, it's my setup. If the ringing stays with a short ground and a probe with enough bandwidth, then it's real and I'd look at termination and the trace."*
- 한국어 핵심: **측정 먼저 의심 — 긴 접지선 ≈140 MHz 공진, spring tip으로 바꿔 사라지면 프로브 탓** [1].

**꼬리1. And a slow I2C edge — could your probe cause that too?**
*"Yes. I2C rise time is set by the pull-up and the bus capacitance, so adding a probe adds capacitance — a 1x probe adds a lot, which would make the edge slower than it is. I use a 10x probe, measure 30 to 70 percent as the spec defines it, and measure at the farthest device. Then I compare with the spec — 300 nanoseconds maximum in Fast-mode — and with the golden board."*
- 한국어 핵심: **I2C rise time = R×C, 1× 프로브는 C를 더함 → 10×, 30–70% 기준, 가장 먼 장치, Fm 300 ns** [8].

**꼬리2. What bandwidth scope do you need for that measurement?**
*"Very little — for a 300 nanosecond edge, 0.35 over the edge is about 1.2 megahertz, times three is under 4 megahertz. Any scope does it. The point is that bandwidth only matters for fast edges; for slow buses the probe loading and where you measure matter more."*
- 한국어 핵심: **느린 신호는 BW가 아니라 부하·위치가 문제** (§6 계산).

</details>

### 체인 4 — "Counter or waveform — which do you trust?"

<details><summary>Q → 꼬리1 → 꼬리2 (답 펼치기)</summary>

**Q. The driver's error counter says zero, but the application sees corrupted data. Which do you trust?**
*"Neither blindly. A zero counter means the error checks the hardware does passed — it doesn't mean the data is right. CRC can pass if the corruption happened before the CRC was computed or after it was checked, for example in a DMA buffer with a cache-coherency bug. So I localize where the data goes wrong: compare at the wire with an analyzer, then at the receive buffer, then at the application. If the wire is right and the buffer is wrong, it's DMA, cache or memory, not the interface."*
- 한국어 핵심: **카운터 0 = HW 검사 통과일 뿐 → 와이어·버퍼·앱 3지점 비교 → 와이어 OK·버퍼 틀림이면 DMA/캐시/메모리**.

**꼬리1. How do you make sure you're reading the counter correctly?**
*"I check the IP documentation for whether it's clear-on-read, write-one-to-clear, saturating or wrapping, and I always read it twice — before and after a known workload — and work with the delta. I also do a negative control: inject a known error, like a wrong CRC or a forced NACK, and see the counter move. A counter that never moves might just be wired to nothing."*
- 한국어 핵심: **clear-on-read/W1C/포화/랩 확인, 전후 diff, 일부러 에러 주입해서 카운터가 움직이는지 (negative control)**.

**꼬리2. What if instrumenting it makes the bug go away?**
*"That's data, not bad luck. If adding a print or a probe changes the behaviour, the bug is timing-sensitive — a race, or a marginal edge that the probe's capacitance helps or hurts. I switch to lower-impact instrumentation: a GPIO toggle instead of a print, a trace buffer, an active probe with lower loading, and I note which change made it disappear because that points at the mechanism."*
- 한국어 핵심: **heisenbug = 타이밍 민감 단서 → 저침습 계측(GPIO·trace·active probe), 무엇이 사라지게 했는지 기록**.

</details>

## 5. Don 경험 연결

- "At Apple I root-caused wireless-chipset integration issues on PCIe, I2C, SPMI and RFFE with a DSO and a protocol analyzer — the habit I took from that is to read the controller's error status first and then trigger the instrument on the failure, which on one issue cut the capture time from [숫자 채우기] to [숫자 채우기]."
- "At SK hynix I ran interface-reliability and shmoo campaigns on NAND and PCIe 3/4/5 in thermal chambers, so I think of 'fails only when hot' as a margin I can measure, not a mystery — we swept [숫자 채우기] conditions across [숫자 채우기] units."
- "At Solidigm, during pre-silicon FPGA bring-up, I verified I2C, SPI, DMA and DRAM paths before RTL freeze — I'd capture a golden trace from the FPGA build and diff silicon against it, which caught [숫자 채우기] bugs before tape-out."
- "On the factory side at Apple I built test nodes for latent defects, so I always end a debug with the question 'what test would have caught this on the line?'"

## 6. 실습 / 코드

### 6.1 캡처 확률 · 장비 설정 계산기 — 실제 실행 [확인됨]

- 파일: [onsitePrep/n07/a/capture_odds.py](onsitePrep/n07/a/capture_odds.py) (Python stdlib), [run.sh](onsitePrep/n07/a/run.sh) → [run_output.txt](onsitePrep/n07/a/run_output.txt). 실행: `sh onsitePrep/n07/a/run.sh` (`python3 -I` 사용).
- 하는 일 4가지 (각 케이스에 기대값이 있어 PASS/FAIL 자체 판정): ① scope dead time % [3] ② 드문 이벤트를 persistence로 볼 확률 vs 이벤트 트리거 ③ segmented memory vs 연속 기록 메모리 ④ 인터페이스별 필요 scope BW(3×0.35/tr)와 LA 샘플 레이트 판정(≥ 4배 [6]) + LA 타이밍 측정 오차.
- 실제 출력 (2026-11-01 실행, 전체는 run_output.txt):

```text
== 1. Scope dead time (persistence / free-running acquisition) ==
  fast scope 1,000,000 wfm/s @ 20 ns/div: window 0.2 us, dead time 80.00000%
  [PASS] fast scope 1,000,000 wfm/s @ 20 ns/div dead-time %: got 80, expected 80
  decode-on scope 1,000 wfm/s @ 20 ns/div: window 0.2 us, dead time 99.98000%
  [PASS] decode-on scope 1,000 wfm/s @ 20 ns/div dead-time %: got 99.98, expected 99.98
  I2C view 50 wfm/s @ 100 us/div: window 1e+03 us, dead time 95.00000%
  [PASS] I2C view 50 wfm/s @ 100 us/div dead-time %: got 95, expected 95

== 2. Rare event: wait for luck (persistence) vs trigger on it ==
  Keysight example: glitch every 10 ms, 200 ns window, 1e6 wfm/s, 1 s: P = 100.00%
  same glitch, 1e3 wfm/s (decode on), 1 s: P = 1.98%
  I2C glitch every 10 s, 1 ms window, 50 wfm/s, 60 s: P = 25.92%
  I2C glitch every 10 s, 1 ms window, 50 wfm/s, 10 min: P = 95.02%
  pulse-width trigger on the same I2C glitch: P(miss one event) ~ 2.5e-07 -> capture ~certain

== 3. Segmented memory vs one continuous record ==
  transaction length 67.5 us, segment 100 us = 10000 pts
  segmented: 1000 x 10000 = 10.0 Mpts ; continuous 10 s: 1000 Mpts
  memory ratio continuous/segmented = 100x

== 4. Per-interface: scope bandwidth for 5% rise-time error, LA sample rate ==
  I2C Fm 400 kHz (tr<=300 ns spec): scope BW >= 3 MHz ; LA 25 MS/s = 62.5x signal rate -> OK (timing resolution 40 ns)
  UART 3 Mbaud (tr~10 ns est.): scope BW >= 105 MHz ; LA 25 MS/s = 8.3x signal rate -> OK (timing resolution 40 ns)
  SPI 50 MHz (tr~2 ns est.): scope BW >= 525 MHz ; LA 100 MS/s = 2.0x signal rate -> TOO SLOW (timing resolution 10 ns)
  SPI 50 MHz on 500 MS/s LA: scope BW >= 525 MHz ; LA 500 MS/s = 10.0x signal rate -> OK (timing resolution 2 ns)
  RGMII data 250 Mb/s DDR on 125 MHz clk (tr~0.75 ns est.): scope BW >= 1,400 MHz ; LA 500 MS/s = 2.0x signal rate -> TOO SLOW (timing resolution 2 ns)
  measuring a 100 ns setup time with 25 MS/s: +-40 ns = +-40% of the value
  measuring a 100 ns setup time with 500 MS/s: +-2 ns = +-2% of the value

SUMMARY: 18/18 PASS
```

(위는 PASS 줄 일부를 생략해 붙인 것 — 원문 18개 PASS 줄은 run_output.txt.)

**출력 읽는 법**

- **§1 dead time**: 초당 100만 파형 scope도 20 ns/div에선 **80%를 못 본다**; 디코드를 켜 update rate가 1,000 wfm/s로 떨어지면 **99.98% 장님**. "화면이 빠르게 갱신된다 ≠ 다 보고 있다" [3].
- **§2 확률**: 같은 글리치라도 1e6 wfm/s면 1초에 거의 100%, 1e3 wfm/s면 **2%**. I2C 규모(10초에 한 번)는 1분 25.9%, 10분 95% — 면접 답: "**10분 기다리는 대신 pulse-width 트리거를 걸면 다음 발생에서 바로 잡힌다**"(놓칠 확률 ≈ re-arm/주기 = 2.5e-7; re-arm 2.5 µs는 Keysight 고급 기종의 intersegment 시간 [4]을 차용한 가정값).
- **§3 segmented**: 트랜잭션 1,000개를 연속으로 담으면 1 Gpts, segmented면 10 Mpts — **100배**. 더 중요한 건 조각별 시간 태그로 "실패 간격"을 본다는 것.
- **§4 BW / LA**: I2C는 3.5 MHz면 충분(BW 걱정 대신 프로브 부하), RGMII는 1.4 GHz급 + LA로는 2배라 **타이밍 측정 불가 → scope로**. SPI 50 MHz를 100 MS/s LA로 받으면 2배 → 디코드도 못 믿음. 마지막 두 줄: LA 해상도 40 ns로 100 ns setup을 재면 **±40%** — "LA는 순서, scope는 타이밍".
- [추정] 표시: UART/SPI/RGMII 엣지 tr 값은 전형값 가정(드라이버 세기·부하에 따라 다름). I2C 300 ns·100 ns는 UM10204 표 [8].

### 6.2 랩 레시피 `[미실행]` — 간헐 I2C NACK을 segmented capture로 잡기

1. **카운터 기준점**: 테스트 루프 시작 전 컨트롤러 NACK/timeout 카운트(또는 dmesg 에러 수) 기록. 루프: 같은 레지스터 read 10만 회, 실패 k/N.
2. **FW GPIO**: I2C 드라이버/테스트가 NACK 상태 비트를 본 순간 spare GPIO를 high→low 토글 (bare-metal이면 한 줄; Linux면 테스트 앱에서 libgpiod로 — 지연 µs–ms라 pre-trigger를 넉넉히 [추정]).
3. **scope 채널**: CH1 SCL, CH2 SDA (10× 프로브, spring/짧은 접지, **가장 먼 slave 핀**), CH3 FW GPIO. 수평: 트랜잭션 1개(400 kHz, 3 byte ≈ 70 µs)가 화면 70–80%, 즉 10 µs/div.
4. **트리거**: CH3 falling edge, normal 모드, **pre-trigger 80–90%** (GPIO는 NACK 이후에 오므로 그 앞 트랜잭션이 화면에 남게). 대안: scope I2C serial 트리거 "address X + NACK" (scope가 지원 시).
5. **segmented memory**: 세그먼트 수 100–1000, 세그먼트 길이 = 한 화면. 각 세그먼트 **시간 태그** 저장.
6. **I2C decode**를 세그먼트마다 적용(scope 지원 시 [5]) → 실패 트랜잭션의 주소·바이트 위치 표.
7. **읽기**: ① 실패가 항상 같은 바이트 위치? (장치 내부 상태/stretching) ② SDA/SCL rise time이 실패 세그먼트에서 더 느린가? (전기) ③ 시간 태그 간격이 주기적? (다른 소스 — 예: 같은 레일의 모터·SerDes 버스트와 상관 → CH4로 그 신호 추가) ④ 실패 직전 SCL에 짧은 글리치? (pulse-width 트리거로 재확인).
8. **비교**: golden 보드에서 같은 설정으로 1 회 캡처 → rise time·트랜잭션 간격 diff.
9. **회귀**: 원인 수정 후 같은 루프 ≥ 3/p 회 무실패 + 팜 nightly에 NACK 카운트 0 판정 추가.

## 7. 자주 틀리는 것

1. **증상의 층 = 원인의 층으로 착각** — CRC/NACK/drop은 프로토콜·시스템에서 "보일" 뿐, 원인은 전기·타이밍인 경우가 많다. 아래 층 증거 없이 "드라이버 버그"라고 말하지 말 것.
2. **카운터를 한 번만 읽기** — 누적값은 의미 없다. 전/후 diff, clear-on-read·W1C 확인, negative control(일부러 에러 주입)로 카운터가 살아 있는지.
3. **persistence 화면만 보고 "이상 없음"** — dead time 99% 이상일 수 있다 [3]. 드문 이벤트는 **이벤트 트리거** 또는 FW GPIO.
4. **측정이 신호를 바꾸는 걸 잊음** — 긴 접지선 ringing [1], 1× 프로브가 I2C rise time을 늦춤, interposer가 고속 링크 마진을 바꿈 [추정]. "측정 셋업도 용의자".
5. **송신단에서 재고 판정** — 판정은 수신단(가장 먼 slave, 수신 핀) 기준.
6. **LA 디코드를 아날로그 확인 없이 믿기** — 임계값 하나 vs 장치 VIH/VIL. 그리고 LA 샘플 4배는 디코드 최소, 타이밍 측정엔 부족 [6].
7. **고속 차동 링크를 일반 scope로 "보면 된다"** — D-PHY HS·PCIe·SGMII는 RX 에러 카운터·내장 eye scan·BERT가 1차 도구. 고속 SI 측정은 fixture와 SI팀.
8. **손잡이 여러 개 동시에** — 속도와 드라이버 빌드를 같이 바꾸면 인과를 모름. 그리고 "저속 OK"는 타이밍뿐 아니라 대역폭(시스템) 문제도 가린다.
9. **golden 없이 데이터시트 값과만 비교** — 실제 정상 보드의 rise time·margin·카운터를 먼저. golden에도 보드 rev·FW·온도·장비 설정 기록.
10. **고친 뒤 "몇 번 돌려 봤더니 괜찮다"** — 실패율 p면 ≈ 3/p 무실패 ([N06H §2.3](2026-11-01_N06H_debug_methodology_triage.html)), 그리고 회귀 테스트가 fix를 되돌리면 실패하는지.

## 8. 출처

1. Tektronix, "ABCs of Probes" primer — 0.35/BW, 3:1 → ~5% · 5:1 → ~2% 오차, 11 pF + 6인치 ground lead ≈ 140 MHz ringing, 접지 자가 진단, 1× 프로브 (BV11에서 확인한 것 재사용) · https://docs.ampnuts.ru/eevblog.docs/Tektronix/pub/ABCs%20of%20Probes.pdf
2. Keysight, "Evaluating Oscilloscope Bandwidths for Your Application" (5989-5733EN) — Gaussian 응답 0.35/BW · https://helpfiles.keysight.com/sp/PD1000A/Content/PD1500A%20DPT%20Tests/5989-5733EN.pdf
3. Keysight, "Oscilloscope Waveform Update Rate Determines Probability of Capturing Elusive Events" (5989-7885EN) — dead time `%DT = 100 × (1 − U·W)`, W = timebase × 10, 캡처 확률 `1 − ((S−1)/S)^N`, 10 ms 글리치·20 ns/div → S = 50,000 예 (PDF 텍스트에서 확인) · https://xdevs.com/doc/HP_Agilent_Keysight/5989-7885EN%20English%20_%202014-08-02%20_%20PDF%202.87%20MB%20c20140903%20%5B12%5D.pdf
4. Keysight, "Using a Scope's Segmented Memory to Capture Signals" (5989-4932) — 버스트만 저장·idle 미저장, 90000 시리즈 intersegment 2.5 µs, 최대 131,072 세그먼트(옵션 기준) · https://www.keysight.com/ca/en/assets/7018-01382/application-notes/5989-4932.pdf
5. Keysight, "Using Oscilloscope Segmented Memory for Serial Bus Applications" (5990-5817) — 세그먼트마다 serial decode, 다수 프레임 연속 캡처 (검색 결과 요약으로 확인, 본문 미열람) · https://www.keysight.com/at/de/assets/7018-02543/application-notes/5990-5817.pdf
6. Saleae support, "What Sampling Rate Should I Use?" — 디지털 신호는 대역폭의 최소 4배, SPI는 클럭의 최소 4배, 100 kHz I2C 예 1 MS/s 이상 · https://www.saleae.com/support/logic-software/capturing-data/what-sample-rate-is-required
7. Logic analyzer — timing mode(비동기, 내부 클럭) vs state mode(동기, DUT 클럭) 개념 (검색 결과 요약으로 확인): Wikipedia "Logic analyzer" https://en.wikipedia.org/wiki/Logic_analyzer · Keysight "Solve Debugging Problems with Logic Analyzer" (5968-5700) https://www.keysight.com/ie/en/assets/7018-06691/application-notes/5968-5700.pdf.html
8. NXP UM10204, I2C-bus specification and user manual (Rev. 7, 2021) — 표: tr Standard-mode ≤ 1000 ns · Fast-mode 20–300 ns · Fm+ ≤ 120 ns, tSU;DAT 250 / 100 / 50 ns, tLOW 4.7 / 1.3 / 0.5 µs (PDF 텍스트에서 확인) · https://www.nxp.com/docs/en/user-guide/UM10204.pdf
9. ethtool(8) man page — `-S`, `--phy-statistics`, `-T`, `-i`, `-d`, `--cable-test`, `-t` (external_lb) 확인; SQI 표시는 man 페이지에 없음 → 본문 SQI 언급은 PHY 드라이버 지원 시 `ethtool <dev>` 출력 기준 [추정] · https://man7.org/linux/man-pages/man8/ethtool.8.html
10. 내부 노트 (방법론·장비 원본): [N06H](2026-11-01_N06H_debug_methodology_triage.html), [BV11](2026-11-01_BV11_lab_instruments_measurement.html), [BV13](2026-11-01_BV13_board_debug_scenarios.html), [LP15](2026-10-31_LP15_debug_scenarios.html), [BV10](2026-11-01_BV10_board_interface_tests.html), [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html), [deck](2026-10-25_rivian_hm_screen_prep.html)
