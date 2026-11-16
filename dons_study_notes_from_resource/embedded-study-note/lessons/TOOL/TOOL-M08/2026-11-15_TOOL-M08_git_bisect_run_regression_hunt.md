---
id: TOOL-M08
time: 80분
updated: 2026-11-15
---
# TOOL-M08 git bisect run으로 회귀 커밋 자동 탐색

## 학습 목표

- 100 개 커밋짜리 실습 저장소를 **결정적으로**(같은 해시가 맥·VM 에서 똑같이) 만드는 스크립트를 읽고, 그 안에 심어 둔 기능 회귀·빌드 깨짐·성능 회귀를 `git bisect run` 으로 찾는다.
- `git bisect run` 의 종료 코드 규약 **0 / 1–124·126·127 / 125 / 128–255** 를 실측으로 알고, 빌드 실패 → 125, 크래시(rc 134) → 1, 장비 고장 → 255 로 **의도적으로** 매핑한다.
- 테스트 스크립트가 틀리면 bisect 가 **조용히 엉뚱한 커밋**을 지목한다는 것을 starter 로 직접 본다 (실측: 범인 c063 대신 BAD 끝점 c099 를 지목).
- 단계 수 ≈ ⌈log₂ N⌉ + skip 수를 실제 `git bisect log` 로 확인하고, `skip` 범위, `--first-parent`, `terms`, pathspec 제한, `replay` 를 쓴다.
- 성능 회귀처럼 **잡음이 있는** 판정을 median + 교차 측정(interleaving) + 회색 지대 skip 으로 안정화하고, 펌웨어 HIL(Hardware-in-the-loop) 테스트를 bisect 스크립트로 쓰는 법을 설명한다.

## 1. 문제

"지난 릴리스에서는 됐는데 지금은 안 돼요. 그 사이 커밋이 300 개예요." — 펌웨어 팀에서 매주 듣는 말이고, 면접에서도 "회귀 커밋 어떻게 찾아요?" 로 나온다. 답은 `git bisect` 지만, 실무에서 bisect 가 실패하는 이유는 거의 항상 **git 이 아니라 테스트 스크립트**다: 빌드가 안 되는 커밋을 "bad" 라고 하거나, 크래시 종료 코드(134)를 그대로 넘겨 bisect 를 멈추거나, 이전 단계의 빌드 산출물을 재사용하거나, 체크아웃마다 바뀌는 저장소 안의 테스트로 판정한다.

이분 탐색(binary search)의 조건은 하나다: 커밋을 순서대로 놓았을 때 판정이 **good…good bad…bad** 로 한 번만 바뀌어야(단조, monotonic) 한다. 그러면 N 개 후보는 ⌈log₂ N⌉ 번이면 끝난다 — 100 개면 7 번. 아래 그림은 이 레슨의 저장소에서 solution 스크립트로 돌린 **실제** 순서다.

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">git bisect run main v1.0 — 실제 순서 (solution_test.sh, 맥 git 2.54)</text>
  <text x="230" y="44" fill="currentColor" font-size="12" font-family="monospace">c000</text>
  <text x="680" y="44" fill="currentColor" font-size="12" font-family="monospace">c099</text>
  <text x="455" y="44" fill="currentColor" font-size="12" font-family="monospace">c049</text>
  <g font-size="12" fill="currentColor">
    <text x="10" y="66">시작: 99 개 의심</text>
    <text x="10" y="90">① c049 → good</text>
    <text x="10" y="114">② c074 → bad (rc 134)</text>
    <text x="10" y="138">③ c061 → skip (빌드 X)</text>
    <text x="10" y="162">④ c064 → bad</text>
    <text x="10" y="186">⑤ c057 → good</text>
    <text x="10" y="210">⑥ c060 → skip (빌드 X)</text>
    <text x="10" y="234">⑦ c059 → good</text>
    <text x="10" y="258">⑧ c062 → good</text>
    <text x="10" y="282">⑨ c063 → bad = 첫 bad</text>
  </g>
  <rect x="230" y="54" width="485" height="14" class="f-mut" stroke="currentColor"/>
  <rect x="470" y="78" width="245" height="14" class="f-mut" stroke="currentColor"/>
  <rect x="470" y="102" width="123" height="14" class="f-mut" stroke="currentColor"/>
  <rect x="470" y="126" width="123" height="14" class="f-mut" stroke="currentColor"/>
  <rect x="470" y="150" width="74" height="14" class="f-mut" stroke="currentColor"/>
  <rect x="509" y="174" width="35" height="14" class="f-mut" stroke="currentColor"/>
  <rect x="509" y="198" width="35" height="14" class="f-mut" stroke="currentColor"/>
  <rect x="519" y="222" width="25" height="14" class="f-mut" stroke="currentColor"/>
  <rect x="534" y="246" width="10" height="14" class="f-mut" stroke="currentColor"/>
  <rect x="534" y="270" width="5" height="14" class="f-acc" stroke="currentColor"/>
  <g stroke="currentColor" stroke-width="3">
    <path d="M470 76 L470 94" class="f-ok"/>
    <path d="M593 100 L593 118"/>
    <path d="M529 124 L529 142" stroke-dasharray="3 2"/>
    <path d="M544 148 L544 166"/>
    <path d="M509 172 L509 190"/>
    <path d="M524 196 L524 214" stroke-dasharray="3 2"/>
    <path d="M519 220 L519 238"/>
    <path d="M534 244 L534 262"/>
    <path d="M539 268 L539 286"/>
  </g>
  <text x="550" y="283" fill="currentColor" font-size="12" font-family="monospace">c063</text>
  <text x="10" y="310" fill="currentColor" font-size="12">회색 막대 = 남은 의심 구간(c000 → c099 를 485 px 로). 굵은 선 = 이번에 테스트한 커밋, 점선 = 125 skip.</text>
  <text x="10" y="326" fill="currentColor" font-size="12">good/bad 판정 7 번 = ⌈log₂ 99⌉ = 7, 그리고 skip 2 번. 직선 탐색이었다면 최대 99 번 빌드.</text>
</svg>
```

저장소 (`gen_repo.sh` 가 /tmp 에 만든다, 호스트에서 도는 작은 펌웨어 코어: crc16 · ringbuf · uart shim · app):

| 커밋 | 내용 | 이 레슨에서의 역할 |
|---|---|---|
| c000 (`v1.0`) | 최초 import | 알려진 good |
| c020 | `tests/check.sh` + `tests/expected.txt` (저장소 안의 smoke test) | starter 가 이걸 오라클로 쓴다 |
| c060 / c061 | HAL 함수 이름 변경, main.c 는 그대로 → **컴파일 에러** 2 커밋 | 125 skip 대상 |
| c062 | 빌드 수정 | |
| **c063** | `include/ringbuf.h` 의 `rb_next()` 를 `>=` → `>` (**헤더만** 바뀜) → `assert` → SIGABRT | **기능 회귀 (정답)** |
| c085 | "flaky" 라며 저장소 테스트에서 ringbuf 케이스를 뺌 | 오라클이 체크아웃마다 바뀌는 함정 |
| **c088** | 테이블 CRC → 비트 단위 CRC ("512 B 절약") | **성능 회귀 (추가 연습)** |
| c099 (`main`) | HEAD | 알려진 bad |

### Part A — 종료 코드 규약을 지키는 bisect run 스크립트

- `solution_test.sh` 를 완성한다: git 이 각 단계마다 **저장소 최상위를 cwd 로** 스크립트를 부른다. 깨끗한 트리에서 빌드하고, 앱을 돌려 **고정된(frozen) 오라클**과 비교해 0/1 을 돌려준다.
- 빌드가 안 되면 125(skip), 앱이 크래시하면(rc ≥ 128) **1(bad)** 로 바꾼다. 장비(rig) 자체가 고장이면 255 로 bisect 를 멈춘다.
- `git bisect start main v1.0` + `git bisect run` 이 **c063** 을 찾고, 판정 ≤ 8 번 + skip 은 c060/c061 에만 있어야 한다.

### Part B — 함정 4 개 (starter 에 심어 둔 버그, 체커가 하나씩 FAIL 로 잡는다)

1. 빌드 실패 → `exit 1` (빌드 깨짐 구간을 회귀로 착각)
2. 저장소 테스트의 종료 코드를 그대로 전달 → 크래시 rc **134** → `git bisect run` 중단
3. `git clean` 없이 증분 빌드 → 이전 체크아웃의 `build/` 를 테스트 (헤더만 바뀐 c063 은 다시 컴파일되지 않는다)
4. 오라클이 저장소 안(`tests/check.sh`, `tests/expected.txt`)에 있어서 **체크아웃마다 판정 기준이 바뀐다** (c085 이후엔 버그가 있어도 통과)

### Part C — 펌웨어 현실: 장비, 잡음, 시간

- 성능 회귀(c088)를 `--term-old=fast --term-new=slow` 로 찾는다 (추가 연습 P1, 체커 채점): median, 기준 바이너리와 교차 측정, 회색 지대 → 125.
- HIL 테스트(빌드 → 플래시 → 리셋 → UART 판정)를 bisect 스크립트로 만들 때의 종료 코드 정책과 단계당 시간 계산.
- 같은 저장소·스크립트를 VM(Linux, git 2.53, make 4.4.1, gcc 15)에서 돌려 **해시와 경로가 같음**을 확인한다.

## 2. Problem (English)

> **Automate a regression hunt with `git bisect run`.**
>
> `gen_repo.sh <dir>` creates a deterministic 100-commit repository (fixed authors/dates, no global git config). Release `v1.0` is good; `main` crashes in its self-test. Somewhere in between a commit introduced the regression; two consecutive commits do not compile; one later commit weakened the in-repo smoke test.
>
> Write the bisect run script (it must live **outside** the repository) so that `git bisect start main v1.0 && git bisect run /path/to/script` reports the true first bad commit:
> 1. Exit 0 if the commit is good, 1 if bad, **125** if it cannot be built, and ≥ 128 only when the test rig itself is broken (so the bisect stops instead of recording garbage).
> 2. A crashing binary (e.g. `SIGABRT`, shell status 134) is a *bad commit*, not a rig failure.
> 3. Every step must build from a clean tree — nothing from the previous checkout may be reused.
> 4. The pass/fail criterion must be identical at every commit (do not use the repository's own, changing, test).
>
> Expected: first bad commit `[c063] ringbuf: tidy wrap arithmetic`, at most 8 good/bad decisions plus skips only on the two broken commits. Follow-ups: why 125 and not 1 for a build failure; what `git bisect run` does with 126/127 and with ≥ 128; how many steps for N commits; what to do with flaky or noisy tests (performance); merges and `--first-parent`; how you would bisect firmware that has to be flashed to a board.

```text
$ git bisect start main v1.0
Bisecting: 49 revisions left to test after this (roughly 6 steps)
[ec0e9e3…] [c049] build: tidy CFLAGS comment
$ git bisect run ~/bisect/solution_test.sh
...
ae61600b5f8c58141cb592ad898e867bb7b605b1 is the first bad commit
```

## 3. 시작 코드

```text
TOOL-M08/
  gen_repo.sh                       # 100-커밋 결정적 저장소 생성기 (변형 아님, 공용)
  starter_test.sh solution_test.sh  # 변형 = bisect run 스크립트 (기능 회귀)
  practice_test.sh                  # 추가 연습 = 성능 회귀 bisect 스크립트 (terms fast/slow)
  check.sh                          # 체커
  demo_contract.sh  demo_bisect.sh  demo_first_parent.sh  demo_perf.sh  demo_vm.sh  (+ *_out.txt 실제 출력)
```

저장소 생성기 — 결정성의 열쇠는 `GIT_CONFIG_GLOBAL=/dev/null`, `GIT_CONFIG_NOSYSTEM=1`, `init.defaultBranch=main`, 커밋마다 고정 `GIT_AUTHOR_DATE/GIT_COMMITTER_DATE` (`@epoch +0000`) 와 이름/메일이다. 그래서 맥(git 2.54)과 VM(git 2.53)에서 HEAD 가 똑같이 `5841db8daf72…` 다.

<!-- code: gen_repo.sh -->

고칠 파일 — `starter_test.sh` (TODO 4 개):

<!-- code: starter_test.sh -->

체커 — 저장소를 `/tmp/TOOL-M08_work/<variant>/repo` 에 새로 만들고, 변형 스크립트를 저장소 **밖**(`/tmp/TOOL-M08_work/<variant>/test.sh`)에 복사한다. ① 골라 둔 커밋에서 스크립트의 종료 코드를 하나씩 확인(단위 검사)하고 ② 진짜 `git bisect run` 을 돌려 `refs/bisect/bad` 가 c063 인지, `git bisect log` 의 판정/skip 수를 본다.

<!-- code: check.sh -->

```bash
bash check.sh starter     # 3/9, exit 1
bash check.sh solution    # 9/9, exit 0   (~12 s: 저장소 생성 ~5 s + 스크립트 실행 16 번)
bash check.sh practice    # 6/6, exit 0   (~20 s: 벤치 5×2 회/단계)
```

`bash check.sh starter` 실제 출력:

```text
== starter: git version 2.54.0 (Apple Git-156), GNU Make 3.81 ==
PASS c049 (healthy) -> 0 good
FAIL c060 (does not compile) -> want 125 skip, got 1 (bad)
     an unbuildable commit says nothing about the regression
FAIL c074 (app aborts in assert) -> want 1..124, got 134 (ABORTS the bisect)
     a crash is a bad commit; rc >= 128 stops git bisect run
FAIL c090 (bug present, repo test weakened at c085) -> want bad, got 0 (good)
     oracle taken from the checkout changes with the checkout
FAIL c062 -> c063 without cleaning (header-only change) -> want bad, got 0 (good)
     stale build/ from the previous step was tested
PASS work tree clean after a step (no tracked edits, no untracked files)
FAIL git bisect run main v1.0 -> want [c063] (rc=0)
     5841db8daf7203e4403660fd9979d30ca320fada is the first bad commit -> landed on c099
FAIL steps: want <= 8 good/bad + skips only on c060/c061
     good=7 bad=0 skip=0 {}
PASS git bisect reset -> back on main, tree clean
3/9 passed
```

가장 무서운 줄은 마지막 FAIL 두 개다: `git bisect run` 은 **성공(rc=0)** 했고 "first bad commit" 을 자신 있게 출력했다 — 그런데 그건 문서만 고친 c099 다. 경고는 하나도 없다.

## 4. 힌트

<details>
<summary>힌트 1 — 빌드 실패는 "판단 불가" 다</summary>

빌드가 안 되는 커밋은 회귀가 있는지 없는지 **아무 정보도** 주지 않는다. "bad" 라고 하면 bisect 는 범인을 그 구간 안으로 좁혀 버린다. `make … || exit 125` — git 이 근처의 다른 커밋을 대신 골라 준다.

</details>

<details>
<summary>힌트 2 — 128 이상은 git 에게 "그만" 이다</summary>

셸은 시그널로 죽은 프로세스의 상태를 128 + 시그널 번호로 보고한다 (SIGABRT 6 → 134, SIGSEGV 11 → 139). `git bisect run` 은 ≥ 128 을 "스크립트가 이상하다" 로 보고 멈춘다. 앱의 rc 를 그대로 `exit` 하지 말고, 0 이 아니면 `exit 1` 로 **번역**하라.

</details>

<details>
<summary>힌트 3 — 이전 단계의 build/ 를 믿지 마라</summary>

`build/` 는 `.gitignore` 에 있어서 체크아웃해도 그대로 남는다. 저장소의 Makefile 은 헤더 의존성이 없고(TOOL-B08 의 starter 와 같은 "레거시" Makefile), 맥 make 3.81 은 초 단위로 시각을 비교한다. 매 단계 `git clean -fdxq` 로 무시된 파일까지 지우고 처음부터 빌드한다.

</details>

<details>
<summary>힌트 4 — 판정 기준은 bisect 시작 시점에 얼려라</summary>

`tests/check.sh` 는 c020 이전에는 없고, c085 부터는 ringbuf 케이스를 건너뛴다. 기대 출력을 스크립트 안에 상수로 두면(또는 저장소 밖으로 복사해 두면) 모든 커밋이 같은 기준으로 판정된다.

</details>

## 5. 풀이

### Part A — bisect run 이 실제로 하는 일과 종료 코드 규약

`git bisect start main v1.0` 은 `main` 에서 닿을 수 있고 `v1.0` 에서는 닿을 수 없는 커밋(99 개)을 의심 집합으로 잡고, 그 집합을 **가장 반에 가깝게 나누는** 커밋을 체크아웃한다. 선형 이력에서는 가운데(c049)다. `git bisect run X` 는 그 다음부터 루프다: X 실행 → 종료 코드로 `good/bad/skip` 기록 → 다음 커밋 체크아웃. 매 단계 git 이 출력하는 `roughly N steps` 는 남은 후보 수의 log₂ 다 (시작 시 49 → "roughly 6 steps", 이미 체크아웃한 c049 를 빼고).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">git bisect run 종료 코드 → 의미 (git 2.54 실측, demo_contract_out.txt)</text>
  <rect x="10" y="36" width="80" height="50" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="50" y="58" fill="currentColor" text-anchor="middle" font-family="monospace">0</text>
  <text x="50" y="76" fill="currentColor" text-anchor="middle" font-size="12">good / old</text>
  <rect x="96" y="36" width="170" height="50" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="181" y="58" fill="currentColor" text-anchor="middle" font-family="monospace">1 … 124</text>
  <text x="181" y="76" fill="currentColor" text-anchor="middle" font-size="12">bad / new</text>
  <rect x="272" y="36" width="90" height="50" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="317" y="58" fill="currentColor" text-anchor="middle" font-family="monospace">125</text>
  <text x="317" y="76" fill="currentColor" text-anchor="middle" font-size="12">skip</text>
  <rect x="368" y="36" width="140" height="50" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="438" y="58" fill="currentColor" text-anchor="middle" font-family="monospace">126, 127</text>
  <text x="438" y="76" fill="currentColor" text-anchor="middle" font-size="12">bad (처음엔 재확인*)</text>
  <rect x="514" y="36" width="216" height="50" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="622" y="58" fill="currentColor" text-anchor="middle" font-family="monospace">128 … 255</text>
  <text x="622" y="76" fill="currentColor" text-anchor="middle" font-size="12">bisect run 중단</text>
  <text x="10" y="112" fill="currentColor" font-weight="bold">solution_test.sh 의 번역표</text>
  <g font-size="12" fill="currentColor">
    <text x="10" y="134">make / cc 가 PATH 에 없음 (장비 고장)</text><text x="400" y="134" font-family="monospace">→ exit 255  (멈춰라)</text>
    <text x="10" y="154">make 실패 (이 커밋은 컴파일 안 됨)</text><text x="400" y="154" font-family="monospace">→ exit 125  (건너뛰어라)</text>
    <text x="10" y="174">앱 rc ≠ 0 (assert → SIGABRT → 134 포함)</text><text x="400" y="174" font-family="monospace">→ exit 1    (bad)</text>
    <text x="10" y="194">출력 ≠ 얼려 둔 기대값</text><text x="400" y="194" font-family="monospace">→ exit 1    (bad)</text>
    <text x="10" y="214">출력 = 기대값</text><text x="400" y="214" font-family="monospace">→ exit 0    (good)</text>
  </g>
  <text x="10" y="242" fill="currentColor" font-size="12">* 처음 126/127 이 나오면 git 이 알려진 good 커밋에서 스크립트를 한 번 더 돌려 본다 (아래 실측).</text>
</svg>
```

`demo_contract.sh` (16 커밋 장난감 저장소, t09 부터 bug) 실측 — 규약을 외우는 대신 보자:

| 스크립트가 bad 에서 낸 코드 | git 2.54 의 반응 (실제 메시지) | `git bisect run` 종료 코드 |
|---|---|---|
| 1 | `cef2b19… is the first bad commit` (t09) | 0 |
| 125 (t05, t06 에서) | 경로가 t07/t11/t09/t08 이라 t05·t06 은 밟지도 않음 → **정확히 t09** (범인에서 먼 깨짐은 무해) | 0 |
| 125 (t07, t08 = 범인 바로 앞) | `There are only 'skip'ped commits left to test.` / `The first bad commit could be any of:` t08 t07 t09 | 2 |
| 126 / 127 | bad 로 처리, t09 정확 | 0 |
| SIGABRT (134) | `error: '…/rc134.sh' died of signal 6` / `error: bisect run failed: exit code 134 from '…' is < 0 or >= 128` | 122 |
| `exit -1` (→ 255) | `exit code 255 from … is < 0 or >= 128` | 1 |
| 128 | `exit code 128 from … is < 0 or >= 128` | 128 |
| 스크립트 경로 오타 | `No such file or directory` 2 번 → `error: bogus exit code 127 for good revision` | 1 |
| 항상 0 (버그 재현 실패) | `b3f7618… is the first bad commit` = **t15, 처음에 bad 라고 준 끝점** | 0 |

126/127 의 "재확인" 은 man page 에 없는 동작이라 verbose 로 다시 봤다:

```text
=== 127 again, verbose: which commits does git actually run the script on?
  [script ran at t7]
[19e48478…] t11
  [script ran at t11]          ← 처음 127
[6342f3f5…] t00                ← git 이 알려진 good(t00)을 체크아웃해서
  [script ran at t0]           ← 한 번 더 돌린다: 0 이면 "스크립트는 멀쩡" → 127 을 bad 로 인정
[19e48478…] t11
[cef2b19b…] t09
  [script ran at t9]           ← 두 번째 127 부터는 재확인 없음
...
cef2b19b2d8807266429076eb7918eee60a12f41 is the first bad commit
```

즉 "명령을 못 찾음(127)" 을 회귀로 착각하는 사고를 막으려는 안전장치다 — 경로 오타 실험에서 good 에서도 127 이 나오자 `bogus exit code 127 for good revision` 으로 멈췄다. (git 2.54/2.53 에서 관찰한 동작. 도입 버전은 확인 권장. 어쨌든 **의도적으로 126/127 을 쓰지 말 것**.) `git bisect run` 자체의 종료 코드(122, 1, 128…)는 일정하지 않으니 CI 에서는 출력의 `is the first bad commit` 문자열이나 `refs/bisect/bad` 를 보는 게 안전하다.

solution 실측 (`demo_bisect_out.txt`, 9 번 실행 · 판정 7 + skip 2):

```text
Bisecting: 24 revisions left to test after this (roughly 5 steps)
[15e9d5cb…] [c074] crc16: comment polynomial
bisect-test: app rc=134 -> bad
Bisecting: 12 revisions left to test after this (roughly 4 steps)
[106926f0…] [c061] docs: describe hal_ prefix          ← 125 (컴파일 에러)
Bisecting: 12 revisions left to test after this (roughly 4 steps)
[f6612752…] [c064] ringbuf: document drop-when-full policy
bisect-test: app rc=134 -> bad
Bisecting: 6 revisions left to test after this (roughly 3 steps)
[8040317f…] [c057] app: comment selftest order
Bisecting: 2 revisions left to test after this (roughly 2 steps)
[93031ed6…] [c060] hal: rename uart_puts -> hal_uart_puts   ← 125
Bisecting: 2 revisions left to test after this (roughly 2 steps)
[eabc12fa…] [c059] chore: update changelog format
Bisecting: 2 revisions left to test after this (roughly 1 step)
[23323833…] [c062] app: fix build after HAL rename
Bisecting: 0 revisions left to test after this (roughly 0 steps)
[ae61600b…] [c063] ringbuf: tidy wrap arithmetic
bisect-test: app rc=134 -> bad
ae61600b5f8c58141cb592ad898e867bb7b605b1 is the first bad commit
bisect found first bad commit
wall time: 5.2 s
```

읽는 법: skip 직후 `revisions left` 가 줄지 않는다(12 → 12). skip 은 정보가 없으니 구간을 못 줄이고, git 은 반값 지점 **근처의 다른** 커밋(c064)을 고른다. 범인 c063 은 skip 된 c060/c061 과 붙어 있지 않아서(c062 가 good 으로 판정 가능) 정확히 하나로 좁혀졌다. 같은 경로가 VM(git 2.53, gcc 15)에서도 그대로 나왔다 — 해시가 같으면 bisect 의 선택도 같다 (`demo_vm_out.txt`: `good+bad decisions: 7`, `skips: 2`).

단계 수: 판정 7 번 = ⌈log₂ 99⌉. 한 단계(클린 빌드 + 실행)는 이 맥에서 0.46–0.63 s (5 샘플, 부하에 따라 변동), bisect 전체 5.2 s. 99 커밋을 하나씩 빌드했다면 대략 50 s — 여기선 차이가 작아 보이지만, 단계가 10 분짜리 HIL 이면 70 분 vs 16 시간이다 (Part C).

`git bisect log` 실측 (앞부분 + 끝, 해시 줄임):

```text
# bad: [5841db8d…] [c099] docs: add bring-up notes
# good: [f598e3cb…] [c000] initial import: crc16, ringbuf, uart shim, app
git bisect start 'main' 'v1.0'
# good: [ec0e9e36…] [c049] build: tidy CFLAGS comment
git bisect good ec0e9e364aeebf698138cc6c535deb2ec4cb0b17
# bad: [15e9d5cb…] [c074] crc16: comment polynomial
git bisect bad 15e9d5cb4a157a2b41146378f2f33efd08e6c757
# skip: [106926f0…] [c061] docs: describe hal_ prefix
git bisect skip 106926f0539fc7ba0932b431413e3dc2dec75418
…
# first bad commit: [ae61600b…] [c063] ringbuf: tidy wrap arithmetic
```

`#` 줄은 사람용 주석(커밋 제목), `git bisect …` 줄이 실제 명령이다. 이 log 는 그 자체로 재현 기록이다. 그대로 `git bisect replay bisect.log` 하면 스크립트도 빌드도 없이 같은 결론에 도착한다 (실측: `ae61600… is the first bad commit`). 버그 리포트에 log 를 붙이는 습관이 좋다.

### Part B — starter 의 함정 4 개, 각각 실제로 어떻게 무는가

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">starter 가 c099 를 범인으로 지목한 실제 경로 — 모든 단계가 "good"</text>
  <g font-size="12" fill="currentColor">
    <text x="10" y="48" font-weight="bold">테스트한 커밋</text><text x="160" y="48" font-weight="bold">starter 판정</text>
    <text x="290" y="48" font-weight="bold">진실</text><text x="370" y="48" font-weight="bold">왜 good 이 나왔나</text>
  </g>
  <g font-size="12" fill="currentColor" font-family="monospace">
    <text x="10" y="74">c049</text><text x="10" y="100">c074</text><text x="10" y="126">c086</text>
    <text x="10" y="152">c092 c095</text><text x="10" y="178">c097 c098</text>
  </g>
  <rect x="160" y="62" width="100" height="18" class="f-ok" stroke="currentColor"/><text x="170" y="76" fill="currentColor" font-size="12">good</text>
  <rect x="160" y="88" width="100" height="18" class="f-ok" stroke="currentColor"/><text x="170" y="102" fill="currentColor" font-size="12">good</text>
  <rect x="160" y="114" width="100" height="18" class="f-ok" stroke="currentColor"/><text x="170" y="128" fill="currentColor" font-size="12">good</text>
  <rect x="160" y="140" width="100" height="18" class="f-ok" stroke="currentColor"/><text x="170" y="154" fill="currentColor" font-size="12">good</text>
  <rect x="160" y="166" width="100" height="18" class="f-ok" stroke="currentColor"/><text x="170" y="180" fill="currentColor" font-size="12">good</text>
  <rect x="290" y="62" width="60" height="18" class="f-ok" stroke="currentColor"/>
  <rect x="290" y="88" width="60" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="290" y="114" width="60" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="290" y="140" width="60" height="18" class="f-warn" stroke="currentColor"/>
  <rect x="290" y="166" width="60" height="18" class="f-warn" stroke="currentColor"/>
  <g font-size="12" fill="currentColor">
    <text x="370" y="76">처음 빌드 — 진짜 good</text>
    <text x="370" y="102">함정 3: ringbuf.o 가 c049 것 (헤더 의존성 X)</text>
    <text x="370" y="128">함정 3+4: 저장소 테스트가 ringbuf 안 봄 (c085~)</text>
    <text x="370" y="152">함정 4: 클린 빌드여도 good (오라클 약화)</text>
    <text x="370" y="178">〃</text>
  </g>
  <rect x="10" y="200" width="720" height="44" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="20" y="218" fill="currentColor">bad 는 처음에 준 c099 하나뿐 → "c099 is the first bad commit" (문서만 고친 커밋)</text>
  <text x="20" y="236" fill="currentColor" font-size="12">bisect 는 스크립트를 믿는다. 판정이 단조가 아니거나 틀리면, 틀린 답을 "성공" 으로 낸다.</text>
  <text x="10" y="268" fill="currentColor" font-size="12">함정 1(빌드 실패 → 1), 2(rc 134 전달)는 이 경로가 c060/c061/c063 을 안 밟아 안 터졌다.</text>
  <text x="10" y="286" fill="currentColor" font-size="12">그래서 체커는 단위 검사로 하나씩 본다. 경로가 달랐다면 c060 지목 또는 rc 134 중단.</text>
</svg>
```

**함정 1 — 빌드 실패를 bad 로.** 체커 단위 검사: c060 에서 starter 는 1. bisect 경로가 c060/c061 을 bad 로 받으면, 그 앞은 전부 good 이므로 "첫 bad = c060 (HAL rename)" 으로 끝난다. 해결은 125. 단, 125 가 **범인 바로 옆**에 몰리면 git 도 결정할 수 없다 (`demo_contract`: t07, t08 skip → 후보 3 개, exit 2). 그래서 실무 규칙: "모든 커밋이 빌드되게"(CI 가 커밋 단위로 빌드), 깨진 구간이 알려져 있으면 `git bisect skip c059..c061` 처럼 **범위로** 미리 빼 둔다 (실측: 한 줄로 c060, c061 두 개 skip).

VM 의 gcc 15 는 같은 에러를 이렇게 낸다 (맥 clang: `call to undeclared function 'uart_puts'; ISO C99 and later do not support implicit function declarations`):

```text
src/main.c:17:5: error: implicit declaration of function 'uart_puts'; did you mean 'hal_uart_puts'? [-Wimplicit-function-declaration]
```

**함정 2 — 크래시 rc 를 그대로 전달.** 범인 c063 의 앱은 `assert(rb->head < RB_CAP)` 에서 죽는다. 맥: `Assertion failed: (rb->head < RB_CAP), function rb_push, file ringbuf.c, line 9.`, VM glibc: `app: src/ringbuf.c:9: rb_push: Assertion 'rb->head < RB_CAP' failed.` — 둘 다 SIGABRT → 셸 상태 134. 저장소의 `tests/check.sh` 는 `./build/app > build/out.txt || exit $?` 로 134 를 올려 보내고, starter 는 `exit "$rc"` 로 그대로 git 에 준다 → `bisect run failed: exit code 134 … is < 0 or >= 128` 로 중단. "크래시 = 회귀의 가장 흔한 증상" 인데 하필 그때 bisect 가 멈춘다. 해결: 앱 rc ≠ 0 이면 무조건 `exit 1`. 반대로 **장비가 고장 났을 때** 일부러 ≥ 128 을 쓴다 — 아래 Part C.

**함정 3 — 이전 단계의 build/ 재사용.** `demo_bisect.sh` 실측 두 장면:

```text
### why (a): build c049, checkout c074, plain make in the SAME second (what the starter does)
  make: cc -std=c11 -O2 -Wall -Iinclude -c src/main.c -o build/main.o
  app: fw version 1.0.49                  ← c074 인데 버전이 49! (재링크도 안 됨)
  app: rb order = 4 5 6 7 8 21 22 23      ← 버그가 있어야 할 커밋에서 통과
  mtime 1794793178  build/app
  mtime 1794793178  build/version.o       ← 같은 초 → make 3.81 은 "새롭지 않다"
### why (b): same, but the c049 build is an hour old (normal when a step takes minutes)
  make: cc ... -c src/main.c -o build/main.o
  make: cc ... -c src/uart.c -o build/uart.o
  make: cc ... -c src/version.c -o build/version.o
  make: cc build/crc16.o build/main.o build/ringbuf.o build/uart.o build/version.o -o build/app
  app: fw version 1.0.74
  app: rb order = 4 5 6 7 8 21 22 23      ← 재링크는 됐지만 ringbuf.o 는 c049 것
```

두 겹의 이유다: (b) 저장소 Makefile 에 헤더 의존성이 없어서 `ringbuf.h` 만 바뀐 c063 이후에도 `ringbuf.o` 를 다시 만들지 않는다 (TOOL-B08 의 바로 그 버그). (a) 게다가 단계가 1 초 안에 끝나면 맥 make 3.81 의 **초 단위** 시각 비교 때문에 `.c` 가 바뀐 것도 놓친다(`git checkout` 은 바뀐 파일의 mtime 을 "지금" 으로 쓴다). 해결: 매 단계 `git clean -fdxq` (무시된 파일까지, `-x`) 후 처음부터. 빌드가 길면 ccache(컴파일 캐시 — 내용 해시라 체크아웃에 안전)나 단계별 별도 빌드 폴더를 쓴다. 주의: `git clean -fdx` 는 저장소 안의 **추적 안 되는 모든 것**을 지운다 — 작업 중인 클론에서 하지 말고 bisect 전용 클론/`git worktree` 에서 한다.

**함정 4 — 저장소 안의 오라클.** 회귀 테스트는 보통 버그를 **발견한 뒤에** 추가된다. 그러면 옛 커밋에는 그 테스트가 없다(c020 이전엔 `tests/check.sh` 가 아예 없다 → 127). 더 교묘한 것이 c085: "flaky" 라며 ringbuf 케이스를 끈 커밋이다 — 그 뒤 커밋은 저장소 테스트로는 영원히 통과한다. 체커 단위 검사: c090 에서 starter 는 클린 빌드여도 0. 해결: 판정 기준(기대 출력, 또는 테스트 파일 자체)을 bisect 시작 시점에 **얼려서** 저장소 밖에 둔다. 스크립트 자체도 저장소 밖에 둔다 — `demo_contract` 의 마지막 실험: 저장소 안의 추적 안 되는 `inrepo.sh` 가 `git clean -fdx` 로 **자기 자신을 지우고**, 다음 단계에서 `No such file or directory` → `bogus exit code 127 for good revision` 으로 중단됐다 (`inrepo.sh still exists? no`).

**단조성 점검 — bisect 전에 끝점 두 개를 직접 돌려 보라.** `git bisect run` 은 시작할 때 good/bad 끝점에서 스크립트를 돌려 보지 **않는다** (실측: 항상 0 인 스크립트로도 "t15 is the first bad commit" 을 출력). 그러니 `git checkout main && ./test.sh; echo $?` (1 이어야), `git checkout v1.0 && ./test.sh; echo $?` (0 이어야) 를 먼저 확인한다. starter 는 main 에서 0 을 냈을 것이다 — 그 30 초가 엉뚱한 범인을 막는다. 결과가 BAD 끝점 자신이면 거의 항상 "테스트가 버그를 재현하지 못했다" 는 뜻이다.

### Part C — 펌웨어 현실

**① 장비 고장은 skip 이 아니라 abort.** VM 에서 처음 이 레슨을 돌렸을 때, `gen_repo.sh` 의 main.c 에 `_POSIX_C_SOURCE` 가 없어서 glibc + `-std=c11` 에서 `clock_gettime` 이 선언되지 않았다 → **모든** 커밋이 빌드 실패 → 전부 125 (`demo_vm_first_attempt_out.txt`: `good+bad decisions: 0`, `skips: 98`, 후보 99 개 나열 후 `We cannot bisect more!`). 같은 일이 HIL 에서는 "보드가 USB 에서 빠짐", "플래셔 라이선스 만료" 로 일어난다. 장비가 원인인데 125 를 내면 bisect 가 커밋들을 하나씩 다 버린다. 그래서 solution 첫 줄이 장비 점검이다: `command -v make && command -v "${CC:-cc}" || exit 255`. 실측(VM, `CC=gcc-99`): 점검이 없으면 98 skip 후 `bisect run cannot continue any more`, 있으면 첫 단계에서 `exit code 255 … is < 0 or >= 128` 로 즉시 멈춘다.

**② HIL 스크립트의 모양.** (아래 명령은 예시 — 보드/도구마다 다름)

```bash
#!/bin/bash
# bisect run script for a Cortex-M board on a HIL rig (sketch)
probe_ok || exit 255                                 # 프로브/보드/전원 릴레이 점검 실패 → 멈춤
git clean -fdxq && cmake --build build-m4 || exit 125   # 이 커밋은 빌드 안 됨
for try in 1 2 3; do flash build-m4/fw.elf && break; done || exit 255   # 플래시 3 번 실패 = 장비 문제
relay_power_cycle
uart_capture --timeout 30 > log.txt                  # 무한 대기 금지: 타임아웃 필수
grep -q 'SELFTEST PASS' log.txt && exit 0
grep -q 'HardFault' log.txt && exit 1                # 크래시 = bad (rc 를 그대로 넘기지 않음)
exit 1
```

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m8a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">HIL bisect 한 단계 — 실패 지점마다 종료 코드가 다르다</text>
  <g font-size="12" fill="currentColor" text-anchor="middle">
    <rect x="10" y="40" width="110" height="40" rx="5" class="f-mut" stroke="currentColor"/><text x="65" y="64">장비 점검</text>
    <rect x="160" y="40" width="110" height="40" rx="5" class="f-mut" stroke="currentColor"/><text x="215" y="64">클린 빌드</text>
    <rect x="310" y="40" width="110" height="40" rx="5" class="f-mut" stroke="currentColor"/><text x="365" y="64">플래시 (×3)</text>
    <rect x="460" y="40" width="110" height="40" rx="5" class="f-mut" stroke="currentColor"/><text x="515" y="64">리셋 + UART</text>
    <rect x="610" y="40" width="120" height="40" rx="5" class="f-ok" stroke="currentColor"/><text x="670" y="64">PASS → 0</text>
  </g>
  <g stroke="currentColor" marker-end="url(#m8a)">
    <path d="M120 60 L158 60"/><path d="M270 60 L308 60"/><path d="M420 60 L458 60"/><path d="M570 60 L608 60"/>
    <path d="M65 80 L65 118"/><path d="M215 80 L215 118"/><path d="M365 80 L365 118"/><path d="M515 80 L515 118"/>
  </g>
  <g font-size="12" fill="currentColor" text-anchor="middle">
    <rect x="10" y="120" width="110" height="34" rx="5" class="f-acc" stroke="currentColor"/><text x="65" y="142">255 멈춤</text>
    <rect x="160" y="120" width="110" height="34" rx="5" class="f-mut" stroke="currentColor"/><text x="215" y="142">125 skip</text>
    <rect x="310" y="120" width="110" height="34" rx="5" class="f-acc" stroke="currentColor"/><text x="365" y="142">255 멈춤</text>
    <rect x="440" y="120" width="160" height="34" rx="5" class="f-warn" stroke="currentColor"/><text x="520" y="142">FAIL/Fault/타임아웃 → 1</text>
  </g>
  <text x="10" y="184" fill="currentColor" font-size="12">원칙: "이 커밋을 판정할 수 없다" = 125, "판정 장치가 고장" = 255, "판정했더니 나쁨" = 1 (크래시 포함).</text>
</svg>
```

**③ 시간 계산.** 가정: 빌드 3 분 + 플래시 30 초 + 부팅·테스트 1 분 ≈ 4.5 분/단계. 300 커밋이면 ⌈log₂ 300⌉ = 9 판정 + skip 몇 개 → 약 45–55 분. 선형이면 최악 22 시간. 실무 최적화: (a) CI 가 커밋마다 남긴 **빌드 산출물(ELF)을 다운로드**해 빌드 단계를 생략 (아티팩트 보관 기간 안에서), (b) 테스트를 회귀를 재현하는 **최소 케이스**로 줄이기, (c) 플래시 대신 RAM 로드/QEMU 로 1차 bisect 후 보드로 확인. 이 레슨의 호스트 앱에서는 한 단계가 0.5 s 안팎이라 실감이 안 나지만, 비율은 같다.

**④ 잡음 있는 판정 (성능).** c088 은 결과가 같은 CRC 를 테이블 대신 비트 단위로 계산한다. `demo_perf.sh` 실측 (9 번 교차 실행, 다른 작업 10 여 개가 같이 도는 맥):

```text
c087 min/median/max 39.6 / 60.2 / 96.8 ms
c088 min/median/max 358.9 / 527.8 / 630.9 ms
ratio of medians 8.76   worst single-run ratio (c088 min / c087 max) 3.71
```

같은 코드(c087)가 39.6 ms 에서 96.8 ms 까지 2.4 배 흔들린다. 체커를 돌린 다른 시각에는 비율이 3.2 안팎이었다 (부하·코어 배치에 따라 비율 자체도 변함). 그래서 practice 스크립트는 (1) 기준 바이너리(v1.0)를 한 번 빌드해 `.git/perf-base-<sha>` 에 두고 (2) 기준 A 와 현재 B 를 **A B A B …** 로 번갈아 5 번씩 재서 (3) median 의 비율로 판정하고 (4) 2.0 초과 → slow(1), 1.4 미만 → fast(0), 그 사이(회색 지대) → **125** 로 판정을 미룬다. 비율 기준은 기계 부하가 양쪽에 같이 걸리게 하려는 것이다. 잘못된 판정 하나가 bisect 전체를 틀어 버리므로(단계마다 오판 확률 p 면 전체는 대략 단계수 × p), "모르면 skip" 이 "찍기" 보다 낫다.

그리고 bisect 가 찾은 다음엔 **트레이드오프를 확인**한다. 커밋 메시지는 "512 B flash 절약" 이지만 Cortex-M4 오브젝트로 재 보면:

```text
c087: .text=92 .data=0 .bss=512
c088: .text=52 .data=0 .bss=0
```

테이블은 `crc16_init()` 이 실행 중에 채우므로 **RAM(.bss) 512 B** 를 아낀 것이지 flash 가 아니다 (flash 는 `.text` 40 B 감소). `const` 테이블로 컴파일 시점에 만들면 flash 512 B + RAM 0 + 빠른 속도 — 이게 진짜 해결책일 수 있다 (TOOL-B06 의 .data/.bss 구분, TOOL-M13 size diet 참고).

## 6. 정답 코드

<!-- code: solution_test.sh diff=starter_test.sh -->

`bash check.sh solution` 실제 출력:

```text
== solution: git version 2.54.0 (Apple Git-156), GNU Make 3.81 ==
PASS c049 (healthy) -> 0 good
PASS c060 (does not compile) -> 125 skip
PASS c074 (app aborts in assert) -> 1 bad
PASS c090 (bug present, repo test weakened at c085) -> 1 bad
PASS c062 -> c063 without cleaning (header-only change) -> 1 bad
PASS work tree clean after a step (no tracked edits, no untracked files)
PASS git bisect run main v1.0 -> first bad = c063 ringbuf: tidy wrap arithmetic
PASS steps: 7 good/bad + 2 skip {c061 c060}  (bound <= 8 = ceil(log2 99) + 1)
PASS git bisect reset -> back on main, tree clean
9/9 passed
```

한 줄씩 왜 그런가:

| 줄 | 하는 일 | 막는 함정 |
|---|---|---|
| `EXPECTED='…'` | 기대 출력을 스크립트 안에 고정 (버전 줄은 비교에서 제외 — 커밋마다 바뀜) | 4 |
| `command -v make … \|\| exit 255` | 장비 고장이면 bisect 중단 | Part C ① |
| `git clean -fdxq` | `build/` 포함 무시된 파일까지 삭제 → 매번 클린 빌드 | 3 |
| `make -s app … \|\| exit 125` | 컴파일 안 되는 커밋은 판정 보류 | 1 |
| `rc ≠ 0 → exit 1` | 134(SIGABRT) 같은 크래시를 bad 로 번역 | 2 |
| 출력 비교 → 0 / 1 | 오라클과 같으면 good | — |

`set -u` 는 쓰지만 `set -e` 는 일부러 쓰지 않았다: `set -e` 상태에서 `out=$(./build/app)` 이 134 로 끝나면 스크립트가 **134 로** 그 자리에서 종료된다 — 함정 2 를 스스로 다시 만드는 셈이다.

## 7. 자주 하는 실수

**1. `make || exit 1`.** 빌드 깨짐을 회귀로 기록. → `|| exit 125`. 깨진 구간이 범인과 붙어 있으면 git 도 후보 목록만 준다 — 그건 정상이고, 그 몇 개를 손으로 본다.

**2. `./app; exit $?`.** 크래시(134/139)가 bisect 를 멈춘다. → 0 이 아니면 `exit 1`. 반대로 타임아웃(무한 루프)도 bad 로 번역해야 한다 — 맥에는 `timeout` 이 없으니 `perl -e 'alarm 10; exec @ARGV' ./app` 같은 방법을 쓰는데, 시간 초과 시 rc 는 **142**(128 + SIGALRM 14, 실측) 라 이것도 1 로 번역해야 한다 (이 레슨 앱은 안 멈춰서 넣지 않았다).

**3. `set -e` 를 습관처럼.** 위 6. 정답 코드 설명 참고 — 실패한 명령의 rc 가 그대로 스크립트의 rc 가 된다.

**4. 이전 단계의 빌드를 재사용.** 헤더 의존성이 없는 Makefile + 초 단위 시각 → 실측으로 "c074 가 버전 1.0.49 를 출력". → `git clean -fdxq`, 또는 단계마다 새 빌드 폴더.

**5. 저장소 안의 테스트를 오라클로.** 테스트가 버그보다 나중에 생기거나, 중간에 약해진다(c085). → 테스트/기대값을 bisect 시작 때 저장소 밖으로 복사. 저장소에 새 테스트를 넣어야 하면 스크립트가 **매 단계 그 파일을 복사해 넣고** 끝나면 `git checkout -- .`/`git clean` 으로 지운다(빌드가 추적 파일을 고쳐 놓으면 다음 체크아웃이 거부될 수 있다 — `Your local changes … would be overwritten`, 확인 권장).

**6. 스크립트를 저장소 안에 두기.** 체크아웃마다 내용이 바뀌거나(추적 파일), `git clean -fdx` 에 지워진다(추적 안 되는 파일 — 실측). → 저장소 밖 경로로.

**7. 끝점을 확인하지 않고 시작.** `git bisect run` 은 끝점을 검증하지 않는다. 결과가 BAD 끝점 자체면 테스트가 버그를 못 재현한 것. → 시작 전에 good/bad 에서 한 번씩 직접 실행.

**8. 성능 회귀를 한 번 측정으로.** 실측 같은 코드가 2.4 배 흔들린다. → median, 기준과 교차 측정, 넉넉한 임계값, 회색 지대 125.

**9. 장비 고장에 125.** 모든 커밋이 skip 되어 bisect 가 아무것도 못 배운다(VM 실측 98 skip). → 장비 점검 실패는 255.

## 8. 면접 Q&A

<details>
<summary>Q1. git bisect 는 어떻게 동작하고, 100 커밋이면 몇 번 테스트하나요?</summary>

good 에서 닿을 수 없고 bad 에서 닿을 수 있는 커밋들이 의심 집합이다. 매 단계 그 집합을 가장 반에 가깝게 나누는 커밋(선형이면 가운데, 머지가 있으면 "조상 수가 집합 크기의 절반에 가장 가까운 커밋")을 고르고, 판정에 따라 반을 버린다. 판정 횟수 ⌈log₂ N⌉ — 99 후보면 7 (이 레슨 실측 7 + skip 2). 전제는 단조성: 회귀가 한 번 들어와서 계속 남아 있어야 한다. 중간에 고쳐졌다가 다시 깨지면(재발) 어느 한 경계를 찾을 뿐이라 결과를 해석할 때 주의.

</details>

<details>
<summary>Q2. bisect run 스크립트의 종료 코드 규약은? 왜 하필 125 인가요?</summary>

0 = good, 1–127 중 125 를 뺀 것 = bad, 125 = skip, 128 이상(그리고 음수 → 255) = bisect 중단. 125 는 "셸이 이미 쓰는 126(실행 불가)·127(명령 없음)을 피한 가장 큰 값" (man page). ≥ 128 이 중단인 이유: 셸이 시그널 종료를 128+N 으로 보고하므로 "스크립트 자체가 죽었다" 로 해석. 실측 덧붙임: 처음 126/127 이 나오면 git 이 알려진 good 에서 스크립트를 한 번 더 돌려 보고, 거기서도 127 이면 `bogus exit code 127 for good revision` 으로 멈춘다 — 경로 오타를 회귀로 착각하지 않게.

</details>

<details>
<summary>Q3. 빌드가 안 되는 커밋이 섞여 있으면?</summary>

125 로 skip. git 은 근처의 다른 커밋을 대신 고르고(실측: c061 skip 후 c064), 남은 후보가 skip 된 것뿐이면 `The first bad commit could be any of:` 로 후보를 준다 (범인 바로 앞 두 커밋이 skip 인 실험에서 3 개). 알려진 깨진 구간은 `git bisect skip A..B` 로 미리 뺀다. 근본 대책은 "모든 커밋이 빌드·테스트 통과" 를 CI 로 강제하는 것 — 스쿼시 머지나 커밋 단위 CI 가 bisect 가능성(bisectability)을 지킨다.

</details>

<details>
<summary>Q4. 회귀 테스트가 저장소에 나중에 추가됐습니다. 그 테스트로 bisect 하려면?</summary>

테스트 파일(또는 기대값)을 bisect 시작 전에 저장소 밖으로 복사하고, 스크립트가 각 커밋의 소스에 그것을 붙여 빌드·실행한다. 저장소 안의 테스트를 그대로 쓰면 옛 커밋에선 테스트가 없고(127 → bad 로 오판), 중간에 테스트가 바뀌면(이 레슨 c085) 판정 기준이 움직인다 — 실측으로 starter 가 c090 을 good 으로 판정. API 가 바뀌어 옛 커밋에 테스트가 컴파일되지 않으면 그 커밋은 125.

</details>

<details>
<summary>Q5. 간헐적(flaky) 실패나 성능 회귀는 어떻게 bisect 하나요?</summary>

한 번의 오판이 결과 전체를 틀리게 만드므로 판정을 튼튼하게: flaky 면 N 번 반복해서 "한 번이라도 실패 → bad" (실패 확률이 낮으면 N 을 키움), 성능이면 median + 기준 바이너리와 교차 측정 + 넉넉한 임계값 + 애매하면 125. 이 레슨 실측: 같은 코드가 39.6–96.8 ms 로 흔들렸지만 median 비율은 c088 전후로 ~1.0 vs 3.1–5.7 로 갈렸다. `--term-old=fast --term-new=slow` 로 로그도 읽기 쉽게. 측정 환경(CPU 주파수, 다른 부하)을 고정할 수 있으면 더 좋다.

</details>

<details>
<summary>Q6. 머지 커밋이 많은 저장소에서는? --first-parent 는 언제 쓰나요?</summary>

기본 bisect 는 머지된 브랜치 안의 커밋까지 내려가서 정확한 커밋을 찾는다 (실측: feature 브랜치의 `f2` 를 지목, 브랜치 안의 빌드 깨진 `f3` 은 skip). 브랜치 안 커밋들이 개별로는 빌드/테스트가 안 되는 경우가 많으면 `git bisect start --first-parent` 로 메인 라인만 본다 (실측: 머지 커밋 `M` 을 지목, 테스트 5 번). "어느 PR 이 깨뜨렸나" 가 질문이면 --first-parent 가 더 빠르고 안정적이다. (옵션 도입 버전 2.29 무렵 — 확인 권장)

</details>

<details>
<summary>Q7. 펌웨어를 보드에 올려야 판정할 수 있습니다. bisect 를 어떻게 자동화하나요?</summary>

스크립트 = 장비 점검(실패 → 255) → 클린 빌드(실패 → 125) → 플래시 재시도(계속 실패 → 255, 코드 탓이 아님) → 전원 사이클 → UART/GPIO 로 타임아웃 있는 판정(PASS → 0, FAIL/HardFault/타임아웃 → 1). 단계당 수 분이므로 단계 수가 곧 비용: 300 커밋이면 9 판정 ≈ 45 분 vs 선형 22 시간 (4.5 분/단계 가정). CI 아티팩트(커밋별 ELF)를 쓰면 빌드를 건너뛴다. 크래시 덤프(BM-A09, ARM-M14)를 판정에 쓰면 "같은 크래시인가" 까지 구분할 수 있다 — 다른 크래시는 다른 회귀일 수 있으니 125 로 보류하는 정책도 가능.

</details>

<details>
<summary>Q8. bisect 결과가 처음에 bad 라고 준 커밋(HEAD) 자신이었습니다. 무슨 뜻인가요?</summary>

테스트가 중간 어디에서도 bad 를 내지 못했다 — 즉 bisect 환경에서 버그가 재현되지 않았다. `git bisect run` 은 끝점을 검증하지 않으므로(실측: 항상 0 인 스크립트로도 결과를 출력) 이런 결과가 "성공" 으로 나온다. 이 레슨 starter 가 정확히 그랬다: 낡은 build/ 와 약해진 저장소 테스트 때문에 모든 단계가 good → c099 지목. 대책: 시작 전 bad/good 끝점에서 스크립트를 직접 돌려 1/0 이 나오는지 확인, 결과가 끝점이면 테스트부터 의심.

</details>

<details>
<summary>Q9. "언제 고쳐졌나" 를 찾고 싶을 때는?</summary>

good/bad 가 "고쳐지기 전/후" 와 방향이 반대라 헷갈린다. `git bisect start --term-old=broken --term-new=fixed` 처럼 용어를 바꾸면 스크립트는 0 = old(broken), 1 = new(fixed) 로 같은 규약을 쓰고 로그도 `git bisect fixed …` 로 읽힌다. 백포트할 수정 커밋을 찾을 때 쓴다. 이 레슨 practice 의 `fast/slow` 가 같은 기능이다 (`refs/bisect/slow` 에 결과).

</details>

## 9. 추가 연습

**P1 (채점). 성능 회귀 bisect.** `practice_test.sh` 를 완성해 `git bisect start --term-old=fast --term-new=slow main v1.0` + `git bisect run` 이 c088 을 찾게 하라. 조건: 판정은 기준(v1.0) 대비 median 비율, 기준 빌드는 작업 트리 밖, 컴파일 안 되는 커밋은 125, 잡음이 커도 오판하지 않을 것.

<details>
<summary>P1 답</summary>

기준 빌드는 `git archive v1.0 | tar -x -C .git/perf-base-<sha>` 로 `.git` 안에 만든다 — 작업 트리 밖이라 `git clean -fdx` 가 못 건드리고, 다음 단계에서 재사용된다. 측정은 `A B A B …` 교차 5 회 → 각 median → 비율. 실측 bisect 경로의 비율: c049 1.10, c074 1.02, c086 0.98, c092 3.47, c089 3.20, c088 3.19, c087 1.06 → `[c088] … is the first slow commit`. 판정 4 fast + 3 slow, skip 0 (c060/c061 을 밟지 않음). 로그는 `git bisect fast …` / `git bisect slow …` 로 남는다. 마지막 출력은 용어와 상관없이 `bisect found first bad commit` 이었다 (git 2.54 실측).

</details>

**P2. 머지된 브랜치와 `--first-parent`.** `demo_first_parent.sh` 는 main(m0–m8) 에 feature 브랜치(f1, f2=버그, f3=빌드 X, f4)를 머지 커밋 M 으로 합친 저장소를 만든다. 두 방식의 결과와 테스트 횟수를 예측해 보라.

<details>
<summary>P2 답</summary>

실측 (`demo_first_parent_out.txt`):

```text
=== git bisect start main m0
tested: m8=bad;m0=good;f3 (does not build)=skip;f4 (build fixed)=bad;m3=good;f2 (introduces the bug)=bad;f1=good;
first bad: f2 (introduces the bug)
=== git bisect start --first-parent main m0
tested: m8=bad;m0=good;m4=good;M (merge feature)=bad;m5=good;
first bad: M (merge feature)
```

기본은 브랜치 안까지 내려가 진짜 원인 f2 를 찾지만 브랜치 내부의 깨진 f3 을 밟는다. --first-parent 는 m4, M, m5 세 번으로 "M(이 PR)이 깨뜨렸다" 에서 멈춘다. 그다음 필요하면 `git bisect start M M^1 -- …` 처럼 그 PR 안에서만 다시 bisect.

</details>

**P3. pathspec 으로 후보 줄이기의 함정.** `git bisect start main v1.0 -- include` 는 `include/` 를 건드린 커밋만 후보로 삼는다. 결과는?

<details>
<summary>P3 답</summary>

후보가 c060(hal.h) 과 c063(ringbuf.h) 둘뿐이다. c060 은 빌드가 안 돼 125, c063 은 bad → 실측:

```text
There are only 'skip'ped commits left to test.
The first bad commit could be any of:
93031ed647a9e4c634d56f88fd72666f23148df0
ae61600b5f8c58141cb592ad898e867bb7b605b1
```

빌드를 고친 c062 는 `src/` 만 건드려서 후보에서 빠졌고, 그래서 c060 을 대신할 good 판정 지점이 없다. pathspec 은 "범인이 이 경로 안에 있다" 는 확신이 있을 때만 — 그리고 skip 이 많으면 오히려 모호해진다.

</details>

**P4. skip 이 범인 바로 앞에 몰리면.** 16 커밋(t09 부터 bug)에서 t07, t08 이 125 를 내면 git 은 무엇을 출력하나? 그리고 실무에서 이 상황을 어떻게 끝내나?

<details>
<summary>P4 답</summary>

실측 (`demo_contract_out.txt`): `There are only 'skip'ped commits left to test.` → 후보 t07, t08, t09 (해시 3 개) → `We cannot bisect more!`, `git bisect run` 종료 코드 2. 끝내는 법: 깨진 커밋에 빌드 수정을 임시로 얹어 테스트한다 (man page 의 hot-fix 예제: 스크립트가 `git merge --no-commit --no-ff hot-fix && make` 로 고친 뒤 테스트하고 `git reset --hard` 로 되돌림 — 또는 `git cherry-pick`), 또는 세 커밋의 diff 를 사람이 읽는다 — 셋 중 둘은 빌드 수정/무관한 변경일 가능성이 높다.

</details>

**P5. 단계 수 계산.** 릴리스 간 커밋이 1,000 개, 빌드 4 분 + 플래시 1 분 + HIL 테스트 3 분, 그중 5 % 가 빌드 안 됨. 대략 몇 시간 걸리나?

<details>
<summary>P5 답</summary>

판정 ⌈log₂ 1000⌉ = 10 번. skip 은 "고른 커밋이 깨졌을 때" 만 생기므로 기대값으로 10 × 0.05 / 0.95 ≈ 0.5 번 더 (빌드 실패 커밋은 빌드 단계 4 분만 쓰고 끝). 약 10 × 8 분 + 0.5 × 4 분 ≈ 82 분. 선형 최악 1,000 × 8 분 ≈ 133 시간. 실무에선 CI 아티팩트로 빌드 4 분을 없애면 판정 10 번 × 4 분 ≈ 40 분. (모두 가정치 계산 — 실측 아님)

</details>

### 한 번에 검증

<!-- code: practice_test.sh -->

```bash
bash check.sh practice
```

실제 출력:

```text
== practice: git version 2.54.0 (Apple Git-156), GNU Make 3.81 ==
PASS P c049 (table CRC) -> 0 fast  [perf: median cur=52219000 ns base=61552000 ns ratio=0.85]
PASS P c090 (bitwise CRC) -> 1 slow  [perf: median cur=164883000 ns base=51298000 ns ratio=3.21]
PASS P c061 (does not compile) -> 125 skip
PASS git bisect run (terms fast/slow) -> first slow = c088 crc16: drop lookup table to save 512 B flash
PASS log uses the terms: 4 fast + 3 slow decisions (<= 8)
PASS baseline build cached in perf-base-f598e3cb95d4 (outside the work tree), tree clean
6/6 passed
```

## 10. 핵심 정리

- bisect = 의심 집합을 반씩 버리는 이분 탐색. 판정 ⌈log₂ N⌉ 번 (99 후보 → 7, 실측 7 + skip 2). 전제는 단조성.
- 규약: **0 good · 1–124/126/127 bad · 125 skip · ≥ 128 중단**. 처음 126/127 은 git 이 good 에서 재확인한다(실측).
- 번역 규칙: 빌드 실패 → 125, 크래시(134) → 1, 장비 고장 → 255. `set -e` 는 이 번역을 망친다.
- 매 단계 클린 빌드(`git clean -fdxq`) — 헤더 의존성 없는 Makefile + 초 단위 mtime 은 "c074 인데 버전 1.0.49" 를 만든다.
- 오라클과 스크립트는 저장소 **밖**에, 시작 시점에 얼려서. 시작 전 good/bad 끝점에서 1/0 을 직접 확인 — 결과가 BAD 끝점이면 테스트를 의심.
- skip 이 범인 옆에 몰리면 후보 목록만 나온다. 알려진 깨진 구간은 `git bisect skip A..B`, PR 단위로 보려면 `--first-parent`, 방향이 헷갈리면 `--term-old/--term-new`, 기록은 `git bisect log` → `replay`.
- 잡음 있는 판정: median + 기준과 교차 측정 + 넉넉한 임계값 + 회색 지대 125. HIL 은 단계당 분 단위라 log₂ 가 시간을 결정한다.

## 참고자료

- git-bisect(1) — `bisect run` 종료 코드, `skip`, `terms`, `--first-parent`, hot-fix 예제: https://git-scm.com/docs/git-bisect
- Christian Couder, "Fighting regressions with git bisect" (git 문서 git-bisect-lk2009): https://git-scm.com/docs/git-bisect-lk2009
- Pro Git 7.10 "Debugging with Git — Binary Search": https://git-scm.com/book/en/v2/Git-Tools-Debugging-with-Git
- git 환경 변수 (`GIT_CONFIG_GLOBAL`, `GIT_CONFIG_NOSYSTEM`, `GIT_AUTHOR_DATE`): https://git-scm.com/docs/git#_environment_variables
- Bash 매뉴얼 — Exit Status (128+N): https://www.gnu.org/software/bash/manual/bash.html#Exit-Status
- 관련 레슨: TOOL-B08 (Make 와 헤더 의존성 — 함정 3 의 원인), TOOL-B06 (.data/.bss 크기), TOOL-M13 (size diet), TOOL-M05 (CMake 크로스 빌드 — HIL 스크립트의 빌드 단계), TOOL-A06 (재현 가능한 빌드), BM-A09 / ARM-M14 (크래시 덤프로 판정 세분화)
