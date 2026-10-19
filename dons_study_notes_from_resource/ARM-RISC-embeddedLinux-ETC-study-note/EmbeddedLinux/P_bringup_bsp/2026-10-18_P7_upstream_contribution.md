# P7. upstream 기여 — Documentation/process 규칙대로 IIO 패치를 v1 → 리뷰 → v2까지 돌려 보기

> **이 노트를 다 읽으면**: upstream 기여가 왜 커리어 신호이자 유지보수 비용 절감인지 `1.Intro.rst` 근거로 말할 수 있다 · `submitting-patches.rst`·`2.Process.rst`·`coding-style.rst`·maintainer handbook의 핵심 규칙을 절 이름까지 짚을 수 있다 · `MAINTAINERS`와 `get_maintainer.pl`로 받는 사람을 정하고, 명령형 제목·Signed-off-by·Fixes/Reported-by/Closes/Cc: stable을 바르게 쓸 수 있다 · checkpatch → W=1 + sparse → QEMU 실기 테스트 → `format-patch -v2 --cover-letter` → 리뷰 대응까지 한 사이클을 직접 돌릴 수 있다
> **왜 필요한가**: "upstream에 패치 보내 본 적 있나?"는 Embedded Linux 시니어 면접의 단골 질문이고, 벤더 BSP 위 사내 패치 스택(P2, P5)의 유지보수 비용을 줄이는 유일한 근본 해법이다
> **Don 기준 난이도**: git·C·리뷰 문화는 이미 안다 / 메일 기반 리뷰, 커밋 메시지 관습, 트리 계층(maintainer → linux-next → mainline), 서브시스템별 규칙은 새로 배운다
> **선행 노트**: E7(릴리스 모델·패치 흐름 첫 실습·태그 표·stable 규칙), D7(binding YAML·`dt_binding_check`), M1(`dev_err`·로그), I2(IIO 서브시스템), H5(I2C), H3(레귤레이터), G3(devm·deferred probe), P3(데이터시트 → 드라이버), P6(테스트·CI)

---

## 0. 큰 그림 — 이게 왜 필요한가

E7 5절에서 gpio-zynq 패치로 "커밋 → format-patch → checkpatch → get_maintainer" 흐름을 처음 돌려 봤다. P7은 그 다음 단계다. **도구 사용법이 아니라 "받아들여지는 패치"를 만드는 법** — 커널 문서가 실제로 요구하는 것, 리뷰어가 보는 것, 서브시스템마다 다른 관습, 그리고 첫 기여를 어디서 시작할지를 다룬다. 실습은 Don의 진로(로보틱스 센서)와 바로 이어지는 `drivers/iio/imu/`에서 한다.

### 0.1 왜 upstream인가 — 회사 쪽 이유: 유지보수 비용

`Documentation/process/1.Intro.rst`의 "The importance of getting code into the mainline" 절이 회사 관점의 근거를 정리해 둔다. 요지는 **"out-of-tree로 따로 들고 가는 것은 false economy(가짜 절약)"** 이다.

- 커널 **내부 API는 일부러 안정화하지 않는다** (`stable-api-nonsense.rst`). 그래서 트리 밖 코드는 커널을 올릴 때마다 손봐야 한다 — P5(커널 업그레이드·리베이스)의 고통이 바로 이것이다.
- 트리 안 코드는 **"API를 바꾸는 사람이 깨지는 코드도 같이 고친다"** 는 규칙 덕에 남이 대신 고쳐 준다. 같은 절에 그대로 쓰여 있다.
- 공개 리뷰를 거친 코드가 품질이 높다("Out-of-tree code is lower-quality code").
- 늦게 내면 **누군가 다른 구현을 먼저 merge** 할 수 있고, 그러면 내 코드는 영원히 트리 밖에 남는다.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="24" font-size="13">커널을 올릴 때마다 드는 비용 (개념도 — 측정값 아님)</text>
  <line x1="120" y1="70" x2="660" y2="70" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="120" y1="190" x2="660" y2="190" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="20" y="66" font-size="13">트리 밖</text>
  <text x="20" y="84" font-size="12">(사내 패치)</text>
  <text x="20" y="186" font-size="13">트리 안</text>
  <text x="20" y="204" font-size="12">(upstream)</text>
  <rect x="140" y="40" width="100" height="22" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="190" y="56" text-anchor="middle" font-size="12">6.6: 패치 40개</text>
  <rect x="270" y="40" width="110" height="22" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="325" y="56" text-anchor="middle" font-size="12">6.12: 충돌 해결</text>
  <rect x="410" y="40" width="110" height="22" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="465" y="56" text-anchor="middle" font-size="12">6.18: 리베이스</text>
  <rect x="550" y="40" width="100" height="22" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="600" y="56" text-anchor="middle" font-size="12">7.x: 또 ...</text>
  <text x="140" y="100" font-size="12">API 변경을 우리 팀이 매번 따라간다 → 비용이 버전마다 반복</text>
  <rect x="140" y="160" width="100" height="22" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="190" y="176" text-anchor="middle" font-size="12">한 번 merge</text>
  <rect x="270" y="160" width="110" height="22" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="325" y="176" text-anchor="middle" font-size="12">API 바꾼 사람이</text>
  <rect x="410" y="160" width="110" height="22" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="465" y="176" text-anchor="middle" font-size="12">같이 고쳐 줌</text>
  <rect x="550" y="160" width="100" height="22" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="600" y="176" text-anchor="middle" font-size="12">리뷰 대응만</text>
  <text x="140" y="220" font-size="12">대가: 처음 한 번의 리뷰 비용 + 이후 리뷰 응답</text>
  <text x="140" y="238" font-size="12">(2.Process.rst "Long-term maintenance")</text>
  <text x="20" y="272" font-size="12">근거: Documentation/process/1.Intro.rst "The importance of getting code into the mainline"</text>
</svg>
```

그림 1 — 트리 밖 패치는 커널을 올릴 때마다 같은 비용을 다시 낸다. 트리 안 코드는 한 번의 리뷰 비용으로 그 반복을 없앤다.

### 0.2 왜 upstream인가 — Don 쪽 이유: 커리어 신호

- **검증 가능한 공개 기록**이다. `git log --author`와 lore.kernel.org 메일 아카이브에 영구히 남는다. 이력서의 "Linux 드라이버 경험"을 면접관이 링크 하나로 확인할 수 있다.
- **"남의 코드베이스에서, 남의 규칙대로, 공개 리뷰를 견딘다"** 는 증거다. 로보틱스·국방·자동차 회사는 벤더 BSP를 다루며 upstream과 사내 트리 사이를 오가는 사람을 원한다.
- Don은 SSD 펌웨어 시절 사내 코드 리뷰는 많이 했지만 **공개 기록이 없다**. 커널 패치 몇 개(특히 IIO 센서 드라이버)가 그 공백을 메워 준다.
- `Documentation/process/contribution-maturity-model.rst`(Linux Foundation TAB)는 회사 성숙도를 Level 0~5로 나누고, Level 2부터 "upstream 기여가 승진·평가에 반영된다"고 쓴다. 면접에서 "우리 팀은 upstream을 어떻게 다루나?"를 **역으로 물어볼 근거**가 된다.

Bare-metal 비유: upstream에 넣는 것은 **사내 공용 HAL에 내 수정을 머지하는 것**과 같다. 내 브랜치에 따로 들고 다니면 HAL이 바뀔 때마다 내가 머지 충돌을 푼다. 공용 HAL에 들어가면 HAL을 바꾸는 사람이 내 코드도 같이 고친다.

### 이 노트의 실습 환경

- VM: Lima `elinux` (`uname -r` = `7.0.0-38-generic`, aarch64). git 2.53.0, sparse 설치됨. **smatch, b4, git-send-email은 설치돼 있지 않다** (공용 VM이라 설치하지 않았다 → 해당 부분은 "가이드(실행 안 함)").
- 소스: 랩의 Linux 6.18.55 트리(`/var/lab/src/linux-6.18.55`, git 아님, 읽기 전용).
- 빌드 트리: `/var/lab/out/linux-build-dom` (6.18.55-dom, `BMI160_I2C=m`, `I2C_STUB=m`).
- 게스트: QEMU virt + `Image-dom` + BusyBox initramfs. TCG 에뮬레이션이고 VM을 19명이 공유하는 중이라 **시간 값은 실제 HW 수치가 아니다**.
- **아무것도 보내지 않는다.** 메일 주소는 `Don Example <don@example.com>`, 리뷰어는 가상의 `Reviewer A/B <...@example.org>`다. 실제 사람 이름은 `MAINTAINERS`/`get_maintainer.pl` 출력에만 나온다(공개 파일 내용 그대로).

---

## 1. 규칙의 원천 — Documentation/process 지도

"카더라"가 아니라 **트리 안 문서**가 기준이다. 리뷰어가 "submitting-patches.rst를 읽어라"라고 답장하는 일이 흔하다. 6.18.55 트리에서 직접 확인한 문서와 절:

| 문서 | 무엇을 정하나 | 이 노트에서 쓰는 절 |
|---|---|---|
| `submitting-patches.rst` | 패치 한 개를 보내는 법의 정본 | Describe your changes · Separate your changes · Style-check your changes · Select the recipients · Respond to review comments · Use trimmed interleaved replies · Don't get discouraged - or impatient · Sign your work (DCO 1.1) · Using Reported-by:… and Fixes: · Tagging people requires permission · The canonical patch format (Subject Line, Commentary) · Providing base tree information · Tooling |
| `development-process.rst` → `1.Intro`~`8.Conclusion` | 전체 개발 과정 안내서 | 1.Intro "The importance of getting code into the mainline" · 2.Process "The big picture", "The lifecycle of a patch", "How patches get into the Kernel", "Next trees", "Staging trees" · 5.Posting "When to post", "Before creating patches" · 6.Followthrough "Working with reviewers", "What happens next" |
| `coding-style.rst` | 코드 모양 | 1) Indentation · 2) Breaking long lines and strings · 7) Centralized exiting of functions · 13) Printing kernel messages |
| `submit-checklist.rst` | 보내기 전 점검표 | Check your code with tools · Build your code · Test your code |
| `maintainer-handbooks.rst` | 서브시스템별 규칙 모음 (목차) | netdev, soc, soc-clean-dts, tip, kvm-x86 |
| `stable-kernel-rules.rst` | stable로 가는 조건 | E7 5.11에서 정리함 |
| `Documentation/devicetree/bindings/submitting-patches.rst` | DT binding 패치 규칙 | I. For patch submitters 1)~6) |
| `MAINTAINERS`의 `P:` 항목 | 그 서브시스템의 "maintainer entry profile" | 2.2절 |

### 1.1 핵심 문장 다섯 개 (원문 인용)

`submitting-patches.rst`에서 리뷰에 가장 자주 인용되는 문장들이다. 영어 그대로 외워 두면 면접에서도 쓸 수 있다.

> Describe your problem. … there must be an underlying problem that motivated you to do this work. (Describe your changes)

> Describe your changes in imperative mood, e.g. "make xyzzy do frotz" instead of "[This patch] makes xyzzy do frotz" (Describe your changes)

> Solve only one problem per patch. (Describe your changes)

> Wait for a minimum of one week before resubmitting or pinging reviewers - possibly longer during busy times like merge windows. (Don't get discouraged - or impatient)

> Please put this information **after** the `---` line … It is additional information for the reviewers. (Commentary — 버전별 changelog 위치)

그리고 `6.Followthrough.rst`의 한 문장이 리뷰 문화 전체를 요약한다:

> what will it be like to maintain a kernel with this code in it five or ten years later?

리뷰어의 모든 질문은 결국 이것이다. SSD 펌웨어로 치면 **"이 코드를 3세대 뒤 컨트롤러 팀이 유지보수할 수 있나?"** 와 같은 질문이다.

---

## 2. 누구에게 보내나 — MAINTAINERS와 get_maintainer.pl

### 2.1 MAINTAINERS 항목 읽는 법

`MAINTAINERS` 파일 첫머리가 각 필드의 뜻을 설명한다(직접 확인). 자주 쓰는 것만:

| 필드 | 뜻 | IIO 항목의 값 |
|---|---|---|
| `M:` | 패치를 받을 maintainer | Jonathan Cameron |
| `R:` | 지정 리뷰어 (Cc 대상) | David Lechner, Nuno Sá, Andy Shevchenko |
| `L:` | 메일링 리스트 | linux-iio@vger.kernel.org |
| `S:` | 상태: Supported / Maintained / Odd Fixes / Orphan / Obsolete | Maintained |
| `T:` | maintainer git 트리 — **이 트리를 base로 작업해야 한다** | `git://git.kernel.org/pub/scm/linux/kernel/git/jic23/iio.git` |
| `F:` / `X:` | 담당 파일 패턴 / 제외 패턴 | `drivers/iio/`, `Documentation/devicetree/bindings/iio/` … |
| `N:` / `K:` | 경로 정규식 / 패치 내용 정규식 | — |
| `P:` | 서브시스템 프로필 문서 (그 트리만의 규칙) | IIO 항목에는 없음 |

### 2.2 `P:` — 서브시스템마다 규칙이 다르다

공통 규칙 위에 서브시스템 규칙이 얹힌다. `MAINTAINERS`에서 `P:`를 가진 항목을 세면 netdev, soc, tip(x86), kvm-x86, riscv, xfs, damon, nvdimm, doc-guide, rust 등이 나온다. 대표 예(문서에서 직접 확인):

- **netdev** (`maintainer-netdev.rst` 첫 목록): 제목에 트리를 지정 `[PATCH net]` / `[PATCH net-next]`, 15개 넘는 시리즈 금지, **24시간 안에 재전송 금지**, 지역 변수는 "reverse xmas tree" 순서, merge window 동안 net-next는 닫힘(새 기능 보내지 말 것).
- **tip** (`maintainer-tip.rst`): "Patch subject"(제목 접두어 `x86/apic:` 형식, 대문자 시작, 명령형), "Ordering of commit tags", "Merge window" 절이 따로 있다.
- **IIO**: `P:` 문서가 없다. 그래서 리스트의 최근 패치와 maintainer 답장에서 관습을 배운다(4.4절). **기억 기반 — 리스트에서 확인 필요**: 제목은 `iio: <type>: <driver>: ` 형식(예: `iio: imu: bmi160:`), 80열 선호, maintainer가 다음 merge window용 패치를 받는 마감이 대략 rc6 전후.

Don에게 주는 교훈: **새 서브시스템에 처음 보낼 때는 `git log --oneline -20 -- <디렉터리>`로 제목 모양을, lore에서 최근 리뷰 답장 몇 개를 먼저 읽는다.**

### 2.3 실습 저장소 만들기 — 트리 일부만 git으로

랩 소스는 git이 아니고 읽기 전용이다. checkpatch의 `Fixes:` 검증, `format-patch`, `range-diff`를 쓰려면 git 저장소가 필요하므로, 필요한 부분만 복사해 25 MB짜리 실습 저장소를 만든다. `get_maintainer.pl`은 커널 트리 최상위인지(`COPYING`, `MAINTAINERS`, `Makefile`, `arch/`, `drivers/` … 존재) 확인하므로 최상위 파일과 빈 디렉터리도 만든다.

무엇을 확인하나: 트리 일부만으로 checkpatch·get_maintainer가 동작하는 git 저장소를 만든다.

```sh
#!/bin/bash
# P7: 6.18.55 트리의 일부만 담은 실험용 git 저장소 만들기
set -e
S=/var/lab/src/linux-6.18.55
R=/var/tmp/P7/linux
rm -rf "$R" && mkdir -p "$R" && cd "$R"
cp $S/{COPYING,CREDITS,Kbuild,Kconfig,MAINTAINERS,Makefile,README} .
mkdir -p scripts Documentation arch fs init ipc kernel lib include/linux drivers
cp $S/scripts/{checkpatch.pl,get_maintainer.pl,spelling.txt,const_structs.checkpatch} scripts/
cp -a $S/Documentation/process Documentation/
mkdir -p Documentation/devicetree/bindings
cp -a $S/Documentation/devicetree/bindings/iio Documentation/devicetree/bindings/
cp -a $S/drivers/iio drivers/
cp -a $S/include/linux/iio include/linux/
git init -q -b master
git config user.name "Don Example"
git config user.email "don@example.com"
git config core.abbrev 12
git add -A && git commit -q -m "Linux 6.18.55 (subset: iio + process docs + scripts)"
git tag v6.18.55-subset
git log --oneline; du -sh .; git ls-files | wc -l
```

```text
9b9e164b6110 Linux 6.18.55 (subset: iio + process docs + scripts)
25M	.
1280
```

출력에서 볼 것: `core.abbrev 12` — `submitting-patches.rst`가 "at least the first twelve characters of the SHA-1 ID"를 요구하므로 처음부터 12자로 맞춰 둔다. 실제 작업은 `git clone`한 전체 트리에서, **`T:`에 적힌 maintainer 트리의 최신 브랜치**(IIO라면 iio.git의 `togreg`, 기억 기반)를 base로 한다.

### 2.4 예제 — 파일별 받는 사람

무엇을 확인하나: 파일 경로로 maintainer·리뷰어·리스트를 찾는다. DT binding 파일은 DT maintainer가 추가되는지 본다.

```sh
cd /var/tmp/P7/linux
for f in drivers/iio/imu/bmi160/bmi160_core.c drivers/iio/imu/kmx61.c \
         Documentation/devicetree/bindings/iio/accel/lis302.txt; do
  echo "### $f"; ./scripts/get_maintainer.pl --no-git --no-git-fallback -f $f
done
echo "### --scm --subsystem"
./scripts/get_maintainer.pl --no-git --no-git-fallback --scm --subsystem --no-email -f drivers/iio/imu/bmi160/bmi160_core.c
```

```text
### drivers/iio/imu/bmi160/bmi160_core.c
Jonathan Cameron <jic23@kernel.org> (maintainer:IIO SUBSYSTEM AND DRIVERS)
David Lechner <dlechner@baylibre.com> (reviewer:IIO SUBSYSTEM AND DRIVERS)
"Nuno Sá" <nuno.sa@analog.com> (reviewer:IIO SUBSYSTEM AND DRIVERS)
Andy Shevchenko <andy@kernel.org> (reviewer:IIO SUBSYSTEM AND DRIVERS)
linux-iio@vger.kernel.org (open list:IIO SUBSYSTEM AND DRIVERS)
linux-kernel@vger.kernel.org (open list)
### drivers/iio/imu/kmx61.c
Jonathan Cameron <jic23@kernel.org> (maintainer:IIO SUBSYSTEM AND DRIVERS)
...(위와 같음)
### Documentation/devicetree/bindings/iio/accel/lis302.txt
Jonathan Cameron <jic23@kernel.org> (maintainer:IIO SUBSYSTEM AND DRIVERS)
David Lechner <dlechner@baylibre.com> (reviewer:IIO SUBSYSTEM AND DRIVERS)
"Nuno Sá" <nuno.sa@analog.com> (reviewer:IIO SUBSYSTEM AND DRIVERS)
Andy Shevchenko <andy@kernel.org> (reviewer:IIO SUBSYSTEM AND DRIVERS)
Rob Herring <robh@kernel.org> (maintainer:OPEN FIRMWARE AND FLATTENED DEVICE TREE BINDINGS)
Krzysztof Kozlowski <krzk+dt@kernel.org> (maintainer:OPEN FIRMWARE AND FLATTENED DEVICE TREE BINDINGS)
Conor Dooley <conor+dt@kernel.org> (maintainer:OPEN FIRMWARE AND FLATTENED DEVICE TREE BINDINGS)
linux-iio@vger.kernel.org (open list:IIO SUBSYSTEM AND DRIVERS)
devicetree@vger.kernel.org (open list:OPEN FIRMWARE AND FLATTENED DEVICE TREE BINDINGS)
linux-kernel@vger.kernel.org (open list)
### --scm --subsystem
linux-iio@vger.kernel.org (open list:IIO SUBSYSTEM AND DRIVERS)
linux-kernel@vger.kernel.org (open list)
git git://git.kernel.org/pub/scm/linux/kernel/git/jic23/iio.git
git git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
IIO SUBSYSTEM AND DRIVERS
THE REST
```

출력에서 볼 것:

- bmi160에는 **드라이버 전용 항목이 없다** (bmi270, bmi323에는 있다 — `MAINTAINERS`에서 `BOSCH SENSORTEC BMI270 IMU IIO DRIVER` 확인). 그래서 서브시스템 maintainer에게 바로 간다. kmx61도 마찬가지.
- binding 파일은 IIO + **DT maintainer 3명 + devicetree 리스트**가 붙는다. binding 패치는 두 팀의 리뷰를 받아야 한다(D7).
- `--no-git --no-git-fallback`: 실습 저장소엔 이력이 없어서 끈다. 실제 전체 트리에서는 기본값이 git 이력으로 "최근 이 파일을 고친 사람"도 추가한다. 이 사람들은 **선택적으로** Cc한다 — 무조건 다 넣으면 "unrelated people을 spam하지 말라"(Select the recipients 절)에 걸린다.
- `--scm`이 보여 주는 `T:` 트리가 **base로 삼을 트리**다. mainline(`torvalds/linux.git`) 위에 만든 패치가 iio.git `togreg`에 이미 들어간 변경과 충돌하면 maintainer가 "rebase on iio/togreg"라고 답한다.

---

## 3. 패치의 일생과 릴리스 사이클

### 3.1 author → list → maintainer → linux-next → mainline → stable

`2.Process.rst` "The lifecycle of a patch"는 단계를 Design → Early review → Wider review → Merging into the mainline → Stable release → Long-term maintenance로 나눈다. "How patches get into the Kernel"은 Linus가 직접 고른 패치가 2.6.38에서 9,500여 개 중 112개(약 1.3%)였다고 쓴다 — 나머지는 전부 **chain of trust**(maintainer → 상위 maintainer → Linus의 pull)로 들어간다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="190" height="54" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="52" text-anchor="middle" font-size="13">작성자 (Don)</text>
  <text x="115" y="72" text-anchor="middle" font-size="12">commit -s, format-patch</text>
  <rect x="245" y="30" width="190" height="54" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="52" text-anchor="middle" font-size="13">메일링 리스트</text>
  <text x="340" y="72" text-anchor="middle" font-size="12">linux-iio → lore 보관</text>
  <rect x="470" y="30" width="190" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="52" text-anchor="middle" font-size="13">maintainer 트리</text>
  <text x="565" y="72" text-anchor="middle" font-size="12">jic23/iio.git (togreg)</text>
  <rect x="470" y="170" width="190" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="192" text-anchor="middle" font-size="13">linux-next</text>
  <text x="565" y="212" text-anchor="middle" font-size="12">매일 통합 · 빌드 봇</text>
  <rect x="245" y="170" width="190" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="192" text-anchor="middle" font-size="13">mainline (Linus)</text>
  <text x="340" y="212" text-anchor="middle" font-size="12">merge window에 pull</text>
  <rect x="20" y="170" width="190" height="54" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="115" y="192" text-anchor="middle" font-size="13">stable / LTS</text>
  <text x="115" y="212" text-anchor="middle" font-size="12">Fixes: + Cc: stable</text>
  <line x1="210" y1="50" x2="243" y2="50" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="435" y1="50" x2="468" y2="50" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="245" y1="72" x2="212" y2="72" stroke="#d0564a" stroke-dasharray="4 3" marker-end="url(#ah2)"/>
  <text x="228" y="104" text-anchor="middle" font-size="12">리뷰 → vN+1</text>
  <line x1="565" y1="84" x2="565" y2="168" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="575" y="126" font-size="12">상위 트리</text>
  <text x="575" y="144" font-size="12">경유</text>
  <line x1="470" y1="197" x2="437" y2="197" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="245" y1="197" x2="212" y2="197" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="20" y="254" font-size="12">리뷰 첫 답은 보통 2~3주 안 · togreg의 기능은 다음 merge window에 mainline</text>
  <text x="20" y="274" font-size="12">버그 수정은 fixes 브랜치로 rc 중에도 · stable 팀은 Fixes:/Cc: stable을 보고 가져감</text>
  <text x="20" y="294" font-size="12">상위 트리(IIO는 Greg KH의 char-misc)와 togreg 이름은 기억 기반</text>
  <text x="20" y="314" font-size="12">파랑 = 내 PC · 주황 = 공개 리뷰 · 초록 = 트리 계층 · 빨강 = stable 팀</text>
</svg>
```

그림 2 — IIO 패치의 일생. E7 그림 7이 도구 순서였다면, 이 그림은 **누구의 트리를 어떤 순서로 지나가는지**와 그때의 시간 감각이다.

**linux-next**(`2.Process.rst` "Next trees"): Stephen Rothwell이 모든 서브시스템 트리의 next 브랜치를 매일 합친 트리. "다음 merge window 뒤 mainline이 어떻게 생길지의 스냅샷"이며, 문서는 "merge window에 들어가는 모든 패치는 그 전에 linux-next에 있었어야 한다"고 쓴다. 내 패치가 maintainer 트리에 적용되면 며칠 안에 linux-next에 나타나고, 여기서 빌드 봇(0-day 등)이 다른 아키텍처·설정으로 깨지는지 본다. 봇이 보낸 "build failure" 메일에 **바로 답하는 것**도 작성자의 일이다("What happens next" 절).

### 3.2 릴리스 사이클 — 언제 보내나

E7 1절에서 kernel.org 데이터로 사이클 길이(9~10주)를 직접 쟀다. 여기서는 **기여자 입장의 달력**만 본다.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <line x1="40" y1="60" x2="665" y2="60" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="40" y="24" font-size="13">mainline 한 사이클 (주 단위, 9~10주)</text>
  <rect x="40" y="44" width="120" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="100" y="65" text-anchor="middle" font-size="12">merge window</text>
  <line x1="160" y1="52" x2="160" y2="68" stroke="currentColor"/><text x="160" y="92" text-anchor="middle" font-size="12">rc1</text>
  <line x1="220" y1="52" x2="220" y2="68" stroke="currentColor"/><text x="220" y="92" text-anchor="middle" font-size="12">rc2</text>
  <line x1="280" y1="52" x2="280" y2="68" stroke="currentColor"/><text x="280" y="92" text-anchor="middle" font-size="12">rc3</text>
  <line x1="340" y1="52" x2="340" y2="68" stroke="currentColor"/><text x="340" y="92" text-anchor="middle" font-size="12">rc4</text>
  <line x1="400" y1="52" x2="400" y2="68" stroke="currentColor"/><text x="400" y="92" text-anchor="middle" font-size="12">rc5</text>
  <line x1="460" y1="52" x2="460" y2="68" stroke="currentColor"/><text x="460" y="92" text-anchor="middle" font-size="12">rc6</text>
  <line x1="520" y1="52" x2="520" y2="68" stroke="currentColor"/><text x="520" y="92" text-anchor="middle" font-size="12">rc7</text>
  <line x1="580" y1="48" x2="580" y2="72" stroke="#d0564a" stroke-width="2"/><text x="580" y="92" text-anchor="middle" font-size="12">v7.N</text>
  <text x="20" y="128" font-size="12">mainline</text>
  <text x="70" y="128" font-size="12"></text>
  <rect x="100" y="114" width="60" height="20" rx="3" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="130" y="128" text-anchor="middle" font-size="12">pull</text>
  <rect x="160" y="114" width="420" height="20" rx="3" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="370" y="128" text-anchor="middle" font-size="12">버그 수정만 (fixes 브랜치 → Linus)</text>
  <text x="20" y="168" font-size="12">IIO</text>
  <rect x="40" y="154" width="120" height="20" rx="3" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="100" y="168" text-anchor="middle" font-size="12">리뷰는 계속</text>
  <rect x="160" y="154" width="300" height="20" rx="3" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="310" y="168" text-anchor="middle" font-size="12">다음 릴리스용 새 기능 받음 (기억 기반)</text>
  <rect x="460" y="154" width="120" height="20" rx="3" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="520" y="168" text-anchor="middle" font-size="12">마감 · 그다음으로</text>
  <text x="20" y="208" font-size="12">next</text>
  <rect x="160" y="194" width="420" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="370" y="208" text-anchor="middle" font-size="12">linux-next: maintainer 트리들이 매일 합쳐짐</text>
  <text x="40" y="244" font-size="12">기능 패치는 rc1~rc5 사이에 보내면 다음 merge window에 들어갈 가능성이 높다</text>
  <text x="40" y="264" font-size="12">merge window 중에는 maintainer가 바쁘다 → 1주 넘게 기다리는 게 정상</text>
  <text x="40" y="284" font-size="12">netdev는 merge window 동안 net-next를 닫는다 (maintainer-netdev.rst)</text>
</svg>
```

그림 3 — 기여자 입장의 릴리스 달력. `2.Process.rst` "The big picture": merge window 약 2주 → rc1 → 주 1회 rc → rc6~rc9 무렵 릴리스. "IIO가 언제 마감하나"는 서브시스템 관습이라 기억 기반이다 — maintainer가 리스트에 "I'll be picking up for the next cycle until …"류 공지를 하는지 본다.

예외가 하나 있다. 같은 문서가 "이전에 지원하지 않던 HW용 **새 드라이버**는 다른 코드를 건드리지 않으면 회귀를 일으킬 수 없어서 언제든 넣을 수 있다"고 쓴다(`2.Process.rst` "The big picture"). 그래도 maintainer 트리의 마감은 지켜야 한다.

---

## 4. 실습 — 엉성한 초안 → v1 → 가상 리뷰 → v2

### 4.1 무엇을 고치나

두 가지 작은 변경을 고른다. 둘 다 실제 6.18.55 코드에서 찾았다.

- **bmi160 (Bosch IMU)**: `bmi160_core_probe()`가 `devm_regulator_bulk_get()` 실패를 `dev_err(... "%d")` + `return ret`으로 처리한다. 이 함수는 PMIC 드라이버가 아직 안 올라왔으면 `-EPROBE_DEFER`(-517)를 돌려주는데, 그때도 error 레벨로 "Failed to get regulators: -517"을 찍는다. `dev_err_probe()`는 defer일 때 조용히 이유만 기록한다(6절에서 커널 경로 확인). 로봇 센서 보드에서 흔한 상황이다 — IMU가 PMIC보다 먼저 probe되면 부팅 로그에 무서운 에러가 남는다.
- **kmx61 (Kionix 가속도+자기)**: `checkpatch -f`가 실제로 잡는 들여쓰기 경고가 있다.

```sh
cd /var/lab/src/linux-6.18.55
./scripts/checkpatch.pl --no-tree -q --terse -f drivers/iio/imu/kmx61.c
```

```text
drivers/iio/imu/kmx61.c:305: WARNING: suspect code indent for conditional statements (16, 32)
drivers/iio/imu/kmx61.c:756: WARNING: Prefer using '"%s...", __func__' to using 'kmx61_set_power_state', this function's name, in a string
total: 0 errors, 2 warnings, 1504 lines checked
```

같은 명령을 `bmi160_core.c`, `bmi160_i2c.c`, `bmi160_spi.c`, `fxos8700_core.c`에 돌리면 전부 `0 errors, 0 warnings`였다 — 요즘 IIO 드라이버는 대부분 checkpatch-clean이다. **checkpatch 경고만 고치는 패치는 staging 밖에서는 환영받지 못하는 경우가 많다**(4.8절, 7절). kmx61 건은 리뷰 반응을 보여 주는 재료로 쓴다.

> 정직한 주의: 이 두 변경은 **연습용**이다. 6.18.55는 mainline보다 한참 뒤라 같은 변경이 이미 들어갔거나 다른 이유로 거절될 수 있다. 실제로 보내기 전에는 반드시 maintainer 트리 최신 브랜치와 lore 검색(`https://lore.kernel.org/linux-iio/?q=bmi160`)으로 확인한다.

### 4.2 초안 — 흔한 실수를 다 넣은 한 커밋

무엇을 확인하나: 두 드라이버 변경을 한 커밋에 섞고, 접두어 없는 과거형 제목, 긴 본문, SoB 없음, `if(`, 줄 끝 공백을 넣은 초안을 checkpatch가 어디까지 잡는지 본다.

```sh
#!/bin/bash
# P7: 엉성한 초안 (bmi160 + kmx61을 한 커밋에) → checkpatch
set -e
cd /var/tmp/P7/linux
git checkout -q -B draft v6.18.55-subset
python3 - <<'PY'
p='drivers/iio/imu/bmi160/bmi160_core.c'; s=open(p).read()
old='''	if (ret) {
		dev_err(dev, "Failed to get regulators: %d\\n", ret);
		return ret;
	}
'''
new='''	if(ret)
		return dev_err_probe(dev, ret, "Failed to get regulators: %d\\n", ret);
'''
assert old in s; open(p,'w').write(s.replace(old,new))
p='drivers/iio/imu/kmx61.c'; s=open(p).read()
old='''			kmx61_wake_up_odr_table[i].val2 == val2)
				return kmx61_wake_up_odr_table[i].odr_bits;
'''
new='''		    kmx61_wake_up_odr_table[i].val2 == val2) 
			return kmx61_wake_up_odr_table[i].odr_bits;
'''
assert old in s; open(p,'w').write(s.replace(old,new))
PY
git commit -q -a -F - <<'MSG'
Use dev_err_probe for regulators in bmi160.

I changed the code to use dev_err_probe because it is better and it handles EPROBE_DEFER so the log is not spammed when the regulator driver is not ready yet.
MSG
rm -rf ../draft && git format-patch -q -1 -o ../draft
./scripts/checkpatch.pl ../draft/0001-*.patch; echo "exit=$?"
```

(실제로 돌린 스크립트에는 kmx61의 `__func__` 치환도 들어 있었다 — 위에서는 줄 수 때문에 생략.)

```text
WARNING: Prefer a maximum 75 chars per line (possible unwrapped commit description?)
#6: 
I changed the code to use dev_err_probe because it is better and it handles EPROBE_DEFER so the log is not spammed when the regulator driver is not ready yet.

ERROR: space required before the open parenthesis '('
#24: FILE: drivers/iio/imu/bmi160/bmi160_core.c:846:
+	if(ret)

ERROR: trailing whitespace
#39: FILE: drivers/iio/imu/kmx61.c:306:
+^I^I    kmx61_wake_up_odr_table[i].val2 == val2) $

ERROR: Missing Signed-off-by: line(s)

total: 3 errors, 1 warnings, 32 lines checked

NOTE: For some of the reported defects, checkpatch may be able to
      mechanically convert to the typical style using --fix or --fix-inplace.

NOTE: Whitespace errors detected.
      You may wish to use scripts/cleanpatch or scripts/cleanfile

../draft/0001-Use-dev_err_probe-for-regulators-in-bmi160.patch has style problems, please review.

NOTE: If any of the errors are false positives, please report
      them to the maintainer, see CHECKPATCH in MAINTAINERS.
```

출력에서 볼 것 — checkpatch가 **잡은 것**과 **못 잡은 것**을 나눠 보자.

| 문제 | checkpatch | 근거 문서 |
|---|---|---|
| 본문 75자 초과 | WARNING | The canonical patch format: "line wrapped at 75 columns" |
| `if(ret)` | ERROR | coding-style 3) Placing Braces and Spaces |
| 줄 끝 공백 | ERROR | — |
| Signed-off-by 없음 | ERROR | Sign your work (DCO) |
| **두 드라이버를 한 커밋에** | 못 잡음 | Separate your changes / "Solve only one problem per patch" |
| **제목에 `iio: imu: bmi160:` 접두어 없음, 마침표** | 못 잡음 | Subject Line: `subsystem: summary phrase` |
| **"I changed …", "it is better"** — 1인칭, 근거 없음 | 못 잡음 | Describe your changes: imperative mood, "Describe your problem" |
| `dev_err_probe(..."%d", ret)` — 에러 코드 중복 출력 | 못 잡음 | 리뷰어가 잡는다 (4.5절) |

checkpatch는 **형식 검사기**다. "왜 이 변경이 필요한가"와 "한 패치 한 문제"는 사람이 지킨다.

### 4.3 v1 — 나눠서, 명령형으로, 이유를 쓰고, 서명

커밋 메시지 공식 (`submitting-patches.rst` Describe your changes + Subject Line):

1. 제목: `iio: imu: bmi160: use dev_err_probe() for regulator lookup` — 서브시스템 접두어, 명령형 동사, 마침표 없음, 70~75자 이내, **무엇을 + 왜를 짐작할 수 있게**.
2. 본문 1문단 = **문제**(언제, 누가 보는 증상인가). 2문단 = **해결**(무엇을 바꾸는가). "this patch", "I"를 쓰지 않는다.
3. `Signed-off-by:` — `git commit -s`. DCO 1.1 동의(E7 5.2). 회사에서 하는 일이면 **회사 승인**이 먼저다(`5.Posting.rst` "Before creating patches": "If this work was done for an employer, the employer likely has a right to the work").

무엇을 확인하나: 두 변경을 커밋 두 개로 나누고, 메시지를 고친 v1 시리즈를 만들어 `--strict`로 검사한다.

```sh
#!/bin/bash
# P7: v1 — 변경 1개 = 커밋 1개, 명령형 제목, 문제→해결 본문, SoB (kmx61 커밋은 같은 방식이라 생략)
set -e
cd /var/tmp/P7/linux
git checkout -q -B bmi-v1 v6.18.55-subset
python3 - <<'PY'
p='drivers/iio/imu/bmi160/bmi160_core.c'; s=open(p).read()
old='''	if (ret) {
		dev_err(dev, "Failed to get regulators: %d\\n", ret);
		return ret;
	}
'''
new='''	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulators: %d\\n",
				     ret);
'''
assert old in s; open(p,'w').write(s.replace(old,new))
PY
git commit -q -a -s -F - <<'MSG'
iio: imu: bmi160: use dev_err_probe() for regulator lookup

devm_regulator_bulk_get() returns -EPROBE_DEFER when the supply
regulators are not registered yet, which is common on boards where
the PMIC driver probes after the IMU. Each deferred probe currently
prints "Failed to get regulators" at error level, so a normal boot
can log the same error several times.

Use dev_err_probe(), which stays silent for -EPROBE_DEFER and records
the reason in /sys/kernel/debug/devices_deferred instead.
MSG
# ... (2/2) iio: imu: kmx61: fix indentation and use __func__ in error message
rm -rf ../v1 && git format-patch -q --cover-letter --base=v6.18.55-subset -o ../v1 v6.18.55-subset
ls ../v1
./scripts/checkpatch.pl --strict ../v1/000[12]-*.patch; echo "exit=$?"
```

```text
0000-cover-letter.patch
0001-iio-imu-bmi160-use-dev_err_probe-for-regulator-looku.patch
0002-iio-imu-kmx61-fix-indentation-and-use-__func__-in-er.patch
---------------------------------------------------------------------
../v1/0001-iio-imu-bmi160-use-dev_err_probe-for-regulator-looku.patch
---------------------------------------------------------------------
total: 0 errors, 0 warnings, 0 checks, 13 lines checked

../v1/0001-iio-imu-bmi160-use-dev_err_probe-for-regulator-looku.patch has no obvious style problems and is ready for submission.
---------------------------------------------------------------------
../v1/0002-iio-imu-kmx61-fix-indentation-and-use-__func__-in-er.patch
---------------------------------------------------------------------
total: 0 errors, 0 warnings, 0 checks, 20 lines checked

../v1/0002-iio-imu-kmx61-fix-indentation-and-use-__func__-in-er.patch has no obvious style problems and is ready for submission.
exit=0
```

출력에서 볼 것: "ready for submission"은 **형식이 맞다**는 뜻일 뿐이다. 이 v1에는 아직 리뷰어가 지적할 문제가 둘 남아 있다(다음 절). 줄바꿈은 `coding-style.rst` 2) "align descendants to a function open parenthesis"를 따랐고, 사용자에게 보이는 문자열 `"Failed to get regulators: %d\n"`은 쪼개지 않았다(같은 절: "never break user-visible strings … grep").

### 4.4 가상 리뷰 — 메일 리뷰는 이렇게 생겼다

아래는 **이 노트를 위해 지어낸 리뷰 메일**이다(실행 출력 아님, 실존 인물 아님). 형식은 실제 리스트의 interleaved 답장 그대로다 — 인용(`>`)한 코드 줄 바로 아래에 코멘트를 달고, 관계없는 부분은 잘라낸다.

```
From: Reviewer A <reviewer.a@example.org>
Subject: Re: [PATCH 1/2] iio: imu: bmi160: use dev_err_probe() for regulator lookup

On Sun, 18 Oct 2026 Don Example wrote:
> -	if (ret) {
> -		dev_err(dev, "Failed to get regulators: %d\n", ret);
> -		return ret;
> -	}
> +	if (ret)
> +		return dev_err_probe(dev, ret, "Failed to get regulators: %d\n",
> +				     ret);

dev_err_probe() already prints the error (symbolically, as %pe), so the
": %d" is redundant. Please drop it.

While you are here, the regmap init failures in bmi160_i2c.c and
bmi160_spi.c are also probe paths. A follow-up patch converting those
too would make the driver consistent.
```

```
From: Reviewer B <reviewer.b@example.org>
Subject: Re: [PATCH 2/2] iio: imu: kmx61: fix indentation and use __func__ in error message

This has nothing to do with patch 1, please send it on its own.

> -		dev_err(&data->client->dev,
> -			"Failed: kmx61_set_power_state for %d, ret %d\n",
> -			on, ret);
> +			"Failed: %s for %d, ret %d\n",
> +			__func__, on, ret);

I'd rather not churn this message just to please checkpatch. Drop this
hunk. With that, for the indentation part:

Reviewed-by: Reviewer B <reviewer.b@example.org>
```

리뷰에 답하는 법 (`submitting-patches.rst` Respond to review comments + Use trimmed interleaved replies, `6.Followthrough.rst` Working with reviewers):

- **모든 코멘트에 답한다.** "Agreed, will fix in v2." 또는 반대 이유. 무시하고 v2를 보내는 것이 "fatal mistake"(6.Followthrough).
- 반대할 수 있다. 문서가 "you do not have to agree with every change suggested by reviewers … describe it and justify your solution"이라고 쓴다. 단, 다른 사람도 리뷰어 편을 들면 다시 생각한다.
- 코드를 안 바꾸는 질문이었다면 **주석이나 커밋 메시지에 답을 남긴다**(Andrew Morton의 제안으로 6.Followthrough에 인용됨).
- 감사 인사, 그리고 v2에 리뷰어를 Cc한다("Notify people that commented on your patch about new versions").
- 받은 `Reviewed-by:`는 **그 패치가 크게 바뀌지 않았을 때만** v2에 붙인다. 태그는 본인의 명시적 허락이 필요하다 — 리뷰어가 메일에 직접 쓴 태그만 옮긴다("Tagging people requires permission": Cc:, Reported-by:, Suggested-by:를 뺀 모든 태그).

Don의 답장 예시(지어낸 예):

```
On Mon, 19 Oct 2026 Reviewer A wrote:
> dev_err_probe() already prints the error (symbolically, as %pe), so the
> ": %d" is redundant. Please drop it.

Right, will drop it in v2.

> A follow-up patch converting those too would make the driver consistent.

Will add it as patch 2/2 in v2 with your Suggested-by, thanks.
```

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="50" y="20" width="100" height="28" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="100" y="39" text-anchor="middle" font-size="13">Don</text>
  <rect x="270" y="20" width="140" height="28" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="39" text-anchor="middle" font-size="13">리스트 · 리뷰어</text>
  <rect x="520" y="20" width="120" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="580" y="39" text-anchor="middle" font-size="13">maintainer</text>
  <line x1="100" y1="48" x2="100" y2="405" stroke="#888" stroke-dasharray="3 3"/>
  <line x1="340" y1="48" x2="340" y2="405" stroke="#888" stroke-dasharray="3 3"/>
  <line x1="580" y1="48" x2="580" y2="405" stroke="#888" stroke-dasharray="3 3"/>
  <line x1="100" y1="80" x2="338" y2="80" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="112" y="74" font-size="12">1. v1 시리즈 (패치 2개 + 커버)</text>
  <line x1="340" y1="120" x2="102" y2="120" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="112" y="114" font-size="12">2. A: %d 중복, i2c/spi도</text>
  <line x1="340" y1="160" x2="102" y2="160" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="112" y="154" font-size="12">3. B: kmx61 분리, __func__ 빼라</text>
  <line x1="100" y1="200" x2="338" y2="200" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="112" y="194" font-size="12">4. inline 답장: 고치겠다</text>
  <rect x="20" y="214" width="160" height="26" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="100" y="232" text-anchor="middle" font-size="12">최소 1주 기다림</text>
  <line x1="100" y1="270" x2="338" y2="270" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="112" y="264" font-size="12">5. v2: bmi160 2개 + kmx61 1개</text>
  <line x1="340" y1="310" x2="102" y2="310" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="112" y="304" font-size="12">6. Reviewed-by / Acked-by</text>
  <line x1="340" y1="350" x2="578" y2="350" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="352" y="344" font-size="12">7. b4 am → 트리에 적용</text>
  <line x1="580" y1="390" x2="102" y2="390" stroke="#3f9a6b" marker-end="url(#ah4)"/>
  <text x="352" y="384" font-size="12">8. "Applied, thanks" 메일</text>
</svg>
```

그림 4 — 이 노트의 v1 → v2 사이클. 실제로는 2와 3 사이에 며칠씩 걸리고, 7번 전에 maintainer 본인의 리뷰가 한 번 더 있는 경우가 많다.

### 4.5 v2 — 리뷰 반영, changelog는 `---` 아래

v2에서 한 일:

- 1/2: 메시지에서 `: %d` 제거 → 메시지가 한 줄에 들어가서 줄바꿈도 사라졌다.
- 2/2 (새 패치): i2c/spi glue의 regmap 실패도 `dev_err_probe()`로. `Suggested-by: Reviewer A`.
- kmx61은 시리즈에서 빼서 **독립 `[PATCH v2]`** 로, `__func__` 부분은 제거, 리뷰어 B의 `Reviewed-by` 반영.
- 버전 변경 이력은 `git notes`에 적고 `format-patch --notes`로 **`---` 아래**에 넣는다(Commentary 절: "If it is below the separator line, it gets automatically stripped off when applying the patch"). 커버 레터에도 전체 변경 요약을 넣는다.
- `--base=`로 `base-commit:` 트레일러를 단다(Providing base tree information 절 — CI 봇이 이걸 보고 `git am`한다).

무엇을 확인하나: 리뷰를 반영한 v2 bmi160 시리즈(2 패치 + 커버 레터)를 만든다. kmx61 v2 브랜치는 같은 방식이라 아래 블록에서 생략한다.

```sh
#!/bin/bash
# P7: 가상 리뷰 반영 → v2 (bmi160 2 패치 + 커버 레터). kmx61 단일 패치 v2 부분은 생략
set -e
cd /var/tmp/P7/linux
RA="Reviewer A <reviewer.a@example.org>"
git checkout -q -B bmi-v2 v6.18.55-subset
# 1/2: v1과 같은 python 치환, 단 새 줄은 다음 한 줄 (생략)
#	return dev_err_probe(dev, ret, "Failed to get regulators\n");
git commit -q -a -s -F - <<'MSG'
iio: imu: bmi160: use dev_err_probe() for regulator lookup

devm_regulator_bulk_get() returns -EPROBE_DEFER when the supply
... (v1과 같은 첫 문단)

Use dev_err_probe(), which stays silent for -EPROBE_DEFER and records
the reason in /sys/kernel/debug/devices_deferred instead. It already
prints the error code, so drop it from the message.
MSG
git notes add -m "v2: drop the redundant error code from the message (Reviewer A)" HEAD
# 2/2: bmi160_i2c.c / bmi160_spi.c 의 IS_ERR(regmap) 블록을 정규식으로 치환 (생략)
git commit -q -a -s -F - <<MSG
iio: imu: bmi160: use dev_err_probe() for regmap init failures

Use dev_err_probe() for the regmap initialization error paths in the
I2C and SPI glue drivers, so that all probe-time error messages of the
driver follow the same pattern as the regulator lookup in the core.

No functional change apart from the message format.

Suggested-by: $RA
MSG
git notes add -m "v2: new patch (Reviewer A)" HEAD
rm -rf ../v2 && git format-patch -q -v2 --cover-letter --notes \
    --base=v6.18.55-subset -o ../v2 v6.18.55-subset
python3 /Users/donh/.elinux_scratch/P7/cover.py ../v2/v2-0000-cover-letter.patch  # SUBJECT/BLURB 채우기
ls ../v2 ../v2-kmx61
./scripts/checkpatch.pl --strict --terse ../v2/v2-000[12]-*.patch ../v2-kmx61/*.patch && echo "checkpatch: clean"
```

```text
../v2:
v2-0000-cover-letter.patch
v2-0001-iio-imu-bmi160-use-dev_err_probe-for-regulator-lo.patch
v2-0002-iio-imu-bmi160-use-dev_err_probe-for-regmap-init-.patch

../v2-kmx61:
v2-0001-iio-imu-kmx61-fix-indentation-in-kmx61_convert_wa.patch
checkpatch: clean
```

결과물 — 커버 레터와 1/2 (실제 파일 그대로):

```text
From 2d65748ad55ebebe3ee5c7918bf7d2773a67dcb0 Mon Sep 17 00:00:00 2001
From: Don Example <don@example.com>
Date: Sun, 18 Oct 2026 09:02:31 -0700
Subject: [PATCH v2 0/2] iio: imu: bmi160: quieter probe deferral

The bmi160 driver prints "Failed to get regulators" at error level
on every deferred probe. On boards where the PMIC probes late this shows
up as a scary but harmless error in every boot log.

Patch 1 switches the regulator lookup to dev_err_probe(), patch 2 does
the same for the regmap init paths in the I2C/SPI glue.

Tested on QEMU virt (arm64) with i2c-stub emulating a BMI160 at 0x68:
probe, raw accel/gyro reads and unbind/rebind behave as before.

Changes in v2:
- patch 1: drop the redundant error code from the message (Reviewer A)
- patch 2: new, suggested by Reviewer A
- kmx61 indentation fix split out and sent separately (Reviewer B)

v1: https://lore.kernel.org/r/<message-id-of-v1>

Don Example (2):
  iio: imu: bmi160: use dev_err_probe() for regulator lookup
  iio: imu: bmi160: use dev_err_probe() for regmap init failures

 drivers/iio/imu/bmi160/bmi160_core.c | 6 ++----
 drivers/iio/imu/bmi160/bmi160_i2c.c  | 8 +++-----
 drivers/iio/imu/bmi160/bmi160_spi.c  | 8 +++-----
 3 files changed, 8 insertions(+), 14 deletions(-)


base-commit: 9b9e164b61103cdf86407e412047cd586acca840
-- 
2.53.0
```

```text
Subject: [PATCH v2 1/2] iio: imu: bmi160: use dev_err_probe() for regulator
 lookup

devm_regulator_bulk_get() returns -EPROBE_DEFER when the supply
regulators are not registered yet, which is common on boards where
the PMIC driver probes after the IMU. Each deferred probe currently
prints "Failed to get regulators" at error level, so a normal boot
can log the same error several times.

Use dev_err_probe(), which stays silent for -EPROBE_DEFER and records
the reason in /sys/kernel/debug/devices_deferred instead. It already
prints the error code, so drop it from the message.

Signed-off-by: Don Example <don@example.com>
---

Notes:
    v2: drop the redundant error code from the message (Reviewer A)

 drivers/iio/imu/bmi160/bmi160_core.c | 6 ++----
 1 file changed, 2 insertions(+), 4 deletions(-)

diff --git a/drivers/iio/imu/bmi160/bmi160_core.c b/drivers/iio/imu/bmi160/bmi160_core.c
index 86f6ecfd64aa..ed3509b46fa6 100644
--- a/drivers/iio/imu/bmi160/bmi160_core.c
+++ b/drivers/iio/imu/bmi160/bmi160_core.c
@@ -843,10 +843,8 @@ int bmi160_core_probe(struct device *dev, struct regmap *regmap,
 	ret = devm_regulator_bulk_get(dev,
 				      ARRAY_SIZE(data->supplies),
 				      data->supplies);
-	if (ret) {
-		dev_err(dev, "Failed to get regulators: %d\n", ret);
-		return ret;
-	}
+	if (ret)
+		return dev_err_probe(dev, ret, "Failed to get regulators\n");
 
 	ret = iio_read_mount_matrix(dev, &data->orientation);
 	if (ret)
-- 
2.53.0
```

그리고 단독 kmx61 v2 (헤더 일부 생략):

```text
Subject: [PATCH v2] iio: imu: kmx61: fix indentation in
 kmx61_convert_wake_up_odr_to_bit()

Align the continuation line of the condition with the opening
parenthesis and indent the return statement by one tab, as reported
by checkpatch:

  WARNING: suspect code indent for conditional statements (16, 32)

No functional change.

Reviewed-by: Reviewer B <reviewer.b@example.org>
Signed-off-by: Don Example <don@example.com>
---

Notes:
    v2: split out of the bmi160 series, drop the __func__ change (Reviewer B)
    v2: add Reviewed-by from Reviewer B

 drivers/iio/imu/kmx61.c | 4 ++--
 1 file changed, 2 insertions(+), 2 deletions(-)
...
-			kmx61_wake_up_odr_table[i].val2 == val2)
-				return kmx61_wake_up_odr_table[i].odr_bits;
+		    kmx61_wake_up_odr_table[i].val2 == val2)
+			return kmx61_wake_up_odr_table[i].odr_bits;
...
base-commit: 9b9e164b61103cdf86407e412047cd586acca840
```

출력에서 볼 것:

- `[PATCH v2 0/2]`, `[PATCH v2 1/2]` — `-v2`가 붙여 준다. 단일 패치는 `[PATCH v2]`.
- 커버 레터의 `*** SUBJECT HERE ***`/`*** BLURB HERE ***`를 채웠다. 커버 레터는 **커밋에 안 남는다** → 시리즈 전체의 동기·테스트 방법·버전 이력을 여기 쓴다. 각 패치 본문은 그래도 **스스로 완결**돼야 한다("Don't just say that this is version N", Describe your changes).
- `Notes:` 블록이 `---` 아래에 있다. `git am`으로 적용하면 사라진다(4.7절에서 확인).
- `Suggested-by:` → `Signed-off-by:` 순서. 태그는 시간 순으로 쌓는다 — 남의 태그가 먼저, 내 SoB가 마지막(maintainer-tip.rst "Ordering of commit tags"가 자세히 다룬다).
- 커버 레터의 `Tested on QEMU …` 문장은 4.6절의 실제 테스트 결과다. **안 한 테스트를 쓰면 안 된다.**
- `v1: https://lore.kernel.org/r/<message-id-of-v1>`은 실제로 보낼 때 v1 메일의 Message-ID로 채운다(In-Reply-To로 v1에 엮지 말고 링크로 걸라는 것이 "Explicit In-Reply-To headers" 절의 권고).

### 4.6 보내기 전에 — v1과 v2의 차이를 기계로 확인 (range-diff)

리뷰어에게 "v1과 무엇이 달라졌나"를 정확히 말하려면 `git range-diff`가 가장 정확하다.

```sh
cd /var/tmp/P7/linux && git range-diff v6.18.55-subset bmi-v1 bmi-v2
```

```text
1:  706bd5f7c8e4 ! 1:  58910ad89d99 iio: imu: bmi160: use dev_err_probe() for regulator lookup
    @@ Commit message
         can log the same error several times.
     
         Use dev_err_probe(), which stays silent for -EPROBE_DEFER and records
    -    the reason in /sys/kernel/debug/devices_deferred instead.
    +    the reason in /sys/kernel/debug/devices_deferred instead. It already
    +    prints the error code, so drop it from the message.
     
         Signed-off-by: Don Example <don@example.com>
     
    +
    + ## Notes ##
    +    v2: drop the redundant error code from the message (Reviewer A)
    +
      ## drivers/iio/imu/bmi160/bmi160_core.c ##
     @@ drivers/iio/imu/bmi160/bmi160_core.c: int bmi160_core_probe(struct device *dev, struct regmap *regmap,
      	ret = devm_regulator_bulk_get(dev,
    @@ drivers/iio/imu/bmi160/bmi160_core.c: int bmi160_core_probe(struct device *dev,
     -		return ret;
     -	}
     +	if (ret)
    -+		return dev_err_probe(dev, ret, "Failed to get regulators: %d\n",
    -+				     ret);
    ++		return dev_err_probe(dev, ret, "Failed to get regulators\n");
      
      	ret = iio_read_mount_matrix(dev, &data->orientation);
      	if (ret)
2:  f3fb5018bbf0 < -:  ------------ iio: imu: kmx61: fix indentation and use __func__ in error message
-:  ------------ > 2:  2d65748ad55e iio: imu: bmi160: use dev_err_probe() for regmap init failures
```

출력에서 볼 것: `!`(바뀜), `<`(v1에만 — kmx61이 빠짐), `>`(v2에만 — 새 패치). 이게 커버 레터의 "Changes in v2"와 정확히 맞는지 대조한다. b4는 이걸 자동으로 해 준다(4.10절).

### 4.7 함정 — 커버 레터에 checkpatch를 돌리면

무엇을 확인하나: 습관적으로 `*.patch` 전체에 checkpatch를 돌리면 커버 레터에서 무엇이 나오나.

```sh
cd /var/tmp/P7/linux && ./scripts/checkpatch.pl --terse ../v2/v2-0000-cover-letter.patch; echo "exit=$?"
```

```text
../v2/v2-0000-cover-letter.patch:21: WARNING: Patch version information should be after the --- line
total: 0 errors, 1 warnings, 0 lines checked
exit=1
```

출력에서 볼 것: 커버 레터는 커밋이 되지 않으므로 이 경고는 의미가 없다(false positive). 커버 레터의 "v1: …" 줄을 checkpatch가 커밋 메시지 안의 버전 정보로 오해했다. **checkpatch는 0001부터 돌린다.** 반대로 개별 패치에서 이 경고가 나오면 진짜다 — changelog를 `---` 위에 쓴 것이다.

### 4.8 maintainer 쪽에서 본 적용 — `git am`

무엇을 확인하나: maintainer가 v2 메일을 `git am`으로 적용하면 Notes가 사라지고 결과 트리가 내 브랜치와 같은지.

```sh
cd /var/tmp/P7/linux && git checkout -q -B maint v6.18.55-subset && git am -q -s ../v2/v2-000[12]-*.patch
git log --format="%h %s" v6.18.55-subset..HEAD
git log -1 --format=%B HEAD~1 | tail -4
[ "$(git rev-parse HEAD^{tree})" = "$(git rev-parse bmi-v2^{tree})" ] && echo "tree identical to bmi-v2: yes"
```

```text
5a61b71b96e7 iio: imu: bmi160: use dev_err_probe() for regmap init failures
45cce9e56962 iio: imu: bmi160: use dev_err_probe() for regulator lookup
prints the error code, so drop it from the message.

Signed-off-by: Don Example <don@example.com>

tree identical to bmi-v2: yes
```

출력에서 볼 것: 커밋 메시지는 SoB에서 끝난다 — `Notes:`의 "v2: …"는 남지 않았다. 커밋 해시는 바뀌었지만(커미터·시간이 다르다) **트리는 똑같다**. 실제 maintainer는 여기에 자기 `Signed-off-by:`와 `Link: https://patch.msgid.link/<msgid>`를 더한다(`b4 am`이 자동으로, 기억 기반). 그래서 커밋 해시는 maintainer 트리에 들어간 뒤에야 확정된다 — `Fixes:`에 내 로컬 해시를 쓰면 안 되는 이유다.

---

## 5. 보내기 전에 — 빌드·정적 분석·실기 테스트

`submit-checklist.rst`의 세 절 "Check your code with tools / Build your code / Test your code"가 기준이다. 요약: checkpatch에 남은 경고는 전부 설명할 수 있어야 하고, **sparse로 깨끗해야 하며**, `=y`/`=m`/`=n` 모두와 `allnoconfig`/`allmodconfig`로 빌드되고, 32/64비트·빅/리틀 엔디언에서도 빌드해 보고, lockdep 등 디버그 옵션을 켠 채 모든 코드 경로를 실행해 봐야 한다.

### 5.1 W=1과 sparse가 각각 잡는 것

무엇을 확인하나: 일부러 틀린 작은 모듈로 기본 빌드, `W=1`, `C=1`(sparse)이 잡는 경고가 어떻게 다른지 본다.

```c
// SPDX-License-Identifier: GPL-2.0
/* P7: W=1 과 sparse(C=1)가 각각 무엇을 잡는지 보는 일부러 틀린 모듈 */
#include <linux/module.h>
#include <linux/types.h>
#include <asm/byteorder.h>

/* IMU FIFO에서 읽은 little-endian 샘플 (예: BMI160 accel X) */
static __le16 p7_raw[3] = { cpu_to_le16(0x1234), 0, 0 };

static const int p7_scale = 4;	/* (4) W=1: unused const */

/* (1) 기본 빌드에서도: static 이 빠진 함수 + 프로토타입 없음 → -Wmissing-prototypes */
int p7_decode(int axis)
{
	u16 v = p7_raw[axis];		/* (2) sparse: __le16 → u16, le16_to_cpu() 누락 */
	int last;			/* (3) W=1: set-but-not-used */

	last = v;
	return v;
}

static int __init p7_init(void)
{
	pr_info("p7: x=%d\n", p7_decode(0));
	return 0;
}
module_init(p7_init);
MODULE_DESCRIPTION("P7 sparse/W=1 demo");
MODULE_LICENSE("GPL");
```

```sh
cd /var/tmp/P7/sp   # Kbuild: obj-m += p7_sparse_demo.o
for opt in "" "W=1" "C=1"; do
  echo "### make $opt"
  make -s -C /var/lab/out/linux-build-sub M=$PWD $opt modules 2>&1   # gcc 캐럿 줄은 grep으로 걸러 냄
  touch p7_sparse_demo.c
done
```

```text
### make 
p7_sparse_demo.c:13:5: warning: no previous prototype for ‘p7_decode’ [-Wmissing-prototypes]
### make W=1
p7_sparse_demo.c:13:5: warning: no previous prototype for ‘p7_decode’ [-Wmissing-prototypes]
p7_sparse_demo.c:16:13: warning: variable ‘last’ set but not used [-Wunused-but-set-variable]
p7_sparse_demo.c: At top level:
p7_sparse_demo.c:10:18: warning: ‘p7_scale’ defined but not used [-Wunused-const-variable=]
### make C=1
p7_sparse_demo.c:13:5: warning: no previous prototype for ‘p7_decode’ [-Wmissing-prototypes]
p7_sparse_demo.c:15:23: warning: incorrect type in initializer (different base types)
p7_sparse_demo.c:15:23:    expected unsigned short [usertype] v
p7_sparse_demo.c:15:23:    got restricted __le16
p7_sparse_demo.c:13:5: warning: symbol 'p7_decode' was not declared. Should it be static?
```

출력에서 볼 것:

- `-Wmissing-prototypes`는 6.18에서는 **기본 빌드에서도** 나온다(예전엔 W=1 전용이었는데 기본으로 올라왔다 — 몇 버전에서 바뀌었는지는 기억이 확실치 않다).
- **W=1**만 잡는 것: set-but-unused 변수, 안 쓰는 const. 리뷰어·빌드 봇이 W=1로 빌드하므로 내 패치가 새 W=1 경고를 만들면 "kernel test robot" 메일이 온다.
- **sparse**만 잡는 것: `__le16`(restricted 타입)을 변환 없이 `u16`에 넣은 것. 센서 드라이버에서 가장 흔한 실수다 — BMI160 같은 IMU의 FIFO 데이터는 little-endian이고, ARM64 리틀 엔디언에서는 **동작은 맞아서** 테스트로는 절대 안 잡힌다. 빅 엔디언 SoC(일부 PowerPC·MIPS)에서만 값이 깨진다. 그래서 checklist가 sparse를 요구한다.
- `C=1`은 다시 컴파일되는 파일만, `C=2`는 모든 파일을 sparse로 검사한다.

### 5.2 실제 패치에 W=1 + sparse — 전과 후 비교

무엇을 확인하나: 패치 전(base)과 v2의 bmi160을 랩 `Image-dom` 빌드 트리에 대고 W=1 + sparse로 빌드해 **새 경고가 없는지** 본다. 모듈 이름은 트리 안 모듈과 겹치지 않게 `p7_` 접두어로 바꾼다.

```sh
#!/bin/bash
# P7: 패치 적용 전/후 bmi160을 out-of-tree로 W=1 + sparse(C=1) 빌드해서 새 경고가 없는지 비교
# usage: build.sh <branch> <kbuild tree>
set -e
BR=$1; KB=${2:-/var/lab/out/linux-build-dom}
W=/var/tmp/P7/build-$BR; rm -rf $W; mkdir -p $W
cd /var/tmp/P7/linux
for f in bmi160_core.c bmi160.h bmi160_i2c.c bmi160_spi.c; do
  git show $BR:drivers/iio/imu/bmi160/$f > $W/$f
done
printf 'obj-m += p7_bmi160_core.o p7_bmi160_i2c.o p7_bmi160_spi.o\np7_bmi160_core-y := bmi160_core.o\np7_bmi160_i2c-y := bmi160_i2c.o\np7_bmi160_spi-y := bmi160_spi.o\n' > $W/Kbuild
make -s -C $KB M=$W W=1 C=1 -j2 modules 2>&1 | sed "s|$W/||g"
ls $W/*.ko
```

```sh
for b in v6.18.55-subset bmi-v2; do echo "### build $b (W=1 C=1)"; ./build.sh $b /var/lab/out/linux-build-dom; done
```

```text
### build v6.18.55-subset (W=1 C=1)
/var/tmp/P7/build-v6.18.55-subset/p7_bmi160_core.ko
/var/tmp/P7/build-v6.18.55-subset/p7_bmi160_i2c.ko
/var/tmp/P7/build-v6.18.55-subset/p7_bmi160_spi.ko
### build bmi-v2 (W=1 C=1)
/var/tmp/P7/build-bmi-v2/p7_bmi160_core.ko
/var/tmp/P7/build-bmi-v2/p7_bmi160_i2c.ko
/var/tmp/P7/build-bmi-v2/p7_bmi160_spi.ko
```

출력에서 볼 것: 경고 0개(전과 후 모두). 실제 기여에서는 out-of-tree가 아니라 **트리 안에서** `make W=1 C=1 drivers/iio/imu/bmi160/`처럼 디렉터리 단위로 빌드하고, `CONFIG_BMI160_I2C=y`/`=m`, `CONFIG_BMI160_SPI=n` 조합, 그리고 32비트 ARM 크로스 빌드도 돌린다(submit-checklist "Build your code" 1)~2)). 이 VM에는 32비트 크로스 컴파일러를 설치하지 않았다 — 가이드만.

### 5.3 실기 테스트 — i2c-stub이 흉내 내는 BMI160

진짜 BMI160 보드가 없어도 **probe 경로는 실행해 볼 수 있다**. `i2c-stub`은 레지스터 값을 마음대로 써 넣을 수 있는 가짜 I2C 칩이다(H5·I2). 칩 ID 레지스터 `0x00`에 BMI160 값 `0xD1`, accel X 레지스터 `0x12/0x13`에 `0x1234`를 넣고, base와 v2 드라이버를 차례로 붙여 본다.

무엇을 확인하나: 패치 전후로 probe, raw 읽기, scale 읽기, unbind/bind가 똑같이 동작하는지(회귀 없음).

```sh
#!/bin/sh
# P7 guest test: i2c-stub이 흉내 내는 BMI160(0x68)에 base/v2 드라이버를 차례로 붙여 본다
insmod /i2c-stub.ko chip_addr=0x68
N=$(ls /sys/bus/i2c/devices | sed -n 's/^i2c-//p' | head -1); echo "stub bus = i2c-$N"
i2cset -y $N 0x68 0x00 0xd1; i2cset -y $N 0x68 0x12 0x34; i2cset -y $N 0x68 0x13 0x12
i2cset -y $N 0x68 0x41 0x03   # ACC_RANGE=2g (stub 초기값 0이면 scale 읽기가 EINVAL)
for v in base v2; do
  echo "===== $v"
  insmod /$v/p7_bmi160_core.ko && insmod /$v/p7_bmi160_i2c.ko
  echo bmi160 0x68 > /sys/bus/i2c/devices/i2c-$N/new_device
  D=$(ls -d /sys/bus/iio/devices/iio:device* | head -1)
  echo "name=$(cat $D/name) accel_x_raw=$(cat $D/in_accel_x_raw) scale=$(cat $D/in_accel_scale)"
  echo $N-0068 > /sys/bus/i2c/drivers/bmi160_i2c/unbind; echo "after unbind: $(ls /sys/bus/iio/devices | wc -l) iio devices"
  echo $N-0068 > /sys/bus/i2c/drivers/bmi160_i2c/bind;   echo "after bind:   $(ls /sys/bus/iio/devices | wc -l) iio devices"
  echo 0x68 > /sys/bus/i2c/devices/i2c-$N/delete_device
  rmmod p7_bmi160_i2c p7_bmi160_core
  dmesg | grep -i bmi160 | tail -4
  dmesg -c > /dev/null
done
```

이 스크립트를 BusyBox initramfs(랩 rootfs 복사본 + `i2c-stub.ko` + 두 버전의 `.ko`)에 넣고 `Image-dom`으로 부팅했다(`LAB_CMD=/t.sh`, 3절 README 레시피와 같은 방식).

```sh
timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image-dom -initrd /var/tmp/P7/guest/i.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=/t.sh"
```

```text
lab initramfs: 6.18.55-dom up 5.95s
stub bus = i2c-0
===== base
name=bmi160 accel_x_raw=4660 scale=0.000598
after unbind: 0 iio devices
after bind:   1 iio devices
[    7.604867][   T68] bmi160_i2c 0-0068: supply vdd not found, using dummy regulator
[    7.610416][   T68] bmi160_i2c 0-0068: supply vddio not found, using dummy regulator
[    7.610782][   T68] bmi160_i2c 0-0068: mounting matrix not found: using identity...
[    7.758191][   T68] i2c i2c-0: delete_device: Deleting device bmi160 at 0x68
===== v2
name=bmi160 accel_x_raw=4660 scale=0.000598
after unbind: 0 iio devices
after bind:   1 iio devices
[    8.711506][   T68] bmi160_i2c 0-0068: supply vdd not found, using dummy regulator
[    8.712121][   T68] bmi160_i2c 0-0068: supply vddio not found, using dummy regulator
[    8.712325][   T68] bmi160_i2c 0-0068: mounting matrix not found: using identity...
[    8.882392][   T68] i2c i2c-0: delete_device: Deleting device bmi160 at 0x68
[    9.249128][  T116] reboot: Power down
```

출력에서 볼 것:

- `accel_x_raw=4660` = `0x1234` — 레지스터 두 개를 little-endian으로 읽어 합친 값. `scale=0.000598` = ±2g 범위의 m/s² 단위 LSB(9.80665 × 2 / 32768 ≈ 0.000598).
- base와 v2의 출력이 **완전히 같다**. regulator는 DT가 없어 dummy가 쓰였으므로 이번 테스트는 **바뀐 에러 경로 자체를 지나가지 않는다**. 즉 이 테스트가 증명하는 것은 "정상 경로 회귀 없음"뿐이다. 커버 레터에도 딱 그만큼만 썼다.
- 처음 시도에서는 `0x41`을 안 넣어 `cat in_accel_scale`이 `read error: Invalid argument`였다. stub 레지스터 초기값 0이 범위 표에 없는 값이어서다 — 가짜 HW로 테스트할 때는 **드라이버가 읽는 레지스터를 다 채워야** 한다.

### 5.4 바뀐 경로를 직접 실행 — `dev_err()` vs `dev_err_probe()`

바뀐 에러 경로를 실제로 보려면 regulator가 `-EPROBE_DEFER`를 돌려줘야 한다. QEMU virt에는 I2C 컨트롤러가 없어서 DT로 그 상황을 만들기 어렵다. 대신 **같은 두 코드 패턴을 가진 최소 platform 드라이버**로 커널 동작을 확인한다.

무엇을 확인하나: 같은 실패 값(-EPROBE_DEFER, -ENODEV)을 old 스타일(`dev_err` + `%d`)과 new 스타일(`dev_err_probe`)로 처리했을 때 로그와 `devices_deferred`가 어떻게 다른지.

```c
// SPDX-License-Identifier: GPL-2.0
/* P7: dev_err() 와 dev_err_probe() 가 로그·devices_deferred 에 남기는 것 비교 */
#include <linux/module.h>
#include <linux/platform_device.h>

static int p7_err = -EPROBE_DEFER;
module_param_named(err, p7_err, int, 0444);
static bool p7_old_style;
module_param_named(old_style, p7_old_style, bool, 0444);

static int p7_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = p7_err;	/* devm_regulator_bulk_get() 가 이 값을 돌려줬다고 치자 */

	if (p7_old_style) {
		dev_err(dev, "Failed to get regulators: %d\n", ret);
		return ret;
	}
	return dev_err_probe(dev, ret, "Failed to get regulators\n");
}

static struct platform_driver p7_drv = {
	.probe = p7_probe,
	.driver = { .name = "p7_imu" },
};
static struct platform_device *p7_pdev;

static int __init p7_init(void)
{
	int ret = platform_driver_register(&p7_drv);

	if (ret)
		return ret;
	p7_pdev = platform_device_register_simple("p7_imu", -1, NULL, 0);
	if (IS_ERR(p7_pdev)) {
		platform_driver_unregister(&p7_drv);
		return PTR_ERR(p7_pdev);
	}
	return 0;
}

static void __exit p7_exit(void)
{
	platform_device_unregister(p7_pdev);
	platform_driver_unregister(&p7_drv);
}
module_init(p7_init);
module_exit(p7_exit);
MODULE_DESCRIPTION("P7 dev_err_probe demo");
MODULE_LICENSE("GPL");
```

```sh
#!/bin/sh
# P7 guest test 2: 같은 실패를 old/new 스타일로 — 로그와 devices_deferred
for args in "old_style=1" "" "err=-19 old_style=1" "err=-19"; do
  echo "===== insmod p7_defer.ko $args"
  insmod /p7_defer.ko $args
  insmod /i2c-stub.ko chip_addr=0x50; rmmod i2c-stub   # 장치 추가 이벤트 → deferred probe 재시도 기회
  sleep 1
  dmesg -c | grep p7_imu | sed 's/^\[[^]]*\]\[[^]]*\] //'
  echo "devices_deferred: $(cat /sys/kernel/debug/devices_deferred)"
  rmmod p7_defer
done
```

(모듈은 `make -C /var/lab/out/linux-build-dom M=$PWD W=1 C=1 modules`로 경고 없이 빌드했고, 5.3절과 같은 방식으로 `Image-dom` 게스트에서 `LAB_CMD=/t2.sh`로 실행했다.)

```text
lab initramfs: 6.18.55-dom up 25.18s
===== insmod p7_defer.ko old_style=1
[   28.536495][   T69] p7_imu p7_imu: Failed to get regulators: -517
p7_imu p7_imu: Failed to get regulators: -517
devices_deferred: p7_imu	
===== insmod p7_defer.ko 
devices_deferred: p7_imu	p7_imu: Failed to get regulators
===== insmod p7_defer.ko err=-19 old_style=1
[   36.614108][   T87] p7_imu p7_imu: Failed to get regulators: -19
p7_imu p7_imu: Failed to get regulators: -19
devices_deferred: 
===== insmod p7_defer.ko err=-19
[   42.609032][   T96] p7_imu p7_imu: error -ENODEV: Failed to get regulators
p7_imu p7_imu: error -ENODEV: Failed to get regulators
devices_deferred: 
[   46.554269][  T105] reboot: Power down
```

출력에서 볼 것 (타임스탬프 달린 줄은 콘솔로 바로 나간 같은 메시지다):

- old + defer: **error 레벨로 `-517`** 이 찍히고, `devices_deferred`에는 장치 이름만 있고 **이유가 비어 있다**.
- new + defer: 콘솔·dmesg에 **아무것도 안 찍히고**, `devices_deferred`에 `p7_imu: Failed to get regulators`라는 **이유가 남는다**. "부팅이 끝났는데 IMU가 안 보인다"를 디버그할 때 `cat /sys/kernel/debug/devices_deferred` 한 번으로 원인을 본다(M1의 부팅 디버그와 연결).
- new + 진짜 에러: `error -ENODEV:` — 숫자 대신 **이름**으로 찍힌다(`%pe`). 리뷰어 A가 `: %d`를 빼라고 한 이유다.
- 이번 실행에서 old 스타일 메시지는 한 번만 보였다. 재시도마다 반복 출력되는 장면은 이 짧은 실험에서 재현하지 못했다(재시도는 다른 드라이버의 probe 성공이 계기가 되는데, 여기서는 그런 이벤트가 충분히 없었다). 그래서 v1/v2 커밋 메시지의 "can log the same error several times"는 **실제 보드에서 확인한 뒤에** 보내야 한다 — 확인 못 하면 "may log"로 낮춰 쓴다.

### 5.5 smatch와 dt_binding_check — 가이드(실행 안 함)

- **smatch**: sparse보다 깊은 흐름 분석(널 포인터, 에러 경로에서의 해제 누락, 배열 범위). 리뷰어가 "smatch warns …"라고 답하는 일이 흔하다. 설치되어 있지 않아 실행하지 않았다. 사용법(기억 기반):

```sh
git clone https://repo.or.cz/smatch.git ~/smatch && make -C ~/smatch -j2
cd ~/linux && ~/smatch/smatch_scripts/kchecker drivers/iio/imu/bmi160/bmi160_core.c
```

- **dt_binding_check / dtbs_check**: binding YAML을 고치거나 새로 쓸 때 필수(`Documentation/devicetree/bindings/submitting-patches.rst` 2)). 설치·실행은 D7 3절에서 직접 해 봤다. 변환 패치라면 `make dt_binding_check DT_SCHEMA_FILES=<파일명>.yaml`로 내 파일만 검사하고, 그 compatible을 쓰는 DTS가 있으면 `make dtbs_check`로 기존 DTS가 새 스키마를 통과하는지도 본다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <rect x="190" y="20" width="300" height="38" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="44" text-anchor="middle" font-size="13">실기 테스트 (보드 · QEMU · i2c-stub)</text>
  <rect x="150" y="68" width="380" height="38" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="92" text-anchor="middle" font-size="13">설정 조합 빌드 (=y, =m, =n, allmodconfig, 32비트)</text>
  <rect x="110" y="116" width="460" height="38" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="140" text-anchor="middle" font-size="13">정적 분석: sparse (C=1) · smatch · dt_binding_check</text>
  <rect x="70" y="164" width="540" height="38" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="188" text-anchor="middle" font-size="13">컴파일 경고: W=1 에서 새 경고 0개</text>
  <rect x="30" y="212" width="620" height="38" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="236" text-anchor="middle" font-size="13">형식: checkpatch.pl --strict (남은 경고는 설명 가능해야)</text>
  <text x="30" y="278" font-size="12">아래일수록 싸고 자동이다. 리뷰어는 위쪽(실기 테스트)을 했는지를 커버 레터의 Tested 문장으로 판단한다</text>
</svg>
```

그림 5 — 보내기 전 점검의 층. `submit-checklist.rst`의 세 절을 비용 순으로 쌓은 것이다.

---

## 6. 태그 — Fixes, Reported-by/Closes, Cc: stable을 checkpatch로 확인

E7 5.2절과 5.10절에 태그 표가 있다. 여기서는 E7에서 다루지 않은 **Reported-by → Closes 규칙**과 checkpatch의 실제 반응만 본다.

`submitting-patches.rst` "Using Reported-by:, Tested-by:, Reviewed-by:, Suggested-by: and Fixes:" 절: Reported-by 뒤에는 **보고 링크를 가리키는 `Closes:`가 바로 와야 한다**(웹에 보고가 없으면 예외). "Describe your changes" 절: Fixes:는 sha **12자 이상 + 제목**, 75자 줄바꿈 규칙의 예외라 **한 줄에** 쓴다.

무엇을 확인하나: 태그 형식만 일부러 틀린 커밋을 checkpatch가 어떻게 잡는지(코드 변경은 주석 한 단어 — 형식 검사용 가짜 수정이다).

```sh
#!/bin/bash
# P7: 태그 줄만 검사하는 실험 (코드 변경은 주석 한 단어 — 태그 형식 확인용 가짜 수정)
cd /var/tmp/P7/linux
git checkout -q -B tags v6.18.55-subset
sed -i 's|page 86 of the datasheet|page 86 of the BMI160 datasheet|' drivers/iio/imu/bmi160/bmi160_core.c
git diff --quiet && { echo 'sed did not change anything'; exit 1; }
BASE=$(git rev-parse --short=8 HEAD)
git commit -q -a -s -F - <<MSG
iio: imu: bmi160: clarify datasheet reference

Name the datasheet in the SPI dummy-read comment.

Fixes: $BASE
Reported-by: Tester C <tester.c@example.org>
Cc: stable@vger.kernel.org
MSG
echo "### bad tags"; ./scripts/checkpatch.pl --terse -g HEAD
git commit -q --amend -F - <<MSG
iio: imu: bmi160: clarify datasheet reference

Name the datasheet in the SPI dummy-read comment.

Fixes: $(git log -1 --format='%h ("%s")' --abbrev=12 HEAD~1)
Reported-by: Tester C <tester.c@example.org>
Closes: https://lore.kernel.org/r/<report-message-id>
Cc: stable@vger.kernel.org
Signed-off-by: Don Example <don@example.com>
MSG
echo "### fixed tags"; ./scripts/checkpatch.pl --terse -g HEAD; echo "exit=$?"
git log -1 --format=%B | sed -n '5,9p'
```

```text
### bad tags
828f6c5b18448c42dbce9c2e4ca44bb7365d9296:8: WARNING: Please use correct Fixes: style 'Fixes: <12+ chars of sha1> ("<title line>")' - ie: 'Fixes: 9b9e164b6110 ("Linux 6.18.55 (subset: iio + process docs + scripts)")'
828f6c5b18448c42dbce9c2e4ca44bb7365d9296:9: WARNING: Reported-by: should be immediately followed by Closes: with a URL to the report
total: 0 errors, 2 warnings, 8 lines checked
### fixed tags
exit=0
Fixes: 9b9e164b6110 ("Linux 6.18.55 (subset: iio + process docs + scripts)")
Reported-by: Tester C <tester.c@example.org>
Closes: https://lore.kernel.org/r/<report-message-id>
Cc: stable@vger.kernel.org
Signed-off-by: Don Example <don@example.com>
```

출력에서 볼 것:

- `checkpatch -g <commit>`: 패치 파일 없이 커밋을 바로 검사한다.
- `Fixes:`의 올바른 줄을 checkpatch가 **로컬 git에서 찾아** 만들어 준다. `git log --format='%h ("%s")' --abbrev=12`가 그 줄을 만드는 표준 방법이다(문서가 `[pretty] fixes = Fixes: %h (\"%s\")` git 설정을 권한다).
- **checkpatch가 못 잡은 것**: 주석 한 단어 고친 패치에 `Cc: stable`을 단 것. `stable-kernel-rules.rst` 기준으로 stable 대상이 아니다(사용자를 괴롭히는 실제 버그만). 이런 태그는 stable 팀의 시간을 낭비시킨다. `Fixes:`도 "버그를 들여온 커밋"에만 쓴다 — 오타 수정에 붙이지 않는다.
- 첫 시도에서 sed 패턴이 틀려 커밋이 안 만들어졌고, `checkpatch -g HEAD`가 **1,280개 파일짜리 base 커밋 전체**를 검사하느라 몇 분 동안 끝나지 않았다(파일마다 get_maintainer를 부른다). 그래서 스크립트에 `git diff --quiet` 가드를 넣었다. `-g`에는 항상 내 커밋 범위(`-g v6.18.55-subset..HEAD`)를 준다.

---

## 7. 첫 기여는 어디서 — Don에게 맞는 목표

### 7.1 사다리

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="250" width="270" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="155" y="270" text-anchor="middle" font-size="13">1. 문서 오타 · htmldocs 경고</text>
  <text x="155" y="287" text-anchor="middle" font-size="12">흐름 연습용. 커리어 신호는 약함</text>
  <rect x="110" y="196" width="270" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="245" y="216" text-anchor="middle" font-size="13">2. staging TODO 항목 정리</text>
  <text x="245" y="233" text-anchor="middle" font-size="12">checkpatch 정리가 환영받는 곳</text>
  <rect x="200" y="142" width="270" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="335" y="162" text-anchor="middle" font-size="13">3. DT binding .txt → YAML 변환</text>
  <text x="335" y="179" text-anchor="middle" font-size="12">D7 지식 + 두 팀의 리뷰 경험</text>
  <rect x="290" y="88" width="270" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="425" y="108" text-anchor="middle" font-size="13">4. 내가 쓰는 센서 드라이버 버그</text>
  <text x="425" y="125" text-anchor="middle" font-size="12">Fixes: + 실기 테스트 = 가장 강함</text>
  <rect x="380" y="34" width="280" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="520" y="54" text-anchor="middle" font-size="13">5. 새 장치 ID · 새 IIO 드라이버</text>
  <text x="520" y="71" text-anchor="middle" font-size="12">binding + 드라이버 시리즈, 수 차례 vN</text>
  <line x1="40" y1="236" x2="96" y2="216" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="20" y="24" font-size="12">위로 갈수록 리뷰 부담과 커리어 신호가 함께 커진다</text>
</svg>
```

그림 6 — 첫 기여 사다리. Don은 1~2단계를 흐름 익히기로 짧게 거치고, 3·4단계를 주력으로 삼는 것이 좋다.

### 7.2 staging — checkpatch 정리가 환영받는 유일한 곳

`2.Process.rst` "Staging trees": `drivers/staging/`은 품질 기준에 못 미친 드라이버가 머무는 곳이고, 각 디렉터리의 `TODO`가 남은 일을 적는다. Greg KH가 관리한다. **staging 밖**에서 checkpatch 경고만 고치는 패치는 "churn"(이력만 더럽힌다)이라며 거절되는 일이 많다 — 4.4절 리뷰어 B의 반응이 그 예다.

무엇을 확인하나: `drivers/staging/iio`의 실제 파일들에 `checkpatch --strict -f`가 몇 개를 보고하는지, 그리고 그중 거짓 경보가 있는지.

```sh
cd /var/lab/src/linux-6.18.55
for f in drivers/staging/iio/*/*.c; do printf "%-48s " $f; ./scripts/checkpatch.pl -q --strict -f $f | grep "^total"; done
./scripts/checkpatch.pl -q --strict --terse -f drivers/staging/iio/frequency/ad9832.c | head -4
sed -n 258,259p drivers/staging/iio/frequency/ad9832.c
```

```text
drivers/staging/iio/accel/adis16203.c            total: 0 errors, 1 warnings, 0 checks, 315 lines checked
drivers/staging/iio/adc/ad7816.c                 total: 0 errors, 3 warnings, 0 checks, 449 lines checked
drivers/staging/iio/addac/adt7316-i2c.c          total: 0 errors, 6 warnings, 0 checks, 148 lines checked
drivers/staging/iio/addac/adt7316-spi.c          total: 0 errors, 6 warnings, 0 checks, 154 lines checked
drivers/staging/iio/addac/adt7316.c              total: 0 errors, 0 warnings, 18 checks, 2199 lines checked
drivers/staging/iio/frequency/ad9832.c           total: 9 errors, 2 warnings, 0 checks, 435 lines checked
drivers/staging/iio/frequency/ad9834.c           total: 11 errors, 4 warnings, 0 checks, 508 lines checked
drivers/staging/iio/impedance-analyzer/ad5933.c  total: 0 errors, 0 warnings, 6 checks, 756 lines checked
drivers/staging/iio/frequency/ad9832.c:259: ERROR: Use 4 digit octal (0777) not decimal permissions
drivers/staging/iio/frequency/ad9832.c:260: ERROR: Use 4 digit octal (0777) not decimal permissions
drivers/staging/iio/frequency/ad9832.c:263: ERROR: Use 4 digit octal (0777) not decimal permissions
drivers/staging/iio/frequency/ad9832.c:264: ERROR: Use 4 digit octal (0777) not decimal permissions
static IIO_DEV_ATTR_FREQ(0, 0, 0200, NULL, ad9832_write, AD9832_FREQ0HM);
static IIO_DEV_ATTR_FREQ(0, 1, 0200, NULL, ad9832_write, AD9832_FREQ1HM);
```

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13">drivers/staging/iio — checkpatch --strict -f 보고 수 (6.18.55, 실제 실행)</text>
  <text x="182" y="53" text-anchor="end" font-size="12">accel/adis16203.c</text>
  <rect x="190" y="40" width="22" height="18" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="218" y="53" font-size="12">1</text>
  <text x="182" y="81" text-anchor="end" font-size="12">adc/ad7816.c</text>
  <rect x="190" y="68" width="66" height="18" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="262" y="81" font-size="12">3</text>
  <text x="182" y="109" text-anchor="end" font-size="12">addac/adt7316-i2c.c</text>
  <rect x="190" y="96" width="132" height="18" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="328" y="109" font-size="12">6</text>
  <text x="182" y="137" text-anchor="end" font-size="12">addac/adt7316-spi.c</text>
  <rect x="190" y="124" width="132" height="18" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="328" y="137" font-size="12">6</text>
  <text x="182" y="165" text-anchor="end" font-size="12">addac/adt7316.c</text>
  <rect x="190" y="152" width="396" height="18" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="592" y="165" font-size="12">18</text>
  <text x="182" y="193" text-anchor="end" font-size="12">frequency/ad9832.c</text>
  <rect x="190" y="180" width="198" height="18" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <rect x="388" y="180" width="44" height="18" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="438" y="193" font-size="12">11</text>
  <text x="182" y="221" text-anchor="end" font-size="12">frequency/ad9834.c</text>
  <rect x="190" y="208" width="242" height="18" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <rect x="432" y="208" width="88" height="18" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="526" y="221" font-size="12">15</text>
  <text x="182" y="249" text-anchor="end" font-size="12">impedance/ad5933.c</text>
  <rect x="190" y="236" width="132" height="18" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="328" y="249" font-size="12">6</text>
  <rect x="190" y="276" width="14" height="12" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="210" y="287" font-size="12">ERROR</text>
  <rect x="270" y="276" width="14" height="12" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="290" y="287" font-size="12">WARNING</text>
  <rect x="370" y="276" width="14" height="12" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="390" y="287" font-size="12">CHECK (--strict)</text>
</svg>
```

그림 7 — staging IIO 파일별 checkpatch 보고 수(위 실행 결과로 그림). `impedance/`는 `impedance-analyzer/`를 줄여 쓴 것이다.

출력에서 볼 것:

- 빨간 막대(ERROR)가 가장 많은 ad9832/ad9834의 ERROR는 **거짓 경보**다. `IIO_DEV_ATTR_FREQ(0, 0, 0200, …)`에서 권한은 세 번째 인자 `0200`인데, checkpatch가 매크로 앞쪽 인자 `0`을 "10진 권한"으로 오해했다. 이걸 "고치는" 패치를 보내면 첫 기여부터 신뢰를 잃는다. checkpatch 출력의 마지막 NOTE("If any of the errors are false positives, please report them")가 바로 이런 경우다.
- 경고 수가 많다고 좋은 목표가 아니다. staging에서도 maintainer가 원하는 것은 `TODO`에 적힌 **구조적 정리**다(예: greybus TODO의 "Convert all uses of the old GPIO API from `<linux/gpio.h>` to the GPIO descriptor API").

### 7.3 DT binding 변환 — Don에게 가장 좋은 3단계 목표

6.18.55 트리에는 아직 **텍스트 binding이 814개** 남아 있다(YAML은 5,182개). 디렉터리별 상위는 sound 100, net 64, pinctrl 47, media 42, clock 36, regulator 35 … 이고, IIO에는 `adc/xilinx-xadc.txt`, `accel/lis302.txt`, `mount-matrix.txt` 세 개가 남아 있다(직접 셌다). 2 KB보다 작은 짧은 `.txt`가 350개라서 첫 변환 후보가 많다.

- 제목 형식은 binding 문서가 직접 예를 든다: `dt-bindings: iio: adc: adi,ad7476: Convert to DT schema` (`Documentation/devicetree/bindings/submitting-patches.rst` I. 1)).
- `Documentation`, `doc`, `YAML`, `binding`이라는 단어는 제목에 쓰지 않는다(같은 절).
- 라이선스는 `(GPL-2.0-only OR BSD-2-Clause)` (I. 3)), DT 리스트와 DT maintainer를 Cc (I. 4)).
- 변환 때 **기존 DTS가 쓰는 속성**을 다 담아야 한다 — `dtbs_check`가 기존 보드 DTS에서 새 경고를 내면 리뷰에서 막힌다.
- `lis302.txt`는 119줄에 `st,click-*` 같은 벤더 속성이 많아서 첫 변환으로는 무겁다. 센서에 가까운 짧은 것을 고르는 게 낫다. **누가 이미 작업 중인지 lore에서 먼저 검색**한다 — binding 변환은 인기 있는 첫 기여라 겹치는 일이 많다.

### 7.4 4단계 — 내가 쓰는 센서 드라이버

가장 강한 신호는 **"실제 HW에서 버그를 찾아 고쳤다"** 이다. Don이 로봇 센서 키트를 만지면(Q5) 다음이 자연스러운 소재가 된다.

- IMU 드라이버의 에러 경로 누수(regulator·PM 참조), 엔디언 변환 누락(sparse), 타임스탬프 처리, 범위·scale 표 오류. `Fixes:` + 실기 테스트(`Tested-by` 대상) + 보드 증상(dmesg)을 커밋 메시지에 담을 수 있다.
- 새 칩 변형의 **장치 ID 추가**(호환 칩 ID, `of_match_table`/`i2c_device_id` 항목)는 작으면서도 binding + 드라이버 2패치 시리즈 경험을 준다. binding 패치가 먼저 와야 한다(binding 문서 I. 5)).
- 회사 일이라면: 회사의 오픈소스 승인 → 회사 메일 주소로 SoB(DCO) → 사내 트리에는 upstream 링크를 단 backport(E7 5.11 옵션 3 형식)로 넣는다.

---

## 8. b4 — 시리즈 관리 자동화 (가이드, 실행 안 함)

`submitting-patches.rst` "Tooling" 절이 b4(https://b4.docs.kernel.org/en/latest/)를 소개한다. 이 VM에는 설치돼 있지 않아 실행하지 않았다. 아래는 **기억 기반 흐름**이다 — 버전에 따라 옵션이 다르니 문서로 확인한다.

```sh
# 작성자 쪽 — 4절에서 손으로 한 일을 b4가 대신한다
b4 prep -n bmi160-deferral -f v6.18        # 시리즈 브랜치 + 커버 레터(빈 커밋으로 저장) 생성
git commit -s ...                           # 평소처럼 커밋 쌓기
b4 prep --edit-cover                        # 커버 레터 편집 (Changes in vN 포함)
b4 prep --auto-to-cc                        # get_maintainer.pl 로 To/Cc 채움
b4 prep --check                             # checkpatch 등 점검
b4 send --reflect                           # 자기 자신에게만 보내 보기
b4 send                                     # 전송 (웹 엔드포인트 또는 SMTP), 보낸 뒤 버전 자동 +1
b4 trailers -u                              # 리스트에 달린 Reviewed-by 등을 커밋에 반영
b4 prep --compare-to v1                     # v1 대비 range-diff
# maintainer/리뷰어 쪽
b4 am <message-id>                          # 리스트에서 시리즈 + 태그를 받아 mbox로
b4 shazam <message-id>                      # 받아서 바로 git am
```

b4가 특히 좋은 점: 버전 번호·changelog 링크(`Link to v1:`)·`base-commit`·태그 수집을 사람이 실수할 여지 없이 처리한다. 4.7절에서 본 "커버 레터 v1 링크" 같은 형식은 b4가 만드는 모양이다.

메일 전송 자체(`git send-email`, 메일 클라이언트 설정, `--dry-run`, 자기에게 먼저 보내기)는 E7 5.9절에 정리했다. 다시 강조: **plain text만**, HTML·첨부 금지("No MIME, no links, no compression, no attachments. Just plain text" 절).

---

## 9. 커널 안에서는 무슨 일이 일어나나 — `dev_err_probe()`가 지나는 길

이번 패치가 건드린 함수의 커널 내부 경로를 6.18.55 소스에서 따라가 본다.

- `drivers/base/core.c`: `dev_err_probe()` → `__dev_probe_failed(dev, err, fatal=true, …)`.
  - `err == -EPROBE_DEFER` → `device_set_deferred_probe_reason(dev, &vaf)` + `dev_dbg` (기본 설정에선 안 보임).
  - `err == -ENOMEM` → **아무것도 안 찍는다** (주석: "there's already enough output").
  - 그 밖 → `dev_err(dev, "error %pe: %pV", …)` — 5.4절의 `error -ENODEV:` 형식.
  - 마지막으로 `err`를 그대로 돌려주므로 `return dev_err_probe(...)` 한 줄이 된다.
- `drivers/base/dd.c`: probe가 `-EPROBE_DEFER`를 돌려주면 `driver_deferred_probe_add()`가 장치를 `deferred_probe_pending_list`에 넣는다. 다른 드라이버의 probe가 성공할 때마다 `driver_deferred_probe_trigger()`가 이 목록을 다시 시도한다. debugfs `devices_deferred`(`deferred_devs_show()`)는 이 목록을 돌며 장치 이름과 **기록된 이유**를 출력한다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="230" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="135" y="45" text-anchor="middle" font-size="12">bmi160_core_probe()</text>
  <rect x="20" y="90" width="230" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="135" y="115" text-anchor="middle" font-size="12">dev_err_probe(dev, ret, ...)</text>
  <rect x="20" y="160" width="230" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="135" y="185" text-anchor="middle" font-size="12">__dev_probe_failed()</text>
  <rect x="300" y="100" width="360" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="480" y="125" text-anchor="middle" font-size="12">-EPROBE_DEFER: set_deferred_probe_reason + dev_dbg</text>
  <rect x="300" y="160" width="360" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="480" y="185" text-anchor="middle" font-size="12">-ENOMEM: 출력 없음</text>
  <rect x="300" y="220" width="360" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="480" y="245" text-anchor="middle" font-size="12">그 밖: dev_err("error %pe: ...") → dmesg</text>
  <rect x="20" y="230" width="230" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="135" y="255" text-anchor="middle" font-size="12">deferred_probe_pending_list</text>
  <rect x="20" y="284" width="230" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="135" y="307" text-anchor="middle" font-size="12">debugfs devices_deferred</text>
  <line x1="135" y1="60" x2="135" y2="88" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="135" y1="130" x2="135" y2="158" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="250" y1="175" x2="298" y2="125" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="250" y1="180" x2="298" y2="180" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="250" y1="190" x2="298" y2="240" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="135" y1="200" x2="135" y2="228" stroke="#3f9a6b" marker-end="url(#ah8)"/>
  <text x="145" y="218" font-size="12">probe가 -517 반환</text>
  <line x1="135" y1="270" x2="135" y2="282" stroke="#3f9a6b" marker-end="url(#ah8)"/>
  <text x="300" y="296" font-size="12">다른 드라이버 probe 성공</text>
  <text x="300" y="314" font-size="12">→ driver_deferred_probe_trigger() → 재시도</text>
</svg>
```

그림 8 — `dev_err_probe()`의 분기와 deferred probe 목록. 이유(reason)는 목록의 장치에 붙어서 debugfs로 보인다.

드라이버를 쓸 때 다시 만나는 곳: G2(디바이스 모델·바인딩), G3(platform driver·devm·deferred probe — 이 패치가 깔끔한 이유는 regulator를 devm으로 얻어서 에러 경로에 정리 코드가 없기 때문이다), M1(부팅 로그 디버그에서 `devices_deferred`를 보는 습관), P4(회로도의 전원 트리가 DT `vdd-supply`가 되고, 그 PMIC 드라이버가 늦게 뜨면 defer가 난다).

---

## 10. Embedded 실무에서 다시 보기

### 10.1 "벤더 BSP에 사내 패치 40개" — 무엇을 upstream으로 보낼까

P2·P5의 패치 스택을 분류해 보면 보통 세 종류다.

| 종류 | 예 | upstream? |
|---|---|---|
| 일반 버그 수정 | 센서 드라이버 에러 경로, 엔디언 | **보낸다** — 가장 쉽고 다음 리베이스에서 사라진다 |
| 우리 보드 지원 | 보드 DTS, 새 센서 변형 ID | **보낸다** — DTS도 upstream 가능(`maintainer-soc.rst`) |
| 제품 전용 해킹 | 하드코딩된 타이밍, 디버그 훅 | 보내지 않는다 — 대신 범용화할 수 있는지 리뷰어에게 RFC로 묻는다 |

패치를 보낼 때마다 사내 트리의 그 커밋에 lore 링크를 달아 두면, 다음 리베이스 때 "upstream에 들어갔으니 버리고 backport로 교체"를 기계적으로 할 수 있다.

### 10.2 회사 메일과 DCO

`Signed-off-by:`는 법적 진술이다(DCO 1.1). 회사 일로 쓴 코드는 회사가 권리를 가진다(`5.Posting.rst`). 실무 순서: 사내 오픈소스 정책 확인 → 승인 → 회사 메일 주소로 작성자·SoB → 사내 리뷰(선택) → 리스트. 개인 시간에 개인 HW로 하는 기여도 회사 계약서의 IP 조항을 먼저 확인한다.

### 10.3 면접 이야깃거리 만들기

Don의 현재 상황(이직 준비)에서 현실적인 계획: 손에 있는 IMU(BMI160/BMI270/ICM-42600 계열) 하나를 Raspberry Pi 같은 보드에 붙여 mainline 커널로 돌려 보고, (1) binding 하나 변환, (2) 그 드라이버에서 실제로 본 문제 하나를 고치는 패치. 두 개면 "upstream 경험 있다"고 말할 수 있고, 리뷰 대응 이야기가 생긴다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| mainline 위에서 작업 | "doesn't apply to iio/togreg" 답장 | `T:` 트리 무시 | maintainer 트리 최신 브랜치로 rebase, `--base=` 달기 |
| 무관한 변경을 한 패치·시리즈에 | "please split" | Separate your changes 위반 | 변경 1개 = 패치 1개, 무관하면 다른 시리즈 |
| 제목이 "Fixed bug." | 리뷰 첫 줄에서 지적 | Subject Line 규칙 | `git log --oneline -- <dir>`의 접두어 따라 명령형 |
| changelog를 `---` 위에 | checkpatch "version information" 경고, maintainer가 손으로 지움 | Commentary 절 | `git notes` + `--notes`, 또는 커버 레터 |
| 리뷰를 무시하고 v2 | 반응 없음 | 6.Followthrough "fatal mistake" | 코멘트마다 inline 답장, v2에 리뷰어 Cc |
| 하루 만에 v2, v3 | 리뷰어 짜증, netdev는 규칙 위반 | 성급함 | 최소 1주(netdev 24시간 규칙도), 리뷰가 모이면 |
| 리뷰어가 쓰지 않은 Reviewed-by를 붙임 | 신뢰 상실 | Tagging people requires permission | 메일에 명시된 태그만, 크게 바뀌면 떼기 |
| 사소한 수정에 Cc: stable | stable 팀이 거절 | stable-kernel-rules 미숙지 | 사용자가 겪는 실제 버그에만 |
| checkpatch 거짓 경보를 "고침" | 거절, 코드가 더 나빠짐 | 도구 맹신 | 경고를 이해하고, 틀렸으면 그대로 둔다 |
| 안 한 테스트를 커버 레터에 | 회귀 나면 신뢰 붕괴 | 과장 | 실제로 실행한 범위만 정확히 |

---

## 12. 면접에서 이렇게 말한다

**Q.** Have you contributed to the upstream Linux kernel? Walk me through the process.

**A.** 패치 하나를 예로 든다: MAINTAINERS로 서브시스템과 트리를 찾고, 그 트리를 base로 작업, 명령형 제목과 문제→해결 본문, SoB, checkpatch·W=1·sparse·실기 테스트, `format-patch --cover-letter --base`, get_maintainer로 받는 사람, 리뷰에 inline으로 답하고 changelog를 `---` 아래 넣은 v2. 그다음 maintainer 트리 → linux-next → merge window → mainline.

> I find the owner in MAINTAINERS and base my work on the tree listed under T:. Each patch solves one problem, with an imperative subject like "iio: imu: bmi160: use dev_err_probe() for regulator lookup" and a body that explains the problem first. Before sending I run checkpatch, build with W=1 and sparse, and test on hardware or QEMU. I send with format-patch, a cover letter and base-commit, to the people get_maintainer lists. I answer every review comment inline, and the v2 changelog goes below the "---" line so it is not part of the commit. Once applied, it goes to linux-next and then into mainline in the next merge window.

**꼬리 질문**: "What if the maintainer doesn't respond?" → 최소 1주(merge window면 더) 기다린 뒤 정중한 ping 또는 변경 없는 `[PATCH v1 RESEND]`; 수정했으면 RESEND가 아니라 v2.

**Q.** Why should a company bother upstreaming its drivers?

**A.** 내부 API가 안정적이지 않아서 트리 밖 코드는 커널을 올릴 때마다 비용을 다시 낸다. 트리 안에 넣으면 API 바꾸는 사람이 같이 고치고, 공개 리뷰로 품질이 오르고, 고객은 배포판 커널에서 바로 쓴다. 비용은 리뷰 대응 시간.

> Out-of-tree code is a false economy. The in-kernel API is deliberately unstable, so every kernel upgrade means rebasing our patch stack again. Once code is in mainline, whoever changes an API has to fix the users too, the code gets public review, and customers get it from any distribution kernel. What we pay is review time, once.

**Q.** What goes into a good commit message?

**A.** 제목은 `subsys: component: 명령형 요약`, 75자 이내. 본문은 "무엇이 문제이고 사용자에게 어떻게 보이는가"를 먼저, 그다음 해결. "what"은 diff가 보여 주니 "why"를 쓴다. 태그: Fixes(12자 sha + 제목), Reported-by 뒤 Closes, 필요하면 Cc: stable, 마지막에 SoB.

> Subject: "subsystem: component: imperative summary", under 75 characters. The body explains the problem and its user-visible impact first, then the fix — the diff already shows what changed, so the message explains why. Then tags: Fixes with a 12-character SHA and the original subject, Closes right after Reported-by, Cc stable only for real bugs, and my Signed-off-by last.

**Q.** What does Signed-off-by mean?

**A.** DCO 1.1에 동의한다는 진술: 내가 썼거나 오픈소스 라이선스로 낼 권리가 있다, 그리고 공개 영구 보관에 동의한다. 패치를 거쳐 가는 maintainer도 각자 SoB를 단다. 회사 코드면 회사 승인이 전제다.

> It certifies the Developer's Certificate of Origin: I wrote it or have the right to submit it under the license, and I understand it is public and permanent. Every maintainer who handles the patch adds their own, so the chain is recorded. For work code, the employer has to agree first.

**Q.** A reviewer asks for a change you disagree with. What do you do?

**A.** 먼저 리뷰어가 무엇을 걱정하는지 이해한다("5~10년 유지보수"). 기술적 근거로 inline 답장. 설득이 안 되고 다른 사람도 동의하면 다시 생각한다. 코드를 안 바꾸기로 했으면 그 이유를 주석이나 커밋 메시지에 남긴다.

> I first try to understand the concern — usually long-term maintainability. Then I reply inline with the technical reason for my approach. If others agree with the reviewer, I reconsider. If we keep my version, I add a comment or changelog line, so the next reviewer doesn't have to ask the same question.

**Q.** How do you test a sensor driver patch without the hardware?

**A.** 빌드: W=1, sparse(엔디언 실수), 설정 조합, 32비트 크로스. 실행: QEMU에서 i2c-stub으로 칩 ID·데이터 레지스터를 채워 probe·read·unbind/bind를 돌린다. 단 바뀐 경로를 실제로 지나갔는지 확인하고, 커버 레터에는 실제로 테스트한 범위만 쓴다. 가능하면 HW를 가진 사람에게 Tested-by를 부탁한다.

> Build coverage first: W=1, sparse for endianness bugs, different config combinations, and a 32-bit cross build. For runtime I use QEMU with i2c-stub: I fill in the chip ID and data registers so the real driver probes, and I check reads and unbind/rebind. I write down exactly what that covered — for example, it doesn't exercise a deferral path — and I ask someone with the board for a Tested-by.

**Q.** What is linux-next and why does it matter to you as a contributor?

**A.** 모든 maintainer 트리의 next 브랜치를 매일 합친 통합 트리. 내 패치가 다른 서브시스템 변경과 충돌하거나 다른 아키텍처에서 깨지는지 merge window 전에 드러난다. 봇이 보고하면 바로 대응하는 것이 작성자의 책임.

> linux-next merges every subsystem's next branch daily. It is a preview of the next merge window, so conflicts and build breaks on other architectures appear there first. If a build bot reports a problem with my patch, I fix it quickly.

**Q.** Where would you start contributing as a newcomer?

**A.** 흐름은 문서 오타 하나로 익히고, 실질적인 첫 목표는 DT binding 텍스트→YAML 변환이나 내가 쓰는 센서 드라이버에서 실제로 본 버그 수정. staging 밖에서 checkpatch 경고만 고치는 패치는 피한다.

> I'd learn the mechanics with a small documentation fix, then do real work: converting a text DT binding to schema — there are still about 800 of them — or fixing a bug I actually hit in an IIO driver I use. I'd avoid checkpatch-only cleanups outside staging; maintainers often see them as churn.

---

## 13. 직접 해보기

1. **개념**: `submitting-patches.rst`에서 "RESEND"를 붙여도 되는 경우와 안 되는 경우를 찾아 한 문장으로 써라.
   정답/힌트: "Don't get discouraged - or impatient" 절 — 아무것도 안 바꾼 재전송에만 RESEND, 수정했으면 vN+1.
2. **도구**: 실습 저장소에서 `drivers/iio/imu/st_lsm6dsx/`와 `drivers/iio/imu/inv_mpu6050/`에 `get_maintainer.pl -f`를 돌려 bmi160과 무엇이 다른지 보라.
   정답/힌트: 드라이버 전용 항목(`M:` 벤더 엔지니어)이 추가로 나온다 — 전용 maintainer가 있으면 그 사람의 리뷰가 먼저다.
3. **코드 과제**: 4.5절 v2 시리즈에 v3를 만들어라. 가상 리뷰어 A가 "patch 2의 'No functional change apart from the message format'은 모순이니 지워라"라고 했다고 치고, `git notes`로 v3 이력을 쌓고 `range-diff`로 v2와 비교하라.
   정답/힌트: `git rebase -i` 대신 `git commit --amend`를 해당 커밋에서(이 환경은 interactive 불가 → `git checkout` + `git cherry-pick`으로 재구성), `format-patch -v3 --notes`.
4. **정적 분석**: 5.1절 예제의 `u16 v = p7_raw[axis];`를 sparse가 조용해지도록 고치고, 왜 ARM64에서는 고치기 전에도 값이 맞았는지 설명하라.
   정답/힌트: `le16_to_cpu(p7_raw[axis])`. 리틀 엔디언 CPU에서는 변환이 no-op이라 버그가 숨는다.
5. **관찰**: 5.4절 모듈을 `err=-12`(ENOMEM)로 넣어 보고 dmesg에 무엇이 남는지 예측한 뒤 확인하라.
   정답/힌트: 9절의 분기대로 아무것도 안 찍힌다(메모리 부족은 이미 다른 경고가 나온다는 이유).
6. **binding**: `Documentation/devicetree/bindings`에서 2 KB 미만 `.txt` 중 센서·IIO에 가까운 것을 하나 골라 YAML 초안을 쓰고 D7의 venv로 `dt_binding_check`를 돌려라. 제목 한 줄도 써라.
   정답/힌트: 제목은 `dt-bindings: <dir>: <vendor,chip>: Convert to DT schema`. 보내기 전에 lore에서 같은 파일 검색.
7. **조사**: lore.kernel.org의 linux-iio 아카이브에서 최근 v2 이상 시리즈 하나를 골라 커버 레터의 changelog 형식과 maintainer의 "Applied" 답장을 읽어 보라.
   정답/힌트: b4가 만든 커버 레터에는 "Changes in v2:"와 "Link to v1:"이 있는 경우가 많다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| upstream / mainline | 원류 커널 | Linus의 트리. 벤더·배포판 커널의 출발점 |
| out-of-tree | 트리 밖 코드 | 사내·벤더가 따로 들고 다니는 패치·모듈 |
| maintainer | 담당자 | 서브시스템 트리를 관리하고 패치를 적용하는 사람 (`M:`) |
| chain of trust | 신뢰 사슬 | maintainer → 상위 maintainer → Linus로 이어지는 pull 구조 |
| merge window | 머지 기간 | 릴리스 직후 약 2주, 새 기능이 mainline에 들어가는 기간 |
| linux-next | 통합 미리보기 트리 | 모든 next 브랜치를 매일 합친 트리 |
| DCO | Developer's Certificate of Origin | Signed-off-by가 동의하는 기여 권리 진술 |
| cover letter | 시리즈 소개 메일 | `[PATCH 0/N]`, 커밋에 안 남는 동기·테스트·changelog |
| changelog (patch) | 버전별 변경 이력 | `---` 아래 또는 커버 레터에 |
| interleaved reply | 인라인 답장 | 인용한 줄 바로 아래에 답하는 메일 형식, top-posting 금지 |
| RESEND | 재전송 표시 | 내용 변경 없이 다시 보낼 때만 |
| RFC | 의견 요청 | 머지용이 아니라 방향을 묻는 패치 |
| churn | 의미 없는 변경 | 기능 이득 없이 이력만 늘리는 정리 패치 |
| staging | 수습 드라이버 디렉터리 | `drivers/staging`, TODO를 끝내면 졸업 |
| maintainer entry profile | 서브시스템 규칙 문서 | `MAINTAINERS`의 `P:` |
| b4 | kernel.org 패치 도구 | 시리즈 준비·전송·태그 수집·적용 자동화 |
| sparse | 의미 분석 정적 검사기 | `__le16`·`__user`·`__iomem` 같은 주석 타입 검사, `C=1` |
| smatch | 흐름 분석 정적 검사기 | 에러 경로·널·범위 버그 |
| `dev_err_probe()` | probe 에러 헬퍼 | defer면 조용히 이유 기록, 아니면 `%pe`로 출력 |
| deferred probe | 지연 probe | 의존 자원이 아직 없을 때 `-EPROBE_DEFER`로 나중에 재시도 |

---

## 15. 요약 & 체크리스트

upstream 기여는 "메일로 패치 보내기"가 아니라 **트리 계층과 리뷰 문화에 맞춰 변경을 설득하는 일**이다. 규칙의 원천은 트리 안 `Documentation/process`(특히 `submitting-patches.rst`, `2.Process.rst`, `6.Followthrough.rst`, `submit-checklist.rst`)와 `MAINTAINERS`의 서브시스템 항목이다. 이 노트에서 bmi160 `dev_err_probe()` 변환과 kmx61 들여쓰기로 초안 → checkpatch → v1 → 가상 리뷰 → v2(`--notes`, `--cover-letter`, `--base`, `range-diff`) → W=1/sparse → QEMU i2c-stub 테스트 → `git am`까지 한 바퀴 돌렸다. 도구가 잡는 것은 형식뿐이고, "왜", "한 패치 한 문제", "실제로 테스트한 범위", "태그의 정직함"은 사람이 지킨다. Don의 첫 목표는 DT binding 변환과 실제로 쓰는 IIO 센서 드라이버의 버그 수정이다.

- [ ] 트리 밖 코드가 "false economy"인 이유를 `1.Intro.rst` 근거로 설명할 수 있다
- [ ] `MAINTAINERS`의 M/R/L/S/T/F/P 필드를 읽고 base로 삼을 트리를 고를 수 있다
- [ ] `get_maintainer.pl`로 받는 사람을 정하고, git 이력 기반 추가 Cc를 가려 쓸 수 있다
- [ ] `subsys: component: 명령형` 제목과 문제→해결 본문을 쓸 수 있다
- [ ] Signed-off-by(DCO), Fixes(12자 + 제목), Reported-by + Closes, Cc: stable을 언제 쓰는지 말할 수 있다
- [ ] checkpatch가 잡는 것과 못 잡는 것, 거짓 경보를 구분할 수 있다
- [ ] W=1과 sparse가 잡는 경고의 차이를 예로 들 수 있다
- [ ] `format-patch -v2 --cover-letter --notes --base`로 시리즈를 만들고 changelog를 `---` 아래 둘 수 있다
- [ ] `range-diff`로 버전 간 차이를 확인할 수 있다
- [ ] 리뷰에 inline으로 답하고, 태그 허락 규칙을 지킬 수 있다
- [ ] maintainer 트리 → linux-next → merge window → mainline → stable 흐름과 대략의 시간을 말할 수 있다
- [ ] 첫 기여 후보(binding 변환, staging TODO, 센서 드라이버 버그)를 고르고 lore에서 중복을 확인할 수 있다

## 참고 자료

- 커널 트리 문서(6.18.55에서 직접 읽음): `Documentation/process/submitting-patches.rst`, `development-process.rst`(1.Intro~8.Conclusion), `coding-style.rst`, `submit-checklist.rst`, `maintainer-handbooks.rst`, `maintainer-netdev.rst`, `maintainer-tip.rst`, `stable-kernel-rules.rst`, `stable-api-nonsense.rst`, `contribution-maturity-model.rst`, `email-clients.rst`; `Documentation/devicetree/bindings/submitting-patches.rst`; `MAINTAINERS`
- 온라인판: https://docs.kernel.org/process/submitting-patches.html , https://docs.kernel.org/process/development-process.html , https://docs.kernel.org/process/maintainer-handbooks.html
- b4 문서: https://b4.docs.kernel.org/en/latest/
- 메일 아카이브: https://lore.kernel.org/linux-iio/
- 소스 위치: `drivers/base/core.c` (`dev_err_probe`, `__dev_probe_failed`), `drivers/base/dd.c` (`deferred_probe_pending_list`, `devices_deferred`), `drivers/iio/imu/bmi160/`, `drivers/iio/imu/kmx61.c`, `drivers/staging/iio/` — https://elixir.bootlin.com/linux/v6.18.55/source
- 관련 노트: E7(릴리스 모델·태그 표·stable·send-email), D7(binding·dt_binding_check), G2/G3(probe·devm), I2(IIO), H5(I2C), H3(레귤레이터), M1(로그·deferred probe 디버그), P2/P5(패치 스택·리베이스), P6(테스트·CI)
