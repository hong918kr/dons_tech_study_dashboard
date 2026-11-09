---
id: OS-M11
time: 90분
updated: 2026-11-08
---
# OS-M11 교착 탐지기: 락 순서 그래프 (mini lockdep)

## 학습 목표

- `pthread_mutex_t` 를 감싼 `ld_mutex_t` 에 **락 클래스**(lock class)를 붙이고, 스레드마다 **보유 락 스택**(`_Thread_local`)을 둔다. 락을 잡을 때마다 "쥔 락 → 새 락" 의존 간선을 전역 그래프에 기록하는 mini lockdep 을 직접 만든다.
- 새 간선 X→Y 가 들어올 때 **Y ⇝ X 경로가 이미 있으면** 사이클 = 잠재적 교착이라는 것을 설명하고, 반복 DFS 로 검사한다. 이미 본 간선은 전역 락 없이 건너뛰게 해서 정상 상태 비용을 재 본다 (실측 1 스레드 33.7 ns/세트, 캐시 없이 전역 락을 매번 잡으면 8 스레드에서 29배 느림).
- 보고에 **두 경로**(방금 생긴 간선 + 사이클을 닫는 기존 사슬)를 간선마다 *처음 기록할 때* 잡은 호출 스택(`backtrace()`)과 함께 찍는다. 두 스레드가 **차례로** 돌아서 실제로는 절대 안 멈추는데도 ABBA 버그를 잡는다는 것 — 이게 lockdep 의 가치다.
- trylock 은 간선을 만들지 않는 이유, 같은 인스턴스 재획득(자기 교착), 같은 클래스 중첩과 subclass 주석, 순서와 다른 해제, 오탐(false positive)을 구분한다.
- 이 맥의 TSan(`detect_deadlocks=1`), Lima VM 의 TSan · helgrind 를 같은 버그에 돌려 보고, Linux lockdep(`CONFIG_PROVE_LOCKING`) 과 무엇이 같고 다른지 말한다.

## 1. 문제

"교착(deadlock)이 일어나기 **전에** 잡을 수 있나요?" — 교착은 두 스레드의 타이밍이 정확히 맞아야만 일어나서, 테스트에서는 거의 안 나오고 현장에서 가끔 멈춘다(펌웨어라면 워치독 리셋 로그만 남는다). Linux 커널은 이 문제를 **lockdep**(runtime locking correctness validator)으로 푼다: 락 순서를 모두 기록해 두고, 서로 반대인 두 순서를 **한 번씩이라도** 본 순간 경고한다. 커널·드라이버 면접에서 "lockdep 경고를 읽어 본 적 있나", "어떻게 동작하나" 가 자주 나오고, SMP RTOS(Zephyr, FreeRTOS SMP)나 큰 펌웨어에서 디버그 빌드용 락 검사기를 직접 만드는 일도 있다.

동시성 가이드 6층의 두 번째 레슨이다. OS-M10 이 식사하는 철학자로 교착을 재현하고 **4가지 조건**(Coffman, Elphick, Shoshani 1971)과 각 조건을 깨는 해법을 정리했다. 그중 "순환 대기(circular wait)를 깬다 = 전역 락 순서를 지킨다" 가 실무의 기본 해법인데, 사람이 그 순서를 늘 지킨다는 보장이 없다. 이 레슨은 **그 순서를 실행 중에 자동으로 검사하는 도구**를 만든다. (OSTEP 32장 §32.3 Deadlock Bugs 의 "lock ordering" 과 "Detect and Recover" 사이에 있는 도구다.)

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">ABBA 를 "차례로" 실행: 실제로는 안 멈추지만 그래프에는 사이클이 생긴다</text>
<text x="10" y="62" fill="currentColor">T1</text>
<rect x="40" y="45" width="70" height="26" class="f-ok" stroke="currentColor"/><text x="48" y="63" fill="currentColor">lock A</text>
<rect x="115" y="45" width="70" height="26" class="f-ok" stroke="currentColor"/><text x="123" y="63" fill="currentColor">lock B</text>
<rect x="190" y="45" width="80" height="26" class="f-mut" stroke="currentColor"/><text x="196" y="63" fill="currentColor">unlock B,A</text>
<text x="40" y="92" fill="currentColor" font-size="12">B 를 잡을 때 A 를 쥐고 있음 → 간선 A→B 기록</text>
<text x="10" y="142" fill="currentColor">T2</text>
<rect x="290" y="125" width="70" height="26" class="f-ok" stroke="currentColor"/><text x="298" y="143" fill="currentColor">lock B</text>
<rect x="365" y="125" width="70" height="26" class="f-warn" stroke="currentColor"/><text x="373" y="143" fill="currentColor">lock A</text>
<rect x="440" y="125" width="80" height="26" class="f-mut" stroke="currentColor"/><text x="446" y="143" fill="currentColor">unlock A,B</text>
<text x="290" y="172" fill="currentColor" font-size="12">새 간선 B→A, 그런데 A⇝B 이미 있음 → 보고</text>
<line x1="40" y1="200" x2="520" y2="200" stroke="currentColor"/>
<text x="480" y="194" fill="currentColor" font-size="12">시간 →</text>
<text x="40" y="216" fill="currentColor" font-size="12">T1 이 끝난 뒤에 T2 시작 (pthread_join) — 두 스레드는 한 번도 겹치지 않음</text>
<circle cx="600" cy="80" r="24" class="f-acc" stroke="currentColor"/><text x="595" y="85" fill="currentColor">A</text>
<circle cx="600" cy="190" r="24" class="f-acc" stroke="currentColor"/><text x="595" y="195" fill="currentColor">B</text>
<path d="M588 103 L588 165" stroke="currentColor" stroke-width="2" fill="none"/>
<path d="M583 158 L588 167 L593 158" stroke="currentColor" fill="none" stroke-width="2"/>
<text x="540" y="140" fill="currentColor" font-size="12">T1</text>
<path d="M612 167 L612 105" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3" fill="none"/>
<path d="M607 112 L612 103 L617 112" stroke="currentColor" fill="none" stroke-width="2"/>
<text x="620" y="140" fill="currentColor" font-size="12">T2 (새 간선)</text>
<text x="545" y="245" fill="currentColor" font-size="12">A→B→A = 순환 대기 가능</text>
<text x="545" y="262" fill="currentColor" font-size="12">타이밍만 맞으면 멈춘다</text>
<text x="10" y="290" fill="currentColor" font-size="12">TSan / helgrind / lockdep 이 모두 이 원리다. 다른 점은 노드가 "인스턴스(주소)" 냐 "클래스" 냐 (5. 풀이)</text>
</svg>
```

| 용어 | 이 레슨에서 | Linux lockdep 에서 |
|---|---|---|
| 락 클래스 (lock class) | `ld_mutex_init(&m, "spi_bus")` 의 이름 문자열. 같은 이름 = 같은 노드 | 정적 `struct lock_class_key` 의 주소. 보통 `mutex_init` 을 부른 **코드 위치**마다 하나 |
| 보유 락 스택 | `_Thread_local ld_held_t t_held[8]` | `task_struct` 의 `held_locks[]` |
| 의존 간선 (dependency) | "X 를 쥔 채 Y 를 잡았다" = X→Y, 처음 본 순간의 호출 스택과 함께 저장 | `lock_list` + 저장된 stack trace |
| 사이클 검사 | 새 간선일 때만 Y ⇝ X 반복 DFS | 새 간선일 때만 BFS (`check_noncircular`) |
| 빠른 길 | `g_known[X][Y]` 플래그 (락 없이 읽음) | held-lock 사슬 해시(chain key) 캐시 |

### Part A — 래퍼, 보유 스택, 그래프, 보고

- **A1** `graph_path(from, to, path)` — 반복(iterative) DFS. `from ⇝ to` 경로가 있으면 노드 열과 길이, 없으면 0. 재귀 금지.
- **A2** `add_edge(held, to, site)` — 이미 아는 간선이면 락 없이 바로 끝. 처음이면 내부 락 `G.lock` 아래에서 `to ⇝ from` 을 먼저 검사하고, 간선과 `backtrace()` 를 저장하고, 경로가 있었으면 사이클 `[from, to, …, from]` 을 `G.last` 에 남기고 `LD_CYCLE`.
- **A3** `ld_lock_nested(m, sub, site)` — 검사가 **진짜 `pthread_mutex_lock` 보다 먼저**: 같은 인스턴스 재획득은 거절(`LD_SELF`), 같은 클래스·subclass 면 `LD_RECURSIVE`, 그 외 쥔 락마다 `add_edge`. 그다음 잠그고 push.
- **A4** `ld_unlock(m)` — 스택 **어디에 있든** 찾아 빼고 푼다. 안 쥔 락이면 `LD_NOT_HELD` (pthread 는 안 건드림).
- 검증: 손으로 만든 그래프의 경로, 순차 ABBA → 보고 1번 + 기존 간선의 스레드·위치·스택, 같은 위반 반복 → 보고 안 늘어남, 3-락 사이클 `C -> A -> B -> C`.

### Part B — 진짜 lockdep 이 고민하는 경계

- **B1** `ld_trylock` — 성공하면 스택에 넣지만 **간선은 안 만든다**. 그 뒤에 잡는 락으로 가는 간선은 정상적으로 생긴다.
- 같은 인스턴스를 두 번 `ld_lock` → 멈추지 않고 `LD_SELF`. 같은 클래스 두 인스턴스(`inode` 둘) → `LD_RECURSIVE`, `ld_lock_nested(&child, 1, …)` 로 주석을 달면 조용. A, B 를 잡고 A 를 먼저 풀고 C → 간선 B→C 만. 안 쥔 락 해제 → `LD_NOT_HELD`.
- 4 스레드 × 2만 번 같은 순서 A→B→C 스트레스: 카운터 정확, 간선 정확히 3개, 보고 0 (TSan 깨끗 = 빠른 길의 원자 플래그가 레이스 없음).
- 데모: `demo_abba.c`(순차 ↔ 동시 진짜 교착), `demo_bench.c`(비용), `demo_tsan.c`(실제 도구 비교).

### Part C — 펌웨어 드라이버: sensor / spi_bus / log

- 세 경로가 모두 "그럴듯하다": `sensor_task` 는 센서 락을 쥐고 SPI 버스를 잡는다, `spi_err_worker` 는 버스를 쥔 채 로그를 남긴다, `log_flush` 는 로그 락을 쥔 채 센서 상태를 읽는다 → **sensor → spi_bus → log → sensor**.
- 세 스레드를 차례로 돌리면 기능 테스트(출력 문자열)는 통과하지만 mini lockdep 은 3-사이클을 보고해야 한다. **C1** `log_flush_fixed` 로 고친 뒤에는 보고 0, 간선 2개, 출력은 같아야 한다.

**같은 순환 대기, 다른 층** — 6층의 모든 레슨은 같은 그래프 문제다:

| 아이템 | 노드 | 간선 "X→Y" 의 뜻 | 언제 검사 | 사이클이면 |
|---|---|---|---|---|
| OS-M10 | 포크(뮤텍스) | 쥔 X, 기다리는 Y (지금) | 진행 없음 감시 | 실제 교착 → 복구 |
| **OS-M11 (여기)** | 락 **클래스** | X 를 쥔 채 Y 를 잡은 적 있음 (과거 전체) | 새 간선이 들어올 때 (온라인) | 잠재적 교착 경고 |
| ALGO-A10 | 락 | 로그 전체의 획득 순서 | 실행 후 한 번에 (오프라인, SCC) | 사이클마다 증거 보고 |
| STOR-M08 | 트랜잭션 | T1 이 T2 의 락을 기다림 (지금) | 막힐 때 / 주기적 | 희생자 abort |
| STOR-M10 | B+tree 래치 | 위→아래 순서만 허용 | 검사 없음 (규약으로 증명) | — |
| SOCX-M05 | NoC 채널 | 채널 c1 을 쥔 패킷이 c2 를 요청 가능 | 설계 시 (CDG) | 라우팅 함수 수정 |
| DCN-M05 | 스위치 버퍼 | PFC PAUSE 가 거꾸로 전파 | 설계 시 + 워치독 | 경로 제한 / 버림 |

OS-M10·STOR-M08 은 **wait-for 그래프**(지금 누가 누구를 기다리나, 인스턴스 단위)이고, 이 레슨·ALGO-A10·SOCX-M05 는 **순서(가능성) 그래프**(언젠가 그렇게 기다릴 수 있나)다. 앞쪽은 실제로 멈춘 뒤 찾고, 뒤쪽은 멈추기 전에 찾는다.

## 2. Problem (English)

> Build a runtime lock-order validator ("mini lockdep") on top of pthreads.
>
> - `typedef struct { pthread_mutex_t m; int cls; } ld_mutex_t;` — `ld_mutex_init(&m, "class_name")` maps the name to a lock class. All instances with the same name are one graph node.
> - `int ld_lock(ld_mutex_t *m, const char *site)`, `int ld_lock_nested(ld_mutex_t *m, int subclass, const char *site)`, `int ld_trylock(...)`, `int ld_unlock(ld_mutex_t *m)`. Keep a per-thread stack of held locks.
> - On every blocking acquire, for every held lock X add the dependency X→Y (Y = the class being acquired) to a global graph protected by an internal lock. When an edge is new, check whether Y can already reach X; if so report a possible circular locking dependency **before** blocking: print the new edge and the existing chain, each with the call stack captured when that edge was first recorded. Return `LD_CYCLE`.
> - Already-known edges must not take the global lock. Trylock must not add a dependency. Re-acquiring a held instance returns `LD_SELF` without blocking; acquiring a second lock of a held class returns `LD_RECURSIVE` unless a different subclass is given. Releasing out of order is allowed; releasing a lock that is not held returns `LD_NOT_HELD`.
>
> Example: T1 locks A then B and exits; afterwards T2 locks B then A. No thread ever blocks, yet T2's `ld_lock(&A)` returns `LD_CYCLE` and the report shows `B -> A -> B`.
>
> Follow-ups: what does it cost per lock/unlock in steady state? Why must the validator's own lock be untracked? Which reports are false positives? How do TSan, helgrind and Linux lockdep differ?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.10 초에 끝나고 exit 1):

```text
-- Part A: classes, graph search, ABBA found without a real deadlock
PASS A.cls != B.cls && I1.cls == I2.cls
FAIL starter.c:299  n == 3 && path[0] == a && path[1] == b && path[2] == c
…
FAIL starter.c:314  t1.rc[0] == LD_OK && t1.rc[1] == LD_OK && t2.rc[0] == LD_OK && t2.rc[1] == LD_CYCLE
FAIL starter.c:315  G.ncycle == 1 && last_cycle_is("B -> A -> B")
…
-- Part C: firmware driver sensor / spi_bus / log
FAIL starter.c:375  G.ncycle == 1 && last_cycle_is("log -> sensor -> spi_bus -> log")
PASS strcmp(g_flush_out, "spi crc error\n| sensor=49") == 0
FAIL starter.c:380  G.ncycle == 0 && G.nedges == 2
PASS strcmp(g_flush_out, "spi crc error\n| sensor=49") == 0
10/33 passed
```

TODO 는 A1–A4, B1, C1. 스텁 `ld_lock`/`ld_unlock` 은 검사 없이 pthread 만 부른다. 뮤텍스가 `PTHREAD_MUTEX_ERRORCHECK` 라서, 스텁이 같은 락을 두 번 잡아도 `EDEADLK` 가 돌아올 뿐 멈추지 않는다. 그리고 모든 시나리오가 스레드를 **차례로** 돌리므로, 시작 코드는 어떤 경우에도 걸리지 않는다. `PASS strcmp(g_flush_out…)` 두 줄은 일부러 넣었다. 버그 판도 기능은 멀쩡하다는 것, 즉 **보통 테스트로는 안 보이는 버그**라는 것을 보여 준다.

## 4. 힌트

<details><summary>힌트 1 — 어느 방향을 검사하나</summary>

새 간선이 X→Y 일 때 사이클이 생기려면 이미 그래프에 Y 에서 X 로 가는 길이 있어야 한다. 그래서 `graph_path(to, from)` 이다. 간선을 넣기 **전에** 검사하면 그래프는 첫 보고 전까지 늘 사이클이 없다(DAG). 그래서 SCC 같은 무거운 분석 없이 도달성 검사 한 번이면 충분하다.
</details>

<details><summary>힌트 2 — 재귀 없는 DFS 에서 경로 얻기</summary>

스택에 "노드" 와 "그 노드에서 다음에 볼 이웃 번호(커서)" 를 같이 둔다. 맨 위 노드의 커서를 다음 이웃으로 옮기며 아직 안 본 이웃을 찾고, 찾으면 push, 없으면 pop. 이렇게 하면 **스택 내용 자체가 from 에서 현재 노드까지의 경로**라서, to 에 닿는 순간 스택을 복사하면 끝이다. 커서는 노드마다 하나라 메모리는 O(V).
</details>

<details><summary>힌트 3 — 빠른 길과 내부 락</summary>

간선은 몇 개 안 되는데 lock 호출은 수백만 번이다. `g_known[X][Y]` 를 원자 변수로 두고 먼저 읽는다. 1 이면 끝이고(전역 락 안 잡음), 0 이면 `G.lock` 을 잡고 `G.eidx` 로 **다시** 확인한다(그사이 다른 스레드가 넣었을 수 있다). 내부 락은 그냥 `pthread_mutex_t` 다. 이것을 `ld_lock` 으로 잡으면 무슨 일이 생길지 생각해 보라.
</details>

<details><summary>힌트 4 — 검사 → 잠금 순서</summary>

`pthread_mutex_lock` 을 먼저 하면, 진짜 교착이 난 경우 보고를 찍기도 전에 영원히 막힌다. 검사·기록을 먼저 하고 잠근다. 같은 인스턴스 재획득은 잠그면 그 자리에서 멈추는 진짜 자기 교착이므로 잠그지 말고 돌려준다.
</details>

<details><summary>힌트 5 — C1 은 순서를 바꾸거나, 겹치지 않게</summary>

`log_flush` 가 로그 락을 쥔 채 센서 락을 잡는 것이 사이클의 마지막 간선이다. 센서 값을 먼저 복사(센서 락 안에서 잠깐)해 두고 풀고, 그다음 로그 락을 잡으면 두 락이 겹치지 않아서 간선 자체가 없다.
</details>

## 5. 풀이

### Part A — 클래스 vs 인스턴스: 왜 이름(클래스)으로 노드를 만드나

인스턴스(락 객체 주소)마다 노드를 만들면 세 가지가 깨진다. ① 객체는 생겼다 없어진다(inode, 소켓, DMA 디스크립터마다 락). 노드 수가 끝없이 늘고, 주소가 재사용되면 다른 객체의 간선이 섞인다. ② **버그가 일반화되지 않는다.** 오늘 실행에서 `uart0→dma3` 과 `dma5→uart1` 를 봤다면 인스턴스 그래프에는 사이클이 없지만, 코드 경로로 보면 "uart 를 쥔 채 dma" 와 "dma 를 쥔 채 uart" 가 둘 다 존재한다. 다음 날 같은 인스턴스끼리 만나면 멈춘다. 클래스로 묶어야 **실행해 본 경로가 조금만 겹쳐도** 사이클이 보인다. ③ 메모리가 정해진다(클래스 수 × 클래스 수). 커널 lockdep 이 클래스 상한을 고정 배열로 잡을 수 있는 이유다.

대가: 같은 클래스 안의 순서(부모 inode → 자식 inode, 주소 순으로 두 계좌 잠그기)는 그래프가 구분하지 못한다. 그래서 "같은 클래스 중첩" 은 무조건 경고하고, 개발자가 **subclass 주석**(`ld_lock_nested(&child, 1)`, 커널의 `mutex_lock_nested(&m, SINGLE_DEPTH_NESTING)`)으로 "이건 의도한 다른 층" 이라고 알려 준다. 이 레슨은 노드 = `cls * LD_MAX_SUB + sub`, 즉 `inode/1` 이 `inode` 와 다른 노드다.

Linux 는 클래스를 이름이 아니라 **정적 키의 주소**로 정한다. `mutex_init()` 매크로가 호출 위치마다 `static struct lock_class_key` 를 하나 만들기 때문에, 보통 "초기화한 코드 줄 = 클래스" 다. 이름은 보고용이다. 같은 초기화 함수가 서로 다른 용도의 락을 만들면 `lockdep_set_class()` 로 키를 갈라 준다(커널 문서 lockdep-design 기준, 세부는 버전마다 다를 수 있음).

반대로 TSan 과 helgrind 는 **인스턴스(주소)** 단위다. 아래 Part B 실측 보고에서 `M0 (0xaaaaac023560)`, `lock at 0x4020018` 처럼 주소가 나온다. 그래서 정적 전역 락 두 개짜리 ABBA 는 잡지만, 실행마다 다른 객체에서 생기는 패턴에는 약하다.

### Part A — 엔진의 흐름

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">ld_lock(&amp;A, site) — 스레드 T 가 이미 B 를 쥐고 있을 때 (t_held = [B])</text>
<rect x="10" y="40" width="200" height="40" rx="4" class="f-mut" stroke="currentColor"/>
<text x="20" y="58" fill="currentColor">① 같은 인스턴스 쥠?</text><text x="20" y="74" fill="currentColor" font-size="12">→ LD_SELF (잠그지 않음)</text>
<rect x="10" y="95" width="200" height="40" rx="4" class="f-mut" stroke="currentColor"/>
<text x="20" y="113" fill="currentColor">② 같은 클래스·subclass?</text><text x="20" y="129" fill="currentColor" font-size="12">→ LD_RECURSIVE 보고</text>
<rect x="10" y="150" width="200" height="40" rx="4" class="f-acc" stroke="currentColor"/>
<text x="20" y="168" fill="currentColor">③ 쥔 락마다 add_edge</text><text x="20" y="184" fill="currentColor" font-size="12">B→A</text>
<rect x="10" y="255" width="200" height="40" rx="4" class="f-ok" stroke="currentColor"/>
<text x="20" y="273" fill="currentColor">④ pthread_mutex_lock</text><text x="20" y="289" fill="currentColor" font-size="12">+ t_held 에 push</text>
<path d="M110 190 L110 253" stroke="currentColor" fill="none" stroke-width="1.5"/>
<path d="M105 246 L110 254 L115 246" stroke="currentColor" fill="none"/>
<path d="M210 170 L258 170" stroke="currentColor" fill="none" stroke-width="1.5"/>
<path d="M251 165 L259 170 L251 175" stroke="currentColor" fill="none"/>
<rect x="260" y="150" width="200" height="40" rx="4" class="f-ok" stroke="currentColor"/>
<text x="270" y="168" fill="currentColor">g_known[B][A] == 1 ?</text><text x="270" y="184" fill="currentColor" font-size="12">예 → 끝 (전역 락 안 잡음)</text>
<path d="M360 190 L360 218" stroke="currentColor" fill="none" stroke-width="1.5"/>
<path d="M355 211 L360 219 L365 211" stroke="currentColor" fill="none"/>
<text x="368" y="210" fill="currentColor" font-size="12">아니오</text>
<rect x="260" y="220" width="460" height="100" rx="4" class="f-warn" stroke="currentColor"/>
<text x="270" y="240" fill="currentColor">G.lock 아래 (추적 안 하는 내부 락):</text>
<text x="270" y="258" fill="currentColor" font-size="12">• G.eidx[B][A] 다시 확인 (그사이 남이 넣었을 수 있음)</text>
<text x="270" y="275" fill="currentColor" font-size="12">• graph_path(A ⇝ B) — 넣기 전에 검사, 반복 DFS</text>
<text x="270" y="292" fill="currentColor" font-size="12">• 간선 저장 + backtrace() + 처음 본 스레드·위치</text>
<text x="270" y="309" fill="currentColor" font-size="12">• 경로가 있었으면 [B, A, …, B] 보고, LD_CYCLE → g_known = 1</text>
<text x="480" y="60" fill="currentColor" font-size="12">t_held: _Thread_local</text>
<text x="480" y="78" fill="currentColor" font-size="12">→ 자기 스택은 락 없이 읽고 쓴다</text>
<text x="480" y="104" fill="currentColor" font-size="12">G (그래프·통계): 모든 스레드 공유</text>
<text x="480" y="122" fill="currentColor" font-size="12">→ G.lock 으로만 바꾼다</text>
</svg>
```

간선을 **쥔 락 전부**에서 새 락으로 넣는다(A, B 를 쥐고 C → A→C, B→C). Linux 는 더 아낀다: 스택 맨 위의 "trylock 이 아닌" 락까지만 간선을 넣고 멈춘다. 아래쪽 락 X 에서 맨 위 락 T 로 가는 간선은 T 를 잡을 때 이미 생겼으므로 X→T→C 로 도달성은 그대로이기 때문이다(전이성). trylock 으로 잡은 락은 들어오는 간선이 없으니 그 아래까지 내려간다. 이 레슨은 brief 대로 "전부" 를 택했다. 간선이 조금 늘 뿐 보고는 같다.

### Part A — 새 간선일 때만, 넣기 전에 검사 (A1, A2)

불변식: **첫 보고가 나오기 전까지 그래프는 DAG 다.** 사이클이 생기는 유일한 순간은 새 간선이 들어올 때이고, 그 간선 X→Y 가 사이클을 만들 필요충분조건은 "Y ⇝ X 경로가 이미 있음" 이다. 그래서 온라인 검사는 간선 하나당 도달성 질의 한 번(O(V+E))이면 된다. 그래프 전체에서 사이클을 모두 찾는 SCC(Tarjan/Kosaraju)는 **이미 사이클이 여러 개 있을 수 있는 로그 전체**를 한 번에 분석할 때 필요하다. 그게 ALGO-A10 의 일이다.

`graph_path` 는 커서식 반복 DFS 다. `st[0..sp-1]` 이 언제나 from 에서 현재 노드까지의 경로이므로, to 를 push 하는 순간 스택이 곧 보고할 경로다. 재귀를 피하는 이유는 커널·펌웨어 스택이 작기 때문이다(커널 스택 16 KB, RTOS 태스크 스택 1–4 KB). 이 레슨은 노드가 64 개뿐이지만, 깊이가 그래프 크기에 비례하는 재귀는 검사기 안에서 쓰지 않는 것이 원칙이다. 이웃을 작은 id 부터 보므로 결과가 결정적이다. 그래서 CHECK 가 `"C -> A -> B -> C"` 같은 정확한 문자열을 볼 수 있다.

Linux 는 DFS 대신 BFS 로 **가장 짧은** 사슬을 찾아 보고한다(`__bfs()`. 2.6.3x 무렵 DFS 에서 바뀐 것으로 기억, 버전 미확인). 경로가 짧을수록 사람이 읽기 쉽다. 추가 연습 1 에서 두 방식이 다른 경로를 내는 그래프를 만든다.

사이클을 발견해도 간선은 저장한다. 그래야 `g_known` 이 1 이 되어 **같은 위반은 한 번만** 보고된다(테스트 `t2b`: 보고 수 1 유지). Linux 는 첫 보고 뒤 lockdep 자체를 끈다(`debug_locks_off()`). 첫 경고 뒤의 그래프는 이미 오염돼 있어서 경고가 쏟아지기 때문이다. 이 레슨은 계속 돌게 두고 대신 중복만 막았다. 테스트 `u4` 에서 사이클이 있는 그래프 위에 A→D→C 를 더하면 보고가 2 개 더 나오는 것(3)이 그 차이를 보여 준다.

### Part A — 내부 락은 추적하지 않는다

`G.lock` 을 `ld_mutex_t` 로 만들면 어떻게 될까. `ld_lock(&X)` → `add_edge` → `ld_lock(&G.lock)` → (X 를 쥐고 있으니) `add_edge(X→G.lock)` → `ld_lock(&G.lock)` … 끝없이 재귀한다. 재귀를 플래그로 끊는다 해도, 이번에는 이미 쥔 `G.lock` 을 다시 잡는 진짜 자기 교착이 된다. 검사기 자신의 락은 **추적 밖의 가장 안쪽(leaf) 락**이어야 하고, 그 락을 쥔 동안에는 다른 추적 대상 락을 절대 잡지 않는다. 이 코드에서 `G.lock` 아래 부르는 것은 `backtrace()`, `backtrace_symbols()`(malloc), `printf`(FILE 락)뿐이다. 모두 추적되지 않는 libc 내부 leaf 락이다.

Linux 는 같은 이유로 내부를 raw arch 스핀락(`lockdep_lock`)으로 보호하고, 태스크별 `lockdep_recursion` 카운터로 재진입을 막는다. 또 lockdep 안에서는 **인터럽트를 끈다.** 펌웨어에서 그대로 통하는 교훈이다. 검사기가 그래프 락을 쥔 순간 같은 코어의 ISR 이 락을 잡으면, ISR 의 검사가 그래프 락을 기다리며 영원히 돈다(C-M14 의 "ISR 은 기다릴 수 없다").

`g_known` 은 `relaxed` 로 충분하다. 빠른 길은 플래그 하나만 보고 그래프 데이터는 읽지 않는다. 1 을 봤다면 그 간선은 이미 `G.lock` 아래에서 저장됐고, 이후의 모든 DFS 는 `G.lock` 을 잡고 돌기 때문에 그 저장을 본다. 0(낡은 값)을 봤다면 느린 길로 가서 락 아래에서 다시 확인한다(이중 확인). 어느 쪽도 틀린 결론이 나지 않는다. TSan 이 solution 스트레스(4 스레드 × 2만)에서 아무것도 보고하지 않은 것은 이 플래그가 `_Atomic` 이기 때문이다. 일반 `bool` 이었다면 data race 다(C11 5.1.2.4).

### Part A — 정상 상태 비용: `demo_bench.c`

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o demo_bench && ./demo_bench
```

기계: Apple M6 (`sysctl`: perflevel0 2 / perflevel1 4 / perflevel2 6 코어), Apple clang version 21.0.0 (clang-2100.0.123.102). 실행 전 `uptime` load averages: 12.09 7.19 4.81, 후 12.80 7.49 4.95. 다른 세션이 같이 돌고 있으므로 **배수만** 믿는다. 실제 출력:

```text
single thread, ns per lock+unlock set (best of 5)
mode        depth=1    depth=3
plain           4.2       11.0
errchk          7.1       20.4
ld              8.9       33.7
ld_nc          10.7       37.8
first time an edge is seen (DFS + backtrace + insert): 56 ns per A,B set  (steady state 23.1 ns, clock 16 ns subtracted)

threads, depth=3, each thread has its OWN 3 locks (classes shared), Msets/s total
threads       plain     errchk         ld      ld_nc
1              90.5       46.6       32.2       26.2
2             164.8       86.2       57.2        8.3
4             314.0      156.9      110.9        6.8
8             449.9      247.4      154.7        5.3
edges in graph: 3, cycles: 0
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">스레드별 락 3개 중첩 잠금, 초당 백만 세트 (실측, load ≈ 12)</text>
<line x1="70" y1="250" x2="730" y2="250" stroke="currentColor"/>
<rect x="80" y="209.8" width="26" height="40.2" class="f-mut" stroke="currentColor"/>
<text x="93" y="205.8" fill="currentColor" font-size="12" text-anchor="middle">90.5</text>
<rect x="110" y="229.3" width="26" height="20.7" class="f-ok" stroke="currentColor"/>
<text x="123" y="225.3" fill="currentColor" font-size="12" text-anchor="middle">46.6</text>
<rect x="140" y="235.7" width="26" height="14.3" class="f-acc" stroke="currentColor"/>
<text x="153" y="231.7" fill="currentColor" font-size="12" text-anchor="middle">32.2</text>
<rect x="170" y="238.4" width="26" height="11.6" class="f-warn" stroke="currentColor"/>
<text x="183" y="234.4" fill="currentColor" font-size="12" text-anchor="middle">26.2</text>
<text x="138" y="268" fill="currentColor" text-anchor="middle">1 스레드</text>
<rect x="240" y="176.8" width="26" height="73.2" class="f-mut" stroke="currentColor"/>
<text x="253" y="172.8" fill="currentColor" font-size="12" text-anchor="middle">164.8</text>
<rect x="270" y="211.7" width="26" height="38.3" class="f-ok" stroke="currentColor"/>
<text x="283" y="207.7" fill="currentColor" font-size="12" text-anchor="middle">86.2</text>
<rect x="300" y="224.6" width="26" height="25.4" class="f-acc" stroke="currentColor"/>
<text x="313" y="220.6" fill="currentColor" font-size="12" text-anchor="middle">57.2</text>
<rect x="330" y="246.3" width="26" height="3.7" class="f-warn" stroke="currentColor"/>
<text x="343" y="242.3" fill="currentColor" font-size="12" text-anchor="middle">8.3</text>
<text x="298" y="268" fill="currentColor" text-anchor="middle">2 스레드</text>
<rect x="400" y="110.4" width="26" height="139.6" class="f-mut" stroke="currentColor"/>
<text x="413" y="106.4" fill="currentColor" font-size="12" text-anchor="middle">314</text>
<rect x="430" y="180.3" width="26" height="69.7" class="f-ok" stroke="currentColor"/>
<text x="443" y="176.3" fill="currentColor" font-size="12" text-anchor="middle">156.9</text>
<rect x="460" y="200.7" width="26" height="49.3" class="f-acc" stroke="currentColor"/>
<text x="473" y="196.7" fill="currentColor" font-size="12" text-anchor="middle">110.9</text>
<rect x="490" y="247.0" width="26" height="3.0" class="f-warn" stroke="currentColor"/>
<text x="503" y="243.0" fill="currentColor" font-size="12" text-anchor="middle">6.8</text>
<text x="458" y="268" fill="currentColor" text-anchor="middle">4 스레드</text>
<rect x="560" y="50.0" width="26" height="200.0" class="f-mut" stroke="currentColor"/>
<text x="573" y="46.0" fill="currentColor" font-size="12" text-anchor="middle">449.9</text>
<rect x="590" y="140.0" width="26" height="110.0" class="f-ok" stroke="currentColor"/>
<text x="603" y="136.0" fill="currentColor" font-size="12" text-anchor="middle">247.4</text>
<rect x="620" y="181.2" width="26" height="68.8" class="f-acc" stroke="currentColor"/>
<text x="633" y="177.2" fill="currentColor" font-size="12" text-anchor="middle">154.7</text>
<rect x="650" y="247.6" width="26" height="2.4" class="f-warn" stroke="currentColor"/>
<text x="663" y="243.6" fill="currentColor" font-size="12" text-anchor="middle">5.3</text>
<text x="618" y="268" fill="currentColor" text-anchor="middle">8 스레드</text>
<rect x="80" y="282" width="12" height="12" class="f-mut" stroke="currentColor"/><text x="97" y="293" fill="currentColor" font-size="12">plain</text>
<rect x="150" y="282" width="12" height="12" class="f-ok" stroke="currentColor"/><text x="167" y="293" fill="currentColor" font-size="12">errorcheck</text>
<rect x="250" y="282" width="12" height="12" class="f-acc" stroke="currentColor"/><text x="267" y="293" fill="currentColor" font-size="12">mini lockdep (캐시)</text>
<rect x="400" y="282" width="12" height="12" class="f-warn" stroke="currentColor"/><text x="417" y="293" fill="currentColor" font-size="12">캐시 없음 (매번 G.lock)</text>
</svg>
```

읽는 법:

| 관찰 (실측) | 해석 |
|---|---|
| 1 스레드 3중 세트 plain 11.0 → ld 33.7 ns (약 3배) | 대부분 ERRORCHECK(20.4)와 보유 스택·`g_known` 읽기. 간선 검사 자체는 몇 ns |
| 새 간선 첫 기록 ≈ 56 − 23 ≈ 33 ns 추가 | 그래프가 작아서 DFS 가 짧고, arm64 macOS 의 `backtrace()` 는 프레임 포인터를 따라가는 것뿐이라 싸다. 한 번만 내는 비용 |
| 8 스레드: ld 154.7 vs ld_nc 5.3 Mset/s (29배) | 캐시가 없으면 **모든 lock 호출이 전역 락 하나에 줄을 선다**. 검사기가 원래 프로그램의 병렬성을 지워 버린다(그러면 레이스·교착 타이밍도 바뀐다) |
| ld 는 스레드가 늘면 늘어남 (32 → 155) | `g_known` 은 읽기만 하므로 모든 코어의 캐시에 공유(S) 상태로 남는다. 쓰기가 없으니 무효화도 없다 |

곁가지 하나. 첫 버전은 스레드별 `job_t` 를 배열에 붙여 뒀더니 plain 8 스레드가 132.8 Mset/s 에 머물렀다. 128 B 정렬을 넣자 449.9 가 됐다. 이웃 스레드의 락과 같은 캐시 라인을 쓰는 false sharing 이었다(7층 주제). 벤치마크의 자료구조도 정렬해야 한다.

### Part A — 보고: 두 경로와 호출 스택 (`demo_abba.c`)

<!-- code: demo_abba.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -g -pthread demo_abba.c -o demo_abba
./demo_abba          # 순차
./demo_abba conc     # 동시 (장벽으로 두 스레드가 첫 락을 쥔 뒤 둘째 락 요청)
```

순차 실행 실제 출력. T1(`uart_start_dma_tx`)이 끝난 **뒤에** T2(`dma_done_callback`)가 시작했다:

```text
==== mini-lockdep: possible circular locking dependency (2 classes) ====
cycle: dma_lock -> uart_lock -> dma_lock
[1] new dependency, recorded just now:
  NEW dma_lock -> uart_lock   thread dma_thread: holding 'dma_lock' (taken at dma_done_callback:dma), acquiring 'uart_lock' at dma_done_callback:uart
      0   v_abba                              0x0000000104648b78 ld_lock + 772
      1   v_abba                              0x0000000104649000 dma_done_callback + 192
      2   v_abba                              0x00000001046494d4 t2 + 44
      3   libsystem_pthread.dylib             0x000000018760fa3c _pthread_start + 116
      4   libsystem_pthread.dylib             0x000000018760ac28 thread_start + 8
[2] existing chain that closes the cycle (each edge's stack = when it was FIRST seen):
  OLD uart_lock -> dma_lock   thread tx_thread: holding 'uart_lock' (taken at uart_start_dma_tx:uart), acquiring 'dma_lock' at uart_start_dma_tx:dma
      0   v_abba                              0x0000000104648b78 ld_lock + 772
      1   v_abba                              0x0000000104648748 uart_start_dma_tx + 192
      2   v_abba                              0x0000000104649464 t1 + 44
      3   libsystem_pthread.dylib             0x000000018760fa3c _pthread_start + 116
      4   libsystem_pthread.dylib             0x000000018760ac28 thread_start + 8
==== reported at edge insert, BEFORE the real pthread_mutex_lock: order bug, not (yet) a hang ====
sequential run finished normally: reports=1, both threads completed
```

- `[2]` 의 스택은 **이미 끝나 버린 스레드**의 것이다. 간선을 처음 기록할 때 `backtrace()` 로 저장해 뒀기 때문에 볼 수 있다. 보고 순간에 찍는 스택만으로는 "반대 순서를 누가 어디서 만들었나" 를 알 수 없다. 이것이 brief 가 말한 "각 간선을 처음 기록할 때의 스택" 이다.
- frame 0 이 `ld_lock` 하나뿐인 것은 `-O2` 에서 `add_edge`·`ld_lock_nested` 가 인라인됐기 때문이다. `static` 함수(`t1`, `t2`)도 이름이 나왔다. 이 맥의 `backtrace_symbols` 는 별도 `-rdynamic` 없이 실행 파일의 심볼을 찾았다(실측). 줄 번호가 필요하면 주소를 `atos -o demo_abba -l <load addr> <addr>` 로 바꾼다. Linux(VM, gcc 15.2, glibc)는 다르다. 같은 demo 를 `-rdynamic` 없이 빌드하면 `./a_nordyn(+0x1fd4) [0xaaee283f1fd4]` 처럼 **이름이 하나도 없고**, `-rdynamic` 을 주면 전역 함수만 `./a_rdyn(dma_done_callback+0x44)` 로 나오고 static 함수(`ld_lock`, `t2`)는 여전히 `+0x…` 였다(실측). glibc 는 동적 심볼 표만 보기 때문이다. 오프셋은 `addr2line -e a_nordyn 0x1fd4` 로 바꾼다.

동시 실행(`conc`)은 4번 돌려 4번 모두 exit 3 이었다. 마지막 실행의 실제 출력 앞뒤:

```text
==== mini-lockdep: possible circular locking dependency (2 classes) ====
cycle: uart_lock -> dma_lock -> uart_lock
[1] new dependency, recorded just now:
  NEW uart_lock -> dma_lock   thread tx_thread: holding 'uart_lock' (taken at uart_start_dma_tx:uart), acquiring 'dma_lock' at uart_start_dma_tx:dma
…
==== reported at edge insert, BEFORE the real pthread_mutex_lock: order bug, not (yet) a hang ====
WATCHDOG: no progress for 1 s, 0/2 threads finished -> real deadlock (tx_thread holds uart_lock waits dma_lock, dma_thread holds dma_lock waits uart_lock)
```

두 스레드가 정말로 멈췄다(exit 3, main 의 `pthread_cond_timedwait` 1 초 감시가 확인하고 `_exit`). 그래도 보고는 **막히기 전에** 찍혔다. 두 번째로 간선을 넣는 스레드가 `G.lock` 아래에서 사이클을 보고, 그다음에 `pthread_mutex_lock` 으로 들어갔기 때문이다. 어느 스레드가 "NEW" 가 되는지는 실행마다 다르다(4번 중 tx_thread 3번, dma_thread 1번 → 사이클 표기의 시작점만 바뀜).

### Part B — trylock, 자기 교착, 같은 클래스, 해제 순서

| 상황 | 이 구현 | 이유 |
|---|---|---|
| A 를 쥔 채 `trylock(B)` 성공 | 스택에 넣음, 간선 A→B **없음** | trylock 은 기다리지 않는다. 순환 대기의 한 고리가 되려면 "기다리는" 화살표가 필요하다. 실패하면 그냥 돌아오고, 호출자는 보통 쥔 것을 놓고 다시 시도한다(OS-M10 의 trylock+backoff 해법) |
| 그 상태에서 `lock(C)` | A→C, B→C 둘 다 생김 | B 는 이제 정말 쥐고 있고, C 는 기다릴 수 있다 |
| 같은 인스턴스 두 번 `ld_lock` | 보고 + 잠그지 않고 `LD_SELF` | 비재귀 뮤텍스에서 이건 가능성이 아니라 **확정된** 교착이다. 검사기가 막아 주지 않으면 그 자리에서 멈춘다 |
| 쥔 락에 `trylock` | `LD_BUSY` (보고 없음) | pthread 가 `EBUSY` 를 돌려줄 뿐 멈추지 않는다(실측 16 = EBUSY) |
| 같은 클래스 다른 인스턴스 (`inode` 둘) | `LD_RECURSIVE` 보고, 잠그기는 함 | 지금은 안 멈춘다. 하지만 클래스 그래프로는 순서를 구분할 수 없다. 다른 스레드가 반대 순서로 잡으면 멈춘다 |
| `ld_lock_nested(&I2, 1, …)` | 노드 `inode/1`, 간선 `inode → inode/1` | 개발자가 "부모 → 자식" 층을 선언. 거꾸로(자식 sub 1 을 쥐고 부모 sub 0)가 나오면 정상적인 사이클 검사가 잡는다 |
| A, B 잡고 **A 먼저** 해제 후 C | 간선 B→C 만 | 스택 중간 삭제를 허용해야 한다. 실제 코드에 흔하다(hand-over-hand, STOR-M10 의 crabbing) |
| 안 쥔 락 해제 | `LD_NOT_HELD`, pthread 안 부름 | 기본 뮤텍스에서 안 쥔 락 해제는 미정의 동작이다. ERRORCHECK 면 `EPERM`(실측 1) |

Linux lockdep 의 trylock 처리도 같은 생각으로 안다. trylock 으로 잡은 락으로 들어오는 의존은 만들지 않고, 그 락을 쥔 뒤 잡는 락으로는 만든다(`held_lock.trylock` 비트. 소스 줄 단위로는 확인 안 함).

### Part B — 오탐(false positive): 보고됐지만 실제로는 못 멈추는 경우

1. **게이트 락**: 모든 A↔B 순서 위반이 공통 바깥 락 G 를 쥔 채 일어나면, 두 스레드가 동시에 안쪽에 있을 수 없으니 교착이 불가능하다. 그래도 그래프에는 A→B→A 가 있다. 추가 연습 5 에서 mini lockdep 이 보고하는 것을 확인한다. 오프라인 분석(ALGO-A10)은 간선에 "그때 쥐고 있던 락 집합" 을 붙여 걸러낼 수 있다. Linux lockdep 이 게이트를 따로 빼 주는지는 확인하지 않았다(빼 주지 않는 것으로 알고 있음).
2. **단일 스레드 초기화**: 부팅 때 한 스레드만 도는 동안 생긴 역순(예: probe 가 B→A, 이후 런타임은 A→B). 동시성이 없던 구간이라 실제로는 안전하다. 그래도 나중에 코드가 바뀌면 진짜가 되므로, 보통은 고친다.
3. **클래스가 너무 넓음**: 같은 이름으로 초기화한 두 용도의 락(예: RX 큐 락과 TX 큐 락을 같은 `queue_init` 이 만듦)은 한 노드라서, 용도 A 의 순서와 용도 B 의 순서가 섞여 가짜 사이클을 만든다. → 클래스를 쪼갠다(`lockdep_set_class`).
4. **같은 클래스, 정해진 규칙의 중첩**: 주소 순으로 두 계좌 잠그기. 규칙이 있어 안전하지만 클래스 그래프는 모른다 → subclass/nest 주석.

반대로 **놓치는 것**(false negative): 실행해 보지 않은 경로, `ld_` 래퍼를 안 거친 락(직접 쓴 `pthread_mutex_t`, 스핀락, 세마포어·condvar 대기 — 조건 변수를 기다리는 것도 일종의 의존이다), 간선 표가 꽉 찬 뒤(`G.full`).

### Part B — 실제 도구와 비교 (`demo_tsan.c`)

<!-- code: demo_tsan.c -->

같은 순차 ABBA 를 mini lockdep 없이 순수 pthread 로 썼다. **이 맥** 실측:

```bash
cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_tsan.c -o demo_tsan
./demo_tsan                                                     # → both threads finished, exit=0
TSAN_OPTIONS=detect_deadlocks=1:second_deadlock_stack=1 ./demo_tsan   # → both threads finished, exit=0
TSAN_OPTIONS=help=1 ./demo_tsan 2>&1 | grep -A1 detect_deadlocks
```

```text
	detect_deadlocks
		- If set, deadlock detection is enabled. (Current Value: false)
```

Apple clang 21 의 TSan 런타임은 이 검사가 기본으로 꺼져 있고, 1 로 켜도 **아무것도 보고하지 않았다**(TOOL-M09 의 측정과 같음). Lima VM `elinux`(Ubuntu aarch64, Ubuntu clang 21.1.8)에서 같은 소스, `TSAN_OPTIONS=second_deadlock_stack=1` 실제 출력(BuildId 생략):

```text
WARNING: ThreadSanitizer: lock-order-inversion (potential deadlock) (pid=8186)
  Cycle in lock order graph: M0 (0xaaaaac023560) => M1 (0xaaaaac023590) => M0

  Mutex M1 acquired here while holding mutex M0 in thread T1:
    #0 pthread_mutex_lock <null> (demo_tsan+0x6ff58)
    #1 uart_start_dma_tx /tmp/os_m11/demo_tsan.c:12:37 (demo_tsan+0xff4b8)
    #2 t1 /tmp/os_m11/demo_tsan.c:19:37 (demo_tsan+0xff5d8)

  Mutex M0 previously acquired by the same thread here:
    #0 pthread_mutex_lock <null> (demo_tsan+0x6ff58)
    #1 uart_start_dma_tx /tmp/os_m11/demo_tsan.c:12:5 (demo_tsan+0xff4a8)
…
  Mutex M0 acquired here while holding mutex M1 in thread T2:
    #1 dma_done_callback /tmp/os_m11/demo_tsan.c:16:36 (demo_tsan+0xff510)
…
SUMMARY: ThreadSanitizer: lock-order-inversion (potential deadlock) (/tmp/os_m11/demo_tsan+0x6ff58) in pthread_mutex_lock
==================
both threads finished
ThreadSanitizer: reported 1 warnings
```

Linux TSan 은 `detect_deadlocks` 를 따로 켜지 않아도(그 플랫폼의 기본값) 보고했고 exit 66 이었다. 구조가 mini lockdep 보고와 같다. 간선마다 "쥔 락을 잡은 곳" 과 "새 락을 잡은 곳" 의 스택이 있다(`second_deadlock_stack=1` 이 앞의 것까지 찍게 한다). 다른 점은 노드가 `M0 (주소)`, 즉 **인스턴스**라는 것이다. 같은 VM 의 valgrind 3.26 helgrind (`gcc -g -O1`, 일부):

```text
==8256== Thread #3: lock order "0x4020018 before 0x4020048" violated
==8256== Observed (incorrect) order is: acquisition of lock at 0x4020048
==8256==    by 0x4000A23: dma_done_callback (demo_tsan.c:16)
==8256==  followed by a later acquisition of lock at 0x4020018
==8256==    by 0x4000A2B: dma_done_callback (demo_tsan.c:16)
==8256== Required order was established by acquisition of lock at 0x4020018
==8256==    by 0x40009C3: uart_start_dma_tx (demo_tsan.c:12)
…
==8256==  Address 0x4020018 is 0 bytes inside data symbol "uart_lock"
```

helgrind 도 주소 단위이고, "Required order was established by …" 가 우리의 `[2] OLD` 간선에 해당한다.

**Linux lockdep** 은 이 VM 커널(7.0.0-38-generic)에서 `# CONFIG_PROVE_LOCKING is not set`, `/proc/lockdep` 없음이라 돌려 보지 못했다(배포판 커널은 보통 끔. 비용이 커서 디버그 커널에서만 켠다). 기억으로 적는 보고 모양: `WARNING: possible circular locking dependency detected` → 지금 잡으려는 락과 쥔 락 → `the existing dependency chain (in reverse order) is:` → 간선마다 `-> #1 (&dev->lock){+.+.}-{3:3}:` 와 저장된 스택 → `Possible unsafe locking scenario:` 의 CPU0/CPU1 표 → `*** DEADLOCK ***`. 관련 설정: `CONFIG_PROVE_LOCKING`(의존 검사), `CONFIG_DEBUG_LOCK_ALLOC`, `CONFIG_LOCK_STAT`. 이 밖에 lockdep 은 우리가 안 한 것도 검사한다: **IRQ 상태**(ISR 에서 쓰는 락을 IRQ 켠 채 잡음 → `inconsistent {IN-HARDIRQ-W} -> {HARDIRQ-ON-W} usage`. 추가 연습 3), 읽기/쓰기 락의 재귀 읽기, `lock held when returning to user space`, `/proc/lockdep_chains` 의 체인 캐시.

| 도구 | 노드 | 이 맥 | 어디서 쓰나 |
|---|---|---|---|
| mini lockdep (이 레슨) | 클래스 (+subclass) | 동작 (실측) | 디버그 빌드, 펌웨어에 이식 가능 |
| TSan `lock-order-inversion` | 인스턴스(주소) | **보고 없음** (`detect_deadlocks=1` 로도) | Linux 사용자 공간 테스트 |
| helgrind | 인스턴스(주소) | valgrind 없음 (VM 에서 실측) | Linux, 아주 느림 |
| Linux lockdep | 클래스 (정적 키) | — (VM 커널도 꺼짐) | 커널 디버그 빌드, syzbot 퍼징 |

### Part C — sensor / spi_bus / log 3-사이클

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">버그 판: 각 간선은 다른 스레드가, 다른 때 만들었다</text>
<circle cx="120" cy="70" r="32" class="f-acc" stroke="currentColor"/><text x="98" y="75" fill="currentColor">sensor</text>
<circle cx="250" cy="200" r="32" class="f-acc" stroke="currentColor"/><text x="226" y="205" fill="currentColor">spi_bus</text>
<circle cx="40" cy="200" r="30" class="f-acc" stroke="currentColor"/><text x="27" y="205" fill="currentColor">log</text>
<path d="M142 94 L226 176" stroke="currentColor" stroke-width="2" fill="none"/>
<path d="M216 172 L227 178 L223 166" stroke="currentColor" fill="none" stroke-width="2"/>
<text x="190" y="120" fill="currentColor" font-size="12">sensor_task</text>
<path d="M218 200 L72 200" stroke="currentColor" stroke-width="2" fill="none"/>
<path d="M80 195 L71 200 L80 205" stroke="currentColor" fill="none" stroke-width="2"/>
<text x="95" y="225" fill="currentColor" font-size="12">spi_err_worker → log_event</text>
<path d="M55 174 L100 98" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3" fill="none"/>
<path d="M92 101 L101 96 L101 107" stroke="currentColor" fill="none" stroke-width="2"/>
<text x="10" y="130" fill="currentColor" font-size="12">log_flush</text>
<text x="10" y="146" fill="currentColor" font-size="12">(닫는 간선)</text>
<text x="10" y="262" fill="currentColor" font-size="12">보고: log -> sensor -> spi_bus -> log</text>
<text x="420" y="20" fill="currentColor" font-weight="bold">고친 판 (C1)</text>
<circle cx="500" cy="70" r="32" class="f-acc" stroke="currentColor"/><text x="478" y="75" fill="currentColor">sensor</text>
<circle cx="630" cy="200" r="32" class="f-acc" stroke="currentColor"/><text x="606" y="205" fill="currentColor">spi_bus</text>
<circle cx="420" cy="200" r="30" class="f-acc" stroke="currentColor"/><text x="407" y="205" fill="currentColor">log</text>
<path d="M522 94 L606 176" stroke="currentColor" stroke-width="2" fill="none"/>
<path d="M596 172 L607 178 L603 166" stroke="currentColor" fill="none" stroke-width="2"/>
<path d="M598 200 L452 200" stroke="currentColor" stroke-width="2" fill="none"/>
<path d="M460 195 L451 200 L460 205" stroke="currentColor" fill="none" stroke-width="2"/>
<text x="420" y="245" fill="currentColor" font-size="12">log_flush: sensor 값만 복사 → 풀기</text>
<text x="420" y="262" fill="currentColor" font-size="12">→ 그다음 log 락. 안 겹침 = 간선 없음</text>
</svg>
```

버그 판을 세 스레드로 **차례로** 돌린 결과: 보고 1번, 사이클 `log -> sensor -> spi_bus -> log`, 그리고 `g_flush_out == "spi crc error\n| sensor=49"`(기능 정상). 세 간선은 각각 다른 스레드가 다른 시점에 만들었고, 마지막 `log_flush` 가 사이클을 닫았다. 사람이 코드 리뷰로 이걸 찾으려면 세 함수 안에서 일어나는 모든 중첩 락을 머릿속에 그려야 한다. `log_event` 처럼 **도우미 함수 안에 숨은 락**이 특히 안 보인다.

고치는 방법 세 가지를 비교하면:

| 방법 | 결과 | 비고 |
|---|---|---|
| ① 스냅샷 후 해제 (C1 정답) | 간선 2개, 사이클 없음 | 로그 락 보유 시간도 짧아진다. 센서 값은 "flush 직전 값" 이면 충분하다는 판단이 필요 |
| ② 순서를 sensor → log 로 (`log_flush` 가 sensor 를 먼저 잡음) | sensor→spi→log, sensor→log: DAG | 문서화된 순서 "sensor < spi_bus < log" 가 생긴다(추가 연습 4 가 그래프에서 뽑아 줌) |
| ③ `spi_err_worker` 가 버스를 놓고 로그 | spi→log 간선이 없어짐 | 오류 처리 중 버스를 다른 태스크가 잡아도 되는지 확인해야 함 |

펌웨어 현실: FreeRTOS·Zephyr 에는 lockdep 같은 그래프 검사기가 기본으로 없다(Zephyr 의 `CONFIG_SPIN_VALIDATE` 는 재귀 획득·남의 해제 같은 스핀락 오용을 잡는 것으로 알고 있음. 순서 그래프는 아님. 미확인). 이 레슨의 엔진은 MCU 디버그 빌드에 그대로 옮길 수 있다. 바꿀 점: 클래스 16 개면 인접 비트 행렬이 32 B, `backtrace()` 대신 `__builtin_return_address(0)` 하나(간선당 4 B), 내부 락은 뮤텍스가 아니라 **인터럽트 금지 구간**(검사기 안에서 ISR 이 끼어들면 안 됨). 보고는 UART 로 찍거나, 패닉 덤프용 RAM 영역(noinit)에 남겼다가 리셋 뒤 읽는다. RTOS-M05 의 우선순위 천장(ceiling)도 정해진 순서를 강제해서 교착을 막지만(OS-M10), 순서를 **검사**해 주지는 않는다. 둘은 보완 관계다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution     # 33/33 passed, exit 0 (8번 반복 실행 모두 같음)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o s_asan && ./s_asan   # 33/33, 보고 0
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o s_tsan && ./s_tsan                                     # 33/33, WARNING 0
```

## 7. 자주 하는 실수

**1. 실제 잠금 뒤에 검사한다.** `pthread_mutex_lock(&m->m); check…` 로 쓰면 진짜 교착이 난 실행에서는 보고 한 줄 없이 멈춘다. → 검사·기록을 먼저, 잠금은 마지막(A3 ④). `demo_abba conc` 에서 보고가 WATCHDOG 보다 먼저 찍히는 것이 그 증거다.

**2. 사이클 검사 방향을 거꾸로 한다.** 새 간선 X→Y 에 대해 `graph_path(X, Y)` 를 보면, 방금 넣으려는 간선 때문에 늘 "경로 있음" 이거나(넣은 뒤 검사), 아무 의미 없는 질문이 된다. → `graph_path(Y, X)` 를, **넣기 전에**.

**3. 내부 락을 래퍼로 만든다 / 내부 락 아래에서 추적 대상 락을 잡는다.** 무한 재귀나 자기 교착이 된다. 내부 락 아래에서 "로그를 남기려고" `ld_lock(&log)` 를 부르는 것도 같은 실수다. → 내부 락은 raw, leaf. 보고 출력은 추적 밖의 `printf` 로.

**4. 빠른 길 플래그를 일반 `bool` 로 둔다.** 동작은 하는 것처럼 보여도 data race(미정의 동작)이고 TSan 이 잡는다. → `_Atomic` + relaxed(이유는 5. 풀이). 반대로 "안전하게" 하려고 매번 전역 락을 잡으면 8 스레드에서 29배 느려진다(실측).

**5. 인스턴스 주소로 노드를 만든다.** 해제된 객체의 주소가 재사용되면 남의 간선을 물려받고, 실행마다 다른 객체에서 생기는 순서 위반은 놓친다. → 클래스. 같은 클래스 중첩은 subclass 로 표시.

**6. trylock 에도 간선을 만든다.** "A 를 쥔 채 trylock(B), 실패하면 놓고 재시도" 라는 정당한 교착 회피 코드(OS-M10)가 거짓 사이클로 보고된다. → trylock 은 스택에만 넣는다.

**7. 해제를 LIFO 로만 허용한다.** `t_held[--t_nheld]` 만 하면 hand-over-hand 잠금에서 스택이 어긋나고, 이후 간선이 엉뚱한 락에서 나온다. → 스택 어디서든 찾아서 지운다(A4, 테스트 "A 먼저 해제 후 C → B→C 만").

## 8. 면접 Q&A

<details><summary>Q1. lockdep 은 어떻게 실제로 일어나지 않은 교착을 찾나요?</summary>

락마다 클래스를 정하고, 스레드마다 지금 쥔 락의 스택을 둔다. Y 를 잡을 때 쥔 X 마다 "X→Y" 의존을 전역 그래프에 기록한다. 이 그래프는 **실행 전체의 과거**를 모은 것이라, 두 스레드가 반대 순서를 서로 다른 때 한 번씩만 보여도 사이클이 생긴다. 새 간선 X→Y 를 넣기 전에 Y ⇝ X 가 있으면 보고한다. 실측: T1 이 끝난 뒤 T2 를 시작해도(겹침 0) 보고가 나왔고, 같은 코드를 동시에 돌리면 3번 모두 진짜로 멈췄다. 꼬리: "그럼 보고가 났다고 꼭 멈추나?" → 아니다. 가능성이다. 게이트 락·단일 스레드 구간 같은 오탐이 있다. 하지만 대부분은 "타이밍만 맞으면 멈춘다" 이므로 고친다.
</details>

<details><summary>Q2. 왜 락 인스턴스가 아니라 클래스로 그래프를 만드나요? 단점은?</summary>

인스턴스는 동적으로 생기고 사라져서 그래프가 끝없이 커지고, 주소가 재사용되면 섞인다. 무엇보다 `uart0→dma3` 과 `dma5→uart1` 처럼 **다른 객체에서 본 같은 코드 경로**를 연결하지 못한다. 클래스(Linux 는 초기화 위치의 정적 키)로 묶으면 일반화되고 메모리도 정해진다. 단점은 같은 클래스 안의 순서를 구분 못 한다는 것이다. 부모→자식 inode, 주소 순 두 계좌 잠금은 "recursive locking" 오탐이 된다. 그래서 `mutex_lock_nested(m, subclass)` 같은 주석이 필요하다. 꼬리: "TSan 은?" → 주소 단위다(VM 실측 `M0 (0xaaaaac023560)`). 정적 전역 락의 ABBA 는 잡지만 동적 객체 패턴에는 약하다.
</details>

<details><summary>Q3. 사이클 검사를 언제, 얼마나 자주 하나요? 비용은?</summary>

간선이 **처음** 생길 때만 한다. 그래프는 첫 보고 전까지 DAG 라서 사이클은 새 간선과 함께만 생기고, 검사는 도달성 질의 한 번(O(V+E))이다. 이미 본 간선은 원자 플래그 하나로 건너뛴다. 실측(load ≈ 12, 배수만): 3중 잠금 한 세트 plain 11.0 ns → mini lockdep 33.7 ns(대부분 ERRORCHECK 뮤텍스와 스택 관리), 새 간선 첫 기록 +≈33 ns. 전역 락을 매번 잡는 판은 8 스레드에서 5.3 vs 154.7 Mset/s(29배). Linux 는 쥔 락 사슬 전체의 해시(chain key)를 캐시해서, 같은 사슬이면 간선을 하나하나 보지도 않는다. 꼬리: "디버그 도구가 느리면 왜 문제인가?" → 타이밍이 바뀌어 원래 버그(레이스)가 숨는다(Heisenbug). 그래서 빠른 길이 중요하다.
</details>

<details><summary>Q4. 검사기 자신의 락은 왜 추적하면 안 되나요?</summary>

추적하면 `ld_lock(X)` → 그래프 락을 `ld_lock` → 다시 간선 기록 → 다시 그래프 락… 재귀가 끝나지 않는다. 플래그로 끊어도 이미 쥔 그래프 락을 다시 잡는 자기 교착이 된다. 그래서 검사기 락은 raw 락이고, 쥔 동안 다른 추적 대상 락을 절대 잡지 않는 leaf 다. Linux 는 raw arch 스핀락 + `lockdep_recursion` 카운터 + 인터럽트 금지로 지킨다. 꼬리: "펌웨어라면?" → 검사기 안에서 인터럽트를 꺼야 한다. 안 끄면 그래프 락을 쥔 순간 ISR 이 들어와 자기 검사에서 그 락을 기다리며 영원히 돈다.
</details>

<details><summary>Q5. trylock 은 왜 의존 간선을 만들지 않나요?</summary>

교착은 순환 **대기**다. trylock 은 기다리지 않고 실패를 돌려주므로 대기 고리의 한 칸이 될 수 없다. 오히려 "쥔 채 trylock, 실패하면 다 놓고 재시도" 는 Coffman 의 hold-and-wait 를 깨는 정당한 회피 기법이다(OS-M10). 여기에 간선을 만들면 오탐이 된다. 다만 trylock 으로 잡은 **뒤**에 다른 락을 blocking 으로 잡으면 그 간선은 정상적으로 만든다. 꼬리: "trylock 을 무한 루프로 돌리면?" → 사실상 blocking 이 되고, 기다림은 사라지지 않고 livelock 위험으로 바뀐다. 검사기는 그걸 못 본다.
</details>

<details><summary>Q6. 교착의 4가지 조건과, lockdep 은 그중 무엇을 보나요?</summary>

Coffman 1971: ① 상호 배제, ② 점유 대기(hold and wait), ③ 비선점(no preemption), ④ 순환 대기. 넷이 모두 있어야 교착이다. 깨는 법: ① 락 없는 설계/원자 연산, ② 필요한 락을 한 번에 다 잡거나 trylock 실패 시 다 놓기, ③ 빼앗기(타임아웃·abort. DB 는 희생자 abort = STOR-M08, 네트워크는 PFC 워치독 = DCN-M05), ④ 전역 순서. 표 전체는 OS-M10. lockdep 은 ④ **순환 대기의 가능성**만 본다. 나머지는 "있다" 고 가정한다(블로킹 뮤텍스이므로). 그래서 lockdep 경고에 대한 정답은 거의 늘 "순서를 하나로 정하고 지켜라" 다. 꼬리: "순서를 문서로만 정하면?" → 지켜지는지 확인할 방법이 필요하다. 순위 검사기(추가 연습 2)나 lockdep 이 그것이다.
</details>

<details><summary>Q7. 런타임 lockdep 과 오프라인 SCC 분석, 정적 순위 검사는 어떻게 다른가요?</summary>

lockdep(이 레슨): 간선이 들어올 때마다 도달성 검사. 그래프를 DAG 로 유지하므로 SCC 가 필요 없고, 첫 위반을 그 자리에서 스택과 함께 잡는다. 오프라인(ALGO-A10): 실행 로그 전체로 그래프를 만들고 Tarjan/Kosaraju 로 강한 연결 요소를 찾는다. 사이클이 여러 개여도 모두 찾고, 게이트 락 필터처럼 무거운 후처리를 할 수 있다. 실행에 부담이 없다. 순위 검사(추가 연습 2): 락마다 숫자를 주고 "쥔 것보다 큰 순위만" 허용한다. 가장 싸고, **반대 순서를 한 번도 안 봐도** 첫 위반에서 잡는다(실측: `log`(30) 다음 `sensor`(10) 를 첫 실행에서 거부). 대신 모든 락에 전역 번호를 미리 매겨야 한다. 꼬리: "펌웨어엔 뭘 넣겠나?" → 락이 수십 개 이하이고 구조가 고정이면 순위 검사(코드 몇 줄, RAM 몇 바이트), 드라이버가 많고 동적이면 클래스 그래프.
</details>

## 9. 추가 연습

### 연습 1 — 가장 짧은 사슬로 보고하기 (BFS)

그래프 A→B→C→D 와 지름길 A→D 에서, 엔진의 DFS(작은 id 먼저)와 BFS 가 A ⇝ D 경로로 무엇을 내나?

<details><summary>답</summary>

DFS 는 B 부터 깊이 들어가서 `A -> B -> C -> D`(4 노드), BFS 는 간선 수가 최소인 `A -> D`(2 노드). 보고는 사람이 읽는 것이라 짧을수록 좋다. Linux lockdep 이 BFS(`__bfs`)로 바꾼 이유로 알고 있다. 둘 다 O(V+E) 이고, BFS 는 큐가 필요하다(노드 수만큼). practice.c `path_bfs`.
</details>

### 연습 2 — 락 순위(rank) 검사기

`sensor=10, spi_bus=20, log=30` 으로 순위를 주고 "쥔 락의 순위보다 큰 것만 잡을 수 있다" 를 검사하라. Part C 의 `log_flush_buggy` 경로를 **혼자** 돌렸을 때 결과는?

<details><summary>답</summary>

`log`(30) 를 쥔 채 `sensor`(10) 를 잡는 순간 위반 1. mini lockdep 은 사이클이 완성될 때까지(세 경로를 모두 볼 때까지) 조용하지만, 순위 검사기는 **한 경로만 봐도** 잡는다. 미리 정한 전체 순서가 있기 때문이다. 대가: 모든 락에 번호를 미리 매겨야 하고, 같은 순위끼리의 중첩은 따로 규칙이 필요하다. 커널의 문서화된 락 순서(OSTEP 32장이 인용한 Linux mm 의 주석처럼)를 기계가 검사하는 판이다. practice.c `rank_lock`.
</details>

### 연습 3 — IRQ 안전성 검사 (펌웨어)

클래스마다 두 비트를 둔다: "ISR 안에서 잡힘", "태스크에서 IRQ 를 켠 채 잡힘". `uart_lock` 을 UART RX ISR 이 잡고, 태스크는 한 번은 IRQ 를 끄고(`irqsave`) 한 번은 켠 채 잡는다. 언제 보고하고 왜 위험한가?

<details><summary>답</summary>

두 비트가 처음 함께 켜지는 순간, 즉 태스크가 IRQ 를 켠 채 잡을 때 보고 1. 위험한 이유: 태스크가 `uart_lock` 을 쥔 동안 같은 코어에 UART 인터럽트가 오면, ISR 이 같은 락을 기다리며 돈다. 그런데 태스크는 ISR 이 끝나야 다시 돌 수 있다 → 한 코어 안의 교착. IRQ 를 끄고 잡은 경우는 안전하다(그동안 ISR 이 못 들어옴). 태스크에서만 쓰는 `stats_lock` 은 문제없다. Linux lockdep 의 `{IN-HARDIRQ-W} -> {HARDIRQ-ON-W}` 검사를 줄인 판이다(OS-M07 의 `spin_lock_irqsave` 순서, C-M14 참고). practice.c `irq_track`.
</details>

### 연습 4 — 그래프에서 "문서용 락 순서" 뽑기

고친 Part C 그래프(sensor→spi_bus, spi_bus→log, sensor→log)를 위상 정렬(Kahn)해서 락 순서 문서를 만들어라. 버그 판의 log→sensor 를 더하면?

<details><summary>답</summary>

`sensor -> spi_bus -> log`. 들어오는 간선이 0 인 노드부터 꺼낸다. 이 순서를 헤더 주석이나 연습 2 의 순위표로 박아 두면 이후에는 순위 검사로 싸게 지킬 수 있다. log→sensor 를 더하면 모든 노드의 진입 차수가 1 이상인 고리가 남아 정렬이 실패한다(-1). 위상 정렬이 성공한다 ⇔ 사이클이 없다. 이게 SOCX-M05 가 "XY 라우팅의 CDG 는 위상 순서가 있으니 교착 없음" 을 보이는 방법과 같다. practice.c `topo_order`.
</details>

### 연습 5 — 게이트 락 오탐

모든 경로가 먼저 G 를 잡는다: 경로 1 = G, A, B / 경로 2 = G, B, A. mini lockdep 은 보고하나? 실제로 멈출 수 있나?

<details><summary>답</summary>

보고한다(사이클 1). 그래프에는 A→B 와 B→A 가 있고, G 로 들어오는 간선은 사이클 판단과 무관하다. 하지만 G 를 쥔 스레드만 A, B 에 닿을 수 있으므로 두 스레드가 동시에 "A 를 쥐고 B 를 기다림 / B 를 쥐고 A 를 기다림" 상태가 될 수 없다. 즉 **오탐**이다. 고치는 쪽: ① 그래도 순서를 하나로 맞춘다(G 가 언젠가 사라지면 진짜 버그가 됨. 가장 흔한 선택), ② 오프라인 분석에서 "사이클의 모든 간선이 공통 락 G 를 쥔 채 생겼으면 제외" 로 거른다(ALGO-A10 Part B). 게이트가 없는 같은 코드는 같은 보고를 낸다. 보고만으로는 둘을 구분할 수 없다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice    # 13/13 passed, exit 0
```

실제 출력 일부:

```text
-- 연습 1: DFS vs BFS path for the report
  dfs: A -> B -> C -> D
PASS nd == 4 && strcmp(s, "A -> B -> C -> D") == 0
  bfs: A -> D
…
-- 연습 3: IRQ-safe vs IRQ-unsafe use of one lock class
PASS irq_reports == 0
  irq-check: 'uart_lock' is taken in ISR AND in task context with IRQs enabled -> ISR can spin/block on it
…
  order: sensor -> spi_bus -> log
…
13/13 passed
```

ASan+UBSan, TSan 빌드에서도 13/13, 보고 0.

## 10. 핵심 정리

- 간선 X→Y = "X 를 쥔 채 Y 를 잡은 적 있음". 모든 스레드·모든 시간의 간선을 그래프 하나에 모은다. 새 간선을 넣기 전에 Y ⇝ X 가 있으면 잠재적 교착 → 보고.
- 그래서 **두 스레드가 겹치지 않아도** 잡는다(순차 ABBA 실측). 동시에 돌리면 진짜로 멈췄는데, 보고는 그 전에 찍혔다(검사 → 잠금 순서).
- 노드는 인스턴스가 아니라 **클래스**: 일반화 + 고정 메모리. 같은 클래스 중첩은 subclass 주석. TSan/helgrind 는 주소 단위다.
- 그래프는 첫 보고 전까지 DAG → 간선 하나당 도달성 질의 한 번. SCC 는 오프라인(ALGO-A10).
- 이미 본 간선은 원자 플래그로 락 없이 통과(relaxed 로 충분). 없으면 8 스레드에서 29배 느림(실측). 검사기 자신의 락은 raw·leaf·추적 안 함. 펌웨어에서는 IRQ 금지까지.
- trylock 은 간선 없음(기다리지 않음). 같은 인스턴스 재획득은 확정 교착이니 거절. 해제는 순서와 달라도 됨.
- 보고에는 각 간선을 **처음 본 순간의 스택**이 있어야 "반대 순서를 누가 만들었나" 를 안다.
- 이 맥 TSan 은 `detect_deadlocks=1` 로도 무보고(실측). Linux TSan·helgrind 는 보고. Linux lockdep 은 `CONFIG_PROVE_LOCKING` 디버그 커널에서만.

## 참고자료

- E. G. Coffman, M. J. Elphick, A. Shoshani, "System Deadlocks", *ACM Computing Surveys* 3(2), 1971 — 4가지 조건.
- OSTEP 32장 "Common Concurrency Problems" §32.3 Deadlock Bugs (lock ordering, Linux mm 의 락 순서 주석 인용, Detect and Recover) — 로컬 PDF 에서 장 번호 확인.
- Linux 커널 문서 `Documentation/locking/lockdep-design.rst` (lock class, state, 단일 깊이 중첩, 성능) — https://docs.kernel.org/locking/lockdep-design.html
- Linux 소스 `kernel/locking/lockdep.c` (`check_noncircular`, `__bfs`, `lookup_chain_cache`) — 함수 이름은 기억 기준.
- ThreadSanitizer flags (`detect_deadlocks`, `second_deadlock_stack`) — https://github.com/google/sanitizers/wiki/ThreadSanitizerFlags
- Valgrind Helgrind manual, "Detected errors: Inconsistent Lock Orderings" — https://valgrind.org/docs/manual/hg-manual.html
- `backtrace(3)` / `backtrace_symbols(3)` — macOS `man 3 backtrace`, `atos(1)`.
- 같은 층: OS-M10 (4조건 표, 철학자), ALGO-A10 (오프라인 SCC), STOR-M08 (wait-for 그래프), STOR-M10 (래치 순서), SOCX-M05 (CDG), DCN-M05 (PFC). 앞 층: OS-B05 (뮤텍스), OS-B06 (조건 변수), OS-M07 (스핀락, irqsave), TOOL-M09 (TSan 보고 읽기), RTOS-M05 (천장 프로토콜), C-M14 (ISR).
