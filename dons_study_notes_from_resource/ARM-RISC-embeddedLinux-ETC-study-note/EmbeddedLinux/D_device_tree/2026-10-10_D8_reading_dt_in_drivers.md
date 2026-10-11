# D8. 드라이버에서 DT 읽기 — `of_*` 와 `device_property_*` API로 노드의 값을 꺼내는 법

> **이 노트를 다 읽으면**: DTB가 커널 안에서 `struct device_node`·`struct property` 연결 리스트로 바뀌는 모습을 그리고 `of_find_property()`가 무엇을 하는지 설명할 수 있다 · `of_property_read_u32/_array/_u64/_string/_string_index`, `of_property_read_bool` vs `of_property_present`, 자식 순회, `of_parse_phandle_with_args`를 직접 써서 노드 하나를 완전히 파싱할 수 있다 · 같은 일을 `device_property_*`(fwnode) API로 하고, 왜 새 드라이버가 그쪽을 써야 하는지(DT·ACPI·swnode 겸용) 말할 수 있다 · `-EINVAL`/`-ENODATA`/`-EOVERFLOW`를 구분해서 "optional 속성 + 기본값"과 "required 속성" 코드를 올바르게 짤 수 있다
> **왜 필요한가**: 드라이버 `probe()`의 첫 20줄은 거의 항상 "DT에서 설정값 읽기"다. 면접에서는 "센서 드라이버에 sample rate를 DT로 넘기려면?", "`of_property_read_u32`가 실패하면 어떻게 처리하나?", "`of_*`와 `device_property_*` 차이는?", "`of_node_put`은 언제 부르나?"로 나온다
> **Don 기준 난이도**: 빅엔디안 바이트 순서, 구조체 연결 리스트, 참조 카운트, 에러 코드 전파는 펌웨어에서 이미 익숙하다 / 새로 배울 부분은 커널의 DT 표현(unflatten), OF API 이름 체계, fwnode 추상화, refcount 규칙과 `_scoped` 매크로
> **선행 노트**: D2(DT 문법), D3(compatible 매칭 → probe), D4(자원 기술 — phandle), D5(`/proc/device-tree`, `dtc`) — 모듈 빌드는 E 모듈과 F 모듈에서 자세히 다룬다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어를 만들 때 "이 보드는 NAND 채널이 8개, 저 보드는 4개"를 어떻게 처리했는지 떠올려 보자. 아마 `board_config.h`에 `#define NUM_CH 8`을 두거나, 플래시의 특정 섹터에 있는 설정 블록(구조체)을 부팅 때 읽어서 전역 변수에 채웠을 것이다. **코드와 보드 설정을 분리**하는 것, 그리고 설정 블록은 정해진 바이트 형식으로 저장돼 있어서 읽는 쪽이 파싱해야 한다는 것 — 이게 Device Tree를 읽는 드라이버의 본질이다.

Linux에서는 그 "설정 블록"이 DTB(Device Tree Blob)이고, 드라이버는 `probe()` 안에서 자기 노드의 속성(property)을 읽어 동작을 정한다. 예를 들어 센서 키트의 IMU 드라이버라면:

- `reg = <0x68>` — I2C 주소 (버스 코어가 대신 읽어 준다, D4)
- `interrupts`, `reset-gpios`, `vdd-supply` — 자원 (각 서브시스템 API가 대신 읽어 준다, D4)
- `sample-rate = <1000>`, `mount-matrix = ...`, `label = "imu-left"` — **드라이버 자신이 직접 읽어야 하는 설정값.** 이 노트의 주제다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="140" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="80" y="45" text-anchor="middle" font-size="13">board.dts / .dtsi</text>
  <rect x="180" y="20" width="150" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="255" y="45" text-anchor="middle" font-size="13">board.dtb (FDT)</text>
  <rect x="360" y="20" width="130" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="425" y="45" text-anchor="middle" font-size="13">부트로더 / QEMU</text>
  <rect x="520" y="20" width="150" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="595" y="45" text-anchor="middle" font-size="13">커널 early boot</text>
  <line x1="150" y1="40" x2="178" y2="40" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="330" y1="40" x2="358" y2="40" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="490" y1="40" x2="518" y2="40" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="80" y="78" text-anchor="middle" font-size="12">사람이 쓰는 텍스트</text>
  <text x="255" y="78" text-anchor="middle" font-size="12">dtc가 만든 바이너리</text>
  <text x="425" y="78" text-anchor="middle" font-size="12">x0 = DTB 주소</text>
  <text x="595" y="78" text-anchor="middle" font-size="12">unflatten_device_tree()</text>
  <rect x="460" y="110" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="565" y="135" text-anchor="middle" font-size="13">struct device_node 트리</text>
  <rect x="240" y="110" width="190" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="335" y="135" text-anchor="middle" font-size="13">of_platform_populate()</text>
  <rect x="10" y="110" width="200" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="110" y="135" text-anchor="middle" font-size="13">드라이버 probe(dev)</text>
  <line x1="595" y1="86" x2="595" y2="108" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="460" y1="130" x2="432" y2="130" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="240" y1="130" x2="212" y2="130" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="335" y="103" text-anchor="middle" font-size="12">platform_device 생성, dev.of_node 연결</text>
  <rect x="10" y="200" width="310" height="70" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="165" y="225" text-anchor="middle" font-size="13">of_property_read_u32(np, ...)</text>
  <text x="165" y="250" text-anchor="middle" font-size="12">DT 전용 · device_node를 직접 받음</text>
  <rect x="350" y="200" width="320" height="70" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="225" text-anchor="middle" font-size="13">device_property_read_u32(dev, ...)</text>
  <text x="510" y="250" text-anchor="middle" font-size="12">DT · ACPI · swnode 공용 (fwnode)</text>
  <line x1="90" y1="150" x2="120" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="150" y1="150" x2="440" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
</svg>
```
그림 1 — DTS가 DTB로 컴파일되고, 커널이 부팅 초기에 이를 `device_node` 트리로 풀어 놓는다(unflatten). 드라이버는 `probe()`에서 두 가지 API 계열 중 하나로 그 트리를 읽는다.

이 노트는 그림 1의 마지막 줄을 끝까지 판다. 순서는 이렇다.

1. 실습용 "풍부한" 노드를 QEMU `virt` DT에 추가한다 (§1).
2. 커널이 그 노드를 메모리에 어떤 구조체로 들고 있는지 본다 (§2).
3. OF API로 모든 타입을 읽는다 (§3) → 일부러 에러를 낸다 (§4) → 자식 노드와 refcount (§5) → phandle (§6).
4. 같은 것을 `device_property_*`로 읽고, DT가 없는 "보드"(software node)에서도 같은 드라이버가 도는 걸 본다 (§7).
5. 유저스페이스에서 같은 바이트를 본다 (§8).

> 실행 환경: 커널 모듈은 Lima VM `elinux`(호스트 커널 `uname -r` = `7.0.0-38-generic`) 안에서 공용 랩의 **Linux 6.18.55** 빌드 트리로 컴파일하고, QEMU `virt`(cortex-a57, 512 MB, TCG) 게스트에서 insmod해서 확인했다. 아래 모든 출력은 실제 실행 결과다. dmesg 타임스탬프는 실행마다 다르다. 모듈 빌드는 `-Wall` 포함 커널 기본 경고 옵션에서 **경고 0개**다.

---

## 1. 실습 준비 — 속성 타입을 전부 담은 노드 하나

### 1.1 어떤 노드를 만들까

센서 키트의 IMU를 흉내 낸 노드 `d8-sensor-a`를 만든다. 속성마다 하나의 "타입"을 대표하게 했다.

| 속성 | DTS 표기 | 타입 | 이 노트에서 확인할 것 |
|---|---|---|---|
| `sample-rate` | `<1000>` | u32 (cell 1개) | 기본 읽기, 빅엔디안 |
| `gains` | `<1 2 4 8>` | u32 배열 | count, array, 크기 초과 에러 |
| `timestamp-base` | `/bits/ 64 <0x123456789abcdef0>` | u64 (cell 2개) | 64비트 조립 |
| `label` | `"imu-left"` | 문자열 | `of_property_read_string` |
| `modes` | `"low-power", "normal", "burst"` | 문자열 리스트 | index, count, match |
| `d8,invert-x` | (값 없음) | bool | 존재 = true |
| `d8,legacy-flag` | `<1>` | 값 있는 "bool" | `read_bool`의 경고 |
| `d8,empty-name` | (값 없음) | 빈 속성 | `-ENODATA` vs `-EOVERFLOW` |
| `d8,clk` | `<&d8_clkgen 3 7>` | phandle + 인자 2개 | `#d8-cells`, `of_parse_phandle_with_args` |
| `channel@0..2` | 자식 노드, `reg`, 하나는 `disabled` | 자식 | 순회, available, refcount |
| `d8,timeout-ms` | **일부러 없음** | optional | 기본값 관용구 |

그리고 `compatible = "d8,sensor-v1"`인 단순한 두 번째 노드 `d8-sensor-b`를 둬서 §7에서 "변형(variant)별 match data"를 확인한다. 노드 이름·벤더 접두어에 노트 ID `d8`을 붙인 건 같은 VM에서 다른 노트 작업과 충돌하지 않기 위해서다(실제 제품이라면 `vendor,` 접두어 자리에 회사 이름이 들어간다).

### 1.2 예제 1 — DTS 조각을 virt.dts에 붙이고 dtc로 컴파일

무엇을 확인하나: 랩의 `virt.dts`(QEMU가 만든 DT를 덤프한 것) 끝에 우리 노드를 붙여 새 DTB를 만들고, `fdtdump`로 컴파일 결과를 확인한다.

```dts
	d8_clkgen: d8-clkgen {
		compatible = "d8,clkgen";
		#d8-cells = <2>;
	};

	d8-sensor-a {
		compatible = "d8,sensor-v2";
		sample-rate = <1000>;
		gains = <1 2 4 8>;
		label = "imu-left";
		modes = "low-power", "normal", "burst";
		d8,invert-x;
		d8,legacy-flag = <1>;
		d8,empty-name;
		timestamp-base = /bits/ 64 <0x123456789abcdef0>;
		d8,clk = <&d8_clkgen 3 7>;
		#address-cells = <1>;
		#size-cells = <0>;

		channel@0 {
			reg = <0>;
			label = "accel-x";
		};
		channel@1 {
			reg = <1>;
			label = "accel-y";
			status = "disabled";
		};
		channel@2 {
			reg = <2>;
			label = "accel-z";
		};
	};

	d8-sensor-b {
		compatible = "d8,sensor-v1";
		sample-rate = <200>;
		label = "imu-right";
	};
```

위 조각을 Mac 쪽 `~/.elinux_scratch/D8/d8_nodes.dtsi`에 두고, VM 안에서 다음 스크립트로 루트 노드의 닫는 `};` 바로 앞에 끼워 넣는다.

```sh
#!/bin/sh
# mkdtb.sh: build /var/tmp/D8/d8.dtb = virt.dts + d8 nodes (inserted before the root's closing brace)
set -e
mkdir -p /var/tmp/D8 && cd /var/tmp/D8
head -n -1 /var/lab/out/virt.dts > d8.dts
cat /Users/donh/.elinux_scratch/D8/d8_nodes.dtsi >> d8.dts
echo "};" >> d8.dts
dtc -q -I dts -O dtb -o d8.dtb d8.dts
ls -l d8.dtb
```

```sh
limactl shell elinux -- bash -lc 'sh ~/.elinux_scratch/D8/mkdtb.sh; cd /var/tmp/D8; fdtdump d8.dtb 2>/dev/null | head -12'
```

```text
-rw-rw-r-- 1 donh donh 8294 Oct 10 10:23 d8.dtb
/dts-v1/;
// magic:		0xd00dfeed
// totalsize:		0x2066 (8294)
// off_dt_struct:	0x38
// off_dt_strings:	0x1e30
// off_mem_rsvmap:	0x28
// version:		17
// last_comp_version:	16
// boot_cpuid_phys:	0x0
// size_dt_strings:	0x236
// size_dt_struct:	0x1df8
```

`fdtdump`로 우리 노드 부분만 보면 dtc가 무엇을 했는지 보인다 (가운데 부분 생략).

```text
    d8-clkgen {
        compatible = "d8,clkgen";
        #d8-cells = <0x00000002>;
        phandle = <0x00000001>;
    };
    d8-sensor-a {
        compatible = "d8,sensor-v2";
        sample-rate = <0x000003e8>;
        gains = <0x00000001 0x00000002 0x00000004 0x00000008>;
        label = "imu-left";
        modes = "low-power", "normal", "burst";
        d8,invert-x;
        d8,legacy-flag = <0x00000001>;
        d8,empty-name;
        timestamp-base = <0x12345678 0x9abcdef0>;
        d8,clk = <0x00000001 0x00000003 0x00000007>;
        ...
```

출력에서 볼 것:

- `d8_clkgen:` 라벨을 누군가 `&d8_clkgen`으로 참조했기 때문에 dtc가 **`phandle = <1>` 속성을 자동으로 추가**했다. 그리고 `d8,clk`의 `&d8_clkgen`은 숫자 `0x00000001`로 바뀌었다. DTB에는 라벨이 없다. 오직 숫자 phandle만 있다.
- `/bits/ 64 <0x123456789abcdef0>`는 그냥 cell 2개 `<0x12345678 0x9abcdef0>`가 됐다. **DTB에는 타입 정보가 없다.** u64인지 u32 두 개인지는 읽는 쪽(드라이버)이 정한다. 이 사실이 §4의 함정 대부분의 원인이다.

### 1.3 부팅 스크립트

이후 모든 커널 예제는 같은 방식으로 돌린다: 랩 rootfs를 복사해 `.ko`와 게스트 스크립트(`/d8.sh`)를 넣고, `-dtb`로 우리 DTB를 주고 부팅한다. 랩 initramfs의 `LAB_CMD=sh~/d8.sh`가 스크립트를 실행하고 자동으로 전원을 끈다.

```sh
#!/bin/sh
# boot.sh guest_script.sh -> boots lab kernel with d8.dtb, runs script, prints console
set -e
W=/var/tmp/D8
rm -rf $W/r && mkdir -p $W/r && cp -a /var/lab/build/rootfs/. $W/r/
cp $W/mod/*.ko $W/mod/d8_dtread $W/r/ 2>/dev/null || true
cp "$1" $W/r/d8.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd $W/i.gz -dtb $W/d8.dtb \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/d8.sh" | tr -d '\r'
```

모듈 빌드는 랩 커널 빌드 트리를 쓴다 (VM이 arm64라 `CROSS_COMPILE` 불필요). Kbuild 파일 한 줄:

```make
obj-m := d8_of.o d8_err.o d8_child.o d8_phandle.o d8_fw.o d8_swnode.o
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/D8/mod && cp ~/.elinux_scratch/D8/m/* /var/tmp/D8/mod/ && cd /var/tmp/D8/mod && make -C /var/lab/out/linux-build M=$PWD modules'
```

```text
make: Entering directory '/var/lab/build/linux'
  CC [M]  d8_of.o
  CC [M]  d8_err.o
  CC [M]  d8_child.o
  CC [M]  d8_phandle.o
  CC [M]  d8_fw.o
  CC [M]  d8_swnode.o
  MODPOST Module.symvers
  CC [M]  d8_of.mod.o
  CC [M]  .module-common.o
  LD [M]  d8_of.ko
  ...
  LD [M]  d8_swnode.ko
make: Leaving directory '/var/lab/build/linux'
```

경고 줄이 하나도 없다(중간 `.mod.o`/`.ko` 줄은 생략). 처음에는 `MODULE_DESCRIPTION()`을 빠뜨려서 modpost가 모듈마다 `WARNING: modpost: missing MODULE_DESCRIPTION()`을 냈다 — 최근 커널의 modpost는 이걸 경고로 잡는다. 한 줄 추가로 해결했다.

---

## 2. 커널 안의 DT — FDT blob에서 device_node 트리로

### 2.1 직관: "플래시의 설정 블록을 RAM의 연결 리스트로 파싱해 둔다"

DTB는 **flattened** 형식이다. 바이트를 순서대로 읽어야만 구조를 알 수 있는 토큰 스트림이다 (D5). 매번 이걸 처음부터 스캔하면 느리니까, 커널은 부팅 초기에 `unflatten_device_tree()`로 한 번 쭉 파싱해서 **포인터로 연결된 트리**를 만들어 둔다. 펌웨어로 치면 "부팅 때 설정 섹터를 한 번 읽어서 `struct board_cfg` 배열을 채워 두는 것"과 같다.

### 2.2 예제 2 — blob 안의 노드를 바이트 단위로 보기

무엇을 확인하나: DTB의 struct 블록에서 `d8-sensor-a`의 시작 토큰과 첫 두 속성을 직접 디코딩한다. (Python은 VM에서 `-I`로 실행)

```python
# blobpeek.py: find the FDT_BEGIN_NODE of d8-sensor-a and dump the first two properties raw
import struct, sys
b = open(sys.argv[1], 'rb').read()
off_struct, off_str = struct.unpack('>II', b[8:16])
i = b.index(b'd8-sensor-a\0', off_struct) - 4          # FDT_BEGIN_NODE token
print(f"off_dt_struct=0x{off_struct:x} off_dt_strings=0x{off_str:x}")
print(f"node at 0x{i:x}: token={struct.unpack('>I', b[i:i+4])[0]} (1=BEGIN_NODE)")
p = i + 4 + ((len('d8-sensor-a') + 1 + 3) & ~3)        # name padded to 4
for _ in range(2):
    tok, ln, nameoff = struct.unpack('>III', b[p:p+12])
    name = b[off_str+nameoff:b.index(b'\0', off_str+nameoff)].decode()
    val = b[p+12:p+12+ln]
    print(f"0x{p:x}: token={tok}(3=PROP) len={ln} nameoff=0x{nameoff:x}->'{name}' value={val.hex(' ')}")
    p += 12 + ((ln + 3) & ~3)
```

```sh
limactl shell elinux -- bash -lc 'python3 -I ~/.elinux_scratch/D8/blobpeek.py /var/tmp/D8/d8.dtb; xxd -s 0x1c04 -l 0x3c /var/tmp/D8/d8.dtb'
```

```text
off_dt_struct=0x38 off_dt_strings=0x1e30
node at 0x1c04: token=1 (1=BEGIN_NODE)
0x1c14: token=3(3=PROP) len=13 nameoff=0x3f->'compatible' value=64 38 2c 73 65 6e 73 6f 72 2d 76 32 00
0x1c30: token=3(3=PROP) len=4 nameoff=0x1d8->'sample-rate' value=00 00 03 e8
00001c04: 0000 0001 6438 2d73 656e 736f 722d 6100  ....d8-sensor-a.
00001c14: 0000 0003 0000 000d 0000 003f 6438 2c73  ...........?d8,s
00001c24: 656e 736f 722d 7632 0000 0000 0000 0003  ensor-v2........
00001c34: 0000 0004 0000 01d8 0000 03e8            ............
```

출력에서 볼 것:

- 노드는 `FDT_BEGIN_NODE`(1) + 이름(NUL 포함, 4바이트 정렬), 속성은 `FDT_PROP`(3) + `len` + `nameoff` + 값(4바이트 정렬). 모든 정수 필드가 **빅엔디안**이다.
- 속성 이름은 struct 블록에 없고 strings 블록의 **오프셋**(`nameoff`)으로만 나온다. `compatible`, `reg` 같은 이름이 수백 번 반복돼도 문자열은 한 번만 저장된다.
- `sample-rate`의 값 4바이트는 `00 00 03 e8` = 1000. ARM64는 리틀엔디안이므로 이 4바이트를 `u32`로 그냥 캐스팅하면 `0xe8030000`이 된다. API가 변환해 주는 이유다 (§3.6).

### 2.3 struct device_node 와 struct property — 랩 소스에서 확인

랩 소스 `/var/lab/src/linux-6.18.55/include/linux/of.h`에서 그대로 옮긴 정의다 (SPARC 전용 필드 생략).

```c
struct property {
	char	*name;
	int	length;
	void	*value;
	struct property *next;
#if defined(CONFIG_OF_DYNAMIC) || defined(CONFIG_SPARC)
	unsigned long _flags;
#endif
#if defined(CONFIG_OF_KOBJ)
	struct bin_attribute attr;	/* /sys/firmware/devicetree 파일 */
#endif
};

struct device_node {
	const char *name;
	phandle phandle;
	const char *full_name;
	struct fwnode_handle fwnode;

	struct	property *properties;
	struct	property *deadprops;	/* removed properties */
	struct	device_node *parent;
	struct	device_node *child;
	struct	device_node *sibling;
#if defined(CONFIG_OF_KOBJ)
	struct	kobject kobj;		/* refcount + sysfs */
#endif
	unsigned long _flags;
	void	*data;
};
```

필드별 의미:

| 필드 | 뜻 | 실습에서 본 값 |
|---|---|---|
| `name` | 유닛 주소(`@...`)를 뺀 노드 이름 | `d8-sensor-a`, 자식은 `channel` |
| `full_name` | 이 노드 한 단계 이름(유닛 주소 포함). 예전 커널은 전체 경로였지만 지금은 아니다 | `d8-sensor-a`, `channel@0` |
| `phandle` | 다른 노드가 참조할 때 쓰는 번호. 참조 안 되면 0 | sensor-a는 0, clkgen은 1 |
| `fwnode` | 통합 property API(§7)용 핸들. `ops = &of_fwnode_ops` | `dev_fwnode(dev)`가 이걸 가리킴 |
| `properties` | `struct property`의 **단일 연결 리스트** 머리 | DTS 순서대로 + 끝에 `name` |
| `parent/child/sibling` | 트리. child는 첫 자식, 나머지는 sibling 체인 | channel@0 → @1 → @2 |
| `kobj` | 참조 카운트(kref) + `/sys/firmware/devicetree/base` 노출 | §5에서 직접 읽어 본다 |
| `_flags` | `OF_POPULATED`, `OF_DETACHED` 같은 상태 비트 | — |

`struct property`의 `value`는 **unflatten할 때 복사하지 않고 DTB 안을 직접 가리킨다**(`drivers/of/fdt.c`의 `populate_properties()`: `pp->value = (__be32 *)val;`). 그래서 값은 여전히 빅엔디안 그대로다. 커널이 DTB 메모리를 부팅 후에도 보존하는 이유이기도 하다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="115" y="16" text-anchor="middle" font-size="13" font-weight="bold">FDT blob (d8.dtb)</text>
  <rect x="10" y="24" width="210" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="20" y="42" font-size="12">header: magic 0xd00dfeed</text>
  <rect x="10" y="54" width="210" height="22" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="20" y="70" font-size="12">mem_rsvmap</text>
  <rect x="10" y="80" width="210" height="214" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="98" font-size="12" font-weight="bold">struct 블록 (토큰 열)</text>
  <text x="20" y="120" font-size="12">1 BEGIN_NODE d8-sensor-a</text>
  <text x="20" y="140" font-size="12">3 PROP len=13 off=0x3f</text>
  <text x="32" y="158" font-size="12">64 38 2c 73 ... 32 00</text>
  <text x="20" y="178" font-size="12">3 PROP len=4 off=0x1d8</text>
  <text x="32" y="196" font-size="12">00 00 03 e8</text>
  <text x="20" y="216" font-size="12">... 속성 10개 더</text>
  <text x="20" y="236" font-size="12">1 BEGIN_NODE channel@0</text>
  <text x="20" y="256" font-size="12">... 2 END_NODE (자식)</text>
  <text x="20" y="276" font-size="12">2 END_NODE (d8-sensor-a)</text>
  <rect x="10" y="300" width="210" height="64" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="20" y="318" font-size="12" font-weight="bold">strings 블록</text>
  <text x="20" y="338" font-size="12">0x3f: compatible</text>
  <text x="20" y="356" font-size="12">0x1d8: sample-rate</text>
  <line x1="222" y1="150" x2="270" y2="150" stroke="currentColor" stroke-width="2" marker-end="url(#ah2)"/>
  <text x="246" y="140" text-anchor="middle" font-size="12">unflatten</text>
  <rect x="275" y="24" width="210" height="216" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="285" y="44" font-size="13" font-weight="bold">struct device_node</text>
  <text x="285" y="66" font-size="12">properties</text>
  <text x="285" y="86" font-size="12">name = "d8-sensor-a"</text>
  <text x="285" y="106" font-size="12">full_name = "d8-sensor-a"</text>
  <text x="285" y="126" font-size="12">phandle = 0</text>
  <text x="285" y="146" font-size="12">fwnode.ops = of_fwnode_ops</text>
  <text x="285" y="166" font-size="12">parent = 루트 노드 (/)</text>
  <text x="285" y="186" font-size="12">sibling = d8-sensor-b</text>
  <text x="285" y="206" font-size="12">kobj (refcount)</text>
  <text x="285" y="226" font-size="12">child</text>
  <line x1="360" y1="62" x2="508" y2="37" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="510" y="24" width="160" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="520" y="42" font-size="12">compatible · len 13</text>
  <rect x="510" y="66" width="160" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="520" y="84" font-size="12">sample-rate · len 4</text>
  <rect x="510" y="108" width="160" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="520" y="126" font-size="12">gains · len 16</text>
  <text x="590" y="160" text-anchor="middle" font-size="12">... (9개)</text>
  <rect x="510" y="176" width="160" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="520" y="194" font-size="12">name · len 12</text>
  <line x1="590" y1="50" x2="590" y2="64" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="590" y1="92" x2="590" y2="106" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="590" y1="134" x2="590" y2="146" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="590" y1="164" x2="590" y2="174" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="590" y="222" text-anchor="middle" font-size="12">next = NULL</text>
  <text x="590" y="242" text-anchor="middle" font-size="12">value는 blob 안을 가리킴</text>
  <line x1="300" y1="232" x2="320" y2="298" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="275" y="300" width="110" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="330" y="320" text-anchor="middle" font-size="12">channel@0</text>
  <rect x="405" y="300" width="150" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="480" y="320" text-anchor="middle" font-size="12">channel@1 disabled</text>
  <rect x="575" y="300" width="95" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="622" y="320" text-anchor="middle" font-size="12">channel@2</text>
  <line x1="385" y1="315" x2="403" y2="315" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="555" y1="315" x2="573" y2="315" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="472" y="352" text-anchor="middle" font-size="12">child는 첫 자식만, 나머지는 sibling 체인</text>
  <text x="472" y="372" text-anchor="middle" font-size="12">각 자식의 parent는 d8-sensor-a</text>
</svg>
```
그림 2 — 왼쪽은 DTB 안의 토큰 열(예제 2에서 디코딩한 값), 오른쪽은 unflatten 후 커널 메모리의 모습. 속성은 노드마다 단일 연결 리스트이고, 자식은 child + sibling 체인이다. disabled 노드도 트리에는 그대로 있다.

### 2.4 of_find_property — 모든 읽기 함수의 출발점

`drivers/of/base.c`의 실제 코드 (6.18.55):

```c
static struct property *__of_find_property(const struct device_node *np,
					   const char *name, int *lenp)
{
	struct property *pp;

	if (!np)
		return NULL;

	for (pp = np->properties; pp; pp = pp->next) {
		if (of_prop_cmp(pp->name, name) == 0) {
			if (lenp)
				*lenp = pp->length;
			break;
		}
	}

	return pp;
}
```

공개 함수 `of_find_property()`는 이걸 `devtree_lock`(raw spinlock)으로 감싼 것이다. 핵심 세 가지:

- **선형 탐색**이다. 노드당 속성이 많아야 수십 개라 해시 테이블을 둘 이유가 없다. 그래도 인터럽트 핸들러나 hot path에서 DT를 반복해서 읽지 말고, `probe()`에서 한 번 읽어 드라이버 private 구조체에 저장하는 게 원칙이다. 펌웨어에서 "설정 섹터를 매번 읽지 않고 부팅 때 RAM에 캐시"하는 것과 같다.
- `of_prop_cmp`는 `strcmp`(대소문자 구분)다.
- `np == NULL`이면 그냥 NULL을 돌려준다. 그래서 `dev->of_node`가 없는(DT 없이 만든) 디바이스에서 OF 함수를 불러도 크래시하지 않고 "속성 없음"(`-EINVAL`)이 된다.

### 2.5 예제 3 — 커널 모듈로 노드와 속성 리스트 직접 걷기, 그리고 OF API로 모든 타입 읽기

무엇을 확인하나: `dev.of_node`의 필드를 찍고, `properties` 리스트를 손으로 걸은 다음, 각 타입을 해당 OF 함수로 읽는다. `compatible = "d8,sensor-v2"`에 매칭되는 platform 드라이버다 (매칭 원리는 D3).

```c
// d8_of: read every property type of the d8 node with the OF API
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

static int d8_of_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct property *pp;
	const char *label, *mode;
	u32 rate, gains[4], tmo = 50;	/* 50 = default */
	u64 ts;
	int i, n, ret;
	dev_info(&pdev->dev, "node %pOF full_name=%s name=%s phandle=%u\n",
		 np, np->full_name, np->name, np->phandle);
	for (pp = np->properties; pp; pp = pp->next)	/* raw list walk */
		dev_info(&pdev->dev, "  prop %-16s len=%d\n", pp->name, pp->length);

	pp = of_find_property(np, "sample-rate", NULL);
	if (pp)
		dev_info(&pdev->dev, "raw sample-rate bytes %4ph -> be32_to_cpup=%u\n",
			 pp->value, be32_to_cpup(pp->value));
	ret = of_property_read_u32(np, "sample-rate", &rate);
	dev_info(&pdev->dev, "of_property_read_u32(sample-rate) ret=%d val=%u\n", ret, rate);

	n = of_property_count_u32_elems(np, "gains");
	ret = of_property_read_u32_array(np, "gains", gains, ARRAY_SIZE(gains));
	dev_info(&pdev->dev, "gains count=%d ret=%d -> %u %u %u %u\n",
		 n, ret, gains[0], gains[1], gains[2], gains[3]);

	ret = of_property_read_u64(np, "timestamp-base", &ts);
	dev_info(&pdev->dev, "of_property_read_u64 ret=%d val=0x%llx\n", ret, ts);

	ret = of_property_read_string(np, "label", &label);
	dev_info(&pdev->dev, "label ret=%d \"%s\"\n", ret, ret ? "-" : label);

	n = of_property_count_strings(np, "modes");
	for (i = 0; i < n; i++)
		if (!of_property_read_string_index(np, "modes", i, &mode))
			dev_info(&pdev->dev, "modes[%d]=\"%s\"\n", i, mode);
	dev_info(&pdev->dev, "match_string(burst)=%d\n",
		 of_property_match_string(np, "modes", "burst"));

	dev_info(&pdev->dev, "read_bool(d8,invert-x)=%d present(d8,invert-x)=%d\n",
		 of_property_read_bool(np, "d8,invert-x"),
		 of_property_present(np, "d8,invert-x"));
	ret = of_property_read_u32(np, "d8,timeout-ms", &tmo);	/* optional */
	dev_info(&pdev->dev, "timeout-ms ret=%d -> using %u\n", ret, tmo);
	return 0;
}
static const struct of_device_id d8_of_ids[] = {
	{ .compatible = "d8,sensor-v2" }, { }
};
MODULE_DEVICE_TABLE(of, d8_of_ids);
static struct platform_driver d8_of_drv = {
	.probe = d8_of_probe,
	.driver = { .name = "d8_of", .of_match_table = d8_of_ids },
};
module_platform_driver(d8_of_drv);
MODULE_DESCRIPTION("D8 DT-reading lab"); MODULE_LICENSE("GPL");
```

게스트 스크립트는 두 줄이다: `insmod /d8_of.ko` 와 `dmesg | grep d8_of`.

```text
lab initramfs: 6.18.55 up 5.07s
[    6.465425] d8_of: loading out-of-tree module taints kernel.
[    6.507458] d8_of d8-sensor-a: node /d8-sensor-a full_name=d8-sensor-a name=d8-sensor-a phandle=0
[    6.507565] d8_of d8-sensor-a:   prop compatible       len=13
[    6.507639] d8_of d8-sensor-a:   prop sample-rate      len=4
[    6.507652] d8_of d8-sensor-a:   prop gains            len=16
[    6.507662] d8_of d8-sensor-a:   prop label            len=9
[    6.507672] d8_of d8-sensor-a:   prop modes            len=23
[    6.507682] d8_of d8-sensor-a:   prop d8,invert-x      len=0
[    6.507692] d8_of d8-sensor-a:   prop d8,legacy-flag   len=4
[    6.507702] d8_of d8-sensor-a:   prop d8,empty-name    len=0
[    6.507713] d8_of d8-sensor-a:   prop timestamp-base   len=8
[    6.507722] d8_of d8-sensor-a:   prop d8,clk           len=12
[    6.507732] d8_of d8-sensor-a:   prop #address-cells   len=4
[    6.507741] d8_of d8-sensor-a:   prop #size-cells      len=4
[    6.507749] d8_of d8-sensor-a:   prop name             len=12
[    6.507785] d8_of d8-sensor-a: raw sample-rate bytes 00 00 03 e8 -> be32_to_cpup=1000
[    6.507866] d8_of d8-sensor-a: of_property_read_u32(sample-rate) ret=0 val=1000
[    6.507922] d8_of d8-sensor-a: gains count=4 ret=0 -> 1 2 4 8
[    6.508002] d8_of d8-sensor-a: of_property_read_u64 ret=0 val=0x123456789abcdef0
[    6.508070] d8_of d8-sensor-a: label ret=0 "imu-left"
[    6.512547] d8_of d8-sensor-a: modes[0]="low-power"
[    6.512619] d8_of d8-sensor-a: modes[1]="normal"
[    6.512634] d8_of d8-sensor-a: modes[2]="burst"
[    6.512665] d8_of d8-sensor-a: match_string(burst)=2
[    6.512701] d8_of d8-sensor-a: read_bool(d8,invert-x)=1 present(d8,invert-x)=1
[    6.512730] d8_of d8-sensor-a: timeout-ms ret=-22 -> using 50
```

출력에서 볼 것:

- `%pOF`는 노드의 **전체 경로**(`/d8-sensor-a`)를, `full_name` 필드는 **한 단계 이름**만 준다. 로그에는 `%pOF`를 쓰는 게 관례다. (`%pOFn`은 유닛 주소 없는 이름, `%pOFP`는 유닛 주소 포함 이름 — §5에서 사용.)
- 속성 리스트 끝에 DTS에 없던 `name`(len 12 = `"d8-sensor-a"` + NUL)이 있다. 옛 OpenFirmware 호환을 위해 unflatten이 붙이는 의사 속성이다. `/proc/device-tree`에도 `name` 파일로 보인다.
- `d8,invert-x`와 `d8,empty-name`은 **length 0**. bool 속성은 "값 없는 속성"이다.
- `d8,timeout-ms`는 없으므로 `ret=-22`(`-EINVAL`)이고 `tmo`는 미리 넣어 둔 50 그대로다. **OF 읽기 함수는 실패하면 출력 변수를 건드리지 않는다**(`drivers/of/property.c` 문서 주석: "pointer to return value, modified only if no error"). 이게 §4의 "optional + default" 관용구의 근거다.

---

## 3. OF API 정리 — 이름 체계만 알면 외울 게 없다

### 3.1 이름 규칙

`of_property_` + 동작(`read`, `count`, `match`, `present`) + 타입(`u8/u16/u32/u64`, `string`) + 형태(`_array`, `_index`, `_elems`). 모두 첫 인자가 `const struct device_node *`다.

| 함수 | 반환 | 내부에서 하는 일 |
|---|---|---|
| `of_property_read_u32(np, name, &v)` | 0 / 음수 errno | `of_property_read_u32_array(np, name, &v, 1)` 의 inline wrapper |
| `of_property_read_u32_array(np, name, buf, n)` | 0 / 음수 | 길이 ≥ 4·n 확인 후 `be32_to_cpup()` n번 |
| `of_property_read_variable_u32_array(np, name, buf, min, max)` | 읽은 개수 / 음수 | 위 두 함수의 실제 구현. `max=0`이면 상한 없음 |
| `of_property_read_u32_index(np, name, i, &v)` | 0 / 음수 | i번째 cell 하나 |
| `of_property_read_u64(np, name, &v)` | 0 / 음수 | 길이 ≥ 8 확인 후 `of_read_number(val, 2)` |
| `of_property_count_u32_elems(np, name)` | 개수 / 음수 | `length / 4` (나눠떨어지지 않으면 `-EINVAL` + pr_err) |
| `of_property_read_string(np, name, &s)` | 0 / 음수 | `s`가 **DTB 안을 가리키는 포인터**. 복사 아님 |
| `of_property_read_string_index(np, name, i, &s)` | 0 / 음수 | NUL로 구분된 i번째 문자열 |
| `of_property_count_strings(np, name)` | 개수 / 음수 | NUL 개수 세기 |
| `of_property_match_string(np, name, str)` | index / 음수 | 리스트에서 `str`의 위치 (`-ENODATA` 없음) |
| `of_property_read_bool(np, name)` | bool | 존재 여부. 값이 있으면 경고(6.18) |
| `of_property_present(np, name)` | bool | 존재 여부. 타입 무관, 경고 없음 |

`u8`/`u16` 배열 버전도 있다 (DTS에서 `/bits/ 8 <...>` 또는 `[01 02 03]` 바이트 문자열로 쓴 속성용). 그리고 `of_property_for_each_u32(np, name, u)`, `of_property_for_each_string(np, name, prop, s)` 순회 매크로가 있다.

### 3.2 정수와 배열 — "최소 길이"만 본다

`of_property_read_u32()`는 내부적으로 `sz_min = 1, sz_max = 0`(상한 없음)으로 부른다. 즉 **속성이 4바이트보다 길어도 에러가 아니고 첫 cell만 돌려준다.** `of_property_read_u32(np, "gains", &v)`는 1을 돌려줄 것이다. 반대로 `of_property_read_u32_array(np, "gains", buf, 8)`은 길이 16 < 32라 `-EOVERFLOW`다 (§4에서 실측).

배열 크기가 보드마다 다르면 이렇게 쓴다:

```c
n = of_property_count_u32_elems(np, "gains");	/* 먼저 개수 */
if (n < 0 || n > MAX_GAINS) return -EINVAL;
of_property_read_u32_array(np, "gains", priv->gains, n);
```

또는 한 번에 `of_property_read_variable_u32_array(np, "gains", buf, 1, MAX_GAINS)`로 "1~MAX개 사이면 OK, 실제 읽은 개수 반환"을 쓴다.

### 3.3 64비트 값

DTB엔 64비트 타입이 없다. `/bits/ 64 <X>`나 `<hi lo>`나 똑같이 cell 2개(상위 cell 먼저)다. `of_property_read_u64()`는 `of_read_number(val, 2)` = `(be32(val[0]) << 32) | be32(val[1])`로 조립한다. 예제 3에서 `0x123456789abcdef0`이 제대로 나왔다. `reg`의 주소처럼 `#address-cells`에 따라 1 또는 2 cell인 값은 직접 읽지 말고 `of_address_to_resource()`/`platform_get_resource()`에 맡긴다 (D4, F 모듈).

### 3.4 문자열과 문자열 리스트

문자열 리스트는 `"a\0b\0c\0"`처럼 NUL로 이어 붙인 바이트열이다 (`modes`의 length 23 = 10 + 7 + 6). `of_property_read_string()`이 돌려주는 포인터는 DTB 내부를 가리키므로:

- `kfree()` 하면 안 된다.
- 노드가 사라지지 않는 한(보통 DT는 부팅 내내 고정) 계속 유효하다. 단 overlay(D6)로 붙었다 떨어지는 노드라면 `devm_kstrdup()`으로 복사해 두는 게 안전하다.

`of_property_match_string()`은 "이 문자열이 리스트 몇 번째에 있나"를 준다. `clock-names`/`reg-names`/`interrupt-names`에서 이름으로 인덱스를 찾을 때 서브시스템 코드가 내부에서 이걸 쓴다.

### 3.5 bool — read_bool 과 present 의 차이 (6.x에서 바뀐 부분)

DT의 bool은 "속성이 있으면 true". 값이 없는 속성(`d8,invert-x;`)으로 쓴다. 문제는 예전 드라이버들이 `of_property_read_bool()`을 "값이 있는 속성이 존재하는가?"를 묻는 용도로도 남용했다는 것이다. 6.18의 구현은 이렇다 (`drivers/of/property.c`):

```c
bool of_property_read_bool(const struct device_node *np, const char *propname)
{
	struct property *prop = of_find_property(np, propname, NULL);

	/*
	 * Boolean properties should not have a value. Testing for property
	 * presence should either use of_property_present() or just read the
	 * property value and check the returned error code.
	 */
	if (prop && prop->length)
		pr_warn("%pOF: Read of boolean property '%s' with a value.\n", np, propname);

	return prop ? true : false;
}
```

규칙:

- **진짜 bool**(값 없는 플래그) → `of_property_read_bool()` / `device_property_read_bool()`.
- **"값 있는 속성이 존재하나?"** → `of_property_present()` / `device_property_present()`. 예: "`clocks`가 있으면 클럭을 켠다".
- 혹은 그냥 값을 읽고 에러 코드로 판단한다(§4).

예제 4(§4)에서 값이 있는 `d8,legacy-flag`를 `read_bool`로 읽으면 실제로 이 경고가 찍힌다.

### 3.6 엔디안 — DT cell은 항상 빅엔디안

예제 3의 `raw sample-rate bytes 00 00 03 e8 -> be32_to_cpup=1000`이 전부를 말해 준다. Don이 SSD 펌웨어에서 NVMe(리틀엔디안)와 SCSI/ATA 일부 필드(빅엔디안)를 오갈 때 쓰던 `be32_to_cpu`와 같은 함수다.

```
 /proc/device-tree/.../sample-rate     : 00 00 03 e8
 u32 cast on arm64 (little-endian)     : 0xe8030000 = 3892510720   (wrong)
 be32_to_cpup()  -> of_property_read_u32 : 0x000003e8 = 1000         (right)
```

`of_property_read_*`, `device_property_read_*`는 전부 내부에서 변환한다. **`of_find_property()`나 `of_get_property()`로 raw 포인터를 받았을 때만** 직접 `be32_to_cpup()`을 불러야 한다. raw 포인터의 타입이 `const __be32 *`인 이유다(sparse가 변환 누락을 잡아 준다).

---

## 4. 에러 코드와 "optional + default" 관용구

### 4.1 세 가지 에러의 정확한 의미 (6.18 소스 기준)

모든 정수 읽기의 공통 헬퍼 `of_find_property_value_of_size()`:

```c
static void *of_find_property_value_of_size(const struct device_node *np,
			const char *propname, u32 min, u32 max, size_t *len)
{
	const struct property *prop = of_find_property(np, propname, NULL);

	if (!prop)
		return ERR_PTR(-EINVAL);
	if (!prop->value)
		return ERR_PTR(-ENODATA);
	if (prop->length < min)
		return ERR_PTR(-EOVERFLOW);
	if (max && prop->length > max)
		return ERR_PTR(-EOVERFLOW);
	...
```

| 에러 | 이름이 주는 느낌 | 실제 의미 |
|---|---|---|
| `-EINVAL` (-22) | "잘못된 값" | **속성이 없다.** 가장 흔하다 |
| `-ENODATA` (-61) | "값이 없다" | 속성은 있는데 value 포인터가 NULL. 문자열 함수에서는 length 0일 때, 문자열 index가 범위 밖일 때 |
| `-EOVERFLOW` (-75) | "넘쳤다" | 길이가 요청 크기와 안 맞는다 (짧거나, 상한이 있는데 길거나) |
| `-EILSEQ` (-84) | "잘못된 바이트열" | 문자열 함수: NUL로 끝나지 않는다 |

함정: FDT에서 만든 노드는 값이 없는 속성이어도 `value` 포인터가 blob 안을 가리키므로 **NULL이 아니다**. 그래서 빈 속성을 u32로 읽으면 `-ENODATA`가 아니라 `-EOVERFLOW`(길이 0 < 4)가 나온다. 문서 주석의 "`-ENODATA` if property does not have a value"를 믿고 정수 읽기에서 `-ENODATA`를 분기하면 그 분기는 DT에선 안 탄다. 아래에서 실측한다.

### 4.2 예제 4 — 각 에러를 일부러 내기

무엇을 확인하나: 각 OF 함수와 `device_property_*` 함수가 어떤 상황에서 어떤 에러를 내는지, 그리고 "타입 검사가 없다"는 사실. 마지막에 required 속성이 없으면 probe를 실패시킨다.

```c
// d8_err: trigger each OF / device_property error code on purpose
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/property.h>

#define T(expr) do { int r_ = (expr); if (r_ < 0) \
	dev_info(dev, "%-52s = %pe\n", #expr, ERR_PTR(r_)); else \
	dev_info(dev, "%-52s = %d\n", #expr, r_); } while (0)

static int d8_err_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const char *s = NULL;
	u32 v = 0, arr[8];
	u64 v64;

	T(of_property_read_u32(np, "d8,missing", &v));
	T(of_property_read_string(np, "d8,empty-name", &s));
	T(of_property_read_u32(np, "d8,empty-name", &v));
	T(of_property_read_u32_array(np, "gains", arr, 8));
	T(of_property_read_u64(np, "sample-rate", &v64));
	T(of_property_read_string_index(np, "modes", 5, &s));
	T(of_property_count_u32_elems(np, "d8,empty-name"));
	T(of_property_read_u32(np, "label", &v));
	dev_info(dev, "  ^ label read as u32 = 0x%08x (no type check!)\n", v);
	T(of_property_read_string(np, "sample-rate", &s));
	dev_info(dev, "  ^ sample-rate read as string = \"%s\"\n", s);
	T(device_property_read_u32(dev, "d8,missing", &v));
	T(device_property_read_string(dev, "d8,empty-name", &s));
	T(device_property_read_u32(dev, "d8,empty-name", &v));
	T(device_property_read_u32_array(dev, "gains", arr, 8));
	T(device_property_count_u32(dev, "gains"));
	dev_info(dev, "read_bool(d8,legacy-flag)=%d\n",
		 of_property_read_bool(np, "d8,legacy-flag"));
	/* required property: fail probe with a clear message */
	if (of_property_read_u32(np, "d8,required-id", &v))
		return dev_err_probe(dev, -EINVAL, "missing required d8,required-id\n");
	return 0;
}
/* of_device_id 테이블과 platform_driver 등록은 예제 3과 같다 (.name = "d8_err") */
```

(지면상 마지막 15줄 — `of_device_id` 테이블, `platform_driver`, `module_platform_driver`, `MODULE_*` — 은 예제 3과 이름만 다르므로 생략했다. 실제 빌드한 파일에는 들어 있다.) `%pe`는 에러 포인터를 `-EINVAL` 같은 기호 이름으로 찍어 주는 printk 포맷이다.

게스트에서 `insmod /d8_err.ko; dmesg | grep -E "d8_err|OF: /"`:

```text
[   13.508236] d8_err d8-sensor-a: of_property_read_u32(np, "d8,missing", &v)           = -EINVAL
[   13.508637] d8_err d8-sensor-a: of_property_read_string(np, "d8,empty-name", &s)     = -ENODATA
[   13.508676] d8_err d8-sensor-a: of_property_read_u32(np, "d8,empty-name", &v)        = -EOVERFLOW
[   13.508707] d8_err d8-sensor-a: of_property_read_u32_array(np, "gains", arr, 8)      = -EOVERFLOW
[   13.508777] d8_err d8-sensor-a: of_property_read_u64(np, "sample-rate", &v64)        = -EOVERFLOW
[   13.508840] d8_err d8-sensor-a: of_property_read_string_index(np, "modes", 5, &s)    = -ENODATA
[   13.520815] d8_err d8-sensor-a: of_property_count_u32_elems(np, "d8,empty-name")     = 0
[   13.520874] d8_err d8-sensor-a: of_property_read_u32(np, "label", &v)                = 0
[   13.520894] d8_err d8-sensor-a:   ^ label read as u32 = 0x696d752d (no type check!)
[   13.520954] d8_err d8-sensor-a: of_property_read_string(np, "sample-rate", &s)       = 0
[   13.520971] d8_err d8-sensor-a:   ^ sample-rate read as string = ""
[   13.521005] d8_err d8-sensor-a: device_property_read_u32(dev, "d8,missing", &v)      = -EINVAL
[   13.521057] d8_err d8-sensor-a: device_property_read_string(dev, "d8,empty-name", &s) = -ENODATA
[   13.521087] d8_err d8-sensor-a: device_property_read_u32(dev, "d8,empty-name", &v)   = -EOVERFLOW
[   13.521113] d8_err d8-sensor-a: device_property_read_u32_array(dev, "gains", arr, 8) = -EOVERFLOW
[   13.521140] d8_err d8-sensor-a: device_property_count_u32(dev, "gains")              = 4
[   13.521171] OF: /d8-sensor-a: Read of boolean property 'd8,legacy-flag' with a value.
[   13.521240] d8_err d8-sensor-a: read_bool(d8,legacy-flag)=1
[   13.521382] d8_err d8-sensor-a: error -EINVAL: missing required d8,required-id
[   13.521510] d8_err d8-sensor-a: probe with driver d8_err failed with error -22
```

출력에서 볼 것:

- 없는 속성 → `-EINVAL` (OF든 device_property든 같다).
- 빈 속성: 문자열로 읽으면 `-ENODATA`, **u32로 읽으면 `-EOVERFLOW`**. count는 0.
- 길이 부족(`gains` 4개를 8개로, 4바이트를 u64로) → `-EOVERFLOW`.
- 문자열 index 범위 밖 → `-ENODATA`.
- **타입 검사가 없다**: 문자열 `"imu-left"`를 u32로 읽으면 성공하고 `0x696d752d`(= ASCII `i m u -`를 빅엔디안으로 읽은 값)가 나온다. u32 `1000`(`00 00 03 e8`)을 문자열로 읽으면 첫 바이트가 NUL이라 빈 문자열 `""`이 성공으로 나온다. **DT 바인딩(YAML 스키마, D7)과 `dtbs_check`가 이걸 막아 주는 유일한 안전장치다.**
- `device_property_*`는 DT 위에서는 OF 함수를 그대로 부르므로 에러 코드가 같다.
- `dev_err_probe()`가 `error -EINVAL: ...` 형식의 로그를 남기고, 드라이버 코어가 `probe ... failed with error -22`를 덧붙인다. `insmod` 자체는 성공한다(모듈 로드와 probe 성공은 별개 — F 모듈).

### 4.3 관용구 — optional, required, 범위 검사

```c
/* 1) optional + default: 실패 시 출력 변수가 안 바뀌는 성질을 이용 */
priv->timeout_ms = 50;
device_property_read_u32(dev, "vendor,timeout-ms", &priv->timeout_ms);

/* 2) optional인데 "없음"과 "잘못됨"을 구분하고 싶을 때 */
ret = device_property_read_u32(dev, "vendor,timeout-ms", &val);
if (ret == -EINVAL)
	val = 50;			/* 없음 → 기본값 */
else if (ret)
	return dev_err_probe(dev, ret, "bad vendor,timeout-ms\n");	/* 있는데 깨짐 */

/* 3) required */
ret = device_property_read_u32(dev, "sample-rate", &priv->rate);
if (ret)
	return dev_err_probe(dev, ret, "sample-rate missing\n");

/* 4) 값 범위는 API가 안 본다 — 드라이버가 본다 */
if (priv->rate < 10 || priv->rate > 4000)
	return dev_err_probe(dev, -EINVAL, "sample-rate %u out of range\n", priv->rate);
```

1번 형태가 upstream 드라이버에서 가장 흔하다. 2번은 "오타 난 DT를 조용히 기본값으로 덮지 않는다"는 장점이 있어서 리뷰어가 선호하기도 한다. 어느 쪽이든 **반환값을 무시하면서 출력 변수를 초기화하지 않는 것**만은 안 된다(미초기화 스택 값 사용).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="215" y="10" width="250" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="33" text-anchor="middle" font-size="13">ret = ..._read_u32(.., &amp;val)</text>
  <line x1="340" y1="46" x2="340" y2="58" stroke="currentColor"/>
  <line x1="80" y1="58" x2="595" y2="58" stroke="currentColor"/>
  <line x1="80" y1="58" x2="80" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="250" y1="58" x2="250" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="425" y1="58" x2="425" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="595" y1="58" x2="595" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="10" y="72" width="140" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="80" y="92" text-anchor="middle" font-size="13">ret == 0</text>
  <text x="80" y="108" text-anchor="middle" font-size="12">값을 얻음</text>
  <rect x="170" y="72" width="160" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="250" y="92" text-anchor="middle" font-size="13">-EINVAL</text>
  <text x="250" y="108" text-anchor="middle" font-size="12">속성이 없음</text>
  <rect x="350" y="72" width="150" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="425" y="92" text-anchor="middle" font-size="13">-EOVERFLOW</text>
  <text x="425" y="108" text-anchor="middle" font-size="12">길이가 안 맞음</text>
  <rect x="520" y="72" width="150" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="595" y="92" text-anchor="middle" font-size="13">-ENODATA</text>
  <text x="595" y="108" text-anchor="middle" font-size="12">값 없음(문자열)</text>
  <line x1="80" y1="116" x2="80" y2="150" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="10" y="152" width="140" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="80" y="172" text-anchor="middle" font-size="12">범위 검사</text>
  <text x="80" y="188" text-anchor="middle" font-size="12">(API는 안 해 줌)</text>
  <line x1="250" y1="116" x2="250" y2="150" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="170" y="152" width="160" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="250" y="179" text-anchor="middle" font-size="13">바인딩상 required?</text>
  <line x1="215" y1="196" x2="150" y2="238" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="160" y="216" text-anchor="middle" font-size="12">아니오</text>
  <line x1="285" y1="196" x2="330" y2="238" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="330" y="216" text-anchor="middle" font-size="12">예</text>
  <rect x="40" y="240" width="200" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="140" y="261" text-anchor="middle" font-size="13">기본값 유지, 계속</text>
  <text x="140" y="279" text-anchor="middle" font-size="12">val은 안 바뀜</text>
  <rect x="260" y="240" width="190" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="355" y="261" text-anchor="middle" font-size="13">probe 실패</text>
  <text x="355" y="279" text-anchor="middle" font-size="12">dev_err_probe(dev, ret)</text>
  <line x1="425" y1="116" x2="520" y2="238" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="595" y1="116" x2="560" y2="238" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="470" y="240" width="200" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="570" y="261" text-anchor="middle" font-size="13">DT가 깨짐 → 에러 반환</text>
  <text x="570" y="279" text-anchor="middle" font-size="12">기본값으로 덮지 말 것</text>
  <text x="340" y="318" text-anchor="middle" font-size="12">주의: DT의 빈 속성을 정수로 읽으면 -ENODATA가 아니라 -EOVERFLOW가 나온다</text>
</svg>
```
그림 3 — 속성 하나를 읽은 뒤의 판단 흐름. `-EINVAL`(없음)만 "optional이면 기본값"으로 처리하고, 길이가 어긋난 경우는 DT 버그로 보고 실패시킨다.

---

## 5. 자식 노드 순회와 refcount

### 5.1 왜 자식 노드인가

한 칩이 여러 개의 "채널·포트·LED·레귤레이터 출력"을 가질 때 DT는 자식 노드로 표현한다. 예: 다채널 ADC의 `channel@N`, PMIC의 `regulators { buck1 {...}; ldo2 {...}; }`, LED 컨트롤러의 `led@N`, 이더넷 스위치의 `ports { port@0 ... }`. 드라이버는 자식을 순회하며 채널마다 설정을 읽는다.

### 5.2 refcount 규칙 — Don이 아는 "핸들 빌리기/반납"

`device_node`는 `kobj` 안의 kref로 참조 카운트를 가진다(`CONFIG_OF_DYNAMIC`일 때만 실제로 카운트한다 — overlay·핫플러그로 노드가 사라질 수 있기 때문. 정적 DT만 쓰는 구성에서는 `of_node_get/put`이 빈 inline 함수다). 규칙은 RTOS의 "객체 핸들을 얻으면 반드시 release"와 같다.

- **노드를 돌려주는 OF 함수는 참조를 +1 한 채로 돌려준다**: `of_get_child_by_name`, `of_find_node_by_*`, `of_parse_phandle`, `of_get_parent`, `of_parse_phandle_with_args`의 `args.np` 등. 다 쓰면 `of_node_put()`.
- `for_each_child_of_node(parent, child)`는 내부에서 `of_get_next_child(parent, prev)`를 부르는데, 이 함수가 **다음 노드를 get 하고 이전 노드를 put** 한다 (`__of_get_next_child`: `next = prev ? prev->sibling : node->child; of_node_get(next); of_node_put(prev);`). 그래서 루프를 끝까지 돌면 균형이 맞는다.
- **루프를 `break`/`return`/`goto`로 빠져나가면 현재 `child`의 참조가 +1로 남는다.** 그 노드를 계속 쓸 거면 나중에 put, 아니면 즉시 `of_node_put(child)`.
- 6.x 커널은 이 실수를 없애려고 `_scoped` 매크로를 추가했다. 랩 6.18.55의 `include/linux/of.h`에 실제로 있다:

```c
#define for_each_child_of_node_scoped(parent, child) \
	for (struct device_node *child __free(device_node) =		\
	     of_get_next_child(parent, NULL);				\
	     child != NULL;						\
	     child = of_get_next_child(parent, child))
```

`__free(device_node)`는 GCC/Clang의 `cleanup` 속성으로, 변수가 스코프를 벗어날 때(`break`, `return` 포함) `DEFINE_FREE(device_node, ..., if (_T) of_node_put(_T))`에 정의된 put을 자동 호출한다. C++의 RAII, 또는 펌웨어에서 "함수 탈출 지점마다 unlock을 매크로로 강제"하던 것의 컴파일러 버전이다. 같은 계열로 `for_each_available_child_of_node_scoped`, `device_for_each_child_node_scoped`, `fwnode_for_each_available_child_node_scoped`가 있다. 루프 변수는 매크로 안에서 선언되므로 바깥에 따로 선언하지 않는다.

### 5.3 예제 5 — 자식 순회, available, 그리고 누수를 숫자로 보기

무엇을 확인하나: 전체 자식 vs available 자식, 각 자식의 `reg`·`label`, 그리고 `break` 후 `of_node_put()`을 안 하면 refcount가 실제로 1 남는 것. 랩 커널은 `CONFIG_OF_DYNAMIC=y`(overlay 지원 때문)라서 `kref_read(&np->kobj.kref)`로 카운트를 직접 볼 수 있다.

```c
// d8_child: iterate child nodes, and show what a missing of_node_put() leaks
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

static unsigned int refs(struct device_node *n)
{
	return kref_read(&n->kobj.kref);	/* needs CONFIG_OF_DYNAMIC */
}

static int d8_child_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node, *ch, *ch0;
	const char *label;
	u32 reg;

	dev_info(dev, "of_get_child_count=%d available=%d\n",
		 of_get_child_count(np), of_get_available_child_count(np));
	for_each_child_of_node(np, ch)
		dev_info(dev, "all:   %pOFP ok=%d\n", ch, of_device_is_available(ch));
	for_each_available_child_of_node_scoped(np, c) {
		of_property_read_u32(c, "reg", &reg);
		of_property_read_string(c, "label", &label);
		dev_info(dev, "avail: %pOFP reg=%u label=%s\n", c, reg, label);
	}

	ch0 = of_get_child_by_name(np, "channel");	/* +1 ref, keep it */
	dev_info(dev, "channel@0 refcount at start: %u\n", refs(ch0));
	for_each_child_of_node(np, ch)		/* BUG: break without put */
		if (ch == ch0)
			break;
	dev_info(dev, "after break w/o of_node_put: %u (leaked 1)\n", refs(ch0));
	of_node_put(ch);
	dev_info(dev, "after of_node_put(ch):      %u\n", refs(ch0));
	for_each_child_of_node_scoped(np, c)	/* scoped: put is automatic */
		if (c == ch0)
			break;
	dev_info(dev, "after scoped loop + break:  %u\n", refs(ch0));
	of_node_put(ch0);
	return 0;
}
/* of_device_id ("d8,sensor-v2") + platform_driver (.name = "d8_child") — 예제 3과 동일 */
```

```text
[   14.281380] d8_child d8-sensor-a: of_get_child_count=3 available=2
[   14.281932] d8_child d8-sensor-a: all:   channel@0 ok=1
[   14.284550] d8_child d8-sensor-a: all:   channel@1 ok=0
[   14.284774] d8_child d8-sensor-a: all:   channel@2 ok=1
[   14.284896] d8_child d8-sensor-a: avail: channel@0 reg=0 label=accel-x
[   14.284950] d8_child d8-sensor-a: avail: channel@2 reg=2 label=accel-z
[   14.285039] d8_child d8-sensor-a: channel@0 refcount at start: 3
[   14.285119] d8_child d8-sensor-a: after break w/o of_node_put: 4 (leaked 1)
[   14.285152] d8_child d8-sensor-a: after of_node_put(ch):      3
[   14.290903] d8_child d8-sensor-a: after scoped loop + break:  3
```

출력에서 볼 것:

- `of_get_child_count`는 3(disabled 포함), `of_get_available_child_count`는 2. `status = "disabled"`인 노드는 **트리에서 사라지지 않는다**. "available" 계열 함수가 거르는 것뿐이다. 채널 순회에는 거의 항상 available 버전을 쓴다.
- 시작 refcount 3의 절대값은 의미가 없다(초기 1 + 우리 `of_get_child_by_name` 1 + 커널 다른 곳의 보유분). 봐야 할 것은 **변화량**: 일반 루프에서 `break`하면 +1(4), 수동 `of_node_put` 후 3으로 복귀, `_scoped` 루프는 `break`해도 3 그대로.
- `of_get_child_by_name(np, "channel")`은 유닛 주소를 뺀 `name`으로 비교하므로 첫 번째 `channel@0`을 찾았다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" font-size="13" font-weight="bold">for_each_child_of_node(np, ch) — 반복마다 get(next) + put(prev)</text>
  <rect x="10" y="36" width="150" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="57" text-anchor="middle" font-size="12">1회차: ch = @0</text>
  <text x="85" y="75" text-anchor="middle" font-size="12">get @0</text>
  <rect x="180" y="36" width="150" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="255" y="57" text-anchor="middle" font-size="12">2회차: ch = @1</text>
  <text x="255" y="75" text-anchor="middle" font-size="12">get @1, put @0</text>
  <rect x="350" y="36" width="150" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="425" y="57" text-anchor="middle" font-size="12">3회차: ch = @2</text>
  <text x="425" y="75" text-anchor="middle" font-size="12">get @2, put @1</text>
  <rect x="520" y="36" width="150" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="595" y="57" text-anchor="middle" font-size="12">끝: ch = NULL</text>
  <text x="595" y="75" text-anchor="middle" font-size="12">put @2 → 균형</text>
  <line x1="160" y1="61" x2="178" y2="61" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="330" y1="61" x2="348" y2="61" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="500" y1="61" x2="518" y2="61" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="85" y1="86" x2="85" y2="128" stroke="#d0564a" stroke-width="2" marker-end="url(#ah4)"/>
  <text x="95" y="112" font-size="12">여기서 break</text>
  <rect x="10" y="130" width="310" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="165" y="151" text-anchor="middle" font-size="12">@0 참조가 +1로 남음 (실측 3 → 4)</text>
  <text x="165" y="169" text-anchor="middle" font-size="12">계속 쓸 거면 나중에, 아니면 즉시 of_node_put</text>
  <rect x="350" y="130" width="320" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="151" text-anchor="middle" font-size="12">_scoped 버전: __free(device_node)</text>
  <text x="510" y="169" text-anchor="middle" font-size="12">스코프 탈출 시 자동 put (실측 3 유지)</text>
  <text x="340" y="212" text-anchor="middle" font-size="12">같은 규칙: of_parse_phandle, of_get_child_by_name, of_find_node_by_* 의 반환값도 put 대상</text>
  <text x="340" y="232" text-anchor="middle" font-size="12">fwnode 쪽은 fwnode_handle_put, scoped 매크로는 device_for_each_child_node_scoped</text>
</svg>
```
그림 4 — 자식 순회 매크로의 참조 카운트 흐름. 루프를 끝까지 돌면 자동으로 균형이 맞지만, 중간 탈출은 마지막으로 얻은 노드의 참조를 남긴다.

### 5.4 이름으로 자식 하나 찾기

자식 이름이 고정된 바인딩(예: PMIC의 `regulators` 하위 노드)이라면 순회 대신:

```c
struct device_node *regs __free(device_node) = of_get_child_by_name(np, "regulators");
if (!regs)
	return dev_err_probe(dev, -ENODEV, "no regulators node\n");
```

fwnode 쪽은 `device_get_named_child_node(dev, "regulators")` + `fwnode_handle_put()` (또는 `__free(fwnode_handle)`).

---

## 6. phandle 따라가기 — of_parse_phandle_with_args

### 6.1 직관: "포인터 + 인자"

`d8,clk = <&d8_clkgen 3 7>`는 C로 치면 `{ &clkgen, 3, 7 }`이다. 첫 cell은 대상 노드의 phandle 번호, 그 뒤 cell 몇 개가 인자인지는 **대상 노드의 `#d8-cells`가 정한다.** 그래서 같은 속성에 서로 다른 provider를 섞어 `<&a 1 &b 2 3>`처럼 쓸 수 있다(a는 `#cells = <1>`, b는 `<2>`). `clocks`/`#clock-cells`, `gpios`/`#gpio-cells`, `interrupts-extended`/`#interrupt-cells`, `dmas`/`#dma-cells`, `pwms`/`#pwm-cells`가 모두 이 패턴이다 (D4). 서브시스템(`clk_get`, `gpiod_get` …)이 내부에서 이 함수를 부른다. 드라이버가 **자기만의** provider/consumer 관계를 만들 때 직접 쓴다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="300" height="110" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="20" y="30" font-size="13" font-weight="bold">/d8-sensor-a</text>
  <text x="20" y="52" font-size="12">d8,clk = &lt;&amp;d8_clkgen 3 7&gt;</text>
  <rect x="20" y="70" width="90" height="30" rx="3" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="65" y="90" text-anchor="middle" font-size="12">0x00000001</text>
  <rect x="115" y="70" width="90" height="30" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="160" y="90" text-anchor="middle" font-size="12">0x00000003</text>
  <rect x="210" y="70" width="90" height="30" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="255" y="90" text-anchor="middle" font-size="12">0x00000007</text>
  <rect x="400" y="10" width="270" height="110" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="410" y="30" font-size="13" font-weight="bold">/d8-clkgen</text>
  <text x="410" y="54" font-size="12">compatible = "d8,clkgen"</text>
  <text x="410" y="76" font-size="12">#d8-cells = &lt;2&gt;</text>
  <text x="410" y="98" font-size="12">phandle = &lt;1&gt; (dtc가 추가)</text>
  <path d="M65,100 C80,160 380,160 430,122" fill="none" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="313" y="64" font-size="12">② cells=2</text>
  <line x1="398" y1="80" x2="304" y2="85" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="140" y="163" font-size="12">① phandle 1 → /d8-clkgen 노드 찾기</text>
  <rect x="10" y="170" width="660" height="56" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="20" y="192" font-size="13">③ struct of_phandle_args { np = /d8-clkgen (+1 ref), args_count = 2, args = {3, 7} }</text>
  <text x="20" y="214" font-size="12">실측: with_args ret=0 np=/d8-clkgen args_count=2 args=3,7</text>
  <text x="10" y="252" font-size="12">④ index 1 → cell이 더 없음 → -ENOENT (실측). of_count_phandle_with_args() = 1</text>
  <text x="10" y="276" font-size="12">⑤ 다 쓰면 of_node_put(args.np) — fwnode 버전은 fwnode_handle_put(fargs.fwnode)</text>
</svg>
```
그림 5 — `of_parse_phandle_with_args(np, "d8,clk", "#d8-cells", 0, &args)`의 해석 과정. 인자 개수는 소비자(sensor)가 아니라 공급자(clkgen) 노드가 정한다.

### 6.2 예제 6 — phandle, phandle+args, fwnode 버전, 그리고 gpiod optional

무엇을 확인하나: 그림 5의 각 단계 값, 범위 밖 index의 에러, 같은 정보를 `fwnode_property_get_reference_args()`로 얻기, 그리고 `reset-gpios`가 없을 때 `devm_gpiod_get_optional()`의 동작.

```c
// d8_phandle: follow d8,clk = <&d8_clkgen 3 7> with #d8-cells = <2>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/gpio/consumer.h>

static int d8_ph_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node, *tgt;
	struct of_phandle_args args;
	struct fwnode_reference_args fargs;
	struct gpio_desc *rst;
	int ret;

	tgt = of_parse_phandle(np, "d8,clk", 0);	/* phandle only */
	dev_info(dev, "of_parse_phandle -> %pOF\n", tgt);
	of_node_put(tgt);

	ret = of_parse_phandle_with_args(np, "d8,clk", "#d8-cells", 0, &args);
	if (ret)
		return dev_err_probe(dev, ret, "d8,clk\n");
	dev_info(dev, "with_args ret=%d np=%pOF args_count=%d args=%u,%u\n",
		 ret, args.np, args.args_count, args.args[0], args.args[1]);
	of_node_put(args.np);

	ret = of_parse_phandle_with_args(np, "d8,clk", "#d8-cells", 1, &args);
	dev_info(dev, "index 1 -> %d %pe\n", ret, ERR_PTR(ret));
	ret = of_count_phandle_with_args(np, "d8,clk", "#d8-cells");
	dev_info(dev, "of_count_phandle_with_args=%d\n", ret);

	ret = fwnode_property_get_reference_args(dev_fwnode(dev), "d8,clk",
						 "#d8-cells", 0, 0, &fargs);
	if (!ret) {
		dev_info(dev, "fwnode ref: %pfwP nargs=%u args=%llu,%llu\n",
			 fargs.fwnode, fargs.nargs, fargs.args[0], fargs.args[1]);
		fwnode_handle_put(fargs.fwnode);
	}
	rst = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW); /* reset-gpios */
	if (IS_ERR(rst))
		return PTR_ERR(rst);
	dev_info(dev, "reset gpio: %s\n", rst ? "present" : "absent -> NULL, skip reset");
	return 0;
}
/* of_device_id ("d8,sensor-v2") + platform_driver (.name = "d8_phandle") — 예제 3과 동일 */
```

```text
[   14.656055] d8_phandle d8-sensor-a: of_parse_phandle -> /d8-clkgen
[   14.656194] d8_phandle d8-sensor-a: with_args ret=0 np=/d8-clkgen args_count=2 args=3,7
[   14.656251] d8_phandle d8-sensor-a: index 1 -> -2 -ENOENT
[   14.656320] d8_phandle d8-sensor-a: of_count_phandle_with_args=1
[   14.667781] d8_phandle d8-sensor-a: fwnode ref: d8-clkgen nargs=2 args=3,7
[   14.668249] d8_phandle d8-sensor-a: reset gpio: absent -> NULL, skip reset
```

출력에서 볼 것:

- `of_parse_phandle()`은 인자를 무시하고 노드만 준다(`cells_name = NULL`, 인자 0개로 해석). 인자가 있는 리스트에서 index 1 이상을 쓰면 엉뚱한 cell을 phandle로 해석하므로, **인자가 있는 속성엔 항상 `_with_args`**.
- 범위 밖 index는 `-ENOENT`. "몇 개 있나"는 `of_count_phandle_with_args()`.
- fwnode 버전은 `nargs` 인자로 `#cells` 이름 대신 고정 개수를 줄 수도 있다(이름을 주면 그쪽이 우선). `%pfwP`는 fwnode 이름 출력 포맷이다.
- `devm_gpiod_get_optional()`은 속성이 없으면 **에러가 아니라 NULL**을 준다. gpiod 함수들은 NULL 디스크립터를 받으면 아무것도 안 하므로 "리셋 핀이 있는 보드/없는 보드"를 `if` 없이 처리할 수 있다. 진짜 에러(예: `-EPROBE_DEFER`)만 `IS_ERR`로 걸러 반환한다.

### 6.3 of_get_named_gpio는 쓰지 않는다

옛 드라이버는 `gpio = of_get_named_gpio(np, "reset-gpios", 0); gpio_request(gpio, ...); gpio_set_value(gpio, 1);`처럼 **전역 GPIO 번호**를 썼다. 6.18의 `drivers/gpio/gpiolib-of.c` 주석은 명시적으로 `**DEPRECATED** This function is deprecated and must not be used in new code.`라고 적고 있다. 이유:

- 전역 번호는 GPIO 컨트롤러 probe 순서에 따라 바뀐다 (D4, H 모듈).
- DT의 `GPIO_ACTIVE_LOW` 플래그를 무시한다. gpiod는 "논리값 1 = assert"로 추상화해서 극성을 DT가 결정한다.
- DT 전용이다. `devm_gpiod_get(dev, "reset", ...)`은 fwnode를 통해 ACPI `_DSD`와 swnode에서도 동작한다.

새 코드는 `devm_gpiod_get()` / `devm_gpiod_get_optional()` / `devm_gpiod_get_array()`. 속성 이름은 `<con_id>-gpios`(`reset-gpios`)이고 함수에는 `"reset"`만 넘긴다.

---

## 7. 통합 device property API (fwnode) — 새 드라이버의 기본값

### 7.1 왜 또 다른 API가 있나

같은 IMU 칩이 ARM 보드(DT)에도, x86 산업용 PC(ACPI)에도 붙는다. OF API로 쓴 드라이버는 `dev->of_node`가 없는 ACPI 시스템에서 모든 읽기가 `-EINVAL`이 된다. 그래서 커널은 **firmware node(fwnode)** 라는 추상 계층을 만들었다. `struct fwnode_handle`은 `ops`(함수 테이블) 포인터를 가진 작은 구조체이고, `device_node` 안에 하나(`np->fwnode`), ACPI 디바이스 안에 하나, software node 안에 하나 들어 있다. 드라이버는 `device_property_read_u32(dev, ...)`만 부르고, 실제 데이터 소스는 ops가 결정한다. 펌웨어로 치면 "설정값을 EEPROM에서 읽든 OTP에서 읽든 `cfg_get_u32(key)` 하나로 읽게 만든 HAL"이다.

```c
/* include/linux/fwnode.h (6.18.55) — 앞부분 */
struct fwnode_handle {
	struct fwnode_handle *secondary;
	const struct fwnode_operations *ops;
	/* The below is used solely by device links, don't use otherwise */
	struct device *dev;
	...
};
```

호출 경로 (DT 위에서):

```
device_property_read_u32(dev, "sample-rate", &v)
  -> device_property_read_u32_array(dev, name, &v, 1)
    -> fwnode_property_read_u32_array(dev_fwnode(dev), ...)
      -> fwnode_property_read_int_array()
           ret = fwnode->ops->property_read_int_array(...)   /* of_fwnode_ops */
             -> of_fwnode_property_read_int_array()
               -> of_property_read_u32_array(to_of_node(fwnode), ...)  /* same code as OF API */
           if (ret == -EINVAL) try fwnode->secondary (e.g. a software node)
```

`dev_fwnode(dev)`는 `dev->of_node`가 있으면 `&dev->of_node->fwnode`, 없으면 `dev->fwnode`를 준다. 마지막 줄의 **secondary** fallback이 실무에서 중요하다: DT에 없는 속성을 보드 코드나 다른 드라이버가 software node로 "덧붙일" 수 있다(primary가 `-EINVAL`일 때만 secondary를 본다).

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="250" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="135" y="30" text-anchor="middle" font-size="13">of_property_read_u32(np)</text>
  <text x="135" y="47" text-anchor="middle" font-size="12">DT 전용 드라이버</text>
  <rect x="330" y="10" width="340" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="500" y="30" text-anchor="middle" font-size="13">device_property_read_u32(dev)</text>
  <text x="500" y="47" text-anchor="middle" font-size="12">펌웨어 종류를 모르는 드라이버 (권장)</text>
  <line x1="500" y1="54" x2="500" y2="78" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="330" y="80" width="340" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="500" y="105" text-anchor="middle" font-size="13">dev_fwnode(dev)→ops (fwnode_operations)</text>
  <line x1="420" y1="120" x2="160" y2="168" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="500" y1="120" x2="400" y2="168" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="580" y1="120" x2="590" y2="168" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="60" y="170" width="200" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="191" text-anchor="middle" font-size="13">of_fwnode_ops</text>
  <text x="160" y="209" text-anchor="middle" font-size="12">Device Tree</text>
  <rect x="300" y="170" width="200" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="400" y="191" text-anchor="middle" font-size="13">acpi_device_fwnode_ops</text>
  <text x="400" y="209" text-anchor="middle" font-size="12">ACPI _DSD (x86 · 서버)</text>
  <rect x="520" y="170" width="150" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="595" y="191" text-anchor="middle" font-size="13">software_node_ops</text>
  <text x="595" y="209" text-anchor="middle" font-size="12">C 코드의 속성 표</text>
  <line x1="35" y1="54" x2="35" y2="258" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="160" y1="220" x2="160" y2="258" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="10" y="260" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="285" text-anchor="middle" font-size="12">drivers/of/property.c — 같은 구현에 도착</text>
  <text x="340" y="326" text-anchor="middle" font-size="12">OF API를 쓸 이유: DT 전용 기능(of_parse_phandle_with_args_map, of_graph, overlay 노드 조작)</text>
  <text x="340" y="346" text-anchor="middle" font-size="12">또는 DT만 있는 서브시스템 코어(of_clk, pinctrl DT 파서). 일반 드라이버의 설정값은 device_property_*</text>
</svg>
```
그림 6 — 두 API 계열의 관계. DT 위에서는 결국 같은 함수에 도착하므로 성능·결과 차이는 없다. 차이는 "ACPI와 software node에서도 동작하느냐"다.

### 7.2 대응표

| OF API | device property API (fwnode) | 비고 |
|---|---|---|
| `of_property_read_u32(np, n, &v)` | `device_property_read_u32(dev, n, &v)` | 노드 기준이면 `fwnode_property_read_u32(fw, ...)` |
| `of_property_read_u32_array` | `device_property_read_u32_array` | `val = NULL`이면 개수 반환 |
| `of_property_count_u32_elems` | `device_property_count_u32` | |
| `of_property_read_u64` | `device_property_read_u64` | |
| `of_property_read_string` | `device_property_read_string` | |
| `of_property_read_string_index` | `device_property_read_string_array` | 배열로 한 번에 |
| `of_property_match_string` | `device_property_match_string` | |
| `of_property_read_bool` | `device_property_read_bool` | |
| `of_property_present` | `device_property_present` | |
| `of_get_child_count` / available | `device_get_child_node_count` | **available만 센다** (아래) |
| `for_each_available_child_of_node(_scoped)` | `device_for_each_child_node(_scoped)` | 자식은 `struct fwnode_handle *` |
| `of_get_child_by_name` | `device_get_named_child_node` | |
| `of_parse_phandle_with_args` | `fwnode_property_get_reference_args` | |
| `of_node_put` | `fwnode_handle_put` | |
| `of_device_get_match_data` | `device_get_match_data` | ACPI match table도 봄 |
| `%pOF`, `%pOFP` | `%pfw`, `%pfwP` | printk 포맷 |

주의: DT 백엔드의 `get_next_child_node`는 `of_get_next_available_child()`를 부른다 (`drivers/of/property.c`의 `of_fwnode_get_next_child_node`). 그래서 fwnode 쪽 자식 순회와 개수는 **disabled 노드를 자동으로 건너뛴다.** 예제 7에서 `device_get_child_node_count=2`로 확인된다.

### 7.3 per-variant match data — device_get_match_data

같은 드라이버가 `sensor-v1`(최대 400 Hz, 채널 없음)과 `sensor-v2`(4 kHz, 채널 있음)를 지원한다고 하자. `compatible` 문자열마다 다른 상수 구조체를 `.data`에 매달고, probe에서 `device_get_match_data(dev)`로 꺼낸다. `if (of_device_is_compatible(np, "...v2"))` 같은 문자열 비교를 probe 곳곳에 뿌리는 것보다 깔끔하고, ACPI match table의 `driver_data`도 같은 함수로 받는다. 랩 소스의 `drivers/iio/accel/adxl345_core.c`도 `st->info = device_get_match_data(dev);`로 칩 변형 정보를 얻는다.

### 7.4 예제 7 — device_property API로 같은 노드 읽기 + match data + DT 없는 보드

무엇을 확인하나: (1) 예제 3과 같은 값이 `device_property_*`로도 나오는지, (2) v1/v2 노드가 서로 다른 match data를 받는지, (3) **DT가 전혀 없는 디바이스**(software node)에서도 같은 드라이버가 수정 없이 도는지.

```c
// d8_fw: same reads via the unified device property API + per-variant match data
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/mod_devicetable.h>

struct d8_variant { const char *name; u32 max_rate; bool has_channels; };
static const struct d8_variant d8_v1 = { "v1", 400, false };
static const struct d8_variant d8_v2 = { "v2", 4000, true };

static int d8_fw_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct d8_variant *var = device_get_match_data(dev);
	const char *label = "?", *modes[4];
	u32 rate = 0, gains[4] = { 0 }, tmo = 50, reg;	/* tmo default 50 */
	int n;

	if (!var && platform_get_device_id(pdev))	/* non-DT: id_table */
		var = (const void *)platform_get_device_id(pdev)->driver_data;
	if (!var)
		return -ENODEV;
	device_property_read_u32(dev, "sample-rate", &rate);
	device_property_read_string(dev, "label", &label);
	device_property_read_u32(dev, "d8,timeout-ms", &tmo);	/* optional */
	dev_info(dev, "[%s] fwnode=%pfwP label=%s rate=%u(max %u) tmo=%u invert=%d\n",
		 var->name, dev_fwnode(dev), label, rate, var->max_rate, tmo,
		 device_property_read_bool(dev, "d8,invert-x"));
	if (rate > var->max_rate)
		return dev_err_probe(dev, -EINVAL, "rate too high\n");
	if (!var->has_channels)
		return 0;
	n = device_property_count_u32(dev, "gains");
	device_property_read_u32_array(dev, "gains", gains, 4);
	dev_info(dev, "  gains n=%d %u %u %u %u\n", n, gains[0], gains[1], gains[2], gains[3]);
	n = device_property_read_string_array(dev, "modes", modes, 4);
	dev_info(dev, "  modes n=%d first=%s last=%s\n", n, modes[0], modes[n - 1]);
	dev_info(dev, "  device_get_child_node_count=%u\n", device_get_child_node_count(dev));
	device_for_each_child_node_scoped(dev, child) {
		fwnode_property_read_u32(child, "reg", &reg);
		fwnode_property_read_string(child, "label", &label);
		dev_info(dev, "  child %pfwP reg=%u label=%s\n", child, reg, label);
	}
	return 0;
}
static const struct of_device_id d8_fw_ids[] = {
	{ .compatible = "d8,sensor-v1", .data = &d8_v1 },
	{ .compatible = "d8,sensor-v2", .data = &d8_v2 }, { }
};
MODULE_DEVICE_TABLE(of, d8_fw_ids);
static const struct platform_device_id d8_fw_pids[] = {
	{ "d8-sensor-v1", (kernel_ulong_t)&d8_v1 }, { }
};
static struct platform_driver d8_fw_drv = {
	.probe = d8_fw_probe, .id_table = d8_fw_pids,
	.driver = { .name = "d8_fw", .of_match_table = d8_fw_ids },
};
module_platform_driver(d8_fw_drv);
MODULE_DESCRIPTION("D8 DT-reading lab"); MODULE_LICENSE("GPL");
```

DT 없는 "보드"를 흉내 내는 모듈. ACPI가 없는 QEMU `virt`에서 fwnode의 다른 백엔드를 보여 주는 가장 쉬운 방법이 software node다. 실제로 x86 보드 파일, MFD 드라이버가 자식 디바이스를 만들 때, 그리고 DT/ACPI 테이블이 틀린 기기를 커널이 quirk로 보정할 때 이렇게 쓴다.

```c
// d8_swnode: a board without DT - describe the same sensor with a software node
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>

static const struct property_entry d8_props[] = {
	PROPERTY_ENTRY_U32("sample-rate", 100),
	PROPERTY_ENTRY_STRING("label", "imu-swnode"),
	PROPERTY_ENTRY_BOOL("d8,invert-x"),
	{ }
};
static struct platform_device *d8_pd;

static int __init d8_sw_init(void)
{
	struct platform_device_info info = {
		.name = "d8-sensor-v1", .id = PLATFORM_DEVID_NONE,
		.properties = d8_props,	/* becomes a swnode (fwnode) */
	};

	d8_pd = platform_device_register_full(&info);
	return PTR_ERR_OR_ZERO(d8_pd);
}
static void __exit d8_sw_exit(void)
{
	platform_device_unregister(d8_pd);
}
module_init(d8_sw_init);
module_exit(d8_sw_exit);
MODULE_DESCRIPTION("D8 DT-reading lab"); MODULE_LICENSE("GPL");
```

게스트 스크립트:

```sh
insmod /d8_fw.ko
insmod /d8_swnode.ko
dmesg | grep -E "d8_fw|d8-sensor"
ls /sys/bus/platform/drivers/d8_fw | tr "\n" " "; echo
```

```text
lab initramfs: 6.18.55 up 2.95s
[    3.205551] d8_fw: loading out-of-tree module taints kernel.
[    3.212981] d8_fw d8-sensor-a: [v2] fwnode=d8-sensor-a label=imu-left rate=1000(max 4000) tmo=50 invert=1
[    3.213151] d8_fw d8-sensor-a:   gains n=4 1 2 4 8
[    3.213462] d8_fw d8-sensor-a:   modes n=3 first=low-power last=burst
[    3.213520] d8_fw d8-sensor-a:   device_get_child_node_count=2
[    3.213562] d8_fw d8-sensor-a:   child channel@0 reg=0 label=accel-x
[    3.213597] d8_fw d8-sensor-a:   child channel@2 reg=2 label=accel-z
[    3.214078] d8_fw d8-sensor-b: [v1] fwnode=d8-sensor-b label=imu-right rate=200(max 400) tmo=50 invert=0
[    3.243920] d8_fw d8-sensor-v1: [v1] fwnode=node0 label=imu-swnode rate=100(max 400) tmo=50 invert=1
bind d8-sensor-a d8-sensor-b d8-sensor-v1 module uevent unbind 
```

출력에서 볼 것:

- **OF API(예제 3)와 device_property API의 값이 전부 같다**: rate 1000, gains 1 2 4 8, label imu-left, modes 3개, invert-x 1, 기본값 50.
- `d8-sensor-a`는 `[v2]`, `d8-sensor-b`는 `[v1]` — `.data`가 compatible별로 전달됐다. v1은 `has_channels = false`라 채널을 읽지 않았다.
- 자식 개수 2, 순회도 channel@0과 @2만 — fwnode 순회는 disabled를 건너뛴다 (OF의 `of_get_child_count`는 3이었다).
- 세 번째 줄 `d8-sensor-v1 ... fwnode=node0`: **DT 노드가 전혀 없는** 디바이스인데 같은 probe 코드가 software node에서 속성을 읽었다. 이 디바이스는 `of_match_table`이 아니라 `id_table`의 이름으로 매칭됐고, `device_get_match_data()`가 NULL을 줘서 `platform_get_device_id()` fallback이 v1 데이터를 골랐다. OF API로 쓴 드라이버였다면 `dev->of_node == NULL`이라 모든 읽기가 `-EINVAL`이었을 것이다.

---

## 8. 유저스페이스에서 같은 노드 보기

드라이버 디버깅의 첫 질문은 "**커널이 받은 DT에 내가 생각한 값이 정말 들어 있나?**"다. 부트로더가 DTB를 고쳐 넣거나(U-Boot `fdt set`, C 모듈), 다른 DTB가 로드되는 일이 흔하다. `/proc/device-tree`(= `/sys/firmware/devicetree/base`로의 심볼릭 링크)는 unflatten된 트리를 **속성 하나 = 파일 하나, 원본 바이트 그대로** 보여 준다. 이 파일이 바로 `struct property`의 `attr`(bin_attribute) 필드다.

### 8.1 예제 8 — 게스트에서 hexdump

무엇을 확인하나: 같은 속성의 raw 바이트 (빅엔디안, NUL 구분 문자열, 길이 0 bool).

```sh
cd /proc/device-tree/d8-sensor-a
ls | tr "\n" " "; echo
echo "--- sample-rate"; hexdump -C sample-rate
echo "--- gains"; hexdump -C gains
echo "--- modes"; hexdump -C modes
echo "--- timestamp-base"; hexdump -C timestamp-base
echo "--- d8,invert-x (bool)"; wc -c < d8,invert-x
echo "--- d8,clk"; hexdump -C d8,clk
echo "--- clkgen phandle"; hexdump -C ../d8-clkgen/phandle
ls channel@1 | tr "\n" " "; echo; cat channel@1/status; echo
```

```text
lab initramfs: 6.18.55 up 2.91s
#address-cells #size-cells channel@0 channel@1 channel@2 compatible d8,clk d8,empty-name d8,invert-x d8,legacy-flag gains label modes name sample-rate timestamp-base 
--- sample-rate
00000000  00 00 03 e8                                       |....|
00000004
--- gains
00000000  00 00 00 01 00 00 00 02  00 00 00 04 00 00 00 08  |................|
00000010
--- modes
00000000  6c 6f 77 2d 70 6f 77 65  72 00 6e 6f 72 6d 61 6c  |low-power.normal|
00000010  00 62 75 72 73 74 00                              |.burst.|
00000017
--- timestamp-base
00000000  12 34 56 78 9a bc de f0                           |.4Vx....|
00000008
--- d8,invert-x (bool)
0
--- d8,clk
00000000  00 00 00 01 00 00 00 03  00 00 00 07              |............|
0000000c
--- clkgen phandle
00000000  00 00 00 01                                       |....|
00000004
label name reg status 
disabled 
```

출력에서 볼 것: 예제 3의 `len=` 값과 파일 크기가 정확히 같다(gains 0x10 = 16, modes 0x17 = 23). bool은 0바이트 파일이다. `d8,clk`의 첫 cell 1은 `d8-clkgen/phandle` 파일의 값 1과 같다 — 유저스페이스에서도 phandle을 손으로 따라갈 수 있다. disabled 채널도 디렉터리로 존재한다.

### 8.2 예제 9 — C로 읽기: be32 변환을 유저스페이스에서

무엇을 확인하나: 커널의 `be32_to_cpup()`이 하는 일을 유저 공간 `ntohl()`로 똑같이 해서 같은 값을 얻는다. 게스트엔 컴파일러가 없으므로 VM에서 static으로 빌드해 initramfs에 넣었다 (`gcc -std=gnu11 -Wall -Wextra -O2 -static`, 경고 0개).

```c
// d8_dtread: userspace view of the same node - raw big-endian cells from /proc/device-tree
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>		/* ntohl = be32_to_cpu */

#define NODE "/proc/device-tree/d8-sensor-a/"

static ssize_t slurp(const char *prop, void *buf, size_t sz)
{
	char path[256];
	snprintf(path, sizeof(path), NODE "%s", prop);
	int fd = open(path, O_RDONLY);
	if (fd < 0) { perror(path); return -1; }
	ssize_t n = read(fd, buf, sz);
	if (n < 0) { perror("read"); exit(1); }
	close(fd);
	return n;
}

int main(void)
{
	uint32_t cells[8];
	char str[64];
	ssize_t n = slurp("gains", cells, sizeof(cells));
	printf("gains: %zd bytes ->", n);
	for (int i = 0; i < n / 4; i++)
		printf(" raw=0x%08x val=%u;", cells[i], ntohl(cells[i]));
	printf("\n");

	n = slurp("timestamp-base", cells, sizeof(cells));
	uint64_t ts = ((uint64_t)ntohl(cells[0]) << 32) | ntohl(cells[1]);
	printf("timestamp-base: %zd bytes -> 0x%llx\n", n, (unsigned long long)ts);

	n = slurp("modes", str, sizeof(str));
	for (char *p = str; p < str + n; p += strlen(p) + 1)
		printf("mode: \"%s\"\n", p);

	n = slurp("d8,invert-x", str, sizeof(str));
	printf("d8,invert-x: %zd bytes (bool = exists)\n", n);
	n = slurp("d8,missing", str, sizeof(str));
	printf("d8,missing: %zd\n", n);
	return 0;
}
```

게스트에서 `/d8_dtread` 실행 후 sysfs도 같이 본다 (`ls -l of_node`, `cat uevent`):

```text
lab initramfs: 6.18.55 up 5.91s
gains: 16 bytes -> raw=0x01000000 val=1; raw=0x02000000 val=2; raw=0x04000000 val=4; raw=0x08000000 val=8;
timestamp-base: 8 bytes -> 0x123456789abcdef0
mode: "low-power"
mode: "normal"
mode: "burst"
d8,invert-x: 0 bytes (bool = exists)
/proc/device-tree/d8-sensor-a/d8,missing: No such file or directory
d8,missing: -1
--- sysfs
of_node -> ../../../firmware/devicetree/base/d8-sensor-a
OF_NAME=d8-sensor-a
OF_FULLNAME=/d8-sensor-a
OF_COMPATIBLE_0=d8,sensor-v2
OF_COMPATIBLE_N=1
MODALIAS=of:Nd8-sensor-aT(null)Cd8,sensor-v2
```

출력에서 볼 것:

- `raw=0x01000000`: 리틀엔디안 CPU가 빅엔디안 cell을 그대로 읽은 값. `ntohl` 후 1. 커널 드라이버에서 `of_find_property()`의 raw 포인터를 변환 없이 쓰면 정확히 이 버그가 난다.
- 없는 속성 = 없는 파일(`ENOENT`) ↔ 커널의 `-EINVAL`. 빈 bool = 0바이트 파일.
- `/sys/bus/platform/devices/d8-sensor-a/of_node`가 DT 노드 디렉터리를 가리킨다. 디바이스에서 DT 노드로 가는 가장 빠른 길이다. `uevent`의 `MODALIAS=of:N...T...C<compatible>`가 udev/modprobe의 자동 모듈 로드 키다 (D3, `MODULE_DEVICE_TABLE(of, ...)`가 이걸 만든다).

### 8.3 예제 10 — 호스트 쪽 fdtget (부팅 전에 DTB 검사)

무엇을 확인하나: 보드에 올리기 전, 빌드한 DTB에서 속성을 타입 지정해 바로 뽑기. CI에서 "이 DTB에 sample-rate가 들어 있나"를 검사할 때 쓴다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/D8; fdtget -t x d8.dtb /d8-sensor-a gains; fdtget -t s d8.dtb /d8-sensor-a modes; fdtget -t x d8.dtb /d8-sensor-a d8,clk; fdtget d8.dtb /d8-clkgen phandle'
```

```text
1 2 4 8
low-power normal burst
1 3 7
1
```

출력에서 볼 것: `fdtget`도 타입을 모른다. `-t x`(hex u32), `-t s`(문자열)를 **사람이 정해 줘야** 한다 — 커널 API와 정확히 같은 상황이다.

---

## 9. 커널 안에서는 무슨 일이 일어나나

부팅부터 `probe()` 안의 읽기까지를 함수 이름으로 따라가면:

```
start_kernel()
  setup_arch()                              arch/arm64/kernel/setup.c
    setup_machine_fdt(__fdt_pointer)        x0 from bootloader -> early scan (memory, chosen)
    unflatten_device_tree()                 drivers/of/fdt.c
      __unflatten_device_tree()             2 passes: size, then allocate + fill
        populate_node() / populate_properties()
          pp->value = pointer into the blob ; adds "name" if missing
      of_alias_scan()                       /aliases -> of_alias_get_id()
...
of_platform_default_populate_init()         drivers/of/platform.c (arch_initcall_sync)
  of_platform_populate(NULL, of_default_bus_match_table, ...)
    of_platform_device_create_pdata()       for root children with "compatible" (+ simple-bus)
      dev->of_node = np ; dev->fwnode = &np->fwnode
...
driver_register() -> bus match (of_driver_match_device) -> really_probe() -> drv->probe(pdev)
  of_property_read_u32(dev->of_node, ...)   drivers/of/property.c
    of_find_property() under devtree_lock -> be32_to_cpup()
  device_property_read_u32(dev, ...)        drivers/base/property.c
    dev_fwnode(dev)->ops->property_read_int_array -> same OF code
```

드라이버를 쓸 때 다시 만나는 지점:

- **I2C/SPI 클라이언트 드라이버(H 모듈)**: 버스 컨트롤러 드라이버가 자식 노드를 순회해 `i2c_client`/`spi_device`를 만든다(`of_i2c_register_devices` 등). 그 클라이언트의 `probe()`에서 이 노트의 API로 설정을 읽는다. 버스 디바이스는 `i2c_get_match_data()`처럼 버스 전용 match-data 헬퍼도 있다.
- **자원은 직접 읽지 않는다(D4, F 모듈)**: `reg` → `platform_get_resource()`/`devm_platform_ioremap_resource()`, `interrupts` → `platform_get_irq()`, `clocks` → `devm_clk_get()`, `*-supply` → `devm_regulator_get()`, `*-gpios` → `devm_gpiod_get()`. 이 함수들 안에서 §6의 phandle 파싱이 일어난다.
- **sysfs 노출(F 모듈)**: 읽은 설정을 `struct my_priv`에 저장하고 필요하면 sysfs 속성으로 보여 준다. DT는 다시 읽지 않는다.
- **overlay(D6)**: 런타임에 노드가 붙고 떨어지므로 refcount(§5)와 문자열 포인터 수명(§3.4)이 실제 문제가 된다.

---

## 10. Embedded 실무에서 다시 보기

### 10.1 센서 키트 IMU — 바인딩을 설계하고 probe에서 읽기

IMU를 두 개(좌·우) 달고, 장착 방향 행렬과 FIFO 워터마크를 보드마다 다르게 해야 한다. 바인딩 설계 원칙: **하드웨어 사실만 DT에** 넣는다(장착 방향, 배선, 클럭 주파수). 런타임 정책(필터 컷오프, 로그 레벨)은 sysfs/ioctl로. 표준 속성이 있으면 그걸 쓴다(IIO의 `mount-matrix`는 `iio_read_mount_matrix(dev, &matrix)`가 fwnode로 읽어 준다). 드라이버 쪽은 이 노트의 패턴 그대로:

```c
priv->odr_hz = 1000;					/* optional, default */
device_property_read_u32(dev, "vendor,odr-hz", &priv->odr_hz);
if (priv->odr_hz > var->max_odr)
	return dev_err_probe(dev, -EINVAL, "odr %u > %u\n", priv->odr_hz, var->max_odr);
priv->invert_x = device_property_read_bool(dev, "vendor,invert-x");
```

### 10.2 다채널 ADC / LED 컨트롤러 — 자식 노드 + reg 범위 검사

```c
device_for_each_child_node_scoped(dev, child) {
	u32 ch;
	if (fwnode_property_read_u32(child, "reg", &ch) || ch >= NUM_CH)
		return dev_err_probe(dev, -EINVAL, "%pfwP: bad reg\n", child);
	fwnode_property_read_u32(child, "vendor,gain", &priv->gain[ch]);
	priv->enabled_mask |= BIT(ch);
}
```

`_scoped` 덕분에 루프 안의 `return`이 참조를 새지 않는다. 예전 코드(`device_for_each_child_node`)였다면 `return` 전에 `fwnode_handle_put(child)`가 필요했고, 실제로 이런 누락을 고치는 패치가 커널에 수백 개 들어갔다.

### 10.3 "x86 산업용 PC에도 같은 센서를 달자" — fwnode의 값

Scout AI 같은 회사가 ARM SoC 기반 로봇과 x86 기반 지상국에 같은 CAN·IMU 칩을 쓴다면, `device_property_*`로 쓴 드라이버는 ACPI `_DSD`에 같은 이름의 속성(`"sample-rate"`)만 정의하면 그대로 동작한다. OF API로 썼다면 ACPI용 분기를 따로 짜야 한다. upstream 리뷰어(특히 IIO, input, LED 서브시스템)가 새 드라이버에 `of_property_read_*` 대신 `device_property_*`를 요구하는 이유다.

### 10.4 "드라이버가 값을 이상하게 읽어요" 디버깅 순서

1. `hexdump -C /proc/device-tree/<path>/<prop>` — 커널이 받은 DT에 값이 있나, 길이가 맞나 (§8).
2. `ls -l /sys/bus/platform/devices/<dev>/of_node` — 디바이스가 내가 생각한 노드에 붙었나.
3. 속성 이름 오타(`sample_rate` vs `sample-rate`) — 커널은 `-EINVAL`만 주고 조용하다. 반환값을 로그로 찍는다.
4. 타입 불일치(문자열을 u32로) — 에러 없이 이상한 값 (§4). `make dtbs_check`(D7)로 잡는다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 반환값 무시 + 출력 변수 미초기화 | 보드마다 다른 이상한 값, 가끔만 재현 | 실패 시 출력 변수가 안 바뀌어 스택 쓰레기 사용 | 기본값으로 초기화하거나 반환값 검사 |
| `of_find_property()` raw 값을 그대로 `u32`로 사용 | 1000이 3892510720으로 읽힘 | DT cell은 빅엔디안 | `of_property_read_u32()` 사용, raw면 `be32_to_cpup()` |
| 값 있는 속성을 `of_property_read_bool()`로 검사 | dmesg에 `Read of boolean property ... with a value.` | 6.x부터 경고 | `of_property_present()` / `device_property_present()` |
| `for_each_child_of_node`에서 `break`/`return` 후 put 안 함 | overlay 제거 시 노드 해제 안 됨, kmemleak 보고 | 현재 child 참조 +1 잔존 | `of_node_put(child)` 또는 `_scoped` 매크로 |
| `of_parse_phandle()`을 인자 있는 리스트에 index ≥ 1로 사용 | 엉뚱한 노드 또는 NULL | 인자 cell을 phandle로 해석 | `of_parse_phandle_with_args()` + `#xxx-cells` |
| 빈 속성 정수 읽기에서 `-ENODATA` 분기 | 그 분기가 안 탐 | FDT에선 `-EOVERFLOW`가 나옴 | `-EINVAL`만 "없음"으로, 나머지는 에러 |
| 채널 순회에 `of_get_child_count()`로 배열 할당 후 available만 채움 | disabled 채널 자리가 0으로 남아 오동작 | 전체 vs available 개수 차이 | available 계열 또는 fwnode 순회 일관되게 |
| `of_get_named_gpio()` + `gpio_set_value()` | active-low 보드에서 리셋 극성 반대 | DT 극성 플래그 무시, 전역 번호 | `devm_gpiod_get(_optional)()` |
| OF API로만 쓴 드라이버를 ACPI 보드에 사용 | probe에서 모든 속성 `-EINVAL` | `dev->of_node == NULL` | `device_property_*`로 전환 |
| hot path(IRQ 등)에서 매번 DT 읽기 | 지연 증가, `devtree_lock` 경합 | 선형 탐색 + 스핀락 | probe에서 한 번 읽어 priv에 저장 |

---

## 12. 면접에서 이렇게 말한다

**Q.** How does a driver read a configuration value like a sample rate from the device tree?

**A.** probe에서 `device_property_read_u32(dev, "sample-rate", &val)`(또는 DT 전용이면 `of_property_read_u32(dev->of_node, ...)`)를 부른다. 반환값 0이면 성공, `-EINVAL`이면 속성 없음, `-EOVERFLOW`면 길이 불일치다. 값은 DT에서 빅엔디안 cell이고 API가 CPU 엔디안으로 바꿔 준다. 읽은 값은 범위 검사 후 private 구조체에 저장하고, 이후엔 DT를 다시 읽지 않는다.

> In probe I call device_property_read_u32(dev, "sample-rate", &val) — or of_property_read_u32 on dev->of_node if the driver is DT-only. Zero means success, -EINVAL means the property is absent, -EOVERFLOW means the length doesn't match. DT cells are big-endian and the helper converts them. I range-check the value, store it in the driver's private struct, and never touch the DT again in the hot path.

**꼬리 질문**: "속성이 없으면?" → optional이면 미리 기본값을 넣어 두고 반환값을 무시(실패 시 출력은 안 바뀐다), required면 `dev_err_probe()`로 probe 실패.

**Q.** What is the difference between the `of_*` API and the `device_property_*` API? Which one would you use for a new driver?

**A.** `of_*`는 `struct device_node`를 직접 받는 DT 전용 API이고, `device_property_*`는 `struct fwnode_handle` 추상화 위의 통합 API라서 DT, ACPI `_DSD`, software node 모두에서 동작한다. DT 위에서는 내부적으로 같은 OF 코드를 부르므로 결과는 같다. 새 드라이버는 `device_property_*`를 쓰고, `of_graph`나 phandle-args map처럼 DT 전용 기능이 필요할 때만 `of_*`를 쓴다.

> The `of_*` helpers take a struct device_node and only work with device tree. The `device_property_*` helpers sit on top of the fwnode abstraction, so the same driver works with DT, ACPI _DSD properties, and software nodes; on a DT system they end up in the same OF code. For a new driver I'd use `device_property_*` and device_get_match_data, and only drop to `of_*` for DT-specific things like of_graph.

**Q.** When do you need of_node_put()?

**A.** 노드 포인터를 돌려주는 OF 함수는 참조를 올려서 준다 — `of_parse_phandle`, `of_get_child_by_name`, `of_find_node_by_*`, phandle args의 `np`. 다 쓰면 put 해야 한다. `for_each_child_of_node`는 반복마다 다음을 get, 이전을 put 하므로 끝까지 돌면 균형이 맞지만 `break`나 `return`으로 나가면 현재 child를 put 해야 한다. 최신 커널은 `__free(device_node)`를 쓰는 `_scoped` 매크로로 이걸 자동화한다.

> Any OF function that returns a node pointer returns it with an elevated refcount — of_parse_phandle, of_get_child_by_name, `of_find_node_by_*`, and the np in of_phandle_args — so I put it when done. for_each_child_of_node gets the next and puts the previous on each iteration, so a full loop is balanced, but breaking out leaves the current child held. Recent kernels have for_each_child_of_node_scoped, which uses the cleanup attribute to put it automatically.

**꼬리 질문**: "정적 DT인데 refcount가 왜 필요?" → overlay와 dynamic DT(`CONFIG_OF_DYNAMIC`)에서 노드가 해제될 수 있다. 정적 구성에선 get/put이 no-op이지만 코드는 항상 맞게 써야 한다.

**Q.** How does of_parse_phandle_with_args work? Give an example.

**A.** `clocks = <&clkc 3>` 같은 속성에서 첫 cell은 phandle, 이후 인자 개수는 대상 노드의 `#clock-cells`가 정한다. 함수는 phandle로 노드를 찾고, 그 노드의 cells 속성을 읽어 그만큼 인자를 `of_phandle_args`에 채워 준다. index로 리스트의 몇 번째 항목인지 고르고, 범위를 넘으면 `-ENOENT`. 반환된 `np`는 put 해야 한다. clk, gpio, dma, pwm 프레임워크가 내부에서 다 이걸 쓴다.

> In clocks = <&clkc 3>, the first cell is a phandle and the number of argument cells is defined by the provider's #clock-cells. The helper resolves the phandle to a node, reads that cells property, and fills of_phandle_args with the node and the arguments. The index selects which entry in the list; past the end it returns -ENOENT. The returned np holds a reference. The clk, gpio, dma and pwm frameworks all build on it.

**Q.** A property exists in the DTS but your driver reads 0 / garbage. How do you debug it?

**A.** 먼저 `/proc/device-tree`에서 커널이 실제로 받은 바이트를 hexdump로 본다 — 부트로더가 다른 DTB를 주거나 고쳐 넣는 일이 많다. 그다음 `of_node` 심볼릭 링크로 디바이스가 맞는 노드에 붙었는지, 속성 이름 오타, 타입 불일치(문자열을 u32로 읽어도 에러가 안 남)를 확인하고, 반환값을 로그로 찍는다. 근본 대책은 YAML 바인딩과 `dtbs_check`.

> First I check what the kernel actually received: hexdump the file under /proc/device-tree, because the bootloader may have loaded or patched a different DTB. Then I confirm the device's of_node symlink points at the node I expect, look for a typo in the property name, and check for a type mismatch — reading a string as u32 succeeds silently. I log the return codes. The long-term fix is a YAML binding and dtbs_check.

**Q.** How do you support two variants of a chip in one driver?

**A.** `of_device_id` 테이블에 compatible마다 `.data`로 변형별 상수 구조체(최대 ODR, 레지스터 맵, 기능 플래그)를 달고, probe에서 `device_get_match_data(dev)`로 꺼낸다. ACPI match table이나 I2C/platform id_table의 `driver_data`도 같은 구조체를 가리키게 하면 펌웨어 종류에 상관없이 한 경로로 처리된다. probe 곳곳에서 compatible 문자열을 비교하지 않는다.

> I attach a const per-variant struct — max ODR, register layout, feature flags — to each compatible via the .data field of the of_device_id table, and fetch it with device_get_match_data in probe. The ACPI table and the bus id_table point to the same structs, so every firmware path ends in one code path. I avoid sprinkling of_device_is_compatible checks through the driver.

**Q.** What's the difference between of_property_read_bool and of_property_present?

**A.** 둘 다 존재 여부를 bool로 돌려주지만, `read_bool`은 "값이 없는 진짜 boolean 속성"용이고 값이 있는 속성에 쓰면 6.x 커널은 경고를 찍는다. `present`는 타입과 상관없이 "이 속성이 있나"를 묻는 용도다. 예를 들어 `clocks`가 있는지 볼 때는 present.

> Both return whether the property exists. read_bool is meant for real boolean flags with no value, and recent kernels warn if you use it on a property that has a value. of_property_present is the type-agnostic presence check — for example, checking whether a clocks property exists.

**Q.** Why is of_get_named_gpio deprecated?

**A.** 전역 GPIO 번호를 돌려주는 legacy integer API라서 컨트롤러 probe 순서에 따라 번호가 바뀌고, DT의 active-low 플래그를 무시하며, DT 전용이다. 대신 `devm_gpiod_get(dev, "reset", flags)`로 디스크립터를 받으면 극성은 DT가 정하고 ACPI에서도 동작한다. 선택 핀은 `_optional`로 받아 NULL이면 그냥 생략된다.

> It returns a global integer GPIO number, which depends on controller probe order, ignores the active-low flag in the DT, and only works with device tree. The descriptor API — devm_gpiod_get(dev, "reset", flags) — handles polarity from firmware and works with ACPI too. For optional pins, devm_gpiod_get_optional returns NULL when absent, and gpiod calls on NULL are no-ops.

---

## 13. 직접 해보기

1. (개념) `of_property_read_u32(np, "gains", &v)`는 4개짜리 배열에서 무엇을 돌려주는가? 에러인가? 그리고 `of_property_read_u32_array(np, "sample-rate", buf, 2)`는?
   정답/힌트: 첫 cell 1을 돌려주고 성공(0) — `sz_max = 0`이라 상한 검사가 없다. 두 번째는 길이 4 < 8이라 `-EOVERFLOW`.

2. (코드) 예제 7의 `d8_fw`에 "`gains`가 있으면 개수가 1~8개인지 검사하고, 없으면 `{1}`로 기본값"을 `device_property_count_u32()`와 `device_property_read_u32_array()`로 추가하라. `d8-sensor-b`(gains 없음)와 `d8-sensor-a` 둘 다 probe 성공해야 한다.
   정답/힌트: `n = device_property_count_u32(dev, "gains"); if (n == -EINVAL) {기본값} else if (n < 1 or n > 8) return dev_err_probe(...)`.

3. (코드) `d8_child`의 두 번째 루프를 `device_for_each_child_node_scoped()`로 바꾸고, `reg`가 3 이상인 채널이 있으면 probe를 실패시키도록 하라. DTS에 `channel@5 { reg = <5>; }`를 추가해 실패 메시지를 확인하라. `return`이 루프 안에 있어도 refcount가 새지 않는 이유를 설명하라.
   정답/힌트: `__free(fwnode_handle)`가 스코프 탈출 시 `fwnode_handle_put()`을 호출한다.

4. (관찰) 게스트에서 `ls -l /proc/device-tree`가 무엇을 가리키는지 확인하고, `/sys/firmware/devicetree/base/d8-sensor-a/name` 파일 내용을 hexdump 하라. 그 파일은 DTS 어디에도 없는데 왜 있는가?
   정답/힌트: `/proc/device-tree`는 `/sys/firmware/devicetree/base`로의 심볼릭 링크. `name`은 unflatten의 `populate_properties()`가 OpenFirmware 호환용으로 추가한 의사 속성.

5. (코드) DTS의 `d8-clkgen`을 `#d8-cells = <1>`로 바꾸고 `d8,clk = <&d8_clkgen 3 &d8_clkgen 9>`로 두 항목을 만든 뒤, `of_count_phandle_with_args()`와 index 0/1의 `args`를 출력하라. `#d8-cells = <2>`로 두면 어떻게 되는지도 예측 후 확인하라.
   정답/힌트: cells 1이면 count 2, args 3과 9. cells 2면 첫 항목이 `{3, 1}`(두 번째 phandle cell을 인자로 먹음)이 되고 이후 해석이 깨진다 — provider의 cells 값이 진실의 근원이다.

6. (관찰) `of_property_read_bool()`을 `d8,legacy-flag`에 부르면 dmesg에 경고가 찍혔다. 같은 일을 `device_property_read_bool()`로 해도 찍히는가? 소스(`drivers/of/property.c`의 `of_fwnode_property_read_bool`)를 보고 예측한 뒤 실험하라.
   정답/힌트: 찍힌다 — `of_fwnode_property_read_bool()`이 `of_property_read_bool()`을 그대로 부른다.

7. (설계) 센서 키트의 GNSS 수신기(UART) 드라이버에 "PPS 핀(선택), 보드레이트(기본 9600), 안테나 전원 레귤레이터(선택)"를 DT로 넘기는 바인딩을 설계하고, probe의 읽기 코드를 `device_property_*`·gpiod·regulator API로 10줄 안에 써 보라.
   정답/힌트: `pps-gpios` → `devm_gpiod_get_optional(dev, "pps", GPIOD_IN)`, `current-speed`(표준 속성) → `device_property_read_u32` + 기본값, `vant-supply` → `devm_regulator_get_optional()`.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| FDT / DTB | Flattened Device Tree / Device Tree Blob | dtc가 만든 바이너리. 헤더 + struct 토큰 열 + strings 블록 |
| unflatten | 평탄 → 트리 | 부팅 초기 `unflatten_device_tree()`가 DTB를 `device_node` 트리로 만드는 것 |
| `struct device_node` | DT 노드의 커널 표현 | name, phandle, properties 리스트, parent/child/sibling, kobj |
| `struct property` | DT 속성의 커널 표현 | name, length, value(DTB 안을 가리킴), next |
| cell | 32비트 빅엔디안 정수 | DT 정수의 기본 단위. `<...>` 안의 숫자 하나 |
| phandle | 노드 참조 번호 | `&label`을 dtc가 숫자로 바꾼 것, 대상 노드에 `phandle` 속성 추가 |
| `#xxx-cells` | 인자 개수 지정 속성 | provider 노드가 "내 참조 뒤엔 인자 N개"라고 선언 |
| OF | Open Firmware | DT의 원조(IEEE 1275). 커널 DT API 접두어 `of_`의 유래 |
| fwnode | firmware node | DT·ACPI·swnode를 하나로 추상화한 `struct fwnode_handle` |
| software node (swnode) | C 코드로 정의한 속성 | `property_entry` 배열로 DT 없이 속성을 제공 |
| ACPI `_DSD` | Device Specific Data | ACPI에서 DT식 key-value 속성을 담는 객체 |
| match data | 변형별 데이터 | `of_device_id.data` → `device_get_match_data()` |
| available | 사용 가능 노드 | `status`가 없거나 `"okay"`/`"ok"`인 노드 |
| refcount (kref) | 참조 카운트 | `of_node_get/put`, `fwnode_handle_get/put` |
| `__free()` / scoped | 스코프 기반 자동 해제 | GCC cleanup 속성. `_scoped` 순회 매크로의 기반 |
| `dev_err_probe()` | probe 에러 헬퍼 | 에러 로그 + 반환, `-EPROBE_DEFER`면 조용히 기록 |

---

## 15. 요약 & 체크리스트

DT는 부팅 초기에 `struct device_node` 트리로 풀리고, 각 노드는 `struct property`의 단일 연결 리스트를 가진다. 속성 값은 DTB 안을 가리키는 빅엔디안 바이트열이고 **타입 정보가 없다** — 타입은 읽는 함수가 정한다. OF API(`of_property_read_*`)는 `device_node`를 직접 받고, 통합 API(`device_property_*`)는 fwnode ops를 거쳐 DT 위에서는 같은 코드에 도착하지만 ACPI와 software node에서도 동작하므로 새 드라이버의 기본값이다. 에러는 `-EINVAL`(없음), `-EOVERFLOW`(길이 불일치), `-ENODATA`(주로 문자열에서 값 없음)이고, 실패 시 출력 변수가 안 바뀌는 성질로 "optional + default"를 짠다. 노드 포인터를 돌려주는 함수는 참조를 올리므로 put 해야 하고, 순회 중 탈출은 `_scoped` 매크로로 안전하게 한다. phandle 참조의 인자 개수는 provider의 `#xxx-cells`가 정한다. 변형별 차이는 `.data` + `device_get_match_data()`로 처리한다.

- [ ] `struct device_node`와 `struct property`의 주요 필드를 그리고, `of_find_property()`가 선형 탐색임을 설명할 수 있다
- [ ] DTB의 토큰(`BEGIN_NODE`, `PROP`, `END_NODE`)과 strings 블록을 hexdump에서 찾을 수 있다
- [ ] u32 / u32 배열 / u64 / 문자열 / 문자열 리스트 / bool 속성을 OF API로 읽는 코드를 짤 수 있다
- [ ] `of_property_read_bool`과 `of_property_present`를 언제 쓰는지 구분할 수 있다
- [ ] `-EINVAL` / `-ENODATA` / `-EOVERFLOW`가 각각 언제 나오는지, 빈 속성을 u32로 읽으면 무엇이 나오는지 말할 수 있다
- [ ] optional 속성 + 기본값, required 속성 + `dev_err_probe` 패턴을 코드로 쓸 수 있다
- [ ] `for_each_child_of_node`의 refcount 동작과 `break` 시 누수, `_scoped` 매크로의 원리를 설명할 수 있다
- [ ] `of_parse_phandle_with_args`의 해석 과정(phandle → `#cells` → args)을 그림으로 설명할 수 있다
- [ ] `device_property_*` / `fwnode_*`로 같은 노드를 읽고, fwnode 순회가 disabled 노드를 건너뛴다는 것을 안다
- [ ] `.data` + `device_get_match_data()`로 칩 변형을 처리할 수 있다
- [ ] `of_get_named_gpio` 대신 `devm_gpiod_get(_optional)`을 써야 하는 이유를 말할 수 있다
- [ ] `/proc/device-tree`와 `of_node` 심볼릭 링크로 커널이 받은 값을 검증할 수 있다

---

## 참고 자료

- docs.kernel.org — [Linux and the Devicetree (usage model)](https://docs.kernel.org/devicetree/usage-model.html)
- docs.kernel.org — [Kernel API: Open Firmware and Device Tree](https://docs.kernel.org/devicetree/kernel-api.html) (`of_property_read_*`, `of_parse_phandle_with_args` 문서 주석 모음)
- docs.kernel.org — [Scope-based Cleanup Helpers](https://docs.kernel.org/core-api/cleanup.html) (`__free`, `DEFINE_FREE`)
- docs.kernel.org — [GPIO Descriptor Consumer Interface](https://docs.kernel.org/driver-api/gpio/consumer.html) 와 [Legacy GPIO](https://docs.kernel.org/driver-api/gpio/legacy.html)
- Devicetree Specification v0.4 — [devicetree.org/specifications](https://www.devicetree.org/specifications/) (FDT 형식 5장, phandle·`#cells`)
- 랩 소스 (Linux 6.18.55, VM의 `/var/lab/src/linux-6.18.55`): `include/linux/of.h`, `drivers/of/base.c`(`__of_find_property`, `__of_get_next_child`), `drivers/of/property.c`(`of_find_property_value_of_size`, `of_property_read_bool`, `of_fwnode_ops`), `drivers/of/fdt.c`(`populate_properties`), `drivers/base/property.c`(`fwnode_property_read_int_array`, `device_get_match_data`), `include/linux/property.h`, `include/linux/fwnode.h`, `drivers/gpio/gpiolib-of.c`(`of_get_named_gpio` DEPRECATED 주석)
- elixir.bootlin.com — 같은 파일을 버전별로 비교할 때: [drivers/of/property.c](https://elixir.bootlin.com/linux/v6.18/source/drivers/of/property.c)
- Bootlin, "Linux kernel and driver development" 교육 슬라이드 — Device Tree 및 platform driver 장 ([bootlin.com/training/kernel](https://bootlin.com/training/kernel/))
