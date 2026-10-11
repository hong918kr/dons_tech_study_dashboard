# D3. 드라이버 매칭 — compatible에서 probe()까지

> **이 노트를 다 읽으면**: DT 노드가 `platform_device`가 되고 `of_match_table`과 만나 `probe()`가 불리기까지의 함수 경로를 소스 이름 그대로 말할 수 있다 · fallback compatible 목록에서 어느 항목이 왜 이기는지(점수 규칙) 설명할 수 있다 · sysfs의 `driver`/`of_node`/`modalias`/`driver_override`/`bind`/`unbind`로 결합 상태를 보고 바꿀 수 있다 · "센서가 안 보인다"를 노드 없음 / device 없음 / 매칭 실패 / probe 실패 / defer 다섯 갈래로 나눠 진단할 수 있다
> **왜 필요한가**: 드라이버 면접의 거의 모든 질문은 "How does a driver get bound to a device?"로 시작하거나 끝나고, 실무 첫 달에는 "DT에 센서를 넣었는데 probe가 안 불린다"를 반드시 만난다
> **Don 기준 난이도**: 레지스터 주소·버스·초기화 순서는 펌웨어에서 이미 한다 / 새로 배울 부분은 "누가 init 함수를 부르나"를 커널이 문자열 매칭으로 정한다는 점, 그리고 그 결과가 sysfs 링크로 다 보인다는 점
> **선행 노트**: D1(왜 DT인가), D2(DT 문법), E3(커널 모듈) — G2(디바이스 모델)·G3(platform driver)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don의 SSD 펌웨어에서 NAND 컨트롤러를 초기화하는 장면을 떠올려 보자. `main()`이 `nand_init(NAND_BASE)`를 부른다. **누가 어떤 초기화 함수를 어떤 주소로 부를지는 소스에 하드코딩**되어 있다. 보드가 바뀌면 `#ifdef BOARD_REV_B`가 늘어난다.

Linux는 이 결정을 **런타임 매칭**으로 바꿨다.

- 하드웨어 쪽: "이 주소에 이런 칩이 있다"는 사실은 Device Tree에 **데이터**로 적는다 (`compatible = "don,imu-v2"`, `reg = <...>`).
- 소프트웨어 쪽: 드라이버는 "나는 이런 칩을 다룰 수 있다"는 **문자열 표**(`of_match_table`)만 내놓는다.
- 커널(driver core)이 둘을 짝지어서, 짝이 맞으면 드라이버의 `probe()`를 **그 장치 하나에 대해** 부른다. 장치가 3개면 `probe()`는 3번 불린다.

펌웨어 비유로 말하면, `probe()`는 "커널이 대신 불러 주는 `nand_init()`"이고, 매칭은 "부트 때 칩 ID 레지스터를 읽어서 함수 포인터 테이블에서 init 함수를 고르는 코드"를 커널이 일반화해 둔 것이다. 다만 SoC 내부 IP는 ID 레지스터로 스스로를 알리지 못하므로(non-discoverable, D1), **ID 역할을 DT의 `compatible` 문자열이 대신한다.**

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="22" text-anchor="middle" font-size="13" font-weight="bold">장치 쪽 — Device Tree</text>
  <text x="510" y="22" text-anchor="middle" font-size="13" font-weight="bold">드라이버 쪽 — 커널 모듈</text>
  <rect x="20" y="34" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="56" text-anchor="middle" font-size="12">board.dts — 사람이 쓰는 소스</text>
  <rect x="20" y="82" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="104" text-anchor="middle" font-size="12">dtc → board.dtb (FDT 바이너리)</text>
  <rect x="20" y="130" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="152" text-anchor="middle" font-size="12">U-Boot가 DTB 주소를 x0로 넘김</text>
  <rect x="20" y="178" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="200" text-anchor="middle" font-size="12">unflatten_device_tree() → device_node</text>
  <rect x="20" y="226" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="248" text-anchor="middle" font-size="12">of_platform_default_populate_init()</text>
  <rect x="20" y="274" width="300" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="296" text-anchor="middle" font-size="12">platform_device 생성 → device_add()</text>
  <rect x="360" y="34" width="300" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="56" text-anchor="middle" font-size="12">d3_sensor.c — 드라이버 소스</text>
  <rect x="360" y="82" width="300" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="104" text-anchor="middle" font-size="12">of_match_table + MODULE_DEVICE_TABLE</text>
  <rect x="360" y="130" width="300" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="152" text-anchor="middle" font-size="12">make → d3_sensor.ko (alias 내장)</text>
  <rect x="360" y="178" width="300" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="200" text-anchor="middle" font-size="12">insmod 또는 modprobe (uevent)</text>
  <rect x="360" y="226" width="300" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="248" text-anchor="middle" font-size="12">__platform_driver_register()</text>
  <rect x="360" y="274" width="300" height="34" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="296" text-anchor="middle" font-size="12">driver_register() → bus_add_driver()</text>
  <line x1="170" y1="68" x2="170" y2="80" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="116" x2="170" y2="128" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="164" x2="170" y2="176" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="212" x2="170" y2="224" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="260" x2="170" y2="272" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="68" x2="510" y2="80" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="116" x2="510" y2="128" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="164" x2="510" y2="176" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="212" x2="510" y2="224" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="260" x2="510" y2="272" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="308" x2="250" y2="332" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="308" x2="430" y2="332" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="140" y="334" width="400" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="356" text-anchor="middle" font-size="12" font-weight="bold">platform_match() → really_probe() → d3_probe()</text>
</svg>
```
그림 1 — 왼쪽(장치)과 오른쪽(드라이버)은 서로를 모른 채 따로 만들어지고, 버스(platform bus)의 `match`가 둘을 짝지어 `probe()`를 부른다. 어느 쪽이 먼저 와도 된다 (§8).

이 노트는 그림 1의 모든 상자를 **QEMU에서 실제로** 돌려 본다. 순서는 이렇다.

| 절 | 단계 | 실습 |
|---|---|---|
| §1 | 실습 준비: DT 노드 8개 + 드라이버 모듈 | 예제 1–2 |
| §2 | DT 노드 → `platform_device` | 예제 3–4 |
| §3 | 드라이버 등록과 `of_match_table` | 예제 4 |
| §4 | 버스 `match` — 누가 이기나 | 예제 5–6 |
| §5 | `really_probe()` → `probe()` | 예제 7 |
| §6 | sysfs에서 결합 상태 보기 | 예제 8 |
| §7 | 수동 unbind/bind, `driver_override` | 예제 9–10 |
| §8 | 로드 순서, 자식 노드를 만드는 드라이버(I2C/SPI 미리보기) | 예제 11–12 |
| §9 | modalias와 모듈 자동 로딩 | 예제 13 |
| §10 | deferred probe 미리보기 | 예제 14 |

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`) 안의 공용 랩에서 QEMU `virt`(cortex-a57, 512 MB, TCG)로 부팅한 **게스트 커널 `6.18.55`**(BusyBox initramfs)에서 실제로 돌린 결과다. 커널 소스 인용은 랩의 `/var/lab/src/linux-6.18.55`에서 grep으로 확인했다. 타임스탬프·모듈 주소는 실행마다 다르다.

---

## 1. 실습 준비 — DT 노드 8개와 드라이버 하나

### 1.1 무엇을 실험하나

센서 키트 보드를 흉내 낸다. IMU 칩이 세 세대(v1, v2, v3)가 있고, 기압계(baro)가 하나 있다. 이 상황에서 생길 수 있는 경우를 노드 하나씩으로 만든다.

| 노드 | compatible / 속성 | 무엇을 보려고 |
|---|---|---|
| `d3-sensor@9100000` | `"don,imu-v2", "don,imu"` | 가장 구체적인 항목이 이기는가 |
| `d3-sensor@9101000` | `"don,imu"` | 일반(generic) 항목만 있을 때 |
| `d3-sensor@9102000` | `"don,imu-v3", "don,imu"` | 드라이버가 모르는 새 칩 → fallback |
| `d3-sensor@9103000` | `"don,imu-v2"`, `status = "disabled"` | 꺼진 노드는 device가 안 생긴다 |
| `d3-baro@9104000` | `"acme,baro"` | device는 있는데 드라이버가 없다 |
| `d3-sensor@9105000` | `"don,imu-v2"`, `don,needs-supply` | deferred probe (§10) |
| `d3-hub` → 자식 `d3-sensor@0` | 부모 `"don,hub"`, 자식 `"don,imu"` | simple-bus가 아닌 부모의 자식은 자동 생성 안 됨 (§8) |
| `d3-soc@9200000` → 자식 `d3-sensor@100` | 부모 `"simple-bus"` | simple-bus 자식은 자동 생성됨 |

### 1.2 예제 1 — `virt.dts` 복사본에 노드를 넣고 `dtc`로 컴파일

무엇을 확인하나: 랩의 `virt.dts`(QEMU가 만들어 주는 DT를 덤프한 것) 끝에 실습 노드를 붙여 `.dtb`를 만들고, 컴파일 결과에 우리가 쓴 속성이 그대로 들어갔는지 `fdtget`으로 읽는다.

추가한 노드(루트 `/` 바로 아래, 루트는 `#address-cells = <2>`, `#size-cells = <2>`):

```dts
	d3-sensor@9100000 {
		compatible = "don,imu-v2", "don,imu";
		reg = <0x0 0x09100000 0x0 0x1000>;
	};
	d3-sensor@9101000 {
		compatible = "don,imu";
		reg = <0x0 0x09101000 0x0 0x1000>;
	};
	d3-sensor@9102000 {
		compatible = "don,imu-v3", "don,imu";
		reg = <0x0 0x09102000 0x0 0x1000>;
	};
	d3-sensor@9103000 {
		compatible = "don,imu-v2";
		reg = <0x0 0x09103000 0x0 0x1000>;
		status = "disabled";
	};
	d3-baro@9104000 {
		compatible = "acme,baro";
		reg = <0x0 0x09104000 0x0 0x1000>;
	};
	d3-sensor@9105000 {
		compatible = "don,imu-v2";
		reg = <0x0 0x09105000 0x0 0x1000>;
		don,needs-supply;
	};
	d3-hub {
		compatible = "don,hub";
		#address-cells = <1>;
		#size-cells = <0>;
		d3-sensor@0 {
			compatible = "don,imu";
			reg = <0>;
		};
	};
	d3-soc@9200000 {
		compatible = "simple-bus";
		#address-cells = <1>;
		#size-cells = <1>;
		ranges = <0x0 0x0 0x09200000 0x10000>;
		d3-sensor@100 {
			compatible = "don,imu-v2", "don,imu";
			reg = <0x100 0x100>;
		};
	};
```

```sh
W=/var/tmp/D3
head -n -1 /var/lab/out/virt.dts > $W/d3_virt.dts      # 마지막 "};" 빼고 복사
cat ~/.elinux_scratch/D3/d3_nodes.dtsi >> $W/d3_virt.dts
echo "};" >> $W/d3_virt.dts
cd $W && dtc -I dts -O dtb -o d3_virt.dtb d3_virt.dts
fdtget -t s d3_virt.dtb /d3-sensor@9100000 compatible
fdtget d3_virt.dtb /d3-sensor@9103000 status
dtc -I dtb -O dts d3_virt.dtb 2>/dev/null | sed -n "/d3-sensor@9100000 {/,/};/p"
```

```text
d3_virt.dts:276.3-21: Warning (clocks_property): /pl061@9030000:clocks: cell 0 is not a phandle reference
d3_virt.dts:302.3-21: Warning (clocks_property): /pl031@9010000:clocks: cell 0 is not a phandle reference
d3_virt.dts:310.3-28: Warning (clocks_property): /pl011@9000000:clocks: cell 0 is not a phandle reference
d3_virt.dts:310.3-28: Warning (clocks_property): /pl011@9000000:clocks: cell 1 is not a phandle reference
d3_virt.dts:267.4-31: Warning (gpios_property): /gpio-keys/poweroff:gpios: cell 0 is not a phandle reference
don,imu-v2 don,imu
disabled
	d3-sensor@9100000 {
		compatible = "don,imu-v2", "don,imu";
		reg = <0x00 0x9100000 0x00 0x1000>;
	};
```

출력에서 볼 것: 경고 5개는 전부 QEMU가 만든 원래 노드(phandle이 라벨 없이 숫자로 덤프됨) 것이고 `d3-` 노드 경고는 없다. 처음에는 `d3-soc` 이라고 unit address 없이 썼다가 `unit_address_vs_reg` 경고("node has a reg or ranges property, but no unit name")를 받고 `d3-soc@9200000`으로 고쳤다 — `ranges`가 있는 버스 노드도 이름에 `@주소`가 필요하다(D2). `compatible`은 문자열 **목록**이고, dtb 안에서는 NUL로 구분된 문자열들이 이어져 있다. `fdtget -t s`가 그걸 공백으로 풀어 보여 준다.

> 함정: README에 적혀 있듯 `virt.dts`는 `-m 512M -cpu cortex-a57`로 덤프한 것이다. `-dtb`로 넘길 때는 같은 `-m`/`-cpu`를 써야 한다. 메모리 크기가 다르면 DT의 `memory` 노드와 실제 RAM이 어긋난다.

### 1.3 예제 2 — 드라이버 `d3_sensor` (platform driver, compatible 2개 + match data)

무엇을 확인하나: 최소한의 platform driver가 갖춰야 할 것 — `of_match_table`, `MODULE_DEVICE_TABLE(of, ...)`, `probe()`/`remove()` — 과 **match data**(`.data`)로 칩 세대별 정보를 넘기는 관용구.

```c
// SPDX-License-Identifier: GPL-2.0
/* d3_sensor: DT compatible -> of_match_table -> probe() 를 관찰하는 platform driver */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/property.h>

struct d3_chip_info { const char *model; unsigned int fifo_depth; };
static const struct d3_chip_info d3_imu_v2 = { .model = "IMU v2", .fifo_depth = 1024 };
static const struct d3_chip_info d3_imu    = { .model = "IMU v1 (generic)", .fifo_depth = 64 };

static bool supply_ready = true;	/* false면 don,needs-supply 노드는 defer */
module_param(supply_ready, bool, 0644);

static int d3_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct d3_chip_info *info = of_device_get_match_data(dev);
	struct resource *res;
	const char *first = "?";

	if (!info)
		return -ENODEV;
	if (of_property_read_bool(dev->of_node, "don,needs-supply") && !supply_ready)
		return dev_err_probe(dev, -EPROBE_DEFER, "supply not ready yet\n");

	of_property_read_string(dev->of_node, "compatible", &first);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev_info(dev, "probe: node=%pOFn compatible[0]=\"%s\" -> %s, fifo=%u, reg=%pR\n",
		 dev->of_node, first, info->model, info->fifo_depth, res);
	platform_set_drvdata(pdev, (void *)info);
	return 0;
}

static void d3_remove(struct platform_device *pdev)
{
	const struct d3_chip_info *info = platform_get_drvdata(pdev);
	dev_info(&pdev->dev, "remove: %s\n", info->model);
}

static const struct of_device_id d3_of_match[] = {
#ifndef D3_REVERSE
	{ .compatible = "don,imu-v2", .data = &d3_imu_v2 },
	{ .compatible = "don,imu",    .data = &d3_imu },
#else	/* 실험: 테이블 순서를 뒤집어도 결과가 같은가? */
	{ .compatible = "don,imu",    .data = &d3_imu },
	{ .compatible = "don,imu-v2", .data = &d3_imu_v2 },
#endif
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, d3_of_match);

static struct platform_driver d3_driver = {
	.probe  = d3_probe,
	.remove = d3_remove,
	.driver = { .name = "d3_sensor", .of_match_table = d3_of_match },
};
module_platform_driver(d3_driver);
MODULE_DESCRIPTION("D3 driver matching demo");
MODULE_LICENSE("GPL");
```

```make
obj-m += d3_sensor.o d3_hub.o
```

```sh
cd /var/tmp/D3/mod && make -C /var/lab/out/linux-build M=$PWD modules
modinfo d3_sensor.ko
```

```text
  CC [M]  d3_sensor.o
  CC [M]  d3_hub.o
  MODPOST Module.symvers
  CC [M]  d3_sensor.mod.o
  CC [M]  .module-common.o
  LD [M]  d3_sensor.ko
  CC [M]  d3_hub.mod.o
  LD [M]  d3_hub.ko
filename:       /var/tmp/D3/mod/d3_sensor.ko
license:        GPL
description:    D3 driver matching demo
alias:          of:N*T*Cdon,imuC*
alias:          of:N*T*Cdon,imu
alias:          of:N*T*Cdon,imu-v2C*
alias:          of:N*T*Cdon,imu-v2
depends:        
name:           d3_sensor
vermagic:       6.18.55 SMP preempt mod_unload aarch64
parm:           supply_ready:bool
```

출력에서 볼 것: 경고 0개. `MODULE_DEVICE_TABLE(of, d3_of_match)` 한 줄이 `.ko` 안에 `alias:` 4줄을 만들었다 (§9에서 이 alias가 자동 로딩의 열쇠가 된다). `d3_hub.c`는 §8에서 쓴다. 코드에서 기억할 관용구 네 가지:

- `struct of_device_id`는 `name`, `type`, `compatible`, `data` 필드를 가진다. 현대 드라이버는 거의 `compatible`과 `data`만 쓴다. 마지막 `{ }`(sentinel)가 없으면 매칭 루프가 배열 끝을 넘어 읽는다.
- `.data`에는 **칩별 차이**(레지스터 맵, FIFO 크기, 기능 플래그)를 담은 구조체 포인터를 넣는다. `probe()`에서 `of_device_get_match_data(dev)`로 꺼내면 "어느 compatible로 매칭됐나"를 if-else 문자열 비교 없이 안다. ACPI까지 고려하는 새 코드는 `device_get_match_data()`(`linux/property.h`, D8)를 쓴다.
- `module_platform_driver(x)`는 `module_init`에서 `platform_driver_register(&x)`, `module_exit`에서 `platform_driver_unregister(&x)`를 부르는 매크로다(E3).
- 6.18의 `platform_driver.remove`는 `void`를 돌려준다(예전 커널은 `int`). 오래된 BSP 코드를 옮길 때 컴파일 에러로 바로 보인다.

---

## 2. 1단계 — DT 노드가 `platform_device`가 되기까지

### 2.1 직관 — "명단은 부팅 때 미리 만든다"

드라이버가 하나도 없어도, 커널은 부팅 중에 DT를 훑어서 **"여기 장치가 있다"는 객체(`struct platform_device`)를 먼저 만들어 둔다.** 드라이버는 나중에 와서 그 명단에서 자기 짝을 찾는다. 펌웨어로 치면 부트 코드가 하드웨어 구성표를 RAM에 테이블로 만들어 두고, 각 모듈 init이 그 테이블을 검색하는 구조다.

### 2.2 정의 — 세 단계

1. **unflatten**: 부트로더가 넘긴 FDT(평평한 바이너리 블롭)를 `unflatten_device_tree()`가 `struct device_node`의 트리(부모·자식·형제 포인터, 속성 리스트)로 펼친다. arm64에서는 `setup_arch()`(arch/arm64/kernel/setup.c)가 부른다. 이 트리가 `/proc/device-tree` = `/sys/firmware/devicetree/base`로 보인다.
2. **populate**: `of_platform_default_populate_init()`가 `arch_initcall_sync` 레벨(C6)에서 돈다. 이 함수는 결국 `of_platform_default_populate(NULL, ...)` → `of_platform_populate(root, match_table, ...)`를 부르고, 루트의 각 자식에 대해 `of_platform_bus_create(child, ..., strict=true)`를 재귀로 부른다.
3. **device 생성**: `of_platform_device_create_pdata()`가 `of_device_alloc()`으로 `platform_device`를 만들고(`reg`→`struct resource`, `interrupts`→IRQ 리소스) `of_device_add()` → `device_add()`로 등록한다.

`of_platform_bus_create()`의 규칙을 랩 소스에서 그대로 옮기면:

| 조건 | 결과 | 소스 근거 |
|---|---|---|
| `compatible` 속성이 없다 | 건너뜀 (device 없음) | `strict && !of_property_present(bus, "compatible")` |
| `status`가 `"okay"`/`"ok"`가 아니다(속성이 없으면 okay) | device 없음 | `of_platform_device_create_pdata()`의 `of_device_is_available()` |
| 이미 만든 노드 | 건너뜀 | `OF_POPULATED` 플래그 |
| `"arm,primecell"` 호환 | `amba_device`로 만든다 (platform 아님) | `of_amba_device_create()` |
| 노드가 `simple-bus`/`simple-mfd`/`isa`/`arm,amba-bus` | device를 만들고 **자식도 재귀** | `of_platform_default_populate()`의 `match_table` |
| 그 밖 | 그 노드만 device, **자식은 안 내려감** | 버스 노드가 `matches`에 없으면 재귀 전에 `return 0` |

핵심 결론 두 가지:

- **루트 직속 노드**와 **simple-bus 계열 노드의 자식**만 자동으로 platform device가 된다.
- **I2C 컨트롤러 밑의 센서 노드**는 컨트롤러 노드가 simple-bus가 아니므로 여기서 만들어지지 않는다. 컨트롤러 드라이버가 probe될 때 **I2C 코어**가 자식들을 `i2c_client`로 만든다 (§8, H5).

### 2.3 예제 3 — 부팅 중 populate를 직접 보기 (`initcall_debug` + dyndbg)

무엇을 확인하나: `of_platform_default_populate_init()`이 initcall로 불리는 순간, 그 안에서 우리 노드들이 어떤 순서로 처리되는지. `drivers/of/platform.c`의 `pr_debug`를 커널 인자로 켠다.

게스트 쪽 스크립트(`/t.sh`로 initramfs에 넣고 `LAB_CMD=sh~/t.sh`로 실행):

```sh
dmesg | sed -n '/calling  of_platform_default_populate_init/,/initcall of_platform_default_populate_init/p' | grep -E "populate_init|/d3"
echo "== probe of simple-bus node"; dmesg | grep "probe of d3-soc"
```

VM 쪽 실행(초기 램디스크 묶기 + QEMU 부팅; 이후 모든 예제가 같은 방식이다):

```sh
W=/var/tmp/D3
cp ~/.elinux_scratch/D3/t_populate.sh $W/rootfs/t.sh
(cd $W/rootfs && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd $W/i.gz -dtb $W/d3_virt.dtb \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/t.sh initcall_debug platform.dyndbg=+p log_buf_len=4M" </dev/null
```

```text
lab initramfs: 6.18.55 up 1.94s
[    0.366958] calling  of_platform_default_populate_init+0x0/0x114 @ 1
[    0.461878] OF: create platform device: /d3-sensor@9100000
[    0.462733] OF: create platform device: /d3-sensor@9101000
[    0.463816] OF: create platform device: /d3-sensor@9102000
[    0.465046] OF: create platform device: /d3-sensor@9103000
[    0.465191] OF: create platform device: /d3-baro@9104000
[    0.466602] OF: create platform device: /d3-sensor@9105000
[    0.468168] OF: create platform device: /d3-hub
[    0.469915] OF: create platform device: /d3-soc@9200000
[    0.471450] OF:    create child: /d3-soc@9200000/d3-sensor@100
[    0.471566] OF: create platform device: /d3-soc@9200000/d3-sensor@100
[    0.473713] initcall of_platform_default_populate_init+0x0/0x114 returned 0 after 104000 usecs
== probe of simple-bus node
[    0.884983] probe of d3-soc@9200000 returned 0 after 241 usecs
```

출력에서 볼 것:

- `create child:`는 `d3-soc@9200000`(simple-bus)에서만 나온다. `d3-hub` 밑으로는 내려가지 않는다.
- `/d3-sensor@9103000`(disabled)에도 "create platform device" 줄이 찍힌다. 소스를 보면 `pr_debug`가 `of_device_is_available()` 검사 **앞**에 있어서다. 이 로그만 보고 "만들어졌다"고 판단하면 틀린다 — 예제 4에서 실제로는 없다는 걸 확인한다.
- `platform.dyndbg=+p`: 빌트인 코드의 dyndbg를 "모듈 이름" 단위로 켜는 커널 인자다. `drivers/of/platform.c`와 `drivers/base/platform.c` 둘 다 KBUILD_MODNAME이 `platform`이라 함께 켜진다. 로그가 많아 `log_buf_len=4M`으로 버퍼를 키웠다(처음엔 기본 버퍼가 넘쳐서 앞부분이 잘렸다).
- simple-bus 노드 자체도 platform device이고, `simple-pm-bus` 드라이버(drivers/bus/simple-pm-bus.c, `{ .compatible = "simple-bus", .data = ONLY_BUS }`)에 묶인다.

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="24" font-size="12" font-weight="bold">DT 노드</text>
  <text x="240" y="24" font-size="12" font-weight="bold">compatible / 속성</text>
  <text x="465" y="24" font-size="12" font-weight="bold">결과 (부팅 + insmod 후)</text>
  <line x1="10" y1="32" x2="670" y2="32" stroke="currentColor" stroke-opacity="0.4"/>
  <text x="20" y="57" font-size="12">/d3-sensor@9100000</text>
  <text x="240" y="57" font-size="12">"don,imu-v2", "don,imu"</text>
  <rect x="465" y="40" width="205" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="475" y="57" font-size="12">bound — IMU v2</text>
  <text x="20" y="89" font-size="12">/d3-sensor@9101000</text>
  <text x="240" y="89" font-size="12">"don,imu"</text>
  <rect x="465" y="72" width="205" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="475" y="89" font-size="12">bound — IMU v1</text>
  <text x="20" y="121" font-size="12">/d3-sensor@9102000</text>
  <text x="240" y="121" font-size="12">"don,imu-v3", "don,imu"</text>
  <rect x="465" y="104" width="205" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="475" y="121" font-size="12">bound — fallback으로 v1</text>
  <text x="20" y="153" font-size="12">/d3-sensor@9103000</text>
  <text x="240" y="153" font-size="12">status = "disabled"</text>
  <rect x="465" y="136" width="205" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="475" y="153" font-size="12">device 없음</text>
  <text x="20" y="185" font-size="12">/d3-baro@9104000</text>
  <text x="240" y="185" font-size="12">"acme,baro"</text>
  <rect x="465" y="168" width="205" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="475" y="185" font-size="12">device만, 드라이버 없음</text>
  <text x="20" y="217" font-size="12">/d3-sensor@9105000</text>
  <text x="240" y="217" font-size="12">"don,imu-v2", needs-supply</text>
  <rect x="465" y="200" width="205" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="475" y="217" font-size="12">defer 가능 (§10)</text>
  <text x="20" y="249" font-size="12">/d3-hub</text>
  <text x="240" y="249" font-size="12">"don,hub" (simple-bus 아님)</text>
  <rect x="465" y="232" width="205" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="475" y="249" font-size="12">device만 (d3_hub 대기)</text>
  <text x="36" y="281" font-size="12">↳ d3-sensor@0</text>
  <text x="240" y="281" font-size="12">"don,imu"</text>
  <rect x="465" y="264" width="205" height="26" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="475" y="281" font-size="12">hub probe 전엔 없음</text>
  <text x="20" y="313" font-size="12">/d3-soc@9200000</text>
  <text x="240" y="313" font-size="12">"simple-bus"</text>
  <rect x="465" y="296" width="205" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="475" y="313" font-size="12">bus device, 자식 생성</text>
  <text x="36" y="345" font-size="12">↳ d3-sensor@100</text>
  <text x="240" y="345" font-size="12">"don,imu-v2", "don,imu"</text>
  <rect x="465" y="328" width="205" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="475" y="345" font-size="12">9200100.d3-sensor bound</text>
</svg>
```
그림 2 — 실습 DT 노드 10개의 운명. 초록 = 드라이버에 묶임, 주황 = device는 있으나 짝 없음, 회색 = 버스·꺼짐, 빨강 = 부모 드라이버가 만들어 줘야 생김. 예제 3·4·11·14의 실제 결과를 모은 것이다.

### 2.4 예제 4 — 드라이버 없이 생긴 device 목록, 그리고 insmod

무엇을 확인하나: 드라이버를 올리기 **전에** 이미 `/sys/bus/platform/devices`에 장치가 있는지, `insmod` 한 번에 `probe()`가 장치 수만큼 불리는지.

```sh
echo "== uname"; uname -r
echo "== /sys/bus/platform/devices (d3/hub/baro)"; ls /sys/bus/platform/devices | grep -E "d3|hub|baro"
echo "== insmod"; insmod /d3_sensor.ko
dmesg | grep d3_sensor
```

```text
lab initramfs: 6.18.55 up 2.27s
== uname
6.18.55
== /sys/bus/platform/devices (d3/hub/baro)
9100000.d3-sensor
9101000.d3-sensor
9102000.d3-sensor
9104000.d3-baro
9105000.d3-sensor
9200100.d3-sensor
d3-hub
d3-soc@9200000
== insmod
[    2.657403] d3_sensor: loading out-of-tree module taints kernel.
[    2.665199] d3_sensor 9100000.d3-sensor: probe: node=d3-sensor compatible[0]="don,imu-v2" -> IMU v2, fifo=1024, reg=[mem 0x09100000-0x09100fff]
[    2.666275] d3_sensor 9101000.d3-sensor: probe: node=d3-sensor compatible[0]="don,imu" -> IMU v1 (generic), fifo=64, reg=[mem 0x09101000-0x09101fff]
[    2.666724] d3_sensor 9102000.d3-sensor: probe: node=d3-sensor compatible[0]="don,imu-v3" -> IMU v1 (generic), fifo=64, reg=[mem 0x09102000-0x09102fff]
[    2.668051] d3_sensor 9105000.d3-sensor: probe: node=d3-sensor compatible[0]="don,imu-v2" -> IMU v2, fifo=1024, reg=[mem 0x09105000-0x09105fff]
[    2.668442] d3_sensor 9200100.d3-sensor: probe: node=d3-sensor compatible[0]="don,imu-v2" -> IMU v2, fifo=1024, reg=[mem 0x09200100-0x092001ff]
```

출력에서 볼 것:

- `9103000`(disabled)과 `d3-hub/d3-sensor@0`은 목록에 **없다.** `9104000.d3-baro`와 `d3-hub`는 **있지만** 아무도 probe하지 않는다.
- `probe()`가 5번 불렸다. 같은 드라이버 코드가 장치 인스턴스마다 한 번씩 — 펌웨어의 "init 함수에 base 주소 인자를 다르게 넘겨 여러 번 호출"과 같다. 그래서 드라이버는 전역 변수가 아니라 장치별 구조체(`platform_set_drvdata`)에 상태를 둔다(G3).
- 장치 이름 `9100000.d3-sensor`는 `of_device_make_bus_id()`가 "**CPU 주소로 번역한 `reg`** + `.` + 노드 이름"으로 지은 것이다. simple-bus 밑 `reg = <0x100 ...>`이 `ranges`를 거쳐 `9200100`이 된 것도 보인다. 주소 번역이 안 되는 노드(`d3-hub`)는 노드 이름만 쓴다.
- `%pR`이 `[mem 0x09100000-0x09100fff]` — `reg`가 `struct resource`로 변환되어 있다. 실제 드라이버는 여기서 `devm_platform_ioremap_resource(pdev, 0)`(H1)를 부른다. 이 실습 주소에는 실제 하드웨어가 없으므로 일부러 ioremap/접근은 하지 않았다.

---

## 3. 2단계 — 드라이버 등록과 `of_match_table`

### 3.1 드라이버는 "자기소개서"를 버스에 제출한다

`insmod d3_sensor.ko` → `module_init` → `__platform_driver_register(&d3_driver, THIS_MODULE)`. 이 함수는 `d3_driver.driver.bus = &platform_bus_type`을 채우고 `driver_register()` → `bus_add_driver()`를 부른다. `bus_add_driver()`는 드라이버를 버스의 드라이버 목록(klist)에 넣고 sysfs에 `/sys/bus/platform/drivers/d3_sensor/`를 만든 뒤, 버스의 `drivers_autoprobe`가 켜져 있으면(기본값 1) `driver_attach()`로 **이미 있는 모든 장치에 대해 매칭을 시도**한다.

| 구조체 | 들어 있는 것 | 이 노트의 값 |
|---|---|---|
| `struct platform_driver` | `probe`, `remove`, `shutdown`, `id_table`, `driver` | `d3_driver` |
| `struct device_driver` | `name`, `bus`, `of_match_table`, `acpi_match_table`, `pm`, `probe_type` | `.name = "d3_sensor"` |
| `struct of_device_id` | `name[32]`, `type[32]`, `compatible[128]`, `data` | `{"don,imu-v2", &d3_imu_v2}` |
| `struct bus_type` | `match`, `probe`, `remove`, `uevent`, `driver_override` 플래그 | `platform_bus_type` |

### 3.2 compatible 문자열 규칙

- 형식은 `"vendor,device"`. vendor 접두어는 `Documentation/devicetree/bindings/vendor-prefixes.yaml`에 등록된 것(예: `bosch`, `st`, `ti`, `nxp`, `invensense`)을 쓴다. 이 노트의 `don`은 실습용 가짜 접두어다 (upstream이라면 `dtbs_check`가 경고한다, D7).
- 목록은 **가장 구체적인 것부터** 쓴다: `"don,imu-v3", "don,imu"`. 뜻은 "나는 정확히 v3이고, v3 전용 드라이버가 없으면 v1 드라이버로 돌려도 된다". 그러므로 fallback 항목은 **진짜로 하위 호환**일 때만 넣는다. v3에서 레지스터 맵이 바뀌었는데 `"don,imu"`를 fallback으로 적으면, 드라이버가 v1 레지스터를 써서 조용히 쓰레기 값을 읽는다.
- SoC IP는 흔히 `"fsl,imx8mm-i2c", "fsl,imx21-i2c"`처럼 "이 SoC의 인스턴스" + "처음 나온 호환 IP" 쌍으로 쓴다. 드라이버는 대개 오래된 이름 하나만 테이블에 두고, 새 SoC에서 버그 회피가 필요해지면 구체적 이름을 테이블에 추가해서 다른 `.data`를 준다.
- 와일드카드(`"don,imu-*"`)나 대소문자 무시 같은 것은 없다. `of_compat_cmp()`는 정확한 문자열 비교다(대부분의 아키텍처에서 대소문자 구분).

---

## 4. 3단계 — 버스 `match`: 누가 이기나

### 4.1 예제 5 — `platform_match()` 소스를 랩 트리에서 직접 읽기

무엇을 확인하나: 면접에서 "어떤 순서로 매칭하나?"에 답하려면 이 15줄을 알면 된다. 기억에 의존하지 말고 실제 소스를 본다.

```sh
cd /var/lab/src/linux-6.18.55
sed -n "/^static int platform_match/,/^}/p" drivers/base/platform.c
sed -n "/^static inline int of_driver_match_device/,/^}/p" include/linux/of_device.h
grep -n "^const struct of_device_id \*of_match_device\|^static int __of_device_is_compatible" drivers/of/device.c drivers/of/base.c
```

```text
static int platform_match(struct device *dev, const struct device_driver *drv)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct platform_driver *pdrv = to_platform_driver(drv);
	int ret;

	/* When driver_override is set, only bind to the matching driver */
	ret = device_match_driver_override(dev, drv);
	if (ret >= 0)
		return ret;

	/* Attempt an OF style match first */
	if (of_driver_match_device(dev, drv))
		return 1;

	/* Then try ACPI style match */
	if (acpi_driver_match_device(dev, drv))
		return 1;

	/* Then try to match against the id table */
	if (pdrv->id_table)
		return platform_match_id(pdrv->id_table, pdev) != NULL;

	/* fall-back to driver name match */
	return (strcmp(pdev->name, drv->name) == 0);
}
static inline int of_driver_match_device(struct device *dev,
					 const struct device_driver *drv)
{
	return of_match_device(drv->of_match_table, dev) != NULL;
}
drivers/of/device.c:26:const struct of_device_id *of_match_device(const struct of_device_id *matches,
drivers/of/base.c:346:static int __of_device_is_compatible(const struct device_node *device,
```

출력에서 볼 것: 5단 사다리다. `driver_override`가 설정되어 있으면 **그것만으로** 결론이 난다(같으면 1, 다르면 0 — OF 매칭은 보지도 않는다). 그다음 OF → ACPI → `id_table` → 이름. 호출 경로는 `of_driver_match_device()` → `of_match_device()` → `of_match_node()` → `__of_match_node()` → 항목마다 `__of_device_is_compatible()`.

```svg
<svg viewBox="0 0 660 320" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="300" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="170" y="45" text-anchor="middle" font-size="12">1. driver_override가 설정됐나?</text>
  <rect x="20" y="80" width="300" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="105" text-anchor="middle" font-size="12">2. OF: of_driver_match_device()</text>
  <rect x="20" y="140" width="300" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="165" text-anchor="middle" font-size="12">3. ACPI: acpi_driver_match_device()</text>
  <rect x="20" y="200" width="300" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="225" text-anchor="middle" font-size="12">4. id_table: platform_match_id()</text>
  <rect x="20" y="260" width="300" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="285" text-anchor="middle" font-size="12">5. 이름: pdev->name == drv->name</text>
  <rect x="360" y="20" width="280" height="40" rx="5" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a"/>
  <text x="500" y="45" text-anchor="middle" font-size="12">이름 같으면 1, 다르면 0 → 끝</text>
  <rect x="360" y="80" width="280" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b"/>
  <text x="500" y="105" text-anchor="middle" font-size="12">compatible 점수 &gt; 0 → 1 (그림 4)</text>
  <rect x="360" y="140" width="280" height="40" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <text x="500" y="165" text-anchor="middle" font-size="12">ACPI _HID/_CID (x86·서버)</text>
  <rect x="360" y="200" width="280" height="40" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <text x="500" y="225" text-anchor="middle" font-size="12">platform_device_id 표의 name</text>
  <rect x="360" y="260" width="280" height="40" rx="5" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <text x="500" y="285" text-anchor="middle" font-size="12">옛 board file 방식 → 1 또는 0</text>
  <line x1="320" y1="40" x2="358" y2="40" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="339" y="34" text-anchor="middle" font-size="12">예</text>
  <line x1="320" y1="100" x2="358" y2="100" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="339" y="94" text-anchor="middle" font-size="12">예</text>
  <line x1="320" y1="160" x2="358" y2="160" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="339" y="154" text-anchor="middle" font-size="12">예</text>
  <line x1="320" y1="220" x2="358" y2="220" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="339" y="214" text-anchor="middle" font-size="12">있음</text>
  <line x1="320" y1="280" x2="358" y2="280" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="170" y1="60" x2="170" y2="78" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="180" y="73" font-size="12">아니오</text>
  <line x1="170" y1="120" x2="170" y2="138" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="180" y="133" font-size="12">불일치</text>
  <line x1="170" y1="180" x2="170" y2="198" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="180" y="193" font-size="12">불일치</text>
  <line x1="170" y1="240" x2="170" y2="258" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="180" y="253" font-size="12">id_table 없음</text>
</svg>
```
그림 3 — `platform_match()`의 5단 사다리 (drivers/base/platform.c). DT 기반 ARM 보드에서는 사실상 1번과 2번만 의미가 있다.

### 4.2 점수 — "가장 구체적인 compatible이 이긴다"의 정확한 뜻

드라이버 테이블에 `"don,imu-v2"`와 `"don,imu"`가 둘 다 있고 노드가 `"don,imu-v2", "don,imu"`라면 둘 다 일치한다. 그럼 `.data`는 어느 쪽인가? `__of_match_node()`는 테이블을 처음부터 끝까지 **다 돌면서** 항목마다 점수를 매기고 **가장 높은 점수** 하나를 고른다. 점수 함수의 핵심 줄(drivers/of/base.c):

```c
	if (compat && compat[0]) {
		prop = __of_find_property(device, "compatible", NULL);
		for (cp = of_prop_next_string(prop, NULL); cp;
		     cp = of_prop_next_string(prop, cp), index++) {
			if (of_compat_cmp(cp, compat, strlen(compat)) == 0) {
				score = INT_MAX/2 - (index << 2);
				break;
			}
		}
		if (!score)
			return 0;
	}
```

`index`는 **노드의** compatible 목록에서 몇 번째 문자열과 일치했느냐다. 앞쪽일수록 점수가 높다. `type`(device_type) 일치 +2, `name` 일치 +1이 더해질 수 있지만, compatible만 쓰는 현대 테이블에서는 index만 중요하다. `<< 2`는 type·name 가산점(최대 3)이 index 한 칸 차이를 뒤집지 못하게 하려는 간격이다.

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
  <rect x="10" y="10" width="320" height="236" rx="6" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <text x="170" y="34" text-anchor="middle" font-size="13" font-weight="bold">9100000.d3-sensor</text>
  <text x="170" y="56" text-anchor="middle" font-size="12">compatible = "don,imu-v2", "don,imu"</text>
  <rect x="30" y="68" width="135" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="97" y="87" text-anchor="middle" font-size="12">[0] don,imu-v2</text>
  <rect x="175" y="68" width="135" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="242" y="87" text-anchor="middle" font-size="12">[1] don,imu</text>
  <text x="30" y="124" font-size="12">드라이버 테이블 항목 → 점수</text>
  <rect x="30" y="134" width="280" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="40" y="154" font-size="12">"don,imu-v2" → [0] 일치 → S − 0</text>
  <rect x="30" y="172" width="280" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="40" y="192" font-size="12">"don,imu" → [1] 일치 → S − 4</text>
  <text x="30" y="230" font-size="12" font-weight="bold">승자: don,imu-v2 → d3_imu_v2</text>
  <rect x="350" y="10" width="320" height="236" rx="6" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <text x="510" y="34" text-anchor="middle" font-size="13" font-weight="bold">9102000.d3-sensor (새 칩 v3)</text>
  <text x="510" y="56" text-anchor="middle" font-size="12">compatible = "don,imu-v3", "don,imu"</text>
  <rect x="370" y="68" width="135" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="437" y="87" text-anchor="middle" font-size="12">[0] don,imu-v3</text>
  <rect x="515" y="68" width="135" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="582" y="87" text-anchor="middle" font-size="12">[1] don,imu</text>
  <text x="370" y="124" font-size="12">드라이버 테이블 항목 → 점수</text>
  <rect x="370" y="134" width="280" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="380" y="154" font-size="12">"don,imu-v2" → 일치 없음 → 0</text>
  <rect x="370" y="172" width="280" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="380" y="192" font-size="12">"don,imu" → [1] 일치 → S − 4</text>
  <text x="370" y="230" font-size="12" font-weight="bold">승자: don,imu → d3_imu (fallback)</text>
  <text x="10" y="272" font-size="12">S = INT_MAX/2. 점수 = S − (index &lt;&lt; 2), type 일치 +2, name 일치 +1 (__of_device_is_compatible)</text>
  <text x="10" y="294" font-size="12">테이블 전체에서 점수가 가장 높은 항목 하나가 이긴다 → 테이블 순서와 무관 (예제 6)</text>
</svg>
```
그림 4 — 같은 드라이버 테이블, 다른 두 노드. 왼쪽은 구체적 항목(v2)이 이기고, 오른쪽은 v3을 모르는 드라이버가 fallback(`don,imu`)으로 붙는다. 기준은 **노드의** 목록 순서다.

### 4.3 예제 6 — 테이블 순서를 뒤집어도 결과가 같은가

무엇을 확인하나: "테이블에서 먼저 나온 항목이 이긴다"는 흔한 오해를 실험으로 깬다. `-DD3_REVERSE`로 `"don,imu"`를 테이블 맨 앞에 둔 모듈을 따로 빌드한다.

```sh
mkdir -p /var/tmp/D3/rev && cp ~/.elinux_scratch/D3/d3_sensor.c /var/tmp/D3/rev/
printf 'obj-m += d3_sensor.o\nccflags-y += -DD3_REVERSE\n' > /var/tmp/D3/rev/Makefile
make -s -C /var/lab/out/linux-build M=/var/tmp/D3/rev modules
modinfo -F alias /var/tmp/D3/rev/d3_sensor.ko
# 게스트: insmod /d3_rev.ko; dmesg | grep -E "9100000|9200100" | grep -o "compatible.*fifo=[0-9]*"
```

```text
of:N*T*Cdon,imu-v2C*
of:N*T*Cdon,imu-v2
of:N*T*Cdon,imuC*
of:N*T*Cdon,imu
lab initramfs: 6.18.55 up 3.31s
compatible[0]="don,imu-v2" -> IMU v2, fifo=1024
compatible[0]="don,imu-v2" -> IMU v2, fifo=1024
```

출력에서 볼 것: alias 순서는 바뀌었지만(테이블 순서가 뒤집힌 증거), 두 노드 모두 여전히 `IMU v2`로 매칭됐다. **현재 커널은 점수제라 테이블 순서와 무관하다.** 다만 점수제가 들어오기 전의 오래된 커널은 테이블에서 처음 맞는 항목을 돌려주었으므로, 관례상 지금도 구체적 항목을 테이블 앞에 쓴다.

> 함정: 점수 비교는 **한 드라이버의 테이블 안에서** 어느 항목(`.data`)을 쓸지 정할 때 쓰인다. **서로 다른 두 드라이버**가 같은 장치에 둘 다 매칭되면? 점수 비교는 없다. 장치 쪽에서 드라이버 목록을 도는 경우(`__device_attach`)는 먼저 등록된 드라이버가, 드라이버 등록 시(`driver_attach`)는 그 시점에 아직 묶이지 않은 장치만 대상이므로 **먼저 probe에 성공한 쪽**이 가져간다. "v2 전용 드라이버"와 "generic 드라이버"를 동시에 두면 로드 순서에 따라 결과가 바뀐다 — 그래서 한 드라이버가 두 compatible을 다 갖고 `.data`로 분기하는 설계를 쓴다.

---

## 5. 4단계 — `really_probe()`에서 `probe()`까지

### 5.1 두 입구, 하나의 길

매칭 시도는 두 방향에서 시작될 수 있다.

- **드라이버가 나중에 올 때** (`insmod`): `driver_register()` → `bus_add_driver()` → `driver_attach()` → `bus_for_each_dev()`로 모든 장치에 `__driver_attach(dev, drv)`.
- **장치가 나중에 생길 때** (부팅 populate, hub의 자식 생성, 핫플러그): `device_add()` → `bus_probe_device()` → `device_initial_probe()` → `__device_attach()` → `bus_for_each_drv()`로 모든 드라이버에 `__device_attach_driver()`.

둘 다 `driver_match_device(drv, dev)`를 부르는데, 이것은 `drivers/base/base.h`의 인라인 함수로 `drv->bus->match(dev, drv)` — 즉 `platform_match()`다. 일치하면 `driver_probe_device()` → `__driver_probe_device()` → `really_probe()`.

`really_probe()`가 하는 일(drivers/base/dd.c, 6.18):

1. `device_links_check_suppliers()` — fw_devlink가 만든 공급자(클럭·레귤레이터·GPIO 컨트롤러 등) 링크 중 아직 준비 안 된 것이 있으면 바로 `-EPROBE_DEFER`.
2. `device_set_driver(dev, drv)` — `dev->driver`를 채운다.
3. `pinctrl_bind_pins()` — DT의 `pinctrl-0`("default" 상태)을 probe **전에** 적용한다 (H4).
4. `bus->dma_configure` — `platform_dma_configure()`가 `of_dma_configure()`로 DMA mask·IOMMU 설정 (H9).
5. `driver_sysfs_add()` — sysfs `driver` 링크 생성.
6. `call_driver_probe()` → `bus->probe` = `platform_probe()` → `of_clk_set_defaults()`, `dev_pm_domain_attach()` → **`drv->probe(pdev)` = `d3_probe()`**.
7. 성공하면 `driver_bound()` — 장치를 드라이버의 장치 목록에 넣고, 대기 중인 deferred 장치 재시도를 트리거한다.

`call_driver_probe()`의 반환값 처리도 외워 둘 만하다:

| `probe()` 반환 | driver core의 반응 | 로그 |
|---|---|---|
| `0` | bound | (dev_dbg) "bound device to driver" |
| `-EPROBE_DEFER` | deferred 목록에 넣고 나중에 재시도 | (dev_dbg) "Driver ... requests probe deferral" |
| `-ENODEV` / `-ENXIO` | "이 장치는 내 것이 아님" — 조용히 포기 | (dev_dbg) "probe with driver ... rejects match" |
| 그 밖의 음수 | 실패 | **dev_err** "probe with driver ... failed with error N" |

`-ENODEV`가 조용한 이유: 드라이버가 칩 ID 레지스터를 읽어 보고 "내 칩이 아니네"라고 판단하는 정상 경로이기 때문이다. 반대로 진짜 실패를 `-ENODEV`로 돌려주면 **아무 로그 없이** 장치가 사라진다 — 디버깅을 어렵게 하는 흔한 실수다.

### 5.2 예제 7 — dyndbg로 driver core의 매칭 로그 켜기

무엇을 확인하나: 위 경로가 실제로 그 순서로 실행되는지. `drivers/base/dd.c`의 `dev_dbg`를 런타임에 켠다(M1).

```sh
echo "file drivers/base/dd.c +p" > /sys/kernel/debug/dynamic_debug/control
grep -c "drivers/base/dd.c" /sys/kernel/debug/dynamic_debug/control
dmesg -c > /dev/null
insmod /d3_sensor.ko
dmesg | grep -E "9100000|9102000"
```

```text
lab initramfs: 6.18.55 up 6.43s
20
[    7.924481] platform 9100000.d3-sensor: bus: 'platform': __driver_probe_device: matched device with driver d3_sensor
[    7.924577] platform 9100000.d3-sensor: bus: 'platform': really_probe: probing driver d3_sensor with device
[    7.924918] d3_sensor 9100000.d3-sensor: probe: node=d3-sensor compatible[0]="don,imu-v2" -> IMU v2, fifo=1024, reg=[mem 0x09100000-0x09100fff]
[    7.925006] d3_sensor 9100000.d3-sensor: driver: 'd3_sensor': driver_bound: bound to device
[    7.925598] d3_sensor 9100000.d3-sensor: bus: 'platform': really_probe: bound device to driver d3_sensor
[    7.926032] d3_sensor 9102000.d3-sensor: probe: node=d3-sensor compatible[0]="don,imu-v3" -> IMU v1 (generic), fifo=64, reg=[mem 0x09102000-0x09102fff]
```

(9102000의 matched/probing/bound 줄 4개는 9100000과 같은 모양이라 생략했다.)

출력에서 볼 것: `20`은 dd.c 안의 dev_dbg/pr_debug 호출 지점 수다. 로그 접두어가 `platform 9100000...`에서 `d3_sensor 9100000...`으로 바뀌는 순간을 보라 — `dev_driver_string()`이 `dev->driver`가 채워진 뒤부터 드라이버 이름을 쓴다(위 2번 단계). 이 로그는 실무에서 "매칭은 됐는데 probe가 실패한 것인가, 아예 매칭이 안 된 것인가"를 가르는 가장 빠른 방법이다. 부팅 중 매칭을 보려면 커널 인자로 `dyndbg="file drivers/base/dd.c +p"`를 준다.

```svg
<svg viewBox="0 0 680 480" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="325" height="88" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="20" y="30" font-size="12" font-weight="bold">드라이버가 나중에 올 때 (insmod)</text>
  <text x="20" y="50" font-size="12">driver_register() → bus_add_driver()</text>
  <text x="20" y="68" font-size="12">→ driver_attach(): 모든 장치 순회</text>
  <text x="20" y="86" font-size="12">→ __driver_attach(dev, drv)</text>
  <rect x="345" y="10" width="325" height="88" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="355" y="30" font-size="12" font-weight="bold">장치가 나중에 생길 때 (populate)</text>
  <text x="355" y="50" font-size="12">device_add() → bus_probe_device()</text>
  <text x="355" y="68" font-size="12">→ __device_attach(): 모든 드라이버 순회</text>
  <text x="355" y="86" font-size="12">→ __device_attach_driver(drv, data)</text>
  <line x1="172" y1="98" x2="250" y2="118" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="507" y1="98" x2="420" y2="118" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="110" y="120" width="400" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="310" y="142" text-anchor="middle" font-size="12">driver_match_device() → bus->match = platform_match()</text>
  <rect x="110" y="166" width="400" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="310" y="188" text-anchor="middle" font-size="12">driver_probe_device() → __driver_probe_device()</text>
  <rect x="110" y="212" width="400" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="310" y="234" text-anchor="middle" font-size="12">really_probe(): supplier 링크 확인 (미준비면 defer)</text>
  <rect x="110" y="258" width="400" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="310" y="280" text-anchor="middle" font-size="12">pinctrl_bind_pins → dma_configure → sysfs 링크</text>
  <rect x="110" y="304" width="400" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="310" y="326" text-anchor="middle" font-size="12">call_driver_probe() → platform_probe()</text>
  <rect x="110" y="350" width="400" height="34" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="310" y="372" text-anchor="middle" font-size="12" font-weight="bold">drv->probe(pdev) = d3_probe()</text>
  <rect x="110" y="396" width="400" height="34" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="310" y="418" text-anchor="middle" font-size="12">driver_bound(): 드라이버의 장치 목록에 추가</text>
  <line x1="310" y1="154" x2="310" y2="164" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="310" y1="200" x2="310" y2="210" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="310" y1="246" x2="310" y2="256" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="310" y1="292" x2="310" y2="302" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="310" y1="338" x2="310" y2="348" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="310" y1="384" x2="310" y2="394" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="525" y="188" font-size="12">+0.000 ms matched</text>
  <text x="525" y="234" font-size="12">+0.096 ms probing</text>
  <text x="525" y="372" font-size="12">+0.437 ms probe() 로그</text>
  <text x="525" y="418" font-size="12">+0.525 ms bound</text>
  <text x="10" y="456" font-size="12">오른쪽 숫자: 예제 7의 dyndbg 타임스탬프 차이 (9100000, TCG라 실보드보다 느림)</text>
</svg>
```
그림 5 — 매칭에서 `probe()`까지의 호출 순서 (drivers/base/dd.c, 6.18). 두 입구 중 어느 쪽으로 들어와도 `driver_match_device()`부터는 같은 길이다.

---

## 6. sysfs에서 결합 상태 보기

### 6.1 직관

driver core는 결합 결과를 **전부 sysfs 링크로** 드러낸다. 그래서 "이 장치에 무슨 드라이버가 붙었나", "이 장치는 DT의 어느 노드에서 왔나"는 `readlink` 한 번으로 답이 나온다. JTAG로 메모리를 찍어 보는 대신 커널이 디버그 뷰를 파일 시스템으로 열어 둔 셈이다.

### 6.2 예제 8 — `driver`, `of_node`, `modalias`, `uevent`

무엇을 확인하나: `insmod` 전후로 장치 디렉터리에 무엇이 생기는지, 각 링크가 어디를 가리키는지.

```sh
D=/sys/bus/platform/devices/9100000.d3-sensor
echo "== 드라이버 로드 전"; ls --color=never $D
insmod /d3_sensor.ko
echo "== 드라이버 로드 후"; ls --color=never $D
echo "== 링크 3개"
readlink $D; readlink $D/driver; readlink $D/of_node
echo "== modalias / uevent"; cat $D/modalias; cat $D/uevent
echo "== 드라이버 쪽"; ls --color=never /sys/bus/platform/drivers/d3_sensor
echo "== 짝 없는 baro"; ls --color=never /sys/bus/platform/devices/9104000.d3-baro; cat /sys/bus/platform/devices/9104000.d3-baro/modalias
echo "== hub child / disabled 노드는 DT에는 있다"
ls --color=never /proc/device-tree/d3-hub; cat /proc/device-tree/d3-sensor@9103000/status; echo
ls /sys/bus/platform/devices | grep -c 9103000
```

```text
== 드라이버 로드 전
driver_override       power                 waiting_for_supplier
modalias              subsystem
of_node               uevent
== 드라이버 로드 후
driver           modalias         power            uevent
driver_override  of_node          subsystem
== 링크 3개
../../../devices/platform/9100000.d3-sensor
../../../bus/platform/drivers/d3_sensor
../../../firmware/devicetree/base/d3-sensor@9100000
== modalias / uevent
of:Nd3-sensorT(null)Cdon,imu-v2Cdon,imu
DRIVER=d3_sensor
OF_NAME=d3-sensor
OF_FULLNAME=/d3-sensor@9100000
OF_COMPATIBLE_0=don,imu-v2
OF_COMPATIBLE_1=don,imu
OF_COMPATIBLE_N=2
MODALIAS=of:Nd3-sensorT(null)Cdon,imu-v2Cdon,imu
== 드라이버 쪽
9100000.d3-sensor  9105000.d3-sensor  module
9101000.d3-sensor  9200100.d3-sensor  uevent
9102000.d3-sensor  bind               unbind
== 짝 없는 baro
driver_override       power                 waiting_for_supplier
modalias              subsystem
of_node               uevent
of:Nd3-baroT(null)Cacme,baro
== hub child / disabled 노드는 DT에는 있다
#address-cells  compatible      name
#size-cells     d3-sensor@0
disabled 
0
```

출력에서 볼 것:

- `/sys/bus/platform/devices/` 항목 자체가 실제 장치 디렉터리 `/sys/devices/platform/9100000.d3-sensor`로 가는 **symlink**다. 실체는 `/sys/devices/` 트리(부모-자식 계층) 하나뿐이고, `/sys/bus/...`는 "버스별로 모아 본 색인"이다.
- 바인딩 후 `driver` 링크가 생기고, 드라이버 디렉터리에는 반대 방향 링크(`9100000.d3-sensor`)가 생긴다. **양방향 링크 = 결합의 증거.**
- `waiting_for_supplier`가 바인딩 후 사라졌다. fw_devlink가 공급자 대기 상태를 보여 주는 파일이다(§10).
- `of_node` 링크는 DT 노드 디렉터리(`/sys/firmware/devicetree/base/...`)를 가리킨다. 드라이버가 `dev->of_node`로 읽는 바로 그 노드다.
- `uevent`의 `OF_COMPATIBLE_N`/`OF_COMPATIBLE_x`는 udev rule에서 쓸 수 있다. `T(null)`은 노드에 `device_type` 속성이 없다는 뜻이다(`of_modalias()`가 `of_node_get_device_type()` 결과를 `%s`로 찍는데 NULL이라 이렇게 나온다).
- disabled 노드와 hub의 자식은 **DT에는 있다**(`/proc/device-tree`에 보임). 하지만 platform device는 없다(`grep -c` = 0). "DT에 있는데 장치가 없다"는 이 둘을 의심하라는 뜻이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="330" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="40" font-size="12">/sys/bus/platform/devices/</text>
  <text x="20" y="60" font-size="12">9100000.d3-sensor (symlink)</text>
  <rect x="360" y="20" width="310" height="70" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="370" y="40" font-size="12">/sys/bus/platform/drivers/d3_sensor/</text>
  <text x="370" y="60" font-size="12">9100000.d3-sensor → 장치 (symlink)</text>
  <text x="370" y="80" font-size="12">bind · unbind · module · uevent</text>
  <rect x="120" y="130" width="380" height="72" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="130" y="150" font-size="12" font-weight="bold">/sys/devices/platform/9100000.d3-sensor/</text>
  <text x="130" y="170" font-size="12">modalias · uevent · driver_override · power</text>
  <text x="130" y="190" font-size="12">driver · of_node · subsystem (symlink)</text>
  <rect x="10" y="252" width="380" height="56" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="20" y="272" font-size="12">/sys/firmware/devicetree/base/d3-sensor@9100000</text>
  <text x="20" y="292" font-size="12">= /proc/device-tree/… (compatible, reg, status)</text>
  <rect x="420" y="252" width="250" height="56" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="430" y="272" font-size="12">/sys/module/d3_sensor/</text>
  <text x="430" y="292" font-size="12">parameters/supply_ready</text>
  <line x1="175" y1="70" x2="250" y2="128" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="120" y="104" font-size="12">가리킴</text>
  <line x1="440" y1="130" x2="500" y2="92" stroke="currentColor" marker-start="url(#ah4)" marker-end="url(#ah4)"/>
  <text x="482" y="122" font-size="12">driver ↔ 장치</text>
  <line x1="230" y1="202" x2="200" y2="250" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="222" y="232" font-size="12">of_node</text>
  <line x1="600" y1="90" x2="560" y2="250" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="590" y="180" font-size="12">module</text>
</svg>
```
그림 6 — 바인딩 하나가 sysfs에 남기는 링크 지도. 초록 상자가 실체, 나머지는 색인과 링크다. 진단할 때는 초록 상자에서 출발해 `driver`와 `of_node`를 따라간다.

---

## 7. 손으로 떼고 붙이기 — `unbind`/`bind`, `driver_override`

### 7.1 예제 9 — unbind/bind 사이클

무엇을 확인하나: 드라이버를 내리지 않고 **장치 하나만** 드라이버에서 떼었다 붙일 수 있는지. 드라이버 개발 중 "probe를 다시 돌려 보기"에 매일 쓰는 기술이다.

```sh
P=/sys/bus/platform/drivers/d3_sensor
insmod /d3_sensor.ko; dmesg -c > /dev/null
echo "== unbind"
echo 9100000.d3-sensor > $P/unbind
dmesg -c | grep d3_sensor
ls /sys/bus/platform/devices/9100000.d3-sensor/driver 2>&1
echo "== bind"
echo 9100000.d3-sensor > $P/bind
dmesg -c | grep d3_sensor
readlink /sys/bus/platform/devices/9100000.d3-sensor/driver
echo "== 매칭 안 되는 장치를 bind 시도"
echo 9104000.d3-baro > $P/bind; echo "exit=$?"
echo "== rmmod 하면 남은 모든 장치의 remove()"
rmmod d3_sensor; dmesg -c | grep -c "remove:"
```

```text
lab initramfs: 6.18.55 up 5.32s
== unbind
[    7.534216] d3_sensor 9100000.d3-sensor: remove: IMU v2
ls: /sys/bus/platform/devices/9100000.d3-sensor/driver: No such file or directory
== bind
[    7.820744] d3_sensor 9100000.d3-sensor: probe: node=d3-sensor compatible[0]="don,imu-v2" -> IMU v2, fifo=1024, reg=[mem 0x09100000-0x09100fff]
../../../bus/platform/drivers/d3_sensor
== 매칭 안 되는 장치를 bind 시도
sh: write error: No such device
exit=1
== rmmod 하면 남은 모든 장치의 remove()
5
```

출력에서 볼 것: `unbind` → `remove()`, `bind` → 다시 `probe()`. 모듈은 계속 로드돼 있다. `bind`도 **매칭 검사를 거친다** — baro는 `d3_sensor`와 매칭이 안 되니 `-ENODEV`("No such device")로 거절된다. `rmmod`는 묶인 장치 5개 모두에 `remove()`를 부른다. remove 경로의 자원 해제 버그(해제 누락, 이중 해제)는 이 unbind/bind 반복으로 잡는다 — 커널에는 이를 자동화한 `CONFIG_DEBUG_TEST_DRIVER_REMOVE`(probe 직후 remove→probe를 한 번 더)도 있다(이 랩 커널은 꺼져 있다).

### 7.2 예제 10 — `driver_override`: 매칭 규칙을 무시하고 강제로 지정

`driver_override`는 장치별 sysfs 파일이다. 여기에 드라이버 이름을 쓰면 그 장치는 **오직 그 이름의 드라이버하고만** 매칭된다(그림 3의 1번). 원래 용도는 VFIO/UIO처럼 "아무 장치나 받는 범용 드라이버"에 특정 장치를 넘기는 것이다. 빈 줄을 쓰면 해제된다.

> 버전 메모: 이 랩의 6.18.55 소스에서는 `driver_override`가 `struct device` 안(`dev->driver_override`)에 있고, `bus_type.driver_override = true`인 버스에 대해 driver core(drivers/base/bus.c)가 sysfs 파일을 만든다. 더 오래된 커널은 `struct platform_device`에 `driver_override` 필드가 있고 platform 버스가 직접 파일을 만들었다. sysfs 사용법은 같다.

무엇을 확인하나: (1) 매칭 안 되는 baro에 override로 `d3_sensor`를 강제하면 어떻게 되나, (2) override를 다른 이름으로 걸면 정상 장치도 bind가 거부되나, (3) 지우면 원래대로 돌아오나.

```sh
echo "file drivers/base/dd.c +p" > /sys/kernel/debug/dynamic_debug/control
P=/sys/bus/platform/drivers/d3_sensor
insmod /d3_sensor.ko; dmesg -c > /dev/null
B=/sys/bus/platform/devices/9104000.d3-baro
echo "== 1) baro에 driver_override=d3_sensor 강제"
cat $B/driver_override
echo d3_sensor > $B/driver_override
echo 9104000.d3-baro > /sys/bus/platform/drivers_probe
dmesg -c
ls $B/driver 2>&1
echo "== 2) 9101000을 다른 드라이버 이름에 묶어 두기"
S=/sys/bus/platform/devices/9101000.d3-sensor
echo 9101000.d3-sensor > $P/unbind
echo some_vfio_like_drv > $S/driver_override
echo 9101000.d3-sensor > /sys/bus/platform/drivers_probe
echo 9101000.d3-sensor > $P/bind; echo "bind exit=$?"
echo "== 3) override 지우기 (빈 줄) -> 다시 OF 매칭"
echo > $S/driver_override
echo 9101000.d3-sensor > /sys/bus/platform/drivers_probe
dmesg -c | grep -o "9101000.*-> [^,]*"
```

```text
lab initramfs: 6.18.55 up 3.47s
== 1) baro에 driver_override=d3_sensor 강제
(null)
[    5.040184] platform 9104000.d3-baro: bus: 'platform': __driver_probe_device: matched device with driver d3_sensor
[    5.040250] platform 9104000.d3-baro: bus: 'platform': really_probe: probing driver d3_sensor with device
[    5.040483] d3_sensor 9104000.d3-baro: probe with driver d3_sensor rejects match -19
ls: /sys/bus/platform/devices/9104000.d3-baro/driver: No such file or directory
== 2) 9101000을 다른 드라이버 이름에 묶어 두기
sh: write error: No such device
bind exit=1
== 3) override 지우기 (빈 줄) -> 다시 OF 매칭
9101000.d3-sensor: probe: node=d3-sensor compatible[0]="don,imu" -> IMU v1 (generic)
```

출력에서 볼 것:

- (1) override 덕분에 **매칭은 됐다**("matched device with driver d3_sensor"). 하지만 `probe()` 안의 `of_device_get_match_data()`가 테이블에서 baro의 compatible을 못 찾아 NULL → 우리 코드가 `-ENODEV`(-19)를 돌려주어 "rejects match"로 조용히 끝났다. **`driver_override`는 match 단계만 우회한다. probe가 DT 정보에 의존하면 소용없다.** match data가 NULL일 때 방어하는 `if (!info)` 줄이 이 경우를 안전하게 막았다 — 없었다면 NULL 역참조 Oops(M3)다.
- (2) override가 다른 이름이면 `bind`조차 거부된다(`bind_store()`도 `driver_match_device()`를 거친다).
- (3) 빈 줄로 지우고 `drivers_probe`(버스 단위 "이 장치 매칭 다시 해 봐" 파일)에 장치 이름을 쓰면 OF 매칭으로 돌아온다.

---

## 8. 로드 순서, 그리고 자식을 만드는 드라이버 (I2C/SPI 미리보기)

### 8.1 어느 쪽이 먼저 와도 된다

그림 5의 두 입구 덕분에 순서는 상관없다. 예제 4는 "장치 먼저(부팅 때), 드라이버 나중(insmod)"이었다. 반대 방향 — 드라이버가 이미 있고 장치가 나중에 생기는 경우 — 을 만들려면 장치를 런타임에 생성해야 한다. 실무에서 가장 흔한 형태는 **부모 드라이버가 probe 중에 자식 노드를 장치로 만드는 것**이다: I2C/SPI 컨트롤러, MFD(PMIC 하나에 레귤레이터·RTC·GPIO가 들어 있는 칩), simple-bus가 아닌 커스텀 버스.

그래서 `d3_hub`를 만든다. `"don,hub"` 노드에 붙어서 probe에서 `devm_of_platform_populate()`로 자식 노드를 platform device로 만든다 — MFD 드라이버들이 쓰는 바로 그 패턴이다.

```c
// SPDX-License-Identifier: GPL-2.0
/* d3_hub: "don,hub" 노드에 붙어서, probe()에서 자식 노드들을 platform_device로 만든다 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>

static int d3_hub_probe(struct platform_device *pdev)
{
	int ret;

	dev_info(&pdev->dev, "hub probe: populating children of %pOF\n", pdev->dev.of_node);
	ret = devm_of_platform_populate(&pdev->dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "populate failed\n");
	return 0;
}

static const struct of_device_id d3_hub_of_match[] = {
	{ .compatible = "don,hub" },
	{ }
};
MODULE_DEVICE_TABLE(of, d3_hub_of_match);

static struct platform_driver d3_hub_driver = {
	.probe  = d3_hub_probe,
	.driver = {
		.name = "d3_hub",
		.of_match_table = d3_hub_of_match,
	},
};
module_platform_driver(d3_hub_driver);

MODULE_DESCRIPTION("D3 hub: populates child nodes");
MODULE_LICENSE("GPL");
```

### 8.2 예제 11 — 순서 A(드라이버 먼저) vs 순서 B(장치 먼저)

무엇을 확인하나: 자식 장치 `d3-hub:d3-sensor@0`에 대해 (A) `d3_sensor`가 이미 로드된 상태에서 장치가 생기는 경우와 (B) 장치가 먼저 생기고 드라이버가 나중에 오는 경우, 둘 다 probe가 불리는지.

```sh
echo "== A) sensor 드라이버 먼저, 그다음 hub (자식 device가 나중에 생김)"
insmod /d3_sensor.ko
dmesg -c > /dev/null
insmod /d3_hub.ko
dmesg -c | grep -E "d3_"
ls /sys/bus/platform/devices | grep hub
rmmod d3_hub; rmmod d3_sensor; dmesg -c > /dev/null
echo "== B) hub 먼저 (자식 device 생성), 그다음 sensor 드라이버"
insmod /d3_hub.ko
ls -l /sys/bus/platform/devices/ | grep "d3-hub:" | sed 's/.*\(d3-hub[^ ]*\) ->.*/\1/'
dmesg -c > /dev/null
insmod /d3_sensor.ko
dmesg -c | grep "d3-hub:"
```

```text
lab initramfs: 6.18.55 up 7.29s
== A) sensor 드라이버 먼저, 그다음 hub (자식 device가 나중에 생김)
[    9.727639] d3_hub d3-hub: hub probe: populating children of /d3-hub
[    9.759144] d3_sensor d3-hub:d3-sensor@0: probe: node=d3-sensor compatible[0]="don,imu" -> IMU v1 (generic), fifo=64, reg=(null)
d3-hub
d3-hub:d3-sensor@0
== B) hub 먼저 (자식 device 생성), 그다음 sensor 드라이버
d3-hub:d3-sensor@0
[   11.034265] d3_sensor d3-hub:d3-sensor@0: probe: node=d3-sensor compatible[0]="don,imu" -> IMU v1 (generic), fifo=64, reg=(null)
```

출력에서 볼 것:

- A: hub probe가 자식을 만드는 **그 순간** `device_add()` → `__device_attach()`가 이미 로드된 `d3_sensor`를 찾아 probe했다(타임스탬프 30 ms 차이, 같은 insmod 안).
- B: 자식 장치가 먼저 있고, `d3_sensor` insmod 때 `driver_attach()`가 찾아 probe했다.
- 장치 이름 `d3-hub:d3-sensor@0`: hub에 `ranges`가 없어 `reg = <0>`을 CPU 주소로 번역할 수 없으니, `of_device_make_bus_id()`가 "부모이름:노드이름"으로 지었다. `reg=(null)`도 같은 이유 — 메모리 리소스가 없다. I2C 슬레이브 주소처럼 **CPU 주소 공간이 아닌 `reg`**는 platform device로 만들면 안 되는 이유가 바로 이것이다. 그래서 I2C/SPI는 자기 버스 타입을 쓴다.
- `rmmod d3_hub` 하면 `devm_` 덕분에 자식 장치가 자동으로 지워진다(A 끝에서 정리됨).

### 8.3 예제 12 — 진짜 I2C IMU 드라이버는 어떻게 생겼나

무엇을 확인하나: 실제 커널의 Bosch BMI160 IMU 드라이버(I2C)와 I2C 버스의 `match` 함수. platform과 같은 구조인지.

```sh
cd /var/lab/src/linux-6.18.55
grep -n -A6 "of_device_id bmi160_of_match" drivers/iio/imu/bmi160/bmi160_i2c.c
grep -n "of_match_table\|module_i2c_driver\|i2c_device_id" drivers/iio/imu/bmi160/bmi160_i2c.c
sed -n "/^static int i2c_device_match/,/^}/p" drivers/i2c/i2c-core-base.c
grep -rl "struct of_device_id" drivers | wc -l
grep -rh "MODULE_DEVICE_TABLE(of" drivers | wc -l
```

```text
63:static const struct of_device_id bmi160_of_match[] = {
64-	{ .compatible = "bosch,bmi120" },
65-	{ .compatible = "bosch,bmi160" },
66-	{ }
67-};
68-MODULE_DEVICE_TABLE(of, bmi160_of_match);
69-
21:	const struct i2c_device_id *id = i2c_client_get_device_id(client);
40:static const struct i2c_device_id bmi160_i2c_id[] = {
75:		.of_match_table		= bmi160_of_match,
80:module_i2c_driver(bmi160_i2c_driver);
static int i2c_device_match(struct device *dev, const struct device_driver *drv)
{
	struct i2c_client	*client = i2c_verify_client(dev);
	const struct i2c_driver	*driver;


	/* Attempt an OF style match */
	if (i2c_of_match_device(drv->of_match_table, client))
		return 1;

	/* Then ACPI style match */
	if (acpi_driver_match_device(dev, drv))
		return 1;

	driver = to_i2c_driver(drv);

	/* Finally an I2C match */
	if (i2c_match_id(driver->id_table, client))
		return 1;

	return 0;
}
5176
4512
```

출력에서 볼 것: 마지막 두 숫자는 랩 6.18.55 트리의 `drivers/` 아래 `struct of_device_id`를 쓰는 파일 수와 `MODULE_DEVICE_TABLE(of` 줄 수다 — DT 매칭은 드라이버 세계의 공용어다. I2C도 **OF → ACPI → id_table** 순서로 똑같다. 차이는 장치를 누가 만드느냐다. I2C 컨트롤러(예: i.MX `"fsl,imx21-i2c"`) 노드는 SoC의 simple-bus 밑에 있어서 부팅 때 platform device가 되고, 그 **컨트롤러 드라이버의 probe**가 `i2c_add_adapter()` → `i2c_register_adapter()` → `of_i2c_register_devices()`를 부른다. 이 함수가 `for_each_available_child_of_node()`로 자식 노드(센서)를 돌며 `i2c_client`를 만든다(예제 11의 hub와 같은 그림, `status="disabled"` 자식은 `available`에서 걸러진다). SPI는 `of_register_spi_devices()`가 같은 일을 한다. 자세한 건 H5(I2C), H7(SPI).

---

## 9. modalias와 모듈 자동 로딩

### 9.1 직관 — "장치가 자기 드라이버 이름을 외친다"

드라이버가 모듈일 때 누가 `insmod`를 해 주나? 장치가 생기면 커널은 uevent(`KOBJ_ADD`)를 유저 공간으로 보낸다. 그 안에 `MODALIAS=of:Nd3-sensorT(null)Cdon,imu-v2Cdon,imu`가 있다. udev(또는 BusyBox mdev)가 이걸 받아 `modprobe "$MODALIAS"`를 실행한다. `modprobe`는 `modules.alias`(depmod가 모든 `.ko`의 `alias:`를 모아 만든 색인)에서 패턴 매칭을 해 `d3_sensor`를 찾는다.

| 쪽 | 문자열 | 누가 만드나 |
|---|---|---|
| 장치 | `of:Nd3-sensorT(null)Cdon,imu-v2Cdon,imu` | `of_modalias()` (drivers/of/module.c): `N`=노드 이름, `T`=device_type, `C`=compatible마다 하나 |
| 드라이버 | `of:N*T*Cdon,imu-v2`, `of:N*T*Cdon,imu-v2C*` | `MODULE_DEVICE_TABLE(of, ...)` → 빌드 때 `scripts/mod/file2alias.c` |
| 매칭 | glob 패턴: `of:N*T*Cdon,imuC*` 가 장치 문자열과 맞음 | `modprobe` |

드라이버 alias 끝의 `C*`가 있는 버전과 없는 버전이 둘 다 생기는 이유: 장치 문자열에 compatible이 여러 개 이어 붙어 있으므로, "내 compatible 뒤에 다른 것이 더 붙어 있어도" 맞게 하려는 것이다. 그런데 `of:N*T*Cdon,imu`가 `...Cdon,imu-v2Cdon,imu`와 맞는 것은 맨 앞 `N*T*`의 `*`가 중간을 다 먹기 때문이다. 즉 **모듈 자동 로딩은 fallback 목록의 어느 항목과 맞아도 일어난다.** "어느 `.data`를 쓸지"는 로드 후 커널의 점수제가 정한다.

### 9.2 예제 13 — depmod + modalias로 modprobe (udev coldplug 흉내)

무엇을 확인하나: 장치의 `modalias` 파일 내용만으로 올바른 모듈이 로드되는지, 그리고 부팅 후 udev가 하는 "coldplug"(이미 있는 모든 장치에 대해 modalias로 modprobe) 루프.

```sh
K=/lib/modules/$(uname -r)
mkdir -p $K/extra && cp /d3_sensor.ko /d3_hub.ko $K/extra/
depmod
ls --color=never $K; cat $K/modules.dep 2>/dev/null | head; grep -h "don," $K/modules.* 2>/dev/null
M=/sys/bus/platform/devices/9100000.d3-sensor/modalias
echo "== 한 장치: $(cat $M)"
modprobe -v "$(cat $M)"; lsmod
rmmod d3_sensor
echo "== udev coldplug 흉내: 모든 장치의 modalias로 modprobe (못 찾는 건 조용히)"
for m in /sys/bus/platform/devices/*/modalias; do modprobe -q "$(cat $m)" 2>/dev/null; done
lsmod
ls --color=never /sys/bus/platform/drivers/d3_sensor | grep d3
```

```text
lab initramfs: 6.18.55 up 8.91s
extra           modules.dep.bb
extra/d3_hub.ko of:N*T*Cdon,hubC* of:N*T*Cdon,hub
extra/d3_sensor.ko of:N*T*Cdon,imuC* of:N*T*Cdon,imu of:N*T*Cdon,imu_v2C* of:N*T*Cdon,imu_v2
== 한 장치: of:Nd3-sensorT(null)Cdon,imu-v2Cdon,imu
d3_sensor 12288 0 - Live 0xffffaa190a16b000 (O)
== udev coldplug 흉내: 모든 장치의 modalias로 modprobe (못 찾는 건 조용히)
d3_hub 12288 0 - Live 0xffffaa190a171000 (O)
d3_sensor 12288 0 - Live 0xffffaa190a16b000 (O)
9100000.d3-sensor
9101000.d3-sensor
9102000.d3-sensor
9105000.d3-sensor
9200100.d3-sensor
d3-hub:d3-sensor@0
```

출력에서 볼 것:

- BusyBox `depmod`는 데스크톱 kmod처럼 `modules.alias`를 따로 만들지 않고, `modules.dep.bb` 한 파일에 의존성과 alias를 함께 넣는다. 이 색인에서는 `-`가 `_`로 정규화되어 보인다(BusyBox modprobe가 비교할 때 양쪽을 같은 방식으로 정규화한다). 데스크톱/Yocto의 kmod `depmod`는 `modules.alias`에 원래 문자열을 그대로 둔다.
- coldplug 루프 한 번에 `d3_hub`와 `d3_sensor`가 둘 다 올라왔고, hub가 만든 자식(`d3-hub:d3-sensor@0`)까지 묶였다. hub 자식이 생기며 보낸 uevent는 이 스크립트에서 처리하지 않았지만, `d3_sensor`가 이미 로드돼 있어서 `__device_attach()`로 바로 묶였다.
- 실제 시스템에서는 `udevadm trigger --action=add`(systemd-udevd) 또는 `mdev -s`(BusyBox)가 이 coldplug를 한다. 부팅 후 생기는 장치는 hotplug uevent로 처리된다. 자세한 건 G9.

> 함정: `MODULE_DEVICE_TABLE(of, ...)`를 빠뜨리면 **insmod로는 완벽히 동작**하는데 제품 이미지에서는 자동 로드가 안 된다. 개발 중엔 손으로 insmod하니 아무도 모른다. `modinfo x.ko | grep alias`가 비어 있으면 이것이다.

---

## 10. deferred probe 미리보기

### 10.1 직관

센서 드라이버의 probe가 레귤레이터(전원), 클럭, GPIO, IRQ 컨트롤러가 필요하다고 하자. 그 공급자 드라이버가 아직 probe되지 않았다면? 펌웨어라면 init 순서를 손으로 맞춘다. Linux는 **probe에서 `-EPROBE_DEFER`를 돌려주면 "나중에 다시 불러 줘"** 로 처리한다. driver core는 그 장치를 deferred 목록에 넣고, **다른 드라이버가 하나 bind될 때마다**(`driver_bound()` → `driver_deferred_probe_trigger()`) 목록 전체를 다시 시도한다. 그리고 late_initcall 시점에 한 번 더 몰아서 시도한다.

요즘 커널은 fw_devlink(기본 `fw_devlink=on`)가 DT의 phandle 참조(`clocks`, `*-supply`, `gpios`, `interrupts`…)를 보고 공급자→소비자 링크를 미리 만든다. 그래서 공급자가 준비되기 전에는 아예 probe를 시도하지 않는다(`really_probe()`의 `device_links_check_suppliers()`, 예제 8의 `waiting_for_supplier` 파일). 드라이버의 `-EPROBE_DEFER`는 그래도 필요하다 — DT에 표현되지 않은 의존성이나 fw_devlink가 모르는 속성이 있기 때문이다. 깊은 이야기는 G3.

### 10.2 예제 14 — 일부러 defer하고, 원인을 보고, 다시 probe

무엇을 확인하나: `dev_err_probe(dev, -EPROBE_DEFER, ...)`가 남기는 이유 문자열이 debugfs `devices_deferred`에 보이는지, 조건이 풀린 뒤 재시도로 bind되는지.

```sh
dmesg -c > /dev/null
insmod /d3_sensor.ko supply_ready=0
dmesg -c | grep 9105000
echo "== devices_deferred"; cat /sys/kernel/debug/devices_deferred
ls /sys/bus/platform/devices/9105000.d3-sensor/driver 2>&1
echo "== 공급자 준비됨 -> 다시 probe 요청"
echo 1 > /sys/module/d3_sensor/parameters/supply_ready
echo 9105000.d3-sensor > /sys/bus/platform/drivers_probe
dmesg -c | grep 9105000
echo "== devices_deferred (비었나)"; cat /sys/kernel/debug/devices_deferred; echo "[end]"
```

```text
lab initramfs: 6.18.55 up 6.95s
== devices_deferred
9105000.d3-sensor	d3_sensor: supply not ready yet
ls: /sys/bus/platform/devices/9105000.d3-sensor/driver: No such file or directory
== 공급자 준비됨 -> 다시 probe 요청
[    8.949261] d3_sensor 9105000.d3-sensor: probe: node=d3-sensor compatible[0]="don,imu-v2" -> IMU v2, fifo=1024, reg=[mem 0x09105000-0x09105fff]
== devices_deferred (비었나)
[end]
```

출력에서 볼 것:

- defer 시점에 dmesg에는 **아무것도 안 찍혔다**(첫 `grep 9105000`이 빈 결과). `dev_err_probe()`는 `-EPROBE_DEFER`일 때 에러를 찍지 않고 이유만 저장한다 — 부팅 중 defer는 정상이라 로그를 어지럽히지 않으려는 설계다. 이유는 `/sys/kernel/debug/devices_deferred`에서 본다. "센서가 안 보인다" 디버깅의 첫 명령 중 하나다.
- 실제 시스템에서는 공급자 드라이버가 bind되는 순간 재시도가 자동으로 일어난다. 여기서는 공급자를 모듈 파라미터로 흉내 냈으므로 `drivers_probe`로 직접 재시도를 요청했다.
- `CONFIG_MODULES=y`인 커널에서는 `deferred_probe_timeout` 기본값이 10초다(drivers/base/dd.c). 타임아웃이 지나면 `deferred_probe_timeout_work_func()`가 `fw_devlink_drivers_done()`으로 드라이버가 끝내 안 온 공급자 링크를 완화하고 전체 재시도를 한 번 더 한 뒤, 그래도 남은 장치마다 `deferred probe pending: <이유>` 경고를 찍는다. 현장 로그에서 이 줄을 보면 바로 이 상황이다.

---

## 11. 커널 안에서는 무슨 일이 일어나나

### 11.1 구조체 지도

| 구조체 | 어디서 만들어지나 | 핵심 필드 | 이 노트의 대상 |
|---|---|---|---|
| `struct device_node` | `unflatten_device_tree()` | `name`, `full_name`, `properties`, `parent`/`child`/`sibling`, `_flags`(`OF_POPULATED`) | `/d3-sensor@9100000` |
| `struct platform_device` | `of_device_alloc()` | `name`, `id`, `resource[]`, `num_resources`, 내장 `struct device dev` | `9100000.d3-sensor` |
| `struct device` | (platform_device 안) | `of_node`, `fwnode`, `driver`, `bus`, `parent`, `driver_data`, `driver_override`, `kobj` | `pdev->dev` |
| `struct platform_driver` | 드라이버 소스 (정적) | `probe`, `remove`, `id_table`, 내장 `struct device_driver driver` | `d3_driver` |
| `struct device_driver` | (platform_driver 안) | `name`, `bus`, `of_match_table`, `owner`, `probe_type` | `"d3_sensor"` |
| `struct bus_type` | `platform_bus_type` (정적) | `match`, `uevent`, `probe`, `remove`, `dma_configure`, `driver_override` | `"platform"` |

버스는 장치 목록(klist)과 드라이버 목록(klist)을 갖고 있고, 매칭은 이 두 목록의 **곱(N×M)을 필요할 때마다 훑는** 단순한 구조다. 장치 하나가 추가되면 드라이버 M개를, 드라이버 하나가 추가되면 장치 N개를 훑는다. 해시 같은 건 없다 — `match()`가 싸기 때문이다.

### 11.2 sysfs 쓰기가 부르는 함수

| sysfs 동작 | 커널 함수 (6.18) | 결과 |
|---|---|---|
| `echo dev > drivers/X/bind` | `bind_store()` (drivers/base/bus.c) → `driver_match_device()` → `device_driver_attach()` | 매칭 검사 후 probe |
| `echo dev > drivers/X/unbind` | `unbind_store()` → `device_driver_detach()` | `remove()` 후 링크 제거 |
| `echo dev > bus/platform/drivers_probe` | `drivers_probe_store()` → `bus_rescan_devices_helper()` | 드라이버 없는 장치면 `device_attach()`로 재매칭 |
| `echo name > devices/D/driver_override` | `driver_override_store()` → `__device_set_driver_override()` | 다음 매칭부터 이름만 비교 |
| `cat devices/D/modalias` | `of_device_modalias()` → `of_modalias()` | `of:N...T...C...` |

### 11.3 드라이버를 쓸 때 다시 만나는 지점

- **G2 디바이스 모델**: 이 노트의 bus/device/driver 삼각형과 kobject·sysfs 트리를 일반화한다.
- **G3 platform driver와 생명주기**: `probe()` 안에서 `devm_*`로 자원을 잡는 법, `remove()`가 비는 이유, deferred probe의 실전 처리(`dev_err_probe`).
- **D4 자원 기술**: probe 안에서 `platform_get_irq()`, `devm_clk_get()`, `devm_regulator_get()`, `devm_gpiod_get()`가 DT의 `interrupts`, `clocks`, `*-supply`, `gpios`를 읽는다 — 이 중 공급자 미준비가 `-EPROBE_DEFER`의 대부분이다.
- **D8 드라이버에서 DT 읽기**: `of_property_read_*` vs `device_property_*`, `device_get_match_data()`.
- **H5/H7**: I2C/SPI 컨트롤러가 자식 노드를 `i2c_client`/`spi_device`로 만드는 과정(예제 12).
- **G9 udev**: modalias·uevent·coldplug의 유저 공간 쪽.

---

## 12. Embedded 실무에서 다시 보기

### 12.1 장면 1 — "IMU를 DT에 넣었는데 `/sys/bus/iio/devices`에 안 보인다"

센서 키트 보드에서 I2C IMU를 추가했는데 IIO 장치가 안 생긴다. 이 노트의 사슬을 순서대로 확인하면 어느 고리가 끊겼는지 5분 안에 나온다.

```sh
# 1) 노드가 DT에 있나? (dtb가 실제로 이걸로 부팅됐나)
ls /proc/device-tree/soc@0/bus@30800000/i2c@30a30000/ 2>/dev/null   # 경로는 보드마다 다르다
cat /proc/device-tree/.../imu@68/compatible; cat /proc/device-tree/.../imu@68/status
# 2) 장치 객체가 생겼나? (부모 컨트롤러가 probe됐나)
ls /sys/bus/i2c/devices/          # 예: 2-0068
# 3) 드라이버가 로드돼 있고 매칭되나?
cat /sys/bus/i2c/devices/2-0068/modalias; modinfo bmi160_i2c | grep alias
# 4) probe가 실패했나 / defer됐나?
dmesg | grep -i -E "2-0068|bmi160"; cat /sys/kernel/debug/devices_deferred
# 5) 묶였나?
readlink /sys/bus/i2c/devices/2-0068/driver
```

(위는 진단 순서를 보여 주는 명령 모음이다. 경로·주소는 예시이고 이 랩에서 실행한 출력은 없다.)

| 단계에서 막힘 | 흔한 원인 |
|---|---|
| 1. 노드 없음 | 다른 dtb로 부팅(U-Boot `fdtfile`), overlay 미적용, 노드를 잘못된 부모 밑에 넣음 |
| 2. 장치 없음 | `status = "disabled"`(SoC dtsi 기본값!), 부모 I2C 컨트롤러가 disabled 또는 probe 실패 |
| 3. 매칭 없음 | compatible 오타, vendor 접두어 틀림, 드라이버 Kconfig 꺼짐, 모듈 미설치 |
| 4. probe 실패 | chip ID 불일치(`-ENODEV`, 조용함), I2C NACK(`-ENXIO`/`-EREMOTEIO`), 전원 레귤레이터 미설정 |
| 5. defer 고착 | `vdd-supply`가 가리키는 레귤레이터 드라이버 없음, pinctrl 공급자 문제 |

SoC `.dtsi`는 대부분의 IP 노드를 `status = "disabled"`로 두고, 보드 `.dts`가 쓰는 것만 `&i2c3 { status = "okay"; }`로 켠다(D5). **컨트롤러를 켜는 것을 잊으면 그 밑 센서 노드는 아무리 맞아도 장치가 안 생긴다.**

### 12.2 장면 2 — 보드 리비전에서 IMU 칩이 바뀌었다

Rev B 보드에서 IMU가 후속 칩으로 바뀌었다. 레지스터 호환이면 DT에 `compatible = "vendor,imu-new", "vendor,imu-old";`로 적고 드라이버는 그대로 둔다(예제 4의 9102000처럼 fallback으로 붙는다). 나중에 새 칩 전용 기능(더 큰 FIFO)을 쓰고 싶으면 드라이버 테이블에 `"vendor,imu-new"` 항목과 새 `.data`만 추가한다 — DT는 이미 구체적 이름을 갖고 있으니 **필드에 깔린 dtb를 바꿀 필요가 없다.** 처음부터 구체적 이름을 앞에 넣는 관례의 실익이 이것이다.

### 12.3 장면 3 — 유저 공간 드라이버로 넘기기 (VFIO/UIO)

카메라 ISP 같은 장치를 커널 드라이버 대신 유저 공간 프로세스에서 직접 다루고 싶을 때, `driver_override`로 그 장치만 `vfio-platform`(또는 `uio_pdrv_genirq`)에 넘긴다. 순서: 기존 드라이버에서 `unbind` → `driver_override`에 새 드라이버 이름 → `drivers_probe`. 예제 10의 (2)(3)이 그 절차다. 다른 드라이버가 먼저 가져가지 못하게 하려고 override를 쓰는 것이다.

### 12.4 장면 4 — 부팅 시간과 probe 순서

카메라·LiDAR 드라이버가 많은 보드에서 부팅이 느리면, `initcall_debug`와 dd.c dyndbg(예제 3·7)로 어떤 probe가 오래 걸리는지, 어떤 장치가 defer를 반복하는지 본다. 오래 걸리는 드라이버는 `.driver.probe_type = PROBE_PREFER_ASYNCHRONOUS`로 비동기 probe를 허용해 다른 장치와 병렬로 돌릴 수 있다(C7).

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| compatible 오타 (`"bosh,bmi160"`) | 장치는 있는데 driver 링크 없음, 로그 없음 | `of_compat_cmp()`는 정확 비교 | `cat .../of_node/compatible`과 `modinfo` alias 비교 |
| 부모 컨트롤러 노드가 disabled | 센서 장치 자체가 없음 | 자식은 부모 드라이버 probe 때 생성 | 보드 dts에서 `&i2cN { status = "okay"; }` |
| sentinel `{ }` 누락 | 랜덤 매칭, Oops | `__of_match_node()`가 배열 끝을 넘어 읽음 | 테이블 끝에 항상 `{ }` |
| `MODULE_DEVICE_TABLE` 누락 | insmod로는 되는데 부팅 시 자동 로드 안 됨 | `.ko`에 alias 없음 | `modinfo`로 alias 확인 |
| 진짜 실패에 `-ENODEV` 반환 | 아무 에러 로그 없이 장치가 안 묶임 | `-ENODEV`/`-ENXIO`는 dev_dbg로만 기록 | 실제 에러 코드 반환 + `dev_err_probe()` |
| fallback compatible을 비호환 칩에 적음 | 드라이버가 붙는데 값이 이상함 | 점수제가 generic 항목으로 매칭 | 진짜 호환일 때만 fallback, 아니면 드라이버에 새 항목 |
| I2C 센서를 simple-bus/루트 밑에 둠 | `reg=(null)`, I2C 접근 불가 | platform device로 만들어짐 (예제 11) | 반드시 I2C 컨트롤러 노드 밑에 |
| `driver_override` 걸어 두고 잊음 | 재부팅 전까지 정상 드라이버가 안 붙음 | override는 OF 매칭보다 우선 | `echo > driver_override`로 해제 |
| `of_device_get_match_data()` NULL 미검사 | override/수동 bind 시 Oops | match 단계를 우회한 바인딩 | `if (!info) return -ENODEV;` |
| 같은 compatible을 두 드라이버가 가짐 | 부팅마다 다른 드라이버가 붙음 | 드라이버 간에는 점수 비교 없음, 선착순 | 한 드라이버로 합치고 `.data`로 분기 |

---

## 14. 면접에서 이렇게 말한다

**Q.** How does a driver get bound to a device on a Device Tree based system?

**A.** 부팅 때 커널이 DTB를 `device_node` 트리로 펼치고, `of_platform_default_populate_init()`가 루트와 simple-bus 밑의 compatible 있는 노드마다 `platform_device`를 만든다. 드라이버는 `of_match_table`을 가진 `platform_driver`를 등록한다. 장치든 드라이버든 나중에 추가되는 쪽에서 버스의 `match`(`platform_match` → `of_driver_match_device`)가 compatible 문자열을 비교하고, 맞으면 `really_probe()`가 pinctrl·DMA 설정 후 드라이버의 `probe()`를 장치마다 부른다. 결과는 sysfs의 `driver` 링크로 보인다.

> At boot the kernel unflattens the DTB into a tree of device_nodes, and of_platform_populate creates a platform_device for every node with a compatible under the root or a simple-bus. A driver registers a platform_driver with an of_match_table. Whenever either side is added, the bus match function — platform_match, which calls of_driver_match_device — compares the compatible strings. On a match, really_probe sets up pinctrl and DMA and calls the driver's probe once per device. You can see the result as the driver symlink in sysfs.

**꼬리 질문**: "I2C 센서는요?" → I2C 노드는 컨트롤러 드라이버가 probe될 때 `of_i2c_register_devices()`가 `i2c_client`로 만들고, I2C 버스의 `i2c_device_match`가 같은 OF 매칭을 한다.

**Q.** A node has `compatible = "vendor,chip-v3", "vendor,chip"`. What does that mean, and which driver entry wins?

**A.** 목록은 구체적인 것부터 쓴다. "정확히는 v3이고, v3 전용이 없으면 generic chip 드라이버로 돌려도 호환된다"는 뜻이다. 커널은 드라이버 테이블 전체에 점수를 매기는데, 노드 목록에서 앞쪽 문자열과 맞을수록 점수가 높다. 그래서 테이블에 v3 항목이 있으면 그게 이기고, 없으면 generic 항목으로 붙는다. 테이블 순서와는 무관하다 — 직접 테이블을 뒤집어 확인했다.

> The list goes from most specific to most generic: it is exactly a v3, but it is compatible with the generic chip binding. The kernel scores every entry in the driver's table, and a match earlier in the node's list scores higher. So a v3 entry wins if the driver has one; otherwise it falls back to the generic one. The order of the driver table does not matter — I verified that by reversing the table.

**Q.** Your sensor node is in the device tree but probe is never called. How do you debug it?

**A.** 다섯 고리로 나눈다. `/proc/device-tree`에 노드가 있고 `status`가 okay인지, `/sys/bus/<bus>/devices`에 장치 객체가 있는지(없으면 부모 컨트롤러가 disabled이거나 probe 실패), 드라이버가 로드돼 있고 `modalias`와 `modinfo` alias가 맞는지, `devices_deferred`에 걸려 있는지, dmesg에 probe 실패가 있는지. 매칭 여부는 `dyndbg`로 `drivers/base/dd.c`를 켜면 "matched device with driver" 줄로 바로 보인다.

> I split it into five links. Is the node in /proc/device-tree with status okay? Does a device object exist under the bus's devices directory in sysfs — if not, the parent controller is probably disabled or failed to probe. Is the driver loaded, and does the modalias match the module aliases? Is the device listed in devices_deferred? Is there a probe error in dmesg? Turning on dynamic debug for drivers/base/dd.c shows "matched device with driver" lines, which tells me immediately whether matching happened.

**Q.** What is `MODULE_DEVICE_TABLE` for? What breaks if you forget it?

**A.** 매칭 테이블을 `.ko`의 `alias`로 내보내는 매크로다. depmod가 이걸 색인으로 모으고, 장치가 생길 때 uevent의 `MODALIAS`로 udev가 modprobe해서 자동 로드한다. 빠뜨려도 insmod하면 매칭은 정상이라 개발 중엔 모르고, 제품에서 자동 로드가 안 되는 형태로 드러난다.

> It exports the match table as module aliases in the .ko. depmod indexes them, and when a device appears udev runs modprobe with the MODALIAS from the uevent, which autoloads the right module. If you forget it, insmod still works because matching happens in the kernel, so you don't notice during development — it shows up later as a driver that never autoloads on the product.

**Q.** What is `-EPROBE_DEFER` and when do you return it?

**A.** probe에 필요한 공급자(레귤레이터, 클럭, GPIO, PHY 등)가 아직 준비되지 않았을 때 돌려준다. driver core가 장치를 deferred 목록에 넣고, 다른 드라이버가 bind될 때마다 다시 시도한다. 보통 `devm_regulator_get()` 같은 함수가 이 값을 돌려주면 그대로 전파하고, `dev_err_probe()`를 쓰면 이유가 `devices_deferred`에 남는다. 요즘은 fw_devlink가 DT 참조로 공급자 링크를 만들어서 대부분 미리 막아 준다.

> It means a resource the probe needs — a regulator, clock, GPIO, or PHY — isn't available yet. The driver core puts the device on the deferred list and retries whenever another driver binds. Usually you just propagate it from calls like devm_regulator_get, and using dev_err_probe records the reason in debugfs devices_deferred. Nowadays fw_devlink creates supplier links from the DT references, so most of these are avoided up front.

**Q.** How can you force a specific driver onto a device, or rebind a driver without rebooting?

**A.** sysfs의 `drivers/<drv>/unbind`와 `bind`에 장치 이름을 쓰면 remove와 probe가 다시 돈다. 매칭 규칙을 무시하고 특정 드라이버만 붙게 하려면 장치의 `driver_override`에 드라이버 이름을 쓰고 `drivers_probe`를 친다. VFIO로 장치를 넘길 때 쓰는 방법이다. 다만 override는 match만 우회하므로 probe가 OF match data에 의존하는 드라이버는 거절할 수 있다.

> Writing the device name to the driver's unbind and bind files in sysfs reruns remove and probe. To force a particular driver regardless of the match tables, write the driver name to the device's driver_override and then trigger drivers_probe — that's how devices are handed to VFIO. Override only bypasses match, though: a driver whose probe relies on OF match data can still reject the device.

**Q.** Does it matter whether the driver module is loaded before or after the device is created?

**A.** 상관없다. 드라이버가 등록될 때 `driver_attach()`가 기존 장치를 모두 훑고, 장치가 추가될 때 `device_add()` → `__device_attach()`가 기존 드라이버를 모두 훑는다. 둘 다 같은 `match` → `really_probe` 경로로 합류한다. 직접 hub 드라이버로 자식 장치를 런타임에 만들어 두 순서를 다 확인했다.

> No. When a driver registers, driver_attach walks all existing devices; when a device is added, device_add calls into __device_attach, which walks all registered drivers. Both converge on the same match and really_probe path. I checked both orders with a small hub driver that creates child devices at runtime.

---

## 15. 직접 해보기

1. **개념**: `d3-sensor@9103000`의 `status`를 `"okay"`로 바꾸면 dmesg에 probe가 몇 번 찍히나? `status`를 아예 지우면? — 정답/힌트: 둘 다 6번(속성이 없으면 okay로 본다, `of_device_is_available()`).
2. **코드**: `d3_of_match`에 `{ .compatible = "don,imu-v3", .data = &d3_imu_v3 }`(FIFO 4096)를 추가하고 다시 부팅해 9102000이 어떤 모델로 붙는지 확인하라. — 정답/힌트: 노드 목록의 [0]과 맞으므로 v3이 이긴다. DT는 바꿀 필요 없다(§12.2).
3. **관찰**: 커널 인자에 `dyndbg="file drivers/base/dd.c +p"`를 넣고(QEMU `-append`에서 따옴표 주의) 부팅해 `d3-soc@9200000`이 `simple-pm-bus`에 묶이는 줄을 찾아라. — 정답/힌트: "matched device with driver simple-pm-bus".
4. **코드**: `d3_probe()`가 `-EIO`를 돌려주게 바꾸고 dmesg를 보라. `-ENODEV`일 때와 무엇이 다른가? — 정답/힌트: `-EIO`는 dev_err로 "probe with driver d3_sensor failed with error -5"가 찍히고, `-ENODEV`는 dyndbg 없이는 아무것도 안 찍힌다.
5. **sysfs**: baro 장치에 `driver_override`를 걸지 않고 `/sys/bus/platform/drivers/d3_sensor/bind`에 써 보고, override를 건 뒤에도 써 보라. 결과가 왜 다른가? — 정답/힌트: 첫째는 match 실패로 `-ENODEV`, 둘째는 match는 통과하지만 probe가 match data NULL로 거절(예제 10).
6. **I2C 미리보기**: `d3_hub`를 고쳐 자식의 `reg`를 "슬레이브 주소"로 읽어 `dev_info`로 찍게 하라(`of_property_read_u32(child, "reg", &addr)`). 왜 진짜 I2C에서는 이 방식 대신 `i2c_client`를 쓰는가? — 정답/힌트: 주소가 CPU 메모리 주소가 아니므로 `struct resource`/ioremap 대상이 아니고, 전송은 어댑터의 `i2c_transfer()`를 거쳐야 하기 때문(H5).
7. **modalias**: `of:Nd3-sensorT(null)Cdon,imu-v3Cdon,imu`(9102000) 문자열이 `d3_sensor`의 alias 중 어느 것과 맞는지 손으로 glob 매칭해 보라. — 정답/힌트: `of:N*T*Cdon,imu`(끝이 `Cdon,imu`로 끝남)와 맞는다. `C*`가 붙은 버전은 뒤에 다른 compatible이 더 올 때를 위한 것이다.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| compatible | 호환 문자열 목록 | DT 노드가 "나는 어떤 칩이고 무엇과 호환인지" 밝히는 속성, 구체적인 것부터 |
| `of_match_table` | OF 매칭 표 | 드라이버가 다룰 수 있는 compatible 목록(`struct of_device_id[]`) |
| match data | 매칭 데이터 | `of_device_id.data` — 매칭된 항목별 칩 정보, `of_device_get_match_data()`로 꺼냄 |
| `platform_device` | 플랫폼 장치 | 자기 열거가 안 되는 SoC 내부 장치를 나타내는 객체, DT에서 생성 |
| populate | 장치 채우기 | `of_platform_populate()`가 DT 노드를 platform device로 만드는 과정 |
| simple-bus | 단순 버스 | 자식도 그대로 platform device로 만들라는 표시의 compatible |
| bind / unbind | 결합 / 분리 | 장치에 드라이버를 붙이고(probe) 떼는(remove) 것 |
| `really_probe()` | 실제 probe 함수 | driver core에서 pinctrl·DMA·sysfs를 준비하고 `probe()`를 부르는 함수 |
| `driver_override` | 드라이버 강제 지정 | 장치별 sysfs 파일, 쓰면 그 이름의 드라이버와만 매칭 |
| modalias | 모듈 별칭 | 장치가 내보내는 `of:N...T...C...` 문자열, modprobe가 모듈을 찾는 키 |
| `MODULE_DEVICE_TABLE` | 장치 테이블 내보내기 | 매칭 표를 `.ko`의 alias로 내보내 자동 로딩을 가능하게 하는 매크로 |
| uevent | 유저 이벤트 | 장치 추가·제거 시 커널이 유저 공간(udev/mdev)에 보내는 메시지 |
| coldplug | 콜드플러그 | 부팅 전에 이미 생긴 장치들의 uevent를 다시 재생해 모듈을 로드하는 것 |
| deferred probe | 지연 probe | probe가 `-EPROBE_DEFER`를 돌려주면 나중에 재시도하는 메커니즘 |
| fw_devlink | 펌웨어 장치 링크 | DT phandle 참조로 공급자→소비자 의존성을 자동으로 만드는 기능 |
| `OF_POPULATED` | 생성 완료 플래그 | 노드에서 이미 장치를 만들었음을 표시해 중복 생성을 막는 플래그 |

---

## 17. 요약 & 체크리스트

드라이버 바인딩은 "장치 명단"과 "드라이버 자기소개서"를 버스가 짝짓는 일이다. 장치 쪽은 부팅 때 `unflatten_device_tree()`와 `of_platform_default_populate_init()`가 루트·simple-bus 밑의 compatible 있고 `status`가 okay인 노드마다 `platform_device`를 만든다(I2C/SPI 자식은 컨트롤러 드라이버가 만든다). 드라이버 쪽은 `of_match_table`을 가진 `platform_driver`를 등록한다. 어느 쪽이 나중에 오든 `driver_match_device()` → `platform_match()`가 `driver_override` → OF → ACPI → id_table → 이름 순으로 판정하고, OF 매칭은 노드 compatible 목록의 앞쪽일수록 높은 점수를 주어 가장 구체적인 항목을 고른다(테이블 순서 무관). 일치하면 `really_probe()`가 공급자 링크·pinctrl·DMA·sysfs를 준비하고 `probe()`를 부르며, `-EPROBE_DEFER`는 재시도, `-ENODEV`는 조용한 거절, 그 밖은 에러다. 결과는 sysfs의 `driver`·`of_node` 링크로 보이고, `bind`/`unbind`/`driver_override`/`drivers_probe`로 손으로 바꿀 수 있다. 모듈 자동 로딩은 `MODULE_DEVICE_TABLE`이 만든 alias와 장치의 `modalias`가 만나 일어난다.

- [ ] DT 노드가 platform device가 되는 조건(루트/simple-bus 자식, compatible, status)을 설명할 수 있다
- [ ] `d3-hub`의 자식이 왜 자동으로 안 생기는지, I2C 센서는 누가 만드는지 설명할 수 있다
- [ ] `platform_match()`의 5단 판정 순서를 말할 수 있다
- [ ] fallback compatible 목록에서 어느 테이블 항목이 이기는지 점수 규칙으로 설명할 수 있다
- [ ] `of_match_table` + match data + `MODULE_DEVICE_TABLE`을 갖춘 platform driver를 경고 없이 짤 수 있다
- [ ] `driver_attach`와 `__device_attach` 두 입구가 `really_probe()`로 합류하는 경로를 그릴 수 있다
- [ ] probe 반환값(`0`, `-EPROBE_DEFER`, `-ENODEV`, 기타)에 따른 driver core 반응을 말할 수 있다
- [ ] sysfs에서 `driver`, `of_node`, `modalias`, `uevent`로 결합 상태를 읽을 수 있다
- [ ] unbind/bind와 `driver_override`로 장치를 다른 드라이버에 넘길 수 있다
- [ ] modalias와 module alias가 어떻게 만나 자동 로딩이 되는지 설명할 수 있다
- [ ] `devices_deferred`와 dd.c dyndbg로 "probe가 안 불린다"를 진단할 수 있다
- [ ] QEMU에 커스텀 dtb(`-dtb`)와 모듈을 넣어 매칭 실험을 재현할 수 있다

---

## 참고 자료

- docs.kernel.org — Linux and the Devicetree (usage model): https://docs.kernel.org/devicetree/usage-model.html
- docs.kernel.org — Driver Model, Platform Devices and Drivers: https://docs.kernel.org/driver-api/driver-model/platform.html
- docs.kernel.org — Driver Binding: https://docs.kernel.org/driver-api/driver-model/binding.html
- docs.kernel.org — Dynamic debug: https://docs.kernel.org/admin-guide/dynamic-debug-howto.html
- devicetree.org — Devicetree Specification (compatible, status 정의): https://www.devicetree.org/specifications/
- 커널 소스 (이 노트에서 인용한 위치, v6.18): `drivers/base/platform.c`(`platform_match`, `platform_probe`, `platform_uevent`), `drivers/base/dd.c`(`really_probe`, `call_driver_probe`, `__driver_attach`, `__device_attach`, deferred probe), `drivers/base/bus.c`(`bind_store`, `driver_override`), `drivers/of/platform.c`(`of_platform_bus_create`, `of_platform_default_populate_init`), `drivers/of/base.c`(`__of_device_is_compatible`, `__of_match_node`), `drivers/of/device.c`(`of_match_device`, `of_device_get_match_data`, `of_device_make_bus_id`), `drivers/of/module.c`(`of_modalias`), `scripts/mod/file2alias.c`, `drivers/i2c/i2c-core-of.c` — https://elixir.bootlin.com/linux/v6.18/source
- Bootlin, "Linux kernel and driver development training" 슬라이드 (Device Model, platform drivers 장): https://bootlin.com/training/kernel/
- Documentation/devicetree/bindings/vendor-prefixes.yaml — 등록된 vendor 접두어 목록
