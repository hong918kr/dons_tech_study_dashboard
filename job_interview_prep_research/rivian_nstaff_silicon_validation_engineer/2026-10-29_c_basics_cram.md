# C 기본기 벼락치기 — Rivian HM screen 전날 (2–3시간)
<!-- theme: pastel -->

> **대상**: 10-30(금) 45분 · Manjiang Zhang · **범위**: strcpy · revstr · atoi · 64-bit timer · ring buffer · bit/field mask + α · **분량**: 10문제 + 개념 퀵파이어 · **검증**: 모든 코드 `cBasicsCram/cram_all.c` 컴파일 + ASan/UBSan ALL PASS · **작성**: 2026-10-29

## TL;DR — 오늘 밤 이것만 (총 2시간 30분)

| 시간 | 할 것 | 목표 |
|---|---|---|
| 0:00–0:10 | §0 말하는 순서 소리 내 읽기 | 코딩 중 침묵 없애기 |
| 0:10–0:40 | **C6 bit/field mask + RMW + W1C** | HW 출신 면접관 1순위. GENMASK·FIELD_PREP 손으로 |
| 0:40–1:05 | **C4 64-bit timer (hi-lo-hi)** + wrap-safe 비교 | "왜 retry?"를 숫자로 설명 |
| 1:05–1:30 | **C5 ring buffer** (free-running index) | full/empty 판정 + ISR/멀티코어 순서 |
| 1:30–2:00 | **C1 strcpy/memmove · C2 revstr · C3 atoi** | 경계(빈 문자열, NULL, overflow) 말하기 |
| 2:00–2:15 | C7 bit tricks · C8 endian | 한 줄 공식 외우기 |
| 2:15–2:30 | §11 개념 퀵파이어 | volatile·const·padding·promotion |
| 남으면 | C9 list reverse · C10 aligned_malloc | 보너스 |

- 방법: 각 문제 **코드 접힌 상태에서 시그니처만 보고 5분 안에 머릿속/종이로** → 펼쳐서 비교 → "함정" 체크 → 영어 30초 설명 한 번 소리 내기.
- 직접 돌려 보려면 (이 Mac은 기본 SDK 27.2 링커가 깨져서 `-isysroot` 필요):

```bash
cd ~/workspace/dons_tech_study_dashboard/job_interview_prep_research/rivian_nstaff_silicon_validation_engineer/cBasicsCram
cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
   -fsanitize=address,undefined cram_all.c -o /tmp/cram && /tmp/cram     # → ALL PASS
```

- 더 깊게: [V2 reg field RMW](hmScreenPrep/problems/V2_reg_field_rmw/index.html) · [V3 DMA desc ring](hmScreenPrep/problems/V3_dma_desc_ring/index.html) · [L5 SPSC ring](hmScreenPrep/problems/L5_spsc_sensor_ring/index.html) · [HM screen deck](2026-10-25_rivian_hm_screen_prep.html) · **[면접 직전 훑어보기 (volatile · const · 임베디드/검증)](2026-10-30_final_skim.html)**

## 0. 코딩할 때 말하는 순서 (모든 문제 공통)

1. **질문 다시 말하기 + clarify** — "So I take a NUL-terminated string and reverse it in place. Can I assume the buffer is writable, and should I handle NULL?"
2. **시그니처 먼저** — "Let me write the signature first: `void revstr(char *s)`."
3. **예시 1개 + 엣지 2개 말하기** — "Normal: hello → olleh. Edge: empty string, single char."
4. **코드 쓰면서 중얼거리기** — "Two pointers, swap until they meet."
5. **손으로 trace + 테스트 나열** — "Let me trace 'ab' … then I'd test empty, one char, even and odd length."
6. **임베디드 한 마디 추가** — "On target I'd avoid strlen twice / avoid malloc / note this isn't ISR-safe."

- 막히면: "Let me first write the straightforward version, then I'll fix the edge cases." — 침묵보다 100배 낫다.
- 공통 엣지 체크: **NULL · 길이 0 · 길이 1 · 최대값/overflow · wrap-around · 겹치는 버퍼 · 부호(signed/unsigned)**.

## C1. strcpy / strlcpy / strlen / strcmp / memmove

- **한 줄**: 문자열 복사. 기본 strcpy는 NUL까지 복사하고 dst 반환. 꼬리 질문으로 **"안전한 버전?"**, **"memcpy vs memmove?"** 가 거의 반드시 온다.
- **예상 질문**: "Implement strcpy." → "What's wrong with strcpy?" → "Write a bounded version." → "Implement memmove."
- **함정**
  - NUL 복사 빠뜨림 → `while ((*d++ = *src++) != '\0')` 이 한 줄이 NUL까지 복사.
  - 반환값은 원래 `dst` (이미 증가한 포인터 반환 X).
  - `strncpy`는 **NUL 보장 안 함** + 남는 공간을 0으로 다 채움(느림) → strlcpy 의미가 더 안전.
  - strcmp는 **unsigned char로 비교** (`'\xff'` 가 음수가 되는 플랫폼).
  - memmove: dst > src 이고 겹치면 **뒤에서부터** 복사. 서로 다른 객체 포인터 비교는 UB라 `uintptr_t`로 비교.
- **테스트 말하기**: "rivian", "" (빈 문자열), 버퍼보다 긴 src(잘림 + NUL), size 0, memmove 양방향 겹침.

<details><summary>💡 정답 코드 — strcpy · strlcpy · strlen · strcmp</summary>

```c
char *my_strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != '\0')
        ;                       /* copies the NUL too */
    return dst;
}

/* Bounded copy (strlcpy semantics): always NUL-terminates when size > 0,
 * returns strlen(src) so the caller can detect truncation (ret >= size). */
size_t my_strlcpy(char *dst, const char *src, size_t size)
{
    size_t i = 0;
    if (size > 0) {
        for (; i + 1 < size && src[i] != '\0'; i++)
            dst[i] = src[i];
        dst[i] = '\0';
    }
    while (src[i] != '\0')
        i++;
    return i;
}

size_t my_strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

int my_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
```

</details>

<details><summary>💡 정답 코드 — memcpy · memmove (겹침 처리)</summary>

```c
void *my_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

/* Overlap-safe: copy backwards when dst is above src. */
void *my_memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0)
        return dst;
    if ((uintptr_t)d < (uintptr_t)s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}
```

- 확인값: `"123456789"`에서 `memmove(m+2, m, 5)` → `"121234589"`, `memmove(m, m+2, 5)` → `"345676789"`.

</details>

- **30초 영어**: "strcpy has no bound, so a long source overflows the destination — a classic security bug. I'd use a bounded copy that always NUL-terminates and returns the source length so the caller can detect truncation. memcpy assumes no overlap; memmove copies backwards when the destination is above the source."
- **Follow-up**: "word 단위로 빠르게?" → 정렬 맞춰 앞부분 byte, 중간은 `uint32_t`/`uint64_t` 단위, 꼬리 byte. 단 strict aliasing·unaligned access(Cortex-M0 fault) 주의 → 실제론 컴파일러 builtin/DMA 사용.

## C2. revstr (in-place 문자열 뒤집기) + 단어 순서 뒤집기

- **한 줄**: 두 포인터 l/r 를 양 끝에서 swap 하며 가운데로. 추가 메모리 O(1).
- **함정**
  - `""` 에서 `s + n - 1` 은 `s - 1` → **UB**. 그래서 `n < 2` 면 바로 return.
  - `char *s = "hello"; revstr(s);` → 문자열 리터럴은 read-only(.rodata) → **crash**. `char s[] = "hello";` 로 테스트.
  - 루프 조건 `l < r` (`!=` 쓰면 짝수 길이에서 서로 지나침).
- **테스트 말하기**: "hello"→"olleh", "ab"→"ba", "", "x", NULL.
- **꼬리**: "단어 순서 뒤집기" → **전체 뒤집기 → 단어마다 다시 뒤집기**. "hello big world" → "world big hello".

<details><summary>💡 정답 코드 — revstr · rev_words</summary>

```c
static void rev_range(char *l, char *r)
{
    while (l < r) {
        char t = *l;
        *l++ = *r;
        *r-- = t;
    }
}

void revstr(char *s)
{
    if (s == NULL)
        return;
    size_t n = strlen(s);
    if (n < 2)
        return;                 /* also avoids s + n - 1 underflow on "" */
    rev_range(s, s + n - 1);
}

/* "hello big world" -> "world big hello" (single spaces) */
void rev_words(char *s)
{
    size_t n = strlen(s);
    if (n == 0)
        return;
    rev_range(s, s + n - 1);    /* 1) reverse whole string */
    char *w = s;
    for (char *p = s;; p++) {   /* 2) reverse each word back */
        if (*p == ' ' || *p == '\0') {
            if (p > w)
                rev_range(w, p - 1);
            if (*p == '\0')
                break;
            w = p + 1;
        }
    }
}
```

</details>

- **30초 영어**: "Two pointers from both ends, swap and move inward until they meet. O(n) time, O(1) extra space. I guard empty and one-char strings so I never form a pointer before the buffer."

## C3. atoi (+ itoa, hex parse)

- **한 줄**: 공백 skip → 부호 1개 → 숫자가 아닌 글자에서 멈춤 → **overflow 시 INT_MAX/INT_MIN으로 clamp**.
- **clarify 할 것**: "Overflow behavior — clamp, or error? Leading whitespace? Hex?" (표준 atoi는 overflow가 UB, strtol은 clamp + errno=ERANGE)
- **핵심 overflow 검사**: `acc*10 + d > limit` 를 곱하기 전에 `acc > (limit - d) / 10` 로 검사 (곱해서 넘친 뒤엔 이미 늦음).
  - 음수는 한계가 1 더 크다: `limit = INT_MAX + 1` (= 2147483648) 을 unsigned로.
- **함정**: `-INT_MIN` 은 overflow(UB). `isspace(c)` 에 음수 char 넘기면 UB → `(unsigned char)` 캐스트.
- **테스트 말하기**: "42", "   -42", "+7", "4193 with words"→4193, "words 987"→0, "", "-", "2147483647", "2147483648"→INT_MAX, "-2147483648"→INT_MIN, "-91283472332"→INT_MIN.

<details><summary>💡 정답 코드 — my_atoi</summary>

```c
/* Skip leading whitespace, optional sign, digits until first non-digit,
 * clamp to INT_MAX / INT_MIN on overflow. */
int my_atoi(const char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    bool neg = false;
    if (*s == '+' || *s == '-')
        neg = (*s++ == '-');

    unsigned limit = neg ? (unsigned)INT_MAX + 1u : (unsigned)INT_MAX;
    unsigned acc = 0;
    while (*s >= '0' && *s <= '9') {
        unsigned d = (unsigned)(*s++ - '0');
        if (acc > (limit - d) / 10u)          /* acc*10 + d would exceed limit */
            return neg ? INT_MIN : INT_MAX;
        acc = acc * 10u + d;
    }
    if (neg)
        return acc == limit ? INT_MIN : -(int)acc;
    return (int)acc;
}
```

</details>

<details><summary>💡 정답 코드 — my_itoa (INT_MIN 처리) · parse_hex_u32</summary>

```c
/* buf must hold 12 bytes ("-2147483648" + NUL). Handles INT_MIN. */
char *my_itoa(int v, char *buf)
{
    char tmp[11];
    int i = 0;
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    do {
        tmp[i++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u != 0);
    char *p = buf;
    if (v < 0)
        *p++ = '-';
    while (i > 0)
        *p++ = tmp[--i];
    *p = '\0';
    return buf;
}

/* "0x1F" / "1f" -> 31. Returns false on empty / bad char / > 8 hex digits. */
bool parse_hex_u32(const char *s, uint32_t *out)
{
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    uint32_t v = 0;
    int nd = 0;
    for (; *s; s++, nd++) {
        unsigned d;
        if (*s >= '0' && *s <= '9')      d = (unsigned)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') d = (unsigned)(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') d = (unsigned)(*s - 'A' + 10);
        else return false;
        if (nd == 8)
            return false;                  /* would overflow 32 bits */
        v = (v << 4) | d;
    }
    if (nd == 0)
        return false;
    *out = v;
    return true;
}
```

- itoa 핵심: `do-while` (0 → "0"), 음수는 **unsigned로 바꿔서** 절댓값 (`0u - (unsigned)v` → INT_MIN도 안전), 뒤에서부터 채운 뒤 역순 복사.

</details>

- **30초 영어**: "I skip whitespace, read one optional sign, then accumulate digits. Before each multiply I check `acc > (limit - d) / 10` so I detect overflow before it happens, and clamp. The negative limit is one larger, so I accumulate in unsigned and only negate at the end."

## C4. 32-bit 레지스터 2개 → 64-bit 타이머 읽기 ★

- **한 줄**: HW 카운터가 `TMR_LO`/`TMR_HI` 두 32-bit 레지스터로 나뉘어 있을 때, **두 번 읽는 사이에 LO가 wrap** 되면 값이 찢어진다(torn read). → **hi → lo → hi 다시 읽고, hi가 같을 때까지 retry**.
- **숫자로 설명 (면접에서 이걸 말하면 강함)**
  - 카운터 = `0x1_FFFF_FFFF`. naive로 hi 먼저 읽음 → `hi = 1`. 그 사이 tick → `0x2_0000_0000`. lo 읽음 → `lo = 0`.
  - 결과 `0x1_0000_0000` → **실제보다 2^32 tick (≈ 4.29e9) 과거**. 1 GHz면 4.3초 뒤로 감 → 타임아웃·스케줄러 폭주.
  - 반대로 lo 먼저 → hi 순서면 **2^32 tick 미래**로 튄다. 순서만 바꿔선 해결 안 됨.
  - hi-lo-hi: `hi=1, lo=0, hi2=2` → 불일치 → retry → `hi=2, lo=3, hi2=2` → `0x2_0000_0003` ✔ (테스트로 확인한 값)
- **clarify 할 것**: "Does the HW latch HI when LO is read?" (많은 타이머가 LO 읽을 때 HI를 shadow에 latch → 그럼 LO→HI 순서 한 번이면 끝. 데이터시트 확인이 먼저.) "Is this 32-bit CPU?" (AArch64면 64-bit 단일 load로 끝 — ARM generic timer `CNTVCT_EL0`).
- **함정**
  - 레지스터 포인터에 **`volatile`** 빠뜨리면 컴파일러가 hi 두 번 읽기를 한 번으로 합침 → retry가 무의미.
  - `(uint64_t)hi << 32` — 캐스트 없이 `hi << 32` 는 32-bit shift **UB**.
  - 무한 루프? hi는 2^32 tick마다 한 번 바뀌므로 retry는 최대 1번.
- **SW 확장(HW에 HI 없음)**: 마지막 값보다 작아지면 wrap → high += 2^32. **wrap 주기 안에 최소 1번 호출** 보장 필요(주기 타이머 ISR), ISR과 공유하면 보호 필요.
- **wrap-safe 비교**: `elapsed = now - start` (unsigned 뺄셈은 wrap에서도 정답), 시간 비교는 `(int32_t)(b - a) < 0` (리눅스 `time_after`). `now > deadline` 직접 비교는 wrap에서 깨짐.

<details><summary>💡 정답 코드 — 실제 MMIO 버전 (면접에서 쓸 형태)</summary>

```c
#define REG32(addr)  (*(volatile uint32_t *)(uintptr_t)(addr))
#define TMR_LO       0x40001000u
#define TMR_HI       0x40001004u

uint64_t timer_read64(void)
{
    uint32_t hi, lo, hi2;
    do {
        hi  = REG32(TMR_HI);
        lo  = REG32(TMR_LO);
        hi2 = REG32(TMR_HI);
    } while (hi != hi2);        /* lo wrapped between the two hi reads -> retry */
    return ((uint64_t)hi << 32) | lo;
}
```

- `cram_all.c` 에서는 레지스터 대신 콜백 `rd_hi()/rd_lo()` 로 바꿔서, 읽을 때마다 카운터가 1 tick 증가하게 시뮬레이션해 위 숫자를 확인했다(로직 동일).

</details>

<details><summary>💡 정답 코드 — SW 64-bit 확장 · wrap-safe 비교</summary>

```c
/* No HW high word: extend a free-running 32-bit counter in SW.
 * Must be called at least once per wrap period (2^32 ticks). Not ISR-safe as is. */
typedef struct { uint32_t last; uint64_t high; } ext64_t;

uint64_t ext64_update(ext64_t *e, uint32_t now)
{
    if (now < e->last)
        e->high += 1ull << 32;
    e->last = now;
    return e->high | now;
}

/* Wrap-safe compare: true if a is after b (valid while |a-b| < 2^31). */
static inline bool time_after32(uint32_t a, uint32_t b) { return (int32_t)(b - a) < 0; }

/* elapsed across wrap: (uint32_t)(0x00000005u - 0xFFFFFFF0u) == 21 */
```

</details>

- **30초 영어**: "Reading two 32-bit halves isn't atomic: the low word can roll over between the reads, giving a value off by 2^32. I read high, low, high again and retry if the high word changed — at most one retry since high only changes every 2^32 ticks. First I'd check the datasheet: if reading LO latches HI, a single LO-then-HI read is enough. And the registers must be accessed through volatile pointers."
- **Follow-up**
  - "ISR에서도 부르면?" → 읽기 전용이라 그대로 안전(재진입 OK). SW 확장 버전은 `last/high` 갱신이 공유 상태 → IRQ off 구간 또는 ISR에서만 갱신.
  - "1 GHz 32-bit는 몇 초에 wrap?" → 2^32 / 1e9 ≈ **4.29초**. 64-bit는 ≈ 584년.

## C5. Ring buffer (원형 큐) ★

- **한 줄**: 고정 배열 + head(쓰기)/tail(읽기). **크기를 2의 거듭제곱**으로 → `% N` 대신 `& (N-1)`. **free-running index**(계속 증가, 쓸 때만 mask) → `count = head - tail`, 빈 칸 낭비 없이 full/empty 구분.
- **full/empty 판정 3가지 (물어보면 비교해서 말하기)**

| 방식 | empty | full | 장단점 |
|---|---|---|---|
| free-running index (아래 코드) | `head == tail` | `head - tail == N` | 칸 낭비 0, unsigned wrap 자연스러움. N이 2^k 이어야 함 |
| 한 칸 비우기 (wrapped index) | `head == tail` | `(head+1) % N == tail` | 가장 흔함, 용량 N-1 |
| count 변수 따로 | `count == 0` | `count == N` | 쉬움. 하지만 producer·consumer **둘 다 count를 씀** → ISR과 공유 시 lock 필요 |

- **ISR/동시성 (Staff면 여기서 갈림)**
  - SPSC(생산자 1·소비자 1)면 **lock 없이** 가능: head는 producer만, tail은 consumer만 쓴다.
  - 순서: producer는 **데이터 쓰기 → head 증가**, consumer는 **데이터 읽기 → tail 증가**. 순서가 뒤집히면 아직 안 쓴 칸을 읽음.
  - 단일 코어 MCU(ISR ↔ main): `volatile` + 32-bit aligned 쓰기는 atomic → OK (컴파일러 재배치 막으려면 compiler barrier).
  - **멀티코어/A-core**: `volatile`은 CPU 재정렬을 못 막음 → C11 `memory_order_release`(publish) / `acquire`(observe) 또는 `dmb`.
  - full일 때 정책: **drop newest(오버플로 카운트 증가)** vs overwrite oldest — 센서/로그 성격에 따라. "I'd count drops so validation can see them."
- **테스트 말하기**: empty에서 get 실패 · N개 put 후 N+1번째 실패 · FIFO 순서 · **index가 2^32 넘어 wrap** (head=tail=0xFFFFFFFC 에서 6개 넣고 빼기) · interleaved put/get.

<details><summary>💡 정답 코드 — 단일 코어 ISR ↔ main 버전</summary>

```c
#define RB_SIZE 8u                      /* power of 2 -> & instead of % */
_Static_assert((RB_SIZE & (RB_SIZE - 1)) == 0, "RB_SIZE must be a power of 2");

typedef struct {
    uint8_t buf[RB_SIZE];
    volatile uint32_t head;             /* written only by producer (e.g. RX ISR) */
    volatile uint32_t tail;             /* written only by consumer (main loop)   */
} ring_t;                               /* free-running indices: count = head - tail */

static inline uint32_t rb_count(const ring_t *r) { return r->head - r->tail; }

bool rb_put(ring_t *r, uint8_t v)
{
    if (rb_count(r) == RB_SIZE)
        return false;                   /* full: drop newest, caller counts overflow */
    r->buf[r->head & (RB_SIZE - 1)] = v;
    r->head++;                          /* publish AFTER the data write */
    return true;
}

bool rb_get(ring_t *r, uint8_t *out)
{
    if (r->head == r->tail)
        return false;                   /* empty */
    *out = r->buf[r->tail & (RB_SIZE - 1)];
    r->tail++;
    return true;
}
```

- 왜 free-running이 wrap에서도 맞나: 2^32 가 N(=2^k)의 배수라서, head가 0xFFFFFFFF→0 으로 넘어가도 `head & (N-1)` 슬롯 순서가 이어지고 `head - tail` 도 unsigned 뺄셈이라 정확.

</details>

<details><summary>💡 정답 코드 — 멀티코어 SPSC (C11 atomics)</summary>

```c
typedef struct {
    uint8_t buf[RB_SIZE];
    _Atomic uint32_t head, tail;
} aring_t;

bool arb_put(aring_t *r, uint8_t v)
{
    uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - t == RB_SIZE)
        return false;
    r->buf[h & (RB_SIZE - 1)] = v;
    atomic_store_explicit(&r->head, h + 1, memory_order_release);   /* data visible before head */
    return true;
}

bool arb_get(aring_t *r, uint8_t *out)
{
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (h == t)
        return false;
    *out = r->buf[t & (RB_SIZE - 1)];
    atomic_store_explicit(&r->tail, t + 1, memory_order_release);   /* slot free after read */
    return true;
}
```

</details>

- **30초 영어**: "Power-of-two size so I can mask instead of modulo. Head and tail are free-running counters; count is head minus tail, which stays correct across 32-bit wrap. Single producer, single consumer: only the ISR writes head, only main writes tail, so no lock — but the producer must write the data before publishing head. On a multi-core part I'd use release/acquire atomics, because volatile doesn't order memory between cores."
- **Follow-up**
  - "DMA가 producer면?" → HW가 쓰는 write pointer 레지스터를 읽고, **cache invalidate 후** 데이터 읽기 (non-coherent DMA). → [V3](hmScreenPrep/problems/V3_dma_desc_ring/index.html)
  - "여러 byte를 한 번에?" → 연속 구간 2개(끝까지 + 처음부터)로 나눠 memcpy.

## C6. Bit mask / field mask / register RMW ★★ (1순위)

- **한 줄**: 레지스터의 `[h:l]` 비트 필드를 **다른 비트는 건드리지 않고** 읽고/쓰기. 공식은 딱 3개.
  - mask: `GENMASK(h,l)` = h..l 비트가 1. 예 `GENMASK(7,4) = 0xF0`
  - get: `(reg & mask) >> shift`
  - set: `reg = (reg & ~mask) | ((val << shift) & mask)` — **clear 먼저, 그다음 OR**, val이 넓으면 mask로 잘라냄
- **비트 기본 4종**: set `x |= BIT(n)` · clear `x &= ~BIT(n)` · toggle `x ^= BIT(n)` · test `!!(x & BIT(n))`
- **손으로 trace (말로 연습)**: `reg = 0x12345678`, bits[15:8] = 0xAB 로 → mask `0x0000FF00` → `reg & ~mask = 0x12340078` → `| 0xAB00` → **`0x1234AB78`**. get bits[15:8] of 0x12345678 → **0x56**.
- **함정 (HW 면접관이 노리는 곳)**
  - `1 << 31` → int signed overflow **UB** → 항상 `1u`.
  - `1u << 32` → **UB** (width=32 필드에서 `(1u<<width)-1` 쓰면 터짐) → 분기 처리.
  - **W1C(write-1-to-clear) 상태 레지스터에 `|=` 금지**: RMW가 읽은 pending 1들을 다시 써서 **다른 인터럽트까지 다 지워버림**. 지울 비트만 쓰기: `STATUS = BIT(2)`.
  - **RC(read-to-clear)** 레지스터는 디버거/printf로 읽기만 해도 지워짐 → 한 번 읽어 변수에 저장.
  - RMW는 atomic이 아님 → ISR이 같은 레지스터를 만지면 IRQ off / set-clear 전용 레지스터(SET/CLR alias) / bit-banding.
  - `volatile` → 읽기·쓰기 횟수와 순서를 그대로 지키게 함. `reg_update` 는 **정확히 1 read + 1 write**.
  - 비트필드 struct(`uint32_t en:1;`)는 레지스터 매핑에 비추천: 비트 순서·접근 폭이 컴파일러 구현 정의.

<details><summary>💡 정답 코드 — 매크로 + shift/width 버전 + RMW + W1C</summary>

```c
#define BIT(n)            (1u << (n))
#define GENMASK(h, l)     (((~0u) >> (31u - (h))) & ((~0u) << (l)))
#define FIELD_GET(m, v)   (((uint32_t)(v) & (m)) >> __builtin_ctz(m))
#define FIELD_PREP(m, x)  (((uint32_t)(x) << __builtin_ctz(m)) & (m))

/* Portable (no builtin) shift/width form. width == 32 handled: 1u << 32 is UB. */
static inline uint32_t field_mask(unsigned shift, unsigned width)
{
    uint32_t m = (width >= 32u) ? ~0u : ((1u << width) - 1u);
    return m << shift;
}
static inline uint32_t get_field(uint32_t reg, unsigned shift, unsigned width)
{
    return (reg & field_mask(shift, width)) >> shift;
}
static inline uint32_t set_field(uint32_t reg, unsigned shift, unsigned width, uint32_t val)
{
    uint32_t m = field_mask(shift, width);
    return (reg & ~m) | ((val << shift) & m);
}

/* Read-modify-write a register field. Not atomic: an ISR touching the same reg needs a lock/irq-off. */
static inline void reg_update(volatile uint32_t *reg, uint32_t mask, uint32_t val)
{
    uint32_t v = *reg;                  /* exactly one read  */
    v = (v & ~mask) | (val & mask);
    *reg = v;                           /* exactly one write */
}

/* W1C status register: write ONLY the bit to clear. Never |= (that clears every pending bit). */
static inline void w1c_clear(volatile uint32_t *status, uint32_t bits) { *status = bits; }
```

- 확인값: `GENMASK(7,4)=0xF0`, `GENMASK(31,0)=0xFFFFFFFF`, `FIELD_GET(GENMASK(7,4), 0xA5)=0xA`, `FIELD_PREP(GENMASK(7,4), 0x13)=0x30`(넓은 값 잘림), `reg_update(0xFFFF0000, GENMASK(3,1), FIELD_PREP(GENMASK(3,1),5)) → 0xFFFF000A`.
- GENMASK 원리: `~0u >> (31-h)` = 0..h 가 1, `~0u << l` = l..31 이 1 → AND = l..h.

</details>

- **30초 영어**: "To update a field I clear it with the inverted mask, then OR in the shifted value masked to the field width, so an oversized value can't corrupt neighbors. I always use unsigned literals — `1 << 31` is undefined on int. For write-one-to-clear status registers I never read-modify-write; I write only the bit I want to clear, otherwise I'd acknowledge interrupts I haven't handled."
- **Follow-up**
  - "레지스터 맵을 C로 어떻게 표현?" → `typedef struct { volatile uint32_t CTRL; volatile uint32_t STATUS; uint32_t _rsvd[2]; volatile uint32_t DATA; } uart_regs_t;` + `_Static_assert(offsetof(uart_regs_t, DATA) == 0x10, "")` 로 오프셋 고정. `#define UART0 ((uart_regs_t *)0x40002000u)`.
  - "쓰고 나서 바로 효과를 보장?" → posted write라 버스에서 늦게 도착 가능 → 같은 디바이스 레지스터 **read-back** 또는 `dsb`.

## C7. Bit tricks (한 줄 공식)

| 문제 | 공식 | 예 |
|---|---|---|
| set bit 개수 (popcount) | `while (x) { x &= x - 1; n++; }` (가장 낮은 1 지우기) | 0xF0F0 → 8 |
| 2의 거듭제곱? | `x && !(x & (x - 1))` | 64 ✔, 96 ✘, 0 ✘ |
| 가장 낮은 1비트 | `x & (~x + 1)` (= `x & -x`) | 0x68 → 0x8 |
| a(2^k) 배수로 올림 정렬 | `(x + a - 1) & ~(a - 1)` | align_up(13, 8) = 16 |
| 다음 2의 거듭제곱 | `x--` → 오른쪽 shift OR(1·2·4·8·16)로 아래 비트 다 채움 → `x+1` | 5→8, 1000→1024 |
| 바이트 순서 뒤집기 | shift + mask 4조각 | 0x12345678 → 0x78563412 |
| 비트 순서 뒤집기 | 1·2·4비트 swap 후 bswap | 0x1 → 0x80000000 |
| 두 변수 swap (XOR) | `a ^= b; b ^= a; a ^= b;` | a, b 같은 주소면 0이 됨 → 실무엔 temp |

<details><summary>💡 정답 코드 — popcount · next_pow2 · bswap32 · reverse_bits32</summary>

```c
unsigned popcount32(uint32_t x)
{
    unsigned n = 0;
    while (x) {
        x &= x - 1;                     /* clear lowest set bit */
        n++;
    }
    return n;
}

static inline bool is_pow2(uint32_t x)      { return x != 0 && (x & (x - 1)) == 0; }
static inline uint32_t lowest_bit(uint32_t x) { return x & (~x + 1u); }        /* x & -x */
static inline uint32_t align_up(uint32_t x, uint32_t a) { return (x + a - 1u) & ~(a - 1u); }  /* a = pow2 */

uint32_t next_pow2(uint32_t x)          /* 0 -> 1, 5 -> 8, 8 -> 8 (x <= 2^31) */
{
    if (x <= 1)
        return 1;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

uint32_t bswap32(uint32_t x)
{
    return (x >> 24) | ((x >> 8) & 0x0000FF00u) | ((x << 8) & 0x00FF0000u) | (x << 24);
}

uint32_t reverse_bits32(uint32_t x)
{
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    return bswap32(x);
}
```

- popcount 복잡도: set bit 수 만큼만 반복. 실무: `__builtin_popcount` → ARM은 `CNT`(NEON)·x86 `POPCNT`.

</details>

## C8. Endianness / 바이트 패킹

- **한 줄**: little-endian = 낮은 주소에 **LSB**. `0x12345678` → 메모리 `78 56 34 12`. ARM Cortex·x86·RISC-V 대부분 little, **네트워크 바이트 순서는 big**.
- **판별**: `uint16_t one = 1; *(uint8_t *)&one == 1` → little.
- **패킷/레지스터 덤프에서 값 꺼내기**: `*(uint32_t *)&buf[3]` 캐스트 ✘ — **unaligned access fault**(Cortex-M0, 일부 DMA 버퍼) + strict aliasing 위반. **shift로 조립** ✔ — 호스트 엔디안과 무관하고 정렬 무관.

<details><summary>💡 정답 코드 — get_le32 · get_be32 · put_le32</summary>

```c
static inline bool is_little_endian(void)
{
    const uint16_t one = 1;
    return *(const uint8_t *)&one == 1;
}

/* Portable, alignment-safe, independent of host endianness. */
static inline uint32_t get_le32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static inline uint32_t get_be32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}
static inline void put_le32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}
```

- `(uint32_t)b[3] << 24` 캐스트 필수: `uint8_t`는 int로 승격 → `0x80 << 24` 는 signed overflow.

</details>

## C9. Linked list reverse (보너스)

- **한 줄**: prev/cur/next 3 포인터. `next 저장 → cur->next = prev → prev = cur → cur = next`. 반환은 prev.
- **테스트**: NULL, 노드 1개, 3개.

<details><summary>💡 정답 코드 — list_reverse</summary>

```c
typedef struct node { int val; struct node *next; } node_t;

node_t *list_reverse(node_t *head)
{
    node_t *prev = NULL;
    while (head) {
        node_t *next = head->next;
        head->next = prev;
        prev = head;
        head = next;
    }
    return prev;
}
```

</details>

## C10. aligned_malloc / aligned_free (보너스, 임베디드 단골)

- **한 줄**: DMA 버퍼처럼 64-byte(캐시 라인) 정렬이 필요할 때. `size + align - 1 + sizeof(void*)` 만큼 크게 받고, 정렬된 주소 **바로 앞 칸에 원래 포인터를 저장** → free 때 꺼내서 해제.
- **왜 캐시 라인 정렬?** non-coherent DMA에서 버퍼가 다른 변수와 캐시 라인을 공유하면 invalidate가 옆 변수를 날림.

<details><summary>💡 정답 코드 — aligned_malloc · aligned_free</summary>

```c
/* align must be a power of 2. Stores the original pointer just below the returned block. */
void *aligned_malloc(size_t size, size_t align)
{
    void *raw = malloc(size + align - 1 + sizeof(void *));
    if (raw == NULL)
        return NULL;
    uintptr_t p = ((uintptr_t)raw + sizeof(void *) + align - 1) & ~(uintptr_t)(align - 1);
    ((void **)p)[-1] = raw;
    return (void *)p;
}

void aligned_free(void *p)
{
    if (p)
        free(((void **)p)[-1]);
}
```

</details>

## 11. 개념 퀵파이어 (코드 없이 말로 — 각 15초)

| 질문 | 한국어 핵심 | 영어 한 줄 |
|---|---|---|
| `volatile` 이 뭐고 언제? | 컴파일러가 읽기/쓰기를 생략·합치기·재배치 못 하게. MMIO, ISR과 공유 변수, 다른 마스터(DMA)가 바꾸는 메모리 | "Every access really happens, in program order — for MMIO and ISR-shared flags. It's not atomic and not a memory barrier." |
| `volatile` 이 **아닌** 것 | atomic 아님, CPU 재정렬/캐시 일관성 보장 안 함, 멀티코어 동기화 X | "volatile doesn't make `x++` atomic." |
| `const volatile uint32_t *p` | SW는 못 쓰고 HW가 바꾸는 값 (read-only 상태 레지스터) | "Read-only to me, changed by hardware." |
| `const char *p` vs `char *const p` | 앞: 가리키는 **내용** 고정. 뒤: **포인터** 고정. (오른쪽에서 왼쪽으로 읽기) | "Read right to left." |
| `int (*fp)(int)` / `int *fp(int)` | 함수 포인터 vs int* 반환 함수 | |
| `struct {char a; int b; char c;}` sizeof? | 보통 **12** (a+3패딩, b, c+3패딩). 큰 것부터 정렬하면 8. `packed`는 unaligned access 비용 | "Members are aligned to their size; reorder largest-first to cut padding." |
| `uint8_t a=200, b=100; a+b` | int로 **승격** → 300 (uint8_t에 다시 넣으면 44) | "Integer promotion to int before arithmetic." |
| `if (-1 < sizeof(int))` | **false** — -1이 size_t(unsigned)로 변환돼 거대한 수 | "Signed is converted to unsigned in mixed comparisons." |
| `for (unsigned i = 10; i >= 0; i--)` | 무한 루프 (unsigned는 항상 ≥ 0) | |
| `#define SQ(x) x*x` → `SQ(a+1)` | `a+1*a+1`. 괄호 `((x)*(x))` — 그래도 `SQ(i++)` 는 두 번 증가 → `static inline` 함수 | "Prefer static inline over macros." |
| `static` 3가지 | 함수 안: 값 유지(.bss/.data) · 파일 전역: internal linkage · 함수: 파일 밖 숨김 | |
| ISR 작성 규칙 | 짧게, 블로킹·printf·malloc 금지, 플래그/링버퍼로 넘기고 main에서 처리, W1C로 원인 clear, 공유 변수 volatile | "Do the minimum, defer the rest, clear the source." |
| 스택 vs 힙 vs .data/.bss | 지역=스택, malloc=힙(임베디드는 보통 금지/풀), 초기화 전역=.data(flash→RAM 복사), 0 전역=.bss(startup이 0) | |
| 왜 임베디드에서 malloc 기피? | 단편화, 비결정적 시간, 실패 처리 → 정적 할당/메모리 풀 | "Fragmentation and non-deterministic timing." |
| `i++ + i++` | UB (sequence point 없음) | |
| 문자열 리터럴 수정 | UB, .rodata → fault | |

## 마지막 10분 체크리스트 (금요일 아침)

- [ ] GENMASK / set_field 공식 종이에 1번 쓰기 — `(reg & ~m) | ((v << s) & m)`
- [ ] W1C에 `|=` 금지 — 이유 한 문장
- [ ] 64-bit timer: hi-lo-hi + "2^32 tick 과거로 튐" 숫자 예시
- [ ] ring buffer: `head - tail` count, 2^k 크기, 데이터 쓰고 head 증가
- [ ] atoi overflow 검사식 `acc > (limit - d) / 10`
- [ ] strcpy 한 줄 `while ((*d++ = *s++))`, memmove 뒤에서부터
- [ ] revstr: `n < 2` return, 리터럴 수정 금지
- [ ] `1u`, `(uint64_t)` 캐스트, `volatile` — 세 단어를 코드에서 먼저 쓰기
- [ ] 코딩 전 clarify 1문장 · 코딩 후 테스트 케이스 3개 소리 내기
