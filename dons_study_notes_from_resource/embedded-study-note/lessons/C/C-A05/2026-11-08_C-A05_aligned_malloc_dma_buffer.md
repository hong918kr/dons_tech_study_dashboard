---
id: C-A05
time: 90분
updated: 2026-11-08
---
# C-A05 정렬 보장 할당: aligned_malloc과 DMA 버퍼

## 학습 목표

- `malloc` 과할당(over-allocation) + **정렬 포인터 바로 앞에 원래 포인터를 숨기는** 방식으로 `aligned_malloc` / `aligned_free` 를 구현하고, 요청 크기 공식 `size + HDR + (align − 1)` 이 왜 정확히 충분한지 증명할 수 있다.
- 면접관이 파고드는 가장자리 — 2의 거듭제곱이 아닌 align, `size` 오버플로, `malloc` 실패, `free()` 와 섞어 쓰기, `realloc` — 를 테스트로 고정하고, 왜 `realloc(raw)` 로는 정렬을 지킬 수 없는지 설명한다.
- DMA 버퍼의 세 가지 하드웨어 조건 — **시작 정렬, 4 KB 경계 안 넘기, 캐시 라인 격리** — 를 비트 연산 한 줄(`(a ^ (a+len−1)) & ~(B−1)`)과 "다음 경계로 옮기기" 로 만족시키고, 필요한 과할당량 `HDR + (a−1) + 2n` 을 유도한다.
- 펌웨어 현실: 힙 없이 정적 아레나(`.bss.dma`, MPU non-cacheable)에서 이더넷 RX 링 버퍼를 잘라 쓰고, 표준 `aligned_alloc` / `posix_memalign` 이 이 맥에서 실제로 어떻게 동작하는지(실측) 비교한다.

## 1. 문제

"정렬된 메모리를 돌려주는 malloc 을 직접 짜 보세요" 는 펌웨어·드라이버 면접의 단골 코딩 문제다 — 포인터 산술, 비트 마스크, 메모리 소유권을 한 번에 보고, 꼬리질문으로 DMA·캐시·MMU 지식까지 끌어낼 수 있기 때문이다. 시니어 레벨에서는 "구현했다" 보다 **"어떤 조건에서 깨지는지 알고 테스트로 막았다"** 가 평가된다.

`malloc` 은 기본 타입 정렬만 보장한다 (C11 7.22.3p1 "fundamental alignment"). 실측으로 이 맥은 16 B, Cortex-M 의 newlib 은 보통 8 B 다. 그런데 하드웨어는 더 많은 것을 요구한다:

| 요구하는 쪽 | 조건 | 예 |
|---|---|---|
| D-cache 유지보수 | 버퍼 시작·끝이 **캐시 라인**(Cortex-M7 32 B) 경계 | `SCB_InvalidateDCache_by_Addr(addr, size)` 는 라인 단위 |
| DMA 엔진 / 버스 | 버스트가 **4 KB 경계를 넘으면 안 됨** | AMBA AXI 규칙, Linux `dma_pool_create(..., boundary)` |
| 페이지 기반 디스크립터 | 한 엔트리가 **페이지 하나 안** | NVMe PRP 엔트리 |
| MPU | 영역 크기 2^k, 시작이 크기의 배수 | ARMv7-M MPU (연습 4) |

이번 레슨의 할당기는 정렬 포인터 `p` 바로 앞에 24바이트 헤더 `{raw, size, align}` 를 숨긴다. `aligned_free(p)` 는 그 헤더에서 `raw` 를 꺼내 `free` 한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">aligned_malloc(100, 64): raw = 0x10000F30 (malloc 이 준 16 B 정렬 주소)</text>
  <rect x="20" y="50" width="150" height="44" class="f-mut" stroke="currentColor"/>
  <text x="32" y="70" fill="currentColor">앞쪽 버림 56 B</text>
  <text x="32" y="86" fill="currentColor" font-family="monospace">F30..F67</text>
  <rect x="170" y="50" width="100" height="44" class="f-warn" stroke="currentColor"/>
  <text x="180" y="70" fill="currentColor">헤더 24 B</text>
  <text x="180" y="86" fill="currentColor" font-family="monospace">raw,size,al</text>
  <rect x="270" y="50" width="380" height="44" class="f-acc" stroke="currentColor"/>
  <text x="282" y="70" fill="currentColor">사용자 영역 100 B</text>
  <text x="282" y="86" fill="currentColor" font-family="monospace">p = 0x10000F80 (64 의 배수)</text>
  <rect x="650" y="50" width="70" height="44" class="f-mut" stroke="currentColor"/>
  <text x="660" y="76" fill="currentColor">7 B</text>
  <line x1="20" y1="110" x2="720" y2="110" stroke="currentColor"/>
  <text x="20" y="128" fill="currentColor" font-family="monospace">0xF30</text>
  <text x="250" y="128" fill="currentColor" font-family="monospace">0xF80</text>
  <text x="660" y="128" fill="currentColor" font-family="monospace">0xFEB</text>
  <path d="M220 100 C 220 170, 40 170, 40 100" fill="none" stroke="currentColor"/>
  <text x="70" y="168" fill="currentColor">헤더의 raw → 블록 시작을 가리킨다 (aligned_free 가 free 할 주소)</text>
  <g fill="currentColor">
    <text x="20" y="200">요청 = 100 + 24 + 63 = 187 B.  p = align_up(raw + 24, 64) = align_up(0xF48, 64) = 0xF80</text>
    <text x="20" y="222">앞쪽 버림은 0..63 B, 헤더는 항상 24 B → 어떤 raw 가 와도 p + 100 ≤ raw + 187</text>
    <text x="20" y="242">free(p) 를 부르면: p 는 malloc 이 준 주소가 아니다 → 크래시 (demo_wrong_free.c)</text>
  </g>
</svg>
```

계측을 위해 시스템 할당기는 `sys_malloc` / `sys_free` 로 감싸져 있다 (주어진 코드). 이들은 호출 수, 마지막 요청 크기, 살아 있는 raw 블록 목록을 기록하고, malloc 이 준 적 없는 포인터가 `sys_free` 에 오면 진짜 `free` 대신 `g_bad_free` 를 올린다 — 그래서 틀린 구현도 크래시 없이 FAIL 로 끝난다. `g_fail_next = 1` 이면 다음 할당이 실패하고, 1 GB 넘는 요청은 "메모리 부족" 으로 NULL 이다.

### Part A — aligned_malloc / aligned_free

- `void *aligned_malloc(size_t size, size_t align)` — `align` 은 2^k. 그보다 작은 정렬(1, 2, 4)은 `alignof(ahdr_t)` 로 올린다. `sys_malloc` 요청은 정확히 `size + HDR + (a − 1)`. 헤더는 주어진 `place()` 로 쓴다.
- `void aligned_free(void *p)` — 헤더의 `raw` 를 `sys_free`. `NULL` 은 아무 일도 하지 않는다.
- 랜덤 테스트: 정렬 2^3..2^12, 크기 1..5000, 3000번, 동시에 최대 32블록 살아 있음. 각 블록에 고유 무늬를 채우고, 해제 직전에 무늬가 그대로인지(=블록끼리 침범 없음) 확인한다.

### Part B — 가장자리와 aligned_realloc

- `align` 이 0 / 3 / 24 / 4097, `size == 0` → `NULL` 이고 **`malloc` 을 부르지도 않는다**.
- `size + HDR + (a − 1)` 이 `SIZE_MAX` 를 넘으면 `NULL` (호출 없음). 정확히 `SIZE_MAX` 가 되는 크기는 호출은 하되 실패한다 — 경계 한 칸 차이를 테스트가 본다.
- `void *aligned_realloc(void *p, size_t new_size, size_t align)` — `p == NULL` 이면 할당, `new_size == 0` 이면 해제 후 `NULL`, 그 외엔 새 정렬로 할당 + 앞부분 복사 + 옛 블록 해제. **실패하면 `NULL` 이고 `p` 는 그대로 유효**.

### Part C — DMA 버퍼: 경계와 캐시 라인

- `bool crosses_boundary(uintptr_t addr, size_t len, size_t boundary)` — `[addr, addr+len)` 이 `boundary` 의 배수를 가로지르나. `len == 0` 은 false, `len > boundary` 는 true, 주소 공간 끝에서 감싸면 true.
- `void *dma_buf_alloc(size_t size, size_t align, size_t boundary)` — 시작 정렬 `max(align, 32)`, 크기는 32 B 단위로 올린 `n`, `[p, p+n)` 은 boundary 를 넘지 않고, **버퍼가 걸친 캐시 라인 전부가 내 raw 블록 안** (이웃과 라인 공유 없음). `aligned_free` 로 해제. 요청은 `HDR + (a − 1) + 2n`.
- `void *arena_alloc(arena_t *ar, size_t size, size_t align, size_t boundary)` — 같은 규칙을 **힙 없는** 4 KB 정렬 정적 아레나(16 KB)에 적용하는 범프 할당기. 실패하면 `used` 는 그대로. 테스트: 이더넷 RX 버퍼 1536 B × 8 + 디스크립터 테이블.

## 2. Problem (English)

> **Aligned allocation on top of malloc, and DMA-safe buffers.**
>
> You are given an instrumented `sys_malloc(n)` / `sys_free(p)` and a 24-byte header type `ahdr_t { void *raw; size_t size; size_t align; }` with helpers `place(raw, off, size, align)` (writes the header just below `raw + off` and returns `raw + off`) and `hdr_of(p)`.
>
> 1. `void *aligned_malloc(size_t size, size_t align)` / `void aligned_free(void *p)`. `align` must be a power of two (else `NULL`); effective alignment `a = max(align, alignof(ahdr_t))`; `size == 0` → `NULL`. Request exactly `size + sizeof(ahdr_t) + a − 1` bytes; if that overflows `size_t`, return `NULL` without calling `sys_malloc`. `aligned_free(NULL)` is a no-op.
> 2. `void *aligned_realloc(void *p, size_t n, size_t align)` with `realloc`-like semantics; on failure the old block stays valid.
> 3. `bool crosses_boundary(uintptr_t addr, size_t len, size_t B)`; `void *dma_buf_alloc(size_t size, size_t align, size_t B)` returning a buffer aligned to `max(align, 32)`, not crossing a multiple of `B`, whose cache lines (32 B) all lie inside its own raw block; and a heap-free `arena_alloc(arena_t *, size, align, B)` bump allocator with the same rules.
>
> Example: `aligned_malloc(100, 64)` requests 187 bytes; with `raw = 0x10000F30` it returns `0x10000F80`. `crosses_boundary(0x0FF0, 16, 4096)` is false, `(0x0FF0, 17, 4096)` is true. In a 4 KB-aligned 16 KB arena, eight 1536-byte RX buffers land at offsets 0, 1536, 4096, 5632, 8192, 9728, 12288, 13824; the ninth fails.
>
> Follow-ups: Why not just `free(p)`? Why can't `aligned_realloc` call `realloc`? Why must a DMA buffer not cross 4 KB, and why must its *end* be cache-line aligned too? How would you do this without a heap?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "24/53 passed" — 통과하는 24개는 "NULL 이어야 하는" 경우와 하네스 자체 검사
```

`GIVEN` 은 `__attribute__((unused))` 다. starter 에서는 `sys_malloc`, `place` 같은 주어진 도우미를 아직 아무도 부르지 않아 `-Wunused-function` 이 `-Werror` 로 빌드를 깨기 때문에 붙였다.

관찰용 파일 세 개 (하네스와 별개):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_std_alloc.c -o dsa && ./dsa             # aligned_alloc/posix_memalign 실측
cc -std=c11 -O0 -g demo_wrong_free.c -o dwf && ./dwf                                # aligned 포인터를 free() → 크래시
cc -std=c11 -O0 -g -fsanitize=address demo_wrong_free.c -o dwfa && ./dwfa           # 같은 버그를 ASan 으로
clang --target=thumbv7em-none-eabi -mcpu=cortex-m7 -mthumb -O2 -S demo_dma_section.c -o -   # 정적 DMA 버퍼 asm
```

## 4. 힌트

<details>
<summary>힌트 1 — Part A: 최악의 경우를 그려 보면 요청 크기가 나온다</summary>

헤더 자리 `HDR` 을 먼저 비워 두고(`raw + HDR`), 거기서 `a` 의 배수로 올린다. 올림으로 건너뛰는 양은 0..a−1. 그러니 `HDR + (a − 1) + size` 면 어떤 `raw` 에서도 끝까지 들어간다. 포인터는 정수로 만든 주소를 다시 포인터로 바꾸지 말고, **오프셋**(`정렬된 주소 − raw`)을 구해 `(unsigned char *)raw + off` 로 만든다 — `place()` 가 그렇게 한다.

</details>

<details>
<summary>힌트 2 — Part B: 오버플로 검사는 덧셈 전에, 뺄셈으로</summary>

`size + HDR + a − 1 > SIZE_MAX` 는 C 에서 절대 참이 될 수 없다 (부호 없는 덧셈은 감싼다). 같은 뜻을 `size > SIZE_MAX − HDR − (a − 1)` 로 쓴다 — 오른쪽은 넘치지 않는다. `aligned_realloc` 은 "새로 할당이 성공한 뒤에만" 옛 블록을 건드린다. 옛 크기는 헤더에 있다.

</details>

<details>
<summary>힌트 3 — Part C: 가로지르는지는 첫 바이트와 마지막 바이트의 "페이지 번호" 비교</summary>

`addr` 과 `addr + len − 1` 의 상위 비트(`~(B − 1)` 로 남긴 부분)가 같으면 같은 블록 안이다. XOR 해서 상위 비트에 1 이 하나라도 있으면 다르다. `len − 1` 인 이유: 끝은 포함하지 않는다 (`0xF00 + 0x100` 은 정확히 0x1000 에서 끝나지만 넘지 않는다). 넘친다면 **다음 경계** `align_up(s + 1, B)` 로 옮긴다 — 그 자리는 `B ≥ a` 라 자동으로 `a` 정렬이다.

</details>

<details>
<summary>힌트 4 — Part C: 옮기면 얼마나 더 필요한가</summary>

가로지른다는 건 다음 경계가 `s + n − 1` 이하라는 뜻이다. 거기서 `n` 을 더 쓰면 끝은 `s + 2n − 1` 이하. `s ≤ raw + HDR + a − 1` 이므로 `HDR + (a − 1) + 2n` 바이트면 충분하다. 캐시 라인 격리는 "크기를 32 의 배수로 올린 `n` 전체가 raw 블록 안" 이면 자동으로 성립한다.

</details>

## 5. 풀이

### Part A — 과할당, 오프셋, 헤더

```c
static void *aligned_malloc(size_t size, size_t align) {
    if (size == 0 || !is_pow2(align)) return NULL;
    size_t a = max_sz(align, alignof(ahdr_t));               /* 헤더 자리도 정렬되게 */
    if (size > SIZE_MAX - HDR - (a - 1)) return NULL;        /* 요청 크기 계산이 넘친다 */
    void *raw = sys_malloc(size + HDR + (a - 1));
    if (raw == NULL) return NULL;
    uintptr_t r = (uintptr_t)raw;
    return place(raw, (size_t)(align_up(r + HDR, a) - r), size, a);
}
static void aligned_free(void *p) {
    if (p == NULL) return;
    sys_free(hdr_of(p).raw);
}
```

**요청 크기가 정확히 충분하다는 증명.** `s = align_up(raw + HDR, a)` 라 두면 `raw + HDR ≤ s ≤ raw + HDR + a − 1`. 사용자 영역 `[s, s + size)` 의 끝은 최대 `raw + HDR + a − 1 + size` = raw 블록의 끝. 헤더 `[s − HDR, s)` 는 `s − HDR ≥ raw` 라 블록 앞을 벗어나지 않는다. 그리고 `a − 1` 보다 하나 적게 잡으면 `raw + HDR` 이 `a` 의 배수보다 1 큰 경우(최악)에 1바이트 모자란다 → **이 공식이 최소**다. 테스트는 `g_last_req == 100 + HDR + 63` 과 `inside_raw(p − HDR, p + 100)` 으로 둘 다 확인한다.

| 단계 | 값 (`raw = 0x10000F30`, size 100, align 64) |
|---|---|
| `a = max(64, alignof(ahdr_t)=8)` | 64 |
| 요청 | 100 + 24 + 63 = 187 → raw 블록 `[0xF30, 0xFEB)` |
| `raw + HDR` | 0xF48 |
| `align_up(0xF48, 64)` | `(0xF48 + 0x3F) & ~0x3F` = 0xF80 |
| `off` | 0x50 = 80 → 헤더 `[0xF68, 0xF80)`, 사용자 `[0xF80, 0xFE4)` |

**왜 `(uintptr_t)` → 오프셋 → `raw + off` 인가.** 포인터↔정수 변환은 구현 정의다 (C11 6.3.2.3p5–6), `uintptr_t` 자체도 선택(optional) 타입이다 (7.20.1.4). 정렬 **판정**은 정수로 하되(모든 실제 컴파일러에서 주소의 하위 비트 그대로), 결과 포인터는 정수에서 만들지 않고 **원래 포인터에서 바이트 단위로 전진**해 만든다. 이렇게 하면 포인터 출처(provenance)가 `raw` 그대로 유지돼, 포인터 출처를 추적하는 최적화기나 CHERI 같은 capability 하드웨어에서도 뜻이 분명하다. (C-B05 에서 본 `align_up` 관용구의 할당기 버전.)

**헤더를 memcpy 로 읽고 쓰는 이유와 `a ≥ alignof(ahdr_t)` 의 관계.** `place()`/`hdr_of()` 는 `memcpy` 를 쓰므로 헤더가 비정렬이어도 이 코드는 맞다. 그래도 `a` 를 최소 8 로 올리는 이유: ① 실무 코드는 `((ahdr_t *)p)[-1]` 처럼 직접 접근하는 경우가 많고, 그러려면 `p − 24` 가 8 정렬이어야 한다 (Cortex-M0 이면 비정렬 `LDR` 이 HardFault — C-M04). ② `align = 1` 을 받아도 결과가 8 정렬이 되어 손해가 없다. 테스트는 align 1/2/4 를 각각 확인한다.

**`align_up` 의 마스크는 반드시 주소 폭으로.** `~(uintptr_t)(a − 1)` 처럼 **먼저 넓히고** 뒤집는다. `unsigned al` 을 받아 `~(al − 1)` 을 쓰면 32비트 `0xFFFFFFC0` 이 만들어진 뒤 64비트로 0-확장되어 `0x00000000FFFFFFC0` — 이 맥의 힙 주소(`0x10xxxxxxx`, ASan 이면 `0x60f0...`)의 상위 32비트가 지워진다 (C-B02 승격 규칙의 실전 버그).

**랜덤 테스트가 보는 것.** 3000번 반복, 매번 32개 슬롯 중 하나를 골라 옛 블록의 무늬를 검사하고 해제한 뒤 새 블록(정렬 2^3..2^12 중 하나, 크기 1..5000)을 받아 `tag` 로 채운다. 확인 항목: 정렬, `[p, p+n)` 이 자기 raw 블록 안, 무늬 보존(다른 블록이 침범하지 않음), 마지막에 `g_nlive == 0 && g_bad_free == 0 && malloc 수 == free 수` (누수·잘못된 free 없음). ASan 빌드에서 돌리면 1바이트라도 블록 밖을 쓰는 순간 보고된다 — 이 레슨의 모든 체크가 ASan/UBSan 아래서도 그대로 통과한다 (6. 정답 코드).

### Part A' — 표준 함수는? (demo_std_alloc.c 실측)

```text
alignof(max_align_t)=8  sizeof(void*)=8
malloc: 1000 blocks (1..3000 B), min alignment seen = 16 bytes
aligned_alloc(   64,   128) -> ok    addr%al=0
aligned_alloc(   64,   100) -> NULL  errno=22 (Invalid argument)
aligned_alloc( 4096,  4096) -> ok    addr%al=0
aligned_alloc( 4096,    10) -> NULL  errno=22 (Invalid argument)
aligned_alloc(   48,    96) -> NULL  errno=22 (Invalid argument)
aligned_alloc(    4,    16) -> NULL  errno=22 (Invalid argument)
posix_memalign(al=  64,  100) -> ret=0
posix_memalign(al=   4,   16) -> ret=22 (EINVAL)
posix_memalign(al=  48,   96) -> ret=22 (EINVAL)
_Alignas(4096) static buffer: addr % 4096 = 0
```

읽는 법:

- 이 맥(arm64)은 `alignof(max_align_t)` 가 **8** 인데 `malloc` 은 실제로 16 B 정렬을 준다. 표준이 요구하는 건 "기본 정렬" 이지 그 이상이 아니다 — 16 에 기대는 코드는 다른 libc 에서 깨진다.
- `aligned_alloc(64, 100)` 이 **NULL**: C11 7.22.3.1 원문은 "size 는 alignment 의 정수배여야 한다" 였고(어기면 UB), DR 460 으로 C17 에서 그 문장이 빠졌다. macOS libmalloc 은 여전히 배수가 아니면 `EINVAL` 로 거절한다. **이식 가능한 코드는 size 를 align 의 배수로 올려서 부른다.**
- `aligned_alloc(4, 16)` 도 NULL — 4 는 유효한 정렬인데 이 구현은 `sizeof(void *)` 미만을 받지 않는다. `posix_memalign` 은 POSIX 가 "sizeof(void *) 의 배수인 2의 거듭제곱" 을 요구해 `EINVAL` 을 **반환값으로** 준다 (errno 를 쓰지 않는다).
- 두 함수가 준 포인터는 그냥 `free()` 한다. 반면 우리 `aligned_malloc` (그리고 MSVC 의 `_aligned_malloc`)은 **반드시 짝 함수**로 해제해야 한다. C23 은 크기를 알려 주는 `free_aligned_sized()` 를 추가했다.

그럼 왜 직접 짜나? ① 펌웨어 libc(newlib/picolibc/벤더 런타임)마다 지원과 동작이 다르다. ② 경계 조건(4 KB)·캐시 라인 격리·정적 아레나처럼 표준 함수가 표현 못 하는 조건이 필요하다. ③ 면접에서는 "표준 함수 이름을 아는가" 다음에 "그 안에서 무슨 일이 일어나는가" 를 묻는다.

### Part A'' — `free(p)` 를 부르면 (demo_wrong_free.c)

```text
$ ./dwf; echo "exit=$?"    # 일반 빌드 (macOS libmalloc)
raw=0x1050b16e0  aligned=0x1050b1700  (offset 32)
exit=133

$ ./dwfa                   # -fsanitize=address (발췌)
raw=0x60f000000040  aligned=0x60f000000080  (offset 64)
==79754==ERROR: AddressSanitizer: attempting free on address which was not malloc()-ed: 0x60f000000080 in thread T0
    #0 0x000102e11258 in free+0x7c (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x41258)
    #1 0x000102560898 in main demo_wrong_free.c:26
...
0x60f000000080 is located 64 bytes inside of 171-byte region [0x60f000000040,0x60f0000000eb)
allocated by thread T0 here:
    #0 0x000102e11164 in malloc+0x78 (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x41164)
    #1 0x0001025608e0 in aligned_malloc_min demo_wrong_free.c:12
SUMMARY: AddressSanitizer: bad-free demo_wrong_free.c:26 in main
```

이 맥의 libmalloc 은 stderr 메시지 없이 트랩으로 끝났다 (133 = 128 + 5, SIGTRAP). 그 뒤의 `printf("not reached?")` 는 실행되지 않았다. ASan 은 "블록 안쪽 64 바이트 지점" 을 정확히 짚는다 (171 = 100 + 8 + 63, 최소판은 헤더가 포인터 하나). 더 나쁜 경우는 **크래시가 안 나는** 할당기다 — 펌웨어의 단순한 힙 구현은 `free` 가 받은 주소 앞의 바이트를 블록 크기로 믿고 free list 에 넣어 버려, 몇 분 뒤 전혀 다른 곳에서 힙이 깨진다. 이 레슨 하네스의 `sys_free` 가 "모르는 포인터" 를 세기만 하고 진짜 `free` 를 하지 않는 이유다.

### Part B — 가장자리

| 입력 | 결과 | 왜 |
|---|---|---|
| `align` = 0, 3, 24, 4097 | NULL, malloc 호출 0 | 마스크 `~(a−1)` 은 2^k 에서만 "아래 k 비트 지우기" 다. 24 면 `~23 = …11101000` → 8 의 배수조차 아닌 곳으로 "올림". C11 6.2.8p4 도 유효한 정렬은 2의 거듭제곱뿐이라고 정한다 |
| `size == 0` | NULL | `malloc(0)` 은 구현 정의(7.22.3p1). API 에서 한 가지로 정해 두고 문서화한다 |
| `size = SIZE_MAX − 24 − 63` | malloc(SIZE_MAX) 호출 → NULL | 경계: 계산은 넘치지 않음 |
| `size = SIZE_MAX − 24 − 62` | NULL, 호출 0 | 넘침: 그대로 더하면 0 근처로 감싸 **아주 작은 블록**을 받고 사용자는 큰 버퍼라 믿는다 → 힙 오버플로 (CVE 단골) |
| `g_fail_next = 1` | NULL, 살아 있는 블록 0 | 실패 전파, 누수 없음 |
| `aligned_free(NULL)` | `sys_free` 호출 0 | `free(NULL)` 과 같은 계약. `NULL` 의 헤더를 읽으면 즉사 |

오버플로 검사는 `size > SIZE_MAX − HDR − (a − 1)` 형태로 쓴다. `HDR + (a − 1)` 은 `a ≤ 2^63` 이라 넘치지 않으므로 오른쪽 식은 항상 안전하다.

**aligned_realloc — 왜 `realloc(raw, …)` 이 안 되나.**

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">realloc(raw) 은 블록을 옮길 수 있고, 옮긴 곳의 raw 정렬은 또 16 B 뿐이다</text>
  <text x="10" y="62" fill="currentColor">전</text>
  <rect x="60" y="44" width="56" height="28" class="f-mut" stroke="currentColor"/>
  <rect x="116" y="44" width="50" height="28" class="f-warn" stroke="currentColor"/><text x="122" y="63" fill="currentColor">hdr</text>
  <rect x="166" y="44" width="200" height="28" class="f-acc" stroke="currentColor"/><text x="176" y="63" fill="currentColor" font-family="monospace">p = raw+80 (64정렬)</text>
  <text x="380" y="63" fill="currentColor" font-family="monospace">raw = 0x...F30</text>
  <text x="10" y="122" fill="currentColor">후</text>
  <rect x="60" y="104" width="56" height="28" class="f-mut" stroke="currentColor"/>
  <rect x="116" y="104" width="50" height="28" class="f-warn" stroke="currentColor"/><text x="122" y="123" fill="currentColor">hdr</text>
  <rect x="166" y="104" width="200" height="28" class="f-warn" stroke="currentColor"/><text x="176" y="123" fill="currentColor" font-family="monospace">raw'+80 = 64정렬 아님</text>
  <text x="380" y="123" fill="currentColor" font-family="monospace">raw' = 0x...010 (새 주소)</text>
  <g fill="currentColor">
    <text x="10" y="166">realloc 은 바이트를 "그대로" 복사한다 → 오프셋 80 도 그대로. 그런데 raw' + 80 이 64 의 배수일 이유가 없다.</text>
    <text x="10" y="188">새 정렬 위치로 옮기려면 결국 memmove 가 한 번 더 필요하고, 헤더의 raw 도 고쳐야 한다.</text>
    <text x="10" y="210">→ 깔끔한 답: aligned_malloc(new) + memcpy(min(old, new)) + aligned_free(old). 옛 크기는 헤더의 size.</text>
  </g>
</svg>
```

```c
static void *aligned_realloc(void *p, size_t new_size, size_t align) {
    if (p == NULL) return aligned_malloc(new_size, align);
    if (new_size == 0) { aligned_free(p); return NULL; }
    void *q = aligned_malloc(new_size, align);
    if (q == NULL) return NULL;                    /* 옛 블록은 건드리지 않는다 */
    size_t old = hdr_of(p).size;
    memcpy(q, p, old < new_size ? old : new_size);
    aligned_free(p);
    return q;
}
```

- 헤더에 `size` 를 넣은 이유가 여기 있다. 원래 포인터만 저장하는 최소판(8 B 헤더)으로는 복사할 길이를 모른다.
- 실패 시 `p` 를 살려 두는 건 `realloc` 의 계약과 같다 — 그래서 `p = aligned_realloc(p, …)` 처럼 바로 덮어쓰면 실패 때 누수다 (7. 자주 하는 실수).
- `new_size == 0` 의 의미는 API 가 정하는 것이다. 표준 `realloc(p, 0)` 은 C17 까지 구현 정의였고 **C23 에서는 UB** 가 됐다 — 그래서 우리 함수는 동작을 명시한다.
- 개선 여지: 줄이는 경우 + 정렬이 같으면 제자리 반환(복사 0), 다만 헤더의 `size` 를 갱신해야 한다.

### Part C — 경계 검사 한 줄

```c
static bool crosses_boundary(uintptr_t addr, size_t len, size_t boundary) {
    if (len == 0) return false;
    if (len > boundary) return true;
    return ((addr ^ (addr + (len - 1))) & ~(uintptr_t)(boundary - 1)) != 0;
}
```

| addr | len | B | 첫 바이트 블록 | 끝 바이트 | 결과 |
|---|---|---|---|---|---|
| 0x0FF0 | 16 | 4096 | 0 | 0x0FFF → 0 | false |
| 0x0FF0 | 17 | 4096 | 0 | 0x1000 → 1 | **true** |
| 0x1000 | 4096 | 4096 | 1 | 0x1FFF → 1 | false (한 페이지 꽉) |
| 0x1FFF | 2 | 4096 | 1 | 0x2000 → 2 | true |
| `UINTPTR_MAX−15` | 17 | 4096 | 최상위 | 감싸서 0x0 | true |
| 0x1FFF | `SIZE_MAX` | 4096 | — | — | true (`len > B`) |

`len > B` 줄이 없으면 이상한 일이 생긴다: `addr = 0x1FFF, len = SIZE_MAX` 는 `addr + len − 1` 이 감싸서 `0x1FFD` 가 되고, XOR 은 "같은 페이지" 라고 답한다. 주어진 모의 DMA 엔진(`dma_engine_accepts`)은 일부러 **다른 공식**(`a / 4096 == (a + len − 1) / 4096`)으로 같은 규칙을 검사한다 — 랜덤 테스트의 독립 오라클(oracle)이다.

**왜 4 KB 인가.** AMBA AXI 는 한 버스트가 4 KB 주소 경계를 넘지 못하게 정해 두었다(슬레이브 하나가 4 KB 영역을 맡는다는 가정). MMU 가 있는 시스템에서는 가상 주소로 연속인 두 페이지가 물리적으로 연속이라는 보장이 없어서, 4 KB 를 넘는 DMA 는 스캐터-개더(scatter-gather)로 쪼개야 한다. NVMe 의 PRP 엔트리도 하나가 메모리 페이지 하나만 가리킨다 (첫 엔트리만 페이지 내 오프셋 허용). Linux 는 이 요구를 `dma_pool_create(name, dev, size, align, boundary)` 의 `boundary` 인자로 표현한다 (연습 2 가 그 모양). 64 KB 경계를 넘지 못하던 ISA 8237 DMA 처럼 경계 값은 장치마다 다르다 — 그래서 `boundary` 는 인자다.

### Part C — dma_buf_alloc: 옮기기와 과할당량

```c
size_t a = max_sz(max_sz(align, CACHE_LINE), alignof(ahdr_t));
size_t n = (size + (CACHE_LINE - 1)) & ~(size_t)(CACHE_LINE - 1);   /* 끝도 라인 경계까지 */
/* ... 인자·오버플로 검사 (아래) ... */
void *raw = sys_malloc(HDR + (a - 1) + 2 * n);
uintptr_t r = (uintptr_t)raw, s = align_up(r + HDR, a);
if (crosses_boundary(s, n, boundary)) s = align_up(s + 1, boundary);   /* 다음 경계로 */
return place(raw, (size_t)(s - r), size, a);
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">dma_buf_alloc(1536, 32, 4096), raw = 0x20000A10 → 요청 24 + 31 + 2·1536 = 3127 B</text>
  <line x1="20" y1="60" x2="720" y2="60" stroke="currentColor"/>
  <text x="20" y="52" fill="currentColor" font-family="monospace">0xA10</text>
  <text x="372" y="52" fill="currentColor" font-family="monospace">0x1000 (4 KB 경계)</text>
  <text x="650" y="52" fill="currentColor" font-family="monospace">0x1647</text>
  <line x1="380" y1="60" x2="380" y2="200" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="64" y="72" width="410" height="30" class="f-warn" stroke="currentColor"/>
  <text x="74" y="92" fill="currentColor">1차 후보 s = align_up(0xA28, 32) = 0xA40 … 0x1040 → 경계를 넘는다</text>
  <rect x="356" y="118" width="24" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="380" y="118" width="250" height="30" class="f-ok" stroke="currentColor"/>
  <text x="390" y="138" fill="currentColor">s = 0x1000 … 0x1600 (안 넘음)</text>
  <text x="200" y="138" fill="currentColor">hdr 0xFE8 →</text>
  <g fill="currentColor">
    <text x="20" y="180">옮긴 뒤 끝 ≤ (1차 s) + 2n − 1 ≤ raw + 24 + 31 + 2n − 1 → 3127 B 안에 반드시 들어간다.</text>
    <text x="20" y="202">새 시작 0x1000 은 4096 의 배수 → 32 의 배수이기도 하다 (B ≥ a 이고 둘 다 2^k).</text>
    <text x="20" y="224">헤더는 0x1000 − 24 = 0xFE8: 1차 후보보다 뒤라서 raw + 24 이상 → 블록 안.</text>
    <text x="20" y="244">끝 0x1600 은 라인 경계, 0x1600..0x1647 은 내 블록 → 마지막 캐시 라인을 아무와도 공유하지 않는다.</text>
  </g>
</svg>
```

검사 순서가 오버플로를 막는다: `size > boundary` 를 먼저 거르면 `boundary ≤ 2^63` 이라 `size + 31` 이 넘치지 않는다. `2n` 은 `n > (SIZE_MAX − HDR − (a − 1)) / 2` 로 막는다 (테스트: `size = SIZE_MAX/2, B = 2^63` → `n = 2^63`, 호출 없이 NULL).

**캐시 라인 격리는 "시작 정렬" 만으로 부족하다.** Cortex-M7 처럼 D-cache 가 있고 DMA 가 캐시를 모르는(non-coherent) 칩에서, RX 완료 후 CPU 는 버퍼 영역을 invalidate 해야 낡은 캐시 데이터 대신 DMA 가 쓴 메모리를 읽는다. invalidate 는 **라인 단위**다. 버퍼의 마지막 라인에 다른 변수가 같이 살고 있고, CPU 가 그 변수를 막 써서 그 라인이 dirty 라면 — invalidate 가 그 쓰기를 **버린다**. 반대로 DMA 중에 그 라인이 evict 되면 CPU 의 옛 버퍼 내용이 DMA 데이터 위에 덮인다. 그래서 테스트는 `[align_down(p, 32), align_up(p + size, 32))` 전체가 내 raw 블록 안인지 본다 (`diso == 0`). 버그 재현과 clean/invalidate 순서는 ARCH-M12, 디스크립터 링 드라이버 전체는 COMM-A08.

**랜덤 테스트의 경로 커버리지.** 2000번(크기 1..4096, 정렬 2^3..2^12) 중 "다음 경계로 옮기기" 가 실제로 일어난 횟수를 세어 `reloc > 0` 을 확인한다. 이 경로는 malloc 이 준 주소에 달려 있어 고정 입력으로는 강제할 수 없기 때문이다. 이 맥에서 세어 본 값은 `reloc=206/2000` (확인용으로 printf 를 넣어 한 번 돌린 결과 — 하네스에는 출력 없음). 옮기기 경로를 **결정적으로** 확인하는 건 아래 아레나 테스트다.

### Part C — 힙 없는 펌웨어: 정적 아레나

```c
uintptr_t b = (uintptr_t)ar->base, s = align_up(b + ar->used, a);
if (crosses_boundary(s, n, boundary)) s = align_up(s + 1, boundary);
size_t off = (size_t)(s - b);
if (off > ar->cap || n > ar->cap - off) return NULL;     /* 넘치면 used 그대로 */
ar->used = off + n;
return ar->base + off;
```

16 KB, 4 KB 정렬 아레나에 이더넷 RX 버퍼(1536 B)를 8개 요청하면:

| # | 1차 후보 | 넘나? | 최종 오프셋 | 페이지 |
|---|---|---|---|---|
| 0 | 0 | 아니오 | 0 | 0 |
| 1 | 1536 | 아니오 (끝 3071) | 1536 | 0 |
| 2 | 3072 | **예** (끝 4607) | 4096 | 1 |
| 3 | 5632 | 아니오 | 5632 | 1 |
| 4–7 | … | 페이지 0–1 과 같은 패턴 (페이지마다 2개) | 8192, 9728, 12288, 13824 | 2, 3 |
| 8 | 15360 | 예 → 16384 | 공간 없음 → **NULL**, `used` 15360 유지 | — |

페이지마다 1024 B 가 남는다(25% 낭비). 실패 후 `used` 가 그대로이므로, 그 틈에 디스크립터 테이블 256 B(64 정렬)가 15360 에, 20 B 요청이 32 B 를 차지하며 15616 에 들어간다 (테스트가 정확한 주소를 확인). 남은 736 B 에는 800 B 가 들어가지 않는다.

펌웨어에서 이 아레나는 링커 스크립트로 전용 영역에 놓는다. `demo_dma_section.c` 를 Cortex-M7 로 컴파일한 실제 출력 (발췌):

```text
	.type	eth_rx,%object                  @ @eth_rx
	.section	.dma_buf,"aw",%progbits
	.p2align	5, 0x0
eth_rx:
	.zero	6144

	.type	dma_arena,%object               @ @dma_arena
	.section	.bss.dma_arena,"aw",%nobits
	.p2align	12, 0x0
dma_arena:
	.zero	16384

	.type	plain_buf,%object               @ @plain_buf
	.bss
plain_buf:
	.zero	100
```

- `_Alignas(32)` → `.p2align 5`, `_Alignas(4096)` → `.p2align 12`. 정렬 지정이 없는 `plain_buf` 에는 정렬 지시어가 아예 없다 (바이트 배열의 정렬은 1).
- 섹션 이름이 중요하다: `.dma_buf` 는 `%progbits` — 0 으로 채운 6 KB 가 **이미지에 실린다** (링커 스크립트에서 `(NOLOAD)` 로 막지 않으면 플래시를 먹거나 startup 이 복사한다). `.bss.` 로 시작하는 이름은 `%nobits` 라 공간만 차지한다.
- 링커 스크립트에서 이 섹션을 MPU 로 **non-cacheable** 지정한 RAM 영역에 두면 캐시 유지보수 자체가 필요 없어진다 (대신 CPU 접근이 느려진다). ARMv7-M MPU 영역은 크기 2^k(32 B 이상), 시작이 크기의 배수여야 해서 아레나를 그 크기로 정렬해 두는 게 편하다 — 연습 4. 링커 스크립트 쪽 정렬·`ASSERT` 는 TOOL-A07.

| 방식 | 오버헤드 | 해제 | 어디서 |
|---|---|---|---|
| `aligned_malloc` (헤더 24 B) | `24 + a − 1` | 개별 | 호스트, RTOS 힙, 리눅스 유저 공간 |
| 1바이트 오프셋 (연습 1) | `a` | 개별 | align ≤ 128 이면 충분 |
| `dma_buf_alloc` | `24 + a − 1 + n` (2n 요청) | 개별 | 런타임에 DMA 버퍼가 필요한 호스트/리눅스 |
| 정적 아레나 | 페이지 끝 자투리 | 없음 (부팅 때 1회) | MCU 펌웨어 표준 |
| 고정 크기 풀 (연습 2) | 페이지 끝 자투리 | O(1) | USB/네트워크 버퍼, Linux `dma_pool` |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 53/53 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 53/53, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `aligned_malloc` 결과를 `free(p)` | `p` 는 malloc 이 준 주소가 아니다. 이 맥은 SIGTRAP, ASan 은 bad-free, 단순한 펌웨어 힙은 조용히 망가짐 | `aligned_free(p)` (짝 함수), 또는 표준 `aligned_alloc` + `free` |
| `malloc(size + align)` 만 하고 헤더를 `p[-1]` 에 | 올림이 0 바이트였다면(raw 가 이미 정렬) `p == raw` → `p[-1]` 은 블록 **앞** → 힙 메타데이터 파괴 | `align_up(raw + HDR, a)` — 헤더 자리를 먼저 비운다 |
| `~(al - 1)` 에서 `al` 이 `unsigned` | 32비트 마스크가 0-확장되어 64비트 주소 상위 절반이 지워진다 | `~(uintptr_t)(al - 1)` |
| align 검증 없음 (24 등) | 마스크가 "올림" 이 아니게 된다 | `is_pow2(align)` 확인 후 NULL |
| `if (size + HDR + a - 1 < size)` 같은 검사만 | 덧셈이 두 번 감쌀 수 있고 순서에 따라 놓친다 | `size > SIZE_MAX - HDR - (a - 1)` |
| `align < 8` 그대로 사용 + 헤더 직접 접근 | `((ahdr_t *)p)[-1]` 이 비정렬 → M0 HardFault, C 로는 UB | `a = max(align, alignof(ahdr_t))` |
| `realloc(raw, …)` 로 aligned_realloc | 블록이 옮겨지면 오프셋은 그대로라 정렬이 깨지고 헤더의 raw 도 낡는다 | 새로 할당 + 복사 + 해제 |
| `p = aligned_realloc(p, n, a);` | 실패하면 `p` 를 잃어 누수 | `q = …; if (q) p = q;` |
| 경계 검사를 `(addr + len) / B != addr / B` | 끝은 포함하지 않는다 — 경계에 **딱 맞게 끝나는** 버퍼를 넘는다고 판정 | `addr + len - 1` 사용 (XOR 또는 나눗셈) |
| `len > B` 처리 없음 | 감싸는 큰 len 이 "같은 페이지" 로 판정 | `if (len > B) return true;` |
| DMA 버퍼 시작만 32 정렬 | 마지막 라인을 이웃과 공유 → invalidate 가 이웃의 쓰기를 버림 | 크기도 라인 배수로 올리고 그 범위 전체를 소유 |
| 아레나가 실패 때도 `used` 를 옮김 | 실패한 요청이 공간을 "먹는다" | 성공이 확정된 뒤에만 `used` 갱신 |
| ISR 안에서 `aligned_malloc` | 내부의 `malloc` 은 재진입 불가/락 사용 (C-M14, C-A04) | 버퍼는 init 에서 미리 할당, ISR 은 풀에서 O(1) 로 꺼내기 |

## 8. 면접 Q&A

<details>
<summary>Q1. malloc 만 있는 환경에서 aligned_malloc(size, align) 을 구현해 보세요. 원래 포인터는 어디에 두나요?</summary>

`size + align − 1 + sizeof(헤더)` 를 malloc 하고, `raw + sizeof(헤더)` 를 `align` 의 배수로 올린 지점을 돌려준다. 원래 포인터는 **반환 포인터 바로 앞** 에 둔다 — `aligned_free(p)` 가 `p` 만으로 찾을 수 있는 유일한 고정 위치이기 때문이다 (별도 해시 테이블은 메모리와 시간을 더 쓴다). `align − 1` 은 올림으로 건너뛸 수 있는 최대량이고 이 공식이 최소다. 꼬리: "align 이 2의 거듭제곱이 아니면?" → 마스크 올림이 성립하지 않으니 거부(NULL). 표준도 유효한 정렬은 2^k 뿐이다(6.2.8p4). "size 가 크면?" → 덧셈 전에 `size > SIZE_MAX − HDR − (align − 1)` 로 막는다.

</details>

<details>
<summary>Q2. aligned_malloc 이 준 포인터를 실수로 free() 하면 어떻게 되나요? 어떻게 막나요?</summary>

`free` 는 자기가 준 적 없는 주소를 받는다 → UB. 이 맥에서는 SIGTRAP 으로 즉사(exit 133), ASan 은 "attempting free on address which was not malloc()-ed … located 64 bytes inside of 171-byte region" 으로 정확히 짚었다 (demo_wrong_free.c). 최악은 크래시 없는 단순 힙이 엉뚱한 크기를 free list 에 넣어 나중에 다른 곳이 깨지는 경우. 막는 법: 이름으로 짝을 강제(`aligned_malloc/aligned_free`, MSVC `_aligned_malloc/_aligned_free`), 소유 타입을 감싸기(C++ 는 커스텀 deleter 의 `unique_ptr`), 디버그 빌드에서 헤더에 매직 값을 넣어 `aligned_free` 에서 검사, 그리고 가능하면 `free` 와 호환되는 `aligned_alloc`/`posix_memalign` 을 쓴다.

</details>

<details>
<summary>Q3. 그냥 C11 aligned_alloc 을 쓰면 되지 않나요? posix_memalign 과 차이는?</summary>

쓸 수 있으면 쓰는 게 낫다 — 결과를 `free()` 로 해제할 수 있다. 다만 함정: C11 원문은 size 가 alignment 의 배수여야 했고(DR 460 으로 C17 에서 완화), 이 맥 libmalloc 은 여전히 `aligned_alloc(64, 100)` 을 NULL/EINVAL 로 거절하고 `aligned_alloc(4, 16)` 도 거절했다 (실측). `posix_memalign(&p, al, size)` 는 al 이 `sizeof(void*)` 배수인 2^k 여야 하고 에러를 **반환값**으로 준다(errno 아님). 펌웨어 libc 에선 지원 여부부터 다르다. 그리고 둘 다 "4 KB 경계를 넘지 말 것", "마지막 캐시 라인 소유" 같은 조건은 표현 못 한다 — 그래서 DMA 에선 전용 할당기(Linux `dma_pool`, 정적 풀)를 쓴다.

</details>

<details>
<summary>Q4. aligned_realloc 은 어떻게 구현하나요? realloc 을 쓰면 안 되나요?</summary>

`realloc(raw)` 은 블록을 옮길 수 있고, 옮긴 raw 는 다시 16 B 정렬뿐이다. 바이트를 그대로 복사하므로 사용자 영역의 오프셋은 같은데 새 raw + 오프셋이 정렬이라는 보장이 없다. 고치려면 memmove 를 또 해야 하고 헤더의 raw 도 갱신해야 한다. 깔끔한 답은 새 정렬로 할당 → `min(old, new)` 복사 → 옛 블록 해제. 이를 위해 헤더에 **요청 크기** 를 같이 둔다. 실패하면 NULL 과 함께 옛 블록을 그대로 둔다 (`realloc` 계약). `new_size == 0` 의 뜻은 명시한다 — 표준 `realloc(p, 0)` 은 C23 에서 UB 가 됐다.

</details>

<details>
<summary>Q5. DMA 버퍼가 4 KB 경계를 넘으면 왜 문제가 되나요? 넘는지 어떻게 판정하나요?</summary>

장치·버스 규칙 때문이다: AXI 버스트는 4 KB 경계를 넘을 수 없고, MMU 시스템에선 가상으로 연속인 두 페이지가 물리적으로 떨어져 있을 수 있으며, NVMe PRP 엔트리나 많은 DMA 디스크립터는 한 엔트리가 페이지 하나만 가리킨다. 그래서 Linux `dma_pool_create` 에 `boundary` 인자가 있다. 판정은 첫 바이트와 **마지막 바이트**(`addr + len − 1`)의 상위 비트 비교: `((a ^ (a + len − 1)) & ~(B − 1)) != 0`. `len − 1` 을 안 쓰면 경계에 딱 맞게 끝나는 버퍼를 오판하고, `len > B` 를 먼저 처리하지 않으면 감싸는 큰 길이를 놓친다. 할당 쪽에선 넘으면 "다음 경계" 로 옮기고, 그러려면 `2n` 을 잡아야 한다.

</details>

<details>
<summary>Q6. DMA RX 버퍼를 캐시 라인에 정렬했는데도 가끔 옆 변수 값이 사라집니다. 왜죠?</summary>

시작만 정렬하고 **끝** 을 정렬하지 않았기 때문이다. 버퍼 크기가 라인 배수가 아니면 마지막 라인을 옆 변수와 공유한다. RX 완료 후 `SCB_InvalidateDCache_by_Addr` 가 그 라인을 버리면, CPU 가 그 라인에 막 써 둔(dirty) 옆 변수의 새 값도 같이 버려진다. 고침: 버퍼 크기를 라인 배수로 올려 마지막 라인까지 버퍼가 소유하게 하거나(이 레슨의 `n`), 버퍼를 non-cacheable MPU 영역에 둔다. Cortex-M7 라인은 32 B, Cortex-A 는 보통 64 B 이므로 공용 코드는 큰 값에 맞춘다. 상세 재현은 ARCH-M12.

</details>

<details>
<summary>Q7. 힙을 쓰지 않는 펌웨어에서는 DMA 버퍼를 어떻게 준비하나요?</summary>

정적 배열을 `_Alignas` 로 정렬하고 전용 섹션에 둔다. 실측: `_Alignas(4096)` → `.p2align 12`. 섹션 이름을 `.bss.dma_*` 로 지으면 `%nobits` 라 이미지 크기가 0, 그냥 `.dma_buf` 면 `%progbits` 라 0 으로 채운 바이트가 이미지에 실린다 (링커 스크립트에서 `NOLOAD` 필요). 링커 스크립트가 그 섹션을 MPU 로 non-cacheable 지정한 RAM(STM32H7 이면 DMA 가 닿는 SRAM 영역)에 놓는다. 부팅 때 아레나에서 RX 링 버퍼·디스크립터를 잘라 쓰고(해제 없음), 런타임에 버퍼를 돌려 써야 하면 고정 크기 풀(O(1), 연습 2). 꼬리: "MPU 영역 크기 제약은?" → ARMv7-M 은 2^k 크기(≥ 32 B)이고 시작이 크기의 배수, 256 B 이상이면 8개 서브영역을 끌 수 있다. ARMv8-M(M33)은 base/limit 방식에 32 B 단위라 이 제약이 없다.

</details>

<details>
<summary>Q8. 헤더 24 바이트가 아깝습니다. 오버헤드를 줄이려면?</summary>

① **1바이트 오프셋**: `raw + 1` 부터 올려서 `p[-1]` 에 오프셋(1..align)을 저장한다. align ≤ 128 이면 `uint8_t` 에 들어가고 요청은 `size + align` (100 B/64 정렬: 187 → 164, 연습 1). ② "align ≤ malloc 기본 정렬이면 그냥 malloc" 은 **안 된다** — `aligned_free` 가 헤더가 있는지 없는지 구별할 수 없다 (항상 헤더를 두거나, 오프셋 바이트처럼 구별 가능한 표식을 둬야 한다). ③ 많은 작은 버퍼면 개별 할당 대신 풀/아레나로 묶는 게 훨씬 이득이다. 트레이드오프: 크기를 저장하지 않으면 `aligned_realloc` 을 못 만든다.

</details>

<details>
<summary>Q9. 이 할당기를 어떻게 테스트했나요? malloc 주소는 매번 다를 텐데요.</summary>

① 시스템 할당기를 계측 래퍼로 감싸 호출 수·요청 크기·살아 있는 raw 블록을 기록하고, 모르는 포인터가 free 로 오면 세기만 한다 (잘못된 구현도 크래시 없이 FAIL). ② 고정 시드 PRNG 로 정렬 2^3..2^12 × 크기를 수천 번, 각 블록에 무늬를 채워 침범 여부를 본다. ③ 경계값: `SIZE_MAX` 바로 아래/위, align 0/3/24, 실패 주입. ④ 독립 오라클(다른 공식의 DMA 엔진 검사). ⑤ 주소 의존 경로("다음 경계로 옮기기")는 랜덤에서 최소 1회 일어났는지 세고, 결정적으로는 정렬된 정적 아레나에서 정확한 오프셋으로 검증. ⑥ 같은 테스트를 ASan/UBSan 으로 다시 돌린다. 꼬리: "펌웨어 타깃에선?" → 할당기는 순수 C 라 호스트에서 테스트하고, 타깃에선 버퍼 주소를 DMA 레지스터에 쓰기 전에 `assert(!crosses_boundary(...))` 를 남긴다.

</details>

## 9. 추가 연습

### 연습 1 — 1바이트 오프셋 헤더

align ≤ 128 만 지원하는 대신 오버헤드를 `align` 바이트로 줄인 `small_aligned_malloc` / `small_aligned_free` 를 작성하라. `(100, 64)` 의 요청 크기는?

<details>
<summary>답 보기</summary>

`off = align_up(raw + 1, a) − raw` 는 1..a 이므로 최소 1 바이트가 앞에 남는다. `p[-1] = off`, 해제는 `free(p − p[-1])`. 요청은 `size + a` = 164 (Part A 판 187). a = 256 이면 off 가 256 이 될 수 있어 `uint8_t` 에 안 들어가므로 거부한다. 바이트 하나라 정렬 문제가 없다는 것도 장점. 대신 크기를 모르니 realloc 은 못 만든다.

</details>

### 연습 2 — dma_pool: 경계를 넘지 않는 고정 크기 풀

16 KB 정렬 아레나를 `blk` 크기(정렬 `align` 으로 올린 stride) 블록으로 자르되 어떤 블록도 4 KB 경계를 넘지 않게 하고, 빈 블록 안에 다음 포인터를 두는 free list 로 O(1) `pool_alloc` / `pool_free` 를 만들어라. 1536 B, 96 B, 100 B(정렬 32) 각각 몇 개가 나오나?

<details>
<summary>답 보기</summary>

1536 B → 페이지당 2개 × 4 = **8**. 96 B → 페이지당 ⌊4096/96⌋ = 42개(4032 B), 43번째는 넘으므로 다음 페이지로 → **168**. 100 B → stride 128 → 페이지당 32개 → **128**. 블록 42 번째는 `4096` 에서 시작한다 (테스트). 빈 블록 안에 `void *` 를 저장할 때 아레나가 `uint8_t` 로 **선언된** 배열이라 `*(void **)b = …` 는 유효 타입 규칙(6.5p7) 위반이다 — `memcpy` 로 읽고 쓴다 (C-M13). Linux `dma_pool` 과 DS-M02 메모리 풀의 DMA 판이다.

</details>

### 연습 3 — 두 버퍼가 캐시 라인을 공유하나

`shares_cache_line(a, alen, b, blen, line)` 을 작성하고, 4바이트 정렬로 붙여 놓은 두 1500 B RX 버퍼(오프셋 0, 1500)와 라인 단위로 올려 놓은 경우(0, 1504)를 판정하라.

<details>
<summary>답 보기</summary>

각 버퍼가 걸친 라인 번호 구간 `[a/line, (a+alen−1)/line]` 과 `[b/line, (b+blen−1)/line]` 이 겹치면 공유. (0, 1500) 과 (1500, 1500) 은 라인 46(1472..1503)을 공유 → true. 1504 = 47 × 32 에서 시작하면 false. 길이 0 은 아무 라인도 차지하지 않는다. 이 판정이 true 인 두 DMA 버퍼는 한쪽의 invalidate 가 다른 쪽의 데이터를 망칠 수 있다.

</details>

### 연습 4 — ARMv7-M MPU 영역 계산

`[addr, addr+len)` 을 덮는 가장 작은 ARMv7-M MPU 영역(크기 2^k, 32 B..4 GB, 시작은 크기의 배수)을 구하라. `0x20000FF0` 의 32 B 버퍼에는 얼마나 큰 영역이 필요한가?

<details>
<summary>답 보기</summary>

크기 32 부터 두 배씩 키우며 `base = addr & ~(size − 1)` 이 `base + size ≥ addr + len` 을 만족하는 첫 크기. 끝 계산은 64비트로 해서 4 GB 근처에서 넘치지 않게 한다. `0x20000FF0..0x2000100F` 는 0x1000 경계를 가로지르므로 4 KB 영역으로는 못 덮고 **8 KB** (`0x20000000`)가 필요하다 — 32 B 버퍼에 8 KB 영역. 그래서 DMA 버퍼는 MPU 영역 크기로 정렬된 아레나에 모아 둔다. (ARMv8-M MPU 는 base/limit 방식이라 이 문제가 없다.)

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 21/21 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 21/21, 보고 없음
```

## 10. 핵심 정리

- `aligned_malloc` = `malloc(size + HDR + a − 1)` → `p = align_up(raw + HDR, a)` → `p` 앞에 `{raw, size}` 헤더. 이 요청 크기가 최소이자 충분하다.
- 결과 포인터는 정수에서 만들지 말고 `(unsigned char *)raw + off`. 마스크는 `~(uintptr_t)(a − 1)` — 먼저 넓히고 뒤집는다.
- 입력 검증: align 은 2^k, 오버플로는 덧셈 전에 `size > SIZE_MAX − HDR − (a − 1)`, 실패·NULL 계약을 테스트로 고정.
- 짝 함수로만 해제 (`free(p)` → 이 맥 SIGTRAP, ASan bad-free). 표준 `aligned_alloc` 은 `free` 와 호환되지만 이 맥은 size 가 align 배수가 아니면 NULL.
- `aligned_realloc` 은 realloc 으로 못 만든다 (오프셋이 그대로 복사됨): 새 할당 + `min` 복사 + 해제, 실패 시 옛 블록 유지.
- DMA 버퍼 3조건: 시작 정렬, `((a ^ (a+len−1)) & ~(B−1)) == 0` (4 KB 안), **끝까지** 캐시 라인 소유. 넘으면 다음 경계로 → `HDR + a − 1 + 2n` 필요.
- 펌웨어는 힙 대신 `_Alignas` 정적 아레나(`.bss.dma*`, MPU non-cacheable) + 고정 크기 풀. MPU(ARMv7-M) 영역은 2^k 크기·크기 정렬.

## 참고자료

- C11 (N1570): 6.2.8 정렬(p4 유효한 정렬은 2의 거듭제곱), 6.3.2.3p5–6 정수↔포인터 변환, 6.5p6–7 유효 타입, 7.20.1.4 `uintptr_t`, 7.22.3p1 메모리 관리 함수 공통(기본 정렬, 크기 0), 7.22.3.1 `aligned_alloc` — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- WG14 DR 460 (`aligned_alloc` 의 size 요구 완화) — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/summary.htm#dr_460>
- POSIX.1-2017 `posix_memalign` — <https://pubs.opengroup.org/onlinepubs/9699919799/functions/posix_memalign.html>
- Linux kernel — DMA API (`dma_pool_create` 의 `boundary`) — <https://docs.kernel.org/core-api/dma-api.html>
- Arm AMBA AXI Protocol Specification (버스트는 4 KB 경계를 넘지 않는다) — <https://developer.arm.com/documentation/ihi0022/latest/>
- Arm Cortex-M7 — CMSIS-Core 캐시 함수 `SCB_InvalidateDCache_by_Addr` — <https://arm-software.github.io/CMSIS_5/Core/html/group__Dcache__functions__m7.html>
- ARMv7-M Architecture Reference Manual (ARM DDI0403E) B3.5 MPU — <https://developer.arm.com/documentation/ddi0403/latest/>
- NVM Express Base Specification — PRP 엔트리와 PRP 리스트 — <https://nvmexpress.org/specifications/>
- 관련 레슨: C-B05 (`align_up` 관용구), C-B12 (`_Alignas`, 구조체 정렬), C-M04 (비정렬 접근), C-M13 (유효 타입/엄격한 앨리어싱), C-M14 · C-A04 (ISR 에서 malloc 금지), ARCH-M12 (DMA 캐시 clean/invalidate 버그 재현), COMM-A08 (디스크립터 링 드라이버), DS-M02 (고정 크기 풀), DS-A06 (TLSF), TOOL-A07 (링커 스크립트의 4 KB 정렬 DMA 섹션), SOCX-B02 (AXI 4 KB 경계), CPP-B14 (힙 금지)
