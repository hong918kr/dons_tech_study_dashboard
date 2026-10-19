# Q1. 카메라 HW 인터페이스 — 이미지 센서에서 SoC 메모리까지, 비트 단위로

> **이 노트를 다 읽으면**: 이미지 센서의 rolling/global shutter, RAW Bayer, HTS/VTS·pixel rate·blanking으로 프레임 레이트와 노출 한계를 계산할 수 있다 · MIPI CSI-2 short/long 패킷, DT(data type)·VC(virtual channel)·ECC·CRC를 비트 단위로 설명하고 C로 만들어 검증할 수 있다 · D-PHY/C-PHY의 lane·HS/LP·clock 모드를 이해하고 Python 계산기로 lane rate와 `link-frequencies`를 정할 수 있다 · GMSL/FPD-Link SerDes 구조(back-channel I2C 주소 변환, PoC, frame sync, VC 합치기)와 ISP 블록을 설명하고, DT endpoint 속성이 틀렸을 때 실제 커널이 무엇을 찍는지(Oops 포함) 안다
> **왜 필요한가**: 로봇·드론·차량 카메라 bring-up 면접의 단골 질문 "센서 모드 하나를 받았을 때 lane 수와 link frequency를 어떻게 정하나", "카메라가 안 나온다 — 어디부터 보나", "카메라 4대를 15 m 케이블로 어떻게 붙이나"에 숫자로 답하기 위해
> **Don 기준 난이도**: 직렬 링크·패킷·ECC/CRC·PLL·scope 디버깅은 이미 아는 영역(PCIe/RFFE 경험) / 이미지 센서 타이밍, CSI-2 고유 규칙, SerDes 생태계, ISP, V4L2 쪽 DT 규칙은 새로 배울 부분
> **선행 노트**: I5(V4L2·media controller — §11 DT/async 그래프), D2~D4(DT 문법·phandle), D7(binding 검증), H5(I2C 서브시스템), H3(clock·regulator), G1(드라이버 종류) — 이어지는 노트: Q2(Linux 카메라 파이프라인 실무), Q3(멀티 센서 동기화), P4(회로도 → DT)

---

## 0. 큰 그림 — 이게 왜 필요한가

예를 들어 센서 키트에 카메라가 4대 붙어 있다고 하자. 각 카메라에서 SoC 메모리까지 데이터가 가는 길은 이렇다.

1. 센서의 **픽셀 어레이**가 빛을 전하로 바꾸고, 행(row) 단위로 **ADC**가 10/12비트 숫자로 바꾼다.
2. 센서 안의 **CSI-2 송신기**가 한 행을 **long packet** 하나로 포장해서 **D-PHY lane**(차동 쌍 2~4개 + 클럭 쌍 1개)으로 보낸다.
3. SoC의 **CSI-2 수신기**가 패킷 헤더를 검사(ECC)하고 payload를 풀어(CRC 검사) 픽셀 스트림으로 만든다.
4. **ISP**가 Bayer RAW를 RGB/YUV 영상으로 만들고(블랙 레벨, 디모자이크, 화이트밸런스, 노이즈 제거, 톤 매핑), **DMA**가 DRAM 버퍼에 쓴다.
5. CPU의 Linux 드라이버는 픽셀 경로에는 끼지 않는다. 대신 **I2C(카메라 업계 용어로 CCI — Camera Control Interface)**로 센서 레지스터를 써서 모드·노출·게인을 정하고, ISP 통계를 읽어 3A(자동 노출·화이트밸런스·초점) 알고리즘을 돌린다.

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="30" width="190" height="260" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="105" y="52" text-anchor="middle" font-size="14">카메라 모듈</text>
  <rect x="22" y="65" width="166" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="87" text-anchor="middle" font-size="13">픽셀 어레이 (Bayer)</text>
  <rect x="22" y="109" width="166" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="131" text-anchor="middle" font-size="13">ADC (10/12-bit)</text>
  <rect x="22" y="153" width="166" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="175" text-anchor="middle" font-size="13">타이밍 · PLL · 레지스터</text>
  <rect x="22" y="197" width="166" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="105" y="219" text-anchor="middle" font-size="13">CSI-2 TX + D-PHY</text>
  <line x1="105" y1="99" x2="105" y2="108" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="105" y1="143" x2="105" y2="152" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="105" y1="187" x2="105" y2="196" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="318" y="30" width="352" height="260" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="494" y="52" text-anchor="middle" font-size="14">SoC</text>
  <rect x="330" y="65" width="328" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="494" y="95" text-anchor="middle" font-size="13">CPU (Linux): 센서 드라이버 · 3A 알고리즘</text>
  <rect x="330" y="140" width="100" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="380" y="162" text-anchor="middle" font-size="13">I2C 컨트롤러</text>
  <rect x="330" y="197" width="100" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="380" y="222" text-anchor="middle" font-size="13">CSI-2 RX</text>
  <rect x="445" y="197" width="90" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="490" y="222" text-anchor="middle" font-size="13">ISP</text>
  <rect x="550" y="197" width="108" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="604" y="222" text-anchor="middle" font-size="13">DMA/스케일러</text>
  <rect x="550" y="250" width="108" height="32" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="604" y="271" text-anchor="middle" font-size="13">DRAM 버퍼</text>
  <line x1="188" y1="214" x2="328" y2="214" stroke="currentColor" stroke-width="3" marker-end="url(#ah1)"/>
  <text x="259" y="206" text-anchor="middle" font-size="12">CLK + D0..D3</text>
  <line x1="330" y1="157" x2="190" y2="170" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah1)"/>
  <text x="259" y="152" text-anchor="middle" font-size="12">I2C (CCI)</text>
  <line x1="380" y1="115" x2="380" y2="139" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="430" y1="217" x2="444" y2="217" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="535" y1="217" x2="549" y2="217" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="604" y1="237" x2="604" y2="249" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="490" y1="197" x2="490" y2="116" stroke="currentColor" stroke-dasharray="3 3" marker-end="url(#ah1)"/>
  <text x="497" y="160" font-size="12">3A 통계</text>
</svg>
```

그림 1 — 카메라 한 대의 데이터 경로. 굵은 화살표가 픽셀(고속 직렬, Gb/s), 점선이 제어(I2C, 400 kHz). CPU는 픽셀을 만지지 않고 레지스터와 통계만 다룬다.

Don에게 익숙한 말로 바꾸면: CSI-2는 **단방향 PCIe 비슷한 것**이다. 물리층(D-PHY)은 PCIe PHY처럼 차동 직렬 lane이고, 그 위에 헤더(ECC)·payload·CRC로 된 패킷 계층이 있다. 차이는 (1) 링크 학습(LTSSM)이 없다 — 송신기가 정해진 속도로 그냥 보낸다, (2) 재전송이 없다 — CRC가 틀리면 그 줄은 그냥 버려진다, (3) 역방향 채널이 없다 — 제어는 별도 I2C로 한다. 그래서 **양쪽의 설정(lane 수·속도·포맷)을 사람이 DT와 레지스터로 맞춰 줘야** 하고, 이게 bring-up 버그의 대부분이다.

이 노트의 실행 환경: 계산·C 예제는 Lima VM `elinux`(aarch64, `uname -r` = `7.0.0-38-generic`)에서, 커널 예제는 공용 랩의 QEMU 게스트 커널 `6.18.55-dom`(TCG 에뮬레이션 — 시간 값은 실제 HW 수치가 아니다)에서 돌렸다. 실제 카메라 HW는 없다. 그래서 "센서가 픽셀을 보내는" 부분은 계산과 패킷 생성 코드로, "드라이버가 DT를 읽는" 부분은 **진짜 imx219 드라이버를 가짜 I2C 버스에 붙여서** 확인한다.

---

## 1. 이미지 센서 기초 — FW/HW 엔지니어가 알아야 할 만큼만

### 1.1 픽셀 → 전하 → 숫자, 그리고 Bayer

센서의 픽셀 하나는 포토다이오드 + 작은 커패시터라고 보면 된다. 노출 시간 동안 빛이 들어오면 전하가 쌓이고, 읽을 때 그 전압을 ADC가 숫자로 바꾼다. 포토다이오드는 색을 모르므로 픽셀마다 R/G/B 중 한 색 필터를 얹는다. 이 배열이 **CFA(Color Filter Array)**이고, 가장 흔한 것이 2×2 반복의 **Bayer 패턴**이다(G가 두 개 — 사람 눈이 녹색에 민감해서).

```
  RGGB 패턴 (2x2 반복)       V4L2 mbus code 예
  +---+---+---+---+
  | R | G | R | G |          MEDIA_BUS_FMT_SRGGB10_1X10
  +---+---+---+---+
  | G | B | G | B |          (S=sensor, RGGB 순서, 10-bit, 버스 1회에 10비트)
  +---+---+---+---+
```

- **RAW**: CFA 그대로, 픽셀마다 한 색 값만 있는 데이터. ISP 이전 상태다.
- **bit depth**: ADC 해상도. RAW8/10/12/14/16. 10비트면 0~1023. 비트가 많을수록 어두운 부분 계조가 살지만 대역폭이 늘어난다(§4).
- **Bayer 순서**: 센서가 플립(H/V)하거나 crop 시작 좌표가 홀수가 되면 RGGB가 GRBG 등으로 바뀐다. I5 §10.4의 함정 그대로다 — 순서를 틀리면 영상이 "보라색/초록색"으로 나온다.
- **black level(pedestal)**: 빛이 0이어도 ADC 출력은 0이 아니다(암전류·오프셋). 센서는 일부러 64(10비트 기준) 같은 오프셋을 더해서 보낸다. 노이즈가 음수 쪽으로 잘리지 않게 하려는 것이다. ISP 첫 단계(BLC)가 이것을 뺀다(§7).

### 1.2 rolling shutter vs global shutter

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
  <text x="185" y="22" text-anchor="middle" font-size="14">Rolling shutter</text>
  <text x="525" y="22" text-anchor="middle" font-size="14">Global shutter</text>
  <text x="10" y="62" font-size="12">행 0</text>
  <text x="10" y="90" font-size="12">행 1</text>
  <text x="10" y="118" font-size="12">행 2</text>
  <text x="10" y="146" font-size="12">행 3</text>
  <text x="10" y="174" font-size="12">행 4</text>
  <text x="10" y="202" font-size="12">행 5</text>
  <rect x="40" y="50" width="150" height="16" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="190" y="50" width="20" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <rect x="65" y="78" width="150" height="16" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="215" y="78" width="20" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <rect x="90" y="106" width="150" height="16" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="240" y="106" width="20" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <rect x="115" y="134" width="150" height="16" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="265" y="134" width="20" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <rect x="140" y="162" width="150" height="16" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="290" y="162" width="20" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <rect x="165" y="190" width="150" height="16" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="315" y="190" width="20" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="350" y="62" font-size="12">행 0</text>
  <text x="350" y="118" font-size="12">행 2</text>
  <text x="350" y="174" font-size="12">행 4</text>
  <rect x="380" y="50" width="150" height="16" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="530" y="50" width="20" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <rect x="380" y="78" width="150" height="16" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="552" y="78" width="20" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <rect x="380" y="106" width="150" height="16" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="574" y="106" width="20" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <rect x="380" y="134" width="150" height="16" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="596" y="134" width="20" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <rect x="380" y="162" width="150" height="16" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="618" y="162" width="20" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <rect x="380" y="190" width="150" height="16" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="640" y="190" width="20" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <line x1="40" y1="222" x2="335" y2="222" stroke="currentColor"/>
  <line x1="380" y1="222" x2="665" y2="222" stroke="currentColor"/>
  <text x="185" y="238" text-anchor="middle" font-size="12">시간 (행마다 line time만큼 밀림)</text>
  <text x="525" y="238" text-anchor="middle" font-size="12">시간 (노출은 동시, 읽기만 순차)</text>
  <rect x="40" y="246" width="12" height="10" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="58" y="255" font-size="12">노출</text>
  <rect x="110" y="246" width="12" height="10" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="128" y="255" font-size="12">읽기(readout)</text>
</svg>
```

그림 2 — 왼쪽 rolling shutter는 행마다 노출 시작이 한 line time씩 늦다. 오른쪽 global shutter는 모든 행이 같은 순간에 노출되고, 픽셀 안의 저장 노드에 값을 잡아 둔 뒤 순서대로 읽는다.

- **rolling shutter**: 행 0을 노출 시작 → 한 line time 뒤 행 1 노출 시작 → … 행마다 리셋/읽기가 순차로 진행된다. 회로가 단순하고 픽셀이 작아 화질·감도가 좋고 싸다. 대부분의 CMOS 센서(IMX219, OV5640 등)가 이 방식이다. 단점은 **skew**: 위 행과 아래 행이 찍힌 시각이 readout 시간만큼 다르다. IMX219 1080p30이면 뒤 §4 계산기 출력대로 readout이 20.42 ms다 — 그 사이 로봇이 회전하면 수직선이 기울고(젤로 효과), 프로펠러는 휘어 보이고, LED 깜빡임은 띠로 찍힌다.
- **global shutter**: 모든 픽셀을 동시에 노출하고 값을 픽셀 내부 저장 노드에 잡아 둔다. skew가 없어서 **빠르게 움직이는 로봇, VIO(visual-inertial odometry), 바코드, 구조광 depth**에 쓴다. 대신 픽셀이 크고 비싸며 해상도·감도가 낮은 편이다(예: OV9281, AR0234 — 제품 사양은 기억 기반, 데이터시트로 확인 필요).
- **센서 퓨전과의 관계**: IMU와 카메라를 합칠 때 rolling shutter는 "한 프레임 = 한 시각"이라는 가정이 깨진다. 타임스탬프를 프레임 시작(FS)으로 찍는지 중간 행 기준으로 보정하는지가 Q3의 주제다.

### 1.3 노출과 게인 — 레지스터로 보면

노출 시간(integration time)은 보통 **행 단위**로 설정한다. "노출 1000 lines"면 1000 × line time이다. IMX219 드라이버(랩 소스 `drivers/media/i2c/imx219.c`)의 레지스터를 보면:

| 레지스터 | 주소 | 의미 | V4L2 컨트롤 |
|---|---|---|---|
| `IMX219_REG_EXPOSURE` | 0x015a (16-bit) | coarse integration time, 단위 = line | `V4L2_CID_EXPOSURE` (4 ~ FLL−4) |
| `IMX219_REG_ANALOG_GAIN` | 0x0157 (8-bit) | ADC 앞 아날로그 증폭, 코드 0~232 | `V4L2_CID_ANALOGUE_GAIN` |
| `IMX219_REG_DIGITAL_GAIN` | 0x0158 (16-bit) | ADC 뒤 디지털 곱셈, 0x0100 = 1.0배 | `V4L2_CID_DIGITAL_GAIN` |
| `IMX219_REG_FRM_LENGTH_A` | 0x0160 | frame length lines (FLL = VTS) | `V4L2_CID_VBLANK` (FLL − height) |
| `IMX219_REG_LINE_LENGTH_A` | 0x0162 | line length pixels (LLP = HTS) | `V4L2_CID_HBLANK` (LLP − width) |
| `IMX219_REG_MODE_SELECT` | 0x0100 | 0 = standby, 1 = streaming | (s_stream) |

- **아날로그 게인 vs 디지털 게인**: 아날로그 게인은 ADC 전에 신호를 키우므로 ADC 양자화 잡음 대비 이득이 있다. 디지털 게인은 숫자 곱셈이라 계조가 비고 잡음도 같이 커진다. AE 알고리즘은 "노출 시간 → 아날로그 게인 → 디지털 게인" 순으로 올린다(노출이 너무 길면 모션 블러).
- IMX219 아날로그 게인 코드와 배율의 관계(gain = 256 / (256 − code), code 232 ≈ 10.7배)는 기억 기반이다 — 데이터시트로 확인 필요.
- 0x0100(mode_select), 0x0000(model id), 0x0301 계열 PLL 분주기 같은 주소는 MIPI **CCS**(Camera Command Set, 옛 SMIA) 표준 레지스터 맵에서 왔다. 커널에는 표준 맵을 따르는 센서용 범용 드라이버 `drivers/media/i2c/ccs/`가 있다(`ccs-regs.h`에 `CCS_R_MODE_SELECT` = 0x0100, `CCS_R_FRAME_LENGTH_LINES` = 0x0340). IMX219는 FLL/LLP를 0x0160/0x0162에 두는 등 일부만 따른다.

**HDR**: 한 번의 노출로는 밝은 창문과 어두운 실내를 동시에 못 담는다(10비트 = 약 60 dB). 센서 쪽 HDR 방식은 크게 (1) 긴/짧은 노출을 번갈아 찍어 합치는 **staggered/DOL HDR**(Sony 용어, 노출별로 다른 VC나 DT로 보내는 경우가 많다), (2) 한 픽셀에 고/저 변환 이득을 두는 **DCG(dual conversion gain)**, (3) 큰/작은 포토다이오드를 나눈 **split pixel**(차량용 LED 깜빡임 대응)이 있다. 합친 결과는 20~24비트 선형값이고, 이를 대역폭 때문에 **PWL(piecewise linear) 압축**해 12비트로 보내기도 한다. ISP가 다시 펴서 톤 매핑한다(§7). 이 분류는 업계 일반 지식(기억 기반)이다 — 센서별 데이터시트로 확인한다.

### 1.4 프레임 타이밍 — HTS, VTS, pixel clock, blanking

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="60" y="40" width="330" height="160" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="225" y="115" text-anchor="middle" font-size="14">활성 영역 W × H</text>
  <text x="225" y="135" text-anchor="middle" font-size="12">(행마다 long packet 1개)</text>
  <rect x="390" y="40" width="150" height="160" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="465" y="115" text-anchor="middle" font-size="13">HBLANK</text>
  <text x="465" y="135" text-anchor="middle" font-size="12">(lane은 LP)</text>
  <rect x="60" y="200" width="480" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="300" y="228" text-anchor="middle" font-size="13">VBLANK (FE 뒤 ~ 다음 FS 전)</text>
  <text x="300" y="248" text-anchor="middle" font-size="12">노출을 늘릴 여유 · fps 조절 손잡이</text>
  <line x1="60" y1="25" x2="540" y2="25" stroke="currentColor" marker-start="url(#ah3)" marker-end="url(#ah3)"/>
  <text x="300" y="19" text-anchor="middle" font-size="12">HTS = LLP (line_length_pck, 단위: pixel clock)</text>
  <line x1="560" y1="40" x2="560" y2="260" stroke="currentColor" marker-start="url(#ah3)" marker-end="url(#ah3)"/>
  <text x="570" y="146" font-size="12">VTS = FLL</text>
  <text x="570" y="164" font-size="12">(단위: line)</text>
  <text x="20" y="285" font-size="13">fps = pixel_rate / (HTS × VTS) · line time = HTS / pixel_rate</text>
</svg>
```

그림 3 — 센서 내부 타이밍 프레임. 전체 사각형(HTS × VTS)이 한 프레임 시간이고, 그중 초록만 실제로 전송되는 픽셀이다.

- **pixel clock / pixel rate**: 센서 내부에서 픽셀을 읽어 내는 속도(픽셀/초). V4L2는 이것을 `V4L2_CID_PIXEL_RATE`로 노출한다.
- **HTS(horizontal total size) = LLP(line length in pixel clocks)**: 한 행에 걸리는 시간을 픽셀 클럭 수로 센 값. 활성 폭 W + HBLANK.
- **VTS(vertical total size) = FLL(frame length in lines)**: 한 프레임의 총 행 수. 활성 높이 H + VBLANK.
- **blanking**: HBLANK 동안 센서는 ADC 정리·다음 행 준비를 하고, CSI-2 lane은 보통 LP 상태로 내려간다. VBLANK는 프레임 사이 쉬는 구간이다.
- 그래서 **fps를 바꾸는 표준 손잡이는 VBLANK**다(픽셀 클럭과 PLL은 그대로, 쉬는 행만 늘린다). 노출은 한 프레임 안에 들어가야 하므로 최대 노출 ≈ VTS − 여유 lines.

#### 예제 1 — imx219.c의 PLL 표에서 pixel rate, link frequency, fps를 역산하기

무엇을 확인하나: 드라이버의 상수(`IMX219_PIXEL_RATE`, `IMX219_DEFAULT_LINK_FREQ`, 모드별 `fll_def`)가 PLL 레지스터 값과 HTS/VTS 공식으로 정확히 맞아떨어지는지, 그리고 수신기 드라이버가 그 link frequency로 무엇을 계산하는지.

```python
#!/usr/bin/env python3
"""Q1: recompute imx219.c constants from its PLL register tables + frame timing."""
EXCK = 24_000_000                      # IMX219_XCLK_FREQ
PREPLL_DIV = 3                         # 0x0304/0x0305 = 3 (24 MHz / 3 = 8 MHz)
TABLES = {  # lanes: (VTPXCK_DIV 0x0301, PLL_VT_MPY 0x0306, PLL_OP_MPY 0x030c)
    2: (5, 57, 114),
    4: (5, 88, 91),
}
DRIVER = {2: (182_400_000, 456_000_000), 4: (281_600_000, 364_000_000)}
BPP = 10
for lanes, (vtpx_div, vt_mpy, op_mpy) in TABLES.items():
    pll_in = EXCK / PREPLL_DIV
    vt_pll = pll_in * vt_mpy                 # video-timing PLL
    vt_pix = vt_pll / vtpx_div               # VT pixel clock
    pixel_rate = vt_pix * 2                  # x2: matches driver constant (2 pix/clk, check datasheet)
    op_bitrate = pll_in * op_mpy             # output PLL = bit rate per lane
    link_freq = op_bitrate / 2               # D-PHY DDR clock lane frequency
    need = pixel_rate * BPP / lanes
    drv_pr, drv_lf = DRIVER[lanes]
    print(f"{lanes}-lane: VT PLL {vt_pll/1e6:.0f} MHz -> pixel_rate {pixel_rate/1e6:.1f} M"
          f" (driver {drv_pr/1e6:.1f})")
    print(f"        OP PLL {op_bitrate/1e6:.0f} Mb/s/lane -> link_freq {link_freq/1e6:.0f} MHz"
          f" (driver {drv_lf/1e6:.0f}); needed {need/1e6:.0f} Mb/s/lane"
          f" -> margin {op_bitrate/need:.3f}")

# Frame timing: fps = pixel_rate / (LLP * FLL); HBLANK = LLP - width, VBLANK = FLL - height
LLP_MIN = 0x0d78
MODES = [(3280, 2464, 3526), (1920, 1080, 1763)]   # unbinned modes (binned: rate_factor)
pr = 182_400_000
print(f"LLP_MIN = {LLP_MIN} pixels -> line time {LLP_MIN/pr*1e6:.3f} us")
for w, h, fll in MODES:
    fps = pr / (LLP_MIN * fll)
    print(f"  {w}x{h}: FLL {fll} HBLANK {LLP_MIN-w} VBLANK {fll-h} -> {fps:6.2f} fps,"
          f" max exposure {(fll-4)*LLP_MIN/pr*1e3:6.2f} ms")
# Asking for 1080p60: what VBLANK would be needed?
fll60 = pr / (LLP_MIN * 60)
print(f"1080p60 needs FLL {fll60:.0f} but min is height+VBLANK_MIN = {1080+32}"
      f" -> max {pr/(LLP_MIN*(1080+32)):.1f} fps at 2 lanes")

# What the i.MX8MP CSIS receiver does with the link frequency (imx-mipi-csis.c)
for lf in (456_000_000, 450_000_000, 364_000_000, 800_000_000, 40_000_000):
    lane_rate = lf * 2
    if not 80_000_000 <= lane_rate <= 1_500_000_000:
        print(f"link_freq {lf/1e6:5.0f} MHz -> lane rate {lane_rate/1e6:5.0f} Mb/s: -EINVAL (out of bound)")
        continue
    print(f"link_freq {lf/1e6:5.0f} MHz -> lane rate {lane_rate/1e6:5.0f} Mb/s -> Ths_settle {(lane_rate - 5_000_000) // 45_000_000}")
```

```sh
limactl shell elinux -- python3 /Users/donh/.elinux_scratch/Q1/imx219_clk.py
```

```text
2-lane: VT PLL 456 MHz -> pixel_rate 182.4 M (driver 182.4)
        OP PLL 912 Mb/s/lane -> link_freq 456 MHz (driver 456); needed 912 Mb/s/lane -> margin 1.000
4-lane: VT PLL 704 MHz -> pixel_rate 281.6 M (driver 281.6)
        OP PLL 728 Mb/s/lane -> link_freq 364 MHz (driver 364); needed 704 Mb/s/lane -> margin 1.034
LLP_MIN = 3448 pixels -> line time 18.904 us
  3280x2464: FLL 3526 HBLANK 168 VBLANK 1062 ->  15.00 fps, max exposure  66.58 ms
  1920x1080: FLL 1763 HBLANK 1528 VBLANK 683 ->  30.01 fps, max exposure  33.25 ms
1080p60 needs FLL 882 but min is height+VBLANK_MIN = 1112 -> max 47.6 fps at 2 lanes
link_freq   456 MHz -> lane rate   912 Mb/s -> Ths_settle 20
link_freq   450 MHz -> lane rate   900 Mb/s -> Ths_settle 19
link_freq   364 MHz -> lane rate   728 Mb/s -> Ths_settle 16
link_freq   800 MHz -> lane rate  1600 Mb/s: -EINVAL (out of bound)
link_freq    40 MHz -> lane rate    80 Mb/s -> Ths_settle 1
```

출력에서 볼 것:

- 24 MHz ÷ 3 = 8 MHz가 PLL 입력이다. VT(video timing) PLL 8 × 57 = 456 MHz → ÷5 → ×2 = 182.4 Mpix/s가 드라이버 상수와 정확히 같다(×2는 상수를 맞추기 위한 것으로, "클럭당 2픽셀 처리"라는 해석은 데이터시트로 확인 필요). OP(output) PLL 8 × 114 = 912 Mb/s가 lane당 비트레이트이고, D-PHY는 클럭의 양쪽 에지에서 데이터를 싣는 **DDR**이라 클럭 lane 주파수 = 912 / 2 = **456 MHz** — 이것이 DT의 `link-frequencies`에 쓰는 값이다.
- 2-lane에서 필요량(182.4 M × 10비트 ÷ 2 = 912 Mb/s)과 공급량이 margin 1.000으로 딱 맞는다. 센서 설계자가 PLL을 "필요한 만큼 정확히" 잡은 것이다. 4-lane은 3.4% 여유.
- 1080p30의 숫자가 재미있다: 폭은 1920인데 LLP는 3448이다. IMX219의 1080p는 전체 어레이에서 가운데를 crop한 모드라 행 시간은 전체 폭 기준 그대로이고, 남는 시간이 HBLANK가 된다. 그래서 2-lane 1080p는 **최대 47.6 fps**가 한계다(VBLANK 최소 32줄). "1080p60을 달라"는 요구에는 4-lane 또는 binning 모드가 답이다.
- 마지막 다섯 줄은 i.MX8MP CSI-2 수신기 드라이버 `drivers/media/platform/nxp/imx-mipi-csis.c`의 `mipi_csis_calculate_params()` 논리를 그대로 옮긴 것이다: lane rate = link_freq × 2, 허용 범위 80~1500 Mb/s, `hs_settle = (lane_rate − 5 MHz) / 45 MHz`. 즉 **수신기는 link frequency로 D-PHY 타이밍 파라미터를 계산한다**. link frequency를 450으로 잘못 알려 주면 settle이 20 대신 19가 된다 — 정상 동작할 수도, 가끔 첫 바이트를 놓칠 수도 있는 애매한 영역이다(§8.5).

#### 흔한 함정

- **link frequency ≠ lane bit rate**: D-PHY에서 `link-frequencies`는 클럭 lane 주파수(= bit rate / 2)다. video-interfaces.yaml도 "this is the actual frequency of the bus, not bits per clock per lane value"라고 적는다. 912를 넣으면 "no matching link frequencies"다.
- **pixel rate ≠ 버스 위 픽셀 속도**: 커널 문서 `Documentation/driver-api/media/tx-rx.rst`는 버스 기준 pixel rate = `link_freq × 2 × nr_of_lanes × 16 / k / bits_per_sample`(k = D-PHY 16, C-PHY 7)이고, 이는 센서 어레이 기준 `V4L2_CID_PIXEL_RATE`와 **같지 않다**고 경고한다. 수신기가 PIXEL_RATE로 link freq를 추정하면 커널이 `Link frequency estimated using pixel rate: result might be inaccurate` 경고를 찍는다(`v4l2-common.c`의 `v4l2_get_link_freq_ctrl()`).

---

## 2. MIPI CSI-2 프로토콜

### 2.1 계층 구조

MIPI CSI-2 스펙(유료 회원 문서 — 아래 비트 배치는 스펙을 구현한 커널 코드·공개 자료 기준)은 아래에서 위로 이렇게 쌓인다.

| 계층 | 하는 일 | Don의 비유 |
|---|---|---|
| PHY (D-PHY / C-PHY) | 차동 신호, HS/LP 상태, 비트 동기 | PCIe PHY |
| Lane management | 바이트를 lane들에 라운드로빈으로 분배(lane 0에 byte0, lane 1에 byte1 …) | PCIe 링크 폭 x2/x4 스트라이핑 |
| Low Level Protocol (LLP) | 패킷 헤더(DI·WC·ECC), payload, CRC, short/long 패킷 | TLP 헤더 + LCRC |
| Pixel to byte packing | RAW10을 5바이트/4픽셀로 묶는 규칙 등 | 데이터 포맷 |
| Application | 프레임·라인 의미 | 상위 프로토콜 |

### 2.2 short packet과 long packet

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <text x="10" y="22" font-size="13">Short packet (4 bytes): FS / FE / LS / LE / generic</text>
  <rect x="10" y="32" width="90" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="55" y="55" text-anchor="middle" font-size="13">DI: VC+DT</text>
  <rect x="100" y="32" width="170" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="185" y="55" text-anchor="middle" font-size="13">Data 16-bit (frame#)</text>
  <rect x="270" y="32" width="70" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="305" y="55" text-anchor="middle" font-size="13">ECC</text>
  <text x="10" y="98" font-size="13">Long packet: 픽셀 한 줄, embedded data</text>
  <rect x="10" y="108" width="90" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="55" y="131" text-anchor="middle" font-size="13">DI</text>
  <rect x="100" y="108" width="110" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="155" y="131" text-anchor="middle" font-size="13">WC (16-bit)</text>
  <rect x="210" y="108" width="60" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="240" y="131" text-anchor="middle" font-size="13">ECC</text>
  <rect x="270" y="108" width="300" height="36" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="420" y="131" text-anchor="middle" font-size="13">Payload: WC 바이트 (RAW10 = 5B/4px)</text>
  <rect x="570" y="108" width="100" height="36" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="620" y="131" text-anchor="middle" font-size="13">CRC-16</text>
  <line x1="10" y1="152" x2="270" y2="152" stroke="currentColor"/>
  <text x="140" y="168" text-anchor="middle" font-size="12">Packet Header (32-bit)</text>
  <line x1="570" y1="152" x2="670" y2="152" stroke="currentColor"/>
  <text x="620" y="168" text-anchor="middle" font-size="12">Packet Footer</text>
  <text x="10" y="198" font-size="13">한 프레임의 패킷 순서 (VC 하나)</text>
  <rect x="10" y="208" width="44" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="32" y="228" text-anchor="middle" font-size="12">FS</text>
  <rect x="60" y="208" width="120" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="228" text-anchor="middle" font-size="12">Long: line 0</text>
  <rect x="186" y="208" width="44" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="208" y="228" text-anchor="middle" font-size="12">LP</text>
  <rect x="236" y="208" width="120" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="296" y="228" text-anchor="middle" font-size="12">Long: line 1</text>
  <text x="382" y="228" text-anchor="middle" font-size="12">. . .</text>
  <rect x="408" y="208" width="140" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="478" y="228" text-anchor="middle" font-size="12">Long: line H-1</text>
  <rect x="554" y="208" width="44" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="576" y="228" text-anchor="middle" font-size="12">FE</text>
  <text x="10" y="270" font-size="12">DI 바이트 = VC[7:6] + DT[5:0] · WC = payload 바이트 수 (LSB 먼저)</text>
  <text x="10" y="292" font-size="12">ECC = 헤더 24비트에 대한 6비트 Hamming 코드 (1비트 정정, 2비트 검출)</text>
  <text x="10" y="314" font-size="12">CRC-16 = x^16+x^12+x^5+1, 초기값 0xFFFF, payload에만 적용</text>
</svg>
```

그림 4 — CSI-2 패킷 두 종류와 한 프레임의 순서. LS/LE(line start/end) short packet은 선택 사항이라 대부분의 센서는 FS, long packet들, FE만 보낸다.

- **DI(Data Identifier)** 1바이트 = 상위 2비트 VC(virtual channel) + 하위 6비트 DT(data type).
- **short packet**: DT가 0x00~0x0F. "데이터" 16비트 자리에 FS/FE는 **frame number**, LS/LE는 line number를 넣는다. payload·CRC 없음.
- **long packet**: DT가 0x10 이상. 헤더의 16비트가 **WC(word count) = payload 바이트 수**. 뒤에 payload, 마지막에 2바이트 CRC.
- 각 패킷 사이에 lane이 LP로 내려갈 수도(그림의 "LP"), HS를 유지할 수도 있다(송신기 설정).

커널은 DT 값을 `include/media/mipi-csi2.h`에 정의해 둔다(랩 소스에서 발췌한 주요 값):

| DT | 이름 | 종류 | 메모 |
|---|---|---|---|
| 0x00 / 0x01 | FS / FE | short | frame start / end, 16비트 frame number |
| 0x02 / 0x03 | LS / LE | short | line start / end (선택) |
| 0x12 | EMBEDDED_8B | long | 센서가 레지스터 값·노출·온도 등을 실어 보내는 줄 |
| 0x1e | YUV422_8B | long | UYVY 등, 픽셀당 2바이트 |
| 0x22 / 0x24 | RGB565 / RGB888 | long | ISP 내장 센서 출력 |
| 0x2a | RAW8 | long | 픽셀당 1바이트 |
| 0x2b | RAW10 | long | 4픽셀 → 5바이트 |
| 0x2c | RAW12 | long | 2픽셀 → 3바이트 |
| 0x2d / 0x2e | RAW14 / RAW16 | long | |
| 0x30 ~ 0x37 | USER_DEFINED(n) | long | 벤더 정의 (ToF depth, 메타데이터 등) |

### 2.3 Virtual channel과 data type — 한 링크에 여러 스트림

VC는 "한 물리 링크 위의 논리 채널"이다. CSI-2 v1.x는 DI의 2비트로 VC 0~3을, v2.0부터는 ECC 바이트의 남는 상위 2비트(VCX)를 더해 VC 0~15를 지원한다(v2.0 VCX는 기억 기반 — 스펙으로 확인 필요). 쓰임새:

- **카메라 여러 대 합치기**: 역직렬화기(§6)가 카메라 0의 패킷은 VC0, 카메라 1은 VC1로 바꿔서 한 CSI-2 포트로 내보낸다. SoC 수신기는 VC별로 다른 DMA 채널(→ 다른 `/dev/videoN`)에 넣는다. 랩 소스 `ds90ub960.c`의 `ub960_get_vc_maps()`가 정확히 이 일을 한다(§6.4).
- **같은 카메라의 여러 데이터**: 픽셀(DT 0x2b)과 embedded data(DT 0x12)를 같은 VC에 DT만 다르게, 또는 HDR의 긴/짧은 노출을 VC0/VC1로.
- Linux에서는 이것을 subdev **routing/streams API**(`VIDIOC_SUBDEV_S_ROUTING`)로 표현한다 — Q2에서 다룬다.

### 2.4 ECC — 헤더 24비트를 지키는 6비트 Hamming 코드

헤더(DI + WC 16비트 = 24비트)가 깨지면 수신기는 패킷 길이를 모르게 되어 줄 전체가 망가진다. 그래서 헤더에는 **SEC-DED(1비트 정정·2비트 검출) Hamming 코드**를 붙인다. 6개 패리티 비트 P0~P5가 각각 헤더 비트의 정해진 부분집합의 XOR이다. 이 부분집합 표는 MIPI DSI(디스플레이)와 같은 것이라, 커널의 `drivers/gpu/drm/sun4i/sun6i_mipi_dsi.c`에 `sun6i_dsi_ecc_array[]`로 들어 있다. 이 표를 16진수 마스크로 바꿔서 쓴다.

#### 예제 2 — CSI-2 헤더 만들기와 1비트 오류 정정

무엇을 확인하나: FS/FE와 RAW10/RAW12/YUV422 줄의 헤더 4바이트가 실제로 어떻게 생겼는지, 그리고 ECC가 1비트 오류를 고치고 2비트 오류를 검출하는지.

```c
/* Q1: CSI-2 packet header (DI, WC, ECC) build + single-bit error correction */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Parity sets P0..P5 over header bits D0..D23 (same table as kernel sun6i_mipi_dsi.c) */
static const uint32_t P[6] = { 0xF12CB7, 0xF2555B, 0x749A6D, 0xB8E38E, 0xDF03F0, 0xEFFC00 };

static uint8_t ecc24(uint32_t d)
{
	uint8_t e = 0;
	for (int i = 0; i < 6; i++)
		e |= (uint8_t)(__builtin_parity(d & P[i]) << i);
	return e;		/* bits 7:6 = 0 in CSI-2 v1.x */
}

static uint32_t hdr(unsigned vc, unsigned dt, unsigned wc)
{
	return (vc & 3) << 6 | (dt & 0x3f) | (uint32_t)(wc & 0xffff) << 8;
}

/* returns corrected 24-bit header, or -1 if uncorrectable */
static long correct(uint32_t d, uint8_t ecc_rx)
{
	uint8_t syn = ecc24(d) ^ ecc_rx;
	if (!syn || __builtin_popcount(syn) == 1)
		return d;	/* no error, or the flipped bit was in the ECC byte itself */
	for (int j = 0; j < 24; j++) {
		uint8_t col = 0;
		for (int i = 0; i < 6; i++)
			col |= (uint8_t)(((P[i] >> j) & 1) << i);
		if (col == syn)
			return d ^ (1u << j);	/* data bit j flipped */
	}
	return -1;
}

int main(void)
{
	struct { const char *n; unsigned vc, dt, wc; } pk[] = {
		{ "FS  frame#1  ", 0, 0x00, 1 },  { "FE  frame#1  ", 0, 0x01, 1 },
		{ "RAW10 1920px ", 0, 0x2b, 1920 * 10 / 8 },
		{ "RAW12 1920px ", 1, 0x2c, 1920 * 12 / 8 },
		{ "YUV422 640px ", 2, 0x1e, 640 * 2 },
	};
	for (unsigned i = 0; i < sizeof(pk) / sizeof(pk[0]); i++) {
		uint32_t h = hdr(pk[i].vc, pk[i].dt, pk[i].wc);
		printf("%s VC%u DT=0x%02x WC=%5u -> bytes %02x %02x %02x %02x\n", pk[i].n,
		       pk[i].vc, pk[i].dt, pk[i].wc, h & 0xff, (h >> 8) & 0xff,
		       (h >> 16) & 0xff, ecc24(h));
	}
	uint32_t h = hdr(0, 0x2b, 2400);
	uint8_t e = ecc24(h);
	for (int bit = 0; bit < 24; bit += 9) {
		uint32_t bad = h ^ (1u << bit);
		printf("flip D%-2d: rx %06x -> corrected %06lx\n", bit, bad, correct(bad, e));
	}
	printf("flip D0+D1: rx %06x -> %ld (uncorrectable)\n", h ^ 3u, correct(h ^ 3u, e));
	return EXIT_SUCCESS;
}
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /tmp/Q1 && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/Q1/csi2_ecc.c -o /tmp/Q1/csi2_ecc && /tmp/Q1/csi2_ecc'
```

```text
FS  frame#1   VC0 DT=0x00 WC=    1 -> bytes 00 01 00 1a
FE  frame#1   VC0 DT=0x01 WC=    1 -> bytes 01 01 00 1d
RAW10 1920px  VC0 DT=0x2b WC= 2400 -> bytes 2b 60 09 1d
RAW12 1920px  VC1 DT=0x2c WC= 2880 -> bytes 6c 40 0b 11
YUV422 640px  VC2 DT=0x1e WC= 1280 -> bytes 9e 00 05 07
flip D0 : rx 09602a -> corrected 09602b
flip D9 : rx 09622b -> corrected 09602b
flip D18: rx 0d602b -> corrected 09602b
flip D0+D1: rx 096028 -> -1 (uncorrectable)
```

출력에서 볼 것: RAW10 1920픽셀 한 줄의 헤더는 `2b 60 09 1d` — DI 0x2b(VC0, RAW10), WC 0x0960 = 2400바이트가 **LSB 먼저**, 마지막이 ECC. VC1이 되면 DI 상위 2비트가 01이라 0x2c → 0x6c, VC2면 0x1e → 0x9e. 1비트 오류는 syndrome(받은 ECC XOR 다시 계산한 ECC)이 "그 비트의 열 패턴"과 같아서 어느 비트인지 찾아 뒤집을 수 있고, 2비트 오류는 어느 열과도 맞지 않아 -1이 된다. 수신기 하드웨어가 하는 일이 정확히 이것이고, 오류를 고치면 "ECC corrected" 카운터를, 못 고치면 "ECC error"를 올린다.

이 표가 맞는지 독립 검증: DSI의 잘 알려진 short packet `05 11 00`(DCS exit_sleep_mode)과 `05 29 00`(display_on)의 ECC는 데이터시트·패널 init 시퀀스에서 흔히 보는 `36`, `1c`다. 같은 마스크로 VM에서 계산해 본다.

```sh
limactl shell elinux -- python3 -c '
P=[0xF12CB7,0xF2555B,0x749A6D,0xB8E38E,0xDF03F0,0xEFFC00]
e=lambda d: sum((bin(d&p).count("1")&1)<<i for i,p in enumerate(P))
for b in [(0x05,0x11,0),(0x05,0x29,0)]: print("DSI short pkt %02x %02x %02x -> ECC %02x" % (b+(e(b[0]|b[1]<<8|b[2]<<16),)))'
```

```text
DSI short pkt 05 11 00 -> ECC 36
DSI short pkt 05 29 00 -> ECC 1c
```

### 2.5 CRC-16과 RAW10 packing

payload는 ECC 대신 **CRC-16**(다항식 x^16+x^12+x^5+1, 비트 LSB 먼저 = 반사 다항식 0x8408, 초기값 0xFFFF)으로 지킨다. 정정은 못 하고 검출만 한다. 커널의 `lib/crc-ccitt.c`의 `crc_ccitt(0xffff, buf, len)`과 같은 알고리즘이다(sun6i DSI 드라이버가 실제로 이렇게 부른다).

#### 예제 3 — 스펙 예제 벡터로 CRC 검증, RAW10 4픽셀 → 5바이트

무엇을 확인하나: CRC 구현이 CSI-2 스펙의 예제 payload 두 개에 대해 알려진 값(0x00F0, 0xE569 — 스펙 예제 값은 기억 기반이지만 두 값이 모두 맞으면 구현이 맞다는 강한 증거)을 내는지, RAW10 packing 규칙, 폭별 WC.

```c
/* Q1: CSI-2 long-packet CRC-16 (spec vectors) + RAW10 packing/unpacking */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CRC-16: poly x^16+x^12+x^5+1, LSB first (0x8408), init 0xFFFF, no final xor.
 * Same algorithm as the kernel's crc_ccitt(0xffff, buf, len). */
static uint16_t crc16(const uint8_t *p, size_t n)
{
	uint16_t c = 0xffff;
	while (n--) {
		c ^= *p++;
		for (int i = 0; i < 8; i++)
			c = (c & 1) ? (c >> 1) ^ 0x8408 : c >> 1;
	}
	return c;
}

/* RAW10: 4 pixels -> 5 bytes: P0[9:2] P1[9:2] P2[9:2] P3[9:2] {P3[1:0]P2[1:0]P1[1:0]P0[1:0]} */
static void pack_raw10(const uint16_t *px, uint8_t *out)
{
	out[4] = 0;
	for (int i = 0; i < 4; i++) {
		out[i] = (uint8_t)(px[i] >> 2);
		out[4] |= (uint8_t)((px[i] & 3) << (2 * i));
	}
}

static void unpack_raw10(const uint8_t *in, uint16_t *px)
{
	for (int i = 0; i < 4; i++)
		px[i] = (uint16_t)(in[i] << 2 | ((in[4] >> (2 * i)) & 3));
}

int main(void)
{
	static const uint8_t v1[] = { 0xFF, 0x00, 0x00, 0x02, 0xB9, 0xDC, 0xF3, 0x72,
		0xBB, 0xD4, 0xB8, 0x5A, 0xC8, 0x75, 0xC2, 0x7C, 0x81, 0xF8, 0x05, 0xDF,
		0xFF, 0x00, 0x00, 0x01 };
	static const uint8_t v2[] = { 0xFF, 0x00, 0x00, 0x00, 0x1E, 0xF0, 0x1E, 0xC7,
		0x4F, 0x82, 0x78, 0xC5, 0x82, 0xE0, 0x8C, 0x70, 0xD2, 0x3C, 0x78, 0xE9,
		0xFF, 0x00, 0x00, 0x01 };
	printf("CRC vector1 = 0x%04x, vector2 = 0x%04x\n", crc16(v1, sizeof v1), crc16(v2, sizeof v2));

	uint16_t px[4] = { 0x3FF, 0x000, 0x201, 0x0AA }, back[4]; uint8_t b[5];
	pack_raw10(px, b);
	unpack_raw10(b, back);
	printf("RAW10 %03x %03x %03x %03x -> %02x %02x %02x %02x %02x -> %03x %03x %03x %03x %s\n",
	       px[0], px[1], px[2], px[3], b[0], b[1], b[2], b[3], b[4],
	       back[0], back[1], back[2], back[3], memcmp(px, back, sizeof px) ? "BAD" : "ok");

	unsigned widths[] = { 640, 1920, 3280, 1918 };
	for (unsigned i = 0; i < 4; i++) {
		unsigned w = widths[i];
		printf("width %4u: RAW8 WC=%4u RAW10 WC=%4u%s RAW12 WC=%4u  pkt bytes(RAW10)=%u\n",
		       w, w, w * 10 / 8, (w % 4) ? "(!w%4)" : "      ", w * 12 / 8, 4 + w * 10 / 8 + 2);
	}
	return EXIT_SUCCESS;
}
```

```sh
limactl shell elinux -- bash -lc 'gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/Q1/csi2_crc_raw10.c -o /tmp/Q1/crc && /tmp/Q1/crc'
```

```text
CRC vector1 = 0x00f0, vector2 = 0xe569
RAW10 3ff 000 201 0aa -> ff 00 80 2a 93 -> 3ff 000 201 0aa ok
width  640: RAW8 WC= 640 RAW10 WC= 800       RAW12 WC= 960  pkt bytes(RAW10)=806
width 1920: RAW8 WC=1920 RAW10 WC=2400       RAW12 WC=2880  pkt bytes(RAW10)=2406
width 3280: RAW8 WC=3280 RAW10 WC=4100       RAW12 WC=4920  pkt bytes(RAW10)=4106
width 1918: RAW8 WC=1918 RAW10 WC=2397(!w%4) RAW12 WC=2877  pkt bytes(RAW10)=2403
```

출력에서 볼 것:

- CRC 두 벡터 모두 기대값과 일치(CRC는 LS 바이트 먼저 전송되므로 두 번째는 선로 위에서 `69 e5`로 보인다).
- RAW10은 **상위 8비트 4개 + 하위 2비트 묶음 1바이트**다. 0x201은 상위 8비트 0x80, 하위 01 → 다섯 번째 바이트 비트 5:4에 들어간다. 이 배치 덕분에 RAW10을 그냥 8비트로 보면(하위 2비트 무시) 대충 맞는 그림이 나온다 — 디버깅할 때 유용한 성질이다.
- 폭이 4의 배수가 아니면 RAW10 WC가 정수로 안 떨어진다(1918 → 2397.5). 센서 모드·crop 폭은 보통 RAW10이면 4의 배수, RAW12면 2의 배수로 맞춘다. 수신기 DMA는 여기에 추가로 행 stride 정렬(예: 16·64바이트)을 요구한다 — `bytesperline`이 `width × 10 / 8`보다 큰 이유다(I5 §3).
- Linux에서 이 packed 포맷의 V4L2 fourcc는 `V4L2_PIX_FMT_SRGGB10P`('pRAA')이고, 수신기가 16비트로 풀어서 쓰면 `V4L2_PIX_FMT_SRGGB10`('RG10')이다. 어느 쪽을 쓰는지는 SoC마다 다르다.

### 2.6 embedded data와 frame number

많은 센서가 프레임 앞(또는 뒤)에 **embedded data line**(DT 0x12)을 붙여, 그 프레임에 실제 적용된 노출·게인·frame count·온도를 실어 보낸다. 노출을 I2C로 바꾸면 **몇 프레임 뒤에 적용되는지**(보통 1~2프레임 지연)가 센서마다 다른데, embedded data를 보면 "이 프레임이 어떤 설정으로 찍혔는지" 확정할 수 있다. 3A가 진동(oscillation)하지 않으려면 이 지연을 알아야 한다. FS short packet의 frame number(16비트, 1부터 증가하고 0은 "미사용")도 드롭 프레임 검출(I5 §7)에 쓸 수 있다.

---

## 3. D-PHY vs C-PHY — 물리층

### 3.1 D-PHY lane과 HS/LP 모드

D-PHY lane 하나는 선 두 가닥(Dp/Dn)이다. 이 두 가닥이 두 가지 모드로 동작한다.

- **LP(Low Power) 모드**: 두 선을 각각 1.2 V 단일단(single-ended) CMOS 신호로 쓴다. 상태는 (Dp, Dn) 조합 LP-11, LP-10, LP-01, LP-00. 느리지만(수십 Mb/s 이하) 전력이 거의 없다. 정지 상태 = LP-11(stop state).
- **HS(High Speed) 모드**: 두 선을 약 200 mV 스윙의 차동 신호(SLVS — sub-LVDS 계열)로 쓴다. Gb/s 데이터가 여기로 간다.
- 진입 순서: **LP-11 → LP-01 → LP-00(HS-request/prepare) → HS-0(zero) → sync 시퀀스 → 데이터 → HS-trail → LP-11**. 수신기는 LP-00 이후 일정 시간(Ths-settle) 동안 선로를 무시하고, 그 뒤 sync 바이트(0xB8 — 기억 기반)를 찾아 바이트 경계를 잡는다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <text x="10" y="18" font-size="13">Data lane 한 번의 HS 버스트</text>
  <text x="120" y="38" text-anchor="middle" font-size="12">LP: 1.2V 단일단 (저속)</text>
  <text x="430" y="38" text-anchor="middle" font-size="12">HS: 200 mV 차동 (SLVS)</text>
  <rect x="20" y="46" width="80" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="60" y="69" text-anchor="middle" font-size="12">LP-11</text>
  <rect x="100" y="46" width="60" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="130" y="69" text-anchor="middle" font-size="12">LP-01</text>
  <rect x="160" y="46" width="60" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="190" y="69" text-anchor="middle" font-size="12">LP-00</text>
  <rect x="220" y="46" width="80" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="260" y="69" text-anchor="middle" font-size="12">HS-0 zero</text>
  <rect x="300" y="46" width="60" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="330" y="69" text-anchor="middle" font-size="12">Sync</text>
  <rect x="360" y="46" width="170" height="36" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="445" y="69" text-anchor="middle" font-size="12">HS 데이터 (패킷)</text>
  <rect x="530" y="46" width="60" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="560" y="69" text-anchor="middle" font-size="12">Trail</text>
  <rect x="590" y="46" width="70" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="625" y="69" text-anchor="middle" font-size="12">LP-11</text>
  <text x="190" y="102" text-anchor="middle" font-size="12">Ths-prepare</text>
  <text x="260" y="118" text-anchor="middle" font-size="12">Ths-zero</text>
  <line x1="160" y1="128" x2="360" y2="128" stroke="#d0564a"/>
  <text x="260" y="144" text-anchor="middle" font-size="12">수신기: Ths-settle 동안 무시</text>
  <text x="10" y="176" font-size="13">Clock lane — continuous</text>
  <rect x="20" y="184" width="640" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="203" text-anchor="middle" font-size="12">HS 클럭이 계속 토글 (blanking 중에도)</text>
  <text x="10" y="236" font-size="13">Clock lane — non-continuous (clock-noncontinuous)</text>
  <rect x="20" y="244" width="100" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="70" y="263" text-anchor="middle" font-size="12">LP-11</text>
  <rect x="120" y="244" width="440" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="263" text-anchor="middle" font-size="12">HS 클럭 (패킷 전송 중에만)</text>
  <rect x="560" y="244" width="100" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="610" y="263" text-anchor="middle" font-size="12">LP-11</text>
  <text x="10" y="292" font-size="12">수신기는 link-frequency로 Ths-settle을 계산한다 (imx-mipi-csis: (lane_rate − 5M) / 45M)</text>
</svg>
```

그림 5 — D-PHY data lane의 LP → HS → LP 전이와 클럭 lane의 두 모드. 타이밍 이름(Ths-prepare, Ths-zero, Ths-settle, Trail)은 D-PHY 스펙 용어이고, 구체 ns 값은 스펙·SoC 매뉴얼에서 확인한다.

Don에게 익숙한 비교: LP-11 → HS 전이는 PCIe의 electrical idle 탈출과 비슷하다. 다른 점은 링크 학습이 없어서 **수신기가 송신기의 속도를 미리 알아야** settle 시간을 맞출 수 있다는 것. 그게 DT `link-frequencies`의 존재 이유다.

Scope로 보면: bring-up 때 센서 스트림을 켜고 data lane 하나를 단일단 프로브로 보면, 1.2 V(LP-11)에서 0 V 근처로 내려간 뒤 수백 mV 근처에서 작게 흔들리는(HS) 버스트가 행마다 반복된다. 버스트 주기 = line time(IMX219 1080p30이면 18.9 µs). **lane이 LP-11에 계속 머물러 있다면 센서가 스트리밍을 시작하지 않은 것**(레지스터·전원·클럭 문제)이고, HS 버스트는 보이는데 수신기 에러가 쌓이면 PHY 설정(settle, lane 수, 극성)이나 신호 품질 문제다. 이 이분법이 카메라 bring-up 디버깅의 첫 갈림길이다.

LP-11 관련 함정: 일부 수신기는 HS 진입 전에 LP-11을 반드시 관찰해야 한다. 그런데 많은 센서가 전원 직후 LP-00이고 스트림을 한 번 멈춰야 LP-11이 된다. 커널은 이것을 위해 subdev에 `.pre_streamon()`/`.post_streamoff()` 콜백을 두었다(`Documentation/driver-api/media/tx-rx.rst`: "the `.pre_streamon()` callback is used to transition the transmitter to the LP-11 or LP-111 state").

### 3.2 클럭 lane — continuous vs non-continuous

- **continuous**: 클럭 lane이 항상 HS로 토글한다. 수신기 입장에서 쉽다(클럭이 늘 있으니 PLL/DLL이 잠겨 있다). 대신 전력과 EMI가 늘어난다.
- **non-continuous**: 데이터를 보낼 때만 클럭 lane도 HS로 올리고, blanking 중에는 LP-11로 내린다. 전력이 줄지만 수신기가 매 버스트마다 클럭 lane의 HS 진입(Tclk-settle 등)을 처리해야 한다.
- DT에서는 센서(송신기) endpoint에 `clock-noncontinuous;`(boolean)를 두면 `v4l2_fwnode`가 `V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK` 플래그를 세운다(§8.5 실행 출력의 `non-continuous clock` 줄). 센서 드라이버는 이 플래그를 보고 센서 레지스터를 설정하고, 수신기는 `get_mbus_config`로 알아낸다. **센서와 수신기 중 한쪽만 바꾸면** 증상은 "가끔 프레임이 깨진다" 또는 "첫 프레임 이후 멈춘다" 같은 애매한 형태로 나온다.

### 3.3 C-PHY — 3선 trio와 심볼

C-PHY는 클럭 lane이 없다. 선 세 가닥(A/B/C)을 한 **trio**로 묶고, 매 심볼마다 세 선이 세 전압 레벨 중 서로 다른 값을 가지며, 심볼마다 상태가 반드시 바뀌므로 수신기가 **전이에서 클럭을 복원**한다(embedded clock). 7 심볼에 16비트를 실어 **심볼당 약 2.28비트**다. 커널 문서의 pixel rate 식에서 C-PHY의 k = 7(D-PHY는 16)이 이 16/7이다. DT에서는 `bus-type = <1>`(MIPI CSI-2 C-PHY, `video-interfaces.yaml`의 enum)과 trio별 `line-orders`를 쓴다.

| 항목 | D-PHY | C-PHY |
|---|---|---|
| 기본 단위 | lane = 차동 쌍 2선 | trio = 3선 |
| 클럭 | 별도 클럭 lane (DDR) | 데이터에 내장 (심볼 전이) |
| 일반 구성 | 1 클럭 + 1/2/4 데이터 lane = 4~10선 | 1~3 trio = 3~9선 |
| 속도 단위 | Gb/s per lane | Gsps per trio × 2.28 bit |
| 대표 최대 속도 (기억 기반) | v1.2 2.5 Gb/s, v2.1 4.5 Gb/s per lane | v1.2 3.5 Gsps per trio (≈ 8 Gb/s) |
| DT | `bus-type = <4>` (기본), `data-lanes`, `clock-lanes` | `bus-type = <1>`, `data-lanes`, `line-orders` |
| 쓰는 곳 | 대부분의 센서·SoC | 고급 스마트폰 센서·SoC (Qualcomm 등 — 기억 기반) |

같은 핀 수로 C-PHY가 더 많은 대역폭을 내므로, 핀이 비싼 모바일 SoC에서 쓴다. 임베디드/로보틱스 SoC(i.MX8MP, AM62A, Jetson Orin 등)는 거의 D-PHY다(Orin의 C-PHY 지원 여부는 기억 기반 — TRM으로 확인).

---

## 4. 대역폭과 lane rate 계산 — 계산기로

### 4.1 두 가지 공식

1. **payload 하한**: `W × H × fps × bpp` — 활성 픽셀만. 송신기에 프레임 전체를 담을 버퍼가 있다면 이론적으로 이만큼이면 된다.
2. **pixel_rate 규칙(실제 쓰는 공식)**: 센서는 한 행의 활성 픽셀을 **읽어 내는 속도 그대로** 내보낸다(행 버퍼가 작다). 활성 W픽셀을 읽는 데 W / pixel_rate초가 걸리니, 그 시간 안에 W × bpp비트를 보내려면 `pixel_rate × bpp`가 필요하다. lane으로 나누면 `lane bit rate = pixel_rate × bpp / lanes`, D-PHY `link_freq = lane bit rate / 2`. ov5640 드라이버 주석이 이 식을 그대로 쓴다: `link_freq = (pixel_rate * bpp) / (2 * data_lanes)`.

여기에 패킷 오버헤드(헤더 4 + CRC 2바이트/줄 — 2400바이트 줄이면 0.25%)와 LP↔HS 전이 시간이 더해지지만, 대개 PLL 여유 안에 들어간다. SoC 수신기의 lane당 최대(예: i.MX8MP CSIS 1.5 Gb/s — 드라이버 코드 상한), 수신기의 총 처리량, ISP 처리 한계(픽셀/클럭)도 각각 따로 확인한다.

#### 예제 4 — CSI-2/SerDes 대역폭 계산기

무엇을 확인하나: 여러 센서·모드에 대해 payload와 pixel_rate 규칙에 따른 lane rate, `link-frequencies` 최솟값, rolling shutter readout 시간, C-PHY 심볼 레이트, SerDes 순방향 대역폭 여유.

```python
#!/usr/bin/env python3
"""Q1: MIPI CSI-2 lane-rate / bandwidth calculator."""
# (name, W, H, fps, bpp, HTS, VTS, lanes, phy)  HTS/VTS = total incl. blanking
CFGS = [
    ("IMX219 1080p30 RAW10 2L", 1920, 1080, 30, 10, 3448, 1763, 2, "D"),
    ("IMX219 8MP15 RAW10 2L",   3280, 2464, 15, 10, 3448, 3526, 2, "D"),
    ("OV5640 VGA30 YUV422 2L",   640,  480, 30, 16, 1600, 1000, 2, "D"),
    ("4K60 RAW12 4L",           3840, 2160, 60, 12, 4400, 2250, 4, "D"),
    ("4K60 RAW12 1x C-PHY trio",3840, 2160, 60, 12, 4400, 2250, 1, "C"),
    ("2MP30 RAW12 x4 cams -> 4L", 1920, 1280, 30, 12, 2200, 1350, 4, "D"),
]
DPHY_MAX_GBPS = 2.5     # per lane, D-PHY v1.2 (memory-based, check datasheet)
CPHY_MAX_GSPS = 3.5     # per trio, C-PHY v1.2 (memory-based)
CPHY_BITS_PER_SYMBOL = 16 / 7   # 16 bits -> 7 symbols = 2.2857
OVERHEAD = 1.0          # pixel_rate already contains blanking

def calc(name, w, h, fps, bpp, hts, vts, lanes, phy, ncam=1):
    pixel_rate = hts * vts * fps                  # pixels/s on the wire (incl. blank)
    payload = w * h * fps * bpp * ncam            # active bits/s only
    total = pixel_rate * bpp * ncam * OVERHEAD    # bits/s incl. blanking
    per_lane = total / lanes
    line_us = hts / pixel_rate * 1e6
    readout_ms = h * hts / pixel_rate * 1e3       # rolling-shutter top->bottom
    print(f"--- {name}")
    print(f"  pixel_rate {pixel_rate/1e6:8.2f} Mpix/s  line {line_us:6.2f} us  "
          f"readout {readout_ms:6.2f} ms  blank-eff {w*h/(hts*vts):.2f}")
    print(f"  payload {payload/1e9:6.3f} Gb/s (min/lane {payload/lanes/1e6:7.1f})  "
          f"pixel_rate rule {total/1e9:6.3f} Gb/s (per-lane {per_lane/1e6:7.1f} Mb/s)")
    if phy == "D":
        link_freq = per_lane / 2                  # DDR clock: 2 bits per lane per clk
        ok = per_lane <= DPHY_MAX_GBPS * 1e9
        print(f"  D-PHY link-frequencies >= {link_freq/1e6:6.1f} MHz  "
              f"({'OK' if ok else 'OVER'} vs {DPHY_MAX_GBPS} Gb/s/lane)")
    else:
        gsps = per_lane / CPHY_BITS_PER_SYMBOL
        ok = gsps <= CPHY_MAX_GSPS * 1e9
        print(f"  C-PHY symbol rate {gsps/1e9:5.2f} Gsps/trio, link_freq {gsps/2/1e6:6.1f} MHz "
              f"({'OK' if ok else 'OVER'} vs {CPHY_MAX_GSPS} Gsps)")
    return total

for c in CFGS:
    ncam = 4 if "x4" in c[0] else 1
    calc(*c, ncam=ncam)

# SerDes forward-channel budget (rates from in-tree driver math / memory)
print("--- SerDes forward-channel budget for one camera")
cam = 2200 * 1350 * 30 * 12          # pixel_rate rule, one 2MP30 RAW12 camera
for link, gbps in [("FPD-Link III ub953 sync, 26MHz refclk", 26e6 * 2 / 2 * 160 / 1e9),
                   ("FPD-Link IV ub971 sync (ub9702 math)", 47187500 * 160 / 1e9),
                   ("GMSL2 6G mode", 6.0), ("GMSL3 12G mode", 12.0)]:
    # ASSUMPTION: ~80% of raw line rate usable after coding/protocol (check datasheet)
    usable = gbps * 0.8
    print(f"  {link:40s} {gbps:5.2f} Gb/s raw  ~{usable:5.2f} usable  "
          f"cam {cam/1e9:4.2f} -> {'fits' if cam/1e9 < usable else 'NO'}")
```

```sh
limactl shell elinux -- python3 /Users/donh/.elinux_scratch/Q1/csi2_bw.py
```

```text
--- IMX219 1080p30 RAW10 2L
  pixel_rate   182.36 Mpix/s  line  18.91 us  readout  20.42 ms  blank-eff 0.34
  payload  0.622 Gb/s (min/lane   311.0)  pixel_rate rule  1.824 Gb/s (per-lane   911.8 Mb/s)
  D-PHY link-frequencies >=  455.9 MHz  (OK vs 2.5 Gb/s/lane)
--- IMX219 8MP15 RAW10 2L
  pixel_rate   182.36 Mpix/s  line  18.91 us  readout  46.59 ms  blank-eff 0.66
  payload  1.212 Gb/s (min/lane   606.1)  pixel_rate rule  1.824 Gb/s (per-lane   911.8 Mb/s)
  D-PHY link-frequencies >=  455.9 MHz  (OK vs 2.5 Gb/s/lane)
--- OV5640 VGA30 YUV422 2L
  pixel_rate    48.00 Mpix/s  line  33.33 us  readout  16.00 ms  blank-eff 0.19
  payload  0.147 Gb/s (min/lane    73.7)  pixel_rate rule  0.768 Gb/s (per-lane   384.0 Mb/s)
  D-PHY link-frequencies >=  192.0 MHz  (OK vs 2.5 Gb/s/lane)
--- 4K60 RAW12 4L
  pixel_rate   594.00 Mpix/s  line   7.41 us  readout  16.00 ms  blank-eff 0.84
  payload  5.972 Gb/s (min/lane  1493.0)  pixel_rate rule  7.128 Gb/s (per-lane  1782.0 Mb/s)
  D-PHY link-frequencies >=  891.0 MHz  (OK vs 2.5 Gb/s/lane)
--- 4K60 RAW12 1x C-PHY trio
  pixel_rate   594.00 Mpix/s  line   7.41 us  readout  16.00 ms  blank-eff 0.84
  payload  5.972 Gb/s (min/lane  5972.0)  pixel_rate rule  7.128 Gb/s (per-lane  7128.0 Mb/s)
  C-PHY symbol rate  3.12 Gsps/trio, link_freq 1559.2 MHz (OK vs 3.5 Gsps)
--- 2MP30 RAW12 x4 cams -> 4L
  pixel_rate    89.10 Mpix/s  line  24.69 us  readout  31.60 ms  blank-eff 0.83
  payload  3.539 Gb/s (min/lane   884.7)  pixel_rate rule  4.277 Gb/s (per-lane  1069.2 Mb/s)
  D-PHY link-frequencies >=  534.6 MHz  (OK vs 2.5 Gb/s/lane)
--- SerDes forward-channel budget for one camera
  FPD-Link III ub953 sync, 26MHz refclk     4.16 Gb/s raw  ~ 3.33 usable  cam 1.07 -> fits
  FPD-Link IV ub971 sync (ub9702 math)      7.55 Gb/s raw  ~ 6.04 usable  cam 1.07 -> fits
  GMSL2 6G mode                             6.00 Gb/s raw  ~ 4.80 usable  cam 1.07 -> fits
  GMSL3 12G mode                           12.00 Gb/s raw  ~ 9.60 usable  cam 1.07 -> fits
```

출력에서 볼 것:

- IMX219 두 모드 모두 lane당 911.8 Mb/s(반올림 차이로 912) → `link-frequencies` 456 MHz. 해상도가 달라도 PLL(=link)은 같고 blanking만 다르다. 1080p30은 payload만 따지면 lane당 311 Mb/s면 되지만, 센서는 행 단위로 456 MHz 속도로 몰아서 보내고 나머지 시간은 LP로 쉰다(blank-eff 0.34 = 전체 시간 중 34%만 실제 픽셀).
- OV5640 VGA30 YUV422의 HTS 1600 × VTS 1000(480 + `vblank_def` 520)은 드라이버 `ov5640_mode_data[]`의 VGA `csi2_timings` 값이고, 그 결과 pixel rate 48.00 M는 같은 모드의 `.pixel_rate = OV5640_PIXEL_RATE_48M`과 정확히 같다. link freq 192 MHz는 드라이버 메뉴 `ov5640_csi2_link_freqs[]`에 있는 값이다. 드라이버는 이 식(`link_freq = pixel_rate × bpp / (2 × lanes)`)으로 link freq를 **동적으로 계산**한다(§8.3). 나머지 두 구성(4K60의 4400 × 2250은 CTA-861 4K60 표준 타이밍, 2MP30의 2200 × 1350)은 계산용 가정값이다.
- 4K60 RAW12는 4-lane D-PHY에서 lane당 1.78 Gb/s — D-PHY v1.2 한계 안이지만, **i.MX8MP CSIS(드라이버 상한 1.5 Gb/s)에는 못 들어간다**. 센서 데이터시트만 보고 SoC를 고르면 이런 일이 생긴다. 같은 스트림을 C-PHY trio 하나로 보내면 3.12 Gsps.
- 카메라 4대(2MP30 RAW12)를 역직렬화기 하나로 합쳐 4-lane으로 내보내면 lane당 1.07 Gb/s(link 535 MHz 이상). 랩 소스의 ds90ub960 binding 예제가 `link-frequencies = <800000000>`(lane당 1.6 Gb/s)을 쓰는 이유가 이런 합산 때문이다.
- SerDes 줄의 원시 속도 중 FPD-Link III/IV는 **랩 커널 소스의 계산식**에서 나왔다: `ds90ub960.c`의 `ub960_calc_bc_clk_rate_ub960()`이 CSI-2 sync 모드 back-channel 클럭 = refclk × 2, `ds90ub953.c`의 `ub953_get_fc_rate()`가 순방향 = bc_rate / 2 × 160 → refclk 26 MHz면 4.16 Gb/s(25 MHz면 4.0). ub9702(FPD-Link IV) 경로는 bc_rate 47.1875 MHz × 160 = 7.55 Gb/s. GMSL2 3/6 Gb/s, GMSL3 12 Gb/s는 Analog Devices 공개 자료 기반 기억값이다. "80% usable"은 코딩·프로토콜 오버헤드를 대충 잡은 **가정**이다 — 실제 설계는 SerDes 데이터시트의 "max video bandwidth" 표로 한다.

```svg
<svg viewBox="0 0 680 295" xmlns="http://www.w3.org/2000/svg">
  <text x="464" y="22" text-anchor="middle" font-size="12">i.MX8MP CSIS 1500</text>
  <text x="670" y="36" text-anchor="end" font-size="12">D-PHY v1.2 2500</text>
  <line x1="464" y1="28" x2="464" y2="245" stroke="#d0564a" stroke-dasharray="5 3"/>
  <line x1="640" y1="40" x2="640" y2="245" stroke="#888" stroke-dasharray="5 3"/>
  <text x="10" y="60" font-size="12">IMX219 1080p30 2L</text>
  <rect x="200" y="46" width="54.7" height="12" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
  <rect x="200" y="60" width="160.5" height="12" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="366" y="71" font-size="12">912</text>
  <text x="10" y="100" font-size="12">IMX219 8MP15 2L</text>
  <rect x="200" y="86" width="106.7" height="12" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
  <rect x="200" y="100" width="160.5" height="12" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="366" y="111" font-size="12">912</text>
  <text x="10" y="140" font-size="12">OV5640 VGA YUV 2L</text>
  <rect x="200" y="126" width="13.0" height="12" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
  <rect x="200" y="140" width="67.6" height="12" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="274" y="151" font-size="12">384</text>
  <text x="10" y="180" font-size="12">4x 2MP30 RAW12 4L</text>
  <rect x="200" y="166" width="155.7" height="12" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
  <rect x="200" y="180" width="188.2" height="12" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="394" y="191" font-size="12">1069</text>
  <text x="10" y="220" font-size="12">4K60 RAW12 4L</text>
  <rect x="200" y="206" width="262.8" height="12" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
  <rect x="200" y="220" width="313.6" height="12" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="520" y="231" font-size="12">1782</text>
  <line x1="200" y1="245" x2="640" y2="245" stroke="currentColor"/>
  <text x="200" y="260" text-anchor="middle" font-size="12">0</text>
  <text x="288" y="260" text-anchor="middle" font-size="12">500</text>
  <text x="376" y="260" text-anchor="middle" font-size="12">1000</text>
  <text x="464" y="260" text-anchor="middle" font-size="12">1500</text>
  <text x="552" y="260" text-anchor="middle" font-size="12">2000</text>
  <text x="10" y="260" font-size="12">Mb/s per lane</text>
  <rect x="200" y="273" width="12" height="10" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
  <text x="218" y="282" font-size="12">payload만 (하한)</text>
  <rect x="360" y="273" width="12" height="10" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="378" y="282" font-size="12">pixel_rate 규칙 (실제 필요)</text>
</svg>
```

그림 6 — 예제 4 출력의 lane당 비트레이트. 빨간 점선(i.MX8MP CSI-2 수신기 드라이버 상한 1.5 Gb/s)을 넘는 4K60 RAW12는 센서와 PHY 스펙이 허용해도 이 SoC에는 못 붙는다.

### 4.2 흔한 함정

- **"payload만 계산"하고 lane 수를 정한다**: IMX219 1080p30은 payload만 보면 1-lane(622 Mb/s)으로 될 것 같지만 센서는 912 Mb/s × 2 lane으로 몰아 보낸다. 센서가 지원하는 lane 수·PLL 조합은 정해져 있다(IMX219 드라이버는 2 또는 4만 허용 — §8.5).
- **bpp를 픽셀 포맷과 헷갈린다**: YUV422는 픽셀당 16비트, RAW10은 10, RGB888은 24. 같은 센서가 YUV를 내면 RAW보다 대역폭이 1.6배다.
- **멀티 카메라 합산에서 blanking 정렬을 무시한다**: 카메라 4대의 프레임 타이밍이 동기화되어 있지 않으면 역직렬화기 버퍼가 순간 피크를 감당해야 한다. 그래서 frame sync(§6.5)가 대역폭 문제이기도 하다.

---

## 5. 병렬 DVP — 오래됐지만 아직 보이는 인터페이스

**DVP(Digital Video Port, parallel camera interface)**는 CSI-2 이전의 방식이다. 선이 많고 느리지만 단순하다.

| 신호 | 의미 |
|---|---|
| PCLK | 픽셀 클럭 — 에지마다 D[7:0] (또는 D[11:0]) 한 번 샘플 |
| HSYNC / HREF | 행 유효 구간 (line valid) |
| VSYNC | 프레임 시작 |
| D[7:0] ~ D[11:0] | 병렬 데이터 |
| XCLK / MCLK | SoC → 센서 입력 클럭 (CSI-2 센서도 이건 받는다) |

- BT.656은 HSYNC/VSYNC 선을 없애고 데이터 안에 SAV/EAV 코드로 동기를 넣은 변형이다.
- DT에서는 `bus-type = <5>`(Parallel) 또는 `<6>`(BT.656), 그리고 `bus-width`, `hsync-active`, `vsync-active`, `pclk-sample`(어느 에지에서 샘플할지)을 쓴다(`video-interfaces.yaml`). 랩 소스의 ov5640 binding 예제가 `hsync-active = <0>`, `pclk-sample = <1>`을 보여 준다. ov5640 드라이버는 probe에서 `V4L2_MBUS_PARALLEL`, `V4L2_MBUS_CSI2_DPHY`, `V4L2_MBUS_BT656` 셋만 받고 나머지는 `Unsupported bus type`으로 거절한다(`ov5640.c` 3887행 부근).
- 한계: 8비트 × PCLK(보통 ≤ 100 MHz 내외)라 1080p30 YUV 정도가 한계이고, 선 길이 수 cm, 핀 12개 이상. 그래서 지금은 저가 MCU 카메라(ESP32-CAM 같은)나 FPGA 연결, 그리고 SerDes 내부 연결(옛 FPD-Link III ub913은 병렬 입력 — ds90ub960 binding 예제의 link@1이 `ub913` + `pclk-sample`)에서 본다.
- **pclk-sample 함정**: 샘플 에지를 틀리면 데이터가 반 클럭 어긋나 비트가 섞인다. 증상은 "그림은 대충 보이는데 노이즈가 심하고 색이 이상하다". 온도·케이블에 따라 나타났다 사라지는 setup/hold 문제의 전형이다 — Don이 SPI 샘플링 에지 문제에서 본 것과 같다.

---

## 6. SerDes — 카메라를 15 m 떨어뜨리기 (GMSL, FPD-Link)

### 6.1 왜 필요한가

CSI-2 D-PHY는 원래 휴대폰 안에서 **수십 cm 이하**를 가도록 만든 인터페이스다. 차량·로봇·드론에서는 카메라가 SoC에서 수 m~15 m 떨어져 있고, 진동·EMI·커넥터가 있다. 그래서 카메라 쪽에 **serializer(직렬화기)**, SoC 쪽에 **deserializer(역직렬화기)**를 두고 그 사이를 **동축 케이블 1가닥(또는 STP)**으로 잇는다. 이 한 가닥에 세 가지가 동시에 흐른다.

1. **순방향(forward) 채널**: 카메라 → SoC 영상, 수 Gb/s.
2. **역방향(back) 채널**: SoC → 카메라 I2C·GPIO(트리거), 수~수십 Mb/s 급.
3. **PoC(Power over Coax)**: 같은 동축으로 카메라 전원(보통 9~12 V 근처 — 시스템마다 다름)을 공급. 바이어스-T(인덕터+커패시터)로 DC와 RF를 분리한다.

두 진영이 있다.

| 항목 | GMSL (Maxim → Analog Devices) | FPD-Link (TI) |
|---|---|---|
| 세대 | GMSL1 (≈ 3 Gb/s급, 기억 기반), GMSL2 (3/6 Gb/s), GMSL3 (12 Gb/s) | FPD-Link III (≈ 4 Gb/s급), FPD-Link IV (≈ 7.5 Gb/s급, 랩 코드 계산) |
| 대표 칩 (랩 커널 소스) | MAX9271(ser)/MAX9286(des, GMSL1), MAX96717(ser)/MAX96714(des, GMSL2), MAX96712(des, staging) | DS90UB913/953(ser), DS90UB960(des), UB971/UB9702(FPD-Link IV) |
| 커널 드라이버 | `drivers/media/i2c/max9271.c`, `max9286.c`, `max96714.c`, `max96717.c`, `drivers/staging/media/max96712/` | `drivers/media/i2c/ds90ub913.c`, `ds90ub953.c`, `ds90ub960.c` |
| 원격 I2C 방식 (드라이버 기준) | MAX9286: i2c-mux + 원격 장치 주소 재프로그래밍(`max9271_set_address()`), MAX96714/96717: i2c-mux | I2C ATR (address translator, `drivers/i2c/i2c-atr.c`) |
| 쓰는 곳 | NVIDIA Jetson 생태계 카메라, 다수 차량 | TI Jacinto/AM6x 생태계, 다수 차량 |

**같은 세대·같은 벤더끼리만 호환**된다(GMSL ser와 FPD-Link des는 못 섞는다. GMSL2 des가 GMSL1 모드를 지원하는 식의 하위 호환은 칩마다 다름 — 데이터시트 확인). 커넥터는 보통 FAKRA나 Mini-FAKRA다.

```svg
<svg viewBox="0 0 680 345" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="30" width="200" height="120" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="110" y="50" text-anchor="middle" font-size="13">카메라 0</text>
  <rect x="20" y="60" width="180" height="32" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="110" y="81" text-anchor="middle" font-size="12">센서 IMX219 @0x10</text>
  <rect x="20" y="104" width="180" height="32" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="110" y="125" text-anchor="middle" font-size="12">직렬화기 UB953 @0x30</text>
  <rect x="10" y="180" width="200" height="120" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="110" y="200" text-anchor="middle" font-size="13">카메라 1</text>
  <rect x="20" y="210" width="180" height="32" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="110" y="231" text-anchor="middle" font-size="12">센서 IMX219 @0x10</text>
  <rect x="20" y="254" width="180" height="32" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="110" y="275" text-anchor="middle" font-size="12">직렬화기 UB953 @0x30</text>
  <line x1="210" y1="115" x2="328" y2="115" stroke="currentColor" stroke-width="2" marker-end="url(#ah7)"/>
  <line x1="328" y1="127" x2="212" y2="127" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah7)"/>
  <text x="270" y="106" text-anchor="middle" font-size="12">동축 케이블</text>
  <line x1="210" y1="265" x2="328" y2="265" stroke="currentColor" stroke-width="2" marker-end="url(#ah7)"/>
  <line x1="328" y1="277" x2="212" y2="277" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah7)"/>
  <text x="270" y="256" text-anchor="middle" font-size="12">동축 케이블</text>
  <rect x="330" y="30" width="170" height="270" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="415" y="52" text-anchor="middle" font-size="13">역직렬화기 UB960</text>
  <text x="415" y="85" text-anchor="middle" font-size="12">RX0 · RX1 (포트 0~3)</text>
  <text x="415" y="120" text-anchor="middle" font-size="12">I2C ATR (별칭 주소)</text>
  <text x="415" y="140" text-anchor="middle" font-size="12">ser 0x30 → 0x44/0x45</text>
  <text x="415" y="160" text-anchor="middle" font-size="12">센서 0x10 → 0x50/0x51</text>
  <text x="415" y="195" text-anchor="middle" font-size="12">VC 매핑: RX0→VC0</text>
  <text x="415" y="213" text-anchor="middle" font-size="12">RX1→VC1</text>
  <text x="415" y="248" text-anchor="middle" font-size="12">FrameSync 생성</text>
  <text x="415" y="268" text-anchor="middle" font-size="12">PoC 전원 스위치</text>
  <line x1="500" y1="165" x2="558" y2="165" stroke="currentColor" stroke-width="3" marker-end="url(#ah7)"/>
  <text x="529" y="157" text-anchor="middle" font-size="12">CSI-2 4L</text>
  <line x1="558" y1="245" x2="502" y2="245" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah7)"/>
  <text x="529" y="238" text-anchor="middle" font-size="12">I2C</text>
  <rect x="560" y="60" width="110" height="220" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="615" y="82" text-anchor="middle" font-size="13">SoC</text>
  <text x="615" y="140" text-anchor="middle" font-size="12">CSI-2 RX</text>
  <text x="615" y="180" text-anchor="middle" font-size="12">VC0 → video0</text>
  <text x="615" y="200" text-anchor="middle" font-size="12">VC1 → video1</text>
  <text x="10" y="326" font-size="12">실선 → 영상 (forward, Gb/s) · 점선 ← back-channel (I2C · GPIO) · 같은 동축으로 PoC 전원</text>
</svg>
```

그림 7 — 카메라 2대 + FPD-Link III 토폴로지(아래 예제 6의 DT와 같은 구성). 두 센서가 모두 I2C 주소 0x10이지만 역직렬화기가 링크별 별칭으로 바꿔 주어 SoC에서는 서로 다른 주소로 보인다.

### 6.2 back-channel I2C와 주소 변환 — 두 카메라가 둘 다 0x10일 때

카메라 모듈 4개가 똑같은 제품이면 센서 I2C 주소도 다 같다(IMX219 = 0x10). 같은 I2C 버스에 0x10이 넷이면 충돌한다. 해결 방식이 둘이다.

**방식 1 — I2C ATR(Address Translator, TI 방식)**: 역직렬화기가 SoC 쪽 버스에서 **별칭(alias) 주소**를 받으면, 해당 링크의 원격 버스로 **원래 주소**로 바꿔 전달한다. 링크 0의 센서 0x10은 SoC에서 0x50, 링크 1의 센서 0x10은 0x51로 보인다. 별칭 풀은 DT `i2c-alias-pool`로 준다. 커널의 `i2c-atr` 프레임워크가 링크마다 가상 I2C 어댑터를 만들고, 원격 장치가 붙을 때 드라이버 콜백을 부른다. 랩 소스 `ds90ub960.c`의 콜백(발췌):

```c
static int ub960_atr_attach_addr(struct i2c_atr *atr, u32 chan_id,
				 u16 addr, u16 alias)
{
	...
	for (reg_idx = 0; reg_idx < ARRAY_SIZE(rxport->aliased_addrs); reg_idx++) {
		if (!rxport->aliased_addrs[reg_idx])
			break;
	}

	if (reg_idx == ARRAY_SIZE(rxport->aliased_addrs)) {
		dev_err(dev, "rx%u: alias pool exhausted\n", rxport->nport);
		return -EADDRNOTAVAIL;
	}

	rxport->aliased_addrs[reg_idx] = addr;

	ub960_rxport_write(priv, chan_id, UB960_RR_SLAVE_ID(reg_idx),
			   addr << 1, &ret);
	ub960_rxport_write(priv, chan_id, UB960_RR_SLAVE_ALIAS(reg_idx),
			   alias << 1, &ret);
	...
}
```

하드웨어적으로는 링크(RX 포트)마다 "SLAVE_ID[n] = 원래 주소, SLAVE_ALIAS[n] = 별칭" 레지스터 쌍 표가 있고, 드라이버는 빈 칸을 찾아 채울 뿐이다. 표가 꽉 차면 `alias pool exhausted`. 직렬화기 자신은 링크별 `i2c-alias`(DT)로 0x44, 0x45 같은 주소를 받는다.

**방식 2 — 주소 재프로그래밍 + mux(GMSL1 방식)**: MAX9286은 링크별로 I2C 포워딩을 켜고 끄는 mux(`max9286_i2c_mux_select()`가 `FWDCCEN(chan)` 비트를 씀)로 한 번에 한 링크만 열고, 그 사이 원격 직렬화기·센서의 주소를 하나씩 다른 값으로 바꾼다(카메라 모듈 드라이버 `rdacm20.c`가 `max9271_set_address()`를 부른다). MAX96714/96717(GMSL2) 드라이버도 i2c-mux를 쓴다.

**back-channel은 느리고 지연이 있다**. I2C 400 kHz 트랜잭션이 직렬화기를 거쳐 원격 버스에서 실행되고 ACK가 돌아와야 하므로, 원격 쪽 장치가 클럭 스트레칭을 오래 하거나 링크가 잠기지 않았으면 SoC 쪽에서는 NACK/타임아웃으로 보인다. ub960 드라이버가 `UB960_RR_BCC_STATUS`(back-channel 에러: SEQ_ERROR, MASTER_TO, SLAVE_TO …)와 `RX_PORT_STS1_BCC_CRC_ERROR`를 읽는 이유다. 디버깅 순서: **링크 lock 상태 → BCC 에러 레지스터 → 원격 I2C**.

### 6.3 PoC와 전원 순서

카메라 전원을 동축으로 주면, 전원 켜는 시점을 소프트웨어가 정해야 한다. MAX9286 드라이버는 PoC를 regulator 또는 `maxim,gpio-poc` 속성(역직렬화기의 GPIO로 전원 스위치 제어)으로 켜고 끈다(`max9286_poc_power_on()`). ds90ub960 드라이버의 TODO 주석에도 "VPOC … Power domain? Regulator?"가 있다 — 커널에서도 아직 정리 중인 영역이다. 실무 포인트: (1) PoC가 켜지고 원격 직렬화기가 부팅·링크 lock 되기까지 수십~수백 ms가 걸리므로 그 전에 원격 I2C를 치면 실패한다, (2) PoC 과전류(케이블 단락)를 감시하는 eFuse/전류 감지 회로가 보통 있다 — 진단 대상.

### 6.4 여러 카메라를 VC로 합치기

역직렬화기는 RX 포트 4개에서 들어온 스트림을 CSI-2 TX 포트 1~2개로 내보낸다. 이때 포트마다 다른 VC를 붙인다. 랩 소스 `ds90ub960.c`(발췌):

```c
static void ub960_get_vc_maps(struct ub960_data *priv,
			      struct v4l2_subdev_state *state, u8 *vc)
{
	u8 cur_vc[UB960_MAX_TX_NPORTS] = {};
	struct v4l2_subdev_route *route;
	u8 handled_mask = 0;

	for_each_active_route(&state->routing, route) {
		unsigned int rx, tx;

		rx = ub960_pad_to_port(priv, route->sink_pad);
		if (BIT(rx) & handled_mask)
			continue;

		tx = ub960_pad_to_port(priv, route->source_pad);

		vc[rx] = cur_vc[tx]++;
		handled_mask |= BIT(rx);
	}
}
```

활성 route 순서대로 같은 TX 포트로 가는 RX 포트에 VC 0, 1, 2 …를 배정하고, 스트리밍 설정에서 `UB960_RR_CSI_VC_MAP` 레지스터에 "이 RX 포트에서 온 VC 0~3을 모두 VC n으로" 쓴다(non-ub9702 경로). MAX9286도 링크 번호를 VC로 쓴다(주석: "set VC according to Link number"). SoC 수신기는 VC별로 다른 DMA 채널에 넣어야 하므로 **수신기가 VC를 몇 개까지 지원하는지**(예: VC 4개, 또는 VC 1개뿐인 저가 SoC)가 카메라 수의 상한이 된다.

### 6.5 frame sync 포워딩

카메라 4대의 노출 시점을 맞추지 않으면 스테레오·서라운드 뷰가 어긋나고(§1.2의 skew까지 더해진다), §4.2의 대역폭 피크 문제도 생긴다. 역직렬화기가 주기적인 FSYNC 펄스를 만들어 back-channel GPIO로 모든 직렬화기 → 센서의 트리거 핀에 보내는 방식이 흔하다. 랩 소스 `max9286.c`의 `max9286_set_fsync_period()`는 frame interval과 pixel rate로 주기를 계산해 3바이트 레지스터(0x06~0x08)에 쓰고 `MAX9286_FSYNCMODE_INT_OUT | MAX9286_FSYNCMETH_MANUAL`로 내부 생성 모드를 켠다. 스트림 시작 후에는 레지스터 0x31의 `MAX9286_FSYNC_LOCKED` 비트로 모든 링크가 동기됐는지 확인한다. ds90ub960에도 `UB960_SR_FS_CTL`, `FS_HIGH_TIME` 레지스터와 back-channel GPIO 제어(`UB960_RR_BC_GPIO_CTL`)가 있다. 센서 쪽에서는 이 트리거를 받는 "slave mode / external trigger" 설정이 필요하다 — 자세한 동기 설계와 타임스탬프는 Q3.

#### 예제 5 — 카메라 2대 + FPD-Link III 역직렬화기 DT 컴파일

무엇을 확인하나: ds90ub960 binding(랩 소스 `ti,ds90ub960.yaml` 예제)을 본떠 IMX219 두 대를 UB953 직렬화기 두 개를 거쳐 UB960 → SoC CSI-2 수신기로 잇는 DT가 dtc 그래프 검사를 통과하는지, 링크별 별칭과 원격 센서 주소가 의도대로 들어갔는지. (랩 커널 Image-dom에는 ds90ub9xx·max96714/717 드라이버가 켜져 있지 않아 — `.config`에 `# CONFIG_VIDEO_DS90UB960 is not set` — 실제 probe는 하지 않는다.)

핵심 부분(파일 `q1_deser.dts`의 역직렬화기 노드, link@1은 link@0과 같은 구조라 `q1_deser_link1.dtsi`로 분리해 `/include/` 했다):

```dts
		deser@3d {
			compatible = "ti,ds90ub960-q1";
			reg = <0x3d>;
			clocks = <&fpd_refclk>;
			clock-names = "refclk";
			i2c-alias-pool = <0x50 0x51 0x52 0x53>;

			ports {
				#address-cells = <1>;
				#size-cells = <0>;
				port@0 { reg = <0>; des_in0: endpoint { remote-endpoint = <&ser0_out>; }; };
				port@1 { reg = <1>; des_in1: endpoint { remote-endpoint = <&ser1_out>; }; };
				port@4 {
					reg = <4>;
					des_csi_out: endpoint {
						data-lanes = <1 2 3 4>;
						link-frequencies = /bits/ 64 <800000000>;
						remote-endpoint = <&csi0_in>;
					};
				};
			};

			links {
				#address-cells = <1>;
				#size-cells = <0>;
				link@0 {
					reg = <0>;
					i2c-alias = <0x44>;	/* SoC sees serializer 0 at 0x44 */
					ti,rx-mode = <3>;	/* CSI-2 sync */
					#address-cells = <1>;
					#size-cells = <0>;
					ser0: serializer@30 {
						compatible = "ti,ds90ub953-q1";
						reg = <0x30>;
						gpio-controller; #gpio-cells = <2>; #clock-cells = <0>;
						ports {
							#address-cells = <1>;
							#size-cells = <0>;
							port@0 { reg = <0>; ser0_in: endpoint { data-lanes = <1 2>; remote-endpoint = <&cam0_out>; }; };
							port@1 { reg = <1>; ser0_out: endpoint { remote-endpoint = <&des_in0>; }; };
						};
						i2c {
							#address-cells = <1>;
							#size-cells = <0>;
							sensor@10 {	/* same 0x10 on both links: ATR gives aliases */
								compatible = "sony,imx219";
								reg = <0x10>;
								clocks = <&ser0>;
								port { cam0_out: endpoint { remote-endpoint = <&ser0_in>; clock-lanes = <0>;
									data-lanes = <1 2>; link-frequencies = /bits/ 64 <456000000>; }; };
							};
						};
					};
				};
				/* link@1 is identical: q1_deser_link1.dtsi */
				/include/ "q1_deser_link1.dtsi"
			};
		};
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.elinux_scratch/Q1 && dtc -I dts -O dtb -o /tmp/Q1/q1_deser.dtb q1_deser.dts; echo rc=$?; cd /tmp/Q1; for p in /i2c@30a50000/deser@3d/links/link@0 /i2c@30a50000/deser@3d/links/link@1; do echo "$p alias=0x$(fdtget -t x q1_deser.dtb $p i2c-alias) rx-mode=$(fdtget q1_deser.dtb $p ti,rx-mode) sensor=$(fdtget q1_deser.dtb $p/serializer@30/i2c/sensor@10 compatible)@0x$(fdtget -t x q1_deser.dtb $p/serializer@30/i2c/sensor@10 reg)"; done; echo pool: $(fdtget -t x q1_deser.dtb /i2c@30a50000/deser@3d i2c-alias-pool); echo endpoints: $(dtc -I dtb -O dts q1_deser.dtb 2>/dev/null | grep -c "endpoint {")'
```

```text
rc=0
/i2c@30a50000/deser@3d/links/link@0 alias=0x44 rx-mode=3 sensor=sony,imx219@0x10
/i2c@30a50000/deser@3d/links/link@1 alias=0x45 rx-mode=3 sensor=sony,imx219@0x10
pool: 50 51 52 53
endpoints: 10
```

출력에서 볼 것: dtc가 경고 없이 통과했다(그래프 양방향성 검사 포함 — 다음 절에서 깨뜨려 본다). 두 링크 모두 센서가 `@0x10`인데 DT상 충돌이 없는 이유는 **서로 다른 노드 아래(링크별 가상 I2C 버스)**에 있기 때문이다. 커널에서는 i2c-atr가 링크마다 어댑터를 만들고, 센서가 붙을 때 풀에서 0x50, 0x51을 하나씩 꺼내 `ub960_atr_attach_addr()`로 레지스터에 쓴다. endpoint 10개 = 센서 2 + 직렬화기 4 + 역직렬화기 3 + SoC 수신기 1. 그래프가 "센서 → ser → des → SoC" 3홉이고, Linux에서는 이것이 subdev 4종(센서·ser·des·수신기)의 media 그래프가 된다(Q2). 이 DT는 dt-schema(`dt-validate`)로는 검증하지 않았다(VM에 미설치) — D7의 방법으로 binding 검사를 하면 `ub953`의 필수 속성 누락 등을 더 잡을 수 있다.

---

## 7. ISP — RAW를 사람이 보는 영상으로

### 7.1 블록들

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="40" font-size="12">RAW (Bayer) 도메인</text>
  <rect x="10" y="50" width="120" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="70" y="75" text-anchor="middle" font-size="12">BLC 블랙레벨</text>
  <rect x="140" y="50" width="120" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="200" y="75" text-anchor="middle" font-size="12">LSC 렌즈음영</text>
  <rect x="270" y="50" width="120" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="330" y="75" text-anchor="middle" font-size="12">DPC 결함픽셀</text>
  <rect x="400" y="50" width="120" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="460" y="75" text-anchor="middle" font-size="12">AWB 게인</text>
  <rect x="530" y="50" width="120" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="590" y="75" text-anchor="middle" font-size="12">Demosaic</text>
  <line x1="130" y1="70" x2="139" y2="70" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="260" y1="70" x2="269" y2="70" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="390" y1="70" x2="399" y2="70" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="520" y1="70" x2="529" y2="70" stroke="currentColor" marker-end="url(#ah8)"/>
  <polyline points="590,90 590,112 70,112 70,129" fill="none" stroke="currentColor" marker-end="url(#ah8)"/>
  <rect x="10" y="130" width="120" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="70" y="155" text-anchor="middle" font-size="12">CCM 색보정</text>
  <rect x="140" y="130" width="120" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="155" text-anchor="middle" font-size="12">NR 노이즈제거</text>
  <rect x="270" y="130" width="120" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="330" y="155" text-anchor="middle" font-size="12">Tone map (HDR)</text>
  <rect x="400" y="130" width="120" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="460" y="155" text-anchor="middle" font-size="12">Gamma</text>
  <rect x="530" y="130" width="120" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="590" y="155" text-anchor="middle" font-size="12">YUV · 스케일</text>
  <line x1="130" y1="150" x2="139" y2="150" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="260" y1="150" x2="269" y2="150" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="390" y1="150" x2="399" y2="150" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="520" y1="150" x2="529" y2="150" stroke="currentColor" marker-end="url(#ah8)"/>
  <text x="400" y="190" font-size="12">RGB / YUV 도메인</text>
  <rect x="10" y="210" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="235" text-anchor="middle" font-size="12">통계: 히스토그램 · AWB 격자 · AF 선명도</text>
  <rect x="340" y="210" width="330" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="505" y="235" text-anchor="middle" font-size="12">3A 알고리즘 (CPU, libcamera IPA)</text>
  <line x1="70" y1="170" x2="70" y2="209" stroke="currentColor" stroke-dasharray="3 3" marker-end="url(#ah8)"/>
  <line x1="310" y1="230" x2="339" y2="230" stroke="currentColor" marker-end="url(#ah8)"/>
  <text x="10" y="280" font-size="12">3A 결과 → 센서 노출 · 게인 (I2C) + ISP의 AWB 게인 · CCM · 톤 커브 레지스터</text>
</svg>
```

그림 8 — 전형적인 ISP 파이프라인. 실제 순서와 블록 이름은 ISP마다 다르다(예: NR을 RAW 도메인에서 먼저 하는 ISP도 많다). 통계는 보통 RAW 쪽 여러 지점에서 뽑는다.

| 블록 | 하는 일 | 틀리면 |
|---|---|---|
| BLC (black level correction) | 센서 pedestal(예: 64) 빼기 | 어두운 부분이 뿌옇고 색이 밝기에 따라 다르게 틀어짐 (예제 6) |
| LSC (lens shading correction) | 렌즈 가장자리가 어두워지는 비네팅·색 얼룩 보정(위치별 게인 격자) | 모서리가 어둡거나 분홍/초록 |
| DPC (defective pixel correction) | 죽은/튀는 픽셀을 이웃값으로 대체 | 하얀 점·검은 점 |
| AWB 게인 | 조명 색에 맞춰 R/B 채널 게인 | 전체가 노랗거나 파랗게 |
| Demosaic (debayer) | 픽셀마다 없는 두 색을 이웃에서 보간 | 경계에 지퍼·무지개 무늬 |
| CCM (color correction matrix) | 센서 RGB → 표준 sRGB 3×3 행렬 | 채도·색상이 틀림 |
| NR (noise reduction) | 시공간 잡음 제거 | 뭉개짐 vs 지글거림 |
| Tone map / Gamma | 넓은 다이내믹 레인지를 8비트로 압축, 감마 곡선 | HDR 장면에서 하늘이 하얗게 날아감 |
| CSC / Scaler | RGB → YUV, 크기 변환 | |

**3A**: AE(auto exposure — 히스토그램을 보고 노출·게인 결정), AWB(auto white balance — 회색 가정 등으로 R/B 게인 결정), AF(auto focus — 선명도 통계로 렌즈 모터 위치). ISP 하드웨어는 **통계만 만들고**, 알고리즘은 CPU 소프트웨어가 돌린다. Linux에서는 ISP 드라이버가 통계를 별도 video 노드(메타데이터 버퍼)로 내보내고, libcamera의 IPA(Image Processing Algorithm) 모듈이 읽어서 센서 컨트롤(I2C)과 ISP 파라미터 버퍼를 다시 쓴다 — Q2.

### 7.2 ISP는 어디서 돌아가나

| 위치 | 예 | 특징 |
|---|---|---|
| 센서 내장 ISP (SoC ISP 없이 YUV/JPEG 출력) | OV5640 (YUV422·RGB565·JPEG 출력, 드라이버 포맷 표에 `OV5640_FMT_MUX_YUV422` 등) | SoC가 단순해도 됨. 화질·튜닝 자유도 낮음, 대역폭 큼(YUV 16bpp) |
| 별도 ISP 칩 / 카메라 모듈 내 ISP | 차량용 모듈의 컴패니언 ISP (기억 기반) | SerDes 앞에서 YUV로 만들어 보냄 |
| SoC 하드웨어 ISP | i.MX8MP ISP(커널은 RKISP1 계열 드라이버, I5 §10.3), Rockchip RKISP1, Jetson의 ISP, TI AM62A의 VPAC (기억 기반) | 저전력·고성능, 튜닝 파일(센서별) 필요, 벤더 툴 의존 |
| 소프트웨어 / GPU ISP | libcamera software ISP(CPU/GPU), OpenCV debayer | ISP 없는 SoC(i.MX8MP의 ISI만 쓰는 경로)에서. 해상도·fps 한계 |

로보틱스 관점: 머신 비전·VIO 알고리즘은 **RAW나 최소 처리 영상**을 선호하는 경우가 있다(톤 매핑·샤프닝이 특징점을 왜곡하므로). 반면 사람이 보는 원격 조종 영상은 ISP 튜닝이 품질을 좌우한다. 같은 카메라에서 두 경로를 동시에 내야 해서 ISP의 다중 출력(main/self path)이 중요해진다.

#### 예제 6 — 장난감 ISP: BLC → AWB → demosaic → gamma, 그리고 톤 매핑

무엇을 확인하나: 8×8 RGGB RAW10 합성 프레임(회색 패치 두 개, 텅스텐 조명처럼 붉은 빛, black level 64)에 ISP 단계를 순서대로 적용하면 각 단계에서 숫자가 어떻게 바뀌는지, BLC를 빼먹으면 왜 AWB로 못 고치는지, 톤 매핑이 HDR 장면에서 무엇을 하는지.

```python
#!/usr/bin/env python3
"""Q1: toy ISP on a synthetic RGGB RAW10 frame: BLC -> AWB -> demosaic -> gamma, + tone map."""
import random
random.seed(1)
W, H, BL, MAXV = 8, 8, 64, 1023                       # RAW10, black level 64
ILLUM = {"R": 1.6, "G": 1.0, "B": 0.6}                 # warm light (tungsten-like)
def cfa(x, y): return "RGGB"[(y % 2) * 2 + (x % 2)]    # R G / G B
def scene(x, y): return 0.25 if x < W // 2 else 0.5    # two grey patches

raw = [[min(MAXV, int(BL + 600 * scene(x, y) * ILLUM[cfa(x, y)] + random.gauss(0, 4)))
        for x in range(W)] for y in range(H)]

def means(img, sub=0):
    acc = {"R": [], "G": [], "B": []}
    for y in range(H):
        for x in range(W):
            acc[cfa(x, y)].append(img[y][x] - sub)
    return {k: sum(v) / len(v) for k, v in acc.items()}

print("raw  CFA means       ", {k: round(v, 1) for k, v in means(raw).items()})
blc = [[max(0, v - BL) for v in row] for row in raw]   # 1) black level correction
m = means(blc)
print("after BLC            ", {k: round(v, 1) for k, v in m.items()})
gain = {"R": m["G"] / m["R"], "G": 1.0, "B": m["G"] / m["B"]}   # 2) grey-world AWB
print("AWB gains            ", {k: round(v, 3) for k, v in gain.items()})
mr = means(raw)   # what if BLC is skipped? grey-world gains on raw (pedestal included)
g0 = {k: mr["G"] / mr[k] for k in "RGB"}
for s in (0.25, 0.5):
    v = {k: (600 * s * ILLUM[k] + BL) * g0[k] for k in "RGB"}
    print(f"no-BLC patch {s}: R/G {v['R']/v['G']:.3f}  B/G {v['B']/v['G']:.3f}")
wb = [[blc[y][x] * gain[cfa(x, y)] for x in range(W)] for y in range(H)]

def demosaic(img):                                     # 3) bilinear: average same-colour neighbours
    out = []
    for y in range(H):
        row = []
        for x in range(W):
            px = {}
            for c in "RGB":
                vals = [img[j][i] for j in range(y - 1, y + 2) for i in range(x - 1, x + 2)
                        if 0 <= i < W and 0 <= j < H and cfa(i, j) == c]
                px[c] = sum(vals) / len(vals)
            row.append(px)
        out.append(row)
    return out

rgb = demosaic(wb)
def gamma(v): return round(255 * (min(v, MAXV - BL) / (MAXV - BL)) ** (1 / 2.2))   # 4) gamma
for x in (1, 6):
    p = rgb[3][x]
    print(f"pixel(3,{x}) linear RGB", tuple(round(p[c]) for c in "RGB"),
          "-> 8-bit sRGB-ish", tuple(gamma(p[c]) for c in "RGB"))
# 5) global tone mapping of an HDR scene (scene-linear, 1.0 = mid-grey white)
for L in (0.01, 0.1, 1.0, 10.0, 100.0):
    clip = min(L, 1.0)
    rein = L / (1 + L)
    print(f"HDR L={L:6.2f}: clip-8bit {round(255*clip**(1/2.2)):3d}  "
          f"Reinhard-8bit {round(255*rein**(1/2.2)):3d}")
```

```sh
limactl shell elinux -- python3 /Users/donh/.elinux_scratch/Q1/mini_isp.py
```

```text
raw  CFA means        {'R': 424.1, 'G': 288.8, 'B': 198.4}
after BLC             {'R': 360.1, 'G': 224.8, 'B': 134.4}
AWB gains             {'R': 0.624, 'G': 1.0, 'B': 1.673}
no-BLC patch 0.25: R/G 0.967  B/G 1.048
no-BLC patch 0.5: R/G 1.018  B/G 0.976
pixel(3,1) linear RGB (150, 154, 157) -> 8-bit sRGB-ish (110, 111, 112)
pixel(3,6) linear RGB (300, 298, 304) -> 8-bit sRGB-ish (150, 150, 151)
HDR L=  0.01: clip-8bit  31  Reinhard-8bit  31
HDR L=  0.10: clip-8bit  90  Reinhard-8bit  86
HDR L=  1.00: clip-8bit 255  Reinhard-8bit 186
HDR L= 10.00: clip-8bit 255  Reinhard-8bit 244
HDR L=100.00: clip-8bit 255  Reinhard-8bit 254
```

출력에서 볼 것:

- BLC 후 R:G:B ≈ 360:225:134 ≈ 1.6:1:0.6 — 조명 색이 그대로 보인다. gray-world AWB 게인(R 0.624, B 1.673)은 정확히 1/1.6, 1/0.6이다. 적용 후 두 패치가 (150,154,157), (300,298,304)로 거의 회색이 됐다(잡음 때문에 ±2% 차이).
- **BLC를 빼먹으면**: 어두운 패치는 R/G 0.967·B/G 1.048(푸르스름), 밝은 패치는 1.018·0.976(불그스름) — **밝기마다 색이 다르게 틀어져서 하나의 AWB 게인으로는 고칠 수 없다**. pedestal은 곱셈이 아니라 덧셈 오차라서다. 실무에서 "어두운 데서만 색이 이상하다"면 BLC(센서 black level 레지스터와 ISP BLC 값의 불일치)를 먼저 의심한다.
- 톤 매핑: 단순 클리핑은 L ≥ 1을 전부 255로 날린다(하늘·헤드라이트가 하얀 덩어리). Reinhard `L/(1+L)` 같은 곡선은 밝은 영역을 압축해 L = 10과 100을 244와 254로 구분해 남긴다. 실제 ISP는 지역(local) 톤 매핑을 써서 어두운 영역 대비도 살린다.
- demosaic는 같은 색 이웃 평균(bilinear)이라 경계에서 색 번짐이 생기는 가장 단순한 형태다. 실제 ISP는 경계 방향을 보고 보간한다.

---

## 8. Linux 쪽 (짧게) — DT endpoint와 v4l2_fwnode

깊은 파이프라인(subdev·media-ctl·libcamera)은 Q2, 그래프 객체 모델과 async 바인딩은 I5 §11이 다룬다. 여기서는 **HW 파라미터가 DT로 어떻게 전달되고, 틀리면 무엇이 찍히는지**만 본다.

### 8.1 endpoint 속성 — binding 원문 확인

랩 소스 `Documentation/devicetree/bindings/media/video-interfaces.yaml`(6.18.55)에서 확인한 내용:

| 속성 | 타입 (yaml) | 의미 | 메모 |
|---|---|---|---|
| `remote-endpoint` | phandle | 상대 endpoint | 양쪽이 서로 가리켜야 함 (dtc `graph_endpoint` 검사) |
| `data-lanes` | uint32-array, 1~8개 | 위치 = 논리 lane, 값 = 물리 lane | "2-lane이면 `<1 2>`, clock이 물리 lane 0일 때". lane 재배치를 지원 안 하는 HW는 단조 증가 값 |
| `clock-lanes` | uint32, 최대 8 | 클럭 lane의 물리 위치 | D-PHY만 |
| `clock-noncontinuous` | boolean | non-continuous 클럭 허용 | §3.2 |
| `link-frequencies` | **uint64-array** | 허용되는 버스 주파수 목록 | "actual frequency of the bus, not bits per clock per lane" — `/bits/ 64` 필수 |
| `lane-polarities` | uint32-array, 0/1 | lane별 P/N 반전 (clock 먼저, 그다음 data-lanes 순서) | PCB에서 P/N을 바꿔 배선했을 때 |
| `line-orders` | uint32-array | C-PHY trio의 선 순서 (ABC 등 0~5) | C-PHY만 |
| `bus-type` | uint32 enum | 1 C-PHY, 2 CSI1, 3 CCP2, 4 CSI-2 D-PHY, 5 Parallel, 6 BT.656, 7 DPI | 생략하면 드라이버가 기대값으로 해석 |

### 8.2 v4l2_fwnode가 파싱하는 방법

센서 드라이버는 `v4l2_fwnode_endpoint_alloc_parse()`를 부른다(I5 §11.3에 imx219 `check_hwcfg` 발췌). 내부의 `v4l2_fwnode_endpoint_parse_csi2_bus()`(`drivers/media/v4l2-core/v4l2-fwnode.c`)가 하는 일:

1. 호출자가 `bus_type = V4L2_MBUS_CSI2_DPHY`로 기본값을 줬으면 "기본 lane 매핑"으로 시작.
2. `data-lanes` 개수와 값을 읽는다(최대 `V4L2_MBUS_CSI2_MAX_DATA_LANES`). 있으면 기본 매핑을 끈다. 같은 lane 번호가 두 번 나오면 `duplicated lane %u in data-lanes, using defaults` 경고 후 기본값으로.
3. `lane-polarities` 개수가 1 + data lane 수가 아니면 **-EINVAL**.
4. `clock-lanes`를 읽고, data lane과 겹치면 경고.
5. `clock-noncontinuous`가 있으면 `V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK`.
6. 결과를 `struct v4l2_mbus_config_mipi_csi2`(`num_data_lanes`, `data_lanes[]`, `clock_lane`, `flags`)에 채운다.
7. `alloc_parse`는 추가로 `fwnode_property_count_u64("link-frequencies")`가 양수일 때만 배열을 kmalloc해서 `link_frequencies`/`nr_of_link_frequencies`에 넣는다.

그다음 센서는 `v4l2_link_freq_to_bitmap()`(`v4l2-common.c`)으로 "DT가 허용한 주파수 ∩ 드라이버가 아는 주파수"를 비트맵으로 만든다. 교집합이 비면 `no matching link frequencies found`를 찍고 DT 값과 드라이버 값을 dev_dbg로 나열한 뒤 -ENOENT, DT에 아예 없으면 `no link frequencies in firmware`로 -ENODATA.

수신기 쪽은 스트림 시작 시 `v4l2_get_link_freq(pad, mul, div)`로 센서에게 묻는다: 먼저 `get_mbus_config`의 `link_freq`, 없으면 `V4L2_CID_LINK_FREQ` 컨트롤의 현재 메뉴 값, 그것도 없으면 PIXEL_RATE × mul / div로 추정(경고). i.MX8MP CSIS는 이 값으로 §1.4의 Ths_settle을 계산한다.

### 8.3 두 센서 드라이버의 link-freq 철학 비교

| | imx219 | ov5640 |
|---|---|---|
| PLL 설정 | lane 수별 **고정 레지스터 표** (`imx219_2lane_regs[]`, `imx219_4lane_regs[]`) | 원하는 클럭에서 **분주비를 계산** (`ov5640_calc_sys_clk()`, `ov5640_set_mipi_pclk()`) |
| link freq | 고정: 2-lane 456 MHz, 4-lane 364 MHz | 모드·포맷마다 동적: `link_freq = pixel_rate × bpp / (2 × lanes)`, 1 GHz 넘으면 pixel rate를 낮춰 재시도 |
| DT `link-frequencies` | **검사함** — 표에 없으면 probe 실패 | 드라이버가 DT 값을 검사하지 않음(소스에 `nr_of_link_frequencies` 참조 없음 — grep 확인) |
| 메뉴 컨트롤 | `V4L2_CID_LINK_FREQ` 읽기 전용 1항목 | `ov5640_csi2_link_freqs[]` 23항목 메뉴 |
| 역사적 흔적 | 4-lane에서 363 MHz를 예전에 잘못 광고 → 지금은 경고 후 364 사용 | `OV5640_LINK_RATE_MAX` 490 MHz 넘으면 MIPI 분주 1 |

mainline 정책은 imx219 쪽이다: **보드 설계자가 EMC 검증한 주파수만 DT에 적고, 드라이버는 그 안에서만 고른다**. 링크 주파수는 차량·로봇에서 무선(GNSS·셀룰러) 간섭 대역을 피하도록 고르는 값이기도 하다. ov5640처럼 마음대로 바꾸면 EMI 인증이 무의미해진다.

### 8.4 예제 7 — DT를 dtc로 검사: 센서 → CSI-2 수신기, 그리고 깨진 그래프

무엇을 확인하나: 센서 → CSI-2 수신기 → ISI 3단 그래프 DT가 통과하는지, `link-frequencies`가 64비트로 들어갔는지, 그리고 한쪽 `remote-endpoint`를 틀리고 port의 `reg`를 빼먹었을 때 dtc가 무엇을 잡는지.

핵심 부분(`q1_cam.dts`, 클럭·ISI 노드 생략):

```dts
	i2c@30a50000 {
		compatible = "fsl,imx8mp-i2c", "fsl,imx21-i2c";
		reg = <0x0 0x30a50000 0x0 0x10000>;
		#address-cells = <1>;
		#size-cells = <0>;

		sensor@10 {
			compatible = "sony,imx219";
			reg = <0x10>;
			clocks = <&cam24m>;

			port {
				imx219_out: endpoint {
					remote-endpoint = <&csi0_in>;
					clock-lanes = <0>;
					data-lanes = <1 2>;
					link-frequencies = /bits/ 64 <456000000>;
				};
			};
		};
	};

	csi@32e40000 {
		compatible = "fsl,imx8mp-mipi-csi2";
		reg = <0x0 0x32e40000 0x0 0x10000>;

		ports {
			#address-cells = <1>;
			#size-cells = <0>;

			port@0 {
				reg = <0>;
				csi0_in: endpoint {
					remote-endpoint = <&imx219_out>;
					data-lanes = <1 2>;
				};
			};

			port@1 {
				reg = <1>;
				csi0_out: endpoint {
					remote-endpoint = <&isi_in>;
				};
			};
		};
	};
```

깨진 버전 `q1_cam_bad.dts`는 두 군데를 바꿨다: `csi0_in`의 `remote-endpoint`를 `&isi_in`으로, `port@1`의 `reg = <1>;` 삭제.

```sh
limactl shell elinux -- bash -lc 'cd /tmp/Q1 && dtc --version && dtc -I dts -O dtb -o q1_cam.dtb /Users/donh/.elinux_scratch/Q1/q1_cam.dts && echo OK && fdtget -t x q1_cam.dtb /i2c@30a50000/sensor@10/port/endpoint link-frequencies && fdtget q1_cam.dtb /i2c@30a50000/sensor@10/port/endpoint data-lanes; echo ---bad; dtc -I dts -O dtb -o q1_bad.dtb /Users/donh/.elinux_scratch/Q1/q1_cam_bad.dts; echo rc=$?'
```

```text
Version: DTC 1.7.2
OK
0 1b2e0200
1 2
---bad
/Users/donh/.elinux_scratch/Q1/q1_cam_bad.dts:51.11-56.6: Warning (unit_address_vs_reg): /csi@32e40000/ports/port@1: node has a unit name, but no reg or ranges property
/Users/donh/.elinux_scratch/Q1/q1_cam_bad.dts:25.26-30.7: Warning (graph_endpoint): /i2c@30a50000/sensor@10/port/endpoint: graph connection to node '/csi@32e40000/ports/port@0/endpoint' is not bidirectional
/Users/donh/.elinux_scratch/Q1/q1_cam_bad.dts:45.23-48.7: Warning (graph_endpoint): /csi@32e40000/ports/port@0/endpoint: graph connection to node '/isi@32e00000/port/endpoint' is not bidirectional
q1_bad.dtb: Warning (graph_child_address): Failed prerequisite 'graph_endpoint'
rc=0
```

출력에서 볼 것: `link-frequencies`가 32비트 셀 두 개 `0 1b2e0200`(= 0x1b2e0200 = 456000000)로 저장됐다 — 이것이 uint64다. 깨진 DT는 **경고만 내고 rc=0으로 dtb를 만든다**. 빌드가 성공했다고 그래프가 맞는 게 아니다. 빌드 로그의 `graph_endpoint ... is not bidirectional` 경고는 카메라 bring-up에서 반드시 0으로 만들어야 하는 경고다(커널 빌드에서는 `make dtbs W=1`로 더 많이 본다 — D7).

### 8.5 예제 8 — 진짜 imx219 드라이버에 틀린 DT를 먹여 보기 (QEMU)

무엇을 확인하나: DT endpoint 값이 틀렸을 때 **실제 커널 6.18.55의 imx219 드라이버**가 probe에서 무엇을 찍는지. 카메라 HW가 없으므로, QEMU virt 머신 DT에 (1) `gpio-sim` GPIO 칩, (2) 그 GPIO 두 줄로 만든 bit-bang `i2c-gpio` 버스, (3) 그 버스에 IMX219 노드 네 개(정상 / link freq 틀림 / 3 lane / 4 lane + 옛 363 MHz + non-continuous)를 붙였다. 센서가 없으니 정상 노드도 결국 chip ID 읽기에서 실패하지만, **DT 검사 단계(check_hwcfg)는 그 전에 실행되므로** 우리가 보고 싶은 부분은 전부 진짜 출력이다.

DT 추가분 일부(`q1_virt_add.dtsi`, cam@12·cam@13과 수신기 port@2·3 생략):

```dts
/ {
	q1_cam24m: q1-clk24m {
		compatible = "fixed-clock";
		#clock-cells = <0>;
		clock-frequency = <24000000>;
	};

	q1-gpio-sim {
		compatible = "gpio-simulator";
		q1_bank: bank0 {
			gpio-controller;
			#gpio-cells = <2>;
			ngpios = <8>;
			gpio-sim,label = "q1-bank0";
		};
	};

	q1-i2c {
		compatible = "i2c-gpio";
		sda-gpios = <&q1_bank 0 6>;	/* 6 = GPIO_OPEN_DRAIN */
		scl-gpios = <&q1_bank 1 6>;
		i2c-gpio,scl-output-only;
		i2c-gpio,delay-us = <5>;
		#address-cells = <1>;
		#size-cells = <0>;

		cam@10 {	/* A: correct - 2 lanes, 456 MHz */
			compatible = "sony,imx219"; reg = <0x10>; clocks = <&q1_cam24m>;
			port { q1_a: endpoint { remote-endpoint = <&q1_rx_a>; clock-lanes = <0>;
				data-lanes = <1 2>; link-frequencies = /bits/ 64 <456000000>; }; };
		};
		cam@11 {	/* B: wrong link frequency */
			compatible = "sony,imx219"; reg = <0x11>; clocks = <&q1_cam24m>;
			port { q1_b: endpoint { remote-endpoint = <&q1_rx_b>; clock-lanes = <0>;
				data-lanes = <1 2>; link-frequencies = /bits/ 64 <450000000>; }; };
		};
	};

	q1-csi-rx {
		status = "disabled";	/* no driver: stand-in receiver */
		ports {
			#address-cells = <1>;
			#size-cells = <0>;
			port@0 { reg = <0>; q1_rx_a: endpoint { remote-endpoint = <&q1_a>; }; };
			port@1 { reg = <1>; q1_rx_b: endpoint { remote-endpoint = <&q1_b>; }; };
		};
	};
};
```

게스트에서 돌린 스크립트(`/q1/run.sh` — BusyBox initramfs에 넣고 `LAB_CMD=sh~/q1/run.sh`로 실행). v4l2-fwnode·v4l2-common의 `pr_debug`/`dev_dbg`를 보려고 dynamic debug를 켠다:

```sh
#!/bin/sh
for a in $(cat /proc/cmdline); do case $a in q1sleep=*) Q1_SLEEP="${a#q1sleep=}";; esac; done
# runs inside the BusyBox guest (LAB_CMD)
echo 'file v4l2-fwnode.c +p' > /proc/dynamic_debug/control
echo 'file v4l2-common.c +p' > /proc/dynamic_debug/control
insmod /q1/i2c-gpio.ko
insmod /q1/v4l2-cci.ko
insmod /q1/imx219.ko; echo "insmod rc=$?"
echo "=== deferred right after insmod"; cat /sys/kernel/debug/devices_deferred
sleep ${Q1_SLEEP:-3}
echo "=== uname: $(uname -r)"
dmesg | grep -E "imx219|v4l2|lane|link-freq|link freq|i2c-gpio|Oops|Unable to handle|pc :" | sed 's/^\[ *[0-9.]*\] //'
echo "=== dmesg tail"; dmesg | tail -25
echo "=== bound devices"
for d in /sys/bus/i2c/devices/*-001*; do echo "$d driver=$(basename $(readlink $d/driver 2>/dev/null) 2>/dev/null)"; done
```

VM 쪽 드라이버 스크립트(`q1_guest.sh base` 또는 `q1_guest.sh e32`). initramfs에 모듈 3개(imx219·v4l2-cci·i2c-gpio)를 넣고, 랩의 `virt.dts`(`-m 512M` 기준 덤프)를 `/include/`해 위 추가분을 붙여 dtc로 컴파일한 뒤 `-dtb`로 넘긴다:

```sh
#!/bin/bash
# usage: q1_guest.sh <variant: base|e32>   (runs on the VM host)
set -e
W=/var/tmp/Q1; V=$1; K=6.18.55-dom; M=/var/lab/out/modules-dom/lib/modules/$K/kernel
mkdir -p $W && cd $W
if [ ! -d rootfs ]; then cp -a /var/lab/build/rootfs rootfs; fi
mkdir -p rootfs/q1
cp $M/drivers/media/i2c/imx219.ko $M/drivers/media/v4l2-core/v4l2-cci.ko $M/drivers/i2c/busses/i2c-gpio.ko rootfs/q1/
cp /Users/donh/.elinux_scratch/Q1/q1_run.sh rootfs/q1/run.sh; chmod +x rootfs/q1/run.sh
(cd rootfs && find . | cpio -o -H newc --quiet | gzip) > q1_initrd.gz
SRC=/Users/donh/.elinux_scratch/Q1/q1_virt_add.dtsi
if [ "$V" = e32 ]; then
  CAM='cam@14 { compatible = "sony,imx219"; reg = <0x14>; clocks = <\&q1_cam24m>; port { q1_e: endpoint { remote-endpoint = <\&q1_rx_e>; clock-lanes = <0>; data-lanes = <1 2>; link-frequencies = <456000000>; }; }; };'
  RX='port@4 { reg = <4>; q1_rx_e: endpoint { remote-endpoint = <\&q1_e>; }; };'
else CAM=""; RX=""; fi
sed -e "s|Q1_CAM_E|$CAM|" -e "s|Q1_RX_E|$RX|" $SRC > q1_add_$V.dtsi
printf '/include/ "/var/lab/out/virt.dts"\n/include/ "q1_add_%s.dtsi"\n' $V > q1_virt_$V.dts
dtc -q -I dts -O dtb -o q1_virt_$V.dtb q1_virt_$V.dts
timeout 240 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image-dom -initrd q1_initrd.gz -dtb q1_virt_$V.dtb \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/q1/run.sh $EXTRA" > q1_out_$V.txt 2>&1 || true
```

먼저 실패한 첫 시도: 수신기 자리의 `q1-csi-rx` 노드에 `status`를 안 줬을 때(compatible 없음 → 어떤 드라이버도 붙지 않는 "살아 있는" 노드). 출력의 앞부분:

```text
insmod rc=0
=== deferred right after insmod
0-0010	i2c: wait for supplier /q1-csi-rx
0-0011	i2c: wait for supplier /q1-csi-rx
0-0012	i2c: wait for supplier /q1-csi-rx
0-0013	i2c: wait for supplier /q1-csi-rx
```

출력에서 볼 것: 센서 probe가 **아예 호출되지 않았다**. `remote-endpoint`는 fw_devlink가 "공급자(supplier)" 관계로 해석하는 속성이다(`drivers/of/property.c`의 `parse_remote_endpoint` — supplier = 상대 endpoint의 포트 부모 장치). 상대 노드가 드라이버가 붙을 수 없는 상태로 살아 있으면 센서는 영원히 기다린다(15초를 기다린 재실험에서도 그대로였다). 실제 보드에서는 **CSI-2 수신기 드라이버를 config에서 빠뜨리거나 모듈을 안 올렸을 때 센서 드라이버가 에러 한 줄 없이 조용히 안 뜨는** 형태로 나타난다. 확인 명령은 `cat /sys/kernel/debug/devices_deferred`. 수신기 노드에 `status = "disabled"`를 주면 fw_devlink가 공급자에서 제외해서 아래처럼 probe가 진행된다.

두 번째 실행(`status = "disabled"`를 넣은 위 DT, `limactl shell elinux -- /Users/donh/.elinux_scratch/Q1/q1_guest.sh base`). dmesg 원문에서 발췌:

```text
[    6.784733][   T72] no lane mapping given, using defaults
[    6.784818][   T72] data-lanes property exists; disabling default mapping
[    6.784865][   T72] lane 0 position 1
[    6.784892][   T72] lane 1 position 2
[    6.784973][   T72] clock lane position 0
[    6.785077][   T72] no lane polarities defined, assuming not inverted
[    6.785468][   T72] link-frequencies 0 value 456000000
[    6.785803][   T72] imx219 0-0010: enabling link frequency 456000000 Hz
[    6.788271][   T72] imx219 0-0010: supply VANA not found, using dummy regulator
[    6.789449][   T72] imx219 0-0010: supply VDIG not found, using dummy regulator
[    6.789855][   T72] imx219 0-0010: supply VDDL not found, using dummy regulator
[    6.821451][   T72] imx219 0-0010: error -EIO: chip id mismatch: 219!=0
[    6.826142][   T72] imx219 0-0010: probe with driver imx219 failed with error -5
[    6.833670][   T72] link-frequencies 0 value 450000000
[    6.833850][   T72] imx219 0-0011: no matching link frequencies found
[    6.833878][   T72] imx219 0-0011: specified in firmware:
[    6.833904][   T72] imx219 0-0011: 	450000000 Hz
[    6.833934][   T72] imx219 0-0011: driver supported:
[    6.834133][   T72] imx219 0-0011: 	456000000 Hz
[    6.834223][   T72] imx219 0-0011: error -EINVAL: Link frequency not supported: 450000000
[    6.837858][   T72] imx219 0-0011: probe with driver imx219 failed with error -22
[    6.839275][   T72] lane 2 position 3
[    6.839381][   T72] link-frequencies 0 value 456000000
[    6.839500][   T72] imx219 0-0012: error -EINVAL: only 2 or 4 data lanes are currently supported
[    6.839641][   T72] imx219 0-0012: probe with driver imx219 failed with error -22
[    6.840424][   T72] non-continuous clock
[    6.840508][   T72] link-frequencies 0 value 363000000
[    6.840634][   T72] imx219 0-0013: enabling link frequency 363000000 Hz
[    6.840677][   T72] imx219 0-0013: Link frequency of 363000000 not supported, but has been incorrectly advertised previously
[    6.840710][   T72] imx219 0-0013: Using link frequency of 364000000
[    6.851505][   T72] imx219 0-0013: error -EIO: chip id mismatch: 219!=0
```

(cam@11~13의 반복되는 lane 파싱 줄과 cam@13의 regulator 줄은 생략했다. 시간 값은 TCG 에뮬레이션 기준이다.)

출력에서 볼 것:

- **cam@10(정상)**: v4l2-fwnode의 디버그 줄이 §8.2의 순서를 그대로 보여 준다 — 기본 매핑 → `data-lanes` 발견 → lane 0은 물리 1, lane 1은 물리 2 → clock lane 0 → 극성 기본 → link freq 456 MHz → 드라이버 표와 일치(`enabling link frequency`). DT 검사를 통과한 뒤 regulator(DT에 없으니 dummy) → 전원 → **I2C로 chip ID 0x0000 읽음 → `chip id mismatch: 219!=0`**. 가짜 버스라 SDA가 항상 0(ACK, 데이터 0)으로 읽혀서다. 실제 보드에서 이 줄이 나오면 "I2C는 통하는데 다른 장치가 응답"(주소 충돌, 다른 센서 모델)이고, `failed to read chip id`면 "아무도 응답 안 함"(전원·리셋·MCLK·주소·SerDes 링크)이다.
- **cam@11(450 MHz)**: `v4l2_link_freq_to_bitmap()`이 DT 값과 드라이버 값을 나란히 찍어 준다 — dev_dbg라서 **dynamic debug를 켜야 보인다**. 꺼져 있으면 `no matching link frequencies found`와 `Link frequency not supported: 450000000` 두 줄뿐이다. 보드 DT를 다른 보드에서 복사해 왔을 때 가장 흔한 실패다.
- **cam@12(3 lane)**: 파싱은 성공(lane 2 position 3)했지만 드라이버 정책이 2/4만 허용.
- **cam@13(4 lane, 363 MHz, non-continuous)**: `non-continuous clock` 플래그가 파싱됐고, 옛 DT의 363 MHz를 경고 후 364 MHz로 바꿔 받는 하위 호환 코드(§8.3 표)가 실제로 동작했다. 드라이버가 DT 실수를 "봐주는" 드문 예다.

### 8.6 예제 9 — `/bits/ 64`를 빼먹으면: 6.18.55 imx219의 NULL 역참조

무엇을 확인하나: `link-frequencies = <456000000>;`처럼 32비트로 쓴 흔한 실수가 어떻게 드러나는지. `q1_guest.sh e32`가 위 DT에 cam@14를 이렇게 추가한다(나머지 동일).

```dts
cam@14 { compatible = "sony,imx219"; reg = <0x14>; clocks = <&q1_cam24m>;
	port { q1_e: endpoint { remote-endpoint = <&q1_rx_e>; clock-lanes = <0>;
		data-lanes = <1 2>; link-frequencies = <456000000>; }; }; };
```

```text
[    7.495516][   T72] OF: size of link-frequencies in node /q1-i2c/cam@14/port/endpoint is not a multiple of 8
[    7.495739][   T72] imx219 0-0014: no link frequencies in firmware
[    7.496346][   T72] Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
[    7.496393][   T72] Mem abort info:
[    7.496405][   T72]   ESR = 0x0000000096000006
[    7.496436][   T72]   EC = 0x25: DABT (current EL), IL = 32 bits
...
[    7.506093][   T72] Internal error: Oops: 0000000096000006 [#1]  SMP
[    7.507528][   T72] Modules linked in: imx219(+) v4l2_cci i2c_gpio
[    7.509785][   T72] CPU: 0 UID: 0 PID: 72 Comm: insmod Not tainted 6.18.55-dom #1 PREEMPT 
[    7.510981][   T72] pc : imx219_check_hwcfg.constprop.0+0x100/0x218 [imx219]
[    7.511871][   T72] lr : imx219_check_hwcfg.constprop.0+0xec/0x218 [imx219]
...
[    7.517267][   T72] Call trace:
[    7.517529][   T72]  imx219_check_hwcfg.constprop.0+0x100/0x218 [imx219] (P)
[    7.517910][   T72]  imx219_probe+0x78/0x718 [imx219]
[    7.518073][   T72]  i2c_device_probe+0x19c/0x400
[    7.519525][   T72]  really_probe+0xc8/0x3a0
...
[    7.525183][   T72] ---[ end trace 0000000000000000 ]---
Segmentation fault
insmod rc=139
```

(레지스터 덤프와 호출 스택 중간은 `...`로 생략.)

출력에서 볼 것: dtc는 이 실수를 잡지 못한다(셀 크기는 binding 스키마만 안다 — `dt-validate`는 잡는다, D7). 커널은 `OF: ... is not a multiple of 8`로 정확히 원인을 말하고, v4l2 헬퍼는 `no link frequencies in firmware`(-ENODATA)를 정상적으로 돌려준다. 그런데 imx219의 에러 경로가 `dev_err_probe(..., "Link frequency not supported: %lld\n", ep_cfg.link_frequencies[0])`로 **할당되지 않은(NULL) 배열의 0번을 읽는다** — §8.2에서 본 대로 `alloc_parse`는 개수가 양수일 때만 배열을 만들기 때문이다. Don의 펌웨어 감각으로는 "에러 로그를 찍는 코드가 크래시를 일으키는" 고전적인 버그다. arm64 data abort(EC 0x25, 주소 0)가 그대로 Oops가 되고 insmod 프로세스가 죽었다(rc 139 = SIGSEGV). 이것은 이 랩의 6.18.55 소스에서 재현한 것이고, 최신 mainline에서 고쳐졌는지는 확인하지 않았다(확인 필요). 교훈은 두 가지: (1) `link-frequencies`는 반드시 `/bits/ 64`, (2) "DT 실수 = 깔끔한 -EINVAL"이라고 믿지 말 것 — 드라이버의 에러 경로는 덜 테스트된다.

---

## 9. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="20" text-anchor="middle" font-size="13">센서 드라이버 (I2C)</text>
  <text x="510" y="20" text-anchor="middle" font-size="13">CSI-2 수신기 드라이버 (platform)</text>
  <rect x="10" y="30" width="320" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="55" text-anchor="middle" font-size="12">DT endpoint: data-lanes · link-frequencies</text>
  <rect x="10" y="90" width="320" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="115" text-anchor="middle" font-size="12">v4l2_fwnode_endpoint_alloc_parse()</text>
  <rect x="10" y="150" width="320" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="175" text-anchor="middle" font-size="12">v4l2_fwnode_endpoint.bus.mipi_csi2</text>
  <rect x="10" y="210" width="320" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="235" text-anchor="middle" font-size="12">probe: lane 수 · v4l2_link_freq_to_bitmap()</text>
  <rect x="10" y="270" width="320" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="295" text-anchor="middle" font-size="12">V4L2_CID_LINK_FREQ · PIXEL_RATE 컨트롤</text>
  <line x1="170" y1="70" x2="170" y2="89" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="170" y1="130" x2="170" y2="149" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="170" y1="190" x2="170" y2="209" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="170" y1="250" x2="170" y2="269" stroke="currentColor" marker-end="url(#ah9)"/>
  <rect x="350" y="90" width="320" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="115" text-anchor="middle" font-size="12">수신기 probe: 자기 endpoint 파싱</text>
  <rect x="350" y="150" width="320" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="175" text-anchor="middle" font-size="12">v4l2_async: 센서 subdev 바인딩</text>
  <rect x="350" y="210" width="320" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="235" text-anchor="middle" font-size="12">STREAMON → v4l2_get_link_freq()</text>
  <rect x="350" y="270" width="320" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="295" text-anchor="middle" font-size="12">Ths-settle 계산 → 센서 스트림 시작</text>
  <line x1="510" y1="130" x2="510" y2="149" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="510" y1="190" x2="510" y2="209" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="510" y1="250" x2="510" y2="269" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="330" y1="50" x2="350" y2="105" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah9)"/>
  <line x1="330" y1="290" x2="350" y2="235" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah9)"/>
</svg>
```

그림 9 — DT의 HW 파라미터가 두 드라이버로 흘러가는 길. 점선 위: 같은 DT 그래프를 수신기도 읽는다. 점선 아래: 스트림 시작 시 수신기가 센서의 컨트롤(또는 `get_mbus_config`)에서 link freq를 가져간다.

- **fwnode 계층**: `fwnode_graph_get_next_endpoint()`, `fwnode_property_read_u32_array()`, `fwnode_property_count_u64()` — DT와 ACPI를 같은 API로 읽는 추상화(D8). x86 노트북 카메라(Intel IPU)는 ACPI로 같은 정보를 받는다.
- **센서 드라이버**: `struct v4l2_subdev` + `v4l2_ctrl_handler`. HW 레지스터는 `v4l2-cci`(`cci_write()` — 16비트 주소·8/16비트 값을 다루는 regmap 래퍼, H2 regmap 위)로 쓴다. 예제 8에서 imx219가 `v4l2_cci` 모듈에 의존한 이유다.
- **수신기 드라이버**: `v4l2_async_nf_add_fwnode_remote()`로 "저 endpoint의 주인 subdev"를 기다리고(I5 §11), 스트림 시작 시 `v4l2_get_link_freq()`와 `get_mbus_config`(lane 수·플래그)로 PHY를 설정한다. CSI-2 에러(ECC·CRC·sync)는 보통 수신기의 IRQ 핸들러가 카운터로 모아 debugfs나 `dev_dbg`로 보여 준다(드라이버마다 다름 — Q2에서 i.MX8MP CSIS의 이벤트 카운터를 본다).
- **SerDes 드라이버**: 역직렬화기 = I2C 클라이언트이자 subdev이자 i2c-atr/i2c-mux 제공자이자(링크별 어댑터) GPIO 제공자. 직렬화기 = 원격 I2C 클라이언트 + subdev + clock 제공자(센서 MCLK를 직렬화기가 만든다 — binding 예제의 `clocks = <&serializer>`) + GPIO 제공자(원격 센서의 reset 핀). 드라이버 모델(G1) 관점에서 한 칩이 여러 프레임워크의 provider가 되는 좋은 예다.
- **드라이버를 쓸 때 다시 만나는 곳**: 새 센서 드라이버를 쓰면 (1) PLL 표 또는 계산 → `link_freq_menu`·`PIXEL_RATE`, (2) HTS/VTS → `HBLANK`/`VBLANK` 컨트롤, (3) 노출/게인 레지스터 → 표준 컨트롤, (4) `check_hwcfg`에서 lane·link freq 검사 — 이 노트의 §1~4가 그대로 코드가 된다(P3 데이터시트 → 드라이버 워크플로).

---

## 10. Embedded 실무에서 다시 보기

### 10.1 장면 1 — 새 보드에서 카메라가 안 나온다: 층별 체크리스트

예를 들어 센서 키트 보드에 IMX219 계열 센서를 붙였는데 `/dev/video0`이 없거나 프레임이 안 온다. Don의 PCIe bring-up과 같은 방식으로 **아래 층부터** 확인한다.

1. **전원·클럭·리셋**: 센서 레일 3개(아날로그·디지털·I/O) 순서, MCLK 24 MHz가 실제로 나오는지(scope), reset/powerdown GPIO 극성. dmesg에 `failed to read chip id`면 여기.
2. **I2C**: `i2cdetect -y <bus>`로 주소 응답(SerDes 뒤라면 링크 lock 먼저). chip ID 레지스터 읽기.
3. **probe**: dmesg의 `Link frequency not supported`, `only 2 or 4 data lanes`, `devices_deferred`(예제 8). 드라이버가 안 떴으면 receiver 드라이버·fw_devlink부터.
4. **스트림 시작**: `MODE_SELECT`(0x0100)=1이 쓰였나. data lane을 scope로 보면 LP-11에서 HS 버스트로 바뀌나(§3.1).
5. **수신기 PHY**: lane 수·순서·극성(`lane-polarities`), settle, continuous 설정이 송신기와 같은가. 수신기 에러 카운터(ECC/CRC/sync).
6. **포맷**: 센서 mbus code(Bayer 순서·bit depth)와 수신기·DMA 포맷이 같은가(I5 §9.2의 `-EPIPE`).
7. **화질**: BLC·AWB·LSC 튜닝(§7).

### 10.2 장면 2 — 로봇에 서라운드 카메라 4대 (GMSL2 → Jetson 계열, 기억 기반 구성)

자율주행 로봇의 흔한 구성: 2~3 MP 카메라 4대, 각 모듈 안에 센서 + GMSL2 직렬화기, 차체 쪽 쿼드 역직렬화기(MAX96712 급 — 랩 커널에서는 staging 드라이버) 하나가 4링크를 VC0~3으로 합쳐 4-lane CSI-2 두 포트로 SoC에 들어간다(NVIDIA Jetson 생태계의 일반적인 형태 — 기억 기반, 벤더 BSP 문서로 확인). 계산기 출력처럼 2MP30 RAW12 4대는 4-lane 한 포트에 lane당 1.07 Gb/s로 들어가지만, 3MP나 60fps로 가면 포트 두 개로 나눠야 한다. 실무 과제: (1) PoC 켜기 → 링크 lock 대기 → 원격 I2C 순서를 드라이버/스크립트로 보장, (2) 하나의 FSYNC로 4대 동기(Q3), (3) 케이블 하나가 빠졌을 때 나머지 3대가 계속 도는지(역직렬화기의 링크별 lock 상태 감시와 VC 재매핑) — 국방·차량 고객이 꼭 묻는 고장 모드다.

### 10.3 장면 3 — 카메라가 뜨거워지면 (Scout 기술 면접 주제와 연결)

센서 온도가 오르면 암전류가 늘어 black level이 올라가고 hot pixel이 늘어난다(BLC·DPC가 고정값이면 어두운 영상이 뿌옇게 됨). 많은 센서가 온도 센서 레지스터나 embedded data에 온도를 실어 준다. 펌웨어/드라이버 쪽 대응: 온도를 주기적으로 읽어 hwmon/thermal(I4)로 노출하고, 임계 이상이면 fps를 낮추거나(VBLANK 증가 → 소비전력 감소) 스트림을 멈추는 정책, 그리고 SerDes·PoC 전류 감시. 이 노트의 HTS/VTS 계산이 "fps를 낮추면 얼마나 쉬나"의 답이다.

### 10.4 장면 4 — EMI 때문에 link frequency를 바꿔야 한다

차량 EMC 시험에서 456 MHz 고조파가 특정 무선 대역을 친다는 결과가 나오면, 센서 PLL을 다른 link frequency로 바꾸고(센서 벤더 레지스터 표 또는 계산), 그 값을 DT `link-frequencies`와 드라이버 메뉴에 동시에 넣어야 한다. imx219 같은 "고정 표" 드라이버라면 드라이버 패치가 필요하다. 이때 §4의 계산기로 새 주파수에서 원하는 모드의 lane rate가 여전히 충분한지(margin ≥ 1), 수신기 상한 안인지 먼저 확인한다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `link-frequencies`에 lane bit rate(912 MHz)를 씀 | `no matching link frequencies found` | D-PHY link freq = bit rate / 2 | 클럭 lane 주파수(456 MHz)로 |
| `/bits/ 64` 누락 | `OF: size of link-frequencies ... is not a multiple of 8`, 6.18.55 imx219는 Oops | 속성 타입이 uint64-array | `link-frequencies = /bits/ 64 <...>;`, `dt-validate`로 검사 |
| 다른 보드 DT를 복사, link freq만 다름 | `Link frequency not supported: 450000000` | 드라이버 표와 불일치 | 센서 PLL 설정과 맞는 값으로, dynamic debug로 드라이버 값 확인 |
| CSI-2 수신기 드라이버 미적재 | 센서가 에러 없이 안 뜸, `devices_deferred`에 `wait for supplier` | fw_devlink가 remote-endpoint를 공급자로 봄 | 수신기 드라이버 활성화(또는 쓰지 않는 노드는 `status = "disabled"`) |
| `remote-endpoint` 한쪽만 수정 | dtc `graph_endpoint ... not bidirectional` 경고, async 바인딩 안 됨 | 그래프는 양방향이어야 함 | 경고 0으로 |
| 센서·수신기 lane 수 불일치 | 프레임이 안 오거나 줄무늬·밀림 | `data-lanes`가 양쪽 다름 | 양쪽 endpoint `data-lanes` 일치 |
| PCB에서 lane P/N 반전 | HS 버스트는 보이는데 sync/ECC 에러만 | 극성 반전 | `lane-polarities` (수신기가 지원할 때) 또는 HW 수정 |
| continuous/non-continuous 불일치 | 첫 프레임 후 멈춤, 간헐적 깨짐 | 클럭 lane 모드 차이 | `clock-noncontinuous`를 양쪽 HW 능력에 맞게 |
| 센서 4K60을 수신기 상한 무시하고 선택 | settle 계산 `-EINVAL`, 또는 에러 폭주 | lane당 1.78 Gb/s > 수신기 1.5 Gb/s | 계산기로 미리 확인, lane 늘리거나 RAW10/30fps |
| BLC 값 불일치 | 어두운 부분만 색 틀어짐, 뿌연 검정 | pedestal을 다르게 가정 | 센서 black level 레지스터와 ISP BLC를 맞춤 (예제 6) |
| SerDes 뒤 센서를 링크 lock 전에 접근 | 원격 I2C NACK/타임아웃, BCC 에러 | PoC·링크 준비 시간 | 링크 lock 확인 후 원격 I2C, 재시도 |

---

## 12. 면접에서 이렇게 말한다

**Q.** Walk me through how a frame gets from an image sensor into DRAM on an embedded Linux SoC.

**A.** 센서 픽셀 어레이가 행 단위로 노출·ADC 변환을 하고, 행마다 CSI-2 long packet 하나(헤더 DI·WC·ECC, payload, CRC-16)로 만들어 D-PHY lane으로 보낸다. 프레임은 FS short packet으로 시작해 FE로 끝난다. SoC의 CSI-2 수신기가 ECC·CRC를 검사하고 VC/DT로 스트림을 나눠 ISP나 DMA로 넘기고, ISP가 RAW Bayer를 YUV로 만들어 DMA가 DRAM 버퍼에 쓴다. CPU는 I2C로 센서 모드·노출·게인을 쓰고 ISP 통계로 3A를 돌릴 뿐 픽셀 경로에는 없다.

> The sensor exposes and digitizes row by row, and its CSI-2 transmitter sends each row as a long packet — a header with data identifier, word count and ECC, the payload, and a CRC-16 — over D-PHY lanes, framed by frame-start and frame-end short packets. The SoC's CSI-2 receiver checks ECC and CRC, demultiplexes by virtual channel and data type, and feeds an ISP or a DMA engine, which writes the frames into DRAM buffers. The CPU is only in the control path: it programs the sensor over I2C and runs the 3A algorithms on ISP statistics.

**꼬리 질문**: "What happens if a CRC error occurs?" → 재전송이 없어서 그 줄은 버려지고 수신기 에러 카운터가 오른다. 프레임을 통째로 버릴지는 수신기 정책.

**Q.** Given a sensor mode, how do you choose the number of lanes and the link frequency?

**A.** 센서는 한 행의 활성 픽셀을 픽셀 클럭 속도 그대로 내보내므로, 필요한 lane 비트레이트는 pixel_rate × bpp ÷ lane 수다. pixel_rate = HTS × VTS × fps. D-PHY는 DDR이라 link frequency는 그 절반이다. 예를 들어 IMX219 1080p30 RAW10은 182.4 Mpix/s × 10 ÷ 2 lane = 912 Mb/s, link freq 456 MHz. 그다음 센서가 지원하는 PLL 조합, 수신기 lane당 상한(i.MX8MP CSIS는 1.5 Gb/s), EMI 대역을 확인해서 DT `link-frequencies`에 적는다.

> The sensor streams active pixels at its internal pixel rate, so the per-lane bit rate must be at least pixel rate times bits per pixel divided by the lane count, where pixel rate is HTS times VTS times frame rate. D-PHY is double data rate, so the link frequency is half the lane bit rate. For an IMX219 at 1080p30 RAW10 that's 182.4 megapixels per second times 10 over 2 lanes — 912 megabits per lane, a 456 MHz link frequency. Then I check the sensor's supported PLL settings, the receiver's per-lane maximum, and EMI constraints, and put the validated value in the device tree.

**꼬리 질문**: "Why not just use the payload rate?" → 센서 행 버퍼가 작아서 blanking 동안 몰아 보내는 구조라 payload 평균이 아니라 행 내 순간 속도가 기준이다.

**Q.** What is the difference between D-PHY and C-PHY, and between HS and LP modes?

**A.** D-PHY는 차동 쌍 lane + 별도 클럭 lane(DDR)이고, C-PHY는 3선 trio에 3레벨 심볼을 써서 클럭을 데이터 전이에서 복원하며 심볼당 약 2.28비트를 싣는다. 같은 핀 수로 C-PHY가 대역폭이 크다. D-PHY lane은 1.2 V 단일단 LP 모드(정지·제어)와 200 mV 차동 HS 모드(데이터)를 오가고, LP-11 → LP-01 → LP-00 → HS-0 → sync → 데이터 → trail → LP-11 순서로 버스트를 보낸다. 클럭 lane은 continuous 또는 non-continuous로 운용한다.

> D-PHY uses differential-pair lanes plus a dedicated DDR clock lane; C-PHY uses three-wire trios with three-level symbols and an embedded clock recovered from symbol transitions, carrying about 2.28 bits per symbol, so it gets more bandwidth per pin. A D-PHY lane alternates between 1.2-volt single-ended low-power mode for idle and control, and roughly 200-millivolt differential high-speed mode for data, entering HS through the LP-11, LP-01, LP-00 sequence. The clock lane can stay in HS continuously or drop to LP between bursts.

**Q.** What are virtual channels used for?

**A.** 한 물리 CSI-2 링크 위에서 여러 스트림을 구분하는 논리 채널이다. 대표적으로 SerDes 역직렬화기가 카메라 4대를 VC0~3으로 태그해 한 포트로 합치고, SoC 수신기가 VC별로 다른 DMA 채널로 보낸다. 같은 카메라의 embedded data는 같은 VC에 DT만 다르게, HDR의 다른 노출은 다른 VC로 보내기도 한다. v1.x는 2비트(4개), v2.0부터는 확장으로 16개. Linux에서는 subdev routing/streams API로 표현한다.

> A virtual channel is a logical stream ID within one physical CSI-2 link. The classic use is a deserializer aggregating four cameras onto one port, tagging them VC0 to VC3, so the SoC receiver can steer each into its own DMA context. Embedded metadata usually shares the VC with a different data type, and HDR exposures sometimes use separate VCs. In Linux this is modeled with the subdev routing and streams API.

**Q.** Why use GMSL or FPD-Link, and how does the SoC talk I2C to a sensor behind it?

**A.** D-PHY는 수십 cm용이라 수 m~15 m 케이블·진동·EMI 환경에서는 카메라 쪽 직렬화기와 SoC 쪽 역직렬화기를 동축 하나로 잇는다. 그 동축에 순방향 영상(수 Gb/s), 역방향 I2C/GPIO, PoC 전원이 같이 흐른다. I2C는 back-channel로 터널링되는데, 같은 주소 센서가 여러 대라 역직렬화기가 주소를 변환한다 — TI는 ATR로 링크별 별칭(커널 i2c-atr, ub960 드라이버가 SLAVE_ID/ALIAS 레지스터 쌍을 채움), GMSL1 MAX9286은 링크별 mux로 하나씩 열고 원격 장치 주소를 재프로그래밍한다.

> D-PHY is meant for tens of centimeters, so for meters of cable in a vehicle or robot we put a serializer in the camera and a deserializer at the SoC, connected by a single coax that carries forward video at several gigabits, a back channel for I2C and GPIO, and power over coax. I2C is tunneled over the back channel, and because identical cameras share the same sensor address, the deserializer translates addresses — TI uses an address translator with per-link aliases, which Linux models with the i2c-atr framework, while older GMSL parts gate each link with a mux and reprogram remote addresses one by one.

**꼬리 질문**: "How do you synchronize the four cameras?" → 역직렬화기가 FSYNC를 만들어 back-channel GPIO로 각 센서 트리거 핀에 보낸다(max9286 `set_fsync_period`, lock 비트 확인). 자세히는 Q3.

**Q.** Rolling or global shutter for a fast-moving drone doing visual odometry?

**A.** 기본은 global shutter다. rolling shutter는 행마다 노출 시각이 line time씩 밀려서 IMX219 1080p30이면 위아래가 약 20 ms 차이 난다. 빠른 회전에서 기하가 왜곡되어 VIO가 흔들린다. rolling을 써야 한다면 readout을 짧게 하는 모드를 고르고 행별 타임스탬프 보정(rolling shutter 모델)을 알고리즘에 넣는다. global은 해상도·감도·가격에서 손해다.

> Global shutter by default. With rolling shutter each row starts exposing one line time later — on an IMX219 at 1080p30 the top and bottom rows are about 20 milliseconds apart — so fast rotation warps geometry and hurts VIO. If rolling shutter is required, I'd pick a mode with a short readout and model the per-row timing in the estimator. Global shutter trades off resolution, sensitivity, and cost.

**Q.** The camera driver probes but you get no frames. How do you debug?

**A.** 층별로 내려간다. 스트림 시작 후 data lane을 scope로 본다 — LP-11에 계속 있으면 센서가 안 보내는 것(레지스터·전원·클럭·MODE_SELECT), HS 버스트가 있으면 수신기 쪽이다. 수신기 에러 카운터(ECC·CRC·sync)를 보고, DT `data-lanes`·`link-frequencies`·continuous clock·극성을 양쪽 대조하고, link freq가 settle 계산에 쓰이므로 정확한지 확인한다. 그다음 포맷(mbus code, Bayer 순서, 폭 정렬)과 media 링크 검증(-EPIPE)을 본다.

> I go layer by layer. With streaming enabled I probe a data lane: if it stays in LP-11 the sensor isn't transmitting, so I check power, clocks, and the stream-on register; if I see HS bursts the problem is on the receiver side. Then I read the receiver's ECC, CRC and sync error counters, cross-check data-lanes, link-frequencies, clock mode and lane polarity on both endpoints — the receiver derives its settle timing from the link frequency — and finally verify media-bus formats, Bayer order and stride alignment.

**Q.** Where does the ISP run and what are the 3A?

**A.** 센서 내장(OV5640처럼 YUV 출력), 모듈 내 컴패니언 ISP, SoC 하드웨어 ISP, 또는 소프트웨어/GPU ISP. 처리 블록은 BLC → LSC → DPC → AWB 게인 → demosaic → CCM → NR → 톤 매핑·감마 → YUV. 3A는 AE·AWB·AF이고, 하드웨어는 통계만 만들고 알고리즘은 CPU(Linux에서는 libcamera IPA)가 돌려 센서 노출·게인과 ISP 파라미터를 다시 쓴다. 센서의 설정 반영 지연(1~2프레임)을 모르면 AE가 진동한다.

> It can be in the sensor, in a companion chip, in the SoC's hardware ISP, or in software on the CPU or GPU. The pipeline is black-level correction, lens shading, defect correction, white-balance gains, demosaic, color matrix, noise reduction, tone mapping and gamma, then YUV conversion. 3A means auto exposure, white balance and focus: the hardware produces statistics, and software — libcamera's IPA modules on Linux — computes new sensor exposure and gain and ISP parameters, accounting for the sensor's frame delay in applying settings.

---

## 13. 직접 해보기

1. **개념**: IMX219 4-lane에서 pixel rate 281.6 Mpix/s, LLP 3448일 때 3280×2464 모드를 30 fps로 내려면 FLL이 얼마여야 하나? 최소 VBLANK(32)를 만족하나? 정답/힌트: FLL = 281.6e6 / (3448 × 30) ≈ 2722이고 최소 FLL은 2464 + 32 = 2496이므로 가능하다(VBLANK ≈ 258). 예제 1 스크립트의 `pr`과 MODES를 바꿔 확인.
2. **계산기 과제**: 예제 4 계산기에 "수신기 상한" 열을 추가해 i.MX8MP CSIS(1.5 Gb/s/lane, 4 lane)와 가상의 2.5 Gb/s 수신기 둘에 대해 각 구성의 OK/OVER를 출력하라. 그리고 4K30 RAW10은 CSIS에 들어가는지. 정답/힌트: 4K30 RAW10, HTS 4400 × VTS 2250 → 297 M × 10 ÷ 4 = 742.5 Mb/s → 들어간다.
3. **패킷 과제**: 예제 2를 확장해 FS(frame 1) → RAW10 1920픽셀 줄 3개(픽셀 값은 카운터) → FE를 바이트 스트림으로 만들고, 그 스트림을 lane 2개에 바이트 라운드로빈으로 나눈 뒤 다시 합쳐 파싱하는 수신기 함수를 써라(헤더 ECC 검사, WC만큼 읽고 CRC 검사). 정답/힌트: lane0 = 짝수 바이트, lane1 = 홀수 바이트. 일부러 payload 1비트를 뒤집어 CRC 에러가 잡히는지 확인.
4. **ECC 과제**: 예제 2의 `correct()`로 24개 데이터 비트 + 6개 ECC 비트 각각을 뒤집는 30가지 경우를 모두 돌려 전부 정정되는지, 그리고 2비트 오류 C(24,2)=276가지가 하나도 "잘못 정정"되지 않는지 세어 보라. 정답/힌트: 276가지 모두 -1이다. 같은 마스크로 VM에서 세어 본 결과 `unique cols 24 weights [3, 5]`, `2-bit data errors miscorrected: 0 of 276` — 모든 열의 무게가 홀수(3 또는 5)라 두 열의 XOR은 짝수 무게가 되어 어떤 열(홀수)과도, ECC 단일 비트(무게 1)와도 같을 수 없다. 이것이 SEC-DED의 구조다.
5. **DT 과제**: 예제 7의 `q1_cam.dts`에서 센서 `data-lanes = <1 2>`, 수신기 `data-lanes = <1 2 3 4>`로 바꾸면 dtc는 무엇이라고 하나? 커널에서는 누가 잡아야 하나? 정답/힌트: dtc는 아무 말도 안 한다(값의 의미를 모름). 수신기 드라이버가 `get_mbus_config`로 받은 lane 수와 자기 설정을 비교해야 한다 — 드라이버마다 다르다.
6. **QEMU 관찰 과제**: 예제 8의 DT에 `lane-polarities = <0 1>;`(2-lane인데 항목 2개)를 넣은 cam@15를 추가해 dmesg를 확인하라. 정답/힌트: §8.2의 3번 규칙대로 `invalid number of lane-polarities entries (need 3, got 2)`가 나오고 파싱이 -EINVAL — imx219는 `could not parse endpoint`.
7. **dynamic debug 관찰 과제**: 예제 8에서 dynamic debug를 끄고 다시 돌려 cam@11의 출력이 몇 줄로 줄어드는지 비교하라. 실제 제품 디버깅에서 `dyndbg="file v4l2-common.c +p"`를 커널 명령줄에 넣는 이유를 한 문장으로. 정답/힌트: DT 값/드라이버 값 나열은 dev_dbg라 사라지고 에러 두 줄만 남는다. built-in 드라이버는 부팅 중 probe되므로 명령줄로 켜야 한다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| CSI-2 | Camera Serial Interface 2 | MIPI의 카메라 → 호스트 패킷 프로토콜 |
| D-PHY | MIPI 물리층 | 차동 lane + 클럭 lane, HS/LP 모드 |
| C-PHY | MIPI 물리층 | 3선 trio, 3레벨 심볼, 클럭 내장, 16비트/7심볼 |
| lane | 데이터 선로 | D-PHY에서 차동 쌍 하나 |
| HS / LP | High Speed / Low Power | 200 mV 차동 고속 데이터 / 1.2 V 단일단 저속·정지 |
| LP-11 | stop state | 두 선 모두 high인 정지 상태 |
| Ths-settle | HS settle time | HS 진입 직후 수신기가 선로를 무시하는 시간 |
| continuous clock | 연속 클럭 | 클럭 lane이 blanking 중에도 HS 유지 |
| DI | Data Identifier | VC 2비트 + DT 6비트 |
| DT (data type) | 데이터 타입 | 0x2b = RAW10 등 패킷 내용 종류 |
| VC | Virtual Channel | 한 링크 위의 논리 스트림 번호 |
| WC | Word Count | long packet payload 바이트 수 |
| ECC | Error Correcting Code | 헤더 24비트용 6비트 Hamming, 1비트 정정 |
| FS / FE / LS / LE | Frame/Line Start/End | 프레임·행 경계 short packet |
| embedded data | DT 0x12 | 센서가 프레임에 붙이는 레지스터·상태 줄 |
| RAW Bayer | CFA 원본 | 픽셀마다 한 색 값만 있는 ISP 이전 데이터 |
| CFA | Color Filter Array | 픽셀 위 색 필터 배열(RGGB 등) |
| black level | pedestal | 빛 0일 때의 오프셋, BLC로 제거 |
| rolling shutter | 행 순차 노출 | 행마다 line time씩 노출 시작이 밀림 |
| global shutter | 동시 노출 | 모든 픽셀 동시 노출 후 순차 읽기 |
| HTS / LLP | line length | 한 행의 총 픽셀 클럭 수(활성 + HBLANK) |
| VTS / FLL | frame length | 한 프레임의 총 행 수(활성 + VBLANK) |
| pixel rate | 픽셀 클럭 | 센서 내부 읽기 속도, `V4L2_CID_PIXEL_RATE` |
| link frequency | 버스 주파수 | D-PHY 클럭 lane 주파수 = lane bit rate / 2 |
| DVP | parallel camera | PCLK·HSYNC·VSYNC·D[7:0] 병렬 인터페이스 |
| SerDes | Serializer/Deserializer | 긴 케이블용 직렬화기·역직렬화기 쌍 |
| GMSL | Gigabit Multimedia Serial Link | ADI(옛 Maxim)의 SerDes 계열 |
| FPD-Link | Flat Panel Display Link | TI의 SerDes 계열(III/IV) |
| back-channel | 역방향 채널 | SoC → 카메라 I2C·GPIO 터널 |
| ATR | Address Translator | 원격 I2C 주소를 링크별 별칭으로 변환 |
| PoC | Power over Coax | 동축으로 카메라 전원 공급 |
| FSYNC | frame sync | 여러 카메라 노출 시점을 맞추는 트리거 |
| ISP | Image Signal Processor | RAW → YUV 변환 하드웨어/소프트웨어 |
| 3A | AE·AWB·AF | 자동 노출·화이트밸런스·초점 |
| demosaic | debayer | 픽셀별 빠진 색 보간 |
| CCM | Color Correction Matrix | 센서 RGB → 표준 색공간 3×3 |
| CCS | Camera Command Set | MIPI 표준 센서 레지스터 맵(옛 SMIA) |
| CCI | Camera Control Interface | 카메라 제어용 I2C, 커널 `v4l2-cci` |
| fw_devlink | firmware device link | DT 참조로 probe 순서를 자동으로 정하는 커널 기능 |

---

## 15. 요약 & 체크리스트

카메라 HW 인터페이스는 "센서 타이밍 → 패킷 → 물리층 → (케이블) → 수신기 → ISP"의 층으로 나뉜다. 센서는 HTS × VTS × fps의 pixel rate로 행을 읽고, 행마다 CSI-2 long packet(DI·WC·ECC + payload + CRC)을 만들어 FS/FE 사이에 보낸다. 필요한 lane 비트레이트는 pixel_rate × bpp ÷ lanes이고 D-PHY `link-frequencies`는 그 절반이다 — 이 값은 수신기의 PHY settle 계산에 쓰이므로 정확해야 하며, IMX219 같은 드라이버는 표에 없는 값을 probe에서 거절한다. 긴 케이블에서는 GMSL/FPD-Link SerDes가 영상·I2C·전원·동기를 동축 하나로 나르고, 같은 주소의 센서들은 ATR이나 mux로 구분되며, 스트림은 VC로 합쳐진다. ISP는 BLC부터 톤 매핑까지 RAW를 영상으로 만들고 3A는 소프트웨어가 돌린다. Linux에서는 이 HW 사실들이 DT endpoint 속성 → `v4l2_fwnode` → 센서 컨트롤 → 수신기 설정으로 흐르고, 실험에서 본 것처럼 DT 실수는 깔끔한 에러부터 fw_devlink의 조용한 대기, 드라이버 Oops까지 다양한 모습으로 나타난다.

- [ ] rolling/global shutter의 차이와 readout skew를 숫자로 설명할 수 있다
- [ ] HTS/VTS/pixel rate로 fps, line time, 최대 노출을 계산할 수 있다
- [ ] CSI-2 short/long 패킷 구조와 DI(VC+DT), WC, ECC, CRC의 역할을 그릴 수 있다
- [ ] RAW10 packing과 폭 정렬 제약을 설명할 수 있다
- [ ] D-PHY HS/LP 전이와 continuous/non-continuous 클럭, C-PHY와의 차이를 말할 수 있다
- [ ] 센서 모드에서 lane 수와 `link-frequencies`를 계산하고 수신기 상한과 비교할 수 있다
- [ ] SerDes의 순방향/역방향/PoC, I2C ATR과 mux 방식, VC 합치기, FSYNC를 설명할 수 있다
- [ ] ISP 블록 순서와 BLC를 빼먹었을 때의 증상, 3A의 HW/SW 분담을 말할 수 있다
- [ ] `video-interfaces.yaml`의 endpoint 속성(`data-lanes`, `clock-lanes`, `clock-noncontinuous`, `link-frequencies`, `remote-endpoint`)을 정확한 타입으로 쓸 수 있다
- [ ] dmesg의 `Link frequency not supported`, `no link frequencies in firmware`, `wait for supplier`가 각각 무엇을 뜻하는지 안다
- [ ] 카메라가 안 나올 때 전원 → I2C → probe → LP/HS → 수신기 에러 → 포맷 순으로 디버깅 계획을 말할 수 있다

## 참고 자료

- Linux 커널 문서 "Pixel data transmitter and receiver drivers" — https://docs.kernel.org/driver-api/media/tx-rx.rst.html (랩 소스 `Documentation/driver-api/media/tx-rx.rst`)
- Linux 커널 문서 "Writing camera sensor drivers" — https://docs.kernel.org/driver-api/media/camera-sensor.html (랩 소스 `Documentation/driver-api/media/camera-sensor.rst`)
- DT binding: `Documentation/devicetree/bindings/media/video-interfaces.yaml`, `media/i2c/sony,imx219.yaml`, `media/i2c/ovti,ov5640.yaml`, `media/i2c/ti,ds90ub960.yaml`, `media/i2c/maxim,max9286.yaml`, `maxim,max96714.yaml`, `maxim,max96717.yaml` (랩 소스 6.18.55)
- 커널 소스 (elixir.bootlin.com, v6.18): `drivers/media/i2c/imx219.c`, `drivers/media/i2c/ov5640.c`, `drivers/media/i2c/ds90ub960.c`, `ds90ub953.c`, `max9286.c`, `max9271.c`, `max96714.c`, `max96717.c`, `drivers/i2c/i2c-atr.c`, `drivers/media/v4l2-core/v4l2-fwnode.c`, `v4l2-common.c`, `drivers/media/platform/nxp/imx-mipi-csis.c`, `include/media/mipi-csi2.h`, `drivers/gpu/drm/sun4i/sun6i_mipi_dsi.c`(ECC 표), `lib/crc-ccitt.c`
- MIPI Alliance CSI-2 / D-PHY / C-PHY 스펙 개요 — https://www.mipi.org/specifications/csi-2 (전문은 회원 문서)
- Bootlin, "Linux camera support" 계열 교육 자료 — https://bootlin.com/docs/ (카메라·V4L2 슬라이드)
- libcamera 문서 (ISP·IPA 구조) — https://libcamera.org/docs.html
- 관련 노트: I5(V4L2·media controller, §11 DT/async), D7(binding 검증), H2(regmap), H5(I2C), Q2(파이프라인 실무), Q3(동기화·타임스탬프), P4(회로도 → DT)
