# 📊 실전 자료구조 (이벤트·로그 처리) (Practical Data Structures for Event & Log Processing) — Q69~Q78

> Verkada 에서 실제로 보고된 코딩 문제는 퍼즐이 아니다 — LRU 캐시, Merge Intervals,
> Non-overlapping Intervals, Extract IP Addresses, Camera Logs API, Camera States,
> Filter Alerts, top-k. 전부 **스트림·로그·인터벌**을 다루는 "제품에서 실제로 하는 일"의
> 모양이다. 이 세트는 그 10가지를 게이트웨이/카메라 fleet 시나리오로 다시 쓰되,
> **malloc 없이 고정 크기 저장소**로 푼다. 임베디드 Linux 자리에서 이게 차별점이다.

---

## 1. 핵심 아이디어

**LeetCode 답을 그대로 읊으면 점수가 안 오른다.** 이 자리는 Senior Embedded Linux
Engineer(Connectivity)다. 같은 문제라도 다음 셋을 얹어야 시니어로 읽힌다.

1. **저장소가 고정이다.** 용량을 미리 정하고 넘칠 때 무슨 일이 일어나는지 말한다
   (drop-oldest? reject-newest? 그리고 **버린 건 반드시 카운트한다**).
2. **표준 라이브러리가 없다.** `std::unordered_map` 도 `priority_queue` 도 없으니
   해시맵·힙·이중 연결 리스트를 그 자리에서 짤 수 있어야 한다.
3. **시간은 주입한다.** rate limiter/타임아웃/만료는 `now_ms` 를 인자로 받는다 — 리크루터
   메일의 "design principles for modular, **testable** embedded software" 가 바로 이것.

이 세트의 10문제 전부 **malloc/free 를 한 번도 호출하지 않는다.** 전부 구조체 안의
고정 배열이고, 동적으로 보이는 구조(LRU 노드, dedup 창)는 인덱스 기반 노드 풀이다.

---

## 2. 어떤 자료구조를 언제 고르나 (판단표)

| 요구사항 | 고르는 것 | 비용 | 이 세트 |
|---|---|---|---|
| "가장 최근 K개만 기억" (용량 고정) | 해시 + 이중 연결 리스트 = **LRU** | get/put O(1) | Q69 |
| "구간을 합쳐서 총량" | 정렬 후 1-pass **sweep** | O(n log n) / O(1) | Q70 |
| "겹치지 않게 최소 삭제" | 끝점 정렬 **그리디** | O(n log n) / O(1) | Q71 |
| "상위 K개만" (n ≫ k) | 크기 **K min-heap** | O(n log k) / O(k) | Q72 |
| "키별 카운트/집계" | **오픈 어드레싱 해시맵** | 평균 O(1) / O(slots) | Q73 |
| "초당 N개로 제한, 버스트 허용" | **토큰 버킷** | O(1) / O(1) | Q74 |
| "정렬된 스트림 여러 개를 시간순" | **two-pointer** (2개) / min-heap (k개) | O(n) / O(1) | Q75 |
| "최근 N개 안의 중복 억제" | **순환 버퍼 + 해시** | 평균 O(1) / O(N) | Q76 |
| "정렬된 데이터의 범위 질의" | **lower/upper_bound** | O(log n) / O(1) | Q77 |
| "텍스트 → 구조체" | **수동 스캔 파서** | O(L) / O(1) | Q78 |

> 표를 외우지 말고 **"왜 이걸 골랐는지"** 한 문장을 준비한다. 상위 K개에 전체 정렬은 버릴
> 일을 다 하는 것이고, 정렬된 두 스트림을 다시 정렬하면 가진 정보를 버리는 것이다.

---

## 3. C에서 직접 짜는 정석 패턴

### 3.1 오픈 어드레싱 해시맵 (선형 탐사)

정수 키든 문자열 키든 뼈대는 같다. **슬롯 수는 2의 거듭제곱** → `% cap` 대신
`& (cap-1)`. 나눗셈 명령이 느리거나 없는 MCU 에서 실질적인 차이가 난다.

```c
#define SLOTS 64                       /* 반드시 2의 거듭제곱 */
typedef struct { char key[16]; uint32_t val; bool used; } Slot;

static uint32_t fnv1a(const char *s) { /* 문자열 키 */
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
    return h;
}
static uint32_t mix32(uint32_t x) {    /* 정수 키: Knuth 곱셈 해시 */
    x *= 2654435761u; return x ^ (x >> 16);
}

/* 찾으면 그 슬롯, 없으면 삽입할 빈 슬롯. 포화면 SLOTS. */
static size_t probe(const Slot *t, const char *key, bool *found) {
    size_t i = fnv1a(key) & (SLOTS - 1);
    for (size_t p = 0; p < SLOTS; p++) {       /* ★ 탐사 횟수 상한 = 무한루프 방지 */
        size_t j = (i + p) & (SLOTS - 1);
        if (!t[j].used)                 { *found = false; return j; }
        if (strcmp(t[j].key, key) == 0) { *found = true;  return j; }
    }
    *found = false; return SLOTS;              /* 테이블 포화 */
}
```

기억할 세 가지: ①**탐사 루프에 `p < cap` 상한**(없으면 포화 시 무한 루프) ②부하율
0.7 을 넘으면 급격히 느려지므로 슬롯은 **기대 원소 수의 2배** ③**삭제가 잦으면
tombstone 대신 체이닝**(아래 3.3).

### 3.2 배열 이진 힙 (크기 K min-heap)

```c
/* 0-indexed: parent=(i-1)/2, left=2i+1, right=2i+2 */
static void sift_up(Event *h, size_t i) {
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (!better(&h[p], &h[i])) break;      /* 부모가 더 '낮으면' 정상 */
        swap(&h[i], &h[p]); i = p;
    }
}
static void sift_down(Event *h, size_t n, size_t i) {
    for (;;) {
        size_t l = 2*i+1, r = 2*i+2, low = i;
        if (l < n && better(&h[low], &h[l])) low = l;   /* ★ l<n 가드를 비교보다 먼저 */
        if (r < n && better(&h[low], &h[r])) low = r;
        if (low == i) break;
        swap(&h[i], &h[low]); i = low;
    }
}
```

top-K 의 핵심 관용구 — **힙에는 "지금까지의 상위 K개"만 있고 루트가 그중 최하위**다:

```c
if (cnt < k)                      { h[cnt] = ev[i]; sift_up(h, cnt); cnt++; }
else if (better(&ev[i], &h[0]))   { h[0]   = ev[i]; sift_down(h, cnt, 0); }
```

> 힙 저장소로 **호출자가 준 out[] 을 그대로 쓴다** (Q72) — 할당 0, 마지막에 그 자리에서
> 정렬만 하면 보고용 배열이 된다. 그리고 **동점 규칙을 반드시 정의한다**(score 동점이면
> dev_id 오름차순). 없으면 출력이 힙 내부 순서에 좌우돼 테스트가 플래키해진다.

### 3.3 침습적(intrusive) 이중 연결 리스트 + 체이닝 — 포인터 대신 인덱스

LRU(Q69)와 dedup(Q76)의 공통 뼈대다. **노드 하나가 리스트 원소이자 해시 체인 노드**다.

```c
typedef struct { int key, val; int prev, next; int hnext; bool used; } Node;
/*                              ↑ LRU 리스트      ↑ 해시 버킷 체인 (둘 다 인덱스) */
typedef struct {
    Node pool[CAP];        /* ★ 노드 풀 — malloc 이 여기로 대체된다 */
    int  bucket[BUCKETS];  /* 전부 -1 로 초기화 */
    int  head, tail, free_head, count;
} Cache;
```

포인터 대신 `int` 인덱스를 쓰는 이유 (**면접에서 말할 것**):
- 64비트 포인터 → 32비트(혹은 16비트) 인덱스 = 노드가 작아지고 캐시에 더 들어간다.
- 구조체 전체가 **위치 독립적**이다. 통째로 복사·DMA·flash 저장이 가능하다.
- `-1`(NIL)이 명확한 sentinel 이라 dangling pointer 가 원천적으로 없다.

체인에서 노드를 빼는 정석 — **`int *link` 로 "이전 노드의 next 필드"를 가리킨다.**
head 인지 중간인지 분기할 필요가 없어진다:

```c
static void bucket_remove(Cache *c, int idx) {
    int b = mix32(c->pool[idx].key) & (BUCKETS - 1);
    int *link = &c->bucket[b];
    while (*link != -1) {
        if (*link == idx) { *link = c->pool[idx].hnext; return; }
        link = &c->pool[*link].hnext;
    }
}
```

### 3.4 임베디드에서 malloc 을 피하는 법 — 노드 풀 (free list)

왜 피하나: ①**단편화** — 수개월 가동하면 여유 총량은 남았는데 할당이 실패하는 상태에
도달한다 ②**비결정적 지연** — glibc malloc 은 최악 지연을 보장하지 않아 실시간 경로에서
금물 ③**실패 경로** — NULL 반환 경로는 대부분 테스트되지 않는다. 부팅 때 다 잡아두면
그 경로 자체가 사라진다. 패턴은 항상 **고정 배열 + 그 안에 엮은 free list** 다:

```c
void pool_init(Cache *c, int cap) {
    for (int i = 0; i < cap; i++)
        c->pool[i].next = (i + 1 < cap) ? i + 1 : -1;  /* free list 를 미리 연결 */
    c->free_head = 0;
}
static int pool_alloc(Cache *c) {                      /* O(1) */
    int i = c->free_head;
    if (i == -1) return -1;                            /* 고갈 = 정상적인 상태 */
    c->free_head = c->pool[i].next;
    return i;
}
static void pool_free(Cache *c, int i) {               /* O(1) */
    c->pool[i].next = c->free_head; c->free_head = i;
}
```

핵심: **고갈은 에러가 아니라 설계된 상태**다. `-1` 을 받으면 drop 하고 카운터를 올린다.
LRU 에서는 고갈되기 전에 축출이 일어나므로 `pool_alloc` 이 실패할 수 없다 — 그게
불변식이고, 주석으로 적어두면 리뷰어가 좋아한다.

### 3.5 시간 주입 (dependency injection for the clock)

```c
bool tb_allow(TokenBucket *tb, uint64_t now_ms, uint32_t cost);   /* ← now 를 받는다 */
```

`clock_gettime()` 을 함수 안에서 부르면 "1초 뒤" 테스트가 `sleep(1)` 이 된다 — 느리고
플래키하다. 인자로 받으면 `tb_allow(&tb, 1500, 1)` 로 **즉시·결정적**이고, 프로덕션에서는
호출부가 `clock_gettime(CLOCK_MONOTONIC)` 을 한 번 읽어 넘긴다. 토큰도 **float 대신
milli-token 정수**로 들어 FPU 없는 파트에서 동일하게 돌고 반올림 오차가 없다.

```c
tb->tokens_milli += elapsed_ms * refill_per_sec;                         /* 1ms = rate/1000 토큰 */
if (tb->tokens_milli > tb->cap_milli) tb->tokens_milli = tb->cap_milli;  /* 버스트 상한 clamp */
```

---

## 4. 흔한 함정 (인터뷰 감점 포인트)

| 함정 | 왜 틀렸나 | 올바른 답 |
|---|---|---|
| 인터벌 문제에서 **어느 끝점으로 정렬할지** 안 따짐 | 병합은 start, 최소삭제 그리디는 **end** | 문제마다 다르다. 이유를 말할 것 |
| `[1,2]`, `[2,3]` 을 겹침으로 셈 | 규약을 안 물어봤다 | **"경계가 닿는 건 겹침인가요?"를 먼저 묻는다** |
| top-K 에 max-heap 사용 | max-heap 은 "버릴 것"을 O(1)에 못 준다 | **min-heap** 의 루트가 곧 탈락 후보 |
| 해시 탐사 루프에 상한 없음 | 가득 찬 테이블에서 무한 루프 | `for (p = 0; p < cap; p++)` |
| 파서가 **부분 결과를 out 에 남김** | 실패 시 호출자가 반쯤 채워진 구조체를 씀 | 임시 구조체에 채우고 **전부 통과 후 커밋** |
| 파서가 `strtok_r` 로 **입력 버퍼를 변형** | const 로그 버퍼/공유 버퍼면 파괴적 | const 포인터 수동 스캔 |
| 이진 탐색 `(lo+hi)/2` | 큰 인덱스에서 오버플로 | `lo + (hi - lo) / 2` |
| 드롭/억제를 **조용히** 함 | 조용한 손실이 제일 디버깅이 어렵다 | `dropped`, `suppressed`, `evictions` 를 센다 |
| 시계 역행(NTP step, 재부팅)을 무시 | 음수 elapsed → 거대한 토큰 충전 | `now <= last` 면 충전 0, last 만 재동기화 |

---

## 5. 면접에서 말할 것 (한국어 + 영어)

### 5.1 문제를 받자마자 (30초 요구사항 확인)

- 한: "용량은 고정인가요? 넘치면 가장 오래된 걸 버리나요, 새 걸 거절하나요?"
- 영: **"Is the capacity fixed? When it overflows, do we drop the oldest or reject the newest? Either way I'll count what we dropped — silent data loss is the hardest thing to debug in the field."**
- 한: "구간의 경계가 맞닿는 경우는 겹치는 걸로 보나요?"
- 영: **"Are touching endpoints — [1,2] and [2,3] — considered overlapping? It changes the comparison from `<` to `<=`, so I want to pin it down before I code."**

### 5.2 복잡도와 trade-off 를 말로 (핵심)

- LRU — 영: **"A hash map gives O(1) lookup but no ordering; a doubly linked list gives O(1) reordering but no lookup. Combining them gives O(1) get and put, at the cost of one extra pointer pair per node. I'm using array indices instead of pointers so the whole cache is one contiguous, position-independent block — no malloc, no fragmentation over a six-month uptime."**
- top-K — 영: **"Sorting everything is O(n log n) and throws most of the work away. A size-K min-heap is O(n log k) time and O(k) space, and the root is always the weakest of the current top K, so deciding whether to admit a new event is a single comparison. With two million devices and k = 10, that's the difference that matters."**
- 인터벌 — 영: **"Sorting dominates at O(n log n); the sweep itself is O(n) and in place, so the extra space is O(1) apart from the sort's stack. For the greedy non-overlapping problem I sort by end time, not start — keeping the interval that finishes earliest always leaves the most room for the rest, and that exchange argument is why greedy is optimal here."**
- 해시맵 — 영: **"Open addressing with linear probing keeps everything in one flat array — great cache locality and no per-node allocation — but it degrades past about 70% load and deletion needs tombstones. If this had frequent deletes I'd switch to chaining with an intrusive `next` index, which is what I did for the LRU and the dedup window."**
- 토큰 버킷 — 영: **"A token bucket expresses two independent knobs: the sustained rate and the burst size. It's O(1) per call with no timer and no background thread — we just compute how many tokens the elapsed time earned. I keep tokens as integer milli-tokens so it runs identically on a part with no FPU."**
- 병합 — 영: **"Both streams are already sorted, so re-sorting would be O(n log n) for information we already have. A two-pointer merge is O(na + nb) and single-pass. I break ties toward stream A so the output is stable — same input, same order, every time."**

### 5.3 테스트 전략 (리크루터가 적어준 "testable" 항목)

- 한: "시간을 인자로 주입했기 때문에 sleep 없이 rate limiter 를 테스트할 수 있습니다."
- 영: **"The clock is a parameter, not a call inside the function, so the rate limiter's tests run in microseconds and are fully deterministic — no sleeps, no flakes. In production the caller reads CLOCK_MONOTONIC once and passes it down."**
- 한: "엣지 케이스는 빈 입력, 용량 초과, 동일 타임스탬프, 잘못된 형식 네 가지를 먼저 씁니다."
- 영: **"My first four test cases are always: empty input, capacity exceeded, duplicate keys or identical timestamps, and malformed input. Those are where real device fleets actually break."**
- 한: "파서는 실패 시 출력 구조체를 건드리지 않습니다 — 전부 검증한 뒤에 커밋합니다."
- 영: **"The parser fills a local struct and only commits to the caller's output once every field validates. A half-parsed record that looks valid is worse than a rejected one."**

### 5.4 확장 질문(follow-up) 대비

- *"k개 스트림 병합은?"* → 스트림당 헤드 하나씩 담은 **크기 k min-heap**, O(N log k). two-pointer 는 k=2 의 특수화.
- *"LRU 대신 LFU 는?"* → 빈도별 리스트 버킷(O(1) LFU). 단, 과거에 뜨거웠던 항목이 눌러앉으므로 **aging 필요**.
- *"멀티스레드로?"* → 이 자료구조들은 **전부 스레드 안전이 아니다.** 소유 스레드 하나 + 앞단 bounded queue 가 1순위. 락을 건다면 LRU 는 전체 잠금(리스트 재연결 탓에 샤딩이 어렵다), 집계 해시맵은 키 샤딩.
- *"dedup 을 시간(30초) 기준으로?"* → 링에 타임스탬프를 같이 넣고 앞에서 만료 제거. 여전히 amortized O(1).
- *"로그가 완전히 정렬돼 있지 않다면?"* → 재정렬 창(reorder buffer) 또는 watermark 기반 지연 방출.

---

## 6. 체크리스트

- [ ] LRU 를 **malloc 없이** 노드 풀 + 인덱스 링크로 20분 안에 짤 수 있다
- [ ] LRU 축출 시 **리스트 unlink + 해시 remove + 풀 반납** 3종 세트를 빠뜨리지 않고, `get` 도 순서를 갱신한다
- [ ] 병합은 start 정렬, 최소삭제 그리디는 **end 정렬**임을 이유와 함께 말한다
- [ ] "경계가 닿으면 겹침인가"를 **코딩 전에 묻는다**
- [ ] top-K 에 **min-heap** 을 쓰고 O(n log k) 를 말한다 (전체 정렬 금지)
- [ ] sift_up / sift_down 을 0-indexed 공식으로 쓴다 (`(i-1)/2`, `2i+1`, `2i+2`) — 동점 규칙도 정의
- [ ] 오픈 어드레싱 탐사 루프에 **`p < cap` 상한**이 있고, 슬롯이 2의 거듭제곱 + `& (cap-1)` 인덱싱
- [ ] 토큰 버킷을 **정수 milli-token + 주입된 시각**으로 구현한다
- [ ] 시계 역행을 방어한다 (`now <= last` → 충전 0)
- [ ] 두 정렬 스트림 병합이 **stable** 하고 out_cap 을 넘지 않는다
- [ ] 이진 탐색 중점은 `lo + (hi - lo) / 2`, lower/upper_bound 는 `<` 와 `<=` 로 구분
- [ ] 파서가 입력 버퍼를 변형하지 않고, 실패 시 출력을 건드리지 않으며, 누락·모르는 키·중복 키·범위 밖 값을 전부 거부한다
- [ ] 버린 것(`dropped` / `suppressed` / `evictions` / `rejected`)을 전부 센다
- [ ] 각 함수의 시간·공간 복잡도를 영어 한 문장으로 말할 수 있다
- [ ] 테스트를 빈 입력 / 용량 초과 / 동일 타임스탬프 / 잘못된 형식부터 쓴다
