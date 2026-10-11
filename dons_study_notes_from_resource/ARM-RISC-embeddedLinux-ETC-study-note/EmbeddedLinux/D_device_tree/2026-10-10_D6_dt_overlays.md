# D6. Device Tree Overlay — base dtb는 그대로 두고 확장 보드만 "패치"로 얹기

> **이 노트를 다 읽으면**: overlay 소스(`/plugin/`, fragment, `target`/`target-path`, `__overlay__`, `&label` sugar)를 읽고 쓸 수 있다 · `-@`, `__symbols__`, `__fixups__`, `__local_fixups__`로 phandle이 적용 시점에 어떻게 "링크"되는지 설명할 수 있다 · 같은 `.dtbo`를 빌드 시간(`fdtoverlay`), 부팅 시간(U-Boot `fdt apply`), 런타임(`of_overlay_fdt_apply()`) 세 곳에서 적용하고 결과를 `/proc/device-tree`와 `dmesg`로 검증할 수 있다 · overlay 제거의 한계와 Raspberry Pi·BeagleBone·Jetson의 벤더 방식을 비교해 말할 수 있다
> **왜 필요한가**: "카메라 모듈 A/B, IMU 보드 rev1/rev2를 커널·base dtb 재빌드 없이 어떻게 지원하나?"는 로봇·드론 제품 라인의 일상 업무이고, 면접에서는 "DT overlay가 뭐고 phandle은 어떻게 풀리나", "런타임에 overlay를 올릴 수 있나"로 나온다
> **Don 기준 난이도**: 펌웨어의 "기본 이미지 + 패치 테이블", 링커의 심볼 해석·재배치(B3)는 이미 안다 / 새로 배울 부분은 DT 특유의 label·phandle 해석 규칙, 부트로더·커널 각각의 적용 경로, 런타임 적용이 드라이버 probe로 이어지는 알림(notifier) 체인
> **선행 노트**: D2(문법, label/phandle), D3(compatible → probe), D5(dtsi/dts 계층, `dtc`, `/proc/device-tree`), B3(링킹 — 이 노트의 fixup은 "DT용 링커"다), C3·C6(U-Boot, 커널 부팅)

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어를 양산할 때를 떠올려 보자. 같은 컨트롤러로 NAND 벤더·용량·폼팩터가 다른 SKU를 여러 개 만든다. SKU마다 펌웨어를 통째로 다시 빌드하지 않고, **기본 이미지 + SKU별 설정 테이블**(NAND 파라미터, 채널 수, 전력 한계)을 따로 굽는다. 부팅 때 펌웨어가 테이블을 읽어 기본 동작을 "덮어쓴다".

Device Tree overlay가 정확히 이것이다.

- **base dtb** = SoC + 캐리어 보드처럼 절대 안 바뀌는 하드웨어 기술. 한 번 검증하고 고정한다.
- **overlay (`.dtbo`)** = 그 위에 꽂히는 것: Raspberry Pi의 HAT, BeagleBone의 cape, Jetson 40핀 헤더에 붙인 카메라·SPI 센서, 또는 같은 보드의 공장 변형(RTC 미실장, 다른 카메라 센서).
- **적용(apply)** = overlay가 base 트리에 노드를 붙이고 속성을 바꾸는 것. 결과는 그냥 하나의 DT이고, 커널 드라이버는 overlay가 있었는지조차 모른다.

예를 들어 센서 키트라면: 메인 보드(i.MX8 / AM62 / Orin)는 하나인데, 고객 구성에 따라 IMU 보드 rev-A/rev-B, 카메라 IMX219/IMX477, GNSS 유무가 바뀐다. 조합마다 dtb를 만들면 `2 x 2 x 2 = 8`개가 되고, 보드 하나 늘 때마다 두 배가 된다. overlay로 쪼개면 base 1개 + overlay 5개다.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="85" y="20" text-anchor="middle" font-size="13" font-weight="bold">하드웨어</text>
  <text x="260" y="20" text-anchor="middle" font-size="13" font-weight="bold">소스</text>
  <text x="445" y="20" text-anchor="middle" font-size="13" font-weight="bold">dtc -@ 결과물</text>
  <text x="610" y="20" text-anchor="middle" font-size="13" font-weight="bold">적용 결과</text>
  <rect x="10" y="40" width="150" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="63" text-anchor="middle" font-size="13">베이스 보드</text>
  <text x="85" y="83" text-anchor="middle" font-size="12">SoC + 캐리어</text>
  <rect x="10" y="170" width="150" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="85" y="193" text-anchor="middle" font-size="13">확장 보드 (HAT)</text>
  <text x="85" y="213" text-anchor="middle" font-size="12">IMU + 3.3V LDO</text>
  <rect x="190" y="40" width="140" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="260" y="63" text-anchor="middle" font-size="13">d6_base.dts</text>
  <text x="260" y="83" text-anchor="middle" font-size="12">virt.dts + label</text>
  <rect x="190" y="170" width="140" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="260" y="193" text-anchor="middle" font-size="13">d6_hat.dtso</text>
  <text x="260" y="213" text-anchor="middle" font-size="12">/plugin/ + &amp;label</text>
  <rect x="370" y="40" width="150" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="445" y="63" text-anchor="middle" font-size="13">d6_base.dtb</text>
  <text x="445" y="83" text-anchor="middle" font-size="12">+ __symbols__</text>
  <rect x="370" y="170" width="150" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="445" y="193" text-anchor="middle" font-size="13">d6_hat.dtbo</text>
  <text x="445" y="213" text-anchor="middle" font-size="12">+ __fixups__</text>
  <rect x="548" y="80" width="125" height="110" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="610" y="103" text-anchor="middle" font-size="13" font-weight="bold">합쳐진 트리</text>
  <text x="610" y="127" text-anchor="middle" font-size="12">base 그대로</text>
  <text x="610" y="149" text-anchor="middle" font-size="12">+ /d6-sensor</text>
  <text x="610" y="171" text-anchor="middle" font-size="12">rtc0 disabled</text>
  <line x1="160" y1="68" x2="188" y2="68" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="160" y1="198" x2="188" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="330" y1="68" x2="368" y2="68" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="330" y1="198" x2="368" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="520" y1="68" x2="546" y2="105" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="520" y1="198" x2="546" y2="165" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="340" y="128" text-anchor="middle" font-size="12">base는 한 번 검증하고 고정</text>
  <text x="340" y="146" text-anchor="middle" font-size="12">확장 보드마다 .dtbo 하나</text>
  <text x="340" y="260" text-anchor="middle" font-size="12">펌웨어 비유: 기본 이미지 + SKU별 패치 테이블을 부팅 때 덮어쓰기</text>
</svg>
```
그림 1 — base dtb(고정)와 확장 보드 overlay(교체 가능)를 따로 만들고, 적용 시점에 하나의 트리로 합친다. 이 노트의 실습 전체가 이 그림 그대로다.

이 노트에서 하는 것:

1. QEMU `virt` 머신의 DT(`/var/lab/out/virt.dts`)에 label을 붙여 **base**로 쓰고, "IMU HAT" overlay(`d6-sensor` 노드 + 레귤레이터 노드 + 기존 RTC 노드 `status` 변경)를 만든다.
2. 같은 `.dtbo`를 **세 군데**에서 적용한다 — 빌드 시간 `fdtoverlay`(§4), 부팅 시간 U-Boot `fdt apply`(§5), 런타임 커널 모듈(§6). 세 경우 모두 우리가 만든 드라이버 `d6_sensor`가 probe 되는 것까지 QEMU 게스트에서 확인한다.
3. 일부러 망가뜨린다 — `-@` 없이 빌드, `fdt resize` 생략, 같은 overlay 두 번 적용, 하드웨어에 없는 노드.

> 실행 환경: Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`, dtc/fdtoverlay `DTC 1.7.2`) 안에서 빌드하고, QEMU `virt`(cortex-a57, 512 MB, TCG) 게스트에서 랩 커널 **Linux 6.18.55**(`CONFIG_OF_OVERLAY=y`)와 **U-Boot v2026.10**으로 실행했다. 출력의 타임스탬프·시간 측정값은 실행마다 다르다.

---

## 1. overlay 소스 문법 — `/plugin/`, fragment, `__overlay__`, 그리고 sugar

### 1.1 직관 — "어디에(target) 무엇을(__overlay__) 붙여라"의 목록

overlay 파일은 **패치 목록**이다. 각 항목(fragment)이 두 가지를 말한다.

- **어디에**: base 트리의 어느 노드에 붙일지. label로 가리키면 `target = <&label>`, 경로로 가리키면 `target-path = "/경로"`.
- **무엇을**: `__overlay__ { ... }` 안의 속성은 target 노드의 속성으로 **추가되거나 덮어쓰이고**, 안의 자식 노드는 target의 자식으로 **붙는다**(이미 같은 이름이 있으면 합쳐진다).

파일 맨 위의 `/plugin/;`은 dtc에게 "이건 완결된 트리가 아니라 조각이다. 모르는 label이 나와도 에러 내지 말고 나중에 풀 수 있게 기록해 둬라"라고 알리는 선언이다. B3의 `.o` 파일이 "`printf` 주소는 모르지만 relocation으로 남겨 둔다"와 같은 태도다.

### 1.2 명시(fragment) 문법

```dts
// d6_hat_frag.dtso — 같은 overlay를 fragment/__overlay__ 원래 문법으로
/dts-v1/;
/plugin/;

/ {
	fragment@0 {
		target-path = "/";
		__overlay__ {
			d6_vdd: d6-vdd-3v3 {
				compatible = "regulator-fixed";
				regulator-name = "d6-vdd-3v3";
				regulator-min-microvolt = <3300000>;
				regulator-max-microvolt = <3300000>;
			};
			d6-sensor {
				compatible = "d6,sensor";
				label = "imu-hat";
				d6,sample-rate-hz = <200>;
				vdd-supply = <&d6_vdd>;
				clocks = <&apb_pclk>;
				reset-gpios = <&gpio0 5 0>;
			};
		};
	};

	fragment@1 {
		target = <&rtc0>;
		__overlay__ {
			status = "disabled";
		};
	};
};
```

- `fragment@0`: 루트(`/`)에 노드 두 개를 붙인다. 하나는 HAT 위 3.3 V LDO(`regulator-fixed`), 하나는 IMU(`d6,sensor`).
- `d6-sensor`는 참조를 세 개 갖는다. `&d6_vdd`(overlay **안**의 노드), `&apb_pclk`·`&gpio0`(**base**의 노드). 이 차이가 §2의 핵심이다.
- `fragment@1`: base의 `rtc0` 노드에 `status = "disabled"`를 쓴다 → "이 변형 보드에는 RTC가 실장되지 않았다". **기존 노드의 속성 변경** 예다.

### 1.3 sugar 문법 — 요즘 쓰는 모양

fragment를 손으로 쓰는 건 번거로워서 지금의 dtc는 overlay에서도 `&label { ... };`을 쓸 수 있게 해 준다. 경로로 가리키려면 `&{/경로} { ... };`.

```dts
// d6_hat.dtso — "IMU HAT" 확장 보드 overlay (sugar 문법)
/dts-v1/;
/plugin/;

&{/} {
	d6_vdd: d6-vdd-3v3 {
		compatible = "regulator-fixed";
		regulator-name = "d6-vdd-3v3";
		regulator-min-microvolt = <3300000>;
		regulator-max-microvolt = <3300000>;
	};

	d6-sensor {
		compatible = "d6,sensor";
		label = "imu-hat";
		d6,sample-rate-hz = <200>;
		vdd-supply = <&d6_vdd>;        /* overlay 안의 노드 -> __local_fixups__ */
		clocks = <&apb_pclk>;          /* base의 노드 -> __fixups__ */
		reset-gpios = <&gpio0 5 0>;    /* base의 노드 -> __fixups__ */
	};
};

&rtc0 {
	status = "disabled";               /* 이 변형 보드에는 RTC가 없다 */
};
```

`&{/}`는 "경로 `/`" → dtc가 `fragment@0 { target-path = "/"; __overlay__ {...} }`로 바꾼다. `&rtc0 {...}`은 → `fragment@1 { target = <&rtc0>; __overlay__ {...} }`로 바꾼다. 정말 같은지 바이트 단위로 확인하자.

### 1.4 예제 1 — 두 문법은 같은 바이너리가 된다

무엇을 확인하나: sugar 문법과 명시 문법을 각각 `dtc -@`로 컴파일해서 결과 `.dtbo`가 바이트 단위로 같은지 본다.

```sh
S=/Users/donh/.elinux_scratch/D6; cd /var/tmp/D6
dtc -@ -I dts -O dtb -o d6_hat.dtbo      $S/d6_hat.dtso
dtc -@ -I dts -O dtb -o d6_hat_frag.dtbo $S/d6_hat_frag.dtso
dtc    -I dts -O dtb -o d6_hat_nosym.dtbo $S/d6_hat.dtso
ls -l *.dtbo
cmp d6_hat.dtbo d6_hat_frag.dtbo && echo IDENTICAL
```

```text
-rw-rw-r-- 1 donh donh  997 Oct 10 10:24 d6_hat.dtbo
-rw-rw-r-- 1 donh donh  997 Oct 10 10:24 d6_hat_frag.dtbo
-rw-rw-r-- 1 donh donh  922 Oct 10 10:24 d6_hat_nosym.dtbo
IDENTICAL
```

출력에서 볼 것: `cmp`가 아무 차이도 찾지 못했다 — sugar는 정말 "설탕"일 뿐, 바이너리 포맷에는 fragment만 존재한다. `-@` 유무로 75바이트 차이가 나는데, 그게 무엇인지는 §2에서 본다.

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="320" height="180" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="32" text-anchor="middle" font-size="13" font-weight="bold">sugar 문법 (d6_hat.dtso)</text>
  <text x="25" y="60" font-size="12">&amp;{/} {</text>
  <text x="25" y="80" font-size="12">    d6_vdd: d6-vdd-3v3 { ... };</text>
  <text x="25" y="100" font-size="12">    d6-sensor { ... };</text>
  <text x="25" y="120" font-size="12">};</text>
  <text x="25" y="148" font-size="12">&amp;rtc0 { status = "disabled"; };</text>
  <rect x="350" y="10" width="320" height="180" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="32" text-anchor="middle" font-size="13" font-weight="bold">명시 문법 (d6_hat_frag.dtso)</text>
  <text x="365" y="56" font-size="12">fragment@0 {</text>
  <text x="365" y="73" font-size="12">    target-path = "/";</text>
  <text x="365" y="90" font-size="12">    __overlay__ { d6_vdd, d6-sensor };</text>
  <text x="365" y="107" font-size="12">};</text>
  <text x="365" y="128" font-size="12">fragment@1 {</text>
  <text x="365" y="145" font-size="12">    target = &lt;&amp;rtc0&gt;;</text>
  <text x="365" y="162" font-size="12">    __overlay__ { status = ... };</text>
  <text x="365" y="176" font-size="12">};</text>
  <rect x="190" y="220" width="300" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="240" text-anchor="middle" font-size="13">dtc -@ 결과: 997 B</text>
  <text x="340" y="257" text-anchor="middle" font-size="12">cmp → IDENTICAL (바이트 단위 동일)</text>
  <line x1="170" y1="190" x2="270" y2="218" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="510" y1="190" x2="410" y2="218" stroke="currentColor" marker-end="url(#ah2)"/>
</svg>
```
그림 2 — `&{/path}`는 `target-path`로, `&label`은 `target = <&label>`로 바뀐다. 바이너리에는 fragment 형식만 남는다.

### 1.5 함정 — `/plugin;`이 아니라 `/plugin/;`

문서나 블로그를 보고 손으로 치다 보면 슬래시 하나를 빠뜨린다. dtc 에러는 친절하지 않다.

```sh
printf "/dts-v1/;\n/plugin;\n&{/} { a = <1>; };\n" > t1.dtso
dtc -@ -O dtb -o t1.dtbo t1.dtso
```

```text
Error: t1.dtso:2.2-8 syntax error
FATAL ERROR: Unable to parse input tree
```

출력에서 볼 것: 2번째 줄 2~8열(`plugin;`)에서 그냥 "syntax error"다. `/dts-v1/;`과 `/plugin/;`은 둘 다 **슬래시로 감싼 지시어**라고 외워 두자.

### 1.6 overlay가 할 수 있는 것과 없는 것

| 하고 싶은 것 | overlay로? | 방법 / 이유 |
|---|---|---|
| 새 노드 추가 | 된다 | `__overlay__` 안에 자식 노드 |
| 기존 속성 값 바꾸기 | 된다 | 같은 이름 속성을 쓰면 덮어쓴다 (`status`, `clock-frequency` 등) |
| 기존 노드에 속성 추가 | 된다 | 새 이름 속성 |
| 기존 노드·속성 **삭제** | 안 된다 | overlay 포맷에 delete가 없다. `/delete-node/`는 dtc 소스 단계 문법이라 `.dtbo`에 남지 않는다 → 대신 `status = "disabled"` |
| `#address-cells`/`#size-cells` 변경 | 커널은 거부 | `ERROR: changing value of #address-cells is not allowed` (drivers/of/overlay.c) |
| 다른 overlay가 만든 label 참조 | 된다 | 앞 overlay를 `-@`로 컴파일해야 (§4.4) |

---

## 2. `-@`와 `__symbols__` / `__fixups__` / `__local_fixups__` — DT의 링커

### 2.1 직관 — B3의 링커와 1:1 대응

overlay가 `reset-gpios = <&gpio0 5 0>`이라고 쓰면, 바이너리 DT에서 `&gpio0`은 **phandle**(노드를 가리키는 32비트 정수 ID)이어야 한다. 그런데 overlay를 컴파일하는 순간 dtc는 base 트리를 모른다. 그래서:

| B3 (ELF 링킹) | D6 (DT overlay) |
|---|---|
| `.o`의 미정의 심볼 `U printf` | overlay의 `&gpio0` (base에 있는 label) |
| relocation 엔트리 "섹션 X 오프셋 Y에 printf 주소를 써라" | `__fixups__` 엔트리 `gpio0 = "경로:속성:바이트오프셋"` |
| 공유 라이브러리의 export 심볼 테이블(`.dynsym`) | base의 `__symbols__` 노드 (label → 경로) |
| 심볼 주소 | 그 경로 노드의 `phandle` 값 |
| PIC 코드의 내부 참조를 로드 주소만큼 밀기 | `__local_fixups__`: overlay 내부 phandle을 delta만큼 밀기 |
| `ld.so`가 실행 시간에 링크 | `fdtoverlay` / U-Boot / 커널 resolver가 적용 시간에 링크 |

`dtc -@`의 의미는 두 가지다.

- **base에 쓰면**: 모든 label을 `/__symbols__` 노드에 "label = 경로"로 기록하고, label이 붙은 노드에 phandle을 만들어 준다 → **export 테이블**.
- **overlay에 쓰면**: overlay 안의 label(예: `d6_vdd`)도 `__symbols__`에 기록 → 적용 후 base의 `__symbols__`에 합쳐져서 **다음 overlay가 참조**할 수 있다.

`__fixups__`와 `__local_fixups__`는 `/plugin/`만 있으면 `-@` 없이도 생긴다. 그래서 위 예제에서 `-@` 유무 차이(75 B)는 overlay 자신의 `__symbols__` 노드 크기다.

### 2.2 예제 2 — base 만들기: virt.dts에 label 달고 `-@`로 컴파일

무엇을 확인하나: QEMU가 덤프한 `virt.dts`에는 label이 하나도 없다(phandle이 숫자로만 박혀 있다). label 5개를 붙이고 `-@`로 컴파일하면 `__symbols__`와 새 phandle이 생기는지 본다.

```sh
#!/bin/sh
# mkbase.sh: virt.dts 복사본에 label 5개를 붙여 d6_base.dts를 만든다
sed -e 's/^\tpl061@9030000 {/\tgpio0: pl061@9030000 {/' \
    -e 's/^\tpl031@9010000 {/\trtc0: pl031@9010000 {/' \
    -e 's/^\tpl011@9000000 {/\tuart0: pl011@9000000 {/' \
    -e 's/^\tintc@8000000 {/\tgic: intc@8000000 {/' \
    -e 's/^\tapb-pclk {/\tapb_pclk: apb-pclk {/' \
    /var/lab/out/virt.dts > d6_base.dts
```

```sh
cd /var/tmp/D6 && sh $S/mkbase.sh && diff /var/lab/out/virt.dts d6_base.dts
W="-W no-clocks_property -W no-gpios_property"
dtc $W    -I dts -O dtb -o d6_base_nosym.dtb d6_base.dts
dtc $W -@ -I dts -O dtb -o d6_base.dtb       d6_base.dts
dtc -I dtb -O dts d6_base.dtb 2>/dev/null > d6_base_dec.dts
sed -n 396,405p d6_base_dec.dts
grep -n "phandle" d6_base_dec.dts
```

```text
273c273
< 	pl061@9030000 {
---
> 	gpio0: pl061@9030000 {
300c300
< 	pl031@9010000 {
---
> 	rtc0: pl031@9010000 {
308c308
< 	pl011@9000000 {
---
> 	uart0: pl011@9000000 {
321c321
< 	intc@8000000 {
---
> 	gic: intc@8000000 {
376c376
< 	apb-pclk {
---
> 	apb_pclk: apb-pclk {
	__symbols__ {
		gpio0 = "/pl061@9030000";
		rtc0 = "/pl031@9010000";
		uart0 = "/pl011@9000000";
		gic = "/intc@8000000";
		apb_pclk = "/apb-pclk";
	};
};
274:		phandle = <0x8004>;
306:		phandle = <0x01>;
315:		phandle = <0x02>;
324:		phandle = <0x8002>;
334:			phandle = <0x8003>;
365:			phandle = <0x8001>;
379:		phandle = <0x8000>;
```

출력에서 볼 것:

- `__symbols__`는 그냥 **문자열 속성을 가진 평범한 노드**다. "label → 전체 경로" 표 하나.
- QEMU가 만든 phandle은 `0x8000`대(apb-pclk `0x8000`, gic `0x8002`, pl061 `0x8004`)다. 원래 아무도 참조하지 않던 `pl031`(rtc0)과 `pl011`(uart0)은 phandle이 없었는데, `-@`가 label 붙은 노드에 **새 phandle `0x01`, `0x02`를 만들어 줬다**. overlay가 `target = <&rtc0>`로 가리키려면 rtc0에 phandle이 있어야 하기 때문이다.
- `-W no-clocks_property ...`는 QEMU 덤프가 `clocks = <0x8000>`처럼 숫자 phandle을 쓰기 때문에 나는 경고를 끄는 것이다(경고일 뿐 결과는 같다). 사람이 쓰는 dts라면 `clocks = <&apb_pclk>`로 쓴다.

### 2.3 예제 3 — overlay를 역컴파일해서 "미해결 참조" 보기

무엇을 확인하나: `d6_hat.dtbo` 안에서 base를 가리키는 참조와 overlay 내부 참조가 각각 어떻게 기록되는지 본다.

```sh
dtc -I dtb -O dts d6_hat.dtbo
```

```text
/dts-v1/;

/ {

	fragment@0 {
		target-path = "/";

		__overlay__ {

			d6-vdd-3v3 {
				compatible = "regulator-fixed";
				regulator-name = "d6-vdd-3v3";
				regulator-min-microvolt = <0x325aa0>;
				regulator-max-microvolt = <0x325aa0>;
				phandle = <0x01>;
			};

			d6-sensor {
				compatible = "d6,sensor";
				label = "imu-hat";
				d6,sample-rate-hz = <0xc8>;
				vdd-supply = <0x01>;
				clocks = <0xffffffff>;
				reset-gpios = <0xffffffff 0x05 0x00>;
			};
		};
	};

	fragment@1 {
		target = <0xffffffff>;

		__overlay__ {
			status = "disabled";
		};
	};

	__symbols__ {
		d6_vdd = "/fragment@0/__overlay__/d6-vdd-3v3";
	};

	__fixups__ {
		apb_pclk = "/fragment@0/__overlay__/d6-sensor:clocks:0";
		gpio0 = "/fragment@0/__overlay__/d6-sensor:reset-gpios:0";
		rtc0 = "/fragment@1:target:0";
	};

	__local_fixups__ {

		fragment@0 {

			__overlay__ {

				d6-sensor {
					vdd-supply = <0x00>;
				};
			};
		};
	};
};
<stdout>: Warning (gpios_property): /fragment@0/__overlay__/d6-sensor:reset-gpios: Could not get phandle node for (cell 1)
```

출력에서 볼 것:

- **base 참조**(`clocks`, `reset-gpios`의 첫 셀, `target`)는 자리표시자 `0xffffffff`로 채워졌다. 그리고 `__fixups__`에 "**label = 경로:속성:바이트 오프셋**"이 남았다. `gpio0 = ".../d6-sensor:reset-gpios:0"`은 "`reset-gpios` 속성의 0번째 바이트에 gpio0의 phandle을 써라"다. 정확히 relocation 엔트리다.
- **overlay 내부 참조**(`vdd-supply = <0x01>`)는 overlay 안에서 이미 phandle `0x01`로 확정됐다. 하지만 base에도 `0x01`(rtc0!)이 있으니 그대로 합치면 충돌한다. 그래서 `__local_fixups__`가 "`d6-sensor/vdd-supply`의 오프셋 0은 **내부 phandle이니 적용할 때 delta만큼 밀어라**"고 기록한다. 값 `<0x00>`이 바이트 오프셋 목록이다.
- 마지막 Warning은 역컴파일러가 `0xffffffff`를 phandle로 해석 못 해서 내는 것이라 무시해도 된다.

### 2.4 적용할 때 일어나는 일 — 4단계

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="320" height="180" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="32" text-anchor="middle" font-size="13" font-weight="bold">d6_hat.dtbo (overlay)</text>
  <text x="22" y="58" font-size="12">reset-gpios = &lt;0xffffffff 5 0&gt;</text>
  <text x="22" y="78" font-size="12">vdd-supply  = &lt;0x1&gt;   (내부 phandle)</text>
  <text x="22" y="104" font-size="12" font-weight="bold">__fixups__:</text>
  <text x="22" y="124" font-size="12">  gpio0 = ".../d6-sensor:reset-gpios:0"</text>
  <text x="22" y="150" font-size="12" font-weight="bold">__local_fixups__:</text>
  <text x="22" y="170" font-size="12">  d6-sensor/vdd-supply = &lt;0&gt;</text>
  <rect x="350" y="10" width="320" height="180" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="32" text-anchor="middle" font-size="13" font-weight="bold">d6_base.dtb (base / live tree)</text>
  <text x="362" y="58" font-size="12" font-weight="bold">__symbols__:</text>
  <text x="362" y="78" font-size="12">  gpio0 = "/pl061@9030000"</text>
  <text x="362" y="104" font-size="12">/pl061@9030000:</text>
  <text x="362" y="124" font-size="12">  phandle = &lt;0x8004&gt;</text>
  <text x="362" y="160" font-size="12">base의 가장 큰 phandle = 0x8004</text>
  <line x1="300" y1="120" x2="400" y2="76" stroke="#d0564a" stroke-width="1.5" marker-end="url(#ah3)"/>
  <text x="333" y="96" font-size="12">①</text>
  <line x1="470" y1="82" x2="470" y2="108" stroke="#d0564a" stroke-width="1.5" marker-end="url(#ah3)"/>
  <text x="478" y="98" font-size="12">②</text>
  <rect x="10" y="228" width="660" height="128" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="250" text-anchor="middle" font-size="13" font-weight="bold">적용 결과</text>
  <text x="24" y="276" font-size="12">③ label gpio0 → 경로 → phandle 0x8004 를 reset-gpios 오프셋 0 에 기록 → &lt;0x8004 5 0&gt;</text>
  <text x="24" y="300" font-size="12">④ overlay 내부 phandle + delta (겹치지 않게), __local_fixups__ 위치도 + delta</text>
  <text x="24" y="324" font-size="12">   fdtoverlay · U-Boot (libfdt): delta = 0x8004 → d6_vdd 와 vdd-supply = 0x8005</text>
  <text x="24" y="346" font-size="12">   Linux 커널 resolver: delta = 0x8004 + 1 → 0x8006  (§6.4 실측)</text>
  <line x1="170" y1="190" x2="170" y2="226" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="510" y1="190" x2="510" y2="226" stroke="currentColor" marker-end="url(#ah3)"/>
</svg>
```
그림 3 — phandle 해석은 링커와 같다. ①② `__fixups__`의 label을 base `__symbols__`에서 찾아 경로 → phandle을 얻고, ③ 지정된 속성·오프셋에 써 넣는다. ④ overlay 내부 phandle은 base와 겹치지 않게 전부 밀어 준다.

핵심 결론:

- **base가 `-@` 없이 컴파일되면** `__symbols__`가 없으니 ①에서 실패한다. "overlay가 안 붙는다" 문제의 1순위 원인이다(§4.3, §5.4, §6.5에서 세 경로 모두 재현).
- overlay는 **label 이름**으로 base를 가리키지 phandle 숫자로 가리키지 않는다. 그래서 phandle 번호가 전혀 다른 base에도 같은 `.dtbo`가 붙는다(§5에서 실제로 다른 base에 붙여 본다). ABI가 "label 이름"인 셈이다 — 벤더 BSP에서 label 이름을 함부로 바꾸면 안 되는 이유.

---

## 3. 적용 지점 세 곳 — 빌드 / 부팅 / 런타임

같은 `.dtbo`라도 **누가, 언제** 적용하느냐에 따라 성격이 완전히 다르다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="210" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="32" text-anchor="middle" font-size="14" font-weight="bold">① 빌드 시간</text>
  <text x="115" y="50" text-anchor="middle" font-size="12">호스트 PC</text>
  <rect x="235" y="10" width="210" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="32" text-anchor="middle" font-size="14" font-weight="bold">② 부팅 시간</text>
  <text x="340" y="50" text-anchor="middle" font-size="12">부트로더 · 펌웨어</text>
  <rect x="460" y="10" width="210" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="565" y="32" text-anchor="middle" font-size="14" font-weight="bold">③ 런타임</text>
  <text x="565" y="50" text-anchor="middle" font-size="12">Linux 커널</text>
  <rect x="10" y="70" width="210" height="150" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0"/>
  <text x="22" y="94" font-size="12">fdtoverlay (dtc 패키지)</text>
  <text x="22" y="118" font-size="12">Kbuild 합성 dtb (-dtbs)</text>
  <text x="22" y="142" font-size="12">Yocto · Buildroot 이미지</text>
  <text x="22" y="166" font-size="12">결과: 변형마다 dtb 1개</text>
  <text x="22" y="200" font-size="12" font-weight="bold">실습 §4</text>
  <rect x="235" y="70" width="210" height="150" rx="6" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c"/>
  <text x="247" y="94" font-size="12">U-Boot fdt apply</text>
  <text x="247" y="118" font-size="12">extlinux fdtoverlays</text>
  <text x="247" y="142" font-size="12">FIT 설정 (#conf-a#conf-b)</text>
  <text x="247" y="166" font-size="12">RPi 펌웨어 config.txt</text>
  <text x="247" y="200" font-size="12" font-weight="bold">실습 §5</text>
  <rect x="460" y="70" width="210" height="150" rx="6" fill="#d0564a" fill-opacity="0.06" stroke="#d0564a"/>
  <text x="472" y="94" font-size="12">of_overlay_fdt_apply()</text>
  <text x="472" y="118" font-size="12">드라이버가 dtbo 내장</text>
  <text x="472" y="142" font-size="12">(rp1, lan966x, FPGA)</text>
  <text x="472" y="166" font-size="12">configfs: 다운스트림만</text>
  <text x="472" y="200" font-size="12" font-weight="bold">실습 §6</text>
  <line x1="12" y1="248" x2="666" y2="248" stroke="currentColor" stroke-width="1.5" marker-start="url(#ah4)" marker-end="url(#ah4)"/>
  <text x="20" y="274" font-size="12">고정 · 검증 쉬움 · 커널은 overlay를 모름</text>
  <text x="660" y="274" text-anchor="end" font-size="12">유연 · 핫플러그 · 제거가 위험</text>
</svg>
```
그림 4 — 왼쪽으로 갈수록 결과가 "그냥 dtb 하나"라 검증·재현이 쉽고, 오른쪽으로 갈수록 유연하지만 커널 상태가 복잡해진다. 제품은 대부분 ①②를 쓰고, ③은 FPGA·PCIe 카드처럼 꽂혔다는 걸 런타임에야 아는 경우에 쓴다.

| | ① 빌드 시간 | ② 부팅 시간 | ③ 런타임 |
|---|---|---|---|
| 누가 | `fdtoverlay`, Kbuild | U-Boot, RPi 펌웨어, UEFI(Jetson) | 커널 `drivers/of/overlay.c` |
| base에 필요한 것 | `-@` (label 참조 시) | `-@` + 여유 공간(`fdt resize`) | `-@`로 만든 base가 **live tree**로 부팅됐어야 |
| 실패하면 | 빌드 에러 → 가장 안전 | 부팅 실패, 블롭이 깨질 수 있음 | 부분 적용 가능, 드라이버 상태 꼬임 |
| 커널이 보기에 | 평범한 dtb | 평범한 dtb | 노드가 생겼다 사라짐 (notifier) |
| 대표 사용처 | 양산 이미지, 고정 변형 | HAT/cape, 카메라 선택 | FPGA 비트스트림, PCIe 복합 장치 |

---

## 4. 빌드 시간 — `fdtoverlay`로 오프라인 병합

### 4.1 예제 4 — base + overlay → merged.dtb

무엇을 확인하나: `fdtoverlay`로 합친 결과를 역컴파일해서 base와 diff를 떠 보고, 그림 3의 ③④가 실제로 일어났는지 본다.

```sh
fdtoverlay -i d6_base.dtb -o d6_merged.dtb d6_hat.dtbo && echo "fdtoverlay rc=$?"
ls -l d6_base.dtb d6_merged.dtb
dtc -I dtb -O dts d6_merged.dtb 2>/dev/null > d6_merged.dts
diff d6_base_dec.dts d6_merged.dts
```

```text
fdtoverlay rc=0
-rw-rw-r-- 1 donh donh 7784 Oct 10 10:24 d6_base.dtb
-rw-rw-r-- 1 donh donh 8206 Oct 10 10:24 d6_merged.dtb
10a11,27
> 	d6-sensor {
> 		reset-gpios = <0x8004 0x05 0x00>;
> 		clocks = <0x8000>;
> 		vdd-supply = <0x8005>;
> 		d6,sample-rate-hz = <0xc8>;
> 		label = "imu-hat";
> 		compatible = "d6,sensor";
> 	};
> 
> 	d6-vdd-3v3 {
> 		phandle = <0x8005>;
> 		regulator-max-microvolt = <0x325aa0>;
> 		regulator-min-microvolt = <0x325aa0>;
> 		regulator-name = "d6-vdd-3v3";
> 		compatible = "regulator-fixed";
> 	};
> 
300a318
> 		status = "disabled";
396a415
> 		d6_vdd = "/d6-vdd-3v3";
```

출력에서 볼 것:

- `reset-gpios`의 `0xffffffff`가 `0x8004`(pl061)로, `clocks`가 `0x8000`(apb-pclk)으로 바뀌었다 → `__fixups__` 해석(③).
- overlay 내부 phandle `0x01`이 `0x8005`가 됐다(base 최댓값 `0x8004` + 1). 그리고 `vdd-supply`도 같이 `0x8005`로 따라갔다 → `__local_fixups__`(④). 만약 이걸 안 했다면 `vdd-supply = <0x01>`이 rtc0을 가리켰을 것이다.
- 300번째 줄 근처(pl031)에 `status = "disabled"`가 추가됐고, `__symbols__`에 `d6_vdd`가 합쳐졌다. 결과물에 `fragment@`, `__fixups__` 같은 흔적은 남지 않는다 — **그냥 평범한 dtb**다. 속성 순서가 뒤집혀 보이는 건 libfdt가 속성을 앞쪽에 끼워 넣기 때문이고 의미는 같다.

### 4.2 예제 5 — 합친 dtb로 부팅하고 드라이버가 probe 되는지

무엇을 확인하나: `compatible = "d6,sensor"` 노드에 붙는 플랫폼 드라이버를 만들고, `-dtb d6_merged.dtb`로 부팅했을 때 probe가 되는지, base로 부팅했을 때와 무엇이 다른지 본다. 드라이버는 overlay가 준 세 가지 참조(clock, regulator, gpio)를 모두 실제로 얻어 본다 — phandle이 잘못 풀렸다면 여기서 실패한다.

```c
// SPDX-License-Identifier: GPL-2.0
/* d6_sensor: compatible = "d6,sensor" 노드에 붙는 가짜 IMU 드라이버 */
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>

static int d6_sensor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const char *label = "?";
	u32 rate = 0;
	struct clk *clk;
	struct regulator *vdd;
	struct gpio_desc *rst;

	device_property_read_string(dev, "label", &label);
	device_property_read_u32(dev, "d6,sample-rate-hz", &rate);
	clk = devm_clk_get_enabled(dev, NULL);          /* clocks = <&apb_pclk> */
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "no clock\n");
	vdd = devm_regulator_get(dev, "vdd");           /* vdd-supply = <&d6_vdd> */
	if (IS_ERR(vdd))
		return dev_err_probe(dev, PTR_ERR(vdd), "no vdd\n");
	rst = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW); /* reset-gpios */
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "no reset gpio\n");

	dev_info(dev, "probe: label=%s rate=%u Hz clk=%lu Hz vdd=%d uV reset=gpio%d of_node=%pOF\n",
		 label, rate, clk_get_rate(clk), regulator_get_voltage(vdd),
		 desc_to_gpio(rst), dev_of_node(dev));
	return 0;
}

static void d6_sensor_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "remove\n");
}

static const struct of_device_id d6_sensor_of_match[] = {
	{ .compatible = "d6,sensor" },
	{ }
};
MODULE_DEVICE_TABLE(of, d6_sensor_of_match);

static struct platform_driver d6_sensor_driver = {
	.probe  = d6_sensor_probe,
	.remove = d6_sensor_remove,
	.driver = {
		.name = "d6_sensor",
		.of_match_table = d6_sensor_of_match,
	},
};
module_platform_driver(d6_sensor_driver);
MODULE_DESCRIPTION("D6 overlay demo sensor driver");
MODULE_LICENSE("GPL");
```

`Kbuild`는 한 줄(`obj-m += d6_sensor.o`). 빌드는 랩 커널 트리에 대해 한다.

```sh
cd /var/tmp/D6/drv && make -C /var/lab/out/linux-build M=$PWD modules
```

```text
  CC [M]  d6_sensor.o
  MODPOST Module.symvers
  LD [M]  d6_sensor.ko
```

게스트에서 돌릴 확인 스크립트(`d6_boot.sh`)와 initramfs 재포장 스크립트:

```sh
#!/bin/sh
# d6_boot.sh: 부팅 직후 overlay 결과 확인 (offline/U-Boot 경로 공통)
echo "== /proc/device-tree 의 d6 노드:"; ls -d /proc/device-tree/d6-* 2>&1 | cat
echo "== rtc status: $(cat /proc/device-tree/pl031@9010000/status 2>&1)"
echo "== /sys/class/rtc: $(ls /sys/class/rtc)"
insmod /d6_sensor.ko
dmesg | grep -E "d6_sensor d6|reg-fixed"
ls -l /sys/bus/platform/devices/d6-sensor/driver 2>&1 | cat
```

```sh
#!/bin/sh
# mkinitrd.sh: 랩 rootfs 복사 + d6 파일들 → d6_initrd.gz
set -e
cd /var/tmp/D6
rm -rf rootfs && mkdir rootfs && cp -a /var/lab/build/rootfs/. rootfs/
cp drv/d6_sensor.ko rootfs/
cp ovl/d6_ovl.ko ovl/d6_trace.ko rootfs/
cp /Users/donh/.elinux_scratch/D6/*.sh rootfs/
(cd rootfs && find . | cpio -o -H newc --quiet | gzip) > d6_initrd.gz
```

base와 merged로 한 번씩 부팅한다.

```sh
cd /var/tmp/D6 && sh $S/mkinitrd.sh
for d in d6_base.dtb d6_merged.dtb; do
  echo "######## -dtb $d"
  timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
    -kernel /var/lab/out/Image -initrd d6_initrd.gz -dtb $d \
    -append "console=ttyAMA0 quiet LAB_CMD=sh~/d6_boot.sh" 2>&1 | grep -v "^\[    0.000\|LAB_CMD"
done
```

```text
######## -dtb d6_base.dtb
lab initramfs: 6.18.55 up 4.88s
== /proc/device-tree 의 d6 노드:
ls: /proc/device-tree/d6-*: No such file or directory
== rtc status: cat: can't open '/proc/device-tree/pl031@9010000/status': No such file or directory
== /sys/class/rtc: rtc0
ls: /sys/bus/platform/devices/d6-sensor/driver: No such file or directory
[    6.350261] reboot: Power down
######## -dtb d6_merged.dtb
lab initramfs: 6.18.55 up 5.98s
== /proc/device-tree 의 d6 노드:
/proc/device-tree/d6-sensor
/proc/device-tree/d6-vdd-3v3
== rtc status: disabled
== /sys/class/rtc: 
[    6.924407] d6_sensor d6-sensor: probe: label=imu-hat rate=200 Hz clk=24000000 Hz vdd=3300000 uV reset=gpio517 of_node=/d6-sensor
lrwxrwxrwx    1 0        0                0 Jan  1 00:00 /sys/bus/platform/devices/d6-sensor/driver -> ../../../bus/platform/drivers/d6_sensor
[    7.485551] reboot: Power down
```

출력에서 볼 것:

- base: d6 노드 없음, pl031에 `status` 속성 자체가 없음(없으면 "okay"로 취급), `/sys/class/rtc/rtc0` 존재.
- merged: `/proc/device-tree`에 두 노드가 생겼고, RTC는 disabled라서 **커널이 pl031 디바이스를 아예 만들지 않았다**(`/sys/class/rtc` 비어 있음). 이것이 "공장 변형을 dtb로 표현"의 실체다.
- probe 한 줄에 overlay의 모든 것이 들어 있다: `label`·`rate` 속성, `clk=24000000`(fixup된 `&apb_pclk` → 24 MHz fixed-clock), `vdd=3300000`(local fixup된 `&d6_vdd` → 우리 regulator), `reset=gpio517`(fixup된 `&gpio0` → pl061의 5번 라인, gpiolib 번호 512 + 5).

### 4.3 예제 6 — 오프라인 에러 세 가지: `-@` 없는 base, 중첩 overlay, 두 번 적용

무엇을 확인하나: (a) `-@` 없이 만든 base에 붙이기, (b) 두 번째 overlay가 첫 overlay의 label을 참조하는 "쌓기", (c) 같은 overlay를 두 번 적용하기.

두 번째 overlay(`d6_hat2.dtso`)는 첫 overlay가 만든 `d6_vdd`를 참조한다(예: HAT rev-B는 LDO 이름만 바뀜).

```dts
/dts-v1/;
/plugin/;

/* 두 번째 overlay: 첫 overlay가 만든 label(d6_vdd)을 참조한다 */
&d6_vdd {
	regulator-name = "d6-vdd-rev-b";
};
```

```sh
echo ---nosym; fdtoverlay -i d6_base_nosym.dtb -o x.dtb d6_hat.dtbo; echo rc=$?
dtc -@ -O dtb -o d6_hat2.dtbo $S/d6_hat2.dtso
echo "== stack (hat with -@)"
fdtoverlay -i d6_base.dtb -o d6_stack.dtb d6_hat.dtbo d6_hat2.dtbo && fdtget -t s d6_stack.dtb /d6-vdd-3v3 regulator-name
echo "== stack (hat without -@)"
fdtoverlay -i d6_base.dtb -o d6_stack2.dtb d6_hat_nosym.dtbo d6_hat2.dtbo; echo rc=$?
echo ---twice
fdtoverlay -i d6_merged.dtb -o d6_twice.dtb d6_hat.dtbo; echo rc=$?
dtc -I dtb -O dts d6_twice.dtb 2>/dev/null > d6_twice.dts; diff d6_merged.dts d6_twice.dts && echo "no diff"
```

```text
---nosym

Failed to apply 'd6_hat.dtbo': FDT_ERR_BADOFFSET
base blob does not have a '/__symbols__' node, make sure you have compiled the base blob with '-@' option
rc=1
== stack (hat with -@)
d6-vdd-rev-b
== stack (hat without -@)

Failed to apply 'd6_hat2.dtbo': FDT_ERR_NOTFOUND
rc=1
---twice
rc=0
no diff
```

출력에서 볼 것:

- (a) dtc 1.7.2의 fdtoverlay는 친절하게 "`-@`로 컴파일하라"고 알려 준다. 오래된 버전은 `FDT_ERR_NOTFOUND` 한 줄만 낸다.
- (b) overlay를 **쌓으려면 앞 overlay도 `-@`**여야 한다. `d6_hat_nosym.dtbo`는 `d6_vdd`를 `__symbols__`에 내보내지 않았으니 두 번째 overlay가 label을 못 찾는다(`FDT_ERR_NOTFOUND`).
- (c) 오프라인 두 번 적용은 **조용히 성공하고 결과도 같다**. libfdt(dtc 1.7.x)는 이미 phandle이 있는 노드에 합칠 때 기존 phandle을 보존한다. 커널 런타임에서는 같은 상황이 **에러**다(§6.5). 같은 "두 번 적용"이 도구마다 다르게 동작한다는 점이 함정이다.

### 4.4 커널 빌드 시스템도 같은 일을 한다

리눅스 트리 자체에 "base + overlay → 합성 dtb" 규칙이 있다. `scripts/Makefile.dtbs`가 `<이름>-dtbs := base.dtb overlay.dtbo`를 보면 `fdtoverlay`를 부른다. TI 트리의 실제 예:

```sh
cd /var/lab/src/linux-6.18.55
sed -n 81,82p scripts/Makefile.dtbs
grep -n "DTC_FLAGS\|-dtbs :=" arch/arm64/boot/dts/ti/Makefile | head -4
grep -n "DTC_FLAGS" arch/arm64/boot/dts/ti/Makefile arch/arm64/boot/dts/broadcom/Makefile
```

```text
quiet_cmd_fdtoverlay = OVL $(quiet_dtb_check_tag) $@
      cmd_fdtoverlay = $(objtree)/scripts/dtc/fdtoverlay -o $@ -i $(filter %.dtb %.dtbo, $^) $(cmd_dtb_check)
68:k3-am642-evm-nand-dtbs := k3-am642-evm.dtb k3-am642-evm-nand.dtbo
87:k3-am654-gp-evm-dtbs := k3-am654-base-board.dtb \
90:k3-am654-evm-dtbs := k3-am654-base-board.dtb k3-am654-icssg2.dtbo
91:k3-am654-idk-dtbs := k3-am654-evm.dtb k3-am654-idk.dtbo k3-am654-pcie-usb2.dtbo
arch/arm64/boot/dts/ti/Makefile:293:DTC_FLAGS := -@
arch/arm64/boot/dts/broadcom/Makefile:4:DTC_FLAGS := -@
```

출력에서 볼 것: `k3-am654-idk.dtb` = EVM base + IDK overlay + PCIe/USB overlay를 **빌드 때** 합친 것이다. 그리고 TI와 Broadcom(Raspberry Pi) 디렉터리는 `DTC_FLAGS := -@`로 **모든** base dtb에 `__symbols__`를 넣는다 — 부팅 때 overlay를 붙일 것을 전제로 하기 때문이다.

---

## 5. 부팅 시간 — U-Boot `fdt addr / resize / apply`

### 5.1 흐름

U-Boot가 커널에 넘길 dtb를 메모리에 올려 둔 상태에서:

1. `fdt addr <주소>` — "작업할 FDT는 여기 있는 것"이라고 지정.
2. `fdt resize <바이트>` — 블롭 뒤에 여유 공간을 만든다. overlay는 노드·속성·문자열을 **제자리에서** 추가하므로 공간이 없으면 실패한다.
3. `fdt apply <dtbo 주소>` — libfdt `fdt_overlay_apply()`. fdtoverlay와 같은 코드다.
4. `booti <커널> <initrd>:<크기> <fdt>` — 수정된 dtb로 부팅.

실제 보드에서는 1~3 앞에 `load mmc 0:1 ${fdt_addr_r} base.dtb`, `load mmc 0:1 ${fdtoverlay_addr_r} hat.dtbo` 같은 로드가 있다. 랩에서는 SD 카드 대신 **QEMU `-device loader`로 파일을 RAM에 미리 올려 둔다**.

```
0x4000_0000  QEMU-generated DT       (U-Boot control DT, "devicetree: board")
0x4040_0000  Image  (42,740,224 B)   -> booti arg 1
0x4800_0000  d6_base_ub.dtb          -> fdt addr / fdt resize / booti arg 3
0x4810_0000  d6_hat.dtbo             -> fdt apply
0x4900_0000  d6_initrd.gz            -> booti arg 2 (addr:size)
0x5e44_f000  U-Boot malloc + relocated U-Boot (top of 512 MiB)
```

### 5.2 먼저 만난 함정 — "펌웨어로 부팅하면 하드웨어가 다르다"

처음에는 §4의 `d6_base.dtb`를 그대로 U-Boot에 넘겼다. `fdt apply`는 성공했는데 커널이 부팅 중에 죽었다(`earlycon`을 켜야 보인다 — 콘솔 드라이버가 뜨기 전에 죽기 때문).

```text
[    0.500283] Internal error: synchronous external abort: 0000000096000010 [#1]  SMP
[    0.519013] pc : amba_read_periphid+0x128/0x2a0
[    0.549328] Call trace:
[    0.550220]  amba_read_periphid+0x128/0x2a0 (P)
[    0.551681]  amba_device_add+0x98/0xbc
[    0.553232]  of_platform_bus_create+0x35c/0x4d4
[    0.555697]  of_platform_populate+0x84/0x15c
[    0.557872]  of_platform_default_populate_init+0xec/0x114
[    0.571700] Kernel panic - not syncing: Attempted to kill init! exitcode=0x0000000b
```

(핵심 줄만 남기고 레지스터 덤프 등은 생략했다.) `amba_read_periphid()`는 AMBA 주변장치의 ID 레지스터를 읽는 함수다. **없는 주소를 읽어서 버스 에러(external abort)**가 난 것이다. Don이 bring-up에서 수없이 본 "없는 블록 레지스터를 읽으면 bus fault"와 같은 장면이다. QEMU에 물어보니 원인이 나왔다.

```sh
qemu-system-aarch64 -machine virt,dumpdtb=qemu_nobios.dtb -cpu cortex-a57 -m 512M -nographic
qemu-system-aarch64 -machine virt,dumpdtb=qemu_bios.dtb -cpu cortex-a57 -m 512M -nographic -bios /var/lab/out/u-boot.bin
for f in qemu_nobios.dtb qemu_bios.dtb; do echo "$f: pl061=$(dtc -q -I dtb -O dts $f | grep -c pl061@) gpio-keys=$(dtc -q -I dtb -O dts $f | grep -c gpio-keys)"; done
```

```text
qemu_nobios.dtb: pl061=1 gpio-keys=2
qemu_bios.dtb: pl061=0 gpio-keys=0
```

`-bios`(펌웨어)로 부팅하면 QEMU virt는 **PL061 GPIO 컨트롤러를 만들지 않는다**. 그런데 우리 dtb는 pl061이 있다고 말했으니 커널이 없는 하드웨어를 probe 하다 죽었다. 교훈: **DT는 하드웨어의 "주장"일 뿐, 틀리면 커널은 그대로 믿고 죽는다.** overlay가 실제 꽂힌 보드와 안 맞을 때 생기는 일도 정확히 이것이다.

### 5.3 예제 7 — 다른 base, 같은 `.dtbo`

그래서 U-Boot 경로용 base를 따로 만든다. QEMU가 펌웨어 부팅 때 주는 DT를 덤프하고 label을 단 뒤, GPIO 컨트롤러 대신 커널의 `gpio-sim`(가상 GPIO 칩, `CONFIG_GPIO_SIM=y`) 뱅크에 **같은 label `gpio0`**을 붙인다. "보드 rev2에서 GPIO 컨트롤러가 바뀌었다"의 축소판이다.

```sh
#!/bin/sh
# mkbase_ub.sh: U-Boot(-bios) 경로용 base — pl061 없음 + label + gpio-sim 뱅크
cd /var/tmp/D6
qemu-system-aarch64 -machine virt,dumpdtb=qemu_bios.dtb -cpu cortex-a57 -m 512M \
    -nographic -bios /var/lab/out/u-boot.bin >/dev/null 2>&1
dtc -q -I dtb -O dts qemu_bios.dtb |
sed -e 's/^\tpl031@9010000 {/\trtc0: pl031@9010000 {/' \
    -e 's/^\tpl011@9000000 {/\tuart0: pl011@9000000 {/' \
    -e 's/^\tintc@8000000 {/\tgic: intc@8000000 {/' \
    -e 's/^\tapb-pclk {/\tapb_pclk: apb-pclk {/' \
    -e '/^\tchosen {/i\
\td6-gpio-sim {\
\t\tcompatible = "gpio-simulator";\
\t\tgpio0: bank0 {\
\t\t\tgpio-controller;\
\t\t\t#gpio-cells = <2>;\
\t\t\tngpios = <8>;\
\t\t\tgpio-sim,label = "d6-sim-gpio";\
\t\t};\
\t};\
' > d6_base_ub.dts
W="-W no-clocks_property -W no-gpios_property"
dtc $W -@ -I dts -O dtb -o d6_base_ub.dtb d6_base_ub.dts
dtc $W    -I dts -O dtb -o d6_base_ub_nosym.dtb d6_base_ub.dts
```

U-Boot 조작은 `expect`로 자동화한다(자동 부팅 카운트다운에서 키를 눌러 프롬프트를 잡고, 명령을 하나씩 보낸다).

```text
#!/usr/bin/expect -f
# d6_uboot.exp: U-Boot에서 fdt apply 후 booti
# 사용법: expect d6_uboot.exp <initrd크기> <base.dtb> [noboot|noresize]
set timeout 120
set rdsize [lindex $argv 0]
set base   [lindex $argv 1]
set mode   [lindex $argv 2]
spawn qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
    -bios /var/lab/out/u-boot.bin \
    -device loader,file=/var/lab/out/Image,addr=0x40400000,force-raw=on \
    -device loader,file=$base,addr=0x48000000,force-raw=on \
    -device loader,file=/var/tmp/D6/d6_hat.dtbo,addr=0x48100000,force-raw=on \
    -device loader,file=/var/tmp/D6/d6_initrd.gz,addr=0x49000000,force-raw=on
expect "Hit any key to stop autoboot"
send "\r"
set cmds [list "fdt addr 0x48000000" "fdt list /d6-sensor"]
if {$mode ne "noresize"} { lappend cmds "fdt resize 4096" }
lappend cmds "fdt apply 0x48100000"
lappend cmds "fdt print /d6-sensor" "fdt print /pl031@9010000 status" \
    "fdt print /__symbols__ d6_vdd"
if {$mode eq ""} {
    lappend cmds "setenv bootargs console=ttyAMA0 quiet LAB_CMD=sh~/d6_boot.sh"
    lappend cmds "booti 0x40400000 0x49000000:$rdsize 0x48000000"
}
foreach cmd $cmds { expect "=> "; send "$cmd\r" }
if {$mode ne ""} { expect "=> "; exit 0 }
expect "reboot: Power down"
```

```sh
cd /var/tmp/D6 && sh $S/mkbase_ub.sh && sh $S/mkinitrd.sh
expect $S/d6_uboot.exp $(printf "0x%x" $(stat -c %s d6_initrd.gz)) /var/tmp/D6/d6_base_ub.dtb 2>&1 | tr -d "\r" | grep -v "^\[    0.000"
```

```text
U-Boot 2026.10 (Oct 10 2026 - 09:59:26 -0700)
DRAM:  512 MiB
using memory 0x5e44f000-0x5f48f000 for malloc()
Core:  51 devices, 14 uclasses, devicetree: board
Flash: 64 MiB
Loading Environment from Flash...*** Warning - bad CRC, using default environment
In:    serial,usbkbd
Out:   serial,vidconsole
Err:   serial,vidconsole
No USB controllers found
Net:   eth0: virtio-net#32

Hit any key to stop autoboot: 2Hit any key to stop autoboot: 0
=> fdt addr 0x48000000
Working FDT set to 48000000
=> fdt list /d6-sensor
libfdt fdt_path_offset() returned FDT_ERR_NOTFOUND
=> fdt resize 4096
=> fdt apply 0x48100000
=> fdt print /d6-sensor
d6-sensor {
	reset-gpios = <0x00000003 0x00000005 0x00000000>;
	clocks = <0x00008000>;
	vdd-supply = <0x00008004>;
	d6,sample-rate-hz = <0x000000c8>;
	label = "imu-hat";
	compatible = "d6,sensor";
};
=> fdt print /pl031@9010000 status
status = "disabled"
=> fdt print /__symbols__ d6_vdd
d6_vdd = "/d6-vdd-3v3"
=> setenv bootargs console=ttyAMA0 quiet LAB_CMD=sh~/d6_boot.sh
=> booti 0x40400000 0x49000000:0x130d3e 0x48000000
## Flattened Device Tree blob at 48000000
   Booting using the fdt blob at 0x48000000
Working FDT set to 48000000
   Loading Ramdisk to 5d1fd000, end 5d32dd3e ... OK
   Loading Device Tree to 000000005d1f4000, end 000000005d1fcfff ... OK
Working FDT set to 5d1f4000

Starting kernel ...

lab initramfs: 6.18.55 up 1.47s
== /proc/device-tree 의 d6 노드:
/proc/device-tree/d6-gpio-sim
/proc/device-tree/d6-sensor
/proc/device-tree/d6-vdd-3v3
== rtc status: disabled
== /sys/class/rtc: 
[    1.784802] d6_sensor d6-sensor: probe: label=imu-hat rate=200 Hz clk=24000000 Hz vdd=3300000 uV reset=gpio517 of_node=/d6-sensor
lrwxrwxrwx    1 0        0                0 Jan  1 00:00 /sys/bus/platform/devices/d6-sensor/driver -> ../../../bus/platform/drivers/d6_sensor
[    1.897688] reboot: Power down
```

(터미널 제어 문자와 커널의 `[    0.000000]` 줄은 걸러냈다.) 출력에서 볼 것:

- `fdt apply` 전에는 `/d6-sensor`가 `FDT_ERR_NOTFOUND`, 후에는 노드가 있다. **성공하면 U-Boot는 아무 말도 안 한다** — 반드시 `fdt print`로 확인하는 습관.
- 같은 `d6_hat.dtbo`인데 `reset-gpios`의 phandle이 이번엔 `0x3`(gpio-sim 뱅크), `vdd-supply`는 `0x8004`(이 base의 최댓값이 `0x8003`이라서)다. §4의 `0x8004`/`0x8005`와 숫자가 다르다. **overlay는 label로 연결되므로 base의 phandle 번호와 무관**하다는 그림 3의 결론을 실측했다.
- `booti` 후 U-Boot가 dtb를 RAM 위쪽(`0x5d1f4000`)으로 옮기고 `/chosen`에 bootargs·initrd 정보를 써 넣는다(그래서 "Working FDT set to 5d1f4000"). 그 뒤 커널은 overlay가 적용된 dtb를 받아 `d6_sensor`를 probe 했다.

### 5.4 예제 8 — U-Boot 에러 두 가지: `-@` 없음, `fdt resize` 생략

무엇을 확인하나: 실패한 `fdt apply` 뒤에 작업 중이던 FDT가 어떻게 되는지 본다.

```sh
expect $S/d6_uboot.exp 0 /var/tmp/D6/d6_base_ub_nosym.dtb noboot 2>&1 | tr -d "\r" | sed -n "/=> fdt addr/,\$p"
```

```text
=> fdt addr 0x48000000
Working FDT set to 48000000
=> fdt list /d6-sensor
libfdt fdt_path_offset() returned FDT_ERR_NOTFOUND
=> fdt resize 4096
=> fdt apply 0x48100000
failed on fdt_overlay_apply(): FDT_ERR_BADOFFSET
base fdt does not have a /__symbols__ node
make sure you've compiled with -@
=> fdt print /d6-sensor
libfdt fdt_path_offset() returned FDT_ERR_BADMAGIC
=> fdt print /pl031@9010000 status
libfdt fdt_path_offset() returned FDT_ERR_BADMAGIC
=> fdt print /__symbols__ d6_vdd
libfdt fdt_path_offset() returned FDT_ERR_BADMAGIC
```

```sh
expect $S/d6_uboot.exp 0 /var/tmp/D6/d6_base_ub.dtb noresize 2>&1 | tr -d "\r" | sed -n "/=> fdt addr/,\$p"
```

```text
=> fdt addr 0x48000000
Working FDT set to 48000000
=> fdt list /d6-sensor
libfdt fdt_path_offset() returned FDT_ERR_NOTFOUND
=> fdt apply 0x48100000
failed on fdt_overlay_apply(): FDT_ERR_NOSPACE
=> fdt print /d6-sensor
libfdt fdt_path_offset() returned FDT_ERR_BADMAGIC
=> fdt print /pl031@9010000 status
libfdt fdt_path_offset() returned FDT_ERR_BADMAGIC
=> fdt print /__symbols__ d6_vdd
libfdt fdt_path_offset() returned FDT_ERR_BADMAGIC
```

출력에서 볼 것: **둘 다 실패 후 `FDT_ERR_BADMAGIC`** — base 블롭 자체가 무효가 됐다. libfdt 헤더의 계약은 "에러를 반환해도 base가 수정됐다고 가정하라(Expect the base device tree to be modified, even if the function returns an error)"이고, 실제로 `fdt_overlay.c`의 에러 경로는 반쯤 고친 블롭을 실수로 쓰지 못하게 base와 overlay의 magic을 `~0`으로 지워 버린다(`fdt_set_magic(fdt, ~0)`). 그래서 부트 스크립트에서는:

- 실패 시 **base를 다시 load** 하고 overlay 없이 부팅하는 폴백 경로를 둔다(예: `if fdt apply ${fdtoverlay_addr_r}; then ...; else load ... base.dtb; fi`).
- `fdt resize`는 overlay 크기보다 넉넉하게. U-Boot 도움말(`cmd/fdt.c`)대로 `fdt resize [<extrasize>]`는 "크기 + 4k 주소 정렬 패딩 + 선택적 extrasize"로 키운다. overlay가 크면 extrasize를 꼭 준다.

### 5.5 U-Boot의 다른 적용 방법

| 방법 | 어떻게 | 비고 |
|---|---|---|
| 직접 명령 | `fdt addr; fdt resize; fdt apply` | 위 실습. 부트 스크립트(`boot.scr`)에 넣는다 |
| extlinux.conf | 라벨에 `fdtoverlays /overlays/a.dtbo /overlays/b.dtbo` | U-Boot `boot/pxe_utils.c`의 `label_boot_fdtoverlay()`가 처리 (랩 소스에서 확인) |
| FIT 이미지 | 설정 노드에 `fdt = "base", "overlay"` 또는 `bootm addr#conf-base#conf-hat` | U-Boot 문서 `doc/usage/fit/overlay-fdt-boot.rst` |
| 빌드 시 기본 overlay | `CONFIG_OF_OVERLAY_LIST` | U-Boot 자기 control DT용 (랩 설정은 `""`) |

---

## 6. 런타임 — 커널 모듈이 `of_overlay_fdt_apply()`로 붙이기

### 6.1 직관과 upstream 현실

런타임 overlay = **이미 부팅한 커널의 live tree**(`struct device_node` 트리, `/proc/device-tree`로 보이는 그것)에 노드를 붙였다 뗐다 하는 것. API는 두 개뿐이다.

```c
int of_overlay_fdt_apply(const void *overlay_fdt, u32 overlay_fdt_size,
                         int *ret_ovcs_id, const struct device_node *base);
int of_overlay_remove(int *ovcs_id);
```

`base`(6.x에서 추가된 인자 — 예전 커널은 앞의 세 인자만 받는다)는 overlay의 `target-path`를 어느 노드 기준으로 해석할지다. `NULL`이면 루트.

**중요한 사실**: mainline에는 유저 공간에서 `.dtbo`를 던져 넣는 인터페이스가 **없다**. 흔히 보는 `/sys/kernel/config/device-tree/overlays/<이름>/dtbo` configfs 인터페이스는 Pantelis Antoniou의 패치로, upstream에 머지되지 않았고 Raspberry Pi·BeagleBoard 다운스트림 커널에만 있다. 그래서 mainline에서는 **드라이버(커널 코드)가 `.dtbo`를 품고 있다가 적용**하는 방식만 있다. 랩 소스에서 확인해 보자.

### 6.2 예제 9 — upstream에 configfs overlay가 있나, 누가 런타임 overlay를 쓰나

```sh
cd /var/lab/src/linux-6.18.55
grep -rln "configfs" drivers/of/ ; echo "configfs in drivers/of: $?"
grep -rn "of_overlay_fdt_apply" --include=*.c . | grep -v "^./drivers/of/" | head
cat drivers/misc/rp1/Makefile
grep -rln "of_overlay_notifier_register" drivers
```

```text
configfs in drivers/of: 1
./drivers/misc/rp1/rp1_pci.c:281:		err = of_overlay_fdt_apply(dtbo_start, dtbo_size, &rp1->ovcs_id,
./drivers/misc/lan966x_pci.c:131:	return of_overlay_fdt_apply(dtbo_start, dtbo_size, &data->ovcs_id, dev_of_node(data->dev));
# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_MISC_RP1)		+= rp1-pci.o
rp1-pci-objs			:= rp1_pci.o rp1-pci.dtbo.o
drivers/of/overlay.c
drivers/of/unittest.c
drivers/fpga/of-fpga-region.c
```

(`grep -rln`은 아무것도 못 찾으면 종료 코드 1을 낸다.) 출력에서 볼 것:

- `drivers/of/`에 configfs 코드가 없다 → 6.18 mainline에 configfs overlay 없음.
- 실제 사용자: **Raspberry Pi 5의 RP1**(PCIe로 붙은 I/O 칩 — PCIe 디바이스가 발견되면 그 안의 GPIO/UART/I2C 블록을 DT로 기술하기 위해 overlay를 적용)과 **Microchip LAN966x PCIe 카드**. 둘 다 `base = dev_of_node(PCI 디바이스)`로 PCIe 장치 노드 밑에 overlay를 붙인다. 그리고 **FPGA region**은 비트스트림을 올린 뒤 새로 생긴 IP 블록을 overlay로 기술한다.
- RP1 Makefile의 `rp1-pci.dtbo.o` — `.dtso`를 컴파일해 오브젝트로 링크하는 Kbuild 규칙. 우리도 이걸 쓴다.

### 6.3 예제 10 — `d6_ovl.ko`: dtbo를 품은 모듈

무엇을 확인하나: Kbuild가 `.dtso` → `.dtbo` → `.dtbo.S`(`.incbin`) → `.dtbo.o`로 만들어 모듈에 링크해 주는지, 그리고 그 바이트로 `of_overlay_fdt_apply()`를 부르면 live tree에 노드가 붙는지.

```make
obj-m += d6_ovl.o d6_trace.o
# d6_ovl.ko = C 코드 + d6_hat.dtso를 컴파일한 dtbo를 .incbin 한 오브젝트
d6_ovl-objs := d6_ovl_main.o d6_hat.dtbo.o
DTC_FLAGS_d6_hat += -@
```

`d6_hat.dtso`는 §1.3 파일을 그대로 모듈 디렉터리에 복사했다. `DTC_FLAGS_d6_hat += -@`가 없으면 Kbuild는 overlay를 `-@` 없이 컴파일한다(처음 빌드에서 dtbo가 922 B였다가, 이 줄을 넣고 997 B가 됐다).

```c
// SPDX-License-Identifier: GPL-2.0
/* d6_ovl: 모듈 안에 박아 둔 d6_hat.dtbo를 런타임에 live tree에 적용/제거 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/ktime.h>

/* Kbuild의 cmd_wrap_S_dtb가 d6_hat.dtbo를 .incbin 하며 만들어 주는 심볼 */
extern char __dtbo_d6_hat_begin[];
extern char __dtbo_d6_hat_end[];

static int ovcs_id;
static bool twice;
module_param(twice, bool, 0444);
MODULE_PARM_DESC(twice, "apply the same overlay a second time (error demo)");

static int d6_apply(int *id)
{
	u32 size = __dtbo_d6_hat_end - __dtbo_d6_hat_begin;
	u64 t0 = ktime_get_ns();
	int ret = of_overlay_fdt_apply(__dtbo_d6_hat_begin, size, id, NULL);

	pr_info("d6_ovl: apply %u-byte dtbo -> ret=%d ovcs_id=%d (%llu us)\n",
		size, ret, *id, (ktime_get_ns() - t0) / 1000);
	return ret;
}

static int __init d6_ovl_init(void)
{
	int ret = d6_apply(&ovcs_id);

	if (ret) {
		if (ovcs_id)            /* 실패해도 부분 적용됐을 수 있다 */
			of_overlay_remove(&ovcs_id);
		return ret;
	}
	if (twice) {
		int id2 = 0;

		ret = d6_apply(&id2);
		if (id2)
			pr_info("d6_ovl: remove #%d -> ret=%d\n", id2,
				of_overlay_remove(&id2));
	}
	return 0;
}

static void __exit d6_ovl_exit(void)
{
	int id = ovcs_id;
	int ret = of_overlay_remove(&ovcs_id);

	pr_info("d6_ovl: remove #%d -> ret=%d\n", id, ret);
}

module_init(d6_ovl_init);
module_exit(d6_ovl_exit);
MODULE_DESCRIPTION("D6 runtime DT overlay demo");
MODULE_LICENSE("GPL");
```

- 실패 경로에서 `ovcs_id`가 0이 아니면 `of_overlay_remove()`를 부른다. 커널 주석이 "에러를 반환해도 changeset이 **부분 적용**됐을 수 있으니 호출자가 remove 하라"고 명시한다(drivers/of/overlay.c의 `of_overlay_fdt_apply()` 주석).
- `ovcs_id`는 "overlay changeset 번호"로, 제거할 때의 핸들이다. fd처럼 커널이 들고 있는 테이블(idr)의 인덱스다.

알림 체인을 엿보는 두 번째 모듈 — 그림 5의 각 단계를 dmesg로 보기 위해서다.

```c
// SPDX-License-Identifier: GPL-2.0
/* d6_trace: overlay 알림 체인과 OF reconfig 알림 체인을 엿보는 모듈 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/notifier.h>

static const char *const rc_name[] = {
	"?", "ATTACH_NODE", "DETACH_NODE", "ADD_PROPERTY",
	"REMOVE_PROPERTY", "UPDATE_PROPERTY",
};

static int d6_rc_cb(struct notifier_block *nb, unsigned long action, void *arg)
{
	struct of_reconfig_data *rd = arg;

	if (action < ARRAY_SIZE(rc_name))
		pr_info("d6_trace: reconfig %-15s %pOF%s%s\n", rc_name[action],
			rd->dn, rd->prop ? " : " : "",
			rd->prop ? rd->prop->name : "");
	return NOTIFY_OK;
}

static int d6_ov_cb(struct notifier_block *nb, unsigned long action, void *arg)
{
	struct of_overlay_notify_data *nd = arg;

	pr_info("d6_trace: overlay %-11s target=%pOF\n",
		of_overlay_action_name(action), nd->target);
	return NOTIFY_OK;
}

static struct notifier_block d6_rc_nb = { .notifier_call = d6_rc_cb };
static struct notifier_block d6_ov_nb = { .notifier_call = d6_ov_cb };

static int __init d6_trace_init(void)
{
	int ret = of_reconfig_notifier_register(&d6_rc_nb);

	if (ret)
		return ret;
	ret = of_overlay_notifier_register(&d6_ov_nb);
	if (ret)
		of_reconfig_notifier_unregister(&d6_rc_nb);
	return ret;
}

static void __exit d6_trace_exit(void)
{
	of_overlay_notifier_unregister(&d6_ov_nb);
	of_reconfig_notifier_unregister(&d6_rc_nb);
}

module_init(d6_trace_init);
module_exit(d6_trace_exit);
MODULE_DESCRIPTION("D6 OF notifier tracer");
MODULE_LICENSE("GPL");
```

```sh
cd /var/tmp/D6/ovl && make -C /var/lab/out/linux-build M=$PWD modules
nm d6_ovl.ko | grep dtbo
```

```text
  CC [M]  d6_ovl_main.o
  DTC     d6_hat.dtbo
  WRAP    d6_hat.dtbo.S
  AS [M]  d6_hat.dtbo.o
  LD [M]  d6_ovl.o
  CC [M]  d6_trace.o
  MODPOST Module.symvers
  LD [M]  d6_ovl.ko
  LD [M]  d6_trace.ko
0000000000000020 R __dtbo_d6_hat_begin
0000000000000405 R __dtbo_d6_hat_end
```

출력에서 볼 것: `DTC` → `WRAP`(`.incbin`으로 감싼 어셈블리 생성) → `AS` 3단계. `nm`의 `R`은 읽기 전용 데이터 심볼이고, `0x405 - 0x20 = 0x3e5 = 997` 바이트 = `-@`로 만든 dtbo 크기와 같다. 경고는 0개.

### 6.4 예제 11 — 런타임 적용 → probe → 제거, 전후의 `/proc/device-tree`

게스트 스크립트 `d6_rt.sh`:

```sh
#!/bin/sh
# 런타임 overlay: 적용 전 -> insmod d6_ovl -> 적용 후 -> rmmod -> 제거 후
show() {
	echo "== [$1] /proc/device-tree: $(ls /proc/device-tree | grep d6 | tr '\n' ' ')"
	echo "   rtc status=$(cat /proc/device-tree/pl031@9010000/status 2>/dev/null || echo '(none)')" \
	     " /sys/class/rtc=$(ls /sys/class/rtc | tr '\n' ' ')"
	echo "   platform devs: $(ls /sys/bus/platform/devices | grep d6 | tr '\n' ' ')"
	echo "   __symbols__/d6_vdd: $(cat /proc/device-tree/__symbols__/d6_vdd 2>/dev/null || echo '(none)')"
}
insmod /d6_sensor.ko
insmod /d6_trace.ko
ls -l /proc/device-tree | cat
show before
insmod /d6_ovl.ko $D6_ARGS
show applied
echo "   d6-vdd-3v3/phandle:    $(od -An -tx1 /proc/device-tree/d6-vdd-3v3/phandle)"
echo "   d6-sensor/vdd-supply:  $(od -An -tx1 /proc/device-tree/d6-sensor/vdd-supply)"
echo "   d6-sensor/reset-gpios: $(od -An -tx1 /proc/device-tree/d6-sensor/reset-gpios)"
rmmod d6_ovl
show removed
echo "== dmesg"
dmesg | grep -E "d6|OF:" | grep -v "Kernel command line\|LAB_CMD\|taints"
```

드라이버(`d6_sensor`)를 **먼저** 로드해 두고 overlay를 나중에 붙인다 — "보드는 부팅했고 드라이버도 있는데, 하드웨어가 나중에 나타났다" 시나리오. base는 `-@`로 만든 `d6_base.dtb`로 부팅한다.

```sh
cd /var/tmp/D6 && sh $S/mkinitrd.sh
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd d6_initrd.gz -dtb d6_base.dtb \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/d6_rt.sh" 2>&1 | grep -v "^\[    0.000\|LAB_CMD"
```

```text
lab initramfs: 6.18.55 up 3.20s
lrwxrwxrwx    1 0        0               29 Oct 10 17:39 /proc/device-tree -> /sys/firmware/devicetree/base
== [before] /proc/device-tree: 
   rtc status=(none)  /sys/class/rtc=rtc0 
   platform devs: 
   __symbols__/d6_vdd: (none)
[    5.672730] OF: overlay: WARNING: memory leak will occur if overlay removed, property: /pl031@9010000/status
[    5.673143] OF: overlay: WARNING: memory leak will occur if overlay removed, property: /__symbols__/d6_vdd
== [applied] /proc/device-tree: d6-sensor d6-vdd-3v3 
   rtc status=disabled  /sys/class/rtc=rtc0 
   platform devs: d6-sensor d6-vdd-3v3 
   __symbols__/d6_vdd: /d6-vdd-3v3
   d6-vdd-3v3/phandle:     00 00 80 06
   d6-sensor/vdd-supply:   00 00 80 06
   d6-sensor/reset-gpios:  00 00 80 04 00 00 00 05 00 00 00 00
== [removed] /proc/device-tree: 
   rtc status=(none)  /sys/class/rtc=rtc0 
   platform devs: 
   __symbols__/d6_vdd: (none)
== dmesg
[    5.671583] d6_trace: overlay pre-apply   target=
[    5.671757] d6_trace: overlay pre-apply   target=/pl031@9010000
[    5.671786] d6_trace: overlay pre-apply   target=/__symbols__
[    5.672730] OF: overlay: WARNING: memory leak will occur if overlay removed, property: /pl031@9010000/status
[    5.673143] OF: overlay: WARNING: memory leak will occur if overlay removed, property: /__symbols__/d6_vdd
[    5.684790] d6_trace: reconfig ATTACH_NODE     /d6-vdd-3v3
[    5.685284] d6_trace: reconfig ADD_PROPERTY    /d6-vdd-3v3 : compatible
[    5.685382] d6_trace: reconfig ADD_PROPERTY    /d6-vdd-3v3 : regulator-name
[    5.685417] d6_trace: reconfig ADD_PROPERTY    /d6-vdd-3v3 : regulator-min-microvolt
[    5.685442] d6_trace: reconfig ADD_PROPERTY    /d6-vdd-3v3 : regulator-max-microvolt
[    5.685466] d6_trace: reconfig ADD_PROPERTY    /d6-vdd-3v3 : phandle
[    5.685489] d6_trace: reconfig ADD_PROPERTY    /d6-vdd-3v3 : name
[    5.696307] d6_sensor d6-sensor: probe: label=imu-hat rate=200 Hz clk=24000000 Hz vdd=3300000 uV reset=gpio517 of_node=/d6-sensor
[    5.696942] d6_trace: reconfig ATTACH_NODE     /d6-sensor
[    5.696996] d6_trace: reconfig ADD_PROPERTY    /d6-sensor : compatible
[    5.697021] d6_trace: reconfig ADD_PROPERTY    /d6-sensor : label
[    5.697041] d6_trace: reconfig ADD_PROPERTY    /d6-sensor : d6,sample-rate-hz
[    5.697061] d6_trace: reconfig ADD_PROPERTY    /d6-sensor : vdd-supply
[    5.697079] d6_trace: reconfig ADD_PROPERTY    /d6-sensor : clocks
[    5.697098] d6_trace: reconfig ADD_PROPERTY    /d6-sensor : reset-gpios
[    5.697116] d6_trace: reconfig ADD_PROPERTY    /d6-sensor : name
[    5.697483] d6_trace: reconfig ADD_PROPERTY    /pl031@9010000 : status
[    5.697512] d6_trace: reconfig ADD_PROPERTY    /__symbols__ : d6_vdd
[    5.697912] d6_trace: overlay post-apply  target=
[    5.697932] d6_trace: overlay post-apply  target=/pl031@9010000
[    5.697950] d6_trace: overlay post-apply  target=/__symbols__
[    5.698062] d6_ovl: apply 997-byte dtbo -> ret=0 ovcs_id=1 (31201 us)
[    6.068972] d6_trace: overlay pre-remove  target=
[    6.069101] d6_trace: overlay pre-remove  target=/pl031@9010000
[    6.069146] d6_trace: overlay pre-remove  target=/__symbols__
[    6.070162] d6_trace: reconfig REMOVE_PROPERTY /__symbols__ : d6_vdd
[    6.070991] d6_trace: reconfig REMOVE_PROPERTY /pl031@9010000 : status
[    6.071597] d6_sensor d6-sensor: remove
[    6.076452] d6_trace: reconfig DETACH_NODE     /d6-sensor
[    6.077369] d6_trace: reconfig DETACH_NODE     /d6-vdd-3v3
[    6.077429] d6_trace: overlay post-remove target=
[    6.077444] d6_trace: overlay post-remove target=/pl031@9010000
[    6.077458] d6_trace: overlay post-remove target=/__symbols__
[    6.078014] d6_ovl: remove #1 -> ret=0
[    6.489537] reboot: Power down
```

출력에서 볼 것 (천천히 읽을 가치가 있다):

1. **`/proc/device-tree`는 `/sys/firmware/devicetree/base`의 심볼릭 링크**이고, live tree를 그대로 보여 준다. before에는 d6 노드가 없고, applied에는 `d6-sensor d6-vdd-3v3`가 있고, removed에는 다시 없다.
2. **phandle 실측**: 커널 resolver는 overlay 내부 phandle을 `0x8006`으로 밀었다(libfdt의 `0x8005`와 다름 — 그림 3의 ④). `reset-gpios`는 `0x8004`(pl061) + `5` + `0`으로 fixup 됐다.
3. **probe가 언제 일어났나**: `d6_sensor ... probe` 줄이 `d6_trace: reconfig ATTACH_NODE /d6-sensor` **보다 먼저** 찍혔다. reconfig 알림 체인에서 커널의 `of_platform_notify()`가 부팅 때 먼저 등록됐으므로 우리 tracer보다 먼저 호출되어 플랫폼 디바이스를 만들고, 그 자리에서 드라이버 매칭·probe까지 **동기적으로** 끝났기 때문이다(그림 5).
4. **overlay 알림의 `target=`(빈칸)**: 루트 노드는 `%pOF`가 빈 문자열로 찍힌다. 즉 fragment 3개 = 루트 / pl031 / `__symbols__`(overlay의 `__symbols__`도 하나의 fragment처럼 live `__symbols__`에 합쳐진다).
5. **`WARNING: memory leak will occur if overlay removed`**: base에 원래 있던 노드(pl031, `__symbols__`)의 속성을 overlay가 추가·변경하면 커널이 경고한다. 제거할 때 그 속성 메모리의 수명을 안전하게 관리할 수 없기 때문이다. overlay는 "새 노드 추가"에 가장 깔끔하고, base 노드 수정은 비용이 있다는 신호다.
6. **RTC는 꺼지지 않았다**: `status=disabled`가 live tree에 들어갔는데도 `/sys/class/rtc=rtc0`이 그대로다. `of_platform_notify()`는 status가 okay → disabled로 바뀌면 `of_find_device_by_node()`로 **플랫폼 버스에서** 디바이스를 찾아 지우는데, pl031은 플랫폼이 아니라 **AMBA 버스** 디바이스라 못 찾고 그냥 넘어간다. 오프라인/부팅 경로(§4, §5)에서는 RTC가 아예 안 만들어졌던 것과 대조적이다. **같은 overlay라도 런타임 적용은 결과가 다를 수 있다.**
7. 적용 시간 31 ms — TCG 에뮬레이션 기준이라 실제 하드웨어보다 훨씬 느리다. 이 세션에서 런타임 적용을 네 번 재 보니 69462, 39597, 35316, 31201 us로 첫 회가 가장 느렸다.
8. 제거는 **역순**: symbols·status 속성 → `d6_sensor` remove → 노드 detach. 디바이스가 먼저 사라지고 노드가 나중에 사라진다.

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="300" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="160" y="33" text-anchor="middle" font-size="13">insmod d6_ovl.ko → d6_ovl_init()</text>
  <rect x="10" y="62" width="300" height="36" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="160" y="85" text-anchor="middle" font-size="13">of_overlay_fdt_apply(): unflatten</text>
  <rect x="10" y="114" width="300" height="36" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="160" y="137" text-anchor="middle" font-size="13">of_resolve_phandles(): fixup · delta</text>
  <rect x="10" y="166" width="300" height="36" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="160" y="189" text-anchor="middle" font-size="13">changeset 만들기: fragment → target</text>
  <rect x="10" y="218" width="300" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="241" text-anchor="middle" font-size="13">overlay 알림: PRE_APPLY</text>
  <rect x="10" y="270" width="300" height="36" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="160" y="293" text-anchor="middle" font-size="13">changeset 적용: 노드 · 속성 붙이기</text>
  <rect x="10" y="322" width="300" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="345" text-anchor="middle" font-size="13">reconfig 알림 (노드 · 속성마다)</text>
  <rect x="10" y="374" width="300" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="397" text-anchor="middle" font-size="13">overlay 알림: POST_APPLY → ovcs_id</text>
  <line x1="160" y1="46" x2="160" y2="60" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="98" x2="160" y2="112" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="150" x2="160" y2="164" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="202" x2="160" y2="216" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="254" x2="160" y2="268" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="306" x2="160" y2="320" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="358" x2="160" y2="372" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="350" y="114" width="320" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="137" text-anchor="middle" font-size="13">① of_platform_notify (부팅 때 등록)</text>
  <rect x="350" y="166" width="320" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="189" text-anchor="middle" font-size="13">of_platform_device_create(/d6-sensor)</text>
  <rect x="350" y="218" width="320" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="241" text-anchor="middle" font-size="13">platform bus match: compatible</text>
  <rect x="350" y="270" width="320" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="293" text-anchor="middle" font-size="13">d6_sensor_probe() — 같은 콜스택에서</text>
  <rect x="350" y="322" width="320" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="345" text-anchor="middle" font-size="13">② i2c · spi notifier: 부모가 그 버스면</text>
  <rect x="350" y="374" width="320" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="510" y="397" text-anchor="middle" font-size="13">③ d6_trace (나중에 등록 → 나중에 호출)</text>
  <line x1="310" y1="340" x2="348" y2="140" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="150" x2="510" y2="164" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="202" x2="510" y2="216" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="254" x2="510" y2="268" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="310" y1="340" x2="348" y2="340" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="310" y1="346" x2="348" y2="390" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="350" y="40" font-size="12">reconfig 체인은 등록 순서대로 호출된다:</text>
  <text x="350" y="60" font-size="12">플랫폼 → (i2c, spi) → 나중에 등록한 모듈</text>
  <text x="350" y="80" font-size="12">그래서 probe 로그가 tracer 로그보다 먼저 찍힌다</text>
  <text x="10" y="432" font-size="12">빨강 = overlay.c · resolver.c, 주황 = 알림 체인, 초록 = platform.c → driver core</text>
</svg>
```
그림 5 — 런타임 overlay가 디바이스 생성과 probe로 이어지는 경로. 노드가 붙을 때마다 OF reconfig 알림이 돌고, 부모가 루트(또는 populate된 버스)이면 `of_platform_notify()`가 플랫폼 디바이스를 만들어 그 자리에서 probe까지 간다. I2C/SPI 컨트롤러 밑에 붙는 노드는 각 서브시스템의 notifier가 `i2c_client`/`spi_device`를 만든다.

### 6.5 예제 12 — 런타임 에러: 같은 overlay 두 번, `-@` 없는 base

무엇을 확인하나: (a) `twice=1`로 같은 overlay를 두 번 적용, (b) `-@` 없이 컴파일한 base로 부팅한 커널에 overlay 적용. (a)는 원인을 보려고 `dyndbg`로 `drivers/of/overlay.c`의 `pr_debug`를 켰다.

```sh
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd d6_initrd.gz -dtb d6_base.dtb \
  -append "console=ttyAMA0 quiet dyndbg=\"file drivers/of/overlay.c +p; file drivers/of/resolver.c +p\" LAB_CMD=D6_ARGS=twice=1~sh~/d6_rt.sh" 2>&1 | grep -E "OF:|d6_ovl"
```

```text
[    6.208027] OF: overlay: WARNING: memory leak will occur if overlay removed, property: /pl031@9010000/status
[    6.208312] OF: overlay: WARNING: memory leak will occur if overlay removed, property: /__symbols__/d6_vdd
[    0.000000] OF: reserved mem: Reserved memory: No reserved-memory node in the DT
[    6.205022] OF: overlay: init_overlay_changeset() ovcs->overlay_root is not dynamic
[    6.208027] OF: overlay: WARNING: memory leak will occur if overlay removed, property: /pl031@9010000/status
[    6.208312] OF: overlay: WARNING: memory leak will occur if overlay removed, property: /__symbols__/d6_vdd
[    6.236221] OF: overlay: of_overlay_apply() err=0
[    6.236293] d6_ovl: apply 997-byte dtbo -> ret=0 ovcs_id=1 (35316 us)
[    6.236688] OF: overlay: init_overlay_changeset() ovcs->overlay_root is not dynamic
[    6.236828] OF: overlay: Failed to apply node @/d6-vdd-3v3, err=-22
[    6.236872] OF: overlay: fragment apply failed ''
[    6.236888] OF: overlay: of_overlay_apply() err=-22
[    6.236897] d6_ovl: apply 997-byte dtbo -> ret=-22 ovcs_id=2 (567 us)
[    6.237324] OF: overlay: of_overlay_remove() err=0
[    6.237363] d6_ovl: remove #2 -> ret=0
[    6.879877] OF: overlay: of_overlay_remove() err=0
[    6.879910] d6_ovl: remove #1 -> ret=0
```

(첫 두 줄은 콘솔에 바로 찍힌 것, 나머지는 스크립트의 `dmesg` 출력이라 일부가 두 번 보인다.) 출력에서 볼 것:

- 두 번째 적용은 `-22`(`-EINVAL`). 원인은 `pr_debug`에만 나온다: `Failed to apply node @/d6-vdd-3v3`. live tree에 이미 phandle을 가진 `/d6-vdd-3v3`이 있는데 overlay 노드도 phandle을 가지면 거부한다(소스: `if (node->phandle && tchild->phandle) ret = -EINVAL;`). **오프라인 fdtoverlay는 조용히 성공했던 바로 그 상황**이다(§4.3).
- 기본 로그 레벨에서는 이유가 안 보인다 — 실무에서 overlay가 `-EINVAL`이면 **`dyndbg`로 `drivers/of/overlay.c`를 켜는 것**이 첫 수다.
- 실패한 #2는 `ovcs_id=2`를 돌려줬고, 우리가 remove 해서 정리했다(부분 적용 정리 패턴).

```sh
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd d6_initrd.gz -dtb d6_base_nosym.dtb \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/d6_rt.sh" 2>&1 | grep -v "^\[    0.000\|LAB_CMD"
```

```text
lab initramfs: 6.18.55 up 1.41s
== [before] /proc/device-tree: 
   rtc status=(none)  /sys/class/rtc=rtc0 
   platform devs: 
   __symbols__/d6_vdd: (none)
[    1.761300] OF: resolver: no symbols in root of device tree.
insmod: can't insert '/d6_ovl.ko': invalid parameter
== [applied] /proc/device-tree: 
   rtc status=(none)  /sys/class/rtc=rtc0 
   platform devs: 
   __symbols__/d6_vdd: (none)
rmmod: remove 'd6_ovl': No such file or directory
== [removed] /proc/device-tree: 
   rtc status=(none)  /sys/class/rtc=rtc0 
   platform devs: 
   __symbols__/d6_vdd: (none)
== dmesg
[    1.761300] OF: resolver: no symbols in root of device tree.
[    1.761426] d6_ovl: apply 997-byte dtbo -> ret=-22 ovcs_id=1 (2566 us)
```

(이 실행은 `d6_rt.sh`에 `ls -l /proc/device-tree`와 phandle `od` 줄을 넣기 전 버전으로 돌렸다.) 출력에서 볼 것: `OF: resolver: no symbols in root of device tree.` — 세 경로 모두 같은 원인, 다른 메시지다. `insmod`가 "invalid parameter"라고 하는 건 init이 `-EINVAL`을 반환해서다(모듈 파라미터 문제가 아니다). QEMU가 자체 생성하는 DT(`-dtb` 없이 부팅)도 `__symbols__`가 없으므로 같은 결과가 된다.

---

## 7. 제거(`of_overlay_remove`)와 그 한계

런타임 overlay의 진짜 어려움은 **떼어낼 때**다. 커널 문서(`Documentation/devicetree/overlay-notes.rst`)와 소스가 말하는 규칙:

| 한계 | 내용 | 실측·근거 |
|---|---|---|
| 스택 순서 | 겹치는 노드를 건드린 overlay가 위에 있으면 아래 것을 못 뗀다 | `overlay #%d is not topmost` → `-EBUSY` (`overlay_removal_is_ok()`) |
| base 노드 수정 | base 노드에 추가·변경한 속성은 제거 후 메모리 수명 보장이 안 됨 | 예제 11의 `WARNING: memory leak will occur if overlay removed` |
| 드라이버가 잡은 참조 | 노드에 `of_node_get()`을 잡고 놓지 않는 드라이버가 있으면 revert 때 refcount 경고 | `of_node_put()` 짝이 맞아야 함 (D8) |
| 속성 변경 ≠ 재probe | 이미 probe된 디바이스의 속성을 바꿔도 드라이버는 다시 읽지 않는다 | 예제 11: pl031 `status` 변경에도 rtc0 유지 |
| 버스 종류 | 런타임 디바이스 생성은 notifier가 있는 버스만(platform, i2c, spi 등) | pl031(AMBA)은 생성·제거 알림을 처리하는 notifier가 없음 |
| 부분 적용 | 적용 실패해도 `ovcs_id`가 나올 수 있다 → 호출자가 remove | `of_overlay_fdt_apply()` 주석 |
| 망가진 상태 | revert 실패 시 `devicetree_state_flags`가 세팅되어 이후 모든 apply/remove 거부 | `devicetree state suspect, refuse to apply overlay` |

그래서 실무 권장:

- overlay는 **새 노드 추가 위주로** 설계하고, base 노드는 `status` 정도만 건드린다.
- 런타임 overlay는 "꽂혔다는 걸 런타임에야 알고, **그 장치를 소유하는 드라이버가** 수명을 관리하는" 경우(RP1, LAN966x, FPGA region)에만 쓴다. 유저 공간에서 아무 overlay나 넣고 빼는 configfs 방식이 upstream에 안 들어간 이유가 바로 이 수명·안전성 문제다.
- 제거 후 재적용이 필요하면 `of_overlay_remove_all()`보다 **자기 ovcs_id만** 관리한다.

---

## 8. 벤더별 방식 — Raspberry Pi, BeagleBone/TI, Jetson

> 이 절의 벤더 세부 사항(파일 경로, 키 이름, 도구 동작)은 **기억 기반**이다. 랩에서 직접 확인하지 못했으니 실제 보드·현재 문서로 확인하고 쓰자. 커널·U-Boot 쪽 근거(Makefile, `pxe_utils.c`)는 랩 소스에서 확인했다.

| 플랫폼 | 어디서 적용 | 설정 방법 | 메모 |
|---|---|---|---|
| Raspberry Pi | GPU 펌웨어(부트로더)가 커널 시작 전에 | `config.txt`에 `dtoverlay=<이름>[,파라미터=값]`, `dtparam=i2c_arm=on` | overlay는 `overlays/` 디렉터리의 `.dtbo`. Bookworm부터 부트 파티션이 `/boot/firmware`. 런타임 `sudo dtoverlay <이름>`은 RPi 커널의 configfs 사용. HAT EEPROM에 overlay 이름을 넣어 자동 적용 가능 |
| BeagleBone (AM335x, Debian 이미지) | U-Boot | `/boot/uEnv.txt`의 `enable_uboot_overlays=1`, `uboot_overlay_addr0=/lib/firmware/<cape>.dtbo` | cape EEPROM을 U-Boot가 읽어 자동 선택하는 방식도 있음 |
| TI AM62x / BeaglePlay 등 최신 | U-Boot | extlinux.conf의 `fdtoverlays`, 또는 TI SDK `uEnv.txt`의 `name_overlays=` | 커널 트리에 `-dtbs` 합성 규칙과 `DTC_FLAGS := -@`(랩 소스 확인) |
| NVIDIA Jetson (Orin, JetPack 5/6) | UEFI 부트 단계(L4TLauncher) | `/boot/extlinux/extlinux.conf`의 `FDT`, `OVERLAYS` 줄 | `sudo /opt/nvidia/jetson-io/jetson-io.py`가 40핀 헤더·카메라 구성을 메뉴로 고르면 overlay를 적용한 설정을 써 준다. 카메라 모듈용 `.dtbo`가 `/boot`에 제공됨 |
| Yocto / Buildroot | 빌드 시간 | 머신 설정에 base + overlay 목록 → 이미지에 `.dtbo` 또는 합성 dtb | Yocto의 `KERNEL_DEVICETREE`에 `.dtbo`도 나열 가능 |

공통 패턴: **base dtb는 `-@`로 빌드되어 있고, 보드 EEPROM이나 사용자 설정이 "어떤 overlay를 올릴지"를 결정하며, 적용은 대부분 부트 단계**다. 런타임 configfs는 RPi·BeagleBoard 다운스트림 커널의 편의 기능이다.

---

## 9. 측정 — `-@`는 얼마나 비싼가

```sh
stat -c "%n %s" d6_base_nosym.dtb d6_base.dtb d6_hat_nosym.dtbo d6_hat.dtbo d6_merged.dtb d6_hat2.dtbo
```

```text
d6_base_nosym.dtb 7566
d6_base.dtb 7784
d6_hat_nosym.dtbo 922
d6_hat.dtbo 997
d6_merged.dtb 8206
d6_hat2.dtbo 241
```

```svg
<svg viewBox="0 0 640 262" xmlns="http://www.w3.org/2000/svg">
  <text x="320" y="18" text-anchor="middle" font-size="13" font-weight="bold">dtb · dtbo 크기 (바이트, 실측)</text>
  <line x1="210" y1="30" x2="210" y2="222" stroke="currentColor"/>
  <text x="200" y="56" text-anchor="end" font-size="12">base (-@ 없음)</text>
  <rect x="210" y="40" width="350" height="24" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <text x="568" y="57" font-size="12">7566</text>
  <text x="200" y="94" text-anchor="end" font-size="12">base (-@)</text>
  <rect x="210" y="78" width="360" height="24" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
  <text x="578" y="95" font-size="12">7784</text>
  <text x="200" y="132" text-anchor="end" font-size="12">merged (base+hat)</text>
  <rect x="210" y="116" width="380" height="24" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
  <text x="598" y="133" font-size="12">8206</text>
  <text x="200" y="170" text-anchor="end" font-size="12">hat.dtbo (-@ 없음)</text>
  <rect x="210" y="154" width="43" height="24" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <text x="261" y="171" font-size="12">922</text>
  <text x="200" y="208" text-anchor="end" font-size="12">hat.dtbo (-@)</text>
  <rect x="210" y="192" width="46" height="24" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
  <text x="264" y="209" font-size="12">997</text>
  <text x="320" y="248" text-anchor="middle" font-size="12">-@ 비용: base +218 B (+2.9%), overlay +75 B (+8.1%) — label 개수에 비례</text>
</svg>
```
그림 6 — `-@`의 크기 비용은 label 수에 비례하는 문자열 표 하나라 작다. label 5개짜리 base에서 218 B. 수백 개 label이 있는 실제 SoC dtb에서는 수 KB가 되지만, 부트 파티션 기준으로는 무시할 만하다. "`-@`를 켤까 말까"의 진짜 비용은 크기가 아니라 **label 이름이 overlay에 대한 ABI가 된다**는 점이다.

적용 시간은 이 랩에서 신뢰할 만한 비교가 어렵다(TCG 에뮬레이션, 측정 4회: 31~69 ms, 첫 회가 가장 느림). 실제 하드웨어에서도 overlay 적용 자체보다 그 뒤 **probe(레귤레이터 켜기, 센서 리셋 대기)**가 훨씬 오래 걸린다.

---

## 10. 커널 안에서는 무슨 일이 일어나나

### 10.1 부팅 시: FDT → live tree

부트로더가 넘긴 FDT(평평한 바이너리)를 커널이 `unflatten_device_tree()`로 `struct device_node`/`struct property` 트리로 펼친다. 이게 **live tree**다. `__symbols__`도 이때 평범한 노드로 들어오므로, base dtb가 `-@`로 빌드돼 있어야 런타임 resolver가 label을 찾는다. 그 다음 `of_platform_default_populate_init()`(§5.2의 콜 트레이스에 나온 그 함수)이 루트 밑 노드들로 플랫폼/AMBA 디바이스를 만든다. 오프라인·부팅 시 overlay는 여기까지 오기 전에 끝났으므로 커널 입장에서는 그냥 처음부터 있던 노드다.

### 10.2 런타임: `drivers/of/overlay.c` + `resolver.c` + `dynamic.c`

1. `of_overlay_fdt_apply()` — `fdt_check_header()`로 검사, FDT를 **복사**(호출자 버퍼를 계속 가리키지 않도록), `of_fdt_unflatten_tree()`로 overlay 자체를 작은 detached 트리로 펼친다.
2. `of_resolve_phandles()` (resolver.c) — `live_tree_max_phandle() + 1`을 delta로 overlay 내부 phandle과 `__local_fixups__` 위치를 밀고, `__fixups__`의 label을 live `/__symbols__`에서 찾아 경로 → 노드 → phandle을 써 넣는다. `/__symbols__`가 없으면 `no symbols in root of device tree.`
3. `init_overlay_changeset()` / `build_changeset()` — fragment마다 `target`(phandle) 또는 `target-path`로 live 노드를 찾고, `__overlay__` 내용을 `struct of_changeset`의 엔트리(ATTACH_NODE, ADD_PROPERTY, UPDATE_PROPERTY ...)로 바꾼다. 같은 노드·속성을 두 fragment가 건드리면 `ERROR: multiple fragments add ...`.
4. `overlay_notify(OF_OVERLAY_PRE_APPLY)` → `__of_changeset_apply_entries()` → `__of_changeset_apply_notify()`가 엔트리마다 `of_reconfig_notify()` → `overlay_notify(OF_OVERLAY_POST_APPLY)`.
5. reconfig 체인의 구독자: `of_platform_notify()`(drivers/of/platform.c), `of_i2c_notify()`(i2c-core), `of_spi_notify()`(spi.c). 각자 "부모가 내 버스면" 디바이스를 만든다(`of_platform_device_create()`, `of_i2c_register_device()`, `of_register_spi_device()`).

```c
/* drivers/of/overlay.c (6.18) — 필드는 랩 소스 그대로, 주석은 설명용 */
struct overlay_changeset {
	int id;                                   /* ovcs_id (idr에서 할당) */
	struct list_head ovcs_list;               /* 적용 순서 = 제거 스택 */
	const void *new_fdt;                      /* 복사해 둔 overlay FDT */
	const void *overlay_mem;                  /* unflatten 메모리 */
	struct device_node *overlay_root;
	enum of_overlay_notify_action notify_state;
	int count;
	struct fragment *fragments;
	bool symbols_fragment;                    /* __symbols__ 합치기용 fragment */
	struct of_changeset cset;                 /* 되돌리기(revert)용 기록 */
};
```

`of_changeset`은 **트랜잭션 로그**다. 적용할 때 엔트리를 기록해 두고, 제거할 때 역순으로 되돌린다(Don이 펌웨어에서 쓰던 "변경 저널 → 롤백"과 같다).

### 10.3 드라이버를 쓸 때 다시 만나는 지점

- **D3·G2**: overlay로 붙은 노드도 결국 `compatible` → `of_match_table` → `probe()`. 드라이버 입장에서 차이 없음.
- **D8**: probe에서 쓴 `device_property_read_*()`, `devm_clk_get_enabled()`, `devm_regulator_get()`, `devm_gpiod_get()`가 phandle을 따라가는 API다. overlay fixup이 틀리면 여기서 `-ENOENT`/`-EPROBE_DEFER`.
- **G 모듈(드라이버 모델)**: notifier가 만든 디바이스가 바로 매칭되는 경로, `devm_` 자원이 remove 때 자동 해제되는 순서(예제 11의 remove).
- **I2C/SPI(I 모듈)**: 실제 HAT의 센서는 대부분 I2C/SPI 컨트롤러 노드 밑에 `&i2c1 { imu@68 {...}; };` 형태로 붙는다 → `of_i2c_notify()`가 `i2c_client`를 만든다.

---

## 11. Embedded 실무에서 다시 보기

### 11.1 장면 1 — 센서 키트의 IMU 보드 rev-A / rev-B

rev-B에서 IMU가 I2C 주소 `0x68` → `0x69`, 인터럽트 핀이 GPIO 5 → 7로 바뀌었다. base는 그대로 두고 overlay 두 개를 둔다.

```dts
/dts-v1/;
/plugin/;

&i2c1 {
	status = "okay";
	#address-cells = <1>;
	#size-cells = <0>;
	imu@69 {
		compatible = "invensense,icm42688";
		reg = <0x69>;
		interrupt-parent = <&gpio0>;
		interrupts = <7 2>;            /* IRQ_TYPE_EDGE_FALLING */
		vdd-supply = <&reg_3v3>;
	};
};
```

(이 조각은 개념 예시로, 랩에서 실행하지 않았다. `#address-cells`를 **base와 같은 값으로** 다시 쓰는 것은 허용되지만 다른 값이면 커널이 거부한다.) 보드 EEPROM의 rev 필드를 U-Boot 스크립트가 읽어 `fdt apply`할 파일을 고르면 "한 이미지로 모든 rev 지원"이 된다.

### 11.2 장면 2 — 카메라 모듈 선택 (Jetson, RPi)

같은 CSI 포트에 IMX219 또는 IMX477. 센서 드라이버·레인 수·클록이 다르다. Jetson은 jetson-io로 overlay를 고르고 재부팅, RPi는 `config.txt`에 `dtoverlay=imx477`(기억 기반). 둘 다 **부팅 시 적용 + 재부팅**이다 — 카메라 파이프라인(V4L2 media graph)은 런타임에 노드가 바뀌는 걸 감당하도록 설계되지 않았기 때문.

### 11.3 장면 3 — "overlay가 안 먹는다" 5분 진단

```sh
# 보드 위에서 (예시 순서)
ls /proc/device-tree/__symbols__ | head        # 1. base가 -@ 로 빌드됐나
ls /proc/device-tree/ | grep imu               # 2. overlay 노드가 live tree에 있나
cat /proc/device-tree/imu@69/status            # 3. status 가 okay 인가
ls /sys/bus/i2c/devices/                       # 4. 디바이스가 만들어졌나
cat /sys/kernel/debug/devices_deferred         # 5. probe 가 defer 됐나
dmesg | grep -iE "OF:|overlay|imu"             # 6. 적용 에러
```

1에서 비면 base 문제, 2에서 비면 적용 단계(부트로더 로그·`fdt print`) 문제, 3~5는 D3·D4·G 모듈 문제다. 이 순서는 §5.4, §6.5에서 본 에러들을 그대로 따른다.

### 11.4 장면 4 — PCIe로 붙는 복합 장치

FPGA 카드나 RP1처럼 PCIe 하나 뒤에 GPIO/UART/I2C 블록이 숨어 있는 장치. PCI는 discoverable(D1)이지만 그 안의 블록은 아니다. 그래서 PCI 드라이버가 probe에서 **자기 노드 밑에** `of_overlay_fdt_apply(..., dev_of_node(&pdev->dev))`로 내부 블록을 기술하고, 하위 블록 드라이버들이 평범한 플랫폼 드라이버로 붙는다. 우리 `d6_ovl`의 `base` 인자를 `NULL` 대신 자기 노드로 주면 같은 구조다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| base를 `-@` 없이 빌드 | fdtoverlay `FDT_ERR_BADOFFSET`, U-Boot `base fdt does not have a /__symbols__ node`, 커널 `no symbols in root of device tree.` | label을 풀 `__symbols__` 없음 | base 빌드에 `-@` (`DTC_FLAGS := -@`) |
| `/plugin;` 오타 | `syntax error` (2행 2-8열) | 지시어는 `/plugin/;` | 슬래시로 감싸기 |
| U-Boot `fdt resize` 생략 | `FDT_ERR_NOSPACE` 후 `FDT_ERR_BADMAGIC` | 제자리 확장할 공간 없음 + 실패 시 libfdt가 magic을 지움 | `fdt resize 8192` 등, 실패 시 base 재로드 |
| 앞 overlay를 `-@` 없이 빌드하고 쌓기 | `Failed to apply 'd6_hat2.dtbo': FDT_ERR_NOTFOUND` | 앞 overlay label이 `__symbols__`에 없음 | 모든 overlay도 `-@` (Kbuild는 `DTC_FLAGS_<이름> += -@`) |
| 런타임에 같은 overlay 두 번 | `ret=-22`, 기본 로그엔 이유 없음 | 이미 phandle 가진 노드에 phandle 노드 합치기 거부 | ovcs_id로 적용 상태 관리, `dyndbg`로 `overlay.c` 켜서 원인 확인 |
| 하드웨어에 없는 노드를 overlay로 켬 | 부팅 중 `synchronous external abort`, panic | 없는 MMIO를 읽음 | 보드 감지(EEPROM, ID 핀) 후에만 적용 |
| 기존 노드 속성 변경을 런타임에 기대 | 값은 바뀌었는데 동작은 그대로 (예: rtc 유지) | 이미 probe된 드라이버는 다시 안 읽음, AMBA는 notifier 없음 | 부팅 시 적용, 또는 드라이버 unbind/bind |
| base의 label 이름 변경 | 기존 `.dtbo`가 갑자기 `FDT_ERR_NOTFOUND` | label = overlay ABI | label은 API처럼 유지, 바꾸면 overlay 동시 갱신 |
| `fdt apply` 후 확인 없이 부팅 | 조용히 overlay 없이 부팅 | 성공해도 실패해도 메시지가 적다 | `fdt print <노드>`로 확인, 스크립트에서 반환값 검사 |

---

## 13. 면접에서 이렇게 말한다

**Q.** What is a device tree overlay, and why would you use one instead of just editing the board's dts?

**A.** base dtb는 SoC와 캐리어 보드처럼 안 바뀌는 부분이고, overlay는 그 위에 꽂히는 확장 보드·카메라·공장 변형을 기술하는 조각이다. 조합마다 dtb를 만들면 경우의 수가 곱으로 늘지만 overlay는 합으로 는다. base는 한 번 검증해 고정하고 overlay만 교체한다.

> A device tree overlay is a fragment that patches a base device tree — it adds nodes and overrides properties. I'd use it for anything that varies on top of a fixed base: add-on boards like HATs or capes, camera module options, or factory variants such as "no RTC populated". It keeps the base DTB validated and unchanged, and the number of DT files grows additively instead of multiplicatively with each option.

**꼬리 질문**: "overlay로 노드를 지울 수 있나?" → 포맷에 delete가 없어서 `status = "disabled"`로 끈다.

**Q.** How are phandle references in an overlay resolved when it is applied?

**A.** 링커와 같다. overlay는 base를 모르는 채 컴파일되므로 base label 참조는 `0xffffffff` 자리표시자와 `__fixups__`(label → 경로:속성:오프셋)로 남는다. base는 `-@`로 컴파일해 `__symbols__`(label → 경로)를 내보낸다. 적용 시 label로 경로를 찾아 그 노드의 phandle을 써 넣고, overlay 내부 phandle은 base 최댓값 위로 밀어 `__local_fixups__` 위치도 고친다.

> It works like a linker. The overlay is compiled without knowing the base, so references to base labels become placeholders plus `__fixups__` entries — label, path, property, byte offset. The base must be compiled with `-@` so it exports a `__symbols__` table mapping labels to paths. At apply time each fixup looks up the label, gets that node's phandle and patches it in. The overlay's own phandles are renumbered above the base's maximum, and `__local_fixups__` tells the applier which cells to adjust. In my lab, libfdt shifted by max and the kernel by max plus one, so the same overlay got 0x8005 offline and 0x8006 at runtime.

**꼬리 질문**: "`-@` 없이 빌드된 base에 overlay를 붙이면?" → 세 도구 모두 실패: fdtoverlay `FDT_ERR_BADOFFSET`, U-Boot는 블롭까지 망가짐, 커널은 `no symbols in root`.

**Q.** Where can overlays be applied, and which would you pick for a product?

**A.** 세 곳 — 빌드 시간(fdtoverlay, Kbuild `-dtbs`), 부팅 시간(U-Boot `fdt apply`, extlinux `fdtoverlays`, RPi 펌웨어, Jetson UEFI), 런타임(커널 `of_overlay_fdt_apply()`). 제품이면 고정 변형은 빌드 시간, 현장 교체형 보드는 EEPROM으로 감지해 부팅 시간에. 런타임은 FPGA나 PCIe 복합 장치처럼 소유 드라이버가 수명을 관리할 때만.

> Build time with fdtoverlay or the kernel's composite `-dtbs` rule, boot time in the bootloader — U-Boot `fdt apply`, extlinux `fdtoverlays`, the Raspberry Pi firmware or Jetson's UEFI — and runtime with `of_overlay_fdt_apply` in the kernel. For a product I prefer build or boot time: the kernel just sees a normal DTB, so it's deterministic and testable. I'd detect the add-on board from an ID EEPROM in U-Boot and apply the matching overlay. Runtime overlays I'd reserve for cases where a driver owns the hardware lifetime, like an FPGA region or a PCIe device with internal blocks.

**Q.** Can you load an overlay at runtime from userspace on a mainline kernel?

**A.** mainline에는 유저 공간 인터페이스가 없다. 흔히 쓰는 configfs `/sys/kernel/config/device-tree/overlays`는 upstream에 머지되지 않은 패치로, RPi·BeagleBoard 다운스트림 커널에만 있다. mainline에서는 드라이버가 `.dtbo`를 오브젝트로 링크해 `of_overlay_fdt_apply()`를 부른다. RP1, LAN966x, FPGA region이 그렇게 쓴다.

> Not on mainline. The configfs overlay interface people use on Raspberry Pi or BeagleBone is an out-of-tree patch carried in those vendor kernels; it was never merged, largely because removal and lifetime handling aren't safe in general. Mainline exposes `of_overlay_fdt_apply` and `of_overlay_remove` to kernel code only. The upstream pattern is a driver that links the compiled `.dtbo` into its module — Kbuild has a `.dtbo.o` rule for this — and applies it under its own node, which is what the RP1 and LAN966x PCIe drivers do.

**Q.** When an overlay adds a node at runtime, how does a driver end up probing it?

**A.** changeset을 적용하면 노드·속성마다 OF reconfig 알림이 돈다. 플랫폼 버스의 `of_platform_notify()`가 부모가 루트나 populate된 버스면 플랫폼 디바이스를 만들고, 드라이버 코어가 바로 매칭·probe 한다. I2C·SPI 컨트롤러 밑이면 각 서브시스템 notifier가 클라이언트 디바이스를 만든다. 내 실습에서는 probe 로그가 내 tracer의 ATTACH_NODE 로그보다 먼저 찍혔다 — 플랫폼 notifier가 먼저 등록돼 있어서.

> The overlay is applied as an `of_changeset`, and every attached node or property fires an OF reconfig notification. The platform bus listens with `of_platform_notify`: if the parent is the root or an already-populated bus, it creates a platform device, and driver core matches `compatible` and calls probe synchronously. Nodes under an I2C or SPI controller are handled by the I2C and SPI notifiers, which register clients. I traced it with my own notifier: the probe message appeared before my ATTACH_NODE message, because the platform notifier was registered earlier in the chain.

**Q.** What are the limitations of removing an overlay?

**A.** 스택 순서(위에 겹친 overlay가 있으면 `-EBUSY`), base 노드에 추가한 속성은 메모리 누수 경고, 드라이버가 노드 참조를 놓지 않으면 문제, 그리고 이미 probe된 디바이스의 속성 변경은 반영되지 않는다. 실패하면 커널이 devicetree를 suspect로 표시하고 이후 overlay 조작을 거부한다.

> Removal has to be in stack order — the kernel refuses with "not topmost" if a later overlay touches the same nodes. Properties an overlay added to base nodes trigger a "memory leak will occur if overlay removed" warning, because their lifetime can't be guaranteed. Drivers holding node references can break revert. And property changes don't re-probe existing devices — in my test, setting the AMBA RTC's status to disabled at runtime left the RTC device alive. If a revert fails, the kernel marks the tree as suspect and refuses further apply or remove.

**Q.** U-Boot's `fdt apply` failed and the board now won't boot even without the overlay. What happened?

**A.** libfdt의 overlay 적용은 실패해도 base가 바뀌었다고 가정해야 하고, 실제로 에러 경로에서 base의 magic을 지운다. 실습에서 `-@` 없음, `fdt resize` 생략 두 경우 모두 직후 `FDT_ERR_BADMAGIC`이었다. 부트 스크립트는 실패 시 base를 다시 load 하고 overlay 없이 부팅하는 폴백을 둬야 한다.

> `fdt_overlay_apply` is destructive on failure — libfdt says to expect the base to be modified even on error, and its error path actually erases the base's magic so a half-patched blob can't be used. I reproduced it with both a base missing `__symbols__` and a missing `fdt resize`: right after the failure every `fdt print` returned `FDT_ERR_BADMAGIC`. So the boot script must check the return value, reload the pristine base DTB on failure and boot without the overlay, and always resize the FDT with enough headroom before applying.

---

## 14. 직접 해보기

1. (개념) `__fixups__`의 `gpio0 = "/fragment@0/__overlay__/d6-sensor:reset-gpios:0"`에서 마지막 `0`을 `4`로 바꾸면 무슨 일이 생길까? `reset-gpios = <&gpio0 5 0>`의 어느 셀이 덮이나?
   정답/힌트: 오프셋은 **바이트** 단위. 4바이트 뒤 = 두 번째 셀(`5`)이 phandle로 덮이고 첫 셀은 `0xffffffff`로 남는다 → gpio 조회 실패.
2. (코드) `d6_hat.dtso`에 `&uart0 { d6,owner = "imu-hat"; };` fragment를 추가하고 fdtoverlay로 합친 뒤 `fdtget -t s d6_merged.dtb /pl011@9000000 d6,owner`로 확인하라. 런타임(`d6_ovl`)으로 적용하면 dmesg에 어떤 경고가 추가될까?
   정답/힌트: base 노드에 속성 추가 → `WARNING: memory leak will occur if overlay removed, property: /pl011@9000000/d6,owner`.
3. (관찰) 예제 11에서 `d6_sensor.ko`를 overlay **이후**에 insmod 하도록 순서를 바꿔 보라. probe 로그는 언제 찍히나? `/sys/bus/platform/devices/d6-sensor`는 언제부터 있나?
   정답/힌트: 디바이스는 overlay 적용 시 생기고(드라이버 없이), probe는 드라이버 등록 시점에 일어난다 — 디바이스·드라이버 어느 쪽이 먼저 와도 매칭된다(G 모듈).
4. (코드) `d6_ovl`의 `of_overlay_fdt_apply()` 마지막 인자에 `of_find_node_by_path("/d6-gpio-sim")` 같은 노드를 주고, overlay의 `target-path`를 `"/"`로 둔 채 적용하면 노드가 어디에 붙을까? (노드 refcount는 `of_node_put()`으로 정리)
   정답/힌트: `target-path`가 그 base 노드 기준으로 해석되어 그 밑에 붙는다 — RP1·LAN966x가 PCI 디바이스 노드 밑에 붙이는 방식.
5. (관찰) U-Boot 경로에서 `fdt apply` 직전에 `fdt header`를 찍고, `fdt resize 4096` 후 다시 찍어 `totalsize`가 어떻게 바뀌는지 보라.
   정답/힌트: totalsize가 4096 이상 커지고(정렬 포함), 그만큼이 overlay가 쓸 수 있는 빈 공간이다.
6. (설계) 센서 키트에 IMU 보드 2종 × GNSS 유무 × 카메라 2종이 있다. dtb/dtbo 파일 목록과 "누가 어떤 overlay를 고르는지"(EEPROM? 설정 파일?)를 설계하고, 각 overlay가 참조할 base label 목록을 적어라.
   정답/힌트: base 1 + overlay 5(IMU-A, IMU-B, GNSS, CAM-219, CAM-477). label 목록(`i2c1`, `uart2`, `csi0`, `gpio0`, `reg_3v3` 등)이 곧 base와 overlay 사이의 인터페이스 문서다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| overlay | DT 패치 | base 트리에 노드를 붙이고 속성을 바꾸는 조각 |
| `.dtso` / `.dtbo` | overlay 소스 / 바이너리 | 최근 커널 트리는 overlay 소스 확장자로 `.dtso`를 쓴다 (예전엔 `.dts`) |
| `/plugin/;` | overlay 선언 | 미해결 label을 에러 대신 fixup으로 남기라는 dtc 지시어 |
| fragment | 패치 한 건 | `target` 또는 `target-path` + `__overlay__` |
| `target` / `target-path` | 붙일 위치 | phandle(label) 또는 경로 문자열로 base 노드 지정 |
| `__overlay__` | 붙일 내용 | target에 합쳐질 속성·자식 노드 |
| phandle | 노드 ID | 노드를 가리키는 32비트 정수, 다른 노드의 속성이 참조 |
| `-@` | 심볼 생성 | dtc가 `__symbols__`를 만들고 label 붙은 노드에 phandle 부여 |
| `__symbols__` | 심볼 테이블 | label → 노드 경로. base의 export 테이블 |
| `__fixups__` | 외부 재배치 | label → "경로:속성:오프셋" 목록 |
| `__local_fixups__` | 내부 재배치 | overlay 내부 phandle이 쓰인 위치 목록 (delta 보정) |
| live tree | 커널의 DT | `struct device_node` 트리, `/proc/device-tree`로 노출 |
| changeset | 변경 트랜잭션 | `struct of_changeset`, 적용·되돌리기 로그 |
| ovcs_id | overlay 핸들 | `of_overlay_fdt_apply()`가 돌려주는 번호, remove에 사용 |
| OF reconfig notifier | 변경 알림 | 노드·속성 변경마다 호출, 버스들이 디바이스를 만든다 |
| `fdtoverlay` | 오프라인 도구 | dtc 패키지의 base+overlay 병합기 (libfdt) |
| `fdt apply` | U-Boot 명령 | 메모리의 FDT에 overlay 적용 (libfdt) |
| configfs overlay | 다운스트림 기능 | `/sys/kernel/config/device-tree/overlays`, mainline 아님 |

---

## 16. 요약 & 체크리스트

Device Tree overlay는 base dtb를 고정한 채 확장 보드·카메라·공장 변형을 `.dtbo` 조각으로 얹는 방법이다. overlay는 base를 모르는 채 컴파일되므로 base label 참조는 `__fixups__`로, 내부 phandle은 `__local_fixups__`로 남고, base는 `-@`로 `__symbols__`를 내보내야 한다 — 적용은 링커처럼 label → 경로 → phandle을 써 넣는 일이다. 같은 `.dtbo`를 빌드 시간(`fdtoverlay`, Kbuild `-dtbs`), 부팅 시간(U-Boot `fdt resize`/`fdt apply`, RPi·Jetson 펌웨어), 런타임(드라이버가 품은 dtbo로 `of_overlay_fdt_apply()`)에 적용할 수 있고, 실습에서 세 경로 모두 `d6_sensor`가 probe 됐다. 런타임 적용은 OF reconfig 알림 → `of_platform_notify()` → 디바이스 생성 → 동기 probe로 이어지며, 제거에는 스택 순서·메모리 누수·재probe 없음 같은 한계가 있다. mainline에는 configfs overlay가 없다. 도구마다 실패 양상이 달라서(fdtoverlay는 두 번 적용이 조용히 성공, 커널은 `-EINVAL`, U-Boot는 실패 시 블롭 파괴) 어디서 적용하는지 알고 디버깅해야 한다.

- [ ] overlay가 왜 필요한지 펌웨어 SKU 테이블 비유로 설명할 수 있다
- [ ] fragment(`target`/`target-path`/`__overlay__`) 문법과 `&label`, `&{/path}` sugar를 서로 바꿔 쓸 수 있다
- [ ] `/plugin/;`과 `-@`의 역할을 각각 한 문장으로 말할 수 있다
- [ ] `__symbols__`, `__fixups__`, `__local_fixups__`를 역컴파일 출력에서 읽고 B3 링커 개념에 대응시킬 수 있다
- [ ] `fdtoverlay`로 merged dtb를 만들고 diff로 fixup 결과를 검증할 수 있다
- [ ] U-Boot에서 `fdt addr` → `fdt resize` → `fdt apply` → `fdt print` → `booti`를 수행하고 실패 시 블롭이 망가진다는 점을 고려할 수 있다
- [ ] Kbuild `.dtbo.o` 규칙으로 dtbo를 모듈에 넣고 `of_overlay_fdt_apply()`/`of_overlay_remove()`를 호출하는 코드를 짤 수 있다
- [ ] 런타임 overlay가 notifier를 거쳐 probe까지 가는 경로를 그림으로 설명할 수 있다
- [ ] overlay 제거의 한계 4가지 이상을 말할 수 있다
- [ ] configfs overlay가 mainline이 아님을 근거와 함께 말할 수 있다
- [ ] Raspberry Pi, BeagleBone/TI, Jetson의 overlay 적용 위치를 비교할 수 있다
- [ ] "overlay가 안 먹는다"를 `/proc/device-tree/__symbols__`부터 순서대로 진단할 수 있다

---

## 참고 자료

- Linux 커널 문서 — Devicetree Overlay Notes: https://docs.kernel.org/devicetree/overlay-notes.html (소스: `Documentation/devicetree/overlay-notes.rst`)
- Linux 커널 문서 — Devicetree Dynamic Resolver Notes: https://docs.kernel.org/devicetree/dynamic-resolution-notes.html
- Linux 커널 문서 — Devicetree Changesets: https://docs.kernel.org/devicetree/changesets.html
- 커널 소스(6.18): `drivers/of/overlay.c`, `drivers/of/resolver.c`, `drivers/of/dynamic.c`, `drivers/of/platform.c`(`of_platform_notify`), `scripts/Makefile.dtbs`(`cmd_wrap_S_dtb`, `cmd_fdtoverlay`), `drivers/misc/rp1/`, `drivers/misc/lan966x_pci.c`, `drivers/fpga/of-fpga-region.c` — https://elixir.bootlin.com/linux/v6.18/source
- U-Boot 문서 — Device Tree Overlays: https://docs.u-boot.org/en/latest/usage/fdt_overlays.html, FIT overlay: `doc/usage/fit/overlay-fdt-boot.rst`
- dtc / libfdt 소스(`libfdt/fdt_overlay.c`, `fdtoverlay.c`): https://git.kernel.org/pub/scm/utils/dtc/dtc.git
- Devicetree Specification: https://www.devicetree.org/specifications/
- Bootlin "Device Tree 101" 웨비나 슬라이드(Thomas Petazzoni, 2021) — bootlin.com의 conferences/docs 페이지에서 찾을 수 있다
- Raspberry Pi 문서 — Device Trees, overlays, and parameters: https://www.raspberrypi.com/documentation/computers/configuration.html (기억 기반 위치 — 섹션 이름 확인 필요)
