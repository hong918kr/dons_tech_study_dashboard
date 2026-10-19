# P2. BSP 구조와 벤더 커널 — 벤더가 주는 꾸러미를 해부하고, 우리 패치 스택을 git·quilt·Yocto로 관리하기

> **이 노트를 다 읽으면**: 벤더 BSP(부트 체인 포크, 벤더 커널, DT, 펌웨어·유저스페이스 블롭, Yocto 레이어, 릴리스 노트·errata)가 무엇으로 이루어졌는지 그림으로 설명할 수 있다 · 벤더 커널과 mainline LTS 중 무엇을 쓸지 블롭 ABI 락인·보안 수정·수명·upstream 드라이버 기준으로 근거를 대며 고를 수 있다 · 토픽 브랜치 → 선형 통합 브랜치 → `git format-patch`/quilt/Yocto 패치 디렉터리로 패치 스택을 직접 만들고, `Upstream-Status`로 어떤 패치가 upstream에 갔는지 추적할 수 있다 · `meta-<company>` 레이어와 재현 가능한 패치 내보내기의 요령을 말할 수 있다
> **왜 필요한가**: 입사 첫 주에 받는 것이 "벤더 BSP tarball + EVK"이고, 면접에서는 "vendor kernel or mainline?", "How do you manage your patches on top of the vendor BSP?"를 거의 반드시 묻는다
> **Don 기준 난이도**: SSD 컨트롤러 벤더 SDK 위에 자사 패치를 얹어 양산 펌웨어를 관리해 본 경험이 그대로 통한다 / 새로 배울 부분은 **Linux 생태계의 구성품 이름**(TF-A, OP-TEE, meta-imx, galcore …)과 **git·quilt·Yocto가 패치 큐를 표현하는 방식**
> **선행 노트**: E7(커널 버전·벤더 커널·CIP — 결정표와 rebase 기초), K5(Yocto BSP 레이어·커널 bbappend·devtool), C4·C5(U-Boot 포팅, TF-A/OP-TEE), D5(DTS 파일 구조), G10(펌웨어 로딩), P1(bring-up 순서). 이어지는 노트: P3(데이터시트 → 드라이버), P4(회로도 → DT), **P5(새 LTS로 리베이스)**, P6(테스트·CI), P7(upstream 기여)

---

## 0. 큰 그림 — 이게 왜 필요한가

새 보드 프로젝트가 시작되면 SoC 벤더(NXP, TI, NVIDIA, Rockchip …)의 웹사이트에서 **BSP(Board Support Package)** 를 받는다. BSP는 "이 SoC로 Linux를 띄우는 데 필요한 모든 것"을 벤더가 묶어 준 꾸러미다. 소스 저장소 여러 개, 바이너리 블롭 여러 개, Yocto 레이어, 그리고 수백 쪽짜리 문서가 들어 있다.

Don에게 익숙한 말로 바꾸면 이렇다. SSD 컨트롤러를 새로 받으면 컨트롤러 벤더가 **ROM 코드 문서 + 부트로더 + HAL/드라이버 SDK + NAND 벤더 바이너리 라이브러리 + 릴리스 노트 + errata**를 준다. 우리는 그 위에 자사 FTL·기능 패치를 얹고, 벤더가 새 SDK를 낼 때마다 우리 패치를 옮긴다. Linux BSP도 구조가 똑같다. 다른 점은 **조각 하나하나가 오픈소스 프로젝트의 벤더 포크**라는 것이다. 그래서 "벤더 포크를 쓸 것인가, 원본(upstream)을 쓸 것인가"라는 선택지가 생긴다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20" y="22" font-size="13" text-anchor="start" font-weight="bold">벤더 BSP = SoC 벤더가 주는 꾸러미 (예: NXP i.MX Linux BSP)</text><rect x="20" y="36" width="470" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="255" y="60.2" font-size="12" text-anchor="middle">우리 것: meta-&lt;company&gt; — 보드 DT · 커널/U-Boot 패치 · 설정 · 이미지</text><rect x="20" y="84" width="470" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="255" y="108.2" font-size="12" text-anchor="middle">Yocto BSP 레이어 — meta-imx · meta-ti · meta-tegra · meta-rockchip</text><rect x="20" y="132" width="470" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="255" y="156.2" font-size="12" text-anchor="middle">유저스페이스 블롭 — GPU·NPU 라이브러리, VPU 코덱, ISP 튜닝 (바이너리)</text><rect x="20" y="180" width="470" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="255" y="204.2" font-size="12" text-anchor="middle">펌웨어 블롭 — /lib/firmware: SDMA · VPU · DSP · Wi-Fi/BT · DDR 트레이닝</text><rect x="20" y="228" width="470" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="255" y="252.2" font-size="12" text-anchor="middle">벤더 커널 트리 — LTS 태그 + 벤더 커밋 수천 개 + DT + defconfig</text><rect x="20" y="276" width="470" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="255" y="300.2" font-size="12" text-anchor="middle">부트 체인 포크 — 이미지 패커 · TF-A · OP-TEE · U-Boot (벤더 브랜치)</text><rect x="20" y="324" width="470" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="255" y="348.2" font-size="12" text-anchor="middle">Boot ROM — SoC 안에 구워져 있음 (못 고친다, 문서만 있다)</text><rect x="510" y="36" width="150" height="184" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="585" y="92.2" font-size="12" text-anchor="middle" font-weight="bold">문서</text><text x="585" y="108.2" font-size="12" text-anchor="middle">릴리스 노트</text><text x="585" y="124.2" font-size="12" text-anchor="middle">Known issues</text><text x="585" y="140.2" font-size="12" text-anchor="middle">errata (SoC 버그)</text><text x="585" y="156.2" font-size="12" text-anchor="middle">User/Porting Guide</text><text x="585" y="172.2" font-size="12" text-anchor="middle">보안 공지</text><rect x="510" y="236" width="150" height="128" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="585" y="280.2" font-size="12" text-anchor="middle" font-weight="bold">도구</text><text x="585" y="296.2" font-size="12" text-anchor="middle">플래셔 (uuu 등)</text><text x="585" y="312.2" font-size="12" text-anchor="middle">이미지 서명 툴</text><text x="585" y="328.2" font-size="12" text-anchor="middle">EVK 회로도</text><text x="255" y="410" font-size="12" text-anchor="middle">아래쪽일수록 SoC에 가깝고 우리가 고치기 어렵다 · 위 첫 줄만 온전히 우리 것</text></svg>
```
그림 1 — 벤더 BSP의 구성. 아래쪽(Boot ROM, 부트 체인)일수록 SoC에 가깝고 손대기 어렵다. 맨 위 빨간 줄(`meta-<company>`)만 온전히 우리 것이고, 나머지는 "버전을 고정해서 가져다 쓰는 남의 코드"다. 오른쪽 문서들이 실제 디버깅의 절반을 차지한다.

이 노트의 질문은 세 가지다.

1. 벤더 BSP에는 정확히 무엇이 들어 있고, 각각을 누가 고치나? (1절)
2. 커널은 벤더 것을 쓸까, mainline LTS를 쓸까? (2절)
3. 어느 쪽이든 우리가 얹는 **패치 스택**을 어떻게 관리하나? (3~9절 — 실습의 대부분)

새 LTS로 스택을 옮기는 **리베이스 작업 자체는 P5**에서 다룬다. 이 노트는 "리베이스하기 쉬운 스택을 처음부터 어떻게 만드나"에 집중한다.

### 이 노트의 실습 환경

- Mac의 Lima VM `elinux` (aarch64, `uname -r` = `7.0.0-38-generic`, git 2.53.0). 작업 디렉터리는 VM의 `/var/tmp/P2/`, 스크립트는 Mac의 `~/.elinux_scratch/P2/`.
- 커널 소스는 랩의 `/var/lab/src/linux-6.18.55` (읽기 전용). 그 **일부**(drivers/iio, include/linux/iio, arch/arm64/boot/dts/freescale, arch/arm64/configs, 최상위 Makefile)만 복사해 작은 git 저장소를 만들고 `v6.18.55` 태그를 붙여 "벤더 패치 스택의 기준점"으로 쓴다. 전체 트리 복사는 디스크가 아깝고, 패치 관리 실습에는 이 정도면 충분하다.
- quilt는 VM 호스트에 설치돼 있지 않다(공용 상태를 바꾸지 않기 위해 apt 설치 금지). 대신 GNU Savannah의 공식 tarball `quilt-0.69.tar.gz`(550 KB)를 받아 `/var/tmp/P2/quilt`에 `--prefix`로 설치해 썼다. (랩의 `rootfs-dom` 게스트에는 quilt가 들어 있다.)
- 드라이버 동작 확인은 랩 커널 `out/Image`(6.18.55) + BusyBox initramfs를 QEMU `virt`(TCG, KVM 없음)로 부팅해서 했다. 19명이 같은 VM을 쓰는 중이라 부팅 시간 같은 숫자는 실제 하드웨어 값이 아니다.
- 이 노트의 보드 "P2 SensorKit", 회사 레이어 `meta-p2-sensorkit`, 그리고 "upstream 커밋"으로 등장하는 `sim/mainline-next` 브랜치는 **실습용으로 만든 가상의 것**이다. 실제 커널 커밋이 아니다.

---

## 1. 벤더 BSP에는 무엇이 들어 있나

### 1.1 구성품 한 표로

| 구성품 | 무엇인가 | i.MX 예 (이름은 기억 기반) | 우리가 고치나 |
|---|---|---|---|
| Boot ROM 문서 | SoC 안 마스크 ROM이 어떤 매체에서 무엇을 읽는지 (C2) | Reference Manual의 "System Boot" 장 | 못 고친다. 읽기만 |
| 이미지 패커 + DDR 펌웨어 | 부트 이미지 헤더 생성, DDR PHY 트레이닝 바이너리 | `imx-mkimage`, `firmware-imx`의 `lpddr4_pmu_train_*.bin` | 보드 DDR 설정만 |
| TF-A 포크 | EL3 펌웨어 BL31, PSCI (C5) | `imx-atf` | 거의 안 고친다 |
| OP-TEE 포크 | Secure world OS (C5, L5) | `imx-optee-os` | 보안 기능 쓸 때만 |
| U-Boot 포크 | 부트로더 (C3, C4) | `u-boot-imx` | 보드 파일·DT·env |
| 벤더 커널 | LTS 태그 + 벤더 커밋 수천 개 | `linux-imx` 브랜치 `lf-6.12.y` (아래 1.5에서 확인) | 보드 DT, 드라이버 수정 |
| DT | SoC dtsi + EVK dts (D5) | `arch/arm64/boot/dts/freescale/` | 보드 dts 새로 씀 (P4) |
| 펌웨어 블롭 | 커널 드라이버가 `request_firmware()`로 올리는 바이너리 (G10) | SDMA, VPU, DSP, Wi-Fi/BT | 못 고친다. 버전만 고정 |
| 유저스페이스 블롭 | GPU/NPU/VPU 유저 라이브러리, ISP 튜닝 | `imx-gpu-viv`, NPU용 런타임 | 못 고친다. 커널과 짝 맞춤 |
| Yocto 레이어 | 위 전부를 빌드하는 레시피 (K4, K5) | `meta-imx`(NXP), `meta-freescale`(커뮤니티) | bbappend로 덮는다 |
| 문서 | 릴리스 노트, Known issues, errata, User's Guide, Porting Guide | "i.MX Linux Release Notes" 등 | 읽고 대조 |
| 도구 | 플래셔, 서명 툴, EVK 회로도 | `uuu`, CST(서명) | 그대로 쓴다 |

표의 i.MX 저장소·파일 이름은 **기억 기반**이다. 실제 이름과 버전은 받은 BSP의 릴리스 노트에서 확인해야 한다. 이 노트 안에서 실제로 확인한 것은 1.4절(커널 트리의 펌웨어 참조)과 1.5절(NXP 커널 저장소의 디렉터리)뿐이다.

TI(AM62 등)는 "Processor SDK Linux", NVIDIA Jetson은 "Jetson Linux(L4T)" BSP tarball + 샘플 rootfs, Rockchip은 커뮤니티 Yocto 레이어와 벤더 SDK 저장소 묶음 형태로 비슷한 구성을 준다 (기억 기반 — 각 벤더 문서로 확인 필요). 벤더별 지형은 O5에서 다룬다.

### 1.2 부트 체인은 "포크 네 개"다

부트 체인은 C1~C5에서 다뤘다. BSP 관점에서 기억할 점은 하나다. **TF-A, OP-TEE, U-Boot, 커널이 모두 upstream 프로젝트의 벤더 포크**이고, 서로 버전이 맞물려 있다. 예를 들어 벤더 U-Boot의 특정 커밋은 특정 TF-A 버전의 BL31 주소·SMC 인터페이스를 가정하고, 커널의 전원 도메인 드라이버는 그 TF-A가 제공하는 SiP 서비스를 부른다. 그래서 **부트 체인의 한 조각만 upstream으로 바꾸면 다른 조각이 깨질 수 있다.** 실무에서는 부트 체인은 벤더 릴리스 세트를 통째로 고정하고, 커널만 독립적으로 판단하는 경우가 많다.

### 1.3 벤더 커널 트리와 DT

벤더 커널은 "LTS 태그 + 벤더 커밋 N개"다 (E7 4절). N은 수백에서 수천이다. 그 안에는 세 종류가 섞여 있다.

- **이미 upstream에 있는 것의 backport** — 새 LTS로 가면 저절로 사라진다.
- **upstream에 보낼 예정이거나 보내는 중인 것** — 시간이 지나면 사라진다.
- **upstream에 갈 계획이 없는 것** — 벤더 전용 드라이버(GPU 커널 모듈, 벤더 V4L2 드라이버), EVK용 DT 변형, 디버그 기능. 이것이 벤더 커널을 "벤더 커널"로 만든다.

DT도 마찬가지다. 벤더 트리의 EVK dts는 mainline의 같은 파일보다 길다. 2.3절 예제에서 실제로 잰다.

### 1.4 예제 1 — 커널 트리에서 펌웨어 블롭 찾기

무엇을 확인하나: i.MX8M Plus 보드가 어떤 펌웨어 파일을 `/lib/firmware`에 가져야 하는지, **mainline 6.18.55 트리만 읽고** 알아낸다. 드라이버의 `MODULE_FIRMWARE()`와 DT의 `firmware-name` 속성이 단서다. 그리고 GPU/NPU용 오픈 드라이버가 트리에 있는지도 본다 (1.5절과 연결).

```sh
#!/bin/bash
# P2 ex1: which firmware blobs does an i.MX8M Plus board need? (read-only scan of the 6.18.55 tree)
set -euo pipefail
cd /var/lab/src/linux-6.18.55
echo "== MODULE_FIRMWARE in i.MX-related drivers =="
grep -rhoE 'MODULE_FIRMWARE\("[^"]+"\)' \
  drivers/dma/imx-sdma.c drivers/remoteproc/imx_rproc.c \
  drivers/media/platform/nxp 2>/dev/null \
  | sort -u | head -12
echo "== firmware-name / fsl,sdma-ram-script-name in freescale imx8mp DTs =="
grep -rhoE '(firmware-name|fsl,sdma-ram-script-name) = "[^"]+"' \
  arch/arm64/boot/dts/freescale/imx8mp*.dts* | sort | uniq -c | sort -rn | head -8
echo "== upstream (open) GPU/NPU drivers in this tree =="
for d in drivers/gpu/drm/etnaviv drivers/gpu/drm/panfrost drivers/gpu/drm/imagination \
         drivers/accel/rocket drivers/accel/ivpu; do
  printf '%-30s %3s .c files\n' "$d" "$(ls $d/*.c | wc -l)"
done
```

```text
== MODULE_FIRMWARE in i.MX-related drivers ==
MODULE_FIRMWARE("imx/sdma/sdma-imx6q.bin")
MODULE_FIRMWARE("imx/sdma/sdma-imx7d.bin")
== firmware-name / fsl,sdma-ram-script-name in freescale imx8mp DTs ==
      3 fsl,sdma-ram-script-name = "imx/sdma/sdma-imx7d.bin"
      1 firmware-name = "imx/easrc/easrc-imx8mn.bin"
      1 firmware-name = "imx/dsp/hifi4.bin"
== upstream (open) GPU/NPU drivers in this tree ==
drivers/gpu/drm/etnaviv         15 .c files
drivers/gpu/drm/panfrost        10 .c files
drivers/gpu/drm/imagination     27 .c files
drivers/accel/rocket             5 .c files
drivers/accel/ivpu              18 .c files
```

출력에서 볼 것:

- SDMA(Smart DMA) 엔진의 RAM 스크립트 `imx/sdma/sdma-imx7d.bin`은 i.MX8MP DT 세 곳이 가리킨다. 이 파일이 없으면 SDMA는 ROM 스크립트만으로 동작하고, 일부 주변장치(오디오 등)의 DMA가 안 된다. **커널은 오픈소스지만 그 커널이 요구하는 파일은 블롭**이다. BSP의 `firmware-imx` 같은 패키지가 이것을 채운다.
- `firmware-name`은 DT가 "이 보드에서는 이 파일을 써라"라고 지정하는 표준 속성이다. 보드 DT(P4)를 쓸 때 이 이름이 rootfs의 실제 파일과 맞는지 확인해야 한다. 안 맞으면 부팅 로그에 `Direct firmware load for ... failed with error -2`가 찍힌다 (G10).
- `drivers/gpu/drm/etnaviv`(Vivante GPU의 오픈 드라이버)와 `drivers/accel/rocket`(Rockchip NPU), `drivers/accel/ivpu`(Intel NPU)가 mainline에 있다. 즉 일부 SoC는 **벤더 블롭 없이도** GPU/NPU를 쓸 길이 있다. 다만 오픈 스택이 벤더 스택과 같은 성능·기능(예: 특정 NN 연산자)을 내는지는 별개 문제다 (O4).

### 1.5 GPU/NPU 유저스페이스 블롭과 "ABI 락인"

벤더 GPU/NPU 스택은 보통 두 조각이다. **커널 모듈**(소스 공개, GPL)과 **유저스페이스 라이브러리**(바이너리만 제공). 둘 사이의 인터페이스는 벤더 전용 ioctl이고, 버전이 정확히 짝이 맞아야 한다. i.MX의 Vivante GPU라면 커널 쪽이 `galcore` 모듈, 유저 쪽이 GPU 라이브러리 패키지다 (이름은 기억 기반).

무엇을 확인하나: NXP가 GitHub에 공개한 벤더 커널(`nxp-imx/linux-imx`)의 `lf-6.12.y` 브랜치가 어떤 LTS 위에 있고, mainline에는 없는 벤더 전용 디렉터리 `drivers/mxc`에 무엇이 있는지 본다. (GitHub API로 디렉터리 목록만 읽는다. 전체 클론은 하지 않는다.)

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/P2/vendor &&
  curl -s https://raw.githubusercontent.com/nxp-imx/linux-imx/lf-6.12.y/Makefile | sed -n 2,5p;
  curl -s "https://api.github.com/repos/nxp-imx/linux-imx/contents/drivers/mxc?ref=lf-6.12.y" | grep "\"name\"" | head -20;
  curl -s "https://api.github.com/repos/nxp-imx/linux-imx/commits/lf-6.12.y" | grep -m3 "\"date\"\|\"sha\""'
```

```text
VERSION = 6
PATCHLEVEL = 12
SUBLEVEL = 49
EXTRAVERSION =
    "name": "Kconfig",
    "name": "Makefile",
    "name": "gpu-viv",
    "name": "hantro",
    "name": "hantro_845",
    "name": "hantro_845_h1",
    "name": "hantro_v4l2",
    "name": "hantro_vc8000e",
    "name": "hdmi-cec",
    "name": "ipu3",
    "name": "mipi",
    "name": "mlb",
    "name": "sim",
    "name": "vpu",
  "sha": "df24f9428e38740256a410b983003a478e72a7c0",
      "date": "2025-11-21T03:25:16Z"
      "date": "2025-11-21T03:24:46Z"
```

출력에서 볼 것:

- 2026-10-18에 조회한 `lf-6.12.y`의 머리는 **6.12.49** 위에 있고, 마지막 커밋은 2025-11-21이다. 6.12.y stable은 그 뒤로도 거의 매주 나온다(E7). 지금 몇 point release 차이인지는 kernel.org `releases.json`으로 확인한다 (이 노트에서는 세지 않았다). 즉 **벤더 브랜치가 stable을 얼마나 자주 따라가는지**가 보안 수정의 실제 속도를 정한다.
- `drivers/mxc/gpu-viv`가 Vivante GPU의 벤더 커널 드라이버(`galcore`)다. mainline에는 이 디렉터리가 없다. `hantro*`, `vpu`도 벤더판 비디오 코덱 드라이버다 (mainline에는 별도의 V4L2 기반 hantro 드라이버가 `drivers/media/platform/verisilicon`에 있다).
- 이 GPU 유저 라이브러리를 쓰려면 `gpu-viv`와 **같은 릴리스**의 커널 모듈이 필요하다. 커널을 mainline LTS로 바꾸려면 (a) 이 모듈을 새 커널로 포팅하거나 (b) 오픈 스택(etnaviv + Mesa)으로 갈아타야 한다. 이것이 **블롭 ABI 락인**이다. 2절 결정의 가장 큰 변수다.

함정: "커널 모듈 소스가 공개돼 있으니 아무 커널에나 빌드하면 된다"가 아니다. 모듈이 쓰는 커널 내부 API(dma-buf, DRM, 메모리 관리)는 버전마다 바뀌고 (E4 "커널 내부 API는 안정적이지 않다"), 유저 라이브러리가 기대하는 ioctl 버전도 고정이다. 포팅은 가능하지만 공짜가 아니다.

### 1.6 Yocto 레이어 — 벤더판과 커뮤니티판

| 레이어 | 누가 | 무엇 (기억 기반 — 각 레이어 README로 확인) |
|---|---|---|
| `meta-freescale` | 커뮤니티 (OpenEmbedded) | i.MX 기본 BSP. `linux-fslc`(mainline 계열), `linux-fslc-imx`(NXP 커널 + stable) 등 선택 가능 (K5 2.1) |
| `meta-imx` | NXP | NXP 공식 릴리스 레이어. `meta-freescale` 위에 NXP 전용 패키지(GPU/NPU/ISP 등)와 배포 설정을 얹는다 |
| `meta-ti` (`meta-ti-bsp`) | TI | AM6x/Sitara BSP. TI 커널(`linux-ti-staging`)과 mainline 계열 레시피 |
| `meta-tegra` | 커뮤니티 (OE4T) | NVIDIA Jetson L4T 블롭과 NVIDIA 커널을 Yocto로 묶는다 |
| `meta-rockchip` | Yocto Project 커뮤니티 | Rockchip 보드, 주로 mainline 계열 커널 |

K5가 이 레이어들 위에 회사 레이어를 얹는 법(우선순위, bbappend, kas lock)을 이미 다뤘다. 이 노트의 추가 포인트는 하나다. **같은 SoC에 "벤더판"과 "커뮤니티판" 레이어가 공존한다.** 커뮤니티판은 mainline 커널 선택지와 최신 Yocto 릴리스를 빨리 지원하고, 벤더판은 블롭 패키지와 벤더가 검증한 조합을 준다. 어느 쪽을 바닥에 깔지가 사실상 2절의 커널 결정과 같은 결정이다.

### 1.7 문서 — 릴리스 노트와 errata를 읽는 법

BSP 문서는 길지만 bring-up에서 실제로 펼치는 곳은 정해져 있다.

1. **릴리스 노트의 "구성품 버전표"** — 커널 태그, U-Boot·TF-A·OP-TEE 커밋, 펌웨어 패키지 버전, Yocto 릴리스. 우리 `meta-<company>`의 고정값(SRCREV, kas lock)이 이 표와 일치해야 한다.
2. **Known issues / Limitations** — "이 BSP에서 PCIe Gen3 링크가 X 조건에서 안 올라간다" 같은 항목. bring-up 중 이상 현상을 보면 **디버깅 전에 여기부터** 검색한다. Don이 SSD에서 컨트롤러 errata 시트를 먼저 보던 것과 같다.
3. **SoC errata (Chip Errata 문서)** — 실리콘 버그와 SW 우회법. 커널에는 `ERRATUM_` 같은 Kconfig나 드라이버 quirk로 들어 있는 경우가 많다 (예: 6.18.55 `arch/arm64/Kconfig`에는 Arm 코어 errata 우회용 `config ARM64_ERRATUM_...` 항목이 39개 있다).
4. **Porting Guide** — EVK에서 커스텀 보드로 옮길 때 바꿔야 하는 목록 (DDR 설정, 부트 매체, PMIC, DT). P1 bring-up 순서와 거의 같은 목차다.

---

## 2. 벤더 커널 vs mainline — 결정 매트릭스

### 2.1 직관

E7 3절에서 "벤더 6.6 BSP냐 6.18 LTS냐"를 네 축(upstream 지원, backport 부담, 보안 수정, 수명)으로 봤다. 여기서는 BSP를 실제로 받아 본 사람의 시선으로 축을 두 개 더한다. **블롭 ABI 락인**(1.5절)과 **부트 체인 결합**(1.2절)이다. 이 둘은 "기술적으로 mainline이 더 좋다"는 판단을 뒤집는 경우가 많다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="220" y="10" width="240" height="36" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="340" y="32.5" font-size="13" text-anchor="middle">새 제품: SoC + 요구 기능 + 수명</text><line x1="340" y1="46" x2="340" y2="64" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/><rect x="170" y="64" width="340" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="340" y="88.5" font-size="13" text-anchor="middle">핵심 블록(ISP·NPU·GPU·코덱)이 mainline에?</text><line x1="250" y1="104" x2="150" y2="140" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/><text x="170" y="128" font-size="12" text-anchor="end">아니오</text><line x1="430" y1="104" x2="530" y2="140" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/><text x="500" y="128" font-size="12" text-anchor="start">예</text><rect x="20" y="140" width="260" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="150" y="164.6" font-size="13" text-anchor="middle">그 블록의 유저 블롭이 커널에 묶였나?</text><rect x="400" y="140" width="260" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="530" y="164.6" font-size="13" text-anchor="middle">제품 수명 &gt; LTS 수명 (약 2~4년)?</text><line x1="90" y1="180" x2="90" y2="226" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/><text x="84" y="206" font-size="12" text-anchor="end">예</text><line x1="220" y1="180" x2="250" y2="226" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/><text x="244" y="206" font-size="12" text-anchor="start">아니오</text><line x1="470" y1="180" x2="450" y2="226" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/><text x="452" y="206" font-size="12" text-anchor="end">예</text><line x1="600" y1="180" x2="600" y2="226" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/><text x="606" y="206" font-size="12" text-anchor="start">아니오</text><rect x="10" y="226" width="170" height="64" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="95" y="246.2" font-size="12" text-anchor="middle">벤더 커널</text><text x="95" y="262.2" font-size="12" text-anchor="middle">+ stable 머지 약속</text><text x="95" y="278.2" font-size="12" text-anchor="middle">+ upstream 트랙 병행</text><rect x="190" y="226" width="180" height="64" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="280" y="246.2" font-size="12" text-anchor="middle">최신 LTS</text><text x="280" y="262.2" font-size="12" text-anchor="middle">+ 벤더 out-of-tree 모듈</text><text x="280" y="278.2" font-size="12" text-anchor="middle">(블롭 ABI 확인)</text><rect x="380" y="226" width="140" height="64" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="450" y="254.2" font-size="12" text-anchor="middle">CIP SLTS 계열</text><text x="450" y="270.2" font-size="12" text-anchor="middle">LTS (10년 수정)</text><rect x="530" y="226" width="140" height="64" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="600" y="254.2" font-size="12" text-anchor="middle">최신 LTS 직접</text><text x="600" y="270.2" font-size="12" text-anchor="middle">매주 stable 머지</text><text x="340" y="320" font-size="12" text-anchor="middle">어느 길이든 공통: 우리 패치는 upstream 스타일로 쓰고, 스택을 작게 유지한다</text><text x="340" y="342" font-size="12" text-anchor="middle">"블롭이 커널에 묶임" = 커널 모듈 버전·ABI가 유저 라이브러리 버전과 짝이어야 함</text><text x="340" y="364" font-size="12" text-anchor="middle">(예: Vivante galcore 모듈 ↔ GPU 유저 라이브러리) — 이러면 커널 업그레이드가 벤더 일정에 묶인다</text></svg>
```
그림 2 — 커널 선택 흐름. 첫 질문은 "필요한 블록이 mainline에 있나"이고, 없다면 다음 질문은 "그 블록의 유저 블롭이 벤더 커널 모듈과 묶여 있나"다. 묶여 있으면 벤더 커널이 사실상 강제된다.

### 2.2 결정 매트릭스

| 판단 축 | 벤더 커널 쪽이 유리한 경우 | mainline LTS 쪽이 유리한 경우 | 확인 방법 |
|---|---|---|---|
| 기능 (드라이버) | ISP·NPU·GPU·코덱·전원 관리가 벤더 트리에만 있음 | 필요한 블록이 전부 `drivers/`에 있음 | `git grep` compatible (P3), 벤더의 upstream 지원표 |
| 블롭 ABI 락인 | 유저 라이브러리가 벤더 커널 모듈 버전에 묶임 | 블롭이 없거나, 오픈 스택(etnaviv, rocket 등)으로 충분 | 1.5절처럼 벤더 전용 디렉터리·모듈 확인 |
| 보안 수정 | 벤더가 stable을 매주~매월 머지한다는 근거가 있음 | 벤더 브랜치가 stable보다 수십 point release 뒤처짐 | 벤더 브랜치의 SUBLEVEL과 마지막 커밋 날짜 (1.5절 예제) |
| 수명 | 벤더가 BSP를 제품 수명 동안 지원한다고 계약 | 제품 수명이 길고 CIP SLTS 같은 10년 경로가 필요 | 벤더 롱텀 지원 정책, CIP SLTS 목록 (E7 2.4) |
| upstream 드라이버 품질 | mainline 드라이버가 아직 기능 부족 | mainline 드라이버가 성숙, 커뮤니티 리뷰를 받음 | mainline 드라이버의 최근 커밋, 메일링 리스트 |
| 부트 체인 결합 | 커널이 벤더 TF-A의 SiP 서비스에 의존 | upstream TF-A/U-Boot도 그 SoC를 지원 | 커널 드라이버의 `arm_smccc` 호출, TF-A 플랫폼 목록 |
| 우리 패치의 upstream 가능성 | (불리) 벤더 기준 패치는 mainline에 그대로 못 보냄 | 패치를 거의 그대로 보낼 수 있음 (P7) | 2.3절 같은 diff |
| 디버깅 지원 | 벤더 FAE 지원은 벤더 커널에서만 받음 | 메일링 리스트·커뮤니티 도움 | 계약 조건 |

**CIP SLTS**(Civil Infrastructure Platform의 Super Long Term Support)는 특정 LTS를 10년 정도 유지하는 프로젝트다. 산업·인프라 장비처럼 10년 지원이 필요한 제품이라면 "CIP가 고른 LTS"에 맞추는 것이 유리하다. 어떤 버전이 SLTS인지와 각 EOL은 E7 2.4절과 CIP 위키에서 확인한다 (이 노트에서는 다시 조사하지 않았다).

### 2.3 예제 2 — 벤더 DT와 mainline DT를 같은 기준 버전에서 비교

무엇을 확인하나: 1.5절의 NXP `lf-6.12.y`(6.12.49 기반)와 **같은 기준 버전**인 mainline stable `v6.12.49`에서 `imx8mp-evk.dts` 한 파일을 받아 비교한다. 서로 다른 기준 버전끼리 비교하면 "벤더가 추가한 것"과 "upstream이 그사이 바꾼 것"이 섞인다. 벤더 커밋은 sha로 고정한다 (재현성, 9절).

```sh
#!/bin/bash
# P2 ex12: one board file, vendor (NXP lf-6.12.y, pinned sha) vs mainline stable at the same base (v6.12.49)
set -euo pipefail
V=df24f9428e38740256a410b983003a478e72a7c0     # nxp-imx/linux-imx lf-6.12.y head on 2025-11-21
W=/var/tmp/P2/vendor; mkdir -p "$W/nxp" "$W/mainline"; cd "$W"
F=arch/arm64/boot/dts/freescale/imx8mp-evk.dts
curl -fsSL -o nxp/evk.dts "https://raw.githubusercontent.com/nxp-imx/linux-imx/$V/$F"
curl -fsSL -o mainline/evk.dts \
  "https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/plain/$F?h=v6.12.49"
curl -fsSL "https://raw.githubusercontent.com/nxp-imx/linux-imx/$V/Makefile" | sed -n 3,4p | tr '\n' ' '; echo
wc -l mainline/evk.dts nxp/evk.dts | head -2
git diff --no-index --shortstat mainline/evk.dts nxp/evk.dts || true
echo "--- compatibles only in the vendor file (top 10):"
comm -13 <(grep -o 'compatible = "[^"]*"' mainline/evk.dts | sort -u) \
         <(grep -o 'compatible = "[^"]*"' nxp/evk.dts | sort -u) | head -10
echo "--- do those bindings exist upstream (6.18.55 Documentation)?"
for c in $(comm -13 <(grep -o 'compatible = "[^"]*"' mainline/evk.dts | sort -u) \
              <(grep -o 'compatible = "[^"]*"' nxp/evk.dts | sort -u) | cut -d'"' -f2 | head -10); do
  n=$(grep -rl "$c" /var/lab/src/linux-6.18.55/Documentation/devicetree/bindings 2>/dev/null | wc -l || true)
  printf '%-32s %s\n' "$c" "$([ "$n" -gt 0 ] && echo upstream-binding || echo VENDOR-ONLY)"
done
```

```text
PATCHLEVEL = 12 SUBLEVEL = 49 
 1145 mainline/evk.dts
 1461 nxp/evk.dts
 1 file changed, 478 insertions(+), 162 deletions(-)
--- compatibles only in the vendor file (top 10):
compatible = "fsl,imx-audio-wm8960"
compatible = "ite,it6263"
compatible = "nxp,88w8997-bt"
compatible = "nxp,cbtl04gp"
compatible = "nxp,ptn5110"
compatible = "ovti,ov5640"
compatible = "rohm,dh2228fv"
compatible = "usb-c-connector"
--- do those bindings exist upstream (6.18.55 Documentation)?
fsl,imx-audio-wm8960             upstream-binding
ite,it6263                       upstream-binding
nxp,88w8997-bt                   upstream-binding
nxp,cbtl04gp                     VENDOR-ONLY
nxp,ptn5110                      upstream-binding
ovti,ov5640                      upstream-binding
rohm,dh2228fv                    upstream-binding
usb-c-connector                  upstream-binding
```

출력에서 볼 것:

- **같은 6.12.49 기준**인데도 EVK DT 한 파일이 1145줄 → 1461줄, +478/−162줄 다르다. 벤더 EVK DT에는 카메라(`ovti,ov5640`), USB-C(`nxp,ptn5110` Type-C 컨트롤러, `usb-c-connector`), 블루투스, LVDS 브리지(`ite,it6263`)가 켜져 있다. mainline EVK DT는 그 일부만 기술한다.
- 벤더에만 있는 compatible 8개 중 7개는 **6.18.55 mainline에 바인딩이 이미 있다**. 즉 "벤더 DT의 대부분은 upstream에서도 표현 가능하다". 이것은 "우리 보드 DT를 mainline 스타일로 써 두면 나중에 갈아탈 수 있다"는 근거다.
- `nxp,cbtl04gp`(USB-C 신호 mux로 보인다)는 6.18.55 바인딩 문서에서 찾지 못했다. 이런 노드가 바로 **mainline으로 옮길 때 드라이버·바인딩을 직접 챙겨야 하는 항목**이다. (이 검사는 바인딩 문서에 문자열이 있는지만 본 것이라, 드라이버가 다른 compatible로 같은 칩을 지원할 가능성은 따로 확인해야 한다.)
- 커뮤니티 레이어가 쓰는 `linux-fslc-imx`가 이 NXP 트리와 또 다르다는 것은 K5 3.5절에서 봤다 (패치가 hunk 실패). 비교의 기준은 항상 **실제로 빌드할 SRCREV**다.

### 2.4 실제 벤더 커널 전체를 mainline과 비교하는 법 (가이드 — 실행 안 함)

수 GB의 클론이 필요하므로 이 VM에서는 하지 않았다. 순서와 명령만 정리한다. 명령은 모두 실제 존재하는 git 기능이다.

```sh
# 0) mainline stable 저장소에 벤더 저장소를 remote로 추가 (같은 객체 DB를 공유해 디스크 절약)
git clone --bare https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git linux.git
cd linux.git
git remote add nxp https://github.com/nxp-imx/linux-imx.git
git fetch nxp lf-6.12.y --no-tags

# 1) 벤더 브랜치가 어느 stable 위에 있나 (기준점)
git merge-base nxp/lf-6.12.y v6.12.49        # v6.12.49의 sha와 같으면 깔끔한 기준
git describe --tags --abbrev=0 nxp/lf-6.12.y

# 2) 벤더 커밋 수와 디렉터리별 분포
git rev-list --count --no-merges v6.12.49..nxp/lf-6.12.y
git diff --dirstat=files,2 v6.12.49 nxp/lf-6.12.y

# 3) 벤더 커밋 중 이미 mainline(더 새 버전)에 있는 것 = patch-id가 같은 것
git cherry -v v6.18 nxp/lf-6.12.y | grep -c '^-'   # '-' = upstream에 같은 변경 존재
git log --oneline --no-merges --cherry-pick --right-only v6.18...nxp/lf-6.12.y -- drivers/media/

# 4) 벤더 전용 경로 (mainline에 없는 디렉터리)
git diff --name-only --diff-filter=A v6.12.49 nxp/lf-6.12.y | cut -d/ -f1-2 | sort | uniq -c | sort -rn | head
```

해석 요령:

- 2)의 `--dirstat`에서 `drivers/mxc`, `drivers/gpu`, `drivers/media`, `arch/arm64/boot/dts`가 크게 나오면 "블롭 의존 드라이버 + 멀티미디어 + EVK DT"가 벤더 차이의 본체다.
- 3)의 `git cherry`는 **patch-id**(diff 내용의 해시)로 비교한다 (E7 4.3). 벤더가 upstream 커밋을 **수정해서** backport했다면 patch-id가 달라서 `+`로 나온다. 즉 이 숫자는 "upstream에 있는 것"의 **하한**이다.
- 우리 회사가 실제로 쓰는 것은 그중 일부다. 우리 보드 DT가 참조하는 compatible과 우리 `.config`의 `CONFIG_` 심볼을 기준으로 "우리가 의존하는 벤더 전용 코드"를 좁히는 것이 실무적이다. 이 목록이 P5(리베이스)의 작업 범위가 된다.

---

## 3. 패치 스택 — 개념과 규칙

### 3.1 직관

벤더 커널을 쓰든 mainline을 쓰든, 우리 커널은 **"기준 태그 + 우리 패치 N개"** 다. 이 N개를 **패치 스택**(patch stack, 또는 patch queue)이라 부른다. 스택의 품질은 하나의 질문으로 판가름 난다: **"새 기준 태그로 옮길 때 얼마나 아픈가?"** (P5)

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20" y="22" font-size="13" text-anchor="start" font-weight="bold">우리 커널 = upstream 태그 + 순서가 정해진 패치 스택</text><rect x="20" y="36" width="320" height="34" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="30" y="57.2" font-size="12" text-anchor="start">0006  HACK — 디버그용, 릴리스 SRC_URI에서 제외</text><rect x="350" y="36" width="230" height="34" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="465" y="57.2" font-size="12" text-anchor="middle">Inappropriate [debug]</text><rect x="20" y="76" width="320" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="30" y="97.2" font-size="12" text-anchor="start">0005  설정 조각 p2_sensorkit.config</text><rect x="350" y="76" width="230" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="465" y="97.2" font-size="12" text-anchor="middle">Inappropriate [configuration]</text><rect x="20" y="116" width="320" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="30" y="137.2" font-size="12" text-anchor="start">0004  보드 DT imx8mp-p2-sensorkit.dts</text><rect x="350" y="116" width="230" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="465" y="137.2" font-size="12" text-anchor="middle">Inappropriate [board-specific]</text><rect x="20" y="156" width="320" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="30" y="177.2" font-size="12" text-anchor="start">0003  드라이버 수정 — bus glue에서 chip info</text><rect x="350" y="156" width="230" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="465" y="177.2" font-size="12" text-anchor="middle">Pending → Submitted</text><rect x="20" y="196" width="320" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="30" y="217.2" font-size="12" text-anchor="start">0002  드라이버 수정 — vs/vdd-io 전원 켜기</text><rect x="350" y="196" width="230" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="465" y="217.2" font-size="12" text-anchor="middle">Pending → Submitted</text><rect x="20" y="236" width="320" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="30" y="257.2" font-size="12" text-anchor="start">0001  backport — upstream 커밋 cherry-pick -x</text><rect x="350" y="236" width="230" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="465" y="257.2" font-size="12" text-anchor="middle">Backport [sha]</text><rect x="20" y="276" width="320" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="30" y="297.2" font-size="12" text-anchor="start">v6.18.55  (upstream stable 태그 = 기준점)</text><rect x="350" y="276" width="230" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="465" y="297.2" font-size="12" text-anchor="middle">—</text><line x1="605" y1="300" x2="605" y2="44" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><text x="615" y="60" font-size="12" text-anchor="start">오래</text><text x="615" y="76" font-size="12" text-anchor="start">들고</text><text x="615" y="92" font-size="12" text-anchor="start">간다</text><text x="615" y="262" font-size="12" text-anchor="start">곧</text><text x="615" y="278" font-size="12" text-anchor="start">사라</text><text x="615" y="294" font-size="12" text-anchor="start">진다</text><text x="20" y="330" font-size="12" text-anchor="start">원칙: upstream으로 갈 것은 아래, 보드 전용은 위, HACK은 맨 위 — 리베이스(P5) 때 아래부터 흡수된다</text><text x="20" y="350" font-size="12" text-anchor="start">번호는 "자리"일 뿐 ID가 아니다: 중간에 하나 끼우면 뒤가 전부 다시 번호 매겨진다 (7절)</text></svg>
```
그림 3 — 이 노트의 실습에서 만드는 6개짜리 스택. upstream으로 갈 것(파랑)은 아래, 보드 전용(초록)은 위, HACK(빨강)은 맨 위에 둔다. 리베이스할 때 아래쪽 패치는 upstream에 흡수되어 하나씩 사라진다.

### 3.2 규칙 다섯 개

1. **한 패치 = 한 논리적 변경.** "DT 추가 + 드라이버 수정 + 설정"을 한 커밋에 넣지 않는다. 나중에 드라이버 수정만 upstream에 보내거나 버려야 할 때 쪼갤 수 없다. 커널 커뮤니티 규칙(E7 5절)과 같다.
2. **순서는 "upstream으로 갈 가능성" 순.** backport → 보낼 수정 → 보드 전용 → 설정 → HACK. 리베이스에서 아래쪽이 사라져도 위쪽이 의존하지 않아야 한다.
3. **모든 패치에 출처·상태 표시.** 제목 접두어(`UPSTREAM:`, `BACKPORT:`, `FROMLIST:`, `HACK:` — E7 4.4) 또는 Yocto의 `Upstream-Status:` 태그(3.3절). 이 노트는 Yocto로 내보낼 것이므로 `Upstream-Status`를 커밋 메시지 본문에 넣는다.
4. **backport는 원본 sha를 남긴다.** `git cherry-pick -x`가 `(cherry picked from commit <sha>)`를 붙인다. 리베이스 때 그 sha가 새 기준에 있으면 버린다.
5. **HACK은 스택 맨 위, 릴리스에서 빠지는 경로에.** 디버그 printk, 타이밍 우회, 임시 비활성화는 따로 모아 빌드 설정 하나로 넣고 뺄 수 있게 한다 (5.3절).

### 3.3 `Upstream-Status` 태그

OpenEmbedded는 레이어에 들어가는 모든 패치의 헤더에 `Upstream-Status:` 줄을 요구한다. 없으면 QA 검사(`patch-status`)가 경고 또는 에러를 낸다 (K5 3.4). 값의 형식은 다음과 같다 (OE 문서 기준, 기억 기반 — 사용하는 Yocto 릴리스의 문서로 확인).

| 값 | 뜻 | 이 노트의 예 |
|---|---|---|
| `Pending` | 아직 upstream에 안 보냄 (보낼 예정) | 0002, 0003 드라이버 수정 |
| `Submitted [where]` | 보냈다. 메일링 리스트 링크(lore) 등 | 0002를 보내면 이 값으로 바꾼다 |
| `Backport [version or commit]` | upstream에 이미 있는 것을 가져옴 | 0001 |
| `Denied [reason]` | upstream이 거절했다 | — |
| `Inactive-Upstream [lastcommit/lastrelease]` | upstream 프로젝트가 죽었다 | — |
| `Inappropriate [reason]` | upstream에 보낼 성격이 아님 | 0004 보드 DT, 0005 설정, 0006 HACK |

이 태그는 **"이 패치는 언젠가 사라지나, 영원히 들고 가나"** 를 한 줄로 기록한다. 8절에서 이 태그와 `git cherry`로 스택을 감사(audit)한다.

### 3.4 번호와 이름

`git format-patch`는 `0001-<제목을-하이픈으로>.patch` 형식으로 파일을 만든다. 번호는 **스택 안의 자리**일 뿐 ID가 아니다. 중간에 패치를 하나 끼우면 뒤의 번호가 전부 바뀐다 (7절에서 실제로 일어난다). 그래서 실무에서는 다음 중 하나를 쓴다.

- 레시피의 `SRC_URI`에 **파일 이름 전체**를 쓰고, 번호가 바뀌면 같이 고친다 (작은 스택).
- 번호를 띄워 둔다: 범주별로 `0100-`(backport), `0200-`(fix), `0500-`(board), `0900-`(debug). `git format-patch --start-number`로 시작 번호를 정할 수 있다.
- 범주별 하위 디렉터리를 쓴다 (`files/backport/`, `files/board/`, `files/debug/`). 이 노트는 `debug/` 하나만 분리한다.

---

## 4. 실습 A — git으로 벤더 패치 스택 만들기

시나리오: 우리 회사 제품 "P2 SensorKit"은 i.MX8M Plus SoM + 캐리어 보드이고, I2C3에 ADXL345 가속도계가 달려 있다. 가속도계 전원(3.3 V)은 GPIO로 켜는 고정 레귤레이터에서 온다. 우리는 커널 6.18.55 위에 패치를 얹는다.

### 4.1 예제 3 — 기준 저장소 만들기

무엇을 확인하나: 랩 소스에서 필요한 부분만 복사해 git 저장소를 만들고 `v6.18.55` 태그를 붙인다. 날짜·작성자를 고정해서 **누가 언제 돌려도 같은 커밋 해시**가 나오게 한다 (9절 재현성의 출발점).

```sh
#!/bin/bash
# P2 ex2: a small "kernel" repo = subset of 6.18.55, tagged v6.18.55 (base of our vendor stack)
set -euo pipefail
S=/var/lab/src/linux-6.18.55; R=/var/tmp/P2/linux-subset
rm -rf "$R"; mkdir -p "$R"; cd "$S"
cp --parents -a Makefile drivers/iio include/linux/iio \
   arch/arm64/boot/dts/freescale arch/arm64/configs "$R"/
cd "$R"
git init -q -b master
git config user.name "P2 Lab"; git config user.email "p2@lab.invalid"
export GIT_AUTHOR_DATE="2026-10-01T00:00:00Z"
export GIT_COMMITTER_DATE="$GIT_AUTHOR_DATE"
git add -A
git commit -q -m "Linux 6.18.55 (subset: iio, freescale dts, arm64 configs)"
git tag -a v6.18.55 -m "Linux 6.18.55 subset"
git gc -q; du -sh --exclude=.git . ; git count-objects -vH | grep size-pack
git ls-files | wc -l
git log --oneline --decorate
```

```text
18M	.
size-pack: 3.36 MiB
1366
bb21fc9 (HEAD -> master, tag: v6.18.55) Linux 6.18.55 (subset: iio, freescale dts, arm64 configs)
```

출력에서 볼 것: 파일 1366개, 작업 트리 18 MB, 압축된 git 객체 3.36 MiB. 해시 `bb21fc9`는 날짜·작성자·내용이 같으면 몇 번을 돌려도 같다 (이 노트의 모든 실행에서 같았다). 실제 회사에서는 이 자리에 벤더 커널 저장소(또는 linux-stable)의 태그가 온다.

### 4.2 예제 4 — 변경 종류별 토픽 브랜치

무엇을 확인하나: 변경을 **종류별 브랜치**로 나눠 만든다. 각 브랜치는 `v6.18.55`에서 따로 시작한다. 이렇게 하면 "드라이버 수정만 upstream에 보내기", "HACK만 빼고 빌드하기"가 브랜치 선택으로 끝난다. linux-next가 maintainer 트리들을 모으는 방식과 같은 생각이다.

만드는 것:

- `sim/mainline-next` — **가상의 upstream**. 무관한 정리 커밋 X 하나가 있다 (실습용, 실제 커밋 아님).
- `topic/backports` — X를 `cherry-pick -x`로 가져온다 → `Upstream-Status: Backport [sha]`.
- `topic/iio-fixes` — ADXL345 드라이버가 `vs`/`vdd-io` 전원을 켜도록 수정 → `Pending`. 6.18.55의 `adxl345_core.c`에는 레귤레이터 코드가 없고, 바인딩 문서(`adi,adxl345.yaml`)에도 supply 속성이 없다 (`grep regulator`로 확인). 그래서 upstream에 보내려면 바인딩 패치가 따로 필요하다는 메모를 태그에 남긴다.
- `topic/board` — 보드 DT + `Makefile` 한 줄, 그리고 config fragment → `Inappropriate`.
- `topic/debug` — 버퍼 enable마다 로그를 찍는 HACK → `Inappropriate [debug hack]`.

먼저 보드 DT 파일이다 (스크립트가 복사해 넣는다). DT 문법은 D2~D5, 회로도에서 이 노드를 끌어내는 과정은 P4에서 다룬다. 핀 설정은 I2C3·UART2만 넣고 GPIO 핀 설정은 생략한 단순화 버전이다.

```dts
// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/* P2 SensorKit carrier board with i.MX8M Plus SoM (lab example, not a real product) */
/dts-v1/;
#include <dt-bindings/gpio/gpio.h>
#include <dt-bindings/interrupt-controller/irq.h>
#include "imx8mp.dtsi"

/ {
	model = "P2 SensorKit i.MX8MP";
	compatible = "p2lab,sensorkit-imx8mp", "fsl,imx8mp";
	chosen {
		stdout-path = &uart2;
	};
	reg_imu_3v3: regulator-imu-3v3 {
		compatible = "regulator-fixed";
		regulator-name = "IMU_3V3";
		regulator-min-microvolt = <3300000>;
		regulator-max-microvolt = <3300000>;
		gpio = <&gpio4 20 GPIO_ACTIVE_HIGH>;
		enable-active-high;
		startup-delay-us = <2000>;
	};
};

&i2c3 {
	clock-frequency = <400000>;
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_i2c3>;
	status = "okay";
	accel@53 {
		compatible = "adi,adxl345";
		reg = <0x53>;
		vs-supply = <&reg_imu_3v3>;
		vdd-io-supply = <&reg_imu_3v3>;
		interrupt-parent = <&gpio1>;
		interrupts = <5 IRQ_TYPE_EDGE_RISING>;
		interrupt-names = "INT1";
	};
};

&uart2 { pinctrl-names = "default"; pinctrl-0 = <&pinctrl_uart2>; status = "okay"; };

&iomuxc {
	pinctrl_i2c3: i2c3grp {
		fsl,pins = <MX8MP_IOMUXC_I2C3_SCL__I2C3_SCL 0x400001c2
			    MX8MP_IOMUXC_I2C3_SDA__I2C3_SDA 0x400001c2>;
	};
	pinctrl_uart2: uart2grp {
		fsl,pins = <MX8MP_IOMUXC_UART2_RXD__UART2_DCE_RX 0x140
			    MX8MP_IOMUXC_UART2_TXD__UART2_DCE_TX 0x140>;
	};
};
```

(지면 때문에 실제 파일보다 줄을 합쳐 보였다. 실제 파일은 68줄이고 내용은 같다. 핀 값 `0x400001c2`, `0x140`은 6.18.55의 `imx8mp-evk.dts`에서 그대로 가져왔다.)

이제 토픽 브랜치를 만드는 스크립트다. 커밋은 `-s`(Signed-off-by)를 붙인다. OE 레이어 패치와 커널 패치 모두 Signed-off-by를 요구한다.

```sh
#!/bin/bash
# P2 ex3: one topic branch per kind of change, all based on v6.18.55
set -euo pipefail
R=/var/tmp/P2/linux-subset; F=/Users/donh/.elinux_scratch/P2/files
cd "$R"; export GIT_AUTHOR_DATE="2026-10-02T09:00:00Z"
export GIT_COMMITTER_DATE="$GIT_AUTHOR_DATE"
C=drivers/iio/accel/adxl345_core.c; D=arch/arm64/boot/dts/freescale

# (0) simulated "future mainline": an unrelated upstream cleanup X
git switch -q -c sim/mainline-next v6.18.55
sed -i 's|/\* Reset interrupts at start up \*/|/* Disable all interrupts at start up */|' $C
git -c user.name="Upstream Dev" -c user.email=up@lab.invalid commit -s -qam "iio: accel: adxl345: clarify interrupt reset comment" \
  -m "Simulated upstream commit for the P2 lab (not a real kernel commit)."
X=$(git rev-parse HEAD)

# (1) topic/backports: take X into our tree, keep its sha (-x)
git switch -q -c topic/backports v6.18.55
git cherry-pick -x -s "$X" >/dev/null
git commit -q --amend -m "$(git log -1 --format=%B)" -m "Upstream-Status: Backport [$X]"

# (2) topic/iio-fixes: upstreamable driver fix (regulators)
git switch -q -c topic/iio-fixes v6.18.55
python3 - "$C" <<'PY'
import sys; p=sys.argv[1]; s=open(p).read()
s=s.replace('#include <linux/regmap.h>\n','#include <linux/regmap.h>\n#include <linux/regulator/consumer.h>\n',1)
a='\tst = iio_priv(indio_dev);\n\tst->regmap = regmap;\n'
b=('\tret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(adxl345_supplies),\n'
   '\t\t\t\t\t     adxl345_supplies);\n\tif (ret)\n'
   '\t\treturn dev_err_probe(dev, ret, "Failed to enable supplies\\n");\n\n')
s=s.replace(a,b+a,1)
f='/**\n * adxl345_core_probe()'
s=s.replace(f,'static const char * const adxl345_supplies[] = { "vs", "vdd-io" };\n\n'+f,1)
open(p,'w').write(s)
PY
git commit -s -qam "iio: accel: adxl345: enable vs and vdd-io supplies" \
  -m "On boards where the sensor rail is switched by a GPIO regulator the
device is unpowered at probe time and the DEVID read fails with -ENXIO.
Request and enable both supplies before the first register access." \
  -m "Upstream-Status: Pending [needs dt-bindings patch for vs/vdd-io-supply]"

# (3) topic/board: board DT + Makefile entry, config fragment
git switch -q -c topic/board v6.18.55
cp "$F/imx8mp-p2-sensorkit.dts" $D/
sed -i 's|^dtb-$(CONFIG_ARCH_MXC) += imx8mp-evk.dtb$|&\ndtb-$(CONFIG_ARCH_MXC) += imx8mp-p2-sensorkit.dtb|' $D/Makefile
git add -A; git commit -s -qm "arm64: dts: freescale: add P2 SensorKit i.MX8MP board" \
  -m "Upstream-Status: Inappropriate [board-specific, product not public]"
printf '%s\n' "CONFIG_IIO=y" "CONFIG_IIO_BUFFER=y" "# CONFIG_INPUT_ADXL34X is not set" \
  "CONFIG_ADXL345_I2C=m" "CONFIG_REGULATOR_FIXED_VOLTAGE=y" > arch/arm64/configs/p2_sensorkit.config
git add -A; git commit -s -qm "arm64: configs: add p2_sensorkit.config fragment" \
  -m "Use as: make defconfig p2_sensorkit.config" \
  -m "Upstream-Status: Inappropriate [configuration]"

# (4) topic/debug: a hack that must never ship
git switch -q -c topic/debug v6.18.55
sed -i '/^static int iio_buffer_enable(struct iio_buffer \*buffer,/,/^{/ s|^{|{\n\tdev_info(\&indio_dev->dev, "P2 HACK: buffer enable\\n");|' \
  drivers/iio/industrialio-buffer.c
git commit -s -qam "HACK: iio: buffer: log every buffer enable" \
  -m "Debug aid for bring-up; drop before release." \
  -m "Upstream-Status: Inappropriate [debug hack]"
git branch --list 'topic/*' 'sim/*' -v --no-abbrev | cut -c1-90
```

```text
  sim/mainline-next 60605cd9b5625c091ef647e47cb7f742f7a22163 iio: accel: adxl345: clarify 
  topic/backports   31492ca2d077343a161deed5923cccf4f0c157b7 iio: accel: adxl345: clarify 
  topic/board       a90dfe20238fecbb484b7d14448ebb5148e28756 arm64: configs: add p2_sensor
* topic/debug       864902226f6494eced2a369ac9b3d3a36c4c2bf6 HACK: iio: buffer: log every 
  topic/iio-fixes   9979551b13a57225072b8f0e725329c3982cb7b1 iio: accel: adxl345: enable v
```

출력에서 볼 것: 토픽 브랜치 4개와 가상 upstream 1개. 소스 편집을 `sed`/`python` 치환으로 한 것은 스크립트로 재현하기 위해서다. 실무에서는 에디터로 고치고 `git commit`한다. 드라이버 수정의 실제 diff는 이렇다 (`git diff v6.18.55 topic/iio-fixes`, 컨텍스트 일부 생략).

```c
 #include <linux/regmap.h>
+#include <linux/regulator/consumer.h>
 #include <linux/units.h>
...
+static const char * const adxl345_supplies[] = { "vs", "vdd-io" };
+
 /**
  * adxl345_core_probe() - Probe and setup for the accelerometer.
...
 	if (!indio_dev)
 		return -ENOMEM;
 
+	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(adxl345_supplies),
+					     adxl345_supplies);
+	if (ret)
+		return dev_err_probe(dev, ret, "Failed to enable supplies\n");
+
 	st = iio_priv(indio_dev);
```

`devm_regulator_bulk_get_enable()`은 레귤레이터 여러 개를 얻고 켜고, 드라이버가 떨어질 때 자동으로 끄는 devm 헬퍼다 (H3). DT에 supply가 없으면 regulator core가 **dummy regulator**를 돌려줘서 기존 보드도 깨지지 않는다 (7절 QEMU 출력에서 확인).

### 4.3 예제 5 — 선형 통합 브랜치

무엇을 확인하나: 토픽 브랜치들을 **정해진 순서**로 cherry-pick해 하나의 선형 브랜치 `p2/bsp`를 만들고 릴리스 후보 태그를 붙인다. Yocto나 quilt로 내보낼 스택은 선형이어야 한다 (merge 커밋은 패치 파일로 표현이 안 된다).

```sh
#!/bin/bash
# P2 ex4: linear integration branch from topic branches, in a fixed series order
set -euo pipefail
cd /var/tmp/P2/linux-subset
export GIT_COMMITTER_DATE="2026-10-03T09:00:00Z"
git switch -q -C p2/bsp v6.18.55
for t in topic/backports topic/iio-fixes topic/board topic/debug; do
  git cherry-pick v6.18.55.."$t" >/dev/null       # order = upstream-bound first, hacks last
done
git tag -f -a p2-bsp-1.0-rc1 -m "P2 SensorKit BSP 1.0-rc1 on v6.18.55" >/dev/null
git log --oneline --decorate -6
echo "--- stack vs base:"; git diff --stat v6.18.55 HEAD | tail -1
git describe p2/bsp
```

```text
a3ca774 (HEAD -> p2/bsp, tag: p2-bsp-1.0-rc1) HACK: iio: buffer: log every buffer enable
8df625d arm64: configs: add p2_sensorkit.config fragment
aa9c094 arm64: dts: freescale: add P2 SensorKit i.MX8MP board
62eb9a7 iio: accel: adxl345: enable vs and vdd-io supplies
aedce93 iio: accel: adxl345: clarify interrupt reset comment
bb21fc9 (tag: v6.18.55, master) Linux 6.18.55 (subset: iio, freescale dts, arm64 configs)
--- stack vs base:
 5 files changed, 84 insertions(+), 1 deletion(-)
p2-bsp-1.0-rc1
```

출력에서 볼 것: 이것이 Don이 면접에서 그릴 수 있어야 하는 **`git log --oneline` 한 화면**이다. 맨 아래 기준 태그, 그 위로 backport → fix → board → config → HACK. 스택 전체가 5개 파일, +84/−1줄이다. 토픽 브랜치를 고치면 이 스크립트를 다시 돌려 통합 브랜치를 새로 만든다 (통합 브랜치는 "결과물"이지 직접 편집하는 곳이 아니다).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20" y="20" font-size="12" text-anchor="start" font-weight="bold">토픽 브랜치 (각자 v6.18.55 기준)</text><rect x="20" y="30" width="150" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="95" y="51.2" font-size="12" text-anchor="middle">topic/backports</text><line x1="170" y1="47" x2="250" y2="150" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="20" y="78" width="150" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="95" y="99.2" font-size="12" text-anchor="middle">topic/iio-fixes</text><line x1="170" y1="95" x2="250" y2="150" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="20" y="126" width="150" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="95" y="147.2" font-size="12" text-anchor="middle">topic/board</text><line x1="170" y1="143" x2="250" y2="150" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="20" y="174" width="150" height="34" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="95" y="195.2" font-size="12" text-anchor="middle">topic/debug</text><line x1="170" y1="191" x2="250" y2="150" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="250" y="120" width="180" height="64" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="340" y="140.2" font-size="12" text-anchor="middle">p2/bsp (선형 통합)</text><text x="340" y="156.2" font-size="12" text-anchor="middle">순서: backport → fix</text><text x="340" y="172.2" font-size="12" text-anchor="middle">→ board → HACK</text><text x="340" y="206" font-size="12" text-anchor="middle">tag p2-bsp-1.0-rc1</text><line x1="430" y1="152" x2="490" y2="66" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="490" y="40" width="180" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="580" y="62.2" font-size="12" text-anchor="middle">git format-patch</text><text x="580" y="78.2" font-size="12" text-anchor="middle">→ meta-p2/…/files/</text><line x1="430" y1="152" x2="490" y2="140" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="490" y="114" width="180" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="580" y="136.2" font-size="12" text-anchor="middle">quilt series</text><text x="580" y="152.2" font-size="12" text-anchor="middle">(git 없는 트리, 패치 큐)</text><line x1="430" y1="152" x2="490" y2="214" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="490" y="188" width="180" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="580" y="210.2" font-size="12" text-anchor="middle">git cherry · range-diff</text><text x="580" y="226.2" font-size="12" text-anchor="middle">(무엇이 upstream에?)</text><text x="670" y="270" font-size="12" text-anchor="end">format-patch 결과 = Yocto do_patch의 입력</text><text x="20" y="300" font-size="12" text-anchor="start">진실의 원천(source of truth)은 하나로: 여기서는 git 브랜치</text><text x="20" y="318" font-size="12" text-anchor="start">quilt로 고쳤으면 git quiltimport로 되돌려 넣는다 (6.4절)</text></svg>
```
그림 4 — 작업 흐름. 사람은 토픽 브랜치에서 일하고, 통합 브랜치는 스크립트가 만든다. 통합 브랜치에서 Yocto 패치 디렉터리, quilt 시리즈, upstream 비교 결과가 나온다.

### 4.4 예제 6 — 스택이 빌드되는지 확인 (DTB + 모듈)

무엇을 확인하나: 패치가 "적용된다"와 "빌드된다"는 다르다. 보드 DT를 `cpp → dtc`로 컴파일하고 (D2, K5 3.4와 같은 2단계), 수정한 ADXL345 드라이버를 랩 커널 빌드 트리에 대해 out-of-tree 모듈로 `W=1` 빌드한다.

```sh
#!/bin/bash
# P2 ex5: does the stack still build? DTB (cpp + dtc) and the patched driver as a module
set -euo pipefail
S=/var/lab/src/linux-6.18.55; R=/var/tmp/P2/linux-subset; W=/var/tmp/P2/build
rm -rf "$W"; mkdir -p "$W/mod"; cd "$R"; git switch -q p2/bsp
D=arch/arm64/boot/dts/freescale
cpp -nostdinc -undef -D__DTS__ -x assembler-with-cpp -I "$S/include" -I "$R/$D" \
    "$D/imx8mp-p2-sensorkit.dts" -o "$W/p2.dts.pre"
dtc -q -I dts -O dtb -o "$W/p2.dtb" "$W/p2.dts.pre"
ls -l "$W/p2.dtb" | awk '{print $5, $9}'
fdtget "$W/p2.dtb" / model
dtc -q -I dtb -O dts "$W/p2.dtb" | grep -A7 'accel@53'
# driver: patched adxl345 core + i2c glue, built out-of-tree against the lab kernel
cp drivers/iio/accel/adxl345_core.c drivers/iio/accel/adxl345_i2c.c drivers/iio/accel/adxl345_spi.c \
   drivers/iio/accel/adxl345.h "$W/mod/"
echo 'obj-m := adxl345_core.o adxl345_i2c.o adxl345_spi.o' > "$W/mod/Kbuild"
make -s -C /var/lab/out/linux-build M="$W/mod" W=1 modules 2>&1 | grep -v "^  " || true
ls "$W"/mod/*.ko
modinfo -F depends "$W/mod/adxl345_core.ko"
```

```text
42724 /var/tmp/P2/build/p2.dtb
P2 SensorKit i.MX8MP
				accel@53 {
					compatible = "adi,adxl345";
					reg = <0x53>;
					vs-supply = <0x2e>;
					vdd-io-supply = <0x2e>;
					interrupt-parent = <0x2f>;
					interrupts = <0x05 0x01>;
					interrupt-names = "INT1";
/var/tmp/P2/build/mod/adxl345_core.ko
/var/tmp/P2/build/mod/adxl345_i2c.ko
/var/tmp/P2/build/mod/adxl345_spi.ko
kfifo_buf
```

출력에서 볼 것: DTB 42 KB, `&reg_imu_3v3` 참조가 phandle `0x2e`로, `IRQ_TYPE_EDGE_RISING`이 `0x01`로 바뀌었다. 모듈 세 개가 **경고 없이**(`W=1`에서도 출력 없음) 빌드됐고, `adxl345_core.ko`의 의존 모듈은 `kfifo_buf`뿐이다 (레귤레이터 API는 커널에 내장). 서브셋 저장소에는 `include/dt-bindings`가 없어서 `-I "$S/include"`로 랩 소스의 헤더를 빌려 썼다. 이 DT는 `dt-validate`로 바인딩 검사(D7)를 하면 `vs-supply`가 바인딩에 없다고 지적받을 것이다 — 그것이 0002의 `Pending [needs dt-bindings patch ...]` 메모의 이유다 (이 VM에 dt-schema가 없어 실행하지 않았다).

---

## 5. 실습 B — Yocto 패치 디렉터리로 내보내기

### 5.1 예제 7 — `git format-patch`로 레이어에 넣기

무엇을 확인하나: 통합 브랜치를 패치 파일로 내보내 `meta-p2-sensorkit/recipes-kernel/linux/files/`에 넣는다. HACK은 `debug/` 하위 디렉터리로 분리하고, config fragment는 Yocto 관례대로 `.cfg` 파일로도 꺼내 둔다. `--zero-commit --no-signature`의 이유는 9절에서 측정한다.

```sh
#!/bin/bash
# P2 ex6: export the stack as a Yocto-style patch dir (stable output: no commit sha, no git version)
set -euo pipefail
cd /var/tmp/P2/linux-subset
L=/var/tmp/P2/meta-p2-sensorkit; P=$L/recipes-kernel/linux/files
rm -rf "$L"; mkdir -p "$P/debug"
git format-patch -q --zero-commit --no-signature -o "$P" v6.18.55..p2/bsp
mv "$P"/*HACK*.patch "$P/debug/"            # debug patches live apart, never in release SRC_URI
git show p2/bsp:arch/arm64/configs/p2_sensorkit.config > "$P/p2-sensorkit.cfg"  # Yocto-style fragment
cd "$L"; find . -type f | sort
echo "--- header of 0002:"; sed -n '1,14p' "$P"/0002-*.patch
```

```text
./recipes-kernel/linux/files/0001-iio-accel-adxl345-clarify-interrupt-reset-comment.patch
./recipes-kernel/linux/files/0002-iio-accel-adxl345-enable-vs-and-vdd-io-supplies.patch
./recipes-kernel/linux/files/0003-arm64-dts-freescale-add-P2-SensorKit-i.MX8MP-board.patch
./recipes-kernel/linux/files/0004-arm64-configs-add-p2_sensorkit.config-fragment.patch
./recipes-kernel/linux/files/debug/0005-HACK-iio-buffer-log-every-buffer-enable.patch
./recipes-kernel/linux/files/p2-sensorkit.cfg
--- header of 0002:
From 0000000000000000000000000000000000000000 Mon Sep 17 00:00:00 2001
From: P2 Lab <p2@lab.invalid>
Date: Fri, 2 Oct 2026 09:00:00 +0000
Subject: [PATCH 2/5] iio: accel: adxl345: enable vs and vdd-io supplies

On boards where the sensor rail is switched by a GPIO regulator the
device is unpowered at probe time and the DEVID read fails with -ENXIO.
Request and enable both supplies before the first register access.

Upstream-Status: Pending [needs dt-bindings patch for vs/vdd-io-supply]
Signed-off-by: P2 Lab <p2@lab.invalid>
---
 drivers/iio/accel/adxl345_core.c | 8 ++++++++
 1 file changed, 8 insertions(+)
```

출력에서 볼 것:

- 첫 줄 `From 0000000000000000000000000000000000000000` — `--zero-commit` 때문에 커밋 해시 대신 0이 들어갔다. 통합 브랜치를 다시 만들어 해시가 바뀌어도 패치 파일은 바이트 단위로 같다 (9절).
- `Upstream-Status:`가 커밋 메시지 본문에 있으므로 패치 헤더에 그대로 나온다. OE의 QA가 읽는 위치가 바로 여기다.
- 0004(in-tree `p2_sensorkit.config`)와 `p2-sensorkit.cfg`는 **같은 내용의 두 표현**이다. Yocto에서는 보통 `.cfg`만 `SRC_URI`에 넣고 0004는 빼서 중복을 피한다 (`kernel-yocto`가 `.cfg`를 `merge_config.sh`로 합친다 — K5 3.6). 커널 트리 안의 fragment는 Yocto 밖(개발자 PC의 `make defconfig p2_sensorkit.config`)에서 쓰기 위한 것이다.

### 5.2 예제 8 — 패치 디렉터리 감사(audit)

무엇을 확인하나: 레이어의 모든 패치에 `Upstream-Status`가 있는지, 종류별로 몇 개인지, `checkpatch.pl`이 무엇을 지적하는지 한 번에 본다. 리뷰나 CI에서 돌리는 검사의 축소판이다 (P6).

```sh
#!/bin/bash
# P2 ex7: audit a patch dir - every patch needs an Upstream-Status; group by status; checkpatch
set -uo pipefail
P=/var/tmp/P2/meta-p2-sensorkit/recipes-kernel/linux/files
S=/var/lab/src/linux-6.18.55
printf '%-58s %s\n' PATCH STATUS
for f in $(find "$P" -name '*.patch' | sort); do
  st=$(sed -n 's/^Upstream-Status: *//p' "$f" | head -1)
  printf '%-58s %s\n' "$(basename "$f" | cut -c1-56)" "${st:-MISSING}"
done
echo "--- by kind:"
grep -rh '^Upstream-Status:' "$P" | awk '{print $2}' | sort | uniq -c
echo "--- checkpatch (summary line per patch):"
for f in $(find "$P" -name '*.patch' | sort); do
  printf '%s: ' "$(basename "$f" | cut -c1-4)"
  "$S/scripts/checkpatch.pl" --no-tree -q --terse "$f" 2>&1 | tail -1
  [ "${PIPESTATUS[0]}" = 0 ] && echo "clean"
done
```

```text
PATCH                                                      STATUS
0001-iio-accel-adxl345-clarify-interrupt-reset-comment.p   Backport [60605cd9b5625c091ef647e47cb7f742f7a22163]
0002-iio-accel-adxl345-enable-vs-and-vdd-io-supplies.pat   Pending [needs dt-bindings patch for vs/vdd-io-supply]
0003-arm64-dts-freescale-add-P2-SensorKit-i.MX8MP-board.   Inappropriate [board-specific, product not public]
0004-arm64-configs-add-p2_sensorkit.config-fragment.patc   Inappropriate [configuration]
0005-HACK-iio-buffer-log-every-buffer-enable.patch         Inappropriate [debug hack]
--- by kind:
      1 Backport
      3 Inappropriate
      1 Pending
--- checkpatch (summary line per patch):
0001: total: 0 errors, 0 warnings, 8 lines checked
clean
0002: total: 0 errors, 0 warnings, 26 lines checked
clean
0003: total: 0 errors, 1 warnings, 75 lines checked
0004: total: 0 errors, 1 warnings, 5 lines checked
0005: total: 0 errors, 0 warnings, 7 lines checked
clean
```

출력에서 볼 것: 상태 MISSING이 없다. 0003·0004의 경고 1개는 둘 다 `added, moved or deleted file(s), does MAINTAINERS need updating?`(새 파일을 추가하면 나오는 경고)이다 — 보드 전용 패치라 무시해도 된다. **처음 실행 때는 다섯 개 모두 `Missing Signed-off-by` 에러가 났다.** 커밋에 `-s`를 빼먹었기 때문이다. 그때 0001에는 `Duplicate signature` 경고도 있었다 — 가상 upstream 커밋의 작성자와 backport한 사람이 같은 이름이라 `cherry-pick -s`가 같은 줄을 두 번 붙였기 때문이다. 실제 backport에서는 원작자의 Signed-off-by 아래에 backport한 사람의 것이 추가되는 것이 정상이다.

### 5.3 레이어와 `.bbappend` (가이드 — 실행 안 함)

이 VM에서 Yocto 빌드는 하지 않는다 (K3~K5의 무거운 빌드는 가이드로만 다룬다는 원칙). 레이어 구조와 bbappend만 보인다. 문법은 K5 2~3절과 같다.

```text
meta-p2-sensorkit/
  conf/layer.conf                      # BBFILE_PRIORITY above the vendor layer
  conf/machine/p2-sensorkit.conf       # require the SoC include, KERNEL_DEVICETREE, UBOOT_CONFIG
  recipes-kernel/linux/
    linux-imx_%.bbappend               # or linux-fslc-imx_%.bbappend: check which one is selected
    files/0001-...patch ... 0004-...patch
    files/debug/0005-HACK-...patch
    files/p2-sensorkit.cfg
  recipes-bsp/u-boot/u-boot-imx_%.bbappend
  recipes-bsp/firmware-imx/firmware-imx_%.bbappend   # only if we must pin/trim blobs
```

```text
# meta-p2-sensorkit/recipes-kernel/linux/linux-imx_%.bbappend  (guide, not built here)
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# release stack: order here = do_patch order. 0004 (in-tree fragment) is left out on purpose,
# the same options come from p2-sensorkit.cfg
SRC_URI:append:p2-sensorkit = " \
    file://0001-iio-accel-adxl345-clarify-interrupt-reset-comment.patch \
    file://0002-iio-accel-adxl345-enable-vs-and-vdd-io-supplies.patch \
    file://0003-arm64-dts-freescale-add-P2-SensorKit-i.MX8MP-board.patch \
    file://p2-sensorkit.cfg \
"

# debug stack: only when the developer sets P2_DEBUG_PATCHES = "1" in local.conf
SRC_URI:append:p2-sensorkit = "${@' file://debug/0005-HACK-iio-buffer-log-every-buffer-enable.patch' \
    if d.getVar('P2_DEBUG_PATCHES') == '1' else ''}"
```

포인트:

- 레시피 이름(`linux-imx` vs `linux-fslc-imx`)은 `PREFERRED_PROVIDER_virtual/kernel`이 고른다. bbappend를 쓰기 전에 `bitbake-getvar`로 실제 선택을 확인한다 (K5 2.1).
- `SRC_URI`에 나열한 순서가 `do_patch`의 적용 순서다. 파일 이름 번호와 다르게 나열하면 그 순서대로 적용된다 — 번호를 믿지 말고 목록을 믿는다.
- 디버그 패치를 변수 하나로 넣고 빼면, **릴리스 이미지에 HACK이 섞일 위험**이 구조적으로 사라진다. 다음 예제가 그것을 확인한다.
- 이 패치들은 `p2_sensorkit` 이외 MACHINE에는 적용되지 않는다 (`:p2-sensorkit` override). 같은 레이어로 EVK도 빌드하는 회사라면 이것이 중요하다.

### 5.4 예제 9 — `do_patch`가 하는 일을 손으로: 릴리스 트리 = git 스택 − HACK

무엇을 확인하나: 원본 소스(= 레시피가 받는 tarball 또는 SRCREV)를 풀고, 릴리스용 패치(`files/0*.patch`, `debug/` 제외)를 순서대로 `patch -p1`로 적용한다. 그 결과가 git의 `p2/bsp~1`(HACK 바로 아래 커밋)과 **바이트 단위로 같은지** `diff -r`로 확인한다.

```sh
#!/bin/bash
# P2 ex11: what do_patch does with the release SRC_URI list (debug/ excluded), checked against git
set -euo pipefail
P=/var/tmp/P2/meta-p2-sensorkit/recipes-kernel/linux/files
T=/var/tmp/P2/dopatch; rm -rf "$T"; mkdir -p "$T/S" "$T/ref"
G=/var/tmp/P2/linux-subset
git -C $G archive v6.18.55 | tar -x -C "$T/S"            # = unpacked SRC_URI tarball / SRCREV
cd "$T/S"
for p in "$P"/0*.patch; do                                   # SRC_URI order = file order here
  printf 'apply %-60s ' "$(basename "$p" | cut -c1-58)"
  patch -p1 -s --no-backup-if-mismatch < "$p" && echo ok
done
git -C $G archive p2/bsp~1 | tar -x -C "$T/ref"            # git stack minus the HACK
diff -r "$T/ref" "$T/S" && echo "release tree == git p2/bsp~1 (no HACK)"
grep -c 'P2 HACK' drivers/iio/industrialio-buffer.c || echo "HACK absent from release tree"
```

```text
apply 0001-iio-accel-adxl345-clarify-interrupt-reset-comment.pat   ok
apply 0002-iio-accel-adxl345-enable-vs-and-vdd-io-supplies.patch   ok
apply 0003-arm64-dts-freescale-add-P2-SensorKit-i.MX8MP-board.pa   ok
apply 0004-arm64-configs-add-p2_sensorkit.config-fragment.patch    ok
release tree == git p2/bsp~1 (no HACK)
0
HACK absent from release tree
```

출력에서 볼 것: 네 패치가 모두 깨끗이 적용됐고, `diff -r`이 아무 차이도 출력하지 않았다. `grep -c`가 `0`을 찍고 종료 코드 1을 내서 마지막 메시지가 나왔다. 즉 **"패치 디렉터리로 만든 릴리스 트리"와 "git 통합 브랜치에서 HACK만 뺀 트리"가 같다.** 이 검사를 CI에 넣으면 "git과 레이어가 어긋났다"를 바로 잡는다. 실제 Yocto의 `do_patch`는 기본적으로 quilt로 패치를 적용하고, `kernel-yocto` 클래스를 쓰는 커널 레시피는 git으로 패치를 커밋으로 쌓는다 (기억 기반 — `PATCHTOOL` 변수와 K5 3절로 확인). 어느 쪽이든 결과 트리는 같아야 한다.

---

## 6. 실습 C — quilt로 같은 스택 다루기

### 6.1 quilt가 무엇인가

**quilt**는 git이 없던 시절부터 쓰인 "패치 큐 관리 도구"다. 원본 트리 위에 패치 파일들을 **스택처럼** 쌓고(push) 내리며(pop), 맨 위 패치를 편집한 뒤 다시 패치 파일로 저장(refresh)한다. 상태는 세 군데에 있다.

- `patches/series` — 패치 순서 목록 (텍스트 파일 한 개 = 스택 정의)
- `patches/*.patch` — 패치 파일들
- `.pc/` — 적용된 각 패치가 건드리기 전의 원본 파일 사본 (pop할 때 되돌리는 데 쓴다)

왜 아직 배우나: Yocto의 `devshell`, Debian 패키지(`debian/patches/`), 벤더가 tarball + 패치 디렉터리로 주는 BSP 등에서 여전히 만난다. 그리고 **"series 파일 = 스택"** 이라는 모델이 Yocto `SRC_URI`의 패치 목록과 정확히 같다. Don에게 익숙한 말로는 "ROM 패치 테이블": 순서가 정해진 패치 목록을 부팅 때 위에서부터 차례로 적용하는 것이다.

기본 출력 형식은 git 스타일이 아니라서(`Index:` 줄, `quilt-tree.orig/` 경로), 이 노트는 git과 같은 `a/ b/` 형식이 나오도록 설정 파일을 하나 썼다.

```text
# ~/.elinux_scratch/P2/quiltrc  (used with: quilt --quiltrc <file> ...)
QUILT_PATCHES=patches
QUILT_NO_DIFF_INDEX=1
QUILT_NO_DIFF_TIMESTAMPS=1
QUILT_REFRESH_ARGS="-p ab --no-timestamps --no-index"
QUILT_DIFF_ARGS="-p ab --no-timestamps --no-index"
```

함정 하나를 먼저 밝힌다. 처음에는 이 값들을 환경 변수(`export QUILT_REFRESH_ARGS=...`)로 줬는데 효과가 없었다. 설치된 전역 `quilt.quiltrc`가 같은 변수(`QUILT_REFRESH_ARGS="--no-timestamps --backup"`)를 설정하고 있어서 환경 변수를 덮어썼기 때문이다. `--quiltrc`로 파일을 명시하니 해결됐다.

### 6.2 예제 10 — 원본 트리에 시리즈 올리기 (그리고 `quilt import`의 함정)

무엇을 확인하나: git이 없는 원본 트리(`git archive`로 풀어 낸 것)에 5절의 패치 파일을 quilt 시리즈로 올린다. 먼저 순진하게 `quilt import`를 반복해서 써 본다.

```sh
#!/bin/bash
# P2 ex8: the same stack managed with quilt on a pristine (non-git) tree
set -euo pipefail
export PATH=/var/tmp/P2/quilt/bin:$PATH
q() { quilt --quiltrc /Users/donh/.elinux_scratch/P2/quiltrc "$@"; }   # git-style a/ b/ diffs
P=/var/tmp/P2/meta-p2-sensorkit/recipes-kernel/linux/files; Q=/var/tmp/P2/quilt-tree
rm -rf "$Q"; mkdir -p "$Q"; git -C /var/tmp/P2/linux-subset archive v6.18.55 | tar -x -C "$Q"
cd "$Q"
# trap: 'quilt import' inserts AFTER the top applied patch -> with nothing applied, order reverses
for f in "$P"/0*.patch "$P"/debug/0*.patch; do q import "$f" >/dev/null; done
echo "== series after naive import:"; q series | sed 's|patches/||'
# do it right: the series file IS the order. copy + write it explicitly
rm -rf patches .pc; mkdir patches; cp "$P"/0*.patch "$P"/debug/0*.patch patches/
(cd patches && ls 0*.patch > series)
echo "== q series:"; q series | sed 's|patches/||'
q push -a >/dev/null; echo "== top: $(q top)"
```

```text
== series after naive import:
0005-HACK-iio-buffer-log-every-buffer-enable.patch
0004-arm64-configs-add-p2_sensorkit.config-fragment.patch
0003-arm64-dts-freescale-add-P2-SensorKit-i.MX8MP-board.patch
0002-iio-accel-adxl345-enable-vs-and-vdd-io-supplies.patch
0001-iio-accel-adxl345-clarify-interrupt-reset-comment.patch
== q series:
0001-iio-accel-adxl345-clarify-interrupt-reset-comment.patch
0002-iio-accel-adxl345-enable-vs-and-vdd-io-supplies.patch
0003-arm64-dts-freescale-add-P2-SensorKit-i.MX8MP-board.patch
0004-arm64-configs-add-p2_sensorkit.config-fragment.patch
0005-HACK-iio-buffer-log-every-buffer-enable.patch
== top: 0005-HACK-iio-buffer-log-every-buffer-enable.patch
```

출력에서 볼 것: 순진한 `quilt import` 반복은 **순서를 뒤집었다**. `quilt import`는 새 패치를 "현재 맨 위에 적용된 패치 바로 다음"에 끼우는데, 아무것도 적용되지 않은 상태에서는 매번 맨 앞에 들어가기 때문이다. 이대로 `push -a`하면 HACK부터 적용된다. 해결은 단순하다. **series 파일을 직접 쓴다.** 고친 뒤 `push -a`로 다섯 개가 다 올라갔고 맨 위(`top`)는 HACK이다.

### 6.3 예제 11 — quilt의 일상 루프: 중간 패치 고치기, 새 패치 얹기

무엇을 확인하나: (1) 스택 **중간**의 DT 패치(0003)에서 레귤레이터 기동 지연을 2 ms → 5 ms로 바꾼다: 그 패치까지 pop → 파일 수정 → refresh → 다시 push. (2) 맨 위에 새 패치 0006을 만든다: `new` → `add` → 수정 → `refresh` → `header`.

```sh
#!/bin/bash
# P2 ex9: quilt daily loop - fix a patch in the middle, then add a new patch on top
set -euo pipefail
export PATH=/var/tmp/P2/quilt/bin:$PATH
q() { quilt --quiltrc /Users/donh/.elinux_scratch/P2/quiltrc "$@"; }   # git-style a/ b/ diffs
cd /var/tmp/P2/quilt-tree
D=arch/arm64/boot/dts/freescale/imx8mp-p2-sensorkit.dts
DT=0003-arm64-dts-freescale-add-P2-SensorKit-i.MX8MP-board.patch
q pop "$DT"
q files | sed 's/^/  file in 0003: /'
sed -i 's/startup-delay-us = <2000>;/startup-delay-us = <5000>;/' "$D"   # file already in 0003
q refresh
grep -n 'startup-delay' "patches/$DT"
q push -a | tail -1
q new 0006-arm64-dts-p2-sensorkit-name-the-accel-rail.patch
q add "$D"           # MUST come before the edit: quilt snapshots the old file now
sed -i 's/regulator-name = "IMU_3V3";/regulator-name = "VDD_ACCEL_3V3";/' "$D"
q refresh
printf '%s\n' "arm64: dts: p2-sensorkit: name the accel rail after the schematic" "" \
  "Upstream-Status: Inappropriate [board-specific]" | q header -r
echo "== q applied:"; q applied | sed 's|patches/||'
echo "== patches/0006:"; cat patches/0006-*.patch
```

```text
Removing patch 0005-HACK-iio-buffer-log-every-buffer-enable.patch
Restoring drivers/iio/industrialio-buffer.c

Removing patch 0004-arm64-configs-add-p2_sensorkit.config-fragment.patch
Removing arch/arm64/configs/p2_sensorkit.config

Now at patch 0003-arm64-dts-freescale-add-P2-SensorKit-i.MX8MP-board.patch
  file in 0003: arch/arm64/boot/dts/freescale/Makefile
  file in 0003: arch/arm64/boot/dts/freescale/imx8mp-p2-sensorkit.dts
Refreshed patch 0003-arm64-dts-freescale-add-P2-SensorKit-i.MX8MP-board.patch
53:+		startup-delay-us = <5000>;
Now at patch 0005-HACK-iio-buffer-log-every-buffer-enable.patch
Patch 0006-arm64-dts-p2-sensorkit-name-the-accel-rail.patch is now on top
File arch/arm64/boot/dts/freescale/imx8mp-p2-sensorkit.dts added to patch 0006-arm64-dts-p2-sensorkit-name-the-accel-rail.patch
Refreshed patch 0006-arm64-dts-p2-sensorkit-name-the-accel-rail.patch
Replaced header of patch 0006-arm64-dts-p2-sensorkit-name-the-accel-rail.patch
== q applied:
0001-iio-accel-adxl345-clarify-interrupt-reset-comment.patch
0002-iio-accel-adxl345-enable-vs-and-vdd-io-supplies.patch
0003-arm64-dts-freescale-add-P2-SensorKit-i.MX8MP-board.patch
0004-arm64-configs-add-p2_sensorkit.config-fragment.patch
0005-HACK-iio-buffer-log-every-buffer-enable.patch
0006-arm64-dts-p2-sensorkit-name-the-accel-rail.patch
== patches/0006:
arm64: dts: p2-sensorkit: name the accel rail after the schematic

Upstream-Status: Inappropriate [board-specific]
--- a/arch/arm64/boot/dts/freescale/imx8mp-p2-sensorkit.dts
+++ b/arch/arm64/boot/dts/freescale/imx8mp-p2-sensorkit.dts
@@ -19,7 +19,7 @@
 
 	reg_imu_3v3: regulator-imu-3v3 {
 		compatible = "regulator-fixed";
-		regulator-name = "IMU_3V3";
+		regulator-name = "VDD_ACCEL_3V3";
 		regulator-min-microvolt = <3300000>;
 		regulator-max-microvolt = <3300000>;
 		gpio = <&gpio4 20 GPIO_ACTIVE_HIGH>;
```

출력에서 볼 것:

- `pop`은 위에서부터 하나씩 내리며 `.pc/`의 원본으로 파일을 되돌린다 (`Restoring`, 새로 만든 파일은 `Removing`).
- 0003을 고칠 때는 `add`가 필요 없었다. 그 DTS 파일은 이미 0003이 추적하는 파일이다 (`quilt files`). **아직 추적하지 않는 파일을 고칠 때는 반드시 `quilt add`를 먼저** 해야 한다. 그래야 quilt가 수정 전 사본을 `.pc/`에 떠 둔다. 순서를 바꾸면 수정 내용이 패치에 안 들어간다. 이것이 quilt의 가장 흔한 실수다.
- `quilt header -r`은 패치 헤더(설명)를 표준 입력으로 바꾼다. 여기에 `Upstream-Status`를 넣었다.
- 함정: 0003을 refresh하면 quilt가 **diff 본문만** 다시 쓰고 헤더는 그대로 둔다. git이 만들었던 헤더의 diffstat(`2 files changed, 69 insertions(+)` 같은 줄)은 이제 실제 내용과 맞지 않을 수 있다. `quilt refresh --diffstat`는 `diffstat` 프로그램이 필요한데 이 VM에는 없다 (실행해 보니 `sh: 1: diffstat: not found`). git으로 되돌려 다시 내보내면 깔끔해진다 (다음 예제).

### 6.4 예제 12 — quilt → git 되돌리기와 `range-diff`

무엇을 확인하나: quilt로 고친 스택을 git으로 되돌려 넣고(`git quiltimport`), 원래 git 스택과 무엇이 달라졌는지 `git range-diff`로 짝지어 본다. **진실의 원천(source of truth)을 하나로 유지**하는 연습이다.

```sh
#!/bin/bash
# P2 ex10: quilt -> git round trip, then compare the two stacks with range-diff
set -euo pipefail
cd /var/tmp/P2/linux-subset
export GIT_COMMITTER_DATE="2026-10-04T09:00:00Z"
git switch -q -C p2/bsp-quilt v6.18.55
git quiltimport --patches /var/tmp/P2/quilt-tree/patches \
    --author "P2 Lab <p2@lab.invalid>" 2>&1 | sed 's/^/  /'
git log --oneline -7
echo "--- range-diff (git stack -> quilt-edited stack):"
git range-diff --no-color v6.18.55..p2/bsp v6.18.55..p2/bsp-quilt | grep -E '^[0-9-]+: ' | cut -c1-100
```

```text
  0001-iio-accel-adxl345-clarify-interrupt-reset-comment.patch
  0002-iio-accel-adxl345-enable-vs-and-vdd-io-supplies.patch
  0003-arm64-dts-freescale-add-P2-SensorKit-i.MX8MP-board.patch
  0004-arm64-configs-add-p2_sensorkit.config-fragment.patch
  0005-HACK-iio-buffer-log-every-buffer-enable.patch
  0006-arm64-dts-p2-sensorkit-name-the-accel-rail.patch
9a2fca7 0006-arm64-dts-p2-sensorkit-name-the-accel-rail
7de87bc HACK: iio: buffer: log every buffer enable
281f9df arm64: configs: add p2_sensorkit.config fragment
bffc5ef arm64: dts: freescale: add P2 SensorKit i.MX8MP board
43f18f0 iio: accel: adxl345: enable vs and vdd-io supplies
500aa5f iio: accel: adxl345: clarify interrupt reset comment
bb21fc9 Linux 6.18.55 (subset: iio, freescale dts, arm64 configs)
--- range-diff (git stack -> quilt-edited stack):
1:  aedce93 = 1:  500aa5f iio: accel: adxl345: clarify interrupt reset comment
2:  62eb9a7 = 2:  43f18f0 iio: accel: adxl345: enable vs and vdd-io supplies
3:  aa9c094 ! 3:  bffc5ef arm64: dts: freescale: add P2 SensorKit i.MX8MP board
4:  8df625d = 4:  281f9df arm64: configs: add p2_sensorkit.config fragment
5:  a3ca774 = 5:  7de87bc HACK: iio: buffer: log every buffer enable
-:  ------- > 6:  9a2fca7 0006-arm64-dts-p2-sensorkit-name-the-accel-rail
```

출력에서 볼 것:

- `range-diff`가 두 스택의 커밋을 짝지었다. `=`는 내용이 같음, `!`는 바뀜(0003의 5 ms 수정), `>`는 새로 생김(0006). 해시는 다르지만(커밋 시각·작성 경로가 다름) 내용이 같다는 것을 정확히 알아낸다. 벤더 BSP 새 버전을 받았을 때 "이전 BSP 대비 무엇이 바뀌었나"를 볼 때도 같은 도구를 쓴다 (P5).
- 0006의 커밋 제목이 **파일 이름**(`0006-arm64-dts-...`)이 됐다. 0006의 헤더에는 `From:`/`Subject:` 줄이 없어서, `git quiltimport`가 제목을 찾지 못하고 패치 이름을 썼다. 교훈: quilt로 새 패치를 만들 때도 **메일 형식 헤더**(`From:`, `Subject:`, 빈 줄, 본문, `Upstream-Status`, `Signed-off-by`)를 쓰면 git과 오가는 데 문제가 없다. 또는 처음부터 git에서 만들고 quilt는 "읽기·적용용"으로만 쓴다.

---

## 7. 실습 D — bring-up 테스트가 버그를 찾았다: 스택에 수정 끼워 넣기

### 7.1 예제 13 — QEMU에서 우리 드라이버 올려 보기 (수정 전)

무엇을 확인하나: 실제 보드가 아직 없다고 치고, 랩 커널(6.18.55)을 QEMU로 부팅해 우리 패치가 들어간 ADXL345 드라이버를 올려 본다. 칩은 **i2c-stub**으로 흉내 낸다. i2c-stub은 "레지스터 256개짜리 가짜 I2C 칩"을 만드는 커널 모듈로, 실제 드라이버가 probe할 수 있다 (H5). 랩 기본 커널에는 i2c-stub이 없어서 소스(`drivers/i2c/i2c-stub.c`)를 out-of-tree 모듈로 빌드했다. DEVID 레지스터(0x00)에 ADXL345의 ID `0xE5`를 미리 써 두고, sysfs `new_device`로 장치를 만든다. 같은 스크립트를 **수정 전**(스택이 0002까지인 커밋 `62eb9a7`)과 **수정 후**(7.2절 이후의 `p2/bsp`)에 돌린다.

```sh
#!/bin/bash
# P2 ex15: boot the lab kernel, load our adxl345 (before / after the bus-glue fix) on an i2c-stub chip
set -euo pipefail
W=/var/tmp/P2/qemu; G=/var/tmp/P2/linux-subset; A=drivers/iio/accel
rm -rf "$W"; mkdir -p "$W/stub"
cp /var/lab/src/linux-6.18.55/drivers/i2c/i2c-stub.c "$W/stub/"
echo 'obj-m := i2c-stub.o' > "$W/stub/Kbuild"; make -s -C /var/lab/out/linux-build M="$W/stub" modules
cat > "$W/p2.sh" <<'G'
insmod /i2c-stub.ko chip_addr=0x53
for m in kfifo_buf adxl345_core adxl345_i2c; do insmod /$m.ko; done
i2cset -y 0 0x53 0x00 0xe5          # preload DEVID register of the fake chip
echo adxl345 0x53 > /sys/bus/i2c/devices/i2c-0/new_device
dmesg | grep -E 'adxl345|0-0053' | sed 's/^\[ *[0-9.]*\] //'
echo "iio devices: $(ls /sys/bus/iio/devices/ 2>/dev/null | tr '\n' ' ')"
[ -e /sys/bus/iio/devices/iio:device0 ] && echo "name=$(cat /sys/bus/iio/devices/iio:device0/name)" \
  "scale=$(cat /sys/bus/iio/devices/iio:device0/in_accel_scale)"
G
for v in before:62eb9a7 after:p2/bsp; do                  # 62eb9a7 = stack up to the regulator patch
  n=${v%%:*}; rev=${v#*:}; M="$W/$n/mod"; mkdir -p "$M" "$W/$n/r"
  for f in adxl345_core.c adxl345_i2c.c adxl345.h; do git -C $G show "$rev:$A/$f" > "$M/$f"; done
  echo 'obj-m := adxl345_core.o adxl345_i2c.o' > "$M/Kbuild"
  make -s -C /var/lab/out/linux-build M="$M" modules
  cp -a /var/lab/build/rootfs/. "$W/$n/r/"; cp "$W/p2.sh" "$W/stub/i2c-stub.ko" "$M"/*.ko \
     /var/lab/build/linux/drivers/iio/buffer/kfifo_buf.ko "$W/$n/r/"
  (cd "$W/$n/r" && find . | cpio -o -H newc --quiet | gzip) > "$W/$n/i.gz"
  echo "===== $n ($rev)"
  timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
    -kernel /var/lab/out/Image -initrd "$W/$n/i.gz" -append "console=ttyAMA0 quiet LAB_CMD=sh~/p2.sh" \
    2>&1 | tr -d '\r' | grep -vE '^$|^lab initramfs|reboot: Power down'
done
```

수정 전 결과만 먼저 본다 (전체 출력은 7.3절).

```text
===== before (62eb9a7)
adxl345_i2c 0-0053: supply vs not found, using dummy regulator
adxl345_i2c 0-0053: supply vdd-io not found, using dummy regulator
i2c i2c-0: new_device: Instantiated device adxl345 at 0x53
iio devices: 
```

출력에서 볼 것:

- 우리 0002 패치는 잘 동작한다. DT가 없으니 supply가 없고, regulator core가 **dummy regulator**로 대신했다. "supply가 없는 기존 보드도 안 깨진다"를 확인한 셈이다.
- 그런데 IIO 장치가 **안 생겼다**. 에러 메시지도 없다. probe가 조용히 실패했다.

### 7.2 원인 찾기와 수정 (예제 14)

드라이버를 읽으면 이유가 보인다. 6.18.55의 `adxl345_core_probe()`는 칩 정보를 이렇게 얻는다.

```c
	st->info = device_get_match_data(dev);
	if (!st->info)
		return -ENODEV;
```

`device_get_match_data()`는 **펌웨어 노드(DT/ACPI)** 의 매치 테이블만 본다. sysfs `new_device`나 보드 코드로 만든 장치에는 펌웨어 노드가 없으므로 NULL이 나오고, `-ENODEV`로 끝난다. 그리고 driver core는 probe가 `-ENODEV`/`-ENXIO`를 돌려주면 "이 드라이버의 장치가 아니다"로 보고 **에러를 찍지 않는다** (G3). 그래서 조용했다. I2C 쪽에는 이미 답이 있다: `i2c_get_match_data()`는 펌웨어 노드를 먼저 보고, 없으면 I2C id 테이블(`{ "adxl345", ... }`)의 `driver_data`로 돌아간다. SPI에는 `spi_get_device_match_data()`가 있다.

이것은 6.18.55 트리에서 우리가 관찰한 동작이다. 이후 mainline에서 같은 문제가 이미 고쳐졌는지는 확인하지 않았다 — **실무라면 upstream에 보내기 전에 maintainer 트리와 lore에서 먼저 검색한다** (8절, P7). 그래서 수정은 `topic/iio-fixes`에 `Pending`으로 얹는다.

```sh
#!/bin/bash
# P2 ex16: bring-up found a bug -> new fix on topic/iio-fixes (bus glue passes chip info)
set -euo pipefail
cd /var/tmp/P2/linux-subset; A=drivers/iio/accel
export GIT_AUTHOR_DATE="2026-10-06T09:00:00Z"; export GIT_COMMITTER_DATE="$GIT_AUTHOR_DATE"
git switch -q topic/iio-fixes
python3 - "$A" <<'PY'
import sys; a=sys.argv[1]
def sub(f, old, new):
    p=f"{a}/{f}"; s=open(p).read(); assert old in s, (f, old); open(p,'w').write(s.replace(old,new,1))
sub("adxl345.h", "int adxl345_core_probe(struct device *dev, struct regmap *regmap,\n",
    "int adxl345_core_probe(struct device *dev, struct regmap *regmap,\n"
    "\t\t       const struct adxl345_chip_info *info,\n")
sub("adxl345_core.c", "int adxl345_core_probe(struct device *dev, struct regmap *regmap,\n",
    "int adxl345_core_probe(struct device *dev, struct regmap *regmap,\n"
    "\t\t       const struct adxl345_chip_info *info,\n")
sub("adxl345_core.c", " * @regmap:\tRegmap instance for the device\n",
    " * @regmap:\tRegmap instance for the device\n * @info:\tChip info from the bus driver's match data\n")
sub("adxl345_core.c", "\tst->info = device_get_match_data(dev);\n\tif (!st->info)\n\t\treturn -ENODEV;\n",
    "\tst->info = info;\n\tif (!st->info)\n\t\treturn dev_err_probe(dev, -ENODEV, \"No chip info\\n\");\n")
sub("adxl345_i2c.c", "\treturn adxl345_core_probe(&client->dev, regmap, false, NULL);",
    "\treturn adxl345_core_probe(&client->dev, regmap,\n\t\t\t\t  i2c_get_match_data(client), false, NULL);")
for setup in ("adxl345_spi_setup", "NULL"):
    sub("adxl345_spi.c", f"adxl345_core_probe(&spi->dev, regmap, needs_delay, {setup});",
        f"adxl345_core_probe(&spi->dev, regmap,\n\t\t\t\t\t  spi_get_device_match_data(spi),\n"
        f"\t\t\t\t\t  needs_delay, {setup});")
PY
git commit -s -qam "iio: accel: adxl345: take chip info from the bus driver" \
  -m "device_get_match_data() only looks at firmware nodes, so a device
instantiated from the I2C/SPI id table (e.g. via sysfs new_device, or
board code) gets st->info == NULL and probe fails silently with -ENODEV.
Let the I2C/SPI glue pass i2c_get_match_data()/spi_get_device_match_data(),
which fall back to the id table, and make the failure visible." \
  -m "Upstream-Status: Pending"
git show --stat --format='%h %s' HEAD
```

```text
273f6e2 iio: accel: adxl345: take chip info from the bus driver

 drivers/iio/accel/adxl345.h      | 1 +
 drivers/iio/accel/adxl345_core.c | 6 ++++--
 drivers/iio/accel/adxl345_i2c.c  | 3 ++-
 drivers/iio/accel/adxl345_spi.c  | 8 ++++++--
 4 files changed, 13 insertions(+), 5 deletions(-)
```

I2C 쪽 변경은 한 줄이다.

```c
-	return adxl345_core_probe(&client->dev, regmap, false, NULL);
+	return adxl345_core_probe(&client->dev, regmap,
+				  i2c_get_match_data(client), false, NULL);
```

설계 포인트: 수정은 **토픽 브랜치에** 한다. 통합 브랜치(`p2/bsp`)에 직접 커밋하면 다음 통합 때 사라지거나, upstream으로 보낼 패치를 고를 때 섞인다. 또 이 수정은 core 함수의 시그니처를 바꾸므로 I2C·SPI 글루를 **같은 커밋에서** 함께 고쳐야 한다 (중간 커밋에서 빌드가 깨지면 `git bisect`가 망가진다 — 커널 커뮤니티의 "각 패치는 빌드돼야 한다" 규칙).

### 7.3 다시 통합하고, 다시 내보내고, 다시 테스트

4.3절의 `ex4_integrate.sh`를 그대로 다시 돌린다. 통합 브랜치는 결과물이니까 다시 만들면 된다.

```text
c4fe8ec (HEAD -> p2/bsp, tag: p2-bsp-1.0-rc1) HACK: iio: buffer: log every buffer enable
f9ef8e6 arm64: configs: add p2_sensorkit.config fragment
ed3fdbc arm64: dts: freescale: add P2 SensorKit i.MX8MP board
ca48c9c iio: accel: adxl345: take chip info from the bus driver
62eb9a7 iio: accel: adxl345: enable vs and vdd-io supplies
aedce93 iio: accel: adxl345: clarify interrupt reset comment
--- stack vs base:
 8 files changed, 97 insertions(+), 6 deletions(-)
p2-bsp-1.0-rc1
```

새 수정 `ca48c9c`가 fix 구역(0002 위, DT 아래)에 들어갔다. 아래 두 커밋(`aedce93`, `62eb9a7`)은 해시가 그대로다 — 내용·부모·날짜가 같으니까. 그 위는 부모가 바뀌어 해시가 전부 새로 나왔다. 5.1절의 `ex6_export.sh`로 다시 내보내면:

```text
./recipes-kernel/linux/files/0001-iio-accel-adxl345-clarify-interrupt-reset-comment.patch
./recipes-kernel/linux/files/0002-iio-accel-adxl345-enable-vs-and-vdd-io-supplies.patch
./recipes-kernel/linux/files/0003-iio-accel-adxl345-take-chip-info-from-the-bus-driver.patch
./recipes-kernel/linux/files/0004-arm64-dts-freescale-add-P2-SensorKit-i.MX8MP-board.patch
./recipes-kernel/linux/files/0005-arm64-configs-add-p2_sensorkit.config-fragment.patch
./recipes-kernel/linux/files/debug/0006-HACK-iio-buffer-log-every-buffer-enable.patch
./recipes-kernel/linux/files/p2-sensorkit.cfg
```

3.4절에서 말한 **번호 밀림**이 실제로 일어났다. DT 패치가 0003 → 0004, 설정이 0004 → 0005, HACK이 0005 → 0006이 됐다. 5.3절의 bbappend에 파일 이름을 적어 두었다면 그 목록도 고쳐야 한다. 리뷰어는 이 diff를 "파일 세 개 삭제 + 네 개 추가"로 보게 된다. 그래서 큰 스택에서는 번호를 띄워 두거나 범주 디렉터리를 쓴다. 감사 스크립트(`ex7_audit.sh`)를 다시 돌린 결과의 요약 부분:

```text
--- by kind:
      1 Backport
      3 Inappropriate
      2 Pending
--- checkpatch (summary line per patch):
0001: total: 0 errors, 0 warnings, 8 lines checked
clean
0002: total: 0 errors, 0 warnings, 26 lines checked
clean
0003: total: 0 errors, 0 warnings, 56 lines checked
clean
0004: total: 0 errors, 1 warnings, 75 lines checked
0005: total: 0 errors, 1 warnings, 5 lines checked
0006: total: 0 errors, 0 warnings, 7 lines checked
clean
```

빌드 확인(`ex5_build.sh`, 4.4절)도 다시 돌렸고 출력은 4.4절과 같았다 (DTB 42724바이트, 모듈 세 개 경고 없음, 의존 `kfifo_buf`). 마지막으로 QEMU 테스트의 전체 출력이다.

```text
===== before (62eb9a7)
adxl345_i2c 0-0053: supply vs not found, using dummy regulator
adxl345_i2c 0-0053: supply vdd-io not found, using dummy regulator
i2c i2c-0: new_device: Instantiated device adxl345 at 0x53
iio devices: 
===== after (p2/bsp)
adxl345_i2c 0-0053: supply vs not found, using dummy regulator
adxl345_i2c 0-0053: supply vdd-io not found, using dummy regulator
i2c i2c-0: new_device: Instantiated device adxl345 at 0x53
iio devices: iio:device0 
name=adxl345 scale=0.004789
```

출력에서 볼 것: 수정 후에는 `iio:device0`이 생기고 이름이 `adxl345`다. `in_accel_scale` 0.004789는 드라이버가 probe에서 설정한 범위·해상도에 따라 보고하는 스케일이다 (i2c-stub은 가짜 칩이라 측정값 자체는 의미 없다). 같은 랩 커널, 같은 스크립트에서 **패치 하나의 유무로 결과가 갈렸다** — 이것이 "스택의 각 지점을 따로 테스트할 수 있어야 한다"는 이유다 (P6의 CI가 하는 일).

---

## 8. 어떤 패치가 upstream에 갔나 — 스택 추적

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="20" y="40" width="110" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="75" y="64.5" font-size="13" text-anchor="middle">로컬 커밋</text><line x1="130" y1="60" x2="160" y2="60" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="160" y="40" width="110" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="215" y="64.5" font-size="13" text-anchor="middle">Pending</text><line x1="270" y1="60" x2="300" y2="60" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="300" y="40" width="140" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="370" y="64.5" font-size="13" text-anchor="middle">Submitted [lore]</text><line x1="440" y1="60" x2="470" y2="60" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="470" y="40" width="190" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="565" y="64.2" font-size="12" text-anchor="middle">maintainer 트리 → mainline</text><line x1="565" y1="80" x2="565" y2="120" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="470" y="120" width="190" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="565" y="144.2" font-size="12" text-anchor="middle">Backport [sha] 로 교체</text><line x1="565" y1="160" x2="565" y2="200" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="470" y="200" width="190" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="565" y="224.2" font-size="12" text-anchor="middle">리베이스 때 drop (P5)</text><line x1="370" y1="80" x2="370" y2="120" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="290" y="120" width="160" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="370" y="144.2" font-size="12" text-anchor="middle">리뷰 반려 · Denied</text><line x1="370" y1="160" x2="370" y2="200" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="290" y="200" width="160" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="370" y="224.2" font-size="12" text-anchor="middle">재설계 후 v2 또는 carry</text><line x1="75" y1="80" x2="75" y2="120" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="20" y="120" width="260" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="150" y="144.2" font-size="12" text-anchor="middle">Inappropriate [board/config/debug]</text><line x1="145" y1="160" x2="145" y2="200" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><rect x="20" y="200" width="260" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="150" y="224.2" font-size="12" text-anchor="middle">계속 carry — BSP 올릴 때마다 재검토</text><text x="340" y="22" font-size="12" text-anchor="middle" font-weight="bold">패치 하나의 일생 — Upstream-Status 태그가 지금 어느 칸인지 기록한다</text><text x="340" y="280" font-size="12" text-anchor="middle">목표: 스택에서 Pending/Submitted 칸을 비우는 것 (= 다음 리베이스가 쉬워진다)</text></svg>
```
그림 5 — 패치 하나의 일생. `Upstream-Status`는 이 그림에서 지금 어느 칸에 있는지를 기록한다. 스택 관리의 목표는 파란 칸(Pending/Submitted)에 머무는 패치를 줄이는 것이다.

### 8.1 예제 15 — `git cherry`로 "이미 upstream에 있는 것" 찾기

무엇을 확인하나: 시간이 흘러 maintainer가 우리 드라이버 수정 두 개(0002, 0003)를 **그대로** 받아들였다고 가정한다. 가상 upstream `sim/mainline-next`에 그 두 커밋을 넣고, 우리 스택과 비교한다. `git cherry`는 patch-id(내용 해시)로 비교하므로 커밋 해시·작성 날짜가 달라도 같은 변경을 알아본다.

```sh
#!/bin/bash
# P2 ex13: which of our patches are already upstream? (simulate: mainline-next merges our fix)
set -euo pipefail
cd /var/tmp/P2/linux-subset
export GIT_COMMITTER_DATE="2026-10-10T09:00:00Z"
git switch -q sim/mainline-next
git cherry-pick v6.18.55..topic/iio-fixes >/dev/null   # "maintainer applied our 2 fixes as-is"
echo "--- git cherry -v <upstream> <ours>   (- = already upstream, + = still ours)"
git cherry -v sim/mainline-next p2/bsp | cut -c1-80
echo "--- count still carried:"; git cherry sim/mainline-next p2/bsp | grep -c '^+'
```

```text
--- git cherry -v <upstream> <ours>   (- = already upstream, + = still ours)
- aedce93ddbdd0649d24958067279b6459e948255 iio: accel: adxl345: clarify interrup
- 62eb9a7cc9db66790d7dc6d2c0f0133a112b91f5 iio: accel: adxl345: enable vs and vd
- ca48c9cd6b6e926b61baf5169a65755fe4489c9e iio: accel: adxl345: take chip info f
+ ed3fdbc2cce62e1266bc62af24b1e8ecec9650eb arm64: dts: freescale: add P2 SensorK
+ f9ef8e64b1a8949aa94b7f8d79a7b36c503b6698 arm64: configs: add p2_sensorkit.conf
+ c4fe8ec28cb22a0331f35b0ca7a7240ae13fc7ca HACK: iio: buffer: log every buffer e
--- count still carried:
3
```

출력에서 볼 것: 아래 세 개(backport 1 + 수정 2)가 `-`, 즉 **새 기준 위로 옮길 때 버릴 패치**다. 위 세 개(DT, 설정, HACK)가 `+`, 계속 들고 갈 패치다. 이것이 그림 3의 "아래부터 사라진다"를 숫자로 본 것이다. 실제 리베이스(P5)에서 `git rebase`도 같은 patch-id 비교로 이런 커밋을 자동으로 건너뛴다 (E7 4.3의 `dropping ... patch contents already upstream`).

주의: maintainer가 패치를 **고쳐서**(공백, 변수 이름, 커밋 메시지 말고 코드) 받으면 patch-id가 달라져 `+`로 남는다. 그때는 `git range-diff`나 사람의 눈으로 "같은 의미"인지 판단하고, 우리 패치를 버린 뒤 upstream 커밋을 `Backport`로 다시 가져온다.

### 8.2 추적표 — 스택의 "부채 장부"

`Upstream-Status` 태그는 패치 파일 안에 있어서 한눈에 보기 어렵다. 팀에서는 보통 표 하나를 같이 관리한다 (스프레드시트, 이슈 트래커, 또는 레이어 안의 `PATCHES.md`). 예를 들어 이 노트의 스택이라면:

| 패치 | 상태 | 담당 | upstream 링크·다음 행동 | 기준 BSP에서 제거 예정 |
|---|---|---|---|---|
| clarify interrupt reset comment | Backport [60605cd…] | — | 다음 리베이스에서 drop | v6.18.56+ (가상) |
| enable vs and vdd-io supplies | Pending | Don | 바인딩 패치와 2개 시리즈로 iio 리스트에 전송 | 머지된 LTS |
| take chip info from the bus driver | Pending | Don | 먼저 iio 트리·lore 검색, 없으면 전송 | 머지된 LTS |
| P2 SensorKit board DT | Inappropriate | BSP 팀 | 제품 공개 후 upstream 검토 가능 | — |
| p2_sensorkit.config | Inappropriate | BSP 팀 | Yocto에서는 .cfg만 사용 | — |
| HACK buffer enable log | Inappropriate [debug] | Don | bring-up 끝나면 삭제 | 1.0 릴리스 전 |

이 표의 열 "제거 예정"이 핵심이다. 패치마다 "언제 사라지나"가 없으면 스택은 계속 자란다. Don이 SSD에서 벤더 SDK 위 로컬 패치 목록에 "벤더 반영 요청 번호"를 달아 관리하던 것과 같은 장부다.

---

## 9. 고객 보드 레이어와 재현성

### 9.1 `meta-<company>` — 벤더 위에 얹는 우리 레이어

K5 1~2절이 레이어 스택과 kas lock을 다뤘다. BSP 관점의 규칙만 정리한다.

1. **벤더 레이어는 수정하지 않는다.** 바꿀 것은 전부 우리 레이어의 bbappend·machine conf·패치로. 벤더 레이어를 포크해서 고치면 다음 BSP 릴리스 때 머지 지옥이 된다.
2. **보드 하나 = MACHINE 하나.** `p2-sensorkit.conf`가 SoC include를 `require`하고 `KERNEL_DEVICETREE`, U-Boot 설정, 시리얼 콘솔을 정한다. EVK와 우리 보드가 같은 레이어 스택으로 빌드되게 해 두면 "EVK에서는 되는데 우리 보드에서는 안 된다"를 바로 비교할 수 있다 (P1 bring-up의 기본 전술).
3. **BSP 레이어와 제품 레이어를 나눈다.** `meta-p2-bsp`(보드: 커널·U-Boot·펌웨어)와 `meta-p2-product`(앱·이미지·배포 설정). 보드가 같은 다른 제품이 BSP 레이어를 재사용한다.
4. **벤더 블롭 버전도 고정 대상이다.** 펌웨어·GPU 라이브러리 패키지의 버전은 벤더 레이어의 레시피 버전이 정한다. 레이어 커밋을 고정하면 함께 고정된다. 블롭을 갈아 끼울 일이 있으면 그것도 우리 레이어의 bbappend로.

### 9.2 예제 16 — 패치 내보내기를 바이트 단위로 재현 가능하게

무엇을 확인하나: 통합 브랜치를 "내일 다시 만들면"(cherry-pick 시각이 달라 해시가 바뀜) 레이어의 패치 파일이 바뀌는가? 기본 `format-patch`와 `--zero-commit --no-signature`를 비교한다.

```sh
#!/bin/bash
# P2 ex14: is the exported patch dir byte-stable? default format-patch vs --zero-commit --no-signature
set -euo pipefail
cd /var/tmp/P2/linux-subset; W=/var/tmp/P2/repro; rm -rf "$W"; mkdir -p "$W"
sum() { (cd "$1" && cat 0*.patch | sha256sum | cut -c1-16); }
git format-patch -q -o "$W/def1" v6.18.55..p2/bsp
git format-patch -q --zero-commit --no-signature -o "$W/zero1" v6.18.55..p2/bsp
# "re-do" the integration branch tomorrow: same patches, new committer date -> new shas
git switch -q -C p2/bsp-redo v6.18.55
GIT_COMMITTER_DATE="2026-10-05T09:00:00Z" git cherry-pick v6.18.55..p2/bsp >/dev/null
git format-patch -q -o "$W/def2" v6.18.55..p2/bsp-redo
git format-patch -q --zero-commit --no-signature -o "$W/zero2" v6.18.55..p2/bsp-redo
printf 'default : %s vs %s\n' "$(sum $W/def1)" "$(sum $W/def2)"
printf 'stable  : %s vs %s\n' "$(sum $W/zero1)" "$(sum $W/zero2)"
echo "--- what differs in the default output:"
diff "$W/def1/0002-"*.patch "$W/def2/0002-"*.patch || true
tail -3 "$W/def1/0002-"*.patch
```

```text
default : 70792b8f4794d583 vs 1ebc3563c97ea542
stable  : cbeca173e0b18adc vs cbeca173e0b18adc
--- what differs in the default output:
1c1
< From 62eb9a7cc9db66790d7dc6d2c0f0133a112b91f5 Mon Sep 17 00:00:00 2001
---
> From 596312bc3aefcf921123644b6389ac4ecbc4e7f6 Mon Sep 17 00:00:00 2001
-- 
2.53.0
```

출력에서 볼 것:

- 기본 출력은 내용이 같아도 해시가 다르다. 차이는 첫 줄 `From <커밋 sha>` 하나다. 또 파일 끝에 **git 버전**(`2.53.0`) 서명이 붙는다 — 동료의 git 버전이 다르면 그것만으로 diff가 생긴다.
- `--zero-commit --no-signature`로 내보내면 두 번의 sha256이 같다. 레이어 저장소의 리뷰 diff에 "의미 없는 변경"이 섞이지 않고, Yocto의 패치 파일 체크섬·sstate도 불필요하게 깨지지 않는다.
- 작성 날짜(`Date:`)는 커밋의 author date라 cherry-pick으로 바뀌지 않는다. 사람 손으로 amend하면 바뀐다 — 그것은 의미 있는 변경이니 괜찮다.

### 9.3 재현성 체크리스트 (BSP 관점)

- [ ] 모든 레이어가 커밋 sha로 고정돼 있다 (kas lock 또는 repo manifest의 revision — K5 1절).
- [ ] 커널·U-Boot·TF-A·OP-TEE 레시피가 `SRCREV`로 고정돼 있다 (`AUTOREV` 금지).
- [ ] 벤더 저장소를 **우리 미러**에도 받아 둔다. 벤더가 브랜치를 지우거나 force-push하거나 서버를 옮기면 5년 뒤 빌드가 안 된다. Yocto의 `PREMIRRORS`/`BB_GENERATE_MIRROR_TARBALLS`로 소스 tarball을 보관한다 (K5).
- [ ] 패치 파일은 `--zero-commit --no-signature`로 내보내고, 내보내기 스크립트를 저장소에 둔다.
- [ ] BSP 릴리스마다 태그를 단다: 커널 저장소에 `p2-bsp-1.0`, 레이어 저장소에 같은 이름. 두 태그의 대응을 릴리스 노트에 적는다.
- [ ] 외부에서 받은 파일은 이 노트의 예제 2처럼 **sha로 고정한 URL**로 받는다 (브랜치 이름 URL은 내일 다른 파일을 준다).
- [ ] `SOURCE_DATE_EPOCH` 기반 재현 빌드를 켜 두고 (K5 8절), 같은 입력으로 두 번 빌드해 이미지 해시를 비교하는 CI를 둔다 (P6).

---

## 10. 커널 안에서는 무슨 일이 일어나나

이 노트의 실습에서 커널 안을 지나간 경로 세 개를 정리한다.

1. **`devm_regulator_bulk_get_enable()` (0002 패치)**: 이름 배열(`"vs"`, `"vdd-io"`)마다 regulator core가 DT의 `<name>-supply` 속성을 찾는다 (`of_get_regulator` 계열). 찾으면 그 레귤레이터의 consumer가 되고 `regulator_enable()`이 고정 레귤레이터 드라이버의 GPIO를 올린다. 이때 DT의 `startup-delay-us`만큼 기다린다 (6.3절에서 5 ms로 고친 값). 못 찾으면 `supply ... not found, using dummy regulator`를 찍고 더미를 돌려준다 (7.1절 출력). devm이라 드라이버 unbind 때 역순으로 disable된다 (G3, H3).
2. **match data 경로 (0003 패치)**: I2C 장치가 생기면 I2C core가 드라이버를 매칭하는 순서는 OF 테이블 → ACPI 테이블 → I2C id 테이블이다. probe 안에서 "어떤 칩인가"를 묻는 함수도 이 순서를 따라야 한다. `device_get_match_data()`는 앞의 둘(펌웨어 노드)만 보고, `i2c_get_match_data()`는 세 번째까지 본다. 그래서 DT로 만든 장치에서는 둘 다 동작하고, id 테이블로 만든 장치에서는 앞의 것만 실패했다 (D3, G2).
3. **펌웨어 블롭 (1.4절)**: SDMA 드라이버가 probe 중 `request_firmware_nowait()` 계열로 `imx/sdma/sdma-imx7d.bin`을 요청하면 firmware loader가 `/lib/firmware/`에서 파일을 찾는다. 없으면 `Direct firmware load for ... failed with error -2`(ENOENT) 로그가 난다 (6.18.55 `drivers/base/firmware_loader/main.c`에 이 문자열이 있다). BSP의 펌웨어 패키지 버전과 드라이버가 기대하는 파일 형식이 맞아야 한다 (G10).

그리고 블롭 ABI 락인(1.5절)은 커널 안에서 이렇게 보인다. 벤더 GPU 모듈은 out-of-tree 모듈이라 커널의 `vermagic`(버전 + 주요 설정)이 맞아야 로드되고 (E3), 커널 내부 API(DRM, dma-buf, mm)가 바뀌면 소스 수정 없이는 빌드조차 안 된다 (E4). 유저 라이브러리는 그 모듈의 ioctl 번호·구조체 레이아웃에 맞춰 컴파일돼 있다 (G5). 세 개의 "버전 짝"이 동시에 맞아야 하는 것이다.

---

## 11. Embedded 실무에서 다시 보기

### 11.1 센서 키트 첫 주 — "EVK BSP를 그대로 띄우고, 차이를 패치로"

입사 첫 주에 i.MX8MP 기반 센서 키트 보드를 받았다면: (1) 벤더 BSP 릴리스 노트의 구성품 표대로 EVK 이미지를 빌드해 EVK에서 부팅한다 (기준선). (2) `meta-<company>`에 MACHINE 하나를 만들고 DT를 EVK에서 복사해 보드 차이만 고친다 (P4). (3) 모든 변경을 4절처럼 토픽 브랜치 → 통합 → 패치 디렉터리로. (4) EVK 이미지와 우리 보드 이미지를 같은 레이어 스택에서 매일 빌드한다. 문제가 생기면 "EVK에서도 나나?"로 HW/SW를 가른다.

### 11.2 Jetson — 벤더 커널이 사실상 고정인 경우

NVIDIA Jetson은 GPU·카메라(ISP)·NVENC 스택이 L4T의 커널 모듈과 유저 라이브러리에 묶여 있어서, 실제 제품은 L4T 커널을 쓰는 것이 일반적이다 (E7 3.2, 기억 기반). 이 경우 우리 일은 "커널 선택"이 아니라 **L4T 버전 업그레이드 때 우리 패치 스택을 옮기는 비용을 줄이는 것**이다. 그래서 커널 패치는 최소로 (DT·out-of-tree 모듈 위주로) 하고, 우리 드라이버는 out-of-tree 모듈로 유지하는 팀이 많다. `meta-tegra`를 쓰면 L4T 블롭 버전도 Yocto 레시피로 고정된다.

### 11.3 고객의 보안 감사 — "CVE-XXXX가 이 제품에 해당되나?"

벤더 커널 기반 제품이라면 답하는 순서: (1) 벤더 브랜치의 기준 stable 버전(1.5절처럼 `SUBLEVEL`)과 마지막 머지 날짜 확인. (2) 그 CVE의 수정 커밋이 기준 버전 이후 stable에 들어갔는지 확인. (3) 들어갔지만 벤더가 아직 머지 안 했다면 우리 스택에 `Backport [sha]`로 얹는다 (`cherry-pick -x`). (4) 8.2절 장부에 "벤더가 따라오면 drop" 표시. 벤더가 stable을 늦게 따라올수록 이 backport가 쌓인다 — 2.2절 매트릭스의 "보안 수정" 행이 현실이 되는 장면이다.

### 11.4 NPU를 쓰는 로보틱스 제품

AI 가속기 진로와 연결되는 장면이다. NPU 유저 런타임이 벤더 블롭이고 커널 모듈과 짝이면(1.5절), 커널을 최신 LTS로 올리고 싶어도 NPU 스택 때문에 못 올린다. 선택지는 (a) 벤더 BSP 일정에 맞춘다, (b) 벤더 모듈을 새 커널로 포팅해 우리 스택에 얹는다 (부채 증가), (c) mainline `drivers/accel/`의 오픈 드라이버(1.4절 `rocket`처럼 해당 SoC에 있다면)로 옮긴다. O4에서 DRM accel 구조를 다룬다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 벤더 레이어를 포크해서 직접 수정 | 새 BSP 릴리스마다 수백 개 충돌 | 우리 변경이 벤더 파일 안에 섞임 | 벤더 레이어는 고정, 변경은 `meta-<company>`의 bbappend로 |
| 패치를 mainline 기준으로 만들어 벤더 커널 레시피에 넣음 | `do_patch` hunk 실패 | 패치 기준과 `SRCREV`가 다름 (K5 3.5) | 레시피가 받는 바로 그 소스에서 만든다 (`devtool modify`) |
| 여러 변경을 한 커밋에 | upstream으로 보낼 부분만 떼기 어려움, 리베이스 충돌 범위 큼 | "한 패치 = 한 논리적 변경" 위반 | 토픽 브랜치에서 쪼개서 다시 커밋 |
| HACK을 스택 중간에 둠 | 릴리스 이미지에 디버그 코드, 위 패치가 HACK에 의존 | 순서 규칙 위반 | HACK은 맨 위·별도 디렉터리·변수로 on/off (5.3절) |
| `quilt add` 없이 파일 수정 | refresh해도 패치에 변경이 없음 | quilt가 원본 사본을 못 떴다 | 수정 전에 `quilt add`, 또는 `quilt edit` 사용 |
| `quilt import`를 반복 | series 순서가 뒤집힘 (6.2절) | 현재 top 다음에 삽입 | series 파일을 직접 쓰거나 import 순서를 거꾸로 |
| `Upstream-Status` 누락 | OE QA `patch-status` 경고/에러, 패치의 운명을 아무도 모름 | 습관 부재 | 커밋 메시지에 처음부터 넣고, 감사 스크립트를 CI에 (5.2절) |
| 기본 `format-patch`로 레이어 갱신 | 내용이 같은데 모든 패치 파일이 diff에 뜸 | `From <sha>`와 git 버전 서명 (9.2절) | `--zero-commit --no-signature` |
| 벤더 브랜치 URL(브랜치 이름)로 소스 받기 | 몇 달 뒤 같은 빌드가 다른 결과 | 브랜치는 움직인다 | sha 고정 + 우리 미러 |
| 블롭 ABI를 확인 안 하고 커널 업그레이드 | GPU/NPU 모듈 로드 실패 또는 유저 라이브러리 ioctl 에러 | 모듈 vermagic·ioctl 버전 짝이 깨짐 | 업그레이드 전에 블롭 의존 목록 작성 (2.2절 매트릭스) |

---

## 13. 면접에서 이렇게 말한다

**Q.** "What does a vendor BSP typically contain, and which parts would you expect to own?"

**A.** 부트 체인 포크(이미지 패커, DDR 트레이닝 FW, TF-A, OP-TEE, U-Boot), LTS 기반 벤더 커널과 EVK DT, `/lib/firmware`용 블롭, GPU/NPU 유저 라이브러리, Yocto 레이어, 그리고 릴리스 노트·errata다. 우리가 소유하는 것은 그 위의 회사 레이어: 보드 DT, 커널·U-Boot 패치, 설정 조각, MACHINE 정의다. 나머지는 버전을 고정해서 쓰고, 고칠 일이 있으면 패치로 얹는다.

> A BSP is the vendor's bundle: forked boot chain — imaging tool, DDR training firmware, TF-A, OP-TEE, U-Boot — a vendor kernel that's an LTS tag plus thousands of vendor commits, the EVK device trees, firmware blobs for /lib/firmware, userspace GPU or NPU libraries, Yocto layers, and the release notes and errata. What I own is our layer on top: the board DT, our kernel and U-Boot patches, config fragments and the machine definition. Everything below is pinned by commit, and if I need to change it, I carry a patch rather than editing the vendor's tree.

**Q.** "Vendor kernel or mainline LTS — how would you decide for a new product?"

**A.** 첫째, 필요한 블록(ISP·NPU·GPU·코덱)이 mainline에 있나. 둘째, 없다면 그 블록의 유저 블롭이 벤더 커널 모듈과 ABI로 묶였나 — 묶였으면 사실상 벤더 커널이다. 셋째, 벤더가 stable을 얼마나 자주 머지하나(보안). 넷째, 제품 수명과 LTS/CIP 수명. 어느 쪽이든 우리 코드는 upstream 스타일로 쓰고 스택을 작게 유지해서 나중에 갈아탈 수 있게 한다.

> I go through a few questions. Are the blocks we need — ISP, NPU, GPU, codecs — supported in mainline? If not, is the userspace blob tied to a vendor kernel module ABI? If yes, the vendor kernel is effectively mandatory. Then security: how far behind stable is the vendor branch and how often do they merge it. Then lifetime versus LTS or CIP SLTS. Either way, I write our drivers and DT in upstream style and keep the patch stack small, so switching later is a project, not a rewrite.

**꼬리 질문**: "How would you check how far the vendor branch is behind stable?" → `Makefile`의 SUBLEVEL, 마지막 머지 날짜, `git merge-base`로 기준 태그 확인 (1.5절, 2.4절).

**Q.** "How do you manage your kernel patches on top of the vendor BSP?"

**A.** 변경 종류별 토픽 브랜치(backport, 드라이버 수정, 보드, 디버그)를 기준 태그에서 따로 만들고, 스크립트로 정해진 순서의 선형 통합 브랜치를 만든다. 순서는 upstream으로 갈 것부터, HACK은 맨 위. 모든 패치에 `Upstream-Status`와 Signed-off-by. 통합 브랜치에서 `format-patch --zero-commit`으로 Yocto 레이어에 내보내고, 패치 디렉터리를 원본에 적용한 결과가 git과 같은지 CI에서 확인한다.

> I keep one topic branch per kind of change — backports, driver fixes, board support, debug hacks — each based on the same tag, and a script builds a linear integration branch in a fixed order: upstream-bound patches first, board-specific next, hacks on top and excluded from release builds. Every patch has a Signed-off-by and an Upstream-Status line. I export with format-patch, zero-commit, into our Yocto layer, and CI checks that applying the patch directory to the pristine source gives exactly the git tree.

**Q.** "When a new BSP arrives, how do you know which of your patches you can drop?"

**A.** `git cherry`로 새 기준 대비 patch-id가 같은 커밋을 찾는다 — `-`는 이미 upstream에 있다. 고쳐서 머지된 것은 patch-id가 달라서 안 잡히니 `range-diff`와 `Upstream-Status`의 lore 링크로 사람이 확인한다. 그리고 `Backport [sha]` 패치는 그 sha가 새 기준의 조상인지 `merge-base --is-ancestor`로 본다. 실제 리베이스 절차는 P5.

> First `git cherry` against the new base — anything marked minus has an identical patch upstream and gets dropped. Patches that were modified during review won't match by patch-id, so for those I follow the Submitted link and compare with range-diff. Backports carry the upstream sha from cherry-pick -x, so I just check whether that commit is an ancestor of the new base.

**Q.** "What does 'blob ABI lock-in' mean and why does it matter?"

**A.** GPU·NPU 유저 라이브러리가 바이너리이고, 특정 버전의 벤더 커널 모듈 ioctl에 맞춰져 있다는 뜻이다. 커널을 올리려면 그 모듈을 새 커널로 포팅해야 하는데 커널 내부 API는 안정적이지 않아서 비용이 크고, 벤더 지원도 끊긴다. 그래서 커널 업그레이드 일정이 벤더 BSP 일정에 묶인다. 오픈 드라이버(etnaviv, DRM accel)가 있으면 탈출구가 된다.

> The userspace driver is a binary built against one specific version of the vendor's kernel module interface. To move the kernel, you have to port that module to a new kernel whose internal APIs have changed, and you lose vendor support while doing it. So your kernel schedule becomes the vendor's BSP schedule. An open driver in mainline, like etnaviv or a DRM accel driver, is the way out, if its feature set is good enough.

**Q.** "How do you make a BSP build reproducible five years from now?"

**A.** 레이어·레시피를 sha로 고정하고(kas lock, SRCREV), 벤더 저장소와 소스 tarball을 우리 미러에 보관하고, 패치는 결정적으로 내보내고, 커널 저장소와 레이어 저장소에 같은 릴리스 태그를 단다. CI에서 같은 입력으로 두 번 빌드해 해시를 비교한다.

> Pin everything by hash — layers through a kas lock file, sources through SRCREV — mirror the vendor repositories and source tarballs on our own servers because vendors do move or rewrite branches, export patches deterministically, and tag the kernel repo and the layer repo together for each release. Then CI rebuilds from the same inputs and compares image hashes.

**Q.** "Tell me about working with a vendor SDK where you had to carry local changes."

**A.** (Don의 SSD 경험을 그대로) 컨트롤러 벤더 SDK 위에 자사 FTL·에러 처리 패치를 얹어 양산했다. 패치마다 "벤더 반영 요청 여부"를 장부로 관리했고, 새 SDK가 오면 반영된 것은 버리고 남은 것만 옮겼다. Linux BSP에서는 같은 일을 `Upstream-Status`, `git cherry`, Yocto 패치 디렉터리로 한다는 것을 이 노트의 실습으로 해 봤다.

> On enterprise SSD firmware we built on a controller vendor's SDK and carried our own patches for error handling and performance. We kept a ledger of every local change and whether we'd asked the vendor to take it, and when a new SDK drop arrived we dropped what they'd absorbed and ported the rest. In Linux the same discipline maps to Upstream-Status tags, git cherry against the new base, and a patch directory in our Yocto layer — I've practiced exactly that flow on a 6.18 tree.

---

## 14. 직접 해보기

1. **개념** — 벤더 BSP 구성품 중 "우리가 절대 못 고치는 것" 두 개와 "고칠 수는 있지만 고치면 안 되는 것" 하나를 고르고 이유를 쓰라.
   정답/힌트: Boot ROM, 펌웨어·유저 블롭(바이너리) / 벤더 Yocto 레이어(포크 금지, bbappend로).

2. **조사** — 1.5절의 명령을 `nxp-imx/linux-imx`의 `lf-6.6.y` 브랜치에 돌려 기준 SUBLEVEL과 마지막 커밋 날짜를 확인하고, 같은 날짜의 6.6.y stable과 몇 point release 차이인지 E7 예제 1의 `releases.json`으로 계산하라.
   정답/힌트: 날짜가 아니라 SUBLEVEL 차이가 "아직 받지 못한 stable 수정 묶음 수"다.

3. **코드 과제** — 4.2절 스택에 `topic/backports`로 backport를 하나 더 넣되 `--start-number 100`을 써서 `0100-`부터 번호가 붙게 내보내고, 7.3절처럼 중간 삽입이 있어도 다른 범주 번호가 안 밀리게 하는 내보내기 스크립트를 만들라.
   정답/힌트: 범주별로 `git format-patch --start-number N v6.18.55..topic/X` 형태로 나눠 내보내되, 통합 브랜치 순서와 같은지 5.4절 `diff -r` 검사로 확인.

4. **quilt 과제** — 6.3절 트리에서 `quilt add` 없이 새 파일을 고친 뒤 `quilt refresh`하면 무엇이 패치에 들어가는지 확인하고, `quilt edit`로 같은 작업을 다시 해 차이를 설명하라.
   정답/힌트: add 없이 고친 기존 파일 변경은 빠진다. `quilt edit`는 add + 에디터 실행을 한 번에 한다.

5. **관찰** — 7.1절 QEMU 스크립트에서 `i2cset`으로 DEVID를 `0xE5`가 아닌 값으로 바꿔 수정 후 드라이버를 올리고, dmesg에 무엇이 찍히는지 확인하라. 수정 전 드라이버에서는 같은 실험이 왜 아무 메시지도 안 내는지 10절의 match data 경로로 설명하라.
   정답/힌트: 수정 후는 `Invalid device ID` (`dev_err_probe`), 수정 전은 DEVID를 읽기 전에 `-ENODEV`로 조용히 끝난다.

6. **설계** — 우리 회사가 i.MX8MP NPU를 쓰는 로봇을 10년 지원해야 한다. 2.2절 매트릭스로 벤더 커널 / LTS + 벤더 모듈 / CIP SLTS 계열 LTS 세 안을 평가하고, 각 안의 "5년 차 위험" 하나씩을 쓰라.
   정답/힌트: 벤더 BSP 지원 종료, 벤더 모듈 포팅 부채, NPU 블롭이 그 LTS를 지원하지 않을 위험.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| BSP | Board Support Package | SoC 벤더가 주는 부트 체인·커널·블롭·레이어·문서 묶음 |
| 벤더 커널 | vendor kernel | LTS 태그 + 벤더 커밋 수백~수천 개 (`linux-imx`, L4T 등) |
| mainline | upstream Linux | Linus 트리와 그 stable/LTS 가지 |
| 패치 스택 | patch stack / queue | 기준 태그 위에 순서대로 쌓은 우리 패치 묶음 |
| 토픽 브랜치 | topic branch | 한 종류 변경만 담은 브랜치, 통합 브랜치의 재료 |
| 통합 브랜치 | integration branch | 토픽을 정해진 순서로 합친 선형 브랜치 (스크립트가 만듦) |
| Upstream-Status | OE patch header tag | 패치의 upstream 상태 (Pending, Submitted, Backport, Inappropriate …) |
| quilt | patch queue tool | series 파일로 패치를 push/pop/refresh하는 도구 |
| series 파일 | quilt series | quilt 스택의 순서 정의 (Yocto `SRC_URI` 패치 목록과 같은 역할) |
| patch-id | git patch-id | diff 내용의 해시. `git cherry`·rebase가 "같은 변경"을 알아보는 기준 |
| range-diff | git range-diff | 두 패치 시리즈의 커밋을 짝지어 비교 |
| 블롭 | binary blob | 소스 없이 바이너리로만 주는 펌웨어·라이브러리 |
| 블롭 ABI 락인 | blob ABI lock-in | 유저 블롭이 특정 커널 모듈 인터페이스에 묶여 커널을 못 바꾸는 상태 |
| galcore | Vivante GPU kernel module | i.MX 벤더 GPU 스택의 커널 쪽 (`drivers/mxc/gpu-viv`) |
| CIP SLTS | Civil Infrastructure Platform Super LTS | 특정 LTS를 약 10년 유지하는 프로젝트 |
| errata | chip errata | 실리콘 버그 목록과 SW 우회법 |
| `meta-<company>` | company layer | 벤더 레이어 위에 얹는 우리 Yocto 레이어 |
| dummy regulator | regulator core fallback | DT에 supply가 없을 때 regulator core가 주는 가짜 레귤레이터 |
| i2c-stub | fake I2C chip module | 레지스터 256개짜리 가짜 I2C 장치. 실제 드라이버 probe 시험용 |

---

## 16. 요약 & 체크리스트

벤더 BSP는 부트 체인 포크, LTS 기반 벤더 커널과 DT, 펌웨어·유저스페이스 블롭, Yocto 레이어, 문서로 이루어진다. 커널 선택은 "mainline이 더 좋다"가 아니라 필요한 블록의 upstream 상태, 블롭 ABI 락인, 벤더의 stable 추종, 제품 수명으로 정한다. 어느 쪽이든 우리 커널은 "기준 태그 + 패치 스택"이고, 스택은 토픽 브랜치 → 순서가 정해진 선형 통합 브랜치 → 결정적 `format-patch` → Yocto 패치 디렉터리로 관리한다. 모든 패치에 `Upstream-Status`를 달고, `git cherry`로 upstream에 간 것을 찾아 버리며, 장부로 "언제 사라질지"를 관리한다. quilt는 같은 모델(series 파일)의 다른 도구이고, git과 오갈 때는 진실의 원천을 하나로 둔다. 스택이 작고 깨끗할수록 다음 LTS로의 이동(P5)이 쉬워진다.

- [ ] 벤더 BSP의 구성품 8가지를 그림으로 그리고 각각을 누가 고치는지 설명할 수 있다
- [ ] 커널 트리에서 `MODULE_FIRMWARE`와 `firmware-name`으로 보드가 필요한 펌웨어 블롭을 찾을 수 있다
- [ ] 블롭 ABI 락인이 무엇이고 커널 업그레이드에 왜 문제인지 설명할 수 있다
- [ ] 벤더 커널 vs mainline LTS를 6개 이상의 축으로 비교해 결정을 설명할 수 있다
- [ ] 벤더 브랜치의 기준 stable 버전과 뒤처진 정도를 확인하는 명령을 쓸 수 있다
- [ ] 같은 기준 버전에서 벤더 파일과 mainline 파일을 비교해야 하는 이유를 말할 수 있다
- [ ] 토픽 브랜치에서 선형 통합 브랜치를 만드는 스크립트를 짤 수 있다
- [ ] `Upstream-Status` 값의 종류와 각각의 의미를 말하고 감사 스크립트를 짤 수 있다
- [ ] quilt로 중간 패치를 고치고 새 패치를 얹을 수 있으며 `quilt add` 순서의 함정을 안다
- [ ] Yocto bbappend로 릴리스 패치와 디버그 패치를 분리해 넣을 수 있다
- [ ] `git cherry`와 `range-diff`로 스택 중 upstream에 간 것을 찾을 수 있다
- [ ] 패치 내보내기와 BSP 빌드를 재현 가능하게 만드는 방법 5가지를 말할 수 있다

---

## 참고 자료

- Yocto Project 문서 — "Patch Upstream Status" (OpenEmbedded 패치 가이드라인), Kernel Development Manual, BSP Developer's Guide: https://docs.yoctoproject.org/
- quilt 프로젝트 (GNU Savannah): https://savannah.nongnu.org/projects/quilt — 이 노트에서 쓴 `quilt-0.69.tar.gz`는 https://download.savannah.gnu.org/releases/quilt/
- git 문서: `git-format-patch`, `git-cherry`, `git-range-diff`, `git-quiltimport` — https://git-scm.com/docs
- docs.kernel.org — "Submitting patches" (process/submitting-patches), "Everything you ever wanted to know about Linux -stable releases" (process/stable-kernel-rules)
- NXP 벤더 커널 저장소: https://github.com/nxp-imx/linux-imx (이 노트의 예제는 `lf-6.12.y`의 커밋 `df24f9428e38740256a410b983003a478e72a7c0`)
- linux-stable: https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
- Civil Infrastructure Platform — https://www.cip-project.org/
- 소스 위치 (6.18.55): `drivers/iio/accel/adxl345_core.c`, `drivers/iio/accel/adxl345_i2c.c`, `drivers/i2c/i2c-stub.c`, `drivers/dma/imx-sdma.c`, `drivers/base/firmware_loader/main.c`, `Documentation/devicetree/bindings/iio/accel/adi,adxl345.yaml` — elixir.bootlin.com에서 같은 경로로 볼 수 있다
- 관련 노트: E7(버전·벤더 커널·CIP·rebase 기초), K5(Yocto BSP 레이어·devtool·재현성), C4·C5(U-Boot·TF-A·OP-TEE), G10(펌웨어 로딩), H3(regulator), P1·P3·P4·P5·P6·P7, O4·O5
