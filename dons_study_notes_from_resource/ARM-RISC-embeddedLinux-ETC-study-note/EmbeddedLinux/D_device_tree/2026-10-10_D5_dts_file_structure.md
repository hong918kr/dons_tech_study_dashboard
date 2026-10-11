# D5. DT 파일 구조 — .dtsi/.dts 계층, include, override, 빌드와 확인

> **이 노트를 다 읽으면**: 실제 벤더 BSP의 SoC `.dtsi` → SoM `.dtsi` → 캐리어 `.dts` 체인을 따라가며 "이 속성 값은 어느 파일에서 왔나"를 찾을 수 있다 · `#include`(cpp)와 `/include/`(dtc), `&label` 재정의, `/delete-node/`, `/omit-if-no-ref/`가 최종 dtb를 어떻게 바꾸는지 예측할 수 있다 · 커널의 `make dtbs`가 `cpp → dtc`로 dtb를 만드는 규칙을 읽고 원하는 보드 dtb 하나만 빌드할 수 있다 · `dtc`·`fdtget`·`fdtput`·`dtdiff`와 `/proc/device-tree`·`/sys/firmware/fdt`로 "보드에서 실제로 돌고 있는 DT"를 꺼내 소스와 비교할 수 있다
> **왜 필요한가**: 입사 첫 주에 받는 일이 "벤더 BSP에서 우리 보드 dts를 찾아 CAN 2를 켜 와", "rev C 보드는 IMU 인터럽트 핀이 바뀌었으니 DT 고쳐 와"다. 면접에서는 "dtsi와 dts의 차이", "SoC 파일을 안 건드리고 보드에서 노드를 끄는 법", "보드에서 실제 DT를 어떻게 확인하나"가 나온다
> **Don 기준 난이도**: 칩·모듈·PCB로 나뉜 하드웨어 계층, 헤더 공유, 전처리기 매크로는 이미 익숙하다 / 노드를 "다시 열어" 덮어쓰는 병합 규칙, 커널 Kbuild의 dtb 규칙, 런타임 DT를 읽는 sysfs 경로가 새로 배울 부분
> **선행 노트**: D1(왜 DT인가), D2(문법: node·property·label·phandle·`status`) — D3(드라이버 매칭), D4(자원 기술)와 같이 보면 좋다. U-Boot의 `fdt` 명령은 C3

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어를 만들 때를 떠올려 보자. 컨트롤러 칩 하나로 여러 제품(SKU)을 만든다. 칩 레지스터 맵은 칩 팀이 준 헤더 하나, 그 위에 패키지·NAND 구성별 설정, 그 위에 제품(폼팩터·용량)별 설정이 얹힌다. 보통 `chip_regs.h` → `platform_xxx.h` → `product_yyy.h`처럼 **공통에서 구체로** 층을 나누고, 위층이 아래층의 기본값을 `#define`으로 덮는다.

Device Tree도 똑같은 문제를 똑같은 방식으로 푼다. 다만 대상이 "C 상수"가 아니라 "하드웨어 설명 트리"다.

- **SoC 층** (`imx8mp.dtsi`) — 칩 안에 있는 모든 블록(UART 4개, CAN 2개, I2C 6개 …)을 주소·인터럽트·클록까지 적어 두고, 거의 전부 `status = "disabled"`로 꺼 둔다. 칩 벤더(NXP)가 쓴다.
- **SoM 층** (`imx8mp-verdin.dtsi`) — 칩을 얹은 모듈 위에 납땜된 것: DRAM 크기, PMIC, eMMC, 그리고 "어느 핀을 어느 기능에 쓰는지"(pinctrl). 모듈 벤더(Toradex)가 쓴다.
- **캐리어 보드 층** (`imx8mp-verdin-dev.dtsi` + `imx8mp-verdin-nonwifi-dev.dts`) — 모듈을 꽂는 보드: 커넥터, CAN 트랜시버, 센서. 여기서 실제로 쓰는 장치를 `status = "okay"`로 켠다. 보드를 만드는 회사(= 입사하면 우리 팀)가 쓴다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="145" y="30" text-anchor="middle" font-size="13">물리적 계층</text>
  <text x="520" y="30" text-anchor="middle" font-size="13">DT 파일 계층 (Linux 6.18.55)</text>
  <rect x="20" y="44" width="250" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="145" y="66" text-anchor="middle" font-size="13">SoC — i.MX 8M Plus</text>
  <text x="145" y="88" text-anchor="middle" font-size="12">CPU · UART · CAN · I2C (기본 꺼짐)</text>
  <rect x="20" y="124" width="250" height="56" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="145" y="146" text-anchor="middle" font-size="13">SoM — Verdin 모듈</text>
  <text x="145" y="168" text-anchor="middle" font-size="12">DRAM · PMIC · eMMC · 핀 배정</text>
  <rect x="20" y="204" width="250" height="56" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="145" y="226" text-anchor="middle" font-size="13">캐리어 보드 — Dev Board</text>
  <text x="145" y="248" text-anchor="middle" font-size="12">커넥터 · CAN 트랜시버 · 센서</text>
  <rect x="375" y="44" width="290" height="56" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="520" y="66" text-anchor="middle" font-size="13">imx8mp.dtsi (2451줄)</text>
  <text x="520" y="88" text-anchor="middle" font-size="12">칩 벤더(NXP)가 쓴다</text>
  <rect x="375" y="124" width="290" height="56" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="520" y="146" text-anchor="middle" font-size="12">imx8mp-verdin.dtsi (1493줄)</text>
  <text x="520" y="168" text-anchor="middle" font-size="12">+ imx8mp-verdin-nonwifi.dtsi (53줄)</text>
  <rect x="375" y="204" width="290" height="56" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="520" y="226" text-anchor="middle" font-size="12">imx8mp-verdin-dev.dtsi (269줄)</text>
  <text x="520" y="248" text-anchor="middle" font-size="12">+ imx8mp-verdin-nonwifi-dev.dts (18줄)</text>
  <line x1="270" y1="72" x2="371" y2="72" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
  <line x1="270" y1="152" x2="371" y2="152" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
  <line x1="270" y1="232" x2="371" y2="232" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
  <text x="322" y="66" text-anchor="middle" font-size="12">기술</text>
  <line x1="450" y1="204" x2="450" y2="184" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="450" y1="124" x2="450" y2="104" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="460" y="198" font-size="12">#include</text>
  <text x="460" y="118" font-size="12">#include</text>
  <rect x="20" y="276" width="645" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="342" y="301" text-anchor="middle" font-size="12">cpp + dtc → imx8mp-verdin-nonwifi-dev.dtb (64,443 B) → 부트로더가 커널에 넘긴다</text>
</svg>
```
그림 1 — 물리적 계층과 DT 파일 계층이 1:1로 대응한다. 위층 파일이 아래층 파일을 `#include`하고, 같은 노드를 다시 열어 값을 덮는다. 줄 수와 dtb 크기는 이 노트에서 실제로 잰 값이다.

이 노트가 다루는 것을 한 줄씩 정리하면 다음과 같다.

| 주제 | 한 줄 요약 | 절 |
|---|---|---|
| 실제 체인 | Toradex Verdin iMX8M Plus: SoC → SoM → SoM 변형 → 캐리어 → 보드 | 1 |
| include | `#include`는 cpp(텍스트 붙여넣기 + 매크로), `/include/`는 dtc(매크로 없음) | 2 |
| override | `&label { }`로 노드를 다시 열면 병합, 같은 속성은 나중 값이 이김 | 3 |
| 빌드 | `make dtbs` → `scripts/Makefile.dtbs`의 `cmd_dtc` = `gcc -E` + `dtc` | 4 |
| 층별 기여 | 층을 하나씩 쌓아 컴파일해 노드·속성·`okay` 수를 잰다 | 5 |
| 도구 | `dtc -I dtb -O dts`, `fdtget`, `fdtput`, `fdtdump`, `dtdiff` | 6 |
| 런타임 | `/proc/device-tree`, `/sys/firmware/fdt`, U-Boot `fdt` | 7 |
| BSP 정리법 | SoM당 dtsi 하나, 캐리어·리비전마다 얇은 dts | 8 |

> 실행 환경: 출력은 전부 Lima VM `elinux`(aarch64, `uname -r` = `7.0.0-38-generic`, 6 vCPU, gcc 15.2.0, 시스템 `dtc` 1.7.2, QEMU 10.2.1)에서 실제로 돌린 결과다. 커널 소스는 공용 랩의 `/var/lab/src/linux-6.18.55`(읽기 전용)이고, dtb는 `make O=/var/tmp/D5/build`로 따로 빌드했다. 런타임 DT 예제는 랩의 QEMU `virt` 머신 + Linux 6.18.55 + BusyBox initramfs에서 돌렸다. 시각·phandle 번호·md5 값은 실행마다 다를 수 있다.

---

## 1. 실제 체인 하나 고르기 — Toradex Verdin iMX8M Plus

### 1.1 왜 이 보드인가

교과서 예제로 흔히 NXP `imx8mp-evk.dts`를 쓰지만, EVK는 "SoC 벤더의 평가 보드"라서 **SoC dtsi → 보드 dts** 두 층뿐이다. 실무에서 Don이 만날 구조는 대개 세 층이다. 센서 키트 같은 장비 회사는 칩을 직접 PCB에 올리기보다 SoM(System on Module)을 사서 자기 캐리어 보드에 꽂는다. 그래서 업스트림 커널에서 **SoM 층이 따로 있는** 체인을 골랐다. Toradex Verdin iMX8M Plus는 같은 SoM에 Wi-Fi 유무 변형 2개, 캐리어 보드 5종이 있어서 BSP 정리법을 보기에도 좋다.

### 1.2 예제 1 — 파일 목록과 include 관계 확인

무엇을 확인하나: Verdin 관련 파일이 어떤 이름 규칙으로 몇 개 있고, 보드 dts가 무엇을 include하는지 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55/arch/arm64/boot/dts/freescale &&
  ls imx8mp-verdin* &&
  wc -l imx8mp.dtsi imx8mp-verdin.dtsi imx8mp-verdin-nonwifi.dtsi imx8mp-verdin-dev.dtsi \
        imx8mp-verdin-nonwifi-dev.dts imx8mp-evk.dts &&
  grep -n "#include" imx8mp-verdin-nonwifi-dev.dts imx8mp-verdin.dtsi imx8mp.dtsi imx8mp-evk.dts'
```

```text
imx8mp-verdin-dahlia.dtsi
imx8mp-verdin-dev.dtsi
imx8mp-verdin-ivy.dtsi
imx8mp-verdin-mallow.dtsi
imx8mp-verdin-nonwifi-dahlia.dts
imx8mp-verdin-nonwifi-dev.dts
imx8mp-verdin-nonwifi-ivy.dts
imx8mp-verdin-nonwifi-mallow.dts
imx8mp-verdin-nonwifi-yavia.dts
imx8mp-verdin-nonwifi.dtsi
imx8mp-verdin-wifi-dahlia.dts
imx8mp-verdin-wifi-dev.dts
imx8mp-verdin-wifi-ivy.dts
imx8mp-verdin-wifi-mallow.dts
imx8mp-verdin-wifi-yavia.dts
imx8mp-verdin-wifi.dtsi
imx8mp-verdin-yavia.dtsi
imx8mp-verdin.dtsi
  2451 imx8mp.dtsi
  1493 imx8mp-verdin.dtsi
    53 imx8mp-verdin-nonwifi.dtsi
   269 imx8mp-verdin-dev.dtsi
    18 imx8mp-verdin-nonwifi-dev.dts
  1193 imx8mp-evk.dts
  5477 total
imx8mp-verdin-nonwifi-dev.dts:8:#include "imx8mp-verdin.dtsi"
imx8mp-verdin-nonwifi-dev.dts:9:#include "imx8mp-verdin-nonwifi.dtsi"
imx8mp-verdin-nonwifi-dev.dts:10:#include "imx8mp-verdin-dev.dtsi"
imx8mp-verdin.dtsi:6:#include <dt-bindings/phy/phy-imx8-pcie.h>
imx8mp-verdin.dtsi:7:#include <dt-bindings/pwm/pwm.h>
imx8mp-verdin.dtsi:8:#include "imx8mp.dtsi"
imx8mp.dtsi:6:#include <dt-bindings/clock/imx8mp-clock.h>
imx8mp.dtsi:7:#include <dt-bindings/power/imx8mp-power.h>
imx8mp.dtsi:8:#include <dt-bindings/reset/imx8mp-reset.h>
imx8mp.dtsi:9:#include <dt-bindings/reset/imx8mp-reset-audiomix.h>
imx8mp.dtsi:10:#include <dt-bindings/gpio/gpio.h>
imx8mp.dtsi:11:#include <dt-bindings/input/input.h>
imx8mp.dtsi:12:#include <dt-bindings/interconnect/fsl,imx8mp.h>
imx8mp.dtsi:13:#include <dt-bindings/interrupt-controller/arm-gic.h>
imx8mp.dtsi:14:#include <dt-bindings/thermal/thermal.h>
imx8mp.dtsi:16:#include "imx8mp-pinfunc.h"
imx8mp-evk.dts:8:#include <dt-bindings/phy/phy-imx8-pcie.h>
imx8mp-evk.dts:9:#include "imx8mp.dtsi"
```

출력에서 볼 것: SoM 공통 `imx8mp-verdin.dtsi` 하나, SoM 변형 `-wifi`/`-nonwifi.dtsi` 둘, 캐리어 `-dev/-dahlia/-ivy/-mallow/-yavia.dtsi` 다섯, 그리고 그 조합인 `.dts` 열 개다. 보드 `.dts`는 겨우 18줄이고 include 세 줄 + `model`/`compatible`뿐이다. 반면 EVK는 SoM 층이 없어서 보드 `.dts` 하나에 1193줄이 몰려 있다.

실제 보드 파일 전체는 이렇다.

```dts
// SPDX-License-Identifier: GPL-2.0-or-later OR MIT
/*
 * Copyright 2022 Toradex
 */

/dts-v1/;

#include "imx8mp-verdin.dtsi"
#include "imx8mp-verdin-nonwifi.dtsi"
#include "imx8mp-verdin-dev.dtsi"

/ {
	model = "Toradex Verdin iMX8M Plus on Verdin Development Board";
	compatible = "toradex,verdin-imx8mp-nonwifi-dev",
		     "toradex,verdin-imx8mp-nonwifi",
		     "toradex,verdin-imx8mp",
		     "fsl,imx8mp";
};
```

`compatible`이 **가장 구체적인 것 → 가장 일반적인 것** 순서(보드 → SoM 변형 → SoM → SoC)인 점을 눈여겨보자. 커널과 U-Boot는 앞에서부터 매칭을 시도하므로, 보드 전용 처리가 없으면 SoC 공통 처리로 내려간다(D3에서 드라이버 쪽 매칭으로 다시 나온다).

### 1.3 파일 종류와 이름 규칙

| 확장자 / 이름 | 뜻 | 예 (실제 파일) |
|---|---|---|
| `.dtsi` | include 전용 조각. 단독으로 dtb가 되지 않는다 (`/dts-v1/;` 없음) | `imx8mp.dtsi`, `imx8mp-verdin.dtsi` |
| `.dts` | 보드 하나 = dtb 하나. `/dts-v1/;`로 시작 | `imx8mp-verdin-nonwifi-dev.dts` |
| `.dtso` | 오버레이 소스 → `.dtbo` (D6) | `imx8mp-evk-lvds0-imx-lvds-hdmi.dtso` |
| `.h` | 매크로 상수. `dt-bindings/` 아래 공용 또는 SoC 폴더 안 핀 정의 | `dt-bindings/clock/imx8mp-clock.h`, `imx8mp-pinfunc.h` |
| `<soc>.dtsi` | SoC 전체 | `imx8mp.dtsi`, `k3-am62.dtsi` |
| `<soc>-<도메인>.dtsi` | 큰 SoC를 전원 도메인별로 쪼갬 | `k3-am62-main.dtsi`, `k3-am62-mcu.dtsi`, `k3-am62-wakeup.dtsi` |
| `<부품번호>.dtsi` | 같은 다이의 파생 칩(코어 수·블록 유무) | `k3-am625.dtsi` → `k3-am62.dtsi` |
| `<soc>-<som>.dtsi` | SoM 공통 | `imx8mp-verdin.dtsi`, `k3-am62-verdin.dtsi` |
| `<soc>-<som>-<변형>-<캐리어>.dts` | 조합 하나 = 보드 하나 | `imx8mp-verdin-wifi-mallow.dts` |
| `<board>-u-boot.dtsi` | U-Boot 빌드 때만 자동으로 덧붙는 조각 (U-Boot 트리) | `arch/arm/dts/imx8mp-evk-u-boot.dtsi` |

TI 쪽도 같은 구조다. `k3-am625-sk.dts`가 끌어오는 파일을 빌드 의존성 파일(4절)에서 뽑으면 이렇다(`dt-bindings` 헤더는 생략).

```text
arch/arm64/boot/dts/ti/k3-am625-sk.dts
arch/arm64/boot/dts/ti/k3-am625.dtsi
arch/arm64/boot/dts/ti/k3-am62.dtsi
arch/arm64/boot/dts/ti/k3-pinctrl.h
arch/arm64/boot/dts/ti/k3-am62-thermal.dtsi
arch/arm64/boot/dts/ti/k3-am62-main.dtsi
arch/arm64/boot/dts/ti/k3-am62-mcu.dtsi
arch/arm64/boot/dts/ti/k3-am62-wakeup.dtsi
arch/arm64/boot/dts/ti/k3-am625-sk-common.dtsi
arch/arm64/boot/dts/ti/k3-am62x-sk-common.dtsi
arch/arm64/boot/dts/ti/k3-am62-ti-ipc-firmware.dtsi
```

출력에서 볼 것: `k3-am625.dtsi`(부품번호) → `k3-am62.dtsi`(패밀리) → `-main/-mcu/-wakeup`(전원 도메인)으로 SoC 층 자체가 여러 파일이고, 보드 층도 `k3-am625-sk-common.dtsi` → `k3-am62x-sk-common.dtsi`로 "같은 보드 계열 공통"을 뽑아 두었다. 층의 개수는 회사마다 다르지만 **공통 → 구체** 방향은 같다.

---

## 2. include — cpp가 먼저, dtc가 나중

### 2.1 직관 — `#include`는 그냥 텍스트 붙여넣기다

`.dts`의 `#include`는 C와 **완전히 같은 C 전처리기(cpp)** 가 처리한다. dtc(Device Tree Compiler)는 `#include`를 모른다. 커널 빌드는 먼저 `gcc -E`로 모든 include를 펼치고 매크로를 숫자로 바꾼 **하나의 거대한 텍스트 파일**(`.dts.tmp`)을 만든 다음, 그 파일을 dtc에 넘긴다.

그래서 include 계층의 의미는 오직 "**텍스트가 어떤 순서로 이어 붙느냐**"다. 보드 dts가 `imx8mp-verdin.dtsi`를 먼저, `imx8mp-verdin-dev.dtsi`를 나중에 include하면, 펼친 결과에서 SoC 내용 → SoM 내용 → 캐리어 내용 → 보드 `/ { }` 순서로 나온다. 3절의 "나중 것이 이긴다" 규칙은 이 순서에 대해 적용된다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="220" y="16" width="260" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="350" y="35" text-anchor="middle" font-size="13">imx8mp-verdin-nonwifi-dev.dts</text>
  <text x="350" y="52" text-anchor="middle" font-size="12">보드 · 18줄</text>
  <rect x="20" y="100" width="200" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="120" y="119" text-anchor="middle" font-size="13">imx8mp-verdin.dtsi</text>
  <text x="120" y="136" text-anchor="middle" font-size="12">SoM 공통 · 1493줄</text>
  <rect x="240" y="100" width="220" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="350" y="119" text-anchor="middle" font-size="13">imx8mp-verdin-nonwifi.dtsi</text>
  <text x="350" y="136" text-anchor="middle" font-size="12">SoM 변형 · 53줄</text>
  <rect x="480" y="100" width="185" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="572" y="119" text-anchor="middle" font-size="13">imx8mp-verdin-dev.dtsi</text>
  <text x="572" y="136" text-anchor="middle" font-size="12">캐리어 · 269줄</text>
  <line x1="300" y1="60" x2="140" y2="97" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="350" y1="60" x2="350" y2="97" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="400" y1="60" x2="560" y2="97" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="205" y="76" font-size="13">1</text>
  <text x="358" y="84" font-size="13">2</text>
  <text x="488" y="76" font-size="13">3</text>
  <rect x="20" y="184" width="200" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="203" text-anchor="middle" font-size="13">imx8mp.dtsi</text>
  <text x="120" y="220" text-anchor="middle" font-size="12">SoC · 2451줄</text>
  <rect x="235" y="184" width="245" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="357" y="203" text-anchor="middle" font-size="12">dt-bindings/phy/phy-imx8-pcie.h</text>
  <text x="357" y="220" text-anchor="middle" font-size="12">dt-bindings/pwm/pwm.h</text>
  <line x1="120" y1="144" x2="120" y2="181" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="180" y1="144" x2="280" y2="181" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="20" y="268" width="480" height="60" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="260" y="292" text-anchor="middle" font-size="12">dt-bindings/: clock/imx8mp-clock.h · power · reset ×2 · gpio</text>
  <text x="260" y="314" text-anchor="middle" font-size="12">input(+linux-event-codes) · interconnect · arm-gic(+irq) · thermal</text>
  <rect x="515" y="268" width="150" height="60" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="590" y="292" text-anchor="middle" font-size="13">imx8mp-pinfunc.h</text>
  <text x="590" y="314" text-anchor="middle" font-size="12">핀 mux 상수</text>
  <line x1="100" y1="228" x2="160" y2="265" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="200" y1="228" x2="570" y2="265" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="20" y="360" font-size="12">cpp가 1 → 2 → 3 순서로 텍스트를 펼쳐 이어 붙인다 → .dts.tmp 하나 (95,057 B) → dtc</text>
  <text x="20" y="382" font-size="12">회색 = C 매크로 헤더(#define만 있음). 이 헤더 일부는 커널 C 드라이버도 같이 include한다.</text>
</svg>
```
그림 2 — 실제 보드 `imx8mp-verdin-nonwifi-dev.dts`의 include 트리. 4.4절 의존성 파일(`.d.pre.tmp`)에서 뽑은 그대로다. 번호는 펼쳐지는 순서다.

### 2.2 `dt-bindings` 헤더 — DT와 드라이버가 같은 번호를 쓰게 하는 장치

DT에서 `clocks = <&clk IMX8MP_CLK_CAN1_ROOT>;`라고 쓰면 cpp가 매크로를 숫자로 바꾼다. 이 숫자는 클록 드라이버(`drivers/clk/imx/clk-imx8mp.c`)가 "233번 클록 = CAN1 루트 게이트"로 해석한다. 둘이 어긋나면 엉뚱한 클록이 켜진다. 그래서 숫자를 **한 헤더에 두고 DT와 C 드라이버가 같이 include**한다. 이 헤더들이 `include/dt-bindings/` 아래에 있고, 한번 정해진 번호는 바꾸면 안 되는 ABI다(이미 출시된 dtb가 그 숫자를 품고 있으므로).

예제 2 — 무엇을 확인하나: 자주 쓰는 매크로 정의, 드라이버도 같은 헤더를 쓰는지, dtc가 헤더를 찾는 경로를 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55 &&
  grep -n "define GPIO_ACTIVE_\(HIGH\|LOW\)\|define GIC_SPI\|define IRQ_TYPE_LEVEL_HIGH" \
       include/dt-bindings/gpio/gpio.h include/dt-bindings/interrupt-controller/*.h
  grep -n "IMX8MP_CLK_CAN1_ROOT\b\|IMX8MP_CLK_CAN1\b" include/dt-bindings/clock/imx8mp-clock.h
  ls include/dt-bindings | wc -l
  grep -rn "dt-bindings/clock/imx8mp-clock.h" drivers/clk/imx/ | head -2
  ls -la scripts/dtc/include-prefixes/ | grep dt-bindings'
```

```text
include/dt-bindings/gpio/gpio.h:14:#define GPIO_ACTIVE_HIGH 0
include/dt-bindings/gpio/gpio.h:15:#define GPIO_ACTIVE_LOW 1
include/dt-bindings/interrupt-controller/arm-gic.h:13:#define GIC_SPI 0
include/dt-bindings/interrupt-controller/irq.h:17:#define IRQ_TYPE_LEVEL_HIGH	4
124:#define IMX8MP_CLK_CAN1				116
238:#define IMX8MP_CLK_CAN1_ROOT			233
37
drivers/clk/imx/clk-imx8mp-audiomix.c:19:#include <dt-bindings/clock/imx8mp-clock.h>
drivers/clk/imx/clk-imx8mp.c:6:#include <dt-bindings/clock/imx8mp-clock.h>
lrwxrwxrwx 1 donh donh   28 Oct  3 03:39 dt-bindings -> ../../../include/dt-bindings
```

출력에서 볼 것: `IMX8MP_CLK_CAN1_ROOT = 233`을 클록 드라이버 `clk-imx8mp.c`도 같은 헤더로 쓴다. 커널 빌드는 `scripts/dtc/include-prefixes/`를 include 경로로 주는데, 그 안의 `dt-bindings`는 `include/dt-bindings`로 가는 심볼릭 링크다. 그래서 `#include <dt-bindings/...>`가 찾아진다. 같은 폴더에 `arm`, `arm64`, `riscv` 링크도 있어서 다른 아키텍처의 dtsi를 `<arm64/...>` 꼴로 include할 수도 있다.

Don에게 익숙한 말로: SSD 펌웨어에서 호스트 드라이버와 펌웨어가 같은 `nvme_spec.h`를 공유해 opcode 번호를 맞추는 것과 똑같다.

### 2.3 예제 3 — 3층짜리 작은 트리를 직접 만들어 cpp 결과 보기

실제 BSP는 너무 커서 규칙을 눈으로 확인하기 어렵다. 그래서 같은 구조의 작은 트리를 직접 만든다. 이 예제는 3절에서도 계속 쓴다. 노드 이름·라벨에는 노트 ID 접두어 `d5_`를 붙였다.

SoC 층 — 칩 안의 블록을 전부 적고 기본은 꺼 둔다. GPU만 SoC 기본값이 `okay`다.

```dts
// d5_soc.dtsi — SoC 층: 칩 안에 있는 모든 블록. 기본은 전부 disabled.
#include <dt-bindings/interrupt-controller/arm-gic.h>
#include <dt-bindings/gpio/gpio.h>

/ {
	#address-cells = <1>;
	#size-cells = <1>;
	compatible = "d5,soc";
	interrupt-parent = <&d5_gic>;

	soc {
		compatible = "simple-bus";
		#address-cells = <1>;
		#size-cells = <1>;
		ranges;

		d5_gic: interrupt-controller@8000 {
			compatible = "arm,gic-400";
			reg = <0x8000 0x1000>, <0x9000 0x2000>;
			interrupt-controller;
			#interrupt-cells = <3>;
		};

		d5_pinctrl: pinctrl@5000 {
			compatible = "d5,pinctrl";
			reg = <0x5000 0x100>;
			gpio-controller;
			#gpio-cells = <2>;
			/omit-if-no-ref/ d5_pins_uart1: uart1-grp { pins = "PA0", "PA1"; };
			/omit-if-no-ref/ d5_pins_uart2: uart2-grp { pins = "PB0", "PB1"; };
		};

		d5_uart1: serial@1000 {
			compatible = "d5,uart";
			reg = <0x1000 0x100>;
			interrupts = <GIC_SPI 10 IRQ_TYPE_LEVEL_HIGH>;
			status = "disabled";
		};

		d5_uart2: serial@2000 {
			compatible = "d5,uart";
			reg = <0x2000 0x100>;
			interrupts = <GIC_SPI 11 IRQ_TYPE_LEVEL_HIGH>;
			status = "disabled";
		};

		d5_i2c1: i2c@3000 {
			compatible = "d5,i2c";
			reg = <0x3000 0x100>;
			#address-cells = <1>;
			#size-cells = <0>;
			clock-frequency = <100000>;
			status = "disabled";
		};

		d5_gpu: gpu@4000 {
			compatible = "d5,gpu";
			reg = <0x4000 0x1000>;
			status = "okay";
		};
	};
};
```

SoM 층 — 모듈 위의 DRAM, PMIC, 콘솔 UART를 켠다.

```dts
// d5_som.dtsi — SoM 층: 모듈 위에 납땜된 것(DRAM, PMIC, 콘솔 UART).
#include "d5_soc.dtsi"

/ {
	model = "D5 SoM";
	memory@80000000 {
		device_type = "memory";
		reg = <0x80000000 0x40000000>;
	};
};

&d5_uart1 {
	pinctrl-names = "default";
	pinctrl-0 = <&d5_pins_uart1>;
	status = "okay";
};

&d5_i2c1 {
	status = "okay";
	d5_pmic: pmic@32 {
		compatible = "d5,pmic";
		reg = <0x32>;
	};
};
```

캐리어 보드 층 — 센서 키트 rev B. IMU를 I2C에 달고, 버스 속도를 바꾸고, 없는 GPU를 지운다.

```dts
// d5_board.dts — 캐리어 보드 층: 센서 키트 rev B.
/dts-v1/;
#include "d5_som.dtsi"

/ {
	model = "D5 Sensor Kit carrier rev B";
	compatible = "d5,sensorkit-revb", "d5,som", "d5,soc";
	chosen { stdout-path = &d5_uart1; };
};

&d5_i2c1 {
	clock-frequency = <400000>;	/* SoC 기본 100 kHz → 보드에서 400 kHz */
	d5_imu: imu@68 {
		compatible = "d5,imu";
		reg = <0x68>;
		int-gpios = <&d5_pinctrl 3 GPIO_ACTIVE_LOW>;
	};
};

/delete-node/ &d5_gpu;	/* 이 캐리어에는 디스플레이가 없다 */

&d5_i2c1 {
	clock-frequency = <1000000>;	/* 같은 파일 안에서도 나중 것이 이긴다 */
};
```

빌드는 커널 `scripts/Makefile.dtbs`의 `cmd_dtc`(4절)와 같은 두 단계를 셸 스크립트로 흉내 낸다.

```sh
#!/bin/bash
# build_ex2.sh <src.dts> <outdir> — 커널 scripts/Makefile.dtbs와 같은 cpp → dtc 2단계
set -e
SRC=$1; OUT=$2; mkdir -p $OUT
K=/var/lab/src/linux-6.18.55
B=$(basename $SRC .dts)
gcc -E -nostdinc -I $K/scripts/dtc/include-prefixes -undef -D__DTS__ \
    -x assembler-with-cpp -o $OUT/$B.dts.tmp $SRC
dtc -I dts -O dtb -b 0 -i $(dirname $SRC) -o $OUT/$B.dtb $OUT/$B.dts.tmp
ls -l $OUT/$B.dtb
```

먼저 cpp 단계의 결과(`.dts.tmp`)만 들여다본다. 무엇을 확인하나: include가 어떤 순서로 펼쳐지고 매크로가 어떤 숫자가 되는지.

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.elinux_scratch/D5; $S/build_ex2.sh $S/ex2/d5_board.dts /var/tmp/D5/ex2 &&
  cd /var/tmp/D5/ex2 && wc -l d5_board.dts.tmp &&
  grep -n "^#\|interrupts\|int-gpios\|interrupt-parent" d5_board.dts.tmp'
# 출력의 긴 경로는 읽기 쉽게 $K(커널 소스)와 파일 이름만으로 줄였다
```

```text
-rw-rw-r-- 1 donh donh 1443 Oct 10 10:40 /var/tmp/D5/ex2/d5_board.dtb
121 d5_board.dts.tmp
1:# 0 "d5_board.dts"
2:# 0 "<built-in>"
3:# 0 "<command-line>"
4:# 1 "d5_board.dts"
7:# 1 "d5_som.dtsi" 1
9:# 1 "d5_soc.dtsi" 1
11:# 1 "$K/scripts/dtc/include-prefixes/dt-bindings/interrupt-controller/arm-gic.h" 1
12:# 9 "$K/scripts/dtc/include-prefixes/dt-bindings/interrupt-controller/arm-gic.h"
13:# 1 "$K/scripts/dtc/include-prefixes/dt-bindings/interrupt-controller/irq.h" 1
14:# 10 "$K/scripts/dtc/include-prefixes/dt-bindings/interrupt-controller/arm-gic.h" 2
15:# 3 "d5_soc.dtsi" 2
16:# 1 "$K/scripts/dtc/include-prefixes/dt-bindings/gpio/gpio.h" 1
17:# 4 "d5_soc.dtsi" 2
23: interrupt-parent = <&d5_gic>;
50:   interrupts = <0 10 4>;
57:   interrupts = <0 11 4>;
77:# 3 "d5_som.dtsi" 2
100:# 4 "d5_board.dts" 2
113:  int-gpios = <&d5_pinctrl 3 1>;
```

출력에서 볼 것: `# 1 "파일" 1`(파일 진입)과 `# N "파일" 2`(복귀)는 cpp의 **linemarker**다. dtc는 이것을 읽어서 에러 메시지에 원래 파일 이름과 줄 번호를 찍는다(3.7절 에러 출력에서 `bad_label.dts:3.1-10`처럼 나오는 이유). `GIC_SPI 10 IRQ_TYPE_LEVEL_HIGH`는 `0 10 4`로, `GPIO_ACTIVE_LOW`는 `1`로 바뀌었다. 반면 `&d5_gic` 같은 라벨 참조는 cpp가 건드리지 않고 dtc에게 그대로 넘어간다. 즉 **매크로는 cpp의 몫, 라벨은 dtc의 몫**이다.

### 2.4 커널이 cpp에 주는 옵션의 의미 — 특히 `-undef`

| 옵션 | 왜 필요한가 |
|---|---|
| `-nostdinc` | `/usr/include` 같은 호스트 시스템 헤더를 실수로 끌어오지 않게 한다 |
| `-I scripts/dtc/include-prefixes` | `<dt-bindings/...>`, `<arm64/...>`를 찾는 유일한 경로 |
| `-undef` | gcc가 미리 정의하는 시스템 매크로(`linux`, `unix` 등)를 없앤다 |
| `-D__DTS__` | 공용 헤더 안에서 "지금 DT에서 include됐다"를 구분할 때 쓴다 |
| `-x assembler-with-cpp` | 입력을 C가 아니라 "cpp를 거치는 어셈블리"로 취급. C 문법 검사를 하지 않고, `#`로 시작하는 DT 문법(`#address-cells`)과 충돌을 줄인다 |

`-undef`가 왜 필요한지는 실행해 보면 바로 보인다. 예제 4 — 무엇을 확인하나: DT 속성 이름에 흔한 `linux,` 접두어가 cpp를 지나면서 어떻게 되는지.

```sh
limactl shell elinux -- bash -lc 'printf "linux,code = <1>;\nunix = <2>;\n" > /var/tmp/D5/undef.dts
  echo "### without -undef"; gcc -E -P -x assembler-with-cpp /var/tmp/D5/undef.dts
  echo "### with -undef";    gcc -E -P -undef -x assembler-with-cpp /var/tmp/D5/undef.dts'
```

```text
### without -undef
1,code = <1>;
1 = <2>;
### with -undef
linux,code = <1>;
unix = <2>;
```

출력에서 볼 것: GNU 모드 gcc는 Linux 호스트에서 `linux`를 `1`로 미리 정의한다. `-undef`가 없으면 `linux,code`(키 코드 속성, 실제로 `imx8mp-verdin.dtsi:75`에 `linux,code = <KEY_WAKEUP>;`가 있다)가 `1,code`로 망가진다. 커널 아래 arm64 dts 중 `linux,`가 들어간 파일이 719개다. 사내 빌드 스크립트로 dts를 직접 cpp에 돌릴 때 이 옵션을 빼먹으면 이런 이상한 에러를 만난다.

### 2.5 `/include/` — dtc 자체 include (매크로 없음)

dtc에도 자체 include 문법 `/include/ "file"`이 있다. cpp가 생기기 전의 옛 방식이다. 차이는 하나다: **`/include/`로 읽은 파일은 cpp를 거치지 않는다.** 커널 빌드에서 cpp는 dtc보다 먼저 한 번만 돌기 때문이다.

예제 5 — 무엇을 확인하나: rev C 보드 차이분을 `/include/`로 붙이면 매크로가 해석되지 않아 실패하고, `#include`로 바꾸면 성공하는 것.

```dts
// d5_rev_c.dtsi — rev C 보드 차이분. dtc의 /include/ 로 읽힌다 (cpp를 거치지 않음).
&d5_imu {
	int-gpios = <&d5_pinctrl 4 GPIO_ACTIVE_LOW>;
};
```

```sh
# d5_board_revc.dts  : /dts-v1/;  #include "d5_board.dts"  /include/ "d5_rev_c.dtsi"
# d5_board_revc2.dts : /dts-v1/;  #include "d5_board.dts"  #include "d5_rev_c.dtsi"
limactl shell elinux -- bash -lc 'S=/Users/donh/.elinux_scratch/D5;
  $S/build_ex2.sh $S/ex2/d5_board_revc.dts /var/tmp/D5/ex2;
  $S/build_ex2.sh $S/ex2/d5_board_revc2.dts /var/tmp/D5/ex2 &&
  fdtget -t x /var/tmp/D5/ex2/d5_board_revc2.dtb /soc/i2c@3000/imu@68 int-gpios &&
  fdtget -t x /var/tmp/D5/ex2/d5_board.dtb      /soc/i2c@3000/imu@68 int-gpios'
```

```text
Error: /Users/donh/.elinux_scratch/D5/ex2/d5_rev_c.dtsi:3.29-30 syntax error
FATAL ERROR: Unable to parse input tree
-rw-rw-r-- 1 donh donh 1443 Oct 10 10:40 /var/tmp/D5/ex2/d5_board_revc2.dtb
3 4 1
3 3 1
```

출력에서 볼 것: `/include/` 쪽은 3행 29열, 즉 `GPIO_ACTIVE_LOW` 글자에서 syntax error가 난다. `#include`로 바꾸면 rev C dtb의 IMU 인터럽트 핀이 3번에서 4번으로 바뀐다(`3 4 1` = phandle 3인 pinctrl의 4번 핀, active-low). 커널 arm64 트리에서 `/include/`는 6개 파일 8곳(apm, amd, cavium의 오래된 보드)에만 남아 있다. **새 코드에서는 항상 `#include`를 쓴다.** 단, `/include/`는 dtc가 처리하므로 cpp 없이 dtc만 쓰는 환경(일부 오버레이 빌드 스크립트)에서는 여전히 보인다.

> 주의: `/include/` 경로는 **dtc가 읽는 파일(`.dts.tmp`) 기준**으로 찾는다. 이 tmp 파일은 빌드 출력 폴더에 있으므로, 커널은 `dtc -i $(dir $<)`로 원래 소스 폴더를 검색 경로에 추가해 준다. 위 스크립트의 `-i $(dirname $SRC)`도 같은 이유다.

---

## 3. Override — 라벨로 노드를 다시 열어 덮어쓰기

### 3.1 직관 — 같은 노드를 여러 번 열면 dtc가 합친다

DT 소스에서 같은 노드를 여러 번 정의해도 에러가 아니다. dtc는 같은 경로의 노드 정의를 **차례로 병합**한다. 규칙은 네 줄이다.

1. 처음 보는 속성은 그 노드의 속성 목록 **끝에 추가**된다.
2. 이미 있는 속성을 다시 쓰면 **값만 바뀌고 자리는 그대로**다. 나중에 나온 값이 이긴다.
3. 자식 노드도 같은 규칙으로 재귀 병합된다.
4. "나중"의 기준은 cpp가 펼친 **텍스트 순서**다 (파일 계층이 아니라).

노드를 다시 여는 방법은 세 가지다.

| 문법 | 뜻 | 언제 쓰나 |
|---|---|---|
| `&label { ... };` | 라벨이 붙은 노드를 다시 연다 (루트 레벨에 쓴다) | 가장 흔한 방식. SoC dtsi의 `uart4:` 라벨을 보드에서 연다 |
| `&{/soc/serial@2000} { ... };` | 전체 경로로 다시 연다 | 라벨이 없는 노드. U-Boot `-u-boot.dtsi`에서 자주 보인다 |
| `/ { soc { serial@2000 { ... }; }; };` | 트리를 루트부터 다시 써 내려간다 | 옛 스타일. 경로가 길고 오타에 약해서 요즘은 잘 안 쓴다 |

지우거나 빼는 지시어도 있다.

| 지시어 | 뜻 |
|---|---|
| `/delete-node/ &label;` | 라벨이 붙은 노드를 통째로 지운다 (루트 레벨) |
| `/delete-node/ name@addr;` | 현재 노드 안에서 이름으로 자식을 지운다 |
| `/delete-property/ prop;` | 현재 노드에서 속성 하나를 지운다 |
| `/omit-if-no-ref/` | 노드 정의 앞에 붙인다. 최종 트리에서 아무도 phandle로 참조하지 않으면 그 노드를 dtb에서 뺀다 |

### 3.2 예제 6 — 3층 트리를 컴파일해 병합 결과 보기

무엇을 확인하나: 2.3절의 세 파일을 dtb로 만든 뒤 다시 소스로 풀어서(decompile) 위 규칙들이 실제로 어떻게 적용됐는지 본다.

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.elinux_scratch/D5;
  $S/build_ex2.sh $S/ex2/d5_board.dts /var/tmp/D5/ex2 && dtc -I dtb -O dts /var/tmp/D5/ex2/d5_board.dtb'
```

```text
-rw-rw-r-- 1 donh donh 1443 Oct 10 10:35 /var/tmp/D5/ex2/d5_board.dtb
/dts-v1/;

/ {
	#address-cells = <0x01>;
	#size-cells = <0x01>;
	compatible = "d5,sensorkit-revb", "d5,som", "d5,soc";
	interrupt-parent = <0x01>;
	model = "D5 Sensor Kit carrier rev B";

	soc {
		compatible = "simple-bus";
		#address-cells = <0x01>;
		#size-cells = <0x01>;
		ranges;

		interrupt-controller@8000 {
			compatible = "arm,gic-400";
			reg = <0x8000 0x1000 0x9000 0x2000>;
			interrupt-controller;
			#interrupt-cells = <0x03>;
			phandle = <0x01>;
		};

		pinctrl@5000 {
			compatible = "d5,pinctrl";
			reg = <0x5000 0x100>;
			gpio-controller;
			#gpio-cells = <0x02>;
			phandle = <0x03>;

			uart1-grp {
				pins = "PA0", "PA1";
				phandle = <0x02>;
			};
		};

		serial@1000 {
			compatible = "d5,uart";
			reg = <0x1000 0x100>;
			interrupts = <0x00 0x0a 0x04>;
			status = "okay";
			pinctrl-names = "default";
			pinctrl-0 = <0x02>;
		};

		serial@2000 {
			compatible = "d5,uart";
			reg = <0x2000 0x100>;
			interrupts = <0x00 0x0b 0x04>;
			status = "disabled";
		};

		i2c@3000 {
			compatible = "d5,i2c";
			reg = <0x3000 0x100>;
			#address-cells = <0x01>;
			#size-cells = <0x00>;
			clock-frequency = <0xf4240>;
			status = "okay";

			pmic@32 {
				compatible = "d5,pmic";
				reg = <0x32>;
			};

			imu@68 {
				compatible = "d5,imu";
				reg = <0x68>;
				int-gpios = <0x03 0x03 0x01>;
			};
		};
	};

	memory@80000000 {
		device_type = "memory";
		reg = <0x80000000 0x40000000>;
	};

	chosen {
		stdout-path = "/soc/serial@1000";
	};
};
```

출력에서 하나씩 짚어 보자.

- **값 덮어쓰기**: `i2c@3000`의 `clock-frequency`는 SoC 100000 → 보드 400000 → 같은 보드 파일 아래쪽 1000000을 거쳐 `0xf4240`(= 1,000,000)이 됐다. 같은 파일 안에서도 텍스트상 나중 것이 이긴다.
- **자리 유지 + 끝에 추가**: `serial@1000`에서 `status = "okay"`는 SoC에서 정의된 자리(`interrupts` 다음)에 있고, SoM이 새로 넣은 `pinctrl-names`, `pinctrl-0`은 그 뒤에 붙었다. 루트의 `model`도 SoM이 처음 넣었으므로 SoC의 속성들 뒤에 있다.
- **자식 병합**: `i2c@3000` 아래에 SoM의 `pmic@32`와 보드의 `imu@68`이 나란히 들어갔다.
- **`/delete-node/`**: SoC에서 `okay`였던 `gpu@4000`이 아예 없다.
- **`/omit-if-no-ref/`**: `uart1-grp`는 `pinctrl-0`이 참조하므로 남았고, 아무도 참조하지 않은 `uart2-grp`는 빠졌다.
- **라벨은 사라진다**: `d5_uart1:` 같은 라벨은 dtb에 없다. 참조되는 노드에는 `phandle = <N>` 숫자가 붙고, `&d5_pins_uart1` 참조는 `<0x02>`로 바뀌었다. `stdout-path = &d5_uart1`처럼 문자열 자리에 쓴 참조는 경로 문자열 `"/soc/serial@1000"`이 됐다.
- **매크로도 사라진다**: `interrupts`는 `<0x00 0x0a 0x04>`다. decompile로는 `GIC_SPI`를 되살릴 수 없다.

### 3.3 실제 보드에서 — CAN 1(`flexcan1`)은 세 파일에 걸쳐 완성된다

센서 키트의 CAN 모터 컨트롤러를 생각하며 i.MX8MP의 첫 번째 CAN 컨트롤러가 어떻게 켜지는지 따라가 본다. 예제 7 — 무엇을 확인하나: 같은 노드를 정의·재정의하는 세 곳의 소스.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55/arch/arm64/boot/dts/freescale &&
  grep -n -A13 "flexcan1: can@" imx8mp.dtsi; grep -n -A4 "^&flexcan1 {" imx8mp-verdin.dtsi imx8mp-verdin-dev.dtsi'
```

```text
1124:				flexcan1: can@308c0000 {
1125-					compatible = "fsl,imx8mp-flexcan";
1126-					reg = <0x308c0000 0x10000>;
1127-					interrupts = <GIC_SPI 142 IRQ_TYPE_LEVEL_HIGH>;
1128-					clocks = <&clk IMX8MP_CLK_IPG_ROOT>,
1129-						 <&clk IMX8MP_CLK_CAN1_ROOT>;
1130-					clock-names = "ipg", "per";
1131-					assigned-clocks = <&clk IMX8MP_CLK_CAN1>;
1132-					assigned-clock-parents = <&clk IMX8MP_SYS_PLL1_40M>;
1133-					assigned-clock-rates = <40000000>;
1134-					fsl,clk-source = /bits/ 8 <0>;
1135-					fsl,stop-mode = <&gpr 0x10 4>;
1136-					status = "disabled";
1137-				};
imx8mp-verdin.dtsi:338:&flexcan1 {
imx8mp-verdin.dtsi-339-	pinctrl-names = "default";
imx8mp-verdin.dtsi-340-	pinctrl-0 = <&pinctrl_flexcan1>;
imx8mp-verdin.dtsi-341-	status = "disabled";
imx8mp-verdin.dtsi-342-};
--
imx8mp-verdin-dev.dtsi:100:&flexcan1 {
imx8mp-verdin-dev.dtsi-101-	status = "okay";
imx8mp-verdin-dev.dtsi-102-};
imx8mp-verdin-dev.dtsi-103-
imx8mp-verdin-dev.dtsi-104-&flexcan2 {
```

출력에서 볼 것: SoC 층은 **칩이 아는 것**(주소, 인터럽트 번호, 클록, 40 MHz 클록 소스)을 적고 꺼 둔다. SoM 층은 **모듈이 아는 것**(이 CAN이 SoM 커넥터의 어느 핀으로 나가는지 = pinctrl)을 덧붙이지만 여전히 꺼 둔다. 모듈만으로는 CAN 트랜시버가 있는지 모르기 때문이다. 캐리어 층이 **"이 보드에는 트랜시버가 달려 있다"** 는 사실 하나, `status = "okay"`만 쓴다.

이제 최종 dtb를 확인한다. 예제 8 — 무엇을 확인하나: 빌드한 보드 dtb를 decompile해서 병합된 `flexcan1` 노드를 본다(빌드 방법은 4절).

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/D5/build/arch/arm64/boot/dts/freescale &&
  dtc -I dtb -O dts -o /var/tmp/D5/nonwifi-dev.dts imx8mp-verdin-nonwifi-dev.dtb 2>&1 | head -1;
  sed -n 1500,1515p /var/tmp/D5/nonwifi-dev.dts'
```

```text
/var/tmp/D5/nonwifi-dev.dts: Warning (unique_unit_address): /soc@0/pcie@33800000: duplicate unit-address (also used in node /soc@0/pcie-ep@33800000)
				can@308c0000 {
					compatible = "fsl,imx8mp-flexcan";
					reg = <0x308c0000 0x10000>;
					interrupts = <0x00 0x8e 0x04>;
					clocks = <0x02 0x6e 0x02 0xe9>;
					clock-names = "ipg", "per";
					assigned-clocks = <0x02 0x74>;
					assigned-clock-parents = <0x02 0x30>;
					assigned-clock-rates = <0x2625a00>;
					fsl,clk-source = [00];
					fsl,stop-mode = <0x3f 0x10 0x04>;
					status = "okay";
					pinctrl-names = "default";
					pinctrl-0 = <0x40>;
				};
```

출력에서 볼 것: 3.2절의 작은 예제와 똑같은 모양이다. `status`는 SoC가 정의한 자리에 있지만 값은 캐리어의 `"okay"`, SoM의 `pinctrl-*`은 끝에 붙었다. `0xe9` = 233 = `IMX8MP_CLK_CAN1_ROOT`, `0x8e` = 142 = SPI 142, `0x74` = 116 = `IMX8MP_CLK_CAN1`로 2.2절 헤더의 숫자와 맞는다. 첫 줄 경고는 SoC dtsi에 PCIe RC와 EP 노드가 같은 주소로 둘 다 있어서 나오는 것으로, 커널 빌드는 `-Wno-unique_unit_address`로 이 경고를 끈다(4.3절). 시스템 `dtc`로 직접 decompile할 때만 보인다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="310" height="112" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="32" y="40" font-size="12">imx8mp.dtsi:1124 — flexcan1: can@308c0000</text>
  <text x="32" y="64" font-size="12">compatible · reg · interrupts</text>
  <text x="32" y="84" font-size="12">clocks · clock-names · assigned-clock-*</text>
  <text x="32" y="104" font-size="12">fsl,clk-source · fsl,stop-mode</text>
  <text x="32" y="124" font-size="12">status = "disabled"</text>
  <rect x="20" y="150" width="310" height="86" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="32" y="170" font-size="12">imx8mp-verdin.dtsi:338 — &amp;flexcan1</text>
  <text x="32" y="194" font-size="12">pinctrl-names · pinctrl-0 (새 속성)</text>
  <text x="32" y="216" font-size="12">status = "disabled" (같은 값)</text>
  <rect x="20" y="254" width="310" height="62" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="32" y="274" font-size="12">imx8mp-verdin-dev.dtsi:100 — &amp;flexcan1</text>
  <text x="32" y="298" font-size="12">status = "okay" (덮어씀)</text>
  <line x1="330" y1="76" x2="376" y2="110" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="330" y1="193" x2="376" y2="250" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="330" y1="285" x2="376" y2="232" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="352" y="150" text-anchor="middle" font-size="12">순서대로</text>
  <rect x="380" y="20" width="280" height="296" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="520" y="40" text-anchor="middle" font-size="12">최종 dtb의 can@308c0000 (속성 순서 그대로)</text>
  <rect x="394" y="56" width="10" height="10" fill="#4a7bd0"/><text x="412" y="65" font-size="12">compatible</text>
  <rect x="394" y="74" width="10" height="10" fill="#4a7bd0"/><text x="412" y="83" font-size="12">reg</text>
  <rect x="394" y="92" width="10" height="10" fill="#4a7bd0"/><text x="412" y="101" font-size="12">interrupts</text>
  <rect x="394" y="110" width="10" height="10" fill="#4a7bd0"/><text x="412" y="119" font-size="12">clocks · clock-names</text>
  <rect x="394" y="128" width="10" height="10" fill="#4a7bd0"/><text x="412" y="137" font-size="12">assigned-clocks · -parents · -rates</text>
  <rect x="394" y="146" width="10" height="10" fill="#4a7bd0"/><text x="412" y="155" font-size="12">fsl,clk-source · fsl,stop-mode</text>
  <rect x="394" y="170" width="10" height="10" fill="#3f9a6b"/><text x="412" y="179" font-size="12">status = "okay"</text>
  <text x="412" y="197" font-size="12">값은 캐리어, 자리는 SoC</text>
  <rect x="394" y="220" width="10" height="10" fill="#e08a3c"/><text x="412" y="229" font-size="12">pinctrl-names</text>
  <rect x="394" y="238" width="10" height="10" fill="#e08a3c"/><text x="412" y="247" font-size="12">pinctrl-0</text>
  <text x="412" y="268" font-size="12">새 속성은 끝에 붙는다</text>
  <text x="20" y="350" font-size="12">같은 속성은 나중 값이 덮고(자리는 유지), 처음 보는 속성은 노드 끝에 추가된다.</text>
  <text x="20" y="370" font-size="12">라벨 flexcan1은 dtb에 남지 않는다 (-@로 빌드하면 /__symbols__에만 남는다).</text>
</svg>
```
그림 3 — 라벨 override 해석. 세 층이 같은 노드를 열고, dtc가 cpp가 펼친 텍스트 순서대로 병합한다. 색은 그 속성을 마지막으로 쓴 층이다.

### 3.4 예제 9 — 같은 SoC 노드가 보드마다 다르게 켜진다

무엇을 확인하나: 같은 `imx8mp.dtsi`를 쓰는 7개 보드 dtb에서 CAN 2개, UART4, SPI1의 최종 `status`를 `fdtget`으로 표로 뽑는다. `fdtget -d`는 속성이 없을 때 대신 찍을 기본값이다.

```sh
#!/bin/bash
# status_table.sh — 같은 SoC 노드가 보드(dtb)마다 켜졌는지 fdtget으로 표 만들기
cd /var/tmp/D5/build/arch/arm64/boot/dts/freescale
S=/soc@0/bus@30800000
declare -A P=([flexcan1]=$S/spba-bus@30800000/can@308c0000
              [flexcan2]=$S/spba-bus@30800000/can@308d0000
              [uart4]=$S/serial@30a60000
              [ecspi1]=$S/spba-bus@30800000/spi@30820000)
K="flexcan1 flexcan2 uart4 ecspi1"
printf "%-29s" board; for k in $K; do printf "%-10s" $k; done; echo
for b in imx8mp-evk imx8mp-verdin-{nonwifi,wifi}-dev imx8mp-verdin-nonwifi-{dahlia,ivy,mallow,yavia}; do
  printf "%-29s" $b
  for k in $K; do printf "%-10s" $(fdtget -t s -d "(none)" $b.dtb ${P[$k]} status); done; echo
done
```

```text
board                        flexcan1  flexcan2  uart4     ecspi1    
imx8mp-evk                   okay      disabled  disabled  disabled  
imx8mp-verdin-nonwifi-dev    okay      okay      disabled  okay      
imx8mp-verdin-wifi-dev       okay      okay      okay      okay      
imx8mp-verdin-nonwifi-dahlia okay      okay      disabled  okay      
imx8mp-verdin-nonwifi-ivy    okay      okay      disabled  okay      
imx8mp-verdin-nonwifi-mallow okay      okay      disabled  okay      
imx8mp-verdin-nonwifi-yavia  okay      disabled  disabled  okay      
```

출력에서 볼 것: Yavia 캐리어에는 CAN 2가 꺼져 있다(그 캐리어에 두 번째 트랜시버가 없거나 핀을 다른 용도로 쓴다). UART4는 Wi-Fi 변형에서만 켜지는데, 3.6절 diff에서 보듯 Wi-Fi 모듈의 Bluetooth가 UART4를 쓰기 때문이다. 같은 칩이라도 "켜짐"은 **최종 보드 dtb를 봐야만** 알 수 있다. SoC dtsi만 보고 판단하면 틀린다.

> `fdtget`은 **경로**를 받는다. 라벨(`flexcan1`)이나 `/aliases`에 없는 이름을 주면 `FDT_ERR_NOTFOUND`가 난다. 경로를 모르면 decompile 결과에서 찾거나, 4.6절처럼 `-@`로 빌드된 dtb의 `/__symbols__`에서 라벨 → 경로를 조회한다.

### 3.5 지우기와 경로로 열기 — `/delete-property/`, `/delete-node/`, `&{/path}`

예제 10 — 무엇을 확인하나: 라벨 대신 이름·경로로 지우고 여는 방법.

```dts
/dts-v1/;
#include "../ex2/d5_som.dtsi"

&d5_i2c1 {
	/delete-property/ clock-frequency;	/* 드라이버 기본값을 쓰게 한다 */
	/delete-node/ pmic@32;			/* 라벨 없이 이름으로 자식 삭제 */
};

&{/soc/serial@2000} {				/* 라벨이 없을 때: 경로로 다시 열기 */
	status = "okay";
};
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.elinux_scratch/D5; $S/build_ex2.sh $S/ex4/delprop.dts /var/tmp/D5/ex4 >/dev/null &&
  cd /var/tmp/D5/ex4 && fdtget -d "(deleted)" delprop.dtb /soc/i2c@3000 clock-frequency;
  fdtget -l delprop.dtb /soc/i2c@3000; echo "children: $(fdtget -l delprop.dtb /soc/i2c@3000 | wc -l)";
  fdtget -t s delprop.dtb /soc/serial@2000 status'
```

```text
/Users/donh/.elinux_scratch/D5/ex4/../ex2/d5_soc.dtsi:47.21-54.5: Warning (avoid_unnecessary_addr_size): /soc/i2c@3000: unnecessary #address-cells/#size-cells without "ranges", "dma-ranges" or child "reg" property
  also defined at /Users/donh/.elinux_scratch/D5/ex4/../ex2/d5_som.dtsi:18.10-24.3
  also defined at /Users/donh/.elinux_scratch/D5/ex4/delprop.dts:4.10-7.3
(deleted)
children: 0
okay
```

출력에서 볼 것: `clock-frequency`가 없어졌고 PMIC 자식을 지워서 I2C 노드에 자식이 0개다. dtc가 "자식에 `reg`가 없는데 `#address-cells`가 왜 있나"라고 경고하면서 **이 노드가 정의된 세 곳**(`also defined at`)을 모두 알려 준다. 이 목록이 "이 노드를 누가 건드렸나"를 찾는 가장 빠른 단서다. 커널 빌드는 이 경고를 기본으로 끈다(`-Wno-avoid_unnecessary_addr_size`, 4.3절). `&{/soc/serial@2000}`로 라벨 없는 UART2도 켰다.

실제 트리에서는 이렇게 쓰인다.

```text
ti/k3-am62d2.dtsi:19:/delete-node/ &vpu;   /* Video Codec is disabled in AM62D2 SoC */
ti/k3-am62d2.dtsi:20:/delete-node/ &e5010; /* JPEG Encoder is disabled in AM62D2 SoC */
freescale/imx8mp-verdin.dtsi:187:		/delete-node/ linux,cma;
freescale/imx8mp-skov-reva.dtsi:178:	/delete-property/ opp-suspend;
```

`k3-am62d2.dtsi`는 AM62A7 SoC dtsi를 include한 뒤, 그 파생 칩에 실제로 없는 비디오 코덱과 JPEG 인코더를 지운다. **파생 칩 = 부모 칩 dtsi + 삭제**라는 흔한 패턴이다. `imx8mp-verdin.dtsi`는 SoC dtsi가 잡아 둔 CMA 영역을 지우고 커널 설정값을 쓰게 한다. 커널 arm64 트리 전체에서 `/delete-node/`는 163개 파일 514곳, `/delete-property/`는 132개 파일 254곳에 쓰인다.

> `status = "disabled"`와 `/delete-node/`의 차이: disabled는 노드가 dtb에 **남아 있고** 드라이버만 붙지 않는다. 다른 노드가 그 노드를 phandle로 참조해도 문제없다. delete는 노드가 **사라지므로** 누가 참조하면 빌드 에러다(3.6절). 그래서 보드에서 장치를 끌 때는 거의 항상 `status = "disabled"`를 쓰고, 칩에 물리적으로 없는 블록처럼 "존재 자체가 틀린" 경우에만 delete를 쓴다.

### 3.6 `/omit-if-no-ref/` — 안 쓰는 핀 그룹을 dtb에서 빼기

SoC 벤더는 "이 칩의 UART2는 이 핀 조합 A, B, C 중 하나로 낼 수 있다"를 전부 pinctrl 그룹 노드로 미리 적어 둔다. 보드는 그중 하나만 참조한다. 나머지가 dtb에 다 들어가면 크기만 커진다. `/omit-if-no-ref/`를 붙인 노드는 최종 트리에서 아무도 참조하지 않으면 dtc가 뺀다(3.2절 `uart2-grp`).

커널 arm64 트리에서 `/omit-if-no-ref/`는 12개 파일에 2011번 쓰이고, 상위는 전부 Rockchip pinctrl dtsi다(`rk3576-pinctrl.dtsi` 690, `rk3588-base-pinctrl.dtsi` 433, `rk3568-pinctrl.dtsi` 335). 이 지시어는 **라벨로 참조되는 것을 전제**로 한다. 오버레이(D6)가 나중에 그 핀 그룹을 참조할 예정이라면 베이스 dtb에서 이미 빠져 버려서 오버레이 적용이 실패한다. 그래서 오버레이를 쓰는 플랫폼은 이 지시어를 조심해서 쓴다.

### 3.7 예제 11 — 자주 나는 실수 세 가지

무엇을 확인하나: 없는 라벨 재정의, 지운 노드 참조, `status` 오타가 각각 어떻게 드러나는지.

```sh
# bad_label.dts   : &d5_uart9 { status = "okay"; };          (없는 라벨)
# bad_deleted.dts : /delete-node/ &d5_gpu;  / { display { gpu = <&d5_gpu>; }; };
# typo_status.dts : &d5_uart2 { status = "okey"; };          (오타)
limactl shell elinux -- bash -lc 'S=/Users/donh/.elinux_scratch/D5; for f in bad_label bad_deleted typo_status; do
  echo "### $f"; $S/build_ex2.sh $S/ex4/$f.dts /var/tmp/D5/ex4 2>&1; done;
  fdtget -t s /var/tmp/D5/ex4/typo_status.dtb /soc/serial@2000 status'
```

```text
### bad_label
Error: /Users/donh/.elinux_scratch/D5/ex4/bad_label.dts:3.1-10 Label or path d5_uart9 not found
FATAL ERROR: Syntax error parsing input tree
### bad_deleted
/Users/donh/.elinux_scratch/D5/ex4/bad_deleted.dts:4.13-34: ERROR (phandle_references): /display: Reference to non-existent node or label "d5_gpu"

ERROR: Input tree has errors, aborting (use -f to force output)
### typo_status
-rw-rw-r-- 1 donh donh 1313 Oct 10 10:25 /var/tmp/D5/ex4/typo_status.dtb
okey
```

출력에서 볼 것: 앞의 둘은 빌드가 막아 준다. 위험한 것은 세 번째다. `"okey"`는 문법상 그냥 문자열이라 **경고 하나 없이 빌드된다**. 커널은 `status`가 없거나 `"okay"`/`"ok"`일 때만 장치를 사용 가능으로 보므로(9절 `of_device_is_available()`), 이 UART는 조용히 꺼진다. 이런 실수는 바인딩 스키마 검사(`make dtbs_check`, D7)가 잡는다.

---

## 4. 커널이 dtb를 만드는 방법 — `make dtbs`

### 4.1 어디에 무엇이 적혀 있나

| 파일 | 역할 |
|---|---|
| `Makefile` (최상위) | `%.dtb: dtbs_prepare` → `$(MAKE) $(build)=arch/$(SRCARCH)/boot/dts $(dtstree)/$@`, `dtbs`, `dtbs_install`, `dtbs_check` 타깃. `dtbs_prepare`는 `scripts_dtc`(커널 동봉 dtc 빌드)에 의존 |
| `arch/arm64/boot/dts/Makefile` | `subdir-y += actions`, `airoha`, … 벤더 폴더 나열 |
| `arch/arm64/boot/dts/freescale/Makefile` | `dtb-$(CONFIG_ARCH_MXC) += imx8mp-evk.dtb` 처럼 Kconfig 심볼로 켜고 끄는 보드 목록, 오버레이 합성 규칙 `imx8mp-evk-pcie-ep-dtbs += imx8mp-evk.dtb imx-pcie0-ep.dtbo` |
| `scripts/Makefile.build` | `dtb-y` 등이 있으면 `include $(srctree)/scripts/Makefile.dtbs` |
| `scripts/Makefile.dtbs` | 실제 규칙: `DTC_FLAGS`, `dtc_cpp_flags`, `cmd_dtc`, `cmd_fdtoverlay`, `.dtb.S` 래핑 |
| `scripts/dtc/` | 커널에 동봉된 dtc·libfdt·`fdtoverlay` 소스 (호스트용으로 빌드됨) |

`dtb-$(CONFIG_ARCH_MXC)` 꼴이라서 **그 SoC 계열이 `.config`에서 켜져 있어야** `make dtbs`가 그 보드를 빌드한다. arm64 `defconfig`는 `CONFIG_ARCH_MXC=y`, `CONFIG_ARCH_K3=y` 등 대부분의 플랫폼을 켠다.

`scripts/Makefile.dtbs`의 핵심 부분은 이렇다(6.18.55 소스 그대로, 91~132행 중 발췌).

```make
DTC ?= $(objtree)/scripts/dtc/dtc
DTC_FLAGS += -Wno-unique_unit_address

# Disable noisy checks by default
ifeq ($(findstring 1,$(KBUILD_EXTRA_WARN)),)
DTC_FLAGS += -Wno-unit_address_vs_reg \
             -Wno-avoid_unnecessary_addr_size \
             -Wno-alias_paths \
             -Wno-graph_child_address \
             -Wno-interrupt_map \
             -Wno-simple_bus_reg
else
DTC_FLAGS += -Wunique_unit_address_if_enabled
endif
...
# Set -@ if the target is a base DTB that overlay is applied onto
DTC_FLAGS += $(if $(filter $(patsubst $(obj)/%,%,$@), $(base-dtb-y)), -@)

DTC_INCLUDE := $(srctree)/scripts/dtc/include-prefixes

dtc_cpp_flags = -Wp,-MMD,$(depfile).pre.tmp -nostdinc -I $(DTC_INCLUDE) -undef -D__DTS__

dtc-tmp = $(subst $(comma),_,$(dot-target).dts.tmp)

quiet_cmd_dtc = DTC $(quiet_dtb_check_tag) $@
      cmd_dtc = \
	$(HOSTCC) -E $(dtc_cpp_flags) -x assembler-with-cpp -o $(dtc-tmp) $< ; \
	$(DTC) -o $@ -b 0 $(addprefix -i,$(dir $<) $(DTC_INCLUDE)) \
	       $(DTC_FLAGS) -d $(depfile).dtc.tmp $(dtc-tmp) ; \
	cat $(depfile).pre.tmp $(depfile).dtc.tmp > $(depfile) \
	$(cmd_dtb_check)

$(obj)/%.dtb: $(obj)/%.dts $(DTC) $(DT_TMP_SCHEMA) FORCE
	$(call if_changed_dep,dtc)
```

읽는 법: `.dtb`는 `.dts`에서 `cmd_dtc`로 만든다. `cmd_dtc`는 세 줄이다 — (1) `$(HOSTCC) -E`로 전처리, (2) 커널 동봉 dtc로 컴파일, (3) cpp가 만든 의존성(`.pre.tmp`)과 dtc가 만든 의존성(`.dtc.tmp`, `/include/`·`/incbin/` 파일)을 합쳐 `.d`로. `if_changed_dep`는 명령줄이 바뀌었거나 의존 파일이 바뀌었을 때만 다시 실행한다(B4의 `-MMD` 이야기와 같은 원리).

### 4.2 예제 12 — 보드 dtb 하나만 빌드하기

무엇을 확인하나: 커널 이미지를 빌드하지 않고 `defconfig` + dtb 하나만 만드는 데 걸리는 시간과 출력.

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/D5 && cd /var/lab/src/linux-6.18.55 &&
  time make O=/var/tmp/D5/build defconfig >/var/tmp/D5/defconfig.log 2>&1; tail -3 /var/tmp/D5/defconfig.log;
  time make O=/var/tmp/D5/build -j6 freescale/imx8mp-evk.dtb 2>&1 | tail -20'
```

```text
real	0m5.632s
user	0m3.442s
sys	0m0.646s
# configuration written to .config
#
make[1]: Leaving directory '/var/tmp/D5/build'
  HOSTCC  scripts/dtc/checks.o
  HOSTCC  scripts/dtc/util.o
  LEX     scripts/dtc/dtc-lexer.lex.c
  YACC    scripts/dtc/dtc-parser.tab.[ch]
  HOSTCC  scripts/dtc/libfdt/fdt.o
  HOSTCC  scripts/dtc/libfdt/fdt_ro.o
  HOSTCC  scripts/dtc/libfdt/fdt_wip.o
  HOSTCC  scripts/dtc/libfdt/fdt_sw.o
  HOSTCC  scripts/dtc/libfdt/fdt_rw.o
  HOSTCC  scripts/dtc/libfdt/fdt_strerror.o
  HOSTCC  scripts/dtc/libfdt/fdt_empty_tree.o
  HOSTCC  scripts/dtc/libfdt/fdt_overlay.o
  HOSTCC  scripts/dtc/fdtoverlay.o
  HOSTCC  scripts/dtc/dtc-lexer.lex.o
  HOSTCC  scripts/dtc/dtc-parser.tab.o
  HOSTLD  scripts/dtc/fdtoverlay
  HOSTLD  scripts/dtc/dtc
  DTC     arch/arm64/boot/dts/freescale/imx8mp-evk.dtb
make[1]: Leaving directory '/var/tmp/D5/build'

real	0m3.116s
user	0m3.503s
sys	0m0.523s
```

출력에서 볼 것: 경로는 `arch/arm64/boot/dts/` 아래의 **상대 경로**(`freescale/imx8mp-evk.dtb`)로 준다. 첫 빌드는 커널 동봉 dtc와 `fdtoverlay`를 먼저 컴파일한다(`tail -20`이라 앞쪽 HOSTCC 몇 줄은 잘렸다). 그래서 dtb 작업에는 시스템 dtc가 없어도 된다. 결과물은 `/var/tmp/D5/build/arch/arm64/boot/dts/freescale/imx8mp-evk.dtb`다. 커널 이미지 전체 빌드는 수십 분이지만 DT만 고칠 때는 이렇게 몇 초면 된다. 실무에서 DT를 고치고 보드에 올릴 때 이 명령이 손에 익어야 한다.

### 4.3 예제 13 — 실제로 실행되는 명령 보기 (`V=1`)

무엇을 확인하나: `cmd_dtc`가 펼쳐진 실제 `gcc -E`와 `dtc` 명령줄.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55 &&
  make O=/var/tmp/D5/build V=1 freescale/imx8mp-verdin-nonwifi-dev.dtb 2>&1 |
  grep -E "gcc -E|dtc -o|DTC" | sed "s/; /;\n/g" | head -20'
# 읽기 쉽게 sed로 ';' 뒤에서 줄을 나눴다
```

```text
# DTC     arch/arm64/boot/dts/freescale/imx8mp-verdin-nonwifi-dev.dtb
  gcc -E -Wp,-MMD,arch/arm64/boot/dts/freescale/.imx8mp-verdin-nonwifi-dev.dtb.d.pre.tmp -nostdinc -I /var/lab/src/linux-6.18.55/scripts/dtc/include-prefixes -undef -D__DTS__ -x assembler-with-cpp -o arch/arm64/boot/dts/freescale/.imx8mp-verdin-nonwifi-dev.dtb.dts.tmp /var/lab/src/linux-6.18.55/arch/arm64/boot/dts/freescale/imx8mp-verdin-nonwifi-dev.dts ;
./scripts/dtc/dtc -o arch/arm64/boot/dts/freescale/imx8mp-verdin-nonwifi-dev.dtb -b 0 -i/var/lab/src/linux-6.18.55/arch/arm64/boot/dts/freescale/ -i/var/lab/src/linux-6.18.55/scripts/dtc/include-prefixes -Wno-unique_unit_address -Wno-unit_address_vs_reg -Wno-avoid_unnecessary_addr_size -Wno-alias_paths -Wno-graph_child_address -Wno-interrupt_map -Wno-simple_bus_reg   -d arch/arm64/boot/dts/freescale/.imx8mp-verdin-nonwifi-dev.dtb.d.dtc.tmp arch/arm64/boot/dts/freescale/.imx8mp-verdin-nonwifi-dev.dtb.dts.tmp ;
cat arch/arm64/boot/dts/freescale/.imx8mp-verdin-nonwifi-dev.dtb.d.pre.tmp arch/arm64/boot/dts/freescale/.imx8mp-verdin-nonwifi-dev.dtb.d.dtc.tmp > arch/arm64/boot/dts/freescale/.imx8mp-verdin-nonwifi-dev.dtb.d 
```

출력에서 볼 것: 4.1절 Makefile이 그대로 펼쳐졌다. 중간 산출물 `.imx8mp-verdin-nonwifi-dev.dtb.dts.tmp`가 **빌드 폴더에 남는다**는 점이 실무에서 아주 유용하다. 이 파일이 "cpp가 펼친 최종 소스"이므로, 6.2절처럼 `dtc -I dts -O dts`로 라벨이 살아 있는 평탄화 소스를 얻을 수 있다. 이 보드는 오버레이 베이스가 아니어서 `-@`가 없다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="125" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="82" y="52" text-anchor="middle" font-size="12">.dts + .dtsi</text>
  <text x="82" y="70" text-anchor="middle" font-size="12">+ dt-bindings .h</text>
  <rect x="175" y="30" width="130" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="240" y="52" text-anchor="middle" font-size="13">gcc -E (cpp)</text>
  <text x="240" y="70" text-anchor="middle" font-size="12">include · 매크로</text>
  <rect x="335" y="30" width="110" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="390" y="52" text-anchor="middle" font-size="12">.dtb.dts.tmp</text>
  <text x="390" y="70" text-anchor="middle" font-size="12">펼친 단일 소스</text>
  <rect x="470" y="30" width="92" height="50" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="515" y="52" text-anchor="middle" font-size="13">dtc</text>
  <text x="515" y="70" text-anchor="middle" font-size="12">라벨 · 병합</text>
  <rect x="585" y="30" width="75" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="622" y="60" text-anchor="middle" font-size="13">.dtb</text>
  <line x1="145" y1="55" x2="171" y2="55" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="305" y1="55" x2="331" y2="55" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="445" y1="55" x2="466" y2="55" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="562" y1="55" x2="581" y2="55" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="240" y="102" text-anchor="middle" font-size="12">-nostdinc -undef</text>
  <text x="240" y="120" text-anchor="middle" font-size="12">-D__DTS__</text>
  <text x="240" y="138" text-anchor="middle" font-size="12">-I include-prefixes</text>
  <text x="240" y="156" text-anchor="middle" font-size="12">-x assembler-with-cpp</text>
  <text x="530" y="102" text-anchor="middle" font-size="12">-b 0 · -i 소스 폴더</text>
  <text x="530" y="120" text-anchor="middle" font-size="12">-Wno-... 경고 7개 끔</text>
  <text x="530" y="138" text-anchor="middle" font-size="12">-@ (오버레이 베이스만)</text>
  <rect x="20" y="186" width="300" height="70" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="208" text-anchor="middle" font-size="12">.d.pre.tmp (cpp) + .d.dtc.tmp (dtc)</text>
  <text x="170" y="228" text-anchor="middle" font-size="12">→ .dtb.d → .dtb.cmd</text>
  <text x="170" y="248" text-anchor="middle" font-size="12">if_changed_dep: 바뀐 것만 다시 빌드</text>
  <rect x="350" y="186" width="310" height="70" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="505" y="208" text-anchor="middle" font-size="12">base .dtb + .dtbo → fdtoverlay</text>
  <text x="505" y="228" text-anchor="middle" font-size="12">→ 합성 .dtb (빌드 로그의 OVL 줄)</text>
  <text x="505" y="248" text-anchor="middle" font-size="12">예: imx8mp-evk-pcie-ep.dtb</text>
  <line x1="240" y1="160" x2="200" y2="183" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
  <line x1="622" y1="80" x2="560" y2="183" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
  <rect x="20" y="276" width="640" height="64" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="300" text-anchor="middle" font-size="12">make dtbs → dts/dtbs-list (만든 dtb 목록) → make dtbs_install</text>
  <text x="340" y="322" text-anchor="middle" font-size="12">→ $(INSTALL_PATH)/dtbs/&lt;kernel release&gt;/freescale/... 로 복사</text>
</svg>
```
그림 4 — 커널 dtb 빌드 파이프라인. 위 줄이 `cmd_dtc` 한 번이고, 왼쪽 아래는 증분 빌드용 의존성, 오른쪽 아래는 오버레이를 미리 합친 dtb를 만드는 `cmd_fdtoverlay`다.

### 4.4 예제 14 — 의존성 파일로 include 트리와 "파급 범위" 보기

무엇을 확인하나: cpp가 기록한 의존성(`.d.pre.tmp`)으로 보드 하나가 실제로 읽은 파일 전부를, `.cmd` 파일들로 "이 파일을 고치면 몇 개 dtb가 다시 빌드되나"를 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/D5/build/arch/arm64/boot/dts/freescale &&
  tr " " "\n" < .imx8mp-verdin-nonwifi-dev.dtb.d.pre.tmp | grep -v "^\\\\\?$" | sed "s#/var/lab/src/linux-6.18.55/##";
  cat .imx8mp-verdin-nonwifi-dev.dtb.d.dtc.tmp'
```

```text
imx8mp-verdin-nonwifi-dev.o:
arch/arm64/boot/dts/freescale/imx8mp-verdin-nonwifi-dev.dts
arch/arm64/boot/dts/freescale/imx8mp-verdin.dtsi
scripts/dtc/include-prefixes/dt-bindings/phy/phy-imx8-pcie.h
scripts/dtc/include-prefixes/dt-bindings/pwm/pwm.h
arch/arm64/boot/dts/freescale/imx8mp.dtsi
scripts/dtc/include-prefixes/dt-bindings/clock/imx8mp-clock.h
scripts/dtc/include-prefixes/dt-bindings/power/imx8mp-power.h
scripts/dtc/include-prefixes/dt-bindings/reset/imx8mp-reset.h
scripts/dtc/include-prefixes/dt-bindings/reset/imx8mp-reset-audiomix.h
scripts/dtc/include-prefixes/dt-bindings/gpio/gpio.h
scripts/dtc/include-prefixes/dt-bindings/input/input.h
scripts/dtc/include-prefixes/dt-bindings/input/linux-event-codes.h
scripts/dtc/include-prefixes/dt-bindings/interconnect/fsl,imx8mp.h
scripts/dtc/include-prefixes/dt-bindings/interrupt-controller/arm-gic.h
scripts/dtc/include-prefixes/dt-bindings/interrupt-controller/irq.h
scripts/dtc/include-prefixes/dt-bindings/thermal/thermal.h
arch/arm64/boot/dts/freescale/imx8mp-pinfunc.h
arch/arm64/boot/dts/freescale/imx8mp-verdin-nonwifi.dtsi
arch/arm64/boot/dts/freescale/imx8mp-verdin-dev.dtsi
arch/arm64/boot/dts/freescale/imx8mp-verdin-nonwifi-dev.dtb: arch/arm64/boot/dts/freescale/.imx8mp-verdin-nonwifi-dev.dtb.dts.tmp
```

출력에서 볼 것: 그림 2의 트리가 바로 이 목록에서 나왔다. 파일이 나온 순서가 곧 cpp가 펼친 순서다. `.d.dtc.tmp`에는 tmp 파일 하나뿐인데, 이 보드는 `/include/`나 `/incbin/`을 안 쓰기 때문이다.

이제 전체 dtb를 빌드한 뒤(4.6절) 파급 범위를 센다.

```sh
#!/bin/bash
# fanout.sh — 이 파일을 고치면 다시 빌드되는 dtb 개수 (.cmd 안의 의존성 목록 기준)
cd /var/tmp/D5/build/arch/arm64/boot/dts
for f in freescale/imx8mp.dtsi freescale/imx8mp-verdin.dtsi \
         freescale/imx8mp-verdin-dev.dtsi dt-bindings/clock/imx8mp-clock.h \
         dt-bindings/gpio/gpio.h; do
  printf "%-36s %4d dtb\n" $f $(grep -l "/$f " */.*.dtb.cmd | wc -l)
done
```

```text
freescale/imx8mp.dtsi                  56 dtb
freescale/imx8mp-verdin.dtsi           10 dtb
freescale/imx8mp-verdin-dev.dtsi        2 dtb
dt-bindings/clock/imx8mp-clock.h       56 dtb
dt-bindings/gpio/gpio.h              1284 dtb
```

출력에서 볼 것: 캐리어 dtsi를 고치면 2개(wifi/nonwifi × dev), SoM dtsi는 10개, SoC dtsi는 i.MX8MP 보드 56개, `gpio.h`는 arm64 dtb 1284개에 영향을 준다. **아래층일수록 파급이 크다.** 그래서 BSP 팀은 SoC·SoM dtsi는 거의 건드리지 않고(업스트림 그대로 유지), 자기 보드의 변경은 가장 위층 파일에만 쓴다. 업스트림 리뷰어도 "SoC dtsi에 보드 전용 내용을 넣지 말라"고 지적한다.

### 4.5 `-@`와 `__symbols__` — 오버레이를 위한 라벨 보존

3.2절에서 라벨은 dtb에 남지 않는다고 했다. 그런데 오버레이(D6)는 "베이스 dtb의 `&flexcan1`을 고쳐라"처럼 **라벨로** 대상을 가리킨다. 그래서 오버레이를 받을 베이스 dtb는 `dtc -@`로 빌드해서 라벨 → 경로 표(`/__symbols__` 노드)를 dtb 안에 남긴다. 커널은 `Makefile`에 `<합성>-dtbs += <베이스>.dtb <오버레이>.dtbo`가 있는 베이스에만 자동으로 `-@`를 붙인다(4.1절 `base-dtb-y`).

예제 15 — 무엇을 확인하나: 오버레이 베이스인 EVK와 그렇지 않은 Verdin의 차이, `-@`의 크기 비용, `__symbols__`로 라벨을 경로로 바꾸기.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/D5/build/arch/arm64/boot/dts/freescale &&
  grep -o -- " -@" .imx8mp-evk.dtb.cmd .imx8mp-verdin-nonwifi-dev.dtb.cmd;
  for d in imx8mp-evk imx8mp-verdin-nonwifi-dev; do echo -n "$d: "; fdtget -l $d.dtb / | tr "\n" " "; echo; done;
  fdtget -p imx8mp-evk.dtb /__symbols__ | wc -l; fdtget -t s imx8mp-evk.dtb /__symbols__ flexcan1;
  cd /var/tmp/D5 && F=/var/tmp/D5/build/arch/arm64/boot/dts/freescale/.imx8mp-evk.dtb.dts.tmp;
  dtc -q -I dts -O dtb -o evk_noat.dtb $F; dtc -q -@ -I dts -O dtb -o evk_at.dtb $F;
  ls -l evk_noat.dtb evk_at.dtb | awk "{print \$5, \$9}"'
```

```text
.imx8mp-evk.dtb.cmd: -@
imx8mp-evk: aliases cpus opp-table clock-osc-32k clock-osc-24m clock-ext1 clock-ext2 clock-ext3 clock-ext4 funnel reserved-memory pmu psci thermal-zones timer soc@0 chosen backlight-lvds hdmi-connector gpio-leds memory@40000000 native-hdmi-connector pcie0-refclk regulator-audio-3v3 regulator-audio-1v8 regulator-audio-pwr regulator-can1-stby regulator-can2-stby regulator-pcie regulator-per-12v regulator-usdhc2 regulator-vext-3v3 audio-codec-bt-sco sound-bt-sco sound-hdmi sound-micfil sound-wm8960 sound-xcvr __symbols__ 
imx8mp-verdin-nonwifi-dev: aliases cpus opp-table clock-osc-32k clock-osc-24m clock-ext1 clock-ext2 clock-ext3 clock-ext4 funnel reserved-memory pmu psci thermal-zones timer soc@0 chosen backlight backlight-mezzanine connector gpio-keys sound-hdmi regulator-1p8v regulator-3p3v regulator-5p0v regulator-module-eth1phy regulator-force-sleep-moci regulator-usb1-vbus regulator-usb2-vbus regulator-usdhc2 native-hdmi-connector regulator-eth2phy sound 
265
/soc@0/bus@30800000/spba-bus@30800000/can@308c0000
76379 evk_at.dtb
58142 evk_noat.dtb
```

출력에서 볼 것: EVK의 명령줄에만 `-@`가 있고, 루트 맨 끝에 `__symbols__` 노드(라벨 265개)가 생겼다. `fdtget ... /__symbols__ flexcan1`은 라벨을 경로로 바꿔 주므로 3.4절의 "경로를 모를 때" 문제를 푼다. 대가는 크기다: 같은 소스로 `-@` 없이 58,142 B, 있으면 76,379 B — 31% 커진다. 부트 시간·플래시가 빠듯한 제품에서 오버레이를 안 쓰면 `-@`를 빼는 이유다.

덤으로, 두 보드의 루트 자식 목록을 보면 `aliases`부터 `chosen`까지는 **SoC dtsi가 먼저 정의한 노드라 순서가 같고**, 그 뒤는 보드마다 다르다(EVK는 `backlight-lvds`…, Verdin은 SoM이 넣은 `regulator-*`…). dtb 안의 노드 순서에도 층의 흔적이 남는다.

### 4.6 예제 16 — `make dtbs` 전체

무엇을 확인하나: defconfig가 켠 모든 arm64 보드를 빌드하는 데 드는 시간과 결과 개수.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55 &&
  /usr/bin/time -f "%e s elapsed" make O=/var/tmp/D5/build -j6 dtbs > /var/tmp/D5/dtbs.log 2>&1; tail -2 /var/tmp/D5/dtbs.log;
  grep -c " DTC " /var/tmp/D5/dtbs.log; grep -c " OVL " /var/tmp/D5/dtbs.log; grep -ci "warning" /var/tmp/D5/dtbs.log;
  find /var/tmp/D5/build/arch/arm64/boot/dts -name "*.dtb" | wc -l; find /var/tmp/D5/build/arch/arm64/boot/dts -name "*.dtbo" | wc -l;
  wc -l /var/tmp/D5/build/arch/arm64/boot/dts/dtbs-list; head -3 /var/tmp/D5/build/arch/arm64/boot/dts/dtbs-list'
```

```text
make[1]: Leaving directory '/var/tmp/D5/build'
53.20 s elapsed
1560
181
0
1577
169
1652 /var/tmp/D5/build/arch/arm64/boot/dts/dtbs-list
arch/arm64/boot/dts/actions/s700-cubieboard7.dtb
arch/arm64/boot/dts/actions/s900-bubblegum-96.dtb
arch/arm64/boot/dts/airoha/en7581-evb.dtb
```

출력에서 볼 것: 6 vCPU로 53초에 `.dtb` 1577개, `.dtbo` 169개가 나왔다. `DTC` 줄 1560개(dts와 dtso 컴파일), `OVL` 줄 181개(오버레이를 미리 합친 dtb). 커널이 끈 경고 덕분에 warning은 0개다. `dtbs-list`는 `make dtbs_install`이 복사할 목록이다.

---

## 5. 층별로 무엇이 더해지나 — 실측

### 5.1 예제 17 — Verdin 체인을 한 층씩 쌓아 컴파일

무엇을 확인하나: SoC만, SoC+SoM, +SoM 변형, +캐리어, 완성 보드를 각각 dtb로 만들어 노드·속성·`status` 수를 센다. dtb를 파싱하는 작은 Python 스크립트를 직접 쓴다. dtb 형식(헤더, struct 블록의 `FDT_BEGIN_NODE`=1, `FDT_END_NODE`=2, `FDT_PROP`=3, `FDT_NOP`=4, `FDT_END`=9 토큰, strings 블록)은 D2에서 다룬 그대로다.

```sh
# L1_soc.dts      : /dts-v1/; #include "imx8mp.dtsi"
# L2_som.dts      : /dts-v1/; #include "imx8mp-verdin.dtsi"
# L3_som_var.dts  : + #include "imx8mp-verdin-nonwifi.dtsi"
# L4_carrier.dts  : + #include "imx8mp-verdin-dev.dtsi"
#!/bin/bash
# build_layers.sh — Verdin 체인을 한 층씩 쌓아 컴파일
set -e
K=/var/lab/src/linux-6.18.55; FS=$K/arch/arm64/boot/dts/freescale
S=/Users/donh/.elinux_scratch/D5/ex5; O=/var/tmp/D5/ex5; mkdir -p $O
for f in L1_soc L2_som L3_som_var L4_carrier; do
  gcc -E -nostdinc -I $FS -I $K/scripts/dtc/include-prefixes -undef -D__DTS__ \
      -x assembler-with-cpp -o $O/$f.tmp $S/$f.dts
  dtc -q -I dts -O dtb -b 0 -i $FS -o $O/$f.dtb $O/$f.tmp
done
cp /var/tmp/D5/build/arch/arm64/boot/dts/freescale/imx8mp-verdin-nonwifi-dev.dtb $O/L5_board.dtb
```

```python
#!/usr/bin/env python3
# fdtstat.py <a.dtb>... — FDT struct 블록을 직접 걸어 노드·속성·status를 센다
import struct, sys
def stat(path):
    b = open(path, 'rb').read()
    magic, total, off_st, off_str = struct.unpack('>IIII', b[:16])
    assert magic == 0xd00dfeed, 'not a dtb'
    def name_at(o): return b[off_str+o:b.index(b'\0', off_str+o)].decode()
    p, nodes, props, okay, disabled = off_st, 0, 0, 0, 0
    while True:
        tag, = struct.unpack('>I', b[p:p+4]); p += 4
        if tag == 1:                        # FDT_BEGIN_NODE
            end = b.index(b'\0', p); p = (end + 4) & ~3
            nodes += 1
        elif tag == 3:                      # FDT_PROP
            ln, no = struct.unpack('>II', b[p:p+8]); val = b[p+8:p+8+ln]
            p = (p + 8 + ln + 3) & ~3; props += 1
            if name_at(no) == 'status':
                if val.rstrip(b'\0') in (b'okay', b'ok'): okay += 1
                else: disabled += 1
        elif tag in (2, 4): continue        # FDT_END_NODE, FDT_NOP
        elif tag == 9: break                # FDT_END
    return total, nodes, props, okay, disabled
print(f"{'dtb':34} {'bytes':>6} {'nodes':>5} {'props':>5} {'okay':>5} {'disabled':>8}")
for f in sys.argv[1:]:
    t, n, pr, ok, dis = stat(f)
    print(f"{f.split('/')[-1]:34} {t:6d} {n:5d} {pr:5d} {ok:5d} {dis:8d}")
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.elinux_scratch/D5; $S/build_layers.sh &&
  python3 $S/fdtstat.py /var/tmp/D5/ex5/L*.dtb /var/tmp/D5/build/arch/arm64/boot/dts/freescale/imx8mp-evk.dtb \
                        /var/tmp/D5/build/arch/arm64/boot/dts/ti/k3-am625-sk.dtb'
```

```text
dtb                                 bytes nodes props  okay disabled
L1_soc.dtb                          41753   280  1379     0       63
L2_som.dtb                          62141   421  1934     4       71
L3_som_var.dtb                      62665   421  1946     4       71
L4_carrier.dtb                      64263   430  2000    39       36
L5_board.dtb                        64443   430  2002    39       36
imx8mp-evk.dtb                      76379   393  2243    37       28
k3-am625-sk.dtb                     59474   256  1829    19       46
```

출력에서 볼 것: `okay`/`disabled`는 `status` 속성이 **명시된** 노드만 센 것이다(`status`가 없는 노드도 커널에게는 켜진 것이지만, 여기서는 층의 "스위치 조작"을 보려고 뺐다).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13">한 층씩 쌓아 컴파일한 Verdin iMX8MP 트리 (실측)</text>
<text x="170" y="48" font-size="12">노드 수 (막대) · dtb 바이트</text>
<text x="450" y="48" font-size="12">status = "okay" 노드 수</text>
<text x="20" y="76" font-size="13">L1 SoC</text>
<text x="20" y="92" font-size="12">imx8mp.dtsi</text>
<rect x="170" y="64" width="143.3" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="319.3" y="80" font-size="12">280</text>
<text x="170" y="102" font-size="12">41,753 B</text>
<rect x="450" y="64" width="1.0" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="456.0" y="80" font-size="12">0</text>
<text x="20" y="124" font-size="13">L2 SoM</text>
<text x="20" y="140" font-size="12">+ verdin.dtsi</text>
<rect x="170" y="112" width="215.4" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="391.4" y="128" font-size="12">421</text>
<text x="170" y="150" font-size="12">62,141 B</text>
<rect x="450" y="112" width="16.4" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="472.4" y="128" font-size="12">4</text>
<text x="20" y="172" font-size="13">L3 SoM 변형</text>
<text x="20" y="188" font-size="12">+ nonwifi.dtsi</text>
<rect x="170" y="160" width="215.4" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="391.4" y="176" font-size="12">421</text>
<text x="170" y="198" font-size="12">62,665 B</text>
<rect x="450" y="160" width="16.4" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="472.4" y="176" font-size="12">4</text>
<text x="20" y="220" font-size="13">L4 캐리어</text>
<text x="20" y="236" font-size="12">+ dev.dtsi</text>
<rect x="170" y="208" width="220.0" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="396.0" y="224" font-size="12">430</text>
<text x="170" y="246" font-size="12">64,263 B</text>
<rect x="450" y="208" width="160.0" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="616.0" y="224" font-size="12">39</text>
<text x="20" y="268" font-size="13">L5 보드</text>
<text x="20" y="284" font-size="12">+ nonwifi-dev.dts</text>
<rect x="170" y="256" width="220.0" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="396.0" y="272" font-size="12">430</text>
<text x="170" y="294" font-size="12">64,443 B</text>
<rect x="450" y="256" width="160.0" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="616.0" y="272" font-size="12">39</text>
<line x1="170" y1="56" x2="170" y2="300" stroke="currentColor"/>
<line x1="450" y1="56" x2="450" y2="300" stroke="currentColor"/>
<text x="20" y="318" font-size="12">SoM 층이 노드를 가장 많이 더하고(+141), 장치를 켜는 것은 캐리어 층이다(okay 4 → 39).</text>
</svg>
```
그림 5 — 층별 기여(실측). SoC 층은 280개 노드 중 `okay`가 0개다. 모든 블록을 정의만 하고 꺼 둔다. SoM 층이 노드를 가장 많이 더한다(+141: pinctrl 그룹, 레귤레이터, PMIC, eMMC). 그러나 켜는 것은 4개뿐이고, 실제 스위치를 올리는 것은 캐리어 층이다(+35 `okay`, 그와 함께 `disabled`가 71 → 36으로 줄어든다).

이 숫자가 "SoC 벤더는 정의, SoM 벤더는 배선, 보드 팀은 켜기"라는 역할 분담을 그대로 보여 준다. 마지막 보드 `.dts` 층은 `model`과 `compatible` 두 속성만 더한다(+2 props, +180 B). 같은 숫자로 `imx8mp-evk.dtb`가 76 KB인 것은 4.5절의 `-@` 때문이다.

---

## 6. dtb를 읽고 고치는 도구

### 6.1 decompile로 잃어버리는 것

`dtc -I dtb -O dts`는 dtb를 다시 소스로 풀지만, 원래 소스와 같지 않다.

| 원래 소스에 있던 것 | decompile 결과 | 되살리는 법 |
|---|---|---|
| 라벨 (`flexcan1:`) | 없음. 참조는 phandle 숫자 | `-@` 빌드면 `/__symbols__`에서 경로 조회 |
| 매크로 (`GIC_SPI`, `IMX8MP_CLK_CAN1_ROOT`) | 숫자 (`0x00`, `0xe9`) | `dt-bindings` 헤더에서 역으로 찾기 |
| include 구조·주석 | 한 파일로 평탄화, 주석 없음 | 빌드 폴더의 `.dts.tmp`를 본다 (6.2) |
| 지워진 노드·omit된 노드 | 없음 | 소스에서 `/delete-node/`, `/omit-if-no-ref/` 검색 |
| 셀 경계 (`<a b>, <c d>`) | `<a b c d>` 한 덩어리 | 바인딩 문서의 셀 수로 다시 나눈다 |

### 6.2 예제 18 — 라벨이 살아 있는 평탄화 소스와 변형 비교

dtb끼리 비교하는 `dtdiff`가 있다. `/usr/bin/dtdiff`는 셸 스크립트로, 두 입력(dts, dtb, 또는 `/proc/device-tree` 같은 **디렉터리**)을 각각 `dtc -O dts -s`(노드·속성 이름순 정렬)로 풀어서 `diff -u` 한다. 무엇을 확인하나: Wi-Fi 변형과 non-Wi-Fi 변형 보드가 실제로 무엇이 다른지.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/D5/build/arch/arm64/boot/dts/freescale &&
  dtdiff imx8mp-verdin-nonwifi-dev.dtb imx8mp-verdin-wifi-dev.dtb 2>/dev/null | sed -n 1,30p;
  dtdiff imx8mp-verdin-nonwifi-dev.dtb imx8mp-verdin-wifi-dev.dtb 2>/dev/null | wc -l'
```

```text
--- /dev/fd/63	2026-10-10 10:44:30.568977482 -0700
+++ /dev/fd/62	2026-10-10 10:44:30.568977482 -0700
@@ -3,9 +3,9 @@
 / {
 	#address-cells = <0x02>;
 	#size-cells = <0x02>;
-	compatible = "toradex,verdin-imx8mp-nonwifi-dev", "toradex,verdin-imx8mp-nonwifi", "toradex,verdin-imx8mp", "fsl,imx8mp";
+	compatible = "toradex,verdin-imx8mp-wifi-dev", "toradex,verdin-imx8mp-wifi", "toradex,verdin-imx8mp", "fsl,imx8mp";
 	interrupt-parent = <0x01>;
-	model = "Toradex Verdin iMX8M Plus on Verdin Development Board";
+	model = "Toradex Verdin iMX8M Plus WB on Verdin Development Board";
 
 	aliases {
 		ethernet0 = "/soc@0/bus@30800000/ethernet@30bf0000";
@@ -38,10 +38,10 @@
 		compatible = "pwm-backlight";
 		default-brightness-level = <0x04>;
 		enable-gpios = <0x3a 0x01 0x00>;
-		pinctrl-0 = <0xa3>;
+		pinctrl-0 = <0xa8>;
 		pinctrl-names = "default";
-		power-supply = <0xa4>;
-		pwms = <0xa5 0x00 0x65b9ab 0x01>;
+		power-supply = <0xa9>;
+		pwms = <0xaa 0x00 0x65b9ab 0x01>;
 		status = "disabled";
 	};
 
@@ -50,7 +50,7 @@
 		compatible = "pwm-backlight";
1530
```

출력에서 볼 것: diff가 1530줄이다. 그런데 대부분은 **진짜 차이가 아니다**. Wi-Fi 변형에 노드가 몇 개 더 있어서 phandle 번호가 전부 5씩 밀렸고(`0xa3` → `0xa8`), 그 번호를 참조하는 모든 속성이 diff에 나온다. dtb 비교의 고질적인 문제다.

해결책은 **라벨이 살아 있는 상태에서 비교**하는 것이다. 빌드 폴더의 `.dts.tmp`(cpp만 거친 소스)를 `dtc -I dts -O dts -s`로 한 번 평탄화하면, 병합·삭제는 다 적용됐지만 참조는 `&label`로 남는다. phandle 줄만 빼고 비교한다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/D5/build/arch/arm64/boot/dts/freescale &&
  for v in nonwifi wifi; do dtc -q -I dts -O dts -s -o /var/tmp/D5/$v-dev.src.dts .imx8mp-verdin-$v-dev.dtb.dts.tmp; done;
  cd /var/tmp/D5 && diff -u <(grep -v "phandle = " nonwifi-dev.src.dts) <(grep -v "phandle = " wifi-dev.src.dts) > d5_wifi2.diff;
  grep -c "^[+-][^+-]" d5_wifi2.diff;
  grep "^[+-][^+-]" d5_wifi2.diff | grep -v "^+\s*\(regulator\|startup\|enable-active\|compatible = \"regulator\)" | head -60'
# 출력의 gpio-line-names 두 줄은 30개 문자열이 한 줄에 있어 너무 길어서 가운데를 ...로 줄였다
```

```text
37
-	compatible = "toradex,verdin-imx8mp-nonwifi-dev", "toradex,verdin-imx8mp-nonwifi", "toradex,verdin-imx8mp", "fsl,imx8mp";
+	compatible = "toradex,verdin-imx8mp-wifi-dev", "toradex,verdin-imx8mp-wifi", "toradex,verdin-imx8mp", "fsl,imx8mp";
-	model = "Toradex Verdin iMX8M Plus on Verdin Development Board";
+	model = "Toradex Verdin iMX8M Plus WB on Verdin Development Board";
+	reg_wifi_en: regulator-wifi-en {
+		gpio = <&gpio2 0x0b 0x00>;
+		pinctrl-0 = <&pinctrl_wifi_pwr_en>;
+		pinctrl-names = "default";
+	};
-				gpio-line-names = "SODIMM_42", "SODIMM_46", ... "SODIMM_164", "SODIMM_152", "SODIMM_116", "SODIMM_128", "", "", "SODIMM_55", ...;
+				gpio-line-names = "SODIMM_42", "SODIMM_46", ... "", "", "", "", "", "", "SODIMM_55", ...;
-				pinctrl-0 = <&pinctrl_gpio1>, <&pinctrl_gpio2>, <&pinctrl_gpio3>, <&pinctrl_gpio4>, <&pinctrl_gpio7>, <&pinctrl_gpio8>, <&pinctrl_gpio_hog1>, <&pinctrl_gpio_hog2>, <&pinctrl_gpio_hog3>;
+				pinctrl-0 = <&pinctrl_gpio1>, <&pinctrl_gpio2>, <&pinctrl_gpio3>, <&pinctrl_gpio4>, <&pinctrl_gpio7>, <&pinctrl_gpio8>, <&pinctrl_gpio_hog2>, <&pinctrl_gpio_hog3>, <&pinctrl_gpio_hog4>;
+				keep-power-in-suspend;
+				non-removable;
+				pinctrl-0 = <&pinctrl_usdhc1>, <&pinctrl_wifi_ctrl>;
+				pinctrl-1 = <&pinctrl_usdhc1_100mhz>, <&pinctrl_wifi_ctrl>;
+				pinctrl-2 = <&pinctrl_usdhc1_200mhz>, <&pinctrl_wifi_ctrl>;
+				pinctrl-names = "default", "state_100mhz", "state_200mhz";
-				status = "disabled";
+				status = "okay";
+				vmmc-supply = <&reg_wifi_en>;
-				pinctrl-0 = <&pinctrl_uart4>;
+				pinctrl-0 = <&pinctrl_bt_uart>;
-				status = "disabled";
+				status = "okay";
+				uart-has-rtscts;
+				bluetooth {
+					compatible = "mrvl,88w8997";
+					max-speed = <0xe1000>;
+				};
```

출력에서 볼 것: 진짜 차이는 37줄이다. Wi-Fi 변형은 (1) Wi-Fi 전원 레귤레이터를 추가하고, (2) GPIO 4개(SODIMM_164/152/116/128)를 Wi-Fi 제어에 쓰므로 GPIO 이름에서 빼고 hog를 바꾸고, (3) SDIO 컨트롤러(`usdhc1`)를 켜서 Wi-Fi 칩을 붙이고, (4) UART4를 Bluetooth용 핀으로 바꿔 켜고 `bluetooth` 자식(Marvell 88W8997)을 단다. 3.4절 표에서 UART4가 Wi-Fi 변형에서만 `okay`였던 이유가 여기 있다. **리뷰나 디버깅에서 두 보드를 비교할 때는 dtb가 아니라 `.dts.tmp` 평탄화 소스를 비교하라**가 이 예제의 교훈이다.

### 6.3 예제 19 — `fdtput`으로 dtb 직접 고치기, `fdtdump`로 헤더 보기

`fdtget`/`fdtput`은 libfdt 위의 작은 CLI다. 다시 빌드할 수 없는 상황(벤더가 dtb만 준 경우, 현장에서 빠르게 장치를 끄고 싶을 때)에 쓴다. 무엇을 확인하나: 보드 dtb 복사본에서 CAN 1을 끄고, 새 노드를 만들고, 크기가 어떻게 바뀌는지.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/D5 && cp build/arch/arm64/boot/dts/freescale/imx8mp-verdin-nonwifi-dev.dtb d5_patch.dtb &&
  C=/soc@0/bus@30800000/spba-bus@30800000/can@308c0000 &&
  echo -n "before: "; fdtget -t s d5_patch.dtb $C status; fdtput -t s d5_patch.dtb $C status disabled;
  echo -n "after:  "; fdtget -t s d5_patch.dtb $C status;
  fdtput -c d5_patch.dtb /d5-test; fdtput -t s d5_patch.dtb /d5-test compatible "d5,test";
  fdtput -t u d5_patch.dtb /d5-test d5,rate 400000; fdtget -p d5_patch.dtb /d5-test; fdtget -t u d5_patch.dtb /d5-test d5,rate;
  fdtdump d5_patch.dtb 2>/dev/null | head -14;
  ls -l d5_patch.dtb build/arch/arm64/boot/dts/freescale/imx8mp-verdin-nonwifi-dev.dtb | awk "{print \$5, \$9}"'
```

```text
before: okay
after:  disabled
d5,rate
compatible
400000
/dts-v1/;
// magic:		0xd00dfeed
// totalsize:		0xfbfb (64507)
// off_dt_struct:	0x38
// off_dt_strings:	0xeeac
// off_mem_rsvmap:	0x28
// version:		17
// last_comp_version:	16
// boot_cpuid_phys:	0x0
// size_dt_strings:	0xd4f
// size_dt_struct:	0xee74

/ {
    interrupt-parent = <0x00000001>;
64443 build/arch/arm64/boot/dts/freescale/imx8mp-verdin-nonwifi-dev.dtb
64507 d5_patch.dtb
```

출력에서 볼 것: `fdtput`은 파일을 제자리에서 고치고 필요하면 키운다(64,443 → 64,507 B). 새 속성이 `fdtget -p`에서 `d5,rate`, `compatible` 순, 즉 **나중에 넣은 것이 앞에** 나온다. libfdt가 새 속성을 노드 앞쪽에 끼워 넣기 때문이다(dtc 소스 병합의 "끝에 추가"와 반대다. 속성 순서에 의미를 두는 코드를 쓰지 말라는 뜻이기도 하다). `fdtdump`는 헤더 필드(magic `0xd00dfeed`, version 17, 각 블록 오프셋)를 보여 준다. 이 dtb의 노드 수가 430개인데 struct 블록이 61 KB(`0xee74`)이고 strings 블록(속성 이름 중복 제거 테이블)은 3.4 KB뿐이다.

| 도구 | 하는 일 | 대표 명령 |
|---|---|---|
| `dtc` | dts ↔ dtb ↔ fs 변환, 검사 | `dtc -I dtb -O dts -s x.dtb`, `dtc -I fs -O dts /proc/device-tree` |
| `fdtget` | 속성 값 읽기, 자식·속성 나열 | `fdtget -t s x.dtb /path status`, `fdtget -l`, `fdtget -p` |
| `fdtput` | 속성 쓰기, 노드 만들기·지우기 | `fdtput -t s x.dtb /path status disabled`, `-c`, `-d`, `-r` |
| `fdtdump` | 헤더 + 원시 구조 덤프 | `fdtdump x.dtb` |
| `dtdiff` | 정렬 후 diff (dts·dtb·디렉터리) | `dtdiff a.dtb /proc/device-tree` |
| `fdtoverlay` | 오버레이 적용 (D6) | `fdtoverlay -i base.dtb -o out.dtb x.dtbo` |

---

## 7. 런타임 DT 읽기 — 보드 위에서 "실제로 돌고 있는 DT" 확인

빌드한 dtb가 맞아도 보드에서 돌고 있는 DT가 다를 수 있다. 부트로더가 다른 dtb를 읽었거나, `/chosen`·메모리 노드를 고쳤거나, 오버레이를 붙였을 수 있기 때문이다. 그래서 디버깅은 항상 **보드에서 실제 DT를 꺼내는 것**에서 시작한다.

### 7.1 커널이 보여 주는 두 가지 창

| 경로 | 무엇인가 | 형태 |
|---|---|---|
| `/sys/firmware/devicetree/base/` | 커널이 펼친(unflatten) 트리. 노드 = 디렉터리, 속성 = 파일 | 속성 파일은 바이너리 그대로(문자열은 NUL 종료, 셀은 big-endian 32-bit) |
| `/proc/device-tree` | 위 디렉터리로 가는 심볼릭 링크 (옛 경로 호환) | 같음 |
| `/sys/firmware/fdt` | 부팅 때 받은 **평평한 blob**(dtb) 그대로. root만 읽을 수 있음 | dtb 파일. `dtc -I dtb`로 바로 풀린다 |

### 7.2 예제 20 — QEMU 게스트에서 런타임 DT 들여다보기

무엇을 확인하나: 랩 커널(6.18.55)을 QEMU `virt`로 부팅해서, 게스트 안에서 DT 노드·속성을 읽고, `/sys/firmware/fdt` blob을 콘솔로 꺼낸다. BusyBox에는 `dtc`가 없으므로 게스트에서는 `cat`·`hexdump`만 쓰고, blob과 디렉터리 트리는 base64로 콘솔에 찍어 호스트에서 되살린다(실제 보드에서 네트워크가 없을 때도 쓰는 방법이다).

```sh
#!/bin/sh
# d5.sh — QEMU 게스트 안에서 런타임 DT 들여다보기
echo "### ls -l /proc/device-tree"; ls -l /proc/device-tree | cat
echo "### top-level node count"; ls /sys/firmware/devicetree/base | wc -l
echo "### model / compatible"; cat /proc/device-tree/model; echo
tr '\0' ' ' < /proc/device-tree/compatible; echo
echo "### pl011 node"; ls /proc/device-tree/pl011@9000000 | cat
echo "### pl011 reg (raw big-endian cells)"; hexdump -C /proc/device-tree/pl011@9000000/reg
echo "### status props"; find /proc/device-tree -name status | wc -l
echo "### chosen"; ls /proc/device-tree/chosen | cat; cat /proc/device-tree/chosen/bootargs; echo
echo "### /sys/firmware/fdt"; ls -l /sys/firmware/fdt | cat; hexdump -C /sys/firmware/fdt | head -3; md5sum /sys/firmware/fdt
echo "### fdt-b64-begin"; base64 /sys/firmware/fdt; echo "### fdt-b64-end"
echo "### fs-b64-begin"; tar c -C /sys/firmware/devicetree base 2>/dev/null | gzip | base64; echo "### fs-b64-end"
```

```sh
#!/bin/bash
# run_guest.sh — 랩 initramfs 복사본에 d5.sh를 넣고 QEMU virt 부팅
set -e
W=/var/tmp/D5/guest; rm -rf $W; mkdir -p $W/r
cp -a /var/lab/build/rootfs/. $W/r/
cp /Users/donh/.elinux_scratch/D5/guest/d5.sh $W/r/d5.sh; chmod +x $W/r/d5.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd $W/i.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=/d5.sh" > $W/console.log 2>&1 || true
tr -d '\r' < $W/console.log > $W/console.txt
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.elinux_scratch/D5/run_guest.sh'
limactl shell elinux -- bash -c 'cd /var/tmp/D5/guest && grep -a -v "^[A-Za-z0-9+/=]\{60,\}$" console.txt | cut -c1-200'
# base64 본문 줄(약 1.8만 줄)은 grep으로 걸러 냈다. 각 블록의 마지막 짧은 줄만 남아 보인다
```

```text
lab initramfs: 6.18.55 up 1.55s
### ls -l /proc/device-tree
lrwxrwxrwx    1 0        0               29 Oct 10 17:30 /proc/device-tree -> /sys/firmware/devicetree/base
### top-level node count
56
### model / compatible
linux,dummy-virt 
linux,dummy-virt 
### pl011 node
clock-names
clocks
compatible
interrupts
name
reg
### pl011 reg (raw big-endian cells)
00000000  00 00 00 00 09 00 00 00  00 00 00 00 00 00 10 00  |................|
00000010
### status props
0
### chosen
bootargs
kaslr-seed
linux,initrd-end
linux,initrd-start
name
stdout-path
console=ttyAMA0 quiet LAB_CMD=/d5.sh 
### /sys/firmware/fdt
-r--------    1 0        0          1048576 Oct 10 17:30 /sys/firmware/fdt
00000000  d0 0d fe ed 00 10 00 00  00 00 00 40 00 00 1c 24  |...........@...$|
00000010  00 00 00 30 00 00 00 11  00 00 00 10 00 00 00 00  |...0............|
00000020  00 00 01 fb 00 00 1b e4  00 00 00 00 00 00 00 00  |................|
aad55d24bcd293a668ec29c44e996b38  /sys/firmware/fdt
### fdt-b64-begin
AAAAAA==
### fdt-b64-end
### fs-b64-begin
/8IC8r8BAMBP/h9lXQp9AIgEAA==
### fs-b64-end
[    9.906927] reboot: Power down
```

출력에서 볼 것:

- `/proc/device-tree`는 `/sys/firmware/devicetree/base`를 가리키는 링크다.
- 노드는 디렉터리라 `ls`로 속성 목록이 보인다. 커널이 만든 가짜 속성 `name`(노드 이름)도 있다.
- `reg` 파일을 `hexdump`하면 `#address-cells = 2`, `#size-cells = 2`라서 64-bit 주소 `0x0000_0000_0900_0000`과 크기 `0x1000`이 **big-endian**으로 나온다. 셀 수를 모르면 이 바이트를 해석할 수 없다(D2의 `#address-cells` 이야기).
- QEMU `virt` DT에는 `status` 속성이 하나도 없다(모두 켜짐). 실제 SoC dtsi와 정반대다.
- `/sys/firmware/fdt`는 QEMU가 넘긴 1 MiB(`0x00100000`) blob이다. 헤더의 첫 4바이트 `d0 0d fe ed`가 FDT magic, 다음 4바이트가 totalsize다. root만 읽을 수 있다(`-r--------`).
- md5는 부팅마다 바뀐다(아래 7.3절 이유).

### 7.3 예제 21 — 꺼낸 blob과 디렉터리 트리를 호스트에서 비교

무엇을 확인하나: 콘솔에서 받은 base64를 되살려 `dtc -I dtb`와 `dtc -I fs`로 각각 풀고, 둘이 같은지, 그리고 랩에 저장된 `virt.dts`와 무엇이 다른지.

```sh
#!/bin/bash
# extract.sh — 콘솔 로그에서 base64로 받은 DT를 되살려 비교
cd /var/tmp/D5/guest
sed -n "/### fdt-b64-begin/,/### fdt-b64-end/p" console.txt | sed '1d;$d' | base64 -d > guest.dtb
md5sum guest.dtb
rm -rf base; sed -n "/### fs-b64-begin/,/### fs-b64-end/p" console.txt | sed '1d;$d' | base64 -d | tar xz
echo "### dtc -I fs"; dtc -I fs -O dts -o fromfs.dts base 2>&1 | head -2
dtc -q -I dtb -O dts -o fromblob.dts guest.dtb
wc -l fromfs.dts fromblob.dts
diff <(dtc -q -I dts -O dts -s fromfs.dts) <(dtc -q -I dts -O dts -s fromblob.dts) && echo "fs == blob (sorted)"
echo "### blob vs /var/lab/out/virt.dts"
diff <(dtc -q -I dts -O dts -s fromblob.dts) <(dtc -q -I dts -O dts -s /var/lab/out/virt.dts)
```

```text
aad55d24bcd293a668ec29c44e996b38  guest.dtb
### dtc -I fs
fromfs.dts: Warning (clocks_property): /pl061@9030000:clocks: cell 0 is not a phandle reference
fromfs.dts: Warning (clocks_property): /pl031@9010000:clocks: cell 0 is not a phandle reference
  395 fromfs.dts
  395 fromblob.dts
  790 total
fs == blob (sorted)
### blob vs /var/lab/out/virt.dts
24,27c24,25
< 		bootargs = "console=ttyAMA0 quiet LAB_CMD=/d5.sh";
< 		kaslr-seed = <0x00 0x00>;
< 		linux,initrd-end = <0x00 0x48128212>;
< 		linux,initrd-start = <0x00 0x48000000>;
---
> 		kaslr-seed = <0x2b1021cd 0x73a48fc0>;
> 		rng-seed = <0xf0d4837c 0xfd3f0c55 0xb5d8f04e 0x3234f3a1 0xb647931a 0x6f29cd0c 0x3efea411 0xccaf5ca2>;
```

출력에서 볼 것:

- md5가 게스트에서 찍은 값과 같으므로 콘솔 전송이 깨지지 않았다.
- `dtc -I fs`(디렉터리 트리)와 `dtc -I dtb`(blob)가 **정렬 후 완전히 같다.** 디렉터리 입력에서는 속성 순서가 디렉터리 순서라 달라지므로 `-s`로 정렬해서 비교했다. 디렉터리 입력일 때 dtc가 `clocks` 셀을 phandle로 연결하지 못해 경고를 내지만 내용에는 영향이 없다.
- 랩의 `virt.dts`(QEMU `dumpdtb`로 저장한 것, `-append`/`-initrd` 없이)와 비교하면 `/chosen`만 다르다. `bootargs`와 `linux,initrd-*`는 QEMU가 이번 부팅 인자로 채운 것이다(실제 보드에서는 U-Boot가 `bootm`/`booti` 때 채운다).
- **`kaslr-seed`는 0으로 덮였고 `rng-seed`는 사라졌다.** 커널이 부팅 직후 씨앗 값을 쓰고 지우기 때문이다. arm64 `arch/arm64/kernel/pi/kaslr_early.c`의 `get_kaslr_seed()`는 값을 읽은 뒤 `*prop = 0;`으로 덮고, `drivers/of/fdt.c`의 `early_init_dt_scan_chosen()`은 `rng-seed`를 `fdt_nop_property()`로 지운다. 그래서 **`/sys/firmware/fdt`는 "부트로더가 준 그대로"가 아니다.** NOP 처리는 태그만 바꾸고 값 바이트는 blob 안에 남기 때문에, 매 부팅 다른 난수가 들어간 blob의 md5가 매번 달라진다.

### 7.4 예제 22 — ACPI로 부팅한 시스템의 `/sys/firmware/fdt`

무엇을 확인하나: Lima VM 자체(UEFI + ACPI로 부팅한 arm64 Ubuntu)에서는 DT가 어떻게 보이는지.

```sh
limactl shell elinux -- bash -lc 'ls /proc/device-tree 2>&1 | head -3; ls /sys/firmware/;
  sudo ls -la /sys/firmware/fdt; sudo dtc -I dtb -O dts /sys/firmware/fdt 2>&1 | head -30'
```

```text
ls: cannot access '/proc/device-tree': No such file or directory
acpi
devicetree
dmi
efi
fdt
-r-------- 1 root root 560 Oct 10 10:22 /sys/firmware/fdt
/dts-v1/;

/ {
	#size-cells = <0x02>;
	#address-cells = <0x02>;

	chosen {
		linux,uefi-secure-boot = <0x02>;
		kaslr-seed = <0x00 0x00>;
		linux,uefi-mmap-desc-ver = <0x01>;
		linux,uefi-mmap-desc-size = <0x30>;
		linux,uefi-mmap-size = <0x750>;
		linux,uefi-mmap-start = <0x03 0x6fbf7540>;
		linux,uefi-system-table = <0x03 0x6fbd0018>;
		bootargs = "BOOT_IMAGE=/vmlinuz-7.0.0-38-generic root=UUID=e8c86924-872a-4501-b66c-34b21fe8c63f ro console=tty1 console=ttyAMA0";
	};
};
```

출력에서 볼 것: 하드웨어 기술은 ACPI 테이블이 맡으므로 `/proc/device-tree`가 없다. 그래도 `/sys/firmware/fdt`는 있는데, 560바이트짜리 **`/chosen`만 있는 DT**다. arm64 커널의 EFI stub이 UEFI 메모리 맵 위치와 커맨드라인을 커널 본체에 넘기는 통로로 작은 DT를 만들기 때문이다. "arm64 서버/VM = ACPI, 임베디드 보드 = DT"이지만, 커널 진입 인터페이스로서의 FDT는 둘 다 쓴다는 점이 흥미롭다(D1의 ACPI 비교와 연결).

### 7.5 예제 23 — U-Boot의 `fdt` 명령으로 DT 읽고 고치기

부트로더 단계에서도 DT를 읽고 고칠 수 있다. 커널을 다시 빌드하지 않고 장치를 하나 끄거나, 보드 리비전을 EEPROM에서 읽어 속성을 바꿀 때 쓴다(C3에서 U-Boot 부팅 흐름과 함께 자세히). 무엇을 확인하나: 랩의 U-Boot(qemu_arm64)에서 QEMU가 준 DT를 대상으로 읽기·쓰기·노드 만들기.

```text
#!/usr/bin/expect -f
# uboot_fdt.exp — U-Boot 프롬프트에서 fdt 명령으로 DT 읽고 고치기
set timeout 90
spawn qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -bios /var/lab/out/u-boot.bin
expect "Hit any key to stop autoboot"
send "\r"
expect "=> "
foreach c {
  "printenv fdtcontroladdr fdt_addr"
  "fdt addr \$fdt_addr"
  "fdt header"
  "fdt list /pl011@9000000"
  "fdt set /pl011@9000000 status disabled"
  "fdt print /pl011@9000000 status"
  "fdt mknode / d5-test"
  "fdt set /d5-test compatible d5,test"
  "fdt list /d5-test"
} {
  send "$c\r"
  expect "=> "
}
send "poweroff\r"
expect eof
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/D5 && timeout 150 expect /Users/donh/.elinux_scratch/D5/uboot_fdt.exp > uboot.log 2>&1;
  tr -d "\r" < uboot.log | sed -n "/=> /,\$p" | head -80'
```

```text
=> printenv fdtcontroladdr fdt_addr
fdtcontroladdr=5e34ed80
fdt_addr=0x40000000
=> fdt addr $fdt_addr
Working FDT set to 40000000
=> fdt header
magic:			0xd00dfeed
totalsize:		0x100000 (1048576)
off_dt_struct:		0x40
off_dt_strings:		0x1a7c
off_mem_rsvmap:		0x30
version:		17
last_comp_version:	16
boot_cpuid_phys:	0x0
size_dt_strings:	0x19b
size_dt_struct:		0x1a3c
number mem_rsv:		0x0

=> fdt list /pl011@9000000
pl011@9000000 {
	clock-names = "uartclk", "apb_pclk";
	clocks = <0x00008000 0x00008000>;
	interrupts = <0x00000000 0x00000001 0x00000004>;
	reg = <0x00000000 0x09000000 0x00000000 0x00001000>;
	compatible = "arm,pl011", "arm,primecell";
};
=> fdt set /pl011@9000000 status disabled
=> fdt print /pl011@9000000 status
status = "disabled"
=> fdt mknode / d5-test
=> fdt set /d5-test compatible d5,test
=> fdt list /d5-test
d5-test {
	compatible = "d5,test";
};
=> poweroff
poweroff ...
```

출력에서 볼 것: U-Boot 환경에는 DT 주소가 둘 있다. `fdtcontroladdr`는 **U-Boot 자기 자신이 쓰는 DT**(여기서는 RAM 위쪽으로 재배치된 사본)이고, `fdt_addr`는 QEMU가 RAM 시작(`0x40000000`)에 놓아 준 DT, 즉 **커널에 넘길 DT**다. `fdt addr`로 작업 대상을 고른 뒤 `fdt set`/`fdt mknode`로 고친다. 실제 보드에서는 보통 `load mmc 0:1 ${fdt_addr_r} ${fdtfile}`로 파일을 읽고, `fdt addr ${fdt_addr_r}` → `fdt resize` → `fdt set ...` → `booti ${kernel_addr_r} - ${fdt_addr_r}` 순서가 된다. 여기서 고친 것은 **소스 어디에도 남지 않는다**는 점이 디버깅 함정이다(10절 흐름도).

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="300" height="60" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="44" text-anchor="middle" font-size="13">부트로더 (U-Boot)</text>
  <text x="170" y="66" text-anchor="middle" font-size="12">dtb 로드 · /chosen 채움 · fdt 명령 · 오버레이</text>
  <rect x="20" y="110" width="300" height="60" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="134" text-anchor="middle" font-size="12">커널 early: setup_machine_fdt()</text>
  <text x="170" y="156" text-anchor="middle" font-size="12">kaslr-seed 0으로 · rng-seed NOP 처리</text>
  <rect x="20" y="200" width="300" height="60" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="224" text-anchor="middle" font-size="12">unflatten_device_tree()</text>
  <text x="170" y="246" text-anchor="middle" font-size="12">struct device_node 트리 (of_root)</text>
  <line x1="170" y1="80" x2="170" y2="106" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="170" y1="170" x2="170" y2="196" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="180" y="98" font-size="12">x0 레지스터 = dtb 주소</text>
  <rect x="370" y="110" width="290" height="60" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="515" y="134" text-anchor="middle" font-size="12">/sys/firmware/fdt</text>
  <text x="515" y="156" text-anchor="middle" font-size="12">initial_boot_params blob (root 전용)</text>
  <rect x="370" y="200" width="290" height="60" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="515" y="224" text-anchor="middle" font-size="12">/sys/firmware/devicetree/base</text>
  <text x="515" y="246" text-anchor="middle" font-size="12">← /proc/device-tree 심볼릭 링크</text>
  <rect x="370" y="286" width="290" height="60" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="515" y="310" text-anchor="middle" font-size="12">of_platform_default_populate()</text>
  <text x="515" y="332" text-anchor="middle" font-size="12">사용 가능한 노드만 → platform_device</text>
  <line x1="320" y1="140" x2="366" y2="140" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="320" y1="230" x2="366" y2="230" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="250" y1="260" x2="366" y2="310" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="20" y="286" width="300" height="60" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="170" y="310" text-anchor="middle" font-size="12">라벨 · 매크로 · include는</text>
  <text x="170" y="332" text-anchor="middle" font-size="12">런타임에 존재하지 않는다</text>
</svg>
```
그림 6 — 런타임 DT의 흐름. 부트로더가 넘긴 blob을 커널이 펼쳐 `device_node` 트리로 만들고, sysfs의 두 창과 플랫폼 디바이스 생성이 모두 그 트리(또는 원본 blob)에서 나온다.

---

## 8. BSP 팀은 보드 변형을 이렇게 정리한다

### 8.1 SoM 하나, 변형 둘, 캐리어 다섯 = dts 열 개

Verdin iMX8M Plus의 파일 구성은 BSP 팀이 변형을 정리하는 교과서 같은 예다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <rect x="20" y="16" width="640" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="41" text-anchor="middle" font-size="13">imx8mp.dtsi (SoC) ← imx8mp-verdin.dtsi (SoM 공통) — 모든 dts가 공유</text>
  <text x="70" y="88" text-anchor="middle" font-size="12">SoM 변형</text>
  <text x="70" y="104" text-anchor="middle" font-size="12">↓ / 캐리어 →</text>
  <rect x="130" y="70" width="100" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="180" y="89" text-anchor="middle" font-size="13">dev</text>
  <text x="180" y="106" text-anchor="middle" font-size="12">-dev.dtsi</text>
  <rect x="236" y="70" width="100" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="286" y="89" text-anchor="middle" font-size="13">dahlia</text>
  <text x="286" y="106" text-anchor="middle" font-size="12">-dahlia.dtsi</text>
  <rect x="342" y="70" width="100" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="392" y="89" text-anchor="middle" font-size="13">ivy</text>
  <text x="392" y="106" text-anchor="middle" font-size="12">-ivy.dtsi</text>
  <rect x="448" y="70" width="100" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="498" y="89" text-anchor="middle" font-size="13">mallow</text>
  <text x="498" y="106" text-anchor="middle" font-size="12">-mallow.dtsi</text>
  <rect x="554" y="70" width="106" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="607" y="89" text-anchor="middle" font-size="13">yavia</text>
  <text x="607" y="106" text-anchor="middle" font-size="12">-yavia.dtsi</text>
  <rect x="20" y="124" width="100" height="56" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="70" y="148" text-anchor="middle" font-size="13">nonwifi</text>
  <text x="70" y="166" text-anchor="middle" font-size="12">53줄</text>
  <rect x="20" y="190" width="100" height="56" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="70" y="214" text-anchor="middle" font-size="13">wifi</text>
  <text x="70" y="232" text-anchor="middle" font-size="12">Wi-Fi + BT</text>
  <rect x="130" y="124" width="100" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="180" y="148" text-anchor="middle" font-size="12">nonwifi</text>
  <text x="180" y="166" text-anchor="middle" font-size="12">-dev.dts</text>
  <rect x="236" y="124" width="100" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="286" y="148" text-anchor="middle" font-size="12">nonwifi</text>
  <text x="286" y="166" text-anchor="middle" font-size="12">-dahlia.dts</text>
  <rect x="342" y="124" width="100" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="392" y="148" text-anchor="middle" font-size="12">nonwifi</text>
  <text x="392" y="166" text-anchor="middle" font-size="12">-ivy.dts</text>
  <rect x="448" y="124" width="100" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="498" y="148" text-anchor="middle" font-size="12">nonwifi</text>
  <text x="498" y="166" text-anchor="middle" font-size="12">-mallow.dts</text>
  <rect x="554" y="124" width="106" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="607" y="148" text-anchor="middle" font-size="12">nonwifi</text>
  <text x="607" y="166" text-anchor="middle" font-size="12">-yavia.dts</text>
  <rect x="130" y="190" width="100" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="180" y="214" text-anchor="middle" font-size="12">wifi</text>
  <text x="180" y="232" text-anchor="middle" font-size="12">-dev.dts</text>
  <rect x="236" y="190" width="100" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="286" y="214" text-anchor="middle" font-size="12">wifi</text>
  <text x="286" y="232" text-anchor="middle" font-size="12">-dahlia.dts</text>
  <rect x="342" y="190" width="100" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="392" y="214" text-anchor="middle" font-size="12">wifi</text>
  <text x="392" y="232" text-anchor="middle" font-size="12">-ivy.dts</text>
  <rect x="448" y="190" width="100" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="498" y="214" text-anchor="middle" font-size="12">wifi</text>
  <text x="498" y="232" text-anchor="middle" font-size="12">-mallow.dts</text>
  <rect x="554" y="190" width="106" height="56" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="607" y="214" text-anchor="middle" font-size="12">wifi</text>
  <text x="607" y="232" text-anchor="middle" font-size="12">-yavia.dts</text>
  <text x="20" y="276" font-size="12">회색 칸 하나 = 18줄 안팎의 얇은 .dts (include 3줄 + model + compatible). 2 × 5 = 10개.</text>
  <text x="20" y="298" font-size="12">TI AM62 Verdin도 k3-am62-verdin-{dev,dahlia,ivy,mallow,yavia}.dtsi로 같은 행렬을 쓴다.</text>
</svg>
```
그림 7 — 변형 행렬. 행(SoM 변형)과 열(캐리어)을 각각 dtsi 하나로 두고, 교차점마다 얇은 dts를 둔다. 새 캐리어가 생기면 dtsi 하나 + dts 두 개만 추가된다.

### 8.2 정리 규칙 (업스트림과 잘 관리된 BSP에서 공통으로 보이는 것)

1. **SoC dtsi는 정의만, 켜지 않는다.** 칩 내부 블록은 전부 `status = "disabled"`로 두고, 실제로 항상 쓰이는 것(인터럽트 컨트롤러, 타이머, 클록 컨트롤러)만 `status` 없이 둔다. 5절 실측에서 SoC 층의 `okay`가 0개였다.
2. **SoM dtsi는 "모듈 위의 사실"만.** DRAM, PMIC, eMMC, 그리고 SoM 커넥터 핀 → SoC 기능의 pinctrl. 캐리어에 따라 달라지는 장치는 pinctrl만 붙여 두고 꺼 둔다(3.3절 `flexcan1`).
3. **캐리어 dtsi가 켠다.** 트랜시버·커넥터가 실제로 있는 인터페이스를 `okay`로, 캐리어 위의 센서·EEPROM·GPIO 확장기를 자식 노드로.
4. **보드 dts는 조합만.** include 몇 줄 + `model` + `compatible`. 내용이 생기면 그건 SoM 변형이나 캐리어 dtsi로 올린다.
5. **하드웨어 리비전은 별도 dts 또는 오버레이.** rev B → rev C에서 핀 하나가 바뀌면 `-revc.dts`가 rev B dts를 include하고 차이만 덮는다(2.5절 예제와 같은 구조). 옵션 보드(카메라 모듈, 디스플레이)는 오버레이 `.dtso`로 분리한다(D6).
6. **`compatible`은 구체 → 일반 순서.** 보드, 변형, SoM, SoC 순서로 나열해 소프트웨어가 가장 구체적인 것부터 매칭하게 한다.
7. **라벨로 재정의한다.** 경로를 다시 써 내려가는 옛 방식은 SoC dtsi의 버스 구조가 바뀌면 깨진다. 라벨은 위치가 바뀌어도 그대로다.
8. **부트로더 전용 내용은 부트로더 쪽에.** U-Boot는 `arch/arm/dts/<board>-u-boot.dtsi`가 있으면 빌드 때 자동으로 덧붙인다(`scripts/Makefile.lib`의 `u_boot_dtsi_options`가 `<보드>-u-boot.dtsi`, `<SoC>-u-boot.dtsi`, … 순서로 찾아 첫 번째를 쓴다). 예를 들어 `imx8mp-evk-u-boot.dtsi`는 SPL 단계에서도 PMIC 노드가 살아 있도록 `bootph-all;`을 붙인다. 커널 dts를 건드리지 않고 부트로더 요구를 만족시키는 장치다.

```text
&{/soc@0/bus@30800000/i2c@30a20000/pmic@25} {
	bootph-all;
};
```

(위는 랩의 U-Boot 소스 `arch/arm/dts/imx8mp-evk-u-boot.dtsi`의 한 부분. 라벨이 없는 노드를 경로로 다시 연다 — 3.1절의 `&{/path}` 문법이다.)

### 8.3 업스트림 트리의 규모 — 벤더별로 얼마나 많은가

예제 24 — 무엇을 확인하나: arm64 dts 폴더의 벤더별 `.dts`/`.dtsi`/`.dtso` 개수.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55/arch/arm64/boot/dts && for d in */; do
  printf "%s %d %d %d\n" ${d%/} $(ls $d | grep -c "\.dts$") $(ls $d | grep -c "\.dtsi$") $(ls $d | grep -c "\.dtso$"); done |
  sort -k2 -nr | head -12;
  echo total: $(find . -name "*.dts" | wc -l) dts $(find . -name "*.dtsi" | wc -l) dtsi $(find . -name "*.dtso" | wc -l) dtso'
```

```text
qcom 298 208 11
freescale 210 211 61
rockchip 193 99 13
mediatek 98 57 8
amlogic 95 47 3
apple 92 81 0
renesas 77 115 19
allwinner 61 21 0
ti 57 106 48
marvell 33 37 0
xilinx 23 4 5
nvidia 19 29 0
total: 1399 dts 1185 dtsi 169 dtso
```

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13">arch/arm64/boot/dts 벤더 디렉터리별 파일 수 (Linux 6.18.55, 상위 10)</text>
<rect x="110" y="34" width="12" height="12" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="128" y="45" font-size="12">.dts 보드</text>
<rect x="250" y="34" width="12" height="12" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="268" y="45" font-size="12">.dtsi 공유</text>
<rect x="390" y="34" width="12" height="12" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="408" y="45" font-size="12">.dtso 오버레이</text>
<text x="102" y="75" font-size="12" text-anchor="end">qcom</text>
<rect x="110.0" y="62" width="253.3" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="363.3" y="62" width="176.8" height="18" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="540.1" y="62" width="9.3" height="18" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="555.4" y="75" font-size="12">298 / 208 / 11</text>
<text x="102" y="103" font-size="12" text-anchor="end">freescale</text>
<rect x="110.0" y="90" width="178.5" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="288.5" y="90" width="179.3" height="18" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="467.9" y="90" width="51.9" height="18" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="525.7" y="103" font-size="12">210 / 211 / 61</text>
<text x="102" y="131" font-size="12" text-anchor="end">rockchip</text>
<rect x="110.0" y="118" width="164.0" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="274.0" y="118" width="84.1" height="18" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="358.2" y="118" width="11.0" height="18" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="375.2" y="131" font-size="12">193 / 99 / 13</text>
<text x="102" y="159" font-size="12" text-anchor="end">mediatek</text>
<rect x="110.0" y="146" width="83.3" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="193.3" y="146" width="48.4" height="18" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="241.8" y="146" width="6.8" height="18" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="254.6" y="159" font-size="12">98 / 57 / 8</text>
<text x="102" y="187" font-size="12" text-anchor="end">amlogic</text>
<rect x="110.0" y="174" width="80.8" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="190.8" y="174" width="39.9" height="18" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="230.7" y="174" width="2.5" height="18" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="239.2" y="187" font-size="12">95 / 47 / 3</text>
<text x="102" y="215" font-size="12" text-anchor="end">apple</text>
<rect x="110.0" y="202" width="78.2" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="188.2" y="202" width="68.8" height="18" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="263.0" y="215" font-size="12">92 / 81 / 0</text>
<text x="102" y="243" font-size="12" text-anchor="end">renesas</text>
<rect x="110.0" y="230" width="65.5" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="175.4" y="230" width="97.8" height="18" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="273.2" y="230" width="16.1" height="18" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="295.3" y="243" font-size="12">77 / 115 / 19</text>
<text x="102" y="271" font-size="12" text-anchor="end">allwinner</text>
<rect x="110.0" y="258" width="51.9" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="161.8" y="258" width="17.8" height="18" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="185.7" y="271" font-size="12">61 / 21 / 0</text>
<text x="102" y="299" font-size="12" text-anchor="end">ti</text>
<rect x="110.0" y="286" width="48.4" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="158.4" y="286" width="90.1" height="18" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="248.5" y="286" width="40.8" height="18" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<text x="295.3" y="299" font-size="12">57 / 106 / 48</text>
<text x="102" y="327" font-size="12" text-anchor="end">marvell</text>
<rect x="110.0" y="314" width="28.1" height="18" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="138.1" y="314" width="31.4" height="18" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="175.5" y="327" font-size="12">33 / 37 / 0</text>
<line x1="110" y1="56" x2="110" y2="340" stroke="currentColor"/>
<text x="20" y="366" font-size="12">전체: .dts 1399 · .dtsi 1185 · .dtso 169. defconfig로 make dtbs → .dtb 1577개, 53 s (6 vCPU).</text>
</svg>
```
그림 8 — 벤더 폴더별 파일 수(실측, 상위 10). `.dtsi`가 `.dts`보다 많은 벤더(freescale, renesas, ti)는 SoM 층이나 도메인별 분할을 많이 쓴다. TI는 오버레이(`.dtso`) 비중이 크다 — 확장 보드·카메라를 오버레이로 다루는 문화다.

> 벤더 BSP 트리(NXP `linux-imx`, TI `ti-linux-kernel` 등)는 업스트림과 파일 이름·노드가 다를 수 있다. 업스트림 기준으로 이 구조를 익혀 두면 벤더 트리를 읽을 때 "어디가 벤더가 덧댄 부분인지"가 보인다. 회사 보드 dts를 업스트림 커널로 옮길 때 가장 많이 하는 일이 바로 이 차이를 정리하는 것이다.

---

## 9. 커널 안에서는 무슨 일이 일어나나

DT 파일 구조(층, 라벨, include, 매크로)는 **빌드 타임 개념**이다. 커널이 받는 것은 그 결과인 평평한 dtb 하나뿐이고, 커널 안에는 라벨도 include도 없다. 커널 쪽 경로를 따라가면 다음과 같다(6.18 기준, 함수 이름은 `drivers/of/`와 `arch/arm64/kernel/`에서 확인).

1. **진입**: 부트로더가 arm64 커널에 `x0` = dtb 물리 주소를 넘긴다. `setup_arch()` → `setup_machine_fdt(__fdt_pointer)`가 blob을 매핑하고 `/chosen`(커맨드라인, initrd 위치), `/memory`를 먼저 읽는다. 이때 `early_init_dt_scan_chosen()`이 `rng-seed`를 엔트로피로 쓰고 지운다(7.3절). 이 시점의 blob 포인터가 `initial_boot_params`다. (`kaslr-seed`는 그보다 더 이른 `arch/arm64/kernel/pi/`의 초기 매핑 코드 `kaslr_early_init()` → `get_kaslr_seed()`가 읽고 0으로 덮는다.)
2. **펼치기**: `unflatten_device_tree()`가 blob을 걸어 `struct device_node`(이름, `phandle`, `properties` 연결 리스트, `parent`/`child`/`sibling`, `fwnode`) 트리를 만들고 루트를 `of_root`에 둔다. 속성은 `struct property`(이름, 길이, 값 포인터)다. 5절의 `fdtstat.py`가 한 일이 축소판이다.
3. **sysfs 노출**: `of_core_init()`이 `/sys/firmware/devicetree` kset을 만들고 각 노드를 kobject로 붙인 뒤(`__of_attach_node_sysfs()`), `proc_symlink("device-tree", NULL, "/sys/firmware/devicetree/base")`로 `/proc/device-tree` 링크를 만든다. `late_initcall`인 `of_fdt_raw_init()`은 blob의 CRC가 부팅 초기 값과 같을 때만 `/sys/firmware/fdt`(root 전용 bin attribute)를 만든다 — 누가 blob을 몰래 고쳤으면 원본이라고 내보내지 않는다.
4. **디바이스 생성**: `of_platform_default_populate()`가 루트 아래 `simple-bus` 등을 따라 내려가며 노드마다 `platform_device`를 만든다. 이때 `of_device_is_available()`을 검사하는데, 이 함수는 **`status`가 없거나 `"okay"` 또는 `"ok"`일 때만 참**이다(`drivers/of/base.c`). 그래서 SoC dtsi의 `"disabled"` 노드나 3.7절의 `"okey"` 노드에는 디바이스가 생기지 않는다.
5. **드라이버 매칭**: 만들어진 `platform_device`의 `compatible`이 드라이버의 `of_match_table`과 맞으면 `probe()`가 불린다(D3, G2). 드라이버는 `of_property_read_u32()` 등으로 이 노드의 속성을 읽는다(D8). 3.3절에서 SoC·SoM·캐리어가 나눠 쓴 `flexcan1` 속성은 이 시점에 **한 노드의 속성 리스트로** 드라이버(`drivers/net/can/flexcan/`)에게 보인다. 드라이버는 그 값이 어느 파일에서 왔는지 모르고 알 필요도 없다.
6. **런타임 변경**: 오버레이(`CONFIG_OF_OVERLAY`, 랩 커널에 켜져 있음)는 `device_node` 트리를 실행 중에 고치고, 바뀐 노드에 대해 디바이스를 추가·제거한다(D6). 이때 라벨 해석을 위해 베이스에 `__symbols__`가 필요하다(4.5절).

드라이버를 쓸 때 이 지식이 다시 나오는 지점은 셋이다. (1) `probe()`가 안 불리면 먼저 "노드가 `/proc/device-tree`에 있고 사용 가능 상태인가"를 확인한다 — 파일 구조 문제(이 노트)인지 매칭 문제(D3)인지 가르는 첫 질문이다. (2) 속성을 읽을 때 `-EINVAL`/`-ENODATA`가 나오면 최종 트리에서 그 속성이 덮이거나 지워지지 않았는지 본다. (3) dt-bindings 헤더의 숫자는 드라이버와 공유하는 ABI이므로, 새 클록·리셋 ID를 추가할 때 헤더·드라이버·dts를 한 패치 시리즈로 바꾼다.

---

## 10. Embedded 실무에서 다시 보기

### 10.1 "CAN 2가 안 올라온다" — 캐리어마다 다르다

센서 키트를 Verdin + Yavia 캐리어로 만들었는데 `ip link`에 `can1`이 없다. SoC 매뉴얼에는 FlexCAN이 둘이고, SoM 문서에도 CAN_2 핀이 있다. 3.4절 표처럼 최종 dtb를 보면 Yavia에서는 `flexcan2`가 `disabled`다. 고치는 곳은 SoC dtsi도 SoM dtsi도 아니고 **우리 캐리어 dts**다.

```dts
/* our-sensorkit-yavia.dts — 회사 보드 파일 (업스트림 파일은 건드리지 않는다) */
/dts-v1/;
#include "imx8mp-verdin.dtsi"
#include "imx8mp-verdin-nonwifi.dtsi"
#include "imx8mp-verdin-yavia.dtsi"

/ {
	model = "ACME Sensor Kit on Verdin iMX8M Plus (Yavia)";
	compatible = "acme,sensorkit-yavia", "toradex,verdin-imx8mp-nonwifi",
		     "toradex,verdin-imx8mp", "fsl,imx8mp";
};

&flexcan2 {
	status = "okay";	/* 우리 보드에는 두 번째 트랜시버가 달려 있다 */
};
```

단, 캐리어가 그 핀을 다른 용도(GPIO 등)로 이미 쓰고 있으면 pinctrl 충돌로 probe가 실패한다. `okay`만 켜고 끝내지 말고 그 핀이 캐리어 dtsi에서 무엇으로 쓰였는지 확인해야 한다.

### 10.2 rev C에서 IMU 인터럽트 핀이 바뀌었다

하드웨어 팀이 rev C에서 IMU INT를 다른 GPIO로 옮겼다. 선택지는 둘이다. (1) `sensorkit-revc.dts`가 `sensorkit-revb.dts`를 include하고 `&imu { interrupts-extended = ...; };`만 덮는다(2.5절 예제). (2) 부트로더가 보드 EEPROM의 리비전을 읽어 맞는 dtb를 고르거나(`fdtfile` 환경 변수), `fdt set`으로 고친다(7.5절). 생산 라인에서 두 리비전이 섞여 돌 때는 (2)가 흔하다. 어느 쪽이든 "이 보드에서 실제로 어떤 DT가 돌고 있나"는 `/proc/device-tree/model`과 해당 속성을 읽어 확인한다.

### 10.3 "dts를 고쳤는데 반영이 안 된다"

가장 흔한 신고다. 원인은 거의 항상 빌드가 아니라 **배포**다. 부트 파티션에 dtb가 여러 개 있고 U-Boot가 다른 이름을 읽거나, Yocto 이미지가 커널 레시피의 `KERNEL_DEVICETREE`에 없는 dtb는 설치하지 않거나, 부트로더가 FIT 이미지 안의 dtb를 쓰는 경우다. 아래 흐름으로 좁힌다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="14" width="380" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="210" y="33" text-anchor="middle" font-size="12">증상: 장치가 안 뜬다 / 속성 값이 소스와 다르다</text>
  <text x="210" y="50" text-anchor="middle" font-size="12">(DT 고쳤는데 반영 안 됨 포함)</text>
  <rect x="20" y="86" width="380" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="107" text-anchor="middle" font-size="12">1. 보드에서: /proc/device-tree/model이 내 dts인가?</text>
  <text x="210" y="127" text-anchor="middle" font-size="12">노드가 있고 status가 okay인가?</text>
  <rect x="20" y="168" width="380" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="189" text-anchor="middle" font-size="12">2. dtdiff 빌드한.dtb /proc/device-tree</text>
  <text x="210" y="209" text-anchor="middle" font-size="12">값이 빌드 결과와 같은가?</text>
  <rect x="20" y="250" width="380" height="54" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="271" text-anchor="middle" font-size="12">3. 빌드 결과가 소스 의도와 같은가?</text>
  <text x="210" y="291" text-anchor="middle" font-size="12">.dts.tmp 평탄화 · dtc 'also defined at'</text>
  <rect x="20" y="332" width="380" height="54" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="210" y="353" text-anchor="middle" font-size="12">DT는 맞다 → compatible·드라이버 매칭 (D3)</text>
  <text x="210" y="373" text-anchor="middle" font-size="12">리소스·deferred probe (D4 · G2)</text>
  <line x1="210" y1="58" x2="210" y2="82" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="210" y1="140" x2="210" y2="164" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="210" y1="222" x2="210" y2="246" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="210" y1="304" x2="210" y2="328" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="218" y="156" font-size="12">예</text>
  <text x="218" y="238" font-size="12">예</text>
  <text x="218" y="320" font-size="12">예</text>
  <rect x="440" y="86" width="220" height="54" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="550" y="107" text-anchor="middle" font-size="12">다른 dtb가 로드됨:</text>
  <text x="550" y="127" text-anchor="middle" font-size="12">fdtfile · 설치 목록 · FIT</text>
  <rect x="440" y="168" width="220" height="54" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="550" y="189" text-anchor="middle" font-size="12">부트로더가 고쳤다:</text>
  <text x="550" y="209" text-anchor="middle" font-size="12">fdt set · 오버레이 · fixup</text>
  <rect x="440" y="250" width="220" height="54" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="550" y="271" text-anchor="middle" font-size="12">나중 층이 덮었다:</text>
  <text x="550" y="291" text-anchor="middle" font-size="12">include 순서 · 라벨 · 오타</text>
  <line x1="400" y1="113" x2="436" y2="113" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="400" y1="195" x2="436" y2="195" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="400" y1="277" x2="436" y2="277" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="418" y="106" text-anchor="middle" font-size="12">아니오</text>
  <text x="418" y="188" text-anchor="middle" font-size="12">다름</text>
  <text x="418" y="270" text-anchor="middle" font-size="12">다름</text>
  <text x="20" y="410" font-size="12">도구: 1 = cat · hexdump, 2 = dtdiff(디렉터리 입력), 3 = dtc -O dts -s · grep &amp;label</text>
</svg>
```
그림 9 — "노드가 이상할 때 어디를 보나" 흐름. 위에서부터 **보드 → 빌드 결과 → 소스** 순서로 범위를 좁힌다. 소스부터 읽기 시작하면 배포·부트로더 문제를 놓친다.

### 10.4 업스트림에 보드 dts를 올릴 때

회사 보드를 업스트림에 올리면 리뷰어가 보는 것은 대부분 이 노트의 규칙이다. 보드 전용 내용이 SoC dtsi에 들어가지 않았는가, `status = "okay"`가 보드 파일에만 있는가, 노드를 라벨로 열었는가, `compatible`의 첫 항목이 보드 고유 문자열이고 그 문자열이 바인딩 문서(`Documentation/devicetree/bindings/arm/fsl.yaml` 같은 보드 목록)에 추가됐는가, `make dtbs_check`가 깨끗한가(D7). 그리고 `arch/arm64/boot/dts/<vendor>/Makefile`에 `dtb-$(CONFIG_ARCH_...) += <board>.dtb` 한 줄을 추가해야 `make dtbs`에 포함된다(4.1절).

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 보드 변경을 SoC/SoM dtsi에 직접 씀 | 다른 보드 수십 개의 동작이 바뀜, 업스트림 머지 충돌 | 파급 범위 무시 (SoC dtsi = 56개 dtb) | 최상위 보드 파일에서 `&label`로 덮는다 |
| `status = "okey"` 같은 오타 | 빌드 성공, 장치만 조용히 안 뜸 | `of_device_is_available()`은 `okay`/`ok`만 인정 | `make dtbs_check`, 리뷰 때 `status` 값 grep |
| include 순서를 바꿈 | 어떤 속성 값이 예전과 다름 | 병합은 펼친 텍스트 순서대로, 나중이 이김 | 공통 → 구체 순으로 include, 평탄화 소스로 확인 |
| `/include/`로 매크로 쓰는 파일을 읽음 | `syntax error` (매크로 위치) | `/include/`는 cpp를 거치지 않음 | `#include`로 바꾼다 |
| cpp를 직접 돌리며 `-undef` 누락 | `linux,code`가 `1,code`가 되어 이상한 에러 | gcc가 `linux=1`을 미리 정의 | 커널과 같은 `dtc_cpp_flags`를 쓴다 |
| 끄려고 `/delete-node/` 사용 | 다른 노드가 참조하면 빌드 실패, 오버레이가 대상 못 찾음 | 노드 자체가 사라짐 | 끌 때는 `status = "disabled"` |
| dtb끼리 `dtdiff`로 리뷰 | 수천 줄 diff, 진짜 변경을 못 찾음 | phandle 번호가 통째로 밀림 | `.dts.tmp`를 `dtc -O dts -s`로 평탄화해 비교 |
| `fdtget`에 라벨 이름을 줌 | `FDT_ERR_NOTFOUND` | dtb에는 라벨이 없음 | 경로를 쓰거나 `/__symbols__`·`/aliases` 조회 |
| 오버레이 베이스를 `-@` 없이 빌드 | 오버레이 적용 실패 | `__symbols__` 없음 | Makefile에 `-dtbs` 합성 규칙을 두거나 `DTC_FLAGS_<board> := -@` |
| dts 고치고 커널만 다시 설치 | 보드 DT가 그대로 | 부트로더가 다른 dtb/FIT를 읽음 | `/proc/device-tree/model` 확인, `fdtfile`·설치 목록 점검 |

---

## 12. 면접에서 이렇게 말한다

**Q.** What is the difference between a .dtsi and a .dts file?

**A.** `.dtsi`는 include용 조각이고 단독으로 dtb가 되지 않는다. 보통 SoC 전체(칩 벤더), SoM(모듈 벤더)처럼 여러 보드가 공유하는 하드웨어를 기술한다. `.dts`는 보드 하나에 대응하고 `/dts-v1/;`로 시작해 dtb 하나가 된다. 보드 dts가 SoC·SoM dtsi를 include하고, 라벨로 노드를 다시 열어 실제로 쓰는 장치만 `okay`로 켠다.

> A .dtsi is a shared fragment that's never compiled on its own — typically the SoC description from the silicon vendor, or the SoM description from the module vendor. A .dts describes one board and produces one .dtb. The board file includes the SoC and SoM .dtsi files and then re-opens nodes by label to enable just the peripherals that are actually wired up on that board.

**꼬리 질문**: SoC dtsi에서 장치를 왜 disabled로 두나? → 칩에 블록이 있어도 보드에 핀·트랜시버가 연결됐는지는 보드만 안다. 꺼 두고 보드가 켜야 불필요한 드라이버 probe와 핀 충돌을 막는다.

**Q.** How do you enable or change a peripheral for your board without touching the SoC file?

**A.** 보드 파일에서 그 노드의 라벨로 `&uart2 { status = "okay"; pinctrl-0 = <...>; };`처럼 다시 연다. dtc는 같은 노드의 정의를 텍스트 순서대로 병합하고, 같은 속성은 나중 값이 이긴다. 속성을 없애려면 `/delete-property/`, 칩에 없는 블록이면 `/delete-node/ &label;`을 쓰지만, 단순히 끄는 것은 `status = "disabled"`가 맞다.

> In the board file I re-open the node by its label, like &uart2 { status = "okay"; }, and add pinctrl or child nodes there. dtc merges all definitions of the same node in the order they appear after preprocessing, and the last value for a property wins. I'd use /delete-property/ or /delete-node/ only when something truly doesn't exist; to just turn a device off I set status to disabled.

**Q.** How are device tree files built in the kernel?

**A.** `make dtbs`나 `make <vendor>/<board>.dtb`. Kbuild의 `scripts/Makefile.dtbs`에 있는 `cmd_dtc`가 먼저 `gcc -E`로 `#include`와 `dt-bindings` 매크로를 펼치고(`-nostdinc -undef -D__DTS__ -x assembler-with-cpp`), 그 결과를 커널 동봉 dtc로 컴파일한다. 의존성 파일 덕분에 dtsi나 헤더를 고치면 영향받는 dtb만 다시 빌드된다. 오버레이 베이스 dtb에는 자동으로 `-@`가 붙는다.

> With make dtbs, or make vendor/board.dtb for a single one. The rule in scripts/Makefile.dtbs runs the C preprocessor first to expand includes and the dt-bindings macros, then the in-tree dtc compiles the result into a blob. Dependency files are generated, so touching a .dtsi only rebuilds the boards that include it. Boards used as overlay bases are built with -@ so the labels survive as __symbols__.

**Q.** Why does the device tree use C preprocessor headers like dt-bindings?

**A.** 클록·리셋·인터럽트·GPIO 플래그 같은 숫자 ID를 DT와 C 드라이버가 같은 헤더로 공유하려고다. 예를 들어 `IMX8MP_CLK_CAN1_ROOT`는 233이고 클록 드라이버도 같은 헤더를 include한다. 숫자는 출시된 dtb에 박히므로 ABI이고 바꾸면 안 된다. 다만 cpp가 처리하므로 decompile하면 숫자만 남는다.

> They give the DT and the C driver one shared source of truth for numeric IDs — clock indices, reset lines, interrupt types, GPIO flags. The clock driver includes the same header as the .dtsi, so they can't drift apart. Once a dtb ships, those numbers are ABI. The macros vanish after preprocessing, so a decompiled dtb shows raw numbers.

**Q.** How do you check which device tree is actually running on a board?

**A.** `/proc/device-tree`(= `/sys/firmware/devicetree/base`)에서 `model`과 문제 노드를 읽는다. 전체가 필요하면 `dtc -I fs -O dts /proc/device-tree`, 또는 `/sys/firmware/fdt` blob을 복사해서 `dtc -I dtb`로 푼다. 빌드한 dtb와는 `dtdiff`로 비교한다. 부트로더가 `/chosen`이나 다른 속성을 고쳤을 수 있고, 커널도 `kaslr-seed`·`rng-seed`를 지우므로 blob이 빌드 결과와 바이트 단위로 같지는 않다.

> I read /proc/device-tree — model first, then the node in question. For the whole tree, dtc -I fs -O dts /proc/device-tree, or copy /sys/firmware/fdt and decompile it. Then dtdiff it against the dtb I built. Expect /chosen to differ: the bootloader fills in bootargs and initrd addresses, and the kernel wipes kaslr-seed and rng-seed, so it's never byte-identical.

**꼬리 질문**: 보드에 dtc가 없으면? → `cat`/`hexdump`로 속성을 읽거나, `/sys/firmware/fdt`를 base64로 콘솔에 찍어 호스트에서 되살린다.

**Q.** You changed the .dts, rebuilt, and nothing changed on the board. How do you debug?

**A.** 먼저 보드에서 `/proc/device-tree/model`이 내 dts인지 본다. 다르면 배포 문제다 — U-Boot `fdtfile`, Yocto `KERNEL_DEVICETREE`, FIT 이미지. 같으면 `dtdiff`로 빌드 dtb와 런타임 트리를 비교해 부트로더 fixup이나 오버레이를 찾는다. 그것도 같으면 빌드 결과가 소스 의도와 다른 것이므로 `.dts.tmp`를 평탄화해서 나중 층이 덮었는지, 라벨 오타가 있는지 본다.

> I start on the board, not in the source. If /proc/device-tree/model isn't my board, it's a deployment problem — wrong fdtfile, the dtb not in the image, or a FIT with an old blob. If it is, I dtdiff the built dtb against the live tree to catch bootloader fixups or overlays. If those match, I flatten the preprocessed source to see whether a later include overrode my property.

**Q.** How would you organize device trees for one SoM used on several carrier boards and hardware revisions?

**A.** SoC dtsi는 업스트림 그대로 두고, SoM 공통 dtsi 하나, SoM 변형(예: Wi-Fi 유무) dtsi, 캐리어마다 dtsi 하나를 두고, 조합마다 include 몇 줄짜리 얇은 dts를 둔다. Toradex Verdin이 2 × 5 = 10개 dts를 이렇게 관리한다. 리비전 차이는 이전 리비전 dts를 include하고 차이만 덮는 dts나 오버레이로, 옵션 보드는 오버레이로 분리한다.

> Keep the SoC .dtsi untouched, one .dtsi for what's on the SoM, one per SoM variant, one per carrier, and then a tiny .dts per combination that's just includes, model and compatible. Toradex's Verdin does exactly that — two module variants times five carriers gives ten board files. Board revisions get a .dts that includes the previous revision and overrides only the delta, and add-on boards become overlays.

---

## 13. 직접 해보기

1. (개념) 같은 노드를 SoC dtsi가 `clock-frequency = <100000>`, SoM dtsi가 `<400000>`으로 정의하고 보드 dts는 SoM보다 **SoC dtsi를 나중에** 다시 include했다(가드 없이). 최종 값은? — 정답/힌트: 텍스트 순서상 마지막이 SoC 정의이므로 100000. include 가드가 없는 dtsi를 두 번 include하면 병합 결과가 뒤집힌다. 그래서 dtsi는 한 경로로만 include한다.
2. (코드) 2.3절 작은 트리에 `d5_spi1: spi@6000`을 SoC 층에 `disabled`로 추가하고, 캐리어 층에서 켜서 자식으로 `flash@0`을 단 뒤, `fdtget -l`로 확인하라. — 정답/힌트: SPI 컨트롤러에는 `#address-cells = <1>; #size-cells = <0>;`이 필요하고 자식 `reg = <0>`은 chip select 번호다.
3. (관찰) `imx8mp-verdin-wifi-ivy.dtb`와 `imx8mp-verdin-wifi-dahlia.dtb`의 차이를 6.2절 방식(평탄화 + phandle 제외 diff)으로 뽑고, 캐리어 차이를 한 문단으로 정리하라. — 정답/힌트: `.dts.tmp` 두 개를 `dtc -I dts -O dts -s`로 풀고 `grep -v "phandle = "` 후 diff.
4. (관찰) QEMU 게스트에서 `find /sys/firmware/devicetree/base -name compatible | wc -l`과 `ls /sys/bus/platform/devices | wc -l`을 비교하고, 숫자가 다른 이유를 9절로 설명하라. — 정답/힌트: 모든 노드가 platform_device가 되지는 않는다(버스 아래 자식, `cpus`·`memory` 같은 특수 노드, 다른 버스(virtio-mmio, PCI)가 맡는 노드).
5. (코드) 5절 `fdtstat.py`를 확장해 `status`가 없는 노드도 "켜짐"으로 세는 열을 추가하고, L1~L5에서 그 값이 어떻게 변하는지 그래프로 그려라. — 정답/힌트: FDT_BEGIN_NODE에서 스택에 플래그를 쌓고 FDT_END_NODE에서 꺼내며 판정한다.
6. (빌드) `make O=/var/tmp/D5/build KBUILD_EXTRA_WARN=1 freescale/imx8mp-verdin-nonwifi-dev.dtb`로 다시 빌드해 꺼져 있던 dtc 경고가 몇 개 나오는지 세어 보라(먼저 dtb를 지워야 다시 빌드된다). — 정답/힌트: 4.1절 Makefile의 `ifeq ($(findstring 1,$(KBUILD_EXTRA_WARN)),)` 분기가 바뀐다.
7. (U-Boot) 7.5절 expect 스크립트를 고쳐 `fdt rm /pl011@9000000 status` 후 `booti`로 커널까지 부팅하고, 게스트에서 해당 노드의 `status` 파일이 없는지 확인하라. — 정답/힌트: U-Boot `fdt rm <path> <prop>`, 커널·initramfs는 `load`가 아니라 QEMU `-kernel`/`-initrd`로 미리 올려 두면 간단하다(C3 참고).

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| `.dts` | Device Tree Source | 보드 하나 = dtb 하나가 되는 최상위 소스 |
| `.dtsi` | DTS Include | 여러 보드가 공유하는 조각 (SoC, SoM, 캐리어) |
| `.dtso` / `.dtbo` | DT overlay source / blob | 실행 중이나 부트로더에서 베이스에 덧붙이는 조각 (D6) |
| dtb / FDT | Flattened Device Tree blob | dtc가 만든 바이너리. magic `0xd00dfeed` |
| SoM | System on Module | SoC + DRAM + PMIC + eMMC를 얹은 꽂는 모듈 |
| 캐리어 보드 | carrier board | SoM을 꽂는 회사 고유 보드. 커넥터·트랜시버·센서 |
| `dt-bindings` 헤더 | `include/dt-bindings/` | DT와 C 드라이버가 공유하는 숫자 상수 |
| linemarker | `# 줄 "파일" 플래그` | cpp가 남기는 원래 위치 표시. dtc 에러 메시지에 쓰임 |
| 라벨 | `name:` | 노드 별명. 빌드 때만 존재, `&name`으로 참조·재정의 |
| phandle | `phandle = <N>` | 런타임에 노드를 가리키는 32-bit 숫자. 라벨 참조가 이것으로 바뀜 |
| `__symbols__` | `-@` 빌드 결과 노드 | 라벨 → 경로 표. 오버레이 적용에 필요 |
| `/delete-node/` | dtc 지시어 | 노드를 트리에서 지움 |
| `/delete-property/` | dtc 지시어 | 현재 노드의 속성 하나를 지움 |
| `/omit-if-no-ref/` | dtc 지시어 | 아무도 참조하지 않으면 노드를 dtb에서 뺌 |
| `/include/` | dtc 자체 include | cpp를 거치지 않음 (매크로 불가) |
| `cmd_dtc` | Kbuild 명령 | `gcc -E` + `dtc` + 의존성 합치기 |
| `dtbs-list` | 빌드 산출물 | `make dtbs`가 만든 dtb 목록, `dtbs_install`이 사용 |
| `/sys/firmware/fdt` | sysfs bin 파일 | 부팅 때 받은 blob (커널이 seed는 지움) |
| `of_root` | 커널 전역 | 펼쳐진 `device_node` 트리의 루트 |
| `of_device_is_available()` | 커널 함수 | `status` 없음 또는 `okay`/`ok`일 때만 참 |

---

## 15. 요약 & 체크리스트

DT 파일 구조는 하드웨어 계층(SoC → SoM → 캐리어 → 보드)을 파일 계층(`.dtsi` → `.dtsi` → `.dtsi` → `.dts`)으로 옮긴 것이다. `#include`는 cpp가 처리하는 텍스트 붙여넣기라서 계층의 의미는 결국 "펼친 텍스트의 순서"이고, dtc는 같은 노드의 정의를 그 순서대로 병합한다(같은 속성은 나중 값, 자리는 처음 위치, 새 속성은 끝에). 위층은 `&label { }`로 아래층 노드를 다시 열어 `status = "okay"`로 켜고, 필요하면 `/delete-property/`·`/delete-node/`로 지우며, SoC 벤더는 `/omit-if-no-ref/`로 안 쓰는 핀 그룹을 뺀다. 커널은 `scripts/Makefile.dtbs`의 `cmd_dtc`로 `gcc -E` → `dtc`를 돌려 dtb를 만들고, 의존성 파일로 증분 빌드하며, 오버레이 베이스에만 `-@`로 `__symbols__`를 남긴다. 런타임에는 라벨·매크로·include가 모두 사라지고 `device_node` 트리만 남는다. 그 트리는 `/proc/device-tree`(= `/sys/firmware/devicetree/base`)로, 원본 blob은 `/sys/firmware/fdt`로 볼 수 있으며, 디버깅은 항상 보드 → 빌드 결과 → 소스 순서로 좁힌다. Verdin iMX8M Plus 실측에서 SoC 층은 노드 280개를 정의만 하고 `okay`가 0개였고, 캐리어 층이 `okay` 35개를 켰다.

- [ ] SoC·SoM·캐리어·보드 층이 각각 무엇을 기술하는지 실제 파일 이름(`imx8mp.dtsi` → `imx8mp-verdin.dtsi` → `imx8mp-verdin-dev.dtsi` → `imx8mp-verdin-nonwifi-dev.dts`)으로 설명할 수 있다
- [ ] `#include`와 `/include/`의 차이, 커널이 cpp에 주는 `-nostdinc -undef -D__DTS__ -x assembler-with-cpp`의 이유를 설명할 수 있다
- [ ] `dt-bindings` 헤더가 DT와 드라이버 사이의 ABI인 이유를 예로 들 수 있다
- [ ] 같은 노드가 여러 번 정의될 때 병합 규칙(나중 값, 자리 유지, 끝에 추가, 텍스트 순서)을 예측할 수 있다
- [ ] `&label`, `&{/path}`, `/delete-node/`, `/delete-property/`, `/omit-if-no-ref/`를 상황에 맞게 쓸 수 있다
- [ ] `make O=... <vendor>/<board>.dtb`로 dtb 하나만 빌드하고 `V=1`로 실제 명령을 볼 수 있다
- [ ] `.dts.tmp`와 `.d` 파일로 include 트리와 재빌드 파급 범위를 찾을 수 있다
- [ ] `-@`와 `__symbols__`가 무엇이고 왜 오버레이 베이스에만 붙는지 설명할 수 있다
- [ ] `dtc`, `fdtget`, `fdtput`, `fdtdump`, `dtdiff`로 dtb를 읽고 고치고 비교할 수 있다
- [ ] 보드에서 `/proc/device-tree`와 `/sys/firmware/fdt`로 런타임 DT를 꺼내 빌드 결과와 비교하고, 다른 부분(`/chosen`, seed)의 이유를 말할 수 있다
- [ ] "dts를 고쳤는데 반영이 안 된다"를 보드 → 빌드 결과 → 소스 순서로 디버깅할 수 있다
- [ ] SoM 하나에 변형·캐리어·리비전이 여럿인 제품의 DT 파일 구성을 설계할 수 있다

## 참고 자료

- Linux kernel documentation — Devicetree: [https://docs.kernel.org/devicetree/index.html](https://docs.kernel.org/devicetree/index.html) (특히 "Linux and the Devicetree", "Devicetree Overlay Notes")
- Devicetree Specification (devicetree.org): [https://www.devicetree.org/specifications/](https://www.devicetree.org/specifications/) — dtb 형식(헤더, struct/strings 블록, 토큰)
- dtc 소스와 문서 `Documentation/dt-object-internal.txt`, `Documentation/manual.txt`: [https://git.kernel.org/pub/scm/utils/dtc/dtc.git](https://git.kernel.org/pub/scm/utils/dtc/dtc.git)
- Bootlin, Thomas Petazzoni "Device Tree 101" 웨비나 슬라이드 (2021) — bootlin.com 자료 페이지에서 검색
- 커널 소스 (6.18.55, elixir에서 같은 경로): `scripts/Makefile.dtbs`, `Makefile`(Devicetree files 절), `scripts/dtc/include-prefixes/`, `drivers/of/fdt.c`(`unflatten_device_tree`, `early_init_dt_scan_chosen`, `of_fdt_raw_init`), `drivers/of/base.c`(`of_core_init`, `of_device_is_available`), `drivers/of/platform.c`, `arch/arm64/kernel/pi/kaslr_early.c` — [https://elixir.bootlin.com/linux/v6.18/source](https://elixir.bootlin.com/linux/v6.18/source)
- 이 노트의 실제 체인: `arch/arm64/boot/dts/freescale/imx8mp.dtsi`, `imx8mp-verdin*.dtsi`, `imx8mp-verdin-*-*.dts`, `arch/arm64/boot/dts/ti/k3-am625-sk.dts`
- U-Boot 문서 [https://docs.u-boot.org/](https://docs.u-boot.org/) — Usage → Shell commands의 `fdt` 항목. 자동 `-u-boot.dtsi` 포함 규칙은 U-Boot 소스 `scripts/Makefile.lib`
- 관련 노트: D1(왜 DT인가), D2(문법), D3(드라이버 매칭), D4(자원 기술), D6(Overlay), D7(Bindings와 검증), D8(드라이버에서 DT 읽기), C3(U-Boot), B4(Make·Kbuild)
