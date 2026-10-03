# 📹 S04 · Video / FPGA 파이프라인 펌웨어 — 무엇을 만들고, 어떻게 테스트하나

> FPV 드론에서 비디오는 "보는 기능"이 아니라 **조종 루프의 일부**다. 카메라 → 센서 인터페이스(MIPI CSI-2 / GMSL) → FPGA/SoC 변환·압축 → 무선 → 수신 디코드 → 헤드셋 화면까지의 **glass-to-glass 지연**이 조종 가능성을 정한다. Neros 공고에는 FPGA에서의 video conversion·compression·streaming, MCU↔FPGA 고속 데이터, clock domain 동기화가 명시돼 있다 [확인됨 공고 5095368007]. 이 노트는 이 파이프라인을 **측정 가능한 숫자**(지연 분포, 프레임 무결성, 화질 지표)로 테스트하는 리그를 설계한다.

## 0. 한 장 요약

| 항목 | 내용 |
|---|---|
| 이 직군이 만드는 것 | 카메라 센서 bring-up(I2C 레지스터 설정), MIPI CSI-2/GMSL 수신, FPGA 비디오 변환(색공간·스케일·보정)·압축·스트리밍, MCU↔FPGA 제어/데이터 경로, 수신측 디코드(GStreamer/ffmpeg) [확인됨 공고 5095368007 · 5241249007] |
| 주로 고장 나는 곳 | 센서 설정 race(부팅 타이밍), CDC(clock domain crossing) 경계의 드문 데이터 손상, 버퍼 underrun/overrun, 압축률 vs 지연, 비트 에러 후 복구, 열로 인한 클럭·링크 불안정, FPGA bitstream ↔ FW 버전 불일치 |
| 대표 테스트베드 | **패턴 주입 + 프레임 CRC 체크 리그**(정확성), **LED/타이머 + 포토다이오드 지연 리그**(glass-to-glass), **비트 에러 주입 링크**(복구), 열 챔버 스트레스, 화질 지표(PSNR/SSIM) 파이프라인 |
| 합격 기준 예 | 패턴 프레임 CRC 불일치 **0 / 1,000만 프레임**, glass-to-glass **p99 회귀 +3ms 이내** [추정], BER 1e-5에서 화면 복구 **< 100ms** [추정], 60°C에서 1시간 무중단 |
| Don 연결 | Solidigm **FPGA 이미지 / FW bring-up**(PCIe/NVMe IP), PCIe Gen5/6 고속 링크 이해, SoC verification(DMA, SRAM/DRAM), 성능 튜닝 |

## 1. 이 직군 이해하기 — Neros 공고 근거

- Peripherals FW (Tennessee): "Work with **FPGA, MCU and/or embedded Linux** systems to implement **video conversions, corrections, storage, compression, streaming**" [확인됨 공고 5095368007]
- 같은 공고 요구사항: "Significant FPGA experience", "**Synchronization between clock domains**", "camera interfaces, video formats and compression standards", nice-to-have "**High speed data streaming between MCU and FPGA**", "**Gstreamer or ffmpeg** video pipeline", "video conversions in FPGAs, SoCs" [확인됨 공고 5095368007]
- Autonomy 쪽 카메라 경로: Senior Platform Engineer — "sensor drivers and data paths for cameras … **MIPI CSI-2, GMSL**, USB3 Vision, GigE Vision", "multi-sensor and **video streams**", "end-to-end **sensor-to-actuator latency** measurement under sustained thermal load … as a **release gate**", "hardware time-stamping" [확인됨 공고 5241249007] → 지연을 릴리즈 게이트로 쓰는 문화가 공고에 이미 있다
- 링크 쪽: "low-latency **video links**", "video, command-and-control, telemetry … links" [확인됨 공고 5253713007 Principal Connectivity]
- 지상 표시: **Ground Software Manager** 공고 — GCS의 low-latency video 파이프라인 [확인됨 공고 5173033007]
- 디지털/아날로그 비디오가 둘 다 쓰이는지, 어떤 코덱·FPGA 벤더인지는 공고에 없다 → **[추정]**으로만 말할 것
- 카메라/VTX/OSD 용어: [C02 드론 스택 용어 사전](../../onsitePrep/site/company/2026-10-02_C02_drone_tech_stack_glossary.html)

## 2. 이 펌웨어가 하는 일 — 데이터 흐름

```text
 [장면] ─► 이미지 센서 ──MIPI CSI-2 (또는 GMSL serializer→coax→deserializer)──┐
             ▲ I2C: 레지스터 설정(해상도·fps·노출)                           │
             │                                                              ▼
 ┌───────── MCU (STM32) ─────────┐  SPI/QSPI/병렬 버스   ┌──────────── FPGA ─────────────┐
 │ 부팅 순서·센서 init·전원 시퀀스│ ◄──── 제어·상태 ────► │ CSI-2 RX → 디베이어/색공간     │
 │ OSD 데이터·텔레메트리 합성     │ ════ 고속 데이터 ════►│ → 스케일·보정 → 압축(또는 원본)│
 │ FPGA bitstream 로드·버전 확인  │                       │ → 패킷화 → 무선 모뎀/아날로그  │
 └───────────────────────────────┘     clock domain A ┃ B │   (센서 클럭 ≠ 링크 클럭: CDC)  │
                                                     ┃   └──────────────┬────────────────┘
                                                                         │ 무선 비디오 링크
                                                                         ▼
       [헤드셋/GCS] 수신 → 디패킷·FEC → 디코드(HW 또는 GStreamer/ffmpeg) → OSD 합성 → 디스플레이
                                                                         │
                                                           glass-to-glass = 센서 노출 ~ 화면 발광
```

- **지연 예산** [추정 예시, 60fps 기준]: 센서 readout ~8ms + FPGA 처리 1~3ms + 인코드 2~10ms + 무선 2~10ms + 디코드 2~10ms + 디스플레이 스캔 ~8ms → 디지털 FPV는 대략 **20~40ms** 대, 아날로그는 그보다 짧은 편. 숫자 자체보다 **각 구간을 따로 잴 수 있게** 리그를 만드는 게 핵심
- **CDC**: 센서 픽셀 클럭과 링크/메모리 클럭이 다르다. FIFO·동기화기 버그는 수백만 프레임에 한 번 나온다 → 장시간 CRC soak가 필요한 이유
- **MCU↔FPGA**: 제어(레지스터 읽기/쓰기)와 데이터(OSD, 메타데이터)가 다른 경로일 수 있다 [추정]. 버전 협상(bitstream ID ↔ FW가 기대하는 레지스터 맵)이 깨지면 화면이 안 나온다

## 3. 무엇이 고장 나나

| Failure mode | 증상 | 어느 테스트 레벨에서 잡나 |
|---|---|---|
| 센서 init 순서/타이밍 race | 부팅 100번 중 1~2번 검은 화면, 색이 이상함 | HIL (전원 사이클 반복) |
| I2C 센서 설정 값 오류 | 해상도·fps가 기대와 다름, 노출 깜빡임 | component (레지스터 덤프 비교) |
| CSI-2 링크 에러 (lane deskew, CRC/ECC) | 줄무늬, 프레임 드롭 | HIL (패턴 + 에러 카운터) · thermal |
| CDC/FIFO 경계 버그 | 드문 픽셀 깨짐, 몇 시간에 한 번 프레임 손상 | soak (프레임 CRC 수천만 개) |
| 버퍼 overrun/underrun | 화면 멈춤, 프레임 반복, 지연이 서서히 증가 | HIL (지연 분포 추이) |
| 압축 설정 회귀 (GOP, bitrate) | 지연 증가, 움직임 블록 노이즈 | HIL (지연 리그 + PSNR/SSIM) |
| 비트 에러 후 복구 실패 | 한 번 깨지면 다음 I-frame까지 수 초 회색 화면 | integration (에러 주입) |
| 열로 인한 클럭·링크 불안정 | 10분 비행 후 화면 끊김 | thermal stress |
| bitstream ↔ FW ↔ 수신기 버전 불일치 | 화면 없음, OSD 위치 어긋남 | integration (버전 매트릭스) |
| 수신측 디코더 파이프라인(GStreamer) 버퍼링 | GCS 화면만 지연 큼 | integration (수신측 단독 측정) |
| OSD 합성 타이밍 | 텔레메트리 숫자가 영상보다 늦음 | HIL (타임스탬프 비교) |

## 4. 테스트 레벨 — 누가, 어디서, 언제

| 레벨 | 누가 | 무엇을 | 어디서 | CI 단계 |
|---|---|---|---|---|
| unit | FW 개발자 (+ FPGA 개발자는 HDL 시뮬레이션) | 레지스터 맵 인코딩, 패킷화·FEC 로직, 버전 협상, 센서 설정 테이블 | host / HDL 시뮬레이터 | PR |
| component | FW Test | **패턴 주입**으로 FPGA 경로만: 입력 패턴 → 출력 프레임 CRC | 벤치 (패턴 생성기 또는 FPGA 내장 패턴) | PR 스모크 / nightly |
| integration | FW Test | 실제 센서 + FPGA + MCU + 송신 → 수신 디코드, 버전 매트릭스, 에러 주입 | HIL 랙 (케이블 RF) | nightly |
| HIL | FW Test | glass-to-glass 지연 분포, 전원 사이클 1,000회 부팅, OSD 동기 | 지연 리그 | nightly / release 게이트 |
| system | FW Test + 비행팀 | 열 챔버 1시간 + 진동, 다중 기체 동시 송신 간섭 | 챔버 · 시험장 | release |
| field | 시험 조종사 | 실제 비행 중 화면 품질·끊김 기록, 수신측 녹화 회수 | 필드 | release 후 |

- **지연을 릴리즈 게이트로**: Autonomy 공고가 이미 "latency to be enforced as a release gate"라고 쓴다 [확인됨 공고 5241249007] → 비디오 경로에도 같은 원칙을 제안하면 자연스럽다

## 5. 테스트베드 설계

```text
     ┌──────────────────── CI runner PC (pytest) ──────────────────────┐
     │ drivers: PatternSource · LatencyRig · FrameChecker · BerInjector │
     │          ThermalChamber · Psu · SwdProbe · FpgaLoader             │
     └──┬────────────┬──────────────┬───────────────┬──────────────┬───┘
        │            │              │               │              │
        ▼            ▼              ▼               ▼              ▼
 ┌───────────┐ ┌───────────────┐ ┌─────────────┐ ┌────────────┐ ┌────────────┐
 │ ① 지연 리그│ │ ② 패턴 리그     │ │ ③ 에러 주입 │ │ 열 챔버     │ │ PSU+릴레이 │
 │ LED 모듈   │ │ 테스트 패턴    │ │ 감쇠기 or   │ │ -20~+70°C  │ │ 전원 사이클│
 │ (μs 타이머)│ │ (카운터·색막대)│ │ 패킷 드롭/  │ │ [추정 범위]│ │ 1,000회    │
 │  ↓ 카메라  │ │  ↓ FPGA 입력  │ │ 비트 플립   │ └─────┬──────┘ └─────┬──────┘
 │ DUT 송신   │ │ DUT(FPGA 경로) │ │ (링크 중간) │       │              │
 │  ↓ 무선    │ │  ↓            │ └──────┬──────┘       ▼              ▼
 │ 수신 디코드│ │ 출력 캡처      │        │        ┌──────────── DUT ─────────────┐
 │  ↓ 화면    │ │ (HDMI/USB3)   │        └───────►│ 카메라·MCU·FPGA·송신 보드      │
 │ 포토다이오드│ │  ↓ 프레임별   │                 │ + 레퍼런스 수신기/헤드셋        │
 │ → 오실로/  │ │ CRC·카운터 비교│                 └──────────────────────────────┘
 │ 타이머 보드│ └───────────────┘
 └───────────┘
```

**① glass-to-glass 지연 리그**

- 카메라 앞에 LED(또는 μs 카운터를 띄운 고속 디스플레이)를 두고, 화면 해당 위치에 포토다이오드를 붙인다
- 마이크로컨트롤러 타이머 보드가 LED를 켠 시각 t0과 포토다이오드 임계 교차 시각 t1을 μs 단위로 기록 → `latency = t1 - t0`
- LED를 **프레임 주기와 무관한 무작위 시점**에 켠다 → 노출·스캔 위상 때문에 생기는 분포 전체를 얻는다. 1회 측정은 의미 없고 **500~1,000회 분포**(p50/p99/max)를 낸다
- 구간별 측정: FPGA 출력 GPIO 마커, 송신 직전 타임스탬프, 수신 디코드 완료 타임스탬프를 같은 타이머 보드로 → 어느 구간이 늘었는지 바로 보인다
- 대안: 고속 카메라(240~1,000fps)로 LED와 화면을 한 프레임에 찍기 — 수동 분석이 필요해 CI에는 부적합

**② 패턴 주입 + 프레임 무결성**

- 입력: 프레임 번호가 픽셀에 인코딩된 패턴(카운터 바, 색막대, 체커보드). FPGA 내장 패턴 생성기 또는 외부 패턴 소스 [추정 구성]
- 출력: 무압축 경로는 프레임별 **CRC32를 기대값과 비교**(bit-exact). 압축 경로는 bit-exact가 불가 → 프레임 번호 연속성 + **PSNR/SSIM 임계값**
- 프레임 번호로 드롭·반복·순서 뒤바뀜을 정확히 센다 → "가끔 끊긴다"를 숫자로

**③ 비트 에러 / 패킷 손실 주입**

- 링크 중간에 감쇠기(RF) 또는 패킷 레벨 주입기(이더넷/UDP 구간이 있다면 `tc netem`)로 BER 1e-6 → 1e-4, 버스트 손실 주입 [추정 구성]
- 관측: 화면 복구 시간(패턴 프레임 번호가 다시 연속될 때까지), 디코더 에러 카운터

| 장비 | 용도 | 대략 비용 [추정] |
|---|---|---|
| 타이머 보드(MCU) + LED + 포토다이오드 | glass-to-glass 지연 분포 | $50–200 (자작) |
| HDMI/USB3 캡처 카드 | 출력 프레임 캡처 (1080p60) | $150–800 |
| 오실로스코프 (공용) | 구간 마커 검증, 리그 교정 | 공용 자산 |
| 프로그래머블 RF 감쇠기 + 실드 박스 | 링크 품질 저하 | $500–3,000 |
| 패턴 소스 (FPGA 내장 또는 HDMI 패턴 생성기) | 입력 패턴 주입 | $0–1,500 |
| 열 챔버 (공용) | 장시간 고온 동작 | 공용 자산 |
| JTAG/SWD + FPGA 프로그래머 | bitstream/FW 매트릭스 로드 | $100–1,000 |
| 리그 PC (USB3, GPU 디코드 가능) | 캡처·PSNR/SSIM 계산 | $1,500–3,000 |

**자동화 구조 (pytest)**

- session fixture: `rig`, `latency_rig`, `capture`(캡처 장치 open), `chamber`
- function fixture: `dut(bitstream, fw)` — `FpgaLoader.load()` + SWD 플래시 + 버전 레지스터 확인 → 매트릭스는 `@pytest.mark.parametrize("bitstream,fw", VERSION_MATRIX)`
- 측정 helper: `measure_latency(n=500) -> Distribution(p50, p99, max)`, `check_frames(seconds) -> FrameReport(dropped, repeated, crc_mismatch)`, `psnr_ssim(ref, captured)`
- 결과: 지연·드롭·PSNR을 `record_property` / 측정값 이벤트로 저장 → [N04 문제 C 결과 DB](../notes/2026-10-02_N04_system_design_test_tooling.md)에서 빌드별 추이 그래프. **절대 임계값 + 이전 릴리즈 대비 회귀 임계값** 둘 다 판정

**리그 수 산정** [추정]

- 지연 측정 500회 × 약 0.5초 간격 ≈ 5분, 프레임 CRC soak는 nightly 2시간, 열 soak 주 1회 4시간
- 카메라/FPGA 조합 2종 × 리비전 2 = 4 조합 → **지연+패턴 리그 4대**, 에러 주입(RF) 리그 1대, 열 챔버 공용 1대 = **6대**

## 6. 대표 테스트 케이스

| ID | 시나리오 | 자극 (stimulus) | 관측 | 합격 기준 |
|---|---|---|---|---|
| VF-01 | 부팅 시 첫 화면 | 전원 on 1,000회 (무작위 off 시간) | 첫 유효 프레임까지 시간, 실패 횟수 | 실패 0, p99 < 3s [추정] |
| VF-02 | 센서 설정 검증 | 각 모드(해상도/fps) 전환 | 센서 레지스터 덤프 vs 기대 테이블, 실측 fps | 레지스터 불일치 0, fps ±0.5% |
| VF-03 | 무압축 경로 bit-exact | 카운터 패턴 1,000만 프레임 (nightly soak) | 프레임 CRC32 | 불일치 0 |
| VF-04 | 프레임 연속성 | 패턴 2시간 스트리밍 | 프레임 번호 드롭·반복·역순 | 드롭 0 (케이블 링크 조건) |
| VF-05 | glass-to-glass 지연 | 무작위 시점 LED 500회 | 지연 분포 | p99 ≤ 기준선 + 3ms, max ≤ 기준선 + 8ms [추정] |
| VF-06 | 구간 지연 분해 | 동일 + 구간 GPIO 마커 | 센서→FPGA→송신→디코드→표시 구간별 | 어느 구간도 기준선 + 2ms 초과 없음 [추정] |
| VF-07 | 압축 화질 | 표준 동영상 클립 재생(모니터 → 카메라) | PSNR/SSIM vs 기준 | SSIM ≥ 0.90, 이전 빌드 대비 -0.02 이내 [추정] |
| VF-08 | 비트 에러 복구 | BER 1e-5 버스트 1초 | 패턴 프레임 번호 재연속 시점 | 복구 < 100ms (다음 I-frame 이내) [추정] |
| VF-09 | 링크 완전 단절 후 복귀 | 감쇠기 차단 5초 → 복구 | 화면 재개 시간, 디코더 상태 | 재개 < 1s, 디코더 재시작 불필요 |
| VF-10 | 고온 장시간 | 60°C 챔버 1시간 + 패턴 | 드롭, CSI-2 에러 카운터, FPGA 온도 | 드롭 0, 링크 에러 증가 없음 |
| VF-11 | 버전 매트릭스 | bitstream N/N-1 × FW N/N-1 × 수신기 N/N-1 | 화면·OSD·버전 협상 결과 | 지원 조합 통과, 미지원 조합은 명확한 에러 |
| VF-12 | OSD 동기 | 텔레메트리 값 계단 변화 + LED | OSD 숫자 변화 시각 vs 영상 | OSD 지연 ≤ 영상 + 1프레임 [추정] |

## 7. 면접 시나리오 — "Design a test system for our video pipeline"

### 요구사항 질문 (영어)

- "What's the pipeline — raw or compressed, digital or analog, FPGA or SoC encode? How many camera and board variants?"
- "What matters most to the pilots: latency, dropouts, or image quality? Is there a latency number we've committed to?"
- "Do we have a frame counter or test pattern mode in the FPGA I can use as a known input?"
- "Does the receive side — goggles or GCS decode — belong in scope, or just the air side?"
- "Should latency be a release gate or just a tracked metric?"

### 가정 숫자 [추정]

- 1080p60 또는 720p60, glass-to-glass 기준선 약 30ms, 회귀 허용 +3ms(p99)
- 조합 4개, 리그 6대, 지연 측정 500회 = 5분, CRC soak 2시간 nightly

### 블록도

- 5절: ① LED→카메라→…→화면→포토다이오드 지연 리그, ② 패턴→FPGA→캡처→CRC/프레임 번호, ③ 링크 중간 에러 주입, 열 챔버와 전원 사이클은 공용

### 핵심 결정과 trade-off

| 결정 | 선택 | trade-off |
|---|---|---|
| 지연 측정 방식 | LED + 포토다이오드 + μs 타이머, **분포**로 보고 | 고속 카메라는 정확하지만 자동화가 어렵다. 단일 측정값은 위상 때문에 의미 없음 |
| 정확성 판정 | 무압축 = bit-exact CRC, 압축 = 프레임 번호 + SSIM 임계 | 압축 경로에 bit-exact를 요구하면 정상 변경도 실패 처리됨 |
| 입력 | FPGA 내장 패턴 + 실제 센서 둘 다 | 패턴은 재현성, 실제 센서는 init·노출 경로 커버. 패턴만 쓰면 센서 버그를 놓침 |
| 판정 기준 | 절대 임계 + **이전 릴리즈 대비 회귀** | 절대값만 쓰면 서서히 나빠지는 걸 못 잡음 |
| 링크 구간 | 케이블 + 감쇠기 (재현성) | 실제 전파·다중 경로는 field에서만 |
| 버전 관리 | bitstream·FW·수신기 버전을 결과에 항상 기록 + 매트릭스 nightly | 조합 폭발 → 지원 조합 목록을 명시적으로 관리 |

### 실패 모드

- 디스플레이 자체 지연(헤드셋 패널)이 측정에 섞임 → 레퍼런스 디스플레이를 고정하고, 패널 교체 시 기준선 재측정
- 캡처 카드가 프레임을 떨굼 → 캡처 장치 자체 카운터와 대조, 의심 시 `infra error`
- 리그 교정 드리프트(포토다이오드 임계값) → 매일 첫 실행에 알려진 지연의 기준 경로(LED→포토다이오드 직결)로 self-check
- 측정이 느려서 PR에 못 넣음 → PR은 100회 빠른 측정 + 패턴 5분, 500회와 soak는 nightly

### 영어 2분 요약

> "I'd split the video pipeline into three questions, each with its own rig. Is it correct? A known test pattern with a frame counter goes into the FPGA path; on the raw path every captured frame must match its CRC exactly, and on the compressed path I check frame continuity and an SSIM threshold. Is it fast? An LED in front of the camera fires at random times, a photodiode on the display catches it, and a microsecond timer gives me a latency distribution — p50, p99, max — with GPIO markers so I can see which stage got slower. Is it robust? I inject bit errors and link drops in the middle and measure how quickly the picture recovers, and I run hours of soak in a thermal chamber to shake out clock-domain-crossing bugs that show up once in millions of frames. Every run records the bitstream, firmware and receiver versions, and latency is gated both on an absolute budget and on regression versus the last release."

## 8. 꼬리 질문

**Q. Why measure a distribution instead of one latency number?**

> "Latency depends on where in the exposure and display scan the event lands, so a single sample can be off by most of a frame. Firing at random times and collecting a few hundred samples gives a stable p99 that I can gate on."

**Q. How would you catch a clock-domain-crossing bug?**

> "They're rare and data-dependent, so I need volume and a bit-exact check: a counter pattern for tens of millions of frames, at temperature extremes, with CRC per frame. One mismatch is a failure, and I save the surrounding frames and FPGA error counters as evidence."

**Q. The compressed output is never bit-identical. How do you know it didn't regress?**

> "Frame continuity from the embedded counter, plus objective quality metrics — PSNR or SSIM against a reference — with both an absolute floor and a maximum drop versus the previous release."

**Q. How do you test the FPGA and firmware together when they're released separately?**

> "Each build reports its version through a register, the firmware checks compatibility at boot, and a nightly matrix runs current and previous versions of bitstream, firmware and receiver. Unsupported combinations must fail loudly, not show a corrupted picture."

**Q. Where does the MCU fit in your tests?**

> "The MCU owns power sequencing, sensor init over I2C and loading the bitstream, so it's behind the classic intermittent black screen. I power-cycle a thousand times with random off-times and dump the sensor registers on any failure."

**Q. Could you reuse this for the autonomy cameras?**

> "Yes — the same pattern and latency rig works for the MIPI path into the mission computer, and the timestamps tie into the sensor-to-actuator latency gate the autonomy platform team already wants."

## 9. Don 경험 연결 (레쥬메 범위)

| 이 노트의 포인트 | Don 레쥬메 근거 | 말할 문장 |
|---|---|---|
| FPGA + FW 공동 bring-up, 버전 정합 | Solidigm **FPGA 이미지/FW bring-up** (Cortex R8/R82/M0+, PCIe/NVMe IP), RTL freeze 전 SoC verification | "I've brought up firmware against FPGA images before RTL freeze, so I'm used to testing firmware and hardware images that change on different schedules." |
| 고속 링크 에러·복구 | PCIe 6 차세대 production FW, PCIe 3/4/5 chip reliability system | "From PCIe I'm used to thinking in error counters, link retraining and recovery time — the same lens applies to a video link." |
| 데이터 경로 무결성 | SK hynix NAND 데이터패스 FW, SoC verification(DMA, SRAM/DRAM) | "Data-path bugs are rare and data-dependent, so I test with known patterns and checksums at volume." |
| 성능 측정 | Solidigm 성능 튜닝 | (Don: 실제로 지연/처리량을 어떻게 측정했는지 — 레쥬메에 없음) |
| 갭 | 비디오 코덱, MIPI CSI-2, GStreamer 경험 없음 | 정직하게: "I haven't worked on video codecs, but the test method — known input, exact check, distribution of latency, injected errors — carries over directly." |

## 체크

- [ ] 2절 파이프라인과 지연 예산 구간을 보지 않고 그리기
- [ ] 지연 리그 원리(무작위 LED + 포토다이오드 + 분포)를 영어 30초로
- [ ] bit-exact(CRC) vs SSIM 판정이 언제 각각 맞는지 한 문장씩
- [ ] 7절 영어 2분 요약 소리 내어 2회
- [ ] [S03 ground & peripherals](2026-10-03_S03_ground_and_peripherals.md)의 헤드셋 표시 부분과 연결해서 "손 → 기체 / 카메라 → 눈" 두 지연을 함께 설명해 보기
