# G6. 그 밖의 센서 — 카메라·ISP, PPG, ToF/근접, 기압계, 온도, 터치, 착용 감지

> **이 노트를 다 읽으면**: 카메라 경로(이미지 센서 → MIPI CSI-2 → ISP → NV12 → 모델)를 바이트 단위로 계산하고 장난감 ISP를 numpy로 돌릴 수 있다 · PPG에서 모션 아티팩트를 가속도계 기준으로 빼고 심박을 추정할 수 있다 · 근접·정전용량·온도·IMU를 묶은 착용 감지 FSM을 C로 짤 수 있다 · 기압→고도, NTC→온도 변환과 센서별 전력·데이터율을 자릿수로 말할 수 있다
> **JD 연결**: "Build data collection and ingestion pipelines … various sensors, at scale", (우대) "Wearables, robotics, industrial sensing, IoT" — study_prep_list G6: 카메라(MIPI CSI, ISP 파이프라인 개요), PPG, ToF/근접, 기압계, 온도, 터치, **착용 감지** (multimodal 기기)
> **Don 기준 난이도**: I2C/SPI bring-up, 인터럽트·FIFO, ADC code 변환, hysteresis·debounce FSM, 전력 margin 계산은 이미 강하다 / 이미지 센서·ISP 단계의 의미, 광학 센서(PPG·ToF)의 물리, 모션 아티팩트 제거(adaptive filter), 여러 약한 센서를 묶는 퓨전 설계는 새로 배운다
> **선행 노트**: G1(IMU 원리), G5(DSP 기초 — 필터·FFT), B6(비전 모델 — 카메라 → 텐서, always-on 비전 전력), B7(IMU 모델 — 착용 감지, hysteresis FSM), E8(센서 허브)

---

## 0. 큰 그림 — 이게 왜 필요한가

G1~G5는 웨어러블 AI 기기의 "주연" 센서인 IMU와 마이크를 다뤘다. 이 노트는 나머지 조연들이다. 조연이지만 하루 종일 켜져 있는 것은 오히려 이쪽이다. "지금 차고 있나?"(근접·정전용량·온도), "심박은?"(PPG), "몇 층 올라갔나?"(기압계), "사용자가 뭘 보고 있나?"(카메라)는 모두 **ML 모델에 들어가기 전의 입력 품질**과 **언제 큰 모델을 깨울지**를 결정한다.

예를 들어 Hark 같은 웨어러블이라면(추정), 센서들은 대략 이렇게 붙어 있을 것이다.

```
                         ┌──────────── always-on 도메인 (µA ~ 수 mW) ─────────────┐
 IMU (SPI, FIFO) ───────►│                                                          │
 PPG AFE (SPI/I2C) ─────►│   센서 허브 MCU / always-on island (E8)                  │
 근접·ToF (I2C) ────────►│   - 착용 감지 FSM (§8)       - 심박 추정 (§3)            │
 기압계 (I2C) ──────────►│   - 층 오르기·낙상 보조 (§5) - 터치 baseline (§7)        │
 온도 NTC (ADC) ────────►│   - 로그 batching (H1)                                    │
 정전용량 터치 ─────────►│                                                          │
                         └───────────────┬──────────────────────────────────────────┘
                                         │ 이벤트 / 인터럽트 (드물게)
 카메라 (MIPI CSI-2) ───────────────►  큰 SoC: ISP → NPU/DSP → 음성·비전 AI, 무선
```

펌웨어 관점에서 정리하면, 이 노트의 센서들은 세 부류다.

| 부류 | 센서 | 펌웨어가 하는 일 | ML과의 관계 |
|---|---|---|---|
| 고대역폭 · 고전력 | 카메라 | CSI-2 링크, ISP 설정, 버퍼 관리 | 모델 입력 자체 (B6) |
| 광학 · 생체 | PPG, ToF/근접 | LED 전류·노출 제어, AFE 설정, 아티팩트 제거 | 특징(심박, 거리) 또는 게이트 |
| 저속 · 환경 | 기압계, 온도, 터치, ALS | ADC 변환, baseline·보상, 문턱 | 컨텍스트, 착용 판정, 퓨전 입력 |

모든 센서 데이터는 **합성(synthetic)** 이고, 전력·성능 수치는 별도 표시가 없으면 **자릿수 감각용 가정값**이다. 실제 부품 데이터시트 값이 아니다.

---

## 1. 카메라 — 이미지 센서와 MIPI CSI-2

### 1.1 직관: 이미지 센서는 "빛 → 전하 → 숫자"를 하는 거대한 ADC 배열이다

이미지 센서는 수백만 개의 **photodiode**(빛을 받으면 전하를 모으는 소자)가 격자로 놓인 칩이다. 노출 시간 동안 각 픽셀이 전하를 모으고, 줄(row) 단위로 읽어서 열(column)마다 붙은 ADC가 숫자로 바꾼다. 출력은 **RAW** — 픽셀 하나당 숫자 하나(보통 10~12 bit)다. 색은 아직 없다. 각 픽셀 위에 빨강·초록·파랑 중 하나만 통과시키는 색 필터가 얹혀 있을 뿐이다(§2.2 Bayer).

Don에게 익숙한 말로 하면, 이미지 센서는 "열 수만큼 병렬 ADC를 가진 수백만 채널 데이터 수집 칩"이고, 설정 레지스터(노출, analog gain, 해상도, frame rate)는 I2C(카메라 쪽에서는 **CCI**, Camera Control Interface라고 부른다)로 쓴다. 데이터는 별도의 고속 직렬 링크(MIPI CSI-2)로 나온다. 제어는 느린 버스, 데이터는 빠른 버스 — SSD의 사이드밴드 SMBus와 PCIe 관계와 같다.

### 1.2 센서를 고를 때 보는 숫자

| 항목 | 뜻 | 웨어러블에서의 의미 |
|---|---|---|
| 해상도 | 가로×세로 픽셀 (예: 640×480 VGA, 1920×1080, 12MP) | ML 입력은 보통 96~512 정도라 고해상도는 사진·VLM용 |
| pixel size | 픽셀 한 변의 크기 (µm) | 작으면 칩이 작지만 빛을 덜 모아 어두운 곳 노이즈가 커진다 |
| frame rate | 초당 프레임 (fps) | 전력·대역폭이 거의 비례. always-on은 1 fps 이하도 흔하다 (B6 §9) |
| shutter | rolling / global | 아래 1.3 |
| bit depth | RAW 비트 수 (8/10/12) | dynamic range와 링크 대역폭 |
| 출력 | RAW Bayer / YUV / mono | 센서 안에 작은 ISP가 있으면 YUV를 바로 내기도 한다 |
| 인터페이스 | MIPI CSI-2 / parallel(DVP) / SPI | 저전력 QVGA 센서는 parallel·SPI로 MCU에 바로 붙기도 한다 |

### 1.3 Rolling shutter vs global shutter

- **Rolling shutter**: 줄마다 노출 시작 시각이 조금씩 늦다. 위 줄과 아래 줄이 다른 순간을 찍는다. 회로가 단순하고 픽셀이 작고 싸서 거의 모든 휴대폰·웨어러블 카메라가 이쪽이다. 대신 빠르게 움직이는 물체나 **머리를 돌리는 동안**(안경형 기기!) 찍으면 기울어지거나 휘어진 그림(skew, "jello")이 나온다.
- **Global shutter**: 모든 픽셀이 같은 순간에 노출되고, 픽셀마다 전하를 잠깐 보관할 저장소가 있다. 움직임 왜곡이 없어 머신 비전·로봇·hand tracking에 좋지만 픽셀이 커지고 비싸다.

손으로 감 잡기: 1080줄을 30 fps에서 한 프레임 시간 동안 다 읽는다면 줄당 약 33.3 ms / 1080 ≈ 31 µs다. 위에서 아래까지 33 ms 차이. 그 사이 머리가 초당 100°로 돌면 3.3°가 어긋난다. 그래서 IMU와 카메라의 **타임스탬프 정렬**(G7)이 rolling shutter 보정의 출발점이다.

### 1.4 MIPI CSI-2 — 높은 수준에서

MIPI CSI-2(Camera Serial Interface 2)는 카메라 → 프로세서 단방향 고속 직렬 프로토콜이다. 물리층은 주로 **D-PHY**(클럭 lane 1개 + 데이터 lane 1~4개, 차동쌍) 또는 **C-PHY**(3선 trio)다.

```
 이미지 센서                                                SoC (CSI-2 receiver → ISP)
 ┌─────────┐  clock lane  ───────────────────────────────►  ┌─────────┐
 │ CSI-2 TX│  data lane 0 ───────────────────────────────►  │ CSI-2 RX│
 │         │  data lane 1 ───────────────────────────────►  │         │
 └─────────┘  I2C (CCI)   ◄──────── 레지스터 설정 ──────── └─────────┘

 한 프레임 = 패킷의 연속
 [FS short pkt] [LS] [long pkt: header(DataID, WordCount, ECC) | line payload | CRC] [LE] ... [FE short pkt]
   FS/FE = frame start/end, LS/LE = line start/end (선택), DataID = 가상 채널 + data type(RAW10, YUV422 …)
```

- **long packet** 하나가 보통 한 줄(line)이다. header에 data type(예: RAW10)과 바이트 수, ECC가 있고, 끝에 CRC가 붙는다. Don이 PCIe TLP나 NVMe 명령 구조를 읽던 방식 그대로 읽으면 된다.
- **virtual channel**: 한 링크 위에 여러 스트림(예: 이미지 + 메타데이터, 또는 두 센서)을 섞는 태그다.
- **RAW10 packing**: 픽셀 4개(40 bit)를 5바이트에 넣는다. 앞 4바이트는 각 픽셀의 상위 8 bit, 다섯째 바이트에 네 픽셀의 하위 2 bit를 모은다. 그래서 RAW10은 픽셀당 1.25 B다.
- bring-up에서 흔히 보는 문제는 Don의 PCIe 경험과 닮았다: lane 수·lane rate 불일치, 클럭 연속/비연속 모드 설정, CRC/ECC 에러 카운터 증가, 신호 무결성. 이미지가 "깨져서" 나오면 receiver의 에러 레지스터부터 본다.

### 1.5 프레임 크기와 링크 대역폭 — 손계산 후 코드

손으로: 1920×1080 RAW10 한 장 = 2,073,600 픽셀 × 1.25 B = 2,592,000 B ≈ 2.6 MB. 30 fps면 약 78 MB/s ≈ 622 Mbit/s다. NV12로 바꾸면 픽셀당 1.5 B(§2.5)라서 한 장 3.1 MB.

이 코드는 해상도·포맷별 프레임 크기와, RAW10 스트림에 필요한 D-PHY lane 수를 계산한다.

```python
# 프레임 1장의 바이트 수(포맷별)와 MIPI CSI-2 D-PHY lane 수 감각
fmts = {"RAW8": 1.0, "RAW10": 1.25, "NV12": 1.5, "YUYV": 2.0, "RGB888": 3.0}
res = {"QVGA": (320, 240), "VGA": (640, 480), "1080p": (1920, 1080), "12MP": (4032, 3024)}
print("%-7s" % "B/frame" + "".join("%12s" % f for f in fmts))
for name, (w, h) in res.items():
    print("%-7s" % name + "".join("%12s" % format(int(w * h * b), ",") for b in fmts.values()))
# RAW10 링크 대역폭: blanking·패킷 헤더 등 오버헤드 20% 가정
for name, w, h, fps in [("1080p30", 1920, 1080, 30), ("12MP30", 4032, 3024, 30)]:
    mbps = w * h * fps * 10 / 1e6 * 1.2
    lanes = [-(-mbps // r) for r in (800, 1500, 2500)]
    print("%-8s RAW10 %5.0f Mbit/s -> lanes @0.8/1.5/2.5 Gbps: %d/%d/%d" % (name, mbps, *lanes))
```

```text
B/frame        RAW8       RAW10        NV12        YUYV      RGB888
QVGA         76,800      96,000     115,200     153,600     230,400
VGA         307,200     384,000     460,800     614,400     921,600
1080p     2,073,600   2,592,000   3,110,400   4,147,200   6,220,800
12MP     12,192,768  15,240,960  18,289,152  24,385,536  36,578,304
1080p30  RAW10   746 Mbit/s -> lanes @0.8/1.5/2.5 Gbps: 1/1/1
12MP30   RAW10  4389 Mbit/s -> lanes @0.8/1.5/2.5 Gbps: 6/3/2
```

출력에서 볼 것:

- QVGA RAW8 한 장은 75 KB로 MCU SRAM에 들어가지만, 1080p NV12 한 장은 3.1 MB라서 DRAM이 있는 SoC가 필요하다. **"카메라를 MCU에 붙일 수 있나"는 해상도가 결정한다.**
- 12MP 30 fps는 lane당 0.8 Gbps로는 6 lane이 필요한데, D-PHY CSI-2 링크는 보통 최대 4 lane이다. 그래서 고해상도 센서는 lane rate가 높은 PHY를 쓰거나 frame rate를 낮춘다. 오버헤드 20%는 설명용 가정이다(실제는 blanking 설정에 따라 다르다).

### 1.6 흔한 함정

- 링크 대역폭만 보고 **ISP 처리량**과 **DRAM 대역폭**을 빼먹는다. 프레임은 링크 → ISP → DRAM → NPU로 여러 번 읽고 쓰인다 (D2, E7).
- 노출 시간과 frame rate를 혼동한다. 30 fps여도 어두운 곳에서 노출이 33 ms까지 늘어나면 motion blur가 생기고, 그 이상은 frame rate가 떨어진다.
- 센서 레지스터 시퀀스(벤더 제공 init table)를 "블랙박스"로 두고 수정한다. PLL·lane rate·blanking이 얽혀 있어서 한 값만 바꾸면 링크가 깨진다.

---

## 2. ISP — RAW를 사람이 볼 수 있는 그림으로

### 2.1 직관: ISP는 고정 기능 DSP 파이프라인이다

ISP(Image Signal Processor)는 RAW Bayer 데이터를 RGB/YUV 이미지로 바꾸는 **하드웨어 파이프라인**이다. 단계마다 정해진 일을 하는 블록이 줄지어 있고, 펌웨어(또는 SoC 벤더의 카메라 스택)가 각 블록의 계수를 프레임마다 다시 쓴다. Don의 경험으로 비유하면 "SSD 컨트롤러의 고정 기능 ECC·암호화 엔진 체인 + 그 계수를 갱신하는 펌웨어"와 같은 구조다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="g6a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="30" width="90" height="56" rx="6" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="55" y="54" font-size="13" text-anchor="middle">image sensor</text>
  <text x="55" y="72" font-size="12" text-anchor="middle">RAW10 Bayer</text> <line x1="100" y1="58" x2="128" y2="58" stroke="currentColor" stroke-width="1.5" marker-end="url(#g6a)"/>
  <text x="114" y="48" font-size="12" text-anchor="middle">CSI-2</text> <rect x="130" y="20" width="430" height="80" rx="6" fill="none" stroke="#e08a3c" stroke-width="2"/>
  <text x="345" y="15" font-size="13" text-anchor="middle">ISP (고정 기능 블록 체인)</text> <text x="160" y="50" font-size="12" text-anchor="middle">BLC</text> <text x="210" y="50" font-size="12" text-anchor="middle">LSC</text>
  <text x="270" y="50" font-size="12" text-anchor="middle">demosaic</text> <text x="330" y="50" font-size="12" text-anchor="middle">WB</text> <text x="380" y="50" font-size="12" text-anchor="middle">CCM</text>
  <text x="435" y="50" font-size="12" text-anchor="middle">gamma</text> <text x="510" y="50" font-size="12" text-anchor="middle">NR · sharpen</text>
  <text x="300" y="80" font-size="12" text-anchor="middle">→ tone map → RGB→YUV → scaler (여러 출력)</text> <line x1="560" y1="60" x2="588" y2="60" stroke="currentColor" stroke-width="1.5" marker-end="url(#g6a)"/>
  <rect x="590" y="35" width="80" height="50" rx="6" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="630" y="58" font-size="13" text-anchor="middle">NV12</text>
  <text x="630" y="75" font-size="12" text-anchor="middle">DRAM</text> <path d="M345,100 L345,140 L120,140 L120,92" fill="none" stroke="#888" stroke-width="1.5" stroke-dasharray="5,4" marker-end="url(#g6a)"/>
  <text x="235" y="158" font-size="12" text-anchor="middle">3A 통계 → AE/AWB/AF → 노출·gain (I2C)</text> <line x1="630" y1="85" x2="630" y2="175" stroke="currentColor" stroke-width="1.5"/>
  <line x1="630" y1="175" x2="470" y2="175" stroke="currentColor" stroke-width="1.5" marker-end="url(#g6a)"/> <line x1="630" y1="175" x2="630" y2="205" stroke="currentColor" stroke-width="1.5" marker-end="url(#g6a)"/>
  <rect x="330" y="160" width="138" height="34" rx="6" fill="none" stroke="#888" stroke-width="1.5"/> <text x="399" y="182" font-size="12" text-anchor="middle">display · 인코더</text>
  <rect x="540" y="207" width="130" height="50" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="605" y="228" font-size="12" text-anchor="middle">resize → 정규화</text>
  <text x="605" y="246" font-size="12" text-anchor="middle">→ NPU 모델 (B6)</text> <rect x="10" y="207" width="110" height="50" rx="6" fill="none" stroke="#4a7bd0" stroke-width="1.5" stroke-dasharray="4,3"/>
  <text x="65" y="228" font-size="12" text-anchor="middle">QVGA mono 센서</text> <text x="65" y="246" font-size="12" text-anchor="middle">(저전력)</text>
  <line x1="120" y1="232" x2="198" y2="232" stroke="currentColor" stroke-width="1.5" marker-end="url(#g6a)"/> <text x="160" y="224" font-size="12" text-anchor="middle">SPI</text>
  <rect x="200" y="210" width="150" height="45" rx="6" fill="none" stroke="#4a7bd0" stroke-width="1.5" stroke-dasharray="4,3"/> <text x="275" y="237" font-size="12" text-anchor="middle">MCU: person 감지</text>
</svg>
```

그림 1 — 웨어러블 카메라 경로. 위는 SoC의 정식 경로(센서 → CSI-2 → ISP → NV12 → display·인코더와 ML), 아래 점선은 B6 §9의 저전력 경로(ISP 없는 작은 흑백 센서 → MCU). 회색 점선은 ISP가 모은 통계로 노출·gain을 다시 쓰는 3A 피드백 루프다.

### 2.2 단계별로 — 무엇을 왜 하나

| 단계 | 하는 일 | 왜 필요한가 |
|---|---|---|
| BLC (black level correction) | 모든 픽셀에서 offset(예: RAW10에서 64)을 뺀다 | 빛이 0이어도 ADC 출력이 0이 아니다. 빼야 0 = 검정 |
| LSC (lens shading correction) | 가장자리 픽셀에 gain을 더 준다 | 렌즈 특성상 가장자리가 어둡다 (vignetting) |
| demosaic | Bayer(픽셀당 색 1개) → 픽셀당 RGB 3개 | 빠진 두 색을 이웃에서 보간 |
| WB (white balance) | R, G, B에 다른 gain | 조명 색(백열등은 주황)을 지워 흰 것이 희게 |
| CCM (color correction matrix) | 3×3 행렬곱 | 센서 색 필터의 겹침을 표준 색공간(sRGB)으로 |
| gamma / tone map | 비선형 곡선 | 사람 눈은 어두운 쪽에 민감 → 8 bit를 효율적으로 쓰기 |
| NR (denoise) | 공간·시간 노이즈 제거 | 작은 픽셀, 어두운 곳의 노이즈 |
| sharpen | 경계 강조 | 렌즈·demosaic로 흐려진 경계 보정 (사람 눈용) |
| RGB→YUV, scaler | 색공간 변환, 축소 출력 | 인코더·display·ML이 원하는 크기와 포맷 |
| 3A 통계 | 밝기 히스토그램, 색 평균, 초점 점수 | AE(노출)·AWB(화이트밸런스)·AF(초점) 알고리즘의 입력 |

**Bayer RGGB**: 2×2 블록마다 R 1개, G 2개, B 1개를 놓는 배치다. G가 두 배인 이유는 사람 눈이 초록(밝기)에 가장 민감해서다. 첫 줄이 R G R G…, 둘째 줄이 G B G B…이면 RGGB라고 부른다(센서마다 GRBG, BGGR 등 시작점이 다르다 — bring-up에서 색이 이상하면 이것부터 의심한다).

### 2.3 손으로 계산 — 한 픽셀의 여정

아래 그림의 숫자는 2.4절 코드가 만든 합성 RAW10의 왼쪽 위 4×4다. 장면은 "회색(0.5, 0.5, 0.5) 벽을 따뜻한 조명(R 1.0, G 0.8, B 0.5)이 비추는" 상황이다.

```svg
<svg viewBox="0 0 640 250" xmlns="http://www.w3.org/2000/svg">
  <g stroke="currentColor" stroke-width="1">
  <rect x="20" y="20" width="50" height="50" fill="#d0564a" fill-opacity="0.25"/><rect x="70" y="20" width="50" height="50" fill="#3f9a6b" fill-opacity="0.25"/><rect x="120" y="20" width="50" height="50" fill="#d0564a" fill-opacity="0.25"/><rect x="170" y="20" width="50" height="50" fill="#3f9a6b" fill-opacity="0.25"/>
  <rect x="20" y="70" width="50" height="50" fill="#3f9a6b" fill-opacity="0.25"/><rect x="70" y="70" width="50" height="50" fill="#4a7bd0" fill-opacity="0.25"/><rect x="120" y="70" width="50" height="50" fill="#3f9a6b" fill-opacity="0.25"/><rect x="170" y="70" width="50" height="50" fill="#4a7bd0" fill-opacity="0.25"/>
  <rect x="20" y="120" width="50" height="50" fill="#d0564a" fill-opacity="0.25"/><rect x="70" y="120" width="50" height="50" fill="#3f9a6b" fill-opacity="0.25"/><rect x="120" y="120" width="50" height="50" fill="#d0564a" fill-opacity="0.25"/><rect x="170" y="120" width="50" height="50" fill="#3f9a6b" fill-opacity="0.25"/>
  <rect x="20" y="170" width="50" height="50" fill="#3f9a6b" fill-opacity="0.25"/><rect x="70" y="170" width="50" height="50" fill="#4a7bd0" fill-opacity="0.25"/><rect x="120" y="170" width="50" height="50" fill="#3f9a6b" fill-opacity="0.25"/><rect x="170" y="170" width="50" height="50" fill="#4a7bd0" fill-opacity="0.25"/>
  </g> <rect x="120" y="120" width="50" height="50" fill="none" stroke="#d0564a" stroke-width="3"/> <g font-size="13" text-anchor="middle">
  <text x="45" y="42">R</text><text x="95" y="42">G</text><text x="145" y="42">R</text><text x="195" y="42">G</text>
  <text x="45" y="60">544</text><text x="95" y="60">447</text><text x="145" y="60">545</text><text x="195" y="60">448</text>
  <text x="45" y="92">G</text><text x="95" y="92">B</text><text x="145" y="92">G</text><text x="195" y="92">B</text>
  <text x="45" y="110">446</text><text x="95" y="110">301</text><text x="145" y="110">446</text><text x="195" y="110">304</text>
  <text x="45" y="142">R</text><text x="95" y="142">G</text><text x="145" y="142">R</text><text x="195" y="142">G</text>
  <text x="45" y="160">542</text><text x="95" y="160">447</text><text x="145" y="160">544</text><text x="195" y="160">450</text>
  <text x="45" y="192">G</text><text x="95" y="192">B</text><text x="145" y="192">G</text><text x="195" y="192">B</text>
  <text x="45" y="210">449</text><text x="95" y="210">304</text><text x="145" y="210">446</text><text x="195" y="210">302</text> </g> <text x="120" y="240" font-size="12" text-anchor="middle">행·열 0~3, 굵은 칸 = (2,2)의 R</text>
  <g font-size="13"> <text x="250" y="40">(2,2)에서 RGB 만들기 (RAW10, black level 64, full 1023)</text> <text x="250" y="68">R = 544 (자기 값)</text> <text x="250" y="92">G̃ = 위·아래·좌·우 평균 = (446+446+447+450)/4 = 447.25</text>
  <text x="250" y="116">B̃ = 대각 4개 평균 = (301+304+304+302)/4 = 302.75</text> <text x="250" y="144">BLC: (v − 64)/959 → R 0.500, G 0.400, B 0.249</text> <text x="250" y="168">WB gain (0.8, 1.0, 1.6) → 0.400, 0.400, 0.398</text>
  <text x="250" y="192">gamma 1/2.2 → 0.400^(1/2.2) = 0.659 → 8-bit 168</text> <text x="250" y="222">회색 벽이 회색(168,168,168)으로 돌아왔다</text> </g>
</svg>
```

그림 2 — RGGB Bayer RAW10 4×4와 (2,2) 픽셀의 손계산. 빨강·초록·파랑 칸은 그 픽셀 위의 색 필터다. bilinear demosaic는 빠진 G를 십자 이웃 4개, 빠진 B를 대각 이웃 4개의 평균으로 채운다.

말로 하면: RAW만 보면 R(544)이 B(301)의 거의 두 배라 그림이 주황색이다. 조명이 주황이기 때문이다. black level을 빼고, 조명을 상쇄하는 gain(R은 줄이고 B는 키우기)을 곱하면 세 값이 0.40으로 같아진다 — 회색이다. 마지막 gamma는 0.40을 168/255로 "밝게 펴서" 8 bit에 담는다.

### 2.4 코드로 확인 — 장난감 ISP

이 코드는 8×8 합성 장면(왼쪽 회색, 오른쪽 주황)을 RGGB RAW10으로 "찍고", BLC → WB → bilinear demosaic → gamma를 거쳐 정답과 비교한다. CCM은 이 합성 센서에서는 색 섞임이 없으므로 생략(단위행렬)했다.

```python
# 장난감 ISP: 합성 장면 -> (센서) RGGB Bayer RAW10 -> black level -> WB -> demosaic -> gamma
import numpy as np
from scipy.ndimage import convolve
rng = np.random.default_rng(0)
H, W = 8, 8
scene = np.zeros((H, W, 3)); scene[:, :4] = [0.5, 0.5, 0.5]; scene[:, 4:] = [0.6, 0.25, 0.15]  # 회색 | 주황
illum = np.array([1.0, 0.8, 0.5])               # 따뜻한 조명: 파랑이 약하다 (합성 가정)
mask = np.zeros((H, W, 3)); mask[0::2, 0::2, 0] = 1; mask[0::2, 1::2, 1] = 1; mask[1::2, 0::2, 1] = 1; mask[1::2, 1::2, 2] = 1
BL, FULL = 64, 1023                             # RAW10, black level 64
raw = np.clip(np.round(BL + (scene * illum * mask).sum(2) * (FULL - BL) + rng.normal(0, 2, (H, W))), 0, FULL)
print("RAW10 top-left 4x4 (R G / G B):\n", raw[:4, :4].astype(int))
x = (raw - BL) / (FULL - BL)                    # 1) black level 빼고 0~1
kG = np.array([[0, 1, 0], [1, 4, 1], [0, 1, 0]]) / 4
kRB = np.array([[1, 2, 1], [2, 4, 2], [1, 2, 1]]) / 4
def demosaic(m):                                # 3) bilinear: 빈 칸을 이웃 평균으로
    return np.stack([convolve(m * mask[..., c], k, mode="mirror") for c, k in zip(range(3), (kRB, kG, kRB))], 2)
gains = 0.8 / illum                             # 2) WB: G=1 기준 이득 (조명을 안다고 가정)
print("WB gains R,G,B =", gains)
no_wb = demosaic(x); rgb = np.clip(demosaic(x * (mask * gains).sum(2)), 0, 1)
out = np.round(255 * rgb ** (1 / 2.2)).astype(int)   # 4) gamma 1/2.2 -> 8-bit
truth = np.round(255 * (0.8 * scene) ** (1 / 2.2)).astype(int)
for name, (r, c) in [("gray  ", (3, 1)), ("orange", (3, 6)), ("edge  ", (3, 4))]:
    print(name, "noWB", np.round(no_wb[r, c], 3), "WB", np.round(rgb[r, c], 3), "-> 8bit", out[r, c], "truth", truth[r, c])
err = np.abs(out - truth).max(2)
print("max |err| per column:", err.max(0))
```

```text
RAW10 top-left 4x4 (R G / G B):
 [[544 447 545 448]
 [446 301 446 304]
 [542 447 544 450]
 [449 304 446 302]]
WB gains R,G,B = [0.8 1.  1.6]
gray   noWB [0.5  0.4  0.25] WB [0.4 0.4 0.4] -> 8bit [168 168 168] truth [168 168 168]
orange noWB [0.6   0.198 0.075] WB [0.48  0.198 0.12 ] -> 8bit [183 122  97] truth [183 123  97]
edge   noWB [0.599 0.199 0.162] WB [0.479 0.199 0.259] -> 8bit [183 122 138] truth [183 123  97]
max |err| per column: [ 1  1  1 10 41  1  1  3]
```

출력에서 볼 것:

- RAW 4×4는 그림 2와 같은 숫자다. **WB 전**(noWB)의 회색 픽셀은 (0.5, 0.4, 0.25) — 주황빛이다. WB 후 (0.4, 0.4, 0.4)가 된다.
- 내부 픽셀은 8-bit 오차 1~3이지만, 회색|주황 **경계 열(3, 4)에서 오차가 10, 41**로 튄다. 경계 픽셀 (3,4)의 B가 0.259로, 정답 0.12의 두 배다. bilinear demosaic는 경계 건너편 픽셀까지 평균을 내기 때문에 생기는 **color fringe(색 번짐)** 다. 실제 ISP는 edge 방향을 보고 보간하는(edge-aware) demosaic로 이것을 줄인다.
- WB gain을 "조명을 안다"고 가정하고 넣었다. 실제로는 이것을 **AWB 알고리즘**이 3A 통계로 추정한다(예: gray-world — 장면 평균이 회색이라고 가정). 틀리면 사진 전체 색이 틀어진다.

### 2.5 RAW, YUV, NV12 — 포맷과 메모리 레이아웃

- **RGB888**: 픽셀당 3 B, 보통 R G B R G B… 교차(interleaved). ML 입력(B6 §1)과 가장 가깝지만 가장 크다.
- **YUV**: Y = 밝기(luma), U·V = 색차(chroma). 사람 눈은 색보다 밝기 해상도에 민감하므로 U·V를 줄여서 저장한다. **4:2:0**은 2×2 픽셀이 U·V 한 쌍을 공유한다 → 픽셀당 1 + 0.5 = 1.5 B.
- **NV12**: 4:2:0의 대표 메모리 배치. 앞에 **Y 평면**(W×H 바이트)이 통째로 있고, 뒤에 **UV 교차 평면**(W×H/2 바이트: U0 V0 U1 V1 …)이 온다. 카메라·비디오 인코더·NPU 전처리가 가장 흔하게 주고받는 포맷이다.

```
 4×4 NV12 버퍼 (24 B)
 offset  0 ┌ Y00 Y01 Y02 Y03 ┐
         4 │ Y10 Y11 Y12 Y13 │  Y 평면: 픽셀마다 1 B, row-major
         8 │ Y20 Y21 Y22 Y23 │
        12 └ Y30 Y31 Y32 Y33 ┘
        16 ┌ U0  V0  U1  V1  ┐  UV 평면: 2×2 블록마다 (U,V) 1쌍
        20 └ U2  V2  U3  V3  ┘
 픽셀 (r,c)의 Y = base + r·W + c,   UV 쌍 = base + W·H + (r/2)·W + (c/2)·2
```

이 코드는 4×4 RGB를 BT.601 식으로 YUV로 바꾸고 NV12 버퍼를 만들어 오프셋 공식을 확인한다.

```python
# NV12 메모리 레이아웃: 4x4 RGB -> Y 평면(16 B) + 교차 UV 평면(8 B). ML은 Y만 읽을 수 있다
import numpy as np
rng = np.random.default_rng(1)
rgb = rng.integers(0, 256, (4, 4, 3)).astype(float)
Y = 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]          # BT.601 luma (full range)
U = -0.169 * rgb[..., 0] - 0.331 * rgb[..., 1] + 0.5 * rgb[..., 2] + 128
V = 0.5 * rgb[..., 0] - 0.419 * rgb[..., 1] - 0.081 * rgb[..., 2] + 128
Uq = U.reshape(2, 2, 2, 2).mean((1, 3)); Vq = V.reshape(2, 2, 2, 2).mean((1, 3))  # 2x2 블록 평균 = 4:2:0
uv = np.stack([Uq, Vq], -1).reshape(2, 4)                                    # U0 V0 U1 V1 ...
buf = np.concatenate([Y.ravel(), uv.ravel()]).round().clip(0, 255).astype(np.uint8)
print("NV12 bytes =", buf.size, "= 4x4x1.5")
print("Y plane  [0:16] :", buf[:16])
print("UV plane [16:24]:", buf[16:])
print("pixel (2,3): Y at offset", 2 * 4 + 3, "-> UV pair at offset", 16 + (2 // 2) * 4 + (3 // 2) * 2)
```

```text
NV12 bytes = 24 = 4x4x1.5
Y plane  [0:16] : [135  81 212 166 152 143  36 199 125  87 106  79 114  45  61  83]
UV plane [16:24]: [117 132 116 108 148 188 185 106]
pixel (2,3): Y at offset 11 -> UV pair at offset 22
```

출력에서 볼 것: 버퍼는 24 B = 4×4×1.5이고, 앞 16 B가 그대로 흑백 이미지다. 픽셀 (2,3)의 Y는 오프셋 11, UV 쌍은 16 + 1·4 + 1·2 = 22다. **흑백 모델이라면 NV12의 앞부분만 DMA로 가져가면 된다** — 변환 비용 0이다.

### 2.6 ML은 ISP 단계를 건너뛸 수 있나? (hedge 포함)

짧은 답: **일부는 건너뛸 수 있다. 다만 "학습할 때 본 파이프라인과 추론할 때의 파이프라인이 같아야 한다"는 조건이 붙는다.**

| ISP 단계 | 사람 눈용인가, 모델에도 중요한가 | ML 관점 (일반론, 상황마다 다름) |
|---|---|---|
| AE (노출 제어) | 둘 다 | **건너뛸 수 없다.** 과노출·저노출은 정보 자체를 잃는다 |
| BLC | 둘 다 | 싸고 필수에 가깝다. 안 빼면 어두운 영역의 분포가 틀어진다 |
| demosaic | 반반 | 흑백 모델이면 Y만 쓰거나 Bayer를 2×2 binning해서 우회 가능 |
| WB, CCM | 주로 사람 | 색이 중요한 과제(신호등, 피부)가 아니면 영향이 작을 수 있다. 단, 학습 데이터와 일관돼야 한다 |
| gamma / tone map | 주로 사람 | 모델은 선형이든 비선형이든 배울 수 있지만, 학습 데이터(대부분 sRGB 사진)와 같은 곡선이어야 한다 |
| denoise, sharpen | 주로 사람 | 모델에 불필요하거나 오히려 해로울 수 있다는 보고가 있다. 끄면 전력·지연이 준다 |
| scaler | 둘 다 | ML 입력 크기로 바로 줄이는 출력 포트를 쓰면 DRAM 트래픽이 크게 준다 |

- 공개 데이터셋(ImageNet, COCO)은 **ISP를 거친 sRGB 사진**이다. RAW나 "반쯤 처리된" 이미지에 그 모델을 그대로 쓰면 **분포 이동(distribution shift)** 이 생긴다. 그래서 ISP 일부를 끄려면 그 파이프라인으로 다시 찍은 데이터로 fine-tune하거나, 학습 데이터에 같은 변환을 적용해야 한다.
- 연구 쪽에는 RAW 입력으로 직접 학습하거나 ISP와 모델을 함께 최적화하는 시도들이 있다. 제품에서 얼마나 쓰이는지는 회사마다 다르고 공개 정보가 적다 — 인터뷰에서는 "가능하지만 데이터 파이프라인을 같이 바꿔야 한다"로 말하는 것이 안전하다.
- 현실적인 절충: ML 전용 **저해상도 출력 포트**(ISP scaler의 두 번째 출력)를 Y 또는 NV12로 받아 B6 §1.4처럼 정규화를 양자화 파라미터에 흡수한다.

### 2.7 프라이버시와 전력

- **프라이버시**: 카메라가 달린 웨어러블은 주변 사람에게 "지금 찍고 있다"를 알려야 한다. 흔한 설계 원칙은 **녹화 표시 LED를 펌웨어가 아니라 하드웨어로 카메라 전원·스트림과 묶는 것**(소프트웨어 버그나 해킹으로 LED만 끌 수 없게)이다. 구체 구현은 제품마다 다르다. 데이터 측면은 H6(온디바이스 익명화, 보관 기간), UX 측면은 N4(always-listening·always-watching 신뢰)와 연결된다.
- **전력**: B6 §9.2의 계산이 핵심이다. 1 fps QVGA + MCU 추론은 약 1 mW, 큰 SoC를 5%만 깨워도 16 mW. 카메라 경로의 전력은 센서 자체보다 **ISP + DRAM + NPU를 깨우는 횟수**가 지배한다. 그래서 always-on 비전은 ISP 없는 작은 흑백 센서 → MCU(그림 1 아래 경로)로 하고, 정식 경로는 이벤트가 있을 때만 켠다.

---

## 3. PPG — 빛으로 맥박 재기

### 3.1 직관: 혈관이 부풀 때마다 빛을 조금 더 먹는다

PPG(photoplethysmography, 광용적맥파)는 **LED로 피부를 비추고 photodiode(PD)로 돌아오는 빛을 재는** 센서다. 심장이 뛸 때마다 동맥에 피가 밀려 들어와 혈관이 조금 부풀고, 피(헤모글로빈)가 빛을 흡수하므로 PD에 돌아오는 빛이 맥박에 맞춰 조금 줄었다 늘었다 한다. 스마트워치 뒷면에서 깜빡이는 초록 불이 이것이다.

- **DC 성분**: 피부·뼈·정맥 피·조직이 흡수하고 남은 빛. 크고 거의 일정하다.
- **AC 성분**: 동맥 박동에 따른 변화. DC의 **약 0.1~수 %** 수준으로 아주 작다. 이 비율을 perfusion index(PI)라고 부른다.
- 그 위에 호흡(기저선이 천천히 흔들림), **움직임**(센서와 피부 사이 간격·압력이 바뀜), 주변광이 더해진다.

신호처리 관점에서는 "큰 DC 위에 1% 크기의 0.5~3 Hz 주기 신호, 그리고 같은 대역에 들어오는 큰 간섭(모션)"이다. 문제의 대부분이 모션이다.

### 3.2 파장 선택 — 초록 vs 빨강/IR

| 파장 | 대략 | 특징 | 주 용도 |
|---|---|---|---|
| 초록 | 약 520~530 nm | 헤모글로빈 흡수가 커서 AC가 크고, 얕게 들어가 모션·주변광 영향이 상대적으로 작다 | 손목 심박 |
| 빨강 | 약 660 nm | 산소와 결합한 헤모글로빈(HbO₂)과 아닌 것(Hb)의 흡수 차이가 크다 | SpO2 (IR과 짝) |
| IR | 약 880~940 nm | 깊이 들어가고, 흡수 차이가 빨강과 반대 방향 | SpO2, 근접(착용) 감지 겸용 |

여기서 수치는 흔히 인용되는 범위이고, 실제 파장·배치는 제품마다 다르다.

### 3.3 AFE — PPG 신호 체인 (일반형)

```
 LED driver ──► LED ──► 피부 ──► PD ──► TIA ──► (ambient 제거) ──► ADC ──► FIFO ──► SPI/I2C → MCU
   (전류 DAC,                       (전류→전압,      LED off 구간을
    펄스 폭)                          gain 설정)       재서 빼기
```

- **TIA**(transimpedance amplifier): PD 전류(nA~µA)를 전압으로 바꾼다. gain을 크게 하면 작은 AC가 잘 보이지만 DC에서 포화한다.
- **ambient light cancellation**: LED를 켠 구간과 끈 구간을 번갈아 재서 빼면 햇빛·형광등 성분이 지워진다. 형광등 깜빡임(50/60 Hz의 2배)이 aliasing으로 들어오는 것을 막는 효과도 있다.
- LED는 **펄스**로 짧게(수십 µs) 켠다. PPG 전력은 대부분 LED 전류 × duty다. 그래서 AFE에는 "신호가 좋으면 LED 전류를 줄이는" 자동 제어 루프가 붙는다 — Don이 RF에서 본 AGC와 같은 구조다.
- 이런 AFE는 PPG 전용 칩으로 나온다(예: ADI ADPD 계열, Analog Devices/Maxim MAX8614x 계열, TI AFE44xx 계열). 대부분 내부 FIFO + watermark 인터럽트를 가진다 — G2의 IMU FIFO와 같은 패턴이다.

### 3.4 합성 PPG 만들기

이 절에서 쓸 합성 신호다. 아래 코드를 `ppg_synth.py`로 저장하면 3.6절 코드가 import한다. 0~40 s는 휴식(심박 72 bpm), 40~80 s는 걷기(심박 100 bpm, 팔 흔들기 1.0 Hz). 모션 성분은 가속도의 3배 크기로 PPG에 새어 든다고 가정했다 — 맥파보다 크다. 실제 손목에서도 걷기·달리기 중 모션 성분이 맥파보다 큰 일은 흔하다.

```python
# ppg_synth.py — 합성 손목 PPG(녹색) + 가속도계, 25 Hz. 0~40 s 휴식(HR 72), 40~80 s 걷기(HR 100, 팔 흔들기 1.0 Hz)
import numpy as np
fs = 25.0
def make(seed=0):
    rng = np.random.default_rng(seed)
    t = np.arange(0, 80, 1 / fs)
    walk = t >= 40
    hr = np.where(walk, 100.0, 72.0) / 60.0                       # Hz
    phase = 2 * np.pi * np.cumsum(hr) / fs
    cardiac = np.sin(phase) + 0.35 * np.sin(2 * phase + 0.8)      # 맥파 + 2차 고조파 (모양)
    resp = 0.6 * np.sin(2 * np.pi * 0.25 * t)                     # 호흡 15회/분 -> 기저선 흔들림
    acc = walk * (0.8 * np.sin(2 * np.pi * 1.0 * t) + 0.3 * np.sin(2 * np.pi * 2.0 * t + 0.5))  # [g] 팔 흔들기
    motion = 3.0 * np.convolve(acc, [0.5, 0.3, 0.2], "same")      # 모션이 PPG에 새어 듦 (맥파보다 3배 큼)
    ppg = 100.0 + cardiac + resp + motion + rng.normal(0, 0.15, t.size)   # DC 100 (임의 단위) + AC ~1
    return t, ppg, acc + rng.normal(0, 0.02, t.size), walk
```

```svg
<svg viewBox="0 0 640 310" xmlns="http://www.w3.org/2000/svg">
  <text x="180" y="22" font-size="13" text-anchor="middle">휴식 20~26 s (HR 72)</text> <text x="480" y="22" font-size="13" text-anchor="middle">걷기 60~66 s (HR 100, 팔 1.0 Hz)</text>
  <line x1="60" y1="95" x2="300" y2="95" stroke="#888" stroke-width="0.8" stroke-dasharray="3,3"/> <line x1="60" y1="225" x2="300" y2="225" stroke="#888" stroke-width="0.8" stroke-dasharray="3,3"/>
  <line x1="360" y1="95" x2="600" y2="95" stroke="#888" stroke-width="0.8" stroke-dasharray="3,3"/> <line x1="360" y1="225" x2="600" y2="225" stroke="#888" stroke-width="0.8" stroke-dasharray="3,3"/>
  <polyline fill="none" stroke="#4a7bd0" stroke-width="1.5" points="60.0,76.6 61.6,73.2 63.2,77.7 64.8,74.9 66.4,70.5 68.0,75.9 69.6,78.6 71.2,78.0 72.8,77.6 74.4,83.5 76.0,85.0 77.6,88.7 79.2,97.8 80.8,103.8 82.4,105.3 84.0,116.0 85.6,105.3 87.2,99.9 88.8,102.6 90.4,86.4 92.0,74.2 93.6,69.5 95.2,64.5 96.8,61.5 98.4,59.8 100.0,66.9 101.6,62.3 103.2,69.7 104.8,72.6 106.4,69.0 108.0,74.1 109.6,79.1 111.2,87.8 112.8,88.8 114.4,100.5 116.0,110.6 117.6,118.7 119.2,111.4 120.8,111.2 122.4,104.4 124.0,89.6 125.6,78.3 127.2,68.1 128.8,68.0 130.4,66.4 132.0,75.8 133.6,75.7 135.2,79.0 136.8,79.6 138.4,84.3 140.0,84.2 141.6,91.7 143.2,94.0 144.8,101.0 146.4,115.1 148.0,119.0 149.6,127.3 151.2,135.7 152.8,124.8 154.4,123.3 156.0,116.0 157.6,104.3 159.2,93.4 160.8,82.4 162.4,85.0 164.0,83.2 165.6,82.9 167.2,89.3 168.8,90.8 170.4,98.2 172.0,95.7 173.6,100.1 175.2,99.2 176.8,110.8 178.4,116.9 180.0,118.1 181.6,130.5 183.2,136.4 184.8,136.4 186.4,135.8 188.0,121.8 189.6,120.0 191.2,105.3 192.8,93.8 194.4,88.4 196.0,87.3 197.6,87.4 199.2,85.0 200.8,92.4 202.4,89.0 204.0,97.4 205.6,94.8 207.2,93.7 208.8,100.1 210.4,98.1 212.0,106.5 213.6,117.1 215.2,125.3 216.8,129.5 218.4,124.2 220.0,123.7 221.6,115.8 223.2,101.0 224.8,81.2 226.4,77.9 228.0,69.6 229.6,68.3 231.2,66.4 232.8,75.4 234.4,74.8 236.0,74.5 237.6,76.5 239.2,77.7 240.8,80.4 242.4,81.8 244.0,88.3 245.6,100.5 247.2,104.1 248.8,105.3 250.4,105.7 252.0,104.7 253.6,105.7 255.2,94.6 256.8,83.9 258.4,78.6 260.0,73.3 261.6,62.3 263.2,66.5 264.8,63.1 266.4,63.1 268.0,73.1 269.6,71.1 271.2,69.0 272.8,73.5 274.4,75.4 276.0,78.1 277.6,81.7 279.2,94.9 280.8,99.9 282.4,111.6 284.0,117.5 285.6,114.7 287.2,113.3 288.8,105.1 290.4,95.4 292.0,91.4 293.6,76.1 295.2,76.7 296.8,72.5 298.4,69.8"/>
  <polyline fill="none" stroke="#3f9a6b" stroke-width="1.5" points="60.0,210.0 61.6,205.7 63.2,204.9 64.8,205.9 66.4,207.5 68.0,209.6 69.6,211.7 71.2,213.7 72.8,215.7 74.4,218.7 76.0,223.0 77.6,228.8 79.2,235.9 80.8,242.9 82.4,248.4 84.0,250.9 85.6,249.8 87.2,245.5 88.8,238.4 90.4,229.3 92.0,219.7 93.6,211.8 95.2,206.8 96.8,204.6 98.4,204.9 100.0,206.7 101.6,209.1 103.2,211.4 104.8,213.1 106.4,214.5 108.0,216.9 109.6,220.9 111.2,226.6 112.8,234.0 114.4,242.5 116.0,250.4 117.6,255.1 119.2,254.9 120.8,249.7 122.4,240.0 124.0,227.6 125.6,215.5 127.2,206.6 128.8,202.7 130.4,203.3 132.0,206.2 133.6,209.3 135.2,211.4 136.8,212.7 138.4,213.9 140.0,215.7 141.6,218.7 143.2,223.7 144.8,230.7 146.4,239.2 148.0,247.4 149.6,253.5 151.2,255.7 152.8,253.3 154.4,246.7 156.0,237.2 157.6,225.9 159.2,215.1 160.8,207.2 162.4,203.1 164.0,202.5 165.6,204.4 167.2,207.6 168.8,211.0 170.4,213.6 172.0,215.4 173.6,217.2 175.2,220.5 176.8,225.6 178.4,232.2 180.0,239.7 181.6,247.0 183.2,252.5 184.8,254.1 186.4,251.0 188.0,243.8 189.6,234.0 191.2,223.4 192.8,214.0 194.4,207.8 196.0,205.2 197.6,205.3 199.2,207.4 200.8,210.3 202.4,213.3 204.0,215.6 205.6,217.0 207.2,218.2 208.8,220.4 210.4,225.1 212.0,232.7 213.6,241.9 215.2,250.3 216.8,255.4 218.4,256.0 220.0,251.7 221.6,242.6 223.2,230.2 224.8,217.7 226.4,208.0 228.0,202.7 229.6,201.6 231.2,203.8 232.8,207.4 234.4,210.6 236.0,212.5 237.6,213.9 239.2,215.4 240.8,218.2 242.4,223.1 244.0,230.0 245.6,237.6 247.2,243.8 248.8,247.5 250.4,248.8 252.0,247.8 253.6,244.0 255.2,237.2 256.8,228.9 258.4,220.5 260.0,213.5 261.6,208.5 263.2,206.3 264.8,206.5 266.4,208.6 268.0,211.0 269.6,212.6 271.2,213.2 272.8,213.8 274.4,215.2 276.0,218.4 277.6,224.0 279.2,232.0 280.8,240.9 282.4,248.7 284.0,253.2 285.6,253.2 287.2,248.8 288.8,241.1 290.4,231.5 292.0,221.7 293.6,213.0 295.2,206.9 296.8,204.2 298.4,204.9"/>
  <polyline fill="none" stroke="#d0564a" stroke-width="1.5" points="360.0,82.3 361.6,75.3 363.2,72.2 364.8,73.5 366.4,78.0 368.0,83.4 369.6,87.1 371.2,87.9 372.8,85.8 374.4,82.2 376.0,79.0 377.6,77.8 379.2,79.2 380.8,83.2 382.4,89.5 384.0,97.3 385.6,106.4 387.2,116.7 388.8,127.2 390.4,135.9 392.0,140.2 393.6,138.2 395.2,129.4 396.8,114.8 398.4,97.3 400.0,80.4 401.6,67.6 403.2,60.3 404.8,58.2 406.4,59.9 408.0,64.0 409.6,70.0 411.2,77.4 412.8,85.6 414.4,93.4 416.0,99.2 417.6,101.5 419.2,100.3 420.8,97.5 422.4,95.2 424.0,95.6 425.6,99.6 427.2,106.3 428.8,113.7 430.4,119.5 432.0,122.2 433.6,121.5 435.2,117.9 436.8,112.5 438.4,106.3 440.0,99.2 441.6,91.1 443.2,82.4 444.8,74.0 446.4,67.4 448.0,64.0 449.6,64.6 451.2,68.5 452.8,73.8 454.4,78.5 456.0,82.3 457.6,85.7 459.2,90.0 460.8,96.4 462.4,105.2 464.0,114.9 465.6,123.0 467.2,127.6 468.8,128.0 470.4,125.0 472.0,120.0 473.6,114.1 475.2,107.7 476.8,100.3 478.4,92.1 480.0,83.7 481.6,76.7 483.2,72.7 484.8,72.9 486.4,76.9 488.0,82.5 489.6,86.8 491.2,87.9 492.8,85.7 494.4,82.3 496.0,79.9 497.6,79.4 499.2,81.1 500.8,84.7 502.4,89.8 504.0,96.4 505.6,104.8 507.2,115.4 508.8,126.9 510.4,136.7 512.0,141.6 513.6,139.3 515.2,129.3 516.8,113.5 518.4,95.7 520.0,79.9 521.6,69.0 523.2,63.4 524.8,61.9 526.4,62.8 528.0,65.5 529.6,70.1 531.2,76.9 532.8,85.0 534.4,92.7 536.0,98.0 537.6,100.1 539.2,99.2 540.8,96.5 542.4,94.4 544.0,95.0 545.6,99.6 547.2,107.0 548.8,114.6 550.4,119.8 552.0,121.5 553.6,119.9 555.2,116.5 556.8,112.3 558.4,107.9 560.0,102.1 561.6,93.8 563.2,83.6 564.8,73.4 566.4,65.8 568.0,62.3 569.6,63.1 571.2,67.0 572.8,72.2 574.4,77.1 576.0,81.0 577.6,84.7 579.2,89.7 580.8,96.8 582.4,105.5 584.0,114.4 585.6,122.2 587.2,127.3 588.8,128.4 590.4,125.7 592.0,120.5 593.6,114.0 595.2,106.6 596.8,98.4 598.4,90.0"/>
  <polyline fill="none" stroke="#e08a3c" stroke-width="1.5" points="360.0,220.4 361.6,207.6 363.2,199.3 364.8,195.1 366.4,194.4 368.0,196.1 369.6,199.5 371.2,204.6 372.8,209.4 374.4,212.0 376.0,215.6 377.6,217.1 379.2,218.2 380.8,221.0 382.4,224.6 384.0,231.8 385.6,239.7 387.2,247.0 388.8,255.1 390.4,260.1 392.0,262.3 393.6,260.2 395.2,255.1 396.8,244.5 398.4,232.7 400.0,220.1 401.6,208.3 403.2,198.8 404.8,193.4 406.4,194.3 408.0,195.7 409.6,200.3 411.2,205.2 412.8,209.5 414.4,214.1 416.0,215.0 417.6,217.8 419.2,218.7 420.8,221.8 422.4,224.8 424.0,230.8 425.6,238.7 427.2,247.5 428.8,256.3 430.4,261.5 432.0,262.5 433.6,261.2 435.2,254.6 436.8,244.1 438.4,230.8 440.0,220.3 441.6,208.1 443.2,200.3 444.8,195.8 446.4,194.0 448.0,195.9 449.6,200.1 451.2,204.3 452.8,209.5 454.4,214.4 456.0,214.9 457.6,216.6 459.2,219.7 460.8,221.7 462.4,225.7 464.0,230.9 465.6,239.3 467.2,246.8 468.8,255.7 470.4,260.1 472.0,262.8 473.6,261.0 475.2,253.8 476.8,243.9 478.4,231.9 480.0,219.8 481.6,207.7 483.2,199.0 484.8,195.2 486.4,193.7 488.0,196.8 489.6,200.2 491.2,203.9 492.8,210.0 494.4,212.6 496.0,215.3 497.6,215.8 499.2,218.5 500.8,222.5 502.4,225.7 504.0,231.1 505.6,238.7 507.2,247.1 508.8,254.2 510.4,260.5 512.0,264.2 513.6,261.2 515.2,254.2 516.8,245.0 518.4,232.1 520.0,219.5 521.6,207.3 523.2,200.4 524.8,195.8 526.4,194.7 528.0,196.0 529.6,199.4 531.2,204.8 532.8,209.8 534.4,213.7 536.0,215.5 537.6,216.5 539.2,217.8 540.8,220.3 542.4,224.8 544.0,231.2 545.6,239.6 547.2,247.2 548.8,254.1 550.4,260.6 552.0,262.3 553.6,260.7 555.2,254.0 556.8,245.1 558.4,233.3 560.0,220.4 561.6,209.9 563.2,199.3 564.8,193.2 566.4,193.9 568.0,196.3 569.6,199.5 571.2,203.5 572.8,209.2 574.4,211.9 576.0,215.3 577.6,216.8 579.2,219.7 580.8,220.9 582.4,226.0 584.0,232.2 585.6,240.3 587.2,247.0 588.8,253.5 590.4,261.2 592.0,263.0 593.6,261.0 595.2,254.1 596.8,243.7 598.4,232.4"/>
  <text x="62" y="40" font-size="12">raw − DC (호흡 포함), ±2.5</text> <text x="62" y="170" font-size="12">band-pass 0.5~4 Hz, ±2.5</text> <text x="362" y="40" font-size="12">band-pass PPG, ±5 (모션이 지배)</text>
  <text x="362" y="170" font-size="12">가속도 [g], ±1.5</text> <line x1="60" y1="290" x2="300" y2="290" stroke="currentColor" stroke-width="1"/> <line x1="360" y1="290" x2="600" y2="290" stroke="currentColor" stroke-width="1"/>
  <text x="60" y="305" font-size="12" text-anchor="middle">20</text><text x="140" y="305" font-size="12" text-anchor="middle">22</text><text x="220" y="305" font-size="12" text-anchor="middle">24</text><text x="300" y="305" font-size="12" text-anchor="middle">26 s</text>
  <text x="360" y="305" font-size="12" text-anchor="middle">60</text><text x="440" y="305" font-size="12" text-anchor="middle">62</text><text x="520" y="305" font-size="12" text-anchor="middle">64</text><text x="600" y="305" font-size="12" text-anchor="middle">66 s</text>
</svg>
```

그림 3 — 합성 PPG(실제 계산한 151점 polyline). 왼쪽: 휴식 중 raw(파랑)는 호흡으로 기저선이 흔들리고, band-pass(초록)하면 6초에 약 7개 박동이 깨끗이 보인다. 오른쪽: 걷는 중에는 band-pass 후에도 팔 흔들기(주황, 가속도) 리듬이 PPG(빨강)를 지배한다. 맥파는 그 위의 작은 주름이다.

### 3.5 심박 추정 — 두 가지 고전 방법

1. **시간 영역**: band-pass(0.5~4 Hz = 30~240 bpm) → peak 검출 → 이웃 peak 간격(IBI, inter-beat interval)의 중앙값 → HR = 60 / IBI.
2. **주파수 영역**: 창(예: 8~30 s)에 FFT → 0.67~3.5 Hz(40~210 bpm) 안에서 가장 큰 peak → HR = 60 × f_peak.

손으로: 휴식 HR 72 bpm = 1.2 Hz, 박동 간격 0.833 s = 25 Hz에서 약 20.8 샘플. 30 s FFT 창의 주파수 분해능은 1/30 Hz = 2 bpm이다(zero-padding은 보간일 뿐 분해능을 늘리지 않는다 — G5). 그래서 "HR을 1 bpm 단위로 빨리" 원하면 창 길이와 반응 속도 사이에서 타협해야 한다.

### 3.6 모션 아티팩트와 가속도계 기준 제거

걷는 중 팔 흔들기가 1.0 Hz(60/분)라면, 이것은 HR 대역(0.67~3.5 Hz) 한가운데다. band-pass로 지울 수 없다. 핵심 아이디어: **가속도계가 "모션만" 따로 재고 있다.** 그 신호와 상관된 부분을 PPG에서 빼면 된다.

**NLMS adaptive filter**(Normalized Least Mean Squares)는 그 일을 하는 가장 단순한 알고리즘이다.

```
 y[n] = wᵀ·x[n]                  x[n] = 최근 가속도 taps개 (기준 신호)
 e[n] = d[n] − y[n]              d[n] = PPG,  e[n] = 정리된 PPG (출력)
 w   ← w + μ · e[n] · x[n] / (‖x[n]‖² + ε)
```

말로 하면, "가속도 최근 값들의 가중합으로 PPG를 최대한 흉내 내 보고(y), 흉내 낼 수 있었던 부분 = 모션이라고 보고 뺀다(e)". 맥파는 가속도와 상관이 없으므로 흉내 낼 수 없어 e에 남는다. 가중치 w는 오차가 줄어드는 방향으로 매 샘플 조금씩(μ) 움직인다 — A3의 gradient descent를 샘플마다 한 번씩 하는 것이다. 펌웨어로는 taps개 MAC + taps개 갱신이라 MCU에서 충분히 돈다. (마이크 AEC가 같은 원리다 — G4.)

이 코드는 band-pass + peak, FFT peak, NLMS 후 두 방법을 각각 30 s 창에서 비교한다.

```python
# PPG -> 심박: band-pass + peak 검출, 스펙트럼 peak, 그리고 가속도계 기준 NLMS로 모션 제거
import numpy as np
from scipy.signal import butter, filtfilt, find_peaks
from ppg_synth import make, fs
t, ppg, acc, walk = make()
b, a = butter(2, [0.5 / (fs / 2), 4.0 / (fs / 2)], btype="band")   # 30~240 bpm 통과
bp = filtfilt(b, a, ppg)
def hr_peaks(x):
    pk, _ = find_peaks(x, distance=int(fs * 0.33))              # 180 bpm 이상 간격 금지
    return 60.0 / np.median(np.diff(pk)) * fs
def hr_fft(x):
    X = np.abs(np.fft.rfft(x * np.hanning(x.size), 4096)); f = np.fft.rfftfreq(4096, 1 / fs)
    band = (f > 0.67) & (f < 3.5); return 60 * f[band][np.argmax(X[band])]
def nlms(d, ref, taps=8, mu=0.05):                              # d에서 ref와 상관된 성분을 빼 준다
    w = np.zeros(taps); e = np.zeros_like(d)
    for n in range(taps, d.size):
        x = ref[n - taps + 1:n + 1][::-1]; y = w @ x; e[n] = d[n] - y
        w += mu * e[n] * x / (x @ x + 1e-6)
    return e
rest, wk = slice(int(10 * fs), int(40 * fs)), slice(int(50 * fs), int(80 * fs))   # 각 30 s 창
acc_bp = filtfilt(b, a, acc)
print("AC/DC (perfusion index) at rest = %.2f %%" % (100 * (np.ptp(bp[rest]) / ppg[rest].mean())))
print("rest  truth 72 : peaks %.1f  fft %.1f" % (hr_peaks(bp[rest]), hr_fft(bp[rest])))
print("walk  truth 100: peaks %.1f  fft %.1f   <- motion" % (hr_peaks(bp[wk]), hr_fft(bp[wk])))
for taps, mu in [(8, 0.05), (16, 0.1)]:
    c = nlms(bp, acc_bp, taps, mu)
    print("walk  + NLMS(%2d,%.2f): peaks %.1f  fft %.1f" % (taps, mu, hr_peaks(c[wk]), hr_fft(c[wk])))
print("accel spectral peak during walk = %.1f per min" % hr_fft(acc[wk]))
```

```text
AC/DC (perfusion index) at rest = 2.74 %
rest  truth 72 : peaks 71.4  fft 72.1
walk  truth 100: peaks 93.8  fft 60.1   <- motion
walk  + NLMS( 8,0.05): peaks 107.1  fft 100.0
walk  + NLMS(16,0.10): peaks 100.0  fft 100.0
accel spectral peak during walk = 60.1 per min
```

출력에서 볼 것:

- 휴식에서는 두 방법 모두 72 근처(71.4, 72.1)다. perfusion index 2.7%는 합성 설정값에서 나온 것이다.
- 걷기 중 FFT는 **60.1 bpm** — 가속도 스펙트럼 peak(60.1/분)와 정확히 같다. 즉 HR이 아니라 **팔 흔들기 cadence를 심박으로 보고**했다. 가장 흔하고 가장 위험한 실패다(사용자는 "걷는데 심박이 60?"을 바로 알아챈다).
- NLMS 후 FFT는 100.0으로 맞는다. 하지만 **설정(taps, μ)에 따라 peak 방법은 107.1이 나오기도** 한다. adaptive filter는 수렴 속도·잔여 오차가 파라미터에 민감하다. 실제 제품은 여러 후보 HR을 추적하는 tracking(이전 HR 근처만 허용), 가속도 스펙트럼 peak를 HR 후보에서 빼는 spectral subtraction, 그리고 최근에는 작은 ML 모델을 함께 쓴다(B7 방식의 특징 + 작은 모델, 또는 1D-CNN).

### 3.7 SpO2 — ratio-of-ratios 개념 (의료용 아님)

빨강과 IR에서 각각 AC/DC를 구하고 그 비율을 낸다.

```
 R = (AC_red / DC_red) / (AC_ir / DC_ir)
 SpO2 ≈ f(R)       f는 기기별 캘리브레이션 곡선 (교과서에 자주 나오는 선형 근사: 110 − 25·R)
```

말로 하면, "빨강에서의 맥박 크기 비율을 IR에서의 맥박 크기 비율로 나눈 값"이다. DC로 나누는 이유는 LED 밝기·피부색·센서 접촉 같은 공통 요인을 상쇄하기 위해서다. 산소포화도가 낮을수록 빨강을 더 흡수하므로 R이 커진다.

이 코드는 합성 빨강/IR 신호에서 R을 구한다. `110 − 25R`은 개념 설명용으로 흔히 인용되는 근사식이고, 실제 기기는 임상 데이터로 만든 자기만의 곡선을 쓴다.

```python
# SpO2 개념: ratio-of-ratios R = (AC_red/DC_red)/(AC_ir/DC_ir). 합성 신호, 의료용 아님
import numpy as np
from scipy.signal import butter, filtfilt
fs = 25.0; t = np.arange(0, 20, 1 / fs); rng = np.random.default_rng(2)
pulse = np.sin(2 * np.pi * 1.2 * t)
b, a = butter(2, [0.5 / (fs / 2), 4.0 / (fs / 2)], btype="band")
def ratio(ac_red_pct, ac_ir_pct, dc_red=8000.0, dc_ir=12000.0):
    red = dc_red * (1 + ac_red_pct / 100 * pulse) + rng.normal(0, 3, t.size)
    ir = dc_ir * (1 + ac_ir_pct / 100 * pulse) + rng.normal(0, 3, t.size)
    rms = lambda x: np.sqrt(np.mean(filtfilt(b, a, x)[50:-50] ** 2))   # AC = band-pass RMS
    return (rms(red) / red.mean()) / (rms(ir) / ir.mean())
for ac_red, ac_ir in [(0.5, 1.0), (0.7, 1.0), (1.0, 1.0)]:
    R = ratio(ac_red, ac_ir)
    print("AC_red %.1f%%  AC_ir %.1f%%  ->  R = %.3f  ->  110 - 25R = %.1f %%  (illustrative curve)" % (ac_red, ac_ir, R, 110 - 25 * R))
```

```text
AC_red 0.5%  AC_ir 1.0%  ->  R = 0.502  ->  110 - 25R = 97.4 %  (illustrative curve)
AC_red 0.7%  AC_ir 1.0%  ->  R = 0.702  ->  110 - 25R = 92.4 %  (illustrative curve)
AC_red 1.0%  AC_ir 1.0%  ->  R = 1.001  ->  110 - 25R = 85.0 %  (illustrative curve)
```

출력에서 볼 것: 입력한 AC 비율(0.5, 0.7, 1.0)이 R로 그대로 복원된다. 곡선에 넣은 SpO2 숫자는 **예시일 뿐**이다. 손목 SpO2는 모션·말초 관류·피부색·접촉 압력에 크게 영향을 받고, 의료 기기로 주장하려면 규제 절차가 따로 있다. 인터뷰에서는 "개념은 ratio-of-ratios, 정확도는 캘리브레이션과 신호 품질이 결정"이라고 말하면 충분하다.

### 3.8 임베디드 연결과 함정

- PPG 샘플링은 보통 25~100 Hz면 HR에 충분하다(HRV처럼 박동 시각 정밀도가 필요하면 더 높게). 데이터는 작지만 **LED 전력이 크다** — PPG를 계속 켤지, 착용·정지 상태에서만 주기적으로 켤지가 전력 설계의 핵심이다.
- PPG 자체도 착용 감지에 쓰인다: 피부에서 반사되는 DC 수준(특히 IR)과 맥박 AC의 존재. 하지만 LED를 켜야 하므로 근접 센서보다 비싸다(§8).
- 함정: PPG와 가속도의 **타임스탬프가 어긋나면**(서로 다른 FIFO, 다른 클럭) adaptive filter가 수렴하지 않는다. 두 FIFO의 시간 정렬이 G7의 실제 사례다.
- 함정: 피부색·문신·털·착용 압력에 따라 AC 크기가 몇 배씩 다르다. 데이터 수집 때 이 다양성을 일부러 넣어야 한다(H4, H5).

---

## 4. ToF와 근접 센서

### 4.1 IR 근접 센서 — 반사광의 세기

가장 단순한 근접 센서는 IR LED + PD 한 쌍이다. 물체(피부)가 가까우면 반사광이 많아 count가 크고, 멀면 작다. 거리를 "재는" 것이 아니라 **반사량**을 잰다. 그래서 같은 거리라도 피부색, 표면 반사율, 각도에 따라 count가 크게 다르다. 출력은 보통 I2C로 읽는 12~16 bit count이고, 많은 칩이 "count가 문턱을 넘으면 인터럽트"를 하드웨어로 지원한다(예: Vishay VCNL4040 같은 부품).

- 커버 유리 안쪽 반사(**crosstalk**) 때문에 물체가 없어도 count가 0이 아니다. 공장 캘리브레이션에서 이 offset을 재서 뺀다 — black level과 같은 개념이다.
- 이어버드의 "귀에 꽂았나", 폰의 "통화 중 얼굴 가까이"가 이 센서다.

### 4.2 Direct ToF — 빛의 왕복 시간

dToF(direct time-of-flight)는 짧은 레이저(VCSEL) 펄스를 쏘고, 반사광이 돌아오는 **시간**을 잰다. 거리 = c × t / 2.

손으로: 빛은 1 ns에 30 cm를 간다. 1 m 앞 물체의 왕복 시간 = 2 / (3×10⁸) = 6.67 ns. 1 cm 분해능이면 66.7 ps를 구분해야 한다. 이 정도 시간을 재려면 **SPAD**(single-photon avalanche diode — 광자 하나에도 눈사태처럼 전류를 내는 소자)와 **TDC**(time-to-digital converter)를 쓴다.

광자 하나는 잡음(주변광)과 구별이 안 되므로, 펄스를 수천 번 쏘고 도착 시각을 **히스토그램**으로 쌓는다. 진짜 반사는 같은 bin에 몰리고, 햇빛은 고르게 퍼진다. peak bin이 거리다. 예: ST VL53L 계열 같은 소형 dToF 모듈이 이 방식이다(세부 구현은 부품마다 다르다).

이 코드는 그 히스토그램 방식을 단순화해서 흉내 낸다.

```python
# direct ToF 개념: SPAD 광자 도착 시각을 히스토그램으로 쌓아 거리 추정 (합성, 물리 단순화)
import numpy as np
c = 3e8; rng = np.random.default_rng(5)
print("1 cm 거리 분해능에 필요한 왕복 시간 분해능 = %.1f ps" % (2 * 0.01 / c * 1e12))
d_true = 0.42                                    # 42 cm 앞의 손 (제스처)
bin_ps, n_bins, pulses = 250, 64, 2000           # 250 ps bin, 16 ns 창, 레이저 펄스 2000번
t_rt = 2 * d_true / c * 1e12                     # 왕복 시간 [ps]
hist = np.zeros(n_bins, int)
for _ in range(pulses):
    if rng.random() < 0.05:                      # 펄스 당 5%만 신호 광자가 SPAD를 트리거
        hist[min(int(rng.normal(t_rt, 120) // bin_ps), n_bins - 1)] += 1
    if rng.random() < 0.3:                       # 주변광(햇빛) 광자: 창 전체에 균일
        hist[rng.integers(n_bins)] += 1
k = int(np.argmax(hist)); w = hist[k - 1:k + 2].astype(float)   # peak + 이웃 2칸 무게중심
t_est = (np.dot(w, [k - 1, k, k + 1]) / w.sum() + 0.5) * bin_ps
print("round trip %.0f ps -> peak bin %d (counts %d, ambient median %.1f)" % (t_rt, k, hist[k], np.median(hist)))
print("distance estimate %.3f m (truth %.2f m)" % (t_est * 1e-12 * c / 2, d_true))
```

```text
1 cm 거리 분해능에 필요한 왕복 시간 분해능 = 66.7 ps
round trip 2800 ps -> peak bin 11 (counts 84, ambient median 9.0)
distance estimate 0.422 m (truth 0.42 m)
```

출력에서 볼 것: 펄스 2000번 중 약 5%(≈100번)만 신호 광자가 잡혔지만 peak bin(84 count)이 주변광 수준(중앙값 9)보다 훨씬 높다. peak와 이웃 bin의 무게중심으로 bin 폭(250 ps = 3.75 cm)보다 정밀한 0.422 m를 얻었다. 햇빛이 강하면 주변광 바닥이 올라가 peak가 묻힌다 — 실외 ToF의 근본 한계다.

### 4.3 iToF와 쓰임새

- **iToF**(indirect ToF)는 변조된 빛의 위상 차이로 거리를 잰다. 깊이 카메라(픽셀마다 거리)에 많이 쓰인다. 개념만 알아 두자.
- 웨어러블에서의 쓰임: **착용 감지**(귀 안·손목 위, 피부까지 수 mm), **제스처**(손이 다가옴/멀어짐, 스와이프 — 여러 zone을 가진 ToF로 방향까지), 안경형 기기의 "벗었나" 감지.

### 4.4 근접 기반 착용 감지 — hysteresis + debounce (C)

근접 count는 노이즈가 크다(햇빛, 밴드 장력, 땀). 단일 문턱으로 판정하면 문턱 근처에서 ON/OFF가 마구 바뀐다(chattering). 해법은 Don이 펌웨어에서 버튼·전원 감지에 쓰던 그대로다.

- **hysteresis**: 켤 때 문턱(450)과 끌 때 문턱(250)을 다르게. 사이 구간에서는 상태 유지.
- **debounce**: 조건이 N번 연속 만족돼야 상태를 바꾼다. 켜기는 빨리(3회), 끄기는 신중하게(5회) — 착용 중 잠깐 들뜨는 것을 "벗었다"고 오판하면 음악이 멈추는 등 사용자 경험이 나쁘기 때문이다.

이 코드는 1 Hz 합성 근접 count(±110 노이즈)에서 단일 문턱과 hysteresis + debounce를 비교한다.

```c
/* IR 근접 count로 착용 감지: 단일 문턱 vs hysteresis + debounce (합성 데이터, 1 Hz) */
#include <stdio.h>
#include <stdint.h>
static uint32_t s = 12345u;
static int noise(int amp) { s = s * 1664525u + 1013904223u; return (int)(s >> 16) % (2 * amp + 1) - amp; }
static int truth_level(int t) {               /* 장면: 책상 -> 착용(중간에 들썩) -> 벗음 -> 손이 지나감 */
    if (t < 20) return 60;                    /* 책상 위, 커버 유리 crosstalk만 */
    if (t < 60) return (t == 41 || t == 42) ? 330 : 620;   /* 착용 중, 41~42 s 밴드가 들뜸 */
    if (t < 80) return 60;                    /* 벗음 */
    if (t < 90) return 340;                   /* 벗은 채 손이 근처를 지나감 */
    return 60;
}
#define TH_ON 450
#define TH_OFF 250
#define N_ON 3
#define N_OFF 5
int main(void) {
    int naive = 0, naive_toggles = 0, worn = 0, cnt = 0, toggles = 0;
    for (int t = 0; t < 100; t++) {
        int c = truth_level(t) + noise(110);  /* 큰 노이즈: 햇빛·피부색·밴드 장력 흉내 */
        if (c < 0) c = 0;                     /* count는 음수가 없다 */
        int n = c > 350;                      /* 단일 문턱 */
        if (n != naive) { naive = n; naive_toggles++; }
        int want = worn ? (c > TH_OFF) : (c > TH_ON);   /* hysteresis: 상태에 따라 문턱이 다르다 */
        if (want != worn) {
            if (++cnt >= (worn ? N_OFF : N_ON)) { worn = want; cnt = 0; toggles++;
                printf("t=%3d s count=%4d -> %s\n", t, c, worn ? "ON-BODY" : "OFF-BODY"); }
        } else cnt = 0;
    }
    printf("single threshold toggles: %d, hysteresis+debounce toggles: %d (truth: 2)\n", naive_toggles, toggles);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 prox_wear.c -o prox_wear && ./prox_wear
```

```text
t= 22 s count= 599 -> ON-BODY
t= 64 s count=   0 -> OFF-BODY
single threshold toggles: 12, hysteresis+debounce toggles: 2 (truth: 2)
```

출력에서 볼 것: 단일 문턱은 100초 동안 12번 토글했다(정답은 착용·벗음 2번). hysteresis + debounce는 정확히 2번만 바뀌었고, 41~42 s의 밴드 들뜸(count 330 근처)과 80~90 s의 손 지나감(340 근처)을 모두 무시했다. 대가는 **지연**이다: 착용 후 2초(20 → 22 s), 벗은 후 4초(60 → 64 s) 늦게 판정한다. 이 지연이 제품 요구(예: "벗으면 1초 안에 음악 정지")를 만족하는지가 설계 질문이다.

---

## 5. 기압계 — 압력으로 높이 재기

### 5.1 직관과 공식

MEMS 기압계는 얇은 막(diaphragm)이 압력에 따라 휘는 것을 piezoresistive 또는 정전용량 방식으로 읽는다. 대기압은 높이 올라갈수록 줄어든다. 국제표준대기(ISA) 근사식은 다음과 같다.

```
 h = 44330 · (1 − (P / P0)^(1/5.255))        [m]
 P = P0 · (1 − h / 44330)^5.255               [hPa],  P0 = 1013.25 hPa (해수면 표준)
 dP/dh ≈ −P0 · 5.255 / 44330 ≈ −0.120 hPa/m    (해수면 근처)
```

말로 하면, **해수면 근처에서 1 m 올라가면 기압이 약 0.12 hPa(= 12 Pa) 줄어든다.** 한 층(약 3 m)은 약 0.36 hPa다. 거꾸로 "10 cm 분해능"을 원하면 압력을 0.012 hPa = 1.2 Pa 수준으로 구분해야 한다. 좋은 MEMS 기압계의 RMS 노이즈가 oversampling 설정에 따라 대략 Pa 단위라서, 상대 고도 수십 cm는 가능하지만 **절대 고도는 날씨 때문에 수십 m 틀릴 수 있다**(아래).

### 5.2 코드 — 노이즈, 필터, 날씨 drift, 층 판정

이 코드는 10 Hz 합성 기압에서 고도를 계산하고, EMA 필터로 노이즈를 줄이고, 날씨 변화가 섞인 상태에서 "층 오르기"를 판정한다. 노이즈 σ 0.06 hPa는 설명용 가정이다.

```python
# 기압 -> 고도: 해수면 근처 민감도, 노이즈 필터링, 날씨 drift와 "층 오르기" 판정 (합성 데이터)
import numpy as np
P0 = 1013.25
alt = lambda p: 44330.0 * (1 - (p / P0) ** (1 / 5.255))          # 국제표준대기(ISA) 근사식 [m]
press = lambda h: P0 * (1 - h / 44330.0) ** 5.255                  # 역함수 [hPa]
print("dP/dh at 0 m  = %.4f hPa/m" % (press(1) - press(0)))
print("dP/dh at 2000 m = %.4f hPa/m" % (press(2001) - press(2000)))
fs, T = 10.0, 600; t = np.arange(0, T, 1 / fs); rng = np.random.default_rng(3)
h_true = 30 + np.interp(t, [0, 120, 160, 400, 415, T], [0, 0, 9, 9, 6, 6])   # 3층 오르고 1층 내려옴
weather = -0.3 * t / T                                                        # 10분간 -0.3 hPa (저기압 접근)
p = press(h_true) + weather + rng.normal(0, 0.06, t.size)                    # 노이즈 σ 0.06 hPa (가정)
h_raw = alt(p)
k = 1 / (2.0 * fs); h_f = np.empty_like(h_raw); h_f[0] = h_raw[0]            # EMA, 시정수 2 s
for i in range(1, t.size): h_f[i] = h_f[i - 1] + k * (h_raw[i] - h_f[i - 1])
still = t < 110
print("altitude noise std: raw %.2f m, EMA %.2f m" % (h_raw[still].std(), h_f[still].std()))
print("end-start altitude: true %.1f m, baro %.1f m (weather drift adds %.1f m)" % (h_true[-1] - h_true[0], h_f[-1] - h_f[50], alt(P0 - 0.3) - alt(P0)))
v = (h_f[50:] - h_f[:-50]) / 5.0                                              # 5 s 기울기 [m/s]
moving = np.abs(v) > 0.12                                                     # 계단 속도 문턱 (drift는 0.004 m/s)
dh = np.diff(h_f[50:])[moving[:-1]]
print("climbed %.1f m -> %d floors up, descended %.1f m -> %d floors down" % (dh[dh > 0].sum(), round(dh[dh > 0].sum() / 3), -dh[dh < 0].sum(), round(-dh[dh < 0].sum() / 3)))
```

```text
dP/dh at 0 m  = -0.1201 hPa/m
dP/dh at 2000 m = -0.0987 hPa/m
altitude noise std: raw 0.52 m, EMA 0.20 m
end-start altitude: true 6.0 m, baro 8.7 m (weather drift adds 2.5 m)
climbed 9.9 m -> 3 floors up, descended 3.8 m -> 1 floors down
```

출력에서 볼 것:

- 민감도: 해수면 0.1201 hPa/m, 2000 m에서는 0.0987 hPa/m. 고도가 높을수록 같은 1 m의 압력 변화가 작다.
- 노이즈 σ 0.06 hPa → 고도 0.52 m. 시정수 2 s EMA로 0.20 m까지 줄었다. 대가는 지연(약 2 s)이다.
- **날씨**: 10분 동안 기압이 0.3 hPa 떨어지자 "고도가 2.5 m 올라간 것"처럼 보였다(진짜 6.0 m인데 8.7 m). 저기압이 지나가면 하루에 수 hPa(수십 m) 바뀐다. 그래서 기압계는 **짧은 시간의 상대 변화**에 쓴다. 절대 고도가 필요하면 GNSS나 기준 기압(날씨 데이터)으로 보정한다.
- 층 판정은 "5 s 기울기가 0.12 m/s 이상인 구간"만 더해서 drift(0.004 m/s)를 무시했다. 3층 오르기·1층 내려오기를 맞혔다. 오른 높이 합이 9.9 m로 9 m보다 큰 것은 이동 구간 안의 노이즈까지 더해졌기 때문이다.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <line x1="60" y1="250" x2="640" y2="250" stroke="currentColor" stroke-width="1"/> <line x1="60" y1="30" x2="60" y2="250" stroke="currentColor" stroke-width="1"/> <g stroke="#888" stroke-width="0.6" stroke-dasharray="3,3">
  <line x1="60" y1="220.7" x2="640" y2="220.7"/><line x1="60" y1="176.7" x2="640" y2="176.7"/><line x1="60" y1="132.7" x2="640" y2="132.7"/><line x1="60" y1="88.7" x2="640" y2="88.7"/><line x1="60" y1="44.7" x2="640" y2="44.7"/>
  </g> <g font-size="12" text-anchor="end"> <text x="54" y="224.7">0</text><text x="54" y="180.7">3</text><text x="54" y="136.7">6</text><text x="54" y="92.7">9</text><text x="54" y="48.7">12 m</text> </g>
  <g font-size="12" text-anchor="middle">
  <text x="60" y="267">0</text><text x="156.7" y="267">100</text><text x="253.3" y="267">200</text><text x="350" y="267">300</text><text x="446.7" y="267">400</text><text x="543.3" y="267">500</text><text x="640" y="267">600 s</text>
  </g>
  <polyline fill="none" stroke="#888" stroke-width="1" points="60.0,235.7 61.9,220.7 63.9,222.5 65.8,208.3 67.7,232.5 69.7,217.7 71.6,212.7 73.5,213.0 75.5,226.0 77.4,227.1 79.3,231.4 81.3,203.6 83.2,218.6 85.1,219.3 87.1,218.0 89.0,220.2 90.9,215.2 92.9,229.5 94.8,219.4 96.7,214.5 98.7,208.4 100.6,221.4 102.5,204.9 104.5,224.1 106.4,224.0 108.3,218.1 110.3,217.2 112.2,219.3 114.1,229.9 116.1,201.1 118.0,215.0 119.9,216.7 121.9,213.1 123.8,216.4 125.7,226.3 127.7,215.9 129.6,218.0 131.5,219.1 133.5,209.9 135.4,218.2 137.3,221.0 139.3,229.1 141.2,214.7 143.1,217.1 145.1,221.2 147.0,216.7 148.9,211.2 150.9,234.4 152.8,216.0 154.7,213.7 156.7,207.0 158.6,223.0 160.5,217.6 162.5,216.8 164.4,207.2 166.3,202.7 168.3,207.5 170.2,205.8 172.1,224.9 174.1,217.5 176.0,223.0 177.9,210.9 179.9,205.1 181.8,186.6 183.7,187.7 185.7,178.9 187.6,174.9 189.5,170.9 191.5,161.3 193.4,152.5 195.3,138.0 197.3,141.0 199.2,119.3 201.1,134.4 203.1,115.6 205.0,108.6 206.9,116.1 208.9,108.9 210.8,93.3 212.7,91.7 214.7,65.5 216.6,84.7 218.5,84.0 220.5,81.5 222.4,83.0 224.3,82.5 226.3,70.1 228.2,81.4 230.1,80.1 232.1,86.8 234.0,66.8 235.9,78.8 237.9,74.7 239.8,74.3 241.7,79.0 243.7,75.3 245.6,82.9 247.5,78.4 249.5,68.3 251.4,76.0 253.3,76.6 255.3,76.3 257.2,76.6 259.1,83.7 261.1,85.5 263.0,77.4 264.9,79.0 266.9,74.6 268.8,82.0 270.7,67.7 272.7,76.9 274.6,80.4 276.5,81.8 278.5,75.0 280.4,89.3 282.3,66.1 284.3,74.4 286.2,82.0 288.1,84.8 290.1,73.6 292.0,83.0 293.9,77.8 295.9,93.8 297.8,70.5 299.7,75.4 301.7,76.8 303.6,73.6 305.5,72.1 307.5,86.6 309.4,69.8 311.3,61.8 313.3,66.5 315.2,60.1 317.1,69.8 319.1,67.1 321.0,58.7 322.9,75.8 324.9,68.6 326.8,73.0 328.7,68.8 330.7,63.9 332.6,63.4 334.5,58.5 336.5,91.5 338.4,72.7 340.3,82.0 342.3,77.3 344.2,78.8 346.1,77.7 348.1,74.1 350.0,59.8 351.9,67.2 353.9,60.8 355.8,80.5 357.7,71.3 359.7,63.7 361.6,77.3 363.5,74.8 365.5,70.3 367.4,73.1 369.3,64.0 371.3,66.4 373.2,75.7 375.1,68.0 377.1,56.9 379.0,63.0 380.9,80.8 382.9,69.4 384.8,77.6 386.7,70.4 388.7,71.1 390.6,61.4 392.5,77.3 394.5,66.2 396.4,61.5 398.3,69.4 400.3,63.3 402.2,67.8 404.1,82.2 406.1,58.9 408.0,56.4 409.9,74.7 411.9,58.0 413.8,47.1 415.7,56.5 417.7,75.1 419.6,56.5 421.5,76.2 423.5,61.0 425.4,63.0 427.3,70.5 429.3,71.4 431.2,58.7 433.1,62.3 435.1,74.1 437.0,66.3 438.9,64.3 440.9,59.1 442.8,72.5 444.7,69.9 446.7,53.8 448.6,66.7 450.5,77.3 452.5,88.8 454.4,88.3 456.3,88.9 458.3,108.9 460.2,98.8 462.1,104.7 464.1,114.1 466.0,93.4 467.9,104.1 469.9,111.5 471.8,104.0 473.7,103.3 475.7,104.7 477.6,109.9 479.5,115.2 481.5,121.3 483.4,116.1 485.3,109.7 487.3,99.5 489.2,117.0 491.1,92.8 493.1,99.1 495.0,99.4 496.9,103.1 498.9,96.7 500.8,94.1 502.7,107.1 504.7,91.2 506.6,108.5 508.5,97.8 510.5,91.4 512.4,102.0 514.3,92.9 516.3,83.2 518.2,110.9 520.1,117.4 522.1,95.1 524.0,95.6 525.9,111.4 527.9,100.3 529.8,108.8 531.7,101.8 533.7,119.7 535.6,107.5 537.5,110.4 539.5,105.0 541.4,95.7 543.3,103.1 545.3,106.3 547.2,110.6 549.1,89.9 551.1,95.3 553.0,90.8 554.9,99.9 556.9,111.8 558.8,104.3 560.7,103.2 562.7,103.4 564.6,101.5 566.5,109.4 568.5,82.8 570.4,93.3 572.3,114.8 574.3,107.3 576.2,108.7 578.1,110.8 580.1,101.9 582.0,107.3 583.9,107.8 585.9,93.5 587.8,91.4 589.7,95.0 591.7,104.0 593.6,108.6 595.5,89.7 597.5,95.2 599.4,83.4 601.3,105.2 603.3,104.3 605.2,93.2 607.1,84.9 609.1,96.6 611.0,96.1 612.9,99.9 614.9,99.7 616.8,100.5 618.7,94.1 620.7,90.7 622.6,107.5 624.5,98.8 626.5,96.8 628.4,98.5 630.3,87.5 632.3,99.1 634.2,81.1 636.1,99.4 638.1,107.3"/>
  <polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="60.0,235.7 63.9,221.9 67.7,219.7 71.6,219.9 75.5,219.4 79.3,221.5 83.2,217.6 87.1,221.3 90.9,218.6 94.8,219.4 98.7,218.4 102.5,218.2 106.4,217.8 110.3,218.8 114.1,218.5 118.0,218.4 121.9,218.1 125.7,219.0 129.6,215.9 133.5,215.1 137.3,213.6 141.2,213.9 145.1,214.7 148.9,216.1 152.8,215.6 156.7,214.9 160.5,214.9 164.4,213.2 168.3,214.1 172.1,213.4 176.0,214.1 179.9,204.9 183.7,192.5 187.6,181.0 191.5,165.6 195.3,152.3 199.2,138.5 203.1,124.0 206.9,113.7 210.8,100.3 214.7,84.7 218.5,83.0 222.4,79.9 226.3,77.2 230.1,77.2 234.0,76.6 237.9,77.1 241.7,76.0 245.6,75.7 249.5,76.0 253.3,76.3 257.2,78.2 261.1,77.0 264.9,75.7 268.8,78.2 272.7,75.8 276.5,76.5 280.4,75.5 284.3,73.7 288.1,74.2 292.0,74.7 295.9,72.1 299.7,73.5 303.6,72.1 307.5,75.1 311.3,71.4 315.2,72.8 319.1,71.8 322.9,70.3 326.8,73.1 330.7,70.7 334.5,69.8 338.4,70.9 342.3,71.6 346.1,71.1 350.0,72.0 353.9,68.5 357.7,71.1 361.6,70.3 365.5,69.8 369.3,70.7 373.2,68.1 377.1,69.0 380.9,67.0 384.8,68.2 388.7,67.9 392.5,66.9 396.4,67.5 400.3,67.7 404.1,68.5 408.0,66.3 411.9,66.0 415.7,65.4 419.6,65.1 423.5,62.7 427.3,64.2 431.2,65.2 435.1,65.3 438.9,65.1 442.8,65.4 446.7,65.0 450.5,69.7 454.4,81.9 458.3,94.3 462.1,103.0 466.0,106.8 469.9,107.6 473.7,106.2 477.6,104.4 481.5,104.1 485.3,107.4 489.2,107.1 493.1,105.8 496.9,106.1 500.8,104.4 504.7,102.6 508.5,104.8 512.4,107.0 516.3,102.3 520.1,105.2 524.0,102.3 527.9,103.8 531.7,103.8 535.6,103.8 539.5,102.4 543.3,102.4 547.2,103.1 551.1,102.0 554.9,99.1 558.8,102.7 562.7,101.5 566.5,100.6 570.4,99.2 574.3,101.5 578.1,99.5 582.0,99.6 585.9,99.0 589.7,99.3 593.6,101.1 597.5,100.7 601.3,99.1 605.2,99.5 609.1,97.0 612.9,97.3 616.8,96.9 620.7,97.3 624.5,97.2 628.4,96.3 632.3,95.4 636.1,95.3"/>
  <polyline fill="none" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="6,4" points="60.0,220.7 176.0,220.7 214.7,88.7 446.7,88.7 461.2,132.7 640.0,132.7"/>
  <text x="70" y="22" font-size="12">회색 = raw 고도(2 s마다 1점), 파랑 = EMA, 초록 점선 = 정답</text> <text x="670" y="66" font-size="12" text-anchor="end">끝에서 파랑이 초록보다 높다 = 날씨 drift</text>
  <text x="350" y="285" font-size="12" text-anchor="middle">시간 — 120~160 s 3층 오르기, 400~415 s 1층 내려오기</text>
</svg>
```

그림 4 — 합성 기압계 고도(5.2절 코드와 같은 seed로 실제 계산). raw는 ±1 m로 흔들리지만 EMA는 계단 모양을 따라간다. 시간이 갈수록 파랑이 초록 위로 벌어지는 것이 날씨로 인한 가짜 상승이다.

### 5.3 온도 보상, 그리고 생활 속 함정

- MEMS 압력 센서 출력은 온도에 따라 변한다. 그래서 칩 안에 온도 센서가 있고, 공장에서 굽은 보정 계수(OTP)로 압력을 보상한다. 드라이버는 데이터시트의 보상식을 **그대로**(정수 연산 순서까지) 구현해야 한다. Bosch 계열처럼 보상 코드를 레퍼런스로 주는 경우가 많다.
- 주머니에서 꺼내 차가운 바깥에 나가는 것 같은 **온도 급변**은 보상 후에도 짧은 압력 오차를 만들 수 있다.
- 실내 공조, 문 닫힘, 차 창문, 엘리베이터는 모두 "가짜 고도 변화"를 만든다. 바람이 센서 구멍에 직접 닿아도 그렇다.
- 기압계 구멍(port)을 방수 막으로 막으면 응답이 느려진다. 기구 설계가 신호를 바꾼다.

### 5.4 낙상 감지 보조 (B7 연결)

B7 §8의 낙상 감지는 IMU 충격 + 이후 정지가 주 신호다. 기압계는 "**충격 후 기기 높이가 낮아졌는가**"를 확인하는 보조 증거로 쓰인다. 서 있다가 바닥에 쓰러지면 손목 높이가 대략 0.5~1 m 내려간다 → 0.06~0.12 hPa. 위 예제의 노이즈(σ 0.06 hPa)와 같은 크기라서, 몇 초 평균 전후를 비교해야 겨우 보인다. 그래서 **단독 판정이 아니라 IMU 후보의 false alarm을 줄이는 특징 하나**로 쓰는 것이 맞다. (털썩 앉기는 높이가 덜 내려가고, 계단에서 넘어지면 더 내려간다 — 어느 쪽이든 확률적 증거다.)

---

## 6. 온도 센서 — NTC 서미스터와 피부 온도

### 6.1 직관과 Beta 식

**NTC 서미스터**(negative temperature coefficient)는 온도가 오르면 저항이 내려가는 저항이다. 싸고 작고, ADC 한 채널 + 고정저항 하나면 읽힌다. 피부 온도·배터리 온도·충전 보호에 흔히 쓴다. 디지털 온도 센서(I2C로 °C를 바로 주는 칩)도 있지만, 원리를 이해하기엔 NTC가 좋다.

```
 R(T) = R0 · exp(B · (1/T − 1/T0))          T는 켈빈, R0 = T0(25 °C = 298.15 K)에서의 저항
 1/T  = 1/T0 + ln(R / R0) / B               (거꾸로 풀면)

 분압 회로 (NTC가 GND 쪽, ratiometric ADC):
   Vref ── R_fix ──┬── NTC ── GND           code / FS = R_ntc / (R_fix + R_ntc)
                   └──► ADC                 R_ntc = R_fix · code / (FS − code)
```

말로 하면, "ADC code로 저항을 구하고, 저항의 로그가 1/T에 비례한다는 B 식으로 온도를 푼다". Ratiometric이란 ADC 기준전압과 분압 전원이 같아서 전원 전압이 변해도 비율만 남는다는 뜻이다 — Vref 정확도가 필요 없다.

손으로: 10 kΩ @25 °C, B = 3950인 NTC가 33 °C(306.15 K)일 때
1/306.15 − 1/298.15 = 0.0032664 − 0.0033540 = −0.0000876, × 3950 = −0.346, e^(−0.346) = 0.707 → R ≈ 7.07 kΩ. 10 kΩ 고정저항과 분압하면 code = 4095 × 7.07/17.07 ≈ 1696 (반올림 차이로 코드는 1697).

B 식은 근사다. 넓은 온도 범위에서 더 정확히 하려면 계수 3개짜리 **Steinhart–Hart** 식을 쓰거나, 데이터시트의 R-T 표를 lookup + 선형 보간한다(MCU에서 `log`를 피하고 싶을 때도 표가 좋다).

### 6.2 C 구현과 Python golden 비교

이 C 코드는 온도 → code(테스트 벡터 생성) → 저항 → 온도로 왕복하고, 1 LSB가 몇 °C인지 계산한다.

```c
/* NTC 서미스터: ADC code -> 저항 -> 온도 (Beta 식). 10k@25°C, B=3950, 10k 고정저항 분압, 12-bit ratiometric */
#include <stdio.h>
#include <math.h>
#define R0 10000.0
#define T0 298.15                  /* 25 °C [K] */
#define BETA 3950.0
#define RFIX 10000.0
#define FS 4095.0
static double code_to_ohm(int code) { return RFIX * code / (FS - code); }   /* NTC가 아래쪽(GND 쪽) */
static double ohm_to_c(double r) { return 1.0 / (1.0 / T0 + log(r / R0) / BETA) - 273.15; }
static int c_to_code(double c) {   /* 역방향: 테스트 벡터 생성용 */
    double r = R0 * exp(BETA * (1.0 / (c + 273.15) - 1.0 / T0));
    return (int)lround(FS * r / (r + RFIX));
}
int main(void) {
    const double temps[] = {0.0, 25.0, 33.0, 37.0, 45.0};
    printf("%6s %6s %9s %8s %10s\n", "T(C)", "code", "R(ohm)", "T_back", "C per LSB");
    for (int i = 0; i < 5; i++) {
        int code = c_to_code(temps[i]);
        double r = code_to_ohm(code), tb = ohm_to_c(r);
        double lsb = ohm_to_c(code_to_ohm(code - 1)) - tb;    /* 1 LSB 차이가 몇 °C인가 */
        printf("%6.1f %6d %9.1f %8.3f %10.4f\n", temps[i], code, r, tb, lsb);
    }
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 ntc.c -o ntc -lm && ./ntc
```

```text
  T(C)   code    R(ohm)   T_back  C per LSB
   0.0   3156   33610.2    0.006     0.0261
  25.0   2048   10004.9   24.989     0.0220
  33.0   1697    7076.7   32.990     0.0239
  37.0   1534    5989.8   36.998     0.0254
  45.0   1241    4348.3   44.999     0.0296
```

이 Python 코드는 C가 찍은 code를 받아 같은 식으로 변환한다 — 펌웨어 golden 비교의 축소판이다.

```python
# 같은 Beta 변환을 numpy로: C 출력과 대조 (golden 비교)
import numpy as np
R0, T0, BETA, RFIX, FS = 10e3, 298.15, 3950.0, 10e3, 4095.0
codes = np.array([3156, 2048, 1697, 1534, 1241])        # C 프로그램이 찍은 code
r = RFIX * codes / (FS - codes)
tc = 1 / (1 / T0 + np.log(r / R0) / BETA) - 273.15
print("R    :", " ".join("%.1f" % v for v in r))
print("T(C) :", " ".join("%.3f" % v for v in tc))
```

```text
R    : 33610.2 10004.9 7076.7 5989.8 4348.3
T(C) : 0.006 24.989 32.990 36.998 44.999
```

출력에서 볼 것:

- C와 Python의 저항·온도가 소수 셋째 자리까지 같다. 왕복 오차(0.006 °C 등)는 ADC 양자화(정수 code) 때문이다.
- **1 LSB ≈ 0.02~0.03 °C**. 12-bit ADC로도 피부 온도 분해능은 충분하다. 실제 정확도를 결정하는 것은 ADC가 아니라 서미스터 허용오차(±1% 등), B 값 오차, 고정저항 오차, 그리고 아래의 열 문제다.
- 전력 팁: 분압에 상시 전류가 흐르면(1.8 V / 20 kΩ = 90 µA) 웨어러블에선 아깝다. 측정할 때만 GPIO로 분압 전원을 켠다(duty cycle).

### 6.3 피부 온도 vs 주변 온도, 그리고 thermal lag

- 기기의 온도 센서는 **피부와 공기 사이** 어딘가의 온도를 잰다. 케이스 재질, 접촉 압력, 기기 자체 발열(SoC, 충전, 무선)이 섞인다. 그래서 "피부 온도"는 보통 절댓값보다 **변화량**(밤사이 기준 대비 +0.5 °C 같은)으로 쓴다.
- **thermal lag**: 센서와 케이스의 열용량 때문에 온도는 1차 지연 계통처럼 천천히 따라간다. 시정수 τ = 4분이라고 가정하고, 22 °C 책상에서 33 °C 손목으로 옮겼다면

```
 T(t) = 33 − 11 · e^(−t/τ)
 T > 30 °C 가 되는 시각:  e^(−t/4) < 3/11  →  t > 4 · ln(11/3) = 4 · 1.30 ≈ 5.2 분
```

말로 하면, 온도만으로 착용을 판정하면 차고 나서 5분 뒤에야 "착용"이 된다. 거꾸로 벗은 뒤에도 몇 분간 따뜻하다. 온도는 **느리지만 속이기 어려운** 증거이고, 근접·정전용량은 **빠르지만 속기 쉬운** 증거다. §8의 퓨전이 이 둘을 섞는 이유다.

---

## 7. 정전용량 터치와 force

### 7.1 self vs mutual capacitance

손가락(도체이자 접지된 큰 물체)이 전극 근처에 오면 전극의 정전용량이 바뀐다. 칩은 전극을 충·방전하는 시간이나 전하량을 재서 **raw count**로 준다.

| 방식 | 재는 것 | 장점 | 단점 |
|---|---|---|---|
| self capacitance | 전극 하나와 접지 사이 용량. 손가락이 오면 **증가** | 단순, 감도 좋음, 근접(hover) 감지 가능 | 여러 손가락 위치 구분(ghost) 어려움, 물에 약함 |
| mutual capacitance | 송신(TX)·수신(RX) 전극 사이 용량. 손가락이 전기장을 가져가 **감소** | 멀티터치, 물방울에 상대적으로 강함 | 회로·스캔이 복잡 |

웨어러블의 탭·슬라이드 버튼(이어버드 줄기, 안경 다리)은 대부분 self 방식 몇 채널이면 된다. **착용 감지**에도 같은 원리를 쓴다: 뒷면 전극이 피부에 닿으면 용량이 커진다. 피부 접촉을 직접 보는 센서라 근접(IR)보다 "책상 위 반사물"에 잘 속지 않는다.

**force 센서**: 누르는 힘을 strain gauge나 전극 간격 변화(정전용량)로 잰다. "닿았다"와 "눌렀다"를 구분해서, 주머니 속 스침이나 물방울에 의한 오작동을 줄인다.

### 7.2 baseline tracking — drift는 따라가고 터치는 따라가지 않기

raw count는 온도·습도·착용 압력에 따라 **천천히** 변한다(drift). 고정 문턱을 쓰면 drift 때문에 언젠가는 항상 "터치"가 된다. 그래서 펌웨어는 **baseline**(손이 없을 때의 값)을 느린 low-pass로 추적하고, `delta = raw − baseline`으로 판정한다.

```
 baseline ← baseline + α · (raw − baseline)       α = 1/256  (20 Hz에서 시정수 약 12.8 s)
 touch ON  if delta > 30,   touch OFF if delta < 15  (hysteresis)
 터치 중에는 baseline을 멈춘다 (freeze)
```

말로 하면, "천천히 변하는 건 환경이니까 baseline이 흡수하고, 빠르게 변하는 건 손가락이니까 delta로 본다". 시정수 = 1/(α·fs) = 256/20 = 12.8 s. 터치 중에도 baseline을 계속 갱신하면 **길게 누르는 동안 baseline이 손가락까지 흡수**해서 터치가 저절로 풀린다. α를 2의 거듭제곱 역수로 잡으면 MCU에서 `base += (raw − base) >> 8`로 시프트 한 번이다.

이 코드는 5분짜리 합성 raw(+80 count drift, 터치 5번, 그중 하나는 20 s 길게 누르기)에서 세 방법을 비교한다.

```python
# 정전용량 터치: drift를 따라가는 baseline + 터치 중 baseline 동결 (합성 raw count, 20 Hz)
import numpy as np
fs = 20; t = np.arange(0, 300, 1 / fs); rng = np.random.default_rng(4)
drift = 80 * t / 300                                           # 5분간 +80 count (온도·습도 변화 흉내)
touch = np.zeros(t.size)
for s, d in [(30, 1), (90, 2), (150, 0.5), (200, 20), (260, 1.5)]:   # (200, 20) = 20 s 길게 누르기
    touch[(t >= s) & (t < s + d)] = 60
raw = 1000 + drift + touch + rng.normal(0, 3, t.size)
def detect(raw, freeze, alpha=1 / 256, th_on=30, th_off=15):
    base, on, events, on_time = raw[0], False, 0, 0
    for x in raw:
        delta = x - base
        if not on and delta > th_on: on, events = True, events + 1
        elif on and delta < th_off: on = False
        if not (freeze and on): base += alpha * (x - base)      # 터치 중엔 baseline을 멈춘다
        on_time += on
    return events, on_time / fs
print("fixed threshold raw>1040: touch time %.1f s (truth 25.0 s)" % ((raw > 1040).sum() / fs))
for fz in (False, True):
    ev, ot = detect(raw, fz)
    print("baseline tracking, freeze=%-5s: %d touches, touch time %.1f s (truth 5, 25.0 s)" % (fz, ev, ot))
```

```text
fixed threshold raw>1040: touch time 152.4 s (truth 25.0 s)
baseline tracking, freeze=False: 5 touches, touch time 20.8 s (truth 5, 25.0 s)
baseline tracking, freeze=True : 5 touches, touch time 25.0 s (truth 5, 25.0 s)
```

출력에서 볼 것:

- 고정 문턱(raw > 1040)은 drift가 쌓인 뒤 152 s 동안 "터치 중"이라고 판정했다(정답 25 s). 완전한 실패다.
- baseline 추적은 터치 5번을 모두 찾았다. 그러나 **freeze가 없으면 길게 누르기의 뒤쪽 4초를 놓친다**(20.8 s). freeze가 있으면 25.0 s로 정확하다.
- freeze의 위험: 물방울이 붙어서 delta가 계속 크면 baseline이 영원히 멈춘다. 그래서 실제 펌웨어는 "터치가 N초 이상 지속되면 강제 재보정"(timeout recalibration) 규칙을 함께 둔다. 반대로 baseline이 raw보다 **위로** 벌어지면(음의 delta, 예: 전원 켤 때 손가락이 이미 있었다) 빠르게 내려 맞추는 규칙도 흔하다.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <line x1="60" y1="250" x2="640" y2="250" stroke="currentColor" stroke-width="1"/> <line x1="60" y1="30" x2="60" y2="250" stroke="currentColor" stroke-width="1"/> <g font-size="12" text-anchor="end">
  <text x="54" y="228.1">1000</text><text x="54" y="163.4">1050</text><text x="54" y="98.7">1100</text><text x="54" y="34">1150</text> </g> <g stroke="#888" stroke-width="0.6" stroke-dasharray="3,3">
  <line x1="60" y1="224.1" x2="640" y2="224.1"/><line x1="60" y1="159.4" x2="640" y2="159.4"/><line x1="60" y1="94.7" x2="640" y2="94.7"/> </g>
  <line x1="60" y1="172.4" x2="640" y2="172.4" stroke="#d0564a" stroke-width="1" stroke-dasharray="2,3"/> <text x="636" y="188" font-size="12" text-anchor="end">고정 문턱 1040</text>
  <polyline fill="none" stroke="#888" stroke-width="1" points="60.0,215.1 61.9,217.9 63.9,215.3 65.8,214.9 67.7,213.3 69.7,217.0 71.6,213.2 73.5,216.9 75.5,213.9 77.4,214.9 79.3,213.6 81.3,211.7 83.2,212.3 85.1,208.6 87.1,208.9 89.0,213.9 90.9,212.5 92.9,211.2 94.8,208.4 96.7,210.4 98.7,210.7 100.6,208.8 102.5,212.8 104.5,208.2 106.4,211.0 108.3,206.0 110.3,209.6 112.2,209.1 114.1,207.6 116.1,205.4 118.0,126.1 119.9,204.4 121.9,206.7 123.8,205.9 125.7,202.4 127.7,206.1 129.6,206.3 131.5,204.9 133.5,205.1 135.4,204.3 137.3,202.5 139.3,202.6 141.2,199.7 143.1,204.0 145.1,205.2 147.0,200.3 148.9,200.6 150.9,201.5 152.8,198.4 154.7,198.8 156.7,198.3 158.6,196.4 160.5,196.3 162.5,198.8 164.4,198.1 166.3,196.9 168.3,191.5 170.2,200.5 172.1,195.5 174.1,198.6 176.0,193.6 177.9,197.3 179.9,192.3 181.8,194.9 183.7,195.5 185.7,194.4 187.6,198.3 189.5,194.6 191.5,195.9 193.4,192.4 195.3,194.3 197.3,192.1 199.2,192.7 201.1,192.8 203.1,189.3 205.0,191.3 206.9,190.8 208.9,191.5 210.8,190.1 212.7,188.1 214.7,191.5 216.6,188.1 218.5,188.5 220.5,186.8 222.4,188.9 224.3,186.7 226.3,188.6 228.2,187.2 230.1,189.9 232.1,188.1 234.0,110.2 235.9,104.6 237.9,186.0 239.8,186.7 241.7,184.0 243.7,183.9 245.6,185.4 247.5,182.9 249.5,182.8 251.4,179.8 253.3,182.8 255.3,180.4 257.2,184.5 259.1,181.5 261.1,184.3 263.0,181.2 264.9,184.4 266.9,182.4 268.8,179.0 270.7,179.8 272.7,178.5 274.6,178.0 276.5,177.0 278.5,177.1 280.4,180.3 282.3,175.7 284.3,179.6 286.2,174.5 288.1,177.8 290.1,172.9 292.0,174.9 293.9,175.5 295.9,178.1 297.8,175.1 299.7,176.3 301.7,174.2 303.6,175.5 305.5,174.3 307.5,172.5 309.4,175.1 311.3,168.9 313.3,166.2 315.2,173.1 317.1,167.8 319.1,172.8 321.0,171.0 322.9,170.1 324.9,168.3 326.8,169.7 328.7,169.1 330.7,172.5 332.6,163.1 334.5,165.9 336.5,167.2 338.4,168.7 340.3,167.1 342.3,166.6 344.2,168.5 346.1,168.4 348.1,167.0 350.0,88.0 351.9,167.0 353.9,164.6 355.8,164.3 357.7,165.7 359.7,163.7 361.6,162.5 363.5,161.2 365.5,164.2 367.4,161.2 369.3,161.0 371.3,159.2 373.2,159.8 375.1,162.1 377.1,158.6 379.0,162.9 380.9,156.5 382.9,161.2 384.8,158.8 386.7,156.4 388.7,159.2 390.6,159.1 392.5,158.2 394.5,158.2 396.4,157.0 398.3,157.2 400.3,156.4 402.2,157.5 404.1,153.7 406.1,152.0 408.0,155.0 409.9,153.1 411.9,155.4 413.8,156.4 415.7,152.8 417.7,155.1 419.6,152.5 421.5,149.9 423.5,153.2 425.4,152.0 427.3,151.3 429.3,145.7 431.2,150.2 433.1,149.8 435.1,151.8 437.0,147.5 438.9,150.6 440.9,146.6 442.8,146.6 444.7,146.5 446.7,72.7 448.6,70.1 450.5,68.4 452.5,69.9 454.4,68.3 456.3,70.6 458.3,68.2 460.2,70.9 462.1,67.4 464.1,66.4 466.0,68.6 467.9,63.6 469.9,65.6 471.8,66.8 473.7,65.7 475.7,64.3 477.6,67.0 479.5,63.4 481.5,62.2 483.4,63.2 485.3,139.5 487.3,140.9 489.2,141.9 491.1,140.4 493.1,138.7 495.0,139.3 496.9,137.3 498.9,133.4 500.8,138.5 502.7,135.7 504.7,139.0 506.6,138.9 508.5,131.6 510.5,134.5 512.4,136.2 514.3,135.4 516.3,133.5 518.2,134.4 520.1,133.9 522.1,135.6 524.0,136.1 525.9,129.7 527.9,132.3 529.8,134.6 531.7,132.2 533.7,131.2 535.6,131.3 537.5,132.1 539.5,131.4 541.4,130.0 543.3,131.7 545.3,128.8 547.2,132.5 549.1,130.5 551.1,129.1 553.0,128.5 554.9,131.4 556.9,123.5 558.8,130.0 560.7,127.5 562.7,48.2 564.6,50.6 566.5,126.3 568.5,125.5 570.4,124.2 572.3,126.4 574.3,124.3 576.2,123.6 578.1,122.6 580.1,125.6 582.0,124.2 583.9,124.3 585.9,127.2 587.8,124.4 589.7,120.4 591.7,118.9 593.6,120.4 595.5,119.6 597.5,119.9 599.4,120.8 601.3,122.7 603.3,121.6 605.2,115.5 607.1,118.3 609.1,119.3 611.0,117.3 612.9,117.8 614.9,116.0 616.8,117.8 618.7,119.2 620.7,116.7 622.6,115.5 624.5,114.0 626.5,115.1 628.4,114.6 630.3,114.9 632.3,116.4 634.2,116.6 636.1,114.7 638.1,115.1"/>
  <polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="60.0,226.6 63.9,226.3 67.7,225.8 71.6,225.3 75.5,224.7 79.3,224.1 83.2,223.5 87.1,222.8 90.9,222.2 94.8,221.5 98.7,221.2 102.5,220.6 106.4,220.1 110.3,219.3 114.1,218.8 118.0,218.0 121.9,217.7 125.7,217.1 129.6,216.2 133.5,215.6 137.3,214.9 141.2,214.2 145.1,213.5 148.9,212.9 152.8,212.1 156.7,211.4 160.5,210.8 164.4,210.0 168.3,209.3 172.1,208.6 176.0,208.0 179.9,207.2 183.7,206.4 187.6,205.7 191.5,205.1 195.3,204.4 199.2,203.5 203.1,202.9 206.9,202.1 210.8,201.4 214.7,200.6 218.5,199.9 222.4,199.3 226.3,198.5 230.1,197.9 234.0,197.3 237.9,197.3 241.7,196.6 245.6,195.9 249.5,195.2 253.3,194.5 257.2,193.8 261.1,193.2 264.9,192.4 268.8,191.7 272.7,190.9 276.5,190.1 280.4,189.4 284.3,188.7 288.1,187.9 292.0,187.2 295.9,186.4 299.7,185.8 303.6,185.1 307.5,184.4 311.3,183.8 315.2,183.0 319.1,182.2 322.9,181.7 326.8,180.9 330.7,180.2 334.5,179.6 338.4,178.7 342.3,178.0 346.1,177.5 350.0,176.7 353.9,176.3 357.7,175.4 361.6,174.9 365.5,174.1 369.3,173.5 373.2,172.7 377.1,172.0 380.9,171.4 384.8,170.7 388.7,169.9 392.5,169.3 396.4,168.5 400.3,167.8 404.1,167.1 408.0,166.4 411.9,165.7 415.7,165.1 419.6,164.5 423.5,163.6 427.3,163.0 431.2,162.4 435.1,161.6 438.9,160.9 442.8,160.1 446.7,159.3 450.5,159.3 454.4,159.3 458.3,159.3 462.1,159.3 466.0,159.3 469.9,159.3 473.7,159.3 477.6,159.3 481.5,159.3 485.3,159.2 489.2,157.7 493.1,156.1 496.9,154.7 500.8,153.4 504.7,152.1 508.5,151.0 512.4,149.8 516.3,148.8 520.1,147.9 524.0,147.0 527.9,146.0 531.7,145.2 535.6,144.3 539.5,143.6 543.3,142.8 547.2,142.1 551.1,141.3 554.9,140.5 558.8,139.7 562.7,139.0 566.5,138.9 570.4,138.0 574.3,137.3 578.1,136.5 582.0,136.0 585.9,135.2 589.7,134.5 593.6,133.6 597.5,132.8 601.3,132.0 605.2,131.2 609.1,130.3 612.9,129.8 616.8,129.1 620.7,128.4 624.5,127.7 628.4,127.0 632.3,126.2 636.1,125.6"/>
  <polyline fill="none" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="5,3" points="60.0,187.8 63.9,187.5 67.7,187.0 71.6,186.5 75.5,185.9 79.3,185.3 83.2,184.7 87.1,183.9 90.9,183.4 94.8,182.7 98.7,182.4 102.5,181.8 106.4,181.2 110.3,180.5 114.1,179.9 118.0,179.2 121.9,178.9 125.7,178.2 129.6,177.4 133.5,176.8 137.3,176.1 141.2,175.3 145.1,174.7 148.9,174.1 152.8,173.3 156.7,172.6 160.5,172.0 164.4,171.2 168.3,170.5 172.1,169.8 176.0,169.1 179.9,168.4 183.7,167.6 187.6,166.9 191.5,166.2 195.3,165.6 199.2,164.7 203.1,164.1 206.9,163.2 210.8,162.5 214.7,161.8 218.5,161.1 222.4,160.5 226.3,159.7 230.1,159.1 234.0,158.5 237.9,158.5 241.7,157.8 245.6,157.0 249.5,156.3 253.3,155.6 257.2,154.9 261.1,154.4 264.9,153.6 268.8,152.9 272.7,152.0 276.5,151.2 280.4,150.6 284.3,149.9 288.1,149.1 292.0,148.3 295.9,147.6 299.7,147.0 303.6,146.3 307.5,145.6 311.3,144.9 315.2,144.1 319.1,143.4 322.9,142.9 326.8,142.1 330.7,141.4 334.5,140.7 338.4,139.9 342.3,139.2 346.1,138.6 350.0,137.9 353.9,137.4 357.7,136.6 361.6,136.0 365.5,135.3 369.3,134.7 373.2,133.9 377.1,133.2 380.9,132.5 384.8,131.9 388.7,131.1 392.5,130.5 396.4,129.6 400.3,129.0 404.1,128.3 408.0,127.6 411.9,126.9 415.7,126.3 419.6,125.7 423.5,124.8 427.3,124.2 431.2,123.6 435.1,122.8 438.9,122.1 442.8,121.2 446.7,120.4 450.5,120.4 454.4,120.4 458.3,120.4 462.1,120.4 466.0,120.4 469.9,120.4 473.7,120.4 477.6,120.4 481.5,120.4 485.3,120.4 489.2,118.8 493.1,117.3 496.9,115.9 500.8,114.5 504.7,113.3 508.5,112.1 512.4,110.9 516.3,110.0 520.1,109.1 524.0,108.2 527.9,107.2 531.7,106.4 535.6,105.5 539.5,104.7 543.3,104.0 547.2,103.3 551.1,102.4 554.9,101.6 558.8,100.9 562.7,100.2 566.5,100.0 570.4,99.2 574.3,98.4 578.1,97.7 582.0,97.2 585.9,96.4 589.7,95.7 593.6,94.8 597.5,93.9 601.3,93.1 605.2,92.3 609.1,91.5 612.9,91.0 616.8,90.2 620.7,89.6 624.5,88.9 628.4,88.1 632.3,87.4 636.1,86.8"/>
  <g font-size="12" text-anchor="middle">
  <text x="60" y="267">0</text><text x="156.7" y="267">50</text><text x="253.3" y="267">100</text><text x="350" y="267">150</text><text x="446.7" y="267">200</text><text x="543.3" y="267">250</text><text x="640" y="267">300 s</text>
  </g> <text x="70" y="22" font-size="12">회색 = raw (1 s 최댓값), 파랑 = baseline (freeze), 주황 점선 = baseline + 30 (ON 문턱)</text> <text x="350" y="285" font-size="12" text-anchor="middle">터치: 30, 90, 150(0.5 s), 200~220(길게), 260 s</text>
</svg>
```

그림 5 — 합성 정전용량 raw와 추적 baseline(7.2절 코드와 같은 seed로 실제 계산). baseline이 +80 drift를 따라 올라가고, 200~220 s 길게 누르기 동안에는 수평으로 멈춘다. 빨간 점선(고정 문턱 1040)은 약 150 s 이후 drift만으로 raw가 넘어가 버린다.

---

## 8. 착용 감지 = 센서 퓨전

### 8.1 왜 센서 하나로는 안 되나

| 센서 | 빠르기 | 전력 | 속는 상황 |
|---|---|---|---|
| IR 근접 | 빠름 (초) | 낮음 | 책상 위 반사물, 손이 지나감, 어두운 피부·문신, 밴드 들뜸 |
| 정전용량 | 빠름 | 매우 낮음 | 물·땀, 금속 책상, 손에 쥔 상태(피부 접촉이지만 착용 아님) |
| 피부 온도 | 느림 (분) | 매우 낮음 | 햇볕 드는 창가, 따뜻한 주머니, 기기 자체 발열 |
| IMU 움직임 | 빠름 | 낮음 (이미 켜져 있음) | **잠잘 때 착용 중인데 정지**, 가방 속 이동 |
| PPG 맥박 | 수 초 | 높음 (LED) | 가장 확실하지만 비싸서 확인용 |

각 센서는 서로 다른 상황에서 속는다. 그래서 **약한 증거 여러 개를 점수로 합치고**, 그 점수에 hysteresis와 debounce를 건다. 펌웨어로 말하면 "여러 개의 noisy한 상태 비트를 voting + 상태 기계로 안정화"하는 것이다.

### 8.2 규칙 기반 FSM — 설계

```svg
<svg viewBox="0 0 660 260" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="g6b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <g font-size="12">
  <rect x="10" y="30" width="150" height="150" rx="6" fill="none" stroke="#888" stroke-width="1.5"/> <text x="85" y="22" text-anchor="middle" font-size="13">증거 (1분마다)</text> <text x="22" y="58">근접 prox × 1</text>
  <text x="22" y="86">정전용량 cap × 3</text> <text x="22" y="114">온도 &gt; 30 °C × 1</text> <text x="22" y="142">IMU 움직임 × 1</text> <text x="22" y="168">e = 합 (0~6)</text> </g>
  <line x1="160" y1="105" x2="218" y2="105" stroke="currentColor" stroke-width="1.5" marker-end="url(#g6b)"/> <circle cx="290" cy="105" r="55" fill="none" stroke="#888" stroke-width="2"/>
  <text x="290" y="101" font-size="14" text-anchor="middle">OFF-BODY</text> <text x="290" y="119" font-size="12" text-anchor="middle">센서 저속 polling</text>
  <circle cx="540" cy="105" r="55" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="540" y="101" font-size="14" text-anchor="middle">ON-BODY</text> <text x="540" y="119" font-size="12" text-anchor="middle">기능 활성</text>
  <path d="M335,75 Q415,35 495,75" fill="none" stroke="#3f9a6b" stroke-width="2" marker-end="url(#g6b)"/> <text x="415" y="40" font-size="12" text-anchor="middle">e ≥ 4 가 2분 연속</text>
  <path d="M495,135 Q415,175 335,135" fill="none" stroke="#d0564a" stroke-width="2" marker-end="url(#g6b)"/> <text x="415" y="182" font-size="12" text-anchor="middle">e ≤ 2 가 3분 연속</text>
  <text x="415" y="215" font-size="12" text-anchor="middle">e = 3 은 어느 쪽으로도 안 움직이는 구간 (hysteresis)</text> <text x="415" y="240" font-size="12" text-anchor="middle">조건이 한 번이라도 끊기면 카운터 0 (debounce)</text>
</svg>
```

그림 6 — 점수 기반 착용 감지 FSM(아래 v2 설정). 정전용량(피부 접촉)에 가장 큰 가중치를 주고, 들어갈 때는 높은 점수(4)를, 머물 때는 그보다 낮은 점수(3)를 요구한다. 들어갈 때 2분, 나갈 때 3분 연속을 요구해 짧은 잡음을 무시한다.

설계 이유:

- 잘 때(움직임 거의 없음)도 prox + cap + temp = 5라서 ON을 유지한다. "움직임이 없으면 벗었다"는 규칙은 수면 추적을 망친다.
- 밴드가 잠깐 헐거워져 근접이 0이 돼도 cap 3 + temp 1 = 4 ≥ 3이라 유지한다.
- 창가 책상: 근접 반사물(1) + 따뜻한 햇볕(1) = 2 < 3 → 3분 뒤 OFF. 그리고 OFF에서 다시 들어가려면 4가 필요한데 cap이 없으면 최대 3이라 들어가지 못한다.

### 8.3 C로 하루 시뮬레이션 — 가중치 설계의 차이

이 코드는 1분 1샘플로 합성 하루(1440분)를 만들고, 두 가중치 설정(v1: 근접과 정전용량 동등, v2: 정전용량 우선)을 같은 난수열로 비교한다. 근접 센서 하나만 쓰는 경우도 함께 센다. 장면: 0~7시 수면(착용), 7:00~7:20 샤워(벗음), 12:00 밴드 헐거움 5분, 18:00~18:30 창가 책상 위(벗음, 햇볕 31 °C, 근접 반사물 50%), 23:00 충전(벗음).

```c
/* 착용 감지 센서 퓨전: 근접 + 정전용량 + 피부온도 + IMU 움직임 -> 점수 -> hysteresis/debounce FSM
   합성 하루 (1분 1샘플, 1440분). 모든 값은 설명용 가정 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
static uint32_t s;
static double urand(void) { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0; }
static int truth(int m) {                         /* 1 = 착용 */
    if (m >= 420 && m < 440) return 0;            /* 07:00 샤워 */
    if (m >= 1080 && m < 1110) return 0;          /* 18:00 창가 책상 위 (햇볕) */
    return m < 1380;                              /* 23:00 충전 */
}
static void run(const char *name, int wp, int wc, int th_on, int th_stay) {
    double temp = 33.0; s = 7u;
    int state = 1, cnt = 0, wrong = 0, trans = 0, p_state = 1, p_wrong = 0, p_trans = 0;
    printf("[%s] e = %d*prox + %d*cap + temp>30 + motion, ON if e>=%d, stay ON if e>=%d\n", name, wp, wc, th_on, th_stay);
    for (int m = 0; m < 1440; m++) {
        int w = truth(m), asleep = m < 420, window = m >= 1080 && m < 1110;
        temp += ((w ? 33.0 : window ? 31.0 : 22.0) - temp) * (1.0 - exp(-1.0 / 4.0));  /* 열 시정수 4분 */
        int prox = w ? urand() > 0.03 : (window && urand() < 0.5);   /* 착용 중 3% 들뜸, 창가 반사물 */
        if (m >= 720 && m < 725) prox = 0;                            /* 12:00 밴드 헐거움 5분 */
        int cap = w ? urand() > 0.05 : 0;
        int motion = w ? urand() < (asleep ? 0.1 : 0.7) : 0;
        int e = wp * prox + wc * cap + (temp > 30.0) + motion;
        int want = state ? (e >= th_stay) : (e >= th_on);            /* hysteresis */
        if (want != state) {
            if (++cnt >= (state ? 3 : 2)) {                          /* debounce: OFF 3분, ON 2분 */
                state = want; cnt = 0; trans++;
                printf("  %02d:%02d e=%d temp=%.1f -> %s\n", m / 60, m % 60, e, temp, state ? "ON" : "OFF");
            }
        } else cnt = 0;
        if (prox != p_state) { p_state = prox; p_trans++; }           /* 비교: 근접 센서 하나만 */
        wrong += state != w; p_wrong += p_state != w;
    }
    printf("  fusion: %d transitions, %d wrong min | prox only: %d transitions, %d wrong min (truth 5)\n",
           trans, wrong, p_trans, p_wrong);
}
int main(void) { run("v1", 2, 2, 4, 2); run("v2", 1, 3, 4, 3); return 0; }
```

```sh
cc -std=c11 -Wall -Wextra -O2 wear_fusion.c -o wear_fusion -lm && ./wear_fusion
```

```text
[v1] e = 2*prox + 2*cap + temp>30 + motion, ON if e>=4, stay ON if e>=2
  07:02 e=0 temp=27.2 -> OFF
  07:21 e=5 temp=26.4 -> ON
  18:17 e=1 temp=31.0 -> OFF
  18:31 e=5 temp=31.8 -> ON
  23:02 e=0 temp=27.2 -> OFF
  fusion: 5 transitions, 23 wrong min | prox only: 89 transitions, 55 wrong min (truth 5)
[v2] e = 1*prox + 3*cap + temp>30 + motion, ON if e>=4, stay ON if e>=3
  07:02 e=0 temp=27.2 -> OFF
  07:21 e=5 temp=26.4 -> ON
  18:02 e=2 temp=31.9 -> OFF
  18:31 e=5 temp=31.8 -> ON
  23:02 e=0 temp=27.2 -> OFF
  fusion: 5 transitions, 8 wrong min | prox only: 89 transitions, 55 wrong min (truth 5)
```

출력에서 볼 것:

- **근접 하나만**: 89번 토글, 55분 오판. 착용 중 3% 들뜸이 매번 토글이 되고 창가 반사물에 속는다.
- **v1**(근접 = 정전용량 = 2, 머물기 문턱 2): 전이는 5번으로 정답과 같지만 18:00 창가에서 **17분 늦게** OFF가 됐다(18:17). 벗은 직후 온도가 30 °C 위에 남아 있고(thermal lag + 햇볕) 근접 반사물이 2점을 주니 점수 3이 자꾸 나와서 "e ≤ 1이 3분 연속" 조건이 끊긴 것이다.
- **v2**(정전용량 3, 머물기 문턱 3): 같은 상황에서 18:02에 OFF. 하루 오판 8분은 전부 debounce 지연(전이마다 1~2분)이다.
- 교훈: 퓨전 규칙의 성능은 **가중치보다 "어떤 상황에서 무엇이 속는가"의 목록**에서 나온다. 그 목록이 곧 테스트 시나리오다. Don이 factory test에서 corner case 목록을 만들던 방식 그대로다.
- 이 숫자들은 합성 시나리오에 맞춰 만든 것이다. 실제로는 사용자·기구·환경 데이터를 모아서(H4) 가중치와 문턱을 정하고, 하루 단위 "오판 분"과 "전이 횟수"를 지표로 본다.

### 8.4 규칙 vs ML (B7 연결)

B7 §1은 착용 감지를 "규칙 + hysteresis, 작은 tree"로 분류했다. 여기서 본 것처럼 규칙은

- 해석 가능하고, 몇십 바이트·몇십 연산이며, 실패 상황을 시나리오로 테스트할 수 있다.
- 하지만 가중치·문턱을 사람이 고르고, 센서가 늘거나 사용자 변동(피부색, 손목 굵기)이 크면 손으로 맞추기 어렵다.

중간 단계가 **같은 특징(prox count, cap delta, temp, temp 기울기, IMU 분산)을 넣은 작은 decision tree**다. B7 §5처럼 C로 내보내면 규칙과 비슷한 크기로 데이터에서 문턱을 배운다. 출력 확률에는 여전히 이 절의 hysteresis + debounce FSM을 건다(B7 §7.3). 어느 쪽이든 **후처리 FSM은 남는다** — 모델은 "지금 이 순간의 증거"를, FSM은 "상태의 안정성"을 담당한다.

### 8.5 전력 관점 — 최소 전력 착용 감지

1. 정전용량(가장 쌈)과 IMU의 any-motion 인터럽트(이미 켜져 있음)를 상시 1차 감시로 둔다.
2. 그 둘이 상태 변화를 시사할 때만 근접 IR을 켜고(수 µA 평균), 온도는 1분에 한 번.
3. PPG는 "착용 중인데 확신이 낮을 때" 몇 초만 확인용으로 켠다.
4. 센서 내부 인터럽트(근접 문턱, IMU wake-on-motion)를 최대한 써서 MCU를 재우고, FSM은 MCU가 깨어날 때만 돈다.

---

## 9. 그 밖의 센서들 — 짧게

### 9.1 지자기계 (magnetometer) — G1 연결

3축 지자기 센서로 방위(나침반)를 얻는다. G1에서 다룬 9축 IMU의 세 번째 센서다. 주변 금속·자석(스피커, 노트북)의 영향이 커서 **hard-iron/soft-iron 캘리브레이션**이 필수이고, 실내에서는 방위가 자주 틀린다. 웨어러블에서는 머리 방향(head tracking)의 yaw drift 보정에 쓰인다(G3의 퓨전).

### 9.2 주변광 센서 (ALS)

PD에 사람 눈 감도와 비슷한 필터를 얹어 lux를 준다. 화면 밝기 자동 조절이 주 용도이고, 카메라 AE의 힌트, "주머니·가방 안(어두움)" 판단, 착용 감지의 약한 보조 증거(뒷면이 피부에 덮여 어두움)로도 쓸 수 있다. 형광등 깜빡임과 적외선 성분 때문에 채널 여러 개를 섞어 보정한다.

### 9.3 UWB — 정밀 거리 (hedge)

UWB(ultra-wideband)는 아주 짧은 펄스(넓은 대역)로 두 기기 사이의 **비행 시간**을 재서 거리를 얻는 무선이다. two-way ranging으로 수십 cm 이하 정확도가 흔히 언급되고, 안테나 여러 개로 도래각(AoA)도 잰다. 물건 찾기 태그, 디지털 차 키, 기기 간 "가까이 대면 연결"에 쓰인다. 원리는 §4.2 dToF와 같다(빛 대신 전파). Hark 같은 기기가 UWB를 쓸지는 알 수 없지만, 무선 칩 통합(Don의 현재 일)과 맞닿는 주제라 대화 소재가 된다 — E8 §8의 radio·coexistence와 연결된다.

### 9.4 GNSS — 전력을 많이 먹는 위치 센서

GPS 등 위성 신호로 위치를 얻는다. 고려할 점은 거의 전부 전력이다.

- 처음 위치를 잡는 acquisition(특히 위성 궤도 정보가 없는 cold start)은 수십 초 이상 걸릴 수 있고, 그동안 수십 mW급을 쓴다(부품·모드에 따라 다름). tracking 중에도 웨어러블 기준으로는 큰 전력이다.
- 그래서 **duty cycling**(가끔 켜서 위치만 갱신), **assisted GNSS**(폰이 궤도 정보를 미리 넘겨 줌), 그리고 "폰에 연결돼 있으면 폰의 위치를 빌린다"가 표준 전략이다.
- 실내·도심 골목에서는 정확도가 크게 떨어진다. 위치가 필요한 AI 기능이라면 Wi-Fi/BLE 기반 위치나 폰 위치를 우선 고려한다.

---

## 10. 임베디드 관점에서 다시 보기

### 10.1 버스와 센서 허브 배치 (E8 연결)

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="g6c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="250" y="95" width="170" height="80" rx="8" fill="none" stroke="#4a7bd0" stroke-width="2"/> <text x="335" y="128" font-size="14" text-anchor="middle">센서 허브 MCU</text>
  <text x="335" y="148" font-size="12" text-anchor="middle">always-on, µA~mW</text> <g font-size="12">
  <rect x="20" y="20" width="120" height="34" rx="5" fill="none" stroke="currentColor"/><text x="80" y="42" text-anchor="middle">IMU (FIFO)</text>
  <rect x="20" y="70" width="120" height="34" rx="5" fill="none" stroke="currentColor"/><text x="80" y="92" text-anchor="middle">PPG AFE (FIFO)</text> <line x1="140" y1="37" x2="250" y2="110" stroke="#e08a3c" stroke-width="2"/>
  <line x1="140" y1="87" x2="250" y2="120" stroke="#e08a3c" stroke-width="2"/> <text x="195" y="60" text-anchor="middle">SPI</text>
  <rect x="20" y="150" width="120" height="34" rx="5" fill="none" stroke="currentColor"/><text x="80" y="172" text-anchor="middle">근접 · ToF</text>
  <rect x="20" y="200" width="120" height="34" rx="5" fill="none" stroke="currentColor"/><text x="80" y="222" text-anchor="middle">기압계</text>
  <rect x="160" y="230" width="120" height="34" rx="5" fill="none" stroke="currentColor"/><text x="220" y="252" text-anchor="middle">cap touch · ALS</text>
  <line x1="140" y1="167" x2="250" y2="160" stroke="#3f9a6b" stroke-width="2"/> <line x1="140" y1="217" x2="255" y2="172" stroke="#3f9a6b" stroke-width="2"/>
  <line x1="230" y1="230" x2="270" y2="175" stroke="#3f9a6b" stroke-width="2"/> <text x="180" y="200" text-anchor="middle">I2C 공유 버스</text>
  <rect x="300" y="230" width="110" height="34" rx="5" fill="none" stroke="currentColor"/><text x="355" y="252" text-anchor="middle">NTC (ADC)</text> <line x1="345" y1="230" x2="340" y2="175" stroke="#888" stroke-width="2"/>
  <rect x="530" y="20" width="140" height="34" rx="5" fill="none" stroke="currentColor"/><text x="600" y="42" text-anchor="middle">PDM 마이크 (G4)</text> <line x1="530" y1="37" x2="420" y2="110" stroke="#888" stroke-width="2"/>
  <rect x="510" y="110" width="160" height="60" rx="6" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="590" y="135" text-anchor="middle" font-size="13">큰 SoC (AP·ISP·NPU)</text>
  <text x="590" y="155" text-anchor="middle">대부분 잠들어 있음</text> <line x1="420" y1="135" x2="508" y2="135" stroke="currentColor" stroke-width="1.5" marker-end="url(#g6c)"/> <text x="464" y="128" text-anchor="middle">이벤트·batch</text>
  <rect x="530" y="210" width="140" height="34" rx="5" fill="none" stroke="currentColor"/><text x="600" y="232" text-anchor="middle">카메라</text>
  <line x1="590" y1="210" x2="590" y2="172" stroke="#d0564a" stroke-width="2" marker-end="url(#g6c)"/> <text x="635" y="195" text-anchor="middle">CSI-2</text> </g>
</svg>
```

그림 7 — 센서 버스 배치 예(가상). 고속·FIFO 센서(IMU, PPG)는 SPI, 저속 환경 센서는 I2C 하나를 공유, NTC는 ADC 직결, 카메라만 MIPI CSI-2로 큰 SoC에 직접 붙는다. 센서 허브는 결과와 batch만 SoC로 넘긴다.

- **I2C 공유**: 400 kHz Fast-mode에서 10바이트 읽기는 주소·레지스터 포함 약 0.3 ms 남짓이다. 저속 센서 여러 개는 문제없지만, 한 센서가 SDA를 붙잡고 있으면(bus hang) 나머지가 다 멈춘다. 9 클럭 bus recovery 루틴, 센서별 전원 분리, 타임아웃은 Don의 버스 디버깅 경험이 그대로 쓰이는 곳이다. 새 설계에서는 I3C도 후보다.
- **인터럽트 핀**: 근접 문턱, IMU wake-on-motion, PPG FIFO watermark가 각각 MCU를 깨운다. 핀이 모자라면 open-drain wired-OR로 묶고 상태 레지스터를 읽어 원인을 가린다.
- **카메라는 허브를 거치지 않는다**: 대역폭(수백 Mbit/s)이 MCU가 감당할 수준이 아니다. 예외가 B6 §9의 저해상도 흑백 센서(SPI·parallel).

### 10.2 샘플링 · 전력 · 로그 크기 — 한 표로

이 코드는 센서별 데이터율, 하루 로그 크기, 150 mAh 셀 대비 하루 에너지 비율을 계산한다. 전력 값은 **설명용 가정**이다(같은 종류의 센서도 부품·설정에 따라 몇 배씩 다르다).

```python
# 센서별 데이터율과 전력 예산: 하루 로그 크기, 150 mAh 셀(555 mWh)에서 차지하는 비율
# 전력은 "켜 둔 상태의 평균"에 대한 자릿수 감각용 가정값이다 (부품·설정마다 몇 배씩 다르다)
sensors = [  # name, Hz, bytes/sample, avg mW (가정)
    ("IMU accel 50 Hz", 50, 6, 0.05), ("IMU 6-axis 100 Hz", 100, 12, 1.0),
    ("mic 16 kHz 16-bit", 16000, 2, 0.8), ("PPG green 25 Hz", 25, 3, 0.5),
    ("prox 1 Hz", 1, 2, 0.02), ("baro 1 Hz", 1, 3, 0.01), ("skin temp 1/60 Hz", 1 / 60, 2, 0.001),
    ("cap touch 20 Hz", 20, 2, 0.03), ("camera QVGA Y 1 fps", 1, 320 * 240, 1.0),
    ("GNSS tracking 1 Hz", 1, 32, 25.0),
]
batt = 3.7 * 150
print("%-20s %12s %10s %8s %9s" % ("sensor", "bytes/s", "MB/day", "mW", "% batt/d"))
for n, hz, b, mw in sensors:
    bps = hz * b
    print("%-20s %12.2f %10.3f %8.3f %8.1f%%" % (n, bps, bps * 86400 / 1e6, mw, 100 * mw * 24 / batt))
```

```text
sensor                    bytes/s     MB/day       mW  % batt/d
IMU accel 50 Hz            300.00     25.920    0.050      0.2%
IMU 6-axis 100 Hz         1200.00    103.680    1.000      4.3%
mic 16 kHz 16-bit        32000.00   2764.800    0.800      3.5%
PPG green 25 Hz             75.00      6.480    0.500      2.2%
prox 1 Hz                    2.00      0.173    0.020      0.1%
baro 1 Hz                    3.00      0.259    0.010      0.0%
skin temp 1/60 Hz            0.03      0.003    0.001      0.0%
cap touch 20 Hz             40.00      3.456    0.030      0.1%
camera QVGA Y 1 fps      76800.00   6635.520    1.000      4.3%
GNSS tracking 1 Hz          32.00      2.765   25.000    108.1%
```

출력에서 볼 것:

- **데이터율**은 마이크(32 KB/s)와 카메라가 압도적이고, 환경 센서들은 하루 1 MB도 안 된다. 원본 로그 수집(H1)을 설계할 때 "무엇을 raw로 남기고 무엇을 특징으로만 남길지"는 이 표에서 정해진다. 카메라·오디오 원본을 하루 종일 남기는 것은 저장·전송(H2)·프라이버시(H6) 모두에서 불가능에 가깝다.
- **전력**은 데이터율과 순서가 다르다. GNSS를 계속 켜 두면 이 가정으로는 하루 배터리의 108% — 하루를 못 간다. PPG(LED)와 6축 IMU(자이로)가 그다음이다. 근접·기압·온도·터치는 거의 공짜다. 그래서 착용 감지 같은 상시 기능은 **싼 센서로 1차 판정하고 비싼 센서는 확인용**으로만 켠다(§8.5).
- 이것이 E8의 always-on island 설계를 숫자로 본 것이다: 상시 도는 것은 왼쪽 아래의 싼 센서와 허브, 비싼 것은 이벤트가 있을 때만.

### 10.3 bring-up 체크리스트 (센서 공통)

- 전원 시퀀스와 reset 후 대기 시간(데이터시트의 boot time)을 지키는가.
- WHO_AM_I / chip ID 레지스터를 먼저 읽어 버스·주소를 확인했는가.
- 공장 캘리브레이션 값(OTP: 기압 보상 계수, 근접 crosstalk offset, PPG LED 전류)을 읽어 저장하는가.
- 타임스탬프를 어디서 찍는가: 인터럽트 시각, FIFO 프레임의 센서 내부 타임스탬프, 또는 MCU 수신 시각 (G7).
- 단위 변환(LSB → 물리 단위)을 golden 벡터로 검증했는가 (§6.2처럼).
- 로그 헤더에 센서 설정(ODR, gain, LED 전류, 펌웨어 버전)을 남기는가 (H3, H7).

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| Bayer 패턴 순서를 잘못 설정 | 사진 전체가 초록·자홍색으로 틀어짐 | RGGB인데 GRBG로 demosaic | 센서 데이터시트의 시작 픽셀, crop 시작 좌표의 홀짝 확인 |
| black level을 안 뺌 | 어두운 부분이 뿌옇고 색이 틀어짐 | ADC offset이 WB gain에 곱해짐 | BLC를 맨 앞에, 센서 OB(optical black) 값 사용 |
| ISP 설정을 바꾼 이미지로 기존 모델 추론 | 정확도가 이유 없이 하락 | 학습 데이터와 다른 파이프라인 (분포 이동) | 학습 데이터에 같은 변환 적용, 바꾼 파이프라인으로 fine-tune |
| PPG에서 FFT peak만 사용 | 걷는 중 HR이 cadence(예: 60)에 붙음 | 모션 주파수가 HR 대역 안 | 가속도 기준 제거(NLMS·spectral subtraction) + HR tracking |
| PPG·가속도 타임스탬프 불일치 | adaptive filter가 수렴 안 함 | 서로 다른 FIFO·클럭 | 공통 타임베이스, FIFO 타임스탬프 정렬 (G7) |
| 근접 단일 문턱 | 착용 상태가 초 단위로 깜빡임 | 노이즈가 문턱 근처 | hysteresis + debounce, crosstalk 캘리브레이션 |
| 기압으로 절대 고도 사용 | 하루 사이 고도가 수십 m 움직임 | 날씨에 의한 기압 변화 | 상대 변화만 사용, 기준 기압·GNSS로 보정 |
| 터치 중 baseline 갱신 | 길게 누르기가 저절로 풀림 | baseline이 손가락을 흡수 | 터치 중 freeze + timeout 재보정 |
| 움직임 없음 = 벗음 규칙 | 수면 중 "벗음"으로 판정 | 정지 상태의 착용을 고려 안 함 | 피부 접촉 증거(cap·temp·prox)를 우선 |
| GNSS 상시 켜기 | 배터리가 하루를 못 감 | 수십 mW급 상시 소모 | duty cycling, A-GNSS, 폰 위치 사용 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "How would you detect if a wearable is being worn with minimal power?"

**A.** 상시 감시는 가장 싼 센서 두 개로 한다: 피부 접촉 정전용량과 IMU의 wake-on-motion 인터럽트(어차피 켜져 있음). 상태 변화가 의심될 때만 IR 근접을 켜서 확인하고, 피부 온도는 분 단위로 느리게 읽는다. 점수로 합쳐서 hysteresis + debounce FSM을 걸고, PPG는 확신이 낮을 때만 몇 초 켠다. 테스트는 "무엇이 속는가" 시나리오(수면 중 정지, 창가 책상, 밴드 헐거움, 물·땀)로 하고, 지표는 하루 오판 분과 전이 횟수다.

> "I'd keep the always-on part to the cheapest signals: a capacitive skin-contact electrode and the IMU's wake-on-motion interrupt, which is already running. Only when those suggest a change would I power the IR proximity sensor to confirm, and I'd read skin temperature once a minute. I combine them into an evidence score with hysteresis and debounce, and use PPG only as a short confirmation when confidence is low. I'd test against the failure scenarios — sleeping still, lying on a sunny desk, a loose band, water — and track wrong minutes per day and transitions per day."

**Q.** "Estimate heart rate from PPG with motion artifacts."

**A.** band-pass 0.5~4 Hz 후 휴식 중에는 peak 간격이나 스펙트럼 peak로 충분하다. 움직일 때는 팔 흔들기 주파수가 HR 대역 안에 들어오므로, 가속도계를 기준 신호로 NLMS 같은 adaptive filter로 상관 성분을 빼거나, 가속도 스펙트럼 peak를 HR 후보에서 제외한다. 그리고 이전 HR 근처만 허용하는 tracking과 신호 품질 지표로 "모르겠다"를 출력할 수 있게 한다. 합성 실험에서 FFT만 쓰면 걷기 중 HR을 cadence(60)로 보고했고, NLMS 후 100으로 맞았다.

> "Band-pass to roughly 0.5 to 4 hertz; at rest, peak intervals or the spectral peak work. During motion the arm-swing frequency lands inside the heart-rate band, so I'd use the accelerometer as a reference — an NLMS adaptive filter to cancel the correlated component, or remove accelerometer spectral peaks from the candidates — then track heart rate over time with a prior around the last estimate and a signal-quality index so the device can say 'unknown' instead of reporting cadence as heart rate."

**Q.** "What does an ISP do, and can ML skip parts of it?"

**A.** ISP는 RAW Bayer를 이미지로 바꾸는 고정 기능 파이프라인이다: black level, lens shading, demosaic, white balance, color correction matrix, gamma/tone map, denoise, sharpen, 색공간 변환과 scaling, 그리고 3A 통계. 노출 제어와 black level은 정보 보존에 필요해서 건너뛸 수 없다. denoise·sharpen·tone map은 사람 눈용이라 모델에 꼭 필요하지 않을 수 있지만, 모델은 학습 때 본 파이프라인과 같아야 하므로 바꾸면 그 파이프라인으로 데이터를 다시 만들거나 fine-tune해야 한다. 실무에서는 ISP scaler의 저해상도 출력(NV12의 Y 등)을 ML 전용으로 받는다.

> "An ISP turns raw Bayer data into an image: black-level subtraction, lens shading, demosaicing, white balance, a color correction matrix, gamma and tone mapping, denoise and sharpening, color conversion and scaling, plus statistics for auto-exposure, white balance and focus. Exposure control and black level preserve information, so they stay. Denoise, sharpening and tone mapping are mostly for human eyes and can often be reduced, but the model must see the same pipeline at training and inference, so changing the ISP means regenerating data or fine-tuning. In practice I'd take a low-resolution scaler output, often just the Y plane of NV12, for the model."

**Q.** "How do you turn pressure into altitude, and what's the resolution?"

**A.** 표준 대기식 h = 44330·(1 − (P/P0)^(1/5.255))를 쓴다. 해수면 근처에서 1 m당 약 0.12 hPa(12 Pa)이므로, 압력 노이즈 1 Pa면 약 8 cm다. 필터링으로 상대 고도 수십 cm를 얻을 수 있어 층 오르기(한 층 약 0.36 hPa) 판정이 가능하다. 하지만 날씨로 하루 수 hPa가 바뀌니 절대 고도는 수십 m 틀린다. 그래서 짧은 시간의 상대 변화에 쓰고, 온도 보상은 칩의 보정 계수로 하며, 공조·문·엘리베이터 같은 가짜 변화를 고려한다.

> "Use the standard-atmosphere formula, h equals 44,330 times one minus P over P-zero to the power 1 over 5.255. Near sea level that's about 0.12 hectopascal — 12 pascals — per meter, so one pascal of noise is roughly eight centimeters. With filtering you get relative altitude to a few tens of centimeters, enough to count floors at about 0.36 hectopascal per floor. Absolute altitude drifts by tens of meters with weather, so I use it for short-term relative changes, apply the sensor's factory temperature compensation, and guard against HVAC, doors and elevators."

**Q.** "Your proximity-based wear detection toggles constantly. How do you debug and fix it?"

**A.** 먼저 raw count를 로그로 남겨 분포를 본다: 착용·미착용 count 분포가 겹치는지, crosstalk offset이 기기마다 다른지, 햇빛이나 피부색에 따라 어떻게 바뀌는지. 단일 문턱이면 hysteresis(켜기·끄기 문턱 분리)와 debounce(N회 연속)를 걸고, 공장에서 crosstalk를 캘리브레이션한다. 그래도 겹치면 정전용량·온도를 더해 퓨전한다. 합성 실험에서 단일 문턱 12번 토글이 hysteresis + debounce로 정답 2번이 됐고, 대가는 2~4초 지연이었다.

> "First I'd log raw counts and look at the distributions — whether on-body and off-body overlap, whether crosstalk offset varies per unit, and how sunlight or skin tone shifts them. Then I'd add hysteresis with separate on and off thresholds, debounce with N consecutive samples, and per-unit crosstalk calibration at the factory. If the distributions still overlap, I'd fuse in capacitance and temperature. The trade-off is latency, so I'd check it against the product requirement."

**Q.** "Why not just run everything through one ML model on all sensors?"

**A.** 할 수는 있지만 상시 전력이 문제다. 모든 센서를 켜 두는 비용(특히 PPG LED, GNSS, 카메라)이 모델 연산보다 크다. 계층으로 나눈다: 싼 센서와 규칙/작은 트리가 상시, 비싼 센서와 큰 모델은 이벤트 때만. ML은 규칙으로 맞추기 어려운 사용자 변동이 있는 곳에 넣고, 출력에는 항상 FSM 후처리를 건다.

> "You could, but the always-on cost is dominated by keeping sensors powered — PPG LEDs, GNSS, the camera — not by the model's arithmetic. I'd cascade: cheap sensors with rules or a tiny tree always on, expensive sensors and bigger models only on events. I'd bring ML in where user-to-user variation makes hand-tuned thresholds brittle, and still put a hysteresis state machine on its output."

---

## 13. 직접 해보기

1. 손계산: 640×480 NV12 프레임 한 장은 몇 바이트이고, 15 fps면 초당 몇 MB인가?
정답: 640 × 480 × 1.5 = 460,800 B, × 15 = 6,912,000 B/s ≈ 6.9 MB/s.

2. 손계산: RAW10에서 black level 64, R = 600, G = 500, B = 300인 픽셀에 WB gain (0.8, 1.0, 1.6)을 적용한 뒤의 정규화 값(÷959)은?
정답: (536·0.8, 436, 236·1.6)/959 = (428.8, 436, 377.6)/959 ≈ (0.447, 0.455, 0.394).

3. 손계산: 해수면에서 기압 노이즈가 RMS 2 Pa이면 고도 노이즈는 약 몇 cm인가? 독립 샘플 16개를 평균하면?
정답: 2 / 12 ≈ 0.17 m = 17 cm, 평균 16개면 √16 = 4배 줄어 약 4 cm (샘플이 독립이라는 가정).

4. 손계산: NTC 10 kΩ, B = 3950이 37 °C일 때의 저항은? (힌트: 1/310.15 − 1/298.15)
정답: 1/310.15 − 1/298.15 = −0.0001298, × 3950 = −0.5126, e^(−0.5126) ≈ 0.599 → 약 5.99 kΩ (§6.2 출력 5989.8 Ω과 일치).

5. 코드 과제: §3.6의 `nlms`를 쓰지 말고, 30 s 창의 PPG 스펙트럼에서 가속도 스펙트럼 peak(과 그 2배 주파수) ±3 bpm을 지운 뒤 peak를 찾는 spectral subtraction 방식으로 걷기 HR을 추정하라.
힌트: 가속도 peak는 60.1/분, 2배는 120. 두 구간을 0으로 만들면 남은 최대 peak가 100 근처여야 한다. 합성 모션이 1.0 Hz와 2.0 Hz 성분만 가지므로 잘 되지만, 실제 모션은 고조파가 더 많다.

6. 설계 과제: §8.3 C 코드에 "충전 중이면 무조건 OFF" 입력과 "PPG 확인" 단계(점수 3이 5분 넘게 지속되면 PPG를 켜서 맥박 유무로 결정)를 추가하고, 하루 PPG 켜짐 횟수를 세라.
힌트: 상태를 하나 더 두는 것(UNCERTAIN)이 깔끔하다. PPG 확인 결과는 합성으로 "착용이면 95% 맥박 검출"처럼 넣는다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| rolling shutter | 줄 단위 순차 노출 | 싸고 작지만 움직임에 skew가 생긴다 |
| global shutter | 전 픽셀 동시 노출 | 움직임 왜곡 없음, 픽셀이 크고 비쌈 |
| MIPI CSI-2 | Camera Serial Interface 2 | 카메라 → SoC 고속 직렬 링크, 패킷 기반, D-PHY/C-PHY |
| CCI | Camera Control Interface | 센서 레지스터 설정용 I2C 호환 버스 |
| Bayer (RGGB) | 색 필터 배열 | 2×2마다 R·G·G·B, 픽셀당 색 하나 |
| ISP | Image Signal Processor | RAW → RGB/YUV 고정 기능 파이프라인 |
| BLC | black level correction | 센서 offset 빼기 |
| demosaic | 색 보간 | 픽셀마다 빠진 두 색을 이웃에서 채움 |
| WB / AWB | (auto) white balance | 조명 색을 상쇄하는 채널별 gain (추정) |
| CCM | color correction matrix | 센서 색 → 표준 색공간 3×3 행렬 |
| 3A | AE·AWB·AF | 노출·화이트밸런스·초점 자동 제어 루프 |
| NV12 | YUV 4:2:0 배치 | Y 평면 + UV 교차 평면, 픽셀당 1.5 B |
| PPG | photoplethysmography | LED + PD로 혈류 변화를 빛으로 측정 |
| AC / DC (PPG) | 박동 성분 / 정적 성분 | AC/DC 비율 = perfusion index |
| TIA | transimpedance amplifier | PD 전류를 전압으로 바꾸는 증폭기 |
| NLMS | normalized LMS | 기준 신호와 상관된 성분을 지우는 adaptive filter |
| ratio-of-ratios | SpO2 개념식 | (AC/DC)_red ÷ (AC/DC)_IR, 캘리브레이션 곡선으로 변환 |
| crosstalk (근접) | 커버 내부 반사 | 물체 없을 때의 count offset |
| dToF / SPAD | direct ToF / 단일광자 다이오드 | 광자 도착 시각 히스토그램으로 거리 |
| hysteresis | 이중 문턱 | 켜기·끄기 문턱을 달리해 chattering 방지 |
| debounce | 연속 확인 | 조건이 N번 연속일 때만 상태 변경 |
| barometric formula | 기압-고도 식 | 해수면 근처 약 0.12 hPa/m |
| NTC / Beta 식 | 음의 온도계수 서미스터 | 1/T = 1/T0 + ln(R/R0)/B |
| thermal lag | 열 지연 | 온도 센서의 1차 지연, 시정수 τ |
| self / mutual capacitance | 정전용량 감지 방식 | 전극-접지 용량 vs TX-RX 전극 간 용량 |
| baseline tracking | 기준선 추적 | 느린 drift는 흡수하고 빠른 터치만 delta로 |
| UWB | ultra-wideband | 전파 비행 시간으로 정밀 거리 측정 |
| A-GNSS | assisted GNSS | 궤도 정보를 미리 받아 위치 획득을 빠르게 |

---

## 15. 요약 & 체크리스트

웨어러블의 "조연" 센서들은 모델 입력의 품질과 큰 모델을 언제 깨울지를 결정한다. 카메라는 센서 → MIPI CSI-2 → ISP → NV12로 가는 고대역폭 경로이고, 그 경로의 전력은 ISP·DRAM·NPU를 깨우는 횟수가 지배한다. ML은 ISP의 사람 눈용 단계를 줄일 수 있지만 학습과 추론의 파이프라인이 같아야 한다. PPG는 1% 크기의 AC를 큰 모션 간섭 속에서 꺼내는 문제이고, 가속도계를 기준으로 한 adaptive filter와 tracking이 핵심이다. 근접·정전용량·온도·기압은 거의 공짜인 대신 각자 속는 상황이 있어서, 착용 감지는 점수 퓨전 + hysteresis + debounce FSM으로 만들고 "무엇이 속는가" 시나리오로 테스트한다. 기압은 상대 고도(0.12 hPa/m)에, NTC는 Beta 식(또는 표)에, 터치는 baseline 추적에 쓴다. 마지막으로 센서별 데이터율과 전력은 순서가 다르다 — 마이크·카메라는 데이터를, GNSS·PPG는 전력을 먹는다.

- [ ] 해상도·포맷(RAW10, NV12, RGB888)별 프레임 바이트와 CSI-2 대역폭을 손으로 계산할 수 있다
- [ ] Bayer RGGB 한 픽셀의 BLC → demosaic → WB → gamma를 손으로 따라갈 수 있다
- [ ] ISP 단계 중 ML이 줄일 수 있는 것과 없는 것, 그리고 그 조건(파이프라인 일치)을 말할 수 있다
- [ ] PPG의 AC/DC, 파장 선택, 모션 아티팩트가 왜 band-pass로 안 지워지는지 설명할 수 있다
- [ ] 가속도계 기준 NLMS의 식을 쓰고 MCU 비용을 말할 수 있다
- [ ] 근접 count에 hysteresis + debounce를 거는 C 코드를 짤 수 있다
- [ ] 기압 → 고도 식을 쓰고 0.12 hPa/m, 한 층 0.36 hPa, 날씨 drift를 말할 수 있다
- [ ] NTC ADC code를 저항과 온도로 바꾸고 1 LSB 분해능을 계산할 수 있다
- [ ] 정전용량 baseline 추적과 freeze·timeout 규칙을 설명할 수 있다
- [ ] 여러 센서를 점수로 묶은 착용 감지 FSM을 설계하고 속는 시나리오 목록을 만들 수 있다

---

## 참고 자료

- MIPI Alliance, "Camera Serial Interface 2 (MIPI CSI-2)" 사양 소개 페이지 — mipi.org (사양 본문은 회원용)
- Ramanath, Snyder, Yoo, Drew, "Color Image Processing Pipeline", IEEE Signal Processing Magazine, 2005 — ISP 단계 개요
- Bryce E. Bayer, US Patent 3,971,065 "Color imaging array" (1976) — Bayer 필터
- John G. Webster (ed.), "Design of Pulse Oximeters", CRC Press — SpO2 ratio-of-ratios와 캘리브레이션
- Zhang, Pi, Liu, "TROIKA: A General Framework for Heart Rate Monitoring Using Wrist-Type PPG Signals During Intensive Physical Exercise", IEEE TBME, 2015 — 모션 아티팩트 하의 손목 HR (IEEE SPC 2015 데이터)
- Simon Haykin, "Adaptive Filter Theory" — LMS/NLMS
- NOAA/NASA/USAF, "U.S. Standard Atmosphere, 1976" — 기압-고도 관계
- Steinhart & Hart, "Calibration curves for thermistors", Deep-Sea Research, 1968
- 부품 데이터시트(개념 확인용): Bosch BMP390, ST VL53L1X, Vishay VCNL4040, TI AFE4404, Analog Devices MAX86141
- 이 시리즈의 관련 노트: G1, G3, G4, G5, G7, B6, B7, E8, H1, H6, N4
