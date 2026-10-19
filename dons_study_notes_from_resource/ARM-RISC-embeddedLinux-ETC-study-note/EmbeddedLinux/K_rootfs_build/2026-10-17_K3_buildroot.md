# K3. Buildroot — defconfig, 패키지 추가, BR2_EXTERNAL, 산출물

> **이 노트를 다 읽으면**: Buildroot가 "Makefile + Kconfig로 툴체인 → 패키지 → rootfs → 이미지를 한 번에 만드는 공장"이라는 걸 그림으로 설명하고 `output/{build,host,staging,target,images}`가 각각 무엇인지 말할 수 있다 · defconfig/savedefconfig, 패키지(`Config.in` + `.mk` + `.hash`), generic/cmake/kernel-module 인프라로 회사 패키지를 직접 추가할 수 있다 · 회사 제품을 BR2_EXTERNAL 트리(overlay, post-build, post-image, users/device 테이블, genimage)로 Buildroot 본체와 분리해 관리할 수 있다 · "Buildroot는 변경을 추적하지 않는다"는 재빌드 규칙과 `<PKG>_OVERRIDE_SRCDIR` 드라이버 개발 루프를 설명하고 Yocto와 30초 안에 비교할 수 있다
> **왜 필요한가**: 스타트업·소형 제품(드론, 센서 키트, 게이트웨이)은 Buildroot로 이미지를 만드는 곳이 많고, 면접에서는 "Buildroot와 Yocto 차이", "패키지 하나를 어떻게 추가하나", "staging과 target 차이", "왜 menuconfig 바꾸고 다시 make 했는데 반영이 안 되나"가 단골이다
> **Don 기준 난이도**: Makefile·링커·크로스 컴파일·이미지 레이아웃(펌웨어 바이너리 + 파티션)은 Don이 SSD 펌웨어 빌드에서 이미 다뤘다 / 새로 배울 부분은 **Buildroot의 패키지 인프라 규칙, BR2_EXTERNAL로 제품을 분리하는 방식, rootfs 마무리 단계(overlay → post-build → fakeroot → post-image)의 순서**
> **선행 노트**: B1–B4(크로스 툴체인·sysroot·musl/glibc·crosstool-NG), C3(부트 체인), E2(커널 빌드·defconfig), E4(out-of-tree 모듈), K1(rootfs 구조), K2(BusyBox). Yocto 깊이는 K4·K5, 라이선스는 K7

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어 시절을 떠올려 보자. 펌웨어 빌드는 "툴체인(armcc/gcc) 고정 → 소스 모듈 컴파일 → 링커 스크립트로 묶기 → 이미지 헤더·서명 붙이기 → 다운로드용 바이너리" 순서였고, 이 전부를 Makefile 하나와 설정 파일 몇 개가 지휘했다.

Embedded Linux 장비도 똑같은 문제를 푼다. 다만 재료가 훨씬 많다. 크로스 컴파일러, C 라이브러리(glibc/musl), 커널, 부트로더, BusyBox, 수십~수백 개의 오픈소스 패키지(i2c-tools, can-utils, openssl …), 회사 앱, 그리고 이걸 담을 ext4·SD 카드 이미지. **Buildroot는 이 전체를 `make` 한 번으로 재현 가능하게 만드는 도구**다. 본체는 거의 전부 GNU Make 규칙과 Kconfig(커널과 같은 menuconfig 설정 언어)다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">Buildroot = make 한 번으로 도는 이미지 공장</text>
  <rect x="6" y="40" width="120" height="130" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="66" y="62" text-anchor="middle" font-size="13" font-weight="bold">① 설정</text>
  <text x="66" y="86" text-anchor="middle" font-size="12">defconfig</text>
  <text x="66" y="106" text-anchor="middle" font-size="12">→ .config</text>
  <text x="66" y="126" text-anchor="middle" font-size="12">(Kconfig)</text>
  <text x="66" y="150" text-anchor="middle" font-size="12">menuconfig</text>
  <rect x="144" y="40" width="120" height="130" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="204" y="62" text-anchor="middle" font-size="13" font-weight="bold">② 툴체인</text>
  <text x="204" y="86" text-anchor="middle" font-size="12">internal 빌드</text>
  <text x="204" y="106" text-anchor="middle" font-size="12">또는 external</text>
  <text x="204" y="126" text-anchor="middle" font-size="12">cross gcc</text>
  <text x="204" y="150" text-anchor="middle" font-size="12">+ sysroot</text>
  <rect x="282" y="40" width="120" height="130" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="342" y="62" text-anchor="middle" font-size="13" font-weight="bold">③ 패키지</text>
  <text x="342" y="86" text-anchor="middle" font-size="12">download</text>
  <text x="342" y="106" text-anchor="middle" font-size="12">extract·patch</text>
  <text x="342" y="126" text-anchor="middle" font-size="12">configure·build</text>
  <text x="342" y="150" text-anchor="middle" font-size="12">install</text>
  <rect x="420" y="40" width="120" height="130" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="480" y="62" text-anchor="middle" font-size="13" font-weight="bold">④ rootfs 마무리</text>
  <text x="480" y="86" text-anchor="middle" font-size="12">target/ 정리</text>
  <text x="480" y="106" text-anchor="middle" font-size="12">overlay 복사</text>
  <text x="480" y="126" text-anchor="middle" font-size="12">post-build.sh</text>
  <text x="480" y="150" text-anchor="middle" font-size="12">users·devices</text>
  <rect x="558" y="40" width="116" height="130" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="616" y="62" text-anchor="middle" font-size="13" font-weight="bold">⑤ 이미지</text>
  <text x="616" y="86" text-anchor="middle" font-size="12">fakeroot</text>
  <text x="616" y="106" text-anchor="middle" font-size="12">mkfs.ext4</text>
  <text x="616" y="126" text-anchor="middle" font-size="12">post-image.sh</text>
  <text x="616" y="150" text-anchor="middle" font-size="12">genimage</text>
  <line x1="126" y1="105" x2="142" y2="105" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="264" y1="105" x2="280" y2="105" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="402" y1="105" x2="418" y2="105" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="540" y1="105" x2="556" y2="105" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="6" y="190" width="668" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="215" text-anchor="middle" font-size="13">전부 output/ 아래: build/ · host/ · staging(sysroot) · target/ · images/</text>
  <text x="340" y="256" text-anchor="middle" font-size="13">순서는 Makefile 규칙 + 패키지별 stamp 파일이 정한다</text>
  <text x="340" y="280" text-anchor="middle" font-size="13">설정 변경을 추적해 자동 재빌드하지는 않는다 (10절)</text>
</svg>
```

그림 1 — Buildroot의 다섯 단계. ①에서 고른 설정으로 ②툴체인을 만들고(또는 받아 오고), ③패키지를 의존 순서대로 빌드해 `target/`에 설치한 뒤, ④rootfs를 다듬고 ⑤파일시스템·디스크 이미지를 만든다.

bare-metal 비유로 정리하면 이렇다.

| Buildroot | SSD 펌웨어 빌드에서의 대응 |
|---|---|
| `configs/<board>_defconfig` | 제품별 빌드 설정 헤더(`#define` 묶음) |
| 툴체인 단계 | 고정된 컴파일러 버전 + 런타임 라이브러리 |
| 패키지(`package/<pkg>/`) | 서드파티 라이브러리 소스를 받아 빌드하는 규칙 |
| `output/target/` | 링크 직전의 오브젝트 묶음 (제품에 실릴 것만) |
| `output/images/` | 최종 다운로드 이미지(헤더·파티션 포함) |
| post-image 스크립트 | 이미지 서명·패키징 스크립트 |

### 0.1 이 노트의 실행 원칙 — 무엇을 실제로 돌렸나

Don의 요청으로 **전체 Buildroot 빌드(툴체인 빌드, 커널·패키지 컴파일, 이미지 생성)는 실행하지 않았다.** 시간과 디스크가 많이 들기 때문이다. 대신 다운로드·컴파일이 필요 없는 단계는 전부 실제로 돌렸고, 빌드가 필요한 부분은 **"실행하지 않음 — 가이드"** 라벨을 붙여 명령과 기대 산출물(문서·소스 기준)만 적었다. 가이드 블록에는 출력 블록을 붙이지 않는다.

| 구분 | 내용 |
|---|---|
| 실제 실행 | `git clone --depth 1`, `make qemu_aarch64_virt_defconfig`, `savedefconfig`, `list-defconfigs`, `printvars`, `show-info`/`show-depends`/`external-deps`, `utils/config`로 Kconfig 조작, BR2_EXTERNAL 트리 로드, `utils/check-package` |
| 실제 실행 (Buildroot 밖에서 같은 단계 재현) | 회사 패키지 소스를 VM gcc/cmake로 빌드, 커널 모듈을 랩 커널 트리로 빌드, overlay → post-build → fakeroot(mkusers·makedevs·mkfs.ext4)를 손으로 돌려 ext4 생성, 그 ext4를 QEMU로 부팅 |
| 가이드만 | `make`(전체 빌드), `linux-menuconfig`·`busybox-menuconfig`처럼 소스 다운로드가 필요한 타깃, `legal-info`, `sdk`, genimage 실행 |

실행 환경은 Lima VM `elinux`(aarch64, 호스트 커널 `7.0.0-38-generic`)와 공용 QEMU 랩(Linux 6.18.55)이다. Buildroot 작업 디렉터리는 VM의 `/var/tmp/K3/`, BR2_EXTERNAL 트리와 소스는 Mac 쪽 `~/.elinux_scratch/K3/`다.

---

## 1. Buildroot 받기와 디렉터리 구조

### 1.1 릴리스 체계 — 어떤 버전을 쓰나

Buildroot는 3개월마다(2월·5월·8월·11월) `YYYY.MM` 릴리스를 낸다. buildroot.org 다운로드 페이지(2026-10-17 확인) 기준 상황은 다음과 같다.

| 시리즈 | 최신 | 비고 |
|---|---|---|
| Stable | **2026.08** (2026-09-04) | 2026년 12월까지 버그픽스 |
| Old stable | 2026.05.3 (2026-09-10) | |
| LTS | **2025.02.18** (2026-09-10) | 2025.02부터 LTS는 **3년** 지원, 홀수 해 첫 릴리스(2025.02, 2027.02 …)가 LTS |

제품은 보통 **LTS를 기준으로 고정**하고 보안 픽스 포인트 릴리스(2025.02.x)만 따라간다. 이 노트는 최신 기능을 보기 위해 **2026.08**을 쓴다.

### 1.2 예제 1 — 얕은 clone과 최상위 구조

무엇을 확인하나: Buildroot 소스 트리 전체가 100 MB 남짓의 Makefile·Kconfig·패치 묶음이라는 것.

```sh
mkdir -p /var/tmp/K3 && cd /var/tmp/K3
git clone -q --depth 1 --branch 2026.08 https://gitlab.com/buildroot.org/buildroot.git k3_buildroot
cd k3_buildroot && git log --oneline -1 && du -sh . && ls
ls package | wc -l      # 패키지 디렉터리 수
ls configs | wc -l      # 보드 defconfig 수
grep -n "^export BR2_VERSION :=" Makefile
```

```text
d5180309 Update for 2026.08
99M	.
CHANGES
COPYING
Config.in
Config.in.legacy
DEVELOPERS
Makefile
Makefile.legacy
README
SECURITY.md
arch
board
boot
configs
docs
fs
linux
package
support
system
toolchain
utils
2993
305
95:export BR2_VERSION := 2026.08
```

출력에서 볼 것: 소스 코드는 **하나도 들어 있지 않다**. 2993개 패키지 디렉터리에는 "어디서 받아서 어떻게 빌드하나"라는 레시피만 있고, 실제 소스는 빌드할 때 `dl/`로 내려받는다. (clone 시 "refs/tags/2026.08 … is not a commit" 경고가 한 줄 나왔는데, annotated tag를 얕게 받을 때 나오는 정상 메시지다.)

### 1.3 최상위 디렉터리 — 무엇이 어디 있나

| 디렉터리 | 내용 | 회사에서 손대나 |
|---|---|---|
| `Makefile`, `Config.in` | 최상위 규칙과 메뉴 | 아니오 |
| `arch/` | CPU 아키텍처 Kconfig (`BR2_aarch64`, `BR2_cortex_a53` …) | 아니오 |
| `toolchain/` | internal/external 툴체인 로직 | 아니오 |
| `package/` | 패키지 레시피 (`<pkg>/Config.in`, `<pkg>.mk`, `<pkg>.hash`, 패치) + 인프라(`pkg-generic.mk`, `pkg-cmake.mk` …) | 업스트림에 기여할 때만 |
| `linux/`, `boot/` | 커널·부트로더(U-Boot, TF-A, barebox …) 레시피 | 아니오 |
| `system/` | rootfs skeleton, 기본 device table, init 선택 | 아니오 |
| `fs/` | rootfs 이미지 형식(ext2/3/4, squashfs, ubifs, cpio, erofs …) | 아니오 |
| `board/<vendor>/<board>/` | 보드별 커널 config, 패치, post-image 스크립트, genimage.cfg | **BR2_EXTERNAL로 옮긴다** |
| `configs/` | `<board>_defconfig` 305개 | **BR2_EXTERNAL로 옮긴다** |
| `support/`, `utils/` | 보조 스크립트(`mkusers`, `genimage.sh`), 개발 도구(`check-package`, `config`) | 사용만 |
| `docs/manual/` | 공식 매뉴얼 asciidoc 원본 — 가장 정확한 레퍼런스 | 읽기 |

핵심 원칙: **Buildroot 트리 자체는 업스트림 그대로 두고**, 회사 것(보드, defconfig, 패키지, 패치)은 6절의 BR2_EXTERNAL 트리에 넣는다. 그래야 2025.02.17 → 2025.02.18 업그레이드가 "git checkout 한 줄"로 끝난다.

---

## 2. defconfig 워크플로

### 2.1 직관 — 커널과 똑같은 Kconfig

Buildroot 설정은 커널 설정(E2)과 같은 Kconfig 언어다. `make menuconfig`로 메뉴를 고르면 `.config`(수천 줄)가 생기고, 이걸 **기본값과 다른 줄만** 남긴 것이 defconfig다. 커널과 다른 점은 심볼 접두어가 `CONFIG_`가 아니라 `BR2_`라는 것 하나다.

### 2.2 예제 2 — QEMU aarch64 보드 defconfig 적용

무엇을 확인하나: 18줄짜리 defconfig가 5307줄짜리 `.config`로 펼쳐지는 과정. `O=`로 출력 디렉터리를 소스 트리 밖에 둔다(커널의 `O=`와 같다).

```sh
cd /var/tmp/K3/k3_buildroot
cat configs/qemu_aarch64_virt_defconfig
time make O=/var/tmp/K3/k3_out qemu_aarch64_virt_defconfig
ls /var/tmp/K3/k3_out; du -sh /var/tmp/K3/k3_out; wc -l /var/tmp/K3/k3_out/.config
```

```text
BR2_aarch64=y
BR2_PACKAGE_HOST_LINUX_HEADERS_CUSTOM_6_18=y
BR2_GLOBAL_PATCH_DIR="board/qemu/patches"
BR2_DOWNLOAD_FORCE_CHECK_HASHES=y
BR2_SYSTEM_DHCP="eth0"
BR2_ROOTFS_POST_IMAGE_SCRIPT="board/qemu/post-image.sh"
BR2_ROOTFS_POST_SCRIPT_ARGS="$(BR2_DEFCONFIG)"
BR2_LINUX_KERNEL=y
BR2_LINUX_KERNEL_CUSTOM_VERSION=y
BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE="6.18.7"
BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y
BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE="board/qemu/aarch64-virt/linux.config"
BR2_LINUX_KERNEL_NEEDS_HOST_OPENSSL=y
BR2_TARGET_ROOTFS_EXT2=y
BR2_TARGET_ROOTFS_EXT2_4=y
# BR2_TARGET_ROOTFS_TAR is not set
BR2_PACKAGE_HOST_QEMU=y
BR2_PACKAGE_HOST_QEMU_SYSTEM_MODE=y
...
  GEN     /var/tmp/K3/k3_out/Makefile
#
# configuration written to /var/tmp/K3/k3_out/.config
#

real	0m0.668s
...
Makefile
build
676K	/var/tmp/K3/k3_out
5307 /var/tmp/K3/k3_out/.config
```

출력에서 볼 것: `...` 자리에는 Kconfig 도구(`conf`)를 호스트 gcc로 컴파일하는 줄이 있다(생략). 0.7초, 676 KB — **아직 아무것도 다운로드하거나 빌드하지 않았다.** `O=` 디렉터리에는 `.config`와, 그 디렉터리에서 바로 `make`를 칠 수 있게 해 주는 래퍼 `Makefile`만 생겼다. 이후 명령은 전부 `/var/tmp/K3/k3_out`에서 그냥 `make …`로 친다.

### 2.3 예제 3 — 펼쳐진 `.config`에서 중요한 기본값

무엇을 확인하나: defconfig에 적지 않은 항목(툴체인 종류, gcc 버전, init, /dev 관리)이 기본값으로 정해졌다는 것.

```sh
cd /var/tmp/K3/k3_out
grep -E "^BR2_(TOOLCHAIN_BUILDROOT|TOOLCHAIN_BUILDROOT_GLIBC)=|^BR2_GCC_VERSION|^BR2_BINUTILS_VERSION|^BR2_INIT_|^BR2_ROOTFS_DEVICE_CREATION|^BR2_DL_DIR|^BR2_JLEVEL|^BR2_GCC_TARGET_CPU|^BR2_TARGET_ROOTFS_EXT2_SIZE|^BR2_TARGET_GENERIC_ROOT_PASSWD|^BR2_KERNEL_HEADERS" .config
```

```text
BR2_GCC_TARGET_CPU="cortex-a53"
BR2_TOOLCHAIN_BUILDROOT=y
BR2_TOOLCHAIN_BUILDROOT_GLIBC=y
BR2_KERNEL_HEADERS_AS_KERNEL=y
BR2_BINUTILS_VERSION_2_45_X=y
BR2_BINUTILS_VERSION="2.45.1"
BR2_GCC_VERSION_15_X=y
BR2_GCC_VERSION="15.3.0"
BR2_DL_DIR="$(TOPDIR)/dl"
BR2_JLEVEL=0
BR2_INIT_BUSYBOX=y
BR2_ROOTFS_DEVICE_CREATION_DYNAMIC_DEVTMPFS=y
BR2_TARGET_GENERIC_ROOT_PASSWD=""
BR2_TARGET_ROOTFS_EXT2_SIZE="60M"
```

출력에서 볼 것: 기본은 **internal 툴체인**(Buildroot가 binutils 2.45.1 + gcc 15.3.0 + glibc를 직접 빌드), init은 **BusyBox init**(K2), `/dev`는 **devtmpfs만**(udev·mdev 없음, K1), root 비밀번호는 **빈 문자열**(개발용 — 제품에서는 반드시 바꾼다), 다운로드 캐시는 Buildroot 트리 안 `dl/`이다.

### 2.4 예제 4 — 설정 바꾸고 savedefconfig로 최소 diff 저장

무엇을 확인하나: menuconfig 대신 `utils/config`(커널 `scripts/config`의 Buildroot판)로 설정을 바꾸고, `savedefconfig`가 "기본값과 다른 줄"만 남긴다는 것.

```sh
cd /var/tmp/K3/k3_out
/var/tmp/K3/k3_buildroot/utils/config --file .config \
    -e BR2_PACKAGE_BUSYBOX_SHOW_OTHERS -e BR2_PACKAGE_I2C_TOOLS -e BR2_PACKAGE_CAN_UTILS \
    --set-str BR2_TARGET_GENERIC_HOSTNAME sensorkit
make olddefconfig >/dev/null
make savedefconfig BR2_DEFCONFIG=/var/tmp/K3/k3_saved_defconfig 2>&1 | tail -1
diff /var/tmp/K3/k3_buildroot/configs/qemu_aarch64_virt_defconfig /var/tmp/K3/k3_saved_defconfig
wc -l .config /var/tmp/K3/k3_saved_defconfig
```

```text
  GEN     /var/tmp/K3/k3_out/Makefile
4a5
> BR2_TARGET_GENERIC_HOSTNAME="sensorkit"
13a15,17
> BR2_PACKAGE_BUSYBOX_SHOW_OTHERS=y
> BR2_PACKAGE_I2C_TOOLS=y
> BR2_PACKAGE_CAN_UTILS=y
  5350 .config
    22 /var/tmp/K3/k3_saved_defconfig
  5372 total
```

출력에서 볼 것: `.config`는 43줄 늘었지만(새 패키지의 하위 옵션 기본값까지 펼쳐짐) defconfig는 딱 4줄 늘었다. **git에 커밋하는 것은 항상 savedefconfig 결과**다. `.config`를 커밋하면 버전 업그레이드 때 새 옵션 기본값을 놓치고, 리뷰 diff가 수백 줄이 된다.

함정: `BR2_DEFCONFIG`를 주지 않으면 savedefconfig는 **defconfig를 불러온 원래 파일**(`configs/qemu_aarch64_virt_defconfig`)을 덮어쓴다. 업스트림 파일을 실수로 고치지 않으려면 경로를 명시하거나, 처음부터 BR2_EXTERNAL의 자기 defconfig를 쓴다. 또 i2c-tools는 `depends on BR2_PACKAGE_BUSYBOX_SHOW_OTHERS`라서(5.2절 Config.in 참고) 그것 없이 `-e BR2_PACKAGE_I2C_TOOLS`만 하면 olddefconfig가 조용히 꺼 버린다.

### 2.5 서브 컴포넌트 설정 — 커널·BusyBox·U-Boot (실행하지 않음 — 가이드)

Buildroot는 커널·BusyBox·U-Boot의 Kconfig도 같은 방식으로 감싼다. 이 타깃들은 해당 소스를 내려받고 압축을 풀어야 하므로 실행하지 않았다.

```sh
# 실행하지 않음 — 가이드 (소스 다운로드 필요)
make linux-menuconfig          # output/build/linux-6.18.7/.config 편집
make linux-update-defconfig    # 결과를 BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE에 savedefconfig 형식으로 저장
make busybox-menuconfig        # BusyBox 애플릿 선택
make busybox-update-config     # BR2_PACKAGE_BUSYBOX_CONFIG 파일에 저장
make uboot-menuconfig          # BR2_TARGET_UBOOT=y 일 때
make uboot-update-defconfig
make savedefconfig             # Buildroot 자신의 defconfig 저장 — 이것까지 해야 한 세트
```

실무 규칙: `*-menuconfig`로 바꾼 내용은 `output/build/` 안에만 있으므로 **`*-update-defconfig`(또는 `-update-config`)를 하지 않으면 `make clean` 한 번에 사라진다.** 작은 변경은 전체 config 파일 대신 **config fragment**(`BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES`)로 관리하는 편이 업그레이드에 강하다. 6절의 `linux.fragment`가 그 예다.

---

## 3. output/ 디렉터리 — build · host · staging · target · images

### 3.1 그림으로 먼저

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">output/ — 한 패키지가 어디로 설치되나</text>
  <rect x="10" y="40" width="170" height="110" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="95" y="62" text-anchor="middle" font-size="13" font-weight="bold">build/&lt;pkg&gt;-&lt;ver&gt;/</text>
  <text x="95" y="86" text-anchor="middle" font-size="12">압축 푼 소스 + 빌드 결과</text>
  <text x="95" y="106" text-anchor="middle" font-size="12">.stamp_* 진행 기록</text>
  <text x="95" y="126" text-anchor="middle" font-size="12">dirclean = 이 폴더 삭제</text>
  <rect x="290" y="40" width="380" height="150" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="480" y="62" text-anchor="middle" font-size="13" font-weight="bold">host/ — 빌드 머신에서 도는 것</text>
  <text x="480" y="84" text-anchor="middle" font-size="12">host/bin: cross gcc, mkfs, genimage, qemu …</text>
  <rect x="305" y="98" width="350" height="80" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="480" y="120" text-anchor="middle" font-size="13" font-weight="bold">staging = host/&lt;tuple&gt;/sysroot</text>
  <text x="480" y="142" text-anchor="middle" font-size="12">헤더 · .a · .so · .pc (strip 안 함)</text>
  <text x="480" y="162" text-anchor="middle" font-size="12">다른 패키지가 컴파일·링크할 때 참조</text>
  <rect x="290" y="220" width="380" height="80" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="480" y="242" text-anchor="middle" font-size="13" font-weight="bold">target/ — 보드에 실릴 rootfs 원본</text>
  <text x="480" y="264" text-anchor="middle" font-size="12">실행 파일 · .so만, 헤더 없음, strip됨</text>
  <text x="480" y="284" text-anchor="middle" font-size="12">소유자는 빌드 사용자 (root는 fakeroot가)</text>
  <rect x="10" y="220" width="170" height="80" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="95" y="242" text-anchor="middle" font-size="13" font-weight="bold">images/</text>
  <text x="95" y="264" text-anchor="middle" font-size="12">Image · rootfs.ext4</text>
  <text x="95" y="284" text-anchor="middle" font-size="12">sdcard.img · *.dtb</text>
  <line x1="180" y1="95" x2="303" y2="130" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="186" y="86" font-size="11">INSTALL_STAGING</text>
  <line x1="150" y1="150" x2="320" y2="218" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="190" y="206" font-size="11">INSTALL_TARGET</text>
  <line x1="290" y1="260" x2="182" y2="260" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="340" y="330" text-anchor="middle" font-size="13">target/ → fakeroot + mkfs → images/rootfs.ext4 (7절)</text>
</svg>
```

그림 2 — 패키지는 `build/`에서 빌드되고, 라이브러리는 **staging**(=툴체인 sysroot, 개발용 전체 사본)에, 실행에 필요한 것만 **target**(제품용)에 설치된다. images/는 target/을 굽는 결과다.

### 3.2 예제 5 — printvars로 실제 경로 확인

무엇을 확인하나: 그림 2의 각 디렉터리가 Make 변수로 어떻게 정의되어 있는지. `printvars`는 아무것도 빌드하지 않고 변수 값만 찍는다.

```sh
cd /var/tmp/K3/k3_out
make printvars VARS="BUSYBOX_VERSION BUSYBOX_SITE BUSYBOX_SOURCE BUSYBOX_LICENSE BUSYBOX_DIR TARGET_DIR STAGING_DIR HOST_DIR BINARIES_DIR TARGET_CROSS LINUX_VERSION GNU_TARGET_NAME"
```

```text
BINARIES_DIR=/var/tmp/K3/k3_out/images
BUSYBOX_DIR=/var/tmp/K3/k3_out/build/busybox-1.38.0
BUSYBOX_LICENSE=GPL-2.0, bzip2-1.0.6
BUSYBOX_SITE=https://www.busybox.net/downloads
BUSYBOX_SOURCE=busybox-1.38.0.tar.bz2
BUSYBOX_VERSION=1.38.0
GNU_TARGET_NAME=aarch64-buildroot-linux-gnu
HOST_DIR=/var/tmp/K3/k3_out/host
LINUX_VERSION=6.18.7
STAGING_DIR=/var/tmp/K3/k3_out/host/aarch64-buildroot-linux-gnu/sysroot
TARGET_CROSS=/var/tmp/K3/k3_out/host/bin/aarch64-buildroot-linux-gnu-
TARGET_DIR=/var/tmp/K3/k3_out/target
```

출력에서 볼 것: `STAGING_DIR`이 **`host/<tuple>/sysroot`** 다. 즉 staging은 별도 개념이 아니라 **크로스 컴파일러의 sysroot 그 자체**다(B3에서 본 `--sysroot`). 패키지가 `INSTALL_STAGING = YES`로 헤더와 라이브러리를 여기 넣으면, 그 뒤에 빌드되는 패키지는 cross gcc가 자동으로 그 헤더를 찾는다. `TARGET_CROSS`는 모든 패키지에 `CC=$(TARGET_CROSS)gcc`로 넘어가는 접두어다.

### 3.3 staging vs target — 왜 둘로 나누나

| | staging (sysroot) | target |
|---|---|---|
| 목적 | 빌드할 때 컴파일·링크 대상 | 보드에 실을 rootfs |
| 헤더(`/usr/include`) | 있음 | 없음 (target-finalize가 삭제) |
| 정적 라이브러리 `.a`, `.pc` | 있음 | 없음 |
| 디버그 심볼 | 있음 (unstripped) | `BR2_STRIP_strip`이면 strip됨 |
| 누가 쓰나 | cross gcc, pkg-config, gdb(심볼) | fakeroot → mkfs |
| SSD 비유 | 링크용 `.lib` + `.h` + `.map` | 다운로드 이미지 |

이 구분이 실무에서 중요한 장면은 **원격 디버깅**이다. 보드 위 `gdbserver`는 target의 strip된 바이너리를 돌리고, 호스트 gdb는 `set sysroot output/staging`으로 **심볼이 살아 있는 staging 사본**을 읽는다(M 모듈에서 다시 나온다).

---

## 4. 툴체인 선택 — internal vs external

### 4.1 두 가지 길

| | internal (`BR2_TOOLCHAIN_BUILDROOT`) | external (`BR2_TOOLCHAIN_EXTERNAL`) |
|---|---|---|
| 동작 | binutils·gcc·C 라이브러리를 소스에서 빌드 | 미리 빌드된 툴체인 tarball을 받아 `host/`에 풀고 sysroot를 staging으로 복사 |
| C 라이브러리 | glibc / musl / uClibc-ng 자유 선택 | 툴체인이 정한 것 |
| 첫 빌드 시간 | 툴체인 빌드만큼 더 걸림 | 빠름 |
| 재현성 | 버전이 defconfig에 고정 | tarball 버전이 고정 (`.hash`로 검증) |
| 대표 선택지 | — | Bootlin toolchains, Arm GNU toolchain, 회사·SoC 벤더 SDK(`toolchain-external-custom`) |
| 언제 | musl·특수 옵션이 필요할 때, 툴체인도 소스로 감사할 때 | 대부분의 제품 — 툴체인을 팀 전체가 공유 |

실무 팁: 툴체인 설정을 바꾸면 **무조건 전체 재빌드**다(10절). 그래서 많은 팀이 "Buildroot로 툴체인만 한 번 만들어 tarball로 굳히고, 제품 빌드는 그걸 external custom으로 쓴다". B4의 crosstool-NG도 같은 목적이다.

### 4.2 예제 6 — external 툴체인으로 바꿔 보기 (함정 포함)

무엇을 확인하나: Kconfig 의존성 때문에 "고른 줄 알았던" 툴체인이 조용히 다른 것으로 바뀌는 장면, 그리고 툴체인 종류에 따라 다운로드 목록이 어떻게 달라지는지.

```sh
mkdir -p /var/tmp/K3/k3_out_ext && cp /var/tmp/K3/k3_out/.config /var/tmp/K3/k3_out_ext/.config
cd /var/tmp/K3/k3_out_ext
/var/tmp/K3/k3_buildroot/utils/config --file .config -d BR2_TOOLCHAIN_BUILDROOT -e BR2_TOOLCHAIN_EXTERNAL \
    -e BR2_TOOLCHAIN_EXTERNAL_BOOTLIN -e BR2_TOOLCHAIN_EXTERNAL_BOOTLIN_AARCH64_GLIBC_STABLE
make -C /var/tmp/K3/k3_buildroot O=/var/tmp/K3/k3_out_ext olddefconfig
grep -E "^BR2_TOOLCHAIN_(BUILDROOT|EXTERNAL)[A-Z_]*=|^BR2_TOOLCHAIN_EXTERNAL_PREFIX" .config
make printvars VARS="TARGET_CROSS STAGING_DIR"
make busybox-show-recursive-depends
make external-deps | wc -l
make external-deps | grep -E "gcc|glibc|binutils|aarch64"
```

```text
...
# configuration written to /var/tmp/K3/k3_out_ext/.config
#
BR2_TOOLCHAIN_EXTERNAL=y
BR2_TOOLCHAIN_EXTERNAL_DOWNLOAD=y
BR2_TOOLCHAIN_EXTERNAL_GLIBC=y
BR2_TOOLCHAIN_EXTERNAL_PREFIX="aarch64-none-linux-gnu"
BR2_TOOLCHAIN_EXTERNAL_BOOTLIN_ARCH_SUPPORTS=y
STAGING_DIR=/var/tmp/K3/k3_out_ext/host/aarch64-buildroot-linux-gnu/sysroot
TARGET_CROSS=/var/tmp/K3/k3_out_ext/host/bin/aarch64-none-linux-gnu-

host-skeleton skeleton skeleton-init-common skeleton-init-sysv toolchain toolchain-external toolchain-external-arm-aarch64
41
arm-gnu-toolchain-15.2.rel1-aarch64-aarch64-none-linux-gnu.tar.xz
```

출력에서 볼 것: Bootlin을 켰는데 결과는 **Arm 툴체인**(`aarch64-none-linux-gnu-`, `toolchain-external-arm-aarch64`)이다. 원인은 Bootlin 툴체인의 Kconfig에 있다.

```sh
grep -n -A4 "^config BR2_TOOLCHAIN_EXTERNAL_BOOTLIN$" toolchain/toolchain-external/toolchain-external-bootlin/Config.in
make printvars VARS="HOSTARCH"
```

```text
1:config BR2_TOOLCHAIN_EXTERNAL_BOOTLIN
2-	bool "Bootlin toolchains"
3-	depends on BR2_TOOLCHAIN_EXTERNAL_BOOTLIN_ARCH_SUPPORTS
4-	depends on BR2_HOSTARCH = "x86_64"
5-	help
HOSTARCH=aarch64
```

Bootlin 툴체인은 **x86_64 호스트용 바이너리만** 있다. 이 VM은 aarch64 호스트(M2)라 Bootlin 선택지가 사라지고, olddefconfig가 choice의 다음 후보(Arm AArch64 15.2.rel1, x86_64와 aarch64 호스트 모두 지원)를 골랐다. **Kconfig는 불가능한 선택을 에러 없이 조용히 바꾼다** — 그래서 CI에서는 savedefconfig 후 diff가 비어 있는지 확인하는 단계를 둔다. 또 비교해 보면 internal은 busybox의 재귀 의존성에 `host-gcc-initial host-gcc-final host-binutils glibc …`가 들어가지만(예제 7), external은 `toolchain-external-arm-aarch64` 하나로 끝나고 다운로드 목록에서 gcc·binutils·glibc 소스가 툴체인 tarball 하나로 바뀐다.

---

## 5. 패키지 인프라 — Config.in + .mk + .hash

### 5.1 직관 — 패키지 = 메뉴 항목 + Make 레시피 + 체크섬

Buildroot 패키지는 디렉터리 하나에 파일 세 개다.

| 파일 | 역할 | 비유 |
|---|---|---|
| `Config.in` | menuconfig 항목, 의존성(`depends on`, `select`) | 기능 플래그 정의 |
| `<pkg>.mk` | 버전·URL·라이선스·빌드/설치 명령 | 빌드 레시피 |
| `<pkg>.hash` | 소스 tarball과 라이선스 파일의 sha256 | 다운로드 이미지 CRC 검증 |

`.mk` 안의 변수는 전부 **패키지 이름을 대문자로, `-`를 `_`로 바꾼 접두어**를 쓴다(`i2c-tools` → `I2C_TOOLS_`). 마지막 줄 `$(eval $(generic-package))`가 이 변수들을 읽어 download → extract → patch → configure → build → install 규칙을 자동 생성한다.

### 5.2 예제 7 — 실제 패키지 읽기: i2c-tools

무엇을 확인하나: Don이 H 모듈에서 쓴 `i2cdetect`·`i2cget`이 Buildroot에서 어떻게 패키지화되어 있는지. (`package/i2c-tools/` 실제 파일, 파이썬 바인딩 부분은 `...`로 생략)

```sh
cd /var/tmp/K3/k3_buildroot
cat package/i2c-tools/Config.in package/i2c-tools/i2c-tools.hash
cat package/i2c-tools/i2c-tools.mk
```

```text
config BR2_PACKAGE_I2C_TOOLS
	bool "i2c-tools"
	depends on BR2_PACKAGE_BUSYBOX_SHOW_OTHERS
	help
	  Heterogeneous set of I2C tools for Linux
...
# From https://www.kernel.org/pub/software/utils/i2c-tools/sha256sums.asc
sha256  8b15f0a880ab87280c40cfd7235cfff28134bf14d5646c07518b1ff6642a2473  i2c-tools-4.4.tar.xz

# License files
sha256  ab15fd526bd8dd18a9e77ebc139656bf4d33e97fc7238cd11bf60e2b9b8666c6  COPYING
sha256  dc626520dcd53a22f727af3ee42c770e56c97a64fe3adb063799d8ab032fe551  COPYING.LGPL
sha256  adcc60d0fdf7b4e2ad22fe8a29d65c260ad3fc619b3105439b70914df5f6b5a8  README
################################################################################
#
# i2c-tools
#
################################################################################

I2C_TOOLS_VERSION = 4.4
I2C_TOOLS_SOURCE = i2c-tools-$(I2C_TOOLS_VERSION).tar.xz
I2C_TOOLS_SITE = https://www.kernel.org/pub/software/utils/i2c-tools
I2C_TOOLS_LICENSE = GPL-2.0+, GPL-2.0 (py-smbus), LGPL-2.1+ (libi2c)
I2C_TOOLS_LICENSE_FILES = COPYING COPYING.LGPL README
I2C_TOOLS_CPE_ID_VALID = YES
I2C_TOOLS_MAKE_OPTS = EXTRA=eeprog
I2C_TOOLS_INSTALL_STAGING = YES

ifeq ($(BR2_PACKAGE_PYTHON3),y)
I2C_TOOLS_DEPENDENCIES += host-python-setuptools python3
endif
...
define I2C_TOOLS_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) $(I2C_TOOLS_MAKE_OPTS) -C $(@D)
	$(I2C_TOOLS_BUILD_PYSMBUS)
endef

define I2C_TOOLS_INSTALL_TARGET_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) $(I2C_TOOLS_MAKE_OPTS) \
		DESTDIR="$(TARGET_DIR)" PREFIX=/usr -C $(@D) install
	$(I2C_TOOLS_INSTALL_PYSMBUS)
endef

define I2C_TOOLS_INSTALL_STAGING_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) $(I2C_TOOLS_MAKE_OPTS) \
		DESTDIR="$(STAGING_DIR)" PREFIX=/usr -C $(@D) install
endef

$(eval $(generic-package))
```

출력에서 볼 것: 
`INSTALL_STAGING = YES`인 이유는 `libi2c`(LGPL 라이브러리)를 다른 패키지가 링크할 수 있게 하려는 것이다. `$(@D)`는 그 패키지의 빌드 디렉터리(`build/i2c-tools-4.4`), `$(TARGET_CONFIGURE_OPTS)`는 `CC=…-gcc CFLAGS=… LDFLAGS=…` 묶음이다. 해시 파일이 **라이선스 파일의 해시까지** 들고 있다는 점도 보자 — 업스트림이 라이선스를 바꾸면 빌드가 멈춰서 법무 검토를 강제한다(K7).

### 5.3 핵심 변수 표

| 변수 | 뜻 | 예 |
|---|---|---|
| `<PKG>_VERSION` | 버전 또는 git 태그/커밋 | `4.4`, `v0.3.0` |
| `<PKG>_SITE` | 다운로드 위치 (URL, git 저장소, 로컬 경로) | `https://www.kernel.org/...` |
| `<PKG>_SOURCE` | tarball 이름 (기본 `<pkg>-<ver>.tar.gz`) | `i2c-tools-4.4.tar.xz` |
| `<PKG>_SITE_METHOD` | `wget`(기본)·`git`·`svn`·`local`·`file` … | `git`, `local` |
| `<PKG>_LICENSE` | SPDX 식 라이선스 | `GPL-2.0+` |
| `<PKG>_LICENSE_FILES` | 소스 안의 라이선스 파일 (legal-info가 복사) | `COPYING` |
| `<PKG>_DEPENDENCIES` | 먼저 빌드·설치돼야 하는 패키지 | `zlib host-pkgconf` |
| `<PKG>_INSTALL_STAGING` | staging에도 설치할지 (라이브러리면 YES) | `YES` |
| `<PKG>_INSTALL_TARGET` | target에 설치할지 (기본 YES) | `NO`(헤더 전용 라이브러리) |
| `<PKG>_CONF_OPTS` | configure/cmake/meson 인자 | `-DFOO=ON` |
| `<PKG>_BUILD_CMDS` | generic 전용: 빌드 명령 | `$(MAKE) -C $(@D)` |
| `<PKG>_INSTALL_TARGET_CMDS` | generic 전용: target 설치 명령 | `$(INSTALL) -D -m 0755 …` |
| `<PKG>_INSTALL_INIT_SYSV` | BusyBox/SysV init용 스크립트 설치 | `S90foo` |
| `<PKG>_POST_INSTALL_TARGET_HOOKS` | 단계 뒤에 끼우는 훅 | 설정 파일 패치 |

### 5.4 인프라 종류 — 빌드 시스템에 맞춰 고른다

빌드 시스템이 표준적이면 명령을 쓸 필요가 없다. 인프라가 configure·build·install을 대신 생성한다. 2026.08 트리에서 각 인프라를 쓰는 `.mk` 개수를 세어 봤다(예: `grep -l '$(eval $(cmake-package))' package/*/*.mk | wc -l`).

```text
generic: 506
autotools: 852
cmake: 367
meson: 200
python: 578
kernel-module: 30
golang: 32
cargo: 24
```

| 인프라 | 소스가 이렇게 생겼으면 | 직접 써야 하는 것 |
|---|---|---|
| `generic-package` | 손으로 만든 Makefile, 특수한 빌드 | `_BUILD_CMDS`, `_INSTALL_TARGET_CMDS` 전부 |
| `autotools-package` | `./configure && make` | 보통 `_CONF_OPTS`만 |
| `cmake-package` | `CMakeLists.txt` | `_CONF_OPTS`만 (`-DCMAKE_TOOLCHAIN_FILE`은 자동) |
| `meson-package` | `meson.build` | `_CONF_OPTS`만 (cross file 자동) |
| `python-package` | setuptools/pep517 | `_SETUP_TYPE` |
| `kernel-module` | out-of-tree `.ko` (Kbuild) | 대개 아무것도 — `generic-package`와 함께 eval |

autotools가 가장 많은 것은 전통적인 GNU 소프트웨어가 많기 때문이다. 회사 C/C++ 코드는 대부분 cmake나 generic, 드라이버는 kernel-module이다.

### 5.5 패키지 빌드 단계와 stamp 파일

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">한 패키지의 단계 — 각 단계가 끝나면 .stamp_* 파일을 만든다</text>
  <rect x="6" y="50" width="88" height="56" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="50" y="74" text-anchor="middle" font-size="12">download</text>
  <text x="50" y="94" text-anchor="middle" font-size="12">→ dl/</text>
  <rect x="104" y="50" width="88" height="56" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="148" y="74" text-anchor="middle" font-size="12">extract</text>
  <text x="148" y="94" text-anchor="middle" font-size="12">→ build/</text>
  <rect x="202" y="50" width="88" height="56" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="246" y="74" text-anchor="middle" font-size="12">patch</text>
  <text x="246" y="94" text-anchor="middle" font-size="12">*.patch</text>
  <rect x="300" y="50" width="88" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="344" y="74" text-anchor="middle" font-size="12">configure</text>
  <text x="344" y="94" text-anchor="middle" font-size="12">cmake …</text>
  <rect x="398" y="50" width="88" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="442" y="74" text-anchor="middle" font-size="12">build</text>
  <text x="442" y="94" text-anchor="middle" font-size="12">make</text>
  <rect x="496" y="50" width="88" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="540" y="74" text-anchor="middle" font-size="12">install</text>
  <text x="540" y="94" text-anchor="middle" font-size="12">staging</text>
  <rect x="594" y="50" width="80" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="634" y="74" text-anchor="middle" font-size="12">install</text>
  <text x="634" y="94" text-anchor="middle" font-size="12">target</text>
  <line x1="94" y1="78" x2="102" y2="78" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="192" y1="78" x2="200" y2="78" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="290" y1="78" x2="298" y2="78" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="388" y1="78" x2="396" y2="78" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="486" y1="78" x2="494" y2="78" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="584" y1="78" x2="592" y2="78" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="50" y="126" text-anchor="middle" font-size="11">downloaded</text>
  <text x="148" y="126" text-anchor="middle" font-size="11">extracted</text>
  <text x="246" y="126" text-anchor="middle" font-size="11">patched</text>
  <text x="344" y="126" text-anchor="middle" font-size="11">configured</text>
  <text x="442" y="126" text-anchor="middle" font-size="11">built</text>
  <text x="540" y="126" text-anchor="middle" font-size="11">staging_</text>
  <text x="540" y="140" text-anchor="middle" font-size="11">installed</text>
  <text x="634" y="126" text-anchor="middle" font-size="11">target_</text>
  <text x="634" y="140" text-anchor="middle" font-size="11">installed</text>
  <line x1="50" y1="200" x2="50" y2="150" stroke="#d0564a" marker-end="url(#ah3)"/>
  <text x="14" y="216" font-size="12">-dirclean</text>
  <text x="14" y="232" font-size="12">처음부터</text>
  <line x1="344" y1="200" x2="344" y2="150" stroke="#d0564a" marker-end="url(#ah3)"/>
  <text x="300" y="216" font-size="12">-reconfigure</text>
  <line x1="442" y1="185" x2="442" y2="150" stroke="#d0564a" marker-end="url(#ah3)"/>
  <text x="414" y="201" font-size="12">-rebuild</text>
  <line x1="540" y1="170" x2="540" y2="150" stroke="#d0564a" marker-end="url(#ah3)"/>
  <text x="510" y="186" font-size="12">-reinstall</text>
  <text x="340" y="268" text-anchor="middle" font-size="13">빨간 화살표 = 그 stamp부터 지우고 다시 시작 (rootfs 이미지는 따로 make 필요)</text>
</svg>
```

그림 3 — 패키지별 단계와 `output/build/<pkg>-<ver>/.stamp_*` 파일. `make <pkg>-rebuild`는 `.stamp_built` 이후를 지우고, `-dirclean`은 디렉터리를 통째로 지운다.

stamp 파일 이름은 `package/pkg-generic.mk`에서 그대로 확인할 수 있다.

```sh
grep -oE "\.stamp_[a-z_]+" package/pkg-generic.mk | sort -u
```

```text
.stamp_actual_downloaded
.stamp_built
.stamp_configured
.stamp_dircleaned
.stamp_downloaded
.stamp_extracted
.stamp_host_installed
.stamp_images_installed
.stamp_installed
.stamp_patched
.stamp_rsynced
.stamp_staging_installed
.stamp_target_installed
```

`.stamp_rsynced`는 11절의 `OVERRIDE_SRCDIR`(로컬 소스를 rsync로 복사)용이고, `.stamp_images_installed`는 부트로더처럼 `images/`에 설치하는 패키지용이다. SSD 펌웨어 Makefile의 `.o` 타임스탬프 비교와 달리, **Buildroot는 소스나 설정이 바뀌었는지 보지 않고 stamp가 있으면 그 단계를 건너뛴다.** 10절의 재빌드 규칙이 전부 여기서 나온다.

### 5.6 예제 8 — 패키지 메타데이터를 JSON으로 보기

무엇을 확인하나: `show-info`는 다운로드 없이 패키지의 버전·라이선스·URI·패치·CVE 정보를 JSON으로 낸다. SBOM·보안 스캔(K7)의 입력이 된다. (출력 끝부분 생략)

```sh
cd /var/tmp/K3/k3_out
make busybox-show-info | python3 -m json.tool | head -60
```

```text
{
    "busybox": {
        "type": "target",
        "name": "busybox",
        "virtual": false,
        "version": "1.38.0",
        "licenses": "GPL-2.0, bzip2-1.0.6",
        "license_files": [
            "LICENSE",
            "archival/libarchive/bz/LICENSE"
        ],
        "redistributable": true,
        "dl_dir": "busybox",
        "downloads": [
            {
                "source": "busybox-1.38.0.tar.bz2",
                "uris": [
                    "https+https://www.busybox.net/downloads",
                    "https|urlencode+https://sources.buildroot.net/busybox",
                    "https|urlencode+https://sources.buildroot.net"
                ]
            }
        ],
        "hashes": [
            "package/busybox/busybox.hash"
        ],
        "patches": [
            "package/busybox/0001-networking-libiproute-use-linux-if_packet.h-instead-.patch",
            ...
            "package/busybox/0012-netstat-sanitize-argv0-for-p-CVE-2024-58251.patch"
        ],
        "package_dir": "package/busybox",
        "stamp_dir": "build/busybox-1.38.0",
        "source_dir": "build/busybox-1.38.0",
        "build_dir": "build/busybox-1.38.0/",
        "install_target": true,
        "install_staging": false,
        "install_images": false,
        "dependencies": [
            "host-skeleton",
            "skeleton",
            "toolchain"
        ],
        "reverse_dependencies": [],
        "cpe-id": "cpe:2.3:a:busybox:busybox:1.38.0:-:*:*:*:*:*:*",
        "ignore_cves": [
            "CVE-2022-28391",
            "CVE-2023-42366",
            "CVE-2024-58251",
            "CVE-2025-46394",
            "CVE-2025-60876"
...
```

출력에서 볼 것: URI가 세 개다. 원래 사이트가 죽으면 `sources.buildroot.net`(BR2_BACKUP_SITE) 미러에서 받는다. `patches`에 CVE 수정 패치가 12개 쌓여 있고, `ignore_cves`는 "이 CVE는 위 패치로 고쳤다"는 선언이다. LTS를 쓰는 가치가 바로 이 패치들의 백포트다.

### 5.7 예제 9 — 의존성과 다운로드 목록

무엇을 확인하나: 패키지 하나가 실제로 무엇을 끌고 오는지, 그리고 이 defconfig 전체가 무엇을 내려받을지(아직 받지 않고 목록만).

```sh
cd /var/tmp/K3/k3_out
make busybox-show-depends; make linux-show-depends
make busybox-show-recursive-depends
make external-deps | head -40; make external-deps | wc -l
```

```text
host-skeleton skeleton toolchain
host-kmod host-openssl host-skeleton skeleton toolchain
gcc-final glibc host-autoconf host-automake host-binutils host-bison host-gawk host-gcc-final host-gcc-initial host-gmp host-libtool host-m4 host-mpc host-mpfr host-skeleton linux-headers skeleton skeleton-init-common skeleton-init-sysv toolchain toolchain-buildroot

Python-3.14.7.tar.xz
acl-2.4.0.tar.xz
attr-2.6.0.tar.xz
autoconf-2.73.tar.xz
...
binutils-2.45.1.tar.xz
...
busybox-1.38.0.tar.bz2
...
gcc-15.3.0.tar.xz
...
glibc-2.44-36-g2d5421ffca8893534d5e02ad38c28acd8e778fa3-git4.tar.gz
...
linux-6.18.7.tar.xz
...
openssl-3.6.4.tar.gz
...
47
```

출력에서 볼 것: busybox 직접 의존성은 `toolchain` 하나지만, 재귀로 펼치면 internal 툴체인 전체(binutils, gcc 두 단계, glibc, gmp/mpfr/mpc …)가 나온다. 이 예제는 예제 4의 변경 **전**(원본 defconfig 상태)에 실행한 것이라 47개다. 예제 4 이후(i2c-tools·can-utils 추가) 다시 세면 49개이고, 거기서 `BR2_PACKAGE_HOST_QEMU`(호스트용 QEMU를 직접 빌드)를 끄면 39개로 줄어든다. 빠지는 10개는 `qemu-11.0.0`, `glib`, `pixman`, `libslirp`, `dtc`, `pcre2`, `gettext`, `gettext-tiny`, `flex`, `distlib`이다(`diff`로 확인). Python·meson·ninja는 QEMU를 꺼도 남는다 — 다른 호스트 도구 빌드에도 쓰이기 때문이다. **오프라인 빌드 서버**를 준비할 때는 `make source`(실행하지 않음 — 전부 다운로드)로 `dl/`을 채워 둔다.

---

## 6. BR2_EXTERNAL — 회사 제품 트리

### 6.1 직관 — 본체는 건드리지 않는다

회사에서 Buildroot를 쓸 때 가장 나쁜 패턴은 Buildroot 트리를 fork해서 `configs/`, `board/`, `package/`에 직접 커밋하는 것이다. 업스트림 업그레이드 때마다 merge 지옥이 된다. **BR2_EXTERNAL**은 "Buildroot 밖의 디렉터리를 Buildroot 트리의 일부처럼 끼워 넣는" 공식 메커니즘이다. 자기 Config.in 메뉴, 패키지, defconfig, 보드 파일을 갖고, 여러 개를 콜론으로 이어 쓸 수도 있다(`BR2_EXTERNAL=/a:/b`).

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">BR2_EXTERNAL — 업스트림 트리 + 회사 트리 + 출력 디렉터리</text>
  <rect x="10" y="40" width="200" height="150" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="110" y="62" text-anchor="middle" font-size="13" font-weight="bold">buildroot/ (업스트림)</text>
  <text x="110" y="86" text-anchor="middle" font-size="12">2026.08 그대로</text>
  <text x="110" y="106" text-anchor="middle" font-size="12">package/ (2993개)</text>
  <text x="110" y="126" text-anchor="middle" font-size="12">board/qemu/ …</text>
  <text x="110" y="146" text-anchor="middle" font-size="12">회사 커밋 없음</text>
  <text x="110" y="170" text-anchor="middle" font-size="12">git submodule로 고정</text>
  <rect x="250" y="40" width="420" height="230" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="460" y="62" text-anchor="middle" font-size="13" font-weight="bold">k3_sensorkit_ext/ (회사 git)</text>
  <text x="266" y="88" font-size="12">external.desc — name: SENSORKIT</text>
  <text x="266" y="108" font-size="12">Config.in — 회사 패키지 메뉴</text>
  <text x="266" y="128" font-size="12">external.mk — package/*/*.mk include</text>
  <text x="266" y="148" font-size="12">configs/k3_sensorkit_defconfig</text>
  <text x="266" y="168" font-size="12">board/k3sensorkit/ — overlay, post-build.sh,</text>
  <text x="266" y="186" font-size="12">   post-image.sh, genimage.cfg, users.txt,</text>
  <text x="266" y="204" font-size="12">   device_table.txt, linux.fragment</text>
  <text x="266" y="224" font-size="12">package/k3-sensord · k3-tools · k3-imu-mod</text>
  <text x="266" y="244" font-size="12">src/ — 앱 소스 (보통은 별도 저장소)</text>
  <rect x="10" y="300" width="660" height="60" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="324" text-anchor="middle" font-size="13" font-weight="bold">output/ (O=, git에 넣지 않음)</text>
  <text x="340" y="346" text-anchor="middle" font-size="12">.config · .br2-external.mk (외부 트리 경로 기억) · build/ host/ target/ images/</text>
  <line x1="160" y1="190" x2="200" y2="298" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="460" y1="270" x2="460" y2="298" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="14" y="262" font-size="12">make BR2_EXTERNAL=…</text>
  <text x="14" y="280" font-size="12">k3_sensorkit_defconfig</text>
</svg>
```

그림 4 — 세 덩어리를 분리한다. 업스트림 Buildroot(고정 버전), 회사 BR2_EXTERNAL 트리(제품 정의 전부), 출력 디렉터리(버리는 것).

### 6.2 필수 파일 세 개

```text
# external.desc
name: SENSORKIT
desc: K3 SensorKit product tree (BR2_EXTERNAL example)
```

```text
# Config.in
menu "SensorKit packages"
source "$BR2_EXTERNAL_SENSORKIT_PATH/package/k3-sensord/Config.in"
source "$BR2_EXTERNAL_SENSORKIT_PATH/package/k3-tools/Config.in"
source "$BR2_EXTERNAL_SENSORKIT_PATH/package/k3-imu-mod/Config.in"
endmenu
```

```make
# external.mk
include $(sort $(wildcard $(BR2_EXTERNAL_SENSORKIT_PATH)/package/*/*.mk))
```

`name:`이 `SENSORKIT`이면 Buildroot가 `BR2_EXTERNAL_SENSORKIT_PATH`를 정의한다. defconfig와 스크립트 경로는 전부 이 변수로 쓴다 — 그래야 트리를 어디에 clone해도 동작한다.

### 6.3 제품 defconfig

QEMU 보드 defconfig를 바탕으로 회사 것을 얹었다. 아래는 6.5절의 savedefconfig 왕복 검증을 통과한 최종본이다.

```text
BR2_aarch64=y
BR2_PACKAGE_HOST_LINUX_HEADERS_CUSTOM_6_18=y
BR2_CCACHE=y
BR2_GLOBAL_PATCH_DIR="board/qemu/patches $(BR2_EXTERNAL_SENSORKIT_PATH)/patches"
BR2_TARGET_GENERIC_HOSTNAME="sensorkit"
BR2_TARGET_GENERIC_ISSUE="Welcome to K3 SensorKit"
BR2_ROOTFS_DEVICE_TABLE="system/device_table.txt $(BR2_EXTERNAL_SENSORKIT_PATH)/board/k3sensorkit/device_table.txt"
BR2_SYSTEM_DHCP="eth0"
BR2_ROOTFS_USERS_TABLES="$(BR2_EXTERNAL_SENSORKIT_PATH)/board/k3sensorkit/users.txt"
BR2_ROOTFS_OVERLAY="$(BR2_EXTERNAL_SENSORKIT_PATH)/board/k3sensorkit/overlay"
BR2_ROOTFS_POST_BUILD_SCRIPT="$(BR2_EXTERNAL_SENSORKIT_PATH)/board/k3sensorkit/post-build.sh"
BR2_ROOTFS_POST_IMAGE_SCRIPT="$(BR2_EXTERNAL_SENSORKIT_PATH)/board/k3sensorkit/post-image.sh"
BR2_LINUX_KERNEL=y
BR2_LINUX_KERNEL_CUSTOM_VERSION=y
BR2_LINUX_KERNEL_CUSTOM_VERSION_VALUE="6.18.7"
BR2_LINUX_KERNEL_USE_CUSTOM_CONFIG=y
BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE="board/qemu/aarch64-virt/linux.config"
BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES="$(BR2_EXTERNAL_SENSORKIT_PATH)/board/k3sensorkit/linux.fragment"
BR2_LINUX_KERNEL_NEEDS_HOST_OPENSSL=y
BR2_PACKAGE_BUSYBOX_SHOW_OTHERS=y
BR2_PACKAGE_I2C_TOOLS=y
BR2_PACKAGE_CAN_UTILS=y
BR2_TARGET_ROOTFS_EXT2=y
BR2_TARGET_ROOTFS_EXT2_4=y
BR2_TARGET_ROOTFS_EXT2_SIZE="128M"
# BR2_TARGET_ROOTFS_TAR is not set
BR2_PACKAGE_HOST_GENIMAGE=y
BR2_PACKAGE_HOST_QEMU=y
BR2_PACKAGE_HOST_QEMU_SYSTEM_MODE=y
BR2_PACKAGE_K3_SENSORD=y
BR2_PACKAGE_K3_TOOLS=y
BR2_PACKAGE_K3_IMU_MOD=y
```

업스트림 qemu defconfig에 있던 `BR2_DOWNLOAD_FORCE_CHECK_HASHES=y`는 뺐다. 이 옵션은 **모든 패키지에 `.hash`를 강제**하는데, git 사이트의 회사 패키지(`k3-imu-mod`)에는 아직 해시 파일이 없기 때문이다. 제품 릴리스 브랜치에서는 다시 켜고 회사 패키지에도 해시를 붙이는 것이 정석이다(git 다운로드도 Buildroot가 만든 tarball의 해시로 검증 가능하다). `linux.fragment`는 다음 다섯 줄이다.

```text
CONFIG_I2C_CHARDEV=y
CONFIG_CAN=y
CONFIG_CAN_VCAN=m
CONFIG_IKCONFIG=y
CONFIG_IKCONFIG_PROC=y
```

### 6.4 회사 패키지 세 종류

**(1) generic-package — 손으로 쓴 Makefile** (`package/k3-tools/k3-tools.mk`)

```make
################################################################################
#
# k3-tools
#
################################################################################

K3_TOOLS_VERSION = 1.0
K3_TOOLS_SITE = $(BR2_EXTERNAL_SENSORKIT_PATH)/src/k3-tools
K3_TOOLS_SITE_METHOD = local
K3_TOOLS_LICENSE = MIT
K3_TOOLS_LICENSE_FILES = LICENSE

define K3_TOOLS_BUILD_CMDS
	$(MAKE) $(TARGET_CONFIGURE_OPTS) -C $(@D)
endef

define K3_TOOLS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/k3-imu-dump $(TARGET_DIR)/usr/bin/k3-imu-dump
endef

$(eval $(generic-package))
```

소스 쪽 Makefile은 `CC`·`CFLAGS`를 하드코딩하지 않는 것이 핵심이다. Buildroot가 `$(TARGET_CONFIGURE_OPTS)`로 cross gcc를 넘겨준다.

```make
# src/k3-tools/Makefile
CFLAGS ?= -O2
CFLAGS += -Wall -Wextra

k3-imu-dump: k3-imu-dump.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f k3-imu-dump
```

**(2) cmake-package — CMake 프로젝트** (`package/k3-sensord/k3-sensord.mk`)

```make
################################################################################
#
# k3-sensord
#
################################################################################

K3_SENSORD_VERSION = 1.0
K3_SENSORD_SITE = $(BR2_EXTERNAL_SENSORKIT_PATH)/src/k3-sensord
K3_SENSORD_SITE_METHOD = local
K3_SENSORD_LICENSE = MIT
K3_SENSORD_LICENSE_FILES = LICENSE
K3_SENSORD_CONF_OPTS = -DK3_SAMPLE_HZ=100

define K3_SENSORD_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(K3_SENSORD_PKGDIR)/S90k3-sensord \
		$(TARGET_DIR)/etc/init.d/S90k3-sensord
endef

$(eval $(cmake-package))
```

빌드 명령이 한 줄도 없다. `package/pkg-cmake.mk`가 대신 만드는 명령의 핵심 줄은 이렇다.

```sh
grep -n "CMAKE_TOOLCHAIN_FILE\|CMAKE_INSTALL_PREFIX=\"/usr\"\|DESTDIR=\$\$(TARGET_DIR)" package/pkg-cmake.mk
```

```text
109:		-DCMAKE_TOOLCHAIN_FILE="$$(HOST_DIR)/share/buildroot/toolchainfile.cmake" \
110:		-DCMAKE_INSTALL_PREFIX="/usr" \
221:	$$(TARGET_MAKE_ENV) $$($$(PKG)_BUILD_ENV) DESTDIR=$$(TARGET_DIR) $$(BR2_CMAKE) --install $$($$(PKG)_BUILDDIR) $$($$(PKG)_INSTALL_TARGET_OPTS)
```

init 스크립트는 패키지 디렉터리에 두고 `_INSTALL_INIT_SYSV`로 설치했다(init으로 systemd를 고르면 `_INSTALL_INIT_SYSTEMD`가 대신 쓰인다). 스크립트는 `package/busybox/S01syslogd`의 형식(`DAEMON`, `PIDFILE`, `start()`/`stop()`, 탭 들여쓰기)을 따랐다 — 6.6절의 check-package가 이 형식을 검사한다.

```sh
#!/bin/sh

DAEMON="k3-sensord"
PIDFILE="/var/run/$DAEMON.pid"

start() {
	printf 'Starting %s: ' "$DAEMON"
	start-stop-daemon --start --background --make-pidfile \
		--pidfile "$PIDFILE" --chuid k3sensor \
		--exec "/usr/bin/$DAEMON"
	status=$?
	[ "$status" -eq 0 ] && echo "OK" || echo "FAIL"
	return "$status"
}

stop() {
	printf 'Stopping %s: ' "$DAEMON"
	start-stop-daemon --stop --pidfile "$PIDFILE" --exec "/usr/bin/$DAEMON"
	status=$?
	[ "$status" -eq 0 ] && echo "OK" || echo "FAIL"
	rm -f "$PIDFILE"
	return "$status"
}

case "$1" in
	start|stop)
		"$1";;
	restart)
		stop; start;;
	*)
		echo "Usage: $0 {start|stop|restart}"
		exit 1
esac
```

**(3) kernel-module — out-of-tree 드라이버** (`package/k3-imu-mod/k3-imu-mod.mk`)

```make
################################################################################
#
# k3-imu-mod
#
################################################################################

K3_IMU_MOD_VERSION = v0.3.0
K3_IMU_MOD_SITE = git@git.example.com:sensorkit/k3-imu.git
K3_IMU_MOD_SITE_METHOD = git
K3_IMU_MOD_LICENSE = GPL-2.0
K3_IMU_MOD_LICENSE_FILES = COPYING

define K3_IMU_MOD_LINUX_CONFIG_FIXUPS
	$(call KCONFIG_ENABLE_OPT,CONFIG_MODULES)
endef

$(eval $(kernel-module))
$(eval $(generic-package))
```

```text
# package/k3-imu-mod/Config.in
config BR2_PACKAGE_K3_IMU_MOD
	bool "k3-imu-mod"
	depends on BR2_LINUX_KERNEL
	help
	  Out-of-tree kernel driver for the SensorKit IMU
	  (k3_imu.ko, misc device /dev/k3_imu).

	  https://example.com/sensorkit/k3-imu

comment "k3-imu-mod needs a Linux kernel to be built"
	depends on !BR2_LINUX_KERNEL
```

`$(eval $(kernel-module))`를 `generic-package` **앞**에 둔다. kernel-module 인프라는 빌드·설치 단계 뒤에 훅을 붙이고, `linux`를 의존성에 자동으로 추가한다. 드라이버 소스는 E4에서 본 그대로의 Kbuild 한 줄(`obj-m := k3_imu.o`)이다. `git@git.example.com:…`은 예시 주소다 — 실제로는 회사 Git 서버이고, 11절의 `OVERRIDE_SRCDIR`로 로컬 작업 트리를 대신 쓴다. `_LINUX_CONFIG_FIXUPS`는 "이 패키지를 켜면 커널 config에 이 옵션도 켜라"는 훅으로, 실제 `package/dtbocfg/dtbocfg.mk`도 같은 방식으로 `CONFIG_OF_OVERLAY`를 켠다.

### 6.5 예제 10 — 외부 트리 로드와 왕복 검증

무엇을 확인하나: `BR2_EXTERNAL`을 한 번 주면 출력 디렉터리가 그 경로를 기억하고, 회사 패키지가 메뉴·Make 변수·의존성 그래프에 정상 등록되는지.

```sh
cd /var/tmp/K3/k3_buildroot
make O=/var/tmp/K3/k3_out_sk BR2_EXTERNAL=/Users/donh/.elinux_scratch/K3/k3_sensorkit_ext k3_sensorkit_defconfig
cd /var/tmp/K3/k3_out_sk && cat .br2-external.mk
grep -E "K3_|SENSORKIT" .config
```

```text
...
  GEN     /var/tmp/K3/k3_out_sk/Makefile
#
# configuration written to /var/tmp/K3/k3_out_sk/.config
#
#
# Automatically generated file; DO NOT EDIT.
#

BR2_EXTERNAL ?= /Users/donh/.elinux_scratch/K3/k3_sensorkit_ext
export BR2_EXTERNAL_NAMES = 
BR2_EXTERNAL_DIRS = 
BR2_EXTERNAL_MKS = 

BR2_EXTERNAL_NAMES += SENSORKIT
BR2_EXTERNAL_DIRS += /Users/donh/.elinux_scratch/K3/k3_sensorkit_ext
BR2_EXTERNAL_MKS += /Users/donh/.elinux_scratch/K3/k3_sensorkit_ext/external.mk
export BR2_EXTERNAL_SENSORKIT_PATH = /Users/donh/.elinux_scratch/K3/k3_sensorkit_ext
export BR2_EXTERNAL_SENSORKIT_DESC = K3 SensorKit product tree (BR2_EXTERNAL example)
export BR2_EXTERNAL_SENSORKIT_VERSION = 
BR2_EXTERNAL_NAMES="SENSORKIT"
BR2_EXTERNAL_SENSORKIT_PATH="/Users/donh/.elinux_scratch/K3/k3_sensorkit_ext"
BR2_EXTERNAL_SENSORKIT_VERSION=""
BR2_GLOBAL_PATCH_DIR="board/qemu/patches $(BR2_EXTERNAL_SENSORKIT_PATH)/patches"
...
# BR2_TARGET_TI_K3_BOOT_FIRMWARE is not set
# BR2_TARGET_TI_K3_R5_LOADER is not set
# BR2_TARGET_TI_K3_IMAGE_GEN is not set
# BR2_TARGET_UBOOT_NEEDS_TI_K3_DM is not set
BR2_PACKAGE_K3_SENSORD=y
BR2_PACKAGE_K3_TOOLS=y
BR2_PACKAGE_K3_IMU_MOD=y
```

출력에서 볼 것: `.br2-external.mk`가 외부 트리 경로를 기억하므로 이후에는 `BR2_EXTERNAL=`을 다시 줄 필요가 없다. 그리고 재미있는 사고 하나 — `grep K3_`에 **TI K3** 옵션(`BR2_TARGET_TI_K3_*`)이 같이 걸렸다. TI AM62/AM64 계열 SoC 플랫폼 이름이 "K3"다. 이 노트는 노트 ID 접두어 규칙 때문에 `k3`를 썼지만, 실제 제품에서는 **SoC·업스트림 이름과 겹치지 않는 회사 접두어**(`ACME_`, `SKIT_` 등)를 고르는 편이 grep·로그 분석에서 덜 헷갈린다. 또 이 외부 트리는 VM에서 **읽기 전용 마운트**(Mac 홈)인데도 문제가 없었다 — Buildroot는 BR2_EXTERNAL 트리에 아무것도 쓰지 않고 전부 `O=` 아래에 쓴다.

다음은 printvars와 의존성, 그리고 defconfig 왕복(불러오기 → savedefconfig → diff) 검증이다.

```sh
make printvars VARS="K3_SENSORD_SITE K3_SENSORD_SITE_METHOD K3_SENSORD_DIR K3_SENSORD_CONF_OPTS K3_IMU_MOD_DEPENDENCIES K3_TOOLS_PKGDIR"
make k3-imu-mod-show-depends k3-sensord-show-depends k3-tools-show-depends
make savedefconfig BR2_DEFCONFIG=/var/tmp/K3/k3_sk_saved2 >/dev/null 2>&1
diff /Users/donh/.elinux_scratch/K3/k3_sensorkit_ext/configs/k3_sensorkit_defconfig /var/tmp/K3/k3_sk_saved2 && echo "round-trip: identical"
```

```text
K3_IMU_MOD_DEPENDENCIES=linux  skeleton toolchain host-ccache host-skeleton
K3_SENSORD_CONF_OPTS=-DK3_SAMPLE_HZ=100
K3_SENSORD_DIR=/var/tmp/K3/k3_out_sk/build/k3-sensord-1.0
K3_SENSORD_SITE=/Users/donh/.elinux_scratch/K3/k3_sensorkit_ext/src/k3-sensord
K3_SENSORD_SITE_METHOD=local
K3_TOOLS_PKGDIR=/Users/donh/.elinux_scratch/K3/k3_sensorkit_ext/package/k3-tools
host-ccache host-skeleton linux skeleton toolchain
host-ccache host-pkgconf host-skeleton skeleton toolchain
host-ccache host-skeleton skeleton toolchain
round-trip: identical
```

출력에서 볼 것: `.mk`에 의존성을 적지 않았는데 `k3-imu-mod`에는 `linux`가(kernel-module 인프라가 추가), `k3-sensord`에는 `host-pkgconf`가(cmake 인프라가 추가) 들어 있다. `host-ccache`는 `BR2_CCACHE=y` 때문이다. `host-cmake`가 없는 것은 VM에 쓸 만한 cmake가 이미 있어서 Buildroot가 시스템 cmake를 쓰기로 했기 때문이다.

처음 손으로 쓴 defconfig는 왕복 diff에서 **줄 순서만** 달랐다(Buildroot 메뉴 순서대로 재배열됨). 위의 최종본은 savedefconfig 결과로 교체한 것이다. 규칙: **손으로 쓴 defconfig도 한 번 불러와서 savedefconfig로 정규화한 뒤 커밋한다.**

### 6.6 예제 11 — check-package로 패키지 린트

무엇을 확인하나: 업스트림이 패치 리뷰에 쓰는 린터가 회사 패키지에도 그대로 쓸 수 있다는 것. flake8·python-magic이 필요해서 `/var/tmp/K3/k3_venv`에 venv를 만들어 pip로 설치했다. (shellcheck는 VM에 없어 설치하지 않았다.)

```sh
cd /Users/donh/.elinux_scratch/K3/k3_sensorkit_ext
/var/tmp/K3/k3_venv/bin/python3 /var/tmp/K3/k3_buildroot/utils/check-package --br2-external package/*/* board/k3sensorkit/*.sh
```

처음 버전의 init 스크립트(스페이스 들여쓰기, `DAEMON` 변수 없음)에 대한 결과:

```text
package/k3-sensord/S90k3-sensord:3: should be indented with tabs (https://nightly.buildroot.org/#adding-packages-start-script)
package/k3-sensord/S90k3-sensord:4: should be indented with tabs (https://nightly.buildroot.org/#adding-packages-start-script)
package/k3-sensord/S90k3-sensord:0: DAEMON variable not defined (https://nightly.buildroot.org/#adding-packages-start-script)
package/k3-sensord/S90k3-sensord:0: failed to call 'shellcheck'
board/k3sensorkit/post-build.sh:0: failed to call 'shellcheck'
board/k3sensorkit/post-image.sh:0: failed to call 'shellcheck'
120 lines processed
6 warnings generated
```

6.4절 형식으로 고친 뒤:

```text
package/k3-sensord/S90k3-sensord:0: failed to call 'shellcheck'
board/k3sensorkit/post-build.sh:0: failed to call 'shellcheck'
board/k3sensorkit/post-image.sh:0: failed to call 'shellcheck'
148 lines processed
3 warnings generated
```

남은 3개는 shellcheck가 없어서 생긴 것뿐이고, `.mk`와 `Config.in`은 처음부터 통과했다. 일부러 틀린 `.mk`를 하나 만들어 무엇을 잡는지도 봤다(`/var/tmp/K3/k3_bad/package/k3-bad/k3-bad.mk`: 헤더 주석 없음, `K3BAD_DEPENDENCIES` 오타, `K3_BAD_LICENCE` 철자, 스페이스 들여쓰기).

```text
package/k3-bad/k3-bad.mk:1: should be 80 hashes (https://nightly.buildroot.org/#writing-rules-mk)
package/k3-bad/k3-bad.mk:2: should be 1 hash (https://nightly.buildroot.org/#writing-rules-mk)
package/k3-bad/k3-bad.mk:4: should be 1 hash (https://nightly.buildroot.org/#writing-rules-mk)
package/k3-bad/k3-bad.mk:5: should be 80 hashes (https://nightly.buildroot.org/#writing-rules-mk)
package/k3-bad/k3-bad.mk:5: possible typo, variable not properly prefixed: K3BAD_DEPENDENCIES -> *K3_BAD_XXXX* (https://nightly.buildroot.org/#_tips_and_tricks)
package/k3-bad/k3-bad.mk:8: expected indent with tabs
11 lines processed
6 warnings generated
```

출력에서 볼 것: 접두어 오타(`K3BAD_`)는 잡지만, 접두어가 맞는 철자 오류(`LICENCE`)는 **못 잡는다.** 그런 변수는 Make가 그냥 무시하므로 라이선스가 "unknown"으로 남는다. `make <pkg>-show-info`의 `licenses` 필드나 `legal-info` 결과로 한 번 더 확인해야 한다.

---

## 7. rootfs 다듬기 — overlay · post-build · fakeroot · post-image

### 7.1 순서가 전부다

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">패키지 설치가 끝난 뒤 — 사용자 훅이 끼어드는 자리</text>
  <rect x="20" y="40" width="300" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="65" text-anchor="middle" font-size="13">모든 패키지 → target/ 설치 완료</text>
  <rect x="20" y="100" width="300" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="125" text-anchor="middle" font-size="13">target-finalize: 헤더·.a 삭제, strip</text>
  <rect x="20" y="160" width="300" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="185" text-anchor="middle" font-size="13">① rootfs overlay를 rsync로 덮어쓰기</text>
  <rect x="20" y="220" width="300" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="245" text-anchor="middle" font-size="13">② post-build.sh $(TARGET_DIR)</text>
  <rect x="360" y="40" width="300" height="220" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="64" text-anchor="middle" font-size="13" font-weight="bold">③ fakeroot 안 (이미지 형식마다)</text>
  <text x="380" y="92" font-size="12">target/ 사본에 대해</text>
  <text x="380" y="116" font-size="12">chown -R 0:0 (전부 root 소유로)</text>
  <text x="380" y="140" font-size="12">mkusers: users 테이블 → passwd</text>
  <text x="380" y="164" font-size="12">makedevs: device 테이블 → 권한</text>
  <text x="380" y="188" font-size="12">post-fakeroot 스크립트</text>
  <text x="380" y="212" font-size="12">mkfs.ext4 / mksquashfs …</text>
  <text x="380" y="240" font-size="12">→ images/rootfs.ext4</text>
  <rect x="360" y="290" width="300" height="60" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="314" text-anchor="middle" font-size="13">④ post-image.sh $(BINARIES_DIR)</text>
  <text x="510" y="336" text-anchor="middle" font-size="12">genimage → sdcard.img, 서명, 패키징</text>
  <line x1="170" y1="80" x2="170" y2="98" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="170" y1="140" x2="170" y2="158" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="170" y1="200" x2="170" y2="218" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="320" y1="240" x2="358" y2="200" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="260" x2="510" y2="288" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="20" y="300" font-size="12">①② 는 진짜 target/을 바꾼다</text>
  <text x="20" y="320" font-size="12">③ 은 사본에서 돈다 (재빌드에도 안전)</text>
  <text x="20" y="340" font-size="12">④ 는 images/만 다룬다</text>
</svg>
```

그림 5 — 최상위 `Makefile`의 `target-finalize`와 `fs/common.mk`의 fakeroot 스크립트 생성 부분을 읽고 그린 순서. 이 순서를 알면 "어디에 무엇을 넣어야 하나"가 바로 나온다.

| 수단 | 언제 쓰나 | 예 | 주의 |
|---|---|---|---|
| rootfs overlay (`BR2_ROOTFS_OVERLAY`) | **정적 파일**을 그대로 얹을 때 | `/etc/k3-sensord.conf`, `/etc/network/interfaces` | 같은 경로의 패키지 파일을 덮어쓴다. 권한은 소스 파일 권한 그대로 |
| post-build 스크립트 | **생성·수정·삭제**가 필요할 때, 빌드 정보 각인 | 빌드 ID 스탬프, 불필요한 init 스크립트 삭제 | 인자 `$1`=TARGET_DIR. 여기서 chown은 의미 없음(fakeroot 전) |
| users 테이블 (`BR2_ROOTFS_USERS_TABLES`) | 데몬용 시스템 계정 | `k3sensor` | uid `-1`이면 Buildroot가 100~999에서 자동 할당 |
| device(권한) 테이블 (`BR2_ROOTFS_DEVICE_TABLE`) | **소유자·권한**을 정할 때 | `/var/lib/k3`를 0750 k3sensor로 | devtmpfs 환경에서 `/dev` 노드는 만들지 않는다 |
| post-fakeroot 스크립트 | root 소유권이 보이는 상태에서 마지막 손질 | setcap, 특수 권한 확인 | 이미지 형식마다 따로 실행 |
| post-image 스크립트 | 디스크 이미지·서명·업데이트 번들 | genimage, SWUpdate 번들(L 모듈) | 인자 `$1`=BINARIES_DIR, CWD는 Buildroot 최상위 |

회사 보드의 post-build 스크립트는 다음과 같다(파일 그대로).

```sh
#!/bin/sh
# post-build: $1 = TARGET_DIR. Runs after packages + overlay, before fakeroot/images.
# Env from Buildroot: BR2_CONFIG HOST_DIR STAGING_DIR TARGET_DIR BUILD_DIR BINARIES_DIR BASE_DIR
set -eu
TARGET="$1"
BUILD_ID="${K3_BUILD_ID:-dev}"

# 1) build stamp for field debugging (CI exports K3_BUILD_ID=git describe)
cat > "$TARGET/etc/k3-build-info" <<STAMP
K3_BUILD_ID=$BUILD_ID
DEFCONFIG=${2:-unknown}
STAMP

# 2) load the IMU driver early (BusyBox init runs /etc/init.d/S??* in order)
mkdir -p "$TARGET/etc/init.d"
cat > "$TARGET/etc/init.d/S05k3-modules" <<'INIT'
#!/bin/sh
case "$1" in
  start) modprobe k3_imu && echo "k3_imu loaded" ;;
  stop)  modprobe -r k3_imu ;;
esac
INIT
chmod 0755 "$TARGET/etc/init.d/S05k3-modules"

# 3) product hardening: no telnetd, no empty-password root login over serial
rm -f "$TARGET/etc/init.d/S50telnet"
```

users 테이블과 device 테이블(필드 순서는 `docs/manual/makeusers-syntax.adoc`, `makedev-syntax.adoc`):

```text
# users.txt : username uid group gid password home shell groups comment
k3sensor -1 k3sensor -1 * /var/lib/k3 - dialout K3 sensor daemon
```

```text
# device_table.txt
# name             type mode uid      gid      major minor start inc count
/var/lib/k3        d    750  k3sensor k3sensor -     -     -     -   -
/etc/k3-sensord.conf f  640  root     k3sensor -     -     -     -   -
/usr/bin/k3-imu-dump f  755  root     root     -     -     -     -   -
```

비밀번호 `*`는 "로그인 불가", shell `-`는 "셸 없음"이다. 데몬 계정은 이렇게 만든다.

### 7.2 예제 12 — 회사 앱이 실제로 컴파일되는지 먼저 확인

무엇을 확인하나: 패키지 레시피를 쓰기 전에, 소스가 경고 없이 빌드되고 CMake install 규칙이 `DESTDIR` 아래 `/usr/bin`에 설치하는지. Buildroot의 cmake 인프라가 하는 일(toolchain file만 빼고)과 같다. VM 네이티브 gcc라 결과물은 aarch64이지만 Buildroot sysroot와 링크된 것은 아니다.

```sh
E=/Users/donh/.elinux_scratch/K3/k3_sensorkit_ext
mkdir -p /var/tmp/K3/k3_native && cd /var/tmp/K3/k3_native
cmake -S $E/src/k3-sensord -B sensord -DK3_SAMPLE_HZ=100 -DCMAKE_INSTALL_PREFIX=/usr -G Ninja | tail -3
cmake --build sensord | tail -2
DESTDIR=/var/tmp/K3/k3_native/target cmake --install sensord
cp -r $E/src/k3-tools tools && make -C tools && find target -type f
```

```text
-- Configuring done (0.1s)
-- Generating done (0.0s)
-- Build files have been written to: /var/tmp/K3/k3_native/sensord
[1/2] Building C object CMakeFiles/k3-sensord.dir/main.c.o
[2/2] Linking C executable k3-sensord
-- Install configuration: ""
-- Installing: /var/tmp/K3/k3_native/target/usr/bin/k3-sensord
make: Entering directory '/var/tmp/K3/k3_native/tools'
cc -O2 -Wall -Wextra  -o k3-imu-dump k3-imu-dump.c
make: Leaving directory '/var/tmp/K3/k3_native/tools'
target/usr/bin/k3-sensord
```

출력에서 볼 것: 경고 0개. `DESTDIR` + `CMAKE_INSTALL_PREFIX=/usr` 조합이 `target/usr/bin/k3-sensord`를 만든다 — Buildroot가 `DESTDIR=$(TARGET_DIR)`로 하는 일과 똑같다.

앱 소스(`src/k3-sensord/main.c`)는 다음과 같다.

```c
/* k3-sensord: read config, poll the IMU at K3_SAMPLE_HZ, print samples */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *conf = argc > 1 ? argv[1] : "/etc/k3-sensord.conf";
	char dev[128] = "/dev/k3_imu", line[160], buf[64];
	int samples = 5;
	FILE *cf = fopen(conf, "r");

	if (cf) {
		while (fgets(line, sizeof(line), cf)) {
			if (sscanf(line, "device=%127s", dev) == 1) continue;
			sscanf(line, "samples=%d", &samples);
		}
		fclose(cf);
	}
	printf("k3-sensord: dev=%s samples=%d rate=%dHz\n", dev, samples, K3_SAMPLE_HZ);
	int fd = open(dev, O_RDONLY);
	if (fd < 0) { perror(dev); exit(1); }
	struct timespec ts = { 0, 1000000000L / K3_SAMPLE_HZ };
	for (int i = 0; i < samples; i++) {
		ssize_t r = read(fd, buf, sizeof(buf) - 1);
		if (r < 0) { perror("read"); exit(1); }
		buf[r] = '\0';
		fputs(buf, stdout);
		nanosleep(&ts, NULL);
	}
	close(fd);
	return 0;
}
```

### 7.3 예제 13 — Buildroot 마지막 단계를 손으로 재현하기

무엇을 확인하나: 그림 5의 ①~③을 Buildroot가 쓰는 **바로 그 스크립트**(`support/scripts/mkusers`, `package/makedevs/makedevs.c`)로 돌려서, 일반 사용자 권한으로도 root 소유 파일이 든 ext4를 만들 수 있다는 것(fakeroot의 원리). 전체 빌드 대신 랩의 BusyBox rootfs(`/var/lab/build/rootfs`)를 "패키지 설치가 끝난 target/"의 대역으로 썼다. 회사 앱은 랩 rootfs에 libc가 없어서 `-static`으로 빌드했다.

```sh
#!/bin/bash
# Buildroot's last stages by hand: target/ -> overlay -> post-build -> fakeroot{mkusers,makedevs,mkfs}
set -euo pipefail
BR=/var/tmp/K3/k3_buildroot  EXT=/Users/donh/.elinux_scratch/K3/k3_sensorkit_ext
W=/var/tmp/K3/k3_mini        T=/var/tmp/K3/k3_mini/target   KB=/var/lab/out/linux-build
mkdir -p "$W/host/bin" "$T"

# [packages] skeleton + BusyBox (lab rootfs stands in for output/target after "make")
cp -a /var/lab/build/rootfs/. "$T/"
cp "$BR"/system/skeleton/etc/{passwd,group,shadow} "$T/etc/"
# [k3-tools, k3-sensord] static here because the lab rootfs has no libc
gcc -O2 -Wall -Wextra -static -o "$T/usr/bin/k3-imu-dump" "$EXT/src/k3-tools/k3-imu-dump.c"
gcc -O2 -Wall -Wextra -static -DK3_SAMPLE_HZ=100 -o "$T/usr/bin/k3-sensord" "$EXT/src/k3-sensord/main.c"
install -D -m 0755 "$EXT/package/k3-sensord/S90k3-sensord" "$T/etc/init.d/S90k3-sensord"
# [k3-imu-mod] what the kernel-module infra does: make -C $(LINUX_DIR) M=... modules{,_install}
cp -r /Users/donh/.elinux_scratch/K3/src/k3-imu "$W/k3-imu"
make -s -C "$KB" M="$W/k3-imu" modules
mkdir -p "$T/lib/modules/6.18.55" && cp "$KB"/modules.builtin* "$KB"/modules.order "$T/lib/modules/6.18.55/"
make -s -C "$KB" M="$W/k3-imu" INSTALL_MOD_PATH="$T" modules_install

# [target-finalize] overlay, then post-build script (arg1 = TARGET_DIR)
rsync -a "$EXT/board/k3sensorkit/overlay/" "$T/"
BR2_CONFIG=/var/tmp/K3/k3_out_sk/.config TARGET_DIR="$T" K3_BUILD_ID=k3-demo-0001 \
  "$EXT/board/k3sensorkit/post-build.sh" "$T" k3_sensorkit_defconfig

# [fakeroot] host-makedevs is a single C file in package/makedevs
gcc -O2 -o "$W/host/bin/makedevs" "$BR/package/makedevs/makedevs.c"
BR2_CONFIG=/var/tmp/K3/k3_out_sk/.config "$BR/support/scripts/mkusers" \
  "$EXT/board/k3sensorkit/users.txt" "$T" > "$W/mkusers.cmds"
cat > "$W/fakeroot.sh" <<FR
set -e
chown -h -R 0:0 $T
. $W/mkusers.cmds
$W/host/bin/makedevs -d $BR/system/device_table.txt $T
$W/host/bin/makedevs -d $EXT/board/k3sensorkit/device_table.txt $T
mkfs.ext4 -q -d $T -L rootfs -F $W/rootfs.ext4 64M
FR
fakeroot -- sh "$W/fakeroot.sh"
echo "== mkusers emitted:"; cat "$W/mkusers.cmds"
echo "== real owner of target/ on disk: $(stat -c %U:%G "$T/var/lib/k3")"
ls -l "$W/rootfs.ext4"
```

```sh
limactl shell elinux -- bash /Users/donh/.elinux_scratch/K3/k3_assemble.sh
```

```text
rootdir=/var/tmp/K3/k3_mini/target
table='/var/tmp/K3/k3_buildroot/system/device_table.txt'
rootdir=/var/tmp/K3/k3_mini/target
table='/Users/donh/.elinux_scratch/K3/k3_sensorkit_ext/board/k3sensorkit/device_table.txt'
== mkusers emitted:
chown -h -R 100:101 '/var/tmp/K3/k3_mini/target//var/lib/k3'
== real owner of target/ on disk: donh:donh
-rw-rw-r-- 1 donh donh 67108864 Oct 17 17:47 /var/tmp/K3/k3_mini/rootfs.ext4
```

출력에서 볼 것: `mkusers`는 passwd/group을 직접 고치고, **소유권 변경은 명령으로 출력**한다(`chown -h -R 100:101 …`). Buildroot는 이 출력을 fakeroot 스크립트에 이어 붙인다(`fs/common.mk` 179행이 정확히 `mkusers … >> $(FAKEROOT_SCRIPT)`). 디스크 위 실제 파일은 여전히 `donh:donh` 소유인데, 이미지 안에서는 어떨까.

```sh
cd /var/tmp/K3/k3_mini
find target/lib/modules -name "*.ko*"; grep k3 target/etc/passwd target/etc/group
for f in /var/lib/k3 /etc/k3-sensord.conf /usr/bin/k3-imu-dump /etc/shadow /tmp; do
  /usr/sbin/debugfs -R "stat $f" rootfs.ext4 2>/dev/null | grep -m2 -E "^Inode|User:" | tr "\n" " "; echo " <- $f"
done
```

```text
target/lib/modules/6.18.55/updates/k3_imu.ko
target/etc/passwd:k3sensor:x:100:101:K3 sensor daemon:/var/lib/k3:/bin/false
target/etc/group:k3sensor:x:101:
target/etc/group:dialout:x:18:k3sensor
Inode: 465   Type: directory    Mode:  0750   Flags: 0x80000 User:   100   Group:   101   Project:     0   Size: 4096  <- /var/lib/k3
Inode: 114   Type: regular    Mode:  0640   Flags: 0x80000 User:     0   Group:   101   Project:     0   Size: 74  <- /etc/k3-sensord.conf
Inode: 281   Type: regular    Mode:  0755   Flags: 0x80000 User:     0   Group:     0   Project:     0   Size: 715784  <- /usr/bin/k3-imu-dump
Inode: 121   Type: regular    Mode:  0600   Flags: 0x80000 User:     0   Group:     0   Project:     0   Size: 153  <- /etc/shadow
Inode: 219   Type: directory    Mode:  01777   Flags: 0x80000 User:     0   Group:     0   Project:     0   Size: 4096  <- /tmp
```

출력에서 볼 것: ext4 안에서는 `/var/lib/k3`가 100:101 0750, 설정 파일이 root:k3sensor 0640, `/etc/shadow`가 0600, `/tmp`가 1777이다. fakeroot는 `LD_PRELOAD`로 `chown`·`stat`을 가로채 "가짜 소유권 DB"를 들고 있다가, 같은 세션 안의 `mkfs.ext4 -d`가 `stat`할 때 그 가짜 값을 돌려준다. 그래서 **root 권한 없이 root 소유 rootfs 이미지**가 나온다. 모듈은 `/lib/modules/6.18.55/updates/`에 설치됐다 — 6.18의 `modules_install`은 외부 모듈(`M=`)을 기본적으로 `updates/` 아래에 넣는다.

### 7.4 예제 14 — 만든 ext4를 QEMU로 부팅

무엇을 확인하나: 손으로 조립한 rootfs가 진짜 블록 디바이스 루트로 부팅되고, overlay·post-build·users·device 테이블·커널 모듈이 전부 반영됐는지. 커널은 랩 `out/Image`(virtio-blk·ext4 내장), 명령 형식은 Buildroot `board/qemu/aarch64-virt/readme.txt`와 같은 `-drive … -device virtio-blk-device` 조합이다. 랩 `/init`(proc·sys·devtmpfs 마운트 후 `LAB_CMD` 실행)을 `init=/init`으로 지정했다. ANSI 색 코드는 `sed`로 지웠다.

```sh
cd /var/tmp/K3/k3_mini && cp rootfs.ext4 boot.ext4
timeout 90 qemu-system-aarch64 -M virt -cpu cortex-a57 -m 512M -nographic -smp 1 \
  -kernel /var/lab/out/Image \
  -drive file=boot.ext4,if=none,format=raw,id=hd0 -device virtio-blk-device,drive=hd0 \
  -append "rootwait root=/dev/vda rw console=ttyAMA0 quiet init=/init LAB_CMD=grep~/dev/root~/proc/mounts;cat~/etc/k3-build-info;/etc/init.d/S05k3-modules~start;ls~-l~/dev/k3_imu;k3-imu-dump~/dev/k3_imu~3;ls~-ln~/var/lib;su~-s~/usr/bin/id~k3sensor;su~-s~/usr/bin/k3-sensord~k3sensor" \
  2>&1 | sed "s/\x1b\[[0-9;]*m//g" | tail -25
```

```text
lab initramfs: 6.18.55 up 0.70s
/dev/root / ext4 rw,relatime 0 0
K3_BUILD_ID=k3-demo-0001
DEFCONFIG=k3_sensorkit_defconfig
k3_imu loaded
cr--r--r--    1 root     root       10, 258 Oct 18 00:48 /dev/k3_imu
seq=1 ax=3 ay=-2 az=1000
seq=2 ax=6 ay=-2 az=1000
seq=3 ax=9 ay=-2 az=1000
total 4
drwxr-x---    2 100      101           4096 Oct 18 00:47 k3
uid=100(k3sensor) gid=101(k3sensor) groups=18(dialout),101(k3sensor)
k3-sensord: dev=/dev/k3_imu samples=3 rate=100Hz
seq=4 ax=12 ay=-2 az=1000
seq=5 ax=15 ay=-2 az=1000
seq=6 ax=18 ay=-2 az=1000
[    0.986857] reboot: Power down
```

출력에서 볼 것: 첫 줄의 "lab initramfs"는 랩 `/init`의 고정 문구일 뿐이고, 실제 루트는 `/dev/root … ext4`(= `/dev/vda`)다. post-build가 각인한 빌드 ID, post-build가 만든 `S05k3-modules`의 `modprobe k3_imu`(depmod가 만든 `modules.dep` 덕분에 이름만으로 로드), misc 드라이버가 만든 `/dev/k3_imu`(major 10, mode 0444 — 드라이버의 `.mode` 필드), users 테이블의 `k3sensor` 계정, overlay의 `/etc/k3-sensord.conf`(`samples=3`)까지 전부 보인다. Buildroot가 `make` 한 번으로 하는 일을 단계별로 쪼개 본 것이다. 시각은 게스트 RTC(UTC) 기준이라 호스트 시각과 다르다.

---

## 8. genimage — SD 카드·eMMC 디스크 이미지

rootfs.ext4는 파티션 하나의 내용일 뿐이다. 보드는 **파티션 테이블 + 부트로더 raw 영역 + boot 파티션 + rootfs** 로 된 디스크 이미지가 필요하다(C2, J1). Buildroot는 이것을 **genimage**(Pengutronix 도구, `host-genimage` 패키지)와 `support/scripts/genimage.sh` 래퍼로 만든다. 실제 i.MX8MP 보드(`board/polyhex/debix-model-a/genimage.cfg.in`)의 설정이 좋은 예다.

```text
image sdcard.img {
	hdimage {
		partition-table-type = "gpt"
	}

	partition imx-boot {
		in-partition-table = "no"
		image = "flash.bin"
		offset = 32K
	}

	partition rootfs {
		offset = 8M
		image = "rootfs.ext4"
		partition-uuid = %PARTUUID%
	}
}
```

읽는 법: i.MX8M BootROM은 SD 카드 32 KiB 오프셋에서 부트 이미지(`flash.bin` = SPL + DDR 펌웨어 + TF-A + U-Boot)를 찾는다(C2). 그래서 `imx-boot`는 **파티션 테이블에 넣지 않고**(`in-partition-table = "no"`) raw로 32K에 쓰고, rootfs 파티션은 8M부터 시작한다. `%PARTUUID%`는 post-image 스크립트가 sed로 채워 커널 커맨드라인 `root=PARTUUID=…`와 맞춘다.

K3 센서 키트 보드의 post-image와 genimage 설정은 다음과 같다(실행하지 않음 — genimage가 VM에 없고, `host-genimage`는 전체 빌드 때 만들어진다).

```sh
#!/bin/sh
# post-image: $1 = BINARIES_DIR. CWD is the Buildroot top dir.
set -eu
BOARD_DIR="$(dirname "$0")"
support/scripts/genimage.sh -c "$BOARD_DIR/genimage.cfg"
```

```text
image sdcard.img {
	hdimage {
		partition-table-type = "gpt"
	}

	partition rootfs {
		offset = 8M
		partition-type-uuid = root-arm64
		image = "rootfs.ext4"
	}
}
```

`root-arm64`는 Discoverable Partitions 규격의 arm64 루트 파티션 타입 별칭이다(2026.08 트리의 board/ 아래 genimage 설정 3곳에서 쓰인다). 실제 제품에서는 여기에 A/B rootfs 두 개, `/data` 파티션, U-Boot 환경 영역이 붙는다 — OTA(L 모듈)의 전제다. `genimage.sh`는 `--inputpath`와 `--outputpath`를 둘 다 `$(BINARIES_DIR)`로 주므로, 입력(`rootfs.ext4`)과 출력(`sdcard.img`)이 모두 `output/images/`에 있다.

---

## 9. 빌드와 QEMU 부팅 (실행하지 않음 — 가이드)

Don이 나중에 직접 돌릴 순서다. 명령은 `board/qemu/aarch64-virt/readme.txt`와 매뉴얼 기준이다. 디스크는 넉넉히(수십 GB) 두고, `/tmp`(VM은 2 GB tmpfs)가 아닌 `/var/tmp`에서 한다.

### 9.1 업스트림 QEMU 보드 그대로

```sh
# 실행하지 않음 — 가이드
cd /var/tmp/K3/k3_buildroot
make O=/var/tmp/K3/k3_out qemu_aarch64_virt_defconfig
cd /var/tmp/K3/k3_out
export BR2_DL_DIR=/var/tmp/br-dl          # 다운로드 캐시를 트리 밖에 (14절)
make source                                # (선택) 47개 tarball만 먼저 받기
make 2>&1 | tee build.log                  # 툴체인 → 패키지 → 커널 → 이미지. -j는 주지 않는다
ls -l images/
images/start-qemu.sh                        # post-image.sh가 readme의 명령으로 만들어 둔 스크립트
```

`make`에 `-jN`을 주지 않는 이유: 최상위 병렬화는 기본으로 지원하지 않고(`BR2_PER_PACKAGE_DIRECTORIES`가 experimental), 각 패키지 내부는 `BR2_JLEVEL=0` → `PARALLEL_JOBS`(이 VM에서 printvars로 확인한 값 7, nproc 6 + 1)로 이미 병렬이다.

기대 산출물(소스·문서 기준 — 실제 파일 목록은 Don이 빌드 후 확인):

| 파일 | 출처 |
|---|---|
| `images/Image` | `linux/linux.mk` — arm64 커널 이미지 |
| `images/rootfs.ext2` + `rootfs.ext4` 심볼릭 링크 | `fs/ext2/ext2.mk`의 `ROOTFS_EXT2_SYMLINK` (`ln -sf rootfs.ext2 … rootfs.ext4`) |
| `images/start-qemu.sh` | `board/qemu/post-image.sh`가 readme의 `# qemu_aarch64_virt_defconfig` 태그 줄을 읽어 생성 |
| `host/bin/qemu-system-aarch64` | `BR2_PACKAGE_HOST_QEMU_SYSTEM_MODE` |
| `host/bin/aarch64-buildroot-linux-gnu-gcc` | internal 툴체인 |

부팅 명령은 readme에 있는 그대로다.

```sh
# 실행하지 않음 — 가이드 (board/qemu/aarch64-virt/readme.txt 원문)
qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic -smp 1 -kernel output/images/Image -append "rootwait root=/dev/vda console=ttyAMA0" -netdev user,id=eth0 -device virtio-net-device,netdev=eth0 -drive file=output/images/rootfs.ext4,if=none,format=raw,id=hd0 -device virtio-blk-device,drive=hd0 # qemu_aarch64_virt_defconfig
```

로그인 프롬프트(`buildroot login:`)에서 `root`, 비밀번호 없음(`BR2_TARGET_GENERIC_ROOT_PASSWD=""`)으로 들어간다. readme 끝의 `# qemu_aarch64_virt_defconfig` 주석은 장식이 아니다 — `post-image.sh`가 이 태그로 해당 줄을 찾아 `start-qemu.sh`를 만들고, Buildroot CI가 이 스크립트로 부팅 테스트를 돌린다.

### 9.2 회사 트리로

```sh
# 실행하지 않음 — 가이드
cd /var/tmp/K3/k3_buildroot
make O=/var/tmp/K3/k3_out_sk BR2_EXTERNAL=/Users/donh/.elinux_scratch/K3/k3_sensorkit_ext k3_sensorkit_defconfig
cd /var/tmp/K3/k3_out_sk && K3_BUILD_ID=$(git -C /Users/donh/.elinux_scratch/K3 describe --always 2>/dev/null || echo dev) make
output_check() { ls -l images/sdcard.img images/Image target/lib/modules/*/updates/k3_imu.ko target/usr/bin/k3-*; }
output_check
host/bin/qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic -smp 1 -kernel images/Image \
  -append "rootwait root=/dev/vda1 console=ttyAMA0" \
  -drive file=images/sdcard.img,if=none,format=raw,id=hd0 -device virtio-blk-device,drive=hd0
# 게스트에서: cat /etc/k3-build-info ; lsmod ; k3-imu-dump ; /etc/init.d/S90k3-sensord start
```

이 빌드가 성공하려면 `k3-imu-mod`의 git 사이트가 실제로 존재하거나, 11절처럼 `local.mk`로 소스 위치를 덮어써야 한다. 7.3·7.4절은 이 결과물의 "rootfs 부분"을 손으로 미리 검증한 것이다.

---

## 10. 재빌드 의미론 — Buildroot는 변경을 추적하지 않는다

### 10.1 원칙

`docs/manual/rebuilding-packages.adoc`의 첫 문장이 핵심이다. 요약하면 "Buildroot는 menuconfig로 설정을 바꿨을 때 무엇을 다시 빌드해야 하는지 **감지하려고 시도하지 않는다.** 완전히 신뢰할 수 있게 감지하기가 너무 어렵기 때문에, 그 판단은 사용자 책임으로 남겼다." 5.5절의 stamp 파일이 있으면 그 단계는 끝난 것으로 본다. 또 Buildroot는 **어떤 패키지가 어떤 파일을 target/staging에 설치했는지 추적하지 않으므로**(`packages-file-list.txt`는 정보용) 패키지를 "제거"하는 기능이 없다.

| 바꾼 것 | 필요한 조치 | 이유 |
|---|---|---|
| 아키텍처·CPU·FPU, 툴체인(gcc 버전, libc) | **전체 재빌드** `make clean all` | 모든 바이너리의 ABI가 바뀜 |
| 새 패키지 추가 | 그냥 `make` | 처음 보는 패키지는 빌드됨 |
| 새 라이브러리 추가 → 기존 패키지가 그걸 선택적으로 쓸 수 있음 | 기존 패키지 `-reconfigure` 또는 전체 | 기존 패키지는 다시 configure되지 않음 (매뉴얼의 ctorrent + openssl 예) |
| 패키지 제거 | **전체 재빌드** | target/에 파일이 남아 있음 |
| 패키지 하위 옵션 변경 | 그 패키지 `-reconfigure`/`-rebuild` | 자동 재빌드 없음 |
| `<PKG>_DEPENDENCIES`의 패키지가 바뀜 | 의존하는 쪽도 직접 재빌드 | 역방향 전파 없음 |
| overlay, post-build, post-image 스크립트 | 그냥 `make` | 매번 다시 실행됨 |
| skeleton(`system/skeleton`) 변경 | 전체 재빌드 | skeleton은 맨 처음 한 번만 설치 |
| 커널 config | `make linux-rebuild` 후 `make` (또는 `linux-reconfigure`) | 이미지는 따로 다시 묶어야 함 |

### 10.2 명령 정리

```sh
# 실행하지 않음 — 가이드
make k3-tools-rebuild        # .stamp_built 이후부터: make + install 다시
make k3-sensord-reconfigure  # cmake 단계부터 (CONF_OPTS를 바꿨을 때)
make k3-imu-mod-dirclean     # build/k3-imu-mod-*/ 삭제 → 다음 make에서 처음부터
make linux-rebuild           # 커널만 다시
make                         # ★ 위 명령들은 rootfs 이미지를 다시 만들지 않는다 — 마지막에 make 필수
make clean all               # 전체 재빌드 (dl/ 은 유지)
```

`make help`가 직접 알려 주는 패키지 타깃 목록(실행 결과, 일부):

```text
Package-specific:
  <pkg>                  - Build and install <pkg> and all its dependencies
  <pkg>-source           - Only download the source files for <pkg>
  <pkg>-extract          - Extract <pkg> sources
  <pkg>-patch            - Apply patches to <pkg>
  <pkg>-depends          - Build <pkg>'s dependencies
  <pkg>-configure        - Build <pkg> up to the configure step
  <pkg>-build            - Build <pkg> up to the build step
  <pkg>-show-info        - generate info about <pkg>, as a JSON blurb
  <pkg>-show-depends     - List packages on which <pkg> depends
  <pkg>-show-rdepends    - List packages which have <pkg> as a dependency
...
  <pkg>-dirclean         - Remove <pkg> build directory
  <pkg>-reconfigure      - Restart the build from the configure step
  <pkg>-rebuild          - Restart the build from the build step
  <pkg>-reinstall        - Restart the build from the install step
```

SSD 펌웨어 빌드와 비교하면: 펌웨어 Makefile은 `.o`와 `.c`의 타임스탬프를 비교해 바뀐 파일만 다시 컴파일했다. Buildroot는 그 단위가 "파일"이 아니라 "패키지 단계"이고, 비교 대상이 타임스탬프가 아니라 **stamp 파일의 존재 여부**다. 패키지 내부는 `-rebuild` 때 그 패키지 자신의 make가 증분 빌드를 한다.

---

## 11. 드라이버 개발 루프 — `<PKG>_OVERRIDE_SRCDIR`

### 11.1 문제

드라이버를 고칠 때마다 git에 커밋·태그하고 `K3_IMU_MOD_VERSION`을 올리고 `dirclean`할 수는 없다. 매뉴얼(`using-buildroot-development.adoc`)의 해법은 **override 파일** `local.mk`다. 기본 위치는 `.config` 옆(`$(CONFIG_DIR)/local.mk`, 즉 `O=` 디렉터리)이다.

```make
# /var/tmp/K3/k3_out_sk/local.mk
K3_IMU_MOD_OVERRIDE_SRCDIR = /Users/donh/.elinux_scratch/K3/src/k3-imu
```

이 줄이 있으면 Buildroot는 그 패키지를 **다운로드·압축 해제·패치하지 않고**, 지정한 디렉터리를 rsync로 `build/<pkg>-custom/`에 복사해 빌드한다. `make clean`도 원본 디렉터리는 건드리지 않는다. 커널도 같은 방식(`LINUX_OVERRIDE_SRCDIR = /home/…/linux`)으로 회사 커널 작업 트리를 그대로 쓸 수 있다.

### 11.2 예제 15 — override 전후 비교

무엇을 확인하나: `local.mk` 한 줄로 패키지의 소스 위치와 빌드 디렉터리 이름이 바뀌는지.

```sh
cd /var/tmp/K3/k3_out_sk
J='import json,sys; d=json.load(sys.stdin)["k3-imu-mod"]; print({k:d.get(k) for k in ["version","licenses","downloads","source_dir","dependencies"]})'
echo "[before]"; make -s k3-imu-mod-show-info | python3 -c "$J"
echo "K3_IMU_MOD_OVERRIDE_SRCDIR = /Users/donh/.elinux_scratch/K3/src/k3-imu" > local.mk
echo "[after local.mk]"; make -s k3-imu-mod-show-info | python3 -c "$J"
make -s printvars VARS="K3_IMU_MOD_OVERRIDE_SRCDIR K3_IMU_MOD_DIR"
rm local.mk
```

```text
[before]
{'version': 'v0.3.0', 'licenses': 'GPL-2.0', 'downloads': [{'source': 'k3-imu-mod-v0.3.0-git4.tar.gz', 'uris': ['git+git@git.example.com:sensorkit/k3-imu.git', 'https|urlencode+https://sources.buildroot.net/k3-imu-mod', 'https|urlencode+https://sources.buildroot.net']}], 'source_dir': 'build/k3-imu-mod-v0.3.0', 'dependencies': ['host-ccache', 'host-skeleton', 'linux', 'skeleton', 'toolchain']}
[after local.mk]
{'version': 'v0.3.0', 'licenses': 'GPL-2.0', 'downloads': [{'source': 'k3-imu-mod-custom-git4.tar.gz', 'uris': ['git+git@git.example.com:sensorkit/k3-imu.git', 'https|urlencode+https://sources.buildroot.net/k3-imu-mod', 'https|urlencode+https://sources.buildroot.net']}], 'source_dir': 'build/k3-imu-mod-custom', 'dependencies': ['host-ccache', 'host-skeleton', 'linux', 'skeleton', 'toolchain']}
K3_IMU_MOD_DIR=/var/tmp/K3/k3_out_sk/build/k3-imu-mod-custom
K3_IMU_MOD_OVERRIDE_SRCDIR=/Users/donh/.elinux_scratch/K3/src/k3-imu
```

출력에서 볼 것: 빌드 디렉터리가 `k3-imu-mod-v0.3.0` → **`k3-imu-mod-custom`** 으로 바뀌었다. show-info의 `downloads`는 메타데이터로 남지만 실제 빌드에서는 다운로드 단계 대신 rsync 단계(`.stamp_rsynced`)가 돈다. 또 하나 — 회사 비공개 패키지인데도 URI 목록에 `sources.buildroot.net`이 백업으로 들어 있다. 회사 빌드에서는 `BR2_BACKUP_SITE=""`로 비우거나 `BR2_PRIMARY_SITE`를 사내 미러로 지정해 **비공개 패키지 이름이 외부로 질의되지 않게** 한다(14절).

### 11.3 매일의 루프 (실행하지 않음 — 가이드)

```sh
# 실행하지 않음 — 가이드
vim /Users/donh/.elinux_scratch/K3/src/k3-imu/k3_imu.c   # 드라이버 수정
make k3-imu-mod-rebuild                                 # rsync(바뀐 파일만) → Kbuild → modules_install
make                                                    # rootfs.ext4 / sdcard.img 다시 묶기
images/start-qemu.sh 또는 scp target/lib/modules/*/updates/k3_imu.ko root@board:/tmp/   # 보드에서 insmod
```

실제 보드에서는 이미지 전체를 다시 굽지 않고 `.ko`만 scp해서 `rmmod`/`insmod`하는 루프가 더 빠르다. Buildroot 쪽 `-rebuild`는 "이 수정이 이미지에도 제대로 들어가는가"를 확인하는 단계로 쓴다. 드라이버 소스는 이렇다(E4·G7의 misc device 패턴).

```c
// SPDX-License-Identifier: GPL-2.0
/* k3_imu: fake IMU as a misc device. read() returns one text sample per call. */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/atomic.h>

static atomic_t k3_seq = ATOMIC_INIT(0);

static ssize_t k3_imu_read(struct file *f, char __user *buf, size_t len, loff_t *ppos)
{
	char line[64];
	int n, s = atomic_inc_return(&k3_seq);

	/* fake accel in mg: gravity on z plus a small ramp on x */
	n = scnprintf(line, sizeof(line), "seq=%d ax=%d ay=%d az=%d\n",
		      s, (s % 10) * 3, -2, 1000);
	if (len < n)
		return -EINVAL;
	if (copy_to_user(buf, line, n))
		return -EFAULT;
	return n;
}

static const struct file_operations k3_imu_fops = {
	.owner = THIS_MODULE,
	.read  = k3_imu_read,
};

static struct miscdevice k3_imu_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "k3_imu",
	.fops  = &k3_imu_fops,
	.mode  = 0444,
};

static int __init k3_imu_init(void)
{
	int ret = misc_register(&k3_imu_dev);

	if (!ret)
		pr_info("k3_imu: /dev/%s ready\n", k3_imu_dev.name);
	return ret;
}

static void __exit k3_imu_exit(void)
{
	misc_deregister(&k3_imu_dev);
}

module_init(k3_imu_init);
module_exit(k3_imu_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("K3 fake IMU misc device");
MODULE_VERSION("0.3.0");
```

```sh
modinfo /var/tmp/K3/k3_mini/target/lib/modules/6.18.55/updates/k3_imu.ko | grep -E "^(version|vermagic|description|license)"
```

```text
version:        0.3.0
description:    K3 fake IMU misc device
license:        GPL
vermagic:       6.18.55 SMP preempt mod_unload aarch64
```

`vermagic`이 랩 커널(6.18.55)과 맞아서 7.4절에서 로드됐다. Buildroot 안에서는 `linux` 패키지가 빌드한 커널(6.18.7)에 대해 빌드되므로 vermagic이 자동으로 맞는다 — kernel-module 인프라가 `linux`를 의존성으로 강제하는 이유다.

---

## 12. legal-info — 라이선스 산출물 (K7로 연결)

`make legal-info`는 선택된 모든 패키지의 **소스 tarball, 패치, 라이선스 파일, 매니페스트(CSV)** 를 `output/legal-info/`에 모은다. GPL 패키지의 "소스 제공 의무"를 지키는 기본 재료다. 모든 소스를 내려받아야 하므로 실행하지 않았다.

```sh
# 실행하지 않음 — 가이드
make legal-info
ls output/legal-info/          # manifest.csv, host-manifest.csv, sources/, licenses/, legal-info.sha256 …
```

회사 패키지에 `_LICENSE`/`_LICENSE_FILES`를 빠뜨리면 manifest에 "unknown"으로 찍힌다(6.6절에서 check-package가 `LICENCE` 철자를 못 잡은 이유와 연결). 독점 패키지는 `<PKG>_REDISTRIBUTE = NO`로 소스가 legal-info에 복사되지 않게 한다. SBOM 생성(`utils/generate-cyclonedx` — 2026.08 `utils/`에 실제로 있다), GPL·LGPL 의무, 고객 배포 절차는 **K7**에서 다룬다.

---

## 13. Buildroot vs Yocto — 30초 비교

| 관점 | Buildroot | Yocto / OpenEmbedded (K4·K5) |
|---|---|---|
| 본질 | Makefile + Kconfig, **이미지 생성기** | BitBake + 레이어·레시피, **배포판 생성기** |
| 결과물 | 펌웨어처럼 통째로 굽는 rootfs 이미지 | 이미지 + 패키지 피드(ipk/rpm/deb) — 부분 업데이트 가능 |
| 설정 | `defconfig` 한 파일 | `local.conf` + `bblayers.conf` + MACHINE/DISTRO + 레이어 수십 개 |
| 학습 곡선 | 하루면 첫 이미지 | 몇 주 (레이어 우선순위, bbappend, 변수 오버라이드) |
| 첫 빌드 | 상대적으로 짧고 디스크 작게 | 길고 디스크 큼 |
| 재빌드 | 변경 추적 없음 — 사용자가 판단 (10절) | 태스크 해시 기반 자동 재빌드 + sstate 캐시 공유 |
| 커스텀 | BR2_EXTERNAL 트리 | 회사 레이어(`meta-company`) + bbappend |
| SoC 벤더 지원 | 일부 보드 defconfig | NXP·TI·Xilinx 등 **공식 BSP 레이어가 Yocto 기준** |
| 앱 개발 SDK | `make sdk` (relocatable 툴체인 + sysroot) | eSDK, devtool |
| 잘 맞는 곳 | 작은 팀, 단일 제품, 작은 이미지, 빠른 프로토타입 | 여러 제품 라인, 큰 팀, 벤더 BSP 의존, 장기 유지보수 |

면접용 한 줄: **"Buildroot는 단순하고 빠른 이미지 생성기, Yocto는 패키지 단위로 배포판을 만드는 프레임워크다. 작은 팀·단일 제품이면 Buildroot, 벤더 BSP가 Yocto 레이어로만 오거나 제품군이 여러 개면 Yocto."** 깊은 내용은 K4(레이어·레시피·BitBake), K5(devtool·sstate·재현성)로 넘긴다.

---

## 14. 회사 워크플로 — CI, 버전, 캐시

### 14.1 저장소 구조

```text
sensorkit-os/                  (회사 git)
  buildroot/                   git submodule -> 2025.02.18 태그 고정 (LTS)
  br2-external/                BR2_EXTERNAL 트리 (6절 구조)
  Makefile                     얇은 래퍼: make -C buildroot O=$(PWD)/output BR2_EXTERNAL=$(PWD)/br2-external ...
  ci/build.sh
```

업그레이드는 submodule 포인터를 옮기고 defconfig를 불러와 savedefconfig diff를 리뷰하는 것으로 끝난다.

### 14.2 예제 16 — 다운로드 캐시 위치

무엇을 확인하나: `BR2_DL_DIR` 환경 변수가 `.config` 값보다 우선한다는 것(매뉴얼 `download-location.adoc` 27행: "is overridden by the BR2_DL_DIR environment variable"). CI에서 모든 빌드가 tarball 캐시 하나를 공유하는 방법이다.

```sh
cd /var/tmp/K3/k3_out_sk
make -s printvars VARS="DL_DIR PARALLEL_JOBS BR2_PRIMARY_SITE BR2_BACKUP_SITE"
echo "--- with env BR2_DL_DIR"
BR2_DL_DIR=/var/tmp/K3/k3_dlcache make -s printvars VARS="DL_DIR"
nproc
```

```text
BR2_BACKUP_SITE="https://sources.buildroot.net"
BR2_PRIMARY_SITE=""
DL_DIR=/var/tmp/K3/k3_buildroot/dl
PARALLEL_JOBS=7
--- with env BR2_DL_DIR
DL_DIR=/var/tmp/K3/k3_dlcache
6
```

출력에서 볼 것: 기본 다운로드 캐시는 Buildroot 트리 안(`dl/`)이라 `git clean`에 날아간다. 환경 변수 하나로 공유 캐시를 쓴다. `PARALLEL_JOBS=7` = nproc 6 + 1.

### 14.3 CI 파이프라인 (실행하지 않음 — 가이드)

```sh
# 실행하지 않음 — 가이드 (ci/build.sh 개요)
set -eu
export BR2_DL_DIR=/cache/br-dl              # 러너 간 공유 볼륨
export BR2_CCACHE_DIR=/cache/br-ccache      # BR2_CCACHE=y 일 때 컴파일 캐시
export K3_BUILD_ID="$(git describe --always --dirty)"
make -C buildroot O="$PWD/output" BR2_EXTERNAL="$PWD/br2-external" k3_sensorkit_defconfig
make -C buildroot O="$PWD/output" savedefconfig BR2_DEFCONFIG="$PWD/output/defconfig.check"
diff br2-external/configs/k3_sensorkit_defconfig output/defconfig.check   # Kconfig가 조용히 바꾼 게 없는지 (4.2절)
make -C output
make -C output legal-info
# 아티팩트: images/sdcard.img, images/Image, legal-info/, .config, build.log
```

실무 체크 포인트:
- 릴리스 빌드는 **clean 빌드**로만 한다(10절 — 증분 빌드는 이전 설정의 잔재가 남을 수 있다). 증분은 개발자 PC에서만.
- `BR2_PRIMARY_SITE`를 사내 미러로, `BR2_BACKUP_SITE`를 비워 외부 의존을 끊으면 업스트림 사이트가 사라져도 재현된다.
- `BR2_REPRODUCIBLE`(experimental)로 타임스탬프 등을 고정할 수 있다.
- `.config`, `build.log`, `legal-info/`를 이미지와 함께 보관해야 "필드의 장비가 정확히 무엇으로 빌드됐나"에 답할 수 있다. post-build가 `/etc/k3-build-info`에 넣은 빌드 ID가 그 연결 고리다.
- 앱 개발자에게는 `make sdk`로 만든 relocatable 툴체인 tarball을 준다(실행하지 않음). 앱 팀은 Buildroot 전체를 빌드하지 않고 같은 sysroot로 크로스 컴파일한다.

---

## 15. 커널 안에서는 무슨 일이 일어나나

Buildroot 자체는 빌드 도구라 커널 안으로 들어가지 않는다. 하지만 **kernel-module 패키지 하나가 보드에서 `/dev` 노드가 되기까지**는 E·G 모듈의 커널 경로를 그대로 탄다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">k3-imu-mod 패키지 → 보드의 /dev/k3_imu</text>
  <text x="170" y="44" text-anchor="middle" font-size="13" font-weight="bold">빌드 머신 (Buildroot)</text>
  <text x="510" y="44" text-anchor="middle" font-size="13" font-weight="bold">보드 (커널 6.18)</text>
  <rect x="20" y="56" width="300" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="83" text-anchor="middle" font-size="12">kernel-module 인프라 훅</text>
  <rect x="20" y="120" width="300" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="147" text-anchor="middle" font-size="12">make -C $(LINUX_DIR) M=$(@D) modules</text>
  <rect x="20" y="184" width="300" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="211" text-anchor="middle" font-size="12">Kbuild: k3_imu.ko + vermagic</text>
  <rect x="20" y="248" width="300" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="275" text-anchor="middle" font-size="12">modules_install → lib/modules/…/updates</text>
  <rect x="20" y="312" width="300" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="339" text-anchor="middle" font-size="12">depmod → modules.dep → rootfs.ext4</text>
  <rect x="360" y="56" width="300" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="83" text-anchor="middle" font-size="12">S05: modprobe k3_imu</text>
  <rect x="360" y="120" width="300" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="147" text-anchor="middle" font-size="12">finit_module → load_module → init</text>
  <rect x="360" y="184" width="300" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="211" text-anchor="middle" font-size="12">misc_register → device_create</text>
  <rect x="360" y="248" width="300" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="275" text-anchor="middle" font-size="12">devtmpfs: /dev/k3_imu (10, minor)</text>
  <rect x="360" y="312" width="300" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="510" y="339" text-anchor="middle" font-size="12">read() → misc fops → k3_imu_read</text>
  <line x1="170" y1="100" x2="170" y2="118" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="170" y1="164" x2="170" y2="182" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="170" y1="228" x2="170" y2="246" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="170" y1="292" x2="170" y2="310" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="320" y1="334" x2="358" y2="78" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="510" y1="100" x2="510" y2="118" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="510" y1="164" x2="510" y2="182" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="510" y1="228" x2="510" y2="246" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="510" y1="292" x2="510" y2="310" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="340" y="390" text-anchor="middle" font-size="12">vermagic이 커널과 다르면 load_module 단계에서 거부된다</text>
</svg>
```

그림 6 — 왼쪽은 7.3절에서 손으로 돌린 빌드 머신 쪽 단계, 오른쪽은 7.4절 QEMU 게스트에서 일어난 커널 경로.

- **빌드 쪽**: `package/pkg-kernel-module.mk`의 `KERNEL_MODULES_BUILD` 훅은 `$(LINUX_MAKE_ENV) make -C $(LINUX_DIR) $(LINUX_MAKE_FLAGS) M=$(@D) modules`를 실행한다. E4에서 `make -C /var/lab/out/linux-build M=$PWD`로 하던 것과 같다. 다른 점은 `LINUX_MAKE_FLAGS`에 `ARCH=arm64 CROSS_COMPILE=$(TARGET_CROSS)`가 들어 있다는 것뿐이다. 이 인프라는 커널 config의 `CONFIG_TRIM_UNUSED_KSYMS`를 끈다(`_LINUX_CONFIG_FIXUPS`). 켜져 있으면 in-tree에서 쓰지 않는 심볼이 export에서 빠져 외부 모듈이 링크되지 않기 때문이다.
- **설치 쪽**: `modules_install`은 `INSTALL_MOD_PATH=$(TARGET_DIR)` 아래 `lib/modules/<kernelrelease>/updates/`에 `.ko`를 넣고 depmod를 돌린다. 그래서 보드에서 `modprobe k3_imu`가 경로 없이 동작한다(7.4절 출력).
- **보드 쪽**: BusyBox `modprobe` → `finit_module(2)` syscall(BusyBox 1.37 `modutils.c`는 finit_module을 먼저 시도하고 실패하면 `init_module`로 내려간다) → 커널의 `load_module()`(vermagic·심볼 버전 검사, 재배치) → `module_init` → `misc_register()`가 `device_create()`를 불러 uevent를 내고, devtmpfs가 `/dev/k3_imu`를 major 10(misc)로 만든다. 노드 권한 0444는 `struct miscdevice`의 `.mode`에서 온 것이다(G7). 이후 `read()`는 VFS → misc 계층 → `k3_imu_fops.read`로 간다(A3, G4).
- 드라이버를 쓸 때 다시 만나는 지점: `_LINUX_CONFIG_FIXUPS`로 드라이버가 필요로 하는 커널 옵션을 패키지와 함께 관리(D·I 모듈의 서브시스템 의존), DT overlay를 함께 설치하려면 같은 패키지의 `_INSTALL_TARGET_CMDS`나 `_INSTALL_IMAGES_CMDS`로 `.dtbo`를 넣는다(D 모듈). 펌웨어 blob(`/lib/firmware`)은 G10.

---

## 16. Embedded 실무에서 다시 보기

**장면 1 — 센서 키트 첫 이미지 만들기.** SoC 보드(예: i.MX8MP EVK)가 오면 `make list-defconfigs`에서 가장 가까운 defconfig를 찾는다. 이 트리에는 `freescale_imx8mpevk_defconfig`, `beagleplay_defconfig`(TI AM62) 등이 있다(list-defconfigs 출력에서 확인). 그걸 BR2_EXTERNAL의 `configs/`로 복사해 이름을 바꾸고, 보드 파일을 `board/<product>/`로 옮긴 뒤 회사 패키지를 얹는다. 업스트림 defconfig를 직접 고치지 않는다.

**장면 2 — "드라이버 고쳤는데 이미지에 안 들어가요".** 원인은 거의 항상 둘 중 하나다. `make k3-imu-mod-rebuild`만 하고 마지막 `make`를 안 해서 rootfs 이미지가 예전 것이거나, `OVERRIDE_SRCDIR` 없이 원본 tarball/git 버전이 그대로라서 수정이 반영되지 않은 것이다. `make k3-imu-mod-show-info`의 `source_dir`가 `-custom`인지, `images/rootfs.ext4`의 시각이 최신인지 본다.

**장면 3 — 필드 장비 문제 분석.** 고객 장비의 `/etc/k3-build-info`에서 빌드 ID를 읽고, CI 아티팩트에서 그 빌드의 `.config`, staging(심볼 있는 바이너리), `legal-info/manifest.csv`를 찾는다. 크래시 코어 덤프는 staging의 unstripped 바이너리로 gdb에서 연다(3.3절). 이 연결이 끊기면 "어떤 소스로 빌드됐는지 모르는 장비"가 생긴다.

**장면 4 — 보안 패치 대응.** CVE가 공지되면 LTS 포인트 릴리스(2025.02.x)의 CHANGES를 확인하고 submodule을 올린다. 해당 패키지만 바뀌었어도 릴리스는 clean 빌드로 다시 만든다. `show-info`의 `cpe-id`·`ignore_cves`(5.6절)와 `utils/generate-cyclonedx` 결과를 보안 스캐너에 넣는다(K7).

---

## 17. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| menuconfig 후 `.config`만 커밋 | 업그레이드 후 새 옵션 누락, 리뷰 diff 수백 줄 | defconfig가 아닌 전체 config를 관리 | `make savedefconfig` 결과만 커밋 |
| `BR2_DEFCONFIG` 없이 savedefconfig | 업스트림 `configs/` 파일이 바뀜 | 불러온 원본 경로에 덮어씀 | BR2_EXTERNAL defconfig 사용 또는 경로 명시 |
| 고른 툴체인이 바뀌어 있음 | 예상과 다른 `TARGET_CROSS` 접두어 | Kconfig `depends on`(예: Bootlin은 x86_64 호스트만)이 조용히 다른 선택지로 대체 | CI에서 savedefconfig diff 검사 |
| 하위 옵션만 바꾸고 `make` | 기능이 안 켜짐 | Buildroot는 변경을 추적하지 않음 | `<pkg>-reconfigure` 후 `make` |
| 패키지를 끄고 `make` | 파일이 target/에 그대로 남음 | 설치 파일 추적 없음 | `make clean all` |
| `<pkg>-rebuild`만 하고 끝 | 보드 이미지에 변경 없음 | 패키지 타깃은 rootfs를 다시 만들지 않음 | 마지막에 `make` |
| post-build에서 `chown` | 이미지 안 소유권이 그대로 root 또는 엉뚱함 | post-build는 fakeroot 밖, 이후 `chown -R 0:0`이 덮어씀 | device 테이블이나 post-fakeroot 스크립트 사용 |
| 소스 Makefile에 `CC=gcc` 하드코딩 | 보드에서 `Exec format error` | 호스트 아키텍처로 컴파일됨 | `$(CC)` 사용, `.mk`에서 `$(TARGET_CONFIGURE_OPTS)` 전달 |
| `K3_TOOLS_LICENCE` 같은 철자 오류 | legal-info에 unknown 라이선스 | Make는 모르는 변수를 무시, check-package도 못 잡음 | `show-info`의 `licenses` 필드 확인 |
| `/dev` 노드 권한을 device 테이블로 설정 | 부팅 후 권한이 반영 안 됨 | devtmpfs 환경에선 노드가 런타임에 생김 | 드라이버 `.mode`, mdev/udev 규칙 사용 |

---

## 18. 면접에서 이렇게 말한다

**Q.** What is Buildroot, and how does it produce an image?

**A.** Buildroot는 GNU Make와 Kconfig로 된 임베디드 Linux 이미지 생성기다. defconfig로 설정을 정하면 크로스 툴체인을 만들거나 받아 오고, 패키지를 의존 순서대로 download·extract·patch·configure·build·install해서 `output/target`에 rootfs를 모은다. 그다음 overlay와 post-build 스크립트로 다듬고, fakeroot 안에서 사용자·권한 테이블을 적용해 ext4 같은 파일시스템 이미지를 만들고, post-image에서 genimage로 SD 카드 이미지를 만든다.

> Buildroot is a Make- and Kconfig-based image generator. A defconfig selects the architecture, toolchain, kernel, bootloader and packages. Buildroot then builds or imports a cross toolchain, builds each package through download, extract, patch, configure, build and install steps into output/target, applies the rootfs overlay and post-build scripts, generates filesystem images under fakeroot using the users and permission tables, and finally runs post-image scripts, typically genimage, to produce a flashable disk image.

**꼬리 질문**: "Why fakeroot?" — 빌드는 일반 사용자로 하지만 이미지 안 파일은 root 소유·특수 권한이어야 하므로, `chown`과 `stat`을 가로채는 fakeroot 세션 안에서 mkfs를 돌린다.

**Q.** What is the difference between the staging directory and the target directory?

**A.** staging은 크로스 툴체인의 sysroot다. 헤더·정적 라이브러리·pkg-config 파일·심볼 있는 라이브러리가 들어가고, 다른 패키지가 컴파일·링크할 때 쓴다. target은 보드에 실릴 rootfs로, 실행 파일과 공유 라이브러리만 strip된 상태로 들어간다. 라이브러리 패키지는 `INSTALL_STAGING = YES`로 양쪽에 설치한다. 원격 디버깅 때 gdb의 sysroot를 staging으로 두는 이유이기도 하다.

> Staging is the toolchain sysroot, `host/<tuple>/sysroot`. It contains headers, static libraries, pkg-config files and unstripped libraries, and it is what other packages compile and link against. Target is what ends up on the device: binaries and shared libraries only, stripped, no headers. Library packages set INSTALL_STAGING to YES so they land in both. For remote debugging I point gdb's sysroot at staging to get symbols.

**Q.** How do you add your own application or driver to a Buildroot build?

**A.** 회사 BR2_EXTERNAL 트리에 패키지 디렉터리를 만든다. `Config.in`에 메뉴 항목과 의존성, `.mk`에 VERSION·SITE·LICENSE·LICENSE_FILES를 쓰고 빌드 시스템에 맞는 인프라를 eval한다 — CMake면 `cmake-package`라 빌드 명령을 안 써도 되고, 손으로 쓴 Makefile이면 `generic-package`에 BUILD_CMDS와 INSTALL_TARGET_CMDS를 쓴다. 드라이버는 `kernel-module` 인프라를 `generic-package`와 함께 eval하면 커널 트리에 대해 Kbuild로 빌드하고 modules_install까지 해 준다. 공개 tarball이면 `.hash`도 넣고, 마지막에 `utils/check-package`로 린트한다.

> I add a package directory in our BR2_EXTERNAL tree: a Config.in entry with its dependencies, and a .mk that sets VERSION, SITE, LICENSE and LICENSE_FILES and evaluates the right infrastructure. For CMake it is cmake-package and I usually only set CONF_OPTS; for a hand-written Makefile it is generic-package with BUILD_CMDS and INSTALL_TARGET_CMDS. For an out-of-tree driver I evaluate kernel-module together with generic-package, which builds it with Kbuild against the Buildroot kernel and runs modules_install, and adds the linux dependency automatically. Then I run utils/check-package.

**Q.** Why would you use BR2_EXTERNAL instead of committing to the Buildroot tree?

**A.** Buildroot 트리를 업스트림 그대로 유지하려고다. 회사 defconfig·보드 파일·패키지·패치를 외부 트리에 두면, Buildroot는 submodule로 LTS 태그에 고정하고 업그레이드는 태그 이동 + savedefconfig diff 리뷰로 끝난다. 경로는 `BR2_EXTERNAL_<NAME>_PATH`로 쓰므로 어디에 clone해도 동작하고, 여러 외부 트리를 겹칠 수도 있다.

> To keep Buildroot itself pristine. Our defconfigs, board files, packages and patches live in an external tree referenced through `BR2_EXTERNAL_<NAME>_PATH`, while Buildroot is a submodule pinned to an LTS tag. Upgrading becomes moving the tag and reviewing the savedefconfig diff, instead of rebasing a fork.

**Q.** You changed an option in menuconfig and ran make, but the change is not in the image. Why?

**A.** Buildroot는 설정 변경을 추적하지 않고 패키지별 stamp 파일만 본다. 이미 빌드된 패키지는 다시 configure되지 않는다. 해당 패키지를 `-reconfigure`나 `-rebuild`하고 마지막에 `make`로 rootfs를 다시 묶어야 한다. 아키텍처·툴체인 변경이나 패키지 제거는 `make clean all`이 필요하다 — 어떤 패키지가 어떤 파일을 설치했는지 추적하지 않기 때문이다.

> Buildroot deliberately does not track configuration changes; it relies on per-package stamp files. A package that was already built is not reconfigured, so I run `make <pkg>-reconfigure` or `-rebuild` and then a plain make to regenerate the rootfs. Changing the architecture or toolchain, or removing a package, needs make clean all, because Buildroot does not track which files each package installed.

**꼬리 질문**: "How do you iterate on a driver quickly?" — `local.mk`에 `<PKG>_OVERRIDE_SRCDIR`를 지정하면 다운로드 대신 로컬 작업 트리를 rsync해 빌드하므로 `make <pkg>-rebuild && make`로 루프를 돈다.

**Q.** Rootfs overlay, post-build script, post-image script — when do you use which?

**A.** 정적 설정 파일은 overlay로 얹는다. 빌드 ID 각인, 파일 생성·삭제처럼 로직이 필요하면 post-build(TARGET_DIR를 인자로 받음, fakeroot 전). 소유권·권한은 스크립트에서 chown하지 말고 users·device 테이블로. 디스크 이미지 조립·서명·업데이트 번들은 post-image(BINARIES_DIR를 인자로 받음)에서 genimage로 한다.

> Static files go in the overlay. Anything that needs logic, like stamping a build ID or removing a service, goes in post-build, which receives TARGET_DIR and runs before fakeroot. Ownership and permissions go in the users and device tables, not chown in a script. Disk image assembly, signing and update bundles go in post-image, which receives BINARIES_DIR, usually with genimage.

**Q.** Buildroot or Yocto for a new robotics product?

**A.** 팀이 작고 제품이 하나이며 이미지를 통째로 OTA하는 구조라면 Buildroot가 빠르고 단순하다. 반대로 SoC 벤더 BSP가 Yocto 레이어로만 제공되거나(NXP·TI가 대표적), 제품 라인이 여러 개거나, 패키지 피드로 부분 업데이트를 하거나, 큰 팀이 sstate 캐시를 공유해야 하면 Yocto다. 실제로는 벤더 BSP 지원이 결정 요인인 경우가 많다.

> If it is a small team shipping one product with whole-image A/B updates, Buildroot is faster to set up and easier to reason about. If the SoC vendor ships its BSP as Yocto layers, if there are several product lines, if we need package feeds, or a large team sharing an sstate cache, Yocto wins. In practice vendor BSP support is often the deciding factor.

**Q.** How do you make Buildroot builds reproducible and CI-friendly?

**A.** Buildroot는 LTS 태그로 고정, defconfig는 savedefconfig 형식으로 커밋, 외부 트리 하나에 제품 정의 전부. CI는 clean 빌드, `BR2_DL_DIR`로 공유 다운로드 캐시, `BR2_PRIMARY_SITE`로 사내 미러, 해시 강제. 산출물로 이미지와 함께 `.config`, legal-info, 빌드 로그를 보관하고 빌드 ID를 rootfs에 각인한다.

> Pin Buildroot to an LTS tag, commit defconfigs in savedefconfig form, keep the whole product in one external tree. CI does clean builds with a shared BR2_DL_DIR, an internal primary mirror, forced hash checks, and a savedefconfig diff to catch Kconfig silently changing choices. We archive the image together with .config, legal-info and the build log, and stamp the build ID into the rootfs.

---

## 19. 직접 해보기

1. (개념) `STAGING_DIR`이 `host/` 아래에 있는 이유를 sysroot 관점에서 설명하라. 정답/힌트: 크로스 gcc의 `--sysroot`가 가리키는 곳이 곧 staging이다(예제 5).
2. (관찰) `/var/tmp/K3/k3_out`에서 `make linux-show-depends`와 `make host-qemu-show-recursive-depends`를 비교하고, 호스트 QEMU를 끄면(`-d BR2_PACKAGE_HOST_QEMU`) `make external-deps | wc -l`이 얼마나 줄어드는지 세어 보라. 정답/힌트: 예제 4 이후 기준 49 → 39. qemu·glib·pixman·libslirp·dtc 등 10개가 빠지고 Python·meson·ninja는 남는다(예제 9 해설).
3. (코드) `k3-tools`에 `k3-imu-stat`(N개 샘플의 ax 평균을 출력)을 추가하고 `K3_TOOLS_INSTALL_TARGET_CMDS`를 고친 뒤 check-package를 통과시켜라. 정답/힌트: `$(INSTALL) -D -m 0755` 한 줄 추가, 탭 들여쓰기.
4. (코드) `k3-sensord`를 meson 프로젝트로 바꾸고 `meson-package`용 `.mk`를 써 보라. 정답/힌트: `_CONF_OPTS = -Dsample_hz=100`, `$(eval $(meson-package))`, `docs/manual/adding-packages-meson.adoc`.
5. (관찰) 예제 13 스크립트에서 users 테이블의 uid `-1`을 `-2`로 바꾸고 다시 돌려 `/etc/passwd`의 uid 범위를 확인하라. 정답/힌트: `-2`는 일반 사용자 범위(1000~1999).
6. (설계) 센서 키트에 A/B rootfs와 `/data` 파티션이 있는 genimage.cfg를 설계하라(파티션 오프셋, 타입, 크기). 정답/힌트: rootfs-a, rootfs-b 둘 다 `root-arm64` 타입, data는 `L`, 부트로더 raw 영역은 `in-partition-table = "no"`.
7. (빌드 — Don이 나중에) 9.1절 가이드대로 qemu_aarch64_virt를 실제로 빌드하고, 기대 산출물 표가 맞는지, `output/build/*/.stamp_*`가 그림 3대로 생기는지 확인하라. 정답/힌트: `ls output/build/busybox-1.38.0/.stamp_*`.

---

## 20. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| Buildroot | 임베디드 Linux 이미지 생성기 | Make + Kconfig로 툴체인부터 디스크 이미지까지 |
| defconfig | 최소 설정 파일 | 기본값과 다른 `BR2_` 줄만 담음 |
| savedefconfig | 최소 설정 저장 | `.config` → defconfig, 커밋 대상 |
| BR2_EXTERNAL | 외부 트리 | 회사 패키지·보드·defconfig를 본체 밖에 둠 |
| staging | 빌드용 sysroot | `host/<tuple>/sysroot`, 헤더·라이브러리 |
| target | 제품 rootfs 원본 | `output/target`, strip된 실행 파일 |
| host | 빌드 머신용 도구 | `output/host/bin`의 cross gcc, mkfs 등 |
| generic-package | 기본 패키지 인프라 | 빌드·설치 명령을 직접 씀 |
| cmake-package | CMake 인프라 | toolchain file·DESTDIR 자동 |
| kernel-module | 외부 모듈 인프라 | Kbuild `M=` 빌드 + modules_install 훅 |
| `.hash` | 체크섬 파일 | 소스·라이선스 파일 sha256 검증 |
| stamp 파일 | 단계 완료 표시 | `.stamp_built` 등, 있으면 단계 생략 |
| rootfs overlay | 덮어쓸 파일 트리 | target/ 위에 rsync |
| post-build | target 후처리 스크립트 | fakeroot 전, 인자 TARGET_DIR |
| post-image | 이미지 후처리 스크립트 | 인자 BINARIES_DIR, genimage 호출 |
| fakeroot | 가짜 root 환경 | chown/stat 가로채 root 소유 이미지 생성 |
| makedevs | 권한 테이블 도구 | device 테이블로 소유자·모드 설정 |
| mkusers | 사용자 테이블 도구 | users 테이블 → passwd/group/shadow |
| genimage | 디스크 이미지 도구 | 파티션 테이블 + 파티션 이미지 조립 |
| OVERRIDE_SRCDIR | 로컬 소스 대체 | `local.mk`로 다운로드 대신 작업 트리 사용 |
| legal-info | 라이선스 산출물 | 소스·라이선스·매니페스트 수집 (K7) |
| BR2_DL_DIR | 다운로드 캐시 | 환경 변수로 공유 캐시 지정 |
| LTS | 장기 지원 릴리스 | 2025.02부터 3년, 홀수 해 2월 |

---

## 21. 요약 & 체크리스트

Buildroot는 Make와 Kconfig로 된 이미지 공장이다. defconfig가 아키텍처·툴체인·커널·패키지를 고르면, 툴체인(internal 빌드 또는 external 수입) → 패키지(stamp 파일로 단계 관리) → `target/` 조립 → overlay·post-build → fakeroot 안 users/device 테이블과 mkfs → post-image·genimage 순서로 이미지를 만든다. staging은 툴체인 sysroot(개발용), target은 제품용이다. 회사 제품은 업스트림 트리를 건드리지 않고 BR2_EXTERNAL 트리에 defconfig·보드·패키지를 두며, 패키지는 `Config.in` + `.mk` + `.hash`와 빌드 시스템에 맞는 인프라(generic/cmake/kernel-module …)로 쓴다. Buildroot는 설정 변경을 추적하지 않으므로 `-reconfigure`/`-rebuild`/`clean all`을 사용자가 판단하고, 드라이버 개발은 `local.mk`의 `OVERRIDE_SRCDIR`로 루프를 돈다. Yocto와 비교하면 단순·빠름 대 확장성·벤더 BSP·자동 재빌드의 교환이다.

- [ ] Buildroot 다섯 단계(설정·툴체인·패키지·rootfs 마무리·이미지)를 그림으로 설명할 수 있다
- [ ] `output/` 아래 build·host·staging·target·images의 역할과 staging = sysroot임을 설명할 수 있다
- [ ] defconfig를 불러오고, utils/config나 menuconfig로 바꾸고, savedefconfig로 최소 diff를 저장할 수 있다
- [ ] internal과 external 툴체인의 장단점과, Kconfig가 선택을 조용히 바꾸는 함정을 말할 수 있다
- [ ] `<PKG>_VERSION/_SITE/_LICENSE/_LICENSE_FILES/_DEPENDENCIES/_INSTALL_TARGET_CMDS`로 generic 패키지를 쓸 수 있다
- [ ] cmake-package와 kernel-module 인프라로 회사 앱과 드라이버를 패키지화할 수 있다
- [ ] BR2_EXTERNAL 트리(external.desc, Config.in, external.mk, configs/, board/, package/)를 만들 수 있다
- [ ] overlay, post-build, users/device 테이블, post-fakeroot, post-image의 실행 순서와 용도를 구분할 수 있다
- [ ] fakeroot가 root 권한 없이 root 소유 이미지를 만드는 원리를 설명할 수 있다
- [ ] genimage 설정으로 부트로더 raw 영역 + GPT rootfs 디스크 이미지를 설계할 수 있다
- [ ] "Buildroot는 변경을 추적하지 않는다"에서 나오는 재빌드 규칙을 표로 말할 수 있다
- [ ] `OVERRIDE_SRCDIR`로 드라이버 개발 루프를 돌리고, Buildroot와 Yocto를 30초 안에 비교할 수 있다

---

## 참고 자료

- The Buildroot user manual (공식): https://buildroot.org/downloads/manual/manual.html — 이 노트에서 읽은 절의 원본은 소스 트리 `docs/manual/`의 `customize-outside-br.adoc`, `adding-packages-generic.adoc`, `adding-packages-cmake.adoc`, `adding-packages-kernel-module.adoc`, `rebuilding-packages.adoc`, `using-buildroot-development.adoc`, `makeusers-syntax.adoc`, `makedev-syntax.adoc`, `download-location.adoc`
- Buildroot 다운로드·릴리스: https://buildroot.org/download.html , LTS 정책: https://buildroot.org/lts.html
- Buildroot 소스: https://gitlab.com/buildroot.org/buildroot (이 노트는 태그 2026.08, 커밋 d5180309)
- Bootlin Buildroot 교육 자료(슬라이드·실습 공개): https://bootlin.com/training/buildroot/
- Bootlin toolchains: https://toolchains.bootlin.com/
- genimage: https://github.com/pengutronix/genimage
- Frank Vasquez, Chris Simmonds, "Mastering Embedded Linux Development" (4th ed.) — Buildroot·Yocto 장
- 커널 문서 "Building External Modules": https://docs.kernel.org/kbuild/modules.html
- 관련 노트: B3(sysroot), B4(crosstool-NG), E4(out-of-tree 모듈), G7(misc device), K1(rootfs 구조), K2(BusyBox), K4·K5(Yocto), K7(라이선스·SBOM), L(OTA)
