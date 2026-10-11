# C4. U-Boot 포팅 — 새 보드를 지원하려면 무엇을 고치나

> **이 노트를 다 읽으면**: U-Boot 소스 트리에서 "보드 포트가 손대는 파일"과 "손대지 않는 공통 코드"를 구분할 수 있다 · defconfig·fragment·`savedefconfig`로 보드 기본값을 바꾸고 그 결과를 `.config`/`autoconf.h`까지 추적할 수 있다 · `U_BOOT_CMD` 명령과 `U_BOOT_DRIVER` DM 드라이버를 직접 써서 QEMU에서 bind/probe 로그와 `dm tree`로 확인할 수 있다 · SPL·DDR training·pre-relocation·relocation이 왜 있는지 메모리 지도로 설명할 수 있다
> **왜 필요한가**: 새 보드 bring-up 첫 2주의 일이 "EVK의 U-Boot를 우리 carrier board로 옮기기"다. 면접에서는 "How would you port U-Boot to a new board?", "What does SPL do?", "Explain U-Boot driver model vs Linux"가 나온다
> **Don 기준 난이도**: 부트로더가 ROM→SRAM→DRAM으로 자기를 옮기는 것, DDR 컨트롤러 초기화, 레지스터 읽기, 링커 섹션은 SSD 펌웨어에서 이미 했다 / Kconfig 빌드 흐름, Device Tree로 드라이버를 붙이는 driver model, `bootph-*` 태그, U-Boot의 init 훅 이름들이 새로 배울 부분
> **선행 노트**: C1(부트 전체 지도), C3(U-Boot 사용), B4(빌드 시스템). 뒤에서 이어지는 노트: D(Device Tree), E2(커널 Kconfig), G2/G3(Linux driver model)

---

## 0. 큰 그림 — "포팅"이란 무엇을 하는 일인가

Don이 SSD 컨트롤러의 새 리비전을 bring-up하던 장면을 떠올려 보자. 코어(Cortex-R)와 대부분의 펌웨어는 그대로다. 바뀌는 것은 **보드·칩마다 다른 부분** — 클럭 트리 설정값, DRAM 타이밍 테이블, 핀 배치, 어느 UART로 로그를 낼지, NAND 채널 수 — 이고, 이것들은 보통 "보드 설정 헤더 + 초기화 함수 몇 개 + 타이밍 테이블"에 모여 있었다.

U-Boot 포팅도 정확히 같다. U-Boot는 수천 개 보드를 하나의 소스 트리로 지원한다(이 트리에만 defconfig가 1537개다). 그래서 코드가 **공통 코드**(명령, 환경 변수, 파일시스템, 네트워크, 부팅 로직, CPU 아키텍처 코드)와 **보드별 데이터·훅**(defconfig, Device Tree, `board/<vendor>/<board>/` 안의 C 파일, 기본 환경 변수 파일)으로 깔끔하게 나뉘어 있다. **포팅 = 보드별 부분을 새로 만드는 일**이고, 대개 가장 비슷한 기존 보드(보통 칩 벤더의 EVK)를 복사해서 시작한다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">U-Boot 소스 트리 — 보드 포트가 손대는 곳</text>
  <rect x="20" y="36" width="260" height="24" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="53" font-size="12">configs/&lt;board&gt;_defconfig</text>
  <text x="295" y="53" font-size="12">보드 선택 + 기능 on/off (Kconfig 값)</text>
  <rect x="20" y="66" width="260" height="24" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="83" font-size="12">board/&lt;vendor&gt;/&lt;board&gt;/</text>
  <text x="295" y="83" font-size="12">board.c 훅, Kconfig, Makefile, .env, DDR 타이밍</text>
  <rect x="20" y="96" width="260" height="24" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="113" font-size="12">arch/arm/dts/&lt;board&gt;-u-boot.dtsi</text>
  <text x="295" y="113" font-size="12">U-Boot 전용 DT 추가분 (bootph-*, binman)</text>
  <rect x="20" y="126" width="260" height="24" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="30" y="143" font-size="12">include/configs/&lt;board&gt;.h</text>
  <text x="295" y="143" font-size="12">CFG_SYS_SDRAM_BASE 등 남은 C 매크로</text>
  <rect x="20" y="156" width="260" height="24" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="173" font-size="12">dts/upstream/src/arm64/&lt;vendor&gt;/</text>
  <text x="295" y="173" font-size="12">Linux와 공유하는 보드 .dts (OF_UPSTREAM)</text>
  <rect x="20" y="186" width="260" height="24" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="203" font-size="12">arch/arm/mach-&lt;soc&gt;/</text>
  <text x="295" y="203" font-size="12">새 SoC일 때만 (clock, pinmux, SPL, DDR 드라이버)</text>
  <rect x="20" y="216" width="260" height="24" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="30" y="233" font-size="12">drivers/&lt;subsys&gt;/</text>
  <text x="295" y="233" font-size="12">보드에 지원 안 되는 칩이 있을 때 DM 드라이버</text>
  <rect x="20" y="246" width="260" height="24" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="30" y="263" font-size="12">arch/arm/cpu/armv8/, arch/arm/lib/</text>
  <text x="295" y="263" font-size="12">CPU 공통: start.S, MMU, cache, relocate</text>
  <rect x="20" y="276" width="260" height="24" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="30" y="293" font-size="12">common/ (board_f.c, board_r.c)</text>
  <text x="295" y="293" font-size="12">init 시퀀스, main_loop, autoboot</text>
  <rect x="20" y="306" width="260" height="24" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="30" y="323" font-size="12">cmd/ env/ boot/ net/ fs/ lib/</text>
  <text x="295" y="323" font-size="12">명령, 환경, bootstd, 네트워크 (+ drivers/core)</text>
  <rect x="30" y="352" width="14" height="14" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="52" y="364" font-size="12">항상 만진다 (보드 포트)</text>
  <rect x="250" y="352" width="14" height="14" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="272" y="364" font-size="12">경우에 따라</text>
  <rect x="400" y="352" width="14" height="14" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="422" y="364" font-size="12">거의 안 만진다 (공통 코드)</text>
  <text x="340" y="390" text-anchor="middle" font-size="12">기존 SoC 위의 새 보드라면 초록 4곳 + 주황 1~2곳이 PR의 전부인 경우가 많다</text>
</svg>
```
그림 1 — U-Boot 소스 트리의 층. 초록은 보드 포트마다 새로 만드는 파일, 주황은 칩이나 주변장치가 새로울 때만, 회색은 모든 보드가 공유하는 공통 코드다.

새 보드 bring-up에서 U-Boot 포팅이 차지하는 위치는 이렇다.

- **SoC는 지원되고 보드만 새것** (가장 흔한 경우: i.MX8MP 위에 회사 자체 carrier board): EVK 디렉터리를 복사 → DDR 타이밍 교체(메모리 칩이 다르면) → DT에서 핀·PMIC·eMMC·이더넷 PHY 수정 → defconfig에서 필요 없는 것 끄기 → 환경 변수 위치와 `bootcmd` 정하기.
- **SoC도 새것** (실리콘 벤더 쪽 일): `arch/arm/mach-<soc>/`에 클럭·핀먹스·SPL 부트 경로, `drivers/`에 DDR 컨트롤러·클럭 드라이버까지 쓴다. 대부분의 회사는 벤더 BSP U-Boot fork를 받아서 시작한다.
- **주변장치 하나가 미지원** (예: 센서 키트 보드의 I2C GPIO expander로 이더넷 PHY 리셋을 풀어야 함): `drivers/gpio/`에 DM 드라이버 하나 추가.

이 노트는 실물 보드 대신 **QEMU `virt` 머신의 `qemu_arm64` 보드**로 손을 움직인다. 이 "보드"는 U-Boot에서 가장 작은 보드 포트라 구조가 한눈에 보인다. 그리고 실물 보드에서만 의미가 있는 SPL·DDR training은 i.MX8MP와 TI AM62 소스를 직접 읽어서 설명한다(§6).

> 실행 환경: Lima VM `elinux`(aarch64, Ubuntu 26.04, `uname -r` = `7.0.0-38-generic`, gcc 15.2.0, GNU ld 2.46, QEMU 10.2.1). U-Boot 소스는 공용 랩의 v2026.10(`/var/lab/src/u-boot`, 읽기 전용)을 `/var/tmp/C4/src`로 **복사해서** 수정했고, 빌드 출력은 모두 `O=/var/tmp/C4/...`다. QEMU는 `-machine virt -cpu cortex-a57 -m 512M -nographic -bios u-boot.bin`으로 돌렸고, U-Boot 콘솔 조작은 `expect` 스크립트로 했다. 아래 출력의 빌드 시각·주소·RTC 값은 실행마다 다르다. 출력에서 터미널 제어 문자(ESC 시퀀스)는 `sed`로 지웠다.

---

## 1. U-Boot 소스 트리 지도

### 1.1 최상위 디렉터리

| 디렉터리 | 무엇이 있나 | Linux 커널의 대응 |
|---|---|---|
| `arch/<arch>/` | CPU 시작 코드(`cpu/armv8/start.S`), MMU·캐시, relocation(`lib/relocate_64.S`), SoC별 코드(`mach-imx/`, `mach-k3/`, `mach-qemu/`), U-Boot DT(`dts/`) | `arch/arm64/` |
| `board/<vendor>/<board>/` | 보드 C 파일(init 훅), `Kconfig`, `Makefile`, 기본 환경 `.env`, DDR 타이밍 | (없음 — Linux는 보드 코드를 DT로 대체) |
| `configs/` | `<board>_defconfig` 1500여 개와 공유 fragment(`*.config`) | `arch/arm64/configs/` |
| `include/configs/` | 보드 헤더 `<board>.h` — Kconfig로 아직 못 옮긴 C 매크로 | (없음) |
| `drivers/` | DM 드라이버: serial, mmc, net, i2c, spi, gpio, clk, pinctrl, ram(DDR), misc … `drivers/core/`가 driver model 코어 | `drivers/` |
| `cmd/` | 콘솔 명령 하나당 파일 하나(`date.c`, `misc.c`, `fdt.c` …) | (없음) |
| `common/` | `board_f.c`(relocation 전 init), `board_r.c`(후 init), `main.c`, `autoboot.c`, `console.c` | `init/main.c` 비슷 |
| `boot/` | bootm, FIT, bootstd(bootdev/bootmeth/bootflow), PXE, 커널에 넘길 DT 고치기 | — |
| `env/` | 환경 변수 저장소 백엔드(`flash.c`, `mmc.c`, `spi.c`, `fat.c`, `ubi.c` …) | — |
| `dts/upstream/` | Linux 커널 DT를 그대로 미러링한 트리 (`OF_UPSTREAM`) | `arch/arm64/boot/dts/` |
| `net/`, `fs/`, `disk/`, `lib/` | TFTP/DHCP, FAT/ext4, 파티션, libfdt·해시·압축 | `net/`, `fs/`, `lib/` |
| `tools/` | `mkimage`, binman, dtoc, buildman | `scripts/` |

### 1.2 가장 작은 보드 포트 — `board/emulation/qemu-arm/`

무엇을 확인하나: QEMU 보드 디렉터리에 무엇이 있고, 실제 칩 보드(i.MX8MP EVK)와 비교해 얼마나 작은지 본다.

```sh
cd /var/tmp/C4/src
ls board/emulation/qemu-arm board/nxp/imx8mp_evk
wc -l board/nxp/imx8mp_evk/*.c board/emulation/qemu-arm/qemu-arm.c
```

```text
board/emulation/qemu-arm:
Kconfig
MAINTAINERS
Makefile
qemu-arm.c
qemu-arm.env

board/nxp/imx8mp_evk:
Kconfig
MAINTAINERS
Makefile
imx8mp_evk.c
imx8mp_evk.env
imximage-8mp-lpddr4.cfg
lpddr4_timing.c
spl.c
   50 board/nxp/imx8mp_evk/imx8mp_evk.c
 2048 board/nxp/imx8mp_evk/lpddr4_timing.c
  119 board/nxp/imx8mp_evk/spl.c
  204 board/emulation/qemu-arm/qemu-arm.c
 2421 total
```

출력에서 볼 것: 두 보드 모두 `Kconfig`·`Makefile`·`MAINTAINERS`·`.env`·보드 C 파일이라는 같은 뼈대를 갖는다. i.MX8MP에만 `spl.c`(SPL 단계 초기화)와 2048줄짜리 `lpddr4_timing.c`(DDR 컨트롤러·PHY 레지스터 값 테이블)가 있다. **실물 보드 포트에서 가장 큰 파일은 DDR 타이밍 테이블**이다 — Don이 SSD에서 DRAM 타이밍 테이블을 받아 넣던 것과 같다. QEMU에는 DDR 컨트롤러가 없으니 이 두 파일이 없다(§6).

`qemu-arm.c`의 204줄 안에서 "보드 포트"가 하는 일만 골라 보면 다음과 같다(파일 발췌).

```c
static struct mm_region qemu_arm64_mem_map[] = {      /* MMU 페이지 테이블 소스 */
	{ .virt = 0x00000000UL, .phys = 0x00000000UL,   /* Flash */
	  .size = 0x08000000UL,
	  .attrs = PTE_BLOCK_MEMTYPE(MT_NORMAL) | PTE_BLOCK_INNER_SHARE },
	{ .virt = 0x08000000UL, .phys = 0x08000000UL,   /* Lowmem peripherals */
	  .size = 0x38000000,
	  .attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) | PTE_BLOCK_NON_SHARE |
		   PTE_BLOCK_PXN | PTE_BLOCK_UXN },
	{ .virt = 0x40000000UL, .phys = 0x40000000UL,   /* RAM */
	  .size = 255UL * SZ_1G, ... },
	...
};
struct mm_region *mem_map = qemu_arm64_mem_map;

int board_late_init(void)            /* board_init_r 끝부분에서 호출 */
{
	virtio_init();
	if (CONFIG_IS_ENABLED(USB_KEYBOARD))
		usb_init();
	return 0;
}

int dram_init(void)                  /* board_init_f 안: RAM 크기 결정 */
{
	if (fdtdec_setup_mem_size_base() != 0)
		return -EINVAL;
	...
}

int board_fdt_blob_setup(void **fdtp) /* control DT가 어디 있나 */
{
	/* QEMU loads a generated DTB for us at the start of RAM. */
	*fdtp = (void *)CFG_SYS_SDRAM_BASE;
	return 0;
}
```

네 가지가 보드 포트의 핵심 훅이다: **메모리 지도(MMU 속성)**, **DRAM 크기(`dram_init`)**, **control DT 위치(`board_fdt_blob_setup`)**, **늦은 초기화(`board_late_init`)**. Don의 SSD 펌웨어에 있던 "MPU region 테이블 + DRAM 크기 감지 + 보드 init 함수"와 그대로 대응한다.

### 1.3 defconfig 한 줄이 보드 디렉터리를 고르는 사슬

`make qemu_arm64_defconfig`가 어떻게 `board/emulation/qemu-arm/`을 찾아가는지 따라가 보자. 핵심은 Kconfig 심볼 네 개 `SYS_ARCH`·`SYS_CPU`·`SYS_VENDOR`·`SYS_BOARD`다. 최상위 `Makefile`이 이 값으로 `board/$(VENDOR)/$(BOARD)/`와 `arch/$(ARCH)/cpu/$(CPU)/`를 빌드 대상에 넣는다.

무엇을 확인하나: defconfig의 `CONFIG_ARCH_QEMU=y`에서 출발해 보드 디렉터리 이름이 정해지는 과정.

```sh
grep -E "^CONFIG_(SYS_ARCH|SYS_CPU|SYS_VENDOR|SYS_BOARD|SYS_CONFIG_NAME|TARGET_QEMU_ARM_64BIT|ARCH_QEMU)=" /var/tmp/C4/build/.config
sed -n 31,38p arch/arm/mach-qemu/Kconfig
```

```text
CONFIG_SYS_ARCH="arm"
CONFIG_SYS_CPU="armv8"
CONFIG_SYS_VENDOR="emulation"
CONFIG_SYS_BOARD="qemu-arm"
CONFIG_SYS_CONFIG_NAME="qemu-arm"
CONFIG_ARCH_QEMU=y
CONFIG_TARGET_QEMU_ARM_64BIT=y
if TARGET_QEMU_ARM_32BIT || TARGET_QEMU_ARM_64BIT

config SYS_BOARD
	default "qemu-arm"

config SYS_CONFIG_NAME
	default "qemu-arm"

endif
```

출력에서 볼 것: defconfig에는 `CONFIG_ARCH_QEMU=y`만 있고(§2 참고), `TARGET_QEMU_ARM_64BIT`는 `choice`의 기본값으로, `SYS_BOARD="qemu-arm"`은 그 아래 `if` 블록의 `default`로 정해진다. `SYS_CONFIG_NAME`은 `include/configs/qemu-arm.h`를 고른다. **새 보드를 추가할 때 만드는 사슬도 똑같다**: `arch/arm/mach-<soc>/Kconfig`에 `config TARGET_MYBOARD` 항목을 추가하고, `board/<vendor>/<board>/Kconfig`에서 `SYS_BOARD`/`SYS_VENDOR`/`SYS_CONFIG_NAME` 기본값을 주고, defconfig에 `CONFIG_TARGET_MYBOARD=y`를 쓴다.

`include/configs/qemu-arm.h`는 이제 거의 비어 있다.

```c
/* Physical memory map */
#define CFG_SYS_SDRAM_BASE		0x40000000
```

예전 U-Boot는 보드 설정을 전부 이 헤더의 `#define CONFIG_...`로 했다. 지금은 거의 모두 Kconfig로 옮겨졌고, 남은 것은 `CFG_` 접두어로 이름이 바뀌었다. 옛 BSP(2018년 이전 벤더 fork)를 다루면 이 헤더가 수백 줄인 것을 보게 된다 — 그게 "Kconfig 이전 시대"의 흔적이다.

---

## 2. Kconfig와 defconfig — Linux와 같은 언어, 같은 흐름

### 2.1 직관

Kconfig는 "이 빌드에 무엇을 넣을지"를 정하는 **설정 언어 + 도구**다. U-Boot는 Linux 커널의 Kconfig를 그대로 가져왔다(`scripts/kconfig/`가 커널 것과 같은 계열). 그래서 여기서 배우는 것은 E2(커널 설정·빌드)에 그대로 쓰인다.

- **Kconfig 파일**: 심볼 정의. `config CMD_DATE` / `bool "date"` / `default y if DM_RTC` / `select LIB_DATE` / `depends on ...` / `imply ...`.
- **defconfig**: 보드가 **기본값과 다르게** 고른 심볼만 적은 최소 목록.
- **`.config`**: 모든 심볼의 최종 값(이 빌드에서 1879줄). 사람이 직접 고치기보다 도구가 만든다.
- **`include/generated/autoconf.h`**: C 코드가 쓰는 `#define CONFIG_X 1`.
- **`include/config/auto.conf`**: Makefile이 쓰는 `CONFIG_X=y`. `obj-$(CONFIG_X) += x.o`가 이것으로 `obj-y` 또는 `obj-`가 된다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="10" width="200" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="31" text-anchor="middle" font-size="13">Kconfig 트리</text>
  <text x="120" y="50" text-anchor="middle" font-size="12">arch/ board/ drivers/ cmd/</text>
  <rect x="240" y="10" width="220" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="350" y="31" text-anchor="middle" font-size="13">qemu_arm64_defconfig</text>
  <text x="350" y="50" text-anchor="middle" font-size="12">보드가 고른 값 (최소본)</text>
  <rect x="480" y="10" width="180" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="31" text-anchor="middle" font-size="13">c4.config</text>
  <text x="570" y="50" text-anchor="middle" font-size="12">fragment (덮어쓰기)</text>
  <line x1="120" y1="60" x2="250" y2="98" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="350" y1="60" x2="340" y2="98" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="570" y1="60" x2="440" y2="98" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="140" y="100" width="400" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="121" text-anchor="middle" font-size="12">make O=build qemu_arm64_defconfig</text>
  <text x="340" y="140" text-anchor="middle" font-size="12">make O=build c4.config (merge_config.sh + oldconfig)</text>
  <line x1="340" y1="150" x2="340" y2="184" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="230" y="186" width="220" height="46" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="206" text-anchor="middle" font-size="13" font-weight="bold">.config</text>
  <text x="340" y="224" text-anchor="middle" font-size="12">모든 심볼의 최종 값 (1879줄)</text>
  <line x1="230" y1="209" x2="122" y2="268" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="340" y1="232" x2="340" y2="268" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="450" y1="209" x2="560" y2="268" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="20" y="270" width="200" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="291" text-anchor="middle" font-size="12">make savedefconfig</text>
  <text x="120" y="310" text-anchor="middle" font-size="12">→ 최소 defconfig</text>
  <rect x="240" y="270" width="200" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="291" text-anchor="middle" font-size="12">include/generated/autoconf.h</text>
  <text x="340" y="310" text-anchor="middle" font-size="12">#define CONFIG_X 1</text>
  <rect x="460" y="270" width="200" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="291" text-anchor="middle" font-size="12">include/config/auto.conf</text>
  <text x="560" y="310" text-anchor="middle" font-size="12">CONFIG_X=y (make용)</text>
  <line x1="340" y1="320" x2="340" y2="354" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="560" y1="320" x2="560" y2="354" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="240" y="356" width="200" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="381" text-anchor="middle" font-size="12">C: #ifdef, IS_ENABLED()</text>
  <rect x="460" y="356" width="200" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="560" y="381" text-anchor="middle" font-size="12">obj-$(CONFIG_X) += x.o</text>
  <line x1="340" y1="396" x2="420" y2="424" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="560" y1="396" x2="480" y2="424" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="370" y="426" width="160" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="450" y="448" text-anchor="middle" font-size="13">u-boot.bin</text>
</svg>
```
그림 2 — Kconfig 흐름. defconfig와 fragment가 Kconfig 트리의 규칙(`depends on`, `select`, `default`)을 거쳐 `.config`가 되고, 거기서 C용 헤더와 make용 파일이 생성된다. `savedefconfig`는 반대 방향으로 최소본을 뽑는다.

### 2.2 Kconfig 키워드 다섯 개

| 키워드 | 뜻 | 예 (이 트리에서) |
|---|---|---|
| `depends on A` | A가 켜져야 이 심볼을 켤 수 있다 (꺼져 있으면 보이지도 않음) | `CMD_BOOTEFI_HELLO depends on CMD_BOOTEFI_BINARY && BOOTEFI_HELLO_COMPILE` |
| `select A` | 이 심볼이 켜지면 A를 **강제로** 켠다 (A의 depends 무시 — 위험) | `ARCH_QEMU`가 `select DM`, `select OF_CONTROL` |
| `imply A` | 이 심볼이 켜지면 A의 **기본값**을 y로 (사용자가 끌 수 있음) | `ARCH_QEMU`가 `imply CMD_DM`, `imply RTC_PL031` |
| `default y if A` | A가 켜져 있으면 기본값 y | `CMD_DATE default y if DM_RTC` |
| `choice` | 여러 개 중 하나 | `TARGET_QEMU_ARM_32BIT` / `TARGET_QEMU_ARM_64BIT` |

`select`와 `imply`의 차이는 면접 단골이다. `ARCH_QEMU`는 DM과 OF_CONTROL 없이는 아예 동작이 불가능하니 `select`, RTC 드라이버는 있으면 좋지만 빼도 되니 `imply`다. 아래 §2.4에서 `imply`가 실제로 어떤 흔적을 남기는지 본다.

U-Boot만의 추가 규칙이 하나 있다. 같은 소스가 **U-Boot proper, SPL, TPL** 세 번 빌드되므로 심볼도 단계별로 따로 있다: `CONFIG_MISC`(proper), `CONFIG_SPL_MISC`, `CONFIG_TPL_MISC`. C 코드에서는 `CONFIG_IS_ENABLED(MISC)`가 "지금 빌드 중인 단계의 MISC"를 뜻한다. Makefile에서도 `obj-$(CONFIG_$(PHASE_)MISC)` 같은 패턴이 보인다. 그냥 `IS_ENABLED(CONFIG_MISC)`를 쓰면 SPL 빌드에서도 proper의 값을 보게 되는 버그가 생긴다.

### 2.3 실습 — defconfig fragment로 기본값 바꾸기

보드 기본값을 바꾸는 방법은 세 가지다. (1) `configs/<board>_defconfig`를 직접 고친다 — 자기 보드라면 정답. (2) `make menuconfig` 후 `savedefconfig` — 탐색할 때. (3) **fragment** — 같은 보드의 변형(디버그 빌드, 생산 빌드, 고객별 빌드)을 만들 때. 여기서는 (3)으로 QEMU 보드를 바꾼다.

U-Boot의 `scripts/kconfig/Makefile`은 `make <이름>.config`를 받으면 `kernel/configs/`, `arch/$(SRCARCH)/configs/`, 그리고 **`board/` 아래 전체에서 같은 이름의 파일을 찾아** `merge_config.sh`로 합친다.

```make
configfiles=$(wildcard $(srctree)/kernel/configs/$@ \
		$(srctree)/arch/$(SRCARCH)/configs/$@ \
		$(shell find $(srctree)/board -name "$@"))

%.config: $(obj)/conf
	$(if $(call configfiles),, $(error No configuration exists for this target on this architecture))
	$(Q)$(CONFIG_SHELL) $(srctree)/scripts/kconfig/merge_config.sh -m .config $(configfiles)
	+$(Q)yes "" | $(MAKE) -f $(srctree)/Makefile oldconfig
```

그래서 fragment를 `board/emulation/qemu-arm/c4.config`에 둔다(복사한 트리 안). 내용은 이렇다.

```text
# C4 defconfig fragment for qemu_arm64
CONFIG_BOOTDELAY=5
CONFIG_USE_PREBOOT=y
CONFIG_PREBOOT="echo c4: preboot ran, board=${c4_board}"
CONFIG_ENV_SOURCE_FILE="c4"
CONFIG_CMD_C4HELLO=y
CONFIG_C4_WIDGET=y
CONFIG_MISC=y
CONFIG_CMD_MISC=y
# CONFIG_CMD_TPM is not set
# CONFIG_CMD_EFIDEBUG is not set
# CONFIG_CMD_BOOTEFI_SELFTEST is not set
```

`CMD_C4HELLO`와 `C4_WIDGET`은 §4·§7에서 직접 추가하는 심볼이다. `ENV_SOURCE_FILE="c4"`는 기본 환경 변수 파일을 `board/emulation/qemu-arm/c4.env`로 바꾼다(§3.4).

무엇을 확인하나: defconfig 적용 후 fragment를 합치면 무엇이 덮어써지는지.

```sh
cd /var/tmp/C4/src
make O=/var/tmp/C4/build qemu_arm64_defconfig
make O=/var/tmp/C4/build c4.config
```

```text
  GEN     Makefile
Using .config as base
Merging /var/tmp/C4/src/board/emulation/qemu-arm/c4.config
Value of CONFIG_BOOTDELAY is redefined by fragment /var/tmp/C4/src/board/emulation/qemu-arm/c4.config:
Previous value: CONFIG_BOOTDELAY=2
New value: CONFIG_BOOTDELAY=5

Value of CONFIG_PREBOOT is redefined by fragment /var/tmp/C4/src/board/emulation/qemu-arm/c4.config:
Previous value: CONFIG_PREBOOT="usb start"
New value: CONFIG_PREBOOT="echo c4: preboot ran, board=${c4_board}"

Value of CONFIG_ENV_SOURCE_FILE is redefined by fragment /var/tmp/C4/src/board/emulation/qemu-arm/c4.config:
Previous value: CONFIG_ENV_SOURCE_FILE=""
New value: CONFIG_ENV_SOURCE_FILE="c4"

Value of CONFIG_CMD_C4HELLO is redefined by fragment /var/tmp/C4/src/board/emulation/qemu-arm/c4.config:
Previous value: # CONFIG_CMD_C4HELLO is not set
New value: CONFIG_CMD_C4HELLO=y
...
Value of CONFIG_CMD_TPM is redefined by fragment /var/tmp/C4/src/board/emulation/qemu-arm/c4.config:
Previous value: CONFIG_CMD_TPM=y
New value: # CONFIG_CMD_TPM is not set
...
Enable driver for generic I2C-attached EEPROMs (I2C_EEPROM) [N/y/?] (NEW) 
Enable gdsys RXAUI control driver (GDSYS_RXAUI_CTRL) [N/y/?] (NEW) 
...
#
# configuration written to .config
#
```

(같은 형식의 "redefined" 블록과 `(NEW)` 질문 줄 일부를 `...`로 생략했다.)

출력에서 볼 것: `merge_config.sh`가 바뀌는 값마다 "Previous / New"를 보여 준다. 그 뒤 `oldconfig`가 **새로 보이게 된 심볼**(MISC를 켜자 나타난 misc 드라이버 14개, `(NEW)`)에 `yes ""`로 기본값을 답한다. 결과 확인은 이렇게 한다.

```sh
grep -n "C4\|^CONFIG_MISC=\|CMD_MISC\|BOOTDELAY\|CONFIG_PREBOOT=\|ENV_SOURCE\|CMD_TPM\|CMD_EFIDEBUG\|BOOTEFI_SELFTEST" /var/tmp/C4/build/.config
```

```text
186:CONFIG_ENV_SOURCE_FILE="c4"
443:CONFIG_BOOTDELAY=5
469:CONFIG_PREBOOT="echo c4: preboot ran, board=${c4_board}"
637:# CONFIG_CMD_BOOTEFI_SELFTEST is not set
720:CONFIG_CMD_MISC=y
801:# CONFIG_CMD_EFIDEBUG is not set
805:CONFIG_CMD_C4HELLO=y
838:# CONFIG_CMD_TPM is not set
1171:CONFIG_MISC=y
1172:CONFIG_C4_WIDGET=y
```

> 함정 (이 VM에서 실제로 겪음): 처음에 `make O=... qemu_arm64_defconfig c4.config`처럼 **두 타깃을 한 줄에** 줬더니, 병합 메시지는 다 나왔는데 최종 `.config`에는 `BOOTDELAY=2`, `CMD_C4HELLO is not set` 등 원래 값이 남아 있었다. 두 번의 `make`로 나눠 실행하니 위처럼 정상 적용됐다. config 타깃은 한 번에 하나씩 실행하고, 결과는 항상 `.config`를 grep해서 확인한다.

C와 Makefile이 실제로 보는 값도 확인해 두자.

```sh
grep -n "C4\|CONFIG_BOOTDELAY " /var/tmp/C4/build/include/generated/autoconf.h
grep -n "C4\|BOOTDELAY" /var/tmp/C4/build/include/config/auto.conf
```

```text
47:#define CONFIG_C4_WIDGET 1
332:#define CONFIG_BOOTDELAY 5
472:#define CONFIG_CMD_C4HELLO 1
45:CONFIG_C4_WIDGET=y
330:CONFIG_BOOTDELAY=5
470:CONFIG_CMD_C4HELLO=y
```

### 2.4 `savedefconfig` — 바뀐 것만 남긴 defconfig 만들기

`.config`는 1879줄이라 리뷰할 수 없다. 보드 defconfig로 커밋할 때는 `make savedefconfig`로 **기본값과 다른 줄만** 뽑아 `configs/`에 넣는다.

무엇을 확인하나: fragment 적용 후의 최소 defconfig가 원래 defconfig와 어떻게 다른지.

```sh
make O=/var/tmp/C4/build savedefconfig
diff -u configs/qemu_arm64_defconfig /var/tmp/C4/build/defconfig
```

```text
--- configs/qemu_arm64_defconfig	2026-10-05 14:35:33.000000000 -0700
+++ /var/tmp/C4/build/defconfig	2026-10-10 10:26:20.942122093 -0700
@@ -2,6 +2,7 @@
 CONFIG_KVM_VIRT_INS=y
 CONFIG_ARCH_QEMU=y
 CONFIG_SYS_MALLOC_LEN=0x1000000
+CONFIG_ENV_SOURCE_FILE="c4"
 CONFIG_CUSTOM_SYS_INIT_SP_ADDR=0x40200000
 CONFIG_ENV_SIZE=0x40000
 CONFIG_ENV_SECT_SIZE=0x40000
@@ -22,7 +23,9 @@
 CONFIG_FIT_BEST_MATCH=y
 CONFIG_BOOTSTD_FULL=y
 CONFIG_LEGACY_IMAGE_FORMAT=y
+CONFIG_BOOTDELAY=5
 CONFIG_USE_PREBOOT=y
+CONFIG_PREBOOT="echo c4: preboot ran, board=${c4_board}"
 # CONFIG_DISPLAY_CPUINFO is not set
 # CONFIG_DISPLAY_BOARDINFO is not set
 # CONFIG_BOARD_INIT is not set
@@ -30,13 +33,14 @@
 CONFIG_BLOBLIST=y
 CONFIG_CMD_SMBIOS=y
 CONFIG_CMD_BOOTZ=y
-CONFIG_CMD_BOOTEFI_SELFTEST=y
+CONFIG_CMD_BOOTEFI_HELLO=y
 CONFIG_CMD_NVEDIT_EFI=y
 CONFIG_CMD_DFU=y
+CONFIG_CMD_MISC=y
 CONFIG_CMD_MTD=y
 CONFIG_CMD_PCI=y
-CONFIG_CMD_EFIDEBUG=y
-CONFIG_CMD_TPM=y
+CONFIG_CMD_POWEROFF=y
+CONFIG_CMD_C4HELLO=y
 CONFIG_CMD_MTDPARTS=y
 CONFIG_CMD_SPAWN=y
 CONFIG_ENV_IS_IN_FLASH=y
@@ -46,6 +50,8 @@
 CONFIG_DFU_TFTP=y
 CONFIG_DFU_MTD=y
 CONFIG_DFU_RAM=y
+CONFIG_MISC=y
+CONFIG_C4_WIDGET=y
 # CONFIG_MMC is not set
 CONFIG_MTD=y
 CONFIG_DM_MTD=y
```

출력에서 볼 것이 두 가지 있다.

- 끈 심볼(`CMD_TPM`, `CMD_EFIDEBUG`)은 `# ... is not set`으로 남지 않고 **줄이 사라졌다**. 이 심볼들의 기본값이 원래 n이기 때문이다. 원래 defconfig에서 `=y`로 켜던 줄이 없어지면 기본값 n으로 돌아간다.
- 내가 건드리지 않은 `CMD_BOOTEFI_HELLO=y`, `CMD_POWEROFF=y`가 **새로 생겼다**. 원인을 Kconfig에서 찾아보면:

```sh
grep -n "^config CMD_BOOTEFI_HELLO$" -A3 cmd/Kconfig
grep -rn "imply CMD_POWEROFF" --include=Kconfig* lib/
```

```text
504:config CMD_BOOTEFI_HELLO
505-	bool "Allow booting a standard EFI hello world for testing"
506-	depends on CMD_BOOTEFI_BINARY && BOOTEFI_HELLO_COMPILE
507-	default y if CMD_BOOTEFI_SELFTEST
lib/efi_selftest/Kconfig:8:	imply CMD_POWEROFF if PSCI_RESET || SYSRESET_PSCI
```

`CMD_BOOTEFI_HELLO`의 기본값은 `CMD_BOOTEFI_SELFTEST`를 따라가고, `CMD_POWEROFF`는 EFI selftest가 `imply`로 켜 주던 것이다. selftest를 끄면 둘의 **기본값이 n으로 바뀌는데**, `oldconfig`는 이미 y였던 값을 유지했다. 그래서 `savedefconfig`가 "기본값(n)과 다르다"며 명시적으로 `=y`를 적었다. 이것이 `imply`/`default`의 흔적이다. 실무 교훈: **defconfig diff 리뷰에서 "건드리지 않은 줄"이 나타나면 Kconfig 의존성 변화 때문**이니, 그 줄이 의도한 것인지(여기서는 QEMU를 끄는 `poweroff` 명령이 필요하니 의도대로다) 확인하고 커밋한다.

---

## 3. 보드 포트가 실제로 건드리는 것

### 3.1 체크리스트

| 무엇 | 파일 | 결정할 것 |
|---|---|---|
| 보드 등록 | `arch/arm/mach-<soc>/Kconfig`(TARGET), `board/<v>/<b>/Kconfig`, `MAINTAINERS` | `TARGET_xxx` 심볼, `SYS_BOARD`/`SYS_VENDOR`/`SYS_CONFIG_NAME` |
| defconfig | `configs/<b>_defconfig` | 콘솔 UART, 부트 미디어 드라이버, 명령 집합, `TEXT_BASE`, SPL 여부, `DEFAULT_DEVICE_TREE` |
| Device Tree | `dts/upstream/.../<b>.dts`(또는 `arch/arm/dts/<b>.dts`) + `arch/arm/dts/<b>-u-boot.dtsi` | 핀먹스, UART, eMMC/SD, PMIC, 이더넷 PHY, GPIO; 어떤 노드를 SPL·pre-reloc에 넣을지(`bootph-*`) |
| DDR | `board/<v>/<b>/lpddr4_timing.c`, 또는 DT의 DDR 노드(TI) | 메모리 칩 타이밍 — 벤더 툴이 생성 |
| 보드 C 훅 | `board/<v>/<b>/<b>.c`, `spl.c` | `board_init_f`(SPL), `dram_init`, `board_init`, `board_late_init`, `ft_board_setup` |
| 환경 변수 | defconfig `ENV_IS_IN_*`, `ENV_OFFSET`, `ENV_SIZE`; `board/<v>/<b>/<b>.env` | 저장 위치(eMMC 파티션·오프셋, SPI NOR 섹터), 기본 변수 |
| 부팅 명령 | defconfig `BOOTCOMMAND`, `BOOTDELAY`, `PREBOOT`; `.env` | bootstd(`bootflow scan`) / 스크립트 / 고정 `bootcmd` |
| 이미지 조립 | `<soc>-u-boot.dtsi`의 binman 노드 | SPL + DDR firmware + TF-A + U-Boot를 한 flash 이미지로 |
| 문서 | `doc/board/<vendor>/<b>.rst` | 빌드·플래싱 방법 (upstream 제출 시 필수) |

### 3.2 init 시퀀스 — 훅은 어디서 불리나

U-Boot proper의 init은 두 단계다. **`board_init_f`**("f" = flash에서 도는 단계, relocation 전)와 **`board_init_r`**("r" = RAM으로 relocation한 뒤). 이 버전(v2026.10)은 `common/board_f.c`·`board_r.c`에서 함수 배열 대신 `INITCALL()` 매크로를 줄줄이 부른다. 보드 훅이 어디 끼는지 실제 소스에서 뽑아 보자.

무엇을 확인하나: pre-reloc 단계에서 불리는 초기화 함수의 순서.

```sh
grep -n "INITCALL(" common/board_f.c | sed -n 1,40p
```

```text
923:	INITCALL(setup_mon_len);
924:	INITCALL(initf_malloc);
926:	INITCALL(fdtdec_setup);
929:	INITCALL(trace_early_init);
931:	INITCALL(initf_upl);
932:	INITCALL(log_init);
933:	INITCALL(initf_bootstage); /* uses its own timer, so does not need DM */
934:	INITCALL(event_init);
936:	INITCALL(bloblist_init);
938:	INITCALL(setup_spl_handoff);
940:	INITCALL(console_record_init);
943:	INITCALL(arch_cpu_init);	/* basic arch cpu dependent setup */
944:	INITCALL(mach_cpu_init);	/* SoC/machine dependent CPU setup */
945:	INITCALL(initf_dm);
947:	INITCALL(board_early_init_f);
951:	INITCALL(get_clocks);		/* get CPU and bus clocks (etc.) */
954:	INITCALL(timer_init);		/* initialize timer */
957:	INITCALL(board_postclk_init);
959:	INITCALL(env_init);		/* initialize environment */
960:	INITCALL(init_baud_rate);	/* initialze baudrate settings */
961:	INITCALL(serial_init);		/* serial communications setup */
962:	INITCALL(console_init_f);	/* stage 1 init of console */
963:	INITCALL(display_options);	/* say that we are here */
964:	INITCALL(display_text_info);	/* show debugging info if required */
965:	INITCALL(checkcpu);
967:	INITCALL(print_resetinfo);
971:	INITCALL(print_cpuinfo);
974:	INITCALL(embedded_dtb_select);
977:	INITCALL(show_board_info);
983:	INITCALL(init_func_i2c);
985:	INITCALL(announce_dram_init);
986:	INITCALL(dram_init);		/* configure available RAM banks */
988:	INITCALL(post_init_f);
992:	INITCALL(testdram);
996:	INITCALL(init_post);
1011:	INITCALL(setup_ram_base);
1012:	INITCALL(dram_init_banksize);
1013:	INITCALL(setup_ram_config);
1014:	INITCALL(setup_dest_addr);
1017:	INITCALL(fix_fdt);
```

출력에서 볼 것: `fdtdec_setup`(control DT 찾기) → `initf_dm`(pre-reloc DM: `bootph-*` 노드만 bind) → `serial_init`(콘솔) → `dram_init`(RAM 크기) → `setup_dest_addr` 이하 `reserve_*`들(relocation 목적지 계산, §8). 순서가 곧 의존성이다: **DT 없이는 DM이 없고, DM 없이는 시리얼이 없고, 시리얼 없이는 아무 것도 못 찍는다.** 그래서 bring-up 초기에 화면이 깜깜하면 이 앞부분을 의심한다(§11.2의 DEBUG_UART).

post-reloc 쪽은 이렇다(같은 방식으로 `common/board_r.c`에서 발췌, 줄 번호는 함수 안 순번).

```text
	INITCALL(initr_reloc);
	INITCALL(initr_caches);
	INITCALL(initr_malloc);
	INITCALL(initr_dm);
	INITCALL(board_init);	/* Setup chipselects */
	INITCALL(initr_dm_devices);
	INITCALL(serial_initialize);
	INITCALL(initr_flash);
	INITCALL(initr_env);
	INITCALL(console_init_r);	/* fully init console as a device */
	INITCALL(misc_init_r);
	INITCALL(board_late_init);
	INITCALL(initr_net);
	INITCALL(run_main_loop);
```

(사이사이의 `#if` 조건부 INITCALL은 생략했다.)

```svg
<svg viewBox="0 0 680 450" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="20" text-anchor="middle" font-size="13" font-weight="bold">공통 코드 (common/, arch/)</text>
  <text x="530" y="20" text-anchor="middle" font-size="13" font-weight="bold">보드가 채우는 훅</text>
  <rect x="20" y="32" width="300" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="53" text-anchor="middle" font-size="12">_start (arch/arm/cpu/armv8/start.S)</text>
  <text x="170" y="72" text-anchor="middle" font-size="12">flash 0x0에서 실행, 스택은 임시 SRAM/RAM</text>
  <line x1="170" y1="82" x2="170" y2="100" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="20" y="102" width="300" height="90" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="122" text-anchor="middle" font-size="13" font-weight="bold">board_init_f()  — pre-reloc</text>
  <text x="170" y="142" text-anchor="middle" font-size="12">fdtdec_setup → initf_dm (bootph 노드만)</text>
  <text x="170" y="161" text-anchor="middle" font-size="12">serial_init → dram_init → reserve_*</text>
  <text x="170" y="180" text-anchor="middle" font-size="12">BSS 없음, malloc_f 8 KiB, gd만 사용</text>
  <line x1="170" y1="192" x2="170" y2="210" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="20" y="212" width="300" height="50" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="170" y="233" text-anchor="middle" font-size="13">relocate_code()</text>
  <text x="170" y="252" text-anchor="middle" font-size="12">DRAM 꼭대기로 복사 + 주소 3833개 수정</text>
  <line x1="170" y1="262" x2="170" y2="280" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="20" y="282" width="300" height="90" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="302" text-anchor="middle" font-size="13" font-weight="bold">board_init_r()  — post-reloc</text>
  <text x="170" y="322" text-anchor="middle" font-size="12">initr_dm (전체 DT bind) → board_init</text>
  <text x="170" y="341" text-anchor="middle" font-size="12">initr_flash → initr_env → console_init_r</text>
  <text x="170" y="360" text-anchor="middle" font-size="12">misc_init_r → board_late_init → initr_net</text>
  <line x1="170" y1="372" x2="170" y2="390" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="20" y="392" width="300" height="46" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="412" text-anchor="middle" font-size="12">main_loop(): preboot → bootdelay</text>
  <text x="170" y="430" text-anchor="middle" font-size="12">→ bootcmd (또는 콘솔 프롬프트)</text>
  <rect x="400" y="110" width="260" height="76" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="530" y="130" text-anchor="middle" font-size="12">board_fdt_blob_setup()</text>
  <text x="530" y="149" text-anchor="middle" font-size="12">board_early_init_f()</text>
  <text x="530" y="168" text-anchor="middle" font-size="12">dram_init(), dram_init_banksize()</text>
  <line x1="398" y1="148" x2="322" y2="148" stroke="#e08a3c" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
  <rect x="400" y="290" width="260" height="76" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="530" y="310" text-anchor="middle" font-size="12">board_init()</text>
  <text x="530" y="329" text-anchor="middle" font-size="12">misc_init_r(), board_late_init()</text>
  <text x="530" y="348" text-anchor="middle" font-size="12">env 위치: board_mmc_get_env_dev()</text>
  <line x1="398" y1="328" x2="322" y2="328" stroke="#e08a3c" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
  <rect x="400" y="392" width="260" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="530" y="412" text-anchor="middle" font-size="12">bootm 직전 ft_board_setup()</text>
  <text x="530" y="430" text-anchor="middle" font-size="12">(커널에 줄 DT 고치기)</text>
  <line x1="398" y1="415" x2="322" y2="415" stroke="#e08a3c" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
</svg>
```
그림 3 — U-Boot proper의 init 흐름(왼쪽, 공통 코드)과 그 사이사이에서 불리는 보드 훅(오른쪽, 주황). 보드 C 파일은 이 훅들의 구현을 제공할 뿐, 흐름 자체는 건드리지 않는다.

### 3.3 보드 훅 함수 표

| 훅 | 단계 | 하는 일 | 구현 안 하면 |
|---|---|---|---|
| `board_fdt_blob_setup()` | f, 맨 처음 | control DT 주소를 알려 줌 (QEMU: RAM 시작) | 빌드 시 붙인 `u-boot.dtb` 사용 |
| `board_early_init_f()` | f | 핀먹스·클럭 같은 아주 이른 레지스터 설정 (`BOARD_EARLY_INIT_F`가 켜져야 호출) | 호출 안 됨 |
| `dram_init()` | f | `gd->ram_size` 설정 (DT `memory` 노드, SPD, 고정값) | **필수** — 링크 에러 |
| `dram_init_banksize()` | f | `gd->bd->bi_dram[]` 뱅크별 시작·크기 | weak 기본값(뱅크 1개) |
| `board_init()` | r | 보드 레벨 초기화 (`BOARD_INIT`가 켜져야 호출. qemu는 defconfig에서 끔) | 호출 안 됨 |
| `misc_init_r()` | r | 기타 (예: EEPROM에서 MAC 주소 읽어 env에 넣기) | 호출 안 됨 |
| `board_late_init()` | r, 콘솔·env 다음 | env 변수 설정(`board_name`), 부트 모드 감지, virtio 스캔 | 호출 안 됨 |
| `ft_board_setup()` | 커널 부팅 직전 | 커널에 넘길 DT 고치기(MAC, 메모리 크기, 시리얼 번호) | `OF_BOARD_SETUP` 꺼짐 |
| `mem_map` (변수) | f | ARMv8 MMU 영역 테이블 | 링크 에러(ARM64) |

i.MX8MP EVK의 `board_late_init()`가 좋은 실물 예다(발췌).

```c
int board_late_init(void)
{
#if CONFIG_IS_ENABLED(ENV_IS_IN_MMC)
	board_late_mmc_env_init();
#endif
#ifdef CONFIG_ENV_VARS_UBOOT_RUNTIME_CONFIG
	env_set("board_name", "EVK");
	env_set("board_rev", "iMX8MP");
#endif
	return 0;
}
```

부팅한 SD/eMMC 슬롯에 맞춰 env 장치를 고르고, 부트 스크립트가 쓸 변수를 넣는다. 보드 C 파일이 50줄로 끝나는 이유는 나머지가 전부 DT와 DM 드라이버로 갔기 때문이다.

### 3.4 환경 변수 — 기본값과 저장 위치

환경 변수에는 두 층이 있다.

- **기본 환경**(default env): 빌드 때 U-Boot 바이너리에 박힌다. `board/<v>/<b>/<b>.env` 텍스트 파일(또는 `CONFIG_ENV_SOURCE_FILE`로 지정한 파일)을 C 전처리기로 돌려 만든다.
- **저장된 환경**(saved env): `saveenv`로 저장 매체에 쓴 것. 부팅 때 읽어서 CRC가 맞으면 기본 환경 대신 쓴다.

C4용 `.env` 파일은 이렇게 만들었다(`qemu-arm.env`를 복사하고 두 줄 추가).

```text
/* C4: board env file, selected by CONFIG_ENV_SOURCE_FILE="c4" */
/* (copied from qemu-arm.env, then C4 variables appended) */
stdin=serial,usbkbd
stdout=serial,vidconsole
stderr=serial,vidconsole
fdt_addr=0x40000000
scriptaddr=0x40200000
pxefile_addr_r=0x40300000
kernel_addr_r=0x40400000
ramdisk_addr_r=0x44000000
boot_targets=qfw usb scsi virtio nvme dhcp
c4_board=c4-qemu-virt
c4_greet=echo hello from ${c4_board}
```

> 함정 (실제로 겪음): 처음에는 `#include "qemu-arm.env"`로 원본을 포함하려 했는데 빌드가 `include/generated/env.txt:2:10: fatal error: qemu-arm.env: No such file or directory`로 실패했다. 빌드 시스템이 `.env`를 `include/generated/env.txt`로 **복사한 뒤** 전처리하기 때문에 상대 경로 include가 깨진다. 다른 보드들은 `#include <env/nxp/...env>`처럼 `include/` 기준 경로를 쓴다.

무엇을 확인하나: 빌드 결과에 기본 환경이 어떻게 들어갔는지 (`make u-boot-initial-env`는 바이너리에 들어갈 기본 환경을 텍스트로 뽑는다).

```sh
make O=/var/tmp/C4/build u-boot-initial-env
grep -n "c4\|preboot\|bootdelay" /var/tmp/C4/build/u-boot-initial-env
```

```text
7:bootdelay=5
8:c4_board=c4-qemu-virt
9:c4_greet=echo hello from ${c4_board}
14:preboot=echo c4: preboot ran, board=${c4_board}
```

출력에서 볼 것: `bootdelay`와 `preboot`는 `.env`가 아니라 **Kconfig 값**(`CONFIG_BOOTDELAY`, `CONFIG_PREBOOT`)에서 온 것이다. 기본 환경은 "Kconfig에서 생성되는 변수 + `.env` 파일" 두 출처가 합쳐진다.

저장 위치는 `ENV_IS_IN_*` 중 하나를 고른다. qemu_arm64는 `ENV_IS_IN_FLASH`(`ENV_ADDR=0x4000000`, 두 번째 CFI flash 뱅크)다. 이 랩에서는 그 flash에 아무 것도 넣지 않으니 부팅 때마다 이 메시지가 뜬다.

```text
Loading Environment from Flash...*** Warning - bad CRC, using default environment
```

이건 에러가 아니라 "저장된 env가 없으니 기본 env 쓴다"는 뜻이다. 실물 보드에서도 공장 출하 직후 첫 부팅에 같은 메시지가 뜬다. 반대로 **출하 후 기본 env를 바꿨는데 반영이 안 된다**면 저장된 env가 이기고 있는 것이다(`env default -a; saveenv` 또는 env 영역 지우기).

| 저장소 | Kconfig | 실무에서 |
|---|---|---|
| eMMC/SD raw 오프셋 | `ENV_IS_IN_MMC` + `ENV_OFFSET`, `SYS_MMC_ENV_DEV/PART` | i.MX·TI 보드 기본. 부트 파티션(boot0/boot1)에 두기도 함 |
| SPI NOR 섹터 | `ENV_IS_IN_SPI_FLASH` + `ENV_OFFSET`, `ENV_SECT_SIZE` | 부트로더가 SPI NOR에 있는 보드 |
| FAT 파일 | `ENV_IS_IN_FAT` (`uboot.env`) | Raspberry Pi 류 |
| UBI 볼륨 | `ENV_IS_IN_UBI` | raw NAND 보드 |
| 없음 | `ENV_IS_NOWHERE` | 생산 보드에서 env 변조를 막을 때 |
| 이중화 | `SYS_REDUNDAND_ENVIRONMENT` + `ENV_OFFSET_REDUND` | 저장 중 전원 차단 대비 (Don의 SSD 메타데이터 이중 기록과 같은 발상) |

### 3.5 부팅 명령

최신 U-Boot의 기본 `bootcmd`는 **bootstd**(standard boot)다: `CONFIG_BOOTCOMMAND="bootflow scan -lb"`. 부팅 가능한 장치(bootdev)를 훑어 extlinux.conf, boot.scr, EFI 등(bootmeth)을 찾아 부팅한다(C3). 보드 포트에서는 `boot_targets` 변수로 장치 순서를 정하고, 필요하면 `BOOTCOMMAND`를 보드 전용 스크립트로 바꾼다. i.MX8MP EVK는 `CONFIG_BOOTCOMMAND="bootflow scan -lb; run bsp_bootcmd"`로 bootstd가 실패하면 NXP 전통 방식으로 넘어간다.

---

## 4. 실습 1 — 사용자 명령 `c4hello` 추가 (`U_BOOT_CMD`)

### 4.1 무엇을 만드나

bring-up 때 가장 먼저 만드는 도구가 "레지스터 하나 읽어 보는 명령"이다. U-Boot에는 `md`(memory display)가 있지만, **DT에서 주소를 찾아** 읽는 명령을 만들면 U-Boot가 DT·레지스터·relocation을 어떻게 다루는지 한 번에 보인다. `c4hello`는:

1. DT 루트의 `compatible` 속성을 읽고,
2. 인자로 받은 compatible(기본 `arm,pl011`)을 가진 노드를 찾아 `reg` 주소를 얻고,
3. 그 주소의 AMBA PrimeCell ID 레지스터(`base + 0xFE0..0xFEC`) 4개를 읽어 부품 번호를 해독하고,
4. 시스템 레지스터 `MIDR_EL1`(CPU 종류)을 읽고,
5. relocation 정보를 출력한다.

### 4.2 코드

`cmd/c4hello.c` (복사한 트리에 추가):

```c
// SPDX-License-Identifier: GPL-2.0+
/* C4 study: custom U-Boot command that reads DT properties and registers */
#include <command.h>
#include <dm/ofnode.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/types.h>

DECLARE_GLOBAL_DATA_PTR;

static int do_c4hello(struct cmd_tbl *cmdtp, int flag, int argc,
		      char *const argv[])
{
	const char *compat = argc > 1 ? argv[1] : "arm,pl011";
	ofnode root = ofnode_path("/");
	ofnode node;
	fdt_addr_t base;
	u32 pid = 0;
	u64 midr;
	int i;

	printf("model/compatible : %s\n", ofnode_read_string(root, "compatible"));

	node = ofnode_by_compatible(ofnode_null(), compat);
	if (!ofnode_valid(node)) {
		printf("no node with compatible \"%s\"\n", compat);
		return CMD_RET_FAILURE;
	}
	base = ofnode_get_addr(node);
	printf("node             : %s  reg base = 0x%llx\n",
	       ofnode_get_name(node), (unsigned long long)base);

	/* AMBA PrimeCell PeriphID0..3 live at base + 0xfe0..0xfec */
	for (i = 0; i < 4; i++)
		pid |= (readl(base + 0xfe0 + 4 * i) & 0xff) << (8 * i);
	printf("PeriphID         : 0x%08x (part 0x%03x, rev %u)\n",
	       pid, pid & 0xfff, (pid >> 20) & 0xf);

	asm volatile("mrs %0, midr_el1" : "=r"(midr));
	printf("MIDR_EL1         : 0x%08llx (impl 0x%02llx, part 0x%03llx)\n",
	       midr, midr >> 24, (midr >> 4) & 0xfff);

	printf("relocaddr        : 0x%lx  reloc_off 0x%lx\n",
	       gd->relocaddr, gd->reloc_off);
	printf("do_c4hello @     : %p (link addr %p)\n", do_c4hello,
	       (void *)((ulong)do_c4hello - gd->reloc_off));
	return CMD_RET_SUCCESS;
}

U_BOOT_CMD(c4hello, 2, 0, do_c4hello,
	   "C4 demo: read DT + PrimeCell ID registers",
	   "[compatible]  - default \"arm,pl011\"");
```

`U_BOOT_CMD(이름, 최대 인자 수, 반복 가능 여부, 함수, 짧은 도움말, 긴 도움말)`이다. 반복 가능(`_rep`=1)이면 프롬프트에서 Enter만 눌러도 다시 실행된다(`md`가 그렇다). `gd`는 **global data** 포인터로, AArch64에서는 레지스터 `x18`에 고정되어 있다(`DECLARE_GLOBAL_DATA_PTR`). relocation 전에는 BSS·전역 변수를 못 쓰므로 U-Boot는 모든 단계에서 살아남아야 하는 상태를 이 구조체에 모은다.

Kconfig와 Makefile에 한 항목씩 추가한다(복사한 트리 기준 diff).

```sh
diff -u /var/lab/src/u-boot/cmd/Kconfig cmd/Kconfig
```

```text
--- /var/lab/src/u-boot/cmd/Kconfig	2026-10-05 14:35:33.000000000 -0700
+++ cmd/Kconfig	2026-10-10 10:25:35.751901182 -0700
@@ -2493,6 +2493,13 @@
 	  placed into environment variables. Please check the source code for
 	  this as there is no documentation.
 
+config CMD_C4HELLO
+	bool "c4hello - C4 study demo command"
+	depends on OF_CONTROL
+	help
+	  Prints a DT property, a PrimeCell PeriphID register and
+	  MIDR_EL1. Study example for note C4.
+
 config CMD_DATE
 	bool "date"
 	default y if DM_RTC
```

```make
# cmd/Makefile 끝에 추가한 한 줄
obj-$(CONFIG_CMD_C4HELLO) += c4hello.o
```

### 4.3 빌드

무엇을 확인하나: 새 파일 두 개(명령, §7의 드라이버)가 경고 없이 컴파일되는지.

```sh
time make O=/var/tmp/C4/build -j6 > /var/tmp/C4/build.log 2>&1; echo rc=$?
grep -in "warning\|error" /var/tmp/C4/build.log
grep -n "c4hello\|c4_widget" /var/tmp/C4/build.log
ls -l /var/tmp/C4/build/u-boot.bin
```

```text
rc=0

real	1m12.120s
user	1m49.748s
sys	0m27.291s
516:  CC      cmd/c4hello.o
663:  CC      drivers/misc/c4_widget.o
-rw-rw-r-- 1 donh donh 1204392 Oct 10 10:28 /var/tmp/C4/build/u-boot.bin
```

출력에서 볼 것: `warning`/`error` grep 결과가 비어 있다(경고 0개). 처음부터 빌드해도 1분 남짓이다(같은 VM에서 다른 작업이 함께 돌아 원래 빌드 33초보다 느렸다). 커널에 비하면 U-Boot 빌드는 매우 빨라서 "고치고 → 빌드 → QEMU 부팅" 루프를 1~2분에 돌릴 수 있다.

### 4.4 QEMU에서 실행

`expect` 스크립트(`run.exp`)는 QEMU를 띄우고, autoboot 카운트다운에서 키를 눌러 멈춘 뒤, 인자로 받은 명령들을 차례로 치고 `poweroff`한다.

```text
#!/usr/bin/expect -f
# usage: run.exp <u-boot.bin> <dtb or -> <cmd1> [cmd2 ...]
set timeout 60
set bin [lindex $argv 0]
set dtb [lindex $argv 1]
set cmds [lrange $argv 2 end]
if {$dtb eq "-"} {
  spawn qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -bios $bin
} else {
  spawn qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -bios $bin -dtb $dtb
}
expect "Hit any key to stop autoboot" { send " " } timeout { puts "NO AUTOBOOT PROMPT"; exit 1 }
expect "=> "
foreach c $cmds {
  send "$c\r"
  expect "=> "
}
send "poweroff\r"
expect eof
```

무엇을 확인하나: `c4hello`가 DT에서 UART·RTC 주소를 찾고, 실제 레지스터 값을 읽는지.

```sh
cd /var/tmp/C4
expect /Users/donh/.elinux_scratch/C4/run.exp build/u-boot.bin c4_virt.dtb \
  "c4hello" "c4hello arm,pl031" "c4hello foo,bar" "dm tree" "run c4_greet" "printenv bootdelay preboot" \
  | tr -d '\r' | sed -r 's/\x1b\[[0-9;?]*[a-zA-Z]//g'
```

(`c4_virt.dtb`는 §7.4에서 만드는 DT다. 이 명령에는 원래 DT로도 충분하다. 이 한 번의 실행 출력을 §4.4, §5, §7.5에서 나눠 보여 준다. 여기서는 `c4hello` 부분만.)

```text
=> c4hello
model/compatible : linux,dummy-virt
node             : pl011@9000000  reg base = 0x9000000
PeriphID         : 0x00141011 (part 0x011, rev 1)
MIDR_EL1         : 0x411fd070 (impl 0x41, part 0xd07)
relocaddr        : 0x5f4c0000  reloc_off 0x5f4c0000
do_c4hello @     : 000000005f4ea70c (link addr 000000000002a70c)
=> c4hello arm,pl031
model/compatible : linux,dummy-virt
node             : pl031@9010000  reg base = 0x9010000
PeriphID         : 0x00141031 (part 0x031, rev 1)
MIDR_EL1         : 0x411fd070 (impl 0x41, part 0xd07)
relocaddr        : 0x5f4c0000  reloc_off 0x5f4c0000
do_c4hello @     : 000000005f4ea70c (link addr 000000000002a70c)
=> c4hello foo,bar
model/compatible : linux,dummy-virt
no node with compatible "foo,bar"
```

출력에서 볼 것:

- **PeriphID**: `0x00141011`의 하위 12비트 `0x011`은 PL**011**, `0x00141031`은 PL**031**(RTC)이다. 비트 [19:12] `0x41`은 설계사 Arm('A'), [23:20]은 리비전이다. QEMU가 실제 PrimeCell 레지스터를 흉내 내고 있다는 증거다. 실물 보드 bring-up에서 "이 주소에 정말 그 IP가 있나"를 ID 레지스터로 확인하는 것과 같다.
- **MIDR_EL1** `0x411fd070`: implementer 0x41(Arm), part 0xd07 = **Cortex-A57**(QEMU `-cpu cortex-a57`), variant 1 → r1p0.
- **link addr 0x2a70c vs 실행 주소 0x5f4ea70c**: 함수는 0x2a70c에 링크됐지만(`TEXT_BASE=0`), 지금은 `0x5f4c0000`만큼 올라간 DRAM에서 돌고 있다. relocation의 직접 증거다(§8).

### 4.5 명령은 어떻게 "등록"되나 — linker list

U-Boot에는 명령 등록 함수가 없다. `U_BOOT_CMD`는 `struct cmd_tbl` 하나를 `__u_boot_list_2_cmd_2_<이름>`이라는 **특별한 섹션**에 놓을 뿐이고, 링커 스크립트가 이 섹션들을 이름순으로 정렬해 하나의 배열로 이어 붙인다. 명령 파서는 그 배열을 처음부터 끝까지 훑는다. 드라이버(`U_BOOT_DRIVER`)도 똑같은 방식이다.

무엇을 확인하나: 명령과 드라이버가 `__u_boot_list` 섹션 안의 심볼로 들어갔는지.

```sh
cd /var/tmp/C4/build
nm u-boot | grep -i "c4hello\|u_boot_list_2_driver_2_c4\|u_boot_list_2_driver_2_serial_pl01x"
readelf -S u-boot | grep -A1 "u_boot_list"
```

```text
0000000000102ed8 D _u_boot_list_2_cmd_2_c4hello
0000000000104c60 D _u_boot_list_2_driver_2_c4_widget
00000000001056b0 D _u_boot_list_2_driver_2_serial_pl01x
000000000002a70c t do_c4hello
  [ 6] __u_boot_list     PROGBITS         0000000000102a58  00103a58
       0000000000006178  0000000000000000  WA       0     0     8
```

출력에서 볼 것: 내 명령과 드라이버가 기존 드라이버(`serial_pl01x`)와 같은 `__u_boot_list` 섹션(0x6178 = 24952바이트)에 나란히 들어갔다. `do_c4hello`의 0x2a70c는 위 실행 결과의 "link addr"와 정확히 같다. Linux 커널의 `__initcall` 섹션, `module_init`과 같은 발상이다 — Don의 펌웨어에서 "명령 테이블을 링커 섹션으로 모으는" 트릭을 써 봤다면 바로 그거다.

---

## 5. 실습 2 — 바뀐 기본값을 부팅에서 확인

§2.3의 fragment가 실제 부팅에 어떻게 드러나는지 보자. 비교를 위해 먼저 **원래 defconfig로 빌드한** U-Boot(`/var/tmp/C4/base`, 공용 랩 `out/u-boot.bin`과 같은 설정)를 부팅한다.

무엇을 확인하나: 기본 qemu_arm64 U-Boot의 부팅 메시지와 기본 변수.

```sh
expect /Users/donh/.elinux_scratch/C4/run.exp base/u-boot.bin - "version" "bdinfo" "printenv bootdelay bootcmd"
```

(`bdinfo` 출력은 생략했다. C4 빌드의 `bdinfo`는 §8.2에 있다.)

```text
U-Boot 2026.10 (Oct 10 2026 - 10:22:54 -0700)

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
=> version
U-Boot 2026.10 (Oct 10 2026 - 10:22:54 -0700)

gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
GNU ld (GNU Binutils for Ubuntu) 2.46
=> printenv bootdelay bootcmd
bootdelay=2
bootcmd=bootflow scan -lb
```

(카운트다운 줄은 원래 같은 줄을 지우며 다시 쓰는데, 제어 문자를 지워서 붙어 보인다.)

이제 C4 빌드다.

```sh
# §4.4와 같은 실행 — 부팅 메시지와 마지막 두 명령 부분
```

```text
c4_widget: bind  c4_rtc@9010000 (pre-reloc)


U-Boot 2026.10 (Oct 10 2026 - 10:27:07 -0700)

DRAM:  512 MiB
using memory 0x5e480000-0x5f4c0000 for malloc()
c4_widget: bind  c4_rtc@9010000 (post-reloc)
c4_widget: bind  c4_uart@9000000 (post-reloc)
Core:  53 devices, 15 uclasses, devicetree: board
Flash: 64 MiB
Loading Environment from Flash...*** Warning - bad CRC, using default environment

In:    serial,usbkbd
Out:   serial,vidconsole
Err:   serial,vidconsole
No USB controllers found
Net:   eth0: virtio-net#32

c4: preboot ran, board=c4-qemu-virt
Hit any key to stop autoboot: 5Hit any key to stop autoboot: 0
=> run c4_greet
hello from c4-qemu-virt
=> printenv bootdelay preboot
bootdelay=5
preboot=echo c4: preboot ran, board=${c4_board}
```

출력에서 볼 것:

- `preboot`가 autoboot 카운트다운 **직전**에 실행됐고, `.env`에서 온 `${c4_board}`가 치환됐다.
- 카운트다운이 2초 → 5초.
- `Core: 51 devices, 14 uclasses` → `53 devices, 15 uclasses`: DT 노드 2개(§7)와 misc uclass 1개가 늘었다.
- 맨 위의 `c4_widget: bind ... (pre-reloc)`이 **U-Boot 배너보다 먼저** 찍혔다. 이건 §8.4에서 설명한다.
- `malloc()` 영역 주소가 바뀌었다(`0x5e44f000` → `0x5e480000`). 바이너리 크기가 줄어 relocation 목적지가 달라졌기 때문이다(§8, §9).

---

## 6. SPL·TPL과 DDR 초기화 — QEMU에 없는 단계

### 6.1 왜 U-Boot가 여러 번 나뉘나

전원이 들어온 순간 SoC가 쓸 수 있는 메모리는 **칩 안의 작은 SRAM**(i.MX8MP OCRAM, TI AM62 MSRAM 등, 수백 KB 수준)뿐이다. DRAM은 컨트롤러와 PHY를 설정하고 **training**(신호 타이밍 보정)을 끝내야 쓸 수 있다. 그런데 U-Boot proper는 1 MB가 넘는다. 그래서:

- **SPL**(Secondary Program Loader): 같은 U-Boot 소스를 **최소 기능으로** 다시 빌드한 작은 바이너리. BootROM이 SRAM에 올려 실행한다. 할 일은 클럭·PMIC·콘솔 → **DDR 초기화** → 다음 단계(TF-A, U-Boot proper)를 부트 미디어에서 DRAM으로 로드.
- **TPL**(Tertiary PL): SRAM이 SPL조차 못 담을 만큼 작을 때 그 앞에 하나 더 (일부 Rockchip 등).
- **VPL**(Verifying PL): 어느 이미지를 부팅할지 검증·선택하는 단계(VBE).

빌드 결과물도 나뉜다: `spl/u-boot-spl.bin`, `tpl/u-boot-tpl.bin`, `u-boot.bin`. 같은 `.c` 파일이 단계마다 다른 `CONFIG_SPL_*`/`CONFIG_TPL_*`로 다시 컴파일된다(§2.2의 `CONFIG_IS_ENABLED`). Don에게 익숙한 말로 하면, SSD 컨트롤러의 **ROM 부트로더 → SRAM 로더(DRAM init) → DRAM에서 도는 메인 펌웨어**와 똑같은 3단 구조다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="13" font-weight="bold">i.MX8MP (NXP)</text>
  <rect x="20" y="32" width="110" height="56" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="75" y="55" text-anchor="middle" font-size="12">BootROM</text>
  <text x="75" y="74" text-anchor="middle" font-size="12">칩 내부 ROM</text>
  <line x1="130" y1="60" x2="148" y2="60" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="150" y="32" width="140" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="220" y="55" text-anchor="middle" font-size="12">SPL @ OCRAM</text>
  <text x="220" y="74" text-anchor="middle" font-size="12">0x920000</text>
  <line x1="290" y1="60" x2="308" y2="60" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="310" y="32" width="160" height="56" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="390" y="55" text-anchor="middle" font-size="12">DDR PHY training</text>
  <text x="390" y="74" text-anchor="middle" font-size="12">lpddr4_pmu_train 4개</text>
  <line x1="470" y1="60" x2="488" y2="60" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="490" y="32" width="170" height="56" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="575" y="55" text-anchor="middle" font-size="12">TF-A BL31 + U-Boot</text>
  <text x="575" y="74" text-anchor="middle" font-size="12">DRAM 0x40200000</text>
  <text x="20" y="130" font-size="13" font-weight="bold">TI AM62x</text>
  <rect x="20" y="140" width="110" height="56" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="75" y="163" text-anchor="middle" font-size="12">BootROM</text>
  <text x="75" y="182" text-anchor="middle" font-size="12">tiboot3.bin</text>
  <line x1="130" y1="168" x2="148" y2="168" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="150" y="140" width="140" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="220" y="163" text-anchor="middle" font-size="12">R5 SPL</text>
  <text x="220" y="182" text-anchor="middle" font-size="12">+ TIFS sysfw 로드</text>
  <line x1="290" y1="168" x2="308" y2="168" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="310" y="140" width="160" height="56" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="390" y="163" text-anchor="middle" font-size="12">k3-ddrss 드라이버</text>
  <text x="390" y="182" text-anchor="middle" font-size="12">DT의 DDR 타이밍</text>
  <line x1="470" y1="168" x2="488" y2="168" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="490" y="140" width="170" height="56" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="575" y="163" text-anchor="middle" font-size="12">A53 SPL → TF-A</text>
  <text x="575" y="182" text-anchor="middle" font-size="12">+ OP-TEE → U-Boot</text>
  <text x="20" y="238" font-size="13" font-weight="bold">QEMU virt (이 노트의 랩)</text>
  <rect x="20" y="248" width="270" height="56" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="155" y="271" text-anchor="middle" font-size="12">-bios u-boot.bin → flash 0x0</text>
  <text x="155" y="290" text-anchor="middle" font-size="12">DRAM은 처음부터 준비됨 (호스트 메모리)</text>
  <line x1="290" y1="276" x2="488" y2="276" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="390" y="268" text-anchor="middle" font-size="12">SPL·DDR 단계 없음</text>
  <rect x="490" y="248" width="170" height="56" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="575" y="271" text-anchor="middle" font-size="12">U-Boot proper</text>
  <text x="575" y="290" text-anchor="middle" font-size="12">dram_init = DT 읽기</text>
</svg>
```
그림 4 — 실물 SoC는 "ROM → SRAM의 SPL → DDR training → DRAM의 U-Boot" 순서를 거친다. QEMU는 DRAM이 처음부터 있으므로 빨간 단계가 통째로 없다.

### 6.2 i.MX8MP — SPL의 `board_init_f`와 DDR 타이밍 테이블

i.MX8MP EVK의 SPL 진입점(`board/nxp/imx8mp_evk/spl.c` 발췌, 공용 랩 소스에서 읽음):

```c
/* Do not use BSS area in this phase */
void board_init_f(ulong dummy)
{
	int ret;

	arch_cpu_init();

	ret = spl_early_init();
	if (ret) {
		debug("spl_init() failed: %d\n", ret);
		hang();
	}

	preloader_console_init();

	enable_tzc380();

	power_init_board();

	/* DDR initialization */
	spl_dram_init();
}
```

SPL에서는 보드가 `board_init_f`를 **직접 구현**한다(U-Boot proper의 `board_init_f`는 공통 코드였던 것과 대비). 주석 "Do not use BSS area in this phase"가 핵심이다 — DRAM이 아직 없으니 BSS를 SRAM에 두지 않은 이상 전역 변수를 쓸 수 없다. `spl_dram_init()`은 `ddr_init(&dram_timing)`을 부르고, `dram_timing`이 2048줄짜리 `lpddr4_timing.c`에 있다.

```c
struct dram_cfg_param ddr_ddrc_cfg[] = {
	/** Initialize DDRC registers **/
	{ 0x3d400304, 0x1 },
	{ 0x3d400030, 0x1 },
	{ 0x3d400000, 0xa3080020 },
#ifdef CONFIG_IMX8M_LPDDR4_FREQ0_2400MTS
	{ 0x3d400020, 0x223 },
	{ 0x3d400024, 0x124f800 },
	...
```

`{레지스터 주소, 값}` 쌍이 1884개다(`grep -c "{ 0x"`로 셌다). 이 파일은 사람이 쓰지 않는다. NXP의 **DDR Tool**(구 RPA 엑셀 + config tool)에 메모리 칩 데이터시트 값과 보드 배선을 넣고 stress test를 돌려 **생성**한다. 그래서 carrier board에서 LPDDR4 칩 용량이나 벤더를 바꾸면 이 파일을 새로 생성하는 것이 포팅의 첫 번째 일이다.

그리고 DDR PHY training에는 **벤더 바이너리 펌웨어**가 필요하다. PHY 안의 작은 마이크로컨트롤러가 돌리는 코드다. U-Boot는 이것을 binman 이미지 조립 단계에서 SPL 뒤에 붙인다(`arch/arm/dts/imx8mp-u-boot.dtsi` 발췌).

```dts
section {
	filename = "u-boot-spl-ddr.bin";
	...
	u-boot-spl {
		align-end = <4>;
	};

	ddr-1d-imem-fw {
		filename = "lpddr4_pmu_train_1d_imem_202006.bin";
		type = "blob-ext";
		align-end = <4>;
	};
	...
	ddr-2d-dmem-fw {
		filename = "lpddr4_pmu_train_2d_dmem_202006.bin";
		type = "blob-ext";
		align-end = <4>;
	};
};
```

`blob-ext`는 "U-Boot 트리 밖에서 가져오는 바이너리"라는 뜻이다. NXP `firmware-imx` 패키지(라이선스 동의 후 배포)에서 받아 빌드 디렉터리에 넣어야 한다. 실제로 빌드해 보면 binman이 이 파일들을 요구하는 것을 확인할 수 있다.

무엇을 확인하나: i.MX8MP EVK용 U-Boot(SPL 포함)를 빌드하면 binman이 어떤 외부 blob을 요구하는지, SPL이 얼마나 작은지. 이 VM은 arm64라 `CROSS_COMPILE` 없이 네이티브 gcc로 빌드된다. blob이 없으면 binman이 에러를 내므로 `BINMAN_ALLOW_MISSING=1`로 "가짜 blob을 넣고 계속"하게 했다(결과 이미지는 동작하지 않는다).

```sh
cd /var/tmp/C4/src
make O=/var/tmp/C4/imx imx8mp_evk_defconfig
time make O=/var/tmp/C4/imx -j6 BINMAN_ALLOW_MISSING=1 > /var/tmp/C4/imx.log 2>&1; echo rc=$?
tail -30 /var/tmp/C4/imx.log
```

```text
rc=0

real	0m31.177s
user	1m37.698s
sys	0m21.760s
  CFGS    spl/u-boot-spl.cfgout
  BINMAN  .binman_stamp
Image 'image' is missing external blobs and is non-functional: ddr-1d-imem-fw ddr-1d-dmem-fw ddr-2d-imem-fw ddr-2d-dmem-fw atf-blob

/binman/section/nxp-imx8mimage/section/ddr-1d-imem-fw (lpddr4_pmu_train_1d_imem_202006.bin):
   Missing blob

/binman/section/nxp-imx8mimage/section/ddr-1d-dmem-fw (lpddr4_pmu_train_1d_dmem_202006.bin):
   Missing blob

/binman/section/nxp-imx8mimage/section/ddr-2d-imem-fw (lpddr4_pmu_train_2d_imem_202006.bin):
   Missing blob

/binman/section/nxp-imx8mimage/section/ddr-2d-dmem-fw (lpddr4_pmu_train_2d_dmem_202006.bin):
   Missing blob

/binman/section/fit/images/atf/atf-blob (bl31.bin):
   See the documentation for your board. You may need to build ARM Trusted
   Firmware and build with BL31=/path/to/bl31.bin

Image 'image' has faked external blobs and is non-functional: lpddr4_pmu_train_1d_imem_202006.bin lpddr4_pmu_train_1d_dmem_202006.bin lpddr4_pmu_train_2d_imem_202006.bin lpddr4_pmu_train_2d_dmem_202006.bin bl31.bin

Image 'image' is missing optional external blobs but is still functional: tee-os

/binman/section/fit/images/tee/tee-os (tee.bin):
   See the documentation for your board. You may need to build Open Portable
   Trusted Execution Environment (OP-TEE) and build with TEE=/path/to/tee.bin

Some images are invalid
  OFCHK   .config
```

출력에서 볼 것: 최종 `flash.bin`에 들어가야 하는 외부 조각이 정확히 보인다 — **DDR training firmware 4개**(1D/2D training × 명령 메모리 imem / 데이터 메모리 dmem), **TF-A `bl31.bin`**(필수), **OP-TEE `tee.bin`**(선택). 실물 보드용 빌드에서는 NXP firmware 패키지의 `.bin` 4개와 직접 빌드한 TF-A를 빌드 디렉터리에 두거나 `BL31=`로 지정한다. 이 메시지 목록이 곧 "i.MX8MP 부팅 이미지의 부품 목록"이다.

산출물 크기와 SPL 메모리 배치도 보자.

```sh
cd /var/tmp/C4/imx
ls -l spl/u-boot-spl.bin u-boot-nodtb.bin u-boot.dtb flash.bin
grep -E "^CONFIG_(SPL_TEXT_BASE|TEXT_BASE|SPL_MAX_SIZE|SPL_BSS_START_ADDR|SPL_STACK|SPL_SYS_MALLOC_F_LEN)=" .config
ls -l spl/u-boot-spl.dtb u-boot.dtb
fdtget -l spl/u-boot-spl.dtb /
```

```text
-rw-rw-r-- 1 donh donh 1461592 Oct 10 10:45 flash.bin
-rw-rw-r-- 1 donh donh  122240 Oct 10 10:45 spl/u-boot-spl.bin
-rwxrwxr-x 1 donh donh 1020976 Oct 10 10:45 u-boot-nodtb.bin
-rw-rw-r-- 1 donh donh   67880 Oct 10 10:45 u-boot.dtb
CONFIG_TEXT_BASE=0x40200000
CONFIG_SPL_SYS_MALLOC_F_LEN=0x10000
CONFIG_SPL_STACK=0x960000
CONFIG_SPL_TEXT_BASE=0x920000
CONFIG_SPL_BSS_START_ADDR=0x98fc00
CONFIG_SPL_MAX_SIZE=0x26000
-rw-rw-r-- 1 donh donh  8712 Oct 10 10:45 spl/u-boot-spl.dtb
-rw-rw-r-- 1 donh donh 67880 Oct 10 10:45 u-boot.dtb
aliases
clock-osc-32k
clock-osc-24m
soc@0
chosen
regulator-usdhc2
wdt-reboot
```

출력에서 볼 것:

- SPL은 122240바이트로 `SPL_MAX_SIZE`(0x26000 = 155648) 안에 들어가야 한다. OCRAM `0x920000`에서 실행되고, 스택(`0x960000`)과 BSS(`0x98fc00`)도 전부 OCRAM 안이다. U-Boot proper(1 MB, 링크 주소 `0x40200000` = DRAM)와 비교하면 SPL이 왜 따로 있어야 하는지 숫자로 보인다.
- **SPL용 DT는 8712바이트, proper용은 67880바이트**다. 빌드 때 fdtgrep이 `bootph-*` 태그가 없는 노드를 지웠다(§8.4). 루트에 남은 것은 클럭 소스, `soc@0`, SD 전원 레귤레이터, 와치독 리셋이다. `dtc -I dtb -O dts spl/u-boot-spl.dtb`로 `soc@0` 아래를 펼쳐 보면 GPIO, 와치독, 핀먹스(그룹은 `i2c1grp`·`pmicgrp`·`uart2grp`·`usdhc2grp`·`usdhc3grp` 등 태그 붙은 것만), 클럭 컨트롤러, UART2(`serial@30890000`), CAAM 암호 엔진, I2C(PMIC `pmic@25` 포함), MMC 컨트롤러만 남아 있다 — SPL이 PMIC로 전압을 맞추고 DDR을 켠 뒤 SD/eMMC에서 다음 이미지를 읽는 데 필요한 것들이다.


### 6.3 TI AM62 — 또 다른 방식

TI K3 계열(AM62, AM64, J721E)은 구조가 다르다.

- 부팅은 **Cortex-R5 코어**가 먼저 시작한다. 같은 U-Boot 소스를 R5용(32-bit ARM)으로 한 번, A53용(64-bit)으로 한 번 빌드한다(`am62x_evm_r5_defconfig`, `am62x_evm_a53_defconfig`).
- R5 SPL이 담긴 `tiboot3.bin`에 **TIFS 펌웨어**(TI Foundational Security, 흔히 "sysfw"라 부름 — 보안·전원·클럭·리소스 관리를 맡는 별도 코어의 펌웨어)와 보드 설정 데이터가 함께 들어간다. `arch/arm/dts/k3-am625-sk-binman.dtsi`의 `ti-fs-enc.bin`, `tifs-rm-cfg.yaml` 같은 항목이 그것이다.
- DDR 초기화는 R5 SPL 안의 **`k3-ddrss` DM 드라이버**가 하고, 타이밍 값은 C 테이블이 아니라 **DT**(`k3-am62x-sk-ddr4-1600MTs.dtsi`)에 있다. TI의 SysConfig DDR 툴이 이 `.dtsi`를 생성한다.

즉 "DDR 타이밍을 툴로 생성해 넣는다"는 본질은 같고, 그릇이 C 배열이냐 DT냐가 다르다.

### 6.4 QEMU에는 왜 DDR init이 없나

QEMU `virt`의 "DRAM"은 호스트 프로세스의 메모리(`-m 512M`)일 뿐이라 컨트롤러도 training도 없다. 리셋 순간부터 0x4000_0000에 RAM이 있다. 그래서 qemu-arm 보드의 `dram_init()`은 QEMU가 만든 DT의 `memory` 노드에서 크기만 읽는다(`fdtdec_setup_mem_size_base()`). 실물 보드라면 같은 자리에서 "SPL이 이미 초기화한 DRAM의 크기"를 DT 또는 고정값, 또는 DDR 컨트롤러 레지스터로 알아낸다. QEMU로 배울 수 있는 것은 "DDR 이후"의 모든 것이고, DDR 단계는 위처럼 소스를 읽어 이해해야 한다.

---

## 7. Driver Model (DM) — U-Boot의 드라이버 프레임워크

### 7.1 세 객체

DM은 2014년쯤부터 U-Boot에 들어온 드라이버 프레임워크로, 지금은 사실상 모든 새 드라이버가 DM이다. 객체는 셋이다.

- **driver** (`struct driver`): 코드. `U_BOOT_DRIVER(name)`으로 linker list에 등록된다. `of_match`(compatible 표), `bind`, `of_to_plat`, `probe`, `remove`, `ops`, 그리고 `priv_auto`(장치마다 자동 할당할 private 구조체 크기)를 가진다.
- **uclass** (`struct uclass` + `struct uclass_driver`): 같은 종류 장치들의 **공통 API**. `UCLASS_SERIAL`, `UCLASS_MMC`, `UCLASS_I2C`, `UCLASS_GPIO`, `UCLASS_MISC` … 상위 코드는 uclass API(`misc_read()`, `dm_i2c_read()`)만 부르고, uclass가 driver의 `ops`로 넘긴다.
- **udevice** (`struct udevice`): 인스턴스. 보통 **DT 노드 하나 = udevice 하나**. driver·uclass·부모·private 데이터 포인터를 가진다.

이건 Linux의 driver model(G2/G3)과 거의 1:1이다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <text x="165" y="22" text-anchor="middle" font-size="14" font-weight="bold">U-Boot DM</text>
  <text x="515" y="22" text-anchor="middle" font-size="14" font-weight="bold">Linux driver model</text>
  <rect x="20" y="36" width="290" height="70" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="165" y="58" text-anchor="middle" font-size="13">struct driver</text>
  <text x="165" y="77" text-anchor="middle" font-size="12">U_BOOT_DRIVER(c4_widget)</text>
  <text x="165" y="95" text-anchor="middle" font-size="12">of_match, bind, probe, ops</text>
  <rect x="370" y="36" width="290" height="70" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="515" y="58" text-anchor="middle" font-size="13">struct platform_driver</text>
  <text x="515" y="77" text-anchor="middle" font-size="12">module_platform_driver()</text>
  <text x="515" y="95" text-anchor="middle" font-size="12">of_match_table, probe, remove</text>
  <line x1="310" y1="71" x2="370" y2="71" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="20" y="126" width="290" height="70" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="165" y="148" text-anchor="middle" font-size="13">uclass + uclass_driver</text>
  <text x="165" y="167" text-anchor="middle" font-size="12">UCLASS_MISC: misc_ops</text>
  <text x="165" y="185" text-anchor="middle" font-size="12">상위 API: misc_read()</text>
  <rect x="370" y="126" width="290" height="70" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="515" y="148" text-anchor="middle" font-size="13">subsystem / class</text>
  <text x="515" y="167" text-anchor="middle" font-size="12">misc, i2c, spi, net, tty 프레임워크</text>
  <text x="515" y="185" text-anchor="middle" font-size="12">상위 API: file_operations 등</text>
  <line x1="310" y1="161" x2="370" y2="161" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="20" y="216" width="290" height="70" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="165" y="238" text-anchor="middle" font-size="13">struct udevice</text>
  <text x="165" y="257" text-anchor="middle" font-size="12">DT 노드 하나 = 장치 하나</text>
  <text x="165" y="275" text-anchor="middle" font-size="12">priv_auto, plat, parent</text>
  <rect x="370" y="216" width="290" height="70" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="515" y="238" text-anchor="middle" font-size="13">struct device (platform_device)</text>
  <text x="515" y="257" text-anchor="middle" font-size="12">of_platform_populate()가 생성</text>
  <text x="515" y="275" text-anchor="middle" font-size="12">devm_kzalloc, dev_set_drvdata</text>
  <line x1="310" y1="251" x2="370" y2="251" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="20" y="306" width="290" height="96" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="165" y="328" text-anchor="middle" font-size="13">수명 주기</text>
  <text x="165" y="348" text-anchor="middle" font-size="12">bind: 부팅 때 DT 스캔으로 전부</text>
  <text x="165" y="367" text-anchor="middle" font-size="12">probe: 처음 쓰일 때 (lazy)</text>
  <text x="165" y="386" text-anchor="middle" font-size="12">인터럽트 없음, 폴링, 단일 스레드</text>
  <rect x="370" y="306" width="290" height="96" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="515" y="328" text-anchor="middle" font-size="13">수명 주기</text>
  <text x="515" y="348" text-anchor="middle" font-size="12">match 되면 즉시 probe</text>
  <text x="515" y="367" text-anchor="middle" font-size="12">의존성 미준비면 -EPROBE_DEFER</text>
  <text x="515" y="386" text-anchor="middle" font-size="12">IRQ, 동시성, 런타임 PM, hotplug</text>
</svg>
```
그림 5 — U-Boot DM과 Linux driver model의 대응. 구조는 거의 같고, 가장 큰 차이는 맨 아래 "언제 probe하나"다.

### 7.2 bind와 probe — 두 단계로 나눈 이유

- **bind**: "이 DT 노드에 이 드라이버를 짝지어 udevice를 만든다". udevice 구조체 하나를 malloc할 뿐 하드웨어 접근은 없다. 부팅 때 DT 전체를 스캔해 한꺼번에 한다(`initr_dm` → `dm_init_and_scan` → `dm_extended_scan`).
- **probe**: "하드웨어를 실제로 깨운다". 클럭 켜기, 리셋 풀기, 레지스터 초기화. **누가 그 장치를 처음 쓸 때** 한다(`uclass_get_device*()` 계열이 내부에서 `device_probe()`를 부름).

부트로더는 빨리 끝나야 하고, 부팅에 쓰지 않는 장치(HDMI, 카메라 등)는 아예 건드릴 필요가 없다. 그래서 U-Boot는 bind만 다 해 두고 probe는 미룬다. Linux는 반대로 match되면 바로 probe하고, 아직 준비되지 않은 의존성(클럭·레귤레이터)이 있으면 `-EPROBE_DEFER`로 나중에 재시도한다.

`device_probe()`(`drivers/core/device.c`)의 실제 순서는 이렇다(소스에서 순서만 뽑음).

1. `device_of_to_plat(dev)` — DT 속성을 읽어 plat/priv에 채움 (드라이버의 `of_to_plat`)
2. `device_probe(dev->parent)` — **부모 먼저** probe (I2C 장치면 I2C 컨트롤러가 먼저)
3. power domain 켜기, `pinctrl_select_state(dev, "default")` — DT의 `pinctrl-0` 적용
4. `uclass_pre_probe_device()`, 부모의 `child_pre_probe`
5. `clk_set_defaults()` — DT의 `assigned-clocks`/`assigned-clock-rates` 적용
6. **드라이버의 `probe()`**
7. `uclass_post_probe_device()`

2·3·5는 Linux의 `really_probe()`가 하는 일(pinctrl default 상태 적용, 부모 우선)과 같다. 드라이버가 직접 하지 않아도 프레임워크가 DT를 보고 해 준다는 점이 핵심이다.

### 7.3 실습 3 — UCLASS_MISC 드라이버 `c4_widget`

`drivers/misc/c4_widget.c` (복사한 트리에 추가):

```c
// SPDX-License-Identifier: GPL-2.0+
/* C4 study: a tiny UCLASS_MISC driver bound from a DT node */
#include <dm.h>
#include <misc.h>
#include <asm/global_data.h>
#include <asm/io.h>

DECLARE_GLOBAL_DATA_PTR;

struct c4_widget_priv {
	void __iomem *base;
	const char *label;
	u32 scale;
};

static const char *stage(void)
{
	return (gd->flags & GD_FLG_RELOC) ? "post-reloc" : "pre-reloc";
}

static int c4_widget_bind(struct udevice *dev)
{
	printf("c4_widget: bind  %s (%s)\n", dev->name, stage());
	return 0;
}

static int c4_widget_of_to_plat(struct udevice *dev)
{
	struct c4_widget_priv *priv = dev_get_priv(dev);

	priv->base = dev_read_addr_ptr(dev);
	if (!priv->base)
		return -EINVAL;
	priv->label = dev_read_string(dev, "c4,label");
	priv->scale = dev_read_u32_default(dev, "c4,scale", 1);
	return 0;
}

static int c4_widget_probe(struct udevice *dev)
{
	struct c4_widget_priv *priv = dev_get_priv(dev);

	printf("c4_widget: probe %s label=%s scale=%u base=%p (%s)\n",
	       dev->name, priv->label ? priv->label : "-", priv->scale,
	       priv->base, stage());
	return 0;
}

/* misc_read(): offset = register offset, returns scaled 32-bit value */
static int c4_widget_read(struct udevice *dev, int offset, void *buf, int size)
{
	struct c4_widget_priv *priv = dev_get_priv(dev);
	u32 v;

	if (size != sizeof(u32))
		return -EINVAL;
	v = readl(priv->base + offset) * priv->scale;
	memcpy(buf, &v, sizeof(v));
	return size;
}

static const struct misc_ops c4_widget_ops = {
	.read = c4_widget_read,
};

static const struct udevice_id c4_widget_ids[] = {
	{ .compatible = "c4,widget" },
	{ }
};

U_BOOT_DRIVER(c4_widget) = {
	.name		= "c4_widget",
	.id		= UCLASS_MISC,
	.of_match	= c4_widget_ids,
	.bind		= c4_widget_bind,
	.of_to_plat	= c4_widget_of_to_plat,
	.probe		= c4_widget_probe,
	.priv_auto	= sizeof(struct c4_widget_priv),
	.ops		= &c4_widget_ops,
};
```

Linux 드라이버와 비교하면: `of_match` ↔ `of_match_table`, `priv_auto` ↔ `devm_kzalloc` + `dev_set_drvdata`, `dev_read_addr_ptr` ↔ `devm_platform_ioremap_resource`, `dev_read_u32_default` ↔ `device_property_read_u32` / `of_property_read_u32`, `misc_ops.read` ↔ `file_operations.read`. U-Boot는 MMU를 1:1(물리 = 가상)로 매핑하므로 `ioremap`이 필요 없다(§1.2의 `mem_map`).

Kconfig·Makefile 추가분:

```text
config C4_WIDGET
	bool "C4 study widget (UCLASS_MISC)"
	depends on MISC
	help
	  Tiny driver-model example bound from a "c4,widget" DT node.
```

```make
# drivers/misc/Makefile 끝에 추가
obj-$(CONFIG_C4_WIDGET) += c4_widget.o
```

### 7.4 DT 노드 추가 — qemu_arm64의 control DT는 QEMU가 만든다

드라이버를 붙이려면 DT 노드가 있어야 한다. 그런데 qemu_arm64는 특이하다: `.config`에 `CONFIG_OF_BOARD=y`, `CONFIG_OF_OMIT_DTB=y`가 켜져 있다. U-Boot가 쓰는 DT(**control DT**)를 빌드 때 만들지 않고, **QEMU가 런타임에 생성해 RAM 시작(0x4000_0000)에 놓은 DT**를 쓴다(§1.2의 `board_fdt_blob_setup`). 그래서 노드를 추가하려면 QEMU에 `-dtb`로 수정한 DT를 넘긴다. 공용 랩의 `virt.dts`(QEMU가 만든 DT를 덤프한 것)에 노드 두 개를 붙였다.

```dts
/* C4: appended to QEMU's virt.dts -> c4_virt.dtb (passed with -dtb) */
/ {
	c4_rtc@9010000 {
		compatible = "c4,widget";
		reg = <0x0 0x09010000 0x0 0x1000>;   /* PL031 RTC registers */
		c4,label = "rtc-seconds";
		c4,scale = <1>;
		bootph-all;                          /* bind before relocation too */
	};
	c4_uart@9000000 {
		compatible = "c4,widget";
		reg = <0x0 0x09000000 0x0 0x1000>;   /* PL011 UART registers */
		c4,label = "uart-id";
		c4,scale = <1>;
	};
};
```

dts 파일은 루트 노드 `/ { };` 블록을 여러 번 써도 dtc가 하나로 합친다. 실습용이라 실제 RTC·UART 레지스터 블록을 **읽기 전용으로 빌려 쓴다**. 하나에만 `bootph-all`을 붙여 pre-reloc 차이를 본다.

무엇을 확인하나: DT가 컴파일되고 두 노드가 루트 아래 생겼는지.

```sh
cd /var/tmp/C4
cat /var/lab/out/virt.dts /Users/donh/.elinux_scratch/C4/c4_nodes.dtsi > c4_virt.dts
dtc -I dts -O dtb -o c4_virt.dtb c4_virt.dts 2>&1 | grep -c Warning
dtc -I dts -O dtb -o c4_virt.dtb c4_virt.dts 2>&1 | grep unique_unit_address
fdtget -l c4_virt.dtb / | grep c4
```

```text
7
c4_virt.dts:300.16-306.4: Warning (unique_unit_address): /pl031@9010000: duplicate unit-address (also used in node /c4_rtc@9010000)
c4_virt.dts:308.16-314.4: Warning (unique_unit_address): /pl011@9000000: duplicate unit-address (also used in node /c4_uart@9000000)
c4_rtc@9010000
c4_uart@9000000
```

출력에서 볼 것: 경고 7개 중 2개가 **같은 주소에 노드가 두 개**라는 `unique_unit_address`다 — 여기서는 의도한 것이지만, 실제 보드 DT에서 이 경고가 나오면 대개 복사·붙여넣기 실수다. 나머지 5개는 `clocks_property`·`gpios_property`("cell 0 is not a phandle reference")로, QEMU DT를 dts로 덤프하면서 phandle이 숫자로 풀려 생기는 것이라 원래 `virt.dts`에서도 똑같이 나온다. dtb는 정상 생성됐고 두 노드가 루트 아래 있다.

### 7.5 부팅 — bind 로그와 `dm tree`

무엇을 확인하나: 두 노드가 언제 bind되고, `dm tree`에서 어떻게 보이며, probe는 언제 일어나는지.

```sh
# §4.4와 같은 실행 — 부팅 메시지와 dm tree 부분
```

```text
c4_widget: bind  c4_rtc@9010000 (pre-reloc)


U-Boot 2026.10 (Oct 10 2026 - 10:27:07 -0700)

DRAM:  512 MiB
using memory 0x5e480000-0x5f4c0000 for malloc()
c4_widget: bind  c4_rtc@9010000 (post-reloc)
c4_widget: bind  c4_uart@9000000 (post-reloc)
Core:  53 devices, 15 uclasses, devicetree: board
...
=> dm tree
 Class     Seq    Probed  Driver                Name
-----------------------------------------------------------
 root          0  [ + ]   root_driver           root_driver
 rsa_mod_ex    0  [   ]   mod_exp_sw            |-- mod_exp_sw
 firmware      0  [   ]   psci                  |-- psci
 sysreset      0  [   ]   psci-sysreset         |   `-- psci-sysreset
 simple_bus    0  [   ]   simple_bus            |-- platform-bus@c000000
 qfw           0  [   ]   qfw_mmio              |-- fw-cfg@9020000
 bootdev       0  [   ]   qfw_bootdev           |   `-- fw-cfg@9020000.bootdev
 virtio        0  [   ]   virtio-mmio           |-- virtio_mmio@a000000
 virtio        1  [   ]   virtio-mmio           |-- virtio_mmio@a000200
...
 virtio       31  [   ]   virtio-mmio           |-- virtio_mmio@a003e00
 pci           0  [ + ]   pci_generic_ecam      |-- pcie@10000000
 pci_generi    0  [   ]   pci_generic_drv       |   |-- pci_0:0.0
 virtio       32  [ + ]   virtio-pci.l          |   `-- virtio-pci.l#0
 ethernet      0  [ + ]   virtio-net            |       `-- virtio-net#32
 bootdev       1  [   ]   eth_bootdev           |           `-- virtio-net#32.bootdev
 rtc           0  [   ]   rtc-pl031             |-- pl031@9010000
 serial        0  [ + ]   serial_pl01x          |-- pl011@9000000
 mtd           0  [ + ]   cfi_flash             |-- flash@0
 misc          0  [   ]   c4_widget             |-- c4_rtc@9010000
 misc          1  [   ]   c4_widget             |-- c4_uart@9000000
 bootstd       0  [   ]   bootstd_drv           |-- bootstd
 bootmeth      0  [   ]   bootmeth_extlinux     |   |-- extlinux
...
 bootmeth      6  [   ]   vbe_simple            |   `-- vbe_simple
 efi           0  [   ]   EFI block driver      `-- efi
```

(virtio_mmio 32개와 bootmeth 일부는 `...`로 생략했다.)

출력에서 볼 것:

- **`Probed [ + ]`인 장치는 몇 개뿐이다**: root, PCI, virtio-net(네트워크 init에서 썼다), 시리얼(콘솔), flash(env 읽기). 내 `c4_widget` 두 개는 bind만 되고 **`[   ]` — 아직 probe 안 됨**. `rtc-pl031`도 아무도 시간을 묻지 않아서 probe 안 됐다. 이것이 lazy probe다.
- `c4_rtc`는 **pre-reloc에 한 번, post-reloc에 또 한 번** bind됐다. pre-reloc DM 트리는 relocation 후 버려지고 처음부터 다시 만든다(§8.4).
- `c4_uart`(태그 없음)는 post-reloc에만 bind.
- 트리 구조: 두 노드는 DT 루트 바로 아래에 있으므로 `root_driver`의 자식이다. I2C 센서 노드라면 I2C 컨트롤러 udevice의 자식으로 보인다 — DT 계층이 그대로 DM 계층이 된다.

### 7.6 probe 시키고 uclass API로 읽기

`misc` 명령은 `UCLASS_MISC` 장치에 대해 `misc_read()`를 부른다. 이 호출이 첫 사용이 되어 probe가 일어난다.

무엇을 확인하나: probe 시점, `misc_read()`가 드라이버 `.read`로 가는 경로, 레지스터 값.

```sh
expect /Users/donh/.elinux_scratch/C4/run.exp build/u-boot.bin c4_virt.dtb \
  "misc list" "misc read c4_rtc@9010000 0 0x50000000 4" "md.l 0x50000000 1" \
  "misc read c4_uart@9000000 0xfe0 0x50000010 4" "md.l 0x50000010 1" "date" \
  "dm uclass" "dm drivers" "dm devres"
```

(부팅 메시지는 §5와 같아서 생략. `dm uclass`·`dm drivers`는 §7.7에서 본다.)

```text
=> misc list
Device               Index     Driver
-------------------------------------
c4_widget: probe c4_rtc@9010000 label=rtc-seconds scale=1 base=0000000009010000 (post-reloc)
c4_rtc@9010000           0  c4_widget
c4_widget: probe c4_uart@9000000 label=uart-id scale=1 base=0000000009000000 (post-reloc)
c4_uart@9000000          1  c4_widget
=> misc read c4_rtc@9010000 0 0x50000000 4
=> md.l 0x50000000 1
50000000: 6aca75e0                             .u.j
=> misc read c4_uart@9000000 0xfe0 0x50000010 4
=> md.l 0x50000010 1
50000010: 00000011                             ....
=> date
Date: 2026-10-10 (Saturday)    Time: 17:29:04
```

출력에서 볼 것:

- `misc list`가 `uclass_first_device()`/`uclass_next_device()`로 uclass를 순회하면서 probe를 일으켰다(이 두 함수는 장치를 돌려주기 전에 probe한다). probe 로그가 목록 줄 **바로 앞**에 찍힌다. `of_to_plat`이 DT에서 읽은 `label`·`scale`·`base`가 probe에서 보인다.
- PL031 RTC의 데이터 레지스터(offset 0)는 1970년 이후 초 수다. `0x6aca75e0`을 파이썬으로 바꾸면 `2026-10-10 17:29:04`(UTC) — 바로 다음 `date` 명령(진짜 `rtc-pl031` 드라이버)의 출력과 일치한다. 같은 하드웨어를 두 드라이버가 읽은 셈이다.
- UART offset 0xFE0은 PeriphID0 = `0x11`. §4의 `c4hello`가 읽은 값의 최하위 바이트와 같다.

### 7.7 `dm uclass`와 `dm drivers`

같은 실행에서 `dm uclass`와 `dm drivers`를 쳤다(관련 줄만 남기고 생략).

```text
=> dm uclass
uclass 0: root
0   * root_driver @ 5e4a0170, seq 0
...
uclass 76: misc
0   * c4_rtc@9010000 @ 5e4a2410, seq 0
1   * c4_uart@9000000 @ 5e4a24b0, seq 1
...
uclass 109: rtc
0   * pl031@9010000 @ 5e4a2130, seq 0

uclass 113: serial
0   * pl011@9000000 @ 5e4a2230, seq 0
...
=> dm drivers
Driver                    uid uclass               Devices
----------------------------------------------------------
ahci_pci                  020 ahci                 <none>
...
c4_widget                 076 misc                 c4_rtc@9010000           
                                                   c4_uart@9000000          
cfi_flash                 079 mtd                  flash@0                  
...
psci                      047 firmware             psci                     
rtc-pl031                 109 rtc                  pl031@9010000            
serial_pl01x              113 serial               pl011@9000000            
...
```

출력에서 볼 것: `dm uclass`의 `*`는 probe됨 표시다 — `misc list`와 `date`가 지나간 뒤라 c4 장치 둘과 rtc가 이제 `*`다. `@ 5e4a2410`은 udevice 구조체의 주소로, relocation 후 malloc 영역(`0x5e480000`~) 안에 있다. `dm drivers`는 **링크된 모든 드라이버**(linker list) 중 어떤 것이 장치를 가졌는지 보여 준다. `<none>`인 드라이버(`ahci_pci` 등)는 바이너리 크기만 차지하고 있는 것이니, 생산 보드 defconfig에서 끌 후보다(§9).

| 명령 | 질문 | Linux에서 비슷한 것 |
|---|---|---|
| `dm tree` | 장치 계층과 probe 여부 | `/sys/devices/` 트리, `ls /sys/bus/platform/devices` |
| `dm uclass` | 종류별로 어떤 장치가 있나 | `/sys/class/<class>/` |
| `dm drivers` | 링크된 드라이버와 붙은 장치 | `/sys/bus/platform/drivers/<drv>/` |
| `dm compat` | 드라이버별 compatible 문자열 | `modinfo -F alias` |

### 7.8 U-Boot 전용 DT 조각 — `<board>-u-boot.dtsi`

실물 보드의 control DT는 **Linux와 같은 `.dts`** 를 쓰는 것이 원칙이다(요즘은 `dts/upstream/`에 Linux DT를 미러링하고 `CONFIG_OF_UPSTREAM`으로 그걸 쓴다. i.MX8MP EVK의 defconfig가 `CONFIG_DEFAULT_DEVICE_TREE="freescale/imx8mp-evk"`인 것이 그 형태다). U-Boot만 필요한 속성(`bootph-*`, binman 이미지 레이아웃, U-Boot 전용 노드)은 Linux DT를 더럽히지 않도록 **`arch/arm/dts/<board>-u-boot.dtsi`** 에 따로 두고, 빌드 시스템이 자동으로 `#include`한다.

qemu_arm64는 런타임 DT를 쓰지만, 빌드 시스템은 여전히 `qemu-arm64.dts`를 컴파일하므로 이 자동 include 동작을 확인할 수 있다. 복사한 트리에 `arch/arm/dts/qemu-arm64-u-boot.dtsi`를 만들었다.

```dts
// C4: auto-included by U-Boot's dtc rule for qemu-arm64.dts
/ {
	c4-build-marker {
		compatible = "c4,marker";
		bootph-all;
	};
};
```

무엇을 확인하나: 이 파일을 어디서도 include하지 않았는데 빌드된 dtb에 노드가 들어갔는지, 그리고 그 메커니즘.

```sh
cd /var/tmp/C4/build
fdtget -l arch/arm/dts/qemu-arm64.dtb /
fdtget -p arch/arm/dts/qemu-arm64.dtb /c4-build-marker
fdtget -l /var/tmp/C4/base/arch/arm/dts/qemu-arm64.dtb /
head -c 330 arch/arm/dts/.qemu-arm64.dtb.cmd; echo
```

```text
smbios
c4-build-marker
compatible
bootph-all
smbios
cmd_arch/arm/dts/qemu-arm64.dtb := mkdir -p arch/arm/dts/ ; (cat /var/tmp/C4/src/arch/arm/dts/qemu-arm64.dts > arch/arm/dts/.qemu-arm64.dtb.pre.tmp);  echo '$(pound)include "qemu-arm64-u-boot.dtsi"' >> arch/arm/dts/.qemu-arm64.dtb.pre.tmp; cc -E
```

출력에서 볼 것: 기본 빌드(base)의 dtb에는 `smbios`만, C4 빌드에는 `c4-build-marker`가 추가됐다. 빌드 명령(`.cmd` 파일)을 보면 Kbuild가 `.dts`를 임시 파일로 복사하고 **끝에 `#include "qemu-arm64-u-boot.dtsi"`를 덧붙인 뒤** cpp → dtc를 돌린다. 어떤 파일을 찾는지는 `scripts/Makefile.lib`의 `u_boot_dtsi_options`에 있다: `<dts이름>-u-boot.dtsi`, `<SYS_SOC>-u-boot.dtsi`, `<SYS_CPU>-u-boot.dtsi`, `<SYS_VENDOR>-u-boot.dtsi`, `u-boot.dtsi` 순서로 후보를 보고 처음 존재하는 것을 쓴다. 다만 qemu_arm64에서는 `OF_OMIT_DTB` 때문에 이 dtb가 `u-boot.bin`에 **붙지 않는다** — `u-boot.bin`과 `u-boot-nodtb.bin`이 1204392바이트로 같다. 실물 보드(`OF_SEPARATE`)라면 `u-boot.bin = u-boot-nodtb.bin + u-boot.dtb`가 된다.

| DT 공급 방식 | Kconfig | 쓰는 곳 |
|---|---|---|
| 빌드한 dtb를 바이너리 끝에 붙임 | `OF_SEPARATE` | 대부분의 실물 보드 |
| 바이너리 안에 링크 | `OF_EMBED` | 디버깅용, 비권장 |
| 이전 단계가 런타임에 줌 | `OF_BOARD` (+ `OF_HAS_PRIOR_STAGE`) | QEMU, TF-A/코어부트가 DT를 넘기는 플랫폼, Raspberry Pi |
| Linux DT 미러 사용 | `OF_UPSTREAM` | i.MX8M, 최근 TI·Rockchip 보드 |

---

## 8. Pre-relocation과 relocation

### 8.1 직관 — 부트로더가 자기를 옮기는 이유

Don의 SSD 부트 코드는 ROM이나 SPI flash에서 시작해 자기 자신(또는 다음 단계)을 SRAM/DRAM으로 복사한 뒤 거기로 점프했다. U-Boot도 같다.

1. 처음엔 **링크된 주소(`CONFIG_TEXT_BASE`)** 에서 돈다. qemu_arm64는 `TEXT_BASE=0` — 즉 **flash(0x0)에서 바로 실행**(XIP)한다. 실물 보드에서는 SPL이 DRAM의 어떤 고정 주소(i.MX8MP: `0x40200000`)에 올려 준 곳이다.
2. `board_init_f`가 DRAM 크기를 알아낸 뒤, **DRAM 꼭대기부터 아래로** 필요한 영역(MMU 테이블, 프레임버퍼, U-Boot 자신, malloc, global data, DT 복사본, 스택)을 예약한다(`reserve_*` INITCALL들).
3. `relocate_code()`가 U-Boot를 그 자리로 **복사**하고, 코드 안의 절대 주소들을 고친 뒤 점프한다.
4. `board_init_r`부터는 새 위치에서 돈다.

왜 꼭대기로 가나? DRAM 아래쪽(0x4000_0000~)은 커널·initramfs·DT를 로드할 자리(`kernel_addr_r=0x40400000`, `ramdisk_addr_r=0x44000000`)라 비워 둬야 하고, 어느 보드든 "RAM 끝"은 런타임에 알 수 있으니 같은 바이너리가 RAM 크기가 다른 보드에서도 동작한다.

### 8.2 메모리 지도 — 실측

무엇을 확인하나: relocation 후 어디에 무엇이 있는지 (`bdinfo`).

```sh
expect /Users/donh/.elinux_scratch/C4/run.exp build/u-boot.bin c4_virt.dtb "bdinfo" "fdt addr" "printenv fdt_addr fdtcontroladdr"
```

```text
=> bdinfo
boot_params = 0x0000000000000000
DRAM bank   = 0x0000000000000000
-> start    = 0x0000000040000000
-> size     = 0x0000000020000000
flashstart  = 0x0000000000000000
flashsize   = 0x0000000004000000
flashoffset = 0x00000000001260a8
baudrate    = 115200 bps
relocaddr   = 0x000000005f4c0000
reloc off   = 0x000000005f4c0000
Build       = 64-bit
current eth = virtio-net#32
ethaddr     = 52:54:00:12:34:56
IP addr     = <NULL>
fdt_blob    = 0x000000005e477220
lmb_dump_all:
 memory.count = 0x1
 memory[0]	[0x40000000-0x5fffffff], 0x20000000 bytes, flags: none
 reserved.count = 0x2
 reserved[0]	[0x5d456ff0-0x5fdfffff], 0x29a9010 bytes, flags: no-overwrite
 reserved[1]	[0x5fffd000-0x5fffffff], 0x3000 bytes, flags: no-notify, no-overwrite
devicetree  = board
serial addr = 0x0000000009000000
...
TLB addr    = 0x000000005fde0000
irq_sp      = 0x000000005e456ff0
sp start    = 0x000000005e456ff0
Early malloc usage: f50 / 2000
=> fdt addr
=> printenv fdt_addr fdtcontroladdr
fdt_addr=0x40000000
fdtcontroladdr=5e477220
```

(serial 세부 필드 몇 줄은 `...`로 생략했다. `fdt addr`는 아직 작업 DT를 지정하지 않아 아무것도 출력하지 않았다.)

출력에서 볼 것:

- `relocaddr = 0x5f4c0000`, `reloc off = 0x5f4c0000`: 링크 주소가 0이었으므로 오프셋 = 목적지 주소.
- `TLB addr = 0x5fde0000`: MMU 페이지 테이블이 맨 위쪽.
- `relocaddr`(0x5f4c0000)와 TLB(0x5fde0000) 사이는 0x920000이다. U-Boot 이미지+BSS(`__bss_end` = 0x11fa18, 4 KiB 정렬하면 0x120000)를 빼면 정확히 0x800000 = `CONFIG_VIDEO_PCI_DEFAULT_FB_SIZE`(8 MiB 프레임버퍼 예약)가 남는다. `reserve_video`가 `reserve_uboot`보다 먼저 불리는 순서와 맞는다(계산으로 맞춘 배치다).
- malloc `0x5e480000-0x5f4c0000` = 0x1040000 = `SYS_MALLOC_LEN`(16 MiB) + `ENV_SIZE`(256 KiB).
- `fdt_blob = 0x5e477220`: control DT를 RAM 시작(0x40000000)에서 **복사해 온** 위치. `fdt_addr=0x40000000`은 커널에 넘길 원본 자리.
- `Early malloc usage: f50 / 2000`: pre-reloc 단계의 malloc(`SYS_MALLOC_F_LEN = 0x2000` = 8 KiB) 중 3920바이트를 썼다. pre-reloc에서 bind하는 장치가 많아지면 여기가 넘친다(§10의 함정).

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="250" y="20" text-anchor="middle" font-size="13" font-weight="bold">물리 주소 (위가 높은 주소, 크기 비례 아님)</text>
  <text x="130" y="48" text-anchor="end" font-size="12">0x6000_0000</text>
  <rect x="140" y="36" width="230" height="28" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="255" y="55" text-anchor="middle" font-size="12">예약 3 페이지 + 여유</text>
  <text x="130" y="80" text-anchor="end" font-size="12">0x5fde_0000</text>
  <rect x="140" y="66" width="230" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="255" y="84" text-anchor="middle" font-size="12">TLB (MMU 페이지 테이블)</text>
  <rect x="140" y="94" width="230" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="255" y="112" text-anchor="middle" font-size="12">video FB 8 MiB</text>
  <text x="130" y="140" text-anchor="end" font-size="12">0x5f4c_0000</text>
  <rect x="140" y="122" width="230" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="255" y="144" text-anchor="middle" font-size="12">U-Boot (relocated) + BSS</text>
  <text x="130" y="175" text-anchor="end" font-size="12">0x5e48_0000</text>
  <rect x="140" y="158" width="230" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="255" y="178" text-anchor="middle" font-size="12">malloc 16 MiB + env</text>
  <text x="130" y="206" text-anchor="end" font-size="12">0x5e47_7220</text>
  <rect x="140" y="190" width="230" height="26" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="255" y="208" text-anchor="middle" font-size="12">bd, gd, control DT 복사본</text>
  <text x="130" y="236" text-anchor="end" font-size="12">0x5e45_6ff0</text>
  <rect x="140" y="218" width="230" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="255" y="238" text-anchor="middle" font-size="12">스택 (아래로 자람)</text>
  <rect x="140" y="250" width="230" height="40" fill="none" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="255" y="275" text-anchor="middle" font-size="12">비어 있음: 커널·initrd 로드 자리</text>
  <text x="130" y="310" text-anchor="end" font-size="12">0x4000_0000</text>
  <rect x="140" y="292" width="230" height="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="255" y="311" text-anchor="middle" font-size="12">QEMU DTB (fdt_addr)</text>
  <text x="130" y="346" text-anchor="end" font-size="12">0x0900_0000</text>
  <rect x="140" y="332" width="230" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="255" y="350" text-anchor="middle" font-size="12">MMIO: UART, RTC, GIC …</text>
  <text x="130" y="384" text-anchor="end" font-size="12">0x0400_0000</text>
  <rect x="140" y="370" width="230" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="255" y="388" text-anchor="middle" font-size="12">flash 뱅크 1 (ENV_ADDR)</text>
  <text x="130" y="424" text-anchor="end" font-size="12">0x0000_0000</text>
  <rect x="140" y="406" width="230" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="255" y="424" text-anchor="middle" font-size="12">u-boot.bin (flash, XIP)</text>
  <text x="255" y="440" text-anchor="middle" font-size="12">TEXT_BASE = 0, pre-reloc 실행</text>
  <path d="M370,420 C470,420 470,140 372,140" fill="none" stroke="#d0564a" stroke-width="1.5" marker-end="url(#ah5)"/>
  <text x="470" y="270" font-size="12">relocate_code()</text>
  <text x="470" y="289" font-size="12">복사 + 3833개 수정</text>
  <text x="470" y="308" font-size="12">reloc_off = 0x5f4c0000</text>
</svg>
```
그림 6 — qemu_arm64 U-Boot의 relocation 전후 메모리 지도(`bdinfo` 실측 주소). flash 0x0에서 시작한 U-Boot가 DRAM 꼭대기 근처로 자신을 옮기고, 그 아래에 malloc·gd·DT 복사본·스택을 둔다. DRAM 아래쪽은 커널을 위해 비워 둔다.

### 8.3 relocation은 어떻게 주소를 고치나 — PIE와 `R_AARCH64_RELATIVE`

U-Boot ARM64는 **position-independent**로 링크된다(`-pie`). 대부분의 코드는 PC-상대 주소(`adrp`/`add`)로 동작하므로 옮겨도 그대로 돈다. 문제는 **데이터 안에 박힌 절대 주소** — 함수 포인터 테이블(`U_BOOT_CMD`의 `cmd`, `U_BOOT_DRIVER`의 `probe`/`ops`), 문자열 포인터 등이다. 링커는 이런 위치를 `.rela.dyn` 섹션에 목록으로 남긴다.

무엇을 확인하나: 수정해야 할 위치가 몇 개이고 어떤 종류인지, ELF 섹션 배치.

```sh
cd /var/tmp/C4/build
readelf -SW u-boot | sed -n 5,16p
readelf -rW u-boot | awk 'NR>3{print $3}' | sort | uniq -c
nm u-boot | grep -i "__rel_dyn_start\|__rel_dyn_end\|__image_copy_end\|__bss_start$\|__bss_end$"
```

```text
  [ 0]                   NULL            0000000000000000 000000 000000 00      0   0  0
  [ 1] .text             PROGBITS        0000000000000000 001000 000188 00  AX  0   0  8
  [ 2] .efi_runtime      PROGBITS        0000000000000188 001188 000f90 00 WAX  0   0  8
  [ 3] .text_rest        PROGBITS        0000000000001800 002800 0c8228 00  AX  0   0 2048
  [ 4] .efi_runtime_rel  RELA            00000000000c9a28 0caa28 0001b0 18   A  0   0  8
  [ 5] .rodata           PROGBITS        00000000000c9bd8 0cabd8 038e80 00   A  0   0  8
  [ 6] __u_boot_list     PROGBITS        0000000000102a58 103a58 006178 00  WA  0   0  8
  [ 7] .data             PROGBITS        0000000000108bd0 109bd0 006f10 00  WA  0   0  8
  [ 8] .got              PROGBITS        000000000010fae0 110ae0 000008 08  WA  0   0  8
  [ 9] .got.plt          PROGBITS        000000000010fae8 110ae8 000018 08  WA  0   0  8
  [10] .rela.dyn         RELA            000000000010fb00 110b00 0165a8 18   A  0   0  8
  [11] .bss              PROGBITS        000000000010fb00 127100 00ff18 00   W  0   0 128
      1 
      1 '.rela.dyn'
   3833 R_AARCH64_RELATIVE
      1 Type
000000000011fa18 ? __bss_end
000000000010fb00 ? __bss_start
000000000010fb00 R __image_copy_end
00000000001260a8 R __rel_dyn_end
000000000010fb00 R __rel_dyn_start
```

출력에서 볼 것:

- 재배치 항목이 **전부 `R_AARCH64_RELATIVE`** 3833개다(`.rela.dyn`의 0x165a8/0x18 = 3815개 + `.efi_runtime_rel`의 18개). RELATIVE 하나는 "이 위치에 (addend + 새 base)를 써라"라는 뜻이라, 심볼 테이블 없이 덧셈 하나로 처리된다.
- **`.bss`와 `.rela.dyn`이 같은 주소(0x10fb00)에서 시작한다**. 복사 범위는 `__image_copy_end`(= 0x10fb00)까지라 `.rela.dyn`은 새 위치로 복사되지 않는다. 원래 자리에서 읽기만 하고 버리며, 새 위치에서는 그 자리가 BSS가 된다. 공간을 아끼는 트릭이다. `u-boot.bin`(1204392 = 0x1260a8바이트)이 `__rel_dyn_end`에서 끝나는 것도 이 때문이고, `bdinfo`의 `flashoffset = 0x1260a8`이 바로 그 값이다.

수정 루프는 `arch/arm/lib/relocate_64.S`에 있다(발췌).

```text
copy_loop:
	ldp	x10, x11, [x1], #16	/* copy from source address [x1] */
	stp	x10, x11, [x0], #16	/* copy to   target address [x0] */
	cmp	x1, x2			/* until source end address [x2] */
	b.lo	copy_loop
	...
fixloop:
	ldp	x0, x1, [x2], #16	/* (x0,x1) <- (SRC location, fixup) */
	ldr	x4, [x2], #8		/* x4 <- addend */
	and	x1, x1, #0xffffffff
	cmp	x1, #R_AARCH64_RELATIVE
	bne	fixnext

	/* relative fix: store addend plus offset at dest location */
	add	x0, x0, x9
	add	x4, x4, x9
	str	x4, [x0]
fixnext:
	cmp	x2, x3
	b.lo	fixloop
```

Don에게는 익숙한 코드다: 16바이트씩 `ldp`/`stp`로 복사하고, 24바이트짜리 `Elf64_Rela`(offset, info, addend)를 순회하며 `*(new_base + offset) = addend + new_base`를 쓴다. 동적 로더(`ld.so`)가 공유 라이브러리를 로드할 때 하는 일(B3)의 가장 단순한 버전이다.

### 8.4 `bootph-*` 태그 — pre-relocation에 어떤 장치를 살릴까

pre-reloc 단계에서도 DM은 돈다(`initf_dm`). 콘솔 UART, SPL의 MMC 컨트롤러, PMIC용 I2C 같은 장치는 relocation 전에 필요하기 때문이다. 하지만 메모리가 8 KiB malloc뿐이고(`SYS_MALLOC_F_LEN`) 시간도 아까우니 **필요한 노드만** bind한다. 어떤 노드가 필요한지 DT에 태그로 표시한다.

| 태그 | 의미 (U-Boot proper 기준 동작) |
|---|---|
| `bootph-all` | 모든 단계(TPL, SPL, proper의 pre-reloc, post-reloc)에서 bind |
| `bootph-pre-ram` | SPL(DRAM 설정 단계)에 포함 |
| `bootph-pre-sram` | TPL(SRAM도 없는 단계)에 포함 |
| `bootph-some-ram` | proper의 pre-reloc 단계에 포함 (RAM은 있지만 아직 일부만) |
| `bootph-verify` | VPL에 포함 |
| (태그 없음) | 마지막 단계(post-reloc)에서만 |

`u-boot,dm-pre-reloc`, `u-boot,dm-spl`, `u-boot,dm-tpl`이 옛 이름이다. U-Boot 문서(`doc/develop/spl.rst`)에 "2023년에 `bootph-*`로 바뀌었다"고 적혀 있고, 이 트리의 C·Python 코드에서는 옛 이름을 찾는 곳이 없다(`grep -rln "u-boot,dm-pre-reloc" --include=*.c --include=*.py` 결과 없음). 옛 벤더 BSP를 최신 U-Boot로 옮길 때 **이 이름을 안 바꾸면 SPL에서 UART가 사라져 아무 것도 안 찍힌다**.

실제 판정 코드(`drivers/core/ofnode.c`, U-Boot proper 빌드 경로):

```c
bool ofnode_pre_reloc(ofnode node)
{
	...
	if (ofnode_read_bool(node, "bootph-all"))
		return true;
	if (ofnode_read_bool(node, "bootph-some-ram"))
		return true;

	/*
	 * In regular builds individual spl and tpl handling both
	 * count as handled pre-relocation for later second init.
	 */
	if (ofnode_read_bool(node, "bootph-pre-ram") ||
	    ofnode_read_bool(node, "bootph-pre-sram"))
		return gd->flags & GD_FLG_RELOC;

	return false;
}
```

그리고 `drivers/core/lists.c`에서 pre-reloc 스캔 중에는 `!ofnode_pre_reloc(node) && !(entry->flags & DM_FLAG_PRE_RELOC)`이면 건너뛴다. 즉 **노드 쪽 태그 또는 드라이버 쪽 `DM_FLAG_PRE_RELOC` 플래그** 둘 중 하나면 pre-reloc에 bind된다.

§5·§7.5의 부팅 로그가 이것을 그대로 보여 줬다.

```text
c4_widget: bind  c4_rtc@9010000 (pre-reloc)
...
c4_widget: bind  c4_rtc@9010000 (post-reloc)
c4_widget: bind  c4_uart@9000000 (post-reloc)
```

`bootph-all`이 있는 `c4_rtc`만 pre-reloc에 bind됐다. 그 줄이 **U-Boot 배너보다 먼저** 보이는 것은, 배너를 찍는 `display_options`가 `initf_dm` 뒤에 오기 때문이다(§3.2 순서). 콘솔 초기화(`serial_init`, `console_init_f`) 전의 `printf`인데도 보이는 이유는 이 defconfig가 `CONFIG_DEBUG_UART=y`(PL011 `0x9000000`)를 켜 두었기 때문이다. `common/console.c`의 `putc()`에 "if we don't have a console yet, use the debug UART" 분기가 있어서, `GD_FLG_SERIAL_READY`가 서기 전의 글자는 `printch()`로 UART 레지스터에 직접 써진다.

SPL 쪽에서는 빌드 시 **fdtgrep**이 태그 없는 노드를 아예 지운 작은 DT(`spl/u-boot-spl.dtb`)를 만든다. 그래서 i.MX8MP의 `imx8mp-evk-u-boot.dtsi`에는 27개의 `bootph-*`가 있다 — UART2 핀먹스(`&pinctrl_uart2 { bootph-pre-ram; }`), PMIC와 그 I2C(`bootph-all`), SD 전원 레귤레이터(`bootph-pre-ram`), 와치독 리셋 등. **부모 노드에도 태그를 달아야 한다**는 점이 함정이다: bootph.yaml 문서가 "fdtgrep은 현재 태그를 붙인 노드에만 적용하고 부모에는 적용하지 않는다"고 밝힌다. I2C PMIC에 태그를 달았으면 I2C 컨트롤러와 그 버스 노드에도 달아야 SPL에서 PMIC가 보인다.

### 8.5 pre-reloc 코드에서 하면 안 되는 것

| 하면 안 됨 | 이유 | 대신 |
|---|---|---|
| 전역·static 변수에 쓰기 | BSS가 아직 0으로 안 지워졌거나(flash XIP면 쓰기 불가) relocation 때 버려짐 | `gd` 필드, DM의 `priv_auto` |
| 큰 `malloc` | `SYS_MALLOC_F_LEN`(여기 8 KiB)만 있음 | 필요하면 `SYS_MALLOC_F_LEN` 증가 |
| 함수 포인터를 저장해 두고 relocation 뒤에 사용 | 옛 주소를 가리킴 | relocation 후 다시 조회 |
| 많은 노드에 `bootph-all` | malloc_f 고갈, 부팅 느려짐 | 콘솔·DRAM·부트 미디어에 필요한 것만 |

`gd->flags & GD_FLG_RELOC`로 "지금 relocation 후인가"를 알 수 있다 — `c4_widget`의 `stage()`가 그것을 썼다.

---

## 9. 측정 — 설정 하나가 바이너리 크기를 얼마나 바꾸나

부트 미디어(SPI NOR 8~16 MB, eMMC boot 파티션 4 MB)와 부팅 시간 때문에 생산 U-Boot는 작게 만드는 경우가 많다. 기본 `qemu_arm64_defconfig`에 fragment 하나씩만 적용해 `u-boot.bin` 크기를 쟀다.

무엇을 확인하나: 각 Kconfig 변경의 크기 영향.

```sh
#!/bin/bash
# build each variant: qemu_arm64_defconfig + one fragment, record u-boot.bin size
S=/var/tmp/C4/src; F=/Users/donh/.elinux_scratch/C4/frags; R=/var/tmp/C4/sizes.txt
: > $R
for f in $F/*.config; do
  n=$(basename $f .config); O=/var/tmp/C4/v/$n
  rm -rf $O; mkdir -p $O; cd $O
  make -C $S O=$O qemu_arm64_defconfig >/dev/null 2>&1
  KCONFIG_CONFIG=$O/.config $S/scripts/kconfig/merge_config.sh -m -O $O $O/.config $f >/dev/null 2>&1
  make -C $S O=$O olddefconfig >/dev/null 2>&1
  if make -C $S O=$O -j6 > $O/build.log 2>&1; then
    echo "$n bin=$(stat -c %s $O/u-boot.bin) $(size $O/u-boot | awk 'NR==2{print "text="$1" data="$2" bss="$3}') warnings=$(grep -c 'warning:' $O/build.log)" >> $R
  else
    echo "$n FAILED" >> $R
  fi
done
echo DONE >> $R
```

> 함정 (실제로 겪음): 처음 버전은 `merge_config.sh -m -O $O ...`만 줬는데, `-m` 모드는 결과를 **현재 디렉터리의 `.config`** 에 쓰기 때문에 fragment가 적용되지 않은 채 빌드됐다(모든 변형의 크기가 기본값과 같게 나왔다). `cd $O`와 `KCONFIG_CONFIG=$O/.config`를 추가해 고쳤다. **크기·성능 측정 전에는 반드시 `.config`에 변경이 들어갔는지 grep으로 확인한다.**

결과(`cat /var/tmp/C4/sizes.txt`, 실제 출력):

```text
v0_base bin=1504208 text=1439653 data=62784 bss=0 warnings=0
v1_c4hello bin=1505264 text=1440646 data=62840 bss=0 warnings=0
v2_widget bin=1505320 text=1440519 data=63024 bss=0 warnings=0
v3_widget_cmdmisc bin=1507192 text=1442170 data=63248 bss=0 warnings=0
v4_no_efiselftest bin=1246064 text=1188091 data=56200 bss=0 warnings=0
v5_no_efidebug bin=1487912 text=1424311 data=61832 bss=0 warnings=0
v6_no_cmdtpm bin=1474736 text=1412533 data=60432 bss=0 warnings=0
v7_no_efi_loader bin=1029408 text=978940 data=48808 bss=0 warnings=0
v8_no_net bin=1407200 text=1346362 data=59064 bss=0 warnings=0
v9_full_c4 bin=1204392 text=1149208 data=53416 bss=0 warnings=0
DONE
```

(`size`의 bss가 0인 것은 §8.3의 `readelf -S`에서 보듯 U-Boot의 `.bss`가 ALLOC 플래그 없이(`W`만) `.rela.dyn`과 겹쳐 배치되어 `size`가 세지 않기 때문이다. 비교에는 `bin`만 쓴다. 모든 변형이 경고 0개로 빌드됐다.)

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">u-boot.bin 크기 (KiB) — qemu_arm64_defconfig + fragment 하나</text>
  <line x1="230.0" y1="34" x2="230.0" y2="352" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="230.0" y="368" text-anchor="middle" font-size="12">0</text>
  <line x1="312.5" y1="34" x2="312.5" y2="352" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="312.5" y="368" text-anchor="middle" font-size="12">400</text>
  <line x1="395.0" y1="34" x2="395.0" y2="352" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="395.0" y="368" text-anchor="middle" font-size="12">800</text>
  <line x1="477.5" y1="34" x2="477.5" y2="352" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="477.5" y="368" text-anchor="middle" font-size="12">1200</text>
  <line x1="560.0" y1="34" x2="560.0" y2="352" stroke="currentColor" stroke-opacity="0.2"/>
  <text x="560.0" y="368" text-anchor="middle" font-size="12">1600</text>
  <text x="222" y="54" text-anchor="end" font-size="12">v0 기본 qemu_arm64</text>
  <rect x="230" y="40" width="303.0" height="20" fill="#888" fill-opacity="0.75" stroke="#888"/>
  <text x="539.0" y="54" font-size="12">1469</text>
  <text x="222" y="85" text-anchor="end" font-size="12">+ CMD_C4HELLO</text>
  <rect x="230" y="71" width="303.2" height="20" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="539.2" y="85" font-size="12">1470 (+1056)</text>
  <text x="222" y="116" text-anchor="end" font-size="12">+ MISC + C4_WIDGET</text>
  <rect x="230" y="102" width="303.2" height="20" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="539.2" y="116" font-size="12">1470 (+1112)</text>
  <text x="222" y="147" text-anchor="end" font-size="12">+ 위 + CMD_MISC</text>
  <rect x="230" y="133" width="303.6" height="20" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="539.6" y="147" font-size="12">1472 (+2984)</text>
  <text x="222" y="178" text-anchor="end" font-size="12">- CMD_EFIDEBUG</text>
  <rect x="230" y="164" width="299.7" height="20" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="535.7" y="178" font-size="12">1453 (-16296)</text>
  <text x="222" y="209" text-anchor="end" font-size="12">- CMD_TPM</text>
  <rect x="230" y="195" width="297.0" height="20" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="533.0" y="209" font-size="12">1440 (-29472)</text>
  <text x="222" y="240" text-anchor="end" font-size="12">- NET</text>
  <rect x="230" y="226" width="283.4" height="20" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="519.4" y="240" font-size="12">1374 (-97008)</text>
  <text x="222" y="271" text-anchor="end" font-size="12">- CMD_BOOTEFI_SELFTEST</text>
  <rect x="230" y="257" width="251.0" height="20" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="487.0" y="271" font-size="12">1217 (-258144)</text>
  <text x="222" y="302" text-anchor="end" font-size="12">c4.config 전체</text>
  <rect x="230" y="288" width="242.6" height="20" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="478.6" y="302" font-size="12">1176 (-299816)</text>
  <text x="222" y="333" text-anchor="end" font-size="12">- EFI_LOADER</text>
  <rect x="230" y="319" width="207.3" height="20" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
  <text x="443.3" y="333" font-size="12">1005 (-474800)</text>
  <line x1="230" y1="34" x2="230" y2="352" stroke="currentColor"/>
  <text x="340" y="390" text-anchor="middle" font-size="12">괄호 = 기본 대비 바이트 차이. 주황 = 늘어남, 초록 = 줄어듦, 파랑 = §2.3 fragment</text>
</svg>
```
그림 7 — 설정 하나씩 바꿨을 때 `u-boot.bin` 크기(기본 1469 KiB 대비). 직접 만든 명령·드라이버는 1~3 KB, 기능 하나를 통째로 끄면 수십~수백 KB가 움직인다.

| 변형 | 크기 변화 | 비율 | 해석 |
|---|---|---|---|
| `+ CMD_C4HELLO` | +1056 B | +0.1% | 명령 하나 = 코드 + `cmd_tbl` 엔트리 + 도움말 문자열 |
| `+ MISC + C4_WIDGET` | +1112 B | +0.1% | 드라이버 + misc uclass 코어 |
| `+ 위 + CMD_MISC` | +2984 B | +0.2% | `misc` 명령까지 |
| `- CMD_EFIDEBUG` | -16296 B | -1.1% | 디버그 명령 하나도 16 KB |
| `- CMD_TPM` | -29472 B | -2.0% | TPM 명령 (TPM 드라이버는 남음) |
| `- NET` | -97008 B | -6.4% | 네트워크 스택 전체 (TFTP 부팅 불가) |
| `- CMD_BOOTEFI_SELFTEST` | -258144 B | -17.2% | EFI 자체 테스트 앱이 통째로 들어 있었다 |
| c4.config 전체 | -299816 B | -19.9% | 위 셋을 끄고 C4 기능을 더한 합 |
| `- EFI_LOADER` | -474800 B | -31.6% | UEFI 구현 전체 (EFI 부팅 불가) |

출력에서 볼 것: **"명령 하나 끄기"의 무게가 천차만별이다.** `CMD_BOOTEFI_SELFTEST`는 명령 이름만 보면 작아 보이지만 EFI selftest 프로그램 전체를 끌고 들어와 17%를 차지했다. 그리고 효과는 단순 합이 아니다: c4.config는 EFIDEBUG(-16 KB) + TPM(-29 KB) + SELFTEST(-258 KB) + C4 기능(+3 KB)인데 결과는 -300 KB로, 의존 심볼(`CMD_BOOTEFI_HELLO` 등)이 함께 움직인 만큼 차이가 난다. 생산 보드에서 크기를 줄일 때는 이렇게 **한 번에 하나씩 끄고 측정**하고, 끈 기능 때문에 부팅 경로(EFI 부팅, 네트워크 복구)가 깨지지 않는지 QEMU나 보드에서 확인한다.


---

## 10. 커널 안에서는 무슨 일이 일어나나 — U-Boot DM에서 Linux driver model로

U-Boot는 커널이 아니지만, 이 노트에서 만든 것들은 **커널 드라이버를 쓸 때 거의 그대로 다시 나온다**.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="250" y="14" width="180" height="46" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="34" text-anchor="middle" font-size="13">같은 .dts 노드</text>
  <text x="340" y="52" text-anchor="middle" font-size="12">compatible = "c4,widget"</text>
  <line x1="290" y1="60" x2="165" y2="96" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="390" y1="60" x2="515" y2="96" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="20" y="98" width="290" height="60" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="165" y="120" text-anchor="middle" font-size="13">U-Boot: lists_bind_fdt()</text>
  <text x="165" y="140" text-anchor="middle" font-size="12">of_match 비교 → device_bind</text>
  <rect x="370" y="98" width="290" height="60" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="515" y="120" text-anchor="middle" font-size="13">Linux: of_platform_populate()</text>
  <text x="515" y="140" text-anchor="middle" font-size="12">platform_device 생성 → bus match</text>
  <line x1="165" y1="158" x2="165" y2="186" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="515" y1="158" x2="515" y2="186" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="20" y="188" width="290" height="60" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="165" y="210" text-anchor="middle" font-size="13">첫 사용 때 device_probe()</text>
  <text x="165" y="230" text-anchor="middle" font-size="12">of_to_plat → pinctrl → probe</text>
  <rect x="370" y="188" width="290" height="60" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="515" y="210" text-anchor="middle" font-size="13">really_probe() 즉시</text>
  <text x="515" y="230" text-anchor="middle" font-size="12">pinctrl → drv->probe (DEFER 가능)</text>
  <line x1="165" y1="248" x2="165" y2="276" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="515" y1="248" x2="515" y2="276" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="20" y="278" width="290" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="165" y="303" text-anchor="middle" font-size="12">misc_read() → ops->read</text>
  <rect x="370" y="278" width="290" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="515" y="303" text-anchor="middle" font-size="12">read(fd) → file_operations.read</text>
</svg>
```
그림 8 — 같은 DT 노드가 U-Boot와 Linux에서 각각 드라이버에 붙는 경로. 매칭 키(compatible)와 단계(bind → probe → ops 호출)는 같고, probe 시점과 호출 방식이 다르다.

| 이 노트에서 한 것 | 커널에서 다시 만나는 곳 |
|---|---|
| `U_BOOT_DRIVER` + `of_match` | `module_platform_driver()` + `of_match_table` (G3) |
| `priv_auto`, `dev_get_priv()` | `devm_kzalloc()` + `platform_set_drvdata()` / `platform_get_drvdata()` (G3) |
| `dev_read_addr_ptr()` | `devm_platform_ioremap_resource()` — 커널은 MMU 가상 주소가 필요해 `ioremap`이 필수 (G5) |
| `dev_read_u32_default()`, `dev_read_string()` | `device_property_read_u32()`, `of_property_read_string()` (D, G3) |
| `UCLASS_MISC` + `misc_ops` | `misc_register()` + `file_operations` (G4) |
| bind/probe 분리, 부모 먼저 probe | 커널 device core의 bus match, `-EPROBE_DEFER`, `device_link` (G2) |
| `bootph-*` 태그 | 커널에는 없음 — 대신 `earlycon`, initcall 레벨(C6) |
| `U_BOOT_CMD` linker list | 커널의 `__initcall`, `EXPORT_SYMBOL` 섹션 (B3, C6) |
| Kconfig `select`/`imply`/`depends`, defconfig, fragment | 커널 Kconfig 그대로 (E2) |
| `ft_board_setup()`로 커널에 줄 DT 수정 | 커널이 받는 DT의 `/chosen`, `memory` 노드 (C6, D) |

U-Boot가 커널에 넘겨주는 것도 정리해 두자. ARM64에서는 `x0` 레지스터에 **DT 주소** 하나만 넘긴다(커널 Documentation의 arm64 booting 규약). U-Boot는 부팅 직전 그 DT에 `/chosen/bootargs`(커널 명령줄), `/chosen/linux,initrd-start/end`, `memory` 노드의 실제 RAM 크기를 채운다(`boot/image-fdt.c`의 `image_setup_libfdt()` → 보드의 `ft_board_setup()`). 커널이 "U-Boot가 알려 준 메모리 크기가 틀렸다"며 이상하게 뜬다면 이 단계를 본다.

---

## 11. Embedded 실무에서 다시 보기

### 11.1 센서 키트 carrier board — EVK에서 갈라져 나오기

예를 들어 센서 키트가 i.MX8MP SoM 위에 자체 carrier board(IMU용 SPI, GNSS용 UART3, CAN 2개, 이더넷 PHY 1개, eMMC)를 얹었다면, U-Boot 포트는 보통 이렇게 진행한다.

1. `board/nxp/imx8mp_evk/` → `board/acme/sensorkit/` 복사, `Kconfig`의 `SYS_BOARD`/`SYS_VENDOR`/`SYS_CONFIG_NAME` 변경, `arch/arm/mach-imx/imx8m/Kconfig`에 `TARGET_IMX8MP_SENSORKIT` 추가.
2. `configs/imx8mp_evk_defconfig` → `imx8mp_sensorkit_defconfig`, `CONFIG_TARGET_...`과 `DEFAULT_DEVICE_TREE` 변경.
3. DDR: SoM 벤더가 준 `lpddr4_timing.c`를 받는다(SoM을 쓰면 DDR은 SoM 벤더 몫이라 대개 그대로 쓴다).
4. DT: Linux용 `imx8mp-sensorkit.dts`를 먼저 쓰고, `imx8mp-sensorkit-u-boot.dtsi`에 콘솔 UART·PMIC I2C·eMMC에 `bootph-*`.
5. 이더넷 PHY 리셋 GPIO·PHY 주소를 DT에 반영, `ping`으로 확인.
6. env를 eMMC user 영역 특정 오프셋에(`ENV_IS_IN_MMC`, `ENV_OFFSET`), A/B 업데이트를 하면 `bootcmd`를 RAUC/SWUpdate 규칙에 맞춘 스크립트로 (P 모듈).
7. 생산용: `BOOTDELAY=0` 또는 `-2`(키 입력 확인 안 함), `AUTOBOOT_KEYED`, `ENV_IS_NOWHERE`나 env 쓰기 제한, 디버그 명령 제거(§9 측정처럼 크기도 준다).

### 11.2 "SPL이 아무 것도 안 찍어요"

bring-up 첫날 가장 흔한 장면이다. 원인 후보는 거의 정해져 있다: (a) DDR 이전이라 콘솔 UART 핀먹스·클럭 노드에 `bootph-pre-ram`이 없다, (b) 다른 UART를 쓰는데 `stdout-path`/`serial` alias가 EVK 것 그대로다, (c) DDR training 실패로 SPL이 멈췄다(로그가 "DDRINFO" 근처에서 끊김). 이럴 때 쓰는 것이 이 defconfig에도 켜져 있던 `CONFIG_DEBUG_UART`다 — DM·DT 없이 고정 주소(`DEBUG_UART_BASE`)의 UART 레지스터에 직접 쓰므로, DT가 틀려도 글자가 나온다. Don이 JTAG 없이 GPIO 토글로 위치를 찾던 것과 같은 최후의 수단이다.

### 11.3 보드에 새 칩이 하나 있을 때

센서 키트의 이더넷 PHY 리셋이 I2C GPIO expander(예: PCA9555 계열)에 물려 있다면, U-Boot가 그 expander를 알아야 네트워크 부팅이 된다. `drivers/gpio/`에 지원 드라이버가 이미 있는지 `dm compat`이나 소스 grep으로 찾고, 있으면 DT 노드 + `CONFIG_` 하나로 끝난다. 없으면 이 노트의 `c4_widget`과 같은 모양으로 `UCLASS_GPIO` 드라이버(`dm_gpio_ops`)를 쓴다. 이때 **부모(I2C 컨트롤러) 노드가 DT에 있고 probe 가능해야** 자식이 probe된다(§7.2의 부모 우선).

### 11.4 벤더 fork에서 upstream으로

벤더 BSP U-Boot(예: NXP `lf_v20xx.xx` 브랜치)는 upstream보다 기능이 앞서지만 보안 패치는 늦다. 장기 유지보수 제품이라면 upstream U-Boot로 옮기는 프로젝트를 하게 되고, 그때 부딪히는 것이 `u-boot,dm-pre-reloc` → `bootph-*` 이름 변경, `include/configs/*.h`의 매크로 → Kconfig 이전, 보드 전용 DT → `OF_UPSTREAM` 전환이다. 이 노트의 §2·§7.8·§8.4가 그 체크리스트다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 콘솔 UART 노드에 `bootph-*` 누락 | SPL/pre-reloc에서 무출력, post-reloc 이후에만 로그 | pre-reloc DM이 UART를 bind 안 함 | UART·핀먹스·클럭 노드와 **부모 노드**에 `bootph-all`/`bootph-pre-ram` |
| 옛 `u-boot,dm-pre-reloc` 사용 | 최신 U-Boot로 올린 뒤 SPL 무출력 | 2023년 `bootph-*`로 이름 변경 | 태그 이름 일괄 변경 |
| pre-reloc 코드에서 전역 변수에 쓰기 | 값이 사라지거나 XIP에서 hang | BSS 미준비, relocation 때 버려짐 | `gd` 또는 DM priv 사용 |
| `SYS_MALLOC_F_LEN` 부족 | pre-reloc 중 `alloc space exhausted ptr ... limit ...` 에러(`common/malloc_simple.c`), 장치 누락 | `bootph-all` 노드가 많음 | 값 증가, 불필요한 태그 제거 (`Early malloc usage`로 확인) |
| defconfig와 fragment를 한 make 줄에 | fragment가 적용 안 된 바이너리 | 이 트리에서 실제로 발생 (§2.3) | 두 번의 `make`로 분리하고 `.config` grep |
| `.config`를 직접 고치고 커밋 안 함 | 다음 `make <board>_defconfig`에서 변경 소실 | `.config`는 산출물 | `savedefconfig` → `configs/`에 복사 |
| `select`로 의존 심볼을 강제 | 빌드 에러나 링크 에러, 설정 불일치 | `select`는 상대의 `depends on`을 무시 | 가능하면 `depends on`/`imply` |
| `.env`에서 상대 경로 `#include` | `fatal error: xxx.env: No such file or directory` | 빌드가 `include/generated/`로 복사 후 전처리 (§3.4) | `include/` 기준 경로 사용 또는 내용 복사 |
| 저장된 env가 새 기본 env를 가림 | 새 빌드의 `bootcmd`가 반영 안 됨 | CRC 맞는 saved env 우선 | `env default -a; saveenv` 또는 env 영역 삭제 |
| DDR 타이밍을 다른 메모리 칩용으로 사용 | SPL에서 training 실패, 또는 부팅 후 랜덤 크래시 | 칩·보드 배선에 맞지 않는 타이밍 | 벤더 DDR 툴로 재생성 + 메모리 stress test |

---

## 13. 면접에서 이렇게 말한다

**Q.** "How would you port U-Boot to a new custom board based on an SoC that's already supported?"

**A.** 가장 가까운 기존 보드(보통 EVK)를 복사해서 시작한다. Kconfig에 TARGET 심볼과 board 디렉터리를 추가하고, defconfig를 만들고, Device Tree를 우리 보드의 핀·UART·eMMC·PMIC·PHY에 맞춘다. 메모리 칩이 다르면 벤더 툴로 DDR 타이밍을 다시 생성한다. 그다음 보드 C 파일의 훅(dram_init, board_late_init 등), env 저장 위치와 bootcmd를 정한다. 콘솔 → DDR → 부트 미디어 → 네트워크 → 커널 부팅 순서로 하나씩 확인한다.

> I start from the closest existing board, usually the SoC vendor's EVK. I add a TARGET symbol and a board directory, create a defconfig, and adapt the device tree to our pinmux, console UART, eMMC, PMIC and Ethernet PHY, with a board-u-boot.dtsi for U-Boot-only properties like bootph tags. If the memory part differs I regenerate the DDR timing with the vendor's tool and run a memory stress test. Then I fill in the board hooks — dram_init, board_late_init, maybe ft_board_setup — pick the environment location and the boot command. I bring it up in order: console, DDR, boot media, network, then booting the kernel.

**꼬리 질문**: "What goes in the -u-boot.dtsi rather than the main dts?" → `bootph-*` 태그, binman 이미지 레이아웃, U-Boot 전용 노드. 메인 `.dts`는 Linux와 공유하므로 깨끗하게 둔다.

**Q.** "What is SPL and why does it exist?"

**A.** Secondary Program Loader. 리셋 직후에는 칩 내부 SRAM 수백 KB만 쓸 수 있는데 U-Boot proper는 1 MB가 넘는다. 그래서 같은 소스를 최소 기능으로 다시 빌드한 SPL을 BootROM이 SRAM에 올리고, SPL이 클럭·PMIC·콘솔을 켠 뒤 DDR을 초기화하고 training한 다음, TF-A와 U-Boot proper를 DRAM에 로드한다. SRAM이 더 작으면 그 앞에 TPL을 둔다.

> SPL is a stripped-down build of U-Boot from the same source tree. At reset only a few hundred kilobytes of on-chip SRAM are usable, and full U-Boot is over a megabyte, so the boot ROM loads SPL into SRAM. SPL brings up clocks, the PMIC and a console, initialises and trains DRAM — on i.MX8M that includes loading the vendor's DDR PHY training firmware — and then loads TF-A and U-Boot proper into DRAM. If SRAM is even smaller, a TPL stage runs before SPL. Each stage has its own CONFIG_SPL_ and CONFIG_TPL_ options and its own filtered device tree.

**Q.** "Explain U-Boot's driver model. How does it compare to Linux's?"

**A.** driver, uclass, udevice 세 객체다. driver는 `U_BOOT_DRIVER`로 링커 리스트에 등록되고 compatible로 DT 노드에 매칭된다. uclass는 같은 종류 장치의 공통 API(예: misc_read, dm_i2c_read)를 제공하고, udevice는 DT 노드 하나의 인스턴스다. Linux의 platform_driver, subsystem/class, struct device와 거의 1:1이다. 큰 차이는 probe 시점이다: U-Boot는 부팅 때 전부 bind만 하고 probe는 처음 쓸 때 하고, Linux는 match되면 즉시 probe하며 의존성이 없으면 -EPROBE_DEFER로 재시도한다. 또 U-Boot는 인터럽트 없이 폴링, 단일 스레드다.

> It has three objects: drivers, registered with U_BOOT_DRIVER into a linker list and matched to device-tree nodes by compatible; uclasses, which define the common API for a class of device like misc_read or dm_i2c_read; and udevices, one per bound node. That maps almost one-to-one onto Linux's platform_driver, subsystem or class, and struct device. The main difference is timing: U-Boot binds everything at startup but probes lazily on first use, while Linux probes as soon as a driver matches and uses -EPROBE_DEFER when a dependency isn't ready. U-Boot is also single-threaded and polled — no interrupts. In practice I check it with dm tree, which shows the hierarchy and which devices are actually probed.

**Q.** "What's the difference between bind and probe in U-Boot?"

**A.** bind는 DT 노드와 드라이버를 짝지어 udevice 구조체를 만드는 것뿐이라 하드웨어를 건드리지 않는다. probe는 DT 속성을 읽고(of_to_plat), 부모를 먼저 probe하고, pinctrl·clock 기본값을 적용한 뒤 드라이버의 probe로 하드웨어를 깨운다. QEMU에서 직접 해 보니 내 misc 드라이버는 부팅 때 bind만 되고 dm tree에서 probed 표시가 없다가, `misc list`로 처음 접근할 때 probe 로그가 찍혔다.

> Bind just pairs a node with a driver and allocates the udevice — no hardware access, and it's done for the whole tree at startup. Probe is when the device is actually brought up: of_to_plat reads the DT properties, the parent is probed first, default pinctrl and assigned clocks are applied, then the driver's probe runs. I verified this in QEMU with a small UCLASS_MISC driver: it showed up in dm tree unprobed after boot, and the probe message only appeared the first time something accessed the misc uclass.

**Q.** "What is relocation in U-Boot and why is it needed?"

**A.** U-Boot는 링크 주소(flash나 SPL이 올려 준 곳)에서 시작하고, DRAM 크기를 알아낸 뒤 DRAM 꼭대기로 자신을 복사해서 옮긴다. 아래쪽 DRAM을 커널·initrd용으로 비워 두고, RAM 크기가 달라도 같은 바이너리가 동작하게 하기 위해서다. PIE로 링크되어 코드는 PC-상대라 그대로 돌고, 데이터 안의 절대 주소는 .rela.dyn의 R_AARCH64_RELATIVE 항목을 돌며 오프셋을 더해 고친다. QEMU 빌드에서는 3833개였다. relocation 전에는 BSS를 못 쓰고 작은 early malloc과 gd만 쓴다.

> U-Boot starts at its link address — in flash, or wherever SPL put it — and once board_init_f knows the DRAM size it reserves space from the top of RAM down and copies itself there. That keeps low memory free for the kernel and initrd and makes the same binary work regardless of RAM size. The ARM64 build is position-independent, so code just runs; absolute addresses stored in data, like function pointers in driver and command tables, are fixed up by walking the R_AARCH64_RELATIVE entries in .rela.dyn — about 3800 in the QEMU build. Before relocation you can't use BSS or globals; you only have global data in x18 and a small early malloc pool.

**꼬리 질문**: "How do you debug code before relocation?" → `DEBUG_UART`로 DM 없이 직접 출력, `bdinfo`의 `relocaddr`로 심볼 주소 보정해 gdb에 `add-symbol-file u-boot <relocaddr>`.

**Q.** "What are bootph tags (formerly u-boot,dm-pre-reloc)?"

**A.** DT 노드가 어느 부팅 단계에 포함될지 표시하는 속성이다. 태그가 없으면 마지막 단계(U-Boot proper의 relocation 후)에서만 bind된다. bootph-all은 모든 단계, bootph-pre-ram은 SPL, bootph-pre-sram은 TPL이다. SPL 빌드는 fdtgrep으로 태그 없는 노드를 지운 작은 DT를 만든다. 부모 노드에도 태그를 달아야 하고, 콘솔 UART에 빠뜨리면 SPL이 아무것도 안 찍는다.

> They mark which boot phases need a device-tree node. Untagged nodes only exist in the final phase, after relocation. bootph-all means every phase, bootph-pre-ram means SPL, bootph-pre-sram means TPL, and bootph-some-ram covers U-Boot proper before relocation. The SPL build runs fdtgrep to drop everything untagged, which keeps the SPL DTB tiny. The classic bug is forgetting to tag the console UART, its pinmux, or a parent bus node — SPL then prints nothing. They replaced the u-boot,dm-pre-reloc style properties in 2023, which matters when porting an old vendor BSP to mainline.

**Q.** "How do you manage U-Boot configuration for several product variants?"

**A.** 보드 defconfig 하나를 기준으로 두고, 변형(디버그·생산·고객별)은 fragment로 덮어쓴다. U-Boot는 `make <name>.config`로 board 디렉터리의 fragment를 찾아 merge_config.sh로 합치고, defconfig 안에서 `#include`로 공유 fragment를 쓸 수도 있다. 바꾼 뒤엔 savedefconfig로 최소본을 뽑아 diff를 리뷰한다. 이때 건드리지 않은 줄이 나타나면 imply·default 의존성 변화이니 의도한 것인지 확인한다.

> One base defconfig per board, and variants — debug, production, customer-specific — as small fragments. U-Boot's kconfig Makefile finds a fragment named on the make command line anywhere under board/ and merges it with merge_config.sh, and defconfigs can also #include shared fragments, like qemu_arm64_acpi_defconfig including acpi.config. After changes I run savedefconfig and review the diff. If lines appear that I didn't touch, that's an imply or default dependency shifting, so I decide whether to keep them explicitly. For production I'd also check the binary size impact of each option.

**Q.** "Where does the environment live, and what happens on a fresh board?"

**A.** 기본 환경은 바이너리에 들어 있고(.env 파일 + Kconfig 값), 저장된 환경은 ENV_IS_IN_MMC/SPI_FLASH 등으로 정한 위치에 있다. 부팅 때 저장된 env를 읽어 CRC가 맞으면 그것을 쓰고, 아니면 "bad CRC, using default environment"를 띄우고 기본값을 쓴다. 새 보드나 env 영역이 지워진 보드에서 정상이다. 반대로 기본값을 바꿨는데 안 바뀌면 저장된 env가 이기고 있는 것이다. 생산 제품은 이중화 env나 env 쓰기 제한을 고려한다.

> There are two layers: the default environment compiled into the binary, built from the board's .env file plus Kconfig values like BOOTDELAY, and a saved environment at a location chosen with ENV_IS_IN_MMC, SPI_FLASH and so on. At boot U-Boot loads the saved copy and uses it if the CRC is valid; otherwise it prints "bad CRC, using default environment", which is normal on a fresh board. The flip side is that a saved environment overrides a new default, so after an update you may need env default -a and saveenv. For products you'd use a redundant environment for power-fail safety, and often restrict or disable writes.

---

## 14. 직접 해보기

1. **개념**: `qemu_arm64_defconfig`에는 `CONFIG_TARGET_QEMU_ARM_64BIT`가 없다. 그런데 `.config`에는 `=y`로 있다. 왜인가? `CONFIG_SYS_BOARD="qemu-arm"`은 어디서 정해지나?
   정답/힌트: `arch/arm/mach-qemu/Kconfig`의 `choice`가 기본값 `TARGET_QEMU_ARM_64BIT`를 갖고, 그 아래 `if` 블록의 `config SYS_BOARD default "qemu-arm"`이 값을 준다 (§1.3).
2. **코드**: `c4hello`에 서브명령을 추가하라 — `c4hello dump <compatible>`은 해당 노드의 모든 속성 이름을 출력한다(`ofnode_for_each_prop`, `ofnode_get_property_by_prop` 사용). `U_BOOT_CMD_WITH_SUBCMDS`를 써 보라.
   정답/힌트: `include/command.h`의 `U_BOOT_SUBCMD_MKENT`와 `cmd/misc.c`의 구조를 그대로 따라 하면 된다.
3. **DM 관찰**: `c4_virt.dts`의 `c4_uart` 노드에도 `bootph-all`을 붙이고 다시 부팅하라. 부팅 로그의 pre-reloc bind 줄과 `bdinfo`의 `Early malloc usage`가 어떻게 바뀌나?
   정답/힌트: pre-reloc bind가 2줄이 되고 early malloc 사용량이 늘어난다(udevice 하나와 priv만큼). 값은 직접 확인.
4. **드라이버 플래그**: DT 태그 대신 드라이버에 `.flags = DM_FLAG_PRE_RELOC`를 넣으면 어떻게 되나? 두 노드 모두 pre-reloc에 bind되는지 확인하라.
   정답/힌트: `lists.c`의 조건이 "노드 태그 OR 드라이버 플래그"라 둘 다 bind된다. 플래그는 그 드라이버의 모든 노드에 적용되므로 노드 태그가 더 정밀하다.
5. **probe 순서**: `c4_widget`의 `.read`에서 일부러 `-EIO`를 반환하게 바꾸고 `misc read`를 실행해 보라. 그리고 `probe`에서 `-ENODEV`를 반환하면 `dm tree`·`misc list`가 어떻게 보이는가?
   정답/힌트: probe 실패 장치는 `[ + ]`가 안 되고, `misc list`가 다음 장치로 넘어가거나 에러를 낸다. U-Boot는 probe 실패를 기억하지 않아 다음 접근 때 다시 probe를 시도한다 — 로그로 확인하라.
6. **크기 측정**: §9의 스크립트에 `# CONFIG_VIDEO is not set`, `# CONFIG_USB is not set`, `# CONFIG_FIT_SIGNATURE is not set` 변형을 추가해 크기를 재고, 어떤 것이 부팅에 영향을 주는지(예: `poweroff`가 사라지나, 네트워크 부팅이 되나) QEMU로 확인하라.
   정답/힌트: 크기만 보지 말고 `dm drivers`와 기능 테스트를 같이 해야 한다. `CONFIG_VIDEO`를 끄면 §8.2의 8 MiB 프레임버퍼 예약도 사라져 `relocaddr`가 올라간다.
7. **소스 읽기**: `board/nxp/imx8mp_evk/spl.c`의 `spl_board_init()`과 `board_boot_order()`(있다면)를 읽고, SPL이 다음 이미지를 SD에서 읽을지 eMMC에서 읽을지 어떻게 정하는지 설명하라.
   정답/힌트: BootROM이 기록한 부트 장치를 `spl_boot_device()`가 읽어 SPL 로더를 고른다. `arch/arm/mach-imx/spl.c`도 함께 보라.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| board port | 보드 포트 | 새 보드를 위한 defconfig·DT·보드 C 파일·env 묶음 |
| defconfig | 기본 설정 파일 | 기본값과 다른 Kconfig 값만 적은 최소 목록 |
| fragment | 설정 조각 | 기존 `.config` 위에 덮어쓰는 작은 `*.config` |
| `savedefconfig` | 최소 defconfig 생성 | `.config`에서 기본값과 다른 줄만 뽑는 make 타깃 |
| `select` / `imply` | 강제 선택 / 기본값 제안 | 다른 심볼을 강제로 켬 / 기본값만 y로 |
| `CONFIG_IS_ENABLED()` | 단계별 설정 매크로 | 지금 빌드 중인 단계(SPL/TPL/proper)의 심볼 값 |
| SPL / TPL / VPL | 부트 단계 | SRAM에서 도는 축소판 U-Boot / 그 이전 단계 / 검증 단계 |
| DDR training | DDR 타이밍 보정 | PHY가 신호 지연·전압 기준을 측정해 맞추는 과정 |
| blob-ext | 외부 바이너리 | U-Boot 트리 밖에서 가져와 binman이 붙이는 파일 (DDR firmware, TF-A) |
| binman | 이미지 조립기 | DT에 적힌 레이아웃대로 SPL·firmware·U-Boot를 한 이미지로 |
| TIFS / sysfw | TI 시스템 펌웨어 | K3 SoC의 보안·전원·리소스 관리 코어용 펌웨어 |
| driver model (DM) | 드라이버 모델 | driver / uclass / udevice로 구성된 U-Boot 드라이버 프레임워크 |
| uclass | 장치 종류 | 같은 종류 장치의 공통 API (`UCLASS_MISC` 등) |
| udevice | 장치 인스턴스 | 보통 DT 노드 하나에 하나 |
| bind / probe | 연결 / 활성화 | 노드-드라이버 짝짓기 / 하드웨어 초기화 (lazy) |
| `of_to_plat` | DT → 플랫폼 데이터 | probe 직전 DT 속성을 읽어 구조체에 채우는 콜백 |
| linker list | 링커 리스트 | `__u_boot_list_2_<type>_2_<name>` 섹션을 이어 붙인 정적 테이블 |
| control DT | 제어용 DT | U-Boot 자신이 장치를 찾는 데 쓰는 DT (커널에 넘기는 DT와 다를 수 있음) |
| `-u-boot.dtsi` | U-Boot 전용 DT 조각 | 빌드 시 `.dts` 끝에 자동 include |
| `OF_UPSTREAM` | upstream DT 사용 | `dts/upstream/`의 Linux DT를 그대로 쓰는 설정 |
| `OF_BOARD` | 런타임 DT | 이전 단계(QEMU, TF-A)가 넘겨준 DT를 쓰는 설정 |
| relocation | 재배치 | U-Boot가 DRAM 꼭대기로 자신을 복사하고 절대 주소를 고치는 것 |
| `R_AARCH64_RELATIVE` | 상대 재배치 | "이 위치에 base + addend를 써라" 형식의 재배치 항목 |
| pre-reloc | 재배치 전 | `board_init_f` 단계, BSS 없음, malloc_f만 |
| `bootph-*` | 부트 단계 태그 | DT 노드를 어느 단계에 포함할지 표시 |
| `gd` (global data) | 전역 데이터 | 모든 단계에서 살아남는 상태 구조체, AArch64는 `x18` |
| `TEXT_BASE` | 링크 주소 | U-Boot가 처음 실행되는 주소 |
| default env / saved env | 기본 / 저장 환경 | 바이너리에 내장 / 저장소에 `saveenv`로 쓴 것 |
| bootstd | 표준 부팅 | bootdev·bootmeth·bootflow로 부팅 대상을 자동 탐색 |
| `DEBUG_UART` | 디버그 UART | DM 없이 고정 주소 UART에 직접 출력하는 초기 콘솔 |

---

## 16. 요약 & 체크리스트

U-Boot 포팅은 **공통 코드는 두고 보드별 부분을 새로 만드는 일**이다. 보드별 부분은 defconfig(Kconfig 값), Device Tree(Linux와 공유하는 `.dts` + U-Boot 전용 `-u-boot.dtsi`), `board/<vendor>/<board>/`의 훅(`dram_init`, `board_late_init` …)과 `.env`, 그리고 실물 SoC라면 SPL의 `board_init_f`와 툴로 생성한 DDR 타이밍·벤더 training 펌웨어다. 설정은 Linux와 같은 Kconfig로 하고, fragment로 변형을 만들고, `savedefconfig` diff로 리뷰한다. 드라이버는 driver/uclass/udevice의 DM으로 쓰며, Linux driver model과 1:1로 대응하되 **bind는 전부, probe는 처음 쓸 때**라는 점이 다르다. U-Boot는 링크 주소에서 시작해 DRAM 꼭대기로 자신을 relocate하며(PIE + `R_AARCH64_RELATIVE` 수정), relocation 전에는 `bootph-*` 태그가 붙은 노드만 bind하고 BSS 없이 `gd`와 작은 malloc으로 버틴다. QEMU에서 직접 명령과 드라이버를 추가해 이 흐름을 모두 로그로 확인했다.

- [ ] U-Boot 소스 트리에서 보드 포트가 만드는 파일과 공통 코드를 구분해 말할 수 있다
- [ ] defconfig 한 줄이 `SYS_BOARD`/`SYS_VENDOR`를 거쳐 `board/<v>/<b>/`를 고르는 사슬을 설명할 수 있다
- [ ] Kconfig의 `depends on`/`select`/`imply`/`default`를 구분하고 `savedefconfig` diff의 "건드리지 않은 줄"을 해석할 수 있다
- [ ] fragment로 보드 기본값(bootdelay, preboot, env 변수, CMD_ 토글)을 바꾸고 `.config`·`autoconf.h`·`u-boot-initial-env`로 확인할 수 있다
- [ ] `U_BOOT_CMD`로 명령을 추가하고 linker list에 들어간 것을 `nm`으로 보여 줄 수 있다
- [ ] `board_init_f`/`board_init_r`의 순서와 보드 훅이 끼는 위치를 그릴 수 있다
- [ ] SPL·TPL이 왜 있는지, DDR training과 벤더 blob(i.MX DDR firmware, TI TIFS)의 역할을 설명할 수 있다
- [ ] `U_BOOT_DRIVER`로 UCLASS_MISC 드라이버를 쓰고 DT 노드로 bind해 `dm tree`·`misc`로 확인할 수 있다
- [ ] bind와 probe의 차이, `device_probe()`의 순서(of_to_plat → 부모 → pinctrl → clk → probe)를 말할 수 있다
- [ ] U-Boot DM과 Linux driver model의 대응표를 그릴 수 있다
- [ ] relocation 전후 메모리 지도를 `bdinfo`로 읽고, `.rela.dyn` 수정 방식을 설명할 수 있다
- [ ] `bootph-*` 태그의 의미와 "부모에도 태그" 함정을 설명할 수 있다

## 참고 자료

- U-Boot 공식 문서 (docs.u-boot.org): [Driver Model 설계](https://docs.u-boot.org/en/latest/develop/driver-model/design.html), [Generic SPL framework](https://docs.u-boot.org/en/latest/develop/spl.html), [Kconfig 사용법](https://docs.u-boot.org/en/latest/develop/kconfig.html), [Environment](https://docs.u-boot.org/en/latest/usage/environment.html), [Standard Boot](https://docs.u-boot.org/en/latest/develop/bootstd/index.html)
- 이 VM의 소스(v2026.10)에서 직접 읽은 파일: `common/board_f.c`, `common/board_r.c`, `arch/arm/lib/relocate_64.S`, `arch/arm/lib/crt0_64.S`, `drivers/core/device.c`(`device_probe`), `drivers/core/lists.c`, `drivers/core/ofnode.c`(`ofnode_pre_reloc`), `include/dm/device.h`(`struct driver`), `include/command.h`, `include/misc.h`, `doc/device-tree-bindings/bootph.yaml`, `doc/develop/spl.rst`, `scripts/kconfig/Makefile`, `board/emulation/qemu-arm/`, `board/nxp/imx8mp_evk/`, `arch/arm/dts/imx8mp-u-boot.dtsi`, `arch/arm/dts/k3-am625-sk-binman.dtsi`
- elixir.bootlin.com의 U-Boot 소스 브라우저: [https://elixir.bootlin.com/u-boot/latest/source](https://elixir.bootlin.com/u-boot/latest/source)
- docs.kernel.org: [Kconfig Language](https://docs.kernel.org/kbuild/kconfig-language.html), [Booting AArch64 Linux](https://docs.kernel.org/arch/arm64/booting.html), [Driver Model — Platform Devices and Drivers](https://docs.kernel.org/driver-api/driver-model/platform.html)
- Bootlin, "Embedded Linux system development" 교육 슬라이드 — Bootloaders 장: [https://bootlin.com/doc/training/embedded-linux/](https://bootlin.com/doc/training/embedded-linux/)
- Chris Simmonds & Frank Vasquez, "Mastering Embedded Linux Programming" (3rd ed.) — 3장 All About Bootloaders (U-Boot 포팅 절)
- QEMU: [Arm 'virt' machine](https://www.qemu.org/docs/master/system/arm/virt.html)
