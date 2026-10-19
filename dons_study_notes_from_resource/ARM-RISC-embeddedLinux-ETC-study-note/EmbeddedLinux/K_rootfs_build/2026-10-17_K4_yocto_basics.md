# K4. Yocto / OpenEmbedded 기초 — layer, recipe, BitBake, MACHINE/DISTRO, image

> **이 노트를 다 읽으면**: Yocto · OpenEmbedded · Poky · BitBake · bitbake-setup이 각각 무엇인지 헷갈리지 않고 말할 수 있다 · layer → conf → class → recipe로 이어지는 메타데이터 모델과 `=` `?=` `:append` `:remove` override 규칙을 실제 BitBake 2.18 출력으로 설명할 수 있다 · 레시피(SRC_URI, S/B/D, LIC_FILES_CHKSUM, do_compile/do_install, PACKAGES/FILES, DEPENDS/RDEPENDS)와 bbappend, 이미지 레시피를 직접 써서 BitBake 파서로 검증할 수 있다 · MACHINE/DISTRO/IMAGE를 나눠 설계하고 core-image-minimal을 qemuarm64로 빌드·실행하는 절차를 안내할 수 있다
> **왜 필요한가**: "Yocto 써 봤나요?"는 Embedded Linux JD의 단골 키워드이고, 면접에서는 "bbappend로 고치나 레시피를 고치나", "DEPENDS와 RDEPENDS 차이", "이미지에 패키지를 어떻게 넣나"를 묻는다. 입사 후에는 벤더 BSP layer 위에 제품 layer를 쌓고 레시피를 고치는 게 매일 하는 일이다
> **Don 기준 난이도**: 크로스 컴파일, 링커 스크립트, 이미지 패키징, 빌드 재현성 같은 "펌웨어 빌드 시스템" 감각은 이미 있다 / 새로 배울 부분은 **BitBake라는 독특한 언어(변수 대입 연산자 · override · 클래스 상속)와 "레시피 → 태스크 → 패키지 → 이미지" 데이터 흐름**
> **선행 노트**: B1–B4(크로스 툴체인, sysroot, 링킹), K1(rootfs 구조), K2(BusyBox). 같은 모듈의 K3(Buildroot)와 비교하며 읽고, 실무 심화(BSP layer, devtool, 커널 레시피·패치, SDK/eSDK, sstate 공유, 재현성)는 K5로 미룬다

---

## 0. 큰 그림 — Yocto는 "배포판을 만드는 공장"이다

SSD 펌웨어 시절 빌드를 떠올려 보자. 펌웨어 이미지 하나를 만들려면 컴파일러 버전, 링커 스크립트, 제품별 `#define`(용량·NAND 종류·고객사), 각 모듈의 빌드 순서, 그리고 "이 바이너리에 어떤 기능이 들어갔나"를 기록하는 장치가 필요했다. 보통 Makefile 몇 개와 제품별 설정 파일로 해결했다.

Embedded Linux 장비의 이미지는 규모가 다르다. 부트로더, 커널, C 라이브러리, BusyBox 또는 coreutils, systemd, OpenSSL, Python, 앱 수십 개 — 수백 개의 오픈소스 프로젝트를 **같은 툴체인으로, 같은 정책(보안 플래그·init 시스템·라이선스 규칙)으로, 보드에 맞게** 크로스 빌드해서 하나의 rootfs로 묶어야 한다. 그리고 다음 달에도, 3년 뒤에도 같은 결과가 나와야 한다.

**Yocto Project는 이 일을 하는 빌드 프레임워크다.** 핵심 아이디어는 세 가지다.

- **모든 것은 메타데이터(텍스트 파일)다.** "busybox 1.37.0을 이 URL에서 받아 이 패치를 적용하고 이 설정으로 빌드하라"가 레시피(`.bb`) 한 장이다. 툴체인도, 커널도, 이미지도 레시피다.
- **메타데이터는 layer로 쌓는다.** 커뮤니티 공통 레시피(OE-Core) 위에 SoC 벤더 BSP layer, 그 위에 우리 제품 layer. 남의 레시피는 고치지 않고 위 layer에서 덧씌운다(`.bbappend`).
- **BitBake가 메타데이터를 읽어 태스크 그래프를 만들고 병렬로 실행한다.** 태스크 결과는 해시로 캐시(sstate)되어, 입력이 같으면 다시 빌드하지 않는다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="10" y="10" width="660" height="214" rx="6" fill="#4a7bd0" fill-opacity="0.05" stroke="#4a7bd0" stroke-width="1.3"/><text x="22.0" y="30.0" font-size="13" text-anchor="start" font-weight="bold">Yocto Project — 우산 프로젝트 (릴리스 · 문서 · 테스트 · 도구)</text><rect x="22" y="42" width="636" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="340.0" y="63.5" font-size="13" text-anchor="middle">BitBake 2.18 — 빌드 엔진: 메타데이터를 읽고 태스크를 병렬 실행</text><rect x="22" y="86" width="636" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="340.0" y="107.5" font-size="13" text-anchor="middle">OpenEmbedded-Core (meta/) — 클래스 230 · 레시피 950 (툴체인 · glibc · 커널 …)</text><rect x="22" y="130" width="636" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="340.0" y="151.5" font-size="13" text-anchor="middle">meta-yocto — meta-poky (DISTRO "poky") + meta-yocto-bsp (레퍼런스 보드)</text><rect x="22" y="174" width="636" height="36" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="340.0" y="195.5" font-size="13" text-anchor="middle">도구: bitbake-setup · bitbake-layers · runqemu · devtool · yocto-check-layer</text><rect x="10" y="236" width="324" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="172.0" y="254.0" font-size="13" text-anchor="middle">OpenEmbedded 커뮤니티 layer</text><text x="172.0" y="271.0" font-size="13" text-anchor="middle">meta-openembedded (meta-oe …)</text><rect x="346" y="236" width="324" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="508.0" y="254.0" font-size="13" text-anchor="middle">SoC 벤더 BSP layer</text><text x="508.0" y="271.0" font-size="13" text-anchor="middle">meta-freescale · meta-ti · meta-tegra</text><rect x="10" y="290" width="660" height="30" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="340.0" y="308.5" font-size="13" text-anchor="middle">우리 제품 layer: meta-k4-sensorkit (앱 레시피 · MACHINE · DISTRO · 이미지)</text><text x="340.0" y="336.0" font-size="12" text-anchor="middle">Poky = 레퍼런스 DISTRO 이름. 예전 poky git 저장소(위 셋을 합친 것)는 5.2까지만 브랜치가 있다</text></svg>
```
그림 1 — 이름 정리. Yocto Project는 우산이고, 실제로 일하는 것은 BitBake(엔진) + OpenEmbedded-Core(공통 메타데이터)다. Poky는 레퍼런스 DISTRO의 이름이다. 아래쪽 layer들은 Yocto Project 밖에서 만들어져 같은 방식으로 쌓인다.

### 0.1 이름 다섯 개를 정리한다

면접에서 이름을 섞어 쓰면 바로 티가 난다. 한 번 정확히 정리해 두자.

| 이름 | 무엇인가 | 한 줄 설명 |
|---|---|---|
| Yocto Project | Linux Foundation 산하 협업 프로젝트 | 릴리스 일정, 문서, 테스트 인프라(autobuilder), 도구를 관리하는 우산 |
| OpenEmbedded (OE) | 커뮤니티 + 빌드 방법론 | Yocto보다 먼저 있었다. 지금은 OE-Core와 meta-openembedded를 관리 |
| OpenEmbedded-Core (OE-Core) | git 저장소 `openembedded-core`의 `meta/` layer | 툴체인·glibc·busybox·커널·이미지 클래스 등 공통 메타데이터. 모든 Yocto 빌드의 바닥 |
| BitBake | Python으로 쓴 태스크 실행 엔진 | 메타데이터를 파싱하고 의존성 그래프를 만들어 태스크를 병렬 실행. `make`에 해당 |
| Poky | 레퍼런스 DISTRO 이름 (`meta-poky` layer) | "Yocto가 테스트하는 기본 배포판 정책". 예전에는 git 저장소 이름이기도 했다 |

Don의 bare-metal 경험으로 번역하면 이렇다: **BitBake = make**, **레시피 = 모듈별 Makefile 조각 + 소스 위치 + 설치 규칙**, **layer = 벤더 SDK 폴더와 우리 제품 폴더**, **MACHINE = 보드 정의 헤더**, **DISTRO = 펌웨어 빌드 정책(최적화·보안 옵션·기능 플래그)**, **IMAGE = 최종 바이너리에 무엇을 링크할지 정한 목록**.

### 0.2 지금(2026-10) 어떤 릴리스를 써야 하나 — 그리고 "poky 저장소"는 끝났다

Yocto는 6개월마다 릴리스를 내고, 2년마다 4년 지원 LTS를 낸다. yoctoproject.org 릴리스 페이지(2026-10-17 확인) 기준 상황은 이렇다.

| 코드명 | 버전 | 출시 | 상태 |
|---|---|---|---|
| Wrynose | 6.0 | 2026-04 | **LTS — 2030-04까지** (현재 최신 LTS) |
| Scarthgap | 5.0 | 2024-04 | LTS — 2028-04까지 |
| Whinlatter | 5.3 | 2025-11 | EOL |
| Walnascar | 5.2 | 2025-04 | EOL |
| Blacksail | 6.1 | 개발 중 | 다음 릴리스 |

이 노트는 **Wrynose 6.0 LTS**를 기준으로 한다. 신규 제품이면 Wrynose, 이미 Scarthgap으로 출시한 제품이면 2028년까지 Scarthgap을 유지하는 게 보통의 선택이다.

중요한 변화가 하나 있다. 예전 튜토리얼은 전부 `git clone git://git.yoctoproject.org/poky`로 시작한다. poky 저장소는 BitBake + OE-Core + meta-yocto를 한 git에 합쳐 둔 편의용 저장소였다. Whinlatter(5.3)부터 이 저장소의 갱신이 멈췄다. 실제로 확인해 보자.

무엇을 확인하나: 세 저장소에 어떤 릴리스 브랜치가 있는지.

```sh
for r in https://git.yoctoproject.org/poky https://git.openembedded.org/openembedded-core \
         https://git.yoctoproject.org/meta-yocto; do
  echo "== $r"
  git ls-remote --heads $r | grep -oE "refs/heads/(scarthgap|walnascar|whinlatter|wrynose)$" | tr "\n" " "; echo
done
```

```text
== https://git.yoctoproject.org/poky
refs/heads/scarthgap refs/heads/walnascar 
== https://git.openembedded.org/openembedded-core
refs/heads/scarthgap refs/heads/walnascar refs/heads/whinlatter refs/heads/wrynose 
== https://git.yoctoproject.org/meta-yocto
refs/heads/scarthgap refs/heads/walnascar refs/heads/whinlatter refs/heads/wrynose 
```

출력에서 볼 것: poky에는 `wrynose` 브랜치가 없다. 6.0을 쓰려면 **bitbake, openembedded-core, meta-yocto를 따로 clone**하거나, 새 도구 **`bitbake-setup`**을 쓴다. yocto-docs의 migration-5.3 문서가 이 변화를 "The Poky repository master branch is no longer updated"라는 절로 설명하고, Poky라는 **DISTRO**는 그대로 남는다고 밝힌다.

이 노트 작업을 위해 Lima VM의 `/var/tmp/K4/`에 세 저장소와 문서를 `--depth 1`로 받았다. 버전을 확인한다.

```sh
cd /var/tmp/K4
git clone -q --depth 1 -b wrynose https://git.openembedded.org/openembedded-core k4_oe-core
git clone -q --depth 1 -b wrynose https://git.yoctoproject.org/meta-yocto k4_meta-yocto
git clone -q --depth 1 -b 2.18 https://git.openembedded.org/bitbake k4_bitbake
git clone -q --depth 1 -b wrynose https://git.yoctoproject.org/yocto-docs k4_yocto-docs
du -sh k4_*
for d in k4_*; do (cd $d; echo "$d $(git log -1 --format='%h %cd %s' --date=short)"); done
```

```text
14M	k4_bitbake
724K	k4_meta-yocto
50M	k4_oe-core
22M	k4_yocto-docs
k4_bitbake 6e260eb 2026-10-07 ast: Make PyLibNode support submodules in BBIMPORTS
k4_meta-yocto 7c25b11 2026-09-30 poky.conf: Bump version for 6.0.4 release
k4_oe-core 636e5d3 2026-10-12 wget: fix CVE-2026-15146
k4_yocto-docs 4328c23 2026-10-15 dev-manual: correct directory name for oe-core wic files
```

출력에서 볼 것: 메타데이터 전체가 100 MB도 안 된다. Yocto 빌드가 수십~수백 GB를 쓰는 것은 메타데이터가 아니라 **받은 소스와 빌드 산출물** 때문이다. 버전을 확인한다.

```sh
grep -n "__version__" k4_bitbake/lib/bb/__init__.py
grep -n "LAYERSERIES_CORENAMES" k4_oe-core/meta/conf/layer.conf
grep -n "DISTRO_VERSION\|DISTRO_CODENAME" k4_meta-yocto/meta-poky/conf/distro/poky.conf
grep -n "\"expires\"\|\"branch\"" k4_bitbake/default-registry/configurations/poky-wrynose.conf.json
```

```text
12:__version__ = "2.18.0"
10:LAYERSERIES_CORENAMES = "wrynose"
3:DISTRO_VERSION = "6.0.4"
4:DISTRO_CODENAME = "wrynose"
6:SDK_VERSION = "${@d.getVar('DISTRO_VERSION').replace('snapshot-${METADATA_REVISION}', 'snapshot')}"
3:    "expires": "2030-05-31",
8:                "branch": "2.18",
15:                "branch": "wrynose",
22:                "branch": "wrynose",
29:                "branch": "wrynose",
```

출력에서 볼 것: Wrynose의 짝은 **BitBake 2.18**이다. `poky-wrynose.conf.json`은 bitbake-setup이 읽는 "설정 레지스트리"로, 어떤 저장소의 어떤 브랜치를 받을지가 적혀 있다(expires 2030-05-31 = LTS 기간). wrynose 브랜치 HEAD는 6.0.4 출시 준비 커밋이고, git 태그는 `yocto-6.0.3`까지 나와 있다(`git ls-remote --tags`로 확인).

### 0.3 이 노트의 실습 범위 — 빌드는 하지 않는다

전체 빌드는 하지 않는다(Don이 나중에 직접 한다). 대신 다음은 **전부 실제로 실행**했다.

- 실제 Wrynose 메타데이터 파일 읽기 (`grep`, `sed`)
- 빌드 디렉터리 만들기(`oe-init-build-env`), `bitbake-layers`, `bitbake-config-build`, `bitbake-getvar` — **설정 파싱 단계까지는 BitBake가 정상 동작했다**
- BitBake 파서(tinfoil API)로 **레시피 파일 하나씩 파싱** — 태스크는 실행하지 않고 변수·태스크 목록만 꺼낸다
- 예제 layer `meta-k4-sensorkit`의 C 앱을 VM gcc로 컴파일·실행, 라이선스 md5 계산, BusyBox 설정 조각 병합

그리고 **빌드를 시작하는 순간 Ubuntu 26.04의 AppArmor user namespace 제한에 막힌다**는 것도 실제 에러로 확인했다(10절). 실제 빌드 절차·출력이 필요한 부분은 **"실행하지 않음 — 가이드"**라고 표시하고, 출력을 지어내지 않았다.

실습 환경: Lima VM `elinux`, Ubuntu 26.04.1 LTS, 커널 `7.0.0-38-generic`, aarch64 6코어, RAM 11 GB, `/var/tmp` 여유 68 GB.

---

## 1. 메타데이터 모델 — conf · class · recipe · layer

### 1.1 파일 종류는 다섯 가지뿐이다

BitBake가 읽는 파일은 확장자로 역할이 정해진다.

| 파일 | 역할 | 예 | bare-metal 비유 |
|---|---|---|---|
| `.conf` | 전역 설정. 변수만 정의 | `bitbake.conf`, `local.conf`, `machine/qemuarm64.conf`, `layer.conf` | 전역 `config.h`, 보드 헤더 |
| `.bbclass` | 공통 로직(태스크 함수·변수). `inherit`로 가져다 씀 | `autotools.bbclass`, `image.bbclass`, `module.bbclass` | 공용 Makefile include (`rules.mk`) |
| `.bb` | 레시피 = 소프트웨어 하나(또는 이미지 하나) 빌드 방법 | `busybox_1.37.0.bb`, `core-image-minimal.bb` | 모듈별 Makefile |
| `.bbappend` | 다른 layer의 `.bb`에 덧붙이는 조각 | `busybox_%.bbappend` | 벤더 코드를 안 건드리고 제품 패치를 얹는 오버레이 |
| `.inc` | `require`/`include`로 끼워 넣는 공용 조각 | `busybox.inc`, `qemu.inc` | `#include "common.h"` |

OE-Core의 `meta/`를 세어 보면 규모가 감이 온다. 클래스 디렉터리는 세 개로 나뉘어 있다: `classes-global/`(전역 `INHERIT`로만 쓰는 것), `classes-recipe/`(레시피에서 `inherit`하는 것), `classes/`(둘 다 가능).

```sh
cd /var/tmp/K4/k4_oe-core/meta
for d in classes-global classes-recipe classes; do echo "$d: $(ls $d/*.bbclass | wc -l)"; done
find . -name "*.bb" | wc -l
```

```text
classes-global: 23
classes-recipe: 164
classes: 43
950
```

출력에서 볼 것: OE-Core 하나에 레시피 950개, 클래스 230개. 이게 "공통 바닥"이고, 제품에 필요한 나머지(Qt, ROS 2, 벤더 GPU 드라이버 …)는 다른 layer에서 온다.

### 1.2 layer란 무엇이고, 어떻게 자기를 등록하나

**layer**는 `conf/layer.conf`가 들어 있는 디렉터리다. 관례상 이름이 `meta-`로 시작한다. layer.conf는 BitBake에게 세 가지를 알려 준다: 내 conf·class를 어디서 찾나(`BBPATH`), 내 레시피는 어떤 파일인가(`BBFILES`), 나의 이름과 우선순위는(`BBFILE_COLLECTIONS`, `BBFILE_PRIORITY_<이름>`).

실제 OE-Core와 meta-poky의 layer.conf 앞부분이다.

```sh
sed -n 1,17p /var/tmp/K4/k4_oe-core/meta/conf/layer.conf
cat /var/tmp/K4/k4_meta-yocto/meta-poky/conf/layer.conf
```

```text
# We have a conf and classes directory, add to BBPATH
BBPATH .= ":${LAYERDIR}"
# We have recipes-* directories, add to BBFILES
BBFILES += "${LAYERDIR}/recipes-*/*/*.bb"

BBFILE_COLLECTIONS += "core"
BBFILE_PATTERN_core = "^${LAYERDIR}/"
BBFILE_PRIORITY_core = "5"

LAYERSERIES_CORENAMES = "wrynose"

# This should only be incremented on significant changes that will
# cause compatibility issues with other layers
LAYERVERSION_core = "15"
LAYERSERIES_COMPAT_core = "wrynose"

BBLAYERS_LAYERINDEX_NAME_core = "openembedded-core"

# We have a conf and classes directory, add to BBPATH
BBPATH =. "${LAYERDIR}:"

# We have recipes-* directories, add to BBFILES
BBFILES += "${LAYERDIR}/recipes-*/*/*.bb \
            ${LAYERDIR}/recipes-*/*/*.bbappend"

BBFILE_COLLECTIONS += "yocto"
BBFILE_PATTERN_yocto = "^${LAYERDIR}/"
BBFILE_PRIORITY_yocto = "5"

LAYERSERIES_COMPAT_yocto = "wrynose"

# This should only be incremented on significant changes that will
# cause compatibility issues with other layers
LAYERVERSION_yocto = "3"

LAYERDEPENDS_yocto = "core"

REQUIRED_POKY_BBLAYERS_CONF_VERSION = "2"
```

출력에서 볼 것:

- `BBPATH`는 콜론으로 이은 **검색 경로**다. `require conf/machine/qemuarm64.conf`나 `inherit autotools`가 나오면 BitBake는 BBPATH의 각 디렉터리 아래 `conf/…`, `classes-recipe/…`를 차례로 찾는다. OE-Core는 뒤에 붙이고(`.=`), meta-poky는 앞에 붙인다(`=.`) — 앞에 있을수록 먼저 찾아진다.
- `BBFILES`는 **glob 패턴**이다. 그래서 레시피는 반드시 `recipes-<분류>/<이름>/<이름>_<버전>.bb` 깊이에 있어야 잡힌다. 디렉터리 한 단계를 잘못 만들면 레시피가 "없는 것"이 된다.
- OE-Core layer.conf는 `.bbappend`를 BBFILES에 넣지 않는다 — OE-Core는 남의 레시피를 고치지 않는 바닥이기 때문이다.
- `LAYERSERIES_COMPAT`은 "이 layer는 어느 릴리스와 호환되나"의 선언이다. 다르면 거부된다(아래 예제).
- `LAYERDEPENDS`는 "이 layer가 동작하려면 어떤 layer가 있어야 하나"다.

### 1.3 layer 쌓기, 우선순위, bbappend 순서

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="20" y="14" width="300" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="170.0" y="34.0" font-size="13" text-anchor="middle">meta-k4-sensorkit</text><text x="170.0" y="51.0" font-size="13" text-anchor="middle">BBFILE_PRIORITY 10</text><rect x="20" y="72" width="300" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="170.0" y="92.0" font-size="13" text-anchor="middle">벤더 BSP layer (예: meta-ti-bsp)</text><text x="170.0" y="109.0" font-size="13" text-anchor="middle">우선순위는 그 layer.conf가 정함</text><rect x="20" y="130" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="170.0" y="150.0" font-size="13" text-anchor="middle">meta-poky  (collection "yocto")</text><text x="170.0" y="167.0" font-size="13" text-anchor="middle">BBFILE_PRIORITY 5</text><rect x="20" y="188" width="300" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="170.0" y="208.0" font-size="13" text-anchor="middle">meta  (OE-Core, collection "core")</text><text x="170.0" y="225.0" font-size="13" text-anchor="middle">BBFILE_PRIORITY 5</text><rect x="20" y="252" width="300" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="170.0" y="272.5" font-size="13" text-anchor="middle">BitBake 엔진 (layer 아님)</text><text x="170.0" y="312.0" font-size="12" text-anchor="middle">위로 갈수록 나중에 덮어쓴다</text><line x1="330" y1="280" x2="330" y2="20" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah2)"/><text x="346.0" y="34.0" font-size="13" text-anchor="start" font-weight="bold">bblayers.conf</text><text x="346.0" y="52.0" font-size="12" text-anchor="start">BBLAYERS = 이 목록 (빌드 디렉터리 conf/)</text><text x="346.0" y="78.0" font-size="13" text-anchor="start" font-weight="bold">각 layer의 conf/layer.conf</text><text x="346.0" y="96.0" font-size="12" text-anchor="start">BBPATH .= ":${LAYERDIR}" → conf·classes 검색</text><text x="346.0" y="122.0" font-size="12" text-anchor="start">BBFILES += recipes-*/*/*.bb, *.bbappend</text><text x="346.0" y="148.0" font-size="13" text-anchor="start" font-weight="bold">BBFILE_PRIORITY</text><text x="346.0" y="166.0" font-size="12" text-anchor="start">같은 이름 레시피가 둘이면 높은 쪽이 이긴다</text><text x="346.0" y="192.0" font-size="13" text-anchor="start" font-weight="bold">bbappend 적용 순서</text><text x="346.0" y="210.0" font-size="12" text-anchor="start">낮은 우선순위 → 높은 우선순위 (마지막이 이김)</text><text x="346.0" y="236.0" font-size="13" text-anchor="start" font-weight="bold">LAYERSERIES_COMPAT</text><text x="346.0" y="254.0" font-size="12" text-anchor="start">"wrynose"가 아니면 add-layer 단계에서 거부</text></svg>
```
그림 2 — layer 스택. 빌드 디렉터리의 `bblayers.conf`가 쓸 layer 목록(BBLAYERS)을 정하고, 각 layer의 `layer.conf`가 자기 경로를 BBPATH·BBFILES에 등록한다. 같은 레시피가 여러 layer에 있으면 우선순위가 높은 쪽이, bbappend는 낮은 쪽부터 적용되어 높은 쪽이 마지막에 덮는다.

bbappend 적용 순서를 BitBake 소스로 확인했다. `lib/bb/cooker.py`는 BBFILES를 우선순위 오름차순으로 정렬하고, 그 순서대로 `.bbappend` 목록을 만든다.

```sh
sed -n 1850,1851p /var/tmp/K4/k4_bitbake/lib/bb/cooker.py
sed -n 1962,1972p /var/tmp/K4/k4_bitbake/lib/bb/cooker.py
```

```text
        # Sort files by priority
        files.sort( key=lambda fileitem: self.calc_bbfile_priority(fileitem)[0] )
    def get_file_appends(self, fn):
        """
        Returns a list of .bbappend files to apply to fn
        """
        filelist = []
        f = os.path.basename(fn)
        for b in self.bbappends:
            (bbappend, filename) = b
            if (bbappend == f) or ('%' in bbappend and bbappend.startswith(f[:bbappend.index('%')])):
                filelist.append(filename)
        return tuple(filelist)
```

출력에서 볼 것: bbappend 매칭은 **파일 이름**으로 한다. `busybox_%.bbappend`의 `%`는 "이 뒤는 아무거나" 와일드카드라서 `busybox_1.37.0.bb`에도, 다음 릴리스의 `busybox_1.38.0.bb`에도 붙는다. `busybox_1.37.0.bbappend`처럼 버전을 박으면 레시피 버전이 올라가는 순간 "bbappend에 짝이 없다"는 에러로 빌드가 멈춘다. 이건 의도된 안전장치다 — 버전이 바뀌면 덧붙인 패치가 여전히 맞는지 사람이 확인하라는 뜻이다.

### 1.4 파싱 순서 — 전역 설정 한 번, 레시피마다 한 번

BitBake의 일은 두 단계다.

1. **전역 설정 파싱(한 번)**: `conf/bblayers.conf` → 각 `layer.conf` → `conf/bitbake.conf`. bitbake.conf가 다시 `local.conf`, MACHINE conf, DISTRO conf 등을 include하고, 마지막에 `INHERIT` 목록의 전역 클래스를 읽는다. 결과는 전역 datastore(변수 사전) 하나다.
2. **레시피 파싱(레시피마다)**: 전역 datastore를 복사한 뒤 `.bb`를 읽고, `inherit`한 클래스와 `.bbappend`를 얹고, override·`:append`를 확정한다. 결과가 레시피 datastore이고, 이게 그 레시피 태스크들의 입력이다.

bitbake.conf 안의 include 순서는 실제 파일에서 바로 보인다.

```sh
grep -n "^require \|^include \|^addfragments" /var/tmp/K4/k4_oe-core/meta/conf/bitbake.conf
grep -n "^INHERIT\|^PACKAGE_CLASSES" /var/tmp/K4/k4_oe-core/meta/conf/distro/defaultsetup.conf
```

```text
813:require conf/abi_version.conf
814:include conf/site.conf
815:include conf/auto.conf
816:include conf/toolcfg.conf
817:include conf/local.conf
822:addfragments ${OE_FRAGMENTS_PREFIX} OE_FRAGMENTS OE_FRAGMENTS_METADATA_VARS OE_FRAGMENTS_BUILTIN
824:require ${@"conf/multiconfig/${BB_CURRENT_MC}.conf" if "${BB_CURRENT_MC}" != "" else ""}
825:include conf/machine/${MACHINE}.conf
826:include conf/machine-sdk/${SDKMACHINE}.conf
827:require conf/cve-check-map.conf
828:include conf/distro/${DISTRO}.conf
829:include conf/distro/defaultsetup.conf
830:include conf/documentation.conf
831:include conf/licenses.conf
832:require conf/sanity.conf
833:include conf/bblock.conf
16:PACKAGE_CLASSES ?= "package_ipk"
17:INHERIT_DISTRO ?= "debian devshell sstate license remove-libtool create-spdx buildstats uninative"
18:INHERIT += "${PACKAGE_CLASSES} ${USER_CLASSES} ${INHERIT_DISTRO}"
```

출력에서 볼 것:

- `local.conf`가 machine/distro conf보다 **먼저** 읽힌다. 그래서 local.conf에서 `MACHINE = "..."`라고 정하면 그 값으로 825행의 `conf/machine/${MACHINE}.conf`가 결정된다. 반대로 machine conf 안에서 `?=`로 준 기본값은 local.conf가 이미 정한 값을 못 이긴다.
- `include`는 파일이 없어도 조용히 넘어가고, `require`는 없으면 에러다. `MACHINE`을 오타 내면 825행이 조용히 넘어가는 대신, 나중에 sanity 검사가 "MACHINE을 찾을 수 없다"고 알려 준다.
- `addfragments`(822행)는 Wrynose의 새 기능인 **설정 조각(fragment)**을 적용한다. 2.3절에서 쓴다.
- 패키지 형식(`PACKAGE_CLASSES`)도, sstate·라이선스·SPDX·buildstats도 결국 `INHERIT`로 들어가는 전역 클래스다(`INHERIT_DISTRO`).

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20.0" y="22.0" font-size="13" text-anchor="start" font-weight="bold">① 전역 설정 파싱 (한 번) — bitbake-getvar가 보여 주는 단계</text><rect x="20" y="34" width="190" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="115.0" y="54.5" font-size="13" text-anchor="middle">bblayers.conf</text><rect x="232" y="34" width="190" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="327.0" y="54.5" font-size="13" text-anchor="middle">layer.conf × N</text><line x1="210" y1="51" x2="230" y2="51" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><rect x="444" y="34" width="190" height="34" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="539.0" y="54.5" font-size="13" text-anchor="middle">conf/bitbake.conf</text><line x1="422" y1="51" x2="442" y2="51" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><rect x="20" y="82" width="640" height="120" rx="6" fill="#e08a3c" fill-opacity="0.05" stroke="#e08a3c" stroke-width="1.3"/><text x="32.0" y="100.0" font-size="12" text-anchor="start" font-weight="bold">bitbake.conf 안의 include 순서 (실제 파일 813~833행)</text><rect x="32" y="112" width="196" height="32" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="130.0" y="131.0" font-size="12" text-anchor="middle">site.conf · auto.conf</text><rect x="240" y="112" width="196" height="32" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="338.0" y="131.0" font-size="12" text-anchor="middle">toolcfg.conf (fragment)</text><rect x="448" y="112" width="196" height="32" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="546.0" y="131.0" font-size="12" text-anchor="middle">local.conf</text><rect x="32" y="156" width="196" height="32" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="130.0" y="175.0" font-size="12" text-anchor="middle">machine/${MACHINE}.conf</text><rect x="240" y="156" width="196" height="32" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="338.0" y="175.0" font-size="12" text-anchor="middle">distro/${DISTRO}.conf</text><rect x="448" y="156" width="196" height="32" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="546.0" y="175.0" font-size="12" text-anchor="middle">defaultsetup.conf</text><line x1="340" y1="202" x2="340" y2="220" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><rect x="20" y="222" width="640" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="340.0" y="242.5" font-size="13" text-anchor="middle">INHERIT 클래스 (base · package_rpm · sstate · sanity …) → 전역 datastore</text><text x="20.0" y="284.0" font-size="13" text-anchor="start" font-weight="bold">② 레시피마다 (수백~수천 번) — tinfoil parse_recipe_file이 하는 일</text><rect x="20" y="296" width="145" height="34" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="92.5" y="316.0" font-size="12" text-anchor="middle">xxx_1.0.bb</text><rect x="185" y="296" width="145" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="257.5" y="316.0" font-size="12" text-anchor="middle">inherit 클래스</text><line x1="165" y1="313" x2="183" y2="313" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><rect x="350" y="296" width="145" height="34" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="422.5" y="316.0" font-size="12" text-anchor="middle">*.bbappend</text><line x1="330" y1="313" x2="348" y2="313" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><rect x="515" y="296" width="145" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="587.5" y="316.0" font-size="12" text-anchor="middle">override 확정</text><line x1="495" y1="313" x2="513" y2="313" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/><line x1="340" y1="256" x2="340" y2="294" stroke="currentColor" stroke-width="1.3" stroke-dasharray="4 3" marker-end="url(#ah3)"/><text x="340.0" y="356.0" font-size="12" text-anchor="middle">전역 datastore를 복사한 위에 레시피를 얹는다 → 레시피 datastore 하나 = 태스크들의 입력</text><text x="340.0" y="380.0" font-size="12" text-anchor="middle">Ubuntu 26.04 VM: ①은 실제로 돌았고, 빌드 시작 시 sanity가 user namespace 검사에서 멈춘다</text></svg>
```
그림 3 — BitBake의 파싱 순서. ①은 빌드 디렉터리당 한 번, ②는 레시피마다 일어난다. 이 노트에서는 ①과 ②를 실제로 돌렸고, 그 다음 단계인 태스크 실행(빌드)만 하지 않았다.

### 1.5 레시피 이름에서 PN·PV가 나온다

레시피 파일 이름 규칙은 `<PN>_<PV>.bb`다. bitbake.conf가 파일 이름을 잘라 변수로 만든다.

```sh
sed -n 243,247p /var/tmp/K4/k4_oe-core/meta/conf/bitbake.conf
sed -n 413,422p /var/tmp/K4/k4_oe-core/meta/conf/bitbake.conf
```

```text
PN = "${@bb.parse.vars_from_file(d.getVar('FILE', False),d)[0] or 'defaultpkgname'}"
PV = "${@bb.parse.vars_from_file(d.getVar('FILE', False),d)[1] or '1.0'}"
PR = "r0"
PE = ""
PF = "${PN}-${EXTENDPE}${PV}-${PR}"
STAMP = "${STAMPS_DIR}/${MULTIMACH_TARGET_SYS}/${PN}/${PV}"
STAMPCLEAN = "${STAMPS_DIR}/${MULTIMACH_TARGET_SYS}/${PN}/*"
BASE_WORKDIR ?= "${TMPDIR}/work"
WORKDIR = "${BASE_WORKDIR}/${MULTIMACH_TARGET_SYS}/${PN}/${PV}"
UNPACKDIR ??= "${WORKDIR}/sources"
BB_GIT_DEFAULT_DESTSUFFIX = "${BP}"
T = "${WORKDIR}/temp"
D = "${WORKDIR}/image"
S = "${UNPACKDIR}/${BP}"
B = "${S}"
```

출력에서 볼 것:

- `PN`(package name) = 파일 이름의 `_` 앞, `PV`(package version) = `_` 뒤. 버전이 없으면 `1.0`. `PR`(package revision)은 레시피를 고쳤을 때 올리는 리비전이었지만, 지금은 PR 서버/해시가 관리해서 손으로 올릴 일이 거의 없다. `PE`(epoch)는 버전 번호가 거꾸로 갈 때만 쓴다.
- git 소스의 기본 풀림 위치 `BB_GIT_DEFAULT_DESTSUFFIX`도 `${BP}`라서, git으로 받는 레시피는 기본 `S`가 그대로 맞는다.
- 6.0의 `WORKDIR`에는 `PR`이 들어가지 않는다(`.../<PN>/<PV>`). 그리고 소스는 `WORKDIR` 바로 아래가 아니라 `${WORKDIR}/sources`(UNPACKDIR)에 풀린다. 옛 튜토리얼의 `S = "${WORKDIR}/git"`나 `S = "${WORKDIR}"`는 5.3부터 에러다 — migration-5.3 문서가 `S = "${UNPACKDIR}/something"`으로 바꾸라고 하고, git 소스는 기본값이 맞으므로 S 줄을 아예 지우라고 한다.

---

## 2. 빌드 디렉터리 — oe-init-build-env, local.conf, fragment

### 2.1 빌드 디렉터리 만들기

빌드는 항상 **빌드 디렉터리** 안에서 한다. `oe-init-build-env`를 `source`하면 디렉터리가 없을 때 템플릿으로 `conf/`를 만들고, PATH에 `bitbake`와 OE 스크립트를 넣고, 그 디렉터리로 cd한다. 개별 clone 방식에서는 bitbake 위치를 `BITBAKEDIR`로 알려 준다.

```sh
cd /var/tmp/K4/k4_oe-core
export BITBAKEDIR=/var/tmp/K4/k4_bitbake
. ./oe-init-build-env /var/tmp/K4/k4_build > /var/tmp/K4/k4_initenv.log 2>&1
pwd; ls conf; cat conf/templateconf.cfg
echo $PATH | tr ":" "\n" | head -2
```

```text
/var/tmp/K4/k4_build
bblayers.conf
conf-notes.txt
conf-summary.txt
local.conf
templateconf.cfg
meta/conf/templates/default
/var/tmp/K4/k4_oe-core/scripts
/var/tmp/K4/k4_bitbake/bin
```

출력에서 볼 것: `conf/` 아래 두 파일이 핵심이다 — **`bblayers.conf`(어떤 layer를 쓰나)**와 **`local.conf`(이 빌드의 설정)**. 템플릿은 OE-Core의 `meta/conf/templates/default`에서 왔다(Wrynose에서는 템플릿이 meta-poky가 아니라 OE-Core에 있다). 이 템플릿의 bblayers.conf는 OE-Core 하나만 담고 있고 DISTRO도 정하지 않는다.

```sh
cat /var/tmp/K4/k4_oe-core/meta/conf/templates/default/bblayers.conf.sample
grep -n '^DISTRO ??=' /var/tmp/K4/k4_oe-core/meta/conf/bitbake.conf
```

```text
# LAYER_CONF_VERSION is increased each time build/conf/bblayers.conf
# changes incompatibly
LCONF_VERSION = "7"

BBPATH = "${TOPDIR}"
BBFILES ?= ""

BBLAYERS ?= " \
  ##OEROOT##/meta \
  "
788:DISTRO ??= "nodistro"
```

출력에서 볼 것: 아무 것도 안 하면 DISTRO는 `nodistro`(OE-Core 기본 정책)다. Poky로 빌드하려면 meta-poky layer를 추가하고 DISTRO를 poky로 바꿔야 한다 — bitbake-setup의 `poky-wrynose` 설정이 정확히 그 일(layer 3개 + `distro/poky` fragment)을 대신 해 준다.

### 2.2 첫 실행에서 만나는 것 — HOSTTOOLS 검사

환경을 잡고 첫 명령을 실행하면 BitBake는 먼저 호스트를 검사한다.

```sh
timeout 300 bitbake-layers show-layers
```

```text
NOTE: Starting bitbake server...
ERROR: The following required tools (as specified by HOSTTOOLS) appear to be unavailable in PATH, please install them in order to proceed:
  chrpath diffstat
```

출력에서 볼 것: OE는 빌드가 호스트 환경에 오염되지 않도록, `HOSTTOOLS`에 나열된 호스트 명령만 `tmp/hosttools/`에 심볼릭 링크로 모아 두고 **PATH를 그 디렉터리로 제한**한다. 목록에 있는데 호스트에 없으면 시작조차 하지 않는다. 이 VM에는 `chrpath`와 `diffstat`이 없었다(공용 VM이라 apt 설치는 하지 않았다). 정식 해결은 `sudo apt install chrpath diffstat`(또는 yocto-docs의 Ubuntu 필수 패키지 목록 전체)이다.

이 노트에서는 **설정 파싱만** 하려고, 아무 일도 하지 않는 가짜 `chrpath`/`diffstat` 스크립트를 `/var/tmp/K4/k4_fakebin`에 두고 PATH 뒤에 붙였다. 실제 빌드에서는 절대 이렇게 하면 안 된다(chrpath는 바이너리의 RPATH를 고치는 도구라 가짜면 패키지가 망가진다). 그 뒤 hosttools 디렉터리가 만들어진 모습이다.

```sh
cd /var/tmp/K4/k4_build/tmp/hosttools
ls | wc -l
ls -l gcc chrpath make
```

```text
124
lrwxrwxrwx 1 donh donh 30 Oct 17 17:40 chrpath -> /var/tmp/K4/k4_fakebin/chrpath
lrwxrwxrwx 1 donh donh 12 Oct 17 17:40 gcc -> /usr/bin/gcc
lrwxrwxrwx 1 donh donh 13 Oct 17 17:40 make -> /usr/bin/make
```

출력에서 볼 것: 호스트 명령 124개만 허용 목록으로 링크되어 있다. 호스트 gcc는 **native 도구를 빌드할 때만** 쓰이고, 타깃 코드는 Yocto가 직접 만든 크로스 gcc로 빌드한다.

### 2.3 layer 추가와 MACHINE/DISTRO 선택 — bitbake-layers, bitbake-config-build

예제 layer(9절)와 meta-poky를 추가한다. `bitbake-layers add-layer`는 bblayers.conf를 고치고, 추가한 상태로 설정을 다시 파싱해 문제가 있으면 되돌린다.

```sh
bitbake-layers add-layer /var/tmp/K4/k4_meta-yocto/meta-poky /var/tmp/K4/k4_layers/meta-k4-sensorkit
bitbake-layers show-layers
```

```text
NOTE: Starting bitbake server...
NOTE: Reconnecting to bitbake server...
layer                 path                                                                    priority
========================================================================================================
core                  /var/tmp/K4/k4_oe-core/meta                                             5
yocto                 /var/tmp/K4/k4_meta-yocto/meta-poky                                     5
k4-sensorkit          /var/tmp/K4/k4_layers/meta-k4-sensorkit                                 10
```

출력에서 볼 것: 첫 열은 디렉터리 이름이 아니라 `BBFILE_COLLECTIONS`에 등록한 **collection 이름**이다(meta-poky의 이름은 "yocto").

호환되지 않는 layer를 넣으면 어떻게 되나. `bitbake-layers create-layer`로 템플릿 layer를 만들고, LAYERSERIES_COMPAT을 일부러 이전 LTS 이름으로 바꿔 추가해 봤다.

```sh
bitbake-layers create-layer --priority 7 /var/tmp/K4/k4_layers/meta-k4-gen
sed -i 's/"wrynose"/"scarthgap"/' /var/tmp/K4/k4_layers/meta-k4-gen/conf/layer.conf
bitbake-layers add-layer /var/tmp/K4/k4_layers/meta-k4-gen
grep -c meta-k4-gen conf/bblayers.conf
```

```text
Add your new layer with 'bitbake-layers add-layer /var/tmp/K4/k4_layers/meta-k4-gen'
ERROR: Layer meta-k4-gen is not compatible with the core layer which only supports these series: wrynose (layer is compatible with scarthgap)
ERROR: Parse failure with the specified layer added, exiting.
0
```

출력에서 볼 것: 거부되고 bblayers.conf는 그대로다(`0`). 인터넷에서 찾은 layer가 Scarthgap용이면 이렇게 막힌다. LAYERSERIES_COMPAT만 고쳐서 억지로 넣는 것은 "컴파일 경고를 끄는 것"과 같다 — 실제로 호환되는지는 빌드와 테스트로 확인해야 한다.

다음은 MACHINE과 DISTRO 선택이다. 예전에는 local.conf에 `MACHINE = "..."`를 직접 썼다(지금도 된다). Wrynose에는 **fragment** 방식이 새로 생겼다. `machine/<이름>`, `distro/<이름>`은 내장 fragment라서 아무 MACHINE/DISTRO 이름이나 붙일 수 있다.

```sh
bitbake-config-build list-fragments 2>&1 | sed -n 2,4p
bitbake-config-build enable-fragment machine/k4-sensorkit distro/k4-distro
cat conf/toolcfg.conf
```

```text
Available built-in fragments:
machine/...	Sets MACHINE = ...
distro/...	Sets DISTRO = ...
Fragment machine/k4-sensorkit added to /var/tmp/K4/k4_build/conf/toolcfg.conf.
Fragment distro/k4-distro added to /var/tmp/K4/k4_build/conf/toolcfg.conf.
# Automated config file controlled by tools
#
# Run 'bitbake-config-build enable-fragment <fragment-name>' to enable additional fragments
# or replace built-in ones (e.g. machine/<name> or distro/<name> to change MACHINE or DISTRO).

OE_FRAGMENTS += "machine/k4-sensorkit distro/k4-distro"
```

출력에서 볼 것: 도구가 고치는 파일(`toolcfg.conf`)과 사람이 고치는 파일(`local.conf`)이 분리되었다. `k4-sensorkit`, `k4-distro`는 9절에서 만든 예제 layer의 MACHINE/DISTRO다.

### 2.4 bitbake-getvar — "이 변수의 최종 값과 출처"

설정 디버깅의 1번 도구다. 예전 `bitbake -e | grep`을 대신한다. 설정 단계 변수는 레시피를 파싱하지 않아도 바로 나온다.

```sh
for v in MACHINE DISTRO MACHINEOVERRIDES DISTROOVERRIDES TUNE_FEATURES TUNE_PKGARCH MULTIMACH_TARGET_SYS \
         PACKAGE_CLASSES PREFERRED_PROVIDER_virtual/kernel KERNEL_IMAGETYPE BB_NUMBER_THREADS PARALLEL_MAKE; do
  printf "%s=%s\n" $v "$(bitbake-getvar --value -q $v 2>/dev/null)"
done
```

```text
MACHINE=k4-sensorkit
DISTRO=k4-distro
MACHINEOVERRIDES=qemuall:aarch64:k4-sensorkit
DISTROOVERRIDES=k4-distro
TUNE_FEATURES=aarch64 crc cortexa57
TUNE_PKGARCH=cortexa57
MULTIMACH_TARGET_SYS=cortexa57-poky-linux
PACKAGE_CLASSES=package_rpm
PREFERRED_PROVIDER_virtual/kernel=linux-yocto
KERNEL_IMAGETYPE=Image
BB_NUMBER_THREADS=6
PARALLEL_MAKE=-j 6
```

출력에서 볼 것:

- `MULTIMACH_TARGET_SYS=cortexa57-poky-linux` — 이게 작업 디렉터리 경로(`tmp/work/cortexa57-poky-linux/...`)의 아키텍처 부분이다. 같은 CPU tune을 쓰는 보드끼리는 대부분의 패키지를 공유한다.
- `BB_NUMBER_THREADS`(동시에 돌 BitBake 태스크 수)와 `PARALLEL_MAKE`(태스크 하나 안의 `make -j`)는 기본값이 CPU 수(6)로 잡힌다.
- **`MACHINEOVERRIDES`에 `qemuarm64`가 없다.** k4-sensorkit.conf가 qemuarm64.conf를 `require`했는데도 그렇다. 이게 MACHINE 상속의 대표적인 함정이고 7.1절에서 고친다.

### 2.5 local.conf의 필수 항목

템플릿 local.conf에서 주석을 뺀 실제 내용은 몇 줄 안 된다.

```sh
grep -v "^#" /var/tmp/K4/k4_oe-core/meta/conf/templates/default/local.conf.sample | grep -v "^\s*$"
```

```text
MACHINE ??= "qemux86-64"
USER_CLASSES ?= "buildstats"
PATCHRESOLVE = "noop"
BB_DISKMON_DIRS ??= "\
    STOPTASKS,${TMPDIR},1G,100K \
    STOPTASKS,${DL_DIR},1G,100K \
    STOPTASKS,${SSTATE_DIR},1G,100K \
    STOPTASKS,/tmp,100M,100K \
    HALT,${TMPDIR},100M,1K \
    HALT,${DL_DIR},100M,1K \
    HALT,${SSTATE_DIR},100M,1K \
    HALT,/tmp,10M,1K"
CONF_VERSION = "2"
```

출력에서 볼 것: `BB_DISKMON_DIRS`는 디스크가 1 GB 아래로 떨어지면 새 태스크를 멈추고(STOPTASKS), 100 MB 아래면 즉시 중단(HALT)한다. Yocto 빌드가 디스크를 다 채워 호스트를 망가뜨리는 것을 막는 장치다.

실무에서 local.conf에 추가하는 항목을 정리하면 다음과 같다. **(실행하지 않음 — 가이드: 실제 빌드용 local.conf 예시)**

```text
# --- 무엇을 위해 빌드하나 (fragment를 안 쓸 때) ---
MACHINE ?= "qemuarm64"
DISTRO ?= "poky"

# --- 캐시는 빌드 디렉터리 밖에 두고 여러 빌드·팀이 공유 ---
DL_DIR ?= "/srv/yocto/downloads"
SSTATE_DIR ?= "/srv/yocto/sstate-cache"

# --- 병렬도: 기본값(CPU 수)이 보통 맞다. RAM이 부족하면 줄인다 ---
BB_NUMBER_THREADS ?= "8"
PARALLEL_MAKE ?= "-j 8"

# --- 다 만든 레시피의 작업 디렉터리를 지워 디스크 절약 (yocto-docs 기준 140 GB → 40 GB) ---
INHERIT += "rm_work"
RM_WORK_EXCLUDE += "k4-imu-reader"

# --- 패키지 형식: poky는 rpm, OE 기본은 ipk ---
PACKAGE_CLASSES ?= "package_ipk"

# --- 개발 빌드에서만: 빈 root 비밀번호로 로그인 (제품 빌드 금지) ---
EXTRA_IMAGE_FEATURES ?= "allow-empty-password empty-root-password allow-root-login"
```

각 항목의 이유:

- `DL_DIR`은 받은 소스 tarball·git 미러, `SSTATE_DIR`은 태스크 결과 캐시다. 빌드 디렉터리를 지워도 이 둘이 남아 있으면 다음 빌드는 몇 분 만에 끝난다. 팀·CI 공유 설계는 K5에서 다룬다.
- `rm_work`는 레시피가 끝나면 `tmp/work/.../<PN>/<PV>/`를 지운다. 디버깅 중인 레시피는 `RM_WORK_EXCLUDE`에 넣어 로그와 중간 산출물을 남긴다.
- 개발용 로그인 편의 기능은 예전에 `debug-tweaks` 하나로 켰지만 5.2에서 제거되었다. 지금은 위처럼 개별 기능을 나열한다(OE-Core에는 같은 일을 하는 `core/yocto/root-login-with-empty-password` fragment도 있다).

---

## 3. 변수 문법 — 대입 연산자, override, 인라인 Python

BitBake 메타데이터는 결국 **변수 대입의 연속**이다. 그런데 대입 연산자가 열 가지 가까이 되고, "언제 계산되나"와 "어느 것이 이기나" 규칙이 C와 완전히 다르다. 면접에서도 실무에서도 여기서 가장 많이 틀린다.

### 3.1 연산자 한눈에 보기

| 연산자 | 뜻 | 계산 시점 | 대표 용도 |
|---|---|---|---|
| `A = "v"` | 대입 (덮어씀) | 읽을 때 확장 (지연) | 레시피의 대부분 |
| `A ?= "v"` | 아직 값이 없을 때만 대입 | 그 줄을 읽을 때 판단 | machine/distro conf의 기본값 |
| `A ??= "v"` | 가장 약한 기본값. 파싱이 다 끝나도 아무도 안 정했을 때만 | 파싱 끝 | 템플릿 local.conf의 `MACHINE ??=` |
| `A := "v"` | 즉시 확장해서 대입 | 그 줄에서 | 경로를 그 시점 값으로 고정 (`${THISDIR}`) |
| `A += "v"` | 뒤에 공백 + v | 그 줄에서 붙임 | 목록에 추가 |
| `A =+ "v"` | 앞에 v + 공백 | 그 줄에서 붙임 | 목록 앞에 추가 |
| `A .= "v"` | 뒤에 공백 없이 붙임 | 그 줄에서 붙임 | 경로·문자열 이어 붙이기 (`BBPATH .=`) |
| `A =. "v"` | 앞에 공백 없이 붙임 | 그 줄에서 붙임 | `MACHINEOVERRIDES =. "x:"` |
| `A:append = "v"` | 뒤에 붙임 (공백 자동 없음) | **모든 파싱이 끝난 뒤** | bbappend·local.conf에서 안전하게 추가 |
| `A:prepend = "v"` | 앞에 붙임 (공백 자동 없음) | 모든 파싱이 끝난 뒤 | `FILESEXTRAPATHS:prepend` |
| `A:remove = "v"` | 공백으로 나눈 단어 중 v를 제거 | 가장 마지막 | 기본 기능 끄기 (`DISTRO_FEATURES:remove`) |

핵심은 두 줄이다. **`+=` `.=` 같은 연산자는 "그 줄을 읽는 순간" 붙이고, `:append` `:prepend` `:remove`는 "파싱이 다 끝난 뒤" 적용된다.** 그리고 `:append`는 공백을 넣어 주지 않으니 값 앞에 공백을 직접 쓴다.

### 3.2 실제 BitBake로 확인하기

규칙을 외우는 대신 BitBake에게 물어보자. 데모 변수를 담은 파일을 만들고, 빌드 디렉터리의 local.conf 끝에 `require /Users/donh/.elinux_scratch/K4/k4_demo_vars.conf` 한 줄을 넣었다.

무엇을 확인하나: 각 연산자를 섞었을 때 최종 값.

```text
# ---- K4 demo: 대입 연산자 ----
K4_A = "one"
K4_A ?= "two"
K4_B ??= "weak"
K4_B ?= "soft"
K4_C = "base"
K4_C += "plus"
K4_C =+ "pre"
K4_C .= "-dot"
K4_C =. "dot-"
# ---- :append / :remove 는 마지막에 적용된다 ----
K4_D = "x"
K4_D:append = " app"
K4_D += "late"
K4_D:remove = "x"
# ---- = (지연 확장) vs := (즉시 확장) ----
K4_LATER = "before"
K4_E = "${K4_LATER}"
K4_F := "${K4_LATER}"
K4_F2 := "${K4_NOT_YET}"
K4_LATER = "after"
K4_NOT_YET = "defined-later"
# ---- override: OVERRIDES에서 오른쪽에 있을수록 이긴다 ----
K4_G = "generic"
K4_G:aarch64 = "for-aarch64"
K4_G:qemuarm64 = "for-qemuarm64"
K4_G:k4-sensorkit = "for-k4"
K4_G:qemux86-64 = "for-x86"
K4_H = "a"
K4_H:append:k4-sensorkit = " k4-only"
K4_H:append:qemux86-64 = " x86-only"
# ---- 인라인 python ----
K4_PY = "${@'big' if int(d.getVar('BB_NUMBER_THREADS')) > 4 else 'small'}"
```

```sh
for v in K4_A K4_B K4_C K4_D K4_E K4_F K4_F2 K4_G K4_H K4_PY; do
  printf "%-6s = \"%s\"\n" $v "$(bitbake-getvar --value -q $v 2>/dev/null)"
done
```

```text
K4_A   = "one"
K4_B   = "soft"
K4_C   = "dot-pre base plus-dot"
K4_D   = " late app"
K4_E   = "after"
K4_F   = "before"
K4_F2  = "defined-later"
K4_G   = "for-k4"
K4_H   = "a k4-only"
K4_PY  = "big"
```

출력에서 볼 것:

- `K4_A`: 이미 값이 있으니 `?=`는 무시된다.
- `K4_B`: `??=`(가장 약함)보다 `?=`가 이긴다.
- `K4_C`: 줄 순서대로 `base` → `base plus` → `pre base plus` → `pre base plus-dot` → `dot-pre base plus-dot`.
- `K4_D`: `+=`가 `:append`보다 **뒤 줄**에 있는데도 결과는 `late app` 순서다. `:append`는 파싱이 끝난 뒤 적용되기 때문이다. 그리고 `:remove`가 `x`를 지운 자리에 앞 공백이 남았다. 목록 변수에서는 무해하지만, 문자열 비교를 하는 곳이면 함정이 된다.
- `K4_E`와 `K4_F`: `=`는 **읽을 때** 확장하므로 마지막 값 `after`, `:=`는 그 줄에서 확장해 `before`로 고정.
- `K4_F2`: `:=`인데도 `defined-later`다. 그 줄 시점에 `K4_NOT_YET`이 정의되지 않아 `${K4_NOT_YET}` 문자열이 그대로 저장됐다가 나중에 확장된 것이다. "`:=`면 무조건 고정"이 아니다.
- `K4_PY`: `${@ ... }`는 파싱 중에 도는 Python 식이다. `d`는 datastore 객체다.

### 3.3 변수 history — "누가 이 값을 바꿨나"

`bitbake-getvar`는 값만이 아니라 **어느 파일 몇 번째 줄이 어떤 연산을 했는지**를 보여 준다. 실무에서 "왜 내 설정이 안 먹지?"의 답은 거의 항상 여기서 나온다.

```sh
bitbake-getvar K4_D
```

```text
#
# $K4_D [4 operations]
#   set /Users/donh/.elinux_scratch/K4/k4_demo_vars.conf:12
#     "x"
#   :append /Users/donh/.elinux_scratch/K4/k4_demo_vars.conf:13
#     " app"
#   append /Users/donh/.elinux_scratch/K4/k4_demo_vars.conf:14
#     "late"
#   :remove /Users/donh/.elinux_scratch/K4/k4_demo_vars.conf:15
#     "x"
# pre-expansion value:
#   "x late app"
K4_D=" late app"
```

출력에서 볼 것: `append`(= `+=`)와 `:append`가 다른 연산으로 기록된다. "pre-expansion value"는 `:remove` 전 값이다.

### 3.4 override — 조건부 변수

**override**는 변수 이름 뒤에 `:<조건>`을 붙여 "이 조건일 때만 이 값"을 주는 문법이다. 조건 목록은 `OVERRIDES` 변수 하나에 콜론으로 이어져 있고, **오른쪽에 있을수록 우선순위가 높다.** C의 `#ifdef BOARD_X`를 변수 단위로 하는 셈이다.

```sh
bitbake-getvar --value OVERRIDES
grep -n "^OVERRIDES\|^DISTROOVERRIDES\|^MACHINEOVERRIDES\|^FILESOVERRIDES" /var/tmp/K4/k4_oe-core/meta/conf/bitbake.conf
```

```text
linux:aarch64:pn-defaultpkgname:layer-config:qemuarm64:qemuall:aarch64:k4-sensorkit:k4-distro:class-target:${TCOVERRIDE}:libc-glibc:forcevariable
800:OVERRIDES = "${TARGET_OS}:${TRANSLATED_TARGET_ARCH}:pn-${PN}:layer-${FILE_LAYERNAME}:${MACHINEOVERRIDES}:${DISTROOVERRIDES}:${CLASSOVERRIDE}:${TCOVERRIDE}${LIBCOVERRIDE}:forcevariable"
804:DISTROOVERRIDES ?= "${@d.getVar('DISTRO') or ''}"
805:MACHINEOVERRIDES ?= "${MACHINE}"
807:FILESOVERRIDES = "${TRANSLATED_TARGET_ARCH}:${MACHINEOVERRIDES}:${DISTROOVERRIDES}"
```

출력에서 볼 것:

- 순서는 OS(`linux`) → 아키텍처(`aarch64`) → 레시피 이름(`pn-<PN>`) → layer → **MACHINE 계열** → **DISTRO** → 클래스(`class-target`, native 레시피면 `class-native`) → `forcevariable`. 그래서 같은 변수에 `:aarch64`와 `:k4-sensorkit`이 둘 다 있으면 MACHINE 쪽이 이기고, `:k4-distro`가 있으면 그게 더 이긴다.
- 설정 단계에서 물었기 때문에 `pn-defaultpkgname`, `layer-config`, `${TCOVERRIDE}`처럼 레시피에서 정해질 부분이 기본값으로 나온다. 레시피를 파싱하면 `pn-k4-imu-reader` 같은 값이 된다.
- `forcevariable`은 맨 오른쪽이라 무엇보다 강하다. `A:forcevariable = "..."`는 디버깅용 "최후의 수단"이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="14.0" y="22.0" font-size="13" text-anchor="start" font-weight="bold">OVERRIDES (MACHINE=k4-sensorkit, 실제 값) — 오른쪽일수록 우선</text><rect x="8" y="34" width="56.61538461538461" height="26" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="36.3" y="50.0" font-size="12" text-anchor="middle">linux</text><rect x="69.61538461538461" y="34" width="70.46153846153845" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="104.8" y="50.0" font-size="12" text-anchor="middle">aarch64</text><rect x="145.07692307692307" y="34" width="84.3076923076923" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="187.2" y="50.0" font-size="12" text-anchor="middle">qemuarm64</text><rect x="234.38461538461536" y="34" width="70.46153846153845" height="26" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="269.6" y="50.0" font-size="12" text-anchor="middle">qemuall</text><rect x="309.8461538461538" y="34" width="105.07692307692308" height="26" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="362.4" y="50.0" font-size="12" text-anchor="middle">k4-sensorkit</text><rect x="419.9230769230769" y="34" width="84.3076923076923" height="26" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="462.1" y="50.0" font-size="12" text-anchor="middle">k4-distro</text><rect x="509.2307692307692" y="34" width="105.07692307692308" height="26" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="561.8" y="50.0" font-size="12" text-anchor="middle">class-target</text><rect x="619.3076923076923" y="34" width="34.0" height="26" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="636.3" y="50.0" font-size="12" text-anchor="middle">…</text><line x1="14" y1="72" x2="660" y2="72" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/><text x="14.0" y="100.0" font-size="13" text-anchor="start" font-weight="bold">K4_G 후보 (local.conf에서 require한 데모 파일)</text><rect x="14" y="110" width="300" height="26" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="24.0" y="126.0" font-size="12" text-anchor="start">K4_G = "generic"</text><text x="330.0" y="128.0" font-size="12" text-anchor="start">override 없음 — 기본값</text><rect x="14" y="142" width="300" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="24.0" y="158.0" font-size="12" text-anchor="start">K4_G:aarch64 = "for-aarch64"</text><text x="330.0" y="160.0" font-size="12" text-anchor="start">aarch64 활성 → 후보</text><rect x="14" y="174" width="300" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="24.0" y="190.0" font-size="12" text-anchor="start">K4_G:qemuarm64 = "for-qemuarm64"</text><text x="330.0" y="192.0" font-size="12" text-anchor="start">qemuarm64 활성 → 후보</text><rect x="14" y="206" width="300" height="26" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="24.0" y="222.0" font-size="12" text-anchor="start">K4_G:k4-sensorkit = "for-k4"</text><text x="330.0" y="224.0" font-size="12" text-anchor="start">가장 오른쪽 활성 override → 승리</text><rect x="14" y="238" width="300" height="26" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="24.0" y="254.0" font-size="12" text-anchor="start">K4_G:qemux86-64 = "for-x86"</text><text x="330.0" y="256.0" font-size="12" text-anchor="start">비활성 → 무시</text><rect x="14" y="276" width="316" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="172.0" y="291.0" font-size="12" text-anchor="middle">k4-sensorkit → "for-k4"</text><text x="172.0" y="307.0" font-size="12" text-anchor="middle">qemux86-64 → "for-x86"</text><text x="346.0" y="290.0" font-size="12" text-anchor="start">적용 순서: 일반 대입(= ?= += …)</text><text x="346.0" y="308.0" font-size="12" text-anchor="start">→ override 선택 → :append/:prepend → :remove</text></svg>
```
그림 4 — override 선택. OVERRIDES에 있는 조건 중 값이 정의된 것들 가운데 가장 오른쪽이 이긴다. 실제 전체 값은 위 출력(`linux:aarch64:pn-…:forcevariable`)이고, 그림은 일부만 보였다.

history에서도 override 후보가 따로 기록된다.

```sh
bitbake-getvar K4_G
```

```text
#
# $K4_G [5 operations]
#   set /Users/donh/.elinux_scratch/K4/k4_demo_vars.conf:24
#     "generic"
#   override[aarch64]:set /Users/donh/.elinux_scratch/K4/k4_demo_vars.conf:25
#     "for-aarch64"
#   override[qemuarm64]:set /Users/donh/.elinux_scratch/K4/k4_demo_vars.conf:26
#     "for-qemuarm64"
#   override[k4-sensorkit]:set /Users/donh/.elinux_scratch/K4/k4_demo_vars.conf:27
#     "for-k4"
#   override[qemux86-64]:set /Users/donh/.elinux_scratch/K4/k4_demo_vars.conf:28
#     "for-x86"
# pre-expansion value:
#   "for-k4"
K4_G="for-k4"
```

MACHINE를 바꾸면 같은 메타데이터에서 다른 값이 나온다. fragment로 MACHINE을 qemux86-64로 바꿔 물어보고, 다시 되돌렸다.

```sh
bitbake-config-build enable-fragment -q machine/qemux86-64
grep OE_FRAG conf/toolcfg.conf
for v in MACHINE MACHINEOVERRIDES K4_G K4_H TUNE_PKGARCH; do
  printf "%-17s = \"%s\"\n" $v "$(bitbake-getvar --value -q $v 2>/dev/null)"
done
bitbake-config-build enable-fragment -q machine/k4-sensorkit
grep OE_FRAG conf/toolcfg.conf
```

```text
Fragment machine/qemux86-64 added to /var/tmp/K4/k4_build/conf/toolcfg.conf.
OE_FRAGMENTS += "distro/k4-distro machine/qemux86-64"
MACHINE           = "qemux86-64"
MACHINEOVERRIDES  = "qemuall:qemux86-64"
K4_G              = "for-x86"
K4_H              = "a x86-only"
TUNE_PKGARCH      = "x86-64-v3"
Fragment machine/k4-sensorkit added to /var/tmp/K4/k4_build/conf/toolcfg.conf.
OE_FRAGMENTS += "distro/k4-distro machine/k4-sensorkit"
```

출력에서 볼 것: `machine/` fragment는 하나만 활성화된다(새로 켜면 이전 것을 대체). x86에서는 `:aarch64` 계열이 모두 비활성이라 `for-x86`이 선택되고, `:append:qemux86-64`만 붙는다. `:append:<override>`처럼 **연산과 조건을 겹쳐 쓸 수 있다** — "이 MACHINE일 때만 뒤에 붙여라"다.

### 3.5 varflag — 변수에 붙는 속성

변수에는 값 말고 **flag**(속성)도 붙는다. 문법은 `VAR[flag] = "..."`. 태스크의 동작 방식을 정하는 데 많이 쓴다.

```sh
grep -n "\[network\]\|do_install\[cleandirs\]\|do_compile\[dirs\]" /var/tmp/K4/k4_oe-core/meta/classes-global/base.bbclass
```

```text
180:do_fetch[network] = "1"
398:do_compile[dirs] = "${B}"
410:do_install[cleandirs] = "${D}"
```

출력에서 볼 것: `do_fetch`만 네트워크를 쓸 수 있다(10절에서 이 flag가 AppArmor 문제와 연결된다). `do_compile`은 `${B}`에서 실행되고, `do_install`은 시작할 때 `${D}`를 비운다 — 그래서 do_install은 몇 번을 다시 돌려도 이전 실행의 찌꺼기가 남지 않는다. 그 밖에 `[depends]`(다른 레시피의 태스크에 의존), `[vardeps]`(이 태스크 해시에 포함할 변수), `[doc]`(설명) 등이 있다.

### 3.6 가장 유명한 함정 — local.conf의 `IMAGE_INSTALL +=`

"이미지에 패키지 하나 넣으려고 local.conf에 `IMAGE_INSTALL += "foo"`를 썼더니 이미지가 부팅을 안 한다"는 Yocto 입문자의 고전이다. 실제로 재현했다. local.conf 끝에 한 줄을 넣고 두 이미지 레시피를 파싱해 `IMAGE_INSTALL`을 본 뒤, `:append`로 바꿔 다시 봤다(파싱 도구 `$P`는 4.4절에서 설명한다).

```sh
LC=conf/local.conf; B=$OE/meta/recipes-core/images/core-image-base.bb
Mi=$OE/meta/recipes-core/images/core-image-minimal.bb
cp $LC /var/tmp/K4/k4_local.conf.bak
echo 'IMAGE_INSTALL += "k4-imu-reader"' >> $LC; tail -1 $LC
for r in $B $Mi; do $P $r IMAGE_INSTALL 2>&1 | grep -v ^NOTE; done
cp /var/tmp/K4/k4_local.conf.bak $LC
echo 'IMAGE_INSTALL:append = " k4-imu-reader"' >> $LC; tail -1 $LC
for r in $B $Mi; do $P $r IMAGE_INSTALL 2>&1 | grep -v ^NOTE; done
cp /var/tmp/K4/k4_local.conf.bak $LC
```

```text
IMAGE_INSTALL += "k4-imu-reader"
IMAGE_INSTALL          = ' k4-imu-reader'
IMAGE_INSTALL          = 'packagegroup-core-boot '
IMAGE_INSTALL:append = " k4-imu-reader"
IMAGE_INSTALL          = '    packagegroup-core-boot     packagegroup-base-extended               k4-imu-reader'
IMAGE_INSTALL          = 'packagegroup-core-boot  k4-imu-reader'
```

출력에서 볼 것 — 두 가지 다른 방식으로 망가진다:

- **core-image-base**: core-image.bbclass는 `IMAGE_INSTALL ?= "${CORE_IMAGE_BASE_INSTALL}"`이다. local.conf가 먼저 읽히면서 `+=`로 이미 값(` k4-imu-reader`)을 만들어 버렸으니 `?=` 기본값이 무시된다. **이미지에 앱 하나만 들어가고 부팅에 필요한 packagegroup-core-boot가 빠진다.**
- **core-image-minimal**: 레시피가 `IMAGE_INSTALL = "packagegroup-core-boot ..."`로 **덮어쓰기** 때문에 local.conf의 추가가 조용히 사라진다.
- `:append`는 모든 대입이 끝난 뒤 적용되므로 두 경우 모두 의도대로 된다. 규칙: **local.conf와 bbappend에서는 `+=` 대신 `:append`(앞에 공백 포함)를 쓴다.** 이미지에 넣는 정석은 local.conf가 아니라 이미지 레시피나 packagegroup이다(7.4절).

---

## 4. 레시피 해부 — 소프트웨어 하나를 빌드하는 방법

### 4.1 가장 작은 레시피

OE-Core의 meta-skeleton에 있는 "hello world" 레시피가 출발점이다.

```sh
cat /var/tmp/K4/k4_oe-core/meta-skeleton/recipes-skeleton/hello-single/hello_1.0.bb
```

```text
DESCRIPTION = "Simple helloworld application"
SECTION = "examples"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://helloworld.c"

S = "${UNPACKDIR}"

do_compile() {
	${CC} ${LDFLAGS} helloworld.c -o helloworld
}

do_install() {
	install -d ${D}${bindir}
	install -m 0755 helloworld ${D}${bindir}
}
```

한 줄씩 보면:

- `LICENSE`: 이 소프트웨어의 라이선스(SPDX 식별자). 이미지에 들어갈 수 있는지, 소스 공개 의무가 있는지를 판단하는 데 쓰인다(K7).
- `LIC_FILES_CHKSUM`: 라이선스 **텍스트 파일의 md5**. 업스트림이 라이선스를 바꾸면 md5가 달라져 빌드가 멈춘다 — 사람이 새 라이선스를 검토하라는 장치다.
- `SRC_URI`: 소스를 어디서 가져오나. `file://`은 레시피 옆의 파일 디렉터리(FILESPATH)에서 찾는다.
- `S`: 소스가 풀린 디렉터리. 파일 몇 개뿐이면 `${UNPACKDIR}` 자체다.
- `do_compile`, `do_install`: 셸 함수로 쓴 태스크. `${CC}`는 크로스 컴파일러와 옵션이 다 들어간 변수다.
- `${D}${bindir}`: `D`는 "가짜 루트"(설치 대상 디렉터리), `bindir`은 `/usr/bin`. **절대로 `/usr/bin`에 직접 설치하지 않는다.** make의 `DESTDIR`과 같은 개념이다.

MIT 라이선스의 md5가 맞는지 직접 계산해 보자.

```sh
md5sum /var/tmp/K4/k4_oe-core/meta/files/common-licenses/MIT /var/tmp/K4/k4_oe-core/meta/files/common-licenses/GPL-2.0-only
ls /var/tmp/K4/k4_oe-core/meta/files/common-licenses | wc -l
```

```text
0835ade698e0bcf8506ecda2f7b4f302  /var/tmp/K4/k4_oe-core/meta/files/common-licenses/MIT
801f80980d171dd6425610833a22dbe6  /var/tmp/K4/k4_oe-core/meta/files/common-licenses/GPL-2.0-only
732
```

출력에서 볼 것: 레시피의 md5와 같다. OE-Core는 SPDX 라이선스 텍스트 732개를 `COMMON_LICENSE_DIR`에 갖고 있다.

### 4.2 우리 레시피 — k4-imu-reader

센서 키트의 IMU 값을 IIO sysfs(I2절)에서 읽어 한 줄 찍는 앱을 레시피로 만든다. 먼저 앱 소스다.

```c
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Don H.
// k4_imu_reader: IIO 가속도 raw 값을 읽어 한 줄로 출력하는 예제 앱
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEF_CONF "/etc/k4-imu.conf"

/* conf 파일에서 "iio_dir=<경로>" 한 줄을 읽는다 */
static int read_conf(const char *path, char *dir, size_t len)
{
	FILE *f = fopen(path, "r");
	char line[256];

	if (!f) { perror(path); return -1; }
	while (fgets(line, sizeof line, f)) {
		if (strncmp(line, "iio_dir=", 8) == 0) {
			line[strcspn(line, "\n")] = '\0';
			snprintf(dir, len, "%s", line + 8);
			fclose(f);
			return 0;
		}
	}
	fclose(f);
	fprintf(stderr, "%s: no iio_dir= line\n", path);
	return -1;
}

static long read_long(const char *dir, const char *name)
{
	char p[512];
	long v = 0;
	FILE *f;

	snprintf(p, sizeof p, "%s/%s", dir, name);
	f = fopen(p, "r");
	if (!f) { perror(p); exit(1); }
	if (fscanf(f, "%ld", &v) != 1) { fprintf(stderr, "%s: parse error\n", p); exit(1); }
	fclose(f);
	return v;
}

int main(int argc, char **argv)
{
	char dir[256];
	const char *conf = argc > 1 ? argv[1] : DEF_CONF;

	if (read_conf(conf, dir, sizeof dir) < 0)
		return 1;
	printf("k4-imu-reader 1.0: x=%ld y=%ld z=%ld (dir=%s)\n",
	       read_long(dir, "in_accel_x_raw"), read_long(dir, "in_accel_y_raw"),
	       read_long(dir, "in_accel_z_raw"), dir);
	return 0;
}
```

Yocto에 넣기 전에 VM에서 네이티브로 먼저 확인한다. IIO 장치 대신 가짜 sysfs 디렉터리를 만들었다.

```sh
F=/Users/donh/.elinux_scratch/K4/meta-k4-sensorkit/recipes-apps/k4-imu-reader/files
mkdir -p /tmp/K4/fakeiio && cd /tmp/K4
gcc -std=gnu11 -Wall -Wextra -O2 $F/k4_imu_reader.c -o k4-imu-reader
echo 12 > fakeiio/in_accel_x_raw; echo -3 > fakeiio/in_accel_y_raw; echo 1024 > fakeiio/in_accel_z_raw
echo "iio_dir=/tmp/K4/fakeiio" > test.conf
./k4-imu-reader test.conf
./k4-imu-reader $F/k4-imu.conf; echo rc=$?
sed -n 1,2p $F/k4_imu_reader.c | md5sum
```

```text
k4-imu-reader 1.0: x=12 y=-3 z=1024 (dir=/tmp/K4/fakeiio)
/sys/bus/iio/devices/iio:device0/in_accel_x_raw: No such file or directory
rc=1
7654d15980fd39016d1da4db8481522d  -
```

출력에서 볼 것: 경고 0개로 빌드되고, 기본 설정(`iio:device0`)은 VM에 IIO 장치가 없어 실패한다(rc=1) — 정상이다. 마지막 줄은 **소스 파일 1~2행(SPDX 줄과 Copyright 줄)의 md5**다. 라이선스 파일이 따로 없는 작은 앱은 이렇게 헤더 줄 범위로 체크섬을 건다. OE-Core `insane.bbclass`가 `beginline`/`endline` 사이 줄을 바이트 그대로 md5에 넣는 방식과 `sed -n 1,2p | md5sum`이 같은 계산이다.

이제 레시피다.

```text
SUMMARY = "K4 sensor kit IMU sample reader"
DESCRIPTION = "Reads in_accel_{x,y,z}_raw from an IIO device and prints one line."
HOMEPAGE = "https://example.invalid/k4-sensorkit"
SECTION = "examples"

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://k4_imu_reader.c;beginline=1;endline=2;md5=7654d15980fd39016d1da4db8481522d"

SRC_URI = "file://k4_imu_reader.c \
           file://k4-imu.conf \
           file://k4-imu-reader.service \
           "

# file:// 소스는 압축 해제할 디렉터리가 없으므로 UNPACKDIR 자체가 소스 폴더
S = "${UNPACKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "k4-imu-reader.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_compile() {
    ${CC} ${CFLAGS} ${LDFLAGS} -o k4-imu-reader ${S}/k4_imu_reader.c
}

do_install() {
    install -d ${D}${bindir} ${D}${sysconfdir} ${D}${systemd_system_unitdir}
    install -m 0755 ${B}/k4-imu-reader ${D}${bindir}/
    install -m 0644 ${S}/k4-imu.conf ${D}${sysconfdir}/
    install -m 0644 ${S}/k4-imu-reader.service ${D}${systemd_system_unitdir}/
}

# 설정 파일은 별도 패키지로 나눠, 제품마다 다른 conf 패키지로 바꿔 끼울 수 있게 한다
PACKAGES =+ "${PN}-conf"
FILES:${PN}-conf = "${sysconfdir}/k4-imu.conf"
CONFFILES:${PN}-conf = "${sysconfdir}/k4-imu.conf"
FILES:${PN} += "${systemd_system_unitdir}/k4-imu-reader.service"

RDEPENDS:${PN} = "${PN}-conf"
```

skeleton 레시피보다 늘어난 것:

- `CFLAGS`를 넘긴다. skeleton처럼 `LDFLAGS`만 넘기면 DISTRO가 정한 보안 플래그(`-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2` …)와 최적화가 빠진다. 이 정도로는 빌드가 멈추지 않지만, 디버그 경로 매핑(`-ffile-prefix-map`)이 빠지면 `do_package_qa`가 "빌드 경로가 바이너리에 남았다"(buildpaths)고 경고할 수 있다.
- `inherit systemd`: 서비스를 패키지에 등록하고 `systemctl enable`을 설치 후 스크립트로 처리하는 클래스.
- `PACKAGES =+`, `FILES:`, `CONFFILES:`, `RDEPENDS:`: 6절에서 다룬다.

### 4.3 레시피 파일을 BitBake 파서로 검증하기

빌드는 못 하지만, **BitBake의 파서는 돌릴 수 있다.** BitBake는 Python 라이브러리 API인 **tinfoil**을 제공한다(`bitbake-layers`, `devtool`도 tinfoil로 만들어져 있다). 설정만 파싱한 뒤 레시피 파일 하나를 파싱하면 레시피 datastore를 얻는다. 이건 태스크를 실행하지 않으므로 빌드가 아니다.

```python
# tinfoil(config_only)로 레시피 파일 하나만 파싱해 변수·태스크를 본다 (태스크 실행 없음)
import sys
sys.path.insert(0, "/var/tmp/K4/k4_bitbake/lib")
import bb.tinfoil

recipe, args = sys.argv[1], sys.argv[2:]
appends = [a[len("+append="):] for a in args if a.startswith("+append=")]
names = [a for a in args if not a.startswith("+")]
with bb.tinfoil.Tinfoil() as tf:
    tf.prepare(config_only=True, quiet=2)
    d = tf.parse_recipe_file(recipe, appends=bool(appends),
                             appendlist=appends or None)
    for n in names:
        if n == "@tasks":
            for t in d.getVar("__BBTASKS", False):
                deps = d.getVarFlag(t, "deps", False) or []
                print(f"{t:28} after {' '.join(deps)}")
        elif n == "@classes":
            inh = [c.split("/")[-1] for c in d.getVar("__inherit_cache", False)]
            print(" ".join(inh))
        elif n.startswith("@func:"):
            print(d.getVar(n[6:]))
        elif n.startswith("@flag:"):
            v, f = n[6:].split("[")
            print(f"{v}[{f[:-1]}] = {d.getVarFlag(v, f[:-1])!r}")
        else:
            print(f"{n:22} = {d.getVar(n)!r}")
```

빌드 환경을 잡는 셸 조각(`k4_env.sh`)과 함께 쓴다. 이후 예제의 `$P`는 이 스크립트, `$R`은 우리 레시피 경로다.

```sh
# source me inside the VM: OE build env for the K4 notes (no builds)
cd /var/tmp/K4/k4_oe-core && export BITBAKEDIR=/var/tmp/K4/k4_bitbake
. ./oe-init-build-env /var/tmp/K4/k4_build > /dev/null 2>&1
export PATH=$PATH:/var/tmp/K4/k4_fakebin
P="python3 /Users/donh/.elinux_scratch/K4/k4_parse_recipe.py"
R=/var/tmp/K4/k4_layers/meta-k4-sensorkit/recipes-apps/k4-imu-reader/k4-imu-reader_1.0.bb
```

무엇을 확인하나: 레시피의 PN/PV와 디렉터리 변수(S, B, D, T)가 어디로 잡히는지.

```sh
. /Users/donh/.elinux_scratch/K4/k4_env.sh
time $P $R PN PV PR WORKDIR S B D T
```

```text
NOTE: Reconnecting to bitbake server...
PN                     = 'k4-imu-reader'
PV                     = '1.0'
PR                     = 'r0'
WORKDIR                = '/var/tmp/K4/k4_build/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0'
S                      = '/var/tmp/K4/k4_build/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/sources'
B                      = '/var/tmp/K4/k4_build/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/sources'
D                      = '/var/tmp/K4/k4_build/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/image'
T                      = '/var/tmp/K4/k4_build/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/temp'

real	0m1.118s
user	0m0.159s
sys	0m0.008s
```

출력에서 볼 것: 약 1초 — 전역 설정 파싱 + 레시피 하나 파싱이다("Reconnecting"은 직전 명령이 띄운 BitBake 서버를 재사용했다는 뜻). 파일 이름 `k4-imu-reader_1.0.bb`에서 PN과 PV가 나왔다. 파싱이 에러 없이 끝났다는 것은 문법, `inherit systemd`, 변수 확장이 모두 통과했다는 뜻이다. `S`와 `B`가 같다 — 소스 디렉터리에서 그대로 빌드한다. autotools/cmake/meson 클래스는 `B`를 별도 디렉터리로 바꿔 out-of-tree 빌드를 한다.

do_compile이 **실제로 실행할 명령**도 볼 수 있다. 함수 본문을 확장하면 크로스 컴파일러와 플래그가 다 드러난다.

```sh
$P $R @func:do_compile TARGET_SYS 2>&1 | grep -v ^NOTE
```

```text
    aarch64-poky-linux-gcc  -mcpu=cortex-a57+crc -mbranch-protection=standard -fstack-protector-strong  -O2 -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Werror=format-security --sysroot=/var/tmp/K4/k4_build/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/recipe-sysroot  -O2 -g -fcanon-prefix-map  -ffile-prefix-map=/var/tmp/K4/k4_build/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/sources=/usr/src/debug/k4-imu-reader/1.0  -ffile-prefix-map=/var/tmp/K4/k4_build/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/sources=/usr/src/debug/k4-imu-reader/1.0  -ffile-prefix-map=/var/tmp/K4/k4_build/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/recipe-sysroot=  -ffile-prefix-map=/var/tmp/K4/k4_build/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/recipe-sysroot-native=  -pipe -Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed -Wl,-z,relro,-z,now -o k4-imu-reader /var/tmp/K4/k4_build/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/sources/k4_imu_reader.c

TARGET_SYS             = 'aarch64-poky-linux'
```

출력에서 볼 것 — B 모듈에서 배운 것이 전부 들어 있다:

- `aarch64-poky-linux-gcc`: Yocto가 직접 빌드한 크로스 컴파일러(B1). 이름의 `poky`는 DISTRO의 TARGET_VENDOR다.
- `-mcpu=cortex-a57+crc`: MACHINE의 tune(qemuarm64 → cortex-a57)에서 온다.
- `--sysroot=.../recipe-sysroot`: 이 레시피 전용 sysroot(B2). DEPENDS로 선언한 레시피의 헤더·라이브러리만 들어 있다. 선언하지 않은 라이브러리는 **보이지 않는다** — 이게 Yocto가 "숨은 의존성"을 잡아내는 방법이다.
- `-ffile-prefix-map=...=/usr/src/debug/...`: 디버그 정보에서 빌드 경로를 지워 재현 가능한 바이너리를 만든다(K5).
- `-Wl,-z,relro,-z,now`, `-D_FORTIFY_SOURCE=2`, `-fstack-protector-strong`: DISTRO가 거는 보안 하드닝 플래그.

### 4.4 SRC_URI와 FILESPATH — 소스는 어디서 오나

`SRC_URI`는 공백으로 나눈 URL 목록이다. 형식(fetcher)별로 정리하면 이렇다.

| 형식 | 예 | 메모 |
|---|---|---|
| `file://` | `file://0001-fix.patch` | 레시피 옆 디렉터리(FILESPATH)에서 찾는다. `.patch`·`.diff`는 do_patch가 자동 적용 |
| `https://` | `https://busybox.net/downloads/busybox-${PV}.tar.bz2` | `SRC_URI[sha256sum]`으로 무결성 검사 |
| `git://` | `git://github.com/x/y.git;protocol=https;branch=main` | `SRCREV`에 커밋 해시를 고정 |
| `npm://`, `crate://`, `gitsm://` 등 | 언어별 패키지·서브모듈 | 생태계별 fetcher |

`file://`이 어디를 뒤지는지는 `FILESPATH`가 정한다. busybox 레시피를 meta-poky와 우리 layer의 bbappend를 붙여 파싱해 봤다.

```sh
M=$OE/meta; PA=/var/tmp/K4/k4_meta-yocto/meta-poky/recipes-core/busybox/busybox_%.bbappend
BA=/var/tmp/K4/k4_layers/meta-k4-sensorkit/recipes-core/busybox/busybox_%.bbappend
$P $M/recipes-core/busybox/busybox_1.37.0.bb +append=$PA +append=$BA FILESPATH FILESOVERRIDES 2>&1 \
  | grep -v ^NOTE > /tmp/K4/fp.txt
tail -1 /tmp/K4/fp.txt
head -1 /tmp/K4/fp.txt | cut -d"'" -f2 | tr ":" "\n" | sed "s|/var/tmp/K4/||" | wc -l
head -1 /tmp/K4/fp.txt | cut -d"'" -f2 | tr ":" "\n" | sed "s|/var/tmp/K4/||" | awk "NR<=5 || NR>=31"
```

```text
FILESOVERRIDES         = 'aarch64:qemuarm64:qemuall:aarch64:k4-sensorkit:k4-distro'
35
k4_layers/meta-k4-sensorkit/recipes-core/busybox/busybox/k4-distro
k4_meta-yocto/meta-poky/recipes-core/busybox/busybox/k4-distro
k4_oe-core/meta/recipes-core/busybox/busybox-1.37.0/k4-distro
k4_oe-core/meta/recipes-core/busybox/busybox/k4-distro
k4_oe-core/meta/recipes-core/busybox/files/k4-distro
k4_layers/meta-k4-sensorkit/recipes-core/busybox/busybox/
k4_meta-yocto/meta-poky/recipes-core/busybox/busybox/
k4_oe-core/meta/recipes-core/busybox/busybox-1.37.0/
k4_oe-core/meta/recipes-core/busybox/busybox/
k4_oe-core/meta/recipes-core/busybox/files/
```

출력에서 볼 것: 검색 경로 35개 = (override 6개 + override 없음 1개) × 디렉터리 5개. 바깥 순서는 **override가 구체적인 것부터**(`k4-distro` → `k4-sensorkit` → … → override 없는 기본 디렉터리), 안쪽 순서는 **우리 layer → meta-poky → OE-Core**다. 그래서 우리 layer의 `busybox/defconfig`를 두면 OE-Core의 defconfig 대신 그게 잡히고, `busybox/k4-sensorkit/defconfig`에 두면 **이 MACHINE에서만** 바뀐다. 우리 layer와 meta-poky 디렉터리가 앞에 온 것은 두 bbappend의 `FILESEXTRAPATHS:prepend` 때문이다(8절).

### 4.5 빌드 시스템 클래스 — 직접 do_compile을 쓰지 않는 방법

현실의 소프트웨어는 대부분 autotools, CMake, Meson, 커널 Kbuild로 빌드된다. 레시피는 해당 클래스를 `inherit`만 하면 do_configure/do_compile/do_install이 채워진다.

```sh
cd /var/tmp/K4/k4_oe-core/meta
grep -n "^EXPORT_FUNCTIONS" classes-recipe/autotools.bbclass classes-recipe/cmake.bbclass \
     classes-recipe/meson.bbclass classes-recipe/module.bbclass
sed -n 397,405p classes-global/base.bbclass
```

```text
classes-recipe/autotools.bbclass:239:EXPORT_FUNCTIONS do_configure do_compile do_install
classes-recipe/cmake.bbclass:321:EXPORT_FUNCTIONS do_configure do_compile do_install do_generate_toolchain_file
classes-recipe/meson.bbclass:193:EXPORT_FUNCTIONS do_configure do_compile do_install
classes-recipe/module.bbclass:67:EXPORT_FUNCTIONS do_compile do_install
addtask compile after do_configure
do_compile[dirs] = "${B}"
base_do_compile() {
	if [ -e Makefile -o -e makefile -o -e GNUmakefile ]; then
		oe_runmake || die "make failed"
	else
		bbnote "nothing to compile"
	fi
}
```

출력에서 볼 것:

- `EXPORT_FUNCTIONS do_compile`은 "이 클래스를 inherit하면 `do_compile`이 `autotools_do_compile`을 가리킨다"는 뜻이다. 레시피에서 `do_compile()`을 직접 정의하면 클래스 것을 덮어쓰고, `do_compile:append()`로 뒤에 덧붙일 수도 있다. 클래스 원본을 부르고 싶으면 `autotools_do_compile`처럼 이름을 직접 부른다.
- 아무 클래스도 상속하지 않으면 `base_do_compile`이 기본이다 — Makefile이 있으면 `oe_runmake`(크로스 변수를 넘겨 make 실행), 없으면 아무 것도 안 한다. 그래서 단순 Makefile 프로젝트는 레시피에 `do_install`만 쓰면 되는 경우가 많다.

| 소스 형태 | 레시피에 쓸 것 | 클래스가 대신 하는 일 |
|---|---|---|
| `./configure && make` | `inherit autotools` | 크로스 configure 옵션, `make install DESTDIR=${D}` |
| `CMakeLists.txt` | `inherit cmake` | 툴체인 파일 생성, out-of-tree 빌드, `cmake --install` |
| `meson.build` | `inherit meson` | cross 파일 생성, ninja 빌드 |
| 커널 모듈(Kbuild) | `inherit module` | 커널 빌드 트리에 대고 `make -C ... M=...`, `kernel-module-*` 패키지 |
| Python 패키지 | `inherit python_setuptools_build_meta` 등 | wheel 빌드·설치 |

커널 모듈 레시피의 예도 meta-skeleton에 있다. IMU 드라이버를 out-of-tree로 넣을 때 이 형태를 쓴다(커널 레시피와 패치 관리는 K5).

```sh
cat /var/tmp/K4/k4_oe-core/meta-skeleton/recipes-kernel/hello-mod/hello-mod_0.1.bb
```

```text
SUMMARY = "Example of how to build an external Linux kernel module"
DESCRIPTION = "${SUMMARY}"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=12f884d2ae1ff87c09e5b7ccc2c4ca7e"

inherit module

SRC_URI = "file://Makefile \
           file://hello.c \
           file://COPYING \
          "

S = "${UNPACKDIR}"

# The inherit of module.bbclass will automatically name module packages with
# "kernel-module-" prefix as required by the oe-core build environment.

RPROVIDES:${PN} += "kernel-module-hello"
```

출력에서 볼 것: 레시피에는 빌드 명령이 한 줄도 없다. module.bbclass가 E2 노트에서 손으로 했던 `make -C <커널 빌드 트리> M=$PWD`를 크로스 환경으로 대신 실행한다.

---

## 5. 태스크 모델 — 레시피 하나가 지나가는 길

### 5.1 태스크 목록과 순서

레시피를 파싱하면 태스크 목록과 각 태스크의 선행 태스크(`addtask X after Y`)가 정해진다. 우리 레시피에서 꺼내 봤다(출력의 `_setscene`·spdx·devshell 계열 일부는 아래에서 설명한다).

```sh
$P $R @tasks 2>&1 | grep -v ^NOTE
```

```text
do_patch                     after do_unpack
do_populate_sysroot          after do_install
do_populate_sysroot_setscene after 
do_prepare_recipe_sysroot    after do_fetch
do_listtasks                 after 
do_clean                     after 
do_checkuri                  after 
do_fetch                     after do_recipe_qa
do_unpack                    after do_fetch
do_deploy_source_date_epoch_setscene after 
do_deploy_source_date_epoch  after do_patch
do_configure                 after do_deploy_source_date_epoch do_prepare_recipe_sysroot do_patch
do_compile                   after do_configure
do_install                   after do_compile
do_build                     after do_create_package_spdx do_create_spdx do_populate_lic do_package_write_rpm do_packagedata do_recipe_qa do_package_qa do_populate_sysroot
do_cleansstate               after do_clean
do_cleanall                  after do_cleansstate
do_package_qa                after do_recipe_qa do_packagedata do_package
do_package_qa_setscene       after 
do_recipe_qa                 after 
do_recipe_qa_setscene        after 
do_package                   after do_install
do_package_setscene          after 
do_packagedata               after do_package
do_packagedata_setscene      after 
do_package_write_rpm_setscene after 
do_package_write_rpm         after do_packagedata do_package do_deploy_source_date_epoch
do_devshell                  after do_patch do_prepare_recipe_sysroot
do_pydevshell                after do_patch
do_populate_lic              after do_patch
do_populate_lic_setscene     after 
do_create_recipe_spdx        after 
do_create_recipe_spdx_setscene after 
do_create_spdx               after do_unpack do_patch do_create_recipe_spdx do_deploy_source_date_epoch do_populate_sysroot do_package do_packagedata
do_create_spdx_setscene      after 
do_create_package_spdx       after do_create_spdx
do_create_package_spdx_setscene after 
do_create_recipe_sbom        after do_create_recipe_spdx
do_create_recipe_sbom_setscene after 
```

`bitbake k4-imu-reader`는 사실 `bitbake k4-imu-reader -c build`이고, `do_build`의 선행 태스크를 거꾸로 따라가며 필요한 것을 모두 실행한다. 단계별 의미는 이렇다.

| 태스크 | 하는 일 | 결과가 쌓이는 곳 |
|---|---|---|
| do_fetch | SRC_URI를 받는다 (유일하게 네트워크 허용) | `DL_DIR` |
| do_unpack | 풀어서 작업 디렉터리로 | `UNPACKDIR` (= 보통 `S`의 부모) |
| do_patch | `.patch` 적용 (quilt) | `S` |
| do_prepare_recipe_sysroot | DEPENDS 레시피들의 sysroot 산출물을 모은다 | `recipe-sysroot`, `recipe-sysroot-native` |
| do_configure / do_compile | 빌드 | `B` |
| do_install | 가짜 루트에 설치 | `D` (= `image/`) |
| do_populate_sysroot | 다른 레시피가 링크할 헤더·라이브러리를 내보냄 | `sysroot-destdir` → sstate |
| do_package | `D`를 FILES 규칙으로 패키지별로 나누고 디버그 정보 분리, 공유 라이브러리 의존성 계산 | `package/`, `packages-split/` |
| do_packagedata | 패키지 메타데이터를 전역에 공개 | `pkgdata/` → `tmp/pkgdata/<MACHINE>` |
| do_package_write_rpm | `.rpm` 파일 생성 (PACKAGE_CLASSES에 따라 ipk/deb) | `tmp/deploy/rpm/` |
| do_package_qa | QA 검사 (빌드 경로 누출, 잘못된 아키텍처, 누락된 RDEPENDS …) | 경고·에러 |
| do_populate_lic | 라이선스 텍스트 수집 | `tmp/deploy/licenses/` |
| do_create_spdx 등 | SBOM(SPDX) 생성 (K7) | `tmp/deploy/spdx/` |

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="8" y="40" width="104" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="60.0" y="60.0" font-size="12" text-anchor="middle">do_fetch</text><rect x="120" y="40" width="104" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="172.0" y="60.0" font-size="12" text-anchor="middle">do_unpack</text><line x1="112" y1="57" x2="119" y2="57" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="232" y="40" width="104" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="284.0" y="60.0" font-size="12" text-anchor="middle">do_patch</text><line x1="224" y1="57" x2="231" y2="57" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="344" y="40" width="104" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="396.0" y="60.0" font-size="12" text-anchor="middle">do_configure</text><line x1="336" y1="57" x2="343" y2="57" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="456" y="40" width="104" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="508.0" y="60.0" font-size="12" text-anchor="middle">do_compile</text><line x1="448" y1="57" x2="455" y2="57" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="568" y="40" width="104" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="620.0" y="60.0" font-size="12" text-anchor="middle">do_install</text><line x1="560" y1="57" x2="567" y2="57" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><text x="8.0" y="26.0" font-size="12" text-anchor="start" font-weight="bold">소스 준비 (SRC_URI → S)</text><text x="344.0" y="26.0" font-size="12" text-anchor="start" font-weight="bold">빌드 (S · B → D)</text><rect x="8" y="96" width="210" height="30" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="113.0" y="114.0" font-size="12" text-anchor="middle">do_prepare_recipe_sysroot</text><text x="230.0" y="116.0" font-size="12" text-anchor="start">DEPENDS의 sysroot 모으기</text><path d="M 113 96 L 113 86 L 380 86 L 380 76" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><path d="M 600 74 L 600 148" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><path d="M 580 74 L 580 112 L 398 112 L 398 148" fill="none" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="318" y="150" width="160" height="30" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="398.0" y="168.0" font-size="12" text-anchor="middle">do_populate_sysroot</text><rect x="500" y="150" width="172" height="30" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="586.0" y="168.0" font-size="12" text-anchor="middle">do_package</text><line x1="586" y1="180" x2="586" y2="210" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="500" y="212" width="172" height="30" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="586.0" y="230.0" font-size="12" text-anchor="middle">do_packagedata</text><line x1="500" y1="227" x2="472" y2="227" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="290" y="212" width="180" height="30" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="380.0" y="230.0" font-size="12" text-anchor="middle">do_package_write_rpm</text><line x1="290" y1="227" x2="242" y2="227" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="8" y="212" width="232" height="30" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="124.0" y="230.0" font-size="12" text-anchor="middle">이미지 레시피의 do_rootfs</text><line x1="586" y1="242" x2="586" y2="262" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah4)"/><rect x="500" y="264" width="172" height="30" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="586.0" y="282.0" font-size="12" text-anchor="middle">do_package_qa</text><text x="312.0" y="170.0" font-size="12" text-anchor="end">다른 레시피의 DEPENDS가 가져감 →</text><text x="8.0" y="316.0" font-size="13" text-anchor="start" font-weight="bold">sstate (setscene) 지름길</text><rect x="8" y="324" width="664" height="46" rx="6" fill="#888" fill-opacity="0.1" stroke="#888" stroke-width="1.3"/><text x="340.0" y="342.0" font-size="12" text-anchor="middle">task hash가 sstate-cache와 같으면 *_setscene 태스크가 결과만 풀고 끝</text><text x="340.0" y="358.0" font-size="12" text-anchor="middle">populate_sysroot · packagedata · package_write_rpm · package_qa → fetch~install 생략</text><text x="340.0" y="392.0" font-size="12" text-anchor="middle">초록 = 네트워크 허용(do_fetch[network]="1"), 나머지 태스크는 네트워크 없는 namespace에서 실행</text></svg>
```
그림 5 — 태스크 파이프라인과 sstate 지름길. 앱 레시피의 패키지 결과가 이미지 레시피의 do_rootfs로 들어간다.

### 5.2 stamp, task hash, sstate — 왜 두 번째 빌드는 빠른가

Make는 파일 시간(mtime)으로 "다시 빌드할까"를 정한다. BitBake는 **task hash**를 쓴다. 태스크 하나의 해시는 (그 태스크 함수의 본문 + 함수가 참조하는 변수 값들 + 선행 태스크들의 해시)로 계산된다. 그래서 `CFLAGS` 한 글자만 바꿔도 do_compile부터 아래가 전부 다시 돈다 — Make보다 정확하다.

- **stamp**: 태스크가 끝나면 `STAMP` 경로에 `<PV>.do_compile.<hash>` 같은 빈 파일을 만든다. 우리 레시피의 STAMP는 아래와 같다. 다음 빌드에서 같은 해시의 stamp가 있으면 그 태스크를 건너뛴다.
- **sstate(shared state) cache**: 일부 태스크(그림 5의 회색 상자 목록)의 **결과물**을 해시 이름의 tarball로 `SSTATE_DIR`에 저장한다. 빌드 디렉터리를 지워도, 다른 사람의 머신이어도, 해시가 같으면 `_setscene` 태스크가 결과만 풀고 fetch~install을 통째로 건너뛴다.

```sh
$P $R STAMP SSTATE_DIR SSTATE_PKGARCH 2>&1 | grep -v ^NOTE | sed "s|/var/tmp/K4/k4_build|\${TOPDIR}|g"
```

```text
STAMP                  = '${TOPDIR}/tmp/stamps/cortexa57-poky-linux/k4-imu-reader/1.0'
SSTATE_DIR             = '${TOPDIR}/sstate-cache'
SSTATE_PKGARCH         = 'cortexa57'
```

출력에서 볼 것: `SSTATE_PKGARCH`가 `cortexa57`이다. 같은 cortex-a57 tune을 쓰는 다른 MACHINE은 이 레시피의 sstate를 그대로 재사용할 수 있다. 반대로 `PACKAGE_ARCH = "${MACHINE_ARCH}"`인 레시피(packagegroup, 이미지, 보드 설정)는 MACHINE마다 따로 빌드된다. sstate를 팀·CI와 공유하는 방법, hash equivalence 서버, 해시가 왜 바뀌었는지 `bitbake-diffsigs`로 추적하는 법은 K5에서 다룬다.

### 5.3 DEPENDS vs RDEPENDS, PROVIDES와 virtual/

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="14.0" y="22.0" font-size="13" text-anchor="start" font-weight="bold">DEPENDS — 빌드할 때 (레시피 → 레시피)</text><rect x="8" y="34" width="216" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="116.0" y="51.0" font-size="12" text-anchor="middle">k4-imu-reader</text><text x="116.0" y="67.0" font-size="12" text-anchor="middle">DEPENDS: virtual/libc …</text><rect x="250" y="34" width="214" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="357.0" y="51.0" font-size="12" text-anchor="middle">glibc: do_populate_sysroot</text><text x="357.0" y="67.0" font-size="12" text-anchor="middle">헤더 · libc.so · crt</text><rect x="490" y="34" width="182" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="581.0" y="51.0" font-size="12" text-anchor="middle">recipe-sysroot/</text><text x="581.0" y="67.0" font-size="12" text-anchor="middle">--sysroot= 로 컴파일</text><line x1="226" y1="56" x2="248" y2="56" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/><line x1="466" y1="56" x2="488" y2="56" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/><text x="14.0" y="102.0" font-size="12" text-anchor="start">→ 태스크 의존: A:do_prepare_recipe_sysroot 가 B:do_populate_sysroot 를 기다림</text><line x1="14" y1="120" x2="666" y2="120" stroke="currentColor" stroke-width="1.3" stroke-dasharray="4 3"/><text x="14.0" y="146.0" font-size="13" text-anchor="start" font-weight="bold">RDEPENDS — 기기에서 돌 때 (패키지 → 패키지)</text><rect x="8" y="158" width="216" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="116.0" y="175.0" font-size="12" text-anchor="middle">패키지 k4-imu-reader</text><text x="116.0" y="191.0" font-size="12" text-anchor="middle">RDEPENDS: k4-imu-reader-conf</text><rect x="250" y="158" width="214" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="357.0" y="175.0" font-size="12" text-anchor="middle">패키지 k4-imu-reader-conf</text><text x="357.0" y="191.0" font-size="12" text-anchor="middle">/etc/k4-imu.conf</text><rect x="490" y="158" width="182" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="581.0" y="175.0" font-size="12" text-anchor="middle">이미지 rootfs</text><text x="581.0" y="191.0" font-size="12" text-anchor="middle">둘 다 설치됨</text><line x1="226" y1="180" x2="248" y2="180" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/><line x1="466" y1="180" x2="488" y2="180" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah8)"/><text x="14.0" y="226.0" font-size="12" text-anchor="start">→ 패키지 관리자 의존(rpm Requires) + 이미지 do_rootfs가 자동으로 끌어옴</text><text x="14.0" y="250.0" font-size="12" text-anchor="start">libc.so.6 같은 공유 라이브러리 의존은 do_package가 ELF를 읽어 RDEPENDS에 자동 추가</text><text x="14.0" y="274.0" font-size="12" text-anchor="start">이름 공간이 다르다: DEPENDS는 레시피 이름(PN · PROVIDES), RDEPENDS는 패키지 이름</text></svg>
```
그림 6 — DEPENDS는 빌드할 때 레시피 사이의 의존(sysroot를 채운다), RDEPENDS는 기기에서 돌 때 패키지 사이의 의존(rootfs에 같이 설치된다).

우리 레시피를 파싱해 실제 값을 보자.

```sh
$P $R DEPENDS RDEPENDS:k4-imu-reader @classes 2>&1 | grep -v ^NOTE
```

```text
DEPENDS                = 'gcc-cross-aarch64 virtual/compilerlibs virtual/libc systemd-systemctl-native'
RDEPENDS:k4-imu-reader = 'k4-imu-reader-conf'
base.bbclass patch.bbclass terminal.bbclass staging.bbclass mirrors.bbclass utils.bbclass utility-tasks.bbclass logging.bbclass metadata_scm.bbclass poky-sanity.bbclass package_rpm.bbclass package.bbclass packagedata.bbclass chrpath.bbclass package_pkgdata.bbclass insane.bbclass buildstats.bbclass debian.bbclass devshell.bbclass sstate.bbclass license.bbclass remove-libtool.bbclass create-spdx.bbclass create-spdx-3.0.bbclass spdx-common.bbclass uninative.bbclass sanity.bbclass systemd.bbclass gcc-native.bbclass gcc.bbclass
```

출력에서 볼 것:

- 레시피에 DEPENDS를 한 줄도 안 썼는데 4개가 들어 있다. 크로스 gcc, 컴파일러 런타임, libc는 툴체인 클래스(`gcc.bbclass`)가, `systemd-systemctl-native`(빌드 중에 서비스를 enable할 호스트 도구)는 `systemd.bbclass`가 넣었다. 레시피 하나가 실제로는 30개 클래스를 상속하고 있다.
- `virtual/libc`, `virtual/compilerlibs`는 **가상 이름**이다. "C 라이브러리를 제공하는 무언가"라는 인터페이스이고, 실제 레시피는 `PREFERRED_PROVIDER_virtual/libc`가 고른다.

가상 이름이 실제 레시피로 어떻게 연결되나.

```sh
for v in PREFERRED_PROVIDER_virtual/libc PREFERRED_PROVIDER_virtual/kernel PREFERRED_VERSION_linux-yocto \
         VIRTUAL-RUNTIME_init_manager VIRTUAL-RUNTIME_base-utils VIRTUAL-RUNTIME_dev_manager TCLIBC; do
  printf "%-36s = \"%s\"\n" $v "$(bitbake-getvar --value -q $v 2>/dev/null)"
done
$P $OE/meta/recipes-core/glibc/glibc_2.43.bb PN PROVIDES 2>&1 | grep -v ^NOTE
$P $OE/meta/recipes-kernel/linux/linux-yocto_6.18.bb PN PV PROVIDES 2>&1 | grep -v ^NOTE
```

```text
PREFERRED_PROVIDER_virtual/libc      = "glibc"
PREFERRED_PROVIDER_virtual/kernel    = "linux-yocto"
PREFERRED_VERSION_linux-yocto        = "6.18%"
VIRTUAL-RUNTIME_init_manager         = "systemd"
VIRTUAL-RUNTIME_base-utils           = "busybox"
VIRTUAL-RUNTIME_dev_manager          = "systemd"
TCLIBC                               = "glibc"
PN                     = 'glibc'
PROVIDES               = 'glibc virtual/libc virtual/libintl virtual/libiconv'
PN                     = 'linux-yocto'
PV                     = '6.18.52+git'
PROVIDES               = 'linux-yocto  virtual/kernel'
```

출력에서 볼 것:

- **빌드 시점 가상 이름**(`virtual/…`): 레시피가 `PROVIDES`로 "나는 virtual/kernel을 제공한다"고 선언하고, 설정이 `PREFERRED_PROVIDER_virtual/kernel`로 하나를 고른다. 벤더 BSP layer는 이걸 `linux-imx`, `linux-ti-staging`, `linux-tegra` 같은 자기 커널 레시피로 바꾼다. 이미지 쪽에서는 "커널"이라고만 말하면 되니 MACHINE만 바꿔도 다른 커널이 들어간다.
- **실행 시점 가상 이름**(`VIRTUAL-RUNTIME_…`): 같은 아이디어의 RDEPENDS 버전이다. packagegroup-core-boot는 `${VIRTUAL-RUNTIME_init_manager}`를 RDEPENDS로 갖고, DISTRO가 그걸 systemd 또는 sysvinit으로 정한다.
- 커널 버전 `6.18.52+git`은 Don의 QEMU 랩 커널(6.18.55 LTS)과 같은 6.18 계열이다. `PREFERRED_VERSION_linux-yocto = "6.18%"`의 `%`는 와일드카드다.

DEPENDS와 RDEPENDS를 비교표로 정리한다.

| | DEPENDS | RDEPENDS |
|---|---|---|
| 언제 | 빌드할 때 | 기기에서 실행할 때 |
| 무엇과 무엇 사이 | 레시피 ↔ 레시피 (PN 또는 PROVIDES 이름) | 패키지 ↔ 패키지 (`RDEPENDS:${PN}` 처럼 패키지마다) |
| 효과 | B의 do_populate_sysroot가 A의 do_prepare_recipe_sysroot보다 먼저 돌고, B의 헤더·라이브러리가 A의 recipe-sysroot에 들어간다 | 패키지 메타데이터(rpm Requires)에 기록되고, 이미지 do_rootfs가 B도 설치한다 |
| 빠뜨리면 | 헤더를 못 찾아 컴파일 에러 (sysroot 격리 덕분에 바로 드러난다) | 빌드는 되지만 기기에서 실행 시 "command not found", 스크립트 인터프리터 없음 |
| 자동으로 채워지는 경우 | 상속한 클래스(gcc, cmake, systemd …)가 추가 | 공유 라이브러리(`.so`)는 do_package가 ELF를 읽어 자동 추가. 셸/Python 스크립트가 부르는 명령은 자동이 아니다 |
| 비슷한 것 | 호스트 도구는 `-native` 레시피를 DEPENDS에 (`cmake-native`) | `RRECOMMENDS`(약한 의존, 없어도 설치 진행), `RPROVIDES`, `RCONFLICTS` |

### 5.4 작업 디렉터리 지도

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="14" y="12" width="652" height="30" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-width="1.3"/><text x="24.0" y="30.0" font-size="12" text-anchor="start">${TOPDIR}/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/   = WORKDIR</text><line x1="30" y1="44" x2="30" y2="65" stroke="currentColor" stroke-width="1.3"/><line x1="30" y1="65" x2="46" y2="65" stroke="currentColor" stroke-width="1.3"/><rect x="48" y="52" width="214" height="26" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="58.0" y="68.0" font-size="12" text-anchor="start">sources/</text><text x="274.0" y="70.0" font-size="12" text-anchor="start">S = B = UNPACKDIR — SRC_URI가 풀린 곳</text><line x1="30" y1="76" x2="30" y2="97" stroke="currentColor" stroke-width="1.3"/><line x1="30" y1="97" x2="46" y2="97" stroke="currentColor" stroke-width="1.3"/><rect x="48" y="84" width="214" height="26" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="58.0" y="100.0" font-size="12" text-anchor="start">recipe-sysroot/</text><text x="274.0" y="102.0" font-size="12" text-anchor="start">DEPENDS가 모인 타깃 sysroot (--sysroot)</text><line x1="30" y1="108" x2="30" y2="129" stroke="currentColor" stroke-width="1.3"/><line x1="30" y1="129" x2="46" y2="129" stroke="currentColor" stroke-width="1.3"/><rect x="48" y="116" width="214" height="26" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="58.0" y="132.0" font-size="12" text-anchor="start">recipe-sysroot-native/</text><text x="274.0" y="134.0" font-size="12" text-anchor="start">-native 도구 (호스트에서 도는 것)</text><line x1="30" y1="140" x2="30" y2="161" stroke="currentColor" stroke-width="1.3"/><line x1="30" y1="161" x2="46" y2="161" stroke="currentColor" stroke-width="1.3"/><rect x="48" y="148" width="214" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="58.0" y="164.0" font-size="12" text-anchor="start">temp/</text><text x="274.0" y="166.0" font-size="12" text-anchor="start">log.do_&lt;task&gt; · run.do_&lt;task&gt; (실제 실행된 셸)</text><line x1="30" y1="172" x2="30" y2="193" stroke="currentColor" stroke-width="1.3"/><line x1="30" y1="193" x2="46" y2="193" stroke="currentColor" stroke-width="1.3"/><rect x="48" y="180" width="214" height="26" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="58.0" y="196.0" font-size="12" text-anchor="start">image/</text><text x="274.0" y="198.0" font-size="12" text-anchor="start">D — do_install이 make install 하는 가짜 루트</text><line x1="30" y1="204" x2="30" y2="225" stroke="currentColor" stroke-width="1.3"/><line x1="30" y1="225" x2="46" y2="225" stroke="currentColor" stroke-width="1.3"/><rect x="48" y="212" width="214" height="26" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="58.0" y="228.0" font-size="12" text-anchor="start">package/</text><text x="274.0" y="230.0" font-size="12" text-anchor="start">PKGD — 패키징용 복사본 + 디버그 정보 분리</text><line x1="30" y1="236" x2="30" y2="257" stroke="currentColor" stroke-width="1.3"/><line x1="30" y1="257" x2="46" y2="257" stroke="currentColor" stroke-width="1.3"/><rect x="48" y="244" width="214" height="26" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="58.0" y="260.0" font-size="12" text-anchor="start">packages-split/&lt;pkg&gt;/</text><text x="274.0" y="262.0" font-size="12" text-anchor="start">PKGDEST — FILES 규칙으로 쪼갠 결과</text><line x1="30" y1="268" x2="30" y2="289" stroke="currentColor" stroke-width="1.3"/><line x1="30" y1="289" x2="46" y2="289" stroke="currentColor" stroke-width="1.3"/><rect x="48" y="276" width="214" height="26" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="58.0" y="292.0" font-size="12" text-anchor="start">pkgdata/ · sysroot-destdir/</text><text x="274.0" y="294.0" font-size="12" text-anchor="start">다른 레시피·이미지가 볼 메타데이터 · sysroot</text><line x1="30" y1="300" x2="30" y2="321" stroke="currentColor" stroke-width="1.3"/><line x1="30" y1="321" x2="46" y2="321" stroke="currentColor" stroke-width="1.3"/><rect x="48" y="308" width="214" height="26" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="58.0" y="324.0" font-size="12" text-anchor="start">deploy-rpms/</text><text x="274.0" y="326.0" font-size="12" text-anchor="start">do_package_write_rpm 결과 → tmp/deploy/rpm</text><text x="14.0" y="350.0" font-size="13" text-anchor="start" font-weight="bold">빌드 디렉터리의 다른 큰 덩어리</text><rect x="14" y="360" width="248" height="26" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="24.0" y="376.0" font-size="12" text-anchor="start">downloads/  (DL_DIR)</text><text x="274.0" y="378.0" font-size="12" text-anchor="start">소스 tarball · git 미러 — 빌드 간 공유</text><rect x="14" y="392" width="248" height="26" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="24.0" y="408.0" font-size="12" text-anchor="start">sstate-cache/  (SSTATE_DIR)</text><text x="274.0" y="410.0" font-size="12" text-anchor="start">태스크 결과 캐시 — 팀·CI와 공유</text><rect x="14" y="424" width="248" height="26" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="24.0" y="440.0" font-size="12" text-anchor="start">tmp/deploy/images/&lt;MACHINE&gt;/</text><text x="274.0" y="442.0" font-size="12" text-anchor="start">커널 Image · rootfs · .qemuboot.conf</text></svg>
```
그림 7 — 레시피 작업 디렉터리. 경로는 아래 tinfoil 출력에서 그대로 가져왔다(빌드를 안 했으므로 디렉터리가 실제로 생긴 것은 아니다).

```sh
$P $R UNPACKDIR PKGD PKGDEST PKGDESTWORK SYSROOT_DESTDIR PKGWRITEDIRRPM DEPLOY_DIR_RPM 2>&1 \
  | grep -v ^NOTE | sed "s|/var/tmp/K4/k4_build|\${TOPDIR}|g"
grep -n "BB_RUNFMT\|BB_LOGFMT\|loglink = " /var/tmp/K4/k4_bitbake/lib/bb/build.py
```

```text
UNPACKDIR              = '${TOPDIR}/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/sources'
PKGD                   = '${TOPDIR}/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/package'
PKGDEST                = '${TOPDIR}/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/packages-split'
PKGDESTWORK            = '${TOPDIR}/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/pkgdata'
SYSROOT_DESTDIR        = '${TOPDIR}/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/sysroot-destdir'
PKGWRITEDIRRPM         = '${TOPDIR}/tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/deploy-rpms'
DEPLOY_DIR_RPM         = '${TOPDIR}/tmp/deploy/rpm'
239:    runfmt = d.getVar('BB_RUNFMT') or "run.{func}.{pid}"
635:    logfmt = localdata.getVar('BB_LOGFMT') or 'log.{task}.{pid}'
649:    loglink = os.path.join(tempdir, 'log.{0}'.format(task))
```

출력에서 볼 것: `temp/` 아래에는 태스크마다 `log.do_compile.<pid>`(출력 로그)와 `run.do_compile.<pid>`(**실제로 실행된 셸 스크립트 전체** — 모든 변수가 export된 상태)가 생기고, 최신 것을 가리키는 `log.do_compile` 심볼릭 링크가 걸린다. 빌드가 실패하면 순서는 늘 같다: ① `log.do_<task>`의 마지막 에러, ② `run.do_<task>`로 실제 명령 확인, ③ `bitbake -c devshell <레시피>`로 같은 환경의 셸에 들어가 손으로 재현. Don이 SSD 펌웨어 빌드에서 "make V=1로 실제 명령 보기"를 하던 것과 같다.

빌드 후 디렉터리 모양은 다음과 같을 것이다. **(실행하지 않음 — 가이드: 위 변수 값과 build.py 코드에서 유도한 구조이고, 실제 `ls` 출력이 아니다)**

```
tmp/work/cortexa57-poky-linux/k4-imu-reader/1.0/
  sources/            k4_imu_reader.c  k4-imu.conf  k4-imu-reader.service  k4-imu-reader (built)
  recipe-sysroot/     usr/include  usr/lib  (glibc, gcc runtime)
  recipe-sysroot-native/
  temp/               log.do_compile -> log.do_compile.<pid>   run.do_compile.<pid>  ...
  image/              usr/bin/k4-imu-reader  etc/k4-imu.conf  usr/lib/systemd/system/...
  package/            (copy of image/ + .debug split)
  packages-split/     k4-imu-reader/  k4-imu-reader-conf/  k4-imu-reader-dbg/  ...
  pkgdata/  sysroot-destdir/  deploy-rpms/cortexa57/k4-imu-reader-1.0-r0.cortexa57.rpm
```

---

## 6. 패키지 — 레시피 하나가 여러 패키지가 된다

### 6.1 PACKAGES와 FILES

Yocto는 레시피를 빌드한 뒤 `D`(image/)를 통째로 rootfs에 넣지 않는다. **패키지로 쪼갠 다음, 이미지가 필요한 패키지만 설치한다.** 데비안에서 `libfoo`, `libfoo-dev`, `libfoo-dbg`가 따로 있는 것과 같다. 쪼개는 규칙이 `PACKAGES`(패키지 이름 목록)와 `FILES:<패키지>`(그 패키지가 가져갈 파일 glob)다.

```sh
$P $R PACKAGES FILES:k4-imu-reader-conf CONFFILES:k4-imu-reader-conf PACKAGE_ARCH 2>&1 | grep -v ^NOTE
grep -n "^PACKAGES = " $OE/meta/conf/bitbake.conf
```

```text
PACKAGES               = 'k4-imu-reader-conf k4-imu-reader-src k4-imu-reader-dbg k4-imu-reader-staticdev k4-imu-reader-dev k4-imu-reader-doc k4-imu-reader-locale  k4-imu-reader'
FILES:k4-imu-reader-conf = '/etc/k4-imu.conf'
CONFFILES:k4-imu-reader-conf = '/etc/k4-imu.conf'
PACKAGE_ARCH           = 'cortexa57'
334:PACKAGES = "${PN}-src ${PN}-dbg ${PN}-staticdev ${PN}-dev ${PN}-doc ${PN}-locale ${PACKAGE_BEFORE_PN} ${PN}"
```

출력에서 볼 것:

- 기본 PACKAGES는 bitbake.conf에서 온다: 소스(`-src`), 디버그 심볼(`-dbg`), 정적 라이브러리(`-staticdev`), 헤더·`.so` 링크(`-dev`), 문서, 로케일, 그리고 본 패키지(`${PN}`).
- 우리 `-conf` 패키지는 `=+`로 **맨 앞**에 넣었다. **순서가 중요하다.** do_package는 PACKAGES를 왼쪽부터 돌면서 각 패키지의 FILES에 맞는 파일을 가져가고, 한 번 가져간 파일은 다음 패키지가 못 가져간다. 기본 `FILES:${PN}`에는 `${sysconfdir}`가 들어 있어서, `-conf`를 뒤에 두면 `/etc/k4-imu.conf`를 본 패키지가 먼저 가져가 버린다.
- `CONFFILES`는 "사용자가 고쳤을 수 있는 설정 파일"이라는 표시다. 패키지 업그레이드 때 덮어쓰지 않는다(rpm은 `.rpmnew`로 따로 둔다).
- 어떤 패키지도 가져가지 않은 파일이 `D`에 남으면 do_package가 **"installed-vs-shipped"** QA 에러로 멈춘다. "설치해 놓고 출하하지 않은 파일"은 실수일 가능성이 높기 때문이다.

### 6.2 패키지 형식 — rpm, ipk, deb

`PACKAGE_CLASSES`가 패키지 파일 형식을 정한다.

```sh
grep -n "PACKAGE_CLASSES ?=" $OE/meta/conf/distro/defaultsetup.conf /var/tmp/K4/k4_meta-yocto/meta-poky/conf/distro/poky.conf
```

```text
/var/tmp/K4/k4_oe-core/meta/conf/distro/defaultsetup.conf:16:PACKAGE_CLASSES ?= "package_ipk"
/var/tmp/K4/k4_meta-yocto/meta-poky/conf/distro/poky.conf:31:PACKAGE_CLASSES ?= "package_rpm"
```

| 형식 | 클래스 | 기기의 패키지 관리자 | 특징 |
|---|---|---|---|
| ipk | `package_ipk` | opkg | 가장 가볍다. OE 기본값. 작은 장비 |
| rpm | `package_rpm` | dnf | Poky 기본값. 의존성 해석이 엄격, 메타데이터가 크다 |
| deb | `package_deb` | apt/dpkg | 데비안 도구에 익숙한 팀 |

여러 개를 적으면(`"package_rpm package_ipk"`) 모두 만들고, **첫 번째 형식으로 rootfs를 조립한다.** 기기에 패키지 관리자를 넣을지는 별개다 — `IMAGE_FEATURES += "package-management"`가 없으면 rootfs만 조립하고 dnf/opkg는 들어가지 않는다. 대부분의 제품은 넣지 않고, 업데이트는 이미지 단위(A/B, L 모듈의 OTA)로 한다.

---

## 7. MACHINE · DISTRO · IMAGE — 세 축을 분리한다

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="14" y="12" width="208" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="118.0" y="30.0" font-size="13" text-anchor="middle" font-weight="bold">MACHINE</text><text x="118.0" y="47.0" font-size="13" text-anchor="middle">무슨 하드웨어인가</text><rect x="14" y="70" width="208" height="28" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-width="1.3"/><text x="118.0" y="87.0" font-size="12" text-anchor="middle">conf/machine/&lt;이름&gt;.conf</text><rect x="14" y="104" width="208" height="28" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-width="1.3"/><text x="118.0" y="121.0" font-size="12" text-anchor="middle">CPU tune (cortexa57)</text><rect x="14" y="138" width="208" height="28" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-width="1.3"/><text x="118.0" y="155.0" font-size="12" text-anchor="middle">커널·부트로더 provider</text><rect x="14" y="172" width="208" height="28" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-width="1.3"/><text x="118.0" y="189.0" font-size="12" text-anchor="middle">콘솔 · DTB · 이미지 형식</text><rect x="14" y="206" width="208" height="28" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-width="1.3"/><text x="118.0" y="223.0" font-size="12" text-anchor="middle">MACHINE_FEATURES</text><rect x="236" y="12" width="208" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="340.0" y="30.0" font-size="13" text-anchor="middle" font-weight="bold">DISTRO</text><text x="340.0" y="47.0" font-size="13" text-anchor="middle">어떤 정책의 OS인가</text><rect x="236" y="70" width="208" height="28" rx="6" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.3"/><text x="340.0" y="87.0" font-size="12" text-anchor="middle">conf/distro/&lt;이름&gt;.conf</text><rect x="236" y="104" width="208" height="28" rx="6" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.3"/><text x="340.0" y="121.0" font-size="12" text-anchor="middle">libc (glibc/musl)</text><rect x="236" y="138" width="208" height="28" rx="6" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.3"/><text x="340.0" y="155.0" font-size="12" text-anchor="middle">init (systemd/sysvinit)</text><rect x="236" y="172" width="208" height="28" rx="6" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.3"/><text x="340.0" y="189.0" font-size="12" text-anchor="middle">패키지 형식 · 보안 플래그</text><rect x="236" y="206" width="208" height="28" rx="6" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.3"/><text x="340.0" y="223.0" font-size="12" text-anchor="middle">DISTRO_FEATURES</text><rect x="458" y="12" width="208" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="562.0" y="30.0" font-size="13" text-anchor="middle" font-weight="bold">IMAGE</text><text x="562.0" y="47.0" font-size="13" text-anchor="middle">무엇을 설치하나</text><rect x="458" y="70" width="208" height="28" rx="6" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-width="1.3"/><text x="562.0" y="87.0" font-size="12" text-anchor="middle">recipes-*/images/*.bb</text><rect x="458" y="104" width="208" height="28" rx="6" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-width="1.3"/><text x="562.0" y="121.0" font-size="12" text-anchor="middle">inherit core-image</text><rect x="458" y="138" width="208" height="28" rx="6" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-width="1.3"/><text x="562.0" y="155.0" font-size="12" text-anchor="middle">IMAGE_INSTALL</text><rect x="458" y="172" width="208" height="28" rx="6" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-width="1.3"/><text x="562.0" y="189.0" font-size="12" text-anchor="middle">IMAGE_FEATURES</text><rect x="458" y="206" width="208" height="28" rx="6" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-width="1.3"/><text x="562.0" y="223.0" font-size="12" text-anchor="middle">IMAGE_FSTYPES</text><text x="340.0" y="258.0" font-size="12" text-anchor="middle">같은 이미지 + 다른 MACHINE = 다른 보드용 빌드 · 같은 MACHINE + 다른 DISTRO = 다른 정책</text><text x="340.0" y="282.0" font-size="12" text-anchor="middle">설정 위치: MACHINE · DISTRO는 local.conf / toolcfg.conf, IMAGE는 bitbake 명령의 타깃</text></svg>
```
그림 8 — Yocto 설정의 세 축. "무슨 보드인가"(MACHINE), "어떤 정책의 OS인가"(DISTRO), "무엇을 설치하나"(IMAGE)를 섞지 않는 것이 layer 설계의 기본이다.

이 분리가 왜 중요한가. 센서 키트 회사를 생각해 보자. 보드는 개발용 EVK, 양산 보드 rev A, rev B가 있고(MACHINE 3개), 정책은 개발용(디버그 도구, root 로그인)과 양산용(서명, 읽기 전용 rootfs)이 있고(DISTRO 2개 또는 DISTRO_FEATURES 차이), 이미지는 공장 테스트용과 출하용이 있다(IMAGE 2개). 이걸 한 파일에 섞으면 3×2×2=12개의 조합을 손으로 관리해야 하지만, 축을 나누면 각자 독립적으로 고칠 수 있다.

- MACHINE conf에 패키지 목록(IMAGE_INSTALL)을 넣지 않는다.
- 이미지 레시피에 보드 특정 설정(커널 provider, 시리얼 콘솔)을 넣지 않는다.
- DISTRO conf에 보드 이름을 쓰지 않는다.

### 7.1 MACHINE — 보드 정의

실제 qemuarm64.conf의 주석을 뺀 앞부분이다.

```sh
grep -v "^#" $OE/meta/conf/machine/qemuarm64.conf | grep -v "^$" | head -12
```

```text
require conf/machine/include/arm/armv8a/tune-cortexa57.inc
require conf/machine/include/qemu.inc
KERNEL_IMAGETYPE = "Image"
PREFERRED_PROVIDER_virtual/bootloader ?= "u-boot"
UBOOT_MACHINE ?= "qemu_arm64_defconfig"
SERIAL_CONSOLES ?= "115200;ttyAMA0 115200;hvc0"
QB_SYSTEM_NAME = "qemu-system-aarch64"
QB_MACHINE = "-machine virt"
QB_CPU = "-cpu cortex-a57"
QB_SMP ?= "-smp 4"
QB_CPU_KVM = "-cpu host -machine gic-version=3"
QB_GRAPHICS = "-device virtio-gpu-pci"
```

출력에서 볼 것: MACHINE conf가 정하는 것은 CPU tune(`tune-cortexa57.inc`), 커널 이미지 형식, 부트로더 provider와 defconfig, 시리얼 콘솔(K1의 getty가 여기서 정해진다), 그리고 `QB_`로 시작하는 runqemu 설정이다. `-machine virt -cpu cortex-a57`은 Don의 QEMU 부팅 랩(C 모듈)과 똑같은 가상 보드다. 실제 보드라면 여기에 `KERNEL_DEVICETREE`(D 모듈의 dtb 목록), `IMAGE_FSTYPES`(wic, ubifs …), `MACHINE_FEATURES`(wifi, can …)가 들어간다.

우리 layer의 MACHINE stub이다.

```text
#@TYPE: Machine
#@NAME: K4 sensor kit (QEMU arm64 기반 stub)
#@DESCRIPTION: 실제 보드 대신 qemuarm64 설정을 상속한 연습용 MACHINE

require conf/machine/qemuarm64.conf

# require만으로는 qemuarm64 override가 켜지지 않는다 (MACHINEOVERRIDES ?= "${MACHINE}").
# qemuarm64용으로 쓰인 :qemuarm64 조건들을 이어받으려면 직접 앞에 붙인다.
MACHINEOVERRIDES =. "qemuarm64:"

MACHINE_FEATURES:append = " k4-imu"
SERIAL_CONSOLES = "115200;ttyAMA0"
MACHINE_EXTRA_RRECOMMENDS += "k4-imu-reader"
```

2.4절에서 본 것처럼 처음에는 `MACHINEOVERRIDES =.` 줄 없이 만들었고, `MACHINEOVERRIDES`에 qemuarm64가 없었다. 이게 실제로 어떤 문제를 일으키나? 커널 레시피를 파싱해 보면 바로 드러난다.

```sh
bitbake-getvar MACHINEOVERRIDES 2>&1 | grep -E "qemu.inc|k4-sensorkit.conf|^MACHINEOVERRIDES"
$P $OE/meta/recipes-kernel/linux/linux-yocto_6.18.bb COMPATIBLE_MACHINE 2>&1 | grep -v ^NOTE
MC=/var/tmp/K4/k4_layers/meta-k4-sensorkit/conf/machine/k4-sensorkit.conf
sed -i 's/^MACHINEOVERRIDES =\./#MACHINEOVERRIDES =./' $MC
echo "[주석 처리 후]"; $P $OE/meta/recipes-kernel/linux/linux-yocto_6.18.bb __SKIPPED 2>&1 | grep -v ^NOTE
sed -i 's/^#MACHINEOVERRIDES =\./MACHINEOVERRIDES =./' $MC
echo "[복구 후]"; $P $OE/meta/recipes-kernel/linux/linux-yocto_6.18.bb __SKIPPED 2>&1 | grep -v ^NOTE
```

```text
#   predot /var/tmp/K4/k4_oe-core/meta/conf/machine/include/qemu.inc:9
#   predot /var/tmp/K4/k4_layers/meta-k4-sensorkit/conf/machine/k4-sensorkit.conf:9
MACHINEOVERRIDES="qemuarm64:qemuall:aarch64:k4-sensorkit"
COMPATIBLE_MACHINE     = '^(qemuarm|qemuarmv5|qemuarm64|qemux86|qemuppc|qemuppc64|qemumips|qemumips64|qemux86-64|qemuriscv64|qemuriscv32|qemuloongarch64)$'
[주석 처리 후]
__SKIPPED              = 'incompatible with machine k4-sensorkit (not in COMPATIBLE_MACHINE)'
[복구 후]
__SKIPPED              = None
```

출력에서 볼 것:

- linux-yocto는 `COMPATIBLE_MACHINE` 정규식으로 "이 커널은 이 보드들에서만 빌드된다"고 선언한다. 정규식은 MACHINE 이름 하나가 아니라 **MACHINEOVERRIDES의 각 항목**과 비교된다.
- `MACHINEOVERRIDES =. "qemuarm64:"`이 없으면 k4-sensorkit은 어느 항목도 맞지 않아 **커널 레시피가 통째로 건너뛰어진다**(`__SKIPPED`). 이미지 빌드는 "virtual/kernel을 제공하는 레시피가 없다"는 에러로 멈춘다.
- 이것이 실무의 표준 패턴이다. 벤더의 레퍼런스 보드 conf를 `require`해서 자기 보드를 만들 때(예: `imx8mp-evk.conf`를 기반으로 `mycompany-sensorkit.conf`), 레퍼런스 보드의 override를 이어받으려면 그 이름을 MACHINEOVERRIDES 앞에 붙인다. NXP·TI BSP의 SoC 계열 이름(`mx8mp`, `k3` 같은 것)도 이 방식으로 MACHINEOVERRIDES에 들어가 있어서, `:mx8mp` 같은 override 하나로 같은 SoC를 쓰는 모든 보드에 적용된다.

### 7.2 DISTRO — OS 정책

poky.conf의 앞부분과 정책 줄 몇 개다.

```sh
sed -n 1,4p /var/tmp/K4/k4_meta-yocto/meta-poky/conf/distro/poky.conf
grep -n "PREFERRED_VERSION_linux-yocto \|^PACKAGE_CLASSES\|^DISTRO_FEATURES" /var/tmp/K4/k4_meta-yocto/meta-poky/conf/distro/poky.conf
```

```text
DISTRO = "poky"
DISTRO_NAME = "Poky (Yocto Project Reference Distro)"
DISTRO_VERSION = "6.0.4"
DISTRO_CODENAME = "wrynose"
20:DISTRO_FEATURES ?= "${POKY_DEFAULT_DISTRO_FEATURES}"
22:PREFERRED_VERSION_linux-yocto ?= "6.18%"
31:PACKAGE_CLASSES ?= "package_rpm"
```

우리 DISTRO는 poky를 상속하고 init 시스템만 systemd로 바꿨다.

```text
# 연습용 DISTRO: poky를 기반으로 이름과 정책만 바꾼다
require conf/distro/poky.conf

DISTRO = "k4-distro"
DISTRO_NAME = "K4 SensorKit Linux"
DISTRO_VERSION = "0.1"
INIT_MANAGER = "systemd"
```

```sh
for v in DISTRO_NAME INIT_MANAGER; do printf "%s=%s\n" $v "$(bitbake-getvar --value -q $v 2>/dev/null)"; done
bitbake-getvar --value -q DISTRO_FEATURES | wc -w
bitbake-getvar --value -q DISTRO_FEATURES | tr -s " " "\n" | sed -n 2,8p | tr "\n" " "; echo
```

```text
DISTRO_NAME=K4 SensorKit Linux
INIT_MANAGER=systemd
27
systemd usrmerge acl alsa bluetooth debuginfod ext2 
```

출력에서 볼 것: `INIT_MANAGER = "systemd"` 한 줄이 `DISTRO_FEATURES`에 `systemd`와 `usrmerge`(`/bin` → `/usr/bin` 통합, K1)를 넣고, 앞의 `VIRTUAL-RUNTIME_init_manager`도 systemd로 바꿨다. `DISTRO_FEATURES`(27개)는 "이 OS가 지원하는 기능" 목록이고, 레시피들이 `bb.utils.contains('DISTRO_FEATURES', 'x11', ...)`로 보고 빌드 옵션을 켜고 끈다. 센서 키트처럼 화면이 없는 장비라면 실제 제품 DISTRO에서 `DISTRO_FEATURES:remove = "x11 wayland opengl vulkan pulseaudio"` 같은 정리를 하는 게 보통이다 — 의존성이 줄어 빌드 시간과 이미지 크기가 크게 준다.

| 정하는 곳 | 예 | 효과 |
|---|---|---|
| `MACHINE_FEATURES` | `wifi bluetooth can` | 하드웨어에 무엇이 있나 |
| `DISTRO_FEATURES` | `systemd ipv6 x11` | OS가 무엇을 지원하나 |
| 둘의 교집합 | `COMBINED_FEATURES` | 예: bluetooth가 둘 다 있어야 bluez가 들어간다 |
| `IMAGE_FEATURES` | `ssh-server-dropbear read-only-rootfs` | 이 이미지에 무엇을 켜나 |

### 7.3 IMAGE — 이미지 레시피와 packagegroup

이미지도 레시피다. 다만 컴파일하지 않고, **패키지를 골라 rootfs를 조립한다.** 가장 작은 공식 이미지를 보자.

```sh
cat $OE/meta/recipes-core/images/core-image-minimal.bb
grep -n "^FEATURE_PACKAGES_ssh\|^FEATURE_PACKAGES_tools-debug\|^IMAGE_INSTALL ?=\|^CORE_IMAGE_BASE_INSTALL\|^inherit" \
  $OE/meta/classes-recipe/core-image.bbclass
```

```text
SUMMARY = "A small image just capable of allowing a device to boot."

IMAGE_INSTALL = "packagegroup-core-boot ${CORE_IMAGE_EXTRA_INSTALL}"

IMAGE_LINGUAS = " "

LICENSE = "MIT"

inherit core-image

IMAGE_ROOTFS_SIZE ?= "8192"
IMAGE_ROOTFS_EXTRA_SPACE:append = "${@bb.utils.contains("DISTRO_FEATURES", "systemd", " + 4096", "", d)}"
60:FEATURE_PACKAGES_ssh-server-dropbear = "packagegroup-core-ssh-dropbear"
61:FEATURE_PACKAGES_ssh-server-openssh = "packagegroup-core-ssh-openssh"
62:FEATURE_PACKAGES_tools-debug = "packagegroup-core-tools-debug"
84:CORE_IMAGE_BASE_INSTALL = '\
93:IMAGE_INSTALL ?= "${CORE_IMAGE_BASE_INSTALL}"
95:inherit image
```

출력에서 볼 것:

- `IMAGE_INSTALL`: 설치할 **패키지** 목록. 그 패키지들의 RDEPENDS는 자동으로 따라 들어온다.
- `IMAGE_FEATURES`: 이름 붙은 기능 스위치. `FEATURE_PACKAGES_<기능>`이 "이 기능을 켜면 이 패키지를 넣어라"의 매핑이다. 그 밖에 `read-only-rootfs`, `allow-root-login`처럼 패키지가 아니라 rootfs 후처리를 바꾸는 기능도 있다.
- `IMAGE_LINGUAS = " "`: 로케일 패키지를 넣지 않는다(공백 하나가 "빈 목록"이라는 뜻).
- `CORE_IMAGE_EXTRA_INSTALL`: local.conf에서 이미지들에 공통으로 끼워 넣으라고 열어 둔 변수.

packagegroup은 "패키지 이름만 묶은 빈 패키지"다. 내용 파일은 없고 RDEPENDS만 있다. core-image-minimal이 넣는 유일한 것인 packagegroup-core-boot를 보자.

```sh
sed -n '/^RDEPENDS:\${PN} = /,/MACHINE_ESSENTIAL_EXTRA_RDEPENDS}"/p' $OE/meta/recipes-core/packagegroups/packagegroup-core-boot.bb
```

```text
RDEPENDS:${PN} = "\
    base-files \
    base-passwd \
    ${VIRTUAL-RUNTIME_base-utils} \
    ${@bb.utils.contains("DISTRO_FEATURES", "sysvinit", "${SYSVINIT_SCRIPTS}", "", d)} \
    ${@bb.utils.contains("MACHINE_FEATURES", "keyboard", "${VIRTUAL-RUNTIME_keymaps}", "", d)} \
    ${@bb.utils.contains("MACHINE_FEATURES", "efi", "${EFI_PROVIDER} kernel", "", d)} \
    netbase \
    ${VIRTUAL-RUNTIME_login_manager} \
    ${VIRTUAL-RUNTIME_init_manager} \
    ${VIRTUAL-RUNTIME_dev_manager} \
    ${VIRTUAL-RUNTIME_update-alternatives} \
    ${MACHINE_ESSENTIAL_EXTRA_RDEPENDS}"
```

출력에서 볼 것: K1 노트에서 본 "최소 rootfs의 필수 구성"이 그대로 여기 있다 — `base-files`(FHS 디렉터리와 `/etc` 기본 파일), `base-passwd`(`/etc/passwd`), 기본 유틸(BusyBox, K2), init, 장치 관리자(udev 또는 mdev), 로그인. 그리고 `MACHINE_ESSENTIAL_EXTRA_RDEPENDS`로 **보드가 부팅에 꼭 필요한 패키지**(펌웨어 blob, 커널 모듈)를 MACHINE conf에서 끼워 넣을 수 있게 열어 두었다.

### 7.4 우리 이미지와 packagegroup

```text
SUMMARY = "K4 sensor kit userspace bundle"
PACKAGE_ARCH = "${MACHINE_ARCH}"

inherit packagegroup

# can-utils는 oe-core가 아니라 meta-openembedded/meta-oe에 있다.
# 넣으려면 그 layer를 BBLAYERS에 추가하고 LAYERDEPENDS에 openembedded-layer를 적는다.
RDEPENDS:${PN} = "\
    k4-imu-reader \
    i2c-tools \
    iproute2 \
    "
```

```text
SUMMARY = "K4 sensor kit product image"
LICENSE = "MIT"

inherit core-image

# core-image-minimal과 같은 최소 부팅 세트 + 우리 패키지 묶음
IMAGE_INSTALL = "packagegroup-core-boot packagegroup-k4-sensors ${CORE_IMAGE_EXTRA_INSTALL}"

IMAGE_FEATURES += "ssh-server-dropbear"
IMAGE_LINGUAS = " "
IMAGE_FSTYPES = "ext4 tar.zst"

# 개발 빌드 전용 기능(빈 root 비밀번호 등)은 제품 레시피에 넣지 않고
# local.conf에서 EXTRA_IMAGE_FEATURES += "allow-root-login empty-root-password" 로 켠다
# (debug-tweaks는 5.2에서 제거됨)
```

위가 `packagegroup-k4-sensors.bb`, 아래가 `k4-sensorkit-image.bb`다. 파싱해서 최종적으로 무엇이 설치될지 본다.

```sh
I=/var/tmp/K4/k4_layers/meta-k4-sensorkit/recipes-core/images/k4-sensorkit-image.bb
$P $I IMAGE_INSTALL IMAGE_FEATURES FEATURE_INSTALL PACKAGE_INSTALL IMAGE_FSTYPES IMAGE_ROOTFS DEPLOY_DIR_IMAGE IMAGE_LINK_NAME 2>&1 | grep -v ^NOTE
$P $I @tasks 2>&1 | grep -E "^do_(rootfs|image|image_ext4|image_tar|image_complete) "
$P $OE/meta/recipes-core/images/core-image-minimal.bb PACKAGE_INSTALL IMAGE_FSTYPES 2>&1 | grep -v ^NOTE
G=/var/tmp/K4/k4_layers/meta-k4-sensorkit/recipes-core/packagegroups/packagegroup-k4-sensors.bb
$P $G PACKAGES ALLOW_EMPTY:packagegroup-k4-sensors PACKAGE_ARCH 2>&1 | grep -v ^NOTE
```

```text
IMAGE_INSTALL          = 'packagegroup-core-boot packagegroup-k4-sensors '
IMAGE_FEATURES         = 'ssh-server-dropbear'
FEATURE_INSTALL        = 'packagegroup-core-ssh-dropbear'
PACKAGE_INSTALL        = 'packagegroup-core-boot packagegroup-k4-sensors  run-postinsts packagegroup-core-ssh-dropbear'
IMAGE_FSTYPES          = 'ext4 tar.zst'
IMAGE_ROOTFS           = '/var/tmp/K4/k4_build/tmp/work/k4_sensorkit-poky-linux/k4-sensorkit-image/1.0/rootfs'
DEPLOY_DIR_IMAGE       = '/var/tmp/K4/k4_build/tmp/deploy/images/k4-sensorkit'
IMAGE_LINK_NAME        = 'k4-sensorkit-image-k4-sensorkit.rootfs'
do_rootfs                    after do_prepare_recipe_sysroot
do_image                     after do_flush_pseudodb do_create_rootfs_spdx do_write_qemuboot_conf do_image_qa do_rootfs
do_image_complete            after do_image_tar do_image_ext4 do_image
do_image_ext4                after do_image
do_image_tar                 after do_image
PACKAGE_INSTALL        = 'packagegroup-core-boot  run-postinsts '
IMAGE_FSTYPES          = ' tar.zst ext4.zst'
PACKAGES               = 'packagegroup-k4-sensors packagegroup-k4-sensors-dbg packagegroup-k4-sensors-dev packagegroup-k4-sensors-ptest'
ALLOW_EMPTY:packagegroup-k4-sensors = '1'
PACKAGE_ARCH           = 'k4_sensorkit'
```

출력에서 볼 것:

- `PACKAGE_INSTALL` = IMAGE_INSTALL + IMAGE_FEATURES가 끌어온 `FEATURE_INSTALL` + 자동으로 붙는 `run-postinsts`(첫 부팅 때 남은 설치 후 스크립트를 돌린다). do_rootfs가 실제로 패키지 관리자에게 넘기는 목록이 이것이다.
- `IMAGE_FSTYPES`가 `ext4 tar.zst`라서 `do_image_ext4`, `do_image_tar` 태스크가 생겼다. core-image-minimal은 qemu.inc의 기본값(`tar.zst ext4.zst`)을 그대로 쓴다. 실제 보드에서는 `wic`(파티션 테이블까지 포함한 디스크 이미지, K5·L 모듈)를 주로 쓴다.
- 이미지 작업 디렉터리의 아키텍처 부분은 `k4_sensorkit-poky-linux`다. 이미지와 packagegroup은 `PACKAGE_ARCH = "${MACHINE_ARCH}"`(MACHINE 이름의 `-`를 `_`로 바꾼 것)라서, 같은 tune을 공유하는 앱 패키지(`cortexa57`)와 달리 MACHINE마다 따로 만들어진다.
- `ALLOW_EMPTY = '1'`: 파일이 하나도 없는 패키지도 만들어라 — packagegroup 클래스가 켠다. 보통 레시피에서 빈 패키지는 만들어지지 않는다.

### 7.5 "이미지에 패키지를 어떻게 넣나" — 방법 정리

| 방법 | 어디에 | 언제 쓰나 | 주의 |
|---|---|---|---|
| 이미지 레시피의 `IMAGE_INSTALL` | `recipes-*/images/<이미지>.bb` | 제품 이미지 구성의 정석 | 버전 관리되고 리뷰된다 |
| packagegroup에 RDEPENDS로 추가 | `packagegroup-<이름>.bb` | 여러 이미지가 같은 묶음을 쓸 때 | 이미지에는 packagegroup 하나만 |
| `IMAGE_FEATURES` | 이미지 레시피 또는 `EXTRA_IMAGE_FEATURES`(local.conf) | ssh, 디버그 도구 같은 정형 기능 | 정의된 기능 이름만 가능 |
| `CORE_IMAGE_EXTRA_INSTALL` 또는 `IMAGE_INSTALL:append = " x"` | local.conf | 개발자 개인 실험 | **`+=` 금지**(3.6절). 커밋하지 않는다 |
| 다른 패키지의 `RDEPENDS`/`RRECOMMENDS` | 앱 레시피 | 앱이 실행에 필요로 하는 것 | 정확한 의존 관계 표현 |
| `MACHINE_EXTRA_RRECOMMENDS`, `MACHINE_ESSENTIAL_EXTRA_RDEPENDS` | MACHINE conf | 보드 펌웨어·커널 모듈 | 보드에만 해당하는 것만 |

공통 전제: 넣으려는 이름은 **레시피 이름이 아니라 패키지 이름**이다. `bitbake <레시피>`가 성공해도, `-dev`, `-tools`처럼 다른 패키지로 쪼개진 파일은 그 패키지 이름을 넣어야 들어간다.

---

## 8. bbappend — 남의 레시피를 고치는 정석

### 8.1 왜 레시피를 직접 고치지 않나

OE-Core의 busybox 레시피를 열어 한 줄 고치면 당장은 된다. 하지만 OE-Core는 남의 git 저장소다. 다음 보안 업데이트(`git pull`)에서 충돌하거나, 더 나쁘게는 고친 것이 조용히 사라진다. 그래서 규칙은 이렇다: **남의 layer는 읽기 전용으로 두고, 바꾸고 싶은 것은 우리 layer의 `.bbappend`로 덧씌운다.** Don의 SSD 펌웨어로 치면, 벤더 SDK 소스를 직접 고치지 않고 우리 쪽 패치 디렉터리와 빌드 스크립트로 얹던 것과 같다.

meta-poky 자신도 이렇게 한다.

```sh
cat /var/tmp/K4/k4_meta-yocto/meta-poky/recipes-core/busybox/busybox_%.bbappend
ls /var/tmp/K4/k4_meta-yocto/meta-poky/recipes-core/busybox/busybox/
```

```text
FILESEXTRAPATHS:prepend := "${THISDIR}/${BPN}:"

poky-tiny
```

출력에서 볼 것: 단 한 줄이다. `FILESEXTRAPATHS:prepend`로 자기 디렉터리를 file:// 검색 경로 맨 앞에 끼우고, `busybox/poky-tiny/` 아래에 poky-tiny DISTRO 전용 설정을 둔다. 4.4절의 FILESPATH 규칙(override 디렉터리 우선) 덕분에 DISTRO가 poky-tiny일 때만 그 파일이 쓰인다. 여기서 `:=`가 중요하다 — `${THISDIR}`는 "지금 파싱 중인 파일의 디렉터리"라서, 지연 확장(`=`)하면 나중에 다른 파일을 파싱하는 시점의 디렉터리가 들어가 버린다.

### 8.2 우리 bbappend — BusyBox에 I2C 도구 켜기

센서 키트 현장 디버깅에는 `i2cdetect`, `i2cget`과 레지스터를 직접 보는 `devmem`이 있으면 편하다(H 모듈). BusyBox 설정은 Kconfig라서 **설정 조각(config fragment)** 파일로 바꾼다.

```sh
L=/var/tmp/K4/k4_layers/meta-k4-sensorkit/recipes-core/busybox
cat $L/busybox_%.bbappend; echo "--- k4-sensorkit.cfg"; cat $L/busybox/k4-sensorkit.cfg
```

```text
# 우리 layer의 busybox/ 폴더를 file:// 검색 경로 맨 앞에 끼운다
FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

# .cfg 로 끝나는 SRC_URI 항목은 cml1.bbclass의 find_cfgs()가 골라
# do_configure에서 merge_config.sh로 defconfig 위에 덮어쓴다
SRC_URI += "file://k4-sensorkit.cfg"
```

```text
CONFIG_I2CGET=y
CONFIG_I2CSET=y
CONFIG_I2CDETECT=y
CONFIG_DEVMEM=y
# CONFIG_TELNETD is not set
```

bbappend 안에서는 `SRC_URI += ...`를 썼다. bbappend는 원래 레시피 **바로 뒤에** 이어 붙여 파싱되므로, 레시피가 이미 SRC_URI를 대입한 다음이라 `+=`가 안전하다(local.conf와 다른 점이다). `:append`를 써도 된다.

### 8.3 .cfg가 어떻게 적용되나 — 실제 코드와 실제 병합

```sh
grep -n "def find_cfgs" -A8 $OE/meta/classes-recipe/cml1.bbclass
grep -n "merge_config.sh -m .config" $OE/meta/recipes-core/busybox/busybox.inc
grep -n "^DEPENDS" $OE/meta/recipes-core/busybox/busybox.inc
```

```text
8:def find_cfgs(d):
9-    sources=src_patches(d, True)
10-    sources_list=[]
11-    for s in sources:
12-        if s.endswith('.cfg'):
13-            sources_list.append(s)
14-
15-    return sources_list
16-
149:	merge_config.sh -m .config ${@" ".join(find_cfgs(d))}
186:		merge_config.sh -m .config.orig .config.disable.apps
198:			merge_config.sh -m .config.nonapps .config.app.$s
6:DEPENDS += "kern-tools-native virtual/crypt"
7:DEPENDS += "${@bb.utils.contains('DISTRO_FEATURES', 'selinux', 'libselinux', '', d)}"
```

출력에서 볼 것: busybox의 do_configure(149행)는 SRC_URI 중 `.cfg`로 끝나는 것을 모두 모아 커널의 `merge_config.sh`로 `.config`에 병합한다. 그 스크립트를 제공하는 게 DEPENDS의 `kern-tools-native`다. 커널 레시피(linux-yocto)도 같은 방식으로 `.cfg` 조각을 쓴다(K5).

우리 bbappend가 SRC_URI에 실제로 붙는지 파서로 확인한다.

```sh
M=$OE/meta; PA=/var/tmp/K4/k4_meta-yocto/meta-poky/recipes-core/busybox/busybox_%.bbappend
BA=$L/busybox_%.bbappend
$P $M/recipes-core/busybox/busybox_1.37.0.bb @func:SRC_URI 2>&1 | grep -v ^NOTE | tr " " "\n" | grep -c "cfg$"
$P $M/recipes-core/busybox/busybox_1.37.0.bb +append=$PA +append=$BA @func:SRC_URI 2>&1 | grep -v ^NOTE \
  | tr " " "\n" | grep "cfg$" | tail -3
```

```text
12
file://rev.cfg
file://pgrep.cfg
file://k4-sensorkit.cfg
```

출력에서 볼 것: 원래 레시피에 `.cfg`가 12개 있고(Poky가 기본으로 켜는 기능들), bbappend를 붙이면 우리 조각이 맨 뒤에 추가된다. **merge_config.sh는 뒤에 오는 조각이 이긴다** — 그래서 bbappend로 붙인 조각이 OE-Core 조각보다 우선한다.

do_configure가 할 병합을 그대로 손으로 해 보자. OE-Core의 busybox defconfig에 우리 조각을 병합한다(merge_config.sh는 랩 커널 소스의 것을 썼다).

```sh
mkdir -p /tmp/K4/bbcfg && cd /tmp/K4/bbcfg
cp $OE/meta/recipes-core/busybox/busybox/defconfig .config
grep -E "^(# )?CONFIG_(I2CGET|I2CSET|I2CDETECT|DEVMEM|TELNETD)[= ]" .config
/var/lab/src/linux-6.18.55/scripts/kconfig/merge_config.sh -m .config $L/busybox/k4-sensorkit.cfg > merge.log 2>&1
grep -c "redefined by fragment" merge.log
grep -E "^(# )?CONFIG_(I2CGET|I2CSET|I2CDETECT|DEVMEM|TELNETD)[= ]" .config
```

```text
# CONFIG_DEVMEM is not set
# CONFIG_I2CGET is not set
# CONFIG_I2CSET is not set
# CONFIG_I2CDETECT is not set
# CONFIG_TELNETD is not set
4
CONFIG_I2CGET=y
CONFIG_I2CSET=y
CONFIG_I2CDETECT=y
CONFIG_DEVMEM=y
# CONFIG_TELNETD is not set
```

출력에서 볼 것: 4개 항목이 "redefined by fragment"로 바뀌었다. TELNETD는 원래 꺼져 있어서 바뀐 게 없다 — 조각에 넣어 둔 이유는 "누가 defconfig에서 켜도 우리 제품에서는 꺼진다"는 **의도의 문서화**다. `-m`(merge only)은 `make olddefconfig`를 돌리지 않으므로 의존성 정리는 do_configure의 다음 단계(`oe_runmake oldconfig`, cml1.bbclass)가 한다.

### 8.4 bbappend로 흔히 하는 일

| 하고 싶은 것 | bbappend에 쓸 것 |
|---|---|
| 패치 추가 | `FILESEXTRAPATHS:prepend := "${THISDIR}/files:"` + `SRC_URI += "file://0001-fix.patch"` |
| 설정 파일 교체 | 같은 이름의 파일을 우리 layer `files/`에 두기 (FILESPATH 우선순위) |
| 빌드 옵션 | `EXTRA_OECONF:append = " --enable-x"`, `EXTRA_OECMAKE:append = " -DFOO=ON"`, `PACKAGECONFIG:append = " x"` |
| 설치 단계에 추가 | `do_install:append() { install -m 0644 ${UNPACKDIR}/x.conf ${D}${sysconfdir}/ }` |
| 특정 MACHINE에서만 | `SRC_URI:append:k4-sensorkit = " file://board.cfg"` |
| 기능 끄기 | `PACKAGECONFIG:remove = "x11"` |

반대로 **레시피를 직접 고치거나 새로 쓰는 게 맞는 경우**도 있다: 우리 layer에 있는 우리 레시피, 업스트림에 보낼 수정(OE-Core에 패치를 기여), 버전 자체를 올리는 경우(우리 layer에 새 버전 `.bb`를 두고 `PREFERRED_VERSION`). bbappend가 원래 레시피의 절반을 뒤집고 있다면, 그건 새 레시피를 쓸 때가 된 것이다.

---

## 9. 예제 layer 전체 — meta-k4-sensorkit

지금까지 조각으로 본 파일을 모으면 이렇게 된다.

```sh
cd /var/tmp/K4/k4_layers && find meta-k4-sensorkit -type f | sort
```

```text
meta-k4-sensorkit/README
meta-k4-sensorkit/conf/distro/k4-distro.conf
meta-k4-sensorkit/conf/layer.conf
meta-k4-sensorkit/conf/machine/k4-sensorkit.conf
meta-k4-sensorkit/recipes-apps/k4-imu-reader/files/k4-imu-reader.service
meta-k4-sensorkit/recipes-apps/k4-imu-reader/files/k4-imu.conf
meta-k4-sensorkit/recipes-apps/k4-imu-reader/files/k4_imu_reader.c
meta-k4-sensorkit/recipes-apps/k4-imu-reader/k4-imu-reader_1.0.bb
meta-k4-sensorkit/recipes-core/busybox/busybox/k4-sensorkit.cfg
meta-k4-sensorkit/recipes-core/busybox/busybox_%.bbappend
meta-k4-sensorkit/recipes-core/images/k4-sensorkit-image.bb
meta-k4-sensorkit/recipes-core/packagegroups/packagegroup-k4-sensors.bb
```

layer.conf는 OE-Core와 같은 형식이다.

```text
# meta-k4-sensorkit: 센서 키트 제품 layer (K4 노트 예제)
BBPATH .= ":${LAYERDIR}"

BBFILES += "${LAYERDIR}/recipes-*/*/*.bb \
            ${LAYERDIR}/recipes-*/*/*.bbappend"

BBFILE_COLLECTIONS += "k4-sensorkit"
BBFILE_PATTERN_k4-sensorkit = "^${LAYERDIR}/"
BBFILE_PRIORITY_k4-sensorkit = "10"

LAYERDEPENDS_k4-sensorkit = "core"
LAYERSERIES_COMPAT_k4-sensorkit = "wrynose"
```

실제로 검증한 것을 정리하면:

| 검증 | 방법 | 결과 |
|---|---|---|
| layer 등록 | `bitbake-layers add-layer`, `show-layers` | 통과, priority 10 (2.3절) |
| MACHINE/DISTRO conf 문법 | fragment로 선택 후 `bitbake-getvar` | 통과, override 함정 발견·수정 (7.1절) |
| 앱 레시피 | tinfoil 파싱 (S/B/D, 태스크, 패키지, DEPENDS) | 통과 (4.3, 5절) |
| 라이선스 체크섬 | `sed -n 1,2p ... ⟶ md5sum` | `7654d159…` 레시피와 일치 |
| C 앱 | VM gcc `-Wall -Wextra` | 경고 0, 가짜 IIO로 실행 확인 |
| bbappend | busybox를 append와 함께 파싱 → SRC_URI·FILESPATH | 통과 (4.4, 8.3절) |
| BusyBox 조각 | merge_config.sh로 실제 병합 | 4개 옵션 변경 (8.3절) |
| 이미지 · packagegroup | tinfoil 파싱 → PACKAGE_INSTALL, 태스크 | 통과 (7.4절) |
| 크로스 빌드 · QA · 실제 rootfs | `bitbake k4-sensorkit-image` | **하지 않음** (11절 가이드) |

layer 품질 검사 도구 `yocto-check-layer`도 있다. **(실행하지 않음 — 가이드)** 이 도구는 내부적으로 레시피 파싱과 시그니처 비교를 하므로 이 VM에서는 user namespace 문제로 돌지 않는다.

```sh
# 빌드 환경을 잡은 셸에서
yocto-check-layer /path/to/meta-k4-sensorkit
# 검사 내용(요지): layer.conf 필수 변수, LAYERSERIES_COMPAT, README 존재,
# 그리고 "이 layer를 추가해도 MACHINE/DISTRO를 고르지 않는 한
# 다른 레시피의 task signature가 바뀌지 않는가"(BSP·DISTRO layer는 자기 설정이 선택됐을 때만 영향)
```

마지막 항목이 중요하다. 좋은 layer는 **"추가만 해서는 아무것도 바꾸지 않는다."** 우리 busybox bbappend는 이 원칙을 어긴다 — layer를 추가하기만 해도 모든 MACHINE의 BusyBox에 i2c 도구가 켜진다. 제대로 하려면 `SRC_URI:append:k4-sensorkit = " file://k4-sensorkit.cfg"`처럼 우리 MACHINE(또는 DISTRO) override로 한정해야 한다. 이건 연습문제로 남긴다.

---

## 10. 호스트 요구 사항 — 그리고 Ubuntu 24.04+의 AppArmor 문제

### 10.1 공식 요구 사항

Yocto 문서(yocto-docs `wrynose` 브랜치의 `ref-manual/system-requirements.rst`, 숫자는 `poky.yaml.in`)에서 직접 꺼냈다.

```sh
cd /var/tmp/K4/k4_yocto-docs/documentation
grep -n "^MIN_DISK_SPACE\|^MIN_RAM\|^MIN_GIT\|^MIN_GCC\|^MIN_MAKE\|^MIN_PYTHON\|^MIN_TAR" poky.yaml.in
grep -n "Ubuntu" ref-manual/system-requirements.rst | head -5
```

```text
38:MIN_PYTHON_VERSION : "3.9.0"
39:MIN_TAR_VERSION : "1.28"
40:MIN_GIT_VERSION : "1.8.3.1"
41:MIN_GCC_VERSION : "10.1"
42:MIN_MAKE_VERSION : "4.0"
44:MIN_DISK_SPACE : "140"
46:MIN_DISK_SPACE_RM_WORK : "40"
48:MIN_RAM : "32"
78:-  Ubuntu 22.04 (LTS)
79:-  Ubuntu 24.04 (LTS)
80:-  Ubuntu 25.04
81:-  Ubuntu 25.10
82:-  Ubuntu 26.04 (LTS)
```

출력에서 볼 것:

- 디스크 **140 GB**(`rm_work`를 쓰면 40 GB), RAM **32 GB**. 이 숫자는 `poky.yaml.in` 주석에 따르면 Ubuntu 22.04에서 qemux86-64용 core-image-sato(그래픽 데스크톱 포함 이미지)를 만들 때 기준이다. core-image-minimal은 훨씬 적게 들지만, "Yocto 빌드 머신은 넉넉하게"가 실무의 상식이다. 흔히 말하는 "50~90 GB"는 minimal 이미지 + rm_work 수준의 경험치다.
- 지원 배포판 목록에 **Ubuntu 26.04가 있다.** 그런데도 이 VM에서 빌드가 안 되는 이유가 다음 절이다.

이 VM의 사양과 비교한다.

```sh
grep PRETTY /etc/os-release; nproc; free -g | sed -n 2p; df -h /var/tmp | tail -1
```

```text
PRETTY_NAME="Ubuntu 26.04.1 LTS"
6
Mem:              11           0           3           0           7          10
/dev/vda1        96G   28G   68G  30% /
```

출력에서 볼 것: 6코어, RAM 11 GB, 여유 68 GB. AppArmor 문제가 없더라도 core-image-sato 기준에는 못 미친다. core-image-minimal + `rm_work` + `BB_NUMBER_THREADS`/`PARALLEL_MAKE` 축소(RAM 부족 시 gcc·rust 빌드가 OOM으로 죽는다)라면 가능성은 있지만, 직접 확인하지 않았으므로 단정하지 않는다.

### 10.2 실제로 막히는 지점

```sh
. /Users/donh/.elinux_scratch/K4/k4_env.sh
timeout 500 bitbake -p > /var/tmp/K4/k4_parse.log 2>&1; echo "rc=$?"
cat /var/tmp/K4/k4_parse.log
```

```text
rc=1
ERROR: User namespaces are not usable by BitBake, possibly due to AppArmor.
See https://discourse.ubuntu.com/t/ubuntu-24-04-lts-noble-numbat-release-notes/39890#unprivileged-user-namespace-restrictions for more information.

Summary: There was 1 ERROR message, returning a non-zero exit code.
```

출력에서 볼 것: `bitbake -p`는 "모든 레시피를 파싱만 하라"는 옵션인데, 빌드든 파싱이든 **레시피 처리를 시작하기 전의 sanity 검사**에서 멈춘다. 반면 지금까지 쓴 `bitbake-layers`, `bitbake-config-build`, `bitbake-getvar`(설정 변수), tinfoil `parse_recipe_file`(레시피 하나)은 이 검사를 거치지 않아서 동작했다.

BitBake는 왜 user namespace가 필요한가. 답은 3.5절의 `do_fetch[network] = "1"`에 있다. **BitBake는 do_fetch 말고는 모든 태스크를 네트워크가 없는 상태에서 실행한다.** configure 스크립트가 몰래 인터넷에서 뭔가를 받아 빌드 결과가 날마다 달라지는 것을 막기 위해서다(재현성, K5). 이걸 root 권한 없이 하는 방법이 "새 user namespace + 새 network namespace"를 만드는 것이다. 실제 코드를 보자.

```sh
sed -n 284,289p /var/tmp/K4/k4_bitbake/bin/bitbake-worker
sed -n 2058,2065p /var/tmp/K4/k4_bitbake/lib/bb/utils.py
```

```text
                if not bb.utils.to_boolean(the_data.getVarFlag(taskname, 'network')):
                    if bb.utils.is_local_uid(uid):
                        logger.debug("Attempting to disable network for %s" % taskname)
                        bb.utils.disable_network(uid, gid)
                    else:
                        logger.debug("Skipping disable network for %s since %s is not a local uid." % (taskname, uid))
    ret = libc.unshare(CLONE_NEWNET | CLONE_NEWUSER)
    if ret != 0:
        logger.debug("System doesn't support disabling network without admin privs")
        return
    with open("/proc/self/uid_map", "w") as f:
        f.write("%s %s 1" % (uid, uid))
    with open("/proc/self/setgroups", "w") as f:
        f.write("deny")
```

출력에서 볼 것: bitbake-worker는 태스크를 fork한 뒤 `[network]` flag가 없으면 `unshare(CLONE_NEWNET | CLONE_NEWUSER)`를 하고, 새 user namespace 안에서 자기 uid를 그대로 매핑하려고 `/proc/self/uid_map`에 쓴다.

이 함수를 직접 불러 보면 어디서 실패하는지 정확히 보인다(BitBake 서버 없이 라이브러리 함수만 호출).

```python
# BitBake가 태스크마다 쓰는 네트워크 차단 함수를 직접 불러 본다 (자식 프로세스에서)
import os, sys
sys.path.insert(0, "/var/tmp/K4/k4_bitbake/lib")
import bb.utils

print("uid before:", os.getuid(), flush=True)
pid = os.fork()
if pid == 0:
    try:
        bb.utils.disable_network()
        print("child: disable_network() OK, uid now", os.getuid())
    except Exception as e:
        print("child: exception", type(e).__name__, e)
    sys.stdout.flush()
    os._exit(0)
os.waitpid(pid, 0)
```

```sh
sysctl kernel.apparmor_restrict_unprivileged_userns
unshare --user --map-root-user --net true; echo "unshare rc=$?"
python3 /Users/donh/.elinux_scratch/K4/k4_userns.py
```

```text
kernel.apparmor_restrict_unprivileged_userns = 1
unshare: write failed /proc/self/uid_map: Operation not permitted
unshare rc=1
uid before: 501
child: exception PermissionError [Errno 1] Operation not permitted
```

출력에서 볼 것: `unshare()` 시스템 콜 자체는 성공하고, 그다음 **`uid_map` 쓰기가 EPERM**으로 실패한다. util-linux의 `unshare` 명령도 같은 지점에서 실패한다. sanity.bbclass의 주석이 정확히 이 상황을 설명한다("unshare succeeds (at which point the uid is nobody) but writing to the uid_map fails").

왜 그런가. Ubuntu 24.04부터 AppArmor가 "프로파일이 없는(unconfined) 비특권 프로세스가 user namespace를 만들면 `unprivileged_userns` 프로파일로 전환"시킨다. 그 프로파일이 이렇다.

```sh
grep -v "^\s*#" /etc/apparmor.d/unprivileged_userns | grep -v "^$" | head -9
```

```text
abi <abi/5.0>,
include <tunables/global>
profile unprivileged_userns {
     audit deny capability,
     audit deny change_profile,
     allow network,
     allow signal,
     allow dbus,
     allow file rwlkm /{,**},
```

출력에서 볼 것: `audit deny capability` — namespace 안에서 얻었어야 할 capability(CAP_SETUID 등)를 전부 거부한다. uid_map을 쓰려면 그 capability가 필요하므로 실패한다. 커널 쪽 경로는 13절에서 그림으로 본다.

### 10.3 공식 해결책 — 무엇을 해야 하나 (실행하지 않음 — 가이드)

이 VM은 여러 노트 작성자가 같이 쓰는 공용 환경이라 전역 설정을 바꾸지 않았다. 아래는 Don이 자기 빌드 머신에서 고를 선택지다. BitBake의 에러 메시지가 가리키는 Ubuntu 24.04 릴리스 노트("Unprivileged user namespace restrictions" 절)가 1차 출처이고, Yocto 문서(ref-manual)에는 별도 절이 없다(wrynose yocto-docs에서 apparmor를 grep하면 관련 내용이 나오지 않는다).

| 선택지 | 방법 | 범위 | 평가 |
|---|---|---|---|
| ① 실행 파일에 AppArmor 프로파일 | `/etc/apparmor.d/`에 `flags=(unconfined)` + `userns,` 프로파일 | 그 프로그램만 | Ubuntu 릴리스 노트가 권하는 방식. 가장 좁다 |
| ② 재부팅 전까지 해제 | `/proc/sys/kernel/apparmor_restrict_unprivileged_userns`에 0을 쓴다 (아래 명령 ②) | 시스템 전체, 일시 | 빠른 확인용 |
| ③ 영구 해제 | `/etc/sysctl.d/60-apparmor-namespace.conf`에 `kernel.apparmor_restrict_unprivileged_userns=0` | 시스템 전체, 영구 | 전용 빌드 서버라면 흔히 쓴다. 보안 완화 효과는 사라진다 |
| ④ 다른 호스트 | Ubuntu 22.04(제한 없음) VM/서버, 또는 이 제한이 없는 배포판 | 빌드 환경 분리 | CI에서 흔한 선택 |

```sh
# ① bitbake 실행 파일에 userns를 허용하는 프로파일 (경로는 자기 clone 위치로)
#    lxc-usernsexec 프로파일(이 VM에 실제로 있는 것)과 같은 형식이다.
sudo tee /etc/apparmor.d/bitbake <<'EOF'
abi <abi/4.0>,
include <tunables/global>

profile bitbake /home/*/yocto/layers/bitbake/bin/bitbake flags=(unconfined) {
  userns,
}
EOF
sudo apparmor_parser -r /etc/apparmor.d/bitbake

# ② 재부팅 전까지 전체 해제
echo 0 | sudo tee /proc/sys/kernel/apparmor_restrict_unprivileged_userns

# ③ 영구 해제
echo "kernel.apparmor_restrict_unprivileged_userns=0" | sudo tee /etc/sysctl.d/60-apparmor-namespace.conf
sudo sysctl --system
```

①의 형식은 이 VM에 이미 설치된 lxc 프로파일에서 확인할 수 있다.

```sh
grep -v "^\s*#" /etc/apparmor.d/lxc-usernsexec | grep -v "^$"
```

```text
abi <abi/5.0>,
include <tunables/global>
profile lxc-usernsexec /usr/bin/lxc-usernsexec flags=(unconfined) {
  userns,
  @{exec_path} mr,
  include if exists <local/lxc-usernsexec>
}
```

주의할 점 두 가지:

- ①은 `bitbake`가 Python 스크립트라 프로파일이 붙는 실행 파일 경로가 어디인지(스크립트 경로인지 python 인터프리터인지)에 따라 동작이 달라질 수 있다. 이 노트에서는 검증하지 못했다. 막히면 ③이나 ④를 쓴다.
- 고친 뒤에도 같은 에러가 나면 빌드 디렉터리의 `tmp/`를 지우고 다시 시도한다. sanity.bbclass는 이 검사를 `check_sanity_version_change()` 안에서 한다 — 코드 주석대로 "SANITY_VERSION이나 호스트 배포판이 바뀔 때 한 번" 도는 검사들이라, 실패 상태가 캐시되어 남을 수 있다.

---

## 11. core-image-minimal을 qemuarm64로 빌드하고 runqemu로 부팅 — 가이드

**(이 절 전체가 실행하지 않음 — 가이드다. 출력 예시는 붙이지 않는다.)** Don이 10.3의 문제를 해결한 빌드 머신에서 그대로 따라 할 수 있게, 실제 Wrynose 파일에서 확인한 명령만 썼다.

### 11.1 방법 A — bitbake-setup (Wrynose 권장)

```sh
python3 -m venv ./bitbake-setup-venv
. ./bitbake-setup-venv/bin/activate
pip install bitbake-setup
# 레지스트리의 poky-wrynose 설정 + poky DISTRO + qemuarm64 MACHINE
bitbake-setup init --non-interactive poky-wrynose poky distro/poky machine/qemuarm64
# init이 끝나면 setup 디렉터리 경로를 출력한다. 그 안의 build/init-build-env를 source
source <setup 디렉터리>/build/init-build-env
```

`poky-wrynose`, `distro/poky`, `machine/qemuarm64`는 BitBake 2.18의 `default-registry/configurations/poky-wrynose.conf.json`에 있는 이름이다(0.2절). yocto-docs의 Quick Build 문서는 같은 형식으로 `poky-master poky distro/poky machine/qemux86-64` 예를 든다.

### 11.2 방법 B — 저장소를 직접 clone (이 노트가 쓴 방식)

```sh
mkdir -p ~/yocto/layers && cd ~/yocto
git clone -b wrynose https://git.openembedded.org/openembedded-core layers/openembedded-core
git clone -b wrynose https://git.yoctoproject.org/meta-yocto layers/meta-yocto
git clone -b 2.18    https://git.openembedded.org/bitbake layers/bitbake
export BITBAKEDIR=$PWD/layers/bitbake          # oe-init-build-env가 bitbake를 찾는 위치
source layers/openembedded-core/oe-init-build-env build
bitbake-layers add-layer ../layers/meta-yocto/meta-poky ../layers/meta-yocto/meta-yocto-bsp
bitbake-config-build enable-fragment machine/qemuarm64 distro/poky
# 선택: Yocto CDN의 sstate 미러 사용 (네트워크가 좋을 때 빌드 시간을 크게 줄인다)
bitbake-config-build enable-fragment core/yocto/sstate-mirror-cdn
```

`yocto-docs`의 `dev-manual/poky-manual-setup.rst`는 `layers/` 아래에 clone하고 `oe-init-build-env`를 source하는 같은 흐름을 안내한다(문서는 `-b` 뒤에 최신 릴리스 태그를 쓴다. 제품 빌드라면 브랜치보다 **태그나 커밋 해시로 고정**하는 게 맞다 — K5의 재현성).

### 11.3 빌드와 실행

```sh
bitbake-getvar --value MACHINE        # qemuarm64 확인
bitbake core-image-minimal            # 첫 빌드: 툴체인부터 전부 → 수 시간 단위 (sstate 미러가 있으면 크게 단축)
ls tmp/deploy/images/qemuarm64/       # Image, *.rootfs.ext4.zst, *.qemuboot.conf 등
runqemu qemuarm64 nographic slirp     # 시리얼 콘솔을 현재 터미널로, root 권한 없는 user 네트워킹
# 로그인: root (개발용 fragment/EXTRA_IMAGE_FEATURES를 켰을 때 비밀번호 없음)
# QEMU 종료: 게스트에서 poweroff, 또는 Ctrl-a x
```

- `runqemu`의 옵션은 `scripts/runqemu`의 usage에 있다: `nographic`(비디오 끄기), `slirp`(root 없이 user networking), `snapshot`(이미지에 변경 내용을 쓰지 않음), `serialstdio`, `qemuparams=`, `bootparams=`.
- runqemu가 쓰는 QEMU는 호스트 것이 아니라 Yocto가 빌드한 `qemu-system-native`다. qemu MACHINE은 `qemu.inc`가 `IMAGE_CLASSES += "qemuboot"`를 켜고, qemuboot.bbclass가 `EXTRA_IMAGEDEPENDS += "qemu-system-native qemu-helper-native:do_addto_recipe_sysroot"`를 넣어 이미지 빌드 때 같이 만든다.
- `*.qemuboot.conf`에 qemuarm64.conf의 `QB_*` 값(7.1절)이 정리되어 들어가고, runqemu는 그 파일을 읽어 QEMU 명령줄을 만든다.

### 11.4 레시피 하나만 다룰 때 자주 쓰는 명령

```sh
bitbake k4-imu-reader                       # 레시피 하나 빌드 (do_build까지)
bitbake k4-imu-reader -c compile -f         # 특정 태스크를 강제로 다시
bitbake k4-imu-reader -c devshell           # 빌드 환경 그대로의 셸 (CC, CFLAGS가 export됨)
bitbake k4-imu-reader -c cleansstate        # 작업 디렉터리 + sstate 결과 삭제 (진짜 처음부터)
bitbake-getvar -r k4-imu-reader WORKDIR     # 레시피 문맥의 변수 값 (이 VM에서는 sanity 때문에 실패)
bitbake -g k4-sensorkit-image               # 의존성 그래프 (task-depends.dot, pn-buildlist)
bitbake-layers show-appends                 # 어떤 bbappend가 어떤 레시피에 붙었나
oe-pkgdata-util find-path /usr/bin/k4-imu-reader   # 이 파일은 어느 패키지에서 왔나
oe-pkgdata-util list-pkg-files k4-imu-reader       # 패키지에 든 파일 목록
```

`bitbake-getvar -r <레시피>`는 이 VM에서 실제로 시도했고 실패했다(레시피 문맥은 cooker를 완전히 준비하므로 sanity 검사에 걸린다). 그래서 이 노트에서는 tinfoil `parse_recipe_file`을 썼다.

---

## 12. Buildroot vs Yocto

K3에서 Buildroot를 다룬다. 면접 단골 비교라 핵심만 표로 둔다.

| 관점 | Buildroot | Yocto / OE |
|---|---|---|
| 기본 단위 | 결과물은 **rootfs 이미지 하나** | 결과물은 **패키지 피드 + 이미지들** |
| 설정 방식 | Kconfig(`make menuconfig`), defconfig 한 장 | layer · conf · recipe 메타데이터, BitBake 언어 |
| 패키지 관리 | 기기에 패키지 관리자 없음 | rpm/ipk/deb 패키지를 만들고, 원하면 기기에서 설치·업데이트 |
| 학습 곡선 | 낮다. Makefile을 알면 하루 | 높다. 변수 문법, override, 클래스 상속 |
| 첫 빌드 시간 | 짧다(작은 이미지 기준 수십 분 수준) | 길다(툴체인부터 전부, 수 시간 단위) |
| 증분 빌드 | 설정을 바꾸면 전체 재빌드가 안전한 경우가 많다 | task hash + sstate로 바뀐 부분만 정확히 |
| 여러 보드·제품 | defconfig를 보드마다 복제 | MACHINE/DISTRO/IMAGE 분리, layer 재사용 |
| 벤더 지원 | 일부 SoC | NXP·TI·Qualcomm·NVIDIA 등 대부분 SoC 벤더가 공식 BSP layer 제공 |
| 라이선스·SBOM | `make legal-info` | 레시피마다 LICENSE 검사, SPDX SBOM 기본 생성 |
| 잘 맞는 곳 | 단일 제품, 작은 팀, 빠른 프로토타입 | 제품군, 장기 유지보수(LTS), 벤더 BSP 기반, 큰 팀 |

한 줄 요약: **Buildroot는 "이미지를 만드는 Makefile", Yocto는 "배포판을 만드는 프레임워크"다.** 센서 키트 스타트업이 i.MX8 EVK로 데모를 만들 때는 Buildroot가 빠르지만, 양산 보드 여러 개와 5년 지원이 걸리면 NXP의 meta-freescale 위에 Yocto로 가는 경우가 많다.

---

## 13. 커널 안에서는 무슨 일이 일어나나

Yocto는 빌드 시스템이라 런타임 커널 코드와 직접 맞닿는 곳이 적다. 하지만 **두 군데**에서 커널과 연결된다.

### 13.1 빌드 호스트 커널 — user namespace와 LSM

10절의 실패는 순수하게 호스트 커널의 결정이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="8" y="8" width="446" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="231.0" y="28.0" font-size="12" text-anchor="middle">bitbake-worker: 태스크 프로세스 fork (uid 501)</text><line x1="231" y1="42" x2="231" y2="52" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/><rect x="8" y="53" width="446" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="231.0" y="73.0" font-size="12" text-anchor="middle">unshare(CLONE_NEWUSER | CLONE_NEWNET)</text><line x1="231" y1="87" x2="231" y2="97" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/><rect x="8" y="98" width="446" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="231.0" y="118.0" font-size="12" text-anchor="middle">커널: create_user_ns() → LSM 훅 userns_create</text><line x1="231" y1="132" x2="231" y2="142" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/><rect x="8" y="143" width="446" height="34" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="231.0" y="163.0" font-size="12" text-anchor="middle">AppArmor: 비특권 + unconfined → unprivileged_userns 프로파일</text><line x1="231" y1="177" x2="231" y2="187" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/><rect x="8" y="188" width="446" height="34" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="231.0" y="208.0" font-size="12" text-anchor="middle">새 namespace 안의 capability 거부 (audit deny capability)</text><line x1="231" y1="222" x2="231" y2="232" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/><rect x="8" y="233" width="446" height="34" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="231.0" y="253.0" font-size="12" text-anchor="middle">/proc/self/uid_map 쓰기 → EPERM (Operation not permitted)</text><line x1="231" y1="267" x2="231" y2="277" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah9)"/><rect x="8" y="278" width="446" height="34" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="231.0" y="298.0" font-size="12" text-anchor="middle">sanity.bbclass: User namespaces are not usable by BitBake</text><text x="466.0" y="24.0" font-size="13" text-anchor="start" font-weight="bold">통과하는 경우</text><text x="466.0" y="46.0" font-size="12" text-anchor="start">sysctl ...userns = 0</text><text x="466.0" y="66.0" font-size="12" text-anchor="start">(전체 해제 — 공용 VM 금지)</text><text x="466.0" y="96.0" font-size="12" text-anchor="start">해당 실행 파일에 프로파일:</text><text x="466.0" y="116.0" font-size="12" text-anchor="start">flags=(unconfined) + userns,</text><text x="466.0" y="146.0" font-size="12" text-anchor="start">root(특권)로 실행</text><text x="466.0" y="166.0" font-size="12" text-anchor="start">(BitBake는 root 실행 거부)</text><text x="466.0" y="196.0" font-size="12" text-anchor="start">Ubuntu 22.04 호스트</text><text x="466.0" y="216.0" font-size="12" text-anchor="start">(이 제한이 없음)</text></svg>
```
그림 9 — BitBake 태스크가 네트워크를 끊으려다 실패하는 커널 경로. 오른쪽은 통과하는 조건이다.

- `unshare(CLONE_NEWUSER)`는 커널의 `create_user_ns()`로 새 `struct user_namespace`를 만든다. 리눅스 6.1부터 이 지점에 LSM 훅 `userns_create`가 있어서 보안 모듈이 허용 여부를 정할 수 있다.
- Ubuntu의 AppArmor는 sysctl `kernel.apparmor_restrict_unprivileged_userns`가 1이면 unconfined 비특권 프로세스를 `unprivileged_userns` 프로파일로 전환한다. 그 프로파일은 capability를 거부한다. uid_map 쓰기를 처리하는 `map_write()`(kernel/user_namespace.c)는 대상 namespace 안의 `CAP_SYS_ADMIN`을 `file_ns_capable()`로 확인하므로, 여기서 EPERM이 난다. 랩 커널 소스(6.18.55)에서 `create_user_ns()`의 `security_create_user_ns()` 호출(118행)과 `lsm_hook_defs.h`의 `userns_create` 훅을 확인할 수 있다. 이 sysctl 자체는 업스트림 AppArmor에는 없고(랩 소스 `security/apparmor/`에서 grep되지 않는다) Ubuntu 커널 패치다.
- 이 메커니즘(`struct cred`, capability, LSM 훅)은 드라이버를 쓸 때도 다시 만난다. `ioctl` 핸들러에서 `capable(CAP_SYS_ADMIN)`으로 권한을 확인하는 코드가 바로 같은 capability 체계를 쓴다(F·G 모듈).

### 13.2 타깃 커널 — 커널도 레시피다

Yocto 이미지 안의 커널은 `virtual/kernel`을 제공하는 레시피(linux-yocto, 벤더의 linux-imx 등)가 만든다. 5.3절에서 본 대로 MACHINE이 고르고, `KERNEL_IMAGETYPE`(qemuarm64는 `Image`)과 `KERNEL_DEVICETREE`가 산출물을 정한다. 연결 지점을 모듈 ID로 정리한다.

| 커널 쪽 개념 | Yocto에서 만나는 곳 | 자세히 |
|---|---|---|
| Kconfig, defconfig, config fragment | linux-yocto의 `.cfg`/`.scc` 조각, `bitbake -c menuconfig virtual/kernel` | E3, K5 |
| out-of-tree 모듈 | `inherit module` 레시피 → `kernel-module-<이름>` 패키지 | E2, 4.5절 |
| Device Tree | MACHINE conf의 `KERNEL_DEVICETREE`, dts 패치를 bbappend로 | D 모듈, K5 |
| 커널 모듈 자동 로드 | `KERNEL_MODULE_AUTOLOAD += "k4_imu"` → `/etc/modules-load.d/` | G 모듈 |
| 펌웨어 blob | `linux-firmware` 패키지 분할, `MACHINE_EXTRA_RRECOMMENDS` | P 모듈 |
| 커널 헤더 (유저 공간용) | `linux-libc-headers` 레시피 → 모든 레시피의 sysroot | B2, A 모듈 |

IMU 드라이버를 out-of-tree로 개발한다면 흐름은 이렇다: G·H 모듈에서 쓴 드라이버 소스 → `inherit module` 레시피(우리 layer) → `KERNEL_MODULE_AUTOLOAD`와 DT 노드(bbappend로 커널에 dts 패치) → 이미지의 packagegroup에 `kernel-module-k4-imu` 추가. 커널 레시피에 패치를 넣는 구체적인 방법과 `devtool modify`는 K5에서 다룬다.

---

## 14. Embedded 실무에서 다시 보기

### 14.1 벤더 BSP 위에 제품 layer를 쌓는 구조

i.MX8M Plus 기반 센서 키트라면 bblayers.conf는 대략 이렇게 생긴다. **(실행하지 않음 — 가이드: layer 이름은 각 벤더 저장소의 이름이고, 실제 브랜치 호환성은 각 layer의 LAYERSERIES_COMPAT로 확인해야 한다)**

```text
BBLAYERS = " \
  ${TOPDIR}/../layers/openembedded-core/meta \
  ${TOPDIR}/../layers/meta-yocto/meta-poky \
  ${TOPDIR}/../layers/meta-openembedded/meta-oe \
  ${TOPDIR}/../layers/meta-openembedded/meta-python \
  ${TOPDIR}/../layers/meta-freescale \
  ${TOPDIR}/../layers/meta-mycompany-bsp \
  ${TOPDIR}/../layers/meta-mycompany-distro \
  ${TOPDIR}/../layers/meta-mycompany-sensorkit \
  "
```

회사 layer를 셋으로 나누는 게 흔한 구조다: **BSP layer**(MACHINE conf, 커널·U-Boot bbappend, dts), **DISTRO layer**(정책, 보안 플래그, DISTRO_FEATURES 정리), **제품 layer**(앱 레시피, packagegroup, 이미지). 다음 보드가 나오면 BSP layer만, 다음 제품이 나오면 제품 layer만 바뀐다. 실제 저장소 구성(repo manifest, kas, bitbake-setup 설정 파일)은 K5와 P 모듈에서 다룬다.

### 14.2 "필드 장비에서 설정 파일이 업데이트로 초기화됐다"

현장 로봇의 `/etc/k4-imu.conf`를 기술자가 보정값에 맞게 고쳤는데, 소프트웨어 업데이트 후 원래대로 돌아갔다. 원인 후보는 두 가지다.

- 패키지 단위 업데이트라면 `CONFFILES`를 빠뜨렸을 가능성이 높다. 6.1절처럼 표시하면 패키지 관리자가 사용자 수정본을 보존한다.
- 이미지 단위(A/B) 업데이트라면 rootfs 전체가 바뀌므로 CONFFILES로는 못 막는다. 보정값은 별도 데이터 파티션(`/data`)에 두고, 레시피의 기본 conf는 "보정값이 없을 때의 기본값"으로만 쓰는 설계가 필요하다(J5, L 모듈).

### 14.3 "빌드는 되는데 기기에서 Python 스크립트가 안 돈다"

센서 로깅 스크립트(`#!/usr/bin/env python3`)를 레시피로 설치했는데 기기에서 `python3: not found`. do_package는 ELF 바이너리의 공유 라이브러리 의존성은 자동으로 RDEPENDS에 넣지만, **스크립트의 shebang 인터프리터는 넣지 않는다.** `RDEPENDS:${PN} += "python3-core python3-json"`처럼 직접 적어야 한다. do_package_qa의 `file-rdeps` 검사가 "/usr/bin/env python3를 요구하는데 RDEPENDS에 없다"고 경고해 주므로, QA 경고를 무시하지 않는 습관이 중요하다.

### 14.4 i2c 도구 하나 넣으려다 전체가 다시 빌드된다

BusyBox bbappend로 i2c 도구를 켰더니 BusyBox뿐 아니라 이미지까지 다시 만들어진다 — 이건 정상이다(BusyBox의 task hash가 바뀌었고 이미지는 BusyBox 패키지에 의존). 하지만 local.conf에서 `DISTRO_FEATURES`를 하나 바꿨더니 **수백 개 레시피**가 다시 빌드된다면, 그 변수가 거의 모든 레시피의 해시에 들어가기 때문이다. 그래서 DISTRO_FEATURES 같은 전역 정책은 프로젝트 초기에 확정하고 자주 바꾸지 않는다. 어떤 변수 때문에 해시가 바뀌었는지는 `bitbake-diffsigs`로 추적한다(K5).

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| local.conf에 `IMAGE_INSTALL += "x"` | 이미지에 x만 들어가 부팅 불가, 또는 x가 조용히 빠짐 | local.conf가 먼저 파싱되어 `?=` 기본값을 막거나, 레시피의 `=`에 덮임 (3.6절에서 재현) | `IMAGE_INSTALL:append = " x"`, 정석은 이미지 레시피·packagegroup |
| `:append = "x"` (앞 공백 없음) | `packagegroup-core-bootx` 같은 이상한 패키지 이름 에러 | `:append`는 공백을 넣지 않는다 | `:append = " x"` |
| 레시피 디렉터리 깊이가 틀림 | "Nothing PROVIDES 'x'" | BBFILES glob `recipes-*/*/*.bb`에 안 맞음 | `recipes-<분류>/<이름>/<이름>_<버전>.bb` |
| 버전 박힌 bbappend (`busybox_1.37.0.bbappend`) | 업그레이드 후 "No recipes available for: ... .bbappend" | 레시피 버전이 바뀌어 짝이 없어짐 | `busybox_%.bbappend`, 또는 의도적으로 버전 고정 후 리뷰 |
| `require` 기반 MACHINE에서 override 상속 누락 | 커널 레시피 skip → "Nothing PROVIDES virtual/kernel" | MACHINEOVERRIDES에 기반 보드 이름이 없음 (7.1절에서 재현) | `MACHINEOVERRIDES =. "<기반 보드>:"` |
| `FILESEXTRAPATHS:prepend = ...` (`:=` 아님) | bbappend의 file:// 파일을 못 찾음 | `${THISDIR}`가 지연 확장되어 엉뚱한 디렉터리 | `FILESEXTRAPATHS:prepend := "${THISDIR}/files:"` |
| `do_install`에서 `/usr/bin`에 직접 설치 | 권한 에러, 또는 호스트 오염 | `${D}` 누락 | 항상 `${D}${bindir}` |
| 파일을 설치했는데 FILES에 없음 | do_package "installed-vs-shipped" QA 에러 | 어떤 패키지도 그 파일을 가져가지 않음 | `FILES:${PN} += "..."` 또는 설치하지 않기 |
| DEPENDS 누락 | configure/compile에서 헤더·라이브러리 없음 | recipe-sysroot에 그 레시피가 안 들어옴 | `DEPENDS += "libfoo"` |
| RDEPENDS 누락 (스크립트·dlopen) | 기기에서 "not found" | 자동 의존성은 ELF 링크만 잡음 | `RDEPENDS:${PN} += "..."`, QA 경고 확인 |
| LAYERSERIES_COMPAT 불일치 | add-layer 거부 (2.3절에서 재현) | 다른 릴리스용 layer | 그 layer의 맞는 브랜치 사용 |
| Ubuntu 24.04+ 호스트 | "User namespaces are not usable by BitBake" (10절에서 재현) | AppArmor userns 제한 | 10.3절의 선택지 |

---

## 16. 면접에서 이렇게 말한다

**Q.** "What is the difference between Yocto, OpenEmbedded, Poky and BitBake?"

**A.** BitBake는 태스크 실행 엔진이고, OpenEmbedded-Core는 그 엔진이 읽는 공통 메타데이터(클래스와 레시피)다. Yocto Project는 이 둘을 포함해 릴리스·문서·테스트를 관리하는 우산 프로젝트이고, Poky는 Yocto가 테스트하는 레퍼런스 DISTRO 이름이다. 제품은 보통 OE-Core 위에 벤더 BSP layer와 자기 layer를 쌓고 DISTRO도 자기 것을 만든다. 참고로 6.0부터는 poky라는 합친 저장소 대신 bitbake, openembedded-core, meta-yocto를 따로 받거나 bitbake-setup을 쓴다.

> BitBake is the task execution engine — think of it as make for metadata. OpenEmbedded-Core is the common metadata it executes: the classes and about a thousand base recipes such as the toolchain, glibc and BusyBox. The Yocto Project is the umbrella that releases and tests all of this, and Poky is its reference distribution configuration. In a product you usually stack the vendor's BSP layer and your own layers on top of OE-Core and define your own DISTRO rather than shipping Poky. Since 6.0 the combined poky repository is gone; you clone bitbake, openembedded-core and meta-yocto separately or use bitbake-setup.

**꼬리 질문**: "현재 LTS는?" → Wrynose 6.0(2030-04까지), 이전 LTS는 Scarthgap 5.0(2028-04까지).

**Q.** "Would you modify a recipe directly or use a bbappend? Why?"

**A.** 남의 layer에 있는 레시피는 bbappend로 고친다. 원본 layer를 그대로 두어야 업스트림 보안 업데이트를 충돌 없이 받을 수 있고, 우리가 바꾼 것이 우리 layer에 모여 리뷰·추적이 된다. 패치 추가, 설정 파일 교체(FILESEXTRAPATHS), 빌드 옵션, do_install 덧붙이기가 전형적이다. 반대로 우리 레시피는 직접 고치고, bbappend가 원본을 절반 넘게 뒤집거나 버전 자체를 바꿀 때는 우리 layer에 새 레시피를 둔다. 그리고 범용으로 쓸 수정이면 업스트림에 보내는 게 장기적으로 가장 싸다.

> If the recipe lives in someone else's layer, I use a bbappend in our layer. That keeps the upstream layer pristine so we can pull security updates without merge conflicts, and every product-specific change lives in one reviewed place. Typical uses are adding a patch, overriding a config file via FILESEXTRAPATHS, tweaking PACKAGECONFIG or appending to do_install. I scope it with an override like :mymachine so adding the layer alone changes nothing. If the bbappend ends up rewriting most of the recipe, or I need a different version, I write our own recipe instead, and if the fix is generic I send it upstream.

**꼬리 질문**: "bbappend가 여러 개면 순서는?" → layer 우선순위 낮은 것부터 적용, 높은 것이 마지막(BBFILES 정렬). 파일 이름 `%` 와일드카드로 버전 무관하게 매칭.

**Q.** "What's the difference between DEPENDS and RDEPENDS?"

**A.** DEPENDS는 빌드 시점, 레시피 사이의 의존이다. A가 B를 DEPENDS하면 B의 populate_sysroot 결과(헤더·라이브러리)가 A의 recipe-sysroot에 들어오고, A의 configure는 그 뒤에 돈다. RDEPENDS는 실행 시점, 패키지 사이의 의존이다. 패키지 메타데이터에 기록되고 이미지 조립 때 같이 설치된다. 공유 라이브러리 링크 의존은 do_package가 자동으로 RDEPENDS에 넣지만, 스크립트 인터프리터나 dlopen, 외부 명령 호출은 직접 써야 한다. 이름 공간도 다르다 — DEPENDS는 레시피 이름이나 virtual/ 이름, RDEPENDS는 패키지 이름이다.

> DEPENDS is build-time and recipe-to-recipe: it makes the other recipe's sysroot output — headers, libraries, native tools — available in my recipe-sysroot before my configure task runs. RDEPENDS is run-time and package-to-package: it goes into the package metadata and pulls the other package into the image. Shared-library dependencies are added to RDEPENDS automatically by the packaging step, but things like a Python interpreter for a script or a binary I exec must be listed by hand. Forgetting DEPENDS fails loudly at compile time thanks to the per-recipe sysroot; forgetting RDEPENDS fails quietly on the device.

**꼬리 질문**: "virtual/kernel은?" → 레시피가 PROVIDES로 제공을 선언하고 `PREFERRED_PROVIDER_virtual/kernel`이 고른다. 런타임 버전은 `VIRTUAL-RUNTIME_*`.

**Q.** "How do you add a package to an image?"

**A.** 제품이라면 이미지 레시피의 IMAGE_INSTALL이나, 여러 이미지가 공유하는 packagegroup의 RDEPENDS에 패키지 이름을 넣는다. ssh 서버나 디버그 도구 같은 정형 기능은 IMAGE_FEATURES로 켠다. 개발 중 잠깐 넣을 때만 local.conf에 `IMAGE_INSTALL:append = " foo"`를 쓰는데, `+=`는 local.conf가 먼저 파싱되는 탓에 이미지 기본 목록을 덮거나 사라지므로 쓰면 안 된다. 그리고 넣는 이름은 레시피가 아니라 패키지 이름이어야 한다.

> For a product I add the package name to IMAGE_INSTALL in our image recipe, or to a packagegroup's RDEPENDS if several images share it. Well-known features like an SSH server or debug tools go through IMAGE_FEATURES. For a quick local experiment I use IMAGE_INSTALL:append with a leading space in local.conf — never += there, because local.conf is parsed before the image recipe, so += either blocks the image's default ?= list or gets overwritten. And it has to be a package name, not necessarily the recipe name; a recipe can produce several packages.

**Q.** "Explain the difference between `=`, `?=`, `:=`, `+=` and `:append`."

**A.** `=`는 대입하되 읽을 때 확장(지연), `:=`는 그 줄에서 바로 확장, `?=`는 아직 값이 없을 때만 대입, `??=`는 파싱이 다 끝나도 아무도 안 정했을 때만. `+=`는 그 줄을 읽는 순간 공백과 함께 붙이고, `:append`는 모든 파싱이 끝난 뒤 공백 없이 붙인다. 그래서 다른 파일이 나중에 `=`로 덮을 수 있는 곳(local.conf, bbappend)에서는 `:append`가 안전하다. 실제 값과 각 연산의 출처는 bitbake-getvar의 history로 확인한다.

> `=` assigns with lazy expansion, `:=` expands immediately, `?=` sets a default only if nothing is set yet, and `??=` is a weak default resolved at the very end of parsing. `+=` appends with a space at the moment that line is parsed, whereas `:append` is applied after all parsing and does not add a space. So in local.conf or a bbappend, where a later plain assignment could clobber you, `:append` is the safe choice. When in doubt I run bitbake-getvar, which shows every operation with file and line.

**Q.** "What is sstate and why are Yocto rebuilds fast?"

**A.** 태스크마다 입력(함수 본문, 참조 변수, 선행 태스크 해시)으로 해시를 계산한다. populate_sysroot나 package_write 같은 태스크의 결과물은 그 해시 이름으로 sstate 캐시에 저장되고, 다음 빌드에서 해시가 같으면 setscene 태스크가 결과만 풀고 fetch부터 install까지를 건너뛴다. 빌드 디렉터리를 지워도, 다른 머신이어도 된다. 그래서 팀은 sstate와 DL_DIR을 공유하거나 미러를 두고, CI는 거의 항상 sstate를 재사용한다.

> Every task gets a signature hash computed from its function body, the variables it references and the hashes of the tasks it depends on. The outputs of key tasks are stored in the shared-state cache under that hash. On the next build, if the hash matches, BitBake runs the setscene variant that just unpacks the cached output and skips fetch through install. This works across clean build directories and across machines, which is why teams share SSTATE_DIR or run an sstate mirror, and why changing a global variable like DISTRO_FEATURES triggers a large rebuild.

**꼬리 질문**: "해시가 왜 바뀌었는지 어떻게 아나?" → `bitbake-diffsigs`(K5).

**Q.** "Why would BitBake fail on a fresh Ubuntu 24.04 machine even with all packages installed?"

**A.** BitBake는 do_fetch를 뺀 태스크를 네트워크가 없는 namespace에서 돌리려고 비특권 user namespace를 만든다. Ubuntu 24.04부터 AppArmor가 unconfined 비특권 프로세스의 user namespace에서 capability를 빼앗아 uid_map 쓰기가 EPERM으로 실패하고, sanity 검사가 "User namespaces are not usable"로 멈춘다. bitbake에 userns를 허용하는 AppArmor 프로파일을 주거나, 전용 빌드 서버면 sysctl로 제한을 끄거나, 제한이 없는 호스트를 쓴다. Ubuntu 26.04 VM에서 실제로 재현해 봤다.

> BitBake runs every task except do_fetch inside a new user and network namespace so builds cannot silently reach the network, which helps reproducibility. Since 24.04, Ubuntu's AppArmor moves unconfined, unprivileged processes that create a user namespace into a restrictive profile that denies capabilities, so writing the uid_map fails with EPERM and BitBake's sanity check stops with "User namespaces are not usable". The fixes are an AppArmor profile that grants userns to bitbake, disabling the sysctl on a dedicated build server, or building on a host without that restriction. I reproduced it on an Ubuntu 26.04 VM.

---

## 17. 직접 해보기

1. **(개념)** OE-Core의 `layer.conf`는 BBFILES에 `.bbappend`를 넣지 않는데 meta-poky는 넣는다. 왜 그런 차이가 있는가? 우리 layer는 어느 쪽이어야 하나?
   정답/힌트: OE-Core는 남을 고치지 않는 바닥 layer라 bbappend가 없다. 제품 layer는 남의 레시피를 덧씌우므로 넣어야 한다.

2. **(변수)** 3.2절 데모 파일에 `K4_I ?= "a"`, `K4_I:append = " b"`, `K4_I ??= "c"`를 넣으면 최종 값은? `bitbake-getvar K4_I`로 확인하고 history의 연산 이름을 읽어라.
   정답/힌트: `?=`가 먼저 값을 정하므로 `??=`는 무시, 그 뒤 `:append` → `"a b"`.

3. **(bbappend 범위 좁히기)** 9절 끝에서 지적한 문제를 고쳐라. busybox bbappend의 `SRC_URI +=`를 `SRC_URI:append:k4-sensorkit = " file://k4-sensorkit.cfg"`로 바꾸고, tinfoil 스크립트(`$P`)로 MACHINE을 k4-sensorkit일 때와 qemux86-64일 때(`bitbake-config-build enable-fragment machine/...`) busybox의 SRC_URI에 `.cfg`가 몇 개인지 비교하라.
   정답/힌트: k4-sensorkit일 때만 append 없는 레시피(8.3절의 12개)보다 하나 많아야 한다. 끝나면 fragment를 `machine/k4-sensorkit`으로 되돌린다.

4. **(레시피)** k4-imu-reader에 Makefile을 추가하고, 레시피에서 `do_compile`을 지운 뒤 `base_do_compile`이 `oe_runmake`를 부르도록 바꿔라. `@func:do_compile`로 확장된 함수 본문이 어떻게 바뀌었는지 비교하라. Makefile은 `$(CC) $(CFLAGS) $(LDFLAGS)`를 써야 한다 — 왜인가?
   정답/힌트: BitBake는 태스크 셸에 CC·CFLAGS·LDFLAGS를 환경 변수로 export하고(`EXTRA_OEMAKE`의 기본값은 빈 문자열), make는 환경 변수가 내장 기본값(`CC = cc`)을 덮게 한다. Makefile이 `gcc`를 하드코딩하면 호스트 컴파일러로 빌드되어 do_package_qa의 "arch" 검사가 잘못된 아키텍처를 잡는다.

5. **(MACHINE)** `k4-sensorkit.conf`에 `KERNEL_DEVICETREE = "..."`, `IMAGE_FSTYPES = "wic"`를 넣었을 때 어떤 변수가 바뀌는지 `bitbake-getvar`로 확인하라. 또 `MACHINE_FEATURES`에 `can`을 넣으면 어떤 레시피들이 영향을 받을지 OE-Core에서 `grep -rn "MACHINE_FEATURES.*can"`으로 찾아보라.
   정답/힌트: IMAGE_FSTYPES는 이미지 태스크 목록(`do_image_wic`)을 바꾼다. 7.4절처럼 이미지 레시피를 파싱해 `@tasks`로 확인.

6. **(관찰)** `bitbake-getvar MACHINEOVERRIDES`의 history에서 `predot` 연산이 어느 파일들에서 왔는지 보고, `arch-arm64.inc`가 왜 `aarch64:`를 붙이는지 설명하라. 그리고 `TUNE_FEATURES`를 history로 추적해 `cortexa57`이 어디서 왔는지 찾아라.
   정답/힌트: `tune-cortexa57.inc` → `arch-armv8a.inc` → `arch-arm64.inc` 체인. TUNE_FEATURES의 `aarch64`가 있으면 `aarch64:` override가 생긴다.

7. **(빌드 머신이 생기면)** 11절 가이드대로 core-image-minimal을 빌드하고, `tmp/work/cortexa57-poky-linux/busybox/1.37.0/temp/run.do_compile.*`를 열어 4.3절에서 tinfoil로 본 `${CC}` 확장과 같은지 비교하라. 그다음 meta-k4-sensorkit를 추가해 `bitbake k4-sensorkit-image`를 돌리고 `runqemu k4-sensorkit nographic slirp` 안에서 `i2cdetect`가 있는지 확인하라.
   정답/힌트: k4-sensorkit은 qemuarm64의 `QB_*` 설정을 상속하므로 runqemu로 부팅된다. 가짜 IIO 장치가 없으므로 k4-imu-reader 서비스는 실패한다 — `systemctl status k4-imu-reader`로 그 실패를 확인하는 것까지가 과제다.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| Yocto Project | 우산 프로젝트 | OE-Core·BitBake·문서·테스트·릴리스를 관리 |
| OpenEmbedded-Core | 공통 메타데이터 layer | 툴체인·libc·기본 레시피·클래스 |
| BitBake | 태스크 실행 엔진 | 메타데이터 파싱 → 태스크 그래프 → 병렬 실행 |
| Poky | 레퍼런스 DISTRO | `meta-poky`의 `poky.conf` |
| bitbake-setup | 저장소·빌드 디렉터리 준비 도구 | 레지스트리 설정(JSON)으로 layer clone과 fragment 적용 |
| layer | `conf/layer.conf`가 있는 디렉터리 | 메타데이터 묶음, 우선순위로 쌓는다 |
| recipe (`.bb`) | 소프트웨어 하나의 빌드 방법 | SRC_URI·태스크·패키지 규칙 |
| bbappend | 다른 layer 레시피에 덧붙이는 파일 | 원본을 고치지 않고 수정 |
| class (`.bbclass`) | 공통 로직 | `inherit`로 상속, EXPORT_FUNCTIONS로 태스크 제공 |
| datastore | 변수 사전 | 설정 파싱 결과(전역) + 레시피별 복사본 |
| override | `VAR:<조건>` | OVERRIDES에 있는 조건 중 오른쪽이 우선 |
| varflag | `VAR[flag]` | 변수·태스크의 속성 (`[network]`, `[depends]`) |
| fragment | 설정 조각 | `bitbake-config-build enable-fragment`, `conf/toolcfg.conf` |
| MACHINE | 하드웨어 정의 | tune·커널·부트로더·콘솔·이미지 형식 |
| DISTRO | OS 정책 | libc·init·DISTRO_FEATURES·패키지 형식 |
| IMAGE | 설치 목록 | `inherit core-image`, IMAGE_INSTALL·IMAGE_FEATURES |
| packagegroup | 패키지 묶음 | 내용 없이 RDEPENDS만 있는 패키지 |
| task | 빌드 단계 | `do_fetch` … `do_rootfs`, `addtask after/before` |
| stamp | 태스크 완료 표시 | `tmp/stamps/...<PV>.do_x.<hash>` |
| task hash (signature) | 태스크 입력의 해시 | 바뀌면 재실행 |
| sstate | shared state cache | 해시 이름으로 태스크 결과 재사용 |
| setscene | sstate 복원 태스크 | `do_x_setscene` |
| sysroot | 레시피 전용 빌드 루트 | `recipe-sysroot`(타깃), `recipe-sysroot-native`(호스트 도구) |
| DEPENDS | 빌드 의존 | 레시피 → 레시피, sysroot 채움 |
| RDEPENDS | 실행 의존 | 패키지 → 패키지, 이미지에 같이 설치 |
| PROVIDES / virtual/ | 가상 이름 제공 | `PREFERRED_PROVIDER_virtual/x`가 고름 |
| WORKDIR / S / B / D | 작업·소스·빌드·설치 디렉터리 | `tmp/work/<arch>/<PN>/<PV>/…` |
| LIC_FILES_CHKSUM | 라이선스 텍스트 md5 | 라이선스 변경 감지 |
| tinfoil | BitBake Python API | bitbake-layers·devtool의 기반, 이 노트의 파싱 도구 |
| HOSTTOOLS | 허용 호스트 명령 목록 | `tmp/hosttools`에 링크, PATH 제한 |
| runqemu | QEMU 실행 스크립트 | `*.qemuboot.conf`를 읽어 부팅 |

---

## 19. 요약 & 체크리스트

Yocto는 BitBake(엔진)가 OpenEmbedded-Core와 여러 layer의 메타데이터(conf · class · recipe)를 읽어 태스크 그래프를 만들고, 레시피마다 fetch → unpack → patch → configure → compile → install → package를 실행해 패키지를 만든 뒤, 이미지 레시피가 그 패키지들로 rootfs를 조립하는 시스템이다. 설정은 MACHINE(보드), DISTRO(정책), IMAGE(설치 목록) 세 축으로 나누고, 남의 레시피는 bbappend로 덧씌운다. 변수 문법에서는 `+=`와 `:append`의 적용 시점 차이, 그리고 OVERRIDES의 오른쪽 우선 규칙이 핵심이다. 이 노트에서는 Wrynose 6.0 LTS(BitBake 2.18)로 설정 파싱과 레시피 파싱을 실제로 돌려 모든 규칙을 확인했고, 빌드 자체는 Ubuntu 24.04+의 AppArmor user namespace 제한 때문에 시작 단계에서 멈춘다는 것까지 재현했다.

- [ ] Yocto Project, OE-Core, BitBake, Poky, bitbake-setup을 한 문장씩 구분해 말할 수 있다
- [ ] 현재 LTS(Wrynose 6.0, 2030-04까지)와 poky 저장소가 5.3부터 갱신되지 않는다는 것을 설명할 수 있다
- [ ] layer.conf의 BBPATH · BBFILES · BBFILE_PRIORITY · LAYERSERIES_COMPAT의 역할을 설명할 수 있다
- [ ] `=` `?=` `??=` `:=` `+=` `:append` `:remove`의 결과를 예측하고 bitbake-getvar history로 검증할 수 있다
- [ ] OVERRIDES 순서로 `VAR:<machine>`이 왜 `VAR:<arch>`를 이기는지 설명할 수 있다
- [ ] SRC_URI · S · B · D · LIC_FILES_CHKSUM · do_compile · do_install · FILES를 갖춘 레시피를 쓸 수 있다
- [ ] 태스크 순서와 stamp · task hash · sstate의 관계를 그림으로 설명할 수 있다
- [ ] DEPENDS와 RDEPENDS의 차이, PROVIDES/virtual/의 역할을 예를 들어 설명할 수 있다
- [ ] 작업 디렉터리에서 `log.do_*` · `run.do_*` · `image/` · `packages-split/`를 찾아 빌드 실패를 추적하는 순서를 말할 수 있다
- [ ] MACHINE · DISTRO · IMAGE를 분리하고, require 기반 MACHINE에서 MACHINEOVERRIDES를 이어받을 수 있다
- [ ] 이미지에 패키지를 넣는 방법들과 local.conf `+=`의 함정을 설명할 수 있다
- [ ] bbappend로 패치·설정 조각을 추가하고 그 범위를 override로 좁힐 수 있다
- [ ] Ubuntu 24.04+에서 BitBake가 멈추는 이유와 해결 선택지를 설명할 수 있다

---

## 참고 자료

- Yocto Project 릴리스 목록과 지원 기간: https://www.yoctoproject.org/development/releases/
- Yocto Project 문서(현재 버전): https://docs.yoctoproject.org/ — 이 노트는 yocto-docs `wrynose` 브랜치 원본을 직접 읽었다: `ref-manual/system-requirements.rst`, `brief-yoctoprojectqs/index.rst`, `dev-manual/poky-manual-setup.rst`, `migration-guides/migration-5.3.rst`(poky 저장소 중단, `S = ${WORKDIR}` 변경), `migration-guides/migration-5.2.rst`(debug-tweaks 제거)
- BitBake 사용자 매뉴얼(변수 문법, override, 태스크): https://docs.yoctoproject.org/bitbake/
- 소스 저장소: https://git.openembedded.org/openembedded-core , https://git.openembedded.org/bitbake , https://git.yoctoproject.org/meta-yocto
- 이 노트에서 인용한 실제 파일(Wrynose): `meta/conf/bitbake.conf`, `meta/conf/layer.conf`, `meta/conf/templates/default/*.sample`, `meta/conf/machine/qemuarm64.conf`, `meta/classes-global/{base,sanity,insane}.bbclass`, `meta/classes-recipe/{core-image,cml1,module}.bbclass`, `meta/recipes-core/busybox/busybox_1.37.0.bb`, `meta/recipes-core/images/core-image-minimal.bb`, `meta-skeleton/recipes-skeleton/hello-single/hello_1.0.bb`, BitBake `lib/bb/{cooker,build,utils}.py`, `bin/bitbake-worker`, `default-registry/configurations/poky-wrynose.conf.json`
- Ubuntu 24.04 LTS 릴리스 노트 — "Unprivileged user namespace restrictions" 절 (BitBake 에러 메시지가 가리키는 문서): https://discourse.ubuntu.com/t/ubuntu-24-04-lts-noble-numbat-release-notes/39890
- Frank Vasquez, Chris Simmonds, "Mastering Embedded Linux Development" (4th ed.) — Yocto 장
- Bootlin, "Yocto Project and OpenEmbedded development" 교육 슬라이드: https://bootlin.com/training/yocto/
- 함께 볼 노트: K3(Buildroot), K5(Yocto 실무 — BSP layer, devtool, 커널 레시피, SDK, sstate 공유, 재현성), K7(라이선스·SBOM), B1–B2(툴체인·sysroot), E2(외부 모듈 빌드)
