# K5. Yocto 실무 — BSP layer, devtool, 커널 레시피·패치, SDK/eSDK, sstate, 재현성

> **이 노트를 다 읽으면**: 회사 Yocto 저장소가 vendor BSP · 회사 BSP · distro · product layer와 kas lock 파일로 어떻게 짜여 있는지 그림으로 설명할 수 있다 · 새 보드용 machine conf, DT 패치, 커널 config fragment, U-Boot bbappend가 든 BSP layer를 직접 만들 수 있다 · `devtool modify → build → deploy-target → finish` 루프와 `menuconfig/diffconfig/kernel_configcheck`로 드라이버 개발을 돌릴 수 있다 · sstate·hash equivalence·PR server·SOURCE_DATE_EPOCH가 각각 무슨 문제를 푸는지, 빌드가 왜 다시 도는지 추적하는 방법을 말할 수 있다
> **왜 필요한가**: JD의 "Yocto experience"는 사실 이 노트의 내용이다. 입사 첫 주에 받는 일이 "이 보드에 IMU 드라이버 붙여서 이미지에 넣어 줘"이고, 면접에서는 "커널 패치를 Yocto에 어떻게 넣나", "빌드가 왜 처음부터 다시 도나"를 묻는다
> **Don 기준 난이도**: 펌웨어 빌드 파이프라인·릴리스 관리·패치 큐는 SSD 양산에서 해 본 것이다 / 새로 배울 부분은 **BitBake가 그 일을 layer·bbappend·task·hash로 표현하는 방식**과 커널·U-Boot 전용 클래스(`kernel-yocto`, `uboot-config`)의 동작
> **선행 노트**: K4(Yocto 기초 — layer, recipe 문법, 변수, task 모델, image recipe, local.conf, 호스트 준비). 이 노트는 K4의 문법을 반복하지 않는다. 비교 대상으로 K3(Buildroot), 커널 설정은 E3·E4, Device Tree는 D1~D5, out-of-tree 모듈은 G1, 라이선스·SBOM은 K7

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 이 노트의 실행 원칙 (먼저 읽기)

Don의 요청으로 **이 노트에서는 Yocto 빌드를 한 번도 돌리지 않는다.** 이유는 두 가지다.

1. 시간·디스크 — 이미지 한 번에 수 시간, 수십~백 GB 단위가 든다. 나중에 별도로 한다.
2. 이 VM(Ubuntu 26.04)은 AppArmor가 비특권 user namespace를 막는다. BitBake는 태스크 격리에 user namespace를 쓰므로 여기서 돌지 않는다. 이 설정은 바꾸지 않는다.

그래서 이 노트의 블록은 두 종류다.

- **실행한 예제**: 실제 Yocto 소스(openembedded-core, bitbake, meta-yocto, meta-freescale, yocto-kernel-cache)를 얕게 clone해서 `grep`/`sed`로 읽은 결과, 그리고 BitBake 없이도 검증되는 산출물(DT 패치, config fragment 병합, 모듈 빌드·QEMU insmod, kas checkout/lock, 재현성 실험). 출력은 VM(`uname -r` = `7.0.0-38-generic`, aarch64)에서 나온 그대로다.
- **"실행하지 않음 — 가이드"**: `bitbake`, `devtool`, SDK 설치처럼 BitBake가 필요한 명령. 정확한 명령과 무엇을 확인해야 하는지만 적고 **출력은 쓰지 않는다** (지어낸 출력 금지).

### 0.2 2026년 10월 기준 Yocto의 "입구"

먼저 지금 어떤 릴리스가 기준인지 확인한다. 예전 튜토리얼은 모두 `git clone poky`로 시작하지만, 지금은 그렇지 않다.

poky 저장소와 openembedded-core 저장소의 브랜치·태그를 비교하는 명령이다.

```sh
git ls-remote --heads https://git.yoctoproject.org/poky | awk '{print $2}' | sed 's|refs/heads/||' \
  | grep -E '^(kirkstone|scarthgap|styhead|walnascar|whinlatter|wrynose|master)$'
echo ---
git ls-remote --tags https://git.openembedded.org/openembedded-core 'yocto-6.0*' 'yocto-5.0.2*' \
  | grep -v '\^{}' | awk '{print $2}' | sed 's|refs/tags/||' | sort -V
```

```text
kirkstone
master
scarthgap
styhead
walnascar
---
yocto-5.0.2
yocto-5.0.20
yocto-6.0
yocto-6.0.1
yocto-6.0.2
yocto-6.0.3
yocto-6.0_M1
yocto-6.0_M2
yocto-6.0_M3
```

출력에서 볼 것: poky에는 `walnascar`(5.2)까지만 릴리스 브랜치가 있고 **`whinlatter`(5.3)와 `wrynose`(6.0) 브랜치가 없다.** 반면 openembedded-core에는 `yocto-6.0.3` 태그가 있다. yoctoproject.org 릴리스 페이지도 **Wrynose 6.0 = 현재 LTS**(2026년 4월 출시, 최신 6.0.3 = 2026년 8월, 2030년 4월까지 지원), Scarthgap 5.0 = 이전 LTS(5.0.20, 2028년 4월까지)라고 적고 있다. docs.yoctoproject.org의 Quick Start(6.0)는 poky clone 대신 **`bitbake-setup init`** 으로 시작한다.

즉 지금 구조는 "poky 한 덩어리"가 아니라 **bitbake + openembedded-core + meta-yocto를 따로 받는** 구조다. bitbake-setup이 무엇을 받는지는 bitbake 저장소 안 레지스트리 파일에 그대로 적혀 있다.

```sh
cd /var/tmp/K5
git clone -q --depth 1 -b 2.18 https://git.openembedded.org/bitbake k5_bitbake
sed -n 1,25p k5_bitbake/default-registry/configurations/poky-wrynose.conf.json
```

```text
{
    "description": "Poky - The Yocto Project testing distribution configurations and hardware test platforms, release 6.0 'wrynose'",
    "expires": "2030-05-31",
    "sources": {
        "bitbake": {
            "git-remote": {
                "uri": "https://git.openembedded.org/bitbake",
                "branch": "2.18",
                "rev": "2.18"
            }
        },
        "openembedded-core": {
            "git-remote": {
                "uri": "https://git.openembedded.org/openembedded-core",
                "branch": "wrynose",
                "rev": "wrynose"
            }
        },
        "meta-yocto": {
            "git-remote": {
                "uri": "https://git.yoctoproject.org/meta-yocto",
                "branch": "wrynose",
                "rev": "wrynose"
            }
        },
```

출력에서 볼 것: wrynose는 **bitbake 2.18 브랜치**와 짝이다 (openembedded-core의 `meta/conf/sanity.conf`도 `BB_MIN_VERSION = "2.18.0"`이다). Poky는 이제 "저장소"가 아니라 **meta-yocto 안의 `meta-poky` layer + DISTRO 이름**이다.

이 노트에서 읽은 소스는 다음과 같다 (모두 `/var/tmp/K5/`에 `--depth 1`).

| 저장소 | 브랜치/태그 | HEAD 커밋 (날짜) |
|---|---|---|
| openembedded-core | `yocto-6.0.3` 태그 | `ef022bf8` (2026-08-18) |
| bitbake | `2.18` | `6e260eb6` (2026-10-07) |
| meta-yocto | `wrynose` (`DISTRO_VERSION = "6.0.4"`로 이미 올라가 있음) | — |
| meta-freescale | `wrynose` | `b23da7c1` (2026-10-16) |
| yocto-kernel-cache | `yocto-6.18` | — |

meta-ti(`wrynose`), meta-tegra(OE4T, `wrynose`와 `master-l4t-r38.x`)에도 wrynose 브랜치가 있는 것을 `ls-remote`로 확인했지만, 이 노트는 i.MX8M Plus 하나만 깊게 판다. 구조는 셋 다 같다.

### 0.3 회사 저장소는 이렇게 생겼다

K4에서 "layer = 관심사별 폴더, 우선순위로 겹친다"를 배웠다. 회사에서는 이것이 **소유자별**로 나뉜다. 자기가 고칠 수 있는 layer와 고치면 안 되는 layer가 분명하다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">회사 Yocto 저장소의 layer 스택 (위가 우선순위 높음)</text>
  <rect x="20" y="36" width="420" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="230" y="54" text-anchor="middle" font-size="13">meta-k5-product (priority 10)</text>
  <text x="230" y="70" text-anchor="middle" font-size="12">이미지 레시피 · 앱 · 제품별 설정</text>
  <rect x="20" y="82" width="420" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="230" y="100" text-anchor="middle" font-size="13">meta-k5-distro (priority 9)</text>
  <text x="230" y="116" text-anchor="middle" font-size="12">DISTRO 정책: systemd · 보안 · 업데이트</text>
  <rect x="20" y="128" width="420" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="230" y="146" text-anchor="middle" font-size="13">meta-k5-sensorkit-bsp (priority 8)</text>
  <text x="230" y="162" text-anchor="middle" font-size="12">machine conf · DT 패치 · 커널/U-Boot bbappend</text>
  <rect x="20" y="174" width="420" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="230" y="192" text-anchor="middle" font-size="13">meta-freescale (vendor BSP, priority 5)</text>
  <text x="230" y="208" text-anchor="middle" font-size="12">linux-fslc-imx · u-boot-imx · imx-boot · 펌웨어</text>
  <rect x="20" y="220" width="420" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="230" y="238" text-anchor="middle" font-size="13">meta-yocto/meta-poky (참조 DISTRO)</text>
  <text x="230" y="254" text-anchor="middle" font-size="12">poky.conf — 회사 distro가 require</text>
  <rect x="20" y="266" width="420" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="230" y="284" text-anchor="middle" font-size="13">openembedded-core/meta (priority 5)</text>
  <text x="230" y="300" text-anchor="middle" font-size="12">클래스 · toolchain · 기본 레시피 수천 개</text>
  <rect x="20" y="312" width="420" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="230" y="332" text-anchor="middle" font-size="13">bitbake 2.18 (실행 엔진, layer 아님)</text>
  <rect x="470" y="36" width="190" height="132" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="58" text-anchor="middle" font-size="13" font-weight="bold">회사 저장소</text>
  <text x="565" y="80" text-anchor="middle" font-size="12">k5-platform.git</text>
  <text x="565" y="100" text-anchor="middle" font-size="12">우리가 고친다</text>
  <text x="565" y="120" text-anchor="middle" font-size="12">kas/*.yml + lock</text>
  <text x="565" y="140" text-anchor="middle" font-size="12">코드 리뷰 대상</text>
  <rect x="470" y="174" width="190" height="168" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="565" y="196" text-anchor="middle" font-size="13" font-weight="bold">외부 저장소</text>
  <text x="565" y="218" text-anchor="middle" font-size="12">커밋 해시로 고정</text>
  <text x="565" y="238" text-anchor="middle" font-size="12">직접 수정 금지</text>
  <text x="565" y="258" text-anchor="middle" font-size="12">바꿀 것은 bbappend로</text>
  <text x="565" y="278" text-anchor="middle" font-size="12">버그는 upstream에</text>
  <line x1="450" y1="102" x2="465" y2="102" stroke="currentColor"/>
  <line x1="450" y1="258" x2="465" y2="258" stroke="currentColor"/>
  <rect x="20" y="360" width="640" height="56" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="382" text-anchor="middle" font-size="13">kas lock 파일 = 모든 외부 저장소의 정확한 커밋 목록</text>
  <text x="340" y="402" text-anchor="middle" font-size="12">"6개월 전 v1.3 펌웨어를 똑같이 다시 빌드"가 가능해지는 이유</text>
  <line x1="340" y1="344" x2="340" y2="358" stroke="currentColor" marker-end="url(#ah1)"/>
</svg>
```
그림 1 — 회사 Yocto 저장소의 layer 스택. 아래 네 층(초록 아닌 것)은 남의 코드라 커밋을 고정해서 쓰고, 바꿀 것은 위쪽 회사 layer에서 bbappend로 덮는다. 오른쪽 아래 lock 파일이 재현성의 출발점이다.

Don의 SSD 펌웨어 경험으로 옮기면 이렇다.

| SSD 펌웨어 양산 | Yocto 회사 저장소 |
|---|---|
| 컨트롤러 벤더 SDK (수정 금지, 버전 고정) | vendor BSP layer (meta-freescale, meta-ti, meta-tegra) |
| 우리 보드용 HAL·핀맵·NAND 파라미터 | 회사 BSP layer (machine conf, DT, 커널 패치) |
| 펌웨어 공통 정책 (로그, 보안, 업데이트 방식) | distro layer |
| 고객사별 SKU 빌드 설정 | product layer (image 레시피) |
| 릴리스 매니페스트 (각 컴포넌트 git 해시) | kas lock 파일 / repo manifest |
| 빌드 서버의 오브젝트 캐시 | sstate cache + hash equivalence 서버 |

---

## 1. 여러 저장소를 묶는 법 — kas, repo, SRCREV 고정

### 1.1 문제: "내 PC에선 되는데"

Yocto 빌드에는 최소 4~6개 git 저장소가 필요하다. 각자 `git clone`하고 `bitbake-layers add-layer`를 손으로 하면, 사람마다 다른 커밋·다른 layer 순서·다른 `local.conf`가 된다. 이를 막는 도구가 셋 있다.

| 도구 | 누가 쓰나 | 정의 파일 | 커밋 고정 |
|---|---|---|---|
| **kas** (Siemens, 현재 5.x) | 많은 회사의 새 프로젝트, CI | YAML (`repos`, `machine`, `distro`, `local_conf_header`) | `commit:` 또는 `kas lock`이 만든 `.lock.yml` |
| **repo** (Google, Android에서 옴) | NXP i.MX 릴리스, 오래된 BSP | `default.xml` 매니페스트 | `revision="<sha>"` |
| **bitbake-setup** (Yocto 6.0부터 공식) | Yocto 문서·레퍼런스 | JSON 레지스트리 | `"rev"` 필드 |

kas는 "저장소 받기 + `bblayers.conf`·`local.conf` 생성 + bitbake 실행(또는 shell)"을 한 파일로 끝낸다. repo는 저장소만 받는다(설정은 벤더가 주는 `setup-environment` 같은 스크립트가 만든다). 회사에서 무엇을 쓰든 원리는 같다: **저장소 목록 + 커밋 해시를 git에 넣어 둔다.**

### 1.2 kas 프로젝트 파일 (실제로 만들고 검증함)

`/var/tmp/K5/k5-platform/`에 회사 저장소를 흉내 낸 git 저장소를 만들었다. kas 파일은 다음과 같다.

```text
# kas/k5-sensorkit.yml
header:
  version: 14

machine: k5-sensorkit
distro: k5os
target: k5-sensorkit-image

repos:
  # this repo itself (company layers live next to the kas file)
  k5-platform:
    layers:
      meta-k5-sensorkit-bsp:
      meta-k5-distro:
      meta-k5-product:

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
  meta-freescale:
    url: https://github.com/Freescale/meta-freescale
    branch: wrynose

local_conf_header:
  k5-ci: |
    ACCEPT_FSL_EULA = "1"
    BB_NUMBER_THREADS = "16"
    PARALLEL_MAKE = "-j 16"
    INHERIT += "rm_work"
    BB_GENERATE_MIRROR_TARBALLS = "1"
```

읽는 법: `url`이 없는 `k5-platform`은 "이 kas 파일이 들어 있는 저장소 자신"이다. bitbake 저장소는 받기만 하고 layer로는 쓰지 않는다(`.: disabled`). `layers:`를 생략한 meta-freescale은 저장소 루트가 곧 layer다. `ACCEPT_FSL_EULA`는 NXP 바이너리 펌웨어 EULA 동의로, meta-freescale을 쓰면 반드시 마주친다.

kas는 VM 안 전용 venv에 `pip install kas`로 설치했다 (`kas 5.5`, configuration format version 23). `kas checkout`은 저장소를 받고 빌드 디렉터리 설정 파일만 만든다 — **bitbake는 실행하지 않는다.**

```sh
cd /var/tmp/K5/k5-platform
export KAS_WORK_DIR=/var/tmp/K5/k5_kaswork KAS_CLONE_DEPTH=1
/var/tmp/K5/k5_venv/bin/kas checkout kas/k5-sensorkit.yml 2>&1 | sed 's/^[0-9-]* [0-9:]* //' | tail -n 12
cat /var/tmp/K5/k5_kaswork/build/conf/bblayers.conf
```

```text
- INFO     - Cloning repository bitbake
- INFO     - Cloning repository openembedded-core
- INFO     - Cloning repository meta-yocto
- INFO     - Cloning repository meta-freescale
- INFO     - Repository meta-freescale already contains wrynose as commit
- INFO     - Repository meta-yocto already contains wrynose as commit
- INFO     - Repository bitbake already contains 2.18 as commit
- INFO     - Repository openembedded-core already contains wrynose as commit
- INFO     - Repository bitbake checked out to 6e260eb6ab1631607ea414d9ff8ede1155faa5d9
- INFO     - Repository openembedded-core checked out to 636e5d3b12d54951c13d57346648bc29a0e6b24e
- INFO     - Repository meta-yocto checked out to 7c25b11156553145fa6b241a2d4f65d94438f804
- INFO     - Repository meta-freescale checked out to b23da7c10998926cdacaaa6426b4697de6c2b2a7
BBLAYERS ?= " \
    ${TOPDIR}/../../k5-platform/meta-k5-distro \
    ${TOPDIR}/../../k5-platform/meta-k5-product \
    ${TOPDIR}/../../k5-platform/meta-k5-sensorkit-bsp \
    ${TOPDIR}/../meta-freescale \
    ${TOPDIR}/../meta-yocto/meta-poky \
    ${TOPDIR}/../openembedded-core/meta"
BBPATH ?= "${TOPDIR}"
BBFILES ??= ""
```

출력에서 볼 것: 브랜치 이름(`wrynose`)이 **그 순간의 커밋**으로 풀렸다. openembedded-core의 wrynose 브랜치 끝(`636e5d3b`)은 이미 6.0.3 태그(`ef022bf8`)보다 앞서 있다 — 브랜치만 적어 두면 다음 주에는 다른 코드를 받는다는 뜻이다. 생성된 `local.conf`에는 `local_conf_header` 블록과 `MACHINE ??= "k5-sensorkit"`, `DISTRO ??= "k5os"`가 들어갔다.

### 1.3 `kas lock` — 브랜치를 커밋으로 박제

```sh
cd /var/tmp/K5/k5-platform
KAS_WORK_DIR=/var/tmp/K5/k5_kaswork /var/tmp/K5/k5_venv/bin/kas lock kas/k5-sensorkit.yml 2>&1 \
  | sed 's/^[0-9-]* [0-9:]* //' | tail -3
cat kas/k5-sensorkit.lock.yml
```

```text
- INFO     - Adding lock of meta-yocto: 7c25b11156553145fa6b241a2d4f65d94438f804
- INFO     - Adding lock of meta-freescale: b23da7c10998926cdacaaa6426b4697de6c2b2a7
- INFO     - Updating lockfile kas/k5-sensorkit.lock.yml
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
    meta-freescale:
      commit: b23da7c10998926cdacaaa6426b4697de6c2b2a7
```

출력에서 볼 것: lock 파일은 원래 YAML 옆에 놓이고, kas는 다음 실행부터 이 파일을 자동으로 겹쳐 읽는다. 이 파일을 커밋하면 "릴리스 v1.4 = 이 해시 다섯 개"가 된다. **벤더 layer를 올리는 일 = lock 파일을 갱신하는 PR**이 된다 (리뷰어는 diff에서 바뀐 해시를 본다).

CI는 같은 프로젝트에 캐시 설정만 얹는다. kas는 YAML을 `:`로 이어 붙이거나 `includes`로 겹친다.

```text
# kas/ci-mirrors.yml  (가이드 값 — 사내 서버 주소는 예시)
header:
  version: 14
  includes:
    - kas/k5-sensorkit.yml

local_conf_header:
  k5-cache: |
    DL_DIR = "/srv/yocto/downloads"
    SSTATE_DIR = "/srv/yocto/sstate"
    SSTATE_MIRRORS = "file://.* https://sstate.k5.example/wrynose/PATH;downloadfilename=PATH"
    BB_HASHSERVE = "hashserv.k5.example:8686"
    PRSERV_HOST = "prserv.k5.example:8585"
```

> **실행하지 않음 — 가이드**: 실제 빌드 명령. 이 VM에서는 BitBake가 돌지 않는다.

```sh
# 실행하지 않음 — 가이드
kas build kas/k5-sensorkit.yml                    # 개발자 PC: checkout + bitbake k5-sensorkit-image
kas build kas/ci-mirrors.yml                      # CI: 같은 것 + 공유 캐시
kas shell kas/k5-sensorkit.yml                    # 빌드 환경이 잡힌 셸 (여기서 bitbake/devtool 직접)
kas shell kas/k5-sensorkit.yml -c 'bitbake -c menuconfig virtual/kernel'
kas-container build kas/k5-sensorkit.yml          # 고정된 컨테이너 이미지 안에서 (호스트 배포판 차이 제거)
```

### 1.4 repo 매니페스트와 레시피 안의 SRCREV

NXP 같은 벤더는 BSP를 `repo` 매니페스트로 배포한다. 원리는 같다.

```text
<!-- 가이드: 회사 manifest 예 (default.xml) -->
<manifest>
  <remote name="oe"  fetch="https://git.openembedded.org"/>
  <remote name="fsl" fetch="https://github.com/Freescale"/>
  <project remote="oe"  name="bitbake"            path="sources/bitbake"   revision="6e260eb6ab1631607ea414d9ff8ede1155faa5d9"/>
  <project remote="oe"  name="openembedded-core"  path="sources/oe-core"   revision="636e5d3b12d54951c13d57346648bc29a0e6b24e"/>
  <project remote="fsl" name="meta-freescale"     path="sources/meta-freescale" revision="b23da7c10998926cdacaaa6426b4697de6c2b2a7"/>
</manifest>
```

```sh
# 실행하지 않음 — 가이드
repo init -u https://git.k5.example/k5-manifest.git -b main -m default.xml
repo sync -j8
repo manifest -r -o release-1.4.xml    # 지금 받은 커밋으로 고정된 매니페스트를 뽑는다
```

layer 커밋을 고정해도 끝이 아니다. **레시피 안에서도 소스 커밋이 고정돼야 한다.** vendor 커널 레시피를 보면 그렇게 돼 있다.

```sh
cd /var/tmp/K5/k5_meta-freescale
grep -E '^(KBRANCH|SRC_URI|SRCREV|LINUX_VERSION|KBUILD_DEFCONFIG:mx8)' recipes-kernel/linux/linux-fslc-imx_6.18.bb
```

```text
KBRANCH = "6.18-1.0.x-imx"
SRC_URI = "git://github.com/Freescale/linux-fslc.git;branch=${KBRANCH};protocol=https"
SRCREV = "1d08c2fc5b376a84a3e7092651289fb8e8ffc1af"
LINUX_VERSION = "6.18.24"
KBUILD_DEFCONFIG:mx8-generic-bsp = "imx_v8_defconfig"
```

출력에서 볼 것: `branch=`는 "이 커밋이 어느 브랜치에 있는지 확인용"이고, 실제로 받는 것은 `SRCREV` 해시다. 회사 레시피에서 `SRCREV = "${AUTOREV}"`(항상 최신)를 쓰면 빌드가 매번 네트워크에 묻고, 재현성과 sstate 재사용이 깨진다. 개발 중 잠깐만 쓰고 릴리스 브랜치에는 금지하는 것이 보통이다.

---

## 2. BSP layer 해부 — 가상의 보드 `k5-sensorkit` 만들기

### 2.1 vendor machine conf 읽기

새 보드는 거의 항상 **벤더 평가 보드(EVK)에서 출발**한다. 센서 키트 캐리어 보드가 i.MX8M Plus SoM을 쓴다면, meta-freescale의 `imx8mp-lpddr4-evk.conf`가 출발점이다.

```sh
cd /var/tmp/K5/k5_meta-freescale
grep -vE '^\s*freescale/|^\s*$|^#' conf/machine/imx8mp-lpddr4-evk.conf | grep -vE '^\s+lpddr4' | head -20
```

```text
require include/imx8mp-evk.inc
KERNEL_DEVICETREE_BASENAME = "imx8mp-evk"
KERNEL_DEVICETREE:append:use-nxp-bsp = " \
"
UBOOT_CONFIG_BASENAME = "imx8mp_evk"
UBOOT_CONFIG[fspi] = "${UBOOT_CONFIG_BASENAME}_defconfig"
UBOOT_CONFIG[ndm]  = "${UBOOT_CONFIG_BASENAME}_ndm_defconfig"
DDR_FIRMWARE_VERSION = "202006"
DDR_FIRMWARE_NAME = " \
"
IMXBOOT_TARGETS_BASENAME = "flash_evk"
IMX_DEFAULT_BSP = "nxp"
```

진짜 정보는 `require`로 끌어오는 include에 있다. 커널·부트로더를 누가 제공하는지(`PREFERRED_PROVIDER`)는 `imx-base.inc`가 정한다.

```sh
grep -nE 'IMX_DEFAULT_BSP \?=|MACHINEOVERRIDES =\. "use-|PREFERRED_PROVIDER_virtual/(kernel|bootloader)|IMX_DEFAULT_KERNEL:' \
  conf/machine/include/imx-base.inc
grep -nE '^KERNEL_DEVICETREE =|^UBOOT_DTB_NAME|^IMX_DEFAULT_BOOTLOADER|^UBOOT_CONFIG|^SERIAL_CONSOLES|^ATF_PLATFORM' \
  conf/machine/include/imx8mp-evk.inc
```

```text
13:IMX_DEFAULT_BSP ?= "mainline"
19:MACHINEOVERRIDES =. "use-${IMX_DEFAULT_BSP}-bsp:"
78:PREFERRED_PROVIDER_virtual/bootloader ??= "${IMX_DEFAULT_BOOTLOADER}"
79:PREFERRED_PROVIDER_u-boot ??= "${PREFERRED_PROVIDER_virtual/bootloader}"
80:PREFERRED_PROVIDER_u-boot-mfgtool ??= "${PREFERRED_PROVIDER_virtual/bootloader}"
675:IMX_DEFAULT_KERNEL:imx-mainline-bsp ??= "linux-fslc"
676:IMX_DEFAULT_KERNEL:imx-nxp-bsp      ??= "linux-fslc-imx"
678:PREFERRED_PROVIDER_virtual/kernel ??= "${IMX_DEFAULT_KERNEL}"
20:KERNEL_DEVICETREE = " \
24:UBOOT_DTB_NAME = "${KERNEL_DEVICETREE_BASENAME}.dtb"
26:IMX_DEFAULT_BOOTLOADER:use-nxp-bsp = "u-boot-imx"
27:IMX_DEFAULT_BOOTLOADER:use-mainline-bsp = "u-boot-fslc"
31:UBOOT_CONFIG ??= "sd"
32:UBOOT_CONFIG[sd]      = "${UBOOT_CONFIG_BASENAME}_defconfig"
33:UBOOT_CONFIG[ecc]     = "${UBOOT_CONFIG_BASENAME}_inline_ecc_defconfig"
37:ATF_PLATFORM = "imx8mp"
45:SERIAL_CONSOLES = "115200;ttymxc1"
```

출력에서 볼 것 세 가지.

1. **`virtual/kernel`은 이름표**다. 실제 레시피는 `PREFERRED_PROVIDER_virtual/kernel`이 고른다. 여기서 `IMX_DEFAULT_BSP = "nxp"`이면 `linux-fslc-imx`(NXP 커널 + 커뮤니티 stable 패치), `mainline`이면 `linux-fslc`다. 커널 bbappend를 만들기 전에 **어느 레시피가 실제로 선택되는지 먼저 확인**해야 한다 (6.4절의 `bitbake-getvar`).
2. `??=`(가장 약한 기본값)로 써 있으므로 회사 machine conf가 `=`로 덮을 수 있다.
3. i.MX는 `UBOOT_MACHINE`을 직접 쓰지 않고 **`UBOOT_CONFIG` 플래그**를 쓴다. `uboot-config.bbclass`가 `UBOOT_CONFIG = "sd"` → `UBOOT_CONFIG[sd]`의 defconfig 이름 → 내부 `UBOOT_MACHINE`으로 바꾼다. 다른 BSP(예: TI, Raspberry Pi)는 `UBOOT_MACHINE = "xxx_defconfig"` 한 줄로 쓴다.

같은 정보가 다른 벤더에도 있다: meta-ti는 `conf/machine/am62xx-evm.conf` 계열, meta-tegra는 `conf/machine/jetson-orin-*.conf`에서 `KERNEL_DEVICETREE`, `PREFERRED_PROVIDER_virtual/kernel`(`linux-jammy-nvidia-tegra` 등 브랜치마다 다름)을 정한다. 이름은 달라도 읽는 순서는 같다: **machine conf → require된 inc → PREFERRED_PROVIDER → 그 레시피.**

### 2.2 회사 BSP layer의 파일 구성

이 노트를 위해 만든 layer다 (`/var/tmp/K5/k5-platform/`, 사본 `~/.elinux_scratch/K5/k5-platform/`).

```text
meta-k5-sensorkit-bsp/
  conf/layer.conf
  conf/machine/k5-sensorkit.conf
  recipes-kernel/linux/linux-fslc-imx_%.bbappend
  recipes-kernel/linux/files/0001-arm64-dts-imx8mp-add-K5-SensorKit-board-with-ICM-426.patch
  recipes-kernel/linux/files/k5-sensorkit.cfg
  recipes-kernel/k5-imu-mod/k5-imu-mod_0.1.bb
  recipes-kernel/k5-imu-mod/files/{Makefile,k5_imu_mod.c}
  recipes-bsp/u-boot/u-boot-imx_%.bbappend
  recipes-bsp/u-boot/files/k5-uboot.cfg
meta-k5-distro/conf/distro/k5os.conf
meta-k5-product/recipes-images/images/k5-sensorkit-image.bb
```

`layer.conf`의 핵심은 우선순위와 호환 선언이다.

```text
# meta-k5-sensorkit-bsp/conf/layer.conf
BBPATH .= ":${LAYERDIR}"
BBFILES += "${LAYERDIR}/recipes-*/*/*.bb \
            ${LAYERDIR}/recipes-*/*/*.bbappend"

BBFILE_COLLECTIONS += "k5-sensorkit-bsp"
BBFILE_PATTERN_k5-sensorkit-bsp = "^${LAYERDIR}/"
# above meta-freescale (5) so our bbappends/overrides win
BBFILE_PRIORITY_k5-sensorkit-bsp = "8"

LAYERDEPENDS_k5-sensorkit-bsp = "core freescale-layer"
LAYERSERIES_COMPAT_k5-sensorkit-bsp = "wrynose"
```

`LAYERDEPENDS`의 이름은 상대 layer의 `BBFILE_COLLECTIONS` 값이다. meta-freescale은 `freescale-layer`이고, 그쪽도 `LAYERSERIES_COMPAT_freescale-layer = "whinlatter wrynose"`로 선언돼 있다 (clone한 `conf/layer.conf`에서 확인).

### 2.3 machine conf — EVK에서 파생

```text
# meta-k5-sensorkit-bsp/conf/machine/k5-sensorkit.conf
#@TYPE: Machine
#@NAME: K5 SensorKit (i.MX 8M Plus, LPDDR4) -- fictional carrier
#@SOC: i.MX8MP
#@DESCRIPTION: Derived from meta-freescale imx8mp-lpddr4-evk.conf (wrynose)

require conf/machine/include/imx8mp-evk.inc

# NXP BSP flavour -> linux-fslc-imx + u-boot-imx (see imx-base.inc)
IMX_DEFAULT_BSP = "nxp"
PREFERRED_PROVIDER_virtual/kernel = "linux-fslc-imx"
PREFERRED_PROVIDER_virtual/bootloader = "u-boot-imx"

# Kernel: our board DTB comes from the DT patch in our linux bbappend
KERNEL_DEVICETREE_BASENAME = "imx8mp-k5-sensorkit"
KERNEL_DEVICETREE = "freescale/imx8mp-k5-sensorkit.dtb"

# U-Boot: no board port yet, reuse the EVK defconfig + EVK DTB
UBOOT_CONFIG_BASENAME = "imx8mp_evk"
UBOOT_CONFIG = "sd"
UBOOT_DTB_NAME = "imx8mp-evk.dtb"

# Same LPDDR4 training firmware as the EVK (same DDR part on our SoM)
DDR_FIRMWARE_VERSION = "202006"
DDR_FIRMWARE_NAME = " \
    lpddr4_pmu_train_1d_dmem_${DDR_FIRMWARE_VERSION}.bin \
    lpddr4_pmu_train_1d_imem_${DDR_FIRMWARE_VERSION}.bin \
    lpddr4_pmu_train_2d_dmem_${DDR_FIRMWARE_VERSION}.bin \
    lpddr4_pmu_train_2d_imem_${DDR_FIRMWARE_VERSION}.bin \
"
IMXBOOT_TARGETS_BASENAME = "flash_evk"

SERIAL_CONSOLES = "115200;ttymxc1"
MACHINE_FEATURES:append = " can"
MACHINE_EXTRA_RRECOMMENDS += "kernel-module-k5-imu-mod"
```

설계 판단을 적어 둔다.

- **파일 이름이 곧 MACHINE 이름**이고, MACHINE 이름은 자동으로 override가 된다. 그래서 bbappend에서 `SRC_URI:append:k5-sensorkit`라고 쓰면 이 보드에서만 적용된다. EVK 빌드에는 영향이 없다.
- `imx8mp-evk.inc`가 `UBOOT_DTB_NAME = "${KERNEL_DEVICETREE_BASENAME}.dtb"`로 정해 두었기 때문에, 커널 DTB 이름을 바꾸면 U-Boot도 그 이름을 찾는다. U-Boot 보드 포팅을 아직 안 했으므로 `require` **뒤에서** `imx8mp-evk.dtb`로 되돌렸다. 이런 "include가 만든 연쇄 효과"를 놓치는 것이 BSP 작업의 흔한 실수다.
- DDR 훈련 펌웨어는 같은 DDR 부품일 때만 그대로 쓸 수 있다. DDR이 다르면 NXP의 DDR tool로 타이밍을 다시 뽑아 U-Boot SPL의 `lpddr4_timing.c`를 바꿔야 한다 — Don이 SSD에서 DRAM 트레이닝 파라미터를 다룬 것과 같은 일이다.

---

## 3. 커널 레시피 실무 — linux-yocto vs vendor 커널, defconfig vs fragment

### 3.1 두 종류의 커널 레시피

| | linux-yocto (oe-core) | vendor 커널 (linux-fslc-imx, linux-ti-staging, linux-tegra 계열) |
|---|---|---|
| 소스 | `git.yoctoproject.org/linux-yocto` — LTS + Yocto 패치 브랜치 | 벤더 GitHub 트리 (수천 개 벤더 패치) |
| 기본 설정 | **kernel-cache의 `.scc`/`.cfg` 조합** (`KMACHINE`, `KTYPE`) | 트리 안 `KBUILD_DEFCONFIG` (예: `imx_v8_defconfig`) |
| 우리 변경 | `.cfg` fragment, `KERNEL_FEATURES`, 패치 | 같은 방법 — 둘 다 `inherit kernel-yocto`라서 |
| 언제 쓰나 | QEMU, genericarm64, upstream이 잘 지원하는 SoC | 신형 SoC, GPU/VPU/ISP 같은 벤더 IP가 필요할 때 (실무 대부분) |

중요한 사실 하나: vendor 커널도 대부분 `kernel-yocto` 클래스를 상속한다. meta-freescale의 `linux-imx.inc`가 그렇다.

```sh
cd /var/tmp/K5/k5_meta-freescale
grep -nE '^inherit|^KCONFIG_MODE' recipes-kernel/linux/linux-imx.inc
grep -n -A3 'DELTA_KERNEL_DEFCONFIG.*present' recipes-kernel/linux/linux-imx_6.18.bb
```

```text
7:inherit kernel-yocto kernel fsl-kernel-localversion fsl-vivante-kernel-driver-handler
34:KCONFIG_MODE = "--alldefconfig"
45:    # Fail fast if DELTA_KERNEL_DEFCONFIG is present in the datastore (even if empty)
46-    if "DELTA_KERNEL_DEFCONFIG" in d.keys():
47-        val = d.getVar("DELTA_KERNEL_DEFCONFIG", expand=False)
48-        bb.error(f"Detected deprecated/unsupported variable 'DELTA_KERNEL_DEFCONFIG' (value: '{val}').")
```

출력에서 볼 것: 그래서 linux-yocto의 fragment 방식(`.cfg` → `merge_config.sh`)이 vendor 커널에도 그대로 통한다. NXP는 자기만의 옛 방식(`DELTA_KERNEL_DEFCONFIG`)을 아예 에러로 막고 "kernel-yocto의 configuration fragment를 써라"라고 안내한다.

linux-yocto 쪽은 이렇게 생겼다.

```sh
cd /var/tmp/K5/k5_oe-core/meta
grep -nE '^KBRANCH \?=|^SRCREV_meta|^SRC_URI|^LINUX_VERSION|^KMETA|^KERNEL_EXTRA_FEATURES|qemuall' \
  recipes-kernel/linux/linux-yocto_6.18.bb
```

```text
1:KBRANCH ?= "v6.18/standard/base"
31:SRCREV_meta ?= "2f71b0a288c307062fc60948ac793d8d51c685e2"
42:SRC_URI = "git://git.yoctoproject.org/linux-yocto.git;name=machine;branch=${KBRANCH};protocol=https \
46:LINUX_VERSION ?= "6.18.39"
50:KMETA = "kernel-meta"
58:KERNEL_EXTRA_FEATURES ?= "features/netfilter/netfilter.scc"
60:KERNEL_FEATURES:append:qemuall = " cfg/virtio.scc features/drm-bochs/drm-bochs.scc cfg/net/mdio.scc"
```

출력에서 볼 것: `SRC_URI`에 저장소가 **두 개**다 — 커널 소스(`name=machine`)와 `yocto-kernel-cache`(`type=kmeta`, 설정 메타데이터). 각각 `SRCREV_machine`, `SRCREV_meta`로 고정된다.

### 3.2 `.scc`와 `.cfg` — 설정을 기능 단위로 쪼개기

`KERNEL_FEATURES`에 적는 `features/netfilter/netfilter.scc`가 실제로 무엇인지 kernel-cache에서 본다.

```sh
cd /var/tmp/K5/k5_kernel-cache
cat features/netfilter/netfilter.scc
sed -n 5,13p features/netfilter/netfilter.cfg
echo ----; cat bsp/nxp-imx9/nxp-imx9-standard.scc
```

```text
# SPDX-License-Identifier: MIT
#
# Not directly sourced via a kernel type but via an external bb
#

define KFEATURE_DESCRIPTION "Enable netfilter + conn tracking"
define KFEATURE_COMPATIBILITY all

kconf non-hardware netfilter.cfg
CONFIG_NETFILTER=y
CONFIG_NETFILTER_ADVANCED=y

#
# Core Netfilter Configuration
#
CONFIG_NETFILTER_NETLINK_QUEUE=m
CONFIG_NETFILTER_NETLINK_LOG=m
CONFIG_NF_CONNTRACK=m
----
define KMACHINE nxp-imx9
define KTYPE standard
define KARCH arm64

include ktypes/standard

include nxp-imx9.scc
```

정리하면:

- **`.cfg`** = Kconfig 조각. 그냥 `CONFIG_X=y` 몇 줄이다.
- **`.scc`** (Series Configuration Control) = "이 기능에는 이 `.cfg`와 이 패치들이 필요하다"를 묶은 목록. `kconf hardware|non-hardware <cfg>`, `patch <file>`, `include <다른 scc>`.
- `hardware`/`non-hardware` 분류는 나중에 `kernel_configcheck`가 "부팅에 중요한 설정이 빠졌나"를 판정할 때 쓴다.
- kernel-cache에는 `bsp/nxp-imx9`, `bsp/nvidia-orin`처럼 BSP별 `.scc`도 있다 — linux-yocto로 그 보드를 돌릴 때 쓰는 것이다.

회사 layer에서는 보통 `.scc`까지 만들지 않고 **`.cfg`를 `SRC_URI`에 바로 넣는다.** `kernel-yocto`가 `SRC_URI`의 `.cfg` 파일을 찾아 자동으로 설정 큐에 올린다. 기능이 많아지면 `.scc`로 묶어 `KERNEL_FEATURES`에 넣는다.

### 3.3 커널 레시피의 태스크 파이프라인

`kernel-yocto.bbclass`의 `addtask` 줄이 태스크 순서를 정한다.

```sh
cd /var/tmp/K5/k5_oe-core/meta
grep -n '^addtask' classes-recipe/kernel-yocto.bbclass
grep -n '^addtask \(symlink_kernsrc\|kernel_link_images\|deploy\)' classes-recipe/kernel.bbclass
```

```text
456:addtask kernel_checkout before do_kernel_metadata after do_symlink_kernsrc
457:addtask kernel_metadata after do_validate_branches do_unpack before do_patch
516:addtask kernel_configme before do_configure after do_patch
517:addtask config_analysis
781:addtask kernel_version_sanity_check after do_kernel_metadata do_kernel_checkout before do_compile
782:addtask validate_branches before do_patch after do_kernel_checkout
783:addtask kernel_configcheck after do_configure before do_compile
214:addtask symlink_kernsrc before do_patch do_configure after do_unpack
741:addtask kernel_link_images after do_compile before do_strip
844:addtask deploy after do_install do_populate_sysroot do_packagedata
```

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">kernel-yocto 레시피의 태스크 흐름과 입력</text>
  <rect x="10" y="36" width="150" height="54" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="85" y="58" text-anchor="middle" font-size="12">SRC_URI 커널 git</text>
  <text x="85" y="76" text-anchor="middle" font-size="12">SRCREV 고정</text>
  <rect x="180" y="36" width="150" height="54" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="255" y="58" text-anchor="middle" font-size="12">0001-...patch</text>
  <text x="255" y="76" text-anchor="middle" font-size="12">DT · 드라이버 수정</text>
  <rect x="350" y="36" width="150" height="54" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="425" y="58" text-anchor="middle" font-size="12">KBUILD_DEFCONFIG</text>
  <text x="425" y="76" text-anchor="middle" font-size="12">또는 KMACHINE scc</text>
  <rect x="520" y="36" width="150" height="54" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="595" y="58" text-anchor="middle" font-size="12">*.cfg fragment</text>
  <text x="595" y="76" text-anchor="middle" font-size="12">KERNEL_FEATURES</text>
  <rect x="10" y="130" width="150" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="155" text-anchor="middle" font-size="12">fetch · unpack</text>
  <rect x="180" y="130" width="150" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="255" y="148" text-anchor="middle" font-size="12">kernel_metadata</text>
  <text x="255" y="164" text-anchor="middle" font-size="12">→ patch</text>
  <rect x="350" y="130" width="150" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="425" y="148" text-anchor="middle" font-size="12">kernel_configme</text>
  <text x="425" y="164" text-anchor="middle" font-size="12">merge_config.sh</text>
  <rect x="520" y="130" width="150" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="595" y="155" text-anchor="middle" font-size="12">configure (.config)</text>
  <line x1="85" y1="90" x2="85" y2="128" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="255" y1="90" x2="255" y2="128" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="425" y1="90" x2="425" y2="128" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="595" y1="90" x2="440" y2="128" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="160" y1="150" x2="178" y2="150" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="330" y1="150" x2="348" y2="150" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="500" y1="150" x2="518" y2="150" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="520" y="210" width="150" height="54" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="595" y="232" text-anchor="middle" font-size="12">kernel_configcheck</text>
  <text x="595" y="250" text-anchor="middle" font-size="12">요청값 vs 최종값</text>
  <rect x="350" y="210" width="150" height="54" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="425" y="232" text-anchor="middle" font-size="12">compile</text>
  <text x="425" y="250" text-anchor="middle" font-size="12">Image · dtbs · .ko</text>
  <rect x="180" y="210" width="150" height="54" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="255" y="232" text-anchor="middle" font-size="12">install · package</text>
  <text x="255" y="250" text-anchor="middle" font-size="12">kernel-module-*</text>
  <rect x="10" y="210" width="150" height="54" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="232" text-anchor="middle" font-size="12">deploy</text>
  <text x="85" y="250" text-anchor="middle" font-size="12">deploy/images/</text>
  <line x1="595" y1="170" x2="595" y2="208" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="520" y1="237" x2="502" y2="237" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="350" y1="237" x2="332" y2="237" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="180" y1="237" x2="162" y2="237" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="340" y="300" text-anchor="middle" font-size="12">개발용 수동 태스크: menuconfig · diffconfig · savedefconfig (cml1.bbclass)</text>
  <text x="340" y="320" text-anchor="middle" font-size="12">mismatch.txt · invalid.txt · redefinition.txt 가 kernel-meta/cfg/ 에 남는다</text>
  <text x="340" y="340" text-anchor="middle" font-size="12">KMETA_AUDIT_WERROR = "1" 이면 경고가 빌드 실패로 바뀐다</text>
</svg>
```
그림 2 — 커널 레시피 파이프라인. 위 네 입력 중 회사 layer가 손대는 것은 주황(패치)과 오른쪽 초록(fragment)이다. 패치는 `do_patch`에, fragment는 `do_kernel_configme`의 `merge_config.sh`에 들어가고, `do_kernel_configcheck`가 "요청한 값이 정말 살아남았나"를 검사한다.

`do_kernel_configme`의 핵심 줄은 이것이다 (`kernel-yocto.bbclass`에서 그대로).

```sh
sed -n 494,498p /var/tmp/K5/k5_oe-core/meta/classes-recipe/kernel-yocto.bbclass
```

```text
	configs="$(scc --configs -o ${meta_dir})"
	if [ $? -ne 0 ]; then
		bberror "${configs}"
		bbfatal_log "Could not find configuration queue (${meta_dir}/config.queue)"
	fi
```

그리고 바로 아래에서 `merge_config.sh -O ${B} ${config_flags} ${configs}`를 부른다. 즉 **Yocto의 커널 설정 = 커널 자체의 `scripts/kconfig/merge_config.sh` + Kconfig 의존성 해석**이다. 그래서 BitBake 없이도 같은 일을 재현할 수 있다 (3.5절).

### 3.4 DT 패치 만들기 — `git format-patch`

센서 키트 캐리어 보드에 IMU(InvenSense ICM-42688)를 I2C3 0x68에 붙인다고 하자. 벤더의 EVK DTS를 직접 고치지 않고, **EVK DTS를 `#include`하는 새 보드 DTS**를 추가한다 (EVK 파일을 고치면 벤더 커널을 올릴 때마다 충돌한다).

랩 커널 소스(`/var/lab/src/linux-6.18.55`, 읽기 전용)에서 필요한 두 파일만 작은 git 저장소로 복사하고 패치를 만든다.

```sh
#!/bin/bash
# K5: make a DT patch with git format-patch against a 2-file copy of the lab kernel
set -euo pipefail
S=/var/lab/src/linux-6.18.55
R=/var/tmp/K5/k5_dts_repo
D=arch/arm64/boot/dts/freescale
rm -rf "$R"; mkdir -p "$R/$D"; cd "$R"
git init -q -b main .
git config user.name "K5 Writer"; git config user.email "k5@example.com"
cp "$S/$D/imx8mp-evk.dts" "$S/$D/Makefile" "$D/"
git add -A; git commit -q -m "import imx8mp-evk.dts + Makefile from linux 6.18.55"
cat > "$D/imx8mp-k5-sensorkit.dts" <<'DTS'
// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * K5 SensorKit carrier (fictional) on i.MX8M Plus.
 * Starts from the NXP EVK and adds the IMU on I2C3.
 */
/dts-v1/;

#include "imx8mp-evk.dts"

/ {
	model = "K5 SensorKit i.MX8MP";
	compatible = "k5,sensorkit-imx8mp", "fsl,imx8mp-evk", "fsl,imx8mp";
};

&i2c3 {
	k5_imu: imu@68 {
		compatible = "invensense,icm42688";
		reg = <0x68>;
		interrupt-parent = <&gpio1>;
		interrupts = <5 IRQ_TYPE_EDGE_FALLING>;
		interrupt-names = "INT1";
	};
};
DTS
sed -i 's|^dtb-$(CONFIG_ARCH_MXC) += imx8mp-evk.dtb$|&\ndtb-$(CONFIG_ARCH_MXC) += imx8mp-k5-sensorkit.dtb|' "$D/Makefile"
git add -A
git commit -q -m "arm64: dts: imx8mp: add K5 SensorKit board with ICM-42688 IMU" \
  -m "Carrier board derived from imx8mp-evk. Adds the IMU on I2C3 at 0x68, INT1 on GPIO1_IO05." \
  -m "Upstream-Status: Inappropriate [board-specific]"
git format-patch -1 -o /var/tmp/K5/k5_patches
```

만들어진 패치의 머리 부분이다 (뒷부분은 새 DTS 23줄의 `+` 줄들이다).

```text
From 665d65b5954cb117464905f323f1fa922fc39064 Mon Sep 17 00:00:00 2001
From: K5 Writer <k5@example.com>
Date: Sat, 17 Oct 2026 17:59:51 -0700
Subject: [PATCH] arm64: dts: imx8mp: add K5 SensorKit board with ICM-42688 IMU

Carrier board derived from imx8mp-evk. Adds the IMU on I2C3 at 0x68, INT1 on GPIO1_IO05.

Upstream-Status: Inappropriate [board-specific]
---
 arch/arm64/boot/dts/freescale/Makefile        |  1 +
 .../dts/freescale/imx8mp-k5-sensorkit.dts     | 23 +++++++++++++++++++
 2 files changed, 24 insertions(+)
 create mode 100644 arch/arm64/boot/dts/freescale/imx8mp-k5-sensorkit.dts

diff --git a/arch/arm64/boot/dts/freescale/Makefile b/arch/arm64/boot/dts/freescale/Makefile
index 525ef18..197c1bc 100644
--- a/arch/arm64/boot/dts/freescale/Makefile
+++ b/arch/arm64/boot/dts/freescale/Makefile
@@ -206,6 +206,7 @@ dtb-$(CONFIG_ARCH_MXC) += imx8mp-dhcom-pdk3.dtb
 dtb-$(CONFIG_ARCH_MXC) += imx8mp-dhcom-picoitx.dtb
 dtb-$(CONFIG_ARCH_MXC) += imx8mp-edm-g-wb.dtb
 dtb-$(CONFIG_ARCH_MXC) += imx8mp-evk.dtb
+dtb-$(CONFIG_ARCH_MXC) += imx8mp-k5-sensorkit.dtb
 dtb-$(CONFIG_ARCH_MXC) += imx8mp-hummingboard-mate.dtb
```

출력에서 볼 것: `Upstream-Status:` 줄. OpenEmbedded는 layer 안 모든 패치에 이 태그를 요구한다(`Pending`, `Submitted [where]`, `Backport [commit]`, `Inappropriate [reason]` 등). 이 태그가 없으면 QA 경고(`patch-status`)가 나고, 회사 리뷰에서도 "이 패치는 언젠가 사라지나, 영원히 들고 가나"를 바로 알 수 있다. Don이 SSD에서 벤더 SDK 위에 얹던 "로컬 패치 목록"을 관리하던 것과 같은 목적이다.

`do_patch`가 하는 일(패치 적용)과 DTB 빌드를 랩 소스 사본에서 확인한다.

```sh
#!/bin/bash
# K5: apply the patch to a copy of the lab DTS dir (like do_patch), then compile the new DTB
set -euo pipefail
S=/var/lab/src/linux-6.18.55
W=/var/tmp/K5/k5_dtcheck
D=arch/arm64/boot/dts/freescale
rm -rf "$W"; mkdir -p "$W/$D"; cp -r "$S/$D/." "$W/$D/"
cd "$W"
patch -p1 --dry-run < /var/tmp/K5/k5_patches/0001-*.patch
patch -p1 -s < /var/tmp/K5/k5_patches/0001-*.patch
cpp -nostdinc -undef -x assembler-with-cpp -D__DTS__ \
    -I "$S/include" -I "$S/scripts/dtc/include-prefixes" -I "$W/$D" \
    "$W/$D/imx8mp-k5-sensorkit.dts" -o k5.dts.pre
dtc -q -I dts -O dtb -o k5-sensorkit.dtb k5.dts.pre
ls -l k5-sensorkit.dtb
fdtget k5-sensorkit.dtb / model
fdtget k5-sensorkit.dtb / compatible
dtc -q -I dtb -O dts k5-sensorkit.dtb | grep -n -A6 "imu@68"
```

```text
checking file arch/arm64/boot/dts/freescale/Makefile
checking file arch/arm64/boot/dts/freescale/imx8mp-k5-sensorkit.dts
-rw-rw-r-- 1 donh donh 58278 Oct 17 17:59 k5-sensorkit.dtb
K5 SensorKit i.MX8MP
k5,sensorkit-imx8mp fsl,imx8mp-evk fsl,imx8mp
1609:				imu@68 {
1610-					compatible = "invensense,icm42688";
1611-					reg = <0x68>;
1612-					interrupt-parent = <0x3b>;
1613-					interrupts = <0x05 0x02>;
1614-					interrupt-names = "INT1";
1615-				};
```

출력에서 볼 것: 매크로 `IRQ_TYPE_EDGE_FALLING`이 cpp를 거쳐 `0x02`로 바뀌었고, `&gpio1` 참조가 phandle(`0x3b`)이 됐다. 커널 빌드의 `make dtbs`가 내부에서 하는 일이 정확히 이 `cpp → dtc` 2단계다 (D2 참고).

### 3.5 함정: 패치는 레시피의 SRCREV 기준으로 만들어야 한다

방금 패치는 **mainline 6.18.55** 기준이다. 그런데 레시피가 실제로 받는 것은 `linux-fslc` 커밋 `1d08c2fc`다. 그 커밋의 두 파일만 받아서 dry-run 해 본다.

```sh
#!/bin/bash
set -euo pipefail
REV=1d08c2fc5b376a84a3e7092651289fb8e8ffc1af   # SRCREV from linux-fslc-imx_6.18.bb
D=arch/arm64/boot/dts/freescale
W=/var/tmp/K5/k5_vendorcheck; rm -rf "$W"; mkdir -p "$W/$D"; cd "$W"
for f in Makefile imx8mp-evk.dts; do
  curl -fsSL -o "$D/$f" "https://raw.githubusercontent.com/Freescale/linux-fslc/$REV/$D/$f"
done
wc -l "$D/Makefile" "$D/imx8mp-evk.dts"
grep -n "imx8mp-evk" "$D/Makefile" | head -2
patch -p1 --dry-run < /var/tmp/K5/k5_patches/0001-*.patch
```

```text
  1271 arch/arm64/boot/dts/freescale/Makefile
  1529 arch/arm64/boot/dts/freescale/imx8mp-evk.dts
  2800 total
278:dtb-$(CONFIG_ARCH_MXC) += imx8mp-evk.dtb imx8mp-evk-rm67191.dtb imx8mp-evk-it6263-lvds-dual-channel.dtb \
279:			  imx8mp-evk-rpmsg.dtb imx8mp-evk-ecspi-slave.dtb \
checking file arch/arm64/boot/dts/freescale/Makefile
Hunk #1 FAILED at 206.
1 out of 1 hunk FAILED
checking file arch/arm64/boot/dts/freescale/imx8mp-k5-sensorkit.dts
```

출력에서 볼 것: `patch`가 실패해 `set -e`로 스크립트가 종료 코드 1로 멈췄다. 벤더 트리의 같은 파일은 mainline보다 훨씬 길고(Makefile 1271줄, EVK DTS 1529줄 vs mainline 1193줄), `imx8mp-evk.dtb` 줄의 모양이 달라 hunk가 실패했다. Yocto에서는 이것이 `do_patch` 실패로 나타난다. **교훈: 패치는 항상 레시피가 받는 바로 그 소스(`SRCREV`) 위에서 만든다.** 그걸 자동으로 해 주는 것이 `devtool modify`다(4절). 여기서는 같은 일을 손으로 했다 — 벤더 파일로 저장소를 만들고, 같은 변경을 다시 커밋하고, 다시 `format-patch`.

```sh
#!/bin/bash
set -euo pipefail
V=/var/tmp/K5/k5_vendorcheck; R=/var/tmp/K5/k5_dts_vendor_repo; D=arch/arm64/boot/dts/freescale
rm -rf "$R"; mkdir -p "$R/$D"; cd "$R"; git init -q -b main .
git config user.name "K5 Writer"; git config user.email k5@example.com
cp "$V/$D/Makefile" "$V/$D/imx8mp-evk.dts" "$D/"
git add -A; git commit -q -m "linux-fslc 1d08c2fc (6.18-1.0.x-imx): Makefile + imx8mp-evk.dts"
git -C /var/tmp/K5/k5_dts_repo show HEAD:$D/imx8mp-k5-sensorkit.dts > "$D/imx8mp-k5-sensorkit.dts"
sed -i '278i dtb-$(CONFIG_ARCH_MXC) += imx8mp-k5-sensorkit.dtb' "$D/Makefile"
git add -A
git -C /var/tmp/K5/k5_dts_repo log -1 --format=%B | git commit -q -F -
rm -rf /var/tmp/K5/k5_patches_v2
git format-patch -1 -o /var/tmp/K5/k5_patches_v2 | xargs -n1 basename
git show --stat --format= HEAD
cd "$V" && patch -p1 --dry-run < /var/tmp/K5/k5_patches_v2/0001-*.patch && echo "vendor tree: applies cleanly"
```

```text
0001-arm64-dts-imx8mp-add-K5-SensorKit-board-with-ICM-426.patch
 arch/arm64/boot/dts/freescale/Makefile             |  1 +
 .../boot/dts/freescale/imx8mp-k5-sensorkit.dts     | 23 ++++++++++++++++++++++
 2 files changed, 24 insertions(+)
checking file arch/arm64/boot/dts/freescale/Makefile
checking file arch/arm64/boot/dts/freescale/imx8mp-k5-sensorkit.dts
vendor tree: applies cleanly
```

이 v2 패치가 layer의 `recipes-kernel/linux/files/`에 들어간다. (vendor 트리의 나머지 dtsi는 받지 않았으므로 vendor 기준 DTB 컴파일은 하지 않았다 — 그건 실제 Yocto 빌드의 `do_compile`이 확인할 몫이다.)

### 3.6 커널 bbappend와 config fragment

```text
# meta-k5-sensorkit-bsp/recipes-kernel/linux/linux-fslc-imx_%.bbappend
# K5 SensorKit: board DT + config fragment on top of the vendor kernel
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:k5-sensorkit = " \
    file://0001-arm64-dts-imx8mp-add-K5-SensorKit-board-with-ICM-426.patch \
    file://k5-sensorkit.cfg \
"
# Fail the build (not just warn) if a fragment value does not survive Kconfig
KMETA_AUDIT_WERROR:k5-sensorkit = "1"
```

- `_%`는 버전 와일드카드다. 벤더가 `linux-fslc-imx_6.18.bb`를 `_6.19.bb`로 올려도 bbappend가 따라간다. 반대로 패치가 새 버전에 안 맞을 수 있으니, 버전을 올릴 때 패치를 다시 확인하는 것은 사람 몫이다.
- `FILESEXTRAPATHS:prepend`의 `:=`는 **즉시 확장**이다. `${THISDIR}`가 "이 bbappend가 있는 폴더"로 지금 고정돼야 하기 때문이다 (`=`로 쓰면 나중에 다른 파일 기준으로 풀린다 — K4의 변수 확장 시점 참고).
- 패치 파일(`.patch`, `.diff`)은 `do_patch`가, `.cfg`는 `kernel-yocto`의 설정 큐가 자동으로 처리한다.

이제 fragment가 진짜로 먹히는지가 문제다. 일부러 함정 두 개를 넣은 첫 버전으로 시험한다.

```text
# k5-sensorkit.cfg (첫 버전)
CONFIG_INV_ICM42600_I2C=m
CONFIG_CAN_FLEXCAN=y
CONFIG_PPS_CLIENT_GPIO=y
# typo on purpose: this symbol does not exist
CONFIG_K5_IMU_DEBUG=y
```

`do_kernel_configme`(병합 + Kconfig 해석)와 `do_kernel_configcheck`(요청값 vs 최종값)를 랩 커널의 `.config`(arm64 defconfig 기반)로 손으로 재현한다. 소스는 읽기 전용이므로 `O=`로 빌드 디렉터리를 따로 쓴다.

```sh
#!/bin/bash
# K5: what do_kernel_configme + do_kernel_configcheck do, by hand, on the lab kernel
set -euo pipefail
S=/var/lab/src/linux-6.18.55
O=/var/tmp/K5/k5_kbuild
FRAG=/Users/donh/.elinux_scratch/K5/k5-sensorkit.cfg
rm -rf "$O"; mkdir -p "$O"; cd "$O"
cp /var/lab/out/linux-build/.config base.config
# 1) merge only (-m): base + fragment -> $O/.config   (configme step 1)
"$S/scripts/kconfig/merge_config.sh" -m -O "$O" base.config "$FRAG" | tail -n 4
# 2) let Kconfig resolve dependencies              (configme step 2)
make -s -C "$S" O="$O" ARCH=arm64 olddefconfig
# 3) audit: requested vs final                       (configcheck)
grep -E '^CONFIG_' "$FRAG" | while IFS='=' read -r sym want; do
  got=$(grep -E "^${sym}=" .config | cut -d= -f2 || true)
  [ -z "$got" ] && got="(not set / unknown)"
  if [ "$got" = "$want" ]; then st=OK; else st=MISMATCH; fi
  printf '%-26s want=%-3s got=%-20s %s\n' "$sym" "$want" "$got" "$st"
done
```

```text

#
# merged configuration written to /var/tmp/K5/k5_kbuild/.config (needs make)
#
CONFIG_INV_ICM42600_I2C    want=m   got=m                    OK
CONFIG_CAN_FLEXCAN         want=y   got=m                    MISMATCH
CONFIG_PPS_CLIENT_GPIO     want=y   got=y                    OK
CONFIG_K5_IMU_DEBUG        want=y   got=(not set / unknown)  MISMATCH
```

출력에서 볼 것 — 두 가지 전형적인 실패다.

1. **`CAN_FLEXCAN=y`가 `m`으로 강등.** 이 `.config`에서 상위 심볼 `CONFIG_CAN=m`이다. tristate 규칙상 모듈인 부모 밑의 자식은 `y`가 될 수 없다. 에러 없이 조용히 바뀐다. 부팅 초기에 CAN이 필요하다고 `=y`를 썼다면, 이미지에는 모듈로 들어가서 initramfs 단계에 CAN이 없다.
2. **오타 심볼은 그냥 사라진다.** Kconfig는 모르는 심볼을 경고 없이 버린다.

Yocto의 `do_kernel_configcheck`가 정확히 이것을 잡는다. 클래스 코드에서 메시지를 확인한다.

```sh
cd /var/tmp/K5/k5_oe-core/meta
grep -n 'bb.warn( "\[kernel config\]\|promoting to fatal\|cfg/mismatch.txt\|cfg/invalid.txt\|--classify"' \
  classes-recipe/kernel-yocto.bbclass
```

```text
623:       extra_params = "--classify"
632:        outfile = "{}/{}/cfg/mismatch.txt".format( s, kmeta )
641:                bb.warn( "[kernel config]: specified values did not make it into the kernel's final configuration:\n\n%s" % results)
654:        outfile = "{}/{}/cfg/invalid.txt".format(s,kmeta)
663:                bb.warn( "[kernel config]: This BSP contains fragments with warnings:\n\n%s" % results)
684:                bb.warn( "[kernel config]: This BSP has configuration options defined in more than one config, with differing values:\n\n%s" % results)
688:        bb.fatal( "configuration warnings detected, werror is set, promoting to fatal" )
```

출력에서 볼 것: 세 종류 검사 — **mismatch**(요청값이 최종 `.config`에 없음), **invalid**(fragment 안의 잘못된 항목), **redefinition**(같은 심볼을 여러 fragment가 다른 값으로 정의). 결과 파일은 `${S}/kernel-meta/cfg/`에 남는다. `KCONF_AUDIT_LEVEL = "1"`(기본값)이면 `--classify`로 부팅에 중요한 항목 위주로 걸러 보고하므로, 전부 보려면 `2`로 올린다. 우리 bbappend는 `KMETA_AUDIT_WERROR = "1"`로 경고를 **빌드 실패**로 바꿨다 — 조용한 강등을 CI에서 막는 방법이다.

고친 fragment(레이어에 들어간 최종본):

```text
# K5 SensorKit kernel config fragment (applied by kernel-yocto via SRC_URI)
# IMU on I2C3
CONFIG_INV_ICM42600_I2C=m
# FlexCAN to the motor controllers -- we want it built in
CONFIG_CAN=y
CONFIG_CAN_FLEXCAN=y
# GNSS PPS on a GPIO
CONFIG_PPS_CLIENT_GPIO=y
```

### 3.7 `menuconfig` → `diffconfig` — fragment를 손으로 쓰지 않는 법

어떤 심볼이 필요한지 모를 때는 menuconfig에서 고르고, **바뀐 줄만** fragment로 뽑는다. `cml1.bbclass`의 `do_diffconfig`가 쓰는 명령은 이것이다.

```sh
sed -n 93,97p /var/tmp/K5/k5_oe-core/meta/classes-recipe/cml1.bbclass
```

```text
    if isdiff:
        statement = 'diff --unchanged-line-format= --old-line-format= --new-line-format="%L" ' + configorig + ' ' + config + '>' + fragment
        # No need to check the exit code as we know it's going to be
        # non-zero, but that's what we expect.
        subprocess.call(statement, shell=True)
```

menuconfig 대신 `scripts/config`로 두 항목을 바꾸고, 같은 diff 명령으로 fragment를 뽑는다.

```sh
#!/bin/bash
# K5: what "bitbake -c menuconfig" + "-c diffconfig" produce, emulated with scripts/config
set -euo pipefail
S=/var/lab/src/linux-6.18.55
O=/var/tmp/K5/k5_kbuild
cd "$O"
cp .config .config.orig                       # cml1 saves this before menuconfig
"$S/scripts/config" --file .config -m INV_ICM42600_SPI -e CAN_MCP251XFD_SANITY   # "menuconfig" edits
make -s -C "$S" O="$O" ARCH=arm64 olddefconfig
# exactly the diff command from cml1.bbclass do_diffconfig:
diff --unchanged-line-format= --old-line-format= --new-line-format="%L" \
     .config.orig .config > fragment.cfg || true
cat fragment.cfg
```

```text
CONFIG_CAN_MCP251XFD_SANITY=y
CONFIG_INV_ICM42600_SPI=m
```

출력에서 볼 것: 4천 줄이 넘는 `.config`에서 **바뀐 두 줄만** 남았다. 이것을 layer의 `.cfg`로 복사하면 끝이다. (`olddefconfig`가 `select`로 다른 심볼까지 켰다면 그 줄들도 함께 나온다 — 그런 줄은 지워도 된다. 의존성은 Kconfig가 다시 풀어 준다.)

실제 Yocto에서는 이렇게 쓴다.

> **실행하지 않음 — 가이드**

```sh
# 실행하지 않음 — 가이드 (kas shell 또는 oe-init-build-env 이후)
bitbake -c kernel_configme -f virtual/kernel     # 깨끗한 .config 만들기 (configme 다시 돌림)
bitbake -c menuconfig virtual/kernel             # ncurses 창. 저장 후 종료
bitbake -c diffconfig virtual/kernel             # "Config fragment has been dumped into: <WORKDIR>/fragment.cfg"
cp <WORKDIR>/fragment.cfg ../k5-platform/meta-k5-sensorkit-bsp/recipes-kernel/linux/files/k5-can.cfg
bitbake -c kernel_configcheck -f virtual/kernel  # 요청값이 살아남았나 검사 (mismatch.txt 등)
bitbake -c savedefconfig virtual/kernel          # "${B}/defconfig" — 벤더에 defconfig로 올릴 때
```

함정: `menuconfig`로 바꾼 `.config`는 **다음 `do_kernel_configme`가 돌면 사라진다** (레시피 입력이 아니기 때문). 반드시 `diffconfig`로 fragment를 뽑아 layer에 넣어야 남는다. "menuconfig로 켰는데 다음 날 빌드에서 꺼져 있다"는 신입의 단골 증상이다.

### 3.8 defconfig 통째로 vs fragment — 언제 무엇을

| 방법 | 장점 | 단점 | 언제 |
|---|---|---|---|
| layer에 `file://defconfig` 전체 | 결과가 명확 | 벤더 커널을 올릴 때 벤더의 새 설정을 다 놓친다, 리뷰 불가(수천 줄) | 벤더 defconfig와 완전히 다른 제품 (드묾) |
| 벤더 `KBUILD_DEFCONFIG` + `.cfg` fragment | 변경이 작고 이유가 보인다, 벤더 업데이트를 따라간다 | fragment가 많아지면 겹침(redefinition) 관리 필요 | **대부분의 회사** |
| `.scc` feature + `KERNEL_FEATURES` | 기능 단위 on/off, 패치와 설정을 같이 묶음 | 문법을 하나 더 배워야 함 | 제품군이 여러 개일 때 |

---

## 4. devtool — 드라이버 개발의 일상 루프

### 4.1 왜 devtool인가

레시피를 고칠 때마다 패치 파일을 만들고 `SRC_URI`에 넣고 `bitbake`를 돌리는 것은 느리다. **devtool은 레시피 소스를 내 손 안의 git 트리로 꺼내 주고**, 그 트리에서 고친 것을 그대로 빌드하게 해 주고(`externalsrc`), 끝나면 커밋들을 패치로 바꿔 layer에 넣어 준다. 3.5절에서 손으로 한 "SRCREV 기준 패치 만들기"를 자동으로 해 준다.

devtool 하위 명령 목록을 소스에서 뽑았다 (oe-core `scripts/lib/devtool/`).

```sh
cd /var/tmp/K5/k5_oe-core/scripts/lib/devtool
grep -ho "add_parser(.[a-z-]*.\(, help=.[^\"']*\)\?" *.py | sed -E "s/add_parser\(.([a-z-]+).(, help=.)?/\1 : /" | sort
```

```text
add : Add a new recipe
build : Build a recipe
build-image : 
build-sdk : 
check-upgrade-status : Report upgradability for multiple (or all) recipes
configure-help : Get help on configure script options
deploy-target : 
edit-recipe : Edit a recipe file
export : 
extract : Extract the source for an existing recipe
find-recipe : Find a recipe file
finish : Finish working on a recipe in your workspace
ide-sdk : 
import : 
latest-version : Report the latest version of an existing recipe
menuconfig : 
modify : Modify the source for an existing recipe
package : 
rename : Rename a recipe file in the workspace
reset : Remove a recipe from your workspace
runqemu : Run QEMU on the specified image
sdk-install : 
sdk-update : 
search : Search available recipes
status : Show workspace status
sync : Synchronize the source tree for an existing recipe
undeploy-target : 
update-recipe : Apply changes from external source tree to recipe
upgrade : Upgrade an existing recipe
```

(도움말이 비어 있는 것은 help 문자열이 다음 줄에 있어서 이 grep에 안 잡힌 것이다.) `devtool modify`가 workspace에 만드는 bbappend의 내용도 소스에 그대로 있다.

```sh
sed -n 941,953p standard.py
```

```text

            f.write('\ninherit externalsrc\n')
            f.write('# NOTE: We use pn- overrides here to avoid affecting multiple variants in the case where the recipe uses BBCLASSEXTEND\n')
            f.write('EXTERNALSRC:pn-%s = "%s"\n' % (pn, srctree))

            b_is_s = use_external_build(args.same_dir, args.no_same_dir, rd)
            if b_is_s:
                f.write('EXTERNALSRC_BUILD:pn-%s = "%s"\n' % (pn, srctree))

            if bb.data.inherits_class('kernel', rd):
                f.write('\ndo_kernel_configme:prepend() {\n'
                        '    if [ -e ${S}/.config ]; then\n'
                        '        mv ${S}/.config ${S}/.config.old\n'
```

출력에서 볼 것: 비밀은 **`inherit externalsrc` + `EXTERNALSRC`** 한 쌍이다. "fetch/unpack/patch를 건너뛰고 이 폴더를 소스로 써라." 커널 레시피라면 소스 트리에 남은 `.config`를 치워 configme와 충돌하지 않게 한다.

### 4.2 루프

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">devtool 드라이버 개발 루프</text>
  <rect x="20" y="40" width="190" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="62" text-anchor="middle" font-size="13">devtool modify</text>
  <text x="115" y="82" text-anchor="middle" font-size="12">SRCREV+패치 → git 트리</text>
  <rect x="250" y="40" width="180" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="62" text-anchor="middle" font-size="13">편집 · git commit</text>
  <text x="340" y="82" text-anchor="middle" font-size="12">workspace/sources/</text>
  <rect x="470" y="40" width="190" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="62" text-anchor="middle" font-size="13">devtool build</text>
  <text x="565" y="82" text-anchor="middle" font-size="12">externalsrc로 빌드</text>
  <rect x="470" y="150" width="190" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="172" text-anchor="middle" font-size="13">devtool deploy-target</text>
  <text x="565" y="192" text-anchor="middle" font-size="12">ssh로 보드에 설치</text>
  <rect x="250" y="150" width="180" height="56" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="172" text-anchor="middle" font-size="13">보드에서 시험</text>
  <text x="340" y="192" text-anchor="middle" font-size="12">dmesg · i2cdetect · iio</text>
  <line x1="210" y1="68" x2="248" y2="68" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="430" y1="68" x2="468" y2="68" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="565" y1="96" x2="565" y2="148" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="470" y1="178" x2="432" y2="178" stroke="currentColor" marker-end="url(#ah3)"/>
  <path d="M 340 150 L 340 110" stroke="currentColor" fill="none" marker-end="url(#ah3)"/>
  <text x="352" y="128" font-size="12">고칠 게 남았다</text>
  <rect x="20" y="260" width="190" height="56" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="115" y="282" text-anchor="middle" font-size="13">devtool finish</text>
  <text x="115" y="302" text-anchor="middle" font-size="12">커밋 → 패치 → layer</text>
  <rect x="250" y="260" width="180" height="56" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="282" text-anchor="middle" font-size="13">회사 BSP layer</text>
  <text x="340" y="302" text-anchor="middle" font-size="12">bbappend + 0001..patch</text>
  <rect x="470" y="260" width="190" height="56" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="565" y="282" text-anchor="middle" font-size="13">PR · CI 이미지 빌드</text>
  <text x="565" y="302" text-anchor="middle" font-size="12">workspace는 정리됨</text>
  <path d="M 250 190 L 115 190 L 115 258" stroke="currentColor" fill="none" marker-end="url(#ah3)"/>
  <text x="125" y="226" font-size="12">다 됐다</text>
  <line x1="210" y1="288" x2="248" y2="288" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="430" y1="288" x2="468" y2="288" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="340" y="350" text-anchor="middle" font-size="12">update-recipe -a &lt;layer&gt; = finish와 비슷하지만 workspace를 유지 (계속 작업할 때)</text>
</svg>
```
그림 3 — devtool 루프. 안쪽 사각형(modify → 편집 → build → deploy → 시험)이 하루에 수십 번 도는 루프이고, 바깥 줄(finish → layer → PR)은 기능 하나가 끝날 때 한 번 돈다.

> **실행하지 않음 — 가이드**: IMU 드라이버를 vendor 커널에서 고치는 하루. 출력은 적지 않는다.

```sh
# 실행하지 않음 — 가이드
devtool modify linux-fslc-imx                    # workspace/sources/linux-fslc-imx 에 git 트리 (branch "devtool")
cd workspace/sources/linux-fslc-imx
git log --oneline -3                             # 확인: SRCREV 위에 우리 0001 패치가 커밋으로 올라가 있어야 함
vi drivers/iio/imu/inv_icm42600/inv_icm42600_core.c
git commit -am "iio: imu: inv_icm42600: fix FIFO watermark on K5"
devtool build linux-fslc-imx                     # 커널 + 모듈만 빌드 (이미지 X)
devtool deploy-target linux-fslc-imx root@192.168.7.2   # do_install 결과를 ssh+tar로 보드에
# 보드: reboot 또는 rmmod/modprobe inv-icm42600-i2c ; dmesg | grep icm
devtool menuconfig linux-fslc-imx                # 설정 변경은 devtool-fragment.cfg 로 남는다
devtool finish linux-fslc-imx ../k5-platform/meta-k5-sensorkit-bsp   # 커밋 → patch, bbappend 갱신, workspace 정리
```

확인 포인트와 함정:

- `deploy-target`은 **`do_install`이 설치한 파일을 그대로** 보드에 tar로 푼다(`deploy.py`의 `tar cf - . | ssh ...`). 커널 `Image`나 DTB를 부트 파티션에 써 주지는 않는다. 커널 본체를 바꿨다면 `deploy/images/<machine>/`의 Image/DTB를 직접 복사하거나 tftp/NFS 부팅을 쓴다. 모듈(.ko)만 고칠 때 가장 빠르다.
- 옵션은 소스에 있다: `-n/--dry-run`(무엇을 보낼지만 출력), `-s`(진행 표시), `-P` 포트, `-I` 키, `-S/--strip`. 처음엔 `-n`으로 보내는 파일 목록부터 본다.
- `finish`/`update-recipe`는 **커밋된 것만** 패치로 만든다 (도움말: "changes need to have been committed"). 커밋 안 한 수정은 조용히 빠진다. `--mode patch|srcrev|auto` — 우리 소유의 git 저장소(예: 사내 앱)라면 `srcrev` 모드로 `SRCREV`만 갱신한다.
- `finish`의 대상 layer를 회사 BSP layer로 지정하면 **vendor 레시피 자체가 아니라 우리 layer에 bbappend를 만든다**. vendor layer 경로를 주면 남의 저장소를 고치게 된다.
- 새 소프트웨어(사내 센서 데몬 등)는 `devtool add k5-sensord https://git.k5.example/sensord.git`로 레시피 초안부터 만든다. 라이선스 체크섬·의존성은 생성된 레시피를 반드시 다시 읽고 고친다.

### 4.3 out-of-tree 모듈 레시피 — `inherit module`

드라이버를 커널 트리에 넣지 않고 따로 관리할 때(벤더 Wi-Fi 드라이버, 사내 FPGA 드라이버). oe-core의 예제 `meta-skeleton/recipes-kernel/hello-mod`가 표준 모양이다. 우리 layer의 레시피:

```text
# meta-k5-sensorkit-bsp/recipes-kernel/k5-imu-mod/k5-imu-mod_0.1.bb
SUMMARY = "K5 sensor-kit out-of-tree kernel module"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

inherit module

SRC_URI = "file://Makefile file://k5_imu_mod.c"
S = "${UNPACKDIR}"

RPROVIDES:${PN} += "kernel-module-k5-imu-mod"
COMPATIBLE_MACHINE = "k5-sensorkit"
```

md5는 oe-core의 `meta/files/common-licenses/GPL-2.0-only`를 `md5sum`해서 확인한 값이다. `module.bbclass`가 하는 일을 보자.

```sh
grep -nE 'KERNEL_SRC=|oe_runmake|O=\$\{STAGING_KERNEL_BUILDDIR\}|DEPMOD=echo|MODLIB=' \
  /var/tmp/K5/k5_oe-core/meta/classes-recipe/module.bbclass
```

```text
9:EXTRA_OEMAKE += "KERNEL_SRC=${STAGING_KERNEL_DIR}"
34:	oe_runmake KERNEL_PATH=${STAGING_KERNEL_DIR}   \
39:	           O=${STAGING_KERNEL_BUILDDIR} \
46:	oe_runmake DEPMOD=echo MODLIB="${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}" \
50:	           O=${STAGING_KERNEL_BUILDDIR} \
```

출력에서 볼 것: 결국 `make KERNEL_SRC=<커널 빌드 트리>`와 `make modules_install MODLIB=<D 아래> DEPMOD=echo`다 — G1에서 손으로 하던 out-of-tree 빌드 그대로다. 그래서 같은 Makefile을 랩 커널로 돌려 결과를 확인할 수 있다.

```c
// SPDX-License-Identifier: GPL-2.0
/* K5: minimal out-of-tree module, packaged by a recipe that does "inherit module" */
#include <linux/module.h>
#include <linux/init.h>

static int rate_hz = 200;
module_param(rate_hz, int, 0444);
MODULE_PARM_DESC(rate_hz, "IMU sample rate the sensor-kit app expects");

static int __init k5_imu_init(void)
{
	pr_info("k5_imu_mod: loaded, rate_hz=%d\n", rate_hz);
	return 0;
}

static void __exit k5_imu_exit(void)
{
	pr_info("k5_imu_mod: unloaded\n");
}

module_init(k5_imu_init);
module_exit(k5_imu_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("K5 sensor-kit out-of-tree module example");
```

```make
obj-m := k5_imu_mod.o

SRC := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC)

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	rm -f *.o *.ko *.mod.c *.mod .*.cmd Module.symvers modules.order
```

`module_do_compile`/`module_do_install`과 같은 make 호출을 랩 커널 빌드 트리로 돌린다.

```sh
#!/bin/bash
set -euo pipefail
K=/var/lab/out/linux-build
W=/var/tmp/K5/k5_mod; D=/var/tmp/K5/k5_mod_image
rm -rf "$W" "$D"; mkdir -p "$W" "$D"
cp /Users/donh/.elinux_scratch/K5/mod/* "$W/"; cd "$W"
make -s KERNEL_SRC="$K" all 2>&1
make -s KERNEL_SRC="$K" DEPMOD=echo MODLIB="$D/lib/modules/6.18.55" INSTALL_MOD_STRIP=1 modules_install 2>&1 | grep -v '^$' || true
find "$D" -type f | sed "s|$D||"
modinfo -F vermagic "$W/k5_imu_mod.ko"
modinfo -F parm "$W/k5_imu_mod.ko"
```

```text
/lib/modules/6.18.55/updates/k5_imu_mod.ko
6.18.55 SMP preempt mod_unload aarch64
rate_hz:IMU sample rate the sensor-kit app expects (int)
```

출력에서 볼 것: 경고 0개로 빌드됐고, 외부 모듈은 `/lib/modules/<ver>/updates/`에 설치된다 (`INSTALL_MOD_DIR` 기본값). Yocto에서는 이 파일이 `kernel-module-k5-imu-mod` 패키지가 된다 — `kernel-module-split`이 `.ko` 파일 이름의 `_`를 `-`로 바꿔 패키지 이름을 만든다. machine conf의 `MACHINE_EXTRA_RRECOMMENDS`와 이미지의 `IMAGE_INSTALL`이 이 이름을 쓴다.

"패키지가 rootfs에 들어간 상태"를 흉내 내서 랩 rootfs 사본에 그 트리를 넣고 QEMU에서 로드한다.

```sh
#!/bin/bash
set -euo pipefail
W=/var/tmp/K5/k5_qemu; rm -rf "$W"; mkdir -p "$W/r"; cd "$W"
cp -a /var/lab/build/rootfs/. r/
cp -a /var/tmp/K5/k5_mod_image/lib/. r/lib/
(cd r && find . | cpio -o -H newc --quiet | gzip) > i.gz
timeout 90 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd i.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=insmod~/lib/modules/6.18.55/updates/k5_imu_mod.ko~rate_hz=400~&&~cat~/sys/module/k5_imu_mod/parameters/rate_hz~&&~dmesg~|~grep~k5_" \
  | tr -d '\r' | grep -E "k5_|^[0-9]+$" || true
```

```text
400
[    0.000000] Kernel command line: console=ttyAMA0 quiet LAB_CMD=insmod~/lib/modules/6.18.55/updates/k5_imu_mod.ko~rate_hz=400~&&~cat~/sys/module/k5_imu_mod/parameters/rate_hz~&&~dmesg~|~grep~k5_
[    0.000000] Unknown kernel command line parameters "LAB_CMD=insmod~/lib/modules/6.18.55/updates/k5_imu_mod.ko~rate_hz=400~&&~cat~/sys/module/k5_imu_mod/parameters/rate_hz~&&~dmesg~|~grep~k5_", will be passed to user space.
[    0.653965]     LAB_CMD=insmod~/lib/modules/6.18.55/updates/k5_imu_mod.ko~rate_hz=400~&&~cat~/sys/module/k5_imu_mod/parameters/rate_hz~&&~dmesg~|~grep~k5_
[    0.749460] k5_imu_mod: loading out-of-tree module taints kernel.
[    0.752619] k5_imu_mod: loaded, rate_hz=400
```

출력에서 볼 것: 파라미터 `rate_hz=400`이 sysfs(`/sys/module/k5_imu_mod/parameters/rate_hz`)에 반영됐고, 트리 밖 모듈이라 커널이 "taints kernel"(O 플래그)을 찍었다. 실제 이미지에서는 `depmod`가 rootfs 생성 때 돌아서 `modprobe k5_imu_mod`로 로드된다. 이 노트에서는 `insmod` 경로로만 확인했다. 타임스탬프는 실행마다 다르다.

---

## 5. U-Boot 레시피 다듬기

U-Boot도 커널과 같은 패턴이다: vendor 레시피 + 우리 bbappend + Kconfig fragment. oe-core의 `u-boot-configure.inc`가 `SRC_URI`의 `.cfg`를 모아 `merge_config.sh`로 합친다.

```sh
cd /var/tmp/K5/k5_oe-core/meta
grep -n 'find_cfgs\|UBOOT_CONFIG_FRAGMENTS\|merge_config' recipes-bsp/u-boot/u-boot-configure.inc
cd /var/tmp/K5/k5_meta-freescale/recipes-bsp/u-boot
grep -nE '^require|^SRCBRANCH|^SRCREV|^PROVIDES' u-boot-imx_2025.04.bb u-boot-imx-common_2025.04.inc
```

```text
39:    config_fragments=$(uboot_config_get_indexed_value "${UBOOT_CONFIG_FRAGMENTS}" $i)
42:    if [ -n "${@' '.join(find_cfgs(d))}" ]; then
43:        merge_config.sh -m -O ${B}/${builddir} ${B}/${builddir}/.config ${@" ".join(find_cfgs(d))}
54:    merge_config.sh -m .config ${@" ".join(find_cfgs(d))}
u-boot-imx_2025.04.bb:5:require recipes-bsp/u-boot/u-boot.inc
u-boot-imx_2025.04.bb:6:require u-boot-imx-common_${PV}.inc
u-boot-imx_2025.04.bb:11:PROVIDES += "u-boot u-boot-mfgtool"
u-boot-imx-common_2025.04.inc:17:SRCBRANCH = "lf_v2025.04"
u-boot-imx-common_2025.04.inc:19:SRCREV = "99518e6b6f20cb6a2bf19115e355db9f58100af8"
```

출력에서 볼 것: vendor의 `u-boot-imx`도 oe-core의 `u-boot.inc`를 require하므로 `.cfg` 병합을 공짜로 얻는다. 우리 bbappend:

```text
# meta-k5-sensorkit-bsp/recipes-bsp/u-boot/u-boot-imx_%.bbappend
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append:k5-sensorkit = " file://k5-uboot.cfg"

# meta-k5-sensorkit-bsp/recipes-bsp/u-boot/files/k5-uboot.cfg
CONFIG_BOOTDELAY=1
CONFIG_ENV_IS_IN_MMC=y
CONFIG_CMD_I2C=y
```

세 심볼이 실제로 존재하는지 랩 U-Boot 소스(v2026.10)에서 확인했다 (vendor는 2025.04라서 최종 확인은 vendor 트리에서 해야 한다).

```sh
S=/var/lab/src/u-boot
for s in BOOTDELAY ENV_IS_IN_MMC CMD_I2C; do
  grep -rln "^config $s\$" $S --include='Kconfig*' | head -1 | sed "s|$S/|$s -> |"
done
```

```text
BOOTDELAY -> boot/Kconfig
ENV_IS_IN_MMC -> env/Kconfig
CMD_I2C -> cmd/Kconfig
```

U-Boot 쪽 실무 포인트:

- 보드 포팅이 끝나면 `UBOOT_CONFIG[sd] = "k5_sensorkit_defconfig"`처럼 **우리 defconfig**로 바꾸고, U-Boot 소스에 보드 디렉터리(`board/k5/...`)·DTS·defconfig를 추가하는 패치를 bbappend에 넣는다.
- i.MX8M은 U-Boot 바이너리만으로 부팅되지 않는다. `imx-boot` 레시피가 SPL + DDR 펌웨어 + TF-A(`ATF_PLATFORM`) + U-Boot를 `mkimage_imx8`로 묶어 `flash.bin`을 만든다(C 모듈의 부트 체인 참고). machine conf의 `IMXBOOT_TARGETS`, `DDR_FIRMWARE_NAME`이 여기에 쓰인다.
- U-Boot 환경 변수를 Linux에서 읽고 쓰려면(`fw_printenv`) `libubootenv`와 `/etc/fw_env.config`가 맞아야 한다 — A/B 업데이트(R 모듈)에서 다시 나온다.

---

## 6. 빌드가 실패했을 때 — 디버깅 도구

### 6.1 실패한 태스크의 로그부터

BitBake 에러는 항상 `ERROR: <recipe>-<ver> do_<task>: ... Logfile of failure stored in: <WORKDIR>/temp/log.do_<task>.<pid>`로 끝난다. 그 `WORKDIR/temp/` 폴더가 디버깅의 출발점이다.

| 파일 | 내용 |
|---|---|
| `log.do_compile` (심볼릭 링크 → 최신 실행) | 그 태스크의 전체 출력 |
| `run.do_compile` | **실제로 실행된 셸 스크립트** — 변수가 다 풀린 상태 |
| `log.task_order` | 이 레시피에서 태스크가 돈 순서 |

`run.do_compile`이 핵심이다. 이것을 열면 BitBake가 어떤 `CC`, `CFLAGS`, `make` 인자로 돌렸는지 그대로 보인다. Don이 펌웨어 빌드 로그에서 실제 컴파일러 커맨드라인을 뽑아 보던 것과 같다.

### 6.2 변수 추적 — `bitbake -e`, `bitbake-getvar`

"`KERNEL_DEVICETREE`가 왜 이 값이지?"는 변수의 **이력**을 보면 풀린다. `bitbake-getvar`의 옵션을 소스에서 확인했다.

```sh
grep -n 'add_argument' /var/tmp/K5/k5_bitbake/bin/bitbake-getvar | cut -c1-110
```

```text
25:    parser.add_argument("variable", help="variable name to query")
26:    parser.add_argument("-r", "--recipe", help="Recipe name to query", default=None, required=False)
27:    parser.add_argument('-u', '--unexpand', help='Do not expand the value (with --value)', action="store_tr
28:    parser.add_argument('-f', '--flag', help='Specify a variable flag to query (with --value)', default=Non
29:    parser.add_argument('--value', help='Only report the value, no history and no variable name', action="s
30:    parser.add_argument('-q', '--quiet', help='Silence bitbake server logging', action="store_true")
31:    parser.add_argument('--ignore-undefined', help='Suppress any errors related to undefined variables', ac
```

> **실행하지 않음 — 가이드**

```sh
# 실행하지 않음 — 가이드
bitbake-getvar PREFERRED_PROVIDER_virtual/kernel          # 이력: 어느 파일 몇 번째 줄이 값을 정했나
bitbake-getvar -r virtual/kernel --value SRC_URI          # 우리 패치·cfg가 정말 들어갔나
bitbake-getvar -r virtual/kernel KERNEL_FEATURES
bitbake -e virtual/kernel > env.txt                       # 전체 환경 (수만 줄)
grep -n '^# \$KERNEL_DEVICETREE\b' -A12 env.txt           # 이력 블록: set / append / override 적용 순서
bitbake-layers show-appends linux-fslc-imx                # 우리 bbappend가 붙었나
bitbake-layers show-layers                                 # 우선순위
bitbake -c devshell virtual/kernel                         # 빌드 환경 그대로의 셸 (CC, ARCH 등 설정됨)
bitbake -c cleansstate k5-imu-mod && bitbake k5-imu-mod    # 캐시 무시하고 다시
```

함정 몇 가지.

- `bitbake-getvar -r linux-fslc-imx`가 아니라 `-r virtual/kernel`로 물으면 **실제로 선택된 레시피**를 확인할 수 있다. bbappend를 엉뚱한 레시피(`linux-imx`)에 붙여 놓고 왜 안 먹히나 고민하는 일이 흔하다 — meta-freescale에는 `linux-imx_6.18.bb`도 있지만 NXP BSP 기본은 `linux-fslc-imx`다(2.1절).
- `devshell`은 `do_patch` 이후 상태(`addtask devshell after do_patch do_prepare_recipe_sysroot`)에서 열린다. 여기서 직접 `make`를 돌려 볼 수 있지만, 여기서 고친 것은 레시피에 남지 않는다 — 남기려면 devtool.

### 6.3 패키지와 파일 찾기 — `oe-pkgdata-util`

빌드는 됐는데 "이 파일이 이미지에 왜 없지/어느 패키지에 들어갔지"일 때 쓴다. 하위 명령(소스 `scripts/oe-pkgdata-util`): `lookup-pkg`, `list-pkgs`, `list-pkg-files`, `lookup-recipe`, `package-info`, `find-path`, `read-value`, `glob`.

```sh
# 실행하지 않음 — 가이드
oe-pkgdata-util find-path /lib/modules/*/updates/k5_imu_mod.ko   # → kernel-module-k5-imu-mod
oe-pkgdata-util list-pkg-files kernel-module-inv-icm42600-i2c     # IMU 드라이버 패키지에 든 파일
oe-pkgdata-util lookup-recipe kernel-module-k5-imu-mod            # → k5-imu-mod
oe-pkgdata-util list-pkgs -p linux-fslc-imx | grep icm            # 커널 레시피가 만든 모듈 패키지
```

### 6.4 "왜 다시 빌드하지?" — `bitbake-diffsigs`

한 줄 고쳤는데 수백 개 태스크가 다시 돌면, 서명(signature) 비교로 원인을 찾는다. `bitbake-diffsigs -t recipe task`의 의미를 소스에서 확인했다.

```sh
sed -n 135,141p /var/tmp/K5/k5_bitbake/bin/bitbake-diffsigs
```

```text
    parser.add_argument("-t", "--task",
                        help="find the signature data files for the last two runs of the specified task and compare them",
                        action="store", dest="taskargs", nargs=2, metavar=('recipename', 'taskname'))

    parser.add_argument("-s", "--signature",
                        help="With -t/--task, specify the signatures to look for instead of taking the last two",
                        action="store", dest="sigargs", nargs=2, metavar=('fromsig', 'tosig'))
```

```sh
# 실행하지 않음 — 가이드
bitbake-diffsigs -t linux-fslc-imx do_compile    # 지난 두 번의 do_compile 서명 비교 → 바뀐 변수/파일/의존 태스크
bitbake-dumpsig -t linux-fslc-imx do_compile     # 한 서명이 무엇으로 만들어졌는지 전부
bitbake -S printdiff k5-sensorkit-image          # 이미지 전체에서 sstate와 어긋나는 첫 지점들
```

전형적인 원인: `DATETIME` 같은 매번 바뀌는 값을 레시피 변수에 넣음, `AUTOREV`, layer 안의 파일을 빌드 중에 생성, 호스트 경로가 변수에 섞임. 다음 절(sstate)을 이해하면 이 출력이 읽힌다.

---

## 7. sstate cache와 hash equivalence

### 7.1 직관 — 입력 해시로 찾는 결과물 캐시

BitBake는 각 태스크마다 **입력의 해시(taskhash)** 를 계산한다: 태스크 코드 + 그 태스크가 쓰는 변수 값 + `SRC_URI` 파일 체크섬 + 의존 태스크들의 해시. 같은 해시의 결과물이 캐시에 있으면 태스크를 돌리지 않고 결과만 풀어 놓는다(setscene). 이 캐시가 **sstate**(shared state)다. ccache와 비슷하지만 단위가 "컴파일 한 번"이 아니라 "태스크 하나의 출력 전체"다.

sstate 파일 이름 규칙이 클래스에 있다.

```sh
grep -nE '^SSTATE_PKGSPEC|^SSTATE_PKGNAME|^SSTATE_PATHSPEC' /var/tmp/K5/k5_oe-core/meta/classes-global/sstate.bbclass
```

```text
42:SSTATE_PKGSPEC    = "sstate:${PN}:${PACKAGE_ARCH}${TARGET_VENDOR}-${TARGET_OS}:${PV}:${PR}:${SSTATE_PKGARCH}:${SSTATE_VERSION}:"
44:SSTATE_PKGNAME    = "${SSTATE_EXTRAPATH}${@generate_sstatefn(d.getVar('SSTATE_PKGSPEC'), d.getVar('BB_UNIHASH'), d.getVar('SSTATE_CURRTASK'), False, d)}"
48:SSTATE_PATHSPEC   = "${SSTATE_DIR}/${SSTATE_EXTRAPATHWILDCARD}*/*/${SSTATE_PKGSPEC}*_${SSTATE_PATH_CURRTASK}.tar.zst*"
```

출력에서 볼 것: 파일 이름에 들어가는 해시는 `BB_UNIHASH`다 — taskhash가 아니다. 이것이 hash equivalence의 핵심이다. 압축은 `.tar.zst`.

### 7.2 hash equivalence — "입력은 바뀌었지만 출력은 같다"

문제: 라이브러리 주석 한 줄을 고치면 그 라이브러리의 taskhash가 바뀌고, 그것에 의존하는 **모든** 레시피의 taskhash가 연쇄로 바뀐다. 출력 바이너리는 똑같은데도 전부 다시 빌드한다.

해결: 태스크를 실제로 돌린 뒤 **출력의 해시(outhash)** 를 계산해 서버(hashserv)에 보고한다. 이전에 같은 outhash를 낸 taskhash가 있으면 "둘은 동등하다"고 기록하고, 둘 다 같은 **unihash**를 쓴다. 하위 태스크의 해시는 taskhash가 아니라 unihash로 계산되므로, 연쇄 재빌드가 거기서 끊긴다.

기본값을 확인한다.

```sh
cd /var/tmp/K5/k5_oe-core/meta
grep -n 'BB_SIGNATURE_HANDLER\|BB_HASHSERVE ' conf/bitbake.conf
grep -n '^SSTATE_HASHEQUIV_METHOD ' classes-global/sstate.bbclass
sed -n 539,545p lib/oe/sstatesig.py
cat conf/fragments/yocto/sstate-mirror-cdn.conf | tail -2
```

```text
956:BB_SIGNATURE_HANDLER ?= "OEEquivHash"
957:BB_HASHSERVE ??= "auto"
966:    SSTATE_HASHEQUIV_OWNER CCACHE_TOP_DIR BB_HASHSERVE GIT_CEILING_DIRECTORIES \
122:SSTATE_HASHEQUIV_METHOD ?= "oe.sstatesig.OEOuthashBasic"
539:def OEOuthashBasic(path, sigfile, task, d):
540:    """
541:    Basic output hash function
542:
543:    Calculates the output hash of a task by hashing all output file metadata,
544:    and file contents.
545:    """
BB_HASHSERVE_UPSTREAM = 'wss://hashserv.yoctoproject.org/ws'
SSTATE_MIRRORS ?= "file://.* http://sstate.yoctoproject.org/all/PATH;downloadfilename=PATH"
```

출력에서 볼 것: 현재 oe-core는 **기본으로** hash equivalence를 켠다(`OEEquivHash`, `BB_HASHSERVE = "auto"` = 빌드할 때 로컬 서버를 자동으로 띄움). outhash는 출력 파일의 메타데이터와 내용 전체의 sha256이다. Yocto 프로젝트가 공개 sstate 미러와 hashserv를 운영하며, `OE_FRAGMENTS += "core/yocto/sstate-mirror-cdn"` 한 줄(6.0의 새 "config fragment" 방식)로 쓸 수 있다 — 단 회사 BSP의 레시피는 거기 없으므로 효과는 공통 부분(툴체인, native 도구)에 한정된다.

동작을 장난감 모델로 확인한다. 실제 BitBake 코드가 아니라 **아이디어**를 돌려 보는 Python이다.

```python
#!/usr/bin/env python3
"""K5: toy model of taskhash vs outhash vs unihash (the idea behind OEEquivHash)."""
import hashlib

def h(*parts):
    return hashlib.sha256("\x00".join(parts).encode()).hexdigest()[:12]

hashserv = {}        # taskhash -> unihash   (what bitbake-hashserv stores)
sstate = {}          # unihash  -> artifact  (what SSTATE_DIR / mirror stores)

def build(recipe, recipe_txt, src, dep_unihash, compile_fn):
    taskhash = h(recipe, recipe_txt, src, dep_unihash)     # inputs only
    if taskhash in hashserv and hashserv[taskhash] in sstate:
        uni = hashserv[taskhash]
        print(f"  {recipe:8} taskhash={taskhash} -> unihash={uni}  sstate HIT (no compile)")
        return uni
    out = compile_fn(src)                                  # really run do_compile
    outhash = h(out)                                       # hash of the OUTPUT
    # same output already known under another taskhash? reuse that unihash
    uni = next((u for t, u in hashserv.items() if sstate.get(u) == out), taskhash)
    hashserv[taskhash] = uni
    sstate.setdefault(uni, out)
    tag = "EQUIVALENT to earlier build" if uni != taskhash else "new output"
    print(f"  {recipe:8} taskhash={taskhash} outhash={outhash} unihash={uni}  BUILT ({tag})")
    return uni

def cc(src):          # "compiler": comments do not reach the binary
    return "\n".join(l for l in src.splitlines() if not l.strip().startswith("//"))

libsrc = "int imu_read(void);\n"
appsrc = "int main(void){return imu_read();}\n"

print("build 1: clean")
u = build("libimu", "v1", libsrc, "", cc)
build("imu-app", "v1", appsrc, u, cc)

print("build 2: someone adds a comment to libimu")
u = build("libimu", "v1", "// fix typo in doc\n" + libsrc, "", cc)
build("imu-app", "v1", appsrc, u, cc)

print("build 3: real code change in libimu")
u = build("libimu", "v1", libsrc.replace("void", "int ch"), "", cc)
build("imu-app", "v1", appsrc, u, cc)
```

```text
build 1: clean
  libimu   taskhash=57c8654ef891 outhash=caca8cfe52d3 unihash=57c8654ef891  BUILT (new output)
  imu-app  taskhash=56c650f3fb70 outhash=80bb34ccdefa unihash=56c650f3fb70  BUILT (new output)
build 2: someone adds a comment to libimu
  libimu   taskhash=e298159f5236 outhash=caca8cfe52d3 unihash=57c8654ef891  BUILT (EQUIVALENT to earlier build)
  imu-app  taskhash=56c650f3fb70 -> unihash=56c650f3fb70  sstate HIT (no compile)
build 3: real code change in libimu
  libimu   taskhash=9315b083660e outhash=84d3efc8c87d unihash=9315b083660e  BUILT (new output)
  imu-app  taskhash=2877c63ea62a outhash=80bb34ccdefa unihash=56c650f3fb70  BUILT (EQUIVALENT to earlier build)
```

출력에서 볼 것:

- **build 2**: libimu의 taskhash는 바뀌었지만(`e298…`) outhash가 build 1과 같아서(`caca…`) unihash가 예전 값(`57c8…`)으로 매핑됐다. 그래서 imu-app의 taskhash가 **변하지 않았고** 캐시 HIT — 컴파일 없이 끝났다. 실제 Yocto에서는 "주석 한 줄 고쳤더니 그 레시피만 다시 빌드되고 이미지까지 연쇄가 안 간다"로 나타난다.
- **build 3**: libimu가 진짜로 바뀌어 새 unihash. imu-app은 다시 빌드됐지만 출력이 같았으므로 다시 동등 판정을 받았다 — 그 **아래 단계**(패키징, 이미지)는 재사용된다.
- 전제 조건: 출력이 **재현 가능**해야 한다. 빌드할 때마다 타임스탬프가 박히면 outhash가 매번 달라 동등 판정이 절대 나지 않는다. 그래서 8절의 재현성이 sstate 효율과 직결된다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">태스크 하나가 sstate를 찾는 흐름</text>
  <rect x="20" y="36" width="200" height="54" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="58" text-anchor="middle" font-size="12">입력: 코드 · 변수 · 파일</text>
  <text x="120" y="76" text-anchor="middle" font-size="12">+ 의존 태스크 unihash</text>
  <rect x="250" y="36" width="170" height="54" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="335" y="58" text-anchor="middle" font-size="13">taskhash</text>
  <text x="335" y="76" text-anchor="middle" font-size="12">sha256(입력)</text>
  <rect x="450" y="36" width="210" height="54" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="555" y="58" text-anchor="middle" font-size="13">hashserv 조회</text>
  <text x="555" y="76" text-anchor="middle" font-size="12">taskhash → unihash</text>
  <line x1="220" y1="63" x2="248" y2="63" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="420" y1="63" x2="448" y2="63" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="450" y="130" width="210" height="70" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="555" y="152" text-anchor="middle" font-size="13">sstate 찾기 (unihash)</text>
  <text x="555" y="172" text-anchor="middle" font-size="12">1) SSTATE_DIR</text>
  <text x="555" y="190" text-anchor="middle" font-size="12">2) SSTATE_MIRRORS</text>
  <line x1="555" y1="90" x2="555" y2="128" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="450" y="250" width="210" height="54" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="555" y="272" text-anchor="middle" font-size="13">HIT → setscene</text>
  <text x="555" y="290" text-anchor="middle" font-size="12">tar.zst 풀기, 수 초</text>
  <line x1="555" y1="200" x2="555" y2="248" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="200" y="250" width="200" height="54" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="300" y="272" text-anchor="middle" font-size="13">MISS → 태스크 실행</text>
  <text x="300" y="290" text-anchor="middle" font-size="12">수 분 ~ 수십 분</text>
  <path d="M 450 180 L 300 180 L 300 248" stroke="currentColor" fill="none" marker-end="url(#ah4)"/>
  <rect x="20" y="250" width="150" height="54" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="95" y="272" text-anchor="middle" font-size="13">outhash 계산</text>
  <text x="95" y="290" text-anchor="middle" font-size="12">출력 파일 sha256</text>
  <line x1="200" y1="277" x2="172" y2="277" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="20" y="334" width="380" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="210" y="354" text-anchor="middle" font-size="12">hashserv에 보고: 같은 outhash가 있으면</text>
  <text x="210" y="372" text-anchor="middle" font-size="12">그 unihash 재사용 → 하위 태스크 해시 불변</text>
  <line x1="95" y1="304" x2="95" y2="332" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="450" y="334" width="210" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="555" y="354" text-anchor="middle" font-size="12">결과를 sstate에 저장</text>
  <text x="555" y="372" text-anchor="middle" font-size="12">다음 빌드·다른 PC가 HIT</text>
  <line x1="400" y1="359" x2="448" y2="359" stroke="currentColor" marker-end="url(#ah4)"/>
</svg>
```
그림 4 — 태스크 하나가 sstate를 찾고 채우는 흐름. 오른쪽 위→아래가 빠른 길(HIT), 왼쪽 아래가 느린 길(MISS → 실행 → outhash 보고 → 저장)이다. 동등성 정보는 hashserv에, 결과물은 sstate에 따로 있다는 점이 중요하다.

### 7.3 CI에서 공유하기 — DL_DIR, SSTATE, hashserv, PR server

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">빌드 팜의 공유 캐시 구성</text>
  <rect x="20" y="40" width="190" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="64" text-anchor="middle" font-size="13">CI 러너 (nightly)</text>
  <text x="115" y="84" text-anchor="middle" font-size="12">sstate 쓰기 권한</text>
  <rect x="20" y="130" width="190" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="154" text-anchor="middle" font-size="13">CI 러너 (PR 빌드)</text>
  <text x="115" y="174" text-anchor="middle" font-size="12">읽기 위주</text>
  <rect x="20" y="220" width="190" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="244" text-anchor="middle" font-size="13">개발자 PC</text>
  <text x="115" y="264" text-anchor="middle" font-size="12">미러에서 읽기만</text>
  <rect x="300" y="40" width="200" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="400" y="62" text-anchor="middle" font-size="13">DL_DIR / PREMIRRORS</text>
  <text x="400" y="80" text-anchor="middle" font-size="12">소스 tarball · git 미러</text>
  <rect x="300" y="105" width="200" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="400" y="127" text-anchor="middle" font-size="13">SSTATE_MIRRORS</text>
  <text x="400" y="145" text-anchor="middle" font-size="12">http로 공개된 sstate</text>
  <rect x="300" y="170" width="200" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="400" y="192" text-anchor="middle" font-size="13">BB_HASHSERVE</text>
  <text x="400" y="210" text-anchor="middle" font-size="12">bitbake-hashserv</text>
  <rect x="300" y="235" width="200" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="400" y="257" text-anchor="middle" font-size="13">PRSERV_HOST</text>
  <text x="400" y="275" text-anchor="middle" font-size="12">bitbake-prserv</text>
  <line x1="210" y1="70" x2="298" y2="65" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="210" y1="160" x2="298" y2="130" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="210" y1="250" x2="298" y2="195" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="540" y="40" width="120" height="245" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="600" y="70" text-anchor="middle" font-size="12">무엇을 아끼나</text>
  <text x="600" y="98" text-anchor="middle" font-size="12">네트워크</text>
  <text x="600" y="113" text-anchor="middle" font-size="12">· upstream 장애</text>
  <text x="600" y="150" text-anchor="middle" font-size="12">컴파일 시간</text>
  <text x="600" y="210" text-anchor="middle" font-size="12">연쇄 재빌드</text>
  <text x="600" y="262" text-anchor="middle" font-size="12">패키지 버전</text>
  <text x="600" y="277" text-anchor="middle" font-size="12">단조 증가</text>
  <text x="340" y="314" text-anchor="middle" font-size="12">원칙: 쓰기는 신뢰된 CI만, 나머지는 읽기 — 캐시 오염을 막는다</text>
</svg>
```
그림 5 — 빌드 팜의 공유 캐시 네 가지와 각각이 아끼는 것. 실제 서버 주소는 `kas/ci-mirrors.yml`의 예시 값처럼 사내 환경마다 다르다.

| 설정 | 하는 일 | 실무 메모 |
|---|---|---|
| `DL_DIR` | 받은 소스(tarball, git 미러)를 모아 두는 곳 | 여러 빌드 디렉터리가 공유. NFS여도 됨 |
| `BB_GENERATE_MIRROR_TARBALLS = "1"` | git 소스도 tarball로 `DL_DIR`에 남김 | 이것을 사내 `PREMIRRORS`로 공개 → GitHub 장애와 무관하게 빌드 |
| `BB_NO_NETWORK = "1"` | 네트워크 금지 | 릴리스 빌드가 미러만으로 되는지 검증 |
| `SSTATE_DIR` / `SSTATE_MIRRORS` | 로컬 sstate / 원격 sstate (`PATH` 치환) | 원격은 http 정적 서버면 충분 |
| `BB_HASHSERVE` | `auto`=로컬 자동, `host:port`=공유 서버 | 공유 sstate를 쓰면 hashserv도 공유해야 동등성 정보가 같이 간다 |
| `PRSERV_HOST` | PR server — 패키지 revision 자동 증가 | 아래 설명 |

**PR server**: 패키지 버전은 `PV-PR`(예: `1.0-r0`)다. 레시피 내용이 바뀌어도 사람이 `PR`을 올리지 않으면 같은 버전의 다른 패키지가 생겨 보드의 패키지 관리자가 업데이트를 안 한다. PR server는 "이 레시피의 출력 해시가 바뀔 때마다 `r0.1`, `r0.2`…로 자동으로 올리는" 작은 DB 서버다 (`package.bbclass`: `PRSERV_ACTIVE = "${@bool(d.getVar("PRSERV_HOST"))}"`, 없으면 `PRSERV_PV_AUTOINC`가 0으로 고정). **패키지 피드로 현장 장비를 업데이트하는 제품**이라면 필수다. 이미지 통째 교체(A/B, R 모듈)만 한다면 덜 중요하다. DB는 `bitbake-prserv-tool export/import`로 백업한다.

> **실행하지 않음 — 가이드**: 서버 띄우기 (bitbake 저장소 `bin/`에 있는 도구들)

```sh
# 실행하지 않음 — 가이드
bitbake-hashserv --bind 0.0.0.0:8686 --database /srv/yocto/hashserv.db
# 개발자용 읽기 전용 창구: -r/--read-only, 또는 --anon-perms로 익명 권한을 @read 로 제한 (옵션은 bin/bitbake-hashserv 소스에서 확인)
bitbake-prserv   --start --host 0.0.0.0 --port 8585 --file /srv/yocto/prserv.sqlite3
# sstate 미러 = SSTATE_DIR을 웹 서버로 공개 (예: nginx root /srv/yocto/sstate)
```

### 7.4 빌드 시간·디스크 줄이기

| 설정 | 효과 | 주의 |
|---|---|---|
| `BB_NUMBER_THREADS`, `PARALLEL_MAKE` | 태스크 병렬도 / 태스크 안 make `-j` | 기본은 CPU 수. RAM 부족(OOM)이면 낮춘다 (C++ 큰 레시피는 job당 1~2 GB) |
| `BB_PRESSURE_MAX_CPU` / `_IO` / `_MEMORY` | Linux PSI 압력이 높으면 새 태스크 시작을 미룸 | 공유 빌드 서버에서 유용 |
| `INHERIT += "rm_work"` | 레시피 빌드가 끝나면 `WORKDIR` 삭제 | 디스크 크게 절약. 디버깅할 레시피는 `RM_WORK_EXCLUDE += "linux-fslc-imx"` |
| `BB_DISK_DIRS` | 디스크가 부족하면 태스크 중단/정지 | local.conf 샘플에 `STOPTASKS,${SSTATE_DIR},1G,100K` 같은 예가 있다 |
| 공유 `DL_DIR`/`SSTATE` | 두 번째 빌드부터 대부분 setscene | 위 7.3 |
| `kas-container` | 호스트 배포판 차이 제거 | 재현성에도 도움 |

호스트 사양 기준(코어 수, RAM, 디스크 최소치)은 K4의 호스트 준비 절과 Yocto Reference Manual "System Requirements"를 따른다.

---

## 8. 재현 가능한 빌드 — SOURCE_DATE_EPOCH

### 8.1 왜 같은 소스가 다른 바이너리가 되나

같은 커밋을 두 번 빌드해도 파일 mtime, tar/gzip 헤더의 시간, 빌드 경로, 파일 순서, 사용자 이름이 결과에 섞여 해시가 달라진다. 그러면 (1) "이 바이너리가 정말 이 소스에서 나왔나"를 증명할 수 없고(보안·규제), (2) hash equivalence가 동작하지 않는다(7.2).

해법의 중심이 **`SOURCE_DATE_EPOCH`** 다 — "현재 시간 대신 이 시각을 써라"라는 표준 환경 변수(reproducible-builds.org 사양). gcc(`__DATE__`), tar, gzip 계열 도구, 커널 빌드(`KBUILD_BUILD_TIMESTAMP`와 함께) 등이 존중한다.

### 8.2 직접 확인

같은 내용의 "rootfs"를 2초 간격으로 두 번 묶는다. 한 번은 그냥, 한 번은 SOURCE_DATE_EPOCH를 지켜서.

```sh
#!/bin/bash
# K5: why SOURCE_DATE_EPOCH matters -- pack the same "rootfs" twice, 2 s apart
set -euo pipefail
W=/var/tmp/K5/k5_repro; rm -rf "$W"; mkdir -p "$W/rootfs/etc"; cd "$W"
echo "k5os 1.4.0" > rootfs/etc/k5-release
pack_naive() { tar -C rootfs -cf - . | gzip > "$1"; }
pack_repro() {
  find rootfs -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +      # clamp mtimes
  tar -C rootfs --sort=name --mtime="@$SOURCE_DATE_EPOCH" --owner=0 --group=0 \
      --numeric-owner --pax-option=exthdr.name=%d/PaxHeaders/%f,delete=atime,delete=ctime \
      -cf - . | gzip -n > "$1"                                  # -n: no name/time in gzip header
}
pack_naive a.tgz; sleep 2; touch rootfs/etc/k5-release; pack_naive b.tgz
export SOURCE_DATE_EPOCH=$(git -C /var/tmp/K5/k5-platform log -1 --format=%ct)
echo "SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH ($(date -u -d @$SOURCE_DATE_EPOCH +%F' '%T) UTC, last commit)"
pack_repro c.tgz; sleep 2; touch rootfs/etc/k5-release; pack_repro d.tgz
sha256sum a.tgz b.tgz c.tgz d.tgz | cut -c1-16,65-
```

```text
SOURCE_DATE_EPOCH=1792284343 (2026-10-18 00:45:43 UTC, last commit)
8e2d282d0d926b05  a.tgz
379b03bd5777625e  b.tgz
9934ffc076c9cb42  c.tgz
9934ffc076c9cb42  d.tgz
```

출력에서 볼 것: 그냥 묶은 a/b는 내용이 같은데도 해시가 다르다(mtime·gzip 헤더 시간). SOURCE_DATE_EPOCH(마지막 git 커밋 시각)로 시간을 고정하고 정렬·소유자까지 고정한 c/d는 **비트 단위로 같다.** a/b의 해시는 실행할 때마다 달라지지만, c/d의 `9934ffc0…`는 이 스크립트를 몇십 분 간격으로 두 번 돌렸을 때 두 번 다 같았다 — 입력(파일 내용 + 커밋 시각)이 같으면 언제 돌려도 같은 출력이라는 뜻이다. Yocto의 이미지·패키지 클래스가 하는 일이 이 규칙을 모든 레시피에 적용하는 것이다.

### 8.3 Yocto는 SOURCE_DATE_EPOCH를 어떻게 정하나

```sh
cd /var/tmp/K5/k5_oe-core/meta
grep -n 'SOURCE_DATE_EPOCH\b.*?=\|SOURCE_DATE_EPOCH_FALLBACK\|REPRODUCIBLE_TIMESTAMP_ROOTFS' conf/bitbake.conf
sed -n 152,157p lib/oe/reproducible.py
```

```text
687:export SOURCE_DATE_EPOCH ?= "${@get_source_date_epoch_value(d)}"
689:SOURCE_DATE_EPOCH_FALLBACK ??= "1302044400"
690:REPRODUCIBLE_TIMESTAMP_ROOTFS ??= "1520598896"
152:def get_source_date_epoch(d, sourcedir):
153:    return (
154:        get_source_date_epoch_from_git(d, sourcedir) or
155:        get_source_date_epoch_from_youngest_file(d, sourcedir) or
156:        fixed_source_date_epoch(d)       # Last resort
157:    )
```

출력에서 볼 것: 레시피마다 **자기 소스에서** 값을 뽑는다 — git이면 마지막 커밋 시각, tarball이면 가장 새 파일의 mtime(그 전에 NEWS/ChangeLog 같은 "알려진 파일"도 본다), 아무것도 없으면 고정값. `reproducible.py` 머리 주석에 5단계 규칙이 적혀 있고, 정해진 값은 레시피별 `__source_date_epoch.txt`(SDE_FILE)에 저장돼 sstate와 함께 다닌다. rootfs 파일 시간은 `REPRODUCIBLE_TIMESTAMP_ROOTFS`로 고정된다. 예전에 있던 `reproducible_build.bbclass`는 이제 별도 클래스가 아니다 — 이 동작이 `bitbake.conf`와 `lib/oe/reproducible.py`에 **기본으로** 들어가 있다 (wrynose의 `classes*/`에 그 파일이 없음을 확인했다).

회사 layer가 재현성을 깨는 흔한 방법:

- 레시피·스크립트에서 `date`, `$(hostname)`, `${DATETIME}`을 결과물에 박는다 → 빌드 정보가 필요하면 `image-buildinfo` 클래스(layer 커밋 기록)나 SOURCE_DATE_EPOCH 기반 값을 쓴다.
- 사내 빌드 스크립트가 절대 경로(`/home/jenkins/...`)를 바이너리에 넣는다 → `-ffile-prefix-map`(oe-core가 기본으로 넣는 `DEBUG_PREFIX_MAP`)을 우회하지 않는다.
- 검증: Yocto의 `oe-selftest -r reproducible`가 같은 빌드를 두 경로에서 돌려 패키지를 비교한다 (빌드가 필요하므로 이 노트에서는 실행하지 않음).

### 8.4 buildhistory와 SBOM — 출하물의 영수증

- **buildhistory** (`INHERIT += "buildhistory"`, 우리 `k5os.conf`에 넣었다): 이미지마다 패키지 목록·크기·파일 목록·의존성을 git 저장소에 커밋한다. "어제 이미지와 오늘 이미지 차이"가 `git diff`가 된다 (`buildhistory-diff` 스크립트).
- **SBOM**: oe-core `defaultsetup.conf`의 `INHERIT_DISTRO`에 이미 `create-spdx`가 들어 있고, `create-spdx.bbclass`는 이제 한 줄 `inherit create-spdx-3.0`이다 — 즉 **기본으로 SPDX 3.0 SBOM을 만든다**. 라이선스 의무(GPL 소스 제공, `archiver` 클래스), SBOM의 내용과 CVE 점검(`sbom-cve-check`)은 K7에서 다룬다.

```sh
grep -n '^INHERIT_DISTRO' /var/tmp/K5/k5_oe-core/meta/conf/distro/defaultsetup.conf
grep -v '^#' /var/tmp/K5/k5_oe-core/meta/classes/create-spdx.bbclass | grep -v '^$'
```

```text
17:INHERIT_DISTRO ?= "debian devshell sstate license remove-libtool create-spdx buildstats uninative"
inherit create-spdx-3.0
```

---

## 9. SDK와 eSDK — Yocto 밖에서 개발하기

### 9.1 두 종류

| | 표준 SDK (`populate_sdk`) | 확장 SDK, eSDK (`populate_sdk_ext`) |
|---|---|---|
| 내용 | 크로스 툴체인 + 타깃 sysroot(헤더·라이브러리) + `environment-setup-*` 스크립트 | 위 + **devtool과 BitBake 일부 + sstate** |
| 대상 | 앱 개발자, 펌웨어 팀 밖 사람 | 레시피까지 만지는 개발자, Yocto 빌드 머신이 없는 사람 |
| 할 수 있는 것 | `$CC`로 앱·모듈 빌드 | `devtool add/modify/build/deploy-target`, `devtool sdk-update` |
| 크기 | 작다 | 크다 (`SDK_EXT_TYPE = "minimal"`이면 작게 받고 필요할 때 sstate를 받음) |

클래스 기본값:

```sh
cd /var/tmp/K5/k5_oe-core/meta/classes-recipe
grep -nE '^SDK_EXT_TYPE|^SDK_INCLUDE_TOOLCHAIN|^SDK_UPDATE_URL' populate_sdk_ext.bbclass
grep -nE '^SDK_DEPLOY|^TOOLCHAIN_OUTPUTNAME|^SDK_ARCHIVE_TYPE' populate_sdk_base.bbclass
```

```text
22:SDK_EXT_TYPE ?= "full"
24:SDK_INCLUDE_TOOLCHAIN ?= "${@'1' if d.getVar('SDK_EXT_TYPE') == 'full' else '0'}"
45:SDK_UPDATE_URL ?= ""
52:SDK_DEPLOY = "${DEPLOY_DIR}/sdk"
86:TOOLCHAIN_OUTPUTNAME ?= "${SDK_NAME}-toolchain-${SDK_VERSION}"
89:SDK_ARCHIVE_TYPE ?= "tar.xz"
```

### 9.2 environment-setup 스크립트가 하는 일

SDK를 설치하면 `environment-setup-<target>` 스크립트가 생긴다. 무엇을 export하는지는 `toolchain-scripts.bbclass`에 그대로 있다.

```sh
grep -n "echo 'export \(CC\|CXX\|SDKTARGETSYSROOT\|PKG_CONFIG_SYSROOT_DIR\|CONFIGURE_FLAGS\|CFLAGS\|LDFLAGS\|KCFLAGS\|ARCH\|CROSS_COMPILE\)=" \
  /var/tmp/K5/k5_oe-core/meta/classes-recipe/toolchain-scripts.bbclass
```

```text
58:	echo 'export SDKTARGETSYSROOT='"$sysroot" >> $script
64:	echo 'export PKG_CONFIG_SYSROOT_DIR=$SDKTARGETSYSROOT' >> $script
97:	echo 'export PKG_CONFIG_SYSROOT_DIR=$standalone_sysroot_target' >> $script
100:	echo 'export SDKTARGETSYSROOT=$standalone_sysroot_target' >> $script
135:	echo 'export CC="${TARGET_PREFIX}gcc ${TARGET_CC_ARCH} --sysroot=$SDKTARGETSYSROOT"' >> $script
136:	echo 'export CXX="${TARGET_PREFIX}g++ ${TARGET_CC_ARCH} --sysroot=$SDKTARGETSYSROOT"' >> $script
150:	echo 'export CONFIGURE_FLAGS="--target=${TARGET_SYS} --host=${TARGET_SYS} --build=${SDK_ARCH}-linux --with-libtool-sysroot=$SDKTARGETSYSROOT"' >> $script
151:	echo 'export CFLAGS="${TARGET_CFLAGS}"' >> $script
153:	echo 'export LDFLAGS="${TARGET_LDFLAGS}"' >> $script
155:	echo 'export KCFLAGS="--sysroot=$SDKTARGETSYSROOT"' >> $script
158:	echo 'export ARCH=${ARCH}' >> $script
159:	echo 'export CROSS_COMPILE=${TARGET_PREFIX}' >> $script
```

출력에서 볼 것: **`CC` 자체에 `--sysroot`와 CPU 튜닝 플래그(`TARGET_CC_ARCH`, 예: `-mcpu=cortex-a53+crypto`)가 들어 있다.** 그래서 Makefile에서 `CC = gcc`로 덮어쓰거나 `$(CC)` 대신 `aarch64-poky-linux-gcc`를 직접 부르면 sysroot를 잃고 호스트 헤더를 집는다 — SDK 사용자의 1번 실수다. `ARCH`·`CROSS_COMPILE`·`KCFLAGS`가 있는 이유는 커널 모듈 빌드용이다.

> **실행하지 않음 — 가이드**: SDK 만들기와 쓰기

```sh
# 실행하지 않음 — 가이드  (빌드 머신)
bitbake k5-sensorkit-image -c populate_sdk        # → tmp/deploy/sdk/<...>-toolchain-<ver>.sh
bitbake k5-sensorkit-image -c populate_sdk_ext    # eSDK
# 커널 모듈도 SDK로 빌드하려면 이미지 레시피나 local.conf에:
#   TOOLCHAIN_TARGET_TASK:append = " kernel-devsrc"

# 실행하지 않음 — 가이드  (개발자 PC)
sh k5os-glibc-x86_64-k5-sensorkit-image-cortexa53-crypto-k5-sensorkit-toolchain-1.4.0.sh -d ~/sdk/k5 -y
. ~/sdk/k5/environment-setup-cortexa53-crypto-poky-linux   # 파일 이름은 DISTRO/TUNE에 따라 다르다
echo $CC                                                    # --sysroot=... 가 붙어 있는지 확인
$CC -O2 -Wall -o imu_reader imu_reader.c                    # 앱
cmake -S . -B build && cmake --build build                  # CMake는 OE 툴체인 파일을 환경에서 잡는다
# out-of-tree 모듈: SDK sysroot의 커널 소스에서 한 번 준비
cd $SDKTARGETSYSROOT/usr/src/kernel && make modules_prepare
make -C $SDKTARGETSYSROOT/usr/src/kernel M=$HOME/k5_imu_mod modules
```

eSDK에서는 같은 PC에서 `devtool modify k5-sensord` → `devtool build` → `devtool deploy-target`이 그대로 된다. 회사가 eSDK를 배포하면 `SDK_UPDATE_URL`을 사내 서버로 두고 `devtool sdk-update`로 갱신한다.

---

## 10. 커널 안에서는 무슨 일이 일어나나 — 빌드 산출물이 부팅에서 만나는 곳

Yocto는 빌드 도구지만, 이 노트에서 만든 산출물 셋(DTB, `.config`, `.ko` 패키지)은 각각 커널의 특정 경로로 들어간다. 드라이버 엔지니어가 "Yocto에서 넣은 게 왜 안 보이지"를 추적할 때 이 지도가 필요하다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14" font-weight="bold">Yocto 산출물 → 부팅된 커널 안의 경로</text>
  <rect x="20" y="40" width="190" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="115" y="62" text-anchor="middle" font-size="12">imx8mp-k5-sensorkit.dtb</text>
  <text x="115" y="80" text-anchor="middle" font-size="12">(DT 패치)</text>
  <rect x="245" y="40" width="190" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="62" text-anchor="middle" font-size="12">.config (fragment 반영)</text>
  <text x="340" y="80" text-anchor="middle" font-size="12">INV_ICM42600_I2C=m</text>
  <rect x="470" y="40" width="190" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="565" y="62" text-anchor="middle" font-size="12">kernel-module-* 패키지</text>
  <text x="565" y="80" text-anchor="middle" font-size="12">/lib/modules/ver/</text>
  <rect x="20" y="130" width="190" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="115" y="152" text-anchor="middle" font-size="12">unflatten_device_tree</text>
  <text x="115" y="170" text-anchor="middle" font-size="12">→ device_node 트리</text>
  <rect x="20" y="210" width="190" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="115" y="232" text-anchor="middle" font-size="12">i2c 어댑터 등록 시</text>
  <text x="115" y="250" text-anchor="middle" font-size="12">자식 노드 → i2c_client</text>
  <rect x="245" y="130" width="190" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="152" text-anchor="middle" font-size="12">=m → .ko 로 빌드</text>
  <text x="340" y="170" text-anchor="middle" font-size="12">MODULE_DEVICE_TABLE(of)</text>
  <rect x="470" y="130" width="190" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="565" y="152" text-anchor="middle" font-size="12">depmod → modules.alias</text>
  <text x="565" y="170" text-anchor="middle" font-size="12">(rootfs 생성 때)</text>
  <rect x="245" y="210" width="415" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="452" y="232" text-anchor="middle" font-size="12">uevent MODALIAS=of:N imu T (null) C invensense,icm42688</text>
  <text x="452" y="250" text-anchor="middle" font-size="12">udev → modprobe → inv-icm42600-i2c probe()</text>
  <line x1="115" y1="90" x2="115" y2="128" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="115" y1="180" x2="115" y2="208" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="340" y1="90" x2="340" y2="128" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="565" y1="90" x2="565" y2="128" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="210" y1="235" x2="243" y2="235" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="340" y1="180" x2="340" y2="208" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="565" y1="180" x2="565" y2="208" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="20" y="290" width="640" height="54" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="312" text-anchor="middle" font-size="12">보드에서 확인: /proc/device-tree/.../imu@68 · zcat /proc/config.gz · lsmod</text>
  <text x="340" y="332" text-anchor="middle" font-size="12">/sys/bus/i2c/devices/2-0068/driver · /sys/bus/iio/devices/iio:device0/name</text>
</svg>
```
그림 6 — 이 노트의 세 산출물이 부팅한 커널 안에서 만나는 지점. 셋 중 하나라도 빠지면 IMU가 안 보인다: DTB가 아니면 `i2c_client`가 안 생기고, `.config`가 아니면 드라이버가 없고, 모듈 패키지가 아니면 rootfs에 `.ko`가 없다.

연결되는 커널 구조와 노트:

- DTB → `unflatten_device_tree()`가 `struct device_node` 트리를 만들고, I2C 어댑터 드라이버가 등록될 때 `of_i2c_register_devices()`가 자식 노드마다 `i2c_new_client_device()`로 `struct i2c_client`를 만든다 (D4, H2).
- 드라이버의 `of_device_id` 테이블(`MODULE_DEVICE_TABLE(of, ...)`)은 `.ko` 안의 alias가 되고, rootfs 생성 시 `depmod`가 `modules.alias`로 모은다. uevent의 `MODALIAS`가 이 alias와 맞으면 udev가 `modprobe`한다 (G3, G4).
- `/proc/config.gz`(`CONFIG_IKCONFIG_PROC`)로 **보드에서 실제로 돌고 있는 커널의 설정**을 본다 — `kernel_configcheck`가 빌드 때 본 것과 같아야 한다. 랩 커널에도 `CONFIG_IKCONFIG_PROC=y`가 켜져 있다 (3.6절에서 본 `.config`).
- `MACHINE_EXTRA_RRECOMMENDS`로 넣은 패키지는 "추천"이라 이미지가 `BAD_RECOMMENDATIONS`로 뺄 수 있다. 부팅에 반드시 필요한 모듈이면 `MACHINE_ESSENTIAL_EXTRA_RDEPENDS`를 쓴다.

Don 경험과 연결: SSD 펌웨어에서 "하드웨어 설정 테이블(핀·NAND 파라미터) + 빌드 옵션(define) + 링크되는 모듈 목록"이 맞아야 기능이 살던 것과 정확히 같은 3요소다. Yocto는 이 셋을 각각 DT, Kconfig fragment, 패키지로 분리했을 뿐이다.

---

## 11. Embedded 실무에서 다시 보기

### 11.1 정적 점검 — BitBake 없이 layer 실수 잡기

BitBake가 파싱 단계에서 잡는 실수 몇 가지는 스크립트로 미리 잡을 수 있다 (사전 커밋 훅이나 CI 첫 단계로 쓸 만하다). 다음은 kas가 받은 실제 vendor layer를 기준으로 우리 layer를 검사한다.

```python
#!/usr/bin/env python3
"""K5: static sanity checks bitbake would do at parse time (no bitbake needed)."""
import glob, os, re, sys

work, plat = sys.argv[1], sys.argv[2]
layers = [os.path.join(plat, l) for l in ("meta-k5-sensorkit-bsp", "meta-k5-distro", "meta-k5-product")]
vendor = [os.path.join(work, p) for p in ("openembedded-core/meta", "meta-yocto/meta-poky", "meta-freescale")]
allayers = layers + vendor
ok = True

def report(cond, msg):
    global ok
    print(("OK   " if cond else "FAIL ") + msg)
    ok &= cond

core = open(os.path.join(work, "openembedded-core/meta/conf/layer.conf")).read()
corename = re.search(r'LAYERSERIES_CORENAMES\s*=\s*"([^"]+)"', core).group(1)
for l in layers:
    txt = open(os.path.join(l, "conf/layer.conf")).read()
    compat = re.search(r'LAYERSERIES_COMPAT_\S+\s*=\s*"([^"]+)"', txt).group(1).split()
    report(corename in compat, f"{os.path.basename(l)}: LAYERSERIES_COMPAT {compat} vs core '{corename}'")

for conf in glob.glob(os.path.join(plat, "*/conf/*/*.conf")):
    for req in re.findall(r'^(?:require|include)\s+(\S+)', open(conf).read(), re.M):
        hit = [a for a in allayers if os.path.exists(os.path.join(a, req))]
        report(bool(hit), f"{os.path.basename(conf)}: require {req} -> {hit[0].replace(work + '/', '') if hit else 'NOT FOUND'}")

recipes = [os.path.basename(p) for a in allayers for p in glob.glob(os.path.join(a, "recipes-*/*/*.bb"))]
for app in glob.glob(os.path.join(plat, "*/recipes-*/*/*.bbappend")):
    name = os.path.basename(app)[:-len(".bbappend")]
    pat = re.compile("^" + re.escape(name).replace("%", ".*") + r"\.bb$")
    m = [r for r in recipes if pat.match(r)]
    report(len(m) > 0, f"{os.path.basename(app)} -> {m if m else 'NO RECIPE (bitbake: dangling bbappend error)'}")
    files = os.path.join(os.path.dirname(app), "files")
    for f in re.findall(r'file://([^\s;"]+)', open(app).read()):
        report(os.path.exists(os.path.join(files, f)), f"  SRC_URI file://{f}")
sys.exit(0 if ok else 1)
```

```sh
python3 -I ~/.elinux_scratch/K5/k5_lint.py /var/tmp/K5/k5_kaswork /var/tmp/K5/k5-platform; echo "exit=$?"
```

```text
OK   meta-k5-sensorkit-bsp: LAYERSERIES_COMPAT ['wrynose'] vs core 'wrynose'
OK   meta-k5-distro: LAYERSERIES_COMPAT ['wrynose'] vs core 'wrynose'
OK   meta-k5-product: LAYERSERIES_COMPAT ['wrynose'] vs core 'wrynose'
OK   k5-sensorkit.conf: require conf/machine/include/imx8mp-evk.inc -> meta-freescale
OK   k5os.conf: require conf/distro/poky.conf -> meta-yocto/meta-poky
OK   u-boot-imx_%.bbappend -> ['u-boot-imx_2025.04.bb']
OK     SRC_URI file://k5-uboot.cfg
OK   linux-fslc-imx_%.bbappend -> ['linux-fslc-imx_6.18.bb']
OK     SRC_URI file://0001-arm64-dts-imx8mp-add-K5-SensorKit-board-with-ICM-426.patch
OK     SRC_URI file://k5-sensorkit.cfg
```

출력에서 볼 것: 우리 layer의 호환 선언, `require` 경로, bbappend 대상 레시피, `SRC_URI` 파일이 실제 vendor checkout과 맞는다. 이 스크립트가 잡는 대표 사고가 **"dangling bbappend"** — 벤더가 `u-boot-imx_2025.04.bb`를 `_2026.04.bb`로 올렸는데 우리 bbappend가 버전을 박아 두었다면(`u-boot-imx_2025.04.bbappend`) BitBake는 대상 없는 bbappend를 에러로 처리한다. 이것은 정적 점검일 뿐이고, 변수 값·override 해석·태스크 의존성까지는 BitBake 파싱만이 확인한다.

### 11.2 장면 1 — "벤더가 새 BSP를 냈다"

NXP가 새 릴리스를 내면 할 일: (1) kas 파일에서 meta-freescale 브랜치/커밋 갱신 → `kas lock`; (2) 우리 커널 패치가 새 `SRCREV`에 적용되나 — 3.5절처럼 실패하면 `devtool modify` → `git rebase` → `devtool finish`; (3) `kernel_configcheck`로 fragment가 아직 유효한지(심볼 이름 변경 감지); (4) `bitbake-diffsigs`로 예상보다 큰 재빌드 범위 확인; (5) buildhistory diff로 이미지 패키지 변화 리뷰. 이 다섯 줄이 "BSP 업그레이드 PR"의 체크리스트다.

### 11.3 장면 2 — "CI는 되는데 내 PC에서만 실패"

거의 항상 셋 중 하나다: 다른 커밋(lock 파일 안 씀), 다른 `local.conf`(손으로 넣은 줄), 다른 호스트(배포판·Python 버전). 해법도 셋: `kas lock` 커밋, `local_conf_header`에만 설정, `kas-container`. 호스트 차이로 인한 sstate 불일치는 `uninative`(호스트 glibc 차이를 흡수하는 oe-core 기본 클래스, `INHERIT_DISTRO`에 들어 있음)가 상당 부분 막아 준다.

### 11.4 장면 3 — 입사 첫 주 체크리스트

- [ ] 저장소 받기: kas 파일 또는 repo 매니페스트가 어디 있나, lock 파일이 있나
- [ ] `bitbake-layers show-layers`로 layer 목록·우선순위 그림 1을 우리 회사 버전으로 그려 보기
- [ ] `MACHINE`·`DISTRO`·이미지 이름 확인, machine conf → include → `PREFERRED_PROVIDER_virtual/kernel` 따라가기
- [ ] 커널 레시피가 linux-yocto 계열인지 vendor 계열인지, `KBUILD_DEFCONFIG`와 회사 `.cfg`/`.scc` 위치
- [ ] 회사 커널 패치 목록과 각 `Upstream-Status`
- [ ] CI 설정: `DL_DIR`, `SSTATE_MIRRORS`, `BB_HASHSERVE`, `PRSERV_HOST`가 어디를 가리키나, 쓰기 권한은 누구에게 있나
- [ ] 한 번 풀 빌드 + 한 번 sstate 재빌드 시간 측정 (팀 위키에 남기기)
- [ ] `devtool modify virtual/kernel` → 한 줄 바꿔 `devtool build` → `deploy-target`까지 손에 익히기
- [ ] SDK가 배포되는지, `environment-setup`으로 앱 하나 빌드해 보기
- [ ] 이미지가 보드에 어떻게 올라가나(uuu, SD 카드 `bmaptool`, OTA)와 버전 문자열 확인 방법
- [ ] buildhistory·SBOM 출력 위치와 릴리스 절차(K7) 확인

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 패치를 mainline/다른 버전 기준으로 만듦 | `do_patch` 실패 "Hunk FAILED" | 레시피 `SRCREV`와 다른 소스 | `devtool modify`로 SRCREV 위에서 커밋 → `finish` (3.5절) |
| `menuconfig`만 하고 끝냄 | 다음 빌드에서 설정이 사라짐 | `.config`는 레시피 입력이 아님 | `-c diffconfig`로 fragment 추출 → layer에 추가 |
| fragment에 `=y` 썼는데 `m`이 됨 | 초기 부팅에 드라이버 없음, 에러 없음 | 부모 심볼이 `m` (tristate 규칙) | 부모도 fragment에 `=y`, `KMETA_AUDIT_WERROR = "1"` |
| bbappend를 엉뚱한 레시피에 붙임 | 패치·설정이 전혀 안 먹음 | `PREFERRED_PROVIDER`가 다른 레시피 선택 | `bitbake-getvar -r virtual/kernel`, `bitbake-layers show-appends` |
| bbappend 파일명에 버전을 박음 | 벤더 업그레이드 후 "No recipes available for … bbappend" | dangling bbappend | `_%` 와일드카드, 업그레이드 시 패치 재검증 |
| vendor layer를 직접 수정 | 벤더 업데이트 때 충돌·변경 유실 | 남의 저장소 | 회사 layer의 bbappend/override로 옮김 |
| `SRCREV = "${AUTOREV}"`를 릴리스에 남김 | 같은 태그가 매번 다른 이미지, 네트워크 의존 | 항상 최신 커밋 | 해시 고정, `devtool update-recipe -m srcrev` |
| 결과물에 `date`·호스트명 삽입 | 매 빌드 재빌드, hash equivalence 무효, 재현 불가 | 비결정적 출력 | SOURCE_DATE_EPOCH 사용, `bitbake-diffsigs`로 원인 변수 찾기 |
| 공유 sstate만 쓰고 hashserv는 로컬 | 미러가 있는데도 HIT가 적음 | 동등성 정보가 공유 안 됨 | 같은 `BB_HASHSERVE`(또는 `BB_HASHSERVE_UPSTREAM`) 사용 |
| SDK에서 `gcc`를 직접 호출 | 호스트 헤더/라이브러리를 집어 링크 오류 또는 잘못된 ABI | `$CC`의 `--sysroot`·튜닝 플래그 누락 | 항상 `$CC`/`$CXX`, CMake는 SDK 환경에서 |

---

## 13. 면접에서 이렇게 말한다

**Q.** How is a typical Yocto project for a product organized?

**A.** 남의 코드와 우리 코드를 layer로 나눈다. 아래부터 bitbake·openembedded-core, 참조 distro, vendor BSP layer(커밋 고정, 수정 금지), 그 위에 회사 BSP layer(machine conf, DT·커널 패치, bbappend), distro layer(정책), product layer(이미지). 전체를 kas나 repo 매니페스트로 묶고 모든 외부 저장소를 커밋 해시로 고정한다. 그래서 아무 릴리스나 다시 똑같이 빌드할 수 있다.

> We split it by ownership. At the bottom we have bitbake and openembedded-core, then the vendor BSP layer like meta-freescale, pinned to a commit and never edited. On top of that we keep our own BSP layer with the machine config, device-tree and kernel patches as bbappends, a distro layer for policy, and a product layer with the image recipes. A kas file with a lock file pins every repository to a hash, so any release can be rebuilt exactly.

**꼬리 질문**: "Why not just fork the vendor layer?" → 벤더 업데이트를 받을 때마다 머지 지옥이 되고, 무엇을 바꿨는지 리뷰할 수 없다. bbappend는 변경이 작고 명시적이다.

**Q.** How do you add a device-tree change or a driver patch to the kernel in Yocto?

**A.** `devtool modify virtual/kernel`로 레시피가 받는 바로 그 SRCREV를 git 트리로 꺼내고, 고치고 커밋하고 `devtool build`/`deploy-target`으로 보드에서 확인한 뒤 `devtool finish`로 회사 BSP layer에 패치와 bbappend를 만든다. 결과는 `FILESEXTRAPATHS:prepend`와 `SRC_URI:append:<machine>`에 `0001-….patch`가 들어간 bbappend다. 새 보드는 EVK DTS를 고치지 않고 그것을 include하는 새 DTS를 추가한다.

> I run devtool modify on the kernel recipe, which gives me a git tree at exactly the recipe's SRCREV. I commit the change, test with devtool build and deploy-target, then devtool finish writes a format-patch file and a bbappend into our BSP layer, scoped with a machine override. Patching against the real SRCREV matters — a patch made on mainline often fails do_patch on a vendor tree.

**Q.** defconfig or config fragments — how do you manage kernel configuration?

**A.** vendor의 `KBUILD_DEFCONFIG`를 기본으로 두고 우리 변경은 작은 `.cfg` fragment로 둔다. kernel-yocto가 `merge_config.sh`로 합치고, `do_kernel_configcheck`가 요청한 값이 실제 `.config`에 살아남았는지 검사한다. fragment는 `menuconfig` 후 `diffconfig`로 뽑는다. 부모 심볼이 `m`이면 `=y`가 조용히 `m`이 되는 것 같은 실수를 `KMETA_AUDIT_WERROR`로 빌드 실패로 만든다.

> Vendor defconfig as the base, our deltas as small fragments. kernel-yocto merges them with merge_config.sh and kernel_configcheck reports any requested value that didn't survive Kconfig — for example a =y under a parent that's =m gets silently demoted. I generate fragments with menuconfig plus diffconfig, and I set KMETA_AUDIT_WERROR so CI fails on mismatches.

**Q.** What is sstate and what does hash equivalence add?

**A.** sstate는 태스크 출력의 캐시로, 키는 입력 해시(코드·변수·파일·의존 태스크 해시)다. 같은 해시가 있으면 태스크를 돌리지 않고 결과를 푼다. 문제는 출력이 안 바뀌는 변경(주석)도 하위 전체의 해시를 바꾼다는 것. hash equivalence는 태스크를 돌린 뒤 출력 해시를 서버에 보고해 같은 출력을 낸 입력 해시들을 하나의 unihash로 묶고, 하위 태스크는 unihash를 쓰므로 연쇄 재빌드가 끊긴다. 출력이 재현 가능해야 동작한다.

> sstate caches each task's output keyed by a hash of its inputs. Hash equivalence adds an output hash: after a task runs, bitbake reports the output hash to a hash server, and if it matches an earlier build the new input hash is mapped to the same unified hash. Downstream tasks are keyed on that unihash, so a comment-only change stops the rebuild cascade. It only works if outputs are reproducible.

**꼬리 질문**: "Your build rebuilds everything after a small change — how do you debug it?" → `bitbake-diffsigs -t <recipe> <task>`로 두 서명의 차이(변수·파일·의존)를 보고, 매번 바뀌는 값(날짜, AUTOREV, 호스트 경로)을 찾는다.

**Q.** How do you make Yocto builds reproducible?

**A.** 세 층이다. 소스 — 모든 layer와 레시피를 커밋 해시로 고정(kas lock, SRCREV, AUTOREV 금지), 소스 미러로 네트워크 의존 제거. 빌드 환경 — 컨테이너(kas-container)와 uninative. 출력 — oe-core가 레시피마다 SOURCE_DATE_EPOCH를 소스(git 커밋 시각)에서 정해 모든 도구가 현재 시각 대신 쓰게 하고, 경로 prefix map을 넣는다. 검증은 `oe-selftest -r reproducible`, 출하물 기록은 buildhistory와 SPDX SBOM.

> Pin the inputs — kas lock file, fixed SRCREVs, no AUTOREV, and source mirrors so we don't depend on GitHub. Pin the environment with kas-container. And make outputs deterministic: OE derives SOURCE_DATE_EPOCH per recipe from the source, usually the last git commit time, and tools use it instead of the wall clock. We verify with the reproducible selftest and keep buildhistory and SPDX SBOMs per release.

**Q.** What's the difference between the standard SDK and the eSDK, and how would an app developer use it?

**A.** 표준 SDK는 크로스 툴체인 + 타깃 sysroot + environment-setup 스크립트다. 그것을 source하면 `CC`에 `--sysroot`와 CPU 튜닝이 들어가므로 `$CC`나 CMake로 앱을 빌드한다. eSDK는 여기에 devtool과 sstate까지 들어 있어 레시피 추가·수정·보드 배포까지 Yocto 빌드 머신 없이 할 수 있다. 커널 모듈까지 하려면 `kernel-devsrc`를 SDK에 넣는다.

> The standard SDK is a cross toolchain plus target sysroot plus an environment-setup script; after sourcing it, CC already carries --sysroot and the tuning flags, so you just use $CC or CMake. The extensible SDK adds devtool and sstate, so a developer can add or modify recipes and deploy to a board without a full build machine. For out-of-tree modules you add kernel-devsrc to the SDK.

**Q.** Do you need a PR server?

**A.** 패키지 피드로 현장 장비를 업데이트한다면 필요하다. 레시피 출력이 바뀌어도 사람이 PR을 안 올리면 버전이 같아 패키지 매니저가 업데이트를 안 한다. PR server가 출력 해시 변화마다 revision을 자동으로 올려 버전을 단조 증가시킨다. 이미지 전체를 A/B로 교체하는 제품이면 우선순위가 낮다. 쓴다면 DB를 export해서 백업한다.

> If devices update from a package feed, yes — otherwise a changed package can keep the same version and never get installed. The PR server bumps the revision automatically whenever the output changes, so versions are monotonic across CI builds. With full-image A/B updates it matters less. Either way, back up its database.

---

## 14. 직접 해보기

1. **(개념)** 그림 1을 Don이 지원하는 회사(예: Jetson 기반이면 meta-tegra) 기준으로 다시 그려라. vendor 커널 레시피 이름과 `PREFERRED_PROVIDER_virtual/kernel`을 정하는 파일을 실제로 찾아 적는다.
정답/힌트: `git clone --depth 1 -b wrynose https://github.com/OE4T/meta-tegra`, `grep -rn 'PREFERRED_PROVIDER_virtual/kernel' conf/`.

2. **(코드)** 3.6절 스크립트에 `CONFIG_SPI_IMX=y`를 추가해 돌려 보라. 랩 `.config`에서 `SPI_IMX=m`이다. `=y`가 살아남는가? 살아남지 않는다면 이유는 무엇이고, 무엇을 함께 바꿔야 하나?
정답/힌트: `SPI_IMX`의 `depends on`과 `CONFIG_SPI`의 값을 Kconfig에서 확인한다. 부모가 `y`면 자식은 `y`가 될 수 있다 — 결과를 직접 보고 판단한다.

3. **(코드)** 3.4절 DT에 `pinctrl-0`로 IMU 인터럽트 핀의 pad 설정(`MX8MP_IOMUXC_GPIO1_IO05__GPIO1_IO05`)을 추가한 v3 패치를 만들고, `cpp + dtc`로 컴파일되는지 확인하라. 매크로가 들어 있는 헤더 위치를 찾아라.
정답/힌트: `include/dt-bindings/pinctrl/` 아래가 아니라 `arch/arm64/boot/dts/freescale/imx8mp-pinfunc.h`다. EVK DTS의 `pinctrl_i2c3: i2c3grp` 블록을 본뜬다.

4. **(관찰)** 7.2절 장난감 모델에서 `cc()`가 출력에 빌드 시각을 넣도록 바꿔라(`time.time()`). build 2에서 무슨 일이 생기나? 실제 Yocto에서 이 상황을 어떤 명령으로 찾나?
정답/힌트: outhash가 매번 달라져 EQUIVALENT가 사라지고 imu-app도 매번 다시 빌드된다. `bitbake-diffsigs`는 입력 차이를 보여 주고, 출력 비결정성은 reproducible selftest/`diffoscope`로 찾는다.

5. **(코드)** 8.2절 스크립트의 `pack_repro`에서 `--sort=name`만 빼고 파일을 두 개 더 만든 다음 다시 비교하라. 해시가 같은가? 파일시스템에 따라 결과가 달라질 수 있는 이유는?
정답/힌트: tar는 `readdir` 순서를 따른다. ext4 hash 순서는 디렉터리마다 다르고 tmpfs/다른 FS와 다르다 — "내 PC에서는 같았다"는 증명이 아니다.

6. **(설계)** k5-sensorkit에 두 번째 보드 리비전(rev B: IMU가 SPI로 이동)이 생겼다. machine conf를 하나 더 만들까, DT overlay로 처리할까? 각 방법에서 바뀌는 파일 목록을 적어라.
정답/힌트: `k5-sensorkit-revb.conf`(`require k5-sensorkit.conf` 후 `KERNEL_DEVICETREE`만 교체) vs `KERNEL_DEVICETREE`에 `.dtbo` 추가 + U-Boot에서 리비전 EEPROM을 읽어 overlay 적용(D7). 양산 관리·테스트 매트릭스 관점에서 비교한다.

7. **(관찰)** `kas lock`을 한 뒤 kas 파일에서 meta-freescale의 `branch:`를 `scarthgap`으로 바꾸고 `kas dump`를 돌려 보라. lock 파일이 있으면 무슨 일이 생기나?
정답/힌트: lock의 `commit:`이 그대로 적용되므로 브랜치와 커밋이 어긋난다. kas가 경고/에러를 내는지 직접 확인하고, 업그레이드는 "브랜치 변경 + `kas lock` 갱신"을 한 PR로 한다는 규칙을 정리한다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| BSP layer | Board Support Package layer | machine conf, 커널·부트로더 레시피/bbappend, 펌웨어를 담는 layer |
| machine conf | `conf/machine/<MACHINE>.conf` | 보드 하나의 하드웨어 정의 — 튜닝, DTB, 부트로더, 콘솔 |
| `virtual/kernel` | 가상 provider 이름 | 실제 커널 레시피는 `PREFERRED_PROVIDER_virtual/kernel`이 고른다 |
| bbappend | 레시피 덧붙이기 파일 | 남의 레시피를 수정 없이 확장. `_%`는 버전 와일드카드 |
| `FILESEXTRAPATHS` | 파일 검색 경로 추가 | bbappend 옆의 `files/`를 `file://` 검색 경로에 넣는다 |
| `Upstream-Status` | 패치 헤더 태그 | 이 패치가 upstream에 어떤 상태인지 (Pending, Backport, Inappropriate …) |
| kernel-yocto | oe-core 커널 클래스 | kernel-cache 메타데이터, `.cfg` 병합, configcheck 제공 |
| `.cfg` fragment | Kconfig 조각 | `CONFIG_X=y` 몇 줄. `merge_config.sh`로 합쳐진다 |
| `.scc` | Series Configuration Control | `.cfg`와 패치를 기능 단위로 묶은 kernel-cache 파일 |
| `KERNEL_FEATURES` | 커널 기능 목록 변수 | 적용할 `.scc` 목록 |
| `kernel_configcheck` | 커널 설정 감사 태스크 | 요청값과 최종 `.config` 비교 (mismatch/invalid/redefinition) |
| `diffconfig` | cml1 태스크 | menuconfig 전후 `.config` 차이를 `fragment.cfg`로 저장 |
| devtool | 개발자 워크플로 도구 | 레시피 소스를 workspace로 꺼내 수정·빌드·배포·패치화 |
| externalsrc | oe 클래스 | fetch/unpack/patch 없이 지정 폴더를 소스로 사용 (devtool의 핵심) |
| `inherit module` | out-of-tree 모듈 클래스 | `make -C KERNEL_SRC M=` 빌드와 `kernel-module-*` 패키징 |
| kas | 빌드 설정 도구 | YAML로 저장소·layer·local.conf를 정의, `kas lock`으로 커밋 고정 |
| repo | Google 매니페스트 도구 | XML 매니페스트로 여러 git 저장소 동기화 |
| bitbake-setup | Yocto 6.0의 공식 setup 도구 | JSON 레지스트리로 bitbake·oe-core·meta-yocto를 받는다 |
| sstate | shared state cache | 태스크 출력 캐시. 키는 unihash |
| taskhash | 입력 해시 | 태스크 코드·변수·파일·의존 해시로 계산 |
| outhash | 출력 해시 | 태스크 출력 파일의 메타데이터·내용 해시 (`OEOuthashBasic`) |
| unihash | 통합 해시 | 동등한 출력을 낸 taskhash들을 대표하는 해시 |
| hashserv | hash equivalence 서버 | taskhash → unihash 매핑 저장 (`BB_HASHSERVE`) |
| PR server | 패키지 revision 서버 | 출력이 바뀔 때마다 PR을 자동 증가 (`PRSERV_HOST`) |
| `SOURCE_DATE_EPOCH` | 재현성 표준 시각 | 도구들이 현재 시각 대신 쓰는 epoch 초 |
| buildhistory | 빌드 기록 클래스 | 이미지·패키지 변화를 git에 커밋 |
| SPDX SBOM | 소프트웨어 자재 명세 | oe-core가 기본으로 생성 (create-spdx-3.0), K7 |
| 표준 SDK / eSDK | 배포용 개발 키트 | 툴체인+sysroot / 여기에 devtool·sstate 추가 |
| environment-setup | SDK 환경 스크립트 | `CC`, `SDKTARGETSYSROOT`, `ARCH`, `CROSS_COMPILE` 등을 export |

---

## 16. 요약 & 체크리스트

회사 Yocto는 **소유권별 layer 스택 + 커밋 고정**이다. vendor BSP는 건드리지 않고, 회사 BSP layer에서 machine conf(EVK에서 파생), DT·드라이버 패치(레시피 SRCREV 기준, `Upstream-Status` 포함), Kconfig fragment(`kernel_configcheck`로 검증)를 bbappend로 얹는다. 일상 개발은 `devtool modify → build → deploy-target → finish` 루프이고, out-of-tree 드라이버는 `inherit module` 레시피로 패키지(`kernel-module-*`)가 된다. U-Boot도 같은 bbappend + `.cfg` 패턴이다. 빌드 속도는 sstate(입력 해시 캐시)와 hash equivalence(출력 해시로 연쇄 재빌드 차단)에서 나오고, 이를 CI에서 공유하려면 `DL_DIR`/`SSTATE_MIRRORS`/`BB_HASHSERVE`/`PRSERV_HOST`를 함께 설계한다. hash equivalence와 감사 가능성은 재현성에 기대고, 재현성은 kas lock·SRCREV 고정·SOURCE_DATE_EPOCH로 만든다. 출하물의 영수증은 buildhistory와 SPDX SBOM(K7)이다. 이 노트에서 BitBake는 돌리지 않았지만, 그 아래에서 BitBake가 부르는 도구들(`git format-patch`, `patch`, `cpp`+`dtc`, `merge_config.sh`+`olddefconfig`, `make M=`, `kas`)은 모두 실제로 돌려 확인했다.

- [ ] 2026년 10월 기준 Yocto LTS(Wrynose 6.0, bitbake 2.18)와 poky → bitbake-setup/개별 저장소 변화를 설명할 수 있다
- [ ] 회사 저장소의 layer 스택과 각 layer의 소유자·우선순위를 그림으로 그릴 수 있다
- [ ] kas 파일을 쓰고 `kas lock`으로 커밋을 고정하는 이유를 말할 수 있다
- [ ] vendor machine conf를 include까지 따라가 `PREFERRED_PROVIDER_virtual/kernel`, `KERNEL_DEVICETREE`, `UBOOT_CONFIG`를 찾을 수 있다
- [ ] EVK에서 파생한 새 machine conf를 쓰고, include가 만든 연쇄 값(UBOOT_DTB_NAME 등)을 다룰 수 있다
- [ ] 레시피 SRCREV 기준으로 DT 패치를 `git format-patch`로 만들고 bbappend에 넣을 수 있다
- [ ] defconfig vs fragment를 비교하고, `menuconfig → diffconfig → kernel_configcheck` 흐름을 설명할 수 있다
- [ ] tristate 강등·오타 심볼 같은 fragment 실패를 찾아낼 수 있다
- [ ] devtool의 modify/build/deploy-target/finish/update-recipe 각각이 무엇을 하는지(externalsrc 포함) 설명할 수 있다
- [ ] `inherit module` 레시피를 쓰고 모듈이 `/lib/modules/<ver>/updates/`와 `kernel-module-*` 패키지로 가는 경로를 말할 수 있다
- [ ] taskhash·outhash·unihash와 sstate/hashserv/PR server의 역할을 구분해 설명할 수 있다
- [ ] SOURCE_DATE_EPOCH가 어떻게 정해지고 무엇을 고정하는지, 재현성을 깨는 흔한 원인을 말할 수 있다

## 참고 자료

- Yocto Project 릴리스 목록 (Wrynose 6.0 LTS, Scarthgap 5.0 LTS): https://www.yoctoproject.org/development/releases/
- Yocto Project Quick Build (6.0, bitbake-setup): https://docs.yoctoproject.org/brief-yoctoprojectqs/index.html
- Yocto Project Linux Kernel Development Manual (kernel-yocto, fragments, `.scc`, configcheck): https://docs.yoctoproject.org/kernel-dev/index.html
- Yocto Project Board Support Package Developer's Guide: https://docs.yoctoproject.org/bsp-guide/index.html
- Yocto Project Development Tasks Manual (devtool, 공유 sstate, hash equivalence, 재현성): https://docs.yoctoproject.org/dev-manual/index.html
- Yocto Project Application Development and the Extensible SDK: https://docs.yoctoproject.org/sdk-manual/index.html
- Yocto Project Reference Manual (변수·클래스·태스크 목록): https://docs.yoctoproject.org/ref-manual/index.html
- kas 문서: https://kas.readthedocs.io/
- Google repo: https://gerrit.googlesource.com/git-repo
- SOURCE_DATE_EPOCH 사양: https://reproducible-builds.org/specs/source-date-epoch/
- Bootlin Yocto Project and OpenEmbedded training (슬라이드·실습 공개): https://bootlin.com/training/yocto/
- 소스 (이 노트에서 읽은 파일): openembedded-core `meta/classes-recipe/kernel-yocto.bbclass`, `cml1.bbclass`, `module.bbclass`, `populate_sdk_*.bbclass`, `toolchain-scripts.bbclass`, `meta/classes-global/sstate.bbclass`, `meta/lib/oe/reproducible.py`, `meta/lib/oe/sstatesig.py`, `scripts/lib/devtool/`; meta-freescale `conf/machine/`, `recipes-kernel/linux/`, `recipes-bsp/u-boot/`; bitbake `bin/bitbake-getvar`, `bin/bitbake-diffsigs`, `default-registry/`
