# I9. LED · PWM · RTC · watchdog — 작은 프레임워크들과 제품 안정성

> **이 노트를 다 읽으면**: LED class·PWM·RTC·watchdog 네 프레임워크가 각각 어떤 ops 구조체를 받고 유저에게 무엇을 보여 주는지 설명할 수 있다 · DT의 `gpio-leds`/`pwms`/`default-state`/`linux,default-trigger`를 읽고 쓰며, 트리거·PWM 파형을 실제로 샘플링해 검증할 수 있다 · RTC ioctl로 시간을 읽고 알람을 걸어 s2idle에서 깨어나게 할 수 있다 · `/dev/watchdog`의 open/keepalive/magic close/nowayout/pretimeout 규칙을 알고 실제 리셋까지 재현하며, 로봇 제품의 watchdog 전략을 계층으로 설계해 말할 수 있다
> **왜 필요한가**: "상태 LED가 부팅 중 이상하게 깜빡인다", "팬 PWM duty를 바꿨는데 EINVAL", "현장 장비 시계가 1970년이다", "현장에서 장비가 가끔 멈춘다 — 누가 리셋해 주나?" — bring-up과 양산 직전에 반드시 만나는 장면이다. 면접에서는 "watchdog을 어떻게 설계하겠나", "magic close가 뭔가", "RTC와 시스템 시계의 관계"로 나온다
> **Don 기준 난이도**: GPIO LED, PWM 타이머 레지스터(period/compare), RTC 칩, HW watchdog kick은 펌웨어에서 매일 하던 일 / 새로 배울 것은 "그 하드웨어를 각 서브시스템의 ops 구조체로 등록하면 커널이 sysfs·ioctl·트리거·알람 큐·keepalive 워커까지 대신 제공하는 구조"와 "유저 공간 데몬·systemd·커널 core가 watchdog을 나눠 맡는 방식"
> **선행 노트**: G2–G3(platform 드라이버·probe), D4(DT 문법), H4(gpio-sim, `_cansleep`, PL061), F5·F7(타이머·workqueue), A4(read/poll 블로킹). 관련: I1(프레임워크 원칙), I4(hwmon·thermal — pwm-fan), N4(전원 관리·suspend), Q3(시간 동기화 NTP/PTP)

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 펌웨어에서는 이렇게 했다

SSD 컨트롤러 펌웨어에서 이 네 가지는 보통 이렇게 생겼다.

```c
/* bare-metal 시절: 전부 레지스터 직접 */
GPIO->DR ^= LED_ACT;                   /* I/O 처리마다 activity LED 토글 */
TIM3->ARR = 999; TIM3->CCR2 = 250;     /* 팬 PWM: period 1000틱, duty 25% */
rtc_i2c_read(0x68, 0x00, buf, 7);      /* 외부 RTC 칩에서 BCD 시간 읽기 */
WDT->KICK = 0xA5;                      /* 메인 루프 끝에서 watchdog kick */
```

코드는 몇 줄이지만 제품 수준이 되면 질문이 쏟아진다. LED를 누가 언제 깜빡이나(부팅 중? 에러 코드?), PWM duty를 바꾸는 동안 파형이 튀지 않나, RTC 시간과 펌웨어가 쓰는 시간이 어긋나면 누가 맞추나, **kick을 메인 루프 어디에 두어야 "진짜로 살아 있을 때만" kick하게 되나**.

Linux는 이 네 장치마다 **작은 서브시스템(framework)** 을 둔다. 드라이버는 하드웨어를 만지는 콜백 몇 개만 채운 ops 구조체를 등록하고, 나머지(유저 인터페이스, 트리거, 알람 큐, keepalive 워커, 권한, 동시성)는 core가 맡는다. I1에서 말한 "char device를 직접 만들지 말고 서브시스템에 등록하라"의 가장 작은 예제들이다.

### 0.2 네 프레임워크 한눈에

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <text x="85" y="22" text-anchor="middle" font-size="13">상태 표시</text>
  <text x="255" y="22" text-anchor="middle" font-size="13">밝기 · 팬 · 부저</text>
  <text x="425" y="22" text-anchor="middle" font-size="13">꺼져도 흐르는 시간</text>
  <text x="595" y="22" text-anchor="middle" font-size="13">멈춤 감지 → 리셋</text>
  <text x="4" y="58" font-size="12">유저</text>
  <rect x="10" y="64" width="150" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="86" text-anchor="middle" font-size="13">/sys/class/leds</text>
  <rect x="180" y="64" width="150" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="255" y="86" text-anchor="middle" font-size="13">/sys/class/pwm</text>
  <rect x="350" y="64" width="150" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="425" y="86" text-anchor="middle" font-size="13">/dev/rtc0 + sysfs</text>
  <rect x="520" y="64" width="150" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="595" y="86" text-anchor="middle" font-size="13">/dev/watchdog0</text>
  <text x="4" y="128" font-size="12">core</text>
  <rect x="10" y="134" width="150" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="85" y="156" text-anchor="middle" font-size="13">led-class + trigger</text>
  <rect x="180" y="134" width="150" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="255" y="156" text-anchor="middle" font-size="13">pwm core (state)</text>
  <rect x="350" y="134" width="150" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="425" y="156" text-anchor="middle" font-size="13">rtc core + 알람 큐</text>
  <rect x="520" y="134" width="150" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="595" y="156" text-anchor="middle" font-size="13">watchdog core</text>
  <text x="4" y="198" font-size="12">ops</text>
  <rect x="10" y="204" width="150" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="85" y="226" text-anchor="middle" font-size="13">led_classdev</text>
  <rect x="180" y="204" width="150" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="255" y="226" text-anchor="middle" font-size="13">pwm_chip + pwm_ops</text>
  <rect x="350" y="204" width="150" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="425" y="226" text-anchor="middle" font-size="13">rtc_class_ops</text>
  <rect x="520" y="204" width="150" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="595" y="226" text-anchor="middle" font-size="13">watchdog_ops</text>
  <text x="4" y="268" font-size="12">HW</text>
  <rect x="10" y="274" width="150" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="296" text-anchor="middle" font-size="13">GPIO · I2C LED IC</text>
  <rect x="180" y="274" width="150" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="255" y="296" text-anchor="middle" font-size="13">PWM 타이머 블록</text>
  <rect x="350" y="274" width="150" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="425" y="296" text-anchor="middle" font-size="13">PL031 · I2C RTC</text>
  <rect x="520" y="274" width="150" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="595" y="296" text-anchor="middle" font-size="13">SoC WDT · PMIC</text>
  <g stroke="currentColor" stroke-opacity="0.5">
    <line x1="85" y1="98" x2="85" y2="134"/><line x1="255" y1="98" x2="255" y2="134"/><line x1="425" y1="98" x2="425" y2="134"/><line x1="595" y1="98" x2="595" y2="134"/>
    <line x1="85" y1="168" x2="85" y2="204"/><line x1="255" y1="168" x2="255" y2="204"/><line x1="425" y1="168" x2="425" y2="204"/><line x1="595" y1="168" x2="595" y2="204"/>
    <line x1="85" y1="238" x2="85" y2="274"/><line x1="255" y1="238" x2="255" y2="274"/><line x1="425" y1="238" x2="425" y2="274"/><line x1="595" y1="238" x2="595" y2="274"/>
  </g>
  <text x="340" y="324" text-anchor="middle" font-size="12">네 프레임워크 모두 같은 모양: 드라이버는 ops만, 유저 인터페이스와 정책은 core가</text>
</svg>
```

그림 1 — 이 노트가 다루는 네 프레임워크의 단면. 위에서부터 유저가 보는 인터페이스, core가 하는 일, 드라이버가 채우는 ops 구조체, 실제 하드웨어다. PWM에는 유저 말고도 커널 안 소비자(pwm-fan, pwm_bl 백라이트, pwm-beeper)가 core 위에 붙는다.

| 프레임워크 | 드라이버가 등록하는 것 | 핵심 콜백 | 유저 인터페이스 | 커널 안 소비자 |
|---|---|---|---|---|
| LED class | `struct led_classdev` | `brightness_set` / `brightness_set_blocking`, `blink_set`, `pattern_set` | `/sys/class/leds/<color>:<function>/` | 트리거(timer, heartbeat, disk, netdev, panic …) |
| PWM | `struct pwm_chip` + `struct pwm_ops` | `.apply`, `.get_state` (6.13+ waveform 콜백 선택) | `/sys/class/pwm/pwmchipN/` (+ waveform 드라이버면 `/dev/pwmchipN`) | pwm-fan, pwm_bl, pwm-beeper, leds-pwm, 모터 드라이버 |
| RTC | `struct rtc_device` + `struct rtc_class_ops` | `.read_time`, `.set_time`, `.read_alarm`, `.set_alarm`, `.alarm_irq_enable` | `/dev/rtcN`, `/sys/class/rtc/rtcN/`, `/proc/driver/rtc` | hctosys, NTP systohc, alarmtimer (`CLOCK_*_ALARM`) |
| watchdog | `struct watchdog_device` + `struct watchdog_ops` | `.start`, `.stop`, `.ping`, `.set_timeout`, `.get_timeleft`, `.set_pretimeout` | `/dev/watchdogN`, `/dev/watchdog` | 부팅 중 core keepalive, reboot notifier, pretimeout governor |

### 0.3 센서 키트 장비에서는

로봇용 센서 키트(i.MX8/AM62/Jetson급 SoC + IMU·카메라·LiDAR·CAN 모터)를 예로 들면:

- **LED**: 전원(초록 상시), 상태(부팅 중 heartbeat → 정상 상시 → 에러 코드 패턴), 네트워크 링크(netdev 트리거), 저장장치 활동(disk 트리거). 현장 기술자는 이것만 보고 "어디가 죽었는지" 판단한다.
- **PWM**: 냉각 팬(pwm-fan + thermal), 디스플레이 백라이트(pwm_bl), 부저(pwm-beeper), 조명·IR 투광기, 서보(50 Hz). 실시간 모터 전류 제어는 보통 MCU 몫이다.
- **RTC**: 로그·사진 타임스탬프의 기준. GNSS/NTP가 잡히기 전 부팅 직후 시간. 정해진 시각에 장비를 깨우는 알람(저전력 감시 모드).
- **watchdog**: 커널 hang, 제어 데몬 데드락, 유저 공간 전체 멈춤을 잡아 **사람 없이** 장비를 되살리는 마지막 안전망.

---

## 1. 실습 판 깔기 — Image-sub 커널 + gpio-sim + PL061 + PL031 + i6300esb

모든 실습은 공용 랩(`/var/lab`)의 **서브시스템 커널** `out/Image-sub`(Linux `6.18.55-sub`, 가상·테스트 드라이버를 모듈로 추가한 빌드)를 QEMU `virt`로 부팅해서 게스트 안에서 돌린다. 빌드 호스트는 Lima VM `elinux`(`uname -r` = `7.0.0-38-generic`)다.

### 1.1 virt 머신에서 무엇으로 흉내 내나

| 필요한 것 | 이 노트의 해결 | 비고 |
|---|---|---|
| LED가 달린 핀 | **gpio-sim** 라인 3개 + `leds-gpio` | sim 쪽 `value` 파일로 핀 상태를 읽을 수 있다 → 파형 샘플링 |
| PWM 출력 | **pwm-gpio**(GPIO를 hrtimer로 토글하는 소프트웨어 PWM) on **PL061** 4번 핀 | gpio-sim 위에서는 거부된다(§3.5) |
| PWM 하드웨어 드라이버 | 직접 만든 가짜 `pwm_chip` (`i9_pwm.ko`) | apply 호출을 로그로 본다 |
| RTC | virt의 **PL031**(`rtc0`) + **rtc-test**(가짜 RTC 3개) | PL031은 알람 IRQ가 있어 s2idle에서 깨울 수 있다 |
| 하드웨어 watchdog | QEMU **`-device i6300esb`**(Intel 6300ESB PCI watchdog 에뮬레이션) | 타임아웃이 나면 QEMU가 **실제로 머신을 리셋**한다 |
| 소프트웨어 watchdog | **softdog**(hrtimer 기반) + 직접 만든 `i9_wdt.ko` | `i9_wdt`는 `get_timeleft`·pretimeout·"부팅 때 이미 켜짐"을 흉내 |

랩 커널 설정에서 관련 옵션은 이렇다 (게스트 안 `/proc/config.gz`, 필요한 줄만).

```sh
zcat /proc/config.gz | grep -E "^CONFIG_(LEDS_TRIGGER_(TIMER|HEARTBEAT|PATTERN|NETDEV)|RTC_HCTOSYS|RTC_DRV_PL031|WATCHDOG_(CORE|NOWAYOUT|PRETIMEOUT_GOV|HANDLE_BOOT_ENABLED|OPEN_TIMEOUT)|SOFT_WATCHDOG|I6300ESB_WDT)[= ]"
```

```text
6.18.55-sub
CONFIG_WATCHDOG_CORE=y
CONFIG_WATCHDOG_HANDLE_BOOT_ENABLED=y
CONFIG_WATCHDOG_OPEN_TIMEOUT=0
CONFIG_SOFT_WATCHDOG=m
CONFIG_I6300ESB_WDT=m
CONFIG_LEDS_TRIGGER_TIMER=y
CONFIG_LEDS_TRIGGER_HEARTBEAT=m
CONFIG_RTC_HCTOSYS=y
CONFIG_RTC_DRV_PL031=y
```

출력에서 볼 것: `LEDS_TRIGGER_PATTERN`, `LEDS_TRIGGER_NETDEV`, `WATCHDOG_PRETIMEOUT_GOV`, `WATCHDOG_NOWAYOUT`이 없다(꺼져 있다). 그래서 pattern 트리거는 같은 커널 소스의 `drivers/leds/trigger/ledtrig-pattern.c`를 **out-of-tree 모듈로 따로 빌드**해서 넣었고, pretimeout은 governor 없이 동작하는 경로(§5.7)로 확인한다. 이 커널 설정 하나로 "제품 커널에서 무엇을 켜야 하나"라는 실무 질문이 생긴다 — §7.4에서 정리한다.

### 1.2 예제 1 — DT 조각: sim LED 3개, pwm-gpio 2개, 가짜 PWM 칩과 소비자

무엇을 확인하나: 이 노트의 LED·PWM 실험을 한 DT에 담는다. `virt.dts` 사본 끝에 `#include`해서 C 전처리 → dtc로 dtb를 만든다(D4와 같은 방식). `leds/common.h`의 `LED_FUNCTION_*`·`LED_COLOR_ID_*` 매크로를 쓴다.

```dts
/* i9_nodes.dtsi — gpio-sim 칩 + leds-gpio + pwm-gpio + 가짜 PWM/LED 노드 */
#include <dt-bindings/gpio/gpio.h>
#include <dt-bindings/leds/common.h>
#include <dt-bindings/pwm/pwm.h>

/ {
	i9-gpio-sim {
		compatible = "gpio-simulator";
		i9_sim: bank0 {
			gpio-controller;
			#gpio-cells = <2>;
			ngpios = <8>;
			gpio-sim,label = "i9_sim";
			gpio-line-names = "LED_STATUS", "LED_HB", "LED_FAULT_N",
					  "PWM_OUT", "", "", "", "";
		};
	};

	i9-leds {
		compatible = "gpio-leds";
		led-status {
			function = LED_FUNCTION_STATUS;
			color = <LED_COLOR_ID_GREEN>;
			gpios = <&i9_sim 0 GPIO_ACTIVE_HIGH>;
			default-state = "on";
		};
		led-hb {
			function = LED_FUNCTION_HEARTBEAT;
			color = <LED_COLOR_ID_BLUE>;
			gpios = <&i9_sim 1 GPIO_ACTIVE_HIGH>;
			linux,default-trigger = "heartbeat";
		};
		led-fault {
			function = LED_FUNCTION_FAULT;
			color = <LED_COLOR_ID_RED>;
			gpios = <&i9_sim 2 GPIO_ACTIVE_LOW>;
			default-state = "off";
			panic-indicator;
		};
	};
```

```dts
	i9-pwm-gpio-sim {			/* 잠드는 GPIO(gpio-sim) 위 → 거부됨 */
		compatible = "pwm-gpio";
		#pwm-cells = <3>;
		gpios = <&i9_sim 3 GPIO_ACTIVE_HIGH>;
	};

	i9_swpwm: i9-pwm-gpio-pl061 {		/* MMIO GPIO(PL061 4번) 위 → 동작 */
		compatible = "pwm-gpio";
		#pwm-cells = <3>;
		gpios = <&gpio 4 GPIO_ACTIVE_HIGH>;
	};

	i9_fpwm: i9-fake-pwm {
		compatible = "dons,i9-pwm";
		#pwm-cells = <3>;
	};

	i9-motor {
		compatible = "dons,i9-pwm-consumer";
		pwms = <&i9_fpwm 1 50000 PWM_POLARITY_INVERTED>;
		pwm-names = "motor";
	};

	i9-fan {
		compatible = "pwm-fan";
		pwms = <&i9_fpwm 0 40000 0>;
		cooling-levels = <0 64 128 255>;
		#cooling-cells = <2>;
	};

	i9-led {
		compatible = "dons,i9-led";
		led {
			function = LED_FUNCTION_INDICATOR;
			color = <LED_COLOR_ID_AMBER>;
		};
	};
};
```

요점:

- `function` + `color`가 있으면 LED core가 이름을 `<color>:<function>`으로 **자동 생성**한다(`led_compose_name()`). 옛 DT의 `label = "board:green:status"`는 deprecated다. 같은 function이 여러 개면 `function-enumerator`로 `green:status-1`처럼 번호를 붙인다.
- `default-state`는 `"on"`, `"off"`, `"keep"`(부트로더가 켜 둔 상태 유지) 셋 중 하나다. 부트로더가 이미 전원 LED를 켰다면 `"keep"`이어야 커널 부팅 중 꺼졌다 켜지는 깜빡임이 없다.
- `panic-indicator`는 커널 panic 때 이 LED를 깜빡이게 한다(panic 트리거로 자동 전환).
- `pwms = <&phandle 채널 period_ns 플래그>` — `#pwm-cells = <3>`이면 세 칸이다. 플래그 `PWM_POLARITY_INVERTED`(값 1)는 극성 반전. 소비자 드라이버는 이 period·polarity를 `pwm_init_state()`로 받아 쓴다(§3.6).
- `pl061@9030000` 노드에는 레이블이 없어서, 빌드 스크립트가 `sed`로 `gpio:` 레이블을 붙인다(H4와 같은 수법).

```sh
#!/bin/sh
# mkdtb.sh → /var/tmp/I9/i9.dtb  (virt.dts 사본 + i9_nodes.dtsi)
set -e
W=/var/tmp/I9; K=/var/lab/src/linux-6.18.55/include
sed -e 's/^\tpl061@9030000 {/\tgpio: pl061@9030000 {/' /var/lab/out/virt.dts > $W/i9_virt.dts
echo '#include "/Users/donh/.elinux_scratch/I9/i9_nodes.dtsi"' >> $W/i9_virt.dts
cpp -nostdinc -I $K -undef -x assembler-with-cpp $W/i9_virt.dts > $W/i9.pp.dts
dtc -q -I dts -O dtb -o $W/i9.dtb $W/i9.pp.dts
```

### 1.3 부팅 스크립트

랩 rootfs를 복사해 Image-sub의 모듈 트리(`/lib/modules/6.18.55-sub`, BusyBox `modprobe`가 의존성을 푼다), 이 노트의 `.ko`, 정적 링크한 도구, 시나리오 스크립트 `i9.sh`를 넣고 `LAB_CMD`로 시나리오 하나를 실행한 뒤 전원을 끈다. `virt.dts`는 CPU 노드가 하나라 `-smp 1`이다.

```sh
#!/bin/sh
# boot.sh <시나리오> [QEMU 추가 인자...] — Image-sub + i9.dtb + initramfs(모듈·도구·i9.sh)
set -e
W=/var/tmp/I9; S=/Users/donh/.elinux_scratch/I9; SC=$1; shift
if [ ! -f $W/i.gz ] || [ $S/i9.sh -nt $W/i.gz ] || [ -n "$REPACK" ]; then
  rm -rf $W/r && mkdir $W/r && cp -a /var/lab/build/rootfs/. $W/r/
  mkdir -p $W/r/lib/modules && cp -a /var/lab/out/modules-sub/lib/modules/6.18.55-sub $W/r/lib/modules/
  cp $W/mod/*.ko $W/bin/* $S/i9.sh $W/r/
  (cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
fi
timeout ${TMO:-300} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 512M -nographic \
  -kernel /var/lab/out/Image-sub -initrd $W/i.gz -dtb $W/i9.dtb "$@" \
  -append "console=ttyAMA0 quiet $KARGS LAB_CMD=sh~/i9.sh~$SC"
```

watchdog 실험에서는 `"$@"` 자리에 `-device i6300esb`를 넘긴다. 커널 모듈 4개(`i9_led`, `i9_pwm`, `i9_wdt`, 그리고 랩 소스에서 복사한 `ledtrig-pattern`)는 `make -C /var/lab/out/linux-build-sub M=$PWD modules`로 경고 0개 빌드됐고, 유저 도구 3개(`i9_sample`, `i9_rtc`, `i9_wdog`)는 VM에서 `gcc -std=gnu11 -Wall -Wextra -O2 -static`으로 경고 0개 빌드됐다.

### 1.4 예제 2 — 네 클래스가 생기는 순간

무엇을 확인하나: 모듈을 올리기 전과 후에 `/sys/class/{leds,pwm,rtc,watchdog}`과 `/dev` 노드가 어떻게 바뀌는가.

```sh
for c in leds pwm rtc watchdog; do echo "$c: $(ls /sys/class/$c)"; done
modprobe leds-gpio; modprobe pwm-gpio; modprobe i6300esb; echo "== after modprobe"
for c in leds pwm rtc watchdog; do echo "$c: $(ls /sys/class/$c)"; done
ls -l /dev/watchdog* /dev/rtc* /dev/pwmchip* | awk '{print $1, $5, $6, $NF}'
```

```text
leds: 
pwm: 
rtc: rtc0
watchdog: 
[    4.486903] pwm-gpio i9-pwm-gpio-sim: error -EINVAL: /i9-pwm-gpio-sim: sleeping GPIO not supported
[    4.487090] pwm-gpio i9-pwm-gpio-sim: probe with driver pwm-gpio failed with error -22
== after modprobe
leds: blue:heartbeat
green:status
red:fault
pwm: pwmchip0
rtc: rtc0
watchdog: watchdog0
ls: /dev/pwmchip*: No such file or directory
crw------- 250, 0 /dev/rtc0
crw------- 10, 130 /dev/watchdog
crw------- 245, 0 /dev/watchdog0
```

출력에서 볼 것:

- `rtc0`은 처음부터 있다. PL031 드라이버가 built-in(`=y`)이라 부팅 중에 probe됐다.
- LED 이름이 DT의 `color`·`function`에서 자동으로 `blue:heartbeat`, `green:status`, `red:fault`가 됐다.
- pwm-gpio 노드 두 개 중 gpio-sim 위의 것은 **probe 실패**, PL061 위의 것만 `pwmchip0`이 됐다. 이유는 §3.5.
- watchdog은 노드가 **두 개**다. `/dev/watchdog`(misc, major 10 minor 130)은 옛 단일 장치 API 호환용으로 첫 번째 watchdog에만 붙고, `/dev/watchdog0`(동적 major)은 장치별 노드다. 새 코드는 `/dev/watchdogN`을 쓴다.
- `/dev/pwmchip*`는 없다. 6.18에는 PWM 문자 장치(`PWM_IOCTL_*`)가 있지만, `pwmchip_add()`가 **waveform 콜백(`write_waveform`)을 구현한 드라이버에만** devt를 준다(랩 소스 `drivers/pwm/core.c`에서 확인). pwm-gpio는 아직 `.apply` 방식이다.

---

## 2. LED class — "밝기 하나"를 표준화하고 깜빡임은 트리거에 맡긴다

### 2.1 직관: LED 드라이버는 "밝기 쓰기" 함수 하나다

MCU에서 LED를 깜빡이려면 타이머 ISR이나 RTOS 태스크를 하나 만들어야 했다. Linux LED class는 이것을 둘로 쪼갠다.

1. **LED 드라이버(provider)**: "이 LED의 밝기를 n으로 만들어라"만 할 줄 안다. GPIO 하나든, I2C LED 드라이버 IC의 채널이든, PWM이든.
2. **트리거(trigger)**: "언제 켜고 끌지"를 정하는 정책. timer(주기 깜빡임), heartbeat(심장 박동), disk-activity, netdev(링크/트래픽), panic, default-on, pattern(임의 패턴).

둘이 분리돼 있으니 GPIO LED든 I2C LED든 같은 `netdev` 트리거를 붙일 수 있고, 트리거 코드는 LED 하드웨어를 전혀 모른다. bare-metal 비유로는 "LED HAL 함수 하나 + 이벤트 소스별 정책 모듈"이다.

### 2.2 정의: `struct led_classdev`

드라이버가 채우는 필드 중 중요한 것만 (`include/linux/leds.h`, 6.18 기준):

```c
struct led_classdev {
	const char		*name;
	unsigned int brightness;
	unsigned int max_brightness;	/* 0이면 core가 LED_FULL(255)로 채움 */
	unsigned long		 flags;		/* LED_CORE_SUSPENDRESUME, LED_PANIC_INDICATOR ... */

	/* 잠들면 안 됨 — MMIO GPIO처럼 바로 쓰는 하드웨어 */
	void		(*brightness_set)(struct led_classdev *, enum led_brightness);
	/* 잠들어도 됨 — I2C/SPI LED IC. core가 workqueue에서 부른다 */
	int		(*brightness_set_blocking)(struct led_classdev *, enum led_brightness);
	enum led_brightness (*brightness_get)(struct led_classdev *);
	/* 하드웨어가 스스로 깜빡일 수 있으면 (CPU 개입 없이) */
	int		(*blink_set)(struct led_classdev *, unsigned long *delay_on,
				     unsigned long *delay_off);
	int (*pattern_set)(struct led_classdev *, struct led_pattern *, u32 len, int repeat);
	const char		*default_trigger;
	/* ... hw_control_* : netdev 트리거 등의 하드웨어 오프로드 (PHY LED) */
};
```

| 콜백 | 어디서 불리나 | 잠들어도 되나 | 대표 하드웨어 |
|---|---|---|---|
| `brightness_set` | 트리거의 타이머·softirq, 호출자 문맥 그대로 | 안 됨 | MMIO GPIO, SoC PWM |
| `brightness_set_blocking` | core의 ordered workqueue `leds`(kworker) | 됨 | I2C/SPI LED 드라이버 IC, gpio-sim·I2C GPIO 확장기 위의 LED |
| `blink_set` | timer 트리거 활성화 때 한 번 | 구현에 따라 | 자체 깜빡임 레지스터가 있는 LED IC |
| `pattern_set` | pattern 트리거 `hw_pattern` | 구현에 따라 | 패턴 엔진이 있는 LED IC (예: 일부 PMIC LED) |

`brightness`는 0..`max_brightness` 정수다. GPIO LED는 `max_brightness = 1`이고, I2C LED IC나 PWM LED는 보통 255다.

### 2.3 예제 3 — sysfs로 보는 LED, 그리고 active-low

무엇을 확인하나: DT의 `default-state`와 `GPIO_ACTIVE_LOW`가 실제 핀(gpio-sim 라인의 `value`)에 어떻게 반영되는가. `SIMD`는 `/sys/devices/platform/i9-gpio-sim/gpiochip*` 디렉터리다.

```sh
modprobe leds-gpio
L=/sys/class/leds
for l in green:status blue:heartbeat red:fault; do
  echo "$l: brightness=$(cat $L/$l/brightness) max=$(cat $L/$l/max_brightness) trigger=[$(grep -o '\[[^]]*\]' $L/$l/trigger)]"
done
echo "sim values: status=$(cat $SIMD/sim_gpio0/value) hb=$(cat $SIMD/sim_gpio1/value) fault_n=$(cat $SIMD/sim_gpio2/value)"
echo 1 > $L/red:fault/brightness; echo "fault_n=$(cat $SIMD/sim_gpio2/value)"
cat $L/green:status/trigger
```

```text
green:status: brightness=1 max=1 trigger=[[none]]
blue:heartbeat: brightness=0 max=1 trigger=[[none]]
red:fault: brightness=0 max=1 trigger=[[none]]
sim values: status=1 hb=0 fault_n=1
== fault on (active-low)
fault_n=0
== triggers available
[none] kbd-scrolllock kbd-numlock kbd-capslock kbd-kanalock kbd-shiftlock kbd-altgrlock kbd-ctrllock kbd-altlock kbd-shiftllock kbd-shiftrlock kbd-ctrlllock kbd-ctrlrlock timer disk-activity disk-read disk-write cpu cpu0 default-on panic
```

출력에서 볼 것:

- `red:fault`는 논리 밝기 0(꺼짐)인데 핀은 1이다. `GPIO_ACTIVE_LOW`라서 gpiolib이 뒤집는다(H4 §5). 켜자(`echo 1`) 핀이 0으로 내려갔다. LED 드라이버도 사용자도 극성을 몰라도 된다.
- `blue:heartbeat`의 트리거가 아직 `none`이다. DT에 `linux,default-trigger = "heartbeat"`라고 썼지만 heartbeat 트리거가 **모듈(`=m`)이라 아직 등록 전**이기 때문이다. 트리거가 나중에 등록되면 core가 `default_trigger` 이름이 같은 LED를 찾아 붙인다(바로 다음 예제에서 확인).
- `trigger` 파일은 "읽으면 목록, `[ ]`가 현재 선택, 쓰면 변경"이다. `kbd-*`는 키보드 LED 트리거, `cpu`/`cpu0`은 CPU 활동, `panic`은 panic 때 깜빡임이다.

### 2.4 예제 4 — 트리거 파형을 직접 재 보기 (heartbeat · timer · pattern)

"정말 그렇게 깜빡이나?"를 눈으로 보려면 오실로스코프가 필요하다. gpio-sim은 출력 라인의 현재 값을 sysfs `value`로 보여 주므로, 그 파일을 쉬지 않고 읽어 **엣지 시각을 기록하는 샘플러**를 만든다. PWM 실험(§3)에서도 쓰려고 PL061 레지스터를 `/dev/mem`으로 직접 읽는 모드도 넣었다.

```c
// i9_sample.c — sysfs 값 파일(gpio-sim value)을 계속 읽어 파형을 기록한다
// 사용: i9_sample <file 또는 pl061:N> <ms> [edges]   → 요약(+엣지 목록)
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s file ms [edges]\n", argv[0]); exit(1); }
	volatile uint32_t *gpio = NULL;
	int bit = 0, fd;
	if (!strncmp(argv[1], "pl061:", 6)) {	/* PL061 DATA 레지스터를 /dev/mem으로 직접 */
		bit = atoi(argv[1] + 6);
		if ((fd = open("/dev/mem", O_RDONLY | O_SYNC)) < 0) { perror("/dev/mem"); exit(1); }
		void *m = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0x9030000);
		if (m == MAP_FAILED) { perror("mmap"); exit(1); }
		gpio = m;
	} else if ((fd = open(argv[1], O_RDONLY)) < 0) { perror("open"); exit(1); }
	double dur = atof(argv[2]), t0 = now_ms(), t, last_t = 0, hi = 0, first_rise = -1, last_rise = -1;
	long n = 0, rises = 0;
	int prev = -1;
	char b[8];
	while ((t = now_ms() - t0) < dur) {
		int v;
		if (gpio)			/* 주소 비트[9:2] = 마스크 → 해당 핀만 읽힘 */
			v = !!gpio[1 << bit];
		else {
			if (pread(fd, b, sizeof(b), 0) <= 0) { perror("pread"); exit(1); }
			v = b[0] - '0';
		}
		if (prev == 1) hi += t - last_t;
		if (v != prev) {
			if (argc > 3) printf("E %.3f %d\n", t, v);
			if (prev == 0 && v == 1) {
				if (first_rise < 0) first_rise = t;
				last_rise = t; rises++;
			}
		}
		prev = v; last_t = t; n++;
	}
	printf("samples=%ld (%.1f us/sample) high=%.1f%% rises=%ld",
	       n, dur * 1e3 / n, 100.0 * hi / dur, rises);
	if (rises > 1)
		printf(" period=%.3f ms", (last_rise - first_rise) / (rises - 1));
	printf("\n");
	return 0;
}
```

PL061의 DATA 레지스터는 **주소 비트 [9:2]가 마스크**다(PrimeCell GPIO 설계). 4번 핀만 보려면 오프셋 `(1 << 4) << 2`를 읽는다 — `uint32_t` 배열 인덱스로 `gpio[1 << bit]`. Don이 STM32의 BSRR이나 bit-band로 하던 "주소에 마스크를 싣는" 트릭과 같은 발상이다.

시나리오(시간 순서대로, 엣지 목록은 처음 몇 줄만):

```sh
modprobe ledtrig-heartbeat; grep -o '\[[^]]*\]' $L/blue:heartbeat/trigger
/i9_sample $SIMD/sim_gpio1/value 3000 e
echo timer > $L/green:status/trigger; echo $(ls $L/green:status)
echo 100 > $L/green:status/delay_on; echo 300 > $L/green:status/delay_off
/i9_sample $SIMD/sim_gpio0/value 2000 e
insmod /ledtrig-pattern.ko
echo pattern > $L/green:status/trigger; echo $(ls $L/green:status)
echo "1 100 1 0 0 100 0 0 1 100 1 0 0 700 0 0" > $L/green:status/pattern; cat $L/green:status/pattern
/i9_sample $SIMD/sim_gpio0/value 2500 e
```

```text
== load heartbeat
[heartbeat]
samples=95233 (31.5 us/sample) high=14.7% rises=5 period=622.780 ms
E 0.077 1
E 56.004 0
E 288.035 1
E 364.439 0
E 1223.358 1
E 1299.942 0
E 1531.564 1
E 1606.825 0
E 2471.822 1
== timer 100/300
brightness delay_off delay_on device max_brightness power subsystem trigger uevent
samples=62084 (32.2 us/sample) high=27.5% rises=4 period=426.213 ms
E 0.008 1
E 104.032 0
E 409.178 1
E 512.687 0
E 817.929 1
E 951.973 0
E 1271.485 1
== pattern (double blink)
brightness device hr_pattern max_brightness pattern power repeat subsystem trigger uevent
1 100 1 0 0 100 0 0 1 100 1 0 0 700 0 0
samples=88383 (28.3 us/sample) high=24.6% rises=5 period=516.230 ms
E 0.004 1
E 95.109 0
E 198.636 1
E 302.727 0
E 1031.233 1
E 1135.860 0
E 1239.780 1
E 1343.757 0
E 2056.327 1
E 2159.819 0
```

```svg
<svg viewBox="0 0 680 262" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="60" font-size="13">heartbeat</text>
<line x1="130" y1="70" x2="660" y2="70" stroke="currentColor" stroke-opacity="0.3"/>
<polyline points="130.0,40.0 141.9,40.0 141.9,70.0 191.1,70.0 191.1,40.0 207.3,40.0 207.3,70.0 389.4,70.0 389.4,40.0 405.6,40.0 405.6,70.0 454.7,70.0 454.7,40.0 470.6,40.0 470.6,70.0 654.0,70.0 654.0,40.0 660.0,40.0" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<text x="10" y="122" font-size="13">timer 100/300</text>
<line x1="130" y1="132" x2="660" y2="132" stroke="currentColor" stroke-opacity="0.3"/>
<polyline points="130.0,102.0 152.1,102.0 152.1,132.0 216.7,132.0 216.7,102.0 238.7,102.0 238.7,132.0 303.4,132.0 303.4,102.0 331.8,102.0 331.8,132.0 399.6,132.0 399.6,102.0 421.7,102.0 421.7,132.0 487.8,132.0 487.8,102.0 509.9,102.0 509.9,132.0 660.0,132.0" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="10" y="184" font-size="13">pattern</text>
<line x1="130" y1="194" x2="660" y2="194" stroke="currentColor" stroke-opacity="0.3"/>
<polyline points="130.0,164.0 150.2,164.0 150.2,194.0 172.1,194.0 172.1,164.0 194.2,164.0 194.2,194.0 348.6,194.0 348.6,164.0 370.8,164.0 370.8,194.0 392.8,194.0 392.8,164.0 414.9,164.0 414.9,194.0 565.9,194.0 565.9,164.0 587.9,164.0 587.9,194.0 609.9,194.0 609.9,164.0 631.9,164.0 631.9,194.0 660.0,194.0" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<line x1="130" y1="216" x2="660" y2="216" stroke="currentColor"/>
<line x1="130.0" y1="216" x2="130.0" y2="221" stroke="currentColor"/>
<text x="130.0" y="235" text-anchor="middle" font-size="12">0</text>
<line x1="236.0" y1="216" x2="236.0" y2="221" stroke="currentColor"/>
<text x="236.0" y="235" text-anchor="middle" font-size="12">500</text>
<line x1="342.0" y1="216" x2="342.0" y2="221" stroke="currentColor"/>
<text x="342.0" y="235" text-anchor="middle" font-size="12">1000</text>
<line x1="448.0" y1="216" x2="448.0" y2="221" stroke="currentColor"/>
<text x="448.0" y="235" text-anchor="middle" font-size="12">1500</text>
<line x1="554.0" y1="216" x2="554.0" y2="221" stroke="currentColor"/>
<text x="554.0" y="235" text-anchor="middle" font-size="12">2000</text>
<line x1="660.0" y1="216" x2="660.0" y2="221" stroke="currentColor"/>
<text x="660.0" y="235" text-anchor="middle" font-size="12">2500</text>
<text x="395.0" y="252" text-anchor="middle" font-size="12">ms (gpio-sim 라인 값을 약 30 us 간격으로 읽은 실측)</text>
</svg>
```

그림 2 — 세 트리거가 만든 실제 핀 파형. 위 출력의 엣지 목록(전체)을 그대로 그렸다. heartbeat는 "두 번 짧게 + 긴 쉼", timer는 100/300 ms 사각파, pattern은 우리가 적은 "두 번 깜빡 + 700 ms 쉼"이다.

출력에서 볼 것:

- **트리거가 붙는 순간**: heartbeat 모듈을 올리자 `[heartbeat]`가 선택됐다. DT의 `linux,default-trigger`가 늦게 등록된 트리거에도 적용된다.
- **heartbeat**: 켜짐 약 76 ms, 한 주기(상승 → 다음 쌍의 상승) 약 1223~1248 ms. 소스(`ledtrig-heartbeat.c`)의 주기는 `300 + (6720 << FSHIFT) / (5 * avenrun[0] + (7 << FSHIFT))` ms라 **load average 0이면 1260 ms이고, 부하가 오르면 빨라진다**. 켜짐 70 ms를 jiffy(HZ=250 → 4 ms) 단위로 올림한 것이 76 ms 근처다. 샘플러의 `period=622 ms`는 쌍박동의 상승 간격 평균이라 의미가 없다 — 지표는 파형에 맞게 골라야 한다.
- **timer 100/300**: 켜짐 104 ms, 주기 약 409 ms인데 한 번은 134 ms(817→951) 켜져 있었다. 소프트웨어 타이머(`timer_list`) + TCG 에뮬레이션의 지터다. **LED 트리거는 소프트웨어 타이머라 정밀하지 않다** — 정밀한 깜빡임이 필요하면 하드웨어 `blink_set`을 쓴다.
- **pattern**: `"밝기 시간"` 쌍의 나열이다. `1 100 1 0`처럼 **같은 밝기를 시간 0으로 한 번 더 적으면 계단(즉시 전환)**, 밝기가 다른 두 항목 사이에 시간이 있으면 그 시간 동안 **점진 변화(fade)** 한다(`Documentation/ABI/testing/sysfs-class-led-trigger-pattern`). 한 주기 1000 ms 요청에 실측 1031 ms. `repeat` 파일로 반복 횟수(-1 = 무한)를 정한다. 에러 코드를 "깜빡임 횟수"로 표시하는 제품 LED에 딱 맞는 트리거다.

### 2.5 흔한 함정 — `brightness`에 0을 쓰면 트리거가 사라진다

`brightness_store()`(`drivers/leds/led-class.c`)는 **0이 들어오면 `led_trigger_remove()`부터 부른다**. 그래서 "heartbeat 중인 LED를 잠깐 끄려고 `echo 0 > brightness`" 하면 heartbeat가 영영 꺼진다. 잠깐 끄려면 트리거를 바꾸거나 애플리케이션이 트리거를 다시 설정해야 한다. 같은 이유로 timer 트리거 중 `brightness`에 0이 아닌 값을 쓰면 깜빡임 밝기(`blink_brightness`)가 바뀐다(다음 예제에서 200으로 깜빡이는 것을 본다).

### 2.6 예제 5 — 내 LED 드라이버: `brightness_set_blocking`은 어디서 불리나

무엇을 확인하나: I2C LED IC처럼 쓰기가 잠드는 LED를 흉내 내는 드라이버를 만들고, 콜백이 어떤 문맥(task 이름)에서 불리는지, timer 트리거 10/10 ms를 걸었을 때 콜백이 2초에 몇 번 불리는지 센다.

```c
// i9_led.c — I2C LED 드라이버 흉내: brightness_set_blocking (잠들 수 있음)
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/delay.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/sched.h>

struct i9_led { struct led_classdev cdev; u8 reg; };
static unsigned int writes;			/* 확인용: 콜백 호출 수 */
module_param(writes, uint, 0444);

static int i9_led_set(struct led_classdev *c, enum led_brightness b)
{
	struct i9_led *l = container_of(c, struct i9_led, cdev);

	usleep_range(1000, 1200);		/* "I2C 쓰기" 1 ms 흉내 */
	l->reg = b;
	if (++writes <= 3 || writes % 50 == 0)
		pr_info("i9_led: set %3u by %s (in_task=%d) writes=%u\n",
			b, current->comm, !!in_task(), writes);
	return 0;
}

static enum led_brightness i9_led_get(struct led_classdev *c)
{
	return container_of(c, struct i9_led, cdev)->reg;
}

static int i9_led_probe(struct platform_device *pdev)
{
	struct led_init_data init = { };
	struct i9_led *l;
	int ret;

	l = devm_kzalloc(&pdev->dev, sizeof(*l), GFP_KERNEL);
	if (!l)
		return -ENOMEM;
	init.fwnode = device_get_next_child_node(&pdev->dev, NULL);
	l->cdev.max_brightness = 255;
	l->cdev.brightness_set_blocking = i9_led_set;
	l->cdev.brightness_get = i9_led_get;
	ret = devm_led_classdev_register_ext(&pdev->dev, &l->cdev, &init);
	fwnode_handle_put(init.fwnode);
	if (ret)
		return ret;
	dev_info(&pdev->dev, "registered %s max=%u\n",
		 dev_name(l->cdev.dev), l->cdev.max_brightness);
	return 0;
}

static const struct of_device_id i9_led_of[] = {
	{ .compatible = "dons,i9-led" }, { }
};
MODULE_DEVICE_TABLE(of, i9_led_of);
static struct platform_driver i9_led_drv = {
	.probe = i9_led_probe,
	.driver = { .name = "i9_led", .of_match_table = i9_led_of },
};
module_platform_driver(i9_led_drv);
MODULE_LICENSE("GPL");
```

```sh
insmod /i9_led.ko; L=/sys/class/leds/amber:indicator
echo $(ls /sys/class/leds); echo 200 > $L/brightness; cat $L/brightness
echo timer > $L/trigger; echo 10 > $L/delay_on; echo 10 > $L/delay_off
W0=$(cat /sys/module/i9_led/parameters/writes); sleep 2
echo "writes in 2 s: $(( $(cat /sys/module/i9_led/parameters/writes) - W0 )) (ideal 200)"
echo none > $L/trigger; dmesg | grep -E "i9_led|amber"; echo "brightness after none: $(cat $L/brightness)"
```

```text
amber:indicator
200
writes in 2 s: 118 (ideal 200)
[    4.274640] i9_led: loading out-of-tree module taints kernel.
[    4.282915] i9_led i9-led: registered amber:indicator max=255
[    4.296997] i9_led: set 200 by kworker/u4:3 (in_task=1) writes=1
[    4.318926] i9_led: set   0 by kworker/u4:3 (in_task=1) writes=2
[    4.334690] i9_led: set 200 by kworker/u4:3 (in_task=1) writes=3
[    5.144267] i9_led: set   0 by kworker/u4:3 (in_task=1) writes=50
[    6.005923] i9_led: set   0 by kworker/u4:3 (in_task=1) writes=100
brightness after none: 0
```

출력에서 볼 것:

- 이름이 DT 자식 노드의 `color = AMBER`, `function = "indicator"`에서 `amber:indicator`로 만들어졌다. 드라이버는 이름 문자열을 한 글자도 안 썼다(`led_init_data.fwnode`만 넘김).
- **sysfs에서 `echo 200`을 한 첫 호출조차 `kworker`가 불렀다.** `brightness_set`이 없고 `_blocking`만 있으면 core(`led_set_brightness_nopm()`)가 값을 저장하고 `leds` workqueue에 일을 넘긴다. 그래서 timer 트리거(softirq 문맥의 `timer_list`)에서도 안전하게 잠들 수 있다.
- 깜빡임 밝기가 200이다. 트리거 전 밝기가 `blink_brightness`가 된다(§2.5).
- 10/10 ms 깜빡임이면 2초에 200번이어야 하는데 118번. 원인은 **jiffy 해상도**다: HZ=250이라 10 ms는 `msecs_to_jiffies(10)` = 3 jiffy = 12 ms로 올림되고, `timer_list`는 다음 tick 경계에서 돌므로 실제로는 약 16 ms 간격(2000/118 ≈ 17 ms)이 된다. 여기에 1 ms "I2C 쓰기"가 workqueue에서 직렬로 더해진다. **LED 트리거로 수십 Hz 이상의 정확한 파형을 기대하지 말 것** — 그런 일은 PWM의 몫이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="190" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="105" y="42" text-anchor="middle" font-size="13">sysfs brightness 쓰기</text>
  <rect x="10" y="70" width="190" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="105" y="92" text-anchor="middle" font-size="13">트리거 (timer · heartbeat)</text>
  <rect x="245" y="45" width="190" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="67" text-anchor="middle" font-size="13">led_set_brightness()</text>
  <line x1="200" y1="37" x2="243" y2="56" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="200" y1="87" x2="243" y2="70" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="245" y="120" width="190" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="142" text-anchor="middle" font-size="13">brightness_set 있나?</text>
  <line x1="340" y1="79" x2="340" y2="118" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="470" y="70" width="200" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="92" text-anchor="middle" font-size="13">바로 호출 (호출자 문맥)</text>
  <line x1="435" y1="130" x2="468" y2="94" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="452" y="122" font-size="12">예</text>
  <rect x="470" y="175" width="200" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="197" text-anchor="middle" font-size="13">queue_work(leds_wq)</text>
  <line x1="435" y1="145" x2="468" y2="185" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="400" y="176" font-size="12">아니오</text>
  <rect x="470" y="245" width="200" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="570" y="267" text-anchor="middle" font-size="13">kworker: _blocking()</text>
  <line x1="570" y1="209" x2="570" y2="243" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="10" y="190" width="400" height="90" rx="4" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/>
  <text x="24" y="214" font-size="13">ordered workqueue라서</text>
  <text x="24" y="236" font-size="13">· 쓰기는 한 번에 하나, 순서 보장</text>
  <text x="24" y="258" font-size="13">· 밀린 값은 최신 값 하나로 합쳐질 수 있다</text>
</svg>
```

그림 3 — 밝기 쓰기의 두 갈래. 잠들지 않는 `brightness_set`이 있으면 호출자 문맥에서 바로 부르고, `_blocking`만 있으면 `leds` workqueue를 거쳐 kworker가 부른다. 그래서 드라이버는 "내 쓰기가 잠드나"만 정직하게 고르면 된다.

### 2.7 multicolor LED, PWM LED, 하드웨어 오프로드

- **multicolor (RGB)**: `struct led_classdev_mc`가 `led_classdev` 하나 + 채널 배열(`struct mc_subled`)을 갖는다. sysfs에 `multi_index`(채널 색 순서, 예: `red green blue`)와 `multi_intensity`(채널별 강도)가 생기고, 최종 채널 밝기 = `brightness × intensity / max_brightness`다. 드라이버는 `led_mc_calc_color_components()`로 계산한다. DT는 `color = <LED_COLOR_ID_RGB>` 노드 아래 채널 자식 노드를 둔다. 랩 커널에는 `led-class-multicolor.ko`가 모듈로 있다 (이 노트에서 실행하지 않음).
- **leds-pwm**: PWM 채널을 LED class로 감싸는 드라이버(`compatible = "pwm-leds"`). 밝기 0..255가 duty로 바뀐다. 밝기 조절이 필요한 LED는 GPIO보다 이쪽이다.
- **하드웨어 오프로드**: 이더넷 PHY의 링크/활동 LED는 PHY가 스스로 깜빡인다. 6.x의 netdev 트리거는 `hw_control_is_supported/hw_control_set` 콜백으로 "이 LED는 하드웨어가 링크·트래픽에 맞춰 직접 깜빡여라"를 PHY 드라이버에 맡길 수 있다. CPU가 패킷마다 LED를 토글하지 않아도 된다.

---

## 3. PWM — period · duty · polarity를 "상태 하나"로 원자적으로 바꾼다

### 3.1 직관: 펌웨어의 ARR/CCR이 `struct pwm_state`가 된다

STM32 타이머로 PWM을 만들 때는 `ARR`(주기), `CCR`(비교값 = duty), `CCxP`(극성), `CCxE`(출력 enable)를 각각 썼다. 그리고 "ARR을 먼저 줄이면 CCR > ARR인 순간 100% 출력이 한 주기 튄다" 같은 순서 문제를 preload 비트(ARPE/OCxPE)로 피했다.

Linux PWM은 이 네 값을 **한 구조체에 담아 한 번에 적용(apply)** 한다. 드라이버는 "이 상태를 하드웨어에 반영하라"는 콜백 하나만 구현하고, 순서·글리치 없는 전환은 드라이버가 하드웨어 특성에 맞게 책임진다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker>
  <marker id="ah3b" markerWidth="8" markerHeight="8" refX="1" refY="4" orient="auto"><path d="M8,0 L0,4 L8,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="40" font-size="13">normal</text>
  <polyline points="90,70 90,30 170,30 170,70 330,70 330,30 410,30 410,70 570,70 570,30 650,30" fill="none" stroke="#4a7bd0" stroke-width="2"/>
  <line x1="90" y1="82" x2="330" y2="82" stroke="currentColor" marker-start="url(#ah3b)" marker-end="url(#ah3)"/>
  <text x="210" y="98" text-anchor="middle" font-size="12">period (ns)</text>
  <line x1="90" y1="20" x2="170" y2="20" stroke="currentColor" marker-start="url(#ah3b)" marker-end="url(#ah3)"/>
  <text x="130" y="14" text-anchor="middle" font-size="12">duty_cycle</text>
  <text x="10" y="140" font-size="13">inversed</text>
  <polyline points="90,130 90,170 170,170 170,130 330,130 330,170 410,170 410,130 570,130 570,170 650,170" fill="none" stroke="#e08a3c" stroke-width="2"/>
  <text x="380" y="196" text-anchor="middle" font-size="12">같은 duty_cycle인데 "활성 = Low"</text>
  <rect x="10" y="215" width="200" height="74" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="20" y="235" font-size="12">struct pwm_state {</text>
  <text x="30" y="251" font-size="12">period, duty_cycle (ns)</text>
  <text x="30" y="267" font-size="12">polarity, enabled</text>
  <text x="20" y="283" font-size="12">usage_power }</text>
  <rect x="240" y="232" width="130" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="305" y="257" text-anchor="middle" font-size="13">pwm core 검증</text>
  <rect x="400" y="232" width="120" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="460" y="257" text-anchor="middle" font-size="13">ops-&gt;apply()</text>
  <rect x="550" y="232" width="120" height="40" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="610" y="257" text-anchor="middle" font-size="13">타이머 레지스터</text>
  <line x1="210" y1="252" x2="238" y2="252" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="370" y1="252" x2="398" y2="252" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="520" y1="252" x2="548" y2="252" stroke="currentColor" marker-end="url(#ah3)"/>
</svg>
```

그림 4 — PWM의 네 값과 적용 경로. 시간 단위는 모두 나노초다. inversed 극성은 같은 duty_cycle을 "Low인 시간"으로 해석한다. 소비자는 상태 전체를 만들어 `pwm_apply_might_sleep()`에 넘기고, core가 검증한 뒤 드라이버의 `.apply`를 한 번 부른다.

### 3.2 정의: provider 쪽 — `pwm_chip`과 `pwm_ops`

```c
struct pwm_state {
	u64 period;		/* ns */
	u64 duty_cycle;		/* ns, 0..period */
	enum pwm_polarity polarity;	/* PWM_POLARITY_NORMAL / _INVERSED */
	bool enabled;
	bool usage_power;	/* "평균 전력만 맞으면 됨" — 위상·주기 자유 */
};

struct pwm_ops {
	int (*request)(struct pwm_chip *chip, struct pwm_device *pwm);
	void (*free)(struct pwm_chip *chip, struct pwm_device *pwm);
	int (*capture)(struct pwm_chip *chip, struct pwm_device *pwm,
		       struct pwm_capture *result, unsigned long timeout);
	/* 최근 6.x에 추가된 waveform 콜백 (선택): round_waveform_tohw/fromhw,
	   read_waveform, write_waveform — 아래 설명 */
	int (*apply)(struct pwm_chip *chip, struct pwm_device *pwm,
		     const struct pwm_state *state);
	int (*get_state)(struct pwm_chip *chip, struct pwm_device *pwm,
			 struct pwm_state *state);
};
```

- `.apply`: 상태 전체를 하드웨어에 반영한다. **현재 주기를 끝까지 마치고 바꾸는지(글리치 없음), 즉시 바꾸는지**는 하드웨어에 달렸고, 드라이버 주석에 "Limitations"로 적는 것이 관례다.
- `.get_state`: 하드웨어에서 현재 상태를 읽는다. 부팅 때 부트로더가 켜 둔 PWM(예: 백라이트)을 커널이 이어받을 때 쓴다.
- 등록은 `devm_pwmchip_alloc(parent, npwm, sizeof_priv)` → `chip->ops = &ops` → `devm_pwmchip_add()`. 최근 6.x 커널에서 `pwm_chip`을 core가 할당하는 방식으로 바뀌어(예전 드라이버는 `pwm_chip`을 자기 구조체에 박아 넣었다) private 데이터는 `pwmchip_get_drvdata(chip)`로 꺼낸다.
- `chip->atomic = true`이면 `.apply`가 잠들지 않는다는 약속이고, 소비자가 `pwm_apply_atomic()`(IRQ 문맥 가능)을 쓸 수 있다.
- **waveform API**: 6.18 소스의 `include/linux/pwm.h`에는 `struct pwm_waveform { period_length_ns, duty_length_ns, duty_offset_ns }`와 `pwm_set_waveform_might_sleep()`, `pwm_round_waveform_might_sleep()`이 있다. `pwm_state`와 달리 **상승 엣지의 위상(offset)** 을 표현하고, "하드웨어가 실제로 낼 수 있는 값으로 어떻게 반올림되는지"를 적용 전에 물어볼 수 있다. 이 콜백을 구현한 드라이버(`pwm-stm32`, `pwm-axi-pwmgen`, `pwm-pca9685` 등 — 랩 소스에서 `write_waveform`으로 검색)에만 `/dev/pwmchipN` 문자 장치가 생긴다(§1.4). 아직은 대부분의 드라이버가 `.apply` 방식이다.

### 3.3 예제 6 — sysfs로 소프트웨어 PWM을 돌리고 실제 duty를 재 보기

무엇을 확인하나: `/sys/class/pwm` 인터페이스(export → period/duty_cycle/polarity/enable)를 쓰고, PL061 4번 핀을 `/dev/mem`으로 직접 샘플링해 **요청한 duty와 실제 high 비율**을 비교한다. 대상은 pwm-gpio(GPIO를 hrtimer로 토글하는 소프트웨어 PWM)다.

```sh
modprobe pwm-gpio
P=$(dirname $(dirname $(grep -l i9-pwm-gpio-pl061 /sys/class/pwm/pwmchip*/device/uevent)))
echo 0 > $P/export; echo $(ls $P/pwm0)
for per in 20000000 5000000 1000000; do
  echo 0 > $P/pwm0/duty_cycle; echo $per > $P/pwm0/period; echo 1 > $P/pwm0/enable
  for d in 10 25 50 75 90; do
    echo $((per / 100 * d)) > $P/pwm0/duty_cycle
    echo "REQ per=$per duty=$d% -> $(/i9_sample pl061:4 1000)"
    [ $d = 25 ] && /i9_sample pl061:4 $((per / 250000)) e > /tmp/w$per.txt
  done
  echo 0 > $P/pwm0/enable
done
echo 20000000 > $P/pwm0/period; echo 5000000 > $P/pwm0/duty_cycle
echo inversed > $P/pwm0/polarity; echo 1 > $P/pwm0/enable
echo "inversed 25%: $(/i9_sample pl061:4 1000)"
```

첫 번째 실행의 출력:

```text
/sys/class/pwm/pwmchip0 npwm=1 dev=i9-pwm-gpio-pl061
capture duty_cycle enable period polarity power uevent
/sys/class/pwm/pwmchip0/pwm0: period=0 duty=0 pol=normal en=0
REQ per=20000000 duty=10% -> samples=2585711 (0.4 us/sample) high=7.1% rises=50 period=19.999 ms
REQ per=20000000 duty=25% -> samples=2497392 (0.4 us/sample) high=28.7% rises=50 period=19.986 ms
REQ per=20000000 duty=50% -> samples=2321129 (0.4 us/sample) high=47.7% rises=49 period=20.417 ms
REQ per=20000000 duty=75% -> samples=2119923 (0.5 us/sample) high=72.8% rises=50 period=20.007 ms
REQ per=20000000 duty=90% -> samples=2283856 (0.4 us/sample) high=88.9% rises=50 period=19.980 ms
REQ per=5000000 duty=10% -> samples=1892364 (0.5 us/sample) high=6.0% rises=142 period=7.021 ms
REQ per=5000000 duty=25% -> samples=2213496 (0.5 us/sample) high=21.6% rises=193 period=5.183 ms
REQ per=5000000 duty=50% -> samples=2275350 (0.4 us/sample) high=47.9% rises=197 period=5.076 ms
REQ per=5000000 duty=75% -> samples=2288138 (0.4 us/sample) high=75.9% rises=197 period=5.077 ms
REQ per=5000000 duty=90% -> samples=2105751 (0.5 us/sample) high=91.7% rises=146 period=6.862 ms
REQ per=1000000 duty=10% -> samples=1853798 (0.5 us/sample) high=2.3% rises=202 period=4.915 ms
REQ per=1000000 duty=25% -> samples=1574545 (0.6 us/sample) high=13.1% rises=441 period=2.259 ms
REQ per=1000000 duty=50% -> samples=1662285 (0.6 us/sample) high=55.2% rises=953 period=1.049 ms
REQ per=1000000 duty=75% -> samples=1829273 (0.5 us/sample) high=75.4% rises=949 period=1.054 ms
REQ per=1000000 duty=90% -> samples=1987753 (0.5 us/sample) high=96.9% rises=229 period=4.312 ms
inversed 25%: samples=1936909 (0.5 us/sample) high=78.7% rises=44 period=22.788 ms
```

같은 시나리오를 세 번 부팅해서 돌린 값의 **중앙값**(high 비율)과 "1초 동안 나와야 할 펄스 중 실제로 관측된 펄스 비율"(rises ÷ 1000 ms/period)이다. 다른 노트 작성자들이 같은 Mac에서 QEMU를 동시에 돌리는 상태라 실행마다 편차가 크다.

| period | 요청 10% | 25% | 50% | 75% | 90% | 관측된 펄스 (10% / 50% / 90%) |
|---|---|---|---|---|---|---|
| 20 ms | 8.4% | 28.2% | 50.0% | 73.3% | 89.2% | 100% / 100% / 100% |
| 5 ms | 6.0% | 21.6% | 49.5% | 78.1% | 94.6% | 71% / 99% / 73% |
| 1 ms | 0.4% | 13.1% | 55.2% | 78.5% | 96.9% | 4% / 99.5% / 23% |

```svg
<svg viewBox="0 0 640 380" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="330" x2="470" y2="330" stroke="currentColor"/>
<line x1="70" y1="330" x2="70" y2="30" stroke="currentColor"/>
<line x1="70.0" y1="330" x2="70.0" y2="335" stroke="currentColor"/><text x="70.0" y="349" text-anchor="middle" font-size="12">0</text>
<line x1="65" y1="330.0" x2="70" y2="330.0" stroke="currentColor"/><text x="61" y="334.0" text-anchor="end" font-size="12">0</text>
<line x1="70" y1="330.0" x2="470" y2="330.0" stroke="currentColor" stroke-opacity="0.12"/>
<line x1="170.0" y1="330" x2="170.0" y2="335" stroke="currentColor"/><text x="170.0" y="349" text-anchor="middle" font-size="12">25</text>
<line x1="65" y1="255.0" x2="70" y2="255.0" stroke="currentColor"/><text x="61" y="259.0" text-anchor="end" font-size="12">25</text>
<line x1="70" y1="255.0" x2="470" y2="255.0" stroke="currentColor" stroke-opacity="0.12"/>
<line x1="270.0" y1="330" x2="270.0" y2="335" stroke="currentColor"/><text x="270.0" y="349" text-anchor="middle" font-size="12">50</text>
<line x1="65" y1="180.0" x2="70" y2="180.0" stroke="currentColor"/><text x="61" y="184.0" text-anchor="end" font-size="12">50</text>
<line x1="70" y1="180.0" x2="470" y2="180.0" stroke="currentColor" stroke-opacity="0.12"/>
<line x1="370.0" y1="330" x2="370.0" y2="335" stroke="currentColor"/><text x="370.0" y="349" text-anchor="middle" font-size="12">75</text>
<line x1="65" y1="105.0" x2="70" y2="105.0" stroke="currentColor"/><text x="61" y="109.0" text-anchor="end" font-size="12">75</text>
<line x1="70" y1="105.0" x2="470" y2="105.0" stroke="currentColor" stroke-opacity="0.12"/>
<line x1="470.0" y1="330" x2="470.0" y2="335" stroke="currentColor"/><text x="470.0" y="349" text-anchor="middle" font-size="12">100</text>
<line x1="65" y1="30.0" x2="70" y2="30.0" stroke="currentColor"/><text x="61" y="34.0" text-anchor="end" font-size="12">100</text>
<line x1="70" y1="30.0" x2="470" y2="30.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="270.0" y="370" text-anchor="middle" font-size="13">요청 duty (%)</text>
<text x="18" y="180.0" text-anchor="middle" font-size="13" transform="rotate(-90 18 180.0)">실측 high 비율 (%)</text>
<line x1="70.0" y1="330.0" x2="470.0" y2="30.0" stroke="#888" stroke-dasharray="5 4"/>
<polyline points="110.0,304.8 170.0,245.4 270.0,180.0 370.0,110.1 430.0,62.4" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="110.0" cy="304.8" r="4" fill="#4a7bd0"/>
<circle cx="170.0" cy="245.4" r="4" fill="#4a7bd0"/>
<circle cx="270.0" cy="180.0" r="4" fill="#4a7bd0"/>
<circle cx="370.0" cy="110.1" r="4" fill="#4a7bd0"/>
<circle cx="430.0" cy="62.4" r="4" fill="#4a7bd0"/>
<line x1="495" y1="60" x2="525" y2="60" stroke="#4a7bd0" stroke-width="2"/><text x="532" y="64" font-size="13">period 20 ms</text>
<polyline points="110.0,312.0 170.0,265.2 270.0,181.5 370.0,95.7 430.0,46.2" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="110.0" cy="312.0" r="4" fill="#e08a3c"/>
<circle cx="170.0" cy="265.2" r="4" fill="#e08a3c"/>
<circle cx="270.0" cy="181.5" r="4" fill="#e08a3c"/>
<circle cx="370.0" cy="95.7" r="4" fill="#e08a3c"/>
<circle cx="430.0" cy="46.2" r="4" fill="#e08a3c"/>
<line x1="495" y1="84" x2="525" y2="84" stroke="#e08a3c" stroke-width="2"/><text x="532" y="88" font-size="13">period 5 ms</text>
<polyline points="110.0,328.8 170.0,290.7 270.0,164.4 370.0,94.5 430.0,39.3" fill="none" stroke="#d0564a" stroke-width="2"/>
<circle cx="110.0" cy="328.8" r="4" fill="#d0564a"/>
<circle cx="170.0" cy="290.7" r="4" fill="#d0564a"/>
<circle cx="270.0" cy="164.4" r="4" fill="#d0564a"/>
<circle cx="370.0" cy="94.5" r="4" fill="#d0564a"/>
<circle cx="430.0" cy="39.3" r="4" fill="#d0564a"/>
<line x1="495" y1="108" x2="525" y2="108" stroke="#d0564a" stroke-width="2"/><text x="532" y="112" font-size="13">period 1 ms</text>
<line x1="495" y1="132" x2="525" y2="132" stroke="#888" stroke-dasharray="5 4"/><text x="532" y="136" font-size="13">이상 (y = x)</text>
</svg>
```

그림 5 — 요청 duty 대비 실측 high 비율(3회 중앙값). 20 ms 주기는 이상선(점선) 근처, 주기가 짧아질수록 양 끝(10%, 90%)이 이상선에서 멀어진다 — 짧은 쪽 구간이 사라지기 때문이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="42" font-size="13">period 20 ms</text>
<text x="10" y="60" font-size="12">0 ~ 80 ms</text>
<polyline points="120.0,59.0 196.9,59.0 196.9,29.0 230.7,29.0 230.7,59.0 331.9,59.0 331.9,29.0 365.7,29.0 365.7,59.0 466.9,59.0 466.9,29.0 500.7,29.0 500.7,59.0 601.9,59.0 601.9,29.0 635.7,29.0 635.7,59.0 660.0,59.0" fill="none" stroke="#888" stroke-dasharray="4 3"/>
<polyline points="120.0,60.0 196.9,60.0 196.9,30.0 236.5,30.0 236.5,60.0 335.1,60.0 335.1,30.0 371.9,30.0 371.9,60.0 465.9,60.0 465.9,30.0 507.0,30.0 507.0,60.0 601.2,60.0 601.2,30.0 642.1,30.0 642.1,60.0 660.0,60.0" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<line x1="120" y1="68" x2="660" y2="68" stroke="currentColor" stroke-opacity="0.4"/>
<text x="120.0" y="84" text-anchor="middle" font-size="12">0</text>
<text x="255.0" y="84" text-anchor="middle" font-size="12">20</text>
<text x="390.0" y="84" text-anchor="middle" font-size="12">40</text>
<text x="525.0" y="84" text-anchor="middle" font-size="12">60</text>
<text x="660.0" y="84" text-anchor="middle" font-size="12">80</text>
<text x="10" y="130" font-size="13">period 5 ms</text>
<text x="10" y="148" font-size="12">0 ~ 20 ms</text>
<polyline points="120.0,147.0 162.2,147.0 162.2,117.0 196.0,117.0 196.0,147.0 297.2,147.0 297.2,117.0 331.0,117.0 331.0,147.0 432.2,147.0 432.2,117.0 466.0,117.0 466.0,147.0 567.2,147.0 567.2,117.0 601.0,117.0 601.0,147.0 660.0,147.0" fill="none" stroke="#888" stroke-dasharray="4 3"/>
<polyline points="120.0,148.0 162.2,148.0 162.2,118.0 193.2,118.0 193.2,148.0 295.6,148.0 295.6,118.0 332.4,118.0 332.4,148.0 437.1,148.0 437.1,118.0 466.5,118.0 466.5,148.0 568.6,148.0 568.6,118.0 597.3,118.0 597.3,148.0 660.0,148.0" fill="none" stroke="#e08a3c" stroke-width="2"/>
<line x1="120" y1="156" x2="660" y2="156" stroke="currentColor" stroke-opacity="0.4"/>
<text x="120.0" y="172" text-anchor="middle" font-size="12">0</text>
<text x="255.0" y="172" text-anchor="middle" font-size="12">5</text>
<text x="390.0" y="172" text-anchor="middle" font-size="12">10</text>
<text x="525.0" y="172" text-anchor="middle" font-size="12">15</text>
<text x="660.0" y="172" text-anchor="middle" font-size="12">20</text>
<text x="10" y="218" font-size="13">period 1 ms</text>
<text x="10" y="236" font-size="12">0 ~ 4 ms</text>
<polyline points="120.0,235.0 158.2,235.0 158.2,205.0 192.0,205.0 192.0,235.0 293.2,235.0 293.2,205.0 327.0,205.0 327.0,235.0 428.2,235.0 428.2,205.0 462.0,205.0 462.0,235.0 563.2,235.0 563.2,205.0 597.0,205.0 597.0,235.0 660.0,235.0" fill="none" stroke="#888" stroke-dasharray="4 3"/>
<polyline points="120.0,236.0 158.2,236.0 158.2,206.0 169.3,206.0 169.3,236.0 563.6,236.0 563.6,206.0 587.5,206.0 587.5,236.0 660.0,236.0" fill="none" stroke="#d0564a" stroke-width="2"/>
<line x1="120" y1="244" x2="660" y2="244" stroke="currentColor" stroke-opacity="0.4"/>
<text x="120.0" y="260" text-anchor="middle" font-size="12">0</text>
<text x="255.0" y="260" text-anchor="middle" font-size="12">1</text>
<text x="390.0" y="260" text-anchor="middle" font-size="12">2</text>
<text x="525.0" y="260" text-anchor="middle" font-size="12">3</text>
<text x="660.0" y="260" text-anchor="middle" font-size="12">4</text>
<line x1="420" y1="290" x2="450" y2="290" stroke="#888" stroke-dasharray="4 3"/><text x="456" y="294" font-size="12">요청 (이상 파형)</text>
<line x1="560" y1="290" x2="590" y2="290" stroke="#4a7bd0" stroke-width="2"/><text x="596" y="294" font-size="12">실측</text>
</svg>
```

그림 6 — duty 25%로 세 주기를 실제로 샘플링한 파형(첫 번째 실행의 엣지 목록). 점선은 첫 상승 엣지에 맞춘 이상 파형이다. 1 ms 주기에서는 4 ms 동안 펄스 4개 중 2개만 나왔고 하나는 폭이 82 us(요청 250 us)로 깎였다.

출력에서 볼 것:

- **20 ms 주기는 쓸 만하다**: 펄스 개수가 정확하고 duty 오차는 대략 ±3%p 이내. LED 밝기, 부저, 느린 서보(50 Hz = 20 ms)에는 충분하다.
- **1 ms 주기에서는 무너진다**: 10%(100 us 펄스)는 1초에 1000개 중 41개만 관측됐다. pwm-gpio의 타이머 콜백(`pwm_gpio_timer()`)은 `hrtimer_forward(timer, 이전 만료 시각, 다음 간격)`으로 다음 토글을 잡는다. 콜백이 짧은 구간(100 us)보다 늦게 돌면 다음 만료 시각이 이미 지나 있어서 **곧바로 다시 토글 → 짧은 구간이 거의 0으로 붕괴**한다. 50%처럼 양쪽 구간이 길면 펄스는 다 나오지만 폭이 흔들린다.
- **TCG 주의**: 이 가상 머신은 KVM 없이 QEMU가 명령어를 번역해 돌리고(TCG), 호스트도 바쁘다. 실제 Cortex-A의 hrtimer 지연은 보통 수~수십 us라 숫자는 훨씬 좋다. 하지만 **경향은 진짜다**: 소프트웨어 PWM의 정확도는 "가장 짧은 구간 대비 타이머 지연"이 결정하고, 인터럽트가 막히는 순간(다른 드라이버의 긴 IRQ off 구간) 바로 파형이 깨진다. 모터·전력 제어에 소프트웨어 PWM을 쓰지 않는 이유다.
- **inversed**: duty 25%를 inversed로 걸자 high 비율이 71~79% — 즉 Low가 25%다. 극성 변경은 pwm-gpio에서 disable 상태에서만 했다(대부분의 하드웨어도 마찬가지).
- **샘플러 자체의 정확도**: `0.4 us/sample`이라 100 us 펄스를 놓칠 만큼 느리지는 않다. 다만 샘플러와 hrtimer가 vCPU 하나를 나눠 쓰므로 vCPU가 호스트에서 잠깐 밀리면 둘 다 멈춘다. "관측" 손실의 일부는 측정 쪽 손실일 수 있다 — 그래서 실제 보드에서는 로직 분석기로 다시 확인해야 한다.

### 3.4 순서 함정 — period를 먼저 줄이면 EINVAL

무엇을 확인하나: 펌웨어의 "ARR을 CCR보다 작게 만들면 안 된다"가 Linux에서는 어떻게 나타나나.

```sh
echo 20000000 > $P/pwm0/period; echo 10000000 > $P/pwm0/duty_cycle; echo 1 > $P/pwm0/enable
echo "set period 5ms while duty=10ms:"; echo 5000000 > $P/pwm0/period
echo "period=$(cat $P/pwm0/period) duty=$(cat $P/pwm0/duty_cycle)"
echo 2500000 > $P/pwm0/duty_cycle; echo 5000000 > $P/pwm0/period
echo "after duty first: period=$(cat $P/pwm0/period) duty=$(cat $P/pwm0/duty_cycle)"
```

```text
set period 5ms while duty=10ms:
sh: write error: Invalid argument
period=20000000 duty=10000000
after duty first: period=5000000 duty=2500000
```

출력에서 볼 것: sysfs는 파일 하나를 쓸 때마다 "현재 상태 + 그 값 하나"로 `pwm_apply_might_sleep()`을 부른다. 켜진(enabled) 상태에서 `duty_cycle > period`는 `pwm_state_valid()`가 거부한다. **줄일 때는 duty 먼저, 늘릴 때는 period 먼저.** 꺼진 상태에서는 core가 검증을 건너뛰어(꺼진 상태의 나머지 값은 의미 없다고 보므로) 이상한 값이 들어가도 에러가 안 나고, enable 순간에 에러가 난다. 커널 소비자는 `struct pwm_state`를 통째로 만들어 한 번에 apply하므로 이 문제가 없다 — sysfs가 "원자적 상태"를 잘게 쪼개 버리는 인터페이스라서 생기는 함정이다.

### 3.5 gpio-sim 위의 pwm-gpio는 왜 거부됐나

§1.4의 `sleeping GPIO not supported`는 `pwm_gpio_probe()`의 이 검사다.

```c
	if (gpiod_cansleep(gpwm->gpio))
		return dev_err_probe(dev, -EINVAL,
				     "%pfw: sleeping GPIO not supported\n",
				     dev_fwnode(dev));
	chip->ops = &pwm_gpio_ops;
	chip->atomic = true;
```

토글은 hrtimer 콜백(hardirq 문맥)에서 `gpiod_set_value()`로 하므로 잠드는 GPIO 칩(gpio-sim은 `can_sleep = true`, I2C GPIO 확장기도 마찬가지)은 쓸 수 없다. H4에서 본 "`_cansleep` 구분"이 여기서 probe 실패로 드러난다. 보드에서 소프트웨어 PWM이 필요하면 **SoC 내장 GPIO(MMIO) 핀**을 골라야 한다.

### 3.6 예제 7 — 내 `pwm_chip` 드라이버 + 소비자 드라이버 + pwm-fan

무엇을 확인하나: 가짜 PWM 컨트롤러(채널 2개)를 등록하고, ① 직접 만든 소비자가 DT의 `pwms`에서 period·polarity를 받아 30% duty로 켜는 경로, ② 커널의 pwm-fan(hwmon)이 같은 칩을 쓰는 경로에서 `.apply`가 어떤 상태로 불리는지 본다. 소스는 길어서 두 부분으로 나눈다.

```c
// i9_pwm.c (1/2) — 가짜 pwm_chip: apply/get_state 로그
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/mod_devicetable.h>

struct i9_hw { struct pwm_state st[2]; };	/* "레지스터" = 채널별 상태 */

static int i9_apply(struct pwm_chip *chip, struct pwm_device *pwm,
		    const struct pwm_state *s)
{
	struct i9_hw *hw = pwmchip_get_drvdata(chip);

	if (s->period < 1000)			/* HW 한계 흉내: 최소 1 us */
		return -EINVAL;
	hw->st[pwm->hwpwm] = *s;
	hw->st[pwm->hwpwm].duty_cycle = s->duty_cycle / 1000 * 1000; /* 1us 단위 */
	pr_info("i9_pwm: apply ch%u period=%llu duty=%llu pol=%s en=%d\n",
		pwm->hwpwm, s->period, s->duty_cycle,
		s->polarity == PWM_POLARITY_INVERSED ? "inv" : "norm", s->enabled);
	return 0;
}

static int i9_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			struct pwm_state *s)
{
	struct i9_hw *hw = pwmchip_get_drvdata(chip);

	*s = hw->st[pwm->hwpwm];
	return 0;
}

static const struct pwm_ops i9_ops = { .apply = i9_apply, .get_state = i9_get_state };

static int i9_pwm_probe(struct platform_device *pdev)
{
	struct pwm_chip *chip = devm_pwmchip_alloc(&pdev->dev, 2, sizeof(struct i9_hw));

	if (IS_ERR(chip))
		return PTR_ERR(chip);
	chip->ops = &i9_ops;
	return devm_pwmchip_add(&pdev->dev, chip);
}
```

```c
// i9_pwm.c (2/2) — 소비자 드라이버와 등록
static int i9_cons_probe(struct platform_device *pdev)
{
	struct pwm_device *pwm = devm_pwm_get(&pdev->dev, "motor");
	struct pwm_state st;
	int ret;

	if (IS_ERR(pwm))
		return dev_err_probe(&pdev->dev, PTR_ERR(pwm), "no pwm\n");
	pwm_init_state(pwm, &st);	/* DT의 period·polarity를 가져온다 */
	dev_info(&pdev->dev, "args: period=%llu pol=%d\n", st.period, st.polarity);
	pwm_set_relative_duty_cycle(&st, 30, 100);
	st.enabled = true;
	ret = pwm_apply_might_sleep(pwm, &st);
	pwm_get_state(pwm, &st);
	dev_info(&pdev->dev, "applied ret=%d duty=%llu\n", ret, st.duty_cycle);
	return ret;
}

static const struct of_device_id i9_pwm_of[] = { { .compatible = "dons,i9-pwm" }, { } };
static const struct of_device_id i9_cons_of[] = { { .compatible = "dons,i9-pwm-consumer" }, { } };
static struct platform_driver i9_pwm_drv = { .probe = i9_pwm_probe,
	.driver = { .name = "i9_pwm", .of_match_table = i9_pwm_of } };
static struct platform_driver i9_cons_drv = { .probe = i9_cons_probe,
	.driver = { .name = "i9_pwm_cons", .of_match_table = i9_cons_of } };

static int __init i9_init(void)
{
	int ret = platform_driver_register(&i9_pwm_drv);

	return ret ? ret : platform_driver_register(&i9_cons_drv);
}
static void __exit i9_exit(void)
{
	platform_driver_unregister(&i9_cons_drv);
	platform_driver_unregister(&i9_pwm_drv);
}
module_init(i9_init);
module_exit(i9_exit);
MODULE_LICENSE("GPL");
```

```sh
insmod /i9_pwm.ko; modprobe pwm-fan
dmesg | grep -E "i9|pwm|fan"
H=$(dirname $(grep -l pwmfan /sys/class/hwmon/hwmon*/name))
echo "hwmon: $H name=$(cat $H/name) pwm1=$(cat $H/pwm1)"
echo 128 > $H/pwm1; echo 0 > $H/pwm1
dmesg | grep i9_pwm | tail -3
F=$(dirname $(dirname $(grep -l i9-fake-pwm /sys/class/pwm/pwmchip*/device/uevent)))
echo "$F export ch1:"; echo 1 > $F/export
cat /sys/kernel/debug/pwm
```

```text
[    4.679321] i9_pwm: loading out-of-tree module taints kernel.
[    4.684742] i9_pwm_cons i9-motor: args: period=50000 pol=1
[    4.684857] i9_pwm: apply ch1 period=50000 duty=15000 pol=inv en=1
[    4.684910] i9_pwm_cons i9-motor: applied ret=0 duty=15000
[    4.733606] i9_pwm: apply ch0 period=40000 duty=39999 pol=norm en=0
[    4.733682] i9_pwm: apply ch0 period=40000 duty=39999 pol=norm en=1
hwmon: /sys/class/hwmon/hwmon0 name=pwmfan pwm1=255
[    4.733682] i9_pwm: apply ch0 period=40000 duty=39999 pol=norm en=1
[    4.794576] i9_pwm: apply ch0 period=40000 duty=20078 pol=norm en=1
[    4.795085] i9_pwm: apply ch0 period=40000 duty=0 pol=norm en=0
/sys/class/pwm/pwmchip0 export ch1:
sh: write error: Device or resource busy
== debugfs
0: platform/i9-fake-pwm, npwm: 2
 pwm-0   (i9-fan              ): requested
  requested configuration: disabled, 0/40000 ns, normal polarity, usage_power
  actual configuration:    disabled, 0/40000 ns, normal polarity
 pwm-1   (motor               ): requested
  requested configuration:  enabled, 15000/50000 ns, inverse polarity
  actual configuration:     enabled, 15000/50000 ns, inverse polarity
```

출력에서 볼 것:

- 소비자는 숫자를 하드코딩하지 않았다. `pwm_init_state()`가 DT `pwms = <&i9_fpwm 1 50000 PWM_POLARITY_INVERTED>`에서 50 us 주기와 inversed 극성을 가져왔고(`pol=1`), 30% = 15000 ns가 한 번의 `.apply`로 들어갔다.
- pwm-fan은 probe 때 최대 속도(`pwm1=255` → duty 39999/40000)로 켠다. 팬을 안전 쪽(최대 냉각)으로 시작하는 정책이다. `pwm1=128` → duty 20078 ns(128/255 × 40000), `pwm1=0` → **duty 0이면서 `en=0`** — pwm-fan은 0일 때 PWM을 아예 끈다.
- 소비자가 이미 잡은 채널을 sysfs로 export하려 하자 `EBUSY`. 커널 소비자와 유저 공간이 같은 채널을 동시에 쥘 수 없다.
- debugfs의 `requested`와 `actual`은 각각 core가 마지막으로 apply한 상태와 `.get_state`로 읽은 하드웨어 상태다. 드라이버가 반올림하면 둘이 달라진다 — bring-up 때 "내가 준 값이 실제로 어떻게 들어갔나"를 보는 곳이다. fan 쪽에 `usage_power`가 표시된 것은 pwm-fan이 "평균 전력만 맞으면 된다"고 표시해 하드웨어가 위상을 자유롭게 골라도 된다는 뜻이다.

### 3.7 소비자 API와 DT 요약

| API | 용도 |
|---|---|
| `devm_pwm_get(dev, con_id)` | DT `pwms`/`pwm-names`에서 PWM 얻기 (`con_id`는 `pwm-names` 항목) |
| `pwm_init_state(pwm, &st)` | DT 인자(period·polarity)로 상태 초기화, duty=0·disabled |
| `pwm_set_relative_duty_cycle(&st, n, scale)` | duty = period × n / scale |
| `pwm_apply_might_sleep(pwm, &st)` | 상태 적용 (프로세스 문맥) |
| `pwm_apply_atomic(pwm, &st)` | 원자 문맥 적용 (`chip->atomic`인 칩만) |
| `pwm_get_state(pwm, &st)` | 마지막으로 요청한 상태 읽기 |
| `pwm_disable(pwm)` | 끄기 (상태의 enabled=false apply) |

대표 커널 소비자: `pwm-backlight`(`pwm_bl.c`, `brightness-levels` 테이블), `pwm-fan`(hwmon + thermal cooling device, `cooling-levels`), `pwm-beeper`(input의 `EV_SND`로 주파수 설정), `leds-pwm`, `pwm-vibrator`. 랩 커널에는 `pwm_bl.ko`, `pwm-fan.ko`, `pwm-beeper.ko`가 있다.

---

## 4. RTC — 전원이 꺼져도 흐르는 시계, 그리고 장비를 깨우는 알람

### 4.1 직관: Linux에는 시계가 둘 있다

1. **시스템 시계** (`CLOCK_REALTIME`): 커널이 고해상도 타이머(arm64는 generic timer)로 나노초 단위로 유지한다. `date`, `gettimeofday()`, 로그 타임스탬프가 이것이다. **전원이 꺼지면 사라진다.**
2. **RTC**: 코인셀이나 슈퍼커패시터로 버티는 저전력 칩(또는 SoC/PMIC 안의 always-on 블록). 32.768 kHz 크리스털로 **초 단위** 달력 시간을 센다. 정밀도는 보통 ±20 ppm(하루 약 1.7초) 수준이다.

둘의 관계는 단순하다: **부팅할 때 RTC → 시스템 시계로 한 번 복사**(hctosys)하고, 그 뒤로는 시스템 시계가 주인이다. NTP/PTP/GNSS가 시스템 시계를 맞추면, 커널이 주기적으로 **시스템 시계 → RTC로 되써서**(systohc) 다음 부팅을 대비한다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="100" width="170" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="105" y="126" text-anchor="middle" font-size="13">RTC (rtc0)</text>
  <text x="105" y="146" text-anchor="middle" font-size="12">코인셀 · 1 s 해상도</text>
  <rect x="300" y="100" width="190" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="395" y="126" text-anchor="middle" font-size="13">CLOCK_REALTIME</text>
  <text x="395" y="146" text-anchor="middle" font-size="12">시스템 시계 · ns 해상도</text>
  <path d="M190,115 C240,80 250,80 298,115" fill="none" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="245" y="72" text-anchor="middle" font-size="12">부팅 1회: hctosys</text>
  <path d="M298,148 C250,185 240,185 192,148" fill="none" stroke="currentColor" stroke-dasharray="5 4" marker-end="url(#ah4)"/>
  <text x="245" y="205" text-anchor="middle" font-size="12">동기화 중 약 11분마다: systohc</text>
  <rect x="540" y="40" width="130" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="605" y="62" text-anchor="middle" font-size="13">NTP (chrony)</text>
  <rect x="540" y="113" width="130" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="605" y="135" text-anchor="middle" font-size="13">PTP (ptp4l)</text>
  <rect x="540" y="186" width="130" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="605" y="208" text-anchor="middle" font-size="13">GNSS PPS</text>
  <line x1="540" y1="60" x2="492" y2="112" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="540" y1="130" x2="492" y2="130" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="540" y1="200" x2="492" y2="150" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="105" y="250" text-anchor="middle" font-size="12">hwclock -w : 유저가 직접 되쓰기</text>
  <text x="395" y="250" text-anchor="middle" font-size="12">adjtimex로 주파수·위상 보정</text>
</svg>
```

그림 7 — 두 시계의 관계. hctosys(`CONFIG_RTC_HCTOSYS`)는 부팅 중 한 번, systohc(`CONFIG_RTC_SYSTOHC`)는 커널이 "시계가 외부와 동기화됐다"(adjtimex의 `STA_UNSYNC`가 꺼짐)고 알 때만 주기적으로 RTC에 되쓴다. 시간 동기화 자체(NTP/PTP 서보, PHC)는 Q3에서 다룬다.

bare-metal 비유: SSD 펌웨어가 호스트의 Set Features(Timestamp)로 받은 시각을 RAM 카운터로 유지하다가 전원 손실 전에 NAND에 저장하던 것과 같은 구조다. 다만 RTC는 "꺼져 있는 동안에도" 센다.

### 4.2 정의: `rtc_class_ops`와 등록

```c
struct rtc_class_ops {
	int (*ioctl)(struct device *, unsigned int, unsigned long);
	int (*read_time)(struct device *, struct rtc_time *);
	int (*set_time)(struct device *, struct rtc_time *);
	int (*read_alarm)(struct device *, struct rtc_wkalrm *);
	int (*set_alarm)(struct device *, struct rtc_wkalrm *);
	int (*proc)(struct device *, struct seq_file *);
	int (*alarm_irq_enable)(struct device *, unsigned int enabled);
	int (*read_offset)(struct device *, long *offset);	/* 크리스털 보정 (ppb) */
	int (*set_offset)(struct device *, long offset);
	int (*param_get)(struct device *, struct rtc_param *param);
	int (*param_set)(struct device *, struct rtc_param *param);
};
```

드라이버 probe의 전형적인 모양 (`rtc-test.c`에서 본 그대로):

```c
rtc = devm_rtc_allocate_device(dev);
rtc->ops = &my_rtc_ops;
rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;	/* 칩이 표현할 수 있는 범위 */
rtc->range_max = RTC_TIMESTAMP_END_2099;
device_init_wakeup(dev, true);			/* 알람으로 시스템을 깨울 수 있음 */
return devm_rtc_register_device(rtc);
/* 알람 IRQ 핸들러에서: rtc_update_irq(rtc, 1, RTC_AF | RTC_IRQF); */
```

- 드라이버는 `struct rtc_time`(= `struct tm`과 같은 필드: 연도는 1900 기준, 월은 0 기준)만 다룬다. BCD 변환, 레지스터 순서, 오실레이터 정지 플래그 검사는 드라이버 몫이다.
- `range_min/max`를 주면 core가 범위 밖 시간을 거부하고, 2000~2099만 아는 칩에서 `hctosys`가 엉뚱한 세기로 해석하는 사고를 막는다.
- **알람 큐**: 유저의 알람, alarmtimer(`CLOCK_REALTIME_ALARM`/`CLOCK_BOOTTIME_ALARM` 타이머), uie 에뮬레이션은 모두 core의 timerqueue에 들어가고, **하드웨어 알람 레지스터 하나에는 가장 이른 것만** 프로그램된다. 하드웨어 알람이 하나뿐인 칩에서 여러 사용자가 공존하는 방법이다.
- 알람 기능이 없는 RTC는 core가 `RTC_FEATURE_ALARM` 비트를 끄고 알람 ioctl을 거부한다(아래 예제에서 `rtc-test.0`으로 확인).

### 4.3 예제 8 — 부팅 로그, sysfs, `/proc/driver/rtc`, hwclock

무엇을 확인하나: 부팅 중 hctosys가 일어났다는 증거와, RTC를 보는 세 가지 창구.

```sh
dmesg | grep -iE "rtc|pl031"
R=/sys/class/rtc/rtc0
echo "name=$(cat $R/name) since_epoch=$(cat $R/since_epoch) hctosys=$(cat $R/hctosys) wakeup=$(cat $R/device/power/wakeup)"
echo $(ls $R)
cat /proc/driver/rtc
hwclock -r -u -f /dev/rtc0; date -u
```

```text
[    5.490984] rtc-pl031 9010000.pl031: registered as rtc0
[    5.491611] rtc-pl031 9010000.pl031: setting system clock to 2026-10-11T23:19:11 UTC (1791760751)
name=rtc-pl031 9010000.pl031 since_epoch=1791760752 hctosys=1 wakeup=enabled
alarmtimer.0.auto date dev device hctosys max_user_freq name power range since_epoch subsystem time uevent wakealarm
rtc_time	: 23:19:12
rtc_date	: 2026-10-11
alrm_time	: 00:00:00
alrm_date	: 1970-01-01
alarm_IRQ	: no
alrm_pending	: no
update IRQ enabled	: no
periodic IRQ enabled	: no
periodic IRQ frequency	: 1
max user IRQ frequency	: 64
24hr		: yes
Sun Oct 11 23:19:12 2026  0.000000 seconds
Sun Oct 11 23:19:11 UTC 2026
```

출력에서 볼 것:

- `setting system clock to ...` 한 줄이 hctosys다. 그 시점은 PL031 probe 직후(5.49 s) — **RTC 드라이버가 늦게 뜨면(I2C 모듈 등) 그 전까지의 로그는 1970년 시간**이다. 그래서 I2C RTC를 쓰는 제품은 RTC 드라이버와 I2C 컨트롤러를 built-in으로 두거나, 유저 공간에서 `hwclock -s`를 일찍 돌린다.
- `hctosys=1`은 "이 RTC가 시스템 시계의 출처였다"는 표시다(`CONFIG_RTC_HCTOSYS_DEVICE="rtc0"`).
- `wakeup=enabled`: PL031이 시스템을 깨울 수 있는 장치로 등록됐다(§4.5).
- `alarmtimer.0.auto`: alarmtimer가 이 RTC를 "잠든 동안에도 울리는 타이머"의 하드웨어로 잡았다는 흔적이다.
- RTC는 23:19:12, 시스템 시계는 23:19:11로 1초 차이가 난다. RTC는 **초 단위**만 알고, 읽는 순간 초 경계와의 위상이 다르니 ±1 s는 정상이다. 정밀한 시각이 필요하면 RTC가 아니라 NTP/PTP로 맞춘 시스템 시계를 믿는다.

### 4.4 예제 9 — RTC ioctl로 시간 읽기·설정·알람 대기

무엇을 확인하나: `hwclock`이 내부에서 하는 일(`RTC_RD_TIME`/`RTC_SET_TIME`)과, 알람을 걸고 `read()`로 IRQ를 기다리는 방법. `RTC_WKALM_SET`은 "알람 시각 설정 + 알람 IRQ 켜기"를 한 번에 하는 ioctl이다(옛 방식은 `RTC_ALM_SET` 후 `RTC_AIE_ON`).

```c
// i9_rtc.c — RTC ioctl: 시간 읽기, (선택) 설정, N초 뒤 알람 → read()로 대기
// 사용: i9_rtc /dev/rtcN [alarm_sec] [set_epoch]
#include <fcntl.h>
#include <linux/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define DIE(m) do { perror(m); exit(1); } while (0)

static void tm2rtc(const struct tm *t, struct rtc_time *r)
{
	*r = (struct rtc_time){ t->tm_sec, t->tm_min, t->tm_hour, t->tm_mday,
		t->tm_mon, t->tm_year, 0, 0, 0 };
}
static void show(const char *tag, struct rtc_time *r)
{
	printf("%s %04d-%02d-%02d %02d:%02d:%02d\n", tag, r->tm_year + 1900,
	       r->tm_mon + 1, r->tm_mday, r->tm_hour, r->tm_min, r->tm_sec);
}

int main(int argc, char **argv)
{
	struct rtc_time rt;
	struct rtc_wkalrm wk = { 0 };
	unsigned long data;
	int fd = open(argc > 1 ? argv[1] : "/dev/rtc0", O_RDONLY);
	if (fd < 0) DIE("open");
	if (argc > 3) {				/* RTC를 지정 시각으로 설정 (UTC) */
		time_t e = atol(argv[3]);
		struct tm tm;
		gmtime_r(&e, &tm);
		tm2rtc(&tm, &rt);
		if (ioctl(fd, RTC_SET_TIME, &rt) < 0) DIE("RTC_SET_TIME");
	}
	if (ioctl(fd, RTC_RD_TIME, &rt) < 0) DIE("RTC_RD_TIME");
	show("rtc  now :", &rt);
	time_t sys = time(NULL);
	struct tm st; gmtime_r(&sys, &st);
	struct rtc_time sr; tm2rtc(&st, &sr);
	show("sys  now :", &sr);
	if (argc < 3) return 0;

	struct tm tm = { .tm_sec = rt.tm_sec, .tm_min = rt.tm_min, .tm_hour = rt.tm_hour,
		.tm_mday = rt.tm_mday, .tm_mon = rt.tm_mon, .tm_year = rt.tm_year };
	time_t al = timegm(&tm) + atoi(argv[2]);
	gmtime_r(&al, &tm);
	tm2rtc(&tm, &wk.time);
	wk.enabled = 1;				/* = RTC_ALM_SET + RTC_AIE_ON 한 번에 */
	if (ioctl(fd, RTC_WKALM_SET, &wk) < 0) DIE("RTC_WKALM_SET");
	if (ioctl(fd, RTC_WKALM_RD, &wk) < 0) DIE("RTC_WKALM_RD");
	show("alarm set:", &wk.time);
	struct timespec a, b;
	clock_gettime(CLOCK_MONOTONIC, &a);
	if (read(fd, &data, sizeof(data)) < 0) DIE("read");	/* 알람 IRQ까지 잠듦 */
	clock_gettime(CLOCK_MONOTONIC, &b);
	printf("woke: data=0x%lx (count=%lu, flags=0x%lx) after %.3f s\n", data,
	       data >> 8, data & 0xff, (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9);
	if (ioctl(fd, RTC_AIE_OFF, 0) < 0) DIE("RTC_AIE_OFF");
	return 0;
}
```

처음 짤 때 `struct rtc_time *`을 `struct tm *`로 캐스팅해서 `gmtime_r()`에 넘겼다가 고쳤다. 앞 9개 필드는 같지만 glibc의 `struct tm`은 뒤에 `tm_gmtoff`, `tm_zone`이 더 있어서 **`rtc_wkalrm` 끝을 넘어 덮어쓴다**. 필드를 복사하는 `tm2rtc()`를 따로 둔 이유다.

```sh
/i9_rtc /dev/rtc0 3                       # PL031: +3 s 알람
modprobe rtc-test; echo $(ls /sys/class/rtc)
/i9_rtc /dev/rtc1 2 1700000000            # rtc-test.0: 시간 설정 후 알람
/i9_rtc /dev/rtc2 2 1700000000            # rtc-test.1
for r in rtc0 rtc2; do for d in 1 2 3 1 2 3; do usleep 370000; echo "$r +$d: $(/i9_rtc /dev/$r $d | grep woke)"; done; done
```

```text
rtc  now : 2026-10-11 23:19:12
sys  now : 2026-10-11 23:19:11
alarm set: 2026-10-11 23:19:15
woke: data=0x1a0 (count=1, flags=0xa0) after 3.011 s
rtc0 rtc1 rtc2 rtc3
rtc  now : 2023-11-14 22:13:20
sys  now : 2026-10-11 23:19:15
RTC_WKALM_SET: Invalid argument
rtc  now : 2023-11-14 22:13:20
sys  now : 2026-10-11 23:19:15
alarm set: 2023-11-14 22:13:22
woke: data=0x1a0 (count=1, flags=0xa0) after 2.001 s
rtc0 +1: woke: data=0x1a0 (count=1, flags=0xa0) after 0.997 s
rtc0 +2: woke: data=0x1a0 (count=1, flags=0xa0) after 2.002 s
rtc0 +3: woke: data=0x1a0 (count=1, flags=0xa0) after 3.001 s
rtc0 +1: woke: data=0x1a0 (count=1, flags=0xa0) after 1.001 s
rtc0 +2: woke: data=0x1a0 (count=1, flags=0xa0) after 2.001 s
rtc0 +3: woke: data=0x1a0 (count=1, flags=0xa0) after 3.001 s
rtc2 +1: woke: data=0x1a0 (count=1, flags=0xa0) after 1.029 s
rtc2 +2: woke: data=0x1a0 (count=1, flags=0xa0) after 2.001 s
rtc2 +3: woke: data=0x1a0 (count=1, flags=0xa0) after 3.054 s
rtc2 +1: woke: data=0x1a0 (count=1, flags=0xa0) after 1.009 s
rtc2 +2: woke: data=0x1a0 (count=1, flags=0xa0) after 2.006 s
rtc2 +3: woke: data=0x1a0 (count=1, flags=0xa0) after 3.223 s
```

출력에서 볼 것:

- `read()`가 돌려준 `unsigned long`의 하위 바이트는 이벤트 종류, 상위는 횟수다. `0xa0` = `RTC_IRQF(0x80) | RTC_AF(0x20)`(알람), 횟수 1. 드라이버의 `rtc_update_irq(rtc, 1, RTC_AF | RTC_IRQF)`가 그대로 유저까지 온다. `poll()`/`select()`도 된다(A4).
- `rtc-test.0`(=rtc1)은 일부러 알람 없는 ops로 등록된 장치라 `RTC_WKALM_SET`이 `EINVAL`. 알람 없는 RTC 칩(일부 저가 I2C RTC)에서 rtcwake가 실패하는 모습 그대로다.
- `RTC_SET_TIME`으로 rtc-test 시계를 2023-11-14로 돌려도 시스템 시계(2026)는 그대로다. **RTC를 바꾼다고 시스템 시계가 바뀌지 않는다**(hctosys는 부팅 때 한 번뿐).
- 알람 대기 시간이 요청과 거의 정확히 같다(0.997~3.223 s). 실제 RTC 칩은 **초 경계에서만** 알람이 울리므로, 0.37 s 어긋난 시점에 "+1 s"를 걸면 0.63 s 뒤에 울려야 한다. QEMU의 PL031 모델은 알람을 "지금부터 N초 뒤"의 가상 타이머로 구현해서 초 경계 효과가 안 보인다(QEMU 소스 `hw/rtc/pl031.c`의 알람 계산 — 이 노트에서 직접 확인하지는 않음). rtc-test는 jiffies 타이머라 더 흔들린다. **실제 보드에서는 RTC 알람 오차가 최대 1초**라고 가정하고 설계한다.

### 4.5 예제 10 — 알람으로 s2idle에서 깨어나기 (rtcwake)

무엇을 확인하나: RTC 알람이 "잠든 시스템을 깨우는 wakeup source"로 동작하는 전체 경로.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="90" y="20" text-anchor="middle" font-size="13">유저</text>
  <text x="270" y="20" text-anchor="middle" font-size="13">rtc core · PM core</text>
  <text x="450" y="20" text-anchor="middle" font-size="13">드라이버</text>
  <text x="600" y="20" text-anchor="middle" font-size="13">RTC HW</text>
  <line x1="90" y1="28" x2="90" y2="310" stroke="currentColor" stroke-opacity="0.3"/>
  <line x1="270" y1="28" x2="270" y2="310" stroke="currentColor" stroke-opacity="0.3"/>
  <line x1="450" y1="28" x2="450" y2="310" stroke="currentColor" stroke-opacity="0.3"/>
  <line x1="600" y1="28" x2="600" y2="310" stroke="currentColor" stroke-opacity="0.3"/>
  <line x1="90" y1="50" x2="268" y2="50" stroke="#4a7bd0" marker-end="url(#ah5)"/>
  <text x="180" y="44" text-anchor="middle" font-size="12">RTC_WKALM_SET</text>
  <line x1="270" y1="74" x2="448" y2="74" stroke="#4a7bd0" marker-end="url(#ah5)"/>
  <text x="360" y="68" text-anchor="middle" font-size="12">.set_alarm (가장 이른 것)</text>
  <line x1="450" y1="96" x2="598" y2="96" stroke="#4a7bd0" marker-end="url(#ah5)"/>
  <text x="525" y="90" text-anchor="middle" font-size="12">알람 레지스터</text>
  <line x1="90" y1="126" x2="268" y2="126" stroke="#3f9a6b" marker-end="url(#ah5)"/>
  <text x="180" y="120" text-anchor="middle" font-size="12">echo freeze &gt; state</text>
  <rect x="200" y="136" width="140" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="270" y="155" text-anchor="middle" font-size="12">태스크 freeze</text>
  <text x="270" y="171" text-anchor="middle" font-size="12">장치 suspend</text>
  <text x="450" y="160" text-anchor="middle" font-size="12">enable_irq_wake()</text>
  <rect x="540" y="190" width="120" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="600" y="210" text-anchor="middle" font-size="12">알람 일치 → IRQ</text>
  <line x1="598" y1="232" x2="452" y2="232" stroke="#d0564a" marker-end="url(#ah5)"/>
  <text x="525" y="250" text-anchor="middle" font-size="12">ISR: rtc_update_irq</text>
  <line x1="450" y1="262" x2="272" y2="262" stroke="#d0564a" marker-end="url(#ah5)"/>
  <text x="360" y="256" text-anchor="middle" font-size="12">pm_wakeup_event</text>
  <line x1="270" y1="290" x2="92" y2="290" stroke="#d0564a" marker-end="url(#ah5)"/>
  <text x="180" y="284" text-anchor="middle" font-size="12">resume → read() 깨어남</text>
</svg>
```

그림 8 — RTC 알람이 시스템을 깨우는 흐름. 핵심은 세 가지다: ① 드라이버가 probe에서 `device_init_wakeup(dev, true)`로 "나는 깨울 수 있다"를 선언, ② suspend 경로에서 알람 IRQ를 wake IRQ로 설정(`enable_irq_wake()`, 보통 `.suspend` 콜백이나 `dev_pm_set_wake_irq()`), ③ IRQ가 오면 wakeup 이벤트로 기록되고 PM core가 resume한다. s2idle은 CPU를 idle에 두는 가장 얕은 잠이라 일반 IRQ로도 깨지만, `mem`(S3/deep) 같은 깊은 잠에서는 always-on 도메인에 있는 RTC만 남는다.

```sh
echo +2 > $R/wakealarm; cat $R/wakealarm; cat /proc/driver/rtc | grep -E "alarm|alrm"
echo 0 > $R/wakealarm
cat /sys/power/state; cat /sys/power/mem_sleep
date -u; T0=$(cut -d" " -f1 /proc/uptime)
rtcwake -m freeze -s 5 -d rtc0; T1=$(cut -d" " -f1 /proc/uptime); date -u
echo "uptime $T0 -> $T1"
dmesg | tail -12
cat /sys/kernel/debug/wakeup_sources | head -1; grep -i -E "rtc|pl031|9010000" /sys/kernel/debug/wakeup_sources
```

```text
1791760788
alrm_time	: 23:19:48
alrm_date	: 2026-10-11
alarm_IRQ	: yes
alrm_pending	: no
== power states
freeze mem disk
[s2idle]
== rtcwake freeze 5 s
Sun Oct 11 23:19:46 UTC 2026
wakeup from "freeze" at Sun Oct 11 23:19:52 2026
Sun Oct 11 23:19:52 UTC 2026
uptime 40.22 -> 46.26
[   40.290987] Filesystems sync: 0.000 seconds
[   40.304364] Freezing user space processes
[   40.308332] Freezing user space processes completed (elapsed 0.003 seconds)
[   40.308574] OOM killer disabled.
[   40.308622] Freezing remaining freezable tasks
[   40.309992] Freezing remaining freezable tasks completed (elapsed 0.001 seconds)
[   40.310090] printk: Suspending console(s) (use no_console_suspend to debug)
[   46.287818] OOM killer enabled.
[   46.287866] Restarting tasks: Starting
[   46.288961] Restarting tasks: Done
[   46.289217] random: crng reseeded on system resumption
[   46.290293] PM: suspend exit
name		active_count	event_count	wakeup_count	expire_count	active_since	total_time	max_time	last_change	prevent_suspend_time
rtc-test.2  	0		0		0		0		0		0		0		0		0
rtc-test.1  	8		8		0		0		0		1		0		40160		0
9010000.pl031	8		8		0		0		0		4		1		46241		0
```

출력에서 볼 것:

- sysfs `wakealarm`은 C 없이 알람을 거는 방법이다. `+2`는 "지금부터 2초 뒤", 읽으면 epoch 초가 나온다. 0을 쓰면 해제.
- virt 머신은 `freeze mem disk`를 광고하지만 `mem_sleep`은 `[s2idle]`뿐이다 — 즉 `mem`도 실제로는 s2idle이다. PSCI SYSTEM_SUSPEND가 없는 플랫폼의 전형이다.
- `rtcwake -m freeze -s 5`: 23:19:46에 시작해 23:19:52에 깨어났다(6 s). 알람은 초 단위라 1초 정도의 오차는 예상 범위다. dmesg에 freeze → resume 과정이 그대로 보인다.
- `wakeup_sources`의 `9010000.pl031` 행에 이벤트가 쌓였다. 실제 제품에서 "장비가 왜 깨어났나"를 따질 때 가장 먼저 보는 파일이다(`CONFIG_PM_SLEEP_DEBUG`를 켜면 `/sys/power/pm_wakeup_irq`로 마지막으로 깨운 IRQ도 볼 수 있다 — 이 랩 커널은 꺼져 있다).

### 4.6 실무 메모 — I2C 배터리 백업 RTC

로봇·카메라 SoC는 내장 RTC가 있어도 **SoC 전원 도메인과 함께 꺼지는** 경우가 많아, PMIC RTC나 외부 I2C RTC(NXP PCF85063/PCF2127, Micro Crystal RV-3028, Maxim DS3231 등)를 코인셀로 따로 둔다. 랩 커널에는 이들 드라이버가 모듈로 있다(`rtc-pcf85063.ko`, `rtc-rv3028.ko`, `rtc-ds1307.ko` …). DT는 이렇게 생겼다 (예시 — 이 노트에서 실행하지 않음):

```dts
&i2c3 {
	rtc@51 {
		compatible = "nxp,pcf85063a";
		reg = <0x51>;
		interrupt-parent = <&gpio5>;
		interrupts = <4 IRQ_TYPE_LEVEL_LOW>;	/* INT 핀 → 알람 깨우기 */
		quartz-load-femtofarads = <12500>;
		wakeup-source;
	};
};
```

- **오실레이터 정지 플래그**: 코인셀이 빠졌다 다시 들어오면 칩은 "시간 무효" 비트(OS/VL/PORF 등)를 세운다. 좋은 드라이버는 `.read_time`에서 이 비트를 보고 `-EINVAL`을 돌려 hctosys가 쓰레기 시간을 시스템 시계에 넣지 않게 한다. 이때 부팅 로그에는 "setting system clock" 대신 hctosys 실패가 남고 시계는 1970년이다.
- **1970년 장비**: TLS 인증서 검증 실패(유효 기간 "아직 시작 안 함"), 로그 정렬 붕괴, `make`의 "clock skew detected". 제품에서는 ① RTC 무효 시 빌드 시각이나 마지막으로 저장한 시각을 하한으로 쓰고(systemd-timesyncd가 `/var/lib/systemd/timesync/clock` 파일의 mtime으로 이렇게 한다), ② 네트워크·GNSS 동기화 전까지 시간 의존 작업을 미룬다.
- **drift 보정**: `.set_offset`을 구현한 칩은 sysfs `offset`(ppb)으로 크리스털 오차를 보정할 수 있다. 장기 무선 장비에서 쓴다.

---

## 5. watchdog — "살아 있다는 증거"를 못 내면 하드웨어가 리셋한다

### 5.1 직관: 펌웨어의 WDT kick과 무엇이 다른가

MCU 펌웨어에서 watchdog은 단순했다. 하드웨어 다운카운터를 켜고, 메인 루프가 한 바퀴 돌 때마다 kick한다. 루프가 어딘가에서 멈추면 카운터가 0이 되고 칩이 리셋된다. Don이 SSD에서 겪었듯이 어려운 건 **"어디서 kick하느냐"** 였다 — 타이머 ISR에서 kick하면 메인 루프가 죽어도 ISR은 살아 있어서 watchdog이 무용지물이 된다.

Linux에서도 하드웨어는 같다(SoC WDT, PMIC WDT, 외부 WDT 칩). 달라지는 건 **누가 kick하나**다.

- 부팅 중(유저 공간이 아직 없을 때): 커널 watchdog core가 대신 kick할 수 있다(§5.8).
- 평상시: **유저 공간 프로세스**(전용 데몬, 또는 systemd PID 1)가 `/dev/watchdog`에 주기적으로 쓴다. 이 프로세스가 스케줄되지 못하면(커널 hang, 메모리 고갈, RT 태스크 폭주) kick이 끊겨 리셋된다.
- 그 프로세스가 "시스템이 건강하다"를 무엇으로 판단하느냐가 **watchdog 전략**의 전부다(§7.1).

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" font-size="13">A. i6300esb (QEMU, timeout 5 s) — 실측 uptime</text>
  <line x1="40" y1="80" x2="660" y2="80" stroke="currentColor"/>
  <g stroke="#3f9a6b" stroke-width="2"><line x1="110" y1="60" x2="110" y2="80"/><line x1="160" y1="60" x2="160" y2="80"/><line x1="210" y1="60" x2="210" y2="80"/><line x1="310" y1="60" x2="310" y2="80"/></g>
  <text x="160" y="52" text-anchor="middle" font-size="12">KEEPALIVE x3</text>
  <text x="310" y="52" text-anchor="middle" font-size="12">close (V 없음)</text>
  <text x="310" y="100" text-anchor="middle" font-size="12">10.73 core ping</text>
  <line x1="310" y1="112" x2="565" y2="112" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah6)"/>
  <text x="437" y="128" text-anchor="middle" font-size="12">5 x 1.024 = 5.12 s</text>
  <line x1="557" y1="58" x2="557" y2="80" stroke="#888" stroke-width="2"/>
  <text x="530" y="52" text-anchor="middle" font-size="12">15.68 마지막 출력</text>
  <line x1="567" y1="62" x2="567" y2="98" stroke="#d0564a" stroke-width="3"/>
  <text x="600" y="100" font-size="12">리셋</text>
  <text x="10" y="166" font-size="13">B. i9_wdt (timeout 10 s, pretimeout 3 s) — 실측 dmesg</text>
  <line x1="40" y1="230" x2="660" y2="230" stroke="currentColor"/>
  <line x1="100" y1="205" x2="100" y2="230" stroke="#3f9a6b" stroke-width="2"/>
  <text x="100" y="198" text-anchor="middle" font-size="12">7.33 마지막 ping</text>
  <line x1="100" y1="250" x2="378" y2="250" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah6)"/>
  <text x="240" y="268" text-anchor="middle" font-size="12">timeout - pretimeout = 7 s</text>
  <line x1="380" y1="205" x2="380" y2="230" stroke="#e08a3c" stroke-width="3"/>
  <text x="380" y="198" text-anchor="middle" font-size="12">14.34 pretimeout</text>
  <line x1="500" y1="205" x2="500" y2="230" stroke="#d0564a" stroke-width="3"/>
  <text x="520" y="198" text-anchor="middle" font-size="12">17.34 bite</text>
  <line x1="382" y1="250" x2="498" y2="250" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah6)"/>
  <text x="440" y="268" text-anchor="middle" font-size="12">3 s</text>
</svg>
```

그림 9 — watchdog 타임라인 두 개(둘 다 이 노트의 실측). A: QEMU i6300esb에서 급식을 멈춘 뒤 실제 머신 리셋까지(§5.4). B: 직접 만든 가짜 watchdog에서 pretimeout 경고가 리셋 3초 전에 먼저 오는 모습(§5.7). pretimeout은 "곧 리셋된다 — 지금 진단 정보를 남겨라"라는 마지막 기회다.

### 5.2 정의: `/dev/watchdog`의 약속 (watchdog API)

`Documentation/watchdog/watchdog-api.rst`가 정한 유저 공간 규칙은 짧다.

| 동작 | 의미 |
|---|---|
| `open()` | **watchdog 시작** (이미 돌고 있으면 그대로). 한 번에 한 프로세스만 열 수 있다(두 번째 open은 `EBUSY`) |
| `write()` 아무 데이터 | keepalive(ping) |
| `ioctl(WDIOC_KEEPALIVE)` | keepalive |
| `write()`에 문자 `'V'` 포함 후 `close()` | **magic close** — 드라이버가 `WDIOF_MAGICCLOSE`를 광고하면 이것만이 정지 방법 |
| `'V'` 없이 `close()` (또는 프로세스 죽음) | 정지하지 않음 → core가 "watchdog did not stop!" 출력, 계속 카운트다운 |
| `WDIOC_SETTIMEOUT` / `GETTIMEOUT` | 타임아웃(초) 설정·조회. 설정하면 core가 ping도 한 번 한다 |
| `WDIOC_GETTIMELEFT` | 리셋까지 남은 초 (드라이버가 `.get_timeleft` 구현 시) |
| `WDIOC_SETPRETIMEOUT` / `GETPRETIMEOUT` | 리셋 몇 초 전에 경고할지 (`WDIOF_PRETIMEOUT` 시) |
| `WDIOC_GETSUPPORT` | `struct watchdog_info`: identity, options 비트 |
| `WDIOC_GETBOOTSTATUS` | 지난번 리셋 원인 (`WDIOF_CARDRESET` = watchdog이 리셋했음) |
| `nowayout` (모듈 인자 / `CONFIG_WATCHDOG_NOWAYOUT`) | 한 번 시작되면 **어떤 방법으로도** 정지 불가 |

드라이버 쪽 구조체 (`include/linux/watchdog.h`, 6.18):

```c
struct watchdog_ops {
	struct module *owner;
	int (*start)(struct watchdog_device *);		/* 필수 */
	int (*stop)(struct watchdog_device *);		/* 못 멈추는 HW면 생략 → core가 대신 ping */
	int (*ping)(struct watchdog_device *);		/* 없으면 core가 start로 ping */
	unsigned int (*status)(struct watchdog_device *);
	int (*set_timeout)(struct watchdog_device *, unsigned int);
	int (*set_pretimeout)(struct watchdog_device *, unsigned int);
	unsigned int (*get_timeleft)(struct watchdog_device *);
	int (*restart)(struct watchdog_device *, unsigned long, void *);	/* reboot 수단으로도 등록 */
	long (*ioctl)(struct watchdog_device *, unsigned int, unsigned long);
};

struct watchdog_device {	/* 일부 */
	const struct watchdog_info *info;
	const struct watchdog_ops *ops;
	unsigned int bootstatus, timeout, pretimeout, min_timeout, max_timeout;
	unsigned int min_hw_heartbeat_ms;	/* 너무 자주 ping하면 안 되는 HW */
	unsigned int max_hw_heartbeat_ms;	/* HW 최대 주기 — 더 긴 timeout은 core가 보충 ping */
	unsigned long status;			/* WDOG_ACTIVE, WDOG_NO_WAY_OUT, WDOG_HW_RUNNING ... */
};
```

등록은 `watchdog_init_timeout()`(DT `timeout-sec`이나 모듈 인자 반영) → `watchdog_set_nowayout()` → `devm_watchdog_register_device()`. 드라이버가 할 일은 "레지스터에 카운터 값 쓰기, reload 쓰기, enable 비트" 정도다.

### 5.3 예제 11 — 급식기(feeder) 프로그램

무엇을 확인하나: 위 표의 ioctl을 전부 써 보는 작은 도구. 지정한 횟수만큼 ping한 뒤 ① `'V'`로 magic close 하거나, ② ping을 멈추고(행 흉내) 남은 시간을 1초마다 읽다가 `'V'` 없이 닫는다. 이후 예제에서 계속 쓴다.

```c
// i9_wdog.c — watchdog 급식기: 설정 → N번 ping → 굶기면서 남은 시간 관찰
// 사용: i9_wdog <dev> <timeout> <pings> <interval_ms> <starve_s> [V] [pre]
#include <fcntl.h>
#include <linux/watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static double t0;
static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9 - t0;
}
static void left(int fd)
{
	int l;
	if (ioctl(fd, WDIOC_GETTIMELEFT, &l) == 0) printf("  t=%6.2f left=%d\n", now(), l);
	else printf("  t=%6.2f GETTIMELEFT: %m\n", now());
}

int main(int argc, char **argv)
{
	if (argc < 6) { fprintf(stderr, "usage\n"); exit(1); }
	struct watchdog_info wi;
	int to = atoi(argv[2]), n = atoi(argv[3]), iv = atoi(argv[4]), starve = atoi(argv[5]);
	int bs, pre = argc > 7 ? atoi(argv[7]) : 0;
	t0 = now();
	int fd = open(argv[1], O_WRONLY);	/* open = 시작 (이미 돌고 있으면 유지) */
	if (fd < 0) { perror("open"); exit(1); }
	if (ioctl(fd, WDIOC_GETSUPPORT, &wi) < 0) { perror("GETSUPPORT"); exit(1); }
	ioctl(fd, WDIOC_GETBOOTSTATUS, &bs);
	printf("identity='%s' options=0x%x bootstatus=0x%x\n", wi.identity, wi.options, bs);
	if (ioctl(fd, WDIOC_SETTIMEOUT, &to) < 0) perror("SETTIMEOUT");
	printf("timeout -> %d s\n", to);
	if (pre && ioctl(fd, WDIOC_SETPRETIMEOUT, &pre) < 0) perror("SETPRETIMEOUT");
	if (pre) { ioctl(fd, WDIOC_GETPRETIMEOUT, &pre); printf("pretimeout -> %d s\n", pre); }
	for (int i = 0; i < n; i++) {		/* 정상 동작: 주기적으로 먹이 주기 */
		usleep(iv * 1000);
		if (ioctl(fd, WDIOC_KEEPALIVE, 0) < 0) perror("KEEPALIVE");
		if (i == n - 1) { printf("last ping"); left(fd); }
	}
	if (argc > 6 && argv[6][0] == 'V') {	/* magic close: 'V' 쓰고 닫기 = 정지 */
		if (write(fd, "V", 1) != 1) perror("write V");
		close(fd);
		printf("magic close done at t=%.2f\n", now());
		return 0;
	}
	printf("starving...\n");		/* 행(hang) 흉내: 더 이상 ping 안 함 */
	for (int s = 0; s < starve; s++) { left(fd); fflush(stdout); sleep(1); }
	close(fd);				/* 'V' 없이 닫기 */
	printf("closed without V at t=%.2f\n", now());
	fflush(stdout);
	sleep(starve);
	return 0;
}
```

리셋 시점을 재려고, VM 쪽에서 QEMU 출력 각 줄에 **호스트 경과 시간**을 붙이는 작은 래퍼(`tsrun.py`)로 감쌌다. 두 번째 부팅 배너(또는 지정한 문자열의 두 번째 등장)를 보면 QEMU를 죽인다.

```python
#!/usr/bin/env python3
# tsrun.py <stop_regex|-> <cmd...> : 출력 각 줄 앞에 호스트 경과 시간(초)을 붙인다
import re, subprocess, sys, time
stop = None if sys.argv[1] == '-' else re.compile(sys.argv[1])
p = subprocess.Popen(sys.argv[2:], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
t0 = time.monotonic(); seen = 0
for line in p.stdout:
    print(f"[host {time.monotonic()-t0:7.2f}] {line}", end='', flush=True)
    if stop and stop.search(line):
        seen += 1
        if seen >= 2:
            p.kill(); print(f"[host {time.monotonic()-t0:7.2f}] (killed after 2nd match)"); break
p.wait()
print(f"[host {time.monotonic()-t0:7.2f}] qemu exit code {p.returncode}")
```

### 5.4 예제 12 — i6300esb: 급식을 멈추면 QEMU가 정말 리셋한다

무엇을 확인하나: 하드웨어(에뮬레이션) watchdog이 실제로 머신을 리셋하는가, 리셋까지 얼마나 걸리나, 다음 부팅에서 "watchdog 때문에 리셋됐다"를 알 수 있나. QEMU는 `-device i6300esb`만 주면 PCI 장치로 붙고, 타임아웃 동작은 기본이 `reset`이다(`-action watchdog=reset|shutdown|poweroff|pause|debug|none|inject-nmi`로 바꿀 수 있다 — 이 노트에서는 기본값만 사용).

```sh
# VM 호스트
python3 tsrun.py "bootstatus" sh boot.sh esb -device i6300esb
# 게스트 i9.sh esb
modprobe i6300esb; dmesg | grep -iE "i6300|esb|watchdog"
echo $(ls /sys/class/watchdog/watchdog0)
/i9_wdog /dev/watchdog0 5 3 1000 2
while true; do usleep 250000; echo "alive uptime $(cut -d' ' -f1 /proc/uptime)"; done
```

```text
[host    8.78] [    5.640041] i6300ESB timer 0000:00:02.0: enabling device (0000 -> 0002)
[host    8.78] [    5.642462] i6300ESB timer 0000:00:02.0: initialized. heartbeat=30 sec (nowayout=0)
[host    8.79] dev power subsystem uevent
[host    8.82] identity='i6300ESB timer' options=0x8180 bootstatus=0x0
[host    8.82] timeout -> 5 s
[host   11.82] last ping  t=  3.01 GETTIMELEFT: Operation not supported
[host   11.82] starving...
[host   11.83]   t=  3.01 GETTIMELEFT: Operation not supported
[host   12.83]   t=  4.01 GETTIMELEFT: Operation not supported
[host   13.83] closed without V at t=5.01
[host   13.83] [   10.727932] watchdog: watchdog0: watchdog did not stop!
[host   16.10] alive uptime 12.97
[host   16.38] alive uptime 13.25
...
[host   18.54] alive uptime 15.41
[host   18.81] alive uptime 15.68
[host   26.03] lab initramfs: 6.18.55-sub up 6.80s
[host   26.24] [    7.027056] i6300ESB timer 0000:00:02.0: initialized. heartbeat=30 sec (nowayout=0)
[host   26.26] dev power subsystem uevent
[host   26.29] identity='i6300ESB timer' options=0x8180 bootstatus=0x20
[host   26.29] (killed after 2nd match)
```

(`alive` 줄 중간 7줄은 `...`로 생략했다. 커널 명령줄 echo 줄도 뺐다.)

출력에서 볼 것:

- `options=0x8180` = `WDIOF_KEEPALIVEPING(0x8000) | WDIOF_MAGICCLOSE(0x100) | WDIOF_SETTIMEOUT(0x80)`. `GETTIMELEFT`는 i6300esb 드라이버가 구현하지 않아 `EOPNOTSUPP` — **남은 시간을 못 읽는 HW watchdog이 흔하다.**
- `/sys/class/watchdog/watchdog0`에 속성 파일이 없다. `CONFIG_WATCHDOG_SYSFS`가 꺼져 있어서다. 켜면 `timeout`, `timeleft`, `state`, `bootstatus`, `nowayout`, `pretimeout_governor` 등이 생긴다.
- **'V' 없이 닫는 순간 core가 한 번 더 ping한다**(`watchdog_release()`가 "did not stop!"을 찍고 `watchdog_ping()`). 그래서 카운트다운은 close 시점(uptime 10.73)부터 다시 시작한다. 마지막 출력이 15.68, 다음 출력 예정이 15.95였으니 리셋은 그 사이 — 드라이버 주석대로 i6300esb는 `timeout << 9`를 1 kHz로 세는 2단 타이머라 실제 주기는 5 × 1.024 = 5.12 s, 즉 15.85 근처다. 측정과 맞는다.
- 리셋 후 다시 부팅한 커널에서 **`bootstatus=0x20`(`WDIOF_CARDRESET`)**. i6300esb probe가 reload 레지스터의 timeout 비트를 읽어 "지난번 리셋은 watchdog이 했다"를 알려 준다. 제품에서는 이 값을 부팅 때 로그·텔레메트리로 올려 **현장에서 watchdog 리셋이 몇 번 났는지** 센다.

### 5.5 예제 13 — magic close vs nowayout

무엇을 확인하나: `'V'`를 쓰고 닫으면 정말 멈추나, `nowayout=1`이면 어떻게 되나.

```sh
python3 tsrun.py - sh boot.sh esbV -device i6300esb                 # nowayout=0
python3 tsrun.py "lab initramfs" sh boot.sh esbV~nowayout=1 -device i6300esb
# 게스트: modprobe i6300esb $2; /i9_wdog /dev/watchdog0 5 3 1000 0 V; dmesg | tail -3; 12초 대기
```

```text
[host    4.87] identity='i6300ESB timer' options=0x8180 bootstatus=0x0
[host    4.87] timeout -> 5 s
[host    7.88] last ping  t=  3.01 GETTIMELEFT: Operation not supported
[host    7.88] magic close done at t=3.01
[host    7.91] [    4.566301] i6300ESB timer 0000:00:02.0: initialized. heartbeat=30 sec (nowayout=0)
[host   20.02] still alive after 12 s (uptime 19.67)
[host   20.04] [   19.749825] reboot: Power down
```

```text
[host    5.42] identity='i6300ESB timer' options=0x8180 bootstatus=0x0
[host    5.42] timeout -> 5 s
[host    8.42] last ping  t=  3.01 GETTIMELEFT: Operation not supported
[host    8.43] magic close done at t=3.01
[host    8.43] [    8.241838] watchdog: watchdog0: watchdog did not stop!
[host    8.44] [    5.219924] i6300ESB timer 0000:00:02.0: initialized. heartbeat=30 sec (nowayout=1)
[host    8.44] [    8.241783] watchdog: watchdog0: nowayout prevents watchdog being stopped!
[host    8.44] [    8.241838] watchdog: watchdog0: watchdog did not stop!
[host   21.15] lab initramfs: 6.18.55-sub up 7.33s
[host   21.15] (killed after 2nd match)
```

출력에서 볼 것:

- `nowayout=0`: magic close 후 12초가 지나도 살아 있다 — watchdog이 멈췄다. 업데이트나 유지보수 때 데몬을 정상 종료하는 경로가 이것이다.
- `nowayout=1`: 똑같이 `'V'`를 썼는데 "nowayout prevents watchdog being stopped!" → 약 5초 뒤 리셋(두 번째 부팅 배너). **제품 빌드는 nowayout을 켠다**: 데몬이 버그로 `'V'`를 쓰고 죽거나, 누군가 `echo V > /dev/watchdog`으로 watchdog을 끄는 일을 막는다. 대신 nowayout에서는 watchdog 데몬을 재시작할 때 timeout 안에 다시 열어야 한다.

### 5.6 예제 14 — softdog, 그리고 데몬이 죽으면

무엇을 확인하나: 하드웨어가 없을 때 쓰는 softdog(커널 hrtimer로 흉내)과, 유저 공간 watchdog 데몬(BusyBox `watchdog`)이 SIGKILL로 죽으면 무슨 일이 일어나는가.

```sh
# softdog: modprobe softdog soft_margin=4; /i9_wdog /dev/watchdog0 4 2 1000 2; 이후 1초마다 alive
```

```text
[host    6.54] [    6.222853] softdog: initialized. soft_noboot=0 soft_margin=4 sec soft_panic=0 (nowayout=0)
[host    6.54] [    6.222903] softdog:              soft_reboot_cmd=<not set> soft_active_on_boot=0
[host    6.56] identity='Software Watchdog' options=0x8180 bootstatus=0x0
[host    6.56] timeout -> 4 s
[host    8.56] last ping  t=  2.01 GETTIMELEFT: Operation not supported
[host    8.56] starving...
[host    8.56]   t=  2.01 GETTIMELEFT: Operation not supported
[host    9.57]   t=  3.01 GETTIMELEFT: Operation not supported
[host   10.57] closed without V at t=4.02
[host   10.57] [   10.288181] watchdog: watchdog0: watchdog did not stop!
[host   13.59] alive +1s
[host   19.05] lab initramfs: 6.18.55-sub up 4.17s
```

softdog도 시간이 되자 머신을 재부팅했다(`emergency_restart()`). softdog의 "Initiating system reboot" 메시지는 콘솔에 찍히지 않았다 — 즉시 재시작이라 콘솔 출력이 비워지기 전에 리셋된 것으로 보인다. **softdog의 한계**: 타이머가 커널 안에 있으므로 커널 자체가 멈추면(IRQ off 상태의 데드락) softdog도 같이 죽는다. 유저 공간 행만 잡는다. 진짜 제품에는 하드웨어 watchdog이 필요하다.

```sh
# BusyBox watchdog 데몬: -T 6(리셋 타임아웃) -t 1(1초마다 ping)
modprobe i6300esb
watchdog -T 6 -t 1 /dev/watchdog0; sleep 1; ps | grep "[w]atchdog -T"
sleep 8; echo "after 8 s with daemon: alive (uptime $(cut -d' ' -f1 /proc/uptime))"
killall -9 watchdog; echo "daemon SIGKILLed at uptime $(cut -d' ' -f1 /proc/uptime)"
while true; do usleep 500000; echo "alive uptime $(cut -d' ' -f1 /proc/uptime)"; done
```

```text
[host    8.15]    67 0         0:00 watchdog -T 6 -t 1 /dev/watchdog0
[host   16.17] after 8 s with daemon: alive (uptime 13.14)
[host   16.20] [   13.232169] watchdog: watchdog0: watchdog did not stop!
[host   16.21] daemon SIGKILLed at uptime 13.18
[host   16.73] alive uptime 13.70
...
[host   21.44] alive uptime 18.41
[host   21.97] alive uptime 18.94
[host   26.83] lab initramfs: 6.18.55-sub up 4.33s
```

(`alive` 줄 중간 8줄은 `...`로 생략했다. 위의 softdog 출력에서는 커널 명령줄 줄을 뺐다.)

출력에서 볼 것: 데몬이 살아 있는 8초 동안은 아무 일도 없다. SIGKILL로 죽자 커널이 fd를 닫으면서 "did not stop!" — `'V'`가 없으니 계속 돈다. 6 × 1.024 ≈ 6.1 s 뒤(13.23 + 6.14 ≈ 19.4) 리셋됐고 마지막 출력은 18.94였다. **"watchdog 데몬이 죽으면 장비가 리셋된다"는 의도된 동작**이다. 그래서 데몬은 작고 단순해야 하고, 죽을 이유(OOM killer의 표적 등)를 없애야 한다(`OOMScoreAdjust=-1000`, 메모리 `mlockall()`).

### 5.7 예제 15 — 내 watchdog 드라이버: get_timeleft와 pretimeout

무엇을 확인하나: `watchdog_ops`를 직접 채우면 core가 무엇을 해 주는지. 하드웨어 대신 hrtimer 두 개로 "bark(pretimeout)"와 "bite(리셋)"를 흉내 내고, 진짜 리셋 대신 로그를 남긴다. `max_hw_ms`·`hw_running`은 §5.8에서 쓴다. 소스는 두 부분이다.

```c
// i9_wdt.c (1/2) — 가짜 watchdog: get_timeleft, pretimeout, "부팅 때 이미 켜짐"
#include <linux/module.h>
#include <linux/watchdog.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

static bool hw_running;
module_param(hw_running, bool, 0444);
static unsigned int max_hw_ms;			/* 0이면 HW 한계 없음 */
module_param(max_hw_ms, uint, 0444);

static struct watchdog_device wdd;
static ktime_t deadline;
static struct hrtimer bark, bite;

static enum hrtimer_restart bark_fn(struct hrtimer *t)
{
	watchdog_notify_pretimeout(&wdd);	/* GOV 없으면 pr_alert 한 줄 */
	return HRTIMER_NORESTART;
}
static enum hrtimer_restart bite_fn(struct hrtimer *t)
{
	pr_emerg("i9_wdt: BITE — 실제 HW라면 여기서 SoC 리셋\n");
	return HRTIMER_NORESTART;
}

static int i9_ping(struct watchdog_device *w)
{
	static unsigned int n;
	unsigned int hw_t = max_hw_ms ? max_hw_ms / 1000 : w->timeout;

	deadline = ktime_add_ms(ktime_get(), hw_t * 1000);
	hrtimer_start(&bite, ms_to_ktime(hw_t * 1000), HRTIMER_MODE_REL);
	if (w->pretimeout)
		hrtimer_start(&bark, ms_to_ktime((hw_t - w->pretimeout) * 1000),
			      HRTIMER_MODE_REL);
	if (++n <= 6 || n % 10 == 0)
		pr_info("i9_wdt: ping #%u by %s\n", n, current->comm);
	return 0;
}
static int i9_stop(struct watchdog_device *w)
{
	hrtimer_cancel(&bite);
	hrtimer_cancel(&bark);
	pr_info("i9_wdt: stop\n");
	return 0;
}
static unsigned int i9_timeleft(struct watchdog_device *w)
{
	s64 ms = ktime_ms_delta(deadline, ktime_get());

	return ms > 0 ? (unsigned int)(ms / 1000) : 0;
}
```

```c
// i9_wdt.c (2/2) — info/ops와 등록
static const struct watchdog_info i9_info = {
	.identity = "i9 fake wdt",
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE |
		   WDIOF_PRETIMEOUT,
};
static const struct watchdog_ops i9_ops = {
	.owner = THIS_MODULE, .start = i9_ping, .stop = i9_stop,
	.ping = i9_ping, .get_timeleft = i9_timeleft,
};

static int __init i9_init(void)
{
	hrtimer_setup(&bark, bark_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer_setup(&bite, bite_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	wdd.info = &i9_info;
	wdd.ops = &i9_ops;
	wdd.timeout = 10;
	wdd.min_timeout = 1;
	wdd.max_timeout = 60;
	wdd.max_hw_heartbeat_ms = max_hw_ms;
	if (hw_running) {			/* 부트로더가 켜 놓고 넘겨줬다고 가정 */
		i9_ping(&wdd);
		set_bit(WDOG_HW_RUNNING, &wdd.status);
	}
	return watchdog_register_device(&wdd);
}
static void __exit i9_exit(void)
{
	watchdog_unregister_device(&wdd);
	hrtimer_cancel(&bite);
	hrtimer_cancel(&bark);
}
module_init(i9_init);
module_exit(i9_exit);
MODULE_LICENSE("GPL");
```

`.set_timeout`·`.set_pretimeout`을 구현하지 않았다. 이 경우 core가 값 검증 후 `wdd->timeout`·`wdd->pretimeout`만 바꾼다 — "설정은 다음 ping 때 반영"되는 하드웨어라면 이걸로 충분하다.

```sh
insmod /i9_wdt.ko; echo "params: $(ls /sys/module/watchdog/parameters/)"
/i9_wdog /dev/watchdog0 10 3 1000 12 x 3      # timeout 10, ping 3번, 12초 굶김, pretimeout 3
dmesg | grep -E "i9_wdt|watchdog"
```

```text
[host    4.62] params: handle_boot_enabled
[host    4.62] open_timeout
[host    4.62] stop_on_reboot
[host    4.63] identity='i9 fake wdt' options=0x8380 bootstatus=0x0
[host    4.63] timeout -> 10 s
[host    4.63] pretimeout -> 3 s
[host    7.66] last ping  t=  3.03 left=9
[host    7.66] starving...
[host    7.66]   t=  3.03 left=9
[host    8.68]   t=  4.05 left=8
[host   10.30]   t=  5.05 left=7
[host   10.69]   t=  6.06 left=6
[host   11.70]   t=  7.07 left=5
[host   12.71]   t=  8.08 left=4
[host   13.72]   t=  9.09 left=3
[host   14.67] [   14.337405] watchdog0: pretimeout event
[host   14.74]   t= 10.11 left=2
[host   15.74]   t= 11.11 left=1
[host   16.75]   t= 12.12 left=0
[host   17.67] [   17.339258] i9_wdt: BITE — 실제 HW라면 여기서 SoC 리셋
[host   17.76]   t= 13.13 left=0
[host   18.78]   t= 14.15 left=0
[host   19.78] closed without V at t=15.15
[host   19.79] [   19.454292] watchdog: watchdog0: watchdog did not stop!
[host   26.79] [   26.463497] watchdog0: pretimeout event
[host   29.79] [   29.454971] i9_wdt: BITE — 실제 HW라면 여기서 SoC 리셋
[host   31.83] [    4.300239] i9_wdt: ping #1 by i9_wdog
[host   31.83] [    4.302650] i9_wdt: ping #2 by i9_wdog
[host   31.83] [    5.305453] i9_wdt: ping #3 by i9_wdog
[host   31.83] [    6.316073] i9_wdt: ping #4 by i9_wdog
[host   31.83] [    7.328857] i9_wdt: ping #5 by i9_wdog
[host   31.83] [   14.337405] watchdog0: pretimeout event
[host   31.83] [   17.339258] i9_wdt: BITE — 실제 HW라면 여기서 SoC 리셋
[host   31.83] [   19.454292] watchdog: watchdog0: watchdog did not stop!
[host   31.83] [   19.454396] i9_wdt: ping #6 by i9_wdog
```

(마지막 dmesg 덤프에서 앞부분과 중복되는 줄 일부와 커널 명령줄 줄은 뺐다.)

```svg
<svg viewBox="0 0 640 340" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="280" x2="600" y2="280" stroke="currentColor"/><line x1="60" y1="280" x2="60" y2="40" stroke="currentColor"/>
<line x1="60.0" y1="280" x2="60.0" y2="285" stroke="currentColor"/><text x="60.0" y="299" text-anchor="middle" font-size="12">0</text>
<line x1="127.5" y1="280" x2="127.5" y2="285" stroke="currentColor"/><text x="127.5" y="299" text-anchor="middle" font-size="12">2</text>
<line x1="195.0" y1="280" x2="195.0" y2="285" stroke="currentColor"/><text x="195.0" y="299" text-anchor="middle" font-size="12">4</text>
<line x1="262.5" y1="280" x2="262.5" y2="285" stroke="currentColor"/><text x="262.5" y="299" text-anchor="middle" font-size="12">6</text>
<line x1="330.0" y1="280" x2="330.0" y2="285" stroke="currentColor"/><text x="330.0" y="299" text-anchor="middle" font-size="12">8</text>
<line x1="397.5" y1="280" x2="397.5" y2="285" stroke="currentColor"/><text x="397.5" y="299" text-anchor="middle" font-size="12">10</text>
<line x1="465.0" y1="280" x2="465.0" y2="285" stroke="currentColor"/><text x="465.0" y="299" text-anchor="middle" font-size="12">12</text>
<line x1="532.5" y1="280" x2="532.5" y2="285" stroke="currentColor"/><text x="532.5" y="299" text-anchor="middle" font-size="12">14</text>
<line x1="600.0" y1="280" x2="600.0" y2="285" stroke="currentColor"/><text x="600.0" y="299" text-anchor="middle" font-size="12">16</text>
<text x="52" y="284.0" text-anchor="end" font-size="12">0</text><line x1="60" y1="280.0" x2="600" y2="280.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="52" y="236.0" text-anchor="end" font-size="12">2</text><line x1="60" y1="232.0" x2="600" y2="232.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="52" y="188.0" text-anchor="end" font-size="12">4</text><line x1="60" y1="184.0" x2="600" y2="184.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="52" y="140.0" text-anchor="end" font-size="12">6</text><line x1="60" y1="136.0" x2="600" y2="136.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="52" y="92.0" text-anchor="end" font-size="12">8</text><line x1="60" y1="88.0" x2="600" y2="88.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="52" y="44.0" text-anchor="end" font-size="12">10</text><line x1="60" y1="40.0" x2="600" y2="40.0" stroke="currentColor" stroke-opacity="0.12"/>
<text x="330.0" y="320" text-anchor="middle" font-size="13">급식기 시작 후 시간 (s)</text>
<text x="16" y="160.0" text-anchor="middle" font-size="13" transform="rotate(-90 16 160.0)">GETTIMELEFT (s)</text>
<line x1="162.3" y1="40.0" x2="499.8" y2="280.0" stroke="#888" stroke-dasharray="5 4"/>
<line x1="93.8" y1="280" x2="93.8" y2="40.0" stroke="#3f9a6b" stroke-opacity="0.6"/>
<line x1="127.5" y1="280" x2="127.5" y2="40.0" stroke="#3f9a6b" stroke-opacity="0.6"/>
<line x1="162.3" y1="280" x2="162.3" y2="40.0" stroke="#3f9a6b" stroke-opacity="0.6"/>
<text x="127.5" y="32.0" text-anchor="middle" font-size="12">ping 3회</text>
<line x1="398.5" y1="280" x2="398.5" y2="40" stroke="#e08a3c" stroke-width="2"/><text x="394.5" y="52" text-anchor="end" font-size="12">pretimeout</text>
<line x1="499.8" y1="280" x2="499.8" y2="40" stroke="#d0564a" stroke-width="2"/><text x="503.8" y="52" font-size="12">bite (리셋)</text>
<polyline points="162.3,64.0 196.7,88.0 230.4,112.0 264.5,136.0 298.6,160.0 332.7,184.0 366.8,208.0 401.2,232.0 435.0,256.0 469.0,280.0 503.1,280.0 537.6,280.0" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="162.3" cy="64.0" r="4" fill="#4a7bd0"/>
<circle cx="196.7" cy="88.0" r="4" fill="#4a7bd0"/>
<circle cx="230.4" cy="112.0" r="4" fill="#4a7bd0"/>
<circle cx="264.5" cy="136.0" r="4" fill="#4a7bd0"/>
<circle cx="298.6" cy="160.0" r="4" fill="#4a7bd0"/>
<circle cx="332.7" cy="184.0" r="4" fill="#4a7bd0"/>
<circle cx="366.8" cy="208.0" r="4" fill="#4a7bd0"/>
<circle cx="401.2" cy="232.0" r="4" fill="#4a7bd0"/>
<circle cx="435.0" cy="256.0" r="4" fill="#4a7bd0"/>
<circle cx="469.0" cy="280.0" r="4" fill="#4a7bd0"/>
<circle cx="503.1" cy="280.0" r="4" fill="#4a7bd0"/>
<circle cx="537.6" cy="280.0" r="4" fill="#4a7bd0"/>
</svg>
```

그림 10 — `WDIOC_GETTIMELEFT`로 읽은 카운트다운(파랑 점)과 이상선(점선). 초록 세로선은 1초 간격 ping, 주황은 pretimeout 이벤트, 빨강은 bite. 남은 시간은 드라이버가 **초 단위로 내림**해서 돌려주므로 이상선보다 항상 아래에 있다.

출력에서 볼 것:

- `options=0x8380`에 `WDIOF_PRETIMEOUT(0x200)`이 더해졌다. `SETPRETIMEOUT`이 받아들여진 이유다(이 비트가 없으면 `EOPNOTSUPP`).
- **ping 횟수**: #1은 `open`(core가 `.start` 호출), #2는 `WDIOC_SETTIMEOUT`(core가 설정 후 ping), #3~#5는 KEEPALIVE 세 번, #6은 'V' 없는 close 때 core의 ping이다.
- 마지막 ping(7.33) 후 정확히 7.0 s 뒤 pretimeout, 10.0 s 뒤 bite. 이 커널은 `CONFIG_WATCHDOG_PRETIMEOUT_GOV`가 꺼져 있어 `watchdog_notify_pretimeout()`이 `pr_alert` 한 줄만 찍는다. governor를 켜면 `pretimeout_governor` sysfs로 **`panic`**(pretimeout에서 커널 panic → kdump/pstore로 크래시 덤프를 남김)이나 **`noop`**(로그만)을 고른다. 실제 하드웨어에서는 pretimeout이 보통 **watchdog의 1단계 IRQ**(SoC WDT의 "interrupt then reset" 모드)로 구현된다.
- 'V' 없이 닫자 core가 ping하고 카운트다운이 다시 시작 — 그 뒤로도 7 s/10 s 간격이 그대로 반복됐다(26.46 / 29.45).
- `/sys/module/watchdog/parameters/`에 `handle_boot_enabled`, `open_timeout`, `stop_on_reboot` — watchdog core의 커널 인자 세 개다(`watchdog.open_timeout=5`처럼 쓴다).

### 5.8 예제 16 — 부트로더가 켜 둔 watchdog을 커널이 이어받기

무엇을 확인하나: 제품에서는 **부트로더(U-Boot, 심지어 BootROM)가 watchdog을 켠 채로 커널에 넘긴다.** 커널 부팅이나 initramfs에서 멈추면 그것도 잡아야 하기 때문이다. 그런데 유저 공간 데몬이 `/dev/watchdog`을 열기 전까지는 누가 ping하나? 드라이버가 probe에서 "이미 돌고 있음"(`WDOG_HW_RUNNING`)을 알리면 core가 대신 ping한다. `max_hw_ms=4000`은 "하드웨어 최대 주기가 4초"라는 설정이다.

```sh
insmod /i9_wdt.ko hw_running=1 max_hw_ms=4000
cat /sys/module/watchdog/parameters/handle_boot_enabled /sys/module/watchdog/parameters/open_timeout
sleep 9; dmesg | grep i9_wdt
/i9_wdog /dev/watchdog0 10 2 1000 0 V; dmesg | grep i9_wdt | tail -4
# 두 번째 부팅: 커널 인자 watchdog.open_timeout=5, 아무도 열지 않음
insmod /i9_wdt.ko hw_running=1 max_hw_ms=4000; sleep 12; dmesg | grep i9_wdt
```

```text
core: handle_boot_enabled=Y open_timeout=0
[    5.194376] i9_wdt: loading out-of-tree module taints kernel.
[    5.197667] i9_wdt: ping #1 by insmod
[    5.199265] i9_wdt: ping #2 by watchdogd
[    7.201794] i9_wdt: ping #3 by watchdogd
[    9.202983] i9_wdt: ping #4 by watchdogd
[   11.203784] i9_wdt: ping #5 by watchdogd
[   13.204694] i9_wdt: ping #6 by watchdogd
== user opens
identity='i9 fake wdt' options=0x8380 bootstatus=0x0
timeout -> 10 s
last ping  t=  2.00 left=3
magic close done at t=2.01
[   16.273664] i9_wdt: ping #10 by i9_wdog
[   16.275460] i9_wdt: stop
```

```text
open_timeout=5
[    4.361278] i9_wdt: loading out-of-tree module taints kernel.
[    4.364799] i9_wdt: ping #1 by insmod
[    4.366305] i9_wdt: ping #2 by watchdogd
[    5.367314] i9_wdt: ping #3 by watchdogd
[    9.367783] i9_wdt: BITE — 실제 HW라면 여기서 SoC 리셋
```

출력에서 볼 것:

- **`watchdogd`** 라는 커널 스레드(core의 kthread_worker, RT 우선순위)가 2초(= 하드웨어 최대 4초의 절반)마다 ping했다. `handle_boot_enabled=Y`(`CONFIG_WATCHDOG_HANDLE_BOOT_ENABLED`)가 이 동작을 켠다. 끄면(`watchdog.handle_boot_enabled=0`) 커널은 손대지 않고, 유저 공간이 하드웨어 주기 안에 열지 못하면 리셋된다.
- 유저가 열고 timeout을 10 s로 설정한 뒤 `left=3`이 나왔다. 하드웨어 한계(4 s)보다 긴 timeout을 요청하면 **core가 하드웨어 주기 안에서 보충 ping을 해 주며 "10초"를 가상으로 만든다.** `GETTIMELEFT`는 하드웨어 카운터 기준이라 4 s 창 안의 값이 보인다. 제품 문서에 "timeout은 하드웨어 최대값보다 길게 잡아도 되지만 커널이 살아 있어야 보충된다"고 적어 둘 일이다.
- **`open_timeout=5`**: 아무도 열지 않으면 core는 등록 후 5초에 리셋이 나도록 **마지막 보충 ping을 계산해서** 멈춘다. 등록 4.36 → 마지막 ping 5.37 → 그로부터 하드웨어 4 s 뒤인 9.37에 bite. 5.37 = 9.37 − 4(`watchdog_next_keepalive()`가 `open_deadline − hw_heartbeat`로 계산). "rootfs 마운트 실패, systemd가 못 뜸" 같은 부팅 실패를 **유한 시간 안에** 리셋으로 바꾸는 장치다. 기본값 0은 "영원히 대신 ping" — 부팅이 유저 공간에서 멈춰도 리셋되지 않으므로 제품에서는 위험하다.

### 5.9 예제 17 — systemd가 watchdog 데몬 역할을 한다

현대 배포판과 Yocto/Buildroot의 systemd 이미지에서는 별도 데몬 대신 **PID 1(systemd)이 `/dev/watchdog`을 연다.** 설정은 `/etc/systemd/system.conf`.

```sh
# VM 호스트(Ubuntu, systemd)에서 실제 실행
systemctl show -p RuntimeWatchdogUSec -p RuntimeWatchdogPreUSec -p RebootWatchdogUSec -p KExecWatchdogUSec -p WatchdogDevice
grep -n "Watchdog" /etc/systemd/system.conf
systemctl show -p WatchdogUSec systemd-journald.service systemd-udevd.service systemd-logind.service
```

```text
WatchdogDevice=
RuntimeWatchdogUSec=0
RuntimeWatchdogPreUSec=0
RebootWatchdogUSec=10min
KExecWatchdogUSec=0
34:#RuntimeWatchdogSec=off
35:#RuntimeWatchdogPreSec=off
36:#RuntimeWatchdogPreGovernor=
37:#RebootWatchdogSec=10min
38:#KExecWatchdogSec=off
39:#WatchdogDevice=
WatchdogUSec=3min

WatchdogUSec=3min

WatchdogUSec=3min
```

두 층을 구분해서 읽는다.

| 층 | 설정 | 누가 누구를 감시 | 실패 시 |
|---|---|---|---|
| 하드웨어 ↔ PID 1 | `RuntimeWatchdogSec=` (system.conf) | HW watchdog이 systemd를 감시. systemd는 timeout의 절반 주기로 ping | 하드웨어 리셋 |
| 재부팅 중 | `RebootWatchdogSec=10min` | 종료 과정이 멈추면 | 하드웨어 리셋 (shutdown 행 방지) |
| 사전 경고 | `RuntimeWatchdogPreSec=`, `RuntimeWatchdogPreGovernor=` | 커널 pretimeout을 설정 | panic → 크래시 덤프 |
| PID 1 ↔ 서비스 | 유닛의 `WatchdogSec=` + 서비스의 `sd_notify("WATCHDOG=1")` | systemd가 서비스를 감시 | 서비스 kill + `Restart=`, 또는 `FailureAction=reboot` |

출력에서 볼 것: 이 VM은 `RuntimeWatchdogUSec=0`(꺼짐)이고 `/dev/watchdog*`도 없다(`ls: cannot access`, 가상 머신에 watchdog 장치 없음). 반면 journald·udevd·logind는 각자 `WatchdogSec=3min` — **서비스 수준 watchdog은 이미 기본으로 쓰이고 있다.** 제품에서는 여기에 `RuntimeWatchdogSec=30s` 같은 값을 켜서 하드웨어까지 연결한다. 데몬 방식(BusyBox `watchdog`, `watchdogd`, `watchdog` 패키지)과 비교하면:

| | systemd `RuntimeWatchdogSec` | 전용 데몬 |
|---|---|---|
| 무엇이 살아 있음을 증명하나 | PID 1의 이벤트 루프 | 데몬 루프 + (설정 시) 헬스 체크: 메모리, load, 파일 쓰기, 프로세스 존재, 테스트 스크립트 |
| 장점 | 추가 프로세스 없음, 서비스 watchdog과 자연스럽게 연결 | 시스템 지표 기반 판단, systemd 없는 BusyBox 시스템에서도 됨 |
| 약점 | PID 1이 돌기만 하면 ping — 핵심 앱이 멈춰도 감지는 서비스 `WatchdogSec`에 맡겨야 함 | 데몬 자체가 단일 실패점, 잘못된 헬스 체크가 리셋 루프를 만든다 |

---

## 6. 커널 안에서는 무슨 일이 일어나나

네 프레임워크는 모두 같은 3층 구조다: **유저 인터페이스(char dev·sysfs) → core(정책·검증·동시성) → 드라이버 ops**. 가장 많은 일을 core가 대신 하는 watchdog을 따라가 보자.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="150" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="42" text-anchor="middle" font-size="13">open()</text>
  <rect x="10" y="70" width="150" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="92" text-anchor="middle" font-size="13">write() / ioctl()</text>
  <rect x="10" y="120" width="150" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="142" text-anchor="middle" font-size="13">close()</text>
  <rect x="210" y="20" width="200" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="310" y="42" text-anchor="middle" font-size="13">watchdog_open → start</text>
  <rect x="210" y="70" width="200" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="310" y="92" text-anchor="middle" font-size="13">watchdog_ping → ping</text>
  <rect x="210" y="120" width="200" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="310" y="142" text-anchor="middle" font-size="13">watchdog_release</text>
  <line x1="160" y1="37" x2="208" y2="37" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="160" y1="87" x2="208" y2="87" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="160" y1="137" x2="208" y2="137" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="210" y="175" width="200" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="310" y="197" text-anchor="middle" font-size="12">'V' 받았고 nowayout 아님?</text>
  <text x="310" y="217" text-anchor="middle" font-size="12">예 → stop / 아니오 → ping</text>
  <line x1="310" y1="154" x2="310" y2="173" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="210" y="255" width="200" height="56" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="310" y="277" text-anchor="middle" font-size="12">kthread "watchdogd"</text>
  <text x="310" y="297" text-anchor="middle" font-size="12">hrtimer로 보충 ping</text>
  <rect x="460" y="45" width="210" height="80" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="565" y="70" text-anchor="middle" font-size="13">driver watchdog_ops</text>
  <text x="565" y="92" text-anchor="middle" font-size="12">start · stop · ping</text>
  <text x="565" y="112" text-anchor="middle" font-size="12">set_timeout · get_timeleft</text>
  <line x1="410" y1="37" x2="458" y2="62" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="410" y1="87" x2="458" y2="87" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="410" y1="203" x2="458" y2="112" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="410" y1="283" x2="500" y2="127" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="460" y="170" width="210" height="60" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="565" y="193" text-anchor="middle" font-size="12">보충 ping 조건</text>
  <text x="565" y="213" text-anchor="middle" font-size="12">HW_RUNNING이고 아무도 안 염</text>
  <rect x="460" y="245" width="210" height="60" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="565" y="268" text-anchor="middle" font-size="12">또는 timeout이</text>
  <text x="565" y="288" text-anchor="middle" font-size="12">max_hw_heartbeat_ms보다 김</text>
  <line x1="458" y1="200" x2="412" y2="270" stroke="currentColor" stroke-dasharray="4 3"/>
  <line x1="458" y1="275" x2="412" y2="285" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="340" y="345" text-anchor="middle" font-size="12">drivers/watchdog/watchdog_dev.c (core) — 드라이버는 오른쪽 상자만 구현한다</text>
</svg>
```

그림 11 — watchdog core의 경로. 유저의 open/write/ioctl/close가 core 함수를 거쳐 드라이버 ops로 내려가고, core의 `watchdogd` kthread_worker가 "부팅 중 이어받기"와 "하드웨어보다 긴 timeout"을 보충 ping으로 해결한다(§5.8에서 실측).

API → 커널 → 드라이버 대응표:

| 유저/소비자 | core 함수 (6.18) | 드라이버 콜백 | 이 노트에서 확인 |
|---|---|---|---|
| `echo 1 > /sys/class/leds/x/brightness` | `brightness_store()` → `led_set_brightness()` → `led_set_brightness_nopm()` | `brightness_set` 또는 workqueue의 `brightness_set_blocking` | 예제 5 (kworker) |
| `echo timer > trigger` | `led_trigger_set()` → `timer_trig_activate()` → `led_blink_set()` | `blink_set`(있으면) 아니면 소프트웨어 `timer_list` | 예제 4, 5 |
| `echo N > pwm0/duty_cycle` | `duty_cycle_store()` → `pwm_apply_might_sleep()` → `__pwm_apply()`(`pwm_state_valid()`) | `.apply` | 예제 6, 7 |
| `devm_pwm_get()` | `of_pwm_get()` → `of_xlate`(`#pwm-cells` 해석) | `.request`(선택) | 예제 7 |
| `ioctl(RTC_WKALM_SET)` | `rtc_dev_ioctl()` → `rtc_set_alarm()` → timerqueue → `__rtc_set_alarm()` | `.set_alarm`, `.alarm_irq_enable` | 예제 9 |
| `read(/dev/rtc0)` | `rtc_dev_read()` (wait queue에서 잠듦) | 알람 ISR의 `rtc_update_irq()`가 깨움 | 예제 9 |
| 부팅 hctosys | `rtc_hctosys()` (`devm_rtc_register_device()` 안) | `.read_time` | 예제 8 |
| `open(/dev/watchdog0)` | `watchdog_open()` → `watchdog_start()` | `.start` | 예제 15 |
| `ioctl(WDIOC_KEEPALIVE)` | `watchdog_ioctl()` → `watchdog_ping()` → `__watchdog_ping()` (`min_hw_heartbeat_ms` 지키기) | `.ping` | 예제 15 |
| 'V' 없는 `close()` | `watchdog_release()` → "did not stop!" + ping | `.ping` | 예제 12, 14 |
| pretimeout 만료 | `watchdog_notify_pretimeout()` → governor | 드라이버가 1단계 IRQ에서 호출 | 예제 15 |

드라이버를 쓸 때 다시 만나는 지점:

- **"잠들어도 되나"** 를 콜백마다 정직하게 표시하는 것이 공통 주제다: LED의 `brightness_set` vs `_blocking`, PWM의 `chip->atomic`, GPIO의 `can_sleep`(H4). I2C 위의 장치는 전부 "잠든다" 쪽이다(H5).
- **IRQ → 이벤트 → 유저**: RTC 알람의 `rtc_update_irq()` → wait queue → `read()`/`poll()`은 A4·F6에서 본 패턴 그대로다. wakeup 경로(`device_init_wakeup`, `enable_irq_wake`)는 N4(전원 관리)에서 깊게 다룬다.
- **devm + ops 등록**: `devm_led_classdev_register_ext`, `devm_pwmchip_add`, `devm_rtc_register_device`, `devm_watchdog_register_device` — I1의 "프레임워크에 등록" 원칙과 G3의 devres.

---

## 7. Embedded 실무에서 다시 보기

### 7.1 로봇의 watchdog 전략 — 한 층으로는 부족하다

Don이 SSD에서 겪은 것: 펌웨어 hang은 "멈췄다"보다 **"일부만 멈췄다"** 가 훨씬 흔하다. 호스트 명령 처리는 죽었는데 백그라운드 GC 태스크와 타이머 ISR은 살아 있어서 kick이 계속되는 식이다. RF 통합에서도 "칩은 응답하는데 특정 블록이 lock-up" 같은 반쪽 고장을 봤을 것이다. Linux 로봇에서도 똑같다: 커널은 멀쩡한데 제어 데몬이 데드락, 또는 카메라 파이프라인만 멈춤.

그래서 **계층**으로 설계한다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah8" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="15" width="430" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="225" y="36" text-anchor="middle" font-size="13">4. 앱 헬스: 제어 루프 deadline, 센서 프레임 도착</text>
  <text x="225" y="55" text-anchor="middle" font-size="12">실패 → 안전 정지 명령 + 서비스 재시작</text>
  <rect x="10" y="80" width="430" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="225" y="101" text-anchor="middle" font-size="13">3. systemd 서비스 WatchdogSec + sd_notify</text>
  <text x="225" y="120" text-anchor="middle" font-size="12">실패 → kill · Restart=, 반복 시 reboot</text>
  <rect x="10" y="145" width="430" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="225" y="166" text-anchor="middle" font-size="13">2. PID 1 RuntimeWatchdogSec + pretimeout</text>
  <text x="225" y="185" text-anchor="middle" font-size="12">실패 → pretimeout panic 덤프 → HW 리셋</text>
  <rect x="10" y="210" width="430" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="225" y="231" text-anchor="middle" font-size="13">1. HW watchdog (SoC WDT 또는 PMIC/외부 칩)</text>
  <text x="225" y="250" text-anchor="middle" font-size="12">부트로더가 켬 · nowayout · open_timeout</text>
  <rect x="10" y="275" width="430" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="225" y="296" text-anchor="middle" font-size="13">0. 모터 MCU 자체 watchdog + CAN heartbeat</text>
  <text x="225" y="315" text-anchor="middle" font-size="12">Linux가 통째로 죽어도 모터는 안전 정지</text>
  <rect x="470" y="60" width="200" height="90" rx="5" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/>
  <text x="570" y="85" text-anchor="middle" font-size="12">위층일수록 정밀한 진단</text>
  <text x="570" y="105" text-anchor="middle" font-size="12">아래층일수록 확실한 복구</text>
  <text x="570" y="125" text-anchor="middle" font-size="12">각 층은 아래층이 보충</text>
  <rect x="470" y="200" width="200" height="90" rx="5" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/>
  <text x="570" y="225" text-anchor="middle" font-size="12">리셋 후: bootstatus 기록</text>
  <text x="570" y="245" text-anchor="middle" font-size="12">pstore 덤프 수집</text>
  <text x="570" y="265" text-anchor="middle" font-size="12">리셋 횟수 → 안전 모드</text>
  <line x1="460" y1="35" x2="460" y2="320" stroke="currentColor" marker-end="url(#ah8)"/>
</svg>
```

그림 12 — 로봇 제품의 watchdog 계층. 위층은 "무엇이 잘못됐는지" 알고 부드럽게 복구하고, 아래층은 위층이 다 죽어도 확실하게 복구한다. 0층(모터 MCU)은 Linux와 독립적이어야 한다 — 로봇에서 "리셋"보다 중요한 것은 "리셋되는 동안 모터가 멈춰 있는 것"이다.

설계 체크리스트:

1. **하드웨어 watchdog을 부트로더에서 켠다.** 커널이 initramfs에서 멈춰도 잡히도록. 커널에서는 `handle_boot_enabled=1` + `watchdog.open_timeout=`(예: 120 s, 정상 부팅 시간의 2~3배)로 "유저 공간이 끝내 안 뜨면 리셋".
2. **nowayout을 켠다**(`CONFIG_WATCHDOG_NOWAYOUT=y` 또는 드라이버 모듈 인자). 업데이트 중에도 watchdog은 돈다 — OTA 스크립트도 watchdog을 고려해야 한다.
3. **kick하는 주체가 "건강"을 증명하게 한다.** systemd라면 `RuntimeWatchdogSec=`로 PID 1을, 각 핵심 서비스는 `WatchdogSec=`와 `sd_notify("WATCHDOG=1")`로. 핵심은 **제어 루프가 실제로 일을 마친 지점에서만 `WATCHDOG=1`을 보내는 것** — 별도 스레드에서 무조건 보내면 SSD의 "ISR에서 kick" 실수를 반복하는 것이다.
4. **pretimeout으로 증거를 남긴다.** pretimeout governor `panic` + pstore/ramoops(또는 kdump)로 리셋 직전 스택을 저장. 현장 리셋의 원인 분석이 가능해진다.
5. **리셋 원인을 센다.** 부팅 때 `WDIOC_GETBOOTSTATUS`(또는 SoC의 reset reason 레지스터)를 읽어 텔레메트리로. 짧은 시간에 여러 번 watchdog 리셋이 나면 **리셋 루프 탈출 정책**(안전 모드 부팅, A/B 이전 슬롯으로 롤백 — L 모듈의 업데이트와 연결).
6. **타임아웃 값은 측정으로 정한다.** 최악의 정상 지연(대용량 파일 fsync, 카메라 재협상, 부팅 직후 부하)보다 길게, 위험이 커지기 전보다 짧게. 리셋 루프를 만드는 너무 짧은 타임아웃이 가장 흔한 사고다.
7. **물리 안전은 Linux에 맡기지 않는다.** 모터 컨트롤러는 CAN heartbeat가 끊기면 스스로 정지(I10, Q 모듈), E-stop은 하드웨어 회로로.

### 7.2 상태 LED 설계 — 현장 기술자의 유일한 디버거

- 부팅 단계별 패턴: 부트로더(상시 켜짐, `default-state = "keep"`으로 커널이 끄지 않게) → 커널(heartbeat 트리거 — **커널이 멈추면 heartbeat도 멈춘다**는 점이 오히려 진단 정보다) → 앱 정상(상시) → 에러 코드(pattern 트리거로 "깜빡임 N회").
- `panic-indicator`를 붙인 빨간 LED는 panic 때 깜빡인다. 원격 로그가 없는 현장에서 "커널 panic인지, 앱 문제인지"를 LED 하나로 가른다.
- 네트워크 LED는 netdev 트리거(가능하면 PHY 하드웨어 오프로드)로 — CPU 부하 없이 링크/트래픽 표시.
- 유저 공간에서 LED를 직접 토글하는 루프(`while true; echo 1; sleep; echo 0`)는 금물. 트리거를 고르고 파라미터만 쓴다.

### 7.3 팬·백라이트 PWM — thermal과 연결

pwm-fan은 `#cooling-cells`로 **thermal cooling device**가 된다(I4). DT의 thermal zone `trips`/`cooling-maps`가 온도에 따라 `cooling-levels` 인덱스를 고르고, pwm-fan이 그 값을 duty로 바꾼다. 예제 7에서 pwm-fan이 probe 때 최대 duty로 시작한 것은 "thermal이 준비되기 전에는 최대 냉각"이라는 안전 기본값이다. 백라이트는 pwm_bl의 `brightness-levels` 테이블(사람 눈은 로그 스케일이라 선형 duty가 아닌 테이블을 쓴다)과 `default-brightness-level`.

PWM 주기 선택 감각: 4선 팬은 25 kHz(40 us — 예제의 `pwms = <... 40000 ...>`가 이 값), 백라이트는 깜빡임이 안 보이도록 수 kHz 이상, 서보는 50 Hz(20 ms) + 1~2 ms 펄스, 부저는 소리 주파수 그 자체.

### 7.4 제품 커널 설정 체크리스트 (이 랩 커널과 비교)

| 옵션 | 랩 Image-sub | 제품 권장 | 이유 |
|---|---|---|---|
| `CONFIG_WATCHDOG_NOWAYOUT` | 꺼짐 | 켬 | 실수·버그로 watchdog이 꺼지는 것 방지 |
| `CONFIG_WATCHDOG_HANDLE_BOOT_ENABLED` | 켬 | 켬 + `open_timeout` 설정 | 부팅 중 이어받되 무한정은 아니게 |
| `CONFIG_WATCHDOG_PRETIMEOUT_GOV` (+ `_PANIC`) | 꺼짐 | 켬 | 리셋 직전 덤프 |
| `CONFIG_WATCHDOG_SYSFS` | 꺼짐 | 켬 | `timeleft`, `bootstatus`를 셸에서 확인 |
| `CONFIG_LEDS_TRIGGER_PATTERN`, `_NETDEV` | 꺼짐 | 필요 시 켬 | 에러 코드 패턴, 링크 LED |
| RTC 드라이버 + I2C 컨트롤러 | PL031 built-in | built-in | hctosys가 부팅 초기에 되도록 |
| `CONFIG_RTC_HCTOSYS`, `_SYSTOHC` | 켬 | 켬 (유저 공간이 대신하면 끔) | 시간 출처를 한 곳으로 |

---

## 8. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| heartbeat 중인 LED에 `echo 0 > brightness` | 깜빡임이 영영 멈춤 | 0 쓰기는 트리거 제거 (`brightness_store`) | 트리거를 바꾸거나 다시 설정 |
| I2C LED 드라이버에 `brightness_set` 구현 | "BUG: sleeping function called from invalid context" | 트리거가 타이머(softirq)에서 부름 | `brightness_set_blocking` 사용 |
| `default-state` 생략 | 부팅 중 전원 LED가 꺼졌다 켜짐 | 기본값 off로 초기화 | 부트로더가 켰으면 `"keep"` |
| 켜진 PWM의 period를 duty보다 작게 먼저 씀 | `write error: Invalid argument` | 중간 상태가 `duty > period` | 줄일 때 duty 먼저 (커널 소비자는 상태 통째로 apply) |
| 잠드는 GPIO(I2C 확장기·gpio-sim)에 pwm-gpio | probe 실패 "sleeping GPIO not supported" | hrtimer(원자 문맥)에서 토글 | SoC MMIO GPIO 사용, 가능하면 하드웨어 PWM |
| 소프트웨어 PWM으로 1 kHz 모터 제어 | 펄스 누락·duty 오차 (예제 6: 1 ms에서 10% 펄스 96% 소실) | 타이머 지연 > 짧은 구간 | 하드웨어 PWM 블록 또는 MCU |
| I2C RTC 드라이버를 모듈로 | 부팅 로그가 1970년, 첫 TLS 연결 실패 | hctosys 시점에 RTC 없음 | built-in 또는 early `hwclock -s` |
| RTC 알람이 정확히 N초라고 가정 | 깨우기 시각이 최대 1초 어긋남 | RTC는 초 경계에서만 알람 | 1초 여유, 정밀 타이밍은 시스템 타이머 |
| watchdog 데몬이 별도 스레드에서 무조건 ping | 앱이 데드락인데 리셋 안 됨 | "건강"이 아니라 "스레드 생존"을 증명 | 작업 완료 지점에서 `sd_notify("WATCHDOG=1")` |
| 데몬 종료 스크립트가 `'V'`를 안 씀 | 업데이트 중 서비스 재시작 → 수 초 뒤 리셋 | magic close 없이 close → 계속 카운트 | 정지가 의도라면 `'V'`; nowayout이면 timeout 내 재오픈 |
| `open_timeout=0`(기본) 그대로 | rootfs 마운트 실패로 영원히 멈춤 | core가 무한정 보충 ping | `watchdog.open_timeout=` 설정 |
| 타임아웃을 너무 짧게 | 고부하·fsync 때마다 리셋, 리셋 루프 | 정상 최악 지연 > timeout | 측정 기반 timeout + 리셋 횟수 기반 안전 모드 |

---

## 9. 면접에서 이렇게 말한다

**Q.** How would you design the watchdog strategy for a robot running embedded Linux?

**A.** 계층으로 설계한다. 맨 아래는 부트로더가 켜는 하드웨어 watchdog(nowayout, open_timeout으로 부팅 실패도 잡음). 그 위에서 systemd `RuntimeWatchdogSec`가 PID 1을, 핵심 서비스는 `WatchdogSec` + `sd_notify`로 감시하는데, ping은 제어 루프가 실제로 deadline 안에 일을 마쳤을 때만 보낸다. pretimeout governor `panic` + pstore로 리셋 직전 덤프를 남기고, 부팅 때 bootstatus를 텔레메트리로 올려 리셋 횟수를 센다. 반복되면 안전 모드나 A/B 롤백. 그리고 모터 MCU는 CAN heartbeat가 끊기면 Linux와 무관하게 스스로 정지한다.

> I'd layer it. At the bottom, a hardware watchdog that the bootloader enables, with nowayout and an open timeout so a hang during boot also resets the board. Above that, systemd's RuntimeWatchdogSec pets the hardware from PID 1, and each critical service has WatchdogSec and only sends WATCHDOG=1 from the point where its control loop actually finished its work on time — not from a side thread. I'd enable a pretimeout with the panic governor plus pstore so every reset leaves a crash record, read the boot status at startup and report watchdog resets as telemetry, and fall back to a safe mode or the previous A/B slot if resets repeat. Independently, the motor controllers run their own watchdog on the CAN heartbeat, so the actuators stop safely even if Linux is gone.

**꼬리 질문**: "timeout 값은 어떻게 정하나?" → 정상 최악 지연(부팅 직후, 대용량 fsync, 카메라 재협상)을 측정하고 여유를 둔다. 너무 짧으면 리셋 루프.

**Q.** What is the magic close feature, and what does nowayout change?

**A.** `/dev/watchdog`은 열면 시작하고, 닫는다고 멈추지 않는다. 마지막에 `'V'`를 쓰고 닫아야만 멈춘다 — 데몬이 죽어서 fd가 닫힌 경우와 의도적 종료를 구분하려는 것이다. nowayout이면 `'V'`도 무시돼서 한 번 시작된 watchdog은 절대 멈추지 않는다. 실습에서 i6300esb로 'V' 없이 닫으면 5초 뒤 실제 리셋, nowayout=1이면 'V'를 써도 리셋되는 것을 확인했다.

> Opening the device starts the watchdog, and closing it does not stop it. Only writing the character 'V' right before close stops it, so a daemon that crashes is distinguished from one that shuts down on purpose — the kernel prints "watchdog did not stop" and keeps counting. With nowayout, even the magic close is ignored; once started it can never be stopped, which is what you want in a product.

**Q.** What's the difference between the RTC and the system clock, and how do they stay in sync?

**A.** 시스템 시계는 커널이 ns 단위로 유지하고 전원이 꺼지면 사라진다. RTC는 배터리로 초 단위를 유지한다. 부팅 때 hctosys로 RTC → 시스템 시계 한 번, 이후 NTP/PTP가 시스템 시계를 맞추고, 동기화 상태면 커널이 약 11분마다 systohc로 RTC에 되쓴다. RTC 드라이버가 늦게 뜨면 초기 로그가 1970년이 된다.

> The system clock is kept by the kernel at nanosecond resolution and is lost at power-off; the RTC is a battery-backed, one-second-resolution clock. At boot the kernel copies RTC to system time once (hctosys). After that NTP or PTP disciplines the system clock, and when it's marked synchronized the kernel writes it back to the RTC about every eleven minutes. If the RTC driver probes late — say it's an I2C module — everything before it is stamped 1970.

**Q.** How does an RTC alarm wake the system from suspend?

**A.** 드라이버가 probe에서 `device_init_wakeup()`으로 wakeup 가능을 선언하고, suspend 때 알람 IRQ를 wake IRQ로 켠다. 유저는 `RTC_WKALM_SET`이나 sysfs `wakealarm`, `rtcwake`로 알람을 건다. IRQ가 오면 `rtc_update_irq()`가 이벤트를 올리고 PM core가 resume한다. QEMU virt에서 `rtcwake -m freeze -s 5`로 s2idle에서 깨어나는 것과 `wakeup_sources`의 카운트를 확인했다.

> The driver declares itself wakeup-capable with device_init_wakeup and arms its alarm interrupt as a wake IRQ on suspend. User space sets the alarm with RTC_WKALM_SET, the wakealarm sysfs file, or rtcwake. When it fires, the ISR calls rtc_update_irq, a wakeup event is recorded, and the PM core resumes. I verified it in QEMU with rtcwake into s2idle and checked the pl031 entry in wakeup_sources.

**Q.** brightness_set vs brightness_set_blocking — when do you use which?

**A.** 트리거가 타이머·softirq 문맥에서 밝기를 바꾸므로 `brightness_set`은 절대 잠들면 안 된다 — MMIO GPIO용. I2C/SPI LED IC처럼 쓰기가 잠들면 `_blocking`을 구현하고, core가 ordered workqueue에서 대신 부른다. 실습에서 sysfs 쓰기조차 kworker 문맥에서 불리는 걸 확인했다.

> Triggers change brightness from timer or softirq context, so brightness_set must never sleep — it's for memory-mapped GPIOs. If the write goes over I2C or SPI you implement brightness_set_blocking, and the LED core defers it to its ordered workqueue. In my test even a sysfs write arrived in a kworker.

**Q.** Why does the PWM API use an atomic pwm_state, and what goes wrong with sysfs?

**A.** period·duty·polarity·enable을 한 번에 넘겨야 드라이버가 하드웨어 특성에 맞게 글리치 없이 바꿀 수 있다(주기 끝에서 반영 등). sysfs는 파일 하나씩 쓰므로 중간 상태가 생기고, 켜진 상태에서 period를 duty보다 작게 먼저 쓰면 EINVAL이다. 줄일 땐 duty 먼저.

> The consumer passes period, duty, polarity and enable as one state, so the driver can apply it without glitches — for example latching it at the end of the current period. Sysfs writes one attribute at a time, so you go through intermediate states; shrinking the period below the current duty while enabled returns EINVAL. Lower the duty first, then the period.

**Q.** When is a software PWM (pwm-gpio) acceptable?

**A.** 정확도는 "가장 짧은 구간 대비 타이머 지연"이 결정한다. LED·부저·50 Hz 서보처럼 ms 단위면 괜찮지만, kHz 모터 제어는 안 된다. 잠드는 GPIO에는 아예 못 쓴다. 측정해 보니 20 ms 주기는 ±3%p, 1 ms 주기에서는 10% 펄스가 대부분 사라졌다(에뮬레이터라 절대값보다 경향).

> Its accuracy is bounded by timer latency relative to the shortest phase of the waveform. Fine for LEDs, buzzers or a 50 Hz servo; not for kilohertz motor control, and it refuses sleeping GPIOs outright. In my emulated measurement, a 20 ms period stayed within about three points of duty, while at 1 ms most of the 10 % pulses disappeared.

**Q.** The bootloader enables the watchdog. What happens before user space opens it?

**A.** 드라이버가 `WDOG_HW_RUNNING`을 알리면 core의 `watchdogd` 스레드가 대신 ping한다(`handle_boot_enabled`). `watchdog.open_timeout`을 주면 그 시간 안에 유저가 열지 않을 때 리셋이 나도록 ping을 멈춘다. 기본값 0이면 무한정 ping하므로 제품에서는 설정해야 한다.

> If the driver reports the hardware as already running, the watchdog core's kernel worker keeps pinging it until user space opens the device. With watchdog.open_timeout set, the core stops pinging so that the board resets if nobody opens it in time — I measured the reset landing exactly at the open deadline. The default of zero pings forever, which hides a boot that hangs in user space.

---

## 10. 직접 해보기

1. **개념**: GPIO LED의 `brightness_set`을 구현한 드라이버를 I2C GPIO 확장기 위의 LED에 쓰면 무엇이 문제인가? leds-gpio는 이 문제를 어떻게 피하나? 정답/힌트: 확장기 쓰기는 잠든다. leds-gpio는 `gpiod_cansleep()`이면 `brightness_set_blocking`을, 아니면 `brightness_set`을 등록한다(`drivers/leds/leds-gpio.c`).
2. **코드**: §2.4의 샘플러로 `cpu` 트리거나 `disk-activity` 트리거를 붙인 LED를 재 보라. `dd`로 부하를 주면 파형이 어떻게 바뀌나? 정답/힌트: cpu 트리거는 idle 진입/탈출에 따라 토글. 부하 중 high 비율이 커진다.
3. **코드**: `i9_pwm.c`의 `.apply`를 "현재 주기가 끝난 뒤 적용"하는 하드웨어처럼 바꿔(`hrtimer`로 지연 적용) debugfs `requested`/`actual`이 잠깐 달라지는 것을 관찰하라. 정답/힌트: `.get_state`가 아직 이전 값을 돌려주는 동안 `actual`이 다르다.
4. **관찰**: `rtcwake -m freeze -s 10` 직후 `wakeup_sources`의 `event_count`·`last_change`를 전후로 비교해 깨운 장치를 특정하라. 가능하면 `CONFIG_PM_SLEEP_DEBUG`를 켠 커널로 `/sys/power/pm_wakeup_irq`도 읽어 보라. 정답/힌트: `9010000.pl031` 행만 `event_count`가 늘고 `last_change`(ms)가 resume 시각과 맞는다.
5. **코드**: `i9_wdog.c`를 고쳐 `poll()`로 기다리는 메인 루프를 만들고, 메인 루프가 1초 안에 일을 못 끝내면 ping을 건너뛰게 하라(앱 헬스 기반 kick). `usleep(1500000)`을 끼워 리셋이 나는지 i6300esb로 확인. 정답/힌트: "ping 위치 = 작업 완료 지점"이 핵심.
6. **설계**: 정상 부팅이 40초, 최악의 OTA 후 첫 부팅이 150초인 장비에서 `watchdog.open_timeout`과 `RuntimeWatchdogSec`를 정하고 이유를 써라. 정답/힌트: open_timeout은 최악 부팅보다 크게(예: 300 s). RuntimeWatchdogSec는 PID 1 응답성 기준(예: 30~60 s).
7. **관찰**: `CONFIG_WATCHDOG_SYSFS`를 켠 커널(`O=/var/tmp/I9/build`로 별도 빌드)에서 `/sys/class/watchdog/watchdog0/timeleft`를 1초마다 읽어 그림 10과 비교하라. 정답/힌트: 같은 `.get_timeleft`를 부르므로 같은 내림 값.

---

## 11. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| LED class | LED 서브시스템 | `/sys/class/leds`, `led_classdev` 등록 |
| trigger | 트리거 | LED를 언제 켤지 정하는 정책 모듈 (timer, heartbeat, netdev …) |
| `brightness_set_blocking` | 잠들 수 있는 밝기 콜백 | core가 workqueue에서 부름 |
| `default-state` | DT 초기 상태 | on / off / keep |
| multicolor LED | RGB LED 클래스 | `multi_intensity`로 채널별 강도 |
| PWM | 펄스 폭 변조 | period·duty·polarity로 평균 전력 조절 |
| `struct pwm_state` | PWM 상태 | 한 번에 apply하는 원자적 설정 |
| `pwm_apply_might_sleep` | 소비자 적용 함수 | 프로세스 문맥, 검증 후 `.apply` |
| pwm-gpio | 소프트웨어 PWM | GPIO를 hrtimer로 토글 |
| waveform API | 새 PWM 표현 | `duty_offset` 포함, 반올림 질의 가능 |
| RTC | 실시간 시계 | 배터리 백업, 초 해상도 |
| hctosys / systohc | 시계 복사 | RTC → 시스템(부팅), 시스템 → RTC(동기화 중) |
| `RTC_WKALM_SET` | 알람 ioctl | 시각 + 활성화를 한 번에 |
| wakeup source | 깨움 장치 | suspend에서 시스템을 깨울 수 있는 장치/이벤트 |
| s2idle | suspend-to-idle | 가장 얕은 시스템 잠, `freeze` |
| watchdog | 감시 타이머 | ping이 끊기면 리셋 |
| keepalive / ping / kick | 먹이 주기 | 카운터 재장전 |
| magic close | `'V'` 후 close | watchdog 정지의 유일한 정상 경로 |
| nowayout | 정지 불가 | 한 번 시작되면 영원히 |
| pretimeout | 사전 경고 | 리셋 N초 전 이벤트 → governor |
| governor | pretimeout 처리 정책 | panic(덤프) 또는 noop |
| `WDOG_HW_RUNNING` | HW가 이미 돌고 있음 | core가 대신 ping |
| open_timeout | 첫 open 제한 시간 | 넘으면 core가 ping 중단 → 리셋 |
| `WDIOF_CARDRESET` | bootstatus 비트 | 지난 리셋이 watchdog 때문 |
| RuntimeWatchdogSec | systemd 설정 | PID 1이 HW watchdog을 ping |

---

## 12. 요약 & 체크리스트

LED·PWM·RTC·watchdog은 "드라이버는 작은 ops 구조체만, 나머지는 core가"라는 Linux 프레임워크 원칙의 가장 작은 예다. LED class는 밝기 콜백과 트리거를 분리하고, 잠드는 하드웨어는 `brightness_set_blocking`으로 workqueue에 맡긴다. PWM은 period·duty·polarity·enable을 `struct pwm_state` 하나로 원자적으로 적용하며, 소프트웨어 PWM의 정확도는 타이머 지연이 결정한다(실측: 20 ms는 양호, 1 ms는 붕괴). RTC는 부팅 때 한 번 시스템 시계의 출처가 되고, 알람은 `read()`를 깨우거나 suspend에서 시스템을 깨운다. watchdog은 open이 시작, `'V'` close만이 정지, nowayout이면 정지 불가이며, core가 부팅 중 보충 ping·open_timeout·pretimeout을 제공한다. 제품의 watchdog은 HW → PID 1 → 서비스 → 앱 헬스, 그리고 Linux와 독립된 모터 MCU까지 계층으로 설계한다.

- [ ] `led_classdev`의 `brightness_set`과 `brightness_set_blocking`의 차이와 호출 문맥을 설명할 수 있다
- [ ] DT `gpio-leds` 노드를 `function`/`color`/`default-state`/`linux,default-trigger`로 쓸 수 있다
- [ ] timer·heartbeat·pattern 트리거를 sysfs로 설정하고 파형을 샘플링해 검증할 수 있다
- [ ] `struct pwm_state`와 `.apply`/`.get_state`를 설명하고 가짜 `pwm_chip`을 코드로 짤 수 있다
- [ ] DT `pwms = <&pwm ch period flags>`와 `pwm_init_state`/`pwm_apply_might_sleep` 소비자 코드를 짤 수 있다
- [ ] sysfs PWM 순서 함정과 소프트웨어 PWM의 한계를 측정 근거로 말할 수 있다
- [ ] hctosys/systohc와 NTP/PTP의 관계, 1970년 문제의 원인을 설명할 수 있다
- [ ] `RTC_RD_TIME`/`RTC_SET_TIME`/`RTC_WKALM_SET` + `read()`로 알람을 기다리는 C 코드를 짤 수 있다
- [ ] RTC 알람이 s2idle에서 시스템을 깨우는 경로(`device_init_wakeup`, wake IRQ, wakeup_sources)를 설명할 수 있다
- [ ] `/dev/watchdog`의 open·keepalive·magic close·nowayout·pretimeout 규칙을 설명하고 급식기를 짤 수 있다
- [ ] `handle_boot_enabled`, `max_hw_heartbeat_ms`, `open_timeout`이 core의 보충 ping에 미치는 영향을 설명할 수 있다
- [ ] 로봇 제품의 watchdog 전략을 계층으로 설계해 영어로 설명할 수 있다

---

## 참고 자료

- docs.kernel.org: [LED handling under Linux](https://docs.kernel.org/leds/leds-class.html), [Multicolor LED handling](https://docs.kernel.org/leds/leds-class-multicolor.html), [Pulse Width Modulation (PWM) interface](https://docs.kernel.org/driver-api/pwm.html), [Real Time Clock (RTC) Drivers for Linux](https://docs.kernel.org/admin-guide/rtc.html), [The Linux Watchdog driver API](https://docs.kernel.org/watchdog/watchdog-api.html), [The Linux WatchDog Timer Driver Core kernel API](https://docs.kernel.org/watchdog/watchdog-kernel-api.html)
- 커널 ABI 문서 (랩 트리 `Documentation/ABI/testing/`): `sysfs-class-led`, `sysfs-class-led-trigger-pattern`, `sysfs-class-pwm`, `sysfs-class-rtc`, `sysfs-class-watchdog`
- man7.org: [rtc(4)](https://man7.org/linux/man-pages/man4/rtc.4.html), [hwclock(8)](https://man7.org/linux/man-pages/man8/hwclock.8.html), [rtcwake(8)](https://man7.org/linux/man-pages/man8/rtcwake.8.html), [systemd-system.conf(5)](https://man7.org/linux/man-pages/man5/systemd-system.conf.5.html), [sd_notify(3)](https://man7.org/linux/man-pages/man3/sd_notify.3.html)
- 커널 소스 (랩 트리 6.18.55, elixir.bootlin.com에서도 같은 경로): `drivers/leds/led-class.c`, `drivers/leds/led-core.c`, `drivers/leds/led-triggers.c`, `drivers/leds/leds-gpio.c`, `drivers/leds/trigger/ledtrig-heartbeat.c`, `drivers/leds/trigger/ledtrig-pattern.c`, `drivers/pwm/core.c`, `drivers/pwm/pwm-gpio.c`, `drivers/hwmon/pwm-fan.c`, `drivers/rtc/class.c`, `drivers/rtc/interface.c`, `drivers/rtc/dev.c`, `drivers/rtc/rtc-pl031.c`, `drivers/rtc/rtc-test.c`, `drivers/watchdog/watchdog_dev.c`, `drivers/watchdog/watchdog_core.c`, `drivers/watchdog/i6300esb.c`, `drivers/watchdog/softdog.c`, `include/linux/leds.h`, `include/linux/pwm.h`, `include/linux/rtc.h`, `include/linux/watchdog.h`, `include/uapi/linux/watchdog.h`, `include/uapi/linux/rtc.h`, `include/uapi/linux/pwm.h`
- Bootlin, "Linux kernel and driver development" 교육 자료 (bootlin.com/training/kernel)
- 관련 노트: H4(gpio-sim, `_cansleep`), I1(프레임워크 원칙), I4(hwmon·thermal), N4(전원 관리·suspend), Q3(시간 동기화), L 모듈(OTA·A/B 업데이트)
