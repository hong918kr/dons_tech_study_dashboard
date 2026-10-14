# I4. hwmon · thermal · power supply — 온도/전압 센서, thermal zone·trip·cooling device, 배터리

> **이 노트를 다 읽으면**: `hwmon_chip_info`/`HWMON_CHANNEL_INFO`로 온도·전압·전류·전력·팬 채널을 가진 hwmon 드라이버를 짜고 sysfs 단위(m°C, mV, mA, µW, RPM)를 설명할 수 있다 · DT `thermal-zones`(trips·hysteresis·cooling-maps)와 `HWMON_C_REGISTER_TZ`로 thermal zone을 만들고, `thermal_cooling_device_ops`로 냉각 장치를 붙이고, step_wise governor가 시간에 따라 state를 어떻게 바꾸는지 측정 데이터로 설명할 수 있다 · `emul_temp`, critical trip → `orderly_poweroff()`, polling과 interrupt 방식(`set_trips`)의 차이를 직접 실험으로 보여 줄 수 있다 · `power_supply_desc`로 연료 게이지 드라이버를 짜고 `simple-battery` DT와 uevent 흐름을 설명할 수 있다
> **왜 필요한가**: 로봇·드론 컴퓨트 보드는 밀폐 케이스 안에서 NPU·카메라 ISP를 돌리다 뜨거워지고, 배터리로 돈다. "보드가 뜨거워지면 리눅스는 무엇을 하나?", "thermal zone을 DT로 어떻게 정의하나?", "critical 온도에서 꺼지지 않았다면 어디를 보나?", "배터리 잔량을 UI가 어떻게 아나?"는 Embedded Linux 면접의 단골이고, 실무에서는 열 마진 검증·필드 고장 분석의 출발점이다
> **Don 기준 난이도**: 열·전력 마진 사인오프(Apple 기기), SSD thermal throttling(온도 단계별 성능 제한)은 이미 해 본 일이다 / 새로 배울 것은 그 제어 루프를 커널이 **센서(hwmon) · 판단(thermal zone + governor) · 행동(cooling device)** 세 조각으로 나눠 DT로 조립한다는 구조와 각 조각의 sysfs 창, 그리고 배터리를 보는 power_supply 클래스
> **선행 노트**: D2·D4(DT 문법·phandle 인자), G3(platform driver·devm), G6(sysfs·uevent), H4(gpio-sim), F7(workqueue) — I9(PWM·watchdog), N(전원 관리), M(ftrace)로 이어진다

---

## 0. 큰 그림 — 측정 · 판단 · 행동

Don이 SSD 펌웨어에서 thermal throttling을 짤 때 구조는 늘 같았을 것이다. 온도 센서(컨트롤러 다이 TSENS, NAND 근처 서미스터)를 주기적으로 읽고, 온도가 단계 1(NVMe로 치면 TMT1 같은 경계)을 넘으면 NAND 동시 동작 수를 줄이고, 단계 2를 넘으면 더 세게 줄이고, 그래도 안 되면 위험 온도에서 쓰기를 멈춘다. 내려올 때는 바로 풀지 않고 몇 도 아래까지 기다린다(hysteresis). Apple에서 열 마진을 사인오프할 때도 "어느 센서가, 어느 경계에서, 어떤 조치를 일으키고, 그 조치가 실제로 온도를 잡는가"를 확인했을 것이다.

Linux는 이 루프를 세 개의 서브시스템으로 쪼갠다.

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="85" y="20" text-anchor="middle" font-size="13" font-weight="bold">하드웨어</text>
<text x="330" y="20" text-anchor="middle" font-size="13" font-weight="bold">커널 프레임워크</text>
<text x="580" y="20" text-anchor="middle" font-size="13" font-weight="bold">유저 공간</text>
<rect x="10" y="40" width="150" height="46" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="85" y="59" text-anchor="middle" font-size="13">온도·전압·전류 센서</text>
<text x="85" y="76" text-anchor="middle" font-size="12">TMP102, INA219</text>
<rect x="10" y="150" width="150" height="46" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="85" y="169" text-anchor="middle" font-size="13">냉각 수단</text>
<text x="85" y="186" text-anchor="middle" font-size="12">팬 PWM, CPU DVFS</text>
<rect x="10" y="270" width="150" height="46" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="85" y="289" text-anchor="middle" font-size="13">배터리 · 게이지 · 충전기</text>
<text x="85" y="306" text-anchor="middle" font-size="12">BQ27xxx, MAX17042</text>
<rect x="225" y="40" width="210" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="330" y="59" text-anchor="middle" font-size="13">hwmon</text>
<text x="330" y="76" text-anchor="middle" font-size="12">측정값을 표준 단위로 보고</text>
<rect x="225" y="130" width="210" height="86" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="330" y="150" text-anchor="middle" font-size="13">thermal</text>
<text x="330" y="168" text-anchor="middle" font-size="12">zone (온도 1개) + trips</text>
<text x="330" y="185" text-anchor="middle" font-size="12">governor (step_wise …)</text>
<text x="330" y="202" text-anchor="middle" font-size="12">cooling device (0..max state)</text>
<rect x="225" y="270" width="210" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="330" y="289" text-anchor="middle" font-size="13">power_supply</text>
<text x="330" y="306" text-anchor="middle" font-size="12">CAPACITY, VOLTAGE_NOW …</text>
<rect x="495" y="40" width="175" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="582" y="59" text-anchor="middle" font-size="12">/sys/class/hwmon</text>
<text x="582" y="76" text-anchor="middle" font-size="12">sensors, 헬스 모니터</text>
<rect x="495" y="150" width="175" height="46" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="582" y="169" text-anchor="middle" font-size="12">/sys/class/thermal</text>
<text x="582" y="186" text-anchor="middle" font-size="12">policy, emul_temp, 통계</text>
<rect x="495" y="270" width="175" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="582" y="289" text-anchor="middle" font-size="12">/sys/class/power_supply</text>
<text x="582" y="306" text-anchor="middle" font-size="12">uevent → UI, 저전력 정책</text>
<line x1="160" y1="63" x2="223" y2="63" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="223" y1="183" x2="162" y2="175" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="192" y="214" text-anchor="middle" font-size="12">set_cur_state</text>
<line x1="160" y1="293" x2="223" y2="293" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="300" y1="86" x2="300" y2="128" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="296" y="112" text-anchor="end" font-size="12">REGISTER_TZ</text>
<line x1="360" y1="128" x2="360" y2="88" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
<text x="366" y="112" font-size="12">THERMAL_HWMON (DT 존 제외)</text>
<line x1="330" y1="268" x2="330" y2="218" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
<text x="336" y="246" font-size="12">TEMP 속성 → zone 자동</text>
<line x1="435" y1="63" x2="493" y2="63" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="435" y1="173" x2="493" y2="173" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="435" y1="293" x2="493" y2="293" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="340" y="345" text-anchor="middle" font-size="12">실선 = 이 노트의 i4_temp · i4_batt가 직접 부르는 경로, 점선 = 커널이 자동으로 만들어 주는 연결</text>
</svg>
```
그림 1 — 센서 · 냉각 수단 · 배터리가 각각 어느 커널 프레임워크에 붙고, 유저 공간에 어떤 창으로 보이는가.

| 펌웨어에서 하던 일 | Linux에서 맡는 곳 | 이 노트에서 |
|---|---|---|
| 센서 레지스터를 읽어 °C·mV로 변환 | **hwmon** 드라이버의 `.read` | §2, `i4_temp` |
| "몇 도부터 조치"라는 경계표 | **thermal zone**의 trip (DT `thermal-zones`) | §3 |
| 경계를 넘었을 때 조치 단계를 정하는 로직 | **governor** (step_wise 등) | §3.6, §4 |
| 팬 PWM 올리기, 클럭 내리기 | **cooling device**의 `set_cur_state` | §3.4, §8 |
| 위험 온도에서 강제 종료 | critical trip → `orderly_poweroff()` | §7 |
| 연료 게이지 읽어 잔량 보고 | **power_supply** 클래스 | §10, `i4_batt` |

핵심은 **역할 분리**다. 센서 드라이버는 "지금 몇 도인가"만 답한다. 몇 도에서 무엇을 할지는 DT가 정하고(보드마다 다르다 — 같은 SoC라도 팬이 있는 보드와 없는 보드), 실제 조치는 별도의 cooling device 드라이버가 한다. 그래서 센서 칩을 바꿔도, 팬을 바꿔도, 정책을 바꿔도 나머지 둘은 그대로다. 펌웨어에서 한 함수에 다 넣던 루프를 세 드라이버와 DT 한 덩어리로 나눈 것이다.

---

## 1. 실습 준비 — 가짜 다이 센서 · 가짜 팬 · gpio-fan · 가짜 연료 게이지

### 1.1 어느 커널에서 무엇을 돌리나

실행 환경: Lima VM `elinux`(호스트 커널 `7.0.0-38-generic`, aarch64) 안에서 공용 랩의 QEMU `virt` 게스트를 TCG로 부팅한다. 이 노트는 랩 커널 두 개를 쓴다.

| 커널 | 버전 문자열 | 쓰는 곳 | 이유 |
|---|---|---|---|
| `out/Image-sub` | `6.18.55-sub` | §2~§8, §10 전부 | `test_power`, `gpio-fan`이 모듈로 들어 있다. `THERMAL_EMULATION`, `THERMAL_OF`, `THERMAL_HWMON`, `POWER_SUPPLY_HWMON` = y |
| `out/Image-dbg` | `6.18.55-dbg` | §9 tracepoint만 | `FTRACE=y`. Image-sub에는 ftrace가 없다 |

랩 빌드 트리의 설정을 확인한 결과(일부):

```sh
cd /var/lab/out
for b in linux-build-dbg linux-build-sub; do echo == $b
  grep -E "^(# )?CONFIG_(THERMAL_GOV_[A-Z_]*|THERMAL_EMULATION|FTRACE|TEST_POWER)[ =]" $b/.config
done
```

```text
== linux-build-dbg
# CONFIG_TEST_POWER is not set
# CONFIG_THERMAL_GOV_FAIR_SHARE is not set
CONFIG_THERMAL_GOV_STEP_WISE=y
# CONFIG_THERMAL_GOV_BANG_BANG is not set
# CONFIG_THERMAL_GOV_USER_SPACE is not set
CONFIG_THERMAL_GOV_POWER_ALLOCATOR=y
CONFIG_THERMAL_EMULATION=y
CONFIG_FTRACE=y
== linux-build-sub
CONFIG_TEST_POWER=m
# CONFIG_THERMAL_GOV_FAIR_SHARE is not set
CONFIG_THERMAL_GOV_STEP_WISE=y
# CONFIG_THERMAL_GOV_BANG_BANG is not set
# CONFIG_THERMAL_GOV_USER_SPACE is not set
CONFIG_THERMAL_GOV_POWER_ALLOCATOR=y
CONFIG_THERMAL_EMULATION=y
# CONFIG_FTRACE is not set
```

출력에서 볼 것: 두 커널 모두 governor는 **step_wise와 power_allocator 두 개뿐**이다. fair_share·bang_bang·user_space는 설명만 하고 실행하지 않는다(§3.6). 그리고 **모듈은 커널마다 따로 빌드한다**. `i4_temp.ko`를 `linux-build-sub`로 빌드하면 vermagic이 `6.18.55-sub`, `linux-build-dbg`로 빌드하면 `6.18.55-dbg`가 되고, 다른 커널에 넣으면 insmod가 거부한다. 그래서 작업 폴더를 `/var/tmp/I4/sub/`와 `/var/tmp/I4/dbg/`로 나눴다.

### 1.2 무엇을 만드나

| DT 노드 | 드라이버 | 역할 |
|---|---|---|
| `i4_sensor: i4-temp` | `i4_temp.ko` (직접 작성) | 가짜 SoC 다이 센서. hwmon 채널: 온도·전압·전류·전력·팬 RPM. `HWMON_C_REGISTER_TZ`로 thermal zone 센서가 된다 |
| `i4_fan: fan` (i4-temp의 자식) | `i4_temp.ko`의 cooling device | 가짜 팬. state 0~3. state가 오르면 모델의 열저항이 내려간다 |
| `i4-gpio-sim` / `bank0` | 커널 내장 `gpio-sim` | 가짜 GPIO 칩 2핀 (H4에서 본 그것) |
| `i4_gfan: i4-gpio-fan` | 커널 `gpio-fan.ko` | 진짜 업스트림 팬 드라이버. GPIO 2개 조합으로 속도 4단계 |
| `thermal-zones` / `i4-soc` | 커널 `thermal_of` | trip 60(passive) · 80(active) · 95(critical), cooling-maps |
| `i4_cell: i4-battery` | 커널 `power_supply_core` | `simple-battery`: 용량·전압 범위·내부 저항·OCV 표 |
| `i4-fuel-gauge` | `i4_batt.ko` (직접 작성) | 가짜 연료 게이지. CAPACITY·VOLTAGE_NOW·CURRENT_NOW 보고 |

**다이 온도 모델**이 실습의 핵심이다. 진짜 칩이라면 온도는 레지스터에서 읽지만, 여기서는 100 ms마다 1차 열 모델을 한 걸음 진행한다.

| 값 | 식 | 의미 |
|---|---|---|
| 소비 전력 P | 0.2 W + 2.8 W × load/100 | 모듈 파라미터 `load`(0~100 %)가 CPU 부하 역할 |
| 열저항 Rth | 22 · 16 · 12 · 9 °C/W (fan state 0~3) | 팬이 셀수록 열이 잘 빠진다 |
| 정상 상태 온도 | 35 °C + P × Rth | load 100 %, 팬 0이면 35 + 3.0 × 22 = 101 °C |
| 시간 상수 | 4 s | 100 ms마다 (정상 온도 − 현재 온도)/40 만큼 다가간다 |

즉 팬을 안 돌리면 load 100 %에서 critical(95 °C)을 넘는 칩이고, 팬 state 3이면 62 °C에서 멈추는 칩이다. 이 모델 덕분에 governor가 팬을 바꾸면 **온도가 실제로 반응**하고, 그 폐루프를 시계열로 찍을 수 있다(§4).

### 1.3 예제 1 — DT 조각과 dtb 빌드

무엇을 확인하는 코드인가: 랩 `virt.dts` 끝에 붙일 노드 전부다. 센서·팬·gpio-fan·배터리·thermal zone이 phandle로 어떻게 엮이는지가 이 노트 전체의 배선도다.

```dts
/* I4 additions to the QEMU virt DT (appended at the end of virt.dts) */
#include <dt-bindings/thermal/thermal.h>
#include <dt-bindings/gpio/gpio.h>
#ifndef I4_POLL_MS
#define I4_POLL_MS 1000
#endif
#define I4_PASSIVE_MS 250
/ {
	i4_sensor: i4-temp {
		compatible = "i4,temp";
		#thermal-sensor-cells = <1>;		/* cell = hwmon temp channel */
		i4_fan: fan {
			#cooling-cells = <2>;		/* <&i4_fan min max> */
		};
	};

	i4-gpio-sim {
		compatible = "gpio-simulator";
		i4_gsim: bank0 {
			gpio-controller;
			#gpio-cells = <2>;
			ngpios = <2>;
			gpio-sim,label = "i4_sim";
			gpio-line-names = "FAN_SPD0", "FAN_SPD1";
		};
	};

	i4_gfan: i4-gpio-fan {
		compatible = "gpio-fan";
		gpios = <&i4_gsim 0 GPIO_ACTIVE_HIGH>, <&i4_gsim 1 GPIO_ACTIVE_HIGH>;
		gpio-fan,speed-map = <0 0>, <2000 1>, <4000 2>, <6000 3>;
		#cooling-cells = <2>;
	};

	i4_cell: i4-battery {
		compatible = "simple-battery";
		charge-full-design-microamp-hours = <3000000>;
		voltage-min-design-microvolt = <3300000>;
		voltage-max-design-microvolt = <4200000>;
		factory-internal-resistance-micro-ohms = <150000>;
		ocv-capacity-celsius = <25>;
		ocv-capacity-table-0 = <4200000 100>, <4080000 90>, <3980000 80>,
			<3900000 70>, <3840000 60>, <3800000 50>, <3770000 40>,
			<3740000 30>, <3700000 20>, <3640000 10>, <3300000 0>;
	};

	i4-fuel-gauge {
		compatible = "i4,batt";
		monitored-battery = <&i4_cell>;
	};
```

```dts
	thermal-zones {
		i4-soc {
			polling-delay-passive = <I4_PASSIVE_MS>;	/* while a passive trip is crossed */
			polling-delay = <I4_POLL_MS>;		/* otherwise (0 = interrupt only) */
			thermal-sensors = <&i4_sensor 0>;

			trips {
				i4_warm: warm {
					temperature = <60000>;	/* m°C */
					hysteresis = <2000>;
					type = "passive";
				};
				i4_fanmax: fanmax {
					temperature = <80000>;
					hysteresis = <5000>;
					type = "active";
				};
				i4_crit: crit {
					temperature = <95000>;
					hysteresis = <0>;
					type = "critical";
				};
			};

			cooling-maps {
				map0 {
					trip = <&i4_warm>;
					cooling-device = <&i4_fan 0 1>;
				};
				map1 {
					trip = <&i4_fanmax>;
					cooling-device = <&i4_fan 2 3>, <&i4_gfan 1 3>;
				};
			};
		};
	};
};
```

빌드 스크립트. `cpp`로 `#include`와 매크로(`I4_POLL_MS`)를 풀고 `dtc`로 컴파일한다. `-DI4_POLL_MS=0`을 주면 polling 없는 인터럽트 방식 dtb가 나온다(§5).

```sh
#!/bin/sh
# mkdtb.sh <name> [-DI4_POLL_MS=..] -> /var/tmp/I4/<name>.dtb
set -e
W=/var/tmp/I4; K=/var/lab/src/linux-6.18.55/include; N=$1; shift
cp /var/lab/out/virt.dts $W/i4_virt.dts
echo '#include "/Users/donh/.elinux_scratch/I4/i4_nodes.dtsi"' >> $W/i4_virt.dts
cpp -nostdinc -I $K -undef -x assembler-with-cpp "$@" \
    $W/i4_virt.dts > $W/$N.pp.dts
dtc -q -I dts -O dtb -o $W/$N.dtb $W/$N.pp.dts
```

```sh
sh /Users/donh/.elinux_scratch/I4/mkdtb.sh i4_poll
sh /Users/donh/.elinux_scratch/I4/mkdtb.sh i4_irq -DI4_POLL_MS=0
grep -A2 "polling-delay-passive" /var/tmp/I4/i4_irq.pp.dts
```

```text
   polling-delay-passive = <250>;
   polling-delay = <0>;
   thermal-sensors = <&i4_sensor 0>;
```

출력에서 볼 것: dtc가 경고 없이 끝났고, 두 번째 dtb에는 `polling-delay = <0>`이 들어갔다. 이 DT에서 기억할 규칙 몇 가지:

- `thermal-zones`는 루트 바로 아래 노드 이름으로 찾는다(`of_find_node_by_name(NULL, "thermal-zones")`). 그 아래 **모든** 존은 `thermal-sensors`를 가져야 한다. 하나라도 없으면 `missing thermal sensor`로 존 등록 전체가 실패한다.
- 존의 이름(`i4-soc`)이 그대로 zone의 `type`이 된다. 유저 공간 도구는 `thermal_zoneN`의 N이 아니라 이 type으로 존을 찾는다(N은 등록 순서라 부팅마다 바뀔 수 있다).
- trip에는 `temperature`(m°C), `hysteresis`, `type`이 다 필요하다. hysteresis가 없으면 `missing hysteresis property`로 실패한다.
- `cooling-device = <&i4_fan 2 3>`의 두 숫자는 **이 trip에서 쓸 state의 하한·상한**이다(`#cooling-cells = <2>`). `THERMAL_NO_LIMIT`(dt-bindings 헤더)를 쓰면 "제한 없음"이다.
- `#thermal-sensor-cells = <1>`이면 phandle 뒤 숫자가 센서 id다. hwmon 경유 등록에서는 이 id가 **temp 채널 인덱스(0부터)**다.

### 1.4 예제 2 — 빌드 · 부팅 스크립트

무엇을 확인하는 코드인가: 커널별로 모듈을 빌드하고, 랩 rootfs를 복사해 `.ko`·정적 바이너리·시나리오 스크립트를 넣고, `LAB_CMD`로 시나리오 하나를 돌린 뒤 꺼지는 흐름이다.

```make
obj-m := i4_temp.o i4_batt.o
```

```sh
cd /var/tmp/I4/sub && make -C /var/lab/out/linux-build-sub M=$PWD -j2 modules
cd /var/tmp/I4/dbg && make -C /var/lab/out/linux-build-dbg M=$PWD -j2 modules
```

```text
make: Entering directory '/var/lab/build/linux-sub'
make[1]: Entering directory '/var/tmp/I4/sub'
  CC [M]  i4_temp.o
  CC [M]  i4_batt.o
  MODPOST Module.symvers
  CC [M]  i4_temp.mod.o
  CC [M]  .module-common.o
  CC [M]  i4_batt.mod.o
  LD [M]  i4_temp.ko
  LD [M]  i4_batt.ko
make[1]: Leaving directory '/var/tmp/I4/sub'
make: Leaving directory '/var/lab/build/linux-sub'
```

(위는 sub 빌드 출력이다. dbg 빌드도 경고 없이 끝났고 `modinfo -F vermagic i4_temp.ko`가 `6.18.55-dbg SMP preempt mod_unload aarch64`를 보였다.)

```sh
#!/bin/sh
# boot.sh <sub|dbg> <dtb name> <scenario> [extra kernel args]
set -e
W=/var/tmp/I4; S=/Users/donh/.elinux_scratch/I4; K=$1; D=$2; SC=$3; shift 3
M=/var/lab/out/modules-sub/lib/modules/6.18.55-sub/kernel/drivers
rm -rf $W/r_$K && mkdir $W/r_$K && cp -a /var/lab/build/rootfs/. $W/r_$K/
cp $W/$K/i4_temp.ko $W/$K/i4_batt.ko $W/bin/* $S/i4.sh $W/r_$K/; cp $S/i4_poweroff.sh $W/r_$K/sbin/i4_poweroff; chmod +x $W/r_$K/sbin/i4_poweroff
[ $K = sub ] && cp $M/hwmon/gpio-fan.ko $M/power/supply/test_power.ko $W/r_$K/
(cd $W/r_$K && find . | cpio -o -H newc --quiet | gzip) > $W/i_$K.gz
IMG=/var/lab/out/Image-$K
timeout ${TMO:-300} qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel $IMG -initrd $W/i_$K.gz -dtb $W/$D.dtb \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/i4.sh~$SC $*"
```

게스트에서 쓸 유저 공간 도구 두 개(`i4_log`: sysfs 파일들을 일정 주기로 찍는 로거, `i4_uevent`: netlink uevent 리스너)는 VM에서 `gcc -std=gnu11 -Wall -Wextra -O2 -static`으로 빌드해 경고 없이 넣었다. 게스트는 BusyBox뿐이라 `lm-sensors`도 `udevadm`도 없다. 그래서 둘 다 직접 짠다. 시나리오 스크립트 `i4.sh`는 각 절에서 해당 부분만 보인다.

---

## 2. hwmon — "지금 몇 도, 몇 볼트인가"를 표준 단위로

### 2.1 직관과 정의

**hwmon**(hardware monitoring)은 온도·전압·전류·전력·에너지·습도·팬 속도·PWM 같은 **헬스 모니터링 값**을 보고하는 서브시스템이다. 장치 하나가 `/sys/class/hwmon/hwmonN/` 디렉터리 하나가 되고, 그 안에 `temp1_input`, `in0_input`처럼 **이름 규칙과 단위가 고정된** 파일이 생긴다. 그래서 `lm-sensors`의 `sensors` 명령 하나가 어떤 칩이든 읽을 수 있다.

bare-metal로 말하면 "칩마다 다른 레지스터 포맷(12비트 2의 보수, LSB = 0.0625 °C …)을 드라이버 안에서 전부 정수 m°C로 바꿔, 정해진 이름의 변수에 넣어 두는 규칙"이다. 소비자(유저 공간, thermal 프레임워크)는 칩을 몰라도 된다.

hwmon은 IIO(I2)와 겹쳐 보이지만 목적이 다르다. IIO는 **빠른 샘플 스트림**(IMU 1 kHz, 버퍼·트리거·타임스탬프)용이고, hwmon은 **느리게 변하는 상태 값**(초 단위로 읽는 보드 온도·레일 전압)용이다. 같은 ADC 칩이 둘 다로 노출되기도 하고, `iio-hwmon` 브리지로 IIO 채널을 hwmon으로 보이게 하기도 한다.

### 2.2 sysfs ABI — 이름과 단위가 전부다

| 종류 | 파일 예 | 단위 | 흔한 속성 |
|---|---|---|---|
| 온도 | `temp1_input` | m°C (밀리도) | `_max`, `_min`, `_crit`, `_max_alarm`, `_label`, `_crit_hyst` |
| 전압 | `in0_input` | mV | `_min`, `_max`, `_label`, `_alarm` |
| 전류 | `curr1_input` | mA | `_max`, `_crit` |
| 전력 | `power1_input` | µW | `_cap`, `_average` |
| 에너지 | `energy1_input` | µJ | 누적값 |
| 팬 | `fan1_input` | RPM | `_target`, `_min`, `_alarm` |
| PWM | `pwm1` | 0~255 | `pwm1_enable` |
| 칩 이름 | `name` | 문자열 | 공백·하이픈 없는 짧은 이름 |

인덱스 규칙도 고정이다. `temp`·`curr`·`power`·`fan`은 **1부터**, `in`(전압)만 **0부터** 센다. 드라이버 안의 채널 번호는 모두 0부터지만 hwmon 코어가 이름을 만들 때 이 규칙을 적용한다. `_alarm` 파일은 0/1이고, 칩이 ALERT 핀을 가지면 드라이버가 `hwmon_notify_event()`로 유저 공간에 poll 이벤트와 uevent를 보낸다(§5.3).

### 2.3 등록 API — `devm_hwmon_device_register_with_info`

옛 드라이버는 `DEVICE_ATTR`를 수십 개 손으로 선언하고 `hwmon_device_register()`를 불렀다. 지금 새 드라이버는 **채널 정보 표 + ops 네 개**를 넘기고 sysfs 파일은 코어가 만든다.

| 구조 | 역할 |
|---|---|
| `struct hwmon_channel_info` | 센서 종류(`hwmon_temp` …) 하나와, 채널마다 "어떤 속성이 있나" 비트마스크 배열 |
| `HWMON_CHANNEL_INFO(temp, 채널0 비트, 채널1 비트, …)` 매크로 | 위 구조를 한 줄로 만든다 (인자 하나가 채널 하나, 비트는 `HWMON_T_INPUT` 등의 OR) |
| `struct hwmon_chip_info` | `.ops`와 `.info`(위 표들의 NULL 종료 배열) |
| `struct hwmon_ops` | `.is_visible`(또는 고정 모드 `.visible`), `.read`, `.write`, `.read_string` |

`hwmon_chip` 타입의 `HWMON_C_REGISTER_TZ` 비트가 이 노트의 연결 고리다. 이 비트가 있고 장치에 DT 노드가 있으면, hwmon 코어가 `HWMON_T_INPUT`을 가진 **모든 temp 채널을 `devm_thermal_of_zone_register()`로 thermal 센서로 등록**한다(`drivers/hwmon/hwmon.c`의 `hwmon_thermal_register_sensors`). 드라이버는 thermal API를 한 줄도 안 불러도 thermal zone의 센서가 된다.

### 2.4 예제 3 — 다이 모델과 "샘플링 타이머" (`i4_temp` 1/5)

무엇을 확인하는 코드인가: 모듈 파라미터 `load`가 소비 전력을 정하고, 100 ms 지연 작업이 1차 열 모델을 한 걸음씩 진행한다. 진짜 칩이라면 이 자리가 "변환 완료를 기다렸다가 레지스터를 읽는" 코드다. `irq_mode`는 §5에서 쓰는 가짜 ALERT 인터럽트다.

```c
// SPDX-License-Identifier: GPL-2.0
/* i4_temp: simulated SoC die sensor (hwmon) + thermal zone + fake fan cdev */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>
#include <linux/of.h>

static int load;		/* CPU load 0..100 % -> heating power */
module_param(load, int, 0644);
static bool irq_mode;		/* true: "alert IRQ" when temp leaves window */
module_param(irq_mode, bool, 0644);

#define T_AMB	35000		/* ambient, m°C */
#define TICK_MS	100		/* model step */
#define TAU_TICKS 40		/* time constant 4 s */
static const int rth[4] = { 22, 16, 12, 9 };	/* °C/W per fan state */

struct i4 {
	struct device *hwmon;
	struct delayed_work tick;
	struct mutex lock;
	long temp, tmin, tmax;	/* m°C; tmin/tmax = alert window */
	unsigned long fan;	/* cooling state 0..3 */
	bool alerted;
};

static long i4_power_mw(void)	/* 0.2 W idle + 2.8 W at 100 % */
{
	return 200 + 28 * clamp(load, 0, 100);
}

static void i4_tick(struct work_struct *w)
{
	struct i4 *p = container_of(to_delayed_work(w), struct i4, tick);
	bool fire = false;
	long tss;

	mutex_lock(&p->lock);
	tss = T_AMB + i4_power_mw() * rth[p->fan];	/* mW x °C/W = m°C */
	p->temp += (tss - p->temp) / TAU_TICKS;
	if (irq_mode) {
		bool out = p->temp >= p->tmax || p->temp < p->tmin;

		fire = out && !p->alerted;	/* edge, like an ALERT pin */
		p->alerted = out;
	}
	mutex_unlock(&p->lock);
	if (fire)	/* sysfs_notify + uevent + thermal_zone_device_update */
		hwmon_notify_event(p->hwmon, hwmon_temp, hwmon_temp_max_alarm, 0);
	schedule_delayed_work(&p->tick, msecs_to_jiffies(TICK_MS));
}
```

출력은 §2.6에서 본다. 볼 것: 상태(`temp`, `fan`, 경보 창 `tmin`/`tmax`)는 모두 `p->lock`(mutex) 아래에 있고, `hwmon_notify_event()`는 **락을 놓은 뒤** 부른다. 이 호출이 thermal 코어로 들어가 다시 `.read`(→ `p->lock`)를 부르기 때문이다. 락을 쥔 채 부르면 자기 자신과 데드락이다.

### 2.5 예제 4 — hwmon ops와 채널 표 (`i4_temp` 2/5, 3/5)

무엇을 확인하는 코드인가: 채널 다섯 종류(temp·in·curr·power·fan)를 선언하고, `.read`가 각 단위로 값을 돌려주며, `temp1_min`/`temp1_max`만 쓰기 가능하게 연다.

```c
/* ---- part 2: hwmon ops ---- */
static umode_t i4_is_visible(const void *d, enum hwmon_sensor_types type,
			     u32 attr, int ch)
{
	if (type == hwmon_temp && (attr == hwmon_temp_min || attr == hwmon_temp_max))
		return 0644;	/* thermal core programs the window here */
	return 0444;
}

static int i4_read(struct device *dev, enum hwmon_sensor_types type,
		   u32 attr, int ch, long *val)
{
	struct i4 *p = dev_get_drvdata(dev);
	long mw = i4_power_mw(), mv = 900 - 40 * clamp(load, 0, 100) / 100;

	guard(mutex)(&p->lock);
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input: *val = p->temp; return 0;
		case hwmon_temp_min: *val = p->tmin; return 0;
		case hwmon_temp_max: *val = p->tmax; return 0;
		case hwmon_temp_crit: *val = 95000; return 0;
		case hwmon_temp_max_alarm: *val = p->temp >= p->tmax; return 0;
		}
		break;
	case hwmon_in:	 *val = mv; return 0;			/* mV */
	case hwmon_curr: *val = mw * 1000 / mv; return 0;	/* mA */
	case hwmon_power: *val = mw * 1000; return 0;		/* µW */
	case hwmon_fan:	 *val = p->fan * 1500; return 0;	/* RPM */
	default: break;
	}
	return -EOPNOTSUPP;
}
```

```c
static int i4_write(struct device *dev, enum hwmon_sensor_types type,
		    u32 attr, int ch, long val)
{
	struct i4 *p = dev_get_drvdata(dev);

	guard(mutex)(&p->lock);
	if (attr == hwmon_temp_min)
		p->tmin = val;
	else if (attr == hwmon_temp_max)
		p->tmax = val;
	else
		return -EOPNOTSUPP;
	p->alerted = false;	/* re-arm the edge detector */
	return 0;
}

static int i4_read_string(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int ch, const char **s)
{
	*s = type == hwmon_temp ? "soc_die" : "vdd_cpu";
	return 0;
}

static const struct hwmon_ops i4_hwmon_ops = {
	.is_visible = i4_is_visible, .read = i4_read,
	.write = i4_write, .read_string = i4_read_string,
};
static const struct hwmon_channel_info * const i4_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_MIN |
			   HWMON_T_MAX | HWMON_T_CRIT | HWMON_T_MAX_ALARM),
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr, HWMON_C_INPUT),
	HWMON_CHANNEL_INFO(power, HWMON_P_INPUT),
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT),
	NULL
};
static const struct hwmon_chip_info i4_chip = { .ops = &i4_hwmon_ops, .info = i4_info };
```

볼 것:

- `.read`의 단위 변환이 드라이버의 진짜 일이다: 온도 m°C, 전압 mV, 전류 mA, 전력 **µW**(mW에 1000을 곱했다), 팬 RPM. 실제 칩이면 이 자리에 `regmap_read()`(H2)와 레지스터 포맷 변환이 들어간다.
- `.is_visible`은 파일 권한을 정한다. 0을 돌려주면 그 파일 자체가 안 생긴다. 보드마다 채널 일부만 배선된 칩(INA3221의 3채널 중 2개만 쓰는 경우)이 이것으로 빈 채널을 숨긴다.
- `temp1_min`/`temp1_max`를 쓰기 가능하게 연 이유는 §5에서 나온다. hwmon 경유 thermal zone은 다음 trip 경계를 `temp_min`/`temp_max`에 **써 넣는다**(`hwmon_thermal_set_trips`). 실제 TMP102·LM75 드라이버에서 이 값이 칩의 ALERT 한계 레지스터로 들어간다.
- `.read_string`은 `_label` 파일용이다. 보드 회로도의 이름(`soc_die`, `vdd_cpu`)을 붙이면 현장에서 "hwmon3의 in0가 뭐였지?"를 안 묻게 된다.

### 2.6 예제 5 — sysfs에서 읽어 보기

무엇을 확인하는 코드인가: 모듈을 넣고 2초 뒤(idle, load 0) hwmon 디렉터리를 훑는다. `i4.sh`의 `walk` 시나리오다.

```sh
walk)
  insmod /gpio-fan.ko; insmod /i4_temp.ko; findtz; sleep 2
  echo "### hwmon devices"; for h in /sys/class/hwmon/hwmon*; do echo "$h name=$(cat $h/name)"; done
  H=$(grep -l '^i4_temp$' /sys/class/hwmon/hwmon*/name | xargs dirname)
  echo "### $H"; ls $H | tr "\n" " "; echo
  cd $H; for f in temp1_label temp1_input temp1_min temp1_max temp1_crit temp1_max_alarm in0_label in0_input curr1_input power1_input fan1_input; do printf "%-16s %s\n" $f "$(cat $f)"; done; cd /
  echo "### $TZ"; ls $TZ | tr "\n" " "; echo
  cd $TZ; for f in type temp mode policy available_policies polling_delay passive_delay; do [ -e $f ] && printf "%-20s %s\n" $f "$(cat $f)"; done
  for i in 0 1 2; do echo "trip_point_$i: $(cat trip_point_${i}_type) $(cat trip_point_${i}_temp) hyst=$(cat trip_point_${i}_hyst)"; done
  for c in cdev*_trip_point; do n=${c%_trip_point}; echo "$n -> $(cat $n/type) trip=$(cat $c) weight=$(cat ${n}_weight)"; done; cd /
  echo "### cooling devices"; for c in /sys/class/thermal/cooling_device*; do echo "$c type=$(cat $c/type) max=$(cat $c/max_state) cur=$(cat $c/cur_state)"; done
  echo "### $CD"; ls $CD | tr "\n" " "; echo; ls $CD/stats | tr "\n" " "; echo
  ;;
```

```text
lab initramfs: 6.18.55-sub up 0.98s
### hwmon devices
/sys/class/hwmon/hwmon0 name=gpio_fan
/sys/class/hwmon/hwmon1 name=i4_temp
### /sys/class/hwmon/hwmon1
curr1_input device fan1_input in0_input in0_label name of_node power power1_input subsystem temp1_crit temp1_input temp1_label temp1_max temp1_max_alarm temp1_min uevent 
temp1_label      soc_die
temp1_input      36740
temp1_min        -2147483647
temp1_max        60000
temp1_crit       95000
temp1_max_alarm  0
in0_label        vdd_cpu
in0_input        900
curr1_input      222
power1_input     200000
fan1_input       0
```

(같은 부팅의 뒷부분인 thermal 출력은 §3.5에 있다.)

출력에서 볼 것:

- `temp1_input 36740` = 36.74 °C. idle 0.2 W × 22 °C/W = 4.4 °C만큼 주변(35 °C)보다 높은 정상 온도(39.4 °C)로 올라가는 중이다.
- `curr1_input 222` mA = 200 mW / 0.9 V, `power1_input 200000` µW = 0.2 W. 단위가 섞이지 않게 하는 것이 드라이버 책임이다.
- **`temp1_min -2147483647`, `temp1_max 60000`**: 드라이버가 정한 값이 아니다. probe에서 넣은 초기값은 −273000/200000이었다. thermal zone이 등록되자마자 코어가 "다음 경계는 아래로는 없음(−INT_MAX), 위로는 60 °C"를 `set_trips`로 써 넣은 것이다. hwmon과 thermal이 이미 연결되어 있다는 첫 증거다.
- `hwmon0`은 `gpio_fan`이다. 업스트림 gpio-fan 드라이버도 hwmon 장치를 만든다(`fan1_target`, `pwm1` 등 — §8).

### 2.7 함정

- **단위 실수**: 온도를 °C로(36), 전력을 mW로 보고하는 드라이버는 리뷰에서 바로 반려된다. `sensors`가 0.036 °C로 표시하고 thermal trip은 영원히 안 걸린다.
- **`hwmon_device_register()`(옛 API) 사용**: 경고가 나고 `name` 속성 규칙을 못 지킨다. 새 드라이버는 `_with_info`.
- **`name`에 하이픈**: `name`은 공백·하이픈·와일드카드 문자가 없어야 한다. 그래서 `i4-temp`가 아니라 `i4_temp`로 지었다(코어가 경고한다).
- **`.read`에서 버스 I/O를 매번**: 유저 공간이 1초에 여러 번 읽으면 I2C가 바빠진다. 실제 드라이버는 마지막 읽은 시각을 기억해 변환 주기 안에서는 캐시를 돌려준다(`update_interval` 속성).
- **hwmon_notify_event를 락 안에서**: 위 §2.4에서 본 데드락.

---

## 3. thermal 프레임워크 — zone · trip · cooling device · governor

### 3.1 네 개의 낱말

| 낱말 | 정의 | 펌웨어 비유 |
|---|---|---|
| **thermal zone** | 온도 하나(센서 하나, 또는 여러 센서의 집계)와 그에 딸린 정책 묶음. `/sys/class/thermal/thermal_zoneN` | "다이 온도 관리 태스크" 하나 |
| **trip point** | 그 존에서 "이 온도를 넘으면 무언가 한다"는 경계. 온도·hysteresis·종류 | 단계별 throttle 표의 한 줄 |
| **cooling device** | 0~max_state 정수 하나로 세기를 조절하는 냉각 수단. `/sys/class/thermal/cooling_deviceN` | 팬 PWM 단계, NAND 병렬도 단계, CPU 클럭 단계 |
| **governor** | trip을 넘었을 때 각 cooling device의 state를 몇으로 할지 정하는 알고리즘 | throttle 단계를 올리고 내리는 로직 |

그리고 **cooling map**(바인딩)이 trip과 cooling device를 잇는다. "이 trip을 넘으면 이 장치를 state a~b 범위에서 써라." 커널 안에서는 이 연결 하나하나가 `struct thermal_instance`다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" text-anchor="middle" font-size="13">zone i4-soc: trip 3개, 각 trip의 hysteresis, 그리고 cooling-maps</text>
<line x1="70" y1="40" x2="70" y2="350" stroke="currentColor" marker-start="url(#ah2)"/>
<text x="62" y="54" text-anchor="end" font-size="12">°C</text>
<text x="62" y="84" text-anchor="end" font-size="12">95</text>
<line x1="66" y1="80" x2="250" y2="80" stroke="#d0564a" stroke-width="2"/>
<text x="62" y="150" text-anchor="end" font-size="12">80</text>
<line x1="66" y1="146" x2="250" y2="146" stroke="#e08a3c" stroke-width="2"/>
<text x="62" y="172" text-anchor="end" font-size="12">75</text>
<line x1="66" y1="168" x2="250" y2="168" stroke="#e08a3c" stroke-dasharray="4 3"/>
<rect x="72" y="146" width="178" height="22" fill="#e08a3c" fill-opacity="0.12"/>
<text x="62" y="236" text-anchor="end" font-size="12">60</text>
<line x1="66" y1="232" x2="250" y2="232" stroke="#3f9a6b" stroke-width="2"/>
<text x="62" y="250" text-anchor="end" font-size="12">58</text>
<line x1="66" y1="246" x2="250" y2="246" stroke="#3f9a6b" stroke-dasharray="4 3"/>
<rect x="72" y="232" width="178" height="14" fill="#3f9a6b" fill-opacity="0.12"/>
<text x="62" y="334" text-anchor="end" font-size="12">35</text>
<text x="160" y="74" text-anchor="middle" font-size="12">crit: 넘는 순간 1회</text>
<text x="160" y="141" text-anchor="middle" font-size="12">active 80 / hyst 5</text>
<text x="160" y="186" text-anchor="middle" font-size="12">내려올 땐 75에서 해제</text>
<text x="160" y="227" text-anchor="middle" font-size="12">passive 60 / hyst 2</text>
<text x="160" y="264" text-anchor="middle" font-size="12">내려올 땐 58에서 해제</text>
<rect x="300" y="58" width="370" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="312" y="76" font-size="12">critical → ops.critical → hw_protection</text>
<text x="312" y="93" font-size="12">→ orderly_poweroff() → /sbin/poweroff (§6)</text>
<rect x="300" y="124" width="370" height="64" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="312" y="143" font-size="12">map1: trip = &lt;&amp;i4_fanmax&gt;</text>
<text x="312" y="161" font-size="12">  i4-fan    state 2..3   (lower 2, upper 3)</text>
<text x="312" y="179" font-size="12">  gpio-fan  state 1..3</text>
<rect x="300" y="210" width="370" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="312" y="229" font-size="12">map0: trip = &lt;&amp;i4_warm&gt;</text>
<text x="312" y="247" font-size="12">  i4-fan    state 0..1   (passive: 250 ms 재폴링)</text>
<line x1="252" y1="80" x2="298" y2="80" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="252" y1="150" x2="298" y2="150" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="252" y1="236" x2="298" y2="236" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="300" y="280" width="370" height="64" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="312" y="299" font-size="12">한 cdev가 여러 trip에 묶이면:</text>
<text x="312" y="317" font-size="12">instance마다 target을 계산하고</text>
<text x="312" y="335" font-size="12">cdev는 그중 가장 큰 target으로 간다</text>
</svg>
```
그림 2 — 이 노트의 존 `i4-soc`. trip마다 올라갈 때 걸리는 온도와 내려올 때 풀리는 온도(trip − hysteresis)가 다르고, cooling map이 trip별로 쓸 state 범위를 정한다.

### 3.2 trip 종류

| type | 넘으면 코어가 하는 일 | 쓰임 |
|---|---|---|
| `passive` | `tz->passive++` → 이제 `polling-delay-passive` 주기로 빠르게 폴링, governor 호출 | 성능을 깎는 냉각 (CPU 주파수, GPU devfreq). "조용한" 냉각 |
| `active` | governor 호출 (폴링 주기는 그대로) | 팬처럼 에너지를 써서 식히는 냉각 |
| `hot` | 드라이버가 `ops.hot`을 줬으면 그것을 부른다. 아니면 알림뿐 | "곧 critical"을 알리고 플랫폼이 무언가 하게 함 |
| `critical` | `ops.critical` → 기본은 `thermal_zone_device_critical()` → `hw_protection` → `orderly_poweroff()` | 하드웨어 보호 종료 (§7) |

passive와 active의 차이는 커널 입장에서 사실상 **폴링 주기**뿐이다. step_wise는 둘을 똑같이 다룬다. 이 이름은 ACPI 시절의 개념(passive = 성능 저하, active = 팬)에서 왔고, DT 작성자는 그 관례를 따른다. hot과 critical은 governor를 거치지 않는다(`step_wise_manage`가 건너뛴다).

hysteresis는 위로 넘을 때는 쓰이지 않고, **내려올 때만** 쓰인다. 커널 코드에서는 trip마다 `threshold`가 있어서 아직 안 넘은 trip은 `threshold = temperature`, 이미 넘은 trip은 `threshold = temperature − hysteresis`가 된다(`drivers/thermal/thermal_core.c`). 이것이 §2.6에서 본 `temp1_min`/`temp1_max` 값의 정체다(§5에서 57999, 74999가 나온다).

### 3.3 DT `thermal-zones` — 배선도

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" text-anchor="middle" font-size="13">DT thermal-zones: 존 노드는 센서와 냉각 장치를 phandle로 가리키기만 한다</text>
<rect x="10" y="36" width="390" height="352" rx="8" fill="#888" fill-opacity="0.06" stroke="#888"/>
<text x="22" y="56" font-size="13">/thermal-zones</text>
<rect x="24" y="66" width="364" height="312" rx="6" fill="#d0564a" fill-opacity="0.08" stroke="#d0564a"/>
<text x="36" y="86" font-size="13">i4-soc  (→ zone type "i4-soc")</text>
<text x="48" y="106" font-size="12">polling-delay-passive = &lt;250&gt;</text>
<text x="48" y="124" font-size="12">polling-delay = &lt;1000&gt;   (0 = 인터럽트만)</text>
<text x="48" y="142" font-size="12">thermal-sensors = &lt;&amp;i4_sensor 0&gt;</text>
<rect x="40" y="154" width="334" height="92" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="52" y="172" font-size="12">trips</text>
<text x="64" y="190" font-size="12">i4_warm: warm    60000 / 2000 / passive</text>
<text x="64" y="208" font-size="12">i4_fanmax: fanmax 80000 / 5000 / active</text>
<text x="64" y="226" font-size="12">i4_crit: crit    95000 / 0 / critical</text>
<rect x="40" y="258" width="334" height="108" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="52" y="276" font-size="12">cooling-maps</text>
<text x="64" y="296" font-size="12">map0: trip = &lt;&amp;i4_warm&gt;</text>
<text x="80" y="314" font-size="12">cooling-device = &lt;&amp;i4_fan 0 1&gt;</text>
<text x="64" y="336" font-size="12">map1: trip = &lt;&amp;i4_fanmax&gt;</text>
<text x="80" y="354" font-size="12">&lt;&amp;i4_fan 2 3&gt;, &lt;&amp;i4_gfan 1 3&gt;</text>
<rect x="450" y="80" width="220" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="462" y="100" font-size="12">i4_sensor: i4-temp</text>
<text x="462" y="118" font-size="12">#thermal-sensor-cells = &lt;1&gt;</text>
<text x="462" y="136" font-size="12">드라이버: i4_temp (hwmon)</text>
<rect x="450" y="180" width="220" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="462" y="200" font-size="12">i4_fan: fan (i4-temp의 자식)</text>
<text x="462" y="218" font-size="12">#cooling-cells = &lt;2&gt;</text>
<rect x="450" y="266" width="220" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="462" y="286" font-size="12">i4_gfan: i4-gpio-fan</text>
<text x="462" y="304" font-size="12">gpio-fan + gpio-sim 2핀</text>
<line x1="300" y1="138" x2="448" y2="112" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="330" y1="310" x2="448" y2="214" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="374" y1="350" x2="448" y2="300" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="560" y="350" text-anchor="middle" font-size="12">&lt;phandle min max&gt;:</text>
<text x="560" y="368" text-anchor="middle" font-size="12">이 trip에서 쓸 state 범위</text>
</svg>
```
그림 3 — §1.3 DT의 구조. 존 노드는 센서와 냉각 장치를 phandle로 가리킬 뿐이고, 각 드라이버는 자기 노드만 안다.

등록 순서도 알아 두자. **존은 센서 드라이버가 `devm_thermal_of_zone_register(dev, id, data, ops)`를 부를 때 만들어진다.** DT에 존이 적혀 있어도 센서 드라이버가 probe되지 않으면 존은 없다. cooling device는 먼저 등록되든 나중에 등록되든 상관없다. 코어가 등록 시점마다 모든 존의 cooling-maps를 훑어서 맞는 짝을 묶는다(`thermal_of_should_bind`). 그래서 gpio-fan 모듈을 i4_temp보다 늦게 넣어도 바인딩된다.

### 3.4 예제 6 — cooling device와 probe (`i4_temp` 4/5, 5/5)

무엇을 확인하는 코드인가: cooling device는 `get_max_state`·`get_cur_state`·`set_cur_state` 세 콜백이 전부다. state는 0(냉각 없음)부터 max(최대 냉각)까지의 정수이고, 그 의미(PWM 듀티, 주파수 단계)는 드라이버만 안다.

```c
/* ---- part 3: cooling device + probe ---- */
static int fan_get_max(struct thermal_cooling_device *c, unsigned long *s)
{
	*s = ARRAY_SIZE(rth) - 1;
	return 0;
}

static int fan_get_cur(struct thermal_cooling_device *c, unsigned long *s)
{
	struct i4 *p = c->devdata;

	*s = READ_ONCE(p->fan);
	return 0;
}

static int fan_set_cur(struct thermal_cooling_device *c, unsigned long s)
{
	struct i4 *p = c->devdata;

	if (s >= ARRAY_SIZE(rth))
		return -EINVAL;
	mutex_lock(&p->lock);
	p->fan = s;	/* real HW: write PWM duty / fan controller reg */
	mutex_unlock(&p->lock);
	return 0;
}

static const struct thermal_cooling_device_ops i4_fan_ops = {
	.get_max_state = fan_get_max, .get_cur_state = fan_get_cur,
	.set_cur_state = fan_set_cur,
};

static void i4_stop(void *data)
{
	cancel_delayed_work_sync(data);
}
```

```c
static int i4_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *fan_np;
	struct thermal_cooling_device *cdev;
	struct i4 *p;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	mutex_init(&p->lock);
	p->temp = T_AMB;
	p->tmin = -273000;
	p->tmax = 200000;
	INIT_DELAYED_WORK(&p->tick, i4_tick);

	fan_np = of_get_child_by_name(dev->of_node, "fan");
	cdev = devm_thermal_of_cooling_device_register(dev, fan_np, "i4-fan", p,
						       &i4_fan_ops);
	of_node_put(fan_np);
	if (IS_ERR(cdev))
		return dev_err_probe(dev, PTR_ERR(cdev), "cooling device\n");

	/* registers hwmonN and, because of HWMON_C_REGISTER_TZ, the zone too */
	p->hwmon = devm_hwmon_device_register_with_info(dev, "i4_temp", p,
							&i4_chip, NULL);
	if (IS_ERR(p->hwmon))
		return PTR_ERR(p->hwmon);
	schedule_delayed_work(&p->tick, msecs_to_jiffies(TICK_MS));
	return devm_add_action_or_reset(dev, i4_stop, &p->tick);
}

static const struct of_device_id i4_of[] = { { .compatible = "i4,temp" }, { } };
MODULE_DEVICE_TABLE(of, i4_of);
static struct platform_driver i4_drv = {
	.probe = i4_probe,
	.driver = { .name = "i4_temp", .of_match_table = i4_of },
};
module_platform_driver(i4_drv);
MODULE_DESCRIPTION("I4 simulated die sensor + fan cooling device");
MODULE_LICENSE("GPL");
```

볼 것:

- `devm_thermal_of_cooling_device_register(dev, fan_np, "i4-fan", p, &ops)`의 `fan_np`가 DT의 `i4_fan: fan` 노드다. cooling-maps의 `<&i4_fan 2 3>`은 **이 노드 포인터**로 매칭된다. 이름 문자열 `"i4-fan"`은 sysfs `type`에 보이는 표시용일 뿐이다.
- 한 드라이버가 센서와 cooling device를 둘 다 가질 필요는 없다. 실제 보드에서는 센서(SoC TSENS 드라이버)와 팬(pwm-fan)과 CPU 냉각(cpufreq-dt)이 전부 다른 드라이버다. 여기서는 열 모델이 팬 state를 알아야 해서 한 모듈에 넣었다.
- `set_cur_state`는 **잠들 수 있는 문맥**(thermal 코어의 kworker, `tz->lock` mutex 아래)에서 불린다. I2C 팬 컨트롤러에 써도 된다.
- devm 해제 순서는 등록의 역순이다: `i4_stop`(타이머 정지) → hwmon(+zone) 해제 → cooling device 해제. 존이 사라진 뒤에 cooling device가 사라지므로 해제 중 `set_cur_state`가 사라진 데이터를 만지는 일이 없다.

### 3.5 예제 7 — thermal sysfs 훑기

무엇을 확인하는 코드인가: §2.6과 같은 부팅(`walk`)의 뒷부분이다. 존의 trip·바인딩, cooling device의 통계 파일을 본다.

```text
### /sys/class/thermal/thermal_zone0
available_policies cdev0 cdev0_trip_point cdev0_weight cdev1 cdev1_trip_point cdev1_weight cdev2 cdev2_trip_point cdev2_weight emul_temp integral_cutoff k_d k_i k_po k_pu mode offset policy power slope subsystem sustainable_power temp trip_point_0_hyst trip_point_0_temp trip_point_0_type trip_point_1_hyst trip_point_1_temp trip_point_1_type trip_point_2_hyst trip_point_2_temp trip_point_2_type type uevent 
type                 i4-soc
temp                 36870
mode                 enabled
policy               step_wise
available_policies   power_allocator step_wise 
trip_point_0: passive 60000 hyst=2000
trip_point_1: active 80000 hyst=5000
trip_point_2: critical 95000 hyst=0
cdev0 -> i4-fan trip=0 weight=0
cdev1 -> i4-fan trip=1 weight=0
cdev2 -> gpio-fan trip=1 weight=0
### cooling devices
/sys/class/thermal/cooling_device0 type=gpio-fan max=3 cur=0
/sys/class/thermal/cooling_device1 type=i4-fan max=3 cur=0
### /sys/class/thermal/cooling_device1
cur_state max_state power stats subsystem type uevent 
reset time_in_state_ms total_trans trans_table 
```

출력에서 볼 것:

- 존의 `cdev0`, `cdev1`, `cdev2`는 **cooling device가 아니라 instance(바인딩)**다. i4-fan은 trip 0과 trip 1에 각각 묶여 두 번 나온다. 그림 2의 cooling-maps 그대로다.
- `weight=0`: DT에 `contribution`을 안 줬기 때문이다. weight는 fair_share·power_allocator가 쓰고 step_wise는 무시한다.
- `k_po`, `k_pu`, `k_i`, `k_d`, `integral_cutoff`, `sustainable_power`는 power_allocator(PID 제어기)의 파라미터다. 정책이 step_wise여도 파일은 있다.
- `polling_delay`·`passive_delay` 파일은 이 커널에 없다(스크립트의 `[ -e ]`가 걸렀다). 폴링 주기는 DT에서만 보인다.
- 존 `temp` 36870과 §2.6의 `temp1_input` 36740이 다른 이유: 두 값을 읽은 시각이 다르다(그사이 모델이 진행했다). 존 `temp` 읽기는 캐시가 아니라 매번 `get_temp` → hwmon `.read`를 부른다.
- cooling device의 `stats/`는 `CONFIG_THERMAL_STATISTICS=y` 덕분이다. §4에서 이 통계로 제어 루프를 요약한다.

### 3.6 governor — 누가 state를 정하나

| governor | 알고리즘 | 쓰는 곳 | 이 랩 |
|---|---|---|---|
| `step_wise` | trip 위에서 온도 추세가 오르면 state +1, 내리면 −1, trip 아래로 내려가면 바인딩 해제 | 기본값. 대부분의 ARM 보드 | **있음 (기본)** |
| `power_allocator` (IPA) | PID 제어기로 열 예산(`sustainable-power`, mW)을 계산해 "power actor"들에게 나눠 준다 | Arm 모바일 SoC (big.LITTLE CPU + GPU) | 있음 — §3.7에서 실패시켜 봄 |
| `fair_share` | trip 수준에 비례해 state를 weight대로 나눔 | 여러 cdev를 비율로 쓰는 플랫폼 | 없음 (설명만) |
| `bang_bang` | 팬 on/off 두 상태. trip 넘으면 켜고, trip−hyst 밑이면 끔 | 단순 팬 | 없음 (설명만) |
| `user_space` | 커널은 trip 넘었다는 uevent만 보내고 결정은 유저 데몬이 | 데몬(thermald 등)이 정책을 가진 시스템 | 없음 (설명만) |

step_wise의 핵심 함수를 랩 소스(`drivers/thermal/gov_step_wise.c`, 6.18.55)에서 그대로 옮긴다. 아래는 **소스 발췌**이고 실행한 코드가 아니다.

```c
	if (!instance->initialized) {
		if (throttle)
			return clamp(cur_state + 1, instance->lower, instance->upper);

		return THERMAL_NO_TARGET;
	}

	if (throttle) {
		if (trend == THERMAL_TREND_RAISING)
			return clamp(cur_state + 1, instance->lower, instance->upper);

		/* ... */
		if (trend == THERMAL_TREND_DROPPING)
			return clamp(cur_state - 1,
				     min(instance->lower + 1, instance->upper),
				     instance->upper);
	} else if (trend == THERMAL_TREND_DROPPING) {
		/* ... */
		return THERMAL_NO_TARGET;
	}

	return instance->target;
```

읽는 법:

1. `throttle` = 지금 온도 ≥ 그 trip의 threshold(넘기 전엔 trip 온도, 넘은 뒤엔 trip − hyst).
2. `trend`는 드라이버가 `get_trend`를 안 주면 직전 샘플과 비교한 결과다(`get_tz_trend()`): 오름·내림·같음.
3. throttle 중 **오르면 +1**, **내리면 −1이지만 하한은 lower+1**. 이 "lower+1" 때문에 map1(lower 2, upper 3)에서 내려가는 중에도 state가 3으로 간다. §4와 §9의 측정에서 그대로 보인다.
4. throttle 중인데 **온도가 같으면 그대로 둔다**(`return instance->target`). 일정한 온도에서는 단계를 더 올리지 않는다. §6의 emul_temp 실험이 이를 보여 준다.
5. throttle이 풀리고 내려가는 중이면 `THERMAL_NO_TARGET` — 바인딩을 해제한다. 한 cdev에 instance가 여럿이면 cdev는 **남은 instance target 중 최댓값**으로 간다(`thermal_cdev_update`).

### 3.7 예제 8 — power_allocator로 바꿔 보면

무엇을 확인하는 코드인가: `policy`에 다른 governor 이름을 쓰면 실행 중에 바뀐다. 그런데 power_allocator는 cooling device가 **전력 모델**(`get_requested_power`·`state2power`·`power2state` 콜백)을 가져야 한다. i4-fan에는 없다. `i4.sh`의 `gov` 시나리오 일부다.

```sh
echo power_allocator > $TZ/policy; echo "policy=$(cat $TZ/policy)"
dmesg | tail -3
echo step_wise > $TZ/policy; echo "policy=$(cat $TZ/policy)"
```

```text
sh: write error: Invalid argument
[    2.551046] thermal thermal_zone0: power_allocator: i4-fan is not a power actor
[    2.551092] thermal thermal_zone0: power_allocator: binding failed
policy=step_wise
[    1.167273] i4_temp: loading out-of-tree module taints kernel.
[    2.551046] thermal thermal_zone0: power_allocator: i4-fan is not a power actor
[    2.551092] thermal thermal_zone0: power_allocator: binding failed
policy=step_wise
```

출력에서 볼 것: 쓰기가 `EINVAL`로 거부되고 정책은 step_wise로 남는다. power_allocator에 쓸 수 있는 cooling device는 전력 모델을 가진 것 — 업스트림에서는 cpufreq cooling(Energy Model 또는 DT `dynamic-power-coefficient`)과 devfreq cooling(GPU)이다. 그래서 IPA는 "팬 보드"가 아니라 "팬 없는 휴대폰 SoC"의 governor다. 팬은 step_wise나 bang_bang으로 돌린다.

---

## 4. 실험 — step_wise 제어 루프를 시계열로 찍기

### 4.1 시나리오

무엇을 확인하는 코드인가: load를 100 %로 40초, 30 %로 21초 걸고, 250 ms마다 존 온도 · i4-fan state · gpio-fan state · gpio-sim 두 핀 · `temp1_min`/`temp1_max`를 기록한다. 로거는 `clock_nanosleep(TIMER_ABSTIME)`으로 드리프트 없이 샘플링한다.

```c
/* i4_log: sample sysfs files every <ms> for <sec> seconds -> "t v1 v2 ..." */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

static long rd(const char *path)
{
	char buf[32];
	int fd = open(path, O_RDONLY);
	ssize_t n;

	if (fd < 0)
		return -999999;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -999999;
	buf[n] = '\0';
	return strtol(buf, NULL, 10);
}

int main(int argc, char **argv)
{
	struct timespec t0, now, next;
	long ms, sec;

	if (argc < 4) {
		fprintf(stderr, "usage: %s ms sec file...\n", argv[0]);
		exit(1);
	}
	ms = atol(argv[1]);
	sec = atol(argv[2]);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	next = t0;
	for (;;) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		double t = (now.tv_sec - t0.tv_sec) + (now.tv_nsec - t0.tv_nsec) / 1e9;
		if (t > sec)
			break;
		printf("%.2f", t);
		for (int i = 3; i < argc; i++)
			printf(" %ld", rd(argv[i]));
		printf("\n");
		fflush(stdout);
		next.tv_nsec += ms * 1000000L;
		while (next.tv_nsec >= 1000000000L) {
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
	}
	return 0;
}
```

```sh
heat)
  insmod /gpio-fan.ko; insmod /i4_temp.ko $2; findtz; sleep 1
  G0=$(find /sys/devices/platform -name sim_gpio0 | head -1)/value; G1=${G0%sim_gpio0/value}sim_gpio1/value
  H=$(grep -l '^i4_temp$' /sys/class/hwmon/hwmon*/name | xargs dirname)
  echo "# cols: t zone_temp i4fan gpiofan gpio0 gpio1 temp1_min temp1_max"
  /i4_log 250 60 $TZ/temp $CD/cur_state $GF/cur_state $G0 $G1 $H/temp1_min $H/temp1_max > /tmp/log &
  echo 100 > /sys/module/i4_temp/parameters/load; sleep 40
  echo 30 > /sys/module/i4_temp/parameters/load; sleep 21
  cat /tmp/log
  echo "### stats"; cat $CD/stats/total_trans; cat $CD/stats/time_in_state_ms; cat $CD/stats/trans_table
  ;;
```

### 4.2 출력 — 처음 몇 줄과 상태가 바뀐 줄

```sh
TMO=200 sh boot.sh sub i4_poll heat > /var/tmp/I4/heat_poll.txt 2>&1
grep -E "^[0-9]+\.[0-9]+ " heat_poll.txt | sed -n 6,13p
```

```text
1.25 53193 0 0 0 0 -2147483647 60000
1.50 55553 0 0 0 0 -2147483647 60000
1.75 58876 0 0 0 0 -2147483647 60000
2.00 60955 0 0 0 0 -2147483647 60000
2.25 62932 0 0 0 0 -2147483647 60000
2.50 65714 0 0 0 0 -2147483647 60000
2.75 67456 1 0 0 0 57999 80000
3.00 68591 1 0 0 0 57999 80000
```

열 순서: 시각(s), 존 온도(m°C), i4-fan, gpio-fan, gpio0, gpio1, temp1_min, temp1_max. 이제 i4-fan이나 gpio-fan state가 바뀐 줄만 뽑는다.

```sh
grep -E "^[0-9]+\.[0-9]+ " heat_poll.txt | awk '{ if ($3!=p3 || $4!=p4) print; p3=$3; p4=$4 }'
```

```text
2.75 67456 1 0 0 0 57999 80000
9.75 80132 2 1 1 0 74999 95000
10.01 79682 3 2 0 1 74999 95000
11.75 74537 1 0 0 0 57999 80000
16.25 80140 2 1 1 0 74999 95000
16.75 78397 3 2 0 1 74999 95000
18.00 74940 1 0 0 0 57999 80000
22.51 79682 2 1 1 0 74999 95000
22.75 78809 3 2 0 1 74999 95000
24.25 74537 1 0 0 0 57999 80000
29.00 79690 2 1 1 0 74999 95000
29.25 78817 3 2 0 1 74999 95000
30.77 74544 1 0 0 0 57999 80000
35.50 79915 2 1 1 0 74999 95000
35.75 79040 3 2 0 1 74999 95000
37.00 74903 1 0 0 0 57999 80000
46.25 57801 0 0 0 0 -2147483647 60000
```

그리고 시나리오 끝에서 읽은 i4-fan의 통계:

```text
### stats
17
state0	19043
state1	34778
state2	1323
state3	7321
 From  :    To
       : state 0  state 1  state 2  state 3  
state 0:       0        1        0        0 
state 1:       1        0        5        0 
state 2:       0        0        0        5 
state 3:       0        5        0        0 
```

### 4.3 그림으로

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<text x="340.0" y="20" text-anchor="middle" font-size="13">측정: load 100% (0–40 s) → 30% (40–61 s), polling 1000 ms / passive 250 ms</text>
<line x1="60" y1="250" x2="650" y2="250" stroke="currentColor"/><line x1="60" y1="40" x2="60" y2="250" stroke="currentColor"/>
<line x1="56" y1="250.0" x2="60" y2="250.0" stroke="currentColor"/><text x="52" y="254.0" text-anchor="end" font-size="12">30</text>
<line x1="56" y1="220.0" x2="60" y2="220.0" stroke="currentColor"/><text x="52" y="224.0" text-anchor="end" font-size="12">40</text>
<line x1="56" y1="190.0" x2="60" y2="190.0" stroke="currentColor"/><text x="52" y="194.0" text-anchor="end" font-size="12">50</text>
<line x1="56" y1="160.0" x2="60" y2="160.0" stroke="currentColor"/><text x="52" y="164.0" text-anchor="end" font-size="12">60</text>
<line x1="56" y1="130.0" x2="60" y2="130.0" stroke="currentColor"/><text x="52" y="134.0" text-anchor="end" font-size="12">70</text>
<line x1="56" y1="100.0" x2="60" y2="100.0" stroke="currentColor"/><text x="52" y="104.0" text-anchor="end" font-size="12">80</text>
<line x1="56" y1="70.0" x2="60" y2="70.0" stroke="currentColor"/><text x="52" y="74.0" text-anchor="end" font-size="12">90</text>
<line x1="56" y1="40.0" x2="60" y2="40.0" stroke="currentColor"/><text x="52" y="44.0" text-anchor="end" font-size="12">100</text>
<text x="16" y="145.0" font-size="12" text-anchor="middle" transform="rotate(-90 16 145.0)">zone temp (°C)</text>
<line x1="60" y1="55.0" x2="650" y2="55.0" stroke="#d0564a" stroke-dasharray="6 3"/>
<text x="646" y="52.0" text-anchor="end" font-size="12">crit 95</text>
<line x1="60" y1="100.0" x2="650" y2="100.0" stroke="#e08a3c" stroke-dasharray="6 3"/>
<text x="646" y="97.0" text-anchor="end" font-size="12">active 80</text>
<line x1="60" y1="115.0" x2="650" y2="115.0" stroke="#e08a3c" stroke-dasharray="2 3"/>
<text x="646" y="112.0" text-anchor="end" font-size="12">80−hyst 75</text>
<line x1="60" y1="160.0" x2="650" y2="160.0" stroke="#3f9a6b" stroke-dasharray="6 3"/>
<text x="646" y="157.0" text-anchor="end" font-size="12">passive 60</text>
<line x1="60" y1="166.0" x2="650" y2="166.0" stroke="#3f9a6b" stroke-dasharray="2 3"/>
<text x="646" y="179.0" text-anchor="end" font-size="12">60−hyst 58</text>
<line x1="453.3" y1="40" x2="453.3" y2="360" stroke="#888" stroke-dasharray="3 3"/>
<text x="457.3" y="52" font-size="12">load → 30%</text>
<polyline points="60.0,231.3 62.5,221.7 64.9,208.2 67.4,199.8 69.8,191.7 72.3,180.4 74.8,173.3 77.2,163.4 79.7,157.1 82.1,151.2 84.6,142.9 87.0,137.6 89.5,134.2 92.0,132.1 94.4,130.1 96.9,127.2 99.3,125.4 101.8,122.9 104.2,121.3 106.7,119.9 109.2,117.7 111.6,116.4 114.1,114.6 116.5,113.4 119.0,112.3 121.5,110.8 124.0,109.8 126.4,108.9 128.8,107.6 131.3,106.7 133.8,105.6 136.2,104.9 138.7,104.2 141.1,103.2 143.6,102.6 146.0,101.8 148.5,101.3 151.1,100.8 153.4,100.0 155.9,99.6 158.4,101.0 160.8,104.8 163.2,107.3 165.7,110.7 168.2,112.8 170.7,114.8 173.1,117.7 175.5,116.4 178.0,115.1 180.5,113.4 182.9,112.3 185.4,110.7 187.8,109.8 190.3,108.8 192.8,107.5 195.2,106.7 197.7,105.6 200.1,104.9 202.6,104.2 205.0,103.2 207.5,102.6 210.0,102.0 212.4,101.2 214.9,100.7 217.3,100.3 219.8,99.6 222.3,100.9 224.7,104.8 227.2,107.2 229.6,109.5 232.1,112.8 234.6,114.8 237.0,115.2 239.5,113.4 242.0,112.3 244.4,111.3 246.9,109.8 249.3,108.9 251.8,108.0 254.2,106.7 256.7,106.0 259.1,105.2 261.6,104.2 264.0,103.5 266.6,102.6 269.0,102.1 271.4,101.5 273.9,100.8 276.3,100.3 278.8,99.6 281.3,101.0 283.7,103.6 286.2,107.3 288.6,109.6 291.1,112.8 293.5,114.8 296.0,116.8 298.5,116.4 300.9,115.1 303.4,113.9 305.8,112.3 308.3,111.2 310.8,110.2 313.2,108.8 315.7,108.0 318.1,106.7 320.7,105.9 323.0,105.2 325.6,104.2 328.1,103.5 330.5,102.9 332.9,102.0 335.3,101.5 337.8,100.7 340.3,100.3 342.7,99.8 345.2,100.9 347.6,103.5 350.1,107.2 352.5,109.5 355.0,111.7 357.5,114.8 359.9,116.8 362.6,116.4 364.8,115.1 367.3,113.9 369.8,112.8 372.2,111.2 374.7,110.2 377.1,108.8 379.6,107.9 382.0,107.1 384.5,105.9 387.0,105.2 389.4,104.2 391.9,103.5 394.3,102.9 396.8,102.0 399.2,101.5 401.7,101.0 404.3,100.2 406.7,99.8 409.1,100.3 411.5,102.9 414.0,105.4 416.6,107.8 419.0,110.1 421.5,112.2 423.8,115.3 426.3,114.1 428.8,113.0 431.3,111.4 433.7,110.4 436.1,109.4 438.6,108.1 441.0,107.2 443.5,106.0 446.0,105.3 448.4,104.6 450.9,103.6 453.3,103.0 455.9,109.0 458.3,112.7 460.7,116.3 463.2,121.3 465.6,124.5 468.1,127.5 470.5,131.7 473.1,134.3 475.5,136.8 477.9,140.3 480.4,142.6 482.8,145.7 485.3,147.6 487.8,149.4 490.2,152.1 492.7,153.7 495.1,156.0 497.6,157.4 500.0,158.8 502.6,160.7 505.0,161.9 507.4,163.6 509.9,164.6 512.3,165.6 514.8,166.6 517.2,166.6 519.7,166.6 522.2,166.6 524.7,166.6 527.1,166.6 529.5,166.6 532.0,166.5 534.6,166.5 536.9,166.5 539.4,166.5 541.8,166.5 544.4,166.5 546.8,166.5 549.2,166.5 551.8,166.5 554.1,166.5 556.6,166.5 559.1,166.5 561.5,166.5 564.0,166.5 566.5,166.5 568.9,166.5 571.3,166.5 573.9,166.5 576.2,166.5 578.7,166.5 581.3,166.5 583.7,166.5 586.1,166.5 588.5,166.5 591.0,166.5 593.5,166.5 596.0,166.5 598.4,166.5 600.8,166.5 603.4,166.5 605.8,166.5 608.3,166.5 610.8,166.5 613.2,166.5 615.7,166.5 618.1,166.5 620.6,166.5 623.1,166.5 625.4,166.5 628.0,166.5 630.3,166.5 632.9,166.5 635.2,166.5 637.8,166.5 640.2,166.5 642.6,166.5 645.1,166.5 647.5,166.5" fill="none" stroke="#4a7bd0" stroke-width="1.8"/>
<line x1="60" y1="360" x2="650" y2="360" stroke="currentColor"/><line x1="60" y1="290" x2="60" y2="360" stroke="currentColor"/>
<text x="52" y="364.0" text-anchor="end" font-size="12">0</text><line x1="60" y1="360.0" x2="650" y2="360.0" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="340.7" text-anchor="end" font-size="12">1</text><line x1="60" y1="336.7" x2="650" y2="336.7" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="317.3" text-anchor="end" font-size="12">2</text><line x1="60" y1="313.3" x2="650" y2="313.3" stroke="#888" stroke-opacity="0.3"/>
<text x="52" y="294.0" text-anchor="end" font-size="12">3</text><line x1="60" y1="290.0" x2="650" y2="290.0" stroke="#888" stroke-opacity="0.3"/>
<text x="16" y="325.0" font-size="12" text-anchor="middle" transform="rotate(-90 16 325.0)">state</text>
<polyline points="60.0,360.0 62.5,360.0 64.9,360.0 67.4,360.0 69.8,360.0 72.3,360.0 74.8,360.0 77.2,360.0 79.7,360.0 82.1,360.0 84.6,360.0 87.0,360.0 87.0,336.7 89.5,336.7 92.0,336.7 94.4,336.7 96.9,336.7 99.3,336.7 101.8,336.7 104.2,336.7 106.7,336.7 109.2,336.7 111.6,336.7 114.1,336.7 116.5,336.7 119.0,336.7 121.5,336.7 124.0,336.7 126.4,336.7 128.8,336.7 131.3,336.7 133.8,336.7 136.2,336.7 138.7,336.7 141.1,336.7 143.6,336.7 146.0,336.7 148.5,336.7 151.1,336.7 153.4,336.7 155.9,336.7 155.9,313.3 158.4,313.3 158.4,290.0 160.8,290.0 163.2,290.0 165.7,290.0 168.2,290.0 170.7,290.0 173.1,290.0 175.5,290.0 175.5,336.7 178.0,336.7 180.5,336.7 182.9,336.7 185.4,336.7 187.8,336.7 190.3,336.7 192.8,336.7 195.2,336.7 197.7,336.7 200.1,336.7 202.6,336.7 205.0,336.7 207.5,336.7 210.0,336.7 212.4,336.7 214.9,336.7 217.3,336.7 219.8,336.7 219.8,313.3 222.3,313.3 224.7,313.3 224.7,290.0 227.2,290.0 229.6,290.0 232.1,290.0 234.6,290.0 237.0,290.0 237.0,336.7 239.5,336.7 242.0,336.7 244.4,336.7 246.9,336.7 249.3,336.7 251.8,336.7 254.2,336.7 256.7,336.7 259.1,336.7 261.6,336.7 264.0,336.7 266.6,336.7 269.0,336.7 271.4,336.7 273.9,336.7 276.3,336.7 278.8,336.7 281.3,336.7 281.3,313.3 283.7,313.3 283.7,290.0 286.2,290.0 288.6,290.0 291.1,290.0 293.5,290.0 296.0,290.0 298.5,290.0 298.5,336.7 300.9,336.7 303.4,336.7 305.8,336.7 308.3,336.7 310.8,336.7 313.2,336.7 315.7,336.7 318.1,336.7 320.7,336.7 323.0,336.7 325.6,336.7 328.1,336.7 330.5,336.7 332.9,336.7 335.3,336.7 337.8,336.7 340.3,336.7 342.7,336.7 345.2,336.7 345.2,313.3 347.6,313.3 347.6,290.0 350.1,290.0 352.5,290.0 355.0,290.0 357.5,290.0 359.9,290.0 362.6,290.0 362.6,336.7 364.8,336.7 367.3,336.7 369.8,336.7 372.2,336.7 374.7,336.7 377.1,336.7 379.6,336.7 382.0,336.7 384.5,336.7 387.0,336.7 389.4,336.7 391.9,336.7 394.3,336.7 396.8,336.7 399.2,336.7 401.7,336.7 404.3,336.7 406.7,336.7 409.1,336.7 409.1,313.3 411.5,313.3 411.5,290.0 414.0,290.0 416.6,290.0 419.0,290.0 421.5,290.0 423.8,290.0 423.8,336.7 426.3,336.7 428.8,336.7 431.3,336.7 433.7,336.7 436.1,336.7 438.6,336.7 441.0,336.7 443.5,336.7 446.0,336.7 448.4,336.7 450.9,336.7 453.3,336.7 455.9,336.7 458.3,336.7 460.7,336.7 463.2,336.7 465.6,336.7 468.1,336.7 470.5,336.7 473.1,336.7 475.5,336.7 477.9,336.7 480.4,336.7 482.8,336.7 485.3,336.7 487.8,336.7 490.2,336.7 492.7,336.7 495.1,336.7 497.6,336.7 500.0,336.7 502.6,336.7 505.0,336.7 507.4,336.7 509.9,336.7 512.3,336.7 514.8,336.7 514.8,360.0 517.2,360.0 519.7,360.0 522.2,360.0 524.7,360.0 527.1,360.0 529.5,360.0 532.0,360.0 534.6,360.0 536.9,360.0 539.4,360.0 541.8,360.0 544.4,360.0 546.8,360.0 549.2,360.0 551.8,360.0 554.1,360.0 556.6,360.0 559.1,360.0 561.5,360.0 564.0,360.0 566.5,360.0 568.9,360.0 571.3,360.0 573.9,360.0 576.2,360.0 578.7,360.0 581.3,360.0 583.7,360.0 586.1,360.0 588.5,360.0 591.0,360.0 593.5,360.0 596.0,360.0 598.4,360.0 600.8,360.0 603.4,360.0 605.8,360.0 608.3,360.0 610.8,360.0 613.2,360.0 615.7,360.0 618.1,360.0 620.6,360.0 623.1,360.0 625.4,360.0 628.0,360.0 630.3,360.0 632.9,360.0 635.2,360.0 637.8,360.0 640.2,360.0 642.6,360.0 645.1,360.0 647.5,360.0" fill="none" stroke="#e08a3c" stroke-width="1.8"/>
<polyline points="60.0,360.0 62.5,360.0 64.9,360.0 67.4,360.0 69.8,360.0 72.3,360.0 74.8,360.0 77.2,360.0 79.7,360.0 82.1,360.0 84.6,360.0 87.0,360.0 89.5,360.0 92.0,360.0 94.4,360.0 96.9,360.0 99.3,360.0 101.8,360.0 104.2,360.0 106.7,360.0 109.2,360.0 111.6,360.0 114.1,360.0 116.5,360.0 119.0,360.0 121.5,360.0 124.0,360.0 126.4,360.0 128.8,360.0 131.3,360.0 133.8,360.0 136.2,360.0 138.7,360.0 141.1,360.0 143.6,360.0 146.0,360.0 148.5,360.0 151.1,360.0 153.4,360.0 155.9,360.0 155.9,336.7 158.4,336.7 158.4,313.3 160.8,313.3 163.2,313.3 165.7,313.3 168.2,313.3 170.7,313.3 173.1,313.3 175.5,313.3 175.5,360.0 178.0,360.0 180.5,360.0 182.9,360.0 185.4,360.0 187.8,360.0 190.3,360.0 192.8,360.0 195.2,360.0 197.7,360.0 200.1,360.0 202.6,360.0 205.0,360.0 207.5,360.0 210.0,360.0 212.4,360.0 214.9,360.0 217.3,360.0 219.8,360.0 219.8,336.7 222.3,336.7 224.7,336.7 224.7,313.3 227.2,313.3 229.6,313.3 232.1,313.3 234.6,313.3 237.0,313.3 237.0,360.0 239.5,360.0 242.0,360.0 244.4,360.0 246.9,360.0 249.3,360.0 251.8,360.0 254.2,360.0 256.7,360.0 259.1,360.0 261.6,360.0 264.0,360.0 266.6,360.0 269.0,360.0 271.4,360.0 273.9,360.0 276.3,360.0 278.8,360.0 281.3,360.0 281.3,336.7 283.7,336.7 283.7,313.3 286.2,313.3 288.6,313.3 291.1,313.3 293.5,313.3 296.0,313.3 298.5,313.3 298.5,360.0 300.9,360.0 303.4,360.0 305.8,360.0 308.3,360.0 310.8,360.0 313.2,360.0 315.7,360.0 318.1,360.0 320.7,360.0 323.0,360.0 325.6,360.0 328.1,360.0 330.5,360.0 332.9,360.0 335.3,360.0 337.8,360.0 340.3,360.0 342.7,360.0 345.2,360.0 345.2,336.7 347.6,336.7 347.6,313.3 350.1,313.3 352.5,313.3 355.0,313.3 357.5,313.3 359.9,313.3 362.6,313.3 362.6,360.0 364.8,360.0 367.3,360.0 369.8,360.0 372.2,360.0 374.7,360.0 377.1,360.0 379.6,360.0 382.0,360.0 384.5,360.0 387.0,360.0 389.4,360.0 391.9,360.0 394.3,360.0 396.8,360.0 399.2,360.0 401.7,360.0 404.3,360.0 406.7,360.0 409.1,360.0 409.1,336.7 411.5,336.7 411.5,313.3 414.0,313.3 416.6,313.3 419.0,313.3 421.5,313.3 423.8,313.3 423.8,360.0 426.3,360.0 428.8,360.0 431.3,360.0 433.7,360.0 436.1,360.0 438.6,360.0 441.0,360.0 443.5,360.0 446.0,360.0 448.4,360.0 450.9,360.0 453.3,360.0 455.9,360.0 458.3,360.0 460.7,360.0 463.2,360.0 465.6,360.0 468.1,360.0 470.5,360.0 473.1,360.0 475.5,360.0 477.9,360.0 480.4,360.0 482.8,360.0 485.3,360.0 487.8,360.0 490.2,360.0 492.7,360.0 495.1,360.0 497.6,360.0 500.0,360.0 502.6,360.0 505.0,360.0 507.4,360.0 509.9,360.0 512.3,360.0 514.8,360.0 517.2,360.0 519.7,360.0 522.2,360.0 524.7,360.0 527.1,360.0 529.5,360.0 532.0,360.0 534.6,360.0 536.9,360.0 539.4,360.0 541.8,360.0 544.4,360.0 546.8,360.0 549.2,360.0 551.8,360.0 554.1,360.0 556.6,360.0 559.1,360.0 561.5,360.0 564.0,360.0 566.5,360.0 568.9,360.0 571.3,360.0 573.9,360.0 576.2,360.0 578.7,360.0 581.3,360.0 583.7,360.0 586.1,360.0 588.5,360.0 591.0,360.0 593.5,360.0 596.0,360.0 598.4,360.0 600.8,360.0 603.4,360.0 605.8,360.0 608.3,360.0 610.8,360.0 613.2,360.0 615.7,360.0 618.1,360.0 620.6,360.0 623.1,360.0 625.4,360.0 628.0,360.0 630.3,360.0 632.9,360.0 635.2,360.0 637.8,360.0 640.2,360.0 642.6,360.0 645.1,360.0 647.5,360.0" fill="none" stroke="#3f9a6b" stroke-width="1.4" stroke-dasharray="4 2"/>
<line x1="60.0" y1="360" x2="60.0" y2="364" stroke="currentColor"/><text x="60.0" y="377" text-anchor="middle" font-size="12">0</text>
<line x1="158.3" y1="360" x2="158.3" y2="364" stroke="currentColor"/><text x="158.3" y="377" text-anchor="middle" font-size="12">10</text>
<line x1="256.7" y1="360" x2="256.7" y2="364" stroke="currentColor"/><text x="256.7" y="377" text-anchor="middle" font-size="12">20</text>
<line x1="355.0" y1="360" x2="355.0" y2="364" stroke="currentColor"/><text x="355.0" y="377" text-anchor="middle" font-size="12">30</text>
<line x1="453.3" y1="360" x2="453.3" y2="364" stroke="currentColor"/><text x="453.3" y="377" text-anchor="middle" font-size="12">40</text>
<line x1="551.7" y1="360" x2="551.7" y2="364" stroke="currentColor"/><text x="551.7" y="377" text-anchor="middle" font-size="12">50</text>
<line x1="650.0" y1="360" x2="650.0" y2="364" stroke="currentColor"/><text x="650.0" y="377" text-anchor="middle" font-size="12">60</text>
<text x="650" y="392" text-anchor="end" font-size="12">time (s)</text>
<line x1="70" y1="388" x2="92" y2="388" stroke="#4a7bd0" stroke-width="1.8"/><text x="97" y="392" font-size="12">zone temp</text>
<line x1="180" y1="388" x2="202" y2="388" stroke="#e08a3c" stroke-width="1.8"/><text x="207" y="392" font-size="12">i4-fan cur_state</text>
<line x1="340" y1="388" x2="362" y2="388" stroke="#3f9a6b" stroke-width="1.4" stroke-dasharray="4 2"/><text x="367" y="392" font-size="12">gpio-fan cur_state</text>
</svg>
```
그림 4 — 위 측정 그대로(250 ms 샘플 240개). 파란 선이 존 온도, 아래 주황 계단이 i4-fan state, 초록 점선이 gpio-fan state.

### 4.4 해석 — 무슨 일이 일어났나

1. **0~2 s, 60 °C 통과**: 1.75 s에 58.9 °C, 2.00 s에 61.0 °C로 이미 trip을 넘었는데 i4-fan은 **2.75 s에야** 1이 됐다. 존이 아직 passive 상태가 아니어서 `polling-delay = 1000` ms 주기로만 보고 있었기 때문이다. 그 0.75 s 동안 온도는 67.5 °C까지 올랐다. 감지 지연이 곧 오버슈트다(§5에서 줄인다).
2. **2.75 s, trip 0 진입**: `temp1_min/max`가 −∞/60000에서 **57999/80000**으로 바뀐다. 아래 경계 57999 = 60000 − hyst 2000 − 1. 이제 `tz->passive`가 1이라 250 ms마다 폴링한다. i4-fan은 map0의 upper인 1에서 멈춘다(lower 0, upper 1).
3. **2.75~9.75 s**: 팬 1의 정상 온도는 35 + 3.0 × 16 = 83 °C다. 천천히 80 °C에 다가간다.
4. **9.75 s, trip 1 진입**: i4-fan 2, gpio-fan 1(gpio0=1). 각 instance의 첫 throttle이라 `clamp(cur + 1, lower, upper)` — i4-fan은 1+1=2, gpio-fan은 0+1=1이다. 경계는 74999/95000으로 바뀐다.
5. **10.01 s, 곧바로 3**: 온도가 내려가기 시작했는데(79.7 °C) state는 오히려 3(gpio-fan 2)이 됐다. §3.6의 "내려갈 때도 하한은 lower+1" 규칙이다. map1의 lower가 2이므로 내려가는 중의 최소 state가 3이다.
6. **11.75 s, trip 1 해제**: 팬 3의 정상 온도는 62 °C라 금방 75 °C 아래로 떨어진다. trip 1은 throttle이 풀리고 추세가 내림이라 `THERMAL_NO_TARGET` — 바인딩 해제. i4-fan은 남은 instance(trip 0, target 1)를 따라 **3 → 1로 한 번에** 떨어지고 gpio-fan은 0이 된다.
7. **반복**: 팬 1로는 다시 83 °C를 향해 오르니 80 °C에서 또 걸린다. 약 6.5초 주기로 75~80 °C 사이를 도는 **limit cycle**이 생겼다. 통계가 이를 요약한다: 1→2 다섯 번, 2→3 다섯 번, 3→1 다섯 번.
8. **40 s, load 30 %**: 전력 1.04 W. 팬 1에서 정상 온도는 51.6 °C라 내려간다. 46.25 s에 58 °C(60 − 2) 밑으로 내려가 trip 0도 풀리고 팬은 0이 된다. 팬 0의 정상 온도는 35 + 1.04 × 22 = 57.9 °C로 58 °C 바로 아래라 다시 걸리지 않았다.

이 limit cycle은 버그가 아니라 **이 설정의 성질**이다. 부하가 "팬 1로는 못 버티고 팬 3이면 넘치게 버티는" 구간에 있으면, step_wise는 두 단계 사이를 오간다. hysteresis 5 °C가 진동 폭을 정했다. 실제 제품이라면 선택지가 있다:

- 진동이 소음 문제라면(팬 rpm이 6초마다 오르내림) map1의 하한을 낮추거나(`<&i4_fan 1 3>`) trip 사이 간격·hysteresis를 넓힌다.
- 팬 단계가 거칠다면 cooling device의 state를 더 잘게(PWM 0~255를 10단계로) 나눈다. pwm-fan의 `cooling-levels`가 그 역할이다.
- 정상 상태 오차 없이 목표 온도에 붙이고 싶으면 PID인 power_allocator를 쓰되, 그러려면 cooling device에 전력 모델이 있어야 한다(§3.7).

**Don의 경험과 연결**: SSD thermal throttling도 똑같이 진동한다. TMT2(heavy throttle)에서 성능을 크게 깎으면 온도가 빨리 떨어지고, 풀면 다시 오른다. 그래서 펌웨어는 단계 사이 hysteresis와 최소 체류 시간을 두고, 사인오프 때는 "throttle 진입·해제 횟수와 단계별 체류 시간"을 지표로 본다. 위 `stats/trans_table`과 `time_in_state_ms`가 정확히 그 지표다. 열 마진 사인오프에서 보던 "최고 온도 × 냉각 단계 × 시간" 그래프를, Linux에서는 `trans_table` 한 번 cat으로 얻는다.

---

## 5. polling 방식과 interrupt 방식

### 5.1 세 가지 주기

| DT 속성 | 의미 | 0이면 |
|---|---|---|
| `polling-delay` | passive trip이 하나도 안 걸렸을 때 폴링 주기 (ms) | 폴링 안 함. 센서 드라이버가 알려 줄 때만 갱신 |
| `polling-delay-passive` | passive trip이 하나라도 걸렸을 때 주기 | passive 중에도 폴링 안 함 |
| (센서 하드웨어) | ALERT/IRQ로 "창을 벗어났다"를 알림 | — |

인터럽트 방식은 `set_trips` 콜백이 핵심이다. 존을 갱신할 때마다 코어는 "현재 온도를 감싸는 가장 가까운 아래·위 경계"(low, high)를 계산해 센서 드라이버에 준다(`thermal_zone_set_trips`). 드라이버는 그 창을 칩의 ALERT 한계 레지스터에 쓰고, 칩이 창을 벗어나면 IRQ 핸들러에서 `thermal_zone_device_update()`를 부른다. hwmon 경유 존에서는 이 둘이 `temp_min`/`temp_max` 쓰기와 `hwmon_notify_event()`로 이미 연결되어 있다(`hwmon_thermal_set_trips`, `hwmon_thermal_notify`). 그래서 `i4_temp`의 `irq_mode`는 "온도가 tmin..tmax 창을 벗어나는 순간 한 번 `hwmon_notify_event()`"만 하면 된다(§2.4의 `i4_tick`).

이것은 Don에게 익숙한 패턴이다. 온도 센서 칩의 T_HIGH/T_LOW 레지스터와 ALERT 핀으로 폴링 없이 깨우는 펌웨어 — 다만 **창의 위치를 커널이 trip 상태에 맞춰 계속 다시 써 준다**는 점이 다르다.

### 5.2 예제 9 — 같은 가열, 폴링 0

무엇을 확인하는 코드인가: `polling-delay = <0>` dtb와 `irq_mode=1`로 §4와 같은 시나리오를 돌린다.

```sh
TMO=200 sh boot.sh sub i4_irq "heat~irq_mode=1" > /var/tmp/I4/heat_irq.txt 2>&1
grep -E "^[0-9]+\.[0-9]+ " heat_irq.txt | sed -n 6,11p
```

```text
1.25 53134 0 0 0 0 -2147483647 60000
1.50 55496 0 0 0 0 -2147483647 60000
1.75 57742 0 0 0 0 -2147483647 60000
2.00 60905 1 0 0 0 57999 80000
2.25 61995 1 0 0 0 57999 80000
2.50 63531 1 0 0 0 57999 80000
```

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="320.0" y="20" text-anchor="middle" font-size="13">같은 가열, 다른 감지: 60 °C 통과 후 fan이 1로 바뀐 시각</text>
<line x1="60" y1="220" x2="620" y2="220" stroke="currentColor"/><line x1="60" y1="40" x2="60" y2="220" stroke="currentColor"/>
<text x="52" y="224.0" text-anchor="end" font-size="12">30</text><line x1="56" y1="220.0" x2="60" y2="220.0" stroke="currentColor"/>
<text x="52" y="184.0" text-anchor="end" font-size="12">40</text><line x1="56" y1="180.0" x2="60" y2="180.0" stroke="currentColor"/>
<text x="52" y="144.0" text-anchor="end" font-size="12">50</text><line x1="56" y1="140.0" x2="60" y2="140.0" stroke="currentColor"/>
<text x="52" y="104.0" text-anchor="end" font-size="12">60</text><line x1="56" y1="100.0" x2="60" y2="100.0" stroke="currentColor"/>
<text x="52" y="64.0" text-anchor="end" font-size="12">70</text><line x1="56" y1="60.0" x2="60" y2="60.0" stroke="currentColor"/>
<text x="60.0" y="237" text-anchor="middle" font-size="12">0</text><line x1="60.0" y1="220" x2="60.0" y2="224" stroke="currentColor"/>
<text x="200.0" y="237" text-anchor="middle" font-size="12">1</text><line x1="200.0" y1="220" x2="200.0" y2="224" stroke="currentColor"/>
<text x="340.0" y="237" text-anchor="middle" font-size="12">2</text><line x1="340.0" y1="220" x2="340.0" y2="224" stroke="currentColor"/>
<text x="480.0" y="237" text-anchor="middle" font-size="12">3</text><line x1="480.0" y1="220" x2="480.0" y2="224" stroke="currentColor"/>
<text x="620.0" y="237" text-anchor="middle" font-size="12">4</text><line x1="620.0" y1="220" x2="620.0" y2="224" stroke="currentColor"/>
<text x="620" y="252" text-anchor="end" font-size="12">time (s)</text>
<line x1="60" y1="100.0" x2="620" y2="100.0" stroke="#3f9a6b" stroke-dasharray="6 3"/><text x="66" y="96.0" font-size="12">passive 60</text>
<polyline points="60.0,195.1 95.0,182.3 130.0,164.3 165.0,153.0 200.0,142.3 235.0,127.2 270.0,117.8 305.0,104.5 340.0,96.2 375.0,88.3 410.0,77.1 445.0,70.2 480.0,65.6 515.0,62.8 550.0,60.1 585.0,56.3 620.0,53.9" fill="none" stroke="#4a7bd0" stroke-width="1.8" stroke-dasharray=""/>
<circle cx="445.0" cy="70.2" r="5" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<polyline points="60.0,195.4 97.8,182.6 131.4,170.4 165.0,153.3 201.4,142.6 235.0,127.5 270.0,118.0 305.0,109.0 340.0,96.4 375.0,92.0 410.0,85.9 445.0,82.0 480.0,78.4 515.0,73.2 550.0,70.0 585.0,65.5 620.0,62.7" fill="none" stroke="#e08a3c" stroke-width="1.8" stroke-dasharray="5 3"/>
<circle cx="340.0" cy="96.4" r="5" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/>
<text x="330.0" y="60.0" text-anchor="end" font-size="12">irq 모드: 2.00 s</text>
<line x1="332.0" y1="64.0" x2="338.0" y2="92.0" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="457.0" y="116.0" font-size="12">polling 1 s: 2.75 s</text>
<line x1="455.0" y1="110.0" x2="448.0" y2="76.0" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="70" y1="284" x2="92" y2="284" stroke="#4a7bd0" stroke-width="1.8"/><text x="97" y="288" font-size="12">polling-delay=1000</text>
<line x1="240" y1="284" x2="262" y2="284" stroke="#e08a3c" stroke-width="1.8" stroke-dasharray="5 3"/><text x="267" y="288" font-size="12">polling-delay=0 + set_trips 알림</text>
</svg>
```
그림 5 — 두 실행의 처음 4초. 폴링 1초 방식은 60 °C를 넘고 0.75 s 뒤(67.5 °C)에 반응했고, 인터럽트 방식은 넘은 그 샘플(60.9 °C)에서 반응했다.

출력에서 볼 것: 2.00 s, 60.9 °C에서 이미 fan 1. 모델 틱(100 ms) 안에 반응했다. 이후의 limit cycle은 두 방식이 거의 같다. passive trip이 걸린 뒤에는 둘 다 250 ms 폴링으로 돌기 때문이다. 즉 인터럽트 방식의 이득은 **첫 감지 지연**과, trip 아래에 있는 대부분의 시간 동안 **깨어날 일이 없다**는 전력 이득이다. 배터리 장비에서 1초 폴링 kworker는 CPU를 계속 깨운다.

### 5.3 예제 10 — ALERT가 유저 공간에도 보인다

무엇을 확인하는 코드인가: `hwmon_notify_event()`는 thermal 갱신만 하는 게 아니라 `temp1_max_alarm`에 sysfs poll 알림과 `KOBJ_CHANGE` uevent도 보낸다. §10.3의 uevent 리스너로 hwmon 서브시스템을 듣는다.

```sh
alert)
  insmod /i4_temp.ko irq_mode=1; findtz; sleep 1
  H=$(grep -l '^i4_temp$' /sys/class/hwmon/hwmon*/name | xargs dirname)
  /i4_uevent hwmon NAME= > /tmp/ev &
  sleep 0.5; echo 100 > /sys/module/i4_temp/parameters/load; sleep 3
  cat /tmp/ev; echo "temp1_min=$(cat $H/temp1_min) temp1_max=$(cat $H/temp1_max) fan=$(cat $CD/cur_state)"
  ;;
```

```text
lab initramfs: 6.18.55-sub up 1.30s
[    4.858] change@/devices/platform/i4-temp/hwmon/hwmon0
    NAME=temp1_max_alarm
    OF_NAME=i4-temp
    OF_FULLNAME=/i4-temp
temp1_min=57999 temp1_max=80000 fan=1
```

출력에서 볼 것: 이벤트는 정확히 한 번이다(60 °C를 넘은 순간). 그 뒤 코어가 창을 57999..80000으로 다시 썼고 온도가 창 안에 있으니 더 이상 알림이 없다. 리스너의 필터 `NAME=`은 부분 문자열 매칭이라 `OF_NAME`, `OF_FULLNAME`도 함께 찍혔다. 유저 공간 헬스 모니터는 `temp1_max_alarm` 파일을 `poll()`로 기다리거나 이 uevent를 받아 로그를 남길 수 있다.

---

## 6. 예제 11 — `emul_temp`: 센서를 속여 정책을 시험하기

진짜 보드에서 80 °C를 만들려면 히트건이 필요하다. `CONFIG_THERMAL_EMULATION=y`면 존의 `emul_temp`에 m°C 값을 쓰는 순간부터 **존이 읽는 온도**가 그 값으로 바뀐다. 0을 쓰면 해제다. 정책(trip·cooling map·governor) 검증 전용 장치다.

무엇을 확인하는 코드인가: idle(load 0)에서 emul_temp를 65 → 85 → 88 → 70 → 50 °C → 해제 순으로 쓰며, 존 온도와 hwmon `temp1_input`(진짜 센서 값), 두 팬 state를 500 ms마다 본다.

```sh
emul)
  insmod /gpio-fan.ko; insmod /i4_temp.ko; findtz; sleep 1
  H=$(grep -l '^i4_temp$' /sys/class/hwmon/hwmon*/name | xargs dirname)
  /i4_log 500 23 $TZ/temp $H/temp1_input $CD/cur_state $GF/cur_state > /tmp/log &
  sleep 1;  echo 65000 > $TZ/emul_temp
  sleep 4;  echo 85000 > $TZ/emul_temp
  sleep 4;  echo 88000 > $TZ/emul_temp
  sleep 4;  echo 70000 > $TZ/emul_temp
  sleep 4;  echo 50000 > $TZ/emul_temp
  sleep 4;  echo 0 > $TZ/emul_temp
  sleep 2; echo "# cols: t zone_temp hwmon_temp1 i4fan gpiofan"; cat /tmp/log
  ;;
```

```text
lab initramfs: 6.18.55-sub up 1.01s
# cols: t zone_temp hwmon_temp1 i4fan gpiofan
0.00 35979 35979 0 0
0.50 36384 36384 0 0
1.00 65000 36740 1 0
1.50 65000 36911 1 0
2.00 65000 37033 1 0
2.50 65000 37170 1 0
3.00 65000 37290 1 0
3.51 65000 37396 1 0
4.00 65000 37490 1 0
4.50 65000 37556 1 0
5.00 65000 37630 1 0
5.50 85000 37605 2 1
6.00 85000 37583 2 1
6.50 85000 37563 2 1
7.00 85000 37550 2 1
7.50 85000 37535 2 1
8.00 85000 37520 2 1
8.50 85000 37509 2 1
9.00 85000 37499 2 1
9.50 88000 37433 3 2
10.00 88000 37360 3 2
10.50 88000 37295 3 2
11.00 88000 37238 3 2
11.50 88000 37189 3 2
12.00 88000 37153 3 2
12.50 88000 37113 3 2
13.00 88000 37078 3 2
13.50 70000 37210 1 0
14.00 70000 37325 1 0
14.50 70000 37407 1 0
15.00 70000 37495 1 0
15.50 70000 37579 1 0
16.00 70000 37637 1 0
16.50 70000 37702 1 0
17.00 70000 37759 1 0
17.50 50000 37954 0 0
18.00 50000 38124 0 0
18.50 50000 38245 0 0
19.00 50000 38380 0 0
19.50 50000 38499 0 0
20.00 50000 38603 0 0
20.50 50000 38677 0 0
21.00 50000 38761 0 0
21.50 38834 38834 0 0
22.00 38899 38899 0 0
22.50 38956 38956 0 0
```

출력에서 볼 것:

- **존 온도와 hwmon 온도가 갈라진다.** emul은 thermal 코어의 `__thermal_zone_get_temp()` 안에서만 값을 바꾼다. 센서 드라이버와 `temp1_input`은 진짜 값(37 °C대)을 그대로 보고한다. 필드 로그에서 둘이 다르면 누군가 emul_temp를 걸어 둔 것이다.
- 그런데 진짜 온도도 반응했다: 팬이 돌자 37.6 → 37.1 °C로 내려갔다. 모델의 팬이 Rth를 낮추기 때문이다. 정책은 가짜 온도로 돌았지만 냉각은 진짜로 일어났다 — 실제 보드에서도 마찬가지다.
- **85 °C에서 state가 2에 머문다.** 온도가 일정하면 추세가 STABLE이고 step_wise는 target을 유지한다(§3.6의 4번). 88 °C로 "올리자" 비로소 3이 됐다. emul로 정책을 시험할 때 값 하나를 쓰고 기다리면 최대 냉각까지 안 간다는 뜻이다. 실제 열 상승을 흉내 내려면 값을 조금씩 올려 써야 한다.
- 70 °C(trip 1의 해제점 75 아래, 내림): trip 1 바인딩 해제 → i4-fan은 trip 0 target 1로, gpio-fan은 0으로.
- 50 °C(58 아래, 내림): 전부 해제. 0을 쓰자 존 온도가 다시 진짜 값을 따른다.
- emul 값이 critical을 넘어도 막지 않는다. 커널이 막는 것은 반대 방향이다: **진짜 온도가 이미 critical 이상이면 emul 값을 무시**한다(`thermal_helpers.c`: "emulation code cannot hide critical conditions"). 그래서 §7에서는 emul로 critical을 일으킬 수 있다.

---

## 7. critical trip → `orderly_poweroff()` — 꺼지긴 꺼지나?

### 7.1 경로

critical trip을 위로 넘으면 governor를 거치지 않고 바로 `ops.critical`이 불린다. 기본 구현의 경로(`thermal_core.c`, `kernel/reboot.c`):

1. `thermal_zone_device_critical()` → `dev_emerg("…: critical temperature reached")`
2. `__hw_protection_trigger("Temperature too high", CONFIG_THERMAL_EMERGENCY_POWEROFF_DELAY_MS, …)` → `pr_emerg("HARDWARE PROTECTION shutdown …")`. 동작은 `/sys/kernel/reboot/hw_protection`(shutdown 또는 reboot)과 DT의 `critical-action`으로 고를 수 있다.
3. `orderly_poweroff(true)` → `/proc/sys/kernel/poweroff_cmd`(기본 `/sbin/poweroff`)를 **usermode helper로 실행**한다. 실행 자체가 실패하면 그때만 `emergency_sync()` + `kernel_power_off()`로 강제 종료한다.
4. 백업 타이머: `THERMAL_EMERGENCY_POWEROFF_DELAY_MS` > 0이면 그 시간 뒤 강제 종료를 예약한다. **0이면 예약하지 않는다.**

즉 "커널이 끈다"가 아니라 "커널이 **유저 공간에게 끄라고 시킨다**"이다. 유저 공간이 그 요청을 제대로 처리하는지는 시스템마다 다르다. 랩 initramfs의 `/init`는 셸 스크립트이고, 이 랩 커널의 `THERMAL_EMERGENCY_POWEROFF_DELAY_MS`는 0이다(§1.1의 설정 확인과 같은 방법으로 봤다: `CONFIG_THERMAL_EMERGENCY_POWEROFF_DELAY_MS=0`).

### 7.2 예제 12 — 세 가지 결말 (일회용 부팅 세 번)

무엇을 확인하는 코드인가: emul_temp에 100 °C를 써서 critical을 일으킨다. `poweroff_cmd`를 (a) 기본값 그대로, (b) 로그를 남기고 `poweroff -f`하는 스크립트로, (c) 없는 경로로 바꿔 각각 부팅한다. 마지막 `poweroff -f`는 LAB_CMD 끝의 정상 종료다.

```sh
crit)
  insmod /i4_temp.ko; findtz; sleep 1
  [ -n "$2" ] && echo "$2" > /proc/sys/kernel/poweroff_cmd
  echo "poweroff_cmd=$(cat /proc/sys/kernel/poweroff_cmd)"
  echo "hw_protection=$(cat /sys/kernel/reboot/hw_protection)"
  echo 100000 > $TZ/emul_temp
  sleep 3; echo "still alive after 3 s: temp=$(cat $TZ/temp)"; dmesg | tail -6
  ;;
```

```sh
#!/bin/sh
# /sbin/i4_poweroff: what a real init's shutdown path would do, minimally
echo "i4_poweroff: called by kernel (orderly_poweroff), args=[$*]" > /dev/console
sync
echo "i4_poweroff: synced, powering off" > /dev/console
exec /bin/busybox poweroff -f
```

```sh
for a in "" "/sbin/i4_poweroff" "/sbin/nonexistent"; do
  echo "===== crit [$a]"
  TMO=60 sh boot.sh sub i4_poll "crit~$a" loglevel=5 2>&1 | grep -v "^\[    0\.\|LAB_CMD"
done
```

```text
===== crit []
lab initramfs: 6.18.55-sub up 1.05s
[    1.155961] i4_temp: loading out-of-tree module taints kernel.
poweroff_cmd=/sbin/poweroff
hw_protection=shutdown
[    2.249236] thermal thermal_zone0: i4-soc: critical temperature reached
[    2.249317] reboot: HARDWARE PROTECTION shutdown (Temperature too high)
still alive after 3 s: temp=100000
[    1.155961] i4_temp: loading out-of-tree module taints kernel.
[    2.249236] thermal thermal_zone0: i4-soc: critical temperature reached
[    2.249317] reboot: HARDWARE PROTECTION shutdown (Temperature too high)
[    5.304563] reboot: Power down
===== crit [/sbin/i4_poweroff]
lab initramfs: 6.18.55-sub up 0.72s
poweroff_cmd=/sbin/i4_poweroff
hw_protection=shutdown
[    1.889749] thermal thermal_zone0: i4-soc: critical temperature reached
[    1.889816] reboot: HARDWARE PROTECTION shutdown (Temperature too high)
i4_poweroff: called by kernel (orderly_poweroff), args=[]
i4_poweroff: synced, powering off
[    1.915270] reboot: Power down
===== crit [/sbin/nonexistent]
lab initramfs: 6.18.55-sub up 0.90s
[    1.005088] i4_temp: loading out-of-tree module taints kernel.
poweroff_cmd=/sbin/nonexistent
hw_protection=shutdown
[    2.080362] thermal thermal_zone0: i4-soc: critical temperature reached
[    2.080434] reboot: HARDWARE PROTECTION shutdown (Temperature too high)
[    2.081221] reboot: Failed to start orderly shutdown: forcing the issue
[    2.087062] reboot: Power down
```

출력에서 볼 것:

- **(a) 기본값: 꺼지지 않았다.** `/sbin/poweroff`(BusyBox)는 실행에 성공했다. BusyBox `poweroff`는 `-f` 없이 부르면 init(PID 1)에게 `SIGUSR2`를 보내 종료를 부탁하는데(`init/halt.c`), 이 initramfs의 PID 1은 셸 스크립트라 그 신호를 처리하지 않는다. helper 실행이 "성공"했으니 커널은 강제 종료도 안 한다. 3초 뒤에도 100 °C로 살아 있고, 마지막 Power down(5.30 s)은 LAB_CMD 끝의 `poweroff -f`다. **실제 보드라면 칩이 타는 동안 시스템이 계속 도는 상황**이다. 줄 1.155961이 두 번 보이는 것은 `dmesg | tail -6`이 같은 로그를 다시 찍었기 때문이다.
- (b) helper가 불리고(인자 없음), sync 후 26 ms 만에 꺼졌다. systemd 시스템이라면 `/sbin/poweroff`가 systemd에 종료를 요청하고 서비스들을 내리므로 수 초가 걸릴 수 있다.
- (c) helper 실행 실패 → `Failed to start orderly shutdown: forcing the issue` → 즉시 `kernel_power_off()`. 이 줄은 `pr_warn`이라 기본 `quiet`(loglevel 4)에선 콘솔에 안 보여서 `loglevel=5`를 덧붙였다.

**실무 결론**: critical 보호를 믿으려면 (1) `poweroff_cmd`가 이 시스템의 init과 맞는지, (2) `CONFIG_THERMAL_EMERGENCY_POWEROFF_DELAY_MS`를 양수(수 초)로 두어 유저 공간이 멈춰도 강제 종료가 걸리는지, (3) 그보다 아래 단계의 하드웨어 보호(PMIC/SoC의 thermal shutdown 핀 — TSHUT)가 있는지 확인한다. Don이 사인오프할 때 "SW throttle이 실패해도 HW shutdown이 받친다"를 확인했던 것과 같은 계층 구조다.

### 7.3 예제 13 — `mode = disabled`면 아무도 안 지킨다

무엇을 확인하는 코드인가: 존을 끄고(`mode`에 disabled) load 100 %를 6초 건 뒤, 다시 켠다. `gov` 시나리오의 마지막 부분이다.

```sh
echo disabled > $TZ/mode; echo 100 > /sys/module/i4_temp/parameters/load; sleep 6
echo "mode=$(cat $TZ/mode) temp=$(cat $TZ/temp) i4fan=$(cat $CD/cur_state)"
echo enabled > $TZ/mode; sleep 1
echo "mode=$(cat $TZ/mode) temp=$(cat $TZ/temp) i4fan=$(cat $CD/cur_state)"
```

```text
mode=disabled temp=86068 i4fan=0
mode=enabled temp=81241 i4fan=3
```

출력에서 볼 것: disabled 동안 온도는 86 °C까지 올랐는데 팬은 0이었다. 폴링도 governor도 멈춘다. 다시 켜자 1초 안에 팬 3으로 반응했다. 디버깅하다 `mode`를 끈 채 출하하거나, 유저 공간 데몬이 존을 끄고 죽으면 이렇게 된다.

---

## 8. 예제 14 — 진짜 드라이버 붙이기: gpio-fan on gpio-sim

`gpio-fan`은 GPIO 몇 개의 조합으로 팬 속도를 고르는 업스트림 드라이버다(`drivers/hwmon/gpio-fan.c`). DT의 `gpio-fan,speed-map = <RPM 제어값> …` 쌍마다 cooling state 하나가 되고, `#cooling-cells = <2>`가 있으면 cooling device로도 등록된다. 여기서는 H4의 gpio-sim을 GPIO 칩으로 썼다.

무엇을 확인하는 코드인가: cooling device의 `cur_state`는 유저 공간이 직접 쓸 수도 있다. state 0~3을 쓰고, gpio-sim 쪽에서 본 핀 값과 gpio-fan의 hwmon 속성을 읽는다. `gov` 시나리오의 앞부분이다.

```sh
for s in 0 1 2 3; do echo $s > $GF/cur_state
  echo "state=$s gpio0=$(cat $G/sim_gpio0/value) gpio1=$(cat $G/sim_gpio1/value) fan1_target=$(cat $HF/fan1_target 2>/dev/null) pwm1=$(cat $HF/pwm1 2>/dev/null)"; done
```

```text
[    1.154073] gpio-fan i4-gpio-fan: supply fan not found, using dummy regulator
[    1.167273] i4_temp: loading out-of-tree module taints kernel.
### gpio-fan by hand (cooling_device cur_state is writable)
state=0 gpio0=0 gpio1=0 fan1_target=0 pwm1=0
state=1 gpio0=1 gpio1=0 fan1_target=2000 pwm1=85
state=2 gpio0=0 gpio1=1 fan1_target=4000 pwm1=170
state=3 gpio0=1 gpio1=1 fan1_target=6000 pwm1=255
```

출력에서 볼 것:

- speed-map의 제어값 0~3이 GPIO 두 개의 비트 패턴(gpio0 = bit0, gpio1 = bit1)으로 그대로 나간다. 진짜 보드라면 팬 컨트롤러의 속도 선택 핀이다. 로직 분석기로 보던 그 핀을 sysfs 한 줄로 바꾼 것이다.
- gpio-fan의 hwmon 쪽 `pwm1`은 0/85/170/255로, 속도 단계를 PWM 눈금에 비례해 보여 준다. `fan1_target`은 speed-map의 RPM이다. 실제 회전수를 재는 타코 입력이 없으므로 `fan1_input`은 없다.
- `supply fan not found, using dummy regulator`: DT에 `fan-supply`가 없어서다. H3에서 본 dummy regulator 경고 그대로다. 실제 보드에서는 팬 전원 레일을 연결해야 팬을 끌 때 레일도 끌 수 있다.
- §4의 시계열에서 gpio-fan이 i4-fan과 함께 움직인 것(map1, 1~3 범위)은 이 드라이버가 thermal 코어의 지시를 받은 결과다. 우리 열 모델은 gpio-fan state를 모르므로 온도에는 영향이 없다.

PWM 팬이라면 `pwm-fan` 드라이버(`cooling-levels = <0 102 170 230 255>`처럼 state별 PWM 듀티)를 쓴다. 랩 Image-sub에는 `pwm-gpio`(GPIO를 소프트웨어 PWM으로)와 `pwm-fan` 모듈이 있어 같은 구성이 가능하지만 이 노트에서는 실행하지 않았다.

---

## 9. 예제 15 — tracepoint로 제어 루프 보기 (Image-dbg)

Image-sub에는 ftrace가 없다(§1.1). 그래서 같은 소스를 `linux-build-dbg`로 다시 빌드한 `i4_temp.ko`(vermagic `6.18.55-dbg`)를 넣고 Image-dbg로 부팅했다. DT는 같은 `i4_poll.dtb`이고 gpio-fan·test_power는 넣지 않았다(Image-sub용 모듈이라 이 커널에 못 넣는다).

무엇을 확인하는 코드인가: `thermal` 이벤트 그룹에서 `thermal_temperature`(존 갱신마다), `thermal_zone_trip`(throttle 중인 trip마다), `cdev_update`(cooling device state 변경)를 켜고 load 100 %를 11초 건다.

```sh
trace)
  mount -t tracefs none /sys/kernel/tracing; T=/sys/kernel/tracing
  ls $T/events/thermal | tr '\n' ' '; echo
  insmod /i4_temp.ko; findtz; sleep 1
  echo 1 > $T/events/thermal/thermal_temperature/enable
  echo 1 > $T/events/thermal/cdev_update/enable
  echo 1 > $T/events/thermal/thermal_zone_trip/enable
  echo 100 > /sys/module/i4_temp/parameters/load; sleep 11
  echo 0 > $T/tracing_on
  echo "thermal_temperature events: $(grep -c thermal_temperature $T/trace)"
  grep thermal_temperature $T/trace | head -6
  echo "--- cdev_update with the sample that caused it"
  grep -B3 cdev_update $T/trace | grep -v thermal_zone_trip
  ;;
```

```text
lab initramfs: 6.18.55-dbg up 4.39s
cdev_update enable filter thermal_power_cpu_get_power_simple thermal_power_cpu_limit thermal_power_devfreq_get_power thermal_power_devfreq_limit thermal_temperature thermal_zone_trip 
thermal_temperature events: 35
    kworker/u4:1-28      [000] .....     6.974262: thermal_temperature: thermal_zone=i4-soc id=0 temp_prev=35892 temp=46607
    kworker/u4:1-28      [000] .....     7.992099: thermal_temperature: thermal_zone=i4-soc id=0 temp_prev=46607 temp=58768
    kworker/u4:1-28      [000] .....     9.016761: thermal_temperature: thermal_zone=i4-soc id=0 temp_prev=58768 temp=67370
    kworker/u4:1-28      [000] .....     9.273308: thermal_temperature: thermal_zone=i4-soc id=0 temp_prev=67370 temp=68512
    kworker/u4:1-28      [000] .....     9.528427: thermal_temperature: thermal_zone=i4-soc id=0 temp_prev=68512 temp=69227
    kworker/u4:1-28      [000] .....     9.784721: thermal_temperature: thermal_zone=i4-soc id=0 temp_prev=69227 temp=70233
--- cdev_update with the sample that caused it
    kworker/u4:1-28      [000] .....     7.992099: thermal_temperature: thermal_zone=i4-soc id=0 temp_prev=46607 temp=58768
    kworker/u4:1-28      [000] .....     9.016761: thermal_temperature: thermal_zone=i4-soc id=0 temp_prev=58768 temp=67370
    kworker/u4:1-28      [000] .....     9.018741: cdev_update: type=i4-fan target=1
--
    kworker/u4:1-28      [000] .....    15.960972: thermal_temperature: thermal_zone=i4-soc id=0 temp_prev=79970 temp=80118
    kworker/u4:1-28      [000] .....    15.961185: cdev_update: type=i4-fan target=2
    kworker/u4:1-28      [000] .....    16.216181: thermal_temperature: thermal_zone=i4-soc id=0 temp_prev=80118 temp=79453
    kworker/u4:1-28      [000] .....    16.216340: cdev_update: type=i4-fan target=3
```

출력에서 볼 것:

- **샘플 간격이 바뀐다**: 6.97 → 7.99 → 9.02 s는 1초(polling-delay), 9.02 이후는 약 256 ms(polling-delay-passive). passive trip이 걸린 순간부터 빨라진다(그림 6의 ⑦).
- **1초 폴링의 대가**: 7.99 s 샘플은 58.8 °C로 60 아래였고, 다음 샘플(9.02 s)은 67.4 °C였다. trip을 넘은 지 거의 1초 뒤에 알았고 그동안 7 °C 넘게 올랐다. §4의 로거가 본 것과 같은 현상을 커널 쪽에서 확인했다.
- 모든 일이 같은 `kworker/u4:1`에서 일어난다. thermal 코어는 전용 워크큐(`thermal_wq`)의 지연 작업으로 돈다. 그래서 `get_temp`와 `set_cur_state`는 잠들어도 된다.
- 16.216 s: temp_prev 80118 → 79453으로 **내려가는 중에** target 3. §3.6 규칙("throttle 중 내림이면 하한 lower+1")의 커널 쪽 증거다.
- `thermal_power_cpu_*`, `thermal_power_devfreq_*`는 cpufreq/devfreq cooling이 power_allocator와 일할 때 찍히는 이벤트다. 이 QEMU 보드에는 cpufreq가 없어 쓸 일이 없다.

---

## 10. power supply 클래스 — 배터리 · 충전기 · 어댑터

### 10.1 직관과 정의

**power_supply** 클래스는 "전원 공급원 하나 = 속성 묶음 하나"다. 배터리(연료 게이지), 충전기 IC, AC 어댑터, USB 포트가 각각 `/sys/class/power_supply/<이름>/`이 되고, 상태가 바뀌면 드라이버가 `power_supply_changed()`를 불러 **uevent**를 보낸다. UI의 배터리 아이콘, 저전력 셧다운 데몬, 충전 LED가 전부 이 uevent와 sysfs를 본다.

bare-metal로 말하면 연료 게이지 칩(BQ27xxx 등)을 I2C로 읽어 SoC%·전압·전류를 얻고, 값이 바뀌면 이벤트 플래그를 올리는 코드다. Linux에서는 그 이벤트가 netlink 멀티캐스트로 나가 여러 유저 프로세스가 동시에 받는다.

| 속성 (`POWER_SUPPLY_PROP_`) | sysfs 파일 | 단위·값 |
|---|---|---|
| `STATUS` | `status` | Charging, Discharging, Not charging, Full, Unknown |
| `PRESENT`, `ONLINE` | `present`, `online` | 0/1 (ONLINE은 어댑터·USB용) |
| `CAPACITY` | `capacity` | % (0~100) |
| `VOLTAGE_NOW` | `voltage_now` | **µV** |
| `CURRENT_NOW` | `current_now` | **µA**, 배터리는 방전이 음수 |
| `CHARGE_FULL_DESIGN`, `CHARGE_NOW` | `charge_full_design` … | µAh |
| `TEMP` | `temp` | **0.1 °C** (250 = 25.0 °C) |
| `HEALTH` | `health` | Good, Overheat, Dead, Over voltage, Cold … |
| `TECHNOLOGY` | `technology` | Li-ion, LiFe, NiMH … |

hwmon(m°C, mV, mA)과 단위 체계가 **다르다**. 같은 보드에서 hwmon은 mV, power_supply는 µV, 온도는 m°C와 0.1 °C가 섞인다. 두 클래스를 오가는 코드에서 가장 흔한 버그다.

### 10.2 예제 16 — `test_power`로 클래스 둘러보기

`test_power`는 커널에 들어 있는 시험용 드라이버다. 가짜 `test_ac`·`test_battery`·`test_usb`를 만들고, 모듈 파라미터(`/sys/module/test_power/parameters/`)를 쓰면 값이 바뀌며 `power_supply_changed()`가 불린다.

무엇을 확인하는 코드인가: 등록된 공급원, 배터리의 uevent 내용, 그리고 커널이 자동으로 만든 thermal zone·hwmon을 보고, 용량과 AC 연결을 바꿔 uevent를 받는다.

```sh
psy)
  insmod /test_power.ko; sleep 1
  echo "### /sys/class/power_supply"; ls /sys/class/power_supply | tr "\n" " "; echo
  echo "### test_battery/uevent"; cat /sys/class/power_supply/test_battery/uevent
  echo "### thermal zones / hwmon created for power supplies"
  for z in /sys/class/thermal/thermal_zone*; do echo "$z type=$(cat $z/type) temp=$(cat $z/temp)"; done
  for h in /sys/class/hwmon/hwmon*; do echo "$h name=$(cat $h/name) $(ls $h | grep _input | tr '\n' ' ')"; done
  /i4_uevent power_supply POWER_SUPPLY_ > /tmp/ev &
  sleep 1
  P=/sys/module/test_power/parameters
  echo 15 > $P/battery_capacity; sleep 1
  echo off > $P/ac_online; sleep 1
  echo "### uevents seen by i4_uevent"; cat /tmp/ev | grep -E "^\[|NAME|ONLINE|CAPACITY=|STATUS"
  ;;
```

```text
lab initramfs: 6.18.55-sub up 0.69s
### /sys/class/power_supply
test_ac test_battery test_usb 
### test_battery/uevent
DEVTYPE=power_supply
POWER_SUPPLY_NAME=test_battery
POWER_SUPPLY_TYPE=Battery
POWER_SUPPLY_STATUS=Discharging
POWER_SUPPLY_CHARGE_TYPE=Standard
POWER_SUPPLY_CHARGE_TYPES=Standard
POWER_SUPPLY_HEALTH=Good
POWER_SUPPLY_PRESENT=1
POWER_SUPPLY_TECHNOLOGY=Li-ion
POWER_SUPPLY_VOLTAGE_NOW=3300
POWER_SUPPLY_CURRENT_NOW=-1600
POWER_SUPPLY_CURRENT_AVG=-1600
POWER_SUPPLY_CHARGE_FULL_DESIGN=100
POWER_SUPPLY_CHARGE_FULL=100
POWER_SUPPLY_CHARGE_NOW=50
POWER_SUPPLY_CHARGE_COUNTER=-1000
POWER_SUPPLY_CHARGE_BEHAVIOUR=auto
POWER_SUPPLY_CAPACITY=50
POWER_SUPPLY_CAPACITY_LEVEL=Normal
POWER_SUPPLY_TEMP=26
POWER_SUPPLY_TEMP_MAX=1000
POWER_SUPPLY_TIME_TO_EMPTY_NOW=3600
POWER_SUPPLY_TIME_TO_EMPTY_AVG=3600
POWER_SUPPLY_TIME_TO_FULL_NOW=3600
POWER_SUPPLY_TYPE=Battery
POWER_SUPPLY_MANUFACTURE_YEAR=1234
POWER_SUPPLY_MODEL_NAME=Test battery
POWER_SUPPLY_MANUFACTURER=Linux
POWER_SUPPLY_SERIAL_NUMBER=6.18.55-sub
### thermal zones / hwmon created for power supplies
/sys/class/thermal/thermal_zone0 type=test_battery temp=2600
/sys/class/hwmon/hwmon0 name=test_ac 
/sys/class/hwmon/hwmon1 name=test_battery curr1_input in0_input temp1_input 
/sys/class/hwmon/hwmon2 name=test_usb 
### uevents seen by i4_uevent
[    2.930] change@/devices/virtual/power_supply/test_battery
    POWER_SUPPLY_NAME=test_battery
    POWER_SUPPLY_STATUS=Discharging
    POWER_SUPPLY_CAPACITY=15
    POWER_SUPPLY_MODEL_NAME=Test battery
[    3.951] change@/devices/virtual/power_supply/test_ac
    POWER_SUPPLY_NAME=test_ac
    POWER_SUPPLY_ONLINE=0
```

출력에서 볼 것:

- `cat uevent`는 uevent로 나갈 환경 변수 목록을 그대로 보여 준다. 속성 하나하나가 `POWER_SUPPLY_<이름>=값`이다(`POWER_SUPPLY_TYPE`이 두 번 나오는 것은 이 커널의 uevent 생성 코드가 그렇게 찍는 것이고, §10.5의 i4_batt에서도 같다).
- **커널이 공짜로 만든 것 두 가지**: `TEMP` 속성을 가진 test_battery에 thermal zone(`type=test_battery`)이 생겼고(그림 1의 점선), `CONFIG_POWER_SUPPLY_HWMON=y`라 공급원마다 hwmon 장치도 생겼다. 배터리 온도에 trip을 걸어 충전을 멈추는 정책을 thermal로 짤 수 있다는 뜻이다.
- **test_power의 단위는 ABI를 안 지킨다**: `VOLTAGE_NOW=3300`은 µV 단위로 읽으면 3.3 mV이고, `TEMP=26`은 0.1 °C 단위로 2.6 °C다. 그래서 thermal zone이 2600 m°C를 보고한다. test_power는 uevent 흐름 시험용이지 값의 의미를 시험하는 드라이버가 아니다. 진짜 드라이버는 §10.4처럼 µV·0.1 °C로 보고해야 한다.
- 용량을 15로 바꾸자 test_battery change 이벤트, AC를 off로 바꾸자 test_ac change 이벤트(`ONLINE=0`)가 왔다. 실제 시스템에서 udev 규칙이나 UPower가 받는 것이 이 이벤트다.

### 10.3 예제 17 — uevent 리스너 (`NETLINK_KOBJECT_UEVENT`)

무엇을 확인하는 코드인가: udev 없이 커널 uevent를 직접 받는 최소 프로그램. 소켓 하나를 만들어 멀티캐스트 그룹 1에 bind하면 모든 kobject 이벤트가 온다. 메시지는 `"change@/devices/…"` 머리말 뒤에 `KEY=VALUE` 문자열들이 NUL로 이어진 형태다.

```c
/* i4_uevent: print kernel uevents (NETLINK_KOBJECT_UEVENT) for one subsystem */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>

int main(int argc, char **argv)
{
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = 1 };
	const char *want = argc > 1 ? argv[1] : "power_supply";
	char buf[8192], subsys[64];
	int fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);

	if (fd < 0 || bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		perror("netlink");
		exit(1);
	}
	snprintf(subsys, sizeof(subsys), "SUBSYSTEM=%s", want);
	for (;;) {
		ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
		struct timespec ts;
		int match = 0;

		if (n <= 0) {
			perror("recv");
			exit(1);
		}
		buf[n] = '\0';
		for (char *p = buf; p < buf + n; p += strlen(p) + 1)
			if (!strcmp(p, subsys))
				match = 1;
		if (!match)
			continue;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		printf("[%5ld.%03ld] %s\n", (long)ts.tv_sec, ts.tv_nsec / 1000000, buf);
		for (char *p = buf + strlen(buf) + 1; p < buf + n; p += strlen(p) + 1)
			if (argc < 3 || strstr(p, argv[2]))
				printf("    %s\n", p);
		fflush(stdout);
	}
}
```

볼 것: 인자 1은 서브시스템(`SUBSYSTEM=` 값으로 거른다), 인자 2는 찍을 키의 부분 문자열이다. 이 프로그램이 §5.3(hwmon), §10.2, §10.5에서 쓰였다. 커널 쪽 경로는 G6의 `kobject_uevent_env()`이고, power_supply에서는 `power_supply_changed()` → `changed_work` → `kobject_uevent(KOBJ_CHANGE)` → `power_supply_uevent()`가 속성 전부를 읽어 환경 변수로 붙인다.

### 10.4 예제 18 — `i4_batt`: 전압 기반 연료 게이지

DT의 `simple-battery` 노드는 셀의 정격을 적는 표준 바인딩이다. 드라이버는 `monitored-battery` phandle을 따라가 `power_supply_get_battery_info()`로 읽는다. 여기서는 OCV(open-circuit voltage) 표까지 읽어, "측정 전압 + I×R 보정 → OCV 표 → 잔량 %"라는 **전압 기반 게이지**를 흉내 낸다.

무엇을 확인하는 코드인가: (1/3) 시뮬레이션 셀과 게이지 루프. 100 ms 틱마다 셀 시간으로 6초(60배 가속)씩 `drain_ma` 전류로 방전한다. 셀의 단자 전압은 OCV − I×R이고, 게이지는 그 전압에 I×R을 더해 OCV를 복원한 뒤 `power_supply_batinfo_ocv2cap()`(커널 헬퍼, DT 표 보간)으로 %를 얻는다. %가 바뀔 때만 `power_supply_changed()`.

```c
// SPDX-License-Identifier: GPL-2.0
/* i4_batt: voltage-based fuel gauge for a simulated Li-ion cell */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>

static int drain_ma = 1500;	/* load current; 1 tick = 6 s of battery time */
module_param(drain_ma, int, 0644);

struct gauge {
	struct power_supply *psy;
	struct power_supply_battery_info *bi;
	struct delayed_work tick;
	long charge_uah;	/* the "true" cell state (simulation) */
	int vbat_uv, cap;	/* what the gauge measured / reports */
};

static int ocv_of(struct gauge *g, long pct10)	/* cell model: SoC -> OCV */
{
	const struct power_supply_battery_ocv_table *t = g->bi->ocv_table[0];
	int i, n = g->bi->ocv_table_size[0];

	for (i = 1; i < n && t[i].capacity * 10 > pct10; i++)
		;
	if (i == n)
		return t[n - 1].ocv;
	return t[i].ocv + (int)((long)(t[i - 1].ocv - t[i].ocv) *
		(pct10 - t[i].capacity * 10) / ((t[i - 1].capacity - t[i].capacity) * 10));
}

static void gauge_tick(struct work_struct *w)
{
	struct gauge *g = container_of(to_delayed_work(w), struct gauge, tick);
	int full = g->bi->charge_full_design_uah, r = g->bi->factory_internal_resistance_uohm;
	int ir_uv = (int)((long)drain_ma * r / 1000);	/* mA x µΩ / 1000 = µV */
	int cap;

	g->charge_uah = max(0L, g->charge_uah - (long)drain_ma * 1000 * 6 / 3600);
	g->vbat_uv = ocv_of(g, g->charge_uah * 1000 / full) - ir_uv;
	/* gauge side: measure Vbat, add back I*R, look up the DT OCV table */
	cap = power_supply_batinfo_ocv2cap(g->bi, g->vbat_uv + ir_uv, 25);
	if (cap != g->cap) {
		g->cap = cap;
		power_supply_changed(g->psy);	/* -> KOBJ_CHANGE uevent */
	}
	if (g->charge_uah)
		schedule_delayed_work(&g->tick, msecs_to_jiffies(100));
}
```

(2/3) 속성 목록과 `get_property`. 단위에 주목: µV, µA(방전 음수), 0.1 °C.

```c
/* ---- part 2: properties + probe ---- */
static const enum power_supply_property gauge_props[] = {
	POWER_SUPPLY_PROP_STATUS, POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY, POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW, POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_HEALTH, POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
};

static int gauge_get(struct power_supply *psy, enum power_supply_property psp,
		     union power_supply_propval *val)
{
	struct gauge *g = power_supply_get_drvdata(psy);

	if (!g->bi)	/* uevent at register time comes before battery_info */
		return -ENODATA;
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = drain_ma > 0 ? POWER_SUPPLY_STATUS_DISCHARGING
					   : POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case POWER_SUPPLY_PROP_PRESENT:	val->intval = 1; break;
	case POWER_SUPPLY_PROP_CAPACITY:	val->intval = g->cap; break;		/* % */
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:	val->intval = g->vbat_uv; break;	/* µV */
	case POWER_SUPPLY_PROP_CURRENT_NOW:	val->intval = -drain_ma * 1000; break;	/* µA */
	case POWER_SUPPLY_PROP_TEMP:	val->intval = 250; break;		/* 0.1 °C */
	case POWER_SUPPLY_PROP_HEALTH:	val->intval = POWER_SUPPLY_HEALTH_GOOD; break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = g->bi->charge_full_design_uah; break;
	default: return -EINVAL;
	}
	return 0;
}

static const struct power_supply_desc gauge_desc = {
	.name = "i4_batt", .type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = gauge_props, .num_properties = ARRAY_SIZE(gauge_props),
	.get_property = gauge_get,
};
```

(3/3) probe. 등록 → battery info 읽기 → 틱 시작.

```c
static void gauge_stop(void *w) { cancel_delayed_work_sync(w); }

static int gauge_probe(struct platform_device *pdev)
{
	struct power_supply_config cfg = { .fwnode = dev_fwnode(&pdev->dev) };
	struct gauge *g = devm_kzalloc(&pdev->dev, sizeof(*g), GFP_KERNEL);
	int ret;

	if (!g)
		return -ENOMEM;
	cfg.drv_data = g;
	INIT_DELAYED_WORK(&g->tick, gauge_tick);
	g->psy = devm_power_supply_register(&pdev->dev, &gauge_desc, &cfg);
	if (IS_ERR(g->psy))
		return PTR_ERR(g->psy);
	ret = power_supply_get_battery_info(g->psy, &g->bi);	/* monitored-battery */
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "no simple-battery\n");
	dev_info(&pdev->dev, "cell %d uAh, %d..%d uV, R=%d uohm, %d OCV points\n",
		 g->bi->charge_full_design_uah, g->bi->voltage_min_design_uv,
		 g->bi->voltage_max_design_uv, g->bi->factory_internal_resistance_uohm,
		 g->bi->ocv_table_size[0]);
	g->charge_uah = g->bi->charge_full_design_uah * 9 / 10;	/* start at 90 % */
	g->cap = -1;
	schedule_delayed_work(&g->tick, 0);
	return devm_add_action_or_reset(&pdev->dev, gauge_stop, &g->tick);
}

static const struct of_device_id gauge_of[] = { { .compatible = "i4,batt" }, { } };
MODULE_DEVICE_TABLE(of, gauge_of);
static struct platform_driver gauge_drv = {
	.probe = gauge_probe,
	.driver = { .name = "i4_batt", .of_match_table = gauge_of },
};
module_platform_driver(gauge_drv);
MODULE_DESCRIPTION("I4 simulated fuel gauge");
MODULE_LICENSE("GPL");
```

볼 것:

- `cfg.fwnode = dev_fwnode(dev)`: power_supply 장치에 DT 노드를 물려야 `monitored-battery`를 찾는다(못 찾으면 부모 장치 노드로도 찾는다).
- **등록 순서의 함정**: `power_supply_get_battery_info()`는 `struct power_supply`를 인자로 받으니 등록 뒤에만 부를 수 있다. 그런데 등록하는 순간 uevent·thermal zone 생성이 `get_property`를 부른다. 그래서 `g->bi`가 아직 NULL일 때는 `-ENODATA`를 돌려준다. 이 가드 없이 처음 짰다면 등록 중 NULL 역참조로 oops가 났을 것이다. `-ENODATA`는 uevent 생성 코드가 "이 속성은 지금 없음"으로 건너뛰는 값이다.
- `power_supply_changed()`는 스핀락과 `schedule_work()`만 하므로 **인터럽트 문맥에서도 부를 수 있다**(게이지 칩의 ALERT IRQ 핸들러에서 바로 불러도 된다). 무거운 uevent 생성은 작업 큐에서 일어난다.

DT 쪽(§1.3에서 이미 봄)의 핵심 속성:

| 속성 | 이 랩의 값 | 의미 |
|---|---|---|
| `charge-full-design-microamp-hours` | 3000000 | 3000 mAh |
| `voltage-min-design-microvolt` / `-max-` | 3300000 / 4200000 | 셀 전압 범위 |
| `factory-internal-resistance-micro-ohms` | 150000 | 150 mΩ — IR 보정에 쓴다 |
| `ocv-capacity-celsius` | 25 | 아래 표가 측정된 온도 |
| `ocv-capacity-table-0` | 11쌍 | <µV %> 쌍, 전압 내림차순 |

### 10.5 예제 19 — 방전 관찰과 uevent

무엇을 확인하는 코드인가: `i4_batt`를 넣고 uevent를 본 뒤, 1초마다 capacity·voltage_now·current_now를 14초 동안 기록하며 CAPACITY uevent를 받는다.

```sh
batt)
  insmod /i4_batt.ko; sleep 0.3
  dmesg | grep i4_batt
  B=/sys/class/power_supply/i4_batt
  echo "### $B/uevent"; cat $B/uevent
  /i4_uevent power_supply POWER_SUPPLY_CAPACITY= > /tmp/ev &
  /i4_log 1000 14 $B/capacity $B/voltage_now $B/current_now > /tmp/log
  echo "# cols: t capacity voltage_now(uV) current_now(uA)"; cat /tmp/log
  echo "### uevents (first 8)"; grep -A1 "^\[" /tmp/ev | grep -v "^--" | head -16
  ;;
```

```text
lab initramfs: 6.18.55-sub up 1.01s
[    1.129960] i4_batt: loading out-of-tree module taints kernel.
[    1.138721] i4_batt i4-fuel-gauge: cell 3000000 uAh, 3300000..4200000 uV, R=150000 uohm, 11 OCV points
### /sys/class/power_supply/i4_batt/uevent
DEVTYPE=power_supply
OF_NAME=i4-fuel-gauge
OF_FULLNAME=/i4-fuel-gauge
OF_COMPATIBLE_0=i4,batt
OF_COMPATIBLE_N=1
POWER_SUPPLY_NAME=i4_batt
POWER_SUPPLY_TYPE=Battery
POWER_SUPPLY_STATUS=Discharging
POWER_SUPPLY_HEALTH=Good
POWER_SUPPLY_PRESENT=1
POWER_SUPPLY_VOLTAGE_MAX_DESIGN=4200000
POWER_SUPPLY_VOLTAGE_MIN_DESIGN=3300000
POWER_SUPPLY_VOLTAGE_NOW=3851000
POWER_SUPPLY_CURRENT_NOW=-1500000
POWER_SUPPLY_CHARGE_FULL_DESIGN=3000000
POWER_SUPPLY_CAPACITY=89
POWER_SUPPLY_TEMP=250
POWER_SUPPLY_TYPE=Battery
# cols: t capacity voltage_now(uV) current_now(uA)
0.00 89 3851000 -1500000
1.00 88 3843000 -1500000
2.00 88 3835000 -1500000
3.00 87 3827000 -1500000
4.00 86 3820000 -1500000
5.00 85 3811000 -1500000
6.00 84 3803000 -1500000
7.00 84 3795000 -1500000
8.00 83 3787000 -1500000
9.00 82 3779000 -1500000
10.00 81 3771000 -1500000
11.00 80 3763000 -1500000
12.00 80 3755000 -1500000
13.00 79 3748600 -1500000
### uevents (first 8)
[    2.350] change@/devices/platform/i4-fuel-gauge/power_supply/i4_batt
    POWER_SUPPLY_CAPACITY=88
[    3.597] change@/devices/platform/i4-fuel-gauge/power_supply/i4_batt
    POWER_SUPPLY_CAPACITY=87
[    4.845] change@/devices/platform/i4-fuel-gauge/power_supply/i4_batt
    POWER_SUPPLY_CAPACITY=86
[    6.093] change@/devices/platform/i4-fuel-gauge/power_supply/i4_batt
    POWER_SUPPLY_CAPACITY=85
[    7.341] change@/devices/platform/i4-fuel-gauge/power_supply/i4_batt
    POWER_SUPPLY_CAPACITY=84
[    8.589] change@/devices/platform/i4-fuel-gauge/power_supply/i4_batt
    POWER_SUPPLY_CAPACITY=83
[    9.837] change@/devices/platform/i4-fuel-gauge/power_supply/i4_batt
    POWER_SUPPLY_CAPACITY=82
[   11.085] change@/devices/platform/i4-fuel-gauge/power_supply/i4_batt
    POWER_SUPPLY_CAPACITY=81
```

```svg
<svg viewBox="0 0 600 260" xmlns="http://www.w3.org/2000/svg">
<text x="300.0" y="20" text-anchor="middle" font-size="13">i4_batt 측정: 1.5 A 방전 (시간 60배 가속: 100 ms tick = 6 s)</text>
<line x1="60" y1="200" x2="540" y2="200" stroke="currentColor"/><line x1="60" y1="40" x2="60" y2="200" stroke="currentColor"/><line x1="540" y1="40" x2="540" y2="200" stroke="currentColor"/>
<text x="52" y="204.0" text-anchor="end" font-size="12">78%</text>
<text x="52" y="150.7" text-anchor="end" font-size="12">82%</text>
<text x="52" y="97.3" text-anchor="end" font-size="12">86%</text>
<text x="52" y="44.0" text-anchor="end" font-size="12">90%</text>
<text x="546" y="204.0" font-size="12">3.74 V</text>
<text x="546" y="150.7" font-size="12">3.78 V</text>
<text x="546" y="97.3" font-size="12">3.82 V</text>
<text x="546" y="44.0" font-size="12">3.86 V</text>
<text x="60.0" y="217" text-anchor="middle" font-size="12">0</text>
<text x="133.8" y="217" text-anchor="middle" font-size="12">2</text>
<text x="207.7" y="217" text-anchor="middle" font-size="12">4</text>
<text x="281.5" y="217" text-anchor="middle" font-size="12">6</text>
<text x="355.4" y="217" text-anchor="middle" font-size="12">8</text>
<text x="429.2" y="217" text-anchor="middle" font-size="12">10</text>
<text x="503.1" y="217" text-anchor="middle" font-size="12">12</text>
<text x="540" y="232" text-anchor="end" font-size="12">time (s)</text>
<polyline points="60.0,53.3 96.9,53.3 96.9,66.7 133.8,66.7 170.8,66.7 170.8,80.0 207.7,80.0 207.7,93.3 244.6,93.3 244.6,106.7 281.5,106.7 281.5,120.0 318.5,120.0 355.4,120.0 355.4,133.3 392.3,133.3 392.3,146.7 429.2,146.7 429.2,160.0 466.2,160.0 466.2,173.3 503.1,173.3 540.0,173.3 540.0,186.7" fill="none" stroke="#4a7bd0" stroke-width="1.8"/>
<polyline points="60.0,52.0 96.9,62.7 133.8,73.3 170.8,84.0 207.7,93.3 244.6,105.3 281.5,116.0 318.5,126.7 355.4,137.3 392.3,148.0 429.2,158.7 466.2,169.3 503.1,180.0 540.0,188.5" fill="none" stroke="#e08a3c" stroke-width="1.8" stroke-dasharray="5 3"/>
<line x1="70" y1="244" x2="92" y2="244" stroke="#4a7bd0" stroke-width="1.8"/><text x="97" y="248" font-size="12">capacity (왼쪽 축)</text>
<line x1="250" y1="244" x2="272" y2="244" stroke="#e08a3c" stroke-width="1.8" stroke-dasharray="5 3"/><text x="277" y="248" font-size="12">voltage_now (오른쪽 축)</text>
</svg>
```
그림 7 — 위 로그의 capacity(계단)와 voltage_now(점선). 1.5 A × 6 s/틱이면 1 %(30 mAh)마다 12틱 = 1.2 s. uevent 간격 1.248 s와 맞는다.

출력에서 볼 것:

- dmesg 한 줄이 DT 파싱 결과다: 3000 mAh, 3.3~4.2 V, 150 mΩ, OCV 11점.
- 드라이버가 선언하지 않은 `VOLTAGE_MAX_DESIGN`/`VOLTAGE_MIN_DESIGN`이 uevent에 있다. power_supply 코어가 battery info에서 나온 속성을 자동으로 붙인 것이다(`power_supply_battery_info_properties`). 드라이버가 같은 정보를 두 번 보고할 필요가 없다.
- `OF_NAME`, `OF_COMPATIBLE_0`도 붙는다. udev 규칙이 DT 노드 이름으로 장치를 고를 수 있다.
- 시작 용량이 90이 아니라 89다: 셀 모델(1 % 단위 보간 → OCV)과 게이지(DT 표 → 정수 %)의 반올림 차이다. 실제 전압 기반 게이지도 이 정도 오차는 기본이고, 그래서 실전 게이지는 쿨롱 카운팅(전류 적분)과 OCV 보정을 섞는다.
- uevent는 정확히 %가 바뀔 때만 1.248 s 간격으로 왔다. **값이 바뀔 때만 `power_supply_changed()`**를 부르는 것이 중요하다. 매 샘플마다 부르면 udev·UPower가 초당 수십 번 깨어나 배터리를 더 빨리 닳게 한다.

### 10.6 함정

- **단위**: µV·µA·0.1 °C. mV로 보고하면 UI가 0.003 V를 표시한다. test_power도 이 점에선 나쁜 예다(§10.2).
- **전류 부호**: 배터리 `current_now`는 방전이 음수가 ABI 문서의 규약이지만, 오래된 드라이버 중 반대 부호를 쓰는 것이 있다. 유저 공간 도구가 status와 함께 해석해야 하는 이유다.
- **등록 직후의 get_property**: §10.4의 NULL 가드.
- **uevent 폭주**: 매 틱 `power_supply_changed()`.
- **잔량 0 % 정책을 커널에 기대**: power_supply 클래스는 보고만 한다. "5 %에서 안전 종료"는 유저 공간 데몬(UPower, 제품 데몬)이나 PMIC의 UVLO가 한다.

---

## 11. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" text-anchor="middle" font-size="13">thermal_zone_device_update() 한 번에 일어나는 일 (drivers/thermal/thermal_core.c)</text>
<rect x="10" y="34" width="200" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="110" y="54" text-anchor="middle" font-size="12">poll_queue 지연 작업</text>
<rect x="240" y="34" width="200" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="54" text-anchor="middle" font-size="12">hwmon_notify_event()</text>
<rect x="470" y="34" width="200" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="570" y="54" text-anchor="middle" font-size="12">emul_temp · mode 쓰기</text>
<line x1="110" y1="64" x2="300" y2="86" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="340" y1="64" x2="340" y2="86" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="570" y1="64" x2="380" y2="86" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="150" y="88" width="380" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="108" text-anchor="middle" font-size="12">① get_temp → hwmon_thermal_get_temp → i4_read</text>
<rect x="150" y="134" width="380" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="154" text-anchor="middle" font-size="12">② trace thermal_temperature, last_temperature 갱신</text>
<rect x="150" y="180" width="380" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="200" text-anchor="middle" font-size="12">③ handle_trips: 위/아래로 넘은 trip 처리</text>
<rect x="150" y="226" width="380" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="246" text-anchor="middle" font-size="12">④ set_trips(low, high) → temp1_min / temp1_max</text>
<rect x="150" y="272" width="380" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="292" text-anchor="middle" font-size="12">⑤ governor->manage (step_wise_manage)</text>
<rect x="150" y="318" width="380" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="338" text-anchor="middle" font-size="12">⑥ thermal_cdev_update → set_cur_state (fan_set_cur)</text>
<rect x="150" y="364" width="380" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="384" text-anchor="middle" font-size="12">⑦ monitor: passive &gt; 0 ? 250 ms : polling-delay 뒤 다시</text>
<line x1="340" y1="118" x2="340" y2="132" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="340" y1="164" x2="340" y2="178" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="340" y1="210" x2="340" y2="224" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="340" y1="256" x2="340" y2="270" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="340" y1="302" x2="340" y2="316" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="340" y1="348" x2="340" y2="362" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="548" y="166" width="124" height="58" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="610" y="184" text-anchor="middle" font-size="12">critical 넘음</text>
<text x="610" y="200" text-anchor="middle" font-size="12">→ hw_protection</text>
<text x="610" y="216" text-anchor="middle" font-size="12">→ orderly_poweroff</text>
<line x1="530" y1="195" x2="546" y2="195" stroke="currentColor" marker-end="url(#ah6)"/>
<rect x="10" y="166" width="124" height="58" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="72" y="184" text-anchor="middle" font-size="12">passive 넘음</text>
<text x="72" y="200" text-anchor="middle" font-size="12">tz-&gt;passive++</text>
<text x="72" y="216" text-anchor="middle" font-size="12">→ 빠른 폴링</text>
<line x1="150" y1="195" x2="136" y2="195" stroke="currentColor" marker-end="url(#ah6)"/>
<path d="M150,379 L4,379 L4,49 L8,49" fill="none" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah6)"/>
<text x="340" y="418" text-anchor="middle" font-size="12">tz-&gt;lock(mutex)를 잡고 kworker에서 돈다 — get_temp · set_cur_state는 잠들어도 된다</text>
</svg>
```
그림 6 — 존 갱신 한 번의 순서(`__thermal_zone_device_update()`, 6.18.55). 세 개의 진입점 중 어디로 들어와도 같은 경로를 탄다.

| 단계 | 함수 (파일) | 이 노트에서 본 흔적 |
|---|---|---|
| 진입 | `thermal_zone_device_check()`(지연 작업), `hwmon_notify_event()` → `hwmon_thermal_notify()`, `emul_temp_store()`, `mode_store()` | §4(폴링), §5(알림), §6, §7.3 |
| ① 온도 읽기 | `__thermal_zone_get_temp()` → `ops.get_temp` = `hwmon_thermal_get_temp()` → 드라이버 `.read(hwmon_temp_input)` | hwmon `.read`가 두 경로(sysfs, thermal)에서 불림 |
| emul 적용 | 같은 함수 안, critical 이상이면 무시 | §6 |
| ② 기록 | `last_temperature` ← `temperature` ← 새 값, `trace_thermal_temperature` | §9의 `temp_prev`/`temp` |
| ③ trip 처리 | `thermal_zone_handle_trips()` → `thermal_trip_crossed()` (passive++, critical → `handle_critical_trips()`), threshold를 trip 또는 trip−hyst로 | §4의 57999/74999, §7 |
| ④ 창 설정 | `thermal_zone_set_trips()` → `ops.set_trips` = `hwmon_thermal_set_trips()` → `.write(temp_min/max)` | §2.6의 `temp1_max 60000` |
| ⑤ governor | `step_wise_manage()` → `thermal_zone_trip_update()` → `get_target_state()` | §3.6 |
| ⑥ 적용 | `thermal_cdev_update()` → instance target 최댓값 → `ops.set_cur_state` + 통계 | §4의 trans_table, §9 `cdev_update` |
| ⑦ 재예약 | `monitor_thermal_zone()` → `mod_delayed_work(thermal_wq, …)` | §9의 1 s → 256 ms |

드라이버 작성자가 다시 만나는 지점:

- **센서 드라이버**(SoC TSENS, I2C 온도 칩)는 hwmon으로 쓰고 `HWMON_C_REGISTER_TZ` 한 비트로 thermal에 붙이거나, hwmon 없이 `devm_thermal_of_zone_register()`를 직접 부른다(SoC 내부 센서 드라이버 대부분이 후자 — `drivers/thermal/*_thermal.c`). 칩에 ALERT가 있으면 `set_trips` + IRQ에서 `thermal_zone_device_update()`.
- **냉각 드라이버**는 `thermal_cooling_device_ops` 세 함수. 팬은 pwm-fan/gpio-fan을 그대로 쓰고, 새로 짤 일은 "독자 팬 컨트롤러 IC"나 "가속기 클럭 단계" 정도다. CPU는 cpufreq-dt가 `CPUFREQ_IS_COOLING_DEV` 플래그로 자동 등록하고(CPU 노드에 `#cooling-cells = <2>`), GPU·NPU는 devfreq cooling(`devfreq_cooling_em_register()`)이다.
- **연료 게이지·충전기 드라이버**는 `power_supply_desc` + `get_property` + 상태 변화 때 `power_supply_changed()`. 충전기와 배터리는 `supplied_to`/`power-supplies`로 서로를 알린다(`external_power_changed` 콜백).
- 모든 콜백은 **프로세스 문맥**(kworker 또는 sysfs 쓰기)에서 불린다. I2C·mutex를 써도 된다. 반대로 IRQ 핸들러에서는 `thermal_zone_device_update()`를 직접 부르지 말고 threaded IRQ(F6)에서 부른다 — 내부에서 mutex를 잡는다.

---

## 12. Embedded 실무에서 다시 보기

**장면 1 — 밀폐 케이스의 로봇 컴퓨트 보드.** Jetson Orin·i.MX8 보드에 NPU 추론과 카메라 4대를 돌리면 SoC가 80 °C를 넘는다. 보드 DT에는 보통 SoC 존(`cpu-thermal`, `gpu-thermal`)이 SoC 벤더 dtsi로 이미 있고, 보드 dts에서 팬과 trip을 덧붙인다. 일의 대부분은 이 노트의 §1.3 같은 DT 작업과, §4 같은 **실측 시계열**로 "60 °C passive에서 CPU를 몇 단계까지 깎고, 75 °C active에서 팬 몇 단계"를 튜닝하는 것이다. 측정 도구는 `i4_log` 같은 sysfs 샘플러와 thermal tracepoint다.

**장면 2 — "현장에서 장비가 갑자기 꺼졌다".** 로그에 `critical temperature reached`가 있으면 thermal 종료다. 없으면 PMIC의 하드웨어 thermal shutdown이나 전원 문제다. 반대로 "뜨거웠는데 안 꺼졌다"면 §7의 세 가지(`poweroff_cmd`와 init, `THERMAL_EMERGENCY_POWEROFF_DELAY_MS`, 존 `mode`)를 본다. 필드 로그에 hwmon 값과 존 값을 같이 남기면 emul_temp가 걸린 채 출하된 사고도 잡힌다(§6).

**장면 3 — 헬스 모니터 데몬.** 센서 키트 장비는 원격 텔레메트리로 보드 온도·레일 전압·팬 RPM을 보낸다. 데몬은 `/sys/class/hwmon/*/name`과 `_label`로 채널을 찾고(번호는 부팅마다 바뀔 수 있다), `_alarm` 파일을 `poll()`하거나 hwmon uevent를 듣는다. 레일 전압 모니터 칩(INA219/INA3221)은 hwmon 드라이버가 업스트림에 있어 DT 노드만 추가하면 된다.

**장면 4 — 드론 배터리.** 비행 컨트롤러가 아닌 Linux 컴패니언 컴퓨터가 배터리 잔량으로 귀환 결정을 하려면, 게이지 드라이버가 정확한 CAPACITY와 TEMP를 보고하고 uevent를 제때 보내야 한다. 저온에서 Li-ion 내부 저항이 커지면 전압 기반 게이지가 잔량을 과소평가한다(§10.5의 IR 보정이 온도에 따라 달라야 하는 이유 — DT의 `resistance-temp-table`이 이를 위한 속성이다).

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| hwmon 온도를 °C로 보고 | `sensors`에 0.04 °C, trip이 영원히 안 걸림 | 단위는 m°C | `.read`에서 1000배, 리뷰 체크리스트에 단위 |
| `thermal-zones` 아래 존 하나가 `thermal-sensors` 누락 | 모든 존 등록 실패, `missing thermal sensor` | thermal_of가 존을 훑다 첫 오류에서 중단 | 존마다 센서 지정, 안 쓰는 존은 `status = "disabled"` |
| `#thermal-sensor-cells = <1>`인데 채널 번호 틀림 | `temp1_input not attached to any thermal zone` | hwmon은 temp 채널 인덱스(0부터)를 id로 씀 | `<&sensor 0>`이 temp1 |
| `polling-delay = 1000`, 센서 IRQ 없음 | trip 넘고 최대 1초 뒤 반응, 오버슈트 | 폴링 주기 = 감지 지연 (§4, §9) | ALERT + `set_trips`로 polling 0, 또는 주기 단축 |
| hysteresis 너무 작음 | 팬이 몇 초마다 켜졌다 꺼짐, 소음 | step_wise limit cycle (§4) | hyst·trip 간격 확대, cooling map 하한 조정, 더 잘게 나눈 state |
| emul_temp를 걸어 둔 채 출하 | 존 온도가 고정, 실제 과열에 무반응 | emul은 critical 미만의 진짜 온도를 가린다 | 출하 이미지에서 `THERMAL_EMULATION` 끄기, 로그에 hwmon 값 병기 |
| `poweroff_cmd`가 init과 안 맞음 | critical 로그만 찍히고 계속 동작 (§7) | helper 실행 "성공" → 강제 종료 안 함 | init 확인, `THERMAL_EMERGENCY_POWEROFF_DELAY_MS` 양수, HW TSHUT |
| 존 `mode`를 disabled로 두고 잊음 | 과열에도 팬 0 (§7.3) | 폴링·governor 정지 | 데몬 종료 시 복구, 부팅 스크립트에서 상태 점검 |
| power_supply에 mV·°C로 보고 | UI에 3 mV, 2.6 °C (§10.2) | ABI는 µV, 0.1 °C | 단위 변환, `test_power`를 기준으로 삼지 않기 |
| 매 샘플마다 `power_supply_changed()` | uevent 폭주, 유저 공간 깨어남, 전력 낭비 | 변화 없이도 이벤트 | 값이 바뀔 때만 (§10.5) |

---

## 14. 면접에서 이렇게 말한다

**Q.** Walk me through what Linux does when the SoC gets hot.

**A.** 센서 드라이버가 온도를 주고(hwmon 또는 thermal 센서 드라이버), DT `thermal-zones`가 그 센서에 trip을 붙인다. 코어는 `polling-delay`마다, 혹은 센서 ALERT IRQ가 오면 존을 갱신한다. 온도를 읽고, 넘은 trip을 처리하고, 센서에 다음 경계 창(`set_trips`)을 쓰고, governor(기본 step_wise)가 trip에 묶인 cooling device(CPU 주파수, 팬)의 state를 정한다. passive trip이 걸리면 더 짧은 `polling-delay-passive`로 본다. critical trip은 governor 없이 `orderly_poweroff()`로 간다. 내 실습에서 1초 폴링이면 trip 통과 후 0.75~1초 늦게 반응해 7 °C 넘게 오버슈트했고, ALERT 방식은 즉시 반응했다.

> A sensor driver reports the temperature, usually through hwmon or a thermal sensor driver, and the device tree's thermal-zones node attaches trip points to that sensor. The thermal core updates the zone either every polling interval or when the sensor raises an alert interrupt. Each update reads the temperature, handles any trips that were crossed, programs the next alert window into the sensor through set_trips, and runs the governor — step_wise by default — which picks a state for each cooling device bound to the trip, such as a CPU frequency cap or a fan level. Once a passive trip is active the zone is polled faster. A critical trip bypasses the governor and triggers an orderly poweroff. In my lab, one-second polling reacted up to a second late and overshot by about seven degrees, while the interrupt-driven version reacted on the first sample above the trip.

**꼬리 질문**: "passive와 active trip의 차이는?" → 커널 동작상 차이는 passive가 빠른 폴링을 켠다는 것 정도이고, 관례상 passive는 성능을 깎는 냉각, active는 팬이다. hot·critical은 governor를 안 탄다.

**Q.** How do you describe a thermal zone in the device tree?

**A.** 루트의 `thermal-zones` 아래에 존 노드 하나: `polling-delay`, `polling-delay-passive`, `thermal-sensors = <&sensor id>`. 그 아래 `trips`에 trip마다 temperature(m°C)·hysteresis·type, `cooling-maps`에 map마다 `trip = <&trip>`과 `cooling-device = <&cdev min max>`. cooling device 노드는 `#cooling-cells = <2>`, 센서 노드는 `#thermal-sensor-cells`. 존은 센서 드라이버가 `devm_thermal_of_zone_register()`를 부를 때 생기고, cooling device는 등록 순서와 무관하게 phandle로 묶인다.

> Under the root thermal-zones node you add one node per zone with the two polling delays and a thermal-sensors phandle to the sensor plus its index. Inside it, a trips node lists each trip with a temperature in millidegrees, a hysteresis and a type, and a cooling-maps node binds each trip to cooling devices with a phandle and a minimum and maximum state. The cooling device node has #cooling-cells of two. The zone itself is only created when the sensor driver registers with devm_thermal_of_zone_register; cooling devices are matched by device node, whatever order they probe in.

**Q.** Explain how the step_wise governor behaves. Can it oscillate?

**A.** trip마다 instance가 있고, throttle 중(온도 ≥ threshold)이면 온도가 오를 때 state +1, 내릴 때 −1(단 하한은 map의 lower+1), 같으면 유지한다. trip 아래로 내려가면서 하강 추세면 바인딩을 해제한다. 한 cooling device의 실제 state는 그 장치에 걸린 instance target의 최댓값이다. 그래서 진동한다 — 내 측정에서 load가 팬 1로는 못 버티고 팬 3이면 넉넉한 구간에 있었더니, 75~80 °C 사이를 6.5초 주기로 돌며 state가 1→2→3→1을 반복했고 `trans_table`에 그대로 찍혔다. hysteresis가 진폭을 정한다. 고치려면 hysteresis·trip 간격·cooling map 범위를 조정하거나 state를 더 잘게 나눈다.

> Step_wise keeps one instance per trip and cooling device. While the zone is above the trip's threshold it raises the state by one when the temperature is rising, lowers it by one when falling but not below the map's lower limit plus one, and holds it when the temperature is flat. When the zone drops below the threshold with a falling trend, the instance is released. The device ends up at the highest target among its instances. So yes, it can oscillate: in my measurement the load sat between what fan level one and fan level three could handle, and the zone cycled between 75 and 80 degrees every six and a half seconds, going 1, 2, 3, back to 1. The hysteresis sets the amplitude; you fix it with wider hysteresis, different map limits, or finer-grained cooling states.

**Q.** How would you test thermal policy without a heat gun?

**A.** `CONFIG_THERMAL_EMULATION`의 `emul_temp`에 m°C를 써서 존이 읽는 값만 바꾼다. 센서 드라이버와 hwmon은 진짜 값을 계속 보고하므로 둘을 같이 기록하면 구분된다. step_wise는 추세를 보므로 값을 하나 쓰고 기다리면 단계가 안 오른다 — 조금씩 올려 써야 한다. critical 경로도 emul로 시험할 수 있지만 일회용 부팅에서 한다. 출하 빌드에서는 emulation을 끈다. 진짜 온도가 critical을 넘으면 커널이 emul을 무시하는 안전장치는 있다.

> I use the emul_temp attribute from CONFIG_THERMAL_EMULATION, which overrides only the temperature the zone sees; the hwmon sensor keeps reporting the real value, so I log both. Because step_wise reacts to the trend, writing one value and waiting won't escalate — you step the value up. I also test the critical path this way, in a disposable boot. Emulation should be disabled in production builds, although the core ignores the emulated value once the real temperature is above critical.

**Q.** The board reached the critical trip but didn't shut down. Where do you look?

**A.** critical은 커널이 직접 끄는 게 아니라 `orderly_poweroff()`가 `poweroff_cmd`(기본 `/sbin/poweroff`)를 usermode helper로 실행하는 것이다. helper가 실행되기만 하면 커널은 성공으로 보고 강제 종료를 안 한다. 내 랩에서 BusyBox `poweroff`가 PID 1(셸 스크립트)에 SIGUSR2를 보냈지만 처리되지 않아 100 °C로 계속 돌았다. 확인할 것: `poweroff_cmd`와 init의 궁합, `CONFIG_THERMAL_EMERGENCY_POWEROFF_DELAY_MS`(0이면 백업 강제 종료 없음), 존 `mode`, `critical-action`, 그리고 그 아래 하드웨어 TSHUT.

> A critical trip doesn't power the machine off directly; it calls orderly_poweroff, which runs poweroff_cmd as a usermode helper. If that helper merely starts, the kernel considers it done and doesn't force anything. In my lab the busybox poweroff signalled PID 1, which was a shell script that ignored it, and the system kept running at a hundred degrees. So I check that poweroff_cmd matches the init system, that CONFIG_THERMAL_EMERGENCY_POWEROFF_DELAY_MS is a positive value so there's a forced fallback, that the zone isn't disabled, and that a hardware thermal shutdown exists underneath.

**Q.** hwmon versus IIO for a new temperature sensor driver?

**A.** 느리게 변하는 헬스 값(보드 온도, 레일 전압, 팬)이고 `sensors`·thermal에 붙어야 하면 hwmon이다. `HWMON_C_REGISTER_TZ`로 thermal zone 센서까지 공짜로 된다. 고속 샘플링·버퍼·트리거·타임스탬프가 필요한 측정용(IMU, 고속 ADC)이면 IIO다. 둘 다 필요하면 IIO로 쓰고 `iio-hwmon`으로 연결하거나, 열 관리용이면 `thermal-generic-adc`를 쓴다.

> If it's a slowly changing health value — board temperature, rail voltage, fan speed — that should show up in lm-sensors and feed the thermal framework, I write an hwmon driver with the info API; HWMON_C_REGISTER_TZ makes it a thermal sensor for free. If it's a measurement channel that needs fast sampling, buffers, triggers and timestamps, it's IIO. When both are needed, I expose it through IIO and bridge it with iio-hwmon or thermal-generic-adc.

**Q.** How does a fuel gauge driver report battery state to user space?

**A.** `power_supply_desc`에 속성 목록과 `get_property`를 주고 `devm_power_supply_register()`. 단위는 µV, µA(방전 음수), 0.1 °C, %다. 셀 정격은 DT `simple-battery`를 `monitored-battery`로 가리키고 `power_supply_get_battery_info()`로 읽는다 — OCV 표까지 있어 `power_supply_batinfo_ocv2cap()`로 잔량을 계산할 수 있다. 값이 바뀌면 `power_supply_changed()` → uevent. 바뀔 때만 불러야 한다. TEMP가 있으면 thermal zone, POWER_SUPPLY_HWMON이면 hwmon도 자동으로 생긴다.

> The driver fills a power_supply_desc with its property list and a get_property callback and registers it. Values follow the ABI units: microvolts, microamps with discharge negative, tenths of a degree, and percent. The cell's ratings come from a simple-battery node referenced by monitored-battery, read with power_supply_get_battery_info, including the OCV table, so the driver can convert voltage to capacity. When something changes, it calls power_supply_changed, which emits a uevent — only on real changes, otherwise you flood user space. The core also creates a thermal zone if there's a temperature property, and an hwmon device.

**Q.** Polling versus interrupt-driven thermal zones — when do you use which?

**A.** 센서에 ALERT 핀과 한계 레지스터가 있으면 `polling-delay = 0` + `set_trips` + IRQ에서 `thermal_zone_device_update()`(threaded IRQ에서). 감지가 즉시이고 평소에 CPU를 깨우지 않는다. passive trip이 걸린 뒤에는 governor가 단계를 조정해야 하므로 `polling-delay-passive`로 폴링한다. ALERT가 없으면 폴링이고, 주기는 열 시간 상수와 허용 오버슈트로 정한다 — 내 모델(시간 상수 4초)에서 1초 폴링은 최대 7 °C 오버슈트였다.

> If the sensor has an alert pin with limit registers, I set polling-delay to zero, implement set_trips to program the window, and call thermal_zone_device_update from the threaded interrupt handler. Detection is immediate and the CPU isn't woken up periodically. Once a passive trip is active I still poll at the passive interval so the governor can step. Without an alert pin I poll, and pick the period from the thermal time constant and the overshoot I can tolerate — with a four-second time constant, one-second polling overshot by about seven degrees in my lab.

---

## 15. 직접 해보기

1. **개념**: trip 60 °C/hyst 2, 80 °C/hyst 5인 존에서 온도가 59 → 61 → 79 → 81 → 76 → 74 → 57 °C로 움직일 때, 각 시점의 `temp1_min`/`temp1_max`(set_trips 창)를 적어라. 정답/힌트: 59 → (−∞, 60000), 61 → (57999, 80000), 79 → 같음, 81 → (74999, 95000), 76 → 같음, 74 → (57999, 80000), 57 → (−∞, 60000). 아래 값은 threshold − 1이다.
2. **코드**: `i4_temp`에 `HWMON_T_CRIT_HYST`를 추가하고 DT critical trip과 같은 값을 보고하게 하라. 그리고 `get_trend`를 직접 주는 방법(존을 hwmon 경유가 아니라 `devm_thermal_of_zone_register()`로 직접 등록)으로 바꿔 보라. 정답/힌트: 직접 등록하면 `thermal_zone_device_ops`에 `.get_temp`, `.set_trips`를 드라이버가 채운다. `HWMON_C_REGISTER_TZ`는 빼야 같은 센서가 두 번 등록되지 않는다.
3. **튜닝**: §4의 limit cycle을 없애 보라. map1을 `<&i4_fan 1 3>`으로 바꾸거나 trip 1의 hysteresis를 10000으로 바꾸고 `trans_table`의 전이 횟수를 비교하라. 정답/힌트: 측정해서 비교한다. 하한을 낮추면 내려가는 중의 최소 state가 2가 된다(lower+1).
4. **관찰**: Image-dbg에서 `thermal_zone_trip` 이벤트만 켜고 emul_temp를 65000으로 쓴 뒤 1초에 몇 번 찍히는지 세라. 그다음 50000을 쓰면 어떻게 되는가. 정답/힌트: throttle 중에는 passive 폴링(250 ms)마다 한 번씩, 풀리면 멈춘다(그리고 1초 폴링으로 돌아간다).
5. **critical**: `/proc/sys/kernel/poweroff_cmd`를 기본값으로 두고, critical 후에도 확실히 꺼지게 하는 방법을 커널 설정과 부트 인자 측면에서 두 가지 들어라. 정답/힌트: `CONFIG_THERMAL_EMERGENCY_POWEROFF_DELAY_MS`를 양수로(백업 강제 종료), init을 BusyBox init처럼 SIGUSR2를 처리하는 것으로. DT `critical-action = "reboot"`는 종료 대신 재부팅을 고를 뿐이다.
6. **power supply**: `i4_batt`에 `STATUS`를 `drain_ma`가 음수면 Charging으로 바꾸고, `CHARGE_NOW`(µAh)를 추가하라. 그리고 `i4_uevent power_supply STATUS`로 충전 시작 이벤트를 받아라. 정답/힌트: `drain_ma`를 바꾸는 것만으로는 uevent가 안 나간다 — 파라미터 set 콜백에서 `power_supply_changed()`를 불러야 한다. test_power는 `param_ops_<이름>`을 정의하고 `module_param(ac_online, ac_online, 0644)`처럼 등록해 그렇게 한다(`module_param_cb`도 같은 방법).
7. **sysfs 탐색**: 아무 Linux 노트북에서 `for z in /sys/class/thermal/thermal_zone*; do echo $(cat $z/type) $(cat $z/temp); done`와 `grep . /sys/class/hwmon/hwmon*/name`을 돌려, 어떤 존이 DT/ACPI에서 왔고 어떤 hwmon이 thermal zone의 그림자(THERMAL_HWMON)인지 구분하라. 정답/힌트: DT 존은 hwmon을 안 만든다(`no_hwmon`). ACPI 존(`acpitz`)은 hwmon 이름도 `acpitz`로 보인다.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| hwmon | hardware monitoring 클래스 | 온도·전압·전류·전력·팬을 고정 이름·단위로 보고하는 sysfs 규약 |
| `hwmon_chip_info` | 채널 표 + ops | `_with_info` 등록 API에 넘기는 칩 설명 |
| `HWMON_C_REGISTER_TZ` | hwmon chip 속성 비트 | temp 입력 채널을 thermal_of 센서로 자동 등록 |
| thermal zone | 온도 하나 + 정책 | `/sys/class/thermal/thermal_zoneN`, type은 DT 노드 이름 |
| trip point | 경계 온도 | passive · active · hot · critical, 각자 hysteresis |
| hysteresis | 해제 여유 | 넘은 trip은 trip − hyst 아래로 내려가야 풀린다 |
| threshold | 코어 내부 경계값 | 안 넘은 trip은 trip 온도, 넘은 trip은 trip − hyst |
| cooling device | 냉각 수단 | 0..max_state 정수로 세기를 정함, `set_cur_state` |
| thermal instance | trip-cdev 바인딩 | cooling map 한 줄 = instance 하나, lower/upper/target |
| governor | 결정 알고리즘 | step_wise, power_allocator, fair_share, bang_bang, user_space |
| `set_trips` | 경보 창 설정 콜백 | 코어가 센서에 (low, high)를 줘서 인터럽트 방식 가능 |
| `emul_temp` | 온도 에뮬레이션 | 존이 읽는 온도만 덮어씀, 정책 시험용 |
| `orderly_poweroff` | 정돈된 종료 요청 | `poweroff_cmd`를 usermode helper로 실행 |
| `hw_protection` | 하드웨어 보호 동작 | critical에서 shutdown 또는 reboot |
| power_supply | 전원 공급원 클래스 | 배터리·충전기·어댑터, `power_supply_changed` → uevent |
| `simple-battery` | 배터리 정격 DT 바인딩 | 용량·전압 범위·내부 저항·OCV 표 |
| OCV | open-circuit voltage | 부하 없는 셀 전압, 잔량과 1:1에 가까운 관계 |
| uevent | kobject 이벤트 | netlink `NETLINK_KOBJECT_UEVENT`로 유저 공간에 전달 |
| TSHUT | thermal shutdown (HW) | 소프트웨어와 무관하게 칩/PMIC가 전원을 끊는 최후 보호 |

---

## 17. 요약 & 체크리스트

Linux의 열·전원 관리는 **측정(hwmon) · 판단(thermal zone + trip + governor) · 행동(cooling device)**, 그리고 배터리를 보는 **power_supply**로 나뉜다. 센서 드라이버는 표준 단위로 값만 주고(`hwmon_chip_info`, m°C/mV/mA/µW), `HWMON_C_REGISTER_TZ` 한 비트로 DT `thermal-zones`의 센서가 된다. trip은 올라갈 때 trip 온도, 내려올 때 trip − hysteresis에서 바뀌고, 코어는 그 창을 `set_trips`로 센서에 내려보내 인터럽트 방식을 가능하게 한다. step_wise는 추세를 보고 state를 한 단계씩 움직이며, 냉각 단계 사이에 부하가 걸리면 limit cycle을 만든다 — 이 노트에서 75~80 °C, 6.5초 주기로 측정했다. 폴링 주기는 곧 감지 지연이고 오버슈트다(1초 폴링에서 7 °C). critical은 governor 없이 `orderly_poweroff()`로 가지만 유저 공간이 따르지 않으면 꺼지지 않으므로 백업 강제 종료와 하드웨어 TSHUT까지 확인해야 한다. power_supply 드라이버는 µV·µA·0.1 °C 단위로 보고하고, `simple-battery` DT로 셀 정격을 받고, 값이 바뀔 때만 uevent를 보낸다.

- [ ] hwmon sysfs 단위(temp m°C, in mV, curr mA, power µW, fan RPM)와 인덱스 규칙(in만 0부터)을 설명할 수 있다
- [ ] `HWMON_CHANNEL_INFO`와 `hwmon_ops`로 다채널 hwmon 드라이버를 짤 수 있다
- [ ] `HWMON_C_REGISTER_TZ`가 하는 일과 `#thermal-sensor-cells` 인자의 의미를 설명할 수 있다
- [ ] DT `thermal-zones`에 polling-delay·trips·cooling-maps를 쓰고 `<&cdev min max>`의 뜻을 설명할 수 있다
- [ ] `thermal_cooling_device_ops` 세 콜백으로 cooling device를 짜고 cooling map에 묶을 수 있다
- [ ] trip의 threshold·hysteresis와 `set_trips` 창(`temp1_min`/`temp1_max`)이 어떻게 움직이는지 설명할 수 있다
- [ ] step_wise의 규칙(오름 +1, 내림 −1 단 하한 lower+1, 같음 유지, 해제)으로 측정 시계열을 해석할 수 있다
- [ ] polling과 interrupt 방식의 감지 지연 차이를 측정으로 보여 줄 수 있다
- [ ] `emul_temp`로 정책을 시험하고, 존 온도와 hwmon 온도가 왜 다른지 설명할 수 있다
- [ ] critical trip이 꺼지지 않는 원인 세 가지(poweroff_cmd/init, 강제 종료 지연 0, mode disabled)를 진단할 수 있다
- [ ] `power_supply_desc`와 `simple-battery`로 연료 게이지 드라이버를 짜고 단위·uevent 규칙을 지킬 수 있다
- [ ] thermal tracepoint(`thermal_temperature`, `cdev_update`, `thermal_zone_trip`)로 제어 루프를 커널 쪽에서 확인할 수 있다

---

## 참고 자료

- docs.kernel.org — hwmon sysfs ABI: [Naming and data format standards for sysfs files](https://docs.kernel.org/hwmon/sysfs-interface.html)
- docs.kernel.org — hwmon 커널 API: [The Linux Hardware Monitoring kernel API](https://docs.kernel.org/hwmon/hwmon-kernel-api.html)
- docs.kernel.org — thermal: [Generic Thermal Sysfs driver How To](https://docs.kernel.org/driver-api/thermal/sysfs-api.html), [Power allocator governor tunables](https://docs.kernel.org/driver-api/thermal/power_allocator.html)
- docs.kernel.org — power supply: [Linux power supply class](https://docs.kernel.org/power/power_supply_class.html)
- DT 바인딩 (커널 소스 트리): `Documentation/devicetree/bindings/thermal/thermal-zones.yaml`, `.../thermal/thermal-cooling-devices.yaml`, `.../power/supply/battery.yaml`, `.../hwmon/gpio-fan.yaml`
- 랩 소스(6.18.55)에서 읽은 파일: `drivers/thermal/thermal_core.c`(`__thermal_zone_device_update`, `thermal_zone_device_halt`), `drivers/thermal/gov_step_wise.c`, `drivers/thermal/thermal_of.c`, `drivers/thermal/thermal_helpers.c`(emul), `drivers/hwmon/hwmon.c`(`hwmon_thermal_*`), `drivers/hwmon/gpio-fan.c`, `drivers/power/supply/power_supply_core.c`, `drivers/power/supply/test_power.c`, `kernel/reboot.c`(`__hw_protection_trigger`, `__orderly_poweroff`)
- elixir.bootlin.com에서 같은 파일을 버전별로 볼 수 있다: [gov_step_wise.c](https://elixir.bootlin.com/linux/v6.18/source/drivers/thermal/gov_step_wise.c)
- 관련 노트: D4(DT 리소스 문법), G6(sysfs·uevent), H3(regulator — dummy regulator 경고), H4(gpio-sim), F7(workqueue), I9(PWM·watchdog), M(ftrace), N(전원 관리)
