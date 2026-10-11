# H4. pinctrl과 GPIO — 핀 먹스, pinctrl state, gpiolib, gpiod API, GPIO→IRQ, 유저 공간 uAPI

> **이 노트를 다 읽으면**: SoC 패드 하나가 GPIO·UART·I2C·PWM 중 무엇이 될지 정하는 핀 먹스와 패드 설정(풀업·구동 세기·슈미트)을 DT 한 줄에서 해독할 수 있다 · pinctrl state(`default`/`sleep`/`init`/`idle`)가 언제 누구에 의해 바뀌는지 설명하고 debugfs로 확인할 수 있다 · `devm_gpiod_get` + 논리값/물리값(ACTIVE_LOW) + `_cansleep` + `gpiod_to_irq`로 센서 reset/enable/INT 핀을 다루는 드라이버를 짤 수 있다 · `/dev/gpiochipN` uAPI v2로 라인 요청·엣지 이벤트·라인 감시를 C로 짜고, 이벤트 유실이 어디서 생기는지 측정으로 보일 수 있다
> **왜 필요한가**: "센서 리셋 핀이 반대로 동작한다", "INT가 안 들어온다", "부팅 때 모터 enable 핀이 잠깐 튄다", "I2C 핀이 GPIO로 먹스돼 있어서 버스가 죽었다" — 보드 bring-up 첫 주에 반드시 만나는 장면이고, 면접에서는 "active-low를 드라이버가 어떻게 처리하나", "sysfs GPIO는 왜 폐기됐나", "`gpiod_set_value`와 `_cansleep`의 차이"로 나온다
> **Don 기준 난이도**: IOMUX 레지스터, 풀업/구동 세기, GPIO 방향·데이터 레지스터, 엣지/레벨 인터럽트는 MCU 펌웨어에서 매일 하던 일 / 새로 배울 것은 "그 설정을 DT 데이터로 적고, pinctrl core와 gpiolib이 소유권·극성·상태 전환·유저 공간 인터페이스까지 대신 관리하는 구조"
> **선행 노트**: A4(poll, gpio-sim POLLPRI), A10(libgpiod CLI), D4(`*-gpios`·`pinctrl-*` DT 문법), F6(threaded IRQ), G2–G3(platform 드라이버·probe)

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 펌웨어에서는 이렇게 했다

MCU 펌웨어에서 센서 하나를 붙일 때 핀 관련 코드는 보통 이랬다.

```c
/* bare-metal 시절: 보드 헤더 + init 함수 */
IOMUXC->SW_MUX_CTL[I2C3_SCL] = ALT0 | SION;      /* 이 패드는 I2C3 SCL */
IOMUXC->SW_PAD_CTL[I2C3_SCL] = PE | PU | HYS | DSE_X4;
GPIO3->GDIR |= (1u << 16);                         /* LED 핀 출력 */
GPIO3->DR   &= ~(1u << 16);                        /* LED 끔 (active-low면 반대!) */
GPIO5->ICR1 = (GPIO5->ICR1 & ~(3u << 4)) | (3u << 4); /* INT 핀 falling edge */
NVIC_EnableIRQ(GPIO5_IRQn);
```

이 코드에는 세 종류의 정보가 섞여 있다.

1. **패드가 무슨 기능인가** (먹스) — 보드마다 다르다.
2. **패드의 전기적 성질** (풀업, 구동 세기, 슈미트) — 보드 배선에 따라 다르다.
3. **그 핀으로 무엇을 하는가** (리셋을 건다, INT를 기다린다) — 드라이버(장치)의 로직이다.

Linux는 1·2를 **pinctrl 서브시스템**이, 3을 **gpiolib**이 맡게 나눴다. 보드 정보(1·2, 극성, 핀 번호)는 DT에 적고, 드라이버는 "reset 핀을 assert해라" 같은 **의미만** 말한다. 그래서 같은 IMU 드라이버가 i.MX8, AM62, Jetson에서 코드 수정 없이 돈다.

### 0.2 패드 하나, 기능 여러 개

SoC 칩의 볼(ball)은 수백 개뿐인데 내부 IP 블록의 신호는 수천 개다. 그래서 패드 하나에 여러 신호를 연결해 두고 **먹스(multiplexer) 레지스터로 하나를 고른다**. i.MX8MP의 `I2C3_SCL` 패드는 커널 소스의 `imx8mp-pinfunc.h`에 다섯 개의 얼굴이 정의돼 있다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="30" width="170" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="95" y="50" text-anchor="middle" font-size="13">ALT0  I2C3_SCL</text>
  <rect x="10" y="72" width="170" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="92" text-anchor="middle" font-size="13">ALT1  PWM4_OUT</text>
  <rect x="10" y="114" width="170" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="134" text-anchor="middle" font-size="13">ALT2  GPT2_CLK</text>
  <rect x="10" y="156" width="170" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="176" text-anchor="middle" font-size="13">ALT3  ECSPI2_SCLK</text>
  <rect x="10" y="198" width="170" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="95" y="218" text-anchor="middle" font-size="13">ALT5  GPIO5_IO18</text>
  <line x1="180" y1="45" x2="238" y2="70" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="180" y1="87" x2="238" y2="100" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="180" y1="129" x2="238" y2="130" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="180" y1="171" x2="238" y2="160" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="180" y1="213" x2="238" y2="190" stroke="currentColor" marker-end="url(#ah1)"/>
  <polygon points="240,30 300,60 300,200 240,230" fill="#888" fill-opacity="0.12" stroke="currentColor"/>
  <text x="270" y="134" text-anchor="middle" font-size="13">MUX</text>
  <text x="270" y="252" text-anchor="middle" font-size="12">MUX_CTL 0x210</text>
  <text x="270" y="268" text-anchor="middle" font-size="12">= ALT0 + SION</text>
  <line x1="300" y1="115" x2="338" y2="115" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="340" y="30" width="190" height="160" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="435" y="52" text-anchor="middle" font-size="13">패드 설정 PAD_CTL 0x470</text>
  <text x="352" y="80" font-size="12">PE · PU : 풀업/풀다운</text>
  <text x="352" y="104" font-size="12">DSE : 구동 세기 X1~X6</text>
  <text x="352" y="128" font-size="12">FSEL : 슬루 레이트</text>
  <text x="352" y="152" font-size="12">ODE : 오픈 드레인</text>
  <text x="352" y="176" font-size="12">HYS : 슈미트 입력</text>
  <line x1="530" y1="110" x2="588" y2="110" stroke="currentColor" marker-end="url(#ah1)"/>
  <circle cx="620" cy="110" r="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="620" y="115" text-anchor="middle" font-size="13">패드</text>
  <text x="620" y="160" text-anchor="middle" font-size="12">I2C3_SCL 볼</text>
  <rect x="340" y="206" width="190" height="48" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="435" y="226" text-anchor="middle" font-size="12">SELECT_INPUT 0x5B4</text>
  <text x="435" y="244" text-anchor="middle" font-size="12">입력 경로 선택 (daisy)</text>
  <path d="M620,170 L620,230 L534,230" fill="none" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="628" y="205" font-size="12">입력</text>
</svg>
```
그림 1 — i.MX8MP의 `I2C3_SCL` 패드 하나. 먹스 레지스터(0x210)가 다섯 기능 중 하나를 고르고, 패드 설정 레지스터(0x470)가 전기적 성질을, SELECT_INPUT(0x5B4)이 "여러 패드 중 어느 것을 I2C3 SCL 입력으로 쓸지"를 정한다. 숫자는 커널 소스 `imx8mp-pinfunc.h`의 `MX8MP_IOMUXC_I2C3_SCL__*` 매크로 값이다.

여기서 GPIO는 **다섯 기능 중 하나(ALT5)**일 뿐이다. 이 사실이 pinctrl과 gpiolib의 관계를 결정한다: GPIO로 쓰려면 먼저 먹스가 GPIO 쪽이어야 하고, I2C로 쓰는 동안에는 GPIO로 건드리면 안 된다.

### 0.3 Linux에서 누가 무엇을 맡나

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="14" width="640" height="36" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="37" text-anchor="middle" font-size="13">유저 공간 — libgpiod 도구, h4_mon 같은 앱 → ioctl(/dev/gpiochipN)</text>
  <rect x="20" y="66" width="420" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="230" y="86" text-anchor="middle" font-size="13">consumer 드라이버 (예: h4_sensor)</text>
  <text x="230" y="106" text-anchor="middle" font-size="12">devm_gpiod_get · gpiod_to_irq · pinctrl_select_state</text>
  <rect x="20" y="136" width="300" height="54" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="158" text-anchor="middle" font-size="13">pinctrl core</text>
  <text x="170" y="178" text-anchor="middle" font-size="12">state · map · 소유권(mux_owner)</text>
  <rect x="360" y="136" width="300" height="54" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="158" text-anchor="middle" font-size="13">gpiolib + gpiolib-cdev</text>
  <text x="510" y="178" text-anchor="middle" font-size="12">gpio_desc · 논리/물리값 · IRQ · uAPI</text>
  <text x="340" y="207" text-anchor="middle" font-size="12">gpio-ranges</text>
  <line x1="322" y1="163" x2="358" y2="163" stroke="currentColor" marker-end="url(#ah2)" marker-start="url(#ah2)"/>
  <rect x="20" y="216" width="300" height="54" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="238" text-anchor="middle" font-size="13">pin controller 드라이버</text>
  <text x="170" y="258" text-anchor="middle" font-size="12">pinctrl-imx, pinctrl-single …</text>
  <rect x="360" y="216" width="300" height="54" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="238" text-anchor="middle" font-size="13">gpio_chip 드라이버</text>
  <text x="510" y="258" text-anchor="middle" font-size="12">gpio-mxc, pl061, gpio-sim, I2C 확장기</text>
  <rect x="20" y="290" width="300" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="170" y="317" text-anchor="middle" font-size="13">IOMUXC 레지스터 (먹스 · 패드)</text>
  <rect x="360" y="290" width="300" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="317" text-anchor="middle" font-size="13">GPIO 블록 (DR · GDIR · PSR · ICR)</text>
  <line x1="230" y1="50" x2="230" y2="64" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="560" y1="50" x2="560" y2="134" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="170" y1="116" x2="170" y2="134" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="400" y1="116" x2="400" y2="134" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="170" y1="190" x2="170" y2="214" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="510" y1="190" x2="510" y2="214" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="170" y1="270" x2="170" y2="288" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="510" y1="270" x2="510" y2="288" stroke="currentColor" marker-end="url(#ah2)"/>
</svg>
```
그림 2 — 핀 관련 커널 스택. 왼쪽 기둥(pinctrl)은 "패드가 무엇인가", 오른쪽 기둥(gpiolib)은 "GPIO 라인으로 무엇을 하나"를 맡는다. 둘은 `gpio-ranges`로 연결된다: GPIO를 요청하면 gpiolib이 pinctrl에 "이 패드를 GPIO로 먹스해 달라"고 부탁할 수 있다. 유저 공간은 드라이버를 거치지 않고 gpiolib-cdev로 바로 들어온다.

비유로 정리하면:

| 개념 | 펌웨어에서 하던 것 | Linux에서 |
|---|---|---|
| 핀 먹스 | `SW_MUX_CTL` 레지스터 쓰기 | pin controller 드라이버 + DT `pinctrl-0` |
| 패드 설정 | `SW_PAD_CTL` 레지스터 쓰기 | 같은 DT 그룹의 config 값 (pinconf) |
| 저전력 진입 시 핀 재설정 | sleep 함수 안에서 직접 | pinctrl `sleep` state를 PM 코드가 선택 |
| GPIO 출력/입력 | `GDIR`, `DR`, `PSR` 직접 | `gpio_chip` 콜백(provider) + `gpiod_*`(consumer) |
| active-low 처리 | 코드 곳곳의 `~` | DT 플래그 하나, gpiolib이 반전 |
| GPIO 인터럽트 | `ICR`·`IMR` 쓰고 벡터 등록 | `gpiod_to_irq` → `request_threaded_irq` |
| 테스트용 핀 흔들기 | JTAG로 레지스터 쓰기 | `/dev/gpiochipN` uAPI, libgpiod |

---

## 1. 실습 판 깔기 — QEMU virt에 "가짜 패드 컨트롤러 + 가짜 GPIO 칩 + 센서 노드"

이 노트의 실습은 전부 공용 랩(`/var/lab`, Linux 6.18.55 + BusyBox initramfs)을 QEMU `virt` 머신으로 부팅해서 게스트 안에서 돌린다. 빌드 호스트는 Lima VM `elinux`(`uname -r` = `7.0.0-38-generic`)이고, 게스트 커널은 `6.18.55`다.

### 1.1 virt에는 무엇이 있고 무엇이 없나

| 필요한 것 | virt 머신에 | 이 노트의 해결 |
|---|---|---|
| GPIO 컨트롤러 | PL061 하나 (`pl061@9030000`, 8핀, 3번은 poweroff 키) | 그대로 쓴다 (enable 핀) |
| 외부에서 흔들 수 있는 입력 핀 | 없음 (QEMU가 PL061 입력을 바꿔 주지 않음) | **gpio-sim** — 커널의 가짜 GPIO 칩. `pull` 파일에 쓰면 입력값이 바뀌고 IRQ가 난다 |
| pin controller | 없음 | **pinctrl-single** — 범용 "레지스터 하나 = 패드 하나" 드라이버. 예약 RAM 32바이트를 가짜 패드 레지스터 8개로 쓴다 |
| 센서 | 없음 | `h4-sensor` DT 노드 + 우리 드라이버 `h4_sensor` |

랩 커널 설정에서 관련 옵션은 이렇다 (게스트 안에서 `/proc/config.gz`를 읽은 실제 결과, 필요한 줄만 남김).

```sh
zcat /proc/config.gz | grep -E "CONFIG_GPIO_(SYSFS|CDEV|CDEV_V1|SIM|PL061)[= ]|CONFIG_PINCTRL_SINGLE[= ]|CONFIG_PINCTRL=|is not set" | grep -E "GPIO_|PINCTRL"
ls /sys/class/gpio
```

```text
CONFIG_PINCTRL=y
CONFIG_PINCTRL_SINGLE=y
CONFIG_GPIO_CDEV=y
CONFIG_GPIO_CDEV_V1=y
CONFIG_GPIO_PL061=y
CONFIG_GPIO_SIM=y
ls: /sys/class/gpio: No such file or directory
```

출력에서 볼 것: `CONFIG_GPIO_SYSFS`는 "is not set" 줄조차 없다. 6.18 Kconfig에서 이 옵션은 `bool "/sys/class/gpio/... (sysfs interface)" if EXPERT`라서 **EXPERT를 켜야 메뉴에 보인다**. 레거시 sysfs GPIO가 얼마나 뒤로 밀려났는지 보여 주는 증거다(§7.1). 그래서 `/sys/class/gpio`도 없다.

### 1.2 예제 1 — DT 조각: 패드 컨트롤러, gpio-sim, 센서, 그리고 "도둑" 두 개

무엇을 확인하나: 이 노트의 모든 실험을 한 DT에 담는다. `virt.dts` 사본 끝에 `#include`해서 C 전처리 → dtc로 dtb를 만든다 (D4와 같은 방식).

```dts
/* h4_nodes.dtsi (앞부분) — 가짜 패드 레지스터와 pinctrl-single */
#include <dt-bindings/gpio/gpio.h>

/ {
	reserved-memory {
		#address-cells = <2>;
		#size-cells = <2>;
		ranges;
		h4-padram@5ff00000 {			/* 가짜 "패드 레지스터" 8개 = RAM 32바이트 */
			reg = <0x0 0x5ff00000 0x0 0x1000>;
			no-map;
		};
	};

	h4_pmx: pinmux@5ff00000 {
		compatible = "pinconf-single";
		reg = <0x0 0x5ff00000 0x0 0x20>;	/* 32비트 레지스터 8개 = 패드 8개 */
		#pinctrl-cells = <1>;
		pinctrl-single,register-width = <32>;
		pinctrl-single,function-mask = <0x7>;	/* [2:0] MUX_MODE */

		h4_pins_default: h4-default-pins {
			pinctrl-single,pins = <0x00 0x2  0x04 0x2>;	/* pad0,1 → ALT2 (I2C) */
			pinctrl-single,bias-pullup = <0x40 0x40 0x0 0x40>;
			pinctrl-single,bias-pulldown = <0x0 0x80 0x0 0x80>;
			pinctrl-single,drive-strength = <0x30 0x30>;
			pinctrl-single,input-schmitt-enable = <0x100 0x100 0x0 0x100>;
		};
		h4_pins_sleep: h4-sleep-pins {
			pinctrl-single,pins = <0x00 0x0  0x04 0x0>;	/* pad0,1 → ALT0 (GPIO) */
			pinctrl-single,bias-pullup = <0x0 0x40 0x0 0x40>;
			pinctrl-single,bias-pulldown = <0x80 0x80 0x0 0x80>;
			pinctrl-single,drive-strength = <0x0 0x30>;
			pinctrl-single,input-schmitt-enable = <0x0 0x100 0x0 0x100>;
		};
	};
```

가짜 패드 레지스터의 비트 배치는 i.MX를 흉내 내서 이렇게 정했다: `[2:0]` MUX_MODE, `[5:4]` 구동 세기, bit 6 풀업, bit 7 풀다운, bit 8 슈미트. pinctrl-single은 "패드 하나 = 레지스터 하나"인 SoC(TI OMAP/AM33xx, HiSilicon 등)를 위한 범용 드라이버라서, 레지스터 폭·먹스 마스크·각 설정 비트의 위치를 전부 DT로 받는다. `no-map` 예약 메모리라 커널의 일반 RAM 할당자가 이 32바이트를 건드리지 않는다.

```dts
/* h4_nodes.dtsi (뒷부분) — gpio-sim 칩, 센서, 도둑 노드, 두 번째 CPU */
	h4-gpio-sim {
		compatible = "gpio-simulator";
		h4_sim: bank0 {
			gpio-controller;
			#gpio-cells = <2>;
			ngpios = <8>;
			gpio-sim,label = "h4_sim";
			gpio-line-names = "IMU_RST_N", "IMU_EN", "IMU_INT", "BTN",
					  "", "", "", "LED_HOG";
			led-hog {
				gpio-hog;
				gpios = <7 GPIO_ACTIVE_HIGH>;
				output-high;
				line-name = "h4-led-hog";
			};
		};
	};

	h4-sensor {
		compatible = "dons,h4-sensor";
		pinctrl-names = "default", "sleep";
		pinctrl-0 = <&h4_pins_default>;
		pinctrl-1 = <&h4_pins_sleep>;
		reset-gpios = <&h4_sim 0 GPIO_ACTIVE_LOW>;
		enable-gpios = <&gpio 4 GPIO_ACTIVE_HIGH>;	/* PL061 4번 핀 */
		irq-gpios = <&h4_sim 2 GPIO_ACTIVE_HIGH>;
	};

	h4-thief-gpio {					/* 같은 리셋 핀을 또 달라는 노드 */
		compatible = "dons,h4-thief";
		reset-gpios = <&h4_sim 0 GPIO_ACTIVE_LOW>;
	};

	h4-thief-pins {					/* 같은 패드를 또 먹스하려는 노드 */
		compatible = "dons,h4-thief";
		pinctrl-names = "default";
		pinctrl-0 = <&h4_pins_default>;
	};
};

&{/cpus} {					/* virt.dts는 -smp 1로 덤프됨 → CPU 하나 추가 */
	cpu@1 {
		reg = <0x1>;
		compatible = "arm,cortex-a57";
		device_type = "cpu";
		enable-method = "psci";
	};
};
```

요점:

- gpio-sim은 보통 configfs로 만들지만(§7.3), `compatible = "gpio-simulator"`로 **DT에서도** 만들 수 있다 (`drivers/gpio/gpio-sim.c`의 `gpio_sim_of_match`). DT로 만들어야 다른 노드가 `<&h4_sim 0 ...>`처럼 phandle로 참조할 수 있다.
- `gpio-line-names`는 라인에 사람이 읽을 이름을 붙인다. 회로도 net 이름(`IMU_RST_N`)을 그대로 쓰면 현장에서 `gpioinfo` 한 번으로 핀을 찾는다.
- `led-hog`는 **GPIO hog**: 어떤 드라이버도 안 쓰지만 부팅 때 정해진 값으로 붙잡아 둘 핀(예: 다른 칩의 strap, 항상 켜둘 전원 스위치)이다. gpiolib이 칩 등록 시점에 요청해 버린다.
- `virt.dts`는 `-smp 1`로 덤프한 것이라 CPU 노드가 하나뿐이다. 측정(§8)에서 리더 스레드와 토글 스레드를 병렬로 돌리려고 `cpu@1`을 붙이고 `-smp 2`로 부팅한다.

빌드 스크립트:

```sh
#!/bin/sh
# mkdtb.sh <이름> [-DXXX] → /var/tmp/H4/<이름>.dtb
set -e
W=/var/tmp/H4; K=/var/lab/src/linux-6.18.55/include; N=$1; shift
mkdir -p $W
sed -e 's/^\tpl061@9030000 {/\tgpio: pl061@9030000 {/' /var/lab/out/virt.dts > $W/h4_virt.dts
echo '#include "/Users/donh/.elinux_scratch/H4/h4_nodes.dtsi"' >> $W/h4_virt.dts
cpp -nostdinc -I $K -undef -x assembler-with-cpp "$@" $W/h4_virt.dts > $W/$N.pp.dts
dtc -q -I dts -O dtb -o $W/$N.dtb $W/$N.pp.dts
```

부팅 스크립트는 랩 rootfs를 복사해 `.ko`, 정적 링크한 uAPI 프로그램들, 시나리오 스크립트 `h4.sh`를 넣고, `LAB_CMD`로 시나리오 하나를 실행한 뒤 전원을 끈다.

```sh
#!/bin/sh
# boot.sh <dtb이름> <시나리오>  — initramfs에 .ko·정적 바이너리·h4.sh를 넣고 부팅
set -e
W=/var/tmp/H4; S=/Users/donh/.elinux_scratch/H4
rm -rf $W/r && mkdir $W/r && cp -a /var/lab/build/rootfs/. $W/r/
cp $W/mod/h4_sensor.ko $W/bin/* $S/h4.sh $W/r/
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout 600 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd $W/i.gz -dtb $W/$1.dtb \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/h4.sh~$2"
```

게스트는 BusyBox뿐이라 libgpiod 도구가 없다. 그래서 유저 공간 실험은 `linux/gpio.h`의 uAPI v2를 직접 쓰는 C 프로그램을 VM에서 `gcc -static`으로 빌드해 넣는다.

```sh
cd /Users/donh/.elinux_scratch/H4
for p in h4_lsgpio h4_line h4_mon h4_watch h4_rate h4_lat; do
  gcc -std=gnu11 -Wall -Wextra -O2 -static -pthread $p.c -o /var/tmp/H4/bin/$p
done
```

여섯 개 모두 경고 0개로 빌드됐다. 각 프로그램은 쓰이는 절에서 소스와 함께 본다.

---

## 2. 핀 먹스와 패드 설정 — 실제 SoC DT 한 줄을 해독하기

virt에는 진짜 pin controller가 없으니, 먼저 **진짜 SoC의 DT를 손으로 해독**해 본다. 랩 소스 트리의 i.MX8MP EVK 보드 파일이다.

### 2.1 i.MX8MP EVK의 I2C3 핀 그룹

```dts
/* arch/arm64/boot/dts/freescale/imx8mp-evk.dts (발췌) */
&i2c3 {
	clock-frequency = <400000>;
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_i2c3>;
	status = "okay";
	/* ... codec@1a ... */
};

&iomuxc {
	pinctrl_i2c3: i2c3grp {
		fsl,pins = <
			MX8MP_IOMUXC_I2C3_SCL__I2C3_SCL		0x400001c2
			MX8MP_IOMUXC_I2C3_SDA__I2C3_SDA		0x400001c2
		>;
	};
};
```

매크로는 `imx8mp-pinfunc.h`에 있다 (실제 파일 내용).

```text
#define MX8MP_IOMUXC_I2C3_SCL__I2C3_SCL       0x210 0x470 0x5B4 0x0 0x4
#define MX8MP_IOMUXC_I2C3_SCL__PWM4_OUT       0x210 0x470 0x000 0x1 0x0
#define MX8MP_IOMUXC_I2C3_SCL__GPT2_CLK       0x210 0x470 0x000 0x2 0x0
#define MX8MP_IOMUXC_I2C3_SCL__ECSPI2_SCLK    0x210 0x470 0x568 0x3 0x2
#define MX8MP_IOMUXC_I2C3_SCL__GPIO5_IO18     0x210 0x470 0x000 0x5 0x0
```

같은 패드라서 앞의 두 숫자(먹스 레지스터 0x210, 패드 레지스터 0x470)는 다섯 줄 모두 같고, 고르는 값(`mux_mode`)만 0,1,2,3,5로 다르다. 그래서 `fsl,pins` 한 항목은 6개 숫자다.

| 위치 | 이름 | I2C3_SCL 값 | 의미 |
|---|---|---|---|
| 1 | `mux_reg` | `0x210` | IOMUXC(0x30330000) + 0x210 = 이 패드의 먹스 레지스터 |
| 2 | `conf_reg` | `0x470` | 이 패드의 패드 설정 레지스터 |
| 3 | `input_reg` | `0x5B4` | I2C3 SCL 입력을 어느 패드에서 받을지 고르는 레지스터 |
| 4 | `mux_val` | `0x0` | ALT0 = I2C3_SCL |
| 5 | `input_val` | `0x4` | `input_reg`에 쓸 값 |
| 6 | `pad_setting` | `0x400001c2` | 패드 설정값 (+ 최상위 SION 플래그) |

여섯 번째 값을 같은 헤더의 매크로로 풀면 (실제 파일의 정의):

```text
#define MX8MP_DSE_X1		0x0
#define MX8MP_DSE_X2		0x4
#define MX8MP_DSE_X4		0x2
#define MX8MP_DSE_X6		0x6
#define MX8MP_FSEL_FAST		0x10
#define MX8MP_ODE_ENABLE	0x20
#define MX8MP_PULL_UP		0x40
#define MX8MP_HYS_SCHMITT	0x80
#define MX8MP_PULL_ENABLE	0x100
#define MX8MP_SION		0x40000000
```

| 비트 | `0x400001c2`에서 | 뜻 |
|---|---|---|
| `0x40000000` | 켜짐 | SION. 커널 `pinctrl-imx.c`가 이 비트를 떼어 **먹스 레지스터의 bit 4**(`IOMUXC_CONFIG_SION`)로 옮긴다 |
| `0x100` | 켜짐 | 풀 enable |
| `0x80` | 켜짐 | 슈미트 트리거 입력 |
| `0x40` | 켜짐 | 풀 방향 = 업 |
| `0x20` | 꺼짐 | 오픈 드레인 아님 (I2C지만 컨트롤러가 알아서 오픈 드레인처럼 구동) |
| `0x10` | 꺼짐 | 느린 슬루 |
| `[2:1]` = `01` | `0x2` | 구동 세기 X4 |

SION(Software Input On)은 "출력으로 먹스돼 있어도 입력 경로를 켜 둬라"는 뜻이다. I2C 컨트롤러는 자기가 구동하는 SCL을 다시 읽어서 clock stretching(슬레이브가 SCL을 붙잡는 것)을 감지해야 하므로 I2C 핀에 흔히 켠다. 같은 헤더에 `MX8MP_I2C_DEFAULT`가 `DSE_X6 | PULL_UP | HYS_SCHMITT | PULL_ENABLE | SION`으로 정의돼 있는 것도 같은 이유다.

UART2는 `0x140` = 풀 enable + 풀업, 나머지 기본이다. 펌웨어에서 보던 레지스터 필드가 **이름만 DT로 옮겨 왔을 뿐 그대로**라는 것이 핵심이다.

### 2.2 "gpio" state — 같은 패드를 상황에 따라 다른 기능으로

같은 트리의 다른 i.MX8MP 보드(`imx8mp-aristainetos3a-som-v1.dtsi`)는 I2C1에 state를 두 개 둔다.

```dts
&i2c1 {
	clock-frequency = <100000>;
	pinctrl-names = "default", "gpio";
	pinctrl-0 = <&pinctrl_i2c1>;
	pinctrl-1 = <&pinctrl_i2c1_gpio>;
	scl-gpios = <&gpio5 14 GPIO_ACTIVE_HIGH>;
	sda-gpios = <&gpio5 15 GPIO_ACTIVE_HIGH>;
	status = "okay";
	/* ... pmic@25 ... */
};

	pinctrl_i2c1_gpio: aristainetos3-i2c1-gpio-grp {
		fsl,pins = <
			MX8MP_IOMUXC_I2C1_SCL__GPIO5_IO14	0x1c3
			MX8MP_IOMUXC_I2C1_SDA__GPIO5_IO15	0x1c3
		>;
	};
```

평소에는 `default`(I2C 기능)로 두다가, 슬레이브가 SDA를 Low로 붙잡아 버스가 멈추면 I2C 드라이버가 `gpio` state로 바꾸고 `scl-gpios`로 SCL을 최대 9번 토글해 슬레이브를 풀어 준다 (I2C 버스 복구, H5에서 자세히). 펌웨어에서 "I2C 블록을 끄고 핀을 GPIO로 바꿔 비트뱅" 하던 그 동작을, DT의 state 두 개 + 커널 공통 코드로 표현한 것이다.

### 2.3 흔한 함정

- **먹스 누락**: 드라이버도 맞고 DT의 `reg`도 맞는데 신호가 안 나온다 → `pinctrl-0`가 없거나 그룹에 그 핀이 빠져서 패드가 리셋 기본값(보통 GPIO 입력)이다. 스코프로 보면 핀이 아예 안 움직인다.
- **같은 패드를 두 그룹에 넣음**: 두 장치가 같은 패드를 각자 먹스하려 하면 나중에 probe하는 쪽이 실패한다 (§9에서 재현).
- **SELECT_INPUT(daisy) 누락**: 출력은 나가는데 입력만 안 들어온다. i.MX에서 입력 신호가 여러 패드로 갈 수 있는 경우 매크로의 3·5번 값이 0이 아니다.

---

## 3. pinctrl 프레임워크 — 핀을 "상태(state)"로 바꾼다

### 3.1 네 가지 단어: pin, group, function, state

| 단어 | 뜻 | i.MX에서 | 우리 실습에서 |
|---|---|---|---|
| pin | 패드 하나 (번호 + 이름) | `I2C3_SCL` | `PIN0`~`PIN7` |
| group | 함께 설정되는 핀 묶음 | `i2c3grp` | `h4-default-pins` |
| function | 그룹이 할 기능 | I2C3 | `h4-default-pins` (pinctrl-single은 그룹 이름을 함수 이름으로 씀) |
| state | 한 장치가 가질 수 있는 "핀 설정 세트" 이름 | `default`, `gpio` | `default`, `sleep` |
| map | (장치, state) → (그룹, 함수, 설정)의 표 | DT의 `pinctrl-N`에서 생성 | 같음 |

pin controller 드라이버(provider)는 `struct pinctrl_desc`로 핀 목록과 세 개의 ops를 등록한다: `pinctrl_ops`(그룹 목록, DT → map 변환), `pinmux_ops`(먹스 적용), `pinconf_ops`(패드 설정 적용). consumer 쪽은 DT에 `pinctrl-names`와 `pinctrl-0..N`만 쓰면 된다.

### 3.2 consumer는 보통 아무 코드도 안 쓴다 — `pinctrl_bind_pins()`

드라이버 코어는 **probe를 부르기 직전에** `pinctrl_bind_pins()`를 부른다 (`drivers/base/dd.c`의 `really_probe()`). 랩 소스 6.18.55에서 확인한 실제 코드다 (주석 일부 생략).

```c
/* drivers/base/pinctrl.c (발췌) */
int pinctrl_bind_pins(struct device *dev)
{
	/* ... */
	dev->pins->p = devm_pinctrl_get(dev);
	/* ... */
	dev->pins->default_state = pinctrl_lookup_state(dev->pins->p,
					PINCTRL_STATE_DEFAULT);
	/* ... */
	dev->pins->init_state = pinctrl_lookup_state(dev->pins->p,
					PINCTRL_STATE_INIT);
	if (IS_ERR(dev->pins->init_state)) {
		/* Not supplying this state is perfectly legal */
		ret = pinctrl_select_state(dev->pins->p,
					   dev->pins->default_state);
	} else {
		ret = pinctrl_select_state(dev->pins->p, dev->pins->init_state);
	}
	/* ... */
#ifdef CONFIG_PM
	dev->pins->sleep_state = pinctrl_lookup_state(dev->pins->p,
					PINCTRL_STATE_SLEEP);
	dev->pins->idle_state = pinctrl_lookup_state(dev->pins->p,
					PINCTRL_STATE_IDLE);
#endif
	return 0;
	/* ... */
}
```

그리고 probe가 성공하면 `really_probe()`가 `pinctrl_init_done()`을 부른다. 이 함수는 "아직 `init` state에 머물러 있으면 `default`로 바꾼다". 정리하면:

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="120" width="130" height="54" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="142" text-anchor="middle" font-size="13">probe 직전</text>
  <text x="85" y="162" text-anchor="middle" font-size="12">bind_pins()</text>
  <rect x="210" y="30" width="120" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-dasharray="5,3"/>
  <text x="270" y="57" text-anchor="middle" font-size="13">init (선택)</text>
  <rect x="380" y="120" width="120" height="54" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="440" y="152" text-anchor="middle" font-size="14">default</text>
  <rect x="560" y="30" width="100" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="610" y="57" text-anchor="middle" font-size="13">sleep</text>
  <rect x="560" y="216" width="100" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="610" y="243" text-anchor="middle" font-size="13">idle</text>
  <rect x="300" y="230" width="150" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-dasharray="5,3"/>
  <text x="375" y="257" text-anchor="middle" font-size="13">사용자 정의 (gpio)</text>
  <line x1="150" y1="130" x2="208" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="96" y="92" font-size="12">init 있으면</text>
  <line x1="150" y1="150" x2="378" y2="150" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="200" y="168" font-size="12">init 없으면</text>
  <line x1="330" y1="60" x2="400" y2="118" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="352" y="76" font-size="12">probe 성공 후</text>
  <line x1="500" y1="128" x2="560" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="568" y1="74" x2="508" y2="132" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="520" y="108" font-size="12">suspend</text>
  <text x="586" y="100" font-size="12">resume</text>
  <line x1="500" y1="166" x2="560" y2="222" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="568" y1="214" x2="508" y2="160" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="514" y="210" font-size="12">runtime PM</text>
  <line x1="430" y1="174" x2="400" y2="228" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="420" y1="230" x2="450" y2="176" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="20" y="226" font-size="12">드라이버가 직접:</text>
  <text x="20" y="246" font-size="12">pinctrl_select_state()</text>
</svg>
```
그림 3 — pinctrl state 전이. `init`이 있으면 probe 동안만 `init`이고, probe 성공 후 코어가 `default`로 바꾼다. `sleep`/`idle`은 드라이버의 PM 콜백이 `pinctrl_pm_select_sleep_state(dev)`/`pinctrl_pm_select_idle_state(dev)`/`pinctrl_pm_select_default_state(dev)`로 바꾼다 — 코어가 자동으로 해 주지 않는다. `gpio` 같은 이름은 드라이버가 `pinctrl_lookup_state()` + `pinctrl_select_state()`로 직접 고른다.

| state 이름 | 매크로 | 누가 고르나 | 쓰임 |
|---|---|---|---|
| `default` | `PINCTRL_STATE_DEFAULT` | 드라이버 코어 (probe 전 또는 init 후) | 평상시 |
| `init` | `PINCTRL_STATE_INIT` | 드라이버 코어 (probe 전) | probe 중 글리치를 피해야 할 때 (예: default로 바로 가면 핀이 튐) |
| `sleep` | `PINCTRL_STATE_SLEEP` | 드라이버의 suspend 콜백 | 시스템 suspend 시 누설 전류 줄이기 |
| `idle` | `PINCTRL_STATE_IDLE` | 드라이버의 runtime suspend 콜백 | runtime PM 유휴 |
| 그 밖 (`gpio` 등) | 없음 | 드라이버가 직접 | I2C 버스 복구, SD 카드 UHS 전압 전환 등 |

### 3.3 예제 2 — debugfs로 state가 적용되는 순간 보기

무엇을 확인하나: 우리 드라이버는 pinctrl 관련 코드를 probe에 **한 줄도 쓰지 않았다**(핸들만 얻어 둔다). 그런데 insmod 하면 패드 레지스터가 바뀌는지, 그리고 sysfs로 `sleep`/`default`를 오가면 레지스터가 어떻게 바뀌는지 본다. 게스트 시나리오 `pinctrl`의 스크립트다.

```sh
PD=/sys/kernel/debug/pinctrl; ls $PD | tr "\n" " "; echo
echo "=== pinctrl-devices ==="; cat $PD/pinctrl-devices
echo "=== before insmod: pinctrl-handles ==="; cat $PD/pinctrl-handles
insmod /h4_sensor.ko; sleep 1
echo "=== after insmod: pinctrl-handles ==="; cat $PD/pinctrl-handles
P=$PD/5ff00000.pinmux-pinctrl-single; ls $P | tr "\n" " "; echo
for f in pingroups pinmux-functions pinmux-pins pins pinconf-groups; do echo "=== $f ==="; cat $P/$f; done
echo sleep > /sys/devices/platform/h4-sensor/pins
echo "=== after sleep: pins ==="; cat $P/pins | head -4; grep -A3 "device: h4-sensor" $PD/pinctrl-handles
echo default > /sys/devices/platform/h4-sensor/pins
echo "=== after default: pins ==="; cat $P/pins | head -4
```

실행 결과 (insmod가 찍은 도둑 노드 관련 커널 로그 6줄은 §9에서 따로 보고 여기서는 생략, `pingroups`·`pinmux-functions`도 일부 생략):

```text
5ff00000.pinmux-pinctrl-single pinctrl-devices pinctrl-handles pinctrl-maps 
=== pinctrl-devices ===
name [pinmux] [pinconf]
pinctrl-single yes yes
=== before insmod: pinctrl-handles ===
Requested pin control handlers their pinmux maps:
=== after insmod: pinctrl-handles ===
Requested pin control handlers their pinmux maps:
device: h4-sensor current state: default
  state: default
    type: MUX_GROUP controller pinctrl-single group: h4-default-pins (0) function: h4-default-pins (0)
    type: CONFIGS_GROUP controller pinctrl-single group h4-default-pins (0)config output drive strength: 0x3
config input bias pull up: 0x1
config input bias pull down: 0x0
config input schmitt enabled: 0x1
  state: sleep
    type: MUX_GROUP controller pinctrl-single group: h4-sleep-pins (1) function: h4-sleep-pins (1)
    type: CONFIGS_GROUP controller pinctrl-single group h4-sleep-pins (1)config output drive strength: 0x0
config input bias pull up: 0x0
config input bias pull down: 0x1
config input schmitt enabled: 0x0
gpio-ranges pinconf-groups pinconf-pins pingroups pinmux-functions pinmux-pins pinmux-select pins 
=== pingroups ===
registered pin groups:
group: h4-default-pins
pin 0 (PIN0)
pin 1 (PIN1)
...
=== pinmux-pins ===
Pinmux settings per pin
Format: pin (name): mux_owner gpio_owner hog?
pin 0 (PIN0): h4-sensor (GPIO UNCLAIMED) function h4-default-pins group h4-default-pins
pin 1 (PIN1): h4-sensor (GPIO UNCLAIMED) function h4-default-pins group h4-default-pins
pin 2 (PIN2): (MUX UNCLAIMED) (GPIO UNCLAIMED)
...
=== pins ===
registered pins: 8
pin 0 (PIN0) 0:? 5ff00000 00000172 pinctrl-single 
pin 1 (PIN1) 0:? 5ff00004 00000172 pinctrl-single 
pin 2 (PIN2) 0:? 5ff00008 00000000 pinctrl-single 
...
=== after sleep: pins ===
registered pins: 8
pin 0 (PIN0) 0:? 5ff00000 00000080 pinctrl-single 
pin 1 (PIN1) 0:? 5ff00004 00000080 pinctrl-single 
pin 2 (PIN2) 0:? 5ff00008 00000000 pinctrl-single 
device: h4-sensor current state: sleep
...
=== after default: pins ===
registered pins: 8
pin 0 (PIN0) 0:? 5ff00000 00000172 pinctrl-single 
pin 1 (PIN1) 0:? 5ff00004 00000172 pinctrl-single 
pin 2 (PIN2) 0:? 5ff00008 00000000 pinctrl-single 
```

출력에서 볼 것:

- insmod 전에는 `pinctrl-handles`가 비어 있다. DT의 `pinctrl-0`는 **장치가 probe될 때** map으로 바뀐다(`pinctrl_dt_to_map`). 드라이버가 없으면 핀은 리셋 기본값 그대로다.
- insmod 후 `current state: default`, 패드 레지스터 `0x172`. 풀면 `0x100`(슈미트) + `0x40`(풀업) + `0x30`(구동 세기 3) + `0x2`(ALT2). **드라이버 코드가 아니라 드라이버 코어가** probe 직전에 적용한 것이다.
- `sleep`으로 바꾸면 `0x080` = 풀다운만 켜지고 먹스는 ALT0(GPIO), 구동 세기·슈미트는 0. 다시 `default`로 오면 `0x172`로 정확히 돌아온다.
- `pinmux-pins`의 `mux_owner`가 `h4-sensor`다. 이 소유권이 §9의 충돌 실험에서 다른 장치를 막는다.

### 3.4 내가 실제로 밟은 함정 — pinctrl-single의 drive-strength 값

처음에는 `pinctrl-single,drive-strength = <3 0x30>;`이라고 썼다("세기 3, 마스크 0x30"). 결과는 이랬다 (그때의 실제 출력).

```text
    type: CONFIGS_GROUP controller pinctrl-single group h4-default-pins (0)config output drive strength: 0x0
...
pin 0 (PIN0) 0:? 5ff00000 00000142 pinctrl-single 
```

구동 세기 비트(`0x30`)가 0이다. `drivers/pinctrl/pinctrl-single.c`의 `pcs_add_conf2()`를 보면 값에 먼저 마스크를 AND한 뒤 시프트한다. 즉 **값은 레지스터 안의 비트 위치에 맞춘 값**(`0x30`)이어야 하고, `3`을 주면 `3 & 0x30 = 0`이 된다. `<0x30 0x30>`으로 고치자 위의 `0x172`가 나왔다. 바인딩 문서의 "drive strength current"라는 설명만 보고 쓰면 틀리기 쉽다. pinctrl 드라이버마다 DT 값의 의미가 다르므로 **바인딩 + 드라이버 소스 + debugfs `pins` 레지스터 값**을 함께 확인하는 습관이 필요하다.

### 3.5 sleep state를 드라이버에서 쓰는 표준 코드 (참고, 이 노트에서 실행하지 않음)

```c
static int imu_suspend(struct device *dev)
{
	/* 센서 끄기 ... */
	return pinctrl_pm_select_sleep_state(dev);
}

static int imu_resume(struct device *dev)
{
	int ret = pinctrl_pm_select_default_state(dev);
	/* 센서 다시 켜기 ... */
	return ret;
}
static DEFINE_SIMPLE_DEV_PM_OPS(imu_pm, imu_suspend, imu_resume);
```

이 형태는 커널 드라이버에 흔한 관례이고, `pinctrl_pm_select_*` 함수들은 `drivers/pinctrl/core.c`에 `EXPORT_SYMBOL_GPL`로 있다. 우리 실습 드라이버는 PM 콜백 대신 sysfs 속성 `pins`로 같은 동작(`pinctrl_lookup_state` + `pinctrl_select_state`)을 손으로 일으켰다.

---

## 4. gpiolib provider — `struct gpio_chip`

### 4.1 GPIO 컨트롤러 드라이버가 채우는 것

GPIO 컨트롤러 드라이버(provider)는 `struct gpio_chip`을 채워 `devm_gpiochip_add_data()`로 등록한다. 6.18의 `include/linux/gpio/driver.h`에서 핵심 필드만 뽑으면:

| 필드 | 하는 일 | 펌웨어 대응 |
|---|---|---|
| `request` / `free` | 라인을 누가 쓰기 시작/끝낼 때 (pinctrl에 GPIO 먹스 요청 등) | — |
| `get_direction` | 현재 방향 읽기 | `GDIR` 읽기 |
| `direction_input` / `direction_output` | 방향 설정 (출력은 초기값과 함께) | `GDIR` 쓰기 + `DR` 쓰기 |
| `get` / `set` | 값 읽기/쓰기 (6.18에서 `set`은 `int` 반환, 예전 커널은 `void`) | `PSR` 읽기 / `DR` 쓰기 |
| `get_multiple` / `set_multiple` | 여러 라인을 한 번에 | 레지스터 한 번 접근 |
| `set_config` | 풀업, 오픈 드레인, 디바운스 등 (`PIN_CONFIG_*`) | 패드 설정 |
| `to_irq` | 라인 offset → Linux IRQ 번호 | 벡터 번호 계산 |
| `base` | 전역 GPIO 번호 시작값. 새 드라이버는 `-1` (동적) | — |
| `ngpio` | 라인 수 | — |
| `names` | 라인 이름 (보통 DT `gpio-line-names`로 채워짐) | — |
| `can_sleep` | `get`/`set`이 잠들 수 있나 (I2C/SPI 확장기면 true) | — |
| `irq` | `struct gpio_irq_chip` — gpiolib이 대신 만들어 주는 irqchip | — |

gpio-sim도 똑같은 provider다. `drivers/gpio/gpio-sim.c`의 `gpio_sim_add_bank()`는 위 콜백을 전부 채우고, **`gc->can_sleep = true`**로 등록한다(값을 mutex로 보호하므로). 이 한 줄 덕분에 우리 실습의 sim 라인은 "I2C GPIO 확장기 뒤의 핀"처럼 행동한다 — §5.5의 함정 재현에 쓴다.

### 4.2 `/sys/kernel/debug/gpio` — 칩과 라인 소유자 한눈에 보기

`h4_sensor`가 로드된 뒤의 실제 출력이다 (시나리오 `irq`).

```text
gpiochip0: 8 GPIOs, parent: amba/9030000.pl061, 9030000.pl061:
 gpio-3   (                    |GPIO Key Poweroff   ) in  lo IRQ 
 gpio-4   (                    |enable              ) out hi 

gpiochip1: 8 GPIOs, parent: platform/h4-gpio-sim, h4_sim, can sleep:
 gpio-0   (reset) output-high,pull-down
 gpio-1   (<unused>) input,pull-down
 gpio-2   (irq) input,pull-down
 gpio-3   (<unused>) input,pull-down
 gpio-4   (<unused>) input,pull-down
 gpio-5   (<unused>) input,pull-down
 gpio-6   (<unused>) input,pull-down
 gpio-7   (h4-led-hog) output-high,pull-down
```

출력에서 볼 것:

- PL061 칩은 gpiolib 공통 포맷이다: `(라인 이름|consumer 이름) 방향 값 [IRQ]`. 4번 핀의 consumer가 `enable`이다 — `devm_gpiod_get(dev, "enable", ...)`의 con_id가 그대로 consumer 이름이 된다. 3번은 `gpio-keys`가 IRQ로 쓰고 있다.
- gpio-sim은 드라이버가 `dbg_show` 콜백을 따로 제공해서 포맷이 다르다(`gpio_sim_dbg_show`). `can sleep` 표시가 칩 머리에 붙어 있다.
- `gpio-0 (reset) output-high`: 우리가 리셋을 **해제**한 상태인데 핀은 High다. active-low라서 "해제 = 물리 High"다 (§5.3).
- hog(`gpio-7`)는 consumer 이름이 DT의 `line-name`이다.

### 4.3 PL061 레지스터로 확인 — 같은 핀을 bare-metal 시각으로

시나리오 `irq`에서 BusyBox `devmem`으로 PL061 레지스터를 직접 읽었다 (랩 커널은 `STRICT_DEVMEM=y`지만 `IO_STRICT_DEVMEM`이 꺼져 있어 드라이버가 잡은 MMIO도 읽힌다).

```sh
echo "=== PL061 regs: GPIODIR=$(devmem 0x09030400 32) GPIODATA=$(devmem 0x090303fc 32)"
```

```text
=== PL061 regs: GPIODIR=0x00000010 GPIODATA=0x00000010
```

출력에서 볼 것: `GPIODIR`(오프셋 0x400)의 bit 4 = 출력, `GPIODATA`를 주소 비트 [9:2] 마스크 전체(0x3fc)로 읽으니 bit 4 = High. `gpiod_set_value_cansleep(en, 1)` 한 줄이 결국 PL061 드라이버의 `set` 콜백을 거쳐 이 레지스터 비트가 된 것이다. PL061은 "주소 비트가 마스크"인 독특한 데이터 레지스터라, `devmem 0x09030040`(bit 4만 마스크)으로 읽어도 같은 비트를 볼 수 있다.

---

## 5. gpiod consumer API — 논리값과 물리값

### 5.1 디스크립터 API의 모양

consumer 드라이버는 **정수 번호가 아니라 `struct gpio_desc *`**를 받는다. 받는 함수는 이름(con_id)으로 DT의 `<con_id>-gpios` 속성을 찾는다.

| 함수 | 없을 때 | 쓰임 |
|---|---|---|
| `devm_gpiod_get(dev, "reset", flags)` | `-ENOENT` 에러 | 필수 핀 |
| `devm_gpiod_get_optional(dev, "enable", flags)` | `NULL` 반환 (에러 아님) | 보드에 따라 없을 수도 있는 핀 |
| `devm_gpiod_get_index(dev, "led", i, flags)` | `-ENOENT` | `led-gpios = <..>, <..>`처럼 여러 개 |
| `devm_gpiod_get_array(dev, "addr", flags)` | `-ENOENT` | 여러 핀을 비트맵으로 한 번에 |

`NULL` 디스크립터는 모든 `gpiod_*` 함수가 조용히 무시한다. 그래서 optional 핀은 `if (h->en)` 없이 그냥 `gpiod_set_value_cansleep(h->en, 1)`을 불러도 된다.

flags는 요청과 **동시에** 방향과 초기값을 정한다.

| flags | 방향 | 초기 논리값 | 언제 |
|---|---|---|---|
| `GPIOD_ASIS` | 안 바꿈 | 안 바꿈 | 부트로더가 켜 둔 상태를 유지해야 할 때 (화면 깜빡임 방지 등) |
| `GPIOD_IN` | 입력 | — | INT, 버튼 |
| `GPIOD_OUT_LOW` | 출력 | 0 (비활성) | enable 핀 — 일단 꺼 둔다 |
| `GPIOD_OUT_HIGH` | 출력 | 1 (활성) | reset 핀 — 일단 리셋을 건 채로 시작 |
| `GPIOD_OUT_LOW_OPEN_DRAIN` / `_HIGH_OPEN_DRAIN` | 오픈 드레인 출력 | 0 / 1 | 공유 신호선 |

**"초기값을 따로 set 하지 말고 flags로 준다"**가 중요하다. `GPIOD_IN`으로 받고 나서 `gpiod_direction_output(d, 0)`을 부르면, 그 사이 아주 짧게라도 핀이 이전 레벨로 나갈 수 있다. flags로 주면 provider의 `direction_output(offset, value)`가 값과 방향을 한 번에 쓴다.

### 5.2 예제 3 — 센서 드라이버 `h4_sensor` (세 조각을 합쳐 한 파일)

무엇을 확인하나: reset(active-low), enable(optional), irq 세 핀을 받아 "전원 켜기 → 리셋 해제" 시퀀스를 돌리고, 각 단계에서 논리값과 물리값을 찍는다. sysfs 속성 세 개(`edges`, `reset`, `pins`)로 나중 실험을 할 수 있게 한다.

```c
// SPDX-License-Identifier: GPL-2.0
/* h4_sensor: reset/enable/irq GPIO + pinctrl state를 다루는 가짜 센서 드라이버 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/of.h>

static bool bad_atomic;			/* 1이면 can_sleep 칩에 gpiod_set_value() */
module_param(bad_atomic, bool, 0444);

struct h4 {
	struct gpio_desc *rst, *en, *irqg;
	struct pinctrl *pctl;
	atomic_t edges;
	int irq;
};

static irqreturn_t h4_thread(int irq, void *p)	/* 스레드 핸들러: 잠들어도 된다 */
{
	struct h4 *h = p;

	atomic_inc(&h->edges);
	return IRQ_HANDLED;
}

static ssize_t edges_show(struct device *d, struct device_attribute *a, char *b)
{
	struct h4 *h = dev_get_drvdata(d);

	return sysfs_emit(b, "%d\n", atomic_read(&h->edges));
}
static DEVICE_ATTR_RO(edges);
```

```c

static ssize_t reset_show(struct device *d, struct device_attribute *a, char *b)
{
	struct h4 *h = dev_get_drvdata(d);

	return sysfs_emit(b, "logical=%d raw=%d\n",
			  gpiod_get_value_cansleep(h->rst),
			  gpiod_get_raw_value_cansleep(h->rst));
}
static ssize_t reset_store(struct device *d, struct device_attribute *a,
			   const char *b, size_t n)
{
	struct h4 *h = dev_get_drvdata(d);

	gpiod_set_value_cansleep(h->rst, b[0] == '1');	/* 논리값 1 = assert */
	return n;
}
static DEVICE_ATTR_RW(reset);

static ssize_t pins_store(struct device *d, struct device_attribute *a,
			  const char *b, size_t n)
{
	struct h4 *h = dev_get_drvdata(d);
	struct pinctrl_state *s = pinctrl_lookup_state(h->pctl,
				  b[0] == 's' ? PINCTRL_STATE_SLEEP : PINCTRL_STATE_DEFAULT);

	return IS_ERR(s) ? PTR_ERR(s) : pinctrl_select_state(h->pctl, s) ?: n;
}
static DEVICE_ATTR_WO(pins);
static struct attribute *h4_attrs[] = { &dev_attr_edges.attr,
	&dev_attr_reset.attr, &dev_attr_pins.attr, NULL };
ATTRIBUTE_GROUPS(h4);
```

```c
#define SHOW(tag, g) dev_info(dev, "%-14s logical=%d raw=%d\n", tag, \
		gpiod_get_value_cansleep(g), gpiod_get_raw_value_cansleep(g))

static int h4_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct h4 *h;
	int ret;

	if (of_device_is_compatible(dev->of_node, "dons,h4-thief")) {
		struct gpio_desc *g = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);

		if (IS_ERR(g))
			return dev_err_probe(dev, PTR_ERR(g), "thief: reset gpio\n");
		dev_info(dev, "thief: probed (got reset=%d)\n", g != NULL);
		return 0;
	}
	h = devm_kzalloc(dev, sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	h->rst = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);	/* 논리 1 = 리셋 assert */
	if (IS_ERR(h->rst))
		return dev_err_probe(dev, PTR_ERR(h->rst), "reset\n");
	h->en = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(h->en))
		return dev_err_probe(dev, PTR_ERR(h->en), "enable\n");
	h->irqg = devm_gpiod_get(dev, "irq", GPIOD_IN);
	if (IS_ERR(h->irqg))
		return dev_err_probe(dev, PTR_ERR(h->irqg), "irq gpio\n");
	h->pctl = devm_pinctrl_get(dev);	/* bind_pins가 만든 핸들을 refcount로 공유 */
	if (IS_ERR(h->pctl))
		return PTR_ERR(h->pctl);
	dev_info(dev, "cansleep: rst=%d en=%d  legacy#: rst=%d en=%d  debounce(1ms)=%d\n",
		 gpiod_cansleep(h->rst), gpiod_cansleep(h->en), desc_to_gpio(h->rst),
		 desc_to_gpio(h->en), gpiod_set_debounce(h->irqg, 1000));
```

```c
	SHOW("reset@probe", h->rst);
	gpiod_set_value_cansleep(h->en, 1);		/* 1) 전원 enable */
	usleep_range(1000, 2000);
	if (bad_atomic)
		gpiod_set_value(h->rst, 0);		/* 함정: sleep 가능한 칩 */
	else
		gpiod_set_value_cansleep(h->rst, 0);	/* 2) 리셋 해제 */
	SHOW("reset@release", h->rst);
	SHOW("enable", h->en);
	h->irq = gpiod_to_irq(h->irqg);
	if (h->irq < 0)
		return h->irq;
	ret = devm_request_threaded_irq(dev, h->irq, NULL, h4_thread,
			IRQF_TRIGGER_RISING | IRQF_ONESHOT, "h4_sensor", h);
	if (ret)
		return dev_err_probe(dev, ret, "irq %d\n", h->irq);
	platform_set_drvdata(pdev, h);
	dev_info(dev, "irq-gpio -> virq %d (hwirq %lu)\n", h->irq,
		 irqd_to_hwirq(irq_get_irq_data(h->irq)));
	return 0;
}

static const struct of_device_id h4_of[] = {
	{ .compatible = "dons,h4-sensor" }, { .compatible = "dons,h4-thief" }, { }
};
MODULE_DEVICE_TABLE(of, h4_of);
static struct platform_driver h4_drv = {
	.probe = h4_probe,
	.driver = { .name = "h4_sensor", .of_match_table = h4_of,
		    .dev_groups = h4_groups },
};
module_platform_driver(h4_drv);
MODULE_DESCRIPTION("H4 pinctrl/GPIO consumer demo");
MODULE_LICENSE("GPL");
```

빌드는 랩 트리에 대고 out-of-tree로 한다.

```sh
mkdir -p /var/tmp/H4/mod && cd /var/tmp/H4/mod
cat ~/.elinux_scratch/H4/h4_sensor_a.c ~/.elinux_scratch/H4/h4_sensor_b.c > h4_sensor.c
echo "obj-m := h4_sensor.o" > Kbuild
make -j2 -C /var/lab/out/linux-build M=$PWD modules
```

```text
make: Entering directory '/var/lab/build/linux'
make[1]: Entering directory '/var/tmp/H4/mod'
  CC [M]  h4_sensor.o
  MODPOST Module.symvers
  CC [M]  h4_sensor.mod.o
  LD [M]  h4_sensor.ko
make[1]: Leaving directory '/var/tmp/H4/mod'
make: Leaving directory '/var/lab/build/linux'
```

(맥 쪽 경로 `/Users/donh/.elinux_scratch/H4`를 VM에서는 같은 절대 경로로 읽는다.) 경고 0개다. 처음 빌드 때는 `of_device_is_compatible`의 헤더(`<linux/of.h>`)를 빠뜨려 implicit declaration 에러가, 그다음엔 `MODULE_DESCRIPTION()` 누락 modpost 경고가 났다 — 둘 다 고친 최종본이 위 코드다.

### 5.3 예제 4 — probe 로그: 논리값 vs 물리값

시나리오 `probe`의 일부다 (게스트 커널 `uname -r` = `6.18.55`).

```sh
./h4_watch /dev/gpiochip1 4000 0 2 > /tmp/w.txt &
sleep 1; insmod /h4_sensor.ko; sleep 1
echo "=== dmesg ==="; dmesg | grep -E "h4|gpio|pinctrl|pinmux" | grep -v "Command line"
```

```text
=== dmesg ===
[    0.000000] OF: reserved mem: 0x000000005ff00000..0x000000005ff00fff (4 KiB) nomap non-reusable h4-padram@5ff00000
[    0.000000] Unknown kernel command line parameters "LAB_CMD=sh~/h4.sh~probe", will be passed to user space.
[    0.135799] pinctrl core: initialized pinctrl subsystem
[    0.394698] pinctrl-single 5ff00000.pinmux: 8 pins, size 32
[    0.405401] pl061_gpio 9030000.pl061: PL061 GPIO chip registered
[    0.706958] input: gpio-keys as /devices/platform/gpio-keys/input/input0
[    0.774334]     LAB_CMD=sh~/h4.sh~probe
[    1.956228] h4_sensor: loading out-of-tree module taints kernel.
[    1.963873] h4_sensor h4-sensor: cansleep: rst=1 en=0  legacy#: rst=520 en=516  debounce(1ms)=-524
[    1.963989] h4_sensor h4-sensor: reset@probe    logical=1 raw=0
[    1.966345] h4_sensor h4-sensor: reset@release  logical=0 raw=1
[    1.966395] h4_sensor h4-sensor: enable         logical=1 raw=1
[    1.968238] h4_sensor h4-sensor: irq-gpio -> virq 22 (hwirq 2)
```

출력에서 볼 것 (줄마다):

- `cansleep: rst=1 en=0` — reset은 gpio-sim(잠들 수 있는 칩), enable은 PL061(MMIO, 안 잠듦). **같은 드라이버 안에서도 핀마다 다르다.** 드라이버는 어느 칩인지 모르므로 프로세스 컨텍스트에서는 항상 `_cansleep`을 쓰는 게 안전하다.
- `legacy#: rst=520 en=516` — 레거시 전역 GPIO 번호. PL061 base가 512(4번 핀 → 516), sim base가 520이다. 이 숫자는 **칩 등록 순서에 따라 동적으로 정해진다**(6.x에서 동적 base는 512부터). 보드가 같아도 드라이버 로드 순서가 바뀌면 달라진다 — 정수 GPIO API가 폐기된 이유다(§5.6).
- `debounce(1ms)=-524` — `-ENOTSUPP`. gpio-sim의 `set_config`는 디바운스를 지원하지 않는다. 하드웨어 디바운스가 없는 칩에서 디바운스가 필요하면 소프트웨어로 해야 한다(§6.3, §7.4).
- `reset@probe logical=1 raw=0` — `GPIOD_OUT_HIGH`로 받았으니 논리 1(리셋 assert), DT가 `GPIO_ACTIVE_LOW`라 **물리 0**. 
- `reset@release logical=0 raw=1` — 논리 0(해제) = 물리 1.
- `enable logical=1 raw=1` — active-high라 같다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="16" width="210" height="58" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="125" y="40" text-anchor="middle" font-size="12">gpiod_set_value(rst, 1)</text>
  <text x="125" y="60" text-anchor="middle" font-size="12">= "리셋 걸어라" (논리 1)</text>
  <rect x="270" y="16" width="160" height="58" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="350" y="40" text-anchor="middle" font-size="12">gpiolib: ACTIVE_LOW?</text>
  <text x="350" y="60" text-anchor="middle" font-size="12">예 → 반전해서 씀</text>
  <rect x="470" y="16" width="190" height="58" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="40" text-anchor="middle" font-size="12">gpio_chip-&gt;set(.., 0)</text>
  <text x="565" y="60" text-anchor="middle" font-size="12">핀 0 V → 칩 리셋</text>
  <line x1="230" y1="45" x2="268" y2="45" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="430" y1="45" x2="468" y2="45" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="20" y="96" width="640" height="30" fill="#888" fill-opacity="0.12" stroke="currentColor"/>
  <line x1="180" y1="96" x2="180" y2="246" stroke="currentColor"/>
  <line x1="400" y1="96" x2="400" y2="246" stroke="currentColor"/>
  <line x1="530" y1="96" x2="530" y2="246" stroke="currentColor"/>
  <text x="100" y="116" text-anchor="middle" font-size="13">DT 플래그</text>
  <text x="290" y="116" text-anchor="middle" font-size="13">논리값 (gpiod_set_value)</text>
  <text x="465" y="116" text-anchor="middle" font-size="13">raw (_raw API)</text>
  <text x="595" y="116" text-anchor="middle" font-size="13">핀 전압</text>
  <rect x="20" y="126" width="640" height="120" fill="none" stroke="currentColor"/>
  <line x1="20" y1="156" x2="660" y2="156" stroke="currentColor" stroke-opacity="0.4"/>
  <line x1="20" y1="186" x2="660" y2="186" stroke="currentColor"/>
  <line x1="20" y1="216" x2="660" y2="216" stroke="currentColor" stroke-opacity="0.4"/>
  <text x="100" y="146" text-anchor="middle" font-size="13">ACTIVE_HIGH</text>
  <text x="290" y="146" text-anchor="middle" font-size="13">1</text>
  <text x="465" y="146" text-anchor="middle" font-size="13">1</text>
  <text x="595" y="146" text-anchor="middle" font-size="13">High</text>
  <text x="100" y="176" text-anchor="middle" font-size="13">ACTIVE_HIGH</text>
  <text x="290" y="176" text-anchor="middle" font-size="13">0</text>
  <text x="465" y="176" text-anchor="middle" font-size="13">0</text>
  <text x="595" y="176" text-anchor="middle" font-size="13">Low</text>
  <rect x="21" y="187" width="638" height="58" fill="#d0564a" fill-opacity="0.12" stroke="none"/>
  <text x="100" y="206" text-anchor="middle" font-size="13">ACTIVE_LOW</text>
  <text x="290" y="206" text-anchor="middle" font-size="13">1</text>
  <text x="465" y="206" text-anchor="middle" font-size="13">0</text>
  <text x="595" y="206" text-anchor="middle" font-size="13">Low</text>
  <text x="100" y="236" text-anchor="middle" font-size="13">ACTIVE_LOW</text>
  <text x="290" y="236" text-anchor="middle" font-size="13">0</text>
  <text x="465" y="236" text-anchor="middle" font-size="13">1</text>
  <text x="595" y="236" text-anchor="middle" font-size="13">High</text>
  <text x="20" y="276" font-size="12">실행 결과: reset@probe logical=1 raw=0 → reset@release logical=0 raw=1 (ACTIVE_LOW 행 그대로)</text>
</svg>
```
그림 4 — 논리값과 물리값. 드라이버는 "활성/비활성"만 말하고, 극성은 DT 플래그 하나가 결정한다. `gpiod_get_raw_value()`/`gpiod_set_raw_value()`만 극성을 무시하고 핀 레벨을 그대로 다룬다.

### 5.4 예제 5 — sysfs로 리셋을 걸었다 풀기

무엇을 확인하나: 드라이버의 `reset` 속성(논리값을 받아 `gpiod_set_value_cansleep`)으로 assert/deassert하면서 논리/물리값이 표대로 움직이는지 본다.

```sh
D=/sys/devices/platform/h4-sensor
cat $D/reset; echo 1 > $D/reset; cat $D/reset; echo 0 > $D/reset; cat $D/reset
```

```text
logical=0 raw=1
logical=1 raw=0
logical=0 raw=1
```

출력에서 볼 것: 유저가 쓴 "1"은 **"리셋을 걸어라"**라는 뜻이고 핀은 0 V로 간다. 이 의미 체계 덕분에 보드 B에서 리셋 회로가 active-high로 바뀌어도 DT 플래그만 고치면 드라이버·유저 스크립트 모두 그대로다.

**고전적 혼동 두 가지**:

1. DT에 `GPIO_ACTIVE_LOW`를 쓰고, 드라이버에서도 "active-low니까" `gpiod_set_value(rst, 0)`으로 리셋을 건다 → **이중 반전**으로 리셋이 안 걸린다. 드라이버는 극성을 몰라야 한다. 논리값만 쓴다.
2. 레거시 코드를 옮기며 `gpiod_set_raw_value`를 남발한다 → DT 플래그가 무시되어 보드마다 코드가 갈라진다. raw API는 "핀 레벨 자체가 의미인" 드문 경우(비트뱅 프로토콜 등)에만 쓴다.

### 5.5 예제 6 — `gpiod_set_value()` vs `_cansleep()`: 잠드는 칩에 atomic API를 쓰면

무엇을 확인하나: `bad_atomic=1`이면 드라이버가 리셋 해제를 `gpiod_set_value()`(잠들면 안 되는 컨텍스트용)로 한다. 대상 칩은 `can_sleep = true`인 gpio-sim이다.

```sh
insmod /h4_sensor.ko bad_atomic=1; sleep 1; dmesg | sed -n '/WARNING/,/end trace/p'
```

```text
[    1.092378] WARNING: CPU: 1 PID: 71 at drivers/gpio/gpiolib.c:3881 gpiod_set_value+0x44/0x70
[    1.093162] Modules linked in: h4_sensor(O+)
[    1.093930] CPU: 1 UID: 0 PID: 71 Comm: insmod Tainted: G           O        6.18.55 #1 PREEMPT 
[    1.094016] Tainted: [O]=OOT_MODULE
[    1.094034] Hardware name: linux,dummy-virt (DT)
[    1.094152] pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
[    1.094191] pc : gpiod_set_value+0x44/0x70
[    1.094214] lr : h4_probe+0x194/0x370 [h4_sensor]
...
[    1.095006]  gpiod_set_value+0x44/0x70 (P)
[    1.095102]  h4_probe+0x194/0x370 [h4_sensor]
[    1.095136]  platform_probe+0x5c/0xa4
...
```

(레지스터 덤프와 콜 트레이스 아랫부분은 생략.) 출력에서 볼 것: `gpiolib.c:3881`은 `gpiod_set_value()` 안의 `WARN_ON(desc->gdev->can_sleep);`이다(랩 소스에서 확인). 동작 자체는 일어나지만(값은 써진다), gpiolib이 "이 칩은 I2C처럼 잠들 수 있는데 너는 atomic 버전을 불렀다"고 경고한다.

| 함수 | 부르는 컨텍스트 | 대상 칩 |
|---|---|---|
| `gpiod_set_value()` / `gpiod_get_value()` | hardirq, spinlock 안 등 어디서든 | `can_sleep = false`(SoC 내장 MMIO GPIO)만 |
| `gpiod_set_value_cansleep()` / `_get_value_cansleep()` | 프로세스 컨텍스트, threaded IRQ | 모든 칩 |
| `gpiod_cansleep(desc)` | — | 이 디스크립터의 칩이 잠드는지 확인 |

실무 감각: SoC GPIO로 되던 enable 핀을 다음 보드에서 핀이 모자라 **I2C GPIO 확장기(PCA953x, TCA6416 등)**로 옮기는 일이 흔하다. 드라이버가 처음부터 `_cansleep`을 썼으면 DT만 바꾸면 되고, atomic 버전을 썼으면 이 WARN이 뜨고 — hardirq 안에서 불렀다면 "scheduling while atomic"으로 커널이 더 크게 깨진다.

### 5.6 레거시 정수 API, hog, line names

- **레거시 API** (`<linux/gpio.h>`: `gpio_request(520, ...)`, `gpio_direction_output()`, `gpio_set_value()`): 전역 번호를 쓰는데 그 번호가 동적이고(위 실행에서 520), 극성 정보가 없고(드라이버가 직접 반전), DT와의 연결이 약하다. 커널 문서가 "obsolete"로 분류하고 새 코드는 `gpiod_*`만 쓴다. 기존 드라이버들도 계속 디스크립터 API로 옮겨지는 중이다.
- **hog**: DT의 `gpio-hog` 자식 노드. 칩 등록 때 gpiolib이 요청해 고정한다. 우리 `LED_HOG`(7번)는 아무 드라이버 없이 `output-high`로 잡혀 있었다 (§4.2의 `gpio-7 (h4-led-hog) output-high`). 그 핀은 이후 누구도 요청할 수 없다.
- **gpio-line-names**: 회로도 net 이름을 라인에 붙인다. libgpiod는 이름으로 라인을 찾을 수 있어서(`gpioget IMU_INT`) 스크립트가 칩 번호·offset에 의존하지 않게 된다(§7.5).

### 5.7 오픈 드레인 에뮬레이션 (실행하지 않음, 커널 동작 설명)

DT에 `GPIO_OPEN_DRAIN` 플래그를 쓰거나 `GPIOD_OUT_LOW_OPEN_DRAIN`으로 받으면, gpiolib은 먼저 칩의 `set_config(PIN_CONFIG_DRIVE_OPEN_DRAIN)`을 시도한다. 칩이 지원하지 않으면 **에뮬레이션**한다: 논리적으로 "High를 내라"면 라인을 **입력으로 바꿔서(Hi-Z)** 외부 풀업이 끌어올리게 하고, "Low"면 출력 Low로 구동한다. 펌웨어에서 I2C 비트뱅 때 "High는 방향을 입력으로" 하던 바로 그 트릭이다. 이 동작은 `drivers/gpio/gpiolib.c`의 open-drain 값 설정 경로에 있다.

---

## 6. GPIO → IRQ

### 6.1 `gpiod_to_irq()` 한 줄 뒤에서 일어나는 일

센서의 INT 핀은 GPIO 컨트롤러로 들어온다. GPIO 컨트롤러는 핀마다 엣지/레벨 검출기와 상태 래치(i.MX의 `ISR`, PL061의 `GPIORIS`)를 갖고, 한 줄로 부모 인터럽트 컨트롤러(GIC)에 이어진다. Linux에서는 GPIO 컨트롤러가 **자기 irqchip과 irq_domain**을 갖고, 라인 offset(hwirq)을 Linux IRQ 번호(virq)로 매핑한다. `gpiod_to_irq()`는 provider의 `to_irq` 콜백을 불러 그 매핑을 만든다(없으면 생성). 그 다음은 보통 인터럽트와 똑같이 `request_threaded_irq()`다.

| 컨트롤러 유형 | 부모와의 관계 | 핸들러 제약 |
|---|---|---|
| SoC 내장 GPIO (gpio-mxc, pl061) | chained 또는 hierarchical(GIC로) | hardirq 핸들러 가능 |
| I2C/SPI GPIO 확장기 (PCA953x) | 확장기 자체의 INT 핀이 SoC GPIO로 → 확장기 드라이버가 I2C로 상태를 읽음 → **nested threaded IRQ** | consumer는 **반드시 threaded 핸들러** (hardirq 핸들러만 주면 `request_irq`가 `-EINVAL`) |
| gpio-sim | `irq_sim` (irq_work로 소프트웨어 발생) | 실험용 |

nested IRQ 구조는 H6에서 자세히 다룬다. 여기서는 "확장기 뒤의 핀이면 `request_threaded_irq(irq, NULL, thread_fn, IRQF_ONESHOT, ...)`"만 기억한다. 우리 드라이버가 처음부터 이 형태라 어느 칩이든 동작한다.

### 6.2 예제 7 — 엣지를 만들어 핸들러가 세는지 보기

무엇을 확인하나: sim 쪽에서 `irq-gpios` 라인(2번)의 pull을 5번 올렸다 내리면, rising edge만 등록했으니 threaded 핸들러가 5번 불려야 한다.

```sh
insmod /h4_sensor.ko; sleep 1
D=/sys/devices/platform/h4-sensor
echo "edges=$(cat $D/edges)"
for i in 1 2 3 4 5; do pull 2 pull-up; pull 2 pull-down; done
sleep 1; echo "after 5 pulses: edges=$(cat $D/edges)"
grep -E "CPU0|h4_sensor|pl061" /proc/interrupts
N=$(grep h4_sensor /proc/interrupts | cut -d: -f1 | tr -d ' ')
for f in hwirq type chip_name name actions; do printf "%-10s %s\n" $f "$(cat /sys/kernel/irq/$N/$f)"; done
ps | grep -E "irq/$N" | grep -v grep
```

(`pull`은 `h4.sh` 머리에 정의한 셸 함수: `pull() { echo $2 > $SIM/sim_gpio$1/pull; }`, `SIM`은 `/sys/devices/platform/h4-gpio-sim/gpiochip*` 디렉터리.)

```text
edges=0
after 5 pulses: edges=5
           CPU0       CPU1       
 21:          0          0 9030000.pl061   3 Edge      GPIO Key Poweroff
 22:          5          0  irq_sim   2 Edge      h4_sensor
hwirq      2
type       edge
chip_name  irq_sim
name       
actions    h4_sensor
   71 0         0:00 [irq/22-h4_senso]
```

출력에서 볼 것:

- 5 펄스 → `edges=5`. falling edge는 등록하지 않았으니 세지 않는다.
- `/proc/interrupts`의 22번: 칩 이름 `irq_sim`, hwirq 2, `Edge`. 실제 보드라면 여기에 `gpio-mxc`나 `9030000.pl061` 같은 GPIO 컨트롤러 이름이 보인다(21번 줄의 poweroff 키처럼).
- `irq/22-h4_senso` 커널 스레드: `request_threaded_irq`가 만든 IRQ 스레드다(이름은 15자로 잘린다). 핸들러는 이 스레드에서 돌아서 잠들어도 된다 — 실제 센서라면 여기서 I2C로 상태 레지스터를 읽는다.

### 6.3 엣지냐 레벨이냐, 그리고 디바운스

| 트리거 | 좋은 경우 | 위험 |
|---|---|---|
| edge (rising/falling) | 짧은 펄스형 INT, 버튼 | 마스크된 동안 온 엣지·연속 엣지가 하나로 합쳐지거나 사라짐 (§8에서 측정) |
| level (high/low) | "상태 레지스터를 읽을 때까지 INT를 붙잡는" 센서 (대부분의 IMU, PMIC) | `IRQF_ONESHOT` 없이 threaded로 쓰면 무한 재진입 |
| both edges | 버튼 누름/뗌, 엔코더 | 노이즈에 취약 → 디바운스 필요 |

공유 가능한 open-drain INT 라인에 여러 센서가 물린 보드에서는 **level-low + `IRQF_ONESHOT` + threaded 핸들러에서 원인 레지스터 읽기**가 가장 견고하다. 엣지로 받으면 두 번째 센서가 INT를 붙잡고 있는 동안 첫 번째가 풀어도 라인에 새 엣지가 안 생겨 영원히 멈출 수 있다.

디바운스: `gpiod_set_debounce(desc, us)`는 provider의 `set_config(PIN_CONFIG_INPUT_DEBOUNCE)`로 **하드웨어 디바운스**를 요청한다. 우리 실행에서 `-524`(`-ENOTSUPP`)가 나왔듯 지원하지 않는 칩이 많다. 그때 커널 안 consumer는 직접 타이머로 거르고(`gpio-keys` 드라이버가 그렇게 한다), 유저 공간 uAPI는 gpiolib-cdev가 **소프트웨어 디바운스로 자동 대체**한다(§7.4에서 확인).

---

## 7. 유저 공간 — sysfs GPIO는 왜 죽었나, 그리고 uAPI v2

### 7.1 sysfs GPIO (`/sys/class/gpio`)가 폐기된 이유

A4에서 `echo 523 > /sys/class/gpio/export` 방식을 써 봤다. 이 인터페이스의 문제:

| 문제 | 구체적으로 |
|---|---|
| 전역 번호 의존 | `523`은 동적 base에 달렸다 (§5.3의 520/516처럼 로드 순서에 따라 바뀜) |
| 소유권이 프로세스에 묶이지 않음 | export한 프로세스가 죽어도 라인은 export된 채 남는다. 누가 쓰는지 알 수 없다 |
| 여러 라인을 원자적으로 못 다룸 | 파일 하나에 라인 하나. 8비트 버스를 동시에 바꿀 수 없다 |
| 이벤트가 빈약 | `POLLPRI`로 "바뀌었다"만 알 뿐 timestamp도, 순서 번호도, 큐도 없다. 빠른 엣지는 그냥 놓친다 |
| 설정이 부족 | 바이어스(풀업), 오픈 드레인, 디바운스를 요청할 방법이 없다 |
| 경쟁 조건 | `export` → `direction` → `value` 사이에 다른 프로세스가 끼어들 수 있다 |

그래서 Linux 4.8에 GPIO character device(`/dev/gpiochipN`)가 들어왔고, 5.10에 지금의 **uAPI v2**가 들어왔다. 6.18 랩 커널에서는 §1.1에서 봤듯 sysfs 인터페이스가 `EXPERT` 뒤로 숨어 있어 기본으로는 빌드조차 안 된다.

### 7.2 uAPI v2의 구조

```c
/* include/uapi/linux/gpio.h 핵심만 (실제 정의에서 발췌) */
struct gpio_v2_line_request {
	__u32 offsets[GPIO_V2_LINES_MAX];	/* 요청할 라인들 (최대 64) */
	char consumer[GPIO_MAX_NAME_SIZE];
	struct gpio_v2_line_config config;	/* 플래그 + 라인별 속성 */
	__u32 num_lines;
	__u32 event_buffer_size;		/* 이벤트 FIFO 크기 힌트 */
	__u32 padding[5];
	__s32 fd;				/* 커널이 돌려주는 "라인 요청 fd" */
};
```

흐름은 이렇다.

1. `open("/dev/gpiochipN")` → 칩 fd.
2. `ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req)` → 성공하면 `req.fd`에 **라인 요청 fd**. 이 fd가 소유권이다. **fd가 닫히면(프로세스가 죽어도) 라인이 자동으로 풀린다.**
3. 라인 fd에 `GPIO_V2_LINE_GET_VALUES_IOCTL` / `SET_VALUES_IOCTL` / `SET_CONFIG_IOCTL`.
4. 엣지 플래그를 켰으면 라인 fd를 `poll`/`epoll`하고 `read()`로 `struct gpio_v2_line_event`(timestamp_ns, id, offset, seqno, line_seqno)를 받는다.
5. 칩 fd에 `GPIO_V2_GET_LINEINFO_WATCH_IOCTL`을 걸면, 그 라인을 누가 요청/해제/재설정할 때마다 칩 fd로 `struct gpio_v2_line_info_changed`가 온다.

### 7.3 예제 8 — configfs로 gpio-sim 칩 만들고 C로 읽고 쓰기

무엇을 확인하나: DT 없이 런타임에 가짜 칩을 만드는 정석 방법(configfs)과, sim 쪽 `pull`이 consumer 쪽 입력값이 되는지, consumer 쪽 출력이 sim 쪽 `value`로 보이는지 확인한다. 먼저 쓸 프로그램 두 개다.

```c
/* h4_lsgpio: /dev/gpiochipN 마다 칩 정보와 라인 정보를 uAPI v2로 출력 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <glob.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>

static void flags_str(__u64 f, char *s)
{
	s[0] = 0;
	if (f & GPIO_V2_LINE_FLAG_USED)        strcat(s, "used ");
	strcat(s, f & GPIO_V2_LINE_FLAG_OUTPUT ? "out " : "in ");
	if (f & GPIO_V2_LINE_FLAG_ACTIVE_LOW)  strcat(s, "active-low ");
	if (f & GPIO_V2_LINE_FLAG_EDGE_RISING) strcat(s, "rising ");
	if (f & GPIO_V2_LINE_FLAG_EDGE_FALLING) strcat(s, "falling ");
	if (f & GPIO_V2_LINE_FLAG_OPEN_DRAIN)  strcat(s, "open-drain ");
	if (f & GPIO_V2_LINE_FLAG_BIAS_PULL_UP) strcat(s, "pull-up ");
	if (f & GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN) strcat(s, "pull-down ");
}

int main(int argc, char **argv)
{
	glob_t g;
	const char *pat = argc > 1 ? argv[1] : "/dev/gpiochip*";

	if (glob(pat, 0, NULL, &g)) { fprintf(stderr, "no chips\n"); exit(1); }
	for (size_t c = 0; c < g.gl_pathc; c++) {
		struct gpiochip_info ci;
		int fd = open(g.gl_pathv[c], O_RDONLY | O_CLOEXEC);

		if (fd < 0 || ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &ci) < 0) {
			perror(g.gl_pathv[c]); exit(1);
		}
		printf("%s  name=%s label=\"%s\" lines=%u\n",
		       g.gl_pathv[c], ci.name, ci.label, ci.lines);
		for (__u32 i = 0; i < ci.lines; i++) {
			struct gpio_v2_line_info li;
			char fs[128];

			memset(&li, 0, sizeof(li));	/* padding은 0이어야 한다 */
			li.offset = i;
			if (ioctl(fd, GPIO_V2_GET_LINEINFO_IOCTL, &li) < 0) {
				perror("LINEINFO"); exit(1);
			}
			flags_str(li.flags, fs);
			printf("  line %2u: %-10s %-16s %s\n", i,
			       li.name[0] ? li.name : "-",
			       li.consumer[0] ? li.consumer : "-", fs);
		}
		close(fd);
	}
	globfree(&g);
	return 0;
}
```

```c
/* h4_line: 라인 하나를 요청해서 읽거나(in) 쓰고(out=V) hold초 동안 쥐고 있는다 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s chip offset in|out=V [al] [pu] [hold]\n", argv[0]);
		exit(2);
	}
	struct gpio_v2_line_request req;
	struct gpio_v2_line_values v = { .mask = 1 };
	int fd = open(argv[1], O_RDONLY | O_CLOEXEC), out = !strncmp(argv[3], "out=", 4);
	int hold = 0;

	if (fd < 0) { perror(argv[1]); exit(1); }
	memset(&req, 0, sizeof(req));
	req.offsets[0] = atoi(argv[2]);
	req.num_lines = 1;
	strcpy(req.consumer, "h4_line");
	req.config.flags = out ? GPIO_V2_LINE_FLAG_OUTPUT : GPIO_V2_LINE_FLAG_INPUT;
	for (int i = 4; i < argc; i++) {
		if (!strcmp(argv[i], "al")) req.config.flags |= GPIO_V2_LINE_FLAG_ACTIVE_LOW;
		else if (!strcmp(argv[i], "pu")) req.config.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
		else hold = atoi(argv[i]);
	}
	if (out) {	/* 출력 초기값은 request와 함께 원자적으로 준다 */
		req.config.num_attrs = 1;
		req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
		req.config.attrs[0].attr.values = atoi(argv[3] + 4);
		req.config.attrs[0].mask = 1;
	}
	if (ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
		printf("GET_LINE offset %s: %s (errno %d)\n", argv[2], strerror(errno), errno);
		exit(1);
	}
	if (ioctl(req.fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) < 0) { perror("GET_VALUES"); exit(1); }
	printf("offset %s %s: value=%llu (logical)\n", argv[2], out ? "out" : "in",
	       (unsigned long long)(v.bits & 1));
	sleep(hold);
	close(req.fd);	/* fd를 닫으면 라인이 풀린다 */
	return 0;
}
```

게스트 스크립트 (시나리오 `cfgfs`):

```sh
mount -t configfs none /sys/kernel/config
C=/sys/kernel/config/gpio-sim/h4_cfg
mkdir $C $C/bank0 $C/bank0/line0 $C/bank0/line1
echo 4 > $C/bank0/num_lines; echo h4_cfg_bank > $C/bank0/label
echo DRDY > $C/bank0/line0/name; echo E_STOP > $C/bank0/line1/name
echo 1 > $C/live
echo "dev_name=$(cat $C/dev_name) chip_name=$(cat $C/bank0/chip_name)"
./h4_lsgpio /dev/$(cat $C/bank0/chip_name)
P=/sys/devices/platform/$(cat $C/dev_name)/$(cat $C/bank0/chip_name)
ls $P | tr "\n" " "; echo; ls $P/sim_gpio0 | tr "\n" " "; echo
echo "-- read line0 while sim pull=$(cat $P/sim_gpio0/pull)"; ./h4_line /dev/gpiochip2 0 in
echo pull-up > $P/sim_gpio0/pull
echo "-- read line0 while sim pull=$(cat $P/sim_gpio0/pull)"; ./h4_line /dev/gpiochip2 0 in
echo "-- same line, active-low request"; ./h4_line /dev/gpiochip2 0 in al
echo "-- drive line1 as output=1 and look from sim side"
./h4_line /dev/gpiochip2 1 out=1 2 & sleep 1; echo "sim value=$(cat $P/sim_gpio1/value)"; wait
echo "-- after release: sim value=$(cat $P/sim_gpio1/value) pull=$(cat $P/sim_gpio1/pull)"
echo "-- request line1 input with BIAS_PULL_UP"; ./h4_line /dev/gpiochip2 1 in pu
echo 0 > $C/live; rmdir $C/bank0/line0 $C/bank0/line1 $C/bank0 $C; echo "removed: $(ls /dev/gpiochip*)"
```

```text
dev_name=gpio-sim.0 chip_name=gpiochip2
/dev/gpiochip2  name=gpiochip2 label="h4_cfg_bank" lines=4
  line  0: DRDY       -                in 
  line  1: E_STOP     -                in 
  line  2: -          -                in 
  line  3: -          -                in 
dev driver power sim_gpio0 sim_gpio1 sim_gpio2 sim_gpio3 software_node subsystem uevent 
pull value 
-- read line0 while sim pull=pull-down
offset 0 in: value=0 (logical)
-- read line0 while sim pull=pull-up
offset 0 in: value=1 (logical)
-- same line, active-low request
offset 0 in: value=0 (logical)
-- drive line1 as output=1 and look from sim side
offset 1 out: value=1 (logical)
sim value=1
-- after release: sim value=0 pull=pull-down
-- request line1 input with BIAS_PULL_UP
offset 1 in: value=1 (logical)
removed: /dev/gpiochip0
/dev/gpiochip1
```

출력에서 볼 것:

- configfs 디렉터리 구조(칩 → bank → line)가 곧 설정이다. `live`에 1을 쓰는 순간 platform 장치 `gpio-sim.0`과 `/dev/gpiochip2`가 생긴다. 이 칩은 software node(fwnode)로 설명되므로 DT phandle로는 참조할 수 없다 — 그래서 §1에서는 DT 버전을 썼다.
- `sim_gpioN/pull`은 "외부 회로", 우리 C 프로그램은 "consumer". pull-up → 읽기 1. 같은 라인을 `ACTIVE_LOW`로 요청하면 같은 전기 상태가 논리 0으로 읽힌다 — uAPI에도 커널 API와 똑같은 논리/물리 구분이 있다.
- 출력으로 요청해 1을 쓰면 sim 쪽 `value`가 1. **프로그램이 끝나 fd가 닫히자** 라인이 풀려 value가 pull 값(0)으로 돌아갔다. sysfs의 "export 해 두고 잊어버림" 문제가 구조적으로 없다.
- `BIAS_PULL_UP` 플래그로 요청하니 1이 읽힌다. gpio-sim의 `set_config`가 바이어스를 받아 pull로 반영하기 때문이다. 실제 SoC에서는 이 요청이 pinctrl의 패드 설정(풀업 비트)까지 내려간다.

### 7.4 예제 9 — 엣지 이벤트: timestamp, seqno, 소프트웨어 디바운스

```c
/* h4_mon: 라인 하나의 엣지 이벤트를 timeout까지 받으며 timestamp·seqno 출력 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s chip offset timeout_ms [debounce_us]\n", argv[0]);
		exit(2);
	}
	struct gpio_v2_line_request req;
	int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
	__u64 prev = 0;
	int n = 0;

	if (fd < 0) { perror(argv[1]); exit(1); }
	memset(&req, 0, sizeof(req));
	req.offsets[0] = atoi(argv[2]);
	req.num_lines = 1;
	strcpy(req.consumer, "h4_mon");
	req.config.flags = GPIO_V2_LINE_FLAG_INPUT |
		GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING;
	if (argc > 4) {
		req.config.num_attrs = 1;
		req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_DEBOUNCE;
		req.config.attrs[0].attr.debounce_period_us = atoi(argv[4]);
		req.config.attrs[0].mask = 1;
	}
	if (ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) { perror("GET_LINE"); exit(1); }
	printf("watching offset %s (fd=%d)\n", argv[2], req.fd);
	fflush(stdout);
	for (;;) {
		struct pollfd p = { .fd = req.fd, .events = POLLIN };
		struct gpio_v2_line_event ev;

		if (poll(&p, 1, atoi(argv[3])) <= 0)
			break;			/* timeout → 끝 */
		if (read(req.fd, &ev, sizeof(ev)) != sizeof(ev)) { perror("read"); exit(1); }
		printf("%-7s ts=%llu.%09llu seq=%u line_seq=%u dt=%lluus\n",
		       ev.id == GPIO_V2_LINE_EVENT_RISING_EDGE ? "RISING" : "FALLING",
		       ev.timestamp_ns / 1000000000ULL, ev.timestamp_ns % 1000000000ULL,
		       ev.seqno, ev.line_seqno,
		       prev ? (ev.timestamp_ns - prev) / 1000 : 0ULL);
		prev = ev.timestamp_ns;
		n++;
	}
	printf("events=%d\n", n);
	return 0;
}
```

```sh
echo "--- 5 pulses on BTN(3), no debounce"
./h4_mon /dev/gpiochip1 3 1500 & sleep 1
for i in 1 2 3 4 5; do pull 3 pull-up; pull 3 pull-down; done; wait
echo "--- 5 fast pulses, debounce 20000us"
./h4_mon /dev/gpiochip1 3 1500 20000 & sleep 1
for i in 1 2 3 4 5; do pull 3 pull-up; pull 3 pull-down; done; pull 3 pull-up; wait
```

```text
--- 5 pulses on BTN(3), no debounce
watching offset 3 (fd=4)
RISING  ts=1.892080192 seq=1 line_seq=1 dt=0us
FALLING ts=1.894584912 seq=2 line_seq=2 dt=2504us
RISING  ts=1.895554864 seq=3 line_seq=3 dt=969us
FALLING ts=1.896503824 seq=4 line_seq=4 dt=948us
RISING  ts=1.897281408 seq=5 line_seq=5 dt=777us
FALLING ts=1.897726944 seq=6 line_seq=6 dt=445us
RISING  ts=1.898148288 seq=7 line_seq=7 dt=421us
FALLING ts=1.898861952 seq=8 line_seq=8 dt=713us
RISING  ts=1.899793120 seq=9 line_seq=9 dt=931us
FALLING ts=1.900291408 seq=10 line_seq=10 dt=498us
events=10
--- 5 fast pulses, debounce 20000us
watching offset 3 (fd=4)
RISING  ts=4.456725664 seq=1 line_seq=1 dt=0us
events=1
```

출력에서 볼 것:

- `ts`는 기본적으로 `CLOCK_MONOTONIC` ns다(요청 플래그 `GPIO_V2_LINE_FLAG_EVENT_CLOCK_REALTIME`이나 하드웨어 타임스탬프 엔진(HTE)으로 바꿀 수 있다). **IRQ가 들어온 순간**에 찍히므로, 유저가 늦게 read 해도 엣지 사이 간격(`dt`)은 정확하다. 셸 `echo` 하나가 TCG에서 0.4~1 ms 걸린다는 것도 보인다.
- `seq`는 이 요청 전체의 순서, `line_seq`는 라인별 순서다. **번호가 건너뛰면 FIFO에서 이벤트가 버려진 것**이다 (§8에서 일부러 만든다).
- 디바운스 20 ms를 주자 10번의 엣지가 이벤트 1개가 됐다: 라인이 20 ms 동안 안정된 뒤의 최종 상태(High) 한 번. gpio-sim은 하드웨어 디바운스가 없으므로(`-ENOTSUPP`) gpiolib-cdev의 `debounce_setup()`이 IRQ + 지연 작업으로 **소프트웨어 디바운스**를 대신 건다(랩 소스 `drivers/gpio/gpiolib-cdev.c`에서 확인).

### 7.5 예제 10 — VM 호스트의 libgpiod 도구로 같은 일 하기

게스트에는 libgpiod가 없지만 VM 호스트(Ubuntu, libgpiod v2.2.1)에는 있다. 호스트 커널의 gpio-sim 모듈(`modprobe gpio-sim`)로 칩을 만들고 도구를 써 봤다. 명령마다 실제 출력이다.

```sh
gpiodetect
```

```text
gpiochip0 [ARMH0061:00] (8 lines)
gpiochip1 [h4_host_bank] (4 lines)
```

```sh
gpioinfo -c gpiochip1
gpioget IMU_INT; gpioget --active-low IMU_INT
```

```text
gpiochip1 - 4 lines:
	line   0:	"IMU_RST_N"     	input
	line   1:	unnamed         	input
	line   2:	"IMU_INT"       	input
	line   3:	unnamed         	input
"IMU_INT"=inactive
"IMU_INT"=active
```

```sh
# 다른 셸에서 sim_gpio2/pull을 0.2초 간격으로 up/down/up/down
timeout 5 gpiomon -e both --num-events=4 IMU_INT
```

```text
39905.022336584	rising	"IMU_INT"
39905.230221216	falling	"IMU_INT"
39905.437365346	rising	"IMU_INT"
39905.644549768	falling	"IMU_INT"
```

```sh
gpioset -t0 IMU_RST_N=1; cat .../sim_gpio0/value           # 설정하고 즉시 종료
timeout 1 gpioset IMU_RST_N=1 & sleep 0.5; cat .../sim_gpio0/value   # 붙잡고 있는 동안
```

```text
sim_gpio0/value=0 (after gpioset exits)
while gpioset holds: sim_gpio0/value=1
```

출력에서 볼 것: libgpiod v2의 `gpioset`은 **프로세스가 살아 있는 동안만** 값을 유지한다(`-t0`이면 설정 직후 종료 → 라인 해제 → 값이 사라짐). sysfs 시절 "echo 1 하고 나면 계속 1"에 익숙한 사람이 가장 많이 당황하는 지점이다. 값을 유지하려면 `gpioset`을 데몬처럼 띄워 두거나, 그 핀을 커널 드라이버(또는 hog)가 소유하게 해야 한다. libgpiod 도구는 전부 §7.2의 ioctl을 감싼 것이다.

| 도구 | 쓰는 ioctl |
|---|---|
| `gpiodetect` | `GPIO_GET_CHIPINFO_IOCTL` |
| `gpioinfo` | `GPIO_V2_GET_LINEINFO_IOCTL` |
| `gpioget` / `gpioset` | `GPIO_V2_GET_LINE_IOCTL` + `LINE_GET_VALUES` / `LINE_SET_VALUES` |
| `gpiomon` | `GPIO_V2_GET_LINE_IOCTL`(엣지 플래그) + `read()` |
| `gpionotify` | `GPIO_V2_GET_LINEINFO_WATCH_IOCTL` |

### 7.6 예제 11 — 라인 감시: 커널 드라이버가 라인을 가져가는 순간 잡기

```c
/* h4_watch: 라인 정보 변화(누가 요청/해제/재설정했나)를 감시 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>

static const char *type[] = { "?", "REQUESTED", "RELEASED", "CONFIG" };

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s chip timeout_ms offset...\n", argv[0]);
		exit(2);
	}
	int fd = open(argv[1], O_RDONLY | O_CLOEXEC);

	if (fd < 0) { perror(argv[1]); exit(1); }
	for (int i = 3; i < argc; i++) {
		struct gpio_v2_line_info li;

		memset(&li, 0, sizeof(li));
		li.offset = atoi(argv[i]);
		if (ioctl(fd, GPIO_V2_GET_LINEINFO_WATCH_IOCTL, &li) < 0) {
			perror("WATCH"); exit(1);
		}
		printf("watch line %u (%s): consumer=\"%s\"\n", li.offset, li.name, li.consumer);
	}
	fflush(stdout);
	for (;;) {
		struct pollfd p = { .fd = fd, .events = POLLIN };
		struct gpio_v2_line_info_changed ch;

		if (poll(&p, 1, atoi(argv[2])) <= 0)
			break;
		if (read(fd, &ch, sizeof(ch)) != sizeof(ch)) { perror("read"); exit(1); }
		printf("ts=%llu.%06llu %-9s line %u consumer=\"%s\" flags=0x%llx\n",
		       ch.timestamp_ns / 1000000000ULL, ch.timestamp_ns % 1000000000ULL / 1000,
		       type[ch.event_type < 4 ? ch.event_type : 0], ch.info.offset,
		       ch.info.consumer, (unsigned long long)ch.info.flags);
		fflush(stdout);
	}
	return 0;
}
```

`probe` 시나리오에서 insmod 전에 라인 0, 2를 감시해 두었고, `watchrm` 시나리오에서는 드라이버가 잡고 있는 상태에서 라인 3을 유저가 잠깐 쓰고 rmmod 했다.

```text
=== watch ===
watch line 0 (IMU_RST_N): consumer=""
watch line 2 (IMU_INT): consumer=""
ts=1.905732 REQUESTED line 0 consumer="reset" flags=0xb
ts=1.906110 REQUESTED line 2 consumer="irq" flags=0x5
```

```sh
insmod /h4_sensor.ko; sleep 1
./h4_watch /dev/gpiochip1 3000 0 2 3 & sleep 1
./h4_line /dev/gpiochip1 3 out=1 al; rmmod h4_sensor; wait
```

```text
watch line 0 (IMU_RST_N): consumer="reset"
watch line 2 (IMU_INT): consumer="irq"
watch line 3 (BTN): consumer=""
offset 3 out: value=1 (logical)
ts=2.924166 REQUESTED line 3 consumer="h4_line" flags=0xb
ts=2.927845 RELEASED  line 3 consumer="" flags=0x8
ts=2.938596 RELEASED  line 2 consumer="" flags=0x4
ts=2.938696 RELEASED  line 0 consumer="" flags=0x8
```

출력에서 볼 것:

- 커널 드라이버의 `devm_gpiod_get`도 유저 요청과 똑같이 `REQUESTED` 이벤트를 만든다. consumer 이름은 con_id(`reset`, `irq`)다.
- flags: `0xb` = USED(1) + ACTIVE_LOW(2) + OUTPUT(8), `0x5` = USED + INPUT(4). 리셋 핀이 active-low 출력으로 잡혔다는 것이 유저 공간에서도 보인다.
- rmmod 하면 `devm` 자원이 역순으로 풀리며 `RELEASED`가 온다(2번 irq가 0번 reset보다 먼저 — 요청 역순).
- 감시 이벤트의 `ts`는 `CLOCK_MONOTONIC`이고 dmesg 시각(`local_clock`)과는 다른 시계라 직접 비교하지 않는다.
- 현장 용도: "누가 부팅 중에 내 핀을 가져갔나"를 잡는 데 쓴다. `gpionotify`가 같은 일을 한다.

---

## 8. 측정 — 엣지가 빨라지면 어디서 사라지나

### 8.1 두 가지 유실 지점

엣지 이벤트가 유저에게 오는 길에는 버퍼가 두 군데 있다.

1. **IRQ 단계**: 컨트롤러의 엣지 래치는 보통 1비트다. 핸들러가 돌기 전에 엣지가 두 번 오면 하나로 합쳐진다. 게다가 gpiolib-cdev는 엣지 IRQ를 `IRQF_ONESHOT` threaded로 요청하므로 스레드가 끝날 때까지 그 IRQ는 마스크돼 있다. gpio-sim의 `irq_sim`은 마스크된 동안 들어온 pending을 **아예 기록하지 않는다**(`irq_sim_set_irqchip_state()`가 `enabled`일 때만 pending 비트를 세움 — 랩 소스에서 확인). 실제 SoC GPIO 컨트롤러는 대개 마스크와 무관하게 상태 비트가 서서 "1번으로 합쳐짐"이 된다.
2. **FIFO 단계**: 라인 요청마다 이벤트 kfifo가 있고 크기는 `event_buffer_size`(0이면 라인 수 × 16, 최대 64 × 16). 가득 차면 `linereq_put_event()`가 **가장 오래된 이벤트를 버리고**(`kfifo_skip`) 새 것을 넣는다.

`line_seqno`는 커널이 이벤트를 **만들 때** 붙이므로, 이것으로 둘을 구분할 수 있다.

- `generated` = 마지막으로 받은 이벤트의 `line_seqno` (커널이 만든 이벤트 수)
- `fifo_drop` = generated − 받은 개수 (FIFO에서 버려짐)
- `irq_merged` = 토글 수 − generated (IRQ 단계에서 합쳐지거나 사라져 이벤트가 아예 안 만들어짐)

### 8.2 예제 12 — 속도별 유실 측정 프로그램

무엇을 확인하나: 메인 스레드가 sim `pull` 파일에 정해진 속도로 up/down을 1000번 쓰고, 리더 스레드가 이벤트마다 `proc_us`만큼 바쁜 대기(이벤트 처리 흉내)를 하며 받는다.

```c
/* h4_rate: sim 라인 pull을 hz로 n번 토글 → 발생(line_seqno)·수신·FIFO 유실 집계 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>

static int lfd, got, lastseq, proc_us;
static unsigned long long now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000000ULL + t.tv_nsec;
}
static void *reader(void *arg)
{
	struct gpio_v2_line_event ev;
	struct pollfd p = { .fd = lfd, .events = POLLIN };

	while (poll(&p, 1, 1000) > 0 && read(lfd, &ev, sizeof(ev)) == sizeof(ev)) {
		got++, lastseq = ev.line_seqno;		/* line_seqno = 커널이 만든 이벤트 번호 */
		for (unsigned long long t = now(); now() - t < proc_us * 1000ULL; )
			;				/* 이벤트당 처리 시간 흉내 */
	}
	return arg;
}
int main(int argc, char **argv)
{
	if (argc < 8) { fprintf(stderr, "usage: chip off pullfile hz n proc_us bufsz\n"); exit(2); }
	int cfd = open(argv[1], O_RDONLY), pfd = open(argv[3], O_WRONLY);
	int hz = atoi(argv[4]), n = atoi(argv[5]);
	struct gpio_v2_line_request req = { .num_lines = 1, .event_buffer_size = atoi(argv[7]) };
	pthread_t th;

	proc_us = atoi(argv[6]);
	if (cfd < 0 || pfd < 0) { perror("open"); exit(1); }
	req.offsets[0] = atoi(argv[2]);
	req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING |
			   GPIO_V2_LINE_FLAG_EDGE_FALLING;
	if (ioctl(cfd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) { perror("GET_LINE"); exit(1); }
	lfd = req.fd;
	pthread_create(&th, NULL, reader, NULL);
	unsigned long long t0 = now(), period = hz ? 1000000000ULL / hz : 0;
	for (int k = 0; k < n; k++) {
		unsigned long long t = t0 + k * period;
		struct timespec ts = { t / 1000000000ULL, t % 1000000000ULL };

		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
		if (pwrite(pfd, k % 2 ? "pull-down" : "pull-up", k % 2 ? 9 : 7, 0) < 0) { perror("pull"); exit(1); }
	}
	double secs = (now() - t0) / 1e9;
	pthread_join(th, NULL);
	printf("req=%5dHz actual=%5.0fHz toggles=%d generated=%d delivered=%d fifo_drop=%d irq_merged=%d\n",
	       hz, n / secs, n, lastseq, got, lastseq - got, n - lastseq);
	return 0;
}
```

(`proc_us`는 처리 시간을 흉내 내는 값, `hz=0`은 "쉬지 않고 최대한 빨리".) 시나리오 `rate3`로 (처리 0 us, 버퍼 256), (처리 2 ms, 버퍼 16), (처리 2 ms, 버퍼 256) 세 설정을 속도 100/200/500/1000 Hz/최대로 3회 반복했다. 3회 원자료 중 1회차 일부:

```text
rep=1 cfg=0 256 req=  100Hz actual=  100Hz toggles=1000 generated=991 delivered=991 fifo_drop=0 irq_merged=9
rep=1 cfg=0 256 req=  200Hz actual=  200Hz toggles=1000 generated=874 delivered=874 fifo_drop=0 irq_merged=126
rep=1 cfg=0 256 req=  500Hz actual=  500Hz toggles=1000 generated=878 delivered=878 fifo_drop=0 irq_merged=122
rep=1 cfg=0 256 req= 1000Hz actual=  916Hz toggles=1000 generated=932 delivered=932 fifo_drop=0 irq_merged=68
rep=1 cfg=2000 16 req=  500Hz actual=  500Hz toggles=1000 generated=999 delivered=955 fifo_drop=44 irq_merged=1
rep=1 cfg=2000 16 req= 1000Hz actual= 1000Hz toggles=1000 generated=1000 delivered=485 fifo_drop=515 irq_merged=0
rep=1 cfg=2000 16 req=    0Hz actual= 2247Hz toggles=1000 generated=896 delivered=187 fifo_drop=709 irq_merged=104
rep=1 cfg=2000 256 req=  500Hz actual=  499Hz toggles=1000 generated=993 delivered=993 fifo_drop=0 irq_merged=7
rep=1 cfg=2000 256 req= 1000Hz actual=  990Hz toggles=1000 generated=1000 delivered=692 fifo_drop=308 irq_merged=0
rep=1 cfg=2000 256 req=    0Hz actual= 1483Hz toggles=1000 generated=797 delivered=535 fifo_drop=262 irq_merged=203
```

3회의 `delivered`(1000개 중 받은 수)를 정리하면:

| 설정 | 100 Hz | 200 Hz | 500 Hz | 1000 Hz |
|---|---|---|---|---|
| 처리 0 us, 버퍼 256 | 991 / 1000 / 996 | 874 / 982 / 1000 | 878 / 787 / 918 | 932 / 868 / 723 |
| 처리 2 ms, 버퍼 16 | 995 / 1000 / 999 | 999 / 1000 / 1000 | 955 / 931 / 888 | 485 / 382 / 500 |
| 처리 2 ms, 버퍼 256 | 1000 / 997 / 1000 | 986 / 998 / 1000 | 993 / 1000 / 999 | 692 / 698 / 718 |

```svg
<svg viewBox="0 0 640 330" xmlns="http://www.w3.org/2000/svg">
<text x="320" y="22" text-anchor="middle" font-size="14">토글 속도별 이벤트 전달률 (1000번 토글, 3회 중앙값)</text>
<line x1="60" y1="260.0" x2="620" y2="260.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="264.0" text-anchor="end" font-size="12">0%</text>
<line x1="60" y1="205.0" x2="620" y2="205.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="209.0" text-anchor="end" font-size="12">25%</text>
<line x1="60" y1="150.0" x2="620" y2="150.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="154.0" text-anchor="end" font-size="12">50%</text>
<line x1="60" y1="95.0" x2="620" y2="95.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="99.0" text-anchor="end" font-size="12">75%</text>
<line x1="60" y1="40.0" x2="620" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="44.0" text-anchor="end" font-size="12">100%</text>
<line x1="60" y1="260" x2="620" y2="260" stroke="currentColor"/>
<line x1="60" y1="40" x2="60" y2="260" stroke="currentColor"/>
<rect x="81.0" y="40.9" width="30" height="219.1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="96.0" y="36.9" text-anchor="middle" font-size="12">100</text>
<rect x="115.0" y="40.2" width="30" height="219.8" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="130.0" y="36.2" text-anchor="middle" font-size="12">100</text>
<rect x="149.0" y="40.0" width="30" height="220.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="164.0" y="36.0" text-anchor="middle" font-size="12">100</text>
<text x="130.0" y="278" text-anchor="middle" font-size="13">100 Hz</text>
<rect x="221.0" y="44.0" width="30" height="216.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="236.0" y="40.0" text-anchor="middle" font-size="12">98</text>
<rect x="255.0" y="40.0" width="30" height="220.0" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="270.0" y="36.0" text-anchor="middle" font-size="12">100</text>
<rect x="289.0" y="40.4" width="30" height="219.6" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="304.0" y="36.4" text-anchor="middle" font-size="12">100</text>
<text x="270.0" y="278" text-anchor="middle" font-size="13">200 Hz</text>
<rect x="361.0" y="66.8" width="30" height="193.2" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="376.0" y="62.8" text-anchor="middle" font-size="12">88</text>
<rect x="395.0" y="55.2" width="30" height="204.8" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="410.0" y="51.2" text-anchor="middle" font-size="12">93</text>
<rect x="429.0" y="40.2" width="30" height="219.8" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="444.0" y="36.2" text-anchor="middle" font-size="12">100</text>
<text x="410.0" y="278" text-anchor="middle" font-size="13">500 Hz</text>
<rect x="501.0" y="69.0" width="30" height="191.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="516.0" y="65.0" text-anchor="middle" font-size="12">87</text>
<rect x="535.0" y="153.3" width="30" height="106.7" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="550.0" y="149.3" text-anchor="middle" font-size="12">48</text>
<rect x="569.0" y="106.4" width="30" height="153.6" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="584.0" y="102.4" text-anchor="middle" font-size="12">70</text>
<text x="550.0" y="278" text-anchor="middle" font-size="13">1000 Hz</text>
<text x="340" y="296" text-anchor="middle" font-size="12">요청 토글 속도 (엣지/초)</text>
<rect x="70" y="308" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="88" y="318" font-size="12">처리 0us · 버퍼 256</text>
<rect x="260" y="308" width="12" height="12" fill="#d0564a" fill-opacity="0.75" stroke="#d0564a"/>
<text x="278" y="318" font-size="12">처리 2ms · 버퍼 16</text>
<rect x="450" y="308" width="12" height="12" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="468" y="318" font-size="12">처리 2ms · 버퍼 256</text>
</svg>
```
그림 5 — 3회 중앙값으로 그린 전달률. 파랑의 손실은 전부 `irq_merged`(IRQ 단계), 빨강·초록의 손실은 대부분 `fifo_drop`(FIFO 단계)이다. QEMU TCG 위의 gpio-sim 측정이라 절대값보다 경향을 본다.

출력에서 볼 것:

- **FIFO 단계 (빨강 vs 초록)**: 처리 2 ms 리더는 초당 약 500개를 처리한다. 500 Hz에서 버퍼 16은 지터를 못 견뎌 4~11% 버렸고, 버퍼 256은 0~0.7%. 1000 Hz에서는 처리 능력의 두 배라 둘 다 버린다. 1초 동안 리더가 약 450~500개 처리 + 끝에 버퍼에 남은 만큼이 전달되므로 버퍼 16 → 약 480, 버퍼 256 → 약 700 — 표의 숫자와 맞다. **버퍼는 순간 폭주를 흡수할 뿐, 평균 처리량이 모자라면 해결책이 아니다.**
- **IRQ 단계 (파랑)**: 리더가 빠른데도(처리 0) 회차에 따라 최대 28%가 사라졌다. 전부 `fifo_drop=0, irq_merged>0` — 이벤트가 만들어지기 전에 엣지가 합쳐지거나 마스크 중에 사라진 것이다. 실행마다 편차가 크다(TCG의 vCPU 스케줄링). 같은 설정을 다른 부팅(`rate` 시나리오, 버퍼 16)에서 돌렸을 때는 1000 Hz에서도 1000/1000이 나오기도 했다.
- 실무 결론: **GPIO 엣지 인터럽트로 고속 펄스를 세지 말 것.** 휠 엔코더·유량계·PPS 이외의 고속 신호는 타이머 input capture, eQEP, PWM capture 같은 하드웨어 카운터로 센다. GPIO 이벤트는 "가끔 오는 사건"(버튼, DRDY, 경보)용이다.

### 8.3 예제 13 — 지연: 엣지에서 유저까지

무엇을 확인하나: 한 번에 하나씩(핑퐁) — pull을 쓰고, 그 이벤트를 받을 때까지 블로킹 read, 400회. 두 구간을 나눠 잰다: (a) pull 쓰기 → 커널이 IRQ에서 찍은 timestamp, (b) 그 timestamp → 유저 read 복귀.

```c
/* h4_lat: 핑퐁 — pull 쓰기 → 이벤트 받을 때까지 기다리기를 n번, 단계별 지연 분포 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>

#define N 400
static unsigned long long a[N], b[N];	/* a: write→IRQ timestamp, b: timestamp→read 복귀 */

static unsigned long long now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000000ULL + t.tv_nsec;
}
static int cmp(const void *x, const void *y)
{
	unsigned long long p = *(const unsigned long long *)x, q = *(const unsigned long long *)y;
	return p > q ? 1 : p < q ? -1 : 0;
}
static void pr(const char *tag, unsigned long long *v)
{
	qsort(v, N, sizeof(*v), cmp);
	printf("%-14s p10=%5lluus p50=%5lluus p90=%5lluus p99=%5lluus max=%5lluus\n", tag,
	       v[N / 10] / 1000, v[N / 2] / 1000, v[N * 9 / 10] / 1000, v[N * 99 / 100] / 1000, v[N - 1] / 1000);
}
int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: chip off pullfile\n"); exit(2); }
	int cfd = open(argv[1], O_RDONLY), pfd = open(argv[3], O_WRONLY);
	struct gpio_v2_line_request req = { .num_lines = 1 };
	struct gpio_v2_line_event ev;

	if (cfd < 0 || pfd < 0) { perror("open"); exit(1); }
	req.offsets[0] = atoi(argv[2]);
	req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING |
			   GPIO_V2_LINE_FLAG_EDGE_FALLING;
	if (ioctl(cfd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) { perror("GET_LINE"); exit(1); }
	for (int k = 0; k < N; k++) {
		unsigned long long tw = now();

		if (pwrite(pfd, k % 2 ? "pull-down" : "pull-up", k % 2 ? 9 : 7, 0) < 0) { perror("pull"); exit(1); }
		if (read(req.fd, &ev, sizeof(ev)) != sizeof(ev)) { perror("read"); exit(1); }	/* 블로킹 */
		a[k] = ev.timestamp_ns - tw;
		b[k] = now() - ev.timestamp_ns;
	}
	pr("write->irq_ts", a);
	pr("irq_ts->user", b);
	return 0;
}
```

```text
cpus=2
--- latency (ping-pong, 400 samples)
write->irq_ts  p10=   44us p50=   53us p90=  127us p99=  513us max= 1264us
irq_ts->user   p10=  127us p50=  201us p90=  348us p99=  785us max= 3976us
```

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
<text x="320" y="22" text-anchor="middle" font-size="14">핑퐁 400회 지연 분포 (QEMU TCG, 2 vCPU)</text>
<line x1="60" y1="240.0" x2="620" y2="240.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="244.0" text-anchor="end" font-size="12">0us</text>
<line x1="60" y1="190.0" x2="620" y2="190.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="194.0" text-anchor="end" font-size="12">200us</text>
<line x1="60" y1="140.0" x2="620" y2="140.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="144.0" text-anchor="end" font-size="12">400us</text>
<line x1="60" y1="90.0" x2="620" y2="90.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="94.0" text-anchor="end" font-size="12">600us</text>
<line x1="60" y1="40.0" x2="620" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="52" y="44.0" text-anchor="end" font-size="12">800us</text>
<line x1="60" y1="240" x2="620" y2="240" stroke="currentColor"/><line x1="60" y1="40" x2="60" y2="240" stroke="currentColor"/>
<rect x="88.0" y="229.0" width="40" height="11.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="108.0" y="225.0" text-anchor="middle" font-size="12">44</text>
<rect x="132.0" y="208.2" width="40" height="31.8" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="152.0" y="204.2" text-anchor="middle" font-size="12">127</text>
<text x="130.0" y="258" text-anchor="middle" font-size="13">p10</text>
<rect x="228.0" y="226.8" width="40" height="13.2" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="248.0" y="222.8" text-anchor="middle" font-size="12">53</text>
<rect x="272.0" y="189.8" width="40" height="50.2" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="292.0" y="185.8" text-anchor="middle" font-size="12">201</text>
<text x="270.0" y="258" text-anchor="middle" font-size="13">p50</text>
<rect x="368.0" y="208.2" width="40" height="31.8" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="388.0" y="204.2" text-anchor="middle" font-size="12">127</text>
<rect x="412.0" y="153.0" width="40" height="87.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="432.0" y="149.0" text-anchor="middle" font-size="12">348</text>
<text x="410.0" y="258" text-anchor="middle" font-size="13">p90</text>
<rect x="508.0" y="111.8" width="40" height="128.2" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="528.0" y="107.8" text-anchor="middle" font-size="12">513</text>
<rect x="552.0" y="43.8" width="40" height="196.2" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="572.0" y="39.8" text-anchor="middle" font-size="12">785</text>
<text x="550.0" y="258" text-anchor="middle" font-size="13">p99</text>
<rect x="70" y="276" width="12" height="12" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="88" y="286" font-size="12">write→IRQ timestamp</text>
<rect x="330" y="276" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="348" y="286" font-size="12">IRQ timestamp→유저 read 복귀</text>
</svg>
```
그림 6 — 핑퐁 400회 지연 분포. 같은 프로그램을 다른 부팅에서 돌렸을 때는 p50이 66 us / 208 us였다 (실행마다 다르다).

출력에서 볼 것:

- (a)는 sysfs 쓰기 → `gpio_sim_apply_pull()` → `irq_set_irqchip_state()` → irq_work → hardirq 핸들러(`edge_irq_handler`가 timestamp 기록)까지다. 실제 보드라면 이 구간은 "핀 엣지 → GIC → 핸들러"라 훨씬 짧다.
- (b)는 hardirq → IRQ 스레드(`edge_irq_thread`가 이벤트를 만들어 kfifo에 넣고 `wake_up_poll`) → 리더 스레드 스케줄링 → `read()` 복귀다. 꼬리(p99, max)가 길다 — 유저 공간 GPIO의 근본 한계다. 하드 실시간 반응(비상 정지)은 하드웨어나 MCU가 맡고, Linux 유저 공간은 기록과 상위 판단을 맡는 설계가 맞다 (A4 §11.2와 같은 결론).
- 그래도 **timestamp는 IRQ 시점**이므로 "언제 일어났나"는 (b)의 지터와 무관하게 정확하다. 센서 DRDY의 시각을 유저 공간에서 기록해 데이터와 맞추는 용도라면 uAPI 이벤트로 충분한 경우가 많다.

---

## 9. 소유권 충돌 — 두 드라이버가 같은 핀을 원할 때

### 9.1 예제 14 — `-EBUSY`와 "already requested"

DT에 일부러 넣은 두 도둑 노드는 insmod 하자마자 이런 로그를 냈다 (모든 시나리오에서 동일, `irq` 시나리오의 실제 로그).

```text
[    1.120281] h4_sensor h4-thief-gpio: error -EBUSY: thief: reset gpio
[    1.120410] h4_sensor h4-thief-gpio: probe with driver h4_sensor failed with error -16
[    1.120608] pinctrl-single 5ff00000.pinmux: pin PIN0 already requested by h4-sensor; cannot claim for h4-thief-pins
[    1.120635] pinctrl-single 5ff00000.pinmux: error -EINVAL: pin-0 (h4-thief-pins)
[    1.120675] pinctrl-single 5ff00000.pinmux: error -EINVAL: could not request pin 0 (PIN0) from group h4-default-pins on device pinctrl-single
[    1.120699] h4_sensor h4-thief-pins: Error applying setting, reverse things back
```

그리고 유저 공간에서 커널이 잡은 라인을 요청하면:

```sh
./h4_line /dev/gpiochip1 0 out=1; ./h4_line /dev/gpiochip1 2 in
```

```text
GET_LINE offset 0: Device or resource busy (errno 16)
GET_LINE offset 2: Device or resource busy (errno 16)
```

`h4-thief-pins`는 드라이버가 붙지 않은 채 남았다 (시나리오 `pinctrl`).

```text
=== thief-pins driver? ===
driver_override modalias of_node power subsystem supplier:platform:5ff00000.pinmux uevent waiting_for_supplier 
ls: /sys/devices/platform/h4-thief-pins/driver: No such file or directory
```

출력에서 볼 것:

- **GPIO 소유권**: 라인은 한 consumer만 가질 수 있다. 두 번째 `gpiod_get`은 `-EBUSY`(-16). 커널 드라이버든 유저 uAPI든 같은 규칙이다. 정말 공유해야 하는 경우(여러 장치가 같은 리셋 핀)를 위해 `GPIOD_FLAGS_BIT_NONEXCLUSIVE` 플래그가 있지만, 값 충돌을 누가 조정할지 직접 책임져야 해서 드물게 쓴다.
- **pin mux 소유권**: 패드 0은 `h4-sensor`가 mux owner다. 다른 장치가 같은 패드를 먹스하려 하면 pinctrl core가 거절한다. 실패는 **probe 함수가 불리기도 전**, `pinctrl_bind_pins()` 단계에서 일어난다. 그래서 드라이버의 probe 로그가 전혀 없고 `driver` 링크만 없다. "드라이버가 매칭은 됐는데 probe 로그가 하나도 안 찍힌다"면 pinctrl을 의심한다.
- 실무에서 이 충돌은 보통 **DT 실수**다: 두 노드가 같은 핀 그룹을 참조하거나, `gpio-hog`·`gpio-keys`·`gpio-leds`가 이미 잡은 핀을 새 드라이버가 또 요청한다. 진단 순서는 `/sys/kernel/debug/gpio`(GPIO consumer) → `/sys/kernel/debug/pinctrl/*/pinmux-pins`(mux owner) → `gpioinfo`.

### 9.2 GPIO 요청과 pinctrl의 연결 (`gpio-ranges`)

i.MX8MP dtsi의 GPIO 노드에는 `gpio-ranges = <&iomuxc 0 5 30>;` 같은 속성이 있다(랩 트리 `imx8mp.dtsi`에서 확인). "이 GPIO 칩의 offset 0부터 30개는 iomuxc의 핀 5번부터 대응한다"는 뜻이다. 이게 있으면 GPIO 드라이버가 `gpiochip_generic_request()`로 라인 요청 시 pinctrl에 그 핀을 **GPIO 용도로 요청**하고, `pinmux-pins`의 `gpio_owner` 칸이 채워진다. pin controller가 `strict` 모드면 같은 핀을 mux owner와 gpio owner가 동시에 가질 수 없다. 우리 실습의 pinctrl-single에는 `gpio-ranges`가 없어서 `(GPIO UNCLAIMED)`로 나왔다.

---

## 10. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 390" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="16" width="130" height="48" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="75" y="36" text-anchor="middle" font-size="13">핀 엣지</text>
  <text x="75" y="54" text-anchor="middle" font-size="12">(센서 INT)</text>
  <rect x="180" y="16" width="150" height="48" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="255" y="36" text-anchor="middle" font-size="13">GPIO 컨트롤러</text>
  <text x="255" y="54" text-anchor="middle" font-size="12">엣지 래치 1비트</text>
  <rect x="370" y="16" width="130" height="48" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="435" y="36" text-anchor="middle" font-size="13">GIC (chained)</text>
  <text x="435" y="54" text-anchor="middle" font-size="12">또는 irq_sim</text>
  <rect x="530" y="16" width="140" height="48" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="600" y="36" text-anchor="middle" font-size="13">irq_desc</text>
  <text x="600" y="54" text-anchor="middle" font-size="12">virq 22, hwirq 2</text>
  <line x1="140" y1="40" x2="178" y2="40" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="330" y1="40" x2="368" y2="40" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="500" y1="40" x2="528" y2="40" stroke="currentColor" marker-end="url(#ah5)"/>
  <path d="M600,64 L600,84 L160,84 L160,104" fill="none" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="84" x2="510" y2="104" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="20" y="98" font-size="12">A. 커널 consumer</text>
  <text x="560" y="98" font-size="12">B. uAPI</text>
  <rect x="10" y="106" width="300" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="126" text-anchor="middle" font-size="12">h4_sensor: request_threaded_irq</text>
  <text x="160" y="145" text-anchor="middle" font-size="12">IRQ 스레드 irq/22-h4_senso</text>
  <rect x="10" y="180" width="300" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="207" text-anchor="middle" font-size="12">h4_thread: edges++ (I2C 읽기 자리)</text>
  <line x1="160" y1="156" x2="160" y2="178" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="360" y="106" width="300" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="126" text-anchor="middle" font-size="12">edge_irq_handler (hardirq)</text>
  <text x="510" y="145" text-anchor="middle" font-size="12">timestamp_ns 기록</text>
  <rect x="360" y="176" width="300" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="196" text-anchor="middle" font-size="12">edge_irq_thread</text>
  <text x="510" y="215" text-anchor="middle" font-size="12">엣지 종류 판별 · line_seqno++</text>
  <rect x="360" y="246" width="300" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="266" text-anchor="middle" font-size="12">kfifo (event_buffer_size)</text>
  <text x="510" y="285" text-anchor="middle" font-size="12">가득 차면 가장 오래된 것 버림</text>
  <rect x="360" y="316" width="300" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="336" text-anchor="middle" font-size="12">wake_up_poll → 유저 read()</text>
  <text x="510" y="355" text-anchor="middle" font-size="12">struct gpio_v2_line_event</text>
  <line x1="510" y1="156" x2="510" y2="174" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="226" x2="510" y2="244" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="296" x2="510" y2="314" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="10" y="262" width="300" height="70" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="160" y="284" text-anchor="middle" font-size="12">한 라인은 A나 B 중 한쪽만 소유</text>
  <text x="160" y="304" text-anchor="middle" font-size="12">(gpio_desc의 FLAG_REQUESTED)</text>
  <text x="160" y="322" text-anchor="middle" font-size="12">두 번째 요청 → -EBUSY</text>
</svg>
```
그림 7 — 핀 엣지에서 두 종류의 소비자까지. A는 커널 드라이버가 `gpiod_to_irq` + `request_threaded_irq`로 직접 받는 길, B는 gpiolib-cdev가 같은 IRQ 인프라 위에 이벤트 큐를 얹어 유저에게 넘기는 길이다. gpio-sim에서는 GIC 대신 `irq_sim`(irq_work)이 IRQ를 만든다.

함수 경로를 정리하면 (6.18 기준 이름):

| 유저/드라이버 호출 | 커널 경로 | 다시 만나는 곳 |
|---|---|---|
| (probe 직전, 자동) | `really_probe()` → `pinctrl_bind_pins()` → `pinctrl_select_state()` → `pinmux_enable_setting()` + `pinconf_apply_setting()` → provider `set_mux` / `pin_config_set` | G3 (probe 흐름) |
| `devm_gpiod_get(dev, "reset", flags)` | `gpiod_find_and_request()` → `of_find_gpio()`(`reset-gpios` 파싱, D4) → `gpiod_request()`(FLAG_REQUESTED, 없으면 `-EBUSY`) → `gpiod_configure_flags()` → `direction_output` | D4, G5 (devm) |
| `gpiod_set_value_cansleep(d, 1)` | ACTIVE_LOW면 반전 → (open-drain이면 에뮬레이션) → `gc->set()` | H2 (I2C 확장기 `regmap`) |
| `gpiod_to_irq(d)` | `gc->to_irq()` 또는 gpiolib irqchip의 `irq_create_mapping()` → virq | H6 (irq_domain, nested IRQ) |
| `request_threaded_irq()` | `__setup_irq()`, `irq/N-name` 스레드 생성, 트리거 타입 → irqchip `irq_set_type` | F6 |
| `ioctl(GPIO_V2_GET_LINE_IOCTL)` | `gpiolib-cdev.c` `linereq_create()` → `gpiod_request_user()` → 엣지면 `edge_detector_setup()` → `request_threaded_irq(edge_irq_handler, edge_irq_thread, IRQF_ONESHOT)`, `anon_inode_getfile()`로 라인 fd 생성 | A3 (fd·anon inode), A4 (poll) |
| `read(line_fd)` | `linereq_read()` → kfifo에서 꺼냄, 비었으면 `wait_event_interruptible_locked` | F (wait queue) |
| `poll(line_fd)` | `linereq_poll()` → `poll_wait()` | A4, 드라이버 `.poll` 구현의 교과서 예 |

드라이버를 쓸 때 이 표에서 기억할 점: gpiolib-cdev의 `linereq_read`/`linereq_poll`은 "IRQ에서 kfifo에 넣고 wake_up, read에서 꺼내고 비었으면 잠듦"이라는 **문자 장치 드라이버의 표준 패턴** 그 자체다. 나중에 자기 센서 드라이버의 `/dev` 인터페이스를 만들 때(G 모듈의 misc device) 그대로 따라 하면 된다.

---

## 11. Embedded 실무에서 다시 보기

### 11.1 센서 키트의 IMU 노드 — 이 노트의 모든 것이 한 노드에

예를 들어 센서 키트에서 i.MX8MP에 SPI IMU를 붙인다면 DT는 이런 모양이 된다 (구조 예시, 실행하지 않음 — 핀 매크로 이름은 보드마다 다르다).

```dts
&ecspi2 {
	pinctrl-names = "default", "sleep";
	pinctrl-0 = <&pinctrl_ecspi2>;		/* SCLK/MOSI/MISO/CS 먹스 */
	pinctrl-1 = <&pinctrl_ecspi2_sleep>;	/* suspend 때 풀다운 입력으로 */
	status = "okay";

	imu@0 {
		compatible = "...";
		reg = <0>;
		spi-max-frequency = <10000000>;
		pinctrl-names = "default";
		pinctrl-0 = <&pinctrl_imu>;		/* INT·RST 패드를 GPIO로 */
		interrupt-parent = <&gpio5>;
		interrupts = <18 IRQ_TYPE_LEVEL_LOW>;
		reset-gpios = <&gpio5 19 GPIO_ACTIVE_LOW>;
	};
};
```

인터럽트는 `interrupts`로 직접 적었다(드라이버가 `spi->irq`로 받음). `irq-gpios`로 적고 드라이버가 `gpiod_to_irq`하는 방식(우리 실습)은 드라이버가 핀 레벨도 읽어야 할 때 쓴다. 바인딩이 어느 쪽을 요구하는지 확인한다.

### 11.2 부팅 때 핀이 튄다 — 모터 enable 글리치

모터 드라이버 enable 핀이 부팅 중 수 ms 동안 High가 되어 모터가 움찔한다. 원인 후보:

- 리셋 후 패드 기본값이 풀업 입력인데 외부 풀다운이 없다 → **하드웨어**(외부 풀다운 저항)로 해결하는 게 맞다. 소프트웨어는 커널이 뜨기 전 수백 ms를 책임질 수 없다.
- 드라이버가 `GPIOD_IN`으로 받고 나중에 `direction_output(0)` → `GPIOD_OUT_LOW`로 바꾼다.
- 부트로더가 켜 둔 값을 커널이 바꾸면 안 되는 경우 → `GPIOD_ASIS`, 또는 DT hog로 부팅 초기에 고정, 또는 pinctrl `init` state.

### 11.3 GPIO가 모자라 I2C 확장기로 옮겼더니

새 보드 리비전에서 카메라 리셋 핀이 PCA9555로 옮겨졌다. 증상: `gpiod_set_value` WARN(§5.5), 그리고 IRQ 핸들러를 hardirq로만 등록한 드라이버는 `request_irq`가 `-EINVAL`. 해결은 드라이버를 처음부터 `_cansleep` + threaded IRQ로 쓰는 것. 확장기 자신은 I2C 드라이버라 `-EPROBE_DEFER` 순서 문제도 생긴다(카메라 드라이버가 확장기보다 먼저 probe → `devm_gpiod_get`이 `-EPROBE_DEFER` → 나중에 재시도, G 모듈).

### 11.4 현장 디버그 순서

```sh
cat /sys/kernel/debug/gpio                       # 누가 어느 라인을 어떤 방향·값으로
cat /sys/kernel/debug/pinctrl/*/pinmux-pins      # 패드 mux owner
cat /sys/kernel/debug/pinctrl/pinctrl-handles    # 장치별 current state
gpioinfo                                         # 이름·consumer·플래그 (유저 공간)
grep -i gpio /proc/interrupts                    # GPIO IRQ 카운트가 오르나
```

그다음에야 스코프로 핀을 본다. 레지스터를 직접 보고 싶으면 `devmem`으로 먹스·패드 레지스터를 읽어 DT 값과 비교한다(§4.3처럼).

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| DT에 `GPIO_ACTIVE_LOW` + 드라이버에서도 반전 | 리셋이 안 걸리거나 계속 걸림 | 이중 반전 | 드라이버는 논리값만, 극성은 DT에만 |
| 잠드는 칩(I2C 확장기)에 `gpiod_set_value()` | `WARNING ... gpiolib.c ... gpiod_set_value` | `can_sleep` 칩에 atomic API | 프로세스 컨텍스트면 `_cansleep` |
| `GPIOD_IN`으로 받고 나중에 출력 전환 | 부팅 시 짧은 글리치 | 방향·값 설정 사이 틈 | `GPIOD_OUT_LOW/HIGH`로 원자적 설정 |
| 두 노드가 같은 GPIO 참조 | 두 번째 probe `-EBUSY` | 라인 배타 소유 | DT 정리, 공유가 필요하면 설계 재검토 |
| 두 노드가 같은 핀 그룹 참조 | probe 로그 없이 드라이버 미바인딩, "already requested" | pin mux owner 충돌 | `pinmux-pins`로 owner 확인 후 DT 수정 |
| `pinctrl-0` 누락 | 신호가 안 나옴, 레지스터는 맞음 | 패드가 기본 기능(GPIO 입력) | consumer 노드에 `pinctrl-names`/`pinctrl-0` |
| pinctrl-single 값 의미 착각 | 설정 비트가 0 | 드라이버별 DT 값 포맷 차이 (§3.4) | 바인딩 + 소스 + debugfs `pins`로 확인 |
| hardirq 핸들러만으로 확장기 IRQ 요청 | `request_irq` `-EINVAL` | nested threaded IRQ | `request_threaded_irq(irq, NULL, fn, IRQF_ONESHOT, ..)` |
| 고속 펄스를 GPIO 이벤트로 셈 | 카운트 부족, `line_seqno` 건너뜀 | IRQ 래치 합쳐짐, FIFO 넘침 | 하드웨어 카운터(input capture) 사용 |
| `gpioset` 후 값이 유지될 거라 기대 | 값이 바로 사라짐 | uAPI는 fd 닫히면 해제 | 프로세스 유지, 또는 커널 consumer/hog |

---

## 13. 면접에서 이렇게 말한다

**Q.** "What is pin multiplexing and how does Linux handle it?"

**A.** SoC 패드 하나에 여러 내부 신호가 연결돼 있고 먹스 레지스터로 하나를 고른다. Linux에서는 pin controller 드라이버가 패드를 핀·그룹·함수로 등록하고, 장치 노드는 DT의 `pinctrl-names`/`pinctrl-N`으로 state를 정의한다. 드라이버 코어가 probe 직전에 `default` state를 자동 적용하고, 패드 설정(풀업, 구동 세기)도 같은 그룹에서 함께 적용된다.

> On an SoC each pad can be routed to several internal signals — GPIO, UART, I2C, PWM — selected by a mux register. In Linux a pin controller driver registers pins, groups and functions, and each device node declares states with pinctrl-names and pinctrl-0, -1 in the device tree. The driver core calls pinctrl_bind_pins right before probe and applies the default state, including pad configuration such as pull-ups and drive strength, so most drivers never touch pinctrl code.

**꼬리 질문**: "When would a driver select a pinctrl state itself?" → suspend의 `sleep`, I2C 버스 복구용 `gpio` state.

**Q.** "How does gpiod handle active-low signals?"

**A.** DT의 GPIO 플래그에 `GPIO_ACTIVE_LOW`를 쓰면 gpiolib이 디스크립터에 기록해 두고, `gpiod_set_value`/`get_value`가 논리값을 물리값으로 반전한다. 드라이버는 "assert/deassert"만 말한다. 실습에서 리셋 assert가 logical=1 raw=0으로 나왔다. 극성을 무시해야 할 때만 `_raw_` 함수를 쓴다.

> The polarity lives in the device tree flag, not in the driver. gpiolib stores ACTIVE_LOW in the descriptor and inverts on every get and set, so the driver deals only in logical values — one means asserted. If you also invert in the driver you get a double inversion, which is a classic bug. The raw variants bypass the flag and are only for cases where the physical level itself is the meaning.

**Q.** "What's the difference between gpiod_set_value and gpiod_set_value_cansleep?"

**A.** 칩이 잠들 수 있느냐다. SoC 내장 MMIO GPIO는 atomic 컨텍스트에서도 쓸 수 있지만, I2C/SPI 확장기는 버스 전송이 필요해 잠든다. 그런 칩에 atomic 버전을 쓰면 gpiolib이 WARN을 낸다. 프로세스 컨텍스트에서는 `_cansleep`을 기본으로 쓰면 하드웨어가 바뀌어도 안전하다.

> It's about whether the GPIO chip may sleep. A memory-mapped SoC controller can be accessed from hard IRQ context, but an I2C or SPI expander needs a bus transfer and can sleep. Calling the non-cansleep variant on such a chip triggers a WARN in gpiolib. My default in process context is the cansleep variant, so the driver keeps working when a later board revision moves the pin to an expander.

**Q.** "Why was the sysfs GPIO interface deprecated?"

**A.** 전역 번호가 동적이라 불안정하고, 소유권이 프로세스에 묶이지 않아 죽어도 export가 남고, 다중 라인 원자 조작·바이어스·디바운스 설정이 없고, 이벤트에 timestamp와 큐가 없다. 문자 장치 uAPI v2는 라인 요청 fd가 소유권이고 닫히면 해제되며, 이벤트에 timestamp와 seqno가 붙는다.

> Sysfs relied on global GPIO numbers that are assigned dynamically, ownership wasn't tied to a process so exported lines leaked, you couldn't change several lines atomically or request bias and debounce, and edge notification was just POLLPRI without timestamps or a queue. The character device API fixes all of that: the line request is a file descriptor, it's released when the fd closes, and events carry a timestamp and sequence numbers so you can detect drops.

**Q.** "How do you get an interrupt from a GPIO line in a driver?"

**A.** `devm_gpiod_get`으로 입력 라인을 받고 `gpiod_to_irq`로 IRQ 번호를 얻어 `devm_request_threaded_irq`를 부른다. DT에서 `interrupts` + `interrupt-parent = <&gpioN>`으로 줄 수도 있다. 확장기 뒤라면 nested threaded IRQ라 스레드 핸들러가 필수고, 공유 INT 라인은 level + ONESHOT이 견고하다.

> Either describe it as an interrupt in the device tree with the GPIO controller as interrupt-parent, or request the line with gpiod and call gpiod_to_irq. Then request a threaded IRQ. I default to a threaded handler with IRQF_ONESHOT because GPIO expanders deliver nested threaded interrupts, and for a sensor that holds INT until its status register is read, level-triggered is more robust than edge.

**꼬리 질문**: "What if edges come faster than you handle them?" → 래치가 합쳐지고 FIFO가 넘친다. 측정에서 line_seqno로 구분했다. 고속 펄스는 하드웨어 카운터로.

**Q.** "Two drivers fail with -EBUSY on the same GPIO. How do you debug?"

**A.** `/sys/kernel/debug/gpio`에서 consumer 이름을 보고, 같은 라인을 참조하는 DT 노드를 찾는다. pin mux 충돌이면 `pinmux-pins`의 owner와 "already requested" 로그를 본다. pinctrl 충돌은 probe 전에 실패해서 드라이버 로그가 없다는 게 단서다.

> I check debugfs gpio for the current consumer of that line, then grep the device tree for other nodes referencing it — often a gpio-hog, gpio-keys or a copied node. If it's a pin mux conflict the pinctrl core logs "already requested", pinmux-pins shows the owner, and the second device never even reaches probe, so the absence of probe logs is itself a hint.

**Q.** "How would you test GPIO handling without hardware?"

**A.** gpio-sim으로 가짜 칩을 configfs나 DT로 만들고, sim 쪽 `pull` 속성으로 외부 신호를 흉내 낸다. IRQ도 나므로 커널 드라이버와 uAPI 프로그램을 CI에서 돌릴 수 있다. 실습에서 QEMU 위에 gpio-sim을 DT로 만들어 드라이버의 reset/irq 핀을 테스트했다.

> The kernel has gpio-sim: you create a simulated chip through configfs or the device tree, and writing to its pull attribute plays the role of the external circuit, including generating interrupts. That lets you exercise both a kernel consumer and user-space code in QEMU or CI. I used it to test reset polarity, interrupt counting and event-drop behaviour without a board.

---

## 14. 직접 해보기

1. (개념) i.MX8MP `MX8MP_IOMUXC_I2C3_SDA__GPIO5_IO19`에 패드값 `0x1c3`을 주면 무엇이 설정되나? 정답/힌트: 먹스 0x214에 ALT5, 패드 0x474에 PE+HYS+PU+DSE 비트 `0x3`(헤더의 DSE 매크로와 비교), SION 없음.
2. (코드) `h4_sensor`에 `dev_pm_ops`를 추가해 suspend에서 `pinctrl_pm_select_sleep_state`, resume에서 default로 가게 하고, 게스트에서 `echo mem > /sys/power/state`가 virt에서 되는지 확인하라. 정답/힌트: virt에 suspend 지원이 없으면 대신 `echo freeze`; 안 되면 sysfs `pins`와 결과를 비교.
3. (코드) `h4_line`을 확장해 두 라인(0,1)을 한 요청으로 받아 `GPIO_V2_LINE_SET_VALUES_IOCTL`의 `mask`/`bits`로 동시에 바꾸고, sim `value`로 확인하라. 정답/힌트: `num_lines = 2`, `bits`의 비트 i = `offsets[i]`.
4. (관찰) `h4_rate`를 `event_buffer_size=0`으로 돌려 실제 FIFO 크기가 16이 되는지 유실 수로 추정하라. 정답/힌트: 커널은 0을 `num_lines * 16`으로 바꾼다.
5. (관찰) `h4_mon`을 `GPIO_V2_LINE_FLAG_EVENT_CLOCK_REALTIME` 플래그로 바꿔 timestamp가 epoch 기준이 되는지 보라. 정답/힌트: `CLOCK_REALTIME` 기준이라 부팅 후 경과 초가 아니라 epoch 기준의 큰 초 값이 나온다(게스트 시계 설정에 따라 값은 다르다).
6. (코드) DT의 `irq-gpios`를 `GPIO_ACTIVE_LOW`로 바꾸고 `IRQF_TRIGGER_RISING`이 물리적으로 어느 엣지가 되는지 확인하라. 정답/힌트: IRQ 트리거는 극성 플래그와 무관하게 **물리** 엣지다 — 논리/물리 구분은 값 API에만 적용된다.
7. (관찰) `h4-thief-gpio` 노드를 `GPIOD_FLAGS_BIT_NONEXCLUSIVE`로 요청하도록 드라이버를 고치고 `-EBUSY`가 사라지는지, `debug/gpio`에 무엇이 보이는지 확인하라.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| pad / ball | 칩의 물리 단자 | 먹스·패드 설정의 대상 |
| pin mux | 핀 다중화 | 한 패드에 연결된 여러 기능 중 하나 선택 |
| pinconf | 핀 설정 | 풀업/다운, 구동 세기, 슬루, 오픈 드레인, 슈미트 |
| pin controller | 핀 컨트롤러 드라이버 | `pinctrl_desc`로 핀·그룹·함수 등록 (pinctrl-imx 등) |
| group / function | 핀 묶음 / 기능 | "이 묶음을 이 기능으로" |
| pinctrl state | 핀 상태 이름 | `default`, `init`, `sleep`, `idle`, 사용자 정의 |
| `pinctrl_bind_pins` | 드라이버 코어 함수 | probe 직전 default(또는 init) 적용 |
| SION | Software Input On | 출력 먹스 중에도 입력 경로 유지 (i.MX) |
| daisy chain / SELECT_INPUT | 입력 선택 | 여러 패드 중 어떤 것을 IP 입력으로 쓸지 |
| `gpio_chip` | GPIO provider 구조체 | 방향·값·설정·IRQ 콜백 |
| `gpio_desc` | GPIO 디스크립터 | consumer가 쥐는 라인 핸들 |
| con_id | consumer 이름 | `reset` → `reset-gpios` 속성 |
| active-low | 저전위 활성 | 논리 1 = 물리 0 |
| raw value | 물리값 | 극성 무시한 핀 레벨 |
| `can_sleep` | 잠드는 칩 | I2C/SPI 확장기 → `_cansleep` 필요 |
| GPIO hog | 고정 GPIO | DT로 부팅 때 잡아 두는 라인 |
| gpio-ranges | GPIO↔pinctrl 대응 | GPIO 요청 시 패드를 GPIO로 먹스 |
| gpio-sim | 시뮬레이션 GPIO 칩 | configfs/DT로 생성, pull로 입력 흉내 |
| uAPI v2 | GPIO 문자 장치 ABI | `/dev/gpiochipN` + ioctl, 5.10~ |
| line request fd | 라인 요청 fd | 소유권, 닫히면 해제, 이벤트 read |
| `line_seqno` | 라인별 이벤트 번호 | 건너뛰면 FIFO 유실 |
| debounce | 채터링 제거 | HW 미지원 시 cdev가 SW로 대체 |
| nested threaded IRQ | 중첩 스레드 IRQ | 확장기 IRQ, 스레드 핸들러 필수 |

---

## 16. 요약 & 체크리스트

SoC 패드 하나는 여러 기능을 가지며, Linux는 "패드가 무엇이고 전기적으로 어떤가"를 pinctrl(DT의 `pinctrl-names`/`pinctrl-N`, state 전환)에, "그 GPIO 라인으로 무엇을 하나"를 gpiolib(`gpio_chip` provider, `gpio_desc` consumer)에 맡긴다. 드라이버는 `devm_gpiod_get` + 논리값 + `_cansleep` + `gpiod_to_irq`/threaded IRQ로 보드와 무관하게 쓰이고, 극성·핀 번호·먹스는 DT가 들고 있다. 유저 공간은 폐기된 sysfs 대신 `/dev/gpiochipN` uAPI v2를 쓰며, 라인 요청 fd가 소유권이고 이벤트에는 timestamp와 seqno가 붙어 유실을 감지할 수 있다. 실습에서 probe 직전 default state 자동 적용(0x172), sleep 전환(0x080), active-low의 logical/raw, can_sleep WARN, 같은 핀 두 번 요청의 `-EBUSY`·"already requested", 그리고 엣지 유실이 IRQ 래치와 FIFO 두 곳에서 생긴다는 것을 직접 확인했다.

- [ ] i.MX `fsl,pins` 한 줄(6개 값)을 해독할 수 있다
- [ ] pin/group/function/state/map의 관계를 설명할 수 있다
- [ ] `pinctrl_bind_pins`가 언제 무엇을 하는지, `init` state의 의미를 설명할 수 있다
- [ ] suspend/resume에서 sleep/default state를 고르는 코드를 짤 수 있다
- [ ] `/sys/kernel/debug/pinctrl`과 `/sys/kernel/debug/gpio`로 소유자와 상태를 읽을 수 있다
- [ ] `gpio_chip`의 주요 콜백과 `can_sleep`의 의미를 설명할 수 있다
- [ ] `devm_gpiod_get`/`_optional`/`_index`와 flags를 골라 쓸 수 있다
- [ ] ACTIVE_LOW의 논리값/물리값을 표로 설명하고 이중 반전 버그를 짚을 수 있다
- [ ] `gpiod_set_value`와 `_cansleep`의 차이를 확장기 예로 설명할 수 있다
- [ ] `gpiod_to_irq` + threaded IRQ로 INT 핀을 받는 코드를 짤 수 있다
- [ ] sysfs GPIO가 폐기된 이유를 4가지 이상 말할 수 있다
- [ ] uAPI v2로 라인 요청·엣지 이벤트·라인 감시를 C로 짜고, `line_seqno`로 유실을 구분할 수 있다

---

## 참고 자료

- docs.kernel.org: [PINCTRL (PIN CONTROL) subsystem](https://docs.kernel.org/driver-api/pin-control.html)
- docs.kernel.org: [GPIO Descriptor Consumer Interface](https://docs.kernel.org/driver-api/gpio/consumer.html), [GPIO Driver Interface](https://docs.kernel.org/driver-api/gpio/driver.html), [Legacy GPIO interfaces](https://docs.kernel.org/driver-api/gpio/legacy.html)
- docs.kernel.org: [GPIO Character Device Userspace API](https://docs.kernel.org/userspace-api/gpio/chardev.html), [GPIO Sysfs Interface for Userspace (obsolete)](https://docs.kernel.org/admin-guide/gpio/sysfs.html), [gpio-sim](https://docs.kernel.org/admin-guide/gpio/gpio-sim.html)
- 커널 소스 (랩 트리 6.18.55, elixir.bootlin.com에서도 같은 경로): `drivers/base/pinctrl.c`, `drivers/pinctrl/core.c`, `drivers/pinctrl/pinctrl-single.c`, `drivers/pinctrl/freescale/pinctrl-imx.c`, `drivers/gpio/gpiolib.c`, `drivers/gpio/gpiolib-cdev.c`, `drivers/gpio/gpio-sim.c`, `kernel/irq/irq_sim.c`, `include/uapi/linux/gpio.h`, `include/linux/gpio/driver.h`, `include/linux/gpio/consumer.h`
- DT: `arch/arm64/boot/dts/freescale/imx8mp-pinfunc.h`, `imx8mp-evk.dts`, `imx8mp-aristainetos3a-som-v1.dtsi`, `Documentation/devicetree/bindings/pinctrl/pinctrl-single.yaml`, `fsl,imx8m-pinctrl.yaml`
- libgpiod: [git.kernel.org libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/)
- Bootlin "Linux kernel and driver development" 교육 슬라이드의 pinctrl/GPIO 장 (bootlin.com/training/kernel)
- 이 노트 시리즈: D4(자원 기술), A4(POLLPRI), A10(libgpiod CLI), F6(threaded IRQ), H5(I2C 버스 복구), H6(irq_domain, nested IRQ)
