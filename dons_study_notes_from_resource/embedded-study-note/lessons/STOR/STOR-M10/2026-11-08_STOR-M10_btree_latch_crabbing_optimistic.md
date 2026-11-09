---
id: STOR-M10
time: 95분
updated: 2026-11-08
---
# STOR-M10 B+tree 동시성: latch crabbing & optimistic

## 학습 목표

- 노드마다 `pthread_rwlock_t` 래치(latch)를 둔 작은 B+tree(키 `int`, 노드당 최대 8 키, insert + lookup — **delete 는 없다**)에 두 가지 래치 프로토콜을 직접 짠다: **pessimistic latch crabbing**(위에서 W 래치를 잡아 내려가며 "안전한" 자식을 만나면 조상을 놓음)과 **optimistic**(R 래치로 내려가 리프만 W, 리프가 꽉 찼으면 다 놓고 pessimistic 으로 재시작).
- "래치에는 교착 탐지기가 없다 → 프로토콜이 **순서로** 교착을 막는다" 를 설명하고, 순서를 어기는 대표 실수인 **제자리 R→W 업그레이드** 가 두 스레드에서 실제로 멈추는 것을 강제 스케줄 + 시간 제한 대기로 재현·보고한다.
- 래치(latch)와 락(lock)의 차이(STOR-M08 의 2PL lock manager 와 무엇이 다른가)를 표로 말한다.
- 4 스레드 동시 삽입 뒤 "모든 키가 정확히 한 번" + B+tree 불변식(정렬, 채움 하한, 모든 리프 같은 깊이, 형제 링크)을 결정적으로 검사하고, TSan 으로 정답이 깨끗한 것과 일부러 넣은 버그(부모를 먼저 놓기)가 잡히는/안 잡히는 것을 본다.
- coarse(트리 전체 rwlock 하나) / crabbing / optimistic 처리량을 스레드 1–8, 읽기 위주·쓰기 위주로 실측하고, "왜 셋 다 잘 안 늘어나는가" 에서 B-link tree(Lehman–Yao)와 OLC(optimistic lock coupling)로 가는 이유를 말한다.

## 1. 문제

"B+tree 인덱스에 스레드 여러 개가 동시에 insert 하면 어떻게 잠그나요? root 에 락 하나면 안 되나요? 교착은요?" — DB·스토리지 엔진(InnoDB, PostgreSQL nbtree, 인메모리 DB 인덱스), 파일시스템 메타데이터, 그리고 Don 이 다뤘던 SSD 펌웨어의 매핑 테이블(멀티 코어 컨트롤러 + NVMe 다중 큐) 면접에서 그대로 나오는 질문이다. 핵심은 **래치를 어떤 순서로 잡고 언제 놓느냐** 다.

동시성 가이드 6층(교착 상태)의 레슨이다. 이 층의 다른 레슨들은 "순환 대기" 를 **찾거나 깨는** 방법을 다룬다. 여기서는 반대로 **탐지기 없이, 처음부터 순환이 생길 수 없게** 잡는 순서를 설계한다. B+tree 코드는 STOR 트랙의 앞 아이템이 아직 없으므로 레슨 안에 다 들어 있다 (노드 split 같은 기본 연산은 "주어짐").

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">키 15 삽입: 같은 트리, 두 프로토콜 (W = 쓰기 래치, R = 읽기 래치, 꽉 참 = 8/8)</text>
<text x="20" y="48" fill="currentColor" font-weight="bold">pessimistic crabbing</text>
<rect x="20" y="58" width="150" height="28" rx="4" class="f-warn" stroke="currentColor"/><text x="30" y="77" fill="currentColor">root_latch  W</text>
<rect x="20" y="104" width="150" height="28" rx="4" class="f-warn" stroke="currentColor"/><text x="30" y="123" fill="currentColor">root (3/8)  W → 안전!</text>
<rect x="20" y="150" width="150" height="28" rx="4" class="f-acc" stroke="currentColor"/><text x="30" y="169" fill="currentColor">inner (8/8)  W 유지</text>
<rect x="20" y="196" width="150" height="28" rx="4" class="f-acc" stroke="currentColor"/><text x="30" y="215" fill="currentColor">leaf (8/8)  W → split</text>
<path d="M95 86 L95 102 M95 132 L95 148 M95 178 L95 194" stroke="currentColor" fill="none"/>
<text x="185" y="77" fill="currentColor">① root 가 안전하면 root_latch 를 놓는다</text>
<text x="185" y="123" fill="currentColor">② root 아래가 꽉 차 있으면 root W 를 쥔 채 내려간다</text>
<text x="185" y="169" fill="currentColor">③ inner 도 꽉 참 → split 이 여기까지 올라올 수 있다</text>
<text x="185" y="215" fill="currentColor">④ leaf split → inner split → root 에 구분 키 추가 (모두 W 보유 중)</text>
<line x1="10" y1="240" x2="730" y2="240" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="20" y="264" fill="currentColor" font-weight="bold">optimistic</text>
<rect x="20" y="274" width="110" height="28" rx="4" class="f-ok" stroke="currentColor"/><text x="30" y="293" fill="currentColor">root  R</text>
<rect x="150" y="274" width="110" height="28" rx="4" class="f-ok" stroke="currentColor"/><text x="160" y="293" fill="currentColor">inner  R</text>
<rect x="280" y="274" width="130" height="28" rx="4" class="f-warn" stroke="currentColor"/><text x="290" y="293" fill="currentColor">leaf  W (리프만)</text>
<path d="M130 288 L148 288 M260 288 L278 288" stroke="currentColor" fill="none"/>
<text x="430" y="285" fill="currentColor">자식 잡고 → 부모 놓기 (한 번에 최대 2 개)</text>
<text x="430" y="301" fill="currentColor">리프에 자리 있으면 끝 (대부분의 삽입)</text>
<rect x="20" y="318" width="390" height="28" rx="4" class="f-mut" stroke="currentColor"/><text x="30" y="337" fill="currentColor">리프 꽉 참 → 전부 놓고 restarts++ → pessimistic 으로 처음부터</text>
<text x="430" y="337" fill="currentColor">R 을 쥔 채 W 를 기다리지 않는다 (업그레이드 금지)</text>
<text x="20" y="370" fill="currentColor">둘 다 래치는 언제나 위 → 아래 (같은 층은 왼쪽 → 오른쪽) 순서로만 잡는다 = 전역 순서 = 순환 대기 불가능.</text>
</svg>
```

**래치 vs 락** — 같은 "잠금" 이지만 DB 문헌에서는 다른 물건이다 (Graefe 2010 의 구분):

| | 래치 (latch) — 이 레슨 | 락 (lock) — STOR-M08 |
|---|---|---|
| 지키는 것 | 메모리 안의 자료구조(노드 하나)의 **물리적** 일관성 | 데이터베이스 내용(행·키 범위)의 **논리적** 일관성(직렬화 가능성) |
| 쥐는 시간 | 노드 하나 읽고 쓰는 동안 (ns–µs) | 트랜잭션이 끝날 때까지 (strict 2PL) |
| 모드 | R / W (가끔 U) | S / X / IS / IX / SIX … |
| 교착 처리 | **탐지기 없음 → 코딩 규칙(순서)으로 회피** | wait-for 그래프 탐지 + 희생자 abort |
| 구현 | rwlock, 스핀락, 버전 카운터 | lock manager 해시 테이블 + 요청 큐 |

### 같은 순환 대기, 다른 층

| 아이템 | 순환하는 것 | 이 레슨과의 관계 |
|---|---|---|
| OS-M10 | 철학자 → 포크 | Coffman 4 조건 표의 주인. 여기서는 "전역 락 순서" 를 트리 모양(위 → 아래)으로 쓴다 |
| OS-M11 | 락 클래스 순서 그래프 (mini lockdep) | 연습 3 의 "래치 순서 검사기" 가 같은 아이디어 (층 번호 = 클래스) |
| ALGO-A10 | 락 의존 그래프의 SCC | 래치에는 그래프를 만들 시간이 없다 → 대신 순서를 강제 |
| STOR-M08 | 트랜잭션 → 행 락 (wait-for 그래프) | 같은 DB 안의 "락" 쪽. S→X 업그레이드 교착은 거기서도 고전 |
| SOCX-M05 | NoC 채널 의존 그래프 | Dally–Seitz: 채널에 순서를 매겨 순환을 없앰 = 여기의 층 순서와 같은 증명 |
| DCN-M05 | PFC 버퍼 의존 (CBD) | up-down 라우팅 = "위로만 갔다가 아래로만" = 트리 순서 |

### Part A — pessimistic crabbing

- 주어짐: 노드 구조(`level` 0 = 리프, 만든 뒤 안 바뀜), `leaf_lb` / `inner_idx` (찾기), `leaf_put_at` / `leaf_split` / `inner_put` / `inner_split` (split 은 9 키를 4 | 5, 내부 노드는 4 | 위로 1 | 4), 래치 도우미 `lat_r` / `lat_w` / `unlat` / `root_r` / `root_w` / `root_unl`, 불변식 검사 `bt_check`.
- A1 `safe_for_insert(n)` — 키 하나가 더 와도 split 하지 않나?
- A2 `bt_lookup(t, key, &val)` — R crabbing: 자식 잡고 → 부모 놓기.
- A3 `bt_insert_pess(t, key, val)` — W crabbing, 경로 스택, split 전파, root split.

### Part B — optimistic, 그리고 업그레이드 교착

- B1 `bt_insert_opt(t, key, val)` — R 로 내려가 리프만 W. 꽉 찼으면 다 놓고 `restarts++` → `bt_insert_pess`. 혼자 돌리면 `restarts == leaf_splits` 가 정확히 성립해야 한다.
- B2 `ul_upgrade_bounded(u, ms)` — mutex + cond 로 만든 업그레이드 가능한 래치의 R→W 업그레이드 (혼자면 성공, 시간 안에 안 되면 `ETIMEDOUT`).
- 강제 스케줄 2 개: (1) 두 optimistic 삽입이 root 를 동시에 R 로 쥔 상태에서 같은 꽉 찬 리프로 → 둘 다 재시작, 교착 없음, `restarts == 2`. (2) 두 스레드가 같은 리프를 R 로 쥐고 둘 다 업그레이드 → 300 ms 동안 진행 0 → `LATCH DEADLOCK` 보고 → "놓고 W 로 재시작" 으로 회복.
- 4 스레드 × 2 만 삽입(서로 다른 키 / 같은 키), 읽기 2 + 쓰기 2 동시 실행: 모든 키 정확히 한 번, 미리 넣은 키는 언제 찾아도 있음, 불변식.
- 데모: `demo_bench.c` (처리량), `demo_planted.c` (부모를 먼저 놓는 버그 2 종: 강제 스케줄 / 스트레스 / TSan), `demo_rwlock_upgrade.c` (macOS `pthread_rwlock_t` 에서 R 쥔 채 `wrlock` 하면?).

### Part C — SSD FTL 의 L2P 인덱스

- C1 `l2p_write(t, lba, ppa)` — 호스트 쓰기 = 매핑 갱신(upsert). 이미 있는 LBA 면 리프의 값만 덮어쓰기(꽉 찬 리프여도 split 이 필요 없으니 재시작 없음), 없으면 넣기, 꽉 찼으면 재시작. pessimistic 이 "그 사이 다른 큐가 먼저 넣었다" 로 실패하면 처음부터.
- NVMe 큐 4 개 = 스레드 4 개(큐 q 가 LBA ≡ q mod 4 담당) × 3 라운드: 1 라운드 = 새 매핑 16384, 2·3 라운드 = 덮어쓰기만 → **재시작 정확히 0**, 마지막 라운드의 PPA 가 남아야 한다.

## 2. Problem (English)

> Implement a concurrent in-memory B+tree (int keys, at most 8 keys per node, insert + lookup only, no delete). Each node has a `pthread_rwlock_t` latch and the tree has a `root_latch` guarding the root pointer.
>
> 1. `bool bt_lookup(bt_t *t, int key, int *val)` — read-latch coupling (latch child, then release parent).
> 2. `bool bt_insert_pess(bt_t *t, int key, int val)` — pessimistic latch crabbing: write-latch top-down, release all ancestors (and `root_latch`) as soon as the newly latched node is *safe* (`nkeys < MAXK`), then split bottom-up through the latched path; create a new root when needed. Return false for duplicates.
> 3. `bool bt_insert_opt(bt_t *t, int key, int val)` — optimistic: read latches down to the parent of the leaf, write latch on the leaf only; if the leaf is full, release everything, count a restart and fall back to the pessimistic insert. Never wait for a write latch while holding a read latch.
> 4. `int ul_upgrade_bounded(ulatch_t *u, int ms)` on a mutex+condvar latch: upgrade the caller's read latch to write when it is the only reader; `ETIMEDOUT` otherwise. Show that two readers upgrading at once can never succeed (latch deadlock) under a forced schedule, and recover by release-and-restart.
> 5. `int l2p_write(bt_t *t, int lba, int ppa)` — upsert for an SSD logical-to-physical map (1 = new mapping, 0 = overwrite).
>
> Tests: after concurrent inserts every key is present exactly once; B+tree invariants hold (sorted, fill ≥ 4 except root, all leaves at the same depth, sibling links in key order). Follow-ups: why is latch crabbing deadlock-free? Why does nothing scale here? What do B-link trees and OLC change?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.14 초 만에 exit 1 — 단일 스레드 체크가 실패하면 멀티스레드 스트레스는 건너뛰므로 멈추지 않는다):

```text
-- Part A: 단일 스레드 --
FAIL starter.c:422  safe_for_insert(&probe) == true
PASS safe_for_insert(&probe) == false
PASS bt_lookup(&t, 5, NULL) == false
FAIL starter.c:427  bt_insert_pess(&t, 5, 50) == true
…
FAIL starter.c:465  atomic_load(&t.restarts) == atomic_load(&t.leaf_splits) && atomic_load(&t.restarts) > 0
FAIL starter.c:474  rc == 0 && g_leaf.writer == true && g_leaf.readers == 0
…
    upgrade rc: T0=-1 T1=-1 (교착을 못 봤다)
FAIL starter.c:497  both_timed_out && ua[0].readers_seen == 2 && ua[1].readers_seen == 2
…
SKIP 12 stress checks (Part A/B/C 멀티스레드): 단일 스레드 체크가 실패했다
7/22 passed
```

TODO 는 A1–A3, B1–B2, C1. 스텁은 즉시 잘못된 값을 돌려줄 뿐 래치를 잡지 않는다. 강제 스케줄의 장벽과 업그레이드 대기는 모두 `pthread_cond_timedwait` 로 시간 제한이 있다 (macOS 에는 `pthread_barrier_t` 도 `pthread_rwlock_timedwrlock` 도 없다).

## 4. 힌트

<details><summary>힌트 1 — "안전" 의 정의</summary>

삽입 기준으로 노드가 안전하다 = 키가 하나 더 들어와도 넘치지 않는다 = `nkeys < MAXK`. 안전한 노드 아래에서 무슨 split 이 일어나든 그 노드에서 멈춘다(구분 키 하나를 받을 자리가 있다). 그러니 그 위의 래치는 더 필요 없다. (delete 가 있다면 "키 하나가 빠져도 하한 아래로 안 내려간다" 가 따로 필요하다 — 이 레슨엔 delete 가 없다.)
</details>

<details><summary>힌트 2 — lookup 의 순서</summary>

`root_r → n = root → lat_r(n) → root_unl`. 그다음 반복: `c = n->child[inner_idx(n,key)]; lat_r(c); unlat(n); n = c;`. **잡고 → 놓기** 순서를 바꾸면(놓고 → 잡기) 그 틈에 `c` 가 split 되어 찾는 키가 오른쪽 새 노드로 이사할 수 있다 (`demo_planted.c forced`).
</details>

<details><summary>힌트 3 — pessimistic 의 경로 스택</summary>

`path[]` 에 W 로 잡은 노드를 쌓는다. 새로 잡은 `c` 가 안전하면 `unlat_path(path, &depth)` 로 위를 다 놓고(`root_held` 면 `root_unl` 도) `c` 부터 다시 쌓는다. 리프에서 split 하면 `path[depth-2]` 부터 거꾸로: 자리 있으면 `inner_put` 하고 끝, 없으면 `inner_split` 해서 올라간다. 끝까지 `right` 가 남으면 `path[0]` 이 root 였다는 뜻이고 그때는 반드시 `root_latch` 를 아직 쥐고 있다 → 새 root.
</details>

<details><summary>힌트 4 — optimistic 에서 리프인지 어떻게 아나</summary>

래치를 잡기 전에 리프인지 알아야 W 로 잡는다. `level` 은 만든 뒤 안 바뀌는 필드이므로 "부모가 `level == 1` 이면 자식은 리프" 로 판단한다. root 자체가 리프(`level == 0`)면 root 를 W 로. 꽉 찬 리프면 **리프를 놓은 뒤** 재시작 — 리프 R 을 쥔 채 root W 를 기다리면 그게 바로 순서 위반이다.
</details>

<details><summary>힌트 5 — 업그레이드의 대기 조건</summary>

호출자가 이미 R 하나를 쥐고 있으므로 "나만 남았다" = `readers == 1 && !writer`. 그때 `readers = 0; writer = true`. 다른 R 이 빠질 때 `ul_runlock` 이 broadcast 해 준다. `pthread_cond_timedwait` 의 시각은 `CLOCK_REALTIME` 절대 시각 (`deadline_ms` 주어짐). 시간이 다 되면 R 을 쥔 채 `ETIMEDOUT` — 정리는 호출자 몫.
</details>

## 5. 풀이

### Part A — 노드와 "안전"

노드 하나는 `keys[8]`, 내부 노드면 `child[9]`, 리프면 `val[8]` + `next`(오른쪽 형제). 규칙: 내부 노드의 `child[i]` 아래 키 k 는 `keys[i-1] <= k < keys[i]` (구분 키 = 오른쪽 자식의 첫 키). split 은 9 키를 4 | 5 로 나누므로 root 가 아닌 노드는 항상 4–8 키 = 채움 하한 50%. `bt_check` 는 이 규칙 6 개(넘침, 하한, 정렬, 구분 키 범위, 리프 깊이, 형제 링크)를 확인하고 키 수를 센다.

`safe_for_insert` 는 한 줄 `return n->nkeys < MAXK;`. 단일 스레드로 5000 키를 무작위 순서로 넣으면(실제 출력):

```text
    pess 5000 키: 높이 5, leaf split 876, inner split 154
```

### Part A — lookup: R crabbing

```c
root_r(t); node_t *n = t->root; lat_r(n); root_unl(t);
while (n->level > 0) {
    node_t *c = n->child[inner_idx(n, key)];
    lat_r(c);      /* 자식을 먼저 */
    unlat(n);      /* 그다음 부모 */
    n = c;
}
```

`root_latch` 가 따로 있는 이유: `t->root` 포인터 자체가 root split 때 바뀐다. root 노드의 래치는 "그 노드 내용" 을 지킬 뿐 "누가 root 인가" 는 못 지킨다. 그래서 root_latch → root 노드 순서로 잡고, root 노드를 잡은 뒤에는 root_latch 를 바로 놓는다 (옛 root 는 split 되어도 여전히 유효한 노드다 — 우리가 R 을 쥐고 있으니 split 될 수도 없다).

### Part A — pessimistic insert: W crabbing

예: 높이 3, root 3/8, 내부 8/8, 리프 8/8 에 키 하나.

| 단계 | 잡은 것 (path) | 판단 |
|---|---|---|
| 1 | root_latch W, root W | root 3/8 → 안전 → root_latch 놓음 |
| 2 | root W, 내부 W | 내부 8/8 → 안전 아님 → root 유지 |
| 3 | root W, 내부 W, 리프 W | 리프 8/8 → 안전 아님 |
| 4 | — | 리프 split → 내부에 구분 키: 8/8 이라 내부 split → root 에 구분 키 (3 → 4 키) |
| 5 | 전부 놓음 | |

리프가 4/8 였다면 3 단계에서 root·내부를 놓고 리프 하나만 W 로 쥐었을 것이다. 문제는 **root W 를 매번 잡는다** 는 것: 1 단계가 모든 삽입의 공통 경로라서, root 가 안전하든 말든 root_latch W + root W 를 한 번씩은 거친다. 쓰기가 많으면 root 에서 전부 줄을 선다 — 처리량 표에서 확인.

```c
if (ok && n->nkeys < MAXK) leaf_put_at(n, i, key, val);
else if (ok) {
    int sep; node_t *right = leaf_split(t, n, key, val, &sep);
    for (int d = depth - 2; d >= 0 && right; d--) {
        node_t *p = path[d];
        if (p->nkeys < MAXK) { inner_put(p, sep, right); right = NULL; }
        else right = inner_split(t, p, sep, right, &sep);
    }
    if (right) { /* path[0] 이 root 였다 → root_held 가 반드시 true */ ... t->root = r; }
}
```

왜 교착이 없나: 모든 스레드가 같은 전역 순서(root_latch → 층 높은 노드 → 낮은 노드)로만 **기다린다**. 놓는 건 아무 때나 해도 된다 (놓기는 기다리지 않으니까). 쥔 래치보다 "위" 를 기다리는 스레드가 없으므로 대기 그래프의 모든 간선이 위 → 아래로만 향한다 → 사이클 불가능. OS-M10 의 "포크에 번호를 매기고 작은 번호부터" 와 같은 증명이다. split 이 만든 새 노드는 아직 아무도 모르는 노드라 래치 없이 채워도 되고, 부모에 연결(W 보유 중)되는 순간부터 보인다.

### Part B — optimistic insert

대부분의 삽입은 리프에 자리가 있다 (split 확률 ≈ 1/(평균 빈칸 수)). 그러니 "split 할지도 모르니 root 부터 W" 는 손해다. optimistic 은 R 로 내려가 리프만 W:

```c
root_r(t); node_t *n = t->root;
if (n->level == 0) lat_w(n); else lat_r(n);
root_unl(t);
while (n->level > 0) {
    node_t *c = n->child[inner_idx(n, key)];
    if (n->level == 1) lat_w(c); else lat_r(c);   /* 부모가 level 1 → 자식은 리프 */
    unlat(n); n = c;
}
if (중복) { unlat(n); return false; }
if (n->nkeys < MAXK) { leaf_put_at(...); unlat(n); return true; }
unlat(n);                                          /* 전부 놓고 */
atomic_fetch_add_explicit(&t->restarts, 1, memory_order_relaxed);
return bt_insert_pess(t, key, val);                /* 처음부터 */
```

혼자 돌리면 재시작 횟수가 정확히 leaf split 수와 같아야 한다 (꽉 찬 리프를 만날 때마다 한 번 재시작하고, 재시작한 pessimistic 이 그 리프를 나눈다). 실제 출력:

```text
    opt 5000 키: restarts 876 = leaf split 876 (삽입의 17.5%)
```

무작위 순서에서 삽입 6 번 중 1 번꼴로 재시작한다 (연습 4: 오름차순이면 24.8%). 재시작한 삽입은 R 경로 + W 경로를 두 번 걷는다. 그래도 나머지 82.5% 가 root 를 R 로만 지나가는 이득이 더 크다.

### Part B — 강제 스케줄 1: 두 optimistic 삽입, 같은 꽉 찬 리프

트리: root(level 1, 구분 키 50) + 왼쪽 리프 [10 11 12 13 14 20 30 40](꽉 참) + 오른쪽 리프. `g_hook` 이 "root 를 R 로 잡은 직후" 두 스레드를 장벽에서 만나게 한다 → 둘 다 root R 을 쥔 채 출발. T1 = 키 15, T2 = 키 16.

| 순간 | T1 | T2 |
|---|---|---|
| 1 | root R 보유 (장벽 통과) | root R 보유 (장벽 통과) |
| 2 | 리프 W 획득 → root R 놓음 | 리프 W 대기 (root R 보유) |
| 3 | 꽉 참 → 리프 놓음 → restart → root_latch W → root W **대기** (T2 의 R 때문에) | 리프 W 획득 → root R 놓음 |
| 4 | root W 획득 → 리프 W 대기 | 꽉 참 → 리프 놓음 → restart → root_latch W 대기 |
| 5 | 리프 split, 15 넣음, 다 놓음 | root_latch → root → 오른쪽 반쪽 리프(안전) → 16 넣음 |

T1 이 리프를 바꾸려면 root W 가 필요하고, root W 는 T2 가 리프 W 를 잡은 **다음에야**(그때 root R 을 놓으니까) 나온다 → T2 는 반드시 꽉 찬 리프를 본다 → 재시작은 매번 정확히 2. 실제 출력:

```text
-- Part B: 강제 스케줄 — 두 optimistic 삽입, 같은 꽉 찬 리프 --
    restarts 2, 새 leaf split 1
PASS atomic_load(&t.restarts) == 2 && atomic_load(&t.leaf_splits) - splits0 == 1
```

3 순간을 보라: T1 은 root W 를 기다리는 동안 **아무 래치도 쥐고 있지 않다** (리프를 놓고 재시작했으니까). 대기하는 스레드가 아무것도 안 쥐고 있으면 사이클의 고리가 될 수 없다.

### Part B — 제자리 업그레이드 = 교착

"리프를 R 로 읽어 보고, 써야 하면 그 자리에서 W 로 올리면 되지 않나?" 두 스레드가 같은 리프를 R 로 쥔 채 둘 다 업그레이드를 시작하면, 각자 "R 을 쥔 사람이 나 하나" 가 되기를 기다린다. 그런데 상대도 R 을 쥔 채 같은 것을 기다린다. 둘 다 아무것도 놓지 않는다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">제자리 R→W 업그레이드: 두 스레드, 리프 하나</text>
<rect x="290" y="90" width="160" height="50" rx="6" class="f-acc" stroke="currentColor"/>
<text x="305" y="112" fill="currentColor">leaf latch</text>
<text x="305" y="130" fill="currentColor">readers = 2</text>
<rect x="40" y="90" width="150" height="50" rx="6" class="f-warn" stroke="currentColor"/>
<text x="55" y="112" fill="currentColor">T0: R 보유</text><text x="55" y="130" fill="currentColor">W 대기 (readers==1?)</text>
<rect x="550" y="90" width="150" height="50" rx="6" class="f-warn" stroke="currentColor"/>
<text x="565" y="112" fill="currentColor">T1: R 보유</text><text x="565" y="130" fill="currentColor">W 대기 (readers==1?)</text>
<path d="M190 105 L288 105" stroke="currentColor" fill="none" stroke-width="2"/><text x="200" y="98" fill="currentColor">holds R</text>
<path d="M550 105 L452 105" stroke="currentColor" fill="none" stroke-width="2"/><text x="470" y="98" fill="currentColor">holds R</text>
<path d="M115 140 C 115 200, 625 200, 625 142" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
<text x="250" y="190" fill="currentColor">T0 → T1 → T0 : 각자 상대의 R 이 빠지기를 기다린다 (순환 대기)</text>
<text x="10" y="225" fill="currentColor">고치는 법 ① 놓고 처음부터 W (hold-and-wait 를 깸 — 이 레슨의 optimistic)</text>
<text x="10" y="243" fill="currentColor">② U(update) 모드: 업그레이드할 사람은 한 명만 (순환 대기를 깸 — 연습 2)</text>
</svg>
```

`ul_upgrade_bounded` 정답은 대기 조건 하나다:

```c
while ((u->readers != 1 || u->writer) && rc == 0) rc = pthread_cond_timedwait(&u->c, &u->m, &dl);
if (u->readers == 1 && !u->writer) { u->readers = 0; u->writer = true; rc = 0; }
```

혼자면 바로 성공한다(`PASS rc == 0 && g_leaf.writer == true …`). 강제 스케줄(둘 다 R → 장벽 → 둘 다 업그레이드 300 ms) 의 실제 출력:

```text
-- Part B: 강제 스케줄 — 두 스레드가 R 을 쥔 채 W 로 업그레이드 --
    LATCH DEADLOCK (upgrade): T0 holds R(leaf) waits W(leaf) [readers=2] -> T1 holds R(leaf) waits W(leaf) [readers=2] -> T0  (300 ms 동안 진행 0, 탐지기 없음 → 시간 제한으로 끊음)
PASS both_timed_out && ua[0].readers_seen == 2 && ua[1].readers_seen == 2
PASS ua[0].fix_rc == 0 && ua[1].fix_rc == 0 && g_leaf_n == 2
```

결정적인 이유: 두 스레드가 결과를 기록할 때까지 R 을 놓지 않도록 두 번째 장벽이 붙잡고 있으니, 대기 300 ms 내내 `readers == 2` 다. 시간 제한은 **탐지기 대용** 이다 — 진짜 래치(rwlock, 스핀락)에는 이것도 없다.

진짜 `pthread_rwlock_t` 는 어떨까? `demo_rwlock_upgrade.c` — 멈춘 스레드는 워치독(500 ms)이 보고만 하고 프로세스를 끝낸다.

<!-- code: demo_rwlock_upgrade.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_rwlock_upgrade.c -o rwup && ./rwup     # 1.1 초, exit 0
```

이 맥의 실제 출력:

```text
1) same thread: rdlock held, trywrlock -> 16 (Resource busy)
2) same thread: rdlock held, wrlock -> still blocked after 500 ms (self-deadlock, no EDEADLK)
3) two threads: both rdlock, both wrlock -> 0 of 2 returned after 500 ms (holding R: 2)
   LATCH DEADLOCK: T0 holds R(L) waits W(L) -> T1 holds R(L) waits W(L) -> T0
```

POSIX 는 "R 을 쥔 스레드가 `wrlock` 을 부르면 교착하거나 `EDEADLK`" 둘 다 허용한다. macOS 는 **혼자서도 그냥 멈췄다** (2). 업그레이드 API 자체가 없는 셈이다. 그래서 프로토콜 규칙이 "R 을 쥔 채 W 를 기다리지 않는다" 다.

**Coffman 4 조건으로 보기** (조건 표 자체는 OS-M10):

| 조건 | 래치 프로토콜에서 | 깨는가 |
|---|---|---|
| 상호 배제 | W 래치는 배타적 | 못 깸 (그게 래치의 목적) |
| 점유 대기 (hold and wait) | crabbing 은 부모를 쥔 채 자식을 기다린다 | 일부러 허용. 대신 순서로 막음. optimistic 재시작은 "놓고 다시" → 재시작 대기 동안은 깸 |
| 비선점 (no preemption) | 래치는 빼앗을 수 없다. 트랜잭션 abort 같은 것도 없다 | 못 깸 (깨려면 STOR-M08 처럼 희생자 abort 가 필요 — 래치에는 너무 비쌈) |
| 순환 대기 | 위 → 아래, 왼쪽 → 오른쪽 전역 순서 | **여기서 깬다**. 업그레이드는 같은 노드에서 "R 다음 W" 를 기다리는, 순서표에 없는 대기라서 순환이 생긴다 |

### Part B — 일부러 넣은 버그: 부모를 먼저 놓기 (`demo_planted.c`)

<!-- code: demo_planted.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_planted.c -o planted
./planted forced; ./planted lookup; ./planted lookup widen; ./planted insert; ./planted insert widen
cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_planted.c -o planted_tsan
./planted_tsan insert; ./planted_tsan forced
```

**버그 1 — lookup 이 `unlat(n)` 을 `lat_r(c)` 보다 먼저.** 강제 스케줄: 그 틈(hook)에서 writer 스레드가 꽉 찬 리프에 15 를 넣어 split 시키고, reader 는 40 을 찾는다. 실제 출력 (2 회 모두 같음):

```text
early-release (BUG):   lookup(40) = MISSING   (writer 가 틈 안에서 split 을 끝냈나: yes)  나중에 다시 lookup(40) = found
correct crabbing:      lookup(40) = found   (writer 가 틈 안에서 split 을 끝냈나: no — 200 ms 동안 막힘)  나중에 다시 lookup(40) = found
```

40 은 split 으로 오른쪽 새 노드 [14 15 20 30 40] 로 이사했다. reader 는 옛 포인터로 왼쪽 반쪽 [10 11 12 13] 을 뒤졌다. 올바른 crabbing 은 같은 자리에서 부모(root) R 을 쥐고 있으므로 writer 가 root W 에서 막혀 틈 안에서 아무것도 못 한다.

무작위 스트레스로는 거의 안 보인다 (쓰기 2 개가 오름차순으로 넣는 곳 근처를 읽기 2 개가 찾음, load ≈ 8): 틈을 안 넓히면 3 회 × 약 20 만 lookup 에서 miss **0**. 틈에 20 µs `nanosleep` 을 넣어야 3 회 × 약 7700 lookup 에서 miss 2 / 1 / 0. **TSan 은 이 버그를 못 본다** — 모든 노드 접근이 그 노드의 래치 안이라 데이터 레이스가 아니다 (`./planted_tsan forced` 경고 0, exit 0). 그래서 Part B 의 CHECK 는 "미리 넣은 키를 읽는 중에 항상 찾는가"(의미 검사)와 강제 스케줄로 짠다.

**B-link tree (Lehman & Yao 1981)** 는 바로 이 틈을 합법으로 만든다: 모든 노드(내부 포함)에 오른쪽 링크와 high key(이 노드가 맡는 최대 키)를 두고, 찾는 키가 high key 보다 크면 "오른쪽으로 이동(move right)". 그러면 reader 는 부모를 먼저 놓아도 되고, 한 번에 래치 하나만 쥔다. PostgreSQL nbtree 가 Lehman–Yao 방식이다 (소스 README 기준 — 세부는 확인 안 함).

**버그 2 — pessimistic insert 가 자식의 `nkeys` 를 래치 없이 엿보고(peek), 안전해 보이면 자식을 잡기 전에 조상을 놓는다.** 엿본 뒤 자식이 꽉 차면 split 을 올려 보낼 부모가 path 에 없다 → `path[0]` 을 root 로 착각하고 root_latch 없이 `t->root` 를 바꾼다 → 다른 가지가 통째로 사라진다. 4 스레드 × 5 만 키, 실제 출력:

```text
# 틈을 안 넓힘: 대부분 멀쩡 (14 회 중 2 회만 키 유실: 53 개, 373 개 — 이 둘은 root swap 카운터를 넣기 전 판, 버그 코드는 같음)
peek-bug insert: returned true 200000 / 200000, bt_check = 0, keys reachable 200000, lookup misses 0, root swaps without root_latch 0
# 틈에 20 µs: 매번 붕괴
peek-bug insert: returned true 200000 / 200000, bt_check = 6, keys reachable 9, lookup misses 199991, root swaps without root_latch 10904
peek-bug insert: returned true 200000 / 200000, bt_check = 4, keys reachable 71, lookup misses 199882, root swaps without root_latch 10959
```

키가 사라진 실행에서도 `bt_check = 0` 이 나온 적이 있다 (53·373 개 유실 때). 왼쪽 가지를 잃으면 남은 트리는 그 자체로 멀쩡한 B+tree 다. 그래서 CHECK 는 불변식만 보지 않고 **모든 키를 하나씩 다시 찾는다**. 이 버그는 진짜 데이터 레이스(래치 없는 읽기)라서 TSan 이 잡는다. 실제 리포트 (`./planted_tsan insert`, exit 134, 경고 2 개, 줄인 것):

```text
WARNING: ThreadSanitizer: data race (pid=9655)
  Read of size 4 at 0x00010a205f4c by thread T1 (mutexes: write M0):
    #0 insert_peek_bug demo_planted.c:58 (planted_tsan:arm64+0x100006694)
    #1 w_bug demo_planted.c:184 (planted_tsan:arm64+0x10000524c)

  Previous write of size 4 at 0x00010a205f4c by thread T2 (mutexes: write M1, write M2):
    #0 insert_peek_bug demo_planted.c:76 (planted_tsan:arm64+0x100006930)
    #1 w_bug demo_planted.c:184 (planted_tsan:arm64+0x10000524c)

  Location is heap block of size 352 at 0x00010a205e80 allocated by thread T2:
    #0 calloc <null> (libclang_rt.tsan_osx_dynamic.dylib:arm64e+0x679ac)
    #1 inner_split solution.c:134 (planted_tsan:arm64+0x10000561c)
…
ThreadSanitizer: reported 2 warnings
```

58 행 = `if (c->nkeys < MAXK)` (래치 없는 엿보기), 76 행 = `inner_split` 이 같은 노드의 `nkeys` 를 쓴 곳. T1 이 쥔 "write M0" 은 **다른 노드** 의 래치라서 HB 관계가 없다. TSan 리포트 읽는 법은 TOOL-M09. 이 실행에서는 키 유실은 0 이었다 — TSan 은 일어난 레이스를 잡을 뿐 결과가 틀렸는지는 모른다.

### Part B — 처리량 (`demo_bench.c`)

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o bench
uptime; ./bench ro; uptime; ./bench rh; uptime; ./bench wh; uptime     # 각 ≈ 10 초
```

조건: Apple M6 (`sysctl -n machdep.cpu.brand_string` → `Apple M6`, `hw.perflevel0/1/2.physicalcpu` = 2 / 4 / 6), Apple clang version 21.0.0 (clang-2100.0.123.102). 트리에 짝수 키 20 만 개를 오름차순으로 미리 넣고(노드가 반만 차서 높이 8 — 실측), 스레드마다 xorshift 로 lookup(0–40 만 범위, 절반 적중) / insert(31 비트 무작위 키). 칸 = 150 ms, 5 회 중앙값, 칸마다 트리 새로 만듦. coarse = 트리 전체에 rwlock 하나(lookup R, insert W, 노드 래치는 끔). nolatch = 래치 0 개 기준선(읽기 전용일 때만 안전). **다른 에이전트들의 벤치마크가 같은 맥에서 동시에 돌았다** — `uptime` load average 6.31 / 6.03 / 6.47 (세 부하 시작 시) → 비율만 보라. 코어 고정은 macOS 에 없다.

| Mops/s | T=1 | T=2 | T=4 | T=8 |
|---|---|---|---|---|
| **read-only** coarse | 6.59 | 12.66 | 11.96 | 11.57 |
| read-only crab (= opt 의 lookup) | 6.58 | 6.87 | 7.93 | 6.41 |
| read-only nolatch (기준선) | 7.75 | 18.76 | 42.90 | **78.80** |
| **read-heavy 95/5** coarse | 10.33 | 3.79 | 2.62 | 1.33 |
| read-heavy crab | 7.62 | 5.35 | 2.33 | 1.37 |
| read-heavy opt | 6.94 | 6.46 | 5.42 | **3.39** |
| **write-heavy 20/80** coarse | 6.03 | 0.60 | 0.66 | 0.66 |
| write-heavy crab | 6.16 | 0.58 | 0.47 | 0.52 |
| write-heavy opt | 4.81 | **1.95** | 0.97 | 0.63 |

opt 의 재시작 / 새 키 = 0.175 (T=8, 읽기·쓰기 위주 모두). 이 표 전에 load 13.7 / 9.4 / 9.0 / 6.7 에서 같은 실행 파일(3 회 중앙값 판)을 네 번 돌렸다. 절대값은 ±40% 흔들렸지만 순서는 같았다: read-heavy T=8 에서 opt 2.8–3.5 vs crab 1.0–1.4, write-heavy T=2 에서 opt 1.3–2.5 vs crab 0.19–0.60. T=1 칸은 특히 흔들린다 (E 코어에 올라간 듯 — 추정).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="40" y="22" fill="currentColor" font-weight="bold">read-heavy (lookup 95 / insert 5), Mops/s</text>
<line x1="40" y1="260" x2="362" y2="260" stroke="currentColor"/>
<line x1="40" y1="260" x2="40" y2="65" stroke="currentColor"/>
<text x="34" y="264" fill="currentColor" text-anchor="end">0</text>
<line x1="40" y1="260" x2="362" y2="260" stroke="currentColor" stroke-dasharray="2 4"/>
<text x="34" y="201" fill="currentColor" text-anchor="end">4</text>
<line x1="40" y1="197" x2="362" y2="197" stroke="currentColor" stroke-dasharray="2 4"/>
<text x="34" y="137" fill="currentColor" text-anchor="end">8</text>
<line x1="40" y1="133" x2="362" y2="133" stroke="currentColor" stroke-dasharray="2 4"/>
<text x="34" y="74" fill="currentColor" text-anchor="end">12</text>
<line x1="40" y1="70" x2="362" y2="70" stroke="currentColor" stroke-dasharray="2 4"/>
<rect x="50" y="96.4" width="20" height="163.6" class="f-mut" stroke="currentColor"/>
<text x="60" y="92" fill="currentColor" text-anchor="middle" font-size="11">10.3</text>
<rect x="72" y="139.3" width="20" height="120.7" class="f-warn" stroke="currentColor"/>
<text x="82" y="135" fill="currentColor" text-anchor="middle" font-size="11">7.6</text>
<rect x="94" y="150.1" width="20" height="109.9" class="f-ok" stroke="currentColor"/>
<text x="104" y="146" fill="currentColor" text-anchor="middle" font-size="11">6.9</text>
<text x="83" y="276" fill="currentColor" text-anchor="middle">T=1</text>
<rect x="128" y="200.0" width="20" height="60.0" class="f-mut" stroke="currentColor"/>
<text x="138" y="196" fill="currentColor" text-anchor="middle" font-size="11">3.8</text>
<rect x="150" y="175.3" width="20" height="84.7" class="f-warn" stroke="currentColor"/>
<text x="160" y="171" fill="currentColor" text-anchor="middle" font-size="11">5.3</text>
<rect x="172" y="157.7" width="20" height="102.3" class="f-ok" stroke="currentColor"/>
<text x="182" y="154" fill="currentColor" text-anchor="middle" font-size="11">6.5</text>
<text x="161" y="276" fill="currentColor" text-anchor="middle">T=2</text>
<rect x="206" y="218.5" width="20" height="41.5" class="f-mut" stroke="currentColor"/>
<text x="216" y="215" fill="currentColor" text-anchor="middle" font-size="11">2.6</text>
<rect x="228" y="223.1" width="20" height="36.9" class="f-warn" stroke="currentColor"/>
<text x="238" y="219" fill="currentColor" text-anchor="middle" font-size="11">2.3</text>
<rect x="250" y="174.2" width="20" height="85.8" class="f-ok" stroke="currentColor"/>
<text x="260" y="170" fill="currentColor" text-anchor="middle" font-size="11">5.4</text>
<text x="239" y="276" fill="currentColor" text-anchor="middle">T=4</text>
<rect x="284" y="238.9" width="20" height="21.1" class="f-mut" stroke="currentColor"/>
<text x="294" y="235" fill="currentColor" text-anchor="middle" font-size="11">1.3</text>
<rect x="306" y="238.3" width="20" height="21.7" class="f-warn" stroke="currentColor"/>
<text x="316" y="234" fill="currentColor" text-anchor="middle" font-size="11">1.4</text>
<rect x="328" y="206.3" width="20" height="53.7" class="f-ok" stroke="currentColor"/>
<text x="338" y="202" fill="currentColor" text-anchor="middle" font-size="11">3.4</text>
<text x="317" y="276" fill="currentColor" text-anchor="middle">T=8</text>
<rect x="50" y="288" width="12" height="12" class="f-mut" stroke="currentColor"/><text x="66" y="298" fill="currentColor">coarse</text>
<rect x="145" y="288" width="12" height="12" class="f-warn" stroke="currentColor"/><text x="161" y="298" fill="currentColor">crab</text>
<rect x="240" y="288" width="12" height="12" class="f-ok" stroke="currentColor"/><text x="256" y="298" fill="currentColor">opt</text>
<text x="410" y="22" fill="currentColor" font-weight="bold">read-only (lookup 100), Mops/s</text>
<line x1="410" y1="260" x2="732" y2="260" stroke="currentColor"/>
<line x1="410" y1="260" x2="410" y2="65" stroke="currentColor"/>
<text x="404" y="264" fill="currentColor" text-anchor="end">0</text>
<line x1="410" y1="260" x2="732" y2="260" stroke="currentColor" stroke-dasharray="2 4"/>
<text x="404" y="216" fill="currentColor" text-anchor="end">20</text>
<line x1="410" y1="212" x2="732" y2="212" stroke="currentColor" stroke-dasharray="2 4"/>
<text x="404" y="169" fill="currentColor" text-anchor="end">40</text>
<line x1="410" y1="165" x2="732" y2="165" stroke="currentColor" stroke-dasharray="2 4"/>
<text x="404" y="122" fill="currentColor" text-anchor="end">60</text>
<line x1="410" y1="118" x2="732" y2="118" stroke="currentColor" stroke-dasharray="2 4"/>
<text x="404" y="74" fill="currentColor" text-anchor="end">80</text>
<line x1="410" y1="70" x2="732" y2="70" stroke="currentColor" stroke-dasharray="2 4"/>
<rect x="420" y="244.3" width="20" height="15.7" class="f-mut" stroke="currentColor"/>
<text x="430" y="240" fill="currentColor" text-anchor="middle" font-size="11">6.6</text>
<rect x="442" y="244.4" width="20" height="15.6" class="f-warn" stroke="currentColor"/>
<text x="452" y="240" fill="currentColor" text-anchor="middle" font-size="11">6.6</text>
<rect x="464" y="241.6" width="20" height="18.4" class="f-acc" stroke="currentColor"/>
<text x="474" y="238" fill="currentColor" text-anchor="middle" font-size="11">7.8</text>
<text x="453" y="276" fill="currentColor" text-anchor="middle">T=1</text>
<rect x="498" y="229.9" width="20" height="30.1" class="f-mut" stroke="currentColor"/>
<text x="508" y="226" fill="currentColor" text-anchor="middle" font-size="11">12.7</text>
<rect x="520" y="243.7" width="20" height="16.3" class="f-warn" stroke="currentColor"/>
<text x="530" y="240" fill="currentColor" text-anchor="middle" font-size="11">6.9</text>
<rect x="542" y="215.4" width="20" height="44.6" class="f-acc" stroke="currentColor"/>
<text x="552" y="211" fill="currentColor" text-anchor="middle" font-size="11">18.8</text>
<text x="531" y="276" fill="currentColor" text-anchor="middle">T=2</text>
<rect x="576" y="231.6" width="20" height="28.4" class="f-mut" stroke="currentColor"/>
<text x="586" y="228" fill="currentColor" text-anchor="middle" font-size="11">12.0</text>
<rect x="598" y="241.2" width="20" height="18.8" class="f-warn" stroke="currentColor"/>
<text x="608" y="237" fill="currentColor" text-anchor="middle" font-size="11">7.9</text>
<rect x="620" y="158.1" width="20" height="101.9" class="f-acc" stroke="currentColor"/>
<text x="630" y="154" fill="currentColor" text-anchor="middle" font-size="11">42.9</text>
<text x="609" y="276" fill="currentColor" text-anchor="middle">T=4</text>
<rect x="654" y="232.5" width="20" height="27.5" class="f-mut" stroke="currentColor"/>
<text x="664" y="229" fill="currentColor" text-anchor="middle" font-size="11">11.6</text>
<rect x="676" y="244.8" width="20" height="15.2" class="f-warn" stroke="currentColor"/>
<text x="686" y="241" fill="currentColor" text-anchor="middle" font-size="11">6.4</text>
<rect x="698" y="72.8" width="20" height="187.2" class="f-acc" stroke="currentColor"/>
<text x="708" y="69" fill="currentColor" text-anchor="middle" font-size="11">78.8</text>
<text x="687" y="276" fill="currentColor" text-anchor="middle">T=8</text>
<rect x="420" y="288" width="12" height="12" class="f-mut" stroke="currentColor"/><text x="436" y="298" fill="currentColor">coarse</text>
<rect x="515" y="288" width="12" height="12" class="f-warn" stroke="currentColor"/><text x="531" y="298" fill="currentColor">crab</text>
<rect x="610" y="288" width="12" height="12" class="f-acc" stroke="currentColor"/><text x="626" y="298" fill="currentColor">nolatch</text>
<text x="40" y="322" fill="currentColor">Apple M6 · 150 ms×5 중앙값 · load 5.8–6.5 → 비율만. nolatch = 래치 0 개 (읽기 전용만 안전)</text>
</svg>
```

읽는 법:

1. **래치 0 개는 8 스레드에서 10배, 어떤 래치든 쥐면 1배.** 읽기만 하는데도 crab 이 6–8 Mops 에서 안 움직인다. rwlock 의 `rdlock`/`unlock` 은 잠금 워드에 원자적 RMW 를 하고(공유 읽기도 "쓰기" 다), 모든 lookup 이 같은 `root_latch` 와 root 노드 래치를 지난다 → 그 캐시 라인 두 개가 코어 사이를 핑퐁한다 (OS-M07 의 TAS 와 같은 현상). 깊이 8 이면 lookup 하나에 래치 연산 18 번. coarse 는 lookup 당 R 한 쌍뿐이라 crab 보다 빠르다. (macOS libpthread 의 rwlock 구현 소스는 확인하지 않았다 — RMW 는 일반적인 구현 기준의 설명이다.)
2. **쓰기가 섞이면 pessimistic 은 coarse 와 다를 게 없다.** crab 의 삽입은 매번 root_latch W + root W 를 지나간다. 5% 만 섞여도 T=8 에서 coarse 와 같은 1.3 Mops. 경합한 rwlock W 는 대기자가 커널에서 잠들었다 깨므로(macOS psynch — 측정으로 본 현상, 내부 미확인) T=1 → T=2 에서 10배 떨어진다.
3. **optimistic 은 root 를 R 로만 지나가므로** read-heavy T=8 에서 2.5배, write-heavy T=2 에서 3.4배. 그래도 rwlock R 자체가 늘지 않으니 T=8 write-heavy 는 다시 0.6 으로 모인다.
4. 그래서 실제 엔진은 **읽는 쪽이 공유 메모리에 쓰지 않는** 방식으로 간다: B-link(래치 하나씩), 그리고 OLC — 노드마다 버전 카운터, reader 는 래치 대신 "버전 읽기 → 내용 읽기 → 버전 재확인, 바뀌었으면 재시작"(LLAT-M02 seqlock 과 같은 원리), writer 만 래치 + 버전 증가 (Leis et al., "The ART of Practical Synchronization", DaMoN 2016 — 개념만, 구현은 이 레슨 밖). reader 가 캐시 라인을 더럽히지 않으니 nolatch 기준선에 가까워질 수 있다 (여기서는 측정 안 함).

### Part C — FTL L2P 인덱스

`l2p_write` 는 optimistic 삽입 + "있으면 덮어쓰기":

```c
if (i < n->nkeys && n->keys[i] == lba) { n->val[i] = ppa; unlat(n); return 0; }   /* 꽉 찬 리프여도 OK */
if (n->nkeys < MAXK) { leaf_put_at(n, i, lba, ppa); unlat(n); return 1; }
unlat(n); restarts++;
if (bt_insert_pess(t, lba, ppa)) return 1;   /* false = 그 사이 다른 큐가 같은 LBA 를 넣음 → 처음부터 (이번엔 덮어쓰기) */
```

실제 출력:

```text
-- Part C: NVMe 큐 4 개 → L2P 인덱스 --
    round 0: 새 매핑 16384, 덮어쓰기 0, 이번 라운드 restarts 2892
    round 1: 새 매핑 0, 덮어쓰기 16384, 이번 라운드 restarts 0
    round 2: 새 매핑 0, 덮어쓰기 16384, 이번 라운드 restarts 0
```

0 라운드 재시작 수(2892, 다른 실행 2889)는 스레드 타이밍에 따라 다르니 출력만 한다. 덮어쓰기 라운드의 **0** 은 매번 같다 → CHECK. SSD 의 정상 상태(steady state) 쓰기는 대부분 이미 매핑된 LBA 의 덮어쓰기이므로 optimistic 이 거의 공짜다.

펌웨어 현실 (Don 의 SSD 경험과 연결):

- 많은 SSD 는 L2P 를 **평면 배열**(LBA 하나당 4 바이트, DRAM 용량 ≈ 용량/1000)로 두고, 인덱스 트리는 KV-SSD, 호스트 FTL, 파일시스템 메타데이터(btrfs·XFS·APFS 는 B-tree 계열)에서 더 흔하다 — 제품마다 다르다(일반론, 확인 안 함).
- 멀티 코어 컨트롤러에서 NVMe 큐를 코어별로 나누는 이유가 바로 이 레슨의 숫자다: 공유 구조의 래치는 늘지 않는다. 그래서 펌웨어는 LBA 범위를 코어별로 **샤딩** 하거나(Part C 의 "큐 q 는 LBA ≡ q" 를 트리까지 나누는 것), 한 코어가 메타데이터를 소유하고 메시지로 요청을 받는다(OS-M08).
- ISR 은 래치를 기다릴 수 없다 (C-M14). 완료 인터럽트는 큐에 표시만 하고, 매핑 갱신은 태스크 문맥에서. RTOS 에서 우선순위 다른 태스크들이 같은 래치를 쓰면 우선순위 역전 (RTOS-M03/M04).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution     # 34/34 passed (6 회 반복, 매번 ≈ 1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_san && ./so_san   # 34/34, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan                                    # 34/34, 경고 0, 3.5 초
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 자식을 잡기 전에 부모를 놓음 | 그 틈에 자식이 split → 찾는 키가 오른쪽으로 이사 (강제 스케줄에서 MISSING). TSan 은 못 봄 | `lat_r(c); unlat(n);` 순서. 아니면 B-link 의 move-right |
| R 을 쥔 채 같은 래치를 W 로 (`rdlock` 후 `wrlock`) | 둘이 동시에 하면 순환 대기, macOS 는 혼자서도 멈춤 | 다 놓고 처음부터 W, 또는 U 모드 (연습 2) |
| 안전 판단을 래치 없이 엿본 `nkeys` 로 | 데이터 레이스(TSan 보고), 엿본 뒤 바뀌면 부모 없이 split → root 를 래치 없이 교체 → 가지 유실 | `lat_w(c)` 다음에 `safe_for_insert(c)` |
| root_latch 없이 `t->root` 를 읽거나 바꿈 | root split 과 경합 → 옛 root 로 시작한 스레드는 괜찮지만 root 교체끼리 충돌하면 가지 유실 | 읽기 `root_r`, 교체는 root_latch W 를 쥔 pessimistic 만 |
| optimistic 에서 꽉 찬 리프를 W 로 쥔 채 부모 W 를 다시 잡으러 감 | 아래 → 위 대기 = 순서 위반. 위에서 내려오는 pessimistic 과 순환 | 리프를 **놓고** 재시작 |
| 불변식만 확인하고 키 수는 안 셈 | 왼쪽 가지를 잃은 트리도 불변식은 통과한다 (`bt_check = 0` 인데 373 개 유실) | 모든 키를 다시 lookup, 개수 일치 |
| 처리량을 CHECK 에 넣음 | load 6–14 에서 ±40% 흔들림 | 정확성만 CHECK, 숫자는 데모 + `uptime` |
| ISR/우선순위 높은 태스크에서 트리 래치 | ISR 은 기다릴 수 없고, 태스크끼리는 우선순위 역전 | ISR 은 큐에 표시만, 갱신은 태스크에서 (C-M14, RTOS-M04) |

## 8. 면접 Q&A

<details><summary>Q1. latch crabbing 이 교착하지 않는 이유를 증명해 보세요.</summary>

모든 스레드가 래치를 **기다리는** 순서가 하나의 전역 순서(root_latch → 높은 층 → 낮은 층, 같은 층은 왼쪽 → 오른쪽)를 따른다. 스레드 A 가 B 를 기다린다면 A 가 원하는 래치는 A 가 쥔 모든 래치보다 순서상 뒤다. 사이클이 있으면 순서가 사이클을 따라 계속 커져 자기 자신보다 커야 하니 모순. 놓기는 기다림이 아니라 순서와 상관없다. Coffman 의 순환 대기 조건을 깬다 (OS-M10).

꼬리질문: "형제 링크로 스캔하는 reader 와 split 하는 writer 는?" — 스캔은 왼쪽 → 오른쪽으로만 기다리고, split 하는 writer 는 새 노드(아직 아무도 모름)만 오른쪽에 붙이므로 왼쪽을 기다리지 않는다. 역방향 스캔이 필요하면 no-wait(trylock 실패 시 재시작)로 한다 — 일반론, 엔진마다 다름.
</details>

<details><summary>Q2. 래치와 락의 차이는? 래치에는 왜 교착 탐지기가 없나요?</summary>

래치 = 메모리 자료구조의 물리적 일관성, ns–µs 동안, R/W. 락 = DB 내용의 논리적 일관성, 트랜잭션 끝까지, S/X/의도 락, lock manager 가 wait-for 그래프로 교착 탐지(STOR-M08). 래치는 초당 수천만 번 잡히므로 대기 그래프를 기록할 여유가 없고, 교착을 깨려면 "누군가를 abort" 해야 하는데 래치 수준에는 롤백할 트랜잭션이 없다. 그래서 프로토콜(순서)로 아예 못 생기게 한다.

꼬리질문: "그럼 래치 대기 중에 lock 을 기다리면?" — 래치를 쥔 채 락(긴 대기)을 기다리면 안 된다. 락을 기다려야 하면 래치를 놓고 기다린 뒤 다시 내려온다 — 일반 규칙 (Graefe 2010).
</details>

<details><summary>Q3. 노드가 "안전" 하다는 것은? delete 가 있으면?</summary>

삽입: `nkeys < MAX` (하나 더 와도 split 없음). 삭제: `nkeys > MIN` (하나 빠져도 merge/재분배 없음). 안전한 노드 아래의 구조 변경은 그 노드에서 멈추므로 그 위 래치를 놓을 수 있다. 이 레슨은 delete 가 없어서 삽입 기준만. delete 가 있으면 노드가 사라질 수 있으니 "놓은 노드가 해제되는" 문제(메모리 회수: OS-A03 epoch, LLAT-M03 hazard pointer)가 붙는다.

꼬리질문: "root 는?" — root 는 하한이 없다 (리프 root 는 0 키도 됨, 내부 root 는 자식 2 개 이상).
</details>

<details><summary>Q4. optimistic 은 언제 손해인가요? 재시작률은 얼마나 되나요?</summary>

재시작한 삽입은 R 경로 + W 경로를 두 번 걷는다. 재시작률 ≈ split 확률: 무작위 키 17.5%, 오름차순 키 24.8% (실측, 연습 4 — 4|5 split 이라 오른쪽 리프가 4 번마다 꽉 참). 쓰기만 하고 거의 다 split 하는 부하(대량 적재 bulk load)라면 pessimistic 이 낫고, 진짜 엔진은 bulk load 를 아예 따로(정렬 후 아래에서 위로 쌓기) 한다. 덮어쓰기가 많은 부하(Part C)는 재시작 0.

꼬리질문: "재시작을 줄이려면?" — 오른쪽 끝 리프에 오름차순 삽입이면 split 지점을 끝 쪽으로(예: 8|1) 옮겨 채움을 높이는 최적화가 있다 (InnoDB·nbtree 에 비슷한 휴리스틱이 있다고 알려짐 — 확인 안 함).
</details>

<details><summary>Q5. 제자리 R→W 업그레이드가 왜 교착이죠? 안전하게 업그레이드하려면?</summary>

두 스레드가 같은 래치를 R 로 쥔 채 둘 다 "다른 R 이 빠지기" 를 기다린다 = 2 노드 순환. 실측: `readers = 2` 로 300 ms 진행 0. macOS `pthread_rwlock_t` 는 혼자 `rdlock` 후 `wrlock` 해도 멈춘다(EDEADLK 없음). 해법: (1) 놓고 W 로 처음부터(점유 대기를 깸), (2) U(update) 모드 — R 과는 공존, U·W 와는 배타. 업그레이드할 수 있는 사람은 U 를 쥔 한 명뿐이라 순환이 안 생긴다 (연습 2, DB 의 U 락·SIX 와 같은 발상). STOR-M08 의 S→X 업그레이드 교착은 락 수준의 같은 문제다.

꼬리질문: "U 모드의 비용은?" — "쓸지도 모르는" reader 끼리 직렬화된다. 대부분 안 쓰는 부하라면 optimistic + 재시작이 더 싸다.
</details>

<details><summary>Q6. 동시 B+tree 를 어떻게 테스트하나요?</summary>

(1) 결정적 키 집합: 스레드 t 가 `k ≡ t (mod 4)` 를 넣으면 끝난 뒤 정확한 키 집합을 안다 → 모든 키 lookup + 개수. (2) 같은 키를 모두가 넣기 → 성공 합계 == 키 수 (중복 검사가 경합에서도 맞는지). (3) 쓰는 중에 미리 넣은 키를 읽기 → 항상 찾아야 함 (부모를 먼저 놓는 버그는 TSan 이 못 보니 이 검사가 필요). (4) 불변식 검사기. (5) 강제 스케줄로 특정 인터리빙 고정 (hook + 장벽). (6) TSan. 불변식만 보면 안 된다 — 가지를 잃은 트리도 불변식은 통과했다.

꼬리질문: "강제 스케줄은 테스트 코드가 프로덕션 코드에 hook 을 넣어야 하잖아요?" — 래치 도우미 한 곳(`lat_r`)에 함수 포인터 하나. 프로덕션 빌드는 매크로로 없앤다. 또는 deterministic 스케줄러(시뮬레이션 테스트).
</details>

<details><summary>Q7. 측정해 보니 crabbing lookup 이 스레드를 늘려도 안 빨라졌습니다. 왜죠? 어떻게 고치나요?</summary>

R 래치도 잠금 워드에 원자적 RMW 를 한다. 모든 lookup 이 root_latch 와 root 노드 래치를 거치므로 그 라인이 코어 사이를 핑퐁 → 1 스레드 6.6 Mops 가 8 스레드에서도 6.4. 래치 0 개 기준선은 7.8 → 78.8. 고치는 방향: reader 가 공유 메모리에 쓰지 않게 — OLC(버전 카운터 검증, seqlock 처럼), B-link(한 번에 래치 하나, 그래도 root 에선 핑퐁), root 를 epoch 보호 하의 lock-free 읽기로(RCU, OS-A03), 또는 샤딩(트리를 여러 개로).

꼬리질문: "OLC reader 가 읽은 노드가 그 사이 해제되면?" — 그래서 OLC 는 epoch 기반 회수와 같이 쓴다 (OS-A03).
</details>

<details><summary>Q8. root split 은 어떻게 안전하게 하나요? root_latch 가 왜 따로 필요하죠?</summary>

`t->root` 포인터는 root 노드의 내용이 아니다. root split 은 새 노드를 만들어 포인터를 바꾸므로 그 포인터를 지킬 래치가 필요하다. pessimistic 은 root_latch W 부터 잡고 root 가 안전하면 바로 놓는다. root 까지 split 이 올라오는 경우는 root 가 꽉 찼을 때뿐이고, 그때는 root_latch 를 아직 쥐고 있다(안전한 노드를 못 만났으니까). 옛 root 를 R 로 쥔 reader 는 영향이 없다 — 옛 root 는 새 root 의 왼쪽 자식으로 남는다.

꼬리질문: "root_latch 대신 root 노드를 절대 안 바꾸는 방법은?" — root 를 고정 노드로 두고, 꽉 차면 root 의 내용을 두 새 자식으로 옮기고 root 에는 구분 키만 남긴다 (일부 구현이 쓰는 방식 — 일반론).
</details>

## 9. 추가 연습

### 연습 1 — 범위 스캔: 형제 링크 crabbing

`bt_scan(t, lo, hi, out, max)`: lookup 처럼 `lo` 의 리프까지 내려간 뒤, 리프 층에서는 "다음 리프 R → 지금 리프 해제" 로 오른쪽으로만 가며 키를 모은다. 쓰기 2 개가 동시에 홀수 키를 넣는 동안 스캔 결과가 정렬·중복 없음·미리 넣은 짝수 키 전부 포함인지 확인하라.

<details><summary>답</summary>

내려가는 부분은 lookup 과 같고, `for (;;) { 모으기; nx = n->next; if (!nx) break; lat_r(nx); unlat(n); n = nx; }`. 지금 리프를 쥔 채 다음을 잡으므로 지금 리프는 그 사이 split 될 수 없고, 따라가는 `next` 는 최신이다. 이미 지나간 리프가 split 되어도 새 노드는 지나간 쪽에 붙는다. 실측: 스캔 22995 회, 이상한 결과 0. 왼쪽 → 오른쪽은 래치 순서의 일부라 writer(위 → 아래)와 교착하지 않는다 (연습 3 의 검사기로 위반 0 확인).
</details>

### 연습 2 — U(update) 래치

R 과는 공존하고 U·W 와는 배타인 U 모드를 `uxlatch_t` 에 더하라: `ux_ulock_bounded`, `ux_upgrade_bounded`(R 이 다 빠질 때까지만 기다림). main 이 R 을 쥔 동안 T0 가 U 를 잡고 업그레이드를 기다리고, T1 은 T0 뒤에 U 를 요청한다. 둘 다 시간 제한 안에 성공하고 쓴 순서가 T0, T1 이어야 한다.

<details><summary>답</summary>

`ulock`: `while (writer || upd) wait; upd = true;`. `upgrade`: `while (readers > 0) wait; upd = false; writer = true;`. 업그레이드를 기다리는 사람은 U 를 쥔 한 명뿐이고, 그 사람이 기다리는 R 보유자들은 아무것도 기다리지 않으므로(R 은 U 와 호환) 순환이 생기지 않는다. 두 번째 U 요청자는 **아무것도 쥐지 않은 채** 기다린다. 실측 출력: `T0 u=0 up=0, T1 u=0 up=0, 쓴 순서 0,1`, R + U 공존 확인.
</details>

### 연습 3 — 래치 순서 검사기 (mini lockdep)

스레드별 `_Thread_local` 쥔 래치 목록을 두고, 래치를 **잡기 전에** "새 래치의 층 < 쥔 모든 래치의 층, 또는 같은 리프 층이면 쥔 리프의 `next`" 인지 검사해 위반을 세라 (root_latch = 가장 높은 층). 올바른 lookup/insert/opt/scan 은 위반 0, "리프를 쥔 채 root 를 다시 잡기" 와 "오른쪽 리프를 쥔 채 왼쪽 리프" 는 각각 위반 1.

<details><summary>답</summary>

`ord_acq(obj, level)` 이 쥔 목록을 훑고 규칙을 어기면 `g_order_viol++` 후 목록에 추가, `ord_rel` 이 제거. 래치 도우미 6 개에 한 줄씩. OS-M11 의 lockdep 이 락 **클래스** 사이 순서 그래프를 만드는 대신, 여기는 순서가 미리 정해져(층 번호) 있으니 그래프 없이 비교만 한다. 혼자 돌려서 실제로 멈추지 않아도 위반을 잡는다 — lockdep 과 같은 가치. 실측: 4 스레드 스트레스(연습 1) 포함 위반 0, 일부러 틀린 두 경우 1 → 2.
</details>

### 연습 4 — 오름차순 삽입의 재시작률

빈 트리에 0..999 를 오름차순으로 optimistic 삽입하면 재시작이 몇 번인가? 식으로 예측하고 확인하라. 리프는 얼마나 차 있나?

<details><summary>답</summary>

처음 8 개로 root 리프가 꽉 차고 9 번째에서 첫 split(4 | 5). 그 뒤로는 오른쪽 끝 리프가 5 → 8 까지 3 개를 더 받고 그다음에 split 하므로 4 삽입마다 1 번. N ≥ 9 이면 restarts = ⌊(N − 5) / 4⌋ = 248 (24.8%). 마지막 리프를 빼면 모든 리프가 정확히 4 키(채움 50%), 높이 4. 무작위 순서는 174 (17.4%). 오른쪽 끝 삽입이 많은 키(타임스탬프, 자동 증가 ID, 순차 LBA)에서는 split 지점을 바꾸는 게 채움과 재시작 둘 다에 이득이다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_san && ./pr_san
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan
```

실제 출력: `18/18 passed` (세 빌드 모두, 5 회 반복, TSan 경고 0). practice.c 는 정답 코드를 복사해 두고 그 위에 연습을 얹었다 (래치 도우미에만 순서 검사 호출이 추가됨).

## 10. 핵심 정리

- 래치 = 노드의 물리적 일관성(짧게, R/W), 락 = 트랜잭션의 논리적 일관성(길게, 탐지기 있음, STOR-M08). 래치는 탐지기가 없어서 **순서**로 교착을 막는다: 위 → 아래, 같은 층은 왼쪽 → 오른쪽.
- crabbing: 자식을 잡고 → 부모를 놓는다. pessimistic insert 는 W 로 내려가며 "안전한"(`nkeys < MAX`) 노드를 만나면 조상을 다 놓고, split 은 쥔 경로를 거슬러 올라간다. root 포인터는 root_latch 가 따로 지킨다.
- optimistic: R 로 내려가 리프만 W, 꽉 찼으면 **다 놓고** 재시작. 혼자면 restarts == leaf split (무작위 17.5%, 오름차순 24.8%), 덮어쓰기는 재시작 0.
- R 을 쥔 채 W 를 기다리면(제자리 업그레이드) 두 스레드가 `readers = 2` 로 영원히 멈춘다. macOS `pthread_rwlock_t` 는 혼자서도 멈춘다. 해법 = 놓고 재시작(점유 대기 깸) 또는 U 모드(순환 깸).
- 부모를 먼저 놓는 버그: lookup 판은 데이터 레이스가 아니라 TSan 이 못 보고(강제 스케줄로 MISSING), insert 판(래치 없는 엿보기)은 TSan 이 잡는다. 불변식만으로는 부족 → 모든 키를 다시 찾는다.
- 실측: R 래치도 공유 라인에 RMW → crab lookup 1T 6.6 vs 8T 6.4 Mops, 래치 0 개는 7.8 → 78.8. optimistic 은 read-heavy 8T 에서 crab 의 2.5배. 더 가려면 B-link(move right)와 OLC(버전 검증, seqlock 원리).
- 펌웨어: 공유 인덱스 래치는 코어 수만큼 안 늘어난다 → LBA 샤딩 / 소유 코어 + 메시지, ISR 은 래치 금지.

## 참고자료

- R. Bayer, M. Schkolnick, "Concurrency of Operations on B-Trees", *Acta Informatica* 9, 1977 — 안전 노드, 비관적/낙관적 래치 프로토콜의 원조
- P. L. Lehman, S. B. Yao, "Efficient Locking for Concurrent Operations on B-Trees", *ACM TODS* 6(4), 1981 — B-link tree, high key, move right
- G. Graefe, "A Survey of B-Tree Locking Techniques", *ACM TODS* 35(3), 2010 — 래치 vs 락 구분, 래치 교착 회피 규칙
- V. Leis, F. Scheibner, A. Kemper, T. Neumann, "The ART of Practical Synchronization", DaMoN 2016 — optimistic lock coupling (OLC) (제목·저자는 기억 기준)
- CMU 15-445/645 *Database Systems*, "Index Concurrency Control" 강의 — latch crabbing/coupling, optimistic: <https://15445.courses.cs.cmu.edu/>
- E. G. Coffman, M. J. Elphick, A. Shoshani, "System Deadlocks", *ACM Computing Surveys* 3(2), 1971 — 4 조건
- OSTEP (Arpaci-Dusseau) 29장 *Lock-based Concurrent Data Structures* (hand-over-hand locking = lock coupling), 32장 *Common Concurrency Problems* (교착 4 조건, 락 순서): <https://pages.cs.wisc.edu/~remzi/OSTEP/>
- POSIX `pthread_rwlock_wrlock` / `pthread_rwlock_trywrlock` (호출 스레드가 R 을 쥐고 있으면 교착 또는 EDEADLK 가능), `pthread_cond_timedwait`: <https://pubs.opengroup.org/onlinepubs/9799919799/>
- PostgreSQL `src/backend/access/nbtree/README` — Lehman–Yao 기반 구현 설명
- 관련 레슨: OS-M10 (Coffman 4 조건, 전역 락 순서), OS-M11 (mini lockdep), ALGO-A10 (SCC), STOR-M08 (2PL lock manager, S→X 업그레이드 교착), SOCX-M05 · DCN-M05 (같은 순환 대기, 다른 층), OS-M07 (캐시 라인 핑퐁), LLAT-M02 (seqlock = OLC 의 버전 검증), OS-A03 (epoch 회수), TOOL-M09 (TSan 리포트 읽기), OS-M08 (메시지 패싱), C-M14 (ISR), RTOS-M03/M04 (우선순위 역전)
