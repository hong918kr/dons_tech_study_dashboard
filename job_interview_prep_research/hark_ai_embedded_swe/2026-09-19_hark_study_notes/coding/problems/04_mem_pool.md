# 04. 고정 크기 블록 memory pool allocator

> **주제**: 정적 메모리 관리 · intrusive free list · O(1) alloc/free · **난이도**: 중급 · **목표 시간**: 35분
> **해설은 보지 말 것**: 먼저 `starters/04_mem_pool.c`를 채워 `make run N=04`로 통과시킨다.

## 면접관의 문장

"Our firmware has a hard rule: no `malloc` after boot. But we still need to hand out buffers at runtime — audio frames coming off the I2S DMA, BLE packets, log records. They're all the same size, 64 bytes, and there are never more than a few dozen of them live at once.

Write me a fixed-block memory pool. The caller gives you a static byte array at init time and you carve it into equal-sized blocks. `alloc` hands out one block, `free` gives it back. Both have to be O(1) — I want to be able to call them from an ISR, so no loops and no searching.

Don't add a separate metadata array; use the free blocks themselves to hold your bookkeeping. And tell me what happens when somebody frees a pointer twice, or frees a pointer that never came from your pool."

## 요구사항

1. `pool_init(p, storage, storage_bytes)`는 caller가 준 저장소를 `POOL_BLOCK_SIZE`(64) 바이트 블록으로 자르고 전부 free list에 엮는다. 만들 수 있는 블록 수는 `storage_bytes / sizeof(pool_block_t)`이다.
2. `pool_init`은 다음 경우 `-1`을 돌려주고 pool을 만들지 않는다: `p` 또는 `storage`가 NULL, `storage` 시작 주소가 `alignof(max_align_t)`의 배수가 아님, 블록을 하나도 만들 수 없음(`n == 0`), 블록 수가 `POOL_MAX_BLOCKS`(64) 초과. 성공하면 `0`.
3. `pool_alloc`은 블록 한 개의 시작 주소를 돌려준다. 남은 블록이 없으면 `NULL`. 내부에 반복문이 있으면 안 된다.
4. `pool_alloc`이 돌려주는 주소는 항상 `alignof(max_align_t)` 배수여야 한다. caller가 그 블록을 `double`이나 `uint64_t` 배열로 캐스팅해도 정렬 위반이 없어야 한다.
5. 반환된 블록은 `POOL_BLOCK_SIZE` 바이트 전체를 caller가 써도 된다. 이웃 블록이나 pool 내부 상태를 침범하면 안 된다.
6. `pool_free(p, ptr)`의 반환값은 정확히 이렇다. `POOL_OK`(0)은 정상 반납, `ptr == NULL`인 no-op도 여기에 포함된다. `POOL_ERR_FOREIGN`(-1)은 이 pool의 블록 시작 주소가 아닌 포인터다. `POOL_ERR_DOUBLE`(-2)은 이미 free 상태인 블록의 재반납이다.
7. `POOL_ERR_FOREIGN`으로 거부해야 하는 것은 세 가지다. pool 범위 밖 주소, 블록 중간을 가리키는 주소(`base + 1`, `base + 8` 등), 마지막 블록 바로 뒤의 one-past-end 주소.
8. 거부된 `pool_free` 호출은 `used`, `high_water`, free list 중 무엇도 바꾸지 않아야 한다. 특히 double free가 free list에 같은 블록을 두 번 넣으면 안 된다.
9. free는 LIFO다. 방금 반납한 블록이 다음 `pool_alloc`에서 그대로 나온다.
10. 회계 함수 넷을 제공한다. `pool_capacity`는 총 블록 수, `pool_used`는 현재 나가 있는 수, `pool_available`은 `capacity - used`, `pool_high_water`는 `used`의 역대 최댓값이다. `high_water`는 free를 해도 내려가지 않는다.
11. `pool_owns(p, ptr)`는 부수효과 없이 소유 여부만 `1`/`0`으로 판정한다.
12. 전부 alloc하고 전부 free하는 사이클을 100번 돌려도 용량이 줄지 않아야 한다. 즉 단편화가 0이다.

## 인터페이스

```c
#define POOL_BLOCK_SIZE  64u
#define POOL_MAX_BLOCKS  64u

#define POOL_OK           0
#define POOL_ERR_FOREIGN (-1)
#define POOL_ERR_DOUBLE  (-2)

/* free일 때는 링크, 사용 중일 때는 payload. max_align_t로 정렬을 끌어올린다. */
typedef union pool_block {
    union pool_block *next;
    max_align_t       align_dummy;
    unsigned char     data[POOL_BLOCK_SIZE];
} pool_block_t;

#define POOL_BITMAP_WORDS ((POOL_MAX_BLOCKS + 31u) / 32u)

typedef struct {
    pool_block_t *blocks;
    pool_block_t *free_head;
    uint32_t      capacity;
    uint32_t      used;
    uint32_t      high_water;
    uint32_t      in_use[POOL_BITMAP_WORDS];
} pool_t;

/* caller 저장소 선언용 */
#define POOL_STORAGE(name, nblocks) static pool_block_t name[(nblocks)]

int      pool_init(pool_t *p, void *storage, size_t storage_bytes);
void    *pool_alloc(pool_t *p);
int      pool_free(pool_t *p, void *ptr);
int      pool_owns(const pool_t *p, const void *ptr);

uint32_t pool_capacity(const pool_t *p);
uint32_t pool_used(const pool_t *p);
uint32_t pool_available(const pool_t *p);
uint32_t pool_high_water(const pool_t *p);
```

## 제약

- `malloc`, `calloc`, `realloc`, `free`, VLA, `alloca` 전부 금지. 메모리는 caller가 준 저장소가 전부다.
- 블록당 별도 메타데이터 배열을 두지 않는다. free 블록 안에 링크를 저장하는 intrusive free list여야 한다. 사용 중인 블록의 오버헤드는 0바이트다.
- `pool_alloc`과 `pool_free`는 반복문 없이 상수 시간. pool이 비었든 꽉 찼든 실행 사이클 수가 같아야 한다.
- `pool_init`만 예외적으로 블록 수에 비례하는 반복문을 가진다(부팅 때 한 번).
- 공유 상태(`free_head`, `used`, `high_water`, `in_use`)는 ISR과 task가 동시에 만질 수 있다고 가정한다. 갱신 구간은 `CRIT_ENTER()` / `CRIT_EXIT()` 매크로로 감싼다. 타깃에서는 PRIMASK 저장/복원, host 테스트에서는 no-op이다.
- 하드웨어·RTOS 헤더 금지. C11 표준 라이브러리만 쓴다.
- `cc -std=c11 -Wall -Wextra -O2`에서 경고 0개.

## 예시 동작

블록 4개짜리 pool에서 일어나는 일을 순서대로 적으면 이렇다.

```
pool_init(p, storage, 4*64)   -> 0     free: [0]->[1]->[2]->[3]->NULL   used=0 hw=0
a = pool_alloc(p)             -> &b[0] free: [1]->[2]->[3]->NULL        used=1 hw=1
b = pool_alloc(p)             -> &b[1] free: [2]->[3]->NULL             used=2 hw=2
c = pool_alloc(p)             -> &b[2] free: [3]->NULL                  used=3 hw=3
pool_free(p, b)               -> 0     free: [1]->[3]->NULL             used=2 hw=3
d = pool_alloc(p)             -> &b[1] free: [3]->NULL                  used=3 hw=3   (LIFO: b가 그대로 돌아옴)
e = pool_alloc(p)             -> &b[3] free: NULL                       used=4 hw=4
f = pool_alloc(p)             -> NULL  free: NULL                       used=4 hw=4   (고갈)
pool_free(p, a)               -> 0     free: [0]->NULL                  used=3 hw=4
pool_free(p, a)               -> -2    free: [0]->NULL                  used=3 hw=4   (double free, 상태 불변)
pool_free(p, (char*)c + 8)    -> -1    free: [0]->NULL                  used=3 hw=4   (블록 중간)
pool_free(p, &stack_var)      -> -1    free: [0]->NULL                  used=3 hw=4   (범위 밖)
pool_free(p, NULL)            -> 0     free: [0]->NULL                  used=3 hw=4   (no-op)
```

`hw`는 `high_water`다. `used`가 4까지 갔던 기록이 free 이후에도 남는 것을 확인하라.

## 스스로 점검할 질문

1. free 블록 안에 `next` 포인터를 저장해도 안전한 이유는 무엇인가? 그 블록을 누군가 아직 읽고 있으면 어떻게 되나?
2. `POOL_BLOCK_SIZE`를 60으로 바꾸면 어디서 무엇이 깨지는가? `_Static_assert` 중 어느 것이 먼저 걸리나?
3. `max_align_t` 멤버를 union에서 빼면 어떤 caller가 언제 죽는가? 그 버그는 x86 host 테스트에서 재현되는가?
4. double free 감지를 bitmap 대신 "free list를 훑어서 이미 있는지 본다"로 구현하면 무엇을 잃는가?
5. `pool_free`의 유효성 검사가 잡지 못하는 잘못된 사용은 무엇인가?
6. `used`를 `uint32_t` 하나로 관리할 때, alloc이 task에서 free가 ISR에서 동시에 일어나면 `used++`와 `used--`가 왜 위험한가?
7. `high_water`를 로그로 뽑아 pool 크기를 정한다면, 어떤 상황의 데이터를 모아야 믿을 수 있는 숫자가 되나?
8. `pool_alloc`이 반환한 블록의 내용은 무엇인가? caller가 초기화 없이 읽으면 무슨 값을 보게 되나?

## follow-up (면접관이 이어서 물을 것)

1. "Now the buffers aren't all the same size. How do you extend this?"
2. "Can you do this without disabling interrupts? What breaks if you try a lock-free stack here?"
3. "How would you detect a caller writing past the end of its block?"
4. "The pool runs out at 3am in the field. What does your firmware do, and how do you find out about it the next morning?"
5. "Why is `malloc` actually a problem here? Give me a concrete failure, not just 'fragmentation'."
