# E7. 커널 버전 · 커뮤니티 · 라이선스 — mainline/stable/LTS, 벤더 커널, 패치 보내기, GPL

> **이 노트를 다 읽으면**: merge window → -rc → 릴리스로 도는 9~10주 사이클과 mainline·stable·longterm·벤더 BSP·Android GKI·CIP SLTS의 관계를 kernel.org 실제 데이터로 설명할 수 있다 · 새 제품에 "벤더 6.6 BSP냐 6.18 LTS냐"를 upstream 지원·backport 부담·보안 수정 기준으로 판단할 수 있다 · `git format-patch -v2 --cover-letter` → `checkpatch.pl --strict` → `get_maintainer.pl` → `send-email --dry-run`으로 이어지는 패치 흐름을 직접 돌리고, Fixes:·Cc: stable·Signed-off-by 태그를 바르게 쓸 수 있다 · GPL-2.0-only, SPDX, `MODULE_LICENSE`, `EXPORT_SYMBOL_GPL`, taint, 제품 출하 때의 소스 공개 의무를 설명할 수 있다
> **왜 필요한가**: 입사 첫 달에 받는 질문이 "이 보드 커널 몇 버전이야? 벤더 BSP 올려야 해?"이고, 면접에서는 "새 제품이면 어떤 커널을 고르겠나?", "upstream에 패치 보내 본 적 있나?", "proprietary 모듈을 커널에 넣어도 되나?"가 나온다. `contents.md`의 실무 시나리오 "벤더 BSP 커널 5.15를 6.x LTS로 올려라"의 출발점이 이 노트다
> **Don 기준 난이도**: SSD 펌웨어의 release train(기능 동결 → RC → 양산 브랜치 → 고객별 핫픽스 브랜치)과 코드 리뷰 경험은 그대로 쓸 수 있다 / 메일 기반 리뷰 문화, 커널의 브랜치 체계, GPL이 실제로 무엇을 요구하는지가 새로 배울 부분
> **선행 노트**: E1(소스 트리 지도), E2(빌드), E3(모듈·`EXPORT_SYMBOL`·taint) — D7(binding 패치에 checkpatch·get_maintainer를 처음 써 봄)을 먼저 봤다면 5절이 더 쉽다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어 팀에 있을 때를 떠올려 보자. 메인 개발 브랜치가 있고, 정해진 날짜에 기능을 동결하고, RC를 몇 번 돌린 뒤 양산 버전을 낸다. 양산 버전은 고객별로 갈라져 몇 년씩 핫픽스만 받는다. 메인 브랜치에서 고친 버그를 양산 브랜치로 "backport"하는 일도 늘 있었을 것이다.

Linux 커널도 똑같이 생겼다. 다만 규모가 크고(매 사이클 수천 명이 기여), 모든 것이 **공개 메일링 리스트와 공개 git 트리**에서 일어나고, 코드가 **GPL**이라서 그 코드를 제품에 넣어 파는 회사에도 의무가 생긴다는 점이 다르다. Embedded Linux 엔지니어가 이 구조를 알아야 하는 장면은 세 가지다.

1. **버전 선택** — 새 보드를 시작할 때 어떤 커널 위에 올릴지. 이 결정이 이후 3~5년 동안의 보안 패치 비용을 정한다.
2. **패치 흐름** — 우리 드라이버 수정을 upstream에 보내거나, upstream 수정을 우리 트리로 가져오는 일. 둘 다 같은 도구(`git format-patch`, `checkpatch.pl`, `Fixes:` 태그)를 쓴다.
3. **라이선스** — 우리 드라이버를 공개해야 하나, 사내 알고리즘을 모듈로 넣어도 되나, 제품을 팔 때 소스를 어떻게 제공하나.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="190" height="58" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="115" y="54" text-anchor="middle" font-size="14">linux-next</text>
  <text x="115" y="76" text-anchor="middle" font-size="12">매일 · 다음 머지 후보</text>
  <rect x="245" y="30" width="190" height="58" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="54" text-anchor="middle" font-size="14">mainline (Linus)</text>
  <text x="340" y="76" text-anchor="middle" font-size="12">7.3-rc6 · 새 기능 진입</text>
  <rect x="470" y="30" width="190" height="58" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="565" y="54" text-anchor="middle" font-size="14">stable 7.2.y</text>
  <text x="565" y="76" text-anchor="middle" font-size="12">버그 수정만 · 약 3개월</text>
  <rect x="470" y="140" width="190" height="58" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="164" text-anchor="middle" font-size="14">longterm (LTS)</text>
  <text x="565" y="186" text-anchor="middle" font-size="12">6.18.y 등 6개 · 2~6년</text>
  <rect x="245" y="140" width="190" height="58" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="164" text-anchor="middle" font-size="14">SoC 벤더 BSP 커널</text>
  <text x="340" y="186" text-anchor="middle" font-size="12">LTS + 벤더 패치 묶음</text>
  <rect x="20" y="140" width="190" height="58" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="115" y="164" text-anchor="middle" font-size="14">제품 커널</text>
  <text x="115" y="186" text-anchor="middle" font-size="12">보드 DTS · 설정 · 우리 패치</text>
  <rect x="470" y="262" width="190" height="58" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="286" text-anchor="middle" font-size="14">CIP SLTS</text>
  <text x="565" y="308" text-anchor="middle" font-size="12">LTS를 10년으로 연장</text>
  <rect x="245" y="262" width="190" height="58" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="286" text-anchor="middle" font-size="14">Android GKI</text>
  <text x="340" y="308" text-anchor="middle" font-size="12">android16-6.12 등</text>
  <line x1="212" y1="59" x2="243" y2="59" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="228" y="22" text-anchor="middle" font-size="12">머지 윈도</text>
  <line x1="437" y1="59" x2="468" y2="59" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="452" y="22" text-anchor="middle" font-size="12">릴리스</text>
  <line x1="565" y1="90" x2="565" y2="138" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="572" y="118" font-size="12">1년에 1개 선정</text>
  <line x1="468" y1="169" x2="437" y2="169" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="243" y1="169" x2="212" y2="169" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="565" y1="200" x2="565" y2="260" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="500" y1="200" x2="400" y2="260" stroke="currentColor" marker-end="url(#ah1)"/>
  <path d="M480,200 L440,232 L115,232 L115,202" fill="none" stroke="currentColor" stroke-dasharray="5 4" marker-end="url(#ah1)"/>
  <text x="128" y="252" font-size="12">upstream LTS 직접 사용</text>
</svg>
```

그림 1 — 커널의 계보. 새 기능은 mainline에만 들어가고, 릴리스된 버전은 stable이 되어 버그 수정만 받다가 끝난다. 1년에 하나쯤 longterm(LTS)으로 지정되어 몇 년 더 산다. SoC 벤더 BSP, Android GKI, CIP SLTS는 모두 LTS를 출발점으로 삼은 "파생 트리"다. 제품 커널은 벤더 BSP 위에 올리거나(실선), 벤더 없이 upstream LTS를 직접 쓴다(점선).

### 이 노트의 실습 환경

| 항목 | 값 |
|---|---|
| 실행 VM | Lima `elinux`, aarch64, `uname -r` = `7.0.0-38-generic`, git 2.53.0 |
| 커널 소스 | 공용 랩 `/var/lab/src/linux-6.18.55` (읽기 전용, git 저장소 아님) |
| 작업 폴더 | VM `/var/tmp/E7/` (파일 이름 접두어 `e7_`), 스크립트는 Mac `~/.elinux_scratch/E7/` |
| 실제 네트워크 데이터 | `kernel.org/releases.json`, `category/releases.html`, CDN 디렉터리 목록, `ChangeLog-6.18.55`, `incr/patch-6.18.54-55.xz` — 모두 2026-10-10에 받음 |
| 시뮬레이션 | 랩 트리를 복사해 6.18.54로 되돌린 뒤 `git init` (선택 이유는 4.2절) — 커밋 해시는 이 로컬 저장소의 것이다 |
| 없는 도구 | `git send-email`(패키지 `git-email` 미설치), `b4` 미설치 — 설치하지 않았다 (5.9절) |

본문에서 **[기억]** 표시는 이번에 실행이나 문서로 확인하지 못하고 기억에 의존한 내용이다. 면접 전에 한 번 더 확인하자.

---

## 1. 릴리스 모델 — merge window, -rc, 9~10주

### 1.1 직관 — 2주 열고 7주 굳힌다

SSD 펌웨어의 "기능 동결(feature freeze) → 버그만 고치는 안정화 → 양산" 사이클을 커널은 **고정된 리듬**으로 돈다.

- **merge window**(머지 윈도): 직전 버전이 나온 다음 날부터 약 2주. 이 기간에만 새 기능이 mainline에 들어온다. Linus가 서브시스템 maintainer들의 pull request를 받는다.
- **-rc**(release candidate): 머지 윈도가 닫히는 날 `-rc1`이 나오고, 그 뒤 매주 일요일 `-rc2`, `-rc3` … 이 기간에는 버그 수정만 들어간다.
- **릴리스**: 보통 `-rc7` 다음 주에 정식 버전(`X.Y`)이 나온다. 불안하면 `-rc8`을 한 번 더 낸다.

머지 윈도에 들어갈 코드는 그 전에 이미 **서브시스템 트리**(예: GPIO maintainer의 git 트리)에 들어가 있어야 하고, 그 트리들을 매일 합쳐 보는 것이 **linux-next**다. 즉 "rc 기간에 리뷰를 끝내 maintainer 트리에 넣어 두고 → 다음 머지 윈도에 mainline으로 간다"가 기본 리듬이다.

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="40" y="70" font-size="12">v7.2 출시 (08-17 UTC)</text>
  <text x="580" y="70" text-anchor="middle" font-size="12">v7.3 (예상 10-18)</text>
  <rect x="40" y="88" width="120" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="100" y="108" text-anchor="middle" font-size="13">머지 윈도 2주</text>
  <rect x="160" y="88" width="420" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="300" y="108" text-anchor="middle" font-size="13">안정화: 매주 -rc, 버그 수정만</text>
  <rect x="580" y="88" width="60" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
  <text x="610" y="108" text-anchor="middle" font-size="12">rc8?</text>
  <line x1="40" y1="124" x2="650" y2="124" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="40" y1="80" x2="40" y2="128" stroke="#4a7bd0" stroke-width="2"/>
  <line x1="580" y1="80" x2="580" y2="128" stroke="#4a7bd0" stroke-width="2"/>
  <text x="160" y="142" text-anchor="middle" font-size="12">rc1</text>
  <text x="220" y="142" text-anchor="middle" font-size="12">rc2</text>
  <text x="280" y="142" text-anchor="middle" font-size="12">rc3</text>
  <text x="340" y="142" text-anchor="middle" font-size="12">rc4</text>
  <text x="400" y="142" text-anchor="middle" font-size="12">rc5</text>
  <text x="460" y="142" text-anchor="middle" font-size="12">rc6</text>
  <text x="520" y="142" text-anchor="middle" font-size="12">rc7</text>
  <line x1="460" y1="84" x2="460" y2="128" stroke="#d0564a" stroke-width="2"/>
  <text x="460" y="162" text-anchor="middle" font-size="12">오늘 = rc6 주간 (7.3-rc6: 10-04)</text>
  <text x="40" y="196" font-size="12">linux-next: 서브시스템 트리를 매일 합친 태그 (오늘 next-20261009)</text>
  <text x="40" y="218" font-size="12">stable 7.2.y: 필요할 때마다, 보통 주 1회 point release (7.2.9 = 10-03)</text>
  <text x="40" y="240" font-size="12">머지 윈도에 들어갈 코드는 그 전 사이클 동안 maintainer 트리와 linux-next에서 익어 있어야 한다</text>
</svg>
```

그림 2 — 7.3 사이클. 날짜는 kernel.org의 `releases-calendar.ics`(예측)와 `releases.json`(실제)에서 가져왔다.

### 1.2 정의 — 버전 번호 읽는 법

| 표기 | 뜻 | 누가 낸다 |
|---|---|---|
| `7.3-rc6` | 7.3의 6번째 release candidate (prepatch) | Linus Torvalds |
| `7.2` | mainline 정식 릴리스 | Linus |
| `7.2.9` | 7.2의 9번째 stable point release (버그 수정만) | stable 팀 (Greg Kroah-Hartman, Sasha Levin) |
| `6.18.55` | longterm 6.18의 55번째 point release | 같은 stable 팀 |
| `next-20261009` | 그날의 linux-next 통합 태그 | linux-next maintainer |
| `6.18.55-acme-r3` 같은 것 | 배포판·벤더 커널 (`-` 뒤가 붙음) | 배포판·벤더 |

kernel.org FAQ가 직접 말한다: 앞 숫자(5.x → 6.x → 7.x)는 "점 뒤 숫자가 너무 커 보이면" 올릴 뿐 **아무 의미가 없다**. 옛날(2.6 이전)의 홀수=개발판 규칙도 없어졌다. 7.0이 큰 변화라는 뜻이 아니다.

### 1.3 예제 1 — `releases.json`으로 지금 살아 있는 커널 보기

무엇을 확인하나: kernel.org가 기계용으로 내놓는 JSON에서 현재의 mainline·stable·longterm 목록을 뽑는다. 메모리 대신 실제 데이터로 시작한다.

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/E7 && cd /var/tmp/E7 &&
  curl -sS -o e7_releases.json https://www.kernel.org/releases.json &&
  curl -sS -o e7_releases.html https://www.kernel.org/category/releases.html && ls -l'
```

```text
total 24
-rw-rw-r-- 1 donh donh 15039 Oct 10 13:36 e7_releases.html
-rw-rw-r-- 1 donh donh  7555 Oct 10 13:36 e7_releases.json
```

JSON 항목 하나는 이렇게 생겼다(`python3 -m json.tool`로 펼친 것 중 longterm 6.18 부분):

```text
        {
            "moniker": "longterm",
            "version": "6.18.55",
            "iseol": false,
            "released": {
                "timestamp": 1791024071,
                "isodate": "2026-10-03"
            },
            "source": "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.55.tar.xz",
            "pgp": "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.55.tar.sign",
            "patch": {
                "full": "https://cdn.kernel.org/pub/linux/kernel/v6.x/patch-6.18.55.xz",
                "incremental": "https://cdn.kernel.org/pub/linux/kernel/v6.x/incr/patch-6.18.54-55.xz"
            },
            "changelog": "https://cdn.kernel.org/pub/linux/kernel/v6.x/ChangeLog-6.18.55",
            "gitweb": "https://git.kernel.org/stable/h/v6.18.55",
            "diffview": "https://git.kernel.org/stable/ds/v6.18.55/v6.18.54"
        },
```

전체를 한 줄씩 요약하는 분석 스크립트 `e7_analyze.py`(앞부분 — 뒷부분은 2.3절):

```python
#!/usr/bin/env python3
# E7: kernel.org 데이터 분석 — releases.json, 디렉터리 목록, ChangeLog
import json, re, sys, datetime as dt
from collections import defaultdict

D = sys.argv[1]  # /var/tmp/E7

def section(t): print(f"\n### {t}")

section("releases.json (fetched)")
rel = json.load(open(f"{D}/e7_releases.json"))
for r in rel["releases"]:
    print(f'{r["moniker"]:<11} {r["version"]:<14} iseol={str(r["iseol"]):<5} released={r["released"]["isodate"]}')

# 디렉터리 목록: linux-X.Y[.Z].tar.xz  DD-Mon-YYYY HH:MM
pat = re.compile(r'linux-(\d+)\.(\d+)(?:\.(\d+))?\.tar\.xz</a>\s+(\d\d-\w{3}-\d{4})')
series = defaultdict(list)   # (maj,min) -> [(sub, date)]
for f in ("e7_v5x.html", "e7_v6x.html", "e7_v7x.html"):
    for m in pat.finditer(open(f"{D}/{f}").read()):
        maj, mi, sub, d = int(m[1]), int(m[2]), int(m[3] or 0), m[4]
        series[(maj, mi)].append((sub, dt.datetime.strptime(d, "%d-%b-%Y").date()))

section("mainline cycle length (CDN upload date of linux-X.Y.tar.xz, UTC)")
mains = sorted((k, min(d for s, d in v if s == 0)) for k, v in series.items()
               if any(s == 0 for s, _ in v) and k >= (5, 10))
prev = None
gaps = []
for k, d in mains:
    g = (d - prev).days if prev else None
    if g: gaps.append(g)
    print(f"{k[0]}.{k[1]:<3} {d}  " + (f"+{g} d ({g/7:.1f} wk)" if g else ""))
    prev = d
print(f"cycles={len(gaps)} min={min(gaps)} max={max(gaps)} mean={sum(gaps)/len(gaps):.1f} d")

section("stable point releases per series (last .Z and its date)")
for k in sorted(series):
    if k < (5, 10): continue
    v = series[k]
    last = max(v)
    first = min(d for s, d in v if s == 0) if any(s == 0 for s, _ in v) else None
    span = (last[1] - first).days if first else 0
    print(f"{k[0]}.{k[1]:<3} last=.{last[0]:<4} on {last[1]}  span={span} d")
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7 &&
  curl -sS -o e7_v5x.html https://cdn.kernel.org/pub/linux/kernel/v5.x/ &&
  curl -sS -o e7_v6x.html https://cdn.kernel.org/pub/linux/kernel/v6.x/ &&
  curl -sS -o e7_v7x.html https://cdn.kernel.org/pub/linux/kernel/v7.x/ &&
  python3 -I /Users/donh/.elinux_scratch/E7/e7_analyze.py /var/tmp/E7'
```

```text
### releases.json (fetched)
mainline    7.3-rc6        iseol=False released=2026-10-04
stable      7.2.9          iseol=False released=2026-10-03
longterm    6.18.55        iseol=False released=2026-10-03
longterm    6.12.112       iseol=False released=2026-10-03
longterm    6.6.158        iseol=False released=2026-10-03
longterm    6.1.189        iseol=False released=2026-10-03
longterm    5.15.222       iseol=False released=2026-10-03
longterm    5.10.271       iseol=False released=2026-10-03
linux-next  next-20261009  iseol=False released=2026-10-10
```

출력에서 볼 것: 2026-10-10 현재 살아 있는 것은 mainline 1개(7.3-rc6), stable 1개(7.2.9), **longterm 6개**(6.18, 6.12, 6.6, 6.1, 5.15, 5.10)다. 6개 LTS가 모두 **같은 날(10-03)** point release를 냈다 — stable 팀이 한 번에 여러 트리를 같이 굴린다는 뜻이다. 랩 커널 6.18.55가 바로 이 날 나온 최신 LTS다.

### 1.4 예제 2 — 사이클 길이를 직접 재 보기

무엇을 확인하나: "9~10주"라는 말이 맞는지, CDN 디렉터리 목록의 `linux-X.Y.tar.xz` 업로드 날짜로 32개 사이클을 잰다(같은 스크립트의 출력 다음 부분 — 33줄 중 일부만 싣고 `...`로 생략, 전체 값은 그림 3에 다 있다).

```text
### mainline cycle length (CDN upload date of linux-X.Y.tar.xz, UTC)
5.10  2020-12-14  
5.11  2021-02-15  +63 d (9.0 wk)
5.12  2021-04-26  +70 d (10.0 wk)
5.13  2021-06-28  +63 d (9.0 wk)
...
6.0   2022-10-03  +63 d (9.0 wk)
6.1   2022-12-12  +70 d (10.0 wk)
6.2   2023-02-20  +70 d (10.0 wk)
...
6.16  2025-07-28  +63 d (9.0 wk)
6.17  2025-09-29  +63 d (9.0 wk)
6.18  2025-12-01  +63 d (9.0 wk)
6.19  2026-02-09  +70 d (10.0 wk)
7.0   2026-04-13  +63 d (9.0 wk)
7.1   2026-06-14  +62 d (8.9 wk)
7.2   2026-08-17  +64 d (9.1 wk)
cycles=32 min=62 max=71 mean=64.8 d
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="50" y="22" font-size="13">mainline 릴리스 간격 (일) — 5.11 ~ 7.2, 32 사이클</text>
<line x1="50" y1="250.0" x2="660" y2="250.0" stroke="currentColor" stroke-opacity="0.15"/> <text x="44" y="254.0" text-anchor="end" font-size="12">0</text> <line x1="50" y1="197.5" x2="660" y2="197.5" stroke="currentColor" stroke-opacity="0.15"/> <text x="44" y="201.5" text-anchor="end" font-size="12">20</text>
<line x1="50" y1="145.0" x2="660" y2="145.0" stroke="currentColor" stroke-opacity="0.15"/> <text x="44" y="149.0" text-anchor="end" font-size="12">40</text> <line x1="50" y1="92.5" x2="660" y2="92.5" stroke="currentColor" stroke-opacity="0.15"/> <text x="44" y="96.5" text-anchor="end" font-size="12">60</text>
<line x1="50" y1="40.0" x2="660" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/> <text x="44" y="44.0" text-anchor="end" font-size="12">80</text> <rect x="52.0" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <text x="59.5" y="266" text-anchor="middle" font-size="12">5.11</text>
<rect x="71.1" y="66.2" width="15.1" height="183.8" rx="2" fill="#e08a3c" fill-opacity="0.75"/> <rect x="90.1" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="109.2" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <text x="116.7" y="266" text-anchor="middle" font-size="12">5.14</text>
<rect x="128.2" y="87.2" width="15.1" height="162.8" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="147.3" y="63.6" width="15.1" height="186.4" rx="2" fill="#e08a3c" fill-opacity="0.75"/> <rect x="166.4" y="66.2" width="15.1" height="183.8" rx="2" fill="#e08a3c" fill-opacity="0.75"/> <text x="173.9" y="266" text-anchor="middle" font-size="12">5.17</text>
<rect x="185.4" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="204.5" y="66.2" width="15.1" height="183.8" rx="2" fill="#e08a3c" fill-opacity="0.75"/> <rect x="223.6" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <text x="231.1" y="266" text-anchor="middle" font-size="12">6.0</text>
<rect x="242.6" y="66.2" width="15.1" height="183.8" rx="2" fill="#e08a3c" fill-opacity="0.75"/> <rect x="261.7" y="66.2" width="15.1" height="183.8" rx="2" fill="#e08a3c" fill-opacity="0.75"/> <rect x="280.8" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <text x="288.3" y="266" text-anchor="middle" font-size="12">6.3</text>
<rect x="299.8" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="318.9" y="87.2" width="15.1" height="162.8" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="337.9" y="82.0" width="15.1" height="168.0" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <text x="345.5" y="266" text-anchor="middle" font-size="12">6.6</text>
<rect x="357.0" y="66.2" width="15.1" height="183.8" rx="2" fill="#e08a3c" fill-opacity="0.75"/> <rect x="376.1" y="87.2" width="15.1" height="162.8" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="395.1" y="82.0" width="15.1" height="168.0" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <text x="402.7" y="266" text-anchor="middle" font-size="12">6.9</text>
<rect x="414.2" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="433.2" y="87.2" width="15.1" height="162.8" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="452.3" y="82.0" width="15.1" height="168.0" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <text x="459.8" y="266" text-anchor="middle" font-size="12">6.12</text>
<rect x="471.4" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="490.4" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="509.5" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <text x="517.0" y="266" text-anchor="middle" font-size="12">6.15</text>
<rect x="528.6" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="547.6" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="566.7" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <text x="574.2" y="266" text-anchor="middle" font-size="12">6.18</text>
<rect x="585.8" y="66.2" width="15.1" height="183.8" rx="2" fill="#e08a3c" fill-opacity="0.75"/> <rect x="604.8" y="84.6" width="15.1" height="165.4" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <rect x="623.9" y="87.2" width="15.1" height="162.8" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <text x="631.4" y="266" text-anchor="middle" font-size="12">7.1</text>
<rect x="642.9" y="82.0" width="15.1" height="168.0" rx="2" fill="#4a7bd0" fill-opacity="0.75"/> <text x="650.5" y="266" text-anchor="middle" font-size="12">7.2</text> <line x1="50" y1="84.6" x2="660" y2="84.6" stroke="currentColor" stroke-dasharray="4 3" stroke-opacity="0.6"/> <line x1="50" y1="66.2" x2="660" y2="66.2" stroke="currentColor" stroke-dasharray="4 3" stroke-opacity="0.6"/>
<text x="56" y="59.0" font-size="12">점선 = 63일(9주, rc7에서 끝) · 70일(10주, rc8까지 감 → 주황 막대)</text> <text x="50" y="288" font-size="12">평균 64.8일 · 최소 62 · 최대 71 (kernel.org CDN의 linux-X.Y.tar.xz 업로드 날짜, UTC)</text>
</svg>
```

그림 3 — 측정한 mainline 사이클 길이. 대부분 63일(머지 윈도 2주 + rc1~rc7 7주)이고, 주황 막대는 70일(rc8까지 간 사이클)이다. 62·64일처럼 하루 차이는 업로드 시각이 UTC 자정을 넘었는지에 따른 것이다.

출력에서 볼 것:

- **9주가 기본, 10주는 예외**다. 32번 중 8번이 70~71일(rc8). 6.1, 6.6, 5.16, 6.7처럼 연말 휴가나 큰 변경이 겹치면 rc8이 나온다.
- CDN 날짜는 UTC라서 kernel.org 표의 "Released"와 하루 다를 수 있다. 예: 6.18은 표에 `2025-11-30`(미국 일요일), CDN에는 `2025-12-01`.
- 그래서 "이 기능이 언제 mainline에 들어가나?"는 이렇게 계산한다: 지금 maintainer 트리에 들어가면 → 다음 머지 윈도 → 그 버전 릴리스까지 약 9주. 지금이 7.3-rc6이니 maintainer 트리에 이번 주에 들어간 패치는 7.4-rc1(2026-11-01 예상)에 들어가고 7.4는 12월 하순에 나온다.

### 1.5 예제 3 — 릴리스 달력(예측)

무엇을 확인하나: kernel.org가 배포하는 ICS 달력으로 앞으로의 머지 윈도·rc1·릴리스 예상일을 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7 && curl -sS -o e7_releases-calendar.ics https://www.kernel.org/releases-calendar.ics &&
  grep -E "SUMMARY|DTSTART" e7_releases-calendar.ics'
```

```text
SUMMARY:Kernel v7.3 merge window
DTSTART;VALUE=DATE:20260817
SUMMARY:Kernel v7.3-rc1 release
DTSTART;VALUE=DATE:20260830
SUMMARY:Kernel v7.3 final release
DTSTART;VALUE=DATE:20261018
SUMMARY:Kernel v7.4 merge window
DTSTART;VALUE=DATE:20261019
SUMMARY:Kernel v7.4-rc1 release
DTSTART;VALUE=DATE:20261101
SUMMARY:Kernel v7.4 final release
DTSTART;VALUE=DATE:20261220
...
```

(7.5·7.6 항목 8줄은 생략했다.)

출력에서 볼 것: 머지 윈도(08-17) → rc1(08-30)이 정확히 2주, rc1 → final(10-18)이 7주다. 모두 **일요일**이다. 이 달력은 **예측**이라 rc8이 나오면 한 주씩 밀린다.

### 1.6 흔한 오해

- "7.0은 큰 변화" — 아니다. 번호는 의미 없다(1.2절).
- "-rc는 테스트용이니 제품에 쓰면 안 되고, 정식 릴리스(X.Y)는 바로 써도 된다" — X.Y.0은 아직 stable 수정이 하나도 안 들어간 상태다. 제품에는 최소한 stable point release(X.Y.Z)를 쓴다.
- "linux-next는 다음 버전 미리보기" — 미리보기는 맞지만 **매일 깨질 수 있는** 통합 테스트 트리다. 그 위에서 개발하지 않는다. 패치는 해당 서브시스템 트리(또는 최신 mainline -rc) 기준으로 만든다.

---

## 2. mainline · stable · longterm · 벤더 · GKI · CIP

### 2.1 정의 — 한 표로

| 종류 | 무엇 | 수명 | 누가 | 제품에 쓰나 |
|---|---|---|---|---|
| mainline | 새 기능이 들어가는 유일한 곳 (Linus 트리) | 9~10주마다 새 버전 | Linus | 직접은 안 씀. 기준점 |
| stable | 막 나온 X.Y에 버그 수정만 backport | 다음 버전이 나오고 조금 뒤 끝 (아래 측정: 약 75~100일) | Greg KH, Sasha Levin | 빨리 따라갈 수 있는 팀만 |
| longterm (LTS) | 1년에 하나쯤 골라 오래 수정 | 처음엔 2년 예정, 수요 있으면 연장 (아래 표: 현재 2~6년) | 같은 stable 팀 | 대부분의 임베디드 제품의 기준 |
| 벤더 BSP | SoC 벤더가 LTS 위에 자기 SoC 드라이버·핵을 얹은 트리 | 벤더가 정함 | NXP, TI, NVIDIA, Qualcomm … | 새 SoC면 처음엔 사실상 강제 |
| Android GKI | Google이 LTS에서 가지 친 Android 공통 커널, 벤더는 모듈로 붙음 | Android 릴리스와 묶임 | Google | Android 기기 |
| CIP SLTS | LTS 일부를 10년 유지 (산업·인프라용) | 10년 | Civil Infrastructure Platform (Linux Foundation 프로젝트) | 철도·전력·공장 등 수명 긴 장비 |
| 배포판 커널 | Ubuntu, RHEL, Yocto 레시피 등 | 배포판 정책 | 배포판 | 배포판 위의 제품 |

kernel.org `releases.html`이 쓰는 원문 정의(일부, 태그를 벗긴 텍스트):

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7 && sed -e "s/<[^>]*>//g" e7_releases.html | grep -v "^\s*$"'
```

```text
...
Mainline tree is maintained by Linus Torvalds. It's the tree where
all new features are introduced and where all the exciting new
development happens. New mainline kernels are released every 9-10
weeks.
Stable
After each mainline kernel is released, it is considered &quot;stable.&quot;
Any bug fixes for a stable kernel are backported from the mainline
tree and applied by a designated stable kernel maintainer. There are
usually only a few bugfix kernel releases until next mainline kernel
becomes available -- unless it is designated a &quot;longterm maintenance
kernel.&quot; Stable kernel updates are released on as-needed basis,
usually once a week.
...
Longterm release kernels
Version
Maintainer
Released
Projected EOL
6.18
Greg Kroah-Hartman &amp; Sasha Levin
2025-11-30
Dec, 2028
6.12
Greg Kroah-Hartman &amp; Sasha Levin
2024-11-17
Dec, 2028
6.6
Greg Kroah-Hartman &amp; Sasha Levin
2023-10-29
Dec, 2027
6.1
Greg Kroah-Hartman &amp; Sasha Levin
2022-12-11
Dec, 2027
5.15
Greg Kroah-Hartman &amp; Sasha Levin
2021-10-31
Dec, 2026
5.10
Greg Kroah-Hartman &amp; Sasha Levin
2020-12-13
Dec, 2026
...
The &quot;projected EOL&quot; dates are not set in stone. Each new longterm kernel
usually starts with only a 2-year projected EOL that can be extended
further if there is enough interest from the industry at large to help
support it for a longer period of time.
...
```

(`...`는 생략한 줄이다. `&quot;`, `&amp;`는 태그만 벗기고 HTML 엔티티는 그대로 둔 탓이다.)

출력에서 볼 것:

- **EOL은 "projected"(예상)** 이다. 처음엔 2년으로 시작하고, 산업계가 테스트·지원을 해 주면 늘린다. 5.10은 2020-12 출시 → 2026-12 EOL로 6년, 6.18은 2025-11 출시 → 2028-12로 지금은 3년이다.
- **5.10과 5.15가 2026년 12월에 같이 끝난다.** 오늘(2026-10-10)부터 3개월도 안 남았다. "벤더 BSP 5.15를 6.x로 올려라"(contents.md의 실무 시나리오)가 지금 당장의 일인 이유다.
- 6.6과 6.1은 2027-12, 6.12와 6.18은 2028-12.

### 2.2 예제 4 — point release 수와 수명 측정

무엇을 확인하나: 같은 스크립트의 셋째 부분. 시리즈마다 마지막 point release 번호와 첫 릴리스부터 마지막 point release까지의 기간을 잰다(33줄 중 일부, `...`는 생략).

```text
### stable point releases per series (last .Z and its date)
5.10  last=.271  on 2026-10-03  span=2119 d
5.11  last=.22   on 2021-05-19  span=93 d
5.12  last=.19   on 2021-07-20  span=85 d
...
5.15  last=.222  on 2026-10-03  span=1798 d
...
6.0   last=.19   on 2023-01-12  span=101 d
6.1   last=.189  on 2026-10-03  span=1391 d
6.2   last=.16   on 2023-05-17  span=86 d
...
6.6   last=.158  on 2026-10-03  span=1069 d
...
6.12  last=.112  on 2026-10-03  span=684 d
...
6.16  last=.12   on 2025-10-12  span=76 d
6.17  last=.13   on 2025-12-18  span=80 d
6.18  last=.55   on 2026-10-03  span=306 d
6.19  last=.14   on 2026-04-22  span=72 d
7.0   last=.14   on 2026-06-27  span=75 d
7.1   last=.13   on 2026-09-02  span=80 d
7.2   last=.9    on 2026-10-03  span=47 d
```

출력에서 볼 것:

- **LTS가 아닌 stable**(5.11, 6.2, 6.19, 7.0 …)은 모두 **72~101일, point release 11~22개**로 끝났다. 다음 mainline이 나오고 한 달쯤 겹치다 EOL이다. 이걸 제품에 쓰면 3개월마다 메이저 업그레이드를 해야 한다.
- **LTS**(5.10, 5.15, 6.1, 6.6, 6.12, 6.18)는 point release가 수십~수백 개다. 5.10은 271번째, 2119일(약 5.8년)째.
- 패턴이 보인다: 매년 **마지막 mainline**(5.10 = 2020-12, 5.15 = 2021-10, 6.1 = 2022-12, 6.6 = 2023-10, 6.12 = 2024-11, 6.18 = 2025-11)이 LTS가 됐다. kernel.org FAQ는 "기능, 상용 배포판·기기 제조사 수요, maintainer 여력 등으로 고른다"고만 말하지만, 실무에서는 "그 해 마지막 릴리스가 LTS"라고 예상하고 계획을 세운다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="60" y="20" font-size="13">LTS 커널 수명 — 출시일 → projected EOL (kernel.org, 2026-10-10 조회)</text>
<line x1="76.8" y1="36" x2="76.8" y2="262" stroke="currentColor" stroke-opacity="0.12"/> <text x="76.8" y="280" text-anchor="middle" font-size="12">2021</text> <line x1="143.3" y1="36" x2="143.3" y2="262" stroke="currentColor" stroke-opacity="0.12"/> <text x="143.3" y="280" text-anchor="middle" font-size="12">2022</text>
<line x1="209.8" y1="36" x2="209.8" y2="262" stroke="currentColor" stroke-opacity="0.12"/> <text x="209.8" y="280" text-anchor="middle" font-size="12">2023</text> <line x1="276.3" y1="36" x2="276.3" y2="262" stroke="currentColor" stroke-opacity="0.12"/> <text x="276.3" y="280" text-anchor="middle" font-size="12">2024</text>
<line x1="343.0" y1="36" x2="343.0" y2="262" stroke="currentColor" stroke-opacity="0.12"/> <text x="343.0" y="280" text-anchor="middle" font-size="12">2025</text> <line x1="409.5" y1="36" x2="409.5" y2="262" stroke="currentColor" stroke-opacity="0.12"/> <text x="409.5" y="280" text-anchor="middle" font-size="12">2026</text>
<line x1="476.0" y1="36" x2="476.0" y2="262" stroke="currentColor" stroke-opacity="0.12"/> <text x="476.0" y="280" text-anchor="middle" font-size="12">2027</text> <line x1="542.6" y1="36" x2="542.6" y2="262" stroke="currentColor" stroke-opacity="0.12"/> <text x="542.6" y="280" text-anchor="middle" font-size="12">2028</text>
<line x1="609.2" y1="36" x2="609.2" y2="262" stroke="currentColor" stroke-opacity="0.12"/> <text x="609.2" y="280" text-anchor="middle" font-size="12">2029</text> <text x="52" y="60" text-anchor="end" font-size="13">5.10</text> <rect x="73.3" y="44" width="387.6" height="22" rx="4" fill="#4a7bd0" fill-opacity="0.55"/>
<rect x="460.9" y="44" width="14.9" height="22" rx="4" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/> <text x="481.9" y="60" font-size="12">2026-12</text> <text x="79.3" y="60" font-size="12">5.10.271</text> <text x="52" y="96" text-anchor="end" font-size="13">5.15</text>
<rect x="132.0" y="80" width="328.9" height="22" rx="4" fill="#4a7bd0" fill-opacity="0.55"/> <rect x="460.9" y="80" width="14.9" height="22" rx="4" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/> <text x="481.9" y="96" font-size="12">2026-12</text> <text x="138.0" y="96" font-size="12">5.15.222</text>
<text x="52" y="132" text-anchor="end" font-size="13">6.1</text> <rect x="206.0" y="116" width="254.9" height="22" rx="4" fill="#4a7bd0" fill-opacity="0.55"/> <rect x="460.9" y="116" width="81.5" height="22" rx="4" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/> <text x="548.4" y="132" font-size="12">2027-12</text>
<text x="212.0" y="132" font-size="12">6.1.189</text> <text x="52" y="168" text-anchor="end" font-size="13">6.6</text> <rect x="264.6" y="152" width="196.3" height="22" rx="4" fill="#4a7bd0" fill-opacity="0.55"/> <rect x="460.9" y="152" width="81.5" height="22" rx="4" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="548.4" y="168" font-size="12">2027-12</text> <text x="270.6" y="168" font-size="12">6.6.158</text> <text x="52" y="204" text-anchor="end" font-size="13">6.12</text> <rect x="334.8" y="188" width="126.1" height="22" rx="4" fill="#4a7bd0" fill-opacity="0.55"/>
<rect x="460.9" y="188" width="148.2" height="22" rx="4" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/> <text x="615.1" y="204" font-size="12">2028-12</text> <text x="340.8" y="204" font-size="12">6.12.112</text> <text x="52" y="240" text-anchor="end" font-size="13">6.18</text>
<rect x="403.7" y="224" width="57.2" height="22" rx="4" fill="#4a7bd0" fill-opacity="0.55"/> <rect x="460.9" y="224" width="148.2" height="22" rx="4" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/> <text x="615.1" y="240" font-size="12">2028-12</text> <text x="409.7" y="240" font-size="12">6.18.55</text>
<line x1="460.9" y1="36" x2="460.9" y2="262" stroke="#d0564a" stroke-width="2"/> <text x="456.9" y="300" text-anchor="end" font-size="12">오늘 2026-10-10</text> <rect x="60" y="306" width="14" height="10" fill="#4a7bd0" fill-opacity="0.55"/><text x="80" y="315" font-size="12">지나온 기간 (막대 안 = 최신 point release)</text> <rect x="360" y="306" width="14" height="10" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><text x="380" y="315" font-size="12">남은 기간 (projected EOL까지)</text>
</svg>
```

그림 4 — 6개 LTS의 수명(kernel.org 표의 Released → Projected EOL, EOL은 해당 월 말로 그림). 파란 부분이 지나온 기간, 막대 안 숫자가 오늘 기준 최신 point release다.

### 2.3 예제 5 — stable 릴리스 하나에는 무엇이 들어가나

무엇을 확인하나: 6.18.54 → 6.18.55 한 번의 point release에 들어간 커밋 395개를 `ChangeLog-6.18.55`에서 세고, 태그 분포를 본다. `e7_analyze.py`의 뒷부분:

```python
section("ChangeLog-6.18.55 — what goes into one stable release")
import lzma
txt = open(f"{D}/e7_ChangeLog-6.18.55").read()
commits = re.split(r'^commit [0-9a-f]{40}\n', txt, flags=re.M)[1:]
n = len(commits)
def cnt(rx): return sum(1 for c in commits if re.search(rx, c, re.M | re.I))
print(f"commits                      : {n}")
print(f"  'Linux 6.18.55' tag commit  : {cnt(r'^    Linux 6\.18\.55$')}")
print(f"  has upstream commit id      : {cnt(r'^    (\[ Upstream commit [0-9a-f]+ \]|commit [0-9a-f]{40} upstream)')}")
print(f"  has Fixes:                  : {cnt(r'^    Fixes: [0-9a-f]{12}')}")
print(f"  has Cc: stable@             : {cnt(r'^    Cc: .*stable@')}")
print(f"  has Stable-dep-of:          : {cnt(r'^    Stable-dep-of:')}")
print(f"  signed off by Sasha Levin   : {cnt(r'^    Signed-off-by: Sasha Levin')}")
print(f"  signed off by Greg KH       : {cnt(r'^    Signed-off-by: Greg Kroah-Hartman')}")
subj = defaultdict(int)
for c in commits:
    s = c.split("\n\n", 1)[1].strip().split("\n")[0]
    subj[s.split(":")[0].strip() if ":" in s else "(none)"] += 1
print("top subject prefixes:", sorted(subj.items(), key=lambda x: -x[1])[:10])

section("patch-6.18.54-55 — files touched by top-level dir")
p = lzma.open(f"{D}/e7_patch-6.18.54-55.xz", "rt").read()
files = re.findall(r'^diff --git a/(\S+)', p, re.M)
top = defaultdict(int)
for f in files:
    parts = f.split("/")
    top["/".join(parts[:2]) if parts[0] in ("drivers", "arch") else parts[0]] += 1
print(f"files={len(files)}")
for k, v in sorted(top.items(), key=lambda x: -x[1])[:12]:
    print(f"{v:4d} {k}")
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7 &&
  curl -sS -o e7_ChangeLog-6.18.55 https://cdn.kernel.org/pub/linux/kernel/v6.x/ChangeLog-6.18.55 &&
  curl -sS -o e7_patch-6.18.54-55.xz https://cdn.kernel.org/pub/linux/kernel/v6.x/incr/patch-6.18.54-55.xz &&
  python3 -I /Users/donh/.elinux_scratch/E7/e7_analyze.py /var/tmp/E7 | sed -n "/ChangeLog-6.18.55/,\$p"'
```

```text
### ChangeLog-6.18.55 — what goes into one stable release
commits                      : 395
  'Linux 6.18.55' tag commit  : 1
  has upstream commit id      : 389
  has Fixes:                  : 352
  has Cc: stable@             : 166
  has Stable-dep-of:          : 32
  signed off by Sasha Levin   : 244
  signed off by Greg KH       : 189

top subject prefixes: [('net', 46), ('KVM', 21), ('bpf', 19), ('xfs', 15), ('nfc', 15), ('Bluetooth', 14), ('ovpn', 10), ('net/sched', 9), ('HID', 9), ('gve', 9)]

### patch-6.18.54-55 — files touched by top-level dir
files=382
  77 net
  65 drivers/net
  59 drivers/gpu
  33 fs
  15 arch/arm64
  15 arch/x86
  14 include
  12 kernel
  11 drivers/nfc
   9 drivers/s390
   8 arch/s390
   8 drivers/hid
```

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="20" font-size="13">ChangeLog-6.18.55 — stable 릴리스 하나에 들어간 커밋 (6.18.54 → 6.18.55)</text>
<text x="192" y="55" text-anchor="end" font-size="13">전체 커밋</text> <rect x="200" y="40" width="400.0" height="20" rx="3" fill="#888" fill-opacity="0.7"/> <text x="606.0" y="55" font-size="12">395 (100%)</text> <text x="192" y="87" text-anchor="end" font-size="13">upstream 커밋 id 표기</text>
<rect x="200" y="72" width="393.9" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.7"/> <text x="599.9" y="87" font-size="12">389 (98%)</text> <text x="192" y="119" text-anchor="end" font-size="13">Fixes: 태그 있음</text> <rect x="200" y="104" width="356.5" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.7"/>
<text x="562.5" y="119" font-size="12">352 (89%)</text> <text x="192" y="151" text-anchor="end" font-size="13">Sasha Levin 서명</text> <rect x="200" y="136" width="247.1" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.7"/> <text x="453.1" y="151" font-size="12">244 (62%)</text>
<text x="192" y="183" text-anchor="end" font-size="13">Greg KH 서명</text> <rect x="200" y="168" width="191.4" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.7"/> <text x="397.4" y="183" font-size="12">189 (48%)</text> <text x="192" y="215" text-anchor="end" font-size="13">Cc: stable@ 있음</text>
<rect x="200" y="200" width="168.1" height="20" rx="3" fill="#3f9a6b" fill-opacity="0.7"/> <text x="374.1" y="215" font-size="12">166 (42%)</text> <text x="192" y="247" text-anchor="end" font-size="13">Stable-dep-of: 있음</text> <rect x="200" y="232" width="32.4" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.7"/>
<text x="238.4" y="247" font-size="12">32 (8%)</text> <text x="20" y="278" font-size="12">Cc: stable은 166개뿐 — 나머지는 stable 팀이 Fixes: 등을 보고 골라 온 커밋</text>
</svg>
```

그림 5 — 6.18.55에 들어간 395개 커밋의 태그 분포. "upstream 커밋 id"가 거의 100%라는 것이 stable 규칙 1번("mainline에 먼저 들어가 있어야 한다")의 증거다.

출력에서 볼 것:

- **일주일에 약 400개 수정**이 LTS 하나에 들어간다. 382개 파일, 네트워크·GPU·파일시스템이 많다. 벤더 BSP가 이걸 몇 달씩 안 따라가면 수천 개가 밀린다.
- 395개 중 389개가 `commit <sha> upstream.` 줄을 가진다 — **stable은 mainline 커밋의 복사본**이다. 나머지 6개는 `Linux 6.18.55` 태그 커밋과 stable 전용 수정이다.
- `Fixes:`가 352개(89%)인데 `Cc: stable`은 166개(42%)뿐이다. 작성자가 stable을 명시하지 않아도 stable 팀이 `Fixes:` 태그 등을 보고 골라 온다. 이게 **`Fixes:` 태그를 정확히 써야 하는 실무적 이유**다. stable 팀이 도구(AUTOSEL 등)로 후보를 고른다는 것은 **[기억]**.
- `Stable-dep-of:` 32개 — 진짜 수정이 의존하는 선행 커밋을 같이 가져왔다는 표시다.

이 중 Don의 센서 키트와 가까운 실제 커밋 하나(gpio 드라이버)를 보면 upstream 커밋 메시지의 모범이 다 들어 있다:

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7 && grep -n -B2 -A30 "gpio: zynq" e7_ChangeLog-6.18.55 | head -60'
```

```text
6151-Date:   Tue Sep 22 09:20:58 2026 +0000
6152-
6153:    gpio: zynq: fix runtime PM leak on request error path
6154-    
6155-    commit e9438ab5328a177c9c0e5df87eb92a7162841e98 upstream.
6156-    
6157-    pm_runtime_get_sync() leaves the usage counter incremented even when it
6158-    fails, and zynq_gpio_request() returns the error without dropping it.
6159-    
6160-    gpiolib does not call ->free() when ->request() fails, so zynq_gpio_free(),
6161-    which holds the only matching pm_runtime_put(), never runs. The reference
6162-    is leaked and the controller can no longer runtime-suspend, so its clock
6163-    stays enabled.
6164-    
6165-    Switch to pm_runtime_resume_and_get(), which only increments the usage
6166-    counter on success.
6167-    
6168-    Fixes: 3242ba117e9b ("gpio: Add driver for Zynq GPIO controller")
6169-    Cc: stable@vger.kernel.org
6170-    Signed-off-by: Ridham Khurana <khurana.ridham222@gmail.com>
6171-    Link: https://patch.msgid.link/20260922092102.1053513-1-khurana.ridham222@gmail.com
6172-    Signed-off-by: Bartosz Golaszewski <bartosz.golaszewski@oss.qualcomm.com>
6173-    Signed-off-by: Greg Kroah-Hartman <gregkh@linuxfoundation.org>
...
```

(6174행부터는 다음 커밋 `gpio: cdev: fix kernel stack leak ...`이라 생략했다.)

출력에서 볼 것: 제목 `gpio: zynq: <명령형 요약>` → 본문(무엇이 왜 틀렸고 결과가 무엇인지) → 고친 방법 → `Fixes:` (12자 sha + 원래 제목) → `Cc: stable` → 작성자 `Signed-off-by` → `Link:`(메일 아카이브) → maintainer(Bartosz, GPIO) `Signed-off-by` → stable 팀(Greg) `Signed-off-by`. **Signed-off-by가 3개 쌓인 것이 이 패치가 거쳐 온 사람들의 경로**다. 이 커밋은 4절과 5절의 실습에서 다시 쓴다.

### 2.4 예제 6 — Android GKI와 CIP SLTS의 실제 브랜치

무엇을 확인하나: 두 파생 트리가 어떤 LTS를 기반으로 하는지, 원격 저장소의 브랜치 이름만 받아서(`git ls-remote`, 내려받기 없음) 확인한다.

```sh
limactl shell elinux -- bash -lc '
  timeout 60 git ls-remote --heads https://git.kernel.org/pub/scm/linux/kernel/git/cip/linux-cip.git | awk "{print \$2}" |
    grep -vE "rt|rebase|wip" | sed "s|refs/heads/||" | sort -V | tr "\n" " "; echo
  timeout 90 git ls-remote --heads https://android.googlesource.com/kernel/common | awk "{print \$2}" | sed "s|refs/heads/||" |
    grep -E "^android[0-9]+-[0-9]+\.[0-9]+$|^android-mainline$" | sort -V | tr "\n" " "'
```

```text
linux-4.4.y linux-4.4.y-cip linux-4.4.y-st linux-4.4.y-st-rc linux-4.19.y linux-4.19.y-cip linux-4.19.y-st linux-4.19.y-st-rc linux-5.10.y-cip linux-5.10.y-cip-rc linux-6.1.y-cip linux-6.1.y-cip-rc linux-6.12.y-cip linux-6.12.y-cip-rc master 
android11-5.4 android12-5.4 android12-5.10 android13-5.10 android13-5.15 android14-5.15 android14-6.1 android15-6.6 android16-6.12 android17-6.18 android-mainline 
```

출력에서 볼 것:

- **CIP**는 4.4, 4.19, 5.10, 6.1, 6.12를 골랐다. 모두 kernel.org LTS다. CIP 웹사이트 문구(2026-10-10 받음): "Super-Long-Term Stable (SLTS) kernels with a maintenance period of ten years … The SLTS kernels start off from the standard LTS kernels". 4.4와 4.19는 kernel.org에서는 이미 EOL이지만 CIP에서는 살아 있다(`-st` 브랜치). 산업용 장비를 10년 팔아야 하면 CIP 버전과 맞추는 것이 유리하다.
- **Android GKI**는 `android<Android 버전>-<커널 버전>` 이름이다. android16-6.12, android17-6.18처럼 매년 그 해 LTS를 따라간다. GKI는 "Google이 빌드한 공통 커널 이미지 + 벤더 기능은 모듈로"라는 구조이고, 커널↔모듈 인터페이스(KMI)를 고정해 벤더가 커널 본체를 못 고치게 한 것이 핵심이다 **[기억]**. 결과적으로 Android 기기는 벤더 패치가 커널 본체에 섞이지 않고, LTS 업데이트를 받기 쉬워졌다.

### 2.5 함정

- "stable"이라는 이름 때문에 **"stable 7.2.y가 LTS보다 안정적"** 이라고 생각하기 쉽다. stable은 "버그 수정만 받는 상태"라는 뜻일 뿐 수명이 3개월이다.
- `uname -r`에 `-` 뒤 문자열(예: VM의 `7.0.0-38-generic`)이 붙어 있으면 **배포판 커널**이다. kernel.org FAQ도 "그건 배포판에 물어보라"고 한다. 버그를 upstream에 보고할 때는 **최신 mainline이나 해당 LTS 최신 point release에서 재현**한 뒤 보고한다.
- EOL 날짜를 기억으로 말하지 마라. **projected**이고 바뀐다. 필요할 때마다 `releases.json`·`releases.html`을 본다.

---

## 3. 제품용 커널 고르기 — 벤더 6.6 BSP냐, 6.18 LTS냐

### 3.1 판단 축 네 개

1. **SoC의 upstream 지원 상태** — mainline에 SoC의 DT(`arch/arm64/boot/dts/<vendor>/`), clock·pinctrl·전원 도메인, 그리고 **제품에 꼭 필요한 블록**(카메라 ISP, GPU, NPU, 비디오 코덱, PCIe·USB PHY)이 들어 있나. DT가 있다고 다 되는 게 아니다.
2. **backport 부담** — 벤더 트리에 있는 패치를 새 LTS로 옮기거나, 새 LTS의 수정을 벤더 트리로 가져오는 비용. 벤더 패치가 많을수록, LTS와 버전 차이가 클수록 커진다.
3. **보안 수정** — 제품 수명(예: 5~10년) 동안 CVE 수정을 어디서 받을 것인가. 벤더가 stable point release를 따라가는 빈도가 핵심이다. 2024년부터 커널 프로젝트가 직접 CNA가 되어 수정된 버그 대부분에 CVE를 붙이면서, "CVE 개수"보다 "stable을 따라가느냐"가 더 중요해졌다 **[기억]**.
4. **제품 수명 vs LTS EOL** — 예제 4의 그림 4. 2027년 출시, 2032년까지 지원해야 하는 장비라면 6.6(2027-12 EOL)은 출시하자마자 끝난다.

### 3.2 예제 7 — upstream DT가 얼마나 있나 (6.18.55 트리)

무엇을 확인하나: `contents.md` 시나리오에 나오는 SoC들이 랩의 6.18.55 mainline 트리에 보드 DTS를 얼마나 가지고 있는지 센다. 판단 축 1의 가장 싼 1차 확인이다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55/arch/arm64/boot/dts &&
  for p in "freescale/imx8mp*.dts" "ti/k3-am62*.dts" "nvidia/tegra234*.dts" "rockchip/rk3588*.dts" "qcom/qcs6490*.dts"; do
    echo "$p: $(ls $p 2>/dev/null | wc -l)"; done; ls nvidia/tegra234*.dts'
```

```text
freescale/imx8mp*.dts: 56
ti/k3-am62*.dts: 31
nvidia/tegra234*.dts: 6
rockchip/rk3588*.dts: 41
qcom/qcs6490*.dts: 1
nvidia/tegra234-p3737-0000+p3701-0000.dts
nvidia/tegra234-p3737-0000+p3701-0008.dts
nvidia/tegra234-p3740-0002+p3701-0008.dts
nvidia/tegra234-p3768-0000+p3767-0000.dts
nvidia/tegra234-p3768-0000+p3767-0005.dts
nvidia/tegra234-sim-vdk.dts
```

출력에서 볼 것: i.MX8MP(56), AM62(31), RK3588(41)은 upstream에 보드가 많다 — 커뮤니티와 벤더가 upstream에 투자한 SoC다. Jetson Orin(tegra234)은 6개로 개발 키트 위주다. 이 숫자는 **출발점일 뿐**이다. 다음 단계는 그 SoC의 카메라·NPU·코덱 드라이버가 `drivers/`에 있는지, 벤더 문서가 "upstream 지원 매트릭스"를 공개하는지 보는 것이다. 예를 들어 Jetson은 실제 제품에서 NVIDIA의 L4T(벤더 커널)를 쓰는 것이 일반적이다 **[기억]**.

### 3.3 결정표

| 상황 | 추천 | 이유 |
|---|---|---|
| SoC가 새로 나와 mainline 지원이 거의 없음 | 벤더 BSP (예: 6.6 기반) | 동작하는 것이 먼저. 대신 벤더가 stable을 따라가는지 계약·확인 |
| SoC가 upstream 잘 됨 + 필요한 블록(ISP·NPU 등)도 upstream | 최신 LTS (6.18) 직접 | 보안 수정 무료 수신, 수명 2028-12+, 벤더 종속 없음 |
| SoC는 upstream 잘 됨, 일부 블록(NPU·GPU)만 벤더 전용 | 최신 LTS + 벤더 out-of-tree 모듈 | 본체는 upstream, 전용 블록만 모듈로. 모듈 라이선스 확인(6절) |
| 제품 수명 10년 (산업·인프라) | CIP SLTS와 맞는 LTS (6.12 등) | 10년 수정 경로 확보 |
| Android 기기 | 해당 Android 버전의 GKI | 선택지가 사실상 고정 |
| 출시 직전, 커널 교체 위험이 큼 | 지금 커널 유지 + stable point release 계속 머지 | 메이저 업그레이드는 다음 제품 주기에 |
| 벤더 BSP가 EOL 임박 LTS(5.10·5.15) 기반 | 마이그레이션 계획을 지금 시작 | 2026-12 projected EOL (예제 4) |

"벤더 6.6 BSP vs mainline 6.18 LTS"를 표로 다시 보면:

| 비교 항목 | 벤더 6.6 BSP | upstream 6.18 LTS |
|---|---|---|
| 첫 부팅까지 | 빠름 (벤더 EVK 그대로) | SoC 지원에 따라 빠르거나 매우 느림 |
| 남은 수명 (2026-10 기준) | 6.6 projected EOL 2027-12 → 약 14개월 | 2028-12 → 약 26개월, 연장 가능성 |
| 보안 수정 | 벤더가 6.6.y를 머지하는 주기에 의존 | 매주 6.18.y를 직접 받음 (예제 5: 1회 약 400개) |
| 커뮤니티 도움 | "벤더 커널이면 벤더에 물어보라" | 메일링 리스트에서 도움 받음 |
| 우리 패치의 upstream | 6.6 기준 패치는 mainline에 그대로 못 보냄 | 거의 그대로 보낼 수 있음 |
| 다음 업그레이드 | 벤더가 다음 BSP를 낼 때까지 대기 | 매년 LTS를 따라 이동 가능 |
| 위험 | 벤더 패치 수천 개가 섞여 디버그가 어려움 **[기억]** | 특정 블록 기능 누락·성능 차이 |

### 3.4 현실적인 하이브리드 전략

많은 팀이 이렇게 한다: (1) 프로토타입은 벤더 BSP로 빨리 띄운다. (2) 동시에 같은 보드를 최신 LTS로 부팅해 보는 "upstream 트랙"을 둔다. (3) 우리 드라이버·DT는 처음부터 upstream 스타일로 써서 두 트리에서 모두 빌드되게 한다. (4) 양산 전에 어느 쪽으로 갈지 정한다. Don이 SSD에서 "새 컨트롤러는 벤더 SDK로 bring-up, 양산 펌웨어는 자사 코드베이스로"를 했던 것과 같은 구조다.

---

## 4. 벤더 커널의 현실 — 패치 스택을 새 stable 위로 옮기기

### 4.1 직관

벤더 BSP는 "LTS 태그 + 그 위에 쌓인 벤더 커밋 N개"다. stable 팀이 매주 새 point release를 내면, 벤더(또는 우리)는 **그 N개를 새 태그 위로 옮겨야**(rebase 또는 merge) 보안 수정을 받는다. 옮길 때 세 가지가 일어난다.

1. 벤더 커밋이 upstream과 상관없는 곳을 건드리면 → **그대로 옮겨진다**.
2. 벤더가 upstream 수정을 미리 cherry-pick 해 뒀고 내용이 같으면 → git이 "이미 upstream에 있다"며 **자동으로 버린다**.
3. 벤더가 같은 버그를 **다른 방식으로** 고쳤으면 → **충돌**. 사람이 판단해야 한다.

### 4.2 예제 8 — 실험용 저장소 만들기 (선택: 랩 트리 + 실제 incremental patch)

무엇을 확인하나: 진짜 stable 변경으로 rebase를 해 보기 위한 git 저장소를 싸게 만든다.

**선택한 방법과 이유**: linux-stable을 shallow clone해 태그 두 개를 받는 방법은 수백 MB를 내려받는다. 대신 (1) 랩의 6.18.55 소스를 `/var/tmp/E7/linux`로 복사하고, (2) kernel.org의 실제 incremental patch `patch-6.18.54-55.xz`(131 KB)를 **거꾸로** 적용해 6.18.54 상태로 만든 뒤 `git init`, (3) 같은 패치를 다시 정방향으로 적용해 "Linux 6.18.55" 커밋 하나로 만들었다. 그래서 **6.18.54 → 6.18.55의 395개 커밋이 한 커밋으로 뭉쳐 있고**, 해시는 이 로컬 저장소의 것이다. 내용(코드)은 진짜 stable과 같다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7 && time cp -a /var/lab/src/linux-6.18.55 linux && cd linux &&
  xz -dc ../e7_patch-6.18.54-55.xz | patch -p1 -R -s --dry-run && echo DRYRUN_OK && head -5 Makefile'
```

```text
real	0m26.505s
user	0m0.376s
sys	0m7.232s
DRYRUN_OK
# SPDX-License-Identifier: GPL-2.0
VERSION = 6
PATCHLEVEL = 18
SUBLEVEL = 55
EXTRAVERSION =
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7/linux && xz -dc ../e7_patch-6.18.54-55.xz | patch -p1 -R -s &&
  grep ^SUBLEVEL Makefile && git init -q -b stable && git config user.name "Don H" && git config user.email don@example.com &&
  time (git add -A && git commit -q -m "Linux 6.18.54" && git tag v6.18.54) && git count-objects -vH | head -2 && git log --oneline'
```

```text
SUBLEVEL = 54

real	3m32.624s
user	0m34.108s
sys	0m22.787s
count: 96654
size: 590.20 MiB
6fabb12 Linux 6.18.54
```

출력에서 볼 것: 거꾸로 적용하니 `SUBLEVEL = 54`가 됐다. 파일 약 9.7만 개 저장소를 만드는 데 3분 반(VM 디스크). incremental patch가 "이전 point release → 다음 point release"의 정확한 diff라서 이런 재구성이 가능하다.

### 4.3 예제 9 — 벤더 커밋 3개를 6.18.55 위로 rebase

무엇을 확인하나: 4.1의 세 경우(그대로 / 자동 drop / 충돌)를 실제로 만든다. 벤더 커밋은 2.3절의 실제 stable 수정(gpio-zynq, gpio-tps65219)과 겹치게 골랐다.

```sh
#!/bin/bash
# E7: 벤더 패치 스택 시뮬레이션 — v6.18.54 위 벤더 커밋 3개 → v6.18.55로 rebase
set -e
cd /var/tmp/E7/linux
git checkout -q stable
# (1) "upstream" stable: 6.18.54 -> 6.18.55 incremental patch를 한 커밋으로
if ! git rev-parse -q --verify v6.18.55 >/dev/null; then
  xz -dc ../e7_patch-6.18.54-55.xz | git apply
  git add -A && git commit -q -m "Linux 6.18.55 (squashed incremental patch, 394 stable commits)"
  git tag v6.18.55
fi
# (2) 벤더 브랜치: v6.18.54에서 시작
git checkout -q -B e7_vendor v6.18.54
F=drivers/gpio/gpio-zynq.c
sed -i 's|^\t{ .compatible = "xlnx,pmc-gpio-1.0", .data = &pmc_gpio_def },|&\n\t{ .compatible = "acme,e7-kit-gpio", .data = \&zynqmp_gpio_def },|' $F
git commit -q -am "VENDOR: gpio: zynq: add acme e7-kit compatible"
# 벤더가 upstream 수정을 그대로 cherry-pick 해 둔 경우 (내용이 같다)
python3 - $F <<'EOF'
import sys; f=sys.argv[1]; s=open(f).read()
old='''\tint ret;

\tret = pm_runtime_get_sync(chip->parent);

\t/*
\t * If the device is already active pm_runtime_get() will return 1 on
\t * success, but gpio_request still needs to return 0.
\t */
\treturn ret < 0 ? ret : 0;
'''
assert old in s; open(f,"w").write(s.replace(old,"\treturn pm_runtime_resume_and_get(chip->parent);\n"))
EOF
git commit -q -am "gpio: zynq: fix runtime PM leak on request error path (backport)"
# 벤더가 같은 버그를 다른 방식으로 고친 경우 (같은 줄을 건드린다)
T=drivers/gpio/gpio-tps65219.c
sed -i 's|ret = !!(val & BIT(TPS65219_MFP_GPIO_STATUS_MASK));|ret = (val \& TPS65219_MFP_GPIO_STATUS_MASK) ? 1 : 0; /* ACME: HW bug #4711 */|' $T
git commit -q -am "VENDOR: gpio: tps65219: fix GPIO0 status read on acme board"
git log --oneline v6.18.54..e7_vendor
echo "--- merge-base / ahead-behind"
git rev-list --left-right --count v6.18.55...e7_vendor
```

```sh
limactl shell elinux -- bash -lc 'bash /Users/donh/.elinux_scratch/E7/e7_vendor.sh'
```

```text
aacc36c VENDOR: gpio: tps65219: fix GPIO0 status read on acme board
84aa2bd gpio: zynq: fix runtime PM leak on request error path (backport)
258aa79 VENDOR: gpio: zynq: add acme e7-kit compatible
--- merge-base / ahead-behind
1	3
```

`1	3`은 "v6.18.55 쪽에만 1개, 벤더 쪽에만 3개"라는 뜻이다(55 쪽 1개는 395개를 뭉친 커밋). 이제 rebase한다:

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7/linux && git checkout -q -B e7_vendor_rebase e7_vendor &&
  git rebase --onto v6.18.55 v6.18.54 e7_vendor_rebase; echo "rebase exit=$?"; git status --short | head; echo ---; git diff | head -30'
```

```text
Rebasing (1/3)Rebasing (2/3)dropping 84aa2bd82767a198a72a16e4c3143f098509cbce gpio: zynq: fix runtime PM leak on request error path (backport) -- patch contents already upstream
Rebasing (3/3)Auto-merging drivers/gpio/gpio-tps65219.c
CONFLICT (content): Merge conflict in drivers/gpio/gpio-tps65219.c
error: could not apply aacc36c... VENDOR: gpio: tps65219: fix GPIO0 status read on acme board
hint: Resolve all conflicts manually, mark them as resolved with
hint: "git add/rm <conflicted_files>", then run "git rebase --continue".
hint: You can instead skip this commit: run "git rebase --skip".
hint: To abort and get back to the state before "git rebase", run "git rebase --abort".
hint: Disable this message with "git config set advice.mergeConflict false"
Could not apply aacc36c... # VENDOR: gpio: tps65219: fix GPIO0 status read on acme board
rebase exit=1
UU drivers/gpio/gpio-tps65219.c
---
diff --cc drivers/gpio/gpio-tps65219.c
index 8e00c3b,fdc8a5e..0000000
--- a/drivers/gpio/gpio-tps65219.c
+++ b/drivers/gpio/gpio-tps65219.c
@@@ -79,7 -79,7 +79,11 @@@ static int tps65219_gpio_get(struct gpi
  	if (ret)
  		return ret;
  
++<<<<<<< HEAD
 +	ret = !!(val & TPS65219_MFP_GPIO_STATUS_MASK);
++=======
+ 	ret = (val & TPS65219_MFP_GPIO_STATUS_MASK) ? 1 : 0; /* ACME: HW bug #4711 */
++>>>>>>> aacc36c (VENDOR: gpio: tps65219: fix GPIO0 status read on acme board)
  	dev_warn(dev, "GPIO%d = %d, MULTI_DEVICE_ENABLE, not a standard GPIO\n", offset, ret);
  
  	/*
```

충돌을 판단한다: upstream(HEAD)도 똑같이 `BIT()`를 없애 버그를 고쳤다. 의미가 같으므로 **upstream 쪽을 채택하고 벤더 커밋을 버린다**(`--skip`). 그리고 `git range-diff`로 "옮기기 전 스택"과 "옮긴 뒤 스택"을 비교한다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7/linux && git rebase --skip; echo "exit=$?"; git log --oneline -3;
  echo "--- range-diff"; git range-diff v6.18.54..e7_vendor v6.18.55..e7_vendor_rebase'
```

```text
Successfully rebased and updated refs/heads/e7_vendor_rebase.
exit=0
0b7923d VENDOR: gpio: zynq: add acme e7-kit compatible
98cd6b4 Linux 6.18.55 (squashed incremental patch, 394 stable commits)
6fabb12 Linux 6.18.54
--- range-diff
1:  258aa79 = 1:  0b7923d VENDOR: gpio: zynq: add acme e7-kit compatible
2:  84aa2bd < -:  ------- gpio: zynq: fix runtime PM leak on request error path (backport)
3:  aacc36c < -:  ------- VENDOR: gpio: tps65219: fix GPIO0 status read on acme board
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <line x1="60" y1="50" x2="630" y2="50" stroke="currentColor" marker-end="url(#ah3)"/>
  <circle cx="100" cy="50" r="11" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="100" y="30" text-anchor="middle" font-size="12">v6.18.54</text>
  <circle cx="380" cy="50" r="11" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <text x="380" y="30" text-anchor="middle" font-size="12">v6.18.55 (stable 395개)</text>
  <text x="600" y="30" text-anchor="middle" font-size="12">stable</text>
  <path d="M100,61 L160,130" fill="none" stroke="currentColor"/>
  <line x1="160" y1="130" x2="280" y2="130" stroke="currentColor"/>
  <circle cx="160" cy="130" r="11" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="160" y="134" text-anchor="middle" font-size="12">A</text>
  <circle cx="220" cy="130" r="11" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="220" y="134" text-anchor="middle" font-size="12">B</text>
  <circle cx="280" cy="130" r="11" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <text x="280" y="134" text-anchor="middle" font-size="12">C</text>
  <text x="220" y="162" text-anchor="middle" font-size="12">e7_vendor (rebase 전)</text>
  <path d="M380,61 L440,130" fill="none" stroke="currentColor"/>
  <circle cx="440" cy="130" r="11" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
  <text x="440" y="134" text-anchor="middle" font-size="12">A'</text>
  <circle cx="500" cy="130" r="11" fill="none" stroke="#888" stroke-dasharray="3 3"/>
  <text x="500" y="134" text-anchor="middle" font-size="12">B</text>
  <circle cx="560" cy="130" r="11" fill="none" stroke="#d0564a" stroke-dasharray="3 3"/>
  <text x="560" y="134" text-anchor="middle" font-size="12">C</text>
  <text x="500" y="158" text-anchor="middle" font-size="12">drop</text>
  <text x="560" y="158" text-anchor="middle" font-size="12">skip</text>
  <text x="500" y="182" text-anchor="middle" font-size="12">e7_vendor_rebase (rebase 후)</text>
  <text x="30" y="222" font-size="12">A  VENDOR: zynq에 보드 compatible 추가 — upstream과 겹치지 않음 → 그대로 옮겨짐 (A')</text>
  <text x="30" y="246" font-size="12">B  upstream 수정을 미리 cherry-pick — 내용이 같음 → git이 patch-id로 알아보고 drop</text>
  <text x="30" y="270" font-size="12">C  같은 버그를 다른 코드로 수정 — 같은 줄 충돌 → upstream 수정 채택, 벤더 커밋 skip</text>
</svg>
```

그림 6 — 예제 9에서 실제로 일어난 일. 벤더 스택 3개가 rebase 뒤 1개로 줄었다. 이것이 "upstream을 자주 따라가면 벤더 패치가 줄어든다"는 말의 실체다.

출력에서 볼 것:

- `dropping ... -- patch contents already upstream` — git은 커밋 해시가 아니라 **patch-id**(diff 내용의 해시)로 같은 변경을 알아본다. 그래서 cherry-pick된 backport는 해시가 달라도 자동으로 빠진다. 단 **한 글자라도 다르면**(컨텍스트를 손봤거나 충돌을 다르게 풀었으면) 못 알아보고 충돌이나 중복이 된다.
- C의 충돌은 기계가 못 푼다. "벤더 코드와 upstream 코드가 같은 의미인가?"를 사람이 판단해야 한다. 실제 BSP 업그레이드는 이런 판단이 수백 번이다.
- `git range-diff`는 "스택 A와 스택 B의 커밋을 짝지어 비교"한다. `=`는 같음, `<`는 오른쪽에서 사라짐, `!`는 바뀜. 벤더 업그레이드 리뷰와 패치 v1/v2 비교(5.8절)에 같은 도구를 쓴다.

### 4.4 실무 팁 — 패치 스택을 관리하는 관습

- **커밋 제목 접두어로 출처를 표시한다.** Android 공통 커널은 `UPSTREAM:`(mainline에서 cherry-pick), `BACKPORT:`(수정해서 가져옴), `FROMGIT:`(maintainer 트리에서), `FROMLIST:`(메일링 리스트에만 있음), `ANDROID:`(Android 전용)를 쓴다 **[기억]**. 이 노트의 `VENDOR:` 접두어도 같은 생각이다. 업그레이드 때 "upstream에 이미 있나?"를 빠르게 분류할 수 있다.
- **backport 커밋에는 upstream 해시를 남긴다**: `commit <40자 sha> upstream.` 또는 `(cherry picked from commit <sha>)`(`git cherry-pick -x`). 나중에 rebase할 때 그 해시가 새 base에 있는지 `git merge-base --is-ancestor`로 바로 확인할 수 있다.
- **merge vs rebase**: 제품 트리는 stable 태그를 주기적으로 **merge**하는 방식도 많다(이력을 유지, 충돌 해결이 한 번). 벤더 스택 자체를 깨끗하게 유지하려면 rebase. 팀 정책을 따르되, 어느 쪽이든 "stable을 매주~매월 따라간다"가 핵심이다.

---

## 5. 패치 보내기 — 실제 작업 흐름

### 5.1 전체 흐름

```svg
<svg viewBox="0 0 680 332" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="15" y="30" width="155" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="92" y="50" text-anchor="middle" font-size="12">git commit -s</text>
  <text x="92" y="68" text-anchor="middle" font-size="12">변경 1개 = 커밋 1개</text>
  <rect x="180" y="30" width="155" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="257" y="50" text-anchor="middle" font-size="12">format-patch -v2</text>
  <text x="257" y="68" text-anchor="middle" font-size="12">v2 · 커버 레터</text>
  <rect x="345" y="30" width="155" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="422" y="50" text-anchor="middle" font-size="12">checkpatch --strict</text>
  <text x="422" y="68" text-anchor="middle" font-size="12">스타일 · 태그 검사</text>
  <rect x="510" y="30" width="155" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="587" y="50" text-anchor="middle" font-size="12">get_maintainer.pl</text>
  <text x="587" y="68" text-anchor="middle" font-size="12">받는 사람 찾기</text>
  <rect x="510" y="120" width="155" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="587" y="140" text-anchor="middle" font-size="12">git send-email</text>
  <text x="587" y="158" text-anchor="middle" font-size="12">--dry-run 먼저</text>
  <rect x="345" y="120" width="155" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="422" y="140" text-anchor="middle" font-size="12">메일링 리스트</text>
  <text x="422" y="158" text-anchor="middle" font-size="12">lore.kernel.org 보관</text>
  <rect x="180" y="120" width="155" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="257" y="140" text-anchor="middle" font-size="12">리뷰 · 태그</text>
  <text x="257" y="158" text-anchor="middle" font-size="12">Reviewed-by 등</text>
  <rect x="15" y="120" width="155" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="92" y="140" text-anchor="middle" font-size="12">maintainer 트리</text>
  <text x="92" y="158" text-anchor="middle" font-size="12">for-next 브랜치</text>
  <rect x="15" y="210" width="155" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="92" y="230" text-anchor="middle" font-size="12">linux-next</text>
  <text x="92" y="248" text-anchor="middle" font-size="12">매일 통합 테스트</text>
  <rect x="180" y="210" width="155" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="257" y="230" text-anchor="middle" font-size="12">머지 윈도 → rc1</text>
  <text x="257" y="248" text-anchor="middle" font-size="12">Linus가 pull</text>
  <rect x="345" y="210" width="155" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="422" y="230" text-anchor="middle" font-size="12">mainline 릴리스</text>
  <text x="422" y="248" text-anchor="middle" font-size="12">예: 7.4</text>
  <rect x="510" y="210" width="155" height="46" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="587" y="230" text-anchor="middle" font-size="12">stable · LTS</text>
  <text x="587" y="248" text-anchor="middle" font-size="12">Cc: stable 태그로</text>
  <line x1="170" y1="53" x2="178" y2="53" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="335" y1="53" x2="343" y2="53" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="500" y1="53" x2="508" y2="53" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="587" y1="76" x2="587" y2="118" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="510" y1="143" x2="502" y2="143" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="345" y1="143" x2="337" y2="143" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="180" y1="143" x2="172" y2="143" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="257" y1="120" x2="257" y2="78" stroke="#d0564a" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
  <text x="264" y="104" font-size="12">고쳐서 vN+1</text>
  <line x1="92" y1="166" x2="92" y2="208" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="170" y1="233" x2="178" y2="233" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="335" y1="233" x2="343" y2="233" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="500" y1="233" x2="508" y2="233" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="15" y="284" font-size="12">파랑 = 내 PC에서 · 주황 = 메일로 공개 리뷰</text>
  <text x="15" y="302" font-size="12">초록 = maintainer와 Linus의 트리 · 빨강 = stable 팀이 가져감</text>
  <text x="15" y="320" font-size="12">rc 기간에 maintainer 트리에 들어간 패치는 다음 머지 윈도에 mainline으로 간다 (1절)</text>
</svg>
```

그림 7 — 커밋에서 LTS까지 패치의 일생. 이 절의 예제들은 위 줄(파랑)과 send-email 직전까지를 실제로 돌린다. 메일은 실제로 보내지 않는다.

### 5.2 커밋 메시지 해부

| 부분 | 규칙 | 예 (5.4절 v2) |
|---|---|---|
| 제목 접두어 | `subsystem: component: ` — 그 파일의 과거 커밋 제목을 따라 한다 (`git log --oneline -- <파일>`) | `gpio: zynq:` |
| 제목 요약 | 명령형, 소문자 시작이 보통, 마침표 없음, 대략 70자 안 | `fix runtime PM leak when gpiochip registration fails` |
| 본문 | **무엇이 문제이고 왜**, 결과가 무엇인지. 그다음 어떻게 고쳤는지. 75자에서 줄바꿈 | 2문단 |
| `Fixes:` | 버그를 들여온 커밋. sha 12자 이상 + `("원래 제목")` | `Fixes: af50089f0070 ("gpio: zynq: use dev_err_probe() ...")` |
| `Cc: stable@vger.kernel.org` | stable에 가져가 달라는 표시 (stable-kernel-rules 옵션 1) | 버그 수정 패치에만 |
| `Signed-off-by:` | DCO(개발자 원산지 증명)에 동의한다는 서명. 실명 | `git commit -s` |
| `Link:`, `Closes:`, `Reported-by:` | 버그 보고·토론 링크, 보고자 | 있으면 |
| `---` 아래 | 커밋에 안 남는 메모: 버전별 변경 이력, 질문 | `Changes in v2:` |

**DCO**(Developer's Certificate of Origin)는 `Documentation/process/submitting-patches.rst`에 원문이 있다. 요지는 "(a) 내가 썼고 이 라이선스로 낼 권리가 있다, 또는 (b) 호환 오픈소스 라이선스의 기존 작업에 기반한다, 또는 (c) (a)(b)를 증명한 사람에게서 받았다, (d) 이 기여와 서명이 공개·영구 보관된다는 걸 안다". 회사 코드를 보낼 때 **회사가 그 권리를 허락했는지**가 (a)의 실질이다 — 대부분의 회사에 오픈소스 기여 승인 절차가 있는 이유다.

제목 접두어를 고르는 실제 방법은 해당 파일의 이력을 보는 것이다. 이 실습 저장소는 이력이 없으므로, 대신 진짜 stable ChangeLog에서 같은 드라이버 제목을 찾았다(2.3절 출력: `gpio: zynq: fix runtime PM leak on request error path`).

### 5.3 예제 10 — 일부러 엉성한 v1과 `checkpatch.pl --strict`

무엇을 확인하나: 가상의 버그 커밋(이 저장소 안에서 "이미 upstream에 있는 커밋" 역할)을 만들고, 그 버그를 고치는 v1 패치를 **흔한 실수를 다 넣어** 만든 뒤 checkpatch가 무엇을 잡는지 본다. 버그는 "에러 경로에서 `goto` 정리 대신 `return`해서 runtime PM 참조가 샌다" — 2.3절 실제 커밋과 같은 종류다.

```sh
#!/bin/bash
# E7: 패치 작업 흐름 — 버그 커밋(가상의 upstream 커밋) + v1(엉성한) 수정 패치
set -e
cd /var/tmp/E7/linux
git checkout -q -B e7_patch v6.18.55
F=drivers/gpio/gpio-zynq.c
# (A) 버그를 들여오는 커밋 — 이 저장소 안에서 "upstream에 이미 있는 커밋" 역할
python3 - $F <<'EOF'
import sys; f=sys.argv[1]; s=open(f).read()
old='''\tif (ret) {
\t\tdev_err(&pdev->dev, "Failed to add gpio chip\\n");
\t\tgoto err_pm_put;
\t}
'''
new='''\tif (ret)
\t\treturn dev_err_probe(&pdev->dev, ret, "Failed to add gpio chip\\n");
'''
assert old in s; open(f,"w").write(s.replace(old,new))
EOF
git commit -q -s -am "gpio: zynq: use dev_err_probe() for gpiochip registration failure" \
  -m "Convert the last dev_err() in probe to dev_err_probe() so that -EPROBE_DEFER is not logged as an error."
git tag e7_bug
# (B) v1 수정 — 엉성한 코드와 커밋 메시지
python3 - $F <<'EOF'
import sys; f=sys.argv[1]; s=open(f).read()
old='''\tif (ret)
\t\treturn dev_err_probe(&pdev->dev, ret, "Failed to add gpio chip\\n");
'''
new='''\tif (ret){
\t\tdev_err_probe(&pdev->dev, ret, "Failed to add gpio chip (gpiochip_add_data returned error), dropping PM ref\\n");
\t\tgoto err_pm_put;
\t}
'''
assert old in s; open(f,"w").write(s.replace(old,new))
EOF
git commit -q -am "Fixed the PM leak in zynq gpio driver." \
  -m "When gpiochip_add_data fails we return directly and never call pm_runtime_put and pm_runtime_disable, this leaks the usage count so the controller never suspends anymore and the clock stays on." \
  -m "Fixes: $(git rev-parse --short=8 e7_bug)"
git log --oneline -3
rm -rf ../e7_v1 && git format-patch -q -1 -o ../e7_v1 && ls ../e7_v1
```

```sh
limactl shell elinux -- bash -lc 'bash /Users/donh/.elinux_scratch/E7/e7_patch_setup.sh && cd /var/tmp/E7/linux &&
  cat ../e7_v1/*.patch && echo "=== checkpatch --strict" && ./scripts/checkpatch.pl --strict ../e7_v1/*.patch; echo "exit=$?"'
```

```text
effce08 Fixed the PM leak in zynq gpio driver.
af50089 gpio: zynq: use dev_err_probe() for gpiochip registration failure
98cd6b4 Linux 6.18.55 (squashed incremental patch, 394 stable commits)
0001-Fixed-the-PM-leak-in-zynq-gpio-driver.patch
From effce0851b134aec5cab35e8668b0687070120bd Mon Sep 17 00:00:00 2001
From: Don H <don@example.com>
Date: Sat, 10 Oct 2026 13:43:23 -0700
Subject: [PATCH] Fixed the PM leak in zynq gpio driver.

When gpiochip_add_data fails we return directly and never call pm_runtime_put and pm_runtime_disable, this leaks the usage count so the controller never suspends anymore and the clock stays on.

Fixes: af50089f
---
 drivers/gpio/gpio-zynq.c | 6 ++++--
 1 file changed, 4 insertions(+), 2 deletions(-)

diff --git a/drivers/gpio/gpio-zynq.c b/drivers/gpio/gpio-zynq.c
index b36df47..7d2c740 100644
--- a/drivers/gpio/gpio-zynq.c
+++ b/drivers/gpio/gpio-zynq.c
@@ -972,8 +972,10 @@ static int zynq_gpio_probe(struct platform_device *pdev)
 
 	/* report a bug if gpio chip registration fails */
 	ret = gpiochip_add_data(chip, gpio);
-	if (ret)
-		return dev_err_probe(&pdev->dev, ret, "Failed to add gpio chip\n");
+	if (ret){
+		dev_err_probe(&pdev->dev, ret, "Failed to add gpio chip (gpiochip_add_data returned error), dropping PM ref\n");
+		goto err_pm_put;
+	}
 
 	irq_set_status_flags(gpio->irq, IRQ_DISABLE_UNLAZY);
 	device_init_wakeup(&pdev->dev, 1);
-- 
2.53.0

=== checkpatch --strict
WARNING: Prefer a maximum 75 chars per line (possible unwrapped commit description?)
#6: 
When gpiochip_add_data fails we return directly and never call pm_runtime_put and pm_runtime_disable, this leaks the usage count so the controller never suspends anymore and the clock stays on.

WARNING: Please use correct Fixes: style 'Fixes: <12+ chars of sha1> ("<title line>")' - ie: 'Fixes: af50089f0070 ("gpio: zynq: use dev_err_probe() for gpiochip registration failure")'
#8: 
Fixes: af50089f

ERROR: space required before the open brace '{'
#23: FILE: drivers/gpio/gpio-zynq.c:975:
+	if (ret){

WARNING: line length of 128 exceeds 100 columns
#24: FILE: drivers/gpio/gpio-zynq.c:976:
+		dev_err_probe(&pdev->dev, ret, "Failed to add gpio chip (gpiochip_add_data returned error), dropping PM ref\n");

ERROR: Missing Signed-off-by: line(s)

total: 2 errors, 3 warnings, 0 checks, 12 lines checked

NOTE: For some of the reported defects, checkpatch may be able to
      mechanically convert to the typical style using --fix or --fix-inplace.

../e7_v1/0001-Fixed-the-PM-leak-in-zynq-gpio-driver.patch has style problems, please review.

NOTE: If any of the errors are false positives, please report
      them to the maintainer, see CHECKPATCH in MAINTAINERS.
exit=1
```

출력에서 볼 것:

- **checkpatch가 잡은 것 5개**: 본문 75자 초과, `Fixes:` 형식(8자 sha, 제목 없음 — 친절하게 올바른 줄을 만들어 준다), `if (ret){` 공백, 100자 초과 줄, **`Signed-off-by` 없음**(DCO 서명 없이는 아무도 받지 않는다). 종료 코드 1.
- **checkpatch가 못 잡은 것**: 제목 `Fixed the PM leak in zynq gpio driver.` — 접두어 없음, 과거형, 마침표. 리뷰어가 첫 줄에서 지적할 내용이다. `Cc: stable` 누락도 도구가 판단하지 않는다.
- `Fixes:`가 8자인 것을 checkpatch가 실제 저장소에서 찾아 12자 + 제목으로 바꿔 보여 준 것에 주목하자. 이 검사는 **git 트리 안에서** 돌릴 때만 된다(5.5절).

### 5.4 예제 11 — v2 시리즈: 고친 수정 + 정리 패치 + 커버 레터

무엇을 확인하나: 리뷰를 반영한 v2를 만든다. 리뷰어가 "덤으로 `of_match_node()`도 `device_get_match_data()`로 바꿔 달라"고 했다고 가정해 **패치 2개 시리즈**로 만들고, 버전별 변경 이력은 `git notes`에 넣어 `---` 아래로 나가게 한다.

```sh
#!/bin/bash
# E7: v2 — 코드·메시지 고치고, 정리 패치 하나 더, notes(변경 이력) 달고 format-patch -v2 --cover-letter
set -e
cd /var/tmp/E7/linux
git checkout -q e7_patch
git reset -q --hard e7_bug
F=drivers/gpio/gpio-zynq.c
BUG=$(git rev-parse --short=12 e7_bug)
BUGSUBJ=$(git log -1 --format=%s e7_bug)
# --- 패치 1: 버그 수정 (stable 대상) — v1과 같은 방식의 python 치환으로 goto 경로 복원 (생략)
git commit -q -s -a -F - <<EOF
gpio: zynq: fix runtime PM leak when gpiochip registration fails

If gpiochip_add_data() fails, probe returns straight away and skips the
err_pm_put/err_pm_dis unwind labels. The runtime PM usage count taken
earlier by pm_runtime_resume_and_get() is never dropped and runtime PM
is left enabled, so the controller can never runtime-suspend.

Log the error with dev_err_probe() and jump to the existing unwind
labels again.

Fixes: $BUG ("$BUGSUBJ")
Cc: stable@vger.kernel.org
EOF
git notes add -m "Changes in v2:
- Use the err_pm_put unwind label instead of returning (Reviewer A)
- Shorten the error message back to the original text
- Wrap the commit message at 72 columns, add Fixes: and Cc: stable"
# --- 패치 2: 정리 (stable 대상 아님) — of_match_node() 블록을 device_get_match_data()로 (python 치환, 생략)
git commit -q -s -a -F - <<'EOF'
gpio: zynq: use device_get_match_data()

Use device_get_match_data() instead of open-coding of_match_node() and
dereferencing ->data. The driver only binds through its of_match_table,
so the extra "of_match_node() failed" message never triggers in practice
and can go.

No functional change intended.
EOF
git notes add -m "New in v2 (suggested by Reviewer A)."
rm -rf ../e7_v2 && git format-patch -q -v2 --cover-letter --notes -2 -o ../e7_v2
# 커버 레터의 *** SUBJECT HERE *** / *** BLURB HERE *** 를 sed·python으로 채움 (생략)
```

(스크립트 원본에는 코드 치환용 python 조각과 커버 레터 채우기가 들어 있다. 60줄 제한 때문에 "(생략)"으로 줄였다. 바뀐 코드는 아래 패치 출력에 그대로 있다.)

```sh
limactl shell elinux -- bash -lc 'bash /Users/donh/.elinux_scratch/E7/e7_patch_v2.sh && cd /var/tmp/E7 &&
  sed -n 1,30p e7_v2/v2-0000-cover-letter.patch && echo ==== && sed -n 1,20p e7_v2/v2-0002-*.patch &&
  cd linux && echo "=== checkpatch --strict" && ./scripts/checkpatch.pl --strict ../e7_v2/v2-000[12]-*.patch; echo "exit=$?"'
```

```text
eaf57ca gpio: zynq: use device_get_match_data()
5884690 gpio: zynq: fix runtime PM leak when gpiochip registration fails
af50089 gpio: zynq: use dev_err_probe() for gpiochip registration failure
98cd6b4 Linux 6.18.55 (squashed incremental patch, 394 stable commits)
v2-0000-cover-letter.patch
v2-0001-gpio-zynq-fix-runtime-PM-leak-when-gpiochip-regis.patch
v2-0002-gpio-zynq-use-device_get_match_data.patch
From eaf57ca1989b62c9b1dc9023a713441679adb27c Mon Sep 17 00:00:00 2001
From: Don H <don@example.com>
Date: Sat, 10 Oct 2026 13:44:30 -0700
Subject: [PATCH v2 0/2] gpio: zynq: fix PM leak in probe error path

Patch 1 fixes a runtime PM reference leak introduced when the
gpiochip_add_data() error path was converted to dev_err_probe(); it is
tagged for stable. Patch 2 is a small cleanup on top and is not.

Changes in v2:
- Patch 1: keep the goto unwind, shorter message, Fixes: + Cc: stable
- Patch 2: new

v1: https://lore.kernel.org/r/<message-id-of-v1>

Don H (2):
  gpio: zynq: fix runtime PM leak when gpiochip registration fails
  gpio: zynq: use device_get_match_data()

 drivers/gpio/gpio-zynq.c | 15 +++++++--------
 1 file changed, 7 insertions(+), 8 deletions(-)

-- 
2.53.0

====
From eaf57ca1989b62c9b1dc9023a713441679adb27c Mon Sep 17 00:00:00 2001
From: Don H <don@example.com>
Date: Sat, 10 Oct 2026 13:44:30 -0700
Subject: [PATCH v2 2/2] gpio: zynq: use device_get_match_data()

Use device_get_match_data() instead of open-coding of_match_node() and
dereferencing ->data. The driver only binds through its of_match_table,
so the extra "of_match_node() failed" message never triggers in practice
and can go.

No functional change intended.

Signed-off-by: Don H <don@example.com>
---

Notes:
    New in v2 (suggested by Reviewer A).

 drivers/gpio/gpio-zynq.c | 9 +++------
 1 file changed, 3 insertions(+), 6 deletions(-)
=== checkpatch --strict
------------------------------------------------------------------------
../e7_v2/v2-0001-gpio-zynq-fix-runtime-PM-leak-when-gpiochip-regis.patch
------------------------------------------------------------------------
total: 0 errors, 0 warnings, 0 checks, 12 lines checked

../e7_v2/v2-0001-gpio-zynq-fix-runtime-PM-leak-when-gpiochip-regis.patch has no obvious style problems and is ready for submission.
----------------------------------------------------------
../e7_v2/v2-0002-gpio-zynq-use-device_get_match_data.patch
----------------------------------------------------------
total: 0 errors, 0 warnings, 0 checks, 27 lines checked

../e7_v2/v2-0002-gpio-zynq-use-device_get_match_data.patch has no obvious style problems and is ready for submission.
exit=0
```

(커버 레터의 `<message-id-of-v1>`은 v1을 실제로 보내지 않았기 때문에 둔 자리표시다. 실제로는 lore에서 v1 스레드 링크를 넣는다.)

패치 1 전체와 패치 2의 diff 부분은 같은 결과물을 다시 출력해서 본다(이 사이에 `git config core.abbrev 12`를 해서 log 해시가 12자로 나온다 — 5.8절):

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7 && git -C linux log --oneline -3 && cat e7_v2/v2-0001-*.patch &&
  echo ==== && sed -n "/^diff --git/,\$p" e7_v2/v2-0002-*.patch'
```

```text
eaf57ca1989b gpio: zynq: use device_get_match_data()
58846909db79 gpio: zynq: fix runtime PM leak when gpiochip registration fails
af50089f0070 gpio: zynq: use dev_err_probe() for gpiochip registration failure
From 58846909db79efb957fc4c3cfe4d8fceaf561ae4 Mon Sep 17 00:00:00 2001
From: Don H <don@example.com>
Date: Sat, 10 Oct 2026 13:44:30 -0700
Subject: [PATCH v2 1/2] gpio: zynq: fix runtime PM leak when gpiochip
 registration fails

If gpiochip_add_data() fails, probe returns straight away and skips the
err_pm_put/err_pm_dis unwind labels. The runtime PM usage count taken
earlier by pm_runtime_resume_and_get() is never dropped and runtime PM
is left enabled, so the controller can never runtime-suspend.

Log the error with dev_err_probe() and jump to the existing unwind
labels again.

Fixes: af50089f0070 ("gpio: zynq: use dev_err_probe() for gpiochip registration failure")
Cc: stable@vger.kernel.org
Signed-off-by: Don H <don@example.com>
---

Notes:
    Changes in v2:
    - Use the err_pm_put unwind label instead of returning (Reviewer A)
    - Shorten the error message back to the original text
    - Wrap the commit message at 72 columns, add Fixes: and Cc: stable

 drivers/gpio/gpio-zynq.c | 6 ++++--
 1 file changed, 4 insertions(+), 2 deletions(-)

diff --git a/drivers/gpio/gpio-zynq.c b/drivers/gpio/gpio-zynq.c
index b36df47..3a85061 100644
--- a/drivers/gpio/gpio-zynq.c
+++ b/drivers/gpio/gpio-zynq.c
@@ -972,8 +972,10 @@ static int zynq_gpio_probe(struct platform_device *pdev)
 
 	/* report a bug if gpio chip registration fails */
 	ret = gpiochip_add_data(chip, gpio);
-	if (ret)
-		return dev_err_probe(&pdev->dev, ret, "Failed to add gpio chip\n");
+	if (ret) {
+		dev_err_probe(&pdev->dev, ret, "Failed to add gpio chip\n");
+		goto err_pm_put;
+	}
 
 	irq_set_status_flags(gpio->irq, IRQ_DISABLE_UNLAZY);
 	device_init_wakeup(&pdev->dev, 1);
-- 
2.53.0

====
diff --git a/drivers/gpio/gpio-zynq.c b/drivers/gpio/gpio-zynq.c
index 3a85061..c51f530 100644
--- a/drivers/gpio/gpio-zynq.c
+++ b/drivers/gpio/gpio-zynq.c
@@ -15,6 +15,7 @@
 #include <linux/module.h>
 #include <linux/platform_device.h>
 #include <linux/pm_runtime.h>
+#include <linux/property.h>
 #include <linux/of.h>
 
 #define DRIVER_NAME "zynq-gpio"
@@ -896,18 +897,14 @@ static int zynq_gpio_probe(struct platform_device *pdev)
 	struct zynq_gpio *gpio;
 	struct gpio_chip *chip;
 	struct gpio_irq_chip *girq;
-	const struct of_device_id *match;
 
 	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
 	if (!gpio)
 		return -ENOMEM;
 
-	match = of_match_node(zynq_gpio_of_match, pdev->dev.of_node);
-	if (!match) {
-		dev_err(&pdev->dev, "of_match_node() failed\n");
+	gpio->p_data = device_get_match_data(&pdev->dev);
+	if (!gpio->p_data)
 		return -EINVAL;
-	}
-	gpio->p_data = match->data;
 	platform_set_drvdata(pdev, gpio);
 
 	gpio->base_addr = devm_platform_ioremap_resource(pdev, 0);
-- 
2.53.0
```

출력에서 볼 것:

- 제목에 `[PATCH v2 1/2]`가 자동으로 붙는다(`-v2`). 커버 레터는 `[PATCH v2 0/2]`이고 `git format-patch`가 shortlog와 diffstat을 채워 준다. `*** SUBJECT HERE ***`, `*** BLURB HERE ***`는 **반드시 직접 채운다** — 그대로 보내는 실수가 리스트에 실제로 자주 보인다.
- `Notes:`가 `---` 아래에 들어갔다. `git am`으로 적용할 때 `---` 아래는 버려지므로 **변경 이력은 메일에만 있고 커밋에는 남지 않는다**. 이것이 커널의 관습이다.
- 버그 수정(패치 1, stable 대상)과 정리(패치 2, stable 아님)를 **분리하고, 수정을 앞에 둔다**. stable 팀이 패치 1만 가져갈 수 있어야 하기 때문이다(stable 규칙: "obviously correct", 100줄 이하).
- 둘 다 0 errors, 0 warnings, 0 checks, exit 0.

### 5.5 예제 12 — 기본 모드 vs `--strict`, 그리고 존재하지 않는 Fixes: sha

무엇을 확인하나: `--strict`가 추가로 무엇을 보는지와, `Fixes:` 해시를 저장소에서 실제로 찾는지 확인한다.

```sh
#!/bin/bash
# E7: 기본 모드 vs --strict 차이 + 존재하지 않는 Fixes: sha
set -e
cd /var/tmp/E7/linux
git checkout -q -B e7_strict e7_patch
F=drivers/gpio/gpio-zynq.c
python3 - $F <<'EOF'
import sys; f=sys.argv[1]; s=open(f).read()
old='''\tgpio->p_data = device_get_match_data(&pdev->dev);
\tif (!gpio->p_data)
\t\treturn -EINVAL;
'''
new='''\tgpio->p_data = device_get_match_data(&pdev->dev);
\tif (gpio->p_data == NULL) {

\t\tdev_err(&pdev->dev, "no match data for %s\\n",
\t\t\tdev_name(&pdev->dev));
\t\treturn -EINVAL;
\t}
'''
assert old in s; open(f,"w").write(s.replace(old,new))
EOF
git commit -q -s -a -m "gpio: zynq: report missing match data" \
  -m "Print which device had no match data." \
  -m 'Fixes: 0123456789ab ("gpio: zynq: some commit that does not exist")'
git format-patch -q -1 -o ../e7_strict
echo "### default"; ./scripts/checkpatch.pl -q ../e7_strict/0001-*.patch || true
echo "### --strict"; ./scripts/checkpatch.pl -q --strict ../e7_strict/0001-*.patch || true
git checkout -q e7_patch
```

```sh
limactl shell elinux -- bash -lc 'rm -rf /var/tmp/E7/e7_strict; bash /Users/donh/.elinux_scratch/E7/e7_strict.sh'
```

```text
### default
WARNING: Unknown commit id '0123456789ab', maybe rebased or not pulled?
#8: 
Fixes: 0123456789ab ("gpio: zynq: some commit that does not exist")

total: 0 errors, 1 warnings, 13 lines checked
### --strict
WARNING: Unknown commit id '0123456789ab', maybe rebased or not pulled?
#8: 
Fixes: 0123456789ab ("gpio: zynq: some commit that does not exist")

CHECK: Comparison to NULL could be written "!gpio->p_data"
#23: FILE: drivers/gpio/gpio-zynq.c:906:
+	if (gpio->p_data == NULL) {

CHECK: Blank lines aren't necessary after an open brace '{'
#24: FILE: drivers/gpio/gpio-zynq.c:907:
+	if (gpio->p_data == NULL) {
+

total: 0 errors, 1 warnings, 2 checks, 13 lines checked
```

출력에서 볼 것:

- **`CHECK:` 등급은 `--strict`에서만 나온다.** `== NULL` 비교, 여는 중괄호 뒤 빈 줄 같은 "취향에 가까운" 규칙이다. 일부 서브시스템(예: 네트워크, 일부 드라이버)은 `--strict` 깨끗함을 기대한다. 새 드라이버는 처음부터 `--strict`로 맞추면 리뷰 왕복이 줄어든다.
- `Unknown commit id ... maybe rebased or not pulled?` — checkpatch가 **로컬 git에서 sha를 실제로 찾는다**. 사내 트리 해시나 오타를 넣으면 잡힌다. 반대로 말하면 checkpatch는 커널 git 저장소 안에서 돌려야 이 검사가 된다(D7은 git이 아닌 트리에서 돌려서 이 검사가 없었다).

### 5.6 예제 13 — 보내기 전에 빌드: `W=1`로 컴파일 테스트

무엇을 확인하나: checkpatch는 스타일만 본다. 실제로 컴파일되는지, 새 경고가 없는지 확인한다. 랩 defconfig에는 `CONFIG_GPIO_ZYNQ`가 꺼져 있어서, v2 적용 후의 파일을 **out-of-tree 모듈로** 랩 빌드 트리에 대고 `W=1`(추가 경고)로 빌드했다.

```sh
limactl shell elinux -- bash -lc 'grep -n "CONFIG_GPIO_ZYNQ\|CONFIG_ARCH_ZYNQMP=" /var/lab/out/linux-build/.config'
```

```text
386:CONFIG_ARCH_ZYNQMP=y
4493:# CONFIG_GPIO_ZYNQ is not set
4494:CONFIG_GPIO_ZYNQMP_MODEPIN=y
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7 && rm -rf e7_ztest && mkdir e7_ztest &&
  git -C linux show e7_patch:drivers/gpio/gpio-zynq.c > e7_ztest/gpio-zynq.c && echo "obj-m := gpio-zynq.o" > e7_ztest/Kbuild &&
  make -s -C /var/lab/out/linux-build M=/var/tmp/E7/e7_ztest W=1 modules 2>&1 | tail -20; echo "exit=${PIPESTATUS[0]}";
  ls -l e7_ztest/*.ko; modinfo e7_ztest/gpio-zynq.ko | grep -E "license|vermagic|description"'
```

```text
exit=0
-rw-rw-r-- 1 donh donh 118992 Oct 10 13:45 e7_ztest/gpio-zynq.ko
license:        GPL
description:    Zynq GPIO driver
vermagic:       6.18.55 SMP preempt mod_unload aarch64
```

출력에서 볼 것: `-s`(조용히) + `W=1`에서 출력이 한 줄도 없고 exit 0 — 경고 0개다. `match` 변수를 지우지 않았다면 여기서 "unused variable" 경고가 났을 것이다. 실제 제출 전에는 해당 설정을 켜고 트리 안에서 `make drivers/gpio/gpio-zynq.o`로, 가능하면 다른 아키텍처(`ARCH=arm`)로도 빌드한다.

### 5.7 예제 14 — `get_maintainer.pl`로 받는 사람 정하기

무엇을 확인하나: 패치가 건드린 파일 경로로 MAINTAINERS를 찾아 To/Cc 목록을 만든다. D7에서는 binding 경로였고, 이번엔 드라이버 경로 + `Fixes:` 태그가 있는 패치다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7/linux && echo "### get_maintainer (default)"; ./scripts/get_maintainer.pl ../e7_v2/v2-0001-*.patch;
  echo "### --no-rolestats --no-git --no-git-fallback (for --to-cmd)"; ./scripts/get_maintainer.pl --no-rolestats --no-git --no-git-fallback ../e7_v2/v2-0001-*.patch;
  echo "### --scm --no-rolestats"; ./scripts/get_maintainer.pl --scm --no-rolestats ../e7_v2/v2-0001-*.patch | grep -i "^git"'
```

```text
### get_maintainer (default)
Shubhrajyoti Datta <shubhrajyoti.datta@amd.com> (maintainer:XILINX GPIO DRIVER)
Srinivas Neeli <srinivas.neeli@amd.com> (reviewer:XILINX GPIO DRIVER)
Michal Simek <michal.simek@amd.com> (reviewer:XILINX GPIO DRIVER)
Linus Walleij <linus.walleij@linaro.org> (maintainer:GPIO SUBSYSTEM)
Bartosz Golaszewski <brgl@kernel.org> (maintainer:GPIO SUBSYSTEM)
Don H <don@example.com> (blamed_fixes:1/1=100%)
linux-gpio@vger.kernel.org (open list:GPIO SUBSYSTEM)
linux-arm-kernel@lists.infradead.org (moderated list:ARM/ZYNQ ARCHITECTURE)
linux-kernel@vger.kernel.org (open list)
### --no-rolestats --no-git --no-git-fallback (for --to-cmd)
Shubhrajyoti Datta <shubhrajyoti.datta@amd.com>
Srinivas Neeli <srinivas.neeli@amd.com>
Michal Simek <michal.simek@amd.com>
Linus Walleij <linus.walleij@linaro.org>
Bartosz Golaszewski <brgl@kernel.org>
Don H <don@example.com>
linux-gpio@vger.kernel.org
linux-arm-kernel@lists.infradead.org
linux-kernel@vger.kernel.org
### --scm --no-rolestats
git git://git.kernel.org/pub/scm/linux/kernel/git/brgl/linux.git
git https://github.com/Xilinx/linux-xlnx.git
git git://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
```

출력에서 볼 것:

- **세 층**이 나온다: 드라이버 담당(XILINX GPIO DRIVER — maintainer 1, reviewer 2), 서브시스템 담당(GPIO SUBSYSTEM — Linus Walleij, Bartosz Golaszewski), 그리고 메일링 리스트(`linux-gpio`, ARM/ZYNQ, 최후의 `linux-kernel`).
- **`blamed_fixes:1/1=100%`** — get_maintainer는 `Fixes:` 태그를 읽고 **버그를 들여온 커밋의 작성자**도 Cc에 넣는다. 여기서는 그게 나(가상의 버그 커밋 작성자)라서 Don H가 나왔다. 실제로는 원래 작성자가 리뷰하기 가장 좋은 사람이다.
- `--scm`은 이 파일이 머지되는 git 트리를 보여 준다. GPIO는 `brgl/linux.git`(Bartosz의 트리)이다. **패치는 이 트리의 for-next(또는 최신 mainline -rc) 기준으로 만든다.**
- 2.3절의 실제 stable 커밋에서 Bartosz가 maintainer로 `Signed-off-by`를 남긴 것과 정확히 일치한다.
- 실무 관습: 보통 maintainer를 To, reviewer와 리스트를 Cc로 둔다. 몇 명을 고를지는 판단이다 — 서브시스템마다 다르다.

### 5.8 예제 15 — v1과 v2 비교, Fixes 형식 자동화, base-commit

무엇을 확인하나: (1) 리뷰어에게 "무엇이 바뀌었나"를 보여 주는 `git range-diff`, (2) `Fixes:` 줄을 손으로 쓰지 않는 git 설정(문서에 있는 방법), (3) 패치가 어디 위에 만들어졌는지 알리는 `--base`.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7/linux && echo "### range-diff v1 -> v2"; git range-diff e7_bug..effce08 e7_bug..e7_patch'
```

```text
### range-diff v1 -> v2
1:  effce08 ! 1:  5884690 Fixed the PM leak in zynq gpio driver.
    @@ Metadata
     Author: Don H <don@example.com>
     
      ## Commit message ##
    -    Fixed the PM leak in zynq gpio driver.
    +    gpio: zynq: fix runtime PM leak when gpiochip registration fails
     
    -    When gpiochip_add_data fails we return directly and never call pm_runtime_put and pm_runtime_disable, this leaks the usage count so the controller never suspends anymore and the clock stays on.
    +    If gpiochip_add_data() fails, probe returns straight away and skips the
    +    err_pm_put/err_pm_dis unwind labels. The runtime PM usage count taken
    +    earlier by pm_runtime_resume_and_get() is never dropped and runtime PM
    +    is left enabled, so the controller can never runtime-suspend.
     
    -    Fixes: af50089f
    +    Log the error with dev_err_probe() and jump to the existing unwind
    +    labels again.
    +
    +    Fixes: af50089f0070 ("gpio: zynq: use dev_err_probe() for gpiochip registration failure")
    +    Cc: stable@vger.kernel.org
    +    Signed-off-by: Don H <don@example.com>
    +
    +
    + ## Notes ##
    +    Changes in v2:
    +    - Use the err_pm_put unwind label instead of returning (Reviewer A)
    +    - Shorten the error message back to the original text
    +    - Wrap the commit message at 72 columns, add Fixes: and Cc: stable
     
      ## drivers/gpio/gpio-zynq.c ##
     @@ drivers/gpio/gpio-zynq.c: static int zynq_gpio_probe(struct platform_device *pdev)
    @@ drivers/gpio/gpio-zynq.c: static int zynq_gpio_probe(struct platform_device *pde
      	ret = gpiochip_add_data(chip, gpio);
     -	if (ret)
     -		return dev_err_probe(&pdev->dev, ret, "Failed to add gpio chip\n");
    -+	if (ret){
    -+		dev_err_probe(&pdev->dev, ret, "Failed to add gpio chip (gpiochip_add_data returned error), dropping PM ref\n");
    ++	if (ret) {
    ++		dev_err_probe(&pdev->dev, ret, "Failed to add gpio chip\n");
     +		goto err_pm_put;
     +	}
      
-:  ------- > 2:  eaf57ca gpio: zynq: use device_get_match_data()
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7/linux && git config core.abbrev 12 && git config pretty.fixes "Fixes: %h (\"%s\")" &&
  git log -1 --pretty=fixes e7_bug && git format-patch -q --stdout -v2 --base=v6.18.55 -1 e7_patch | tail -7'
```

```text
Fixes: af50089f0070 ("gpio: zynq: use dev_err_probe() for gpiochip registration failure")
base-commit: 98cd6b4d8bb928ea660a9b8302f92b77ccd83bab
prerequisite-patch-id: e4c6d98a0cc7d8d386d8acae519b72671489c3cd
prerequisite-patch-id: 2a840f5edb995cd3d2fb9750416c10ca04cbee13
-- 
2.53.0
```

출력에서 볼 것:

- range-diff의 `!`는 "같은 커밋인데 바뀜", `-:  ------- > 2:`는 "v2에서 새로 생김". 이중 diff(앞 열은 v1→v2, 뒤 열은 원래 diff)라서 처음엔 헷갈리지만, **코드에서 `if (ret){` → `if (ret) {`, 메시지 줄바꿈** 같은 변화가 정확히 보인다. 커버 레터 아래에 붙여 주면 리뷰어가 좋아한다(`git format-patch --range-diff=<v1>`이 자동으로 넣어 준다).
- `pretty.fixes` 설정은 `submitting-patches.rst`에 나오는 방법 그대로다. 12자 sha + 원래 제목을 손으로 옮기다 틀리는 일이 사라진다.
- `--base`는 `base-commit:`(이 시리즈가 올라간 커밋)과 `prerequisite-patch-id:`(그 사이에 필요한 패치들)를 붙인다. CI 봇과 maintainer가 패치를 어디에 적용할지 안다. 여기서는 일부러 패치 2만 뽑아서, 그 앞의 버그 커밋과 패치 1이 prerequisite로 나왔다.

### 5.9 `git send-email`과 b4 — 이 VM에는 없다

무엇을 확인하나: 실제로 메일을 보내지는 않는다. 도구가 있는지만 확인한다.

```sh
limactl shell elinux -- bash -lc 'git send-email --dry-run --to=x@example.com /var/tmp/E7/e7_v2/*.patch; echo "exit=$?";
  dpkg -l git-email 2>&1 | tail -1; which b4 || echo "b4: not installed"'
```

```text
git: 'send-email' is not a git command. See 'git --help'.
exit=1
un  git-email      <none>       <none>       (no description available)
b4: not installed
```

출력에서 볼 것: Ubuntu에서 `send-email`은 별도 패키지 `git-email`이다(상태 `un` = 설치 안 됨). 공용 VM 규칙상 설치하지 않았다. 도구가 있으면 흐름은 이렇다 **[기억 — 이 VM에서 실행하지 않음]**:

```sh
# ~/.gitconfig 의 [sendemail] 에 smtpServer, smtpUser, smtpEncryption 등을 설정한 뒤
git send-email --dry-run \
  --to="Bartosz Golaszewski <brgl@kernel.org>" --to="Linus Walleij <linus.walleij@linaro.org>" \
  --cc=linux-gpio@vger.kernel.org --cc=linux-kernel@vger.kernel.org \
  ../e7_v2/*.patch          # --dry-run: 실제 전송 없이 헤더와 받는 사람만 출력
# 또는 받는 사람을 자동으로:
git send-email --dry-run --to-cmd='./scripts/get_maintainer.pl --norolestats --no-git' ../e7_v2/*.patch
```

- 첫 전송은 **자기 자신에게** 보내 메일 클라이언트에서 줄바꿈·탭이 망가지지 않는지 확인한다(`Documentation/process/email-clients.rst`). Outlook·Gmail 웹 편집기로 패치를 붙여 보내면 탭이 공백으로 바뀌어 `git am`이 실패한다.
- 시리즈는 커버 레터에 나머지가 답장(thread)으로 달린다. send-email이 `In-Reply-To`를 알아서 단다.
- **b4**: kernel.org가 만든 도구로, `submitting-patches.rst`의 Tooling 절이 "dependencies 추적, checkpatch 실행, 메일 형식과 전송"을 자동화한다고 소개한다. 기억 기반으로 자주 쓰는 명령은 `b4 prep`(시리즈 브랜치·커버 레터 관리), `b4 send`(웹 엔드포인트 또는 SMTP로 전송, 버전 자동 증가), `b4 trailers -u`(리스트에 달린 Reviewed-by 등을 커밋에 자동 반영), 그리고 maintainer 쪽 `b4 am <message-id>`(리스트에서 시리즈를 받아 태그 붙여 적용)다 **[기억]**. 문서: https://b4.docs.kernel.org/en/latest/

### 5.10 리뷰 예절과 태그

메일 리뷰의 기본 규칙(`submitting-patches.rst`의 "Use trimmed interleaved replies"):

- **inline(interleaved) 답장**: 리뷰어가 코드 줄 아래에 단 코멘트에, 나도 그 줄 바로 아래에 답한다. 위에 몰아 쓰는 top-posting은 안 된다. 관련 없는 인용은 잘라낸다.
- **plain text 메일만**. HTML 메일은 vger 리스트가 거부한다 **[기억]**.
- 모든 코멘트에 답한다 — "동의, v3에서 고침" 또는 "이유가 있어서 이렇게 했다: …". 무시하고 v3를 보내면 리뷰어가 같은 말을 또 해야 한다.
- v(N+1)는 너무 빨리 보내지 않는다. 보통 리뷰가 모일 때까지 며칠~1주 기다린다 **[기억]**. 머지 윈도 중에는 새 기능 패치를 보내지 않는 서브시스템이 많다(maintainer가 바쁘다).
- 받은 태그는 **다음 버전에 붙여서** 보낸다. 단 그 패치를 크게 바꿨으면 태그를 떼고 그렇게 썼다고 적는다.

| 태그 | 누가 | 뜻 (`submitting-patches.rst` 기준) |
|---|---|---|
| `Signed-off-by:` | 작성자, 그리고 패치를 거쳐 간 모든 maintainer | DCO 동의. 경로 기록 (예제 5의 3단 서명) |
| `Reviewed-by:` | 리뷰어 | 기술적으로 검토했고 문제를 못 찾았다는 공식 진술 (문서에 진술 항목이 있다) |
| `Acked-by:` | 해당 코드의 담당자·이해관계자 | "이 변경에 동의". Reviewed-by보다 덜 형식적, 패치 일부만 ack 할 수도 |
| `Tested-by:` | 테스트한 사람 | 어떤 환경에서 성공적으로 테스트했다 |
| `Reported-by:` + `Closes:` | 버그 보고자 / 보고 링크 | 보고에 대한 크레딧 |
| `Suggested-by:` | 아이디어 제공자 | 패치 2에 Reviewer A를 넣을 수 있다 |
| `Co-developed-by:` | 공동 작성자 | 바로 뒤에 그 사람의 `Signed-off-by` 필수 |
| `Fixes:` | 작성자 | 버그를 들여온 커밋 → stable 선별·get_maintainer에 쓰임 |
| `Link:` | maintainer가 적용할 때 | lore 메일 아카이브 링크 (예제 5의 `patch.msgid.link`) |

### 5.11 stable에 보내는 세 가지 방법

`Documentation/process/stable-kernel-rules.rst` 요약(실제 파일에서 확인):

- 규칙: **mainline에 이미 있거나 동등한 수정이 있어야 한다**, 명백히 맞고 테스트됐어야 한다, **context 포함 100줄 이하**, "진짜 사람을 괴롭히는 버그"(oops, hang, 데이터 손상, 실제 보안 문제, HW quirk, 빌드 에러)이거나 **장치 ID 추가**.
- **옵션 1**: mainline에 보낼 때 sign-off 영역에 `Cc: stable@vger.kernel.org`. mainline에 머지되면 자동으로 stable로 간다. 특정 버전부터만이면 `Cc: <stable@vger.kernel.org> # 3.3.x` 처럼 주석을 단다. 공개 전 취약점은 `stable@kernel.org`(어디에도 배달 안 되는 주소)를 쓴다.
- **옵션 2**: 이미 머지된 커밋이면 stable@vger.kernel.org에 제목·커밋 ID·이유·대상 버전을 메일로.
- **옵션 3**: 직접 backport한 패치를 보낸다. 본문 첫 줄에 `commit <sha1> upstream.` — 예제 5의 ChangeLog에서 본 바로 그 줄이다.

---

## 6. 라이선스 — GPL-2.0-only, SPDX, 모듈, 제품 출하

### 6.1 예제 16 — 커널의 라이선스는 정확히 무엇인가

무엇을 확인하나: `COPYING`, `LICENSES/` 디렉터리, syscall 예외 문구, GPL 호환 판정 함수를 원문으로 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55 && cat COPYING; echo ====; find LICENSES -type f | sort; echo ====;
  sed -n 1,40p LICENSES/exceptions/Linux-syscall-note; echo ====; cat include/linux/license.h'
```

```text
The Linux Kernel is provided under:

	SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note

Being under the terms of the GNU General Public License version 2 only,
according with:

	LICENSES/preferred/GPL-2.0

With an explicit syscall exception, as stated at:

	LICENSES/exceptions/Linux-syscall-note

In addition, other licenses may also apply. Please see:

	Documentation/process/license-rules.rst

for more details.

All contributions to the Linux Kernel are subject to this COPYING file.
====
LICENSES/deprecated/0BSD
LICENSES/deprecated/CC0-1.0
LICENSES/deprecated/GFDL-1.1
LICENSES/deprecated/GFDL-1.2
LICENSES/deprecated/GPL-1.0
LICENSES/deprecated/ISC
LICENSES/deprecated/Linux-OpenIB
LICENSES/deprecated/X11
LICENSES/deprecated/Zlib
LICENSES/dual/Apache-2.0
LICENSES/dual/CC-BY-4.0
LICENSES/dual/CDDL-1.0
LICENSES/dual/MPL-1.1
LICENSES/dual/copyleft-next-0.3.1
LICENSES/exceptions/GCC-exception-2.0
LICENSES/exceptions/Linux-syscall-note
LICENSES/preferred/BSD-2-Clause
LICENSES/preferred/BSD-3-Clause
LICENSES/preferred/BSD-3-Clause-Clear
LICENSES/preferred/GPL-2.0
LICENSES/preferred/LGPL-2.0
LICENSES/preferred/LGPL-2.1
LICENSES/preferred/MIT
====
SPDX-Exception-Identifier: Linux-syscall-note
SPDX-URL: https://spdx.org/licenses/Linux-syscall-note.html
SPDX-Licenses: GPL-2.0, GPL-2.0+, GPL-1.0+, LGPL-2.0, LGPL-2.0+, LGPL-2.1, LGPL-2.1+, GPL-2.0-only, GPL-2.0-or-later
Usage-Guide:
  This exception is used together with one of the above SPDX-Licenses
  to mark user space API (uapi) header files so they can be included
  into non GPL compliant user space application code.
  To use this exception add it with the keyword WITH to one of the
  identifiers in the SPDX-Licenses tag:
    SPDX-License-Identifier: <SPDX-License> WITH Linux-syscall-note
License-Text:

   NOTE! This copyright does *not* cover user programs that use kernel
 services by normal system calls - this is merely considered normal use
 of the kernel, and does *not* fall under the heading of "derived work".
 Also note that the GPL below is copyrighted by the Free Software
 Foundation, but the instance of code that it refers to (the Linux
 kernel) is copyrighted by me and others who actually wrote it.

 Also note that the only valid version of the GPL as far as the kernel
 is concerned is _this_ particular version of the license (ie v2, not
 v2.2 or v3.x or whatever), unless explicitly otherwise stated.

			Linus Torvalds

====
/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __LICENSE_H
#define __LICENSE_H

static inline int license_is_gpl_compatible(const char *license)
{
	return (strcmp(license, "GPL") == 0
		|| strcmp(license, "GPL v2") == 0
		|| strcmp(license, "GPL and additional rights") == 0
		|| strcmp(license, "Dual BSD/GPL") == 0
		|| strcmp(license, "Dual MIT/GPL") == 0
		|| strcmp(license, "Dual MPL/GPL") == 0);
}

#endif
```

출력에서 볼 것:

- 커널 전체는 **GPL 버전 2 only** + **syscall 예외**다. "v2 only"라서 GPLv3 코드는 커널에 넣을 수 없다(Linus의 문구: "v2, not v2.2 or v3.x").
- **syscall 예외**: "정상적인 시스템 콜로 커널 서비스를 쓰는 유저 프로그램은 파생 저작물이 아니다". 그래서 상용 앱을 Linux 위에서 돌리고 소스를 공개하지 않아도 된다. 그리고 이 예외를 **uapi 헤더**(유저가 `#include`하는 커널 헤더)에 SPDX로 표시한다.
- `LICENSES/preferred/`의 7개가 새 코드에 쓰라고 권하는 라이선스, `dual/`은 **GPL과 이중으로만** 쓸 수 있는 것, `deprecated/`는 기존 파일에만 남은 것이다.
- `license_is_gpl_compatible()`이 인정하는 `MODULE_LICENSE` 문자열은 **딱 6개**다. `"GPL"`과 `"GPL v2"`는 둘 다 "GPL v2"를 뜻한다(문자열 `"GPL"`이 "v2 or later"를 뜻하지 않는다는 점이 `license-rules.rst`에 설명돼 있다 **[기억: 해당 절 원문은 이번에 읽지 않음]**).

### 6.2 예제 17 — SPDX 태그 세기

무엇을 확인하나: 커널은 2017년경부터 파일 첫 줄에 `SPDX-License-Identifier:`를 다는 작업을 해 왔다 **[기억]**. 랩 트리에서 실제로 얼마나 붙었고 어떤 식이 많은지 센다.

```python
#!/usr/bin/env python3
# E7: 커널 소스 트리의 SPDX 태그와 MODULE_LICENSE 문자열 세기 (읽기 전용 스캔)
import os, re, sys
from collections import Counter
root = sys.argv[1]
spdx_rx = re.compile(rb'SPDX-License-Identifier:\s*(.+?)\s*(?:\*/|-->|$)', re.M)
ml_rx = re.compile(rb'MODULE_LICENSE\("([^"]+)"\)')
exts = {'.c', '.h', '.S', '.rs', '.py', '.sh', '.pl', '.yaml', '.dts', '.dtsi', '.rst', ''}
ids, mls = Counter(), Counter()
total = tagged = 0
for d, dirs, files in os.walk(root):
    dirs[:] = [x for x in dirs if not x.startswith('.')]
    for fn in files:
        p = os.path.join(d, fn)
        ext = os.path.splitext(fn)[1]
        if fn in ('Makefile', 'Kconfig', 'Kbuild') or ext in exts:
            pass
        else:
            continue
        try:
            with open(p, 'rb') as f:
                head = f.read(4096)  # SPDX는 첫 줄(스크립트는 둘째 줄)에 있어야 한다
                rest = f.read() if ext == '.c' else b''
        except OSError:
            continue
        total += 1
        m = spdx_rx.search(head)
        if m:
            tagged += 1
            ids[m.group(1).decode(errors='replace').strip()] += 1
        for lic in ml_rx.findall(head + rest):
            mls[lic.decode()] += 1
print(f"files scanned={total} with SPDX={tagged} ({100*tagged/total:.1f}%) without={total-tagged}")
print("top SPDX expressions:")
for k, v in ids.most_common(12):
    print(f"{v:7d}  {k}")
print(f"distinct expressions: {len(ids)}")
print("MODULE_LICENSE strings:")
for k, v in mls.most_common():
    print(f"{v:6d}  \"{k}\"")
```

```sh
limactl shell elinux -- bash -lc 'time python3 -I /Users/donh/.elinux_scratch/E7/e7_spdx.py /var/lab/src/linux-6.18.55'
```

```text
files scanned=87471 with SPDX=74929 (85.7%) without=12542
top SPDX expressions:
  28178  GPL-2.0
  18186  GPL-2.0-only
   8851  GPL-2.0-or-later
   3433  (GPL-2.0-only OR BSD-2-Clause)
   3269  GPL-2.0+
   2150  MIT
   1251  (GPL-2.0+ OR MIT)
   1115  GPL-2.0 WITH Linux-syscall-note
    870  (GPL-2.0 OR BSD-2-Clause)
    782  GPL-2.0-only OR BSD-2-Clause
    674  GPL-2.0 OR BSD-3-Clause
    510  GPL-2.0 OR Linux-OpenIB
distinct expressions: 109
MODULE_LICENSE strings:
  8299  "GPL"
  3495  "GPL v2"
   469  "Dual BSD/GPL"
    48  "Dual MIT/GPL"
    48  "GPL and additional rights"
    10  "Dual MPL/GPL"

real	0m31.085s
user	0m2.306s
sys	0m5.583s
```

```svg
<svg viewBox="0 0 680 392" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="20" font-size="13">linux-6.18.55 SPDX-License-Identifier 상위 9개 (스캔 87,471 파일, 태그 74,929)</text>
<text x="242" y="53" text-anchor="end" font-size="12">GPL-2.0</text> <rect x="250" y="38" width="330.0" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.7"/> <text x="586.0" y="53" font-size="12">28,178</text> <text x="242" y="85" text-anchor="end" font-size="12">GPL-2.0-only</text>
<rect x="250" y="70" width="213.0" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.7"/> <text x="469.0" y="85" font-size="12">18,186</text> <text x="242" y="117" text-anchor="end" font-size="12">GPL-2.0-or-later</text> <rect x="250" y="102" width="103.7" height="20" rx="3" fill="#3f9a6b" fill-opacity="0.7"/>
<text x="359.7" y="117" font-size="12">8,851</text> <text x="242" y="149" text-anchor="end" font-size="12">(GPL-2.0-only OR BSD-2-Clause)</text> <rect x="250" y="134" width="40.2" height="20" rx="3" fill="#e08a3c" fill-opacity="0.7"/> <text x="296.2" y="149" font-size="12">3,433</text>
<text x="242" y="181" text-anchor="end" font-size="12">GPL-2.0+</text> <rect x="250" y="166" width="38.3" height="20" rx="3" fill="#3f9a6b" fill-opacity="0.7"/> <text x="294.3" y="181" font-size="12">3,269</text> <text x="242" y="213" text-anchor="end" font-size="12">MIT</text>
<rect x="250" y="198" width="25.2" height="20" rx="3" fill="#3f9a6b" fill-opacity="0.7"/> <text x="281.2" y="213" font-size="12">2,150</text> <text x="242" y="245" text-anchor="end" font-size="12">(GPL-2.0+ OR MIT)</text> <rect x="250" y="230" width="14.7" height="20" rx="3" fill="#e08a3c" fill-opacity="0.7"/>
<text x="270.7" y="245" font-size="12">1,251</text> <text x="242" y="277" text-anchor="end" font-size="12">GPL-2.0 WITH Linux-syscall-note</text> <rect x="250" y="262" width="13.1" height="20" rx="3" fill="#4a7bd0" fill-opacity="0.7"/> <text x="269.1" y="277" font-size="12">1,115</text>
<text x="242" y="309" text-anchor="end" font-size="12">(GPL-2.0 OR BSD-2-Clause)</text> <rect x="250" y="294" width="10.2" height="20" rx="3" fill="#e08a3c" fill-opacity="0.7"/> <text x="266.2" y="309" font-size="12">870</text> <text x="242" y="341" text-anchor="end" font-size="12">SPDX 태그 없음</text>
<rect x="250" y="326" width="146.9" height="20" rx="3" fill="#888" fill-opacity="0.7"/> <text x="402.9" y="341" font-size="12">12,542</text> <rect x="20" y="364" width="14" height="10" fill="#4a7bd0" fill-opacity="0.7"/><text x="40" y="374" font-size="12">GPL v2 only</text> <rect x="185" y="364" width="14" height="10" fill="#3f9a6b" fill-opacity="0.7"/><text x="205" y="374" font-size="12">v2 or later / MIT</text>
<rect x="350" y="364" width="14" height="10" fill="#e08a3c" fill-opacity="0.7"/><text x="370" y="374" font-size="12">이중 라이선스</text> <rect x="515" y="364" width="14" height="10" fill="#888" fill-opacity="0.7"/><text x="535" y="374" font-size="12">태그 없음</text>
</svg>
```

그림 8 — 6.18.55 트리의 SPDX 식 상위 9개와 태그 없는 파일 수. 파랑(v2 only)이 압도적이다.

출력에서 볼 것:

- `GPL-2.0`(28178)과 `GPL-2.0-only`(18186)는 **같은 뜻**이다. SPDX가 나중에 `-only`/`-or-later`를 명시하도록 바꿨고, 커널은 둘 다 받아 준다. `GPL-2.0+` = `GPL-2.0-or-later`도 같은 관계다. 새 파일에는 `GPL-2.0-only`나 `GPL-2.0-or-later`를 쓰는 것이 요즘 관습이다 **[기억]**.
- `(GPL-2.0-only OR BSD-2-Clause)` 3433 + 괄호 없는 같은 식 782 — 대부분 D7의 DT binding과 DTS다. **DT는 다른 OS(BSD, Zephyr, U-Boot)도 써야 해서** 이중 라이선스다.

`GPL-2.0 WITH Linux-syscall-note` 1115개는 정말 uapi 헤더일까? 확인했다:

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55 && grep -rl "SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note" --include=*.h . |
  awk -F/ "{ if (\$0 ~ /uapi/) u++; else o++ } END {print \"uapi:\",u,\" other:\",o}"'
```

```text
uapi: 1104  other: 9
```

1113개 중 1104개(99%)가 `uapi/` 경로에 있다. 6.1에서 본 "uapi 헤더를 표시하는 데 쓴다"가 숫자로 확인된다. (스캔 스크립트의 1115와 2개 차이는 스크립트가 `.h` 말고 다른 확장자도 보기 때문으로 보인다 — 따로 확인하지 않았다.)

나머지 관찰:

- 태그 없는 12542개는 대부분 문서(`.rst`)와 오래된 파일이다 **[추정 — 확장자별로 나눠 보지 않았다]**. 태그가 없는 파일은 `COPYING`에 따라 GPL-2.0이다.
- `MODULE_LICENSE`는 트리 안에 6가지 문자열만 있다 — `license.h`의 6개와 정확히 일치한다. 트리 안 모듈은 전부 GPL 호환이다.

### 6.3 예제 18 — `EXPORT_SYMBOL_GPL`과 빌드 때의 차단

무엇을 확인하나: (1) 커널이 내보내는 심볼 중 GPL 전용이 얼마나 되나, (2) 비GPL 라이선스 모듈이 GPL 전용 심볼을 쓰면 **빌드 단계(modpost)** 에서 막히는지.

```sh
limactl shell elinux -- bash -lc 'grep -wE "ktime_get|_printk|gpiod_get|msleep" /var/lab/out/linux-build/Module.symvers;
  awk "{print \$4}" /var/lab/out/linux-build/Module.symvers | sort | uniq -c'
```

```text
0x00000000	_printk	vmlinux	EXPORT_SYMBOL	
0x00000000	msleep	vmlinux	EXPORT_SYMBOL	
0x00000000	ktime_get	vmlinux	EXPORT_SYMBOL_GPL	
0x00000000	gpiod_get	vmlinux	EXPORT_SYMBOL_GPL	
   8329 EXPORT_SYMBOL
  12449 EXPORT_SYMBOL_GPL
```

출력에서 볼 것: 랩 빌드(vmlinux + 모듈)가 내보낸 심볼 20778개 중 **12449개(60%)가 GPL 전용**이다. `_printk`, `msleep`은 누구나 쓰지만 `ktime_get`(시간), `gpiod_get`(GPIO 소비자 API)은 GPL 모듈만 쓸 수 있다. 새 서브시스템 API는 대부분 `_GPL`로 내보내진다 — 비GPL 드라이버로는 현대 드라이버 프레임워크를 거의 못 쓴다는 뜻이다. (CRC가 0인 것은 랩 설정에 `CONFIG_MODVERSIONS`가 꺼져 있어서다.)

이제 같은 소스를 라이선스 문자열만 바꿔 네 번 빌드한다:

```c
// SPDX-License-Identifier: GPL-2.0-only
/* E7: 같은 소스를 라이선스 문자열만 바꿔 빌드한다 (-DE7_LIC, -DE7_USE_GPL_SYM) */
#include <linux/module.h>
#include <linux/timekeeping.h>
#include <linux/delay.h>

static int __init e7_lic_init(void)
{
#ifdef E7_USE_GPL_SYM
	ktime_t t = ktime_get();		/* EXPORT_SYMBOL_GPL */

	pr_info("e7_lic: license=%s ktime=%lld\n", E7_LIC, ktime_to_ns(t));
#else
	msleep(1);				/* EXPORT_SYMBOL */
	pr_info("e7_lic: license=%s (no GPL-only symbols)\n", E7_LIC);
#endif
	return 0;
}

static void __exit e7_lic_exit(void)
{
	pr_info("e7_lic: bye\n");
}

module_init(e7_lic_init);
module_exit(e7_lic_exit);
MODULE_LICENSE(E7_LIC);
MODULE_DESCRIPTION("E7 license demo");
```

```sh
#!/bin/bash
# usage: build.sh <outdir> <license string> [gplsym]
set -e
O=$1; L=$2; G=$3
rm -rf $O && mkdir -p $O && cp /Users/donh/.elinux_scratch/E7/mod/e7_lic.c $O/
EXTRA="-DE7_LIC='\"$L\"'"; [ -n "$G" ] && EXTRA="$EXTRA -DE7_USE_GPL_SYM"
printf 'obj-m := e7_lic.o\nccflags-y := %s\n' "$EXTRA" > $O/Kbuild
make -s -C /var/lab/out/linux-build M=$O modules
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.elinux_scratch/E7/mod/build.sh; B=/var/tmp/E7/mod
  echo "### GPL + GPL-only symbol"; $S $B/gpl "GPL" y && echo OK
  echo "### Proprietary + GPL-only symbol"; $S $B/propgpl "Proprietary" y; echo "exit=$?"
  echo "### Proprietary, normal symbols only"; $S $B/prop "Proprietary" && echo OK
  echo "### Dual MIT/GPL + GPL-only symbol"; $S $B/dual "Dual MIT/GPL" y && echo OK
  for d in gpl prop dual; do modinfo -F license $B/$d/e7_lic.ko; done'
```

```text
### GPL + GPL-only symbol
OK
### Proprietary + GPL-only symbol
ERROR: modpost: GPL-incompatible module e7_lic.ko uses GPL-only symbol 'ktime_get'
make[3]: *** [/var/lab/src/linux-6.18.55/scripts/Makefile.modpost:147: Module.symvers] Error 1
make[2]: *** [/var/lab/src/linux-6.18.55/Makefile:2000: modpost] Error 2
make[1]: *** [/var/lab/src/linux-6.18.55/Makefile:248: __sub-make] Error 2
make: *** [/var/lab/src/linux-6.18.55/Makefile:248: __sub-make] Error 2
exit=2
### Proprietary, normal symbols only
OK
### Dual MIT/GPL + GPL-only symbol
OK
GPL
Proprietary
Dual MIT/GPL
```

출력에서 볼 것:

- **"Proprietary" + GPL 전용 심볼 = 빌드 실패.** 컴파일은 되지만 링크 정보를 검사하는 `modpost`가 `GPL-incompatible module ... uses GPL-only symbol 'ktime_get'`로 막는다(`scripts/mod/modpost.c`의 에러 문자열).
- `"Dual MIT/GPL"`은 GPL 호환 목록에 있으므로 GPL 전용 심볼을 쓸 수 있다.
- "Proprietary"라도 일반 `EXPORT_SYMBOL`만 쓰면 빌드된다. 다음 예제에서 이걸 로드한다.

### 6.4 예제 19 — QEMU 게스트에서 taint 확인

무엇을 확인하나: 비GPL 모듈을 로드하면 커널이 어떻게 "오염(taint)" 표시를 하는지 랩 커널(6.18.55)에서 본다. 깊은 설명은 E3, 여기서는 라이선스 관점만.

```sh
#!/bin/sh
echo "tainted(before)=$(cat /proc/sys/kernel/tainted)"
insmod /e7_gpl.ko && echo "tainted(after GPL)=$(cat /proc/sys/kernel/tainted) module_taint=[$(cat /sys/module/e7_lic/taint)]"
rmmod e7_lic
insmod /e7_prop.ko && echo "tainted(after Proprietary)=$(cat /proc/sys/kernel/tainted) module_taint=[$(cat /sys/module/e7_lic/taint)]"
dmesg | grep -E "e7_lic|taint"
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E7 && rm -rf e7_rootfs && mkdir e7_rootfs && cp -a /var/lab/build/rootfs/. e7_rootfs/ &&
  cp mod/gpl/e7_lic.ko e7_rootfs/e7_gpl.ko && cp mod/prop/e7_lic.ko e7_rootfs/e7_prop.ko &&
  cp /Users/donh/.elinux_scratch/E7/mod/e7.sh e7_rootfs/e7.sh && chmod +x e7_rootfs/e7.sh &&
  (cd e7_rootfs && find . | cpio -o -H newc --quiet | gzip) > e7_initrd.gz &&
  timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -kernel /var/lab/out/Image \
    -initrd e7_initrd.gz -append "console=ttyAMA0 quiet LAB_CMD=sh~/e7.sh" 2>&1 | tail -15'
```

```text
lab initramfs: 6.18.55 up 5.40s
tainted(before)=0
tainted(after GPL)=4096 module_taint=[O]
tainted(after Proprietary)=4097 module_taint=[PO]
[    7.106912] e7_lic: loading out-of-tree module taints kernel.
[    7.114722] e7_lic: license=GPL ktime=5701434624
[    7.222756] e7_lic: bye
[    7.259693] e7_lic: module license 'Proprietary' taints kernel.
[    7.259833] Disabling lock debugging due to kernel taint
[    7.263377] e7_lic: module license taints kernel.
[    7.269648] e7_lic: license=Proprietary (no GPL-only symbols)
[    7.652195] reboot: Power down
```

출력에서 볼 것:

- GPL 모듈도 **out-of-tree**라서 `O` taint(비트 12 = 4096)가 붙는다. 라이선스와 무관한 표시다.
- Proprietary 모듈은 `P` taint(비트 0 = 1)가 추가되어 4097. **모듈을 내려도 taint는 남는다**(부팅 동안 영구).
- `Disabling lock debugging due to kernel taint` — lockdep 같은 디버그 기능이 꺼진다. 그리고 커뮤니티는 P taint가 있는 커널의 버그 보고를 보통 받지 않는다. **"proprietary 모듈을 쓰면 upstream 지원을 포기한다"** 는 실질적 의미다.
- 비트 번호는 `include/linux/panic.h`의 `TAINT_PROPRIETARY_MODULE 0`, `TAINT_OOT_MODULE 12`에서 확인했다.

### 6.5 경계 그림 — 무엇이 GPL을 따라야 하나

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="230" y="20" width="220" height="58" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="44" text-anchor="middle" font-size="13">DT binding · DTS</text>
  <text x="340" y="66" text-anchor="middle" font-size="12">GPL-2.0 OR BSD-2-Clause</text>
  <rect x="230" y="120" width="220" height="80" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="148" text-anchor="middle" font-size="14">Linux 커널 (vmlinux)</text>
  <text x="340" y="170" text-anchor="middle" font-size="12">GPL-2.0-only</text>
  <text x="340" y="190" text-anchor="middle" font-size="12">+ Linux-syscall-note</text>
  <rect x="15" y="120" width="170" height="80" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="100" y="148" text-anchor="middle" font-size="14">유저 공간 앱</text>
  <text x="100" y="170" text-anchor="middle" font-size="12">라이선스 자유</text>
  <text x="100" y="190" text-anchor="middle" font-size="12">상용 비공개 가능</text>
  <rect x="495" y="20" width="170" height="80" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="580" y="46" text-anchor="middle" font-size="14">GPL 호환 모듈</text>
  <text x="580" y="68" text-anchor="middle" font-size="12">GPL · GPL v2 · Dual …</text>
  <text x="580" y="88" text-anchor="middle" font-size="12">소스 공개 의무</text>
  <rect x="495" y="220" width="170" height="80" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="580" y="246" text-anchor="middle" font-size="14">비GPL 모듈</text>
  <text x="580" y="268" text-anchor="middle" font-size="12">taint P · _GPL 불가</text>
  <text x="580" y="288" text-anchor="middle" font-size="12">법적으로 회색 지대</text>
  <rect x="230" y="250" width="220" height="70" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="276" text-anchor="middle" font-size="14">펌웨어 blob</text>
  <text x="340" y="298" text-anchor="middle" font-size="12">별도 파일 · request_firmware</text>
  <line x1="187" y1="160" x2="228" y2="160" stroke="currentColor" marker-start="url(#ah5)" marker-end="url(#ah5)"/>
  <text x="208" y="112" text-anchor="middle" font-size="12">syscall · uapi 헤더</text>
  <line x1="452" y1="140" x2="493" y2="80" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="470" y="118" font-size="12">모든 export</text>
  <line x1="452" y1="185" x2="493" y2="235" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="470" y="200" font-size="12">EXPORT_SYMBOL만</text>
  <line x1="340" y1="202" x2="340" y2="248" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
  <line x1="340" y1="80" x2="340" y2="118" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
</svg>
```

그림 9 — 라이선스 경계. 초록은 "확실히 괜찮음"(유저 앱은 syscall 예외, GPL 모듈은 공개 전제), 빨강은 "기술적으로는 로드되지만 법적으로 회색"이다. 펌웨어 blob은 커널이 파일로 읽어 장치에 넘기는 별도 저작물로 취급되는 것이 일반적이다 **[기억]**.

### 6.6 "GPL 호환"과 비GPL 모듈 — 무엇이 확실하고 무엇이 아닌가

- **확실한 것(코드로 확인)**: 커널은 `MODULE_LICENSE` 문자열 6개만 GPL 호환으로 본다. 그 외는 `P` taint, GPL 전용 심볼 차단(빌드 때 modpost, 로드 때 `resolve_symbol`의 `gplok`). 7절에서 경로를 본다.
- **기술적 장치의 의미**: `EXPORT_SYMBOL_GPL`은 "이 심볼을 쓰는 코드는 커널 내부와 너무 밀접해서 파생 저작물일 가능성이 높다"는 **개발자들의 의사 표시**다. 반대로 `EXPORT_SYMBOL`만 쓴다고 비GPL 모듈이 합법이 된다는 보장은 없다 **[기억 — 법률 문제, 이 노트는 법률 자문이 아니다]**.
- **`MODULE_LICENSE`를 거짓으로 쓰는 것**(비공개 코드에 `"GPL"`)은 기술적으로 통하지만 라이선스 위반을 자백하는 셈이다. 커널에는 그런 이력이 있는 모듈 이름 목록(`module_license_offenders[]`, 예: `"driverloader"` — 주석이 "caught wrongly pretending to be under GPL")이 있어 이름으로 강제 taint한다(7절 grep의 2519행).
- **실무 결론**: 회사 드라이버는 GPL로 쓰고 upstream하거나 최소한 소스를 제공한다. 정말 숨겨야 하는 알고리즘(예: 센서 퓨전 로직)은 **유저 공간**으로 옮기고 커널 드라이버는 얇은 GPL 데이터 경로로 만든다. GPU·NPU 벤더들이 "얇은 GPL 커널 드라이버 + 두꺼운 유저 공간 라이브러리" 구조를 쓰는 이유다 **[기억]**.

### 6.7 제품을 출하할 때의 의무 (GPLv2 기준 — 법률 자문 아님)

GPLv2 원문(`LICENSES/preferred/GPL-2.0`)의 3절을 실무 언어로 옮기면 **[기억 — 조항 번호와 요지는 기억, 회사 법무팀 확인 필수]**:

| 의무 | 무엇을 | Embedded 제품에서 |
|---|---|---|
| 소스 제공 | 배포한 바이너리(커널 이미지, 모듈)에 **대응하는 완전한 소스** — 우리 수정, 빌드 스크립트·설정(`.config`) 포함 | 펌웨어 이미지에 들어간 커널 트리 전체 + 패치 + defconfig |
| 제공 방법 | (a) 바이너리와 같이 제공, 또는 (b) 최소 3년 유효한 서면 제공 약속(written offer) | 제품 문서·웹 페이지에 "소스 요청 방법"과 다운로드 링크 |
| 라이선스 고지 | GPL 사본과 저작권 고지 | 매뉴얼, 설정 화면의 "오픈소스 라이선스" 메뉴 |
| 수정 사항 | 바꾼 파일에 변경 표시 | git 이력이 사실상 이 역할 |
| 추가 제약 금지 | 받은 사람의 권리를 계약으로 막으면 안 됨 | NDA로 커널 소스 배포를 막는 건 충돌 |

- **해당 없음**: 유저 공간 앱(syscall 예외), 사내에서만 쓰고 배포하지 않는 커널.
- U-Boot도 GPL-2.0+라서 같은 의무가 있고, BusyBox는 GPL 위반 소송으로 유명하다 **[기억]**. Yocto는 `archiver` 클래스로 출하 소스 묶음과 라이선스 목록을 자동으로 만든다 **[기억]** — I 모듈(Yocto)에서 다시 본다.
- 실무 체크: 출하 빌드마다 "커널 소스 tarball + 적용된 패치 + defconfig + 툴체인 버전"을 아카이브한다. 3년 뒤 요청이 와도 **그 바이너리와 정확히 대응하는** 소스를 줄 수 있어야 한다.

---

## 7. 커널 안에서는 무슨 일이 일어나나

이 노트의 대부분(버전, 패치, 리뷰)은 **사람과 git의 일**이고 런타임 커널에는 흔적이 없다. `Fixes:`, `Signed-off-by`, 버전 번호는 실행 중인 커널에 존재하지 않는다(버전 문자열 `UTS_RELEASE` 하나 빼고). 런타임에 실제로 동작하는 것은 **라이선스 검사**다. 예제 19의 insmod가 지나간 길:

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="30" y="16" width="300" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="180" y="39" text-anchor="middle" font-size="13">insmod e7_lic.ko (유저 공간)</text>
  <rect x="30" y="72" width="300" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="180" y="95" text-anchor="middle" font-size="13">finit_module() / init_module() syscall</text>
  <rect x="30" y="128" width="300" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="180" y="151" text-anchor="middle" font-size="13">load_module()</text>
  <rect x="30" y="184" width="300" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="180" y="207" text-anchor="middle" font-size="13">module_augment_kernel_taints()</text>
  <rect x="30" y="240" width="300" height="36" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="180" y="263" text-anchor="middle" font-size="13">module_license_taint_check()</text>
  <rect x="30" y="296" width="300" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="180" y="319" text-anchor="middle" font-size="12">simplify_symbols() → resolve_symbol()</text>
  <rect x="30" y="352" width="300" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="180" y="375" text-anchor="middle" font-size="13">do_init_module() → e7_lic_init()</text>
  <line x1="180" y1="52" x2="180" y2="70" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="180" y1="108" x2="180" y2="126" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="180" y1="164" x2="180" y2="182" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="180" y1="220" x2="180" y2="238" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="180" y1="276" x2="180" y2="294" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="180" y1="332" x2="180" y2="350" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="350" y="30" font-size="12">빌드 때: modpost가 GPL 전용 심볼 사용을 먼저 막음</text>
  <text x="350" y="46" font-size="12">(예제 18의 ERROR)</text>
  <text x="350" y="200" font-size="12">modinfo에 intree 없음 → TAINT_OOT_MODULE (O)</text>
  <text x="350" y="256" font-size="12">license_is_gpl_compatible() 거짓 →</text>
  <text x="350" y="272" font-size="12">TAINT_PROPRIETARY_MODULE (P), 경고 출력</text>
  <text x="350" y="312" font-size="12">gplok = !(mod->taints &amp; P 비트)</text>
  <text x="350" y="328" font-size="12">→ P면 GPL_ONLY 심볼은 찾지 않음</text>
</svg>
```

그림 10 — 모듈 로드 중 라이선스가 검사되는 지점(6.18.55 `kernel/module/main.c`). taint가 먼저 정해지고, 그다음 심볼 해석에서 그 taint로 GPL 전용 심볼을 숨긴다.

코드로 확인한 순서:

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55/kernel/module && grep -n "^static int load_module\|^SYSCALL_DEFINE3(finit_module\|module_augment_kernel_taints(mod\|simplify_symbols(mod\|setup_modinfo(mod\|static int init_module_from_file\|static int idempotent_init_module\|load_module(&info\|^static const struct kernel_symbol \*resolve_symbol\|Unknown symbol\|inherit_taint(" main.c | head -30;
  grep -n "TAINT_PROPRIETARY_MODULE\|TAINT_OOT_MODULE" /var/lab/src/linux-6.18.55/include/linux/panic.h'
```

```text
1211:static bool inherit_taint(struct module *mod, struct module *owner, const char *name)
1231:static const struct kernel_symbol *resolve_symbol(struct module *mod,
1256:	if (!inherit_taint(mod, fsa.owner, name)) {
1295:			!IS_ERR(ksym = resolve_symbol(mod, info, name, owner))
1566:			pr_warn("%s: Unknown symbol %s (err %d)\n",
3370:static int load_module(struct load_info *info, const char __user *uargs,
3427:	module_augment_kernel_taints(mod, info);
3454:	err = setup_modinfo(mod, info);
3459:	err = simplify_symbols(mod, info);
3602:	return load_module(&info, uargs, 0);
3689:static int init_module_from_file(struct file *f, const char __user * uargs, int flags)
3714:	return load_module(&info, uargs, flags);
3717:static int idempotent_init_module(struct file *f, const char __user * uargs, int flags)
3736:SYSCALL_DEFINE3(finit_module, int, fd, const char __user *, uargs, int, flags)
63:#define TAINT_PROPRIETARY_MODULE	0
75:#define TAINT_OOT_MODULE		12
```

출력에서 볼 것:

- `finit_module` → `idempotent_init_module` → `init_module_from_file` → `load_module`(3370). 그 안에서 **taint 결정(3427)이 심볼 해석(3459)보다 먼저**다.
GPL 전용 심볼을 숨기는 곳과 taint 상속을 grep으로 확인:

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55/kernel/module && grep -n "gplok = \|syms->license == GPL_ONLY\|license_is_gpl_compatible\|module_license_offenders\[\] =\|from proprietary module" main.c'
```

```text
368:	if (!fsa->gplok && syms->license == GPL_ONLY)
1217:		pr_err("%s: module using GPL-only symbols uses symbols %s from proprietary module %s.\n",
1223:		pr_warn("%s: module uses symbols %s from proprietary module %s, inheriting taint.\n",
1754:	if (!license_is_gpl_compatible(license)) {
2519:static const char *const module_license_offenders[] = {
```

- 368행: `find_exported_symbol_in_section()`은 `gplok`가 거짓이면 GPL 전용 심볼을 아예 "없는 것"처럼 만든다. 그래서 modpost를 우회해 억지로 만든 모듈은 로드 때 `Unknown symbol`(1566행)로 실패한다.
- 1217·1223행(`inherit_taint()`): 어떤 모듈이 **P taint 모듈이 내보낸 심볼**을 쓰면, 그 모듈이 GPL 전용 심볼도 쓰는 경우 거부하고(1217), 아니면 P taint를 **물려받는다**(1223). "GPL 껍데기 모듈로 비GPL 모듈을 감싸 GPL 심볼을 쓰는" 우회를 막는 장치다.

**드라이버를 쓸 때 다시 만나는 곳**: E3(모듈 로드·`EXPORT_SYMBOL`·taint 전반), F 모듈(platform driver의 `MODULE_LICENSE`·`MODULE_DEVICE_TABLE`), 그리고 모든 드라이버 파일 첫 줄의 SPDX. 패치 흐름(5절)은 F~H에서 드라이버를 쓰고 upstream할 때 그대로 쓴다.

---

## 8. Embedded 실무에서 다시 보기

### 8.1 "BSP 5.15를 6.x로 올려라" (contents.md 시나리오)

센서 키트가 NXP i.MX8MP + 벤더 BSP 5.15 기반이라고 하자. 2026-12 EOL(예제 4)이 다가온다.

1. **목표 정하기**: 벤더의 새 BSP(예: 6.6 또는 6.12 기반)가 있나? i.MX8MP는 upstream 보드가 56개(예제 7)라 6.18 LTS 직행도 후보다.
2. **우리 패치 목록화**: `git log --oneline <벤더 태그>..HEAD`로 우리 커밋만 뽑고, 각 커밋을 "upstream에 이미 있음 / 새 BSP에 있음 / 우리 전용"으로 분류한다(4.4절 접두어가 있으면 빠르다).
3. **옮기기**: `git rebase --onto <새 base> <옛 base>` 또는 `git cherry-pick`, 끝나면 `git range-diff`로 전후 비교(예제 9).
4. **DT 맞추기**: 5.15 DTS의 비표준 속성이 새 커널 binding과 안 맞는 경우가 많다 → `make CHECK_DTBS=y`(D7).
5. **검증**: 부팅, 드라이버별 기능 테스트, 성능(카메라 fps, IMU 지연) 회귀 비교.

### 8.2 upstream 수정을 우리 제품 커널로 가져오기

현장에서 "IMU 드라이버가 가끔 runtime PM이 안 꺼져 전류가 샌다"는 보고가 왔다. 조사해 보니 mainline에 같은 증상의 수정이 있다(예제 5의 gpio-zynq 커밋처럼). 할 일: `git cherry-pick -x <upstream sha>`(출처 줄 자동 추가) → 충돌 있으면 해결하고 본문에 `[ Don: adjusted context for 6.6 ]` 같은 메모 → 그 수정의 `Fixes:` 대상 커밋이 우리 트리에 있는지 `git merge-base --is-ancestor`로 확인. stable을 정기적으로 머지하는 트리라면 대부분 이미 들어와 있다 — 이것이 stable을 따라가는 가치다.

### 8.3 우리 버그 수정을 upstream에 보내기

우리 보드에서 찾은 드라이버 버그를 고쳤다면, 제품 트리에만 두지 말고 5절 흐름으로 mainline에 보낸다. 이유: (1) 다음 업그레이드 때 우리 패치가 하나 줄어든다(예제 9의 B처럼 자동 drop), (2) `Cc: stable`을 달면 모든 LTS로 퍼져 우리 다음 제품도 혜택을 받는다, (3) 리뷰로 더 나은 수정을 얻는다. 회사의 오픈소스 기여 승인 절차(DCO (a)항)를 먼저 확인한다.

### 8.4 출하 체크리스트에 라이선스 넣기

CI에서 출하 빌드마다: 커널 `.config`와 소스 tarball 보관, `modinfo -F license`로 모든 `.ko`의 라이선스 나열해 비GPL이 있으면 경고, 부팅 후 `/proc/sys/kernel/tainted`가 0이 아닌지 확인(예제 19). 제품 펌웨어에 P taint가 있다면 그 모듈이 무엇인지 법무와 확인한다.

```sh
# 출하 이미지의 모듈 라이선스 점검 (예: 이 노트의 모듈들에 적용)
limactl shell elinux -- bash -lc 'for k in /var/tmp/E7/mod/*/e7_lic.ko /var/tmp/E7/e7_ztest/gpio-zynq.ko; do
  [ -f "$k" ] && printf "%-40s %s\n" "$k" "$(modinfo -F license $k)"; done'
```

```text
/var/tmp/E7/mod/dual/e7_lic.ko           Dual MIT/GPL
/var/tmp/E7/mod/gpl/e7_lic.ko            GPL
/var/tmp/E7/mod/prop/e7_lic.ko           Proprietary
/var/tmp/E7/e7_ztest/gpio-zynq.ko        GPL
```

출력에서 볼 것: `propgpl`은 빌드에 실패해 `.ko`가 없으므로 목록에 없다. "Proprietary"가 하나라도 보이면 CI가 실패하게 만든다.

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 제품에 non-LTS stable(예: 7.1) 사용 | 3개월 뒤 EOL, 보안 수정 끊김 | stable과 longterm 혼동 | 예제 4처럼 수명 확인, LTS 사용 |
| EOL을 기억으로 말함 | 계획이 틀림 (projected EOL은 바뀜) | 연장·변경을 모름 | `releases.json`, `releases.html` 확인 |
| 벤더 BSP를 몇 년간 stable 머지 안 함 | 수천 개 수정 누락, CVE 대응 불가 | "동작하니 건드리지 마라" | 정기 stable 머지·rebase를 일정에 넣음 |
| backport에 upstream sha를 안 남김 | 업그레이드 때 중복·충돌 원인 추적 불가 | `cherry-pick` 시 `-x` 안 씀 | `-x`, `commit <sha> upstream.` |
| `Fixes:`에 짧은 sha·제목 없음 | checkpatch WARNING, stable 선별 누락 | 형식 모름 | `pretty.fixes` 설정 (예제 15) |
| 커버 레터 `*** SUBJECT HERE ***` 그대로 | 리스트에서 바로 지적 | 채우는 걸 잊음 | 보내기 전 `--dry-run`과 grep |
| 버그 수정과 정리를 한 패치에 | stable에 못 감, 리뷰 어려움 | "논리 변경 1개 = 패치 1개" 위반 | 수정을 앞에, 정리는 별도 패치 (예제 11) |
| 리뷰에 top-posting·HTML 메일 | 리스트 거부, 무시됨 | 메일 문화 모름 | plain text, inline 답장 |
| `MODULE_LICENSE` 누락 | 로드 시 `module license 'unspecified' taints kernel` | 매크로 빠뜨림 | 파일 첫 줄 SPDX와 맞는 `MODULE_LICENSE` |
| 비GPL 모듈에서 `_GPL` 심볼 사용 | modpost `GPL-incompatible module ... uses GPL-only symbol` | 라이선스와 API 불일치 | GPL로 바꾸거나 로직을 유저 공간으로 |

---

## 10. 면접에서 이렇게 말한다

**Q.** Which kernel would you choose for a new product and why?

**A.** 먼저 SoC의 upstream 상태를 본다. mainline에 SoC DT와 우리에게 필요한 블록(카메라, NPU 등)이 있으면 그 해의 최신 LTS(지금이면 6.18, projected EOL 2028-12)를 직접 쓰고 stable point release를 매주~매월 머지한다. upstream이 부족하면 벤더 BSP로 시작하되, 벤더가 stable을 얼마나 자주 따라가는지 확인하고, 우리 코드는 upstream 스타일로 써서 나중에 옮길 수 있게 한다. 제품 수명이 10년이면 CIP SLTS와 맞는 버전을 고른다.

> I start from the SoC's upstream status. If mainline already supports the SoC and the blocks we actually need, I pick the newest LTS — today that's 6.18, projected EOL December 2028 — and merge stable point releases on a regular cadence, so security fixes come for free. If upstream support isn't there yet, I start on the vendor BSP to get hardware running, but I check how often the vendor rebases onto stable, and I keep our own drivers and device tree upstream-clean so we can move later. For a ten-year product I'd align with a CIP SLTS version.

**꼬리 질문**: "벤더 BSP는 6.6인데 6.18로 가고 싶다면 무엇부터 확인하나?" → 우리 패치 목록 분류, 필요한 드라이버의 upstream 여부, DT binding 차이, 성능 회귀 테스트 계획.

**Q.** Explain the Linux kernel release process.

**A.** 정식 릴리스 다음 날부터 2주가 merge window로, 이때만 새 기능이 Linus 트리에 들어간다. 그 뒤 매주 -rc를 내며 7주 정도 버그만 고치고, 보통 rc7 다음 주에 릴리스한다. 그래서 9~10주 사이클이다(직접 재 보니 32사이클 평균 64.8일). 릴리스된 버전은 stable 팀이 mainline 수정만 backport하는 stable이 되고, 1년에 하나 정도가 LTS가 된다.

> After each release there's a two-week merge window when new features are pulled into Linus's tree. Then there's a weekly -rc cycle of bug fixes only, usually up to rc7, so a release comes every nine or ten weeks. Once released, a version becomes a stable branch where the stable team backports fixes that are already in mainline, and roughly one release a year is picked as a long-term kernel.

**Q.** What makes a good kernel patch?

**A.** 논리 변경 하나, 서브시스템 접두어가 있는 명령형 제목, "무엇이 왜 문제고 어떻게 고쳤나"를 쓴 본문, 버그 수정이면 12자 sha의 `Fixes:`와 필요하면 `Cc: stable`, `Signed-off-by`. 보내기 전에 checkpatch `--strict`, `W=1` 빌드, get_maintainer로 받는 사람을 정한다. 리뷰에는 inline으로 답하고 v2에는 `---` 아래 변경 이력을 단다.

> One logical change per patch, a subject with the subsystem prefix in imperative mood, and a body that explains what was wrong, why it matters and how it's fixed. For a bug fix I add a Fixes tag with the twelve-character SHA and the original subject, plus Cc stable when it should be backported, and my Signed-off-by. Before sending I run checkpatch with --strict, build with W=1, and use get_maintainer to pick recipients. In v2 I keep a changelog below the three dashes and reply to every review comment inline.

**Q.** What is Signed-off-by and why does it matter?

**A.** Developer's Certificate of Origin에 동의한다는 서명이다. 내가 이 코드를 이 라이선스로 낼 권리가 있다는 법적 진술이고, 패치를 거쳐 간 maintainer들도 서명을 쌓아 경로가 남는다. 회사 코드라면 회사가 기여를 허락했는지가 그 권리의 실체다.

> It certifies the Developer's Certificate of Origin — that I have the right to submit this code under the file's license. Every maintainer who handles the patch adds their own sign-off, so the chain of custody is recorded. For company code it means the company has actually approved the contribution.

**Q.** Can you ship a proprietary kernel module?

**A.** 기술적으로는 로드된다. 하지만 커널은 P taint를 걸고, `EXPORT_SYMBOL_GPL` 심볼은 빌드 때 modpost가, 로드 때 심볼 해석이 막는다 — 현대 API의 60% 정도가 GPL 전용이다. 커뮤니티는 tainted 커널의 버그를 지원하지 않고, 법적으로도 파생 저작물 논란이 있다. 그래서 커널 쪽은 GPL로 얇게, 비공개 로직은 유저 공간으로 옮기는 설계를 권한다. 최종 판단은 법무팀이다.

> Technically it loads, but the kernel marks itself tainted with the P flag, and GPL-only exports — about sixty percent of exported symbols in my build — are blocked at build time by modpost and at load time by the symbol resolver. The community won't debug tainted kernels, and there's a real derivative-work question. So I'd keep the kernel part a thin GPL driver and move the proprietary logic to user space, and let legal make the final call.

**Q.** What are your GPL obligations when shipping a Linux-based device?

**A.** 출하한 커널·모듈 바이너리에 대응하는 완전한 소스(우리 수정, 설정, 빌드 스크립트)를 같이 주거나 서면 제공 약속을 하고, GPL 사본과 고지를 넣는다. 유저 앱은 syscall 예외 덕분에 해당 없다. 실무에서는 출하 빌드마다 소스와 `.config`를 아카이브한다.

> We have to provide the complete corresponding source for the kernel and modules we ship — including our changes, the config and build scripts — either with the device or through a written offer, along with the license text and notices. User-space applications aren't covered because of the syscall exception. In practice I archive the exact source tree and config for every release build.

**Q.** How do you keep a vendor kernel up to date with security fixes?

**A.** stable point release를 정기적으로 머지하거나 벤더 패치 스택을 새 stable 태그 위로 rebase한다. 이미 upstream에 있는 backport는 git이 patch-id로 버리고, 충돌은 사람이 의미를 판단한다. backport에 upstream sha를 남기고, 우리 수정은 upstream에 보내 스택을 줄인다. `range-diff`로 전후를 리뷰한다.

> I merge the stable point releases on a schedule, or rebase the vendor stack onto each new stable tag. Backports that are already upstream drop out automatically by patch-id; real conflicts need someone to decide which fix is right. Every backport carries its upstream SHA, our own fixes go upstream so the stack shrinks, and I review the result with git range-diff.

**꼬리 질문**: "stable에 들어가는 패치의 규칙은?" → mainline에 먼저, 명백히 맞고 테스트됨, context 포함 100줄 이하, 실제 버그나 장치 ID 추가.

---

## 11. 직접 해보기

1. (개념) `releases.json`을 다시 받아 오늘의 mainline·stable·longterm을 적고, 이 노트(2026-10-10)와 무엇이 달라졌는지 비교하라. 5.10·5.15가 아직 있나?
   정답/힌트: `curl -s https://www.kernel.org/releases.json` 후 예제 1의 파이썬. 2026-12 이후라면 두 LTS가 `iseol: true`이거나 목록에서 빠졌을 것이다.

2. (측정) 예제 2를 고쳐 "rc1 → 릴리스" 간격을 재 보라. 디렉터리 목록에는 rc tarball이 없으니 `git.kernel.org`의 `torvalds/t/linux-X.Y-rc1.tar.gz` 대신 `releases-calendar.ics` 또는 `git ls-remote --tags https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git`을 써라.
   정답/힌트: `ls-remote --tags`는 태그 이름만 주므로 날짜는 `git log -1 --format=%ci <tag>`가 필요하다 — 얕은 fetch 하나면 된다. 7주(49일)가 나와야 한다.

3. (코드) 예제 9에 네 번째 벤더 커밋을 추가하라: 6.18.54→55 패치가 건드리지 않는 `drivers/gpio/gpio-zynq.c` 함수에 `dev_info` 한 줄. rebase 결과가 A와 같은지 range-diff로 확인하라.
   정답/힌트: `=`로 표시되어야 한다. 같은 파일이어도 다른 hunk면 충돌하지 않는다.

4. (코드) 예제 10의 v1에 `checkpatch.pl --fix-inplace`를 적용해 보고, 무엇이 자동으로 고쳐지고 무엇이 안 고쳐지는지 적어라.
   정답/힌트: `if (ret){` 공백은 고쳐지고, 긴 줄·커밋 메시지·Signed-off-by는 사람이 고쳐야 한다. 패치 파일이 아니라 `-f <파일>` 모드로 소스에 직접 쓴다.

5. (관찰) 예제 19의 initramfs에 `e7_dual.ko`("Dual MIT/GPL" + `ktime_get`)를 넣어 부팅하고 `/proc/sys/kernel/tainted`와 `/sys/module/e7_lic/taint`를 확인하라.
   정답/힌트: `O`만(4096). Dual MIT/GPL은 GPL 호환이므로 P가 없다.

6. (개념) 다음 SPDX 식을 뜻으로 풀어라: `GPL-2.0+ OR MIT`, `GPL-2.0 WITH Linux-syscall-note`, `GPL-2.0-only OR BSD-2-Clause`. 각각이 트리의 어떤 종류 파일에 주로 쓰이는지 예제 17 결과로 추정하라.
   정답/힌트: 셋째는 DT(D7). 둘째는 uapi 헤더(1104/1113). 첫째는 DRM 드라이버처럼 다른 OS와 코드를 공유하는 쪽 — `grep -rl "GPL-2.0+ OR MIT" | cut -d/ -f2-3 | sort | uniq -c | sort -rn | head`로 확인.

7. (실무) `get_maintainer.pl`을 `drivers/iio/imu/inv_mpu6050/inv_mpu_core.c`를 건드리는 가상의 패치에 돌려 IIO 쪽 받는 사람을 찾고, `--scm`으로 어느 트리 기준으로 패치를 만들어야 하는지 답하라.
   정답/힌트: `./scripts/get_maintainer.pl -f drivers/iio/imu/inv_mpu6050/inv_mpu_core.c`. D7 예제 15의 IIO maintainer(Jonathan Cameron)가 나온다.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| mainline | Linus의 트리 | 새 기능이 들어가는 유일한 곳 |
| merge window | 머지 윈도 | 릴리스 직후 약 2주, 새 기능을 pull 하는 기간 |
| -rc | release candidate | 머지 윈도 뒤 매주 나오는 안정화 스냅샷 |
| linux-next | 통합 테스트 트리 | 서브시스템 트리들을 매일 합친 것 |
| stable | 안정 브랜치 | 막 나온 X.Y에 mainline 수정을 backport, 약 3개월 |
| longterm (LTS) | 장기 지원 | 1년에 하나쯤, 2년 이상(현재 최대 6년) 유지 |
| point release | X.Y.Z | stable/LTS의 수정 릴리스, 보통 주 1회 |
| projected EOL | 예상 지원 종료 | 바뀔 수 있는 EOL 날짜 |
| BSP | Board Support Package | SoC 벤더가 주는 커널·부트로더·DT 묶음 |
| GKI | Generic Kernel Image | Android 공통 커널 + 벤더 모듈 구조 |
| CIP SLTS | Super Long Term Support | 산업용, LTS를 10년 유지 |
| backport | 역이식 | 새 버전의 수정을 옛 버전에 적용 |
| patch-id | 패치 내용 해시 | rebase가 "이미 upstream"을 알아보는 기준 |
| range-diff | 스택 비교 | 두 커밋 시리즈를 짝지어 비교 |
| DCO | Developer's Certificate of Origin | `Signed-off-by`로 동의하는 기여 증명 |
| `Fixes:` | 원인 커밋 태그 | 12자 sha + 제목, stable 선별에 쓰임 |
| cover letter | 0/N 메일 | 시리즈 전체 설명과 버전 이력 |
| lore | lore.kernel.org | 커널 메일링 리스트 공개 아카이브 |
| SPDX | 라이선스 식별자 표준 | 파일 첫 줄의 `SPDX-License-Identifier:` |
| Linux-syscall-note | syscall 예외 | 유저 프로그램은 파생 저작물이 아님 |
| `EXPORT_SYMBOL_GPL` | GPL 전용 export | GPL 호환 모듈만 쓸 수 있는 심볼 |
| taint | 오염 표시 | P(비GPL), O(out-of-tree) 등 비트 플래그 |
| written offer | 서면 제공 약속 | 소스를 나중에 제공하겠다는 GPL 방식 |

---

## 13. 요약 & 체크리스트

커널은 2주 merge window + 매주 -rc 7주로 9~10주마다 릴리스된다(직접 잰 32사이클 평균 64.8일). 릴리스된 버전은 stable이 되어 mainline 수정만 backport 받다가 약 3개월에 끝나고, 해마다 마지막 릴리스쯤이 LTS가 되어 몇 년(현재 5.10·5.15는 2026-12, 6.6·6.1은 2027-12, 6.18·6.12는 2028-12 예상)을 산다. LTS 하나에 매주 약 400개 수정이 들어가며 거의 전부가 `Fixes:`가 달린 mainline 커밋의 복사본이다. 벤더 BSP·Android GKI·CIP SLTS는 LTS에서 갈라진 트리다. 제품 커널은 SoC upstream 상태, backport 부담, 보안 수정 경로, 제품 수명 대 EOL로 고른다. 벤더 스택을 새 stable로 옮기면 같은 backport는 patch-id로 빠지고 의미 충돌만 사람이 판단한다. 패치는 논리 변경 하나, `subsystem: component:` 제목, 이유를 쓴 본문, 12자 `Fixes:`, `Cc: stable`, `Signed-off-by`(DCO)로 만들고 checkpatch `--strict`·`W=1` 빌드·get_maintainer·send-email `--dry-run`을 거쳐 보내며, 리뷰엔 inline으로 답한다. 커널은 GPL-2.0-only + syscall 예외이고, `MODULE_LICENSE` 6개 문자열만 GPL 호환으로 인정되어 나머지는 P taint와 GPL 전용 심볼 차단을 받는다. 제품을 출하하면 대응 소스를 제공해야 한다.

- [ ] merge window, -rc, 릴리스 주기를 날짜 예와 함께 설명할 수 있다
- [ ] `releases.json`으로 현재 LTS 목록과 EOL을 확인할 수 있다
- [ ] stable과 longterm의 차이, 그리고 non-LTS stable을 제품에 쓰면 안 되는 이유를 숫자로 말할 수 있다
- [ ] Android GKI와 CIP SLTS가 LTS와 어떤 관계인지 설명할 수 있다
- [ ] "벤더 6.6 BSP vs 6.18 LTS" 결정을 네 가지 축으로 설명할 수 있다
- [ ] 벤더 패치 스택을 새 stable 태그로 rebase하고 range-diff로 검토할 수 있다
- [ ] 좋은 커밋 메시지(제목 접두어, 본문, Fixes:, Cc: stable, Signed-off-by)를 직접 쓸 수 있다
- [ ] `format-patch -v2 --cover-letter --notes`로 시리즈를 만들고 checkpatch `--strict`를 통과시킬 수 있다
- [ ] get_maintainer 출력에서 maintainer·reviewer·리스트·`blamed_fixes`를 읽을 수 있다
- [ ] Reviewed-by, Acked-by, Tested-by의 차이와 리뷰 예절을 말할 수 있다
- [ ] GPL-2.0-only, SPDX, syscall 예외, `MODULE_LICENSE`, `EXPORT_SYMBOL_GPL`, taint의 관계를 설명할 수 있다
- [ ] 제품 출하 때 GPL 의무와 실무 체크리스트를 말할 수 있다

---

## 참고 자료

- kernel.org 릴리스 정보: https://www.kernel.org/category/releases.html , https://www.kernel.org/releases.json , https://www.kernel.org/releases-calendar.ics
- 커널 문서 (소스 트리 `Documentation/process/`, docs.kernel.org에 같은 내용): `2.Process.rst`(개발 주기·merge window), `submitting-patches.rst`(커밋 메시지, DCO, 태그, interleaved replies, b4 소개), `stable-kernel-rules.rst`(stable 규칙과 옵션 1~3), `5.Posting.rst`, `email-clients.rst`, `license-rules.rst`, `backporting.rst`, `maintainer-soc.rst`
- https://docs.kernel.org/process/submitting-patches.html , https://docs.kernel.org/process/stable-kernel-rules.html , https://docs.kernel.org/process/license-rules.html
- 라이선스 원문: 소스 트리 `COPYING`, `LICENSES/preferred/GPL-2.0`, `LICENSES/exceptions/Linux-syscall-note`, SPDX 목록 https://spdx.org/licenses/
- 코드: `include/linux/license.h`, `kernel/module/main.c`(`module_license_taint_check`, `resolve_symbol`, `inherit_taint`), `scripts/mod/modpost.c`, `scripts/checkpatch.pl`, `scripts/get_maintainer.pl` — elixir.bootlin.com에서 버전별로 볼 수 있다
- b4 문서: https://b4.docs.kernel.org/en/latest/
- 공개 메일 아카이브: https://lore.kernel.org/
- CIP 프로젝트: https://www.cip-project.org/ (SLTS 설명), 커널 트리 https://git.kernel.org/pub/scm/linux/kernel/git/cip/linux-cip.git
- Android 공통 커널: https://android.googlesource.com/kernel/common
- 관련 노트: E1(소스 트리), E2(빌드), E3(모듈·taint), D7(binding 패치·checkpatch·get_maintainer), I 모듈(Yocto 라이선스 관리)
