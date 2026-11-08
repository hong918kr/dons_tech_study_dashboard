---
id: SIV-M11
time: 90분
updated: 2026-11-08
---
# SIV-M11 Bisect 자동화: 펌웨어 커밋과 파라미터

## 학습 목표

- `git bisect run` 의 종료 코드 규약(0 good · 1–127 bad · 125 skip · 128 이상 abort)에 맞춰 **러너(runner)** 를 짜고, 빌드 실패는 skip, 보드·장비 문제는 abort로 보내 "엉뚱한 커밋을 범인으로 지목"하는 일을 막을 수 있다.
- 일회용 git 저장소를 `tempfile.TemporaryDirectory` 안에 만들고 `GIT_CONFIG_GLOBAL=/dev/null` 로 격리해, 회귀(regression)를 심은 커밋을 **진짜 `git bisect run`** 으로 찾아낸다.
- 전압·주파수·버퍼 크기 같은 **파라미터 경계** 를 하나의 일반 함수(단조 술어, 방향·격자·전제 조건 검사)로 bisect한다.
- flaky 테스트에서 1회 판정 bisect가 오답을 내는 비율을 결정적 seed로 **측정** 하고, 다수결 · retry-until-pass · SPRT(순차 확률비 검정)를 오답률과 실행 횟수로 비교할 수 있다.
- 찾은 범인을 이항 검정으로 **확정(confirm)** 하고, 디버그 엔지니어가 바로 움직일 수 있는 bisect 리포트를 쓴다.

## 1. 문제

야간 회귀(nightly regression)에서 "어제까지 되던 DMA 루프백이 오늘 30% 확률로 깨진다"는 결과가 나오면, 검증 엔지니어가 아침에 가장 먼저 할 일은 **어느 변경이 깨뜨렸는지** 를 사람 손 없이 좁히는 것이다. 면접에서도 "flaky 테스트로 bisect하면 무슨 일이 생기나?", "Vmin 경계는 어떻게 찾나?" 가 자주 나온다.

bisect는 단순한 이진 탐색이지만 실리콘 검증에서는 세 가지가 어렵다. ① 테스트가 **flaky** 하다 (보드·링크·온도 때문에 좋은 커밋도 가끔 실패). ② 회귀 자체가 **간헐적** 이다 (워크로드에 따라 가끔만 터진다). ③ 중간 커밋이 **빌드가 안 되거나**, 보드가 응답하지 않는 등 *테스트할 수 없는* 상황이 섞인다. 1회 판정 bisect는 이 중 하나만 있어도 엉뚱한 커밋을 지목하고, 그 커밋 작성자는 억울한 메일을 받는다.

이 레슨의 실험실은 가상의 SoC **ORCA-A0** 다. 펌웨어는 `fw.py` 한 파일로 흉내 낸다 (`split_transfer(nbytes)` 가 전송을 DMA 디스크립터 조각으로 나눈다). 하드웨어는 mock DUT 함수 `dut_dma()` 다.

| mock DUT 규칙 (ORCA-A0, 가상) | 위반하면 |
|---|---|
| 디스크립터 길이는 1..4096 B | `desc[i] len=0 illegal` 또는 `len=8192 > 4096` |
| 조각 길이의 합 = 요청 길이 | `len sum ... != ...` |
| 매 실행마다 확률 `flake` 로 링크 글리치 (펌웨어와 무관) | 그 실행만 FAIL |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="sivm11-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">git bisect run: 커밋마다 러너를 부르고, 종료 코드로 다음 커밋을 고른다</text>
  <g stroke="currentColor">
    <line x1="20" y1="70" x2="330" y2="70"/>
    <circle cx="20" cy="70" r="8" class="f-ok"/><circle cx="80" cy="70" r="6" class="f-mut"/>
    <circle cx="140" cy="70" r="8" class="f-ok"/><circle cx="185" cy="70" r="8" class="f-ok"/>
    <circle cx="205" cy="70" r="8" class="f-ok"/><circle cx="230" cy="70" r="8" class="f-warn"/>
    <circle cx="280" cy="70" r="6" class="f-mut"/><circle cx="330" cy="70" r="8" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="20" y="98">c00</text><text x="140" y="98">c07</text><text x="190" y="112">c09 c10</text>
    <text x="230" y="98">c11</text><text x="330" y="98">c15</text>
    <text x="20" y="50">good</text><text x="330" y="50">bad</text>
    <text x="140" y="50">①</text><text x="230" y="50">②</text><text x="185" y="50">③</text><text x="207" y="38">④</text>
  </g>
  <text x="20" y="140" fill="currentColor" font-size="12">16커밋 → 질의 4번 (⌈log2 15⌉). 범인 c11 = 마지막 good 바로 다음 bad.</text>
  <g stroke="currentColor">
    <rect x="390" y="40" width="160" height="34" rx="6" class="f-acc"/>
    <rect x="390" y="100" width="160" height="34" rx="6" class="f-acc"/>
    <rect x="390" y="160" width="160" height="34" rx="6" class="f-acc"/>
    <rect x="600" y="40" width="130" height="34" rx="6" class="f-mut"/>
    <rect x="600" y="100" width="130" height="34" rx="6" class="f-warn"/>
    <rect x="600" y="160" width="130" height="34" rx="6" class="f-ok"/>
    <rect x="600" y="220" width="130" height="34" rx="6" class="f-warn"/>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#sivm11-a)">
    <path d="M550 57 L598 57"/><path d="M470 74 L470 98"/><path d="M550 117 L598 117"/>
    <path d="M470 134 L470 158"/><path d="M550 177 L598 177"/><path d="M500 194 L598 237"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="400" y="62">fw.py 빌드(컴파일)</text><text x="400" y="122">보드/장비 응답?</text>
    <text x="400" y="182">n회 실행 → 판정</text>
    <text x="556" y="52">실패</text><text x="556" y="112">아니오</text><text x="556" y="172">good</text><text x="520" y="222">bad</text>
    <text x="610" y="62">exit 125 skip</text><text x="610" y="122">exit 255 abort</text>
    <text x="610" y="182">exit 0 good</text><text x="610" y="242">exit 1 bad</text>
  </g>
  <text x="20" y="280" fill="currentColor" font-size="12">러너는 저장소 밖(이 레슨 파일 --runner)에 둔다: bisect가 옛 커밋을 체크아웃해도 러너는 바뀌지 않는다.</text>
</svg>
```

### Part A — 진짜 `git bisect run` + 다수결 러너

- `majority_vote(run_once, n)` — `run_once(attempt) -> True(PASS)` 를 최대 n번(홀수) 부르고 과반이 정해지면 **조기 종료**. 반환 `(is_good, passes, fails)`.
- `exit_code(status, is_good)` — `ok` → 0/1, `build_fail` → 125, `infra` → 255.
- 주어진 코드가 일회용 저장소(16커밋, c11에 회귀: `split_transfer` 가 4096의 배수에서 길이 0 조각을 만든다)를 만들고, 이 파일 자신을 `--runner` 로 `git bisect run` 에 넘긴다. 확인할 것: 잡음 없을 때 c11 · flake 10%에서 1회 판정은 **틀린 커밋** · 3회 다수결은 c11 · 인프라 오류는 bisect를 **중단** (아무도 지목 안 함) · 저장소가 Don의 `~/.gitconfig` 를 읽지 않음.

### Part B — 파라미터 bisect와 flaky 모델 측정

- `find_boundary(lo, hi, is_fail, step=1, check_ends=True)` → `(last_pass, first_fail, evals)`. `lo > hi` 도 된다 (Vmin: 전압을 내리면 실패). 격자에 안 맞는 범위 · lo가 이미 FAIL → `ValueError`, hi가 PASS → `NoBoundary`.
- `sprt_vote(run_once, p0, p1, alpha, beta, max_n)` — Wald SPRT로 한 지점을 good/bad 판정.
- 주어진 `simulate()` 로 64칸 · 2000회 시행에서 정책별 **오답률과 평균 실행 횟수** 를 측정한다. 시나리오 두 개: one-sided flaky(좋은 커밋 10% 실패, 나쁜 커밋 100% 실패) / intermittent(5% / 65%).

### Part C — 야간 회귀: 간헐 실패 + 빌드 깨짐 + 확정 + 경계 리포트

- 40커밋(`v1.4.0`=c00 good, `v1.5.0`=c39 bad). c23 `dma: raise MAX_SEG to 8192 for throughput` 가 회귀 (A0 디스크립터 한계 4096 B 초과, 랜덤 워크로드라 **간헐적**), c19는 빌드 실패, flake 5%.
- SPRT 러너로 bisect → `binom_sf` / `confirm_culprit` 로 부모(c22)·범인(c23)을 20회씩 재실행해 확정 → 범인 커밋에서 전송 크기 경계 bisect(64..65536 B) → 리포트.

## 2. Problem (English)

> **Automated bisection under flaky tests.**
>
> 1. Write a `git bisect run` runner. Exit 0 if the checked-out firmware is good, 1 if bad, 125 if it does not build (skip),
>    and 255 on test-infrastructure failure (board not responding) so bisect aborts instead of blaming a commit.
>    `majority_vote(run_once, n)` runs a test up to `n` (odd) times with early stop and returns `(is_good, passes, fails)`.
>    Demonstrate on a throwaway repository (created in a temporary directory, isolated from the user's git config)
>    that a single-run bisect blames the wrong commit under a 10% flaky test while majority-of-3 finds the real one.
> 2. `find_boundary(lo, hi, is_fail, step=1, check_ends=True) -> (last_pass, first_fail, evals)`: binary search on a grid for a
>    monotonic pass→fail predicate, in either direction (`lo > hi` allowed), validating the endpoints.
>    Example: Vmin sweep 1000→600 mV step 5 with failures below 713 mV → `(715, 710, ...)`.
> 3. `sprt_vote(run_once, p0, p1, alpha, beta, max_n)`: sequential probability ratio test between "fail rate p0 (flaky only)"
>    and "fail rate p1 (regressed)". Measure the wrong-answer rate of naive / majority / retry / SPRT bisection on a 64-point model.
> 4. On a 40-commit history with an intermittent regression, one unbuildable commit and 5% flakiness: bisect with SPRT,
>    confirm the culprit with a one-sided binomial test against the flake rate, find the transfer-size boundary on the culprit,
>    and print a report.

## 3. 시작 코드

<!-- code: starter.py -->

```bash
cd lessons/SIV/SIV-M11
python3 -I starter.py; echo "exit=$?"        # TODO 상태: 10/51 passed, exit=1
```

실제 출력 (일부):

```text
-- Part A: real git bisect run (throwaway repo) --
PASS  isolated git config (no ~/.gitconfig)
PASS  16 commits, deterministic hashes
FAIL  no flake, naive -> c11 (true culprit)
PASS  flake 10%, naive, seed 2 -> WRONG commit (reproduced)
FAIL  flake 10%, majority-of-3, seed 2 -> c11
FAIL  majority runner: >=2 runs per step, <=3
FAIL  infra error at first probe -> bisect aborted, nobody blamed
   naive  -> c15 | majority3 -> c15 | clean -> c15
...
PASS  one-sided: naive wrong >= 20%
PASS  intermittent: naive wrong >= 50%
FAIL  one-sided: retry3 wrong <= 1%
...
first bad : c39 35bde4f733 "pcie: ASPM L1 default off"
...
10/51 passed
```

> 눈여겨볼 것: stub `majority_vote` 가 늘 good을 돌려주면 git은 모든 커밋을 good으로 표시하고 **bad 끝점 c15/c39를 범인** 으로 낸다. 그래서 "naive가 틀린 커밋을 지목한다" 같은 *오답 재현* 체크와 "정상 조건에서 맞춘다" 체크를 반드시 짝으로 둔다 — 음성 체크 하나만으로는 stub도 통과한다.

## 4. 힌트

<details>
<summary>힌트 1 — 다수결의 조기 종료</summary>

`need = n // 2 + 1`. pass나 fail 중 하나가 `need` 에 닿는 순간 나머지 실행은 결과를 못 바꾼다. 3회 다수결에서 첫 두 번이 같으면 2회로 끝나므로 평균 비용은 3보다 작다 (Part A 로그에서 실제로 단계당 2–3회).

</details>

<details>
<summary>힌트 2 — 125와 255는 "판정 불가"의 두 종류</summary>

빌드가 안 되는 커밋은 *이 커밋은 모르겠다* (125 → git이 근처 다른 커밋을 고른다). 보드가 JTAG에 응답하지 않는 건 *지금은 아무것도 모르겠다* — 그 상태로 계속 가면 이후 모든 커밋이 bad가 되므로 bisect 자체를 멈춰야 한다 (128 이상 → abort). 둘 다 절대 1(bad)이 아니다.

</details>

<details>
<summary>힌트 3 — 방향이 있는 격자 bisect</summary>

값 대신 **격자 인덱스** 로 탐색한다: `d = ±1`, `n = |hi-lo| // step`, `x(i) = lo + d*i*step`. 불변식 "인덱스 g는 PASS, b는 FAIL"을 `g=0, b=n` 으로 시작해 `b-g > 1` 동안 가운데를 본다. 끝점을 검사하지 않으면, 범위 전체가 PASS여도 "hi가 첫 실패"라는 거짓 답이 나온다.

</details>

<details>
<summary>힌트 4 — SPRT는 로그 우도비의 랜덤 워크</summary>

FAIL 한 번 = `ln(p1/p0)` 만큼 위로, PASS 한 번 = `ln((1-p1)/(1-p0))` 만큼 아래로 (음수). 위 문턱 `ln((1-β)/α)` 를 넘으면 bad, 아래 문턱 `ln(β/(1-α))` 아래로 가면 good. `p0=0.1, p1=0.5, α=β=0.01` 이면 FAIL 3번이면 bad, 실패 없이 PASS 8번이면 good이다.

</details>

<details>
<summary>힌트 5 — 확정은 "부모는 flake로 설명되고, 범인은 설명 안 된다"</summary>

`P(X ≥ k)`, `X ~ Binomial(n, p0)`. 범인의 실패 수가 flake만으로 나올 확률이 α 미만이고, 부모의 실패 수는 flake로 충분히 나올 수 있어야(≥ α) 확정이다. `math.comb` 로 꼬리 합을 직접 더하면 된다 (n=20이면 충분히 빠르다).

</details>

## 5. 풀이

### Part A — 러너는 "판정 함수"이고, 종료 코드가 인터페이스다

`git bisect run <cmd>` 는 커밋을 체크아웃하고 `<cmd>` 를 실행해 종료 코드만 본다 (`git help bisect`, "Bisect run" 절을 이 Mac의 git 2.54.0에서 확인).

| 종료 코드 | git의 해석 | 이 러너에서 쓰는 경우 |
|---|---|---|
| 0 | good (old) | 다수결/SPRT가 good |
| 1–127 (125 제외) | bad (new) | 다수결/SPRT가 bad → 1 |
| 125 | skip (테스트 불가) | `fw.py` 컴파일 실패 (`SyntaxError`) |
| 그 밖 (<0, ≥128) | bisect 중단 | 보드/장비 무응답 → 255 |

126/127은 셸이 "실행 권한 없음 / 명령 없음"에 쓰는 값이라 man page는 125를 skip으로 고른 이유로 이것을 든다. 규약상 126/127도 1–127 범위(bad)지만, 이 Mac의 git 2.54.0은 126/127을 받으면 **알려진 good 커밋에서 러너를 한 번 더 돌려 보고**, 거기서도 같으면 멈춘다. 없는 러너 경로로 직접 해 본 결과:

```text
running '<tmp>/no_such_runner.sh'
'<tmp>/no_such_runner.sh': <tmp>/no_such_runner.sh: No such file or directory
[f7cd53f78db4d128fee949eebb06d094df3f6410] c00: init: DMA split + descriptor ring
running '<tmp>/no_such_runner.sh'
'<tmp>/no_such_runner.sh': <tmp>/no_such_runner.sh: No such file or directory
error: bogus exit code 127 for good revision
```

이 안전장치가 없는 옛 git이나 다른 bisect 도구에서는 러너 경로 오류가 "모든 커밋 bad"로 둔갑한다. 그래서 러너는 저장소 밖 절대 경로로 넘긴다 (이 레슨은 `sys.executable -I <이 파일> --runner`). 저장소 안에 두면 옛 커밋을 체크아웃할 때 러너가 *옛 버전* 으로 바뀌어, 오류 없이 다른 테스트를 돌리는 더 나쁜 상황도 생긴다.

실험실 격리 — Don의 설정과 프로젝트 저장소를 절대 건드리지 않게:

| 설정 | 이유 |
|---|---|
| `tempfile.TemporaryDirectory()` 안에 `orca-fw/` | 끝나면 사라진다 (`temp repo removed` 체크) |
| 환경에서 `GIT_*` 전부 제거 후 다시 설정 | 상속된 `GIT_DIR`/`GIT_WORK_TREE` 가 있으면 엉뚱한 저장소를 건드린다 |
| `GIT_CONFIG_GLOBAL=/dev/null`, `GIT_CONFIG_NOSYSTEM=1` | `~/.gitconfig` 의 hooks·서명·alias 무시 (`config --show-origin` 이 `file:.git/config` 만 보임) |
| `GIT_AUTHOR_*`/`GIT_COMMITTER_*` + 고정 날짜 | 사용자 정보 불필요 + **커밋 해시까지 결정적** (c11은 매번 `7606f353e8`) |
| `-c init.defaultBranch=main -c commit.gpgsign=false` | 기본 브랜치 경고·서명 프롬프트 방지 |

회귀 커밋 c11의 실제 diff (`git diff HEAD~5 HEAD~4`, 일부):

```text
 def split_transfer(nbytes, max_seg=MAX_SEG):
-    segs = []
-    while nbytes > 0:
-        n = min(nbytes, max_seg)
-        segs.append(n)
-        nbytes -= n
+    count = nbytes // max_seg + 1
+    segs = [max_seg] * (count - 1)
+    segs.append(nbytes - max_seg * (count - 1))
     return segs
```

4096이면 `count = 2` → `[4096, 0]`. 길이 0 디스크립터는 많은 DMA 엔진에서 불법이거나 "최대 길이"로 해석되는 위험한 값이다. 고정 워크로드 `(1, 512, 4095, 4096, 4097, 8192, 10000)` 에 4096·8192가 있어 이 버그는 **항상** 잡힌다 — Part A의 어려움은 버그가 아니라 잡음이다.

flake 10%, seed 2, 1회 판정의 실제 `git bisect log`:

```text
git bisect start 'bad15' 'good0'
# good: [34c6967694c9c7055fc8026538e7895c87d8128b] c07: docs: descriptor format
# bad: [7606f353e8a9ca12d112cba6b5f0cac150d34d1b] c11: dma: compute segment count up front (faster)
# bad: [8a685931d72cf3c276db6c54cb7097d6755a25f2] c09: boot: print ROM version
# good: [956966c4ef44022092bd90319a75315975753c64] c08: irq: coalesce TX done
# first bad commit: [8a685931d72cf3c276db6c54cb7097d6755a25f2] c09: boot: print ROM version
```

c09(ROM 버전 출력 — DMA와 무관)가 링크 글리치 한 번 때문에 bad로 찍혔고, 이후 탐색은 c08–c09 사이만 보니 c11은 다시 볼 기회가 없다. **bisect는 한 번의 오판을 되돌리지 않는다** — 이것이 핵심이다. 같은 seed의 3회 다수결은 c09에서 `passes=2 fails=1` → good, c11을 정확히 찾는다.

seed 1–8로 직접 돌려 본 결과 (16커밋, flake 10%, 진짜 git):

| seed | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| naive | c11 | **c09** | c11 | c11 | c11 | c11 | **c09** | **c06** |
| majority3 | c11 | c11 | c11 | c11 | c11 | c11 | **c09** | c11 |

seed 2는 "naive가 처음으로 틀린 seed"라서 재현 케이스로 골랐다. 다수결도 seed 7에서 틀렸다 (c09가 3번 중 2번 실패) — 3회 다수결은 오답을 *줄일* 뿐 없애지 않는다. 숫자는 Part B에서 정확히 잰다.

인프라 오류(`--infra-at 7`, 첫 질의 c07에서 보드 무응답)의 실제 git 출력:

```text
error: bisect run failed: exit code 255 from '/usr/local/bin/python3' '-I' '.../solution.py' '--runner' '--policy' 'naive' '--infra-at' '7' '--log' '<tmp>/runner_infra.jsonl' is < 0 or >= 128
```

`git bisect log` 에는 start 줄만 남고 good/bad 표시는 하나도 없다. 이 상태에서 보드를 복구하고 `git bisect run` 을 다시 부르면 이어서 진행된다. 만약 255 대신 1을 냈다면 c07이 bad가 되고 범인은 c01–c07 어딘가로 나왔을 것이다.

### Part B — 파라미터 bisect, 그리고 flaky가 bisect를 망치는 확률

같은 이진 탐색이 실리콘에서는 **파라미터** 에 쓰인다. 술어가 단조(monotonic)이기만 하면 된다.

| 대상 | lo (PASS) → hi (FAIL) | step | 이 레슨의 결과 |
|---|---|---|---|
| Vmin (713 mV 미만 실패) | 1000 → 600 mV | 5 mV | `(715, 710)` |
| Fmax (1612 MHz 초과 실패) | 800 → 2000 MHz | 25 MHz | `(1600, 1625)` |
| 버퍼 크기 (4096 B 초과 실패) | 64 → 65536 B | 1 B | `(4096, 4097)`, 평가 ≤ 2+17 |

평가 횟수는 끝점 2번 + `⌈log2 n⌉` (n = 격자 칸 수). 65472칸이면 16번, 1024칸이면 정확히 10번 (`1024 points -> exactly 10 probes` 체크). 끝점 검사를 빼면 안 되는 이유: 범위 전체가 PASS인 부모 커밋에서 "65536이 첫 실패"라는 **거짓 경계** 가 나온다. Part C는 이것을 `NoBoundary` 로 받아 "부모는 전 범위 통과"라고 보고한다. 단조성이 깨지는 경우(통과 구간이 창(window)인 PLL lock 범위, 아이 다이어그램)는 연습 2처럼 bisect를 양쪽으로 두 번 한다. 2D shmoo와 guard band를 둔 정식 Vmin 탐색은 SIV-M07이 다룬다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="sivm11-b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 번의 잡음이 탐색 구간을 영구히 잘못 자른다 (seed 2, 실제 로그)</text>
  <rect x="40" y="60" width="440" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="480" y="60" width="220" height="24" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="40" y="104">c00</text><text x="320" y="104">c07</text><text x="400" y="104">c09</text>
    <text x="480" y="104">c11</text><text x="700" y="104">c15</text>
    <text x="260" y="77">진짜 good</text><text x="590" y="77">진짜 bad (회귀)</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#sivm11-b)">
    <path d="M320 140 L320 88"/><path d="M480 140 L480 88"/><path d="M400 140 L400 88"/><path d="M360 140 L360 88"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="320" y="156">① PASS</text><text x="480" y="156">② FAIL</text>
    <text x="400" y="172">③ FAIL (글리치!)</text><text x="352" y="188">④ c08 PASS</text>
  </g>
  <text x="10" y="214" fill="currentColor" font-size="12">③ 이후 구간은 (c08, c09]만 남는다 → "c09: boot: print ROM version" 지목. 진짜 범인 c11은 다시 보지 않는다.</text>
</svg>
```

**왜 이렇게 자주 틀리나.** 질의 중 good 쪽에 떨어지는 것이 k개, 좋은 커밋의 실패 확률이 p면 naive가 맞을 확률은 대략 `(1-p)^k`. 64칸이면 질의 6번 중 평균 3번 정도가 good 쪽이라 p=0.1에서 `1 - 0.9^3 ≈ 27%` 쯤 틀린다 (측정 24.9%). 간헐 회귀(나쁜 커밋이 65%만 실패)면 bad 쪽 질의도 35%씩 틀려 훨씬 나쁘다.

한 지점의 판정 오류 확률 (p = 좋은 커밋 실패율, q = 나쁜 커밋 실패율):

| 정책 | 좋은 커밋을 bad로 | 나쁜 커밋을 good으로 |
|---|---|---|
| naive (1회) | p | 1 − q |
| 3회 다수결 | 3p²(1−p) + p³ (p=0.1 → 0.028) | 3q(1−q)² + (1−q)³ (q=0.65 → 0.282) |
| retry-until-pass (3회 중 1번만 PASS하면 good) | p³ (→ 0.001) | 1 − q³ (q=0.65 → 0.725) |
| SPRT (α=β=0.01) | ≈ α (Wald 근사) | ≈ β |

retry-until-pass는 CI에서 흔한 "실패하면 다시 돌려 봐"다. 잡음이 한쪽 방향(좋은 커밋이 가끔 실패)일 때는 최고지만, 회귀가 간헐적이면 **버그를 숨긴다**.

**SPRT 유도.** 가설 H0 "실패율 p0 (flake만)" vs H1 "실패율 p1 (회귀)". 관측마다 로그 우도비를 더한다.

- FAIL: `ln(p1/p0) = ln 5 = +1.609`, PASS: `ln((1-p1)/(1-p0)) = ln(0.5/0.9) = −0.588`
- 문턱: `A = ln((1−β)/α) = ln 99 = 4.595`, `B = ln(β/(1−α)) = −4.595`
- 따라서 FAIL 3번(4.83 ≥ 4.595)이면 bad, PASS만 8번(−4.70)이면 good. 중간에 FAIL 하나가 끼면 PASS가 10번 더 필요하다 (체크: `PFPPPPPPPPPP → (True, 11, 1)`).

p0는 "좋은 커밋이 보여도 되는 flake 상한"(flaky 대시보드의 이력값), p1은 "이 정도는 실패해야 회귀라 부른다"는 하한이다. 실제 실패율이 p0 이하·p1 이상이면 오류 확률이 대략 α·β 이하로 묶인다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">SPRT: 로그 우도비(LLR)의 랜덤 워크 (p0=0.1, p1=0.5, α=β=0.01)</text>
  <g stroke="currentColor">
    <line x1="60" y1="150" x2="720" y2="150" opacity=".5"/>
    <line x1="60" y1="58" x2="720" y2="58" stroke-dasharray="5 4"/>
    <line x1="60" y1="242" x2="720" y2="242" stroke-dasharray="5 4"/>
    <line x1="60" y1="40" x2="60" y2="260"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="62">+4.6</text><text x="22" y="154">0</text><text x="10" y="246">−4.6</text>
    <text x="560" y="52">위 문턱 → bad (exit 1)</text><text x="560" y="236">아래 문턱 → good (exit 0)</text>
    <text x="600" y="272">실행 횟수 →</text>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="2.5" points="60,150 90,117.8 120,85.6 150,53.4"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="2 3" points="60,150 90,161.8 120,173.5 150,185.3 180,197 210,208.8 240,220.6 270,232.3 300,244.1"/>
  <polyline fill="none" stroke="currentColor" stroke-width="1.5" points="60,150 90,161.8 120,129.6 150,141.4 180,153.1 210,164.9 240,176.6 270,188.4 300,200.2 330,211.9 360,223.7 390,235.4 420,247.2"/>
  <circle cx="150" cy="53.4" r="5" class="f-warn" stroke="currentColor"/>
  <circle cx="300" cy="244.1" r="5" class="f-ok" stroke="currentColor"/>
  <circle cx="420" cy="247.2" r="5" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="160" y="82">FFF → 3회 만에 bad</text>
    <text x="240" y="268">PPPPPPPP → 8회 good</text>
    <text x="300" y="140">P F P×10 → 12회 good</text>
  </g>
</svg>
```

2000회 시행 × 64칸, seed 11의 실제 측정 (`python3 -I solution.py` 출력):

| 시나리오 (좋은/나쁜 커밋 실패율) | naive | majority3 | majority5 | retry3 | SPRT |
|---|---|---|---|---|---|
| one-sided (10% / 100%) 오답률 | 24.9% | 7.9% | 1.6% | **0.2%** | 1.4% |
| 　　평균 실행 횟수 / bisect | 6.0 | 12.6 | 18.9 | 12.3 | 47.4 |
| intermittent (5% / 65%) 오답률 | 73.0% | 59.8% | 54.3% | **93.5%** | **0.1%** |
| 　　평균 실행 횟수 / bisect | 6.0 | 13.9 | 22.1 | 11.0 | 48.4 |

읽는 법: ① 잡음 모양을 *알면* 싸게 이긴다 (one-sided면 retry3이 12회로 0.2%). ② 모르면 retry는 도박이다 (intermittent에서 93.5% 오답). ③ 다수결은 회귀가 간헐적이면 5회로 늘려도 54% 오답 — 각 질의에서 "65% 실패"와 "5% 실패"를 구분하기엔 표본이 모자라다. ④ SPRT는 두 시나리오 모두 1.4% 이하지만 실행이 8배 든다. 보드 한 대에서 테스트 1회 2분이면 bisect 한 번에 약 1.6시간 — 야간 회귀가 아침까지 끝내기에 현실적인 범위이고, 연습 4의 확률적 bisection은 같은 정확도를 더 적은 실행으로 낸다.

### Part C — 야간 회귀 시나리오와 확정

c23은 `MAX_SEG` 를 8192로 올렸지만 A0의 디스크립터는 4096 B까지만 허용한다. 테스트는 매 실행 랜덤 크기 3개(64..6000 B)를 보내므로 실패는 **간헐적** 이다: 한 전송이 4096을 넘을 확률 ≈ 1904/5937 = 0.32, 세 번 중 하나라도 넘을 확률 ≈ 1 − 0.68³ ≈ 0.69. 여기에 flake 5%. c19는 WIP 커밋이라 컴파일이 안 된다.

실제 `git bisect log` (SPRT 러너):

```text
git bisect start 'v1.5.0' 'v1.4.0'
# skip: [92b5231965e95260ad8a627614baf8b536f530bd] c19: debug: WIP dump helper
# bad: [dadb2fc9d0f6f882d6e5bf4a9b8a1e712f954439] c27: ci: lint fixes
# good: [156a32ce3804ce9b640f677956ac22e9e1b7572d] c13: log: rate-limit warnings
# good: [a9d816fd507b50f548df5c11dab4c1057c613935] c20: debug: fix WIP build
# bad: [39c0bc7632cd67c636c33e5f6293e208c95fbe01] c24: ring: bump TX ring to 128
# good: [6ebdde8fc79e5ee745e931e27703e5b45d1aabee] c22: clk: PLL lock timeout 2ms
# bad: [6fa6657efd2c278f85a6073f444929e1c9557dbf] c23: dma: raise MAX_SEG to 8192 for throughput
# first bad commit: [6fa6657efd2c278f85a6073f444929e1c9557dbf] c23: dma: raise MAX_SEG to 8192 for throughput
```

러너 로그(JSONL)를 보면 SPRT가 지점마다 다른 비용을 쓴 것이 보인다: c13은 flake 때문에 `passes=17 fails=3` 으로 20회 만에 good, c23은 `passes=1 fails=4` 로 5회 만에 bad, c19는 빌드 실패로 125 (git은 c19 대신 c27로 옮겨 갔다). 첫 질의가 skip이어도 결과에는 영향이 없다 — skip이 **경계에 붙을 때만** 후보가 여러 개가 된다 (연습 3에서 진짜 git으로 확인).

**확정(confirm).** bisect가 끝나도 끝이 아니다. 범인과 부모를 *새 seed* 로 20번씩 다시 돌린다.

- 부모 c22: 0/20 실패 → `P(X ≥ 0) = 1` ≥ α. flake로 충분히 설명된다.
- 범인 c23: 16/20 실패 → `P(X ≥ 16 | n=20, p0=0.1) = 3.3e-13` < α=0.01. flake로는 불가능하다.

이 검정은 "범인 vs 부모 차이"를 직접 보는 2표본 검정(Fisher exact 등)의 단순판이다. p0(flake 상한)를 이력에서 알고 있다는 가정이 있어 계산이 한 줄로 끝난다.

**경계로 원인 좁히기.** 범인 커밋에서 전송 크기를 64..65536 B로 bisect하면(지점마다 SPRT) 4096 B까지 통과, 4097 B부터 실패. 부모는 전 범위 통과(`NoBoundary`). "실패가 정확히 4096+1에서 시작" — 디스크립터 길이 한계와 같은 숫자이므로 디버그 엔지니어는 DMA 디스크립터 코드부터 본다. 실제 리포트:

```text
== BISECT REPORT: dma_loopback_random on EVB-07 (ORCA-A0) ==
range     : v1.4.0 (c00, good) .. v1.5.0 (c39, bad)
policy    : SPRT p0=0.10 p1=0.50 alpha=beta=0.01, max 30 runs/step
first bad : c23 6fa6657efd "dma: raise MAX_SEG to 8192 for throughput"
skipped   : c19 (build failed -> exit 125)
cost      : 7 runner calls (git bisect steps), 62 test runs
confirm   : parent c22 0/20 fail (p=1) | culprit c23 16/20 fail (p=3.3e-13) -> CONFIRMED
boundary  : culprit c23 size 64..65536 B: last pass 4096 B, first fail 4097 B (18 probes, 110 runs)
            parent  c22 size 64..65536 B: no fail up to 65536 B
next      : DMA segments > 4096 B fail on A0 since c23; check MAX_SEG vs A0 descriptor limit
```

리포트에 꼭 들어가야 하는 것: 탐색 범위와 끝점이 정말 good/bad였는지, 판정 정책과 파라미터(재현 가능하게), skip된 커밋, 비용, **확정 통계**, 경계 같은 물리적 단서, 다음 행동. 이 형식은 SIV-B14의 리포트·SIV-M10의 triage(어떤 실패를 bisect할지 고르는 단계)와 이어지고, 장비 제어·재시도·DB 저장은 SIV-M12의 시퀀서가 맡는다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py; echo "exit=$?"            # 51/51 passed, exit=0 (이 Mac에서 약 4초)
python3 -I -W error -X dev solution.py            # 경고 없음, 51/51 passed
```

두 번 실행한 출력이 바이트 단위로 같다 (커밋 해시 포함) — 날짜·작성자·seed를 모두 고정했기 때문이다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 러너가 보드 무응답·타임아웃을 `exit 1` | 그 커밋이 bad가 되고 이후 탐색이 전부 오염 | 인프라 오류는 `exit 255` (abort), 빌드 실패는 125 |
| 러너 스크립트를 저장소 안에 둠 | 옛 커밋에서 러너가 없거나(127) 옛 버전이 조용히 다른 테스트를 돌림 | 저장소 밖 절대 경로 (`git bisect run /abs/runner ...`) |
| 실험용 저장소에서 사용자 git 설정을 그대로 씀 | hooks·서명·alias가 끼어들고, 상속된 `GIT_DIR` 이면 진짜 저장소를 건드림 | `GIT_*` 제거, `GIT_CONFIG_GLOBAL=/dev/null`, temp dir |
| flaky 테스트를 1회 판정으로 bisect | 한 번의 오판은 되돌릴 수 없다 (seed 2: c09 지목) | 다수결/SPRT + 마지막에 확정 |
| 간헐 회귀에 retry-until-pass | 버그가 한 번만 PASS해도 good → 93.5% 오답 | 잡음 모양 모르면 SPRT |
| 파라미터 bisect에서 끝점 검사 생략 | 전 범위 PASS여도 hi를 "첫 실패"로 보고 | `check_ends=True`, `NoBoundary` |
| `(lo + hi) // 2` 를 값으로 계산 | step 격자에서 벗어난 전압을 설정하거나 방향(lo>hi)을 못 다룸 | 격자 인덱스로 탐색, `x(i) = lo + d*i*step` |
| 비단조 술어(창)에 bisect 한 번 | 창의 한쪽 가장자리만 나옴 | 알려진 PASS 점에서 양쪽으로 두 번 (연습 2) |
| bisect가 낸 커밋을 바로 revert | 확정 없이 revert하면 flaky 커밋 작성자만 억울 | 부모·범인 재실행 + 이항 검정 |
| 잡음 seed를 전역 카운터로 만듦 | 탐색 순서가 바뀌면 같은 커밋의 결과가 달라져 재현 불가 | `(seed, 빌드, 크기, 시도 번호)` 로 seed |

## 8. 면접 Q&A

<details>
<summary>Q1. git bisect run의 종료 코드 규약을 설명하고, 검증 러너에서 각각 언제 쓰나요?</summary>

0 = good, 1–127(125 제외) = bad, 125 = skip, 그 밖(128 이상, 음수) = bisect 중단. 빌드가 안 되거나 그 커밋에서 테스트가 성립하지 않으면 125, 보드·장비·라이선스 서버 문제처럼 *커밋과 무관하게* 판정할 수 없으면 128 이상으로 멈춘다.
꼬리질문: "126/127은?" → 셸의 '실행 권한 없음/명령 없음'. 규약상으로는 bad 범위지만, 최근 git(이 Mac 2.54.0에서 확인)은 good 커밋에서 다시 돌려 같은 코드가 나오면 `bogus exit code 127 for good revision` 으로 멈춘다. 그래도 그 안전장치에 기대지 말고, 러너 첫 부분에서 자기 환경(보드 연결, 툴 경로)을 확인하고 실패하면 스스로 abort하는 게 맞다.

</details>

<details>
<summary>Q2. 테스트가 flaky하면 bisect 결과가 왜 틀리고, 어떻게 고치나요?</summary>

이진 탐색은 각 질의 결과를 믿고 구간의 절반을 영구히 버린다. 좋은 커밋 하나가 우연히 실패하면 진짜 범인이 있는 절반을 버린다. 좋은 쪽 질의가 k번이면 맞을 확률이 대략 `(1-p)^k`. 고치는 법: 지점마다 반복 판정(다수결, 또는 잡음이 한쪽이면 retry), 잡음 모양을 모르면 SPRT 같은 순차 검정, 끝나면 부모·범인을 여러 번 돌려 확정. 측정: 64칸, flake 10%에서 naive 24.9% 오답, 3회 다수결 7.9%, SPRT 1.4%.
꼬리질문: "그럼 무조건 10번씩 돌리면?" → 비용이 10배이고, 간헐 회귀(65%)에는 고정 횟수 다수결이 생각보다 약하다(5회도 54% 오답). 결론이 확실한 지점에서 일찍 멈추는 순차 검정이 같은 비용에 더 정확하다.

</details>

<details>
<summary>Q3. retry-until-pass와 다수결 중 무엇을 쓰나요?</summary>

잡음이 **한쪽 방향** (좋은 커밋이 가끔 실패, 나쁜 커밋은 항상 실패)이면 retry가 싸고 정확하다 (p³). 회귀 자체가 간헐적이면 retry는 버그를 숨긴다 (나쁜 커밋이 한 번만 PASS해도 good → 측정 93.5% 오답). 다수결은 양쪽 오류를 대칭으로 줄이지만 효율이 낮다. 실무에서는 flaky 이력(테스트별 실패율)을 보고 고르고, 모르면 SPRT.
꼬리질문: "CI의 자동 retry가 위험한 이유?" → 간헐 회귀가 '초록'으로 머지된다. retry로 통과한 테스트도 결과 DB에 "1차 실패"를 남겨 flaky 대시보드와 triage(SIV-M10)에 보이게 해야 한다.

</details>

<details>
<summary>Q4. SPRT의 문턱과 p0, p1은 어떻게 정하나요?</summary>

Wald 근사: 위 문턱 `ln((1−β)/α)`, 아래 문턱 `ln(β/(1−α))`. α = 좋은 커밋을 bad로 할 확률, β = 나쁜 커밋을 good으로 할 확률. p0는 그 테스트의 flake 이력 상한, p1은 "회귀라고 부를 최소 실패율". p0=0.1, p1=0.5, α=β=0.01이면 FAIL 3번 → bad, PASS 8번 → good.
꼬리질문: "실제 실패율이 p0와 p1 사이(예: 0.3)면?" → 결정이 늦어진다 (랜덤 워크가 0 근처에서 오래 맴돈다). 그래서 `max_n` 상한이 필요하고, 상한에 걸린 지점은 리포트에 "inconclusive"로 남겨 사람이 보게 한다.

</details>

<details>
<summary>Q5. Vmin(또는 Fmax) 경계를 bisect로 찾을 때 주의할 점은?</summary>

① 술어가 단조인지 — 전압을 내리면 실패가 늘어나는 게 보통이지만, 특정 전압 근처에서만 실패하는 창(window)이나 온도에 따라 바뀌는 경우가 있다. ② 끝점을 실제로 확인 — 높은 전압에서도 실패하면 bisect가 아니라 기능 버그. ③ 경계 근처는 확률적 — 같은 전압에서 통과/실패가 섞이므로 지점마다 반복 판정, 경계는 "실패율 x%가 되는 전압"으로 정의. ④ 격자(전원 장비 해상도, PMIC step) 위에서 탐색. ⑤ 히스테리시스 — 전압을 내리면서 보는지 올리면서 보는지, 매 지점 리셋 여부를 고정. 2D shmoo와 guard band는 SIV-M07.
꼬리질문: "버퍼 크기 경계가 4097이라는 사실이 왜 유용한가?" → 숫자가 하드웨어 한계(디스크립터 길이 4096)와 정확히 일치하면 원인이 사실상 지목된다. 경계값은 리포트의 가장 강한 단서다.

</details>

<details>
<summary>Q6. bisect가 범인을 냈습니다. 바로 revert해도 되나요?</summary>

확정부터 한다. 범인과 부모를 새 seed로 N번씩 돌려, 부모의 실패는 flake로 설명되고(`P(X ≥ k_parent | p0) ≥ α`) 범인의 실패는 설명되지 않음(`< α`)을 보인다. Part C: 부모 0/20, 범인 16/20, p=3.3e-13. 가능하면 최신 브랜치에 범인만 revert한 빌드도 돌려 실패가 사라지는지 본다 (회귀가 여러 커밋에 걸쳐 있거나 다른 커밋과 상호작용하는지 확인).
꼬리질문: "간헐 실패율 30%인 회귀가 고쳐졌다고 말하려면 몇 번 PASS해야 하나?" → `(1−0.3)^N ≤ 0.01` → N ≥ 12.9 → 13번 연속 PASS (연습 1). SIV-M06의 BER 신뢰도 공식 `N ≥ −ln(1−CL)/BER` 와 같은 꼴이다.

</details>

<details>
<summary>Q7. 중간 커밋이 빌드가 안 되면? 머지 커밋이 섞인 이력은?</summary>

빌드 실패는 125로 skip. git이 근처 다른 커밋을 대신 고른다. 단 skip된 커밋들이 경계에 붙으면 "The first bad commit could be any of:" 로 후보 여러 개를 낸다 (연습 3에서 c09·c10·c11을 실제로 확인). 그때는 빌드를 고치는 패치를 러너가 임시로 적용하고 테스트하는 방법이 man page에도 나온다. 머지가 많은 이력에서 브랜치 내부의 깨진 커밋을 피하려면 `--first-parent` 로 머지 커밋 단위로만 탐색한다.
꼬리질문: "고친 커밋(fix)을 찾고 싶다면?" → 의미가 거꾸로이므로 `git bisect start --term-old=broken --term-new=fixed` 처럼 용어를 바꿔 쓴다.

</details>

## 9. 추가 연습

### 연습 1 — 확정에 필요한 실행 횟수

`runs_to_confirm(q, alpha)`: 실패율 q인 커밋이 N번 연속 PASS할 확률 `(1−q)^N ≤ α` 를 만족하는 최소 N. `(0.3, 0.01) → 13`, `(0.65, 0.01) → 5`, `(0.05, 0.01) → 90`, `(0.5, 0.25) → 2`.

<details>
<summary>답 보기</summary>

`N = ⌈ln α / ln(1−q)⌉`. 함정은 부동소수: `ln 0.25 / ln 0.5` 가 `2.0000000000000004` 로 나오면 ceil이 3을 준다. 그래서 `ceil(x − 1e-9)` 로 계산한다 (`(0.5, 0.25) → 2` 체크). q가 작으면 `ln(1−q) ≈ −q` 라 `N ≈ −ln α / q` (q=0.05면 92, 정확한 값 90) — SIV-M06의 "0 에러로 BER 1e-12를 95% 신뢰도로 주장하려면 약 3.0e12 비트" 와 같은 식이다. 실무 의미: 5% 확률로만 터지는 버그를 "고쳤다"고 하려면 90번은 연속으로 돌려야 한다.

</details>

### 연습 2 — 통과 창(window)의 양쪽 가장자리

`find_window(lo, hi, known_pass, is_fail, step)` → `(low_edge, high_edge, evals)`. 수신기 지연 탭 0..127에서 37..81만 통과 → `(37, 81)`. PLL 400..1600 MHz, step 25, 640..1360 MHz에서 lock → `(650, 1350)`. 한쪽이 범위 끝까지 열려 있으면 그 끝값.

<details>
<summary>답 보기</summary>

창은 단조가 아니지만, **알려진 PASS 점에서 바깥쪽으로** 는 각각 단조다. `find_boundary(known_pass, lo, ...)` 와 `find_boundary(known_pass, hi, ...)` 두 번 (`lo < known_pass` 라 첫 번째는 내려가는 방향). 끝까지 통과하면 `NoBoundary` 를 받아 그 끝값을 낸다. 평가 횟수는 `1 + 2·(⌈log2⌉ + 1)` 이하 (체크: 15 이하). DDR 트레이닝의 eye 중심 찾기(SIV-M05)와 shmoo(SIV-M07)가 이 아이디어를 2D로 확장한다.

</details>

### 연습 3 — skip이 경계에 붙으면 범인 후보는 여러 개

`culprit_candidates(status)`: `{커밋: 'good'|'bad'|'skip'}` → 마지막 good 다음부터 첫 bad까지의 목록. bad 뒤에 good이 있으면 `ValueError` (flaky 또는 비단조의 신호). 그리고 c09·c10이 빌드 실패, c11이 회귀인 진짜 git 저장소에서 git의 답과 비교한다.

<details>
<summary>답 보기</summary>

`last_good = max(good)`, `first_bad = min(bad)`, 후보 = `range(last_good+1, first_bad+1)`. 진짜 git 출력 (이 Mac):

```text
There are only 'skip'ped commits left to test.
The first bad commit could be any of:
22d628d0c4fce83c8d354bf018ba5ed07f456c54
c27e0d911eaed18607276f18f6e333fd8f360a9c
480492276cf74736b0cbc1fcfca2121a92012913
We cannot bisect more!
error: bisect run cannot continue any more
```

세 해시는 c09·c10·c11이고 우리 함수도 `[9, 10, 11]`. 이때는 c09·c10의 빌드를 고치는 패치를 러너가 임시로 얹어서 테스트하거나, 변경 내용을 사람이 읽고 판단한다. "good after bad" 검사는 수동 bisect에서 누군가 결과를 잘못 표시했거나 테스트가 flaky하다는 가장 싼 경보다.

</details>

### 연습 4 — 확률적 bisection (사후분포 + 정보량 최대 질의)

경계 b의 사후분포를 들고 다니며, 매번 **기대 정보량(mutual information)이 가장 큰** 지점을 1회씩 질의하고 베이즈 갱신한다. 예산 40회 후 MAP를 답한다. intermittent(5%/65%) 모델 1000회 시행에서 SPRT(지점마다 순차 검정)와 비교.

<details>
<summary>답 보기</summary>

실패 관측 시 우도: `b ≤ x` (x가 bad)면 p1, 아니면 p0. 질의점 x의 정보량 `I(x) = H2(P_fail(x)) − [F(x)·H2(p1) + (1−F(x))·H2(p0)]`, `F(x) = P(b ≤ x)`, `P_fail = p1·F + p0·(1−F)`. 처음에는 단순히 "사후분포 중앙값"을 질의했는데, 이산 분포에서 질량이 한 칸에 몰리면 늘 같은 칸만 묻게 되어 예산을 늘려도 오답률이 12–14%에서 멈췄다. 정보량 기준으로 바꾸자 해결됐다. 실제 측정:

```text
prob-bisect budget=20: wrong=19.0% | budget=40: wrong=0.7% | sprt: wrong=0.2% runs=48.3
```

SPRT는 질의 지점마다 따로 확신을 쌓지만, 확률적 bisection은 모든 관측을 하나의 사후분포에 모아 재활용해서 더 적은 실행으로 비슷한 정확도를 낸다. 대신 p0, p1 모델에 의존하고 구현·설명이 복잡하다 (Horstein 1963의 노이즈 있는 피드백 문제에서 나온 아이디어로, "probabilistic bisection algorithm" 으로 연구돼 있다).

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py; echo "exit=$?"     # 20/20 passed, exit=0 (진짜 git 저장소 1개 포함, 약 4.5초)
```

## 10. 핵심 정리

- `git bisect run` 종료 코드: 0 good · 1–127(125 제외) bad · 125 skip · 128 이상 abort. 인프라 오류는 절대 bad(1)로 내지 않는다.
- 러너는 저장소 밖에. 실험 저장소는 temp dir + `GIT_*` 제거 + `GIT_CONFIG_GLOBAL=/dev/null` + 고정 날짜(해시까지 결정적).
- bisect는 한 번의 오판을 되돌리지 않는다: naive 오답률 ≈ `1 − (1−p)^k`. 64칸·flake 10%에서 24.9%.
- retry-until-pass는 한쪽 잡음에만 맞고, 간헐 회귀는 숨긴다 (93.5% 오답). 다수결은 간헐 회귀에 약하다. 잡음을 모르면 SPRT(FAIL 3번 bad / PASS 8번 good, p0=.1 p1=.5 α=β=.01).
- 파라미터 bisect = 격자 인덱스 + 방향 + 끝점 검사. 평가 = 2 + ⌈log2 n⌉. 창은 양쪽으로 두 번.
- 찾은 범인은 확정한다: 부모는 flake로 설명되고 범인은 안 된다 (이항 꼬리 확률). "고쳤다"는 `(1−q)^N ≤ α` 만큼 연속 PASS로 증명한다.
- 리포트: 범위·정책·skip·비용·확정 통계·경계값(물리적 단서)·다음 행동.

## 참고자료

- [git-bisect 문서 — "Bisect run", 종료 코드 125, `--first-parent`, `--term-old/--term-new`](https://git-scm.com/docs/git-bisect)
- [git-config 문서 — `GIT_CONFIG_GLOBAL`, `GIT_CONFIG_NOSYSTEM`](https://git-scm.com/docs/git-config)
- A. Wald, *Sequential Analysis*, Wiley 1947 — SPRT와 문턱 근사
- M. Horstein, "Sequential transmission using noiseless feedback", IEEE Trans. Information Theory, 1963 — 확률적 bisection의 출발점
- R. Waeber, P. Frazier, S. Henderson, "Bisection search with noisy responses", SIAM J. Control and Optimization, 2013
- [Google Testing Blog — "Flaky Tests at Google and How We Mitigate Them" (2016)](https://testing.googleblog.com/2016/05/flaky-tests-at-google-and-how-we.html)
- [mozregression — Firefox 빌드 회귀 bisect 도구](https://mozilla.github.io/mozregression/)
- [Python `tempfile`](https://docs.python.org/3/library/tempfile.html) · [`subprocess`](https://docs.python.org/3/library/subprocess.html) · [`math.comb`](https://docs.python.org/3/library/math.html#math.comb)
- 관련 레슨: SIV-B14(리포트·종료 코드), SIV-M06(BER 신뢰도 — 같은 꼴의 식), SIV-M07(shmoo·Vmin), SIV-M10(triage), SIV-M12(시퀀서)
