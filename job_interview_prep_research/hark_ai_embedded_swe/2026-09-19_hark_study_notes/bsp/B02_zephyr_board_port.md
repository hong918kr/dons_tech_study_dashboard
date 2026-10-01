# B02. Zephyr 보드 포팅 실전 — board.yml에서 "안 켜지는 보드"까지

> **시리즈**: BSP 집중 2/6 · **선행**: B01(BSP 해부), C03 10절(Zephyr device model), C04 14절(west·Kconfig·devicetree) · **JD 근거**: "Own BSP development, peripheral driver integration (SPI, I2C, UART, I2S), and RTOS task scheduling"
> **Don 상태**: ❌ → 🟡. Zephyr 보드를 포팅해 본 적이 없다. 이 노트는 "읽고 아는 척"이 아니라 **nRF52840 DK 하나로 실제로 따라 할 수 있는 절차**를 만드는 게 목적이다. 여기 있는 명령은 전부 실행 가능한 것만 적었다.
> **이 노트를 다 읽으면**: ① 새 보드를 Zephyr에 올릴 때 만들어야 하는 파일과 각 파일의 역할을 말할 수 있다 ② devicetree / Kconfig / C 코드 중 무엇을 어디에 써야 하는지 판단할 수 있다 ③ 보드가 안 켜질 때 빌드 산출물(`zephyr.dts`, `.config`, `devicetree_generated.h`)로 원인을 좁힐 수 있다.

> **버전 경고**: Zephyr는 **hardware model v2**로 보드 구조가 크게 바뀌었다(`board.yml` 도입, 보드 이름이 `nrf52840dk/nrf52840` 형태로). 이 노트는 2026-09-21 기준 `docs.zephyrproject.org/latest`와 zephyr `main` 브랜치의 실제 보드 파일을 확인해 썼다. 회사 코드베이스가 구버전이면 `boards/arm/<board>/` 같은 옛 구조일 수 있으니 **먼저 트리 구조를 보고 버전을 확인**해야 한다.

---

## 0. 큰 그림 — 빌드 한 번에 무슨 일이 일어나나

```
  boards/<vendor>/<board>/                       앱 디렉터리
   ├ board.yml (보드·SoC·리비전)                   ├ prj.conf
   ├ Kconfig.<board> (SoC select) / Kconfig.defconfig ├ app.overlay
   ├ <board>_<qual>.dts / -pinctrl.dtsi            ├ boards/<board>.overlay
   ├ <board>_<qual>_defconfig                      ├ CMakeLists.txt
   └ board.cmake (flash·debug 러너)                 └ src/main.c
              └───────────────┬────────────────┘
                              ▼
   .dts + .dtsi + overlay ──► C 전처리기 ──► dtc/edtlib + bindings(YAML) 검증
        ├─► build/zephyr/zephyr.dts                      (최종 트리, 사람이 읽는 것)
        └─► build/zephyr/include/generated/zephyr/devicetree_generated.h
   prj.conf + defconfig ──► Kconfig ──► build/zephyr/.config, autoconf.h (CONFIG_*)
                              ▼
        CMake + ninja ──► zephyr.elf / zephyr.hex ──► west flash
```

기억할 한 줄: **devicetree는 "하드웨어가 무엇인가", Kconfig는 "소프트웨어를 어떻게 빌드할 것인가"**다. 이 둘을 헷갈리는 게 Zephyr 입문자의 1순위 실수고, 면접에서도 그대로 물어본다(13절 Q4).

---

## 1. 이름부터 — hardware model v2의 보드 타깃

### 1.1 보드 타깃 문법

공식 문서 기준 보드 타깃(build target)의 형태는 이렇다.

```
board_name[@revision][/SoC[/CPU_cluster][/variant]]
```

예시: `nrf52840dk/nrf52840`, `bl5340_dvk@1.2.0/nrf5340/cpuapp/ns`.

| 요소 | 뜻 · 예 |
|---|---|
| board_name | PCB 이름. `nrf52840dk` |
| revision | 보드 리비전. 같은 PCB의 EVT/DVT 차이 (8절) |
| SoC | 그 보드에 올라간 칩. 한 보드에 여러 SoC 옵션이 있을 수 있다 |
| CPU cluster | 멀티코어 SoC의 어느 코어. nRF5340의 `cpuapp` / `cpunet` |
| variant | 같은 코어의 다른 빌드 구성. TrustZone non-secure `ns` 등 |

`/`는 파일 이름에서는 `_`로 정규화된다. 즉 `nrf52840dk/nrf52840`의 dts 파일 이름은 `nrf52840dk_nrf52840.dts`다. 이 규칙 하나만 알아도 트리에서 파일을 찾는 속도가 달라진다. 보드 목록은 `west boards`(필터: `west boards | grep -i nrf52840`)로 본다.

---

## 2. 보드 디렉터리에 실제로 들어가는 파일

### 2.1 필수 3개 + 선택

공식 board porting guide 기준, 보드 `plank` 하나에 **반드시** 필요한 것은 셋이다.

```
boards/<VENDOR>/plank/ ├ board.yml              보드·SoC·variant·revision 메타데이터
                       ├ Kconfig.plank          Kconfig 트리에서 SoC를 select
                       └ plank_<qualifiers>.dts 하드웨어 기술
```

| 선택 파일 | 역할 |
|---|---|
| `board.cmake` | `west flash` / `west debug` 러너 인자 |
| `<board>_<qual>_defconfig` | 이 보드에서 항상 켜야 하는 Kconfig (`.conf` 문법) |
| `Kconfig.defconfig` | 기존 심볼의 **기본값**을 보드에 맞게 바꿈 |
| `Kconfig` | 보드 고유의 새 Kconfig 심볼(프롬프트 있음) |
| `<board>_<qual>.yaml` | twister(테스트 러너) 메타데이터 |
| `<board>_<qual>-pinctrl.dtsi` | 핀 먹싱 정의 (7절) |
| `CMakeLists.txt` + `board.c`, `doc/index.rst` | 보드 전용 C 코드, 보드 문서 |

실제 트리(zephyr `main`의 `boards/nordic/nrf52840dk/`)가 정확히 이 조합이다 — `board.yml`, `Kconfig.nrf52840dk`, `Kconfig.defconfig`, `board.cmake`, `CMakeLists.txt`, `pre_dt_board.cmake`, `nrf52840dk_nrf52840{.dts, -pinctrl.dtsi, _defconfig, .yaml}`, 그리고 같은 PCB의 nRF52811 에뮬레이션용 세트가 한 벌 더.

**`board.yml`** — 보드 이름·벤더와 어떤 SoC/variant가 있는지.

```yaml
board:
  name: nrf52840dk
  full_name: nRF52840 DK
  vendor: nordic
  socs:
  - name: nrf52840
  - name: nrf52811       # 같은 PCB를 nRF52811 에뮬레이션 모드로도 쓴다
```

`socs:` 아래에 `variants:`를 두면 `/ns` 같은 variant가 생긴다. 리비전은 8절.

### 2.3 Kconfig.\<board\>

```
config BOARD_NRF52840DK
	select SOC_NRF52840_QIAA if BOARD_NRF52840DK_NRF52840
	select SOC_NRF52811_QFAA if BOARD_NRF52840DK_NRF52811
```

여기서 하는 일은 **SoC를 고르는 것뿐**이다. 문서는 "재사용 가능한 board/SoC Kconfig 트리 바깥의 것을 여기서 select 하지 말라"고 못 박는다. 드라이버를 켜고 싶으면 defconfig로 간다.

### 2.4 \<board\>\_\<qual\>\_defconfig vs Kconfig.defconfig

가장 헷갈리는 한 쌍이다.
| 파일 | 문법 | 하는 일 | 앱이 덮어쓸 수 있나 |
|---|---|---|---|
| `nrf52840dk_nrf52840_defconfig` | `.conf` (`CONFIG_X=y`) | 이 보드에서 **켜 둘 것**(GPIO, SERIAL, CONSOLE 등) | ✅ 앱 설정이 우선 |
| `Kconfig.defconfig` | Kconfig 문법 (`config X` + `default`) | 기존 심볼의 **기본값**을 보드에 맞게 변경 | ✅ 사용자가 바꿀 수 있는 기본값 |

실제 내용은 이렇다.
```
# nrf52840dk_nrf52840_defconfig          # Kconfig.defconfig
CONFIG_ARM_MPU=y                         if BOARD_NRF52840DK
CONFIG_GPIO=y                            config HW_STACK_PROTECTION
CONFIG_SERIAL=y                             default ARCH_HAS_STACK_PROTECTION
CONFIG_CONSOLE=y                         endif # BOARD_NRF52840DK
CONFIG_UART_CONSOLE=y
```

기준선: **"이 보드는 이게 없으면 부팅 로그도 안 나온다" → defconfig. "이 보드에서는 이 값이 더 적절하다" → Kconfig.defconfig.**

### 2.5 board.cmake와 twister yaml

```cmake
board_runner_args(jlink "--device=nRF52840_xxAA" "--speed=4000")
board_runner_args(pyocd "--target=nrf52840" "--frequency=4000000")
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
```

이게 `west flash --runner jlink`가 동작하는 이유다. 여러 개 include 하면 첫 번째가 기본이 되고 `--runner`로 고른다. twister용 yaml은 이렇다.

```yaml
identifier: nrf52840dk/nrf52840
name: nRF52840-DK-NRF52840
type: mcu
arch: arm
ram: 256          # KB
flash: 472        # KB
toolchain: [zephyr]
supported: [gpio, i2c, spi, i2s, bluetooth]
vendor: nordic
```

`supported:` 목록은 twister가 "이 보드에서 어떤 테스트를 돌릴 수 있나"를 판단하는 근거다. 보드를 새로 만들면 **여기에 적은 만큼 CI가 검증해 준다** — BSP 오너가 이 줄을 늘려 가는 게 곧 회귀 테스트 커버리지다.

---

## 3. Devicetree

### 3.1 무엇을 어디에 쓰나

| 질문 | 들어갈 곳 |
|---|---|
| I2C 센서가 주소 0x39로 붙어 있다, UART 보레이트 초기값 | devicetree (`reg`, `current-speed`) |
| 그 센서 드라이버를 빌드에 포함할 것인가, 로그 레벨·힙·스택 크기 | Kconfig |
| 런타임에 조건에 따라 바꾸는 값 | C 코드 |

경계가 흐린 것도 있다(예: 버퍼 크기는 둘 다 가능). 기준은 **"하드웨어를 바꿔야 달라지는 값이면 devicetree"**다.

**파일 종류.** `.dts`는 보드 하나의 최상위 소스로 `/dts-v1/;`로 시작한다. `.dtsi`는 include 되는 조각(SoC 정의, pinctrl, 공통 보드 부분), `.overlay`는 기존 트리를 덮어쓰는 앱·리비전·스니펫용 조각이다. 바인딩 `.yaml`은 노드의 `compatible`에 대응하는 스키마이고 `dts/bindings/<subsystem>/<vendor>,<device>.yaml`에 둔다.

### 3.3 실제 보드 dts의 뼈대

```dts
/dts-v1/;
#include <nordic/nrf52840_qiaa.dtsi>
#include <nordic/nrf52840_partition.dtsi>
#include "nrf52840dk_nrf52840-pinctrl.dtsi"

/ {
	model = "Nordic nRF52840 DK NRF52840";
	compatible = "nordic,nrf52840-dk-nrf52840";

	chosen {
		zephyr,console = &uart0;
		zephyr,shell-uart = &uart0;
		zephyr,ieee802154 = &ieee802154;
	};

	leds {
		compatible = "gpio-leds";
		led0: led_0 {
			gpios = <&gpio0 13 GPIO_ACTIVE_LOW>;
			label = "Green LED 0";
		};
	};
	buttons {
		compatible = "gpio-keys";
		button0: button_0 {
			gpios = <&gpio0 11 (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
			zephyr,code = <INPUT_KEY_0>;
		};
	};
	aliases { led0 = &led0; sw0 = &button0; };
};
```

읽는 법 넷. ① SoC `.dtsi`가 주변장치 노드를 전부 정의해 두되 대부분 `status = "disabled"`이고, 보드 dts는 쓰는 것만 `okay`로 켠다. ② `led0:`는 devicetree 라벨이라 `&led0`로 참조한다(`label = "..."` 속성과 다른 것이다). ③ `gpios = <&gpio0 13 GPIO_ACTIVE_LOW>`의 `13`은 핀 번호, 뒤는 플래그다. active-low를 여기서 선언해 두면 앱은 `gpio_pin_set_dt(&led, 1)`만 하면 된다. ④ `chosen`과 `aliases`가 **B01 7절의 "역할 → 인스턴스 매핑"**이다.

### 3.4 chosen과 aliases

`chosen`은 시스템 전역 역할(`zephyr,console`, `zephyr,sram`, `zephyr,flash`, `zephyr,code-partition`, `zephyr,shell-uart` 등)을 지정한다. `aliases`는 앱이 쓰는 별명(`led0`, `sw0`)이다. 앱 코드는 이렇게 받는다.

```c
#include <zephyr/drivers/gpio.h>
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int main(void)
{
    if (!gpio_is_ready_dt(&led)) { return -ENODEV; }  /* alias가 없다는 뜻 */
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    return 0;
}
```

보드를 바꿔도 이 코드는 그대로다. 보드 dts가 `led0` alias만 제공하면 된다 — 이것이 Zephyr 보드 포팅의 목적 그 자체다.

### 3.5 오버레이: 어떤 파일이 자동으로 적용되나

앱 디렉터리에서 **자동으로 발견되는 순서**(공식 devicetree how-to 기준)는 `socs/<SOC>_<BOARD_QUALIFIERS>.overlay` → `boards/<BOARD>.overlay` → `boards/<BOARD>_<revision>.overlay` → `<BOARD>.overlay` → `app.overlay`다. 명시적으로 줄 때는 CMake 변수를 쓴다. `DTC_OVERLAY_FILE`은 기본 목록을 대체하고, `EXTRA_DTC_OVERLAY_FILE`은 더 높은 우선순위로 추가한다.

```sh
west build -b nrf52840dk/nrf52840 app -- -DEXTRA_DTC_OVERLAY_FILE=my_sensor.overlay
```

오버레이 예시(기존 I2C 버스에 센서를 하나 붙인다).

```dts
&i2c0 {
	status = "okay";
	clock-frequency = <I2C_BITRATE_FAST>;
	mysensor: mysensor@39 {
		compatible = "acme,mysensor";
		reg = <0x39>;
		int-gpios = <&gpio0 3 GPIO_ACTIVE_LOW>;
	};
};
```

### 3.6 바인딩 YAML

`compatible = "acme,mysensor"`를 쓰면 빌드 시스템은 `dts/bindings/` 아래에서 `acme,mysensor.yaml`을 찾는다. 없으면 노드는 조용히 무시되고 **드라이버가 아예 인스턴스화되지 않는다** — "코드는 있는데 디바이스가 없다"의 1순위 원인이다.

```yaml
description: ACME mysensor ambient light sensor
compatible: "acme,mysensor"
include: [sensor-device.yaml, i2c-device.yaml]
properties:
  int-gpios:
    type: phandle-array
    description: Interrupt line (active low)
  gain:
    type: int
    default: 1
    enum: [1, 4, 16, 64]
```

`include:`로 공통 속성(`reg`, `status`, 버스별 속성)을 가져오고 이 디바이스만의 속성을 아래에 적는다. `bus:`는 이 노드가 버스 컨트롤러임을, `on-bus:`는 특정 버스에 붙는 디바이스임을 뜻한다. child-binding으로 자식 노드 스키마도 줄 수 있다.

### 3.7 결과를 확인하는 법

```sh
west build -b nrf52840dk/nrf52840 app --cmake-only
less build/zephyr/zephyr.dts   # 오버레이·include가 전부 적용된 최종 트리
less build/zephyr/include/generated/zephyr/devicetree_generated.h  # 앞부분에 binding 목록
```

디버깅 규칙 하나: **`zephyr.dts`에 없으면 코드에도 없다.** 오버레이를 썼는데 동작하지 않으면 먼저 이 파일에서 노드와 `status`를 확인한다.

---

## 4. Kconfig

### 4.1 설정이 합쳐지는 순서

공식 문서 기준으로 세 갈래가 병합된다. ① 보드의 `<BOARD>_defconfig` ② `CONFIG_`로 시작하는 CMake 캐시 항목 ③ 애플리케이션 설정(`CONF_FILE` 직접 지정 → `boards/<BOARD>.conf`·`boards/<BOARD>_<revision>.conf` + `prj.conf` → `prj.conf`). **같은 심볼이 보드 defconfig와 앱 설정에 모두 있으면 앱 설정이 이긴다.**

```sh
# 추가 조각을 더할 때
west build -b nrf52840dk/nrf52840 app -- -DEXTRA_CONF_FILE=debug.conf
```

**`.config`를 직접 고치지 말 것.** 병합 결과는 `build/zephyr/.config`에 저장된다. 이 파일이 소스 설정 파일보다 새로우면 **그대로 재사용**되므로, 여기서 고친 값은 설정 파일을 건드리는 순간 사라진다. 실험은 `menuconfig`로 하되 확정된 값은 `prj.conf`나 보드 파일로 옮긴다.

```sh
west build -t menuconfig          # 터미널 UI
west build -t guiconfig           # GUI
grep CONFIG_I2C build/zephyr/.config   # 실제로 뭐가 켜졌는지 확인
```

### 4.3 보드 포팅에서 자주 쓰는 심볼

| 심볼 | 언제 |
|---|---|
| `CONFIG_SERIAL`·`CONFIG_CONSOLE`·`CONFIG_UART_CONSOLE`, `CONFIG_GPIO`/`I2C`/`SPI`/`I2S` | 콘솔 최소 3종과 서브시스템 활성화. devicetree만으론 부족하다 |
| `CONFIG_LOG`, `CONFIG_LOG_MODE_IMMEDIATE` | 초기 bring-up에서는 immediate가 안전(버퍼링 중 죽으면 로그가 사라진다) |
| `CONFIG_ARM_MPU`, `CONFIG_HW_STACK_PROTECTION` | 스택 오버플로 검출 |
| `CONFIG_BOOT_BANNER`, `CONFIG_DEBUG_OPTIMIZATIONS` | 부팅 확인, 디버깅 중 `-Og` |

---

## 5. 드라이버 인스턴스화 — DEVICE_DT_DEFINE과 DT_INST 매크로

### 5.1 핵심 매크로 (공식 API 문서 기준 인자 순서)

```c
DEVICE_DT_DEFINE(node_id, init_fn, pm, data, config, level, prio, api, ...)
DEVICE_DT_INST_DEFINE(inst, ...)     /* 위와 같되 첫 인자가 인스턴스 번호 */
DEVICE_DT_GET(node_id)  DEVICE_DT_INST_GET(inst)  DEVICE_DT_GET_ONE(compat)
bool device_is_ready(const struct device *dev);
const struct device *device_get_binding(const char *name);   /* 문자열 검색 */
```

`device_get_binding()`은 문자열 검색이라 런타임 비용이 있다. 새 코드는 **`DEVICE_DT_GET` + `device_is_ready()`** 조합을 쓴다.

### 5.2 DT_DRV_COMPAT 패턴

드라이버 한 파일이 "compatible이 같은 모든 노드"에 대해 인스턴스를 만드는 것이 Zephyr의 표준 패턴이다.

```c
#define DT_DRV_COMPAT acme_mysensor      /* "acme,mysensor" → 쉼표·하이픈은 _ */

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
struct mysensor_config { struct i2c_dt_spec bus; uint8_t gain; };
struct mysensor_data   { int32_t last; };

static int mysensor_init(const struct device *dev)
{
    const struct mysensor_config *cfg = dev->config;
    /* 0 = 성공, 실패는 음수 errno */
    return i2c_is_ready_dt(&cfg->bus) ? 0 : -ENODEV;
}

static DEVICE_API(sensor, mysensor_api) = { /* .sample_fetch, .channel_get ... */ };
#define MYSENSOR_DEFINE(inst)                                           \
    static struct mysensor_data mysensor_data_##inst;                   \
    static const struct mysensor_config mysensor_config_##inst = {      \
        .bus  = I2C_DT_SPEC_INST_GET(inst),                             \
        .gain = DT_INST_PROP(inst, gain),                               \
    };                                                                  \
    DEVICE_DT_INST_DEFINE(inst, mysensor_init, NULL,                    \
                          &mysensor_data_##inst,                        \
                          &mysensor_config_##inst,                      \
                          POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,     \
                          &mysensor_api);

/* devicetree에서 status = "okay"인 노드마다 위 매크로를 한 번씩 편다 */
DT_INST_FOREACH_STATUS_OKAY(MYSENSOR_DEFINE)
```

이 구조의 장점은 **보드가 센서를 두 개 달아도 드라이버 코드가 한 줄도 안 바뀐다**는 것이다. dts에 노드를 하나 더 추가하면 인스턴스가 하나 더 생긴다. 실제 in-tree 드라이버(예: `drivers/sensor/bosch/bme280/bme280.c`)가 정확히 이 형태이고, 센서 서브시스템은 `SENSOR_DEVICE_DT_INST_DEFINE` 래퍼를 쓴다.

**앱에서 쓰기.**

```c
static const struct device *const sensor = DEVICE_DT_GET(DT_NODELABEL(mysensor));

int app_start(void)   /* false면 init 실패 또는 status가 okay가 아니다 */
{
    return device_is_ready(sensor) ? 0 : -ENODEV;
}
```

`device_is_ready()`가 false인 원인은 대개 셋이다. ① dts 노드가 `disabled` ② binding을 못 찾아 노드가 무시됨 ③ init 함수가 0이 아닌 값을 반환(전원·버스 문제). 이 셋을 순서대로 확인하는 게 11절의 디버깅 흐름이다.

---

## 6. SYS_INIT과 초기화 레벨

### 6.1 레벨과 우선순위

공식 문서 기준 `SYS_INIT(init_fn, level, prio)`이고, level은 여섯 가지다.

| 레벨 | 시점 | 커널 서비스 |
|---|---|---|
| `EARLY` | C 진입 직후, 부트 과정 아주 초기 | ❌ |
| `PRE_KERNEL_1` / `PRE_KERNEL_2` | 커널 초기화 컨텍스트, 인터럽트 스택 사용 (2는 1 이후) | ❌ |
| `POST_KERNEL` | 커널이 살아난 뒤. 커널 프리미티브 사용 가능 | ✅ |
| `APPLICATION` / `SMP` | `main()` 직전 / `CONFIG_SMP`일 때만 | ✅ |

`prio`는 0~999의 10진 정수 리터럴(또는 심볼 상수)이고 작을수록 먼저다. 같은 레벨 안의 순서를 정한다.

**보드 레벨 초기화 코드.** 보드 디렉터리에 `board.c`를 두고 CMakeLists.txt에 넣으면 보드 전용 초기화를 걸 수 있다(실제로 `boards/nordic/nrf9160dk/board.c`가 그렇다).

```c
#include <zephyr/init.h>
static int board_early_setup(void)
{
    /* 예: 외부 전원 스위치를 켜고 레일 안정까지 기다린다.
     * PRE_KERNEL에서는 k_sleep을 못 쓰므로 busy wait. */
    return 0;
}

/* 버스 드라이버보다 먼저, GPIO 드라이버보다는 나중에 */
SYS_INIT(board_early_setup, PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY + 1);
```

함정 셋. ① `PRE_KERNEL_*`에서는 `k_sleep()`·뮤텍스·로그 지연 처리 같은 커널 서비스를 못 쓴다. ② 우선순위를 숫자로 하드코딩하면 다른 드라이버가 옮겨 갔을 때 조용히 깨지므로 `CONFIG_*_INIT_PRIORITY` 기준으로 상대 지정한다. ③ init은 **성공 시 0, 실패 시 음수 errno**를 반환해야 하고, 0이 아니면 그 디바이스는 `device_is_ready()`가 false가 된다.

---

## 7. pinctrl

### 7.1 상태(state)라는 개념

핀 설정은 "한 번 하고 끝"이 아니다. 동작 중과 저전력 중이 다르다. Zephyr는 이걸 **state**로 모델링한다.

| 속성 | 뜻 |
|---|---|
| `pinctrl-0`, `pinctrl-1`, … | 인덱스 N번 상태의 설정 묶음 |
| `pinctrl-names` | 각 인덱스의 이름. 보통 `"default", "sleep"` |
| `PINCTRL_STATE_DEFAULT` / `PINCTRL_STATE_SLEEP` | 동작 상태 / 저전력·sleep 상태 |

```dts
&uart0 {
	pinctrl-0 = <&uart0_default>;
	pinctrl-1 = <&uart0_sleep>;
	pinctrl-names = "default", "sleep";
	current-speed = <115200>;
	status = "okay";
};
```

**핀 정의 (실제 nRF52840 DK 파일).**

```dts
&pinctrl {
	uart0_default: uart0_default {   /* grouping 방식: 설정을 공유하는 핀끼리 묶는다 */
		group1 {
			psels = <NRF_PSEL(UART_TX, 0, 6)>, <NRF_PSEL(UART_RTS, 0, 5)>;
		};
		group2 {
			psels = <NRF_PSEL(UART_RX, 0, 8)>, <NRF_PSEL(UART_CTS, 0, 7)>;
			bias-pull-up;
		};
	};
	uart0_sleep: uart0_sleep {
		group1 {
			psels = <NRF_PSEL(UART_RX, 0, 8)>, <NRF_PSEL(UART_RTS, 0, 5)>;
			low-power-enable;
		};
	};
};
```

grouping 방식이 권장 표현이다. 같은 설정을 공유하는 핀을 한 group에 묶는다. `psels`/`NRF_PSEL`은 Nordic 전용이고 일반적인 표현은 `pinmux = <...>`다 — **벤더마다 핀 지정 문법이 다르다**. 공통인 것은 `bias-pull-up`, `bias-pull-down`, `drive-open-drain`, `low-power-enable` 같은 표준 속성이다. `uart0_sleep`에서 RX를 `low-power-enable`로 두는 이유가 B01 4.2절의 "유휴 핀 누설을 막는다"이고, always-on 기기에서 sleep state를 안 만들면 µA 단위 누설이 그대로 남는다.

**드라이버 쪽 코드.**

```c
#include <zephyr/drivers/pinctrl.h>

PINCTRL_DT_INST_DEFINE(0);                     /* 인스턴스의 모든 상태를 정의 */
static const struct pinctrl_dev_config *pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(0);
static int my_uart_init(const struct device *dev)      /* 동작 상태 적용 */
{
    return pinctrl_apply_state(pcfg, PINCTRL_STATE_DEFAULT);
}
static int my_uart_suspend(void)                       /* 저전력 진입 시 */
{
    return pinctrl_apply_state(pcfg, PINCTRL_STATE_SLEEP);
}
```

노드 기준 매크로는 `PINCTRL_DT_DEFINE(node_id)` / `PINCTRL_DT_DEV_CONFIG_GET(node_id)`다.

---

## 8. 보드 리비전

리비전은 `board.yml`에 선언한다.

```yaml
board:
  name: nrf9160dk
  full_name: nRF9160 DK
  vendor: nordic
  socs:
  - name: nrf9160
    variants:
    - name: 'ns'
  - name: nrf52840
  revision:
    format: major.minor.patch
    default: "0.14.0"
    revisions: [{ name: "0.7.0" }, { name: "0.14.0" }]
```

`format`은 `major.minor.patch`, `letter`, `number`, `custom` 중 하나다. 기본은 **fuzzy matching**으로, 요청한 리비전보다 크지 않은 가장 가까운 리비전이 선택된다. `exact: true`로 두면 목록에 있는 리비전만 허용한다. `custom`은 `revision.cmake`에서 `ACTIVE_BOARD_REVISION`을 직접 계산한다.

리비전별 파일 이름은 이렇게 된다.

| 파일 | 예 |
|---|---|
| `<board>_<qual>_<rev>_defconfig` / `.overlay` / `.yaml` | `nrf9160dk_nrf9160_0_14_0.overlay`, `nrf9160dk_nrf9160_0_7_0.yaml` |

리비전 문자열의 `.`은 파일 이름에서 `_`가 된다(`0.14.0` → `0_14_0`). 실제 `boards/nordic/nrf9160dk/`가 이 규칙 그대로다.

```sh
west build -b nrf9160dk@0.7.0/nrf9160 samples/hello_world
west build -b nrf9160dk/nrf9160 samples/hello_world     # default 리비전(0.14.0)
```

Zephyr의 리비전은 **빌드 타임 분기**라 "잘못된 이미지를 잘못된 보드에 플래시"하는 사고를 막아 주지 않는다(B01 8절). 실무에서는 **런타임 보드 ID 확인**(strap/ADC)을 한 겹 더 얹고 불일치 시 부팅 로그에서 크게 경고한다. 면접에서 이 조합을 말하면 "Zephyr 문법만 외운 사람"과 구분된다.

---

## 9. Snippets

여러 보드·여러 앱에 공통으로 얹는 설정 묶음이다. "콘솔을 USB CDC-ACM으로 바꾼다", "디버그 옵션 세트를 켠다" 같은 것이 전형적인 용도다. 보드별·리비전별 분기도 된다.

```yaml
name: foo
append:                                   # 모든 보드에 공통
  EXTRA_DTC_OVERLAY_FILE: foo.overlay
  EXTRA_CONF_FILE: foo.conf
boards:
  bar:                                    # 특정 보드에만 추가
    append: { EXTRA_DTC_OVERLAY_FILE: bar.overlay }
    revisions:
      "0.7.0":
        append: { EXTRA_DTC_OVERLAY_FILE: extra_0_7_0.overlay }
```
```sh
west build -b nrf52840dk/nrf52840 -S <snippet-name> app
west build -b nrf52840dk/nrf52840 app -- -DSNIPPET="snippet1;snippet2"
```

BSP 오너 입장에서 snippet이 유용한 지점은 **"공장 테스트 빌드", "전류 측정용 최소 빌드", "RTT 콘솔 빌드"**처럼 보드 정의는 그대로 두고 설정만 갈아 끼우는 경우다.

---

## 10. west 워크플로

### 10.1 환경 만들기 (공식 getting started 기준)

```sh
python3 -m venv ~/zephyrproject/.venv && source ~/zephyrproject/.venv/bin/activate && pip install west
west init -m https://github.com/zephyrproject-rtos/zephyr ~/zephyrproject
cd ~/zephyrproject && west update && west zephyr-export && west packages pip --install
cd ~/zephyrproject/zephyr && west sdk install
```

터미널을 새로 열 때마다 venv를 다시 활성화해야 한다. `west packages pip --install`과 `west sdk install`은 비교적 최근에 정리된 명령이고, 구버전에서는 `pip install -r scripts/requirements.txt` + SDK 수동 설치다(버전마다 다름).

### 10.2 빌드·플래시·디버그

```sh
west build -p auto -b nrf52840dk/nrf52840 samples/basic/blinky
west build -p always -b nrf52840dk/nrf52840 app     # 항상 pristine
west build -d build_dvt -b nrf9160dk@0.14.0/nrf9160 app
west build -t menuconfig        # 설정 UI (guiconfig는 GUI 버전)
west build -t ram_report        # RAM/ROM 사용량을 심볼 단위로
west build -t rom_report
west build -t pristine
west flash                      # board.cmake의 기본 러너
west flash --runner jlink
west flash -H                   # 러너별 옵션 보기
west debug                      # GDB 붙이기 (attach: 리셋 없이, debugserver: 외부 GDB용)
```

**테스트.**

```sh
west twister -p nrf52840dk/nrf52840 -T tests/drivers/gpio --device-testing  # 실보드 실행
west twister -p nrf52840dk/nrf52840 -T samples --build-only   # CI: 보드 x 리비전 빌드 검증
```

새 보드를 올렸을 때 "twister가 통과하는가"가 사실상의 인수 조건이다(B01 9.3절).

---

## 11. 보드가 안 켜질 때 — 좁혀 가는 순서

```
 부팅 로그가 아예 없다
   ├─► 디버거가 붙나? ─ 아니오 ─► 전원/클럭/SWD 문제 (B01 9.1절, 펌웨어 이전)
   ├─► main에 breakpoint가 걸리나? ─ 아니오 ─► 링커/벡터/부트로더 슬롯 문제
   │                                          (zephyr.map, VTOR, 파티션 확인)
   ├─► 콘솔 설정: zephyr.dts의 chosen { zephyr,console }가 실제 UART인가 /
   │   .config에 CONFIG_SERIAL·CONSOLE·UART_CONSOLE 가 y인가 /
   │   pinctrl의 TX 핀이 회로도의 그 핀인가 / 보레이트가 터미널과 같은가
   └─► 그래도 없으면 GPIO 토글로 "코드가 실행됨"을 먼저 증명한다

 로그는 나오는데 디바이스가 없다 (device_is_ready() == false)
   ├─► zephyr.dts에 노드가 있나?          없으면 오버레이가 적용 안 된 것
   ├─► status = "okay" 인가?              disabled면 드라이버가 안 만들어진다
   ├─► binding을 찾았나?                  devicetree_generated.h 앞부분 목록 확인
   ├─► Kconfig로 드라이버가 켜졌나?       grep CONFIG_<DRV> build/zephyr/.config
   └─► init이 실패했나?                   init 함수 반환값, 전원 레일·버스 확인
```

### 11.1 흔한 실수와 증상

| 실수 | 증상 | 고치는 법 |
|---|---|---|
| devicetree만 고치고 Kconfig를 안 켬 | 노드는 있는데 `device_is_ready()` false | `CONFIG_I2C=y` 등 서브시스템·드라이버 심볼 확인 |
| binding YAML 없음 또는 `compatible` 오타 | 노드가 조용히 무시됨 | `devicetree_generated.h` 앞부분의 binding 목록 확인 |
| `status = "disabled"` 그대로 | 컴파일은 되는데 디바이스가 없다 | 오버레이에서 `status = "okay"` |
| 오버레이 파일 이름·경로가 규칙과 다름 | 아무 일도 안 일어남 | 3.5절 검색 순서대로 두거나 `-DEXTRA_DTC_OVERLAY_FILE` |
| `.config`를 직접 편집 | 다음 빌드에서 사라짐 | `prj.conf` 또는 보드 defconfig로 옮긴다 |
| init 레벨이 너무 이름, 보드 이름에 qualifier 누락 | `PRE_KERNEL`에서 커널 API 호출 → 폴트 / `-b nrf52840dk` 실패 | 레벨을 올린다 / `nrf52840dk/nrf52840`처럼 SoC까지 쓴다 |
| pinctrl에 sleep state 없음 | sleep 전류가 스펙보다 높음 | `pinctrl-1` + `pinctrl-names`에 `"sleep"` 추가 |

---

## 12. 면접에서 이렇게 말한다

**Q. What files do you need to add a new board to Zephyr?**

**A.** 최소 셋이다. `board.yml`(보드 이름, 벤더, 어떤 SoC와 variant가 있는지), `Kconfig.<board>`(Kconfig 트리에서 SoC를 select), 그리고 `<board>_<qualifiers>.dts`(하드웨어 기술). 실무에서는 여기에 `<board>_<qual>_defconfig`(콘솔·GPIO처럼 항상 켜야 하는 것), `Kconfig.defconfig`(기본값 조정), `board.cmake`(flash/debug 러너), `-pinctrl.dtsi`, twister용 `.yaml`이 붙는다. 보드 이름은 `nrf52840dk/nrf52840`처럼 SoC qualifier를 포함한다.

> At minimum three: board.yml with the board's name, vendor and its SoCs and variants; Kconfig.<board>, which just selects the SoC; and the devicetree source, <board>_<qualifiers>.dts. In practice you also add a defconfig for things the board always needs like console and GPIO, Kconfig.defconfig for board-specific defaults, board.cmake for the flash and debug runners, a pinctrl dtsi, and a twister yaml so CI knows what this board supports.

**Q. What goes in devicetree, what goes in Kconfig?**

**A.** devicetree는 "하드웨어가 무엇이고 어떻게 배선됐나" — 어느 버스에 어느 칩이 어떤 주소로 붙었는지, 어느 핀을 쓰는지, 초기 보레이트 같은 것. Kconfig는 "소프트웨어를 어떻게 빌드할 것인가" — 어떤 드라이버와 서브시스템을 포함할지, 로그 레벨, 힙·스택 크기. 판별 기준은 **하드웨어를 바꿔야 달라지는 값이면 devicetree**다. 둘은 독립이 아니라 AND라서, dts에 노드를 켜도 Kconfig로 드라이버를 안 켜면 디바이스가 안 생긴다 — 입문자가 가장 많이 걸리는 함정이다.

> Devicetree describes the hardware: which chip is on which bus at which address, which pins, initial speeds. Kconfig decides what software gets built: which drivers and subsystems are in, log levels, heap and stack sizes. My rule of thumb is that anything that changes when you change the PCB belongs in devicetree. And they're an AND, not an OR — enabling a node in the dts without enabling the driver in Kconfig gives you a node with no device, which is the classic beginner failure.

**Q. A driver's `device_is_ready()` returns false. How do you debug it?**

**A.** 네 가지를 순서대로 본다. ① `zephyr.dts`에 노드가 있는가(없으면 오버레이가 적용 안 된 것) ② `status`가 `okay`인가 ③ binding을 찾았는가(`devicetree_generated.h` 앞부분 목록 — compatible 오타가 여기서 드러난다) ④ 드라이버 Kconfig가 켜졌는가, init이 0을 반환했는가. ④까지 왔으면 소프트웨어가 아니라 전원 레일이나 버스 문제다.

> Four things in order. Is the node in build/zephyr/zephyr.dts at all — if not, my overlay never got applied. Is its status okay. Did it match a binding — the generated devicetree header lists node paths and their bindings at the top, so a typo'd compatible shows up right there. And is the driver enabled in .config, and did its init function return non-zero. If I get past all four, it's not software any more — it's a rail or a bus, and I go to the scope.

**Q. How do you support three hardware revisions?**

**A.** `board.yml`의 `revision:`으로 선언하고 `<board>_<qual>_<rev>.overlay` / `..._defconfig`로 차이를 표현한 뒤 `-b nrf9160dk@0.7.0/nrf9160`으로 빌드한다. 기본은 fuzzy matching이고 `exact: true`로 막을 수 있다. 다만 전부 빌드 타임이라 잘못된 이미지를 잘못된 보드에 굽는 걸 막지 못한다. 그래서 런타임 보드 ID를 strap/ADC로 읽어 부팅 배너에 찍고 불일치 시 크게 경고하는 걸 같이 넣는다.

> Zephyr declares revisions in board.yml and expresses the differences in `<board>_<rev>.overlay` and `<board>_<rev>_defconfig`, and you build with `-b board@0.7.0/soc`. Matching is fuzzy by default and you can force exact. But all of that is build time, so it doesn't stop anyone flashing the wrong image onto the wrong board. I pair it with a runtime board ID from straps or a resistor divider, printed in the boot banner, so a mismatch is loud.

**Q. You've never ported a Zephyr board. Why should we trust you with our BSP?** (정직 스크립트)

**A.** 맞다, Zephyr 보드 디렉터리를 처음부터 써 본 적은 없다. 지금 nRF52840 DK로 devicetree, Kconfig, pinctrl, west를 직접 돌려 보고 있고, 구조는 이 노트에 정리한 수준으로 안다. 내가 가져오는 건 그 위가 아니라 **아래**다 — 새 실리콘이 처음 깨어나지 않을 때 전원·클럭·버스를 스코프와 LA로 좁혀 들어가고, FPGA pre-silicon에서 Cortex-R/M 코어를 부팅시키고, 공장에서 그걸 판정 가능하게 만드는 일. Zephyr의 문법은 몇 주면 익히지만, 그 디버깅 감각은 몇 년이 걸린다.

> Honestly, no — I haven't authored a Zephyr board directory. I'm working through devicetree, Kconfig, pinctrl and west on an nRF52840 DK right now, and I know the structure well enough to explain where each file fits. What I bring is the layer underneath: when new silicon doesn't wake up, I'm the person narrowing it down on a scope and a logic analyzer, and I've brought up Cortex-R and Cortex-M cores on FPGA before silicon existed. The Zephyr syntax I can learn in weeks. That debugging instinct took years.

---

## 13. 직접 해보기 — out-of-tree 보드 만들기

in-tree에 보드를 넣지 않고도 연습할 수 있다. `BOARD_ROOT`를 쓰면 회사 저장소에 보드를 두는 실제 방식과 같아진다.

```sh
# 1~2) 보드 디렉터리를 만들고 기존 보드를 복사해 시작한다. 그다음 board.yml의
#      name→plank·vendor→acme, 파일명 nrf52840dk_nrf52840*→plank_nrf52840*,
#      Kconfig.nrf52840dk→Kconfig.plank, BOARD_NRF52840DK→BOARD_PLANK 로 수정
mkdir -p ~/myboards/boards/acme/plank
cp -r ~/zephyrproject/zephyr/boards/nordic/nrf52840dk/* ~/myboards/boards/acme/plank/
# 3) 빌드 (BOARD_ROOT는 boards/ 의 부모 디렉터리를 가리킨다)
cd ~/zephyrproject/zephyr
west build -p always -b plank/nrf52840 samples/basic/blinky -- -DBOARD_ROOT=~/myboards
# 4) 확인
west boards | grep plank && grep -n 'model =' build/zephyr/zephyr.dts
west flash            # DK에 그대로 구워진다 (하드웨어가 같으니)
```

이어서 해 볼 것 넷. ① LED 핀을 옮기고 `led0` alias만으로 blinky가 따라오는지 확인한다. ② `board.yml`에 `revision:`을 추가하고 `plank_nrf52840_0_2_0.overlay`를 만들어 `-b plank@0.2.0/nrf52840`으로 빌드한다. ③ `uart0`의 pinctrl에 `sleep` state를 추가한다. ④ 가짜 센서 노드와 binding YAML을 만들고 `DT_INST_FOREACH_STATUS_OKAY`로 인스턴스가 생기는지 `devicetree_generated.h`에서 확인한다.

---

## 14. 요약 & 체크리스트

Zephyr 보드 포팅은 결국 **"보드에 대한 사실을 세 종류의 파일에 나눠 적는 일"**이다. 하드웨어 사실은 devicetree, 빌드 구성은 Kconfig, 메타데이터와 도구 설정은 `board.yml`·`board.cmake`·twister yaml. 이 셋을 정확히 나눠 쓰면 앱 코드는 보드를 몰라도 되고, 리비전이 늘어도 overlay 한 장이면 된다. 반대로 셋을 섞기 시작하면 보드가 늘어날 때마다 코드가 갈라진다. 디버깅의 출발점은 항상 빌드 산출물 셋이다 — `zephyr.dts`, `.config`, `devicetree_generated.h`.

- [ ] 보드 타깃 문법 `name[@rev]/soc[/cluster][/variant]`를 예와 함께 말할 수 있다
- [ ] 필수 파일 3개와 선택 파일 5개 이상을 역할과 함께 말하고, `<board>_defconfig`와 `Kconfig.defconfig`의 차이를 설명할 수 있다
- [ ] devicetree / Kconfig / C 코드의 경계를 예로 판별하고, `chosen`·`aliases`가 왜 BSP의 핵심인지 설명할 수 있다
- [ ] 오버레이 자동 검색 순서와 `EXTRA_DTC_OVERLAY_FILE`을 쓸 수 있다
- [ ] `DT_DRV_COMPAT` + `DT_INST_FOREACH_STATUS_OKAY` 패턴을 화이트보드에 쓸 수 있다
- [ ] 초기화 레벨 6종·우선순위 범위와 `PRE_KERNEL`의 제약, pinctrl의 default/sleep state와 저전력의 관계를 설명할 수 있다
- [ ] `device_is_ready()`가 false일 때 확인 순서 4단계를 말하고, DK 하나로 out-of-tree 보드를 만들어 빌드·플래시까지 해 봤다

---

## 참고 자료

| 자료 | 확인한 내용 (2026-09-21 접속) | URL |
|---|---|---|
| Board Porting Guide | 필수 파일 3종, board.yml 스키마, 보드 qualifier 정규화 규칙, revision format과 파일명 | https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html |
| Devicetree — 문법과 구조 | 노드·라벨·phandle, `reg`/`status`/`compatible`, `/aliases`·`/chosen` | https://docs.zephyrproject.org/latest/build/dts/intro-syntax-structure.html |
| Devicetree — HOWTOs · bindings 문법 | `zephyr.dts`·`devicetree_generated.h` 경로, 오버레이 자동 검색 순서, `DTC_OVERLAY_FILE`/`EXTRA_DTC_OVERLAY_FILE` · `compatible`, `include`, `properties`의 type/required/default/enum, `bus`/`on-bus`, child-binding | https://docs.zephyrproject.org/latest/build/dts/howtos.html · https://docs.zephyrproject.org/latest/build/dts/bindings-syntax.html |
| Kconfig — 설정하는 법 | 보드 defconfig → CMake 캐시 → 앱 설정 순서, 앱 설정 우선, `EXTRA_CONF_FILE`, `.config` 직접 수정 금지 | https://docs.zephyrproject.org/latest/build/kconfig/setting.html |
| Pin Control | `pinctrl-N`/`pinctrl-names`, default·sleep state, `PINCTRL_DT_(INST_)DEFINE`, `PINCTRL_DT_(INST_)DEV_CONFIG_GET`, `pinctrl_apply_state` | https://docs.zephyrproject.org/latest/hardware/pinctrl/index.html |
| Device Driver Model + Device Model API (doxygen) | 초기화 레벨의 의미와 init 반환값 규약(0 / errno) · `DEVICE_DT_DEFINE(node_id, init_fn, pm, data, config, level, prio, api, ...)`, `DEVICE_DT_INST_DEFINE`, `DEVICE_DT_GET`, `device_is_ready`, `device_get_binding`, `device_init` | https://docs.zephyrproject.org/latest/kernel/drivers/index.html · https://docs.zephyrproject.org/latest/doxygen/html/group__device__model.html |
| System init API (doxygen) | `SYS_INIT(init_fn, level, prio)`, 레벨 6종(EARLY/PRE_KERNEL_1/PRE_KERNEL_2/POST_KERNEL/APPLICATION/SMP), prio 0~999 | https://docs.zephyrproject.org/latest/doxygen/html/group__sys__init.html |
| Snippets — 작성법 | `snippet.yml`의 `name`/`append`/`boards`/`revisions`, `EXTRA_DTC_OVERLAY_FILE`·`EXTRA_CONF_FILE` | https://docs.zephyrproject.org/latest/build/snippets/writing.html |
| west build/flash/debug + Getting Started | `-p auto/always`, `-d`, `-t menuconfig/ram_report/rom_report`, `-- -DCONF_FILE`/`-DEXTRA_CONF_FILE`/`-DSNIPPET`, `flash --runner`, `-H`, `debug`/`attach` · venv + `west init -m … ~/zephyrproject`, `west update`, `west zephyr-export`, `west packages pip --install`, `west sdk install` | https://docs.zephyrproject.org/latest/develop/west/build-flash-debug.html · https://docs.zephyrproject.org/latest/develop/getting_started/index.html |
| 실제 보드 트리 (zephyr `main`) | `boards/nordic/nrf52840dk/`의 파일 목록과 board.yml·Kconfig·defconfig·board.cmake·pinctrl dtsi 내용, `boards/nordic/nrf9160dk/`의 리비전 선언과 리비전별 파일명 | https://github.com/zephyrproject-rtos/zephyr/tree/main/boards/nordic |
| 실제 드라이버 예 | `DT_DRV_COMPAT` + 인스턴스 매크로 + `DT_INST_FOREACH_STATUS_OKAY` 패턴 | https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/sensor/bosch/bme280/bme280.c |

> **확인하지 못해 버전 의존으로 남겨 둔 것**: `west twister`의 정확한 옵션 조합은 버전마다 바뀌므로 `west twister --help`로 확인할 것. `DEVICE_API()` 매크로와 `SENSOR_DEVICE_DT_INST_DEFINE` 같은 서브시스템 전용 래퍼는 비교적 최근에 정리된 것이라 구버전에서는 `static const struct sensor_driver_api ...` 형태를 쓴다. Nordic 전용 `NRF_PSEL`/`psels` 문법은 다른 벤더에서는 `pinmux`로 다르게 쓴다. 벤더 SDK(Qualcomm, Ambiq)의 Zephyr 지원 범위는 NDA·포털 배포라 공개 문서로 확인할 수 없었다.
