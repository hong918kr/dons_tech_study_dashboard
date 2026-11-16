---
id: TOOL-M12
time: 80분
updated: 2026-11-15
---
# TOOL-M12 정적 분석: clang-tidy·scan-build·cppcheck

## 학습 목표

- 버그 10개를 심은 펌웨어 모듈에 컴파일러 경고 · Clang 정적 분석기(clang static analyzer) · clang-tidy · cppcheck · (VM) scan-build · gcc `-fanalyzer` 를 돌려 **버그 × 도구 매트릭스**를 실측으로 만들 수 있다.
- 각 도구가 **왜** 그 버그를 놓치는지 엔진 방식(AST 매처 / 경로 민감 심볼릭 실행 / 값 흐름 패턴)과 기본 설정(`suppress-null-return-paths`, `-analyzer-max-loop 4`)으로 설명할 수 있다.
- `.clang-tidy` 를 체크 선별 + `WarningsAsErrors` + `HeaderFilterRegex` + `ExtraArgs` 로 짜서, 버그 파일은 **CI 를 실패**시키고 고친 파일은 **0건**인 기준선(baseline)을 만들 수 있다.
- 오탐(false positive)·의도된 코드의 억제(suppression) 방침 — 한 줄, 한 id, 이유 주석 — 을 세우고, 전역 억제가 진짜 버그를 숨기는 것을 실측으로 보일 수 있다.
- 레거시 코드에 "새 줄만 검사" 하는 래칫(ratchet)을 `clang-tidy-diff.py` 로 걸 수 있다.

## 1. 문제

"정적 분석 돌리세요" 는 쉽지만, 펌웨어 팀에서 실제로 어려운 건 **무엇을 켜고, 무엇을 억제하고, 언제 빌드를 깨뜨릴지** 정하는 일이다. 면접에서도 "clang-tidy 랑 cppcheck 차이가 뭔가요? 둘 다 필요해요? 오탐은 어떻게 관리해요?" 가 꼬리질문으로 나온다. 체크를 전부 켜면 고친 코드에서도 100건이 넘게 나와 아무도 안 보고, 너무 좁히면 진짜 버그가 CI 를 통과한다.

대상은 센서 허브 펌웨어의 UART 링크 모듈 `fw_bugs.c`(294줄, 호스트 빌드)다. 레지스터 블록, RX 링 버퍼(뮤텍스), 프레임 파서, 명령 테이블, 링크 상태 머신이 들어 있고 **버그 10개**가 `// BUGnn` 태그 줄에 숨어 있다. 같은 모듈을 고친 `fw_clean.c` 와, 고친 코드가 정말 동작하는지 확인하는 `selftest.c`(16개 실행 검사)가 함께 있다. 우리 헤더 `include/fw_util.h`, 고칠 수 없는 벤더 헤더 `third_party/vendor_hal.h` 도 있다.

| 태그 | 줄 | 버그 | 펌웨어에서 일어나는 일 |
|---|---|---|---|
| BUG01 | 217 | 에러 경로의 NULL 역참조: `cmd == NULL` 인데 `cmd->name` 로그 | 모르는 명령 1바이트로 HardFault |
| BUG02 | 168 | CRC 불일치로 조기 반환할 때 `pkt` 누수(leak) | 잡음 많은 링크에서 힙이 서서히 고갈 → 며칠 뒤 리셋 |
| BUG03 | 207 | `id > CMD_COUNT` (≥ 이어야 함) → `cmd_hits[3]` 범위 밖(OOB) 쓰기 | 옆 전역 변수 오염 |
| BUG04 | 53/55 | `default:` 경로에서 `baud` 미초기화 → BRR 에 쓰레기 | 잘못된 설정 명령 하나로 통신 두절 |
| BUG05 | 252 | `int16_t rssi < uint32_t 90u` 부호 비교 | −95 dBm 이 4294967201 이 되어 "링크 저하" 판정이 영원히 안 됨 |
| BUG06 | 272/273 | `err = flash_write(); err = flash_verify();` 죽은 저장(dead store) | 쓰기 실패가 검증 결과로 덮여 사라짐 |
| BUG07 | 75/81 | 링 버퍼가 가득 찼을 때 뮤텍스를 쥔 채 반환 | 다음 `rb_pop` 에서 RX 태스크 교착(deadlock) |
| BUG08 | 150 | `size_t body = n_rec * REC_SIZE` — 곱셈은 32비트 | `n_rec = 0x2AAAAAAB` 면 2 로 랩 → 길이 검사 통과 |
| BUG09 | 224 | `packet_free(pkt)` 뒤 `pkt->type` 로그 — 해제 후 사용(UAF) | 재할당된 블록을 읽음, 가끔 엉뚱한 값 |
| BUG10 | 268 | `snprintf("%u", uint64_t)` 형식 불일치 | 32비트 타깃에선 뒤 인자까지 밀려 상태 값이 틀림 |

미리 보는 결론 — 실측 매트릭스(`matrix.txt`, `vm.txt`). 한 도구로 10개를 다 잡는 건 없고, **설정한 clang-tidy + cppcheck 의 합집합**이 10/10 이다.

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">버그 × 도구 (fw_bugs.c, 태그 줄에 진단이 있으면 ●, 실측)</text>
  <g fill="currentColor" font-size="12">
    <text x="256" y="44">01</text><text x="300" y="44">02</text><text x="344" y="44">03</text><text x="388" y="44">04</text><text x="432" y="44">05</text>
    <text x="476" y="44">06</text><text x="520" y="44">07</text><text x="564" y="44">08</text><text x="608" y="44">09</text><text x="652" y="44">10</text>
    <text x="700" y="44">합계</text>
  </g>
  <g font-size="12" fill="currentColor">
    <text x="10" y="69">cc -Wall -Wextra -Wthread-safety</text>
    <text x="10" y="99">clang --analyze / scan-build 기본</text>
    <text x="10" y="129">gcc 15 -fanalyzer (VM)</text>
    <text x="10" y="159">cppcheck solution.cppcheck</text>
    <text x="10" y="189">clang-tidy solution.clang-tidy</text>
    <text x="10" y="219" font-weight="bold">solution = tidy ∪ cppcheck</text>
  </g>
  <g stroke="currentColor">
    <rect x="248" y="55" width="36" height="20" class="f-mut"/><rect x="292" y="55" width="36" height="20" class="f-mut"/><rect x="336" y="55" width="36" height="20" class="f-mut"/><rect x="380" y="55" width="36" height="20" class="f-ok"/><rect x="424" y="55" width="36" height="20" class="f-ok"/><rect x="468" y="55" width="36" height="20" class="f-mut"/><rect x="512" y="55" width="36" height="20" class="f-ok"/><rect x="556" y="55" width="36" height="20" class="f-mut"/><rect x="600" y="55" width="36" height="20" class="f-mut"/><rect x="644" y="55" width="36" height="20" class="f-ok"/>
    <rect x="248" y="85" width="36" height="20" class="f-mut"/><rect x="292" y="85" width="36" height="20" class="f-mut"/><rect x="336" y="85" width="36" height="20" class="f-mut"/><rect x="380" y="85" width="36" height="20" class="f-ok"/><rect x="424" y="85" width="36" height="20" class="f-mut"/><rect x="468" y="85" width="36" height="20" class="f-ok"/><rect x="512" y="85" width="36" height="20" class="f-mut"/><rect x="556" y="85" width="36" height="20" class="f-mut"/><rect x="600" y="85" width="36" height="20" class="f-ok"/><rect x="644" y="85" width="36" height="20" class="f-mut"/>
    <rect x="248" y="115" width="36" height="20" class="f-ok"/><rect x="292" y="115" width="36" height="20" class="f-mut"/><rect x="336" y="115" width="36" height="20" class="f-mut"/><rect x="380" y="115" width="36" height="20" class="f-ok"/><rect x="424" y="115" width="36" height="20" class="f-mut"/><rect x="468" y="115" width="36" height="20" class="f-mut"/><rect x="512" y="115" width="36" height="20" class="f-mut"/><rect x="556" y="115" width="36" height="20" class="f-mut"/><rect x="600" y="115" width="36" height="20" class="f-ok"/><rect x="644" y="115" width="36" height="20" class="f-ok"/>
    <rect x="248" y="145" width="36" height="20" class="f-ok"/><rect x="292" y="145" width="36" height="20" class="f-ok"/><rect x="336" y="145" width="36" height="20" class="f-ok"/><rect x="380" y="145" width="36" height="20" class="f-mut"/><rect x="424" y="145" width="36" height="20" class="f-mut"/><rect x="468" y="145" width="36" height="20" class="f-ok"/><rect x="512" y="145" width="36" height="20" class="f-mut"/><rect x="556" y="145" width="36" height="20" class="f-mut"/><rect x="600" y="145" width="36" height="20" class="f-mut"/><rect x="644" y="145" width="36" height="20" class="f-ok"/>
    <rect x="248" y="175" width="36" height="20" class="f-mut"/><rect x="292" y="175" width="36" height="20" class="f-mut"/><rect x="336" y="175" width="36" height="20" class="f-mut"/><rect x="380" y="175" width="36" height="20" class="f-ok"/><rect x="424" y="175" width="36" height="20" class="f-ok"/><rect x="468" y="175" width="36" height="20" class="f-ok"/><rect x="512" y="175" width="36" height="20" class="f-ok"/><rect x="556" y="175" width="36" height="20" class="f-ok"/><rect x="600" y="175" width="36" height="20" class="f-ok"/><rect x="644" y="175" width="36" height="20" class="f-ok"/>
    <rect x="248" y="205" width="36" height="20" class="f-acc"/><rect x="292" y="205" width="36" height="20" class="f-acc"/><rect x="336" y="205" width="36" height="20" class="f-acc"/><rect x="380" y="205" width="36" height="20" class="f-acc"/><rect x="424" y="205" width="36" height="20" class="f-acc"/><rect x="468" y="205" width="36" height="20" class="f-acc"/><rect x="512" y="205" width="36" height="20" class="f-acc"/><rect x="556" y="205" width="36" height="20" class="f-acc"/><rect x="600" y="205" width="36" height="20" class="f-acc"/><rect x="644" y="205" width="36" height="20" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="13">
    <text x="702" y="70">4</text><text x="702" y="100">3</text><text x="702" y="130">4</text><text x="702" y="160">5</text><text x="702" y="190">7</text><text x="698" y="220" font-weight="bold">10</text>
  </g>
  <rect x="10" y="240" width="14" height="12" class="f-ok" stroke="currentColor"/><text x="30" y="251" fill="currentColor" font-size="12">잡았음</text>
  <rect x="80" y="240" width="14" height="12" class="f-mut" stroke="currentColor"/><text x="100" y="251" fill="currentColor" font-size="12">놓침</text>
  <text x="160" y="251" fill="currentColor" font-size="12">fw_clean.c: tidy 0, cppcheck 0, clang --analyze 1 (NOLINT 무시, Part C)</text>
</svg>
```

### Part A — 도구별로 돌려 매트릭스 만들기

- 컴파일러 경고, `clang --analyze`, clang-tidy(`'*'`, `clang-analyzer-*`, 설정 파일들), cppcheck(옵션 없음 / `--enable=all` / 설정) 를 Mac 에서, scan-build-21 · cppcheck 2.19 · clang-tidy-21 · gcc 15 `-fanalyzer` 를 VM 에서 돌린다(`demo_matrix.sh`, `demo_vm.sh`).
- 각 버그를 누가 잡고 누가 놓치는지 표로 만들고, **놓친 이유**를 엔진의 동작으로 설명한다.

### Part B — 함정: starter 설정의 5가지 실수

- `starter.clang-tidy` / `starter.cppcheck` 는 "전부 켜고, 거슬리는 건 끈" 첫 시도다. `bash check.sh starter` 의 FAIL 줄 하나하나가 실무에서 실제로 무는 실수다: 잡음 100건, CI 가 안 깨짐, 우리 헤더를 조용히 건너뜀, 전역 억제가 BUG06 을 숨김, 스레드 안전성 분석 누락.

### Part C — 펌웨어 CI 기준선

- `solution.clang-tidy` + `solution.cppcheck` 로 버그 10/10, 고친 코드 0건, 실패 시 exit 1.
- 억제 방침(한 줄·한 id·이유), 타깃처럼 분석하기(`-D_FORTIFY_SOURCE=0`), 툴 버전 고정, scan-build 를 빌드에 감싸기, 레거시 코드 래칫, MISRA 언급.

## 2. Problem (English)

> **Set up the static-analysis gate for a firmware repo.**
>
> `fw_bugs.c` (do not edit) contains 10 planted bugs, each on a line tagged `// BUGnn`: a NULL dereference on an error path, a leak on an early return, an off-by-one array index, a variable left uninitialized on one switch path, a signed/unsigned comparison, a dead store that loses an error code, a mutex left locked on an early return, a 32-bit multiplication overflow in a size calculation, a use-after-free, and a printf format mismatch. `fw_clean.c` is the fixed module and must stay at **zero** findings.
>
> Edit only `<variant>.clang-tidy` (clang-tidy 23 config) and `<variant>.cppcheck` (one cppcheck option per line). The checker builds a `compile_commands.json` with the project flags `-std=c11 -Wall -Wextra -D_FORTIFY_SOURCE=0 -Iinclude -Ithird_party` and requires:
>
> 1. every BUG line gets a finding from a bug-finding check of clang-tidy or cppcheck;
> 2. both tools exit non-zero on `fw_bugs.c` and zero (with no findings) on `fw_clean.c`;
> 3. findings in `include/` are reported, findings in `third_party/` are not, whatever way `-I` is spelled;
> 4. suppressions are scoped: no global `--suppress=<id>`; inline ones name exactly one check on one line.
>
> Follow-ups: Why can't one tool find all ten? Why does the Clang analyzer miss a NULL dereference that cppcheck finds? How do you introduce this on a 200k-line legacy codebase without a flag day? What changes when you upgrade clang-tidy?

## 3. 시작 코드

시작 설정 두 개(팀의 첫 시도):

<!-- code: starter.clang-tidy -->

<!-- code: starter.cppcheck -->

검사 대상 — 고치지 않는다:

<!-- code: fw_bugs.c -->

검사기 — `<variant>.clang-tidy` / `<variant>.cppcheck` 를 읽어 Mac 의 clang-tidy 23.1.4 (`$HOME/.venvs/clangtidy`) 와 cppcheck 2.22 로 돌린다:

<!-- code: check.sh -->

```bash
bash check.sh starter      # 일부 FAIL, exit 1  (약 3초)
bash check.sh solution     # 전부 PASS, exit 0 (약 2초)
bash check.sh practice     # 9. 추가 연습, exit 0 (약 16초: 분석기 루프 한도 16 + 래칫)
bash demo_matrix.sh > matrix.txt   # Mac 매트릭스 (약 25초)
bash demo_vm.sh     > vm.txt       # VM: scan-build-21, cppcheck 2.19, clang-tidy-21, gcc -fanalyzer
bash demo_extra.sh  > extra.txt    # 5. 풀이의 곁가지 실험 (E1–E10)
bash demo_runtime.sh > runtime.txt # 같은 버그를 테스트(+ASan)로 보면?
```

`bash check.sh starter` 실제 출력:

```text
cppcheck options (starter.cppcheck): --enable=all --suppress=redundantInitialization 
PASS rules: starter.clang-tidy passes clang-tidy --verify-config
PASS rules: fw_bugs.c fw_clean.c include/ third_party/ untouched
FAIL rules: unscoped suppression: --suppress=redundantInitialization
PASS fw_bugs.c BUG01 found by cppcheck:217[nullPointerRedundantCheck]
PASS fw_bugs.c BUG02 found by cppcheck:168[memleak]
PASS fw_bugs.c BUG03 found by cppcheck:207[arrayIndexOutOfBoundsCond]
PASS fw_bugs.c BUG04 found by tidy:53[clang-diagnostic-sometimes-uninitialized] tidy:55[clang-analyzer-core.CallAndMessage]
PASS fw_bugs.c BUG05 found by tidy:252[clang-diagnostic-sign-compare]
FAIL fw_bugs.c BUG06 line(s) 272 273 : no finding from clang-tidy or cppcheck
FAIL fw_bugs.c BUG07 line(s) 75 81 : no finding from clang-tidy or cppcheck
PASS fw_bugs.c BUG08 found by tidy:150[bugprone-implicit-widening-of-multiplication-result]
PASS fw_bugs.c BUG09 found by tidy:224[clang-analyzer-unix.Malloc]
PASS fw_bugs.c BUG10 found by tidy:268[clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling] cppcheck:268[invalidPrintfArgType_uint]
FAIL CI gate: clang-tidy exits 0 on fw_bugs.c despite 110 findings (job stays green)
FAIL CI gate: cppcheck exits 0 on fw_bugs.c despite 30 findings (job stays green)
FAIL header filter: finding in our include/canary.h NOT reported (own headers silently skipped)
PASS header filter: third_party/ header not reported
FAIL fw_clean.c: clang-tidy 104 findings (exit 0), top: [cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers] x17 [readability-identifier-length] x15 [cppcoreguidelines-macro-to-enum,modernize-macro-to-enum] x15
FAIL fw_clean.c: cppcheck 26 findings (exit 0), top: [missingIncludeSystem] x10 [unusedFunction] x8 [staticFunction] x6
PASS fw_clean.c runs: selftest(fw_clean.c): 16/16 OK
12/20 passed
exit=1
```

검사기 설계:

- **compile DB**: `$W/compile_commands.json` 에 프로젝트 플래그를 적고 `clang-tidy -p` 로 읽는다 — CMake 가 내보내는 것과 같은 방식(TOOL-M04). 경고 플래그 `-Wall -Wextra` 가 들어 있어서 clang-tidy 의 `clang-diagnostic-*` 가 컴파일러 경고를 함께 보고한다.
- **BUG 판정**: `fw_bugs.c:줄:` 에 `error/warning/style` 진단이 있고 그 id 가 버그 계열(`clang-analyzer-`, `clang-diagnostic-`, `bugprone-`, `cert-`, `concurrency-`, cppcheck id)일 때만. `readability-identifier-length` 처럼 우연히 같은 줄에 떨어진 스타일 경고는 세지 않는다. 줄 단위 매칭이라 거칠다(BUG10 에서 starter 가 엉뚱한 id 를 먼저 보여 주는 것처럼).
- **canary**: `$W/canary/` 에 `include/canary.h` 와 `third_party/vendor_canary.h` 를 만들고 둘 다 같은 결함(`#define X(x) x * 2`)을 넣어, 헤더 필터가 우리 것만 통과시키는지 본다 — "린트 설정을 테스트하는" 실무 기법. 입력 4개 파일은 sha256 으로 "안 고쳤음" 을 확인, 산출물은 `/tmp/TOOL-M12_work/<variant>/` 에만.

## 4. 힌트

<details>
<summary>힌트 1 — 잡음부터 줄여라: 어떤 계열이 버그를 잡나?</summary>

`'*'` 는 602개 체크를 켠다. `matrix.txt` 아래쪽의 "fw_clean.c findings by id" 를 보면 고친 코드에서 나온 것은 거의 전부 `readability-*`, `modernize-*`, `cppcoreguidelines-*`, `misc-*`, `altera-*` 다. 버그를 잡은 id 는 `clang-analyzer-*`, `clang-diagnostic-*`, `bugprone-*` 뿐. `-*` 로 다 끄고 필요한 계열만 켜라.

</details>

<details>
<summary>힌트 2 — CI 를 깨뜨리는 건 출력이 아니라 종료 코드</summary>

clang-tidy 는 경고가 110개여도 exit 0 이다. `WarningsAsErrors` 에 해당 체크를 넣어야 exit 1. cppcheck 는 `--error-exitcode=1` 이 없으면 항상 0.

</details>

<details>
<summary>힌트 3 — 같은 정규식이 -I 를 어떻게 썼느냐에 따라 다르게 동작한다</summary>

헤더 경로는 `-Iinclude` 면 `include/canary.h`, `-I/abs/include` 면 `/abs/.../include/canary.h` 로 보고된다. `'.*/include/.*'` 는 앞쪽 경우에 `/` 가 없어 매치되지 않는다.

</details>

<details>
<summary>힌트 4 — BUG06 과 BUG07</summary>

BUG06 은 `clang-analyzer-deadcode.DeadStores` 와 cppcheck `redundantInitialization` 가 잡는데, starter 는 HAL 관용구 경고를 없애려고 둘 다 **전역으로** 껐다. 그 관용구에는 한 줄짜리 억제가 이미 `fw_clean.c` 에 있다(`--inline-suppr` 필요). BUG07 은 어떤 체크도 아니라 Clang 의 스레드 안전성 분석(`-Wthread-safety`)이 잡는다 — `include/fw_util.h` 의 뮤텍스 래퍼에 이미 `acquire_capability`/`release_capability` 주석(annotation)이 달려 있다. 컴파일 인자를 설정 파일에서 더하는 키가 있다.

</details>

## 5. 풀이

### Part A — 매트릭스 (실측)

Mac(`matrix.txt`, clang-tidy 23.1.4, cppcheck 2.22.0, Apple clang 21):

| tool / config | 01 | 02 | 03 | 04 | 05 | 06 | 07 | 08 | 09 | 10 | 잡은 수 | fw_clean 진단 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `cc -Wall -Wextra` | · | · | · | ● | ● | · | · | · | · | ● | 3 | 0 |
| `cc ... -Wthread-safety` | · | · | · | ● | ● | · | ● | · | · | ● | 4 | 0 |
| `clang --analyze` (기본) | · | · | · | ● | · | ● | · | · | ● | · | 3 | 1 |
| `clang --analyze` (튜닝: max-loop 16, null-return 보고) | ● | ● | · | ● | · | ● | · | · | ● | · | 5 | 1 |
| clang-tidy `'*'` | · | · | · | ● | ● | ● | · | ● | ● | ● | 6 | **118** |
| clang-tidy starter | · | · | · | ● | ● | · | · | ● | ● | ● | 5 | 104 |
| clang-tidy **solution** | · | · | · | ● | ● | ● | ● | ● | ● | ● | 7 | 0 |
| clang-tidy practice | ● | ● | · | ● | ● | ● | ● | ● | ● | ● | 9 | 0 |
| cppcheck (옵션 없음) | · | ● | · | · | · | · | · | · | · | · | 1 | 0 |
| cppcheck `--enable=all` | ● | ● | ● | · | · | ● | · | · | · | ● | 5 | 15 |
| cppcheck **solution** | ● | ● | ● | · | · | ● | · | · | · | ● | 5 | 0 |

VM(`vm.txt`, aarch64 Ubuntu: gcc 15.2, LLVM 21.1.8, cppcheck 2.19.0):

| tool / config | 01 | 02 | 03 | 04 | 05 | 06 | 07 | 08 | 09 | 10 | 잡은 수 | fw_clean 진단 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| gcc-15 `-Wall -Wextra` | · | · | · | · | ● | · | · | · | · | ● | 2 | 0 |
| gcc-15 `-fanalyzer` | ● | · | · | ● | · | · | · | · | ● | ● | 4 | 0 |
| clang-tidy-21 solution | · | · | · | ● | ● | ● | ● | ● | ● | ● | 7 | 0 |
| cppcheck-2.19 solution | ● | ● | ● | · | · | ● | · | · | · | ● | 5 | 0 |
| scan-build-21 (기본) | · | · | · | ● | · | ● | · | · | ● | ● | 4 | 1 |

(gcc `-fanalyzer` 와 scan-build 의 BUG10 은 분석기가 아니라 기본 켜진 컴파일러 `-Wformat`.)

버그별로 읽기:

| 버그 | 잡은 것 (실제 id) | 놓친 것과 이유 |
|---|---|---|
| BUG01 NULL 역참조 | cppcheck `nullPointerRedundantCheck`, gcc `-Wanalyzer-null-dereference` | Clang 분석기: NULL 이 **인라인된 callee 의 `return NULL`** 에서 왔기 때문에 기본 휴리스틱 `suppress-null-return-paths=true` 가 보고를 버린다(E4). |
| BUG02 누수 | cppcheck `memleak`, Clang 분석기는 루프 한도를 올렸을 때만 | `crc8` 의 안쪽 `for (k < 8)` 은 8번 돈다. 분석기는 같은 루프 블록을 **4번까지만** 펼치고(기본 `-analyzer-max-loop 4`) 그 경로를 버린다 → CRC 검사 뒤의 모든 경로가 사라짐. 16 으로 올리면 `Potential leak of memory pointed to by 'pkt'` (E4). gcc `-fanalyzer` 도 놓쳤다. |
| BUG03 OOB | cppcheck `arrayIndexOutOfBoundsCond` 만 | cppcheck 는 "조건이 `id > 3u` 인데 `[id]` 를 쓴다 → 조건이 쓸모없거나 index 3 이 범위 밖" 이라는 **패턴**으로 본다. Clang 분석기는 `id ∈ [0,3]` 경로에서 "범위 밖일 **수도**" 는 보고하지 않는다(확실할 때만, 오탐 방지) — 같은 결과가 gcc 에도. |
| BUG04 미초기화 | clang `-Wsometimes-uninitialized`(53줄), 분석기 `core.CallAndMessage`(55줄), gcc `-fanalyzer` | **gcc `-Wall -Wextra` 는 놓쳤다** — GCC 의 미초기화 경고는 최적화(`-O`)가 있어야 보인다(TOOL-B02 Part B). cppcheck 도 놓침. |
| BUG05 부호 비교 | 컴파일러 `-Wsign-compare` (`-Wextra`), clang-tidy 는 `clang-diagnostic-sign-compare` 로 전달만 | 경로 분석기·cppcheck 의 관심사가 아니다. **compile DB 에 `-Wextra` 가 있어야** clang-tidy 도 보인다. |
| BUG06 죽은 저장 | 분석기 `deadcode.DeadStores`(272줄 초기화), cppcheck `redundantInitialization`(273줄 덮어쓰기) | 같은 버그를 **다른 줄**에 보고한다 → 태그를 두 줄에. |
| BUG07 락 누락 | `-Wthread-safety` (`clang-diagnostic-thread-safety-analysis`) 만 | 함수 끝(81줄 `}`)에서 "mutex 'rb->lock' is not held on every path through here". 주석이 달린 래퍼가 없으면 아무도 못 잡는다. 분석기 `alpha.unix.PthreadLock` 은 이중 unlock 은 잡았지만 "락을 쥔 채 반환" 은 잡지 않았다(작은 별도 파일로 실험, 이 레슨 파일엔 미포함 — 확인 권장). |
| BUG08 곱셈 오버플로 | `bugprone-implicit-widening-of-multiplication-result` 만 | "32비트로 곱한 뒤 64비트 `size_t` 로 넓힌다" 는 AST **패턴**. 주의: 이건 **호스트(LP64)에서만** 보인다. Cortex-M 에선 `size_t` 가 32비트라 넓힘이 없어 이 체크가 침묵하는데, 랩은 그대로다. |
| BUG09 UAF | 분석기 `unix.Malloc`, gcc `-Wanalyzer-use-after-free` | cppcheck 는 `free` 가 `packet_free()` **안**에 있어 함수 경계를 넘는 추적이 약해 놓쳤다. |
| BUG10 형식 | 컴파일러 `-Wformat`(기본 켜짐), cppcheck `invalidPrintfArgType_uint` | cppcheck 는 `uint64_t` 를 `unsigned long` 으로 본다(자기 플랫폼 모델) — Mac 의 실제 타입은 `unsigned long long`. 메시지가 컴파일러와 다르다. |

세 가지 엔진이 다르게 본다는 게 핵심이다:

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <rect x="10" y="10" width="235" height="280" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="32" fill="currentColor" font-weight="bold">① AST 매처 (패턴)</text>
  <text x="20" y="52" fill="currentColor" font-size="12">clang-tidy bugprone-*, 컴파일러 경고</text>
  <rect x="20" y="66" width="215" height="54" class="f-acc" stroke="currentColor"/>
  <text x="28" y="86" fill="currentColor" font-size="12" font-family="monospace">size_t = (u32 * u32)</text>
  <text x="28" y="106" fill="currentColor" font-size="12">→ 이 모양이면 보고 (BUG08)</text>
  <text x="20" y="146" fill="currentColor" font-size="12">+ 빠르다, 결정적이다</text>
  <text x="20" y="166" fill="currentColor" font-size="12">− 실행 경로를 모른다</text>
  <text x="20" y="186" fill="currentColor" font-size="12">− 타입 크기가 바뀌면(ILP32)</text>
  <text x="20" y="204" fill="currentColor" font-size="12">   같은 버그에 침묵</text>
  <rect x="262" y="10" width="235" height="280" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="272" y="32" fill="currentColor" font-weight="bold">② 경로 민감 심볼릭 실행</text>
  <text x="272" y="52" fill="currentColor" font-size="12">Clang 분석기 · gcc -fanalyzer</text>
  <rect x="330" y="64" width="100" height="22" class="f-acc" stroke="currentColor"/>
  <text x="340" y="80" fill="currentColor" font-size="12">parse_frame</text>
  <line x1="350" y1="86" x2="300" y2="112" stroke="currentColor"/>
  <line x1="410" y1="86" x2="450" y2="112" stroke="currentColor"/>
  <rect x="270" y="112" width="70" height="22" class="f-ok" stroke="currentColor"/>
  <text x="276" y="128" fill="currentColor" font-size="12">len 짧음</text>
  <rect x="405" y="112" width="85" height="22" class="f-acc" stroke="currentColor"/>
  <text x="410" y="128" fill="currentColor" font-size="12">crc8 루프</text>
  <line x1="447" y1="134" x2="447" y2="160" stroke="currentColor"/>
  <rect x="380" y="160" width="110" height="34" class="f-warn" stroke="currentColor"/>
  <text x="386" y="175" fill="currentColor" font-size="12">5번째 반복에서</text>
  <text x="386" y="189" fill="currentColor" font-size="12">경로 버림(max 4)</text>
  <text x="272" y="216" fill="currentColor" font-size="12">+ 경로마다 값을 추적 → UAF, 누수</text>
  <text x="272" y="236" fill="currentColor" font-size="12">− 예산(루프 4회, 인라인 깊이)</text>
  <text x="272" y="256" fill="currentColor" font-size="12">− 오탐 억제 휴리스틱이 진짜를</text>
  <text x="272" y="274" fill="currentColor" font-size="12">   버리기도 함 (BUG01)</text>
  <rect x="514" y="10" width="236" height="280" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="524" y="32" fill="currentColor" font-weight="bold">③ 값 흐름 + 패턴</text>
  <text x="524" y="52" fill="currentColor" font-size="12">cppcheck (컴파일 없이 토큰 분석)</text>
  <rect x="524" y="66" width="216" height="54" class="f-acc" stroke="currentColor"/>
  <text x="532" y="86" fill="currentColor" font-size="12" font-family="monospace">if (id &gt; 3u) ...; a[id]</text>
  <text x="532" y="106" fill="currentColor" font-size="12">→ "조건이 중복 or OOB" (BUG03)</text>
  <text x="524" y="146" fill="currentColor" font-size="12">+ 컴파일러·헤더 없이도 돈다</text>
  <text x="524" y="166" fill="currentColor" font-size="12">+ "조건 vs 사용" 모순을 잘 잡음</text>
  <text x="524" y="186" fill="currentColor" font-size="12">− 함수 경계 넘는 추적이 약함</text>
  <text x="524" y="204" fill="currentColor" font-size="12">   (free 가 packet_free 안 → BUG09)</text>
  <text x="524" y="224" fill="currentColor" font-size="12">− 자기 플랫폼 모델로 타입 판단</text>
</svg>
```

곁가지 실험 E4(`extra.txt`) — 분석기 노브(knob)를 하나씩:

```text
  knobs: [default]
    fw_bugs.c:55:18: warning: 2nd function call argument is an uninitialized value [core.CallAndMessage]
    fw_bugs.c:105:18: warning: Value stored to 'tmp' during its initialization is never read [deadcode.DeadStores]
    fw_bugs.c:224:55: warning: Use of memory after it is freed [unix.Malloc]
    fw_bugs.c:272:9: warning: Value stored to 'err' during its initialization is never read [deadcode.DeadStores]
  knobs: [-Xanalyzer -analyzer-max-loop -Xanalyzer 16]
    ... (위 4개) + fw_bugs.c:168:16: warning: Potential leak of memory pointed to by 'pkt' [unix.Malloc]
  knobs: [-Xanalyzer -analyzer-config -Xanalyzer suppress-null-return-paths=false]
    ... (위 4개) + fw_bugs.c:217:52: warning: Access to field 'name' results in a dereference of a null pointer (loaded from variable 'cmd') [core.NullDereference]
```

`suppress-null-return-paths` 는 `clang -cc1 -analyzer-config-help` 에 "Whether or not paths that go through null returns should be suppressed" 로 나온다. 이유: `p = lookup(); if (!p) ...` 처럼 "방어적으로 NULL 을 반환하는 함수" 는 흔하고, 호출자가 다른 불변식으로 NULL 이 안 온다는 걸 알 때가 많아서 기본은 버린다. 이 기본값이 BUG01 같은 **진짜** 에러 경로 버그도 같이 버린다.

105줄 `tmp` — 버그 태그가 없는 줄에도 분석기와 cppcheck 가 같은 id 로 보고한다. 이게 Part B·C 의 주인공이다.

### Part B — 함정 1: `'*'` 는 기준선이 아니다

`clang-tidy --checks='*'` 는 버그를 6개 잡지만 **고친 파일에서 118건**(Mac, `matrix.txt`). 상위 id:

```text
     17 [readability-uppercase-literal-suffix]
     17 [readability-identifier-length]
     17 [cppcoreguidelines-macro-to-enum,modernize-macro-to-enum]
     17 [cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers]
     13 [misc-use-internal-linkage]
      8 [bugprone-signed-bitwise]
      6 [cppcoreguidelines-avoid-non-const-global-variables]
      5 [altera-struct-pack-align]
```

전부 "취향" 이거나 C++/FPGA(altera)/LLVM libc 프로젝트용 규칙이다. 118건짜리 리포트는 아무도 안 읽고, 그 속에 진짜 버그 7건이 묻힌다. 계열을 실측으로 고른다: `-*` 로 끄고, fw_bugs 에서 버그를 잡거나 fw_clean 에서 0건인 계열만 켠다(`clang-analyzer-*` 는 HAL 관용구 1건 → 한 줄 억제(함정 4)와 Annex K 체커 2건 → 끔(함정 6), `bugprone-*` 는 signed-bitwise 8건 → 옵션 조정, `cert-*`/`concurrency-*`/`performance-*`/`portability-*` 는 0건).

`bugprone-signed-bitwise` 8건은 `(1u << 13)` 의 `13` 이 signed int 리터럴이라서다. 끄지 않고 `IgnorePositiveIntegerLiterals: true` 로 조정했다 — 펌웨어 비트 연산에서 `1 << 31` 같은 진짜 부호 문제는 계속 잡도록. 그래도 남은 건 `crc8` 의 `(c << 1)`(uint8_t 가 int 로 승격) 하나였고, 코드를 `(uint32_t)c << 1` 로 바꿨다(MISRA 식 "본질 타입" 습관과 같은 방향).

### Part B — 함정 2: 출력이 아니라 종료 코드

starter: `CI gate: clang-tidy exits 0 on fw_bugs.c despite 110 findings`. clang-tidy 는 기본적으로 경고만 찍고 0 으로 끝난다. `WarningsAsErrors: '*'` 를 넣으면 진단이 `error: ... [check,-warnings-as-errors]` 로 바뀌고 exit 1. cppcheck 도 `--error-exitcode=1` 없이는 30건이어도 0. **CI 에서 실패하지 않는 분석은 없는 것과 같다.**

### Part B — 함정 3: HeaderFilterRegex 와 -I 철자, 그리고 버전마다 다른 기본값

E3(`extra.txt`) — canary 헤더 두 개, 정규식 × `-I` 철자:

```text
  '<unset: 23 default .*>'   -I rel  -> reported: include/canary.h third_party/vendor_canary.h 
  '<unset: 23 default .*>'   -I abs  -> reported: include/canary.h third_party/vendor_canary.h 
  ''                         -I rel  -> reported: (nothing)
  ''                         -I abs  -> reported: (nothing)
  '.*/include/.*'            -I rel  -> reported: (nothing)
  '.*/include/.*'            -I abs  -> reported: include/canary.h 
  '(^|/)include/'            -I rel  -> reported: include/canary.h 
  '(^|/)include/'            -I abs  -> reported: include/canary.h 
```

- starter 의 `'.*/include/.*'` 는 CMake 가 만드는 절대 경로(`-I/home/ci/.../include`)에선 맞지만, 상대 경로 `-Iinclude` 에선 **우리 헤더를 조용히 건너뛴다**. 에러도 경고도 없다. 그래서 check.sh 가 canary 로 "보고돼야 할 것이 보고되는지" 를 시험한다.
- 기본값이 버전마다 다르다(실측): clang-tidy **23.1.4 는 `HeaderFilterRegex: '.*'`**(E1, 벤더 헤더까지 전부 보고), **21.1.8(VM)은 `''`**(헤더는 전혀 보고 안 함). 설정에 명시하지 않으면 툴 업그레이드가 곧 CI 결과 변화다.
- 대안: `ExcludeHeaderFilterRegex: 'third_party/'` 로 빼는 방식. 벤더 헤더를 `-isystem` 으로 넣으면(TOOL-B02) `SystemHeaders: false` 때문에 처음부터 안 보인다.

### Part B — 함정 4: 전역 억제가 진짜 버그를 숨긴다

`uart1_rx_poll()` 의 오버런 처리:

```c
uint32_t tmp = UART1->SR;   /* ORE 플래그는 SR 읽고 DR 읽으면 지워진다 */
tmp = UART1->DR;
(void)tmp;
```

STM32 HAL 의 `__HAL_UART_CLEAR_OREFLAG` 와 같은 모양이다(STM32F4 RM0090 의 "SR 읽기 후 DR 읽기" 순서 — 칩마다 다름, 확인 권장). 분석기는 "Value stored to 'tmp' during its initialization is never read", cppcheck 는 `redundantInitialization` — **진단 자체는 사실**이다(저장은 죽었다). 읽기가 목적이라 버그는 아니다. 오탐이라기보다 "맞는 지적, 의도된 코드" 다.

starter 는 이걸 없애려고 `-clang-analyzer-deadcode.DeadStores` 와 `--suppress=redundantInitialization` 를 **전역**으로 걸었다. 결과: `FAIL fw_bugs.c BUG06 ... no finding` — 플래시 쓰기 에러를 덮어쓰는 진짜 버그가 정확히 같은 두 id 로 잡히던 것이 함께 사라졌다. 억제는 **id 하나, 줄 하나**로:

```c
        uint32_t tmp = UART1->SR; // NOLINT(clang-analyzer-deadcode.DeadStores)
        tmp = UART1->DR;          // cppcheck-suppress redundantInitialization
```

cppcheck 인라인 억제는 `--inline-suppr` 를 줘야 읽는다(starter 엔 없음). 같은 줄 끝 주석으로도 동작했다(2.22 실측). 두 도구가 **다른 줄**(105/106)에 보고하는 것도 실측 그대로다.

### Part B — 함정 5: BUG07 은 "체크" 가 아니라 주석 기반 분석

`include/fw_util.h`:

```c
typedef struct FW_CAPABILITY("mutex") { pthread_mutex_t m; } fw_mutex_t;
static inline void fw_mutex_lock(fw_mutex_t *mu) FW_ACQUIRE(mu) FW_NO_TSA { ... }
static inline void fw_mutex_unlock(fw_mutex_t *mu) FW_RELEASE(mu) FW_NO_TSA { ... }
```

Clang 스레드 안전성 분석(Thread Safety Analysis)은 이 주석으로 "어느 함수가 락을 잡고 놓는지" 를 알고, 함수 끝에서 모든 경로의 락 상태가 같은지 본다. 컴파일러 경고(`-Wthread-safety`)라서 clang-tidy 체크 목록엔 없다. `ExtraArgs: ['-Wthread-safety']` 로 컴파일 인자에 붙이면 `clang-diagnostic-thread-safety-analysis` 로 나온다. RTOS 의 뮤텍스/크리티컬 섹션 래퍼에 이 주석을 다는 건 비용이 거의 없다(GCC 에선 매크로가 비어서 무해).

### Part B — 함정 6: 호스트 헤더가 분석 결과를 바꾼다

VM 의 clang-tidy-21 + solution 초안이 `fw_clean.c` 에서 `memcpy`/`snprintf` 를 "insecure … use memcpy_s" 로 2건 보고했다. Mac 에선 0건. 원인(E6):

```text
  [default macOS SDK] findings: 0
  [-D_FORTIFY_SOURCE=0] findings: 2
  preprocessed memcpy on the Mac:  __builtin___memcpy_chk (pkt->recs
```

macOS SDK 는 `_FORTIFY_SOURCE` 매크로로 `memcpy` 를 `__builtin___memcpy_chk` 로 바꿔서 체커가 이름을 못 본다. 펌웨어(newlib)엔 그런 매크로가 없다 → compile DB 에 `-D_FORTIFY_SOURCE=0` 을 넣어 **타깃처럼** 분석하고, C11 Annex K(`memcpy_s`)를 요구하는 이 체크는 끈다(newlib·glibc 모두 Annex K 가 없다). 끄지 않으면(E7) 고친 파일이 2건으로 CI 실패.

### Part C — 최종 기준선

<!-- code: solution.clang-tidy -->

<!-- code: solution.cppcheck -->

| 키 | 값 | 이유 |
|---|---|---|
| `Checks` | `-*` + `clang-diagnostic-*`, `clang-analyzer-*`(− Annex K), `bugprone-*`, `cert-*`, `concurrency-*`, `performance-*`, `portability-*` | 실측으로 버그를 잡거나 fw_clean 0건. `clang-diagnostic-*` 만 있으면 "Error: no checks enabled."(E2) — 진짜 체크가 하나는 있어야 한다 |
| `WarningsAsErrors` | `'*'` | exit 1 = CI 실패 |
| `HeaderFilterRegex` | `'(^|/)include/'` | 상대·절대 `-I` 둘 다, 벤더 제외 |
| `ExtraArgs` | `-Wthread-safety` | BUG07 |
| `CheckOptions` | `bugprone-signed-bitwise.IgnorePositiveIntegerLiterals: true` | `1u << 13` 잡음 제거 |
| cppcheck `--enable` | `warning,style,performance,portability` | `information`(시스템 헤더 못 찾음 10건)과 `unusedFunction`(파일 하나만 보면 전부 "미사용" 8건) 제외 |
| `--inline-suppr` / `--error-exitcode=1` | | 한 줄 억제 읽기 / CI 실패 |

**버전 고정**: VM clang-tidy-21 의 `--verify-config` 는 `unknown check option 'bugprone-signed-bitwise.IgnorePositiveIntegerLiterals'` — 21 에선 이 체크 이름이 `hicpp-signed-bitwise` 다(`--list-checks` 실측; 23 의 pip 휠에는 `hicpp-*` 가 아예 없다). 체크 수도 21: 582개, 23: 602개. CI 이미지의 clang-tidy 버전을 고정하고, 업그레이드는 "새 진단 정리" 작업과 함께 한다. `--verify-config` 를 CI 첫 단계로 두면 설정 오타가 조용히 무시되는 걸 막는다.

**scan-build 는 빌드를 감싼다** (VM, `vm.txt`):

```text
scan-build: Using '/usr/lib/llvm-21/bin/clang' for static analysis
/usr/share/clang/scan-build-21/bin/../libexec/ccc-analyzer -std=c11 -Wall -Wextra -Iinclude -Ithird_party -c fw_bugs.c -o fw_bugs.o
fw_bugs.c:55:18: warning: 2nd function call argument is an uninitialized value [core.CallAndMessage]
fw_bugs.c:105:18: warning: Value stored to 'tmp' during its initialization is never read [deadcode.DeadStores]
fw_bugs.c:224:55: warning: Use of memory after it is freed [unix.Malloc]
fw_bugs.c:272:9: warning: Value stored to 'err' during its initialization is never read [deadcode.DeadStores]
...
fw_clean.c:105:18: warning: Value stored to 'tmp' during its initialization is never read [deadcode.DeadStores]
scan-build: 4 bugs found.
scan-build exit status: 1   (HTML reports: 4)
```

`scan-build -o out --status-bugs make` 는 `CC` 를 `ccc-analyzer` 로 바꿔 진짜 컴파일(여기선 gcc — gcc 경고가 섞여 나온다)과 분석을 둘 다 하고, 버그마다 경로가 그려진 HTML 리포트를 만든다. `--status-bugs` 가 있어야 exit 1. 진단 5줄인데 "4 bugs" 인 건 두 파일의 `tmp` 보고가 같은 함수·같은 코드라 하나로 합쳐진 것으로 보인다(이슈 해시 기준 중복 제거 — 확인 권장). 그리고 **`NOLINT` 는 clang-tidy 전용**이라 scan-build/`clang --analyze` 는 fw_clean.c 의 그 줄을 여전히 보고한다. 도구마다 억제 문법이 다르다(분석기는 `#ifndef __clang_analyzer__`, gcc 는 `#pragma GCC diagnostic`) → 셋 다 돌릴 거면 **코드를 고치는 게** 억제보다 낫다(9. 추가 연습 3).

CI 흐름:

```svg
<svg viewBox="0 0 760 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <rect x="10" y="20" width="130" height="44" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="22" y="40" fill="currentColor">CMake configure</text>
  <text x="22" y="57" fill="currentColor" font-size="12">(TOOL-M04/M05)</text>
  <line x1="140" y1="42" x2="168" y2="42" stroke="currentColor"/>
  <polygon points="168,37 178,42 168,47" fill="currentColor"/>
  <rect x="180" y="20" width="225" height="44" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="190" y="40" fill="currentColor" font-size="12" font-family="monospace">compile_commands.json</text>
  <text x="190" y="57" fill="currentColor" font-size="12">-Wall -Wextra -D_FORTIFY_SOURCE=0</text>
  <line x1="405" y1="35" x2="420" y2="35" stroke="currentColor"/>
  <polygon points="420,30 430,35 420,40" fill="currentColor"/>
  <line x1="405" y1="55" x2="422" y2="96" stroke="currentColor"/>
  <polygon points="416,97 428,104 427,93" fill="currentColor"/>
  <rect x="432" y="14" width="215" height="44" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="442" y="33" fill="currentColor">clang-tidy -p build</text>
  <text x="442" y="50" fill="currentColor" font-size="12">.clang-tidy (버전 고정)</text>
  <rect x="432" y="82" width="215" height="44" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="442" y="101" fill="currentColor">cppcheck</text>
  <text x="442" y="118" fill="currentColor" font-size="12">--inline-suppr --error-exitcode=1</text>
  <line x1="647" y1="36" x2="665" y2="62" stroke="currentColor"/>
  <line x1="647" y1="104" x2="665" y2="82" stroke="currentColor"/>
  <rect x="665" y="50" width="85" height="44" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="675" y="69" fill="currentColor">gate</text>
  <text x="675" y="86" fill="currentColor" font-size="12">exit≠0 실패</text>
  <rect x="10" y="150" width="190" height="44" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="169" fill="currentColor">MR diff (git diff -U0)</text>
  <text x="20" y="186" fill="currentColor" font-size="12">레거시 경고 수천 건</text>
  <line x1="200" y1="172" x2="238" y2="172" stroke="currentColor"/>
  <polygon points="238,167 248,172 238,177" fill="currentColor"/>
  <rect x="250" y="150" width="210" height="44" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="260" y="169" fill="currentColor" font-size="12" font-family="monospace">clang-tidy-diff.py -p1</text>
  <text x="260" y="186" fill="currentColor" font-size="12">바뀐 줄의 진단만</text>
  <line x1="460" y1="172" x2="498" y2="172" stroke="currentColor"/>
  <polygon points="498,167 508,172 498,177" fill="currentColor"/>
  <rect x="510" y="150" width="240" height="44" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="520" y="169" fill="currentColor">래칫: 새 코드만 0건 요구</text>
  <text x="520" y="186" fill="currentColor" font-size="12">실측: 새 줄 2건, 레거시 294줄 0건</text>
  <text x="10" y="226" fill="currentColor" font-size="12">억제: 한 줄 · 한 id · 이유 주석. 전역 끄기 금지 (BUG06 이 같이 사라진다).</text>
  <text x="10" y="246" fill="currentColor" font-size="12">MR 잡 = solution 설정 (약 0.2초/파일), 야간 잡 = practice 설정 (max-loop 16, 약 2.8초/파일).</text>
</svg>
```

**테스트와 비교** (`runtime.txt`): `selftest.c` 를 fw_bugs.c 로 빌드하면 16개 중 4개 FAIL(BUG04, BUG07, BUG02, BUG08 증상) 후 id 3 프레임에서 `Segmentation fault`(exit 139) — lldb 로 보면 `cmd->fn(pkt)` 에서 `cmd = &cmd_table[3]`(배열 끝 다음), 그 "명령" 의 `fn = 0x0` 을 호출했다(BUG03). ASan 빌드는 그 자리에서 `global-buffer-overflow ... in cmd_lookup fw_bugs.c:207` 로 멈춘다(exit 134). 나머지 BUG01/05/06/09/10 은 크래시에 막혀 **테스트가 볼 기회조차 없었다**. 정적 분석은 실행하지 않고 모든 함수를 본다 — 반대로 BUG03 처럼 정적으로 한 도구만 잡는 것을 ASan 은 확실히 잡는다. 둘은 대체재가 아니다(TOOL-B11, TOOL-M10).

**비용** (E10, `fw_bugs.c` 한 파일, 5회 중앙값, 병렬 작업 중이라 잡음 있음): `cc -fsyntax-only` 133 ms, `clang --analyze` 152 ms, clang-tidy solution 212 ms, `'*'` 319 ms, cppcheck 159 ms, exhaustive 188 ms, **practice(max-loop 16) 2823 ms**. 루프 한도를 4→16 으로 올리면 이 파일에서 약 13배. 그래서 MR 잡은 solution, 야간 잡은 practice 같은 2단 구성이 현실적이다.

**MISRA**: `cppcheck --addon=misra --enable=style fw_clean.c` → 61건, 15개 규칙(상위 15.5 단일 출구 14건, 8.4 12건, 12.1 7건 — E9). 규칙 **본문**은 MISRA 라이선스 문서라 cppcheck 에 없고 `--rule-texts=<file>` 로 직접 넣어야 메시지가 나온다(그 전엔 "misra violation (use --rule-texts=<file> to get proper output)"). 오픈소스 애드온의 규칙 커버리지는 완전하지 않으니 인증용이라면 상용 도구(cppcheck Premium, PC-lint, Polyspace 등)를 검토 — 확인 권장.

## 6. 정답 코드

<!-- code: solution.clang-tidy diff=starter.clang-tidy -->

<!-- code: solution.cppcheck diff=starter.cppcheck -->

고친 모듈 — `fw_bugs.c` 대비 바뀐 줄이 각 FIXnn 이다(입력이라 검사 대상이지 정답 파일은 아니지만, 버그를 어떻게 고쳤는지 함께 본다):

<!-- code: fw_clean.c diff=fw_bugs.c -->

`bash check.sh solution` 실제 출력:

```text
cppcheck options (solution.cppcheck): --enable=warning,style,performance,portability --std=c11 --inline-suppr --error-exitcode=1 
PASS rules: solution.clang-tidy passes clang-tidy --verify-config
PASS rules: fw_bugs.c fw_clean.c include/ third_party/ untouched
PASS rules: suppressions scoped (cppcheck options: none global; fw_clean.c: 2 inline, one id on one line each)
PASS fw_bugs.c BUG01 found by cppcheck:217[nullPointerRedundantCheck]
PASS fw_bugs.c BUG02 found by cppcheck:168[memleak]
PASS fw_bugs.c BUG03 found by cppcheck:207[arrayIndexOutOfBoundsCond]
PASS fw_bugs.c BUG04 found by tidy:53[clang-diagnostic-sometimes-uninitialized] tidy:55[clang-analyzer-core.CallAndMessage]
PASS fw_bugs.c BUG05 found by tidy:252[clang-diagnostic-sign-compare]
PASS fw_bugs.c BUG06 found by tidy:272[clang-analyzer-deadcode.DeadStores] cppcheck:273[redundantInitialization]
PASS fw_bugs.c BUG07 found by tidy:81[clang-diagnostic-thread-safety-analysis]
PASS fw_bugs.c BUG08 found by tidy:150[bugprone-implicit-widening-of-multiplication-result]
PASS fw_bugs.c BUG09 found by tidy:224[clang-analyzer-unix.Malloc]
PASS fw_bugs.c BUG10 found by tidy:268[clang-diagnostic-format] cppcheck:268[invalidPrintfArgType_uint]
PASS CI gate: clang-tidy exits 1 on fw_bugs.c (9 findings)
PASS CI gate: cppcheck exits 1 on fw_bugs.c (7 findings)
PASS header filter: finding in our include/canary.h is reported
PASS header filter: third_party/ header not reported
PASS fw_clean.c: clang-tidy 0 findings, exit 0
PASS fw_clean.c: cppcheck 0 findings, exit 0
PASS fw_clean.c runs: selftest(fw_clean.c): 16/16 OK
20/20 passed
exit=0
```

`selftest.c` 는 ASan+UBSan(`-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined`)으로도 `selftest(fw_clean.c): 16/16 OK`. 고친 방법 요약: FIX08 은 곱하지 말고 **나눠서** 비교(`n_rec > (len - 8) / 6`) — `(size_t)` 캐스트는 Cortex-M 에서 32비트라 소용없다. FIX09 는 로그를 먼저, 해제를 나중에. FIX06 은 `if (err == 0) err = verify(...)`.

## 7. 자주 하는 실수

- **`Checks: '*'` 로 시작** → 고친 코드에서 118건. 아무도 안 읽는 리포트는 0건짜리보다 나쁘다(진짜 7건이 묻힘). → `-*` 후 계열을 실측으로 추가.
- **CI 가 녹색인데 경고가 110건** → `WarningsAsErrors: '*'`, `--error-exitcode=1`. 출력이 아니라 exit code 가 게이트.
- **헤더 필터를 경로 철자와 무관하게 짠다고 착각** → `'.*/include/.*'` 는 `-Iinclude` 에서 우리 헤더를 조용히 놓친다. `'(^|/)include/'` + canary 로 설정을 시험.
- **보기 싫은 경고 하나 때문에 체크를 전역으로 끔** → 같은 id 의 진짜 버그(BUG06)가 사라짐. `// NOLINT(<one-check>)`, `// cppcheck-suppress <id>` 를 그 줄에만, 이유와 함께. 맨 `NOLINT`(모든 체크 끔)나 `NOLINTBEGIN/END` 로 함수 통째 덮기 금지.
- **cppcheck 인라인 억제를 써 놓고 `--inline-suppr` 를 안 줌** → 억제가 무시된다(`matrix.txt` 의 `cppcheck --enable=all` 열: fw_clean 15건 중 `redundantInitialization` 1건이 바로 그 억제된 줄).
- **`CheckOptions` 로 분석기 옵션을 넘기려 함** → 23.1.4 에서 `clang-analyzer-suppress-null-return-paths: false` 를 넣어도 `--dump-config` 에 `CheckOptions: {}` 로 사라지고 효과 없음(E5). `ExtraArgs: ['-Xclang', '-analyzer-config', '-Xclang', 'k=v']` 는 동작.
- **호스트에서 0건이니 타깃도 0건이라 가정** → macOS fortify 매크로가 `memcpy` 를 숨겼다(E6), `size_t` 가 64비트라 BUG08 이 보였다(Cortex-M 에선 침묵). compile DB 를 타깃처럼(`-D_FORTIFY_SOURCE=0`, 가능하면 `--target=thumbv7em-none-eabi` + 타깃 헤더) — 확인 권장.
- **`-Wall -Wextra` 를 compile DB 에서 빼먹음** → clang-tidy 의 `clang-diagnostic-*` 는 컴파일 명령에 켜진 경고만 보고한다. BUG05 는 `-Wextra` 가 없으면 어디에도 안 나온다.

## 8. 면접 Q&A

<details>
<summary>Q1. clang-tidy, Clang static analyzer(scan-build), cppcheck 은 각각 뭐고 왜 하나로 안 되나요?</summary>

clang-tidy = Clang AST 위의 **매처/규칙 모음**(bugprone, cert, readability…) + 컴파일러 경고 전달(`clang-diagnostic-*`) + 분석기 호출(`clang-analyzer-*`). Clang static analyzer = **경로 민감 심볼릭 실행**: 함수를 경로별로 따라가며 값의 범위·메모리 상태를 추적(누수, UAF, NULL). scan-build 는 그 분석기를 빌드에 끼워 HTML 리포트를 만드는 래퍼. cppcheck = 컴파일러 없이 토큰과 **값 흐름**으로 보는 독립 분석기. 실측: 분석기는 NULL-return 휴리스틱·루프 한도 때문에 BUG01/02 를 놓쳤고, cppcheck 는 함수 경계 너머의 `free` 를 놓쳤고(BUG09), BUG08 은 AST 패턴만 잡았다. 합집합이 10/10.

</details>

<details>
<summary>Q2. 분석기가 cppcheck 도 잡는 NULL 역참조를 왜 놓치나요?</summary>

`cmd = cmd_lookup(id)` 가 인라인되면서 NULL 이 callee 의 `return NULL` 에서 왔다. Clang 분석기는 `suppress-null-return-paths=true`(기본)로 그런 경로의 보고를 버린다 — "방어적 NULL 반환 함수" 때문에 생기는 오탐을 줄이려는 의도. `-Xanalyzer -analyzer-config -Xanalyzer suppress-null-return-paths=false` 로 끄면 217줄이 보고된다(E4 실측). 꼬리질문 "그럼 늘 끄면?" → 다른 코드에서 오탐이 늘 수 있다. 우리 fw_clean.c 에선 0건이었지만 대규모 코드에선 측정 후 결정.

</details>

<details>
<summary>Q3. 정적 분석기가 "놓친다" 는 건 버그인가요? 경로 폭발은 어떻게 다루나요?</summary>

설계상 트레이드오프다. 경로 수는 분기마다 2배라 분석기는 예산을 둔다: 같은 루프 블록 방문 4회(`-analyzer-max-loop`), 인라인 깊이, 노드 수. 예산을 넘은 경로는 **버린다**(보고도 안 함 = 미탐). 실측: `crc8` 의 8회 루프 때문에 CRC 이후 경로가 전부 사라져 BUG02 누수 미보고, 16 으로 올리면 보고, 대신 clang-tidy 시간이 212 → 2823 ms(E10). 그래서 sound(모든 버그 보장)가 아니라 **오탐을 적게** 하는 쪽으로 튜닝된 도구다. 반대 성격(정형 검증, Frama-C/Polyspace 의 증명 모드)은 미탐 0 을 목표로 하지만 오탐·비용이 크다.

</details>

<details>
<summary>Q4. 레거시 코드베이스(경고 수천 건)에 어떻게 도입하나요?</summary>

플래그 데이 금지. ① 체크 계열을 실측으로 고르고 `--verify-config`. ② **래칫**: MR 에서 바뀐 줄만 검사(`git diff -U0 | clang-tidy-diff.py -p1` — 실측: 새 함수 2건만, 레거시 294줄 0건). ③ 디렉터리별로 정리 완료되면 전체 검사로 승격. ④ 경고 수를 지표로 추적하고 "증가 금지". cppcheck 는 `--suppressions-list`/XML 베이스라인이 있지만 줄 번호 기반이라 코드가 움직이면 깨진다 — diff 기반이 더 튼튼하다. 꼬리질문: "기존 경고는 영원히?" → 체크 하나씩 0 으로 만들고 그 체크를 `WarningsAsErrors` 로 승격.

</details>

<details>
<summary>Q5. 오탐은 어떻게 억제하나요? 정책을 말해 보세요.</summary>

(1) 먼저 코드를 고쳐 도구가 이해하게(HAL 관용구는 `(void)UART1->SR; (void)UART1->DR;` 로 바꾸면 세 도구 모두 0건 — E8). (2) 못 고치면 **한 줄·한 id·이유**: `// NOLINT(clang-analyzer-deadcode.DeadStores)`, `// cppcheck-suppress redundantInitialization`. (3) 전역 끄기·맨 `NOLINT`·블록 억제는 리뷰에서 거부. 실측 근거: 전역 억제가 BUG06 을 같이 숨겼다. (4) 억제 개수를 세서 CI 리포트에 올린다(check.sh 의 "fw_clean.c: 2 inline"). 그리고 "오탐" 과 "맞는 지적이지만 의도된 코드" 를 구분 — 이번 경우는 후자.

</details>

<details>
<summary>Q6. 같은 버그가 호스트 분석에선 보이고 Cortex-M 에선 안 보일 수 있나요?</summary>

그렇다. BUG08 `size_t body = n_rec * REC_SIZE;` 는 Mac(LP64)에선 "32비트 곱셈 결과를 64비트로 넓힘" 이라 `bugprone-implicit-widening-of-multiplication-result` 가 잡지만, ARM 32비트에선 `size_t` 도 32비트라 넓힘이 없어 침묵 — 랩은 그대로. 반대로 Mac SDK 의 fortify 매크로는 `memcpy` 를 숨겨 체커가 못 봤다(E6). 해결: compile DB 를 타깃처럼, 가능하면 타깃 트리플로도 한 번 분석, 그리고 **나눗셈 비교**처럼 플랫폼과 무관하게 안전한 코드(FIX08).

</details>

<details>
<summary>Q7. clang-tidy 를 업그레이드했더니 CI 가 깨졌습니다. 왜 그런가요?</summary>

체크가 늘고(21: 582 → 23: 602), 이름이 바뀌고(`hicpp-signed-bitwise` → `bugprone-signed-bitwise`, 21 의 `--verify-config` 가 23 용 옵션을 unknown 으로 경고), **기본값**이 바뀐다(HeaderFilterRegex 21 `''` → 23 `'.*'`: 설정에 없으면 벤더 헤더 경고가 갑자기 쏟아짐). 대책: 버전 고정(CI 이미지), 설정에 기본값도 명시, `--verify-config` 를 첫 단계로, 업그레이드는 별도 MR 에서 새 진단을 정리하며.

</details>

<details>
<summary>Q8. 정적 분석이 있으면 ASan·테스트가 필요 없나요? 반대는?</summary>

둘 다 필요하다. 실측: fw_bugs.c 를 selftest 로 돌리면 4개 FAIL 후 BUG03 에서 크래시 — BUG01/05/06/09/10 은 실행조차 안 됐다. 정적 분석은 모든 함수를 실행 없이 본다(에러 경로, 드문 분기). 반대로 BUG03 은 정적으로 cppcheck 하나만 잡았지만 ASan 은 그 줄을 확정적으로 짚었다. 정적 = 넓고 얕게(경로 예산), 동적 = 실행된 경로만 깊고 정확하게. 펌웨어에선 여기에 컴파일러 경고(TOOL-B02), 호스트 단위 테스트 + 새니타이저(TOOL-B11/B12), 타깃 HIL 이 겹친다. MISRA 같은 코딩 규칙 검사는 버그 탐지와 목적이 다르다(안전 인증·이식성).

</details>

## 9. 추가 연습

### 연습 1 — cppcheck 가 없는 러너: clang-tidy 만으로 ≥ 9/10

새 CI 이미지엔 cppcheck 가 없다. `practice.clang-tidy` 를 만들어 clang-tidy **혼자** 9개 이상 잡게 하라(고친 파일은 여전히 0건).

<details>
<summary>답 — 분석기 노브 두 개를 ExtraArgs 로</summary>

solution 에 `ExtraArgs` 로 `-Xclang -analyzer-max-loop -Xclang 16` 과 `-Xclang -analyzer-config -Xclang suppress-null-return-paths=false` 를 더한다. BUG02(누수)와 BUG01(NULL)이 clang-tidy 로도 잡혀 9/10. BUG03 은 분석기가 "범위 밖일 수도" 를 보고하지 않아 못 잡는다 — cppcheck 가 계속 필요한 이유. `CheckOptions` 경로는 23.1.4 에서 효과가 없었다(E5). 대가: 이 파일에서 212 → 2823 ms. 루프 한도를 올렸더니 고친 파일에서 `malloc(0)`(`optin.portability.UnixAPI`)이 새로 보고됐는데, 진짜 이식성 문제였다(newlib 등에서 `malloc(0)` 이 NULL 이면 정상 0-레코드 프레임을 할당 실패로 버림) → 두 파일 모두 "body 가 0 이면 할당 안 함" 으로 고쳐 두었다.

</details>

### 연습 2 — 래칫: 바뀐 줄만 검사

레거시 `fw_bugs.c` 를 커밋한 저장소에 새 함수(`rssi_avg`, `sum` 미초기화)를 추가하는 MR 을 만들고, 새 줄의 진단만 나오게 하라.

<details>
<summary>답 — git diff -U0 | clang-tidy-diff.py</summary>

`check.sh practice` 가 `/tmp/TOOL-M12_work/practice/ratchet` 에 결정적 저장소(`GIT_CONFIG_GLOBAL=/dev/null`, 고정 날짜)를 만들고 실행한다:

```bash
git diff -U0 HEAD | python3 -I clang-tidy-diff.py -p1 -quiet \
    -clang-tidy-binary $CT -config-file practice.clang-tidy -path <dir with compile_commands.json>
```

실제 출력(줄여서):

```text
fw_bugs.c:301:9: error: variable 'sum' is uninitialized when used here [clang-diagnostic-uninitialized,-warnings-as-errors]
fw_bugs.c:301:13: error: The left expression of the compound assignment uses uninitialized memory [clang-analyzer-core.uninitialized.Assign,-warnings-as-errors]
```

레거시 BUG01–10 은 바뀐 줄이 아니라 보고되지 않는다. 내부적으로 `-line-filter` 로 바뀐 줄 범위를 clang-tidy 에 넘긴다(분석은 파일 전체, 보고만 필터).

</details>

### 연습 3 — 억제 없이 HAL 관용구 쓰기

`fw_clean.c` 의 오버런 처리에서 억제 주석 두 개를 없애고도 clang-tidy, cppcheck, `clang --analyze` 모두 0건이 되게 바꿔라. 레지스터 읽기 두 번이 사라지지 않았음을 어셈블리로 확인하라.

<details>
<summary>답 — (void) 로 volatile 읽기</summary>

```c
        (void)UART1->SR;
        (void)UART1->DR;
```

E8 실측: `clang --analyze` 0, clang-tidy solution 0, cppcheck solution 0. `volatile` 멤버 읽기는 결과를 버려도 지워지지 않는다(C11 5.1.2.3 — volatile 접근은 관찰 가능한 동작). `-O2 -S` 의 `uart1_rx_poll` 에 `ldr wzr, [x8]` / `ldr wzr, [x8, #4]` 두 줄이 남는다(wzr = 결과 버림). 세 도구를 다 돌리는 팀이라면 이 쪽이 억제 3종보다 낫다.

</details>

### 연습 4 — VM 의 gcc -fanalyzer 와 scan-build

`demo_vm.sh` 를 돌려 gcc 15 `-fanalyzer` 가 Clang 분석기와 어떻게 다른지 비교하라. arm-none-eabi-gcc 로도 쓸 수 있다는 점이 펌웨어에서 왜 중요한가?

<details>
<summary>답</summary>

`-fanalyzer` 는 BUG01 을 잡았다(`dereference of NULL 'cmd' [CWE-476]`) — Clang 의 NULL-return 억제 같은 휴리스틱이 없다. 대신 BUG02 누수와 BUG06 은 놓쳤다. 경고 이름이 CWE 번호를 달고 나온다. GCC 분석기는 GCC 백엔드와 같은 컴파일러라 **타깃 툴체인(arm-none-eabi-gcc 14, 타깃 헤더·`size_t` 32비트)** 그대로 돌릴 수 있다 — 호스트 clang 분석과 결과가 달라지는 Q6 문제를 줄인다(GCC 13 이후 C 분석이 꽤 쓸 만해졌다고 알려짐 — 확인 권장). scan-build 는 `make` 를 감싸 실제 빌드와 분석을 동시에 하고 `--status-bugs` 로 exit 1.

</details>

### 연습 5 — MISRA 애드온 맛보기

`cppcheck --addon=misra --enable=style fw_clean.c` 를 돌려 가장 많이 나온 규칙 세 개를 보고, 각각이 이 코드의 어떤 습관을 지적하는지 추측하라.

<details>
<summary>답</summary>

실측 61건: `misra-c2012-15.5`(14, 함수 중간 `return` — 단일 출구 규칙), `8.4`(12, 외부 링크 함수에 앞선 선언 없음 — 헤더에 프로토타입을 두라), `12.1`(7, 연산자 우선순위를 괄호로 명시). 버그가 아니라 **코딩 규칙**이다 — 차량/의료 인증 프로젝트에서 요구된다. 규칙 본문은 MISRA 문서를 사서 `--rule-texts` 로 넣어야 메시지가 나온다. 일부 규칙(15.5 등)은 팀에서 deviation 으로 문서화해 빼는 게 보통이다.

</details>

### 한 번에 검증

<!-- code: practice.clang-tidy -->

`practice.cppcheck` (solution + 야간 잡용 `--check-level=exhaustive`; 이 파일에선 결과 동일, 159 → 188 ms):

```text
--enable=warning,style,performance,portability
--std=c11
--inline-suppr
--error-exitcode=1
--check-level=exhaustive
```

```bash
bash check.sh practice
```

실제 출력:

```text
cppcheck options (practice.cppcheck): --enable=warning,style,performance,portability --std=c11 --inline-suppr --error-exitcode=1 --check-level=exhaustive 
... (rules 3줄 PASS, solution 과 같음)
PASS fw_bugs.c BUG01 found by tidy:217[clang-analyzer-core.NullDereference] cppcheck:217[nullPointerRedundantCheck]
PASS fw_bugs.c BUG02 found by tidy:168[clang-analyzer-unix.Malloc] cppcheck:168[memleak]
PASS fw_bugs.c BUG03 found by cppcheck:207[arrayIndexOutOfBoundsCond]
... (BUG04–BUG10 PASS, solution 과 같음)
PASS CI gate: clang-tidy exits 1 on fw_bugs.c (11 findings)
PASS CI gate: cppcheck exits 1 on fw_bugs.c (7 findings)
PASS header filter: finding in our include/canary.h is reported
PASS header filter: third_party/ header not reported
PASS fw_clean.c: clang-tidy 0 findings, exit 0
PASS fw_clean.c: cppcheck 0 findings, exit 0
PASS fw_clean.c runs: selftest(fw_clean.c): 16/16 OK
PASS clang-tidy alone finds 9/10 planted bugs (>= 9)
PASS ratchet: clang-tidy-diff reports 2 finding(s) on new lines, 0 on the 294 legacy lines
22/22 passed
exit=0
```

## 10. 핵심 정리

- 엔진이 셋: AST 매처(clang-tidy 체크, 컴파일러 경고) / 경로 민감 심볼릭 실행(Clang 분석기 = scan-build = `clang-analyzer-*`, gcc `-fanalyzer`) / 값 흐름+패턴(cppcheck). 실측 단독 최고 7/10, 합집합 10/10.
- 분석기는 예산(`-analyzer-max-loop 4`)과 오탐 억제 휴리스틱(`suppress-null-return-paths`) 때문에 진짜 버그도 버린다 — "0건" 은 "버그 없음" 이 아니다.
- 기준선 = `-*` 후 실측으로 계열 선택 + `WarningsAsErrors: '*'` + `HeaderFilterRegex: '(^|/)include/'` + `ExtraArgs: [-Wthread-safety]`; cppcheck `--enable=warning,style,performance,portability --inline-suppr --error-exitcode=1`. 고친 코드 0건이 조건.
- 억제는 한 줄·한 id·이유. 전역 억제는 같은 id 의 진짜 버그를 숨긴다(BUG06). 코드를 고칠 수 있으면 고친다(`(void)REG;`).
- 분석은 타깃처럼: compile DB 에 실제 경고 플래그, `-D_FORTIFY_SOURCE=0`; 호스트 LP64 와 Cortex-M ILP32 에서 결과가 다르다.
- 툴 버전 고정 + `--verify-config`: 21→23 에서 기본 헤더 필터(`''`→`'.*'`), 체크 이름(`hicpp-`→`bugprone-signed-bitwise`)이 바뀌었다.
- 레거시엔 래칫(`clang-tidy-diff.py`, 바뀐 줄만), 무거운 설정은 야간 잡(practice: 약 13배 느림).

## 참고자료

- clang-tidy 문서(설정 키 `Checks`, `WarningsAsErrors`, `HeaderFilterRegex`, `ExcludeHeaderFilterRegex`, `ExtraArgs`, NOLINT 문법): https://clang.llvm.org/extra/clang-tidy/
- clang-tidy 체크 목록: https://clang.llvm.org/extra/clang-tidy/checks/list.html
- Clang Static Analyzer 문서·체커 목록: https://clang.llvm.org/docs/analyzer/checkers.html , scan-build: https://clang-analyzer.llvm.org/scan-build.html
- Clang Thread Safety Analysis: https://clang.llvm.org/docs/ThreadSafetyAnalysis.html
- cppcheck 매뉴얼(인라인 억제, `--error-exitcode`, `--check-level`, MISRA 애드온): https://cppcheck.sourceforge.io/manual.pdf
- GCC `-fanalyzer`: https://gcc.gnu.org/onlinedocs/gcc/Static-Analyzer-Options.html
- MISRA C:2012 (규칙 본문은 유료): https://misra.org.uk/
- 이 레슨의 실측 원본: `matrix.txt`(demo_matrix.sh), `vm.txt`(demo_vm.sh), `extra.txt`(demo_extra.sh E1–E10), `runtime.txt`(demo_runtime.sh)
- 관련 레슨: TOOL-B02(컴파일러 경고 기준선), TOOL-B11(ASan), TOOL-B12(UBSan), TOOL-M04(CMake·compile_commands.json), TOOL-M09(TSan), TOOL-M10(leaks/valgrind), CPP-A09(C++ 의 clang-tidy)
