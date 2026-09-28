# C7. 힙과 top-k — 전부 정렬하지 않고 상위 k개 뽑기

> **이 노트를 읽고 나면**
> - 이진 힙을 배열 하나로 표현하고 sift-up / sift-down을 백지에서 써 내려갈 수 있다
> - "상위 k개"에 **min-heap**을 쓰는 이유를 O(N log k) vs O(N log N)로 설명하고, 추가 메모리 0으로 구현할 수 있다
> - 값이 계속 바뀌는데 힙을 매번 고칠 수 없을 때 쓰는 **lazy update** 기법을 불변식과 함께 증명할 수 있다
>
> **선행**: 없음. 배열 인덱스 계산과 while 루프만 알면 된다.
> 해시맵 쪽이 같이 나오는 문제라면 [C6. 해시맵 — 오픈 어드레싱](C6_hashmap_open_addressing.md)을 먼저 봐도 좋다.
>
> **이 개념을 쓰는 문제**: [09_wifi_scan_snapshot](../09_wifi_scan_snapshot/question_note) Part 2 (`wifi_top_k` — 가장 센 AP k개) ·
> [10_heartbeat_watchdog](../10_heartbeat_watchdog/question_note) Part 2 (`wd_next_deadline`, `wd_overdue` — 가장 먼저 만료되는 워커).

---

## 1. 왜 이게 필요한가

두 문제에서 막히는 지점은 모양이 똑같다. **"전부 보지 않고 제일 앞의 것만 알고 싶다."**

09번 — `size_t wifi_top_k(struct ApEntry *out, size_t k);` "The k strongest APs ...
strongest first." 테이블에 AP가 256칸 있고 roaming 로직은 보통 k=4나 8만 원한다. 전부
정렬하면 256 × log2(256) ≈ 2000번 비교인데 필요한 건 8개다. 게다가 **해시 테이블은 제자리
정렬을 할 수 없다** — 항목을 움직이면 probe chain이 깨진다(C6 참고). 그래서 N칸짜리 scratch
복사본이 필요하고, 256 × 48바이트 = 12 KB를 스택에 잡아야 한다.

10번 — `int wd_next_deadline(uint64_t *when_us);` "the earliest deadline among all
registered workers ... Lets the monitor sleep exactly that long instead of polling."
워커 32개의 "다음 만료 시각" 중 **최솟값** 하나만 알면 monitor가 정확히 그때까지 잘 수 있다.
매 tick마다 32개를 다 훑으면 O(N)이고, tick 주기를 임의로 정해야 한다.

둘 다 "최솟값을 싸게 꺼내는 자료구조"를 원한다. 그게 힙이다.

그리고 10번에는 한 겹이 더 있다. **heartbeat는 값을 계속 바꾸지만 힙을 건드릴 수 없다.**
`wd_heartbeat()` 는 워커의 hot path이고 wait-free여야 하므로 락을 잡을 수 없는데,
힙을 고치려면 락이 필요하다. 이 모순을 푸는 기법이 §5의 lazy update다.

---

## 2. 그림으로 먼저

이진 힙은 **완전 이진 트리**지만 **포인터가 하나도 없는 배열**이다. 인덱스 산수가 트리 구조를
대신한다.

```svg
<svg viewBox="0 0 660 300" role="img" aria-label="이진 힙의 트리 모양과 배열 인덱스 대응">
  <text class="lbl" x="10" y="18">트리로 본 모습 (부모는 자식보다 작다 = min-heap)</text>

  <circle class="fill-soft" cx="330" cy="46" r="20"/>
  <text x="330" y="51" text-anchor="middle">3</text>
  <text class="lbl" x="330" y="22" text-anchor="middle">i=0</text>

  <circle class="box" cx="210" cy="120" r="20"/>
  <text x="210" y="125" text-anchor="middle">7</text>
  <text class="lbl" x="176" y="118">i=1</text>
  <circle class="box" cx="450" cy="120" r="20"/>
  <text x="450" y="125" text-anchor="middle">5</text>
  <text class="lbl" x="478" y="118">i=2</text>

  <circle class="box" cx="150" cy="196" r="20"/>
  <text x="150" y="201" text-anchor="middle">9</text>
  <text class="lbl" x="118" y="194">i=3</text>
  <circle class="box" cx="270" cy="196" r="20"/>
  <text x="270" y="201" text-anchor="middle">8</text>
  <text class="lbl" x="296" y="194">i=4</text>
  <circle class="box" cx="390" cy="196" r="20"/>
  <text x="390" y="201" text-anchor="middle">6</text>
  <text class="lbl" x="358" y="194">i=5</text>

  <line class="muted" x1="315" y1="60" x2="225" y2="106"/>
  <line class="muted" x1="345" y1="60" x2="435" y2="106"/>
  <line class="muted" x1="197" y1="136" x2="163" y2="180"/>
  <line class="muted" x1="223" y1="136" x2="257" y2="180"/>
  <line class="muted" x1="437" y1="136" x2="403" y2="180"/>

  <text class="lbl" x="10" y="245">같은 것을 배열로</text>
  <rect class="fill-soft" x="60"  y="255" width="60" height="32" rx="5"/>
  <rect class="box" x="120" y="255" width="60" height="32" rx="5"/>
  <rect class="box" x="180" y="255" width="60" height="32" rx="5"/>
  <rect class="box" x="240" y="255" width="60" height="32" rx="5"/>
  <rect class="box" x="300" y="255" width="60" height="32" rx="5"/>
  <rect class="box" x="360" y="255" width="60" height="32" rx="5"/>
  <text x="90"  y="277" text-anchor="middle">3</text>
  <text x="150" y="277" text-anchor="middle">7</text>
  <text x="210" y="277" text-anchor="middle">5</text>
  <text x="270" y="277" text-anchor="middle">9</text>
  <text x="330" y="277" text-anchor="middle">8</text>
  <text x="390" y="277" text-anchor="middle">6</text>
  <text class="lbl" x="440" y="277">n = 6</text>

  <text class="lbl" x="60" y="248">0</text>
  <text class="lbl" x="120" y="248">1</text>
  <text class="lbl" x="180" y="248">2</text>
  <text class="lbl" x="240" y="248">3</text>
  <text class="lbl" x="300" y="248">4</text>
  <text class="lbl" x="360" y="248">5</text>

  <text class="lbl" x="470" y="250">부모: (i-1)/2</text>
  <text class="lbl" x="470" y="268">왼쪽 자식: 2i+1</text>
  <text class="lbl" x="470" y="286">오른쪽 자식: 2i+2</text>
</svg>
```

인덱스 산수를 손으로 한 번 확인한다. `i=4` (값 8)의 부모는 `(4-1)/2 = 1` (값 7)이다.
`i=1` 의 자식은 `2*1+1 = 3` 과 `2*1+2 = 4` — 값 9와 8. 전부 맞는다.

**힙은 정렬된 배열이 아니다.** 위 배열 `3 7 5 9 8 6` 은 정렬돼 있지 않다.
힙이 보장하는 것은 딱 하나: **부모 <= 자식**. 그래서 `h[0]` 이 전체 최솟값이라는 것만
공짜로 알 수 있고, 2등이 어디 있는지는 모른다(자식 둘 중 하나다).

---

## 3. 개념 (용어를 하나씩)

### 완전 이진 트리 (complete binary tree)

**정의**: 마지막 레벨만 오른쪽이 비어 있을 수 있고, 나머지는 빈틈없이 꽉 찬 이진 트리.

**왜 중요하나**: 빈틈이 없으니 **레벨 순서로 번호를 붙이면 0..n-1 이 연속**이다. 배열
하나에 그대로 담기고 포인터가 필요 없다. 높이는 항상 `floor(log2 n)` — 한쪽으로 기울어질 수
없으므로 이진 탐색 트리와 달리 **최악 케이스가 없다.**

### 힙 성질 (heap property)

**정의**: min-heap이면 모든 i에 대해 `h[parent(i)] <= h[i]`. max-heap이면 `>=`.

**왜 존재하나**: 이것만 유지하면 최솟값이 항상 `h[0]` 이다. 전체 정렬보다 훨씬 약한
조건이라 유지 비용이 싸다 — 삽입/삭제 한 번에 log n 번 비교면 된다.

### sift-up (위로 올리기)

**정의**: 새 값을 배열 끝에 붙이고, 부모보다 작으면 부모와 swap하며 위로 올라간다.

**왜 맞나**: 새 값 하나만 규칙을 어기고 있고, 그 값이 올라가는 경로만 고치면 다른 곳은
건드리지 않는다. 경로 길이 = 트리 높이 = log n.

### sift-down (아래로 내리기)

**정의**: 루트를 마지막 원소로 대체한 뒤, **두 자식 중 더 작은 쪽**과 비교해 자식이 더
작으면 swap하며 내려간다.

**왜 두 자식을 다 봐야 하나**: 한쪽만 보고 내려가면 안 본 자식이 부모보다 작을 수 있다.
7개 원소 힙에서 pop을 한 번 했을 때, 오른쪽 자식을 빼먹은 코드와 올바른 코드의 실제 결과다.

```
(2) heap before pop : 1 5 2 9 7 3 4
    buggy sift_down  : 4 5 2 9 7 3   heap property broken at index 2
    correct sift_down: 2 5 3 9 7 4   violation index 0 (0 = OK)
```

버그 버전은 루트가 4가 되었다. 힙 안에 2가 있는데 최솟값이 4라고 답한다. 증상이 조용해서
찾기 어렵다 — 크래시도 없고, 작은 입력에서는 우연히 맞기도 한다.

### push / pop 이 왜 O(log n)인가

높이가 `log2 n` 이고 sift-up/sift-down은 **한 레벨에 상수 번 비교**하며 한 방향으로만
움직인다. n=32면 높이 5, n=1024면 10, n=100만이면 20이다.

| 연산 | 비용 | 하는 일 |
|---|---|---|
| peek (최솟값 보기) | O(1) | `h[0]` 읽기 |
| push | O(log n) | 끝에 붙이고 sift-up |
| pop | O(log n) | `h[0]` 를 꺼내고 마지막 원소를 루트로, sift-down |
| 임의 위치 삭제 | O(log n) | 위치를 **알고 있다면**. 마지막 원소로 덮고 sift-down + sift-up |
| 임의 위치 검색 | O(n) | 힙은 검색 자료구조가 아니다. 위치를 별도로 기억해야 한다 |

마지막 두 줄이 10번 문제의 설계를 설명한다. 워커를 unregister 할 때 힙에서 찾아 지워야
하는데 검색이 O(n)이면 소용없다. 그래서 `g_pos[id] = 힙 인덱스` 배열을 따로 둔다.

```c
/* 10_heartbeat_watchdog/watchdog_solution.c */
static int g_pos[WD_MAX_WORKERS];      /* id -> heap index, -1 = absent */

static void heap_swap(int a, int b)
{
    wd_node_t t = g_heap[a];
    g_heap[a] = g_heap[b];
    g_heap[b] = t;
    g_pos[g_heap[a].id] = a;
    g_pos[g_heap[b].id] = b;
}
```

swap할 때마다 `g_pos` 를 같이 갱신하는 게 전부다. **이 두 줄을 빼면** unregister가 엉뚱한
슬롯을 지우고 힙과 `g_pos` 가 영원히 어긋난다.

### 왜 top-k에 min-heap인가 (직관이 반대로 가는 부분)

"제일 센 것 k개"를 원하니 max-heap이 맞을 것 같다. 틀렸다. **min-heap을 쓴다.**

이유: 우리가 유지하는 것은 "현재까지의 상위 k개"라는 **집합**이고, 새 후보가 들어올 때
알아야 하는 것은 **그 집합에서 가장 약한 놈**이다. 새 후보가 그놈보다도 약하면 볼 필요 없이
버린다. 그놈보다 세면 그놈을 쫓아내고 들어온다. "가장 약한 놈"을 O(1)로 보려면
**약한 게 위로 오는 힙** = min-heap이다.

```
            들어온 AP: -55 dBm
                 │
                 ▼
        out[0] = -70  ← 크기 k 힙의 루트 = 현재 살아남은 것 중 가장 약한 AP
                 │
      -55 > -70 ?  ── 아니오 ──▶ 비교 1번으로 끝. 힙은 그대로.   (대부분이 여기)
                 │
                예
                 ▼
      out[0] = -55  로 덮고 sift_down  (log k 번 비교)
```

핵심: **N개 중 대부분은 비교 1번으로 탈락한다.** 힙이 log k 번 흔들리는 건 "현재 상위 k에
들어올 만한 놈"이 왔을 때뿐이고, 랜덤 순서라면 그런 일은 약 `k * ln(N/k)` 번이다. 그래서
총 비용이 **O(N + k log k · log(N/k))** ≈ O(N log k) 다.

| 방법 | 시간 | 추가 메모리 | N=256, k=8 비교 횟수 | N=10000, k=8 (실측) |
|---|---|---|---|---|
| 전부 정렬하고 앞 k개 | O(N log N) | scratch 복사본 N개 | 약 2000 | **136801** |
| 크기 k min-heap | O(N log k) | **0** (호출자 버퍼 사용) | 약 800 | **10115** |
| 매번 최댓값 찾기 k번 | O(N·k) | 0 | 2048 | 80000 |
| 부분 정렬(선택 정렬 k번) | O(N·k) | scratch N개 | 2048 | 80000 |

N=10000, k=8에서 **13.5배** 차이고, k가 작을수록·N이 클수록 격차가 벌어진다. 메모리 차이가
더 중요할 수도 있다 — `struct ApEntry` 가 48바이트라 N=256 복사는 12 KB 스택이고,
임베디드 스레드 스택이 8 KB면 그 자체로 crash다.

### 동점 처리 (tie-break)

**정의**: 정렬 키가 완전히 같은 두 항목의 순서를 정하는 규칙.

**왜 문서화해야 하나**: 힙은 **불안정(unstable)** 하다. swap이 원래 순서를 보존하지 않으므로
동점인 두 AP 중 어느 것이 위로 올지는 입력 순서와 힙 내부 상태에 달렸고, 스캔마다 결과가
달라진다. roaming이 똑같이 센 AP 두 개를 매 스캔 왕복하면 **버그 리포트**가 된다.

그래서 09번은 헤더에 규칙을 박아 놓았다.

```c
/* 09_wifi_scan_snapshot/ap_table.h */
/* Order: by rssi_smoothed descending; exact ties broken by BSSID
 * ascending (memcmp), so the result is deterministic. */
```

규칙이 **전순서(total order)** 여야 한다. BSSID는 유일하므로 모든 동점이 깨진다.
"먼저 본 것 우선"은 힙에서 구현할 수 없다(순서 정보가 없다) — 원하면 항목에 sequence
번호를 넣어야 한다.

### 힙 대신 쓸 수 있는 것

| 방법 | peek | 삽입 | 삭제 | 언제 더 나은가 |
|---|---|---|---|---|
| 정렬된 배열 | O(1) | O(n) memmove | O(n) | n이 아주 작고(8 이하) 삽입이 드물 때. 코드가 10줄 |
| 힙 | O(1) | O(log n) | O(log n) | 기본값. n이 중간(수십~수만), 정확한 최솟값이 필요할 때 |
| 타이머 휠 (timer wheel) | O(1) amortized | O(1) | O(1) | 타이머가 수천 개, 계속 재무장되고, tick 단위 오차를 허용할 때 |
| 정렬 안 한 배열 전체 스캔 | O(n) | O(1) | O(1) | n <= 32이고 peek이 드물 때. 10번도 사실 이걸로 통과한다 |

타이머 휠은 `deadline / tick` 으로 버킷을 잡는 해시 배열이다. 리눅스 커널의 `timer_list`,
네트워크 스택의 재전송 타이머가 이 방식이다. **heartbeat야말로 "계속 재무장되는 타이머"이므로
타이머 휠의 교과서적 사례다.** 그런데 10번이 힙을 고른 이유는 세 가지다.

- `n <= 32` — 휠의 상수 오버헤드(버킷 메모리, cascading)가 log 32 = 5보다 비싸다.
- **정확한** 다음 만료 시각이 필요하다(monitor가 딱 그때까지 자야 한다). 휠은 tick 단위로
  양자화되고 "정확히 다음 만료가 언제냐"에 답하려면 버킷들을 걸어야 한다.
- lazy update 덕분에 재무장이 **공짜**다. 휠의 최대 장점이 필요 없어진다.

정렬 배열은 peek이 O(1)이지만 삽입이 O(n)이고, heartbeat가 잦은 이 문제에서는 최악이다.

---

## 4. 코드로 보기

09번의 힙은 40줄이 안 된다. 먼저 순서 규칙, 그 다음 sift 두 개.

```c
/* 09_wifi_scan_snapshot/ap_table_solution.c */
/* "weaker" 가 이 힙의 순서다. 동점이면 BSSID 작은 쪽이 이긴다(= 큰 쪽이 weaker). */
static bool weaker(const struct ApEntry *a, const struct ApEntry *b)
{
    if (a->rssi_smoothed != b->rssi_smoothed)
        return a->rssi_smoothed < b->rssi_smoothed;
    return memcmp(a->bssid, b->bssid, 6) > 0;
}

static void heap_up(struct ApEntry *h, size_t i)
{
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (!weaker(&h[i], &h[p]))
            return;
        heap_swap(&h[i], &h[p]);
        i = p;
    }
}

static void heap_down(struct ApEntry *h, size_t m, size_t i)
{
    for (;;) {
        size_t l = 2 * i + 1, r = l + 1, w = i;
        if (l < m && weaker(&h[l], &h[w])) w = l;
        if (r < m && weaker(&h[r], &h[w])) w = r;
        if (w == i)
            return;
        heap_swap(&h[i], &h[w]);
        i = w;
    }
}
```

한 줄씩.

- `weaker()` 를 **함수 하나로 뽑는다.** 비교가 나오는 곳은 세 군데(up, down의 두 자식)이고,
  세 곳이 미세하게 다르면 힙이 조용히 깨진다.
- `if (!weaker(&h[i], &h[p])) return;` — `<` 가 아니라 `!weaker` 다. 동점에 swap하지 않고
  멈춘다. 동점에 swap하면 비일관 비교 함수와 겹칠 때 무한 루프가 될 수 있다.
- `w = i` 로 시작해 두 자식과 차례로 비교하고 **마지막에 한 번만 swap**한다. 비교마다
  swap하면 비교 횟수가 늘고 경로가 틀어진다.
- `l < m`, `r < m` 경계 검사. `m` 은 **힙의 크기**이고 배열 용량이 아니다. 없으면 힙 밖의
  쓰레기와 비교한다.
- `for (;;)` — 재귀가 아니다. 최악 스택 깊이를 못 박고 싶으면 루프가 답이다.

그리고 top-k 본체. **호출자 버퍼 `out` 이 곧 힙이다.**

```c
/* 09_wifi_scan_snapshot/ap_table_solution.c — wifi_top_k() 핵심 */
for (uint32_t j = 0; j < AP_TABLE_CAP; j++) {
    const ap_slot_t *s = &g_tab.slot[j];
    if (!s->used || entry_stale(&s->e, now))
        continue;
    if (m < k) {
        out[m] = s->e;                /* 힙을 채우는 중 */
        heap_up(out, m);
        m++;
    } else if (weaker(&out[0], &s->e)) {
        out[0] = s->e;                /* 가장 약한 생존자를 이겼다 */
        heap_down(out, m, 0);
    }
    /* else: 힙 전체보다 약하다 — 비교 한 번으로 끝. 이게 O(N log k)의 정체다. */
}
```

- `out` 은 **호출자가 준 k칸 버퍼**다. 힙을 거기서 굴리므로 추가 메모리가 0이다 — malloc도,
  큰 로컬 배열도 없다. "결과를 담을 곳을 호출자가 준다"를 작업 공간으로도 쓰는 관용구다.
- `m < k` 동안은 채우고 sift-up. `m == k` 가 되면 루트가 "가장 약한 생존자"다.
- 반환 직전에 손질이 한 번 더 필요하다. **힙은 부분 정렬**이라 `out` 이 아직 "센 것부터"가 아니다.

```c
/* 힙은 부분 정렬일 뿐이므로, 락을 놓은 뒤 k개만 삽입 정렬한다 */
for (size_t i = 1; i < m; i++) {
    struct ApEntry v = out[i];
    size_t j = i;
    while (j > 0 && weaker(&out[j - 1], &v)) {
        out[j] = out[j - 1];
        j--;
    }
    out[j] = v;
}
```

k가 8이면 삽입 정렬이 최고다(O(k^2) = 64, 상수가 작다). 그리고 이 루프는 **락 밖에서**
돈다 — `out` 은 호출자 전용이라 다른 스레드가 볼 일이 없다. 락 안에서 정렬하면 이유 없이
임계 구역이 길어진다.

---

## 5. 단계별로 만들어 보기

### top-k: v0 → v1 → v2

**v0 (동작하지만 비싸다)**: 전부 복사해서 qsort 하고 앞 k개.

```c
/* v0: 되긴 된다 */
struct ApEntry tmp[AP_TABLE_CAP];      /* 256 * 48B = 12 KB 스택! */
size_t n = collect_live(tmp);
qsort(tmp, n, sizeof tmp[0], cmp_strong);
memcpy(out, tmp, (n < k ? n : k) * sizeof *out);
```

무엇이 문제인가: 12 KB 스택(스레드 스택이 8 KB면 즉시 터진다), 비교 136801번(실측),
그리고 9등부터 10000등까지의 순서를 알아내고 버리는 낭비.

**v1 (흔한 오답)**: 크기 k **max**-heap을 쓴다.

무엇이 문제인가: max-heap의 루트는 "지금까지 본 것 중 가장 센 것"이다. 새 후보를 볼 때
필요한 정보는 그게 아니라 **"내 k개 중 가장 약한 것"** 이고, max-heap에서 그걸 찾으려면
리프를 다 훑어야 한다(O(k)). 그러면 전체가 O(N·k)로 퇴화한다. 방향이 반대다.

**v2 (정답)**: 크기 k **min**-heap, 호출자 버퍼 안에서. §4의 코드다.
경계 조건 세 개를 반드시 처리한다: `k == 0`, `n < k`(가능한 만큼만 채우고 그 수를 반환),
`k > 실제 항목 수`. 실측:

```
test2 top-8 of N=10000 : mismatches=0  compares heap=10115  compares full sort=136801  (13.5x)
test2 edges      : n=3,k=8 -> 3   n=0,k=8 -> 0
```

### lazy update 힙: v0 → v1 → v2

이제 10번이다. 상황을 다시 정리한다.

- `wd_heartbeat(id)` 는 워커의 hot path에서 불리고 **wait-free**여야 한다 — 락 금지.
- monitor는 "가장 먼저 만료되는 워커"를 알아야 한다.
- heartbeat가 오면 그 워커의 만료 시각 = `last_seen + deadline` 이 **미래로 밀린다**.
  즉 힙의 키가 계속 바뀐다.

**v0 (틀림)**: heartbeat에서 힙을 고친다.

```c
/* v0: 절대 이렇게 하면 안 된다 */
void wd_heartbeat(uint32_t id)
{
    pthread_mutex_lock(&g_lock);              /* ← hot path에 락 */
    g_heap[g_pos[id]].key = hwwdt_now_us() + g_cold[id].deadline_us;
    sift_down(g_pos[id]);
    pthread_mutex_unlock(&g_lock);
}
```

무엇이 틀렸나: 요구사항 위반이다. 32개 워커가 같은 락과 같은 캐시 라인에 줄을 선다.
더 나쁜 것 — 워커가 **watchdog 안에서 블록될 수 있다.** 살아 있는지 재는 대상이 재는 장치
때문에 멈춘다. 락 보유자가 선점되면 함대 전체가 멈춘다. priority inversion의 교과서적 예다.

**v1 (되지만 아깝다)**: heartbeat는 원자적 store만 하고, monitor가 매 tick 32개를 다 훑는다.

```c
void wd_heartbeat(uint32_t id)   /* 이 부분은 v2에서도 그대로 유지된다 */
{
    if (id >= WD_MAX_WORKERS) return;
    atomic_store_explicit(&g_hot[id].last_seen_us, hwwdt_now_us(), memory_order_release);
}
```

무엇이 아까운가: tick마다 O(N). N=32면 사실 이게 제일 좋은 답이고 면접에서 그렇게 말해도
된다. 하지만 "monitor가 **정확히** 다음 만료까지 잔다"를 구현하려면 최솟값이 필요하고,
N이 커지면(수천 개 세션의 타임아웃) O(N)이 부담이 된다.

**v2 (lazy update)**: 힙의 키를 **"하한(lower bound)"** 으로 해석한다.

핵심 아이디어 한 줄: `last_seen` 은 앞으로만 간다(단조 증가). 따라서 진짜 만료 시각
`true = last_seen + deadline` 도 앞으로만 간다. 힙에 넣어 둔 키는 **과거 어느 시점의 진짜
만료 시각**이므로, 지금의 진짜 값보다 작거나 같다.

```
불변식 (INVARIANT):   모든 힙 항목 i에 대해   key_i <= true_i
```

즉 키는 **한 방향으로만 틀린다.** 실제보다 이르게 말할 뿐, 늦게 말하지 않는다.
이 한 방향성이 모든 것을 가능하게 한다. **루트만 고치면 정확한 전역 최솟값이 나온다.**

```svg
<svg viewBox="0 0 660 260" role="img" aria-label="lazy update: 루트만 고쳐서 정확한 최솟값을 얻는다">
  <text class="lbl" x="10" y="16">heartbeat는 힙을 건드리지 않는다. 키는 '하한'이고, 진짜 값은 그보다 뒤에 있다.</text>

  <rect class="fill-soft" x="40" y="34" width="120" height="34" rx="6"/>
  <text x="100" y="56" text-anchor="middle">root key=100</text>
  <line class="dash" x1="160" y1="51" x2="300" y2="51"/>
  <text class="lbl" x="230" y="44" text-anchor="middle">진짜는 420</text>
  <rect class="box" x="300" y="34" width="120" height="34" rx="6"/>
  <text x="360" y="56" text-anchor="middle">true=420</text>
  <text class="lbl" x="440" y="56">heartbeat가 밀어냈다</text>

  <rect class="box" x="40" y="92" width="120" height="34" rx="6"/>
  <text x="100" y="114" text-anchor="middle">key=250</text>
  <line class="dash" x1="160" y1="109" x2="230" y2="109"/>
  <rect class="box" x="230" y="92" width="120" height="34" rx="6"/>
  <text x="290" y="114" text-anchor="middle">true=260</text>

  <rect class="box" x="40" y="150" width="120" height="34" rx="6"/>
  <text x="100" y="172" text-anchor="middle">key=310</text>
  <line class="dash" x1="160" y1="167" x2="290" y2="167"/>
  <rect class="box" x="290" y="150" width="120" height="34" rx="6"/>
  <text x="350" y="172" text-anchor="middle">true=390</text>

  <line class="accent" x1="180" y1="206" x2="600" y2="206"/>
  <polygon class="accent" points="600,206 592,202 592,210"/>
  <text class="lbl" x="20" y="210">루트 수리:</text>
  <text class="lbl" x="200" y="228">root.key = 420 으로 갱신 → sift_down → 새 루트 key=250</text>
  <text class="lbl" x="200" y="246">250 == true(250의 주인) 이므로 여기서 멈춘다 → 정확한 최솟값 250</text>
</svg>
```

코드는 짧다.

```c
/* 10_heartbeat_watchdog/watchdog_solution.c */
static bool heap_refresh_root(uint64_t now, uint64_t *out_key, uint32_t *out_id)
{
    while (g_heap_n > 0) {
        uint32_t id = g_heap[0].id;
        uint64_t truth = true_deadline(id);
        /* (살아 돌아온 워커의 faulted 플래그를 여기서 지운다 — 원문 참고) */
        if (truth <= g_heap[0].key) {                   /* key is exact */
            *out_key = g_heap[0].key;
            *out_id  = id;
            return true;
        }
        g_heap[0].key = truth;                          /* keys only grow ... */
        sift_down(0);                                   /* ... so: sift down  */
    }
    return false;
}
```

**증명 한 문단.** 불변식에서 모든 i에 대해 `key_i <= true_i` 다. 루프는 루트의 키가
진짜 값보다 작을 때만 돌고, 그때 키를 진짜 값으로 올린다 — 키는 커지기만 하므로 sift_up이
아니라 sift_down만 필요하다(sift_up은 절대 움직이지 않는다). 매 반복에서 어떤 항목의 키가
그 항목의 정확한 값으로 확정되고, 그 값은 다음 heartbeat까지 다시 틀려지지 않으므로 반복
횟수는 항목 수로 유한하다. 루프가 멈춘 순간 `key_root == true_root` 다. 다른 모든 항목 i는
힙 성질로 `key_i >= key_root` 이고 불변식으로 `true_i >= key_i` 이므로
`true_i >= key_root == true_root`. 따라서 **루트가 전역 최솟값이다** — 나머지 31개를
쳐다보지도 않고. 수리 비용은 한 번에 O(log n)이고, 수리는 heartbeat 한 번에 최대 한 번만
필요하므로 amortized 비용이 아주 작다. 실측으로 10000 라운드에 수리 1637번 =
**라운드당 0.16번**이었다.

같은 불변식이 `wd_overdue()` 의 **가지치기(pruning)** 도 정당화한다. `key_i > now` 인
항목은 `true_i >= key_i > now` 이므로 건강하고, 힙 성질 때문에 그 자식들의 키는 더 크다 —
**그 서브트리 전체를 건너뛸 수 있다.**

```c
/* 10_heartbeat_watchdog/watchdog_solution.c — wd_overdue() 중 */
if (g_heap[i].key > now) continue;              /* prune subtree */
uint32_t id = g_heap[i].id;
if (true_deadline(id) <= now) {                 /* stale keys filtered */
    if (n < max) ids_out[n] = id;
    n++;
}
```

`true_deadline(id) <= now` 를 한 번 더 확인하는 이유: 키는 하한이라서 "키는 과거인데 실제로는
건강한" 항목이 있을 수 있다. 이 줄이 없으면 살아 있는 워커를 죽었다고 보고하고,
monitor가 kick을 멈추고 **보드가 리셋된다.**

---

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
|---|---|---|---|
| 0-based / 1-based 혼동 (`parent = i/2` 를 0-based에 씀) | 루트가 자기 부모가 되어 무한 루프, 또는 힙 성질이 조용히 깨짐 | 0-based는 `(i-1)/2`, `2i+1`, `2i+2`. 1-based는 `i/2`, `2i`, `2i+1` | 하나를 정하고 주석에 박는다. 0-based + `(i-1)/2` 를 쓰고 `i > 0` 을 루프 조건으로 |
| sift-down에서 오른쪽 자식을 빼먹음 | pop이 최솟값이 아닌 값을 돌려준다. 크래시 없음, 작은 입력에선 우연히 맞음 | 왼쪽만 보고 내려가면 안 본 자식이 부모보다 작을 수 있다 | `w=i` 로 시작해 `l`, `r` 둘 다 비교하고 마지막에 한 번만 swap |
| 동점에서 swap (`<=` 대신 `<` 를 잘못 씀) | 결과가 실행마다 달라진다. 최악에는 무한 루프 | 힙은 unstable하고, 비일관 비교 함수와 겹치면 순환한다 | 비교를 `weaker()` 함수 하나로 뽑고, `if (!weaker(child, parent)) return;` 로 동점에 멈춘다 |
| tie-break 규칙을 문서화하지 않음 | roaming이 똑같이 센 AP 두 개를 매 스캔 왕복한다 | 힙 순서는 입력 순서에 의존하므로 재현 불가능 | 유일 키(BSSID 등)로 전순서를 만들고 헤더에 적는다 |
| top-k에 max-heap | O(N·k)로 퇴화 | max-heap 루트는 "가장 센 것"인데 필요한 것은 "내 k개 중 가장 약한 것" | min-heap. 루트가 컷라인이 된다 |
| 크기 k 힙에서 `m`(현재 크기) 대신 배열 용량을 경계로 씀 | 초기화 안 된 쓰레기와 비교, 결과에 쓰레기가 섞임 | `heap_down(h, m, i)` 의 `m` 은 **원소 개수**다 | `l < m`, `r < m` 으로만 경계 검사. `k` 나 `CAP` 을 쓰지 않는다 |
| 힙에서 항목을 지우려고 선형 검색 | unregister가 O(n), 그리고 잘못된 슬롯을 지운다 | 힙은 검색 자료구조가 아니다 | `g_pos[id] = 힙 인덱스` 를 유지하고 **swap마다 갱신**한다 |
| lazy 힙에서 키가 줄어들 수 있게 만듦 | 최솟값이 틀린다. 죽은 워커를 놓친다 | 하한 불변식(`key <= true`)이 깨지면 루트 수리 논증이 무너진다 | 키를 만드는 값이 단조 증가임을 보장한다. 줄어들 수 있으면 sift_up도 해야 하고, 그러면 전체 스캔이 필요하다 |
| 힙의 루트만 보고 "정렬됐다"고 가정 | `out[1]` 부터 순서가 엉망 | 힙은 부분 정렬. 형제 사이 순서는 보장이 없다 | 반환 전에 k개를 삽입 정렬 (락 밖에서) |

---

## 7. 손으로 확인하기

```sh
cd 09_wifi_scan_snapshot && ./main.sh sol     # top-k 가 brute force 와 일치하는지 검사한다
cd 10_heartbeat_watchdog && ./main.sh sol     # wd_next_deadline 을 전체 스캔과 비교한다
```

10번의 question_note가 하네스 방식을 밝혀 놓았다 — 이게 이 노트가 권하는 테스트 방식이다.

> `wd_next_deadline()` is compared with a brute-force minimum computed by scanning every worker.

같은 것을 임시 드라이버로 직접 해 본다. 세 가지를 검사했다.

```c
/* 참조: 그냥 전부 훑는다. 느리지만 틀릴 수 없다. */
uint64_t bf = UINT64_MAX; bool any = false;
for (uint32_t i = 0; i < W; i++)
    if (in_use[i]) { any = true; if (truth(i) < bf) bf = truth(i); }
```

그리고 매 라운드 **불변식과 힙 성질도 같이 검사한다.** 이게 lazy 힙 디버깅의 핵심이다.

```c
for (int i = 0; i < hp_n; i++)                      /* 하한 불변식 */
    if (hp[i].key > truth(hp[i].id)) { bad++; break; }
for (int i = 1; i < hp_n; i++)                      /* 힙 성질 */
    if (hp[(i - 1) / 2].key > hp[i].key) { bad++; break; }
```

실제 출력이다.

```
test1 heap push x10000 then pop x10000 vs qsort : mismatches=0  (heap empty=yes)
test2 top-8 of N=10000 : mismatches=0  compares heap=10115  compares full sort=136801  (13.5x)
test2 edges      : n=3,k=8 -> 3   n=0,k=8 -> 0
test3 lazy heap  : rounds=10000 heartbeats=6098 checks=10000 mismatches=0  root repairs=1637 (0.16 per round)
ALL OK
```

읽는 법:

- `test1` — 10000개를 push하고 전부 pop하면 정렬된 순서가 나온다. qsort 결과와 한 칸도
  다르지 않았고, pop을 다 하면 힙이 정확히 비었다. 힙 구현이 맞는지 보는 가장 싼 검사다.
- `test2` — top-8 결과 8개가 전체 정렬 결과의 앞 8개와 **동점 순서까지** 일치했다.
  동점을 일부러 많이 만들었다(점수를 0.5 단위 40단계로만 줬다) — tie-break 규칙이 없으면
  여기서 바로 깨진다.
- `test3` — 32칸 워커, 70% heartbeat / 15% register / 15% unregister로 10000 라운드.
  매 라운드 lazy 힙의 답과 전체 스캔의 답이 일치했고, 불변식과 힙 성질도 매 라운드 성립했다.
  루트 수리는 라운드당 0.16번.

- [ ] 09번 `ap_table.c` 의 `wifi_top_k` 를 **주석 없는 빈 파일에서** 작성하고 `./main.sh` 통과
- [ ] `heap_down` 에서 `r` 비교 줄을 지우고 테스트가 어떻게 깨지는지 본다
- [ ] 10번 `watchdog.c` 의 힙을 `g_pos` 없이 만들어 보고 unregister가 왜 O(n)인지 체감
- [ ] `heap_refresh_root` 의 `sift_down(0)` 을 `sift_up(0)` 으로 바꿔 보고 최솟값이 틀리는 것 확인

---

## 8. 자가 점검

```check
Q: 0-based 배열에서 인덱스 i의 부모와 두 자식은? 1-based와 뭐가 다른가?
A: 0-based는 부모 `(i-1)/2`, 왼쪽 자식 `2i+1`, 오른쪽 자식 `2i+2`. 1-based는 부모 `i/2`,
자식 `2i`와 `2i+1`. 섞으면 루트가 자기 부모가 되어(0-based에서 `0/2 = 0`) 무한 루프가 나거나
힙 성질이 조용히 깨진다. 하나를 정해서 주석에 박고, sift-up 루프 조건을 `i > 0` 으로 둔다.

Q: "제일 센 k개"를 원하는데 왜 min-heap인가? max-heap이면 무엇이 안 되나?
A: 유지하는 것은 "현재까지의 상위 k개"라는 집합이고, 새 후보가 올 때 필요한 정보는
"그 집합에서 가장 약한 놈"이다. 그놈보다 약하면 비교 한 번으로 버린다. min-heap의 루트가
바로 그놈이므로 O(1)에 컷라인을 본다. max-heap에서는 가장 약한 놈을 찾으려고 리프를 다
훑어야 해서 O(k)가 붙고, 전체가 O(N·k)로 퇴화한다.

Q: 크기 k min-heap의 top-k가 왜 O(N log k)인가? 실제로 얼마나 빠른가?
A: N개 중 대부분은 루트와의 비교 한 번으로 탈락한다. 힙이 log k 번 흔들리는 것은 상위 k에
들어올 만한 후보가 왔을 때뿐이고, 랜덤 순서면 그런 일은 약 `k·ln(N/k)` 번이다. 그래서
전체가 O(N + k log k·log(N/k)) ≈ O(N log k). N=10000, k=8에서 비교 10115번 대 전체 정렬
136801번 — 13.5배. 추가 메모리는 전체 정렬이 N개 scratch, 힙은 0이다.

Q: 호출자 버퍼를 힙으로 쓰는 게 왜 이득인가? 주의할 점은?
A: `out` 이 이미 k칸 있으므로 별도 작업 공간이 필요 없다 — malloc도, 큰 로컬 배열도 없다.
09번은 `struct ApEntry` 가 48바이트라 N=256을 복사하면 12 KB 스택인데, 임베디드 스레드
스택이 8 KB면 그 자체로 crash다. 주의할 점: 반환 전에 k개를 정렬해야 한다(힙은 부분 정렬),
그리고 그 정렬은 `out` 이 호출자 전용이므로 락 밖에서 한다.

Q: lazy update 힙의 불변식은 무엇이고, 왜 루트만 고쳐도 정확한 최솟값이 나오나?
A: 불변식은 `모든 항목 i: key_i <= true_i` — 키는 진짜 만료 시각의 하한이다. `last_seen` 이
단조 증가하므로 진짜 값도 단조 증가하고, 저장된 키는 과거의 진짜 값이라 항상 작거나 같다.
루트를 진짜 값으로 갱신하고 sift_down 하는 것을 `key_root == true_root` 가 될 때까지
반복하면, 다른 모든 i에 대해 힙 성질로 `key_i >= key_root`, 불변식으로 `true_i >= key_i`
이므로 `true_i >= true_root`. 따라서 루트가 전역 최솟값이다. 나머지는 보지 않는다.

Q: heartbeat가 힙을 직접 고치면 무엇이 잘못되나?
A: 힙 수정은 O(log n)이고 락이 필요하다. 그러면 워커의 hot path에 락이 들어가서
(1) 32개 워커가 한 락·한 캐시 라인에 줄을 서고, (2) 워커가 watchdog 안에서 블록될 수 있다 —
살아 있는지 재는 대상이 재는 장치 때문에 멈추는 priority inversion이다. 락 보유자가
선점되면 함대 전체가 멈춘다. 그래서 heartbeat는 자기 캐시 라인에 release store 하나만 하고,
비용은 전부 reader(monitor)가 낸다.

Q: 힙 대신 타이머 휠이 나은 때는 언제인가? 10번은 왜 힙인가?
A: 타이머가 수천 개이고, 계속 재무장되고, tick 단위 오차를 허용할 때다. 삽입/삭제/만료가
O(1)이다(리눅스 커널 `timer_list`, TCP 재전송 타이머). 10번이 힙인 이유는 세 가지다.
n <= 32라서 휠의 상수 오버헤드가 log 32 = 5보다 비싸고, monitor가 정확히 다음 만료까지
자야 하는데 휠은 tick 단위로 양자화되며, lazy update 덕분에 재무장이 공짜라서 휠의 최대
장점이 필요 없다.

Q: 힙에서 특정 워커를 지우려면? 왜 선형 검색이 답이 아닌가?
A: 힙은 검색 자료구조가 아니라서 값으로 찾으면 O(n)이다. `g_pos[id] = 힙 인덱스` 배열을
따로 두고 swap마다 갱신하면 위치를 O(1)에 알 수 있고, 삭제는 마지막 원소로 덮은 뒤
sift_down과 sift_up을 둘 다 호출한다(대체된 원소는 아래로 갈 수도 위로 갈 수도 있고,
둘 중 하나만 실제로 움직인다). `g_pos` 갱신을 빼먹으면 힙과 위치 표가 영원히 어긋난다.
```

---

## 9. 요약 카드

- 이진 힙 = **포인터 없는 완전 이진 트리**. 0-based로 부모 `(i-1)/2`, 자식 `2i+1`, `2i+2`.
- 보장은 **부모 <= 자식** 하나뿐. `h[0]` 은 최솟값, 2등이 어디인지는 모른다. **정렬이 아니다.**
- peek O(1) · push/pop O(log n) · 위치를 알면 삭제 O(log n) · **검색 O(n)** → 위치 표(`g_pos`)를 따로 둔다.
- top-k는 **크기 k min-heap**. 루트 = 가장 약한 생존자 = 컷라인. 대부분이 비교 1번에 탈락 → O(N log k).
- 전체 정렬 대비 실측 13.5배(N=10000, k=8), 추가 메모리는 N개 scratch 대신 **0**.
- 힙을 **호출자 버퍼 안에서** 굴린다. 반환 전 k개만 삽입 정렬, 그것도 **락 밖에서**.
- 비교는 `weaker()` 함수 **하나로** 뽑고, 동점은 유일 키로 깨고 그 규칙을 **헤더에 적는다**.
- lazy update: 키를 **하한**으로 두면(`key <= true`) **루트만 고쳐도** 정확한 전역 최솟값.
  키가 커지기만 하므로 sift_down만 필요. 라운드당 수리 0.16번(실측).
- 같은 불변식이 가지치기를 정당화한다: `key > now` 인 노드의 서브트리는 전부 건강하다.
- n이 32쯤이면 전체 스캔이 정답일 수 있다. 수천 개 + 계속 재무장 + tick 오차 허용이면 **타이머 휠**.
