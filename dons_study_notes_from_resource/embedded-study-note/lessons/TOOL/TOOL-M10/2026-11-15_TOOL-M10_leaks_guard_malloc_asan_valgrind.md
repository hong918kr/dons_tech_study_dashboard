---
id: TOOL-M10
time: 80분
updated: 2026-11-15
---
# TOOL-M10 Valgrind 대안: leaks·MallocStackLogging·ASan

## 학습 목표

- Apple Silicon 맥에는 valgrind 가 없다는 전제에서, **버그 종류마다 맞는 도구**를 고른다: 잃어버린 포인터 → `leaks`, 닿지만 잊힌 메모리 → `malloc_history` / `leaks --referenceTree`, 1바이트 오버플로·해제 후 쓰기 → ASan / Guard Malloc, 초기화 안 된 읽기 → valgrind / MSan (VM).
- `leaks` 가 누수를 정하는 방법(루트에서 시작하는 보수적 스캔), `ROOT LEAK` 의 뜻, `MallocStackLogging` 의 lite / full 차이(현재 할당만 vs 할당+해제 이력)를 설명하고, full 이력으로 "누가 할당하고 누가 풀었나" 를 주소 하나로 뽑는다.
- Guard Malloc(`libgmalloc`)이 페이지 경계로 오버플로를 잡는 원리와 **16바이트 정렬 때문에 놓치는 경우**를 실측으로 보여 준다. `MallocScribble` 등 libmalloc 환경 변수가 무엇을 하고 무엇을 못 하는지 안다.
- 같은 프로그램을 VM 의 valgrind memcheck · LSan · MSan 에 돌려 **탐지 매트릭스(버그 × 도구)** 와 **비용표(속도 ×, 메모리 ×)** 를 실제 실행으로 만들고, CI 에 어떤 조합을 넣을지 정한다.
- 펌웨어 관점: MCU 위에서는 이 도구들이 없다 → 하드웨어와 무관한 모듈을 호스트에서 이 도구들로 검사하고, 타깃에서는 힙 통계·래퍼로 보완한다.

## 1. 문제

"맥으로 개발하는데 valgrind 가 없잖아요, 누수는 어떻게 찾아요?" — 펌웨어 팀 면접과 실무에서 실제로 나오는 질문이다. 게이트웨이(임베디드 Linux) 데몬이나 펌웨어 모듈의 호스트 단위 테스트에서 메모리 버그를 놓치면, 필드에서 몇 주 돌다가 메모리가 바닥나거나(누수) 엉뚱한 곳에서 크래시(힙 손상)가 난다. 도구마다 보는 것이 다르므로 **"이 버그는 어느 도구가 잡나"** 를 말할 수 있어야 한다.

valgrind 는 바이너리를 가상 CPU 위에서 돌리는 도구라서 OS·CPU 마다 포팅이 필요한데, upstream valgrind 는 Apple Silicon(arm64) macOS 를 지원하지 않는다(확인 권장 — 이 맥에는 설치돼 있지 않고, VM 에는 3.26.0 이 있다). 그래서 맥에서는 도구 여러 개를 조합한다. 각 도구가 **프로그램의 어디에 끼어드는지** 가 곧 무엇을 볼 수 있는지를 정한다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">도구가 끼어드는 자리 = 볼 수 있는 것</text>
  <rect x="10" y="40" width="120" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="66" fill="currentColor">소스 + 컴파일러</text>
  <rect x="160" y="40" width="120" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="178" y="66" fill="currentColor">실행 파일 + dyld</text>
  <rect x="310" y="40" width="130" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="322" y="60" fill="currentColor">libmalloc</text>
  <text x="322" y="76" fill="currentColor" font-size="12">(malloc/free)</text>
  <rect x="470" y="40" width="120" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="482" y="60" fill="currentColor">CPU 가 실행</text>
  <text x="482" y="76" fill="currentColor" font-size="12">load / store</text>
  <rect x="620" y="40" width="110" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="632" y="60" fill="currentColor">프로세스 종료</text>
  <text x="632" y="76" fill="currentColor" font-size="12">힙에 남은 것</text>
  <path d="M130 62 L160 62 M280 62 L310 62 M440 62 L470 62 M590 62 L620 62" stroke="currentColor"/>
  <rect x="10" y="104" width="270" height="52" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="124" fill="currentColor">ASan / MSan: 컴파일러가 load/store 마다</text>
  <text x="20" y="144" fill="currentColor">검사 코드 삽입 → 재컴파일 필요 (MSan 은 VM 만)</text>
  <rect x="160" y="166" width="280" height="52" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="170" y="186" fill="currentColor">Guard Malloc: DYLD_INSERT_LIBRARIES 로</text>
  <text x="170" y="206" fill="currentColor">malloc 을 통째로 교체 (블록마다 페이지)</text>
  <rect x="310" y="228" width="280" height="52" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="320" y="248" fill="currentColor">MallocScribble / MallocStackLogging:</text>
  <text x="320" y="268" fill="currentColor">libmalloc 자체 옵션 (환경 변수만)</text>
  <rect x="460" y="104" width="270" height="52" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="470" y="124" fill="currentColor">valgrind: 가상 CPU 로 명령마다 추적</text>
  <text x="470" y="144" fill="currentColor">재컴파일 X, 맥 arm64 X → VM 에서만</text>
  <rect x="600" y="166" width="130" height="114" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="610" y="186" fill="currentColor">leaks / heap /</text>
  <text x="610" y="204" fill="currentColor">malloc_history:</text>
  <text x="610" y="222" fill="currentColor" font-size="12">프로세스 밖에서</text>
  <text x="610" y="240" fill="currentColor" font-size="12">힙을 읽고 스캔</text>
  <text x="610" y="258" fill="currentColor" font-size="12">(--atExit = 종료</text>
  <text x="610" y="274" fill="currentColor" font-size="12">직전에 멈춰서)</text>
</svg>
```

이 레슨의 모듈 `evlog` 는 게이트웨이가 무선 노드에서 받은 센서 이벤트를 쌓는 로그다. 레코드(장치·값·텍스트·재시도 횟수)는 꼬리 포인터가 있는 연결 리스트, 장치 이름은 전역 캐시(`g_devs`)에 한 번만 저장한다. 버그 5개가 `TODO(...)` 로 표시돼 있고, 드라이버는 **케이스 하나 = 버그 하나 = 별도 프로세스** 로 돈다 (`./ev overflow|uaf|leak|cache|uninit|all`).

| 케이스 | 함수 | 버그 | 일반 malloc 에서 보이는 증상 |
|---|---|---|---|
| `leak` | `rec_set_text` | 텍스트를 바꿀 때 예전 포인터를 덮어씀 (잃어버린 포인터) | 없음 |
| `cache` | `dev_cache_clear` | 종료 때 캐시를 안 비움 — 전역에서 **닿으니까** 누수 도구가 조용함 | 없음 |
| `overflow` | `rec_to_line` | `malloc(len)` 에 `s[len] = '\0'` (1바이트 넘침) | 없음 (16바이트 칸의 남는 자리에 씀) |
| `uaf` | `evlog_purge` | 꼬리 레코드를 지우고 `g_tail` 을 안 옮김 → 다음 추가가 해제된 블록에 `next` 를 씀 | 새 레코드가 리스트에서 사라짐 (`values=40`) |
| `uninit` | `rec_new` | `retries` 필드를 초기화 안 함 | 없음 (새 페이지는 0 이라 운 좋게 맞음) |

### Part A — 누수 두 종류: 잃어버린 포인터 vs 닿지만 잊힌 메모리

- `leak` 케이스를 `MallocStackLogging=1 leaks --atExit` 로 돌려 `ROOT LEAK` 와 할당 스택을 읽고 고친다. 스택 기록 모드(없음 / `0` / `1` / `lite` / `full`)에 따라 리포트가 어떻게 달라지는지 비교한다.
- `cache` 케이스는 `leaks` 가 **0 leaks** 라고 한다. `--outputGraph` 로 메모리 그래프(memgraph)를 저장하고 `malloc_history -allBySize`, `leaks --referenceTree` 로 "종료 시점에 누가 아직 240바이트를 들고 있나" 를 찾는다.

### Part B — 함정: 일반 malloc 에서는 조용한 버그 3개

- `overflow`: ASan 은 바로 잡는데 Guard Malloc 은 첫 줄(11바이트)을 **놓치고** 둘째 줄(16바이트)에서 잡는다. 왜 그런지 페이지 배치로 설명한다.
- `uaf`: 해제된 꼬리에 쓰는 순간을 ASan(세 스택) · Guard Malloc(SIGSEGV) 으로 잡고, `MallocStackLogging=full` 이력으로 같은 사실(할당/해제 스택)을 맥 도구만으로 재구성한다. libmalloc 의 검사용 환경 변수는 이 버그를 못 잡는다는 것도 실측한다.
- `uninit`: `MallocScribble=1`(새 블록을 `0xaa` 로 채움)과 ASan(`0xbe`)은 **값을 바꿔서 증상을 드러낼 뿐** 보고는 안 한다. 진짜 "초기화 안 된 값 사용" 보고는 valgrind 와 MSan 만 한다 (VM).

### Part C — 펌웨어 팀 현실: 매트릭스 · 비용 · CI

- `demo_matrix.sh` 로 버그 × 도구 탐지 매트릭스를, `demo_cost.sh` 로 도구별 속도·메모리 비용을 실제 실행으로 만든다.
- `check.sh` 를 CI 게이트로 쓰는 방법, valgrind 를 CI 에 넣을 때 필요한 옵션(`--error-exitcode`, `--errors-for-leak-kinds`), MCU 타깃에서의 대안(힙 통계, 래퍼, 정적 풀)을 정리한다.

## 2. Problem (English)

> **Find and fix five memory bugs in an event-log module without Valgrind (macOS, Apple Silicon).**
>
> `evlog` stores sensor events in a singly linked list with a tail pointer, interns device names in a global cache `g_devs`, and exports `"name=value"` lines to a host. The driver runs one case per process: `./ev overflow|uaf|leak|cache|uninit|all`; each prints exactly one result line.
>
> Using only macOS tools — `leaks`, `MallocStackLogging`, `malloc_history`, Guard Malloc (`libgmalloc`), `MallocScribble`, AddressSanitizer — locate each bug, then fix **only the `TODO` lines** in `starter.c`. The fixed program must pass `bash check.sh solution`:
>
> - ASan reports nothing for `overflow` and `uaf`; `leaks --atExit` reports `0 leaks for 0 total leaked bytes.` for `leak`;
> - for `cache`, a memory graph taken at exit shows no live block allocated under `dev_intern` (note: `leaks` already says 0 for the buggy version — explain why);
> - `uninit` prints the same result under `MallocScribble=1`; `all` runs clean under Guard Malloc.
>
> Then compare with Valgrind memcheck, LeakSanitizer and MemorySanitizer on Linux (VM): which tool detects which bug, and at what cost?
>
> Follow-ups: how does `leaks` decide what is leaked; what is "still reachable"; why did Guard Malloc miss an overflow; why can't ASan report uninitialized reads; what would you run in CI; how do you find leaks on a Cortex-M target?

## 3. 시작 코드

<!-- code: starter.c -->

검사 스크립트 (Form S, 맥 전용 채점 — `bash check.sh <starter|solution|practice>`, 작업은 `/tmp/TOOL-M10_work/<variant>` 에서만):

<!-- code: check.sh -->

손으로 돌리기:

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o ev && ./ev all
cc -std=c11 -g -O0 starter.c -o ev_g && MallocStackLogging=1 leaks --atExit -- ./ev_g leak
bash check.sh starter
```

처음 실행 결과 (실제 출력):

```text
$ ./ev all
overflow: lines=gw-temp=215,node-07-hum=4551,node-12=90210
uaf: purged=3 left=2 values=40
leak: recs=3 texts=ack,rx late,rx
cache: devs=6 recs=6
uninit: retries=3

$ bash check.sh starter
PASS build -Werror -O2 (starter.c)
FAIL selftest all  first diff: < uaf: purged=3 left=2 values=40,12
FAIL overflow/asan  AddressSanitizer: heap-buffer-overflow starter.c:147 in rec_to_line
FAIL uaf/asan  AddressSanitizer: heap-use-after-free starter.c:93 in evlog_add
FAIL leak/leaks  rc=1 3 leaks for 64 total leaked bytes.
FAIL cache/malloc_history  10 blocks / 240 bytes from dev_intern still alive at exit (leaks said: 0 leaks for 0 total leaked bytes.)
FAIL uninit/MallocScribble  output 'uninit: retries=1431655767' want 'uninit: retries=3'
FAIL all/GuardMalloc  rc=139 first diff: < overflow: lines=gw-temp=215,node-07-hum=4551,node-12=90210
1/8 passed
```

일반 실행 결과에서 눈에 띄는 증상은 두 개뿐이다: `uaf` 의 `values=40` (12 가 사라짐) 과 `all` 모드의 `devs=6` (앞 케이스의 캐시가 남아 있음). 나머지 세 버그는 **일반 malloc 에서 완벽하게 조용하다**. 그래서 도구가 필요하다.

## 4. 힌트

<details><summary>힌트 1 — leaks 리포트에서 볼 곳</summary>

`ROOT LEAK: <malloc in str_dup>` 의 스택에서 `str_dup` 바로 위 프레임이 `rec_set_text` 다. 그 함수가 `r->text` 에 새 포인터를 넣기 전에 예전 값은 어떻게 되나? 리포트에 블록 내용(`"rx crc-retry"`)까지 찍히니 어느 텍스트가 버려졌는지도 보인다.
</details>

<details><summary>힌트 2 — leaks 가 0 이라고 하면 정말 0 인가?</summary>

`leaks` 는 "아무 데서도 가리키지 않는 블록" 만 누수라고 부른다. 전역 `g_devs` 가 가리키는 블록은 종료 때까지 닿는다. "종료 시점에 살아 있는 블록을 할당 스택별로" 보려면 `leaks --atExit --outputGraph=x.memgraph` 로 그래프를 저장하고 `malloc_history x.memgraph -allBySize` 를 본다.
</details>

<details><summary>힌트 3 — Guard Malloc 이 첫 줄을 그냥 지나간 이유</summary>

`GuardMalloc: Allocations will be placed on 16 byte boundaries.` 배너를 보라. 11바이트 블록은 16바이트로 정렬되어 페이지 끝에 놓이므로 끝에 5바이트 여유가 생긴다. `s[11]` 은 그 여유 안이다. 16바이트 블록이면 `s[16]` 이 바로 다음 페이지(가드 페이지)다.
</details>

<details><summary>힌트 4 — 꼬리를 지울 때</summary>

`evlog_purge` 는 `prev->next` / `g_head` 는 고치지만, 지운 레코드가 **꼬리였을 때** `g_tail` 은 해제된 블록을 계속 가리킨다. 다음 `evlog_add` 의 `g_tail->next = r` 가 해제 후 쓰기다. 꼬리를 지웠다면 꼬리는 `prev` 가 된다 (`prev` 가 NULL 이면 리스트가 빈 것).
</details>

<details><summary>힌트 5 — 초기화 안 된 값은 왜 운 좋게 0 인가</summary>

새로 받은 페이지는 커널이 0 으로 채워 준다. `MallocScribble=1` 은 할당할 때마다 `0xaa` 로 채우므로 `retries` 가 `0xaaaaaaaa + 재시도 수` 가 된다. `rec_new` 가 모든 필드를 설정하는지 세어 보라.
</details>

## 5. 풀이

### Part A-1 — `leaks` 는 무엇을 "누수" 라고 부르나

`leaks` 는 대상 프로세스를 멈추고 (`--atExit` 는 `exit` 직전에) 프로세스 밖에서 힙을 읽는다. 원리는 가비지 컬렉터의 mark 단계와 같다 (VM-M02 참고).

1. **루트(root)**: 전역 데이터(`__DATA`, `__bss`), 모든 스레드의 스택, 레지스터.
2. 루트와, 이미 표시한 블록 안의 8바이트 정렬 값 하나하나가 "어떤 힙 블록 안을 가리키는가?" 를 본다 → 가리키면 그 블록을 **닿음(reachable)** 으로 표시. 타입 정보 없이 모든 값을 포인터 후보로 보므로 **보수적(conservative)** 스캔이다 (`leaks --help` 의 `-c/--conservative` 는 Objective-C ivar 정보까지 끄고 순수 바이트 스캔만 하는 옵션).
3. 표시되지 않은 블록 = 누수. 그중 **다른 누수 블록에서도 가리켜지지 않는** 것이 `ROOT LEAK` (사슬의 머리), 나머지는 그 밑에 들여쓰기로 붙는다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">leaks 의 표시(mark): 루트에서 닿는가?  (starter, leak + cache 상황)</text>
  <rect x="10" y="40" width="150" height="200" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor" font-weight="bold">루트</text>
  <text x="20" y="88" fill="currentColor" font-family="monospace">__bss: g_devs</text>
  <text x="20" y="114" fill="currentColor" font-family="monospace">__bss: g_head</text>
  <text x="20" y="140" fill="currentColor">스레드 스택</text>
  <text x="20" y="166" fill="currentColor">레지스터</text>
  <rect x="220" y="70" width="90" height="34" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="230" y="92" fill="currentColor" font-family="monospace">dev</text>
  <rect x="330" y="70" width="90" height="34" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="340" y="92" fill="currentColor" font-family="monospace">dev</text>
  <rect x="440" y="70" width="90" height="34" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="450" y="92" fill="currentColor" font-family="monospace">dev …×5</text>
  <path d="M160 84 L220 87 M310 87 L330 87 M420 87 L440 87" stroke="currentColor"/>
  <text x="220" y="124" fill="currentColor" font-size="12">닿음 → 누수 아님 (240 bytes 를 g_devs 가 소유)</text>
  <rect x="220" y="150" width="90" height="34" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="230" y="172" fill="currentColor" font-family="monospace">rec</text>
  <rect x="330" y="150" width="110" height="34" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="340" y="172" fill="currentColor" font-family="monospace">"ack"</text>
  <path d="M160 110 L220 160 M310 167 L330 167" stroke="currentColor"/>
  <rect x="520" y="150" width="200" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="530" y="172" fill="currentColor" font-family="monospace">"rx crc-retry" [32]</text>
  <path d="M440 200 L520 175" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="330" y="214" fill="currentColor" font-size="12">r-&gt;text 를 덮어쓴 순간 끊김 (점선 = 예전 포인터)</text>
  <text x="520" y="200" fill="currentColor" font-size="12">아무도 안 가리킴 → ROOT LEAK</text>
  <text x="10" y="266" fill="currentColor" font-size="12">"닿음" 은 "다시 쓸 것" 이 아니다 → 닿지만 잊힌 메모리(cache)는 leaks · LSan 이 원리상 못 본다</text>
</svg>
```

실제 리포트 (`leaks_leak.txt`, Binary Images 목록과 프로세스 머리말은 잘라냄):

```text
$ MallocStackLogging=1 leaks --atExit -- ./ev_g leak
leaks(30323) MallocStackLogging: recording malloc (and VM allocation) stacks using lite mode
leak: recs=3 texts=ack,rx late,rx

leaks Report Version: 4.0, multi-line stacks
Process 30331: 194 nodes malloced for 10 KB
Process 30331: 3 leaks for 64 total leaked bytes.

STACK OF 1 INSTANCE OF 'ROOT LEAK: <malloc in str_dup>':
6   dyld                                  0x18753e054 start + 6652
5   ev_g                                  0x104c80664 main + 284  starter.c:227
4   ev_g                                  0x104c809f4 case_leak + 104  starter.c:194
3   ev_g                                  0x104c811d4 evlog_annotate + 56  starter.c:108
2   ev_g                                  0x104c8101c rec_set_text + 28  starter.c:85
1   ev_g                                  0x104c81058 str_dup + 40  starter.c:39
0   libsystem_malloc.dylib                0x188f028e0 _malloc_zone_malloc_instrumented_or_legacy + 152
====
    1 (32 bytes) ROOT LEAK: <malloc in str_dup 0x7897028020> [32]  length: 12  "rx crc-retry"
…  (같은 모양으로 "rx" 2개 — evlog_add 경로)
```

| 줄 | 읽는 법 |
|---|---|
| `194 nodes malloced for 10 KB` | 종료 시점에 살아 있는 블록 전체 (libSystem 이 만든 것 포함) |
| `3 leaks for 64 total leaked bytes.` | 표시 안 된 블록 3개. **바이트는 블록 크기** 기준 (요청 13바이트가 `[32]` 블록) |
| `STACK OF 1 INSTANCE OF 'ROOT LEAK: …'` | 같은 할당 스택끼리 묶은 그룹. 스택은 `MallocStackLogging` 이 할당 순간에 기록해 둔 것 |
| `<malloc in str_dup 0x…> [32] length: 12 "rx crc-retry"` | 블록 주소 · 크기 · 내용 추정 (문자열이면 보여 줌) |
| `leaks` 의 종료 코드 | 누수 있으면 **1**, 없으면 0 → `check.sh` 가 그대로 게이트로 쓴다 |

누수 3개 = `annotate` 가 덮어쓴 텍스트 3개 ("rx crc-retry", gw-temp/node-07 의 처음 "rx"). 고치기: 덮어쓰기 전에 `free(r->text);` 한 줄 (`r->text` 가 NULL 인 첫 호출에서도 `free(NULL)` 은 아무 일도 안 한다 — C11 7.22.3.3).

두 가지 디테일:

- frame 3 이 `evlog_add … starter.c:93` 으로 찍힌 그룹이 있다. 실제 호출은 92번 줄(`rec_set_text(r, text);`)이다. 스택의 각 프레임은 **리턴 주소** 라서 호출 명령 바로 다음 명령의 줄이 나올 수 있다 — 같은 할당을 valgrind 는 `evlog_add (starter.c:92)` 로 보여 준다 (리턴 주소에서 1을 빼서 찾기 때문, TOOL-M14 의 주제).
- `ROOT LEAK` 개념은 "사슬" 일 때 중요하다. 연습 4에서 캐시 머리 포인터만 NULL 로 만들면 `10 leaks for 240 total leaked bytes.` 인데 `ROOT LEAK` 은 **1개** (첫 `dev`) 이고 나머지 9개는 그 밑에 매달린다. 고칠 곳은 루트 하나다.

### Part A-2 — MallocStackLogging: 스택은 언제, 얼마나 기록되나

`leaks` 리포트의 스택은 `leaks` 가 만든 것이 아니라 **대상 프로세스의 libmalloc 이 할당할 때 기록해 둔 것** 이다. `man malloc` 기준 `MallocStackLogging=1` 은 `lite` 와 같고(현재 살아 있는 할당의 스택만, 메모리 안), `full` 은 할당·해제 이벤트 전부를 디스크(`/tmp/stack-logs.*`)에 쓴다. `leaks_modes.txt` (실측, 요약 줄 + 첫 ROOT LEAK):

| 실행 | 스택 | 누수 바이트 | 관찰 |
|---|---|---|---|
| `leaks --atExit` (env 없음) | 있음 | 64 | env 없이도 스택이 나온다 → 이 맥의 `--atExit` 는 lite 기록을 기본으로 켜는 것으로 보임 |
| `MallocStackLogging=0` | **없음** (`ROOT LEAK: 0x102d157e0 [16]`) | 48 | 명시적으로 끄면 주소·크기만 |
| `MallocStackLogging=1` / `lite` | 있음 | 64 | `recording … stacks using lite mode` |
| `MallocStackLogging=full` | 있음 | 48 | `stack logs being written to /private/tmp/stack-logs.<pid>.index` |

같은 3개 누수가 64 바이트 / 48 바이트로 다르게 찍힌다: lite 모드에서는 13바이트 요청이 `[32]` 블록, 끄거나 full 이면 `[16]` 블록이다. 할당 경로가 `_malloc_zone_malloc_instrumented_or_legacy` 로 바뀌면서 다른 zone·크기 등급을 쓰는 것으로 보인다 (원인은 확인 권장). 교훈: **누수 "바이트" 는 도구·모드마다 다르다, 개수와 스택을 비교하라.**

full 모드의 진짜 쓸모는 **해제 이력** 이다. ASan 이 없는 빌드에서 "이 주소, 누가 만들고 누가 풀었나" 를 볼 수 있다 (`history_uaf.txt`, 경로·dyld 프레임 정리):

```text
$ MallocStackLogging=full leaks --atExit --fullStackHistory --outputGraph=uaf.memgraph -- ./ev_g uaf
uaf: purged=3 left=2 values=40
Output graph successfully written to 'uaf.memgraph' [0.44 MB]

# 네 번째 rec_new 블록 = node-12 value 3 = purge 로 지워진 꼬리: 0x1011758b0
$ malloc_history uaf.memgraph 0x1011758b0 | grep 'ev_g'
ALLOC 0x1011758b0-0x1011758cf [size=32]:  main  starter.c:226 | case_uaf  starter.c:183 | evlog_add  starter.c:90 | rec_new  starter.c:68
FREE  0x1011758b0-0x1011758cf [size=32]: main  starter.c:226 | case_uaf  starter.c:183 | evlog_purge  starter.c:126 | rec_free  starter.c:81
```

ASan 의 "freed by / previously allocated by" 두 스택과 같은 정보다 (ALLOC 의 `case_uaf starter.c:183` 은 실제 182번 줄 호출 — 위와 같은 리턴 주소 효과). 비용은 Part C-2 표: full 이 lite 보다 느리고 디스크를 쓴다.

### Part A-3 — 닿지만 잊힌 캐시: leaks 는 0, 그래도 240바이트

`dev_cache_clear()` 가 비어 있으면 종료 시점에 캐시 10개 블록(장치 5 × 구조체 + 이름)이 남는다. 그런데 `g_devs` 가 머리를 가리키므로 **모두 닿는다**. `cache_tools.txt` (실측):

```text
$ MallocStackLogging=1 leaks --atExit --outputGraph=cache.memgraph -- ./ev_g cache
cache: devs=5 recs=6
Output graph successfully written to 'cache.memgraph' [0.44 MB]

$ leaks -q cache.memgraph
Process 31424: 195 nodes malloced for 10 KB
Process 31424: 0 leaks for 0 total leaked bytes.

$ malloc_history cache.memgraph -allBySize | grep dev_intern     # 종료 시점에 살아 있는 블록, 할당 스택별
5 calls for 160 bytes: … main  starter.c:228 | case_cache  starter.c:204 | evlog_add  starter.c:89 | dev_intern  starter.c:47
5 calls for 80 bytes: … main  starter.c:228 | case_cache  starter.c:204 | evlog_add  starter.c:89 | dev_intern  starter.c:49 | str_dup  starter.c:39

$ leaks --referenceTree cache.memgraph | grep -A9 g_devs     # 루트(전역) 에서 내려가는 소유 사슬
      10 (240 bytes) ev_g __DATA __bss: 'g_devs' 0x1023a8048 --> <malloc in dev_intern 0x7c790280a0> [32]
      + 8 (192 bytes) <malloc in dev_intern 0x7c79028080> [32]
      + ! 6 (144 bytes) <malloc in dev_intern 0x7c79028060> [32]
      …
```

- `--outputGraph` 를 주면 `leaks` 는 리포트를 찍지 않고 memgraph 파일만 쓴다. 이후 `leaks`, `heap`, `malloc_history` 모두 **이 파일을 프로세스처럼** 분석한다 (프로세스는 이미 끝났어도).
- lite 모드는 "현재 살아 있는 할당" 의 스택을 갖고 있으므로 `-allBySize` 가 곧 "종료 시점의 소유자 목록" 이다. `check.sh` 의 6번 줄은 여기서 `dev_intern` 줄이 없을 것을 요구한다.
- `--referenceTree` 는 "누가 소유하나" 를 루트에서 내려가며 보여 준다: `__bss` 의 `g_devs` → 10 블록 240 바이트.

VM 의 valgrind 는 이것을 요약 줄에 따로 센다 (`valgrind_reachable.txt`): `still reachable: 160 bytes in 10 blocks` (valgrind 는 **요청 바이트**, 24 × 5 + 이름 8 × 5 를 센다 — 맥 블록 크기 240 과 다른 이유). 기본 종료 코드는 0 이고, `--errors-for-leak-kinds=all --error-exitcode=9` 를 주면 `ERROR SUMMARY: 2 errors` + `[exit code 9]` 로 CI 를 실패시킬 수 있다.

왜 이게 버그인가? 프로세스가 끝나면 OS 가 회수하니 "무해" 해 보이지만, ① 모듈을 다시 초기화하거나(연습 4: 머리만 NULL 로 하면 즉시 진짜 누수), ② 장치 이름이 계속 새로 들어오는 장기 실행 데몬이면 캐시가 끝없이 자란다 — **닿는 누수(logical leak)** 는 장기 실행 시스템에서 메모리가 서서히 느는 흔한 원인이다. 그리고 ③ 종료 시 0 이어야 진짜 누수 검사의 잡음이 없다. 고치기: 캐시를 돌며 `free(d->name); free(d);` 후 `g_devs = NULL;`.

### Part B-1 — 1바이트 오버플로: ASan 은 즉시, Guard Malloc 은 "운에 따라"

ASan (`asan_mac.txt`, 섀도 덤프 생략):

```text
==PID==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000a3b at pc 0x000100899304 bp 0x00016f566a30 sp 0x00016f566a28
WRITE of size 1 at 0x602000000a3b thread T0
    #0 0x000100899300 in rec_to_line starter.c:147
    #1 0x000100898b1c in case_overflow starter.c:171
    #2 0x000100898934 in main starter.c:225
0x602000000a3b is located 0 bytes after 11-byte region [0x602000000a30,0x602000000a3b)
allocated by thread T0 here:
    #0 0x000101049164 in malloc+0x78 (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x41164)
    #1 0x0001008991f0 in rec_to_line starter.c:142
```

`WRITE of size 1`, `0 bytes after 11-byte region` → "11바이트를 받고 12번째에 1바이트 썼다". 리포트 읽는 법은 TOOL-B11 그대로다. (빌드는 `-O1 -fno-inline` — 인라인되면 `#0` 이 `main` 으로 뭉개져 보인다. 처음 `-O1` 만 줬을 때 실제로 `SUMMARY: … starter.c:225 in main` 이 나왔다.)

Guard Malloc 은 재컴파일 없이 `DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib` 로 malloc 을 바꿔 끼운다. 이 맥(macOS 27.2)에 아직 있다: `-rwxr-xr-x 305136 Oct 29 00:44 /usr/lib/libgmalloc.dylib`, 배너 `version 064578.81.1`. 블록마다 가상 메모리 페이지를 따로 주고 **블록 끝을 페이지 끝에 맞춘 뒤 다음 페이지를 접근 금지(가드 페이지)** 로 둔다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Guard Malloc 배치 (기본: 16바이트 정렬, 블록 끝 = 페이지 끝 근처)</text>
  <text x="10" y="50" fill="currentColor" font-family="monospace">malloc(11) "gw-temp=215"</text>
  <rect x="250" y="34" width="210" height="30" class="f-mut" stroke="currentColor"/>
  <text x="258" y="54" fill="currentColor" font-size="12">… 헤더(32B, magic) …</text>
  <rect x="460" y="34" width="110" height="30" class="f-ok" stroke="currentColor"/>
  <text x="470" y="54" fill="currentColor" font-family="monospace">s[0..10]</text>
  <rect x="570" y="34" width="50" height="30" class="f-acc" stroke="currentColor"/>
  <text x="574" y="54" fill="currentColor" font-size="12">여유5</text>
  <rect x="620" y="34" width="110" height="30" class="f-warn" stroke="currentColor"/>
  <text x="630" y="54" fill="currentColor" font-size="12">가드 페이지 ---</text>
  <path d="M575 84 L575 66" stroke="currentColor"/>
  <text x="290" y="98" fill="currentColor" font-size="12">s[11]='\0' 이 여유 칸에 떨어짐 → 아무 일 없음 (놓침)</text>
  <text x="10" y="140" fill="currentColor" font-family="monospace">malloc(16) "node-07-hum=4551"</text>
  <rect x="250" y="124" width="210" height="30" class="f-mut" stroke="currentColor"/>
  <text x="258" y="144" fill="currentColor" font-size="12">… 헤더(32B, magic) …</text>
  <rect x="460" y="124" width="160" height="30" class="f-ok" stroke="currentColor"/>
  <text x="470" y="144" fill="currentColor" font-family="monospace">s[0..15]</text>
  <rect x="620" y="124" width="110" height="30" class="f-warn" stroke="currentColor"/>
  <text x="630" y="144" fill="currentColor" font-size="12">가드 페이지 ---</text>
  <path d="M624 174 L624 156" stroke="currentColor"/>
  <text x="230" y="188" fill="currentColor" font-size="12">s[16] = 가드 페이지 첫 바이트 → EXC_BAD_ACCESS address=0x1282f0000</text>
  <text x="10" y="232" fill="currentColor" font-family="monospace">MALLOC_PROTECT_BEFORE=1</text>
  <rect x="250" y="216" width="110" height="30" class="f-warn" stroke="currentColor"/>
  <text x="258" y="236" fill="currentColor" font-size="12">가드 페이지</text>
  <rect x="360" y="216" width="160" height="30" class="f-ok" stroke="currentColor"/>
  <text x="370" y="236" fill="currentColor" font-family="monospace">s[0..15]</text>
  <rect x="520" y="216" width="210" height="30" class="f-acc" stroke="currentColor"/>
  <text x="528" y="236" fill="currentColor" font-size="12">헤더 magic 0xdeadbeef…</text>
  <text x="250" y="268" fill="currentColor" font-size="12">넘친 '\0' 이 헤더 magic 끝 바이트를 덮음 → free 때 검사</text>
  <text x="250" y="288" fill="currentColor" font-size="12">…be00, not …beef → 그 자리에서 크래시 대신 나중에 abort</text>
</svg>
```

실측 (`gmalloc.txt`):

```text
$ DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib ./ev_g overflow
GuardMalloc[ev_g-32128]: Allocations will be placed on 16 byte boundaries.
GuardMalloc[ev_g-32128]:  - Some buffer overruns may not be noticed.
…
overflow: lines=gw-temp=215,
[exit code 139]

$ MALLOC_PROTECT_BEFORE=1 DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib ./ev_g overflow
overflow: lines=gw-temp=215,node-07-hum=4551,GuardMalloc[ev_g-32151]: free: header for block 0x1200ac000-0x1200ac010 has been trashed by a buffer overrun. Header magic value at 0x1200ac010 is 0xdeadbeefdeadbe00, not 0xdeadbeefdeadbeef.  Try running without MALLOC_PROTECT_BEFORE to catch this error immediately as it happens.
[exit code 134]
```

- 첫 줄 `gw-temp=215,` 는 찍히고 두 번째 줄을 만들다 SIGSEGV(139). 11바이트 블록의 넘침은 놓쳤고 16바이트 블록에서 잡았다 — 그림 그대로. `MALLOC_POINTER_SIZE=1`(8바이트 정렬)로 바꿔도 11 → 16 으로 올림되는 건 같아서 결과가 같았다 (`gmalloc.txt`). 예전 문서에 보이는 `MALLOC_STRICT_SIZE` 는 지금 man page 에 없고, 줘도 배너가 16바이트 그대로였다.
- `PROTECT_BEFORE` 는 가드를 앞에 두는 대신 블록 **뒤에 헤더** 를 둔다. 16바이트 줄의 `'\0'` 이 magic 의 끝 바이트(little endian 이라 메모리상 첫 바이트)를 `00` 으로 덮었고, `free` 때 들켰다.

크래시 지점을 lldb 로 (`demo_gm.lldb`: `env DYLD_INSERT_LIBRARIES=…` → `run` → `bt 4` → `frame variable len a` → `memory region s+len`):

```text
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=1, address=0x1282f0000)
    frame #0: 0x0000000100000dec ev_g`rec_to_line(r=0x000000012829bfe0) at starter.c:147:12
-> 147 	    s[len] = '\0';
(lldb) frame variable len a
(size_t) len = 16
(size_t) a = 11
(lldb) memory region s+len
[0x00000001282f0000-0x0000000128304000) ---
```

`memory region` 의 `---` 가 권한 없는 가드 페이지다. Guard Malloc 의 장점은 **ASan 빌드가 없는 바이너리**(서드파티 라이브러리 포함)에도 걸린다는 것, 단점은 정렬 여유 · 블록마다 페이지라는 비용(Part C-2: 약 112배 느림)이다.

### Part B-2 — 꼬리 별칭 UAF: 일반 malloc 은 조용히 리스트를 잃는다

`purge(10)` 이 `[5, 40, 7, 3]` 에서 5·7·3 을 지우면 마지막 3이 꼬리였다. `prev(40)->next = NULL` 은 했지만 `g_tail` 은 해제된 3 을 계속 가리킨다. 다음 `evlog_add(12)` 가 `g_tail->next = r` 로 **해제된 블록에 8바이트를 쓰고**, 새 레코드는 머리에서 닿지 않는 곳에 붙는다 → 일반 실행 `values=40` (12 실종), `left=2` 와 실제 개수(1)가 어긋난다.

ASan (`asan_mac.txt`):

```text
==PID==ERROR: AddressSanitizer: heap-use-after-free on address 0x603000001090 …
WRITE of size 8 at 0x603000001090 thread T0
    #0 0x0001007910ac in evlog_add starter.c:93
    #1 0x000100790c3c in case_uaf starter.c:184
0x603000001090 is located 0 bytes inside of 32-byte region [0x603000001090,0x6030000010b0)
freed by thread T0 here:
    #1 0x000100791714 in evlog_purge starter.c:125
    #2 0x000100790c24 in case_uaf starter.c:183
previously allocated by thread T0 here:
    #1 0x0001007914ec in rec_new starter.c:68
    #3 0x000100790c20 in case_uaf starter.c:182
```

`0 bytes inside of 32-byte region` + `WRITE of size 8` = 구조체 첫 필드(`next`, 오프셋 0)에 포인터를 썼다. 세 스택이 "182에서 만들고, 183(purge)에서 풀고, 184에서 썼다" 를 말해 준다. Guard Malloc 은 해제된 페이지를 돌려주므로 같은 쓰기에서 `[exit code 139]`.

일반 libmalloc 의 검사 옵션은 어떤가? `malloc_env.txt` 실측 — **전부 조용했다**:

| 실행 (`./ev_g uaf`) | 결과 |
|---|---|
| 그냥 | `uaf: purged=3 left=2 values=40` rc 0 |
| `MallocScribble=1` (해제 블록을 0x55 로) | 같음 — 해제 후 **읽기** 는 드러낼 수 있어도 **쓰기** 는 못 본다 |
| `MallocCheckZeroOnFreeCorruption=1` | 같음 (man page: 해제 때 0으로 채운 블록이 재할당 때 0이 아니면 abort — 이 실행에선 감지 안 됨) |
| `MallocCheckHeapStart=1 MallocCheckHeapEach=1 MallocCheckHeapAbort=1` | 같음 |

`./ev_g overflow` 에도 `MallocCheckHeap*`, `MallocGuardEdges=1`(큰 블록에만 가드 페이지) 은 아무것도 못 찾았다. 이 버전의 libmalloc 은 블록 사이에 인라인 메타데이터를 두지 않는 구조로 보인다(크래시 스택의 `mfm_alloc` / referenceTree 의 `initial_xzone_zone` 이름으로 추정 — 확인 권장) — 그래서 "작은 넘침이 다음 블록 헤더를 깨서 free 가 죽는" 고전적 증상조차 안 나온다. **일반 malloc 이 조용하다 ≠ 버그 없음.**

반대로 일반 malloc 이 죽는 경우도 있다. 연습 1 의 BUGGY `grow` 를 lldb 로 돌리면 (`plain_crash.txt`):

```text
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=2, address=0x1106206b08)
  * frame #0: 0x0000000188ee3158 libsystem_malloc.dylib`mfm_alloc + 508
    frame #1: 0x0000000100000ab4 kvb_g`str_ndup(s="can=500k;eth=1G", n=8) at practice.c:21:15
```

버그(해제된 배열에 `memmove`)는 i2c 삽입 때 났는데 크래시는 그다음 `can` 토큰의 `malloc` 안이다 — **크래시 위치 ≠ 버그 위치**. 이럴 때 ASan / Guard Malloc 으로 다시 돌리면 멈추는 곳이 버그 자리로 당겨진다.

### Part B-3 — 초기화 안 된 필드: "값만 바뀌는" 도구 vs "보고하는" 도구

`rec_new` 가 `retries` 를 안 채운다. 새 페이지는 0 이라서 일반 실행은 `retries=3` 으로 맞는다. 채우기 옵션별 결과 (gw-temp 2회 + node-07 1회 재시도 → `2·fill + 3`, 32비트 unsigned 로 감김):

| 실행 | 결과 | 계산 |
|---|---|---|
| 일반 malloc, `MallocZeroOnFree=0`, `MallocPreScribble=1` | `retries=3` | 0 + 3. `MallocPreScribble` 은 현재 `MallocHelp` 목록에 없고 효과도 없었다 |
| `MallocScribble=1` | `retries=1431655767` | 2·0xAAAAAAAA + 3 = 0x55555557 |
| Guard Malloc `MALLOC_FILL_SPACE=1` | `retries=1431655767` | 2·0xAAAAAAAA + 3 — man page 는 `0x55` 라는데 실제는 `0xaa` |
| ASan (기본) | `retries=2105376127` | 2·0xBEBEBEBE + 3 = 0x7D7D7D7F |
| ASan `max_malloc_fill_size=0` | `retries=3` | 채우기 끔 |

Guard Malloc 줄은 man page(`filled with the value 0x55`)와 다르다. 0x55 였다면 2·0x55555555 + 3 = 2863311533 이 나와야 한다. 새 블록 첫 바이트를 찍는 4줄짜리 확인 프로그램으로 다시 재 보니 `MALLOC_FILL_SPACE=1` + libgmalloc 에서 `first byte=0xaa` (일반 malloc 은 `0x00`, `MallocScribble=1` 도 `0xaa`). 문서와 실제가 다른 사례 — 채움 값에 의존하는 테스트는 쓰지 말고 "0 이 아닌 무엇" 으로만 기대하라.

중요한 점: 이 도구들은 **아무것도 보고하지 않는다**. 값을 쓰레기로 바꿔서 테스트가 틀리게 만들 뿐이다 — 테스트가 그 필드를 안 보면 계속 조용하다. 진짜 "초기화 안 된 값을 썼다" 보고는 값마다 "정의됨" 비트를 추적하는 도구만 한다 (VM):

```text
$ valgrind --leak-check=full --track-origins=yes ./ev_g all        (valgrind_starter.txt 발췌)
==PID== Conditional jump or move depends on uninitialised value(s)
==PID==    at 0x492E790: _itoa_word (_fitoa_word.c:38)
==PID==    by 0x492F92F: printf (printf.c:33)
==PID==    by 0x40016AB: case_uninit (starter.c:217)
==PID==  Uninitialised value was created by a heap allocation
==PID==    at 0x48A5618: malloc (vg_replace_malloc.c:447)
==PID==    by 0x4000D3F: rec_new (starter.c:68)

$ ./ev_msan uninit        (clang -fsanitize=memory -fsanitize-memory-track-origins, msan_vm.txt 발췌)
==PID==WARNING: MemorySanitizer: use-of-uninitialized-value
    #0 0xabdddc98b1bc in case_uninit /tmp/TOOL-M10_work/vm/starter.c:217:5
  Uninitialized value was stored to memory at
    #0 0xabdddc98b378 in evlog_retry /tmp/TOOL-M10_work/vm/starter.c:113:22
  Uninitialized value was created by a heap allocation
    #1 0xabdddc98b734 in rec_new /tmp/TOOL-M10_work/vm/starter.c:68:21
```

- 둘 다 `r->retries++` (113) 나 `total += …` (216) 에서는 **보고하지 않는다**. 정의 안 된 값을 복사·더하는 것은 괜찮고, 그 값이 **분기 · 주소 · 시스템 호출 · (MSan 은) 함수 인자** 에 쓰이는 순간 보고한다 (valgrind 는 printf 안의 분기, MSan 은 printf 에 넘기는 217번 줄). MSan 이 인자 전달에서 바로 잡는 것은 clang 16 부터 기본인 eager 검사(`-fsanitize-memory-param-retval`)로 알고 있다 (확인 권장).
- `--track-origins=yes` / `-fsanitize-memory-track-origins` 가 "어디서 생겼나(rec_new:68)" 를 붙여 준다. 이것 없이는 "printf 에서 썼다" 만 나온다.
- 맥에서는 `cc -fsanitize=memory` 가 `clang: error: unsupported option '-fsanitize=memory' for target 'arm64-apple-darwin27.2.0'` (`-fsanitize=leak` 도 같은 오류). → 초기화 문제는 맥에서는 컴파일러 경고(TOOL-B02, 스택 변수만) + `MallocScribble` 테스트, 확실히 하려면 VM.

### Part C-1 — 탐지 매트릭스 (실제 실행, `matrix.txt`)

`demo_matrix.sh` 가 starter 를 케이스마다 각 도구로 돌리고, 출력을 분류한다: **ERR** 오류 보고 / **LEAK** 누수 보고 / **REACH** still reachable 로만 / **VAL** 보고 없이 값만 틀림 / **-** 신호 없음.

```text
case      plain     leaks     scribble  gmalloc   asan      | vg_memchk vg_full   lsan      asan+lsan msan
overflow  -         -         -         ERR       ERR       | ERR       ERR       -         ERR       -
uaf       VAL       LEAK      VAL       ERR       ERR       | ERR       ERR       LEAK      ERR       VAL
leak      -         LEAK      -         -         -         | LEAK      LEAK      LEAK      LEAK      -
cache     -         -         -         -         -         | REACH     REACH     -         -         -
uninit    -         -         VAL       -         VAL       | ERR       ERR       -         VAL       ERR
```

| 버그 | 맥에서 잡는 도구 | VM 에서 잡는 도구 | 못 잡는 도구 (이유) |
|---|---|---|---|
| 1바이트 힙 넘침 | ASan ✓, Guard Malloc △ (16바이트 정렬일 때만) | valgrind ✓, ASan ✓ | leaks · LSan · MSan (누수/초기화만 봄) |
| 해제 후 쓰기 | ASan ✓, Guard Malloc ✓, `malloc_history` full (사후 재구성) | valgrind ✓, ASan ✓ | MallocScribble/CheckHeap (쓰기는 안 봄), MSan |
| 잃어버린 포인터 | leaks ✓ | valgrind ✓, LSan ✓, ASan(+LSan) ✓ | 맥 ASan (LSan 없음), Guard Malloc |
| 닿지만 잊힌 캐시 | `malloc_history` / `--referenceTree` (사람이 판단) | valgrind still reachable ✓ | leaks · LSan (원리상 "닿음" = 누수 아님) |
| 초기화 안 된 읽기 | (보고 없음) Scribble/ASan 은 값만 바꿈 | valgrind ✓, MSan ✓ | leaks · LSan · Guard Malloc |

`uaf` 행의 `leaks`/`lsan` 이 LEAK 인 것은 UAF 의 **2차 효과**다: 해제된 꼬리 뒤에 붙은 새 레코드가 머리에서 닿지 않아 종료 때 누수로 보인다 (valgrind 도 `34 (32 direct, 2 indirect) bytes … definitely lost … case_uaf (starter.c:184)`). 누수 리포트가 "이상한 곳" 이면 그 앞의 메모리 오류부터 의심하라.

valgrind 기본 실행(`vg_memchk`)도 요약은 내지만 **종료 코드는 0** 이다 (`valgrind_starter.txt`: `ERROR SUMMARY: 14 errors from 10 contexts` 다음 `[exit code 0]`). CI 라면 `--error-exitcode=N` 필수.

### Part C-2 — 비용 (실측, `cost.txt`)

`demo_cost.c` = 메시지 20만 개를 32~95바이트로 malloc → 채우기 → 체크섬 → 1024칸 링에서 가장 오래된 것 free. 설정마다 3회 중앙값 (같은 머신에서 다른 작업이 함께 돌아 ±30% 잡음):

| 설정 | loop_ms | 느려짐 | maxRSS | 메모리 |
|---|---|---|---|---|
| 맥 일반 malloc | 17.9 | 1.0× | 1.9 MB | 1.0× |
| 맥 `MallocStackLogging=lite` | 46.2 | 2.6× | 2.9 MB | 1.5× |
| 맥 `MallocStackLogging=full` | 67.6 | 3.8× | 3.1 MB | 1.6× (+ 디스크 로그) |
| 맥 `MallocScribble=1` | 20.5 | 1.1× | 1.9 MB | 1.0× |
| 맥 Guard Malloc | 2012.8 | **112.4×** | 20.8 MB | 10.9× |
| 맥 ASan | 46.6 | 2.6× | 39.1 MB | 20.6× |
| 맥 `leaks --atExit` (+분석) | 36.7 | 2.1× | 363.0 MB* | — |
| VM 일반 malloc (gcc) | 11.6 | 1.0× | 1.5 MB | 1.0× |
| VM valgrind memcheck (기본) | 310.6 | 26.8× | 91.7 MB | 61.1× |
| VM valgrind `--leak-check=full --track-origins=yes` | 472.6 | 40.7× | 190.2 MB | 126.8× |
| VM ASan (clang, LSan on) | 58.1 | 5.0× | 28.7 MB | 19.1× |
| VM LSan 단독 | 168.8 | 14.6× | 4.2 MB | 2.8× |
| VM MSan | 26.8 | 2.3× | 4.5 MB | 3.0× |

\* `leaks` 줄의 maxRSS 는 `/usr/bin/time` 이 잰 **leaks 도구 프로세스 자체** (분석기)의 메모리다. 프로세스 전체 시간도 0.02 s → 1.88 s 로, 대부분 종료 시점 분석 시간이다.

읽는 법: ① Guard Malloc 은 malloc 이 잦은 코드에서 압도적으로 느리다 (할당마다 `mmap` 급 작업) — 짧은 단위 테스트용. ② valgrind 는 수십 배 + `--track-origins` 로 더 느려진다. ③ ASan 은 속도는 싸고 메모리(섀도 + 격리 구역)가 비싸다. ④ LSan 단독이 ASan 보다 느리게 나온 것은 이 부하(할당마다 스택 기록)에서의 실측이고, 원인은 확인하지 않았다. 숫자는 이 부하에 대한 것 — 계산 위주 코드라면 Guard Malloc/LSan 비용은 거의 0, ASan/valgrind 는 load/store 비율만큼 든다.

### Part C-3 — CI 와 펌웨어: 무엇을 어디서 돌리나

- **CI 게이트 (맥 러너)**: `check.sh` 의 구조가 그대로 레시피다 — ① `-Werror` 빌드, ② ASan 빌드로 케이스별 실행(`ERROR: AddressSanitizer` 없음 + 결과 줄 일치), ③ 일반 `-g` 빌드를 `leaks --atExit` (종료 코드 1 = 실패), ④ `MallocScribble=1` 로 한 번 더 (초기화 의존성), ⑤ 필요하면 memgraph + `malloc_history` 로 "종료 시 소유자 없음" 확인, ⑥ Guard Malloc 은 짧은 테스트에만. 도구마다 **빌드를 따로** 한다 (ASan 빌드를 leaks/Guard Malloc 아래서 돌리지 않는다 — 둘 다 malloc 을 바꾼다).
- **CI 게이트 (Linux 러너/VM)**: `valgrind --leak-check=full --errors-for-leak-kinds=definite --error-exitcode=9` (reachable 까지 막을지는 팀 정책), ASan+LSan 빌드, MSan 빌드(모든 코드를 MSan 으로 빌드해야 오탐이 없다 — libc 는 인터셉터가 처리).
- **펌웨어 타깃 (Cortex-M)**: 이 도구들은 MCU 에서 안 돈다 (MMU 없음 → 가드 페이지 불가, RAM 수십~수백 KB → 섀도 메모리 불가, valgrind 는 Linux 사용자 공간 전용). 그래서 ① 하드웨어와 무관한 모듈(파서·큐·캐시)은 호스트 단위 테스트로 이 레슨의 도구를 돌리고(TEST-B08), ② 타깃에서는 힙을 아예 안 쓰거나 고정 풀(DS-M02, CPP-M08), 써야 하면 힙 통계(FreeRTOS `xPortGetFreeHeapSize` / `xPortGetMinimumEverFreeHeapSize`, RTOS-M10)를 장시간 테스트에서 추세로 보고(TEST-A06), `-Wl,--wrap=malloc` 래퍼로 할당 지점·개수를 세는 방식을 쓴다 (GNU ld 의 `--wrap` — 확인 권장). Linux 커널 쪽은 KASAN/kmemleak (ELX-A03).
- SSD 컨트롤러 펌웨어 경험과 연결: FTL 메타데이터 캐시처럼 **전역 테이블에 매달린 엔트리** 가 회수되지 않으면 "누수 도구에는 안 보이는데 가용 엔트리가 줄어드는" 버그가 된다 — Part A-3 과 같은 모양이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```text
$ bash check.sh solution
PASS build -Werror -O2 (solution.c)
PASS selftest all (plain malloc, 5 lines match)
PASS overflow/asan  (overflow: lines=gw-temp=215,node-07-hum=4551,node-12=90210)
PASS uaf/asan  (uaf: purged=3 left=2 values=40,12)
PASS leak/leaks  (0 leaks for 0 total leaked bytes.)
PASS cache/malloc_history  (nothing from dev_intern alive at exit; leaks: 0 leaks for 0 total leaked bytes.)
PASS uninit/MallocScribble  (uninit: retries=3)
PASS all/GuardMalloc  (rc=0, 5 lines match)
8/8 passed
```

VM valgrind 로도 깨끗하다 (`valgrind_solution.txt`): `in use at exit: 0 bytes in 0 blocks`, `total heap usage: 76 allocs, 76 frees, 1,238 bytes allocated`, `All heap blocks were freed -- no leaks are possible`, `ERROR SUMMARY: 0 errors`.

| TODO | 고친 줄 | 왜 |
|---|---|---|
| overflow | `malloc(len + 1)` | `"name=value"` 길이 + `'\0'` |
| uaf | `if (g_tail == r) g_tail = prev;` | 해제 전에 그 블록을 가리키는 **모든 별칭**(head, prev->next, tail)을 정리 |
| leak | `free(r->text);` (덮어쓰기 전) | 소유 포인터를 바꾸기 전에 예전 것을 돌려준다 |
| cache | 리스트를 돌며 `free(d->name); free(d);`, `g_devs = NULL;` | 모듈의 종료 함수가 자기 전역 자원을 전부 회수 |
| uninit | `r->retries = 0;` | malloc 은 0 을 약속하지 않는다 (C11 7.22.3.4: 값은 indeterminate). `calloc` 도 답이지만 필드를 명시하는 편이 리뷰하기 쉽다 |

## 7. 자주 하는 실수

1. **`leaks` 가 0 이니 메모리 문제 없다** → 닿는 메모리는 원리상 안 보인다 (cache 케이스: 0 leaks, 실제 240 바이트). 종료 시점 소유자는 `malloc_history -allBySize` / `--referenceTree`, Linux 는 valgrind `still reachable`.
2. **`MallocStackLogging` 없이 leaks 를 붙이고 "스택이 안 나온다"** → 실행 중인 프로세스에 `leaks <pid>` 를 붙일 때는 그 프로세스가 **시작할 때부터** `MallocStackLogging=1` 이어야 스택이 있다. `attach.txt` 실측: env 없이 붙이면 `ROOT LEAK: 0x1028ad150 [48]` (주소만), `MallocStackLogging=1` 로 시작했으면 `ROOT LEAK: <malloc in lose_one …>` + 스택. (`--atExit` 는 스스로 lite 기록을 켜 주는 것으로 보인다 — Part A-2.)
3. **Guard Malloc 이 안 죽었으니 넘침 없다** → 16바이트 정렬 여유에 떨어진 1~15바이트 넘침은 못 본다 (11바이트 블록 실측). 크기가 16의 배수가 아닌 버퍼는 ASan 으로 확인.
4. **ASan 빌드를 leaks / Guard Malloc / valgrind 아래서 돌린다** → 각자 malloc 을 바꿔 끼우므로 결과가 무의미하거나 실행이 안 된다. 도구별로 빌드를 따로.
5. **`MallocScribble` 로 "UAF 검사했다"** → 해제 블록을 0x55 로 채워 해제 후 *읽기* 결과를 바꿀 뿐, *쓰기* 는 아무 신호가 없다 (uaf 케이스 실측: 결과 동일, rc 0). 쓰기는 ASan / Guard Malloc.
6. **valgrind 를 CI 에 넣고 종료 코드만 본다** → 기본 종료 코드는 오류 14개여도 0 이다. `--error-exitcode=N` (+ 누수를 오류로 칠 범위 `--errors-for-leak-kinds`).
7. **해제 후 포인터 하나만 NULL 로** → `free(r); r = NULL;` 은 지역 변수만 고친다. 같은 블록을 가리키는 다른 별칭(`g_tail`, 캐시된 인덱스·포인터, realloc 전 포인터 — 연습 1)이 진짜 범인이다.

## 8. 면접 Q&A

<details><summary>Q1. macOS 의 leaks 는 어떻게 누수를 판정하나요? 오탐/미탐은?</summary>

루트(전역 데이터, 스레드 스택, 레지스터)에서 시작해 포인터처럼 보이는 모든 정렬된 값을 따라가며 닿는 힙 블록을 표시하고, 표시 안 된 블록을 누수로 본다 (보수적 mark). 미탐: ① 전역/살아 있는 구조체에서 닿지만 다시 안 쓰는 메모리(이 레슨의 cache), ② 우연히 포인터처럼 보이는 정수나 스택에 남은 옛 포인터가 블록을 "살려" 놓는 경우 — `attach.txt` 실측: `main` 에서 `p = NULL` 로 지웠는데 `0 leaks`, `leaks --trace=<addr>` 가 `main (in attach) 0x16b44abb0 --> 0x105145150` (main 의 스택 슬롯에 남은 사본)을 보여 줬다. 같은 누수를 다른 함수에서 만들고 스택을 덮으면 `1 leak for 48 total leaked bytes.` 오탐은 드물지만 포인터를 변형해 저장(태그 비트, XOR 리스트, 오프셋만 저장)하면 닿지 않는 것으로 보일 수 있다. `ROOT LEAK` 는 누수 사슬의 머리 — 고칠 곳은 보통 거기 하나다.
</details>

<details><summary>Q2. Apple Silicon 에 valgrind 가 없는데 무엇으로 대신하나요? 무엇을 잃나요?</summary>

누수 = `leaks --atExit` (+ `MallocStackLogging`), 힙 범위/해제 후 사용 = ASan(재컴파일) 또는 Guard Malloc(재컴파일 없이, 느림), 이력 = `MallocStackLogging=full` + `malloc_history`. 잃는 것: **초기화 안 된 값 보고** (MSan 도 맥에 없음 — `unsupported option '-fsanitize=memory'`), 재컴파일 없이 모든 load/store 를 보는 것. 그래서 초기화 검사는 Linux 러너(valgrind/MSan)에서, 맥에서는 `MallocScribble` 로 증상만 끌어낸다. 이 레슨 매트릭스: 맥 도구만으로는 uninit 행이 "VAL" 뿐이다.
</details>

<details><summary>Q3. Guard Malloc 이 1바이트 넘침을 놓쳤습니다. 왜, 어떻게 하죠?</summary>

블록 시작을 16바이트(옵션으로 8바이트)에 정렬하고 **끝을 페이지 끝 쪽에** 두므로, 크기가 16의 배수가 아니면 블록 끝과 가드 페이지 사이에 여유가 생긴다. 11바이트 블록은 5바이트 여유에 `'\0'` 이 떨어져 조용했고, 16바이트 블록은 바로 가드 페이지라 SIGSEGV. 대응: 같은 테스트를 ASan 으로(바이트 단위 섀도), 또는 `MALLOC_PROTECT_BEFORE=1` 로 헤더 magic 손상을 free 시점에 잡기(실측: `0xdeadbeefdeadbe00, not 0xdeadbeefdeadbeef`). 벡터 명령 때문에 정렬을 1바이트로 낮추는 옵션은 현재 man page 에 없다.
</details>

<details><summary>Q4. ASan 은 왜 초기화 안 된 읽기를 못 잡나요? valgrind 와 MSan 은 어떻게 잡나요?</summary>

ASan 섀도는 "이 바이트에 **접근해도 되나**" (주소 가능성)만 기록한다. 할당된 블록 안이면 값이 정의됐든 말든 접근은 합법이다. 대신 새 블록을 `0xbe` 로 채워 증상을 드러낸다 (실측 `retries=2105376127` = 2·0xBEBEBEBE + 3). valgrind memcheck 는 모든 비트에 "정의됨(V bit)" 을 붙여 복사·연산을 따라가고, MSan 은 컴파일러가 같은 정보를 섀도로 전파한다. 둘 다 **값이 분기·주소·시스템 호출(MSan 은 함수 인자까지)에 쓰일 때** 보고한다 — `r->retries++` 에서가 아니라 printf 에서 보고된 이유. origin 추적(`--track-origins`, `-fsanitize-memory-track-origins`)이 "rec_new 의 malloc 에서 생김" 을 알려 준다.
</details>

<details><summary>Q5. "still reachable" 은 누수인가요? CI 에서 막아야 하나요?</summary>

정의상 "종료 시점에 포인터가 남아 있는 메모리" 다. 프로그램 수명 내내 쓰는 전역 테이블이면 무해하지만, 이 레슨의 캐시처럼 **회수 함수가 할 일을 안 한 것** 일 수도 있다. 장기 실행 데몬/펌웨어에서는 오히려 이게 진짜 문제(계속 자라는 캐시)다. 정책 예: 모듈 단위 테스트에서는 init → 사용 → shutdown 후 reachable 0 을 요구(`--errors-for-leak-kinds=all` 또는 이 레슨의 malloc_history 검사), 통합 테스트에서는 definite 만 막고 reachable 은 추세(memgraph 두 장 `heap --diffFrom`, 힙 통계)로 본다.
</details>

<details><summary>Q6. 일반 빌드가 malloc 안(mfm_alloc)에서 죽습니다. 어떻게 접근하나요?</summary>

malloc 내부 크래시는 거의 항상 **앞선 힙 손상의 결과** 다 (연습 1 실측: 버그는 i2c 삽입의 memmove, 크래시는 다음 토큰 `str_ndup` 의 `mfm_alloc`). 크래시 스택을 고치려 하지 말고 ① ASan 빌드로 재실행 → 손상을 일으킨 접근에서 멈춤, ② 재컴파일이 어려우면 Guard Malloc, ③ 둘 다 안 되면 `MallocStackLogging=full` 로 돌린 뒤 문제 주소를 `malloc_history <memgraph|pid> <addr>` 로 할당/해제 이력 확인. double free 는 이 맥 libmalloc 이 `mfm_free` 에서 `brk #0x1` (SIGTRAP, rc 133) 로 즉시 끝낸다 — 메시지는 출력되지 않았다 (실측).
</details>

<details><summary>Q7. 이 도구들의 비용은? 어떤 걸 매 커밋에 돌리나요?</summary>

이 레슨 실측(malloc 이 잦은 부하): ASan 2.6× 시간 / 20× 메모리, leaks lite 기록 2.6× + 종료 분석 수 초, Guard Malloc 112×, valgrind 27×(track-origins 41×), MSan 2.3×. 매 커밋: `-Werror` + ASan(+UBSan) 단위 테스트 + 맥이면 `leaks --atExit`, Linux 면 LSan. 야간/주간: valgrind 전체(+ origins), MSan 빌드, Guard Malloc 짧은 스위트, soak 테스트에서 힙 추세. 이유: 빠른 것으로 대부분을 잡고, 느린 것은 다른 종류(초기화, 재컴파일 안 된 코드)를 위해 덜 자주.
</details>

<details><summary>Q8. Cortex-M 펌웨어에서 메모리 누수/손상은 어떻게 찾나요?</summary>

MCU 에는 MMU·충분한 RAM·OS 가 없어 ASan/valgrind/Guard Malloc 원리가 안 통한다. ① 설계: 런타임 malloc 금지(부팅 때만 할당) 또는 고정 크기 풀(DS-M02) — 누수가 "풀 고갈" 로 바뀌어 셀 수 있다. ② 관측: RTOS 힙 통계(FreeRTOS `xPortGetMinimumEverFreeHeapSize`), 할당 래퍼로 호출 지점별 카운트, 장시간 테스트의 추세(TEST-A06). ③ 손상: MPU 로 스택 가드/널 페이지(BM-A08 계열), 블록 앞뒤 canary, HardFault 덤프(ARM-M12/M14). ④ 그리고 무엇보다 하드웨어 무관 모듈을 **호스트로 빌드해 이 레슨의 도구로** 돌린다 — 이 레슨의 evlog 가 그 예다.
</details>

## 9. 추가 연습

`practice.c` = 설정 문자열 `k=v;k=v;…` 를 key 순으로 정렬된 배열에 넣는 `kvs` 모듈. 기본은 고친 버전, `-DBUGGY` 로 빌드하면 아래 버그 3개가 되살아난다. `check.sh practice` 는 **고친 버전이 모든 도구에서 깨끗한지** 와 **BUGGY 의 각 버그가 맞는 도구에 잡히는지** 를 둘 다 본다.

### 연습 1 — realloc 이 옮긴 배열 (정렬 삽입 위치 포인터)

`-DBUGGY` 의 `kvs_put_owned` 는 삽입 위치를 **포인터** 로 잡은 뒤 배열을 늘린다. ASan 은 무엇을 보고하고, 일반 malloc 은 어디서 죽나?

<details><summary>답</summary>

`uart, spi` 다음 `i2c` 삽입에서 용량 2 → 4 로 `realloc` 이 블록을 옮긴다. `pos` 는 **옛 블록** 안을 가리키므로 `memmove(pos + 1, pos, …)` 가 해제된 메모리를 읽고 쓴다. ASan (`-O1 -fno-inline`): `heap-use-after-free … READ of size 32` `#0 __asan_memmove` `#1 kvs_put_owned practice.c:64`, `freed by … realloc … practice.c:54`. 일반 malloc 은 그 순간 조용하고, 다음 `str_ndup` 의 `mfm_alloc` 에서 `EXC_BAD_ACCESS` (`plain_crash.txt`). 고치기: 위치는 **인덱스** 로 기억하고 늘린 뒤에 `s->items + i` 로 다시 계산 (C++ 의 iterator invalidation 과 같은 문제).
</details>

### 연습 2 — 오류 경로 누수

`kvs_parse` 가 `=` 없는 토큰을 만나면 `bad` 만 세고 넘어간다. BUGGY 를 `leaks --atExit` 로 돌려 보라.

<details><summary>답</summary>

`MallocStackLogging=1 leaks --atExit -- ./kvb_g parse` → `3 leaks for 64 total leaked bytes.` (check.sh 실측), 스택은 `str_ndup ← kvs_parse`. 잘못된 토큰 3개(`noeq`, `=empty`, `also-bad`)를 복사한 `tok` 를 버렸다. 고치기: 오류 경로에서도 `free(tok)`. 함수가 길어지면 `goto cleanup` 한 곳으로 모으는 패턴이 리뷰에 강하다 (C-M15).
</details>

### 연습 3 — 교체 경로의 double free

같은 key 를 다시 넣으면(`mode=rx` → `mode=tx`) BUGGY 는 예전 값을 `free` 하고 포인터를 그대로 둔다. 도구별로 무엇이 보이나?

<details><summary>답</summary>

`kvs_free` 가 같은 값을 또 `free` 한다. ASan: `ERROR: AddressSanitizer: attempting double-free` → `SUMMARY: AddressSanitizer: double-free practice.c:99 in kvs_free`. 일반 malloc: 메시지 없이 `mfm_free.cold.4` 의 `brk #0x1` → SIGTRAP, rc 133. Guard Malloc: `attempted free of pointer 0x14b82bff0 that was not claimed by any registered malloc zone` + abort(134). 고치기: 해제 즉시 새 값으로 교체(`old->val = val`) — "free 한 포인터를 그대로 두는 순간" 이 버그의 시작이다.
</details>

### 연습 4 — 닿는 메모리가 진짜 누수로 바뀌는 순간

starter 의 `evlog_shutdown()` 끝에 "깨끗하게 다시 시작" 하려고 `g_devs = NULL;` 한 줄만 넣으면 `leaks` 결과는?

<details><summary>답</summary>

실측(`/tmp` 에서 sed 로 만든 변형): `10 leaks for 240 total leaked bytes.` — 캐시 전체가 루트에서 끊긴다. 트리는 `10 (240 bytes) ROOT LEAK: <malloc in dev_intern …> [32]` 하나 밑에 나머지 9개(`dev_intern` 4 + `str_dup` 5)가 매달린 모양이다. 교훈: "still reachable" 을 방치하면 재초기화·핫플러그·테스트 반복 같은 **두 번째 사이클** 에서 진짜 누수가 된다. 해법은 머리를 지우는 게 아니라 `dev_cache_clear()` 를 제대로 구현하는 것.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```text
$ bash check.sh practice
PASS build -Werror -O2 (fixed and -DBUGGY)
PASS selftest all (plain malloc, 3 lines match)
PASS fixed all/asan  (clean)
PASS fixed all/leaks  (0 leaks for 0 total leaked bytes.)
PASS fixed all/GuardMalloc  (rc=0)
PASS BUGGY grow/asan  caught: AddressSanitizer: heap-use-after-free practice.c:64 in kvs_put_owned
PASS BUGGY parse/leaks  caught: 3 leaks for 64 total leaked bytes.
PASS BUGGY replace/asan  caught: AddressSanitizer: double-free practice.c:99 in kvs_free
PASS BUGGY replace/plain malloc  (libmalloc kills it: rc=133)
9/9 passed
```

데모 파일 (모두 맥에서 실행, VM 부분은 `limactl shell elinux` 로):

```bash
bash demo_tools.sh    # leaks_leak / leaks_modes / cache_tools / history_uaf / gmalloc / malloc_env / asan_mac / attach / plain_crash .txt
                      #   (attach.txt 는 demo_attach.c 에 leaks <pid> / --trace 를 붙인 결과, demo_gm.lldb 는 Guard Malloc lldb 세션)
bash demo_vm.sh       # valgrind_starter / valgrind_reachable / valgrind_solution / lsan_vm / msan_vm .txt
bash demo_matrix.sh   # matrix.txt (버그 × 도구)
bash demo_cost.sh     # cost.txt (demo_cost.c, 3회 중앙값)
```

## 10. 핵심 정리

- 맥 조합: 누수 = `leaks --atExit` (+`MallocStackLogging=1`, 종료 코드 1 = 누수), 범위·UAF = ASan (또는 재컴파일 없이 Guard Malloc), 이력 = `MallocStackLogging=full` + `malloc_history <memgraph> <addr>`.
- `leaks` = 루트에서의 보수적 mark → **닿으면 누수 아님**. 종료 시점 소유자는 `--outputGraph` + `malloc_history -allBySize` / `--referenceTree`. Linux 는 valgrind `still reachable`.
- Guard Malloc = 블록 끝에 가드 페이지, 16바이트 정렬 여유 안의 넘침은 놓친다 (11바이트 놓침, 16바이트 잡음). 실측 112× 느림.
- `MallocScribble` / ASan 채우기(0xaa / 0xbe)는 초기화 버그의 **증상만** 드러낸다. 보고는 valgrind / MSan 만 (맥에 MSan 없음).
- libmalloc 검사 변수(CheckHeap, ZeroOnFreeCorruption, GuardEdges)는 이 레슨의 작은 넘침/UAF 를 하나도 못 잡았다. 일반 malloc 이 조용하다 ≠ 버그 없음, 크래시 위치 ≠ 버그 위치.
- valgrind 는 기본 종료 코드 0 → CI 는 `--error-exitcode` 필수. 도구마다 빌드 따로 (malloc 교체 충돌).
- 펌웨어: MCU 에서는 못 돈다 → 하드웨어 무관 모듈을 호스트에서 이 도구들로, 타깃은 풀·힙 통계·래퍼·추세.

## 참고자료

- `man leaks`, `man malloc_history`, `man heap`, `man malloc` (ENVIRONMENT: MallocStackLogging, MallocScribble, MallocCheckHeap*, MallocZeroOnFree), `man libgmalloc` — macOS 27.2 에서 확인
- Apple Developer: "Diagnosing memory, thread, and crash issues early" / Xcode Scheme Diagnostics (Malloc Scribble, Guard Malloc, Malloc Stack Logging)
- Valgrind User Manual, Memcheck: https://valgrind.org/docs/manual/mc-manual.html (leak kinds, `--track-origins`, `--errors-for-leak-kinds`)
- MemorySanitizer: https://clang.llvm.org/docs/MemorySanitizer.html · LeakSanitizer: https://clang.llvm.org/docs/LeakSanitizer.html · AddressSanitizer flags: https://github.com/google/sanitizers/wiki/AddressSanitizerFlags (`malloc_fill_byte`, `max_malloc_fill_size`)
- 관련 레슨: TOOL-B11 (ASan 리포트 읽기), TOOL-B02 (경고), TOOL-B10 (lldb), TOOL-M09 (TSan), TOOL-M11 (코어 덤프), TOOL-M14 (리턴 주소와 심볼화), C-M15 (goto cleanup), DS-M02 (고정 풀), OS-M04/M05 (malloc 내부), RTOS-M10 (RTOS 힙), VM-M02 (mark-sweep), ELX-A03 (KASAN/kmemleak), TEST-B08 (sanitizer 호스트 테스트), TEST-A06 (soak · 누수 추세)
