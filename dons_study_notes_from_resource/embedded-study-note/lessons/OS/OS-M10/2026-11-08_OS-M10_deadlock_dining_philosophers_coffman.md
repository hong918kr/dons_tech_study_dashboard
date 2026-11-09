---
id: OS-M10
time: 90분
updated: 2026-11-08
---
# OS-M10 교착 상태: 식사하는 철학자와 4가지 조건

## 학습 목표

- 교착(deadlock)의 **4가지 필요 조건**(Coffman, Elphick, Shoshani 1971)인 상호 배제·점유 대기·비선점·순환 대기를 말하고, 해법마다 **어느 조건을 깨는지**와 그 대가를 표 하나로 정리한다.
- 식사하는 철학자(dining philosophers)를 그대로(왼쪽 → 오른쪽) 구현한 뒤, **강제 스케줄**(모두 왼쪽 포크를 쥔 채 mutex+cond 문에서 기다리게 함)로 교착을 **매번** 재현한다. 워치독은 대기 그래프(wait-for graph)에서 순환을 찾아 `DEADLOCK: P0 holds F0 waits F1, … P4 holds F4 waits F0` 를 찍는다. 그다음 모두에게 포크를 놓게 해서 **멈추지 않고 회복**시킨다.
- 해법 세 가지를 구현하고 비교한다. (1) 전역 락 순서(번호 작은 포크 먼저) (2) trylock + 무작위 상한 지수 백오프(백오프가 고정이면 락스텝 스케줄에서 **라이브락**: 식사 0번) (3) 동시 착석 n−1명(맥에는 `sem_init` 이 없어서 mutex+cond 로 세마포어를 직접 만든다).
- 5명·12명에서 meals/s 와 철학자별 식사 횟수(min/max)를 재고, "교착은 없지만 **공정하지 않은**" 해법을 숫자로 짚는다. 실측에서 전역 순서는 P10 137143끼, P11 4끼였다.
- 펌웨어 쪽 연결: RTOS 태스크의 락 순위(rank) 규칙, ISR 은 뮤텍스를 기다릴 수 없다(C-M14), 우선순위 천장(RTOS-M05)이 교착도 막는 이유, 워치독은 최후의 "선점"이라는 것.

## 1. 문제

"식사하는 철학자를 짜 보세요. 왜 멈추죠? 고치는 방법 세 가지와 각각 무엇을 포기하는지 말해 보세요." 면접에서 교착 질문은 거의 이 순서로 나온다. 펌웨어에서는 같은 버그가 **가끔 멈추는 보드**로 나타난다. 센서 태스크는 `i2c_bus` → `log` 순서로 잡고 로거 태스크는 `log` → `i2c_bus` 순서로 잡으면, 1년에 몇 번 둘이 동시에 걸려 워치독 리셋이 난다. 재현이 안 되는 버그라서 **규칙으로 막고, 구조로 탐지하는** 방법을 알아야 한다.

동시성 가이드 6층의 첫 레슨이다. 뮤텍스·조건 변수(OS-B05, OS-B06), 스핀락(OS-M07), futex(OS-A01), 우선순위 역전·상속·천장(RTOS-M03/M04/M05)은 이미 배웠다. 여기서는 **기다림들이 서로 물려서 고리가 되는 문제**만 다룬다. OSTEP 31.6(Dining Philosophers)과 32장(Common Concurrency Problems)이 같은 내용을 다룬다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" fill="currentColor" font-weight="bold">강제 스케줄 직후: 모두 왼쪽 포크를 쥐고 오른쪽을 기다린다</text>
<circle cx="200" cy="170" r="62" class="f-mut" stroke="currentColor"/>
<text x="178" y="174" fill="currentColor">식탁</text>
<circle cx="200" cy="50" r="20" class="f-warn" stroke="currentColor"/><text x="190" y="55" fill="currentColor">P0</text>
<circle cx="314" cy="133" r="20" class="f-warn" stroke="currentColor"/><text x="304" y="138" fill="currentColor">P1</text>
<circle cx="271" cy="267" r="20" class="f-warn" stroke="currentColor"/><text x="261" y="272" fill="currentColor">P2</text>
<circle cx="129" cy="267" r="20" class="f-warn" stroke="currentColor"/><text x="119" y="272" fill="currentColor">P3</text>
<circle cx="86" cy="133" r="20" class="f-warn" stroke="currentColor"/><text x="76" y="138" fill="currentColor">P4</text>
<rect x="144" y="99" width="24" height="18" class="f-acc" stroke="currentColor"/><text x="146" y="113" fill="currentColor" font-size="12">F0</text>
<rect x="232" y="99" width="24" height="18" class="f-acc" stroke="currentColor"/><text x="234" y="113" fill="currentColor" font-size="12">F1</text>
<rect x="259" y="184" width="24" height="18" class="f-acc" stroke="currentColor"/><text x="261" y="198" fill="currentColor" font-size="12">F2</text>
<rect x="188" y="236" width="24" height="18" class="f-acc" stroke="currentColor"/><text x="190" y="250" fill="currentColor" font-size="12">F3</text>
<rect x="117" y="184" width="24" height="18" class="f-acc" stroke="currentColor"/><text x="119" y="198" fill="currentColor" font-size="12">F4</text>
<line x1="188" y1="66" x2="162" y2="99" stroke="currentColor" stroke-width="3"/>
<line x1="296" y1="142" x2="256" y2="112" stroke="currentColor" stroke-width="3"/>
<line x1="266" y1="247" x2="271" y2="202" stroke="currentColor" stroke-width="3"/>
<line x1="144" y1="254" x2="190" y2="248" stroke="currentColor" stroke-width="3"/>
<line x1="92" y1="152" x2="120" y2="186" stroke="currentColor" stroke-width="3"/>
<line x1="212" y1="66" x2="234" y2="98" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah1)"/>
<line x1="312" y1="153" x2="282" y2="183" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah1)"/>
<line x1="252" y1="268" x2="214" y2="253" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah1)"/>
<line x1="124" y1="247" x2="128" y2="204" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah1)"/>
<line x1="94" y1="114" x2="143" y2="106" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah1)"/>
<line x1="380" y1="40" x2="380" y2="310" stroke="currentColor" stroke-dasharray="2 4"/>
<line x1="400" y1="50" x2="430" y2="50" stroke="currentColor" stroke-width="3"/><text x="440" y="54" fill="currentColor">쥠 (holds): p 의 왼쪽 F[p]</text>
<line x1="400" y1="74" x2="430" y2="74" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah1)"/><text x="440" y="78" fill="currentColor">기다림 (waits): 오른쪽 F[(p+1) % n]</text>
<text x="400" y="114" fill="currentColor" font-weight="bold">대기 그래프 (누가 누구를 기다리나)</text>
<text x="400" y="138" fill="currentColor" font-family="monospace">P0 → P1 → P2 → P3 → P4 → P0</text>
<text x="400" y="160" fill="currentColor">P0 은 F1 을 기다리는데 F1 은 P1 이 쥠</text>
<text x="400" y="178" fill="currentColor">→ P0 이 P1 을 기다린다. 각자 하나만</text>
<text x="400" y="196" fill="currentColor">기다리니 화살표를 따라가면 고리.</text>
<rect x="400" y="210" width="330" height="62" rx="4" class="f-warn" stroke="currentColor"/>
<text x="410" y="230" fill="currentColor" font-family="monospace" font-size="12">DEADLOCK: P0 holds F0 waits F1,</text>
<text x="410" y="246" fill="currentColor" font-family="monospace" font-size="12"> P1 holds F1 waits F2, … ,</text>
<text x="410" y="262" fill="currentColor" font-family="monospace" font-size="12"> P4 holds F4 waits F0   (실제 출력)</text>
<text x="400" y="294" fill="currentColor">네 조건 모두 성립: 상호 배제 · 점유 대기</text>
<text x="400" y="312" fill="currentColor">· 비선점 · 순환 대기 (아래 표)</text>
</svg>
```

**4가지 조건과 각 조건을 깨는 방법** (이 표는 6층 전체의 기준이다. 형제 레슨이 이 표를 참조한다):

| Coffman 조건 | 뜻 | 철학자에서는 | 깨는 방법 | 이 레슨 | 대가 |
|---|---|---|---|---|---|
| 상호 배제 (mutual exclusion) | 자원을 한 번에 한 명만 쓴다 | 포크 한 개 = 한 명 | 공유 가능하게 만든다: 읽기 전용 복사, lock-free 구조(5층), 자원 복제(포크 2n개) | 깨지 않음 | 대부분의 자원(버스, 레지스터 블록, 플래시)은 원래 배타적이다 |
| 점유 대기 (hold-and-wait) | 쥔 채로 다른 것을 기다린다 | 왼쪽 쥐고 오른쪽 대기 | 필요한 것을 **한 번에 전부** 잡거나 하나도 잡지 않는다 (waiter/arbitrator, OSTEP의 전역 `prevention` 락) | 언급만 (§5 C) | 병렬성이 줄고, 필요한 자원을 미리 알아야 한다 |
| 비선점 (no preemption) | 쥔 것을 강제로 빼앗을 수 없다 | 남의 포크를 뺏을 수 없다 | **스스로 내려놓기**(trylock 실패 시 놓고 재시도), 밖에서 빼앗기(타임아웃 + abort, DB 희생자, 워치독) | **해법 2** trylock + 백오프 | 라이브락 위험, 되돌리기(rollback) 비용, 캡슐화가 깨짐 |
| 순환 대기 (circular wait) | 기다림이 고리를 이룬다 | P0→P1→…→P4→P0 | **전역 순서**(자원 계층, 주소 순서), 고리가 생길 수 없게 참가자를 제한 | **해법 1** 순서, **해법 3** n−1명 착석 | 순서를 코드 전체가 지켜야 한다, 공정성은 보장하지 않는다 |

네 조건은 **필요 조건**이다. 넷이 다 있어야 교착이 가능하고, 하나만 깨도 교착은 불가능하다. 반대로 넷이 다 있다고 해서 반드시 교착이 일어나지는 않는다. 그래서 운이 좋으면 몇 년간 안 멈춘다. 이 레슨의 강제 스케줄은 그 "운"을 없애서 매번 재현한다.

### Part A — 교착 재현 · 탐지 · 회복, 해법 1 (전역 순서)

- 하네스가 주어진다. 포크는 mutex+cond 로 만든 잠드는 락이고, 모든 대기는 20 ms 조각 + `halt` 확인으로 끊긴다. 강제 스케줄 문(gate)과 워치독도 들어 있다. 워치독은 식사가 200 ms 동안 0번이면 대기 그래프 스냅샷을 두 번 떠서 같은 순환이 보일 때 `DEADLOCK: …` 을 찍고, `halt` 를 세워 모두 포크를 놓고 끝나게 한다.
- naive(왼쪽 → 오른쪽)를 5명·12명으로 강제 스케줄에서 돌리면 교착이 **매번** 나야 한다. 보고 문자열은 정확히 `P0 holds F0 waits F1, …, P(n−1) holds F(n−1) waits F0` 이어야 한다. 회복 뒤에는 식사 0번, 포크 전부 놓임, 스레드 전부 join 되어야 한다.
- **TODO A1** `fork_order(S_ORDERED, …)` — 두 포크 중 번호가 작은 쪽을 먼저 잡는다. 같은 강제 스케줄에서 교착 0, 전원 50끼, 이웃 동시 식사 0이어야 하고, 12명 × 2만 끼 자유 실행도 통과해야 한다.

### Part B — 해법 2: trylock + 무작위 백오프, 그리고 라이브락

- **TODO B1** `backoff_rounds(rng, attempt, randomized)` — 고정이면 항상 1을 돌려준다. 무작위면 `1 + rng % 2^min(attempt, 6)`(실패할 때마다 창이 두 배, 상한 64)이다.
- **TODO B2** `take_forks_trylock` — 첫 포크는 기다려서 잡고, 둘째는 `fork_try` 로 한 번만 시도한다. 실패하면 첫 포크를 **내려놓고**, `retries` 를 세고, 백오프만큼 잔 뒤 처음부터 다시 한다.
- 라이브락은 하네스의 **락스텝 강제 스케줄**(배리어 3개/판, 맥에는 `pthread_barrier_t` 가 없어서 mutex+cond 로 만든 `bar_t`)로 본다. 고정 백오프면 200판 동안 식사 0번, 시도 1000번이다. 무작위면 전원 ≥ 1끼이고, 같은 seed 로 두 번 돌리면 결과가 같다.

### Part C — 해법 3: 동시 착석 n−1, 펌웨어 순위 락

- **TODO C1** `csem_wait(s, halt, max_ms)` / `csem_post(s)` — mutex+cond 카운팅 세마포어. 맥에서 무명 POSIX 세마포어(`sem_init`)는 지원되지 않는다(OS-B06에서 확인). 자리 n−1개로 강제 스케줄을 돌리면 교착 0, `max_seated == 4` 이어야 한다.
- **TODO C2** `fw_take(task, lock)` — RTOS 락에 순위(rank)를 붙인다. ISR 문맥이면 `FW_E_ISR`, 이미 쥔 락보다 순위가 높지 않으면 `FW_E_ORDER`(재귀도 여기서 걸린다), 남이 쥐었으면 `FW_E_BUSY` 를 돌려준다. **막히기 전에** 순서 위반을 거부하므로, 그 순간 교착이 안 나도 버그가 드러난다.

**같은 순환 대기, 다른 층** — 6층의 나머지 레슨은 이 표의 "순환 대기"를 다른 곳에서 찾는다.

| 아이템 | 무엇이 "자원"이고 무엇이 "기다림"인가 | 그 레슨이 깨는/찾는 방법 |
|---|---|---|
| OS-M10 (이 레슨) | 포크(뮤텍스), 스레드가 쥔 채 기다림 | 전역 순서 · trylock · 입장 제한, 런타임 대기 그래프로 탐지 |
| OS-M11 | 락 **클래스** 사이의 "쥔 채 잡음" 간선 | mini lockdep: 실제로 멈추기 전에, 간선이 새로 생길 때 순환 검사 |
| ALGO-A10 | 로그에서 뽑은 락 순서 그래프 | 오프라인 SCC (Tarjan/Kosaraju)로 순환 전부 보고 |
| STOR-M08 | DB 레코드 락, 트랜잭션 대기 그래프 | 탐지 후 희생자 abort = 비선점을 밖에서 깸, wait-die/wound-wait |
| STOR-M10 | B+tree 노드 래치(탐지기 없음) | 위→아래 순서만, 제자리 업그레이드 금지 = 순서로 예방 |
| SOCX-M05 | NoC 채널(버퍼), 패킷이 채널을 쥔 채 다음 채널 요청 | Dally–Seitz 채널 의존 그래프(CDG) 비순환 = XY 라우팅, dateline VC |
| DCN-M05 | 스위치 무손실 버퍼, PFC PAUSE | 순환 버퍼 의존(CBD) 탐지, up-down 라우팅, PFC 워치독(= 비선점 깨기) |
| RTOS-M05 | 단일 코어 RTOS 뮤텍스 | 우선순위 천장 → 고리가 생길 수 없음 |

## 2. Problem (English)

> Five (or *n* ≤ 16) philosophers sit around a table; fork *F[p]* is on the left of philosopher *p* and *F[(p+1) % n]* on the right. Each philosopher repeatedly thinks, takes two forks, eats, and puts them back.
>
> 1. Implement the naive version (left, then right). Reproduce its deadlock **deterministically** with a forced schedule: every philosopher holds its first fork and waits at a gate built from a mutex and a condition variable (macOS has no `pthread_barrier_t`). Then everybody requests the second fork. A watchdog must **detect** the deadlock without hanging. It looks for a cycle in the wait-for graph, prints `DEADLOCK: P0 holds F0 waits F1, ..., P4 holds F4 waits F0`, and **recovers** (all threads release and exit). macOS also has no `pthread_mutex_timedlock`, so every wait is bounded.
> 2. Fix it three ways and say which Coffman condition each breaks:
>    - `void fork_order(int strat, int p, int n, int *first, int *second)` — global order (lowest-numbered fork first).
>    - `unsigned backoff_rounds(uint32_t *rng, unsigned attempt, bool randomized)` and `bool take_forks_trylock(...)` — try the second fork; on failure put the first one back and back off (bounded exponential, randomized). Show that a *fixed* backoff livelocks under a lock-step schedule.
>    - `bool csem_wait(csem_t *, const atomic_int *halt, int max_ms)`, `void csem_post(csem_t *)` — a counting semaphore from mutex+condvar; at most *n−1* philosophers seated.
> 3. Firmware: `int fw_take(fw_task_t *, fw_lock_t *)` — ranked RTOS locks. Reject taking a lock from ISR context, taking a lock whose rank is not higher than every lock already held (including recursion), and report BUSY instead of blocking in the scripted model.
>
> Follow-ups: benchmark meals/s and per-philosopher fairness (min/max) for 5 and 12 philosophers. Which fix starves someone? How would you detect deadlocks in production? What about ISRs and priority ceilings?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행한 실제 출력 일부다. 1.54초 만에 exit 1로 끝난다. 고치지 않은 해법은 전부 naive 와 같아서, 워치독이 판마다 교착을 잡고 회복시킨다.

```text
== Part A: naive forced schedule (expect DEADLOCK) ==
DEADLOCK: P0 holds F0 waits F1, P1 holds F1 waits F2, P2 holds F2 waits F3, P3 holds F3 waits F4, P4 holds F4 waits F0
PASS r.deadlocked == 1 && r.cycle_len == 5
PASS strcmp(r.report, want) == 0
PASS r.total == 0 && r.forks_free_after == 1 && r.gate_timeout == 0
…
FAIL starter.c:556  f1 == 0 && s1 == 4 && f2 == 0 && s2 == 1 && f3 == 0 && s3 == 11
== Part A: ordered (lowest fork first) ==
DEADLOCK: P0 holds F0 waits F1, P1 holds F1 waits F2, P2 holds F2 waits F3, P3 holds F3 waits F4, P4 holds F4 waits F0
  [ordered n=5 forced] deadlocked=1 meals=0 (min 0 max 0) retries=0 max_seated=0 208 ms
FAIL starter.c:564  r.deadlocked == 0 && all_meals(&r, 5, 50) && r.violations == 0 && r.forks_free_after
…
  lockstep randomized: meals=0 [0 0 0 0 0]
FAIL starter.c:609  bad == 0 && bad2 == 0 && everyone && same
…
  [seated n=5 forced] deadlocked=1 meals=0 (min 0 max 0) retries=0 max_seated=5 208 ms
…
FAIL starter.c:659  fw_take(&sensor, &i2c) == FW_OK && fw_take(&sensor, &cache) == FW_OK && sensor.nheld == 2
…
12/28 passed
```

TODO 는 A1(`fork_order`), B1(`backoff_rounds`), B2(`take_forks_trylock`), C1(`csem_wait`/`csem_post`), C2(`fw_take`) 다섯 개다. 단위 테스트가 실패하면 그 Part 의 자유 실행 스트레스는 건너뛴다(`SKIP`). 그래서 시작 코드는 오래 돌지 않는다. 참고로 같은 시작 코드를 TSan 으로 돌려도 경고는 0개다(실측). 교착은 **데이터 레이스가 아니다**. 포크 상태는 전부 락으로 보호되어 있고, 프로그램은 그냥 "아무도 진전하지 않는" 상태가 될 뿐이다.

## 4. 힌트

<details><summary>힌트 1 — 전역 순서 한 줄</summary>

철학자 p 의 두 포크는 `p` 와 `(p+1) % n` 이다. 둘 중 작은 번호를 `first` 로 둔다. p = n−1 일 때만 순서가 뒤집힌다(`F0` 먼저, `F(n−1)` 나중). Dijkstra 가 처음 낸 "한 명만 왼손잡이" 해법과 똑같다(연습 4에서 확인).
</details>

<details><summary>힌트 2 — trylock 루프의 모양</summary>

`for (;;) { take(first) 기다려서; gate; if (try(second)) return true; put(first); retries++; if (halt) return false; sleep(backoff); }` 이다. 핵심은 **실패했을 때 첫 포크를 놓는 것**이다. 놓지 않고 계속 try 만 하면 쥔 채 기다리는 것과 같아서(점유 대기 + 비선점 그대로) 교착이 바쁜 대기로 바뀔 뿐이다.
</details>

<details><summary>힌트 3 — 왜 무작위여야 하나</summary>

모두가 같은 박자로 움직이면(락스텝), 모두 같이 왼쪽을 잡고 같이 실패하고 같이 놓고 같이 다시 잡는다. 고정 백오프는 이 박자를 그대로 유지한다. 백오프에 난수가 들어가야 누군가 한 판을 쉬고, 그 틈에 왼쪽 이웃이 오른쪽 포크를 얻는다. 창을 실패마다 두 배로 키우는 이유는 경합 인원을 몰라도 스스로 맞추기 위해서다(이더넷 CSMA/CD 와 같은 생각). 상한을 두는 이유는 OS-M07 힌트 3과 같다.
</details>

<details><summary>힌트 4 — mutex+cond 세마포어</summary>

`wait`: 락을 쥐고 `while (count == 0)` 안에서 `halt`/시간 초과를 확인한 뒤 `pthread_cond_timedwait`(20 ms 조각)를 한다. 루프를 빠져나오면 `count--`. `post`: 락을 쥐고 `count++`, `signal`. 반드시 `while` 로 쓴다. spurious wakeup 이 있고, 다른 대기자가 먼저 가져갈 수도 있다(OS-B06).
</details>

<details><summary>힌트 5 — 순위 락의 검사 순서</summary>

① ISR 인가 → ② 이미 쥔 락 중 `rank >= lk->rank` 가 있는가 → ③ 남이 쥐었나. ②를 ③보다 먼저 해야 한다. 그래야 "지금은 마침 비어 있어서 안 막혔지만 순서는 틀린" 코드도 잡힌다. 같은 락을 다시 잡으면 rank 가 같으므로 ②에서 걸린다.
</details>

## 5. 풀이

### Part A — 재현 · 탐지 · 회복

**하네스는 왜 `pthread_mutex_lock` 을 그대로 쓰지 않나.** 진짜 교착에 빠진 `pthread_mutex_lock` 은 영원히 돌아오지 않는다. 맥에는 `pthread_mutex_timedlock` 도 없다. 그래서 포크를 직접 만든다. `holder`(누가 쥐었나) + mutex + cond 구조이고, 기다림은 `pthread_cond_timedwait` 20 ms 조각으로 끊어서 매번 `halt` 를 본다. 포크 내부 mutex 는 몇 줄 동안만 쥐므로 그 자체는 교착에 끼지 않는다. 겉으로 보이는 동작(쥔 채 잠들어 기다림)은 진짜 뮤텍스와 같다.

**강제 스케줄.** 철학자는 첫 포크를 잡은 직후 첫 판에서만 `gate_arrive_wait` 에서 멈춘다. 워치독이 `gate_settled()` 를 확인한 뒤 문을 연다. "모두 도착"만 기다리면 안 되는 이유는 해법마다 문에 못 오는 사람이 있기 때문이다. ordered 는 P0 과 P4 가 둘 다 F0 을 먼저 원하고, seated 는 한 명이 자리를 못 얻는다. 그래서 조건을 이렇게 정했다.

| 철학자 상태 | 문을 열어도 되나 | 이유 |
|---|---|---|
| `AT_GATE` (첫 포크 쥠) | 예 | 문이 닫혀 있는 한 포크를 놓지 않는다 |
| `WAIT_FIRST`, 그 포크를 쥔 사람이 `AT_GATE` | 예 | 문이 열리기 전에는 절대 못 잡는다 = 진짜로 막힘 |
| `WAIT_SEAT`, `AT_GATE` 가 n−1명 | 예 | 자리 n−1개를 문 앞 사람들이 다 쥐었다 |
| 그 밖 (생각 중, 잡는 중) | 아니오 | 잠깐 지나가는 상태일 수 있다 |

이 조건은 순간적인 상태를 "막혔다"로 잘못 보지 않으므로 결과가 **결정적**이다. 하네스 첫 버전에는 naive 강제 스케줄에서 식사가 2번 나오는 버그가 있었다(실측 `total=2`). 원인은 문이 아니라 회복 쪽이었다. 회복 중에 이웃이 놓은 포크를 대기자가 **새로 잡아서** 먹어 버린 것이다. 그래서 `fork_take` 는 비어 있는지보다 `halt` 를 먼저 본다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" fill="currentColor" font-weight="bold">강제 스케줄 + 워치독 (naive, n = 5) — 시간 →</text>
<text x="10" y="62" fill="currentColor">P0…P4</text>
<rect x="80" y="44" width="130" height="28" rx="4" class="f-ok" stroke="currentColor"/><text x="88" y="63" fill="currentColor">take(F[p]) 왼쪽</text>
<rect x="215" y="44" width="120" height="28" rx="4" class="f-mut" stroke="currentColor"/><text x="223" y="63" fill="currentColor">gate 에서 대기</text>
<rect x="340" y="44" width="200" height="28" rx="4" class="f-warn" stroke="currentColor"/><text x="348" y="63" fill="currentColor">take(F[p+1]) … 20 ms 조각</text>
<rect x="545" y="44" width="185" height="28" rx="4" class="f-ok" stroke="currentColor"/><text x="553" y="63" fill="currentColor">false → put(F[p]) → 종료</text>
<text x="10" y="132" fill="currentColor">워치독</text>
<rect x="80" y="114" width="255" height="28" rx="4" class="f-mut" stroke="currentColor"/><text x="88" y="133" fill="currentColor">2 ms 마다 gate_settled()? → 문 열기</text>
<rect x="340" y="114" width="200" height="28" rx="4" class="f-mut" stroke="currentColor"/><text x="348" y="133" fill="currentColor">식사 0 이 200 ms 지속</text>
<rect x="545" y="114" width="185" height="28" rx="4" class="f-acc" stroke="currentColor"/><text x="553" y="133" fill="currentColor">스냅샷 2 번 → 같은 순환</text>
<line x1="275" y1="114" x2="275" y2="76" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="637" y1="114" x2="637" y2="76" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="600" y="98" fill="currentColor" font-size="12">halt + 깨우기</text>
<text x="10" y="180" fill="currentColor" font-weight="bold">스냅샷 = 대기 그래프 (각 철학자 out-degree ≤ 1 → 포인터 따라가기)</text>
<text x="10" y="202" fill="currentColor" font-family="monospace">wants  = [F1, F2, F3, F4, F0]      holder = [P0, P1, P2, P3, P4]</text>
<text x="10" y="222" fill="currentColor" font-family="monospace">next(p) = holder[wants[p]]  :  P0→P1→P2→P3→P4→P0  (길이 5)</text>
<text x="10" y="248" fill="currentColor">두 번 연속 같은 순환일 때만 보고한다: 포크 상태는 포크마다 따로 읽으니 한 장면이 아닐 수 있다.</text>
<text x="10" y="268" fill="currentColor">진짜 교착은 변하지 않으므로 두 번 봐도 같다. 순환 없이 2 초간 식사 0 이면 STALL (라이브락·굶주림 의심).</text>
<text x="10" y="290" fill="currentColor">회복 = 밖에서 "선점": halt 를 본 철학자가 쥔 포크를 스스로 놓는다 → 비선점 조건을 사후에 깸.</text>
</svg>
```

**대기 그래프에서 순환 찾기.** 철학자는 한 번에 포크 하나만 기다리고, 포크의 주인은 한 명뿐이다. 그래서 그래프의 모든 노드가 나가는 간선을 많아야 하나 가진다(함수 그래프). 이런 그래프에서는 SCC 알고리즘이 필요 없다. 시작점에서 `next(p) = holder[wants[p]]` 를 따라가다가 지금 경로에 이미 있는 노드를 다시 만나면 그곳부터가 순환이다. 보고는 순환 안에서 가장 작은 번호부터 한다. 그래서 문자열이 결정적이고 `strcmp` 로 비교할 수 있다. 일반 그래프(한 스레드가 여러 개를 기다리는 경우, 락 클래스 그래프)는 OS-M11(간선 삽입 때 DFS)과 ALGO-A10(Tarjan/Kosaraju)이 다룬다.

정답의 실제 출력:

```text
DEADLOCK: P0 holds F0 waits F1, P1 holds F1 waits F2, P2 holds F2 waits F3, P3 holds F3 waits F4, P4 holds F4 waits F0
PASS r.deadlocked == 1 && r.cycle_len == 5
PASS strcmp(r.report, want) == 0
PASS r.total == 0 && r.forks_free_after == 1 && r.gate_timeout == 0
DEADLOCK: P0 holds F0 waits F1, … , P10 holds F10 waits F11, P11 holds F11 waits F0
PASS r.deadlocked == 1 && r.cycle_len == 12 && strcmp(r.report, want) == 0
```

**A1 전역 순서가 순환을 막는 이유(증명).** 모든 스레드가 자원을 정해진 전순서(total order) `<` 의 오름차순으로만 잡는다고 하자. 순환 `T1 → T2 → … → Tk → T1` 이 있다면 각 Ti 는 자기가 쥔 것 중 가장 큰 자원 `hi` 보다 **큰** 자원 `wi` 를 기다린다. `wi` 의 주인이 T(i+1) 이니 `h(i+1) ≥ wi > hi` 이다. 고리를 한 바퀴 돌면 `h1 > h1` 이 되어 모순이다. 그러니 순환 대기는 불가능하다.

```svg
<svg viewBox="0 0 740 220" xmlns="http://www.w3.org/2000/svg" font-size="13">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" fill="currentColor" font-weight="bold">락 순서 그래프: 간선 = "first 를 쥔 채 second 를 잡는다" (철학자마다 하나)</text>
<text x="10" y="58" fill="currentColor">naive</text>
<rect x="90" y="40" width="40" height="26" class="f-acc" stroke="currentColor"/><text x="100" y="58" fill="currentColor">F0</text>
<rect x="190" y="40" width="40" height="26" class="f-acc" stroke="currentColor"/><text x="200" y="58" fill="currentColor">F1</text>
<rect x="290" y="40" width="40" height="26" class="f-acc" stroke="currentColor"/><text x="300" y="58" fill="currentColor">F2</text>
<rect x="390" y="40" width="40" height="26" class="f-acc" stroke="currentColor"/><text x="400" y="58" fill="currentColor">F3</text>
<rect x="490" y="40" width="40" height="26" class="f-acc" stroke="currentColor"/><text x="500" y="58" fill="currentColor">F4</text>
<line x1="130" y1="53" x2="188" y2="53" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="230" y1="53" x2="288" y2="53" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="330" y1="53" x2="388" y2="53" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="430" y1="53" x2="488" y2="53" stroke="currentColor" marker-end="url(#ah3)"/>
<path d="M510 66 C 510 100, 110 100, 110 68" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3" marker-end="url(#ah3)"/>
<text x="545" y="58" fill="currentColor">P4: F4 → F0 이 뒤로 감</text>
<text x="545" y="78" fill="currentColor">→ 순환 = 교착 가능</text>
<text x="10" y="148" fill="currentColor">ordered</text>
<rect x="90" y="130" width="40" height="26" class="f-acc" stroke="currentColor"/><text x="100" y="148" fill="currentColor">F0</text>
<rect x="190" y="130" width="40" height="26" class="f-acc" stroke="currentColor"/><text x="200" y="148" fill="currentColor">F1</text>
<rect x="290" y="130" width="40" height="26" class="f-acc" stroke="currentColor"/><text x="300" y="148" fill="currentColor">F2</text>
<rect x="390" y="130" width="40" height="26" class="f-acc" stroke="currentColor"/><text x="400" y="148" fill="currentColor">F3</text>
<rect x="490" y="130" width="40" height="26" class="f-acc" stroke="currentColor"/><text x="500" y="148" fill="currentColor">F4</text>
<line x1="130" y1="143" x2="188" y2="143" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="230" y1="143" x2="288" y2="143" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="330" y1="143" x2="388" y2="143" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="430" y1="143" x2="488" y2="143" stroke="currentColor" marker-end="url(#ah3)"/>
<path d="M110 156 C 110 195, 510 195, 510 158" fill="none" stroke="currentColor" stroke-width="2" marker-end="url(#ah3)"/>
<text x="545" y="148" fill="currentColor">P4: F0 → F4 (작은 것 먼저)</text>
<text x="545" y="168" fill="currentColor">간선이 모두 오른쪽 → 비순환</text>
<text x="545" y="188" fill="currentColor">(연습 4 가 DFS 로 확인)</text>
</svg>
```

강제 스케줄에서 ordered 는 P0 과 P4 가 F0 을 두고 다툰다. 진 쪽은 `WAIT_FIRST`(문 앞 사람이 쥐었으니 진짜로 막힘), 나머지 넷은 `AT_GATE` 이다. P0 이 이겼다면, 문이 열릴 때 P3 는 F4 를 얻는다(F4 는 P4 의 둘째인데 P4 는 아직 F0 도 없다). P4 가 이겼다면 P3 와 P4 가 F4 를 두고 다투고, 누가 이기든 사슬의 끝이 먹는다. 거기서부터 사슬이 풀린다. 실측: `[ordered n=5 forced] deadlocked=0 meals=250 (min 50 max 50)`, `[ordered n=12 free] meals=240000 (min 20000 max 20000) … 88 ms`.

### Part B — trylock + 백오프와 라이브락

trylock 해법은 OSTEP 32장 분류로 **비선점** 조건을 스스로 깬다. 둘째를 못 얻으면 쥔 것을 내놓는다(자발적 선점). "기다리는 동안 아무것도 쥐고 있지 않으니 점유 대기를 깬다"고 설명하는 책도 있다. 면접에서는 둘 다 말하고 "쥔 것을 내놓는다"는 동작을 짚으면 된다. 대가는 OSTEP 이 말한 그대로다. ① **라이브락**: 모두 일은 하는데 아무도 진전하지 않는다. ② 캡슐화: 첫 락이 깊은 함수 안에 있으면 "처음으로 되돌아가기"가 어렵다. ③ 첫 락을 쥐고 이미 상태를 바꿨다면 되돌려야 한다(rollback).

**락스텝 = 라이브락의 강제 스케줄.** 한 판을 배리어 3개로 나눈다: ① 참가자가 왼쪽 `fork_try` (항상 성공 — F[p] 를 이 박자에 원하는 사람은 p 뿐) ② 참가자가 오른쪽 `fork_try` (F[p+1] 은 p+1 이 참가했으면 쥐고 있다) ③ 먹거나 놓기, 실패면 `skip = backoff_rounds(…) − 1` 판 쉼. 박자 사이에 경쟁이 없으므로 결과는 seed 에만 달려 있다.

| 백오프 | 200 판 결과 (실제 출력) | 해석 |
|---|---|---|
| 고정 (항상 1 → 쉬지 않음) | `meals=0 attempts=1000` | 매 판 5명 모두 왼쪽 잡기 → 모두 실패 → 모두 놓기. 포크는 1000번 오갔지만 식사 0 = **라이브락** |
| 무작위 (`1 + rng % 2^min(a,6)`) | `meals=375 [51 92 57 51 124]` | 누가 한 판 쉬면 왼쪽 이웃이 성공한다. seed 가 같으면 두 번 돌려도 같다 (CHECK) |

진짜 스레드(자유 실행)에서는 고정 백오프도 라이브락에 빠지지 않았다. `trylock+fixed` 는 5명·12명 모두 1초 동안 33만~75만 끼를 먹었다(아래 벤치 표). 스레드마다 깨어나는 시각이 µs 단위로 흔들려서 박자가 저절로 깨지기 때문이다. 이 맥에서는 라이브락이 **위험**일 뿐 측정으로 재현되지는 않았다. 하지만 대칭성이 강한 환경, 예를 들어 같은 tick 에 깨어나는 RTOS 태스크나 같은 클럭의 하드웨어 상태 기계에서는 락스텝이 실제로 생긴다. 그래서 백오프에는 난수를 넣는다. 이더넷 지수 백오프, CAN 의 ID 중재처럼 대칭을 깨는 장치는 하드웨어에도 늘 있다.

```c
/* TODO B2 의 답: 둘째를 못 얻으면 첫 포크를 놓는다 — 이 한 줄이 조건 하나를 깬다 */
if (fork_try(&t->fork[second], p)) return true;
fork_put(&t->fork[first], p);
atomic_fetch_add(&t->retries, 1);
if (atomic_load(&t->halt)) return false;
unsigned r = backoff_rounds(rng, ++attempt, t->c.strat == S_TRYLOCK);
if (t->c.backoff_us) sleep_us(r * t->c.backoff_us);
```

실측(강제 스케줄): `[trylock n=5 forced] deadlocked=0 meals=250 … retries=3`. 문이 열린 직후 **처음 시도한 사람은 반드시 실패한다**(모두 왼쪽을 쥐고 있으므로). 그래서 `retries >= 1` 은 매 실행 성립하고 CHECK 할 수 있다.

### Part C — 착석 n−1과 펌웨어

**왜 n−1명이면 교착이 없나.** 앉은 사람이 모두 막혔다고 가정하고 대기 그래프를 따라가 보자. 첫 포크 F[p] 를 기다리는 p 를 생각한다. F[p] 를 쥘 수 있는 다른 사람은 p−1 뿐이고, p−1 에게 F[p] 는 **둘째** 포크다. 그러니 p−1 은 두 개를 다 쥐고 먹는 중이고 곧 놓는다. 즉 막힌 게 아니다. 그러므로 진짜로 막힌 사람은 모두 첫 포크를 쥐고 F[p+1] 을 기다리며, F[p+1] 의 주인은 그것을 첫 포크로 쥔 p+1 이다. 대기 그래프의 화살표는 항상 p → p+1 이다. 순환이 되려면 화살표가 식탁을 **한 바퀴** 돌아야 하므로 n명 전원이 앉아 있어야 한다. 그런데 자리는 n−1개다. 모순이다. 이것은 고리가 생길 수 없도록 **입장을 제한**하는 방식(회피와 닮은 순환 대기 깨기)이다. 이름이 비슷한 "웨이터(waiter)" 해법과 헷갈리기 쉽다. 웨이터는 두 포크를 **한꺼번에** 허락하는 중재자라서 점유 대기를 깬다(아래 Q7).

```c
/* TODO C1 의 답 — OS-B06 의 조건 변수 규칙 그대로, 대기는 시간 조각으로 */
while (s->count == 0) {
    if ((halt && atomic_load(halt)) || (max_ms >= 0 && now_ms() - t0 >= (double)max_ms)) { unlock; return false; }
    deadline_after(&ts, WAIT_SLICE_MS);
    pthread_cond_timedwait(&s->cv, &s->m, &ts);
}
s->count--;
```

실측: `[seated n=5 forced] deadlocked=0 meals=250 (min 50 max 50) … max_seated=4`(넷은 문 앞, 한 명은 자리 대기), `[seated n=12 free] meals=240000 … max_seated=11 186 ms`.

**펌웨어: 순위 락(C2).** FreeRTOS 의 `xSemaphoreTake` 는 락 순서를 검사하지 않는다. V11.3.2 `queue.c` 의 `xQueueSemaphoreTake` 를 읽었고, `queue.c`/`tasks.c` 에서 deadlock 관련 검사를 찾지 못했다(`~/.esn_scratch/FreeRTOS-Kernel`). 그래서 프로젝트가 직접 규칙을 둔다. 락마다 정수 순위를 정적으로 정하고(예: `i2c_bus` 10 < `sensor_cache` 20 < `log` 30), 래퍼에서 "이미 쥔 것보다 높은 것만" 허용한다. 이 검사는 쥔 락 개수만큼 비교 몇 번이면 끝나서 MCU 에서도 싸다. 실제로 막히기 전에 거부하므로 테스트에서 한 번만 그 경로를 지나가도 잡힌다. OS-M11 의 lockdep 은 같은 일을 순위 선언 없이 **실행 중에 순서를 배워서** 한다.

| 스크립트 (CHECK) | 결과 | 의미 |
|---|---|---|
| 센서: `i2c_bus` → `sensor_cache` | OK, OK | 오름차순 |
| 로거: `log` → `i2c_bus` | OK, `FW_E_ORDER` | 센서와 반대 순서. 지금 i2c 는 센서가 쥐었지만 BUSY 보다 ORDER 가 먼저다 |
| 로거: `log` 재귀 | `FW_E_ORDER` | 비재귀 뮤텍스 재귀 = 자기 자신과의 교착 |
| ISR: `log` | `FW_E_ISR` | ISR 은 기다릴 수 없다. 큐/플래그로 넘기고 태스크가 처리 (C-M14) |
| 고친 로거: 메시지 복사 → `log` 놓기 → `i2c_bus` → `log` | OK, OK | 순서를 지키도록 코드 구조를 바꿈 |

펌웨어에서 교착을 막는 다른 장치들:

- **우선순위 천장(IPCP)** — 단일 코어에서 락을 잡는 순간 그 락을 쓰는 태스크 중 가장 높은 우선순위로 올라간다. 그래서 같은 락 집합을 쓰는 다른 태스크가 끼어들어 두 번째 락을 먼저 잡을 수 없고, 고리가 처음부터 생기지 않는다. RTOS-M05 가 반대 순서로 두 뮤텍스를 잡는 예에서 PI 는 교착, IPCP 는 무교착인 것을 타임라인으로 보였다. 멀티코어에서는 이 보장이 성립하지 않는다.
- **ISR 은 뮤텍스를 기다리지 않는다.** 태스크가 쥔 락을 ISR 이 기다리면, 그 태스크는 ISR 이 끝나야 돌 수 있으므로 즉시 교착이다. ISR 에서는 `FromISR` 계열 비대기 호출, 인터럽트 금지 구간(C-M14), 지연 처리(deferred work)만 쓴다.
- **워치독 = 최후의 선점.** 태스크 하트비트가 끊기면 리셋한다. 모든 자원을 강제로 회수하는 가장 거친 "비선점 깨기"이다. 이 레슨의 워치독이 `halt` 로 회복시키는 것도 같은 생각을 작게 한 것이다. 리셋 전에 진단(어느 태스크가 어느 락을 기다리나)을 백업 RAM 에 남겨 두면 재현이 안 되는 교착을 현장에서 잡을 수 있다.

### 벤치마크 — demo_bench.c

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -O2 -pthread demo_bench.c -o bench && ./bench 1000     # solution.c 를 include, 약 18 초
```

기계: Apple M6 (`hw.perflevel0/1/2.physicalcpu` = 2 / 4 / 6), Apple clang version 21.0.0 (clang-2100.0.123.102). **`uptime` load average 9.84 → 9.57** (다른 세션의 작업이 동시에 돌던 상태). 아래는 세 번 돌린 것 중 세 번째 실행의 표다. 앞의 두 번(load 9.5~13.2)도 순위와 비율은 같았다. **비율만 믿을 것.** 판마다 1초, 생각 = 먹기 = `spin_work(2000)` ≈ 0.9~1.8 µs (실행마다 측정값이 달랐다: 코어 종류·클럭 영향).

| n | 해법 | backoff | meals/s | min | max | max/min | retry/meal | 결과 |
|---|---|---|---|---|---|---|---|---|
| 5 | naive | - | (5끼 후 정지) | 0 | 3 | - | 0 | **DEADLOCK** ~7 ms |
| 5 | ordered | - | 193 607 | 10 439 | 102 457 | 9.81 | 0 | ok |
| 5 | trylock+rand | 5 µs | **432 337** | 85 904 | 87 332 | **1.02** | 0.240 | ok |
| 5 | trylock+fixed | 5 µs | 348 552 | 69 725 | 70 506 | 1.01 | 0.393 | ok |
| 5 | trylock+rand | 0 µs | 272 241 | 49 305 | 60 827 | 1.23 | 76.0 | ok |
| 5 | trylock+fixed | 0 µs | 333 023 | 62 882 | 71 323 | 1.13 | 71.6 | ok |
| 5 | seated n−1 | - | 173 702 | 27 352 | 40 016 | 1.46 | 0 | ok |
| 12 | naive | - | (2237끼 후 정지) | 0 | 654 | - | 0 | **DEADLOCK** ~12 ms |
| 12 | ordered | - | 253 705 | **2** | 151 143 | **75 571** | 0 | ok |
| 12 | trylock+rand | 5 µs | 693 305 | 50 753 | 65 450 | 1.29 | 0.106 | ok |
| 12 | trylock+fixed | 5 µs | 752 971 | 58 972 | 69 422 | 1.18 | 0.167 | ok |
| 12 | trylock+rand | 0 µs | 800 518 | 59 037 | 73 465 | 1.24 | 22.9 | ok |
| 12 | trylock+fixed | 0 µs | 700 832 | 47 209 | 67 451 | 1.43 | 31.1 | ok |
| 12 | seated n−1 | - | 148 569 | 11 015 | 14 145 | 1.28 | 0 | ok |

```text
per-philosopher meals (P0..Pn-1), 1000 ms each:
  ordered    n= 5: 9955 35889 38453 108784 6721
  ordered    n=12: 16 266 62 176 578 1283 3060 7021 35044 72334 137143 4
  seated n-1 n= 5: 49322 49078 49364 50536 48636
  seated n-1 n=12: 24241 24110 23855 23966 23675 23492 23385 23372 23355 23668 23441 23893
naive time-to-deadlock, n=5, 8 runs (think=eat=2000):
  run 0: deadlock after 38 meals, 5.1 ms
  run 1: deadlock after 14 meals, 6.6 ms
  …
  run 7: deadlock after 24 meals, 6.2 ms
```

읽는 법:

1. **naive 는 강제하지 않아도 금방 멈춘다.** 이 실행의 8번은 모두 10~38끼, 시작 후 5~7 ms 만에 멈췄다(첫 실행에서는 9~1086끼, 5.8~14.3 ms). 강제 스케줄은 "가능성"을 "확실"로 바꿨을 뿐이고, 이 설정에서는 원래도 거의 확실했다. 시각은 워치독 탐지 시각에서 `STALL_MS` 를 뺀 근삿값이다.
2. **전역 순서는 교착이 없지만 공정하지 않다.** n = 12 에서 P10 은 137143끼, P11 은 4끼, P0 은 16끼를 먹었다(세 번 모두 같은 모양이었다). 모두 "왼쪽 먼저"인데 P11 만 F0 먼저라서, 기다림의 사슬이 P0 ← P1 ← … 방향으로 길게 생긴다. 사슬 끝에 있는 P10 은 경쟁자가 하나(P11, 그런데 P11 은 먼저 F0 을 P0 과 다퉈야 함)뿐이라 거의 혼자 먹는다. 포크는 barging(깨운 대기자보다 방금 놓은 사람이 먼저 다시 잡을 수 있음)을 허용하는 락이라 이 쏠림이 굳는다. 해석은 추정이다. 숫자는 실측이다. **"교착 없음 ≠ 굶주림 없음"**.
3. **trylock 이 가장 빠르고 고르다.** 쥔 채 잠드는 사람이 없어서 이웃이 막히지 않는다. 대기자의 깨어남(수십 µs)이 먹는 시간(~1 µs)보다 훨씬 비싼 이 설정에서는 그 차이가 크다. 백오프 0 µs 는 재시도가 식사당 23~76번이다(CPU 를 태움). 5 µs 면 0.1~0.4번으로 줄고 처리량도 비슷하거나 낫다. 자유 실행에서 고정과 무작위의 차이는 이 표의 오차 안이다(라이브락 없음).
4. **seated n−1 이 가장 고르지만 가장 느리다(5명 기준 ordered 의 0.9배, 12명은 0.6배).** 자리 세마포어 하나가 전원이 지나가는 전역 락이 되고, 앉은 n−1명 사이에는 여전히 쥔 채 기다리는 사슬이 생긴다. 공정한 이유는 대기자가 자리 큐에서 차례로 들어오기 때문으로 보인다(추정). max/min 은 판마다 흔들렸다. 표의 판은 1.46, 바로 다음 판(per-philosopher 줄)은 1.04 였다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution            # 31/31 passed (8회 반복), ~1 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_san && ./so_san   # 31/31, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan                                    # 31/31, 경고 0
```

시작 코드와 다른 줄은 TODO 다섯 군데(A1·B1·B2·C1·C2)뿐이다. 회복 버그를 고친 `fork_take` 의 "halt 먼저" 순서는 하네스에 있으므로 양쪽이 같다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| trylock 실패 후 첫 포크를 쥔 채 다시 try | 점유 대기·비선점이 그대로다. 교착이 바쁜 대기(CPU 100%)로 바뀔 뿐 | 실패하면 `fork_put(first)` → 백오프 → 처음부터 |
| 백오프를 고정값(또는 0)으로 | 대칭적인 스케줄에서 모두 같이 실패한다 = 라이브락 (락스텝 실측 식사 0/200판) | `1 + rng % 2^min(attempt, cap)` — 난수 + 상한 있는 지수 |
| "락 순서"를 호출하는 쪽 인자 순서로 | `transfer(a, b)` 와 `transfer(b, a)` 가 반대로 잡는다 (ABBA) | 인자와 무관한 키(주소, id, rank)로 정렬 — 연습 1 `lock_pair` |
| 같은 자원을 두 번(`a == b`) 잡기 | 비재귀 뮤텍스면 자기 자신과 교착 (POSIX 기본 뮤텍스는 미정의 동작) | `if (a == b) lock(a) once` |
| 교착 테스트에 `pthread_mutex_lock`/`join` 을 그대로 | 테스트가 영원히 멈춘다 (CI 타임아웃) | 모든 대기에 시간 제한 + 진전 감시(워치독), join 전에 완료 확인 |
| 워치독이 스냅샷 한 장으로 "교착" 판정 | 포크를 하나씩 읽으니 지나가는 상태가 순환처럼 보일 수 있다 | 진전 0 + 같은 순환이 두 번 연속일 때만 |
| 회복 중 대기자가 막 풀린 포크를 잡음 | 회복하는 동안 식사가 생긴다 (처음 버전에서 실측: 2끼) | 대기 루프에서 `halt` 를 `holder == -1` 보다 먼저 |
| ISR 에서 뮤텍스 take (타임아웃 0이라도) | 소유권·우선순위 상속은 태스크 문맥 개념이다. 기다리면 즉시 교착 | ISR 은 큐/플래그로 넘기고 태스크가 락을 잡는다 (C-M14) |
| "순서 해법이면 다 해결" | 교착만 막는다. 실측 12명에서 4끼 vs 137143끼 | 공정성이 필요하면 FIFO 락, 착석 제한, 나이(age) 기반 우선 |

## 8. 면접 Q&A

<details><summary>Q1. 교착의 4가지 조건을 말하고, 각각을 깨는 방법을 하나씩 대 보세요.</summary>

Coffman 외(1971)의 필요 조건은 넷이다. ① 상호 배제: 공유 가능하게 만든다(읽기 전용 복사, lock-free, 자원 복제). 대부분 불가능하다. ② 점유 대기: 필요한 것을 한 번에 전부 잡거나 아무것도 잡지 않는다(중재자, 전역 prevention 락). ③ 비선점: trylock 으로 실패 시 스스로 놓거나, 밖에서 빼앗는다(타임아웃 abort, DB 희생자, 워치독). ④ 순환 대기: 전역 순서(rank, 주소), 또는 고리가 생길 수 없도록 참가자를 제한한다(n−1 착석). 실무에서 가장 많이 쓰는 것은 ④ 전역 순서다(OSTEP 32장도 "가장 실용적"이라고 한다). 꼬리질문: "넷이 다 있으면 반드시 교착인가요?" → 아니다. 필요 조건일 뿐이고, 실제로는 타이밍이 맞아야 한다. 그래서 테스트로 잘 안 잡히고, 이 레슨은 강제 스케줄로 타이밍을 고정했다.
</details>

<details><summary>Q2. 세 해법을 비교해 보세요. 실제로 무엇을 재셨나요?</summary>

전역 순서: 코드가 가장 단순하고 오버헤드가 0이지만 공정하지 않다. 12명에서 max/min 이 7만 배였다(P11 4끼). trylock + 무작위 백오프: 이 설정에서 가장 빠르고(12명 69만~80만 meals/s) 고르다(max/min 1.2~1.4). 대신 재시도 비용이 있고(백오프 0 µs 면 식사당 23번), 라이브락 위험, 되돌리기 문제가 있다. n−1 착석: 가장 고르지만(1.03~1.46) 가장 느리다(12명 15만 meals/s). 자리 세마포어가 전역 병목이 된다. load 가 9~13인 공유 맥에서 측정했으므로 순위와 비율만 말한다. 꼬리질문: "왜 trylock 이 blocking 보다 빠르죠?" → 쥔 채 잠드는 사람이 없어서다. 대기자가 깨어나는 데 수십 µs 가 걸리는 동안 그 사람이 쥔 포크는 아무도 못 쓴다. 먹는 시간이 1 µs 인 이 설정에서 그 차이가 크다(해석은 추정).
</details>

<details><summary>Q3. 교착(deadlock), 라이브락(livelock), 굶주림(starvation)의 차이는?</summary>

교착: 모두 막혀 있고 상태가 변하지 않는다(대기 그래프에 순환). 라이브락: 모두 움직이고 상태도 계속 변하는데(포크가 1000번 오감) 진전이 0이다. 대기 그래프에 순환이 안 보일 수 있어서 "진전 0인데 순환 없음 = STALL"로 따로 잡는다. 굶주림: 시스템 전체는 진전하는데 특정 스레드만 계속 진다(ordered 의 P11). 탐지 방법도 다르다. 교착은 그래프 스냅샷, 라이브락은 진전 카운터, 굶주림은 스레드별 진전 min/max 로 본다. 꼬리질문: "라이브락은 실제로 나나요?" → 이 맥의 자유 실행에서는 고정 백오프도 안 났다(타이밍 잡음). 하지만 같은 tick 에 깨어나는 태스크처럼 대칭적인 시스템에서는 난다. 락스텝 강제 스케줄에서는 200판 식사 0이었다.
</details>

<details><summary>Q4. 운영 중인 시스템이 멈췄습니다. 교착인지 어떻게 확인하고, 미리 어떻게 막나요?</summary>

사후 확인: 모든 스레드의 스택(`gdb thread apply all bt`, lldb `bt all`, 커널은 hung task 경고·sysrq)을 떠서 누가 어떤 락에서 자는지와 그 락의 소유자를 이어 본다. 이것이 이 레슨의 대기 그래프다. 런타임 감시: 진전 카운터 + 워치독 + 락 소유자 기록(이 레슨). 개발 중 예방: 락 순서 규칙을 선언하고 래퍼에서 검사하거나(rank, Part C), 순서를 **학습**해서 실제로 멈추기 전에 ABBA 를 보고하는 lockdep(OS-M11), 로그를 모아 오프라인 SCC 분석(ALGO-A10). 꼬리질문: "TSan 은요?" → 이 레슨의 교착에는 경고 0이었다(실측, 데이터 레이스가 아니므로). TOOL-M09 작성 메모에 따르면 TSan 의 lock-order-inversion 보고는 이 맥에서 나오지 않았고 Lima VM(Linux clang)에서만 확인됐다. 이 레슨에서 다시 돌려 보지는 않았다.
</details>

<details><summary>Q5. 락이 동적으로 만들어져서 미리 순서를 정할 수 없으면요? (예: 계좌 이체)</summary>

인자 순서가 아니라 **락 자체의 키**로 정렬한다. 주소(OSTEP 32장 팁), 계좌 id, inode 번호 같은 것이다. `lock_pair(a, b)` 는 `a < b` 면 a 먼저, 아니면 b 먼저 잡는다. 같은 락이면 한 번만 잡는다(연습 1: 반대 방향 이체 20만 번씩, 합계 보존). 정렬할 수 없는 경우(중간에 어떤 락이 필요한지 알게 됨)는 trylock 으로 시도하고 실패 시 전부 놓고 정렬된 순서로 처음부터 다시 잡는다(Linux 의 `double_lock`/`lock_two_nondirectories` 류가 주소·inode 순서를 쓴다고 알려져 있다 — 세부 미확인). 꼬리질문: "주소 순서는 프로세스마다 다른데 괜찮나요?" → 한 실행 안에서 전순서이기만 하면 된다. 다만 공유 메모리에서 프로세스마다 매핑 주소가 다르면 주소 대신 id 를 써야 한다.
</details>

<details><summary>Q6. RTOS 펌웨어에서 교착을 어떻게 막나요?</summary>

① 락 계층(rank) 문서화 + 래퍼 검사(Part C). ② 락을 쥔 채 다른 태스크를 기다리지 않는다(큐 send 블록, 이벤트 대기 금지). ③ 단일 코어라면 우선순위 천장(IPCP)을 쓴다. 락을 잡는 순간 천장 우선순위가 되어, 같은 락을 쓰는 태스크가 끼어들 수 없으니 고리가 생기지 않는다(RTOS-M05 타임라인). OSEK/AUTOSAR 자원이 이 방식이다. ④ ISR 은 절대 뮤텍스를 기다리지 않는다(C-M14). ⑤ 타임아웃 있는 take(`xSemaphoreTake(m, pdMS_TO_TICKS(10))`) + 실패 시 에러 로그는 탐지 장치다. 실패 후 재시도 정책이 없으면 해결은 아니다. ⑥ 마지막은 태스크 하트비트 워치독이다. 리셋 전에 락 소유자 정보를 남긴다. 꼬리질문: "천장이 멀티코어에서도 교착을 막나요?" → 아니다. 다른 코어의 태스크는 내 우선순위와 상관없이 동시에 돈다. 멀티코어는 순서 규칙이나 MrsP 같은 별도 프로토콜이 필요하다(MrsP 는 이름만 언급).
</details>

<details><summary>Q7. 다른 고전 해법도 아세요? (waiter, Chandy–Misra)</summary>

**Waiter(중재자)**: 포크를 집기 전에 웨이터(전역 락 + 조건 변수)에게 두 포크를 **한꺼번에** 달라고 하고, 둘 다 비었을 때만 허락받는다. 점유 대기를 깬다. Tanenbaum 교재의 THINKING/HUNGRY/EATING 상태 + `test(i)` 해법이 이 모양이다. 공정성은 별도로 챙겨야 하고, 중재자가 병목이 된다. **Chandy–Misra(1984)**: 포크마다 "깨끗함/더러움" 표시를 두고, 요청 메시지를 받으면 더러운 포크는 닦아서 넘긴다. 처음 배치를 비순환(우선순위 그래프)으로 두고 먹은 뒤 간선 방향을 뒤집어서 교착과 굶주림을 모두 막는다. 공유 메모리 없이 메시지만으로 되는 분산 해법이다(이름과 큰 틀만 언급, 이 레슨에서 구현하지 않음). 꼬리질문: "n−1 착석과 waiter 의 차이?" → n−1 은 입장만 제한하고 포크는 하나씩 잡는다(순환 대기를 깸). waiter 는 두 개를 원자적으로 준다(점유 대기를 깸).
</details>

<details><summary>Q8. 예방(prevention), 회피(avoidance), 탐지·회복(detection)의 차이는?</summary>

예방: 네 조건 중 하나가 구조적으로 성립하지 못하게 한다(전역 순서, trylock). 회피: 요청을 받을 때마다 "이걸 허락해도 안전한 상태인가"를 계산해서 위험하면 기다리게 한다. Dijkstra 의 Banker's algorithm 이 대표적이다. 최대 요구량을 미리 알아야 해서 일반 OS 에서는 거의 쓰지 않는다. OSTEP 은 모든 태스크와 락을 아는 임베디드 같은 제한된 환경에서만 쓸모 있다고 한다. 탐지·회복: 교착을 허용하고, 대기 그래프의 순환을 주기적으로나 막힐 때 찾아서 희생자를 abort/rollback 한다. DB 가 이 방식이다(STOR-M08). 이 레슨의 워치독도 탐지·회복이다. 꼬리질문: "n−1 착석은 어느 쪽?" → 결과는 순환 대기 예방이다. 동작은 "자원 수를 보고 입장을 제한"하는 것이라 회피와 닮았다.
</details>

## 9. 추가 연습

### 연습 1 — `lock_pair`: 주소 순서로 두 락 잡기

두 계좌 사이 이체 `transfer(from, to)` 를 스레드 둘이 **반대 방향**으로 20만 번씩 한다. 셋째 스레드는 자기 자신에게 이체한다(`from == to`). 인자 순서와 상관없이 같은 순서로 잠그는 `lock_pair` 를 만들고, 합계가 보존되는지 확인한다.

<details><summary>답</summary>

`(uintptr_t)a < (uintptr_t)b` 면 a → b, 아니면 b → a 순서로 잠근다. `a == b` 면 한 번만 잠근다(기본 뮤텍스를 두 번 잠그면 POSIX 상 미정의 동작이고, 실제로는 보통 자기 교착이다). 해제 순서는 상관없다(해제는 기다리지 않는다). 테스트 하네스는 `join` 전에 완료 카운터를 10초까지만 기다린다. 교착이 나면 FAIL 을 찍고 끝나므로 멈추지 않는다. 결과: 합계 2,000,000 유지, `acct[0] = 1,000,000 − 3·200,000 + 7·200,000`.
</details>

### 연습 2 — 교착에 "휘말린" 스레드 전부 찾기

`wfg_find_cycle` 은 순환 하나만 준다. 그런데 순환을 **기다리는** 스레드(꼬리)도 영원히 깨어나지 못한다. 순환 위에 있거나 순환에 닿는 스레드를 모두 표시하는 `wfg_stuck` 을 만든다. 경우는 꼬리 1개 + 순환 2개, 재귀 락(길이 1 순환), 순환 없는 사슬, 5명 고리 + 꼬리 2명이다.

<details><summary>답</summary>

3색 표시를 쓴다. 0 = 미확인, 1 = 지금 경로 위, 2 = 판정 끝. 시작점부터 `next(p) = holder[wants[p]]` 를 따라가서 ① 경로 위 노드(색 1)를 만나면 순환이 닫힌 것이므로 지금 경로 **전부**(꼬리 포함)를 stuck 으로 표시한다. ② 이미 판정된 노드(색 2)를 만나면 그 노드의 판정을 물려받는다. ③ −1(기다리지 않음 = 실행 중)에 닿으면 경로 전부가 stuck 아님이다. 각 노드는 한 번만 지나가므로 O(n)이다. 복구할 때는 순환을 끊으면(희생자 하나) 꼬리도 함께 풀린다. 그래서 탐지 결과에서 희생자는 순환 위에서 고른다.
</details>

### 연습 3 — `mutex_timedlock_poll`: 맥에 없는 `pthread_mutex_timedlock`

`pthread_mutex_trylock` + 1 µs 부터 두 배씩, 1 ms 에서 멈추는 sleep 으로 시간 제한 잠금을 만든다. 시간이 지나면 `ETIMEDOUT` 을 돌려준다. 남이 300 ms 쥐고 있으면 50 ms 후 포기해야 하고, 20 ms 뒤 풀리면 잡아야 하고, 빈 락은 즉시 잡아야 한다.

<details><summary>답</summary>

`EBUSY` 일 때만 계속하고, 다른 오류는 그대로 돌려준다. sleep 이 지수적인 이유는 짧게 쥐는 락을 빨리 잡으면서 오래 쥐는 락에 CPU 를 덜 쓰기 위해서다. 단점은 공정성이 없다는 것(FIFO 큐 없음)과 깨어나는 시각이 최대 1 ms 늦을 수 있다는 것이다. RTOS 의 `xSemaphoreTake(m, ticks)` 는 커널이 대기 리스트와 tick 타이머로 이것을 정확하게 한다. 타임아웃은 교착을 **탐지**만 한다. 실패한 뒤에는 쥔 것을 놓고 재시도하거나(trylock 해법) 에러를 보고해야 해결이 된다.
</details>

### 연습 4 — 이 포크 순서는 교착이 없나? (락 순서 그래프 DFS)

철학자마다 간선 `first[p] → second[p]` 하나를 그린 그래프가 비순환이면 교착이 없다. naive, ordered, 홀짝(짝수는 오른쪽 먼저, 홀수는 왼쪽 먼저), Dijkstra 비대칭(마지막 한 명만 오른쪽 먼저)을 DFS 로 판정한다. 그리고 Dijkstra 비대칭이 n = 2..16 에서 "작은 번호 먼저"와 똑같은지 확인한다.

<details><summary>답</summary>

3색 DFS 로 회색 노드로 돌아가는 간선이 있으면 순환이다. naive 는 F0→F1→…→F4→F0 순환이다. ordered·홀짝·Dijkstra 는 비대칭이다. 홀짝은 전순서는 아니지만 간선이 고리를 닫지 못한다(n = 5, 12 확인). 비순환 그래프는 위상 정렬이 있다. 그 순서가 곧 "전역 순서"이므로 Part A 의 증명이 그대로 적용된다. 반대로 순환이 있다고 해서 반드시 교착이 나지는 않는다(타이밍이 필요하다). 이 검사는 **보수적**이다. 이것을 런타임에 자동으로 하는 것이 OS-M11 의 lockdep 이고, 큰 그래프 전체에서 SCC 를 찾는 것이 ALGO-A10 이다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_san && ./pr_san
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan
```

실제 출력: `15/15 passed` (세 빌드 모두, ASan/UBSan 보고 없음, TSan 경고 0).

## 10. 핵심 정리

- 교착의 필요 조건은 상호 배제 · 점유 대기 · 비선점 · 순환 대기(Coffman 1971)이다. **하나만 깨도 교착은 불가능**하고, 넷이 다 있어도 타이밍이 맞아야 난다.
- 전역 순서(작은 번호 먼저 = Dijkstra 비대칭)는 순환 대기를 깬다. 증명은 "각자 쥔 최대 rank 가 고리를 돌면 자기보다 커진다 → 모순". 오버헤드는 0이지만 **공정하지 않다**(실측 4끼 vs 137143끼).
- trylock + 무작위 상한 지수 백오프는 쥔 것을 스스로 놓아서 비선점(관점에 따라 점유 대기)을 깬다. 고정 백오프 + 락스텝이면 라이브락(식사 0/200판)이다. 이 맥의 자유 실행에서는 가장 빠르고 고른 해법이었다.
- n−1 착석은 고리 전체가 필요한 순환을 못 만들게 한다. mutex+cond 세마포어로 만든다(맥은 `sem_init` 불가). 가장 고르고 가장 느리다(전역 병목).
- 탐지: 진전 0 → 대기 그래프 스냅샷(out-degree ≤ 1 → 포인터 따라가기) → 두 번 같은 순환 → 보고 → `halt` 로 회복. 테스트의 모든 대기에는 시간 제한을 둔다(맥에는 `timedlock`·`barrier` 도 없다).
- 펌웨어: rank 래퍼로 순서 위반을 막히기 전에 거부, ISR 은 뮤텍스 대기 금지(C-M14), 단일 코어 IPCP 는 교착도 예방(RTOS-M05), 워치독은 최후의 선점(리셋 전 소유자 기록).
- 같은 순환 대기가 lockdep(OS-M11), SCC(ALGO-A10), DB 대기 그래프(STOR-M08), 래치 순서(STOR-M10), NoC CDG(SOCX-M05), PFC CBD(DCN-M05)에도 나온다.

## 참고자료

- E. G. Coffman, M. J. Elphick, A. Shoshani, "System Deadlocks", *ACM Computing Surveys* 3(2), June 1971 — 4가지 조건의 원전 (OSTEP 32장 참고문헌 [C+71] 에서 확인)
- OSTEP (Arpaci-Dusseau) 31.6 *The Dining Philosophers*, 32장 *Common Concurrency Problems* — 32.3 교착: 4조건, 주소 순서 팁, trylock 과 라이브락, Banker's: <https://pages.cs.wisc.edu/~remzi/OSTEP/threads-bugs.pdf> · <https://pages.cs.wisc.edu/~remzi/OSTEP/threads-sema.pdf> (장 번호는 로컬 OSTEP PDF 추출본 `C31_semaphores.md` / `C32_common_concurrency_problems.md` 로 확인)
- E. W. Dijkstra, "Hierarchical ordering of sequential processes", *Acta Informatica* 1(2), 1971 (EWD310) — 식사하는 철학자, 비대칭 해법: <https://www.cs.utexas.edu/users/EWD/ewd03xx/EWD310.PDF>
- K. M. Chandy, J. Misra, "The Drinking Philosophers Problem", *ACM TOPLAS* 6(4), 1984 — 깨끗한/더러운 포크 (기억 기반, 세부 미확인)
- A. S. Tanenbaum, *Modern Operating Systems* — 식사하는 철학자 모니터 해법(THINKING/HUNGRY/EATING), 교착 장 (판·절 번호 미확인)
- POSIX.1-2017 `pthread_mutex_lock`/`pthread_mutex_trylock`(EBUSY), `pthread_cond_timedwait`(절대 시각, ETIMEDOUT): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_mutex_lock.html>
- FreeRTOS `xSemaphoreTake` (블록 시간), 뮤텍스는 ISR 에서 사용 불가: <https://www.freertos.org/a00122.html>
- 관련 레슨: OS-B05 (뮤텍스), OS-B06 (조건 변수 · 맥 `sem_init` 불가), OS-M07 (스핀락 · 백오프), OS-A01 (futex), RTOS-M03/M04/M05 (역전 · 상속 · 천장 = 교착 예방), C-M14 (ISR vs main), TOOL-M09 (TSan · lock-order-inversion), OS-M11 (mini lockdep), ALGO-A10 (SCC), STOR-M08 (2PL · 대기 그래프), STOR-M10 (래치 순서), SOCX-M05 (채널 의존 그래프), DCN-M05 (PFC 교착)
