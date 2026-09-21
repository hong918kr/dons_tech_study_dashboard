# 04. 고정 크기 블록 memory pool allocator — 해설

> **문제**: [problems/04_mem_pool.md](../problems/04_mem_pool.html) · **답안**: `solutions/04_mem_pool.c`
> **이 노트를 다 읽으면**: (1) free 블록 안에 링크를 심는 intrusive free list를 그림으로 그리고 alloc/free를 손으로 따라갈 수 있다. (2) 블록 크기와 정렬이 어긋났을 때 몇 번째 블록부터 어떻게 깨지는지 주소 단위로 설명할 수 있다. (3) 왜 펌웨어가 `malloc`을 금지하는지 "단편화"라는 단어 없이 구체적인 실패로 말할 수 있다.

---

## 0. 한 문장으로

정적 배열 하나를 같은 크기 블록으로 자르고 비어 있는 블록의 첫 8바이트에 "다음 빈 블록의 주소"를 적어 사슬을 만들면, 할당은 사슬의 머리를 떼는 것이고 반납은 머리에 다시 붙이는 것이라 둘 다 포인터 대입 두 번으로 끝난다.

## 1. 왜 이 자료구조가 필요한가 — 하드웨어에서 출발

always-on 오디오 기기에는 "같은 크기 버퍼를 계속 돌려쓰는" 자리가 세 군데 나온다.

```
   MIC ──I2S──> [DMA]  ──┐
                          │  frame 64B                ┌────────────┐
                          ├──────────────────────────>│   MEMORY   │
   BLE RX ─────> [radio] ─┤  packet 64B               │    POOL    │
                          │                           │  (static   │
   log()  ─────> [app]  ──┘  record 64B               │   .bss)    │
                                                      └─────┬──────┘
         alloc()                                     free() │
            ▲                                               ▼
   ┌────────┴────────┐   queue of pointers       ┌───────────────────┐
   │ producer (ISR)  │ ─────────────────────────>│  consumer task    │
   └─────────────────┘   [ptr][ptr][ptr] ...     │ (DSP/추론/flash)  │
                                                 └───────────────────┘
```

조건이 셋이다. 생산자가 ISR이라 I2S half-transfer 인터럽트나 BLE 콜백 안에서 "버퍼 하나 주세요"를 불러야 한다. 수명이 엇갈려서, 3번 프레임을 flash에 쓰는 동안 4번과 5번이 도착한다. ping-pong(2칸)이나 ring buffer로는 부족하고 "여러 개를 들고 있다가 끝나는 순서대로 반납"이 필요하다. 그리고 총량이 고정이다. 오디오에 쓸 RAM이 2KB면 64바이트 블록 32개가 전부고, 이 숫자는 컴파일 타임에 정해지며 넘으면 넘었다는 사실을 알아야 한다.

이게 고정 블록 pool의 요구사항 전부다. Zephyr의 `k_mem_slab`, Linux 커널의 slab allocator가 같은 아이디어다. 저장소가 `static` 배열 하나이므로 최종 RAM 사용량이 링커 맵에 그대로 찍히고, heap 섹션이 0바이트여도 되니 "heap이 stack을 침범해서 죽는" 버그가 원천적으로 사라진다.

## 2. 먼저 그림으로 이해하기

블록 4개짜리 pool로 전 과정을 그린다. 주소는 예시로 `0x2000_0000`부터, 블록 크기는 64바이트다.
### 그림 1 — `pool_init` 직후

```
             0x20000000   0x20000040   0x20000080   0x200000C0
             ┌──────────┬──────────┬──────────┬──────────┐
  g_storage  │ block 0  │ block 1  │ block 2  │ block 3  │
             └──────────┴──────────┴──────────┴──────────┘
              [0..63]    [64..127]  [128..191] [192..255]
  각 블록의 앞 8바이트(next)에 적힌 값:
             ┌──────────┬──────────┬──────────┬──────────┐
   next:     │0x20000040│0x20000080│0x200000C0│   NULL   │
             └──────────┴──────────┴──────────┴──────────┘
  free_head ──> [0] ──> [1] ──> [2] ──> [3] ──> NULL
  used = 0   high_water = 0   capacity = 4   bitmap: 0 0 0 0
```

링크를 위한 별도 공간이 없다. 블록 0의 페이로드 첫 8바이트를 링크로 재사용할 뿐이고, 블록이 나가면 그 8바이트는 caller 것이 되고 돌아오면 다시 링크 자리가 된다. intrusive(침습적) free list라고 부르는 이유다.

### 그림 2 — `a = pool_alloc(p)` — 머리를 뗀다

`b = free_head`, `free_head = b->next`. 딱 두 줄이다.

```
  before:  free_head ──> [0] ──> [1] ──> [2] ──> [3] ──> NULL
                          ^ 이 칸을 caller에게 준다.
                            주기 전에 이 칸의 next(=&[1])를 free_head로 옮긴다
  after:   free_head ─────────> [1] ──> [2] ──> [3] ──> NULL
           a = 0x20000000
           ┌──────────┬──────────┬──────────┬──────────┐
           │ ##사용중## │ next=[2] │ next=[3] │   NULL   │
           └──────────┴──────────┴──────────┴──────────┘
  used = 1   high_water = 1   bitmap: 1 0 0 0
```

블록 0의 내용은 이제 의미가 없다. 이전 링크 값 `0x20000040`이 그대로 남아 있으니 caller가 초기화 없이 읽으면 쓰레기 포인터를 본다. `pool_alloc`은 `calloc`이 아니다.

### 그림 3 — `b`, `c`까지 세 번 alloc

```
  free_head ───────────────────────────> [3] ──> NULL
           ┌──────────┬──────────┬──────────┬──────────┐
           │ ##a##    │ ##b##    │ ##c##    │   NULL   │
           └──────────┴──────────┴──────────┴──────────┘
             a=..000     b=..040     c=..080    used=3  hw=3  bitmap: 1 1 1 0
```

### 그림 4 — `pool_free(p, b)` — 머리에 붙인다

`b->next = free_head`, `free_head = b`. 역시 두 줄이다.

```
  step 1: b의 앞 8바이트에 현재 head 주소(&[3])를 적는다
           ┌──────────┬──────────┬──────────┬──────────┐
           │ ##a##    │ next=[3] │ ##c##    │   NULL   │
           └──────────┴──────────┴──────────┴──────────┘
                           │
  step 2: free_head를 b로  └────────────┐
                                        ▼
  free_head ──> [1] ──> [3] ──> NULL
  used = 2   high_water = 3 (내려가지 않는다)   bitmap: 1 0 1 0
```

사슬 순서가 `[1] -> [3]`으로 주소 순이 아니다. free list는 주소 정렬을 전혀 유지하지 않고, 유지할 필요도 없다. 모든 블록이 같은 크기라 어느 것을 줘도 똑같기 때문이다. 이 한 문장이 고정 블록 pool에 외부 단편화가 없는 이유 전부다.

### 그림 5 — `d = pool_alloc(p)` — LIFO 재사용, 그리고 고갈

```
  free_head ──> [1] ──> [3] ──> NULL
                 ^ 방금 반납한 그 블록이 바로 다시 나온다 → d == b == 0x20000040
  free_head ──> [3] ──> NULL                     used = 3   bitmap: 1 1 1 0
  한 번 더 alloc하면 free_head ──> NULL
           ┌──────────┬──────────┬──────────┬──────────┐
           │ ##a##    │ ##d##    │ ##c##    │ ##e##    │
           └──────────┴──────────┴──────────┴──────────┘
                                                 used = 4   bitmap: 1 1 1 1
  pool_alloc(p) -> free_head가 NULL이므로 NULL 반환.
                   블록을 못 준 것이지 크래시가 아니다. caller가 반드시 확인해야 한다.
```

방금 반납한 블록을 바로 주는 건 캐시 관점에서도 이득이다. 그 라인이 아직 따뜻하다.

### 그림 6 — 블록 크기가 정렬의 배수가 아니면

`POOL_BLOCK_SIZE`를 64 대신 60으로 바꿨다고 하자. `alignof(max_align_t)`가 8인데 60은 8의 배수가 아니다.

```
  base = 0x20000000  (8의 배수, 여기까진 멀쩡)

  블록   시작 주소      주소 % 8     8바이트 접근이 정렬되어 있나?
  ────────────────────────────────────────────────────────────
  [0]    0x20000000       0         OK
  [1]    0x2000003C       4         X   ← 여기서부터 깨진다
  [2]    0x20000078       0         OK
  [3]    0x200000B4       4         X
  블록 [1]의 앞 8바이트에 next 포인터를 쓰면:
         0x20000038   0x2000003C   0x20000040   0x20000044
         ┌────┬────┬────┬────┬────┬────┬────┬────┐
  워드경계│         │         │         │         │
         └────┴────┴────┴────┴────┴────┴────┴────┘
                    ^^^^^^^^^^^^^^^^^^^^
                    8바이트 next가 워드 경계를 가로지른다
```

플랫폼마다 결과가 다른 것이 이 버그의 최악인 점이다.

| 플랫폼 | 정렬 안 된 8바이트 접근 | 증상 |
|---|---|---|
| x86-64 (host 테스트) | 하드웨어가 처리 | 아무 일도 없다. 테스트 전부 통과 |
| Cortex-M3/M4 `LDR`/`STR` | 대부분 허용 | 통과. 단 `LDRD`/`STRD`/`LDM`은 UsageFault |
| Cortex-M0/M0+ | 불가 | 즉시 HardFault |
| Cortex-M4F `VLDR` (double) | 정렬 필요 | caller가 `double`을 쓰는 순간 UsageFault |
| DMA 엔진 | 보통 워드 정렬 요구 | 전송이 조용히 잘못된 주소로 간다 |

host 테스트는 초록불인데 현장 보드에서만 죽는다. 그래서 런타임 검사가 아니라 `_Static_assert(POOL_BLOCK_SIZE % alignof(max_align_t) == 0u, ...)`로 못박아 컴파일 자체를 막는다. 60을 쓰고 싶으면 64로 올림하는 것이 답이고, 그 4바이트가 내부 단편화(internal fragmentation)다. 블록당 4바이트 낭비를 받아들이는 대신 외부 단편화와 HardFault를 동시에 없앤다.

## 3. 잘못된 구현부터 보기
### 3-1. 순진한 구현 — `malloc`을 그냥 쓴다

```
  부팅 직후 heap (연속 640바이트)
  ┌────────────────────────────────────────────────────────┐
  │////////////////// 전부 free ///////////////////////////│
  └────────────────────────────────────────────────────────┘
  3시간 뒤, 64B 오디오 / 100B BLE / 20B 로그가 섞여 할당·해제된 결과
  ┌──┬────┬──┬──────┬──┬───┬──┬─────┬──┬────┬──┬────┬──┬───┐
  │##│free│##│ free │##│fre│##│free │##│free│##│free│##│fr │
  └──┴────┴──┴──────┴──┴───┴──┴─────┴──┴────┴──┴────┴──┴───┘
     40B     50B      30B    48B      55B    44B    35B
  free 총합 = 302바이트. 그런데 malloc(64)가 실패한다.
  연속된 64바이트가 어디에도 없기 때문이다.  ← 외부 단편화

  ISR 마감 20us:
  I2S 인터럽트 ─> malloc(64) ─> free list 탐색 ...
                                 heap이 깨끗하면 2us
                                 3시간 돌린 뒤엔 40us (칸 200개 탐색)
                                        ▼
                        다음 I2S 인터럽트가 겹친다 → 프레임 유실
```

`malloc`은 free list를 훑어 맞는 칸을 찾고, 쪼개고, 인접 free 블록을 병합한다. 실행 시간이 heap 상태에 의존하므로 최악이 언제인지 아무도 모른다. 거기에 newlib의 `malloc`은 내부 mutex를 쓴다. ISR에서 mutex를 잡으면 RTOS가 죽거나 재진입 버그가 생긴다.

**교훈**: 펌웨어에서 `malloc`이 위험한 진짜 이유는 느려서가 아니라 **언제 실패할지, 얼마나 걸릴지 아무도 증명할 수 없어서**다. 고정 블록 pool은 그 둘을 컴파일 타임 상수로 바꾼다.

### 3-2. 반쯤 맞는 구현 — 별도 `used[]` 배열 + 선형 탐색

```
  for (i = 0; i < N; i++) if (!used[i]) { used[i] = true; return &blocks[i]; }
  블록 32개 중 31개가 이미 사용 중일 때 alloc:
  used[]: 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 0
          └──────────────────── 31번 비교 ─────────────────────────┘  ^찾음
  빈 pool일 때 1번, 꽉 찬 pool일 때 32번.
  → 실행 시간이 상태에 의존한다. ISR 최악 지연시간을 계산할 수 없다.
```

게다가 블록당 메타데이터가 pool과 별도로 필요하다. intrusive free list는 둘을 동시에 해결한다. 항상 O(1)이고, 사용 중인 블록의 오버헤드는 0바이트다.

### 3-3. 흔한 함정 — double free를 검사하지 않는다

S01 드릴의 기본형은 범위 검사만 하고 double free는 못 잡는다. 못 잡으면 이렇게 된다.

```
  상태:  free_head ──> [3] ──> NULL,  블록 [1]은 caller가 들고 있음

  pool_free(p, &b[1])   (정상)     free_head ──> [1] ──> [3] ──> NULL

  pool_free(p, &b[1])   (실수로 한 번 더)
         b[1].next = free_head;   /* free_head가 지금 &b[1]이다! */
         free_head = &b[1];

         결과:  free_head ──> [1] ──┐
                                ^   │   자기 자신을 가리키는 cycle
                                └───┘

  이제 pool이 무한한 블록을 가진 것처럼 보인다:
  x = pool_alloc(p) -> &b[1]    free_head = b[1].next = &b[1]
  y = pool_alloc(p) -> &b[1]    같은 블록!   z, w, ... 계속 같은 블록!

  오디오 task는 x를, BLE task는 y를 자기 것이라 믿고 동시에 쓴다.
  → 오디오에 BLE 패킷이 섞여 나온다. 크래시는 안 난다.
  → 몇 시간 뒤 "소리가 이상하다"는 버그 리포트만 온다.
```

`used`는 4, 3, 2로 내려가다 `0 - 1 = 0xFFFFFFFF`로 언더플로한다. 회계 자체가 거짓말을 하므로 로그를 봐도 원인을 못 찾는다.

**교훈**: double free는 즉시 크래시가 아니라 **조용한 데이터 손상**으로 나타난다. 그래서 1비트짜리 검사가 값어치를 한다.

## 4. 한 줄씩 만들기

### 4-1. 블록 타입과 컴파일 타임 검증

```c
typedef union pool_block {
    union pool_block *next;              /* free 상태: 다음 free 블록 */
    max_align_t       align_dummy;       /* 정렬 강제용. 읽거나 쓰지 않는다 */
    unsigned char     data[POOL_BLOCK_SIZE]; /* 사용 중: caller의 payload */
} pool_block_t;

_Static_assert(POOL_BLOCK_SIZE >= sizeof(void *),
               "block too small to hold a free-list link");
_Static_assert(POOL_BLOCK_SIZE % alignof(max_align_t) == 0u,
               "block size must be a multiple of max alignment");
_Static_assert(sizeof(pool_block_t) == POOL_BLOCK_SIZE,
               "union must not add padding");
```

`next`는 free 상태에서만 의미가 있다. 사용 중인 블록에서 읽으면 caller 데이터를 포인터로 해석하는 꼴이다. `align_dummy`는 한 번도 읽거나 쓰지 않고 오직 union의 정렬을 `alignof(max_align_t)`로 끌어올리려고 있다. `data`는 `unsigned char`라 어떤 타입으로든 캐스팅해 쓸 수 있다.

assert 셋은 각각, 블록이 포인터보다 작으면 링크를 못 넣으므로 4바이트 블록 pool 시도를 즉시 막고, 그림 6의 정렬 붕괴를 막고, 누가 union에 멤버를 추가해 `sizeof`가 72로 늘어났을 때 빌드를 세운다. 셋 다 없으면 "동작하는 것처럼 보이는" 런타임 버그가 된다.  **없으면**: `align_dummy`를 빼면 union 정렬이 `alignof(void *)`로 떨어져 caller가 블록을 `double` 배열로 쓰는 순간 M4F에서 UsageFault가 난다. x86 host 테스트는 100% 통과하고 타깃에서만 죽는다. 그림 6의 표를 다시 보라.

### 4-2. `pool_init` — 사슬 엮기

```c
    if (((uintptr_t)storage % alignof(max_align_t)) != 0u) {
        return -1;
    }
    size_t n = storage_bytes / sizeof(pool_block_t);
    if (n == 0u || n > POOL_MAX_BLOCKS) {
        return -1;                       /* 한 블록도 못 만들거나 상한 초과 */
    }
    pool_block_t *b = (pool_block_t *)storage;
    for (size_t i = 0u; i + 1u < n; i++) {
        b[i].next = &b[i + 1u];
    }
    b[n - 1u].next = NULL;
```

정렬 검사가 필요한 이유는 `POOL_STORAGE` 매크로를 쓰면 컴파일러가 맞춰 주지만 caller가 `static uint8_t buf[2048]`을 넘기면 어긋날 수 있어서다. 나머지 바이트는 버린다. 2050바이트를 주면 32블록을 만들고 2바이트를 남긴다. 못 쓰는 꼬리를 억지로 쓰려다 경계를 넘는 것보다 낫다. 루프 조건이 `i + 1 < n`인 것은 안에서 `&b[i+1]`을 쓰기 때문이고, 마지막 블록은 루프 밖에서 `NULL`로 끝낸다.  **없으면**: `n == 0` 검사를 빼면 빈 저장소를 줬을 때 `n`이 `size_t`라 `b[SIZE_MAX].next = NULL`을 실행한다. 즉사한다.

### 4-3. `pool_alloc` — 머리 떼기

```c
    CRIT_ENTER();
    pool_block_t *b = p->free_head;
    if (b != NULL) {
        p->free_head = b->next;          /* 머리를 다음 칸으로 옮긴다 */
        uint32_t idx = (uint32_t)(b - p->blocks);
        pool_bitmap_set(p, idx);
        p->used++;
        if (p->used > p->high_water) {
            p->high_water = p->used;
        }
    }
    CRIT_EXIT();
    return b;
```

`b - p->blocks`는 포인터 뺄셈이 곧 블록 인덱스라는 점을 쓴다. `sizeof`가 64, 2의 거듭제곱이라 컴파일러가 나눗셈 대신 shift를 쓴다. 블록 크기를 2의 거듭제곱으로 잡는 실용적 이유다. `high_water` 갱신은 `used++` 직후에만 하고 free에서는 절대 건드리지 않는다. 최댓값 기록이니까.  **없으면**: `p->free_head = b->next`를 빼먹으면 모든 alloc이 같은 주소를 준다. 3-3의 cycle과 증상이 같다.

### 4-4. `pool_owns` — 포인터 검문소

```c
    uintptr_t base = (uintptr_t)p->blocks;
    uintptr_t end  = base + (uintptr_t)p->capacity * sizeof(pool_block_t);
    uintptr_t a    = (uintptr_t)ptr;

    if (a < base || a >= end) {
        return 0;                        /* 범위 밖 = 남의 메모리 */
    }
    if (((a - base) % sizeof(pool_block_t)) != 0u) {
        return 0;                        /* 블록 중간을 가리킴 */
    }
    return 1;
```

```
        base                                            end
         ▼                                               ▼
         ┌──────────┬──────────┬──────────┬──────────┐
         │ block 0  │ block 1  │ block 2  │ block 3  │
         └──────────┴──────────┴──────────┴──────────┘
         ^     ^          ^                          ^
       OK│     │X 중간    │OK                        │X one-past-end
         │     │ (offset % 64 != 0)                  │ (a >= end)
         │     └─ free(ptr+8) 같은 실수: "구조체 멤버 주소를 free"
         └─ 정상

   다른 pool의 블록, 스택 변수 주소 → a < base 또는 a >= end 로 걸린다
```

`a >= end`에서 `>=`가 중요하다. `end`는 마지막 블록의 한 칸 뒤라 유효하지 않다.  **없으면**: 범위 검사를 빼면 스택 주소를 free했을 때 스택에 `free_head` 값을 써 넣는다. 반환 주소를 덮어써 함수가 엉뚱한 곳으로 돌아간다. 추적이 거의 불가능한 부류다.

### 4-5. `pool_free` — bitmap으로 double free를 막고 머리에 붙이기

```c
    CRIT_ENTER();
    if (!pool_bitmap_test(p, idx)) {
        rc = POOL_ERR_DOUBLE;
    } else {
        pool_bitmap_clear(p, idx);
        b->next      = p->free_head;
        p->free_head = b;
        p->used--;
        rc = POOL_OK;
    }
    CRIT_EXIT();
```

bitmap은 블록당 1비트다. 64블록이면 `uint32_t` 두 개, 8바이트. 그 8바이트로 얻는 것을 비교하면 이렇다.

| 방법 | 비용 | 시간 | 정확도 |
|---|---|---|---|
| 검사 안 함 | 0 | O(1) | 못 잡음 |
| free list 순회 | 0 | O(n) — ISR에서 못 씀 | 정확 |
| poison 패턴(`0xDEADBEEF`) | 4바이트/블록 | O(1) | caller 데이터가 우연히 같으면 오탐 |
| **bitmap 1비트/블록** | **1비트/블록** | **O(1)** | **정확** |

검사와 갱신이 같은 critical section 안에 있어야 한다. 검사를 밖에 두면 그 사이 ISR이 끼어들어 검사 결과가 낡아 버린다(TOCTOU).  **없으면**: 3-3의 cycle이 생기고 두 task가 같은 블록을 동시에 쓴다.

## 5. 전체 코드 읽기

`solutions/04_mem_pool.c`는 설정 상수 → 블록 union과 `_Static_assert` 3개 → `pool_t` 서술자 → critical section 매크로 → bitmap 도우미 → 공개 API → 테스트 10개 순서다. 눈여겨볼 대목 셋만 꼽는다. 첫째, `pool_t`가 저장소를 **소유하지 않고 가리키기만** 한다. `blocks`는 caller 배열의 주소다. 덕분에 같은 코드로 pool을 여러 개 만들 수 있고(테스트 7이 확인한다), 저장소를 어느 RAM 섹션에 둘지를 caller가 결정한다. DMA 접근 가능한 SRAM에만 버퍼를 둬야 하는 MCU에서 이건 필수적인 유연성이다.

둘째, `pool_alloc`이 반환 직전에 블록을 지우지 않는다. 지우면 alloc마다 `memset` 64바이트가 붙는다. ISR에서 부르는 함수에 그 비용을 기본값으로 넣지 않는다. 보안상 지워야 하면 `pool_free`에서 지우는 게 맞다. 반납은 대개 덜 급하다.

셋째, 테스트 4가 "블록 전체를 자기 번호로 칠하고 **전부 칠한 뒤에** 검사"하는 형태인 이유.

```
  칠하고 바로 검사:  [A A A A][? ? ?]  → A 통과
                     [A A A B][B B B]  → B 통과. A가 망가진 걸 아무도 안 본다.
  다 칠한 뒤 검사:   [A A A B][B B B C][C C C]
                     → A 검사에서 4번째 칸이 B임이 드러난다
```

## 6. 동시성·메모리 관점
### 6-1. critical section이 없으면

`free_head` 갱신은 읽기-수정-쓰기(RMW)라 원자적이지 않다.

```
  초기:  free_head ──> [0] ──> [1] ──> NULL   시간 ───────────────────────>
  task:   b = free_head  (=[0])
          │              <<< I2S 인터럽트 >>>
          │              ISR:  b2 = free_head  (=[0])  ← 같은 걸 읽었다
          │                    free_head = b2->next = [1]
          │                    used = 1
          │              <<< 복귀 >>>
          ▼
          free_head = b->next = [1]   ← ISR이 한 일을 덮어쓴다
          used = 1                     ← 2여야 하는데 1

  결과: task도 [0], ISR도 [0]을 받았다. 같은 블록을 둘이 동시에 쓴다.
```

`used++`만 따로 봐도 같다. Cortex-M에서 `p->used++`는 세 명령이고 그 사이 어디서든 인터럽트가 들어온다.

```
  task:  LDR r0, [used]   (r0 = 5)
         <<< ISR: used를 6으로 만들고 복귀 >>>
         ADDS r0, #1      (r0 = 6)
         STR r0, [used]   (used = 6)   ← ISR의 증가가 사라졌다. 7이어야 한다.

  #define CRIT_ENTER() uint32_t _pm = __get_PRIMASK(); __disable_irq()
  #define CRIT_EXIT()  __set_PRIMASK(_pm)
```

저장/복원 형태인 이유는 **중첩**이다. 이미 인터럽트가 꺼진 ISR 안에서 `pool_alloc`을 부를 때, 단순히 `__enable_irq()`로 나가는 구현은 ISR 도중에 인터럽트를 켜 버린다. 이전 PRIMASK를 복원하면 그런 일이 없다. 닫아 두는 시간은 `LDR`/`STR` 대여섯 개, 80MHz에서 100ns 수준이라 어떤 인터럽트 지연 예산에도 들어간다. "인터럽트를 끄니까 나쁘다"가 아니라 "얼마나 오래 끄는가"가 기준이다.

### 6-2. lock-free로 하면 — ABA 문제

Cortex-M3 이상은 `LDREX`/`STREX`로 lock-free stack을 만들 수 있다. 여기에 고전적 함정이 있다.

```
  free_head ──> [A] ──> [B] ──> [C] ──> NULL
  task:  b = LDREX(free_head)   → A
         next = b->next          → B    ← 이 값을 읽어 뒀다
         │    <<< ISR >>>
         │    alloc: A를 가져감    free_head ──> [B] ──> [C]
         │    alloc: B를 가져감    free_head ──> [C]
         │    free(A):            free_head ──> [A] ──> [C]
         │    <<< 복귀 >>>        (B는 아직 ISR이 들고 있다)
         ▼
         STREX(next=B, &free_head)
              free_head가 여전히 A이므로 성공한다!
              free_head ──> [B] ──> ???

  결과: ISR이 사용 중인 B가 free list에 실리고, C는 리스트에서 사라졌다.
```

`STREX`는 "값이 안 바뀌었나"가 아니라 "이 코어의 exclusive monitor가 살아 있나"를 본다. Cortex-M의 monitor는 인터럽트나 컨텍스트 스위치 때 `CLREX`로 지워지는 것이 보통이라 실제로는 이 시나리오에서 STREX가 실패해 준다. 즉 M 시리즈 단일 코어에서는 대개 안전하다. 그런데 "대개"에 기대는 설계를 리뷰에서 방어하려면 아키텍처 매뉴얼의 monitor 동작을 인용해야 하고, 멀티코어로 가면 tag 카운터를 붙인 넓은 CAS가 필요해진다.

| 항목 | PRIMASK critical section | LDREX/STREX lock-free |
|---|---|---|
| 코드 길이 | 2줄 | 재시도 루프 + 배리어 |
| 최악 지연 | 결정적(수십 ns) | 재시도 횟수가 이론상 무한 |
| ABA | 없음 | 아키텍처 세부에 의존 |
| 멀티코어 | 부족(스핀락 추가 필요) | 가능하지만 tag 필요 |
| 검증 난이도 | 쉬움 | 어려움 |

### 6-3. `volatile`은 답이 아니다

`free_head`를 `volatile`로 선언해도 해결되지 않는다. `volatile`은 컴파일러의 캐싱·재배치만 막을 뿐 RMW를 원자적으로 만들지 않는다. 6-1의 `used++` 타임라인은 `volatile`을 붙여도 그대로 일어난다. 필요한 건 상호배제이지 가시성이 아니다. 반대로 이 pool에서 `volatile`이 필요한 자리는 없다. critical section 매크로가 컴파일러 배리어를 겸하기 때문이다(실제 구현에서는 인라인 어셈의 `memory` clobber가 그 역할을 한다).

## 7. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `align_dummy` 없이 union 선언 | host 통과, 타깃에서 UsageFault | union 정렬이 포인터 크기로 떨어짐 | `max_align_t` 멤버 + `_Static_assert` |
| 블록 크기를 60처럼 비정렬 값으로 | 홀수 번째 블록에서만 HardFault | 블록 간격이 정렬의 배수가 아님 | 정렬 단위로 올림하고 assert 추가 |
| `free_head = b->next` 누락 | 모든 alloc이 같은 주소 반환 | 머리를 안 옮김 | pop 두 줄을 한 묶음으로 본다 |
| double free 검사 없음 | 몇 시간 뒤 데이터가 섞이고 `used` 언더플로 | free list에 cycle 생성 | bitmap 1비트 검사 |
| 범위 검사 없이 free | 스택·전역 손상, 엉뚱한 곳에서 크래시 | 외부 주소에 링크를 써 넣음 | `pool_owns`로 범위와 오프셋 검사 |
| `pool_alloc` 반환값 NULL 미확인 | NULL 역참조로 HardFault | 고갈을 처리하지 않음 | 호출부마다 NULL 분기 + 실패 카운터 |
| critical section 없이 ISR과 공유 | 재현 안 되는 간헐 버그 | RMW 경합 | `CRIT_ENTER`/`CRIT_EXIT`로 감쌈 |
| 검사만 critical section 밖에 둠 | 부하가 높을 때만 double free 통과 | TOCTOU | 검사와 갱신을 한 구간에 |
| `n == 0` 검사 누락 / 반납 후 계속 사용 | init에서 즉시 크래시 / 남의 데이터가 덮어써짐 | `b[n-1]`이 `b[SIZE_MAX]`, use-after-free | 나누기 직후 `n == 0` 확인, 반납 직후 caller 변수에 `NULL` 대입 |

## 8. 직접 확인하기

`make sol N=04`는 모범답안을 빌드·실행해 `ALL TESTS PASSED`를 찍고, `make run N=04`는 내 구현을 빌드·실행한다(채우기 전에는 첫 assert에서 멈추는 것이 정상이다).

출력 첫 줄 `pool: block=64 bytes, align=8, blocks=8, pool_t=40 bytes`를 본다. `align=8`은 이 host의 `alignof(max_align_t)`다. `pool_t=40 bytes`는 블록 8개(512바이트)에 대한 관리 오버헤드가 40바이트라는 뜻이고, 블록 수를 늘려도 bitmap 워드만 늘어난다.

**실험 1 — 정렬을 일부러 깨뜨린다.** `POOL_BLOCK_SIZE`를 60으로 바꾸면 `static_assert failed "block size must be a multiple of max alignment"`로 빌드가 멈춘다. 런타임까지 가지 않는 것이 `_Static_assert`의 값어치다. 56으로 바꾸면? 8의 배수라 통과하고 테스트도 전부 통과한다. 블록 크기가 꼭 64일 필요는 없고 정렬의 배수이기만 하면 된다는 걸 확인할 수 있다.

**실험 2 — double free 검사를 꺼 본다.** `pool_free`의 `pool_bitmap_test` 분기를 주석 처리하면 테스트 6이 실패한다. 실패 지점이 `assert(... == POOL_ERR_DOUBLE)`가 아니라 그 다음 줄 `assert(freelist_len(&pool) == NBLOCKS)`인 것도 확인하라. `freelist_len`이 `UINT32_MAX`를 돌려주는 건 사슬에 cycle이 생겼다는 뜻이고, 3-3에서 그린 그 상황이다.

**실험 3 — pool 크기를 바꿔 본다.** `NBLOCKS`를 8에서 4로 줄이고 다시 돌리면 `live` 배열이 `NBLOCKS`를 따라가므로 여전히 통과하고 `high_water`가 4가 된다. 용량이 바뀌어도 불변식이 유지되는지 보는 실험이다.

## 9. 면접에서 말하기

### 한국어 설명 흐름

먼저 "왜 malloc이 아닌가"로 연다. heap은 실패 시점과 실행 시간을 증명할 수 없고 그게 배터리 기기에서 3시간 뒤 필드 크래시로 나타난다. 그다음 해법을 한 문장으로 던진다. 정적 배열을 같은 크기로 자르고 free 블록 안에 링크를 심는다. 화이트보드에 사슬을 그리고 alloc은 머리 pop, free는 머리 push라고 짚는다. 여기까지 30초다. 그 뒤 면접관이 묻기 전에 정렬, double free, ISR 안전성 세 가지를 먼저 꺼내면 "생각해 본 사람"으로 보인다.

### 그대로 쓸 영어 문장

- "I carve a static array into equal-sized blocks and thread the free list through the free blocks themselves, so there's zero per-block metadata overhead and zero external fragmentation."
- "Alloc pops the head, free pushes it back — two pointer assignments each, constant time regardless of pool state, which is what makes it safe to call from an ISR."
- "The block union carries a `max_align_t` member so every block starts on the strictest alignment boundary, and a static assert pins the block size to a multiple of that — otherwise block one onward is misaligned and you get a fault on the target that never reproduces on your host tests."
- "I validate freed pointers against the pool range and the block stride, and I keep one bit per block so a double free is rejected instead of splicing a cycle into the free list, which would silently hand the same buffer to two owners."
- "I track a high-water mark so we size the pool from measured worst-case usage instead of guessing, and I count allocation failures so exhaustion shows up in telemetry rather than as a NULL dereference at three in the morning."

### 화이트보드에 그릴 순서

1. 가로로 긴 사각형에 `static storage`라고 쓰고 세로선으로 4칸 나눈 뒤, 각 칸 왼쪽 끝에 작은 상자를 그려 화살표로 다음 칸을 가리키고 마지막은 `NULL`. 위에 `free_head`를 달아 첫 칸을 가리키게 한다.
2. alloc 한 번: 첫 칸에 빗금, `free_head`를 둘째 칸으로. 옆에 `used=1`.
3. free 한 번: 빗금을 지우고 그 칸의 화살표를 현재 head로, `free_head`를 그 칸으로. 사슬 순서가 주소 순이 아니게 된 걸 짚으며 "그래도 상관없다, 블록이 전부 같으니까"라고 말한다. 마지막으로 칸 아래에 `1 0 1 0`을 쓰고 "double free 검사는 이 1비트"라고 덧붙인다.

## 10. follow-up 답안

**"Now the buffers aren't all the same size."** size class별 pool을 여러 개 둔다. 64 / 256 / 1024 바이트 pool 셋을 만들고 요청 크기 이상인 가장 작은 class에서 꺼낸다. slab allocator와 같은 구조다. 100바이트를 요청하면 256 블록이 나가고 156바이트가 낭비된다(내부 단편화). 대신 외부 단편화는 여전히 0이고 alloc/free도 여전히 O(1)이다. 실무에서는 실제 요청 크기 히스토그램을 EVT 단계에서 로그로 뽑아 class 경계를 정한다. class가 고갈됐을 때 상위 class에서 빌릴지는 정책 문제인데, 빌리기 시작하면 최악 지연시간 분석이 복잡해지므로 기본은 빌리지 않고 실패시키는 쪽이다.

**"Can you do this without disabling interrupts?"** 6-2절 그대로다. `LDREX`/`STREX` 재시도 루프로 lock-free stack을 만들 수 있고, Cortex-M 단일 코어에서는 인터럽트 시 exclusive monitor가 클리어되므로 ABA가 실질적으로 막힌다. 하지만 재시도 횟수의 상한을 증명하기 어렵고 멀티코어로 가면 tagged pointer가 필요하다. PRIMASK 구간은 수십 나노초라 인터럽트 지연 예산에 들어가고 코드가 짧아 리뷰와 검증이 쉽다. 인터럽트를 100ns도 못 끄는 경로가 실제로 있다면 그때 lock-free를 꺼낸다.

**"How would you detect a caller writing past the end of its block?"** 디버그 빌드에서만 블록 끝에 canary 8바이트를 붙인다. 블록 크기를 72로 늘려 앞 64바이트만 내주고 뒤 8바이트에 `0xA5A5A5A5A5A5A5A5`를 심는다. `pool_free`에서 canary가 그대로인지 확인하고 깨졌으면 그 자리에서 로그를 남기고 fault를 일으킨다. 릴리스에서는 빼서 RAM과 사이클을 되찾는다. MPU 영역으로 블록 뒤에 접근 불가 구간을 두는 방법도 있지만 영역 수가 8개 남짓이라 pool마다 쓰기는 어렵다. 실무에서는 free 시점에 블록 전체를 `0xDD`로 칠해 use-after-free를 빨리 드러나게 하는 것도 같이 한다.

**"The pool runs out at 3am in the field."** 세 층으로 답한다. 즉시 동작은 "실패를 반환하고 계속 산다"이다. `pool_alloc`이 `NULL`을 주고 caller는 그 프레임을 버린다. 오디오면 한 프레임 드롭, 로그면 기록 하나 유실이고 어느 쪽도 리부트보다 낫다. 절대 `NULL`을 역참조하지 않는다. 다음은 기록이다. `alloc_fail_count`를 올리고 그 순간의 `high_water`, 어느 서브시스템이 몇 개를 들고 있었는지를 비휘발성 영역이나 다음 연결 시 보낼 링버퍼에 남긴다. 다음 날 아침에 할 일은 `high_water`를 보는 것이다. `high_water == capacity`면 pool이 작은 것이고, 여유가 있는데도 실패했다면 누수다. 즉 어딘가 free를 빼먹은 경로가 있다는 뜻이라 블록별 "마지막으로 alloc한 호출자" 태그를 디버그 빌드에 넣어 추적한다. 애초에 EVT 기간 `high_water` 로그로 pool 크기를 정하고 여유율을 얹어 두는 것이 본업이다.

**"Why is `malloc` actually a problem here?"** 3-1의 그림을 말로 옮기고 구체적인 실패 하나를 든다. 오디오 64바이트, BLE 100바이트, 로그 20바이트가 섞인 힙을 몇 시간 돌리면 free 총합이 300바이트여도 연속 64바이트가 없어 `malloc(64)`가 `NULL`을 돌려준다. 그 시점이 언제인지는 사용자의 행동 패턴에 달려 있어 재현되지 않는다. 시간도 같이 말한다. `malloc`의 실행 시간은 힙 상태에 비례해 늘어나 ISR 마감을 증명할 수 없고, newlib의 `malloc`은 내부 lock을 쓰므로 ISR에서 부르는 것 자체가 위험하다. 고정 블록 pool은 이 셋을 전부 컴파일 타임 상수로 바꾼다. 용량은 링커 맵에 찍히고, 실행 시간은 명령어 몇 개로 고정되고, 실패는 `NULL` 하나로 명시된다.

## 11. 요약 & 체크리스트

핵심은 네 문장이다. free 블록 안에 링크를 심으면 메타데이터가 공짜다. 모든 블록이 같으므로 어느 것을 줘도 되고 그래서 외부 단편화가 0이다. alloc/free가 포인터 대입 두 번이라 ISR 마감을 계산할 수 있다. 정렬은 `max_align_t`와 `_Static_assert`로 컴파일 타임에 못박는다.

- [ ] `union`에 `next` / `max_align_t` / `data[]` 세 멤버를 왜 넣는지, `_Static_assert` 세 개가 각각 어떤 실수를 잡는지 말할 수 있다.
- [ ] free list를 그리고 alloc 1회, free 1회를 화살표로 재배치할 수 있고, 사슬이 주소 순으로 정렬되지 않아도 되는 이유를 말할 수 있다.
- [ ] 블록 크기가 정렬의 배수가 아닐 때 몇 번째 블록부터 깨지는지 주소로 보일 수 있다.
- [ ] double free가 왜 즉시 크래시가 아니라 조용한 데이터 손상으로 나타나는지 그릴 수 있다.
- [ ] `pool_owns`의 두 검사(범위, 오프셋)가 각각 어떤 실수를 잡는지 구분한다.
- [ ] `used++`가 왜 원자적이지 않은지 명령 세 개로 설명하고, PRIMASK 저장/복원이 단순 `__enable_irq()`보다 나은 이유와 lock-free 버전의 ABA 시나리오를 말할 수 있다.
- [ ] `high_water`로 pool 크기를 정하는 절차와, 고갈 시 펌웨어의 동작을 말할 수 있다.
