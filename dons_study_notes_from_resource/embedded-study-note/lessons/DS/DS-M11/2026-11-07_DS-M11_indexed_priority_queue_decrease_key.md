---
id: DS-M11
time: 90분
updated: 2026-11-07
---
# DS-M11 인덱스 우선순위 큐: Decrease-key와 취소

## 학습 목표

- 힙 슬롯 배열 `heap[]` 과 그 역함수 `pos[]` 를 함께 유지하는 **indexed min-heap** 을 만들고, 불변식 `pos[heap[i]] == i` 를 **모든 swap 에서** 지킨다.
- `decrease_key` 는 위로만, `increase_key` 는 아래로만 움직이는 이유를 힙 속성에서 유도하고, `remove(id)` 는 **양방향** sift 가 필요하다는 것을 반례로 보인다.
- 타이머 **재시작(restart)·취소(cancel)** 를 중복 엔트리 없이 O(log n) 으로 처리하고, 만료 순서를 참조 모델과 대조한다.
- Dijkstra 에서 decrease-key 방식과 lazy deletion(중복 push) 방식을 **PQ 크기·비교 횟수·시간** 으로 실측 비교하고, RAM 이 고정된 펌웨어에서 어느 쪽을 고를지 말할 수 있다.
- 오래된 핸들로 새 타이머를 취소하는 ABA 문제를 **세대(generation) 핸들** 로 막는다.

## 1. 문제

RTOS 타이머 서비스, TCP 재전송 타이머, 라우팅 비용 갱신은 모두 "이미 큐에 들어 있는 원소의 우선순위를 바꾸거나 빼기"가 핵심 연산이다. DS-B12 의 이진 힙은 push/pop 만 빠르고, 특정 원소를 찾는 데 O(n) 이 걸린다. 면접에서 "타이머 매니저에 cancel 을 넣어라", "Dijkstra 의 decrease-key 를 어떻게 구현하나" 는 바로 이 꼬리질문이고, 답은 **id → 힙 위치 맵** 하나다. (DS-B12 연습 4 에서 맛본 `pos[]` 맵을 이번에는 정식 자료구조로 만든다.)

**구조**: 원소는 작은 정수 id(타이머 슬롯 번호, 그래프 정점 번호)로 가리킨다. 세 배열이 있다.

| 배열 | 인덱스 | 값 | 의미 |
|---|---|---|---|
| `heap[i]` | 힙 슬롯 i | id | 완전 이진 트리의 i 번 칸에 누가 있나 |
| `pos[id]` | id | 슬롯 (없으면 `IPQ_NONE`) | id 가 지금 몇 번 칸에 있나 — `heap` 의 역함수 |
| `key[id]` | id | 키 | id 의 우선순위. **id 쪽에 붙어 있어서** swap 때 안 옮긴다 |

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m11a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">decrease_key(4, 0) 직후의 indexed min-heap — 노드 = id:key</text>
  <g stroke="currentColor" fill="none">
    <path d="M370 72 L210 112"/><path d="M370 72 L530 112"/>
    <path d="M210 142 L130 182"/><path d="M210 142 L290 182"/>
    <path d="M530 142 L450 182"/><path d="M530 142 L610 182"/>
  </g>
  <rect x="330" y="42" width="80" height="30" rx="6" class="f-acc" stroke="currentColor"/>
  <g class="f-mut" stroke="currentColor">
    <rect x="170" y="112" width="80" height="30" rx="6"/><rect x="490" y="112" width="80" height="30" rx="6"/>
    <rect x="90" y="182" width="80" height="30" rx="6"/><rect x="250" y="182" width="80" height="30" rx="6"/>
    <rect x="410" y="182" width="80" height="30" rx="6"/><rect x="570" y="182" width="80" height="30" rx="6"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="370" y="62">4:0</text>
    <text x="210" y="132">0:1</text><text x="530" y="132">2:2</text>
    <text x="130" y="202">3:60</text><text x="290" y="202">1:50</text><text x="450" y="202">5:3</text><text x="610" y="202">6:4</text>
  </g>
  <g fill="currentColor" font-size="12"><text x="416" y="56">슬롯0</text><text x="256" y="126">슬롯1</text><text x="576" y="126">슬롯2</text>
    <text x="96" y="228">슬롯3</text><text x="256" y="228">슬롯4</text><text x="416" y="228">슬롯5</text><text x="576" y="228">슬롯6</text></g>
  <g fill="currentColor" font-family="monospace">
    <text x="10" y="262">slot</text><text x="90" y="262" text-anchor="middle">0</text><text x="135" y="262" text-anchor="middle">1</text><text x="180" y="262" text-anchor="middle">2</text><text x="225" y="262" text-anchor="middle">3</text><text x="270" y="262" text-anchor="middle">4</text><text x="315" y="262" text-anchor="middle">5</text><text x="360" y="262" text-anchor="middle">6</text>
    <text x="10" y="282">heap</text><text x="90" y="282" text-anchor="middle">4</text><text x="135" y="282" text-anchor="middle">0</text><text x="180" y="282" text-anchor="middle">2</text><text x="225" y="282" text-anchor="middle">3</text><text x="270" y="282" text-anchor="middle">1</text><text x="315" y="282" text-anchor="middle">5</text><text x="360" y="282" text-anchor="middle">6</text>
    <text x="10" y="312">id</text><text x="90" y="312" text-anchor="middle">0</text><text x="135" y="312" text-anchor="middle">1</text><text x="180" y="312" text-anchor="middle">2</text><text x="225" y="312" text-anchor="middle">3</text><text x="270" y="312" text-anchor="middle">4</text><text x="315" y="312" text-anchor="middle">5</text><text x="360" y="312" text-anchor="middle">6</text>
    <text x="10" y="332">pos</text><text x="90" y="332" text-anchor="middle">1</text><text x="135" y="332" text-anchor="middle">4</text><text x="180" y="332" text-anchor="middle">2</text><text x="225" y="332" text-anchor="middle">3</text><text x="270" y="332" text-anchor="middle">0</text><text x="315" y="332" text-anchor="middle">5</text><text x="360" y="332" text-anchor="middle">6</text>
    <text x="10" y="352">key</text><text x="90" y="352" text-anchor="middle">1</text><text x="135" y="352" text-anchor="middle">50</text><text x="180" y="352" text-anchor="middle">2</text><text x="225" y="352" text-anchor="middle">60</text><text x="270" y="352" text-anchor="middle">0</text><text x="315" y="352" text-anchor="middle">3</text><text x="360" y="352" text-anchor="middle">4</text>
  </g>
  <rect x="252" y="268" width="36" height="20" fill="none" stroke="currentColor" stroke-width="2"/>
  <rect x="117" y="318" width="36" height="20" fill="none" stroke="currentColor" stroke-width="2"/>
  <path d="M150 318 L262 292" stroke="currentColor" fill="none" stroke-width="1.5" marker-end="url(#m11a)"/>
  <g fill="currentColor" font-size="12">
    <text x="470" y="270">pos[1] = 4  →  heap[4] = 1</text>
    <text x="470" y="290">heap[] 과 pos[] 는 서로의 역함수:</text>
    <text x="470" y="310">pos[heap[i]] == i  (모든 i &lt; n)</text>
    <text x="470" y="330">heap[pos[id]] == id (큐에 있는 모든 id)</text>
    <text x="470" y="350">key[] 는 id 로 찾는다 — swap 해도 그대로</text>
  </g>
</svg>
```

메모리: `heap` 2 B + `pos` 2 B + `key` 4 B = **id 당 8 B**, 1024 id 면 8 KB 가 `.bss` 에 고정된다. id 가 유일하고 `< IPQ_CAP` 이므로 큐에는 최대 `IPQ_CAP` 개만 들어갈 수 있다 — **용량 초과가 구조적으로 불가능**하다 (DS-B12 의 `push` 가 `false` 를 돌려주던 경우가 사라진다). 순서는 DS-B12 와 같이 `(key, id)` 전순서.

### Part A — swap(+pos) / sift / insert / pop

- `ipq_swap(q, i, j)` — 두 슬롯의 id 를 바꾸고 **바뀐 두 id 의 `pos` 도 고친다**. 불변식을 지키는 유일한 통로.
- `sift_up`, `sift_down` — DS-B12 와 같되 모든 교환을 `ipq_swap` 으로.
- `ipq_insert(q, id, key)` — id 가 범위 밖이거나 이미 있으면 `false`.
- `ipq_pop(q, &id, &key)` — 루트를 꺼내고 그 id 의 `pos` 를 `IPQ_NONE` 으로.
- 테스트: 빈 큐, 7개 insert 마다 `ipq_check()` + peek, 중복/범위 밖 거절, pop 순서, 1024 id 를 키 내림차순으로 가득 채우고 비우기.

### Part B — decrease_key / increase_key / remove(id)

- `ipq_decrease_key(q, id, k)` — `k > key[id]` 면 계약 위반으로 `false`, 상태 그대로. 아니면 키를 바꾸고 **sift-up**.
- `ipq_increase_key(q, id, k)` — `k < key[id]` 면 `false`. 아니면 **sift-down**.
- `ipq_remove(q, id)` — 마지막 원소로 구멍을 메우고 **위 또는 아래로**. (`ipq_update` 는 둘을 고르는 래퍼로 주어진다.)
- 테스트: 그림의 두 경우(위로 가야 하는 remove, 아래로 가야 하는 remove), 루트·마지막 슬롯·유일 원소 삭제, 계약 위반 거절, 랜덤 30000 연산 × 2 (키 0..999 / 동점 폭주 키 0..7) 를 선형 탐색 참조 모델과 매번 대조.

### Part C — 타이머 재시작/취소 + Dijkstra

- `timer_arm(q, id, expire)` — 돌고 있지 않으면 insert, 돌고 있으면 **만료 시각만 바꾼다**(재시작). 중복 엔트리가 생기지 않는다. `timer_cancel` = `ipq_remove`, `timer_service` 는 주어진다.
- `dijkstra_pq(g, q, src, dist, prev)` — 큐에 있는 정점은 decrease_key, 없는 정점은 insert. 반환값은 decrease_key 호출 수.
- 테스트: 재시작(늦춤/당김)·취소·동시 만료 시나리오, 랜덤 타이머 시뮬레이션 20000 스텝, 손으로 추적한 5정점 그래프, 랜덤 그래프 60개를 **O(V²) 배열 Dijkstra 참조**와 `dist` 완전 일치 + `prev` 경로 검증. (Dijkstra 알고리즘 자체는 ALGO-M06 에서 자세히 — 여기서는 PQ 의 검증 입력으로 쓴다.)

## 2. Problem (English)

> **Indexed min-priority queue with decrease-key and cancel.**
>
> Items are identified by integer ids in `[0, 1024)`; each live id has a `uint32_t` key. Order is by key, ties broken by id. Use static arrays only.
>
> 1. Implement `insert(id, key)`, `peek`, `pop`, `contains(id)` — `insert` rejects a duplicate or out-of-range id.
> 2. Implement `decrease_key(id, k)` (reject if `k > key[id]`), `increase_key(id, k)` (reject if `k < key[id]`), and `remove(id)`, all in `O(log n)`.
> 3. Maintain the invariant `pos[heap[i]] == i` for every slot `i < n`, and verify it plus the heap property after every operation.
> 4. Build a timer service on it (`arm` = start or restart, `cancel`, `service(now)`), and use it in Dijkstra; check Dijkstra against an `O(V^2)` array implementation.
>
> Example: insert (id:key) 0:1, 1:50, 2:2, 3:60, 4:70, 5:3, 6:4 → `remove(4)` moves id 6 (key 4) into slot 4, and it must then **sift up** past id 1 (key 50). Result: `heap = [0, 6, 2, 3, 1, 5]`, `pos[6] = 1`, `pos[1] = 4`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "4/35 passed", exit 1 — TODO 10곳을 채워 35/35 를 만든다
```

TODO: `ipq_swap`, `sift_up`, `sift_down`, `ipq_insert`, `ipq_pop` (Part A) / `ipq_decrease_key`, `ipq_increase_key`, `ipq_remove` (Part B) / `timer_arm`, `dijkstra_pq` 본문 (Part C). `ipq_check()`, 참조 모델, `ipq_update`, `timer_service`, O(V²) 참조 Dijkstra, 그래프 생성, 하네스는 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — swap 한 곳만 고치면 불변식이 전부 지켜진다</summary>

sift 는 "슬롯 두 개 교환"의 반복이다. 교환할 때 `heap[i]`, `heap[j]` 를 바꾼 **뒤에** `pos[heap[i]] = i; pos[heap[j]] = j;` 를 하면, 교환 전후로 `pos[heap[·]] == ·` 가 유지된다. 그다음엔 sift_up/down 안에서 배열을 직접 건드리지 말고 `ipq_swap` 만 부른다. `key[]` 는 id 에 붙어 있으니 건드릴 필요가 없다.

</details>

<details>
<summary>힌트 2 — decrease 는 위로만, increase 는 아래로만</summary>

키가 작아진 노드 x 를 보자. x 의 자식들은 원래 `key[x]` 이상이었다 → 더 작아진 x 보다도 여전히 이상. 깨질 수 있는 건 x 와 **부모** 사이뿐이다 → sift-up. 반대로 키가 커지면 부모와의 관계는 여전히 맞고, 자식과의 관계만 깨질 수 있다 → sift-down. 계약을 어긴 호출(decrease 인데 키가 커짐)을 그냥 받아 주면 sift-up 은 아무것도 안 하고 힙 속성이 조용히 깨진다. 그래서 거절한다.

</details>

<details>
<summary>힌트 3 — remove 의 방향은 옮겨 온 값이 정한다</summary>

슬롯 i 를 지우고 마지막 슬롯의 원소 y 로 메운다. y 는 **트리의 다른 가지**에서 왔을 수 있다. y 가 i 의 부모보다 작을 수도 있고(그림 2 왼쪽: 위로), i 의 자식보다 클 수도 있다(아래로). 둘 다 동시에는 불가능하다 — 부모 ≤ 원래 i 의 원소 ≤ 자식이었으므로 y 가 부모보다 작으면 자식들보다도 작다. 그래서 "부모보다 작으면 sift_up, 아니면 sift_down" 한 번의 분기로 충분하다. i 가 마지막 슬롯이었으면 메울 것이 없다.

</details>

<details>
<summary>힌트 4 — pop 에서 n 이 0 이 되는 순간</summary>

`pos[top] = NONE` 을 먼저 하고, `n--` 한 뒤 **남은 원소가 있을 때만** 마지막을 루트로 옮긴다. 남은 게 없는데 `heap[0] = heap[n]; pos[heap[0]] = 0;` 을 하면 `heap[0]` 은 방금 꺼낸 top 자신이라 `pos[top] = 0` — 꺼낸 id 가 "아직 있다"로 부활한다.

</details>

<details>
<summary>힌트 5 — Dijkstra 에서 큐에 있는 정점 vs 확정된 정점</summary>

음이 아닌 가중치에서 pop 된 정점 u 의 `dist[u]` 는 최종값이다. 이후 u 로 가는 어떤 경로도 `nd >= dist[u]` 이므로 `if (nd >= dist[v]) continue;` 가 확정 정점을 자동으로 걸러 준다. 개선이 있으면 `contains(v)` 로 decrease 와 insert 를 고른다. 따로 visited 배열이 필요 없다.

</details>

## 5. 풀이

### Part A — 불변식과 swap

불변식 세 가지를 `ipq_check()` 가 매 연산 후 검사한다.

1. **역함수**: 모든 슬롯 `i < n` 에 대해 `pos[heap[i]] == i`, 그리고 `pos[id] != NONE` 인 모든 id 에 대해 `heap[pos[id]] == id`.
2. **개수**: `pos[id] != NONE` 인 id 의 수 == `n`. (1 만으로는 "큐 밖 id 가 쓰레기 슬롯을 가리키는" 경우를 못 잡는다.)
3. **힙 속성**: 모든 `i ≥ 1` 에 대해 `(key, id)[heap[parent(i)]] ≤ (key, id)[heap[i]]`.

`ipq_swap` 이 바꾸는 것은 `heap[i]`, `heap[j]`, `pos[a]`, `pos[b]` (a, b 는 두 id) 네 칸뿐이다. 다른 id 의 pos 는 다른 슬롯을 가리키므로 영향이 없다. 그래서 모든 이동이 swap 을 거치면 1 은 자동으로 유지되고, 2 는 insert/pop/remove 에서만 바뀐다.

insert 와 pop 은 DS-B12 와 똑같고 `pos` 세 줄이 추가될 뿐이다.

| 연산 | DS-B12 | 추가로 할 일 |
|---|---|---|
| insert(id, k) | `a[n] = e; sift_up(n); n++` | `key[id] = k`, `heap[n] = id`, `pos[id] = n` |
| pop | `out = a[0]; n--; a[0] = a[n]; sift_down(0)` | `pos[top] = NONE`, 옮긴 id 의 `pos = 0`, **n == 0 이면 옮기지 않기** |
| 이동 | `ev_swap` | `ipq_swap` 이 pos 2개 갱신 |

**키를 슬롯이 아니라 id 에 둔 이유**: swap 때 2 B id 만 옮기면 되고, `decrease_key(id)` 가 `key[id]` 를 바로 고칠 수 있다. 대가는 비교마다 `key[heap[i]]` 한 단계 간접 참조 — 캐시 지역성이 나빠진다 (면접 Q6).

### Part B — decrease / increase / remove

**decrease_key(4, 0)** 추적 — 7개 적재 상태(`heap = 0 1 2 3 4 5 6`, 키 `1 50 2 60 70 3 4`)에서 시작:

| 단계 | 슬롯 i (id) | 부모 (id:key) | 비교 | 동작 | 바뀐 pos |
|---|---|---|---|---|---|
| 0 | 4 (id4, 키 70→0) | 슬롯1 (1:50) | 0 &lt; 50 | swap(4, 1) | pos[4]=1, pos[1]=4 |
| 1 | 1 (id4) | 슬롯0 (0:1) | 0 &lt; 1 | swap(1, 0) | pos[4]=0, pos[0]=1 |
| 2 | 0 | — | 루트 | 끝 | |

결과가 그림 1 이다. 비교 2번, 경로 하나만 고쳤다.

**remove 는 양방향** — 같은 7개 상태에서 두 경우:

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m11b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">remove(4): 마지막 6:4 가 슬롯4 로 → 위로</text>
  <text x="390" y="22" fill="currentColor" font-weight="bold">remove(2): 마지막 6:4 가 슬롯2 로 → 아래로</text>
  <g stroke="currentColor" fill="none">
    <path d="M185 75 L100 115"/><path d="M185 75 L270 115"/><path d="M100 145 L55 185"/><path d="M100 145 L145 185"/><path d="M270 145 L225 185"/><path d="M270 145 L315 185"/>
    <path d="M565 75 L480 115"/><path d="M565 75 L650 115"/><path d="M480 145 L435 185"/><path d="M480 145 L525 185"/><path d="M650 145 L605 185"/><path d="M650 145 L695 185"/>
  </g>
  <g class="f-mut" stroke="currentColor">
    <rect x="155" y="45" width="60" height="30" rx="6"/><rect x="240" y="115" width="60" height="30" rx="6"/>
    <rect x="25" y="185" width="60" height="30" rx="6"/><rect x="195" y="185" width="60" height="30" rx="6"/>
    <rect x="535" y="45" width="60" height="30" rx="6"/><rect x="450" y="115" width="60" height="30" rx="6"/>
    <rect x="405" y="185" width="60" height="30" rx="6"/><rect x="495" y="185" width="60" height="30" rx="6"/>
  </g>
  <rect x="70" y="115" width="60" height="30" rx="6" class="f-ok" stroke="currentColor"/>
  <rect x="115" y="185" width="60" height="30" rx="6" class="f-warn" stroke="currentColor"/>
  <rect x="285" y="185" width="60" height="30" rx="6" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="620" y="115" width="60" height="30" rx="6" class="f-warn" stroke="currentColor"/>
  <rect x="575" y="185" width="60" height="30" rx="6" class="f-ok" stroke="currentColor"/>
  <rect x="665" y="185" width="60" height="30" rx="6" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="185" y="65">0:1</text><text x="100" y="135">1:50</text><text x="270" y="135">2:2</text>
    <text x="55" y="205">3:60</text><text x="145" y="205">6:4</text><text x="225" y="205">5:3</text><text x="315" y="205" font-size="12">비움</text>
    <text x="565" y="65">0:1</text><text x="480" y="135">1:50</text><text x="650" y="135">6:4</text>
    <text x="435" y="205">3:60</text><text x="525" y="205">4:70</text><text x="605" y="205">5:3</text><text x="695" y="205" font-size="12">비움</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="2" marker-end="url(#m11b)">
    <path d="M140 182 L112 150"/>
    <path d="M640 148 L615 180"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="245">id4(70) 자리에 6:4 — 부모 1:50 보다 작다</text>
    <text x="10" y="265">→ sift_up: 슬롯4↔1, 루트 0:1 보다 큼 → 멈춤</text>
    <text x="10" y="285" font-family="monospace">heap = 0 6 2 3 1 5</text>
    <text x="10" y="305" font-family="monospace">pos[6]=1 pos[1]=4 pos[4]=NONE</text>
    <text x="390" y="245">id2(2) 자리에 6:4 — 부모 0:1 보다 큼</text>
    <text x="390" y="265">→ sift_down: 자식 5:3 과 교환 (슬롯2↔5)</text>
    <text x="390" y="285" font-family="monospace">heap = 0 1 5 3 4 6</text>
    <text x="390" y="305" font-family="monospace">pos[5]=2 pos[6]=5 pos[2]=NONE</text>
  </g>
</svg>
```

왼쪽이 핵심이다. sift_down 만 하는 구현은 슬롯 4 (잎)에서 바로 멈추고, 부모 1:50 아래에 4 가 남아 힙 속성이 깨진다. DS-B12 의 pop 은 항상 루트(i = 0)를 지우니 위로 갈 일이 없었고, 그래서 이 버그가 숨어 있기 쉽다. 실제로 solution.c 에서 sift_up 분기만 지운 변형을 돌리면 `30/35 passed` — 그림의 remove(4) 검사 두 개, 랜덤 대조 두 개, 랜덤 타이머 시뮬레이션이 FAIL 한다.

remove 의 경계:

| 경우 | 처리 |
|---|---|
| id 가 없음 / 범위 밖 | `false` |
| 지운 슬롯 == 마지막 슬롯 (`i == n` after `n--`) | 옮길 것 없음, 바로 끝 (`remove(6)` 테스트) |
| 루트 (i = 0) | 부모가 없으니 `i > 0` 검사로 자동으로 sift_down → pop 과 같음 |
| 유일 원소 | n = 1 → 0, 위 "마지막 슬롯" 경우 |

`pos[id] = NONE` 은 **옮기기 전에** 한다. 지운 id 가 마지막 슬롯 원소였다면 early return 으로 빠지므로 덮어쓸 위험이 없다.

**랜덤 대조** — 참조 모델은 `live[id]`, `rkey[id]` 두 배열이고, 최소는 선형 탐색이다. 매 연산 후 ① 반환값이 참조와 같은지(거절 조건 포함), ② `ipq_check()`, ③ 모든 id 의 `contains`·키가 참조와 같은지. 두 번째 실행은 키를 0..7 로 좁혀 동점이 폭주하게 만들어 `(key, id)` tie-break 를 시험한다. 실제 출력:

```text
[B] random ops (keys 0..999): ins 5809 dec 1253 inc 577 upd 1812 rm 1751 pop 3769, max n 329 -> match
[B] random ops (keys 0..7)  : ins 5977 dec 1357 inc 648 upd 1739 rm 1774 pop 3684, max n 558 -> match
```

(숫자는 **성공한** 연산 수. decrease 는 무작위 새 키가 현재 키 이하일 때만 성공하므로 시도의 약 절반이다 — 거절 경로도 그만큼 시험됐다.)

**복잡도와 결정성**

| 연산 | 시간 (최악) | 비고 |
|---|---|---|
| `contains`, `peek`, `key_of` | O(1) | 배열 조회 |
| `insert`, `pop`, `decrease`, `increase`, `remove`, `update` | O(log n) | 경로 하나, 1024 개면 최대 ⌊log₂ 1024⌋ = 10 단계 |
| `ipq_init` | O(CAP) | `pos[]` 를 NONE 으로 — 부팅 때 한 번 |
| 메모리 | 8 B × CAP | 고정, 원소 수와 무관 |

amortized 가 아니라 **매 호출 최악 상한**이 10 단계라는 점이 ISR/임계 구역에서 중요하다 (면접 Q4).

### Part C — 타이머 재시작/취소

타이머 슬롯 id 마다 엔트리가 **최대 하나**다. `timer_arm` 은 "없으면 insert, 있으면 update" — 재시작이 엔트리를 늘리지 않는다. 시나리오 추적 (테스트와 동일):

| 호출 | 큐 상태 (id:expire, 만료 순) | 내부 동작 |
|---|---|---|
| arm 1:100, 2:50, 3:75, 4:200, 5:60 | 2:50 5:60 3:75 1:100 4:200 | insert × 5 |
| arm(2, 150) — keepalive 받아 늦춤 | 5:60 3:75 1:100 2:150 4:200 | update → increase_key (sift-down) |
| arm(4, 70) — 마감 당김 | 5:60 4:70 3:75 1:100 2:150 | update → decrease_key (sift-up) |
| cancel(3), 다시 cancel(3) | 5:60 4:70 1:100 2:150 | remove / 두 번째는 `false` |
| arm(6, 60) | 5:60 6:60 4:70 1:100 2:150 | insert |
| service(now=65) | → fired 5, 6 | 같은 60 은 id 순 |
| service(now=100) | → fired 4, 1 | |
| cancel(1) | `false` | 이미 만료 = pop 때 pos NONE |
| service(now=1000) | → fired 2 | |

랜덤 시뮬레이션: 타이머 256개에 대해 arm(재시작 포함)/cancel/시간 진행+service(한 번에 최대 4개)를 20000 스텝, 만료 순서를 참조 모델(선형 탐색으로 `(expire, id)` 최소)과 매번 대조:

```text
[C] timer sim 20000 steps: fired 6837, restarts 2055, cancels 1061 -> match
```

펌웨어 연결: UART 수신 idle 타임아웃, BLE supervision timeout, 워치독 킥 감시처럼 **"이벤트가 올 때마다 마감을 뒤로 미는"** 타이머는 재시작이 등록보다 훨씬 잦다. 재시작 = increase_key 한 번(아래로 최대 10 단계)이고 RAM 은 안 늘어난다. (32비트 tick 랩어라운드는 DS-B12 연습 3 의 `(int32_t)(a - b) < 0` 비교로 — 여기서는 단조 증가 tick 을 가정했다.)

### Part C — Dijkstra 검증

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m11c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">5정점 예제 (메시 노드 간 지연 ms) — 굵은 간선 = 최단 경로 트리(prev)</text>
  <g stroke="currentColor" fill="none" marker-end="url(#m11c)">
    <path d="M78 120 L262 62" stroke-dasharray="4 3"/>
    <path d="M78 136 L262 196" stroke-width="2.5"/>
    <path d="M290 178 L290 82" stroke-width="2.5"/>
    <path d="M318 62 L502 112" stroke-width="2.5"/>
    <path d="M318 196 L502 136" stroke-dasharray="4 3"/>
    <path d="M318 210 L462 218" stroke-width="2.5"/>
    <path d="M497 201 L519 150" stroke-dasharray="4 3"/>
  </g>
  <g stroke="currentColor">
    <circle cx="60" cy="128" r="20" class="f-acc"/>
    <circle cx="290" cy="60" r="20" class="f-mut"/><circle cx="290" cy="200" r="20" class="f-mut"/>
    <circle cx="525" cy="128" r="20" class="f-mut"/><circle cx="490" cy="220" r="20" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="60" y="133">0</text><text x="290" y="65">1</text><text x="290" y="205">2</text><text x="525" y="133">3</text><text x="490" y="225">4</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="160" y="80">10</text><text x="160" y="182">3</text><text x="298" y="135">4</text>
    <text x="410" y="78">2</text><text x="405" y="160">8</text><text x="385" y="230">2</text><text x="516" y="184">5</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="60" y="100" text-anchor="middle">d=0</text><text x="262" y="50" text-anchor="end">d=7</text>
    <text x="250" y="240" text-anchor="middle">d=3</text><text x="580" y="120">d=9</text><text x="530" y="240">d=5</text>
  </g>
</svg>
```

| 단계 | pop (u, d) | 완화(relax) | PQ 연산 | 이후 PQ (id:key, 키 순) |
|---|---|---|---|---|
| 0 | — | dist[0] = 0 | insert 0:0 | 0:0 |
| 1 | 0, 0 | 1 ← 10, 2 ← 3 | insert, insert | 2:3 1:10 |
| 2 | 2, 3 | 1: 7 &lt; 10, 3 ← 11, 4 ← 5 | **decrease(1,7)**, insert, insert | 4:5 1:7 3:11 |
| 3 | 4, 5 | 3: 10 &lt; 11 | **decrease(3,10)** | 1:7 3:10 |
| 4 | 1, 7 | 3: 9 &lt; 10 | **decrease(3,9)** | 3:9 |
| 5 | 3, 9 | (나가는 간선 없음) | — | 비어 있음 |

```text
[C] 5-node example: dist = 0 7 3 9 5, decrease_key calls = 3
```

랜덤 검증: V = 1..200, E = 0..8V (최대 4096), 가중치 0..99 (**0 포함**, 자기 루프·중복 간선 허용), 시작 정점 랜덤인 그래프 60개. 참조는 인접 행렬 + 매 단계 선형 탐색의 O(V²) Dijkstra — 힙이 전혀 없어서 PQ 버그와 독립적이다. `dist[]` 는 `memcmp` 로 완전 일치, `prev[]` 는 "src 와 도달 불가는 NONE, 나머지는 `dist[p] + w(p,v) == dist[v]` 이고 따라가면 src 에 도착"으로 검사:

```text
[C] dijkstra 60 random graphs (E total 20593): dist match, prev ok, decrease_key 1605, unreachable 1258
```

도달 불가 정점이 1258 개 나왔다 — 희소 그래프에서 INF 경로까지 시험됐다는 뜻이다.

### 관찰 — decrease-key vs lazy deletion (`demo_lazy_vs_indexed.c`)

decrease-key 없이 Dijkstra 를 짜는 흔한 방법은 **lazy deletion**: 개선될 때마다 `(nd, v)` 를 그냥 또 push 하고, pop 할 때 `d > dist[v]` 인 낡은 엔트리를 버린다. C++ `std::priority_queue` 에 decrease-key 가 없어서 면접 코드에서 대부분 이 방식을 쓴다. 같은 그래프에서 비교:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_lazy_vs_indexed.c -o demo && ./demo
```

```text
Dijkstra, V=1000, weights 1..1000, src 0 (times: avg of 20 runs, this Mac, -O2)
E      | method  | PQ ops | max PQ | stale pops | compares | us/run
4000   | indexed |   1279 |    408 |          0 |    15289 |   49.5
4000   | lazy    |   1279 |    526 |        302 |    20317 |   46.2
4000   | O(V^2)  |      - |      - |          - |        - | 1073.5   dist all equal: yes
16000  | indexed |   2347 |    765 |          0 |    19128 |   66.2
16000  | lazy    |   2347 |   1629 |       1347 |    45198 |  107.3
16000  | O(V^2)  |      - |      - |          - |        - | 1064.6   dist all equal: yes
64000  | indexed |   3749 |    912 |          0 |    21741 |  109.2
64000  | lazy    |   3749 |   3038 |       2749 |    79348 |  244.2
64000  | O(V^2)  |      - |      - |          - |        - | 1597.3   dist all equal: yes

RTO timers: 64 connections, 100000 events (98% restarts)
indexed: max heap 64 entries, fired 4684
lazy   : max heap 258 entries, fired 4684, stale pops 97861
```

("PQ ops" = 완화가 일으킨 insert/decrease 또는 push 수 — 둘이 같은 게 당연하다. 시간은 이 Mac(Apple Silicon, -O2) 기준이고 실행마다 수 % 흔들린다. 비율만 볼 것.)

| 관찰 | 해석 |
|---|---|
| max PQ: indexed 912 vs lazy 3038 (E=64000) | indexed 는 **정점 수 V 이하가 보장**. lazy 는 이론상 E+1 까지 → 펌웨어에서는 최악을 위해 E 칸을 정적으로 잡아야 한다 |
| 비교: 21741 vs 79348 | lazy 는 큰 힙 + 버릴 엔트리 2749 개를 pop 하느라 sift-down 비용을 낭비 |
| E=4000 에서는 lazy 가 조금 빠름 | 낡은 엔트리가 적으면 간접 참조(`key[heap[i]]`)가 없는 lazy 가 유리. 정답은 하나가 아니다 |
| O(V²) 는 1~1.6 ms | V=1000 에서 E=64000 도 V² = 10⁶ 의 6 % — 아직 "희소". 힙 버전이 10배 이상 빠르다. E 가 V² 에 가까운 조밀 그래프에서만 배열 버전이 이긴다 |
| RTO 타이머: 64 vs 258 엔트리, 낡은 pop 97861 | 재시작 98 % 워크로드에서 lazy 는 재시작마다 쓰레기를 만들고 나중에 하나씩 치운다. indexed 는 항상 정확히 연결 수만큼 |

결론: **엔트리 수 상한이 RAM 예산을 정하는** 펌웨어에서는 indexed PQ 가 상한을 V(또는 타이머 슬롯 수)로 고정해 준다는 것이 가장 큰 장점이다. 데스크톱 알고리즘 문제에서는 lazy 가 코드가 짧고 대개 충분하다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 35/35 passed
# 메모리 오류 / UB 확인
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan
```

실제 전체 출력:

```text
[B] random ops (keys 0..999): ins 5809 dec 1253 inc 577 upd 1812 rm 1751 pop 3769, max n 329 -> match
[B] random ops (keys 0..7)  : ins 5977 dec 1357 inc 648 upd 1739 rm 1774 pop 3684, max n 558 -> match
[C] timer sim 20000 steps: fired 6837, restarts 2055, cancels 1061 -> match
[C] 5-node example: dist = 0 7 3 9 5, decrease_key calls = 3
[C] dijkstra 60 random graphs (E total 20593): dist match, prev ok, decrease_key 1605, unreachable 1258
35/35 passed
```

ASan/UBSan 빌드도 같은 출력에 `35/35 passed`, 경고 없음.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| swap 에서 `heap[]` 만 바꾸고 `pos[]` 갱신 누락 | 다음 decrease/remove 가 엉뚱한 슬롯을 고친다. 이 두 줄을 지운 변형: `14/35 passed` | 모든 이동을 `ipq_swap` 으로 |
| remove 에서 옮겨 온 id 의 `pos` 갱신 누락 | 옮긴 원소가 이후 sift 로 움직이면 swap 이 pos 를 고쳐 줘서 **우연히 맞고**, 제자리에 머물 때만 틀린다. 그래서 그림의 두 remove 테스트는 통과하고 랜덤 대조 2개 + 타이머 취소 + 타이머 시뮬레이션만 잡는다: `31/35 passed` — 랜덤 테스트가 필요한 이유 | `heap[i] = heap[n]; pos[heap[i]] = i;` |
| remove 에서 sift_down 만 | 다른 가지에서 온 작은 값이 부모 아래 남음 (그림 2 왼쪽). sift_up 분기를 지운 변형: `30/35 passed` | `if (i > 0 && less(i, parent)) sift_up; else sift_down;` |
| pop 이 `pos[top] = NONE` 누락 | 꺼낸 id 가 "있음"으로 남아 재등록이 거절되고, cancel 이 남의 슬롯을 지움. 변형: `27/35 passed` | `pos[top] = IPQ_NONE` |
| pop 에서 n 이 0 이 될 때도 `pos[heap[0]] = 0` | `heap[0]` 은 top 자신 → 방금 지운 id 부활. 이 가드를 지운 변형: `30/35 passed` | `if (q->n > 0) { ... }` 안에서만 옮기기 |
| decrease_key 에 더 큰 키를 받아 줌 | sift_up 은 아무것도 안 함 → 자식보다 큰 부모가 남는다 | 계약 검사 후 거절, 방향을 모르면 `update` |
| 재시작을 insert 로 (같은 id 를 또 넣음) | indexed 구조에서는 거절되고, 보통 힙이면 중복 엔트리 → 옛 마감에 오발 | `contains` 면 `update` (`timer_arm`) |
| Dijkstra 에서 "큐에 없음 = 미방문"으로 보고 insert | 이미 확정돼 큐에서 나간 정점을 다시 넣어 재처리 | `if (nd >= dist[v]) continue;` 가 먼저 (음 아닌 가중치에서 확정 정점은 절대 통과 못 함) |
| `INF + w` 로 완화 | `UINT32_MAX + w` 는 unsigned 랩어라운드로 작은 값이 된다 (UB 는 아니지만 틀린 값) | 큐에서 나온(유한한) 정점에서만 완화. O(V²) 판은 `dist[u] != INF` 확인 |
| `uint16_t pos` 에 `CAP = 65536` | 슬롯 65535 가 `IPQ_NONE` 과 같아진다 | `CAP < 0xFFFF` 이거나 `pos` 를 `uint32_t` 로 |

## 8. 면접 Q&A

<details>
<summary>Q1. 힙에서 임의 원소의 키를 바꾸려면 무엇이 필요하고, 왜 O(log n) 인가요?</summary>

원소의 **현재 슬롯**을 O(1) 에 알아야 한다. id 가 작은 정수면 `pos[id]` 배열, 아니면 원소 구조체 안에 `index` 필드를 두고 swap 때 갱신한다 (Go `container/heap` 문서의 PriorityQueue 예제가 `Swap` 에서 `item.index` 를 갱신하고 `heap.Fix(h, i)` 로 키 변경 후 재정렬하는 바로 이 패턴). 위치를 알면 키를 바꾸고 한 방향으로만 sift — 경로 길이 ⌊log₂ n⌋ 이 상한. 꼬리질문 "id 가 큰 해시값이면?" → 해시맵 id → slot 을 쓰거나, 객체에 index 를 내장(intrusive)한다.

</details>

<details>
<summary>Q2. remove(id) 에서 sift-up 과 sift-down 을 둘 다 고려해야 하는 이유를 반례로 보이세요.</summary>

keys `[1, 50, 2, 60, 70, 3, 4]` (0-based 슬롯). 슬롯 4 (70) 를 지우면 마지막 4 가 옮겨 온다. 4 는 오른쪽 가지 출신이라 왼쪽 가지의 부모 50 보다 작다 → 위로 가야 한다. 반대로 슬롯 2 (2) 를 지우면 4 가 와서 자식 3 보다 크다 → 아래로. 두 방향이 동시에 필요할 수는 없다(부모 ≤ 원래 값 ≤ 자식이므로 부모보다 작으면 자식보다도 작다) — 그래서 if/else 하나. pop 은 i = 0 이라 위가 없으므로 이 문제가 안 보인다.

</details>

<details>
<summary>Q3. Dijkstra 에 decrease-key 가 꼭 필요한가요? lazy deletion 과 비교해 보세요.</summary>

필요 없다. lazy: 개선될 때마다 중복 push, pop 때 `d > dist[v]` 면 버림. 시간 O(E log E) = O(E log V) (E ≤ V² 이므로 log E ≤ 2 log V) 로 점근적으로 같다. 차이는 **공간**: lazy 는 PQ 가 최대 O(E), indexed 는 O(V). 이 레슨 실측(V=1000, E=64000): 최대 PQ 3038 vs 912, 비교 79348 vs 21741, 시간 약 2배. 반대로 E 가 작을 땐 lazy 가 간접 참조가 없어 오히려 조금 빨랐다. 펌웨어는 최악 크기로 정적 할당해야 하므로 "상한 V" 가 결정적 장점이다. 꼬리질문 "Fibonacci heap?" → decrease-key amortized O(1) 로 Dijkstra O(E + V log V) 지만 상수가 크고 **amortized** 라 개별 호출의 최악이 길다 — 실시간 시스템과 실측 성능 모두에서 이진/4-ary 힙이 보통 이긴다.

</details>

<details>
<summary>Q4. ISR 에서 타이머를 재시작(예: UART 바이트 수신마다 idle 타임아웃 연장)해야 한다면?</summary>

선택지: ① ISR 이 `timer_arm` 을 직접 부르되 임계 구역(인터럽트 마스크)으로 감싼다 — 최악이 ⌊log₂ n⌋ 단계로 **상한이 있으므로** 정렬 리스트(O(n))보다 지연 예측이 쉽다. ② ISR 은 "재시작 요청(id, expire)" 을 SPSC 링 버퍼에 넣고 타이머 태스크가 힙을 단독 소유한다 — 락이 없고 가장 깔끔. ③ 아주 잦은 재시작이면 힙을 건드리지 않고 `last_rx_tick` 만 갱신하고, 만료 시점에 "정말 만료인가" 를 다시 확인해 재무장(lazy 재확인) — 힙 연산 횟수가 이벤트 수가 아니라 타임아웃 주기 수에 비례한다. 공통 주의: 콜백은 힙 락 밖에서 실행해야 콜백 안의 `timer_arm` 이 데드락이나 재진입 손상을 일으키지 않는다.

</details>

<details>
<summary>Q5. FreeRTOS / Zephyr / Linux 는 타이머 취소를 어떻게 O(1) 이나 O(log n) 으로 하나요?</summary>

공통점은 **노드를 타이머 객체 안에 내장(intrusive)** 해서 "찾기" 를 없앤다는 것 (DS-M01 참고). FreeRTOS `timers.c`: 타이머마다 `ListItem_t` 를 품고 만료 시각 순 정렬 리스트에 넣는다 — 삽입 O(n), 취소는 `uxListRemove()` O(1). Zephyr `kernel/timeout.c`: 각 timeout 이 앞 노드와의 **차이(delta tick)** 를 저장하는 delta list — 취소 시 다음 노드에 자기 delta 를 더해 주고 O(1) 로 빠진다. Linux: 저해상도 `timer_list` 는 계층 timer wheel (`kernel/time/timer.c`, 삽입·취소 O(1), DS-M06), 고해상도 `hrtimer` 는 red-black tree 기반 `timerqueue` — 삽입·취소 O(log n). 이진 힙 + 위치 맵은 libuv 류 사용자 공간 이벤트 루프에서 쓰는 방식과 같은 계열이다 (libuv `heap-inl.h` 는 배열 대신 부모/자식 포인터를 노드에 내장한 힙).

</details>

<details>
<summary>Q6. 4-ary 힙이 Dijkstra 에 좋다는데, 실제로도 그런가요?</summary>

d-ary 힙: 높이 log_d n → decrease/insert(sift-up) 는 짧아지고, pop(sift-down)은 단계당 자식 d 개를 비교하므로 비교가 d·log_d n 으로 늘어난다. decrease 가 pop 보다 훨씬 많을 때(E ≫ V, 많은 개선) 이득이고, 이론적 최적 d ≈ E/V. 연습 2 실측(V=256, 무작위 가중치)에선 decrease 가 499 번밖에 안 일어나 d=2 와 d=4 의 총 비교가 4103 vs 4111 로 사실상 같았고, d=8 은 5455 로 손해였다. 현실의 이득은 비교 횟수보다 **캐시**에서 온다: 키를 슬롯 배열에 같이 두면 4-ary 의 형제 4개가 한 캐시 라인에 들어간다. 이 레슨처럼 `key[heap[i]]` 간접 참조면 그 이점이 줄어든다 — 그래서 성능 중시 구현은 슬롯에 `(key, id)` 를 같이 저장하고 decrease 때 `pos` 로 슬롯 키를 고친다.

</details>

<details>
<summary>Q7. 타이머를 만료 후 슬롯을 재사용하는데, 옛 핸들로 cancel 이 들어오면?</summary>

ABA 문제: 슬롯 5 의 타이머 A 가 만료되고, 슬롯 5 를 받은 타이머 B 가 시작된 뒤, A 의 소유자가 늦게 `cancel(5)` 를 부르면 B 가 취소된다. 해결은 핸들 = `(세대 << 16) | 슬롯`, 슬롯을 내줄 때마다 세대 증가, cancel 은 세대가 같을 때만. 세대 0 은 건너뛰어 핸들 0 을 "무효" 로 예약한다. 16비트 세대는 65535 번 재사용 뒤 돌아오므로, 그보다 오래 붙잡힌 핸들은 이론상 충돌 가능 — 실무에선 충분하다고 보거나 세대 비트를 늘린다. 연습 3 이 이것을 구현·검증한다.

</details>

## 9. 추가 연습

### 연습 1 — O(n) 일괄 구성 (heapify + pos)

`(id, key)` 배열 n 개로 indexed heap 을 O(n) 에 세우라. 중복 id 는 거절하고 빈 상태로 둘 것. n = 1024 키 내림차순에서 n 번 insert 와 비교 횟수를 비교하라. (용도: 부팅 시 NVM 에 저장된 알람 목록 복원.)

<details>
<summary>답 보기</summary>

`heap[i] = ids[i]; pos[ids[i]] = i; key[ids[i]] = keys[i]` 로 채운 뒤 DS-B12 의 Floyd heapify 와 같이 마지막 내부 노드부터 sift_down. sift_down 이 swap 을 통해 pos 를 고치므로 추가 작업이 없다. d-ary 일반형의 마지막 내부 노드는 `(n - 2) / d` 이고, 루프는 `for (i = (n-2)/d + 1; i-- > 0;)`. 중복 검사는 채우는 도중 `pos[id] != NONE` 으로 한다.

```text
[1] n=1024 descending keys: 1024 inserts = 8204 cmp, build = 2028 cmp
```

build 는 2n 이하(2028 ≤ 2048), insert 는 내림차순 최악이라 Σ⌊log₂ i⌋ 근처 — DS-B12 의 측정(n = 1023 에서 8194 vs 2026)과 같은 그림이다.

</details>

### 연습 2 — d-ary indexed heap 과 Dijkstra 비교 횟수

자식 수 d 를 매개변수로 하는 indexed heap 을 만들고 (부모 `(i-1)/d`, 자식 `d·i+1 … d·i+d`), V = 256 그래프에서 E = 512 / 8192 일 때 d = 2, 4, 8 의 sift_up / sift_down 비교 횟수를 재라. 모든 d 의 `dist` 가 같아야 한다.

<details>
<summary>답 보기</summary>

```text
[2] V=256  E     d | decrease  sift_up cmp  sift_down cmp  total
[2]        512  2 |       16          284           1328   1612
[2]        512  4 |       16          243           1539   1782
[2]        512  8 |       16          226           2305   2531
[2]       8192  2 |      499         1298           2805   4103
[2]       8192  4 |      499          968           3143   4111
[2]       8192  8 |      499          877           4578   5455
```

예상대로 sift_up 비교는 d 가 클수록 줄고(1298 → 968 → 877), sift_down 은 늘었다. 그런데 무작위 가중치에선 decrease 가 V 에 비해 적어서(499) 총합은 d = 2 ≈ d = 4 < d = 8. "4-ary 가 빠르다" 는 비교 횟수가 아니라 캐시 효과와 decrease 가 많은 워크로드에서 나온다 (Q6). 이론을 믿기 전에 재 볼 것.

</details>

### 연습 3 — 세대 핸들로 ABA 안전한 취소

슬롯 64 개 타이머 풀을 만들라: `tm_start(expire)` 가 빈 슬롯을 받아 `(gen << 16) | slot` 핸들을 돌려주고, `tm_cancel(handle)` 은 세대가 맞을 때만 취소. 만료되면 슬롯을 반납한다. "h1 만료 → 같은 슬롯으로 h2 시작 → `cancel(h1)` 은 거절, h2 는 살아 있음" 을 확인하고, 발급된 모든 핸들 중 아무거나 취소하는 랜덤 테스트를 하라.

<details>
<summary>답 보기</summary>

free 슬롯 스택을 LIFO 로 두면 방금 반납한 슬롯이 바로 재사용되어 ABA 가 가장 잘 드러난다(테스트가 일부러 그렇게 만든다). 슬롯을 꺼낼 때 `++gen[s]`, 0 이 되면 1 로. cancel 은 `slot < N && gen != 0 && gen[slot] == gen && contains(slot)`. 랜덤 테스트는 핸들별 `alive` 를 참조 모델로 두고 `cancel` 결과가 그와 같은지, 매 스텝 `dcheck()` 와 `n + free 개수 == 64` 를 확인한다.

```text
[3] random start/cancel/expire: handles 6579, stale cancels rejected 6747 -> ok
```

6747 번의 "이미 만료됐거나 취소된 핸들" 취소가 모두 거절됐고, 그중 상당수는 슬롯이 이미 다른 타이머에 재사용된 상태였다.

</details>

### 연습 4 — k-way 병합의 replace-top

정렬된 스트림 16 개(하나는 비어 있음)를 병합하라. id = 스트림 번호, 키 = 그 스트림의 현재 머리 값. 루트를 출력한 뒤 pop + insert 대신 **루트의 키만 다음 값으로 바꾸는** 방식과 비교 횟수를 비교하라.

<details>
<summary>답 보기</summary>

스트림이 정렬돼 있으니 다음 값 ≥ 현재 값 → 루트에 `increase_key` 한 번(sift-down 한 번)이면 된다. pop + insert 는 sift-down(마지막 원소를 루트로) + sift-up 두 번이다. 스트림이 끝나면 `remove`. 결과를 전체 정렬과 비교한다.

```text
[4] merge 1829 values from 16 streams: replace-top 8196 cmp, pop+insert 10390 cmp
```

약 21 % 절감. 센서 여러 개의 타임스탬프 병합, LSM/FTL 의 정렬 런 병합(ALGO-A12 외부 정렬)에서 쓰는 패턴이고, Python `heapq.heapreplace` 가 같은 아이디어다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_asan && ./practice_asan
```

## 10. 핵심 정리

- indexed heap = `heap[slot] → id` + `pos[id] → slot` + `key[id]`. 불변식 `pos[heap[i]] == i` 를 **모든 swap 에서** 지킨다 — swap 함수 하나로 모으기.
- decrease_key → sift-up 만, increase_key → sift-down 만 (힙 속성에서 유도). 계약 위반은 거절.
- remove(id): 마지막으로 메우고 **부모보다 작으면 up, 아니면 down**. 마지막 슬롯이면 끝. pop 은 i = 0 인 특수형.
- 모든 연산 최악 ⌊log₂ n⌋ 단계, 메모리 id 당 8 B 고정, id 가 유일하니 용량 초과 불가.
- 타이머: arm = insert 또는 update (중복 엔트리 0), cancel = remove, 슬롯 재사용은 세대 핸들로 ABA 차단.
- Dijkstra: decrease-key 는 PQ ≤ V, lazy 는 PQ ≤ E+1. 실측 E=64000 에서 912 vs 3038 엔트리 — 펌웨어 RAM 예산에선 indexed.
- 검증 = 매 연산 `ipq_check()` + 선형 탐색 참조 모델 + 독립 알고리즘(O(V²) Dijkstra) 대조.

## 참고자료

- Sedgewick & Wayne, *Algorithms* 4th ed., §2.4 Priority Queues — `IndexMinPQ` (index priority queue)
- CLRS *Introduction to Algorithms*, Ch. 6.5 (HEAP-DECREASE-KEY), Ch. 24.3 Dijkstra, Ch. 19 Fibonacci heaps
- [Go `container/heap` — `Fix`, `Remove`, PriorityQueue 예제의 index 갱신](https://pkg.go.dev/container/heap)
- [libuv `src/heap-inl.h` — 포인터 내장형 min-heap 과 `heap_remove`](https://github.com/libuv/libuv/blob/v1.x/src/heap-inl.h)
- [FreeRTOS `timers.c` — 정렬 리스트 타이머와 O(1) 제거](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/timers.c)
- [Zephyr `kernel/timeout.c` — delta list 타임아웃](https://github.com/zephyrproject-rtos/zephyr/blob/main/kernel/timeout.c)
- [Linux `lib/timerqueue.c` — hrtimer 용 rbtree 타이머 큐](https://github.com/torvalds/linux/blob/master/lib/timerqueue.c)
- Chen, Chowdhury, Ramachandran, Lan Roche, Tong, "Priority Queues and Dijkstra's Algorithm", UT Austin TR-07-54 (2007) — 실측에서 decrease-key 없는 PQ 가 경쟁력 있음
