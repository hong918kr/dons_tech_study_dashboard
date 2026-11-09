---
id: STOR-M08
time: 95분
updated: 2026-11-08
---
# STOR-M08 동시성 제어: 2PL & 데드락 감지

## 학습 목표

- **lock manager** 를 직접 만든다: S/X **호환성 행렬**(compatibility matrix), 객체마다 **FIFO 요청 큐**, **S→X 업그레이드**(업그레이드 대기자가 먼저). 새 요청은 앞에 대기자가 있으면 호환돼도 새치기하지 않는다.
- **strict 2PL**(strict two-phase locking: 락은 commit/abort 때만 푼다)을 트랜잭션 층에 넣고, **non-strict 2PL** 이 왜 dirty read · 연쇄 abort(cascading abort) · 커밋된 쓰기 소실을 허용하는지 **강제 스케줄**로 재현한다.
- 큐에서 **wait-for 그래프**(WFG)를 만들고, **막히는 순간**(on-block) 막힌 트랜잭션에서 DFS 로 사이클을 찾고, **희생자**(victim)를 고른다(youngest / fewest-locks / least-work). abort = **undo 로그 되돌리기 → 락 해제 → 같은 타임스탬프로 재시도**(굶주림 방지).
- 은행 이체 스트레스(8 스레드 + 감사 트랜잭션)로 **총액 불변식** 과 commit/abort 회계가 정확히 맞는 것을 매번 확인하고, 정책·접근 패턴·타임아웃 감지의 차이를 실측으로 말한다.
- 면접 체크(동시성 가이드 6층)를 DB 쪽에서 답한다: Coffman 4 조건과 DB 가 깨는 조건, 런타임 락 순서 검사(OS-M11 lockdep)와 SCC(ALGO-A10)와의 관계, wait-die vs wound-wait.

## 1. 문제

"두 계좌 사이 이체를 동시에 1000 개 돌리면 총액이 맞나요? 데드락이 나면요?" — DB 엔진 면접의 단골이고, 펌웨어 쪽에서도 같은 모양이 나온다. SSD FTL 은 L2P 매핑 세그먼트, 블록별 valid count, free block 풀 같은 **메타데이터 여러 개를 한 번에 바꿔야** 하고(GC 가 페이지를 옮기면 원래 블록의 valid count 는 줄고 대상 블록의 것은 는다 = 이체), NVMe 다중 큐에서 코어마다 명령을 처리하면 그 메타데이터를 여러 스레드가 동시에 만진다. "여러 객체를 원자적으로, 동시에, 데드락 없이" 가 이 레슨의 주제다.

동시성 가이드 6층의 DB 편이다. OS-M10 이 철학자 문제로 Coffman 4 조건(상호 배제 · 점유 대기 · 비선점 · 순환 대기)과 "락 순서로 순환 대기를 깨기" 를 다룬다. 그런데 **DB 트랜잭션은 락 순서를 미리 정할 수 없다** — 어떤 행을 잠글지는 실행해 봐야(데이터를 읽어 봐야) 안다. 그래서 DB 는 다른 길을 택한다: 데드락이 **일어나게 두고**, 감지해서, 한 명을 **abort(되돌리기 = 강제로 락을 빼앗기)** 한다. 즉 "비선점(no preemption)" 조건을 롤백으로 깬다. 이걸 가능하게 하는 것이 undo 로그다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">lock table (객체마다 FIFO 큐) → wait-for 그래프 → 희생자</text>
<text x="10" y="56" fill="currentColor" font-family="monospace">obj A</text>
<rect x="70" y="40" width="90" height="26" rx="4" class="f-ok" stroke="currentColor"/><text x="80" y="58" fill="currentColor">T1 [X]</text>
<rect x="170" y="40" width="110" height="26" rx="4" class="f-warn" stroke="currentColor"/><text x="180" y="58" fill="currentColor">T2 [wait X]</text>
<text x="10" y="102" fill="currentColor" font-family="monospace">obj B</text>
<rect x="70" y="86" width="90" height="26" rx="4" class="f-ok" stroke="currentColor"/><text x="80" y="104" fill="currentColor">T2 [X]</text>
<rect x="170" y="86" width="110" height="26" rx="4" class="f-warn" stroke="currentColor"/><text x="180" y="104" fill="currentColor">T1 [wait X]</text>
<text x="10" y="140" fill="currentColor">초록 = 보유(held), 주황 = 대기(want). 대기자는 "나와 비호환인 보유자" 를 기다린다.</text>
<path d="M300 76 L380 76" stroke="currentColor" stroke-width="2" fill="none"/>
<path d="M372 70 L382 76 L372 82" stroke="currentColor" fill="none"/>
<text x="304" y="68" fill="currentColor">큐에서 만든다</text>
<circle cx="450" cy="76" r="24" class="f-acc" stroke="currentColor"/><text x="440" y="81" fill="currentColor">T1</text>
<circle cx="620" cy="76" r="24" class="f-warn" stroke="currentColor"/><text x="610" y="81" fill="currentColor">T2</text>
<path d="M474 66 Q535 40 596 66" stroke="currentColor" stroke-width="2" fill="none"/>
<path d="M586 58 L597 66 L585 70" stroke="currentColor" fill="none"/>
<text x="500" y="40" fill="currentColor">T1 → T2 (B 때문)</text>
<path d="M596 88 Q535 114 474 88" stroke="currentColor" stroke-width="2" fill="none"/>
<path d="M484 96 L473 88 L486 84" stroke="currentColor" fill="none"/>
<text x="500" y="124" fill="currentColor">T2 → T1 (A 때문)</text>
<line x1="10" y1="160" x2="730" y2="160" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="10" y="186" fill="currentColor" font-weight="bold">T2 가 막히는 순간 검사: T2 에서 DFS → T1 → T2 로 돌아옴 = 사이클</text>
<rect x="10" y="200" width="230" height="52" rx="4" class="f-mut" stroke="currentColor"/>
<text x="20" y="220" fill="currentColor">① 희생자 = youngest T2 (ts=2)</text>
<text x="20" y="240" fill="currentColor">   대기 취소, 상태 ABORTED</text>
<rect x="255" y="200" width="230" height="52" rx="4" class="f-mut" stroke="currentColor"/>
<text x="265" y="220" fill="currentColor">② T2: undo 로그 거꾸로 적용</text>
<text x="265" y="240" fill="currentColor">   (B = 원래 값) → 그다음 해제</text>
<rect x="500" y="200" width="230" height="52" rx="4" class="f-ok" stroke="currentColor"/>
<text x="510" y="220" fill="currentColor">③ B 를 T1 에 부여 → commit</text>
<text x="510" y="240" fill="currentColor">   T2 는 같은 ts=2 로 재시도</text>
<text x="10" y="284" fill="currentColor">같은 ts 로 재시도 → 시간이 갈수록 늙은 쪽이 되어 다시 희생될 일이 줄어든다.</text>
<text x="10" y="306" fill="currentColor">순서가 중요: 되돌리기 전에 풀면 T1 이 커밋 안 된 B 를 읽는다 (실수 1).</text>
</svg>
```

호환성 행렬 — 이미 부여된 모드(행) 옆에 새 모드(열)가 같이 있을 수 있나:

| held \ want | S | X |
|---|---|---|
| **S** | ✓ (여러 명이 읽기) | ✗ |
| **X** | ✗ | ✗ |

**같은 순환 대기, 다른 층** — 6층 레슨 일곱 개는 같은 그림의 다른 이름이다:

| 레슨 | 자원 (노드/간선) | 누가 기다리나 | 순환 대기를 어떻게 다루나 |
|---|---|---|---|
| OS-M10 | 포크(mutex) | 철학자 스레드 | **예방**: 전역 락 순서, trylock+백오프, N−1 명만 착석 |
| OS-M11 | 락 **클래스** 순서 그래프 | 실행 중인 스레드 | **런타임 검사**: 새 간선마다 도달성 → 실제로 막히기 전에 경고 (lockdep) |
| ALGO-A10 | 락 이벤트 로그 → 그래프 | (오프라인) | **사후 분석**: Tarjan/Kosaraju SCC 로 모든 사이클 묶음 |
| **STOR-M08** | 행/객체 락 **인스턴스**, wait-for 그래프 | 트랜잭션 | **감지 + 복구**: 막히는 순간 DFS, 희생자 abort(undo) |
| STOR-M10 | B+tree 래치(latch) | 인덱스 스레드 | 감지기 없음 → **순서로만** (항상 위→아래, 제자리 업그레이드 금지) |
| SOCX-M05 | NoC 채널 (CDG) | 패킷/플릿 | **설계로 증명**: Dally–Seitz, 비순환 CDG(XY, dateline VC) |
| DCN-M05 | 스위치 버퍼 (PFC) | 프레임 | 상하향 라우팅으로 예방 + PFC watchdog(드롭 = 선점) |

### Part A — lock manager 와 strict 2PL

- `lk_compat(held, want)` (A1) — 위 행렬. `LK_NONE` 은 아무것도 막지 않는다.
- `grant_waiters(lm, obj)` (A2) — 락이 풀리거나 대기가 취소된 뒤: ① 업그레이드 대기자가 유일한 보유자가 되었으면 X 로 올린다(남아 있으면 일반 대기자는 못 지나감) ② 일반 대기자를 도착 순서대로, 처음 막히는 곳에서 멈춤.
- `acquire_locked(...)` (A3) — 이미 보유/재폴링/업그레이드/새 요청. 새 요청은 **대기자가 없고** 보유자와 호환될 때만 즉시 부여. 새로 막혔을 때만 `*blocked_now = true` (그때만 사이클 검사).
- `tx_abort` (A4) — undo 로그를 **거꾸로** 적용한 뒤 `lm_release_all`. `tx_read`=S, `tx_write`=X(쓰기 전 값을 undo 에), `tx_commit`=전부 해제, `tx_unlock`(이른 해제)은 strict 이면 거절 — 이건 주어진다.

### Part B — wait-for 그래프, 사이클, 희생자, 강제 스케줄

- `build_wfg` (B1) — 큐에서 간선 `a→b`("a 가 b 를 기다린다"). 규칙은 5. 풀이의 표.
- `dfs_cycle` (B2) — `start` 에서 출발해 `start` 로 돌아오는 경로를 `cyc[]` 에 남긴다.
- `pick_victim` (B3) — 정책 세 가지, 동률이면 youngest.
- CHECK 는 전부 **스크립트로 한 걸음씩 구동**한다(step 모드, `lm_acquire` 는 막지 않음): 고전 ABBA(T1: X(A)→X(B), T2: X(B)→X(A)) → 데드락 정확히 1 번 · 희생자 T2 · 둘 다 커밋 · 재시도 1 번 · ts 유지. 업그레이드 데드락(S,S → X,X). non-strict 2PL 의 dirty read 와 커밋된 쓰기 소실, strict 에선 둘 다 안 생김.

### Part C — 은행 이체 스트레스 (스레드)

- `transfer_once` (C1) — S(from), S(to) 읽기 → 잔액이 되면 X 로 올려 두 계좌 쓰기 → commit. 도중에 `LM_GRANTED` 가 아니면 abort 후 결과 반환. 바깥 루프(주어짐)는 `LM_ABORTED` 이면 **같은 ts** 로 다시 시도.
- 16 계좌 · 이체 스레드 8 개 × 3000 건 · 감사 스레드(전 계좌를 S 로 읽어 합) 300 건. CHECK: 멈춤 0, 총액 정확, commit 수 = 24300, 시도 = commit + abort, **abort 수 = 찾은 사이클 수**, 모든 감사가 정확한 총액, 끝나면 lock table 비어 있음. 모든 대기는 `pthread_cond_timedwait` 2 초 시한 — 감지가 틀리면 FAIL 하고 큐를 덤프한다(멈추지 않음).
- 데모 `demo_bench.c`: 희생자 정책 · 재시도 ts · 접근 패턴(upgrade / x-direct / ordered) · 타임아웃 감지 비교.

## 2. Problem (English)

> Build a lock manager for transactions over objects `0..63`.
>
> 1. Modes `S` and `X` with the usual compatibility matrix. Each object has a FIFO request queue. A new request is granted immediately only if it is compatible with every holder **and** nobody is waiting (no barging). `S→X` upgrade: granted at once if the requester is the only holder, otherwise it waits with priority over ordinary waiters.
> 2. Strict 2PL transactions: `read` takes S, `write` takes X and logs the before-image; locks are released only at `commit`/`abort`; `abort` applies the undo log in reverse and then releases.
> 3. Deadlock handling: build the wait-for graph from the queues; when a request blocks, search for a cycle through the blocked transaction; pick a victim (youngest / fewest locks / least work), cancel its wait and make it abort; the victim restarts with its **original** timestamp.
> 4. Two front ends: a non-blocking `lm_acquire` for step-by-step scripted tests and a blocking `lm_lock` (per-transaction condvar, bounded wait).
>
> Tests: the classic `T1: X(A)→X(B)` / `T2: X(B)→X(A)` interleaving produces exactly one victim; two readers upgrading the same row deadlock; show with a forced schedule that non-strict 2PL allows a dirty read and a lost committed write; a multithreaded bank-transfer stress keeps the total balance exact, every audit sees the exact total, and `aborts == deadlocks found`.
>
> Follow-ups: why does 2PL guarantee conflict-serializability? Detect on block vs periodically vs timeouts? Wait-die vs wound-wait? Why not just order the locks?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.09 초 만에 끝남 — 스크립트 실행기는 50 걸음 제한, 스트레스는 단위 테스트가 실패하면 건너뛴다):

```text
FAIL starter.c:483  lk_compat(LK_S, LK_S) == true && lk_compat(LK_S, LK_X) == false
PASS lk_compat(LK_X, LK_S) == false && lk_compat(LK_X, LK_X) == false
FAIL starter.c:485  lk_compat(LK_NONE, LK_X) == true
FAIL starter.c:489  lm_acquire(&lm, 0, 0, LK_S) == LM_GRANTED && lm_acquire(&lm, 1, 0, LK_S) == LM_GRANTED
…
  STUCK after 50 steps:
  trace: T1:X(A)=error T2:X(B)=error
…
SKIP 7 stress checks (Part C): unit checks failed
5/44 passed
```

TODO 는 A1–A4, B1–B3, C1. Part A 만 채우고 B 를 비워 두면 ABBA 스크립트가 진짜로 막히고, 하네스가 순환 대기를 큐 덤프로 보여 준다 (실제 출력):

```text
  STUCK after 50 steps:
  obj  0: T1[X] T2[wait X]
  obj  1: T2[X] T1[wait X]
  trace: T1:X(A)=ok T2:X(B)=ok T1:X(B)=wait T2:X(A)=wait
```

## 4. 힌트

<details><summary>힌트 1 — 큐 엔트리 하나로 세 가지 상태</summary>

`{tx, held, want}` 하나로 충분하다. `held=S/X, want=NONE` = 보유자, `held=NONE, want=S/X` = 일반 대기자, `held=S, want=X` = 업그레이드 대기자. 업그레이드를 "두 번째 엔트리" 로 넣으면 자기 S 를 자기가 기다리는 자기 교착(self-deadlock)이 된다.
</details>

<details><summary>힌트 2 — FIFO 와 새치기</summary>

X 가 기다리는데 새 S 를 바로 주면(S 는 보유자 S 와 호환이니까) 읽기가 끊이지 않을 때 X 는 영원히 못 들어간다(writer starvation). 그래서 새 요청은 "대기자가 하나라도 있으면" 줄 끝에 선다. `grant_waiters` 도 처음 막히는 대기자에서 **멈춘다**(건너뛰지 않는다).
</details>

<details><summary>힌트 3 — WFG 간선은 누구에게?</summary>

대기자 w 는 ① w 와 비호환인 **보유자** 를 기다리고, w 가 일반 대기자이면 ② **업그레이드 대기자**(항상 먼저 부여됨)와 ③ **앞에 선 비호환 대기자**(FIFO 라 먼저 부여됨)도 기다린다. 앞에 선 **호환** 대기자(둘 다 S)는 간선이 필요 없다 — 그 대기자를 막는 X 보유자/X 대기자가 w 도 막으므로 이미 간선이 있다.
</details>

<details><summary>힌트 4 — 사이클은 어디서부터 찾나?</summary>

검사 직전까지 그래프에 사이클이 없었다면(매번 검사로 지켜 온 불변식), 새로 생긴 간선은 **방금 막힌 트랜잭션에서 나가는 것** 뿐이다. 그러니 새 사이클은 반드시 그 트랜잭션을 지난다 → 그 노드 하나에서 DFS 하면 된다. 희생자가 내가 아니면 다른 사이클이 또 나를 지날 수 있으니 **반복**한다.
</details>

<details><summary>힌트 5 — abort 의 순서와 희생자 쪽의 일</summary>

lock manager 는 희생자의 **대기만** 취소하고 상태를 `W_ABORTED` 로 둔다. 희생자가 쥔 X 락은 그대로 — 희생자 스레드가 깨어나 undo 를 다 적용한 **다음** 에 `lm_release_all` 한다. 이 순서가 strict 2PL 의 "되돌리기 전 값은 아무도 못 본다" 를 지킨다.
</details>

## 5. 풀이

### Part A — 호환성, 부여, 업그레이드

`lk_compat` 은 한 줄이다: NONE 이 끼면 호환, 아니면 S-S 만. 핵심은 **언제 부여하나** 다.

| 상황 | 결정 | 이유 |
|---|---|---|
| 이미 X, 또는 같은 모드 보유 | 즉시 GRANTED (엔트리 그대로) | 재진입. X 는 S 를 포함 |
| 아직 기다리는 엔트리가 있음 | WAIT | step 모드 재폴링 |
| S 보유 + X 요청, 다른 보유자 0 | 그 자리에서 X 로 | 기다릴 이유 없음 |
| S 보유 + X 요청, 다른 S 보유자 있음 | `want = X` (업그레이드 대기) | 다른 S 가 다 풀릴 때까지 |
| 새 요청, 대기자 0 + 보유자와 호환 | 끝에 추가, 부여 | |
| 새 요청, 그 외 | 끝에 추가, 대기 | FIFO, 새치기 없음 |

`grant_waiters` 는 해제/취소 때마다 돈다. 업그레이드 대기자를 먼저 본다 — 다른 보유자가 없으면 X 로 올리고, 있으면 **바로 return**(일반 대기자도 못 지나감). 그다음 일반 대기자를 앞에서부터 보며 보유자 전부와 호환이면 부여, 아니면 멈춘다. 같은 객체의 S 대기자 여럿이 연달아 깨어나는 것도 이 루프다.

CHECK 로 확인한 시나리오 (전부 step 모드):

```text
obj0: T0[S] T1[S]            T2 X → wait, T3 S → wait (X 대기자 뒤, 새치기 없음)
      T0 해제 → T2 여전히 wait (T1 의 S)   T1 해제 → T2 = X, T3 는 계속 wait   T2 해제 → T3 = S
obj1: T0 S → X               혼자이므로 즉시 업그레이드
obj2: T0[S] T1[S]            T0 X → 업그레이드 wait, T2 S → wait (업그레이드 대기자 우선)
      T1 해제 → T0 = X, T2 는 wait        T0 해제 → T2 = S
```

### Part A — strict 2PL 과 undo 로그

2PL 의 규칙은 하나다: **한 번이라도 락을 풀었으면 더는 잡지 않는다** (늘어나는 단계 growing → 줄어드는 단계 shrinking). `tx_lock` 은 `shrinking` 이면 `LM_ERROR` 를 돌려준다(CHECK: unlock 후 add → ERROR). **strict** 2PL 은 여기에 "X 락은 commit/abort 까지 쥔다" 를 더한다. 이 구현은 S 까지 전부 commit 때 풀므로 엄밀히는 **rigorous 2PL**(SS2PL, strong strict)이고, 교과서·실무에서 흔히 그냥 strict 2PL 이라 부른다.

`tx_write` 는 X 를 잡고, **쓰기 전 값(before-image)** 을 undo 로그에 넣은 다음 쓴다. `tx_abort` 는 로그를 **거꾸로** 적용한다 — 같은 객체를 두 번 쓴 경우 처음 값으로 돌아가야 하기 때문이다 (CHECK: 100 → +5 → +7 = 112, abort → 100). 실제 DB 에서는 이 undo 정보가 WAL 레코드이고(STOR-M03), 되돌리는 동작 자체도 로그로 남긴다(CLR, STOR-M04).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">보유 락 수 (세로) 와 시간 (가로) — lock point 이후에 무엇을 하나</text>
<line x1="60" y1="130" x2="720" y2="130" stroke="currentColor"/>
<line x1="60" y1="40" x2="60" y2="130" stroke="currentColor"/>
<text x="10" y="60" fill="currentColor">basic</text>
<text x="10" y="76" fill="currentColor">2PL</text>
<polyline points="60,130 120,130 120,105 200,105 200,80 300,80 300,105 380,105 380,130 470,130" fill="none" stroke="currentColor" stroke-width="2"/>
<text x="210" y="72" fill="currentColor">lock point</text>
<rect x="300" y="40" width="92" height="26" rx="4" class="f-warn" stroke="currentColor"/>
<text x="306" y="58" fill="currentColor">A 이른 해제</text>
<rect x="395" y="88" width="190" height="26" rx="4" class="f-warn" stroke="currentColor"/>
<text x="403" y="106" fill="currentColor">T2: A=150 읽고 commit</text>
<rect x="600" y="88" width="120" height="26" rx="4" class="f-warn" stroke="currentColor"/>
<text x="608" y="106" fill="currentColor">T1 abort → 100</text>
<line x1="60" y1="270" x2="720" y2="270" stroke="currentColor"/>
<line x1="60" y1="170" x2="60" y2="270" stroke="currentColor"/>
<text x="10" y="200" fill="currentColor">strict</text>
<text x="10" y="216" fill="currentColor">2PL</text>
<polyline points="60,270 120,270 120,245 200,245 200,220 470,220 470,270 720,270" fill="none" stroke="currentColor" stroke-width="2"/>
<rect x="210" y="180" width="250" height="26" rx="4" class="f-mut" stroke="currentColor"/>
<text x="218" y="198" fill="currentColor">T2: S(A) 대기 … (unlock 거절)</text>
<rect x="476" y="230" width="250" height="26" rx="4" class="f-ok" stroke="currentColor"/>
<text x="488" y="248" fill="currentColor">T1 abort·해제 → T2 가 100 을 읽음</text>
<text x="10" y="292" fill="currentColor">basic: 직렬화 가능하지만 회복 불가능(non-recoverable)일 수 있다. strict: commit/abort 때 해제.</text>
</svg>
```

### Part B — wait-for 그래프 만들기

`build_wfg` 는 모든 큐의 모든 대기 엔트리 w 에 대해 다른 엔트리 h 를 보며 간선 `w.tx → h.tx` 를 단다:

| h 의 상태 | w 가 업그레이드 대기자 | w 가 일반 대기자 |
|---|---|---|
| 보유자, w.want 와 비호환 | 간선 | 간선 |
| 보유자, 호환 | — | — |
| 업그레이드 대기자 | (h 의 S 가 X 와 비호환 → 위 줄로 이미 간선) | 간선 (업그레이드가 먼저) |
| w 보다 앞의 일반 대기자, 비호환 | — | 간선 (FIFO) |
| w 보다 앞의 일반 대기자, 호환(S,S) | — | — (그 대기자를 막는 것이 w 도 막는다) |
| w 보다 뒤 | — | — |

CHECK 의 상태 `obj0 = T1[S] T2[S, wait X] T3[wait S] T4[wait X]`, `obj1 = T5[X] T1[wait S]` 에서 실제로 나온 행: `adj[2] = {T1}`, `adj[3] = {T2}`, `adj[4] = {T1, T2, T3}`, `adj[1] = {T5}`, `adj[5] = {}` → 사이클 없음. 여기에 T5 가 obj0 에 S 를 요청하면 `T5 → T2 → T1 → T5` 가 생기고 `find_cycle(adj, 5)` 가 찾는다. Postgres 의 lock manager README 도 비슷하게 "hard edge"(보유자) 와 "soft edge"(앞의 대기자) 를 나누고, soft edge 로만 된 사이클은 큐 순서를 바꿔 풀기도 한다고 설명한다 (기억에 의존 — 세부는 `src/backend/storage/lmgr/README` 확인).

### Part B — 언제 검사하나: 막히는 순간 vs 주기적 vs 타임아웃

| 방식 | 장점 | 단점 | 예 (세부는 버전마다 다름, 기억에 의존) |
|---|---|---|---|
| **막히는 순간** (on-block, 이 레슨) | 데드락이 생기자마자 풀림. 막힌 노드 하나에서 DFS | 막힐 때마다 그래프 비용. 경합이 심하면 lock table 래치를 오래 잡음 | InnoDB `innodb_deadlock_detect` (기본 ON) |
| **잠깐 기다렸다가 검사** | 대부분의 대기는 금방 끝나므로 검사 자체를 거의 안 함 | 데드락이 그만큼 늦게 풀림 | Postgres `deadlock_timeout` (기본 1s) 후 검사 |
| **주기적** (백그라운드) | 래치 경합에서 분리 | 주기만큼 늦음, 전체 그래프 검사 | SQL Server lock monitor (기본 5 초 주기로 알려짐) |
| **타임아웃만** | 그래프 없음, 분산에서도 쉬움 | 헛 abort(데드락 아닌 긴 대기) vs 늦은 해소의 줄다리기 | InnoDB `innodb_lock_wait_timeout` (기본 50s) 은 감지를 껐을 때의 최후 수단 |

막히는 순간 검사가 **완전한** 이유(놓치는 사이클이 없는 이유): 간선은 ① 누가 새로 막힐 때(그 노드에서 나가는 간선) ② 업그레이드 대기가 생길 때 생긴다. ②에서 기존 일반 대기자 Tw 가 업그레이드 대기자 Tu 를 향한 간선을 새로 얻는 것처럼 보이지만, Tw 는 이미 Tu 에 닿아 있었다 — Tw 가 X 를 원하면 Tu 의 S 에 막혀 있었고, S 를 원하면 앞의 X 대기자 Tx 를 통해(Tx 는 Tu 의 S 에 막힘) 닿는다. 부여·취소·해제는 간선을 지우기만 한다. 그래서 "검사 직후엔 사이클 없음" 이 유지되고, 새 사이클은 항상 방금 막힌 노드를 지난다. DFS 는 노드를 한 번씩만 방문하므로 O(V+E) 다. 다만 이 구현의 `build_wfg` 는 막힐 때마다 64 개 큐를 전부 훑는다 — 데모에서 이 비용이 보인다(아래 ordered 행).

### Part B — 희생자 선택과 같은 ts 재시도

`pick_victim` 은 사이클 구성원 중에서 정책 키가 가장 작은(youngest 는 ts 가 가장 큰) 트랜잭션을 고르고, 동률이면 가장 젊은 쪽이다. CHECK 는 같은 3-사이클에서 세 정책이 **서로 다른 희생자** 를 고르는지 본다:

| 트랜잭션 | ts | 보유 락 | 쓴 횟수 | youngest | fewest-locks | least-work |
|---|---|---|---|---|---|---|
| T0 (A 보유, B 대기) | 10 | 1 | 5 | | **희생** | |
| T1 (B,F 보유, C 대기) | 30 | 2 | 2 | **희생** | | |
| T2 (C,D,E 보유, A 요청 → 사이클) | 20 | 3 | 0 | | | **희생** |

희생자가 요청자(T2) 가 아니면 lock manager 는 희생자의 대기를 취소하고(`W_ABORTED`, condvar signal) 다시 검사한다 — 요청자는 계속 기다린다(희생자가 undo 후 풀어 줄 때까지).

**같은 ts 로 재시도** 하는 이유: youngest 정책에서 ts 를 새로 받으면 재시작한 트랜잭션은 다시 가장 젊어지고, 다음 사이클에서 또 희생자가 된다. ts 를 유지하면 시스템의 다른 트랜잭션이 끝나고 새로 시작할수록 상대적으로 늙어 가므로, 유한한 횟수 뒤에는 가장 늙은 쪽이 되어 희생되지 않는다. 데모에서 이게 숫자로 보인다(최대 재시도 9–10 vs 24–41).

### Part B — 강제 스케줄 세 개 (실제 trace)

```text
  DEADLOCK: T2 wants X(A) held by T1 -> T1 wants X(B) held by T2  => victim T2 (ts=2, youngest)
  trace: T1:X(A)=ok T2:X(B)=ok T1:X(B)=wait T2:X(A)=DEADLOCK-victim [rollback+retry] T1:X(B)=ok T2:X(B)=wait T1:commit T2:X(B)=ok T2:X(A)=ok T2:commit
  DEADLOCK: T2 wants X(A) held by T1 -> T1 wants X(A) held by T2  => victim T2 (ts=2, youngest)
  trace: T1:S(A)=ok T2:S(A)=ok T1:X(A)=wait T2:X(A)=DEADLOCK-victim [rollback+retry] T1:X(A)=ok T2:S(A)=wait T1:commit T2:S(A)=ok T2:X(A)=ok T2:commit
  non-strict: T1:X(A)=ok T1:X(B)=ok T1:unlock T2:S(A)=ok T2:commit T1:abort
  non-strict: T1:X(A)=ok T1:X(B)=ok T1:unlock T2:X(A)=ok T2:commit T1:abort
  strict    : T1:X(A)=ok T1:X(B)=ok T1:unlock(refused) T2:S(A)=wait T1:abort T2:S(A)=ok T2:commit
  strict    : T1:X(A)=ok T1:X(B)=ok T1:unlock(refused) T2:X(A)=wait T1:abort T2:X(A)=ok T2:commit
```

1. **ABBA**: T1 이 A−10·B+10, T2 가 B−20·A+20. T2 가 막히는 순간 사이클 `T2→T1→T2`, 희생자 T2. T2 의 undo 가 B 를 100 으로 되돌린 뒤 해제 → T1 이 B 를 받아 커밋 → T2 가 ts=2 그대로 재시도해 커밋. 최종 A=110, B=90 (= 순서 T1;T2 의 결과), `victimized` T1 0 / T2 1.
2. **업그레이드 데드락**: 둘 다 S(A) 로 읽고 둘 다 X(A) 로 올리려 함 → 각자 상대의 S 를 기다림. 이체에서 "읽고 나서 쓰기" 가 이것이다 — Part C 스트레스의 abort 대부분이 이 모양.
3. **non-strict 2PL** (A=100): T1 이 A 를 150 으로 쓰고, B 도 잡은 뒤(lock point) A 를 일찍 푼다 — 2PL 규칙은 지켰다. T2 가 A=150 을 읽고 **커밋**. T1 이 abort → A 는 100. T2 는 존재한 적 없는 값을 근거로 커밋했다(dirty read). 원래라면 T2 도 같이 abort 해야 하는데(연쇄 abort) 이미 커밋했으니 **회복 불가능한(non-recoverable)** 스케줄이다. 두 번째 줄은 더 나쁘다: T2 가 A 에 +10 을 쓰고 커밋한 뒤 T1 의 undo 가 before-image 100 을 덮어써서 **커밋된 쓰기가 사라졌다** (CHECK: 최종 100). strict 에선 unlock 이 거절되고 T2 는 T1 이 끝날 때까지 기다려 100 을 읽는다(쓰기면 110).

### Part C — 은행 이체 스트레스

`transfer_once` 는 읽기 두 번(S) → 쓰기 두 번(X 업그레이드) → commit, 실패하면 abort 후 결과를 돌려준다. 바깥 루프는 `LM_ABORTED` 면 같은 ts 로 처음부터, `LM_TIMEOUT`(2 초) 이면 "멈춤" 으로 기록하고 큐를 덤프한다. 감사 스레드는 0..15 를 차례로 S 로 읽어 합을 낸다 — 2PL 이 직렬화 가능성을 보장하므로 감사가 보는 합은 **매번 정확히** 총액이어야 한다(이체 도중 상태를 보면 틀림).

`./solution` 6 번 실행 (실제 출력, 매번 51/51, 한 번 0.6–0.75 초):

```text
  bank: 8 threads x 3000 transfers + 300 audits: commits=24300 aborts=27030 deadlocks=27030 max_retries=10
  bank: 8 threads x 3000 transfers + 300 audits: commits=24300 aborts=26860 deadlocks=26860 max_retries=9
  bank: 8 threads x 3000 transfers + 300 audits: commits=24300 aborts=26580 deadlocks=26580 max_retries=9
  bank: 8 threads x 3000 transfers + 300 audits: commits=24300 aborts=26471 deadlocks=26471 max_retries=10
  bank: 8 threads x 3000 transfers + 300 audits: commits=24300 aborts=27168 deadlocks=27168 max_retries=8
  bank: 8 threads x 3000 transfers + 300 audits: commits=24300 aborts=28102 deadlocks=28102 max_retries=9
```

abort 수는 매번 다르지만(스케줄이 다르니까) **CHECK 하는 것은 매번 성립하는 것** 뿐이다: commit 24300, 시도 = commit + abort, abort = 사이클 수(사이클 하나에 희생자 하나, 희생자는 반드시 abort), 총액, 감사 정확, lock table 비움. commit 보다 abort 가 많다 — 16 계좌에 8 스레드가 "읽고 올리기" 를 하면 업그레이드 데드락이 거의 매번 난다. 이게 다음 데모의 출발점이다.

### Part C — 데모: 정책 · 재시도 ts · 접근 패턴 · 타임아웃

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o demo_bench && ./demo_bench   # solution.c 를 include
```

설정마다 1.2 초, 이체 스레드 8 + 감사 1, 계좌 16 (두 번 연속 실행 — 두 번째 실행의 머리줄 두 줄은 생략). 기계: Apple M6 (`hw.perflevel0/1/2.physicalcpu` = 2 / 4 / 6), Apple clang 21.0.0. 다른 에이전트 작업과 동시 실행이라 **load average 12–13** — 같은 설정도 두 번 사이에 2 배까지 흔들렸다. **비율과 경향만** 믿을 것. (xfer/s = 초당 커밋된 이체, abort/c = commit 당 abort, waste/c = commit 당 되돌린 쓰기, maxret = 이체 하나의 최대 재시도, >=5 = 5 번 이상 재시도한 이체 수, aud-max = 감사의 최대 재시도, tmo r/f = 타임아웃 때 실제로 사이클이 있었음/없었음)

```text
16:12  up 6 days, 16:33, 10 users, load averages: 12.50 9.30 6.00
config                         xfer/s  deadlock  abort/c  waste/c  maxret    >=5  audits aud-max   tmo r/f
youngest  keep-ts upgrade       27964     51370     1.42    0.558       9   1226    2425       6    0/0
youngest  new-ts  upgrade       35694     58625     1.28    0.426      41   3000    2813      24    0/0
fewest    keep-ts upgrade       16684    120674     3.79    1.797     751   4166   11683       5    0/0
leastwork keep-ts upgrade       54622     62946     0.92    0.059      25   2871    2332      41    0/0
youngest  keep-ts x-direct     107666     45221     0.34    0.229       7     19    4677       4    0/0
youngest  keep-ts ordered      855530         0     0.00    0.000       0      0   17634       0    0/0
timeout 1ms       x-direct       6195         0     0.74    0.304      18    245      72      21 2215/3340
timeout 10ms      x-direct        762         0     0.61    0.263      10     21       9      25  227/345
timeout 1ms       ordered     1106977         0     0.00    0.000       2      0   22372       1    0/26
16:12  up 6 days, 16:34, 10 users, load averages: 13.20 9.57 6.14
youngest  keep-ts upgrade       58686    108162     1.42    0.549      10   2377    5626       7    0/0
youngest  new-ts  upgrade       50845     81766     1.24    0.408      24   4118    4535      12    0/0
fewest    keep-ts upgrade       24074    105571     2.64    1.266     228   6525   11032       4    0/0
leastwork keep-ts upgrade       63973     72440     0.91    0.074      16   2627    2105      45    0/0
youngest  keep-ts x-direct     120467     51934     0.35    0.236       6     19    5237       4    0/0
youngest  keep-ts ordered      862250         0     0.00    0.000       0      0   16909       0    0/0
timeout 1ms       x-direct       6394         0     0.68    0.278      30    206      69      47 2308/2925
timeout 10ms      x-direct        745         0     0.61    0.233      11     20       9      11  266/287
timeout 1ms       ordered      786919         0     0.00    0.000       2      0   15713       1    0/46
```

```svg
<svg viewBox="0 0 740 318" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="18" fill="currentColor" font-weight="bold">초당 커밋된 이체 (두 번째 실행, 로그 눈금, load 13.2)</text>
<line x1="200" y1="30" x2="200" y2="262" stroke="currentColor"/>
<line x1="200" y1="262" x2="720" y2="262" stroke="currentColor"/>
<text x="196" y="278" fill="currentColor">100</text><text x="320" y="278" fill="currentColor">1k</text>
<text x="450" y="278" fill="currentColor">10k</text><text x="578" y="278" fill="currentColor">100k</text><text x="706" y="278" fill="currentColor">1M</text>
<line x1="330" y1="258" x2="330" y2="266" stroke="currentColor"/><line x1="460" y1="258" x2="460" y2="266" stroke="currentColor"/>
<line x1="590" y1="258" x2="590" y2="266" stroke="currentColor"/><line x1="720" y1="258" x2="720" y2="266" stroke="currentColor"/>
<text x="10" y="48" fill="currentColor">youngest keep upgrade</text><rect x="200" y="36" width="360" height="18" class="f-acc" stroke="currentColor"/><text x="566" y="50" fill="currentColor">58.7k</text>
<text x="10" y="76" fill="currentColor">youngest new-ts upgrade</text><rect x="200" y="64" width="352" height="18" class="f-acc" stroke="currentColor"/><text x="558" y="78" fill="currentColor">50.8k</text>
<text x="10" y="104" fill="currentColor">fewest-locks upgrade</text><rect x="200" y="92" width="310" height="18" class="f-warn" stroke="currentColor"/><text x="516" y="106" fill="currentColor">24.1k</text>
<text x="10" y="132" fill="currentColor">least-work upgrade</text><rect x="200" y="120" width="365" height="18" class="f-acc" stroke="currentColor"/><text x="571" y="134" fill="currentColor">64.0k</text>
<text x="10" y="160" fill="currentColor">youngest x-direct</text><rect x="200" y="148" width="401" height="18" class="f-acc" stroke="currentColor"/><text x="607" y="162" fill="currentColor">120k</text>
<text x="10" y="188" fill="currentColor">youngest ordered</text><rect x="200" y="176" width="512" height="18" class="f-ok" stroke="currentColor"/><text x="640" y="190" fill="currentColor">862k</text>
<text x="10" y="216" fill="currentColor">timeout 1ms x-direct</text><rect x="200" y="204" width="235" height="18" class="f-mut" stroke="currentColor"/><text x="441" y="218" fill="currentColor">6.4k</text>
<text x="10" y="244" fill="currentColor">timeout 10ms x-direct</text><rect x="200" y="232" width="113" height="18" class="f-mut" stroke="currentColor"/><text x="319" y="246" fill="currentColor">745</text>
<text x="10" y="294" fill="currentColor">ordered = 순환 대기 자체가 없음 (데드락 0).</text>
<text x="10" y="312" fill="currentColor">타임아웃만 = 사이클마다 시한만큼 멈춤 + 데드락 아닌 긴 대기도 abort.</text>
</svg>
```

읽는 법 (두 번 모두 같은 방향인 것만):

- **ts 유지 vs 새 ts**: 처리량은 비슷하지만 **최대 재시도** 9–10 vs 24–41, 5 번 이상 재시도한 이체 1226–2377 vs 3000–4118. 새 ts 는 꼬리(tail)를 키운다 = 굶주림 쪽으로.
- **fewest-locks**: 최악. abort/c 2.6–3.8, 최대 재시도 228–751. 감사는 락을 16 개 쥐므로 거의 희생되지 않고(감사 11k 건으로 가장 많음), 락 1–2 개 쥔 이체가 계속 죽는다.
- **least-work**: 되돌린 쓰기가 가장 적다(waste/c 0.06–0.07 vs 0.55). 대신 아무것도 안 쓰는 **감사가 늘 희생** 된다 — 감사 최대 재시도 41–45 (youngest 6–7). "가장 싼 희생자" 정책은 읽기 전용 긴 트랜잭션을 굶긴다.
- **접근 패턴이 정책보다 크다**: 처음부터 X 로 잡기(x-direct, `SELECT … FOR UPDATE`)가 업그레이드보다 2–4 배, 계좌 번호 순서로 잡기(ordered)는 데드락 0 으로 x-direct 의 7 배 이상. 순서를 정할 수 있으면 감지가 필요 없다 — OS-M10 의 해법 (1) 과 같다. 다만 DB 일반 트랜잭션은 순서를 정할 수 없다는 게 이 레슨의 출발점이다.
- **타임아웃만**: 처리량이 1–2 자릿수 떨어진다 (6.2–6.4k, 0.75k). 타임아웃 중 **실제 사이클이 있던 것은 40–48%**, 나머지는 그냥 오래 기다린 것(헛 abort). 데드락이 구조적으로 없는 ordered 에서도 1 ms 시한이면 26–46 번 헛 abort 했다.
- **감지 비용**: detect 를 켠 ordered(데드락 0, 그래도 막힐 때마다 64 큐 전체로 WFG 를 만든다) 와 timeout 1 ms ordered 의 차이는 첫 실행에서 1.29 배였지만 두 번째에선 거꾸로(0.91 배)여서 이 부하에서는 판단하지 않는다.

### Part C — 펌웨어 · 실제 시스템 연결

- **SSD FTL**: GC 가 블록 X 의 유효 페이지를 블록 Y 로 옮기면 L2P 엔트리, X 와 Y 의 valid count, (X 가 비면) free 풀이 함께 바뀐다 — 은행 이체와 같은 "여러 객체 원자 갱신" 이다. 호스트 쓰기 스레드는 반대 방향(L2P → 블록 정보)으로 잡기 쉬워 ABBA 가 생긴다. 펌웨어는 보통 **감지기를 두지 않는다**: 메모리·지연 예산이 작고, 롤백할 undo 를 RAM 에 둘 여유도 없다. 대신 ① 문서화된 락 순서(OS-M10, OS-M11 으로 검사) ② 메타데이터 파티션마다 한 코어가 소유하고 나머지는 메시지로 부탁(락 없음, NVMe 큐마다 코어 하나를 두는 구조와 잘 맞는다) ③ 전원 차단 대비 메타데이터 저널(STOR-M03/M04 의 WAL 과 같은 생각). 우선순위 상한(priority ceiling, RTOS-M05)도 순환 대기를 막는다.
- **DB**: 이 레슨의 lock manager 는 교과서형이다. 실제 엔진은 lock table 을 해시 파티션으로 나누고(래치가 여러 개 → 래치끼리는 순서로 교착을 막는다), 행 락을 페이지 비트맵으로 압축하고, 범위 잠금(next-key / predicate lock)으로 팬텀(phantom)을 막는다. MVCC(STOR-M09)를 쓰면 읽기는 락을 안 잡으므로 "상호 배제" 조건 자체가 읽기-쓰기 사이에서 없어진다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_asan && ./so_asan
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan
```

실제 결과: `51/51 passed`, exit 0 (6 번 연속). ASan+UBSan: 51/51, 보고 0. TSan: 51/51, 경고 0, exit 0. `demo_bench.c` 도 TSan 으로 한 번 돌렸다: 경고 0, 불변식 깨짐 0. TSan 이 깨끗한 이유: 계좌 값(`db->val`)은 2PL 락으로만 보호되는데, 락의 해제(release)와 부여(grant)가 모두 lock table 래치 `lm->mu` 안에서 일어나므로 "앞 트랜잭션의 쓰기 → mu 해제 → 다음 트랜잭션이 mu 를 잡고 부여받음 → 읽기" 로 happens-before 가 생긴다.

## 7. 자주 하는 실수

**1. abort 에서 먼저 풀고 나중에 되돌리기.** 푸는 순간 대기자가 X 를 받아 아직 되돌리지 않은 값(커밋 안 된 값)을 읽는다 — strict 2PL 을 지켜 놓고 abort 에서 깨는 셈이다.

```c
/* 틀림 */  lm_release_all(db->lm, t->id); for (i = t->nundo - 1; i >= 0; i--) db->val[...] = ...;
/* 맞음 */  for (int i = t->nundo - 1; i >= 0; i--) db->val[t->undo[i].obj] = t->undo[i].old;
            t->nundo = 0; lm_release_all(db->lm, t->id);
```

**2. undo 를 앞에서부터 적용.** 같은 객체를 두 번 쓰면(100→105→112) 앞에서부터 되돌리면 마지막에 105 가 남는다. **거꾸로** 적용해야 처음 값이 마지막에 쓰인다.

**3. 재시도 때 새 타임스탬프.** youngest 정책의 굶주림 방지가 사라진다. 데모: 최대 재시도 9–10 → 24–41.

```c
for (int retry = 0;; retry++) { tx_begin(db, &t, id, atomic_fetch_add(&g_ts, 1)); ... }   /* 틀림 */
uint64_t ts = atomic_fetch_add(&g_ts, 1); for (;;) { tx_begin(db, &t, id, ts); ... }       /* 맞음 */
```

**4. 업그레이드를 새 요청처럼 처리.** S 를 쥔 채 X 엔트리를 하나 더 넣으면 그 X 는 자기 S 와 비호환 → 자기 자신을 기다린다. 혼자 쥔 S 도 영원히 못 올린다. 같은 엔트리의 `want` 만 X 로 바꾸고, "다른" 보유자만 센다(`holders_except(q, tx)`).

**5. 새치기 허용.** `compat_with_holders` 만 보고 S 를 바로 주면 X 대기자가 굶는다. 새 요청은 `!has_waiter(q)` 일 때만 즉시 부여하고, `grant_waiters` 는 처음 막히는 곳에서 `return`(continue 아님).

```c
if (!e->held && compat_with_holders(q, e->tx, e->want)) grant(e); else continue;   /* 틀림: 건너뜀 */
if (!compat_with_holders(q, e->tx, e->want)) return;                               /* 맞음: FIFO */
```

**6. 희생자 하나 고르고 끝.** 희생자가 요청자가 아니면 요청자를 지나는 사이클이 또 있을 수 있다(요청자가 두 사이클에 걸쳐 있으면). `detect_locked` 는 사이클이 없을 때까지 반복한다.

**7. 기다리는 동안 lock table 래치를 쥐고 있기.** 데이터 락을 기다리면서 `lm->mu` 를 놓지 않으면 아무도 락을 못 풀어서 전체가 멈춘다. `pthread_cond_timedwait` 가 기다리는 동안 mu 를 놓는다. 그리고 시한은 `CLOCK_REALTIME` 기준이다 — 이 맥(macOS)에는 `pthread_condattr_setclock` 이 없어서 MONOTONIC 시한을 줄 수 없다.

## 8. 면접 Q&A

<details><summary>Q1. 2PL 은 왜 직렬화 가능성(serializability)을 보장하나요?</summary>

각 트랜잭션의 **lock point**(마지막 락을 잡은 순간)로 순서를 매기면, 충돌하는 두 연산(같은 객체, 적어도 하나가 쓰기)은 항상 이 순서와 같은 방향으로 실행된다. Ti 가 먼저 x 를 잡았고 Tj 가 비호환 모드로 x 를 원하면 Tj 는 Ti 가 x 를 풀 때까지 기다리는데, Ti 는 풀기 시작한 뒤로 아무것도 안 잡으므로 Ti 의 lock point 가 Tj 의 것보다 앞이다. 그러므로 충돌 그래프(precedence graph)에 사이클이 없다 → **충돌 직렬화 가능**(conflict-serializable). (Eswaran, Gray, Lorie, Traiger 1976.) 꼬리: "직렬화 가능한 스케줄을 2PL 이 전부 허용하나?" → 아니다, 2PL 은 충분조건이다. 꼬리 2: "그럼 팬텀은?" → 행 락만으론 "조건을 만족하는 행 집합" 을 못 잠근다. 술어 락(predicate lock) 또는 범위 잠금(next-key)이 필요하다.
</details>

<details><summary>Q2. strict 2PL 과 그냥 2PL 의 차이, cascading abort 는 왜 생기나요?</summary>

basic 2PL 은 lock point 뒤에 일찍 풀어도 된다. 그러면 남이 아직 커밋 안 된 값을 읽을 수 있고(dirty read), 쓴 쪽이 abort 하면 읽은 쪽도 abort 해야 한다(연쇄 abort). 읽은 쪽이 이미 커밋했으면 **회복 불가능**. 이 레슨의 강제 스케줄: T2 가 A=150 을 읽고 커밋 → T1 abort → A=100. 쓰기였다면 T2 의 커밋된 +10 이 T1 의 before-image 복원에 덮였다. strict 2PL = X 락을 commit/abort 까지 유지 → 둘 다 불가능. rigorous(SS2PL) = S 까지 유지 → 커밋 순서 = 직렬화 순서라 분산·복제에서 편하다. 이 구현은 rigorous. 꼬리: "S 를 일찍 풀면 무슨 문제?" → 연쇄 abort 는 없지만 커밋 순서와 직렬화 순서가 달라질 수 있다.
</details>

<details><summary>Q3. 교착의 4 가지 조건과, DB 는 그중 무엇을 깨나요?</summary>

Coffman, Elphick, Shoshani (1971): 상호 배제 · 점유 대기(hold and wait) · 비선점(no preemption) · 순환 대기(circular wait). 넷 다 성립해야 교착 (자세한 표와 철학자 해법은 OS-M10). DB 쪽 대응: ① 상호 배제 — MVCC 로 읽기가 락을 안 잡게(STOR-M09) ② 점유 대기 — **conservative 2PL**: 시작할 때 필요한 락을 전부(한꺼번에) 잡거나 하나도 안 잡음, 하지만 접근 집합을 미리 알아야 함 ③ 비선점 — **감지 후 abort**(이 레슨), wound-wait 도 늙은 쪽이 젊은 쪽의 락을 빼앗는 선점 ④ 순환 대기 — 전역 순서(ordered, 데모에서 데드락 0), wait-die/wound-wait 은 대기 간선을 ts 한 방향으로만 허락해서 사이클을 원천 차단. 꼬리: "왜 DB 는 OS 처럼 락 순서를 안 쓰나?" → 어떤 행을 잠글지는 실행 중에 데이터를 읽어야 안다 (WHERE 조건, 인덱스 탐색). 순서를 강제할 수 없으니 abort 로 비선점을 깨는 쪽을 택한다. 그 대가가 undo 로그.
</details>

<details><summary>Q4. wait-for 그래프는 어떻게 만들고, 언제 검사하나요?</summary>

대기자 → (비호환 보유자, 업그레이드 대기자, 앞선 비호환 대기자). 검사는 막히는 순간 막힌 노드에서 DFS — 그 전까지 사이클이 없었으면 새 사이클은 반드시 그 노드를 지나므로 충분하고, O(V+E). 대안: 잠깐 기다렸다 검사(Postgres `deadlock_timeout`, 기본 1s로 알고 있음 — 대부분의 대기는 그 전에 끝나므로 검사 비용을 아낀다), 주기적 검사(SQL Server 의 lock monitor), 타임아웃만. 데모: 타임아웃만 쓰면 처리량이 1–2 자릿수 떨어지고 타임아웃 중 실제 사이클은 절반 이하. 꼬리: "검사 비용이 문제가 되면?" → 고경합에서 InnoDB 가 감지를 끄고 `innodb_lock_wait_timeout` 에 맡기는 선택지를 준다(문서 기억, 버전 확인 필요). 꼬리 2: "분산 DB 에선?" → 전역 WFG 를 모으기 어렵다 → 타임아웃, wound-wait(Spanner 가 wound-wait 을 쓴다고 알려져 있음 — 확인 필요), 경로 밀기(edge chasing).
</details>

<details><summary>Q5. 희생자는 어떻게 고르고, 굶주림(starvation)은 어떻게 막나요?</summary>

후보: 가장 젊은(되돌릴 일이 적을 가능성), 보유 락이 가장 적은(풀었을 때 영향이 적은), 쓴 일이 가장 적은(롤백이 싼). InnoDB 는 "더 작은" 트랜잭션(수정한 행 수 등의 가중치)을 고른다고 문서에 있다(기억에 의존). 굶주림 방지 = **재시작해도 원래 ts 유지** + youngest: 결국 가장 늙어져서 희생되지 않는다. 데모(실측, load 12–13): ts 유지 최대 재시도 9–10 vs 새 ts 24–41; fewest-locks 는 락 적은 이체만 계속 죽어 228–751; least-work 는 롤백 비용이 가장 작지만(waste 0.06 vs 0.55) 아무것도 안 쓰는 감사가 늘 희생(최대 41–45). 꼬리: "least-work 를 쓰면서 굶주림을 막으려면?" → 재시도 횟수를 키에 더하거나, 일정 횟수 이상 희생된 트랜잭션은 면제.
</details>

<details><summary>Q6. wait-die 와 wound-wait 의 차이는? 감지와 비교하면?</summary>

둘 다 ts 로 **예방**한다 (Rosenkrantz, Stearns, Lewis 1978). wait-die: 늙은 요청자는 기다리고 젊은 요청자는 죽는다(자기 abort) → 대기 간선은 늙은→젊은 방향만. wound-wait: 늙은 요청자는 젊은 보유자를 다치게(abort) 하고 젊은 요청자는 기다린다 → 간선은 젊은→늙은 방향만. 어느 쪽이든 간선이 ts 한 방향이라 사이클이 불가능하다. 재시작해도 ts 유지 → 굶주림 없음. 대가: 데드락이 아닌데도 abort 한다(연습 1: 그냥 기다리면 될 상황에서 wait-die 가 T2 를 죽임). 연습 1 실측(300 스크립트 × 5 트랜잭션, 무작위 스케줄): 예방 없음 = 291 개에서 사이클, wait-die abort 8381, wound-wait abort 1943 — wait-die 는 젊은 쪽이 같은 늙은 쪽에 부딪혀 계속 죽는다. 꼬리: "wound 당한 쪽이 이미 커밋 중이면?" → 보통 커밋 단계에 들어간 트랜잭션은 다치지 않게 한다.
</details>

<details><summary>Q7. 이체에서 abort 가 commit 보다 많던데요? 어떻게 줄이나요?</summary>

"읽고(S) 나서 쓰기(X)" 는 둘이 같은 행을 읽으면 반드시 업그레이드 데드락이다. 해결: ① 처음부터 X 로 읽기(`SELECT … FOR UPDATE`) — 데모 abort/c 1.42 → 0.35, 처리량 2–4 배 ② **U(update) 락**: U 는 S 와는 호환, U 끼리는 비호환 → 두 번째 쓰기 의도자는 읽는 순간 기다린다(연습 4) ③ 접근 순서를 정할 수 있으면 정렬해서 잡기 — 데드락 0, x-direct 의 7 배+. 꼬리: "U 와 S 의 호환은 대칭인가?" → 시스템마다 다르다 (U 보유 중 새 S 를 막는 구현도 있다 — 확인 필요).
</details>

<details><summary>Q8. lock 과 latch 는 어떻게 다르고, 이 lock manager 의 mutex 는 왜 데드락이 안 나나요?</summary>

lock = 트랜잭션이 논리적 데이터(행, 키)를 commit 까지 잡는 것, 모드가 여럿, 교착 감지·abort 가 있다. latch = 스레드가 메모리 구조(페이지, lock table 버킷)를 아주 짧게 잡는 mutex/rwlock, **감지기가 없으므로 순서로만** 교착을 막는다 (STOR-M10 B+tree crabbing). 여기서 `lm->mu` 는 latch 하나뿐이고, 그 안에서 다른 것을 기다리지 않는다(데이터 락 대기는 condvar 가 mu 를 놓고 잔다) → 순환이 생길 수 없다. 꼬리: "lock table 을 해시 파티션 래치 여러 개로 나누면?" → 한 요청이 여러 파티션을 잡는 경우(감지기가 전체 그래프를 볼 때) 파티션 번호 순으로 잡는다 — Postgres 가 deadlock 검사 때 모든 파티션 락을 순서대로 잡는다고 알고 있다(확인 필요).
</details>

<details><summary>Q9. lockdep 같은 런타임 락 순서 검사, SCC 와는 어떤 관계인가요?</summary>

셋 다 "그래프의 사이클" 이지만 그래프가 다르다. 이 레슨의 WFG 는 **지금 기다리는 인스턴스** 사이의 그래프 → 실제로 일어난 데드락만 본다. lockdep(OS-M11)은 락 **클래스** 사이의 "A 를 쥔 채 B 를 잡은 적 있음" 그래프 → 두 스레드가 순차로 돌아 실제로 막힌 적이 없어도 **잠재적** 교착을 보고한다; 새 간선마다 도달성 검사. SCC(ALGO-A10)는 기록된 로그 전체로 만든 그래프에서 모든 사이클 묶음을 한 번에(Tarjan/Kosaraju O(V+E)). DB 에 lockdep 을 못 쓰는 이유: 락 대상이 데이터(행)라 클래스가 무한하고, 순서가 데이터에 따라 바뀌며, 잠재적 순환은 "버그" 가 아니라 정상 동작(감지·abort 로 처리)이다. 반대로 펌웨어 FTL 락은 클래스가 몇 개로 고정이라 lockdep/SCC 로 검사하기 딱 좋다.
</details>

<details><summary>Q10. 펌웨어(SSD) 에서는 이 문제를 어떻게 다루나요?</summary>

GC 와 호스트 쓰기가 L2P · 블록 정보 · free 풀을 다른 순서로 잡으면 ABBA. 펌웨어는 대개 감지+롤백을 쓰지 않는다: RAM·지연 예산이 작고, 실시간 명령 처리 중 롤백은 비싸다. 대신 문서화된 락 순서(디버그 빌드에서 순서 검사기 = OS-M11), 메타데이터 소유 코어 + 메시지 전달(NVMe 큐마다 코어), 짧은 임계 구역은 인터럽트 금지(C-M14 — ISR 은 mutex 를 기다릴 수 없음), RTOS 우선순위 상한(RTOS-M05). 그래도 "멈춤" 은 워치독이 마지막 선점 수단. 다만 전원 차단 복구는 DB 와 똑같이 저널 + redo/undo (STOR-M03/M04). 꼬리: "이체 불변식 같은 걸 FTL 에서 검사하려면?" → 전체 valid count 합 = L2P 의 유효 매핑 수 — 스트레스 테스트의 감사 트랜잭션처럼 일관된 스냅샷에서 비교해야 한다.
</details>

## 9. 추가 연습

### 연습 1 — wait-die / wound-wait (예방)

`ww_decide(policy, req_ts, blk_ts)` 로 충돌 시 할 일(기다림 / 자기 abort / 상대 abort)을 정하고, X 락만 있는 작은 step 모드 lock table 에서 무작위 스크립트 300 개(트랜잭션 5 개 × 서로 다른 객체 3 개, 객체 4 개, 무작위 스케줄)를 돌린다. 매 걸음 ① 모든 대기 간선이 정책이 허락하는 ts 방향인지 ② 사이클이 없는지 확인하고, 같은 ts 로 재시작해 모두 커밋되는지 본다.

<details><summary>답</summary>

wait-die: `req_ts < blk_ts ? WAIT : DIE`. wound-wait: `req_ts < blk_ts ? WOUND : WAIT`. 다친 쪽은 락을 즉시 회수당하고(이 모델엔 데이터가 없음) 다음 차례에 abort 를 알게 된다 — 이미 락 3 개를 다 잡고 커밋 직전이어도. 실제 출력:

```text
  300 scripts x 5 txns: wait     -> cycles 291, done 9
                        wait-die -> cycles 0, done 300, aborts 8381
                      wound-wait -> cycles 0, done 300, aborts 1943
```

간선이 ts 한 방향이면 경로를 따라 ts 가 단조롭게 변하므로 시작점으로 돌아올 수 없다 = 사이클 불가능. 간선 집합이 늘지 않는 이유: X 만 있고 업그레이드가 없으면, 대기자의 blocker 는 요청 시점에 앞에 있던 엔트리의 부분집합으로만 남는다. wait-die 가 abort 가 4 배 많은 것은 젊은 쪽이 같은 늙은 쪽에 부딪힐 때마다 죽고 다시 와서 또 죽기 때문이다.
</details>

### 연습 2 — 타임아웃 감지의 트레이드오프 (논리 시계)

시나리오 L: T1 이 A 를 12 tick 동안 쥐고 정상 커밋, T2 는 tick 1 부터 기다림(데드락 아님). 시나리오 D: tick 2 에 ABBA. 타임아웃 T = 5 와 20 에서 L 의 헛 abort 여부와 D 가 풀리는 tick 을 구하라.

<details><summary>답</summary>

T=5: L 에서 T2 는 6 tick 째 기다릴 때 abort → **헛 abort**(데드락이 아닌데 일을 버림). D 는 tick 8 에 풀림. T=20: L 은 tick 12 에 정상적으로 받음, D 는 tick 23 까지 **방치**. 짧게 잡으면 헛 abort, 길게 잡으면 데드락 동안 시스템이 그 락들을 못 쓴다. 데모의 실측도 같은 방향: 1 ms 타임아웃에서 타임아웃의 56–60% 가 사이클 없는 대기였고, 데드락이 원천적으로 없는 ordered 에서도 헛 abort 26–46 번. 10 ms 로 늘리면 헛 abort 비율은 비슷한데 처리량이 8 배 줄었다(6.4k → 0.75k). 그래서 실무 DB 는 타임아웃을 "감지 대신" 이 아니라 "감지를 언제 돌릴지"(Postgres) 또는 "감지를 껐을 때 최후 수단"(InnoDB) 으로 쓴다.
</details>

### 연습 3 — 의도 락(intention lock) 호환성 행렬

테이블 전체를 S 로 잠그는 스캔과 행 하나를 X 로 잠그는 갱신이 공존하려면 계층(테이블 → 페이지 → 행)마다 의도 락 IS / IX / SIX 가 필요하다. 5×5 호환성 행렬과 "자식을 이 모드로 잠그려면 부모에 필요한 모드" 를 구현하라.

<details><summary>답</summary>

행(held) / 열(want) 순서 IS IX S SIX X: IS = 1 1 1 1 0, IX = 1 1 0 0 0, S = 1 0 1 0 0, SIX = 1 0 0 0 0, X = 0 0 0 0 0 (Gray et al. 1976, 대칭). 부모 모드: 자식 S/IS → 부모 IS, 자식 X/IX/SIX → 부모 IX. 확인: 다른 행을 고치는 중(테이블 IX)에 행 읽기(테이블 IS) → 호환. 테이블 전체 읽기(S) 중 행 수정(테이블 IX) → 비호환이라 기다림. SIX = "전체를 읽으면서 일부 행을 고친다"(S + IX). 이 계층에서도 데드락 감지는 같다 — 대기 간선이 테이블 노드 큐에서 생길 뿐.
</details>

### 연습 4 — U(update) 락으로 업그레이드 데드락 없애기

모드 S / U / X: U 는 S 와 호환, U 끼리는 비호환, X 는 모두와 비호환. "S 로 읽고 X 로 올리기" 를 두 트랜잭션이 하면 사이클이 생기는 것을, "U 로 읽고 X 로 올리기" 에서는 생기지 않는 것을 보여라.

<details><summary>답</summary>

S 판: T1 S, T2 S → T1 X 대기, T2 X 대기 → `has_cycle` = true. U 판: T1 U, T2 U 는 **읽는 시점에** 대기 → T1 은 혼자 U 이므로 X 로 즉시 업그레이드 → 커밋 → T2 U → X. 사이클 없음. FIFO 주의: T2(U 대기) 뒤의 T3(S)는 T1 의 U 와 호환이지만 앞선 대기자 때문에 기다린다 — 그래서 이 엔진의 blocker 는 앞선 대기자를 **호환 여부와 관계없이** 포함한다(S/X 만 있을 땐 비호환만 세도 충분했지만, S-U 처럼 호환인데 막히는 쌍이 생기면 아니다). SQL Server 가 U 를 이 용도로 쓰는 것으로 알려져 있다(U-S 호환 방향은 구현마다 다를 수 있음).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_san && ./pr_san
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan
```

실제 출력: `26/26 passed` (세 빌드 모두, 보고 0, 경고 0).

## 10. 핵심 정리

- lock manager = 호환성 행렬(S-S 만 호환) + 객체별 FIFO 큐(새치기 없음) + 업그레이드 대기자 우선. 업그레이드는 같은 엔트리의 `want` 를 X 로.
- 2PL(풀기 시작하면 더 안 잡음) ⇒ 충돌 직렬화 가능. strict(X 를 commit 까지) ⇒ dirty read · 연쇄 abort · 커밋된 쓰기 소실 없음. 이 구현은 전부 commit 까지 = rigorous.
- WFG 간선: 비호환 보유자 + (일반 대기자면) 업그레이드 대기자 + 앞선 비호환 대기자. 막히는 순간 그 노드에서 DFS 하면 충분(새 간선은 전부 그 노드에서 나감), O(V+E).
- 희생자: 대기 취소 → 희생자가 undo 를 **거꾸로** 적용 → 그다음 해제 → **같은 ts** 로 재시도. 실측 최대 재시도 9–10 (새 ts 24–41). fewest-locks 는 이체를, least-work 는 읽기 전용 감사를 굶긴다.
- DB 는 순서를 미리 정할 수 없어서 "비선점" 을 롤백으로 깬다. 순서를 정할 수 있으면 정렬이 최고(데드락 0, 7 배+). 타임아웃만 쓰면 헛 abort 와 늦은 해소 사이에서 처리량이 1–2 자릿수 떨어진다.
- 예방형: wait-die(늙은 쪽만 기다림) / wound-wait(늙은 쪽이 선점) — 간선이 ts 한 방향이라 사이클 불가능, 대신 데드락 아닌 상황에서도 abort.
- 업그레이드 데드락은 `SELECT … FOR UPDATE`(X 로 바로) 또는 U 락으로 없앤다.
- 같은 순환 대기: OS-M10(예방), OS-M11(런타임 클래스 그래프), ALGO-A10(오프라인 SCC), STOR-M10(래치 = 순서뿐), SOCX-M05(CDG), DCN-M05(PFC). 펌웨어 FTL 은 감지 대신 순서·소유 코어·워치독.

## 참고자료

- E. G. Coffman, M. J. Elphick, A. Shoshani, "System Deadlocks", *ACM Computing Surveys* 3(2), June 1971 — 4 가지 조건
- R. Arpaci-Dusseau, A. Arpaci-Dusseau, *Operating Systems: Three Easy Pieces*, 32장 *Common Concurrency Problems* (v0.91, Don 의 PDF p.379–393): <https://pages.cs.wisc.edu/~remzi/OSTEP/threads-bugs.pdf>
- K. P. Eswaran, J. N. Gray, R. A. Lorie, I. L. Traiger, "The Notions of Consistency and Predicate Locks in a Database System", *CACM* 19(11), 1976 — 2PL ⇒ 직렬화 가능, 팬텀과 술어 락
- J. N. Gray, R. A. Lorie, G. R. Putzolu, I. L. Traiger, "Granularity of Locks and Degrees of Consistency in a Shared Data Base", 1976 — 의도 락 IS/IX/SIX
- D. J. Rosenkrantz, R. E. Stearns, P. M. Lewis II, "System Level Concurrency Control for Distributed Database Systems", *ACM TODS* 3(2), 1978 — wait-die, wound-wait
- P. A. Bernstein, V. Hadzilacos, N. Goodman, *Concurrency Control and Recovery in Database Systems*, Addison-Wesley 1987 (저자 공개 PDF) — 3장 2PL, 데드락, strict/rigorous
- J. Gray, A. Reuter, *Transaction Processing: Concepts and Techniques*, 1992 — lock manager 구현, 7–8장
- CMU 15-445/645 (A. Pavlo) 강의 "Two-Phase Locking" — deadlock detection/prevention, 희생자 선택, lock granularity: <https://15445.courses.cs.cmu.edu/>
- PostgreSQL 문서: `deadlock_timeout` (Lock Management 설정), Explicit Locking › Deadlocks; 소스 `src/backend/storage/lmgr/README`, `deadlock.c` (hard/soft edge) — 세부는 확인 필요
- MySQL 문서: InnoDB Deadlock Detection (`innodb_deadlock_detect`), `innodb_lock_wait_timeout` — 기본값은 버전별 확인
- Microsoft 문서: SQL Server Transaction Locking and Row Versioning Guide — update(U) 락, lock monitor, `DEADLOCK_PRIORITY`
- 관련 레슨: OS-M10 (Coffman 표, 철학자, 락 순서), OS-M11 (mini lockdep), ALGO-A10 (SCC), STOR-M10 (latch crabbing), SOCX-M05 (CDG), DCN-M05 (PFC 데드락), STOR-M03/M04 (WAL, ARIES 복구), STOR-M09 (MVCC), OS-B06 (condvar 규칙), RTOS-M05 (priority ceiling), C-M14 (ISR 은 mutex 를 기다릴 수 없음), TOOL-M09 (TSan)
