---
id: DS-M05
time: 75분
updated: 2026-11-07
---
# DS-M05 LRU 캐시: 해시 + 이중 연결 리스트

## 학습 목표

- 해시 테이블(조회)과 이중 연결 리스트(최근성 순서)에 **같은 엔트리를 동시에 걸어** get/put/remove 를 모두 O(1)(기대값)로 만든다 — malloc 없이 정적 풀만으로.
- 축출(eviction) 순서를 정확히 말한다: "victim 내용 복사 → 해시에서 떼기 → 리스트에서 떼기 → 재사용". 순서를 바꾸면 무엇이 깨지는지 설명한다.
- 해시·리스트·free 리스트 세 구조의 **교차 불변식 검사기**를 쓰고, 배열 기반 참조 모델과 lockstep 으로 비교해 검증한다.
- FTL 매핑 캐시(CMT)로 확장해 **dirty 엔트리의 write-back** 을 구현하고, 순차/반복/랜덤/핫스팟 트레이스의 hit rate 와 맵 쓰기 횟수를 실제로 측정해 해석한다.

## 1. 문제

LRU(Least Recently Used) 캐시는 LeetCode 146 으로 면접 단골이고, 펌웨어에서는 SSD 의 매핑 캐시·파일시스템 버퍼·Linux 페이지 캐시의 기본 모양이다 — "해시 + 이중 리스트" 를 손으로 즉시 짤 수 있어야 하고, 그 위에서 dirty·축출 비용까지 얘기할 수 있어야 시니어 답변이 된다.

SSD 의 FTL(Flash Translation Layer)은 LBA(논리 주소) → PBA(물리 주소) 표를 들고 있다. 4 KiB 매핑 단위에 엔트리 4 B 면 표 크기는 용량의 4/4096 ≈ 0.1% — 1 TB 드라이브는 2^40 / 2^12 = 2^28 개 엔트리 × 4 B = **1 GiB**. DRAM 이 없거나 작은 SSD(DRAM-less, NVMe HMB 사용)는 표 전체를 못 올리므로, 표는 NAND 의 맵 페이지(translation page)에 두고 **최근에 쓴 매핑만 SRAM 에 캐시**한다. DFTL 논문은 이 캐시를 CMT(Cached Mapping Table)라고 부른다. 이 레슨에서 만드는 것이 바로 그 CMT 의 최소 버전이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">한 엔트리가 두 구조에 동시에 걸린다: 해시 체인(hnext) + LRU 리스트(prev/next)</text>
  <text x="20" y="66" fill="currentColor">bucket[]</text>
  <g stroke="currentColor" fill="none">
    <rect x="110" y="45" width="70" height="32"/><rect x="190" y="45" width="70" height="32"/>
    <rect x="270" y="45" width="70" height="32"/><rect x="350" y="45" width="70" height="32"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="145" y="66">b0</text><text x="225" y="66">b1</text><text x="305" y="66">b2</text><text x="385" y="66">b3</text>
  </g>
  <text x="305" y="96" fill="currentColor" text-anchor="middle" font-size="12">NULL</text>
  <rect x="20" y="150" width="70" height="74" class="f-mut" stroke="currentColor"/>
  <g stroke="currentColor" fill="none">
    <rect x="130" y="150" width="110" height="74"/><rect x="275" y="150" width="110" height="74"/>
    <rect x="420" y="150" width="110" height="74"/>
  </g>
  <rect x="565" y="150" width="110" height="74" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="55" y="185">S</text><text x="55" y="203" font-size="12">sentinel</text>
    <text x="185" y="142">MRU</text><text x="620" y="142">LRU = 다음 victim</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="185" y="172">lba 5</text><text x="185" y="190">pba 105</text><text x="185" y="208">dirty 0</text>
    <text x="330" y="172">lba 1</text><text x="330" y="190">pba 101</text><text x="330" y="208">dirty 0</text>
    <text x="475" y="172">lba 4</text><text x="475" y="190">pba 104</text><text x="475" y="208">dirty 0</text>
    <text x="620" y="172">lba 3</text><text x="620" y="190">pba 333</text><text x="620" y="208">dirty 1</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M90 165 L126 165 M126 165 l-7 -4 M126 165 l-7 4"/><path d="M130 212 L94 212 M94 212 l7 -4 M94 212 l7 4"/>
    <path d="M240 165 L271 165 M271 165 l-7 -4 M271 165 l-7 4"/><path d="M275 212 L244 212 M244 212 l7 -4 M244 212 l7 4"/>
    <path d="M385 165 L416 165 M416 165 l-7 -4 M416 165 l-7 4"/><path d="M420 212 L389 212 M389 212 l7 -4 M389 212 l7 4"/>
    <path d="M530 165 L561 165 M561 165 l-7 -4 M561 165 l-7 4"/><path d="M565 212 L534 212 M534 212 l7 -4 M534 212 l7 4"/>
  </g>
  <g stroke="currentColor" fill="none" stroke-dasharray="5 3">
    <path d="M145 77 L325 148"/><path d="M225 77 L190 148"/><path d="M385 77 L470 148"/>
    <path d="M185 224 C185 268, 620 268, 620 228"/>
  </g>
  <g fill="currentColor"><circle cx="325" cy="148" r="3"/><circle cx="190" cy="148" r="3"/><circle cx="470" cy="148" r="3"/><circle cx="620" cy="228" r="3"/></g>
  <text x="330" y="282" fill="currentColor" text-anchor="middle" font-size="12">hnext: b1 체인 = 5 → 3 (점선 = 해시 포인터)</text>
  <text x="20" y="302" fill="currentColor">실선 = LRU 고리 (S.next = MRU, S.prev = LRU). 조회는 해시로 O(1),</text>
  <text x="20" y="318" fill="currentColor">순서 갱신·축출은 리스트로 O(1). 둘 중 하나만으로는 둘 다 O(1) 이 안 된다.</text>
</svg>
```

```c
typedef struct LruEntry {
    ListNode node;            /* LRU 또는 free 리스트 (침입형, DS-M01) */
    struct LruEntry *hnext;   /* 버킷 체인 (DS-B10) */
    uint32_t lba, pba;
    bool dirty;               /* 플래시 맵보다 새롭다 → 쫓겨날 때 write-back */
} LruEntry;
```

엔트리 풀·버킷 배열은 호출자가 정적으로 준다(`lru_init(c, pool, cap, bucket, nbuckets)`, 주어진 코드). 리스트 도우미(`list_add_head`, `list_del`, `list_move_head`)와 해시(`lba_hash`: Fibonacci 해싱, DS-B09)도 주어진다.

### Part A — 조회와 삽입 (get / put / 축출)

- `lru_find(c, lba)` — 버킷 체인만 걷는다. 순서는 안 바꾼다.
- `lru_get(c, lba, &pba)` — hit 이면 엔트리를 **MRU 로 옮기고** `hits++`. miss 면 `misses++`, 순서 불변.
- `lru_put(c, lba, pba, dirty, &victim)` — ① 이미 있으면 값 갱신(`dirty` 는 OR) + MRU ② 없으면 free 엔트리 사용 ③ free 가 없으면 **LRU 꼬리를 쫓아내** `victim` 에 내용을 돌려주고 그 엔트리를 재사용.

### Part B — 삭제, 충돌 체인, 불변식

- `hash_unlink(c, e)` — 체인 머리·가운데·끝을 **같은 코드**로 떼기 (포인터의 포인터).
- `lru_remove(c, lba, &out)` — 해시·LRU 에서 떼어 free 리스트로. 없으면 false.
- `lru_check(c)` — LRU·free 리스트 링크 일관성, 개수(size, cap − size), 모든 LRU 엔트리가 자기 버킷에서 정확히 한 번 찾히는지, 체인 전체 개수와 버킷 번호. **망가진 상태에서도 반드시 멈춰야** 한다.
- 하네스: cap 1, 버킷 2개(긴 체인) 등 경계 + `random_test(30000)` (cap 8, 키 24개, get/put/remove 혼합, 매 연산 뒤 `lru_check` + 참조 모델과 반환값·victim·순서 비교).

### Part C — FTL 매핑 캐시: write-back 과 트레이스 측정

- `ftl_read(f, lba)` — hit 이면 캐시의 PBA. miss 면 플래시 맵(시뮬레이션 배열)을 읽고(`map_loads++`) clean 으로 put.
- `ftl_write(f, lba, new_pba)` — 옛 PBA 를 돌려주고(GC 의 valid 카운트용, miss 면 맵 로드) 새 매핑을 **dirty** 로 put.
- `ftl_writeback(f, &victim)` — 쫓겨난 엔트리가 dirty 일 때만 플래시 맵에 반영(`map_writebacks++`). clean 은 그냥 버린다.
- `ftl_flush(f)` — 셧다운/NVMe Flush: dirty 를 전부 반영하고 clean 으로. **순서는 건드리지 않는다**.
- 하네스: 4가지 트레이스 × cap {64, 256, 1024} 를 50,000 연산(쓰기 30%)씩 돌리며 **참조 FTL 과 lockstep**, 모든 읽기를 진짜 매핑 `truth[]` 와 비교, 끝나면 flush 후 플래시 맵 == `truth[]`.

## 2. Problem (English)

> **LRU mapping cache for a flash translation layer.**
>
> Implement a fixed-capacity LRU cache mapping `uint32_t lba → uint32_t pba` in C11 with no dynamic allocation. Entries come from a caller-provided pool; each entry is linked into an intrusive circular doubly linked list (recency order, sentinel-based) and into a separately chained hash table.
>
> 1. `bool lru_get(LruCache *c, uint32_t lba, uint32_t *pba)` — O(1) expected; a hit moves the entry to the MRU position.
> 2. `void lru_put(LruCache *c, uint32_t lba, uint32_t pba, bool dirty, LruVictim *victim)` — insert or update (dirty is sticky); when full, evict the LRU entry and report it through `victim`.
> 3. `bool lru_remove(...)` and `bool lru_check(const LruCache *c)`, an invariant checker that cross-checks the list, the hash chains and the free list and always terminates.
> 4. A write-back FTL layer: reads/writes go through the cache, misses load from the on-flash table, dirty victims are written back, `ftl_flush` persists all dirty entries.
>
> Example (capacity 4): put 1,2,3,4 → get(1) hits → put(5) evicts **2**, not 1.
>
> Measure hit rate and map write-backs for sequential, looping, uniform random and hotspot traces, and verify against a simple array-based reference model.
>
> Follow-ups: what is the worst case? Why is LRU bad for scans? How would you make the hit path lock-free or cheaper? What happens to dirty entries on sudden power loss?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 이 Mac: 27.2 SDK 링크가 깨져 있음
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/DS-M05_st && /tmp/DS-M05_st
# 처음엔 14/55 passed, exit 1 — random_test 는 step 0, 트레이스는 op 0 에서 멈춘다
```

## 4. 힌트

<details>
<summary>힌트 1 — 리스트 방향을 하나로 정한다</summary>

`lru.next` = MRU(방금 쓴 것), `lru.prev` = LRU(가장 오래된 것). hit 은 `list_move_head`, 축출은 `c->lru.prev`. 이 약속 하나만 지키면 get/put/evict 가 각각 한두 줄이 된다. 리스트 노드에서 엔트리로는 `container_of(n, LruEntry, node)`.

</details>

<details>
<summary>힌트 2 — 체인 삭제는 포인터의 포인터로</summary>

`LruEntry **pp = &c->bucket[h];` 에서 시작해 `*pp != e` 인 동안 `pp = &(*pp)->hnext;`. 찾으면 `*pp = e->hnext;`. 머리면 `pp` 가 버킷 칸을, 가운데면 앞 엔트리의 `hnext` 칸을 가리킬 뿐 — `if (prev == NULL)` 분기가 필요 없다.

</details>

<details>
<summary>힌트 3 — 축출 순서: 읽고 → 해시 → 리스트 → 덮어쓰기</summary>

`hash_unlink` 는 `e->lba` 로 버킷을 계산한다. 그러니 **새 lba 를 쓰기 전에** 떼어야 한다. victim 내용(`lba, pba, dirty`)도 덮어쓰기 전에 복사해 둔다 — dirty victim 을 잃으면 그게 곧 데이터 손실이다.

</details>

<details>
<summary>힌트 4 — 이미 있는 키의 dirty 는 OR</summary>

dirty 엔트리에 clean put 이 와도 dirty 를 지우면 안 된다. `e->dirty = e->dirty || dirty;`. 지우는 것은 실제로 플래시에 반영했을 때(write-back, flush)뿐이다.

</details>

<details>
<summary>힌트 5 — 검사기가 멈추는 법과 "교차" 검사</summary>

리스트는 `++n > cap` 이면 즉시 false, 체인은 걸음 수가 cap(또는 size)을 넘으면 false. 그리고 "리스트에 있는 모든 엔트리가 해시에서 찾힌다" + "해시 체인 엔트리 총수 == size" 를 둘 다 봐야 리스트에만 있는 엔트리, 해시에만 남은 유령 엔트리를 모두 잡는다.

</details>

## 5. 풀이

### Part A — get 은 "떼서 앞에", put 은 "꼬리를 떼서 재사용"

불변식(invariant) — 모든 연산 전후에 참:

1. 쓰이는 엔트리는 **정확히 하나의** LRU 위치와 **정확히 하나의** 버킷 체인 위치를 가진다. 안 쓰이는 엔트리는 free 리스트에만 있다.
2. LRU 리스트 길이 = `size`, free 리스트 길이 = `cap − size`, 같은 lba 는 최대 1개.
3. LRU 리스트 순서 = 마지막 접근 시각의 내림차순 (next 방향으로 갈수록 오래됨).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">get(4) hit 과 put(9) 축출 — 리스트에서 바뀌는 것</text>
  <g fill="currentColor"><text x="20" y="69">전</text><text x="20" y="119">후</text><text x="20" y="199">전</text><text x="20" y="249">후</text></g>
  <text x="70" y="44" fill="currentColor">① get(4): hit</text>
  <text x="70" y="174" fill="currentColor">② put(9): miss, 꽉 참</text>
  <g stroke="currentColor" fill="none">
    <rect x="126" y="50" width="44" height="28"/><rect x="182" y="50" width="44" height="28"/><rect x="294" y="50" width="44" height="28"/>
    <rect x="182" y="100" width="44" height="28"/><rect x="238" y="100" width="44" height="28"/><rect x="294" y="100" width="44" height="28"/>
    <rect x="126" y="180" width="44" height="28"/><rect x="182" y="180" width="44" height="28"/><rect x="238" y="180" width="44" height="28"/>
    <rect x="182" y="230" width="44" height="28"/><rect x="238" y="230" width="44" height="28"/><rect x="294" y="230" width="44" height="28"/>
  </g>
  <g class="f-mut" stroke="currentColor">
    <rect x="70" y="50" width="44" height="28"/><rect x="70" y="100" width="44" height="28"/>
    <rect x="70" y="180" width="44" height="28"/><rect x="70" y="230" width="44" height="28"/>
  </g>
  <rect x="238" y="50" width="44" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="126" y="100" width="44" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="294" y="180" width="44" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="126" y="230" width="44" height="28" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="92" y="69">S</text><text x="148" y="69">5</text><text x="204" y="69">1</text><text x="260" y="69">4</text><text x="316" y="69">3</text>
    <text x="92" y="119">S</text><text x="148" y="119">4</text><text x="204" y="119">5</text><text x="260" y="119">1</text><text x="316" y="119">3</text>
    <text x="92" y="199">S</text><text x="148" y="199">4</text><text x="204" y="199">5</text><text x="260" y="199">1</text><text x="316" y="199">3*</text>
    <text x="92" y="249">S</text><text x="148" y="249">9</text><text x="204" y="249">4</text><text x="260" y="249">5</text><text x="316" y="249">1</text>
  </g>
  <g fill="currentColor">
    <text x="370" y="62">list_del(4): 1 ⇄ 3 을 잇는다 (포인터 2개)</text>
    <text x="370" y="84">list_add_head: S ⇄ 4 ⇄ 5 (포인터 4개)</text>
    <text x="370" y="120">해시는 그대로 — lba 가 안 바뀌었으니까</text>
    <text x="370" y="192">victim = lru.prev = 3 (* = dirty) → write-back</text>
    <text x="370" y="214">hash_unlink(3) → list_del(3)</text>
    <text x="370" y="242">같은 엔트리에 lba 9 를 채워</text>
    <text x="370" y="262">버킷 머리 + LRU 머리에 연결</text>
    <text x="20" y="292">둘 다 n 과 무관한 상수 개의 포인터만 고친다. victim 엔트리는 그 자리에서 재사용.</text>
  </g>
</svg>
```

하네스 Part A 시퀀스 (cap 4, 값 = 100 + lba) 를 그대로 따라가면:

| 연산 | 결과 | 순서 MRU → LRU |
|---|---|---|
| put 1, 2, 3, 4 | free 엔트리 4개 사용, victim 없음 | 4 3 2 1 |
| get 1 | hit, 101 | 1 4 3 2 |
| put 5 | 꽉 참 → victim **2** (clean) | 5 1 4 3 |
| put 3 (333, dirty) | 이미 있음 → 갱신 + MRU, victim 없음 | 3 5 1 4 |
| get 2 | miss — 순서 불변 | 3 5 1 4 |
| put 6, 7, 8 | victim 4, 1, 5 | 8 7 6 3 |
| put 9 | victim **3** (pba 333, dirty) | 9 8 7 6 |

최종 `hits 1, misses 2, evictions 5`. "get(1) 했으니 1 은 살아남고 2 가 나간다" 가 면접관이 제일 먼저 확인하는 줄이다.

**복잡도**: 해시 버킷 수 ≥ cap 이면 부하율 α = size/nbuckets ≤ 1 → 체인 평균 길이 O(1), get/put/remove 모두 기대 O(1). 리스트 조작은 언제나 O(1) (포인터 2~4개). 최악은 모든 키가 한 버킷에 몰릴 때 O(n) — Q2 참고.

### Part B — 체인 삭제, 경계, 교차 불변식

**포인터의 포인터** 삭제는 체인 위치와 무관하게 같은 코드다:

| 지울 위치 | 루프가 끝났을 때 `pp` 가 가리키는 것 | `*pp = e->hnext` 의 효과 |
|---|---|---|
| 체인 머리 | `&bucket[h]` | 버킷이 다음 엔트리(또는 NULL)를 가리킴 |
| 가운데 | `&prev->hnext` | prev 가 e 를 건너뜀 |
| 끝 | `&prev->hnext` | prev->hnext = NULL |
| 혼자 | `&bucket[h]` | 버킷이 NULL — 빈 버킷 |

하네스는 `lba_hash` 로 버킷 0 에 떨어지는 키 3개를 직접 골라 체인 `k2 → k1 → k0` 을 만들고 **가운데 k1** 부터 지운다. 그다음 머리(k2), 마지막(k0) 순서. 빈 버킷이 `NULL` 로 돌아오는지(`bk[0] == NULL`)까지 확인한다.

**cap 1**: free 1개를 쓰고 나면 `lru.prev == lru.next` 인 엔트리 하나가 MRU 이자 LRU 다. 다음 put 은 그 엔트리를 쫓아내고 같은 메모리에 새 키를 넣는다 — 축출 코드가 "victim == 방금 들어온 것" 도 처리하는지 보는 경계다. cap 0 은 `lru_init` 이 금지한다(꼬리가 sentinel 이라 `container_of` 가 쓰레기를 만든다).

**검사기 `lru_check`** 는 세 구조를 서로 대조한다:

| 검사 | 잡는 버그 |
|---|---|
| LRU·free 리스트: `nx->prev == p`, NULL 없음, 걸음 ≤ cap, 엔트리가 풀 안 | 링크 순서 실수, 해제된 노드 재사용 |
| 개수: LRU == size, free == cap − size | size 갱신 누락, 엔트리 누수 |
| LRU 의 각 엔트리: 자기 버킷에서 포인터로 정확히 1번, 같은 lba 1개 | 해시 삽입 누락, 중복 키 |
| 체인 총 엔트리 수 == size, 각 엔트리 해시 == 버킷 번호 | 축출 때 `hash_unlink` 누락(유령), lba 덮어쓴 뒤 unlink |

하네스가 실제로 망가뜨려 보는 4가지 — `size++`, 엔트리 2 를 버킷에서만 빼기, 엔트리 2 의 lba 를 3 으로(중복 키 / 엉뚱한 버킷), `next->prev = NULL` — 를 모두 false 로 잡고, 원상 복구하면 다시 true. "포인터 범위 검사" 는 `uintptr_t` 로 비교한다: 서로 다른 객체를 가리키는 포인터끼리 `<` 비교는 C 표준상 미정의 동작이라, 망가진 포인터를 검사하는 함수 자체가 UB 를 내면 안 되기 때문이다.

**랜덤 테스트**: cap 8 에 버킷 2개(체인 평균 4) + 키 24개라 축출·충돌·삭제가 끊임없이 섞인다. 참조 모델 `RefLru` 는 MRU 순서 배열 + 선형 탐색 + `memmove` — O(cap) 이지만 틀리기 어렵다. 매 연산 뒤 ① 반환값(hit 여부, pba, remove 성공) ② victim 전체(lba/pba/dirty) ③ `lru_check` ④ 리스트 순서와 모델 배열의 원소별 비교 ⑤ hits/misses 카운터. 시드 고정(xorshift32 `0xC0FFEE`)이라 실패하면 같은 step 이 재현된다.

### Part C — write-back 매핑 캐시와 실제 측정

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m05a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">FTL 매핑 캐시: 비용은 miss(맵 읽기)와 dirty 축출(맵 쓰기)에서만 생긴다</text>
  <rect x="20" y="90" width="120" height="60" stroke="currentColor" fill="none"/>
  <rect x="250" y="70" width="220" height="100" class="f-acc" stroke="currentColor"/>
  <rect x="600" y="70" width="130" height="100" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="80" y="116">호스트</text><text x="80" y="134" font-size="12">read / write</text>
    <text x="360" y="96">CMT (SRAM)</text><text x="360" y="116" font-size="12">LRU: 해시 + 이중 리스트</text>
    <text x="360" y="136" font-size="12">clean / dirty 엔트리</text><text x="360" y="156" font-size="12">hit = 비용 0</text>
    <text x="665" y="100">NAND</text><text x="665" y="120" font-size="12">맵 페이지</text><text x="665" y="140" font-size="12">(전체 L2P)</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m05a)">
    <path d="M140 120 L246 120"/>
    <path d="M598 90 L474 90"/>
    <path d="M474 150 L598 150"/>
    <path d="M300 172 L300 210"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="176" y="112">lba</text>
    <text x="478" y="58">miss: 맵 로드 (NAND read)</text>
    <text x="478" y="192">dirty 축출 / flush:</text><text x="478" y="208">write-back (NAND program)</text>
    <text x="200" y="228">clean 축출: 그냥 버림 (플래시와 같음)</text>
  </g>
  <text x="20" y="260" fill="currentColor">쓰기 = dirty put. 같은 LBA 를 캐시 안에서 여러 번 고치면</text>
  <text x="20" y="280" fill="currentColor">플래시 맵에는 마지막 값이 한 번만 간다 (쓰기 합치기, write coalescing).</text>
</svg>
```

**쓰기 경로**의 설계 선택: 쓰기 miss 에서도 맵을 읽는다. 옛 PBA 를 알아야 그 물리 페이지를 invalid 로 표시해 GC 의 블록별 valid 카운트를 깎을 수 있기 때문이다 (GC 대상 선정은 DS-A09). `ftl_write` 가 옛 PBA 를 반환하고, 하네스는 그것이 `truth[lba]` 와 같은지 매번 확인한다.

**검증 구조 — 3자 대조**: 같은 연산을 ① 내 캐시 FTL ② `RefLru` 로 만든 참조 FTL 에 넣고 ③ 진짜 매핑 `truth[]` (모든 쓰기를 바로 반영하는 전체 표)를 따로 든다. 매 연산: 읽기 결과 == `truth`, 쓰기의 옛 PBA == `truth`, hits/misses/map_loads/map_writebacks 가 참조 FTL 과 같음. 1024 연산마다 `lru_check` + 순서 전체 비교. 끝나면 `ftl_flush` 후 `memcmp(flash_map, truth)` — **write-back 캐시가 데이터를 하나도 잃지 않았다**는 최종 증명이다.

트레이스 (LBA 공간 4096, 50,000 연산, 쓰기 30%):

| 트레이스 | 접근 패턴 |
|---|---|
| seq-4096 | `i % 4096` — 드라이브 전체 순차 스캔 반복 |
| loop-240 | `i % 240` — 작은 영역 반복 (캐시 256 보다 약간 작음) |
| random | 균등 랜덤 |
| hotspot | 90% 는 연속 5% 영역(LBA 1024 부터 205개), 10% 는 전체 랜덤 |

`solution.c` 실제 실행 결과 (`loads` = 맵 로드 = miss 수, `writes` = 호스트 쓰기, `writeback` = 축출로 인한 맵 쓰기, `flush` = 마지막 flush 의 맵 쓰기):

```text
LBA 공간 4096, 연산 50000 (쓰기 30%), sizeof(LruEntry) = 40 B
trace       cap     hit%    loads   writes  writeback    flush
seq-4096     64    0.00%    50000    15064      15049       15
seq-4096    256    0.00%    50000    15064      14993       71
seq-4096   1024    0.00%    50000    15064      14762      302
loop-240     64    0.00%    50000    14886      14863       23
loop-240    256   99.52%      240    14886          0      240
loop-240   1024   99.52%      240    14886          0      240
random       64    1.55%    49224    14943      14837       22
random      256    6.28%    46860    14943      14574       89
random     1024   24.54%    37732    14943      13265      389
hotspot      64   25.11%    37446    15007      13525       22
hotspot     256   85.38%     7311    15007       3581      193
hotspot    1024   91.83%     4083    15007       1062      492
55/55 passed
```

해석:

| 관찰 | 이유 |
|---|---|
| seq-4096 은 cap 1024 에서도 **0%** | 같은 LBA 가 다시 오기까지 4095 개의 다른 LBA 가 지나간다. 재사용 거리 > 용량이면 LRU 는 항상 직전에 그 키를 쫓아낸 상태 — LRU 의 최악 패턴 |
| loop-240: cap 64 → 0%, cap 256 → 99.52% | **LRU 절벽(cliff)**. 작업 집합이 용량보다 1개만 커도 0%. 99.52% = (50000 − 240) / 50000, 즉 처음 240번의 강제 miss(cold miss)만 남는다 |
| random ≈ cap / 4096 | 균등 랜덤에서 어떤 정책도 cap/N 을 못 넘는다: 1.56%, 6.25%, 25% 예측 vs 1.55%, 6.28%, 24.54% 측정. cap 1024 가 조금 낮은 것은 처음 1024 개를 채우는 동안의 cold miss |
| hotspot cap 256 → 85.38% | 핫셋 205개가 들어가고 남는 51칸이 차가운 접근을 흡수. 상한은 대략 90%(핫 접근) + 10% × 차가운 hit 확률 |
| writeback + flush 가 writes 보다 훨씬 적음 | 쓰기 합치기(write coalescing). loop-240/256: 14,886 쓰기 → 맵 쓰기 240번 (62배 감소). hotspot/256: 15,007 → 3,774 (약 4배). seq 는 합칠 게 없어 writeback + flush == writes |

펌웨어 관점의 결론: ① 순차 워크로드는 엔트리 단위 LRU 로는 절대 못 잡는다 — 실제 FTL 은 맵 **페이지** 단위로 캐시하거나 순차 감지(prefetch)를 따로 둔다 (연습 1). ② 맵 쓰기(NAND program)는 dirty 축출에서만 생기므로, 축출을 clean 우선으로 하거나(연습 3) 같은 맵 페이지의 dirty 를 묶으면(연습 4) 쓰기 증폭(WAF)이 준다. ③ 측정 프로그램 전체(정확성 대조 포함)는 이 Mac(Apple Silicon, `-O2`)에서 약 0.2초.

**엔트리당 메모리**: 이 Mac(64비트 포인터)에서 `sizeof(LruEntry)` = 40 B. Cortex-M(32비트)에서는 node 8 + hnext 4 + lba 4 + pba 4 + dirty 1 → 패딩 포함 **24 B** (`clang -target thumbv7m-none-eabi` 에서 `_Static_assert` 로 확인), 버킷 포인터 4 B 를 더하면 엔트리당 약 28 B. 16비트 인덱스로 바꾸면 16 B + 버킷 2 B (Q6).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/DS-M05_so && /tmp/DS-M05_so
# (위의 표) ... 55/55 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o /tmp/DS-M05_as && /tmp/DS-M05_as
# 같은 표, 55/55 passed, ASan/UBSan 리포트 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| get hit 에서 순서를 안 옮김 | 삽입 순서대로 나가는 **FIFO** 가 된다. Part A 의 "put 5 → victim 2" 가 victim 1 로 바뀜 | hit 이면 `list_move_head(&c->lru, &e->node)` |
| 축출 때 `list_del` 만 하고 `hash_unlink` 누락 | 재사용된 엔트리가 옛 체인에도 남아 체인 두 개가 같은 엔트리를 공유 → 체인이 서로 꼬이고 고리가 생길 수 있음 | victim 은 해시·리스트 양쪽에서 떼기 (검사기 ④ 가 잡음) |
| 새 lba 를 먼저 쓰고 `hash_unlink` | unlink 가 **새 lba 의 버킷**을 뒤져 e 를 못 찾고 `*pp == NULL` 역참조 | 복사 → unlink → list_del → 덮어쓰기 순서 |
| 이미 있는 키를 확인 안 하고 put | 같은 lba 가 두 엔트리에 — 어느 쪽이 먼저 찾히느냐에 따라 옛 PBA 를 읽는 **데이터 오류** | put 첫 줄에 `lru_find` |
| `e->dirty = dirty;` (OR 아님) | dirty 엔트리를 clean put 이 덮으면 축출 때 write-back 이 빠짐 → 전원이 멀쩡해도 매핑이 사라짐 | `e->dirty = e->dirty \|\| dirty;` |
| flush 에서 `lru_get` 으로 순회 | 순회가 순서를 뒤섞고 hits 를 부풀린다. flush 는 접근이 아니다 | 리스트를 직접 걸으며 `dirty` 만 처리 |
| TRIM 을 `lru_remove` 로 처리 | 플래시 맵에 남은 옛 PBA 가 다음 miss 때 되살아남 (연습 5 에서 재현) | `lru_put(c, lba, UNMAPPED, true, &v)` |
| 버킷 1개로 init (`shift = 32`) | `uint32_t >> 32` 는 C11 6.5.7 상 미정의 동작 (자료형 폭 이상 시프트) | 버킷 ≥ 2, 2의 거듭제곱 |

## 8. 면접 Q&A

<details>
<summary>Q1. 왜 해시와 이중 연결 리스트 둘 다 필요한가요? 단일 연결 리스트면 안 되나요?</summary>

해시만 있으면 "가장 오래된 것" 을 찾는 데 O(n), 리스트만 있으면 키 조회가 O(n). 둘을 같은 엔트리에 걸어야 조회(해시)와 순서 갱신·축출(리스트)이 모두 O(1). 이중이어야 하는 이유는 **노드 포인터만으로 O(1) 삭제**하려면 앞 노드를 알아야 하기 때문(DS-B06 Q2). 단일 리스트로 하려면 해시 값에 "앞 노드 포인터" 를 저장하는 트릭이 있지만, 앞 노드가 바뀔 때마다 그 노드의 해시 값도 고쳐야 해서 코드가 더 복잡하고 버그가 잘 난다.

</details>

<details>
<summary>Q2. 최악 시간은? 실시간성(결정성)이 필요하면?</summary>

기대 O(1) 이지만 모든 키가 한 버킷에 몰리면 조회 O(n). Fibonacci 해싱은 연속 LBA 를 잘 흩지만 곱셈 상수가 고정이라 **특정 LBA 집합을 골라 충돌시키는 것**은 가능하다 — 호스트가 주소를 정하는 SSD 에서는 이론상 악의적 패턴이 있다. 결정성이 필요하면: ① **집합 연관(set-associative) 캐시** — LBA 의 일부 비트로 set 을 고르고 set 안에 way 4~8개만 둔다, 최악도 way 수만큼만 비교 (CPU 캐시와 같은 구조) ② 체인 길이 상한 + 넘치면 캐시하지 않고 바로 맵에서 읽기 ③ 최악 O(1) 조회가 보장되는 Cuckoo 해싱 (DS-A07). 리스트 쪽은 원래 최악 O(1) 이다.

</details>

<details>
<summary>Q3. dirty 엔트리가 RAM 에만 있을 때 갑자기 전원이 꺼지면? write-through 로 하면 안 되나요?</summary>

dirty 매핑이 사라지면 그 LBA 는 옛 PBA(옛 데이터)를 가리키게 된다. 대책: ① PLP(Power Loss Protection) 커패시터로 전원 차단 순간 dirty 를 flush 할 시간 확보 (엔터프라이즈 SSD) ② 데이터 페이지의 spare(OOB) 영역에 LBA 를 함께 기록해, 부팅 때 최근 블록을 스캔해 매핑을 복구 ③ 맵 갱신 저널 + 주기적 체크포인트. write-through(쓸 때마다 맵 페이지 program)는 정확하지만 측정에서 보듯 쓰기 합치기가 사라져 loop-240 같은 패턴에서 맵 쓰기가 62배 늘어난다 — 쓰기 증폭과 NAND 수명 비용이 크다. 그래서 대부분 write-back + 복구 메커니즘을 택한다.

</details>

<details>
<summary>Q4. LRU 의 약점은? 실제 시스템은 무엇을 쓰나요?</summary>

측정 그대로: 재사용 거리 > 용량인 순차 스캔에서 0%, 작업 집합이 용량보다 조금만 커도 절벽. 그리고 한 번만 읽히는 차가운 데이터가 들어올 때마다 뜨거운 엔트리를 밀어낸다(scan pollution). 대안: **2Q / LRU-K** (두 번 이상 쓰인 것만 보호 구역으로), **ARC** (최근성·빈도 균형을 적응적으로), **CLOCK** (근사 LRU, 연습 2). Linux 페이지 캐시는 active/inactive 두 리스트로 나눠 한 번 쓰인 페이지는 inactive 에서 시작하게 하고(`mm/vmscan.c`, `mm/swap.c` 의 `folio_mark_accessed`), 6.1 부터는 MGLRU(multi-gen LRU)도 들어왔다. 연습 3 에서 clean-first 축출이 hotspot 의 hit rate 를 오히려 올린 것도 같은 현상 — 1회성 차가운 읽기를 먼저 내보낸 효과다.

</details>

<details>
<summary>Q5. 여러 코어나 ISR 이 캐시를 같이 쓰면?</summary>

LRU 는 **읽기(hit)도 리스트를 고친다** — get 이 쓰기 연산이라 읽기끼리도 락이 필요하다. 이것이 LRU 의 큰 동시성 약점. 대안: ① CLOCK 처럼 hit 에서 참조 비트 하나만 세팅 (원자적 store, 리스트 조작 없음) ② hit 마다가 아니라 가끔만 MRU 로 옮기기(승격 지연) ③ LBA 범위로 캐시를 샤딩해 코어별로 따로 — 멀티코어 SSD 컨트롤러가 흔히 FTL 작업을 코어별 LBA 범위로 나누는 이유 중 하나. 단일 코어 MCU 에서 ISR 과 공유하면 get/put 전체를 짧은 임계 구역으로 감싼다; 연산이 O(1) 이라 인터럽트 금지 시간이 상수로 묶인다 (단, 체인 길이 최악은 Q2).

</details>

<details>
<summary>Q6. 엔트리당 메모리를 줄이려면?</summary>

Cortex-M 기준 24 B + 버킷 4 B. ① 포인터 대신 **16비트 인덱스**(DS-B07): prev/next/hnext 각 2 B → 엔트리 16 B (`thumbv7m` 타깃에서 `sizeof` 확인), 버킷도 2 B. 엔트리 65535 개까지. ② dirty 를 pba 의 최상위 비트에 넣기 (PBA 공간이 31비트로 충분하면). ③ lba 를 버킷 번호로 일부 복원(태그만 저장) — 집합 연관 캐시의 tag 와 같은 발상. ④ 엔트리 단위를 포기하고 맵 페이지 단위로 캐시하면 엔트리 오버헤드가 사라진다(연습 1). 실측 기준으로 저울질: 같은 32 KiB 에서 엔트리 1024개 vs 맵 페이지 8장(매핑 8192개).

</details>

<details>
<summary>Q7. Linux 커널이나 다른 곳에 이런 구조가 실제로 있나요?</summary>

`lib/lru_cache.c` / `include/linux/lru_cache.h` — DRBD 의 activity log 용 고정 크기 LRU (해시 + `list_head`). eBPF 의 `BPF_MAP_TYPE_LRU_HASH` (`kernel/bpf/bpf_lru_list.c`) — 해시 맵이 꽉 차면 근사 LRU 로 축출하며, 동시성 때문에 CPU 별 로컬 리스트를 둔다. 페이지 캐시는 Q4 의 active/inactive 리스트. 모두 "엔트리 하나가 해시와 리스트에 동시에 걸린 침입형 구조" 라는 점이 이 레슨과 같다.

</details>

## 9. 추가 연습

### 연습 1 — 맵 페이지 단위 캐시 vs 엔트리 단위 캐시

LBA 65536 개(4 B 매핑 → 맵 256 KiB = 4 KiB 맵 페이지 64장). 같은 32 KiB 예산으로 ① 엔트리 1024개(Cortex-M 기준 약 28 KiB) ② 맵 페이지 8장(키 = `lba / 1024`)을 만들어 4가지 트레이스의 hit rate 를 비교하라. 핫셋이 연속일 때와 흩어져 있을 때(`j * 40503 mod 2^16`, 40503 은 홀수라 전단사) 결과가 어떻게 갈리는가?

<details>
<summary>답 보기</summary>

키만 `lba / PER_PAGE` 로 바꾸면 같은 LRU 가 페이지 캐시가 된다. 실제 출력:

```text
[연습 1] 같은 32 KiB: 엔트리 캐시 1024개 vs 맵 페이지 캐시 8장 (LBA 65536)
  trace         entry hit   page hit
  seq               0.00%     99.90%
  random            1.59%     12.37%
  hot-contig       24.67%     90.58%
  hot-scatter      24.88%     12.63%
```

페이지 캐시는 엔트리 오버헤드가 없어 같은 메모리에 매핑을 8배(8192 vs 1024) 담고, 순차에선 1024번에 1번만 miss (99.90%). 랜덤은 8/64 = 12.5% vs 1024/65536 = 1.56%. 하지만 **핫셋이 흩어지면** 핫 LBA 3277개가 64장 전부에 퍼져 페이지 8장으로는 못 덮고(12.63%), 엔트리 캐시는 핫 엔트리만 골라 담아 24.88% 로 이긴다. 그래서 실제 FTL 은 맵 페이지 캐시 + 엔트리 캐시를 섞거나 워크로드에 맞춰 단위를 고른다.

</details>

### 연습 2 — CLOCK(second-chance)로 LRU 근사

슬롯 배열 + 참조 비트 + 시계바늘(hand). hit 은 `ref = 1` 만, miss 는 바늘이 `ref == 1` 을 0 으로 지우며 지나가다 `ref == 0` 인 슬롯을 교체. 새 엔트리는 `ref = 0` 으로 시작한다(이 구현의 선택 — 1 로 시작하는 변형도 있다). cap 3, 접근 `A B C A D` 에서 누가 쫓겨나는가? cap 256 에서 LRU 와 hit rate 를 비교하라.

<details>
<summary>답 보기</summary>

A B C 가 ref 0 으로 들어가고 A hit → A 만 ref 1. D miss: 바늘이 A(1→0, 통과) → B(0) 교체. LRU 도 B — 같다. hit 경로가 비트 하나 쓰기라 리스트 조작이 없고 동시성에 유리하다(Q5). 실제 출력 (시드가 본문과 달라 LRU 값이 조금 다르다):

```text
[연습 2] cap 256, LBA 4096: LRU vs CLOCK hit rate
  trace          LRU    CLOCK
  loop-240    99.52%   99.52%
  random       6.16%    6.15%
  hotspot     85.35%   87.77%
```

hotspot 에서 CLOCK 이 오히려 높은 것은 새 엔트리가 ref 0 으로 들어와 한 번 더 쓰이기 전에는 보호받지 못하기 때문 — 1회성 차가운 접근이 핫 엔트리보다 먼저 나간다 (2Q 와 비슷한 효과). 조회는 `slot_of[lba]` 배열로 했는데, 해시로 바꿔도 교체 정책과는 무관하다.

</details>

### 연습 3 — Clean-first 축출 창 (CFLRU)

dirty 축출은 NAND program, clean 축출은 공짜다. LRU 꼬리에서 `window` 칸 안에 clean 엔트리가 있으면 그것을 먼저 쫓아내고, 없으면 꼬리를 쫓아내라. hotspot, cap 256, 쓰기 30% 에서 window 0/16/64/128 의 hit rate 와 write-back 횟수를 측정하고, flush 후 플래시 맵 == truth 를 확인하라.

<details>
<summary>답 보기</summary>

`pick_victim` 이 `lru.prev` 에서 `prev` 방향으로 최대 window 칸을 보며 첫 clean 을 고른다. 엔트리를 해시·리스트에서 떼는 코드는 그대로. 실제 출력:

```text
[연습 3] hotspot, cap 256, 쓰기 30%: 꼬리 window 안의 clean 을 먼저 쫓아내기
  window      hit  writeback
       0   85.38%       3581
      16   86.69%       2890
      64   88.72%       1870
     128   89.49%       1536
```

window 0 은 본문 solution 의 hotspot/256 (85.38%, 3581) 과 정확히 같다 — 두 독립 구현이 일치한다는 교차 확인이다. window 128 에서 write-back 57% 감소. 이 트레이스에선 hit rate 도 올랐는데, 핫 LBA 는 자주 쓰여 dirty 이고 차가운 1회성 읽기는 clean 이라 clean-first 가 사실상 "차가운 것 먼저" 가 됐기 때문이다. **일반 법칙은 아니다** — 읽기 위주의 핫셋이면 clean-first 가 핫 엔트리를 내보내 hit rate 가 떨어진다. 이 아이디어는 플래시용 페이지 교체 정책 CFLRU(Park et al., CASES 2006)와 같다.

</details>

### 연습 4 — 같은 맵 페이지의 dirty 형제를 함께 반영

dirty victim 하나를 반영하려면 결국 그 4 KiB 맵 페이지 전체를 읽고-고치고-program 한다. 그렇다면 그 페이지에 속한 다른 dirty 엔트리도 **같은 program 에 실어** clean 으로 만들 수 있다. LBA 65536, hot-contig, cap 1024 에서 맵 페이지 program 횟수를 naive vs batch 로 세라. 셧다운 flush 는 dirty 가 있는 페이지 수(64비트 비트맵 popcount, DS-B08)만큼만 program 한다.

<details>
<summary>답 보기</summary>

실제 출력: `naive 13303 → batch 1078` (약 12배 감소), 두 경우 모두 flush 후 플래시 맵 == truth. 핫셋이 연속(맵 페이지 4장 안)이라 dirty 형제가 거의 다 같은 페이지에 몰려 있어서다. DFTL 논문이 batch update 라고 부르는 최적화다. 이 답은 형제를 찾으려고 캐시 전체를 O(cap) 로 훑는다 — 실제 펌웨어라면 맵 페이지별 dirty 카운트/리스트를 따로 두어 O(형제 수)로 만든다.

</details>

### 연습 5 — TRIM 을 올바르게

TRIM(NVMe Deallocate) 을 `lru_remove` 로 구현하면 무엇이 깨지는지 재현하고, 올바른 구현을 보여라.

<details>
<summary>답 보기</summary>

캐시에서 지우기만 하면 다음 읽기가 miss → 플래시 맵의 **옛 PBA** 를 읽어 지운 LBA 가 옛 데이터를 가리킨다 (테스트: 503 이 되살아남). 옳은 방법은 `lru_put(lba, UNMAPPED, dirty=true)` — 캐시 안에서는 즉시 "매핑 없음" 이 되고, 축출이나 flush 때 플래시 맵에도 UNMAPPED 가 반영된다. `lru_remove` 는 "캐시 값 = 플래시 값" 이 보장될 때(clean 엔트리 버리기)만 안전하다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/DS-M05_pr && /tmp/DS-M05_pr   # 19/19 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o /tmp/DS-M05_pas && /tmp/DS-M05_pas   # 19/19, 리포트 없음
```

## 10. 핵심 정리

- LRU = 해시(조회 O(1)) + 이중 리스트(순서·축출 O(1)). 한 엔트리가 두 구조에 동시에 걸린다 (침입형).
- `lru.next` = MRU, `lru.prev` = LRU. hit → `list_move_head`. miss 는 순서를 안 바꾼다.
- 축출 순서: victim 내용 복사 → `hash_unlink`(옛 lba 로) → `list_del` → 새 키 채우기 → 버킷 머리 + LRU 머리.
- 체인 삭제는 포인터의 포인터 — 머리/가운데/끝 분기 0개. 버킷 ≥ cap 이면 기대 O(1), 최악 O(n).
- 검사기는 세 구조를 교차 대조: 리스트 일관성, 개수, "리스트 ⊂ 해시", "체인 총수 == size". 걸음 수 상한으로 반드시 멈춘다.
- dirty 는 OR 로만 켜고, 플래시 반영 때만 끈다. clean 축출은 공짜, dirty 축출 = 맵 program.
- 측정: 재사용 거리 > 용량이면 0% (seq, loop 절벽), 랜덤은 cap/N, 핫스팟은 핫셋이 들어가면 85%+. write-back 은 쓰기를 4~62배 합친다.
- TRIM 은 remove 가 아니라 UNMAPPED dirty put. 정전 대비는 PLP / OOB 의 LBA 기록 / 맵 저널.

## 참고자료

- [LeetCode 146. LRU Cache](https://leetcode.com/problems/lru-cache/) — 면접 원형 문제
- A. Gupta, Y. Kim, B. Urgaonkar, "DFTL: A Flash Translation Layer Employing Demand-based Selective Caching of Page-level Address Mappings", ASPLOS 2009 — CMT, translation page, batch update
- S.-Y. Park et al., "CFLRU: A Replacement Algorithm for Flash Memory", CASES 2006 — clean-first 축출
- [Linux lib/lru_cache.c](https://github.com/torvalds/linux/blob/master/lib/lru_cache.c), [include/linux/lru_cache.h](https://github.com/torvalds/linux/blob/master/include/linux/lru_cache.h) — 고정 크기 LRU (DRBD)
- [Linux kernel/bpf/bpf_lru_list.c](https://github.com/torvalds/linux/blob/master/kernel/bpf/bpf_lru_list.c) — `BPF_MAP_TYPE_LRU_HASH`
- [Linux mm/vmscan.c](https://github.com/torvalds/linux/blob/master/mm/vmscan.c) — active/inactive LRU, MGLRU
- N. Megiddo, D. Modha, "ARC: A Self-Tuning, Low Overhead Replacement Cache", FAST 2003
- [NVM Express Base Specification](https://nvmexpress.org/specifications/) — Dataset Management(Deallocate), Flush, Host Memory Buffer
