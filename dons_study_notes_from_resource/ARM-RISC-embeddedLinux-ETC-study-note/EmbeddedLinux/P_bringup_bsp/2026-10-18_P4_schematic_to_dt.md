# P4. 회로도 읽고 DT 쓰기 — net 하나를 끝까지 따라가서 프로퍼티 한 줄로 옮기기

> **이 노트를 다 읽으면**: 회로도에서 SW가 알아야 할 정보(net 이름·극성·풀업·레벨 시프터·전원 레일과 EN 핀·I2C 주소 strap·오실레이터)를 체크리스트로 뽑을 수 있다 · IMU·카메라·CAN 트랜시버 회로도 조각을 "net → property" 표로 바꿔 DT 노드를 쓸 수 있다 · `enable-active-high` 누락, `GPIO_ACTIVE_LOW` 이중 반전, 틀린 `IRQ_TYPE`, 풀업 누락이 커널에서 어떤 증상으로 보이는지 QEMU에서 재현하고 `regulator_summary`·`/sys/kernel/debug/gpio`·ftrace로 진단할 수 있다 · dt-schema가 잡는 것과 못 잡는 것을 구분해 리뷰할 수 있다
> **왜 필요한가**: bring-up 첫 주의 일은 "HW 팀이 준 회로도 PDF + SoC 레퍼런스 매뉴얼 → 보드 `.dts`"다. 면접에서는 "새 센서 보드를 받으면 DT를 어떻게 쓰나요?", "reset 핀이 active-low인데 DT와 드라이버에 각각 뭐라고 쓰나요?"로 나온다.
> **Don 기준 난이도**: 회로도 읽기·scope로 핀 확인은 이미 잘한다(Apple RF bring-up) / 새로 배울 것은 "그 정보를 어떤 DT 프로퍼티와 커널 프레임워크가 받는지"와 극성이 어느 층에서 뒤집히는지
> **선행 노트**: D2(DT 문법), D4(`*-gpios`·`*-supply`·`interrupts`·`pinctrl` 자원 문법), D6(overlay), D7(binding·dt-schema), H3(clk·reset·regulator), H4(pinctrl·GPIO), H5(I2C), P1(bring-up 순서), P3(데이터시트 → 드라이버)

---

## 0. 큰 그림 — 회로도는 "무엇이 어디에 어떻게 붙었나", DT는 그 번역본

D 모듈에서 DT **문법**은 다 배웠다. 이 노트는 **번역** 기술이다. 회로도 PDF 한 장을 펴 놓고 "이 선은 DT의 어느 줄이 되나"를 기계적으로 결정하는 방법이다.

bare-metal 시절을 떠올리면 쉽다. Don이 SSD 펌웨어에서 `board_config.h`에 `#define NAND_CE0_PIN 12`, `#define PWR_EN_ACTIVE_LOW 1`을 적던 일이 DT의 원형이다. 차이는 두 가지다.

- Linux에서는 이 정보를 **드라이버 코드가 아니라 DT**에 적는다. 같은 `ov5640.ko`가 i.MX8MM EVK에서도, 우리 보드에서도 돈다 (D1).
- 정보의 **소비자가 여러 프레임워크**로 나뉜다. 핀 먹스는 pinctrl, 전원은 regulator, 리셋 핀은 gpiolib, 인터럽트 라인은 irqchip, 클럭은 CCF가 받는다. 그래서 회로도의 선 하나를 보고 "이건 어느 프레임워크 몫인가"를 먼저 판단해야 한다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="140" y="22" text-anchor="middle" font-size="13">회로도에서 보이는 것</text>
  <text x="510" y="22" text-anchor="middle" font-size="13">DT에 쓰는 것 (받는 프레임워크)</text>
  <rect x="20" y="34" width="240" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="140" y="55" text-anchor="middle" font-size="12">SoC ball C14 = 패드 SAI1_RXD0</text>
  <rect x="340" y="34" width="320" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="55" text-anchor="middle" font-size="12">pinctrl-0 → fsl,pins 먹스+패드값 (pinctrl)</text>
  <rect x="20" y="76" width="240" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="140" y="97" text-anchor="middle" font-size="12">풀업 저항 R12 4.7k, open-drain</text>
  <rect x="340" y="76" width="320" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="97" text-anchor="middle" font-size="12">패드 PUE/PE 비트, drive-open-drain</text>
  <rect x="20" y="118" width="240" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="140" y="139" text-anchor="middle" font-size="12">IMU_INT1_N (active-low)</text>
  <rect x="340" y="118" width="320" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="139" text-anchor="middle" font-size="12">interrupts-extended + IRQ_TYPE (irqchip)</text>
  <rect x="20" y="160" width="240" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="140" y="181" text-anchor="middle" font-size="12">CAM_RST_N, CAM_PWDN</text>
  <rect x="340" y="160" width="320" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="181" text-anchor="middle" font-size="12">reset-gpios ACTIVE_LOW 등 (gpiolib)</text>
  <rect x="20" y="202" width="240" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="140" y="223" text-anchor="middle" font-size="12">LDO U5 + EN 핀 = IMU_PWR_EN</text>
  <rect x="340" y="202" width="320" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="223" text-anchor="middle" font-size="12">regulator-fixed + gpio (regulator)</text>
  <rect x="20" y="244" width="240" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="140" y="265" text-anchor="middle" font-size="12">칩의 VDD / VDDIO 핀</text>
  <rect x="340" y="244" width="320" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="265" text-anchor="middle" font-size="12">vdd-supply, vddio-supply (regulator)</text>
  <rect x="20" y="286" width="240" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="140" y="307" text-anchor="middle" font-size="12">SDO 핀 = GND (주소 strap)</text>
  <rect x="340" y="286" width="320" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="307" text-anchor="middle" font-size="12">reg = &lt;0x68&gt; + 노드 이름 imu@68 (I2C core)</text>
  <rect x="20" y="328" width="240" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="140" y="349" text-anchor="middle" font-size="12">Y2 24 MHz 오실레이터</text>
  <rect x="340" y="328" width="320" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="349" text-anchor="middle" font-size="12">fixed-clock + clocks/clock-names (CCF)</text>
  <line x1="262" y1="50" x2="336" y2="50" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="262" y1="92" x2="336" y2="92" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="262" y1="134" x2="336" y2="134" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="262" y1="176" x2="336" y2="176" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="262" y1="218" x2="336" y2="218" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="262" y1="260" x2="336" y2="260" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="262" y1="302" x2="336" y2="302" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="262" y1="344" x2="336" y2="344" stroke="currentColor" marker-end="url(#ah1)"/>
</svg>
```

그림 1 — 회로도 요소 8종과 DT 표현. 왼쪽을 읽는 능력이 이 노트의 앞절(§1), 오른쪽으로 옮기는 규칙이 §2, 세 개의 실제 조각이 §3–§5, QEMU에서 확인하는 것이 §7–§8이다.

이 노트의 산출물은 세 가지다.

1. **회로도 읽기 체크리스트** (§1.9) — HW 리뷰 미팅에 들고 갈 표.
2. **net → property 번역표** (§2) — 각 조각마다 한 장씩 채운다.
3. **QEMU 위 가상 보드** (§7) — gpio-sim을 "SoC GPIO 뱅크"로, i2c-gpio를 "I2C3"로, `regulator-fixed` 다섯 개를 LDO·로드 스위치로 삼아, 실제 커널 드라이버(`ov5640`, `bmi160_i2c`, `phy-can-transceiver`)가 우리가 쓴 DT를 읽고 **핀을 어떤 순서로 흔드는지** ftrace로 본다.

실행 환경: Lima VM `elinux`(Ubuntu 26.04, 호스트 커널 7.0.0-38-generic)에서 dtc 1.7.2·dt-schema 2026.9를 돌리고, 게스트는 QEMU `virt`(TCG) + 랩 커널 `6.18.55-dom` + BusyBox initramfs다. 시간 값은 전부 **TCG 에뮬레이션 + 19명이 같이 쓰는 VM**에서 잰 것이라 실제 하드웨어 타이밍이 아니다.

---

## 1. SW 엔지니어의 회로도 읽기

### 1.1 회로도 PDF의 구조 — 어디부터 펴나

보드 회로도(schematic)는 보통 30~80페이지다. SW가 다 읽을 필요는 없다. 순서는 이렇다.

| 페이지 | 무엇을 찾나 | DT에서 쓰는 곳 |
|---|---|---|
| 블록 다이어그램 (1–2쪽) | 어떤 칩이 어떤 버스에 붙었나 (I2C3에 IMU·카메라, SPI1에 CAN) | 노드 트리 구조, 버스 부모 |
| 전원 트리 (power tree) | PMIC 레일·LDO·로드 스위치, 각각의 EN 핀과 입력 | `regulator-*` 노드, `vin-supply` |
| SoC 심볼 페이지 (여러 장) | SoC ball 번호 ↔ net 이름 | `pinctrl` 그룹, `*-gpios` 번호 |
| 주변장치 페이지 | 칩 핀 ↔ net, 풀업, strap 저항, DNP 표시 | 노드 속성 대부분 |
| 커넥터 페이지 | 카메라·CAN 커넥터 핀아웃, 레벨 시프터 | 극성, 전압 도메인 |
| 개정 이력 (마지막) | rev A → B에서 바뀐 핀 | 보드 변형 dts (D5) |

**DNP**(Do Not Populate)는 "설계에는 있지만 실장 안 함"이다. 풀업 자리에 DNP가 붙어 있으면 풀업이 **없다**. 이 표시 하나를 놓쳐서 I2C가 안 뜨는 일이 흔하다 (§8.4).

### 1.2 net 이름 규칙 — 이름이 극성과 전압을 말해 준다

net은 "전기적으로 연결된 선 묶음"이다. 회로도 툴은 net에 이름을 붙이고, 페이지를 넘어가는 연결은 이름(off-page connector)으로만 이어진다. SW는 이름만으로 많은 것을 안다.

| 이름 패턴 | 뜻 | 예 | DT에 미치는 영향 |
|---|---|---|---|
| `_N`, `_B`, `_L`, `#`, 윗줄(overbar) | active-low | `CAM_RST_N`, `RESETB`, `INT#` | `GPIO_ACTIVE_LOW`, `IRQ_TYPE_LEVEL_LOW`/`EDGE_FALLING` |
| `_EN`, `_OE` | enable (보통 active-high) | `IMU_PWR_EN`, `IMU_LS_OE` | `enable-active-high`, hog `output-high` |
| `_1V8`, `_3V3`, `VDDIO_x` | 전압 도메인 | `I2C3_SDA_1V8` | 레벨 시프터가 사이에 있는지 확인 |
| `_P`/`_N` 쌍 | 차동쌍 (MIPI, CAN, USB) | `CSI_D0_P/N` | 극성 아님! lane 번호만 본다 |
| `_STB`, `_SLP`, `_PD` | 대기·절전 | `CAN_STB`, `CAM_PWDN` | 칩 데이터시트에서 "1 = 대기"인지 확인 |
| SoC 패드 이름 그대로 | 아직 기능 이름을 안 붙인 핀 | `GPIO1_IO05` | 회로도 작성자에게 용도 물어보기 |

주의: `_N` 접미사는 **차동쌍의 음극**에도 쓴다(`CAN_N`이 아니라 `CANL`을 쓰는 이유). `CSI_CLK_N`은 active-low가 아니다. 문맥으로 구분한다.

### 1.3 SoC ball → 패드 → 기능: 핀 먹스

회로도의 SoC 심볼에는 **ball 번호**(BGA 좌표, 예: `C14`)와 **패드 이름**(예: `SAI1_RXD0`)이 같이 적혀 있다. 패드 하나는 ALT0~ALT7 여러 기능 중 하나가 된다. 어느 기능을 쓸지는 회로도 net 이름이 알려 주고(`IMU_INT1` → GPIO 기능이어야 함), 그 번호를 SoC 레퍼런스 매뉴얼(RM)의 IOMUXC 장에서 찾는다.

Linux는 이 표를 **pinfunc 헤더**로 들고 있다. 예제 1은 i.MX8MP 패드 `SAI1_RXD0`가 될 수 있는 기능을 커널 트리에서 찾는다.

**예제 1 — 무엇을 확인하나**: 패드 하나의 ALT 기능 목록과 매크로 5개 숫자의 의미.

```sh
limactl shell elinux -- bash -lc 'grep -E "MX8MP_IOMUXC_SAI1_RXD0__" \
  /var/lab/src/linux-6.18.55/arch/arm64/boot/dts/freescale/imx8mp-pinfunc.h'
```

```text
#define MX8MP_IOMUXC_SAI1_RXD0__AUDIOMIX_SAI1_RX_DATA00              0x150 0x3B0 0x000 0x0 0x0
#define MX8MP_IOMUXC_SAI1_RXD0__AUDIOMIX_SAI6_TX_BCLK                0x150 0x3B0 0x4A8 0x1 0x0
#define MX8MP_IOMUXC_SAI1_RXD0__AUDIOMIX_PDM_BIT_STREAM00            0x150 0x3B0 0x5C4 0x3 0x1
#define MX8MP_IOMUXC_SAI1_RXD0__GPIO4_IO02                           0x150 0x3B0 0x000 0x5 0x0
#define MX8MP_IOMUXC_SAI1_RXD0__ENET1_1588_EVENT1_IN                 0x150 0x3B0 0x000 0x6 0x0
#define MX8MP_IOMUXC_SAI1_RXD0__ENET_QOS_1588_EVENT1_IN              0x150 0x3B0 0x000 0x6 0x0
```

출력에서 볼 것: 다섯 숫자는 `mux_reg conf_reg input_reg mux_val input_val`이다(커널 binding `fsl,imx8m-pinctrl.yaml`의 설명). 같은 패드라 `mux_reg`(0x150)·`conf_reg`(0x3B0)가 같고 `mux_val`(ALT 번호)만 다르다. 우리 INT 핀은 ALT5 = `GPIO4_IO02`, 즉 DT에서는 `<&gpio4 2 ...>`가 된다. **패드 이름(SAI1_RXD0)과 GPIO 번호(GPIO4_IO02)는 다른 이름 체계**라는 것이 첫 번째 함정이다 — 회로도에는 앞의 것만, DT의 `*-gpios`에는 뒤의 것이 들어간다.

TI AM62x는 같은 역할을 `AM62X_IOPAD(오프셋, 설정, 모드)` 매크로로, Rockchip은 `rockchip,pins = <뱅크 핀 RK_FUNC_x &pcfg_pull_up>`로 한다. 형식만 다르고 정보(어느 패드, 어느 기능, 어떤 패드 설정)는 같다 (H4 §2).

### 1.4 풀업·풀다운, open-drain — "아무도 안 끌 때 선은 몇 V인가"

open-drain(OD) 출력은 "끌어내리기만 하고 올리지는 못하는" 출력이다. 트랜지스터가 켜지면 0 V, 꺼지면 선이 **떠 있다**(high-Z). 그래서 OD 선에는 반드시 풀업 저항이 있어야 1이 된다. I2C의 SDA/SCL, 대부분 센서의 INT 핀(설정에 따라), PMIC의 `IRQ_N`이 OD다.

회로도에서 확인할 것:

- 풀업 저항이 **어느 레일에** 붙었나 (1.8 V? 3.3 V?) — 이것이 그 선의 high 전압이다.
- 풀업이 실장되나 (DNP 아님), 값은 (I2C 400 kHz면 보통 2.2k~4.7k).
- 풀업이 보드에 없으면 SoC 내부 풀업(수십 kΩ)에 기대는지 — 그러면 **pinctrl 패드 설정에 PUE/PE 비트**를 켜야 한다. 내부 풀업은 I2C에는 너무 약하다(상승 시간이 길다).

### 1.5 레벨 시프터 — 전압 도메인을 넘을 때

IMU의 VDDIO가 1.8 V인데 SoC의 I2C3 패드 뱅크가 3.3 V라면 사이에 레벨 시프터가 있다. SW에게 중요한 질문은 셋이다.

1. **극성을 뒤집나?** PCA9306/TXS0102 같은 양방향 패스 트랜지스터형은 안 뒤집는다. 트랜지스터 한 개로 만든 단방향 open-collector 버퍼는 **뒤집는다**. 뒤집으면 칩의 active-low INT가 SoC에서는 active-high로 보인다 → DT의 `IRQ_TYPE`은 **SoC 핀에서 보이는 극성**으로 쓴다.
2. **EN/OE 핀이 있나?** 있으면 누군가 켜야 한다 — GPIO hog, 레귤레이터의 일부, 또는 드라이버.
3. **양쪽 레일이 다 살아 있어야 동작하나?** 대부분 그렇다. IMU 레일이 꺼져 있으면 시프터가 SoC 쪽까지 선을 이상하게 잡을 수 있다(버스 전체가 low로 붙음).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="40" width="150" height="110" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="95" y="62" text-anchor="middle" font-size="13">BMI160 (1.8 V)</text>
  <text x="95" y="84" text-anchor="middle" font-size="12">INT1 = open-drain</text>
  <text x="95" y="104" text-anchor="middle" font-size="12">active-low</text>
  <line x1="95" y1="115" x2="95" y2="140" stroke="currentColor"/>
  <text x="100" y="135" font-size="12">끌어내림만</text>
  <rect x="270" y="60" width="140" height="70" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="88" text-anchor="middle" font-size="12">레벨 시프터 U4</text>
  <text x="340" y="108" text-anchor="middle" font-size="12">비반전, OE 핀</text>
  <rect x="510" y="40" width="150" height="110" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="585" y="62" text-anchor="middle" font-size="13">SoC (3.3 V)</text>
  <text x="585" y="84" text-anchor="middle" font-size="12">GPIO4_IO02</text>
  <text x="585" y="104" text-anchor="middle" font-size="12">입력 + IRQ</text>
  <line x1="170" y1="95" x2="268" y2="95" stroke="currentColor"/>
  <line x1="410" y1="95" x2="508" y2="95" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="220" y1="95" x2="220" y2="30" stroke="currentColor"/>
  <rect x="212" y="40" width="16" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="236" y="28" font-size="12">4.7k → 1V8</text>
  <line x1="460" y1="95" x2="460" y2="30" stroke="currentColor"/>
  <rect x="452" y="40" width="16" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="380" y="28" font-size="12">10k → 3V3</text>
  <text x="20" y="190" font-size="12">SoC 핀에서 본 파형</text>
  <path d="M140,205 L330,205 L330,245 L430,245 L430,205 L640,205" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="140" y="198" font-size="12">대기 = 1 (풀업 덕분)</text>
  <text x="335" y="265" font-size="12">이벤트: 칩이 끌어내림</text>
  <line x1="330" y1="280" x2="330" y2="250" stroke="#d0564a" marker-end="url(#ah2)"/>
  <text x="20" y="292" font-size="12">→ DT: IRQ_TYPE_EDGE_FALLING (또는 LEVEL_LOW) + drive-open-drain</text>
</svg>
```

그림 2 — open-drain INT가 비반전 레벨 시프터를 지나 SoC에 닿는 경로. 풀업이 양쪽 도메인에 하나씩 있고, SoC 핀에서 본 극성(대기 1, 이벤트 0)이 DT의 `IRQ_TYPE`을 정한다. 시프터가 반전형이었다면 `EDGE_RISING`이 맞다.

### 1.6 enable·reset 핀과 그 극성

칩 데이터시트의 핀 표에서 "RESETB: active low, internal pull-up 100k" 같은 줄을 찾는다. 이 한 줄에 세 가지 정보가 있다.

- **극성**: active-low → DT 플래그 `GPIO_ACTIVE_LOW`.
- **기본 상태**: 칩 내부 풀업 → SoC가 아무것도 안 하면 리셋 해제 상태. 보드에 외부 풀다운이 있으면 반대로 "기본은 리셋 유지"다. 부팅 중(드라이버 로드 전) 칩이 어떤 상태로 있는지 결정한다.
- **누가 움직이나**: 드라이버가 `reset-gpios`를 받아 시퀀스를 돈다. 아무 드라이버도 안 쓰는 핀(예: 레벨 시프터 OE, 오실레이터 enable)은 **GPIO hog**로 부팅 때 고정한다 (H4 §1.2).

중요한 규칙 (D4 §8 복습): **DT는 물리 극성을, 드라이버는 논리값을** 책임진다. 드라이버는 항상 `gpiod_set_value(reset, 1)` = "리셋을 건다"로 쓰고, 그게 0 V인지 3.3 V인지는 DT 플래그가 정한다. 그래서 DT 작성자가 극성을 틀리면 드라이버는 정확히 **반대 동작**을 한다.

### 1.7 레귤레이터, 전원 도메인, 전원 트리

전원 트리 페이지는 "PMIC BUCK2 3.3 V → 로드 스위치 U9(EN = `CAN_5V_EN`) → TCAN1042 VCC" 같은 그래프다. SW는 각 레일마다 다음을 뽑는다.

| 항목 | 어디서 | DT 속성 |
|---|---|---|
| 레일 이름 | 회로도 net 이름 | `regulator-name` (회로도 이름 그대로!) |
| 전압 | LDO 출력 설정 저항·부품 번호 | `regulator-min/max-microvolt` (같은 값) |
| EN 핀과 극성 | LDO 데이터시트 (EN high = on이 대부분) | `gpio = <...>` + `enable-active-high` |
| 켜진 뒤 안정 시간 | LDO 데이터시트 t_on, soft-start | `startup-delay-us` |
| 끈 뒤 다시 켜기까지 | 출력 커패시터 방전 | `off-on-delay-us` |
| 입력 레일 | 전원 트리의 위쪽 | `vin-supply` |
| SW가 못 끄는 레일 | EN이 풀업에 묶임, PMIC 기본 on | `regulator-always-on`, `regulator-boot-on` |

전원 도메인이 중요한 이유는 **켜는 순서**다. 카메라 센서 데이터시트는 "DOVDD → AVDD → DVDD 순서, 각 레일 안정 후 PWDN 해제" 같은 시퀀스를 요구한다. DT로 이 순서를 표현하는 방법은 두 가지뿐이다: (1) `vin-supply` 체인(부모가 먼저 켜진다), (2) 드라이버가 순서대로 켠다. **`regulator_bulk_enable()`은 순서를 보장하지 않는다** — §7.5에서 실측으로 본다.

### 1.8 I2C 주소 strap과 오실레이터

**주소 strap**: 많은 I2C 칩은 핀 하나(BMI160은 SDO, MPU-6050은 AD0)를 GND/VDDIO에 묶어 주소 LSB를 정한다. BMI160은 SDO = GND → 0x68, SDO = VDDIO → 0x69 (데이터시트 기반 — 기억, 데이터시트로 확인 필요). 같은 칩 두 개를 한 버스에 달 때 이 strap으로 구분한다. DT에서는 `reg = <0x68>`과 노드 이름 `imu@68`.

strap 핀이 SoC GPIO에 물려 있고 **리셋 해제 순간에 샘플링**되는 칩도 있다(이더넷 PHY의 PHYAD strap이 대표적). 이 경우 GPIO hog나 드라이버가 리셋 해제 **전에** 레벨을 맞춰 놓아야 한다.

**오실레이터**: 회로도에 `Y2 24MHz` 같은 부품이 있고 출력이 칩의 XCLK로 간다. SW는 (1) 주파수, (2) 오실레이터 자체의 enable(OE) 핀 유무, (3) SoC 클럭 출력(`CLKO1`)을 쓰는지 독립 오실레이터인지를 본다. 독립 오실레이터면 `fixed-clock`, OE가 GPIO면 `gpio-gate-clock`(커널 `drivers/clk/clk-gpio.c`, 이 랩 커널에는 `CONFIG_COMMON_CLK_GPIO`가 꺼져 있어 hog로 대신한다), SoC 출력이면 `clocks = <&clk IMX8MP_CLK_CLKOUT1>` + `assigned-clock-rates`다.

### 1.9 회로도 읽기 체크리스트 — 칩 하나당 한 줄씩 채운다

| 칩 | 버스·주소 | 전원 핀 → 레일 (EN 핀) | reset/enable 핀 (극성, 기본값) | IRQ 핀 (OD?, 극성, 시프터) | 클럭 | strap | 패드 설정 |
|---|---|---|---|---|---|---|---|
| BMI160 U3 | I2C3 0x68 | VDD·VDDIO → VDD_IMU_1V8 (IMU_PWR_EN) | 없음 | INT1 OD, low, U4 비반전 | 내부 | SDO=GND | INT 풀업 |
| OV5640 J4 | I2C3 0x3c, CSI 2-lane | DOVDD/AVDD/DVDD → 3 LDO (각 EN) | RESETB low, PWDN high | 없음 | Y2 24 MHz (OE) | 없음 | — |
| TCAN1042 U10 | CAN1 TX/RX | VCC 5 V (CAN_5V_EN), VIO 3.3 V | STB high = 대기, 10k 풀업 | 없음 | — | — | STB 풀업 |

이 표가 다음 절의 번역표 입력이다. HW 엔지니어와의 회의는 이 표의 빈칸을 채우는 시간이라고 생각하면 된다.

---

## 2. 번역 규칙 — net → property 사전

| 회로도에서 본 것 | DT 표현 | 받는 쪽 (프레임워크·함수) | 함정 |
|---|---|---|---|
| 칩이 I2C 버스 X의 주소 A | 버스 노드 아래 `name@A { reg = <A>; }` | I2C core가 `i2c_client` 생성 (H5) | 8-bit 주소(0xD0)를 쓰면 안 됨, 7-bit(0x68) |
| SPI CS n | `name@n { reg = <n>; spi-max-frequency }` | SPI core (H7) | CS가 GPIO면 컨트롤러에 `cs-gpios` |
| SoC 패드 기능 선택 | `pinctrl-names = "default"; pinctrl-0 = <&grp>;` + 그룹 | pinctrl, probe 직전 적용 (H4) | 그룹을 **소비자 노드**에 걸어야 적용된다 |
| 보드 풀업 없음, 내부 풀업 사용 | 패드 설정값의 PE/PUE 비트, 또는 `bias-pull-up` | pinctrl conf | I2C에는 내부 풀업이 약하다 |
| GPIO 출력 (reset, enable, standby) | `<기능>-gpios = <&bank n FLAGS>` | gpiolib `devm_gpiod_get(dev, "기능")` | 이름은 **binding이 정한다** (`reset`, `powerdown`, `standby`) |
| active-low 핀 | FLAGS = `GPIO_ACTIVE_LOW` | gpiolib가 논리값 반전 | 드라이버에서 또 뒤집으면 이중 반전 |
| 아무 드라이버도 안 쓰는 고정 핀 | GPIO hog: `gpio-hog; gpios; output-high;` | gpiolib가 칩 등록 때 | hog한 핀은 다른 소비자가 못 가져간다 |
| 인터럽트 라인 → SoC GPIO | `interrupts-extended = <&bank n IRQ_TYPE_x>` | irqchip `xlate` → `irq_set_type` | 타입은 **SoC 핀에서 본** 극성 |
| 칩 INT가 OD | `drive-open-drain` (binding에 있을 때) | 드라이버가 칩 레지스터 설정 | OD인데 풀업이 없으면 영원히 0 |
| LDO/로드 스위치 + EN GPIO | `regulator-fixed` + `gpio` + `enable-active-high` | `drivers/regulator/fixed.c` | `enable-active-high` 빠지면 **강제 active-low** |
| 칩 전원 핀 | `<이름>-supply = <&reg>` | regulator core `devm_regulator_get(dev, "이름")` | 이름 대소문자도 binding 그대로 (`DOVDD-supply`) |
| 레일의 입력 | `vin-supply` | 부모 먼저 켜짐 | 빠뜨리면 순서·use count가 틀어짐 |
| 오실레이터 | `fixed-clock` + 소비자 `clocks`/`clock-names` | CCF `clk_get(dev, "xclk")` | `clock-names` 철자는 binding이 정함 |
| 버스 번호를 고정하고 싶다 | `/aliases { i2c3 = &i2c3; }` | I2C core가 adapter 번호로 사용 | alias 없으면 probe 순서대로 번호 |
| 실장 안 됨 (DNP, 옵션 보드) | `status = "disabled"` 또는 overlay | of_platform이 장치를 안 만듦 | SoC dtsi는 기본 disabled, 보드에서 `"okay"` |

`<기능>-gpios`·`<이름>-supply`의 **이름**은 내가 정하는 것이 아니다. 드라이버가 `devm_gpiod_get(dev, "reset", ...)`으로 찾는 문자열이고, binding YAML에 적혀 있다. 그래서 번역의 마지막 단계는 언제나 binding과 드라이버 확인이다.

**예제 2 — 무엇을 확인하나**: 우리 세 드라이버가 DT에서 찾는 이름(con_id)을 소스에서 직접 뽑는다. 회로도 net `CAM_PWDN`이 DT에서 `powerdown-gpios`가 되는 근거가 여기 있다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55; grep -nE \
 "devm_(gpiod_get(_optional)?|regulator_get|regulator_bulk_get|clk_get|v4l2_sensor_clk_get|phy_get)\(|\"DOVDD\"|\"AVDD\"|\"DVDD\"|fwnode_irq_get_byname" \
 drivers/media/i2c/ov5640.c drivers/iio/imu/bmi160/bmi160_core.c drivers/phy/phy-can-transceiver.c | sed "s#drivers/##"'
```

```text
media/i2c/ov5640.c:354:	"DOVDD", /* Digital I/O (1.8V) supply */
media/i2c/ov5640.c:355:	"AVDD",  /* Analog (2.8V) supply */
media/i2c/ov5640.c:356:	"DVDD",  /* Digital Core (1.5V) supply */
media/i2c/ov5640.c:3818:	return devm_regulator_bulk_get(&sensor->i2c_client->dev,
media/i2c/ov5640.c:3898:	sensor->xclk = devm_v4l2_sensor_clk_get(dev, "xclk");
media/i2c/ov5640.c:3912:	sensor->pwdn_gpio = devm_gpiod_get_optional(dev, "powerdown",
media/i2c/ov5640.c:3918:	sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset",
iio/imu/bmi160/bmi160_core.c:643:	irq = fwnode_irq_get_byname(fwnode, "INT1");
iio/imu/bmi160/bmi160_core.c:649:	irq = fwnode_irq_get_byname(fwnode, "INT2");
iio/imu/bmi160/bmi160_core.c:843:	ret = devm_regulator_bulk_get(dev,
phy/phy-can-transceiver.c:151:		standby_gpio = devm_gpiod_get_optional(dev, "standby", GPIOD_OUT_HIGH);
phy/phy-can-transceiver.c:158:		enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
```

출력에서 볼 것: 회로도의 `CAM_PWDN` → `"powerdown"` → DT `powerdown-gpios`, `CAN_STB` → `"standby"` → `standby-gpios`. 접미사 `-gpios`/`-supply`는 프레임워크가 붙인다(`gpiod_get(dev, "reset")`은 `reset-gpios`와 옛 이름 `reset-gpio`를 찾는다). OV5640은 전원 이름이 **대문자**(`DOVDD-supply`)이고, BMI160은 IRQ를 `interrupt-names`의 `"INT1"`/`"INT2"`로 고른다 — 둘 다 회로도에 적힌 칩 핀 이름을 binding이 그대로 가져온 것이다. 또 `GPIOD_OUT_HIGH`는 "논리 1로 시작" = standby를 **건 채로** 시작한다는 뜻이다(§5).

---

## 3. 조각 A — I2C IMU + open-drain INT + 레벨 시프터

### 3.1 회로도

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="150" height="250" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="50" text-anchor="middle" font-size="13">SoC (VDD 3.3 V)</text>
  <text x="30" y="84" font-size="12">I2C3_SCL</text>
  <text x="30" y="114" font-size="12">I2C3_SDA</text>
  <text x="30" y="154" font-size="12">GPIO4_IO02</text>
  <text x="30" y="204" font-size="12">GPIO1_IO05</text>
  <text x="30" y="254" font-size="12">GPIO1_IO03</text>
  <rect x="250" y="60" width="120" height="110" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="310" y="80" text-anchor="middle" font-size="12">U4 PCA9306</text>
  <text x="310" y="98" text-anchor="middle" font-size="12">3V3 ↔ 1V8</text>
  <text x="310" y="160" text-anchor="middle" font-size="12">EN</text>
  <rect x="500" y="30" width="160" height="200" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="580" y="50" text-anchor="middle" font-size="13">U3 BMI160</text>
  <text x="650" y="84" text-anchor="end" font-size="12">SCX</text>
  <text x="650" y="114" text-anchor="end" font-size="12">SDX</text>
  <text x="650" y="154" text-anchor="end" font-size="12">INT1 (OD)</text>
  <text x="650" y="184" text-anchor="end" font-size="12">SDO → GND</text>
  <text x="650" y="214" text-anchor="end" font-size="12">VDD, VDDIO</text>
  <line x1="170" y1="80" x2="250" y2="80" stroke="currentColor"/>
  <line x1="170" y1="110" x2="250" y2="110" stroke="currentColor"/>
  <line x1="370" y1="80" x2="500" y2="80" stroke="currentColor"/>
  <line x1="370" y1="110" x2="500" y2="110" stroke="currentColor"/>
  <text x="180" y="74" font-size="12">I2C3_SCL</text>
  <text x="380" y="74" font-size="12">IMU_SCL_1V8</text>
  <line x1="500" y1="150" x2="370" y2="150" stroke="currentColor"/>
  <line x1="250" y1="150" x2="170" y2="150" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="380" y="144" font-size="12">IMU_INT1_N</text>
  <text x="174" y="144" font-size="12">INT1_3V3</text>
  <line x1="170" y1="250" x2="310" y2="250" stroke="currentColor"/>
  <line x1="310" y1="250" x2="310" y2="170" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="180" y="244" font-size="12">IMU_LS_OE</text>
  <rect x="250" y="285" width="120" height="60" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="310" y="306" text-anchor="middle" font-size="12">U5 LDO 1.8 V</text>
  <text x="310" y="324" text-anchor="middle" font-size="12">IN 3V3 · EN</text>
  <line x1="170" y1="200" x2="220" y2="200" stroke="currentColor"/>
  <line x1="220" y1="200" x2="220" y2="315" stroke="currentColor"/>
  <line x1="220" y1="315" x2="248" y2="315" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="176" y="194" font-size="12">IMU_PWR_EN</text>
  <line x1="370" y1="315" x2="580" y2="315" stroke="currentColor"/>
  <line x1="580" y1="315" x2="580" y2="232" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="400" y="308" font-size="12">VDD_IMU_1V8</text>
  <text x="378" y="200" font-size="12">R 4.7k ×3 → 1V8</text>
  <text x="180" y="290" font-size="12">R 4.7k ×3 → 3V3</text>
</svg>
```

그림 3 — 조각 A. IMU는 1.8 V 도메인, SoC I2C3는 3.3 V. PCA9306(양방향, 비반전)이 SCL·SDA·INT 세 선을 넘기고 양쪽에 풀업이 있다. LDO U5의 EN과 시프터의 EN이 SoC GPIO에 물렸다. SDO는 GND에 묶여 주소 0x68.

### 3.2 net → property 표

| net / 핀 | 회로도에서 읽은 사실 | DT 속성 | 어느 노드에 |
|---|---|---|---|
| I2C3_SCL/SDA | SoC I2C3 기능, 보드 풀업 4.7k | `pinctrl-0 = <&pinctrl_i2c3>`, `status = "okay"` | `&i2c3` |
| 버스 속도 | 시프터 PCA9306는 400 kHz 이상 OK | `clock-frequency = <400000>` | `&i2c3` |
| SDO = GND | 주소 0x68 | `imu@68 { reg = <0x68>; }` | `&i2c3`의 자식 |
| VDD, VDDIO → VDD_IMU_1V8 | U5 출력 하나가 두 핀에 | `vdd-supply`, `vddio-supply` = 같은 phandle | `imu@68` |
| U5 EN = IMU_PWR_EN = GPIO1_IO05 | LDO EN active-high, t_on ~ 수백 µs (기억 기반) | `regulator-fixed` + `gpio = <&gpio1 5 GPIO_ACTIVE_HIGH>` + `enable-active-high` + `startup-delay-us` | 새 `regulator-imu-1v8` 노드 |
| U5 IN = 3V3 | PMIC 레일 | `vin-supply = <&reg_3v3>` | `regulator-imu-1v8` |
| INT1 OD active-low → 비반전 시프터 → GPIO4_IO02 | SoC 핀에서 대기 1, 이벤트 0 | `interrupts-extended = <&gpio4 2 IRQ_TYPE_EDGE_FALLING>`, `interrupt-names = "INT1"`, `drive-open-drain` | `imu@68` |
| GPIO4_IO02 패드 | GPIO 기능(ALT5) + 입력, 보드 풀업 있음 | `pinctrl-0 = <&pinctrl_imu_int>` | `imu@68` |
| U4 EN = IMU_LS_OE | 드라이버가 없음, 항상 켜둠 | GPIO hog `output-high` | GPIO 뱅크 노드 |

`IRQ_TYPE_EDGE_FALLING`과 `LEVEL_LOW` 중 무엇을 고르나? 칩이 데이터 준비 때 짧은 펄스를 내면 edge, "읽을 때까지 유지(latch)"면 level이 안전하다. level은 드라이버가 상태 레지스터를 읽어 원인을 지울 때까지 IRQ가 계속 들어오므로 **놓치지 않는다**. edge는 공유 라인·빠른 재발생에서 놓칠 수 있다. bmi160 드라이버는 DT 타입을 읽어 칩의 INT 극성·latch 레지스터를 그에 맞게 프로그래밍한다(`bmi160_config_device_irq()`, inv_mpu6050도 같은 방식) — 즉 **DT의 IRQ 타입이 칩 설정까지 바꾼다.**

### 3.3 DT (i.MX8MP 보드라면)

이 조각을 실제 SoC dtsi 위에 쓴 것이 §6의 `imx_p4.dts`다. 핵심만 먼저 보면:

```dts
reg_imu_1v8: regulator-imu-1v8 {
	compatible = "regulator-fixed";
	regulator-name = "VDD_IMU_1V8";		/* 회로도 net 이름 그대로 */
	regulator-min-microvolt = <1800000>;
	regulator-max-microvolt = <1800000>;
	gpio = <&gpio1 5 GPIO_ACTIVE_HIGH>;	/* 플래그는 무시된다 (§8.1) */
	enable-active-high;			/* 극성은 이 한 줄이 정한다 */
	startup-delay-us = <2000>;
	vin-supply = <&reg_3v3>;
};

&i2c3 {
	clock-frequency = <400000>;
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_i2c3>;
	status = "okay";

	imu@68 {
		compatible = "bosch,bmi160";
		reg = <0x68>;				/* SDO = GND */
		pinctrl-names = "default";
		pinctrl-0 = <&pinctrl_imu_int>;
		vdd-supply = <&reg_imu_1v8>;
		vddio-supply = <&reg_imu_1v8>;
		interrupts-extended = <&gpio4 2 IRQ_TYPE_EDGE_FALLING>;
		interrupt-names = "INT1";
		drive-open-drain;
	};
};
```

`interrupts-extended`는 `interrupt-parent` + `interrupts` 두 줄을 한 줄로 합친 것이다(D4 §4). 부모(I2C 컨트롤러)의 인터럽트 컨트롤러와 다른 곳(GPIO 뱅크)으로 가는 선이므로 이쪽이 읽기 쉽다. pinctrl 그룹을 **IMU 노드에** 거는 이유: pinctrl은 "그 장치가 probe될 때" 상태를 적용한다. GPIO 컨트롤러 노드나 hog에 걸면 다른 시점에 적용된다(H4 §3).

---

## 4. 조각 B — 카메라 센서: 레일 3개 + reset + powerdown + 클럭

### 4.1 회로도

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="150" height="330" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="50" text-anchor="middle" font-size="13">SoC</text>
  <text x="30" y="84" font-size="12">GPIO1_IO04</text>
  <text x="30" y="134" font-size="12">GPIO1_IO05</text>
  <text x="30" y="184" font-size="12">GPIO1_IO06</text>
  <text x="30" y="234" font-size="12">GPIO1_IO07</text>
  <text x="30" y="264" font-size="12">GPIO1_IO08</text>
  <text x="30" y="294" font-size="12">GPIO1_IO11</text>
  <text x="30" y="324" font-size="12">I2C3, CSI 2-lane</text>
  <rect x="250" y="60" width="130" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="315" y="85" text-anchor="middle" font-size="12">U7 LDO 1.8 V</text>
  <rect x="250" y="110" width="130" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="315" y="135" text-anchor="middle" font-size="12">U8 LDO 2.8 V</text>
  <rect x="250" y="160" width="130" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="315" y="185" text-anchor="middle" font-size="12">U9 LDO 1.5 V</text>
  <rect x="250" y="275" width="130" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="315" y="300" text-anchor="middle" font-size="12">Y2 24 MHz OSC</text>
  <line x1="170" y1="80" x2="248" y2="80" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="170" y1="130" x2="248" y2="130" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="170" y1="180" x2="248" y2="180" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="170" y1="290" x2="248" y2="290" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="176" y="74" font-size="12">DOVDD_EN</text>
  <text x="176" y="124" font-size="12">AVDD_EN</text>
  <text x="176" y="174" font-size="12">DVDD_EN</text>
  <text x="176" y="284" font-size="12">OSC_EN</text>
  <rect x="490" y="30" width="170" height="330" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="575" y="50" text-anchor="middle" font-size="13">J4 OV5640 모듈</text>
  <text x="650" y="84" text-anchor="end" font-size="12">DOVDD</text>
  <text x="650" y="134" text-anchor="end" font-size="12">AVDD</text>
  <text x="650" y="184" text-anchor="end" font-size="12">DVDD</text>
  <text x="650" y="234" text-anchor="end" font-size="12">RESETB</text>
  <text x="650" y="264" text-anchor="end" font-size="12">PWDN</text>
  <text x="650" y="294" text-anchor="end" font-size="12">XCLK</text>
  <text x="650" y="324" text-anchor="end" font-size="12">SIOC/SIOD, MIPI</text>
  <line x1="380" y1="80" x2="488" y2="80" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="380" y1="130" x2="488" y2="130" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="380" y1="180" x2="488" y2="180" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="380" y1="295" x2="488" y2="295" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="170" y1="230" x2="488" y2="230" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="170" y1="260" x2="488" y2="260" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="170" y1="320" x2="488" y2="320" stroke="currentColor"/>
  <text x="390" y="224" font-size="12">CAM_RST_N</text>
  <text x="390" y="254" font-size="12">CAM_PWDN</text>
  <text x="390" y="74" font-size="12">CAM_DOVDD_1V8</text>
  <text x="390" y="124" font-size="12">CAM_AVDD_2V8</text>
  <text x="390" y="174" font-size="12">CAM_DVDD_1V5</text>
  <text x="390" y="289" font-size="12">CAM_XCLK</text>
  <line x1="315" y1="100" x2="315" y2="158" stroke="#d0564a" stroke-dasharray="4 3"/>
  <text x="200" y="215" font-size="12">U9 입력 = 1V8 (vin-supply)</text>
</svg>
```

그림 4 — 조각 B. LDO 세 개에 EN이 하나씩 있고, 센서 핀 RESETB(active-low)와 PWDN(active-high), 24 MHz 오실레이터(OE 핀 있음)가 SoC GPIO에 물렸다. 점선은 "U9의 입력이 U7 출력"이라는 전원 트리 관계다.

### 4.2 데이터시트 시퀀스 → 누가 책임지나

OV5640 드라이버 주석(소스 `ov5640_powerup_sequence()` 위, "From section 2.7 power up sequence")이 데이터시트 요구를 옮겨 적었다.

- `t0 + t1 + t2 >= 5ms`: DOVDD 안정 → PWDN 내림
- `t3 >= 1ms`: PWDN 내림 → RESETB 올림
- `t4 >= 20ms`: RESETB 올림 → SCCB(I2C) 사용 가능

이 시퀀스 중 **DT가 책임지는 것**은 "레일이 켜진 뒤 안정될 때까지"(`startup-delay-us`)와 "어느 GPIO가 어느 극성인가"뿐이다. 순서와 t2·t3·t4 지연은 **드라이버 코드**가 한다. 레일끼리의 순서(DOVDD → AVDD → DVDD)는 드라이버가 `regulator_bulk_enable()`로 한 번에 켜기 때문에 **보장되지 않는다** — 데이터시트가 레일 순서를 엄격히 요구하면 `vin-supply` 체인으로 강제하거나 드라이버를 고쳐야 한다. §7.5에서 실제로 순서가 매번 바뀌는 것을 본다.

### 4.3 net → property 표

| net / 핀 | 회로도에서 읽은 사실 | DT 속성 | 노드 |
|---|---|---|---|
| SIOC/SIOD → I2C3, 주소 | OV5640 SCCB 주소 0x3c (7-bit) | `camera@3c { reg = <0x3c>; }` | `&i2c3` 자식 |
| CAM_DOVDD_1V8 (U7, EN = GPIO1_IO04) | I/O 레일, 먼저 켜야 함 | `reg_cam_dovdd`: `regulator-fixed` + `gpio` + `enable-active-high` + `startup-delay-us` | 새 노드 |
| CAM_AVDD_2V8 (U8, EN = GPIO1_IO05) | 아날로그 레일 | `reg_cam_avdd` | 새 노드 |
| CAM_DVDD_1V5 (U9, EN = GPIO1_IO06), 입력 = 1V8 | 코어 레일 | `reg_cam_dvdd` + `vin-supply = <&reg_cam_dovdd>` | 새 노드 |
| 센서 전원 핀 | 이름은 binding 그대로 | `DOVDD-supply`, `AVDD-supply`, `DVDD-supply` | `camera@3c` |
| CAM_RST_N → RESETB | active-low | `reset-gpios = <&gpio1 7 GPIO_ACTIVE_LOW>` | `camera@3c` |
| CAM_PWDN → PWDN | active-high (1 = power down) | `powerdown-gpios = <&gpio1 8 GPIO_ACTIVE_HIGH>` | `camera@3c` |
| Y2 24 MHz → XCLK, OE = GPIO1_IO11 | 6~54 MHz 허용(드라이버 `OV5640_XCLK_MIN/MAX`) | `fixed-clock` + `clocks`/`clock-names = "xclk"`, OE는 hog (또는 `gpio-gate-clock`) | 새 노드 + `camera@3c` |
| MIPI CSI 2-lane | D0/D1 + CLK | `port { endpoint { data-lanes = <1 2>; clock-lanes = <0>; remote-endpoint; } }` | `camera@3c` + CSI 수신기 (Q2) |

`reset-gpios`의 플래그를 회로도 이름 `CAM_RST_N`의 `_N`이 정했다는 것을 보라. 반대로 `CAM_PWDN`에는 `_N`이 없고 데이터시트에서도 "PWDN high = power down"이므로 `GPIO_ACTIVE_HIGH`다. 드라이버는 두 핀을 똑같이 "1 = 건다"로 다룬다.

---

## 5. 조각 C — CAN 트랜시버 + standby 핀

### 5.1 회로도

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="150" height="240" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="50" text-anchor="middle" font-size="13">SoC (3.3 V I/O)</text>
  <text x="30" y="84" font-size="12">CAN1_TX</text>
  <text x="30" y="114" font-size="12">CAN1_RX</text>
  <text x="30" y="164" font-size="12">GPIO4_IO29</text>
  <text x="30" y="234" font-size="12">GPIO1_IO12</text>
  <rect x="300" y="50" width="150" height="160" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="375" y="70" text-anchor="middle" font-size="13">U10 TCAN1042</text>
  <text x="310" y="94" font-size="12">TXD</text>
  <text x="310" y="124" font-size="12">RXD</text>
  <text x="310" y="164" font-size="12">STB</text>
  <text x="310" y="194" font-size="12">VIO 3V3</text>
  <text x="440" y="104" text-anchor="end" font-size="12">CANH</text>
  <text x="440" y="134" text-anchor="end" font-size="12">CANL</text>
  <text x="440" y="194" text-anchor="end" font-size="12">VCC 5V</text>
  <line x1="170" y1="80" x2="298" y2="90" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="298" y1="120" x2="172" y2="110" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="170" y1="160" x2="298" y2="160" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="200" y="154" font-size="12">CAN_STB</text>
  <line x1="240" y1="160" x2="240" y2="40" stroke="currentColor"/>
  <rect x="232" y="50" width="16" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="180" y="36" font-size="12">10k → 3V3</text>
  <rect x="300" y="240" width="150" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="375" y="265" text-anchor="middle" font-size="12">U11 5 V 스위치</text>
  <line x1="170" y1="230" x2="250" y2="230" stroke="currentColor"/>
  <line x1="250" y1="230" x2="250" y2="260" stroke="currentColor"/>
  <line x1="250" y1="260" x2="298" y2="260" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="176" y="224" font-size="12">CAN_5V_EN</text>
  <line x1="450" y1="260" x2="480" y2="260" stroke="currentColor"/>
  <line x1="480" y1="260" x2="480" y2="196" stroke="currentColor"/>
  <line x1="480" y1="196" x2="452" y2="196" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="540" y="80" width="120" height="70" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="600" y="105" text-anchor="middle" font-size="12">J7 커넥터</text>
  <text x="600" y="125" text-anchor="middle" font-size="12">120 Ω (DNP?)</text>
  <line x1="450" y1="100" x2="540" y2="100" stroke="currentColor"/>
  <line x1="450" y1="130" x2="540" y2="130" stroke="currentColor"/>
  <text x="20" y="306" font-size="12">STB=1 대기, STB=0 정상 송수신 (데이터시트 기억 기반, 확인 필요)</text>
</svg>
```

그림 5 — 조각 C. TCAN1042의 STB 핀은 **high = standby**다. 보드 풀업 10k 덕분에 SoC가 핀을 잡기 전(부트로더·커널 초기)에는 트랜시버가 대기 상태로 버스를 방해하지 않는다. VCC 5 V는 로드 스위치 U11로 켠다. 종단 저항 120 Ω의 실장 여부(DNP)는 SW가 아니라 시스템 배선의 문제지만 리뷰 때 같이 묻는다.

### 5.2 net → property 표

| net / 핀 | 회로도에서 읽은 사실 | DT 속성 | 노드 |
|---|---|---|---|
| CAN1_TX/RX 패드 | SoC FlexCAN1 기능 | `pinctrl-0 = <&pinctrl_flexcan1>`, `status = "okay"` | `&flexcan1` |
| TCAN1042 존재 | generic CAN PHY 드라이버 대상 | `compatible = "ti,tcan1042"`, `#phy-cells = <0>` | 새 `can-phy0` |
| 최대 속도 | TCAN1042 변형별 2·5 Mbps (기억 기반) | `max-bitrate = <5000000>` | `can-phy0` |
| CAN_STB → STB (high = standby) | 기본 standby(풀업) | `standby-gpios = <&gpio4 29 GPIO_ACTIVE_HIGH>` | `can-phy0` |
| 컨트롤러 ↔ 트랜시버 | — | `phys = <&can_phy0>` | `&flexcan1` |
| VCC 5 V (U11, EN = CAN_5V_EN) | 트랜시버 전원 | `regulator-fixed` + `enable-active-high`, 컨트롤러에 `xceiver-supply` | 새 노드 + `&flexcan1` |

STB는 "standby를 건다 = 1"이므로 이름 그대로 active-high다. `phy-can-transceiver.c`는 probe에서 `GPIOD_OUT_HIGH`(standby 건 채로)로 받고, 컨트롤러가 `phy_power_on()`을 부르면(= `ip link set can0 up`) 논리 0(standby 해제), `phy_power_off()`(= `down`)에서 논리 1로 되돌린다. §7.6에서 trace로 확인한다.

### 5.3 같은 회로, 두 가지 DT 스타일 — 트리 안의 실제 보드

커널 트리에는 standby 핀을 표현하는 두 방식이 다 있다.

- **PHY 방식** (권장, 새 보드): `arch/arm64/boot/dts/freescale/imx93-phyboard-nash.dts`는 `flexcan1_tc: can-phy0 { compatible = "ti,tcan1042"; ... standby-gpios = <&gpio4 16 GPIO_ACTIVE_HIGH>; }`를 둔다.
- **레귤레이터 방식** (옛 관습): `imx8mp-evk.dts`는 `reg_can1_stby: regulator-can1-stby { compatible = "regulator-fixed"; gpio = <&gpio5 5 GPIO_ACTIVE_HIGH>; enable-active-high; }`를 만들고 `&flexcan1 { xceiver-supply = <&reg_can1_stby>; }`로 건다. 트랜시버 모드 핀을 "전원 스위치"로 취급하는 셈이다.

두 번째 방식은 FlexCAN 드라이버가 PHY 프레임워크를 지원하기 전부터 쓰던 우회로다. 레귤레이터 "켜짐 = GPIO high"가 트랜시버 입장에서 정상 모드인지 standby인지는 **그 보드의 트랜시버와 회로**에 달려 있다 — EVK의 트랜시버 핀 의미는 이 노트에서 확인하지 않았다. 새 보드라면 PHY 방식으로 쓰고, 기존 BSP를 읽을 때는 이 우회로를 알아보면 된다.

---

## 6. 실제 SoC 위에서 컴파일 — i.MX8MP 보드 조각 (컴파일·스키마만)

QEMU에는 i.MX8MP가 없으므로 이 절은 **컴파일과 dt-schema 검사까지만** 한다. 목적은 (1) 라벨(`&i2c3`, `&gpio4`, `&flexcan1`, `&iomuxc`)과 pinfunc 매크로가 실제 SoC dtsi에 존재하는지, (2) 우리 노드가 binding을 통과하는지 확인하는 것이다.

**예제 3 — 무엇을 확인하나**: 세 조각(IMU·CAN, 카메라는 지면상 생략)을 `imx8mp.dtsi` 위에 얹은 보드 파일. pinctrl 그룹이 소비자 노드에 붙는 모양을 본다.

```dts
// imx_p4.dts (뒷부분) — 앞부분은 §3.3의 regulator + can-phy0와 같다
&i2c3 {
	clock-frequency = <400000>;
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_i2c3>;
	status = "okay";
	imu@68 { /* §3.3과 동일, pinctrl-0 = <&pinctrl_imu_int> */ };
};
&flexcan1 {
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_flexcan1>;
	phys = <&can_phy0>;
	status = "okay";
};
&iomuxc {
	pinctrl_i2c3: i2c3grp { fsl,pins = <
		MX8MP_IOMUXC_I2C3_SCL__I2C3_SCL		0x400001c2
		MX8MP_IOMUXC_I2C3_SDA__I2C3_SDA		0x400001c2 >; };
	pinctrl_imu_int: imuintgrp { fsl,pins = <
		MX8MP_IOMUXC_SAI1_RXD0__GPIO4_IO02	0x1c0 >; };	/* PE+PUE+HYS */
	pinctrl_imu_en: imuengrp { fsl,pins = <
		MX8MP_IOMUXC_GPIO1_IO05__GPIO1_IO05	0x106 >; };	/* PE, 풀다운 */
	pinctrl_can_stb: canstbgrp { fsl,pins = <
		MX8MP_IOMUXC_SAI3_RXC__GPIO4_IO29	0x146 >; };	/* 풀업 = 기본 standby */
	pinctrl_flexcan1: flexcan1grp { fsl,pins = <
		MX8MP_IOMUXC_SAI2_TXC__CAN1_RX		0x154
		MX8MP_IOMUXC_SAI2_RXC__CAN1_TX		0x154 >; };
};
```

패드 설정값(여섯 번째 숫자)의 비트 의미는 커널 binding에 없고 RM에만 있다. 내 기억으로 i.MX8MP는 bit8 PE(pull enable), bit7 HYS, bit6 PUE(1 = 풀업), bit5 ODE, bit4 FSEL, bit2:1 DSE이고 `0x40000000`은 SION(입력 경로 강제)이다 — **기억 기반, RM 확인 필요**. 이 해석이면 EVK의 I2C 값 `0x400001c2`(커널 `imx8mp-evk.dts`의 `pinctrl_i2c3`에서 가져옴)은 "SION + PE + HYS + PUE + DSE"이다. 레귤레이터 EN 핀에 **풀다운**(`0x106`: PE만, PUE 0)을 주는 이유는 "SoC가 핀을 잡기 전에는 LDO가 꺼져 있게" 하기 위해서이고, STB에 풀업을 주는 이유는 반대로 "잡기 전에는 standby"이기 때문이다. 리셋 직후 패드의 기본 풀 상태도 RM에서 확인한다 — DT의 pinctrl은 **커널 probe 시점부터만** 효력이 있다.

빌드 스크립트(실제 SoC dtsi를 include하므로 `-I arch/arm64/boot/dts`):

```sh
#!/bin/sh
# build_imx.sh — i.MX8MP 조각을 실제 SoC dtsi 위에서 컴파일하고 스키마 검사
set -e
K=/var/lab/src/linux-6.18.55; O=/var/tmp/P4
cpp -nostdinc -undef -x assembler-with-cpp -I $K/arch/arm64/boot/dts \
    -I $K/include /Users/donh/.elinux_scratch/P4/imx_p4.dts -o $O/imx_p4.pp.dts
dtc -q -@ -I dts -O dtb -o $O/imx_p4.dtb $O/imx_p4.pp.dts
ls -l $O/imx_p4.dtb | awk '{print $5, $9}'
cd $O && ./venv/bin/dt-validate -s schema.json imx_p4.dtb 2>&1 \
  | grep -A1 -E "imx_p4.dtb: (/|imu|regulator-imu|can-phy0|i2c@30a40000|can@308c0000|pinctrl)" \
  || echo "(우리 노드 관련 경고 없음)"
```

`schema.json`은 커널 트리 binding 전체를 미리 합친 것이다(`dt-mk-schema -j -o schema.json Documentation/devicetree/bindings/`, 이 VM에서 1분 37초, 24.9 MB — D7 §3과 같은 과정을 `make` 없이 직접 돌림).

```sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/P4/build_imx.sh'
```

```text
57116 /var/tmp/P4/imx_p4.dtb
imx_p4.dtb: / (dons,p4-carrier): compatible: 'oneOf' conditional failed, one must be fixed:
	['dons,p4-carrier', 'fsl,imx8mp'] is too short
	imx_p4.dtb: / (dons,p4-carrier): compatible: 'oneOf' conditional failed, one must be fixed:
		['dons,p4-carrier', 'fsl,imx8mp'] is too short
--
	imx_p4.dtb: / (dons,p4-carrier): compatible: 'oneOf' conditional failed, one must be fixed:
		['dons,p4-carrier', 'fsl,imx8mp'] is too short
```

출력에서 볼 것: 컴파일이 통과했다 = 라벨과 매크로가 전부 실제로 있다. 남은 경고는 **루트 compatible**뿐이다 — `fsl.yaml`은 i.MX8MP 보드를 "보드 compatible + SoM compatible + `fsl,imx8mp`" 같은 정해진 목록으로만 받는다. 새 보드를 upstream하려면 그 목록에 보드를 추가하는 binding 패치가 같이 간다(D7 §7, P7). IMU·레귤레이터·CAN PHY·pinctrl 노드는 조용히 통과했다.

---
## 7. 랩 — QEMU 위에 "P4 센서 보드"를 만든다

### 7.1 무엇으로 무엇을 흉내 내나

QEMU `virt`에는 카메라도, IMU도, CAN 트랜시버도 없다. 그래도 **드라이버가 DT를 읽고 핀·레일을 움직이는 부분**은 진짜 커널 코드로 돌릴 수 있다. 칩이 대답하지 않을 뿐이다.

| 실제 보드 | 랩의 대역 | 비고 |
|---|---|---|
| SoC GPIO1 뱅크 (16핀) | `gpio-sim` 뱅크 `p4_gpio1` (DT로 생성) | 출력 값을 debugfs·trace로 본다. IRQ 타입은 무시함 (§7.7) |
| SoC GPIO0 (IRQ 입력) | QEMU PL061 `gpio@9030000` + `interrupt-controller` 추가 | 진짜 2셀 IRQ 지정자 해석 |
| I2C3 컨트롤러 | `i2c-gpio` (gpio-sim 0·1번 핀 bit-bang) | 아무도 ACK 안 함 → 칩 ID 읽기 실패 |
| PMIC 3.3 V, LDO 4개, 5 V 스위치 | `regulator-fixed` ×6 | 커널 내장 `fixed.c` |
| 24 MHz 오실레이터 | `fixed-clock` + OE hog | |
| BMI160, OV5640 | 진짜 드라이버 `bmi160_i2c.ko`, `ov5640.ko` | 전원·GPIO 시퀀스까지 진짜로 돈다 |
| TCAN1042 | 진짜 드라이버 `phy-can-transceiver.ko` | |
| SoC FlexCAN | `p4_canctl.ko` (직접 쓴 60줄 소비자) | `phy_power_on/off` + `xceiver-supply` + IRQ |

gpio-sim을 SoC GPIO 대역으로 쓰는 방법은 H4 §1.2에서 봤다. 여기서 새로운 것은 **gpio-line-names에 회로도 net 이름을 그대로** 넣는다는 점이다. 현장에서 `gpioinfo` 한 번으로 핀을 찾는 BSP 관습이다.

### 7.2 예제 4 — 보드 DT (기본 dts + 회로도 조각 + CAN 애드온 overlay)

**무엇을 확인하나**: QEMU가 덤프한 기본 DT(`p4_base.dts`, `-machine virt,dumpdtb=` 로 `-smp 2 -m 1024M` 조건에서 덤프 → dtc로 dts 변환)를 include하고 그 위에 회로도에서 옮긴 노드를 얹는다. 실수 재현용 스위치(`-DBAD_REG` 등)를 cpp 매크로로 넣었다. 파일이 길어 두 블록으로 나눠 보인다.

```dts
/* p4_board.dts — "P4 센서 보드" = QEMU virt + 회로도에서 옮긴 노드들 (1/2) */
#include "p4_base.dts"
#include <dt-bindings/gpio/gpio.h>
#include <dt-bindings/interrupt-controller/irq.h>

#ifdef BAD_REG		/* 실수 1: enable-active-high 빠뜨림 */
#define IMU_EN_POL
#else
#define IMU_EN_POL enable-active-high;
#endif
#ifdef BAD_RST		/* 실수 2: RESETB를 active-high로 */
#define CAM_RST_FLAG GPIO_ACTIVE_HIGH
#else
#define CAM_RST_FLAG GPIO_ACTIVE_LOW
#endif
#ifdef NO_PULL		/* 실수 3: 풀린 선을 올려 줄 풀업이 없음 */
#define I2C_FLAGS (GPIO_ACTIVE_HIGH | GPIO_OPEN_DRAIN)
#define I2C_OD
#else			/* 정상: 보드 풀업 4.7k (gpio-sim 한계 → 본문) */
#define I2C_FLAGS GPIO_ACTIVE_HIGH
#define I2C_OD i2c-gpio,sda-open-drain; i2c-gpio,scl-open-drain;
#endif

/ {
	aliases { i2c3 = &i2c3; };

	p4-soc-gpio {				/* SoC GPIO1 뱅크 대역 (gpio-sim) */
		compatible = "gpio-simulator";
		gpio1: bank0 {
			gpio-controller;
			#gpio-cells = <2>;
			interrupt-controller;
			#interrupt-cells = <2>;
			ngpios = <16>;
			gpio-sim,label = "p4_gpio1";
			gpio-line-names = "I2C3_SCL", "I2C3_SDA", "IMU_INT1_3V3",
				"IMU_LS_OE", "CAM_DOVDD_EN", "CAM_AVDD_EN",
				"CAM_DVDD_EN", "CAM_RST_N", "CAM_PWDN", "CAN_STB",
				"IMU_PWR_EN", "CAM_OSC_EN", "CAN_5V_EN", "", "", "";
			imu-ls-oe-hog {
				gpio-hog;
				gpios = <3 GPIO_ACTIVE_HIGH>;
				output-high;
				line-name = "IMU_LS_OE";
			};
			cam-osc-en-hog {
				gpio-hog;
				gpios = <11 GPIO_ACTIVE_HIGH>;
				output-high;
				line-name = "CAM_OSC_EN";
			};
		};
	};
```

```dts
	/* p4_board.dts (2/2) */
	cam_xclk: clock-cam-24m {		/* Y2: 24 MHz 오실레이터 */
		compatible = "fixed-clock";
		#clock-cells = <0>;
		clock-frequency = <24000000>;
		clock-output-names = "cam_xclk";
	};

	reg_3v3: regulator-3v3 {		/* PMIC BUCK — 항상 켜짐 */
		compatible = "regulator-fixed";
		regulator-name = "VDD_3V3";
		regulator-min-microvolt = <3300000>;
		regulator-max-microvolt = <3300000>;
		regulator-always-on;
		regulator-boot-on;
	};

	reg_imu: regulator-imu-1v8 {		/* U5 LDO, EN = IMU_PWR_EN */
		compatible = "regulator-fixed";
		regulator-name = "VDD_IMU_1V8";
		regulator-min-microvolt = <1800000>;
		regulator-max-microvolt = <1800000>;
		gpio = <&gpio1 10 GPIO_ACTIVE_HIGH>;
		IMU_EN_POL
		startup-delay-us = <2000>;
		vin-supply = <&reg_3v3>;
	};
};
#include "p4_cam_regs.dtsi"
#include "p4_i2c3.dtsi"
soc_gpio0: &{/pl061@9030000} {		/* SoC GPIO0: IRQ 컨트롤러로도 쓴다 */
	interrupt-controller;
	#interrupt-cells = <2>;
};
```

마지막 네 줄이 실무 요령 하나다. QEMU가 만든 기본 DT에는 라벨이 없고 PL061 노드에 `interrupt-controller`도 없다. `라벨: &{/경로} { ... }`로 **경로 참조 노드를 다시 열면서 라벨을 붙일 수 있다**(dtc 1.7.2에서 동작 확인 — 컴파일 후 `__symbols__`에 `soc_gpio0`이 생긴다). 벤더 dtsi를 고칠 수 없을 때 보드 파일에서 쓰는 방법이다.

카메라 레일 세 개(`p4_cam_regs.dtsi`)는 §4.3 표를 그대로 옮겼다. 하나만 보이면:

```dts
	reg_cam_dvdd: regulator-cam-dvdd {
		compatible = "regulator-fixed";
		regulator-name = "CAM_DVDD_1V5";
		regulator-min-microvolt = <1500000>;
		regulator-max-microvolt = <1500000>;
		gpio = <&gpio1 6 GPIO_ACTIVE_HIGH>;
		enable-active-high;
		startup-delay-us = <1000>;
		vin-supply = <&reg_cam_dovdd>;	/* 회로도: U7 입력 = 1V8 */
	};
```

(DOVDD는 GPIO 4번·1.8 V·1000 µs·`vin-supply = <&reg_3v3>`, AVDD는 5번·2.8 V·3000 µs·`<&reg_3v3>`이다.)

I2C3 버스와 그 위의 두 칩(`p4_i2c3.dtsi`):

```dts
/* p4_i2c3.dtsi — I2C3 버스(i2c-gpio 대역)와 그 위의 IMU·카메라 */
/ {
	i2c3: i2c-3 {
		compatible = "i2c-gpio";
		scl-gpios = <&gpio1 0 I2C_FLAGS>;
		sda-gpios = <&gpio1 1 I2C_FLAGS>;
		i2c-gpio,delay-us = <5>;	/* ~100 kHz */
		I2C_OD
		#address-cells = <1>;
		#size-cells = <0>;

		imu@68 {			/* SDO = GND → 0x68 */
			compatible = "bosch,bmi160";
			reg = <0x68>;
			vdd-supply = <&reg_imu>;
			vddio-supply = <&reg_imu>;
			interrupts-extended = <&gpio1 2 IRQ_TYPE_EDGE_FALLING>;
			interrupt-names = "INT1";
			drive-open-drain;
		};

		camera@3c {
			compatible = "ovti,ov5640";
			reg = <0x3c>;
			clocks = <&cam_xclk>;
			clock-names = "xclk";
			DOVDD-supply = <&reg_cam_dovdd>;
			AVDD-supply = <&reg_cam_avdd>;
			DVDD-supply = <&reg_cam_dvdd>;
			powerdown-gpios = <&gpio1 8 GPIO_ACTIVE_HIGH>;
			reset-gpios = <&gpio1 7 CAM_RST_FLAG>;
			port {
				cam_ep: endpoint {
					clock-lanes = <0>;
					data-lanes = <1 2>;
				};
			};
		};
	};
};
```

`endpoint`에 `remote-endpoint`가 없다. 실제 보드라면 CSI 수신기 노드의 endpoint와 서로 가리켜야 한다(Q2). 랩에는 CSI 수신기가 없어서 뺐다. ov5640 드라이버는 `data-lanes`만 보고 bus type을 CSI-2 D-PHY로 판단하므로 probe는 진행된다.

CAN 애드온은 **overlay**로 분리했다(D6). 메인보드와 별개로 장착되는 메자닌 보드를 표현하는 전형적인 방식이다.

```dts
/* p4_can.dtso — CAN 애드온 보드 overlay: TCAN1042 + 5V 레일 + 컨트롤러 대역 */
/dts-v1/;
/plugin/;
#include <dt-bindings/gpio/gpio.h>
#include <dt-bindings/interrupt-controller/irq.h>
#ifdef PHY_SUPPLY_BAD	/* 스키마 실수: 5V를 PHY 노드에 */
#define CAN_SUPPLY_IN_PHY phy-supply = <&reg_can_5v>;
#define CAN_SUPPLY_IN_CTRL
#else
#define CAN_SUPPLY_IN_PHY
#define CAN_SUPPLY_IN_CTRL xceiver-supply = <&reg_can_5v>;
#endif
#ifdef BAD_IRQ		/* INT 극성 실수 */
#define CAN_IRQ_TYPE IRQ_TYPE_LEVEL_LOW
#else
#define CAN_IRQ_TYPE IRQ_TYPE_LEVEL_HIGH
#endif
#ifdef BAD_STB		/* STB 극성 실수 */
#define STB_FLAG GPIO_ACTIVE_LOW
#else
#define STB_FLAG GPIO_ACTIVE_HIGH
#endif

&{/} {
	reg_can_5v: regulator-can-5v {
		compatible = "regulator-fixed";
		regulator-name = "CAN_VCC_5V";
		regulator-min-microvolt = <5000000>;
		regulator-max-microvolt = <5000000>;
		gpio = <&gpio1 12 GPIO_ACTIVE_HIGH>;
		enable-active-high;
		startup-delay-us = <500>;
		vin-supply = <&reg_3v3>;
	};
	can_phy0: can-phy0 {
		compatible = "ti,tcan1042";
		#phy-cells = <0>;
		max-bitrate = <5000000>;
		standby-gpios = <&gpio1 9 STB_FLAG>;
		CAN_SUPPLY_IN_PHY
	};
	p4-can0 {			/* SoC CAN 컨트롤러 대역 (p4_canctl.ko) */
		compatible = "dons,p4-canctl";
		phys = <&can_phy0>;
		phy-names = "can";
		CAN_SUPPLY_IN_CTRL
		interrupts-extended = <&soc_gpio0 5 CAN_IRQ_TYPE>;
	};
};
```

이 랩의 "CAN 컨트롤러 IRQ"는 **active-high push-pull** 출력이라고 가정한다. 이유는 QEMU virt의 PL061 5번 입력이 항상 0으로 읽히기 때문이다(debugfs 실측 `in lo`) — "대기 레벨이 0인 선"이므로 맞는 타입은 `LEVEL_HIGH`, 틀린 타입은 `LEVEL_LOW`가 된다. (실제 MCP2518FD 같은 SPI CAN 컨트롤러의 INT는 active-low OD다. 그때는 반대.)

빌드 스크립트 — cpp → dtc `-@` → overlay 컴파일 → `fdtoverlay`로 병합:

```sh
#!/bin/sh
# build_dt.sh <name> [cpp -D 옵션들] : 보드 dtb + CAN overlay → 병합 dtb
set -e
N=$1; shift
S=/Users/donh/.elinux_scratch/P4; O=/var/tmp/P4; K=/var/lab/src/linux-6.18.55
CPP="cpp -nostdinc -undef -x assembler-with-cpp -I $O -I $S -I $K/include $*"
$CPP $S/p4_board.dts -o $O/$N.pp.dts
dtc -@ -q -I dts -O dtb -o $O/$N.base.dtb $O/$N.pp.dts
$CPP $S/p4_can.dtso -o $O/$N.can.pp.dtso
dtc -@ -q -I dts -O dtb -o $O/$N.can.dtbo $O/$N.can.pp.dtso
fdtoverlay -i $O/$N.base.dtb -o $O/$N.dtb $O/$N.can.dtbo
ls -l $O/$N.base.dtb $O/$N.can.dtbo $O/$N.dtb | awk '{print $5, $9}'
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/P4; sh /Users/donh/.elinux_scratch/P4/build_dt.sh good
  fdtget -t x good.dtb /can-phy0 standby-gpios; fdtget -t x good.dtb /i2c-3/camera@3c reset-gpios
  fdtget -t x good.dtb /p4-can0 interrupts-extended; fdtget -t x good.dtb /pl061@9030000 phandle
  fdtget good.dtb /__symbols__ can_phy0; fdtget -p good.can.dtbo /__fixups__
  fdtget good.can.dtbo /__fixups__ gpio1'
```

```text
10866 /var/tmp/P4/good.base.dtb
1341 /var/tmp/P4/good.can.dtbo
11437 /var/tmp/P4/good.dtb
1 9 0
1 7 1
8005 5 4
8005
/can-phy0
gpio1
reg_3v3
soc_gpio0
/fragment@0/__overlay__/regulator-can-5v:gpio:0 /fragment@0/__overlay__/can-phy0:standby-gpios:0
```

출력에서 볼 것:

- `standby-gpios = <1 9 0>`: phandle 1(gpio-sim 뱅크), 9번 핀, 플래그 0 = `GPIO_ACTIVE_HIGH`. `reset-gpios = <1 7 1>`의 마지막 1이 `GPIO_ACTIVE_LOW`다. **회로도의 `_N` 하나가 dtb에서는 비트 하나**다.
- `interrupts-extended = <0x8005 5 4>`: PL061의 phandle, 5번 핀, 4 = `IRQ_TYPE_LEVEL_HIGH` (`dt-bindings/interrupt-controller/irq.h`: RISING 1, FALLING 2, LEVEL_HIGH 4, LEVEL_LOW 8).
- overlay의 `__fixups__`에 `gpio1`, `reg_3v3`, `soc_gpio0` — overlay가 메인보드에 **요구하는 라벨 목록**이다. 메인보드를 `-@` 없이 빌드했다면 `fdtoverlay`가 여기서 실패한다(D6 §2).

### 7.3 예제 5 — dt-schema: 무엇을 잡고, 무엇을 못 잡나

**무엇을 확인하나**: 정상 dtb, 그리고 "트랜시버 5 V를 PHY 노드의 `phy-supply`에 단" 변형(`-DPHY_SUPPLY_BAD`), 극성만 틀린 세 변형을 같은 스키마로 검사한다. 출력은 우리 노드만 거른다(QEMU가 만든 기본 노드는 `pl061@...` 같은 옛 노드 이름 때문에 경고가 많다).

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/P4; S=/Users/donh/.elinux_scratch/P4
  sh $S/build_dt.sh bad_phy -DPHY_SUPPLY_BAD >/dev/null
  for f in good bad_phy; do echo "== $f"; ./venv/bin/dt-validate -s schema.json $f.dtb 2>&1 \
    | grep -A1 -E "^$f.dtb: (i2c-3|imu|camera|regulator|can-phy0|p4-can0|clock-cam)"; done
  for f in bad_rst bad_stb bad_reg; do echo "== dt-validate $f (우리 노드 경고 수)"
    ./venv/bin/dt-validate -s schema.json $f.dtb 2>&1 \
    | grep -cE "^$f.dtb: (i2c-3|imu|camera|regulator|can-phy0|p4-can0|clock-cam)"; done'
```

```text
== good
== bad_phy
bad_phy.dtb: can-phy0 (ti,tcan1042): 'phy-supply' does not match any of the regexes: '^pinctrl-[0-9]+$'
	from schema $id: http://devicetree.org/schemas/phy/ti,tcan104x-can.yaml
== dt-validate bad_rst (우리 노드 경고 수)
0
== dt-validate bad_stb (우리 노드 경고 수)
0
== dt-validate bad_reg (우리 노드 경고 수)
0
```

출력에서 볼 것:

- `ti,tcan104x-can.yaml`은 `additionalProperties: false`라서 `phy-supply`를 거부한다. PHY 코어(`drivers/phy/phy-core.c`)는 사실 `phy-supply`를 처리하지만, **binding이 허락하지 않으면 upstream에서 안 받는다**. 그래서 5 V는 컨트롤러의 `xceiver-supply`로 옮겼다(imx8mp-evk와 같은 위치).
- 극성 실수 세 개는 **경고 0개**다. 스키마는 "타입과 개수"를 검사하지 회로도를 모른다. `GPIO_ACTIVE_LOW`인지 `HIGH`인지, `enable-active-high`가 있어야 하는지는 **사람이 회로도와 대조**해야 한다. 이것이 §8의 동기다.
- 또 하나: 이름을 `i2c-gpio-3`으로 지었을 때는 `$nodename ... does not match '^i2c(@.+|-[a-z0-9]+)?$'`와 함께 자식 노드가 전부 "Unevaluated properties"로 찍혔다(첫 시도의 실제 출력). 노드 이름 규칙이 자식 검증까지 막는다.

### 7.4 실행 하네스

게스트는 BusyBox initramfs에 `6.18.55-dom` 모듈 6개(`i2c-gpio`, `bmi160_core`, `bmi160_i2c`, `ov5640`, `phy-can-transceiver`, `i2c-stub`, 그리고 `modules.dep`)와 `p4_canctl.ko`를 넣은 것이다. 게스트 스크립트 `p4.sh`는 ftrace 이벤트(레귤레이터, GPIO 값·방향, 클럭)를 켜고, 드라이버를 순서대로 올린 뒤 상태를 덤프한다.

```sh
#!/bin/sh
# p4.sh <mode> : 게스트 안에서 — 추적 켜고, 드라이버 로드, 상태 덤프 (요약본)
T=/sys/kernel/tracing
mount -t tracefs none $T
echo 32768 > $T/buffer_size_kb
for e in regulator/regulator_enable regulator/regulator_enable_complete \
	 regulator/regulator_disable gpio/gpio_value gpio/gpio_direction \
	 clk/clk_prepare clk/clk_enable; do
	echo 1 > $T/events/$e/enable
done
for e in gpio_value gpio_direction; do	# I2C 비트뱅 라인(520,521)은 제외
	echo "gpio != 520 && gpio != 521" > $T/events/gpio/$e/filter
done
modprobe i2c-gpio; modprobe bmi160_i2c; modprobe ov5640
modprobe phy-can-transceiver; insmod /p4_canctl.ko
sleep 1
dmesg | grep -iE "bmi160|3-0068|3-003c|ov5640|p4-can0|i2c-gpio"
cat /sys/kernel/debug/regulator/regulator_summary
cat /sys/kernel/debug/gpio
grep -iE "p4|pl061|CPU0" /proc/interrupts
i2cdetect -y 3 0x08 0x77
[ "$1" = canoff ] && rmmod p4_canctl
cat $T/trace | grep -v "^#"
```

(실제 파일에는 구분용 `echo "### ..."` 줄과 i2c-stub 모드(§7.8)가 더 있다.) 부팅:

```sh
timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1024M -nographic \
  -kernel /var/lab/out/Image-dom -dtb /var/tmp/P4/good.dtb -initrd /var/tmp/P4/p4_initrd.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/p4.sh~base" > /var/tmp/P4/good_base.log 2>&1
```

`-dtb`로 넘기는 dtb는 QEMU가 덤프한 것과 **같은 `-smp`/`-m`** 조건이어야 한다(랩 README의 주의 — CPU 노드·memory 노드가 dtb에 박혀 있다).

### 7.5 예제 6 — 부팅 후 상태: probe 로그, regulator_summary, GPIO

**무엇을 확인하나**: 우리가 쓴 DT가 커널 객체로 어떻게 보이는지. 출력은 `good_base.log`의 해당 구간이다.

```text
[    8.709867][   T82] i2c-gpio i2c-3: Slow GPIO pins might wreak havoc into I2C/SMBus bus timing
[    8.722082][   T82] i2c-gpio i2c-3: using lines 521 (SDA) and 520 (SCL)
[    8.837890][   T83] bmi160_i2c 3-0068: mounting matrix not found: using identity...
[    9.010330][   T85] ov5640 3-003c: ov5640_write_reg: error: reg=3008, val=42
[    9.011379][   T85] ov5640 3-003c: ov5640_write_reg: error: reg=3103, val=11
[    9.016271][   T85] ov5640 3-003c: ov5640_read_reg: error: reg=3108
[    9.016792][   T85] ov5640 3-003c: failed to power on
[    9.144109][   T87] p4_canctl p4-can0: irq 23 trigger-from-DT 0x4
[    9.150725][   T87] p4_canctl p4-can0: transceiver powered on (bus active)
### regulator_summary
 regulator                      use open bypass  opmode voltage current     min     max
---------------------------------------------------------------------------------------
 regulator-dummy                  1    0      0 unknown     0mV     0mA     0mV     0mV 
 VDD_3V3                          2    4      0 unknown  3300mV     0mA  3300mV  3300mV 
    CAN_VCC_5V                    1    1      0 unknown  5000mV     0mA  5000mV  5000mV 
       p4-can0-xceiver            1                                 0mA     0mV     0mV
    CAM_DOVDD_1V8                 0    1      0 unknown  1800mV     0mA  1800mV  1800mV 
       CAM_DVDD_1V5               0    0      0 unknown  1500mV     0mA  1500mV  1500mV 
    CAM_AVDD_2V8                  0    0      0 unknown  2800mV     0mA  2800mV  2800mV 
    VDD_IMU_1V8                   0    0      0 unknown  1800mV     0mA  1800mV  1800mV 
```

출력에서 볼 것:

- 버스 번호가 **3**이다(`3-0068`, `3-003c`) — `/aliases { i2c3 = &i2c3; }` 덕분이다. alias가 없으면 이 버스는 등록 순서대로 0번이 됐을 것이다. 사용자 공간 도구(`i2cdetect -y 3`)와 문서가 번호에 의존하므로 BSP는 alias로 고정한다.
- `regulator_summary`의 들여쓰기가 **전원 트리 그 자체**다: `VDD_3V3` 아래 `CAN_VCC_5V`, `CAM_DOVDD_1V8` 아래 `CAM_DVDD_1V5`(우리가 쓴 `vin-supply`). 회로도 전원 트리 페이지와 이 출력을 나란히 놓고 대조하는 것이 bring-up의 첫 점검이다.
- `CAN_VCC_5V` 아래 `p4-can0-xceiver 1` = 소비자 `p4-can0`의 `xceiver` supply가 켜져 있다. 카메라 레일은 `use 0` — ov5640 probe가 실패하면서 다 껐다. `VDD_IMU_1V8`도 `use 0`이다. 둘 다 **칩이 I2C에 대답하지 않아서**(랩에는 칩이 없다) probe가 실패한 것이고, DT가 틀린 것이 아니다. bmi160은 레일을 켠 직후 첫 I2C 쓰기(soft reset 명령)가 NACK로 실패하면 **로그 한 줄 없이** 레일을 끄고 나간다(`bmi160_chip_init()`의 `goto disable_regulator`). "dmesg에 아무것도 없는데 장치가 없다"의 흔한 원인이다 — trace의 `regulator_enable` → 7 ms 뒤 `regulator_disable`이 유일한 흔적이었다.
- 이름이 회로도 net 이름(`CAM_AVDD_2V8`)이라 이 표만 보고 HW 엔지니어와 대화가 된다. `regulator-name`을 비워 두면 노드 이름이 나온다.

같은 로그의 GPIO 상태:

```text
gpiochip0: 8 GPIOs, parent: amba/9030000.pl061, 9030000.pl061:
 gpio-3   (                    |GPIO Key Poweroff   ) in  lo IRQ 
 gpio-5   (                    |interrupt           ) in  lo IRQ 

gpiochip1: 16 GPIOs, parent: platform/p4-soc-gpio, p4_gpio1, can sleep:
 gpio-0   (scl) output-high,pull-down
 gpio-1   (sda) output-high,pull-down
 gpio-2   (<unused>) input,pull-down
 gpio-3   (IMU_LS_OE) output-high,pull-down
 gpio-4   (regulator-cam-dovdd) output-low,pull-down
 gpio-5   (regulator-cam-avdd) output-low,pull-down
 gpio-6   (regulator-cam-dvdd) output-low,pull-down
 gpio-7   (<unused>) output-low,pull-down
 gpio-8   (<unused>) output-low,pull-down
 gpio-9   (standby) output-low,pull-down
 gpio-10  (regulator-imu-1v8) output-low,pull-down
 gpio-11  (CAM_OSC_EN) output-high,pull-down
 gpio-12  (regulator-can-5v) output-high,pull-down
 gpio-13  (<unused>) input,pull-down
 gpio-14  (<unused>) input,pull-down
 gpio-15  (<unused>) input,pull-down
```

출력에서 볼 것:

- 괄호 안은 **소비자 라벨**이다: hog는 `line-name`(`IMU_LS_OE`, `CAM_OSC_EN`), 레귤레이터는 노드 이름, PHY는 con_id(`standby`). gpio-sim의 debugfs 형식은 칩 드라이버가 정한 것이라 PL061과 다르다(H4 §4).
- 값은 **물리 레벨**이다. `standby` = `output-low` → 트랜시버가 정상 모드(phy_power_on 이후).
- 7·8번(RESETB·PWDN)은 ov5640 probe 실패 후 devm이 해제해서 `<unused>`이고 값은 `output-low`다. 마지막으로 쓴 PWDN 값은 1이었는데(§7.6 trace) 0으로 보이는 이유는 gpio-sim의 `gpio_sim_free()`가 해제 시 값을 **pull 설정**(기본 pull-down → 0)으로 되돌리기 때문이다 — 회로의 풀 저항을 흉내 낸 동작이다. 실제 SoC GPIO는 해제해도 출력 레지스터 값을 그대로 유지하는 경우가 많아서, "드라이버가 실패한 뒤 핀이 어떤 상태로 남나"는 칩마다 확인해야 한다.
- trace의 GPIO 번호는 **전역 번호**(이 칩의 base = 520)다: 520 + 7 = 527 = RESETB, 528 = PWDN, 529 = STB, 530 = IMU_EN, 524~526 = 카메라 레일 EN, 532 = CAN 5 V EN.

### 7.6 예제 7 — ftrace로 본 카메라 전원 시퀀스 (실측)

**무엇을 확인하나**: DT에 적은 레일·GPIO·클럭을 ov5640 드라이버가 어떤 순서·간격으로 움직이는지. 아래는 `good_base.log`의 trace 구간 일부다.

```text
        modprobe-85      [000] .....     8.942289: gpio_value: 528 set 1
        modprobe-85      [000] .....     8.942304: gpio_direction: 528 out (0)
        modprobe-85      [000] .....     8.942570: gpio_value: 527 set 0
        modprobe-85      [000] .....     8.942575: gpio_direction: 527 out (0)
        modprobe-85      [000] .....     8.954534: clk_prepare: cam_xclk
        modprobe-85      [000] d..1.     8.954659: clk_enable: cam_xclk
    kworker/u8:1-34      [001] .....     8.956984: regulator_enable: name=CAM_DOVDD_1V8
    kworker/u8:1-34      [001] .....     8.957016: gpio_value: 524 set 1
    kworker/u8:1-34      [001] .....     8.959394: regulator_enable_complete: name=CAM_DOVDD_1V8
    kworker/u8:1-34      [001] .....     8.962913: regulator_enable: name=CAM_DVDD_1V5
    kworker/u8:1-34      [001] .....     8.962944: gpio_value: 526 set 1
    kworker/u8:1-34      [001] .....     8.964844: regulator_enable_complete: name=CAM_DVDD_1V5
    kworker/u8:2-38      [001] .....     8.965536: regulator_enable: name=CAM_AVDD_2V8
    kworker/u8:2-38      [001] .....     8.965574: gpio_value: 525 set 1
    kworker/u8:2-38      [001] .....     8.969650: regulator_enable_complete: name=CAM_AVDD_2V8
        modprobe-85      [000] .....     8.972934: gpio_value: 527 set 0
        modprobe-85      [000] .....     8.972975: gpio_value: 528 set 1
        modprobe-85      [000] .....     8.982004: gpio_value: 528 set 0
        modprobe-85      [000] .....     8.983888: gpio_value: 527 set 1
        modprobe-85      [001] .....     9.016458: gpio_value: 528 set 1
        modprobe-85      [001] .....     9.016574: regulator_disable: name=CAM_DVDD_1V5
        modprobe-85      [001] .....     9.016591: gpio_value: 526 set 0
        modprobe-85      [001] .....     9.016617: regulator_disable: name=CAM_AVDD_2V8
        modprobe-85      [001] .....     9.016628: gpio_value: 525 set 0
        modprobe-85      [001] .....     9.016637: regulator_disable: name=CAM_DOVDD_1V8
        modprobe-85      [001] .....     9.016642: gpio_value: 524 set 0
```

해석 (값은 **물리 레벨**, 시각은 TCG 실측):

1. `528 set 1`, `527 set 0` — probe의 `devm_gpiod_get_optional(..., GPIOD_OUT_HIGH)` 두 번. 둘 다 **논리 1**(= 건다)로 시작했는데 물리값이 다르다: PWDN은 ACTIVE_HIGH라 1, RESETB는 ACTIVE_LOW라 0. 센서는 전원이 오기 전부터 "리셋 + 파워다운"에 묶인다. 이것이 맞는 초기 상태다.
2. `clk_enable: cam_xclk` → 레일 enable. `regulator_enable` → `gpio_value set 1` → `enable_complete`의 간격이 `startup-delay-us`다: DOVDD 2.4 ms(DT 1000 µs), AVDD 4.1 ms(DT 3000 µs). TCG라 오버헤드가 붙지만 **DT 값보다 짧아지는 일은 없다**.
3. 레일 enable이 **`kworker` 스레드**에서 돈다. `regulator_bulk_enable()`이 레일마다 비동기 작업을 띄우기 때문이다. 이 실행에서는 DOVDD → DVDD → AVDD 순이었다. DVDD가 AVDD보다 먼저 켜진 것은 데이터시트 순서(DOVDD → AVDD → DVDD)와 다르다. `no_pull` 실행(§8.4)의 trace에서는 **AVDD → DOVDD → DVDD**였다. 순서가 실행마다 다르다. 유일하게 보장된 것은 `vin-supply`로 묶은 DVDD가 DOVDD 뒤라는 것뿐이다.
4. `527 set 0` → `528 set 1` → (9 ms) → `528 set 0` → (1.9 ms) → `527 set 1`: `ov5640_powerup_sequence()`의 리셋 assert → power down → t2 대기(5~10 ms) → PWDN 해제 → t3 대기(1~2 ms) → RESETB 해제. 그 뒤 t4(20~25 ms) 후 I2C로 칩 ID를 읽는다.
5. `9.016458`부터 역순 정리 — 칩 ID 읽기 실패(`failed to power on`) 후 PWDN을 다시 걸고 레일을 끈다. 끄는 순서는 bulk disable이라 동기적이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <line x1="130.0" y1="34" x2="130.0" y2="262" stroke="#888" stroke-opacity="0.35"/>
  <text x="130.0" y="280" text-anchor="middle" font-size="12">0</text>
  <line x1="195.0" y1="34" x2="195.0" y2="262" stroke="#888" stroke-opacity="0.35"/>
  <text x="195.0" y="280" text-anchor="middle" font-size="12">10</text>
  <line x1="260.0" y1="34" x2="260.0" y2="262" stroke="#888" stroke-opacity="0.35"/>
  <text x="260.0" y="280" text-anchor="middle" font-size="12">20</text>
  <line x1="325.0" y1="34" x2="325.0" y2="262" stroke="#888" stroke-opacity="0.35"/>
  <text x="325.0" y="280" text-anchor="middle" font-size="12">30</text>
  <line x1="390.0" y1="34" x2="390.0" y2="262" stroke="#888" stroke-opacity="0.35"/>
  <text x="390.0" y="280" text-anchor="middle" font-size="12">40</text>
  <line x1="455.0" y1="34" x2="455.0" y2="262" stroke="#888" stroke-opacity="0.35"/>
  <text x="455.0" y="280" text-anchor="middle" font-size="12">50</text>
  <line x1="520.0" y1="34" x2="520.0" y2="262" stroke="#888" stroke-opacity="0.35"/>
  <text x="520.0" y="280" text-anchor="middle" font-size="12">60</text>
  <line x1="585.0" y1="34" x2="585.0" y2="262" stroke="#888" stroke-opacity="0.35"/>
  <text x="585.0" y="280" text-anchor="middle" font-size="12">70</text>
  <line x1="650.0" y1="34" x2="650.0" y2="262" stroke="#888" stroke-opacity="0.35"/>
  <text x="650.0" y="280" text-anchor="middle" font-size="12">80</text>
  <text x="390" y="300" text-anchor="middle" font-size="12">probe 시작 후 시간 (ms, QEMU TCG 실측 — 실제 HW 수치 아님)</text>
  <text x="20" y="65" font-size="12">XCLK 24MHz</text>
  <path d="M130.0,70 L210.4,70 L210.4,50 L650.0,50 L650.0,50" fill="none" stroke="#e08a3c" stroke-width="2"/>
  <text x="20" y="101" font-size="12">DOVDD 1.8V</text>
  <path d="M130.0,106 L225.7,106 L225.7,86 L613.3,86 L613.3,106 L650.0,106 L650.0,106" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="20" y="137" font-size="12">DVDD 1.5V</text>
  <path d="M130.0,142 L264.3,142 L264.3,122 L613.0,122 L613.0,142 L650.0,142 L650.0,142" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="20" y="173" font-size="12">AVDD 2.8V</text>
  <path d="M130.0,178 L281.4,178 L281.4,158 L613.2,158 L613.2,178 L650.0,178 L650.0,178" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <text x="20" y="209" font-size="12">PWDN</text>
  <path d="M130.0,194 L388.1,194 L388.1,214 L612.1,214 L612.1,194 L650.0,194 L650.0,194" fill="none" stroke="#3f9a6b" stroke-width="2"/>
  <text x="20" y="245" font-size="12">RESETB</text>
  <path d="M130.0,250 L400.4,250 L400.4,230 L650.0,230 L650.0,230" fill="none" stroke="#d0564a" stroke-width="2"/>
  <text x="210.4" y="26" text-anchor="middle" font-size="12">clk on</text>
  <text x="384.1" y="26" text-anchor="end" font-size="12">PWDN 해제</text>
  <text x="404.4" y="26" font-size="12">RESETB 해제</text>
  <text x="612.1" y="320" text-anchor="end" font-size="12">칩 ID 실패 → 전부 끔</text>
</svg>
```

그림 6 — 위 trace를 그대로 그린 타이밍도(물리 레벨, probe의 첫 GPIO 요청을 0 ms로). XCLK의 꺼짐은 이벤트를 켜지 않아 그리지 않았다. 레일 세 개의 상승 순서가 데이터시트 순서와 다르다는 것과, PWDN 해제 → RESETB 해제 간격(1.9 ms ≥ t3 1 ms)을 읽을 수 있다. TCG 실측이므로 절대값은 실제 하드웨어와 다르다.

실무 의미: 실제 보드에서 같은 trace를 뜨고 **scope 캡처와 겹쳐 보면**, "SW가 의도한 순서"와 "레일이 실제로 올라온 시각"의 차이(LDO soft-start, 큰 출력 커패시터)를 바로 볼 수 있다. Don이 RF bring-up에서 scope 트리거를 GPIO 토글에 걸던 방법과 같다 — 커널 쪽 기준점이 ftrace다(M 모듈).

### 7.7 예제 8 — CAN PHY: probe, `phy_power_on`, `phy_power_off`

**무엇을 확인하나**: `p4_canctl.ko`(컨트롤러 대역)가 PHY를 켜고 끌 때 STB와 5 V 레일이 어떻게 움직이는지. 소비자 모듈 전체:

```c
/* p4_canctl.c — CAN 컨트롤러 대역: DT의 xceiver-supply/phys/interrupts 소비 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/regulator/consumer.h>
struct p4c { struct phy *phy; };
static irqreturn_t p4c_isr(int irq, void *d) { return IRQ_HANDLED; }

static int p4c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct p4c *p;
	int irq, ret;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	p->phy = devm_phy_get(dev, "can");
	if (IS_ERR(p->phy))
		return dev_err_probe(dev, PTR_ERR(p->phy), "no can phy\n");
	ret = devm_regulator_get_enable_optional(dev, "xceiver");
	if (ret && ret != -ENODEV)
		return dev_err_probe(dev, ret, "xceiver-supply\n");
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	dev_info(dev, "irq %d trigger-from-DT 0x%x\n", irq,
		 irq_get_trigger_type(irq));
	ret = devm_request_threaded_irq(dev, irq, NULL, p4c_isr, IRQF_ONESHOT,
					"p4-can0", p);
	if (ret)
		return dev_err_probe(dev, ret, "request irq\n");
	ret = phy_power_on(p->phy);		/* = "ip link set can0 up" */
	if (ret)
		return dev_err_probe(dev, ret, "phy_power_on\n");
	platform_set_drvdata(pdev, p);
	dev_info(dev, "transceiver powered on (bus active)\n");
	return 0;
}

static void p4c_remove(struct platform_device *pdev)
{
	struct p4c *p = platform_get_drvdata(pdev);
	phy_power_off(p->phy);			/* = "ip link set can0 down" */
	dev_info(&pdev->dev, "transceiver powered off\n");
}
static const struct of_device_id p4c_of[] = {
	{ .compatible = "dons,p4-canctl" }, { }
};
MODULE_DEVICE_TABLE(of, p4c_of);
static struct platform_driver p4c_drv = {
	.probe = p4c_probe, .remove = p4c_remove,
	.driver = { .name = "p4_canctl", .of_match_table = p4c_of },
};
module_platform_driver(p4c_drv);
MODULE_DESCRIPTION("P4 CAN controller stand-in");
MODULE_LICENSE("GPL");
```

빌드는 `make -C /var/lab/out/linux-build-dom M=$PWD modules` — 경고 0개(`CC [M] p4_canctl.o`, `LD [M] p4_canctl.ko`만 출력). 실제 FlexCAN·m_can 드라이버도 `ndo_open`에서 `phy_power_on()`, `ndo_stop`에서 `phy_power_off()`를 부른다. `canoff` 모드 실행(`good_canoff.log`)의 trace에서 CAN 관련 줄만:

```text
        modprobe-86      [001] .....    18.820989: gpio_value: 529 set 1
        modprobe-86      [001] .....    18.821153: gpio_direction: 529 out (0)
          insmod-87      [000] .....    18.945033: regulator_enable: name=CAN_VCC_5V
          insmod-87      [000] .....    18.945075: gpio_value: 532 set 1
          insmod-87      [000] .....    18.947164: regulator_enable_complete: name=CAN_VCC_5V
          insmod-87      [000] .....    18.955636: gpio_value: 529 set 0
           rmmod-98      [001] .....    21.723074: gpio_value: 529 set 1
           rmmod-98      [001] .....    21.730503: regulator_disable: name=CAN_VCC_5V
           rmmod-98      [001] .....    21.730530: gpio_value: 532 set 0
```

출력에서 볼 것: PHY 드라이버 로드(`modprobe-86`) 시 STB = 1(standby로 시작, 보드 풀업과 같은 상태라 글리치 없음) → 컨트롤러 probe에서 5 V 켜고 → STB = 0(정상) → `rmmod`(= `down`)에서 STB = 1 → 5 V 끔. 이 순서가 보드 풀업 방향과 맞기 때문에 **부팅부터 종료까지 트랜시버가 버스에 쓰레기를 내보낼 틈이 없다**.

### 7.8 예제 9 — IRQ 타입은 DT에서 커널로 어떻게 가나

같은 실행의 `/proc/interrupts`:

```text
           CPU0       CPU1       
 21:          0          0 9030000.pl061   3 Edge      GPIO Key Poweroff
 23:          1          0 9030000.pl061   5 Level     p4-can0
```

dmesg의 `irq 23 trigger-from-DT 0x4`와 `/proc/interrupts`의 `Level`이 DT의 `IRQ_TYPE_LEVEL_HIGH`(4)에서 왔다. 경로: `platform_get_irq()` → `of_irq_get()` → PL061 irq domain의 `xlate`(2셀: 핀, 타입) → `irq_create_fwspec_mapping()`이 `irq_set_irq_type()`으로 PL061의 IS/IEV 레지스터를 설정. 드라이버는 `request_irq`에 트리거 플래그를 안 줬다 — **트리거는 DT가, 핸들러는 드라이버가** 맡는 분업이다.

랩 한계 하나: 같은 실험을 gpio-sim 핀(`<&gpio1 13 IRQ_TYPE_LEVEL_LOW>`)으로 처음 했을 때는 `trigger-from-DT 0x0`, `/proc/interrupts`에 `irq_sim 13 Edge`가 나왔다. 커널의 `irq_sim` domain(`kernel/irq/irq_sim.c`)은 `xlate`/`translate` 콜백이 없어서 DT의 두 번째 셀(타입)을 버린다. 실제 SoC GPIO 드라이버(gpio-mxc, pl061, …)는 2셀 xlate를 쓰므로 이 문제가 없다. 그래서 IRQ 실험은 PL061로 옮겼다.

### 7.9 예제 10 — 칩이 대답하면? i2c-stub에 같은 드라이버 붙이기

**무엇을 확인하나**: probe 실패가 "칩이 없어서"였음을 확인한다. `i2c-stub`은 레지스터 값을 설정할 수 있는 가짜 I2C 칩이다(A10, H5). 칩 ID 레지스터 0x00에 BMI160 값 `0xD1`을 넣고 sysfs `new_device`로 같은 드라이버를 붙인다(`stub` 모드, `good_stub.log`; 커널 cmdline 줄 3개는 생략).

```sh
modprobe i2c-stub chip_addr=0x68; B=$(grep -l "stub" /sys/bus/i2c/devices/i2c-*/name | cut -d/ -f6)
echo "stub bus = $B"; i2cset -y ${B#i2c-} 0x68 0x00 0xd1
echo bmi160 0x68 > /sys/bus/i2c/devices/$B/new_device; sleep 1
dmesg | grep -E -e "${B#i2c-}-0068" -e "stub"; ls /sys/bus/iio/devices/; cat /sys/bus/iio/devices/iio:device0/name
grep -B1 -A2 -e "${B#i2c-}-0068" /sys/kernel/debug/regulator/regulator_summary
```

```text
stub bus = i2c-4
[    8.411896][   T98] i2c-stub: Virtual chip at 0x68
[    8.524711][   T76] bmi160_i2c 4-0068: supply vdd not found, using dummy regulator
[    8.525325][   T76] bmi160_i2c 4-0068: supply vddio not found, using dummy regulator
[    8.529613][   T76] bmi160_i2c 4-0068: mounting matrix not found: using identity...
iio:device0
bmi160
 regulator-dummy                  3    2      0 unknown     0mV     0mA     0mV     0mV 
    4-0068-vddio                  1                                 0mA     0mV     0mV
    4-0068-vdd                    1                                 0mA     0mV     0mV
 VDD_3V3                          2    4      0 unknown  3300mV     0mA  3300mV  3300mV 
    CAN_VCC_5V                    1    1      0 unknown  5000mV     0mA  5000mV  5000mV 
```

출력에서 볼 것: 칩 ID가 맞으니 probe가 성공해 `iio:device0`이 생겼다. 그러나 이 장치는 **DT 노드가 없다**(sysfs로 만들었다). 그래서 `supply vdd not found, using dummy regulator` — 전원은 dummy, IRQ도 없음, mount-matrix도 없음. 이 세 줄은 실제 보드에서도 **"DT 노드와 장치가 연결되지 않았다"**는 신호다(노드가 다른 버스 아래에 있거나 `compatible` 오타로 sysfs/ACPI 경로로 붙었을 때). 진짜 보드에서 `regulator_summary`에 `3-0068-vdd`가 `VDD_IMU_1V8` 아래에 있어야 정상이다.

---
## 8. 실수 재현 — 회로도와 DT가 어긋나면 커널에서 이렇게 보인다

네 실수 모두 §7.3에서 본 대로 **dt-schema는 통과한다**. 같은 하네스로 dtb만 바꿔 부팅했다.

### 8.1 예제 11 — `enable-active-high`를 빠뜨린 `regulator-fixed`

`-DBAD_REG`: `reg_imu` 노드에서 `enable-active-high;` 한 줄만 뺐다. `gpio = <&gpio1 10 GPIO_ACTIVE_HIGH>`는 그대로다. 사람 눈에는 "플래그가 ACTIVE_HIGH니까 괜찮겠지"로 보인다.

```text
### dmesg (boot, P4 관련)
[    6.490559][   T45] regulator-imu-1v8 enforce active low on GPIO handle
...
    VDD_IMU_1V8                   0    0      0 unknown  1800mV     0mA  1800mV  1800mV 
 gpio-10  (regulator-imu-1v8) output-high,pull-down
    kworker/u8:4-45      [000] .....     8.443456: regulator_enable: name=VDD_IMU_1V8
    kworker/u8:4-45      [000] .....     8.448710: gpio_value: 530 set 0
    kworker/u8:4-45      [000] .....     8.453805: regulator_enable_complete: name=VDD_IMU_1V8
        modprobe-85      [001] .....     8.458058: regulator_disable: name=VDD_IMU_1V8
        modprobe-85      [001] .....     8.458121: gpio_value: 530 set 1
```

(`bad_reg_base.log`에서 관련 줄만 뽑았다.) 출력에서 볼 것:

- 부팅 로그 한 줄 `enforce active low on GPIO handle` — `drivers/gpio/gpiolib-of.c`의 `of_gpio_set_polarity_by_property()`가 `regulator-fixed`의 `gpio` 속성에 대해서는 **셀의 플래그를 무시하고** `enable-active-high` 유무만으로 극성을 정한다. 소스 주석: "the presence or absence of "enable-active-high" solely controls the polarity of the GPIO line. Any phandle flags must be actively ignored." 반대로 `enable-active-high`가 있는데 플래그가 ACTIVE_LOW면 `GPIO handle specifies active low - ignored` 경고가 나온다.
- 결과: 커널은 레일이 꺼져 있다고(`use 0`) 믿는데 핀은 **high = LDO 켜짐**이다. enable하면 0을 써서 **오히려 끈다**. 드라이버 입장에서는 "전원을 켰는데 칩이 대답을 안 한다", 전류계로는 "꺼야 할 때 전류가 흐른다". 둘 다 원인 찾기가 고약하다.
- 고치는 법: 레귤레이터 EN은 `enable-active-high;`를 쓰고 플래그는 `GPIO_ACTIVE_HIGH`(무시되지만 읽는 사람을 위해 맞춰 둔다). 이 레거시 규칙은 `regulator-fixed`, `reg-fixed-voltage`, `regulator-gpio`에만 있다.

### 8.2 예제 12 — RESETB를 `GPIO_ACTIVE_HIGH`로

`-DBAD_RST`: `reset-gpios = <&gpio1 7 GPIO_ACTIVE_HIGH>`. 회로도 net `CAM_RST_N`의 `_N`을 놓친 경우다.

```text
        modprobe-88      [001] .....     6.121335: gpio_value: 528 set 1
        modprobe-88      [001] .....     6.121350: gpio_direction: 528 out (0)
        modprobe-88      [001] .....     6.121594: gpio_value: 527 set 1
        modprobe-88      [001] .....     6.121599: gpio_direction: 527 out (0)
        modprobe-88      [001] .....     6.154427: gpio_value: 527 set 1
        modprobe-88      [001] .....     6.154445: gpio_value: 528 set 1
        modprobe-88      [001] .....     6.164986: gpio_value: 528 set 0
        modprobe-88      [001] .....     6.166774: gpio_value: 527 set 0
        modprobe-88      [001] .....     6.197657: gpio_value: 528 set 1
```

(`bad_rst_base.log`의 527·528 줄만, 레일 줄 생략.) §7.6의 정상 trace와 비교하면 527(RESETB)의 물리값이 **전부 반대**다. probe 직후 RESETB = 1(리셋 해제 상태로 전원이 들어옴 — 데이터시트 위반), 시퀀스 끝에 RESETB = 0 → **I2C 통신을 시작하는 순간 센서가 리셋에 잡혀 있다**. 실제 보드라면 증상은 "`ov5640_read_reg: error` + `failed to power on`" — 칩이 없을 때와 **똑같다**. 그래서 "칩 ID 읽기 실패"를 보면 (1) 레일, (2) 리셋 극성, (3) 클럭, (4) 주소를 이 순서로 의심한다. 그리고 이 실수를 "드라이버에서 `gpiod_set_value(rst, 0)`으로 리셋 걸기"로 고치면 DT를 고친 다른 보드에서 깨진다(이중 반전). 고칠 곳은 DT 한 비트뿐이다.

### 8.3 예제 13 — CAN STB를 `GPIO_ACTIVE_LOW`로, 그리고 IRQ 타입 반대로

`-DBAD_STB`(`canoff` 모드)와 `-DBAD_IRQ`. 먼저 STB:

```text
        modprobe-89      [000] .....     8.380817: gpio_value: 529 set 0
        modprobe-89      [000] .....     8.380913: gpio_direction: 529 out (0)
          insmod-90      [000] .....     8.448909: gpio_value: 532 set 1
          insmod-90      [000] .....     8.477926: gpio_value: 529 set 1
           rmmod-101     [001] .....    10.627326: gpio_value: 529 set 0
           rmmod-101     [001] .....    10.631990: gpio_value: 532 set 0
 gpio-9   (standby) output-high,pull-down
```

(`bad_stb_canoff.log`의 529·532 줄과, `rmmod` 전에 찍은 debugfs 한 줄.) 출력에서 볼 것: PHY 드라이버가 로드되자마자 STB = 0 → 트랜시버가 **아직 아무도 쓰지 않는데 정상 모드**(5 V도 아직 안 켜졌다). `ip link set can0 up`에 해당하는 `phy_power_on`에서 STB = 1 → **인터페이스는 up인데 트랜시버는 standby** — `candump`에 아무것도 안 오고, 송신은 ACK를 못 받아 error-passive → bus-off로 간다(Q4). `down`하면 그제서야 정상 모드. 증상이 "가끔 되는 것 같다"로 보고되기 쉽다.

다음은 IRQ 타입. 랩의 IRQ 선은 대기 레벨이 0이다(§7.2). 정상 `LEVEL_HIGH`와 실수 `LEVEL_LOW`의 `/proc/interrupts`(드라이버 로드 후 약 1~2초 뒤):

```text
good_canoff.log: 23:          1          0 9030000.pl061   5 Level     p4-can0
bad_irq_base.log: 23:        286          0 9030000.pl061   5 Level     p4-can0
```

`LEVEL_LOW`면 "선이 0인 동안 계속 인터럽트" → **IRQ 폭주**다. 같은 설정을 앞선 실행에서 돌렸을 때는 같은 시점에 34279, 36660회였다 — 횟수는 VM 부하에 따라 크게 달라진다(TCG). 실제 보드에서 같은 일은 두 경우에 생긴다: (1) 극성을 반대로 썼다, (2) OD INT 선에 **풀업이 없어서** 대기 레벨이 0이다. 둘 다 CPU 한 코어가 100%가 되거나, 핸들러가 `IRQ_NONE`을 돌려주면 커널이 "nobody cared"로 그 IRQ를 끈다. edge 타입을 반대로 쓰면 반대 현상 — 이벤트를 **절반만** 받거나(엣지 하나 놓침) 데이터 준비 펄스의 끝에서 받아 지연이 생긴다.

### 8.4 예제 14 — 풀업이 없는 I2C

`-DNO_PULL`: SCL/SDA를 `GPIO_OPEN_DRAIN`으로 요청하고(gpiolib이 "1 = 입력으로 전환해 놓아 주기"로 에뮬레이션) 보드 풀업이 없는 상황이다. gpio-sim은 입력으로 바뀌어도 **마지막에 쓴 값(0)을 그대로 돌려준다**(`gpio_sim_direction_input()`은 방향 비트만 바꾼다) — 놓아 준 선이 다시 올라오지 않는 것, 즉 풀업 없는 OD 선과 같은 동작이다.

```text
[   13.437387][   T87] ov5640 3-003c: ov5640_write_reg: error: reg=3008, val=42
[   14.269218][   T87] ov5640 3-003c: ov5640_write_reg: error: reg=3103, val=11
[   15.101658][   T87] ov5640 3-003c: ov5640_read_reg: error: reg=3108
[   15.102163][   T87] ov5640 3-003c: failed to power on
...
[  122.581667][  T104] reboot: Power down
```

정상 실행(§7.5)과 비교: 같은 세 에러가 정상 dtb에서는 **1 ms 간격**(9.0103 → 9.0114 → 9.0163)이었는데 여기서는 **약 0.83초 간격**이다. 정상 쪽은 NACK(칩이 없음)으로 즉시 실패, 이쪽은 i2c-algo-bit이 SCL이 올라오기를(clock stretching 확인) 기다리다 타임아웃으로 실패한다. `i2cdetect -y 3`은 둘 다 전부 `--`를 찍지만, 부팅 전체가 11초 → 122초로 늘었다. 맨 처음 이 랩을 짤 때 이 상태(gpio-sim + open-drain 에뮬레이션)로 돌렸다가 trace가 `gpio_value: 520 get 0`(SCL 읽기) 수만 줄로 가득 차서 300초 타임아웃에 걸렸다 — 그것이 이 실수의 정확한 모습이다.

실제 보드의 감별법: scope로 SCL을 보면 풀업이 없으면 **한 번 내려간 뒤 올라오지 않거나**(완전 누락), 상승이 RC 곡선으로 느리다(내부 풀업만). 로그 패턴은 `i2c ... timeout`류 메시지 또는 위처럼 에러 간격이 수백 ms. 정상(NACK)은 `-ENXIO`/`-EREMOTEIO`가 즉시 온다. 그래서 **에러 사이 시간 간격**이 첫 감별 단서다.

랩 정상 dtb는 gpio-sim의 이 한계 때문에 `i2c-gpio,sda-open-drain; i2c-gpio,scl-open-drain;`("이 핀은 HW가 이미 OD다, 드라이버는 값만 써라")을 썼다. 실제 SoC I2C 컨트롤러를 쓰면 해당 없음이다.

---

## 9. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="13">GPIO: 극성은 desc 플래그 한 비트</text>
  <rect x="20" y="32" width="150" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="95" y="51" text-anchor="middle" font-size="12">reset-gpios</text>
  <text x="95" y="68" text-anchor="middle" font-size="12">&lt;&amp;gpio1 7 1&gt;</text>
  <rect x="186" y="32" width="150" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="261" y="51" text-anchor="middle" font-size="12">of_find_gpio</text>
  <text x="261" y="68" text-anchor="middle" font-size="12">+ flags quirks</text>
  <rect x="352" y="32" width="150" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="427" y="51" text-anchor="middle" font-size="12">gpio_desc</text>
  <text x="427" y="68" text-anchor="middle" font-size="12">ACTIVE_LOW 비트</text>
  <rect x="518" y="32" width="150" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="593" y="51" text-anchor="middle" font-size="12">set_value(1)</text>
  <text x="593" y="68" text-anchor="middle" font-size="12">→ chip-&gt;set(0)</text>
  <text x="20" y="114" font-size="13">regulator: phandle → rdev → EN GPIO + 지연</text>
  <rect x="20" y="124" width="150" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="95" y="143" text-anchor="middle" font-size="12">vdd-supply</text>
  <text x="95" y="160" text-anchor="middle" font-size="12">&lt;&amp;reg_imu&gt;</text>
  <rect x="186" y="124" width="150" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="261" y="143" text-anchor="middle" font-size="12">regulator_get</text>
  <text x="261" y="160" text-anchor="middle" font-size="12">없으면 dummy/defer</text>
  <rect x="352" y="124" width="150" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="427" y="143" text-anchor="middle" font-size="12">fixed.c rdev</text>
  <text x="427" y="160" text-anchor="middle" font-size="12">ena_gpiod</text>
  <rect x="518" y="124" width="150" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="593" y="143" text-anchor="middle" font-size="12">regulator_enable</text>
  <text x="593" y="160" text-anchor="middle" font-size="12">GPIO → 지연</text>
  <text x="20" y="206" font-size="13">IRQ: 타입은 domain xlate가 꺼내서 irqchip에 설정</text>
  <rect x="20" y="216" width="150" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="95" y="235" text-anchor="middle" font-size="12">interrupts-</text>
  <text x="95" y="252" text-anchor="middle" font-size="12">extended</text>
  <rect x="186" y="216" width="150" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="261" y="235" text-anchor="middle" font-size="12">of_irq_get</text>
  <text x="261" y="252" text-anchor="middle" font-size="12">parse_one</text>
  <rect x="352" y="216" width="150" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="427" y="235" text-anchor="middle" font-size="12">domain xlate</text>
  <text x="427" y="252" text-anchor="middle" font-size="12">hwirq + type</text>
  <rect x="518" y="216" width="150" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="593" y="235" text-anchor="middle" font-size="12">irq_set_type</text>
  <text x="593" y="252" text-anchor="middle" font-size="12">→ 칩 레지스터</text>
  <line x1="170" y1="54" x2="184" y2="54" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="336" y1="54" x2="350" y2="54" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="502" y1="54" x2="516" y2="54" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="170" y1="146" x2="184" y2="146" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="336" y1="146" x2="350" y2="146" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="502" y1="146" x2="516" y2="146" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="170" y1="238" x2="184" y2="238" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="336" y1="238" x2="350" y2="238" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="502" y1="238" x2="516" y2="238" stroke="currentColor" marker-end="url(#ah7)"/>
  <text x="20" y="288" font-size="12">주황 = DT 텍스트, 회색 = OF 파서, 파랑 = 프레임워크 객체, 초록 = 드라이버 호출 → HW</text>
</svg>
```

그림 7 — DT 속성 세 종류가 드라이버 호출까지 가는 경로. 극성 정보가 저장되는 곳(GPIO는 `gpio_desc` 플래그, regulator는 `fixed.c`가 받은 `ena_gpiod`의 플래그, IRQ는 irq_data의 트리거 타입)이 모두 **프레임워크 객체 안**이라서, 드라이버는 극성을 몰라도 된다.

- **GPIO**: `devm_gpiod_get(dev, "reset", flags)` → `gpiod_find_and_request()` → `of_find_gpio()`가 `reset-gpios`/`reset-gpio`를 찾고 `of_xlate`로 (칩, 오프셋, 플래그)를 얻는다 → `of_gpio_flags_quirks()`가 레거시 규칙(§8.1의 regulator, SPI `cs-gpios`, 일부 이더넷 PHY reset)을 적용 → `gpio_desc`에 `FLAG_ACTIVE_LOW`. 이후 `gpiod_set_value()`가 그 비트로 값을 뒤집어 `gc->set()`을 부른다. `gpiod_set_raw_value()`는 뒤집지 않는다 — 드라이버에서 raw를 쓰면 DT 극성을 무시하는 셈이다.
- **regulator**: `regulator_get(dev, "vdd")` → `of_get_regulator()`가 `vdd-supply` phandle을 따라가 `regulator_dev`를 찾는다. provider가 아직 probe 안 됐으면 `-EPROBE_DEFER`, 속성이 아예 없으면 dummy(§7.9에서 본 메시지). `fixed.c`는 probe 때 `gpio` 속성으로 `ena_gpiod`를 받아 두고, enable 시 core의 `_regulator_do_enable()`이 `regulator_ena_gpio_ctrl()`로 핀을 올린 뒤 `startup-delay-us`(레귤레이터의 enable time)만큼 `fsleep()`으로 기다리고 `regulator_enable_complete` 이벤트를 남긴다 — §7.6 trace의 세 줄이 정확히 이 순서였다. `vin-supply`는 `rdev->supply`로 연결돼 자식 enable 전에 부모가 enable된다.
- **IRQ**: `platform_get_irq()`/`fwnode_irq_get_byname()` → `of_irq_parse_one()`이 `interrupts-extended`를 (컨트롤러 노드, 셀들)로 쪼갬 → 컨트롤러의 irq domain을 찾아 `irq_create_fwspec_mapping()` → domain의 `xlate`/`translate`가 (hwirq, type)을 돌려주고 → `irq_set_irq_type()` → irqchip의 `.irq_set_type`(PL061은 IS/IEV/IBE 레지스터). gpio-sim의 `irq_sim` domain은 이 콜백이 없어 타입을 버린다(§7.8).
- **pinctrl**: 드라이버 코드가 아니라 driver core가 `really_probe()`에서 `pinctrl_bind_pins()`로 `default` 상태를 적용한다(H4). 그래서 pinctrl 그룹은 **소비자 노드**에 있어야 그 장치 probe 직전에 적용된다.

드라이버 작성 때 다시 만나는 곳: G3(probe·devm), H3(regulator·reset 소비자 API), H4(gpiod·pinctrl), F6(threaded IRQ — `IRQF_ONESHOT`이 level IRQ에서 필요한 이유), N6(suspend 때 레일을 끄는 순서).

---

## 10. Embedded 실무에서 다시 보기

### 10.1 HW 회로도 리뷰에 SW가 들고 가는 질문

회로도가 확정되기 **전에** SW가 리뷰에 참여하면 DT가 쉬워진다. 센서 키트 보드라면 이 질문을 칩마다 던진다.

1. 이 칩의 reset/enable/standby 핀은 SoC GPIO인가, 풀업·풀다운 고정인가? 기본 상태(SoC가 잡기 전)는 칩에게 안전한가?
2. INT 선은 OD인가 push-pull인가? 풀업은 어느 레일에? 레벨 시프터는 반전형인가? **IRQ 가능한 GPIO 뱅크**에 갔나(일부 SoC는 뱅크마다 IRQ 능력이 다르다)?
3. 레일마다 EN이 SW 제어인가? 켜는 순서 요구가 있으면 `vin-supply`로 강제할 수 있게 전원 트리를 짰나?
4. I2C 주소 충돌은 없나? strap이 GPIO면 리셋 해제 전에 SoC가 레벨을 잡을 수 있나?
5. 디버그용으로 남겨 둘 테스트 포인트(INT, RESETB, 각 레일)가 있나? — §7.6의 trace를 scope와 겹쳐 보려면 필요하다.

### 10.2 bring-up 로그에 남기는 "DT 검증표"

P1의 bring-up 순서에서 "DT 하나씩 활성화" 단계마다 아래 세 명령의 출력을 저장해 두면, 나중에 rev B 보드에서 회귀를 바로 비교할 수 있다.

```sh
cat /sys/kernel/debug/regulator/regulator_summary > rs_$(date +%F).txt
cat /sys/kernel/debug/gpio > gpio_$(date +%F).txt
cat /sys/kernel/debug/devices_deferred; grep -E "gpio|i2c|pl061" /proc/interrupts
```

`devices_deferred`가 비어 있지 않으면 어떤 provider(레귤레이터·GPIO 확장기·클럭)가 없는지 먼저 본다(D3 §10, D4 §11). 이 노트의 모든 실행에서 비어 있었다.

### 10.3 보드 변형과 옵션 보드

같은 메인보드에 CAN 애드온이 있을 때도, 없을 때도 있다면 §7.2처럼 overlay로 분리하고 부트로더나 빌드(`fdtoverlay`)에서 붙인다(D6). 실장 옵션(DNP)이 rev마다 다르면 보드 dts를 rev별로 나누고 공통을 dtsi로(D5). 어느 쪽이든 **회로도 rev 번호를 dts 주석에** 남긴다 — 1년 뒤 "이 `GPIO_ACTIVE_LOW`는 왜?"에 답하는 유일한 근거다.

### 10.4 Don의 경험과 연결

Apple RF bring-up에서 "새 실리콘이 I2C에 대답 안 함"을 디버깅할 때의 순서 — 전원 레일 측정 → 리셋 해제 확인 → 클럭 확인 → 버스 파형 — 가 §8.2의 감별 순서와 같다. 차이는 Linux에서는 그 네 가지가 **네 개의 서로 다른 DT 속성과 프레임워크**로 흩어져 있다는 점뿐이고, 각각을 `regulator_summary`, `debug/gpio`, `clk_summary`, ftrace `i2c` 이벤트로 소프트웨어에서 먼저 확인할 수 있다는 점이 장점이다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `regulator-fixed`에 `enable-active-high` 누락 | 꺼야 할 때 켜지고 켜면 꺼짐, 부팅 로그 `enforce active low` | gpiolib-of 레거시 규칙: 플래그 무시 | `enable-active-high;` 추가 |
| `_N` 핀을 `GPIO_ACTIVE_HIGH`로 | 칩 ID 읽기 실패 (칩 없음과 같은 증상) | 리셋이 거꾸로 걸림 | DT 플래그를 `GPIO_ACTIVE_LOW`로 — 드라이버는 그대로 |
| DT도 ACTIVE_LOW, 드라이버도 0으로 assert | 리셋이 안 걸리거나 늘 걸림 | 이중 반전 | 드라이버는 논리값(1 = assert)만 |
| IRQ 극성 반대 (`LEVEL_LOW` ↔ `HIGH`) | IRQ 폭주(수만 회) 또는 IRQ 없음 | SoC 핀에서 본 극성이 아니라 칩 핀 이름으로 씀 | 시프터 반전 여부까지 포함해 SoC 핀 기준으로 |
| OD INT/I2C에 풀업 없음 (DNP) | IRQ 폭주, I2C 타임아웃(에러 간격 수백 ms) | 놓아 준 선이 안 올라옴 | 풀업 실장 또는 pinctrl 내부 풀업(약함) |
| 레일 순서를 `regulator_bulk_enable`에 기대함 | 가끔 센서가 안 깨어남 (재현 어려움) | bulk enable은 비동기, 순서 없음 | `vin-supply` 체인 또는 드라이버에서 순차 enable |
| `regulator-always-on`을 "일단 켜 두자"로 | 저전력 모드 전류 초과, suspend 실패 | 커널이 레일을 못 끔 | 진짜 SW가 못 끄는 레일에만 |
| pinctrl 그룹을 GPIO 컨트롤러 노드에 걸음 | 핀이 GPIO 기능으로 안 바뀜 / 다른 장치가 먼저 먹스 | 적용 시점이 소비자 probe가 아님 | 소비자 노드에 `pinctrl-0` |
| 8-bit I2C 주소(0xD0)를 `reg`에 | `i2cdetect`에는 0x68인데 probe 안 됨 | 데이터시트의 R/W 포함 주소 | 7-bit 주소 |
| binding에 없는 속성(`phy-supply` on tcan1042) | 동작은 하는데 dt-schema 경고, upstream 거절 | binding `additionalProperties: false` | binding이 허용하는 위치(`xceiver-supply`)로 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "You get a new sensor board and its schematic. Walk me through writing the device tree for an I2C IMU."

**A.** 블록 다이어그램에서 어느 버스인지, 주소 strap으로 주소를 정한다. 전원 트리에서 VDD/VDDIO 레일과 EN 핀을 찾아 `regulator-fixed`로 만들고, INT 선을 SoC 핀까지 따라가 OD·풀업·레벨 시프터 극성을 보고 IRQ 타입을 정한다. 패드 먹스는 pinctrl 그룹으로 소비자 노드에 건다. 마지막에 binding YAML과 드라이버의 con_id를 확인하고 dt-schema를 돌린 뒤, 부팅해서 `regulator_summary`와 `/sys/kernel/debug/gpio`로 확인한다.

> I start from the block diagram to find the bus and use the address strap to get the 7-bit address. Then I follow the power tree: each LDO or load switch with a GPIO enable becomes a `regulator-fixed` node with `enable-active-high` and a startup delay, and the IMU gets `vdd-supply` and `vddio-supply`. I trace the INT net to the SoC pad — open-drain, pull-up, any level shifter — and pick the IRQ type as seen at the SoC pin. Pinmux goes into a pinctrl group on the consumer node. I check the binding and the driver's property names, run dt-schema, and then verify on the board with `regulator_summary`, `debugfs gpio`, and `/proc/interrupts`.

**꼬리 질문**: "dt-schema가 통과하면 끝인가?" → 아니다. 스키마는 극성·순서를 모른다. 극성은 회로도와 대조, 순서는 ftrace/scope로 확인한다.

**Q.** "The reset pin is active-low. What goes in the DT and what does the driver do?"

**A.** DT에 `reset-gpios = <&gpioX n GPIO_ACTIVE_LOW>`. 드라이버는 `gpiod_set_value(reset, 1)`로 리셋을 걸고 0으로 푼다. 극성은 gpiolib이 desc 플래그로 뒤집는다. 드라이버가 0으로 리셋을 걸면 이중 반전이다.

> The DT says `GPIO_ACTIVE_LOW`; the driver always works in logical values — 1 means assert reset, 0 means release — and gpiolib inverts using the descriptor flag. The board-specific polarity lives only in the DT, so the same driver works on a board with an inverting buffer. Using raw values or inverting again in the driver is the classic double-inversion bug.

**Q.** "A `regulator-fixed` with a GPIO enable seems inverted. What's going on?"

**A.** `enable-active-high`가 빠졌을 가능성이 크다. regulator-fixed는 레거시 규칙으로 GPIO 셀의 플래그를 무시하고 그 속성 유무로만 극성을 정한다. 부팅 로그에 `enforce active low on GPIO handle`이 남는다.

> Most likely `enable-active-high` is missing. For fixed regulators gpiolib ignores the flags in the GPIO specifier and uses only that boolean property, so without it the enable is forced active-low. The kernel even logs "enforce active low on GPIO handle". I reproduced it in QEMU: the kernel reported the rail off while the pin was high.

**Q.** "How do you choose between `IRQ_TYPE_EDGE_FALLING` and `IRQ_TYPE_LEVEL_LOW` for a sensor interrupt?"

**A.** 칩이 latch(읽을 때까지 유지)하면 level이 놓치지 않아 안전하고, 짧은 펄스면 edge다. 극성은 SoC 핀 기준. level을 쓰면 threaded 핸들러에 `IRQF_ONESHOT`이 필요하고, 극성이 틀리면 IRQ 폭주가 난다. 일부 드라이버(bmi160, inv_mpu6050)는 DT 타입으로 칩의 INT 설정까지 바꾼다.

> If the device latches its interrupt until the status register is read, level-triggered is safer because you can't miss it; for short pulses use edge. Polarity is what the SoC pin sees, after any level shifter. Level IRQs with threaded handlers need `IRQF_ONESHOT`, and getting the polarity wrong shows up as an interrupt storm. Drivers like bmi160 even program the chip's INT polarity and latch mode from the DT trigger type.

**Q.** "A camera sensor needs DOVDD, then AVDD, then DVDD. How do you guarantee the order?"

**A.** 각 레일에 `startup-delay-us`를 주고, 순서는 `vin-supply` 체인이나 드라이버의 순차 enable로 보장한다. `regulator_bulk_enable()`은 비동기라 순서가 없다 — QEMU에서 trace를 떠 보니 실행마다 순서가 바뀌었다.

> Startup delays go into each regulator node, but ordering is not something `regulator_bulk_enable()` provides — it enables supplies asynchronously, and in my QEMU trace the order changed between runs. If the datasheet requires strict ordering, I either chain them with `vin-supply` when the power tree allows, or have the driver enable them one by one. Then I verify with ftrace regulator events against a scope capture.

**Q.** "Probe fails with 'failed to power on' on a camera. How do you debug?"

**A.** 칩이 대답 안 하는 네 가지 원인 — 레일, 리셋/파워다운 극성, 클럭, 주소 — 순으로. `regulator_summary`로 레일 use count, ftrace `gpio_value`로 RESETB/PWDN 물리 시퀀스, `clk_summary`로 XCLK, `i2cdetect`와 에러 간격으로 NACK vs 타임아웃을 본다.

> I check the four reasons a chip doesn't answer, in order: rails, reset/powerdown polarity, clock, address. `regulator_summary` and the regulator trace events show whether the rails came up; gpio trace events show the physical RESETB/PWDN sequence; `clk_summary` shows XCLK; and the timing of I2C errors tells NACK from a stuck bus — immediate failures mean NACK, hundreds of milliseconds mean a timeout, often a missing pull-up.

**Q.** "What does dt-schema catch and what doesn't it?"

**A.** 속성 이름·타입·개수·필수 여부·노드 이름 규칙을 잡는다(예: tcan1042에 `phy-supply` 거부). 극성, 레일 순서, 회로와의 일치는 못 잡는다.

> It catches structural errors: unknown or disallowed properties, wrong types or counts, missing required properties, node-name patterns. For example it rejects `phy-supply` on a TI TCAN1042 node. It cannot know that a pin is active-low on this board or that rails must come up in a certain order — that's schematic review plus runtime verification.

---

## 13. 직접 해보기

1. (개념) 회로도에 `IMU_INT_N`이 단일 NMOS 반전 버퍼를 지나 SoC에 간다. 칩 INT는 active-low push-pull. DT의 IRQ 타입은? 정답/힌트: SoC 핀에서는 active-high → `IRQ_TYPE_LEVEL_HIGH` 또는 `EDGE_RISING`. `drive-open-drain`은 쓰지 않는다(칩은 push-pull).
2. (개념) PCA9306 시프터의 EN을 레귤레이터 `VDD_IMU_1V8`에 직접 묶으면 DT에서 무엇이 사라지나? 정답/힌트: hog `IMU_LS_OE`가 사라지고, IMU 레일이 꺼지면 시프터도 꺼져 I2C3 버스의 다른 장치(카메라)에 영향이 없는지 회로를 확인해야 한다.
3. (코드) `p4_board.dts`에 `regulator-imu-1v8`의 `off-on-delay-us = <10000>`을 추가하고, `p4.sh`에서 bmi160_i2c를 `rmmod` 후 다시 `modprobe`해 trace의 `regulator_enable` 간격을 재 보라. 정답/힌트: 두 번째 enable이 마지막 disable로부터 10 ms 이상 늦춰진다(`fixed.c`가 `off_on_delay`를 레귤레이터 desc에 넘긴다). 실행 안 함 — 직접 확인.
4. (코드) `reg_cam_avdd`에 `vin-supply = <&reg_cam_dovdd>`를 줘서 DOVDD → AVDD 순서를 강제하고 세 번 부팅해 trace 순서를 비교하라. 정답/힌트: AVDD와 DVDD는 언제나 DOVDD 뒤. 둘 사이 순서는 여전히 무작위.
5. (관찰) 정상 dtb로 부팅한 뒤 `echo 1 > /sys/kernel/tracing/events/i2c/enable`을 켜고 `modprobe ov5640`을 하라. 어떤 주소·레지스터를 쓰려다 실패하나? 정답/힌트: 0x3c에 `0x3008 = 0x42`(소프트웨어 standby) 쓰기가 첫 실패 — dmesg의 `reg=3008, val=42`와 같다.
6. (관찰) `CONFIG_COMMON_CLK_GPIO`가 켜진 커널이라면 `cam-osc-en-hog` 대신 `gpio-gate-clock`을 쓰는 DT를 써 보라. 무엇이 달라지나? 정답/힌트: 오실레이터 OE가 `clk_prepare_enable(xclk)` 시점에 켜져서 센서가 쓰지 않을 때 꺼진다(전력 절약). `clocks = <&osc_24m>`, `enable-gpios`, `#clock-cells = <0>`. 이 랩 커널에는 꺼져 있어 실행 안 함.
7. (리뷰) §6의 `imx_p4.dts`에서 카메라 노드를 i.MX8MP `CLKOUT1`을 XCLK로 쓰는 버전으로 써 보라(EVK 예: `imx8mm-evk.dtsi`의 `assigned-clock-parents`). 정답/힌트: `clocks = <&clk IMX8MP_CLK_CLKOUT1>`, `assigned-clocks`/`assigned-clock-parents = <&clk IMX8MP_CLK_24M>`/`assigned-clock-rates = <24000000>`, 그리고 CLKO1 패드(`MX8MP_IOMUXC_GPIO1_IO14__CCM_CLKO1`)를 pinctrl 그룹에.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| net | 전기적으로 연결된 선 묶음 | 회로도에서 이름으로 페이지를 넘어 이어진다 |
| ball / pad | BGA 좌표 / 다이의 I/O 셀 | ball C14 = pad SAI1_RXD0 — DT는 pad와 기능 이름을 쓴다 |
| pinmux / ALT | 패드 기능 선택 | `MX8MP_IOMUXC_패드__기능` 매크로 |
| open-drain (OD) | 끌어내리기만 하는 출력 | 풀업이 있어야 1 |
| 레벨 시프터 | 전압 도메인 변환 | 반전형인지가 IRQ 극성을 바꾼다 |
| DNP | Do Not Populate | 풀업이 그려져 있어도 없을 수 있다 |
| strap | 리셋 때 읽는 설정 핀 | I2C 주소 LSB, PHY 주소 |
| `regulator-fixed` | 전압 고정 + GPIO EN 레일 | LDO EN, 로드 스위치 |
| `enable-active-high` | regulator-fixed의 극성 속성 | GPIO 셀 플래그보다 우선 |
| `startup-delay-us` | 레일 켠 뒤 대기 | 소비자의 enable이 이만큼 늦게 돌아온다 |
| `vin-supply` | 레일의 입력 레일 | 부모가 먼저 켜진다 |
| GPIO hog | 드라이버 없이 고정되는 핀 | 시프터 OE, 오실레이터 OE |
| `interrupts-extended` | 부모 지정 + 인터럽트 한 줄 | `<&ctrl hwirq type>` |
| `IRQ_TYPE_*` | 트리거 타입 | RISING 1, FALLING 2, LEVEL_HIGH 4, LEVEL_LOW 8 |
| con_id | 소비자 이름 | `"reset"` → `reset-gpios`, `"vdd"` → `vdd-supply` |
| 이중 반전 | 극성을 DT와 드라이버에서 두 번 뒤집음 | 결과적으로 정반대 동작 |
| gpio-sim | 커널 가짜 GPIO 칩 | 랩에서 SoC GPIO 대역 |
| i2c-gpio | GPIO bit-bang I2C 버스 | 랩에서 I2C3 대역 |

---

## 15. 요약 & 체크리스트

회로도 → DT 번역은 "선 하나를 끝까지 따라가 어느 프레임워크가 받는지 정하는 일"이다. 버스와 주소는 노드 위치와 `reg`, 패드 기능과 풀은 pinctrl, 레일과 EN은 `regulator-fixed`(+`enable-active-high`, `startup-delay-us`, `vin-supply`), 칩의 전원 핀은 `*-supply`, reset/enable/standby는 `*-gpios` + 극성 플래그, INT는 `interrupts-extended` + SoC 핀에서 본 `IRQ_TYPE`, 오실레이터는 `fixed-clock`이 된다. 속성 이름은 binding과 드라이버의 con_id가 정한다. dt-schema는 구조만 검사하므로 극성과 순서는 회로도 대조와 런타임 확인(`regulator_summary`, `debug/gpio`, `/proc/interrupts`, ftrace)으로 닫는다. QEMU 랩에서 확인한 대로 극성 실수는 하나같이 "칩이 대답 안 함" 또는 "IRQ 폭주"로 보이며, `regulator_bulk_enable()`은 레일 순서를 보장하지 않는다.

- [ ] 회로도 net 이름에서 극성·전압 도메인·차동쌍을 구분해 읽을 수 있다
- [ ] SoC 패드 이름과 GPIO 뱅크·번호, pinfunc 매크로의 관계를 설명할 수 있다
- [ ] OD INT 선에서 풀업·레벨 시프터를 보고 SoC 핀 기준 `IRQ_TYPE`을 정할 수 있다
- [ ] LDO + EN 핀을 `regulator-fixed`로 쓰고 `enable-active-high` 레거시 규칙을 설명할 수 있다
- [ ] 칩 하나에 대해 "net → property" 표를 채우고 DT 노드를 쓸 수 있다
- [ ] `reset-gpios`의 `GPIO_ACTIVE_LOW`와 드라이버 논리값의 분업을 설명하고 이중 반전을 피할 수 있다
- [ ] binding과 드라이버 소스에서 con_id(`powerdown`, `standby`, `DOVDD`)를 찾을 수 있다
- [ ] dt-schema로 보드 dtb를 검사하고, 잡는 것과 못 잡는 것을 구분할 수 있다
- [ ] `regulator_summary`·`/sys/kernel/debug/gpio`·`/proc/interrupts`로 DT가 의도대로 적용됐는지 확인할 수 있다
- [ ] ftrace regulator/gpio/clk 이벤트로 전원 시퀀스를 재고 데이터시트 요구와 비교할 수 있다
- [ ] "칩 ID 읽기 실패"와 "I2C 타임아웃"을 에러 간격으로 감별할 수 있다
- [ ] 레일 순서 요구를 `vin-supply` 또는 드라이버로 보장해야 하는 이유를 설명할 수 있다

## 참고 자료

- docs.kernel.org — GPIO Descriptor Consumer Interface (`driver-api/gpio/consumer`): 논리값과 raw 값, active-low: https://docs.kernel.org/driver-api/gpio/consumer.html
- docs.kernel.org — Voltage and current regulator API (`driver-api/regulator`): https://docs.kernel.org/driver-api/regulator.html
- docs.kernel.org — Linux generic IRQ handling / IRQ domain (`core-api/irq/irq-domain`): https://docs.kernel.org/core-api/irq/irq-domain.html
- 커널 소스(6.18.55): `drivers/gpio/gpiolib-of.c`(`of_gpio_set_polarity_by_property`, `of_gpio_quirk_polarity`), `drivers/regulator/fixed.c`, `drivers/media/i2c/ov5640.c`(`ov5640_powerup_sequence`), `drivers/iio/imu/bmi160/bmi160_core.c`, `drivers/phy/phy-can-transceiver.c`, `kernel/irq/irq_sim.c`, `drivers/gpio/gpio-sim.c` — elixir.bootlin.com에서 같은 경로로 볼 수 있다
- 커널 binding: `Documentation/devicetree/bindings/regulator/fixed-regulator.yaml`, `media/i2c/ovti,ov5640.yaml`, `iio/imu/bosch,bmi160.yaml`, `phy/ti,tcan104x-can.yaml`, `pinctrl/fsl,imx8m-pinctrl.yaml`, `i2c/i2c-gpio.yaml`
- 실제 보드 DT: `arch/arm64/boot/dts/freescale/imx8mm-evk.dtsi`(OV5640), `imx93-phyboard-nash.dts`(TCAN1042 PHY), `imx8mp-evk.dts`(레귤레이터 방식 CAN standby, I2C3 패드값)
- dt-schema: https://github.com/devicetree-org/dt-schema
- 데이터시트(직접 확인 필요): Bosch BMI160, OmniVision OV5640, TI TCAN1042, NXP PCA9306, NXP i.MX 8M Plus Reference Manual(IOMUXC 장)
- 이 사이트의 관련 노트: D2, D4, D6, D7(DT 문법·자원·overlay·검증), H3·H4·H5(regulator·GPIO/pinctrl·I2C), P1(bring-up 순서), P3(데이터시트 → 드라이버), Q2(카메라 파이프라인 endpoint), Q4(CAN 실무), M(ftrace)
