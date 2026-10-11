# H3. 클럭 · 리셋 · 레귤레이터 — common clock framework, reset controller, regulator, 전원 시퀀스

> **이 노트를 다 읽으면**: CCF의 클럭 트리(fixed-rate·fixed-factor·gate·divider·mux·PLL)와 prepare/enable 분리를 설명하고 `clk_summary`의 count를 읽을 수 있다 · 리셋 컨트롤러의 exclusive/shared 의미와 레귤레이터 use count를 refcount 관점으로 설명할 수 있다 · 세 프레임워크의 consumer 코드와 provider 코드(`clk_hw_register_*`, `reset_controller_dev`, `regulator_desc`)를 직접 짤 수 있다 · 센서 전원 시퀀스(vddio → vdd → clock → tPWR-RST → reset 해제 → tBOOT → WHO_AM_I)를 코드로 옮기고, "버스에서 칩이 응답을 안 한다"를 debugfs로 진단할 수 있다
> **왜 필요한가**: 새 보드 bring-up에서 가장 흔한 첫 장애가 "센서가 I2C에서 NACK"이고, 원인의 대부분은 버스가 아니라 전원·클럭·리셋 순서다. 면접에서는 "The sensor doesn't respond on the bus at bring-up — what do you check?", "clk_prepare와 clk_enable은 왜 나뉘어 있나?", "dummy regulator 경고가 왜 위험한가?"로 나온다
> **Don 기준 난이도**: 전원 레일 순서·램프 시간·TCXO/PLL 락·리셋 해제 타이밍은 RF 칩 bring-up에서 매일 하던 일이다 / 새로 배울 것은 그 일을 **커널 프레임워크 세 개가 refcount와 트리로 대신 관리**한다는 구조, 그리고 각 프레임워크의 debugfs 창
> **선행 노트**: D4(DT의 `clocks`·`resets`·`*-supply` 문법 — 이 노트는 그 반대편인 프레임워크 내부와 provider를 다룬다), G3(platform driver probe/remove, `devm`) — H4(GPIO), H5(I2C), N5(runtime PM)로 이어진다

---

## 0. 큰 그림 — "칩이 응답 안 함"의 대부분은 버스 앞에서 난다

Don이 RF 칩 bring-up에서 "칩이 SPMI/RFFE에 응답을 안 한다"를 디버그할 때 버스 파형부터 보지 않았을 것이다. 먼저 확인하는 것은 늘 같다. **레일이 올라왔나, 레퍼런스 클럭이 들어가나, 리셋이 풀렸나, 풀린 뒤 충분히 기다렸나.** 이 네 가지 중 하나라도 틀리면 버스 트랜잭션은 NACK이나 타임아웃으로 끝나고, 로그에는 "I2C 에러"만 남는다.

Linux에서도 똑같다. 다만 펌웨어에서 레지스터를 직접 쓰던 네 가지 일을 **세 개의 커널 프레임워크**가 맡는다.

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="155" y="22" text-anchor="middle" font-size="13">bare-metal 펌웨어 (Don이 하던 일)</text>
<text x="495" y="22" text-anchor="middle" font-size="13">Linux 드라이버 (이 노트)</text>
<rect x="15" y="34" width="280" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="27" y="59" font-size="13">① 전원 — PMIC LDO 켜기, 램프 대기</text>
<rect x="345" y="34" width="300" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="357" y="59" font-size="13">regulator_enable() — §4</text>
<line x1="295" y1="54" x2="343" y2="54" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="15" y="86" width="280" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="27" y="111" font-size="13">② 클럭 — PLL 락 대기, 게이트 켜기</text>
<rect x="345" y="86" width="300" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="357" y="111" font-size="13">clk_prepare_enable() — §2</text>
<line x1="295" y1="106" x2="343" y2="106" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="15" y="138" width="280" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="27" y="163" font-size="13">③ 리셋 — 리셋 핀이나 비트 해제</text>
<rect x="345" y="138" width="300" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="357" y="163" font-size="13">reset_control_deassert() — §3</text>
<line x1="295" y1="158" x2="343" y2="158" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="15" y="190" width="280" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="27" y="215" font-size="13">④ 대기 — 데이터시트의 tBOOT</text>
<rect x="345" y="190" width="300" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="357" y="215" font-size="13">usleep_range() — §5</text>
<line x1="295" y1="210" x2="343" y2="210" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="330" y1="234" x2="330" y2="262" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="15" y="266" width="630" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="330" y="287" text-anchor="middle" font-size="13">⑤ 첫 버스 트랜잭션: WHO_AM_I 읽기 → ACK</text>
<text x="330" y="306" text-anchor="middle" font-size="12">①~④ 중 하나라도 틀리면 NACK · 타임아웃 — 로그에는 "I2C 에러"만 남는다</text>
</svg>
```
그림 1 — 버스 접근 전의 네 가지 전제조건. 펌웨어에서 레지스터로 하던 일을 Linux에서는 regulator · CCF · reset 프레임워크가 맡는다.

왜 그냥 레지스터를 쓰지 않고 프레임워크를 거치나? 펌웨어와 다른 Linux 보드의 사정 때문이다.

| 펌웨어에서는 | Linux 보드에서는 | 그래서 프레임워크가 하는 일 |
|---|---|---|
| 한 사람이 전체 전원 맵을 안다 | LDO 하나를 센서 3개 드라이버가 같이 쓴다 | **refcount** — 마지막 사용자가 끌 때만 실제로 끈다 |
| 클럭 경로는 init 코드에 하드코딩 | 클럭은 트리다. 게이트를 켜려면 부모 PLL도 켜져야 한다 | **부모 전파** — 자식을 켜면 부모를 자동으로 켜고 count를 올린다 |
| 보드마다 `#ifdef` | 같은 드라이버가 수십 개 보드에서 돈다 | **DT로 연결** — 어느 LDO·클럭·리셋인지는 DT가 말한다 (D4) |
| JTAG로 레지스터를 본다 | 현장 장비엔 JTAG가 없다 | **debugfs 창** — `clk_summary`, `regulator_summary` |
| 안 쓰는 블록은 처음부터 안 켠다 | 부트로더가 이것저것 켜 놓고 넘긴다 | **unused 정리** — 부팅 끝에 아무도 안 쓰는 클럭·레일을 끈다 |

이 노트는 consumer(센서 드라이버) 쪽과 provider(클럭 컨트롤러·리셋 컨트롤러·PMIC 드라이버) 쪽을 **둘 다** 직접 짜 본다. 가짜 CRU와 가짜 PMIC를 모듈로 만들고, "전원·클럭·리셋이 데이터시트대로 들어와야만 ACK하는" 가짜 센서 모델까지 넣는다. 그래서 순서를 틀리면 정말로 "not responding"이 난다.

---

## 1. 실습 준비 — 가짜 CRU · 가짜 PMIC · 까다로운 가짜 센서

### 1.1 무엇을 만드나

실행 환경: Lima VM `elinux`(호스트 커널 `7.0.0-38-generic`, aarch64) 안에서 공용 랩 커널 **Linux 6.18.55**를 QEMU `virt`로 부팅한다(TCG, KVM 없음). 커널 모듈은 랩 빌드 트리로 빌드해서 게스트 initramfs에 넣는다. 랩 DT(`/var/lab/out/virt.dts`) 사본에 노드를 추가한다.

| DT 노드 | 드라이버 | 역할 |
|---|---|---|
| `h3_cru: h3-cru` | `h3_providers.ko`의 `h3_cru` | 가짜 클럭·리셋 유닛 (i.MX의 CCM+SRC, Rockchip의 CRU 같은 것). 클럭 8개(`#clock-cells = <1>`), 리셋 라인 4개(`#reset-cells = <1>`) |
| `h3-pmic` / `regulators` / `ldo1`, `ldo2` | `h3_providers.ko`의 `h3_pmic` | 가짜 PMIC. LDO 2개, 전압 표 6단계 |
| `h3_vaux: h3-vaux` | 커널 내장 `regulator-fixed` | GPIO로 켜는 로드 스위치 3.3 V, `startup-delay-us = <3000>` |
| `h3-spare` | 커널 내장 `regulator-fixed` | 부트로더가 켜 둔(`regulator-boot-on`), 아무도 안 쓰는 레일 |
| `h3-sensor-a`, `h3-sensor-b` | `h3_sensor.ko` | consumer. 같은 LDO 두 개, 같은 리셋 라인, 각자 다른 클럭 게이트 |

"가짜 레지스터"는 모듈 안의 `u32` 배열이다. `readl`/`writel`이 RAM을 읽고 쓸 뿐이지만, CCF의 범용 gate/divider/mux 코드는 진짜 MMIO와 똑같이 이 주소로 read-modify-write를 한다. 진짜 보드라면 이 자리가 `devm_platform_ioremap_resource()`(H1)로 얻은 주소다.

**센서 칩 모델**이 이 실습의 핵심이다. `h3_providers.ko`가 `h3_chip_whoami(chip)`를 export하고, 센서 드라이버는 "I2C로 WHO_AM_I를 읽는" 대신 이 함수를 부른다. 모델은 가짜 레지스터와 기록된 시각을 보고 실제 칩처럼 판단한다.

| 조건 (위에서부터 검사) | 모델의 대답 |
|---|---|
| vdd·vddio 중 하나라도 꺼져 있거나 램프 중 | NACK — `no power` |
| 리셋 라인이 assert 상태 | NACK — `held in reset` |
| 리셋이 "전원 안정 + 1 ms(tPWR-RST)"보다 먼저 풀렸다 | NACK — `bad POR state` (전원을 껐다 켜기 전까지 계속) |
| MCLK가 꺼졌거나 6~27 MHz 밖 | NACK — `MCLK missing or out of spec` |
| 리셋 해제 후 5 ms(tBOOT)가 안 지났다 | NACK — `still booting` |
| 전부 통과 | `0x6c` 반환 |

### 1.2 예제 1 — 칩 모델과 가짜 레지스터 뱅크 (`h3_providers` 1/6)

무엇을 확인하는 코드인가: provider 모듈의 공통 부분이다. 레지스터 초기값은 "부트로더가 넘겨준 상태"를 흉내 낸다 — PLL ×50, 분주 /50, mux는 PLL 쪽, 디버그 게이트(bit 7)는 켜진 채, 리셋 라인은 전부 assert.

```c
// SPDX-License-Identifier: GPL-2.0
/* h3_providers (1/6): 가짜 레지스터 뱅크 + 센서 칩 모델 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/reset-controller.h>
#include <linux/regulator/driver.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/io.h>

enum { R_PLL, R_DIV, R_MUX, R_GATE, R_RST, R_LDO_EN, R_LDO_SEL, NR_REGS };
static u32 h3_regs[NR_REGS] = {		/* 리셋 직후(부트로더가 남긴) 값 */
	[R_PLL] = 50, [R_DIV] = 49, [R_MUX] = 1,
	[R_GATE] = BIT(7),		/* dbg 게이트: 부트로더가 켜 둔 채 */
	[R_RST] = 0xf,			/* 모든 리셋 라인 assert 상태 */
};
#define REG(i) ((void __iomem *)&h3_regs[i])	/* MMIO 대신 RAM */
static DEFINE_SPINLOCK(h3_lock);
static struct clk_hw_onecell_data *h3_clks;
static ktime_t ldo_good[2], rst_rel[4];
int h3_chip_whoami(u32 chip);

#define T_PWR_RST_US 1000	/* 데이터시트: 전원 안정 → 리셋 해제 최소 */
#define T_BOOT_US    5000	/* 데이터시트: 리셋 해제 → 첫 I2C 최소 */
static const struct { u8 rst, gate; } chip_map[2] = { {0, 4}, {0, 5} };

int h3_chip_whoami(u32 chip)	/* 칩이 버스에서 ACK를 하느냐를 흉내 */
{
	ktime_t now = ktime_get(), pg, rel;
	unsigned long mclk;
	const char *why = NULL;
	u32 en, rst;

	if (chip >= ARRAY_SIZE(chip_map))
		return -EINVAL;
	en = readl(REG(R_LDO_EN));
	rst = readl(REG(R_RST));
	rel = rst_rel[chip_map[chip].rst];
	pg = ktime_after(ldo_good[0], ldo_good[1]) ? ldo_good[0] : ldo_good[1];
	mclk = clk_hw_is_enabled(h3_clks->hws[chip_map[chip].gate]) ?
	       clk_hw_get_rate(h3_clks->hws[chip_map[chip].gate]) : 0;
	if ((en & 3) != 3 || ktime_before(now, pg))
		why = "no power (vdd/vddio not up)";
	else if (rst & BIT(chip_map[chip].rst))
		why = "held in reset";
	else if (ktime_us_delta(rel, pg) < T_PWR_RST_US)
		why = "reset released before rails were stable -> stuck in bad POR state";
	else if (mclk < 6000000 || mclk > 27000000)
		why = "MCLK missing or out of spec (6-27 MHz)";
	else if (ktime_us_delta(now, rel) < T_BOOT_US)
		why = "still booting";
	if (why) {
		pr_info("h3 chip%u model: NACK - %s (mclk=%lu)\n", chip, why, mclk);
		return -ENXIO;
	}
	return 0x6c;
}
EXPORT_SYMBOL_GPL(h3_chip_whoami);
```

`ldo_good[]`은 "LDO를 켠 시각 + 램프 1 ms", `rst_rel[]`은 리셋을 푼 시각이다(§3, §4의 provider ops가 기록한다). Don이 scope로 보던 "VDD가 90 %에 도달한 시점과 RESET_N 상승 엣지 사이 간격"을 코드로 옮긴 것이다.

### 1.3 예제 2 — DT 조각과 dtb 빌드

무엇을 확인하나: provider 두 개, 고정 레귤레이터 두 개, consumer 두 개를 붙인다. 센서 a에만 `assigned-clock-rates`와 `vaux-supply`가 있다. `-DH3_NO_VDD`를 주면 두 센서에서 `vdd-supply`가 빠진다(§4.6).

```dts
/* h3_nodes.dtsi — virt.dts 사본 끝에 #include: provider 2개 + 고정 레귤레이터 2개 + consumer 2개 */
#include <dt-bindings/gpio/gpio.h>

/ {
	h3_cru: h3-cru {			/* 가짜 클럭·리셋 유닛 (i.MX CCM/SRC 역할) */
		compatible = "dons,h3-cru";
		#clock-cells = <1>;		/* 0 osc24m 1 pll 2 div 3 mux 4 gate_a 5 gate_b 6 osc12m 7 gate_dbg */
		#reset-cells = <1>;
	};

	h3-pmic {				/* 가짜 PMIC: LDO 2개 */
		compatible = "dons,h3-pmic";
		regulators {
			h3_ldo1: ldo1 {
				regulator-name = "h3_vddio";
				regulator-min-microvolt = <1800000>;
				regulator-max-microvolt = <3300000>;
				regulator-enable-ramp-delay = <1000>;
			};
			h3_ldo2: ldo2 {
				regulator-name = "h3_vdd";
				regulator-min-microvolt = <2800000>;
				regulator-max-microvolt = <3300000>;
				regulator-enable-ramp-delay = <1000>;
			};
		};
	};

	h3_vaux: h3-vaux {			/* GPIO로 켜는 로드 스위치 */
		compatible = "regulator-fixed";
		regulator-name = "h3_vaux_3v3";
		regulator-min-microvolt = <3300000>;
		regulator-max-microvolt = <3300000>;
		gpio = <&gpio 5 GPIO_ACTIVE_HIGH>;
		enable-active-high;
		startup-delay-us = <3000>;
	};

	h3-spare {				/* 부트로더가 켜 둔, 아무도 안 쓰는 레일 */
		compatible = "regulator-fixed";
		regulator-name = "h3_spare_1v2";
		regulator-min-microvolt = <1200000>;
		regulator-max-microvolt = <1200000>;
		gpio = <&gpio 6 GPIO_ACTIVE_HIGH>;
		enable-active-high;
		regulator-boot-on;
	};

	h3-sensor-a {
		compatible = "dons,h3-sensor";
		dons,chip = <0>;
		clocks = <&h3_cru 4>;
		clock-names = "mclk";
		assigned-clocks = <&h3_cru 4>;
		assigned-clock-rates = <19200000>;
		resets = <&h3_cru 0>;
		vddio-supply = <&h3_ldo1>;
#ifndef H3_NO_VDD
		vdd-supply = <&h3_ldo2>;
#endif
		vaux-supply = <&h3_vaux>;
	};

	h3-sensor-b {
		compatible = "dons,h3-sensor";
		dons,chip = <1>;
		clocks = <&h3_cru 5>;
		clock-names = "mclk";
		resets = <&h3_cru 0>;		/* 센서 a와 같은 리셋 라인 */
		vddio-supply = <&h3_ldo1>;
#ifndef H3_NO_VDD
		vdd-supply = <&h3_ldo2>;
#endif
	};
};
```

```sh
#!/bin/sh
# mkdtb.sh <이름> [-DXXX]  → /var/tmp/H3/<이름>.dtb
set -e
W=/var/tmp/H3; K=/var/lab/src/linux-6.18.55/include; N=$1; shift
sed -e 's/^\tpl061@9030000 {/\tgpio: pl061@9030000 {/' /var/lab/out/virt.dts > $W/h3_virt.dts
echo '#include "/Users/donh/.elinux_scratch/H3/h3_nodes.dtsi"' >> $W/h3_virt.dts
cpp -nostdinc -I $K -undef -x assembler-with-cpp "$@" $W/h3_virt.dts > $W/$N.pp.dts
dtc -q -I dts -O dtb -o $W/$N.dtb $W/$N.pp.dts
```

D4와 같은 방법이다: `virt.dts`의 PL061 노드에 label `gpio`를 붙이고, C 전처리기로 `dt-bindings` 매크로를 푼 뒤 dtc로 컴파일한다. `-q`는 원래 virt 노드에서 나오는 `clocks_property` 경고(D4 예제 2)를 숨긴다.

### 1.4 예제 3 — 빌드 · 부팅 스크립트

```sh
#!/bin/sh
# build.sh [dbg] — 조각을 합쳐 두 모듈 빌드 → /var/tmp/H3/mod[-dbg]
set -e
S=/Users/donh/.elinux_scratch/H3; K=/var/lab/out/linux-build; M=/var/tmp/H3/mod
[ "$1" = dbg ] && K=/var/lab/out/linux-build-dbg && M=/var/tmp/H3/mod-dbg
mkdir -p $M && cd $M
cat $S/p1_hw.c $S/p2_clk.c $S/p3_clktree.c $S/p4_rst.c $S/p5_ldo.c $S/p6_main.c > h3_providers.c
cat $S/s1_decl.c $S/s2_on.c $S/s3_off.c $S/s4_probe.c > h3_sensor.c
printf 'obj-m := h3_providers.o h3_sensor.o\n' > Kbuild
make -C $K M=$PWD -j2 modules 2>&1 | grep -v "Entering\|Leaving"
```

```sh
#!/bin/sh
# boot.sh <dtb이름> <게스트 스크립트> [추가 커널 인자] [dbg]
set -e
W=/var/tmp/H3; S=/Users/donh/.elinux_scratch/H3; M=$W/mod; IMG=/var/lab/out/Image
[ "$4" = dbg ] && M=$W/mod-dbg && IMG=/var/lab/out/Image-dbg
R=$W/r-$(basename $M); I=$W/i-$(basename $M).gz
rm -rf $R && mkdir $R && cp -a /var/lab/build/rootfs/. $R/
cp $M/h3_providers.ko $M/h3_sensor.ko $S/g_*.sh $R/
(cd $R && find . | cpio -o -H newc --quiet | gzip) > $I
timeout 180 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel $IMG -initrd $I -dtb $W/$1.dtb \
  -append "console=ttyAMA0 quiet $3 LAB_CMD=$2"
```

`build.sh`는 §2~§5에 나오는 조각 파일을 합쳐 `h3_providers.ko`와 `h3_sensor.ko`를 만든다. `dbg`를 주면 디버그 커널(lockdep·`DEBUG_ATOMIC_SLEEP`) 빌드 트리로 빌드한다(§2.8에서 쓴다). `boot.sh`의 두 번째 인자는 랩 initramfs의 `LAB_CMD`(공백 대신 `~`)로, 게스트에서 명령 하나를 실행하고 꺼진다. 예를 들어 `H3_ARGS=bad_seq=1~sh~/g_probe.sh`는 게스트에서 `H3_ARGS=bad_seq=1 sh /g_probe.sh`가 된다.

```sh
limactl shell elinux -- sh /Users/donh/.elinux_scratch/H3/build.sh
```

```text
  CC [M]  h3_providers.o
  CC [M]  h3_sensor.o
  MODPOST Module.symvers
  CC [M]  h3_providers.mod.o
  CC [M]  .module-common.o
  CC [M]  h3_sensor.mod.o
  LD [M]  h3_providers.ko
  LD [M]  h3_sensor.ko
```

출력에서 볼 것: 경고 0개다. `W=1`(추가 경고)로 다시 빌드해도 경고가 없었다.

### 1.5 예제 4 — 첫 부팅: 정상 시퀀스 한 번

게스트 스크립트 `g_basic.sh`는 provider → consumer 순으로 모듈을 넣고, 로그와 debugfs 세 가지를 찍는다.

```sh
#!/bin/sh
# g_basic.sh — provider → consumer 로드, 시퀀스 로그와 debugfs 관찰
insmod /h3_providers.ko
insmod /h3_sensor.ko $H3_ARGS
sleep 1
echo "=== dmesg (h3) ==="; dmesg | grep -E "h3|clk:|regulator" | grep -v "loading out-of-tree"
echo "=== clk_summary (h3) ==="; grep -E "enable|count|----|h3_" /sys/kernel/debug/clk/clk_summary
echo "=== regulator_summary ==="; cat /sys/kernel/debug/regulator/regulator_summary
echo "=== h3_hw regs ==="; cat /sys/kernel/debug/h3_hw/regs
echo "=== whoami ==="; for d in /sys/bus/platform/devices/h3-sensor-*; do echo "$(basename $d): $(cat $d/whoami 2>&1)"; done
```

```sh
sh /Users/donh/.elinux_scratch/H3/boot.sh h3_ok sh~/g_basic.sh
```

```text
lab initramfs: 6.18.55 up 1.44s
[    1.607253] h3_vdd: unsupportable voltage range: 3600000-3300000uV
[    1.631023] h3_vdd: unsupportable voltage range: 3600000-3300000uV
=== dmesg (h3) ===
[    1.079174] clk: Disabling unused clocks
[    1.584418] h3_vddio: Bringing 1200000uV into 1800000-1800000uV
[    1.585215] h3_vdd: Bringing 1200000uV into 2800000-2800000uV
[    1.607178] h3_sensor h3-sensor-a: vddio set_voltage(1.70-1.95 V) = 0 -> 1800000 uV
[    1.607253] h3_vdd: unsupportable voltage range: 3600000-3300000uV
[    1.607278] h3_sensor h3-sensor-a: vdd set_voltage(3.6 V) = -22
[    1.607424] regulator regulator.3: hw: ldo1 ON
[    1.610274] h3_sensor h3-sensor-a: [ 2964 us] vddio on
[    1.610312] regulator regulator.4: hw: ldo2 ON
[    1.612295] h3_sensor h3-sensor-a: [ 4982 us] vdd on
[    1.616926] h3_sensor h3-sensor-a: [ 9588 us] vaux on
[    1.617887] h3_sensor h3-sensor-a: [10542 us] mclk on (19200000 Hz)
[    1.619373] h3_sensor h3-sensor-a: [12049 us] waited tPWR-RST
[    1.619615] h3_cru h3-cru: hw: reset line 0 DEASSERT
[    1.619757] h3_sensor h3-sensor-a: [12377 us] reset released
[    1.626850] h3_sensor h3-sensor-a: [19502 us] waited tBOOT
[    1.627284] h3_sensor h3-sensor-a: [19962 us] WHO_AM_I = 0x6c -> probe OK
[    1.630943] h3_sensor h3-sensor-b: vddio set_voltage(1.70-1.95 V) = 0 -> 1800000 uV
[    1.631023] h3_vdd: unsupportable voltage range: 3600000-3300000uV
[    1.631049] h3_sensor h3-sensor-b: vdd set_voltage(3.6 V) = -22
[    1.631080] h3_sensor h3-sensor-b: [    9 us] vddio on
[    1.631102] h3_sensor h3-sensor-b: [   31 us] vdd on
[    1.631141] h3_sensor h3-sensor-b: [   67 us] mclk on (19200000 Hz)
[    1.633359] h3_sensor h3-sensor-b: [ 2265 us] waited tPWR-RST
[    1.633417] h3_sensor h3-sensor-b: [ 2344 us] reset released
[    1.638966] h3_sensor h3-sensor-b: [ 7859 us] waited tBOOT
[    1.639067] h3_sensor h3-sensor-b: [ 7992 us] WHO_AM_I = 0x6c -> probe OK
```

(같은 실행의 debugfs 부분은 §2.5, §4.4에서 나눠 본다.) 출력에서 볼 것: `[ ... us]`는 센서 드라이버가 `power_on` 시작부터 잰 경과 시간이다(값은 실행마다 다르다). 센서 a는 LDO가 실제로 켜지고(`hw: ldo1 ON`) 램프를 기다리느라 수 ms씩 걸렸지만, 센서 b의 `vddio on`·`vdd on`은 9 us, 31 us다 — **이미 켜진 레일이라 refcount만 올렸기 때문이다.** 센서 b의 `reset released` 줄 앞에는 `hw: reset line 0 DEASSERT`가 없다. 공유 리셋이라 카운트만 올라갔다(§3).

이 로그 한 덩어리가 이 노트의 지도다.

| 로그 줄 | 무엇 | 어느 절 |
|---|---|---|
| `clk: Disabling unused clocks` (1.08 s, 모듈 로드 전) | unused 클럭 정리 시점 | §2.9 |
| `Bringing 1200000uV into ...` | 레귤레이터 등록 때 제약 적용 | §4.4 |
| `set_voltage(3.6 V) = -22`, `unsupportable voltage range` | 제약 밖 전압 요청 거부 | §4.4 |
| `hw: ldo1 ON` → `vddio on` 사이 약 3 ms | `regulator-enable-ramp-delay` 대기 | §4.4, §5.3 |
| `vaux on` 약 4.6 ms | `regulator-fixed`의 `startup-delay-us` | §4.5 |
| `mclk on (19200000 Hz)` | `assigned-clock-rates`가 트리를 바꿈 | §2.7 |
| 센서 b에는 `hw:` 줄이 없음 | 공유 refcount | §3.3, §4.4 |
| `WHO_AM_I = 0x6c` | 칩 모델이 모든 조건 통과 | §5 |

---

## 2. Common Clock Framework (CCF)

### 2.1 직관 — 데이터시트의 클럭 트리 그림 그대로

SoC 레퍼런스 매뉴얼의 "Clock Controller" 장에는 늘 같은 그림이 있다: 24 MHz 크리스털 → PLL → 분주기 → mux → 게이트 → IP 블록. CCF는 이 그림을 **커널 안의 자료구조(트리)로 그대로 만든 것**이다. 노드 하나가 `struct clk_core`(provider 쪽 실체는 `struct clk_hw`)이고, 노드마다 "부모가 누구인지, 주파수를 어떻게 계산하는지, 켜고 끄는 법"을 `struct clk_ops`로 갖는다.

| 종류 | 하는 일 | 등록 헬퍼 (provider) | DT 범용 바인딩 |
|---|---|---|---|
| fixed-rate | 고정 주파수 (크리스털·오실레이터) | `clk_hw_register_fixed_rate()` | `compatible = "fixed-clock"` |
| fixed-factor | 부모 × mult ÷ div (고정) | `clk_hw_register_fixed_factor()` | `"fixed-factor-clock"` |
| gate | 레지스터 비트 하나로 켜고 끔 | `clk_hw_register_gate()` | (보통 SoC 드라이버 안) |
| divider | 레지스터 필드로 ÷N | `clk_hw_register_divider()` | (SoC 드라이버 안) |
| mux | 여러 부모 중 하나 선택 | `clk_hw_register_mux()` | (SoC 드라이버 안) |
| PLL | 부모 × N (락 대기 필요) | 범용 헬퍼 없음 → 직접 `clk_ops` 작성 + `clk_hw_register()` | — |

gate·divider·mux는 "레지스터 주소 + 비트 위치 + 스핀락"만 주면 커널의 범용 구현(`drivers/clk/clk-gate.c`, `clk-divider.c`, `clk-mux.c`)이 ops를 대신 제공한다. SoC 클럭 드라이버(예: `drivers/clk/imx/clk-imx8mp.c`)는 대부분 이 헬퍼 호출 수백 줄과, PLL처럼 SoC 고유한 몇 개의 커스텀 ops로 이루어진다.

### 2.2 prepare와 enable — 왜 둘로 나뉘었나

CCF의 가장 특이한 점이다. 클럭을 켜는 일이 **두 단계**다.

| | `clk_prepare()` / `clk_unprepare()` | `clk_enable()` / `clk_disable()` |
|---|---|---|
| 문맥 | **잠들 수 있음** (process context만) | **원자적** (인터럽트 핸들러·스핀락 안에서도 OK) |
| 커널 락 | `prepare_lock` (mutex) | `enable_lock` (spinlock, irq off) |
| provider가 여기서 하는 일 | 오래 걸리거나 잠드는 일: PLL 켜고 락 대기, I2C로 외부 클럭 칩 설정 | 빠른 일: 게이트 비트 하나 쓰기 |
| count | `prepare_count` | `enable_count` |
| 규칙 | enable 전에 반드시 prepare | unprepare 전에 반드시 disable |

펌웨어 비유: PLL 전원을 켜고 LOCK 비트를 폴링하는 `pll_init()`(수십~수백 us, 그동안 다른 태스크로 양보하고 싶다)와, 클럭 게이트 레지스터에 비트 하나 쓰는 `CLK_GATE_ON()`(수 ns, ISR에서도 부른다)을 다른 API로 나눈 것이다. 드라이버는 probe(잠들 수 있는 곳)에서 prepare해 두고, 핫패스(예: 전송 시작 직전, IRQ 핸들러)에서는 enable/disable만 토글할 수 있다.

대부분의 드라이버는 둘을 한 번에 부른다.

```c
ret = clk_prepare_enable(clk);   /* = clk_prepare() + clk_enable(), 실패 시 되돌림 */
...
clk_disable_unprepare(clk);      /* 역순 */
```

자식을 prepare/enable하면 CCF가 **부모 쪽으로 재귀하며** 부모의 count도 올린다. 그래서 게이트 하나를 켜면 mux·divider·PLL·오실레이터까지 전부 켜진다(§2.5에서 count로 확인한다).

### 2.3 consumer API

| API | 의미 |
|---|---|
| `devm_clk_get(dev, "mclk")` | DT `clock-names`의 이름으로 핸들 얻기. 해제는 자동 |
| `devm_clk_get_optional(dev, "bus")` | 속성이 없으면 에러 대신 NULL (NULL clk에 대한 모든 호출은 no-op) |
| `devm_clk_get_enabled(dev, "mclk")` | 얻기 + `clk_prepare_enable`, 해제 때 `clk_disable_unprepare`까지 자동 |
| `devm_clk_bulk_get_all_enabled(dev, &clks)` | 노드의 모든 클럭을 한 번에 |
| `clk_get_rate()` | 현재 주파수 (캐시된 값) |
| `clk_round_rate(clk, r)` | "r을 요청하면 실제로 몇이 되나" — 하드웨어는 안 바꾼다 |
| `clk_set_rate(clk, r)` | 주파수 바꾸기. 필요하면 부모까지 바꾼다(§2.7) |
| `clk_set_parent()` | mux 부모 바꾸기 |
| `clk_rate_exclusive_get()` | "내가 쓰는 동안 다른 consumer가 이 rate를 못 바꾸게" |

`devm_clk_get_enabled()` 한 줄은 편하지만, **켜는 시점을 드라이버가 못 고른다**(get 하는 순간 켜진다). 이 노트의 센서처럼 "레일 다음에 클럭"이라는 순서가 중요하면 `devm_clk_get()`으로 핸들만 받고, 시퀀스 함수 안에서 `clk_prepare_enable()`을 부른다.

### 2.4 예제 5 — provider: PLL ops와 클럭 트리 등록 (`h3_providers` 2/6, 3/6)

무엇을 확인하나: PLL은 범용 헬퍼가 없으니 `clk_ops`를 직접 쓴다. `prepare`에서 `usleep_range()`로 락 대기를 흉내 낸다 — **잠드는 일이니 prepare에 둔다.** `determine_rate`는 "요청 rate에 가장 가까운, PLL이 만들 수 있는 rate(×25~×75)"를 돌려준다.

```c
/* h3_providers (2/6): PLL clk_ops */
static unsigned long pll_recalc(struct clk_hw *hw, unsigned long prate)
{
	return prate * (readl(REG(R_PLL)) & 0xff);
}

static int pll_determine(struct clk_hw *hw, struct clk_rate_request *req)
{
	unsigned long m = DIV_ROUND_CLOSEST(req->rate, req->best_parent_rate);

	req->rate = req->best_parent_rate * clamp(m, 25UL, 75UL);
	return 0;
}

static int pll_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long prate)
{
	u32 v = readl(REG(R_PLL));

	writel((v & ~0xffu) | (rate / prate), REG(R_PLL));
	return 0;
}

static int pll_prepare(struct clk_hw *hw)	/* 잠들 수 있음: lock 대기 */
{
	writel(readl(REG(R_PLL)) | BIT(31), REG(R_PLL));
	usleep_range(200, 300);
	return 0;
}

static void pll_unprepare(struct clk_hw *hw)
{
	writel(readl(REG(R_PLL)) & ~BIT(31), REG(R_PLL));
}

static int pll_is_prepared(struct clk_hw *hw)
{
	return !!(readl(REG(R_PLL)) & BIT(31));
}
```

트리는 이렇게 엮는다: `h3_osc24m`(fixed-rate) → `h3_pll` → `h3_pll_div`(divider, 6비트, 값+1로 나눔) → `h3_mux`(입력 0 = osc24m, 입력 1 = pll_div) → `h3_gate_a`/`h3_gate_b`. 따로 `h3_osc24m` → `h3_osc12m`(fixed-factor ÷2) → `h3_gate_dbg`.

```c
/* h3_providers (3/6): 클럭 트리 등록 */
static const struct clk_ops pll_ops = {
	.prepare = pll_prepare, .unprepare = pll_unprepare,
	.is_prepared = pll_is_prepared, .recalc_rate = pll_recalc,
	.determine_rate = pll_determine, .set_rate = pll_set_rate,
};
static struct clk_hw pll_hw = {
	.init = CLK_HW_INIT("h3_pll", "h3_osc24m", &pll_ops, 0),
};
static const char * const mux_parents[] = { "h3_osc24m", "h3_pll_div" };

static int h3_clk_register(struct device *dev)
{
	struct clk_hw **hw = h3_clks->hws;
	int i, ret;

	hw[0] = devm_clk_hw_register_fixed_rate(dev, "h3_osc24m", NULL, 0, 24000000);
	ret = devm_clk_hw_register(dev, &pll_hw);
	hw[1] = ret ? ERR_PTR(ret) : &pll_hw;
	hw[2] = devm_clk_hw_register_divider(dev, "h3_pll_div", "h3_pll",
		CLK_SET_RATE_PARENT, REG(R_DIV), 0, 6, 0, &h3_lock);
	hw[3] = devm_clk_hw_register_mux(dev, "h3_mux", mux_parents, 2,
		CLK_SET_RATE_PARENT, REG(R_MUX), 0, 1, 0, &h3_lock);
	hw[4] = devm_clk_hw_register_gate(dev, "h3_gate_a", "h3_mux",
		CLK_SET_RATE_PARENT, REG(R_GATE), 0, 0, &h3_lock);
	hw[5] = devm_clk_hw_register_gate(dev, "h3_gate_b", "h3_mux",
		CLK_SET_RATE_PARENT, REG(R_GATE), 1, 0, &h3_lock);
	hw[6] = devm_clk_hw_register_fixed_factor(dev, "h3_osc12m", "h3_osc24m", 0, 1, 2);
	hw[7] = devm_clk_hw_register_gate(dev, "h3_gate_dbg", "h3_osc12m",
		0, REG(R_GATE), 7, 0, &h3_lock);
	for (i = 0; i < 8; i++)
		if (IS_ERR(hw[i]))
			return PTR_ERR(hw[i]);
	h3_clks->num = 8;
	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, h3_clks);
}
```

요점.

- 부모는 **이름 문자열**(`"h3_pll"`)로 연결했다. 클럭 이름은 시스템 전역 이름공간이라 접두어를 붙인다. 최신 드라이버는 이름 대신 `clk_parent_data`(DT `clock-names` 인덱스나 `clk_hw` 포인터)를 권장한다 — 다른 provider 이름에 의존하지 않기 때문이다.
- `CLK_SET_RATE_PARENT`: "내 rate를 바꾸라는 요청을 부모에게도 올려 보내도 된다". gate·mux·divider에 붙였고 PLL에는 안 붙였다(오실레이터는 못 바꾼다).
- 세 범용 클럭이 같은 레지스터를 나눠 쓰므로 **스핀락 하나(`h3_lock`)를 공유**한다. 진짜 SoC에서도 한 레지스터에 게이트 비트 32개가 모여 있으니 RMW 경합을 막는 이 락이 꼭 필요하다.
- `of_clk_hw_onecell_get` + `#clock-cells = <1>`: DT의 `<&h3_cru 4>`에서 `4`가 `hws[4]`(= `h3_gate_a`)의 인덱스가 된다. 실제 SoC는 이 번호를 `include/dt-bindings/clock/<soc>-clock.h` 매크로로 정의한다.

### 2.5 예제 6 — `clk_summary`: 트리와 count 읽기

예제 4와 같은 실행의 debugfs 부분이다.

```text
=== clk_summary (h3) ===
                                 enable  prepare  protect                                duty  hardware                            connection
   clock                          count    count    count        rate   accuracy phase  cycle    enable   consumer                         id
---------------------------------------------------------------------------------------------------------------------------------------------
 h3_osc24m                           1       1        0        24000000    0          0     50000      Y   deviceless                      no_connection_id         
    h3_osc12m                        0       0        0        12000000    0          0     50000      Y      deviceless                      no_connection_id         
       h3_gate_dbg                   0       0        0        12000000    0          0     50000      Y         deviceless                      no_connection_id         
    h3_pll                           1       1        0        672000000   0          0     50000      Y      deviceless                      no_connection_id         
       h3_pll_div                    1       1        0        19200000    0          0     50000      Y         deviceless                      no_connection_id         
          h3_mux                     2       2        0        19200000    0          0     50000      Y            deviceless                      no_connection_id         
             h3_gate_b               1       1        0        19200000    0          0     50000      Y               h3-sensor-b                     mclk                     
             h3_gate_a               1       1        0        19200000    0          0     50000      Y               h3-sensor-a                     mclk                     
=== h3_hw regs ===
PLL = 0x8000001c
DIV = 0x00000022
MUX = 0x00000001
GATE = 0x00000083
...
```

(`grep "h3_"`로 우리 클럭만 남겼고, 레지스터 덤프는 앞 4줄만 남겼다.) 출력에서 볼 것:

- 들여쓰기가 트리 깊이다. `h3_mux`의 enable count가 **2**인 것은 자식 게이트 두 개가 각각 1씩 올렸기 때문이고, `h3_pll_div`·`h3_pll`·`h3_osc24m`은 mux 하나만 자식으로 세므로 1이다. **count는 "나를 켠 consumer 수"가 아니라 "나를 켜 달라고 한 자식+consumer 수"**다.
- `h3_pll` = 672 MHz, `PLL = 0x8000001c` → bit 31(prepared) + 0x1c = ×28. `DIV = 0x22` = 34 → ÷35. 672 / 35 = 19.2 MHz. 부트로더 값(×50, ÷50 = 24 MHz)에서 바뀌었다 — 이유는 §2.7.
- `GATE = 0x83` = bit 0(gate_a), bit 1(gate_b), bit 7(gate_dbg). 그런데 `h3_gate_dbg`는 count 0인데 hardware enable이 `Y`다. **부트로더가 켜 둔 채 아무도 안 쓰는 클럭** — §2.9의 주인공이다.
- consumer 열의 `deviceless / no_connection_id`는 provider가 등록할 때 CCF가 만드는 자체 핸들이다. consumer가 생기면 그 이름(`h3-sensor-a`, connection id = `clock-names`의 `mclk`)이 첫 줄에 찍히고 deviceless 줄은 다음 줄로 밀린다(여기선 `grep`으로 걸렀다).
- hardware enable 열은 provider의 `.is_enabled`를 실제로 불러 본 결과다. `.is_enabled`도 `.enable`도 없는 클럭(fixed-rate, 우리 PLL)은 항상 `Y`로 찍힌다. **count는 소프트웨어 장부, hardware enable은 레지스터** — 둘이 다르면 그게 단서다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<path d="M65,118 L65,44 L472,44 L472,118" fill="none" stroke="#888" stroke-dasharray="5,4" marker-end="url(#ah2)"/>
<text x="268" y="36" text-anchor="middle" font-size="12">mux 입력 0 (지금은 선택 안 됨)</text>
<rect x="10" y="120" width="112" height="62" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="66" y="139" text-anchor="middle" font-size="12">h3_osc24m</text>
<text x="66" y="155" text-anchor="middle" font-size="12">24 MHz</text>
<text x="66" y="171" text-anchor="middle" font-size="12">en 1 / prep 1</text>
<rect x="145" y="120" width="120" height="62" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="205" y="139" text-anchor="middle" font-size="12">h3_pll ×28</text>
<text x="205" y="155" text-anchor="middle" font-size="12">672 MHz</text>
<text x="205" y="171" text-anchor="middle" font-size="12">en 1 / prep 1</text>
<rect x="288" y="120" width="116" height="62" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="346" y="139" text-anchor="middle" font-size="12">h3_pll_div ÷35</text>
<text x="346" y="155" text-anchor="middle" font-size="12">19.2 MHz</text>
<text x="346" y="171" text-anchor="middle" font-size="12">en 1 / prep 1</text>
<rect x="426" y="120" width="96" height="62" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="474" y="139" text-anchor="middle" font-size="12">h3_mux</text>
<text x="474" y="155" text-anchor="middle" font-size="12">19.2 MHz</text>
<text x="474" y="171" text-anchor="middle" font-size="12">en 2 / prep 2</text>
<rect x="548" y="66" width="122" height="62" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="609" y="85" text-anchor="middle" font-size="12">h3_gate_a</text>
<text x="609" y="101" text-anchor="middle" font-size="12">en 1 / prep 1</text>
<text x="609" y="117" text-anchor="middle" font-size="12">→ h3-sensor-a</text>
<rect x="548" y="176" width="122" height="62" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="609" y="195" text-anchor="middle" font-size="12">h3_gate_b</text>
<text x="609" y="211" text-anchor="middle" font-size="12">en 1 / prep 1</text>
<text x="609" y="227" text-anchor="middle" font-size="12">→ h3-sensor-b</text>
<rect x="145" y="222" width="120" height="62" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="205" y="241" text-anchor="middle" font-size="12">h3_osc12m ÷2</text>
<text x="205" y="257" text-anchor="middle" font-size="12">12 MHz</text>
<text x="205" y="273" text-anchor="middle" font-size="12">en 0 / prep 0</text>
<rect x="288" y="222" width="150" height="62" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="363" y="241" text-anchor="middle" font-size="12">h3_gate_dbg</text>
<text x="363" y="257" text-anchor="middle" font-size="12">en 0 인데 HW 켜짐</text>
<text x="363" y="273" text-anchor="middle" font-size="12">(부트로더가 남김)</text>
<line x1="122" y1="151" x2="143" y2="151" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="265" y1="151" x2="286" y2="151" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="404" y1="151" x2="424" y2="151" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="522" y1="145" x2="546" y2="100" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="522" y1="158" x2="546" y2="205" stroke="currentColor" marker-end="url(#ah2)"/>
<path d="M66,182 L66,253 L143,253" fill="none" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="265" y1="253" x2="286" y2="253" stroke="currentColor" marker-end="url(#ah2)"/>
</svg>
```
그림 2 — 실습 클럭 트리와 실제 count (예제 6의 `clk_summary`). mux의 count 2는 자식 게이트 두 개 몫이다. 빨간 상자는 count 0인데 레지스터상 켜져 있는 클럭이다.

### 2.6 예제 7 — 클럭 하나를 debugfs로 자세히 보기, `clk_dump`

무엇을 확인하나: `clk_summary`는 요약이고, 클럭마다 디렉터리가 따로 있다. `clk_dump`는 같은 트리를 JSON으로 준다(스크립트로 파싱하기 좋다).

```sh
ls /sys/kernel/debug/clk/h3_gate_a
for f in clk_rate clk_enable_count clk_prepare_count clk_flags clk_parent; do printf "%-18s %s\n" $f "$(cat /sys/kernel/debug/clk/h3_gate_a/$f)"; done
head -c 1500 /sys/kernel/debug/clk/clk_dump | tr ',' '\n' | head -30
```

```text
clk_accuracy clk_duty_cycle clk_enable_count clk_flags clk_max_rate clk_min_rate clk_notifier_count clk_parent clk_phase clk_prepare_count clk_protect_count clk_rate 
clk_rate           19200000 
clk_enable_count   1 
clk_prepare_count  1 
clk_flags          CLK_SET_RATE_PARENT 
clk_parent         h3_mux 
{"h3_osc24m": { "enable_count": 1
"prepare_count": 1
"protect_count": 0
"rate": 24000000
"min_rate": 0
"max_rate": 18446744073709551615
"accuracy": 0
"phase": 0
"duty_cycle": 50000
"h3_osc12m": { "enable_count": 0
"prepare_count": 0
...
```

(`clk_dump`는 쉼표를 줄바꿈으로 바꿔 앞부분만 남겼다. 두 실행 결과를 한 블록에 모았다.) 출력에서 볼 것: `clk_flags`가 등록 때 준 `CLK_SET_RATE_PARENT`를 그대로 보여 준다. `clk_protect_count`는 `clk_rate_exclusive_get()` 횟수, `clk_notifier_count`는 rate 변경 알림을 구독한 수다. 진짜 보드에서 "이 클럭을 누가 잡고 있나"는 `clk_summary`의 consumer 열, "왜 rate가 안 바뀌나"는 `clk_protect_count`와 `clk_min_rate`/`clk_max_rate`부터 본다.

### 2.7 rate 바꾸기 — `round_rate`, `set_rate`, 부모로의 전파

`clk_set_rate(clk, r)`의 동작:

1. CCF가 `clk`의 `determine_rate`(또는 `round_rate`)로 "실제로 몇 Hz가 되나"를 묻는다. 이 클럭에 `CLK_SET_RATE_PARENT`가 있으면 provider는 **부모에게 다른 rate를 요청하는 답**을 낼 수 있다. 이게 위로 재귀한다.
2. 가장 위에서 바뀌는 클럭부터 아래로 새 rate를 계산하고, rate 변경 알림(`clk_notifier`)의 `PRE_RATE_CHANGE`를 보낸다(거부 가능).
3. 위에서부터 `.set_rate`(필요하면 `.set_parent`)를 부르고, `POST_RATE_CHANGE`를 보낸다.

**예제 4에서 이미 한 번 일어났다.** 센서 a 노드의 `assigned-clocks = <&h3_cru 4>; assigned-clock-rates = <19200000>;`는 platform bus가 드라이버 probe **직전에** `of_clk_set_defaults()`로 `clk_set_rate(gate_a, 19.2 MHz)`를 해 준다. 게이트 → mux → divider로 요청이 올라가고, divider의 범용 알고리즘이 "PLL을 몇으로 바꾸고 몇으로 나누면 정확히 19.2 MHz가 되나"를 PLL의 `determine_rate`에 물어 가며 찾았다. 결과가 PLL ×50(1200 MHz) → ×28(672 MHz), ÷50 → ÷35다. 드라이버 코드는 이 과정을 전혀 모른다.

#### 예제 8 — 다른 consumer가 공유 부모의 rate를 바꾸면

무엇을 확인하나: 센서 b가 probe에서 자기 클럭을 바꾼다(`rate_b` 모듈 파라미터, 코드는 §5.2의 `h3s_rate_demo()`). 센서 a와 b의 게이트는 **같은 mux 아래**에 있다.

```sh
sh /Users/donh/.elinux_scratch/H3/boot.sh h3_ok H3_ARGS=rate_b=24000000~sh~/g_probe.sh
```

```text
[    1.083457] h3_sensor h3-sensor-a: [29750 us] mclk on (19200000 Hz)
[    1.094647] h3_sensor h3-sensor-a: [40963 us] WHO_AM_I = 0x6c -> probe OK
[    1.099476] h3_sensor h3-sensor-b: round_rate(24000000) = 24000000
[    1.100575] h3_sensor h3-sensor-b: set_rate -> 0, now 24000000 Hz
[    1.100961] h3_sensor h3-sensor-b: [  142 us] mclk on (24000000 Hz)
[    1.108682] h3_sensor h3-sensor-b: [ 7857 us] WHO_AM_I = 0x6c -> probe OK
h3-sensor-a whoami: 0x6c
h3-sensor-b whoami: 0x6c
 h3_osc24m                           1       1        0        24000000    
    h3_mux                           2       2        0        24000000    
       h3_gate_b                     1       1        0        24000000    
       h3_gate_a                     1       1        0        24000000    
    h3_pll                           0       0        0        672000000   
       h3_pll_div                    0       0        0        19200000    
```

(`grep`으로 관련 줄만 남겼다.) 출력에서 볼 것: 트리 모양이 바뀌었다. mux의 범용 `determine_rate`가 부모 후보 둘을 비교했고, 24 MHz는 `h3_osc24m`이 그대로 만들 수 있으니 **mux가 부모를 osc24m으로 바꿨다(reparent).** count도 함께 옮겨져 PLL 가지는 count 0이 되었다. 그리고 **센서 a의 클럭도 19.2 → 24 MHz로 바뀌었다** — 센서 a는 아무것도 안 했는데.

```sh
sh /Users/donh/.elinux_scratch/H3/boot.sh h3_ok H3_ARGS=rate_b=48000000~sh~/g_probe.sh
```

```text
[    1.172432] h3_sensor h3-sensor-b: error -ENODEV: chip not responding on the bus
[    1.154272] h3_sensor h3-sensor-a: [ 9385 us] mclk on (19200000 Hz)
[    1.162941] h3_sensor h3-sensor-a: [18069 us] WHO_AM_I = 0x6c -> probe OK
[    1.165186] h3_sensor h3-sensor-b: round_rate(48000000) = 48000000
[    1.165242] h3_sensor h3-sensor-b: set_rate -> 0, now 48000000 Hz
[    1.165287] h3_sensor h3-sensor-b: [   31 us] mclk on (48000000 Hz)
[    1.172359] h3 chip1 model: NACK - MCLK missing or out of spec (6-27 MHz) (mclk=48000000)
[    1.172432] h3_sensor h3-sensor-b: error -ENODEV: chip not responding on the bus
h3-sensor-a whoami: NACK (-6)
 h3_osc24m                           1       1        0        24000000    
    h3_pll                           1       1        0        672000000   
       h3_pll_div                    1       1        0        48000000    
          h3_mux                     1       1        0        48000000    
             h3_gate_b               0       0        0        48000000    
             h3_gate_a               1       1        0        48000000    
```

출력에서 볼 것: 48 MHz는 osc로 못 만드니 PLL 가지를 유지하고 divider만 ÷14로 바꿨다(672 / 14 = 48). `set_rate`는 **성공(0)** 했지만 48 MHz는 센서 스펙 밖이라 센서 b는 probe 실패. 더 나쁜 것은 마지막 줄들이다: 이미 정상 동작 중이던 **센서 a의 클럭도 48 MHz가 되어 `whoami`가 NACK**가 됐다(`-6` = `-ENXIO`). 커널 로그에는 센서 a에 대한 경고가 한 줄도 없다.

교훈: 클럭을 공유하는 장치가 rate를 바꾸면 형제 장치가 조용히 깨진다. 막는 방법은 세 가지다.

- **`clk_rate_exclusive_get(clk)`** — "내가 쓰는 동안 이 rate를 바꾸지 마라". 보호된 클럭은 다른 consumer의 `set_rate`로 바뀌지 않는다(그 경로의 rate 계산이 현재 값을 유지하거나 `-EBUSY`로 끝난다).
- **`clk_set_rate_range(clk, min, max)`** — 내 허용 범위를 등록. CCF가 rate를 정할 때 모든 consumer의 범위 교집합을 지킨다.
- **clk notifier** (`clk_notifier_register`) — 바뀌기 전에 알림을 받고 `NOTIFY_BAD`로 거부하거나, 바뀐 뒤 내 분주 설정을 다시 맞춘다(UART 보드레이트 등).

### 2.8 예제 9 — prepare를 원자 문맥에서 부르면 (디버그 커널)

무엇을 확인하나: 센서 드라이버의 `atomic_bug=1`은 스핀락을 잡은 채 `clk_prepare_enable()`을 부른다(§5.2 코드의 `if (atomic_bug)` 분기). 그 아래에서 PLL의 `.prepare`가 `usleep_range()`로 잠든다. 디버그 커널(`Image-dbg`)로 부팅한다.

```sh
sh /Users/donh/.elinux_scratch/H3/build.sh dbg
sh /Users/donh/.elinux_scratch/H3/boot.sh h3_ok sh~/g_atomic.sh "" dbg
```

```text
[    8.149192] h3_sensor h3-sensor-a: [13719 us] vaux on
[    8.150887] BUG: scheduling while atomic: insmod/67/0x00000002
[    8.150957] 3 locks held by insmod/67:
[    8.150998]  #0: ffff00000520e150 (&dev->mutex){....}-{4:4}, at: __driver_attach+0xcc/0x220
[    8.156886]  #1: ffffaa1905365100 (h3s_lock){+.+.}-{3:3}, at: h3s_probe+0x3bc/0x618 [h3_sensor]
[    8.157295]  #2: ffffaa193adac258 (prepare_lock){+.+.}-{4:4}, at: clk_prepare_lock+0x30/0xd8
[    8.157405] Modules linked in: h3_sensor(O+) h3_providers(O)
...
[    8.158317] Call trace:
...
[    8.158724]  usleep_range_state+0xb8/0x138
[    8.158739]  pll_prepare+0x50/0x70 [h3_providers]
[    8.158768]  clk_core_prepare+0xc4/0x2d0
[    8.158785]  clk_core_prepare+0x54/0x2d0
[    8.158799]  clk_core_prepare+0x54/0x2d0
[    8.158814]  clk_core_prepare+0x54/0x2d0
[    8.158829]  clk_prepare+0x2c/0x58
[    8.158843]  clk_prepare_enable+0x18/0x78 [h3_sensor]
[    8.158867]  h3s_probe+0x3c4/0x618 [h3_sensor]
...
[    8.163400] h3_sensor h3-sensor-a: [27894 us] mclk on (19200000 Hz)
```

(`g_atomic.sh`는 `dmesg`에서 `vaux on`~`mclk on` 구간만 뽑는다. 콜 트레이스는 핵심 줄만 남기고 `...`로 줄였다. 주소는 실행마다 다르다.) 출력에서 볼 것:

- 콜 트레이스의 `clk_core_prepare`가 **네 번** 겹쳐 있다. gate_a → mux → pll_div → pll로 부모를 따라 재귀한 흔적이다. 다섯 번째(osc24m)는 이미 prepare된 상태라 더 내려가지 않았다.
- 잡힌 락 목록 `#1 h3s_lock`(스핀락) 아래에서 `#2 prepare_lock`(mutex)을 잡았고, 그 안에서 잠들었다. CCF는 `prepare_lock`을 먼저 `mutex_trylock`으로 시도해서 이번엔 경고 없이 잡혔지만, PLL의 락 대기가 결국 잠들었다.
- 일반 커널(`Image`)에서는 이 경고가 안 날 수도 있다(`DEBUG_ATOMIC_SLEEP` 꺼짐). **그래서 bring-up은 디버그 설정 커널로 한 번 돌려 보는 게 좋다.** 원자 문맥에서 필요한 것은 `clk_enable()`뿐이고, `clk_prepare()`는 probe에서 미리 해 둔다.

### 2.9 "Disabling unused clocks" — 부팅 끝의 대청소

부트로더는 자기가 쓴 클럭(UART, eMMC, 디스플레이…)을 켠 채 커널에 넘긴다. CCF는 **`late_initcall_sync` 단계에서 한 번** `clk_disable_unused()`를 돌려, count가 0인데 하드웨어상 켜져 있는(`.is_enabled` = 참) 클럭을 끄고, prepare count가 0인데 prepared인 클럭을 unprepare한다. 로그는 `clk: Disabling unused clocks` 한 줄이다. 커널 인자 `clk_ignore_unused`를 주면 이 단계를 건너뛰고 `clk: Not disabling unused clocks`를 찍는다.

#### 예제 10 — 정리 시점과 모듈 provider

```sh
#!/bin/sh
# g_unused.sh — 부팅 직후와 30초 뒤: "안 쓰는" 클럭·레귤레이터 정리
insmod /h3_providers.ko; insmod /h3_sensor.ko
snap() { echo "--- t=$(cut -d' ' -f1 /proc/uptime)s $1"
  grep -E "h3_gate_dbg|h3_gate_a " /sys/kernel/debug/clk/clk_summary | cut -c1-110
  grep -E "h3_spare" /sys/kernel/debug/regulator/regulator_summary
  grep -E "gpio-(5|6) " /sys/kernel/debug/gpio; }
snap "after insmod"
while [ $(cut -d. -f1 /proc/uptime) -lt 36 ]; do sleep 1; done
snap "after 30 s"
echo "=== dmesg ==="; dmesg | grep -E "clk:|regulator:|disabling|unused"
```

```sh
sh /Users/donh/.elinux_scratch/H3/boot.sh h3_ok sh~/g_unused.sh
sh /Users/donh/.elinux_scratch/H3/boot.sh h3_ok sh~/g_unused.sh "clk_ignore_unused regulator_ignore_unused"
```

```text
lab initramfs: 6.18.55 up 0.97s
--- t=1.10s after insmod
       h3_gate_dbg                   0       0        0        12000000    0          0     50000      Y      
             h3_gate_a               1       1        0        19200000    0          0     50000      Y      
 h3_spare_1v2                     0    0      0 unknown  1200mV     0mA  1200mV  1200mV 
 gpio-5   (                    |h3-vaux             ) out hi 
 gpio-6   (                    |h3-spare            ) out hi 
--- t=36.27s after 30 s
       h3_gate_dbg                   0       0        0        12000000    0          0     50000      Y      
             h3_gate_a               1       1        0        19200000    0          0     50000      Y      
 h3_spare_1v2                     0    0      0 unknown  1200mV     0mA  1200mV  1200mV 
 gpio-5   (                    |h3-vaux             ) out hi 
 gpio-6   (                    |h3-spare            ) out lo 
=== dmesg ===
[    0.000000] Kernel command line: console=ttyAMA0 quiet  LAB_CMD=sh~/g_unused.sh
[    0.000000] Unknown kernel command line parameters "LAB_CMD=sh~/g_unused.sh", will be passed to user space.
[    0.865527] clk: Disabling unused clocks
[    0.865697] PM: genpd: Disabling unused power domains
[    0.914876] Freeing unused kernel memory: 3264K
[    0.916033]     LAB_CMD=sh~/g_unused.sh
[   31.741116] h3_spare_1v2: disabling
```

두 번째 실행(`clk_ignore_unused regulator_ignore_unused`)의 다른 부분만 옮긴다.

```text
--- t=37.02s after 30 s
...
 gpio-6   (                    |h3-spare            ) out hi 
=== dmesg ===
[    0.000000] Kernel command line: console=ttyAMA0 quiet clk_ignore_unused regulator_ignore_unused LAB_CMD=sh~/g_unused.sh
...
[    0.627472] clk: Not disabling unused clocks
...
[   31.786697] regulator: Not disabling unused regulators
```

(두 번째 실행에서 첫 실행과 같은 줄은 `...`로 생략했다.) 출력에서 볼 것:

- 클럭 정리는 **0.87 s**, 즉 initramfs의 `/init`이 돌기도 전에 끝났다. 우리 provider 모듈은 그 뒤에 로드되므로 `h3_gate_dbg`(count 0, HW `Y`)는 30초가 지나도 **계속 켜져 있다.** CCF의 정리는 부팅 때 한 번뿐이고 나중에 로드된 모듈 provider의 클럭은 정리하지 않는다. 실제 SoC 클럭 드라이버가 거의 다 built-in(`=y`)인 이유 중 하나다.
- 레귤레이터 쪽 정리는 다르다. `late_initcall_sync`에서 **30초 뒤로 예약**한다(유저스페이스가 모듈을 로드할 시간을 준다 — `regulator_init_complete()`의 주석 그대로). 그래서 모듈 로드 전부터 켜져 있던 `h3_spare_1v2`(`regulator-boot-on`, consumer 없음)가 31.7 s에 `disabling`되고 GPIO 6이 `lo`가 됐다. §4.7에서 다시 본다.
- 두 번째 실행에서는 둘 다 건너뛰었다. **`clk_ignore_unused`는 디버그용이다.** 이걸 켜야 보드가 동작한다면 어떤 드라이버가 자기 클럭을 DT에 안 적었거나 get을 안 한 것이다. 제품 커널 인자에 남기면 전력이 샌다.

정리 대상에서 빼는 정당한 방법:

| 방법 | 어디서 | 의미 |
|---|---|---|
| `CLK_IS_CRITICAL` | provider가 등록할 때 플래그 | "절대 끄지 마라" (DDR, CPU, 인터커넥트 클럭). 등록 즉시 prepare+enable되고 count가 0으로 안 내려간다 |
| `CLK_IGNORE_UNUSED` | provider 플래그 | "unused 정리 때만 건너뛰어라". 쓰다가 끄는 건 허용 |
| `protected-clocks = <...>` | DT의 클럭 컨트롤러 노드 | "이 클럭은 보안 펌웨어 소유라 리눅스가 건드리면 안 된다". 범용 CCF 기능이 아니라 일부 provider가 읽는다(6.18 기준 `drivers/clk/qcom/common.c` 등) |
| consumer가 DT에 `clocks`를 적고 get | consumer | 가장 올바른 방법 |

### 2.10 함정

- `clk_get_rate()`가 0이면 그 클럭은 **고아(orphan)**일 수 있다: 부모 이름이 틀렸거나 부모 provider가 아직 등록 전. `clk_summary`에서 트리 밖 맨 아래에 따로 나온다.
- `devm_clk_get()`이 `-EPROBE_DEFER`면 provider가 아직 없다. `dev_err_probe()`로 그대로 반환한다(D4, G3).
- 같은 레지스터를 쓰는 gate/divider/mux에 **서로 다른 락**을 주면 RMW 경합으로 비트가 날아간다.
- `clk_set_rate()`의 반환값 0은 "요청을 처리했다"이지 "정확히 그 rate다"가 아니다. 항상 `clk_get_rate()`로 확인한다.
- 원자 문맥에서 `clk_prepare*()`/`clk_set_rate()`/`clk_get()` 금지. 원자 문맥에서 되는 건 `clk_enable()`/`clk_disable()`뿐.

---

## 3. Reset controller

### 3.1 직관과 정의

SoC 안의 IP 블록(USB PHY, 카메라 인터페이스, I2C 컨트롤러…)은 각자 리셋 입력을 갖고, 그 리셋 비트들을 모아 둔 블록이 **리셋 컨트롤러**다(i.MX의 SRC, Rockchip CRU의 softrst 레지스터, Allwinner의 CCU 리셋 비트). 보드 수준에서는 "외부 칩의 RESET_N 핀에 연결된 GPIO"도 리셋 라인이다. reset 프레임워크는 이 둘을 같은 API로 다룬다.

| API | 의미 |
|---|---|
| `devm_reset_control_get_exclusive(dev, id)` | 이 라인을 **나 혼자** 쓴다. 다른 장치가 같은 라인을 exclusive로 요청하면 실패 |
| `devm_reset_control_get_shared(dev, id)` | 여러 장치가 **같이** 쓴다. deassert/assert에 refcount 적용 |
| `..._optional_...` 변형 | DT에 `resets`가 없으면 NULL (NULL에 대한 호출은 no-op) |
| `devm_reset_control_get_exclusive_deasserted()` / `..._shared_deasserted()` | get + deassert, 해제 때 assert까지 자동 (6.13 이후) |
| `reset_control_assert()` / `reset_control_deassert()` | 리셋 걸기 / 풀기 |
| `reset_control_reset()` | 컨트롤러가 지원하면 펄스 한 번 (assert → 짧은 대기 → deassert를 하드웨어가) |
| `reset_control_status()` | 지금 assert 상태인가 |

`id`는 DT `reset-names`의 이름이다. 이 노트처럼 라인이 하나면 NULL을 줘도 된다.

**shared의 의미가 핵심이다.** shared 라인은 "누군가 한 명이라도 deassert를 원하면 풀려 있다"는 규칙을 따른다. 커널은 라인마다 `deassert_count`를 세고, 0 → 1이 될 때만 provider의 `.deassert`를, 1 → 0이 될 때만 `.assert`를 부른다. 펌웨어 비유: 여러 태스크가 같은 전원 도메인을 쓸 때 쓰던 "power vote" 카운터다.

shared 라인의 `reset_control_reset()`은 더 특이하다. **처음 한 번만 실제 펄스를 내고**(`triggered_count`), 그다음 consumer들의 호출은 아무것도 안 한다. 다시 펄스를 낼 수 있게 하려면 모든 consumer가 `reset_control_rearm()`을 불러야 한다. 이유: consumer B가 동작 중인데 consumer A가 펄스를 내면 B가 망가지기 때문이다.

### 3.2 예제 11 — provider: `reset_controller_dev` (`h3_providers` 4/6)

무엇을 확인하나: ops 세 개(assert, deassert, status)와 등록. `.deassert`가 불린 시각을 칩 모델이 쓰도록 기록하고, **ops가 실제로 불릴 때마다** `hw:` 로그를 찍는다 — 이 로그가 "refcount 때문에 하드웨어가 안 바뀐" 순간을 보여 준다.

```c
/* h3_providers (4/6): 리셋 컨트롤러 ops */
static int h3_rst_update(struct reset_controller_dev *rc, unsigned long id, bool on)
{
	unsigned long flags;
	u32 v;

	spin_lock_irqsave(&h3_lock, flags);
	v = readl(REG(R_RST));
	writel(on ? v | BIT(id) : v & ~BIT(id), REG(R_RST));
	if (!on)
		rst_rel[id] = ktime_get();
	spin_unlock_irqrestore(&h3_lock, flags);
	dev_info(rc->dev, "hw: reset line %lu %s\n", id, on ? "ASSERT" : "DEASSERT");
	return 0;
}
static int h3_rst_assert(struct reset_controller_dev *rc, unsigned long id)
{ return h3_rst_update(rc, id, true); }
static int h3_rst_deassert(struct reset_controller_dev *rc, unsigned long id)
{ return h3_rst_update(rc, id, false); }
static int h3_rst_status(struct reset_controller_dev *rc, unsigned long id)
{ return !!(readl(REG(R_RST)) & BIT(id)); }

static const struct reset_control_ops h3_rst_ops = {
	.assert = h3_rst_assert, .deassert = h3_rst_deassert, .status = h3_rst_status,
};
static struct reset_controller_dev h3_rcdev = {
	.ops = &h3_rst_ops, .owner = THIS_MODULE, .nr_resets = 4,
};
```

등록은 `h3_cru_probe()`(§4.3의 6/6 조각)에서 한다: `of_node`, `of_reset_n_cells = 1`을 채우고 `devm_reset_controller_register()`. `of_xlate`를 비워 두면 리셋 코어가 범용 `of_reset_simple_xlate`(인자 셀 하나 = 라인 번호, `nr_resets` 범위 검사)를 쓴다. 비트 하나가 리셋 하나인 단순한 컨트롤러라면 직접 쓸 필요도 없다 — 커널에 `reset-simple` 드라이버(`drivers/reset/reset-simple.c`)가 있어 DT만으로 된다.

### 3.3 예제 12 — 공유 리셋과 공유 레일의 refcount

무엇을 확인하나: 센서 a, b가 같은 리셋 라인 0과 같은 LDO 두 개를 shared로 쓴다. sysfs `unbind`로 a를 먼저 떼고, 그다음 b를 뗀다. 레지스터와 칩 응답이 언제 바뀌는지 본다.

```sh
#!/bin/sh
# g_shared.sh — 공유 리셋/레귤레이터 refcount: a를 떼고, b를 떼고
insmod /h3_providers.ko; insmod /h3_sensor.ko $H3_ARGS; sleep 1
D=/sys/bus/platform/drivers/h3_sensor
show() { echo "--- $1"; grep -E "RST|GATE|LDO_EN" /sys/kernel/debug/h3_hw/regs | tr '\n' ' '; echo
  for d in /sys/bus/platform/devices/h3-sensor-*; do [ -e $d/whoami ] && echo "$(basename $d) whoami: $(cat $d/whoami)"; done
  grep -E "h3_vdd |h3_vddio " /sys/kernel/debug/regulator/regulator_summary
  grep -E "h3_mux|h3_pll " /sys/kernel/debug/clk/clk_summary | cut -c1-60; }
show "both bound"
dmesg -c > /dev/null
echo h3-sensor-a > $D/unbind; dmesg -c | grep -E "h3|hw:"; show "after unbind a"
echo h3-sensor-b > $D/unbind; dmesg -c | grep -E "h3|hw:"; show "after unbind b"
```

```sh
sh /Users/donh/.elinux_scratch/H3/boot.sh h3_ok sh~/g_shared.sh
```

```text
lab initramfs: 6.18.55 up 0.66s
[    0.788713] h3_vdd: unsupportable voltage range: 3600000-3300000uV
[    0.808347] h3_vdd: unsupportable voltage range: 3600000-3300000uV
--- both bound
GATE = 0x00000083 RST = 0x0000000e LDO_EN = 0x00000003 
h3-sensor-a whoami: 0x6c
h3-sensor-b whoami: 0x6c
 h3_vddio                         2    2      0 unknown  1800mV     0mA  1800mV  3300mV 
 h3_vdd                           2    2      0 unknown  2800mV     0mA  2800mV  3300mV 
    h3_pll                           1       1        0     
          h3_mux                     2       2        0     
[    1.928028] h3_sensor h3-sensor-a: [  309 us] off: reset asserted
[    1.928116] h3_sensor h3-sensor-a: [  411 us] off: mclk off
[    1.928304] h3_sensor h3-sensor-a: [  600 us] off: vdd off
[    1.928323] h3_sensor h3-sensor-a: [  620 us] off: vddio off
--- after unbind a
GATE = 0x00000082 RST = 0x0000000e LDO_EN = 0x00000003 
h3-sensor-b whoami: 0x6c
 h3_vddio                         1    1      0 unknown  1800mV     0mA  1800mV  3300mV 
 h3_vdd                           1    1      0 unknown  2800mV     0mA  2800mV  3300mV 
    h3_pll                           1       1        0     
          h3_mux                     1       1        0     
[    1.984284] h3_cru h3-cru: hw: reset line 0 ASSERT
[    1.984331] h3_sensor h3-sensor-b: [   99 us] off: reset asserted
[    1.984365] h3_sensor h3-sensor-b: [  135 us] off: mclk off
[    1.984411] regulator regulator.4: hw: ldo2 OFF
[    1.984438] h3_sensor h3-sensor-b: [  208 us] off: vdd off
[    1.984446] regulator regulator.3: hw: ldo1 OFF
[    1.984452] h3_sensor h3-sensor-b: [  222 us] off: vddio off
--- after unbind b
GATE = 0x00000080 RST = 0x0000000f LDO_EN = 0x00000000 
 h3_vddio                         0    0      0 unknown  1800mV     0mA  1800mV  3300mV 
 h3_vdd                           0    0      0 unknown  2800mV     0mA  2800mV  3300mV 
    h3_pll                           0       0        0     
          h3_mux                     0       0        0     
```

출력에서 볼 것:

- a를 뗀 뒤: `off: reset asserted`를 찍었지만 그 앞에 `hw:` 줄이 하나도 없다 — `RST = 0x0e`(bit 0 = 0, 즉 여전히 해제), LDO OFF도 없다. `GATE`는 bit 0만 꺼졌고(0x83 → 0x82), `LDO_EN = 3`(둘 다 켜짐), 레귤레이터 use는 2 → 1, mux count는 2 → 1. **센서 b는 여전히 `0x6c`로 응답한다.**
- b를 뗀 뒤: 비로소 `hw: reset line 0 ASSERT`, `hw: ldo2 OFF`, `hw: ldo1 OFF`가 찍히고 `RST = 0x0f`, `LDO_EN = 0`, PLL count 0.
- 세 프레임워크가 **똑같은 규칙**(마지막 사용자가 놓을 때만 하드웨어를 바꾼다)으로 움직였다.
- off 구간의 us 값에는 TCG 에뮬레이션과 printk 비용이 섞여 있다(다른 실행에서는 `mclk off`가 ms 단위로 튀기도 했다).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<rect x="10" y="10" width="320" height="205" rx="8" fill="#3f9a6b" fill-opacity="0.08" stroke="#3f9a6b"/>
<text x="170" y="32" text-anchor="middle" font-size="13">shared — deassert_count</text>
<text x="22" y="58" font-size="12">사건</text>
<text x="150" y="58" font-size="12">count</text>
<text x="222" y="58" font-size="12">하드웨어</text>
<line x1="20" y1="66" x2="320" y2="66" stroke="currentColor" stroke-opacity="0.4"/>
<text x="22" y="90" font-size="12">a deassert</text>
<text x="150" y="90" font-size="12">0 → 1</text>
<text x="222" y="90" font-size="12">HW 해제</text>
<text x="22" y="120" font-size="12">b deassert</text>
<text x="150" y="120" font-size="12">1 → 2</text>
<text x="222" y="120" font-size="12">변화 없음</text>
<text x="22" y="150" font-size="12">a assert (unbind)</text>
<text x="150" y="150" font-size="12">2 → 1</text>
<text x="222" y="150" font-size="12">변화 없음</text>
<text x="22" y="180" font-size="12">b assert (unbind)</text>
<text x="150" y="180" font-size="12">1 → 0</text>
<text x="222" y="180" font-size="12">HW 리셋</text>
<rect x="216" y="76" width="104" height="20" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<rect x="216" y="166" width="104" height="20" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<rect x="350" y="10" width="320" height="205" rx="8" fill="#e08a3c" fill-opacity="0.08" stroke="#e08a3c"/>
<text x="510" y="32" text-anchor="middle" font-size="13">exclusive — 소유권</text>
<text x="362" y="58" font-size="12">사건</text>
<text x="490" y="58" font-size="12">결과</text>
<line x1="360" y1="66" x2="660" y2="66" stroke="currentColor" stroke-opacity="0.4"/>
<text x="362" y="90" font-size="12">a get_exclusive</text>
<text x="490" y="90" font-size="12">OK, a가 소유</text>
<text x="362" y="120" font-size="12">b get_exclusive</text>
<text x="490" y="120" font-size="12">WARN + -EBUSY</text>
<text x="362" y="150" font-size="12">a assert/deassert</text>
<text x="490" y="150" font-size="12">즉시 하드웨어 반영</text>
<text x="362" y="180" font-size="12">a reset (펄스)</text>
<text x="490" y="180" font-size="12">매번 펄스</text>
<rect x="484" y="106" width="110" height="20" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<rect x="10" y="230" width="660" height="60" rx="8" fill="#888" fill-opacity="0.08" stroke="#888"/>
<text x="340" y="254" text-anchor="middle" font-size="12">regulator use_count와 clk enable/prepare count도 shared와 같은 규칙:</text>
<text x="340" y="276" text-anchor="middle" font-size="12">0 → 1에서만 켜고, 1 → 0에서만 끈다 (예제 12의 LDO_EN, GATE, mux count)</text>
</svg>
```
그림 3 — refcount 의미. 왼쪽은 예제 12에서 실제로 본 순서, 오른쪽은 예제 13의 결과다.

### 3.4 예제 13 — 같은 라인을 exclusive로 두 번 요청하면

무엇을 확인하나: `excl=1`로 두 센서가 같은 라인을 exclusive로 요청한다.

```sh
#!/bin/sh
# g_probe.sh — 로드 후 h3 로그 + whoami + devices_deferred만
insmod /h3_providers.ko; insmod /h3_sensor.ko $H3_ARGS; sleep 1
echo "=== dmesg (h3) ==="; dmesg | grep -E "h3|hw:|dummy regulator|WARNING|Call trace|reset" | grep -v "loading out-of-tree\|Bringing"
for d in /sys/bus/platform/devices/h3-sensor-*; do [ -e $d/whoami ] && echo "$(basename $d) whoami: $(cat $d/whoami)"; done
echo "=== clk ==="; grep -E "h3_(pll|pll_div|mux|osc24m|gate_a|gate_b) " /sys/kernel/debug/clk/clk_summary | cut -c1-75
[ -n "$H3_REG" ] && cat /sys/kernel/debug/regulator/regulator_summary
true
```

```sh
sh /Users/donh/.elinux_scratch/H3/boot.sh h3_ok H3_ARGS=excl=1~sh~/g_probe.sh
```

```text
[    0.767086] h3_sensor h3-sensor-a: [17760 us] WHO_AM_I = 0x6c -> probe OK
[    0.767984] WARNING: CPU: 0 PID: 65 at drivers/reset/core.c:799 __reset_control_get_internal+0x104/0x1e0
[    0.768496] Modules linked in: h3_sensor(O+) h3_providers(O)
[    0.769379] pc : __reset_control_get_internal+0x104/0x1e0
[    0.769394] lr : __of_reset_control_get+0x27c/0x2d0
[    0.769718] Call trace:
[    0.769828]  __reset_control_get_internal+0x104/0x1e0 (P)
[    0.769902]  __of_reset_control_get+0x27c/0x2d0
[    0.769927]  __reset_control_get+0x30/0x1d0
[    0.769938]  __devm_reset_control_get+0x74/0x120
[    0.769949]  h3s_probe+0x1c0/0x608 [h3_sensor]
[    0.770401]  h3s_drv_init+0x20/0xfdc [h3_sensor]
[    0.770964] h3_sensor h3-sensor-b: error -EBUSY: reset
[    0.771063] h3_sensor h3-sensor-b: probe with driver h3_sensor failed with error -16
h3-sensor-a whoami: 0x6c
=== clk ===
...
             h3_gate_b               0       0        0        19200000    
             h3_gate_a               1       1        0        19200000    
```

(센서 a의 정상 시퀀스 줄은 예제 4와 같은 패턴이라 마지막 줄만 남겼고, clk 부분은 두 게이트 줄만 남겼다. `gate_b` count 0 — b는 클럭을 켜기 전에 실패했다.) 출력에서 볼 것: 6.18의 `drivers/reset/core.c` 799행은 `WARN_ON(!rstc->shared || !shared)`이다. 같은 라인에 이미 reset_control이 있는데 둘 중 하나라도 exclusive면 **WARN 스택 덤프 + `-EBUSY`**다. 커널은 이걸 "DT나 드라이버 설계 버그"로 본다. 보드에서 이 WARN을 보면: 회로도상 정말 공유 라인인가 확인하고, 맞다면 두 드라이버 모두 shared로 요청해야 한다.

### 3.5 함정

- **공유 리셋으로는 칩을 다시 리셋할 수 없다.** 다른 consumer가 deassert를 들고 있으면 내 assert는 카운트만 내린다. "센서가 이상해지면 리셋해서 복구"라는 에러 복구 경로를 설계했다면, 그 라인이 shared인지부터 확인한다.
- 리셋 극성: provider가 비트 의미(1 = assert인지)를 책임진다. GPIO 리셋은 DT의 `GPIO_ACTIVE_LOW`가 극성을 책임진다(D4 §8, H4). 드라이버에서 극성을 또 뒤집으면 이중 반전.
- `reset-gpios`만 있고 `resets`가 없는 노드에 리셋 API를 쓰면, 6.18 리셋 코어가 `reset-gpio` 보조 장치를 자동으로 만들려고 한다. `CONFIG_RESET_GPIO`가 모듈인데 로드가 안 되면 영원히 defer — D4 예제 10에서 실제로 봤다. 한 핀은 GPIO API와 리셋 API 중 한쪽으로만 다룬다.
- shared 라인에 assert를 deassert보다 먼저 부르면 커널이 WARN한다(카운트가 0 아래로 못 내려간다).

---

## 4. Regulator

### 4.1 직관과 정의

레귤레이터는 **전압(또는 전류)을 만들어 주는 전원 소스**다: PMIC의 buck/LDO, GPIO로 켜는 로드 스위치, 고정 전원까지. regulator 프레임워크는 세 층으로 나뉜다.

- **provider(regulator driver)**: PMIC의 레지스터를 아는 드라이버. "켜라/꺼라/전압 선택자 N을 써라"를 `regulator_ops`로 제공.
- **machine(constraints)**: 이 보드에서 이 레일이 허용하는 범위. DT의 `regulator-min-microvolt`, `regulator-max-microvolt`, `regulator-always-on`, `regulator-boot-on`, `regulator-enable-ramp-delay` 등. **드라이버가 아니라 보드 설계자가 정한다.** 이 범위 밖 요청은 거부된다 — 잘못된 드라이버가 1.8 V 레일에 3.3 V를 거는 사고를 막는 안전장치다.
- **consumer**: 센서 드라이버. "내 vdd 핀 전원을 켜 줘, 1.7~1.95 V 사이로".

| consumer API | 의미 |
|---|---|
| `devm_regulator_get(dev, "vdd")` | DT `vdd-supply`의 레귤레이터 핸들. 속성이 없으면 **dummy**를 준다(§4.6) |
| `devm_regulator_get_optional(dev, "vaux")` | 속성이 없으면 `-ENODEV` (dummy 안 줌) |
| `devm_regulator_bulk_get(dev, n, data)` | 여러 개를 한 번에 (`struct regulator_bulk_data[]`) |
| `devm_regulator_get_enable(dev, "vdd")` | get + enable, 해제 때 disable까지 자동. 핸들은 안 준다 |
| `regulator_enable()` / `regulator_disable()` | consumer별 enable 횟수와 레일의 `use_count`를 올리고 내림. 0 → 1에서만 실제로 켬 |
| `regulator_set_voltage(r, min, max)` | **범위**로 요청. 코어가 모든 consumer 범위와 constraints의 교집합에서 고른다 |
| `regulator_get_voltage(r)` | 현재 전압(uV) |
| `regulator_is_enabled(r)` | 레일이 켜졌나 (내가 켰나가 아니다) |

전압을 **범위로** 요청하는 이유: vddio를 같이 쓰는 센서 두 개가 각각 "1.7~1.95 V", "1.62~1.98 V"를 요청하면 코어가 둘 다 만족하는 값을 고를 수 있다. 정확한 한 값을 요구하면 공유가 불가능해진다.

### 4.2 예제 14 — provider: `regulator_desc` + `regulator_ops` (`h3_providers` 5/6)

무엇을 확인하나: 전압 표 기반 LDO. `list_voltage`와 `map_voltage`는 커널 헬퍼(`regulator_list_voltage_table`, `regulator_map_voltage_ascend`)를 쓰고, 선택자를 레지스터에 쓰고 읽는 두 함수만 직접 쓴다. enable 때 "전원 안정 시각"을 칩 모델용으로 기록한다.

```c
/* h3_providers (5/6): 레귤레이터(PMIC LDO) ops + desc */
#define LDO_RAMP_US 1000	/* 가짜 LDO의 물리적 램프 시간 */
static int ldo_enable(struct regulator_dev *rdev)
{
	int id = rdev_get_id(rdev);

	writel(readl(REG(R_LDO_EN)) | BIT(id), REG(R_LDO_EN));
	ldo_good[id] = ktime_add_us(ktime_get(), LDO_RAMP_US);
	dev_info(&rdev->dev, "hw: ldo%d ON\n", id + 1);
	return 0;
}
static int ldo_disable(struct regulator_dev *rdev)
{
	writel(readl(REG(R_LDO_EN)) & ~BIT(rdev_get_id(rdev)), REG(R_LDO_EN));
	dev_info(&rdev->dev, "hw: ldo%d OFF\n", rdev_get_id(rdev) + 1);
	return 0;
}
static int ldo_is_enabled(struct regulator_dev *rdev)
{ return !!(readl(REG(R_LDO_EN)) & BIT(rdev_get_id(rdev))); }
static int ldo_set_sel(struct regulator_dev *rdev, unsigned int sel)
{
	int sh = 4 * rdev_get_id(rdev);
	u32 v = readl(REG(R_LDO_SEL)) & ~(0xfu << sh);

	writel(v | sel << sh, REG(R_LDO_SEL));
	return 0;
}
static int ldo_get_sel(struct regulator_dev *rdev)
{ return (readl(REG(R_LDO_SEL)) >> (4 * rdev_get_id(rdev))) & 0xf; }
static const unsigned int ldo_volts[] = {
	1200000, 1800000, 2500000, 2800000, 3000000, 3300000 };
static const struct regulator_ops ldo_ops = {
	.enable = ldo_enable, .disable = ldo_disable, .is_enabled = ldo_is_enabled,
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_ascend,
	.set_voltage_sel = ldo_set_sel, .get_voltage_sel = ldo_get_sel,
};
#define LDO(_id, _name) { .name = _name, .of_match = _name, .id = _id, \
	.regulators_node = "regulators", .ops = &ldo_ops, \
	.type = REGULATOR_VOLTAGE, .owner = THIS_MODULE, \
	.volt_table = ldo_volts, .n_voltages = ARRAY_SIZE(ldo_volts) }
static const struct regulator_desc ldo_desc[] = { LDO(0, "ldo1"), LDO(1, "ldo2") };
```

`of_match = "ldo1"`, `regulators_node = "regulators"`가 핵심이다. 레귤레이터 코어가 `h3-pmic` 노드 아래 `regulators/ldo1`을 찾아 **constraints를 DT에서 자동으로 읽는다**. 드라이버는 `of_get_regulator_init_data()`를 직접 부를 필요가 없다.

### 4.3 예제 15 — provider 등록과 모듈 본체 (`h3_providers` 6/6)

```c
/* h3_providers (6/6): probe 두 개 + debugfs + 모듈 등록 */
static struct debugfs_reg32 h3_dregs[] = {
	{ "PLL", 0 }, { "DIV", 4 }, { "MUX", 8 }, { "GATE", 12 },
	{ "RST", 16 }, { "LDO_EN", 20 }, { "LDO_SEL", 24 } };
static struct debugfs_regset32 h3_regset = {
	.regs = h3_dregs, .nregs = ARRAY_SIZE(h3_dregs), .base = REG(0) };
static struct dentry *h3_dir;
static int h3_cru_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	h3_clks = devm_kzalloc(dev, struct_size(h3_clks, hws, 8), GFP_KERNEL);
	if (!h3_clks)
		return -ENOMEM;
	ret = h3_clk_register(dev);
	if (ret)
		return dev_err_probe(dev, ret, "clk register\n");
	h3_rcdev.dev = dev;
	h3_rcdev.of_node = dev->of_node;
	h3_rcdev.of_reset_n_cells = 1;
	return devm_reset_controller_register(dev, &h3_rcdev);
}

static int h3_pmic_probe(struct platform_device *pdev)
{
	struct regulator_config cfg = { .dev = &pdev->dev };
	int i;

	for (i = 0; i < ARRAY_SIZE(ldo_desc); i++) {
		struct regulator_dev *r = devm_regulator_register(&pdev->dev, &ldo_desc[i], &cfg);

		if (IS_ERR(r))
			return dev_err_probe(&pdev->dev, PTR_ERR(r), "ldo%d\n", i + 1);
	}
	return 0;
}
static const struct of_device_id cru_ids[] = { { .compatible = "dons,h3-cru" }, { } };
static const struct of_device_id pmic_ids[] = { { .compatible = "dons,h3-pmic" }, { } };
MODULE_DEVICE_TABLE(of, cru_ids);
static struct platform_driver h3_drvs[] = {
	{ .probe = h3_cru_probe, .driver = { .name = "h3_cru", .of_match_table = cru_ids } },
	{ .probe = h3_pmic_probe, .driver = { .name = "h3_pmic", .of_match_table = pmic_ids } },
};
static struct platform_driver * const h3_drv_ptrs[] = { &h3_drvs[0], &h3_drvs[1] };
static int __init h3p_init(void)
{
	h3_dir = debugfs_create_dir("h3_hw", NULL);
	debugfs_create_regset32("regs", 0444, h3_dir, &h3_regset);
	return platform_register_drivers(h3_drv_ptrs, ARRAY_SIZE(h3_drv_ptrs));
}
static void __exit h3p_exit(void)
{
	platform_unregister_drivers(h3_drv_ptrs, ARRAY_SIZE(h3_drv_ptrs));
	debugfs_remove(h3_dir);
}
module_init(h3p_init);
module_exit(h3p_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("H3 demo: fake clk/reset/regulator providers");
```

요점: 한 모듈에 platform driver 두 개(`h3_cru`, `h3_pmic`)를 `platform_register_drivers()`로 등록했다. 진짜 PMIC는 I2C 장치라서 `i2c_driver`의 probe에서 regmap(H2)을 만들고 `regulator_config.regmap`에 넣는다. 그러면 `regulator_enable_regmap`, `regulator_set_voltage_sel_regmap` 같은 regmap 헬퍼가 ops를 대신해서 provider 코드가 거의 표(table)만 남는다. debugfs `regset32`는 레지스터 덤프를 공짜로 준다 — bring-up 때 provider에 꼭 넣는다.

### 4.4 예제 16 — `regulator_summary` 읽기, 그리고 constraints

예제 4와 같은 실행의 debugfs 부분이다.

```text
=== regulator_summary ===
 regulator                      use open bypass  opmode voltage current     min     max
---------------------------------------------------------------------------------------
 regulator-dummy                  1    0      0 unknown     0mV     0mA     0mV     0mV 
 h3_spare_1v2                     0    0      0 unknown  1200mV     0mA  1200mV  1200mV 
 h3_vaux_3v3                      1    1      0 unknown  3300mV     0mA  3300mV  3300mV 
    h3-sensor-a-vaux              1                                 0mA     0mV     0mV
 h3_vddio                         2    2      0 unknown  1800mV     0mA  1800mV  3300mV 
    h3-sensor-b-vddio             1                                 0mA  1800mV  1950mV
    h3-sensor-a-vddio             1                                 0mA  1800mV  1950mV
 h3_vdd                           2    2      0 unknown  2800mV     0mA  2800mV  3300mV 
    h3-sensor-b-vdd               1                                 0mA     0mV     0mV
    h3-sensor-a-vdd               1                                 0mA     0mV     0mV
=== h3_hw regs ===
...
LDO_EN = 0x00000003
LDO_SEL = 0x00000031
```

출력에서 볼 것:

- `use`는 레일의 `use_count`(enable한 consumer 수), `open`은 핸들 수. 들여쓴 줄이 consumer(`장치이름-supply이름`)이고, 그 숫자는 그 consumer의 enable 횟수다.
- 레일 줄의 min/max는 **constraints**(DT)이고, consumer 줄의 min/max는 **그 consumer가 요청한 범위**다. `h3-sensor-a-vddio`의 1800~1950 mV는 드라이버의 `regulator_set_voltage(..., 1700000, 1950000)`가 constraints(1.8 V 이상)에 잘려 저장된 값이다. vdd consumer는 전압 요청을 안 해서 0이다.
- `LDO_SEL = 0x31`: ldo1 선택자 1(표의 1.8 V), ldo2 선택자 3(2.8 V). 부팅 초기값 0(1.2 V)은 constraints 밖이라, 등록 순간 코어가 `h3_vddio: Bringing 1200000uV into 1800000-1800000uV`(최솟값으로 맞춤)를 찍고 바꿨다. **PMIC 기본 전압이 보드 설계와 다르면 레귤레이터 등록 순간 전압이 바뀐다** — bring-up에서 레일 전압이 부팅 중간에 튀는 걸 scope로 봤다면 이 줄을 찾는다.
- 예제 4의 `vdd set_voltage(3.6 V) = -22`와 `h3_vdd: unsupportable voltage range: 3600000-3300000uV`: constraints 최대 3.3 V를 넘는 요청은 `-EINVAL`로 거부되고, 하드웨어는 건드리지 않는다.
- `h3_vddio` 램프: `regulator-enable-ramp-delay = <1000>`를 줬으므로 코어가 `.enable` 호출 뒤 1 ms를 기다린 다음 `regulator_enable()`에서 돌아온다. 예제 4에서 `hw: ldo1 ON`(1.607424) → `vddio on`(1.610274) 사이가 약 2.9 ms인 것은 1 ms 대기 + TCG 에뮬레이션의 타이머 오차다.

### 4.5 고정 레귤레이터 — GPIO enable과 `startup-delay-us`

`regulator-fixed`(커널 내장 `drivers/regulator/fixed.c`)는 "전압을 못 바꾸는, GPIO 하나로 켜고 끄는 레일"이다. 보드의 로드 스위치, LDO의 EN 핀이 SoC GPIO에 물린 경우가 전부 이것이다.

| DT 속성 | 의미 |
|---|---|
| `gpio = <&gpio 5 GPIO_ACTIVE_HIGH>` | enable 핀 |
| `enable-active-high` | 이 속성이 없으면 active-low로 본다 (역사적 규칙. GPIO 플래그와 별개라 헷갈린다) |
| `startup-delay-us` | 켠 뒤 안정될 때까지 대기. 코어가 `desc.enable_time`으로 쓴다 |
| `off-on-delay-us` | 끈 뒤 다시 켜기까지 최소 시간 (방전 대기) |
| `regulator-boot-on` | 부트로더가 켜 둔 상태로 시작. GPIO를 high로 잡는다 |
| `vin-supply` | 이 스위치의 입력 레일 (부모 레귤레이터) |

예제 4에서 `vaux on`까지 약 4.6 ms가 걸렸다(`startup-delay-us = <3000>` + 에뮬레이션 오차). 예제 10의 `debug/gpio`에서 `gpio-5 (|h3-vaux) out hi`, `gpio-6 (|h3-spare) out hi`로 두 고정 레귤레이터가 PL061 GPIO를 잡고 있는 것도 보였다.

### 4.6 예제 17 — supply가 DT에 없으면: dummy regulator와 "켜진 줄 아는" 드라이버

무엇을 확인하나: `-DH3_NO_VDD`로 두 센서의 `vdd-supply`를 지운다. 회로도에는 vdd가 LDO2에 물려 있는데 DT 작성자가 빠뜨린 상황이다.

```sh
sh /Users/donh/.elinux_scratch/H3/mkdtb.sh h3_novdd -DH3_NO_VDD
sh /Users/donh/.elinux_scratch/H3/boot.sh h3_novdd H3_REG=1~sh~/g_probe.sh
```

```text
[    2.436705] h3_sensor h3-sensor-a: supply vdd not found, using dummy regulator
[    2.439407] regulator regulator.3: hw: ldo1 ON
[    2.450808] h3_sensor h3-sensor-a: [11538 us] vddio on
[    2.450873] h3_sensor h3-sensor-a: [11622 us] vdd on
[    2.454699] h3_sensor h3-sensor-a: [15419 us] vaux on
[    2.455633] h3_sensor h3-sensor-a: [16320 us] mclk on (19200000 Hz)
[    2.457034] h3_sensor h3-sensor-a: [17762 us] waited tPWR-RST
[    2.457288] h3_cru h3-cru: hw: reset line 0 DEASSERT
[    2.457354] h3_sensor h3-sensor-a: [18098 us] reset released
[    2.462743] h3_sensor h3-sensor-a: [23443 us] waited tBOOT
[    2.463142] h3 chip0 model: NACK - no power (vdd/vddio not up) (mclk=19200000)
[    2.463315] h3_sensor h3-sensor-a: error -ENODEV: chip not responding on the bus
[    2.463883] h3_cru h3-cru: hw: reset line 0 ASSERT
[    2.463953] h3_sensor h3-sensor-a: [  205 us] off: reset asserted
[    2.464182] h3_sensor h3-sensor-a: [  415 us] off: mclk off
[    2.464567] h3_sensor h3-sensor-a: [  821 us] off: vdd off
[    2.464648] regulator regulator.3: hw: ldo1 OFF
[    2.464714] h3_sensor h3-sensor-a: [  969 us] off: vddio off
...
 regulator                      use open bypass  opmode voltage current     min     max
---------------------------------------------------------------------------------------
 regulator-dummy                  1    0      0 unknown     0mV     0mA     0mV     0mV 
 h3_spare_1v2                     0    0      0 unknown  1200mV     0mA  1200mV  1200mV 
 h3_vaux_3v3                      0    0      0 unknown  3300mV     0mA  3300mV  3300mV 
 h3_vddio                         0    0      0 unknown  1800mV     0mA  1800mV  3300mV 
 h3_vdd                           0    0      0 unknown  2800mV     0mA  2800mV  3300mV 
```

(`set_voltage` 관련 줄과 센서 b 부분 — a와 같은 패턴 — 은 생략했다.) 출력에서 볼 것: 드라이버 입장에서는 **모든 단계가 성공**했다. `vdd on`이 84 us만에 끝났는데, dummy regulator의 enable은 아무것도 안 하기 때문이다. `hw: ldo2 ON` 줄이 끝까지 없다. 그런데 칩은 전원이 없어 NACK. 진짜 보드라면 로그에는 경고 한 줄과 "I2C read failed -ENXIO/-EREMOTEIO"만 남는다. D4 §7.3에서 본 dummy의 위험이 **실제 증상으로** 이어진 장면이다. bring-up 중 `using dummy regulator`를 보면 무시하지 말고 회로도와 DT를 대조한다.

### 4.7 unused 레귤레이터 정리 — 30초 뒤

§2.9 예제 10에서 봤다. `regulator_init_complete()`는 `late_initcall_sync`에서 30초 뒤 작업을 예약하고, 그때 **always-on이 아니고, 상태 변경 권한(`REGULATOR_CHANGE_STATUS`)이 있고, use_count가 0인데 켜져 있는** 레일을 `disabling`과 함께 끈다. DT로 constraints를 준 레일은 `regulator-always-on`이 없으면 이 권한이 자동으로 붙는다(`of_regulator.c`). 클럭과 달리 모듈로 로드된 provider의 레일도 30초 안에 등록되면 대상이 된다. 그 레일을 쓰는 consumer 드라이버가 30초 안에 enable하지 않으면(모듈이 늦게 로드되면) 레일이 한 번 꺼졌다 켜진다 — "부팅 30초쯤에 센서가 한 번 죽는다"의 원인 후보다.

### 4.8 bulk API의 함정 — 순서가 보장되지 않는다

`regulator_bulk_enable()`은 편하지만, 6.18 구현(`drivers/regulator/core.c`)은 각 레귤레이터를 **`async_schedule_domain()`으로 병렬로** 켠다. 램프 시간을 겹쳐서 부팅을 빠르게 하려는 설계다. 그래서 "vddio 먼저, 그다음 vdd"처럼 데이터시트가 순서를 요구하면 bulk enable을 쓰면 안 된다. 이 노트의 센서는 **get만 bulk**(`devm_regulator_bulk_get`)로 하고, enable은 하나씩 순서대로 부른다(§5.2). 끌 때 `regulator_bulk_disable()`은 역순으로 하나씩 끈다.

### 4.9 함정

- `regulator_enable()`/`regulator_disable()` 짝이 안 맞으면 `unbalanced disables` WARN이 난다. probe 실패 경로와 remove에서 짝을 맞춘다 — `devm_add_action_or_reset()`이 깔끔하다.
- `regulator_is_enabled()`가 참이어도 **내가** 켠 게 아닐 수 있다(다른 consumer, boot-on). 내가 켰다는 사실은 드라이버가 기억한다.
- `regulator-always-on` 레일은 consumer가 disable해도 안 꺼진다. 전력 측정이 안 맞으면 summary에서 이 레일부터 본다.
- PMIC provider가 I2C 위에 있으면, PMIC I2C 버스가 준비되기 전 consumer probe는 `-EPROBE_DEFER`다. 정상이다. `devices_deferred`로 확인한다(D4).

---

## 5. 전원 시퀀스 — 센서를 깨우는 순서

### 5.1 데이터시트 타이밍을 코드로

센서 데이터시트의 "Power-up sequence" 그림은 대개 이렇다(이 노트의 가짜 센서 기준, 실제 칩마다 다르다).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="12" y="52" font-size="12">VDDIO 1.8V</text>
<path d="M130,60 L150,60 L180,36 L650,36" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<text x="12" y="102" font-size="12">VDD 2.8V</text>
<path d="M130,110 L200,110 L230,86 L650,86" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<text x="12" y="152" font-size="12">MCLK</text>
<path d="M130,155 L262,155 L262,138 L270,138 L270,155 L278,155 L278,138 L286,138 L286,155 L294,155 L294,138 L302,138 L302,155 L310,155 L310,138 L318,138 L318,155 L326,155 L326,138 L334,138 L334,155 L342,155 L342,138 L350,138 L350,155" fill="none" stroke="#3f9a6b" stroke-width="1.5"/>
<rect x="352" y="138" width="298" height="17" fill="#3f9a6b" fill-opacity="0.25"/>
<text x="12" y="202" font-size="12">RESET_N</text>
<path d="M130,210 L350,210 L350,186 L650,186" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="12" y="252" font-size="12">I2C</text>
<path d="M130,250 L560,250" fill="none" stroke="#888" stroke-width="1.5"/>
<rect x="560" y="240" width="90" height="20" rx="3" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="605" y="254" text-anchor="middle" font-size="12">WHO_AM_I</text>
<line x1="230" y1="20" x2="230" y2="270" stroke="currentColor" stroke-dasharray="3,3" stroke-opacity="0.5"/>
<line x1="350" y1="20" x2="350" y2="270" stroke="currentColor" stroke-dasharray="3,3" stroke-opacity="0.5"/>
<line x1="560" y1="20" x2="560" y2="270" stroke="currentColor" stroke-dasharray="3,3" stroke-opacity="0.5"/>
<line x1="200" y1="122" x2="230" y2="122" stroke="currentColor" marker-start="url(#ah4)" marker-end="url(#ah4)"/>
<text x="215" y="134" text-anchor="middle" font-size="12">tRAMP</text>
<line x1="230" y1="226" x2="350" y2="226" stroke="currentColor" marker-start="url(#ah4)" marker-end="url(#ah4)"/>
<text x="290" y="240" text-anchor="middle" font-size="12">tPWR-RST ≥ 1 ms</text>
<line x1="350" y1="226" x2="560" y2="226" stroke="currentColor" marker-start="url(#ah4)" marker-end="url(#ah4)"/>
<text x="455" y="240" text-anchor="middle" font-size="12">tBOOT ≥ 5 ms</text>
<text x="340" y="292" text-anchor="middle" font-size="12">켜기: vddio → vdd → (램프) → MCLK → tPWR-RST → RESET_N 해제 → tBOOT → 첫 I2C</text>
<text x="340" y="314" text-anchor="middle" font-size="12">끄기: 역순 — RESET_N 걸기 → MCLK 정지 → vdd → vddio</text>
</svg>
```
그림 4 — 가짜 센서의 power-up 타이밍(개략도, 축척 아님). 점선은 "레일 안정", "리셋 해제", "첫 버스 접근" 시점이다.

왜 이 순서인가. Don의 RF 칩 경험과 같은 이유들이다.

- **vddio 먼저**: I/O 레일이 없는 상태에서 코어 레일이 먼저 오르면, I/O 핀이 정의되지 않은 상태로 버스를 끌어당기거나 ESD 다이오드로 역전류가 흐를 수 있다. 데이터시트가 반대를 요구하는 칩도 있으니 **항상 데이터시트를 따른다.**
- **레일 안정 후 리셋 해제**: 칩 내부 POR(power-on reset) 회로와 퓨즈/OTP 로딩은 레일이 안정된 상태에서 리셋이 풀려야 정상 동작한다. 레일이 오르는 중에 리셋이 풀리면 내부 상태 머신이 이상한 상태로 시작한다 — 칩 모델의 `bad POR state`.
- **클럭을 리셋 해제 전에**: 카메라 센서처럼 외부 MCLK로 내부 로직을 돌리는 칩은 리셋 해제 순간 클럭이 있어야 한다.
- **tBOOT 대기**: 리셋 해제 후 칩이 내부 초기화(OTP 로드, 펌웨어 부트)를 끝내기 전에는 버스에 응답하지 않는다.

### 5.2 예제 18 — consumer: 센서 드라이버 (`h3_sensor` 1/4 ~ 4/4)

무엇을 확인하나: 자원 구조체와 모듈 파라미터. `STEP()` 매크로는 `power_on` 시작부터의 경과 시간(us)을 붙여 로그를 찍는다.

```c
// SPDX-License-Identifier: GPL-2.0
/* h3_sensor (1/4): 파라미터 + 자원 구조체 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/ktime.h>

int h3_chip_whoami(u32 chip);		/* h3_providers의 칩 모델 */
static bool bad_seq, excl, atomic_bug;
static DEFINE_SPINLOCK(h3s_lock);
static ulong rate_b;
module_param(bad_seq, bool, 0444);	/* 1: 리셋을 전원보다 먼저 푼다 */
module_param(excl, bool, 0444);		/* 1: 리셋을 exclusive로 요청 */
module_param(rate_b, ulong, 0444);	/* !0: 센서 b가 clk_set_rate */
module_param(atomic_bug, bool, 0444);	/* 1: 스핀락 안에서 clk_prepare */

#define T_PWR_RST_US 1000
#define T_BOOT_US    5000
struct h3s {
	struct device *dev;
	u32 chip;
	struct clk *mclk;
	struct reset_control *rst;
	struct regulator_bulk_data sup[2];	/* [0] vddio, [1] vdd */
	struct regulator *vaux;			/* optional */
	ktime_t t0;
};
#define STEP(s, fmt, ...) dev_info((s)->dev, "[%5lld us] " fmt "\n", \
		ktime_us_delta(ktime_get(), (s)->t0), ##__VA_ARGS__)
```

전원 켜기. 실패하면 **켠 것만 역순으로** 되돌린다(goto 사다리 — Don이 펌웨어 init에서 쓰던 그 패턴이다).

```c
/* h3_sensor (2/4): 전원 켜기 시퀀스 — vddio → vdd → (vaux) → mclk → tPWR-RST → reset 해제 → tBOOT */
static int h3s_power_on(struct h3s *s)
{
	int ret;

	s->t0 = ktime_get();
	if (bad_seq && !reset_control_deassert(s->rst))
		STEP(s, "BAD ORDER: reset deasserted before any rail");
	ret = regulator_enable(s->sup[0].consumer);
	if (ret)
		return ret;
	STEP(s, "vddio on");
	ret = regulator_enable(s->sup[1].consumer);
	if (ret)
		goto off_io;
	STEP(s, "vdd on");
	ret = s->vaux ? regulator_enable(s->vaux) : 0;
	if (ret)
		goto off_vdd;
	if (s->vaux)
		STEP(s, "vaux on");
	if (atomic_bug) {		/* 잘못된 예: 원자 문맥에서 prepare */
		spin_lock(&h3s_lock);
		ret = clk_prepare_enable(s->mclk);
		spin_unlock(&h3s_lock);
	} else {
		ret = clk_prepare_enable(s->mclk);
	}
	if (ret)
		goto off_aux;
	STEP(s, "mclk on (%lu Hz)", clk_get_rate(s->mclk));
	usleep_range(T_PWR_RST_US, T_PWR_RST_US + 100);
	STEP(s, "waited tPWR-RST");
	if (!bad_seq) {
		ret = reset_control_deassert(s->rst);
		if (ret)
			goto off_clk;
		STEP(s, "reset released");
	}
	usleep_range(T_BOOT_US, T_BOOT_US + 200);
	STEP(s, "waited tBOOT");
	return 0;
off_clk:
	clk_disable_unprepare(s->mclk);
off_aux:
	if (s->vaux)
		regulator_disable(s->vaux);
off_vdd:
	regulator_disable(s->sup[1].consumer);
off_io:
	regulator_disable(s->sup[0].consumer);
	return ret;
}
```

끄기는 정확히 역순이다. `whoami` sysfs 속성은 언제든 칩 모델에 다시 물어보는 창이고, `h3s_rate_demo()`는 §2.7 예제 8에서 썼다.

```c
/* h3_sensor (3/4): 역순 전원 끄기 + sysfs whoami */
static void h3s_power_off(void *data)
{
	struct h3s *s = data;

	s->t0 = ktime_get();
	reset_control_assert(s->rst);
	STEP(s, "off: reset asserted");
	clk_disable_unprepare(s->mclk);
	STEP(s, "off: mclk off");
	if (s->vaux)
		regulator_disable(s->vaux);
	regulator_disable(s->sup[1].consumer);
	STEP(s, "off: vdd off");
	regulator_disable(s->sup[0].consumer);
	STEP(s, "off: vddio off");
}

static ssize_t whoami_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct h3s *s = dev_get_drvdata(dev);
	int id = h3_chip_whoami(s->chip);

	return id < 0 ? sysfs_emit(buf, "NACK (%d)\n", id) : sysfs_emit(buf, "0x%02x\n", id);
}
static DEVICE_ATTR_RO(whoami);
static struct attribute *h3s_attrs[] = { &dev_attr_whoami.attr, NULL };
ATTRIBUTE_GROUPS(h3s);

static void h3s_rate_demo(struct h3s *s)	/* 센서 b만: 공유 부모의 rate 바꾸기 */
{
	int ret;

	dev_info(s->dev, "round_rate(%lu) = %ld\n", rate_b, clk_round_rate(s->mclk, rate_b));
	ret = clk_set_rate(s->mclk, rate_b);
	dev_info(s->dev, "set_rate -> %d, now %lu Hz\n", ret, clk_get_rate(s->mclk));
}
```

probe는 "자원 전부 받기 → 전원 시퀀스 → 끄기 액션 등록 → 칩 확인" 순서다.

```c
/* h3_sensor (4/4): probe — 자원 받기 → 전원 시퀀스 → WHO_AM_I */
static int h3s_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct h3s *s;
	int ret, id;

	s = devm_kzalloc(dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;
	s->dev = dev;
	platform_set_drvdata(pdev, s);
	if (device_property_read_u32(dev, "dons,chip", &s->chip))
		return -EINVAL;
	s->sup[0].supply = "vddio";
	s->sup[1].supply = "vdd";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(s->sup), s->sup);
	if (ret)
		return dev_err_probe(dev, ret, "supplies\n");
	s->vaux = devm_regulator_get_optional(dev, "vaux");
	if (IS_ERR(s->vaux)) {
		if (PTR_ERR(s->vaux) != -ENODEV)
			return dev_err_probe(dev, PTR_ERR(s->vaux), "vaux\n");
		s->vaux = NULL;
	}
	s->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(s->mclk))
		return dev_err_probe(dev, PTR_ERR(s->mclk), "mclk\n");
	s->rst = excl ? devm_reset_control_get_exclusive(dev, NULL)
		      : devm_reset_control_get_shared(dev, NULL);
	if (IS_ERR(s->rst))
		return dev_err_probe(dev, PTR_ERR(s->rst), "reset\n");
	ret = regulator_set_voltage(s->sup[0].consumer, 1700000, 1950000);
	dev_info(dev, "vddio set_voltage(1.70-1.95 V) = %d -> %d uV\n",
		 ret, regulator_get_voltage(s->sup[0].consumer));
	dev_info(dev, "vdd set_voltage(3.6 V) = %d\n",
		 regulator_set_voltage(s->sup[1].consumer, 3600000, 3600000));
	if (rate_b && s->chip == 1)
		h3s_rate_demo(s);
	ret = h3s_power_on(s);
	if (ret)
		return dev_err_probe(dev, ret, "power on\n");
	ret = devm_add_action_or_reset(dev, h3s_power_off, s);
	if (ret)
		return ret;
	id = h3_chip_whoami(s->chip);
	if (id < 0)
		return dev_err_probe(dev, -ENODEV, "chip not responding on the bus\n");
	STEP(s, "WHO_AM_I = 0x%02x -> probe OK", id);
	return 0;
}

static const struct of_device_id h3s_ids[] = { { .compatible = "dons,h3-sensor" }, { } };
MODULE_DEVICE_TABLE(of, h3s_ids);
static struct platform_driver h3s_drv = { .probe = h3s_probe, .driver = {
	.name = "h3_sensor", .of_match_table = h3s_ids, .dev_groups = h3s_groups } };
module_platform_driver(h3s_drv);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("H3 demo: sensor power sequencing consumer");
```

설계 포인트.

- **자원을 먼저 다 받고 나서 켠다.** `devm_*_get`은 `-EPROBE_DEFER`를 낼 수 있다. 켜는 도중에 defer되면 반쯤 켠 상태를 되돌려야 하니, 받는 단계와 켜는 단계를 나눈다.
- `devm_add_action_or_reset(dev, h3s_power_off, s)`: 전원을 다 켠 **직후**에 등록한다. 이후의 실패(칩 무응답)와 remove/unbind 모두 같은 함수로 역순 끄기가 된다. `devm` 해제는 등록의 역순이라, 이 액션이 핸들 해제(`devm_clk_get` 등)보다 먼저 실행된다 — 핸들이 살아 있을 때 끈다.
- 칩 확인 실패는 `-ENODEV`로 돌려준다. "이 주소에 우리가 기대한 칩이 없다"는 뜻의 관례적 에러다.

### 5.3 예제 19 — 시퀀스 소요 시간 측정 (5회)

무엇을 확인하나: 같은 부팅 안에서 `h3_sensor`를 5번 insmod/rmmod 해서 센서 a의 단계별 경과 시간을 모은다.

```sh
for i in 1 2 3 4 5; do dmesg -c > /dev/null; insmod /h3_sensor.ko; dmesg | grep "h3-sensor-a: \[" | sed 's/.*\] h3_sensor h3-sensor-a: //' | tr '\n' ';'; echo; rmmod h3_sensor; done
```

```text
[ 1770 us] vddio on;[ 4007 us] vdd on;[ 8139 us] vaux on;[ 8885 us] mclk on (19200000 Hz);[10397 us] waited tPWR-RST;[11004 us] reset released;[16441 us] waited tBOOT;[16608 us] WHO_AM_I = 0x6c -> probe OK;
[ 2325 us] vddio on;[ 3987 us] vdd on;[ 8449 us] vaux on;[ 9140 us] mclk on (19200000 Hz);[10725 us] waited tPWR-RST;[10988 us] reset released;[16834 us] waited tBOOT;[17001 us] WHO_AM_I = 0x6c -> probe OK;
[ 2615 us] vddio on;[ 4080 us] vdd on;[ 9645 us] vaux on;[10491 us] mclk on (19200000 Hz);[11772 us] waited tPWR-RST;[12098 us] reset released;[17673 us] waited tBOOT;[17946 us] WHO_AM_I = 0x6c -> probe OK;
[ 1890 us] vddio on;[ 4224 us] vdd on;[ 8682 us] vaux on;[ 9627 us] mclk on (19200000 Hz);[11280 us] waited tPWR-RST;[11426 us] reset released;[17035 us] waited tBOOT;[17201 us] WHO_AM_I = 0x6c -> probe OK;
[ 2259 us] vddio on;[ 4040 us] vdd on;[ 8385 us] vaux on;[ 9173 us] mclk on (19200000 Hz);[10666 us] waited tPWR-RST;[10782 us] reset released;[16259 us] waited tBOOT;[16462 us] WHO_AM_I = 0x6c -> probe OK;
```

(이 루프는 `g_dbgfs.sh`의 마지막 부분이고, 예제 7과 같은 부팅에서 돌렸다.) 각 줄에서 앞 단계와의 차이를 단계별 소요 시간으로 보고, 5회의 중앙값을 그렸다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="22" font-size="13">센서 a 전원 시퀀스 — 단계별 소요 시간 중앙값 (ms, 5회, QEMU TCG)</text>
<line x1="160.0" y1="40" x2="160.0" y2="252" stroke="currentColor" stroke-opacity="0.15"/><text x="160.0" y="268" font-size="12" text-anchor="middle">0</text>
<line x1="240.0" y1="40" x2="240.0" y2="252" stroke="currentColor" stroke-opacity="0.15"/><text x="240.0" y="268" font-size="12" text-anchor="middle">3</text>
<line x1="320.0" y1="40" x2="320.0" y2="252" stroke="currentColor" stroke-opacity="0.15"/><text x="320.0" y="268" font-size="12" text-anchor="middle">6</text>
<line x1="400.0" y1="40" x2="400.0" y2="252" stroke="currentColor" stroke-opacity="0.15"/><text x="400.0" y="268" font-size="12" text-anchor="middle">9</text>
<line x1="480.0" y1="40" x2="480.0" y2="252" stroke="currentColor" stroke-opacity="0.15"/><text x="480.0" y="268" font-size="12" text-anchor="middle">12</text>
<line x1="560.0" y1="40" x2="560.0" y2="252" stroke="currentColor" stroke-opacity="0.15"/><text x="560.0" y="268" font-size="12" text-anchor="middle">15</text>
<line x1="640.0" y1="40" x2="640.0" y2="252" stroke="currentColor" stroke-opacity="0.15"/><text x="640.0" y="268" font-size="12" text-anchor="middle">18 ms</text>
<line x1="160" y1="252" x2="645" y2="252" stroke="currentColor"/>
<text x="10" y="57" font-size="12">vddio on</text>
<text x="150" y="57" font-size="12" text-anchor="end">2.26</text>
<rect x="160.0" y="44" width="60.2" height="18" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="160.0" y="50" width="26.7" height="6" fill="#e08a3c"/>
<text x="10" y="83" font-size="12">vdd on</text>
<text x="150" y="83" font-size="12" text-anchor="end">1.78</text>
<rect x="220.2" y="70" width="47.5" height="18" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="220.2" y="76" width="26.7" height="6" fill="#e08a3c"/>
<text x="10" y="109" font-size="12">vaux on</text>
<text x="150" y="109" font-size="12" text-anchor="end">4.46</text>
<rect x="267.7" y="96" width="118.9" height="18" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="267.7" y="102" width="80.0" height="6" fill="#e08a3c"/>
<text x="10" y="135" font-size="12">mclk on</text>
<text x="150" y="135" font-size="12" text-anchor="end">0.79</text>
<rect x="386.6" y="122" width="21.0" height="18" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="386.6" y="128" width="5.3" height="6" fill="#e08a3c"/>
<text x="10" y="161" font-size="12">tPWR-RST</text>
<text x="150" y="161" font-size="12" text-anchor="end">1.51</text>
<rect x="407.6" y="148" width="40.3" height="18" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="407.6" y="154" width="26.7" height="6" fill="#e08a3c"/>
<text x="10" y="187" font-size="12">reset 해제</text>
<text x="150" y="187" font-size="12" text-anchor="end">0.26</text>
<rect x="447.9" y="174" width="7.0" height="18" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="10" y="213" font-size="12">tBOOT</text>
<text x="150" y="213" font-size="12" text-anchor="end">5.58</text>
<rect x="455.0" y="200" width="148.7" height="18" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="455.0" y="206" width="133.3" height="6" fill="#e08a3c"/>
<text x="10" y="239" font-size="12">WHO_AM_I</text>
<text x="150" y="239" font-size="12" text-anchor="end">0.17</text>
<rect x="603.6" y="226" width="4.5" height="18" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<rect x="160" y="290" width="14" height="12" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/>
<text x="180" y="300" font-size="12">측정 중앙값</text>
<rect x="290" y="293" width="14" height="6" fill="#e08a3c"/>
<text x="310" y="300" font-size="12">요구 최소 대기</text>
</svg>
```
그림 5 — 예제 19의 측정값. 각 단계 막대는 앞 단계 끝에서 시작한다. 주황 막대는 요구 최소 대기(ramp-delay, startup-delay-us, PLL 락, tPWR-RST, tBOOT)다. 전체 중앙값은 17.0 ms, 요구 최소 합은 약 11.2 ms다.

| 단계 | 중앙값 (ms) | 5회 범위 (ms) | 요구 최소 (ms) | 누가 기다리나 |
|---|---|---|---|---|
| vddio on | 2.26 | 1.77 ~ 2.62 | 1.0 | 레귤레이터 코어 (`regulator-enable-ramp-delay`) |
| vdd on | 1.78 | 1.47 ~ 2.33 | 1.0 | 레귤레이터 코어 |
| vaux on | 4.46 | 4.13 ~ 5.57 | 3.0 | 레귤레이터 코어 (`startup-delay-us`) |
| mclk on | 0.79 | 0.69 ~ 0.95 | 0.2 | PLL provider의 `.prepare` |
| tPWR-RST | 1.51 | 1.28 ~ 1.65 | 1.0 | 센서 드라이버 `usleep_range` |
| reset 해제 | 0.26 | 0.12 ~ 0.61 | 0 | — (provider ops + 로그 비용) |
| tBOOT | 5.58 | 5.44 ~ 5.85 | 5.0 | 센서 드라이버 `usleep_range` |
| WHO_AM_I | 0.17 | 0.17 ~ 0.27 | 0 | — |

읽는 법: 모든 단계가 요구 최소보다 길다 — `usleep_range()`와 레귤레이터 코어의 지연은 "최소 이만큼"을 보장하고, 나머지는 타이머 slack, 스케줄링, printk, 그리고 **QEMU TCG 에뮬레이션 오버헤드**다(진짜 Cortex-A 보드에서는 초과분이 훨씬 작다. 이 표의 절대값을 실제 보드 예산으로 쓰면 안 된다). 실무 포인트는 두 가지다. 첫째, 대기 시간이 **누구 책임인지**가 표의 마지막 열처럼 갈린다 — 레일 램프는 DT(레귤레이터), 칩 타이밍은 드라이버. 같은 대기를 양쪽에서 하면 부팅이 느려지고, 양쪽 다 안 하면 칩이 가끔 죽는다. 둘째, 센서가 10개면 이 17 ms가 순차로 쌓인다. 부팅 시간이 중요하면 async probe(`PROBE_PREFER_ASYNCHRONOUS`)나 runtime PM으로 첫 사용 때까지 미룬다.

### 5.4 예제 20 — 순서를 틀리면: 리셋을 전원보다 먼저 풀기

무엇을 확인하나: `bad_seq=1`은 레일을 켜기 **전에** 리셋을 푼다. 나머지 대기는 그대로다.

```sh
sh /Users/donh/.elinux_scratch/H3/boot.sh h3_ok H3_ARGS=bad_seq=1~sh~/g_probe.sh
```

```text
[    1.535139] h3_cru h3-cru: hw: reset line 0 DEASSERT
[    1.535191] h3_sensor h3-sensor-a: [  165 us] BAD ORDER: reset deasserted before any rail
[    1.535377] regulator regulator.3: hw: ldo1 ON
[    1.539787] h3_sensor h3-sensor-a: [ 4752 us] vddio on
[    1.539831] regulator regulator.4: hw: ldo2 ON
[    1.541426] h3_sensor h3-sensor-a: [ 6374 us] vdd on
[    1.545701] h3_sensor h3-sensor-a: [10646 us] vaux on
[    1.546807] h3_sensor h3-sensor-a: [11739 us] mclk on (19200000 Hz)
[    1.548439] h3_sensor h3-sensor-a: [13391 us] waited tPWR-RST
[    1.554215] h3_sensor h3-sensor-a: [19153 us] waited tBOOT
[    1.554503] h3 chip0 model: NACK - reset released before rails were stable -> stuck in bad POR state (mclk=19200000)
[    1.554604] h3_sensor h3-sensor-a: error -ENODEV: chip not responding on the bus
[    1.555107] h3_cru h3-cru: hw: reset line 0 ASSERT
[    1.555149] h3_sensor h3-sensor-a: [  172 us] off: reset asserted
[    1.555239] h3_sensor h3-sensor-a: [  262 us] off: mclk off
[    1.555581] regulator regulator.4: hw: ldo2 OFF
[    1.555621] h3_sensor h3-sensor-a: [  646 us] off: vdd off
[    1.555636] regulator regulator.3: hw: ldo1 OFF
[    1.555654] h3_sensor h3-sensor-a: [  679 us] off: vddio off
...
[    1.576510] h3 chip1 model: NACK - reset released before rails were stable -> stuck in bad POR state (mclk=19200000)
[    1.576529] h3_sensor h3-sensor-b: error -ENODEV: chip not responding on the bus
```

(앞의 `set_voltage` 줄과 센서 b의 같은 패턴은 `...`로 생략했다.) 출력에서 볼 것:

- 모든 프레임워크 호출이 성공했고 대기도 충분했다(`waited tBOOT`까지 19 ms). **순서만** 틀렸다. 그래도 칩은 응답하지 않는다. 진짜 보드에서 이 버그는 "가끔만" 재현되기도 한다 — 부트로더가 레일을 미리 켜 둔 warm reboot에서는 멀쩡하고, cold boot에서만 죽는다.
- probe 실패 경로에서 `devm` 액션이 **역순으로 전부 껐다**(reset ASSERT → mclk off → ldo2 OFF → ldo1 OFF). 실패해도 레일이 켜진 채 남지 않는다.
- **고친 결과**는 예제 4다: `vdd on` → `mclk on` → `waited tPWR-RST` → `hw: reset line 0 DEASSERT` → `waited tBOOT` → `WHO_AM_I = 0x6c`. 같은 모듈에서 `bad_seq=0`(기본값)이면 된다.

### 5.5 예고 — runtime PM에 시퀀스를 걸기 (N5)

지금 센서는 probe부터 remove까지 계속 켜져 있다. 배터리 장비라면 "안 쓰는 동안 끄기"가 필요하고, 그 틀이 runtime PM이다(N5). 이 노트의 `h3s_power_on()`/`h3s_power_off()`가 그대로 콜백이 된다. 아래는 모양만 보이는 코드다 — **이 노트에서 빌드·실행하지 않았다.**

```c
/* (미실행 스케치) runtime PM 콜백에 전원 시퀀스 연결 — 자세한 건 N5 */
static int h3s_runtime_suspend(struct device *dev)
{
	h3s_power_off(dev_get_drvdata(dev));
	return 0;
}
static int h3s_runtime_resume(struct device *dev)
{
	return h3s_power_on(dev_get_drvdata(dev));	/* 다시 tBOOT까지 기다린다 */
}
static DEFINE_RUNTIME_DEV_PM_OPS(h3s_pm, h3s_runtime_suspend, h3s_runtime_resume, NULL);
/* .driver.pm = pm_ptr(&h3s_pm); 읽기 경로에서 pm_runtime_resume_and_get() / pm_runtime_put_autosuspend() */
```

주의할 점(N5에서 자세히): resume마다 17 ms(실제 보드에서도 tBOOT만큼)가 드니 autosuspend 지연을 둔다. 공유 레일·공유 리셋이면 내가 suspend해도 실제로는 안 꺼진다(그림 3) — 전력 이득이 없을 수 있고, 반대로 "리셋으로 깨끗하게 재시작"을 기대한 resume도 실패한다.

---

## 6. 커널 안에서는 무슨 일이 일어나나

세 프레임워크는 모양이 같다: **consumer API → 코어(트리·refcount·제약·락) → provider ops → 하드웨어.** consumer는 provider가 누구인지 모르고, DT의 phandle(D4)이 둘을 이어 준다.

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="15" y="24" font-size="12">consumer (센서 드라이버)</text>
<rect x="15" y="32" width="210" height="58" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="55" text-anchor="middle" font-size="12">devm_clk_get()</text>
<text x="120" y="75" text-anchor="middle" font-size="12">clk_prepare_enable()</text>
<rect x="235" y="32" width="210" height="58" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="55" text-anchor="middle" font-size="12">devm_reset_control_get</text>
<text x="340" y="75" text-anchor="middle" font-size="12">_shared / _exclusive</text>
<rect x="455" y="32" width="210" height="58" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="560" y="55" text-anchor="middle" font-size="12">devm_regulator_get()</text>
<text x="560" y="75" text-anchor="middle" font-size="12">regulator_enable()</text>
<text x="15" y="120" font-size="12">프레임워크 코어 (drivers/clk, drivers/reset, drivers/regulator)</text>
<rect x="15" y="128" width="210" height="76" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="120" y="150" text-anchor="middle" font-size="12">clk_core 트리, count</text>
<text x="120" y="170" text-anchor="middle" font-size="12">prepare_lock (mutex)</text>
<text x="120" y="190" text-anchor="middle" font-size="12">enable_lock (spinlock)</text>
<rect x="235" y="128" width="210" height="76" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="150" text-anchor="middle" font-size="12">rcdev 목록, reset_control</text>
<text x="340" y="170" text-anchor="middle" font-size="12">deassert_count (shared)</text>
<text x="340" y="190" text-anchor="middle" font-size="12">acquired (exclusive)</text>
<rect x="455" y="128" width="210" height="76" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="560" y="150" text-anchor="middle" font-size="12">regulator_dev, use_count</text>
<text x="560" y="170" text-anchor="middle" font-size="12">constraints (DT)</text>
<text x="560" y="190" text-anchor="middle" font-size="12">supply 체인, enable 지연</text>
<text x="15" y="234" font-size="12">provider (클럭·리셋 컨트롤러, PMIC 드라이버)</text>
<rect x="15" y="242" width="210" height="58" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="120" y="265" text-anchor="middle" font-size="12">struct clk_ops</text>
<text x="120" y="285" text-anchor="middle" font-size="12">.prepare .enable .set_rate</text>
<rect x="235" y="242" width="210" height="58" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="265" text-anchor="middle" font-size="12">reset_control_ops</text>
<text x="340" y="285" text-anchor="middle" font-size="12">.assert .deassert .status</text>
<rect x="455" y="242" width="210" height="58" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="560" y="265" text-anchor="middle" font-size="12">regulator_ops</text>
<text x="560" y="285" text-anchor="middle" font-size="12">.enable .set_voltage_sel</text>
<rect x="15" y="318" width="650" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="343" text-anchor="middle" font-size="12">하드웨어: CCM/CRU 게이트·분주·PLL 레지스터 · 리셋 비트나 GPIO · PMIC 레지스터 (I2C)</text>
<line x1="120" y1="90" x2="120" y2="126" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="340" y1="90" x2="340" y2="126" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="560" y1="90" x2="560" y2="126" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="120" y1="204" x2="120" y2="240" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="340" y1="204" x2="340" y2="240" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="560" y1="204" x2="560" y2="240" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="120" y1="300" x2="120" y2="316" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="340" y1="300" x2="340" y2="316" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="560" y1="300" x2="560" y2="316" stroke="currentColor" marker-end="url(#ah3)"/>
</svg>
```
그림 6 — 세 프레임워크의 같은 3층 구조. 이 노트에서 consumer는 `h3_sensor.ko`, provider는 `h3_providers.ko`(그리고 커널 내장 `regulator-fixed`)였다.

경로를 함수 이름으로 따라가면 (6.18 기준):

- **클럭 get**: `devm_clk_get()` → `clk_get()` → `of_clk_get_hw()`가 DT `clocks`/`clock-names`를 `#clock-cells`로 파싱 → provider 목록에서 같은 `of_node`를 찾아 provider의 `get_hw` 콜백(우리는 `of_clk_hw_onecell_get`)으로 `clk_hw`를 얻음 → `clk_hw_create_clk()`가 **consumer별 `struct clk`**를 만들어 `clk_core`의 consumer 목록에 건다(그래서 `clk_summary`에 consumer 이름이 나온다). provider가 없으면 `-EPROBE_DEFER`.
- **클럭 켜기**: `clk_prepare()` → `clk_prepare_lock()`(mutex) → `clk_core_prepare()`가 **부모부터 재귀** → `prepare_count`가 0일 때만 `ops->prepare`. `clk_enable()` → `clk_enable_lock()`(spinlock + irq 저장) → `clk_core_enable()` 재귀 → `ops->enable`. 예제 9의 콜 트레이스가 이 재귀를 그대로 보여 줬다.
- **assigned-clock-rates**: `platform_probe()` → `of_clk_set_defaults()`가 드라이버 `.probe` 직전에 처리. 그래서 드라이버 코드에는 흔적이 없다.
- **리셋 get**: `__devm_reset_control_get()` → `__of_reset_control_get()`가 `resets`를 `#reset-cells`로 파싱 → `reset_controller_list`에서 `of_node`가 같은 `rcdev`를 찾고 `of_xlate`로 라인 번호를 얻음 → `__reset_control_get_internal()`이 그 `rcdev`의 `reset_control` 목록에서 같은 라인을 찾는다. 있고 둘 다 shared면 **같은 객체의 kref를 올려 돌려준다**(그래서 두 장치가 `deassert_count` 하나를 공유한다). 하나라도 exclusive면 예제 13의 WARN + `-EBUSY`.
- **리셋 해제**: `reset_control_deassert()` → shared면 `atomic_inc_return(&deassert_count) != 1`일 때 바로 0 반환, 1일 때만 `rcdev->ops->deassert()`.
- **레귤레이터 get**: `_regulator_get()` → `regulator_dev_lookup()`이 `<이름>-supply` phandle로 `regulator_dev`를 찾음. 속성이 없으면 일반 get은 dummy, optional/exclusive get은 `-ENODEV`. 속성은 있는데 장치가 없으면 `-EPROBE_DEFER`(D4 §7). 찾으면 `create_regulator()`가 consumer 객체와 debugfs 디렉터리(`h3-sensor-a-vddio`)를 만든다.
- **레귤레이터 켜기**: `regulator_enable()` → `_regulator_enable()`이 먼저 **부모 supply를 재귀로 켜고**(`vin-supply` 체인), `use_count`가 0이면 `_regulator_do_enable()` → `ops->enable` → `enable_time`(DT ramp-delay, `startup-delay-us`)만큼 지연 → `use_count++`.

드라이버를 쓸 때 다시 만나는 곳: I2C 센서 드라이버(H5)와 IIO 드라이버(I2)의 probe 첫머리는 항상 이 세 프레임워크다. 카메라 센서(I5)는 MCLK `clk_set_rate` + 레일 3개 + PWDN/RESET GPIO(H4)다. PMIC 드라이버는 regmap(H2) + regulator provider다. runtime PM(N5)과 system suspend는 이 시퀀스를 콜백으로 다시 부른다.

---

## 7. Embedded 실무에서 다시 보기

**장면 1 — 카메라 센서 bring-up.** MIPI CSI-2 카메라 센서는 보통 레일 3개(AVDD 2.8 V, DOVDD 1.8 V, DVDD 1.2 V), SoC의 클럭 출력 핀에서 나오는 MCLK(24 MHz 등), XSHUTDOWN/RESET GPIO를 갖는다. 드라이버는 `devm_clk_get(dev, NULL)` 후 `clk_set_rate(mclk, 24000000)`를 하고 `clk_get_rate()`로 확인한다 — 이 노트 예제 8처럼 SoC 클럭 출력이 다른 장치와 PLL을 공유하면 정확한 값이 안 나올 수 있다. 많은 업스트림 카메라 드라이버가 레일을 `regulator_bulk_enable()`로 켜는데(§4.8: 병렬), 데이터시트가 순서를 요구하는 센서라면 하나씩 켜야 한다.

**장면 2 — "warm reboot에선 되는데 cold boot에선 안 된다".** 부트로더(U-Boot)가 화면이나 eMMC 때문에 어떤 레일·클럭을 켜 두면, DT에서 센서의 `vdd-supply`가 빠져 있어도(예제 17) warm reboot 직후엔 레일이 살아 있어 동작한다. cold boot나 부트로더 설정이 바뀐 빌드에서 갑자기 죽는다. 점검 순서: `dmesg | grep -i dummy` → `regulator_summary`에서 센서가 consumer로 있는지 → `clk_summary`에서 센서 클럭의 consumer 열 → 부팅 30초 뒤 `disabling` 로그.

**장면 3 — 공유 레일 위의 센서 키트.** IMU, 자력계, 기압계가 3.3 V 레일 하나를 같이 쓴다. IMU 드라이버가 에러 복구로 "전원 껐다 켜기"를 하려고 `regulator_disable()` → `regulator_enable()`을 해도 다른 두 센서가 레일을 잡고 있어 실제로는 안 꺼진다(그림 3). 진짜 파워 사이클이 필요하면 하드웨어 설계 단계에서 전용 로드 스위치를 요청하거나, 리셋 핀(exclusive)을 쓴다. Don이 HW 팀과 회로도 리뷰를 할 때 꺼낼 수 있는 질문이다.

**장면 4 — 커널 로그와 scope 맞추기 (M11).** 시퀀스 각 단계에 GPIO 토글을 넣고(H4) scope로 레일 전압과 같이 찍으면, "드라이버가 `reset released`를 찍은 시각에 실제 VDD가 몇 %였나"를 볼 수 있다. 예제 19의 표가 소프트웨어 쪽 타임라인이고, 그걸 scope 파형에 겹치는 것이 Don의 차별화 포인트다. 특히 `regulator-enable-ramp-delay`가 실제 램프보다 짧게 적혀 있는 DT는 scope 없이는 못 잡는다.

---

## 8. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| DT에 `vdd-supply` 누락 | `using dummy regulator` 경고, 첫 I2C에서 NACK. warm reboot에선 될 수도 | 일반 `regulator_get`은 속성이 없으면 dummy를 준다 | 회로도 대로 `*-supply` 추가. 필수 전원이면 드라이버가 dummy를 의심 |
| 리셋을 레일보다 먼저 해제 | 칩 무응답, 전원 껐다 켜야만 복구 | 칩 POR이 불안정한 레일에서 시작 | 레일 → 클럭 → tPWR-RST → 리셋 해제 순서 (예제 20 → 예제 4) |
| 순서가 중요한 레일에 `regulator_bulk_enable` | 가끔만 실패하는 bring-up | bulk enable은 async로 병렬 실행 | 하나씩 `regulator_enable` |
| 원자 문맥에서 `clk_prepare_enable` | `scheduling while atomic` / `sleeping function called from invalid context` | prepare는 mutex와 잠드는 provider ops를 쓴다 | probe에서 prepare, 핫패스에선 `clk_enable`만 |
| 공유 클럭 부모의 rate 변경 | 다른 장치가 조용히 고장 (예제 8) | `CLK_SET_RATE_PARENT` 전파 | `clk_rate_exclusive_get`, `clk_set_rate_range`, notifier |
| 공유 리셋 라인을 exclusive로 요청 | WARN 스택 덤프 + `-EBUSY` (예제 13) | 같은 라인에 exclusive와 다른 요청이 공존 불가 | 모든 consumer를 shared로 |
| 제품 커널에 `clk_ignore_unused` 남김 | 대기 전력 증가 | 부팅 정리를 끈 채 출시 | 정리에 걸리는 클럭의 consumer DT/드라이버를 고침 |
| enable/disable 짝 불일치 | `unbalanced disables` WARN, 레일이 안 꺼짐 | probe 실패 경로·remove 누락 | `devm_add_action_or_reset`로 끄기 등록 |
| clock provider를 모듈로 빌드 | 부트로더가 켠 클럭이 영원히 켜져 있음 (예제 10) | unused 정리는 `late_initcall_sync` 한 번 | SoC 클럭 드라이버는 built-in |

---

## 9. 면접에서 이렇게 말한다

**Q.** The sensor doesn't respond on the bus at bring-up. What do you check?

**A.** 버스보다 앞단부터 본다. 1) 전원: `regulator_summary`에 센서가 consumer로 있고 레일이 켜졌는지, dmesg에 dummy 경고가 없는지, scope로 실제 전압. 2) 클럭: `clk_summary`에서 센서 클럭의 enable count·rate, 레퍼런스 클럭이 스펙 안인지. 3) 리셋: 리셋 핀이 실제로 풀렸는지(`debug/gpio`, 리셋 컨트롤러 status), 극성. 4) 타이밍: 레일 안정 → 리셋 해제 → tBOOT 대기가 데이터시트를 만족하는지 — 로그 타임스탬프와 scope로. 5) 그다음에 버스: 주소, 풀업, `i2cdetect`, 로직 분석기.

> I work from the power side toward the bus. First, power: is the device listed as a consumer in regulator_summary, is the rail actually enabled, is there a "using dummy regulator" warning, and does the scope agree. Second, the clock: enable count and rate in clk_summary, and whether the reference clock is within the datasheet range. Third, reset: is the line really deasserted and is the polarity right. Fourth, sequencing: rails stable, then reset release, then the boot time before the first transaction — I compare log timestamps with the scope. Only then do I look at the bus itself: address, pull-ups, i2cdetect and a logic analyzer.

**꼬리 질문**: "모든 API가 성공했는데도 NACK이면?" → 순서·타이밍 문제(예제 20)나 DT가 실제 회로와 다른 경우(dummy). 시퀀스 각 단계에 GPIO 토글을 넣어 scope로 실제 타이밍을 본다.

**Q.** Why does the clock API split prepare and enable?

**A.** 잠들 수 있는 일과 원자적으로 해야 하는 일을 나누려고. PLL 락 대기나 I2C로 붙은 외부 클럭 칩 설정은 잠들어야 하니 `clk_prepare`(mutex)에서, 게이트 비트 하나 쓰는 일은 `clk_enable`(spinlock)에서 한다. 그래서 드라이버는 probe에서 prepare해 두고 인터럽트 핸들러나 스핀락 안에서는 enable/disable만 토글할 수 있다. 원자 문맥에서 prepare를 부르면 디버그 커널에서 `scheduling while atomic`이 난다.

> Because some clock operations can sleep and some must be atomic. Locking a PLL or programming an external clock chip over I2C may sleep, so that belongs in clk_prepare, which is protected by a mutex. Flipping a gate bit is fast and goes in clk_enable, under a spinlock, so it can be called from interrupt context. A driver prepares in probe and only toggles enable and disable on the hot path.

**Q.** What happens when a driver calls clk_set_rate on a clock that shares a parent with another device?

**A.** `CLK_SET_RATE_PARENT`가 있으면 요청이 부모로 올라가 PLL이나 divider, mux 부모까지 바뀔 수 있고, 같은 부모 아래 형제 클럭의 rate도 같이 바뀐다. 실습에서 센서 b가 48 MHz를 요청하자 센서 a의 클럭도 48 MHz가 되어 a가 응답을 멈췄다. 막으려면 `clk_rate_exclusive_get`, `clk_set_rate_range`, 또는 clk notifier로 변경을 거부하거나 대응한다. `set_rate`가 0을 반환해도 `clk_get_rate`로 실제 값을 확인한다.

> If the clock has CLK_SET_RATE_PARENT, the request propagates up the tree and can retune a divider or a PLL, or even reparent a mux. Every sibling under that parent changes too. In my lab, one sensor asked for 48 MHz and the other sensor's clock silently moved to 48 MHz and it stopped responding. The defenses are clk_rate_exclusive_get, clk_set_rate_range, or a clock notifier, and you always read back the rate with clk_get_rate.

**Q.** Exclusive versus shared reset controls?

**A.** exclusive는 한 consumer가 라인을 소유하고 assert/deassert가 즉시 하드웨어에 반영된다. shared는 여러 consumer가 같이 쓰고, `deassert_count`를 세어 0 → 1일 때만 실제 해제, 1 → 0일 때만 실제 assert한다. 같은 라인을 한쪽이라도 exclusive로 요청하면 WARN + `-EBUSY`. shared의 `reset_control_reset`은 처음 한 번만 펄스를 낸다. 결과적으로 shared 라인으로는 동작 중인 칩을 다시 리셋할 수 없다.

> An exclusive reset control is owned by one consumer, and assert and deassert go straight to hardware. A shared control is reference-counted: the line is only released when the deassert count goes from zero to one, and only asserted again when it drops back to zero. Requesting the same line exclusively while someone else holds it gives a warning and -EBUSY. The consequence is that a shared reset cannot be used to recover a single misbehaving chip.

**Q.** What does "supply vdd not found, using dummy regulator" mean and why is it dangerous?

**A.** DT 노드에 `vdd-supply` 속성이 없어서 레귤레이터 코어가 "보드가 항상 켜 둔 전원이겠지"라고 가정하고 아무것도 안 하는 dummy를 줬다는 뜻이다. 드라이버의 enable은 성공하지만 실제로 아무 레일도 안 켜진다. 그 레일이 실제로는 꺼져 있으면 probe의 첫 버스 접근에서 NACK이 난다. 정말 선택적인 전원이면 `regulator_get_optional`을 써서 `-ENODEV`를 받는다.

> It means the device tree node has no vdd-supply property, so the regulator core assumes the rail is always on and hands back a dummy regulator whose enable does nothing. The driver's power-up sequence "succeeds", but if that rail is actually switched, the chip has no power and the first bus access fails. For bring-up I treat that warning as a device tree bug and check it against the schematic.

**Q.** Why are clocks and regulators switched off late in boot, and what is clk_ignore_unused for?

**A.** 부트로더가 켜 둔 채 넘긴 클럭·레일 중 아무 드라이버도 요청하지 않은 것을 꺼서 전력을 아끼기 위해서다. 클럭은 `late_initcall_sync`에서 한 번(`clk: Disabling unused clocks`), 레귤레이터는 그로부터 30초 뒤(`<이름>: disabling`). `clk_ignore_unused`, `regulator_ignore_unused`는 이걸 건너뛰는 디버그용 인자다. 이걸 켜야 동작한다면 어떤 드라이버가 자기 자원을 DT에 안 적었거나 get을 안 한 버그다. 끄면 안 되는 클럭은 provider가 `CLK_IS_CRITICAL`로 표시한다.

> To save power: anything the bootloader left on that no driver has claimed gets turned off. Clocks are swept once at late_initcall_sync, regulators about thirty seconds later to give modules time to load. clk_ignore_unused and regulator_ignore_unused skip that, and they are debugging aids. If a board only works with them, some driver is not declaring or requesting its clock or supply, and that is the real bug. Clocks that must never be gated are marked CLK_IS_CRITICAL by the provider.

**Q.** How do you implement a power-on sequence in a driver so that it is robust?

**A.** probe에서 먼저 모든 핸들을 받는다(`-EPROBE_DEFER`는 그대로 반환). 그다음 데이터시트 순서대로 하나씩 켜고 각 대기는 `usleep_range`로, 레일 램프는 DT의 ramp-delay로 표현한다. 실패하면 켠 것만 역순으로 되돌리는 goto 사다리를 쓰고, 성공하면 `devm_add_action_or_reset`으로 역순 끄기를 등록해서 이후 실패와 remove가 같은 경로를 탄다. 마지막으로 ID 레지스터를 읽어 확인하고, 이 두 함수를 runtime PM 콜백으로 재사용한다. bulk enable은 순서가 보장되지 않으니 쓰지 않는다.

> I acquire every resource first, propagating probe deferral. Then I power up strictly in datasheet order: rails one at a time, with ramp delays expressed in the device tree, then the clock, then the reset release, with usleep_range for the chip's own timing. On failure I unwind only what I enabled, in reverse. Once everything is up I register the power-off as a devm action, so later failures and unbind share the same reverse path. I verify with an ID register read and reuse the same two functions as runtime PM callbacks.

**꼬리 질문**: "Why not regulator_bulk_enable?" → 병렬(async)로 켜서 순서가 보장되지 않는다.

---

## 10. 직접 해보기

1. **개념**: `clk_summary`에서 어떤 게이트의 enable count가 0인데 hardware enable이 `Y`다. 가능한 원인 두 가지와, 각각을 어떻게 구분하나?
   정답/힌트: (a) 부트로더가 켜 두고 아무도 get 안 함 — 부팅 로그의 unused 정리 시점보다 provider 등록이 늦었는지, `CLK_IGNORE_UNUSED`인지 확인 (예제 10의 `h3_gate_dbg`). (b) provider의 `.is_enabled`가 엉뚱한 비트를 읽음 — 레지스터 덤프와 비교.
2. **코드**: `h3_sensor`에 `clk_rate_exclusive_get()`을 넣어(전원 켠 직후, devm 변형 `devm_clk_rate_exclusive_get`) 예제 8의 `rate_b=48000000`을 다시 돌려라. 센서 a가 살아남는지, 센서 b의 `set_rate` 반환값과 결과 rate가 어떻게 바뀌는지 기록하라.
   정답/힌트: 센서 a가 rate를 보호하면 공유 경로의 rate는 바뀌지 않아야 한다. b의 요청은 다른 답(예: 가능한 근삿값)이나 에러로 끝난다 — 실제 값은 직접 확인할 것(이 노트에서는 실행하지 않았다).
3. **코드**: provider의 `ldo_desc`에 `.enable_time = 1000`을 넣고 DT에서 `regulator-enable-ramp-delay`를 지운 뒤 예제 19를 다시 측정하라. 결과가 같은가? 우선순위는?
   정답/힌트: 코어는 constraints의 `enable_time`(DT) → `ops->enable_time` → `desc->enable_time` 순으로 쓴다. 측정 결과는 비슷해야 한다.
4. **관찰**: 예제 12를 `regulator_summary` 대신 `/sys/kernel/debug/regulator/h3_vddio/use_count`와 `open_count`, 그리고 consumer 디렉터리 안 파일로 다시 관찰하라.
   정답/힌트: `ls /sys/kernel/debug/regulator/h3_vddio`에 `use_count`, `open_count`, `bypass_count`, consumer 디렉터리(`h3-sensor-a-vddio`)가 있다 (예제 7과 같은 부팅에서 확인).
5. **코드**: 리셋 provider에 `.reset` op(assert → `udelay(10)` → deassert)을 추가하고, 센서가 exclusive일 때 `reset_control_reset()`을 두 번 부르면 펄스가 두 번 나는지, shared일 때는 몇 번 나는지 `hw:` 로그로 확인하라.
   정답/힌트: exclusive는 매번, shared는 처음 한 번만(`triggered_count`). shared에서 다시 펄스를 내려면 `reset_control_rearm()`.
6. **설계**: 회로도에서 IMU의 VDD와 VDDIO가 같은 1.8 V LDO에 물려 있고 RESET_N은 SoC GPIO다. DT의 `vdd-supply`, `vddio-supply`, `reset-gpios`를 어떻게 쓰고, 드라이버의 전원 시퀀스는 어떻게 달라지나?
   정답/힌트: 두 supply가 같은 phandle을 가리킨다. `regulator_enable`을 두 번 하면 use_count 2(실제 켜기는 한 번). 리셋은 GPIO API(H4)로, 극성은 DT 플래그로.

---

## 11. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| CCF (common clock framework) | 공용 클럭 프레임워크 | 클럭 트리·count·rate 전파를 관리하는 `drivers/clk` 코어 |
| `clk_hw` / `clk_core` / `clk` | provider 핸들 / 코어 노드 / consumer 핸들 | provider는 `clk_hw`, consumer는 `struct clk`(consumer마다 하나)를 쓴다 |
| prepare / enable | 두 단계 켜기 | 잠들 수 있는 준비 / 원자적 켜기 |
| `CLK_SET_RATE_PARENT` | 클럭 플래그 | rate 요청을 부모로 올려 보내도 된다 |
| reparent | 부모 변경 | mux가 다른 입력을 고르는 것. count도 같이 옮겨진다 |
| `assigned-clock-rates` | DT 속성 | probe 직전 platform 코어가 `clk_set_rate`를 해 준다 |
| orphan clock | 고아 클럭 | 부모가 아직 등록 안 된 클럭. rate가 0 |
| `CLK_IS_CRITICAL` | 클럭 플래그 | 절대 끄지 않는다 |
| reset controller | 리셋 컨트롤러 | 리셋 라인 묶음을 가진 블록. `reset_controller_dev`로 등록 |
| shared reset | 공유 리셋 | `deassert_count` refcount. 마지막 해제 요청이 사라질 때만 assert |
| `triggered_count` | shared reset 펄스 카운트 | shared에서 `reset_control_reset`은 처음 한 번만 |
| regulator | 레귤레이터 | 전압·전류를 공급하는 소스. PMIC LDO/buck, 로드 스위치 |
| constraints | 제약 | 보드가 허용하는 전압 범위·상태. DT `regulator-*` 속성 |
| `use_count` | 레일 사용 수 | enable한 consumer 수. 0 → 1에서 켜고 1 → 0에서 끈다 |
| dummy regulator | 가짜 레귤레이터 | supply 속성이 없을 때 주는 아무것도 안 하는 레귤레이터 |
| `regulator-fixed` | 고정 레귤레이터 | 전압 고정, GPIO로 켜고 끄는 레일 |
| ramp delay / `startup-delay-us` | 켜짐 대기 | enable 뒤 코어가 기다리는 시간 |
| tPWR-RST, tBOOT | 데이터시트 타이밍 | 레일 안정 → 리셋 해제, 리셋 해제 → 첫 버스 접근 최소 시간 |
| POR (power-on reset) | 전원 인가 리셋 | 칩 내부가 레일 상승을 보고 스스로 초기화하는 회로 |
| `devm_add_action_or_reset` | devm 액션 | 해제 때 부를 함수 등록. 등록 실패 시 즉시 호출 |

---

## 12. 요약 & 체크리스트

칩이 버스에서 응답하려면 전원, 클럭, 리셋, 대기 시간 네 가지가 순서대로 맞아야 하고, Linux에서는 이것을 regulator, CCF, reset 세 프레임워크가 맡는다. 셋은 모두 "consumer API → 코어(트리·refcount·제약) → provider ops → 하드웨어"의 같은 모양이고, 공유 자원은 refcount로 관리되어 마지막 사용자가 놓을 때만 하드웨어가 바뀐다. 클럭은 트리라서 켜기와 rate 변경이 부모로 전파되고(형제가 조용히 깨질 수 있다), 켜기는 잠들 수 있는 prepare와 원자적인 enable로 나뉜다. 리셋은 exclusive와 shared의 의미가 다르고, 레귤레이터는 보드 constraints 안에서만 움직이며 DT에 supply가 없으면 dummy가 조용히 성공을 흉내 낸다. 드라이버는 자원을 먼저 다 받고, 데이터시트 순서대로 하나씩 켜고, 끄기를 devm 액션으로 등록해 모든 실패·제거 경로가 역순으로 끄게 만든다. 진단은 `clk_summary`, `regulator_summary`, 리셋 provider 로그, 그리고 로그 타임스탬프와 scope 파형의 대조로 한다.

- [ ] 버스 무응답을 전원 → 클럭 → 리셋 → 타이밍 → 버스 순서로 점검하는 절차를 말할 수 있다
- [ ] `clk_summary`의 enable/prepare count와 hardware enable 열을 읽고, 부모 count가 왜 그 값인지 설명할 수 있다
- [ ] prepare와 enable이 나뉜 이유와, 원자 문맥에서 무엇을 부를 수 있는지 설명할 수 있다
- [ ] `CLK_SET_RATE_PARENT`, reparent, `assigned-clock-rates`가 트리를 어떻게 바꾸는지와 공유 부모의 위험을 설명할 수 있다
- [ ] gate·divider·mux 헬퍼와 커스텀 PLL ops로 클럭 provider를 짜고 `#clock-cells = <1>`로 내보낼 수 있다
- [ ] exclusive와 shared 리셋의 refcount 의미와 `-EBUSY` WARN이 나는 조건을 설명할 수 있다
- [ ] `reset_controller_dev`와 `reset_control_ops`로 리셋 provider를 짤 수 있다
- [ ] `regulator_summary`의 use/open/min/max와 consumer 줄을 읽을 수 있다
- [ ] 전압 표 기반 `regulator_desc` + `regulator_ops`로 레귤레이터 provider를 짤 수 있다
- [ ] dummy regulator와 `-EPROBE_DEFER`, `get_optional`의 차이를 설명할 수 있다
- [ ] unused 클럭·레귤레이터 정리 시점과 `clk_ignore_unused`가 디버그용인 이유를 설명할 수 있다
- [ ] 센서 전원 시퀀스를 goto 역순 정리 + `devm_add_action_or_reset`으로 짤 수 있다

## 참고 자료

- docs.kernel.org — The Common Clk Framework: https://docs.kernel.org/driver-api/clk.html
- docs.kernel.org — Reset controller API: https://docs.kernel.org/driver-api/reset.html
- docs.kernel.org — Regulator Consumer Driver Interface: https://docs.kernel.org/power/regulator/consumer.html
- docs.kernel.org — Regulator Driver Interface: https://docs.kernel.org/power/regulator/regulator.html
- docs.kernel.org — Regulator Machine Driver Interface (constraints): https://docs.kernel.org/power/regulator/machine.html
- 커널 소스 (6.18): `drivers/clk/clk.c`(`clk_disable_unused`, `clk_summary`), `drivers/clk/clk-gate.c`·`clk-divider.c`·`clk-mux.c`, `include/linux/clk-provider.h`, `drivers/reset/core.c`(`__reset_control_get_internal`, `reset_control_deassert`), `drivers/regulator/core.c`(`regulator_late_cleanup`, `regulator_bulk_enable`), `drivers/regulator/of_regulator.c`, `drivers/regulator/fixed.c` — elixir.bootlin.com에서 같은 경로로 볼 수 있다
- Bootlin — Linux kernel and driver development training (slides): https://bootlin.com/doc/training/linux-kernel/
- 이 노트의 선행·후속: D4(DT 자원 문법), G3(probe/remove, devm), H4(GPIO 리셋 핀), H5(I2C 첫 트랜잭션), N5(runtime PM)
