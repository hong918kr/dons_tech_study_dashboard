# T2-9 · Fixed-Block Memory Pool

> **ID**: T2-9 · **Priority**: P2 · **Pattern**: intrusive free list (stack) + used bitmap · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T2-9` / `./run.sh T2-9 sol`

## 1. Problem

On the robot, the telemetry task allocates one block per sensor message, fills it, and hands it to the radio task, which frees it after transmission. `malloc()` is banned (fragmentation, non-deterministic timing), so implement a **fixed-block memory pool**:

- Storage is static: `NUM_BLOCKS` (8) blocks of `BLOCK_SIZE` (32) bytes inside the `Pool` object.
- `pool_init()`: put every block on the free list, block 0 first.
- `pool_alloc()`: **O(1)**. Return a free block, or `NULL` when the pool is exhausted.
- `pool_free()`: **O(1)**. Return the block to the pool. It must detect
  - `POOL_ERR_FOREIGN` — the pointer is not the start of a block in this pool,
  - `POOL_ERR_DOUBLE` — the block is already free.
  - `pool_free(p, NULL)` is a no-op returning `POOL_OK` (like `free(NULL)`).
- `pool_free_count()`: number of free blocks.
- The free list is **intrusive**: the `next` pointer lives inside the free block itself. A used-block bitmap is allowed for the double-free check.

```c
#define BLOCK_SIZE 32u
#define NUM_BLOCKS 8u
enum { POOL_OK = 0, POOL_ERR_FOREIGN = -1, POOL_ERR_DOUBLE = -2 };

typedef union Block { union Block *next; uint8_t bytes[BLOCK_SIZE]; uint64_t align_; } Block;
typedef struct { Block blocks[NUM_BLOCKS]; Block *free_head; uint32_t used; uint32_t n_free; } Pool;

void     pool_init(Pool *p);
void    *pool_alloc(Pool *p);
int      pool_free(Pool *p, void *ptr);
uint32_t pool_free_count(const Pool *p);
```

### Example

```
pool_init        free list: B0 B1 B2 B3 B4 B5 B6 B7   free = 8
alloc x3         returns B0, B1, B2                   free = 5, used = 0x07
free(B1)         -> POOL_OK                           free = 6, used = 0x05
alloc            returns B1 (last freed, LIFO)
free(B1) again   -> POOL_ERR_DOUBLE
free(B0 + 1)     -> POOL_ERR_FOREIGN (not a block start)
```

### Constraints

- `NUM_BLOCKS <= 32` (one `uint32_t` bitmap), `BLOCK_SIZE >= sizeof(void *)` and a multiple of 8.
- Single-threaded for the base version (see Follow-up for ISR / multi-task use).

### Clarifying questions to ask (first 3 minutes)

1. Fixed block size only, or several size classes?
2. What should `pool_free` do on an invalid pointer — return an error, assert, or hard-fault?
3. Will alloc/free be called from an ISR or from several tasks at once?
4. Do blocks need a specific alignment (DMA buffers, `uint64_t` fields)?
5. Is LIFO reuse fine, or do we want FIFO to spread wear / catch use-after-free?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh T2-9`로 바로 테스트하세요. 테스트 6개가 들어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — free list는 스택</summary>

빈 블록들을 단일 연결 리스트로 잇고 `free_head`만 들고 있으면, alloc은 **pop**(`head = head->next`), free는 **push**(`b->next = head; head = b`)라서 둘 다 O(1)입니다. 연결 리스트 노드를 따로 만들 필요 없이 **빈 블록의 첫 8바이트를 `next` 포인터로 재사용**합니다(intrusive). 그래서 `Block`이 `union`입니다.

</details>

<details>
<summary>Hint 2 — 이 포인터가 우리 블록인가?</summary>

`offset = (uintptr_t)ptr - (uintptr_t)blocks`를 구해서
1. `0 <= offset < NUM_BLOCKS * BLOCK_SIZE` 인지(범위),
2. `offset % sizeof(Block) == 0` 인지(블록 시작점),
3. `i = offset / sizeof(Block)`.

포인터끼리 `<` 비교는 서로 다른 객체면 C에서 UB라서 정수(`uintptr_t`)로 바꿔 비교합니다.

</details>

<details>
<summary>Hint 3 — double free는 비트맵으로</summary>

free list를 훑어서 이미 있는지 보면 O(n)입니다. 대신 `uint32_t used`의 비트 i를 alloc 때 켜고 free 때 끕니다. free할 때 비트가 이미 0이면 double free입니다(→ [T2-6](../T2-6_bit_ops/index.html)의 비트 연산).

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

블록들은 메모리에 B0…B7 순서로 고정되어 있고, **빈 블록끼리만** `next`로 연결됩니다. alloc은 head에서 하나 떼어 내고, free는 돌려받은 블록을 head 앞에 붙입니다.

<div class="chart" data-src="chart1.svg" data-caption="free list 변화: ① init 직후 B0→…→B7, ② alloc 3번 뒤 head = B3, ③ free(B1) 뒤 head = B1 → B3. 초록 = free(첫 8바이트가 next), 보라 = 사용 중(32바이트 모두 payload)"></div>

사용 중인 블록에는 `next`가 없습니다. 사용자가 32바이트 전체를 덮어쓰기 때문입니다. 그래서 free list는 **빈 블록만** 이어야 하고, 사용 중 블록의 상태는 비트맵 `used`가 기억합니다.

<div class="chart" data-src="chart2.svg" data-caption="pool_free의 검사 순서: NULL → 범위 → 블록 경계 → 비트맵(double free) → push"></div>

### 4.2 손으로 따라가기 (trace)

테스트 1·4·5번을 한 흐름으로 이어 붙였습니다. `used`는 비트 i = Bi.

| 단계 | 호출 | 반환 | free list (head부터) | used | n_free |
|---|---|---|---|---|---|
| 0 | `pool_init` | – | B0 B1 B2 B3 B4 B5 B6 B7 | 0x00 | 8 |
| 1 | `pool_alloc` | B0 | B1 … B7 | 0x01 | 7 |
| 2 | `pool_alloc` | B1 | B2 … B7 | 0x03 | 6 |
| 3 | `pool_alloc` | B2 | B3 … B7 | 0x07 | 5 |
| 4 | `pool_free(B1)` | OK | **B1** B3 … B7 | 0x05 | 6 |
| 5 | `pool_alloc` | **B1** (LIFO) | B3 … B7 | 0x07 | 5 |
| 6 | `pool_free(B1)` | OK | B1 B3 … B7 | 0x05 | 6 |
| 7 | `pool_free(B1)` | **DOUBLE** (bit 1 = 0) | 변화 없음 | 0x05 | 6 |
| 8 | `pool_free(B0 + 1)` | **FOREIGN** (offset 1 % 32 ≠ 0) | 변화 없음 | 0x05 | 6 |

### 4.3 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `union Block`에 `uint64_t align_`을 넣어 블록이 8바이트 정렬되게 했습니다. 사용자가 `uint64_t` 타임스탬프나 `double`을 넣어도 정렬 오류(Cortex-M의 unaligned fault)가 나지 않습니다. C11이면 `_Alignas(8)`도 됩니다.
- `b - p->blocks`는 같은 배열 안 포인터 뺄셈이라 정의된 연산이고 결과가 블록 인덱스입니다.
- double free를 검사하지 않고 push하면 같은 블록이 리스트에 두 번 들어가 **순환 리스트**가 생기고, 이후 alloc 두 번이 같은 블록을 돌려줍니다. 비트맵 검사가 이것을 막습니다(테스트 4번).
- 테스트에서 `Pool`을 `static`으로 둔 이유: 256바이트 이상을 스택에 올리지 않기 위함입니다. 실제 펌웨어에서도 풀은 전역(`.bss`)에 둡니다.

### 4.4 복잡도

- `pool_alloc` / `pool_free` / `pool_free_count`: **O(1)**, 루프 없음 → 최악 실행 시간이 고정.
- `pool_init`: O(NUM_BLOCKS), 부팅 때 한 번.
- 메모리: 블록 256바이트 + 관리용 12~16바이트. 블록마다 헤더가 없습니다(intrusive).

---

## 5. 임베디드 관점으로 말할 것

- **왜 malloc을 안 쓰나**: 힙 단편화로 오래 돌수록 큰 할당이 실패할 수 있고, 실행 시간이 입력에 따라 달라 실시간 보장이 어렵습니다. MISRA C도 동적 할당을 금지합니다. 고정 블록 풀은 단편화가 원천적으로 없고 O(1)입니다.
- **ISR·멀티태스크**: alloc/free의 "읽고-바꾸고-쓰기"는 원자적이지 않습니다. 짧은 critical section(인터럽트 off)으로 감싸거나, RTOS의 메모리 풀 API(FreeRTOS는 정적 할당 + 큐, Zephyr `k_mem_slab`)를 씁니다.
- **디버깅 보조**: free할 때 블록을 `0xDD`로 채우고(poison), alloc할 때 검사하면 use-after-free를 잡을 수 있습니다. 최저 여유 블록 수(high-water mark)를 기록하면 풀 크기를 정할 근거가 됩니다.
- **소유권 이동**: 텔레메트리 태스크가 alloc → 큐에 포인터만 넣음 → 라디오 태스크가 전송 후 free. 데이터 복사 없이 포인터만 넘기는 zero-copy 패턴입니다(→ [T2-1](../T2-1_ring_buffer/index.html)).

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Make it safe to call from an ISR and a task. | alloc/free 본문을 `primask = __get_PRIMASK(); __disable_irq(); … __set_PRIMASK(primask);`로 감쌉니다. 몇 명령뿐이라 인터럽트 지연이 짧습니다. 락프리로 하려면 LDREX/STREX로 head를 CAS하되 ABA 문제를 태그 카운터로 막아야 합니다. |
| Support several block sizes (32, 128, 512 bytes). | 크기별로 풀을 여러 개 두고 요청 크기 이상인 가장 작은 풀에서 꺼냅니다(size class, slab). 비어 있으면 한 단계 큰 풀로 넘어갈지는 정책으로 정합니다. |
| How do you detect use-after-free? | free 시 블록을 poison 값으로 채우고 alloc 시 `next` 다음 바이트들이 여전히 poison인지 검사합니다. 또는 FIFO로 재사용해서 방금 해제한 블록이 바로 재사용되지 않게 합니다. |
| What if NUM_BLOCKS is 100? | 비트맵을 `uint32_t used[(N + 31) / 32]`로 늘리고 `used[i >> 5] & (1u << (i & 31))`로 접근합니다. |
| Can you avoid the bitmap entirely? | 블록 앞에 매직 값 헤더를 두거나(사용 중 `0xA110C8ED`, 빈 블록 `0xF4EEF4EE`) free list를 훑는 O(n) 검사를 디버그 빌드에서만 켭니다. 비트맵이 가장 싸고 확실합니다. |

## 7. 자주 하는 실수

- 빈 블록이 없을 때 `free_head->next`를 그대로 읽음 → NULL 역참조(테스트 2번)
- `next` 포인터를 사용 중 블록에도 있다고 가정 → 사용자가 payload를 쓰면 리스트가 깨짐(테스트 3번)
- double free 검사 없음 → 리스트에 같은 블록이 두 번 들어가 순환(테스트 4번)
- 포인터 범위를 `ptr < p->blocks` 같은 포인터 비교로 검사 → 다른 객체 포인터면 UB. `uintptr_t`로 비교
- 블록 경계(`% sizeof(Block)`) 검사를 빼서 `B0 + 1` 같은 내부 포인터를 받아들임(테스트 5번)
- `1u << i` 대신 `1 << i` → i = 31에서 부호 있는 오버플로(UB)
