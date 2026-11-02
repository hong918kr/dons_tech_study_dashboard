# N05D · 버퍼 · 문자열 · 메모리
<!-- theme: pastel -->

> **묶음**: D (버퍼·문자열·메모리) · **문제 수**: 8 (D1–D8) · **목표**: 포인터+길이 버퍼를 다루는 함수를 종이에 20분 안에 — 경계(0·끝·겹침·오버플로)를 먼저 말하고 쓰기 · **선행**: [N05 목록](2026-11-01_N05_c_paper_drill_bank.html) · [C 정리 C1–C3·C10](2026-10-29_c_basics_cram.html) · **이전/다음**: [N05C 레지스터·MMIO](2026-11-01_N05C_register_mmio_driver.html) / [N05E 링버퍼·큐·ISR](2026-11-01_N05E_ring_queue_isr.html) · **코드**: onsitePrep/n05/d/ (`./run.sh` → D1–D8 PASS, ASan+UBSan)

## 0. TL;DR · 사용법

- 이 페이지가 훈련하는 것: **포인터 + 길이(n)** 로 들어오는 버퍼를 안전하게 다루기 — 겹침 방향(D1), 비교·BER(D2), printf 없는 출력(D3), 문자열 → 숫자 + 오버플로(D4), in-place 배열 조작(D5·D7), 블록 할당기(D6), 스트리밍 평균(D8).
- 이미 한 것과 안 겹치게: strcpy·strlcpy·strlen·strcmp·기본 memmove는 [C 정리 C1](2026-10-29_c_basics_cram.html), revstr·atoi·itoa·자릿수 방식 hex 파싱은 C2·C3, aligned_malloc은 C10 — 여기선 **변형·확장**만.
- 외울 한 줄 3개:
  - memmove: **"dst below src → copy forward; otherwise copy backward."**
  - hex 오버플로: **`if (v > (UINT32_MAX >> 4)) overflow;`** — 시프트 **전에** 검사.
  - 회전: **reverse(0,k) · reverse(k,n) · reverse(0,n)** = 왼쪽 k칸, O(1) 공간.
- ★★★ (온사이트 확률 높음): **D1** memmove · **D4** hex 파싱. ★★: D2·D3·D5·D6·D7, ★: D8.
- 사용법: §1 문제지만 출력 → 타이머 20분 → 종이에 풀기 → 문제별 `<details>`로 채점 (힌트 1 → 힌트 2 → 모범 답 → trace → 테스트 → 꼬리 질문).
- 공통 함정: `size_t` 역방향 루프 underflow, n==0, 마지막 줄/마지막 토큰, 에러 시 `*out` 건드리지 않기.

## 1. 문제지 (인쇄용 — 답 없음)

### D1 · memmove
Implement an overlap-safe `memmove`. Then speed it up with 32-bit word copies when it is safe to do so.

```c
void *my_memmove(void *dst, const void *src, size_t n);
```

Example: `char m[] = "123456789"; my_memmove(m + 2, m, 5);` → `m == "121234589"`.

### D2 · Buffer compare + bit error count
Compare an expected buffer with a received one. Report the first mismatching offset, the number of mismatching bytes and the total number of flipped bits. Return true if identical.

```c
typedef struct { size_t first_mismatch; size_t byte_errors; uint64_t bit_errors; } cmp_result_t;
bool buf_compare(const uint8_t *exp, const uint8_t *got, size_t n, cmp_result_t *r);
```

Example: exp `{00 11 22 33 44 55 66 77}`, got `{00 11 23 33 44 AA 66 77}` → first=2, bytes=2, bits=9.

### D3 · hexdump without printf
Print a buffer as hex lines through a `putc`-style callback (no printf/snprintf). 16 bytes per line: 8-digit offset, `": "`, each byte as two hex digits + space, one extra space, then ASCII (non-printable → `.`). Keep the ASCII column aligned on a short last line.

```c
typedef void (*putc_fn)(char c);
void hexdump(const uint8_t *buf, size_t n, uint32_t base, putc_fn out);
```

Example: 20 bytes `"Hello, Rivian!\n\0"` + `01 7f 80 ff`, base 0x40000000 →

```text
40000000: 48 65 6c 6c 6f 2c 20 52 69 76 69 61 6e 21 0a 00  Hello, Rivian!..
40000010: 01 7f 80 ff                                      ....
```

### D4 · Parse hex string → uint32
Parse `"0x1A2b"` or `"deadbeef"` into a `uint32_t`. Optional `0x`/`0X` prefix, case-insensitive digits, the whole string must be consumed. Detect empty input, bad characters and overflow. Don't touch `*out` on error.

```c
typedef enum { HEX_OK = 0, HEX_ERR_NULL, HEX_ERR_EMPTY, HEX_ERR_CHAR, HEX_ERR_OVERFLOW } hex_err_t;
hex_err_t hex_to_u32(const char *s, uint32_t *out);
```

Example: `"0x1A2b"` → OK, 0x1A2B · `"0x100000000"` → OVERFLOW · `"0x"` → EMPTY.

### D5 · Rotate array + in-place dedup
(a) Rotate an array left by k in place with O(1) extra space. (b) Remove duplicates from a sorted array in place; return the new length.

```c
void   rotate_left(uint32_t *a, size_t n, size_t k);
size_t dedup_sorted(uint32_t *a, size_t n);
```

Example: `[1 2 3 4 5]`, k=2 → `[3 4 5 1 2]` · `[1 1 2 3 3 3 7]` → length 4, `[1 2 3 7]`.

### D6 · Fixed-size block pool allocator
Given a static memory region, implement a fixed-block allocator with O(1) alloc/free. No extra metadata array — keep the free list inside the free blocks. `pool_free` must reject pointers that aren't block starts of this pool.

```c
typedef struct { void *free_head; uint8_t *base; size_t block_size, nblocks, nfree; } pool_t;
bool  pool_init(pool_t *p, void *mem, size_t mem_size, size_t block_size);
void *pool_alloc(pool_t *p);
bool  pool_free(pool_t *p, void *blk);
```

Example: 256-byte region, block_size 20 → rounded to 24 (LP64), 10 blocks; 11th alloc → NULL.

### D7 · In-place command-line tokenizer
Split a debug-shell line into argc/argv in place (no malloc, no strtok). Separators: space, tab, CR, LF; runs of separators count as one. Return argc, or -1 if more than `max_args` tokens.

```c
int tokenize(char *line, char *argv[], int max_args);
```

Example: `"wr 0x40000000 0x55"` → 3, argv = {"wr", "0x40000000", "0x55"}.

### D8 · Moving average
Moving average over the last N samples of 16-bit ADC data, O(1) per sample, integer only, rounded. Before N samples have arrived, average over the samples seen so far.

```c
typedef struct { uint16_t buf[64]; uint32_t sum; uint8_t window, idx, count; } movavg_t;
bool     ma_init(movavg_t *m, unsigned window);   /* 1..64 */
uint16_t ma_update(movavg_t *m, uint16_t x);
```

Example: N=4, inputs 10 20 30 40 50 0 → outputs 10 15 20 25 35 30.

## D1 · memmove (겹침 방향) + 워드 단위 최적화 (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 겹쳐도 안전한 메모리 복사를 만들고, 32-bit 워드로 빠르게 하는 경우를 붙인다.
- **상황**: 펌웨어에서 버퍼 안의 데이터를 앞/뒤로 밀 때(패킷 헤더 제거, 로그 버퍼 compaction, 링버퍼 정리) src와 dst가 같은 버퍼 안에서 겹친다. memcpy는 겹침을 가정하지 않아서 결과가 깨진다. 기본 바이트 버전은 [C 정리 C1](2026-10-29_c_basics_cram.html)에 있고, 여기선 **방향 판단을 말로 설명 + 워드 경로** 추가가 포인트.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `my_memmove` | src의 n바이트를 dst로 복사, dst 반환 | 겹쳐도 결과는 "src를 먼저 임시 버퍼에 복사한 것"과 같아야 함 |
| (워드 경로) | 정렬이 맞으면 4바이트씩 | 정렬 안 맞으면 바이트로 — fault/UB 없이 |

- **예시를 말로 풀면**: "123456789"에서 앞 5글자 "12345"를 2칸 뒤로 옮기면 → "12" + "12345" + "89" = "121234589".
- **지켜야 할 조건**: n==0, dst==src, 앞쪽 겹침·뒤쪽 겹침 모두 정답. 표준 라이브러리 호출 금지.

### 1. Problem (English)

"Implement memmove. It must work when the source and destination overlap in either direction. Once it's correct, tell me how you'd make it faster, and add a word-copy path."

```c
void *my_memmove(void *dst, const void *src, size_t n);
```

- `m = "123456789"`, `my_memmove(m + 2, m, 5)` → `"121234589"` (dst above src → must copy backward)
- `m = "123456789"`, `my_memmove(m, m + 2, 5)` → `"345676789"` (dst below src → forward is fine)
- `n = 0` or `dst == src` → no change, returns dst

### 2. 먼저 물어볼 것 (clarify)

- "Can I assume normal RAM, not device memory?" → Yes (MMIO needs `memcpy_toio`-style fixed-width accesses).
- "Is the target OK with unaligned word access?" → Assume no (like Cortex-M0) — use words only when both pointers are aligned.
- "Should I return dst like the standard?" → Yes.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (RAM? 정렬?) |
| 2–5 | 예시를 손으로: 앞→뒤로 하면 "121212189"로 깨지는 것 보여주기 |
| 5–15 | 바이트 버전 → 워드 경로 추가 |
| 15–18 | trace (뒤→앞 5스텝) |
| 18–20 | 테스트 · 확장(misaligned head, DMA) |

- Say out loud: **"If dst is below src, copying front to back never overwrites a source byte I still need. If dst is above src, I copy back to front for the same reason."**
- Say out loud: **"Comparing pointers into different objects is undefined in C, so I compare them as uintptr_t."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

겹침이 문제되는 건 dst가 src **뒤쪽**에 있을 때뿐이다. 앞→뒤로 복사하면 아직 안 읽은 src 바이트를 dst 쓰기가 먼저 덮는다. 그럼 어느 쪽 끝에서 시작해야 할까?

</details>

<details><summary>힌트 2</summary>

뒤→앞: `d += n; s += n; while (n--) *--d = *--s;`. 워드 경로는 `((uintptr_t)d | (uintptr_t)s) & 3u` 가 0일 때만, 남은 n>=4 동안 `uint32_t` 단위로, 꼬리는 바이트로.

</details>

<details><summary>모범 답 (C)</summary>

```c
/* Overlap-safe copy. Direction: dst below src -> copy front to back,
 * otherwise back to front. Word path only when both pointers are 4-aligned. */
void *my_memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0)
        return dst;
    if ((uintptr_t)d < (uintptr_t)s) {                 /* forward */
        if ((((uintptr_t)d | (uintptr_t)s) & 3u) == 0) {
            for (; n >= 4; n -= 4, d += 4, s += 4)
                *(uint32_t *)d = *(const uint32_t *)s;
        }
        while (n--)
            *d++ = *s++;
    } else {                                           /* backward */
        d += n;
        s += n;
        if ((((uintptr_t)d | (uintptr_t)s) & 3u) == 0) {
            for (; n >= 4; n -= 4) {
                d -= 4;
                s -= 4;
                *(uint32_t *)d = *(const uint32_t *)s;
            }
        }
        while (n--)
            *--d = *--s;
    }
    return dst;
}
```

- 시간 O(n) (워드 경로는 루프 횟수 ~n/4), 공간 O(1).
- 테스트는 버퍼를 `uint32_t` 배열로 선언 → 워드 접근이 strict aliasing 위반이 아님. 일반 `char` 버퍼에 쓰려면 `__attribute__((may_alias))` 타입이나 `-fno-strict-aliasing` (Linux 커널 빌드 방식)이 필요.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`m = "123456789"`, `my_memmove(m+2, m, 5)` — d > s → backward (char 배열 정렬 미보장 → 바이트 경로라고 가정).

| n (복사 전) | 쓰기 | 버퍼 |
|---|---|---|
| 5 | m[6] = m[4] = '5' | 1 2 3 4 5 6 **5** 8 9 |
| 4 | m[5] = m[3] = '4' | 1 2 3 4 5 **4** 5 8 9 |
| 3 | m[4] = m[2] = '3' | 1 2 3 4 **3** 4 5 8 9 |
| 2 | m[3] = m[1] = '2' | 1 2 3 **2** 3 4 5 8 9 |
| 1 | m[2] = m[0] = '1' | 1 2 **1** 2 3 4 5 8 9 → "121234589" |

- 비교: 같은 호출을 앞→뒤로 하면 m[4]=m[2]가 이미 덮인 '1'을 읽어서 "121212189" — 이걸 2–5분 구간에 보여주면 방향 설명이 끝난다.

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상 겹침 양방향: `(m+2, m, 5)` → "121234589", `(m, m+2, 5)` → "345676789".
- n = 0, dst == src → 변화 없음, dst 반환.
- 안 겹치는 두 버퍼 → memcpy와 같은 결과.
- 정렬 조합: (d, s) offset 0..19 × 모든 길이 전수 → libc memmove와 바이트 비교 (`run.sh`에서 실제로 함).
- 워드 경로 경계: n = 3, 4, 5 (워드 0개/1개 + 꼬리).

</details>

### 4. 꼬리 질문

Q1. "Your word path only triggers when both pointers are aligned. What if `dst` is at offset 1 and `src` at offset 1?"

<details><summary>답</summary>

If both have the same misalignment, `(d ^ s) & 3 == 0`, I can copy 1–3 head bytes to reach alignment, then do words, then the tail. If the relative alignment differs, a plain word loop can't make both sides aligned, so I'd either stay with bytes or do aligned loads with shift-and-merge, which is what optimized libc versions do. In practice I'd use the compiler builtin or a DMA engine for large copies.

- 한국어 핵심: 상대 정렬이 같으면 head 바이트로 맞추고 워드, 다르면 바이트 or shift-merge.

</details>

Q2. "Can I use memmove to copy into a peripheral FIFO register block?"

<details><summary>답</summary>

No. memmove may use any access width and order, may read the destination, and the compiler may merge or split accesses — device registers need exact-width, in-order, volatile accesses. I'd write an explicit loop over a `volatile uint32_t *`, like Linux's `memcpy_toio` / `iowrite32_rep`.

- 한국어 핵심: MMIO는 폭·순서·횟수가 의미 있음 → volatile 고정 폭 루프.

</details>

Q3. "Why does Cortex-M0 care about your word path?"

<details><summary>답</summary>

ARMv6-M doesn't support unaligned word loads and stores — they fault. Cortex-M3/M4 allow unaligned LDR/STR (slower), but not LDM/STM or LDRD. That's why I check alignment instead of just casting.

- 한국어 핵심: M0 unaligned → HardFault, M3/M4는 LDR/STR만 허용.

</details>

### 5. 채점 · 자주 틀리는 것

- 방향 조건을 반대로 씀 (`d < s` 일 때 앞→뒤가 맞다).
- 서로 다른 객체의 포인터를 `<` 로 직접 비교 → `uintptr_t` 캐스트.
- 뒤→앞에서 `*--d = *--s` 대신 `d[n-1]` 인덱스로 하다 `n` underflow.
- 워드 경로 후 꼬리 바이트 처리 빠뜨림 (n=5에서 마지막 1바이트).
- 원래 `dst` 대신 증가된 `d` 반환.

## D2 · 두 버퍼 비교 · 첫 불일치 · 비트 에러 수 (★★ · 15분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 기대값 버퍼와 받은 버퍼를 비교해서 "어디서 처음 틀렸나 · 몇 바이트 틀렸나 · 몇 비트 뒤집혔나"를 낸다.
- **상황**: 링크(PCIe/SerDes/SPI) loopback 테스트, DMA 결과 검증, 메모리 테스트에서 "PASS/FAIL"만 보면 디버그가 안 된다. **BER = bit_errors / (8·n)** 이 신호 품질 지표, **첫 불일치 offset**은 주소 라인/타이밍 문제 단서.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `buf_compare` | n바이트 비교, 결과 struct 채움 | 같으면 true, first_mismatch = n |
| `popcount8` (헬퍼) | 1인 비트 수 | builtin 없이 |

- **예시를 말로 풀면**: 2번 바이트 0x22 → 0x23 은 1비트, 5번 바이트 0x55 → 0xAA 는 8비트 전부 → 2바이트·9비트 에러, 첫 불일치 2.
- **지켜야 할 조건**: n = 0, 전부 같음, 첫/마지막 바이트 불일치, 큰 버퍼에서 카운터 오버플로 없음.

### 1. Problem (English)

"You received a buffer back from a loopback test. Compare it with the expected pattern and report the first mismatching offset, how many bytes differ and how many bits flipped in total."

```c
typedef struct { size_t first_mismatch; size_t byte_errors; uint64_t bit_errors; } cmp_result_t;
bool buf_compare(const uint8_t *exp, const uint8_t *got, size_t n, cmp_result_t *r);
```

- exp `{00 11 22 33 44 55 66 77}`, got `{00 11 23 33 44 AA 66 77}` → false, first=2, bytes=2, bits=9
- identical → true, first=n, 0, 0
- got = ~exp (8 bytes) → first=0, bytes=8, bits=64

### 2. 먼저 물어볼 것 (clarify)

- "What should first_mismatch be when they match?" → n (one past the end), or SIZE_MAX — say which.
- "How big can n be?" → Up to GBs of DRAM, so bit_errors is 64-bit.
- "Do you also want the list of failing offsets?" → Not now (follow-up).

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (match 시 first 값, n 크기) |
| 2–5 | 예시 XOR 계산: 0x22^0x23=0x01, 0x55^0xAA=0xFF |
| 5–12 | popcount8 + 루프 |
| 12–15 | trace + 테스트 |

- Say out loud: **"XOR gives me exactly the flipped bits, and popcount of the XOR is the bit error count — summing that over the buffer gives BER."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

한 바이트에서 "다른 비트"만 1로 만드는 연산은? 그 결과의 1 개수를 세면 된다.

</details>

<details><summary>힌트 2</summary>

`diff = exp[i] ^ got[i]`; popcount는 `while (x) { x &= x - 1; c++; }` (1 비트 개수만큼만 돈다). 첫 불일치는 `byte_errors == 0`일 때만 기록.

</details>

<details><summary>모범 답 (C)</summary>

```c
typedef struct {
    size_t   first_mismatch;   /* == n when buffers are equal */
    size_t   byte_errors;      /* bytes that differ */
    uint64_t bit_errors;       /* total flipped bits = sum popcount(a^b) */
} cmp_result_t;

static unsigned popcount8(uint8_t x)
{
    unsigned c = 0;
    while (x) {
        x &= (uint8_t)(x - 1);  /* clear lowest set bit */
        c++;
    }
    return c;
}

/* Returns true when the buffers are identical. */
bool buf_compare(const uint8_t *exp, const uint8_t *got, size_t n,
                 cmp_result_t *r)
{
    r->first_mismatch = n;
    r->byte_errors = 0;
    r->bit_errors = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t diff = exp[i] ^ got[i];
        if (diff == 0)
            continue;
        if (r->byte_errors == 0)
            r->first_mismatch = i;
        r->byte_errors++;
        r->bit_errors += popcount8(diff);
    }
    return r->byte_errors == 0;
}
```

- 시간 O(n·k) (k = 바이트당 뒤집힌 비트 수 ≤ 8), 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

| i | exp | got | diff | byte_errors | bit_errors | first |
|---|---|---|---|---|---|---|
| 0–1 | 00, 11 | 00, 11 | 0 | 0 | 0 | 8 (초기값 n) |
| 2 | 22 | 23 | 01 | 1 | 1 | 2 |
| 3–4 | 33, 44 | 33, 44 | 0 | 1 | 1 | 2 |
| 5 | 55 | AA | FF | 2 | 9 | 2 |
| 6–7 | 66, 77 | 66, 77 | 0 | 2 | 9 | 2 |

- popcount8(0xFF): FF→FE→FC→F8→F0→E0→C0→80→00 = 8회.

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 예시 → 2 / 2 / 9.
- 빈 입력 n = 0 → true, 0 / 0 / 0.
- 전부 같음 → true, first = n.
- 경계: 첫 바이트와 마지막 바이트만 틀림 → first = 0.
- 전부 반전 → bits = 8n (최댓값).
- popcount8을 0..255 전수로 builtin과 비교 (`run.sh`).

</details>

### 4. 꼬리 질문

Q1. "This is too slow for a 4 GB DRAM compare. Speed it up."

<details><summary>답</summary>

Compare 64-bit words: XOR two `uint64_t`s and skip the word entirely when the XOR is zero, which is the common case. Only for non-zero words do I popcount, ideally with `__builtin_popcountll`, which maps to a hardware instruction (on ARMv8 it's CNT on a SIMD register). Head and tail bytes are handled separately if the buffers aren't 8-byte aligned.

- 한국어 핵심: 64-bit XOR로 0인 워드 건너뛰기, 0 아닌 것만 popcount.

</details>

Q2. "How would you turn this into a BER number and decide pass/fail?"

<details><summary>답</summary>

BER is bit_errors divided by bits compared, 8·n. With zero errors you can't claim BER is zero — you can only claim an upper bound at a confidence level; the rule of thumb is about 3/N bits for 95% confidence with zero errors. So to claim 1e-12 you need roughly 3e12 error-free bits.

- 한국어 핵심: 에러 0이면 "BER < 3/N (95%)" — 1e-12 주장엔 ~3e12 비트 필요.

</details>

Q3. "The first mismatch is always at offset 0x1000 and errors repeat every 4 KB. What does that tell you?"

<details><summary>답</summary>

A periodic pattern aligned to a power of two smells like an address problem — a stuck or shorted address line, or a page/descriptor boundary bug in the DMA — rather than random signal-integrity noise, which would give scattered single-bit errors. I'd log the XOR pattern per failing address and look at which address bit and which data bit are involved.

- 한국어 핵심: 2의 거듭제곱 주기 = 주소 라인/DMA 경계 의심, 랜덤 1비트 = SI 의심.

</details>

### 5. 채점 · 자주 틀리는 것

- first_mismatch를 매번 덮어써서 "마지막" 불일치가 됨.
- `x &= x - 1` 에서 `uint8_t` 승격 경고 → `(uint8_t)(x - 1)`.
- bit_errors를 `uint32_t`로 → 512 MB 넘게 전부 틀리면 오버플로.
- match일 때 first 값을 정의 안 함 (clarify에서 정하기).

## D3 · hexdump 포맷터 (snprintf 없이) (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 버퍼를 "주소: 16바이트 hex  ASCII" 줄로 찍는다. printf 없이 한 글자씩 `putc`로.
- **상황**: bring-up 초기엔 printf(libc, 힙, 스택 수 KB)가 없거나 너무 무겁다. UART `putc` 하나로 메모리·패킷·레지스터 블록을 덤프하는 게 디버그 1번 도구. 니블→문자 변환은 [C 정리 C3 itoa](2026-10-29_c_basics_cram.html)와 같은 패턴.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `put_hex` (헬퍼) | v를 digits자리 hex로 | 상위 니블부터, 소문자 |
| `hexdump` | 16바이트씩 줄 출력 | 마지막 짧은 줄은 hex 칸을 공백으로 채워 ASCII 열 정렬 |

- **예시를 말로 풀면**: 20바이트면 2줄. 첫 줄은 16바이트 꽉, 둘째 줄은 4바이트 + 빈 12칸(칸당 공백 3개) + ASCII 4글자.
- **지켜야 할 조건**: n = 0이면 아무것도 안 찍음, 정확히 16바이트면 1줄, 0x20–0x7E만 그대로.

### 1. Problem (English)

"We only have a UART putc. Write a hexdump: 16 bytes per line, 8-digit hex offset, colon, the bytes in hex, then the printable ASCII. Keep the ASCII column aligned on the last line."

```c
typedef void (*putc_fn)(char c);
void hexdump(const uint8_t *buf, size_t n, uint32_t base, putc_fn out);
```

- 20 bytes at base 0x40000000 → 2 lines (see §1)
- n = 1, `"A"`, base 0x10 → `00000010: 41 ` + 15 empty 3-space slots + one space + `A` + newline
- n = 0 → no output

### 2. 먼저 물어볼 것 (clarify)

- "Is the offset the absolute address or relative to buf?" → Absolute: `base + line`.
- "Uppercase or lowercase hex?" → Lowercase, like `xxd`.
- "Output to a callback or into a buffer?" → Callback (UART putc).

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (offset 의미, 대소문자) |
| 2–5 | 한 줄 레이아웃을 칸 수로 적기: 8 + 2 + 16×3 + 1 + cnt + 1 |
| 5–15 | put_hex → 바깥 루프(줄) → hex 칸 → ASCII |
| 15–18 | 마지막 줄 trace (cnt=4) |
| 18–20 | 테스트 |

- Say out loud: **"Each hex digit is just `hex[(v >> shift) & 0xF]`, starting from the top nibble — no printf needed."**
- Say out loud: **"For a short last line I still emit 16 slots, blanks for missing bytes, so the ASCII column lines up."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

"0123456789abcdef" 문자열 테이블을 니블 값으로 인덱싱한다. 줄 루프는 `line += 16`, 이번 줄 바이트 수는 `min(16, n - line)`.

</details>

<details><summary>힌트 2</summary>

hex 칸 루프는 항상 i = 0..15를 돌고, `i < cnt` 이면 "hh ", 아니면 "   ". ASCII 루프는 i < cnt까지만. `(c >= 0x20 && c <= 0x7E) ? c : '.'`.

</details>

<details><summary>모범 답 (C)</summary>

```c
typedef void (*putc_fn)(char c);

static void put_hex(putc_fn out, uint32_t v, int digits)
{
    static const char hex[] = "0123456789abcdef";
    for (int sh = (digits - 1) * 4; sh >= 0; sh -= 4)
        out(hex[(v >> sh) & 0xFu]);
}

/* Line: "OOOOOOOO: hh hh .. hh  ASCII\n", 16 bytes per line,
 * short last line padded so the ASCII column stays aligned. */
void hexdump(const uint8_t *buf, size_t n, uint32_t base, putc_fn out)
{
    for (size_t line = 0; line < n; line += 16) {
        size_t cnt = (n - line < 16) ? n - line : 16;
        put_hex(out, base + (uint32_t)line, 8);
        out(':');
        out(' ');
        for (size_t i = 0; i < 16; i++) {
            if (i < cnt) {
                put_hex(out, buf[line + i], 2);
                out(' ');
            } else {
                out(' '); out(' '); out(' ');      /* pad missing byte */
            }
        }
        out(' ');
        for (size_t i = 0; i < cnt; i++) {
            uint8_t c = buf[line + i];
            out((c >= 0x20 && c <= 0x7E) ? (char)c : '.');
        }
        out('\n');
    }
}
```

- 시간 O(n), 공간 O(1). 한 줄 = `8 + 2 + 48 + 1 + cnt` 글자 + 줄바꿈 (꽉 찬 줄 75글자 + `\n`).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

예시 20바이트, base 0x40000000.

| line | cnt | offset 출력 | hex 칸 | ASCII |
|---|---|---|---|---|
| 0 | 16 | `40000000` | 48 65 … 0a 00 (16칸) | `Hello, Rivian!..` (0x0a, 0x00 → '.') |
| 16 | 4 | `40000010` | 01 7f 80 ff + 공백 12칸×3 = 36칸 | `....` (0x01, 0x7f, 0x80, 0xff 모두 비인쇄) |
| 32 | — | 루프 종료 (32 ≥ 20) | | |

- put_hex(0x7f, 2): sh=4 → '7', sh=0 → 'f'.

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 20바이트 → 2줄, 문자열 그대로 비교.
- 빈 입력 n = 0 → 출력 0글자.
- 경계: 정확히 16바이트 → 1줄만 (빈 둘째 줄 X).
- 경계: offset 0xFFFFFFF0 → `fffffff0` (8자리 유지).
- 1바이트 → ASCII 열이 꽉 찬 줄과 같은 위치.
- 비인쇄 문자 0x7F·0x80·0xFF → '.' (`char`가 signed인 플랫폼에서도 `uint8_t`로 비교하므로 안전).

</details>

### 4. 꼬리 질문

Q1. "Write it into a caller-provided buffer instead of a callback. What changes?"

<details><summary>답</summary>

I'd add `char *dst, size_t cap` and a small emit helper that only writes while `len + 1 < cap` but always counts, then NUL-terminate and return the full length, snprintf-style, so the caller can detect truncation. The formatting logic stays the same — that's why I like isolating output behind one function.

- 한국어 핵심: emit 헬퍼에서 cap 검사 + 전체 길이 반환(잘림 감지).

</details>

Q2. "Memory dumps are mostly zeros. How do you make the output shorter?"

<details><summary>답</summary>

Like `hexdump -C`, remember the previous line; if the current 16 bytes are identical, print a single `*` once and skip until a different line appears. Always print the final line so the end offset is visible.

- 한국어 핵심: 이전 줄과 같으면 `*` 한 번 찍고 생략.

</details>

Q3. "The register block should be dumped as 32-bit words, not bytes. Why does that matter?"

<details><summary>답</summary>

Byte reads of a device register may be illegal or have side effects, like clearing a read-to-clear status, and the byte order you print depends on endianness. For registers I'd read with `volatile uint32_t` accesses at their natural width and print each as an 8-digit word.

- 한국어 핵심: 레지스터는 32-bit volatile 읽기 + 워드 단위 출력 (바이트 읽기 = 부작용·엔디안 혼동).

</details>

### 5. 채점 · 자주 틀리는 것

- 니블 순서 뒤집힘 (하위 니블부터 출력) — N02 교훈 그대로.
- 마지막 줄 패딩 없음 → ASCII 열이 왼쪽으로 붙음.
- `char c = buf[i]; if (c >= 0x20 ...)` 에서 signed char 0x80 → 음수, 결과는 맞아도 의도 불명확 → `uint8_t`.
- `n - line` 대신 `line + 16 <= n` 같은 조건에서 off-by-one.
- `base + line` 의 `size_t → uint32_t` 변환을 명시 안 함.

## D4 · hex 문자열 → uint32 파싱 + 에러·오버플로 (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: "0x1A2b" / "deadbeef" 같은 문자열을 uint32로 바꾸고, 틀린 입력은 종류별 에러 코드로 돌려준다.
- **상황**: 디버그 쉘 `wr 0x40000000 0x55`, 설정 파일, 테스트 스크립트 인자 — 주소를 잘못 파싱하면 **엉뚱한 레지스터에 쓴다**. 그래서 "대충 숫자처럼 보이면 OK"가 아니라 오버플로·쓰레기 문자를 반드시 거부. [C 정리 C3](2026-10-29_c_basics_cram.html)의 `parse_hex_u32`는 "자릿수 8개 초과"로 막았는데, 여기선 **값 기반 검사**(선행 0 허용) + 에러 enum.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `hex_val` (헬퍼) | 문자 1개 → 0–15, 아니면 -1 | 대소문자 모두 |
| `hex_to_u32` | 전체 문자열 파싱 | 선택적 0x/0X, 숫자 1개 이상, 끝까지 다 소비, 에러 시 `*out` 그대로 |

- **예시를 말로 풀면**: "0x1A2b" → 1, A, 2, b를 차례로 붙여 0x1A2B. "0x100000000"은 9자리라 32비트에 안 들어가니 OVERFLOW. "0x00000000ff"는 자릿수는 10개지만 값이 0xFF라 OK.
- **지켜야 할 조건**: NULL, 빈 문자열, "0x"만, 중간 공백/잘못된 문자, 부호, 오버플로 — 전부 구분.

### 1. Problem (English)

"Write a function that parses a hex string from our debug shell into a uint32. Accept an optional 0x prefix, either case. Reject anything malformed, and detect overflow — I don't want 0x100000000 silently becoming 0."

```c
typedef enum { HEX_OK = 0, HEX_ERR_NULL, HEX_ERR_EMPTY, HEX_ERR_CHAR, HEX_ERR_OVERFLOW } hex_err_t;
hex_err_t hex_to_u32(const char *s, uint32_t *out);
```

- `"0x1A2b"` → HEX_OK, 0x1A2B · `"deadbeef"` → HEX_OK, 0xDEADBEEF
- `"0x100000000"` → HEX_ERR_OVERFLOW · `"0x00000000ff"` → HEX_OK, 0xFF
- `"0x"` → HEX_ERR_EMPTY · `"12 "` → HEX_ERR_CHAR · `"-1"` → HEX_ERR_CHAR

### 2. 먼저 물어볼 것 (clarify)

- "Leading/trailing whitespace allowed?" → No; the tokenizer (D7) already split it.
- "Are leading zeros beyond 8 digits OK?" → Yes, it's the value that must fit.
- "Return value or error code?" → Error code, value through out pointer, out untouched on error.
- "Sign?" → No sign for an address/register value.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (공백, 선행 0, 에러 방식) |
| 2–5 | 에러 종류 enum부터 적기 + 예시 3개 |
| 5–14 | hex_val → prefix → 루프 + 오버플로 검사 |
| 14–17 | "0x1A2b" trace + 오버플로 순간 trace |
| 17–20 | 테스트 · strtoul과 차이 |

- Say out loud: **"Before shifting in a new nibble I check `v > UINT32_MAX >> 4`; if the top nibble is already non-zero, the shift would drop bits, so that's overflow."**
- Say out loud: **"I only write *out on success so the caller never uses a half-parsed value."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

`v = v*16 + d` 가 32비트를 넘는지 **넘기 전에** 알아야 한다. 16을 곱했을 때 넘으려면 v가 얼마보다 커야 할까?

</details>

<details><summary>힌트 2</summary>

`UINT32_MAX >> 4 == 0x0FFFFFFF`. v가 그보다 크면 상위 니블이 0이 아니어서 `v << 4`가 비트를 잃는다. d를 OR 하는 건 하위 4비트라 별도 검사 불필요. prefix 건너뛴 뒤 `*s == '\0'` 이면 EMPTY.

</details>

<details><summary>모범 답 (C)</summary>

```c
typedef enum { HEX_OK = 0, HEX_ERR_NULL, HEX_ERR_EMPTY, HEX_ERR_CHAR, HEX_ERR_OVERFLOW } hex_err_t;

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Optional "0x"/"0X", then 1+ hex digits, then end of string.
 * *out is written only on HEX_OK. Leading zeros are allowed. */
hex_err_t hex_to_u32(const char *s, uint32_t *out)
{
    if (s == NULL || out == NULL)
        return HEX_ERR_NULL;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    if (*s == '\0')
        return HEX_ERR_EMPTY;
    uint32_t v = 0;
    for (; *s != '\0'; s++) {
        int d = hex_val(*s);
        if (d < 0)
            return HEX_ERR_CHAR;
        if (v > (UINT32_MAX >> 4))          /* v << 4 would drop bits */
            return HEX_ERR_OVERFLOW;
        v = (v << 4) | (uint32_t)d;
    }
    *out = v;
    return HEX_OK;
}
```

- 시간 O(len), 공간 O(1).
- `s[1]` 접근은 `s[0] == '0'` 일 때만 → "0" 한 글자 문자열에서도 `s[1]`은 NUL이라 범위 안.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`"0x1A2b"` — prefix 건너뜀 후:

| 문자 | d | 검사 v > 0x0FFFFFFF? | v (후) |
|---|---|---|---|
| '1' | 1 | 0 → no | 0x1 |
| 'A' | 10 | 0x1 → no | 0x1A |
| '2' | 2 | 0x1A → no | 0x1A2 |
| 'b' | 11 | 0x1A2 → no | 0x1A2B → OK |

`"0x100000000"` — 8번째 '0'까지 v = 0x10000000, 9번째 '0'에서 `0x10000000 > 0x0FFFFFFF` → **OVERFLOW** (`*out` 안 씀).

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: "0x1A2b", "deadbeef", "0XFFFFFFFF" (최댓값), "0", "0x0".
- 빈 입력: "" 와 "0x" → EMPTY, NULL → NULL 에러.
- 경계: "0xFFFFFFFF" OK vs "0x100000000" OVERFLOW, "123456789"(9자리) OVERFLOW, 선행 0 "0x00000000ff" OK.
- 잘못된 문자: "0xg1", "12 " (뒤 공백), "-1", "0x0x1".
- 에러 시 `*out` 값 유지 확인.

</details>

### 4. 꼬리 질문

Q1. "Why not just call strtoul(s, &end, 16)?"

<details><summary>답</summary>

You can, but you must check three things: `end != s` for no digits, `*end == '\0'` for trailing junk, and `errno == ERANGE` for overflow — and you must set errno to 0 first. strtoul also skips leading whitespace and accepts a minus sign, so "-1" returns ULONG_MAX without error, and on LP64 unsigned long is 64-bit, so you need an extra `> UINT32_MAX` check. On bare metal, libc may not even be linked.

- 한국어 핵심: strtoul은 end/errno/부호/64-bit 폭 4가지를 직접 검사해야 함 — "-1"이 에러 없이 ULONG_MAX.

</details>

Q2. "Extend it to accept decimal too, like `wr 0x40000000 85`."

<details><summary>답</summary>

Detect the base from the prefix: "0x" means 16, otherwise 10. For base 10 the overflow check becomes `v > (UINT32_MAX - d) / 10` before `v = v * 10 + d`, same idea as the atoi in C3. I'd keep one loop with a `base` variable and a digit function that rejects digits ≥ base.

- 한국어 핵심: base 변수 하나로, 일반식 `v > (MAX - d) / base`.

</details>

Q3. "What about 64-bit addresses?"

<details><summary>답</summary>

Same code with `uint64_t` and `UINT64_MAX >> 4`. The check is width-generic: `v > MAX >> 4` for hex because multiplying by 16 is a 4-bit shift.

- 한국어 핵심: 타입과 MAX만 바꾸면 끝.

</details>

### 5. 채점 · 자주 틀리는 것

- 시프트 **후에** 검사 (`if (v < old)`) → 시프트로 잃은 비트는 감지 안 될 수 있음.
- "0x"를 0으로 받아들임 (EMPTY여야).
- 뒤 공백/쓰레기를 조용히 무시 (끝까지 소비 확인 안 함).
- 에러 경로에서 `*out` 을 중간값으로 덮음.
- `'a' - 'a' + 10` 대신 `'a' - 'A'` 같은 오타 — hex_val을 따로 써서 3줄로.

## D5 · 배열 k칸 회전 (3-reverse) · in-place 중복 제거 (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: (a) 배열을 왼쪽으로 k칸 돌리기 — 추가 배열 없이. (b) 정렬된 배열에서 중복을 지우고 새 길이 반환.
- **상황**: 펌웨어는 힙이 없고 스택도 작아서 "임시 배열 하나 더"가 안 되는 경우가 많다. 샘플 윈도 정렬(링 → 선형), 정렬된 실패 주소 목록에서 중복 제거(메모리 테스트 리포트)에 그대로 쓰인다.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `reverse` (헬퍼) | [lo, hi) 구간 뒤집기 | 반열린 구간, size_t underflow 없이 |
| `rotate_left` | 왼쪽 k칸 회전 | k ≥ n이면 k % n, O(1) 공간 |
| `dedup_sorted` | 연속 중복 제거 | 앞쪽에 유일값만, 새 길이 반환 |

- **예시를 말로 풀면**: [1 2 3 4 5]를 왼쪽 2칸 → 앞의 1, 2가 맨 뒤로 → [3 4 5 1 2]. [1 1 2 3 3 3 7] → 1, 2, 3, 7 네 개만 앞에 남김.
- **지켜야 할 조건**: n = 0, n = 1, k = 0, k = n, k > n, 전부 같은 값, 중복 없음.

### 1. Problem (English)

"Rotate an array left by k positions in place, O(1) extra memory. Then, separately: given a sorted array, remove duplicates in place and return the new length."

```c
void   rotate_left(uint32_t *a, size_t n, size_t k);
size_t dedup_sorted(uint32_t *a, size_t n);
```

- `[1 2 3 4 5]`, k=2 → `[3 4 5 1 2]` · k=7 → same as k=2 · k=4 → `[5 1 2 3 4]`
- `[1 1 2 3 3 3 7]` → 4, `[1 2 3 7]` · `[5 5 5]` → 1

### 2. 먼저 물어볼 것 (clarify)

- "Left or right rotation?" → Left; right by k = left by n − k.
- "Can k be larger than n?" → Yes, reduce mod n.
- "For dedup, is the input sorted, or should I keep first occurrences in an unsorted array?" → Sorted.
- "What's beyond the new length?" → Don't care.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (방향, k 범위, 정렬 여부) |
| 2–5 | 3-reverse를 예시로 손으로 3줄 |
| 5–12 | reverse 헬퍼 → rotate → dedup (read/write 두 인덱스) |
| 12–17 | trace 두 개 |
| 17–20 | 테스트 · 꼬리 |

- Say out loud: **"Reversing the first k, then the rest, then the whole array puts each block in place in its original order — O(n) time and O(1) space."**
- Say out loud: **"For dedup I keep a write index w; a[0..w) is always the unique prefix, and I copy a[r] only when it differs from a[w-1]."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

[A B] → [B A] 를 만들고 싶다 (A = 앞 k개). 전체를 뒤집으면 [B' A'] (각각 거꾸로). 그다음 무엇을 하면 될까? 순서를 바꿔 먼저 부분을 뒤집어도 된다.

</details>

<details><summary>힌트 2</summary>

reverse(0,k) → [A' B], reverse(k,n) → [A' B'], reverse(0,n) → [B A]. 반열린 구간 reverse 루프 조건은 `hi - lo > 1` (`hi - 1`을 미리 계산하면 hi=0에서 underflow). dedup은 r = 1부터.

</details>

<details><summary>모범 답 (C)</summary>

```c
static void reverse(uint32_t *a, size_t lo, size_t hi)   /* [lo, hi) */
{
    while (hi - lo > 1) {                 /* hi > lo + 1, no size_t underflow */
        uint32_t t = a[lo];
        a[lo++] = a[--hi];
        a[hi] = t;
    }
}

/* Rotate left by k in place: [1 2 3 4 5], k=2 -> [3 4 5 1 2]. O(n) time, O(1) space. */
void rotate_left(uint32_t *a, size_t n, size_t k)
{
    if (n < 2)
        return;
    k %= n;
    if (k == 0)
        return;
    reverse(a, 0, k);
    reverse(a, k, n);
    reverse(a, 0, n);
}

/* Remove consecutive duplicates (sorted input -> unique). Returns new length. */
size_t dedup_sorted(uint32_t *a, size_t n)
{
    if (n == 0)
        return 0;
    size_t w = 1;                         /* a[0..w) is the kept prefix */
    for (size_t r = 1; r < n; r++)
        if (a[r] != a[w - 1])
            a[w++] = a[r];
    return w;
}
```

- rotate: 시간 O(n) (각 원소 2번 swap), 공간 O(1). dedup: 시간 O(n), 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

rotate `[1 2 3 4 5]`, k = 2:

| 단계 | 배열 |
|---|---|
| reverse(0,2) | [**2 1** 3 4 5] |
| reverse(2,5) | [2 1 **5 4 3**] |
| reverse(0,5) | [3 4 5 1 2] |

dedup `[1 1 2 3 3 3 7]`:

| r | a[r] | a[w-1] | 동작 | w | 앞부분 |
|---|---|---|---|---|---|
| 1 | 1 | 1 | skip | 1 | [1] |
| 2 | 2 | 1 | a[1]=2 | 2 | [1 2] |
| 3 | 3 | 2 | a[2]=3 | 3 | [1 2 3] |
| 4–5 | 3 | 3 | skip | 3 | [1 2 3] |
| 6 | 7 | 3 | a[3]=7 | 4 | [1 2 3 7] |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 예시 두 개.
- 빈 입력: n = 0 (NULL이어도 접근 안 함), n = 1.
- 경계: k = 0, k = n (변화 없음), k = 2n+…(mod), k = n−1 (= 오른쪽 1칸).
- dedup: 전부 같음 → 1, 중복 없음 → n 그대로.
- 멱등성: dedup 결과에 다시 dedup → 길이 같음. rotate(k) 후 rotate(n−k) → 원래대로.
- 전수: n ≤ 9, k ≤ 2n을 naive와 비교 (`run.sh`).

</details>

### 4. 꼬리 질문

Q1. "Is there a rotation that does each element exactly once?"

<details><summary>답</summary>

Yes, the juggling or cycle-leader algorithm: there are gcd(n, k) cycles, and in each one you hold one element in a temp and move elements k apart until you return to the start. It does n moves instead of about 2n swaps, but the access pattern jumps around, so it's often slower on cached memory than the three reverses.

- 한국어 핵심: gcd(n,k)개 사이클, n번 이동 — 하지만 캐시 친화성은 3-reverse가 좋음.

</details>

Q2. "Now the array isn't sorted. Remove duplicates keeping first occurrence."

<details><summary>답</summary>

Without extra memory it's O(n²): for each element, scan the kept prefix. If the value range is small, like 8-bit IDs, a 256-bit bitmap on the stack makes it O(n). Or sort first in O(n log n) if order doesn't matter.

- 한국어 핵심: 메모리 없으면 O(n²), 값 범위 작으면 비트맵 O(n).

</details>

Q3. "Where would you use rotation in firmware?"

<details><summary>답</summary>

Linearizing a ring buffer snapshot so the oldest sample is at index 0 before handing it to a filter or dumping it — rotate left by the tail index. Same trick for a trace log that wrapped.

- 한국어 핵심: 링버퍼/trace 로그를 오래된 순으로 펴기 = tail만큼 왼쪽 회전.

</details>

### 5. 채점 · 자주 틀리는 것

- `k %= n` 을 n == 0 검사 전에 → 0으로 나누기.
- reverse(lo, hi) 에서 `hi - 1` 을 hi = 0일 때 계산 → `size_t` underflow.
- 세 번 뒤집기 순서/구간을 오른쪽 회전과 섞어 씀 (손으로 한 번 확인).
- dedup에서 `a[r] != a[r-1]` 로 비교해도 정렬 입력에선 맞지만, 기준은 "마지막으로 남긴 값" `a[w-1]` 이 명확.
- dedup 빈 배열에서 w = 1 반환.

## D6 · 고정 크기 블록 pool allocator (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 정적 메모리 덩어리를 같은 크기 블록으로 나눠서 O(1) alloc/free를 만든다. 빈 블록 목록(free list)은 **빈 블록 안**에 저장.
- **상황**: 펌웨어에서 `malloc`은 단편화·비결정적 시간 때문에 거의 금지. 대신 메시지 버퍼, DMA 디스크립터, 패킷 버퍼를 고정 크기 pool로 관리한다 (Zephyr `k_mem_slab`, ThreadX block pool 같은 것). 정렬된 할당 자체는 [C 정리 C10](2026-10-29_c_basics_cram.html).

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `pool_init` | 메모리를 블록으로 나누고 모두 free list에 | block_size를 `sizeof(void*)` 배수로 올림, mem 정렬 검사 |
| `pool_alloc` | free list 머리 하나 떼서 반환 | 없으면 NULL |
| `pool_free` | 블록을 free list 머리에 다시 붙임 | 이 pool의 블록 시작 주소가 아니면 false |

- **예시를 말로 풀면**: 256바이트, 블록 20바이트 요청 → 포인터 저장 + 정렬 위해 24로 올림 → 10블록(240바이트) + 남는 16바이트는 버림. 11번째 alloc은 NULL.
- **지켜야 할 조건**: 메타데이터 배열 없음, alloc/free O(1), 잘못된 free 거부, 할당된 블록은 사용자가 전부 써도 됨.

### 1. Problem (English)

"No malloc on this target. Write a fixed-size block allocator over a static buffer. Alloc and free must be O(1), and you can't use a separate metadata array. Free should reject pointers that don't belong to the pool."

```c
typedef struct { void *free_head; uint8_t *base; size_t block_size, nblocks, nfree; } pool_t;
bool  pool_init(pool_t *p, void *mem, size_t mem_size, size_t block_size);
void *pool_alloc(pool_t *p);
bool  pool_free(pool_t *p, void *blk);
```

- init(256 B, 20) → block_size 24, nblocks 10 (LP64)
- 10 allocs → mem+0, mem+24, …, mem+216; 11th → NULL
- free(b3), free(b7) → next alloc returns b7, then b3 (LIFO)
- free(b2 + 4) → false

### 2. 먼저 물어볼 것 (clarify)

- "What alignment do blocks need?" → At least pointer alignment; for DMA, maybe a cache line (follow-up).
- "Is it called from ISRs?" → Assume thread context only for now.
- "Do you want double-free detection?" → Nice to have — follow-up.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (정렬, ISR, double free) |
| 2–5 | 그림: [B0]→[B1]→[B2]→NULL, 각 블록 첫 8바이트 = next |
| 5–15 | init (역순 push) → alloc (pop) → free (검사 + push) |
| 15–18 | 3블록 trace |
| 18–20 | 테스트 · ISR/double free 꼬리 |

- Say out loud: **"A free block's memory is unused, so I store the next pointer inside it. Alloc pops the head, free pushes it back — both O(1)."**
- Say out loud: **"To validate a free, the offset from base must be inside the pool and a multiple of block_size."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

free 블록은 아무도 안 쓰는 메모리다. 그 안의 첫 칸에 무엇을 저장하면 리스트를 만들 수 있을까? 그러려면 블록 크기 최소값은?

</details>

<details><summary>힌트 2</summary>

`*(void **)blk = head; head = blk;` (push), `blk = head; head = *(void **)blk;` (pop). init에서 블록을 **역순**으로 push하면 첫 alloc이 블록 0. block_size = `(bs + a - 1) / a * a`, a = `sizeof(void*)`.

</details>

<details><summary>모범 답 (C)</summary>

```c
typedef struct {
    void    *free_head;      /* singly linked list threaded through free blocks */
    uint8_t *base;
    size_t   block_size;
    size_t   nblocks;
    size_t   nfree;
} pool_t;

/* mem must be aligned for void*. block_size is rounded up to sizeof(void*). */
bool pool_init(pool_t *p, void *mem, size_t mem_size, size_t block_size)
{
    const size_t a = sizeof(void *);
    if (p == NULL || mem == NULL || ((uintptr_t)mem % a) != 0)
        return false;
    block_size = (block_size + a - 1) / a * a;
    if (block_size == 0 || mem_size < block_size)
        return false;
    p->base = mem;
    p->block_size = block_size;
    p->nblocks = mem_size / block_size;
    p->nfree = p->nblocks;
    p->free_head = NULL;
    for (size_t i = p->nblocks; i-- > 0;) {      /* push in reverse: block 0 first out */
        void *blk = p->base + i * block_size;
        *(void **)blk = p->free_head;            /* "next" lives inside the free block */
        p->free_head = blk;
    }
    return true;
}

void *pool_alloc(pool_t *p)
{
    void *blk = p->free_head;
    if (blk == NULL)
        return NULL;                             /* exhausted */
    p->free_head = *(void **)blk;
    p->nfree--;
    return blk;
}

bool pool_free(pool_t *p, void *blk)
{
    uintptr_t off = (uintptr_t)blk - (uintptr_t)p->base;
    if (blk == NULL || (uintptr_t)blk < (uintptr_t)p->base ||
        off >= p->nblocks * p->block_size || off % p->block_size != 0)
        return false;                            /* not one of ours */
    *(void **)blk = p->free_head;
    p->free_head = blk;
    p->nfree++;
    return true;
}
```

- alloc/free O(1), init O(nblocks), 추가 공간 0 (pool_t 제외).
- 테스트의 backing 메모리는 `static void *mem[32]` — free list가 `void*`를 쓰는 객체 타입과 일치해서 strict aliasing 문제 없음. `uint8_t` 배열을 쓰면 `_Alignas(max_align_t)` 로 정렬은 맞출 수 있지만 aliasing은 엄밀히 회색 지대 `[추정: 실제 RTOS들은 -fno-strict-aliasing이나 char 배열 + 정렬 속성으로 사용]`.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

3블록 pool (B0, B1, B2), block_size 24.

| 동작 | head | 리스트 | nfree |
|---|---|---|---|
| init (i=2,1,0 순 push) | B0 | B0→B1→B2→NULL | 3 |
| alloc → B0 | B1 | B1→B2→NULL | 2 |
| alloc → B1 | B2 | B2→NULL | 1 |
| free(B0) | B0 | B0→B2→NULL | 2 |
| alloc → B0 | B2 | B2→NULL | 1 |

- free(B0)에서 B0의 첫 8바이트에 B2 주소가 쓰인다 → 사용자가 쓰던 데이터가 덮이는 것 = 정상 (이미 반납했으니까).

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 10개 alloc → 주소 순서, 각 블록 24바이트 memset 해도 다른 블록 안 깨짐.
- 고갈: 11번째 → NULL, nfree 0.
- 경계: block_size 0, mem이 1블록보다 작음, 정렬 안 된 mem → init false.
- 잘못된 free: NULL, 블록 중간(+4), pool 끝 남는 영역(mem+240), base 아래 → false, nfree 불변.
- 왕복: 전부 free → nfree 10, 다시 전부 alloc 가능.

</details>

### 4. 꼬리 질문

Q1. "Now an ISR also allocates from this pool. What do you change?"

<details><summary>답</summary>

The pop is a read-modify-write of `free_head`, so main and ISR can race and hand out the same block. On a single core the simplest fix is to disable interrupts around the few lines that touch `free_head` and `nfree` and restore the previous state after. A lock-free CAS pop is possible but has the ABA problem, so I'd only go there with a tagged pointer.

- 한국어 핵심: head RMW = 레이스 → 짧은 IRQ disable 구간 (lock-free CAS는 ABA 문제).

</details>

Q2. "How would you detect a double free?"

<details><summary>답</summary>

Cheapest is a bitmap of allocated blocks, one bit per block: free checks and clears the bit, alloc sets it — still O(1). Without extra memory, I could write a magic value into the second word of a freed block and check it on free, which catches most double frees but can be fooled by user data. Walking the free list is O(n) and only for debug builds.

- 한국어 핵심: 블록당 1비트 비트맵이 O(1) 정답, magic 값은 확률적.

</details>

Q3. "These blocks are DMA buffers on a non-coherent SoC. Anything else?"

<details><summary>답</summary>

Round block_size and align the base to the cache line, for example 64 bytes, so a block never shares a line with another block or with the free-list pointer of a neighbor. Otherwise cache invalidate before a DMA read could throw away another block's dirty data.

- 한국어 핵심: 블록 크기·base를 캐시 라인 정렬 — 이웃 블록과 라인 공유 금지.

</details>

### 5. 채점 · 자주 틀리는 것

- block_size < `sizeof(void*)` 허용 → next 포인터 저장하다 옆 블록 침범.
- init에서 마지막 블록 next를 NULL로 안 끊음.
- pool_free 범위 검사 없이 push → 외부 포인터가 리스트에 섞여 메모리 손상.
- nfree 갱신 빠뜨림 / alloc NULL 경우에 nfree-- 해버림.
- 포인터 차를 `int`로 받음 → `uintptr_t` 또는 `size_t`.

## D7 · 명령줄 in-place 토큰화 → argc/argv (★★ · 15분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: "wr 0x40000000 0x55" 한 줄을 그 자리에서 잘라 argc = 3, argv = {"wr", "0x40000000", "0x55"} 로 만든다.
- **상황**: UART 디버그 쉘(bring-up 보드, 검증 펌웨어)의 첫 단계. malloc 없이 입력 버퍼 자체에 `'\0'`을 박아서 토큰을 만들고 argv는 그 안을 가리키는 포인터. 다음 단계가 D4 hex 파싱과 명령 테이블(G4).

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `is_sep` (헬퍼) | 구분자인가 | space, tab, CR, LF |
| `tokenize` | 줄을 토큰으로, argv 채움 | 연속 구분자 = 하나, 앞뒤 공백 무시, 토큰 > max_args면 -1 |

- **예시를 말로 풀면**: "wr" 뒤 공백을 `'\0'`으로 바꾸면 argv[0] = "wr"가 완성되고, 다음 글자부터 다시 토큰 시작.
- **지켜야 할 조건**: 빈 줄, 공백만 있는 줄, 끝의 "\r\n", 정확히 max_args개, max_args 초과.

### 1. Problem (English)

"Our debug shell reads a line over UART. Split it into argc/argv in place — no malloc, no strtok. Treat any run of spaces, tabs or CR/LF as one separator. Return -1 if there are more tokens than fit."

```c
int tokenize(char *line, char *argv[], int max_args);
```

- `"wr 0x40000000 0x55"` → 3: "wr", "0x40000000", "0x55"
- `"  rd\t 0x10  \r\n"` → 2: "rd", "0x10"
- `""` or `"   "` → 0 · `"a b c d e"` with max_args 4 → -1

### 2. 먼저 물어볼 것 (clarify)

- "Do we need quoted strings?" → No.
- "Too many tokens — error or truncate?" → Error, -1, so a mistyped command isn't half-executed.
- "Should argv be NULL-terminated?" → Not required (we return argc).

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (따옴표, 초과 처리) |
| 2–4 | 상태 2개로 설명: "구분자 건너뛰기" ↔ "토큰 본문" |
| 4–11 | 루프 작성 |
| 11–14 | 예시 trace |
| 14–15 | 테스트 |

- Say out loud: **"I alternate two phases: skip separators, then record a token start and walk to its end, then overwrite that separator with NUL."**
- Say out loud: **"I check argc against max_args before storing, so I never write past argv."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

바깥 무한 루프 안에 작은 while 두 개: 하나는 구분자를 건너뛰고, 하나는 토큰 끝까지 간다. 각 while 뒤에 `'\0'`인지 확인.

</details>

<details><summary>힌트 2</summary>

토큰 끝에서 `*p == '\0'`이면 종료(마지막 토큰은 이미 NUL로 끝남), 아니면 `*p++ = '\0'`. argv 저장 **직전**에 `argc == max_args` 이면 -1.

</details>

<details><summary>모범 답 (C)</summary>

```c
static bool is_sep(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Split line in place: separators become '\0', argv[i] points into line.
 * Returns argc, or -1 if there are more than max_args tokens. */
int tokenize(char *line, char *argv[], int max_args)
{
    int argc = 0;
    char *p = line;
    for (;;) {
        while (is_sep(*p))
            p++;                          /* skip run of separators */
        if (*p == '\0')
            break;
        if (argc == max_args)
            return -1;
        argv[argc++] = p;                 /* token start */
        while (*p != '\0' && !is_sep(*p))
            p++;                          /* token body */
        if (*p == '\0')
            break;
        *p++ = '\0';                      /* terminate token, step past */
    }
    return argc;
}
```

- 시간 O(len), 공간 O(1) (argv는 호출자 제공).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`"wr 0x40000000 0x55"` (index: w=0 r=1 ' '=2 '0'=3 … ' '=13 '0'=14 … '5'=17 NUL=18)

| 단계 | p 위치 | 동작 | argc |
|---|---|---|---|
| skip | 0 ('w') | 구분자 아님 | 0 |
| token | 0 → 2 | argv[0]=line+0, line[2]='\0' | 1 |
| token | 3 → 13 | argv[1]=line+3, line[13]='\0' | 2 |
| token | 14 → 18 | argv[2]=line+14, *p=='\0' → break | 3 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: 예시 → 3개, argv[1] == line + 3 (in-place 확인).
- 빈 입력: "" → 0, 공백/탭/CRLF만 → 0.
- 경계: 앞뒤 공백 + "\r\n" 붙은 줄, 토큰 1개("help"), 정확히 max_args개 → OK.
- 에러: max_args+1개 → -1, max_args = 0에 토큰 1개 → -1 (공백만이면 0).

</details>

### 4. 꼬리 질문

Q1. "Why not strtok?"

<details><summary>답</summary>

strtok keeps hidden static state, so it isn't reentrant — if the shell is interrupted and another context tokenizes, or a command handler itself calls strtok, the state is corrupted. strtok_r fixes that with a caller-provided save pointer, but it may not exist in a minimal libc, and my loop is ten lines anyway.

- 한국어 핵심: strtok = static 상태 → 재진입 불가, strtok_r은 libc 의존.

</details>

Q2. "Add support for double-quoted arguments like `echo "a b"`."

<details><summary>답</summary>

When the token starts with a quote, skip it, set the token start after it, and scan until the closing quote instead of a separator, then overwrite the closing quote with NUL. An unterminated quote should be an error. Escapes inside quotes would need a write pointer that compacts the string in place.

- 한국어 핵심: 따옴표면 끝 조건을 "닫는 따옴표"로 바꾸기, 안 닫히면 에러.

</details>

Q3. "The line comes from UART byte by byte. Who builds the line?"

<details><summary>답</summary>

A line assembler: an RX ISR pushes bytes into a ring buffer, and the main loop pops them into a fixed line buffer, handles backspace, echoes, and on CR or LF terminates the string and calls tokenize. If the line buffer fills, drop the line and report an error rather than overflowing.

- 한국어 핵심: ISR → 링버퍼 → main에서 줄 조립(백스페이스, 길이 제한) → tokenize.

</details>

### 5. 채점 · 자주 틀리는 것

- argv 저장 후에 개수 검사 → argv[max_args] 오버런.
- 마지막 토큰 뒤에서 `*p++ = '\0'` 하고 계속 진행 → NUL 지나서 읽음.
- 끝의 "\r" 를 구분자로 안 봐서 마지막 인자가 "0x55\r" → D4 파싱에서 CHAR 에러.
- 문자열 리터럴(`char *s = "wr …"`)에 테스트 → read-only 쓰기 crash (C2와 같은 함정).

## D8 · 이동 평균 (window N, 정수 running sum) (★ · 15분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 최근 N개 ADC 샘플 평균을 샘플마다 O(1)로 낸다. 처음 N개가 차기 전엔 들어온 개수로 나눈다.
- **상황**: 전압/전류/온도 모니터링, 센서 노이즈 제거. 매번 N개를 다시 더하면 O(N)이고, 부동소수점이 없거나 비싼 MCU가 많아 **정수 running sum**을 쓴다. 시작 구간(warm-up)을 0으로 채운 평균으로 내면 처음 N개 출력이 낮게 나와 알람 오판이 생길 수 있다.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `ma_init` | window 설정, 상태 0 | 1 ≤ N ≤ 64 아니면 false |
| `ma_update` | 샘플 넣고 평균 반환 | sum += new − oldest, 반올림, 시작 구간은 count로 나눔 |

- **예시를 말로 풀면**: N=4에 10, 20, 30, 40 → 10, 15, 20, 25 (1·2·3·4개 평균). 50이 오면 10이 빠져서 (20+30+40+50)/4 = 35. 0이 오면 20이 빠져 120/4 = 30.
- **지켜야 할 조건**: 오버플로 없는 sum 타입, N = 1, 최댓값 0xFFFF 연속, 반올림.

### 1. Problem (English)

"Give me a moving average of the last N 16-bit ADC samples. Constant time per sample, integers only, rounded to nearest. Before the window fills, average what you have."

```c
typedef struct { uint16_t buf[64]; uint32_t sum; uint8_t window, idx, count; } movavg_t;
bool     ma_init(movavg_t *m, unsigned window);
uint16_t ma_update(movavg_t *m, uint16_t x);
```

- N=4: 10 20 30 40 50 0 → 10 15 20 25 35 30
- N=2: 1 2 → 1, 2 (1.5 rounds to 2)
- N=1 → output equals input

### 2. 먼저 물어볼 것 (clarify)

- "Max window?" → 64, so sum ≤ 64 × 65535 < 2³² — uint32 is enough.
- "Startup behavior?" → Divide by the number of samples seen so far.
- "Rounding?" → Round half up: `(sum + count/2) / count`.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (N 최대, 시작 구간, 반올림) |
| 2–4 | sum 범위 계산 → uint32 결정 말하기 |
| 4–10 | struct · init · update |
| 10–13 | N=4 trace 6스텝 |
| 13–15 | 테스트 |

- Say out loud: **"I keep a running sum: add the new sample, subtract the one falling out of the window. That's O(1), and with integers there's no drift — the sum is always exact."**
- Say out loud: **"The worst-case sum is 64 times 65535, which fits in 32 bits."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

원형 버퍼(N칸) + 합계 하나. 새 샘플이 덮을 칸에 있던 값이 "창 밖으로 나가는" 값이다.

</details>

<details><summary>힌트 2</summary>

count < N이면 count++ (빈 칸 = 0이라 뺄 것 없음), 아니면 `sum -= buf[idx]`. 그다음 `buf[idx] = x; sum += x; idx = (idx + 1) % N;` 평균 = `(sum + count/2) / count`.

</details>

<details><summary>모범 답 (C)</summary>

```c
#define MA_MAX 64

typedef struct {
    uint16_t buf[MA_MAX];
    uint32_t sum;            /* 64 * 65535 < 2^32: cannot overflow */
    uint8_t  window;         /* N, 1..MA_MAX */
    uint8_t  idx;            /* next slot to overwrite */
    uint8_t  count;          /* samples seen, saturates at window */
} movavg_t;

bool ma_init(movavg_t *m, unsigned window)
{
    if (window == 0 || window > MA_MAX)
        return false;
    memset(m, 0, sizeof *m);
    m->window = (uint8_t)window;
    return true;
}

/* Push one sample, return rounded mean of the last min(count, N) samples. */
uint16_t ma_update(movavg_t *m, uint16_t x)
{
    if (m->count < m->window)
        m->count++;                       /* warm-up: slot was empty (0) */
    else
        m->sum -= m->buf[m->idx];         /* drop oldest */
    m->buf[m->idx] = x;
    m->sum += x;
    m->idx = (uint8_t)((m->idx + 1u) % m->window);
    return (uint16_t)((m->sum + m->count / 2u) / m->count);
}
```

- update O(1), 공간 O(N).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

N = 4:

| x | count | 빠지는 값 | sum | 출력 (sum + c/2)/c |
|---|---|---|---|---|
| 10 | 1 | — | 10 | 10 |
| 20 | 2 | — | 30 | (30+1)/2 = 15 |
| 30 | 3 | — | 60 | (60+1)/3 = 20 |
| 40 | 4 | — | 100 | (100+2)/4 = 25 |
| 50 | 4 | buf[0]=10 | 140 | (140+2)/4 = 35 |
| 0 | 4 | buf[1]=20 | 120 | (120+2)/4 = 30 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상: N=4 예시 6개 출력.
- 경계: N = 0, N = 65 → init false; N = 1 → 출력 = 입력.
- 최댓값: N = 64에 0xFFFF 192개 → 출력 0xFFFF, sum = 64 × 0xFFFF (오버플로 없음).
- 반올림: N=2, 1·2 → 2.
- 왕복/드리프트: 큰 값 후 0을 N개 → sum 정확히 0.

</details>

### 4. 꼬리 질문

Q1. "Make the divide cheaper."

<details><summary>답</summary>

Restrict N to a power of two so the steady-state division is a shift: `(sum + N/2) >> log2N`. During warm-up the count isn't a power of two, so either accept a divide only there, or pre-fill the window with the first sample so you always divide by N.

- 한국어 핵심: N을 2의 거듭제곱으로 → 시프트, warm-up은 첫 샘플로 채우는 방법도.

</details>

Q2. "I don't have RAM for 64 samples. Alternative?"

<details><summary>답</summary>

An exponential moving average with one state variable: `y += (x - y) >> k`. To avoid losing precision keep y scaled up, for example store `acc = y << k` and do `acc += x - (acc >> k)`, output `acc >> k`. Careful with signed arithmetic: `x - y` can be negative, and right-shifting a negative value is implementation-defined in C, so use the scaled unsigned form.

- 한국어 핵심: EMA = 상태 1개, 스케일된 accumulator로 정밀도 유지, 음수 >> 주의.

</details>

Q3. "The ADC ISR calls ma_update and main reads the average. Problems?"

<details><summary>답</summary>

If main reads `sum` and `count` separately while the ISR updates them, it can see a torn pair. Better: the ISR computes the average and stores it in a single volatile `uint16_t`, which is one atomic store on Cortex-M, and main just reads that. Or main disables the ADC interrupt briefly around reading the struct.

- 한국어 핵심: ISR이 결과를 volatile 16-bit 하나로 → main은 그것만 읽기.

</details>

### 5. 채점 · 자주 틀리는 것

- sum을 `uint16_t`로 → 두 샘플만 더해도 오버플로.
- 시작 구간에 항상 N으로 나눔 → 처음 출력이 낮게 나옴 (요구사항 위반).
- 빼기 전에 덮어씀 (`buf[idx] = x` 를 `sum -= buf[idx]` 보다 먼저).
- 반올림 없이 내림 → 평균이 항상 0.5 LSB 낮게 치우침 (bias).
- `idx` 를 `% N` 대신 `% MA_MAX` 로.

## 출처

1. cppreference — memmove: https://en.cppreference.com/w/c/string/byte/memmove
2. cppreference — strtoul (whitespace, sign, ERANGE 동작): https://en.cppreference.com/w/c/string/byte/strtoul
3. cppreference — strtok (static state, strtok_r 대비): https://en.cppreference.com/w/c/string/byte/strtok
4. C11 draft N1570 (6.5p7 strict aliasing, 6.5.8p5 포인터 관계 비교, 6.5.7 시프트): https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
5. Linux kernel lib/hexdump.c (print_hex_dump 포맷) · include/asm-generic/io.h (memcpy_toio): https://github.com/torvalds/linux/blob/master/lib/hexdump.c
6. Arm — Cortex-M0 (ARMv6-M) unaligned access 미지원 / Cortex-M3·M4 unaligned LDR/STR 지원: https://developer.arm.com/documentation/dui0497/a/the-cortex-m0-processor/memory-model/memory-regions--types-and-attributes (JS 렌더링이라 본문 fetch 실패 — 널리 알려진 사실로 인용, 세부 명령 목록 `[미검증]`)
7. Zephyr — Memory Slabs (고정 크기 블록, free list를 미사용 블록의 첫 워드에 저장 — D6와 같은 구조): https://docs.zephyrproject.org/latest/kernel/memory_management/slabs.html
8. 내부: [C 정리 C1–C3·C10](2026-10-29_c_basics_cram.html) · [N02 니블 회고](2026-10-31_N02_screen_coding_retro.html) · [N05 목록](2026-11-01_N05_c_paper_drill_bank.html)
