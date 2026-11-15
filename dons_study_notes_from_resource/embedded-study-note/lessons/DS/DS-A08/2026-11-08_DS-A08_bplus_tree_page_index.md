---
id: DS-A08
time: 120분
updated: 2026-11-08
---
# DS-A08 B+ 트리: 페이지 단위 인덱스

## 학습 목표

- **노드 = 4 KB 페이지**인 B+ 트리를 고정 페이지 풀 위에 malloc 없이 만든다. 페이지끼리는 raw pointer가 아니라 **페이지 번호(page id)** 로 연결해서, 풀을 다른 주소로 통째로 복사해도 그대로 동작함을 테스트로 보인다.
- 삽입의 **리프 분할 = 구분키 복사(copy up)** 와 **내부 분할 = 구분키 이동(push up)** 의 차이를 그림과 코드로 설명하고, 임시 배열 없이 제자리에서 분할한다. 풀이 부족할 때 **트리를 반쯤 고친 채 실패하지 않게** 필요한 페이지 수를 먼저 센다.
- 삭제의 **재분배(redistribution) → 병합(merge) → 루트 축소(root collapse)** 를 왼쪽·오른쪽, 리프·내부 모든 경우로 구현하고, 합쳤을 때 왜 항상 한 페이지에 들어가는지 산수로 증명한다.
- **불변식 검사기**(정렬, 최소·최대 채움, 균일한 깊이, 구분키 정확성, 리프 사슬 = 중위 순서, 페이지 회계)를 직접 쓰고, 트리를 일부러 망가뜨리는 **고장 주입**으로 검사기 자체를 검증한다.
- 팬아웃, 높이, 페이지 채움률, 연산당 접촉 페이지 수를 실측해서 "왜 블록 장치에는 이진 트리가 아니라 고팬아웃 트리인가"를 숫자로 말한다. 그리고 NAND 위에서는 B+ 트리의 무엇이 깨지는지(제자리 갱신 불가, wandering tree) 설명한다.

## 1. 문제

"페이지 단위 B+ 트리를 짜 보세요. 삭제까지요." 파일시스템, DB, SSD 메타데이터 면접에서 나오는 문제이고, 이진 트리와 달리 **노드 하나가 곧 I/O 한 번**이라는 비용 모델을 알고 있는지를 본다. 펌웨어에서는 NOR/NAND 위의 인덱스, 익스텐트 맵, 로그 구조 저장소의 인덱스로 같은 구조가 나온다.

DS-M07(AVL)과 DS-A05(레드블랙)는 노드 하나에 키 하나다. 메모리에서는 괜찮지만, 노드 하나를 읽는 데 블록 I/O 한 번이 든다면 백만 개 키에 약 20번을 읽게 된다. B+ 트리는 **노드를 페이지 크기로 키워서** 키를 수백 개 담는다. 그러면 트리가 3층이고, 검색 한 번에 페이지 3개만 읽는다. 실제 측정치는 AVL 19.3 노드 대 B+ 3.00 페이지였다(표 4).

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">B+ 트리와 4 KB 페이지 배치 (링크는 전부 페이지 번호)</text>
  <rect x="270" y="36" width="210" height="32" class="f-acc" stroke="currentColor"/>
  <text x="280" y="57" fill="currentColor" font-family="monospace">root p7:  키 [40 | 70]</text>
  <path d="M300 68 L120 108" stroke="currentColor" fill="none"/>
  <path d="M375 68 L370 108" stroke="currentColor" fill="none"/>
  <path d="M450 68 L620 108" stroke="currentColor" fill="none"/>
  <text x="110" y="92" fill="currentColor" font-size="12">키 &lt; 40</text>
  <text x="385" y="92" fill="currentColor" font-size="12">40 ≤ 키 &lt; 70</text>
  <text x="590" y="92" fill="currentColor" font-size="12">키 ≥ 70</text>
  <g fill="none" stroke="currentColor">
    <rect x="20" y="108" width="190" height="32"/>
    <rect x="275" y="108" width="190" height="32"/>
    <rect x="530" y="108" width="190" height="32"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="30" y="129">p3 [10 20 30]</text>
    <text x="285" y="129">p9 [40 50 60]</text>
    <text x="540" y="129">p5 [70 80 90]</text>
  </g>
  <path d="M210 118 L275 118" stroke="currentColor" fill="none"/>
  <path d="M270 112 L275 118 L270 124" stroke="currentColor" fill="none"/>
  <path d="M275 130 L210 130" stroke="currentColor" fill="none"/>
  <path d="M215 124 L210 130 L215 136" stroke="currentColor" fill="none"/>
  <path d="M465 118 L530 118" stroke="currentColor" fill="none"/>
  <path d="M525 112 L530 118 L525 124" stroke="currentColor" fill="none"/>
  <path d="M530 130 L465 130" stroke="currentColor" fill="none"/>
  <path d="M470 124 L465 130 L470 136" stroke="currentColor" fill="none"/>
  <text x="215" y="156" fill="currentColor" font-size="12">next / prev = 리프 사슬 (범위 스캔)</text>

  <text x="10" y="188" fill="currentColor" font-weight="bold">페이지 하나의 내부 (4096 바이트, _Static_assert 로 고정)</text>
  <text x="10" y="212" fill="currentColor">리프</text>
  <rect x="70" y="196" width="64" height="26" class="f-mut" stroke="currentColor"/>
  <text x="76" y="214" fill="currentColor" font-family="monospace">hdr 16</text>
  <rect x="134" y="196" width="294" height="26" class="f-acc" stroke="currentColor"/>
  <text x="142" y="214" fill="currentColor" font-family="monospace">key[510]  (u32)  2040 B</text>
  <rect x="428" y="196" width="294" height="26" class="f-ok" stroke="currentColor"/>
  <text x="436" y="214" fill="currentColor" font-family="monospace">val[510]  (u32)  2040 B</text>
  <text x="10" y="254" fill="currentColor">내부</text>
  <rect x="70" y="238" width="64" height="26" class="f-mut" stroke="currentColor"/>
  <text x="76" y="256" fill="currentColor" font-family="monospace">hdr 16</text>
  <rect x="134" y="238" width="292" height="26" class="f-acc" stroke="currentColor"/>
  <text x="142" y="256" fill="currentColor" font-family="monospace">key[509]  2036 B</text>
  <rect x="426" y="238" width="292" height="26" class="f-ok" stroke="currentColor"/>
  <text x="434" y="256" fill="currentColor" font-family="monospace">child[510] 2040 B</text>
  <text x="10" y="288" fill="currentColor">hdr = { u16 kind, u16 n, u32 next, u32 prev, u32 stamp }. 키·값 배열 분리: 탐색은 키만 읽는다.</text>
  <text x="10" y="310" fill="currentColor">child = 페이지 번호(u32), 0 = 없음. 포인터가 아니라서 풀을 옮겨도 덤프해도 그대로 유효하다.</text>
</svg>
```

| 파트 | 하는 일 | 체크 (결정적) | 측정 |
|---|---|---|---|
| A | 검색, 삽입(분할), 범위 스캔, 풀 고갈, 위치 독립 | 모양 문자열로 분할 결과 고정, 내부 분할 3경우, 풀이 부족하면 무변경 | 팬아웃 |
| B | 삭제(재분배·병합·루트 축소) + 검사기 + 스트레스 | 10개 모양 시나리오, 고장 주입 9종, cap 3~510 무작위 스트레스 vs 참조 모델 | 분할/병합 횟수 |
| C | LBA 익스텐트 맵 (DS-M07 Part C의 AVL판을 B+로) | floor 함정, 덮어쓰기로 조각남, 합치기, 참조 L2P와 전수 비교 | 페이지 수 vs 평면 L2P |

### Part A — 검색·삽입·범위 스캔

- 페이지 풀은 호출자의 `page_t` 배열이다. `bt_init(t, pool, npages, leaf_cap, node_cap)`. 페이지 0은 쓰지 않아서 0 = "없음"이다.
- `leaf_cap` / `node_cap`이 **실행 시 인자**다. 레이아웃은 항상 4 KB이지만 cap 4로 돌리면 몇 개만 넣어도 분할이 일어난다. 실제 4 KB 팬아웃은 cap 510/509로 돌린다.
- `bt_descend`(경로 기록), `bt_search`, `bt_insert`(`BT_NEW` / `BT_UPD` / `BT_NOSPACE`), `bt_scan(lo, hi, ...)`. 리프 분할은 왼쪽이 ceil((cap+1)/2)개를 갖고, 내부 분할은 cap+1개 키의 인덱스 (cap+1)/2를 올린다(규약은 파일 머리 주석).
- **풀 고갈 시 트리는 바뀌지 않아야 한다.** 분할이 몇 층까지 번지는지 먼저 세어 `free_cnt`와 비교한다.

### Part B — 삭제와 불변식

- `bt_delete`: 리프가 최소 미만이면 `leaf_fix` → 병합이 일어났으면 부모로 올라가며 `node_fix` 반복 → 루트의 키가 0이 되면 하나 남은 자식을 새 루트로.
- 순서는 고정이다: **왼쪽에서 1칸 빌리기 → 오른쪽에서 1칸 빌리기 → 왼쪽과 병합 → (왼쪽 없으면) 오른쪽과 병합**. 형제는 같은 부모 아래의 것만 본다.
- `bt_check`(과제): 정렬, 채움, 깊이, 구분키, 리프 사슬(앞뒤 양방향), 키 개수, 도달 페이지 + free 사슬 + 페이지 0 = 전체. 검사기가 틀리면 다른 테스트가 무의미하니, 멀쩡한 트리를 한 군데씩 망가뜨려 전부 잡는지 시험한다.

### Part C — LBA 익스텐트 맵

- 키 = 시작 LBA, 값 = `(PBA << 8) | len`. `bt_floor`, `em_lookup`, `em_trim`(잘라내기·쪼개기), `em_write`(앞 익스텐트와 LBA·PBA가 모두 이어지면 합치기).
- 참조 모델은 `L2P[lba]` 배열이다. 임의의 쓰기·트림 뒤 모든 LBA를 대조하고, 익스텐트가 겹치지 않는지 본다.

## 2. Problem (English)

> **Page-oriented B+ tree index over a fixed page pool.**
>
> Build a B+ tree whose nodes are 4096-byte pages taken from a caller-supplied `page_t pool[npages]` (no malloc, no raw pointers between pages — links are 32-bit page ids, page 0 is reserved as NIL). Keys and values are `uint32_t`, keys are unique.
>
> ```c
> void bt_init(bt_t *t, page_t *pool, uint32_t npages, uint32_t leaf_cap, uint32_t node_cap);
> bool bt_search(bt_t *t, uint32_t key, uint32_t *val);
> int  bt_insert(bt_t *t, uint32_t key, uint32_t val);  // BT_NEW / BT_UPD / BT_NOSPACE
> bool bt_delete(bt_t *t, uint32_t key);
> uint32_t bt_scan(bt_t *t, uint32_t lo, uint32_t hi, uint32_t *ks, uint32_t *vs, uint32_t max);
> bool bt_floor(bt_t *t, uint32_t key, uint32_t *k, uint32_t *v);
> int  bt_check(const bt_t *t);   // BTC_OK or the first violated invariant
> ```
>
> Leaf split copies the first key of the right page up; internal split moves the middle key up. Deletes rebalance by borrowing one entry from the left sibling, else the right sibling, else merging (prefer left), shrinking the root when it loses its last key. If the pool cannot hold a split, `bt_insert` must return `BT_NOSPACE` and leave the tree untouched.
>
> Prove correctness against a reference model with a randomized workload (at least 1,000,000 operations at the real fanout) and an invariant checker covering: strict key order, min/max fill, uniform leaf depth, separator correctness, leaf chain = in-order traversal in both directions, and page accounting. Show the checker itself is effective by corrupting a valid tree in several distinct ways.
>
> Then use the tree as an LBA→extent map with `em_write(lba, len, pba)` that overwrites (trims/splits) existing extents, and verify every LBA against a flat reference L2P.
>
> Follow-ups: fanout and height for a billion keys? Why is the separator not updated when you delete the smallest key in a leaf? Could a merged page overflow? What is the cost of `prev` links? Why doesn't an FTL's L2P use a B+ tree, and what breaks on NAND?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과(실제 출력, 중간 생략):

```text
== Part A: search / insert with split / range scan
PASS sizeof(page_t) == 4096u && ((uintptr_t)g_pool & 4095u) == 0u
FAIL starter.c:363  bt_insert(&t, 5u, 50u) == BT_NEW && bt_search(&t, 5u, &v) && v == 50u
FAIL starter.c:364  bt_insert(&t, 5u, 51u) == BT_UPD && bt_search(&t, 5u, &v) && v == 51u && t.nkeys == 1u
  shape MISMATCH got []
  wanted (40: [10 20 30] [40 50])
...
SKIP height/fanout/no-space/relocation: unit checks failed
== Part B: delete (redistribute, merge, root collapse) + invariant checker
SKIP delete scenarios, checker, stress: Part A failed
== Part C: LBA extent map on the tree
SKIP extent map: Part A/B failed
1/55 passed
```

하네스 안내:
- **모양 문자열** `shape_is(&t, "(40: [10 20 30] [40 50])")`로 분할·재분배 결과를 고정한다. 괄호는 내부 페이지, 대괄호는 리프다. 규약(왼쪽 몫, 왼쪽 먼저)이 정해져 있어서 정답이 하나로 떨어진다.
- 페이지 접근은 `pg_r` / `pg_w`로만 한다. 연산당 **서로 다른 페이지** 수를 세고(`stamp`로 중복 제거), 범위 밖 번호는 페이지 0으로 돌려 `bad_id`를 올리므로 크래시 대신 체크에서 잡힌다.
- Part A가 통과해야 B, C가 돈다. 그래서 스텁이 멈추거나 죽지 않는다. 틀린 구현의 무한 루프는 `alarm(150)`이 끊는다.
- `bt_check`도 TODO다(Part B). 스텁은 `BTC_TODO`를 돌려준다.

관찰용 파일(체크와 별개, solution.c를 그대로 `#include`):

```bash
cc -std=c11 -O2 demo_measure.c -o dm && ./dm   # 높이·채움률·접촉 페이지 표 (결정적)
cc -std=c11 -O2 demo_bench.c   -o db && ./db   # 조회·스캔 시간 (ns, 이 맥)
```

## 4. 힌트

<details><summary>힌트 1 — 내부 페이지에서 내려갈 자식 고르기</summary>

`child[i]`의 범위가 `[key[i-1], key[i])`이다. 그러면 key와 같은 구분키를 만나면 **오른쪽 자식**으로 가야 한다. 즉 "key 보다 **큰** 첫 위치"가 자식 인덱스다(`ub_u32`). `lb_u32`를 쓰면 구분키와 같은 키를 왼쪽에서 찾게 되어 리프 첫 키를 찾을 수 없다.
</details>

<details><summary>힌트 2 — 임시 배열 없이 리프 분할</summary>

왼쪽이 최종적으로 가질 개수를 L = ceil((cap+1)/2)로 정한다. 새 키의 위치 i가 L보다 작으면 새 키는 왼쪽으로 간다. 그때 왼쪽은 L-1개만 남기고 옛 [L-1..] 을 오른쪽으로 보낸다. 아니면 옛 [L..] 을 오른쪽으로 보내고 새 키는 오른쪽의 i-L 자리에 넣는다. 어느 쪽이든 오른쪽은 cap+1-L개다. 이렇게 하면 페이지 두 개만 쓴다. `uint32_t tmp[511]`(2 KB) 스택 할당이 없다.
</details>

<details><summary>힌트 3 — 내부 분할은 세 경우다</summary>

개념상 cap+1개 키 K'를 만들면, mid = (cap+1)/2 번째 키가 위로 올라간다. 새 키 위치 p와 mid를 비교한다. `p < mid`면 올라가는 키는 옛 `key[mid-1]`, 왼쪽에 새 키가 들어간다. `p == mid`면 새 키 자체가 올라간다. `p > mid`면 옛 `key[mid]`가 올라가고, 오른쪽에 새 키가 들어간다. 자식은 "새 키의 **오른쪽**에 새 자식"이다.
</details>

<details><summary>힌트 4 — 사전 점검</summary>

리프가 꽉 찼으면 페이지 1개가 필요하다. 그 위로 부모가 꽉 찬 동안 1개씩 더 든다. 경로 끝(루트)까지 다 찼으면 새 루트로 1개 더 든다. 이 값을 `free_cnt`와 비교한 뒤에야 첫 페이지를 할당한다. 경로는 이미 내려오면서 다 읽었으니 추가 I/O가 없다.
</details>

<details><summary>힌트 5 — 구분키와 재분배</summary>

왼쪽에서 빌리면 내 첫 키가 바뀌므로 구분키 `key[idx-1]`를 내 첫 키로, 오른쪽에서 빌리면 오른쪽 형제의 첫 키가 바뀌므로 구분키 `key[idx]`를 오른쪽의 새 첫 키로 바꾼다. 내부 페이지의 재분배는 구분키가 **내려오고** 형제의 끝 키가 **올라간다**. 회전과 같다. 그리고 자식 하나가 따라 움직인다.
</details>

<details><summary>힌트 6 — floor 가 틀리는 경우</summary>

구분키 40이 남아 있고 오른쪽 리프의 첫 키 40이 지워지면, 45를 찾을 때 오른쪽 리프로 내려가지만 거기에 45 이하 키가 없다. 답은 왼쪽 리프의 마지막 키다. 그래서 `prev` 링크가 필요하다(또는 경로로 왼쪽 형제를 찾거나).
</details>

## 5. 풀이

### 설계: 왜 이 페이지 형식인가

| 결정 | 이유 |
|---|---|
| 노드 = 4096 바이트 | 팬아웃이 클수록 높이가 낮다. 4 KB는 많은 블록 장치·파일시스템의 단위이고, 부분 쓰기 경계와 맞다. `_Static_assert`로 고정 |
| 링크 = u32 페이지 번호 | 풀을 다른 주소로 복사, 덤프, 디스크/플래시에 저장해도 유효하다. 또 8바이트 포인터의 절반이라 팬아웃이 커지고, 번호는 범위 검사가 쉽다(나쁜 번호를 `bad_id`로 잡는다) |
| 키·값 배열 분리 | 이진 탐색이 키 배열(2 KB, 이 맥의 128 B 라인으로 약 16줄)만 건드린다 |
| 헤더에 `stamp` | 접촉 페이지 수를 세는 런타임 전용 필드. 실제 포맷이면 버퍼 풀 프레임에 둔다 (STOR-M01) |
| `next` + `prev` | 범위 스캔(next)과 floor·역방향(prev). `prev` 대가는 분할·병합 때 **옆 페이지 하나를 더 쓰는 것**이다 (접촉 페이지 표의 최대 쓰기 4) |
| cap이 실행 시 인자 | 같은 코드로 cap 4(모든 경로를 작은 입력에서 검증)와 cap 510(실제 수치)을 돌린다 |

위치 독립성은 테스트로 확인했다(`part_a_nospace_and_relocate`). 500개 키의 풀을 다른 배열에 `memcpy`하고 **원본을 0xA5로 덮은 뒤** 복사본의 `bt_check`, 검색, 삽입이 모두 통과한다. raw pointer가 하나라도 있으면 이 테스트에서 바로 죽는다.

### Part A — 삽입과 분할

**키 범위 규약이 전부다.** `child[i]` 안의 모든 키 k는 `key[i-1] ≤ k < key[i]`를 만족한다. 왼쪽 경계는 포함, 오른쪽 경계는 제외다. 그래서 검색은 "key보다 큰 첫 구분키의 인덱스"로 내려간다. 구분키는 **경계**이지 데이터가 아니다. 이 구분이 삭제에서 큰 차이를 만든다.

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">분할 두 가지 (cap = 4, 실제 출력 기준)</text>
  <text x="10" y="50" fill="currentColor">리프 분할: 구분키는 복사(copy up). 40 은 리프에도 남는다</text>
  <g fill="none" stroke="currentColor">
    <rect x="30" y="62" width="200" height="30"/>
    <rect x="330" y="104" width="130" height="30"/>
    <rect x="490" y="104" width="130" height="30"/>
  </g>
  <rect x="440" y="62" width="90" height="30" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace">
    <text x="42" y="82">[10 20 30 40] + 50</text>
    <text x="452" y="82">( 40 )</text>
    <text x="340" y="124">[10 20 30]</text>
    <text x="500" y="124">[40 50]</text>
  </g>
  <path d="M485 92 L395 104" stroke="currentColor" fill="none"/>
  <path d="M485 92 L555 104" stroke="currentColor" fill="none"/>
  <path d="M240 82 L320 82" stroke="currentColor" fill="none"/>
  <path d="M315 76 L320 82 L315 88" stroke="currentColor" fill="none"/>
  <text x="620" y="124" fill="currentColor" font-size="12"> ← L = 3 개</text>

  <text x="10" y="166" fill="currentColor">내부 분할: 구분키는 이동(push up). 60 은 어느 쪽 페이지에도 남지 않는다</text>
  <rect x="30" y="178" width="240" height="30" class="f-mut" stroke="currentColor"/>
  <text x="42" y="198" fill="currentColor" font-family="monospace">(20 40 60 80): + 70 → 5 키</text>
  <rect x="440" y="178" width="90" height="30" class="f-acc" stroke="currentColor"/>
  <text x="452" y="198" fill="currentColor" font-family="monospace">( 60 )</text>
  <g fill="none" stroke="currentColor">
    <rect x="320" y="222" width="130" height="30"/>
    <rect x="510" y="222" width="130" height="30"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="330" y="242">(20 40: ...)</text>
    <text x="520" y="242">(70 80: ...)</text>
  </g>
  <path d="M485 208 L385 222" stroke="currentColor" fill="none"/>
  <path d="M485 208 L575 222" stroke="currentColor" fill="none"/>
  <path d="M280 198 L430 198" stroke="currentColor" fill="none"/>
  <path d="M425 192 L430 198 L425 204" stroke="currentColor" fill="none"/>
  <text x="10" y="284" fill="currentColor">왜 다른가: 리프는 "모든 데이터는 리프에 있다"가 불변식이라 40 을 지울 수 없다.</text>
  <text x="10" y="306" fill="currentColor">내부 페이지의 키는 경계일 뿐이다. 60 이 양쪽 모두에 있으면 키 수가 어긋나고 자식 수와 안 맞는다.</text>
  <text x="10" y="340" fill="currentColor" font-family="monospace">내부 분할: mid=(cap+1)/2=2. 왼쪽 K'[0..1]=20 40, 올림 K'[2]=60, 오른쪽 K'[3..4]=70 80</text>
  <text x="10" y="362" fill="currentColor">자식은 (mid+1)개가 왼쪽, 나머지가 오른쪽: 자식 수 = 키 수 + 1 이 양쪽에서 유지된다.</text>
</svg>
```

제자리 분할의 세 경우(p < mid, p == mid, p > mid)를 한 번에 짜면 off-by-one이 잘 생긴다. 공식으로 보면, 개념상 수열 K'[0..cap]에서 왼쪽은 K'[0..mid-1], 위로 K'[mid], 오른쪽은 K'[mid+1..cap]이고, 코드의 세 경우는 이 수열을 옛 배열 인덱스로 옮긴 것뿐이다(`p > mid`면 옛 `key[mid+1..]`이 오른쪽, 새 키는 오른쪽의 `p-mid-1`). 분할 직후 왼쪽은 `ceil(cap/2)`, 오른쪽은 `floor(cap/2)`개다.

**삽입 뒤 최소 채움은 항상 지켜진다.** 리프: 오른쪽 `floor((cap+1)/2) ≥ ceil(cap/2)`. 내부: 오른쪽 `floor(cap/2)` 키, 최소 `ceil((cap+1)/2)-1` 키인데 cap 짝수일 때 cap/2 = cap/2, 홀수일 때 (cap-1)/2 = (cap+1)/2-1로 등호다. 그래서 그래서 어떤 cap에서도 분할 결과가 최소 채움을 지킨다. 이 레슨은 cap 3 이상(리프·내부)을 스트레스로 검증했다.

**실패 원자성.** `bt_insert`는 새 페이지를 **하나도 할당하기 전에** 필요한 수를 센다. 계산은 정확하다: 리프 1 + 부모가 꽉 찬 층 수 + (루트까지 다 찼으면 새 루트 1). 하네스 출력은 `pool exhausted after 16 keys, free pages left 1 (need > free for this split), tree intact`이고, 트리는 16개 키 그대로 `bt_check` 통과. 이 사전 점검을 뺀 변이는 **SIGBUS로 죽었다**(§6의 주입 버그 표). 분할 도중 `pg_alloc`이 0을 돌려줘도 아무도 확인하지 않기 때문이다. 펌웨어에서 "메모리 부족 시 조용히 망가진 인덱스"는 최악의 실패다.

**경로를 기록하는 이유.** B+ 트리 페이지에는 부모 링크가 없다. 부모 링크가 있으면 분할할 때마다 **옮겨 간 자식 수백 개의 부모 번호를 고쳐야** 해서 쓰기 페이지가 수백 개가 된다. 내려오면서 스택 배열 `step_t path[16]`에 적어 둔다(128 바이트, 높이가 16을 넘을 일은 없다).

### 범위 스캔

`bt_scan`은 lo의 리프로 내려간 뒤 `next`만 따라간다. 내부 페이지는 처음 내려갈 때 한 번만 읽는다. 1000개 키 스캔의 실측 평균은 6.11 페이지 읽기(높이 3 + 약 3 리프)다(표 3). AVL이었다면 1000개 키 각각이 다른 노드다. 시간 비교는 아래 표의 스캔 항목이다.

### Part B — 삭제

삭제의 핵심 불변식은 "최소 채움을 유지"가 전부고, 그 외 모든 것은 이것을 복구하는 절차다. 리프 최소는 ceil(cap/2), 내부 최소는 ceil((cap+1)/2)-1(키 수 기준)이다.

**첫 키를 지워도 구분키를 안 고친다.** 리프 [40 50 60]에서 40을 지우면 리프의 첫 키가 50이 되지만 부모의 구분키는 40 그대로다. 왜 괜찮은가. 구분키는 "이 리프의 첫 키"가 아니라 "경계"이기 때문이다. 불변식은 `왼쪽 최대 < 구분키 ≤ 오른쪽 최소`이지 `구분키 == 오른쪽 최소`가 아니다. 40은 이 부등식을 만족한다. 실제 출력 `(40: [10 20 30] [50 60])`이 그 상태이고 검사기를 통과한다. 고치러 올라가면 삭제마다 부모 페이지 쓰기가 추가된다.

그 대신 B+ 트리에서 내부 키는 "트리에 존재하는 키"가 아닐 수 있다. 이것이 **floor 함정**의 원인이다(Part C).

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">삭제: 재분배 vs 병합 vs 내부 페이지 회전 (실제 shape 출력)</text>
  <text x="10" y="48" fill="currentColor">① 왼쪽에서 빌리기 — 50 삭제로 [40] 이 최소(2) 미만, 왼쪽에 여유. 구분키 40→30</text>
  <g fill="currentColor" font-family="monospace">
    <text x="20" y="74">(40: [10 20 30] [40 50])</text>
    <text x="350" y="74">(30: [10 20] [30 40])</text>
  </g>
  <path d="M260 69 L340 69" stroke="currentColor" fill="none"/>
  <path d="M335 63 L340 69 L335 75" stroke="currentColor" fill="none"/>

  <text x="10" y="106" fill="currentColor">② 형제가 최소뿐이면 병합 — 빌릴 곳 없음 → 왼쪽과 합치고 부모에서 키 하나 제거, 루트 축소</text>
  <g fill="currentColor" font-family="monospace">
    <text x="20" y="132">(40: [10 20] [40 50])</text>
    <text x="350" y="132">[10 20 40]  (50 삭제, height 2 → 1)</text>
  </g>
  <path d="M240 127 L340 127" stroke="currentColor" fill="none"/>
  <path d="M335 121 L340 127 L335 133" stroke="currentColor" fill="none"/>

  <text x="10" y="164" fill="currentColor">③ 내부 페이지 회전 — 합쳐져 키가 1개가 된 왼쪽 내부 페이지가 오른쪽 형제에서 빌린다</text>
  <rect x="20" y="176" width="310" height="30" class="f-mut" stroke="currentColor"/>
  <text x="28" y="196" fill="currentColor" font-family="monospace">(7: [3 5 6] [7 8 9])  ← 키 1 개</text>
  <rect x="340" y="176" width="90" height="30" class="f-acc" stroke="currentColor"/>
  <text x="352" y="196" fill="currentColor" font-family="monospace">( 10 )</text>
  <rect x="440" y="176" width="280" height="30" class="f-mut" stroke="currentColor"/>
  <text x="448" y="196" fill="currentColor" font-family="monospace">(13 16 19 22: [10..] ...)</text>
  <text x="10" y="226" fill="currentColor">10 이 부모에서 내려와 왼쪽의 끝 키가 되고, 오른쪽의 첫 자식이 따라오며, 13 이 부모로 올라간다.</text>
  <text x="10" y="248" fill="currentColor" font-family="monospace">결과: (13: (7 10: [3 5 6] [7 8 9] [10 11 12]) (16 19 22: ...))</text>

  <text x="10" y="282" fill="currentColor">④ 내부 페이지 병합 → 루트 축소 (키가 내려와 "왼쪽 + 구분키 + 오른쪽")</text>
  <text x="10" y="306" fill="currentColor" font-family="monospace">(10: (7: [3 5 6] [7 8 9]) (13 16: [10 11 12] ...))</text>
  <text x="10" y="328" fill="currentColor" font-family="monospace">→ (7 10 13 16: [3 5 6] [7 8 9] [10 11 12] [13 14 15] [16 17 18])</text>
  <text x="10" y="358" fill="currentColor">리프 병합은 구분키를 버리지만(리프엔 데이터만), 내부 병합은 구분키를 끌어내린다(자식 수 = 키 수 + 1 때문).</text>
  <text x="10" y="382" fill="currentColor">병합 뒤 부모가 키를 하나 잃으므로 같은 절차를 부모에서 반복한다. 루트만 예외(키 0 → 자식이 새 루트).</text>
</svg>
```

**병합이 왜 항상 한 페이지에 들어가나.** 언더플로 쪽은 min-1, 다른 쪽은 min(min보다 많았으면 빌렸다)이다.

| 경우 | 합친 키 수 | cap 짝수 (cap = 2m 또는 min = cap/2) | cap 홀수 |
|---|---|---|---|
| 리프, min = ceil(cap/2) | (min-1) + min = 2min - 1 | cap - 1 ≤ cap | (cap+1) - 1 = cap ≤ cap |
| 내부, min = ceil((cap+1)/2)-1 | (min-1) + min + 1(내려온 구분키) = 2min | cap ≤ cap | cap - 1 ≤ cap |

내부 병합은 구분키 하나가 끼어드는 게 차이인데, 내부 min이 1 작아서 맞아떨어진다. cap을 정할 때 이 표가 설계 근거다.

**왜 재분배를 먼저 하나.** 재분배는 페이지 2개 + 부모 1개만 쓰고 끝난다. 병합은 형제를 비우고 부모 키를 지우며 부모의 언더플로를 부를 수 있다. 재분배는 부모의 키 수를 바꾸지 않으므로 위로 번지지 않는다. 그리고 왼쪽을 먼저 보는 것은 관례일 뿐이고, 어떤 쪽이든 결과가 불변식을 지키면 된다. 다만 하네스는 규약을 고정해서 답을 하나로 만든다.

**트랜잭션 없는 삭제의 위험.** 이 코드는 삭제 중간에 페이지 몇 개를 고치는 동안 전원이 나가면 반쯤 고쳐진 트리가 남는다. 메모리 풀이라 괜찮지만 플래시면 안 된다. 실제 시스템은 WAL이나 CoW로 다중 페이지 갱신을 원자적으로 만든다(STOR-M01, 그리고 "NAND 위에서" 절).

**1칸 재분배 vs 절반 재분배.** 실제로는 절반씩 나누면 다음 언더플로까지 여유가 더 생기지만, 쓰기가 더 커진다. 이 레슨은 규약을 단순히 1칸으로 뒀고, 임계값 효과는 연습 2에서 실측한다.

### 불변식 검사기와 고장 주입

`chk_rec`는 DFS로 내려가면서 범위 `[lo, hi)`를 넘겨 구분키를 검사한다. 리프에서 키가 이 범위 안인지 보면, 내부 페이지 어디의 구분키가 틀렸어도 결국 잡힌다(범위가 내려오므로). 리프는 DFS 순서로 방문되므로 그 순서가 사슬 순서와 같은지 `prev`와 `next` **양쪽**을 보면 리프 사슬 전체가 검증된다.

검사기는 트리를 빠르게 망가뜨리는 대신 **쓰는 검사기가 틀릴 수 있다**는 문제가 있다. 그래서 8가지 + 1가지 고장을 주입한다. 한 곳만 망가뜨려 다른 불변식은 살아 있게 만들어야 진짜 시험이 된다(예: "underfill leaf"는 뒤 2칸을 옆으로 옮기고 구분키까지 고쳐서 순서와 구분키는 맞게 둔다).

```text
  corrupt swap two keys          -> bt_check 2
  corrupt underfill leaf         -> bt_check 3
  corrupt separator +1           -> bt_check 5
  corrupt chain skips a leaf     -> bt_check 6
  corrupt prev link wrong        -> bt_check 6
  corrupt nkeys off by one       -> bt_check 7
  corrupt leaked page            -> bt_check 8
  corrupt leaf page marked free  -> bt_check 1
  corrupt uneven leaf depth      -> bt_check 4
```

코드 번호는 ORDER=2, FILL=3, DEPTH=4, SEP=5, CHAIN=6, COUNT=7, PAGES=8, KIND=1이다. "uneven leaf depth"가 가장 만들기 어려웠다. 자식 둘을 새 내부 페이지 Y로 묶어 한 층 내리면 채움(node_cap=3이라 내부 최소 1키), 순서, 사슬은 그대로이고 깊이만 깨진다.

### 무작위 스트레스 (solution.c 실제 출력)

```text
  cap   3/  3 ks     300 ops   30000 check every     1: max height 6, max keys 258 | leaf splits 2190, node splits 296, merges 2162+283, redist L796 R520, root collapses 5
  cap   4/  4 ks     512 ops   30000 check every     1: max height 5, max keys 429 | leaf splits 856, node splits 287, merges 814+275, redist L931 R498, root collapses 6
  cap   5/  5 ks     512 ops   30000 check every     1: max height 5, max keys 436 | leaf splits 1269, node splits 120, merges 1235+112, redist L892 R519, root collapses 1
  cap   6/  7 ks    1024 ops   30000 check every     1: max height 4, max keys 791 | leaf splits 656, node splits 92, merges 589+77, redist L996 R457, root collapses 0
  cap   8/  8 ks    2048 ops   30000 check every     1: max height 4, max keys 1455 | leaf splits 608, node splits 98, merges 464+72, redist L998 R467, root collapses 0
  cap  16/ 16 ks   32768 ops  300000 check every   256: max height 4, max keys 21016 | leaf splits 3264, node splits 259, merges 1875+121, redist L7579 R2992, root collapses 0
  cap 510/509 ks 1048576 ops 1000000 check every 16384: max height 3, max keys 249736 | leaf splits 763, node splits 1, merges 37+0, redist L1385 R345, root collapses 0
```

해석:
- 작은 cap에서는 **매 연산 뒤** `bt_check`와 스캔 전수 비교를 돌렸다(K=1). cap 16은 K=256, cap 510은 K=16384. 모든 스트레스의 끝에도 전수 비교한다. 연산 혼합은 삽입·삭제 비율이 바뀌는 3개 단계에 순차 커서(증가·감소)를 섞어서, 왼쪽 끝·오른쪽 끝 경로와 재분배·병합·축소를 다 지나가게 했다. 커버리지는 리프 분할·병합·좌우 재분배 카운터로 확인한다(하나라도 0이면 FAIL).
- **cap 510에서 내부 분할은 1번, 내부 병합은 0번**이다. 1M 연산으로도 높이 3 + 25만 키에서는 내부 페이지 활동이 드물다. 그래서 내부 페이지 경로는 같은 코드를 cap 3~16으로 돌려 검증했고, cap 510은 "실제 페이지 레이아웃 경계와 대규모 조합"을 검증한다. 실행 시 cap으로 만든 이유다.
- 시간: 일반 빌드 2.6 초, ASan+UBSan 빌드 4.3 초로, 스케일 축소 없이 1M회 그대로 돌렸다.

### 측정: 팬아웃, 높이, 채움률, 접촉 페이지

`demo_measure.c`의 실제 출력이다(시간 측정 없음). 리프 510키, 내부 509키/510자식.

<!-- code: demo_measure.c -->

**표 1. 무작위 삽입 (32비트 키)**

| n | 높이 | 리프 페이지 | 내부 페이지 | 리프 채움 % | 내부 채움 % | 바이트/키 |
|---|---|---|---|---|---|---|
| 100 | 1 | 1 | 0 | 19.6 | — | 40.96 |
| 1000 | 2 | 2 | 1 | 98.0 | 0.2 | 12.29 |
| 10000 | 2 | 32 | 1 | 61.3 | 6.1 | 13.52 |
| 100000 | 2 | 259 | 1 | 75.7 | 50.7 | 10.65 |
| 1000000 | 3 | 2729 | 9 | 71.8 | 59.6 | 11.21 |
| 10000000 | 3 | 30394 | 69 | 64.5 | 86.5 | 12.48 |

**표 1b. 같은 트리에 키를 늘리며 본 리프 채움률**

| n | 리프 페이지 | 리프 채움 % |
|---|---|---|
| 100000 | 259 | 75.7 |
| 200000 | 521 | 75.3 |
| 300000 | 956 | 61.5 |
| 400000 | 1048 | 74.8 |
| 500000 | 1349 | 72.7 |
| 600000 | 1903 | 61.8 |
| 700000 | 2022 | 67.9 |
| 800000 | 2101 | 74.7 |
| 900000 | 2288 | 77.1 |
| 1000000 | 2729 | 71.8 |

**표 2. 삽입 순서와 삭제 뒤 (n = 1,000,000)**

| 작업 | n | 높이 | 리프 | 내부 | 리프 채움 % | 내부 채움 % | 바이트/키 |
|---|---|---|---|---|---|---|---|
| 오름차순 | 1000000 | 3 | 3906 | 16 | 50.2 | 47.9 | 16.06 |
| 내림차순 | 1000000 | 3 | 3921 | 16 | 50.0 | 48.1 | 16.13 |
| 무작위 | 1000000 | 3 | 2729 | 9 | 71.8 | 59.6 | 11.21 |
| 무작위 후 절반 삭제 | 500000 | 3 | 1540 | 6 | 63.7 | 50.4 | 12.66 |
| 다시 50만 새 키 삽입 | 1000000 | 3 | 2723 | 9 | 72.0 | 59.4 | 11.19 |

**표 3. 연산당 서로 다른 페이지 접촉 (n=1e6, 각 20만 연산)**

| 연산 | 읽기 평균 | 읽기 최대 | 쓰기 평균 | 쓰기 최대 |
|---|---|---|---|---|
| 검색 (적중) | 3.00 | 3 | 0.00 | 0 |
| 삽입 (새 키) | 3.01 | 4 | 1.02 | 4 |
| 삭제 (있는 키) | 3.15 | 5 | 1.13 | 4 |
| 범위 스캔 1000키 | 6.11 | 7 | 0.00 | 0 |

**표 4. AVL 대조 (노드 하나 = 페이지 하나로 저장한다고 가정할 때의 접촉 수)**

| n | B+ 높이 | B+ 페이지/검색 | AVL 높이 | AVL 노드/검색 |
|---|---|---|---|---|
| 1000 | 2 | 2.00 | 12 | 9.18 |
| 10000 | 2 | 2.00 | 16 | 12.56 |
| 100000 | 2 | 2.00 | 20 | 15.95 |
| 1000000 | 3 | 3.00 | 24 | 19.32 |

읽는 법:
- **높이는 n에 거의 안 민감하다.** 천만 키도 높이 3이다. 이론 상한도 같다: 높이 h인 트리의 최소 키 수는 `2·leaf_min·(node_min+1)^(h-2)`이고 cap 510에서 leaf_min 255, node_min 254라 높이 4는 최소 `2·255·255²` ≈ 3316만 키를 요구한다. 표 1의 1000만은 높이 3이 정확하다. 10억 키는 최악 높이 4, 1조 키는 5다(연습 4).
- **무작위 삽입의 채움률은 흔들린다.** 표 1b 10개 표본은 61.5%에서 77.1% 사이였고 평균은 71.3%다. Yao(1978)의 분석은 고차 B-트리의 평균 활용률이 ln 2 ≈ 69%라고 보고하며, 이 맥의 측정도 그 근처다. 진동은 균일 난수에서 모든 리프가 비슷한 속도로 차서 분할이 몰려오기 때문으로 해석되지만, 분석으로 확인한 것은 아니다.
- **순차 삽입이 최악이다.** 오름차순·내림차순은 50%로, 무작위보다 페이지를 1.4배 쓴다(3906 대 2729). 왜냐하면 분할이 항상 "꽉 찬 오른쪽 끝"을 반으로 가르고, 왼쪽 절반은 다시는 차지 않기 때문이다. 연습 3의 fast-path가 이를 고친다. 실제 시스템도 맨 오른쪽 삽입을 특별 취급한다(PostgreSQL 문서: 오른쪽 끝 확장 시 리프를 fillfactor로 채움, B-tree 기본 90; SQLite `btree.c`의 `balance_quick()` 주석은 맨 오른쪽 삽입을 특수 경우로 처리한다고 설명한다).
- **접촉 페이지**: 삽입의 쓰기 평균 1.02는 "거의 항상 리프 하나"이고, 분할이 일어날 때만 최대 4페이지(왼쪽, 새 오른쪽, 옆 리프의 prev, 부모)다. 읽기 최대 4는 분할 때 옆 리프의 `prev`를 고치러 그 페이지를 읽기 때문이다. 삭제의 읽기 평균 3.15는 일부가 재분배·병합 때 형제를 읽기 때문이다.
- AVL의 노드 방문은 n=1e6에서 19.3으로 log₂n(19.9)에 가깝고 B+는 3페이지다. 이것이 **하드디스크, SSD, 플래시에서 B+가 쓰이는 이유**다. 각 방문이 임의 I/O 한 번이면 약 6.4배 차이다.

**시간 (demo_bench.c, 이 맥 Apple M6, -O2, 7회 실행의 중앙값; 부하 평균 약 11)**

| 구조 (n = 1,000,000, 조회 200만 회) | 중앙값 ns/조회 | 최소 | 최대 |
|---|---|---|---|
| B+ 트리 (4 KB, cap 510) | 65.9 | 63.2 | 75.8 |
| AVL (인덱스 링크) | 100.2 | 91.1 | 126.3 |
| 정렬 배열 이진 탐색 | 38.6 | 36.0 | 41.3 |

| 구조 (n = 1,000,000, 1000키 범위 스캔) | 중앙값 ns/스캔 | 최소 | 최대 |
|---|---|---|---|
| B+ 리프 사슬 | 689 | 586 | 865 |
| AVL 중위 순회 | 5672 | 5560 | 10009 |

작은 n에서도 비슷한 경향이다(n=1e4에서 조회 B+ 28.8 / AVL 32.2 / 정렬 배열 21.6 ns, 스캔 518 / 1502 ns). 같은 프로그램을 부하 평균 약 40일 때 돌린 첫 측정은 절댓값이 1.5~2배 컸지만 순서는 같았다(n=1e6에서 조회 98/195/65, 스캔 1012/9240). **결론은 비율이다.** 메모리 안에서도 B+는 AVL보다 조회가 1.5배, 긴 스캔은 8배 빨랐다. 스캔은 연속 메모리 순회 대 포인터 추적의 차이이고, 정렬 배열이 조회는 최고이지만 삽입이 O(n)이다.

타이밍 주의: 이것은 총 시간/횟수 평균이다. 한 연산 시간(수십 ns)은 타이머 분해능 ≈ 41.7 ns로 정확히 잴 수 없으니 최악 지연은 위의 **접촉 페이지 수**로 판단하는 편이 맞다.

### Part C — 익스텐트 맵

DS-M07 Part C는 익스텐트 맵을 AVL로 만들었다. 같은 문제를 페이지 트리로 바꾸면 달라지는 것은 **메모리 레이아웃과 범위 스캔**이고, 새로 생기는 어려움은 **구분키 함정**이다.

```text
em_write(lba=120, len=10, pba=5000)   // 기존 [100,150) → 1000
  [100,150)→1000  ⇒  [100,120)→1000, [120,130)→5000, [130,150)→1030  (3 조각)
```

`bt_floor`는 "key 이하 최대 키"다. 리프 안에서 `ub`로 찾아 인덱스 i-1이다. i가 0이면 그 리프에는 key 이하가 없다는 뜻이고, 왼쪽 리프의 마지막 키가 답이다. **구분키가 살아 있고 리프 첫 키가 지워졌을 때** 이 경우가 생긴다. 하네스는 `(40: [10 20 30] [50 60])`에서 floor(45)를 검사한다. `prev` 없이 NIL을 돌려주는 변이는 FAIL 3개로 잡혔다.

`em_trim`의 경계:

| 상황 | 처리 |
|---|---|
| 시작이 a 앞이고 a를 지나감 | 앞부분만 남김 (같은 키로 `bt_insert` → 값 갱신 = 길이 축소) |
| 그 익스텐트가 b도 지나감 (가운데 구멍) | 꼬리 `[b, end)`를 pba 오프셋과 함께 새 키로 삽입, 끝 |
| 시작이 [a, b) 안 | 삭제. b를 넘으면 꼬리 삽입 후 끝 |

끝에서 합치기: 앞 익스텐트가 LBA·PBA 모두 이어지고 합친 길이 ≤ 255면 값만 갱신한다. 순차 쓰기 8섹터 × 1000회는 익스텐트 33개가 된다(31회 = 248섹터가 한 익스텐트, 1000 = 32·31+8). 1000개 키 대신 33개이므로 페이지가 훨씬 적다.

참조 모델과의 대조는 모든 LBA를 `em_lookup`해 `L2P[lba]`와 비교하고, 스캔으로 익스텐트가 겹치지 않는지 본다. 결과(cap 510, LBA 공간 32768, 20만 쓰기/트림):

```text
  extents 1263, height 2, pages 5 = 20480 B  vs flat L2P 131072 B (LBA space 32768)
```

이 숫자를 **그대로 믿으면 안 된다.** 이 부하는 순차 커서(16번 중 6번)가 섞여 있어서 합치기가 잘 된다. 최악은 모든 LBA가 이어지지 않는 작은 쓰기인데, 그러면 익스텐트 수 ≈ LBA 수이고 트리는 표 1의 바이트/키 그대로 키당 약 11바이트를 쓴다. 평면 L2P의 항목당 4바이트보다 커진다. 그래서 실제 SSD FTL은 페이지 단위 L2P를 쓰고 캐시에 일부만 올리며(DFTL류), 파일시스템과 대용량 순차 위주의 매핑이 익스텐트를 쓴다. ext4의 extent 트리가 그 예다(`ee_block`/`ee_len`/`ee_start`, 트리 깊이 최대 5; 커널 문서 ifork 절). MEM-M10 계열이 FTL 쪽을 다룬다.

### 펌웨어·플래시 관점

**Cortex-M에서.** 4 KB 노드를 RAM에 올리면 동시에 몇 개만 둘 수 있다. RAM 64 KB면 페이지 16개다. 현실적으로는 버퍼 풀 크기가 트리 크기의 한도이고, 또 하나의 비용은 `memmove`다. 리프 삽입 최악은 키·값 각각 2 KB를 민다. 인터럽트 비활성 구간에서 실행하면 지연이 길어지므로 태스크 문맥에서 돌린다. 스택은 `path[16]` 128바이트가 전부고, 재귀는 검사기(`chk_rec`, 높이로 유계)에만 있다. 임시 배열을 안 쓰도록 짠 이유도 스택이다.

**NAND에서는 제자리 쓰기를 못 한다.** 이 레슨의 `pg_w`는 메모리에서 바로 페이지를 고친다. NAND에서는 한 페이지를 고치려면 다른 물리 페이지에 새로 써야 한다. 그러면 페이지 번호가 바뀌고, 부모의 child를 고쳐야 하고, 부모도 새로 써야 하고… 루트까지 전파된다. 이것이 **wandering tree 문제**다. 해결은 간접 참조다. F2FS는 node address table(NAT)을 두어 페이지 번호 → 실제 위치 변환을 한 곳에 몰아서 전파를 끊는다(커널 문서 `f2fs`). SSD에서는 FTL이 이미 같은 간접 참조를 한다. 그래서 B+ 트리를 SSD 위에 놓고 쓰면 제자리 쓰기처럼 보이지만, 쓰기 증폭은 숨어 있다. 키 8바이트를 바꾸려고 4 KB를 다시 쓰는 것이고, 그것이 NAND 페이지 안에서 또 부분 프로그래밍 제약과 GC에 들어간다.

**페이지 크기 맞추기.** 트리 페이지(4 KB)와 NAND 페이지(요즘 16 KB 전후)와 FTL 매핑 단위(보통 4 KB)가 다르다. 트리 페이지 경계가 매핑 단위에 맞으면 읽기·수정·쓰기를 줄일 수 있지만, 쓰기 묶음의 크기는 NAND 페이지다. 이 관계를 무시하면 쓰기 증폭이 커진다. 정량 분석은 MEM 트랙의 영역이다.

**ISR에서 호출?** 금지. 분할·병합이 페이지 여러 개를 바꾸는 동안 다른 태스크가 트리를 읽으면 안 된다. 태스크에서 mutex로 보호하고, 동시성이 필요하면 래치 프로토콜(STOR-M10)을 쓴다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 55/55 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o s_asan && ./s_asan
```

검증한 결과: 일반 빌드 `55/55 passed` (2.6 초), ASan+UBSan 빌드 `55/55 passed` 보고 없음 (4.3 초, 스트레스 횟수 축소 없음). 바뀐 곳은 TODO 16개(`bt_descend`, `bt_search`, `leaf_split_insert`, `node_split_insert`, `bt_insert`, `bt_scan`, `leaf_fix`, `node_fix`, `rebalance`, `bt_delete`, `chk_rec`, `bt_check`, `bt_floor`, `em_lookup`, `em_trim`, `em_write`)이다.

하네스가 틀린 코드를 정말 잡는지 solution.c 복사본에 버그를 넣어 시험했다(변이는 lesson 폴더에 두지 않음, 실제 결과).

| 주입한 버그 | 결과 |
|---|---|
| 리프 분할에서 옆 리프의 `prev` 갱신 누락 | FAIL 15개 |
| 왼쪽 빌리기 구분키를 `key[1]`로 잘못 설정 | FAIL 2개 뒤 손상된 트리에서 멈춤, CPU 제한으로 종료 (하네스에 `alarm`을 둔 이유) |
| 병합 때 `dst->next` 갱신 누락 | FAIL 22개 |
| 사전 점검(풀 고갈) 제거 | SIGBUS 충돌 |
| 내부 분할 p > mid 삽입 위치 off-by-one | SIGBUS 충돌 |
| floor의 prev 경로 제거 | FAIL 3개 |
| 내부 병합에서 구분키 끌어내리기 누락 | FAIL 7개 |
| 검사기의 구분키 검사 제거 | FAIL 1개 (separator +1 고장 미탐지) |
| 검사기의 `prev` 검사 제거 | FAIL 1개 |
| `em_trim`의 꼬리 삽입 누락 | FAIL 3개 |
| 루트 축소에서 옛 루트 페이지 반납 누락 | FAIL 10개 (페이지 회계) |
| 내부 오른쪽 빌리기의 자식 위치 오류 | FAIL 1개 뒤 같은 방식으로 CPU 제한 종료 |

## 7. 자주 하는 실수

1. **내부 분할에서도 구분키를 복사한다.** 왼쪽 또는 오른쪽에 키가 남으면 자식 수 = 키 수 + 1이 깨진다. → 리프만 copy up, 내부는 push up.
2. **구분키 비교에서 `lb`를 쓴다.** key == 구분키이면 오른쪽 자식으로 가야 한다. `lb`면 왼쪽으로 가서 리프 첫 키를 영영 못 찾는다. → 내부는 `ub`, 리프는 `lb`.
3. **리프 분할에서 옆 리프의 `prev`를 안 고친다.** 스캔은 next만 보니 정상처럼 보인다. 역방향과 floor만 틀리고 검사기의 `prev` 검사로만 잡힌다. 분할·병합에 사슬 고치기가 항상 따라다닌다.
4. **첫 키가 지워졌다고 부모 구분키를 고친다.** 맞지 않는 것은 아니지만 쓰기만 늘고, 재분배 때 쓰는 구분키와 혼동된다. 경계 의미를 기억한다.
5. **재분배 후 구분키를 안 고친다** (또는 잘못된 쪽을 고친다). 왼쪽에서 빌렸으면 `key[idx-1] = 내 첫 키`, 오른쪽에서 빌렸으면 `key[idx] = 오른쪽의 새 첫 키`.
6. **내부 병합에서 구분키를 빠뜨린다.** 리프 병합은 구분키를 그냥 버리지만 내부 병합은 구분키가 내려와 왼쪽과 오른쪽 사이에 끼어야 자식 수가 맞는다.
7. **내부 빌리기에서 자식을 안 옮긴다** (또는 잘못된 자리에 넣는다). 왼쪽 형제의 **마지막 자식**이 내 **첫 자식**이 되고, 오른쪽 형제의 **첫 자식**이 내 **마지막 자식**이 된다.
8. **루트 축소를 잊는다.** 루트가 키 0개로 남으면 `height`가 줄지 않아 빈 내부 페이지를 계속 내려간다. 또 옛 루트를 `pg_free`해야 페이지 회계가 맞는다.
9. **분할 중에 풀이 동날 수 있다.** 리프를 나눈 뒤 부모를 나누려다 할당 실패면 트리가 반쯤 고쳐진 채 남는다. → 분할 전에 필요한 페이지 수를 센다.
10. **floor에서 리프 안만 본다.** 구분키가 살아 있는 동안 리프의 첫 키가 사라지면 답은 왼쪽 리프에 있다. → `prev` 또는 경로로 왼쪽 형제.

## 8. 면접 Q&A

<details><summary>Q1. B 트리와 B+ 트리의 차이는? 왜 인덱스에는 B+ 트리를 쓰나요?</summary>

B 트리는 내부 노드에도 값을 둔다. B+ 트리는 **값은 리프에만** 있고 내부 노드에는 구분키와 자식만 있다. 그 결과 ① 내부 페이지에 키를 더 많이 넣을 수 있어 팬아웃이 커지고(이 레슨: 내부 509키) 높이가 낮아진다. ② 모든 값이 같은 깊이에 있어서 검색 비용이 균일하다. ③ 리프를 사슬로 이어서 범위 스캔이 내부 페이지를 다시 안 읽는다. 대신 값이 내부 페이지에 없으니 단건 조회에서 항상 리프까지 내려간다. 작은 차이고 실무에서 범위 스캔 이득이 훨씬 크다.
</details>

<details><summary>Q2. 팬아웃과 높이는 어떻게 추정하나요? 10억 키면 몇 층이죠?</summary>

페이지 크기 / (키 크기 + 링크 크기)가 내부 팬아웃이다. 이 레슨은 8바이트 엔트리로 510이다. 키가 더 크면 팬아웃이 줄어드니 prefix compression이 의미가 있다(STOR-M02). 높이는 `log_fanout(n)`인데 실제로는 채움률이 들어간다. 무작위 삽입 70%면 유효 팬아웃 약 350이고 350³ = 4288만, 350⁴ = 150억이다. 그래서 10억 키는 높이 4다. 내 공식(연습 4)으로 cap 510이면 10억 키 높이는 정확히 4(최소 높이와 최악 높이가 같다), 1조 키는 5다. 하나의 검색은 페이지 4개이고, 위쪽 층을 캐시에 두면 사실상 1~2번의 I/O다. 면접에서 "높이 3이나 4라서 버퍼 풀이 거의 다 처리한다"를 말할 수 있으면 좋다.
</details>

<details><summary>Q3. 삭제에서 왜 먼저 재분배를 시도하나요? 언제 병합하나요?</summary>

재분배는 부모 키 수를 안 바꾸니까 위로 번지지 않고, 페이지 3개(자신, 형제, 부모)만 쓴다. 병합은 페이지를 반납해서 공간을 회수하지만 부모의 키를 지우므로 연쇄가 될 수 있다. 그리고 재분배가 불가능한 경우(형제가 최소)에만 병합한다. 그리고 병합 직후 페이지는 거의 꽉 찰 수 있다. 다시 삽입되면 분할되어 merge-split 진동이 생긴다. 그래서 실제 시스템은 임계값을 내린다. InnoDB의 `MERGE_THRESHOLD`는 기본 50%이고 낮출 수 있다(문서가 merge-split 진동을 경고한다). 연습 2의 실측에서는 최소를 8에서 4로 줄이자 1000 사이클에서 병합과 분할이 각각 1000번에서 0번이 되고 페이지 쓰기가 10000번에서 6000번으로 줄었다. PostgreSQL은 한 걸음 더 나아가 완전히 빈 페이지만 삭제하고 부분 채움 페이지는 합치지 않는다(`nbtree` README).
</details>

<details><summary>Q4. 왜 부모 포인터를 저장하지 않죠? 왜 경로 스택을 쓰나요?</summary>

분할할 때 옮겨 간 자식이 수백 개인데 부모 링크가 있으면 그 자식 페이지를 전부 읽고 고쳐야 한다. 쓰기 페이지가 2개에서 수백 개가 된다. 경로를 내려가면서 스택에 적으면 비용은 스택 메모리(이 레슨 128바이트)뿐이다. 리프의 `next/prev`는 예외인데, 형제 사이는 분할 때 고쳐도 옆 한 페이지뿐이고 범위 스캔 효용이 크기 때문이다.
</details>

<details><summary>Q5. 왜 raw pointer 대신 페이지 번호를 쓰죠? 어떤 차이가 있나요?</summary>

세 가지다. ① **위치 독립**: 풀을 다른 주소로 옮기거나 파일/플래시에 저장했다가 다시 올려도 유효하다. 테스트는 원본을 덮어쓰고 복사본으로 동작을 확인한다. 포인터면 저장할 때 전부 바꿔 써야 한다(swizzling). ② **크기**: 64비트 시스템에서 포인터 8바이트 대 번호 4바이트. 내부 엔트리가 12바이트에서 8바이트로 줄어 4 KB 페이지의 키 수가 약 339에서 509로 는다. ③ **검증과 버퍼 풀**: 번호는 범위 검사가 쉽고, 버퍼 풀을 끼우면 "번호 → 프레임" 변환을 거쳐 페이지가 메모리에 없을 때 읽어 올 수 있다(STOR-M01). 비용은 역참조마다 배열 인덱스 계산 하나다.
</details>

<details><summary>Q6. 트리가 깨졌는지 어떻게 테스트하나요? 그 테스트는 어떻게 믿죠?</summary>

독립적인 참조 모델(여기서는 `g_ref[key]` 배열)과 매 연산 결과를 비교하고, 구조 검사기로 불변식을 본다. 작은 입력에서는 매 연산 뒤, 큰 입력에서는 K번마다. 고정 시드라 실패가 재현된다. 그리고 **검사기 자체를 검증**해야 한다. 멀쩡한 트리를 복사해서 한 곳씩 망가뜨려 검사기가 다 잡는지 본다(9종). 망가뜨릴 때 **다른 불변식은 살려 두는 것**이 포인트다. 키 개수를 하나 틀리게 하면 사슬이 멀쩡해도 COUNT에서 잡히는데, 그러면 사슬 검사가 동작하는지 알 수 없기 때문이다. 마지막으로 구현에 일부러 버그를 넣어(mutation) 하네스가 잡는지 확인했다(본문 표, 12종 전부).
</details>

<details><summary>Q7. 왜 SSD의 L2P 맵은 B+ 트리가 아니라 평면 배열이죠? 뭐가 깨지나요?</summary>

L2P는 "4 KB LBA 하나마다 PBA 하나"다. 전부 있으면 4 B × LBA 수이고, 1 TB면 약 1 GB다. 평면 배열은 O(1)이고 키를 저장하지 않으니 항목당 4바이트다. B+ 트리는 항목당 약 11바이트(표 1)에 O(log n) 페이지 접근이다. 그래서 FTL은 평면 맵을 쓰고, DRAM이 부족하면 캐시(DFTL류)로 일부만 올린다. B+ 트리는 **범위로 묶을 수 있을 때**, 즉 키가 드문드문하거나 익스텐트로 합칠 수 있을 때 이긴다. 또 NAND는 제자리 쓰기가 안 되니 B+ 트리의 내부 노드를 갱신하면 루트까지 재기록이 전파된다(wandering tree). F2FS는 NAT로 그 전파를 끊는다. 페이지 맵 FTL을 설계할 때의 선택지 비교로 답할 수 있으면 좋다.
</details>

<details><summary>Q8. 레드블랙 트리나 AVL과 비교하면? 임베디드에서 언제 뭘 쓰나요?</summary>

이진 트리 노드는 키 하나 + 링크들이고 B+는 페이지 하나에 수백 키가 있다. 메모리 안에서는 O(log n)이 같아도 B+는 포인터 체이싱이 적다(실측: n=1e6에서 조회 B+ 66 ns 대 AVL 100 ns, 긴 스캔 689 ns 대 5672 ns). 하지만 B+는 큰 노드 안에서 시프트 비용과 페이지 단위 메모리를 먹는다. 키가 수백 개이고 RAM이 빠듯한 MCU라면 침입형 RB 트리(DS-A05)가 페이지 할당 없이 작고, 노드당 오버헤드가 일정하며 페이지 낭비가 없다. 블록 장치에 저장되거나 범위 스캔이 잦은 인덱스라면 B+다. 참고로 RB 트리는 4-노드까지 있는 2-3-4 트리(B 트리의 일종)를 이진 트리로 표현한 것이다(Guibas–Sedgewick 1978). 그래서 "회전"과 "분할"이 대응한다.
</details>

<details><summary>Q9. 이 트리에 전원 차단 복구(또는 동시성)를 붙인다면?</summary>

전원 차단: 분할이나 병합은 페이지 여러 개를 바꾸니 중간에 끊기면 부모와 자식이 어긋난다. 해법은 ① 쓰기 전 로그(WAL) — 변경을 로그에 먼저 적고 체크포인트; ② CoW — 고치는 경로 전체를 새 페이지에 쓰고 루트 포인터 하나만 원자적으로 바꾼다(루트 갱신이 커밋). 페이지 크기 단위 쓰기가 찢어질 수 있으니 페이지 CRC도 필요하다. 다루는 트랙은 STOR-M01과 MEM의 전원 차단 레슨이다. 동시성: 트리 전체 락은 간단하지만 병렬성이 없다. 내려가면서 잠그는 latch crabbing이 표준이고, 분할이 없을 것을 낙관적으로 가정하고 리프만 잠그는 방식, 그리고 PostgreSQL 식의 오른쪽 링크로 분할을 견디는 방식이 있다. 구현은 STOR-M10이다. 이 레슨의 트리는 단일 태스크 전용이다. 그래서 `pg_r`이 stamp를 쓴다.
</details>

## 9. 추가 연습

### 연습 1 — 역방향 범위 스캔

`bt_rscan(t, hi, lo, ks, vs, max)`: `[lo, hi]`를 내림차순으로. `bt_scan`의 결과를 뒤집은 것과 같아야 하고, 첫 키가 지워진 리프가 많은 트리에서도 맞아야 한다.

<details><summary>답</summary>

`bt_descend(hi)`의 리프에서 `ub_u32`로 hi 이하의 마지막 위치를 찾고, 인덱스를 내려가며 복사한다. 리프를 다 읽으면 `prev`로 넘어가 맨 뒤부터 다시 읽는다. 내려간 리프에 hi 이하가 없으면(`i == 0`) 그대로 prev로 넘어간다. 위의 floor 함정과 같은 경우이므로 이 코드는 반복문 하나로 자연스럽게 처리된다. 테스트는 삭제를 많이 한 트리에서 무작위 구간 2000개를 `bt_scan` 결과와 대조했다.
</details>

### 연습 2 — 병합 임계값과 진동

leaf_min을 8에서 4로 낮추면 무엇이 달라지나? cap 16에서 리프 [1..8] [10..17]을 만든 뒤 "8 삭제, 1 삭제, 1 재삽입, 8 재삽입, 9를 넣었다 지우기" 6연산을 1000번 돌려 병합·분할·페이지 쓰기를 비교한다.

<details><summary>답</summary>

`t.leaf_min = 4`로 낮추기만 하면 된다(기존 삽입·삭제 코드가 `leaf_min`을 쓰고, 검사기도 이 값을 본다). 실제 출력:

```text
  leaf_min 8: merges 1000, splits 1000, page writes 10000 | leaf_min 4: merges 0, splits 0, page writes 6000 (1000 cycles)
```

leaf_min 8에서는 두 리프가 최소 경계에 있어서 8을 지우는 순간 병합이 일어나고(15키 + 루트 축소), 9를 다시 넣을 때 분할해 사이클마다 병합 1번, 분할 1번이다. leaf_min 4에서는 병합도 분할도 없고 쓰기는 삭제·삽입의 리프 페이지뿐이다(사이클 6개 연산에 6 쓰기). 대가는 페이지가 더 빈 채로 남을 수 있다는 것이다. 이 맞바꿈이 InnoDB의 `MERGE_THRESHOLD` 설정의 의미이고, PostgreSQL이 부분 병합을 아예 안 하는 것도 같은 고려다.
</details>

### 연습 3 — 맨 끝 삽입 fast path

키가 오름차순으로만 들어온다면 절반만 차는 문제를 어떻게 해결하나? `bt_append`를 만들어 일반 `bt_insert`와 채움률을 비교한다.

<details><summary>답</summary>

오른쪽 끝 리프가 꽉 찼을 때 **절반을 옮기지 말고** 새 키 하나만 있는 새 리프를 만든다. 왼쪽 리프는 꽉 찬 채로 남는다. 부모에 넣는 구분키는 새 키다. 단, 새 오른쪽 끝 리프는 키가 1개이므로 최소 채움을 어긴다. 그래서 이런 트리는 `leaf_min = 1`이라는 약속을 하고(오른쪽 끝 예외), 삭제를 함께 쓰면 그 예외 처리가 필요하다. 실제 출력:

```text
  normal insert: fill 50.2%, 781 leaves, height 3 | append: fill 99.8%, 393 leaves, height 2
```

리프가 절반으로 줄고 높이도 한 층 낮아졌다. 로그 인덱스나 시계열처럼 키가 단조 증가하는 곳에서 큰 이득이다.
</details>

### 연습 4 — 높이의 하한·상한 공식

`height_max(n)` = n개 키를 가진 트리가 가질 수 있는 최대 높이, `height_min(n)` = 최소 높이. 무작위 삽입·삭제 동안 언제나 그 사이임을 확인한다.

<details><summary>답</summary>

높이 h ≥ 2인 트리의 최소 키 수는 루트가 자식 2개, 내부가 최소 `node_min+1` 자식, 리프가 최소 `leaf_min` 키이므로 `2·leaf_min·(node_min+1)^(h-2)`이다. 이 값이 n을 넘지 않는 최대의 h가 최악 높이다. 최소 높이는 꽉 찼을 때 `leaf_cap·(node_cap+1)^(h-1) ≥ n`인 최소 h다. cap 3~8에서 20000회 무작위 연산 동안 항상 범위 안이었다. cap 510의 실제 값:

```text
  cap 510 keys n=1e6: height 3..3, n=1e9: height 4..4, n=1e12: height 5..5
```

페이지 3개면 백만, 4개면 10억이다. 이 크기들에서는 상한과 하한이 같아서 높이가 키 수로 사실상 정해진다. 이것이 B+ 트리의 강한 보장이다(AVL의 최악 높이는 약 1.44·log₂n이다, DS-M07).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 10/10 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o p_asan && ./p_asan
```

## 10. 핵심 정리

- 노드를 페이지로 키우면 높이가 준다. 4 KB + u32 키·링크면 팬아웃 510, 백만 키도 높이 3이다. 검색은 페이지 3개(AVL은 노드 19개).
- `child[i]`의 범위는 `[key[i-1], key[i])`이다. 내려갈 때는 upper bound, 리프 안에서는 lower bound.
- 리프 분할은 copy up, 내부 분할은 push up. 삭제의 구분키는 경계일 뿐이라 첫 키가 지워져도 그대로 둔다. 그래서 floor는 왼쪽 리프를 봐야 한다.
- 삭제 순서: 왼쪽에서 빌리기 → 오른쪽에서 빌리기 → 병합 → 부모로 전파 → 루트 축소. 리프 병합은 구분키를 버리고 내부 병합은 끌어내린다.
- 분할 전에 필요한 페이지 수를 세면 실패가 원자적이다. 경로는 스택에 적는다(부모 링크를 두면 분할마다 자식 수백 개를 고쳐야 한다).
- 링크는 페이지 번호다. 위치 독립, 작은 크기, 범위 검사가 공짜로 따라온다.
- 무작위 삽입은 평균 70% 안팎(측정 61.5~77.1%), 순차 삽입은 50%다. fast-path 맨 끝 삽입이 이를 해결한다.
- 검사기와 고장 주입을 쌍으로 두어야 불변식 테스트를 믿을 수 있다.
- NAND에서는 B+ 트리의 제자리 갱신이 없다. 간접 참조(FTL, NAT)가 wandering tree를 막는다.

## 참고자료

- R. Bayer, E. McCreight, "Organization and Maintenance of Large Ordered Indexes", Acta Informatica 1(3), 1972 — B 트리 원전
- D. Comer, "The Ubiquitous B-Tree", ACM Computing Surveys 11(2), 1979 — B+ 트리 정리
- G. Graefe, "Modern B-Tree Techniques", Foundations and Trends in Databases 3(4), 2011 — 실무 기법 총정리
- A. C. Yao, "On random 2–3 trees", Acta Informatica 9, 1978 — 고차 B 트리 활용률 ≈ ln 2
- L. Guibas, R. Sedgewick, "A dichromatic framework for balanced trees", FOCS 1978 — RB 트리와 2-3-4 트리
- MySQL Reference Manual, "Configuring the Merge Threshold for Index Pages" — MERGE_THRESHOLD 기본 50, merge-split 진동 경고
- PostgreSQL `src/backend/access/nbtree/README` — L&Y 오른쪽 링크, 완전히 빈 페이지만 삭제; `CREATE INDEX` 문서 — B-tree fillfactor 기본 90, 오른쪽 끝 확장 시 적용
- SQLite `btree.c` `balance_quick()` — 맨 오른쪽 삽입 특수 경우 (소스 주석)
- Linux 커널 문서 `Documentation/filesystems/f2fs.rst` — wandering tree와 NAT; `Documentation/filesystems/ext4/ifork.rst` — extent tree 형식
- 관련 레슨: DS-M07 (AVL, 익스텐트 맵), DS-A05 (RB 트리), STOR-M01/M02 (버퍼 풀, 슬롯 페이지, 벌크 로드, prefix 압축), STOR-M10 (래치), MEM-M10 계열 (FTL), OS 트랙 (페이지 캐시)
