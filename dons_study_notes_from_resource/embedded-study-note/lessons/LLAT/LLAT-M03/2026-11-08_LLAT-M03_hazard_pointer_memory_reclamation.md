---
id: LLAT-M03
time: 95분
updated: 2026-11-08
---
# LLAT-M03 lock-free 메모리 회수: Hazard Pointer

## 학습 목표

- lock-free 구조에서 "떼어낸 노드를 바로 `free` 하면 왜 안 되나" 를 **강제 스케줄 3 가지** (head 를 쥔 채 멈춤 / load 와 게시 사이 / next 를 읽은 뒤) 로 재현하고, ASan 의 `heap-use-after-free` 보고서를 읽는다.
- Michael(2004) 의 hazard pointer(HP)를 직접 짠다: 스레드별 HP 칸(128 B 패딩), **게시 → 재확인(publish-then-revalidate)** 의 `protect`, retire 목록, 길이 `R` 에서 `scan`. 그리고 Michael–Scott 큐 dequeue 에 HP 두 개를 넣는다.
- 게시(store) 뒤 재확인(load) 이 **store→load 순서** 라서 `seq_cst` 가 필요하다는 것을 SB(store buffering) 모양으로 설명하고, arm64 `stlr; ldar` vs `stlr; ldapr`, Cortex-M4 `dmb sy` 하나의 차이를 실제 기계어로 보인다.
- 미해제 노드의 **상한** (스레드당 ≤ R, 전체 ≤ N·R) 을 유도하고, 그 상한으로 MCU 정적 노드 풀 크기를 정해 할당 실패 0 을 보인다. EBR(epoch)은 멈춘 스레드 하나에 상한 없이 늘어난다는 것을 실측(25 만 노드 vs 32 노드)으로 비교한다.
- 읽기 경로 비용(노드마다 게시 vs 구간당 한 번)을 숫자로 말하고, C++26 `std::hazard_pointer` / `std::rcu`, folly hazptr 의 위치를 안다.

## 1. 문제

"lock-free 큐에서 꺼낸 노드는 언제 `free` 해도 되나요?" — lock-free 자료구조 면접의 마지막 꼬리질문이고, 대부분 여기서 막힌다. 락이 있으면 "락을 풀기 전에 아무도 못 본다" 로 끝나지만, lock-free 에서는 **다른 스레드가 방금 읽은 포인터를 아직 손에 쥐고 있을 수 있다.** 펌웨어에서도 같다: 듀얼 코어 MCU 에서 코어 간 메시지 큐를 lock-free 로 만들면 `malloc` 없이 **정적 노드 풀** 을 써야 하고, 그 풀이 "절대 모자라지 않는 크기" 를 컴파일 시점에 정해야 한다 — 회수 방식이 상한을 주지 않으면 불가능하다.

이 레슨은 **Michael–Scott 큐** (M-S 큐, 1996; 연결 리스트 + 더미 노드 + head/tail CAS) 를 무대로 쓴다. lock-free 연결 리스트(Harris–Michael) 대신 큐를 고른 이유: (1) HP 가 스레드당 정확히 2 개로 고정되고 삭제 표시(mark bit)가 필요 없어 **회수 문제만** 남는다, (2) Michael 의 2004 논문이 HP 를 바로 이 큐에 적용해 보인다, (3) 면접에서 가장 자주 나오는 lock-free 큐다. 배열 기반 bounded MPMC(Vyukov) 는 슬롯을 재사용할 뿐 노드를 해제하지 않아 이 문제가 없다 — 그쪽은 DS-A03. ABA 를 정면으로 다루는 Treiber 스택은 DS-A04 (여기선 연습 1 에서 "HP 가 ABA 도 막는다" 만 본다).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">use-after-free 창: 스레드 0 이 head 포인터 A 를 읽고 역참조하기 전에 멈춘다</text>
  <text x="10" y="50" fill="currentColor" font-weight="bold">스레드 0</text>
  <text x="10" y="150" fill="currentColor" font-weight="bold">스레드 1</text>
  <path d="M90 45 L720 45 M90 145 L720 145" stroke="currentColor" stroke-dasharray="3 4"/>
  <rect x="100" y="30" width="130" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="108" y="50" fill="currentColor" font-family="monospace">h = head (A)</text>
  <rect x="240" y="30" width="230" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="290" y="50" fill="currentColor">… 선점 / 멈춤 …</text>
  <rect x="480" y="30" width="230" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="488" y="50" fill="currentColor" font-family="monospace">next = h-&gt;next   ← UAF</text>
  <rect x="250" y="130" width="100" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="258" y="150" fill="currentColor" font-family="monospace">CAS head</text>
  <rect x="360" y="130" width="100" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="372" y="150" fill="currentColor" font-family="monospace">free(A)</text>
  <path d="M410 128 L560 66" stroke="currentColor" stroke-width="1.5" fill="none"/>
  <path d="M549 66 L560 66 L553 75" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <text x="10" y="200" fill="currentColor">HP 가 하는 일: 스레드 0 이 "A 를 쓰는 중" 을 공개 칸(hazard pointer)에 먼저 적는다.</text>
  <text x="10" y="222" fill="currentColor">스레드 1 은 free 대신 retire(A) → 목록에 모았다가 scan 때 "누가 A 를 적어 뒀나?" 를 확인.</text>
  <text x="10" y="244" fill="currentColor">적혀 있으면 남기고, 아무도 안 적었으면 그제야 해제. 남은 노드 수는 스레드당 R 이하로 묶인다.</text>
  <rect x="10" y="260" width="16" height="14" class="f-warn" stroke="currentColor"/>
  <text x="32" y="272" fill="currentColor">= 해제된 메모리를 읽는 지점 / 해제 지점</text>
</svg>
```

| 회수 방식 | 리더 비용 | 미해제 메모리 상한 | 멈춘 스레드 하나의 영향 | 이 과정에서 |
|---|---|---|---|---|
| 해제 안 함 (누수) | 0 | 없음 (계속 증가) | — | — |
| 참조 카운트 (노드마다) | 노드마다 RMW 2 번 + 카운트 자체의 회수 문제 | 있음 | 없음 | 언급만 |
| EBR / RCU (epoch) | 구간당 store 1–2 번 | **없음** | 전체 회수 정지 | OS-A03 |
| QSBR | 루프당 store 1 번 | 없음 | 전체 회수 정지 | OS-A03 Part C |
| **hazard pointer** | **노드마다** 게시 + 재확인 | **N·R** | 자기가 쥔 K 개만 붙잡힘 | **이 레슨** |

### Part A — HP 도메인과 M-S 큐 dequeue

- `hp_set(d, tid, k, p)` — 내 칸 `k` 에 `p` 를 게시. `hp_protect(d, tid, k, src)` — `*src` 를 load → 게시 → **다시 load 해서 같으면** 반환, 다르면 반복 (그 사이에 `HOOK(HK_LOADED)`).
- `hp_retire(d, tid, p)` — 내 retire 목록에 넣고 길이가 `R` 이면 `hp_scan`. `hp_scan(d, tid)` — 모든 스레드의 HP 칸을 모아 목록에서 **보호되지 않은 노드만** 해제, 해제 개수 반환.
- `msq_dequeue` — hp0 = head(`h`), hp1 = `h->next`(`next`). `next` 를 게시한 뒤 **"`h` 가 아직 head 인가"** 로 재확인. 성공하면 HP 를 지우고 `h` 를 retire. (`msq_enqueue` 는 tail 을 hp0 으로 보호하는 예로 주어진다.)
- 단위 테스트 + 4 생산자 × 4 소비자 20 만 메시지: 정확히 한 번, 생산자별 순서 유지, 해제된 노드 읽기 0, 목록 길이 ≤ R, 끝에 남는 노드 = 더미 1 개. **ASan 빌드로 같은 스트레스가 깨끗** 한 것이 "UAF 없음" 의 증거다.

### Part B — 어려운 부분: UAF 창 3 개를 강제로 열기

- B1: 스레드 0 이 hp0 = A 게시 후 멈춤 → 스레드 1 이 A 를 꺼내 retire → scan → **A 는 남는다.** B2: load 와 게시 **사이** 에 멈춤 → A 는 진짜 해제된다 → 재확인이 실패해 다시 읽는다. B3: `next`(B) 를 읽고 hp1 게시 전에 멈춤 → B 도 해제된다 → "`h` 가 아직 head?" 재확인이 막는다.
- B4: 스레드 하나가 HP 를 쥔 채 영원히 멈춰도, 다른 스레드가 1 000 개를 retire 하는 동안 미해제 노드 ≤ R.
- `demo_uaf.c`: HP 없는 dequeue + 즉시 `free` 를 진짜 스레드 2 개로 같은 스케줄에 돌려 ASan 보고서를 본다.

### Part C — 펌웨어: 상한으로 정적 풀 크기 정하기

- `hp_pool_bound(nthreads, k, R, qcap)` — 동시에 살아 있을 수 있는 노드 수의 최댓값 (잘못된 설정 `R ≤ H` 면 0).
- `bq_push` / `bq_pop` — 크레딧(`inflight < cap`)으로 큐 길이를 묶는다. 풀 = 상한 **정확히**, 소비자 하나가 64 번에 한 번 dequeue 도중 50 µs 멈춤 → 할당 실패 0.

## 2. Problem (English)

> **Safe memory reclamation with hazard pointers.** A Michael–Scott queue (dummy head, CAS on `head`/`tail`) must free dequeued nodes while other threads may still hold pointers to them.
>
> A. Implement a hazard-pointer domain with `K = 2` slots per thread (each thread's slots on their own 128-byte line): `void hp_set(hp_dom_t*, int tid, int k, node_t*)`, `node_t *hp_protect(hp_dom_t*, int tid, int k, _Atomic(node_t*) *src)` (publish, then re-read `*src`; retry until stable), `void hp_retire(hp_dom_t*, int tid, node_t*)` (per-thread list; scan when its length reaches `R`, `R > H = N·K`), `int hp_scan(hp_dom_t*, int tid)` (free every retired node not equal to any published hazard pointer). Use them in `bool msq_dequeue(msq_t*, int tid, uint64_t *out)`. 4 producers × 4 consumers × 50 000 messages: each message exactly once, per-producer FIFO, no read of a freed node, clean under ASan and TSan.
>
> B. Using a test hook, force three schedules: the reader paused (1) after publishing `head`, (2) between loading `head` and publishing it, (3) after loading `head->next` but before publishing it. Show no use-after-free in each. Show that a thread stalled forever while holding a hazard pointer pins at most `R` nodes of another thread's list.
>
> C. On an MCU without `malloc`, size a static node pool: `long hp_pool_bound(int nthreads, int k, int R, int qcap)`; bound the queue length with credits (`bq_push`/`bq_pop`). With the pool set to exactly the bound and one consumer stalling repeatedly mid-dequeue, allocation must never fail.
>
> Follow-ups: Why re-read after publishing? Why must the publish be `seq_cst` (or followed by a full fence) while `hp_clear` can be `release`? Why is the validation for `next` "head still equals `h`" rather than "`h->next` still equals `next`"? What is the memory bound and why `R = 2H`? How does this compare with epoch-based reclamation when one reader stalls? Does it also solve ABA?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력, 0.09 초, 멈추지 않는다, exit 1):

```text
FAIL starter.c:386  hp_protect(&g_d, 1, 0, &src) == a && atomic_load(&g_d.slot[1].hp[0]) == a
FAIL starter.c:388  g_d.rec[0].n == 4 && atomic_load(&g_nfreed_log) == 0
...
SKIP 5 stress checks (Part A): unit checks failed
FAIL starter.c:437  g_t1_got[0] == 1 && ok && v == 2
FAIL starter.c:438  g_d.rec[1].nscan == 1 && !was_freed(idA)
FAIL starter.c:439  atomic_load(&g_uaf) == 0
FAIL starter.c:453  ok && v == 2 && g_t1_got[0] == 1 && atomic_load(&g_uaf) == 0
FAIL starter.c:454  g_hook_calls[HK_LOADED] == 2
FAIL starter.c:466  was_freed(idB3) && atomic_load(&g_uaf) == 0
FAIL starter.c:481  !was_freed(idH) && g_d.rec[1].nretired >= 1000
...
SKIP 3 stress checks (Part C): unit checks failed
17/41 passed
```

시작 코드는 **"HP 없이 즉시 free"** 하는 순진한 버전이다 (`hp_retire` = 바로 `reclaim`, `hp_protect` = 그냥 load, dequeue 에 재확인 없음). 단일 스레드에선 큐가 멀쩡히 돌아가지만 강제 스케줄 테스트에서 `g_uaf` 가 올라간다.

하네스 구조 (OS-A03 와 같은 방식):

- **해제 감지 두 단계.** 기본 빌드는 정적 풀 — 해제 = `state = FREED` + 독 칠하기(`next = NULL`, `val = 0xDEAD…`), 메모리는 남는다. 큐 코드는 노드를 반드시 `nd_next()` / `nd_val()` 로 읽고, 이 접근자가 FREED 노드를 읽으면 `g_uaf++` ("ASan-lite"). ASan 빌드(`__has_feature(address_sanitizer)`)는 자동으로 진짜 `malloc/free` 로 바뀌어 같은 읽기가 ASan 보고가 된다. 풀은 FIFO 로 재사용해 해제된 노드가 최대한 늦게 다시 쓰이게 했다.
- **강제 스케줄 = 단일 OS 스레드.** `HOOK(pt, tid)` 지점에서 테스트 훅이 "스레드 1" 의 dequeue 를 그 자리에서 끝까지 돌린다 (tid 1 의 HP 칸·retire 목록 사용). 실제 스레드 없이 순서가 100 % 고정되므로 ASan 빌드에서도 같은 결과다.
- 해제 여부는 포인터가 아니라 **할당 순번 id 기록** (`was_freed(id)`) 으로 본다 (free 된 포인터 값은 C11 6.2.4p2 에 따라 불확정).
- 새니타이저 빌드는 스트레스 메시지 수를 1/2 로 줄인다 (`SAN_SCALE`): ASan 0.2 초, TSan 0.8 초.

관찰용 파일 (모두 `solution.c` 를 `main` 이름만 바꿔 포함):

```bash
cc -std=c11 -O2 -pthread -DMODE=0 demo_uaf.c -o uaf0 && ./uaf0                       # 즉시 free: 해제된 A 를 읽음
cc -std=c11 -g -O1 -fno-inline -fsanitize=address -fno-omit-frame-pointer -pthread -DMODE=0 demo_uaf.c -o uaf0a && ./uaf0a
cc -std=c11 -g -O1 -fno-inline -fsanitize=address -fno-omit-frame-pointer -pthread -DMODE=1 demo_uaf.c -o uaf1a && ./uaf1a
cc -std=c11 -O2 -S demo_asm.c -o -                                                    # 게시-재확인 arm64 기계어
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_asm.c -o -
cc -std=c11 -O2 -pthread demo_bench.c -o bench && ./bench                             # HP / EBR / mutex 비교 (~10 초)
```

## 4. 힌트

<details><summary>힌트 1 — 게시만으로는 왜 부족한가</summary>

`p = load(head); hp = p;` 만 하면, load 와 게시 **사이** 에 다른 스레드가 `p` 를 떼어내고 scan 까지 끝낼 수 있다 — 그 scan 은 아직 빈 칸을 봤으니 `p` 를 해제한다. 게시는 "앞으로의 scan" 만 막는다. 그러면 "게시가 늦지 않았다" 는 것을 어떻게 확인하나? 게시 **뒤에** 무엇을 다시 읽어 보면 될까?
</details>

<details><summary>힌트 2 — 재확인이 증명하는 것</summary>

게시 후 `head` 를 다시 읽어 여전히 `p` 라면: 게시 시점에 `p` 는 아직 큐에 있었다 (떼어내지지 않았다). 떼어낸 뒤에야 retire 되고, retire 뒤의 scan 은 내 게시를 **반드시 본다** — 단, 그 "반드시" 에는 메모리 순서 조건이 붙는다 (힌트 3).
</details>

<details><summary>힌트 3 — store → load 순서</summary>

리더: "HP 칸 store → head load". 회수자: "head CAS(떼어냄) → HP 칸 load". 각자 store 후 상대 것을 load — C-A03 / OS-A03 의 **SB 모양** 이다. 둘 다 상대의 store 를 못 보면 "리더는 옛 head 를 확인했고, 회수자는 빈 칸을 봤다" → 해제 → UAF. release/acquire 는 SB 를 금지하지 않는다.
</details>

<details><summary>힌트 4 — next 의 재확인 대상</summary>

`next` 는 `h->next` 에서 왔다. M-S 큐에서 `h->next` 는 NULL → 값 한 번만 바뀌고 그 뒤 **변하지 않는다.** 그러니 `h->next` 를 다시 읽으면 항상 같다 — 아무것도 증명하지 못한다. `next` 가 아직 큐 안에 있다는 것은 무엇으로 알 수 있나? (`h` 가 아직 head 면 `h` 뒤의 `next` 도 아직 안 떼어졌다.)
</details>

<details><summary>힌트 5 — retire 순서와 R</summary>

dequeue 성공 후 retire 하기 **전에** 내 HP 를 지워야 한다 — 안 그러면 내 scan 이 내가 쥔 `h` 를 보고 남긴다 (정확성 문제는 아니지만 한 바퀴 늦어진다). `R ≤ H` 이면 목록이 전부 보호된 노드일 수 있어 scan 이 하나도 못 풀 수 있다 → 매 retire 마다 scan (연습 4).
</details>

<details><summary>힌트 6 — 풀 상한 세기</summary>

살아 있는 노드는 세 곳 중 하나다: 큐 안 (더미 1 + 값, 크레딧으로 ≤ qcap), 어떤 스레드의 retire 목록 안 (스레드당 ≤ R), 그리고 "떼어냈지만 아직 retire 전" 의 순간 상태. 마지막 것은 목록 길이가 R−1 이하일 때만 생긴다는 점을 이용하라.
</details>

## 5. 풀이

### 0. 필요한 규칙 (자세한 건 C-A03 · OS-A03 · MCPP-M13)

- **게시(publish)** 와 happens-before: `enqueue` 가 `t->next` CAS(release 이상)로 새 노드를 연결하고, dequeue 가 `h->next` 를 acquire 로 읽으면 `val` 이 보인다 (C-A03 의 1칸 채널).
- **SB(store buffering)**: 두 스레드가 각자 store 후 상대 위치를 load 할 때 "둘 다 옛값" 을 금지하려면 양쪽 모두 `seq_cst` (또는 store 와 load 사이 seq_cst fence) 여야 한다 (C11 7.17.3, OS-A03 Part A 에서 실측).
- **ABA** 의 정면 처리 = DS-A04. 참조 카운트 계열은 이 레슨 범위 밖.

### Part A — protect: 게시 → 재확인

```c
static node_t *hp_protect(hp_dom_t *d, int tid, int k, _Atomic(node_t *) *src) {
    node_t *p = atomic_load_explicit(src, memory_order_relaxed);
    for (;;) {
        HOOK(HK_LOADED, tid);
        hp_set(d, tid, k, p);                                            /* ① 게시 (seq_cst store) */
        node_t *q = atomic_load_explicit(src, memory_order_seq_cst);     /* ② 재확인 (seq_cst load) */
        if (q == p) return p;
        p = q;
    }
}
```

왜 이것이 안전한가 (회수자는 "떼어냄(CAS, seq_cst) → retire → scan 에서 HP 칸을 seq_cst load"):

1. seq_cst 연산들은 하나의 전체 순서 S 에 놓인다. ② 가 `p` 를 읽었다 = S 에서 ② 는 `p` 를 떼어내는 CAS 보다 앞이다 (그 CAS 뒤라면 ② 는 새 head 를 봤을 것).
2. ① 은 ② 보다 앞 (같은 스레드, sequenced-before). 회수자의 scan load 는 그 CAS 보다 뒤.
3. 그러므로 S 에서 ① < ② < CAS < scan load → scan load 는 ① 이나 그 뒤의 값을 읽는다 → `p` 를 본다 (리더가 아직 안 지웠다면) → 해제 안 함.
4. 리더가 다 쓰고 `hp_clear` (release store NULL) 하고, scan 이 그 NULL 을 (acquire 이상으로) 읽으면 리더의 모든 읽기 → 해제 의 happens-before 가 생긴다 → 안전하게 해제. 그래서 **지우기는 release 면 충분** 하다.

`relaxed` 첫 load 는 괜찮다: 돌려주는 값은 ② (seq_cst ⊇ acquire) 가 확인한 값이고, 노드 내용(`val`, `next`)의 가시성은 ② 가 읽은 store (head CAS) 까지 이어진 release 사슬로 보장된다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">게시-재확인과 scan 의 SB 모양 — 금지해야 할 결과: 리더 ② = A 그리고 회수자 scan = 빈 칸</text>
  <text x="30" y="52" fill="currentColor" font-weight="bold">리더 (스레드 0)</text>
  <text x="430" y="52" fill="currentColor" font-weight="bold">회수자 (스레드 1)</text>
  <rect x="30" y="66" width="260" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="40" y="88" fill="currentColor" font-family="monospace">① hp[0] = A      (store)</text>
  <rect x="30" y="140" width="260" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="40" y="162" fill="currentColor" font-family="monospace">② q = head  == A ? (load)</text>
  <rect x="430" y="66" width="270" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="440" y="88" fill="currentColor" font-family="monospace">CAS head A→B, retire(A) (store)</text>
  <rect x="430" y="140" width="270" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="440" y="162" fill="currentColor" font-family="monospace">scan: h = hp[0]  (load)</text>
  <path d="M160 100 L160 136" stroke="currentColor" stroke-width="2"/>
  <path d="M154 128 L160 138 L166 128" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M565 100 L565 136" stroke="currentColor" stroke-width="2"/>
  <path d="M559 128 L565 138 L571 128" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M290 158 L430 84" stroke="currentColor" stroke-dasharray="5 4" fill="none"/>
  <path d="M430 158 L290 84" stroke="currentColor" stroke-dasharray="5 4" fill="none"/>
  <text x="300" y="128" fill="currentColor">서로의 store 를</text>
  <text x="300" y="144" fill="currentColor">못 보면 → UAF</text>
  <g fill="currentColor">
    <text x="30" y="210">release ① + acquire ②  : arm64 stlr ; ldapr  — ldapr 가 앞의 stlr 를 앞지를 수 있다 (RCpc) → 금지 못 함</text>
    <text x="30" y="232">seq_cst ① + seq_cst ② : arm64 stlr ; ldar   — ldar 는 앞의 stlr 를 못 앞지른다 (RCsc) → 금지</text>
    <text x="30" y="254">relaxed ① + fence + ② : arm64 str ; dmb ish ; ldapr — Michael 논문의 "membar" 형태</text>
    <text x="30" y="276">Cortex-M4 seq_cst     : dmb sy ; str ; dmb sy ; ldr  — 가운데 dmb sy 가 SB 를 막는다</text>
    <text x="30" y="298">회수자 쪽도 같다: 떼어냄 CAS(seq_cst) 와 scan 의 HP load(seq_cst) 사이 순서가 필요</text>
  </g>
</svg>
```

<!-- code: demo_asm.c -->

실제 기계어 (`cc -std=c11 -O2 -S demo_asm.c -o -`, Apple clang 21, 루프 본문만):

```text
_hp_protect_sc:                 _hp_protect_ra:                 _hp_protect_fence:
	mov	x0, x10                  	mov	x0, x10                  	mov	x0, x10
	stlr	x10, [x9]     ; 게시     	stlr	x10, [x9]                	str	x10, [x9]
	ldar	x10, [x8]     ; 재확인   	ldapr	x10, [x8]   ← 한 글자    	dmb	ish
	cmp	x0, x10                  	cmp	x0, x10                  	ldapr	x10, [x8]
	b.ne	LBB0_1                   	b.ne	LBB1_1                   	cmp	x0, x10
_scan_load:  ldar x0, [x8]                                          	b.ne	LBB2_1
```

(세 열은 실제 출력의 세 함수를 나란히 놓은 것.) 눈여겨볼 점: **seq_cst store 와 release store 는 둘 다 `stlr`** 이다. 차이는 뒤따르는 load 에 있다 — `ldar`(RCsc: 앞선 `stlr` 보다 먼저 관측될 수 없음) vs `ldapr`(RCpc: 앞지를 수 있음). "store 를 seq_cst 로" 라는 말은 arm64 에선 사실상 "**뒤의 load 를 ldar 로**" 라는 뜻이다. Cortex-M4 (`--target=thumbv7em-none-eabi`, 4 배 펼친 루프의 한 바퀴):

```text
hp_protect_sc:  dmb sy ; str.w r3,[r12] ; dmb sy ; ldr r2,[r1] ; mov ; cmp ; dmb sy
hp_protect_ra:  dmb sy ; str   r3,[r2]  ;          ldr.w r1,[r12] ; mov ; cmp ; dmb sy
hp_protect_fence:        str   r3,[r2]  ; dmb sy ; ldr.w r1,[r12] ; mov ; cmp ; dmb sy
```

`_ra` 에는 str 과 ldr 사이 `dmb` 가 **없다.** 단일 코어 Cortex-M 은 자기 store 를 자기가 순서대로 보므로 문제가 안 되지만, 듀얼 코어 (RP2350 의 Cortex-M33 ×2 등, 문서 기준) 에선 이 차이가 그대로 UAF 창이다.

**실측: 약하게 하면 깨지나?** `hp_set` 을 release, 재확인 두 곳을 acquire 로 바꾼 변형을 4×4 스트레스 200 만 메시지 × 10 회 (`uptime` load 9.2) 돌렸다 — **g_uaf 0, 모든 CHECK 통과.** 이 하네스에선 한 번도 안 깨졌다. 그러나 같은 SB 모양을 장벽식 리트머스로 좁혀 돌린 OS-A03 은 rel/acq 양쪽에서 **1 200 만 라운드 중 2 번** 금지 결과를 실제로 봤다. 0 은 증명이 아니다 — 큐 스트레스는 "load 와 게시 사이에 정확히 그 노드가 떼어지고 scan 까지 끝나는" 좁은 창을 거의 만들지 못할 뿐이다. 그래서 순서는 테스트가 아니라 **증명(위 1–4)** 으로 고른다.

### Part A — retire 와 scan, 그리고 R

```c
static void hp_retire(hp_dom_t *d, int tid, node_t *p) {
    hp_rec_t *r = &d->rec[tid];
    r->list[r->n++] = p;                 /* 목록은 이 스레드만 만진다 → 원자 연산 불필요 */
    r->nretired++;
    if (r->n > r->nmax) r->nmax = r->n;
    if (r->n >= d->R) hp_scan(d, tid);
}
```

`hp_scan` 은 (1) 모든 스레드의 HP 칸 `N·K` 개를 seq_cst load 로 모아 `plist` 를 만들고, (2) 내 목록의 각 노드가 `plist` 에 있으면 남기고 없으면 해제한다. 선형 탐색이면 `O(R·H)`, 정렬 + 이분 탐색이면 `O((R+H) log H)` (연습 2), Michael 논문은 해시 집합으로 기대 `O(R)` (문서 기준).

| 기호 | 뜻 | 이 레슨 |
|---|---|---|
| N | 스레드 수 | 스트레스 8, Part C 6 |
| K | 스레드당 HP 수 | 2 (head, next) |
| H = N·K | 전체 HP 칸 수 = **보호 중일 수 있는 서로 다른 노드 수의 최댓값** | 16, 12 |
| R | scan 문턱 (목록 길이) | 2H = 32, 24 |

**상한 유도.** scan 직후 목록에 남는 노드는 모두 누군가의 HP 가 가리키는 서로 다른 노드라 ≤ H. R > H 이면 scan 직후 길이 ≤ H < R 이고, 다음 scan 은 길이가 R 이 될 때 일어나므로 **목록 길이는 언제나 ≤ R.** 전체 미해제 노드 ≤ **N·R** (= N·k·H = k·K·N², 스레드 수의 제곱에 비례). R = 2H 이면 scan 한 번이 최소 R − H = H 개를 풀어 **retire 하나당 상각 O(1)** scan 비용 (연습 4: R = H+1 이면 1 000 retire 에 scan 1 000 번, R = 2H 면 125 번 — 실측). Michael 논문이 R = H + Ω(H) 를 권하는 이유다 (문서 기준).

### Part A — dequeue 에 HP 두 개

```c
node_t *h = hp_protect(d, tid, 0, &q->head);          /* hp0 = h: 이제 h->next 읽기는 안전 */
node_t *t = atomic_load_explicit(&q->tail, memory_order_acquire);   /* tail 은 역참조 안 함 → HP 불필요 */
node_t *next = nd_next(h);
hp_set(d, tid, 1, next);                               /* hp1 = next */
if (atomic_load_explicit(&q->head, memory_order_seq_cst) != h) continue;   /* 재확인 */
...
uint64_t v = nd_val(next);                             /* CAS 전에 읽는다 */
if (CAS(&q->head, h → next)) { hp_clear_all(d, tid); hp_retire(d, tid, h); *out = v; return true; }
```

| 질문 | 답 |
|---|---|
| 왜 hp1 의 재확인이 `h->next == next` 가 아니라 `head == h` 인가 | `h->next` 는 한 번 정해지면 안 바뀐다 → 다시 읽으면 늘 같다 → 아무것도 증명 못 함. "`h` 가 아직 head" 면 `h` 뒤의 `next` 는 아직 떼어지지 않았다 → 이후 retire 될 때 scan 이 hp1 을 본다 (B3 테스트) |
| 왜 `val` 을 CAS **전에** 읽나 | CAS 성공 직후 `next` 는 새 더미가 되고, 다른 소비자가 곧 그것을 떼어내 retire 할 수 있다. hp1 이 지키는 동안 읽어 둔다 |
| 왜 tail 은 보호 안 하나 | dequeue 는 `t` 를 비교·CAS 값으로만 쓰고 역참조하지 않는다. enqueue 는 `t->next` 를 읽으므로 hp0 으로 보호한다 |
| 왜 retire 전에 HP 를 지우나 | 내 scan 이 내 hp0(= h) 를 보면 h 를 남긴다 → 한 바퀴 늦게 풀린다. 지운 뒤 retire |
| HP 가 ABA 도 막나 | 보호 중인 노드는 해제도 재사용도 안 된다 → "같은 주소의 다른 노드" 가 생길 수 없다 → 이 큐의 head CAS 는 ABA 에서 안전 (연습 1, 자세히는 DS-A04) |

스트레스 결과 (실제 출력, 8 회 연속 41/41): `stress A: 200000 msgs, retired 46446, scans 1454 (thread 4)` — 소비자 하나가 4.6 만 개를 retire 하며 scan 1 454 번 (≈ 32 개마다 한 번 = R). TSan 빌드 (`-fsanitize=thread`): 41/41, 보고 없음. ASan+UBSan 빌드: 41/41, 보고 없음.

### Part B — 강제 스케줄 3 개

| 테스트 | 스레드 0 이 멈춘 곳 | 스레드 1 이 그 사이에 | 시작 코드 (즉시 free) | 정답 |
|---|---|---|---|---|
| B1 | hp0 = A 게시 **후** (`HK_DEQ_HEAD`) | A 꺼냄 → retire → scan | A 해제 → `A->next` 읽기 = UAF, 빈 큐로 오판 | scan 이 A 를 남김, 스레드 0 은 재시도해 2 를 얻음 |
| B2 | load 와 게시 **사이** (`HK_LOADED`) | A 꺼냄 → scan → A 진짜 해제 | (protect 없음) | 게시 후 재확인 실패 → 다시 load → B 보호 (훅 호출 2 번) |
| B3 | `next`(B) 읽은 뒤 hp1 게시 **전** (`HK_DEQ_NEXT`) | 두 개 꺼냄 → B 도 해제 | `B->val` 읽기 = UAF | `head != h` 재확인 → 재시도 → 3 |
| B4 | hp0 = 더미를 쥐고 영원히 | enqueue/dequeue 1 000 번 | 쥔 노드가 해제됨 | 쥔 노드만 남고 미해제 ≤ R(=5) |

B2 가 핵심이다: **스레드 0 이 쥔 포인터는 이미 해제된 메모리를 가리키는데도 정답 코드는 안전하다** — 해제된 포인터 값을 HP 칸에 쓰고 `head` 와 비교만 할 뿐, 역참조 전에 재확인이 실패하기 때문이다. (엄밀히는 수명이 끝난 객체의 포인터 값을 읽는 것 자체가 C11 6.2.4p2 의 불확정 값 문제다 — 실무 구현은 모두 이렇게 하며, C/C++ 위원회의 "pointer lifetime-end zap" 논의(P2414 계열, 문서 기준)가 이 패턴을 합법화하려는 시도다.)

<!-- code: demo_uaf.c -->

`demo_uaf.c` — 진짜 스레드 2 개, 같은 B1 스케줄, `MODE=0` (HP 없는 dequeue + 즉시 free). ASan 없이 `-O2` (2 회 같음, 실제 출력):

```text
mode 0 (no HP, free now): head dummy A = 0x100699150
writer: dequeued 1, scans=0 freed=0, A freed
writer: new node 77 at 0x100699150 (SAME address as A)
reader: read A->next = 0x0 from freed memory, head changed -> retry
reader: dequeue -> got 2
```

**`malloc` 이 해제된 A 자리를 노드 77 에 그대로 재사용했다.** 리더는 "A 의 next" 라며 실은 77 의 `next`(NULL)를 읽었고, 우연히 `head != h` 검사가 재시도시켜 결과는 맞았다. 결과가 맞아도 버그다 — 페이지가 반환된 할당기(큰 할당의 `munmap`, MCU 의 MPU 보호 영역)에선 바로 fault 이고, 다른 구조(스택 pop)에선 같은 재사용이 ABA 로 리스트를 망가뜨린다 (연습 1). ASan (`-fsanitize=address`, 그림자 바이트 표 생략, exit 134):

```text
==3258==ERROR: AddressSanitizer: heap-use-after-free on address 0x603000000fa0 at pc 0x000104f7032c bp 0x00016af1aef0 sp 0x00016af1aee8
READ of size 8 at 0x603000000fa0 thread T1
    #0 0x000104f70328 in naive_dequeue demo_uaf.c:37
    #1 0x000104f6f768 in reader demo_uaf.c:58
    ...
0x603000000fa0 is located 0 bytes inside of 24-byte region [0x603000000fa0,0x603000000fb8)
freed by thread T0 here:
    #0 0x0001056c5258 in free+0x7c (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x41258)
    #1 0x000104f702e0 in naive_dequeue demo_uaf.c:47
    #2 0x000104f6f5a4 in main demo_uaf.c:75
previously allocated by thread T0 here:
    #0 0x0001056c5164 in malloc+0x78 (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x41164)
    #1 0x000104f6e06c in node_alloc solution.c:83
    #2 0x000104f6e8d4 in msq_init solution.c:222
    #3 0x000104f6f504 in main demo_uaf.c:65
SUMMARY: AddressSanitizer: heap-use-after-free demo_uaf.c:37 in naive_dequeue
```

읽는 법: `READ of size 8 ... thread T1` = 리더 스레드가 `h->next` (8 바이트 포인터, demo_uaf.c:37) 를 읽음. `0 bytes inside of 24-byte region` = `node_t` 맨 앞 필드 `next`. `freed by thread T0 ... naive_dequeue demo_uaf.c:47` = 메인 스레드의 즉시 해제 (`node_free` 는 끝에서 `free` 로 꼬리 호출돼 프레임이 안 보인다). `previously allocated ... msq_init` = 처음의 더미 노드. ASan 은 해제된 블록을 격리(quarantine)해 바로 재사용하지 않으므로 77 이 그 자리에 오지 않고 `fd`(freed) 그림자로 잡힌다. `MODE=1` (HP) 같은 스케줄, ASan 빌드, exit 0:

```text
mode 1 (hazard pointers): head dummy A = 0x603000000fa0
writer: dequeued 1, scans=1 freed=4, A kept (hp0)
writer: new node 77 at 0x603000001120 (not A)
reader: dequeue -> got 2
done: live nodes 3
```

scan 이 채워 둔 4 개는 풀고 A 는 남겼다 (`freed=4, A kept`).

### Part C — 상한으로 정적 풀 크기 정하기

```c
static long hp_pool_bound(int nthreads, int k, int R, int qcap) {
    if (R <= nthreads * k) return 0;          /* R ≤ H: 목록이 전부 보호 노드일 수 있음 → 상한 R 이 안 지켜짐 */
    return 1 + (long)qcap + (long)nthreads * R;
}
```

| 살아 있는 노드가 있을 수 있는 곳 | 최대 개수 | 근거 |
|---|---|---|
| 큐의 더미 | 1 | M-S 큐 불변식 |
| 큐 안 값 + 할당했지만 아직 연결 전 | qcap | 생산자는 크레딧을 **먼저** 얻고 할당 → 둘 다 크레딧으로 셈 |
| 스레드별 retire 목록 + "떼어냈지만 retire 전" | 스레드당 R | 떼어냄은 목록 길이 ≤ R−1 일 때만 일어나고 (직전 retire 뒤 scan 이 H < R 로 줄였거나 아직 R 미만), 곧 그 목록에 들어간다 |
| HP 가 보호 중인 노드 | 0 (추가) | 보호 중인 노드는 위 세 곳 중 하나에 이미 있다 |

N = 6, K = 2, R = 2H = 24, qcap = 64 → **209 노드**. 크레딧 큐는 "꺼낸 **뒤에** 크레딧 반납" — 꺼내자마자 반납하면 그 노드(이제 더미)가 크레딧 밖에서 살아 있게 된다. 스트레스 (3 생산자 + 3 소비자, 소비자 tid 4 는 64 번에 한 번 dequeue 도중 hp0 을 쥔 채 50 µs `nanosleep`, 풀 = 기존 노드 + 209 정확히):

```text
stress C: pool bound 209 nodes, 75000 msgs, stalls 146
41/41 passed
```

할당 실패 0, 정확히 한 번, 순서 유지. **EBR 로는 이 표를 만들 수 없다** — 멈춘 리더 하나가 grace period 를 막는 동안 retire 된 노드 전부가 남는다 (아래 B4 실측: 200 ms 에 25 만 개). MCU 펌웨어에서 HP 의 진짜 가치는 속도가 아니라 이 **컴파일 시점 상한** 이다. 단일 코어 MCU 에서 ISR ↔ main 사이라면 보통 이렇게까지 하지 않는다 — IRQ 를 잠깐 끄거나(C-M14) SPSC 링(DS-A01)을 쓴다. HP 가 의미 있는 곳은 CAS 가 되는 듀얼 코어 (Cortex-M33 ×2, Xtensa ×2 등 — 칩마다 확인, 문서 기준; RP2040 의 M0+ 는 LDREX/STREX 가 없어 이 큐 자체가 안 된다) 와 Cortex-A + RTOS 다.

### 실측 — HP vs 최소 EBR vs mutex 큐 (`demo_bench.c`)

<!-- code: demo_bench.c -->

기계: `sysctl -n machdep.cpu.brand_string hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel2.physicalcpu` → `Apple M6`, `2`, `4`, `6` (Super 2 + Performance 4 + Efficiency 6), `hw.cachelinesize` = 128, `cc --version` → `Apple clang version 21.0.0 (clang-2100.0.123.102)`. 실행 직전·직후 `uptime` load **4.55 / 5.62** (다른 레슨 벤치마크 여러 개와 동시 실행, 코어 고정 불가) → **절대 ns 가 아니라 배수만** 볼 것. 노드는 진짜 `malloc/free`. EBR 은 OS-A03 의 전체 구현이 아니라 비교용 최소판 (전역 epoch, 슬롯 `(e<<1)|1` seq_cst store, 20 retire 마다 전진 시도, 해제 조건 `e+2 ≤ G`).

| B1 읽기 경로 (단일 스레드, 5 회 중앙값) | 값 | plain 대비 |
|---|---|---|
| plain acquire load + 역참조 | 0.30 ns/op | 1.0× |
| HP protect + clear | 0.57 ns/op | 1.9× |
| EBR enter + load + exit | 0.47 ns/op | 1.6× |
| 64 노드 리스트 순회: plain | 32.4 ns (0.51 ns/노드) | 1.0× |
| 64 노드 순회: **HP 노드마다 protect** (두 칸 번갈아) | 46.8 ns (0.73 ns/노드) | **1.44×** |
| 64 노드 순회: EBR 순회당 enter 1 번 | 33.8 ns (0.53 ns/노드) | 1.04× |

| 큐 (5 회 중앙값, 괄호 = 최소–최대) | HP | EBR | mutex |
|---|---|---|---|
| B2 단일 스레드 enq+deq 1 쌍 | 14.6 ns | 11.5 ns | 13.4 ns |
| B3 1P1C 처리량 (Mops/s) | 13.2 (12.4–14.0) | 14.6 (13.6–15.7) | 10.9 (8.3–11.3) |
| B3 2P2C | 7.0 (6.3–7.9) | 6.2 (6.2–8.4) | 6.3 (5.4–7.6) |
| B3 4P4C | 5.6 (5.4–7.4) | 5.8 (5.6–6.9) | 5.2 (4.9–5.2) |

같은 측정을 load 8–12 에서 세 번 더 돌렸을 때: B1 순회의 HP/plain 배수는 1.50× · 1.77× · 1.51×, EBR/plain 은 1.04× · 1.07× · 0.98× 로 **방향이 안정적** 이었고, B2·B3 은 순위가 실행마다 바뀌었다 (B2 가 HP 30 / EBR 41 / mutex 37 ns 로 뒤집힌 실행도 있음).

읽는 법:

1. **HP 의 비용은 "노드마다" 다.** 한 번 읽고 끝나는 경로(protect 1 번)는 EBR 과 비슷하고 둘 다 1 ns 미만 — 경합 없는 자기 캐시 라인에 `stlr` 하나, 뒤의 `ldar` 가 그것을 기다리는 값이다. 그러나 순회처럼 노드를 **여러 개 건너가면** HP 는 노드마다 `stlr; ldar` 를 내고 EBR 은 구간 입구에서 한 번만 낸다 → 1.44× vs 1.04×. 긴 리스트·트리 탐색에서 HP 가 불리하고 EBR/RCU 가 유리한 이유다.
2. **M-S 큐 처리량에선 회수 방식이 병목이 아니다.** 셋 다 2P2C 이상에서 5–7 Mops/s 로 비슷하고 스레드를 늘리면 오히려 떨어진다 — 병목은 head/tail 두 캐시 라인을 모든 스레드가 CAS 하는 경합이다 (그래서 이 Mac 의 mutex 큐도 비슷하다). **처리량 표로 HP 와 EBR 의 우열을 주장하면 안 된다** — 확실한 차이는 B1 순회와 B4 메모리다.

### 실측 — 멈춘 스레드 하나가 붙잡는 메모리 (B4)

2 생산자 + 2 소비자가 계속 돌고, 다섯 번째 스레드가 t = 50~250 ms 동안 HP 하나를 head 에 쥔 채 (HP) / 읽기 구간 안에서 (EBR) 멈춘다. 25 ms 마다 미해제 노드 수 (Σretire − Σfree) 를 찍었다 (위와 같은 실행, 실제 출력 일부):

```text
B4 unreclaimed nodes, 2P2C + 1 stalled thread (stall 50..250 ms), HP R=20 -> bound N*R=100
  t(ms)        HP        EBR
     50        19         33
     75        23      49528
    150        31     169925
    250        32     255425
    275        25         43
    325        32        206
    375        28       6961
    400        17       7346
```

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">멈춘 스레드(50~250 ms) 동안 미해제 노드 — demo_bench.c B4 실측</text>
  <path d="M80 260 L690 260" stroke="currentColor"/>
  <path d="M80 260 L80 50" stroke="currentColor"/>
  <path d="M80 193.3 L690 193.3 M80 126.7 L690 126.7 M80 60 L690 60" stroke="currentColor" stroke-dasharray="2 4" fill="none"/>
  <text x="62" y="264" fill="currentColor">0</text>
  <text x="34" y="197" fill="currentColor">100 k</text>
  <text x="34" y="131" fill="currentColor">200 k</text>
  <text x="34" y="64" fill="currentColor">300 k</text>
  <text x="76" y="278" fill="currentColor">0</text>
  <text x="220" y="278" fill="currentColor">100</text>
  <text x="370" y="278" fill="currentColor">200</text>
  <text x="520" y="278" fill="currentColor">300</text>
  <text x="664" y="278" fill="currentColor">400 ms</text>
  <rect x="155" y="50" width="300" height="210" class="f-mut" opacity="0.35"/>
  <text x="240" y="44" fill="currentColor">다섯 번째 스레드 멈춤</text>
  <polyline fill="none" stroke="currentColor" stroke-width="2.5" points="117.5,260 155,260 192.5,227 230,186.6 267.5,167.5 305,146.7 342.5,129 380,114.2 417.5,99.6 455,89.7 492.5,260 530,260 567.5,259.9 605,260 642.5,255.4 680,255.1"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4" points="117.5,259 680,259"/>
  <text x="470" y="74" fill="currentColor">EBR: 25.5 만 노드 (≈ 6 MB)</text>
  <text x="470" y="90" fill="currentColor">≈ 소비자 dequeue 속도 × 멈춘 시간</text>
  <text x="470" y="238" fill="currentColor">HP: 최대 32 (상한 N·R = 100)</text>
  <text x="470" y="254" fill="currentColor">멈춘 스레드는 노드 1 개만 붙잡음</text>
  <text x="80" y="300" fill="currentColor">풀린 직후 다음 전진에서 EBR 도 한꺼번에 해제 (t = 275 ms 에 43)</text>
</svg>
```

EBR 은 **멈춘 시간에 정비례** 해서 늘고 (≈ 128 만 노드/s × 0.2 s, 24 B 노드 + limbo 기록 16 B), HP 는 멈춤과 무관하게 10~32 에서 논다. 덤으로 본 것: 멈춤이 끝난 뒤인 t = 375–400 ms 에도 EBR 이 7 000 개로 튀었다 (load 9 의 다른 실행에선 14 026) — **선점된 스레드도 "멈춘 리더"** 다 (읽기 구간 안에서 스케줄러에 밀리면 grace period 가 그만큼 늦어진다). HP 표에는 이런 튐이 없다.

| | hazard pointer | EBR (OS-A03) |
|---|---|---|
| 읽기 경로 | 노드마다 seq_cst store + 재확인 load (순회 1.44×) | 구간당 seq_cst store 1 번 (순회 1.04×) |
| 미해제 상한 | **N·R** (컴파일 시점에 앎) | 없음 — 멈춘 리더 하나가 전체를 막음 |
| 멈춘 스레드 영향 | 자기가 쥔 K 개만 | 그 뒤 retire 된 전부 |
| 해제 지연 | scan 때 (R 개마다) | grace period (모든 리더가 한 번 나간 뒤) |
| 쓰기 쪽 비용 | scan = H 개 load + 비교 | epoch 전진 = 리더 슬롯 N 개 load |
| 코드 침투 | 구조마다 "무엇을 재확인할지" 설계 필요 (dequeue 의 `head == h`) | 구간 표시만 (구조 무관) |
| 잘 맞는 곳 | 큐·스택처럼 짧은 연산, 메모리 상한이 필수인 곳 | 읽기 다수 테이블·긴 순회 (커널 RCU) |

### 표준과 라이브러리 (문서 기준, 일부 미검증)

- **C++26**: `<hazard_pointer>` (`std::hazard_pointer_obj_base<T>`, `std::make_hazard_pointer()`, `hp.protect(src)`, `obj->retire()`) 와 `<rcu>` (`std::rcu_obj_base`, `std::rcu_domain` 을 lock/unlock 해 읽기 구간, `std::rcu_synchronize`, `std::rcu_retire`) 가 작업 초안에 들어갔다 (P2530 · P2545, 2023 년 채택으로 알고 있음). 이 Mac 의 libc++ (`MacOSX26.4.sdk/usr/include/c++/v1/version`) 에는 `__cpp_lib_hazard_pointer 202306L` / `__cpp_lib_rcu 202306L` 이 **주석 처리된 채** 있고, `#include <hazard_pointer>` 는 `file not found` — 아직 구현 안 됨 (실측).
- **folly** (`folly/synchronization/Hazptr.h`): `hazptr_obj_base`, `hazptr_holder`, `make_hazard_pointer()`, 비동기 회수·배치 retire. C++26 제안의 원형이다.
- **Linux 커널**: RCU 가 주력이고, HP 는 2024 년 무렵 RFC 패치가 오간 단계로 알고 있다 (미검증).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so                         # 41/41, 0.15 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o san && ./san
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o tsan && ./tsan
```

실제 결과: 일반 빌드 8 회 연속 `41/41 passed`, ASan+UBSan `41/41` (진짜 malloc/free, 보고 없음), TSan `41/41` (보고 없음). 하네스 검증으로, 정답에서 한 줄씩 망가뜨린 변형 3 개를 돌려 테스트가 잡는지 확인했다 (`/tmp` 에서, 실제 결과): scan 이 HP 를 무시 → 27/41 · ASan `heap-use-after-free in msq_dequeue`; protect 재확인 제거 → 39/41 (B2 + 스트레스에서 g_uaf) · ASan 동일; dequeue 의 `head != h` 재확인 제거 → 38/41 (스트레스 A · B3 · Part C 스트레스) · ASan 동일.

## 7. 자주 하는 실수

1. **게시만 하고 재확인을 안 한다.** `p = load(head); hp = p; use(p)` — load 와 게시 사이에 해제될 수 있다 (B2). 고침: 게시 후 `*src` 를 다시 읽어 같을 때만 사용.
2. **게시를 release, 재확인을 acquire 로 "최적화".** arm64 에서 `stlr; ldapr` 가 되어 SB 가 열린다. 테스트로는 거의 안 잡힌다 (이 레슨 20M 메시지에서 0, OS-A03 리트머스에서 12M 중 2). 고침: 둘 다 seq_cst, 또는 relaxed store + `atomic_thread_fence(seq_cst)` + load (단 TSan 은 독립 fence 를 모델링하지 않아 오탐이 날 수 있다 — MCPP-M13).
3. **`next` 의 재확인을 `h->next` 로 한다.** 변하지 않는 필드라 늘 통과한다 (B3). 고침: `head == h` 로 재확인.
4. **CAS 성공 뒤에 `next->val` 을 읽는다.** CAS 직후 `next` 는 남이 떼어낼 수 있는 더미다. hp1 이 아직 지키고 있으면 안전하지만, 흔히 HP 를 먼저 지우고 읽어서 UAF. 고침: CAS 전에 읽거나, HP 를 지우기 전에 읽는다.
5. **R ≤ H 로 잡는다.** 목록 전체가 보호 노드면 scan 이 아무것도 못 풀고 매 retire 마다 scan → O(H) 비용/retire (연습 4) — 상한 R 도 깨진다. 고침: `R = 2H` (`hp_init` 이 거절하게).
6. **스레드가 나갈 때 retire 목록을 버린다.** 누수이거나, 그냥 `free` 하면 남이 보호 중인 노드를 해제한다. 고침: HP 지우고 scan, 남은 것은 고아 목록으로 넘겨 다음 scan 이 입양 (연습 3).
7. **HP 칸을 패딩 없이 배열로 붙인다.** 스레드마다 매 연산 seq_cst store 하는 칸이 한 캐시 라인을 공유하면 false sharing (128 B 라인, ARCH-M08). 고침: 스레드당 `_Alignas(128)`.

## 8. 면접 Q&A

<details><summary>Q1. hazard pointer 를 한 문단으로 설명하면?</summary>

각 스레드가 "지금 역참조하려는 노드" 를 공개 칸(hazard pointer)에 적고(게시), 원래 위치를 다시 읽어 아직 그대로인지 확인한 뒤에만 역참조한다. 노드를 떼어낸 스레드는 바로 해제하지 않고 retire 목록에 모았다가, 목록이 R 개가 되면 모든 칸을 읽어 아무도 적지 않은 노드만 해제한다. 미해제 노드는 스레드당 R 개, 전체 N·R 개로 묶인다 (Michael, IEEE TPDS 2004).
</details>

<details><summary>Q2. 왜 게시 뒤에 다시 읽어야 하나? 그리고 왜 seq_cst 인가?</summary>

게시는 "앞으로의 scan" 만 막는다. load 와 게시 사이에 노드가 떼어지고 scan 까지 끝났다면 이미 해제됐다. 재확인으로 "게시 시점에 아직 연결돼 있었다" 를 확인하면, 그 뒤의 떼어냄 → retire → scan 은 내 게시를 봐야 한다. 그런데 리더(store HP → load src)와 회수자(store src → load HP)는 SB 모양이라, release/acquire 로는 "둘 다 옛값" 이 허용된다. seq_cst 끼리는 전체 순서 S 가 있어 금지된다. arm64 에서 차이는 store 가 아니라 뒤 load 의 `ldar`(RCsc) vs `ldapr`(RCpc) 한 글자다.
</details>

<details><summary>Q3. 꼬리: hp_clear 도 seq_cst 여야 하나?</summary>

아니다, release 면 된다. 지우기는 "내 읽기가 다 끝났다" 를 알리는 것이고, scan 이 NULL 을 acquire 이상으로 읽으면 리더의 읽기 → 해제 사이에 happens-before 가 생긴다. 지우기가 늦게 보이면 scan 이 노드를 한 바퀴 더 남길 뿐 (보수적) 안전하다.
</details>

<details><summary>Q4. 메모리 상한은? R 은 어떻게 고르나?</summary>

scan 직후 남는 노드는 보호 중인 서로 다른 노드라 ≤ H = N·K. R > H 면 목록 길이는 늘 ≤ R, 전체 미해제 ≤ N·R. R = 2H 처럼 H + Ω(H) 로 잡으면 scan 한 번이 최소 H 개를 풀어 retire 당 상각 O(1) (scan 이 해시/정렬이면). R = H+1 이면 최악에 매 retire 마다 scan — 실측 1 000 retire 에 scan 1 000 번 vs 125 번. 주의: N·R = 2KN² 이라 스레드 수에 제곱으로 커진다.
</details>

<details><summary>Q5. EBR(RCU) 대신 HP 를 쓰는 이유와 반대 경우는?</summary>

HP: 멈춘/선점된 스레드가 자기가 쥔 K 개만 붙잡는다 → 상한이 있어 정적 풀·실시간 시스템에 맞다. 대신 노드마다 seq_cst store + 재확인 (64 노드 순회에서 1.44×) 이고 구조마다 재확인 대상을 설계해야 한다. EBR: 구간당 한 번이라 읽기가 거의 공짜 (순회 1.04×), 코드도 단순. 대신 멈춘 리더 하나가 전체 회수를 막는다 (실측 200 ms 에 25 만 노드). 큐·스택 + 메모리 상한 필수 → HP, 읽기 다수 테이블·긴 순회 → EBR/RCU. 둘을 섞은 방식도 있다 (hazard eras, interval-based reclamation 등 — 이름만, 문서 기준).
</details>

<details><summary>Q6. M-S 큐 dequeue 에서 HP 는 몇 개, 각각 무엇을 재확인하나?</summary>

2 개. hp0 = head(`h`): `head` 를 다시 읽어 같으면 확인. hp1 = `h->next`(`next`): `h->next` 는 불변이라 재확인 대상이 아니고 **`head` 가 아직 `h` 인가** 로 확인한다 — `h` 가 head 면 그 뒤 노드도 아직 안 떼어졌다. tail 은 dequeue 에서 역참조하지 않으므로 HP 불필요, enqueue 는 `tail->next` 를 읽으므로 tail 에 hp0.
</details>

<details><summary>Q7. HP 는 ABA 도 해결하나? DCAS/태그 포인터와 비교하면?</summary>

그렇다. 보호 중인 노드는 해제도 재사용도 안 되므로 "같은 주소의 다른 노드" 가 CAS 를 속일 수 없다 (연습 1: HP 없으면 top 이 해제된 B 가 되고, HP 면 CAS 가 실패해 재시도). 태그(세대 번호) 포인터는 ABA 는 막지만 **해제** 문제는 못 푼다 — 노드 메모리를 OS 에 돌려줄 수 없고 타입 안정(type-stable) 풀에 영원히 남겨야 한다. 자세한 건 DS-A04.
</details>

<details><summary>Q8. HP 기반 큐는 lock-free 인가? wait-free 인가?</summary>

M-S 큐 자체가 lock-free (누군가의 CAS 는 항상 성공해 시스템 전체가 진행) 이고, HP 는 그 성질을 유지한다: protect 의 재시도는 `*src` 가 바뀐 경우, 즉 다른 스레드가 진행한 경우에만 일어나고, scan 은 기다리지 않는 유한 루프다. retire/scan 자체는 wait-free (유한 단계). 하지만 dequeue 전체는 CAS 경쟁에서 무한히 질 수 있으므로 wait-free 는 아니다. EBR 은 회수가 멈춘 리더를 기다리므로 "메모리 회수" 관점에선 blocking 이다 — 이것이 HP 가 non-blocking 회수라고 불리는 이유.
</details>

<details><summary>Q9. 펌웨어에서 쓸 일이 있나?</summary>

단일 코어 MCU 의 ISR ↔ main 은 보통 IRQ 를 잠깐 끄거나 SPSC 링 (C-M14, DS-A01) 이 낫다. 쓸 만한 곳은 CAS 가 되는 멀티코어 (Cortex-M33 듀얼, Cortex-A + RTOS) 의 코어 간 MPMC 메시지 큐: `malloc` 금지 → 정적 풀이 필수이고, HP 상한 `1 + qcap + N·R` 로 풀을 컴파일 시점에 잡으면 "풀 고갈" 이 원리적으로 없다 (Part C, 할당 실패 0). Cortex-M0/M0+ (ARMv6-M) 는 LDREX/STREX 가 없어 CAS 기반 큐 자체가 불가능하다 (CPP-A04).
</details>

## 9. 추가 연습

### 연습 1 — Treiber 스택 pop 에 HP: ABA 도 막힌다

스택 top: A(1) → B(2) → C(3). 스레드 0 이 `t = A, next = B` 를 읽고 멈춘 사이 스레드 1 이 pop, pop, push 9. HP 없는 버전과 HP 버전에서 무슨 일이 생기나? (FIFO 풀이라 9 는 해제된 A 자리에 들어간다.)

<details><summary>답</summary>

HP 없음: 9 가 A 와 **같은 주소** 라 스레드 0 의 `CAS(top, A → B)` 가 성공 → top = 해제된 B, C(3) 는 사라짐 (테스트가 `top == B && B->state == FREED` 를 확인). HP: 스레드 0 의 hp0 이 A 를 지켜 A 가 해제·재사용되지 않음 → 9 는 다른 주소 → CAS 실패 → 재시도해 9 를 pop, 다음 pop 은 3. `ts_pop_hp`, `aba_hook` (practice.c). ASan 빌드에서는 해제된 노드를 읽는 HP 없는 실험이 곧바로 abort 이므로 그 부분만 `#ifndef NODE_MALLOC` 으로 건너뛴다.
</details>

### 연습 2 — 정렬 scan

`hp_scan_sorted`: HP 칸들을 `qsort` 하고 목록의 각 노드를 `bsearch`. 선형 scan 과 결과가 같음을 PRNG 상태 1 000 개로 확인하라. (해제 대신 기록만 하는 `mock_reclaim` 을 써서 같은 seed 로 상태를 두 번 만든다.)

<details><summary>답</summary>

비교 비용 O(R·H) → O(H log H + R log H). 결과(해제 개수, 해제된 노드 집합 비트마스크, 남은 길이)가 1 000 개 상태 모두 같다. H 가 수십 이하면 선형이 더 빠를 수 있다 — 캐시에 다 들어가는 작은 배열의 선형 탐색은 분기 예측이 좋다. 그래서 folly 등은 스레드 수가 클 때만 해시를 쓴다 (문서 기준).
</details>

### 연습 3 — 동적 등록과 고아(orphan) 목록

스레드가 들어오고 나가는 시스템: `hp_register` (빈 레코드를 CAS 로 차지), `hp_unregister` (HP 지우고 scan, 남은 노드는 고아 목록으로), `hp_adopt` (고아를 내 목록으로 가져와 scan). 나간 스레드가 retire 한 노드를 다른 스레드가 아직 보호 중일 때 누수도 UAF 도 없어야 한다.

<details><summary>답</summary>

시나리오: tc 가 더미 x 를 보호 중, ta 가 x·y 를 retire 하고 나감 → y 는 해제, x 는 고아. 새 스레드 td 가 ta 의 레코드를 재사용, `hp_adopt` → x 는 아직 보호 중이라 남음 → tc 가 HP 를 지운 뒤 다시 adopt → x 해제. 고아 목록은 드문 경로라 mutex 로 충분하다 (Michael 논문은 레코드를 lock-free 리스트로 두고 retire 목록을 넘긴다 — 문서 기준).
</details>

### 연습 4 — R 선택의 상각 비용

H = 8 칸이 모두 retire 목록 안의 서로 다른 노드를 보호하고 있다. 보호 안 된 노드 1 000 개를 더 retire 할 때 R = H+1 과 R = 2H 의 scan 횟수를 정확히 예측하라.

<details><summary>답</summary>

R = 9: 목록 8(보호) + 1 → scan 이 1 개 풀고 8 남음 → **매 retire 마다 scan = 1 000 번** (칸 읽기 8 000). R = 16: 8 + 8 → scan 이 8 개 풀고 8 남음 → 8 개마다 scan = **125 번** (칸 읽기 1 000). 실측 출력: `ex4: scans R=9 -> 1000 (slot reads 8000), R=16 -> 125 (slot reads 1000)`.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr      # 23/23 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o prs && ./prs   # 20/20 (연습 1 의 HP 없는 실험 3 개 제외)
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o prt && ./prt     # 23/23
```

## 10. 핵심 정리

- lock-free 에서 떼어낸 노드는 바로 못 지운다 — 다른 스레드가 그 포인터를 쥐고 있을 수 있다. 결과가 맞아도(malloc 재사용) UAF 다 → ASan.
- HP protect = **load → 게시 → 재확인**, 같을 때만 역참조. 재확인 대상은 "이 노드가 아직 연결돼 있다" 를 증명하는 값 (`next` 는 `head == h`).
- 게시 store → 재확인 load 는 SB 모양 → **seq_cst** (arm64 `stlr; ldar`, M4 `dmb sy; str; dmb sy; ldr`). 지우기는 release.
- retire 목록 길이 R 에서 scan, **R = 2H** → 상각 O(1). 미해제 ≤ 스레드당 R, 전체 **N·R** → 정적 풀 = `1 + qcap + N·R`.
- 멈춘 스레드: HP 는 자기 K 개만, EBR 은 전부 (실측 25 만 vs 32). 대신 HP 는 노드마다 비용 (순회 1.44×), EBR 은 구간당.
- M-S 큐 처리량은 head/tail 경합이 지배 — 회수 방식으로 처리량 우열을 말하지 말 것.
- HP 는 ABA 도 막는다 (보호 중 = 재사용 불가). C++26 `<hazard_pointer>`/`<rcu>` 는 이 Mac libc++ 에 아직 없음.

## 참고자료

- Maged M. Michael, "Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects", IEEE Transactions on Parallel and Distributed Systems 15(6), 2004 — 알고리즘, R = H + Ω(H), M-S 큐·리스트 적용
- Maged M. Michael, Michael L. Scott, "Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms", PODC 1996 — M-S 큐
- Thomas E. Hart, Paul E. McKenney, Angela Demke Brown, Jonathan Walpole, "Performance of memory reclamation for lockless synchronization", JPDC 2007 — HP · EBR · QSBR 비교
- Paul E. McKenney, *Is Parallel Programming Hard, And, If So, What Can You Do About It?* (perfbook), 9장 Deferred Processing — <https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook.html>
- P2530 "Hazard Pointers for C++26", P2545 "Read-Copy Update (RCU)" — <https://wg21.link/p2530>, <https://wg21.link/p2545>; pointer lifetime-end zap 논의 P2414 — <https://wg21.link/p2414>
- folly `folly/synchronization/Hazptr.h` — <https://github.com/facebook/folly/blob/main/folly/synchronization/Hazptr.h>
- ISO/IEC 9899:2011 (C11) 7.17 Atomics, 5.1.2.4 Multi-threaded executions and data races, 6.2.4p2 (포인터 수명)
- Arm Architecture Reference Manual for A-profile (DDI 0487) — LDAR / LDAPR / STLR 순서 규칙
- 관련 레슨: C-A03 (release/acquire, SB), OS-A03 (EBR 전체 구현·SB 리트머스 실측·멈춘 리더), DS-A01 (SPSC 링), DS-A03 (Vyukov bounded MPMC), DS-A04 (ABA · 태그 포인터), MCPP-M13 (fence 와 TSan), ARM-M04 (LL/SC vs LSE), LLAT-B06 (atomic 비용), C-M14 (ISR 공유), CPP-A04 (Cortex-M lock-free 여부), ARCH-M08 (false sharing)
