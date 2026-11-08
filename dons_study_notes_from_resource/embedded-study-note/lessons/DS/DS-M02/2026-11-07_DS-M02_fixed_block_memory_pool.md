---
id: DS-M02
time: 75분
updated: 2026-11-07
---
# DS-M02 고정 크기 메모리 풀: O(1) 할당/해제

## 학습 목표

- 호출자가 준 바이트 버퍼를 같은 크기 블록으로 자르고, **빈 블록 자체 안에 "다음 빈 블록" 포인터를 넣는** free list(침입형 free list)로 할당/해제를 O(1)·결정적(deterministic)으로 만든다 — 블록당 메타데이터 0 바이트.
- 블록 간격(stride)을 `max(크기, sizeof(void*))` → `_Alignof(max_align_t)` 배수로 올리는 이유를 정렬(alignment)·Cortex-M 폴트·DMA 관점에서 설명한다.
- 디버그 모드에서 **범위 밖 포인터 / 블록 중간 포인터 / 이중 해제(double free)** 를 상태 변경 없이 거절하고, 사용량 **하이워터마크(high watermark)** 로 풀 크기를 정한다.
- 참조 모델 대조 랜덤 스트레스 테스트로 "살아 있는 블록이 두 번 나가지 않는다"를 검증하고, ASan 수동 poisoning(`ASAN_POISON_MEMORY_REGION`)으로 풀 안의 use-after-free를 잡는다.
- Zephyr `k_mem_slab`, FreeRTOS, ThreadX, Linux SLUB이 같은 문제를 어떻게 푸는지 비교해 말할 수 있다.

## 1. 문제

펌웨어는 `malloc` 을 못 쓰거나(MISRA C:2012 Rule 21.3, Dir 4.12) 쓰기 싫어한다 — 최악 시간이 정해져 있지 않고, 오래 돌면 단편화(fragmentation)로 "메모리는 남는데 할당이 실패"한다. 그래서 RTOS 커널, 네트워크 스택, SSD 컨트롤러의 명령 컨텍스트까지 **"같은 크기 객체 N개를 미리 잡아 두고 O(1)로 빌려주는" 고정 블록 풀**을 쓴다. 면접에서는 "malloc 없이 메시지 버퍼 할당기를 짜라 → 이중 해제는? ISR에서는? 정렬은?" 순서로 꼬리질문이 이어지는 단골 문제다.

DS-B07에서 노드 배열 + 인덱스 free list를 만들었다. 이번엔 한 단계 일반화한다: 자료형을 모르는 **바이트 버퍼**를 블록으로 나누고, 별도 `next[]` 배열 없이 **빈 블록의 첫 8바이트(이 맥) 자체를 링크로 쓴다**. 블록이 할당되면 그 8바이트는 사용자 데이터가 되고, 반납되면 다시 링크가 된다. 그래서 오버헤드는 0이지만, 반납 후에 사용자가 블록을 건드리면 **할당기 자체가 망가진다** — 이게 디버그 검사와 ASan poisoning이 필요한 이유다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m02a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">20 B 블록 6개 = stride 24 B × 6 (alloc 0..5 → free 4, 1, 3 후)</text>
  <g stroke="currentColor">
    <rect x="20" y="60" width="110" height="56" class="f-acc"/>
    <rect x="135" y="60" width="110" height="56" class="f-mut"/>
    <rect x="250" y="60" width="110" height="56" class="f-acc"/>
    <rect x="365" y="60" width="110" height="56" class="f-mut"/>
    <rect x="480" y="60" width="110" height="56" class="f-mut"/>
    <rect x="595" y="60" width="110" height="56" class="f-acc"/>
  </g>
  <g stroke="currentColor" fill="none" stroke-dasharray="3 3">
    <path d="M172 60 L172 116"/><path d="M402 60 L402 116"/><path d="M517 60 L517 116"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="75" y="52">[0] used</text><text x="190" y="52">[1] free</text><text x="305" y="52">[2] used</text>
    <text x="420" y="52">[3] free</text><text x="535" y="52">[4] free</text><text x="650" y="52">[5] used</text>
    <text x="75" y="92">user 20 B</text><text x="305" y="92">user 20 B</text><text x="650" y="92">user 20 B</text>
    <text x="153" y="84">next</text><text x="153" y="100">→[4]</text>
    <text x="383" y="84">next</text><text x="383" y="100">→[1]</text>
    <text x="498" y="84">next</text><text x="498" y="100">NULL</text>
    <text x="208" y="92">(쓰레기)</text><text x="438" y="92">(쓰레기)</text><text x="553" y="92">(쓰레기)</text>
  </g>
  <rect x="560" y="150" width="96" height="28" rx="5" stroke="currentColor" class="f-warn"/>
  <text x="608" y="169" fill="currentColor" text-anchor="middle" font-family="monospace">free_head</text>
  <g stroke="currentColor" fill="none" stroke-width="1.6" marker-end="url(#m02a)">
    <path d="M560 160 L452 120"/>
    <path d="M370 116 Q280 150 196 120"/>
    <path d="M160 116 Q340 200 512 120"/>
  </g>
  <g fill="currentColor" font-size="12"><text x="512" y="166">①</text><text x="270" y="142">②</text><text x="336" y="176">③</text></g>
  <g fill="currentColor">
    <text x="10" y="226">free list: free_head → [3] → [1] → [4] → NULL  (반납 역순 = LIFO). 링크는 빈 블록 첫 sizeof(void*) 바이트에.</text>
    <text x="10" y="248">할당된 블록 [0] [2] [5] 에는 메타데이터가 전혀 없다 — 사용자가 24 B 중 20 B 를 다 쓴다.</text>
    <text x="10" y="270">pool_t: base, stride=24, nblocks=6, free_head, used=3, hwm=6, 디버그 live 비트(블록 0→5) = 1,0,1,0,0,1</text>
    <text x="10" y="292">블록 i 의 주소 = base + i·stride → 포인터 하나로 "몇 번 블록인지" 역산 가능 (Part B 검사의 근거).</text>
  </g>
</svg>
```

시작 코드의 자료형:

```c
#define POOL_ALIGN      ((size_t)_Alignof(max_align_t))   /* 이 맥 arm64: 8 */
#define POOL_MAX_BLOCKS 1024u
typedef enum { POOL_OK = 0, POOL_E_RANGE, POOL_E_INTERIOR, POOL_E_DOUBLE } pool_err_t;
typedef struct {
    uint8_t *base; size_t stride; uint32_t nblocks;
    void *free_head;
    uint32_t used, hwm, fails;
#if POOL_DEBUG
    uint32_t live[POOL_MAX_BLOCKS / 32u];  /* 1비트/블록: 할당됨 */
    uint32_t bad_frees;
#endif
} pool_t;
```

### Part A — stride, init, alloc, push (핵심 메커니즘)

- `pool_stride(size)` — `max(size, sizeof(void*))` 를 `POOL_ALIGN` 의 배수로 올린다. `size == 0` 이나 올림이 넘치는 크기면 0.
- `pool_init(p, buf, bytes, size)` — `buf` 가 `POOL_ALIGN` 정렬이 아니거나, 블록이 0개거나 `POOL_MAX_BLOCKS` 를 넘으면 `false`. 블록 0이 free list 머리가 되게 0 → 1 → … → n−1 → NULL 로 엮는다.
- `pool_alloc(p)` — 머리를 떼어 준다. 비었으면 `fails++` 후 `NULL`. `used`, `hwm`, (디버그) live 비트 갱신.
- `pool_push(p, blk)` — 블록을 머리에 넣는다 (검사 없는 반납 경로).
- 테스트: stride를 1~200 바이트 전부 느린 참조 계산과 대조, init 거절 5종, 42개 전부 할당하며 주소 = `base + i·stride`·정렬·매번 불변식, 43번째 실패, 각 블록 20 B를 꽉 채워도 이웃이 안 깨지는지, LIFO 재사용.

### Part B — 디버그 검사와 통계 (까다로운 경계)

- `pool_validate(p, ptr, &idx)` — `ptr` 이 이 풀의 **할당된 블록 시작 주소**인지 판정: 범위 밖 → `POOL_E_RANGE`, 블록 중간 → `POOL_E_INTERIOR`, 이미 free → `POOL_E_DOUBLE`.
- `pool_free` (주어짐) 는 `NULL` 이면 무해(`free(NULL)` 처럼), 검사 실패면 **아무 상태도 바꾸지 않고** 오류 코드 + `bad_frees++`.
- `pool_reset_hwm(p)` — 측정 구간을 새로 시작: `hwm = used`.
- 테스트: 이중 해제, `b + 4`, 풀 바로 앞 1바이트, 마지막 블록 뒤 꼬리 16 B, 스택 변수, 한 번도 안 나간 블록. 그리고 **랜덤 200,000 연산**: 블록별 shadow 참조 모델과 매번 대조 + 블록 내용 무결성 + 잘못된 free 주입 + 매번 `pool_check()`.

### Part C — ISR → 링 → 태스크 프레임 파이프라인 (펌웨어 사용)

UART/CAN/이더넷 수신 ISR이 64 B 프레임 버퍼를 풀에서 받아 채우고, 포인터만 SPSC 링(DS-B04)에 넣는다. 처리 태스크가 꺼내 검증하고 풀에 반납한다.

- `rx_isr(p, seq, len)` — 할당 실패(풀 고갈)면 `g_drops++`. 데이터를 채워 링에 넣는다.
- `rx_task(p, budget)` — 최대 `budget` 개를 처리: 길이·순서(seq 증가)·내용 검증, **임계 구역 안에서** `pool_free`.
- 하네스가 20,000 tick 트래픽(평균 0.625 프레임/tick, 500 tick마다 18개 버스트, 1000 tick마다 태스크 12 tick 정지)을 풀 크기 8/16/24/32로 돌려 drop·hwm·누수를 표로 찍는다. **hwm으로 풀 크기를 정하는 법**이 목표다.

## 2. Problem (English)

> **Fixed-block memory pool with O(1) alloc/free (no malloc).**
>
> Given a caller-supplied buffer, carve it into equal blocks and thread a free list through the free blocks themselves (zero per-block metadata).
>
> 1. `size_t pool_stride(size_t block_size)` — round `max(block_size, sizeof(void*))` up to `_Alignof(max_align_t)`; return 0 on 0 or overflow.
> 2. `bool pool_init(pool_t*, void *buf, size_t bytes, size_t block_size)` — reject a misaligned buffer, zero blocks, or more than 1024 blocks. Block 0 must be handed out first.
> 3. `void *pool_alloc(pool_t*)` and `pool_err_t pool_free(pool_t*, void*)` in O(1). Track `used`, the high-water mark, and allocation failures.
> 4. In debug builds, `pool_free` must detect pointers outside the pool, pointers into the middle of a block, and double frees — and leave the pool unchanged when it rejects one.
> 5. Use the pool for an ISR→task frame pipeline and pick the pool size from the measured high-water mark.
>
> Example: 1024-byte buffer, 20-byte blocks → stride 24 (on a host with 8-byte `max_align_t`), 42 blocks. Allocate 5 and 9, free 5 then 9 → the next two allocations return 9 then 5. `pool_free(p, blk + 4)` returns `POOL_E_INTERIOR`; freeing the same block twice returns `POOL_E_DOUBLE`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄이 쭉 나오고 "6/30 passed", exit 1 — TODO 8곳을 채워 30/30 을 만든다
```

TODO: `pool_stride`, `pool_init`, `pool_alloc`, `pool_push` (Part A) / `pool_validate`, `pool_reset_hwm` (Part B) / `rx_isr`, `rx_task` (Part C). `blk_next/blk_set_next`, live 비트 함수, `pool_free`, `pool_check()`, 시뮬레이터, 하네스는 주어진다. 처음부터 통과하는 6개는 init 거절 5종(`false` 를 기대)과 "빈 풀의 불변식" 검사다.

## 4. 힌트

<details>
<summary>힌트 1 — stride 는 두 조건의 max 를 올림</summary>

블록이 빈 동안 포인터 하나를 담아야 하니 최소 `sizeof(void*)`. 모든 블록 주소가 정렬되려면 `base` 가 정렬돼 있고 stride가 `POOL_ALIGN` 의 배수면 된다 (`base + i·stride`). 2의 거듭제곱 a로 올림은 `(x + a - 1) & ~(a - 1)`. 넘침 검사는 더하기 **전에**: `x > SIZE_MAX - a` 면 거절.

</details>

<details>
<summary>힌트 2 — 블록 0 을 머리로 만들려면 뒤에서부터 push</summary>

머리에 넣기(push)는 넣은 순서의 역순으로 나온다. 그러니 `i = n-1` 부터 0까지 push 하면 머리가 0이다. `unsigned` 역방향 루프는 `for (size_t i = n; i-- > 0;)`. 링크 쓰기는 반드시 `blk_set_next()` (memcpy) 로.

</details>

<details>
<summary>힌트 3 — 포인터 검사는 정수로</summary>

서로 다른 객체를 가리키는 포인터끼리 `<` 비교는 C에서 UB(C11 6.5.8p5)라 "스택 변수 주소가 풀 안인가"를 포인터 비교로 물을 수 없다. `uintptr_t` 로 바꿔 `off = a - base` 를 구하고, `a < base || off >= nblocks*stride` 면 범위 밖, `off % stride != 0` 이면 블록 중간, 그다음에야 `idx = off / stride` 로 live 비트를 본다. 순서가 중요하다 — 범위를 먼저 봐야 live 배열 밖을 읽지 않는다.

</details>

<details>
<summary>힌트 4 — Part C: 누가 풀을 동시에 만지나</summary>

ISR은 alloc, 태스크는 free. 태스크의 `pool_free` 는 `free_head` 를 읽고-쓰는 두 단계라, 그 사이에 ISR이 alloc 하면 블록이 사라지거나 두 번 나간다. 태스크 쪽 free를 `crit_enter()/crit_exit()` 로 감싼다. ISR 쪽은 (같은 풀을 쓰는 더 높은 우선순위 ISR이 없다면) 태스크에게 선점되지 않으므로 그대로 둬도 된다.

</details>

## 5. 풀이

### Part A — 블록 안에 링크를 숨긴다

**불변식**(`pool_check()` 가 매 연산 후 검사):

1. free list의 모든 노드는 `[base, base + n·stride)` 안, 블록 시작 주소, 중복 없음(사이클 없음).
2. `(free list 길이) + used == nblocks` — 누수도 복제도 없다.
3. `used ≤ hwm ≤ nblocks`.
4. (디버그) free list 노드의 live 비트는 0, live 비트 총 개수 == `used`.

`pool_check()` 는 노드 주소가 범위 안인지 **먼저** 확인한 뒤에만 그 블록에서 next를 읽는다. 그래서 오염된 free list(0x4141… 같은 쓰레기)에서도 엉뚱한 주소를 역참조하지 않고 `false` 로 끝난다.

**stride 규칙** — 실제 출력:

```text
[A] POOL_ALIGN=8 sizeof(void*)=8 | size->stride: 1->8 7->8 8->8 9->16 20->24 24->24 33->40 60->64 64->64
```

| 요청 크기 | stride | 내부 낭비 | 이유 |
|---|---|---|---|
| 1 | 8 | 7 B (88%) | 빈 동안 포인터 8 B가 들어가야 함 |
| 9 | 16 | 7 B (44%) | 8의 배수로 올림 |
| 20 | 24 | 4 B (17%) | 이번 테스트 블록 |
| 33 | 40 | 7 B (18%) | |
| 64 | 64 | 0 | 정렬 배수면 낭비 없음 |

`_Alignof(max_align_t)` 는 "어떤 기본 타입(fundamental type)이든 놓을 수 있는 정렬"이다 (C11 7.19). 이 맥(arm64)은 8, x86-64 Linux는 16, arm-none-eabi(Cortex-M)는 8. `malloc` 도 이 정렬을 보장해야 하므로(C11 7.22.3), 풀이 `malloc` 대체품이 되려면 같은 약속을 해야 한다. 블록에 `double`, `uint64_t` 가 든 구조체를 넣는 순간 필요해진다:

- **Cortex-M0/M0+**: 비정렬 접근 자체가 HardFault.
- **Cortex-M3/M4/M7**: 단일 `LDR/STR` 은 비정렬도 허용(느림)하지만 `LDRD/STRD`, `LDM/STM` 은 워드 정렬이 아니면 UsageFault. 컴파일러는 `uint64_t` 가 정렬돼 있다고 믿고 `LDRD` 를 낸다.
- **DMA·캐시**: Cortex-M7 D-cache 라인은 32 B. DMA 버퍼용 풀이면 stride와 정렬을 32로 올려야 캐시 무효화(invalidate)가 이웃 블록을 지우지 않는다 → 이 경우 `POOL_ALIGN` 을 32로 정의.

**init·alloc·free 트레이스** (블록 4개, 블록 번호로 표시):

| 연산 | 반환 | free list | used | hwm |
|---|---|---|---|---|
| init | — | 0 → 1 → 2 → 3 | 0 | 0 |
| alloc | 0 | 1 → 2 → 3 | 1 | 1 |
| alloc | 1 | 2 → 3 | 2 | 2 |
| alloc | 2 | 3 | 3 | 3 |
| free(1) | OK | 1 → 3 | 2 | 3 |
| alloc | 1 | 3 | 3 | 3 |
| free(0), free(2) | OK | 2 → 0 → 3 | 1 | 3 |
| free(2) | **DOUBLE** | 2 → 0 → 3 (그대로) | 1 | 3 |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m02b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">alloc = 머리 떼기 (pop), free = 머리에 붙이기 (push). 둘 다 포인터 2번 이동</text>
  <g fill="currentColor"><text x="10" y="62">전</text><text x="10" y="132">alloc()</text><text x="10" y="202">free(B5)</text></g>
  <g stroke="currentColor">
    <rect x="90" y="44" width="80" height="28" class="f-warn"/><rect x="210" y="44" width="60" height="28" class="f-mut"/><rect x="310" y="44" width="60" height="28" class="f-mut"/><rect x="410" y="44" width="60" height="28" class="f-mut"/>
    <rect x="90" y="114" width="80" height="28" class="f-warn"/><rect x="210" y="114" width="60" height="28" class="f-mut"/><rect x="310" y="114" width="60" height="28" class="f-mut"/>
    <rect x="460" y="114" width="60" height="28" class="f-acc"/>
    <rect x="90" y="184" width="80" height="28" class="f-warn"/><rect x="210" y="184" width="60" height="28" class="f-ok"/><rect x="310" y="184" width="60" height="28" class="f-mut"/><rect x="410" y="184" width="60" height="28" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="130" y="63">head</text><text x="240" y="63">B3</text><text x="340" y="63">B1</text><text x="440" y="63">B4</text><text x="505" y="63">NULL</text>
    <text x="130" y="133">head</text><text x="240" y="133">B1</text><text x="340" y="133">B4</text><text x="410" y="133">NULL</text><text x="490" y="133">B3</text>
    <text x="130" y="203">head</text><text x="240" y="203">B5</text><text x="340" y="203">B1</text><text x="440" y="203">B4</text><text x="505" y="203">NULL</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m02b)">
    <path d="M170 58 L208 58"/><path d="M270 58 L308 58"/><path d="M370 58 L408 58"/><path d="M470 58 L480 58"/>
    <path d="M170 128 L208 128"/><path d="M270 128 L308 128"/><path d="M370 128 L384 128"/>
    <path d="M170 198 L208 198"/><path d="M270 198 L308 198"/><path d="M370 198 L408 198"/><path d="M470 198 L480 198"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="530" y="128">← 호출자에게 (next 칸은</text><text x="530" y="144">   이제 사용자 데이터)</text>
    <text x="530" y="198">B5.next = 옛 head(B1)</text><text x="530" y="214">head = B5</text>
    <text x="10" y="242">alloc: b = head; head = next(b);   free: next(b) = head; head = b;   — 루프 없음, 블록 수와 무관</text>
  </g>
</svg>
```

**왜 링크를 memcpy로 읽고 쓰나**: `uint8_t buf[]` 는 선언된 타입이 `uint8_t` 인 객체다. 그 안을 `*(void **)blk = head` 로 쓰면 `void *` 타입 lvalue로 `uint8_t` 객체에 접근하는 것이라 유효 타입 규칙(effective type, C11 6.5p7) 위반 — UB다. (malloc 메모리는 선언 타입이 없어 첫 저장이 타입을 정하지만, 정적 배열은 아니다.) `memcpy(&n, blk, sizeof n)` 는 바이트 복사라 합법이고 `-O2` 에서 load 한 개로 컴파일된다. Linux 커널은 `-fno-strict-aliasing` 으로 빌드해서 캐스트를 그냥 쓴다 — 우리 코드가 그 플래그에 기대지 않게 한 것.

**복잡도와 결정성**: alloc/free 모두 분기 1~2개 + load/store 몇 개, **입력·이력과 무관한 상수 시간**. 최악 = 평균. 단편화 없음(모든 블록이 같아 어떤 빈 블록이든 아무 요청이나 만족). 메모리: 블록당 오버헤드 0 + 정렬 낭비(위 표) + `pool_t` 하나. 디버그 비트맵은 1비트/블록. init만 O(n)(블록마다 링크 쓰기) — 연습 2에서 O(1)로 만든다.

### Part B — 잘못된 free를 "상태 변경 없이" 거절

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m02c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">pool_free(p, ptr) 의 디버그 경로 — 검사 순서가 곧 안전성</text>
  <g stroke="currentColor">
    <rect x="10" y="50" width="90" height="40" rx="6" class="f-mut"/>
    <rect x="130" y="50" width="120" height="40" rx="6" class="f-mut"/>
    <rect x="280" y="50" width="140" height="40" rx="6" class="f-mut"/>
    <rect x="450" y="50" width="130" height="40" rx="6" class="f-mut"/>
    <rect x="610" y="50" width="120" height="40" rx="6" class="f-ok"/>
    <rect x="10" y="130" width="90" height="34" rx="6" class="f-ok"/>
    <rect x="130" y="130" width="120" height="34" rx="6" class="f-warn"/>
    <rect x="280" y="130" width="140" height="34" rx="6" class="f-warn"/>
    <rect x="450" y="130" width="130" height="34" rx="6" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="55" y="68">ptr == NULL ?</text><text x="55" y="83" font-family="monospace">(free 규약)</text>
    <text x="190" y="68">a ∈ [base, base</text><text x="190" y="83">+ n·stride) ?</text>
    <text x="350" y="68">(a − base) % stride</text><text x="350" y="83">== 0 ?</text>
    <text x="515" y="68">live[idx] == 1 ?</text><text x="515" y="83">(지금 할당 중?)</text>
    <text x="670" y="68">live=0, push</text><text x="670" y="83">used−− → OK</text>
    <text x="55" y="152">OK (무시)</text>
    <text x="190" y="152" font-family="monospace">E_RANGE</text>
    <text x="350" y="152" font-family="monospace">E_INTERIOR</text>
    <text x="515" y="152" font-family="monospace">E_DOUBLE</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m02c)">
    <path d="M100 70 L128 70"/><path d="M250 70 L278 70"/><path d="M420 70 L448 70"/><path d="M580 70 L608 70"/>
    <path d="M55 90 L55 128"/><path d="M190 90 L190 128"/><path d="M350 90 L350 128"/><path d="M515 90 L515 128"/>
  </g>
  <g fill="currentColor" font-size="12"><text x="103" y="45">아니오</text><text x="254" y="64">예</text><text x="424" y="64">예</text><text x="584" y="64">예</text>
    <text x="60" y="112">예</text><text x="195" y="112">아니오</text><text x="355" y="112">아니오</text><text x="520" y="112">아니오</text></g>
  <text x="10" y="190" fill="currentColor">오류 경로는 bad_frees++ 만 하고 free list·used·live 를 건드리지 않는다 → 버그가 있어도 풀은 계속 일관됨.</text>
</svg>
```

각 검사가 막는 사고:

| 잘못된 free | 검사 없으면 무슨 일이 | 탐지 |
|---|---|---|
| 이중 해제 | 같은 블록이 free list에 두 번 → 다음 두 alloc이 **같은 블록을 두 주인에게** 준다 | live 비트가 이미 0 |
| 블록 중간 (`b+4`) | 어긋난 주소가 free list에 → 이후 블록이 이웃 블록과 겹친다 | `off % stride` |
| 다른 풀 / 스택 / 다른 객체 | 남의 메모리에 next를 써서 그 객체를 부순다 | 범위 |
| 한 번도 안 나간 블록 | 이중 해제와 같은 효과 | live 비트 0 (그래서 이름이 "이미 free 상태") |

실제 출력 (랜덤 스트레스):

```text
[B] stress 200000 ops: match | blocks=42 hwm=42 fails=7601 bad_frees caught=4557
```

- 할당 확률 52%라 풀이 자주 가득 차서 **고갈 경계**가 7,601번 시험됐다 (`fails` 가 참조 모델의 실패 횟수와 정확히 일치).
- 1/64 확률로 직전에 반납한 블록 이중 해제, 1/64 확률로 살아 있는 블록의 +1~+23 바이트 free를 주입 → 4,557번 모두 올바른 오류 코드로 거절, 그 뒤에도 불변식 유지.
- 할당마다 "받은 블록의 shadow가 이미 살아 있지 않은가"(같은 블록 두 번 배포 탐지), 반납마다 "내가 쓴 20 B 태그가 그대로인가"(겹침 탐지), 매 연산 후 `used`, `hwm` 을 참조 모델과 비교. 두 종류 검사는 서로 다른 버그를 잡는다 — 불변식은 "할당기 내부 구조", 내용 태그는 "블록끼리 겹치지 않음".

**하이워터마크**: `hwm` 은 "부팅 후(또는 마지막 reset 후) 동시에 가장 많이 빌려 간 개수"다. 평균 사용량이 아니라 **최대치**가 풀 크기를 정하기 때문에 이것을 잰다. `pool_reset_hwm()` 이 `hwm = 0` 이 아니라 `hwm = used` 인 이유: 지금 빌려 간 블록도 새 구간의 사용량이므로 0으로 두면 불변식 3(`used ≤ hwm`)이 깨진다. FreeRTOS의 `uxTaskGetStackHighWaterMark()`, `xPortGetMinimumEverFreeHeapSize()` 와 같은 발상이다 (거기선 "남은 최소" 로 표현).

**디버그 모드 비용**: alloc에 비트 set 1번, free에 나눗셈·나머지 각 1번 + 비트 검사. `-DPOOL_DEBUG=0` 이면 `#if` 로 사라진다. 현업에선 범위 검사 정도는 릴리스에도 남겨 두고 실패 시 `assert` → 폴트 로그(PC, 주소)를 남기는 경우가 많다 — 현장에서 이중 해제는 재현이 거의 안 되기 때문.

### Part B — ASan 연동: 풀 안의 use-after-free 잡기 (`demo_asan_poison.c`)

ASan은 `malloc/free` 를 가로채서 해제된 힙을 표시(poison)한다. 그런데 풀 메모리는 **정적 배열 하나**라 ASan 눈에는 처음부터 끝까지 유효한 전역 변수다 — 풀에서 반납한 블록을 건드려도 아무 말이 없다. 그래서 할당기가 직접 알려 준다:

```c
#include <sanitizer/asan_interface.h>
pool_free:  blk_set_next(b, head); head = b;  ASAN_POISON_MEMORY_REGION(b, STRIDE);
pool_alloc: ASAN_UNPOISON_MEMORY_REGION(b, STRIDE);  head = blk_next(b);   /* 풀고 나서 읽기 */
```

순서가 핵심이다: free는 **링크를 쓴 다음** poison, alloc은 **unpoison 한 다음** 링크를 읽는다. 반대로 하면 할당기 자신이 ASan에 걸린다. ASan 없이 빌드하면 헤더가 두 매크로를 `((void)(addr), (void)(size))` 로 정의해서 비용이 0이다. 섀도 메모리 1바이트가 앱 8바이트를 나타내므로 블록 주소와 크기를 8의 배수로 맞춰야 경계가 정확하다 (stride 32).

```bash
cc -std=c11 -g -O1 -fsanitize=address demo_asan_poison.c -o demo_asan
cc -std=c11 -g -O1 demo_asan_poison.c -o demo_plain
```

ASan **없이** (같은 버그):

```text
$ ./demo_plain write
alloc m=0x104d54000 id=7 value=1234
  pool_alloc: free list corrupted, next=0x4141414141414141 (not a pool block)
after UAF write: x=0x0 y=0x0
$ ./demo_plain read
alloc m=0x1045e8000 id=7 value=1234
after free: id=73302048 value=1
```

- `write`: 반납한 블록에 `memset(m, 0x41, 16)` → 그 블록의 next가 `0x4141414141414141` 로 바뀐다. 디버그 범위 검사가 **다음 alloc 때** 오염을 발견하고 풀을 멈춘다(NULL). 검사가 없으면 두 번째 alloc이 `0x4141…` 을 그대로 돌려주고, 거기 쓰는 순간 임의 주소 쓰기 — 힙 익스플로잇의 고전적 원리다.
- `read`: free 후 읽은 `id=73302048, value=1` 은 쓰레기가 아니라 **next 포인터**다: 다음 빈 블록 주소 `0x1045e8020` 의 하위 32비트 `0x045e8020` = 73302048, 상위 32비트 = 1. 조용히 틀린 값이 나간다.

ASan **켜고** (`./demo_asan write`, exit 134 — macOS ASan은 보고 후 abort):

```text
==61288==ERROR: AddressSanitizer: use-after-poison on address 0x000100c983c0 at pc 0x00010131aeac bp 0x00016f16ebe0 sp 0x00016f16ebb8
WRITE of size 16 at 0x000100c983c0 thread T0
    #0 0x00010131aea8 in __asan_memset+0x1e0 (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x3eea8)
    #1 0x000100c90a88 in main demo_asan_poison.c:64
    #2 0x00018753e050 in start+0x19f8 (dyld:arm64+0x30050)

0x000100c983c0 is located 0 bytes inside of global variable 'g_mem' defined in 'demo_asan_poison.c' (0x000100c983c0) of size 256
SUMMARY: AddressSanitizer: use-after-poison demo_asan_poison.c:64 in main
Shadow bytes around the buggy address:
  ...
  0x000100c98300: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
=>0x000100c98380: 00 00 00 00 00 00 00 00[f7]f7 f7 f7 f7 f7 f7 f7
  0x000100c98400: f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7
  0x000100c98480: f7 f7 f7 f7 f7 f7 f7 f7 f9 f9 f9 f9 f9 f9 f9 f9
  ...
  Poisoned by user:        f7
  ...
To identify the code that *poisoned* the memory, try the experimental setting ASAN_OPTIONS=poison_history_size=<size>.
==61288==ABORTING
```

(`...` 은 생략한 줄.) `read` 모드도 `READ of size 4 ... in main demo_asan_poison.c:68` 로 같은 `use-after-poison`. 읽는 법: 한 줄 = 섀도 16바이트 = 앱 128바이트이고 줄 머리는 앱 주소다. `[f7]` 은 `0x…380 + 8×8 = 0x…3c0` = 버그 주소. `g_mem` 256 B = 섀도 32바이트(8 + 16 + 8)가 전부 `f7`(Poisoned by user) — 8블록이 모두 free 상태다. 앞의 `00` 은 다른 전역, 뒤의 `f9` 는 전역 redzone. 차이: ASan 없이는 **다음 alloc 때**(늦게, 원인 위치 모름) 알지만, ASan은 **버그가 난 줄**(64행 memset)을 스택과 함께 보여 준다. 호스트 단위 테스트에서 ASan을 돌리고, 보드에서는 소프트웨어 포이즌(연습 1)을 쓰는 게 보통의 분업이다.

### Part C — 풀 크기를 hwm으로 정한다

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m02d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">수신 파이프라인: 데이터는 풀 블록에 그대로, 링에는 포인터만 오간다</text>
  <g stroke="currentColor">
    <rect x="20" y="60" width="120" height="50" rx="6" class="f-warn"/>
    <rect x="320" y="40" width="150" height="40" rx="6" class="f-mut"/>
    <rect x="320" y="110" width="150" height="40" rx="6" class="f-acc"/>
    <rect x="600" y="60" width="120" height="50" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="80" y="82">rx_isr</text><text x="80" y="100" font-size="12">(UART/CAN ISR)</text>
    <text x="395" y="58" font-family="monospace" font-size="12">pool: frame_t × N</text><text x="395" y="74" font-size="12">64 B 블록</text>
    <text x="395" y="128" font-family="monospace" font-size="12">ring: frame_t *[64]</text><text x="395" y="144" font-size="12">SPSC (DS-B04)</text>
    <text x="660" y="82">rx_task</text><text x="660" y="100" font-size="12">(검증·처리)</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m02d)">
    <path d="M140 72 L318 58"/><path d="M140 98 L318 128"/><path d="M472 128 L598 98"/><path d="M598 72 L472 58"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="160" y="52">① pool_alloc (실패 → drop)</text>
    <text x="170" y="134">② 채우고 포인터 push</text>
    <text x="482" y="140">③ 포인터 pop</text>
    <text x="478" y="50">④ pool_free (임계 구역)</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="180">복사 0번: ISR 이 쓴 블록을 태스크가 그대로 읽는다. 링에 64 B 대신 8 B 포인터만 흐른다.</text>
    <text x="10" y="202">풀 = "프레임이 동시에 몇 개까지 떠 있을 수 있나" — 링 크기 64 ≥ 풀 크기라 링은 먼저 차지 않는다.</text>
    <text x="10" y="224">태스크가 멈춘(플래시 erase) 동안 + 버스트가 겹칠 때가 최대 → 그 순간이 hwm.</text>
  </g>
</svg>
```

실제 출력:

```text
[C] pool blocks | generated | dropped | processed | hwm | corrupt | leak
[C]           8 |     13226 |     491 |     12735 |   8 |       0 |    0
[C]          16 |     13226 |     125 |     13101 |  16 |       0 |    0
[C]          24 |     13226 |       0 |     13226 |  22 |       0 |    0
[C]          32 |     13226 |       0 |     13226 |  22 |       0 |    0
```

| 풀 블록 | drop | hwm | 해석 |
|---|---|---|---|
| 8 | 491 (3.7%) | 8 = 가득 | 포화. hwm == N 이면 "실제 수요는 N 이상"이라는 뜻일 뿐 수요를 모른다 |
| 16 | 125 (0.9%) | 16 = 가득 | 여전히 포화 — 버스트(18개)만으로도 넘친다 |
| 24 | 0 | 22 | 수요가 처음 드러난다: 최대 동시 22개. 여유 2개(8%)뿐 |
| 32 | 0 | 22 | 같은 트래픽이면 hwm은 풀 크기와 무관하게 22 → 여유 10개(31%) |

규칙: **hwm < N 인 실행에서만 hwm이 진짜 수요다.** 풀을 넉넉히 잡고 최악 시나리오(버스트 + 태스크 정지 + 최대 길이)로 돌려 hwm을 잰 뒤, 여유(보통 25~50%)를 더해 줄인다. 여기서는 22 → 28~32. 64 B × 32 = 2 KB, 16 KB SRAM MCU에서도 감당할 크기다. 출하 후에는 `hwm`, `fails` 를 텔레메트리/진단 명령으로 노출해 현장 최대치를 모은다.

모든 행에서 `dropped + processed == generated`, `corrupt == 0`, `leak == 0` — 프레임은 정확히 한 번 처리되거나 한 번 버려지고, 처리 후 모든 블록이 돌아왔다(`used == 0`). 풀이 고갈됐을 때의 정책은 설계 결정이다: 여기처럼 **ISR에서 drop + 카운터**(ISR은 기다릴 수 없음), 태스크라면 **타임아웃 대기**(Zephyr `k_mem_slab_alloc(..., K_MSEC(5))`), 또는 상류에 **흐름 제어**(UART RTS, CAN 오류 프레임 대신 버퍼 축소 통지).

**임계 구역**: 단일 코어 Cortex-M에서 ISR alloc과 태스크 free가 경쟁한다. 태스크의 free는 `next(b) = head; head = b;` 두 줄 사이에 ISR이 alloc 하면 ISR이 가져간 블록이 다시 머리가 되어 **두 주인**이 생긴다. 그래서 태스크 쪽을 PRIMASK로 감싼다 (`pm = __get_PRIMASK(); __disable_irq(); … __set_PRIMASK(pm);` — 중첩에 안전하도록 이전 상태 복원). 풀 연산은 루프가 없어 임계 구역 길이가 **수 명령으로 고정**된다 — 인터럽트 지연(latency) 상한이 보장된다는 게 malloc 대비 진짜 장점이다.

**malloc과의 평균 속도** (`demo_bench.c`, 48 B 블록, 4,000,000 연산, 이 맥 Apple Silicon `-O2`, 절대값보다 비율을 볼 것):

```text
block 48 B, 4000000 ops (alloc or free), Apple Silicon -O2
pattern           | harness | pool  | malloc | pool-h | malloc-h | (ns/op)
random 50/50      |    4.38 |  4.37 |   8.95 |  -0.01 |     4.57 |
random 60/40 full |    4.10 |  3.98 |  10.44 |  -0.12 |     6.34 |
```

`harness` 는 할당기 없이 루프·PRNG·보관 배열만 돈 시간이다. 풀은 그 위에 **측정 잡음 이하**(음수는 잡음)만 더하고, macOS malloc은 연산당 4.5~6 ns를 더한다. 맥의 malloc은 작은 크기에 매우 최적화돼 있어서 평균 차이는 이 정도지만, 임베디드에서 중요한 것은 평균이 아니라 **최악 시간이 상수이고, 단편화로 실패하는 일이 없다**는 점이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 30/30 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan   # 30/30, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `stride = block_size` (올림 없음) | 20 B면 블록 1의 주소가 `base+20` — `uint64_t` 필드가 비정렬 → M0 HardFault, M3+ `LDRD` UsageFault | `max(size, sizeof(void*))` 를 `POOL_ALIGN` 배수로 |
| 블록 크기 < `sizeof(void*)` 허용 | 빈 블록의 링크 8 B가 이웃 블록을 덮는다 | stride 최소값 = 포인터 크기 (더 작게 하려면 인덱스 링크 — DS-B07) |
| `*(void **)blk = head` | `uint8_t` 배열을 `void*` 로 접근 — 유효 타입 위반 UB. `-O2` 에서 재배치될 수 있다 | `memcpy(blk, &head, sizeof head)` |
| `if (ptr < p->base \|\| ptr >= end)` (포인터 비교) | 다른 객체를 가리키는 포인터끼리 관계 비교는 UB | `uintptr_t` 로 변환해 비교 |
| 범위 확인 전에 `live[(ptr-base)/stride]` | 범위 밖 포인터면 live 배열 밖을 읽는다 | 범위 → 블록 시작 → live 순서 |
| 이중 해제 미검출 | 같은 블록이 free list에 두 번 → 다음 두 alloc이 같은 주소 | live 비트 (또는 연습 1의 포이즌) |
| free 실패 시 일부 상태를 이미 바꿈 (`used--` 먼저) | 검사 실패 후에도 카운터가 틀어져 불변식 2 붕괴 | 검사 전부 통과한 뒤에만 수정 |
| `pool_reset_hwm` 에서 `hwm = 0` | 지금 빌려 간 블록을 잊어 `used > hwm` | `hwm = used` |
| `for (uint32_t i = n - 1; i >= 0; i--)` | unsigned는 항상 ≥ 0 → 무한 루프 | `for (size_t i = n; i-- > 0;)` |
| ASan poison을 링크 쓰기 **전에** | 할당기 자신의 next 쓰기가 use-after-poison으로 보고됨 | free: 링크 쓰고 poison / alloc: unpoison 하고 읽기 |
| ISR과 태스크가 같은 풀, 임계 구역 없음 | push/pop의 두 줄 사이 선점 → 블록 유실 또는 두 주인 | 짧은 PRIMASK 구간 (또는 ISR→링→태스크 단독 소유) |

## 8. 면접 Q&A

<details>
<summary>Q1. 고정 블록 풀이 malloc보다 펌웨어에 맞는 이유는? 단점은?</summary>

장점: ① alloc/free가 루프 없는 상수 시간 — 최악 실행 시간(WCET) 분석과 인터럽트 지연 상한에 들어간다. ② 모든 블록이 같아 **외부 단편화가 없다** — 몇 년 돌아도 "총 여유는 충분한데 실패"가 없다. ③ 메모리가 링커 맵에 `.bss` 로 확정 — RAM 예산을 빌드 때 안다. ④ 블록당 헤더 0 B. 단점: 크기가 하나라 작은 요청은 내부 단편화(stride 낭비), 큰 요청은 불가 → 크기별 풀 여러 개(연습 4, DS-M03 슬랩)로 해결. 피크 기준으로 잡아야 해서 평균 사용량 대비 RAM이 남는다. MISRA C:2012 Rule 21.3은 `<stdlib.h>` 의 malloc/free 사용 금지라 이런 풀이 사실상 표준 대체품이다.

</details>

<details>
<summary>Q2. 블록 정렬은 몇으로 해야 하나요? max_align_t는 언제 부족한가요?</summary>

범용 풀이면 `_Alignof(max_align_t)` — malloc과 같은 약속(어떤 기본 타입도 놓을 수 있음). 이 맥 8, x86-64 Linux 16, Cortex-M 8. 부족한 경우: ① **DMA + 캐시**: Cortex-M7의 32 B 캐시 라인 단위로 clean/invalidate 하므로 버퍼 시작과 크기를 32 B 배수로 — 아니면 invalidate가 같은 라인의 이웃 데이터를 날린다. ② SIMD/NEON 버퍼(16 B), ③ MPU 영역(Cortex-M의 MPU는 영역 크기·정렬 제약이 있다). 이런 풀은 `POOL_ALIGN` 을 키우고 버퍼를 `_Alignas(32)` 로 선언한다. 반대로 정렬을 지키지 않으면 M0는 모든 비정렬 접근이 HardFault, M3/M4/M7은 `LDRD/STRD/LDM/STM` 이 UsageFault.

</details>

<details>
<summary>Q3. 이중 해제를 어떻게 탐지하나요? 비용은?</summary>

① **할당 비트맵**(이번 구현): 1비트/블록, free 때 비트 확인 — 결정적, 오탐 없음. ② **free 블록에 매직 값/포이즌 패턴**: 메모리 추가 0이지만 사용자 데이터가 우연히 같으면 오탐, 그리고 포이즌은 "free 후 쓰기"도 잡는다(연습 1). ③ **블록 헤더에 상태 바이트**: ThreadX처럼 블록마다 헤더가 있다면 거기에. ④ 호스트 테스트에선 **ASan poisoning**. 범위·블록 시작 검사는 나눗셈/나머지 1번씩인데, Cortex-M0에는 하드웨어 나눗셈이 없어 라이브러리 호출이 된다 → stride를 2의 거듭제곱으로 맞추면 shift/mask로 끝난다(연습 3). 디버그 빌드에만 넣거나, 범위 검사만 릴리스에 남겨 `assert` + 폴트 로그를 남기는 타협이 흔하다.

</details>

<details>
<summary>Q4. ISR과 태스크(또는 두 코어)가 같은 풀을 쓰면? 락-프리로 만들 수 있나요?</summary>

단일 코어 MCU: alloc/free 전체를 PRIMASK(또는 BASEPRI) 임계 구역으로. 수 명령이라 지연이 고정된다. 더 깔끔한 건 소유권 분리 — ISR은 alloc만, 반납은 태스크가, 블록은 SPSC 링으로 넘기기(Part C). 락-프리 free list(Treiber stack)는 CAS로 `head` 를 바꾸는데 **ABA 문제**가 있다: A가 `head=X, next=Y` 를 읽고 멈춘 사이 B가 X, Y를 꺼내고 X만 돌려놓으면, A의 CAS(X→Y)가 성공해 사용 중인 Y가 머리가 된다. 해법은 머리 포인터 + 세대 카운터를 함께 CAS(이중 폭 CAS)하거나, LL/SC(ARM `LDREX/STREX`)처럼 "값이 같은가"가 아니라 "그 사이 독점이 깨졌는가"로 판정하는 원자 연산을 쓰는 것. 단일 코어 MCU에서는 대개 이 복잡도가 필요 없다.

</details>

<details>
<summary>Q5. Zephyr / FreeRTOS / ThreadX / CMSIS-RTOS2 에서는 이걸 뭐라고 부르나요?</summary>

- **Zephyr `k_mem_slab`**: 정확히 이 구조. `K_MEM_SLAB_DEFINE(name, block_size, num_blocks, align)` 로 정적 정의, `k_mem_slab_alloc(slab, &mem, timeout)` / `k_mem_slab_free(slab, mem)`. 커널 소스 `kernel/mem_slab.c` 가 빈 블록 안에 다음 빈 블록 포인터를 넣어 free list를 만든다. 우리와 다른 점: 블록이 없으면 timeout 동안 **스레드가 대기**할 수 있고(ISR에선 `K_NO_WAIT`), 스핀락으로 보호된다. 통계는 `k_mem_slab_num_used_get()`, `k_mem_slab_num_free_get()`, 그리고 `CONFIG_MEM_SLAB_TRACE_MAX_UTILIZATION` 을 켜면 하이워터마크 `k_mem_slab_max_used_get()`.
- **FreeRTOS**: 커널에 고정 블록 풀 API가 없다. `heap_1`~`heap_5` 는 가변 크기 할당기다(`heap_4` = first-fit + 인접 병합). 흔한 패턴은 정적 배열 + 빈 블록 포인터를 담은 큐(`xQueueCreateStatic`) — `xQueueReceive(…, timeout)` 가 블로킹 할당, `…FromISR` 가 ISR 버전이 된다. FreeRTOS+TCP의 네트워크 버퍼(`BufferAllocation_1.c`)가 고정 디스크립터 풀이고 `uxGetMinimumFreeNetworkBuffers()` 로 최저 여유(=하이워터마크의 반대)를 준다.
- **ThreadX**: `tx_block_pool_create`, `tx_block_allocate(pool, &ptr, wait)`, `tx_block_release(ptr)`. release가 풀 인자 없이 포인터만 받는 대신 **블록마다 포인터 하나의 숨은 오버헤드**가 있다 — 우리 풀은 `pool_free(p, ptr)` 로 풀을 받아 오버헤드 0. 이 트레이드오프를 말할 수 있으면 좋다.
- **CMSIS-RTOS2**: `osMemoryPoolNew(count, size, attr)`, `osMemoryPoolAlloc(id, timeout)`, `osMemoryPoolFree(id, block)`.

</details>

<details>
<summary>Q6. Linux 커널에서는? (see:OS)</summary>

같은 크기 객체 할당은 슬랩 할당기(`kmem_cache_create` / `kmem_cache_alloc` / `kmem_cache_free`, 기본 구현 SLUB)가 맡는다 — 페이지를 객체 크기로 자르고 **free 객체 안에 다음 free 포인터**를 둔다는 점에서 이 풀의 확장판이다(크기 클래스·CPU별 캐시·partial 슬랩은 DS-M03). 그래서 커널은 우리와 같은 위험을 겪는다: free 후 쓰기가 free 포인터를 덮으면 임의 주소 할당으로 이어진다. 대책으로 `CONFIG_SLAB_FREELIST_HARDENED` 는 free 포인터를 비밀값과 주소로 섞어(XOR) 저장하고, `slub_debug=P` 는 free 객체를 `0x6b`(`POISON_FREE`, `include/linux/poison.h`)로 채워 다음 할당 때 검사한다(연습 1과 같은 원리), KASAN은 우리가 ASan으로 한 일을 커널에서 한다. 이름이 비슷한 `mempool_t`(`mm/mempool.c`)는 다른 것 — 슬랩 위에 "최소 N개 예약"을 얹어 메모리 압박에서도 진행을 보장하는 장치다.

</details>

<details>
<summary>Q7. 풀 크기는 어떻게 정하나요? 고갈되면 어떻게 하나요?</summary>

최악 시나리오(최대 버스트 + 소비자 최장 정지 + 최대 지연)에서 hwm을 재고 여유 25~50%를 더한다. 해석상 주의: **hwm == N 이면 포화라 진짜 수요를 모른다** — 측정은 넉넉한 풀로 한다(Part C에서 8, 16은 포화, 24·32에서야 22가 보였다). 해석적으로는 리틀의 법칙(평균 동시 개수 = 도착률 × 평균 체류 시간)이 평균을 주지만 풀은 최대치로 잡아야 하므로 측정이 필요하다. 고갈 정책: ISR은 기다릴 수 없으니 drop + 카운터, 태스크는 timeout 대기, 시스템 수준은 흐름 제어. SSD 펌웨어라면 NVMe 명령 컨텍스트 풀이 고갈될 때 새 명령 fetch를 멈추는 것이 흐름 제어에 해당한다 — "드롭하지 않고 상류를 늦춘다".

</details>

<details>
<summary>Q8. 꼬리질문: init이 O(n)인데 O(1)로 만들 수 있나요? 전체 반납(reset)은?</summary>

"아직 한 번도 안 나간 블록" 커서 `fresh` 를 두고, free list가 비었을 때만 `base + fresh++·stride` 를 준다(lazy carving). init과 reset이 둘 다 변수 3개 대입으로 끝나고, 부팅 시 큰 풀(외부 PSRAM 등)을 훑지 않는다. 놀랍게도 내주는 블록 순서는 eager 방식과 완전히 같다 — eager의 free list는 "반납된 블록들(LIFO) 뒤에 아직 안 쓴 블록이 순서대로" 붙어 있는 모양이기 때문. 연습 2가 50,000 연산으로 이를 확인한다. 프레임 단위로 모두 버리는 용도면 아예 bump 할당기(arena)가 더 단순하다.

</details>

## 9. 추가 연습

### 연습 1 — 소프트웨어 포이즌 (ASan 없는 보드용)

free 때 블록의 링크 뒤 바이트를 `0x6B` 로 채우고, alloc 때 그 패턴이 그대로인지 검사하라. 새로 준 블록은 `0x5A` 로 채워 "초기화 안 하고 읽기"를 눈에 띄게 하라. free 후 데이터 영역 쓰기와 free 후 링크 덮어쓰기를 각각 탐지하라.

<details>
<summary>답 보기</summary>

```text
[1] UAF data write -> poison_hits=1 (block 0) | UAF next-ptr write -> corrupt=1 (block 1), alloc returns NULL
```

데이터 영역(`a[20] = 0`)을 건드린 경우: 블록이 다시 나갈 때 `0x6B` 가 깨진 걸 보고 `poison_hits++`, 블록 번호를 기록한다 — 범인은 "이 블록을 마지막으로 가졌던 코드"로 좁혀진다. 링크(`memset(c, 0x41, 8)`)를 덮은 경우는 포이즌이 아니라 **next 값 검증**으로 잡는다: 꺼낼 블록의 next가 풀 블록 시작이 아니면 따라가지 않고 풀을 멈춘다. 정상 사용 20,000 연산에서는 히트 0. 비용은 alloc/free마다 stride 바이트 쓰기·읽기라 O(stride) — 디버그 빌드 전용. 값은 Linux `POISON_FREE`/`POISON_INUSE` 와 같게 골랐다.

</details>

### 연습 2 — O(1) init과 reset (lazy carving)

`pool_init` 이 블록을 하나도 건드리지 않게 바꿔라. free list + `fresh` 커서로 alloc하고, 전체 반납 `reset` 도 O(1)로. 50,000번 랜덤 연산에서 eager 풀(본문 방식)과 **같은 블록 번호**를 내주는지 대조하라.

<details>
<summary>답 보기</summary>

alloc: free list에 있으면 pop, 없으면 `fresh < n` 일 때 `base + fresh++·stride`, 아니면 NULL. reset: `head = NULL; fresh = 0; used = 0`. eager 풀은 init에서 64번 링크를 썼지만(`init_writes == 64`) lazy 풀은 0번이다. 두 풀의 반환 블록 번호가 50,000 연산 내내 일치했다 — eager의 free list는 언제나 `[반납된 블록들 LIFO] → [아직 안 나간 블록 오름차순]` 이고, lazy는 그 뒤쪽 절반을 커서로 표현한 것뿐이기 때문이다. reset 후 다시 블록 0, 1 순서로 나오는 것도 확인한다.

</details>

### 연습 3 — 나눗셈 없는 포인터 검사 (Cortex-M0)

Cortex-M0/M0+(ARMv6-M)에는 `UDIV` 가 없어 `off % stride`, `off / stride` 가 라이브러리 호출(수십 사이클)이 된다. stride가 2의 거듭제곱일 때 shift/mask로 같은 판정을 하라. stride 8~128에 대해 풀 앞뒤 2블록까지 모든 주소에서 나눗셈 버전과 결과가 같은지 검사하라.

<details>
<summary>답 보기</summary>

`off = a - base` 일 때 몫 `off >> k`, 나머지 `off & (2^k − 1)`. 범위 검사도 `(off >> k) >= n` 으로 곱셈 없이 된다(단, `a < base` 는 먼저 따로 거절 — 뺄셈이 감겨 큰 값이 되지만 명시하는 편이 읽기 쉽다).

```text
[3] shift/mask vs divide: 4960 addresses, identical
```

대가: 24 B 블록을 32 B로 올리면 25% 낭비. 그래서 M0에서는 "블록 크기를 2의 거듭제곱으로 설계"하거나, stride가 컴파일 타임 상수면 컴파일러가 상수 나눗셈을 곱셈+시프트로 바꾸게 둔다(단 ARMv6-M에는 32×32→64 곱셈 `UMULL` 도 없어서 이 변환도 M0에서는 싸지 않다). Cortex-M3 이상은 `UDIV` 가 있다.

</details>

### 연습 4 — 크기별 풀 세트와 fallback

32 B × 16, 64 B × 8, 128 B × 4 풀을 하나의 정적 배열에 배치하라. `set_alloc(size)` 는 맞는 가장 작은 클래스부터, 비었으면 다음 클래스로 넘어간다. `set_free(ptr)` 는 **주소 범위로 주인 풀을 찾아** 크기 없이 반납한다. 클래스별 사용량을 참조 모델과 30,000 연산 대조하라.

<details>
<summary>답 보기</summary>

32 B 풀 16개를 다 쓴 뒤 8 B 요청은 64 B 풀에서 나온다(`c == 1`). 주인 찾기는 각 풀의 `[base, base + n·stride)` 범위 비교라 풀 수만큼 O(k) — 풀이 연속 배치되어 있으면 경계 배열 이진 탐색이나, 풀 영역을 2의 거듭제곱 정렬해 주소 상위 비트로 바로 찾을 수도 있다. 실행 결과:

```text
[4] size-class set hwm: 32B 10/16, 64B 8/8, 128B 4/4
```

1~128 B 균등 랜덤 요청이면 64·128 B 클래스가 포화(hwm == N)되고 32 B 풀은 10/16만 쓴다 — 크기 분포에 맞게 개수를 다시 배분해야 한다는 신호다. fallback은 고갈을 늦추지만 큰 블록을 작은 요청이 먹어 큰 요청이 실패할 수 있다. 이 방향을 끝까지 밀면 페이지 단위 슬랩(DS-M03)이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 16/16 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_asan && ./practice_asan   # 16/16, 보고 없음
```

## 10. 핵심 정리

- 고정 블록 풀 = 버퍼를 stride 단위로 자르고 **빈 블록 첫 포인터 칸에 next** 를 넣은 LIFO free list. alloc = pop, free = push, 루프 없는 O(1), 단편화 없음, 블록당 오버헤드 0.
- stride = `max(size, sizeof(void*))` 를 `_Alignof(max_align_t)` 배수로 (이 맥 8 → 20 B는 24). DMA·캐시 버퍼는 캐시 라인(M7: 32 B)으로 더 크게.
- 링크 접근은 `memcpy`(유효 타입 규칙), 포인터 범위 비교는 `uintptr_t`(관계 비교 UB 회피).
- 디버그 free: 범위 → 블록 시작 → live 비트 순으로 검사, 실패 시 상태 무변경 + 오류 코드.
- hwm은 최대 동시 사용량. `reset` 은 `hwm = used`. **hwm == N 이면 포화라 수요 미상** — 넉넉한 풀로 최악 시나리오에서 재고 여유를 더한다 (Part C: 22 → 28~32).
- 풀 메모리는 ASan이 모른다 → free 때 `ASAN_POISON_MEMORY_REGION`(링크 쓴 뒤), alloc 때 unpoison(링크 읽기 전). 보드에선 `0x6B` 소프트웨어 포이즌.
- ISR/태스크 공유 시 짧은 PRIMASK 구간. 락-프리 Treiber stack은 ABA 주의.
- 같은 구조: Zephyr `k_mem_slab`, ThreadX block pool(블록당 포인터 오버헤드), CMSIS `osMemoryPool*`, Linux SLUB free 포인터(DS-M03, see:OS).

## 참고자료

- ISO/IEC 9899:2011 (C11) — 6.5p7 유효 타입, 6.5.8p5 포인터 관계 비교, 7.19 `max_align_t`, 7.22.3 메모리 관리 함수의 정렬 보장
- [Zephyr — Memory Slabs (`k_mem_slab`)](https://docs.zephyrproject.org/latest/kernel/memory_management/slabs.html), [`kernel/mem_slab.c`](https://github.com/zephyrproject-rtos/zephyr/blob/main/kernel/mem_slab.c)
- [FreeRTOS — Memory Management (heap_1 ~ heap_5)](https://www.freertos.org/a00111.html)
- [Eclipse ThreadX 문서 — Memory block pools (`tx_block_pool_create`)](https://github.com/eclipse-threadx/rtos-docs)
- [CMSIS-RTOS2 — Memory Pool API](https://arm-software.github.io/CMSIS_6/latest/RTOS2/group__CMSIS__RTOS__PoolMgmt.html)
- [Linux `include/linux/poison.h`](https://github.com/torvalds/linux/blob/master/include/linux/poison.h), [`mm/slub.c`](https://github.com/torvalds/linux/blob/master/mm/slub.c)
- [AddressSanitizer — manual poisoning (`sanitizer/asan_interface.h`)](https://github.com/google/sanitizers/wiki/AddressSanitizerManualPoisoning)
- ARMv7-M / ARMv6-M Architecture Reference Manual — 비정렬 접근과 UsageFault/HardFault, ARMv6-M에 나눗셈 명령 없음
- MISRA C:2012 — Rule 21.3, Directive 4.12 (동적 메모리 금지)
- Bonwick, *The Slab Allocator: An Object-Caching Kernel Memory Allocator* (USENIX 1994) — DS-M03으로 이어짐
