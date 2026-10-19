# P5. 커널 업그레이드·리베이스 — 패치 스택을 새 LTS로 옮기고, 깨진 API를 고치고, 회귀를 bisect로 잡는다

> **이 노트를 다 읽으면**: 제품이 옛 커널에 갇히는 이유와 LTS·CIP SLTS 수명을 근거로 "언제 올릴지"를 말할 수 있다 · 패치 스택을 인벤토리하고 upstreamed / obsolete / needs-port / keep으로 분류한 뒤 `git cherry`, `git rebase --onto`, zdiff3 충돌 해결, `git range-diff`로 새 base에 옮길 수 있다 · 6.1 시절 스타일 드라이버가 6.18에서 왜 11개 에러를 내는지 읽고 포팅해서 실제 게스트에 올릴 수 있다 · `git bisect run`으로 회귀 커밋을 자동으로 찾고, stable 릴리스의 `Fixes:`/`Cc: stable` 태그로 "우리에게 필요한 수정"을 골라낼 수 있다
> **왜 필요한가**: "벤더 BSP 커널 5.15를 6.x LTS로 올려라"(contents.md 시나리오)는 장수 제품을 만드는 회사(국방·차량·산업 로봇)에서 1~2년마다 반드시 돌아오는 프로젝트이고, 면접에서는 "커널 업그레이드를 어떻게 계획하고 회귀를 어떻게 막나?"로 나온다
> **Don 기준 난이도**: SSD 펌웨어를 새 컨트롤러 세대·새 벤더 SDK로 이식하던 경험(공통 코드 vs 플랫폼 코드 분리, 회귀 테스트 매트릭스)은 그대로 쓴다 / 새로 배울 부분은 git의 스택 이동 도구(`--onto`, patch-id, range-diff, bisect run), 커널 내부 API가 "안정성을 약속하지 않는다"는 문화, stable 트리의 태그 체계
> **선행 노트**: E7(릴리스 모델·LTS·CIP·패치 보내기 — 이 노트는 E7 §2·§4를 전제로 한다), P2(BSP 구조·패치 스택 관리 기초), E3(모듈 빌드), G3(platform driver 수명주기), H3(I2C 드라이버) — 테스트 자동화는 P6, upstream으로 보내 스택 줄이기는 P7

---

## 0. 큰 그림 — 이게 왜 필요한가

제품 커널은 "upstream LTS + 벤더 BSP 패치 + 우리 패치"의 3층 케이크다(P2). 업그레이드란 이 케이크의 **맨 아래층을 바꾸고 위 두 층을 다시 올리는 일**이다. 아래층이 바뀌면 위층 패치 중 일부는 이미 아래층에 들어가 있고(upstreamed), 일부는 대상이 사라졌고(obsolete), 일부는 문맥이나 API가 바뀌어 손으로 고쳐야 하며(needs port), 나머지는 그대로 올라간다(keep). 이 분류를 먼저 하고, 옮기고, 고치고, 회귀를 확인하는 것이 업그레이드 프로젝트 전체다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="200" height="72" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="56" text-anchor="middle" font-size="14">1. 인벤토리</text>
  <text x="120" y="78" text-anchor="middle" font-size="12">git log old..product</text>
  <text x="120" y="94" text-anchor="middle" font-size="12">git cherry · 파일 상태</text>
  <rect x="240" y="30" width="200" height="72" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="56" text-anchor="middle" font-size="14">2. 분류</text>
  <text x="340" y="78" text-anchor="middle" font-size="12">upstreamed · obsolete</text>
  <text x="340" y="94" text-anchor="middle" font-size="12">needs port · keep</text>
  <rect x="460" y="30" width="200" height="72" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="560" y="56" text-anchor="middle" font-size="14">3. 옮기기</text>
  <text x="560" y="78" text-anchor="middle" font-size="12">git rebase --onto</text>
  <text x="560" y="94" text-anchor="middle" font-size="12">(drop 목록 반영)</text>
  <rect x="20" y="148" width="200" height="72" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="120" y="174" text-anchor="middle" font-size="14">4. 충돌 · API 포팅</text>
  <text x="120" y="196" text-anchor="middle" font-size="12">zdiff3 · range-diff</text>
  <text x="120" y="212" text-anchor="middle" font-size="12">컴파일 에러 = 할 일 목록</text>
  <rect x="240" y="148" width="200" height="72" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="174" text-anchor="middle" font-size="14">5. 회귀 테스트</text>
  <text x="340" y="196" text-anchor="middle" font-size="12">빌드 · 부팅 · 모듈 로드</text>
  <text x="340" y="212" text-anchor="middle" font-size="12">KUnit · kselftest · 성능</text>
  <rect x="460" y="148" width="200" height="72" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="174" text-anchor="middle" font-size="14">6. 유지</text>
  <text x="560" y="196" text-anchor="middle" font-size="12">bisect run · stable 머지</text>
  <text x="560" y="212" text-anchor="middle" font-size="12">Fixes: · Cc: stable</text>
  <line x1="220" y1="66" x2="238" y2="66" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="440" y1="66" x2="458" y2="66" stroke="currentColor" marker-end="url(#ah1)"/>
  <path d="M560 102 L560 124 L120 124 L120 146" fill="none" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="220" y1="184" x2="238" y2="184" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="440" y1="184" x2="458" y2="184" stroke="currentColor" marker-end="url(#ah1)"/>
  <path d="M560 220 L560 240 L8 240 L8 66 L18 66" fill="none" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
  <text x="300" y="236" text-anchor="middle" font-size="12">다음 LTS 때 같은 순서로 반복 (스택이 작을수록 싸다)</text>
</svg>
```

그림 1 — 커널 업그레이드 파이프라인. 1~3은 git 작업, 4는 사람이 판단하는 작업, 5~6은 자동화할수록 다음 업그레이드가 싸진다.

bare-metal 비유로 보면 이렇다. SSD 펌웨어에서 새 컨트롤러 세대로 넘어갈 때 "칩 벤더 HAL/SDK v2 → v3 + 우리 FTL 코드"를 옮기던 일과 같다. SDK가 함수 이름과 인자를 바꾸면 컴파일 에러가 할 일 목록이 되고, 우리가 SDK에 넣어 둔 핫픽스 중 일부는 v3에 이미 들어가 있고, 일부 기능은 v3에서 없어졌다. 차이는 두 가지다.

- 리눅스 커널은 **내부 API 안정성을 약속하지 않는다**(`Documentation/process/stable-api-nonsense.rst`). 유저 공간 ABI(syscall, sysfs)는 지키지만, 드라이버가 쓰는 커널 내부 함수는 매 릴리스 바뀐다. 그래서 "in-tree에 들어간 드라이버는 API를 바꾸는 사람이 같이 고쳐 주고, out-of-tree 드라이버는 우리가 고친다".
- 위층이 git 커밋 단위로 관리되므로 **git이 분류의 절반을 자동으로 해 준다**(patch-id 비교, 3-way merge). 나머지 절반은 사람이 의미를 판단한다.

### 이 노트의 실습 환경

- Lima VM `elinux` (aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`, git 2.53.0, gcc 15.2.0). git 실습은 VM의 `/var/tmp/P5/` 아래 합성(synthetic) 저장소에서 한다.
- 커널 모듈은 랩 커널 **6.18.55-dom**(`/var/lab/out/Image-dom`, 빌드 트리 `linux-build-dom`)으로 빌드하고 QEMU `virt` 게스트(TCG, KVM 없음)에서 insmod 했다. 19명이 VM을 같이 쓰는 중이라 부팅·실행 시간은 실제 하드웨어 값이 아니다.
- 커널 소스는 `/var/lab/src/linux-6.18.55` (git 저장소가 아니라 tarball 트리). 그래서 **진짜 커널 히스토리로 rebase·bisect는 하지 않았다** — 대신 커널 업그레이드에서 실제로 생기는 상황(upstream이 같은 수정을 먼저 넣음, API 인자 변경, 파일 삭제, 빌드 깨진 커밋)을 그대로 재현한 작은 저장소로 실습한다. 진짜 커널 트리에서의 명령은 "가이드(실행 안 함)"로 표시했다.
- stable 실습 자료는 kernel.org에서 받은 실제 파일이다: `patch-6.18.55-56.xz`(136 KB), `ChangeLog-6.18.56`(645 KB).

---

## 1. LTS · stable 주기와 수명 — "언제까지 버틸 수 있나"

E7 §1~§2에서 kernel.org 데이터로 직접 쟀다. 여기서는 업그레이드 판단에 필요한 것만 다시 정리한다.

### 1.1 트리 안 문서가 말하는 것

먼저 커널 소스 자체가 stable·LTS를 어떻게 설명하는지 확인한다. 날짜는 바뀌어도 이 규칙은 잘 안 바뀐다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55
grep -n "stable updates for a little" Documentation/process/2.Process.rst
grep -n "purely a matter" -A2 Documentation/process/2.Process.rst
grep -n "^There are three options" -A6 Documentation/process/stable-kernel-rules.rst'
```

```text
113:kernel. Kernels will typically receive stable updates for a little more
136:The selection of a kernel for long-term support is purely a matter of a
137-maintainer having the need and the time to maintain that release.  There
138-are no known plans for long-term support for any specific upcoming
43:There are three options to submit a change to -stable trees:
44-
45-1. Add a 'stable tag' to the description of a patch you then submit for
46-   mainline inclusion.
47-2. Ask the stable team to pick up a patch already mainlined.
48-3. Submit a patch to the stable team that is equivalent to a change already
49-   mainlined.
```

출력에서 볼 것: (1) 일반 stable은 "한 개발 주기보다 조금 더" 산다(약 3개월, E7 측정 75~100일). (2) LTS 지정은 "maintainer가 필요와 시간이 있을 때"의 결정이라 **트리 문서에는 수명이 적혀 있지 않다**. 수명은 `https://www.kernel.org/category/releases.html`을 봐야 한다. (3) stable에 수정을 넣는 방법은 세 가지다(E7 §5.11). 10절에서 "받는 쪽"에서 이 태그를 쓴다.

### 1.2 LTS 수명 지도

아래 그림의 시작일은 E7이 kernel.org 디렉터리 목록으로 잰 값(2026-10-03 기준 span에서 역산), EOL은 E7이 2026-10-10에 받은 `releases.html`의 projected EOL이다. CIP 연장선은 "LTS 시작 + 약 10년"으로 그린 **개략치**다(CIP가 공개하는 정확한 EOL은 CIP 웹사이트에서 확인 — 기억 기반).

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <line x1="60" y1="240" x2="660" y2="240" stroke="currentColor"/>
  <g font-size="12" text-anchor="middle">
    <line x1="60" y1="240" x2="60" y2="245" stroke="currentColor"/><text x="60" y="258">2020</text>
    <line x1="140" y1="240" x2="140" y2="245" stroke="currentColor"/><text x="140" y="258">2022</text>
    <line x1="220" y1="240" x2="220" y2="245" stroke="currentColor"/><text x="220" y="258">2024</text>
    <line x1="300" y1="240" x2="300" y2="245" stroke="currentColor"/><text x="300" y="258">2026</text>
    <line x1="380" y1="240" x2="380" y2="245" stroke="currentColor"/><text x="380" y="258">2028</text>
    <line x1="460" y1="240" x2="460" y2="245" stroke="currentColor"/><text x="460" y="258">2030</text>
    <line x1="540" y1="240" x2="540" y2="245" stroke="currentColor"/><text x="540" y="258">2032</text>
    <line x1="620" y1="240" x2="620" y2="245" stroke="currentColor"/><text x="620" y="258">2034</text>
  </g>
  <g font-size="13">
    <text x="12" y="44">5.10</text><text x="12" y="78">5.15</text><text x="12" y="112">6.1</text>
    <text x="12" y="146">6.6</text><text x="12" y="180">6.12</text><text x="12" y="214">6.18</text>
  </g>
  <rect x="98" y="30" width="200" height="18" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <line x1="298" y1="39" x2="498" y2="39" stroke="#3f9a6b" stroke-dasharray="5 4" stroke-width="2"/>
  <rect x="133" y="64" width="165" height="18" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <rect x="178" y="98" width="160" height="18" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <line x1="338" y1="107" x2="578" y2="107" stroke="#3f9a6b" stroke-dasharray="5 4" stroke-width="2"/>
  <rect x="213" y="132" width="125" height="18" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="255" y="166" width="163" height="18" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <line x1="418" y1="175" x2="655" y2="175" stroke="#3f9a6b" stroke-dasharray="5 4" stroke-width="2"/>
  <rect x="296" y="200" width="122" height="18" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <line x1="332" y1="22" x2="332" y2="240" stroke="#d0564a" stroke-dasharray="3 3"/>
  <text x="336" y="20" font-size="12">오늘 (2026-10)</text>
  <g font-size="12">
    <text x="345" y="80">EOL 2026-12</text>
    <text x="345" y="148">EOL 2027-12</text>
    <text x="425" y="214">EOL 2028-12 (랩 커널)</text>
    <text x="440" y="34">CIP</text>
    <text x="520" y="102">CIP 6.1</text>
    <text x="560" y="170">CIP 6.12</text>
  </g>
  <rect x="60" y="268" width="14" height="10" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="80" y="277" font-size="12">kernel.org LTS (projected EOL)</text>
  <line x1="300" y1="273" x2="330" y2="273" stroke="#3f9a6b" stroke-dasharray="5 4" stroke-width="2"/>
  <text x="336" y="277" font-size="12">CIP SLTS 연장 (개략, 약 10년)</text>
</svg>
```

그림 2 — 살아 있는 LTS 6개와 CIP SLTS 연장(5.10, 6.1, 6.12). 회색 막대(5.10·5.15)는 2026-12에 kernel.org 지원이 끝난다. 6.18이 랩 커널이다. CIP 브랜치 목록(4.4, 4.19, 5.10, 6.1, 6.12)은 E7 예제 6에서 `git ls-remote`로 확인한 값이다.

읽는 법:

- **5.15 벤더 BSP 제품은 지금(2026-10) 두 달 남았다.** contents.md 시나리오 "5.15를 6.x LTS로 올려라"가 바로 지금 해야 하는 일이다. EOL 뒤에도 커널은 동작하지만, CVE 수정이 더 이상 공짜로 오지 않는다.
- 목표 LTS를 고를 때는 **"우리 제품의 남은 수명 ≤ 목표 LTS의 남은 수명"**이 되는 것을 고른다. 10년 장비면 CIP가 고른 버전(6.12)이 유리하다. 최신 6.18은 kernel.org 기준으로는 가장 길지만 CIP가 고를지는 아직 모른다(2026-10 기준 CIP 브랜치에 없음).
- LTS 수명은 **연장되기도 줄기도 한다**. 처음 2년 예정이던 것이 수요가 있으면 늘어난다(E7 §2.1). 계획은 "projected EOL"로 세우되, 분기마다 releases.html을 다시 본다.

### 1.3 CIP SLTS 한 줄 정리

CIP(Civil Infrastructure Platform, Linux Foundation 프로젝트)는 kernel.org LTS 중 일부를 골라 **약 10년** 유지한다. 브랜치는 `linux-6.1.y-cip`처럼 `-cip` 접미어가 붙고, 일정한 주기로 태그(`v6.1.x-cipN`)를 낸다(태그 형식은 기억 기반 — CIP 저장소로 확인 필요). 철도·전력·공장 장비처럼 "10년 넘게 필드에 있고 커널을 자주 못 올리는" 제품이 대상이다. 국방·차량 장비도 같은 고민을 하므로, 면접에서 "LTS 수명보다 제품 수명이 길면?"에 CIP를 꺼낼 수 있어야 한다. 단, CIP도 **보드 지원은 CIP 참조 보드만** 테스트한다 — 우리 BSP 패치는 여전히 우리 몫이다.

---

## 2. 왜 제품은 옛 커널에 갇히나

### 2.1 갇히는 이유

| 이유 | 실제 모습 | 업그레이드 때 비용 |
|---|---|---|
| 벤더 BSP가 그 버전에만 있다 | SoC 벤더가 5.15 BSP만 배포, 6.x BSP는 다음 해 | 벤더가 새 BSP를 낼 때까지 기다리거나 직접 포팅 |
| out-of-tree 드라이버 | NPU·카메라 ISP·Wi-Fi 바이너리 블롭 + 소스 일부 | 내부 API 변경마다 우리가 고침 (6절) |
| 패치 스택이 크다 | "핫픽스"가 수백 개 쌓였고 출처가 기록 안 됨 | 분류에 시간이 대부분 든다 (3절) |
| 인증·검증 비용 | 차량·국방·의료는 커널을 바꾸면 재검증 | 테스트 자동화가 없으면 수개월 |
| 유저 공간 의존 | 오래된 사용자 도구가 사라진 sysfs·ioctl·debugfs를 씀 | 커널은 ABI를 지키지만 debugfs·벤더 ioctl은 예외 |
| 담당자가 없다 | 처음 BSP를 올린 사람이 퇴사 | 왜 그 패치가 있는지 아무도 모름 |

### 2.2 머무르는 비용은 시간이 갈수록 커진다

옛 커널에 머물면 매달 두 가지가 쌓인다. (1) upstream에서 고쳐진 버그가 우리 트리에는 없다 — stable 트리는 EOL 이후 수정을 받지 않는다. (2) upstream과 우리 트리의 거리가 벌어져, 나중에 옮길 때 충돌과 API 변경이 늘어난다. 10절에서 볼 것처럼 **6.18.56 stable 한 번에 402개 수정**이 들어간다. EOL 이후에는 이것들을 우리가 직접 골라 backport해야 한다.

그래서 실무 원칙은 두 가지다.

- **stable point release는 정기적으로(주~월 단위) 머지**한다. 이건 "작은 업그레이드"이고 거의 공짜다(E7 §4).
- **LTS 이동은 1~2년마다 계획된 프로젝트**로 한다. 이때 패치 스택을 줄이는 것(upstream에 보내기, P7)이 다음 이동의 비용을 결정한다.

> Don 경험과 연결: SSD 펌웨어에서 "이전 세대 코드 브랜치에 핫픽스만 계속 얹다가 신규 세대 이식 때 수백 개 diff를 분류하던" 상황과 같다. 그때 효과가 있던 규칙(커밋 메시지에 출처·이슈 번호, 플랫폼 공통 코드는 공통 브랜치에만)이 커널에서도 그대로 통한다.

---

## 3. 업그레이드 계획 — 인벤토리와 분류

### 3.1 네 가지 분류

패치 하나하나를 아래 네 칸 중 하나에 넣는 것이 계획의 핵심이다.

| 분류 | 판단 기준 | 할 일 | 실습 저장소의 예 |
|---|---|---|---|
| upstreamed | 같은 수정이 새 base에 이미 있다 (patch-id 같음, 또는 의미상 같은 수정) | drop. 커밋 메시지의 upstream sha로 확인 | `PRODUCT: sensor: fix channel bound check` |
| obsolete | 패치가 고치던 파일·기능이 새 base에서 사라졌다 | drop. 대체 기능이 필요한지만 확인 | `PRODUCT: watchdog_old: shorten timeout` |
| needs port | 대상은 있지만 문맥·API가 바뀌어 그대로 안 붙는다 | 손으로 고친다 (5·6절) | `PRODUCT: imu: count samples` |
| keep | 새 base에 그대로 붙는다 | 그대로. 단 upstream 후보인지 표시 | `PRODUCT: board: add p5 kit config` |

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="240" height="56" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="140" y="43" text-anchor="middle" font-size="13">upstream에 이미 있나?</text>
  <text x="140" y="63" text-anchor="middle" font-size="12">git cherry '-' · upstream sha</text>
  <rect x="20" y="110" width="240" height="56" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="140" y="133" text-anchor="middle" font-size="13">대상 코드가 사라졌나?</text>
  <text x="140" y="153" text-anchor="middle" font-size="12">파일 DELETED · 기능 제거</text>
  <rect x="20" y="200" width="240" height="56" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="140" y="223" text-anchor="middle" font-size="13">새 base에 깨끗이 붙나?</text>
  <text x="140" y="243" text-anchor="middle" font-size="12">rebase 충돌 · 빌드 에러 없음</text>
  <rect x="440" y="20" width="220" height="56" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="550" y="43" text-anchor="middle" font-size="13">drop (upstreamed)</text>
  <text x="550" y="63" text-anchor="middle" font-size="12">스택이 줄어든다</text>
  <rect x="440" y="110" width="220" height="56" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="550" y="133" text-anchor="middle" font-size="13">drop (obsolete)</text>
  <text x="550" y="153" text-anchor="middle" font-size="12">대체 기능 필요한지 확인</text>
  <rect x="440" y="200" width="220" height="56" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="550" y="223" text-anchor="middle" font-size="13">keep</text>
  <text x="550" y="243" text-anchor="middle" font-size="12">upstream 후보로 표시 (P7)</text>
  <rect x="200" y="268" width="240" height="28" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="320" y="287" text-anchor="middle" font-size="13">needs port — 손으로 고친다</text>
  <line x1="260" y1="48" x2="438" y2="48" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="350" y="42" text-anchor="middle" font-size="12">예</text>
  <line x1="260" y1="138" x2="438" y2="138" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="350" y="132" text-anchor="middle" font-size="12">예</text>
  <line x1="260" y1="228" x2="438" y2="228" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="350" y="222" text-anchor="middle" font-size="12">예</text>
  <line x1="140" y1="76" x2="140" y2="108" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="150" y="96" font-size="12">아니오</text>
  <line x1="140" y1="166" x2="140" y2="198" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="150" y="186" font-size="12">아니오</text>
  <path d="M140 256 L140 282 L198 282" fill="none" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="80" y="278" font-size="12">아니오</text>
</svg>
```

그림 3 — 패치 하나를 분류하는 순서. 위 두 질문은 git이 대부분 자동으로 답해 준다(`git cherry`, 파일 존재 여부). 세 번째는 실제로 옮겨 봐야 안다.

### 3.2 예제 1 — 실습 저장소 만들기

무엇을 확인하나: 커널 업그레이드의 전형적인 상황을 담은 작은 저장소를 만든다. upstream `v1.0 → v2.0`(그 사이에 버그 수정, API 인자 변경, 파일 삭제), 벤더 `bsp-1.0`(v1.0 위 2개)과 `bsp-2.0`(v2.0 위로 벤더가 직접 옮긴 2개, 내용 일부 변경), 그리고 우리 `product` 스택 4개.

```sh
#!/bin/bash
# P5 lab2: synthetic upstream v1.0 -> v2.0, vendor bsp-1.0/bsp-2.0, product stack on bsp-1.0
set -e
R=/var/tmp/P5/lab2; rm -rf $R; mkdir -p $R; cd $R; git init -q -b master .
export GIT_AUTHOR_NAME=p5 GIT_AUTHOR_EMAIL=p5@lab GIT_COMMITTER_NAME=p5 GIT_COMMITTER_EMAIL=p5@lab
n=0; c() { n=$((n+1)); export GIT_AUTHOR_DATE="2026-01-01T00:$(printf %02d $n):00Z" \
           GIT_COMMITTER_DATE="2026-01-01T00:$(printf %02d $n):00Z"; git add -A; git commit -qm "$1"; }
mkdir -p include drivers board
printf 'int sensor_read(int ch);\n' > include/sensor_api.h
printf '%s\n' '#include "../include/sensor_api.h"' '#define NCH 4' 'int sensor_read(int ch)' '{' \
  '	if (ch > NCH)' '		return -1;' '	return ch * 10;' '}' > drivers/sensor.c
printf '%s\n' '#include "../include/sensor_api.h"' 'int imu_sample(void)' '{' \
  '	int v = sensor_read(0);' '	return v;' '}' > drivers/imu.c
printf 'int wdt_timeout = 30;\n' > drivers/watchdog_old.c
c "initial: v1.0 tree"; git tag v1.0
git checkout -qb bsp-1.0                                   # ---- vendor BSP on v1.0
printf 'int soc_clk_rate = 24000000;\n' > drivers/soc_clk.c; c "BSP: soc: add clock driver"
printf 'int board_init(void) { return 0; }\n' > board/soc_board.c; c "BSP: soc: board init"
git checkout -qb product                                   # ---- our stack on bsp-1.0
printf 'const char *kit = "p5kit";\n' > board/p5kit.c; c "PRODUCT: board: add p5 kit config"
sed -i 's/if (ch > NCH)/if (ch >= NCH)/' drivers/sensor.c; c "PRODUCT: sensor: fix channel bound check"
sed -i -e 's/^int imu_sample/static int imu_count;\nint imu_sample/' \
       -e 's/^\tint v = sensor_read(0);/&\n\timu_count++;/' drivers/imu.c; c "PRODUCT: imu: count samples"
sed -i 's/30/10/' drivers/watchdog_old.c; c "PRODUCT: watchdog_old: shorten timeout"
git checkout -q master                                     # ---- upstream moves to v2.0
sed -i 's/if (ch > NCH)/if (ch >= NCH)/' drivers/sensor.c; c "sensor: fix channel bound check"
printf '%s\n' 'struct sensor_dev { int id; };' 'int sensor_read(struct sensor_dev *sd, int ch);' > include/sensor_api.h
sed -i -e 's/int sensor_read(int ch)/int sensor_read(struct sensor_dev *sd, int ch)/' \
       -e 's/^\tif (ch >= NCH)/\t(void)sd;\n\tif (ch >= NCH)/' drivers/sensor.c
sed -i -e 's/^int imu_sample/static struct sensor_dev imu_sd;\nint imu_sample/' \
       -e 's/sensor_read(0)/sensor_read(\&imu_sd, 0)/' drivers/imu.c; c "sensor: pass device to sensor_read()"
git rm -q drivers/watchdog_old.c; c "watchdog_old: remove obsolete driver"; git tag v2.0
git checkout -qb bsp-2.0; mkdir -p board                   # ---- vendor ports its BSP to v2.0
printf 'int soc_clk_rate = 25000000;\n' > drivers/soc_clk.c; c "BSP: soc: add clock driver (v2)"
printf 'int board_init(void) { return 0; }\n' > board/soc_board.c; c "BSP: soc: board init"
git checkout -q product; git log --oneline --graph --all
```

(처음 만든 긴 버전(heredoc·python 치환)을 노트용으로 `printf`/`sed`로 줄인 것이다. 이 짧은 버전을 다시 돌려 아래 출력이 해시까지 똑같이 나오는 것을 확인했다.)

```text
* 49c193a BSP: soc: board init
* 0760b43 BSP: soc: add clock driver (v2)
* b94f412 watchdog_old: remove obsolete driver
* 1cd47cb sensor: pass device to sensor_read()
* 0b2c474 sensor: fix channel bound check
| * 9fa9eb7 PRODUCT: watchdog_old: shorten timeout
| * 7ddb7a8 PRODUCT: imu: count samples
| * af12ce4 PRODUCT: sensor: fix channel bound check
| * 701d8ca PRODUCT: board: add p5 kit config
| * d62562b BSP: soc: board init
| * 574bdba BSP: soc: add clock driver
|/  
* 9c50b91 initial: v1.0 tree
```

출력에서 볼 것: `product`는 `bsp-1.0` 위에 있고, `bsp-1.0`은 `v1.0` 위에 있다. 업그레이드 목표는 **우리 커밋 4개만** `bsp-2.0` 위로 옮기는 것이다. 벤더 커밋 2개(`574bdba`, `d62562b`)는 벤더가 이미 `bsp-2.0`으로 옮겨 놓았으니 우리가 옮기면 안 된다. 날짜를 고정했으므로 같은 스크립트를 돌리면 해시도 같다.

### 3.3 예제 2 — 인벤토리: 범위 고르기, `git cherry`, 파일 상태

무엇을 확인하나: (A) 범위를 잘못 잡으면 벤더 커밋까지 "우리 것"으로 섞인다, (B) 바른 범위, (C) `git cherry`로 "이미 upstream에 있는 것" 찾기, (D) 각 커밋이 건드린 파일이 새 base에서 어떤 상태인지.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/P5/lab2
echo "## A. wrong range: bsp-2.0..product"; git log --oneline bsp-2.0..product
echo "## B. right range: bsp-1.0..product"; git log --oneline bsp-1.0..product
echo "## C. git cherry -v bsp-2.0 product bsp-1.0"; git cherry -v bsp-2.0 product bsp-1.0
echo "## D. touched files vs new base"
for c in $(git rev-list --reverse bsp-1.0..product); do
  printf "%s %-45s " $(git log -1 --format=%h $c) "$(git log -1 --format=%s $c)"
  for f in $(git diff-tree --no-commit-id --name-only -r $c); do
    if git cat-file -e bsp-2.0:$f 2>/dev/null; then
      if git diff --quiet bsp-1.0 bsp-2.0 -- $f; then s=same; else s=changed; fi
    else if git cat-file -e bsp-1.0:$f 2>/dev/null; then s=DELETED; else s=new; fi; fi
    printf "%s(%s) " $f $s
  done; echo
done'
```

```text
## A. wrong range: bsp-2.0..product
9fa9eb7 PRODUCT: watchdog_old: shorten timeout
7ddb7a8 PRODUCT: imu: count samples
af12ce4 PRODUCT: sensor: fix channel bound check
701d8ca PRODUCT: board: add p5 kit config
d62562b BSP: soc: board init
574bdba BSP: soc: add clock driver
## B. right range: bsp-1.0..product
9fa9eb7 PRODUCT: watchdog_old: shorten timeout
7ddb7a8 PRODUCT: imu: count samples
af12ce4 PRODUCT: sensor: fix channel bound check
701d8ca PRODUCT: board: add p5 kit config
## C. git cherry -v bsp-2.0 product bsp-1.0
+ 701d8ca3398c91bdf05af31bfc03fc67d7fc2b90 PRODUCT: board: add p5 kit config
- af12ce4c2a0a156e06c4ea78ca0349732c894d38 PRODUCT: sensor: fix channel bound check
+ 7ddb7a8883746a7f1d75e4b1a57aba1cf6d64f06 PRODUCT: imu: count samples
+ 9fa9eb718d62f522431012dc03e5c82207994099 PRODUCT: watchdog_old: shorten timeout
## D. touched files vs new base
701d8ca PRODUCT: board: add p5 kit config             board/p5kit.c(new) 
af12ce4 PRODUCT: sensor: fix channel bound check      drivers/sensor.c(changed) 
7ddb7a8 PRODUCT: imu: count samples                   drivers/imu.c(changed) 
9fa9eb7 PRODUCT: watchdog_old: shorten timeout        drivers/watchdog_old.c(DELETED) 
```

출력에서 볼 것:

- A는 6개, B는 4개다. `bsp-2.0..product`는 "bsp-2.0에 없는 product 커밋"이라 **옛 벤더 커밋까지 포함**된다. 벤더 커밋은 bsp-2.0에서 내용이 바뀌어(클럭 24 MHz → 25 MHz) 해시도 patch-id도 다르기 때문이다. 우리 스택의 진짜 범위는 **옛 base부터**(B)다. 이게 4절 `--onto`가 필요한 이유다.
- `git cherry <upstream> <head> <limit>`은 `limit..head`의 각 커밋에 대해 "upstream에 같은 patch-id가 있으면 `-`, 없으면 `+`"를 찍는다. `af12ce4`가 `-` — upstream `0b2c474`와 diff가 글자 하나까지 같다. **해시는 다르지만 patch-id가 같다.**
- D의 `DELETED`는 obsolete 후보, `changed`는 충돌 후보(needs port 가능), `new`는 keep 후보다. 진짜 커널에서는 이 스크립트를 수백 개 커밋에 돌려 스프레드시트로 만든다.

진짜 커널에서는 (가이드, 실행 안 함):

```sh
# 우리 스택 범위: 벤더 BSP 태그(옛 base)부터 제품 브랜치까지
git log --oneline --no-merges lf-5.15.y..product-5.15 > stack.txt   # 태그 이름은 벤더마다 다르다
git cherry -v v6.18.55 product-5.15 lf-5.15.y > cherry.txt          # '-' = 같은 patch-id가 6.18.55에 있음
# 커밋 메시지에 남긴 upstream sha가 새 base에 있는지 (E7 §4.4의 cherry-pick -x 관습)
git log --format='%h %(trailers:key=commit,valueonly)' lf-5.15.y..product-5.15
git merge-base --is-ancestor <upstream-sha> v6.18.55 && echo "already in 6.18.55"
```

함정: patch-id는 **diff가 완전히 같을 때만** 같다. backport하면서 문맥을 손봤거나 다르게 고쳤으면 `+`로 나온다. 그래서 `git cherry`의 `-`는 "확실히 upstream", `+`는 "아직 모름"이다. `+` 중 상당수는 커밋 메시지의 `commit <sha> upstream.` 줄이나 제목 검색(`git log --grep`, `git log -S`)으로 사람이 다시 확인해야 한다.

---

## 4. `git rebase --onto` — 스택만 잘라서 옮기기

### 4.1 직관

`git rebase <base>`는 "현재 브랜치에서 `<base>`에 없는 커밋을 전부 `<base>` 위에 다시 쌓는다". 문제는 **"base에 없는 커밋"이 우리 커밋보다 많을 때**다(예제 2의 A). `--onto`는 "어디서부터 잘라서(옛 base) 어디에 붙일지(새 base)"를 따로 지정한다.

```text
git rebase --onto <새 base> <옛 base> <브랜치>
   = <옛 base>..<브랜치> 범위의 커밋을 <새 base> 위에 차례로 cherry-pick 하고, <브랜치>를 그 끝으로 옮긴다
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <line x1="64" y1="70" x2="246" y2="70" stroke="currentColor"/>
  <line x1="264" y1="70" x2="366" y2="70" stroke="#4a7bd0"/>
  <line x1="394" y1="70" x2="496" y2="70" stroke="#3f9a6b"/>
  <line x1="60" y1="80" x2="100" y2="160" stroke="currentColor"/>
  <line x1="124" y1="170" x2="396" y2="170" stroke="currentColor"/>
  <circle cx="50" cy="70" r="14" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="50" y="74" text-anchor="middle" font-size="12">v1</text>
  <circle cx="130" cy="70" r="14" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="130" y="74" text-anchor="middle" font-size="12">U1</text>
  <circle cx="190" cy="70" r="14" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="190" y="74" text-anchor="middle" font-size="12">U2</text>
  <circle cx="250" cy="70" r="14" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="250" y="74" text-anchor="middle" font-size="12">U3</text>
  <circle cx="320" cy="70" r="14" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="320" y="74" text-anchor="middle" font-size="12">B1'</text>
  <circle cx="380" cy="70" r="14" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="380" y="74" text-anchor="middle" font-size="12">B2'</text>
  <circle cx="450" cy="70" r="14" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="450" y="74" text-anchor="middle" font-size="12">P1'</text>
  <circle cx="510" cy="70" r="14" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="510" y="74" text-anchor="middle" font-size="12">P3'</text>
  <text x="250" y="44" text-anchor="middle" font-size="12">v2.0</text>
  <text x="380" y="44" text-anchor="middle" font-size="12">bsp-2.0 (새 base)</text>
  <text x="560" y="74" font-size="12">product (후)</text>
  <circle cx="110" cy="170" r="14" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="110" y="174" text-anchor="middle" font-size="12">B1</text>
  <circle cx="170" cy="170" r="14" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170" y="174" text-anchor="middle" font-size="12">B2</text>
  <circle cx="230" cy="170" r="14" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="230" y="174" text-anchor="middle" font-size="12">P1</text>
  <circle cx="290" cy="170" r="14" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="290" y="174" text-anchor="middle" font-size="12">P2</text>
  <circle cx="350" cy="170" r="14" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="350" y="174" text-anchor="middle" font-size="12">P3</text>
  <circle cx="410" cy="170" r="14" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="410" y="174" text-anchor="middle" font-size="12">P4</text>
  <text x="170" y="205" text-anchor="middle" font-size="12">bsp-1.0 (옛 base)</text>
  <text x="440" y="174" font-size="12">product (전)</text>
  <rect x="208" y="148" width="226" height="44" rx="10" fill="none" stroke="#e08a3c" stroke-dasharray="5 4"/>
  <text x="321" y="140" text-anchor="middle" font-size="12">bsp-1.0..product = 옮길 범위</text>
  <path d="M232 154 C 300 120, 400 110, 446 86" fill="none" stroke="#3f9a6b" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
  <path d="M352 154 C 420 130, 480 110, 506 86" fill="none" stroke="#3f9a6b" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
  <text x="290" y="228" text-anchor="middle" font-size="12">P2: upstream U1과 같은 diff → drop</text>
  <text x="410" y="248" text-anchor="middle" font-size="12">P4: 대상 파일을 U3가 삭제 → drop</text>
  <text x="20" y="282" font-size="13">git rebase -i --onto bsp-2.0 bsp-1.0 product   (B1, B2는 옮기지 않는다)</text>
</svg>
```

그림 4 — `--onto`의 세 인자. 옛 base(`bsp-1.0`)는 "여기까지는 내 것이 아니다"라는 경계, 새 base(`bsp-2.0`)는 "여기 위에 붙인다"는 목적지다. 옮긴 커밋은 내용이 같아도 부모가 달라졌으니 새 해시(P1', P3')를 받는다.

### 4.2 예제 3 — 그냥 `git rebase bsp-2.0`을 하면

무엇을 확인하나: `--onto` 없이 새 base만 주면 무슨 일이 생기는지.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/P5/lab2; git checkout -q -b wrong product
git rebase bsp-2.0 2>&1 | head -12; git status --short
git rebase --abort; git checkout -q product; git branch -D wrong'
```

```text
warning: skipped previously applied commit d62562b
warning: skipped previously applied commit af12ce4
hint: use --reapply-cherry-picks to include skipped commits
hint: Disable this message with "git config set advice.skippedCherryPicks false"
Rebasing (1/4)Auto-merging drivers/soc_clk.c
CONFLICT (add/add): Merge conflict in drivers/soc_clk.c
error: could not apply 574bdba... BSP: soc: add clock driver
hint: Resolve all conflicts manually, mark them as resolved with
hint: "git add/rm <conflicted_files>", then run "git rebase --continue".
hint: You can instead skip this commit: run "git rebase --skip".
hint: To abort and get back to the state before "git rebase", run "git rebase --abort".
hint: Disable this message with "git config set advice.mergeConflict false"
AA drivers/soc_clk.c
Deleted branch wrong (was 9fa9eb7).
```

출력에서 볼 것: git은 patch-id가 같은 두 커밋(벤더의 `board init`, 우리의 `fix channel bound check`)은 알아서 건너뛰었다. 하지만 **벤더가 내용을 바꿔 옮긴 클럭 드라이버 커밋**(`574bdba`)을 우리 커밋처럼 다시 쌓으려다 add/add 충돌이 났다. 커널 BSP에서는 이런 벤더 커밋이 수천 개라, 이 방식으로는 끝이 안 난다. 벤더 커밋은 벤더가 옮긴 것을 믿고, **우리 범위만** 옮긴다.

### 4.3 예제 4 — `--onto` + drop 목록 + 충돌 해결 + range-diff

무엇을 확인하나: 인벤토리 결과(upstreamed·obsolete)를 todo 목록에 자동 반영해 drop하고, 남은 충돌 하나를 zdiff3 표시로 읽어 해결한 뒤, `range-diff`로 "전 스택 vs 후 스택"을 검토하고 빌드까지 확인한다. `git rebase -i`의 편집기를 스크립트로 바꿔(`GIT_SEQUENCE_EDITOR`) 비대화식으로 돌린다.

```sh
#!/bin/sh
# todo_edit.sh — non-interactive "editor" for git rebase -i: "pick <sha>" -> "drop <sha>" for every sha in $DROP
for s in $DROP; do sed -i "s/^pick $s/drop $s/" "$1"; done
sed -i -e '/^#/d' -e '/^$/d' "$1"; cat "$1"
```

```sh
#!/bin/bash
# P5 lab2 steps: inventory -> rebase --onto (drop) -> resolve -> range-diff -> build
cd /var/tmp/P5/lab2 || exit 1
export GIT_COMMITTER_NAME=p5 GIT_COMMITTER_EMAIL=p5@lab GIT_COMMITTER_DATE=2026-01-01T01:00:00Z
git config merge.conflictStyle zdiff3
git branch -f product-bsp1 product                     # backup of the old stack
echo "### 1. inventory"; git cherry -v bsp-2.0 product bsp-1.0
UP=$(git cherry bsp-2.0 product bsp-1.0 | awk '/^-/{print substr($2,1,7)}')   # already upstream
OBS=$(git log --format=%h bsp-1.0..product -- drivers/watchdog_old.c)          # file gone upstream
export DROP="$UP $OBS"; echo "drop: $DROP"
echo "### 2. rebase --onto bsp-2.0 bsp-1.0 product"
GIT_SEQUENCE_EDITOR=/Users/donh/.elinux_scratch/P5/git/todo_edit.sh \
  git rebase -i --empty=drop --onto bsp-2.0 bsp-1.0 product 2>&1 | grep -v '^hint:'
echo "### 3. conflict (zdiff3)"; git status --short; cat drivers/imu.c
printf '%s\n' '#include "../include/sensor_api.h"' 'static struct sensor_dev imu_sd;' \
  'static int imu_count;' 'int imu_sample(void)' '{' '	int v = sensor_read(&imu_sd, 0);' \
  '	imu_count++;' '	return v;' '}' > drivers/imu.c
git add drivers/imu.c && GIT_EDITOR=true git rebase --continue 2>&1 | tail -1
echo "### 4. new stack"; git log --oneline bsp-2.0..product
echo "### 5. range-diff"; git range-diff --no-color --creation-factor=95 bsp-1.0..product-bsp1 bsp-2.0..product
echo "### 6. build check"
for f in drivers/*.c board/*.c; do gcc -std=gnu11 -Wall -Wextra -c "$f" -o /dev/null && echo "OK $f"; done
```

```text
### 1. inventory
+ 701d8ca3398c91bdf05af31bfc03fc67d7fc2b90 PRODUCT: board: add p5 kit config
- af12ce4c2a0a156e06c4ea78ca0349732c894d38 PRODUCT: sensor: fix channel bound check
+ 7ddb7a8883746a7f1d75e4b1a57aba1cf6d64f06 PRODUCT: imu: count samples
+ 9fa9eb718d62f522431012dc03e5c82207994099 PRODUCT: watchdog_old: shorten timeout
drop: af12ce4 9fa9eb7
### 2. rebase --onto bsp-2.0 bsp-1.0 product
pick 701d8ca # PRODUCT: board: add p5 kit config
drop af12ce4 # PRODUCT: sensor: fix channel bound check
pick 7ddb7a8 # PRODUCT: imu: count samples
drop 9fa9eb7 # PRODUCT: watchdog_old: shorten timeout
Rebasing (1/4)Rebasing (2/4)Rebasing (3/4)Auto-merging drivers/imu.c
CONFLICT (content): Merge conflict in drivers/imu.c
error: could not apply 7ddb7a8... PRODUCT: imu: count samples
Could not apply 7ddb7a8... # PRODUCT: imu: count samples
### 3. conflict (zdiff3)
UU drivers/imu.c
#include "../include/sensor_api.h"
<<<<<<< HEAD
static struct sensor_dev imu_sd;
||||||| parent of 7ddb7a8 (PRODUCT: imu: count samples)
=======
static int imu_count;
>>>>>>> 7ddb7a8 (PRODUCT: imu: count samples)
int imu_sample(void)
{
<<<<<<< HEAD
	int v = sensor_read(&imu_sd, 0);
||||||| parent of 7ddb7a8 (PRODUCT: imu: count samples)
	int v = sensor_read(0);
=======
	int v = sensor_read(0);
	imu_count++;
>>>>>>> 7ddb7a8 (PRODUCT: imu: count samples)
	return v;
}
Rebasing (4/4)Successfully rebased and updated refs/heads/product.
### 4. new stack
0aafd5a PRODUCT: imu: count samples
5e990cb PRODUCT: board: add p5 kit config
### 5. range-diff
1:  701d8ca = 1:  5e990cb PRODUCT: board: add p5 kit config
2:  af12ce4 < -:  ------- PRODUCT: sensor: fix channel bound check
3:  7ddb7a8 ! 2:  0aafd5a PRODUCT: imu: count samples
    @@ Commit message
      ## drivers/imu.c ##
     @@
      #include "../include/sensor_api.h"
    + static struct sensor_dev imu_sd;
     +static int imu_count;
      int imu_sample(void)
      {
    - 	int v = sensor_read(0);
    + 	int v = sensor_read(&imu_sd, 0);
     +	imu_count++;
      	return v;
      }
4:  9fa9eb7 < -:  ------- PRODUCT: watchdog_old: shorten timeout
### 6. build check
OK drivers/imu.c
OK drivers/sensor.c
OK drivers/soc_clk.c
OK board/p5kit.c
OK board/soc_board.c
```

출력에서 볼 것:

- **todo 목록이 곧 분류표**다. `pick`=keep/needs-port, `drop`=upstreamed/obsolete. 진짜 업그레이드에서는 이 todo 파일을 리뷰 받고 저장소에 남긴다(왜 drop했는지 한 줄 주석과 함께).
- 4개 중 2개만 남았고 새 해시(`5e990cb`, `0aafd5a`)를 받았다. 벤더 커밋은 옮기지 않았다(`bsp-2.0..product`가 2개).
- range-diff의 기호: `=` 같은 커밋(내용 동일), `<` 왼쪽(전)에만 있음 = drop된 것, `!` 짝은 있지만 내용이 바뀜. `!` 아래는 **diff의 diff**다: 첫 열은 "전→후"의 변화, 둘째 열은 원래 diff 기호다. `- 	int v = sensor_read(0);` / `+ 	int v = sensor_read(&imu_sd, 0);`는 "우리 패치의 문맥 줄이 새 API로 바뀌었다"는 뜻이고, `+imu_count++` 줄 자체(둘째 열 `+`)는 그대로다 — 즉 **우리 의도는 보존되고 문맥만 바뀌었다**. 리뷰어는 range-diff만 보고 포팅이 맞는지 판단할 수 있다.
- `--creation-factor=95`: range-diff는 "얼마나 바뀌어야 다른 커밋으로 볼지"를 비용으로 정한다(기본 60). 이 예제처럼 작은 커밋에서 문맥이 크게 바뀌면 기본값에서는 짝을 못 찾고 `<`와 `>` 두 줄로 나온다 — 실제로 기본값으로 돌렸을 때 `3: 7ddb7a8 < -: -------`와 `-: ------- > 2: ...`로 따로 나왔다. 짝이 안 맞으면 이 값을 올린다.
- 빌드 확인은 범위가 작아도 반드시 한다. 충돌 없이 붙은 커밋도 **의미상 깨졌을 수 있다**(아래 5.3).

### 4.4 함정 — `--onto`에서는 upstream 패치가 자동으로 빠지지 않을 수 있다

예제 3(plain rebase)에서는 git이 `af12ce4`를 "skipped previously applied commit"으로 자동으로 건너뛰었다. 그런데 처음 이 실습을 `--onto`로 돌렸을 때, drop 목록에 `af12ce4`를 넣지 않았더니 git은 그것을 **그대로 pick했고 충돌**이 났다(실제 출력 일부):

```text
Rebasing (1/4)Rebasing (2/4)Auto-merging drivers/sensor.c
CONFLICT (content): Merge conflict in drivers/sensor.c
error: could not apply af12ce4... PRODUCT: sensor: fix channel bound check
```

이유: rebase가 "이미 적용된 커밋"을 거르는 비교 대상은 **`<upstream>` 인자**다. `--onto bsp-2.0 bsp-1.0`에서 `<upstream>`은 `bsp-1.0`(옛 base)이고, 거기에는 그 수정이 없다. 그래서 새 base(bsp-2.0)에 같은 수정이 있어도 걸러지지 않는다(이 동작은 이 VM의 git 2.53.0에서 관찰한 것이다). 게다가 upstream이 같은 줄 근처(`(void)sd;` 추가)를 또 바꿨기 때문에 "적용하면 빈 커밋"이 되지도 않고 충돌이 됐다. **그래서 `--onto`를 쓸 때는 `git cherry`로 먼저 `-`를 찾아 직접 drop 한다** — 예제 4가 그렇게 했다.

---

## 5. 충돌 해결 전략

### 5.1 원칙 — 충돌은 "두 의도의 합"이다

충돌 표시는 세 칸을 보여 준다(zdiff3): `HEAD`(새 base 쪽 = upstream이 바꾼 결과), `|||||||` 아래 공통 조상(옛 코드), `=======` 아래 우리 커밋이 바꾼 결과. 해결은 "둘 중 하나 고르기"가 아니라 **upstream의 변경 의도(API에 sd 추가)와 우리 의도(카운터 증가)를 둘 다 살리는 것**이다. 공통 조상 칸이 없으면(기본 `merge` 스타일) 누가 무엇을 바꿨는지 추측해야 해서 실수가 는다. 커널 문서 `Documentation/process/backporting.rst`도 "Merge styles and diff3" 절에서 `git config merge.conflictStyle diff3`를 권하고, Git 2.35에 추가된 `zdiff3`를 소개한다(트리에서 확인).

### 5.2 충돌 해결 체크리스트

| 단계 | 명령·행동 | 왜 |
|---|---|---|
| 1. 조상 칸 보기 | `merge.conflictStyle zdiff3` | 누가 무엇을 바꿨는지 분리 |
| 2. upstream 쪽 의도 찾기 | `git log -L:'\<imu_sample\>':drivers/imu.c bsp-1.0..bsp-2.0`<br>`git log bsp-1.0..bsp-2.0 -- drivers/imu.c` | 어떤 커밋이 왜 바꿨나 (backporting.rst의 git log 절) |
| 3. 우리 의도 다시 읽기 | `git show REBASE_HEAD` | 커밋 메시지·diff로 목적 확인 |
| 4. 합쳐 쓰기 | 에디터 / `git mergetool` | 두 의도 모두 보존 |
| 5. 메시지에 흔적 | 본문에 `[ Don: adapted to sensor_read(sd, ch) API ]` | 다음 사람이 range-diff `!`를 이해 |
| 6. 같은 충돌 반복 자동화 | `git config rerere.enabled true` | 다시 rebase할 때 같은 해결을 재사용 |
| 7. 빌드·테스트 | `make W=1`, 부팅, 해당 드라이버 테스트 | 텍스트 충돌이 없어도 의미가 깨질 수 있다 |

(2·6·7 행의 명령은 이 노트에서 실행하지 않았다. `git log -L`·`git log -G` 사용법은 backporting.rst에 그대로 있다.)

### 5.3 충돌이 없는데 깨지는 경우 — 의미 충돌

git의 3-way merge는 **줄 단위 텍스트**만 본다. 다음은 충돌 없이 붙지만 깨진다.

- upstream이 함수의 **잠금 규칙**을 바꿨다(예: 이제 호출자가 lock을 잡아야 함). 우리 패치는 옛 규칙으로 호출 → 컴파일은 되고 lockdep(M8)에서만 걸린다.
- upstream이 함수 **반환값 의미**를 바꿨다(0/1 → 0/-errno). 우리 `if (ret == 1)`은 조용히 죽은 코드가 된다.
- upstream이 리소스를 **devm으로 바꿨다**. 우리 패치의 `remove()`에 남은 수동 해제가 이중 해제가 된다.
- 다른 파일에서 같은 기능을 upstream이 이미 추가 → 우리 패치와 **두 번** 등록.

backporting.rst의 "Gotchas" 절(Function arguments, Error handling, Refactored code)이 바로 이 목록이다. 대응은 7절의 회귀 테스트와 리뷰어가 보는 range-diff다.

---

## 6. API 변화 — 6.1 시절 드라이버가 6.18에서 깨지는 모습

### 6.1 예제 5 — 트리에서 직접 확인한 API 모양 (6.18.55)

무엇을 확인하나: 이 노트에서 쓰는 API 변화가 랩 트리에 실제로 어떤 모양인지, 옛 이름이 헤더에 남아 있는지 grep으로 확인한다.

```sh
#!/bin/bash
# P5: verify API shapes in the 6.18.55 tree
cd /var/lab/src/linux-6.18.55
grep -n "(\*remove)" include/linux/platform_device.h include/linux/spi/spi.h include/linux/i2c.h
grep -n "(\*probe)(struct i2c_client" include/linux/i2c.h
grep -n "class_create(" include/linux/device/class.h
grep -n "define timer_container_of\|int timer_delete_sync" include/linux/timer.h
grep -n "void hrtimer_setup(" include/linux/hrtimer.h
grep -n "define MODULE_IMPORT_NS" include/linux/module.h
grep -n "Static allocation of GPIO base" drivers/gpio/gpiolib.c
grep -n "must not\* be included" include/linux/gpio.h
for s in from_timer del_timer_sync hrtimer_init strlcpy no_llseek devm_gpio_request probe_new remove_new; do
  printf "%-18s %s\n" $s "$(grep -rwl $s include/linux | wc -l) headers"; done
```

```text
include/linux/platform_device.h:233:	void (*remove)(struct platform_device *);
include/linux/spi/spi.h:350:	void			(*remove)(struct spi_device *spi);
include/linux/i2c.h:275:	void (*remove)(struct i2c_client *client);
274:	int (*probe)(struct i2c_client *client);
226:struct class * __must_check class_create(const char *name);
132:#define timer_container_of(var, callback_timer, timer_fieldname)	\
165:extern int timer_delete_sync_try(struct timer_list *timer);
166:extern int timer_delete_sync(struct timer_list *timer);
230:extern void hrtimer_setup(struct hrtimer *timer, enum hrtimer_restart (*function)(struct hrtimer *),
307:#define MODULE_IMPORT_NS(ns)	MODULE_INFO(import_ns, ns)
1137:				 "Static allocation of GPIO base is deprecated, use dynamic allocation.\n");
3: * NOTE: This header *must not* be included.
from_timer         0 headers
del_timer_sync     0 headers
hrtimer_init       0 headers
strlcpy            0 headers
no_llseek          0 headers
devm_gpio_request  0 headers
probe_new          0 headers
remove_new         0 headers
```

이걸 표로 정리하면 다음과 같다. **"6.18.55 모양"과 "옛 이름 0개"는 위 출력으로 검증했다.** "바뀐 버전" 열은 기억 기반이다(정확한 버전은 `git log -S<이름>`으로 mainline 히스토리에서 확인 필요 — 이 트리는 git 히스토리가 없다).

| 항목 | 6.1 시절 | 6.18.55 (검증) | 바뀐 시점 (기억 기반) |
|---|---|---|---|
| platform `.remove` | `int (*remove)(struct platform_device *)` | `void (*remove)(...)` | `remove_new`로 과도기(6.3경) → `.remove`가 void (6.11경) |
| i2c `.probe` | `probe(client, id)` + `probe_new(client)` | `int (*probe)(struct i2c_client *)` | 6.3경 단일 인자로, `probe_new` 제거 |
| `class_create` | `class_create(THIS_MODULE, name)` (매크로) | `class_create(const char *name)` | 6.4경 |
| 타이머 해제 | `del_timer_sync()` | `timer_delete_sync()` | 이름 변경 후 옛 이름 제거 (6.15~6.16경) |
| 타이머 콜백 | `from_timer(var, t, field)` | `timer_container_of(...)` | 6.16경 |
| hrtimer 초기화 | `hrtimer_init()` + `.function =` | `hrtimer_setup(timer, fn, clock, mode)` | 6.15경 |
| 문자열 복사 | `strlcpy()` | `strscpy()` (2인자 형태 가능) | 6.8경 제거 |
| llseek | `.llseek = no_llseek` | 없음 → `noop_llseek` 또는 필드 생략 | 6.12경 제거 |
| 심볼 네임스페이스 | `MODULE_IMPORT_NS(IIO)` | `MODULE_IMPORT_NS("IIO")` (문자열) | 6.13경 |
| 레거시 GPIO | `gpio_request()`, `devm_gpio_request()` | 헤더는 "must not be included", `devm_gpio_request` 없음, `gpio_request`·`devm_gpio_request_one`은 아직 있음 | 단계적 제거 진행 중 |
| modpost | `MODULE_DESCRIPTION` 없어도 조용 | `WARNING: missing MODULE_DESCRIPTION()` | 6.11경 |

레거시 GPIO 숫자 공간(global GPIO number space)은 **6.18.55에서 아직 완전히 없어지지 않았다**: `CONFIG_GPIOLIB_LEGACY`가 `def_bool y`로 남아 있고 `gpio_request()`도 있다. 대신 gpiolib는 정적 base 할당에 "deprecated" 경고를 찍고, 일부 devm 래퍼는 이미 제거됐다. 새 코드는 무조건 `gpiod_*`(descriptor) API를 쓴다(H 모듈·D4).

### 6.2 예제 6 — 6.1 시절 스타일 드라이버를 6.18에서 빌드하면

무엇을 확인하나: 센서 키트 BSP에 있을 법한 out-of-tree 드라이버(platform driver + I2C driver + class + 타이머 + 레거시 GPIO)를 6.1 시절 API로 쓰고, `linux-build-dom`(6.18.55)으로 빌드해 진짜 에러를 본다. 이 코드는 6.1 트리에서는 빌드됐을 모양이다(6.1 트리로 직접 빌드해 보지는 않았다 — 기억 기반).

```c
// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/fs.h>
struct p5_dev { struct timer_list timer; char name[16]; };
static struct p5_dev p5;
static struct class *p5_class;
static struct platform_device *p5_pdev;
static int gpio = -1;		/* legacy global GPIO number */
static void p5_timer_fn(struct timer_list *t)
{
	struct p5_dev *d = from_timer(d, t, timer);
	pr_info("p5: timer fired for %s\n", d->name);
}
static const struct file_operations p5_fops = { .owner = THIS_MODULE, .llseek = no_llseek };

static int p5_probe(struct platform_device *pdev)
{
	if (gpio_is_valid(gpio) && devm_gpio_request(&pdev->dev, gpio, "p5"))
		return -EBUSY;
	strlcpy(p5.name, "p5-legacy", sizeof(p5.name));
	timer_setup(&p5.timer, p5_timer_fn, 0);
	mod_timer(&p5.timer, jiffies + HZ / 10);
	dev_info(&pdev->dev, "probe %s fops=%ps\n", p5.name, &p5_fops);
	return 0;
}
static int p5_remove(struct platform_device *pdev)
{
	del_timer_sync(&p5.timer);
	return 0;
}
static struct platform_driver p5_drv = {
	.probe = p5_probe, .remove = p5_remove, .driver = { .name = "p5_legacy" } };
static int p5_i2c_probe(struct i2c_client *c, const struct i2c_device_id *id) { return 0; }
static struct i2c_driver p5_i2c = { .driver = { .name = "p5_sensor" },
	.probe = p5_i2c_probe };
static int __init p5_init(void)
{
	p5_class = class_create(THIS_MODULE, "p5_legacy");
	if (IS_ERR(p5_class)) return PTR_ERR(p5_class);
	i2c_add_driver(&p5_i2c);
	platform_driver_register(&p5_drv);
	p5_pdev = platform_device_register_simple("p5_legacy", -1, NULL, 0);
	return 0;
}
static void __exit p5_exit(void)
{
	platform_device_unregister(p5_pdev);
	platform_driver_unregister(&p5_drv);
	i2c_del_driver(&p5_i2c);
	class_destroy(p5_class);
}
module_init(p5_init); module_exit(p5_exit);
MODULE_LICENSE("GPL"); MODULE_IMPORT_NS(P5_DUMMY_NS);
```

```make
obj-m := p5_legacy.o
```

```sh
limactl shell elinux -- bash -lc 'cp -r /Users/donh/.elinux_scratch/P5/old /var/tmp/P5/old; cd /var/tmp/P5/old
make -C /var/lab/out/linux-build-dom M=$PWD modules 2>&1 | grep -E "error|warning|Error"
echo "--- error count: $(make -C /var/lab/out/linux-build-dom M=$PWD modules 2>&1 | grep -c " error: ")"'
```

```text
p5_legacy.c:14:28: error: implicit declaration of function ‘from_timer’; did you mean ‘mod_timer’? [-Wimplicit-function-declaration]
p5_legacy.c:14:45: error: ‘timer’ undeclared (first use in this function); did you mean ‘ktimerd’?
p5_legacy.c:17:81: error: ‘no_llseek’ undeclared here (not in a function); did you mean ‘noop_llseek’?
p5_legacy.c:21:36: error: implicit declaration of function ‘devm_gpio_request’; did you mean ‘devm_gpio_request_one’? [-Wimplicit-function-declaration]
p5_legacy.c:23:9: error: implicit declaration of function ‘strlcpy’; did you mean ‘strncpy’? [-Wimplicit-function-declaration]
p5_legacy.c:31:9: error: implicit declaration of function ‘del_timer_sync’ [-Wimplicit-function-declaration]
p5_legacy.c:35:38: error: initialization of ‘void (*)(struct platform_device *)’ from incompatible pointer type ‘int (*)(struct platform_device *)’ [-Wincompatible-pointer-types]
p5_legacy.c:38:18: error: initialization of ‘int (*)(struct i2c_client *)’ from incompatible pointer type ‘int (*)(struct i2c_client *, const struct i2c_device_id *)’ [-Wincompatible-pointer-types]
/var/lab/src/linux-6.18.55/include/linux/init.h:174:22: error: passing argument 1 of ‘class_create’ from incompatible pointer type [-Wincompatible-pointer-types]
p5_legacy.c:41:20: error: too many arguments to function ‘class_create’; expected 1, have 2
p5_legacy.c:56:41: error: expected ‘,’ or ‘;’ before ‘P5_DUMMY_NS’
make[3]: *** [/var/lab/src/linux-6.18.55/scripts/Makefile.build:287: p5_legacy.o] Error 1
make[2]: *** [/var/lab/src/linux-6.18.55/Makefile:2050: .] Error 2
make[1]: *** [/var/lab/src/linux-6.18.55/Makefile:248: __sub-make] Error 2
make: *** [/var/lab/src/linux-6.18.55/Makefile:248: __sub-make] Error 2
--- error count: 11
```

출력에서 볼 것 — **에러 메시지 종류가 곧 포팅 유형**이다.

- `implicit declaration of function` (5개): 함수가 **없어졌거나 이름이 바뀌었다**. gcc의 "did you mean" 힌트는 반쯤만 맞다: `noop_llseek`는 맞지만 `strncpy`(→ 정답은 `strscpy`), `mod_timer`(→ 정답 `timer_container_of`)는 틀린 힌트다. 정답은 upstream에서 그 함수를 지운 커밋 메시지나 `Documentation/`에 있다.
- `incompatible pointer type` (3개): 콜백 **시그니처가 바뀌었다**. 커널은 이 경고를 에러로 올린다(`scripts/Makefile.extrawarn`의 `-Werror=incompatible-pointer-types` — 트리에서 확인). 예전처럼 경고로 넘어갔다면 `int` 반환을 `void`로 부르는 미묘한 ABI 버그가 됐을 것이다.
- `too many arguments` (1개): 인자 **개수**가 바뀌었다(`class_create`에서 owner가 빠짐).
- `expected ‘,’ or ‘;’` (1개): **매크로 문법**이 바뀌었다. `MODULE_IMPORT_NS`가 이제 문자열 리터럴을 받는다 — 에러 메시지만으로는 이유가 안 보이는 유형이라 가장 시간을 잡아먹는다.
- `gpio_is_valid()`는 에러가 없다 — 레거시 GPIO 헤더는 아직 있다(6.1절). **빌드가 된다고 끝이 아니다**: 레거시 숫자 GPIO는 런타임에 base가 바뀌면 다른 핀을 잡는다.

### 6.3 예제 7 — 포팅하고 빌드하기

무엇을 확인하나: 위 11개 에러를 6.18.55 API로 고친다. 레거시 GPIO 번호는 descriptor API(`devm_gpiod_get_optional`)로 바꾸고, 그 김에 `init`의 에러 경로도 고친다(옛 코드는 등록 실패를 무시했다 — 업그레이드는 숨은 버그를 고칠 기회다).

```c
// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h>
struct p5_dev { struct timer_list timer; char name[16]; };
static struct p5_dev p5;
static const struct class *p5_class;	/* 6.4+: const, no owner */
static struct platform_device *p5_pdev;
static void p5_timer_fn(struct timer_list *t)
{
	struct p5_dev *d = timer_container_of(d, t, timer);
	pr_info("p5: timer fired for %s\n", d->name);
}
static const struct file_operations p5_fops = { .owner = THIS_MODULE, .llseek = noop_llseek };
static int p5_probe(struct platform_device *pdev)
{
	struct gpio_desc *g = devm_gpiod_get_optional(&pdev->dev, "p5", GPIOD_OUT_LOW);
	if (IS_ERR(g)) return PTR_ERR(g);
	strscpy(p5.name, "p5-ported");
	timer_setup(&p5.timer, p5_timer_fn, 0);
	mod_timer(&p5.timer, jiffies + HZ / 10);
	dev_info(&pdev->dev, "probe %s gpio=%s fops=%ps\n", p5.name, g ? "yes" : "none", &p5_fops);
	return 0;
}
static void p5_remove(struct platform_device *pdev)
{
	timer_delete_sync(&p5.timer);
}
static struct platform_driver p5_drv = {
	.probe = p5_probe, .remove = p5_remove, .driver = { .name = "p5_legacy" } };
static int p5_i2c_probe(struct i2c_client *c) { return 0; }
static struct i2c_driver p5_i2c = { .driver = { .name = "p5_sensor" }, .probe = p5_i2c_probe };
static int __init p5_init(void)
{
	int ret;
	p5_class = class_create("p5_legacy");
	if (IS_ERR(p5_class)) return PTR_ERR(p5_class);
	ret = i2c_add_driver(&p5_i2c);
	if (ret) goto err_class;
	ret = platform_driver_register(&p5_drv);
	if (ret) goto err_i2c;
	p5_pdev = platform_device_register_simple("p5_legacy", -1, NULL, 0);
	if (!IS_ERR(p5_pdev)) return 0;
	ret = PTR_ERR(p5_pdev);
	platform_driver_unregister(&p5_drv);
err_i2c:	i2c_del_driver(&p5_i2c);
err_class:	class_destroy(p5_class);
	return ret;
}
static void __exit p5_exit(void)
{
	platform_device_unregister(p5_pdev);
	platform_driver_unregister(&p5_drv);
	i2c_del_driver(&p5_i2c);
	class_destroy(p5_class);
}
module_init(p5_init); module_exit(p5_exit);
MODULE_LICENSE("GPL"); MODULE_IMPORT_NS("P5_DUMMY_NS");
MODULE_DESCRIPTION("P5 legacy driver ported to 6.18");
```

```sh
limactl shell elinux -- bash -lc 'cp -r /Users/donh/.elinux_scratch/P5/new /var/tmp/P5/new; cd /var/tmp/P5/new
make -C /var/lab/out/linux-build-dom M=$PWD modules 2>&1 | grep -v "Entering\|Leaving"
modinfo -F vermagic p5_ported.ko; modinfo -F import_ns p5_ported.ko'
```

```text
  CC [M]  p5_ported.o
  MODPOST Module.symvers
  CC [M]  p5_ported.mod.o
  CC [M]  .module-common.o
  LD [M]  p5_ported.ko
6.18.55-dom SMP preempt mod_unload aarch64
P5_DUMMY_NS
```

출력에서 볼 것: 경고 0개. `MODULE_DESCRIPTION`을 넣기 전 초안을 같은 6.18.55 소스의 `linux-build-trace`로 빌드했을 때는 `WARNING: modpost: missing MODULE_DESCRIPTION() in p5_ported.o`가 나왔다(실제 출력) — 새 커널은 **경고도 늘어난다**. CI에서 경고를 에러로 처리하면 업그레이드 첫날 빌드가 깨지므로, 업그레이드 브랜치에서는 경고 목록도 할 일 목록으로 관리한다.

포팅 대응표(에러 → 수정):

| 에러 | 옛 코드 | 새 코드 |
|---|---|---|
| `from_timer` 없음 | `from_timer(d, t, timer)` | `timer_container_of(d, t, timer)` |
| `no_llseek` 없음 | `.llseek = no_llseek` | `.llseek = noop_llseek` (또는 필드 생략) |
| `devm_gpio_request` 없음 | 전역 GPIO 번호 | `devm_gpiod_get_optional(dev, "p5", GPIOD_OUT_LOW)` + DT `p5-gpios` |
| `strlcpy` 없음 | `strlcpy(dst, src, sizeof(dst))` | `strscpy(dst, src)` (배열이면 크기 자동) |
| `del_timer_sync` 없음 | `del_timer_sync(&t)` | `timer_delete_sync(&t)` |
| platform `.remove` 타입 | `int p5_remove(...)` + `return 0` | `void p5_remove(...)` |
| i2c `.probe` 타입 | `probe(client, id)` | `probe(client)`, id가 필요하면 `i2c_client_get_device_id(client)` |
| `class_create` 인자 | `class_create(THIS_MODULE, name)` | `class_create(name)` |
| `MODULE_IMPORT_NS` | `MODULE_IMPORT_NS(NS)` | `MODULE_IMPORT_NS("NS")` |

(`i2c_client_get_device_id`는 6.18.55 `include/linux/i2c.h`에 있는 것을 grep으로 확인한 이름이다 — 아래 함정 참조.)

함정: `.remove`를 `void`로 바꿀 때 **옛 `remove`가 에러를 반환하던 경로**를 확인한다. driver core는 원래부터 remove의 반환값을 무시했다(그래서 void로 바뀌었다). 옛 코드가 "에러면 정리를 덜 하고 return"했다면 그건 원래부터 누수 버그였다 — void로 바꾸면서 끝까지 정리하도록 고친다.

### 6.4 예제 8 — Image-dom 게스트에서 올리기 (+ 다른 커널용 .ko는 거부된다)

무엇을 확인하나: 포팅한 모듈이 실제로 probe·타이머·remove까지 동작하는지, 그리고 **같은 소스·같은 6.18.55라도 다른 빌드 트리(-trace)로 만든 .ko는 거부된다**는 것을 본다. BusyBox initramfs 복사본에 두 .ko와 테스트 스크립트를 넣고 `LAB_CMD`로 실행했다.

```sh
#!/bin/sh
# /p5test.sh — runs inside the 6.18.55-dom guest
echo "## uname -r: $(uname -r)"
echo "## 1. module built for 6.18.55-trace"
insmod /p5_ported_trace.ko; echo "insmod rc=$?"; dmesg | tail -1
echo "## 2. module built for 6.18.55-dom"
insmod /p5_ported_dom.ko; echo "insmod rc=$?"; sleep 1
dmesg | grep -i p5 | tail -3
for d in /sys/class/p5_legacy /sys/bus/platform/drivers/p5_legacy /sys/bus/i2c/drivers/p5_sensor; do [ -d $d ] && echo "exists $d"; done
cat /proc/modules | grep p5
echo "## 3. rmmod"; rmmod p5_ported; echo "rmmod rc=$?"; ls /sys/class | grep -c p5_legacy
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/P5; rm -rf rootfs; mkdir rootfs; cp -a /var/lab/build/rootfs/. rootfs/
cp p5_ported_dom.ko p5_ported_trace.ko rootfs/; cp /Users/donh/.elinux_scratch/P5/p5test.sh rootfs/; chmod +x rootfs/p5test.sh
(cd rootfs && find . | cpio -o -H newc --quiet | gzip) > p5_initrd.gz
timeout 240 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -kernel /var/lab/out/Image-dom \
  -initrd p5_initrd.gz -append "console=ttyAMA0 quiet LAB_CMD=/p5test.sh" 2>&1 | tail -22'
```

```text
lab initramfs: 6.18.55-dom up 16.04s
## uname -r: 6.18.55-dom
## 1. module built for 6.18.55-trace
insmod: can't insert '/p5_ported_trace.ko': invalid module format[   18.701038][   T70] p5_ported: version magic '6.18.55-trace SMP preempt mod_unload aarch64' should be '6.18.55-dom SMP preempt mod_unload aarch64'

insmod rc=1
[   18.701038][   T70] p5_ported: version magic '6.18.55-trace SMP preempt mod_unload aarch64' should be '6.18.55-dom SMP preempt mod_unload aarch64'
## 2. module built for 6.18.55-dom
insmod rc=0
[   18.883254][   T73] p5_ported: module verification failed: signature and/or required key missing - tainting kernel
[   18.917661][   T73] p5_legacy p5_legacy: probe p5-ported gpio=none fops=p5_fops [p5_ported]
[   19.019269][    C0] p5: timer fired for p5-ported
exists /sys/class/p5_legacy
exists /sys/bus/platform/drivers/p5_legacy
exists /sys/bus/i2c/drivers/p5_sensor
p5_ported 12288 0 - Live 0xffffc767e2f7d000 (OE)
## 3. rmmod
rmmod rc=0
0
[   20.740490][   T83] reboot: Power down
```

출력에서 볼 것:

- `version magic '6.18.55-trace ...' should be '6.18.55-dom ...'` → `invalid module format`(-ENOEXEC). 커널 버전 숫자가 같아도 **LOCALVERSION이 다르면 거부**된다. 업그레이드 뒤 "예전 .ko를 rootfs에 그대로 두었다"가 부팅 후 장치가 안 뜨는 흔한 원인이다. (콘솔에서 커널 메시지와 insmod 출력이 한 줄에 섞인 것은 printk와 stdout이 같은 시리얼로 나오기 때문이다.)
- dom용은 로드되어 probe → 100 ms 뒤 타이머 → `rmmod`로 `class`까지 깨끗이 사라졌다(`grep -c` 결과 0). `(OE)` taint는 out-of-tree(O) + 서명 없음(E)이다(E7 §6.4). `gpio=none`은 DT에 `p5-gpios`가 없어서 optional GPIO가 NULL로 온 것 — 정상이다.
- 부팅 16초, 타임스탬프는 TCG + 공유 VM 부하 값이라 실제 HW 시간이 아니다.

### 6.5 예제 9 — config도 옮겨야 한다: `scripts/diffconfig`

무엇을 확인하나: 커널을 올리면 `.config`도 옮겨야 한다(`make olddefconfig`로 새 심볼은 기본값, 없어진 심볼은 버려짐). 그때 **무엇이 조용히 사라졌는지** 보는 도구가 `scripts/diffconfig`다. 여기서는 같은 6.18.55의 두 랩 config(trace → dom)로 시연한다. 진짜 업그레이드에서는 "옛 커널 .config"와 "새 커널에서 olddefconfig한 .config"를 비교한다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55
python3 -I scripts/diffconfig /var/lab/out/linux-build-trace/.config /var/lab/out/linux-build-dom/.config > /var/tmp/P5/dc.txt
grep -c "^-" /var/tmp/P5/dc.txt; grep -c "^+" /var/tmp/P5/dc.txt; grep -c "^ " /var/tmp/P5/dc.txt
grep "^ " /var/tmp/P5/dc.txt | head -8; grep "^-" /var/tmp/P5/dc.txt | sed -n 11,30p'
```

```text
17
187
61
 ADXL345_I2C n -> m
 BMI160_I2C n -> m
 BMP280 n -> m
 CAN m -> y
 CAN_BCM m -> y
 CAN_DEV m -> y
 CAN_GS_USB n -> m
 CAN_GW m -> y
-GDB_SCRIPTS y
-MODULE_ALLOW_BTF_MISMATCH n
-NET_SCH_BPF n
-PAHOLE_HAS_LANG_EXCLUDE y
-PAHOLE_HAS_SPLIT_BTF y
-PROBE_EVENTS_BTF_ARGS y
-SCHED_CLASS_EXT n
```

출력에서 볼 것: `-`(사라짐) 17개, `+`(새로 생김) 187개, 값 변경 61개. `-` 목록의 `GDB_SCRIPTS`, `PROBE_EVENTS_BTF_ARGS` 등은 dom에서 `DEBUG_INFO`를 끄자 **의존성 때문에 자동으로 빠진 것**이다 — 아무도 직접 끄지 않았다. 커널 업그레이드에서도 똑같이, 심볼 이름이 바뀌거나 의존성이 바뀌면 olddefconfig가 조용히 기능을 끈다. 업그레이드 체크리스트에 "diffconfig의 `-` 줄을 전부 사람이 확인"을 넣는다. defconfig 조각(K5의 `.cfg` fragment)을 쓰면 `scripts/kconfig/merge_config.sh`가 "요청한 값이 최종 config에 없다"를 경고해 준다.

---

## 7. Coccinelle — 기계적 변경을 수백 파일에 (가이드, 실행 안 함)

이 VM에는 `spatch`(Coccinelle)가 없다(`which spatch` → 없음). 설치는 공용 상태 변경이라 하지 않았다. 아래는 가이드다.

Coccinelle는 **C 문법을 이해하는 패치 도구**다. sed는 텍스트를 바꾸지만 spatch는 "이 함수 호출의 첫 인자가 `THIS_MODULE`이면 지워라"처럼 구조로 바꾼다. upstream이 API를 바꿀 때 in-tree 수백 개 드라이버를 한 번에 고치는 데 쓰고, 그 semantic patch(`.cocci`)는 보통 커밋 메시지나 `scripts/coccinelle/`에 남는다. 우리 out-of-tree 드라이버 수십 개에 같은 변환을 적용할 때 유용하다.

```text
// p5_class_create.cocci — class_create(THIS_MODULE, name) -> class_create(name)   (guide, not executed)
@@
expression name;
@@
- class_create(THIS_MODULE, name)
+ class_create(name)
```

```text
// p5_remove_void.cocci — int remove() that always returns 0 -> void   (guide, not executed)
@r@
identifier drv, fn;
@@
struct platform_driver drv = { ..., .remove = fn, ... };
@@
identifier r.fn, pdev;
@@
- int
+ void
  fn(struct platform_device *pdev)
  {
  ...
- return 0;
  }
```

사용(가이드): `spatch --sp-file p5_class_create.cocci --dir drivers/our_bsp --in-place`. in-tree 검사는 `make coccicheck MODE=report`(트리의 `Documentation/dev-tools/coccinelle.rst`에 사용법이 있다 — 확인함), 트리에는 `scripts/coccinelle/api/` 아래 `platform_get_irq.cocci`, `stream_open.cocci`, `string_choices.cocci` 같은 규칙이 들어 있다(`ls`로 확인). 두 번째 규칙처럼 "반환 경로가 여러 개"인 경우는 규칙이 놓치는 경우가 있으니, spatch 결과도 **빌드 + range-diff로 리뷰**한다.

---

## 8. 회귀 테스트 — "올렸다"와 "제품으로 쓸 수 있다" 사이

업그레이드 브랜치는 아래 층을 순서대로 통과해야 한다. 위 층일수록 비싸고 느리다. 자동화(P6)가 업그레이드 주기를 결정한다.

| 층 | 무엇을 | 도구·명령 | 이 노트에서 |
|---|---|---|---|
| 1. 빌드 | 커널·DT·모든 out-of-tree 모듈, 경고 0 | `make W=1`, `make dtbs_check`(D7) | 예제 6·7 |
| 2. config | 사라진·바뀐 심볼 확인 | `scripts/diffconfig` | 예제 9 |
| 3. 부팅 | 콘솔·rootfs 마운트·init 완료, 시간 비교 | QEMU/보드 부팅 스크립트, `systemd-analyze`(M10) | 예제 8 (initramfs) |
| 4. 장치 | 모든 장치가 probe됐나, deferred probe 남았나 | `/sys/kernel/debug/devices_deferred`, `dmesg` grep, 장치 노드 목록 diff | 가이드 |
| 5. 단위 테스트 | 커널 내부 로직 | KUnit, kselftest — **P6** | P6 참조 |
| 6. 기능 | 센서 데이터·CAN·카메라 파이프라인 end-to-end | HIL 테스트(P6), Q7 품질 툴 | 가이드 |
| 7. 성능 | 지연·처리량·부팅 시간·전력 | `cyclictest`(N), `perf stat`(M6), iperf3, 같은 HW에서 N회 반복 | 가이드 |
| 8. 장시간 | 메모리 누수·드물게 나는 hang | 72시간 soak, kmemleak(M8) | 가이드 |

### 8.1 "부팅 후 무엇이 달라졌나"를 기계적으로 비교하기 (가이드)

옛 커널과 새 커널에서 같은 명령을 돌려 텍스트로 저장하고 diff 한다. 사람 눈으로 dmesg를 읽는 것보다 훨씬 많이 잡는다.

```sh
# guide (not executed here): run on old and new kernel, then diff the two directories
mkdir -p snap; cd snap
dmesg --notime                       > dmesg.txt          # 타임스탬프 제거 → diff 가능
grep -E "WARNING|BUG:|Oops|probe.*failed|deferred" dmesg.txt > problems.txt
cat /sys/kernel/debug/devices_deferred > deferred.txt     # debugfs 필요
ls -l /dev /dev/v4l/by-path /dev/serial/by-path > devnodes.txt 2>&1
for d in /sys/bus/*/drivers/*; do echo "$d $(ls $d | grep -c :)"; done > drivers.txt
lsmod | sort > lsmod.txt; cat /proc/interrupts > irqs.txt
# diff -ru snap-5.15 snap-6.18 → 사라진 장치·새 경고·바인딩 안 된 드라이버
```

### 8.2 성능 비교의 규칙 (가이드)

- **같은 HW, 같은 rootfs, 같은 config 의도**(diffconfig로 확인)에서만 비교한다. TCG QEMU 숫자는 비교에 쓰지 않는다(이 노트의 부팅 시간도 마찬가지).
- 한 번이 아니라 **N회 반복 후 중앙값과 분산**을 본다(`perf stat -r 10`, `cyclictest` 히스토그램 최대값). 커널 업그레이드의 성능 차이는 대개 몇 % 수준이라 노이즈와 구분해야 한다.
- 차이가 나면 9절의 bisect로 범위를 좁힌다. 이때 테스트 스크립트는 "임계값을 넘으면 bad"로 만든다.

---

## 9. `git bisect run` — 회귀 커밋을 자동으로 찾기

### 9.1 직관

"5.15에서는 되는데 6.18에서는 안 된다"는 그 사이 수만 커밋 중 하나(또는 몇 개)의 탓이다. 이진 탐색이면 log2(N)번 테스트로 찾는다: 3만 커밋이면 약 15번. 사람이 매번 빌드·부팅·판정하는 대신, **판정 스크립트의 종료 코드**로 git이 다음 커밋을 고르게 하는 것이 `git bisect run`이다.

| 스크립트 종료 코드 | 의미 |
|---|---|
| 0 | good |
| 1 ~ 127 (125 제외) | bad |
| 125 | 이 커밋은 테스트 불가 → skip (빌드 깨짐 등) |
| 128 이상 | bisect 중단 (스크립트 자체 오류로 취급) |

(종료 코드 규칙은 `git help bisect`의 "bisect run" 절 기준이다.)

### 9.2 예제 10 — 32커밋 중 회귀 하나 + 빌드 깨진 커밋 하나

무엇을 확인하나: 링버퍼 "드라이버"에 32개 커밋을 쌓되, rev 20이 wrap 버그(`% N` → `& N`)를 넣고, rev 24는 빌드를 깨뜨리고 rev 25가 고친다. 판정 스크립트는 빌드 실패면 125, 테스트 실패면 1을 반환한다.

```sh
#!/bin/bash
# P5 lab3: 32 commits of a tiny ring-buffer "driver"; #20 introduces a wrap bug, #24 breaks the build (#25 fixes it)
set -e
R=/var/tmp/P5/lab3; rm -rf $R; mkdir -p $R; cd $R; git init -q -b main .
export GIT_AUTHOR_NAME=p5 GIT_AUTHOR_EMAIL=p5@lab GIT_COMMITTER_NAME=p5 GIT_COMMITTER_EMAIL=p5@lab
cat > ring.c <<'X'
#include <stdio.h>
#define N 8
static int buf[N]; static unsigned head, tail;
static void put(int v) { buf[head] = v; head = (head + 1) % N; }
static int get(void) { int v = buf[tail]; tail = (tail + 1) % N; return v; }
int main(void)
{
	int bad = 0;
	for (int i = 0; i < 100; i++) { put(i); if (get() != i) bad++; }
	printf("bad=%d\n", bad);
	return bad != 0;
}
X
for i in $(seq 1 32); do
	D="2026-02-01T00:$(printf %02d $i):00Z"; export GIT_AUTHOR_DATE=$D GIT_COMMITTER_DATE=$D
	echo "/* rev $i */" > notes.h; msg="notes: rev $i"
	if [ $i = 24 ]; then sed -i 's/int bad = 0;/int bad = 0/' ring.c; msg="ring: refactor counters (breaks build)"; fi
	if [ $i = 25 ]; then sed -i 's/int bad = 0$/int bad = 0;/' ring.c; msg="ring: fix build"; fi
	if [ $i = 20 ]; then sed -i 's/head = (head + 1) % N;/head = (head + 1) \& N;/' ring.c; msg="ring: use mask instead of modulo"; fi
	git add -A; git commit -qm "$msg"
	[ $i = 1 ] && git tag v1-good
done
echo "HEAD=$(git log --oneline -1)"
```

```sh
#!/bin/sh
# bisect_test.sh — git bisect run script: 0 = good, 1..124 = bad, 125 = cannot test (skip)
gcc -std=gnu11 -Wall -Wextra -O2 ring.c -o /tmp/p5_ring 2>/dev/null || exit 125
timeout 5 /tmp/p5_ring
```

```sh
limactl shell elinux -- bash -lc 'bash /Users/donh/.elinux_scratch/P5/git/lab3_setup.sh; cd /var/tmp/P5/lab3
git bisect start HEAD v1-good; time git bisect run /Users/donh/.elinux_scratch/P5/git/bisect_test.sh
echo "--- log"; git bisect log | grep -v "^git"; git bisect reset >/dev/null 2>&1'
```

```text
HEAD=ee3b82d notes: rev 32
Bisecting: 15 revisions left to test after this (roughly 4 steps)
[7ad9839c665c6cdfe669d1a987a8af54fb9ba0d4] notes: rev 16
running '/Users/donh/.elinux_scratch/P5/git/bisect_test.sh'
bad=0
Bisecting: 7 revisions left to test after this (roughly 3 steps)
[9bdeed37686f22109dd2bf4fb89b398ad529085f] ring: refactor counters (breaks build)
running '/Users/donh/.elinux_scratch/P5/git/bisect_test.sh'
Bisecting: 7 revisions left to test after this (roughly 3 steps)
[88bdd4e8d6e57bce4d0193c004097813227a3d83] notes: rev 19
running '/Users/donh/.elinux_scratch/P5/git/bisect_test.sh'
bad=0
Bisecting: 5 revisions left to test after this (roughly 3 steps)
[7a2c978cfd2806c06fc68773aa19636fefcca373] notes: rev 26
running '/Users/donh/.elinux_scratch/P5/git/bisect_test.sh'
bad=87
Bisecting: 2 revisions left to test after this (roughly 2 steps)
[15b5713c402accb6b284a3a80d0739c3083ef04d] notes: rev 23
running '/Users/donh/.elinux_scratch/P5/git/bisect_test.sh'
bad=87
Bisecting: 1 revision left to test after this (roughly 1 step)
[8f87af78f150333663d2c9d492602c73be4837bf] notes: rev 21
running '/Users/donh/.elinux_scratch/P5/git/bisect_test.sh'
bad=87
Bisecting: 0 revisions left to test after this (roughly 0 steps)
[4aed3d83c9ac996b0d13e1146a73618a6ec681d2] ring: use mask instead of modulo
running '/Users/donh/.elinux_scratch/P5/git/bisect_test.sh'
bad=87
4aed3d83c9ac996b0d13e1146a73618a6ec681d2 is the first bad commit
commit 4aed3d83c9ac996b0d13e1146a73618a6ec681d2
Author: p5 <p5@lab>
Date:   Sun Feb 1 00:20:00 2026 +0000

    ring: use mask instead of modulo

 notes.h | 2 +-
 ring.c  | 2 +-
 2 files changed, 2 insertions(+), 2 deletions(-)
bisect found first bad commit

real	0m2.241s
user	0m0.300s
sys	0m0.212s
--- log
# bad: [ee3b82d3563b951d46289b2650e7d4ae58c31d82] notes: rev 32
# good: [0bd8243aaafa8bf161ae7af5f3872e9094ec4997] notes: rev 1
# good: [7ad9839c665c6cdfe669d1a987a8af54fb9ba0d4] notes: rev 16
# skip: [9bdeed37686f22109dd2bf4fb89b398ad529085f] ring: refactor counters (breaks build)
# good: [88bdd4e8d6e57bce4d0193c004097813227a3d83] notes: rev 19
# bad: [7a2c978cfd2806c06fc68773aa19636fefcca373] notes: rev 26
# bad: [15b5713c402accb6b284a3a80d0739c3083ef04d] notes: rev 23
# bad: [8f87af78f150333663d2c9d492602c73be4837bf] notes: rev 21
# bad: [4aed3d83c9ac996b0d13e1146a73618a6ec681d2] ring: use mask instead of modulo
# first bad commit: [4aed3d83c9ac996b0d13e1146a73618a6ec681d2] ring: use mask instead of modulo
```

출력에서 볼 것:

- 7번 테스트(그중 1번 skip)로 31개 후보 중 범인을 찾았다. rev 24는 빌드가 깨져 125 → `# skip:`으로 기록됐고, git은 다른 커밋(rev 19, rev 26)을 대신 골랐다. **빌드 깨진 커밋을 bad로 판정하면 엉뚱한 커밋을 범인으로 지목한다** — 125가 그래서 있다.
- `bad=87`: 100개 중 87개가 틀림 — `(head + 1) & 8`은 0 또는 8이라 head가 버퍼 밖(`buf[8]`)을 쓴다. 모듈러를 마스크로 바꿀 때는 `& (N - 1)`이고 N이 2의 거듭제곱이어야 한다. Don에게 익숙한 펌웨어 링버퍼 버그다.
- `git bisect log`는 재현 기록이다. 버그 리포트·upstream 메일에 그대로 붙인다(`Documentation/admin-guide/bug-bisect.rst`도 log 저장을 권한다).
- 2.2초(공유 VM, 컴파일 7번 포함) — 진짜 커널 bisect는 단계마다 빌드 + 부팅이라 한 단계 수 분~수십 분이다. 그래서 스크립트를 미리 견고하게 짜는 것이 핵심이다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <rect x="152" y="68" width="304" height="24" fill="#3f9a6b" fill-opacity="0.12" stroke="none"/>
  <rect x="456" y="68" width="208" height="24" fill="#d0564a" fill-opacity="0.12" stroke="none"/>
  <line x1="160" y1="80" x2="656" y2="80" stroke="currentColor"/>
  <text x="10" y="84" font-size="13">rev 1 → 32</text>
  <text x="300" y="108" text-anchor="middle" font-size="12">good (rev 1~19)</text>
  <text x="560" y="108" text-anchor="middle" font-size="12">bad (rev 20~32)</text>
  <circle cx="400" cy="80" r="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="400" y="62" text-anchor="middle" font-size="12">1</text>
  <circle cx="528" cy="80" r="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="528" y="62" text-anchor="middle" font-size="12">2</text>
  <circle cx="448" cy="80" r="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="448" y="62" text-anchor="middle" font-size="12">3</text>
  <circle cx="560" cy="80" r="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="560" y="62" text-anchor="middle" font-size="12">4</text>
  <circle cx="512" cy="80" r="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="512" y="62" text-anchor="middle" font-size="12">5</text>
  <circle cx="480" cy="80" r="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="480" y="62" text-anchor="middle" font-size="12">6</text>
  <circle cx="464" cy="80" r="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="464" y="62" text-anchor="middle" font-size="12">7</text>
  <text x="464" y="40" text-anchor="middle" font-size="12">첫 bad = rev 20</text>
  <line x1="464" y1="44" x2="464" y2="54" stroke="currentColor"/>
  <g font-size="12">
    <text x="10" y="149">시작</text><rect x="160" y="140" width="496" height="10" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="10" y="167">1: rev16 good</text><rect x="400" y="158" width="256" height="10" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="10" y="185">2: rev24 skip</text><rect x="400" y="176" width="256" height="10" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="10" y="203">3: rev19 good</text><rect x="448" y="194" width="208" height="10" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="10" y="221">4: rev26 bad</text><rect x="448" y="212" width="112" height="10" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="10" y="239">5: rev23 bad</text><rect x="448" y="230" width="64" height="10" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="10" y="257">6: rev21 bad</text><rect x="448" y="248" width="32" height="10" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="10" y="275">7: rev20 bad</text><rect x="448" y="266" width="16" height="10" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  </g>
  <text x="10" y="306" font-size="12">막대 = 남은 후보 구간 [마지막 good, 처음 bad]. skip(2)은 구간을 줄이지 못하고 한 단계를 쓴다.</text>
</svg>
```

그림 5 — 예제 10의 실제 bisect 경로. 위 줄의 숫자는 테스트 순서, 아래 막대는 각 단계 뒤 남은 구간이다. 7단계 뒤 구간이 rev 19(good)와 rev 20(bad) 사이로 좁혀져 rev 20이 범인이다.

### 9.3 커널에서의 bisect run 스크립트 (가이드, 실행 안 함)

랩 커널 소스는 git 저장소가 아니어서 실제 커널 bisect는 돌리지 않았다. 아래는 QEMU 부팅 + 모듈 테스트를 판정으로 쓰는 형태다. 핵심은 **빌드 실패 = 125, 부팅 실패(타임아웃) = bad인지 skip인지 미리 정책을 정하는 것**이다.

```sh
#!/bin/sh
# kbisect.sh — guide only. usage: git bisect run ./kbisect.sh   (run at the top of a kernel git tree)
O=/var/tmp/P5/kb; mkdir -p $O
cp /path/to/product.config $O/.config && make O=$O olddefconfig >/dev/null || exit 125
make O=$O -j2 Image modules >/dev/null 2>&1 || exit 125          # 빌드 실패 → skip
make -C $O M=$PWD/../p5_driver modules >/dev/null 2>&1 || exit 125
# initramfs에 모듈 + 테스트 스크립트를 넣는다 (예제 8과 같은 방식)
timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel $O/arch/arm64/boot/Image -initrd test_initrd.gz \
  -append "console=ttyAMA0 LAB_CMD=/p5test.sh" > $O/console.log 2>&1
rc=$?; [ $rc = 124 ] && exit 1                                     # 부팅 hang → bad (정책)
grep -q "P5_TEST_PASS" $O/console.log && exit 0 || exit 1
```

실무 요령:

- **두 LTS 사이를 bisect할 때 good/bad 지점은 mainline 태그**(`v5.15`, `v6.18`)로 잡는다. stable 태그(`v5.15.180`)는 mainline의 조상이 아니라 옆 가지라서 bisect가 "merge base가 good인지" 묻고 헤맨다.
- 우리 패치 스택이 있어야 재현되는 버그면, 각 단계에서 스택을 붙여야 한다 → 판정 스크립트 안에서 `git cherry-pick`을 하거나, 우리 드라이버를 out-of-tree로 빌드한다(위 스크립트).
- 단계가 비싸면 먼저 **대략 범위**를 좁힌다: mainline 릴리스 태그(6.0, 6.1, …)만 먼저 테스트 → 범인 릴리스를 찾고 → 그 안에서 bisect. `git bisect skip`이 많으면 `git bisect visualize`로 남은 후보를 보고 사람이 판단한다.

---

## 10. stable backport — 받는 쪽과 보내는 쪽

### 10.1 태그 복습 (E7 §5.11과 트리 문서)

- `Fixes: <12자 sha> ("<제목>")` — 이 수정이 고치는 커밋. stable 팀과 AUTOSEL 도구가 "어느 stable에 넣을지"를 이걸로 계산한다.
- `Cc: stable@vger.kernel.org` — Option 1. mainline에 머지되면 자동으로 stable 후보가 된다. `# 6.1.x` 같은 주석으로 버전 범위, `# a1f84a3: ...`로 선행 패치를 지정할 수 있다(1.1절 grep 출력의 문서 위치 근처, stable-kernel-rules.rst).
- stable 트리에 들어간 커밋은 본문 첫 줄에 `commit <40자 sha> upstream.` 또는 `[ Upstream commit <sha> ]`가 붙는다.
- `Stable-dep-of:` — "이 커밋은 그 자체는 수정이 아니지만, 저 수정을 붙이기 위한 선행 패치로 같이 들어왔다".

### 10.2 예제 11 — 실제 stable 릴리스 한 개를 분류하기 (6.18.56)

무엇을 확인하나: kernel.org에서 받은 `ChangeLog-6.18.56`에서 커밋 수와 태그 분포를 세고, 센서 키트가 쓰는 서브시스템(iio, gpio, i2c, media, can)의 수정만 골라낸다. "우리 제품에 이번 stable이 무엇을 주나"를 판단하는 작업이다.

```python
#!/usr/bin/env python3
# P5: triage one stable release — which fixes touch "our" subsystems, and what tags they carry
import re, sys
cl = open(sys.argv[1], encoding="utf-8", errors="replace").read()
commits = re.split(r"\n(?=commit [0-9a-f]{40}\n)", cl)[1:]
print("commits in ChangeLog:", len(commits))
tags = {"upstream sha line": r"\[ Upstream commit [0-9a-f]{40} \]|commit [0-9a-f]{40} upstream\.",
        "Fixes:": r"\n\s+Fixes: [0-9a-f]{12}", "Cc: stable": r"\n\s+Cc: <?stable@", "Stable-dep-of:": r"Stable-dep-of:",
        "Signed-off-by: Sasha Levin": r"Signed-off-by: Sasha Levin", "Signed-off-by: Greg Kroah-Hartman": r"Signed-off-by: Greg Kroah-Hartman"}
for k, rx in tags.items():
    print(f"  {k:36s} {sum(1 for c in commits if re.search(rx, c)):4d}")
print("fixes in our subsystems (by subject prefix):")
pre = ("iio:", "gpio:", "gpiolib", "i2c:", "media:", "can:")
for c in commits:
    subj = c.split("\n")[4].strip() if len(c.split("\n")) > 4 else ""
    if subj.lower().startswith(pre):
        fx = re.search(r"Fixes: ([0-9a-f]{12})", c)
        st = "Cc:stable" if re.search(r"Cc: <?stable@", c) else "-"
        print(f"  {subj[:66]:66s} {('Fixes:' + fx.group(1)) if fx else 'no-Fixes':20s} {st}")
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/P5/dl && cd /var/tmp/P5/dl
curl -s --max-time 60 -O https://cdn.kernel.org/pub/linux/kernel/v6.x/incr/patch-6.18.55-56.xz \
                      -O https://cdn.kernel.org/pub/linux/kernel/v6.x/ChangeLog-6.18.56
python3 -I /Users/donh/.elinux_scratch/P5/stable_triage.py ChangeLog-6.18.56'
```

```text
commits in ChangeLog: 402
  upstream sha line                     400
  Fixes:                                346
  Cc: stable                            229
  Stable-dep-of:                         33
  Signed-off-by: Sasha Levin            166
  Signed-off-by: Greg Kroah-Hartman     281
fixes in our subsystems (by subject prefix):
  iio: adc: adi-axi-adc: Initialize state mutex                      Fixes:7ecb8ee5c93b   -
  iio: adc: adi-axi-adc: Make use of dev_err_probe()                 no-Fixes             -
  iio: buffer-dmaengine: fix sg entry iteration when building dma_ve Fixes:7a86d469983a   Cc:stable
  iio: adc: ad4030: fix invalid oversampling_ratio validation        Fixes:949abd1ca5a4   Cc:stable
  iio: adc: ad4030: Use BIT macro to improve code readability        no-Fixes             -
  iio: adc: aspeed: propagate reset deassert errors                  Fixes:edf7550a1f93   Cc:stable
  iio: adc: aspeed: Simplify with dev_err_probe                      no-Fixes             -
  iio: trigger: cancel reenable_work before freeing trigger          Fixes:9020ef659885   Cc:stable
  iio: proximity: vl53l0x-i2c: claim direct mode for raw reads       Fixes:762186c6e7b1   Cc:stable
  ...
  iio: pressure: bmp280: fix out-of-bounds access in sampling freque Fixes:10b40ffba2f9   Cc:stable
  ...
  iio: imu: adis16480: fix unprotected debugfs reads                 Fixes:9bf94f836e32   Cc:stable
  ...
  i2c: xiic: don't clobber msg->len to signal block-read completion  Fixes:e4c1ff772e1a   Cc:stable
  i2c: xiic: preserve PEC byte length in SMBus block read setup      Fixes:e4c1ff772e1a   Cc:stable
  i2c: at91: release DMA channels when probe defers                  Fixes:f7eeb1af8537   -
  ...
```

(실제 목록은 54줄이다. 여기에는 처음 9줄과 센서 키트에 관련된 줄 몇 개만 남기고 `...`로 줄였다.)

출력에서 볼 것:

- 6.18.55 다음 stable 한 번에 **402개**, 그중 400개가 upstream sha를 달고 있다 = stable은 새 코드가 아니라 **mainline 커밋의 복사본**이다. 346개(86%)가 `Fixes:`를 갖는다.
- `Cc: stable`은 229개뿐이다. 나머지는 작성자가 stable을 요청하지 않았는데 `Fixes:` 태그나 AUTOSEL(기계학습 기반 자동 선별, 기억 기반)로 stable 팀이 고른 것이다 — Sasha Levin 서명 166개가 대략 그 흐름이다. 그래서 **우리가 upstream에 보내는 수정에도 `Fixes:`를 꼭 단다**(그래야 다른 사람의 stable에도 들어간다).
- `no-Fixes` 커밋(`Make use of dev_err_probe()`, `Use BIT macro …`)은 수정이 아닌데 들어왔다. 아래처럼 `Stable-dep-of:`가 붙어 있다 — 진짜 수정이 붙으려면 문맥이 필요해서 같이 들어온 **선행 패치**다. 우리가 backport할 때도 똑같이 "선행 패치를 같이 가져오느냐, 수정을 고쳐서 붙이느냐"를 결정해야 한다(backporting.rst의 "Prerequisite vs. incidental patches").

```text
    iio: adc: adi-axi-adc: Make use of dev_err_probe()
    
    [ Upstream commit 634a6316617e2ced7016b002d0b284a1502e9601 ]
    ...
    Signed-off-by: Nuno Sá <nuno.sa@analog.com>
    Signed-off-by: Jonathan Cameron <Jonathan.Cameron@huawei.com>
    Stable-dep-of: 549b71ce0506 ("iio: adc: adi-axi-adc: Initialize state mutex")
    Signed-off-by: Sasha Levin <sashal@kernel.org>
    Signed-off-by: Greg Kroah-Hartman <gregkh@linuxfoundation.org>
```

(ChangeLog에서 해당 커밋 본문만 잘라 낸 것이고, 가운데 설명 문단은 `...`로 줄였다.)

### 10.3 예제 12 — stable incremental patch를 트리 일부에 적용하기

무엇을 확인하나: BMP280(센서 키트의 기압계, dom 커널에 `BMP280=m`)의 수정이 우리 트리에 붙는지 확인한다. 전체 트리 대신 `drivers/iio/pressure`만 복사한 작은 저장소에 `git apply --include`로 그 경로의 hunk만 적용한다. 벤더 트리에 stable을 "통째로 머지"할 수 없을 때 서브시스템 단위로 가져오는 방식이다.

```sh
limactl shell elinux -- bash -lc 'S=/var/tmp/P5/stable; rm -rf $S; mkdir -p $S/drivers/iio
cp -r /var/lab/src/linux-6.18.55/drivers/iio/pressure $S/drivers/iio/ && cd $S && git init -q -b v6.18.55-subset .
git add -A && GIT_AUTHOR_NAME=p5 GIT_AUTHOR_EMAIL=p5@lab GIT_COMMITTER_NAME=p5 GIT_COMMITTER_EMAIL=p5@lab \
  git commit -qm "v6.18.55: drivers/iio/pressure only"
P=/var/tmp/P5/dl/patch-6.18.55-56
echo "### files the patch touches here:"; git apply --stat -p1 --include="drivers/iio/pressure/*" $P
echo "### check + apply"; git apply --check -p1 --include="drivers/iio/pressure/*" $P && \
  git apply -p1 --include="drivers/iio/pressure/*" $P && git diff --stat && git diff | sed -n 1,40p'
```

```text
### files the patch touches here:
 drivers/iio/pressure/bmp280-core.c |    2 +-
 drivers/iio/pressure/rohm-bm1390.c |    2 +-
 2 files changed, 2 insertions(+), 2 deletions(-)
### check + apply
 drivers/iio/pressure/bmp280-core.c | 2 +-
 drivers/iio/pressure/rohm-bm1390.c | 2 +-
 2 files changed, 2 insertions(+), 2 deletions(-)
diff --git a/drivers/iio/pressure/bmp280-core.c b/drivers/iio/pressure/bmp280-core.c
index 0ebe0b6..c7b90df 100644
--- a/drivers/iio/pressure/bmp280-core.c
+++ b/drivers/iio/pressure/bmp280-core.c
@@ -836,7 +836,7 @@ static int bmp280_write_sampling_frequency(struct bmp280_data *data,
 					   int val, int val2)
 {
 	const int (*avail)[2] = data->chip_info->sampling_freq_avail;
-	const int n = data->chip_info->num_sampling_freq_avail;
+	const int n = data->chip_info->num_sampling_freq_avail / 2;
 	int ret, prev;
 	int i;
 
diff --git a/drivers/iio/pressure/rohm-bm1390.c b/drivers/iio/pressure/rohm-bm1390.c
index dac27fd..3b48c1c 100644
--- a/drivers/iio/pressure/rohm-bm1390.c
+++ b/drivers/iio/pressure/rohm-bm1390.c
@@ -289,7 +289,7 @@ static int bm1390_read_data(struct bm1390_data *data,
 	if (warn)
 		dev_warn(data->dev, "Failed to stop measurement (%d)\n", warn);
 
-	return 0;
+	return ret;
 }
 
 static int bm1390_read_raw(struct iio_dev *idev,
```

출력에서 볼 것: BMP280 수정은 **한 글자(`/ 2`)**다. ChangeLog 설명대로 `sampling_freq_avail`은 `[정수부, 소수부]` 쌍의 배열인데 개수를 "평탄화한 원소 수"로 세어 2배만큼 배열 밖을 읽었다(out-of-bounds). 이런 수정이 매주 수백 개 들어온다. 우리 트리가 6.18.y를 따라가면 공짜로 받고, EOL된 트리라면 이걸 우리가 찾아서 붙여야 한다. 실제 운영에서는 이 diff 대신 **mainline 커밋을 `git cherry-pick -x`** 하는 것이 원칙이다(backporting.rst: "cherry-pick이 `git am`보다 낫다 — 코드가 움직인 것을 git이 안다").

### 10.4 보내는 쪽 — 우리가 고친 버그를 stable로 (요약)

1. mainline 기준으로 고친다(우리 트리가 6.6이어도). 
2. 커밋에 `Fixes:`(버그를 넣은 커밋)와 `Cc: stable@vger.kernel.org`를 단다(Option 1).
3. 옛 stable에서 그대로 안 붙는다는 메일(FAILED patch)이 오면, 그 트리에 맞게 고친 버전을 `commit <sha> upstream.` 형식으로 보낸다(Option 3, E7 §5.11).
4. 우리 제품 트리에는 mainline 커밋을 `cherry-pick -x`로 미리 넣고, 다음 stable 머지 때 patch-id로 자연히 합쳐지게 한다(예제 3의 "skipped previously applied commit").

---

## 11. 커널 안에서는 무슨 일이 일어나나

업그레이드에서 만나는 에러 두 종류 — "빌드가 안 된다"(6절)와 "빌드는 되는데 로드가 안 된다"(예제 8) — 는 커널 안의 서로 다른 지점에서 생긴다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="140" y="18" text-anchor="middle" font-size="13">모듈 로드 (kernel/module/)</text>
  <text x="530" y="18" text-anchor="middle" font-size="13">driver core (drivers/base/)</text>
  <rect x="20" y="30" width="260" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="150" y="55" text-anchor="middle" font-size="12">insmod → (f)init_module()</text>
  <rect x="20" y="90" width="260" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="115" text-anchor="middle" font-size="12">load_module()</text>
  <rect x="20" y="150" width="260" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="175" text-anchor="middle" font-size="12">check_modinfo(): vermagic 비교</text>
  <rect x="290" y="150" width="84" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="332" y="175" text-anchor="middle" font-size="12">-ENOEXEC</text>
  <rect x="20" y="210" width="260" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="150" y="235" text-anchor="middle" font-size="12">simplify_symbols(): 심볼 해석 · CRC</text>
  <rect x="20" y="270" width="260" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="295" text-anchor="middle" font-size="12">do_init_module() → p5_init()</text>
  <rect x="390" y="30" width="280" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="530" y="55" text-anchor="middle" font-size="12">platform_driver_register(&amp;p5_drv)</text>
  <rect x="390" y="90" width="280" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="530" y="115" text-anchor="middle" font-size="12">driver_register() → 버스 match</text>
  <rect x="390" y="150" width="280" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="530" y="175" text-anchor="middle" font-size="12">platform_probe() → drv-&gt;probe(pdev)</text>
  <rect x="390" y="210" width="280" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="530" y="235" text-anchor="middle" font-size="12">rmmod → ... → platform_remove()</text>
  <rect x="390" y="270" width="280" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="530" y="295" text-anchor="middle" font-size="12">drv-&gt;remove(dev): 반환값 없음 (void)</text>
  <line x1="150" y1="70" x2="150" y2="88" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="150" y1="130" x2="150" y2="148" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="150" y1="190" x2="150" y2="208" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="150" y1="250" x2="150" y2="268" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="280" y1="170" x2="288" y2="170" stroke="#d0564a" marker-end="url(#ah6)"/>
  <path d="M280 290 L335 290 L335 50 L388 50" fill="none" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="530" y1="70" x2="530" y2="88" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="530" y1="130" x2="530" y2="148" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="530" y1="190" x2="530" y2="208" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah6)"/>
  <line x1="530" y1="250" x2="530" y2="268" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="20" y="332" font-size="12">빌드 에러(6절)는 이 그림 이전, 헤더의 구조체·함수 선언에서 난다. 로드 에러는 왼쪽 열에서 난다.</text>
</svg>
```

그림 6 — 모듈 로드 경로(왼쪽)와 driver core가 우리 콜백을 부르는 경로(오른쪽). 함수 이름은 6.18.55 `kernel/module/main.c`, `drivers/base/platform.c`에서 grep으로 확인했다.

- **API 변화의 정체는 "콜백 테이블의 타입"이다.** `struct platform_driver`는 driver core가 부르는 함수 포인터 테이블이다(G3). 6.18.55의 `drivers/base/platform.c`:

```c
static void platform_remove(struct device *_dev)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);

	if (drv->remove)
		drv->remove(dev);
}
```

  core가 반환값을 안 쓰므로 타입을 `void`로 바꿨고, 그 순간 모든 드라이버의 `.remove = fn` 대입이 타입 에러가 된다. bare-metal로 치면 **벡터 테이블 엔트리의 함수 타입이 바뀐 것**이다. i2c도 같다: `drivers/i2c/i2c-core-base.c`의 `i2c_device_probe()`가 `driver->probe(client)`(593행)를 부른다.

- **로드 시 검사는 `check_modinfo()`의 vermagic 비교가 첫 관문이다.** 6.18.55 `kernel/module/main.c`의 `check_modinfo()`는 모듈의 `vermagic` 문자열이 커널의 `VERMAGIC_STRING`과 다르면 `"version magic '%s' should be '%s'"`를 찍고 `-ENOEXEC`를 반환한다 — 예제 8의 출력 그대로다. vermagic에는 커널 릴리스 문자열(LOCALVERSION 포함)과 `SMP preempt mod_unload` 같은 핵심 config 플래그가 들어간다.
- **dom 커널은 `CONFIG_MODVERSIONS`가 꺼져 있다**(`# CONFIG_MODVERSIONS is not set`, `linux-build-dom/.config` 950행). MODVERSIONS가 켜져 있으면 심볼마다 CRC(함수 시그니처·구조체 레이아웃의 해시)를 비교해 `"disagrees about version of symbol"`(`kernel/module/version.c`)로 거부한다. 꺼져 있으면 **vermagic만 맞추면 구조체 레이아웃이 달라도 로드된다** — 그래서 "같은 버전 문자열인데 config가 다른 커널"에 모듈을 억지로 올리면(예: `modprobe --force`) 메모리 깨짐으로 이어진다. 업그레이드 원칙: **out-of-tree 모듈은 정확히 그 커널의 빌드 트리로 다시 빌드한다.** Android GKI는 반대로 KMI(커널-모듈 인터페이스)를 CRC로 고정해 이 문제를 제도로 막는다(E7 §2.4, 기억 기반).
- 서명: dom은 `CONFIG_MODULE_SIG=y`, `MODULE_SIG_FORCE` 꺼짐 → 서명 없는 모듈은 로드되고 `E` taint만 붙는다(예제 8). secure boot 제품(L5)에서는 FORCE를 켜므로, 업그레이드 때 **모듈 서명 키와 빌드 파이프라인도 같이 옮겨야** 한다.

드라이버 작성 때 다시 만나는 곳: G3(platform 수명주기 — probe/remove 대칭), H3(I2C `probe(client)`와 `id_table`/`of_match_table`), D4·H(gpiod), F(타이머 API), E3(Kbuild·vermagic·modinfo).

---

## 12. Embedded 실무에서 다시 보기

### 12.1 센서 키트 BSP를 5.15 → 6.18로 (작업 계획 예)

"i.MX8 기반 센서 키트, 벤더 BSP 5.15 + 우리 패치 120개, out-of-tree 드라이버 4개(IMU, 카메라 serializer, CAN 보조 MCU, 전원 관리)"라면:

1. **1주차 — 결정과 인벤토리.** 벤더에 6.18 BSP가 있는지, 없으면 mainline 6.18의 해당 SoC 지원 범위(DT·드라이버)를 확인(E7 예제 7 방식). 우리 120개를 예제 2 스크립트로 스프레드시트화: 제목 접두어, 파일, `git cherry` 결과, upstream sha trailer.
2. **2주차 — 분류 회의.** upstreamed/obsolete는 근거와 함께 drop 목록으로, needs port는 담당자 배정. keep 중 upstream 가능한 것은 P7 백로그로.
3. **3~4주차 — 옮기기와 포팅.** `rebase -i --onto` + drop 목록, rerere 켜기. out-of-tree 드라이버 4개는 예제 6처럼 에러 목록 → 대응표 → 포팅. 가능하면 이 기회에 in-tree로 넣는다(다음 업그레이드에서 API 변경을 남이 고쳐 준다).
4. **5~6주차 — 회귀.** 8절 표의 1~7층. 부팅 스냅샷 diff, 센서별 데이터 품질(Q7), CAN 부하, 카메라 프레임 드롭, 전력(N5), RT 지연(N3).
5. **이후 — 유지.** 6.18.y stable을 2~4주 주기로 머지(예제 11·12의 triage를 매번), 회귀가 나오면 bisect run.

### 12.2 out-of-tree 드라이버를 여러 커널에서 동시에 빌드해야 할 때

필드에 5.15 장비와 6.18 장비가 섞여 있으면 한 소스가 두 커널을 지원해야 한다. 흔한 방법은 `LINUX_VERSION_CODE` 분기다.

```c
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void p5_remove(struct platform_device *pdev)
#else
static int p5_remove(struct platform_device *pdev)
#endif
{
	timer_delete_sync(&p5.timer);	/* 5.15에는 timer_delete_sync가 없을 수 있다 → compat 헤더 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 11, 0)
	return 0;
#endif
}
```

(위 코드는 문법 예시이고 빌드하지 않았다. 버전 경계 6.11은 기억 기반이라 실제로는 해당 API가 들어온 커밋으로 확인해야 한다.) 분기가 수십 개가 되면 `p5_compat.h` 한 곳에 모으고(`#define timer_delete_sync del_timer_sync` 같은 shim), 가능하면 **벤더 stable 백포트가 버전 숫자와 API를 어긋나게 만드는 경우**(벤더가 새 API를 옛 버전에 backport)를 대비해 버전 대신 **기능 검사**(Kconfig 심볼, 헤더의 매크로 존재)를 쓴다. 장기적으로는 in-tree로 넣는 것이 가장 싸다.

### 12.3 "업그레이드 후 IMU가 가끔 0을 읽는다" — bisect 적용

증상은 새 커널에서만, 재현율 30%. 판정 스크립트는 "부팅 후 IMU를 1000번 읽어 0이 하나라도 있으면 bad"로 만들고, 재현율이 낮으므로 **한 단계에서 10회 반복**(1회라도 bad면 bad)한다. 범위는 우리 스택을 제외한 mainline 태그 사이로 잡고, 우리 드라이버는 out-of-tree 빌드로 매 단계 붙인다(9.3절 스크립트). 범인이 I2C 컨트롤러 드라이버의 clock stretching 처리 변경 같은 커밋으로 나오면, 그 커밋의 `Fixes:`가 달린 후속 수정이 이미 stable에 있는지(예제 11 방식)부터 본다. Don의 장점: 버스 장애 root cause 경험이 있으니, bisect로 커밋을 찾은 뒤 로직 분석기로 I2C 파형을 비교(M11)해 **왜**를 증명할 수 있다.

### 12.4 벤더가 BSP를 안 올려 줄 때

벤더가 5.15에서 멈췄다면 선택지는 (1) 벤더 5.15 + 우리가 stable/보안 수정 backport(EOL 후에는 CIP 5.10/6.1 같은 트리를 참고하거나 상용 지원), (2) mainline 6.18로 가고 벤더 드라이버를 우리가 포팅, (3) 다음 하드웨어 리비전에서 SoC 변경. 면접에서는 "비용·위험·제품 수명"을 기준으로 비교하는 것을 보여 주면 된다(E7 §3, P2).

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `git rebase <새 base>`만 사용 | 벤더 커밋 수천 개가 충돌 | 옛 base 경계를 안 줌 | `git rebase --onto <새> <옛> <브랜치>` |
| `--onto`에서 upstream된 패치가 자동으로 빠질 거라 믿음 | 이미 있는 수정이 충돌·중복 | 거르기 비교 대상이 `<upstream>`(옛 base) | `git cherry`로 `-`를 찾아 todo에서 drop |
| 충돌을 "한쪽 채택"으로 해결 | 기능 사라짐 또는 새 API 무시 | 두 의도 중 하나만 살림 | zdiff3로 조상 칸 보고 둘 다 살림, range-diff 리뷰 |
| 충돌 없으면 끝이라고 생각 | 런타임 이중 해제·락 위반 | 의미 충돌(5.3) | 빌드 + 부팅 + lockdep/KASAN + 기능 테스트 |
| 옛 .ko를 rootfs에 그대로 | `invalid module format`, 장치 미생성 | vermagic 불일치 | 모든 out-of-tree 모듈을 새 빌드 트리로 재빌드, 패키지 버전 묶기 |
| `make olddefconfig` 후 config 확인 안 함 | 기능이 조용히 꺼짐 | 심볼 이름·의존성 변경 | `scripts/diffconfig`의 `-` 줄 전수 확인, fragment + merge_config.sh |
| bisect 스크립트가 빌드 실패에 1 반환 | 엉뚱한 커밋이 범인 | 테스트 불가를 bad로 판정 | 빌드 실패·환경 오류는 `exit 125` |
| stable 태그로 bisect 범위 지정 | bisect가 merge base부터 헤맴 | stable 태그는 mainline 조상이 아님 | mainline 태그(`v5.15`, `v6.18`) 사이로 |
| backport에 출처 안 남김 | 다음 업그레이드 때 upstreamed 판정 불가 | `-x` 미사용 | `cherry-pick -x`, `commit <sha> upstream.` |
| 내부 API 변경을 "버그"로 문의 | upstream에서 무시당함 | 내부 API는 안정성 약속 없음 | stable-api-nonsense.rst — in-tree로 넣거나 직접 포팅 |

---

## 14. 면접에서 이렇게 말한다

**Q.** How would you plan a kernel upgrade from a vendor 5.15 BSP to 6.18 LTS?

**A.** 먼저 목표 LTS의 수명과 SoC의 upstream 지원을 확인해 버전을 정한다. 다음으로 우리 패치 스택을 인벤토리해 upstreamed·obsolete·needs port·keep으로 분류하고, `rebase --onto`로 우리 범위만 새 base에 옮긴다. out-of-tree 드라이버는 컴파일 에러를 할 일 목록 삼아 포팅하고, 빌드·config diff·부팅·장치 probe·기능·성능 순서로 회귀를 막는다. 끝나면 stable을 정기적으로 머지하는 체계를 만든다.

> First I confirm the target: the LTS's projected end of life versus our product lifetime, and how much of our SoC is supported upstream or in the vendor's new BSP. Then I inventory our patch stack and classify every patch as already upstream, obsolete, needs porting, or carried as is — git cherry and the upstream SHAs in our commit messages do half of that automatically. I move only our range with git rebase --onto, port the out-of-tree drivers using the compile errors as a to-do list, and review the result with git range-diff. Validation goes in layers: build with no warnings, a config diff, boot, every device probed, functional tests on the sensors, and performance against the old kernel on the same hardware. After that we merge stable releases on a fixed cadence so the next upgrade is smaller.

**꼬리 질문**: "패치가 수백 개면?" → 분류를 스크립트로 자동화하고(예제 2), 서브시스템별 담당자에게 나누고, 이 기회에 upstream 가능한 것을 보내 스택을 줄인다.

**Q.** What does `git rebase --onto` do and when do you need it?

**A.** `--onto <새 base> <옛 base> <브랜치>`는 "옛 base부터 브랜치 끝까지"만 잘라서 새 base 위에 다시 쌓는다. 벤더 BSP 위에 우리 패치가 있을 때, 벤더가 새 BSP를 따로 만들어 줬다면 벤더 커밋은 옮기면 안 되므로 `--onto`가 필요하다. 주의할 점은 이미 적용된 패치를 거르는 기준이 옛 base라서, upstream에 들어간 우리 패치는 `git cherry`로 찾아 직접 drop해야 한다는 것이다.

> It takes the commits in old-base..branch and replays them on top of a new base. You need it whenever the branch sits on something you don't own — for example our product patches on top of a vendor BSP, when the vendor has already produced their own BSP for the new kernel. A plain rebase onto the new BSP would try to replay the vendor's old commits too. One catch I've seen: with --onto, the automatic skipping of already-applied patches compares against the old base, so I run git cherry against the new base first and drop the patches that are already upstream explicitly.

**Q.** How do you resolve a rebase conflict safely?

**A.** zdiff3 스타일로 공통 조상까지 보고, upstream이 그 줄을 왜 바꿨는지 `git log -L`이나 커밋 메시지로 확인한 뒤, 두 의도를 모두 살려 합친다. 커밋 본문에 어떻게 고쳤는지 한 줄 남기고, range-diff로 "전/후 패치"를 비교해 리뷰받는다. 충돌이 없던 커밋도 의미 충돌이 있을 수 있으니 빌드와 런타임 테스트로 확인한다.

> I turn on the zdiff3 conflict style so I can see the common ancestor, then look up why upstream changed those lines — git log -L on the function, or the commit message. The resolution has to keep both intents: upstream's new API and our feature. I note the adaptation in the commit body, and reviewers look at git range-diff, which shows exactly how our patch changed during the port. And I don't trust a clean rebase either; semantic conflicts like changed locking rules only show up in build and runtime tests.

**Q.** Our driver stopped building on the new kernel with "incompatible pointer type" on `.remove`. What happened?

**A.** 6.x에서 platform driver의 `.remove`가 `int` 반환에서 `void`로 바뀌었다. driver core는 원래 반환값을 쓰지 않았기 때문이다. 함수를 `void`로 바꾸고, 혹시 에러를 반환하며 정리를 건너뛰던 경로가 있었다면 그건 원래부터 누수였으니 끝까지 정리하게 고친다. 비슷하게 i2c `probe`가 인자 하나로, `class_create`가 owner 없이 바뀌었다.

> The platform bus changed the remove callback to return void, because the driver core never used the return value anyway. So I change the function to void, and I check any early-return error paths — if the old code skipped cleanup on error, that was already a leak and now it has to clean up unconditionally. The same kind of change hit I2C probe, which now takes only the client, and class_create, which lost the owner argument. In 6.18 the kernel turns incompatible-pointer-type into an error, which is good: it catches callback ABI mismatches at build time.

**Q.** How do you find which commit introduced a regression between two kernels?

**A.** `git bisect run`으로 자동화한다. 판정 스크립트는 빌드 실패면 125(skip), 테스트 통과면 0, 실패면 1을 반환한다. 범위는 mainline 태그 사이로 잡고, 단계가 비싸면 릴리스 단위로 먼저 좁힌다. 재현율이 낮으면 한 단계에서 여러 번 돌린다. 찾은 뒤에는 그 커밋의 후속 수정이 stable에 이미 있는지 본다.

> I script it with git bisect run: the test script exits 0 for good, 1 for bad, and 125 when the revision can't be tested, like a build failure — otherwise a broken build gets blamed. I bisect between mainline tags, not stable tags, because stable tags aren't ancestors of mainline. If each step is a full build and boot, I first narrow it to a release, then bisect inside it, and for flaky bugs I repeat the test several times per step. Once I have the commit, I check whether a later fix carrying a Fixes: tag for it is already in stable.

**Q.** What are `Fixes:` and `Cc: stable` for?

**A.** `Fixes:`는 이 수정이 고치는 원래 커밋을 가리켜, stable 팀과 도구가 어느 버전에 backport할지 계산하게 한다. `Cc: stable@vger.kernel.org`는 mainline에 들어가면 stable로 자동으로 가져가라는 요청이다. 실제로 6.18.56 stable의 402개 커밋 중 346개가 `Fixes:`를, 229개가 `Cc: stable`을 달고 있었다. 받는 쪽에서는 이 태그로 우리 서브시스템 수정을 골라낸다.

> Fixes: points at the commit that introduced the bug, so the stable team and their tooling can work out which stable trees need the fix. Cc: stable asks for an automatic backport once the patch lands in mainline. When I counted one real stable release, 6.18.56, 346 of its 402 commits had a Fixes: tag but only 229 had Cc: stable — the rest were picked up via Fixes: tags or automated selection. So I always add Fixes:, and on the receiving side I use these tags to triage which fixes matter for our drivers.

**Q.** Why can't we just keep the old kernel forever?

**A.** 커널은 계속 동작하지만, EOL 뒤에는 보안·버그 수정이 더 이상 오지 않는다. stable 하나에 수백 개 수정이 들어오는데 그걸 우리가 직접 골라 붙여야 하고, upstream과 거리가 벌어질수록 다음 업그레이드가 비싸진다. 장수 제품이면 CIP SLTS처럼 10년 유지되는 트리에 맞추거나, 1~2년 주기로 LTS를 옮기는 비용을 계획에 넣는다.

> It keeps running, but after end of life the fixes stop arriving. A single stable release carries a few hundred fixes; on an EOL kernel we would have to find and backport the relevant ones ourselves, and every month the distance to upstream grows, so the eventual upgrade gets more expensive. For a long-lived product I'd either align with a CIP super-long-term kernel, which is maintained for about ten years, or budget an LTS move every one to two years and keep the patch stack small so it stays cheap.

---

## 15. 직접 해보기

1. (개념) 그림 2를 보고, 2026-10에 출시해 7년 필드 지원해야 하는 장비에 6.6·6.12·6.18 중 무엇을 고르겠나? 근거 두 개.
   정답/힌트: kernel.org projected EOL(6.18이 가장 길다)과 CIP 채택(6.12는 CIP 브랜치 있음). 7년이면 kernel.org만으로는 어느 것도 부족 → CIP 6.12 정렬 또는 중간에 LTS 이동 계획.
2. (git) 예제 1 저장소의 `product` 끝에 `drivers/sensor.c`의 `return ch * 10;`을 `return ch * 100;`으로 바꾸는 커밋을 추가하고 예제 4를 다시 돌려라. 무엇으로 분류되고 range-diff는 어떻게 나오나?
   정답/힌트: 바꾼 줄은 upstream이 안 건드려 충돌 없이 붙는다(keep). 그런데 range-diff는 `=`가 아니라 `!`다 — hunk 헤더(`@@ ... int sensor_read(struct sensor_dev *sd, int ch)`)와 문맥 줄(`(void)sd;`)이 바뀌었기 때문이다. `!`라도 둘째 열의 `-`/`+` 줄(우리 변경)이 같으면 의도는 보존된 것이다(직접 돌려 확인함).
3. (git) 예제 4에서 `git config rerere.enabled true`를 켜고, 해결 후 `git reset --hard product-bsp1`로 되돌려 다시 rebase하라. 두 번째에는 무엇이 달라지나?
   정답/힌트: 두 번째에는 충돌 메시지 바로 뒤에 `Resolved 'drivers/imu.c' using previous resolution.`이 나오고 파일에 이미 해결본이 들어가 있다(직접 돌려 확인함). 단 상태는 여전히 `UU`라 `git diff`로 확인한 뒤 `git add` 해야 한다(`rerere.autoUpdate`를 켜면 자동 stage).
4. (코드) 예제 6의 옛 드라이버에 `hrtimer_init()` + `.function =` 초기화를 넣어 에러를 확인하고 `hrtimer_setup()`으로 포팅하라.
   정답/힌트: 6.18.55의 시그니처는 `hrtimer_setup(timer, fn, clock_id, mode)`(예제 5 출력 230행). 콜백 반환 타입은 `enum hrtimer_restart`.
5. (bisect) 예제 10의 판정 스크립트를 `exit 125` 없이(빌드 실패도 1) 바꿔 다시 돌려라. 범인은 무엇으로 나오나?
   정답/힌트: 빌드가 깨진 rev 24가 bad로 판정된다. 이번 경로에서는 rev 24가 진짜 범인(rev 20)보다 뒤라 결과가 같을 수도 있다 — 버그를 rev 26으로 옮기면 rev 24가 범인으로 잘못 나온다.
6. (stable) 예제 11 스크립트를 고쳐 `drivers/net/can/`과 `net/can/`을 건드린 커밋을 **파일 경로 기준**으로 골라라(제목 접두어 대신). 힌트: incremental patch의 `diff --git` 줄과 ChangeLog를 함께 쓰거나, ChangeLog의 각 커밋 본문만으로는 파일 경로가 없다는 점에 주의.
   정답/힌트: ChangeLog에는 파일 목록이 없다 → 진짜로는 `linux-stable` git에서 `git log v6.18.55..v6.18.56 -- net/can drivers/net/can`이 정답(가이드).
7. (관찰) 예제 8 게스트에서 `cat /proc/sys/kernel/tainted` 값을 읽고 비트를 해석하라. dom용 모듈 로드 전후로 어떻게 바뀌나?
   정답/힌트: O(4096) + E(8192) = 12288 근처(다른 taint가 없다면). E7 §6.4의 taint 표.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| LTS (longterm) | 장기 지원 커널 | 1년에 하나쯤 지정, 몇 년 동안 stable 수정을 받음 |
| EOL | End of Life | 그 트리에 더 이상 수정이 나오지 않는 시점 |
| CIP SLTS | Super Long Term Support | 일부 LTS를 약 10년 유지하는 산업용 트리 |
| 패치 스택 | patch stack | base 위에 쌓인 우리(·벤더) 커밋 목록 |
| upstreamed | 이미 upstream에 있음 | 같은 수정이 새 base에 있어 drop 대상 |
| obsolete | 쓸모 없어짐 | 대상 코드·기능이 사라져 drop 대상 |
| patch-id | diff 내용 해시 | 커밋 해시가 달라도 같은 변경을 알아보는 키 |
| `git cherry` | 스택 대조 | 각 커밋이 upstream에 있으면 `-`, 없으면 `+` |
| `rebase --onto` | 범위 지정 rebase | 옛 base..브랜치를 새 base 위로 |
| zdiff3 | 충돌 표시 스타일 | HEAD·공통 조상·우리 쪽을 3칸으로 보여 줌 |
| rerere | reuse recorded resolution | 같은 충돌 해결을 기억해 재사용 |
| range-diff | 시리즈 비교 | 전·후 커밋 시리즈를 짝지어 diff의 diff로 보여 줌 |
| 의미 충돌 | semantic conflict | 텍스트 충돌은 없지만 동작이 깨지는 경우 |
| vermagic | 모듈 버전 문자열 | 커널 릴리스 + 핵심 config, 다르면 로드 거부 |
| MODVERSIONS | 심볼 CRC 검사 | 심볼 시그니처 해시로 ABI 불일치 탐지 |
| Coccinelle (spatch) | semantic patch 도구 | C 구조를 이해하고 대량 변환 |
| `git bisect run` | 자동 이진 탐색 | 스크립트 종료 코드(0/1/125)로 범인 커밋 찾기 |
| `Fixes:` | 원인 커밋 태그 | stable backport 범위 계산의 근거 |
| `Cc: stable` | stable 요청 태그 | mainline 머지 후 자동 backport 요청 |
| `Stable-dep-of:` | 선행 패치 표시 | 수정을 붙이려고 같이 들어온 패치 |
| diffconfig | config 비교 스크립트 | 사라짐(-)·생김(+)·변경 심볼 목록 |

---

## 17. 요약 & 체크리스트

커널 업그레이드는 "제품 = LTS + 벤더 BSP + 우리 패치"에서 맨 아래층을 바꾸고 위층을 다시 올리는 프로젝트다. 언제 올릴지는 LTS의 projected EOL(5.10·5.15는 2026-12)과 제품 수명, CIP SLTS 채택 여부로 정한다. 실행은 인벤토리 → 분류(upstreamed·obsolete·needs port·keep) → `git rebase -i --onto <새> <옛> <브랜치>`(git cherry의 `-`와 삭제된 파일 대상 커밋은 todo에서 drop) → zdiff3로 두 의도를 합치는 충돌 해결 → range-diff 리뷰 순서다. out-of-tree 드라이버는 컴파일 에러 유형(없어진 함수, 바뀐 콜백 타입, 인자 수, 매크로 문법)이 곧 포팅 목록이며, 6.18.55에서 `.remove`는 void, i2c `.probe`는 인자 하나, `class_create(name)`, `timer_container_of`/`timer_delete_sync`, `strscpy`, `MODULE_IMPORT_NS("NS")`다. 새 커널에는 모듈을 그 빌드 트리로 다시 빌드해야 하고(vermagic), config는 diffconfig로 사라진 심볼을 확인한다. 회귀는 빌드·부팅·probe·기능·성능 층으로 막고, 회귀 커밋은 0/1/125 규칙의 `git bisect run`으로 찾는다. 업그레이드 뒤에는 stable을 정기 머지하며 `Fixes:`·`Cc: stable`·`Stable-dep-of:` 태그로 우리 서브시스템 수정을 triage한다.

- [ ] LTS·CIP 수명표를 보고 제품에 맞는 목표 커널을 근거와 함께 고를 수 있다
- [ ] 제품이 옛 커널에 갇히는 이유 다섯 가지와 머무르는 비용을 설명할 수 있다
- [ ] 패치 스택을 인벤토리하고 네 가지로 분류하는 스크립트를 짤 수 있다
- [ ] `git cherry`의 `+`/`-`와 patch-id의 한계를 설명할 수 있다
- [ ] `git rebase --onto`의 세 인자와 plain rebase와의 차이를 그림으로 설명할 수 있다
- [ ] zdiff3 충돌 표시를 읽고 두 의도를 살려 해결할 수 있다
- [ ] range-diff 출력의 `=`, `<`, `>`, `!`와 diff의 diff를 읽을 수 있다
- [ ] 옛 API 드라이버의 컴파일 에러를 유형별로 읽고 6.18 API로 포팅할 수 있다
- [ ] vermagic·MODVERSIONS·모듈 서명이 업그레이드에 미치는 영향을 설명할 수 있다
- [ ] diffconfig로 config 이동에서 사라진 기능을 찾을 수 있다
- [ ] 0/1/125 규칙으로 `git bisect run` 판정 스크립트를 짤 수 있다
- [ ] stable ChangeLog에서 우리 서브시스템 수정과 선행 패치를 골라낼 수 있다

---

## 참고 자료

- 커널 트리 문서 (6.18.55에서 확인): `Documentation/process/2.Process.rst`, `Documentation/process/stable-kernel-rules.rst`, `Documentation/process/backporting.rst`, `Documentation/process/stable-api-nonsense.rst`, `Documentation/admin-guide/bug-bisect.rst`, `Documentation/admin-guide/verify-bugs-and-bisect-regressions.rst`, `Documentation/dev-tools/coccinelle.rst` — 온라인: https://docs.kernel.org/process/backporting.html , https://docs.kernel.org/process/stable-kernel-rules.html
- kernel.org 릴리스 목록: https://www.kernel.org/category/releases.html
- stable 파일: https://cdn.kernel.org/pub/linux/kernel/v6.x/ (`ChangeLog-6.18.56`, `incr/patch-6.18.55-56.xz`)
- CIP: https://www.cip-project.org/ , https://git.kernel.org/pub/scm/linux/kernel/git/cip/linux-cip.git
- git 문서: https://git-scm.com/docs/git-rebase (`--onto`, `--empty`), https://git-scm.com/docs/git-range-diff , https://git-scm.com/docs/git-cherry , https://git-scm.com/docs/git-bisect , https://git-scm.com/docs/git-rerere
- Coccinelle: https://coccinelle.gitlabpages.inria.fr/website/
- 소스 위치 (elixir): https://elixir.bootlin.com/linux/v6.18.55/source/drivers/base/platform.c , https://elixir.bootlin.com/linux/v6.18.55/source/kernel/module/main.c
- 이 노트와 이어지는 노트: E7(릴리스 모델·패치 보내기), P2(BSP·패치 스택), P6(KUnit·kselftest·CI), P7(upstream 기여), G3, H3, M8, K5
