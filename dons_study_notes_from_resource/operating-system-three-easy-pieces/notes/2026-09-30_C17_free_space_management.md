# Ch.17 빈 공간 관리 — 분할, 병합, 그리고 할당 정책들

> 📖 원문: [17. Free-Space Management](../book-md/C17_free_space_management.md) · [PDF p.178](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=178) · ⏱️ 읽기 약 55분 · 🔗 선행: [Ch.14](2026-09-30_C14_memory_api.md), [Ch.16](2026-09-30_C16_segmentation.md)

## 0. 한눈에 보기

- 고정 크기 단위(페이지)만 관리하면 쉽다: 리스트에서 하나 꺼내 주면 끝. 어려운 건 **가변 크기 요청** — malloc 라이브러리, 그리고 세그먼트를 쓰는 OS.
- 가변 크기의 적은 **외부 단편화**: 빈 공간 합계는 충분한데 조각나서 요청을 못 받는다(20바이트 비었는데 15바이트 실패).
- 저수준 메커니즘 셋: **분할(splitting)**, **병합(coalescing)**, 그리고 크기를 기억하는 **헤더**. free list 는 별도 메모리가 아니라 **빈 공간 그 자체 안에** 심는다.
- 정책: **best fit / worst fit / first fit / next fit**. 어느 것도 모든 입력에 최선이 아니다.
- 더 나간 방법: **분리 리스트(segregated list)** 와 **slab 할당기**, **이진 버디 할당기(buddy)**, 트리 기반, 멀티코어용 할당기(Hoard, jemalloc).

> **CRUX: HOW TO MANAGE FREE SPACE** — "How should free space be managed, when satisfying variable-sized requests? What strategies can be used to minimize fragmentation? What are the time and space overheads of alternate approaches?"
>
> → 가변 크기 요청을 처리할 때 빈 공간을 어떻게 관리할까? 단편화를 최소화하는 전략은? 각 방법의 시간 · 공간 오버헤드는?

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| free list | 빈 청크들을 가리키는 자료구조(꼭 리스트일 필요는 없음) | head → [addr 0, len 10] → [addr 20, len 10] |
| 외부 단편화(external fragmentation) | 빈 공간이 조각나 큰 요청 실패 | 10+10 비었는데 15 실패 |
| 내부 단편화(internal fragmentation) | 요청보다 큰 청크를 줘서 안에서 낭비 | 7KB 요청에 8KB 블록 |
| 분할(splitting) | 큰 빈 청크를 요청 크기 + 나머지로 쪼갬 | len 10 → 1 바이트 주고 len 9 남김 |
| 병합(coalescing) | 이웃한 빈 청크를 하나로 합침 | 10+10+10 → 30 |
| 헤더(header) | 할당 청크 바로 앞의 메타데이터 | size, magic |
| magic number | 헤더가 진짜인지 검사하는 값 | 1234567 |
| 임베디드 free list | 빈 공간 안에 노드(size, next)를 심음 | 할당기 안에서는 malloc 을 못 쓰니까 |
| best fit | 들어가는 것 중 가장 작은 청크 | 낭비 최소, 전체 탐색 필요 |
| worst fit | 가장 큰 청크 | 큰 덩어리 남기려 했지만 성능 나쁨 |
| first fit | 처음 맞는 청크 | 빠름, 앞쪽이 잘게 쪼개짐 |
| next fit | 지난번 멈춘 곳부터 first fit | 탐색을 리스트 전체에 분산 |
| 분리 리스트(segregated list) | 인기 크기 전용 리스트 | 커널의 inode 캐시 |
| slab 할당기 | 객체 캐시 + 미리 초기화된 객체 | Solaris, Linux SLUB |
| 버디 할당기(buddy allocator) | 2의 거듭제곱으로 쪼개고 버디끼리 병합 | 버디 주소는 한 비트 차이 |

## 2. 문제 정의와 가정 (17.1)

```text
           free         used        free
     0            10           20          30
```

30바이트 힙에 빈 공간이 총 20바이트지만 10바이트씩 두 조각이다. **15바이트 요청은 실패**. 이게 외부 단편화다.

가정:

1. 인터페이스는 `malloc(size)` / `free(ptr)`. **free 는 크기를 받지 않는다** → 라이브러리가 포인터만 보고 크기를 알아내야 한다.
2. 주로 **외부 단편화** 를 다룬다(내부 단편화도 있지만 덜 흥미롭다).
3. 한 번 준 메모리는 **옮길 수 없다**(포인터가 어디 저장됐는지 모르므로) → **압축(compaction) 불가**. OS 가 세그먼트를 관리할 때는 가능하지만 malloc 은 안 된다.
4. 관리하는 영역은 **고정 크기의 연속 영역** 하나(실제로는 sbrk 로 늘릴 수 있지만 단순화).

## 3. 저수준 메커니즘 (17.2)

### 3.1 분할과 병합

위 30바이트 힙의 free list:

```text
head → [addr:0  len:10] → [addr:20 len:10] → NULL
```

**분할**: 1바이트 요청이 오면, 맞는 청크를 찾아 둘로 쪼갠다. 앞부분은 호출자에게, 나머지는 리스트에 남긴다. 두 번째 청크를 썼다면 malloc 은 20 을 반환하고:

```text
head → [addr:0  len:10] → [addr:21 len:9] → NULL
```

**병합**: 이제 가운데 사용 중이던 10바이트(주소 10)를 free 했다고 하자. 생각 없이 리스트 앞에 넣으면:

```text
head → [addr:10 len:10] → [addr:0 len:10] → [addr:20 len:10] → NULL
```

힙 전체가 비었는데도 **20바이트 요청이 실패** 한다. 그래서 free 할 때 **주소상 이웃한 빈 청크가 있으면 합친다**:

```text
head → [addr:0 len:30] → NULL
```

```svg
<svg viewBox="0 0 700 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C17-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" style="fill:var(--accent)"/>
    </marker>
  </defs>
  <text x="20" y="22" fill="currentColor" font-weight="bold">① 처음 (30바이트 힙)</text>
  <rect x="200" y="8" width="150" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="275" y="25" text-anchor="middle" fill="currentColor">free 0–9</text>
  <rect x="350" y="8" width="150" height="24" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="425" y="25" text-anchor="middle" fill="currentColor">used 10–19</text>
  <rect x="500" y="8" width="150" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="575" y="25" text-anchor="middle" fill="currentColor">free 20–29</text>

  <text x="20" y="77" fill="currentColor" font-weight="bold">② malloc(1): 분할</text>
  <rect x="200" y="63" width="150" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="275" y="80" text-anchor="middle" fill="currentColor">free 0–9</text>
  <rect x="350" y="63" width="150" height="24" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="425" y="80" text-anchor="middle" fill="currentColor">used</text>
  <rect x="500" y="63" width="15" height="24" style="fill:var(--accent);fill-opacity:0.6" stroke="currentColor"/>
  <rect x="515" y="63" width="135" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="582" y="80" text-anchor="middle" fill="currentColor">free 21–29 (len 9)</text>
  <text x="507" y="105" text-anchor="middle" style="fill:var(--accent)" font-size="11">↑ 반환 20</text>

  <text x="20" y="147" fill="currentColor" font-weight="bold">③ free(10), 병합 없음</text>
  <rect x="200" y="133" width="150" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="275" y="150" text-anchor="middle" fill="currentColor">free 10</text>
  <rect x="350" y="133" width="150" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="425" y="150" text-anchor="middle" fill="currentColor">free 10</text>
  <rect x="500" y="133" width="150" height="24" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="575" y="150" text-anchor="middle" fill="currentColor">free 10</text>
  <text x="425" y="180" text-anchor="middle" fill="#d9534f" font-size="12">다 비었는데 리스트는 3조각 → malloc(20) 실패</text>

  <text x="20" y="232" fill="currentColor" font-weight="bold">④ free(10), 병합</text>
  <rect x="200" y="218" width="450" height="24" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
  <text x="425" y="235" text-anchor="middle" fill="currentColor">free 0–29 (len 30)</text>
  <line x1="350" y1="160" x2="350" y2="214" style="stroke:var(--accent)" stroke-width="1.5" marker-end="url(#C17-arrow)"/>
  <line x1="500" y1="160" x2="500" y2="214" style="stroke:var(--accent)" stroke-width="1.5" marker-end="url(#C17-arrow)"/>
  <text x="425" y="275" text-anchor="middle" fill="currentColor" font-size="12">주소순으로 이웃을 확인해 합치면 malloc(20) 성공</text>
</svg>
```

### 3.2 할당된 영역의 크기 추적 — 헤더

`free(ptr)` 이 크기를 알려면, 할당기가 **포인터 바로 앞에 헤더** 를 둔다.

```c
typedef struct __header_t {
    int size;
    int magic;
} header_t;

void free(void *ptr) {
    header_t *hptr = (void *)ptr - sizeof(header_t);
    ...
}
```

- `assert(hptr->magic == 1234567)` 로 **진짜 헤더인지** 검사 (Ch.14 의 invalid free 를 잡는 방법).
- 해제되는 영역의 크기 = **헤더 크기 + 사용자 영역 크기**.
- 그래서 사용자가 N 바이트를 요청하면, 할당기는 **N + 헤더 크기** 짜리 빈 청크를 찾는다.

### 3.3 free list 를 빈 공간 안에 심기

할당기 안에서는 노드를 위해 `malloc` 을 부를 수 없다(자기가 malloc 이니까). 그래서 **빈 공간 자체에 노드를 쓴다**.

```c
typedef struct __node_t {
    int              size;
    struct __node_t *next;
} node_t;

// mmap() returns a pointer to a chunk of free space
node_t *head = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
head->size   = 4096 - sizeof(node_t);
head->next   = NULL;
```

원문은 32비트를 가정해 `node_t` 가 8바이트 → 처음 크기 **4088**. 힙이 가상 주소 16KB(16384)에 있다고 하고 원문 그림을 따라가 보자(헤더 8바이트).

| 단계 | 일 | free list | 원문 그림 |
|---|---|---|---|
| 초기화 | 노드 1개 | @16384 size 4088 | Fig 17.3 |
| malloc(100) | 108 바이트(헤더 8 + 100) 를 떼어 줌, ptr = 16392 | @16492 size 3980 (= 4088 − 108) | Fig 17.4 |
| malloc(100) × 2 더 | ptr = 16500, 16608 | @16708 size 3764 (= 4088 − 3×108) | Fig 17.5 |
| free(16500) | 헤더 = 16500 − 8 = 16492, 크기 100. 리스트 머리에 삽입 | @16492 size 100 → @16708 size 3764 | Fig 17.6 |
| free(16392), free(16608) | 머리에 차례로 삽입 | @16600 → @16384 → @16492 → @16708 | Fig 17.7 |

16500 은 어디서 왔나? 16384(힙 시작) + 108(첫 청크) + 8(두 번째 청크의 헤더) = **16500**. 마지막 상태는 **전부 비었는데 4조각** 이다. 병합을 안 했기 때문. 이 표 전체를 아래 C 코드가 바이트 단위로 재현한다.

```svg
<svg viewBox="0 0 700 290" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="12">
  <defs>
    <marker id="C17-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" style="fill:var(--accent)"/>
    </marker>
  </defs>
  <text x="350" y="18" text-anchor="middle" fill="currentColor" font-weight="bold" font-size="13">Fig 17.7 재현: 다 비었지만 병합 안 된 free list (가상 주소 16KB 부터)</text>
  <rect x="20" y="110" width="130" height="60" fill="none" stroke="currentColor"/>
  <text x="85" y="130" text-anchor="middle" fill="currentColor">@16384</text>
  <text x="85" y="146" text-anchor="middle" fill="currentColor">size 100</text>
  <text x="85" y="162" text-anchor="middle" fill="currentColor">next 16492</text>
  <rect x="150" y="110" width="130" height="60" fill="none" stroke="currentColor"/>
  <text x="215" y="130" text-anchor="middle" fill="currentColor">@16492</text>
  <text x="215" y="146" text-anchor="middle" fill="currentColor">size 100</text>
  <text x="215" y="162" text-anchor="middle" fill="currentColor">next 16708</text>
  <rect x="280" y="110" width="130" height="60" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
  <text x="345" y="130" text-anchor="middle" fill="currentColor">@16600 ← head</text>
  <text x="345" y="146" text-anchor="middle" fill="currentColor">size 100</text>
  <text x="345" y="162" text-anchor="middle" fill="currentColor">next 16384</text>
  <rect x="410" y="110" width="270" height="60" fill="none" stroke="currentColor"/>
  <text x="545" y="130" text-anchor="middle" fill="currentColor">@16708</text>
  <text x="545" y="146" text-anchor="middle" fill="currentColor">size 3764</text>
  <text x="545" y="162" text-anchor="middle" fill="currentColor">next 0 (NULL)</text>
  <path d="M345,108 C345,50 85,50 85,106" fill="none" style="stroke:var(--accent)" stroke-width="1.5" marker-end="url(#C17-arrow2)"/>
  <text x="215" y="60" text-anchor="middle" style="fill:var(--accent)">① head → 16384</text>
  <path d="M85,172 C85,215 215,215 215,174" fill="none" style="stroke:var(--accent)" stroke-width="1.5" marker-end="url(#C17-arrow2)"/>
  <text x="150" y="228" text-anchor="middle" style="fill:var(--accent)">② → 16492</text>
  <path d="M215,172 C215,255 545,255 545,174" fill="none" style="stroke:var(--accent)" stroke-width="1.5" marker-end="url(#C17-arrow2)"/>
  <text x="380" y="270" text-anchor="middle" style="fill:var(--accent)">③ → 16708</text>
  <text x="350" y="90" text-anchor="middle" fill="currentColor">각 칸 = 노드 헤더 8B + 데이터. 주소상으론 딱 붙어 있다 (16384+108 = 16492, 16492+108 = 16600, ...)</text>
  <text x="350" y="286" text-anchor="middle" fill="#d9534f">리스트 순서는 주소순이 아님 → 이웃 찾기가 어렵고 malloc(3800) 실패</text>
</svg>
```

### 3.4 힙 키우기

힙이 모자라면? 가장 단순한 건 **실패(NULL)**. 대부분의 할당기는 작은 힙으로 시작해 모자라면 OS 에 더 요청한다(`sbrk`). OS 는 빈 물리 페이지를 찾아 주소 공간에 매핑하고 새 힙 끝을 돌려준다.

## 4. 기본 전략 (17.3)

이상적인 할당기는 **빠르고 단편화가 적다**. 하지만 요청 순서는 프로그래머 마음대로라, 어떤 전략이든 나쁜 입력에선 나쁘다.

| 전략 | 방법 | 장점 | 단점 |
|---|---|---|---|
| best fit | 요청 이상인 것 중 **가장 작은** 청크 | 낭비 최소화 시도 | 전체 탐색. 아주 작은 찌꺼기 청크가 많이 생김 |
| worst fit | **가장 큰** 청크에서 떼어 줌 | 큰 나머지를 남기려는 의도 | 전체 탐색. 연구 결과 단편화가 오히려 심하고 느림 |
| first fit | **처음 맞는** 청크 | 탐색이 짧아 빠름 | 리스트 앞부분이 작은 조각으로 오염됨 → 주소순 정렬로 완화 |
| next fit | 지난번 멈춘 위치부터 first fit | 탐색을 리스트 전체에 분산 | first fit 과 성능 비슷 |

first fit 에서 리스트를 **주소순(address-based ordering)** 으로 유지하면 병합이 쉬워지고 단편화도 줄어드는 경향이 있다.

### 4.1 원문 예제

free list: `head → 10 → 30 → 20 → NULL`, 요청 15.

```text
best fit : 15 이상인 것 = {30, 20}, 가장 작은 20 선택 → head → 10 → 30 → 5  → NULL
worst fit: 가장 큰 30 선택                          → head → 10 → 15 → 20 → NULL
first fit: 처음 맞는 30 선택 (worst 와 결과 같음)    → head → 10 → 15 → 20 → NULL
탐색 비용: best/worst 는 3개 다 봄, first 는 2개에서 멈춤
```

best fit 이 남긴 **5** 같은 작은 찌꺼기가 best fit 의 전형적인 부작용이다.

### 4.2 새 예제 (직접 계산, 헤더 포함)

헤더 8바이트가 있는 실제 할당기라면? 빈 청크(사용 가능 크기) 100, 300, 200 에 150 요청:

```text
best fit : 200 선택. 떼어 가는 양 = 150 + 8(새 헤더) → 남는 노드 크기 = 200 − 158 = 42
worst fit: 300 선택 → 남는 크기 = 300 − 158 = 142
first fit: 100 은 작음, 300 선택 → 142 (worst 와 같음), 탐색 2개
```

원문의 "20 − 15 = 5" 가 여기선 "200 − 150 − 8 = 42" 가 된다 — **헤더만큼 더 빠진다**. 그리고 남는 쪽이 헤더(8)도 못 담을 만큼 작으면 쪼개지 않고 통째로 줘야 한다(내부 단편화). 아래 C 코드 [3] 이 정확히 이 숫자를 낸다.

## 5. 다른 접근들 (17.4)

### 5.1 분리 리스트와 slab 할당기

**분리 리스트(segregated list)**: 앱이 자주 요청하는 크기가 있다면 **그 크기 전용 리스트** 를 따로 둔다. 나머지는 일반 할당기로.

- 장점: 해당 크기 요청은 탐색 없이 즉시, 단편화 걱정도 적다.
- 문제: 전용 풀에 메모리를 얼마나 줄까?

**slab 할당기**(Jeff Bonwick, Solaris 커널)의 답:

1. 부팅 때 자주 쓰는 커널 객체(락, inode 등)용 **객체 캐시** 를 만든다 — 각각이 특정 크기의 분리 리스트.
2. 캐시가 모자라면 일반 할당기에서 **slab**(페이지 크기의 배수) 을 받아 온다.
3. slab 안 객체들의 참조 카운트가 전부 0 이 되면 일반 할당기가 회수할 수 있다(VM 이 메모리를 원할 때).
4. **free 된 객체를 초기화된 상태로 보관** → 초기화/파괴 비용(Bonwick 이 비싸다고 보여 준)을 아낀다.

> **ASIDE — 훌륭한 엔지니어는 정말 훌륭하다**: slab 과 ZFS 를 만든 Bonwick 같은 사람이 실리콘밸리의 심장이다. "100배" 엔지니어가 되거나, 그런 사람과 일하라. 그것도 아니면… 슬퍼하라(원문 농담).

### 5.2 버디 할당기

병합을 **쉽게** 만드는 데 초점을 맞춘 설계. 빈 공간 전체를 2^N 크기로 보고, 요청이 오면 **반씩 쪼개며** 요청이 들어가는 가장 작은 블록까지 내려간다(더 쪼개면 안 들어가는 지점).

원문 예: 64KB 에서 7KB 요청 → 64 → 32 → 16 → **8KB** 블록을 준다. 2의 거듭제곱만 주므로 **내부 단편화**(1KB)가 생긴다.

```svg
<svg viewBox="0 0 700 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <text x="350" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">이진 버디: 64KB 에서 7KB 요청 → 8KB 블록 (오프셋은 KB)</text>
  <rect x="40" y="35" width="620" height="34" fill="none" stroke="currentColor"/>
  <text x="350" y="57" text-anchor="middle" fill="currentColor">64KB @0</text>
  <rect x="40" y="95" width="310" height="34" fill="none" stroke="currentColor"/>
  <text x="195" y="117" text-anchor="middle" fill="currentColor">32KB @0</text>
  <rect x="350" y="95" width="310" height="34" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="505" y="117" text-anchor="middle" fill="currentColor">32KB @32 (free)</text>
  <rect x="40" y="155" width="155" height="34" fill="none" stroke="currentColor"/>
  <text x="117" y="177" text-anchor="middle" fill="currentColor">16KB @0</text>
  <rect x="195" y="155" width="155" height="34" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="272" y="177" text-anchor="middle" fill="currentColor">16KB @16 (free)</text>
  <rect x="40" y="215" width="77" height="34" style="fill:var(--accent);fill-opacity:0.45;stroke:var(--accent)" stroke-width="2"/>
  <text x="78" y="237" text-anchor="middle" fill="currentColor" font-weight="bold">8KB @0</text>
  <rect x="117" y="215" width="78" height="34" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="156" y="237" text-anchor="middle" fill="currentColor">8KB @8</text>
  <g stroke="currentColor">
    <line x1="195" y1="69" x2="195" y2="95"/><line x1="505" y1="69" x2="505" y2="95"/>
    <line x1="117" y1="129" x2="117" y2="155"/><line x1="272" y1="129" x2="272" y2="155"/>
    <line x1="78" y1="189" x2="78" y2="215"/><line x1="156" y1="189" x2="156" y2="215"/>
  </g>
  <text x="380" y="215" fill="currentColor" font-size="12">버디 주소 = 내 주소 XOR 블록 크기</text>
  <text x="380" y="235" fill="currentColor" font-size="12">8KB @0  ↔ 8KB @8   : 0x0000 ^ 0x2000 = 0x2000</text>
  <text x="380" y="255" fill="currentColor" font-size="12">16KB @0 ↔ 16KB @16 : 0x0000 ^ 0x4000 = 0x4000</text>
  <text x="380" y="275" fill="currentColor" font-size="12">32KB @0 ↔ 32KB @32 : 0x0000 ^ 0x8000 = 0x8000</text>
  <text x="78" y="270" text-anchor="middle" style="fill:var(--accent)" font-size="11">7KB 사용, 1KB 내부 단편화</text>
</svg>
```

**아름다운 부분은 free 때**: 8KB 블록을 돌려줄 때 **버디** 8KB 가 비어 있으면 합쳐 16KB, 그 16KB 의 버디도 비어 있으면 32KB, … 재귀적으로 올라가 전체를 복원하거나 사용 중인 버디를 만나면 멈춘다.

버디를 찾기 쉬운 이유: 버디 쌍의 주소는 **딱 한 비트만 다르다**. 어느 비트인지는 트리의 레벨(블록 크기)이 정한다. 즉 `buddy = addr ^ size`.

### 5.3 그 밖의 아이디어

- **확장성 문제**: 리스트 탐색은 느리다 → 균형 이진 트리, splay tree, 부분 정렬 트리 같은 복잡한 자료구조.
- **멀티프로세서**: 여러 스레드가 동시에 malloc 하면 락 경합 → Hoard(Berger et al.), jemalloc(Evans; FreeBSD, Firefox, Facebook) 같은 스레드/CPU 별 아레나 설계.
- 현실 감각을 원하면 glibc malloc 구조를 읽어 보라(원문 추천 [S15]).

## 6. 요약 (17.5)

할당기는 모든 C 프로그램에 링크되어 있고, OS 도 자기 자료구조를 위해 쓴다. 트레이드오프투성이이며, **워크로드를 정확히 알수록 더 잘 튜닝할 수 있다**. 다양한 워크로드에서 빠르고, 공간 효율적이며, 확장성 있는 할당기는 지금도 열린 문제다.

## 7. 직접 해보기

### 7.1 free list 할당기 — `code/C17_freelist_alloc.c`

원문 그림을 숫자 그대로 재현하려고 헤더를 8바이트로 고정하고(원문의 32비트 가정), `next` 는 64비트 포인터 대신 **힙 시작 기준 오프셋** 으로 저장했다. 출력 주소는 "힙이 가상 16384 에 있다" 고 보고 16384 + 오프셋. 힙 자체는 원문처럼 `mmap` 으로 받는다. 정책(FIRST/BEST/WORST)과 병합 on/off 를 지원한다.

```c
// C17_freelist_alloc.c — 원문 17.2절의 "free list 를 free 공간 안에 심는" 작은 할당기
// build: cc -Wall -Wextra -O0 code/C17_freelist_alloc.c -o .work/bin/C17_freelist_alloc
//
// 원문 그림(32-bit 가정)과 숫자를 그대로 맞추려고 헤더를 8바이트로 고정한다:
//   할당된 청크 헤더 : { uint32 size; uint32 magic; }
//   free 노드 헤더   : { uint32 size; uint32 next;  }   next = 힙 시작 기준 오프셋 (NIL = 없음)
// 주소는 "가상 주소 16KB(16384) 에 힙이 있다"고 가정해 16384 + offset 으로 출력한다.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>

#define HEAP_SIZE 4096u
#define VBASE     16384u                 // 원문 그림의 가상 주소 16KB
#define MAGIC     1234567u
#define NIL       0xFFFFFFFFu
#define HDR       8u

typedef struct { uint32_t size, magic; } header_t;
typedef struct { uint32_t size, next; } node_t;

typedef enum { FIRST, BEST, WORST } policy_t;
static const char *pname[] = {"FIRST", "BEST", "WORST"};

static uint8_t *heap;                    // mmap 으로 받은 4KB
static uint32_t head;                    // free list 머리 (오프셋)
static policy_t policy = FIRST;
static int coalesce = 0;                 // 0: 머리에 끼워넣기(원문 그림), 1: 주소순 + 병합
static int quiet = 0;                    // 1 이면 malloc/free 로그 생략 (준비 단계용)

#define NODE(off) ((node_t *)(heap + (off)))
#define VA(off)   ((off) == NIL ? 0u : VBASE + (off))

static void heap_init(void) {
    if (!heap) {
        heap = mmap(NULL, HEAP_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        assert(heap != MAP_FAILED);
    }
    memset(heap, 0, HEAP_SIZE);
    head = 0;
    NODE(0)->size = HEAP_SIZE - sizeof(node_t);   // 4088
    NODE(0)->next = NIL;
}

static void dump(const char *tag) {
    printf("  %-26s free list:", tag);
    int n = 0;
    for (uint32_t c = head; c != NIL; c = NODE(c)->next, n++)
        printf(" [@%u size:%u next:%u]", VA(c), NODE(c)->size, VA(NODE(c)->next));
    if (!n) printf(" (empty)");
    printf("\n");
}

// 정책에 따라 청크를 고르고 분할(split)한다. 반환값: 사용자 포인터의 오프셋 (실패 NIL)
static uint32_t my_malloc(uint32_t n) {
    uint32_t need = n + HDR;                       // 원문: N 이 아니라 N + 헤더를 찾는다
    uint32_t best = NIL, best_prev = NIL, prev = NIL;
    int searched = 0;
    for (uint32_t c = head; c != NIL; prev = c, c = NODE(c)->next) {
        searched++;
        // 사용 가능 바이트 = 노드 헤더 뒤 size + 헤더 8바이트(재사용) 이므로 size >= n 이면 들어간다
        if (NODE(c)->size < n) continue;
        int take = best == NIL
            || (policy == BEST  && NODE(c)->size < NODE(best)->size)
            || (policy == WORST && NODE(c)->size > NODE(best)->size);
        if (take) { best = c; best_prev = prev; }
        if (policy == FIRST) break;                  // 첫 번째로 맞는 것에서 멈춘다
    }
    if (best == NIL) { if (!quiet) printf("  malloc(%u) -> NULL (searched %d)\n", n, searched); return NIL; }

    node_t *b = NODE(best);
    uint32_t next = b->next, granted = n;
    uint32_t replacement;                            // free list 에서 best 자리를 대신할 것
    if (b->size >= n + HDR + 1) {                    // split: 남는 쪽에 새 노드를 만들 공간이 있다
        uint32_t rest = best + need;
        NODE(rest)->size = b->size - need;
        NODE(rest)->next = next;
        replacement = rest;
    } else {                                         // 너무 작게 남으면 통째로 준다 (내부 단편화)
        granted = b->size;
        replacement = next;
    }
    if (best_prev == NIL) head = replacement; else NODE(best_prev)->next = replacement;

    header_t *h = (header_t *)(heap + best);
    h->size = granted; h->magic = MAGIC;
    if (!quiet) printf("  malloc(%u) -> %u  [%s, searched %d]\n", n, VA(best + HDR), pname[policy], searched);
    return best + HDR;
}

static void my_free(uint32_t ptr) {
    uint32_t hoff = ptr - HDR;                      // 원문: hptr = (void *)ptr - sizeof(header_t)
    header_t *h = (header_t *)(heap + hoff);
    assert(h->magic == MAGIC);                       // 무결성 검사
    uint32_t size = h->size;
    node_t *nd = NODE(hoff);
    nd->size = size;
    if (!quiet) printf("  free(%u)  (header @%u, size %u)\n", VA(ptr), VA(hoff), size);
    if (!coalesce) {                                 // 원문 그림 17.6/17.7: 그냥 머리에 넣기
        nd->next = head; head = hoff;
        return;
    }
    // 주소순 삽입
    uint32_t prev = NIL, c = head;
    while (c != NIL && c < hoff) { prev = c; c = NODE(c)->next; }
    nd->next = c;
    if (prev == NIL) head = hoff; else NODE(prev)->next = hoff;
    // 뒤 이웃과 병합: 내 끝 == 다음 노드 시작
    if (c != NIL && hoff + HDR + nd->size == c) {
        nd->size += HDR + NODE(c)->size;
        nd->next = NODE(c)->next;
    }
    // 앞 이웃과 병합
    if (prev != NIL && prev + HDR + NODE(prev)->size == hoff) {
        NODE(prev)->size += HDR + nd->size;
        NODE(prev)->next = nd->next;
    }
}

int main(void) {
    printf("[1] 원문 Figure 17.3~17.7 재현 (coalescing 없음, 머리에 삽입)\n");
    heap_init(); dump("init (Fig 17.3)");
    uint32_t a = my_malloc(100); dump("after 1 alloc (Fig 17.4)");
    uint32_t b = my_malloc(100);
    uint32_t c = my_malloc(100); dump("after 3 allocs (Fig 17.5)");
    my_free(b); dump("free middle (Fig 17.6)");
    my_free(a); my_free(c); dump("free all (Fig 17.7)");
    uint32_t big = my_malloc(300);                   // 메모리는 전부 비었는데...
    printf("  -> 300B 요청은 %s (4개 조각에 흩어져 있어도 3764 짜리가 있어 성공)\n", big == NIL ? "실패" : "성공");
    my_malloc(3800);                                 // 3764 보다 크면 실패: 단편화

    printf("\n[2] 같은 순서, 주소순 정렬 + coalescing\n");
    coalesce = 1; heap_init();
    a = my_malloc(100); b = my_malloc(100); c = my_malloc(100);
    my_free(b); dump("free middle");
    my_free(a); dump("free first (merge 1)");
    my_free(c); dump("free last (merge all)");
    my_malloc(3800); dump("3800B 요청");

    printf("\n[3] 정책 비교: free 청크 100, 300, 200 에 150B 요청 (원문 10/30/20 + 15 를 x10)\n");
    for (int p = FIRST; p <= WORST; p++) {
        policy = FIRST; coalesce = 1; quiet = 1; heap_init();
        uint32_t x1 = my_malloc(100), y1 = my_malloc(8);
        uint32_t x2 = my_malloc(300), y2 = my_malloc(8);
        uint32_t x3 = my_malloc(200);
        my_malloc(NODE(head)->size);                 // 나머지는 통째로 소진시켜 tail 을 없앤다
        my_free(x1); my_free(x2); my_free(x3);
        (void)y1; (void)y2;
        policy = (policy_t)p; quiet = 0;
        printf("  --- policy %s\n", pname[p]);
        dump("before");
        my_malloc(150);
        dump("after");
    }
    munmap(heap, HEAP_SIZE);
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 code/C17_freelist_alloc.c -o .work/bin/C17_freelist_alloc && .work/bin/C17_freelist_alloc
[1] 원문 Figure 17.3~17.7 재현 (coalescing 없음, 머리에 삽입)
  init (Fig 17.3)            free list: [@16384 size:4088 next:0]
  malloc(100) -> 16392  [FIRST, searched 1]
  after 1 alloc (Fig 17.4)   free list: [@16492 size:3980 next:0]
  malloc(100) -> 16500  [FIRST, searched 1]
  malloc(100) -> 16608  [FIRST, searched 1]
  after 3 allocs (Fig 17.5)  free list: [@16708 size:3764 next:0]
  free(16500)  (header @16492, size 100)
  free middle (Fig 17.6)     free list: [@16492 size:100 next:16708] [@16708 size:3764 next:0]
  free(16392)  (header @16384, size 100)
  free(16608)  (header @16600, size 100)
  free all (Fig 17.7)        free list: [@16600 size:100 next:16384] [@16384 size:100 next:16492] [@16492 size:100 next:16708] [@16708 size:3764 next:0]
  malloc(300) -> 16716  [FIRST, searched 4]
  -> 300B 요청은 성공 (4개 조각에 흩어져 있어도 3764 짜리가 있어 성공)
  malloc(3800) -> NULL (searched 4)

[2] 같은 순서, 주소순 정렬 + coalescing
  malloc(100) -> 16392  [FIRST, searched 1]
  malloc(100) -> 16500  [FIRST, searched 1]
  malloc(100) -> 16608  [FIRST, searched 1]
  free(16500)  (header @16492, size 100)
  free middle                free list: [@16492 size:100 next:16708] [@16708 size:3764 next:0]
  free(16392)  (header @16384, size 100)
  free first (merge 1)       free list: [@16384 size:208 next:16708] [@16708 size:3764 next:0]
  free(16608)  (header @16600, size 100)
  free last (merge all)      free list: [@16384 size:4088 next:0]
  malloc(3800) -> 16392  [FIRST, searched 1]
  3800B 요청               free list: [@20192 size:280 next:0]

[3] 정책 비교: free 청크 100, 300, 200 에 150B 요청 (원문 10/30/20 + 15 를 x10)
  --- policy FIRST
  before                     free list: [@16384 size:100 next:16508] [@16508 size:300 next:16832] [@16832 size:200 next:0]
  malloc(150) -> 16516  [FIRST, searched 2]
  after                      free list: [@16384 size:100 next:16666] [@16666 size:142 next:16832] [@16832 size:200 next:0]
  --- policy BEST
  before                     free list: [@16384 size:100 next:16508] [@16508 size:300 next:16832] [@16832 size:200 next:0]
  malloc(150) -> 16840  [BEST, searched 3]
  after                      free list: [@16384 size:100 next:16508] [@16508 size:300 next:16990] [@16990 size:42 next:0]
  --- policy WORST
  before                     free list: [@16384 size:100 next:16508] [@16508 size:300 next:16832] [@16832 size:200 next:0]
  malloc(150) -> 16516  [WORST, searched 3]
  after                      free list: [@16384 size:100 next:16666] [@16666 size:142 next:16832] [@16832 size:200 next:0]
```

(경고 0개.) 확인할 것:

- **[1]** 의 free list 가 원문 Fig 17.3~17.7 과 **모든 숫자가 일치**: 4088, 3980, 3764, free(16500), 그리고 Fig 17.7 의 순서 16600 → 16384 → 16492 → 16708. 병합이 없으니 메모리가 전부 비어도 **malloc(3800) 이 NULL**. (300B 는 마지막 3764 청크에서 성공 — 조각이 남아도 충분히 큰 게 하나 있으면 산다.)
- **[2]** 같은 할당/해제 순서에 **주소순 삽입 + 병합** 을 켜면: 가운데 free → 2조각, 첫 번째 free → 앞 이웃과 합쳐 208(= 100 + 8 + 100), 마지막 free → **4088 한 덩어리**. 이제 malloc(3800) 성공, 남은 노드 @20192 size 280(= 4088 − 3808).
- **[3]** 정책 비교가 4.2 절 손계산과 일치: BEST 는 200 에서 떼고 **42** 남김(탐색 3), WORST 는 300 에서 떼고 142(탐색 3), FIRST 는 300 에서 떼고 142(탐색 **2**).
- 이 할당기는 **정렬(alignment)을 안 한다**(16666 같은 홀수 주소가 나옴). 실제 malloc 은 16바이트 정렬이 필수 — arm64 에서 정렬 안 된 포인터에 `long double`/SIMD 접근이나 원자 연산을 하면 느려지거나 fault 가 난다.

### 7.2 버디 할당기 — `code/C17_buddy.c`

```c
// C17_buddy.c — 64KB 이진 버디 할당기: 분할(split)과 XOR 로 버디 찾기, 재귀 병합
// build: cc -Wall -Wextra -O0 code/C17_buddy.c -o .work/bin/C17_buddy
// 블록 크기 = 1KB << order (order 0..6 → 1KB..64KB). 오프셋은 64KB 영역 시작 기준.
#include <stdio.h>
#include <stdint.h>

#define KB 1024u
#define MAX_ORDER 6                      // 64KB
#define NBLK 64                          // 1KB 단위 블록 수

// free[order][i] = 1 이면 (i * 1KB<<order) 오프셋의 order 크기 블록이 free
static uint8_t freemap[MAX_ORDER + 1][NBLK];

static uint32_t bsize(int o) { return KB << o; }

static void dump(const char *tag) {
    printf("  %-22s free:", tag);
    for (int o = MAX_ORDER; o >= 0; o--)
        for (unsigned i = 0; i < NBLK >> o; i++)
            if (freemap[o][i]) printf(" [%uKB @%uKB]", bsize(o) / KB, i * bsize(o) / KB);
    printf("\n");
}

static int order_for(uint32_t n) {        // n 을 담는 가장 작은 2^k KB
    int o = 0;
    while (bsize(o) < n) o++;
    return o;
}

static int64_t buddy_alloc(uint32_t n) {
    int want = order_for(n);
    int o = want;
    while (o <= MAX_ORDER) {              // want 이상에서 free 블록 찾기
        for (unsigned i = 0; i < NBLK >> o; i++)
            if (freemap[o][i]) {
                freemap[o][i] = 0;
                uint32_t off = i * bsize(o);
                while (o > want) {        // 반으로 쪼개며 내려간다. 오른쪽 반은 free 로 남김
                    o--;
                    uint32_t right = off + bsize(o);
                    freemap[o][right / bsize(o)] = 1;
                    printf("    split -> %uKB @%uKB (사용 후보) + %uKB @%uKB (free)\n",
                           bsize(o) / KB, off / KB, bsize(o) / KB, right / KB);
                }
                printf("  alloc(%uKB) -> %uKB 블록 @%uKB (내부 단편화 %uKB)\n",
                       n / KB, bsize(want) / KB, off / KB, (bsize(want) - n) / KB);
                return off;
            }
        o++;
    }
    printf("  alloc(%uKB) -> 실패\n", n / KB);
    return -1;
}

static void buddy_free(uint32_t off, uint32_t n) {
    int o = order_for(n);
    printf("  free(%uKB 블록 @%uKB)\n", bsize(o) / KB, off / KB);
    while (o < MAX_ORDER) {
        uint32_t buddy = off ^ bsize(o);  // 핵심: 버디 주소는 딱 한 비트만 다르다
        printf("    buddy of @%-2uKB (%2uKB) = @%-2uKB  (0x%05x ^ 0x%05x = 0x%05x) -> %s\n",
               off / KB, bsize(o) / KB, buddy / KB, off, bsize(o), buddy,
               freemap[o][buddy / bsize(o)] ? "free, 병합" : "사용 중, 멈춤");
        if (!freemap[o][buddy / bsize(o)]) break;
        freemap[o][buddy / bsize(o)] = 0;
        if (buddy < off) off = buddy;
        o++;
    }
    freemap[o][off / bsize(o)] = 1;
}

int main(void) {
    freemap[MAX_ORDER][0] = 1;
    printf("[1] 원문 예제: 64KB 에서 7KB 요청\n");
    dump("init");
    int64_t a = buddy_alloc(7 * KB);
    dump("after alloc 7KB");

    printf("\n[2] 하나 더: 3KB 요청\n");
    int64_t b = buddy_alloc(3 * KB);
    dump("after alloc 3KB");

    printf("\n[3] 7KB 블록 반환: 버디(8KB @8KB)는 3KB 때문에 쪼개져 일부 사용 중 → 병합 없이 멈춤\n");
    buddy_free((uint32_t)a, 7 * KB);
    dump("after free 7KB");

    printf("\n[4] 3KB 블록 반환: 64KB 까지 연쇄 병합\n");
    buddy_free((uint32_t)b, 3 * KB);
    dump("after free 3KB");
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 code/C17_buddy.c -o .work/bin/C17_buddy && .work/bin/C17_buddy
[1] 원문 예제: 64KB 에서 7KB 요청
  init                   free: [64KB @0KB]
    split -> 32KB @0KB (사용 후보) + 32KB @32KB (free)
    split -> 16KB @0KB (사용 후보) + 16KB @16KB (free)
    split -> 8KB @0KB (사용 후보) + 8KB @8KB (free)
  alloc(7KB) -> 8KB 블록 @0KB (내부 단편화 1KB)
  after alloc 7KB        free: [32KB @32KB] [16KB @16KB] [8KB @8KB]

[2] 하나 더: 3KB 요청
    split -> 4KB @8KB (사용 후보) + 4KB @12KB (free)
  alloc(3KB) -> 4KB 블록 @8KB (내부 단편화 1KB)
  after alloc 3KB        free: [32KB @32KB] [16KB @16KB] [4KB @12KB]

[3] 7KB 블록 반환: 버디(8KB @8KB)는 3KB 때문에 쪼개져 일부 사용 중 → 병합 없이 멈춤
  free(8KB 블록 @0KB)
    buddy of @0 KB ( 8KB) = @8 KB  (0x00000 ^ 0x02000 = 0x02000) -> 사용 중, 멈춤
  after free 7KB         free: [32KB @32KB] [16KB @16KB] [8KB @0KB] [4KB @12KB]

[4] 3KB 블록 반환: 64KB 까지 연쇄 병합
  free(4KB 블록 @8KB)
    buddy of @8 KB ( 4KB) = @12KB  (0x02000 ^ 0x01000 = 0x03000) -> free, 병합
    buddy of @8 KB ( 8KB) = @0 KB  (0x02000 ^ 0x02000 = 0x00000) -> free, 병합
    buddy of @0 KB (16KB) = @16KB  (0x00000 ^ 0x04000 = 0x04000) -> free, 병합
    buddy of @0 KB (32KB) = @32KB  (0x00000 ^ 0x08000 = 0x08000) -> free, 병합
  after free 3KB         free: [64KB @0KB]
```

(경고 0개.) [1] 은 원문 그림 그대로 64 → 32 → 16 → 8KB. [3] 에서 7KB 블록을 돌려줘도 버디(8KB @8)가 3KB 할당 때문에 쪼개져 있어 **병합 없이 멈춘다**. [4] 에서 마지막 블록을 돌려주면 4 → 8 → 16 → 32 → **64KB 까지 연쇄 병합**. 매 단계 버디 주소가 `addr ^ size` 한 번으로 나온다 — 리스트 탐색이 없다.

### 7.3 OSTEP 시뮬레이터 — malloc.py

```text
cd .tools/ostep-homework/vm-freespace
python3 malloc.py -n 10 -H 0 -p BEST -s 0 -c
```

기본값: 힙 100바이트, 시작 주소 1000, 헤더 0, 정렬 없음, 리스트는 주소순, 병합 없음.

```text
ptr[0] = Alloc(3) returned 1000 (searched 1 elements)
Free List [ Size 1 ]: [ addr:1003 sz:97 ]

Free(ptr[0])
returned 0
Free List [ Size 2 ]: [ addr:1000 sz:3 ][ addr:1003 sz:97 ]

ptr[1] = Alloc(5) returned 1003 (searched 2 elements)
Free List [ Size 2 ]: [ addr:1000 sz:3 ][ addr:1008 sz:92 ]

Free(ptr[1])
returned 0
Free List [ Size 3 ]: [ addr:1000 sz:3 ][ addr:1003 sz:5 ][ addr:1008 sz:92 ]

ptr[2] = Alloc(8) returned 1008 (searched 3 elements)
Free List [ Size 3 ]: [ addr:1000 sz:3 ][ addr:1003 sz:5 ][ addr:1016 sz:84 ]

Free(ptr[2])
returned 0
Free List [ Size 4 ]: [ addr:1000 sz:3 ][ addr:1003 sz:5 ][ addr:1008 sz:8 ][ addr:1016 sz:84 ]

ptr[3] = Alloc(8) returned 1008 (searched 4 elements)
Free List [ Size 3 ]: [ addr:1000 sz:3 ][ addr:1003 sz:5 ][ addr:1016 sz:84 ]

Free(ptr[3])
returned 0
Free List [ Size 4 ]: [ addr:1000 sz:3 ][ addr:1003 sz:5 ][ addr:1008 sz:8 ][ addr:1016 sz:84 ]

ptr[4] = Alloc(2) returned 1000 (searched 4 elements)
Free List [ Size 4 ]: [ addr:1002 sz:1 ][ addr:1003 sz:5 ][ addr:1008 sz:8 ][ addr:1016 sz:84 ]

ptr[5] = Alloc(7) returned 1008 (searched 4 elements)
Free List [ Size 4 ]: [ addr:1002 sz:1 ][ addr:1003 sz:5 ][ addr:1015 sz:1 ][ addr:1016 sz:84 ]
```

해석(숙제 Q1): 병합이 없으니 free 할 때마다 리스트가 늘고, best fit 이 **딱 맞는 청크**(Alloc(8) → sz 8 @1008)를 잘 찾아 쓰지만 결국 **1바이트짜리 찌꺼기**(@1002, @1015)가 쌓인다. 리스트가 길어질수록 매번 전체(4개)를 탐색한다.

**WORST** (`-p WORST`, 같은 seed) — 뒷부분만:

```text
ptr[3] = Alloc(8) returned 1016 (searched 4 elements)
Free List [ Size 4 ]: [ addr:1000 sz:3 ][ addr:1003 sz:5 ][ addr:1008 sz:8 ][ addr:1024 sz:76 ]

Free(ptr[3])
returned 0
Free List [ Size 5 ]: [ addr:1000 sz:3 ][ addr:1003 sz:5 ][ addr:1008 sz:8 ][ addr:1016 sz:8 ][ addr:1024 sz:76 ]

ptr[4] = Alloc(2) returned 1024 (searched 5 elements)
Free List [ Size 5 ]: [ addr:1000 sz:3 ][ addr:1003 sz:5 ][ addr:1008 sz:8 ][ addr:1016 sz:8 ][ addr:1026 sz:74 ]

ptr[5] = Alloc(7) returned 1026 (searched 5 elements)
Free List [ Size 5 ]: [ addr:1000 sz:3 ][ addr:1003 sz:5 ][ addr:1008 sz:8 ][ addr:1016 sz:8 ][ addr:1033 sz:67 ]
```

(Q2) worst fit 은 딱 맞는 8 이 있는데도 **항상 가장 큰 꼬리 청크** 를 깎는다. 그 결과 리스트가 5개로 더 길어지고 큰 청크가 계속 줄어든다 — "큰 덩어리를 남기려던" 의도와 반대.

**FIRST** (`-p FIRST`) — 결과(주소, 리스트)는 BEST 와 완전히 같지만 탐색 수가 다르다:

```text
ptr[3] = Alloc(8) returned 1008 (searched 3 elements)
ptr[4] = Alloc(2) returned 1000 (searched 1 elements)
ptr[5] = Alloc(7) returned 1008 (searched 3 elements)
```

(Q3) BEST 는 각각 4, 4, 4 개를 봤지만 FIRST 는 3, 1, 3 개. 같은 결과를 **더 싸게** 얻었다 — 주소순 리스트에서는 first fit 이 자주 best fit 과 같은 선택을 한다.

**병합 켜기** (`-C`):

```text
python3 malloc.py -n 10 -H 0 -p BEST -s 0 -C -c
...
Free(ptr[0])
returned 0
Free List [ Size 1 ]: [ addr:1000 sz:100 ]
...
ptr[5] = Alloc(7) returned 1002 (searched 1 elements)
Free List [ Size 1 ]: [ addr:1009 sz:91 ]
```

free 할 때마다 즉시 합쳐져 리스트가 **항상 1개**, 탐색도 항상 1번.

**Q5 — 1000번 연산에서 병합의 효과** (`-n 1000`, 실패한 Alloc 수와 마지막 리스트 길이를 셌다):

```text
$ for C in "" "-C"; do out=$(python3 malloc.py -n 1000 -H 0 -p BEST -s 0 $C -c); \
    echo "flags[$C] fails=$(echo "$out" | grep -c 'returned -1') final=$(echo "$out" | grep 'Free List' | tail -1 | cut -c1-60)"; done
flags[] fails=177 final=Free List [ Size 31 ]: [ addr:1000 sz:2 ][ addr:1002 sz:1 ][
flags[-C] fails=29 final=Free List [ Size 1 ]: [ addr:1002 sz:98 ]
```

| 설정 | 실패한 할당 | 마지막 free list 길이 |
|---|---|---|
| 병합 없음 | **177** | 31 조각 |
| 병합 (-C) | **29** | 1 조각 |

병합이 없으면 시간이 갈수록 리스트가 1~2바이트 조각으로 가득 차서 큰 요청이 계속 실패한다. 병합 하나로 실패가 약 1/6 로 줄었다.

## 8. 펌웨어 엔지니어의 눈으로

- **SSD FW 의 버퍼 관리 = 분리 리스트 + slab**: 4KB/16KB 데이터 버퍼, NVMe 커맨드 컨텍스트, FTL 맵 캐시 엔트리를 크기별 고정 풀로 두고 free list 로 돌렸다. 탐색 O(1), 외부 단편화 0, 최악 지연이 보장된다. slab 의 "초기화된 상태로 보관" 도 그대로 — 디스크립터의 불변 필드(DMA 주소, 큐 ID)를 미리 채워 두면 핫패스가 짧아진다.
- **FTL 의 블록 할당은 이 장의 쌍둥이 문제**: 빈 NAND 블록 풀에서 어떤 블록을 쓸지(wear leveling 을 고려한 정책), 무효 페이지가 섞인 블록을 어떻게 모을지(GC = **압축**!). malloc 은 압축을 못 하지만 FTL 은 논리→물리 매핑을 쥐고 있어서 데이터를 옮길 수 있다 — 세그먼트를 옮길 수 있는 OS 와 같은 입장. 대신 GC 의 복사 비용이 write amplification 으로 돌아온다.
- **버디 할당기는 Linux 페이지 할당기의 핵심** 이고, 연속 물리 메모리가 필요한 DMA 버퍼(CMA)와 huge page 를 위해 쓰인다. 디바이스 드라이버에서 `dma_alloc_coherent` 로 큰 연속 버퍼가 실패하는 건 이 버디 시스템의 고차(order) 블록이 단편화로 바닥났기 때문이다.
- **AI 가속기 / GPU 메모리**: PyTorch CUDA caching allocator 는 크기별 bin(분리 리스트) + 블록 분할 + 인접 블록 병합을 디바이스 메모리 위에서 한다. "메모리는 남았는데 OOM" 은 바로 외부 단편화이고, `expandable_segments` 같은 옵션은 가상 주소 예약 + 물리 페이지 매핑으로 이 장 문제를 페이징으로 우회하는 것이다(Ch.18 예고).
- **헤더의 magic 검사** 는 FW 에서도 싸고 강력한 방어다. 펌웨어 풀 디스크립터에 owner/state/magic 을 두면 double free(상태가 이미 FREE), 다른 풀로의 반환(magic 불일치)을 assert 로 즉시 잡는다.

## 9. 면접 질문

### Q1. malloc 을 직접 구현한다면 어떤 자료구조와 정책을 쓰겠나?
<details>
<summary>답 보기</summary>

- 할당 청크 앞에 **헤더**(size + 플래그/magic), 빈 청크 안에 **free list 노드**(size, next/prev).
- 리스트는 **주소순** 으로 유지하거나 **경계 태그(boundary tag, 청크 끝에도 크기 기록)** 를 둬서 free 시 O(1) **병합**.
- 작은 크기는 **크기 클래스별 분리 리스트**(탐색 없이 O(1)), 큰 크기는 best/first fit 트리, 아주 큰 요청은 직접 **mmap**.
- **16바이트 정렬**, 멀티스레드면 **스레드별 캐시/아레나** 로 락 경합 회피.
이게 대략 glibc ptmalloc, jemalloc, tcmalloc 의 공통 뼈대다.

</details>

### Q2. best fit, worst fit, first fit 을 비교하라.
<details>
<summary>답 보기</summary>

**best fit**: 들어가는 것 중 가장 작은 청크. 낭비를 줄이려 하지만 **전체 탐색**, 그리고 쓸모없는 작은 찌꺼기를 많이 만든다.
**worst fit**: 가장 큰 청크. 큰 나머지를 남기려는 의도지만 연구상 **단편화가 더 심하고** 역시 전체 탐색.
**first fit**: 처음 맞는 청크. **빠르다**. 리스트 앞쪽이 잘게 쪼개지는 문제는 주소순 정렬로 완화.
실험(시뮬레이터 seed 0)에서도 first fit 이 best fit 과 같은 결과를 1/3~3/4 의 탐색으로 냈고, worst fit 은 리스트를 더 길게 만들었다. 결론: "모든 입력에 최선인 정책은 없다".

</details>

### Q3. 버디 할당기는 어떻게 버디를 O(1) 에 찾나? 단점은?
<details>
<summary>답 보기</summary>

블록 크기가 2의 거듭제곱이고 크기에 맞춰 정렬되어 있으므로, 크기 s 블록의 주소 a 의 버디는 **`a ^ s`**(크기에 해당하는 비트 하나만 뒤집기). 반환 시 버디가 free 면 합쳐서 크기 2s 로 올라가 반복.
단점: **내부 단편화**(요청을 2의 거듭제곱으로 반올림, 최악 거의 50%), 그리고 버디가 아닌 이웃끼리는 합칠 수 없어 **외부 단편화도 남는다**(이웃이 비어도 버디가 아니면 병합 불가).
Linux 는 페이지 단위 버디 위에 slab/SLUB 을 얹어 작은 객체의 내부 단편화를 해결한다.

</details>

### Q4. slab 할당기의 핵심 아이디어 두 가지는?
<details>
<summary>답 보기</summary>

1. **객체 크기별 캐시(분리 리스트)**: 자주 쓰는 커널 객체마다 전용 캐시를 두고, 페이지 크기 배수의 **slab** 을 받아 같은 크기 객체로 나눠 쓴다 → 탐색 없음, 단편화 최소. 빈 slab 은 메모리 압박 시 회수.
2. **초기화된 상태로 객체 보관**: free 된 객체를 생성자 실행 후 상태로 유지해 다음 할당에서 초기화를 건너뛴다(락 초기화 등 비싼 작업 절약).
추가로 slab 컬러링으로 캐시 라인 충돌을 줄이고, per-CPU 매거진으로 락을 줄인다.

</details>

### Q5. "메모리가 충분한데 할당이 실패한다" — 원인과 대책은?
<details>
<summary>답 보기</summary>

원인: **외부 단편화** — 빈 공간 합계는 크지만 연속 블록이 없다. 할당기에 병합이 없거나, 수명이 다른 객체가 섞여 큰 빈 공간 사이에 오래 사는 작은 객체가 박혀 있을 때.
대책: 병합 활성화/주소순 정렬, **수명별·크기별 분리 풀**(오래 사는 것과 짧게 사는 것을 다른 아레나에), 부팅 시 큰 버퍼 선할당, 압축이 가능한 계층이라면 압축(OS 의 페이지 compaction), 그리고 근본적으로는 **고정 크기 단위 + 주소 변환(페이징)** 으로 "연속" 요구 자체를 없애기.

</details>

## 10. 자가 점검 & 숙제

### 퀴즈 1. 헤더 8바이트, 4096바이트 힙(초기 노드 크기 4088)에서 malloc(200) 두 번 후 남은 free 노드 크기는?
<details>
<summary>답 보기</summary>

한 번에 208 씩 빠진다. 4088 − 2 × 208 = **3672**.

</details>

### 퀴즈 2. free list: 8, 25, 12, 40 (사용 가능 크기, 헤더 무시). 요청 10 에 대해 best/worst/first fit 이 고르는 청크와 남는 크기는?
<details>
<summary>답 보기</summary>

- best: 12 → 남음 **2**
- worst: 40 → 남음 **30**
- first: 8 은 작음, 25 → 남음 **15**

</details>

### 퀴즈 3. 128KB 버디 영역에서 주소 0x6000 에 있는 8KB 블록의 버디 주소는? 16KB 로 합쳐지면 그 블록의 주소와 버디는?
<details>
<summary>답 보기</summary>

8KB = 0x2000. 0x6000 ^ 0x2000 = **0x4000**. 합친 16KB 블록 주소 = min(0x6000, 0x4000) = **0x4000**, 그 버디 = 0x4000 ^ 0x4000 = **0x0000**.

</details>

### 퀴즈 4. malloc 라이브러리는 왜 압축(compaction)을 할 수 없나?
<details>
<summary>답 보기</summary>

C 프로그램이 받은 포인터를 **어디에 저장했는지(다른 변수, 레지스터, 구조체 안)** 할당기가 알 수 없어서, 청크를 옮기면 그 포인터들을 고칠 방법이 없다. GC 언어는 모든 참조를 알기 때문에 compacting GC 가 가능하다.

</details>

### 원문 Homework 중 꼭 해볼 것

- **Q4 (리스트 정렬 방식)**: `-l ADDRSORT`, `-l SIZESORT+`, `-l SIZESORT-` 로 바꿔 보기 → SIZESORT+ 에서는 **first fit 이 곧 best fit** 이 되고, SIZESORT- 에서는 first fit 이 곧 worst fit 이 된다는 걸 확인하는 문제. (정렬 유지 비용이 탐색 비용으로 옮겨 간다.)
- **Q6 (-P 할당 비율)**: `-P` 를 90 근처로 올리면 힙이 금방 차서 실패가 폭증하고, 10 근처로 내리면 리스트가 거의 항상 비어 있다 → **워크로드가 할당기 성능을 좌우** 한다는 결론.
- **Q7 (-A 로 단편화 만들기)**: 예를 들어 `-A +10,+10,+10,+10,+10,-0,-2,-4` 처럼 번갈아 해제해서 빈 조각을 만들고(실제로 `-H 0 -c` 로 돌리면 최종 리스트가 `[addr:1000 sz:10][addr:1020 sz:10][addr:1040 sz:10][addr:1050 sz:50]` — 1040 과 1050 은 붙어 있는데도 병합이 없어 따로 남는다), 정책·병합 옵션별로 free list 모양이 어떻게 달라지는지 비교 → **최악의 입력을 일부러 설계** 해 보는 문제.

## 11. 다음으로

- 다음 장: [Ch.18 페이징 입문](2026-09-30_C18_paging_intro.md) — 가변 크기를 버리고 고정 크기 페이지로: 외부 단편화를 근본적으로 피하는 길.
- 이전 장: [Ch.16 세그먼트](2026-09-30_C16_segmentation.md) · [Ch.14 메모리 API](2026-09-30_C14_memory_api.md)
- 참고 원문: [17. Free-Space Management](../book-md/C17_free_space_management.md)
