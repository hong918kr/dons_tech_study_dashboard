# S6. Yocto 커스텀 이미지 프로젝트 — `meta-donlab` layer 하나에 드라이버 · 데몬 · 커널 패치 · 이미지를 담고 kas로 재현한다

> **이 노트를 다 읽으면**: 제품 layer(`meta-donlab`)를 처음부터 끝까지 직접 만들 수 있다 · out-of-tree 드라이버 레시피(`inherit module` + `KERNEL_MODULE_AUTOLOAD`), CMake + systemd 데몬 레시피, 커널 `.bbappend`(config fragment + DT overlay 패치), 이미지 레시피를 BitBake 파서로 검증할 수 있다 · 같은 소스를 Yocto 밖에서 빌드해 QEMU(initramfs + systemd 게스트)에서 동작을 증명할 수 있다 · kas lock + CI로 "누가 언제 빌드해도 같은 이미지"를 설명하고 GitHub에 보여 줄 수 있다
> **왜 필요한가**: JD의 "Yocto/BitBake experience"를 면접에서 증명하는 가장 빠른 방법은 **직접 만든 layer 저장소 링크**다. 질문도 대부분 이 프로젝트 안에서 나온다 — "드라이버를 이미지에 어떻게 넣었나", "커널 설정은 어디서 바꿨나", "빌드를 어떻게 재현하나"
> **Don 기준 난이도**: 펌웨어 릴리스 파이프라인 · 패치 큐 · 드라이버 bring-up은 익숙하다 / 새로 손에 익힐 부분은 **layer 파일 배치 규칙, override 문법의 함정, kernel-yocto의 KMACHINE, QA 에러를 읽는 법**
> **선행 노트**: K4(Yocto 기초 — 문법·태스크·이미지), K5(Yocto 실무 — BSP layer, devtool, 커널 레시피, sstate, kas), E3(커널 모듈), D6(DT overlay), L1(systemd), L4(OTA), K7(라이선스·SBOM). 이론은 K4·K5에 있고, **이 노트는 그 이론을 프로젝트 하나로 묶는 실습 절차**다

---

## 0. 큰 그림 — 무엇을 만들고, 무엇을 실제로 돌렸나

### 0.1 실행 원칙 (먼저 읽기)

Don의 결정(heavy build 규칙)에 따라 **이 노트에서 `bitbake`로 빌드를 돌리지 않는다.** 이미지 한 번에 수 시간 · 수십 GB가 들고, 이 공용 VM(Ubuntu 26.04)은 AppArmor가 비특권 user namespace를 막아 BitBake 태스크가 시작조차 못 한다(K4 10.2절에서 원인을 코드로 확인했다). 그래서 블록은 세 종류다.

- **실행함**: layer 파일 작성, `kas checkout`/`kas lock`/`kas dump`, `bitbake-layers show-layers`, `bitbake-getvar`, BitBake **파서**(tinfoil)로 레시피 하나씩 파싱, 정적 lint, `yamllint`, `bash -n`, DT 패치 `git apply --check`, `merge_config.sh` + `olddefconfig`(설정만), 그리고 **같은 소스를 Yocto 밖에서 빌드해 QEMU에서 실행**. 출력은 전부 VM(`uname -r` = `7.0.0-38-generic`, aarch64)에서 나온 그대로다.
- **가이드(실행 안 함)**: `bitbake donlab-image`, `runqemu`, `devtool`, `testimage`, CI의 build 잡. 명령과 "무엇이 나와야 하는지"만 적고 **출력은 쓰지 않는다**.
- **기억 기반**: 빌드 시간 · 디스크 수치처럼 이 환경에서 잴 수 없는 값. 그렇게 표시한다.

파서만 돌려도 꽤 많은 것을 잡는다 — 문법 오류, 없는 class, override가 안 먹는 문제, `COMPATIBLE_MACHINE` 때문에 레시피가 건너뛰어지는 문제, 변수의 최종 값. 못 잡는 것은 **태스크를 실제로 돌려야 나오는 것**(컴파일 에러, QA 에러, 패키지 의존성 해석)이고, 이 노트는 그 부분을 "Yocto 밖 빌드 + QEMU"와 "QA 에러 표"(12절)로 메운다.

### 0.2 최종 산출물

| 산출물 | 위치 | 상태 |
|---|---|---|
| layer 저장소 `meta-donlab` (git, 39개 파일) | `~/.elinux_scratch/S6/meta-donlab/` | 작성 · 커밋함 |
| layer.conf · machine `donlab-qemu` · distro `donlab` | `conf/` | 파서 검증 |
| 드라이버 레시피 `donlab-sensor-mod` (platform + misc device) | `recipes-kernel/donlab-sensor-mod/` | 파서 검증 + QEMU 실행 |
| 데몬 레시피 `donlab-sensord` (CMake + systemd) | `recipes-apps/donlab-sensord/` | 파서 검증 + QEMU systemd 게스트 실행 |
| 커널 `linux-yocto_%.bbappend` + `donlab.cfg` + DT overlay 패치 | `recipes-kernel/linux/` | 파서 · apply-check · config 병합 검증 |
| 이미지 `donlab-image.bb` + `packagegroup-donlab` | `recipes-core/` | 파서 검증 (RO · dev 변형 포함) |
| kas 프로젝트 + lock + overlay 5개 | `kas/` | checkout · lock · dump 실행 |
| OTA 자리(RAUC bundle · SWUpdate .swu) | `dynamic-layers/` | 해당 layer를 붙였을 때 파서 검증 |
| 정적 lint · 테스트 스크립트 · CI 워크플로 | `scripts/` `tests/` `.github/` | lint · yamllint 실행, CI는 가이드 |
| 실제 이미지 `donlab-image-donlab-qemu.rootfs.ext4.zst` | `tmp/deploy/images/donlab-qemu/` | **가이드(실행 안 함)** |

### 0.3 layer 스택 — 우리 layer는 어디에 얹히나

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <rect x="20" y="20" width="380" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="210" y="39" text-anchor="middle" font-size="13">meta-donlab · priority 10</text>
  <text x="210" y="57" text-anchor="middle" font-size="12">machine · distro · 드라이버 · 데몬 · 이미지</text>
  <text x="415" y="47" font-size="13">우선순위 최고 — 우리 값이 이긴다</text>
  <rect x="20" y="76" width="380" height="46" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5 4"/>
  <text x="210" y="95" text-anchor="middle" font-size="13">meta-rauc / meta-swupdate (선택)</text>
  <text x="210" y="113" text-anchor="middle" font-size="12">kas/ota-*.yml 일 때만 · dynamic-layers</text>
  <text x="415" y="103" font-size="13">OTA 자리 — L4</text>
  <rect x="20" y="132" width="380" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-dasharray="5 4"/>
  <text x="210" y="151" text-anchor="middle" font-size="13">BSP layer (실제 보드일 때)</text>
  <text x="210" y="169" text-anchor="middle" font-size="12">meta-freescale · meta-ti · meta-tegra + 회사 BSP</text>
  <text x="415" y="159" font-size="13">QEMU에서는 없음 — 15절</text>
  <rect x="20" y="188" width="380" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="207" text-anchor="middle" font-size="13">meta-openembedded/meta-oe · priority 5</text>
  <text x="210" y="225" text-anchor="middle" font-size="12">libgpiod · htop (LAYERRECOMMENDS)</text>
  <text x="415" y="215" font-size="13">없어도 이미지는 빌드된다</text>
  <rect x="20" y="244" width="380" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="263" text-anchor="middle" font-size="13">meta-yocto/meta-poky · priority 5</text>
  <text x="210" y="281" text-anchor="middle" font-size="12">DISTRO poky — donlab.conf가 require</text>
  <text x="415" y="271" font-size="13">배포판 정책의 출발점</text>
  <rect x="20" y="300" width="380" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="319" text-anchor="middle" font-size="13">openembedded-core/meta · priority 5</text>
  <text x="210" y="337" text-anchor="middle" font-size="12">qemuarm64.conf · linux-yocto 6.18 · classes</text>
  <text x="415" y="327" font-size="13">LAYERSERIES_CORENAMES = wrynose</text>
  <rect x="20" y="356" width="380" height="34" rx="6" fill="none" stroke="currentColor"/>
  <text x="210" y="378" text-anchor="middle" font-size="13">BitBake 2.18 (도구 · layer 아님)</text>
  <text x="415" y="378" font-size="13">kas가 전부 같은 커밋으로 고정</text>
</svg>
```

그림 1 — 이 프로젝트의 layer 스택. 실선은 항상 쓰는 layer, 점선은 조건부(실제 보드 BSP, OTA). 오른쪽은 각 layer가 이 프로젝트에서 맡는 역할이다.

bare-metal 비유로 말하면: oe-core는 **벤더 SDK의 HAL + 빌드 스크립트**, meta-poky는 **레퍼런스 프로젝트의 기본 설정**, BSP layer는 **보드 지원 패키지**, meta-donlab은 **우리 제품 펌웨어 저장소**다. 제품 저장소는 SDK 파일을 직접 고치지 않고 "덮어쓰기 규칙"(bbappend, override)으로만 바꾼다 — SSD 펌웨어에서 벤더 코드를 fork하지 않고 패치 큐로 관리하던 것과 같은 원칙이다.

### 0.4 각 단계가 기대는 선행 노트

| 단계 | 이 노트 절 | 이론은 여기 |
|---|---|---|
| layer.conf, 우선순위, bbappend 순서 | 2 | K4 1.2–1.3, 8 |
| MACHINE · DISTRO 분리, override | 3 | K4 7.1–7.2, 3.4 |
| out-of-tree 모듈, module.bbclass | 4 | E3, K5 4.3 |
| platform driver, misc device | 4 | G1, G2 |
| systemd unit, 하드닝 | 5 | L1 |
| kernel-yocto, .cfg, DT 패치 | 6 | K5 3.2–3.6, E4, D6 |
| 이미지 레시피, IMAGE_FEATURES | 7 | K4 7.3–7.5 |
| kas, lock, sstate, CI | 2, 13 | K5 1, 7 |
| OTA bundle | 10 | L4 6 |
| SPDX, 라이선스 체크섬 | 9, 12 | K7 |

---

## 1. 준비물과 저장소 구조

### 1.1 준비물

- Lima VM `elinux` (aarch64 Ubuntu 26.04) — 레시피 파싱 · Yocto 밖 빌드 · QEMU.
- 공용 랩: 커널 빌드 트리 `/var/lab/out/linux-build-trace`(6.18.55-trace), 커널 `Image-trace`, BusyBox initramfs, systemd 게스트 `rootfs-trace.ext4`(qcow2 overlay로만 사용).
- `/var/tmp/S6/venv` — kas 5.5, yamllint 1.38 (`pip install kas yamllint`, 자기 venv에만 설치).
- 소스는 Mac 쪽 `~/.elinux_scratch/S6/meta-donlab/`(VM에서는 읽기 전용으로 보인다). 그래서 git 작업은 Mac에서, 빌드 산출물은 VM의 `/var/tmp/S6/`에 둔다.

나중에 실제 빌드를 할 때(가이드): Yocto 6.0이 지원하는 호스트 배포판, 디스크 여유 100 GB 이상, RAM 16 GB 이상(기억 기반 권장치), 그리고 user namespace를 허용한 호스트 — 회사 빌드 서버나 클라우드 VM이 일반적이다.

### 1.2 완성된 저장소 트리

git에 들어간 파일 기준으로 그린 트리다 (Mac에 `tree`가 없어서 `git ls-files`를 Python으로 트리 모양으로 찍었다).

```sh
cd ~/.elinux_scratch/S6/meta-donlab && git ls-files | python3 tree.py   # tree.py = 20줄짜리 트리 출력기
```

```text
meta-donlab/
|-- .github/
|   `-- workflows/
|       `-- ci.yml
|-- conf/
|   |-- distro/
|   |   `-- donlab.conf
|   |-- machine/
|   |   `-- donlab-qemu.conf
|   `-- layer.conf
|-- dynamic-layers/
|   |-- rauc/
|   |   `-- recipes-core/
|   |       `-- bundles/
|   |           `-- donlab-bundle.bb
|   `-- swupdate/
|       `-- recipes-core/
|           `-- images/
|               |-- files/
|               |   `-- sw-description
|               `-- donlab-swu.bb
|-- kas/
|   |-- ci.yml
|   |-- dev.yml
|   |-- donlab.lock.yml
|   |-- donlab.yml
|   |-- ota-rauc.yml
|   |-- ota-swupdate.yml
|   `-- ro-rootfs.yml
|-- recipes-apps/
|   `-- donlab-sensord/
|       |-- files/
|       |   |-- CMakeLists.txt
|       |   |-- LICENSE
|       |   |-- donlab-sensord.service
|       |   `-- donlab_sensord.c
|       `-- donlab-sensord_0.1.bb
|-- recipes-core/
|   |-- images/
|   |   `-- donlab-image.bb
|   `-- packagegroups/
|       `-- packagegroup-donlab.bb
|-- recipes-kernel/
|   |-- donlab-sensor-mod/
|   |   |-- files/
|   |   |   |-- Makefile
|   |   |   `-- donlab_sensor.c
|   |   `-- donlab-sensor-mod_0.1.bb
|   `-- linux/
|       |-- files/
|       |   |-- 0001-arm64-dts-add-donlab-temperature-sensor-overlay.patch
|       |   `-- donlab.cfg
|       `-- linux-yocto_%.bbappend
|-- scripts/
|   `-- lint-layer.py
|-- tests/
|   |-- systemd-guest/
|   |   |-- install.sh
|   |   |-- report.sh
|   |   |-- run.sh
|   |   `-- s6-report.service
|   |-- build-outside-yocto.sh
|   |-- check-kernel-fragment.sh
|   `-- initramfs-smoke.sh
|-- .gitignore
|-- .yamllint
|-- COPYING.MIT
`-- README.md
```

출력에서 볼 것: **저장소 루트가 곧 layer**다(`conf/layer.conf`가 루트에 있다). 그래서 kas 파일에서 이 저장소를 `layers: .:`로 가리킨다. 소스(C, CMake, Makefile)가 레시피 옆 `files/`에 있는 것은 포트폴리오용 단순화다 — 실무에서는 드라이버와 데몬이 각자 git 저장소에 있고 레시피는 `SRC_URI = "git://...;branch=main"` + `SRCREV`로 가리킨다(5.2절).

> 이 단계가 끝나면: 빈 디렉터리 골격과 `git init`이 된 저장소가 있어야 한다. VM에서 `git init`을 하면 `Read-only file system`으로 실패한다 — Mac 쪽에서 한다.

---

## 2. Step 1 — layer 뼈대와 kas 프로젝트

### 2.1 `conf/layer.conf`

```text
# meta-donlab: product layer for the Don's-lab sensor kit (S6 project)
BBPATH .= ":${LAYERDIR}"

BBFILES += "${LAYERDIR}/recipes-*/*/*.bb \
            ${LAYERDIR}/recipes-*/*/*.bbappend"

BBFILE_COLLECTIONS += "donlab"
BBFILE_PATTERN_donlab = "^${LAYERDIR}/"
BBFILE_PRIORITY_donlab = "10"

# recipes under dynamic-layers/<collection>/ are parsed only when that layer is present
BBFILES_DYNAMIC += " \
    rauc:${LAYERDIR}/dynamic-layers/rauc/recipes-*/*/*.bb \
    swupdate:${LAYERDIR}/dynamic-layers/swupdate/recipes-*/*/*.bb \
    swupdate:${LAYERDIR}/dynamic-layers/swupdate/recipes-*/*/*.bbappend \
"

LAYERVERSION_donlab = "1"
LAYERDEPENDS_donlab = "core"
LAYERRECOMMENDS_donlab = "openembedded-layer"
# Yocto Project 6.0 "wrynose" (current LTS, verified in K5 0.2)
LAYERSERIES_COMPAT_donlab = "wrynose"
```

K4 9절의 layer.conf와 달라진 점만 짚는다.

- **`LAYERSERIES_COMPAT = "wrynose"`**: 현재 LTS는 Yocto 6.0 "Wrynose"(bitbake 2.18 짝)다. 기억이 아니라 K5 0.2절에서 저장소 태그로 확인한 값이고, 아래 lint가 oe-core의 `LAYERSERIES_CORENAMES`와 다시 맞춰 본다.
- **`BBFILES_DYNAMIC`**: `컬렉션이름:경로` 형식. 그 컬렉션(`rauc`, `swupdate`)이 BBLAYERS에 있을 때만 경로가 BBFILES에 붙는다. 그래서 meta-rauc가 없는 빌드에서 `inherit bundle`이 "class를 찾을 수 없음"으로 터지지 않는다(10절에서 실제로 확인).
- **`LAYERRECOMMENDS`**: meta-oe는 "있으면 좋은" layer다. 반드시 필요한 것은 `LAYERDEPENDS`(core 하나)뿐이다.

### 2.2 `kas/donlab.yml` — 빌드 전체를 파일 하나로

```text
header:
  version: 14

machine: donlab-qemu
distro: donlab
target: donlab-image

repos:
  # this repository itself: the layer lives at the repo root
  meta-donlab:
    layers:
      .:

  bitbake:
    url: https://git.openembedded.org/bitbake
    branch: "2.18"
    layers:
      .: disabled

  openembedded-core:
    url: https://git.openembedded.org/openembedded-core
    branch: wrynose
    layers:
      meta:

  meta-yocto:
    url: https://git.yoctoproject.org/meta-yocto
    branch: wrynose
    layers:
      meta-poky:

  meta-openembedded:
    url: https://git.openembedded.org/meta-openembedded
    branch: wrynose
    layers:
      meta-oe:

local_conf_header:
  donlab-base: |
    INHERIT += "rm_work"
    DL_DIR ?= "${TOPDIR}/../downloads"
    SSTATE_DIR ?= "${TOPDIR}/../sstate-cache"
    BB_NUMBER_THREADS ?= "8"
    PARALLEL_MAKE ?= "-j 8"
    INHERIT += "buildhistory"
    BUILDHISTORY_COMMIT = "1"
```

(저장소 파일에는 각 줄 설명 주석이 더 있다.) kas 문법 자체는 K5 1.2절에 있다. 이 파일에서 새로 볼 것: `meta-donlab`에 `url`이 없으므로 "이 kas 파일이 든 저장소 자신"이고, `layers: .:`는 저장소 루트가 layer라는 뜻이다. `rm_work`는 레시피가 끝나면 WORKDIR을 지워 디스크를 크게 줄이고, `buildhistory`는 이미지마다 "무엇이 들어갔나"를 git으로 기록한다(13절).

### 2.3 실행 — `kas checkout`

`kas checkout`은 저장소를 받고 `bblayers.conf`·`local.conf`만 만든다. **bitbake는 실행하지 않는다.**

```sh
cd ~/.elinux_scratch/S6/meta-donlab
export KAS_WORK_DIR=/var/tmp/S6/work KAS_CLONE_DEPTH=1
/var/tmp/S6/venv/bin/kas checkout kas/donlab.yml 2>&1 | sed "s/^[0-9-]* [0-9:]* //" | tail -n 14
cat /var/tmp/S6/work/build/conf/bblayers.conf
du -sh /var/tmp/S6/work/*
```

```text
- WARNING  - Using branch without commit for repository "meta-yocto" is unsafe. Either add a commit or use a lock file.
- WARNING  - Using branch without commit for repository "meta-openembedded" is unsafe. Either add a commit or use a lock file.
- INFO     - Cloning repository bitbake
- INFO     - Cloning repository openembedded-core
- INFO     - Cloning repository meta-yocto
- INFO     - Cloning repository meta-openembedded
- INFO     - Repository bitbake already contains 2.18 as commit
- INFO     - Repository meta-yocto already contains wrynose as commit
- INFO     - Repository meta-openembedded already contains wrynose as commit
- INFO     - Repository openembedded-core already contains wrynose as commit
- INFO     - Repository bitbake checked out to 6e260eb6ab1631607ea414d9ff8ede1155faa5d9
- INFO     - Repository openembedded-core checked out to 636e5d3b12d54951c13d57346648bc29a0e6b24e
- INFO     - Repository meta-yocto checked out to 7c25b11156553145fa6b241a2d4f65d94438f804
- INFO     - Repository meta-openembedded checked out to f28a144aa0a0ae05ca2ee91e1aef6aa05f6a12db
BBLAYERS ?= " \
    ${TOPDIR}/../../../../../Users/donh/.elinux_scratch/S6/meta-donlab \
    ${TOPDIR}/../meta-openembedded/meta-oe \
    ${TOPDIR}/../meta-yocto/meta-poky \
    ${TOPDIR}/../openembedded-core/meta"
BBPATH ?= "${TOPDIR}"
BBFILES ??= ""
14M	/var/tmp/S6/work/bitbake
32K	/var/tmp/S6/work/build
54M	/var/tmp/S6/work/meta-openembedded
724K	/var/tmp/S6/work/meta-yocto
50M	/var/tmp/S6/work/openembedded-core
```

출력에서 볼 것: kas가 **경고를 먼저 띄운다** — 브랜치 이름만 적으면 다음 주에 다른 커밋을 받는다는 뜻이다. 그래서 2.5절에서 lock 파일을 만든다. 얕은 clone(`--depth 1`)이라 메타데이터 전체가 120 MB 남짓이다. 실제 빌드에서 크는 것은 메타데이터가 아니라 `downloads/`, `sstate-cache/`, `tmp/`다(11.4절).

### 2.4 설정 파싱 — layer가 제대로 붙었나

BitBake 환경을 잡는 셸 조각이다. HOSTTOOLS 검사에서 걸리는 `chrpath`/`diffstat`은 이 공용 VM에 없어서(apt 설치 금지) **파싱 전용** 가짜 스크립트를 PATH 끝에 붙였다 — 실제 빌드에서는 절대 이렇게 하면 안 된다(K4 2.2절).

```sh
# tools/s6_env.sh — source me inside the VM (parse only, no builds)
export BITBAKEDIR=/var/tmp/S6/work/bitbake
cd /var/tmp/S6/work/openembedded-core
. ./oe-init-build-env /var/tmp/S6/work/build > /dev/null 2>&1
export PATH=$PATH:/var/tmp/S6/fakebin
P="python3 /Users/donh/.elinux_scratch/S6/tools/s6_parse.py"
L=/Users/donh/.elinux_scratch/S6/meta-donlab
```

```sh
. ~/.elinux_scratch/S6/tools/s6_env.sh
time timeout 600 bitbake-layers show-layers 2>&1 | tail -8
```

```text
NOTE: Starting bitbake server...
layer                 path                                                                    priority
========================================================================================================
donlab                /var/tmp/S6/work/build/../../../../../Users/donh/.elinux_scratch/S6/meta-donlab  10
openembedded-layer    /var/tmp/S6/work/build/../meta-openembedded/meta-oe                     5
yocto                 /var/tmp/S6/work/build/../meta-yocto/meta-poky                          5
core                  /var/tmp/S6/work/build/../openembedded-core/meta                        5

real	0m1.791s
user	0m0.445s
sys	0m0.065s
```

출력에서 볼 것: 컬렉션 이름 `donlab`, priority 10. 이 명령은 **전역 설정 전체**(bitbake.conf → layer.conf 4개 → local.conf → `MACHINE=donlab-qemu`의 machine conf → `DISTRO=donlab`의 distro conf)를 파싱한다. 즉 3절의 machine/distro 파일에 문법 오류가 있으면 여기서 이미 실패한다. `LAYERSERIES_COMPAT`가 맞지 않으면 BitBake(`lib/bb/cookerdata.py`)가 `Layer %s is not compatible with the core layer which only supports these series: ...`로 `bb.fatal`을 낸다.

### 2.5 `kas lock` — 브랜치를 커밋으로 박제

VM에서는 저장소가 읽기 전용이라 lock 파일을 쓸 수 없다. 사본에서 만들고 Mac 쪽으로 가져왔다.

```sh
cp -a ~/.elinux_scratch/S6/meta-donlab /var/tmp/S6/lockrepo && cd /var/tmp/S6/lockrepo
KAS_WORK_DIR=/var/tmp/S6/work-lock KAS_CLONE_DEPTH=1 /var/tmp/S6/venv/bin/kas lock kas/donlab.yml 2>&1 \
  | sed "s/^[0-9-]* [0-9:]* //" | tail -2
cat kas/donlab.lock.yml
```

```text
- INFO     - Adding lock of meta-openembedded: f28a144aa0a0ae05ca2ee91e1aef6aa05f6a12db
- INFO     - Updating lockfile kas/donlab.lock.yml
header:
  version: 14
overrides:
  repos:
    bitbake:
      commit: 6e260eb6ab1631607ea414d9ff8ede1155faa5d9
    openembedded-core:
      commit: 636e5d3b12d54951c13d57346648bc29a0e6b24e
    meta-yocto:
      commit: 7c25b11156553145fa6b241a2d4f65d94438f804
    meta-openembedded:
      commit: f28a144aa0a0ae05ca2ee91e1aef6aa05f6a12db
```

출력에서 볼 것: kas는 `donlab.yml` 옆의 `donlab.lock.yml`을 자동으로 겹쳐 읽는다. 이 파일을 커밋하면 "meta-donlab v0.1 = 이 해시 4개 + 우리 커밋"이 된다. 벤더 layer를 올리는 일은 이 lock 파일을 바꾸는 PR이 된다(K5 1.3).

> 이 단계가 끝나면: `bitbake-layers show-layers`에 `donlab 10`이 보이고, `kas/donlab.lock.yml`이 커밋돼 있어야 한다.

### 2.6 이 단계의 흔한 실패

| 증상 | 원인 | 고치는 법 |
|---|---|---|
| `Layer donlab is not compatible with the core layer which only supports these series` | `LAYERSERIES_COMPAT`가 `scarthgap` 등 옛 이름 | `wrynose`로 (여러 릴리스 지원이면 공백으로 나열) |
| 레시피가 하나도 안 보임 | `BBFILES` 패턴이 디렉터리 깊이와 다름 (`recipes-*/*/*.bb`는 정확히 2단계) | 디렉터리를 `recipes-<분류>/<레시피>/`로 맞춤 |
| `kas checkout`이 layer를 못 찾음 | 저장소 루트가 layer인데 `layers:`에 이름을 적음 | `layers: .:` |
| VM에서 `git init`/`kas lock` 실패 | 홈이 VM에서 읽기 전용 | Mac에서 git, lock은 사본에서 |

---

## 3. Step 2 — machine과 distro

### 3.1 `conf/machine/donlab-qemu.conf`

```text
#@TYPE: Machine
#@NAME: donlab QEMU arm64 (virt, cortex-a57)
#@DESCRIPTION: qemuarm64 plus the donlab sensor overlay and serial console tweaks.

require conf/machine/qemuarm64.conf

# Keep every :qemuarm64 override of oe-core (linux-yocto KBRANCH/SRCREV, COMPATIBLE_MACHINE,
# runqemu settings) active for this machine. Without this line linux-yocto is skipped
# with "incompatible with machine donlab-qemu".
MACHINEOVERRIDES =. "qemuarm64:"

PREFERRED_PROVIDER_virtual/kernel ?= "linux-yocto"
PREFERRED_VERSION_linux-yocto ?= "6.18%"
KERNEL_DEVICETREE:append = " donlab/donlab-sensor.dtbo"

# our driver is part of the machine: images get it without listing it
MACHINE_EXTRA_RRECOMMENDS += "kernel-module-donlab-sensor"

SERIAL_CONSOLES = "115200;ttyAMA0"
# qemu.inc already adds tar.zst and ext4.zst; runqemu also accepts plain ext4
IMAGE_FSTYPES += "ext4"
```

핵심은 **`MACHINEOVERRIDES =. "qemuarm64:"`** 한 줄이다. 기존 machine을 `require`해서 새 machine을 만들면 변수 값은 다 가져오지만, **override 이름은 `MACHINE` 값(`donlab-qemu`)으로 바뀐다.** oe-core 곳곳의 `:qemuarm64` override(linux-yocto의 `KBRANCH:qemuarm64`, `SRCREV_machine:qemuarm64`, `COMPATIBLE_MACHINE`)가 전부 꺼지는 것이다. 6.4절에서 이 줄을 지웠을 때 커널 레시피가 실제로 건너뛰어지는 것을 보인다.

### 3.2 `conf/distro/donlab.conf`

```text
require conf/distro/poky.conf

DISTRO = "donlab"
DISTRO_NAME = "donlab Linux (sensor kit)"
DISTRO_VERSION = "0.1"
DISTRO_CODENAME = "s6"
MAINTAINER = "Don H <don@example.com>"

# poky defaults to sysvinit; the product uses systemd + merged /usr
INIT_MANAGER = "systemd"
DISTRO_FEATURES:append = " usrmerge"
DISTRO_FEATURES:remove = "x11 wayland vulkan opengl ptest"

PACKAGE_CLASSES = "package_ipk"

# Already inherited by oe-core defaultsetup.conf, listed here so reviewers see them:
#   security_flags.inc (-fstack-protector-strong, _FORTIFY_SOURCE, relro/now)
#   create-spdx (SBOM per image, K7)
# License policy: enable after checking the image still resolves (bash is GPL-3.0-or-later).
# INCOMPATIBLE_LICENSE = "GPL-3.0* LGPL-3.0* AGPL-3.0*"
```

`INIT_MANAGER = "systemd"`를 꼭 적어야 하는 이유를 소스에서 확인했다.

```sh
cd /var/tmp/S6/work
grep -n "INIT_MANAGER" meta-yocto/meta-poky/conf/distro/poky.conf openembedded-core/meta/conf/distro/defaultsetup.conf
grep -n "security_flags\|INHERIT_DISTRO ?=" openembedded-core/meta/conf/distro/defaultsetup.conf
```

```text
meta-yocto/meta-poky/conf/distro/poky.conf:68:POKY_INIT_MANAGER = "sysvinit"
meta-yocto/meta-poky/conf/distro/poky.conf:69:INIT_MANAGER ?= "${POKY_INIT_MANAGER}"
openembedded-core/meta/conf/distro/defaultsetup.conf:20:INIT_MANAGER ??= "systemd"
openembedded-core/meta/conf/distro/defaultsetup.conf:9:require conf/distro/include/security_flags.inc
openembedded-core/meta/conf/distro/defaultsetup.conf:17:INHERIT_DISTRO ?= "debian devshell sstate license remove-libtool create-spdx buildstats uninative"
```

출력에서 볼 것: oe-core 기본(`nodistro`)은 systemd지만 **poky는 아직 sysvinit**이다. poky를 `require`한 우리 distro는 명시하지 않으면 sysvinit 이미지가 된다 — 그러면 5절의 systemd unit이 설치는 되어도 아무도 실행하지 않는다. 하드닝 플래그와 SPDX는 이미 기본이라 다시 적지 않았다(주석으로만 남김). `INCOMPATIBLE_LICENSE`는 주석으로 둔 이유가 있다: GPLv3를 막으면 bash처럼 GPL-3.0인 패키지를 끌어오는 경로가 전부 "nothing provides" 에러가 된다. 켜기 전에 의존성 정리가 먼저다(K7).

### 3.3 실행 — 최종 값 확인 (`bitbake-getvar`)

```sh
. ~/.elinux_scratch/S6/tools/s6_env.sh
for v in MACHINEOVERRIDES DISTRO_FEATURES INIT_MANAGER TUNE_PKGARCH KERNEL_DEVICETREE IMAGE_FSTYPES MACHINE_EXTRA_RRECOMMENDS; do
  bitbake-getvar --value $v 2>/dev/null | sed "s/^/$v = /"
done
```

```text
MACHINEOVERRIDES = qemuarm64:qemuall:aarch64:donlab-qemu
DISTRO_FEATURES =  usrmerge systemd usrmerge      acl alsa bluetooth debuginfod ext2 ipv4 ipv6     wifi xattr nfs zeroconf pci 3g nfc  vfat seccomp pulseaudio     gobject-introspection-data ldconfig   multiarch       
INIT_MANAGER = systemd
TUNE_PKGARCH = cortexa57
KERNEL_DEVICETREE =  donlab/donlab-sensor.dtbo
IMAGE_FSTYPES =  tar.zst ext4.zst ext4
MACHINE_EXTRA_RRECOMMENDS =  kernel-module-donlab-sensor
```

출력에서 볼 것:

- `MACHINEOVERRIDES`의 **맨 앞에 `qemuarm64`**, 맨 끝에 `donlab-qemu`. 뒤에 있을수록 우선순위가 높다 — 같은 변수에 `:qemuarm64`와 `:donlab-qemu` override가 둘 다 있으면 우리 것이 이긴다.
- `DISTRO_FEATURES`에서 `x11 wayland vulkan opengl ptest`가 빠졌고 `systemd`가 들어왔다. `INIT_MANAGER = "systemd"`가 고르는 `conf/distro/include/init-manager-systemd.inc`의 첫 줄이 `DISTRO_FEATURES:append = " systemd usrmerge"`다 — 그래서 `usrmerge`가 두 번 보인다(무해하지만 우리 append는 사실 군더더기다).
- `KERNEL_DEVICETREE`에 qemuarm64 원본에는 없던 `.dtbo` 하나. QEMU는 DT를 스스로 만들기 때문에 qemuarm64는 원래 dtb가 없다(6.5절).

> 이 단계가 끝나면: `bitbake-getvar --value MACHINEOVERRIDES`의 첫 항목이 `qemuarm64`, `INIT_MANAGER`가 `systemd`여야 한다.

---

## 4. Step 3 — 드라이버와 out-of-tree 모듈 레시피

### 4.1 드라이버 — 이 프로젝트의 "센서"

S2(char device)나 S4(IIO 드라이버)가 끝나면 그 소스로 바꿔 끼우면 된다. 이 노트를 쓸 때 S2/S4 소스가 아직 없어서, **레시피 모양을 보여 주기에 충분한 작은 드라이버**를 직접 썼다: DT `compatible = "donlab,temp-sensor"`로 바인딩되는 platform driver가 misc device `/dev/donlab_sensor`를 만들고, `read()` 한 번에 샘플 한 줄(`seq=<n> temp_mC=<t>`)을 준다. 값은 가짜(삼각파)지만 **platform_driver + of_match_table + device property + miscdevice**라는 뼈대는 실제 메모리 맵 센서 드라이버와 같다(G1, G2). 핵심 부분만 옮긴다(전체 132줄은 저장소에).

```c
static ssize_t donlab_read(struct file *f, char __user *buf, size_t len, loff_t *ppos)
{
	struct donlab_sensor *s = container_of(f->private_data, struct donlab_sensor, misc);
	char line[48];
	int seq = atomic_inc_return(&s->seq);
	int tri = seq % 40 < 20 ? seq % 40 : 40 - seq % 40;  /* 0..20..0 */
	int n = scnprintf(line, sizeof(line), "seq=%d temp_mC=%d\n",
			  seq, (int)s->base_mC + tri * 100 - 1000);

	/* one sample per read(): ignore *ppos so that cat/read loops keep going */
	if (len < n)
		return -EINVAL;
	if (copy_to_user(buf, line, n))
		return -EFAULT;
	return n;
}

static int donlab_probe(struct platform_device *pdev)
{
	struct donlab_sensor *s;
	int ret;

	s = devm_kzalloc(&pdev->dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->base_mC = 25000;
	device_property_read_u32(&pdev->dev, "donlab,base-millicelsius", &s->base_mC);

	s->misc.minor = MISC_DYNAMIC_MINOR;
	s->misc.name = "donlab_sensor";
	s->misc.fops = &donlab_fops;
	s->misc.mode = 0444;
	s->misc.parent = &pdev->dev;
	ret = misc_register(&s->misc);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "misc_register failed\n");

	platform_set_drvdata(pdev, s);
	dev_info(&pdev->dev, "donlab_sensor ready, base=%u mC, node=%s\n",
		 s->base_mC, dev_fwnode(&pdev->dev) ? "DT" : "none");
	return 0;
}

static const struct of_device_id donlab_of_match[] = {
	{ .compatible = "donlab,temp-sensor" },
	{ }
};
MODULE_DEVICE_TABLE(of, donlab_of_match);
```

나머지는 `platform_driver` 등록과, DT 노드가 없는 보드를 위한 **`fake_device=1` 모듈 파라미터**(켜면 `platform_device_register_simple()`로 장치를 직접 만든다)다. QEMU의 `runqemu`는 DT overlay를 적용하지 않으므로, donlab-qemu 이미지에서는 이 파라미터로 장치를 만든다(4.3절의 `module_conf_*`). `misc.mode = 0444`라서 udev 규칙 없이도 일반 사용자가 읽을 수 있다 — 5절의 `DynamicUser` 서비스가 이 덕을 본다.

Makefile은 module.bbclass가 넘기는 `KERNEL_SRC`를 받는 표준 Kbuild 래퍼다(K5 4.3과 같은 모양).

```make
obj-m := donlab_sensor.o

SRC := $(shell pwd)
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) clean
```

### 4.2 레시피 `donlab-sensor-mod_0.1.bb`

```text
SUMMARY = "donlab fake temperature sensor driver (out-of-tree kernel module)"
DESCRIPTION = "Platform driver bound by DT compatible donlab,temp-sensor; exposes /dev/donlab_sensor."
HOMEPAGE = "https://github.com/donh/meta-donlab"
SECTION = "kernel/modules"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

inherit module

SRC_URI = "file://Makefile \
           file://donlab_sensor.c \
          "
S = "${UNPACKDIR}"

RPROVIDES:${PN} += "kernel-module-donlab-sensor"

# Write ${nonarch_libdir}/modules-load.d/donlab_sensor.conf -> systemd-modules-load loads it at boot
KERNEL_MODULE_AUTOLOAD += "donlab_sensor"

# On the QEMU machine there is no overlay loader in the boot path: register a fake device.
KERNEL_MODULE_PROBECONF += "donlab_sensor"
module_conf_donlab_sensor:donlab-qemu = "options donlab_sensor fake_device=1"
```

새로 볼 변수 세 개:

- **`KERNEL_MODULE_AUTOLOAD`**: `kernel-module-split.bbclass`가 do_package에서 `modules-load.d/donlab_sensor.conf`(내용 = 모듈 이름 한 줄)를 만들어 `kernel-module-donlab-sensor` 패키지에 넣는다. 부팅 때 `systemd-modules-load.service`가 이 파일을 읽어 `modprobe`한다. 이 디렉터리는 systemd distro면 `/usr/lib/modules-load.d`, 아니면 `/etc/modules-load.d`다.
- **`KERNEL_MODULE_PROBECONF` + `module_conf_<모듈>`**: 같은 class가 `/etc/modprobe.d/<모듈>.conf`를 만든다. 둘을 같이 써야 한다 — `module_conf_*`만 있고 PROBECONF에 이름이 없으면 class가 에러를 낸다(아래 grep 결과의 136행).
- `:donlab-qemu` override 덕분에 **실제 보드 machine에서는 `fake_device=1`이 들어가지 않는다** — 거기서는 DT overlay가 노드를 만든다.

이 두 동작은 oe-core 소스에서 그대로 확인된다.

```sh
grep -n "KERNEL_MODULE_AUTOLOAD\|module_conf\|modules-load.d" \
  /var/tmp/S6/work/openembedded-core/meta/classes-recipe/kernel-module-split.bbclass | head
```

```text
33:modulesloaddir ??= "${@bb.utils.contains('DISTRO_FEATURES', 'systemd', '${nonarch_libdir}', '${sysconfdir}', d)}/modules-load.d"
92:        autoloadlist = (d.getVar("KERNEL_MODULE_AUTOLOAD") or "").split()
95:            bb.warn("module_autoload_%s was replaced by KERNEL_MODULE_AUTOLOAD for cases where basename == module name, please drop it" % basename)
97:            bb.warn("module_autoload_%s is defined but '%s' isn't included in KERNEL_MODULE_AUTOLOAD, please add it there" % (basename, basename))
102:        # If module name is in KERNEL_MODULE_AUTOLOAD, then generate the .conf file and write to `name`.
125:        modconf = d.getVar('module_conf_%s' % basename)
127:        # The .conf file can either be installed by a recipe or generated from module_conf_*
136:            bb.error("Please ensure module %s is listed in KERNEL_MODULE_PROBECONF since module_conf_%s is set" % (basename, basename))
138:        # It doesn't matter if it was generated from module_conf_* or installed by the recipe.
245:do_package[vardeps] += '${@" ".join(map(lambda s: "module_conf_" + s, (d.getVar("KERNEL_MODULE_PROBECONF") or "").split()))}'
```

### 4.3 실행 — BitBake 파서로 레시피 검증

`tools/s6_parse.py`는 tinfoil로 **설정 파싱 + 레시피 하나 파싱**만 하고 변수를 찍는다(태스크 실행 없음, K4 4.3과 같은 방식). 레시피가 `SkipRecipe`로 건너뛰어지면 그 이유를 찍고 종료 코드 3을 낸다.

```python
#!/usr/bin/env python3
"""S6: parse ONE recipe (+ its bbappends) with BitBake tinfoil and print variables.
No task is executed. usage: s6_parse.py RECIPE.bb [+append=X.bbappend ...] VAR ... [@tasks]"""
import sys
sys.path.insert(0, "/var/tmp/S6/work/bitbake/lib")
import bb.tinfoil

recipe, args = sys.argv[1], sys.argv[2:]
appends = [a[len("+append="):] for a in args if a.startswith("+append=")]
names = [a for a in args if not a.startswith("+")]
with bb.tinfoil.Tinfoil() as tf:
    tf.prepare(config_only=True, quiet=2)
    try:
        d = tf.parse_recipe_file(recipe, appends=bool(appends), appendlist=appends or None)
    except bb.parse.SkipRecipe as e:
        print(f"SKIPPED: {e}")
        sys.exit(3)
    if d.getVar("__SKIPPED"):
        print(f"SKIPPED: {d.getVar('__SKIPPED')}")
        sys.exit(3)
    for n in names:
        if n == "@tasks":
            tasks = d.getVar("__BBTASKS", False)
            print(" ".join(t.replace("do_", "") for t in tasks))
        elif n.startswith("@flag:"):
            v, f = n[6:].split("[")
            print(f"{v}[{f[:-1]}] = {d.getVarFlag(v, f[:-1])!r}")
        else:
            val = d.getVar(n)
            print(f"{n:28} = {' '.join(val.split()) if val else val!r}")
```

```sh
. ~/.elinux_scratch/S6/tools/s6_env.sh
R=$L/recipes-kernel/donlab-sensor-mod/donlab-sensor-mod_0.1.bb
$P $R PN PV PACKAGES KERNEL_MODULE_AUTOLOAD module_conf_donlab_sensor DEPENDS EXTRA_OEMAKE PACKAGE_ARCH 2>&1 | grep -v ^NOTE
$P $R RPROVIDES:donlab-sensor-mod @flag:do_configure[depends] KERNEL_VERSION 2>&1 | grep -v ^NOTE
```

```text
PN                           = 'donlab-sensor-mod'
PV                           = '0.1'
PACKAGES                     = 'donlab-sensor-mod-src donlab-sensor-mod-dbg donlab-sensor-mod-staticdev donlab-sensor-mod-dev donlab-sensor-mod-doc donlab-sensor-mod-locale donlab-sensor-mod'
KERNEL_MODULE_AUTOLOAD       = 'donlab_sensor'
module_conf_donlab_sensor    = 'options donlab_sensor fake_device=1'
DEPENDS                      = 'pkgconf-native gcc-cross-aarch64 virtual/compilerlibs virtual/libc'
EXTRA_OEMAKE                 = 'KERNEL_SRC=/var/tmp/S6/work/build/tmp/work-shared/donlab-qemu/kernel-source'
PACKAGE_ARCH                 = 'donlab_qemu'
RPROVIDES:donlab-sensor-mod  = 'kernel-module-donlab-sensor'
do_configure[depends] = 'make-mod-scripts:do_compile'
do_compile[depends] = ''
KERNEL_VERSION               = ''
```

출력에서 볼 것:

- `module_conf_donlab_sensor`에 `:donlab-qemu` override가 적용된 값이 보인다 — override가 실제로 먹었다는 증거.
- `PACKAGE_ARCH = 'donlab_qemu'`: 커널 모듈은 **machine 전용 패키지**다(커널 버전·설정에 묶이니까). 일반 앱(`cortexa57`)과 패키지 피드가 다르다.
- `kernel-module-donlab-sensor`는 아직 `PACKAGES`에 없다. 이 패키지는 do_package 때 `.ko` 파일 이름을 보고 **동적으로** 만들어진다(`_`→`-`). 그래서 `RPROVIDES`로 메타 패키지에 같은 이름을 걸어 둔다.
- `DEPENDS`에 커널이 없다. 대신 `do_configure[depends] = 'make-mod-scripts:do_compile'` — 태스크 단위 의존성으로 "커널 빌드 트리 + modpost 스크립트가 준비된 뒤"를 보장한다. `KERNEL_VERSION`이 파싱 시점에 빈 것도 같은 이유다: 이 값은 빌드 중 work-shared의 커널 트리에서 읽힌다.

### 4.4 실행 — Yocto 밖에서 같은 방식으로 빌드

module.bbclass는 결국 `make KERNEL_SRC=<커널 빌드 트리>`와 `make modules_install MODLIB=<D>/... DEPMOD=echo`를 부른다(K5 4.3에서 bbclass를 grep했다). CMake 레시피도 `cmake -G Ninja` → `ninja` → `DESTDIR=<D> ninja install`이다. `tests/build-outside-yocto.sh`는 이 두 흐름을 랩 커널 빌드 트리(6.18.55-trace)로 그대로 재현하고, 마지막에 `KERNEL_MODULE_AUTOLOAD`·`module_conf_*`가 만들 파일 두 개도 같은 경로에 만든다.

```sh
#!/bin/bash
set -euo pipefail
LAYER=${LAYER:-/Users/donh/.elinux_scratch/S6/meta-donlab}
KB=${KB:-/var/lab/out/linux-build-trace}
OUT=${OUT:-/var/tmp/S6/out}
KVER=$(cat "$KB/include/config/kernel.release")
rm -rf "$OUT"; mkdir -p "$OUT/mod" "$OUT/app" "$OUT/image"

cp "$LAYER"/recipes-kernel/donlab-sensor-mod/files/* "$OUT/mod/"
make -s -C "$OUT/mod" KERNEL_SRC="$KB" -j2 all
make -s -C "$OUT/mod" KERNEL_SRC="$KB" DEPMOD=echo \
     MODLIB="$OUT/image/usr/lib/modules/$KVER" INSTALL_MOD_STRIP=1 modules_install >/dev/null

cp "$LAYER"/recipes-apps/donlab-sensord/files/* "$OUT/app/"
cmake -S "$OUT/app" -B "$OUT/app/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr -DSYSTEMD_UNITDIR=/usr/lib/systemd/system >/dev/null
ninja -C "$OUT/app/build" >/dev/null
DESTDIR="$OUT/image" ninja -C "$OUT/app/build" install >/dev/null
# static copy for the BusyBox initramfs smoke test (no glibc there)
gcc -std=gnu11 -Wall -Wextra -O2 -static "$OUT/app/donlab_sensord.c" -o "$OUT/donlab-sensord.static"

# what KERNEL_MODULE_AUTOLOAD / module_conf_* would generate in do_package
install -d "$OUT/image/usr/lib/modules-load.d" "$OUT/image/etc/modprobe.d"
echo donlab_sensor > "$OUT/image/usr/lib/modules-load.d/donlab_sensor.conf"
echo "options donlab_sensor fake_device=1" > "$OUT/image/etc/modprobe.d/donlab_sensor.conf"
echo "kernel $KVER"; (cd "$OUT/image" && find . -type f | sort)
```

```sh
time bash ~/.elinux_scratch/S6/meta-donlab/tests/build-outside-yocto.sh 2>&1
modinfo /var/tmp/S6/out/mod/donlab_sensor.ko | grep -E "^(alias|vermagic|parm|license|name)"
```

```text
kernel 6.18.55-trace
./etc/modprobe.d/donlab_sensor.conf
./usr/bin/donlab-sensord
./usr/lib/modules-load.d/donlab_sensor.conf
./usr/lib/modules/6.18.55-trace/updates/donlab_sensor.ko
./usr/lib/systemd/system/donlab-sensord.service

real	0m2.188s
user	0m1.452s
sys	0m0.812s
license:        GPL
alias:          of:N*T*Cdonlab,temp-sensorC*
alias:          of:N*T*Cdonlab,temp-sensor
name:           donlab_sensor
vermagic:       6.18.55-trace SMP preempt mod_unload aarch64
parm:           fake_device:register a platform device without DT (QEMU/no overlay) (bool)
```

출력에서 볼 것:

- 경고 0개(모듈은 Kbuild 기본 경고, 앱은 `-Wall -Wextra`).
- `find` 결과가 **이미지 rootfs에 들어갈 파일 목록 그 자체**다. Yocto에서는 이 5개 파일이 두 패키지로 갈린다 — `kernel-module-donlab-sensor`(`.ko` + `modules-load.d` + `modprobe.d`)와 `donlab-sensord`(바이너리 + unit). 실제 빌드에서는 `oe-pkgdata-util list-pkg-files kernel-module-donlab-sensor`로 대조한다(가이드).
- 외부 모듈은 `INSTALL_MOD_DIR`의 기본값인 `updates/` 아래에 설치된다 — 커널 트리 모듈(`kernel/`)과 섞이지 않는다.
- `alias: of:N*T*Cdonlab,temp-sensor`는 `MODULE_DEVICE_TABLE(of, ...)`이 만든 modalias다. DT에 이 compatible 노드가 있으면 udev가 이 alias로 모듈을 **자동 로드**할 수도 있다(G9). `KERNEL_MODULE_AUTOLOAD`는 "노드가 없어도 무조건" 로드하는 경로다.
- 2초는 VM 네이티브 빌드 시간이다. Yocto에서는 이 레시피 하나도 fetch → … → package_qa → sstate 기록까지 수십 초~수 분이 든다(기억 기반).

> 이 단계가 끝나면: `modinfo`에 `vermagic 6.18.55-trace`와 `of:` alias가 보이고, 설치 트리에 파일 5개가 있어야 한다.

---

## 5. Step 4 — 센서 데몬: CMake + systemd 레시피

### 5.1 데몬과 unit

데몬은 `/dev/donlab_sensor`를 주기적으로 읽어 창(window)마다 min/avg/max를 stdout에 찍는다. systemd 아래에서는 stdout이 그대로 journal로 간다 — 로그 파일·syslog 코드가 필요 없다(L1). 메인 루프만 옮긴다(전체 88줄).

```c
	int fd = open(dev, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", dev, strerror(errno));
		return 1;
	}
	printf("donlab-sensord: reading %s every %ld ms, window %ld\n", dev, period_ms, window);
	fflush(stdout);

	long min = LONG_MAX, max = LONG_MIN, sum = 0, n = 0, seq = 0, t = 0;
	struct timespec ts = { period_ms / 1000, (period_ms % 1000) * 1000000L };

	while (!stop && (total == 0 || count < total)) {
		if (read_sample(fd, &seq, &t) < 0) {
			fprintf(stderr, "bad sample from %s\n", dev);
			close(fd);
			return 1;
		}
		count++;
		min = t < min ? t : min;
		max = t > max ? t : max;
		sum += t;
		if (++n == window) {
			printf("seq=%ld n=%ld min=%ld avg=%ld max=%ld mC\n", seq, n, min, sum / n, max);
			fflush(stdout);
			min = LONG_MAX; max = LONG_MIN; sum = 0; n = 0;
		}
		nanosleep(&ts, NULL);
	}
```

`fflush(stdout)`가 중요하다: stdout이 파이프(journald)면 C 라이브러리가 4 KB 단위로 버퍼링해서, 플러시하지 않으면 로그가 몇 분씩 늦게 나타난다. SIGTERM 핸들러는 `stop` 플래그만 세운다 — `systemctl stop`이 깔끔하게 끝난다.

```text
[Unit]
Description=donlab fake temperature sensor logger
Documentation=https://github.com/donh/meta-donlab
# the driver is loaded by systemd-modules-load (KERNEL_MODULE_AUTOLOAD)
After=systemd-modules-load.service
ConditionPathExists=/dev/donlab_sensor

[Service]
Type=simple
ExecStart=/usr/bin/donlab-sensord -p 200 -w 10
Restart=on-failure
RestartSec=2
# hardening: throw-away uid, read-only OS, only our device node is reachable
DynamicUser=yes
DevicePolicy=closed
DeviceAllow=/dev/donlab_sensor r
ProtectSystem=strict
ProtectHome=yes
PrivateNetwork=yes
NoNewPrivileges=yes

[Install]
WantedBy=multi-user.target
```

이것이 **첫 버전**이다. 8.3절에서 `systemd-analyze security`로 노출 점수를 재고 줄을 더 추가한다(저장소에는 최종본이 있다). 순서와 조건이 드라이버와 맞물린다: `After=systemd-modules-load.service`(모듈이 먼저 올라오고) + `ConditionPathExists=/dev/donlab_sensor`(장치가 없으면 실패가 아니라 "조건 불충족으로 건너뜀"). 하드닝은 L1·L6의 내용을 그대로 적용했다 — 매번 바뀌는 임시 uid, OS 읽기 전용, 네트워크 없음, 장치는 우리 노드 하나만 읽기.

CMakeLists.txt는 unit 설치 위치를 캐시 변수로 받는다. 레시피가 Yocto의 `${systemd_system_unitdir}`를 넘겨 주기 위해서다 — 경로를 하드코딩하면 usrmerge QA에 걸릴 수 있다(12절).

```text
cmake_minimum_required(VERSION 3.16)
project(donlab-sensord VERSION 0.1 LANGUAGES C)
include(GNUInstallDirs)

option(DONLAB_INSTALL_SYSTEMD_UNIT "install the systemd service file" ON)
set(SYSTEMD_UNITDIR "${CMAKE_INSTALL_PREFIX}/lib/systemd/system" CACHE PATH "systemd system unit dir")

add_executable(donlab-sensord donlab_sensord.c)
set_property(TARGET donlab-sensord PROPERTY C_STANDARD 11)
target_compile_options(donlab-sensord PRIVATE -Wall -Wextra)
target_compile_definitions(donlab-sensord PRIVATE _GNU_SOURCE)

install(TARGETS donlab-sensord RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(DONLAB_INSTALL_SYSTEMD_UNIT)
  install(FILES donlab-sensord.service DESTINATION ${SYSTEMD_UNITDIR})
endif()
```

### 5.2 레시피 `donlab-sensord_0.1.bb`

```text
SUMMARY = "donlab sensor logger daemon"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=d4cdde6f5ce4990483ff5aeaabcfc4c5"

# In a real product the source lives in its own git repo:
#   SRC_URI = "git://github.com/donh/donlab-sensord.git;protocol=https;branch=main"
#   SRCREV = "<40-hex commit>"
SRC_URI = "file://CMakeLists.txt \
           file://donlab_sensord.c \
           file://donlab-sensord.service \
           file://LICENSE \
          "
S = "${UNPACKDIR}"

inherit cmake systemd

EXTRA_OECMAKE += " \
    -DSYSTEMD_UNITDIR=${systemd_system_unitdir} \
    -DDONLAB_INSTALL_SYSTEMD_UNIT=${@bb.utils.contains('DISTRO_FEATURES', 'systemd', 'ON', 'OFF', d)} \
"

SYSTEMD_SERVICE:${PN} = "donlab-sensord.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

RRECOMMENDS:${PN} += "kernel-module-donlab-sensor"
```

- `LIC_FILES_CHKSUM`의 md5는 `md5 -q files/LICENSE`로 구한 값이다. 라이선스 파일이 한 글자라도 바뀌면 do_populate_lic가 실패한다 — 의도된 동작이다(라이선스가 바뀌었는지 사람이 확인하라는 뜻, K7).
- `inherit systemd` + `SYSTEMD_SERVICE`: 패키지에 postinst를 넣어 이미지 생성 때 `systemctl enable`과 같은 심볼릭 링크를 만든다. 그래서 rootfs가 읽기 전용이어도 첫 부팅부터 서비스가 켜져 있다.
- `RRECOMMENDS`(약한 의존성): 드라이버를 뺀 이미지도 빌드는 되게 한다. 꼭 있어야 하면 `RDEPENDS`.

### 5.3 실행 — 파서로 확인

```sh
. ~/.elinux_scratch/S6/tools/s6_env.sh
R=$L/recipes-apps/donlab-sensord/donlab-sensord_0.1.bb
$P $R PACKAGES SYSTEMD_SERVICE:donlab-sensord systemd_system_unitdir EXTRA_OECMAKE OECMAKE_GENERATOR B DEPENDS RRECOMMENDS:donlab-sensord 2>&1 | grep -v ^NOTE
```

```text
PACKAGES                     = 'donlab-sensord-src donlab-sensord-dbg donlab-sensord-staticdev donlab-sensord-dev donlab-sensord-doc donlab-sensord-locale donlab-sensord'
SYSTEMD_SERVICE:donlab-sensord = 'donlab-sensord.service'
systemd_system_unitdir       = '/usr/lib/systemd/system'
EXTRA_OECMAKE                = '-DSYSTEMD_UNITDIR=/usr/lib/systemd/system -DDONLAB_INSTALL_SYSTEMD_UNIT=ON'
OECMAKE_GENERATOR            = 'Ninja'
B                            = '/var/tmp/S6/work/build/tmp/work/cortexa57-poky-linux/donlab-sensord/0.1/build'
DEPENDS                      = 'cmake-native gcc-cross-aarch64 virtual/compilerlibs virtual/libc ninja-native systemd-systemctl-native'
RRECOMMENDS:donlab-sensord   = 'kernel-module-donlab-sensor'
```

출력에서 볼 것: inline Python(`bb.utils.contains`)이 `ON`으로 풀렸다 — distro에 systemd가 있다는 뜻. `B`가 `.../build`로 `S`와 갈렸다(cmake class의 out-of-tree 빌드). `DEPENDS`에 우리가 적지 않은 `systemd-systemctl-native`가 들어 있다 — `inherit systemd`가 이미지 생성 때 쓸 호스트용 systemctl을 끌어온 것이다. WORKDIR 경로의 `cortexa57-poky-linux`에서 `poky`는 TARGET_VENDOR다 — 우리 distro가 poky를 require했으니 그대로 남는다.

> 이 단계가 끝나면: `EXTRA_OECMAKE`에 `/usr/lib/systemd/system`과 `ON`이 보여야 한다. `OFF`면 distro에 systemd가 없다는 뜻이다(3절로 돌아간다).

---

## 6. Step 5 — 커널 `.bbappend`: config fragment + DT overlay 패치

### 6.1 무엇을 바꾸나

| 바꿀 것 | 파일 | 커널 레시피에서 처리하는 태스크 |
|---|---|---|
| 커널 설정 10개 옵션 | `files/donlab.cfg` | do_kernel_metadata → do_kernel_configme → do_kernel_configcheck |
| DT overlay 소스 + Makefile | `files/0001-arm64-dts-add-donlab-temperature-sensor-overlay.patch` | do_patch → do_compile(`dtbs`) → do_deploy |
| BSP 설명 찾기 | `KMACHINE:donlab-qemu = "qemuarm64"` | do_kernel_metadata |

fragment와 `.scc`, kernel-yocto 태스크 파이프라인은 K5 3.2–3.3에 있다. 여기서는 **우리 파일을 만들고 검증하는 절차**만 다룬다.

### 6.2 `linux-yocto_%.bbappend`

```text
# meta-donlab: kernel changes for the donlab-qemu machine only.
# Everything is guarded by the machine override, so adding this layer to
# another machine's build does not change its kernel (yocto-check-layer rule).
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:donlab-qemu = " \
    file://donlab.cfg \
    file://0001-arm64-dts-add-donlab-temperature-sensor-overlay.patch \
"

# linux-yocto looks up the BSP description by KMACHINE in yocto-kernel-cache;
# there is no "donlab-qemu" entry there, reuse the qemuarm64 one.
KMACHINE:donlab-qemu = "qemuarm64"

# do_kernel_configcheck: report fragment values that did not survive (level 1 = default)
# and turn those warnings into a build error, so a lost CONFIG_ never ships silently.
KCONF_AUDIT_LEVEL:donlab-qemu = "1"
KMETA_AUDIT_WERROR:donlab-qemu = "1"
```

세 가지 함정을 막는 줄이다.

- **`FILESEXTRAPATHS:prepend :=`**: 반드시 즉시 확장(`:=`). `=`로 쓰면 `${THISDIR}`가 나중에 확장되면서 원본 레시피 디렉터리를 가리켜 `file://donlab.cfg`를 못 찾는다(9절 lint가 잡는다).
- **모든 변경에 `:donlab-qemu`**: K4 9절 끝에서 남긴 숙제의 답이다. layer를 추가하기만 해도 다른 machine의 커널이 바뀌면 `yocto-check-layer`가 실패한다.
- **`KMACHINE`**: kernel-yocto는 `KMACHINE ?= "${MACHINE}"`로 yocto-kernel-cache에서 `donlab-qemu`라는 BSP 설명을 찾는다. 없으니 `Could not locate BSP definition for ...`로 do_kernel_metadata가 죽는다. 소스에서 확인한 줄이다.

```sh
grep -n "KMACHINE ?=\|Could not locate BSP\|KMETA_AUDIT_WERROR ?=\|promoting to fatal" \
  /var/tmp/S6/work/openembedded-core/meta/classes-recipe/kernel-yocto.bbclass
```

```text
19:KMACHINE ?= "${MACHINE}"
29:KMETA_AUDIT_WERROR ?= ""
258:			bbfatal_log "Could not locate BSP definition for ${KMACHINE}/${LINUX_KERNEL_TYPE} and no defconfig was provided"
688:        bb.fatal( "configuration warnings detected, werror is set, promoting to fatal" )
```

### 6.3 DT overlay 패치 만들기

overlay 소스는 D6에서 배운 형식 그대로다. 루트에 노드 하나를 붙인다.

```dts
// SPDX-License-Identifier: GPL-2.0-only OR MIT
/dts-v1/;
/plugin/;

&{/} {
	donlab_sensor: donlab-sensor {
		compatible = "donlab,temp-sensor";
		donlab,base-millicelsius = <30000>;
		status = "okay";
	};
};
```

패치는 랩 커널 소스(읽기 전용)의 `arch/arm64/boot/dts/Makefile`만 임시 git 저장소에 복사해서 `git format-patch`로 만든다. 커널 트리 전체를 복사하지 않아도 된다.

```sh
#!/bin/bash
# build 0001-*.patch for linux-yocto: adds arch/arm64/boot/dts/donlab/ (overlay + Makefile)
set -euo pipefail
K=/var/lab/src/linux-6.18.55; W=/var/tmp/S6/dtpatch; S=/Users/donh/.elinux_scratch/S6/dt
rm -rf "$W"; mkdir -p "$W/arch/arm64/boot/dts"; cd "$W"
cp "$K/arch/arm64/boot/dts/Makefile" arch/arm64/boot/dts/
git init -q && git add -A && git -c user.name="Don H" -c user.email=don@example.com commit -qm base
mkdir arch/arm64/boot/dts/donlab
cp "$S/donlab-sensor.dtso" "$S/Makefile" arch/arm64/boot/dts/donlab/
sed -i 's/^subdir-y += cix$/subdir-y += cix\nsubdir-y += donlab/' arch/arm64/boot/dts/Makefile
git add -A
git -c user.name="Don H" -c user.email=don@example.com commit -q \
  -m "arm64: dts: add donlab temperature sensor overlay" \
  -m "Overlay that adds a donlab,temp-sensor node at the root. Built as donlab/donlab-sensor.dtbo." \
  -m "Upstream-Status: Inappropriate [lab-only example device]"
git format-patch -1 --no-signature -o /var/tmp/S6/ >/dev/null
ls /var/tmp/S6/*.patch
```

```sh
bash ~/.elinux_scratch/S6/dt/mkpatch.sh
cd /var/lab/src/linux-6.18.55 && git apply --check --stat -p1 /var/tmp/S6/0001-*.patch && echo "apply-check: OK"
```

```text
/var/tmp/S6/0001-arm64-dts-add-donlab-temperature-sensor-overlay.patch
 arch/arm64/boot/dts/Makefile                  |    1 +
 arch/arm64/boot/dts/donlab/Makefile           |    2 ++
 arch/arm64/boot/dts/donlab/donlab-sensor.dtso |   15 +++++++++++++++
 3 files changed, 18 insertions(+)
apply-check: OK
```

출력에서 볼 것: `git apply --check`는 **아무것도 쓰지 않고** 적용 가능 여부만 본다 — 읽기 전용 랩 소스에도 쓸 수 있다. 패치 본문에는 `Upstream-Status:` 줄이 있다. oe-core는 패치마다 이 태그를 요구하고, 없으면 `patch-status` QA로 경고·에러를 낸다(우리 layer에 적용되는 수준은 설정에 따라 다르다). 실무에서 리뷰어가 가장 먼저 보는 줄이다: "이 패치를 upstream에 보낼 건가, 못 보내면 왜인가".

주의: 이 패치는 랩 소스 **6.18.55** 기준으로 만들었고, wrynose의 linux-yocto는 **6.18.52**(`LINUX_VERSION`, 6.6절)다. 우리가 고친 `Makefile` 문맥(`subdir-y += cix` 근처)은 두 버전에서 같을 가능성이 높지만, 실제 빌드 전에 `devtool modify linux-yocto` 트리에서 다시 만드는 것이 정석이다 — K5 3.5절의 "패치는 레시피의 SRCREV 기준으로" 원칙.

### 6.4 config fragment 검증 — 설정만, 빌드 없이

kernel-yocto가 하는 일을 손으로 재현한다: 기준 `.config`에 fragment를 병합하고(`merge_config.sh`), Kconfig 의존성을 풀고(`olddefconfig`), **요청한 값이 살아남았는지** 확인한다(do_kernel_configcheck에 해당). 기준은 랩 defconfig 커널의 `.config` 사본이다(linux-yocto의 qemuarm64 standard 설정과는 다르다 — 그래서 "이 fragment가 Kconfig 의존성상 성립하는가"를 보는 검사로 쓴다).

```sh
#!/bin/bash
set -euo pipefail
SRC=${SRC:-/var/lab/src/linux-6.18.55}
BASE=${BASE:-/var/lab/out/linux-build/.config}
FRAG=${FRAG:-/Users/donh/.elinux_scratch/S6/meta-donlab/recipes-kernel/linux/files/donlab.cfg}
O=${O:-/var/tmp/S6/kcfg}
rm -rf "$O"; mkdir -p "$O"; cp "$BASE" "$O/.config"; cp "$O/.config" "$O/base.config"
cd "$O"
"$SRC/scripts/kconfig/merge_config.sh" -m -O "$O" "$O/.config" "$FRAG" | grep -E '^Value|^Previous|^New|^Merging' || true
make -s -C "$SRC" O="$O" ARCH=arm64 olddefconfig
bad=0
while IFS= read -r line; do
  case "$line" in CONFIG_*=*) ;; "# CONFIG_"*" is not set") ;; *) continue ;; esac
  sym=${line%%=*}; sym=${sym#\# }; sym=${sym%% *}
  got=$(grep -E "^($sym=|# $sym is not set)" .config || echo "$sym (absent)")
  if [ "$got" = "$line" ]; then echo "OK    $line"; else echo "LOST  want: $line  got: $got"; bad=1; fi
done < "$FRAG"
echo "changed vs base: $(diff <(grep -E '^CONFIG_' base.config) <(grep -E '^CONFIG_' .config) | grep -c '^>') line(s)"
exit $bad
```

```sh
bash ~/.elinux_scratch/S6/meta-donlab/tests/check-kernel-fragment.sh; echo "exit=$?"
```

```text
Merging /Users/donh/.elinux_scratch/S6/meta-donlab/recipes-kernel/linux/files/donlab.cfg
Value of CONFIG_SQUASHFS_ZSTD is redefined by fragment /Users/donh/.elinux_scratch/S6/meta-donlab/recipes-kernel/linux/files/donlab.cfg:
Previous value: # CONFIG_SQUASHFS_ZSTD is not set
New value: CONFIG_SQUASHFS_ZSTD=y
Value of CONFIG_EROFS_FS is redefined by fragment /Users/donh/.elinux_scratch/S6/meta-donlab/recipes-kernel/linux/files/donlab.cfg:
Previous value: # CONFIG_EROFS_FS is not set
New value: CONFIG_EROFS_FS=y
Value of CONFIG_SOFT_WATCHDOG is redefined by fragment /Users/donh/.elinux_scratch/S6/meta-donlab/recipes-kernel/linux/files/donlab.cfg:
Previous value: # CONFIG_SOFT_WATCHDOG is not set
New value: CONFIG_SOFT_WATCHDOG=y
OK    CONFIG_IKCONFIG=y
OK    CONFIG_IKCONFIG_PROC=y
OK    CONFIG_OF_OVERLAY=y
OK    CONFIG_SQUASHFS=y
OK    CONFIG_SQUASHFS_ZSTD=y
OK    CONFIG_EROFS_FS=y
OK    CONFIG_OVERLAY_FS=y
OK    CONFIG_WATCHDOG=y
OK    CONFIG_SOFT_WATCHDOG=y
OK    CONFIG_I2C_CHARDEV=y
changed vs base: 16 line(s)
exit=0
```

출력에서 볼 것: 기준 설정에 없던 3개(`SQUASHFS_ZSTD`, `EROFS_FS`, `SOFT_WATCHDOG`)는 "redefined"로 보고되고, 나머지 7개는 이미 켜져 있어 조용하다. 10개 모두 `OK`. 3개를 켰는데 `.config`는 16줄이 바뀌었다 — 켠 옵션이 `select`/`default`로 끌어온 하위 옵션까지 함께 바뀐 것이다. 이 fragment는 각 줄이 왜 필요한지 주석으로 적어 두었다(디버그용 `/proc/config.gz`, read-only rootfs용 squashfs/erofs/overlayfs, systemd watchdog, bring-up용 i2c-dev).

### 6.5 일부러 실패시키기 — 의존성 때문에 사라지는 옵션

fragment에서 가장 흔한 사고는 **"적었는데 안 켜진" 옵션**이다. Kconfig는 `depends on`이 안 맞는 옵션을 에러 없이 조용히 버린다.

```sh
cd /var/tmp/S6/fragtest
printf "CONFIG_SOFT_WATCHDOG=y\nCONFIG_SOFT_WATCHDOG_PRETIMEOUT=y\n" > bad.cfg
FRAG=$PWD/bad.cfg O=$PWD/o bash ~/.elinux_scratch/S6/meta-donlab/tests/check-kernel-fragment.sh | grep -E "^(OK|LOST)"; echo "exit=${PIPESTATUS[0]}"
grep -n "config SOFT_WATCHDOG_PRETIMEOUT" -A3 /var/lab/src/linux-6.18.55/drivers/watchdog/Kconfig
printf "CONFIG_SOFT_WATCHDOG=y\nCONFIG_WATCHDOG_PRETIMEOUT_GOV=y\nCONFIG_SOFT_WATCHDOG_PRETIMEOUT=y\n" > fixed.cfg
FRAG=$PWD/fixed.cfg O=$PWD/o bash ~/.elinux_scratch/S6/meta-donlab/tests/check-kernel-fragment.sh | grep -E "^(OK|LOST)"; echo "exit=${PIPESTATUS[0]}"
```

```text
OK    CONFIG_SOFT_WATCHDOG=y
LOST  want: CONFIG_SOFT_WATCHDOG_PRETIMEOUT=y  got: CONFIG_SOFT_WATCHDOG_PRETIMEOUT (absent)
exit=1
163:config SOFT_WATCHDOG_PRETIMEOUT
164-	bool "Software watchdog pretimeout governor support"
165-	depends on SOFT_WATCHDOG && WATCHDOG_PRETIMEOUT_GOV
166-	help
OK    CONFIG_SOFT_WATCHDOG=y
OK    CONFIG_WATCHDOG_PRETIMEOUT_GOV=y
OK    CONFIG_SOFT_WATCHDOG_PRETIMEOUT=y
exit=0
```

출력에서 볼 것: `merge_config.sh`도 `olddefconfig`도 에러를 내지 않았다 — 검사 스크립트가 없었다면 옵션이 빠진 커널이 그대로 나간다. Yocto에서는 do_kernel_configcheck가 같은 비교를 하고(`symbol_why.py --mismatches`), 우리 bbappend의 `KMETA_AUDIT_WERROR = "1"` 덕분에 **경고가 아니라 빌드 실패**가 된다. 고치는 법은 `depends on`을 읽고 빠진 부모 옵션을 함께 적는 것이다. `menuconfig`에서 `/`로 심볼을 검색하면 "Depends on:" 줄에 지금 값이 `[=n]`으로 보인다(E4).

### 6.6 실행 — linux-yocto를 우리 bbappend와 함께 파싱

```sh
. ~/.elinux_scratch/S6/tools/s6_env.sh
K=/var/tmp/S6/work/openembedded-core/meta/recipes-kernel/linux/linux-yocto_6.18.bb
A=$L/recipes-kernel/linux/linux-yocto_%.bbappend
$P $K +append=$A PV KMACHINE KBRANCH LINUX_VERSION SRC_URI KERNEL_DEVICETREE KMETA_AUDIT_WERROR 2>&1 \
  | grep -v ^NOTE | sed "s/;/;\n      /g" | cut -c1-150
```

```text
PV                           = '6.18.52+git'
KMACHINE                     = 'qemuarm64'
KBRANCH                      = 'v6.18/standard/base'
LINUX_VERSION                = '6.18.52'
SRC_URI                      = 'git://git.yoctoproject.org/linux-yocto.git;
      name=machine;
      branch=v6.18/standard/base;
      protocol=https git://git.yoctoproject.org/yocto-kernel-cache;
      type=kmeta;
      name=meta;
      branch=yocto-6.18;
      destsuffix=kernel-meta;
      protocol=https file://donlab.cfg file://0001-arm64-dts-add-donlab-temperature-sensor-overlay.patch'
KERNEL_DEVICETREE            = 'donlab/donlab-sensor.dtbo'
KMETA_AUDIT_WERROR           = '1'
```

출력에서 볼 것: `SRC_URI` 끝에 우리 파일 두 개가 붙었고, `KMACHINE`이 `qemuarm64`, `KBRANCH`가 qemuarm64용 브랜치로 풀렸다(`MACHINEOVERRIDES`에 `qemuarm64`가 있어서 `KBRANCH:qemuarm64`가 적용됐다). `type=kmeta`인 두 번째 git이 yocto-kernel-cache — `.scc`/`.cfg` 저장소다. 우리 `.cfg`는 SRC_URI에 넣기만 하면 kernel-yocto가 fragment로 인식한다.

이제 3.1절의 `MACHINEOVERRIDES` 줄을 **주석 처리하고** 같은 명령을 다시 돌린다(확인 후 원상 복구했다).

```sh
sed -i '' 's/^MACHINEOVERRIDES =. "qemuarm64:"/# MACHINEOVERRIDES =. "qemuarm64:"/' conf/machine/donlab-qemu.conf   # Mac에서
$P $K +append=$A PV KBRANCH 2>&1 | grep -v ^NOTE; echo "exit=${PIPESTATUS[0]}"         # VM에서
```

```text
SKIPPED: incompatible with machine donlab-qemu (not in COMPATIBLE_MACHINE)
exit=3
```

출력에서 볼 것: 에러가 아니라 **SKIPPED**다. 실제 빌드에서는 이게 더 헷갈린다 — 커널 레시피가 조용히 빠지고, 이미지를 빌드할 때 "virtual/kernel을 제공하는 레시피가 없다"는 식의 에러로 엉뚱한 곳에서 터진다(정확한 문구는 기억 기반이라 생략). linux-yocto의 `COMPATIBLE_MACHINE = "^(qemuarm|...|qemuarm64|...)$"`는 정규식으로 `MACHINEOVERRIDES` 항목과 비교된다. 우리 machine이 `qemuarm64` override를 가져야 통과한다.

태스크 목록에서 커널 전용 태스크만 추린 것이다.

```text
install transform_kernel compile_kernelmodules shared_workdir shared_workdir_setscene kernel_link_images strip sizecheck deploy kernel_checkout kernel_metadata kernel_configme kernel_version_sanity_check validate_branches kernel_configcheck
```

`shared_workdir`가 4.3절에서 본 "커널 빌드 트리를 work-shared에 공개"하는 태스크다. 우리 모듈 레시피의 `make-mod-scripts:do_compile` 의존성이 결국 이것을 기다린다.

> 이 단계가 끝나면: 패치가 `apply-check: OK`, fragment 10개가 `OK`, linux-yocto 파싱의 `SRC_URI`에 우리 파일 두 개, `KMACHINE = 'qemuarm64'`가 보여야 한다.

---

## 7. Step 6 — 이미지 레시피

### 7.1 `donlab-image.bb`와 packagegroup

```text
SUMMARY = "donlab sensor-kit image (systemd, sensor driver + logger daemon)"
LICENSE = "MIT"

inherit core-image extrausers

IMAGE_INSTALL = " \
    packagegroup-core-boot \
    packagegroup-donlab \
    ${CORE_IMAGE_EXTRA_INSTALL} \
"
# product features; developer-only features come from EXTRA_IMAGE_FEATURES (kas/dev.yml)
IMAGE_FEATURES += "ssh-server-openssh"

# read-only rootfs switch: DONLAB_READONLY = "1" in local.conf or kas/ro-rootfs.yml
DONLAB_READONLY ??= "0"
IMAGE_FEATURES += "${@oe.utils.conditional('DONLAB_READONLY', '1', 'read-only-rootfs', '', d)}"

IMAGE_LINGUAS = ""
IMAGE_ROOTFS_EXTRA_SPACE = "65536"

# users: 'donlab' (password "donlab", lab only!) in groups that may read sensors
# hash = openssl passwd -6 -salt donlab0salt donlab ; every '$' escaped for BitBake
DONLAB_PW = "\$6\$donlab0salt\$PpjeVAypnV3g3Mw.nACos.ht0ytKDu.Q.I/9eHcfFthc0UmnGjL8qFkj2/aP8HIPDRFwJyzmsM0M09S1tEjfC."
EXTRA_USERS_PARAMS = " \
    groupadd -r sensors; \
    useradd -m -s /bin/sh -G sensors,dialout donlab; \
    usermod -p '${DONLAB_PW}' donlab; \
"
```

```text
SUMMARY = "donlab sensor-kit userspace"
LICENSE = "MIT"
PACKAGE_ARCH = "${MACHINE_ARCH}"
inherit packagegroup

RDEPENDS:${PN} = " \
    donlab-sensord \
    kernel-module-donlab-sensor \
    i2c-tools \
"
# from meta-oe (LAYERRECOMMENDS): nice to have, image still builds without them
RRECOMMENDS:${PN} = " \
    libgpiod-tools \
    htop \
"
```

설계 포인트:

- **`IMAGE_INSTALL =`(덮어쓰기)는 이미지 레시피 안에서만** 쓴다. local.conf에서 `IMAGE_INSTALL +=`를 쓰면 기본값을 날리는 유명한 함정(K4 3.6)이 생긴다 — 9절 lint가 conf 파일에서 이 패턴을 경고한다.
- **제품 기능 vs 개발 기능을 분리**: 이미지에는 `ssh-server-openssh`만. 빈 root 암호·root 로그인·디버그 도구는 `EXTRA_IMAGE_FEATURES`로 `kas/dev.yml`에서만 켠다. `bitbake.conf`에 `IMAGE_FEATURES += "${EXTRA_IMAGE_FEATURES}"`가 있어서 이 변수가 그대로 합쳐진다. 예전의 `debug-tweaks`는 없어졌고 `allow-empty-password` · `empty-root-password` · `allow-root-login` · `post-install-logging`으로 쪼개졌다(core-image.bbclass 주석에 그렇게 적혀 있다).
- **read-only rootfs는 스위치 하나**: `read-only-rootfs` feature가 켜지면 rootfs 후처리 훅이 붙고 커널 인자에 `ro`가 들어간다(7.3절에서 실제로 확인).
- **users**: `extrausers` class가 rootfs 후처리에서 `useradd`/`usermod`를 돌린다. 암호 해시의 `$`는 `\$`로 이스케이프한다 — `EXTRA_USERS_PARAMS`가 셸 함수 안의 큰따옴표 문자열로 풀리기 때문이다. 이스케이프를 빠뜨리면 `$6`이 빈 문자열로 확장되어 **틀린 해시가 조용히 들어가고 로그인이 안 된다.**

### 7.2 실행 — 이미지 레시피 파싱

```sh
. ~/.elinux_scratch/S6/tools/s6_env.sh
R=$L/recipes-core/images/donlab-image.bb
$P $R IMAGE_FEATURES PACKAGE_INSTALL IMAGE_FSTYPES IMAGE_LINGUAS ROOTFS_POSTPROCESS_COMMAND 2>&1 | grep -v ^NOTE
$P $R EXTRA_USERS_PARAMS 2>&1 | grep -v ^NOTE | cut -c1-200
$P $R @tasks 2>&1 | grep -v ^NOTE | tr " " "\n" \
  | grep -E "^(rootfs|image|image_ext4|image_tar|image_qa|image_complete|populate_sdk|create_image_spdx)$" | tr "\n" " "
```

```text
IMAGE_FEATURES               = 'ssh-server-openssh'
PACKAGE_INSTALL              = 'packagegroup-core-boot packagegroup-donlab run-postinsts packagegroup-core-ssh-openssh base-passwd shadow'
IMAGE_FSTYPES                = 'tar.zst ext4.zst ext4'
IMAGE_LINGUAS                = ''
ROOTFS_POSTPROCESS_COMMAND   = 'set_user_group zap_empty_root_password rootfs_update_timestamp write_image_test_data set_systemd_default_target systemd_sysusers_check systemd_handle_machine_id empty_var_volatile tidy_shadowutils_files add_empty_root_password_note rootfs_reproducible'
EXTRA_USERS_PARAMS           = "groupadd -r sensors; useradd -m -s /bin/sh -G sensors,dialout donlab; usermod -p '\\$6\\$donlab0salt\\$PpjeVAypnV3g3Mw.nACos.ht0ytKDu.Q.I/9eHcfFthc0UmnGjL8qFkj2/aP8HIPD
populate_sdk rootfs image image_complete image_qa create_image_spdx image_tar image_ext4
```

출력에서 볼 것:

- `PACKAGE_INSTALL`(실제로 opkg에 넘길 목록)에 우리가 적지 않은 것들이 붙었다: `packagegroup-core-ssh-openssh`(IMAGE_FEATURES에서), `base-passwd shadow`(extrausers가 `EXTRA_USERS_PARAMS`가 있으면 추가), `run-postinsts`. `kernel-module-donlab-sensor`는 여기 없다 — packagegroup의 RDEPENDS와 machine의 `MACHINE_EXTRA_RRECOMMENDS`를 통해 **do_rootfs의 의존성 해석 때** 들어온다.
- `ROOTFS_POSTPROCESS_COMMAND` 첫 항목 `set_user_group`이 extrausers의 훅, `zap_empty_root_password`는 `empty-root-password` feature가 **없을 때** root 빈 암호를 지우는 훅이다(제품 이미지의 안전장치).
- 이미지 태스크는 형식별로 `image_ext4`, `image_tar` 두 개다. `.zst`는 별도 태스크가 아니라 같은 태스크 안의 "변환(conversion)" 단계로 처리된다.

### 7.3 실행 — 변형: read-only rootfs와 dev

kas overlay는 `includes`로 기본 파일을 겹친다. RO 변형은 local.conf에 한 줄만 더한다.

```text
# kas/ro-rootfs.yml
header:
  version: 14
  includes:
    - kas/donlab.yml

local_conf_header:
  donlab-ro: |
    DONLAB_READONLY = "1"
```

```sh
cd ~/.elinux_scratch/S6/meta-donlab
export KAS_WORK_DIR=/var/tmp/S6/work KAS_BUILD_DIR=/var/tmp/S6/work/build-ro KAS_CLONE_DEPTH=1
/var/tmp/S6/venv/bin/kas checkout kas/ro-rootfs.yml >/dev/null 2>&1
grep -A2 donlab-ro /var/tmp/S6/work/build-ro/conf/local.conf
# (oe-init-build-env를 build-ro로 다시 잡은 뒤)
$P $L/recipes-core/images/donlab-image.bb IMAGE_FEATURES ROOTFS_POSTPROCESS_COMMAND APPEND 2>&1 | grep -v ^NOTE
```

```text
# donlab-ro
DONLAB_READONLY = "1"

IMAGE_FEATURES               = 'read-only-rootfs ssh-server-openssh'
ROOTFS_POSTPROCESS_COMMAND   = 'set_user_group zap_empty_root_password rootfs_update_timestamp read_only_rootfs_hook write_image_test_data set_systemd_default_target systemd_sysusers_check systemd_handle_machine_id empty_var_volatile tidy_shadowutils_files add_empty_root_password_note rootfs_reproducible'
APPEND                       = 'ro'
```

출력에서 볼 것: 스위치 하나로 세 군데가 바뀌었다 — feature 목록, rootfs 후처리에 `read_only_rootfs_hook`(쓰기 필요한 `/etc` 항목을 volatile로 돌리는 훅), 커널 인자 `APPEND`에 `ro`. 실제 기기에서 RO rootfs를 쓰면 **쓰기가 필요한 것**(ssh host key, `machine-id`, 로그, 설정)을 어디에 둘지 결정해야 한다 — overlayfs `/etc`, 별도 data 파티션, tmpfs. 이 설계는 L4(A/B 업데이트)와 한 세트다.

dev 변형도 같은 방법으로 확인했다.

```text
# kas/dev.yml (local_conf_header 부분)
    EXTRA_IMAGE_FEATURES = "allow-empty-password empty-root-password allow-root-login tools-debug"
    CORE_IMAGE_EXTRA_INSTALL += "strace gdbserver"
```

```text
IMAGE_FEATURES               = 'allow-empty-password allow-root-login empty-root-password ssh-server-openssh tools-debug'
PACKAGE_INSTALL              = 'packagegroup-core-boot packagegroup-donlab strace gdbserver run-postinsts packagegroup-core-ssh-openssh packagegroup-core-tools-debug base-passwd shadow'
```

출력에서 볼 것: feature 이름이 틀렸다면 image.bbclass가 파싱 단계에서 `'...' in IMAGE_FEATURES (added via EXTRA_IMAGE_FEATURES) is not a valid image feature`로 레시피를 건너뛴다. 4개 모두 통과했고 `tools-debug`가 `packagegroup-core-tools-debug`로 풀렸다.

> 이 단계가 끝나면: 기본 · RO · dev 세 변형의 `IMAGE_FEATURES`가 각각 1개 · 2개 · 5개여야 한다.

---

## 8. Step 7 — 소스가 실제로 동작하는지 QEMU에서 증명

Yocto 빌드를 못 하는 대신, 레시피가 패키징할 **바로 그 파일들**(4.4절 설치 트리)을 랩 커널 6.18.55-trace 위에서 돌린다. 두 단계다: BusyBox initramfs에서 드라이버 경로 두 가지(fake / DT)를, systemd 게스트에서 "이미지처럼 설치된 상태"를 확인한다. 모든 QEMU는 TCG(에뮬레이션)라 시간은 실제 하드웨어 수치가 아니다.

### 8.1 initramfs 스모크 테스트 — fake 장치와 DT overlay 두 경로

`tests/initramfs-smoke.sh`는 랩 rootfs 사본에 `.ko`와 정적 링크한 데몬을 넣고, `LAB_CMD`로 테스트 스크립트 하나를 돌리고 꺼진다. `MODE=dt`이면 **커널 패치 안의 `.dtso`를 그대로 잘라 내** `dtc`로 컴파일하고 `fdtoverlay`로 랩 `virt.dtb`에 합친다 — 패치가 유일한 원본이 되도록.

```sh
#!/bin/bash
set -euo pipefail
OUT=${OUT:-/var/tmp/S6/out}; MODE=${MODE:-fake}; W=/var/tmp/S6/qemu-$MODE
LAYER=${LAYER:-/Users/donh/.elinux_scratch/S6/meta-donlab}
rm -rf "$W"; mkdir -p "$W/r"; cd "$W"
cp -a /var/lab/build/rootfs/. r/
cp "$OUT/mod/donlab_sensor.ko" "$OUT/donlab-sensord.static" r/
PARAM="fake_device=1"; DTB=()
if [ "$MODE" = dt ]; then
  PARAM=""
  # the kernel patch is the single source of truth: cut the .dtso out of it
  awk '/^\+\+\+ b\/.*donlab-sensor.dtso/{f=1;next} f&&/^(diff|--) /{f=0} f&&/^\+/{print substr($0,2)}' \
      "$LAYER"/recipes-kernel/linux/files/0001-*.patch > sensor.dtso
  dtc -q -@ -I dts -O dtb -o sensor.dtbo sensor.dtso
  fdtoverlay -i /var/lab/out/virt.dtb -o virt-donlab.dtb sensor.dtbo
  DTB=(-dtb virt-donlab.dtb)
fi
cat > r/s6test.sh <<SH
insmod /donlab_sensor.ko $PARAM
stat -c '%A %n (char %t:%T hex)' /dev/donlab_sensor
head -n 3 /dev/donlab_sensor
[ -d /proc/device-tree/donlab-sensor ] && echo "DT compatible: \$(cat /proc/device-tree/donlab-sensor/compatible)"
/donlab-sensord.static -p 10 -w 10 -n 20
dmesg | grep -i donlab
rmmod donlab_sensor && echo "rmmod ok"
SH
(cd r && find . | cpio -o -H newc --quiet | gzip) > i.gz
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image-trace -initrd i.gz "${DTB[@]}" \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/s6test.sh" | tr -d '\r' \
  | sed -n '/lab initramfs:/,$p' | grep -v -E 'Power down' || true
```

```sh
MODE=fake bash ~/.elinux_scratch/S6/meta-donlab/tests/initramfs-smoke.sh
echo =====
MODE=dt bash ~/.elinux_scratch/S6/meta-donlab/tests/initramfs-smoke.sh
```

```text
lab initramfs: 6.18.55-trace up 3.95s
cr--r--r-- /dev/donlab_sensor (char a:102 hex)
seq=1 temp_mC=24100
seq=2 temp_mC=24200
seq=3 temp_mC=24300
donlab-sensord: reading /dev/donlab_sensor every 10 ms, window 10
seq=13 n=10 min=24400 avg=24850 max=25300 mC
seq=23 n=10 min=25400 avg=25730 max=26000 mC
donlab-sensord: exit after 20 samples
[    4.090362][   T69] donlab_sensor: loading out-of-tree module taints kernel.
[    4.090539][   T69] donlab_sensor: module verification failed: signature and/or required key missing - tainting kernel
[    4.140661][   T69] donlab-sensor donlab-sensor: donlab_sensor ready, base=25000 mC, node=none
rmmod ok
=====
lab initramfs: 6.18.55-trace up 4.15s
cr--r--r-- /dev/donlab_sensor (char a:102 hex)
seq=1 temp_mC=29100
seq=2 temp_mC=29200
seq=3 temp_mC=29300
DT compatible: donlab,temp-sensor
donlab-sensord: reading /dev/donlab_sensor every 10 ms, window 10
seq=13 n=10 min=29400 avg=29850 max=30300 mC
seq=23 n=10 min=30400 avg=30730 max=31000 mC
donlab-sensord: exit after 20 samples
[    4.344237][   T69] donlab_sensor: loading out-of-tree module taints kernel.
[    4.344425][   T69] donlab_sensor: module verification failed: signature and/or required key missing - tainting kernel
[    4.392230][   T69] donlab-sensor donlab-sensor: donlab_sensor ready, base=30000 mC, node=DT
rmmod ok
```

출력에서 볼 것:

- 장치 번호 `a:102`(16진) = major 10(misc), minor 258(동적 할당). 권한 `cr--r--r--`는 드라이버의 `misc.mode = 0444` 그대로다.
- **두 경로의 차이는 `base`와 `node=`뿐**이다. fake 경로는 기본값 25000, DT 경로는 overlay의 `donlab,base-millicelsius = <30000>`을 `device_property_read_u32()`로 읽었다 — DT 속성이 드라이버까지 왔다는 증거다. 장치 이름도 fake는 등록 이름, DT는 노드 이름(`donlab-sensor`)이다.
- 데몬의 첫 창이 `seq=13`에서 끝난다 — `head -n 3`이 샘플 3개를 먼저 소비했기 때문이다(seq는 드라이버 전역 카운터).
- `module verification failed`: 랩 커널은 `MODULE_SIG`가 켜져 있지만 강제는 아니다. Yocto에서 서명하려면 커널 레시피의 서명 키로 모듈도 서명해야 한다(L5).
- 타임스탬프는 실행마다 다르다.

### 8.2 systemd 게스트 — "이미지에 설치된 상태"를 두 번의 부팅으로

이번에는 실제 systemd가 도는 랩 게스트(`rootfs-trace.ext4`, qcow2 overlay — 원본은 건드리지 않는다)에서 확인한다. 4.4절의 설치 트리를 ext4 이미지로 만들어 두 번째 디스크로 붙이고, `systemd.mount-extra=`로 `/mnt`에 마운트한다.

- **부팅 1** (`systemd.run=`으로 설치 스크립트 실행 후 종료): 설치 트리를 `/`에 복사, `depmod`, `systemctl enable`. Yocto의 do_rootfs가 패키지를 풀고 postinst를 돌리는 것에 해당한다.
- **부팅 2** (보통 부팅): `systemd-modules-load`가 모듈을 올리고, 서비스가 자동으로 뜨는지 본다. 결과는 테스트 전용 oneshot unit(`s6-report.service`, layer에는 없음)이 시리얼에 찍고 끈다.

```sh
#!/bin/bash
set -euo pipefail
OUT=${OUT:-/var/tmp/S6/out}; W=/var/tmp/S6/sysd; T=$(dirname "$(readlink -f "$0")")
rm -rf "$W"; mkdir -p "$W/payload/test"
cp -a "$OUT/image" "$W/payload/image"
cp "$T/s6-report.service" "$T/report.sh" "$T/install.sh" "$W/payload/test/"
mkfs.ext4 -q -d "$W/payload" "$W/payload.img" 64M
qemu-img create -q -f qcow2 -F raw -b /var/lab/out/rootfs-trace.ext4 "$W/root.qcow2"
Q=(qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1G -nographic
   -kernel /var/lab/out/Image-trace
   -drive file="$W/root.qcow2",if=virtio,format=qcow2
   -drive file="$W/payload.img",if=virtio,format=raw)
BASE="console=ttyAMA0 root=/dev/vda rw rootwait apparmor=0 systemd.mount-extra=/dev/vdb:/mnt:ext4:ro"
SECONDS=0
timeout 900 "${Q[@]}" -append "$BASE systemd.run=\"/bin/sh /mnt/test/install.sh\" systemd.run_success_action=poweroff" \
  | tr -d '\r' | grep -E 'S6-INSTALL-DONE|Created symlink' || true
echo "boot 1: ${SECONDS}s"; SECONDS=0
timeout 900 "${Q[@]}" -append "$BASE quiet systemd.mask=serial-getty@ttyAMA0.service" | tr -d '\r' | sed -n '/== S6 REPORT/,/== S6 REPORT END/p'
echo "boot 2: ${SECONDS}s (TCG, not real hardware)"
```

```sh
bash ~/.elinux_scratch/S6/meta-donlab/tests/systemd-guest/run.sh
```

```text
S6-INSTALL-DONE
boot 1: 58s
== S6 REPORT (6.18.55-trace)
modules-load.d: donlab_sensor
modprobe.d:     options donlab_sensor fake_device=1
lsmod:          donlab_sensor          12288  1
modules-load:   active
sensord:        enabled / active
-- journal
Started donlab-sensord.service - donlab fake temperature sensor logger.
donlab-sensord: reading /dev/donlab_sensor every 200 ms, window 10
seq=10 n=10 min=24100 avg=24550 max=25000 mC
seq=20 n=10 min=25100 avg=25550 max=26000 mC
seq=30 n=10 min=25000 avg=25450 max=25900 mC
-- process
USER                       PID CMD
63144                      191 /usr/bin/donlab-sensord -p 200 -w 10
-- exposure
→ Overall exposure level for donlab-sensord.service: 7.6 EXPOSED :-(
== S6 REPORT END
boot 2: 58s (TCG, not real hardware)
```

출력에서 볼 것:

- **`KERNEL_MODULE_AUTOLOAD`가 만들 파일**(`modules-load.d`) → `systemd-modules-load` `active` → `lsmod`에 모듈. **`module_conf_*`가 만들 파일**(`modprobe.d`)의 `fake_device=1`이 modprobe 때 적용돼 장치가 생겼다. 아무도 `modprobe`를 손으로 부르지 않았다.
- 서비스가 `enabled / active`이고 journal에 데몬 출력이 그대로 들어갔다(stdout → journald).
- 프로세스 사용자가 이름 없는 숫자 uid `63144` — `DynamicUser=yes`가 부팅마다 임시 uid를 배정한 것이다. root가 아니어도 `0444` 장치를 읽는다.
- 부팅 한 번에 약 1분 — TCG 에뮬레이션 + 1 GB 게스트라서다. 실제 Cortex-A 보드라면 수 초다.

처음 돌렸을 때는 두 가지가 실패했다. 그대로 적어 둔다 — 둘 다 실무에서 흔한 실수다.

| 증상 | 원인 | 고친 것 |
|---|---|---|
| 부팅 2가 900초 타임아웃까지 아무것도 안 찍음 | 보고용 unit에 `[Install]` 절이 없어서 `systemctl enable`이 **아무 링크도 만들지 않았다** (경고만 내고 성공으로 끝남) | `[Install] WantedBy=multi-user.target` 추가 |
| 보고가 중간에 끊기고 로그인 배너가 섞임 | 자동 로그인 getty가 ttyAMA0을 열면서 vhangup → 보고 스크립트의 tty fd가 무효화 | 부팅 2에 `systemd.mask=serial-getty@ttyAMA0.service` |

### 8.3 숫자 하나 만들기 — 서비스 노출 점수 7.6 → 0.5

`systemd-analyze security`는 unit의 하드닝 수준을 0(안전)~10(노출)으로 매긴다. 게스트 안 점수(7.6)는 VM에서 오프라인으로도 똑같이 나온다(`--offline=true`는 unit 파일만 읽는다). 그래서 VM에서 바로 고치면서 쟀다.

```sh
cd ~/.elinux_scratch/S6/meta-donlab/recipes-apps/donlab-sensord/files
systemd-analyze security --offline=true --no-pager donlab-sensord.service 2>&1 | tail -1
```

```text
→ Overall exposure level for donlab-sensord.service: 7.6 EXPOSED :-(
```

`[Service]`에 다음 줄을 더했다 (각 항목의 뜻은 L1·L6). 데몬이 실제로 쓰는 것은 `open/read/nanosleep/write`와 장치 하나뿐이라 거의 다 막을 수 있다.

```text
CapabilityBoundingSet=
RestrictAddressFamilies=AF_UNIX
RestrictNamespaces=yes
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectKernelLogs=yes
ProtectControlGroups=yes
ProtectClock=yes
ProtectHostname=yes
ProtectProc=invisible
ProcSubset=pid
PrivateUsers=yes
LockPersonality=yes
MemoryDenyWriteExecute=yes
RestrictRealtime=yes
SystemCallArchitectures=native
SystemCallFilter=@system-service
SystemCallFilter=~@privileged @resources
UMask=0077
```

```sh
systemd-analyze security --offline=true --no-pager donlab-sensord.service 2>&1 | grep -E "^✗" | head -20
systemd-analyze security --offline=true --no-pager donlab-sensord.service 2>&1 | tail -1
```

```text
✗ RootDirectory=/RootImage=                                   Service runs within the host's root directory                                            0.1
✗ PrivateDevices=                                             Service potentially has access to hardware devices                                       0.2
✗ RestrictAddressFamilies=~AF_UNIX                            Service may allocate local sockets                                                       0.1
✗ DeviceAllow=                                                Service has a device ACL with some special devices: char-rtc:r /dev/donlab_sensor:r      0.1
✗ IPAddressDeny=                                              Service does not define an IP address allow list                                         0.2
→ Overall exposure level for donlab-sensord.service: 0.5 SAFE :-}
```

남은 항목은 의도한 것이다: 장치 접근(`PrivateDevices`를 켜면 우리 장치도 못 본다), `char-rtc`는 `ProtectClock`이 자동으로 넣은 읽기 ACL이다. **하드닝이 동작을 깨지 않았는지**는 같은 2-부팅 테스트를 다시 돌려 확인했다 — 보고 내용은 위와 같고 마지막 줄만 바뀌었다.

```text
-- exposure
→ Overall exposure level for donlab-sensord.service: 0.5 SAFE :-}
== S6 REPORT END
boot 2: 51s (TCG, not real hardware)
```

```svg
<svg viewBox="0 0 640 190" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="24" font-size="14">systemd-analyze security (0 = 안전, 10 = 노출)</text>
  <line x1="170" y1="40" x2="170" y2="150" stroke="currentColor"/>
  <line x1="170" y1="150" x2="570" y2="150" stroke="currentColor"/>
  <text x="20" y="77" font-size="13">첫 버전 unit</text>
  <rect x="170" y="58" width="304" height="28" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="484" y="77" font-size="13">7.6 EXPOSED</text>
  <text x="20" y="125" font-size="13">하드닝 후 unit</text>
  <rect x="170" y="106" width="20" height="28" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="200" y="125" font-size="13">0.5 SAFE</text>
  <text x="170" y="168" text-anchor="middle" font-size="12">0</text>
  <text x="370" y="168" text-anchor="middle" font-size="12">5</text>
  <text x="570" y="168" text-anchor="middle" font-size="12">10</text>
  <line x1="370" y1="150" x2="370" y2="155" stroke="currentColor"/>
  <line x1="570" y1="150" x2="570" y2="155" stroke="currentColor"/>
</svg>
```

그림 2 — 같은 데몬, unit 파일만 바꿔서 잰 노출 점수(VM에서 `--offline=true`, 게스트 안에서도 같은 값). 동작은 2-부팅 테스트로 다시 확인했다. GitHub README에 넣기 좋은 숫자다.

> 이 단계가 끝나면: 두 initramfs 모드가 `rmmod ok`로 끝나고, systemd 게스트 보고에 `enabled / active`, `0.5 SAFE`가 보여야 한다.

---

## 9. Step 8 — 정적 검사: lint · yamllint · bash -n

### 9.1 왜 BitBake 밖 lint가 필요한가

BitBake 파서(tinfoil)는 레시피 **하나씩** 돌려야 하고 환경 준비가 필요하다. 커밋 전 훅이나 CI 첫 단계에서는 1초 안에 끝나는 검사가 유용하다. K5 11.1의 lint가 "vendor layer와의 연결"(require 경로, dangling bbappend)을 봤다면, 이 lint는 **우리 파일 안의 문법·관례**를 본다: 옛 override 문법, 따옴표 짝, `S = "${WORKDIR}"`, 라이선스 md5 대조, `SRC_URI` 파일 존재, bbappend의 `FILESEXTRAPATHS :=`, override 없는 `SRC_URI` 변경. 전체 63줄 중 검사 본체만 옮긴다(앞 19줄은 인자 처리와 `LAYERSERIES_COMPAT` 대조).

```python
files = [p for e in ("bb", "bbappend", "conf", "inc") for p in glob.glob(f"{layer}/**/*.{e}", recursive=True)]
for path in sorted(files):
    text = open(path).read()
    lines = logical_lines(text)
    for n, l in enumerate(lines, 1):
        if l.lstrip().startswith("#"):
            continue
        if re.match(r"\s*[A-Za-z0-9_${}-]+_(append|prepend|remove)\b", l):
            say("ERROR", path, f"old override syntax (use ':append'): {l.strip()[:60]}")
        if re.match(r"\s*(RDEPENDS|RRECOMMENDS|RPROVIDES|FILES|SYSTEMD_SERVICE|INSANE_SKIP)_\$\{PN\}", l):
            say("ERROR", path, f"old override syntax (use ':${{PN}}'): {l.strip()[:60]}")
        if re.match(r'\s*\w[\w:${}-]*\s*(\?\?=|\?=|:=|\+=|=\+|\.=|=\.|=)', l) and l.count('"') % 2:
            say("ERROR", path, f"unbalanced quotes: {l.strip()[:60]}")
        if re.match(r'\s*S\s*=\s*"\$\{WORKDIR\}"', l):
            say("ERROR", path, 'S = "${WORKDIR}" is invalid since 5.1, use ${UNPACKDIR}')
        if path.endswith(".conf") and re.match(r"\s*IMAGE_INSTALL\s*\+=", l):
            say("WARN", path, "IMAGE_INSTALL += in a conf file overrides image defaults; use :append")
    if not path.endswith((".bb", ".bbappend")):
        continue
    rdir = os.path.dirname(path)
    is_meta = re.search(r"inherit\s+.*\b(packagegroup|core-image|image|bundle|swupdate)\b", text)
    if path.endswith(".bb") and "LICENSE" not in text:
        say("ERROR", path, "missing LICENSE")
    if path.endswith(".bb") and not is_meta and "LIC_FILES_CHKSUM" not in text:
        say("ERROR", path, "missing LIC_FILES_CHKSUM (do_populate_lic will fail)")
    for f, md5 in re.findall(r"file://([^;\s\"]+);md5=([0-9a-f]{32})", text):
        src = f.replace("${COMMON_LICENSE_DIR}", f"{core}/files/common-licenses")
        cands = [src] if src.startswith("/") else [f"{rdir}/files/{src}"]
        real = [hashlib.md5(open(c, "rb").read()).hexdigest() for c in cands if os.path.exists(c)]
        if not real or real[0] != md5:
            say("ERROR", path, f"license checksum {f}: recipe {md5[:8]}.. file {real[0][:8] if real else 'MISSING'}")
    for f in re.findall(r"file://([^;\s\"]+)", " ".join(l for l in lines if "SRC_URI" in l)):
        bpn = os.path.basename(path).split("_")[0].split(".")[0]
        if not any(os.path.exists(f"{rdir}/{d}/{f}") for d in ("files", bpn, ".")):
            say("ERROR", path, f"SRC_URI file://{f} not found next to the recipe")
    if path.endswith(".bbappend"):
        if "FILESEXTRAPATHS" in text and not re.search(r"FILESEXTRAPATHS:prepend\s*:=", text):
            say("ERROR", path, "FILESEXTRAPATHS must use ':prepend :=' (THISDIR expands late)")
        if re.search(r"^SRC_URI(:append|:prepend)?\s*\+?=", text, re.M):
            say("WARN", path, "SRC_URI change without a MACHINE/DISTRO override affects every build")
```

### 9.2 실행 — 깨끗한 layer와 일부러 망가뜨린 사본

```sh
cd ~/.elinux_scratch/S6/meta-donlab
python3 -I scripts/lint-layer.py . /var/tmp/S6/work/openembedded-core/meta; echo "exit=$?"
for f in tests/*.sh tests/systemd-guest/*.sh; do bash -n $f && echo "bash -n OK  $f"; done
/var/tmp/S6/venv/bin/yamllint -f parsable . ; echo "yamllint exit=$?"
```

```text
checked 10 files: 0 error(s), 0 warning(s)
exit=0
bash -n OK  tests/build-outside-yocto.sh
bash -n OK  tests/check-kernel-fragment.sh
bash -n OK  tests/initramfs-smoke.sh
bash -n OK  tests/systemd-guest/install.sh
bash -n OK  tests/systemd-guest/report.sh
bash -n OK  tests/systemd-guest/run.sh
yamllint exit=0
```

yamllint은 저장소의 `.yamllint`(기본 규칙 + 줄 길이 120 + `on:` 키 허용)로 kas 파일 7개와 CI 워크플로를 검사했다. 이제 사본(`/var/tmp/S6/badlayer`)에 흔한 실수 6개를 심는다.

```sh
cp -a ~/.elinux_scratch/S6/meta-donlab /var/tmp/S6/badlayer && cd /var/tmp/S6/badlayer && rm -rf .git
R=recipes-apps/donlab-sensord/donlab-sensord_0.1.bb
sed -i 's/md5=d4cdde6f/md5=00000000/; s|^S = "${UNPACKDIR}"|S = "${WORKDIR}"|; s|file://LICENSE \\|file://LICENSE file://donlab.conf \\|' $R
echo 'RDEPENDS_${PN} += "bash"' >> $R
sed -i 's/^LAYERSERIES_COMPAT_donlab = "wrynose"/LAYERSERIES_COMPAT_donlab = "scarthgap"/' conf/layer.conf
sed -i 's/FILESEXTRAPATHS:prepend := /FILESEXTRAPATHS:prepend = /' recipes-kernel/linux/linux-yocto_%.bbappend
python3 -I ~/.elinux_scratch/S6/meta-donlab/scripts/lint-layer.py . /var/tmp/S6/work/openembedded-core/meta; echo "exit=$?"
```

```text
ERROR conf/layer.conf: LAYERSERIES_COMPAT does not list core 'wrynose'
ERROR recipes-apps/donlab-sensord/donlab-sensord_0.1.bb: S = "${WORKDIR}" is invalid since 5.1, use ${UNPACKDIR}
ERROR recipes-apps/donlab-sensord/donlab-sensord_0.1.bb: old override syntax (use ':${PN}'): RDEPENDS_${PN} += "bash"
ERROR recipes-apps/donlab-sensord/donlab-sensord_0.1.bb: license checksum LICENSE: recipe 00000000.. file d4cdde6f
ERROR recipes-apps/donlab-sensord/donlab-sensord_0.1.bb: SRC_URI file://donlab.conf not found next to the recipe
ERROR recipes-kernel/linux/linux-yocto_%.bbappend: FILESEXTRAPATHS must use ':prepend :=' (THISDIR expands late)
checked 10 files: 6 error(s), 0 warning(s)
exit=1
```

### 9.3 lint가 BitBake보다 나은 지점 — 조용히 무시되는 옛 문법

같은 실수 두 개를 **진짜 BitBake 파서**에 넣어 봤다(원본 레시피를 잠시 고치고 확인 후 되돌렸다).

```sh
# 1) S = "${WORKDIR}"
$P $L/recipes-apps/donlab-sensord/donlab-sensord_0.1.bb S 2>&1 | grep -v "^NOTE" | head -2
# 2) RDEPENDS_${PN} += "bash" 를 끝에 추가
$P $L/recipes-apps/donlab-sensord/donlab-sensord_0.1.bb PN 2>&1 | grep -v "^NOTE"
```

```text
ERROR: Using S = ${WORKDIR} is no longer supported
Traceback (most recent call last):
PN                           = 'donlab-sensord'
```

출력에서 볼 것: 첫 번째는 BitBake도 파싱 단계에서 막는다. 두 번째는 **에러 없이 통과한다** — `RDEPENDS_${PN}`은 이제 override가 아니라 `RDEPENDS_donlab-sensord`라는 **아무도 읽지 않는 새 변수**일 뿐이다. 옛 레시피를 복사해 오면 런타임 의존성이 조용히 사라지고, 기기에서 "bash: not found"로 터진다. (BitBake는 `_append`/`_prepend`/`_remove` 같은 옛 연산자 문법은 에러로 잡지만, 패키지 이름 override는 일반 변수 이름과 구별할 방법이 없다.) 이런 것이 정적 lint를 CI에 두는 이유다.

> 이 단계가 끝나면: 원본 layer는 0 error, 망가뜨린 사본은 6 error가 나와야 한다.

---

## 10. Step 9 — OTA 훅 자리 (RAUC / SWUpdate)

OTA 설계 자체(A/B 슬롯, bootcount, 서명)는 L4에 있다. 이 프로젝트는 **"OTA layer를 붙이면 바로 bundle을 만들 수 있는 자리"**만 만든다. 핵심은 2.1절의 `BBFILES_DYNAMIC`이다 — meta-rauc가 없는 빌드에서는 `inherit bundle` 레시피를 아예 파싱하지 않는다.

```text
# dynamic-layers/rauc/recipes-core/bundles/donlab-bundle.bb  (PLACEHOLDER, not built)
SUMMARY = "donlab RAUC bundle"
LICENSE = "MIT"

inherit bundle

RAUC_BUNDLE_COMPATIBLE = "donlab-${MACHINE}"
RAUC_BUNDLE_FORMAT = "verity"
RAUC_BUNDLE_SLOTS = "rootfs"
RAUC_SLOT_rootfs = "donlab-image"
RAUC_SLOT_rootfs[fstype] = "ext4"

# signing material: never commit real keys; CI injects them
RAUC_KEY_FILE ?= "${TOPDIR}/../keys/development-1.key.pem"
RAUC_CERT_FILE ?= "${TOPDIR}/../keys/development-1.cert.pem"
```

```text
# kas/ota-rauc.yml
header:
  version: 14
  includes:
    - kas/donlab.yml

repos:
  meta-rauc:
    url: https://github.com/rauc/meta-rauc
    branch: wrynose

local_conf_header:
  donlab-rauc: |
    DISTRO_FEATURES:append = " rauc"
    IMAGE_INSTALL:append:pn-donlab-image = " rauc"
```

`DISTRO_FEATURES += "rauc"`는 meta-rauc README의 1번 단계다(`README.rst` 62행에서 확인). SWUpdate 쪽(`donlab-swu.bb` + `sw-description`)도 같은 구조로 넣었다.

```sh
# OTA overlay마다 별도 build 디렉터리로 checkout한 뒤, 거기서 layer 목록과 placeholder 레시피를 파싱
for v in rauc swupdate; do
  KAS_BUILD_DIR=/var/tmp/S6/work/build-$v kas checkout kas/ota-$v.yml
  # (oe-init-build-env /var/tmp/S6/work/build-$v 후)
  bitbake-layers show-layers 2>&1 | grep -E "^(rauc|swupdate|donlab) "
  python3 s6_parse.py <placeholder.bb> <변수들>
done
```

```text
== rauc
donlab                /var/tmp/S6/work/build-rauc/../../../../../Users/donh/.elinux_scratch/S6/meta-donlab  10
rauc                  /var/tmp/S6/work/build-rauc/../meta-rauc                                6
RAUC_BUNDLE_COMPATIBLE       = 'donlab-donlab-qemu'
RAUC_BUNDLE_SLOTS            = 'rootfs'
RAUC_BUNDLE_FORMAT           = 'verity'
RAUC_SLOT_rootfs[fstype] = 'ext4'
exit=0
== swupdate
donlab                /var/tmp/S6/work/build-swupdate/../../../../../Users/donh/.elinux_scratch/S6/meta-donlab  10
swupdate              /var/tmp/S6/work/build-swupdate/../meta-swupdate                        6
IMAGE_DEPENDS                = 'donlab-image'
SWUPDATE_IMAGES              = 'donlab-image'
SWUPDATE_IMAGES_FSTYPES[donlab-image] = '.ext4.zst'
SRC_URI                      = 'file://sw-description'
exit=0
```

그리고 `BBFILES_DYNAMIC`이 실제로 조건부인지 — 기본 빌드와 rauc 빌드의 최종 `BBFILES` 중 우리 layer 항목만 비교했다.

```sh
for b in build build-rauc; do (cd /var/tmp/S6/work/openembedded-core; . ./oe-init-build-env /var/tmp/S6/work/$b >/dev/null
  echo "== $b"; bitbake-getvar --value BBFILES 2>/dev/null | tr " " "\n" | grep -i donlab | sed "s|.*/meta-donlab/|meta-donlab/|"); done
```

```text
== build
meta-donlab/recipes-*/*/*.bb
meta-donlab/recipes-*/*/*.bbappend
== build-rauc
meta-donlab/recipes-*/*/*.bb
meta-donlab/recipes-*/*/*.bbappend
meta-donlab/dynamic-layers/rauc/recipes-*/*/*.bb
```

출력에서 볼 것: meta-rauc의 컬렉션 이름 `rauc`(priority 6)가 BBLAYERS에 있을 때만 `dynamic-layers/rauc` 경로가 붙는다. 기본 빌드는 RAUC 레시피를 보지도 않는다. 이 placeholder로 실제 bundle을 만들려면 L4 6절의 키 체계, `rauc-conf`의 `system.conf`(슬롯 정의), 부트로더 연동(U-Boot bootcount)이 더 필요하다 — 그래서 이름이 placeholder다.

> 이 단계가 끝나면: OTA overlay 두 개에서 placeholder 레시피가 exit 0으로 파싱되고, 기본 빌드의 `BBFILES`에는 `dynamic-layers`가 없어야 한다.

---

## 11. Step 10 — 실제 빌드 (가이드, 실행 안 함)

여기부터는 **user namespace가 허용된 빌드 호스트**에서 할 일이다. 명령과 확인할 것만 적는다. 출력은 쓰지 않는다.

### 11.1 레시피 하나가 이미지가 되기까지

```svg
<svg viewBox="0 0 680 460" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="28" font-size="13">① 레시피 (.bb / .bbappend)</text>
  <rect x="20" y="38" width="200" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="63" text-anchor="middle" font-size="12">donlab-sensor-mod_0.1.bb</text>
  <rect x="240" y="38" width="200" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="63" text-anchor="middle" font-size="12">donlab-sensord_0.1.bb</text>
  <rect x="460" y="38" width="200" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="63" text-anchor="middle" font-size="12">linux-yocto + bbappend</text>
  <line x1="120" y1="78" x2="120" y2="110" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="340" y1="78" x2="340" y2="110" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="560" y1="78" x2="560" y2="110" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="20" y="104" font-size="12">② 태스크</text>
  <rect x="20" y="114" width="84" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="62" y="136" text-anchor="middle" font-size="12">fetch</text>
  <rect x="112" y="114" width="84" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="154" y="136" text-anchor="middle" font-size="12">unpack</text>
  <rect x="204" y="114" width="84" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="246" y="136" text-anchor="middle" font-size="12">patch</text>
  <rect x="296" y="114" width="84" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="338" y="136" text-anchor="middle" font-size="12">configure</text>
  <rect x="388" y="114" width="84" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="430" y="136" text-anchor="middle" font-size="12">compile</text>
  <rect x="480" y="114" width="84" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="522" y="136" text-anchor="middle" font-size="12">install</text>
  <rect x="572" y="114" width="84" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="614" y="136" text-anchor="middle" font-size="12">package</text>
  <text x="340" y="168" text-anchor="middle" font-size="12">→ package_qa → packagedata → package_write_ipk (QA 에러는 여기서 — 12절)</text>
  <line x1="120" y1="176" x2="120" y2="198" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="340" y1="176" x2="340" y2="198" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="560" y1="176" x2="560" y2="198" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="20" y="194" font-size="12">③ 패키지</text>
  <rect x="15" y="202" width="210" height="48" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="120" y="222" text-anchor="middle" font-size="12">kernel-module-donlab-sensor</text>
  <text x="120" y="240" text-anchor="middle" font-size="12">.ko · modules-load.d</text>
  <rect x="240" y="202" width="200" height="48" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="222" text-anchor="middle" font-size="12">donlab-sensord</text>
  <text x="340" y="240" text-anchor="middle" font-size="12">(+ -dbg -dev -src 따로)</text>
  <rect x="460" y="202" width="200" height="48" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="560" y="222" text-anchor="middle" font-size="12">kernel-image</text>
  <text x="560" y="240" text-anchor="middle" font-size="12">kernel-devicetree (dtbo)</text>
  <line x1="120" y1="250" x2="120" y2="290" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="340" y1="250" x2="340" y2="290" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="560" y1="250" x2="560" y2="290" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="20" y="276" font-size="12">④ 이미지</text>
  <rect x="20" y="294" width="640" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="319" text-anchor="middle" font-size="13">do_rootfs: 의존성 해석 → opkg 설치 → postinst → 후처리 훅 → do_image_ext4 / tar</text>
  <line x1="340" y1="334" x2="340" y2="364" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="20" y="358" font-size="12">⑤ 산출물</text>
  <rect x="20" y="368" width="640" height="76" rx="5" fill="none" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="340" y="390" text-anchor="middle" font-size="13">tmp/deploy/images/donlab-qemu/ (가이드 — 실행 안 함)</text>
  <text x="340" y="410" text-anchor="middle" font-size="12">donlab-image-donlab-qemu.rootfs.ext4(.zst) · rootfs.tar.zst · Image · donlab-sensor.dtbo</text>
  <text x="340" y="430" text-anchor="middle" font-size="12">rootfs.manifest · rootfs.spdx.json · qemuboot.conf · modules-donlab-qemu.tgz</text>
</svg>
```

그림 3 — 레시피 3개가 같은 태스크 체인(.ipk 패키지까지, 커널 레시피는 kernel_metadata · kernel_configme · kernel_configcheck가 끼어든다)을 지나 패키지가 되고, 이미지 레시피의 do_rootfs가 패키지를 모아 파일시스템 이미지를 만든다. 패키지 이름은 레시피 이름과 다를 수 있다(`.ko` 이름에서 만들어지는 `kernel-module-donlab-sensor`). ⑤의 파일 이름은 wrynose의 `IMAGE_NAME` 규칙(`<image>-<machine>.rootfs.<type>`) 기준이며 기억 기반이다.

### 11.2 명령 (가이드)

```sh
# 0) 호스트: Yocto 6.0 Ubuntu 필수 패키지 + user namespace 허용 (빌드 서버에서만)
# 1) 한 번에: checkout + 설정 + 빌드
kas build kas/donlab.yml
# 2) 레시피 하나씩 (kas shell 안에서)
kas shell kas/donlab.yml
bitbake donlab-sensor-mod            # 모듈만
bitbake -c kernel_configcheck -f virtual/kernel   # fragment 감사만 다시
bitbake -c compile -f donlab-sensord && bitbake donlab-sensord
bitbake donlab-image
# 3) 확인
oe-pkgdata-util list-pkg-files kernel-module-donlab-sensor
oe-pkgdata-util find-path /usr/bin/donlab-sensord
bitbake-getvar -r donlab-image IMAGE_FEATURES
cat tmp/deploy/images/donlab-qemu/donlab-image-donlab-qemu.rootfs.manifest | grep donlab
# 4) 부팅
runqemu donlab-qemu nographic slirp
#    게스트에서: lsmod | grep donlab ; systemctl status donlab-sensord ; journalctl -u donlab-sensord
# 5) 개발 루프 (K5 4.2)
devtool modify donlab-sensord && devtool build donlab-sensord && devtool deploy-target donlab-sensord root@192.168.7.2
```

각 명령에서 확인할 것:

| 명령 | 성공이면 보여야 하는 것 |
|---|---|
| `bitbake donlab-sensor-mod` | `tmp/work/donlab_qemu-poky-linux/donlab-sensor-mod/0.1/` 아래 `packages-split/kernel-module-donlab-sensor/usr/lib/modules/<ver>/updates/donlab_sensor.ko` (4.4절 트리와 같은 상대 경로) |
| `kernel_configcheck` | 경고 없음. 하나라도 빠지면 `KMETA_AUDIT_WERROR` 때문에 `promoting to fatal` |
| `oe-pkgdata-util list-pkg-files` | `.ko`, `modules-load.d/donlab_sensor.conf`, `modprobe.d/donlab_sensor.conf` 세 파일 |
| `.manifest` | `donlab-sensord`, `kernel-module-donlab-sensor`, `packagegroup-donlab` 줄 |
| `runqemu` 게스트 | 8.2절 보고와 같은 내용 (`enabled / active`) |

### 11.3 runqemu에서 DT overlay는?

`runqemu`는 QEMU가 만드는 DT를 그대로 쓰고 overlay를 적용하지 않는다. 그래서 donlab-qemu에는 `fake_device=1`이 들어간다(4.2절). overlay 경로까지 Yocto 산출물로 확인하려면 8.1절처럼 QEMU의 DT를 덤프(`-machine dumpdtb=virt.dtb`)해서 `fdtoverlay`로 합친 뒤 `runqemu ... qemuparams="-dtb merged.dtb"`로 넘긴다(가이드). 실제 보드에서는 U-Boot가 `fdt apply`로 적용한다(D6, C 모듈).

### 11.4 시간과 디스크 (기억 기반 — 이 환경에서 잴 수 없음)

| 항목 | 대략 | 메모 |
|---|---|---|
| 첫 빌드 (sstate 없음, 8코어 · 32 GB) | 1.5 ~ 3시간 | 툴체인 · glibc · systemd · 커널이 대부분 |
| `downloads/` | 5 ~ 10 GB | 소스 tarball · git mirror. 빌드 간 공유 |
| `sstate-cache/` | 5 ~ 15 GB | 태스크 출력 캐시. CI의 핵심 자산 |
| `tmp/` (rm_work 켬) | 15 ~ 30 GB | 끄면 2~3배 |
| 드라이버 `.c` 한 줄 수정 후 | 1 ~ 3분 | 그 레시피 + do_rootfs + do_image만 다시 |
| 커널 fragment 한 줄 수정 후 | 10 ~ 30분 | 커널 재컴파일 + 모듈 레시피들 + 이미지 |
| 이미지 크기 (ext4, systemd + openssh) | 수십 ~ 100 MB 대 | `IMAGE_ROOTFS_EXTRA_SPACE` 64 MB 포함 |

정확한 값은 첫 빌드 후 `buildstats`(`tmp/buildstats/`)와 `buildhistory`(`buildhistory/images/.../image-info.txt`)에서 읽어 README에 적는다 — 면접에서 "직접 잰 숫자"로 쓸 수 있다.

---

## 12. 흔한 빌드 에러와 QA 이슈 — 메시지와 고치는 법

QA 검사(`insane.bbclass`)는 do_package_qa 등에서 돈다. **wrynose의 기본 `ERROR_QA`** 목록과 메시지 문구는 oe-core 소스에서 직접 확인했다.

```sh
cd /var/tmp/S6/work/openembedded-core/meta
sed -n 42,49p classes-global/insane.bbclass
grep -n "GNU_HASH (didn.t pass\|but no providers found\|does not match for" classes-global/insane.bbclass
grep -n "installed but not shipped" lib/oe/package.py
grep -n "was already stripped\|not obeying usrmerge" lib/oe/package.py classes-global/insane.bbclass
```

```text
WARN_QA ?= "32bit-time native-last pep517-backend"
ERROR_QA ?= "\
    already-stripped arch buildpaths build-deps debug-deps dev-deps dev-elf dev-so empty-dirs file-rdeps \
    ldflags libdir missing-ptest rpaths staticdev textrel version-going-backwards \
    ${CHECKLAYER_REQUIRED_TESTS}"

# Add usrmerge QA check based on distro feature
ERROR_QA:append = "${@bb.utils.contains('DISTRO_FEATURES', 'usrmerge', ' usrmerge', '', d)}"
425:        oe.qa.handle_error("ldflags", "File %s in package %s doesn't have GNU_HASH (didn't pass LDFLAGS?)" % (path, name), d)
681:                msg = pn + ": The LIC_FILES_CHKSUM does not match for " + url
909:                    error_msg = "%s contained in package %s requires %s, but no providers found in RDEPENDS:%s?" % \
1525:        msg = pn + ": Files/directories were installed but not shipped in any package:"
lib/oe/package.py:173:                            bb.warn("File '%s' from %s was already stripped, this will prevent future debugging!" % (file[len(dstdir):], pn))
lib/oe/package.py:1207:                        msg = "File '%s' from %s was already stripped, this will prevent future debugging!" % (file[len(dvar):], pn)
classes-global/insane.bbclass:945:            msg = "%s package is not obeying usrmerge distro feature. /%s should be relocated to /usr." % (pkg, f)
```

출력에서 볼 것: 우리 distro는 `usrmerge`를 켰으므로 **`usrmerge` QA도 에러**다. 이 표의 메시지는 위 소스의 문구이고, 실제 빌드에서 본 출력은 아니다.

| QA 이름 / 단계 | 메시지 (소스 문구) | 이 프로젝트에서 생길 수 있는 경우 | 고치는 법 |
|---|---|---|---|
| `ldflags` | `File ... doesn't have GNU_HASH (didn't pass LDFLAGS?)` | 손 Makefile이 `$(CC) -o x x.c`로 링크 (Yocto가 넘긴 `LDFLAGS` 무시) | Makefile에서 `$(CFLAGS) $(LDFLAGS)` 사용. CMake는 자동 |
| `file-rdeps` | `... requires /bin/bash, but no providers found in RDEPENDS:...?` | `#!/bin/bash` 스크립트를 설치하고 bash를 RDEPENDS에 안 적음 | `RDEPENDS:${PN} += "bash"` 또는 `#!/bin/sh`로 |
| license checksum (do_populate_lic) | `The LIC_FILES_CHKSUM does not match for file://LICENSE;md5=...` | LICENSE 파일 연도 · 이름 수정 | 바뀐 내용을 확인하고 md5 갱신 (9절 lint가 미리 잡음) |
| installed-vs-shipped (do_package) | `Files/directories were installed but not shipped in any package:` | CMake가 `/usr/share/donlab/` 등 FILES에 없는 경로에 설치 | `FILES:${PN} += "${datadir}/donlab"` 또는 설치 안 하기 |
| `already-stripped` | `File '...' from ... was already stripped, this will prevent future debugging!` | 빌드 스크립트가 `strip`하거나 `-s`로 링크 | strip 제거 (Yocto가 `-dbg`로 분리) |
| `usrmerge` | `... package is not obeying usrmerge distro feature. /lib should be relocated to /usr.` | Makefile이 `/lib/modules`, `/lib/systemd`에 하드코딩 설치 | `${nonarch_base_libdir}`, `${systemd_system_unitdir}` 변수 사용 (5.1절) |
| `buildpaths` | (요지) 빌드 경로 문자열이 패키지 파일에 남음 | `__FILE__`을 쓰는 매크로, 디버그 경로 | `-ffile-prefix-map`(기본)을 덮어쓰지 않기 (K4 4.3) |
| 파싱: SKIPPED | `incompatible with machine donlab-qemu (not in COMPATIBLE_MACHINE)` | 파생 machine에 `MACHINEOVERRIDES` 누락 | 3.1절 (6.6절에서 실제 확인) |
| do_kernel_metadata | `Could not locate BSP definition for ...` | `KMACHINE` 미설정 | 6.2절 |
| do_kernel_configcheck | `configuration warnings detected, werror is set, promoting to fatal` | fragment 옵션이 의존성 때문에 탈락 | 6.5절처럼 부모 옵션 추가 |

QA를 끄는 `INSANE_SKIP:${PN} += "ldflags"`는 **바이너리 vendor blob처럼 고칠 수 없는 경우에만** 쓰고, 쓸 때는 이유를 주석으로 남긴다. 면접에서 "QA 에러를 어떻게 처리했나"에 "INSANE_SKIP으로 껐다"만 답하면 감점이다.

---

## 13. CI와 재현성 — kas lock, 캐시, 파이프라인

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="24" font-size="13">GitHub Actions (.github/workflows/ci.yml)</text>
  <rect x="20" y="36" width="200" height="72" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="56" text-anchor="middle" font-size="13">lint · 매 push</text>
  <text x="120" y="76" text-anchor="middle" font-size="12">yamllint · lint-layer.py</text>
  <text x="120" y="95" text-anchor="middle" font-size="12">bash -n · 수 분</text>
  <rect x="240" y="36" width="200" height="72" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="56" text-anchor="middle" font-size="13">parse · 매 push</text>
  <text x="340" y="76" text-anchor="middle" font-size="12">kas checkout + bitbake -p</text>
  <text x="340" y="95" text-anchor="middle" font-size="12">userns 허용한 runner VM</text>
  <rect x="460" y="36" width="200" height="72" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="560" y="56" text-anchor="middle" font-size="13">build · main / tag</text>
  <text x="560" y="76" text-anchor="middle" font-size="12">self-hosted runner</text>
  <text x="560" y="95" text-anchor="middle" font-size="12">kas build kas/ci.yml</text>
  <line x1="220" y1="72" x2="238" y2="72" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="440" y1="72" x2="458" y2="72" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="340" y="146" text-anchor="middle" font-size="13">build 잡 = kas build 한 줄. 안에서 일어나는 일:</text>
  <rect x="20" y="164" width="140" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="90" y="187" text-anchor="middle" font-size="13">kas/donlab.yml</text>
  <text x="90" y="207" text-anchor="middle" font-size="12">+ donlab.lock.yml</text>
  <rect x="170" y="164" width="110" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="225" y="187" text-anchor="middle" font-size="12">kas checkout</text>
  <text x="225" y="207" text-anchor="middle" font-size="12">git clone ×4</text>
  <rect x="290" y="164" width="130" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="355" y="187" text-anchor="middle" font-size="13">conf 생성</text>
  <text x="355" y="207" text-anchor="middle" font-size="12">local.conf 등</text>
  <rect x="430" y="164" width="120" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="490" y="187" text-anchor="middle" font-size="13">bitbake</text>
  <text x="490" y="207" text-anchor="middle" font-size="12">sstate 재사용</text>
  <rect x="560" y="164" width="110" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="615" y="187" text-anchor="middle" font-size="13">deploy/</text>
  <text x="615" y="207" text-anchor="middle" font-size="12">이미지·SBOM</text>
  <line x1="160" y1="192" x2="168" y2="192" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="280" y1="192" x2="288" y2="192" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="420" y1="192" x2="428" y2="192" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="550" y1="192" x2="558" y2="192" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="300" y="252" width="360" height="40" rx="6" fill="none" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="480" y="277" text-anchor="middle" font-size="12">DL_DIR · SSTATE_DIR (runner 디스크에 영구)</text>
  <line x1="490" y1="252" x2="490" y2="222" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="20" y="252" width="270" height="40" rx="6" fill="none" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="155" y="277" text-anchor="middle" font-size="12">리뷰 대상: lock diff · buildhistory</text>
</svg>
```

그림 4 — CI 세 단계와 build 잡 안의 kas 흐름. 앞의 두 잡은 GitHub 무료 runner에서 몇 분이면 끝나고, 실제 이미지 빌드는 디스크·캐시가 있는 self-hosted runner에서만 돈다. 잡 시간은 기억 기반 추정이다.

### 13.1 왜 세 단계로 나누나

- **lint**: 사람의 실수(옛 문법, md5, 경로)를 1분 안에 돌려준다. PR마다 돈다.
- **parse**: `bitbake -p`(전체 파싱)는 빌드 없이 "모든 레시피가 이 조합에서 파싱되는가"를 본다 — 의존 layer의 브랜치가 움직였을 때 가장 먼저 깨지는 곳이다. GitHub의 Ubuntu 24.04 runner에도 이 VM과 같은 AppArmor 제한이 있으면 sysctl로 푼다(runner VM은 일회용이라 괜찮다. 이 랩 VM에서는 하지 않는다). 이 잡이 runner에서 실제로 통과하는지는 확인하지 않았다(가이드).
- **build**: GitHub 호스트 runner는 디스크(수십 GB)와 시간 제한 때문에 Yocto 첫 빌드에 맞지 않는다(기억 기반). 그래서 `runs-on: [self-hosted, linux, yocto]`. `kas/ci.yml`이 공유 캐시 경로와 `testimage`(runqemu로 부팅해 ping · ssh · systemd 검사)를 켠다.

워크플로 파일의 build 잡 부분이다.

```text
  build:
    if: github.ref == 'refs/heads/main' || startsWith(github.ref, 'refs/tags/')
    runs-on: [self-hosted, linux, yocto]
    needs: parse
    timeout-minutes: 720
    steps:
      - uses: actions/checkout@v4
      - name: build image (DL_DIR/SSTATE_DIR are persistent on the runner)
        run: kas build kas/ci.yml
      # kas/ci.yml adds IMAGE_CLASSES += "testimage" and TEST_RUNQEMUPARAMS = "slirp nographic"
      - name: smoke test with runqemu (testimage)
        run: kas shell kas/ci.yml -c "bitbake -c testimage donlab-image"
      - uses: actions/upload-artifact@v4
        with:
          name: donlab-image-donlab-qemu
          path: |
            ${{ env.KAS_WORK_DIR }}/build/tmp/deploy/images/donlab-qemu/donlab-image-donlab-qemu.rootfs.ext4.zst
            ${{ env.KAS_WORK_DIR }}/build/tmp/deploy/images/donlab-qemu/Image
            ${{ env.KAS_WORK_DIR }}/build/tmp/deploy/images/donlab-qemu/donlab-image-donlab-qemu.rootfs.manifest
            ${{ env.KAS_WORK_DIR }}/build/tmp/deploy/images/donlab-qemu/donlab-image-donlab-qemu.rootfs.spdx.json
```

`kas dump`로 overlay가 제대로 겹쳐지는지는 실제로 확인했다(앞부분 생략).

```sh
KAS_WORK_DIR=/var/tmp/S6/work KAS_BUILD_DIR=/var/tmp/S6/work/build-dump kas dump kas/ci.yml 2>&1 | sed -n "/local_conf_header/,\$p" | head -16
```

```text
local_conf_header:
  donlab-base: |
    # keep the work directories small: delete each recipe's WORKDIR after it is done
    INHERIT += "rm_work"
    # share downloads and sstate between builds (CI mounts these as caches)
    DL_DIR ?= "${TOPDIR}/../downloads"
    SSTATE_DIR ?= "${TOPDIR}/../sstate-cache"
    BB_NUMBER_THREADS ?= "8"
    PARALLEL_MAKE ?= "-j 8"
    # record what went into every image (package lists, file lists, sizes)
    INHERIT += "buildhistory"
    BUILDHISTORY_COMMIT = "1"
  donlab-ci: |
    IMAGE_CLASSES += "testimage"
    TEST_RUNQEMUPARAMS = "slirp nographic"
    TEST_SUITES = "ping ssh date df systemd"
```

출력에서 볼 것: 기본 파일의 `donlab-base` 블록 뒤에 `donlab-ci` 블록이 붙는다. local.conf에서는 뒤에 오는 `DL_DIR = "/srv/..."`(ci.yml의 아래쪽, 출력에서는 잘렸다)가 앞의 `?=` 기본값을 이긴다.

### 13.2 재현성 체크리스트

| 무엇을 고정하나 | 어떻게 | 이 저장소에서 |
|---|---|---|
| layer 버전 | `kas lock` → `*.lock.yml` 커밋 | `kas/donlab.lock.yml` |
| 소스 버전 | 레시피의 `SRCREV` (브랜치 이름 금지), file:// 소스는 저장소 커밋 | 데몬을 별도 저장소로 옮길 때 `SRCREV` |
| 빌드 호스트 | kas-container(공식 컨테이너 이미지)나 고정 runner | CI build 잡 |
| 결과 비교 | `buildhistory` diff, `SOURCE_DATE_EPOCH`, sstate 해시 | `BUILDHISTORY_COMMIT = "1"` |
| 무엇이 들어갔나 | SPDX SBOM (`create-spdx`, 기본 켜짐) | 업로드 아티팩트에 `.spdx.json` |

재현성의 원리(sstate · hash equivalence · SOURCE_DATE_EPOCH)는 K5 7–8절에 있다.

---

## 14. 커널 안에서는 무슨 일이 일어나나 — 부팅부터 journal까지

이미지가 부팅되면 레시피가 남긴 파일들이 이 순서로 맞물린다. 8.2절의 두 번째 부팅이 정확히 이 경로를 탔다.

```svg
<svg viewBox="0 0 680 450" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="160" y="24" text-anchor="middle" font-size="13">유저 공간 (systemd)</text>
  <text x="520" y="24" text-anchor="middle" font-size="13">커널</text>
  <line x1="340" y1="32" x2="340" y2="444" stroke="#888" stroke-dasharray="4 4"/>
  <rect x="20" y="40" width="280" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="61" text-anchor="middle" font-size="13">systemd-modules-load.service</text>
  <text x="160" y="80" text-anchor="middle" font-size="12">modules-load.d/donlab_sensor.conf</text>
  <rect x="20" y="110" width="280" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="131" text-anchor="middle" font-size="13">modprobe donlab_sensor</text>
  <text x="160" y="150" text-anchor="middle" font-size="12">modprobe.d: fake_device=1</text>
  <rect x="380" y="110" width="280" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="520" y="131" text-anchor="middle" font-size="13">init_module → donlab_init()</text>
  <text x="520" y="150" text-anchor="middle" font-size="12">platform_driver_register()</text>
  <rect x="380" y="180" width="280" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="520" y="201" text-anchor="middle" font-size="13">버스 매칭: of_match_table</text>
  <text x="520" y="220" text-anchor="middle" font-size="12">또는 fake platform_device</text>
  <rect x="380" y="250" width="280" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="520" y="271" text-anchor="middle" font-size="13">donlab_probe()</text>
  <text x="520" y="290" text-anchor="middle" font-size="12">misc_register() → devtmpfs 노드</text>
  <rect x="20" y="250" width="280" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="160" y="271" text-anchor="middle" font-size="13">/dev/donlab_sensor</text>
  <text x="160" y="290" text-anchor="middle" font-size="12">char 10:258 · mode 0444</text>
  <rect x="20" y="320" width="280" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="341" text-anchor="middle" font-size="13">donlab-sensord.service</text>
  <text x="160" y="360" text-anchor="middle" font-size="12">ConditionPathExists · DeviceAllow</text>
  <rect x="380" y="320" width="280" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="520" y="341" text-anchor="middle" font-size="13">read() → donlab_read()</text>
  <text x="520" y="360" text-anchor="middle" font-size="12">copy_to_user: 샘플 한 줄</text>
  <rect x="20" y="390" width="280" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="411" text-anchor="middle" font-size="13">stdout → journald</text>
  <text x="160" y="430" text-anchor="middle" font-size="12">journalctl -u donlab-sensord</text>
  <line x1="160" y1="90" x2="160" y2="108" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="300" y1="135" x2="378" y2="135" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="520" y1="160" x2="520" y2="178" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="520" y1="230" x2="520" y2="248" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="380" y1="275" x2="302" y2="275" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="300" x2="160" y2="318" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="300" y1="345" x2="378" y2="345" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="160" y1="370" x2="160" y2="388" stroke="currentColor" marker-end="url(#ah5)"/>
</svg>
```

그림 5 — 부팅 시 경로. 파랑은 systemd 쪽, 주황은 커널 쪽, 초록은 둘이 만나는 장치 노드. 레시피 변수 두 개(`KERNEL_MODULE_AUTOLOAD`, `module_conf_*`)와 unit 파일 하나가 이 경로 전체를 정한다.

단계별로 커널 구조체와 연결하면:

1. `modprobe`는 `modules.dep`(이미지 생성 때 `depmod`가 만든 것)에서 경로를 찾고 `finit_module()` syscall로 `.ko`를 넘긴다. 커널은 ELF를 재배치하고 `vermagic`을 확인한 뒤 `module_init` 함수(`donlab_init`)를 부른다(E3). vermagic이 다르면 여기서 `Invalid module format` — Yocto에서는 모듈 레시피가 같은 커널 빌드 트리로 빌드되므로 일어나지 않는다. **이 프로젝트가 out-of-tree 모듈을 레시피로 만든 가장 큰 이유**다.
2. `platform_driver_register()`가 드라이버를 platform 버스에 올리면 버스의 `match`가 기존 장치들과 짝을 찾는다: DT에서 온 장치는 `of_match_table`의 compatible 문자열로, fake 장치는 이름(`"donlab-sensor"`)으로 맞는다(G2). 짝이 맞으면 `probe`.
3. `misc_register()`는 동적 minor를 받고 `device_create()`로 `struct device`를 만든다. devtmpfs가 즉시 `/dev/donlab_sensor`를 만들고, uevent가 udev로 간다(G9). 그래서 `ConditionPathExists`가 확인하는 시점에 노드가 있다.
4. 데몬의 `read()`는 VFS → `misc_open`이 바꿔 끼운 `file_operations`의 `.read`(`donlab_read`)로 간다. `f->private_data`가 `struct miscdevice`를 가리키도록 misc 코어가 넣어 주기 때문에 `container_of`로 드라이버 상태를 찾는다(F 모듈, A3).
5. systemd의 `DeviceAllow=`는 cgroup v2의 BPF device 프로그램으로 구현된다 — 커널이 `open()` 때 (char, 10, 258, read) 접근을 검사한다. `DevicePolicy=closed`면 목록 밖 장치는 `EPERM`.

---

## 15. Embedded 실무에서 다시 보기

### 15.1 실제 보드로 옮기기 — product layer와 BSP layer 분리

QEMU machine은 "보드가 없어도 돌아가는 레퍼런스"다. i.MX 8M Plus 같은 실제 보드로 옮길 때 바뀌는 것은 **BSP 쪽뿐**이어야 한다.

| 파일 | QEMU (지금) | 실제 보드 (예: i.MX 8M Plus 캐리어) |
|---|---|---|
| machine conf | `meta-donlab/conf/machine/donlab-qemu.conf` (`require qemuarm64.conf`) | `meta-donlab-bsp/conf/machine/donlab-imx8mp.conf` (`require conf/machine/include/imx8mp-evk.inc`, K5 2.3) |
| 커널 레시피 | `linux-yocto_%.bbappend` | vendor 커널 bbappend(예: `linux-fslc-imx_%.bbappend`) — `KMACHINE` 불필요, `.cfg` 대신 defconfig 조각 · DT 패치는 보드 `.dts` 자체 |
| DT | overlay `.dtbo` + fake 장치 | 보드 `.dts`에 노드를 직접, 또는 확장 보드용 overlay를 U-Boot가 `fdt apply` |
| `module_conf_*` | `fake_device=1` (`:donlab-qemu`) | 없음 — 노드가 있으니 udev modalias로도 로드된다 |
| 드라이버 · 데몬 · 이미지 · distro | 그대로 | 그대로 |

마지막 줄이 이 분리의 핵심이다. 제품 layer에 보드 이름이 박히지 않으면 같은 이미지 레시피로 EVK · 시제품 · 양산 보드를 다 빌드한다. 면접에서 "BSP layer와 product layer를 왜 나누나"의 답이다.

### 15.2 장면 — "새 보드에서 센서 서비스가 안 뜬다"

증상: 이미지는 같은데 새 보드에서만 `donlab-sensord`가 `inactive (condition failed)`. 순서대로 본다: `systemctl status`의 Condition 줄 → `/dev/donlab_sensor` 없음 → `lsmod`에 모듈 있음 → `dmesg`에 probe 로그 없음 → `/proc/device-tree/`에 노드 없음 → **새 보드 dts에 노드가 빠졌다**(또는 overlay가 적용 안 됨). 이 흐름이 그림 5를 거꾸로 따라가는 것이다. SSD 펌웨어에서 "명령이 안 먹는다 → 큐에 들어갔나 → 핸들러가 불렸나 → 하드웨어 레지스터가 바뀌었나" 순으로 계층을 내려가던 것과 같은 방법이다.

### 15.3 장면 — "보안 감사에서 데몬이 root로 돈다고 지적받았다"

8.3절이 답이다. `DynamicUser`와 하드닝 줄을 넣고 `systemd-analyze security` 점수를 감사 보고서에 붙인다. 장치 접근은 udev 규칙으로 그룹을 주거나(L2), 이 프로젝트처럼 드라이버가 노드 권한을 정한다. 이미지에 들어간 버전을 증명하는 것은 SPDX SBOM이다(K7).

---

## 16. GitHub에 올리기 · 면접에서 보여 주기

### 16.1 저장소로 옮기기

```sh
# Mac에서 (가이드 — GitHub 계정 작업이라 이 노트에서는 실행하지 않음)
cp -R ~/.elinux_scratch/S6/meta-donlab ~/src/meta-donlab && cd ~/src/meta-donlab
git log --oneline            # 3개 커밋이 이미 있다
gh repo create meta-donlab --public --source . --push
```

올리기 전에 할 일: `tests/`의 랩 경로(`/var/lab/...`)를 환경 변수 기본값으로만 남겼는지 확인(이미 `KB=`, `OUT=`로 바꿀 수 있다), `HOMEPAGE`의 계정 이름을 실제로 바꾸기, `kas/donlab.lock.yml` 갱신.

### 16.2 README 구성 (저장소에 초안이 있다)

1. 한 문단 요약 + 표(Yocto 릴리스, machine, distro, image, kernel).
2. layer 트리와 각 디렉터리 한 줄 설명.
3. Build: `kas build kas/donlab.yml`, dev/RO 변형, runqemu.
4. "What runs where" — 그림 5를 텍스트로.
5. "Verify without a Yocto build" — lint · tests 스크립트 목록.
6. Real board 절 — 15.1절 표의 요약.
7. 숫자: 노출 점수 7.6 → 0.5, (빌드 후) 첫 빌드 시간 · 증분 빌드 시간 · 이미지 크기 · 부팅 시간.
8. License.

### 16.3 5분 데모 스크립트

| 분 | 보여 줄 것 | 말할 것 |
|---|---|---|
| 0–1 | layer 트리, `layer.conf` | "제품 layer 하나. 우선순위 10, wrynose 호환, 의존은 core 하나" |
| 1–2 | 모듈 레시피 + `KERNEL_MODULE_AUTOLOAD` | "드라이버는 레시피로 커널과 같은 트리에서 빌드 — vermagic 불일치가 원천 차단된다" |
| 2–3 | 커널 bbappend, `.cfg`, LOST 데모 | "fragment가 조용히 빠지는 걸 configcheck werror로 막았다" |
| 3–4 | 8.2절 보고 출력, 노출 점수 그래프 | "부팅 후 자동 로드 → 서비스 → journal. 하드닝으로 7.6 → 0.5" |
| 4–5 | kas lock, CI 그림 | "누가 언제 빌드해도 같은 커밋. lint/parse는 매 PR, 빌드는 self-hosted" |

솔직하게 말할 것: "이미지 빌드 자체는 이 랩 환경에서 돌리지 않았고, 파서 검증 + Yocto 밖 빌드 + QEMU로 소스를 검증했다" — 그리고 빌드 서버에서 돌린 뒤라면 그 숫자로 바꾼다. 면접관은 **어디까지 직접 확인했는지 정확히 말하는 사람**을 신뢰한다.

---

## 17. 확장 과제

1. **S4 IIO 드라이버로 교체**: `donlab-sensor-mod` 대신 S4의 I2C IMU 드라이버를 레시피로. `RDEPENDS`에 `libiio-utils`(meta-oe), 데몬은 `/sys/bus/iio/devices/.../in_accel_x_raw` 또는 buffer를 읽게.
2. **데몬을 별도 git 저장소로**: `SRC_URI = "git://...;branch=main;protocol=https"` + `SRCREV`, `devtool modify`로 개발 루프(K5 4.2).
3. **모듈 서명**: 커널 레시피의 서명 키로 out-of-tree 모듈도 서명하고 `module.sig_enforce=1`로 부팅(L5).
4. **RO rootfs 실전**: `kas/ro-rootfs.yml` 이미지에 overlayfs `/etc` 또는 data 파티션, `machine-id` 처리.
5. **OTA 완성**: L4의 키 체계로 `donlab-bundle`을 실제로 만들고 A/B 슬롯 `system.conf` 작성.
6. **testimage 케이스 추가**: `lib/oeqa/runtime/cases/donlab.py`에 "서비스 active + 샘플 범위" 테스트(P6).
7. **SDK**: `bitbake donlab-image -c populate_sdk`로 데몬을 Yocto 밖에서 크로스 빌드(K5 9).

---

## 18. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 파생 machine에 `MACHINEOVERRIDES` 누락 | linux-yocto SKIPPED, 이미지에서 엉뚱한 에러 | `:qemuarm64` override가 꺼짐 | `MACHINEOVERRIDES =. "qemuarm64:"` (6.6절) |
| `KMACHINE` 미설정 | `Could not locate BSP definition` | kernel-cache에 machine 이름 없음 | `KMACHINE:<machine> = "qemuarm64"` |
| `FILESEXTRAPATHS:prepend =` | `file://donlab.cfg` 못 찾음 | `THISDIR` 지연 확장 | `:=` |
| `RDEPENDS_${PN}` (옛 문법) | 에러 없음, 기기에서 의존 패키지 없음 | 그냥 새 변수가 됨 | `RDEPENDS:${PN}` + lint (9.3절) |
| poky require 후 `INIT_MANAGER` 안 적음 | unit은 있는데 서비스가 안 뜸 | poky 기본이 sysvinit | `INIT_MANAGER = "systemd"` (3.2절) |
| fragment 옵션의 부모 누락 | 옵션이 조용히 빠진 커널 | Kconfig `depends on` | 부모 추가 + `KMETA_AUDIT_WERROR` (6.5절) |
| 암호 해시의 `$` 미이스케이프 | 로그인 실패 | `$6` 등이 빈 문자열로 확장 | `\$` (7.1절) |
| local.conf에서 `IMAGE_INSTALL +=` | 부팅 필수 패키지가 빠짐 | 이미지 기본값 덮어씀 | `IMAGE_INSTALL:append` 또는 `CORE_IMAGE_EXTRA_INSTALL` |
| unit에 `[Install]` 없음 | `systemctl enable`이 조용히 아무것도 안 함 | 링크 대상이 없음 | `WantedBy=multi-user.target` (8.2절) |
| 데몬 stdout 미플러시 | journal에 로그가 몇 분 늦게 | 파이프 버퍼링 | `fflush` 또는 `setvbuf(stdout, NULL, _IOLBF, 0)` |

---

## 19. 면접에서 이렇게 말한다

**Q.** How did you add your out-of-tree driver to the Yocto image?

**A.** `inherit module` 레시피로 만들었다. module.bbclass가 커널 레시피가 공개한 같은 빌드 트리로 빌드하니까 vermagic이 항상 맞는다. `.ko` 이름에서 `kernel-module-donlab-sensor` 패키지가 자동으로 생기고, `KERNEL_MODULE_AUTOLOAD`가 `modules-load.d` 파일을 만들어 부팅 때 systemd가 로드한다. machine conf의 `MACHINE_EXTRA_RRECOMMENDS`로 이미지가 따로 적지 않아도 들어가게 했다.

> I packaged it as a recipe that inherits `module`, so it is built against the exact kernel build tree the kernel recipe exports — vermagic can never mismatch. The `.ko` becomes a `kernel-module-*` package automatically, `KERNEL_MODULE_AUTOLOAD` generates the modules-load.d entry so systemd loads it at boot, and the machine pulls it in through `MACHINE_EXTRA_RRECOMMENDS`.

**꼬리 질문**: "DT가 있는데 왜 autoload가 필요한가?" → modalias로 udev가 로드할 수도 있지만, QEMU처럼 노드가 없는 machine을 위해 명시적 경로를 뒀다(module_conf로 fake 장치).

**Q.** How do you change the kernel configuration in Yocto without forking the defconfig?

**A.** 커널 레시피에 bbappend를 달고 기능 단위 `.cfg` fragment를 `SRC_URI`에 넣는다. kernel-yocto가 BSP 설정 위에 병합하고 do_kernel_configcheck가 "요청한 값이 살아남았는지" 감사한다. 의존성 때문에 조용히 빠지는 옵션이 있어서 `KMETA_AUDIT_WERROR`로 경고를 빌드 실패로 바꿨다. 모든 변경은 machine override로 감쌌다 — layer를 추가만 해서 다른 보드 커널이 바뀌면 안 되니까.

> A bbappend on the kernel recipe adds a feature-sized `.cfg` fragment. kernel-yocto merges it on top of the BSP config and `do_kernel_configcheck` audits that every requested value survived; I set `KMETA_AUDIT_WERROR` so a silently dropped option fails the build. Everything is guarded by the machine override so the layer is inert for other machines.

**Q.** What is the difference between a BSP layer, a distro layer and a product layer?

**A.** BSP는 "어떤 하드웨어인가"(machine conf, 커널·부트로더 레시피, DT), distro는 "어떤 OS 정책인가"(init 시스템, DISTRO_FEATURES, 패키지 형식, 라이선스 정책), product는 "무엇을 싣나"(앱·드라이버 레시피, 이미지). 셋을 나누면 같은 이미지를 여러 보드에 빌드하고, 보드를 바꿀 때 BSP만 바꾼다.

> BSP answers "what hardware" — machine, kernel, bootloader, device tree. Distro answers "what OS policy" — init system, distro features, package format, license policy. Product answers "what we ship" — our recipes and images. Keeping them separate means one image recipe builds for the EVK, the prototype and the production board.

**Q.** How do you make a Yocto build reproducible for your team and CI?

**A.** kas 파일 하나에 layer와 브랜치를 적고 `kas lock`으로 커밋을 박제해 lock 파일을 커밋한다. 레시피는 `SRCREV`로 고정한다. CI는 lint → 전체 파싱 → self-hosted runner의 빌드로 나누고, DL_DIR · sstate를 runner에 영구 보관해 증분 빌드를 분 단위로 만든다. buildhistory와 SPDX로 "무엇이 들어갔나"를 남긴다.

> One kas file describes the layers, `kas lock` pins every repo to a commit and the lock file is committed, recipes pin `SRCREV`. CI runs lint, a full parse, then the image build on a self-hosted runner with persistent downloads and sstate. buildhistory and SPDX record exactly what went into each image.

**Q.** You derived a machine from qemuarm64 and the kernel recipe disappeared. Why?

**A.** `require`로 값은 가져오지만 override 이름은 새 `MACHINE` 이름이 된다. linux-yocto의 `COMPATIBLE_MACHINE`과 `KBRANCH:qemuarm64`가 맞지 않아 레시피가 SKIPPED 됐다. `MACHINEOVERRIDES =. "qemuarm64:"`로 부모 override를 살리고, kernel-cache 조회용 `KMACHINE`도 부모 이름으로 맞춘다. 파서로 SKIPPED 이유를 직접 확인했다.

> `require` copies the values but the override name becomes the new MACHINE, so `COMPATIBLE_MACHINE` and the `:qemuarm64` overrides stop matching and linux-yocto is skipped. Prepending `qemuarm64:` to `MACHINEOVERRIDES` and setting `KMACHINE` fixes it; I confirmed the skip reason with the BitBake parser.

**Q.** How do you handle a `do_package_qa` error?

**A.** 메시지가 QA 이름을 알려 준다. ldflags면 빌드 시스템이 LDFLAGS를 안 쓰는 것, file-rdeps면 RDEPENDS 누락, installed-vs-shipped면 FILES 누락, usrmerge면 하드코딩 경로. 원인을 고치는 게 원칙이고 `INSANE_SKIP`은 고칠 수 없는 바이너리에만, 이유를 주석으로 남겨서 쓴다.

> The message names the check. ldflags means the build ignores our LDFLAGS, file-rdeps a missing RDEPENDS, installed-vs-shipped a missing FILES entry, usrmerge a hard-coded /lib path. I fix the cause; `INSANE_SKIP` is only for prebuilt blobs and always comes with a comment.

**Q.** How did you test the image content without hardware?

**A.** 두 겹이다. 빌드 전에는 파서와 lint, 그리고 레시피가 쓰는 make/cmake 명령을 Yocto 밖에서 같은 커널 트리로 돌려 QEMU initramfs에서 DT 경로와 fake 경로를 둘 다 확인했다. 이미지 수준에서는 systemd 게스트에 설치 트리를 깔고 재부팅해서 자동 로드 · 서비스 · journal을 확인했다. 빌드 서버에서는 같은 확인을 testimage로 자동화한다.

> Two layers. Before building, the parser, a lint, and the recipes' own make/cmake steps run outside Yocto against the same kernel tree, booted in QEMU for both the device-tree and the fallback path. At image level I installed the package tree into a systemd guest and rebooted to verify autoload, service and journal. On the build server the same checks become a testimage suite.

---

## 20. 직접 해보기

1. `donlab-qemu.conf`에서 `MACHINEOVERRIDES` 줄을 지우고 `bitbake-getvar --value KBRANCH -r linux-yocto`를 해 보라(빌드 호스트에서). 무엇이 나오나?
   정답/힌트: 레시피가 SKIPPED라 값을 못 얻는다. 파서 스크립트로는 6.6절의 SKIPPED 메시지.
2. `donlab.cfg`에 `CONFIG_IIO_ST_LSM6DSX=y`를 넣고 `check-kernel-fragment.sh`를 돌려라. 살아남나? I2C 쪽 드라이버(`IIO_ST_LSM6DSX_I2C`)는?
   정답/힌트: 핵심 옵션은 OK(랩 defconfig에 I2C · SPI가 있음). I2C 인터페이스 옵션은 `default I2C && IIO_ST_LSM6DSX`라 자동으로 켜진다 — Kconfig에서 `default`가 하는 일.
3. 데몬 레시피를 `inherit cmake` 대신 손 Makefile + `do_compile() { ${CC} -o ... }`로 바꿨을 때 어떤 QA 에러가 날지 예측하고, 고친 do_compile을 써라.
   정답/힌트: `ldflags`(GNU_HASH). `${CC} ${CFLAGS} ${LDFLAGS} ...`.
4. `module_conf_donlab_sensor`를 지우고 `KERNEL_MODULE_PROBECONF`만 남기면 어떻게 되나? 반대로 하면?
   정답/힌트: 4.2절 grep의 136행 — `module_conf_*`만 있으면 에러. PROBECONF만 있고 conf가 없으면 modprobe.d 파일이 안 생긴다.
5. systemd 게스트 테스트에서 `ConditionPathExists`를 지우고, `modules-load.d` 파일도 지운 상태로 부팅하면 서비스 상태는? `Restart=on-failure`가 무엇을 하나?
   정답/힌트: open 실패로 exit 1 → `RestartSec=2`마다 재시작 → 기본 start limit(`StartLimitBurst`/`StartLimitIntervalSec`)을 넘으면 `failed`. Condition이 있으면 "건너뜀"으로 끝난다.
6. `systemd-analyze security --offline=true`로 `SystemCallFilter=` 줄만 지웠을 때 점수가 얼마나 오르는지 재 보라.
   정답/힌트: VM에서 바로 잴 수 있다. 어느 항목이 가장 무거운지 출력 목록에서 찾는다.
7. `kas/donlab.lock.yml`을 지우고 `kas checkout` 출력의 경고를 다시 보라. lock이 있을 때와 무엇이 다른가?
   정답/힌트: "Using branch without commit ... is unsafe" 경고가 lock이 있으면 사라진다.

---

## 21. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| product layer | 제품 layer | 앱 · 드라이버 레시피와 이미지가 사는 layer. 보드 이름을 모른다 |
| BSP layer | 보드 지원 layer | machine conf, 커널 · 부트로더 레시피, DT |
| kas | Yocto 프로젝트 도구 | layer 저장소 · 브랜치 · local.conf를 YAML 하나로 |
| lock 파일 | `*.lock.yml` | 브랜치를 커밋 해시로 고정한 kas overlay |
| tinfoil | BitBake Python API | 설정 · 레시피를 파싱해 datastore를 돌려준다. bitbake-layers · devtool의 기반 |
| `MACHINEOVERRIDES` | override 목록 | `:qemuarm64` 같은 조건부 값이 켜지는 이름들. 뒤일수록 우선 |
| `KMACHINE` | 커널용 machine 이름 | yocto-kernel-cache에서 BSP 설명을 찾는 키 |
| config fragment | `.cfg` | 커널 설정의 일부만 담은 파일. BSP 설정 위에 병합 |
| `KERNEL_MODULE_AUTOLOAD` | 자동 로드 목록 | `modules-load.d` 파일을 생성하는 변수 |
| `BBFILES_DYNAMIC` | 조건부 레시피 경로 | 특정 layer가 있을 때만 파싱할 레시피 |
| `EXTRA_IMAGE_FEATURES` | 추가 이미지 기능 | local.conf · kas overlay에서 개발 기능을 켜는 변수 |
| QA (insane.bbclass) | 패키지 품질 검사 | ldflags · file-rdeps · usrmerge 등. ERROR_QA면 빌드 실패 |
| `DynamicUser` | 임시 사용자 | 서비스 시작마다 배정되는 uid. 파일 소유가 남지 않는다 |
| exposure level | 노출 점수 | `systemd-analyze security`의 0~10 하드닝 지표 |

---

## 22. 요약 & 체크리스트

`meta-donlab`은 Yocto 6.0 "wrynose" 위의 제품 layer 하나에 machine(`donlab-qemu`) · distro(`donlab`) · out-of-tree 드라이버 레시피 · CMake/systemd 데몬 레시피 · 커널 bbappend(fragment + DT overlay) · 이미지 레시피 · OTA 자리 · kas 프로젝트와 lock · lint · 테스트 · CI를 담았다. 이미지 빌드는 하지 않았지만, BitBake 파서로 모든 레시피와 변형(RO · dev · OTA)을 파싱했고, 레시피가 패키징할 파일을 Yocto 밖에서 같은 방식으로 빌드해 QEMU initramfs(DT · fake 두 경로)와 systemd 게스트(재부팅 후 자동 로드 · 서비스 · journal)에서 동작을 확인했다. 그 과정에서 파생 machine의 override 함정(SKIPPED), fragment 옵션 탈락, 옛 문법이 조용히 무시되는 문제, unit의 `[Install]` 누락을 실제로 재현했고, 서비스 하드닝으로 노출 점수를 7.6에서 0.5로 낮췄다.

- [ ] 제품 layer · BSP layer · distro의 역할을 나눠 설명할 수 있다
- [ ] `layer.conf`의 각 줄(`BBFILES`, `BBFILE_PRIORITY`, `LAYERSERIES_COMPAT`, `BBFILES_DYNAMIC`)을 설명할 수 있다
- [ ] 기존 machine에서 파생할 때 `MACHINEOVERRIDES`와 `KMACHINE`이 왜 필요한지 말할 수 있다
- [ ] `inherit module` 레시피를 쓰고, `kernel-module-*` 패키지 이름이 어디서 오는지 설명할 수 있다
- [ ] `KERNEL_MODULE_AUTOLOAD`와 `module_conf_*`가 만드는 파일과 부팅 때 그 파일을 읽는 주체를 말할 수 있다
- [ ] CMake + systemd 레시피를 쓰고 `SYSTEMD_SERVICE`, `systemd_system_unitdir`를 쓸 수 있다
- [ ] 커널 bbappend로 fragment와 DT 패치를 넣고, 탈락한 옵션을 찾는 방법을 보여 줄 수 있다
- [ ] 이미지 레시피에서 제품 기능과 개발 기능(`EXTRA_IMAGE_FEATURES`)을 분리할 수 있다
- [ ] tinfoil로 레시피 하나를 파싱해 변수 최종 값을 확인할 수 있다
- [ ] ldflags · file-rdeps · license checksum · installed-vs-shipped · usrmerge QA를 보고 원인을 말할 수 있다
- [ ] kas lock과 CI 3단계로 재현성을 설명할 수 있다
- [ ] 이 프로젝트를 5분 데모로 보여 주고, 무엇을 직접 확인했고 무엇이 가이드인지 정확히 말할 수 있다

### 스펙 자체 점검

- [x] SVG 5개 — XML 파싱 · 글자 폭 · viewBox 검사 스크립트 통과 (작성 후 실행)
- [x] 텍스트 fill 색 지정 없음, 표 셀 안 `|` 없음, 코드 예제 60줄 이하
- [x] 출력 블록은 전부 VM에서 실행한 결과, 가이드 블록은 출력 없음
- [x] 띄운 QEMU 전부 종료, `/var/tmp/S6` 큰 중간물 삭제

---

## 참고 자료

- Yocto Project 문서 (6.0): https://docs.yoctoproject.org/ — Reference Manual의 변수 목록(`KERNEL_MODULE_AUTOLOAD`, `BBFILES_DYNAMIC`, `EXTRA_USERS_PARAMS`, `IMAGE_FEATURES`), QA Error and Warning Messages, Kernel Development Manual(config fragment, `KMACHINE`)
- oe-core 소스(직접 읽은 파일): `meta/classes-recipe/module.bbclass`, `kernel-module-split.bbclass`, `kernel-yocto.bbclass`, `core-image.bbclass`, `image.bbclass`, `meta/classes/extrausers.bbclass`, `meta/classes-global/insane.bbclass`, `meta/lib/oe/package.py`, `meta-skeleton/recipes-kernel/hello-mod/`
- kas 문서: https://kas.readthedocs.io/ — project configuration, `kas lock`, `includes`
- meta-rauc README: https://github.com/rauc/meta-rauc · meta-swupdate: https://github.com/sbabic/meta-swupdate
- systemd: `systemd.exec(5)`, `systemd-analyze(1)` (security, `--offline`), `systemd-modules-load.service(8)`, `kernel-command-line(7)` (`systemd.run=`, `systemd.mount-extra=`) — https://www.freedesktop.org/software/systemd/man/
- Linux 커널: `Documentation/kbuild/modules.rst`(out-of-tree 빌드), `Documentation/devicetree/overlay-notes.rst`, `scripts/kconfig/merge_config.sh`
- Bootlin "Yocto Project and OpenEmbedded development" 교육 슬라이드: https://bootlin.com/training/yocto/
- 이 노트 세트: K4 · K5(Yocto 이론), E3(모듈), D6(overlay), G1 · G2 · G9(드라이버 모델 · 핫플러그), L1 · L4 · L5 · L6(systemd · OTA · 보안), K7(라이선스), P6(테스트 · CI)
