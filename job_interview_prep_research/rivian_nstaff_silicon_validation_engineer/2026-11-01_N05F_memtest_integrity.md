# N05F · 메모리 테스트 · 데이터 무결성
<!-- theme: pastel -->
> **묶음**: F 메모리 테스트 · CRC · PRBS · ECC · **문제 수**: 8 (F1–F8) · **목표**: bring-up 첫날 돌리는 bus 테스트와 무결성 코드(checksum/CRC/PRBS/Hamming)를 종이에 20분 안에 쓰고, 고장 결과를 보고 "어느 선이 나갔는지" 말하기 · **선행**: [N05 목록](2026-11-01_N05_c_paper_drill_bank.html) · **이전/다음**: [N05E 링버퍼·큐·ISR](2026-11-01_N05E_ring_queue_isr.html) / [N05G 상태 머신·파서](2026-11-01_N05G_state_machines_parsers.html) · **코드**: onsitePrep/n05/f/ (`./run.sh` → F1–F8 PASS)

## 0. TL;DR · 사용법

- 이 페이지가 훈련하는 것: **고장 난 메모리를 흉내 낸 fake**(stuck-at 비트, data선 short, 주소선 aliasing)에서 내 테스트가 **각 고장을 정말 잡는지** 증명하는 습관 + CRC/PRBS/ECC를 손으로 1스텝씩 돌리는 것. 모든 모범 답은 `onsitePrep/n05/f/fake_mem.h`의 고장 주입 fake로 검증했다.
- 이미 한 것과 겹치지 않게: March C-·첫 실패에서 멈추는 bus 테스트는 [V1](hmScreenPrep/problems/V1_march_memtest/index.html), 알고리즘 배경·DDR 맥락은 [BV09 §2.6](2026-11-01_BV09_ddr_bringup_training_memtest.html), PRBS7/31 load-then-free-run 체커·CRC-32는 [V6](hmScreenPrep/problems/V6_prbs_crc_checker/index.html). 여기선 **고장 마스크를 전부 모으는 버전**, **feed-through self-sync 체커**, **CRC-8 손계산**, **결과 해석(F8)**.
- 외울 한 줄 3개:
  - **"Data bus first, then address bus, then cells — each test assumes the previous one passed."** (F1 → F2 → F3/March)
  - **"Power-of-two offsets toggle one address line each, so log N writes cover every line."** (F2)
  - **"CRC is polynomial division mod 2: if the bit falling off the top is 1, XOR the poly."** (F5)
- ★★★ 온사이트 확률 높음: **F2** (address bus). 그다음 ★★: F1 · F5 · F7 · F8 (20분 문제로 딱 맞음).
- 순서 추천: F1 → F2 → F3 (한 세트로 45분, 모의 면접 "write a memory test") → F5 → F7 → F4 → F6 → F8.
- 체크포인트: CRC-8/SMBUS(poly 0x07, init 0) `"123456789"` → **0xF4**, PRBS7 = x⁷+x⁶+1 주기 **127**, Hamming(7,4) data 1011 → **0110011** — 전부 공개 레퍼런스와 대조 + 코드로 확인.

## 1. 문제지 (인쇄용 — 답 없음)

Memory problems (F1–F3, F8) use two provided accessors: `void mem_wr(uint32_t off, uint32_t v)` and `uint32_t mem_rd(uint32_t off)`, where `off` is a **word offset** from the base of the region under test.

### F1 · Data bus walking-1 test

Walk a single 1 across all 32 data lines at one word offset. Report every line that failed as two masks: lines that read 0 while driven 1, and lines that read 1 while not driven.

```c
bool databus_walk1(uint32_t off, uint32_t *missing, uint32_t *extra);
```

Example: data lines D3 and D4 shorted (wired-OR) → returns false, `missing = 0`, `extra = 0x00000018`.

### F2 · Address bus test (power-of-two offsets)

Region of `nwords` words (power of two, base aligned). Using patterns `0xAAAAAAAA` / `0x55555555` at power-of-two offsets, find stuck-high, stuck-low and shorted address lines. Return a mask of every suspect word-address line (bit k = line k), 0 = pass.

```c
uint32_t addrbus_test(uint32_t nwords);
```

Example: 256 words, address line A3 stuck-low (offset 8 lands on offset 0) → `0x08`.

### F3 · Address-in-address fill + verify + inverted pass

Fill every word with its own bus address (`0x80000000 + 4*i`), verify, write the inverse, verify again. Return the number of failing reads and the first failing word offset (`UINT32_MAX` if none).

```c
uint32_t own_addr_test(uint32_t nwords, uint32_t *first_bad);
```

Example: word 5 bit 31 stuck-at-1 → returns 1, `first_bad = 5` (only the inverted pass sees it).

### F4 · Three checksums

```c
uint8_t  sum8(const uint8_t *p, size_t n);          /* sum mod 256 */
uint8_t  cksum8_twos(const uint8_t *p, size_t n);   /* byte that makes the total sum 0 */
uint16_t fletcher16(const uint8_t *p, size_t n);    /* (sum2 << 8) | sum1, mod 255 */
```

Example: `fletcher16("abcde", 5)` → `0xC8F0`; Intel HEX bytes `03 00 30 00 02 33 7A` → `cksum8_twos` = `0x1E`.

### F5 · CRC-8, polynomial 0x07, bitwise

CRC-8 with poly x⁸+x²+x+1 (0x07), init 0x00, MSB-first, no reflection, no final XOR. Then compute the CRC of the single byte `0x31` by hand.

```c
uint8_t crc8_07(const uint8_t *p, size_t n);
```

Example: `crc8_07("123456789", 9)` → `0xF4`; `crc8_07({0x01}, 1)` → `0x07`.

### F6 · PRBS7 generator + self-synchronizing checker

PRBS7 = x⁷+x⁶+1, Fibonacci LFSR (state = last 7 bits, bit 0 newest). Write the one-bit generator, and a checker that predicts each received bit from the previous 7 **received** bits and counts mismatches after the first 7 bits.

```c
unsigned prbs7_next(uint8_t *s);
typedef struct { uint8_t sr; unsigned fill; uint32_t errs, checked; } Prbs7Chk;
void prbs7_chk_bit(Prbs7Chk *c, unsigned rx);
```

Example: seed `0x7F` → period 127 bits; first 16 bits MSB-first = `0x020C`. One flipped bit in a clean stream → `errs = 3`.

### F7 · Hamming(7,4) encode / single-error-correct decode

Positions 1..7 = `p1 p2 d1 p4 d2 d3 d4`, codeword bit (pos-1) holds position pos, nibble bit 0 = d1. Decode returns 0 (clean) or 1 (one bit corrected).

```c
uint8_t ham74_encode(uint8_t d);
int     ham74_decode(uint8_t cw, uint8_t *d);
```

Example: data d1..d4 = 1,0,1,1 (nibble `0xD`) → codeword `0110011` in position order = `0x66`; `ham74_decode(0x76)` → 1, `d = 0xD`.

### F8 · Failure list → which bit / which address line

Given a list of `{addr, exp, act}` failures from an own-address test (true pass + inverted pass), build summary masks (OR/AND of `exp^act`, bits that rose, bits that fell, AND/OR of failing addresses) and classify: pass, one bad cell, data line(s) reading high, data line(s) reading low, address line(s), other.

```c
typedef struct { uint32_t addr, exp, act; } MemFail;
typedef struct { uint32_t n, x_or, x_and, rose, fell, a_and, a_or; } FailSum;
typedef enum { DX_PASS, DX_ONE_CELL, DX_DATA_HI, DX_DATA_LO, DX_ADDR, DX_OTHER } Dx;
void fail_summarize(const MemFail *f, size_t n, FailSum *s);
Dx   fail_diagnose(const FailSum *s, uint32_t *which);
```

Example: failures `{0x10, 0x00000000, 0x00000010}`, `{0x24, 0xAAAAAAAA, 0xAAAAAABA}`, `{0x3C, 0x12345600, 0x12345610}` → data line D4 reads high, `which = 0x10`.

## F1 · data bus walking-1 테스트 (★★ · 15분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 한 주소에 `1`을 비트 0부터 31까지 하나씩 옮겨 쓰고 읽어서, 고장 난 **데이터선**을 마스크 두 개로 전부 보고한다.
- **상황**: 보드 bring-up 첫날, SRAM/DDR이 "가끔 이상한 값"을 준다. 셀을 의심하기 전에 SoC ↔ 메모리 사이 **데이터 배선**(납땜 불량, 핀 short, open)부터 확인한다. 이 테스트가 깨지면 그 뒤 모든 테스트 결과가 쓰레기다.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `databus_walk1(off, &missing, &extra)` | word offset `off` 하나에 32개 패턴을 쓰고 읽기 | `missing` = 1을 썼는데 0으로 읽힌 선 · `extra` = 안 썼는데 1로 읽힌 선 · 둘 다 0이면 true |

- **예시를 말로 풀면**: D3와 D4가 붙어 있으면(wired-OR) bit 3만 써도 bit 3·4가 둘 다 1로 읽히고, bit 4만 써도 둘 다 1로 읽힌다 → `extra = 0x18`, `missing = 0`.
- **지켜야 할 조건**: 첫 실패에서 멈추지 말고 **모든 선**을 모을 것 (V1은 첫 실패에서 멈춤). 접근은 정확히 32 write + 32 read.

### 1. Problem (English)

You are bringing up external SRAM on a new board. Before testing cells, verify the 32-bit data bus. Write a walking-ones test at a single word offset that reports **every** failing data line, split into "missing" (driven 1, read 0) and "extra" (not driven, read 1).

```c
/* provided */
void     mem_wr(uint32_t off, uint32_t v);
uint32_t mem_rd(uint32_t off);

bool databus_walk1(uint32_t off, uint32_t *missing, uint32_t *extra);
```

- Clean bus → `true`, `missing = 0`, `extra = 0`.
- D5 stuck-high → `false`, `missing = 0`, `extra = 0x00000020`.
- D3–D4 wired-AND short → `false`, `missing = 0x00000018`, `extra = 0`.

### 2. 먼저 물어볼 것 (clarify)

- "Is the bus 32 bits to a single device, or split across byte lanes on different chips?" → 32-bit, one device. (여러 칩이면 칩마다 한 주소씩.)
- "Should I stop at the first failure or collect all failing lines?" → collect all.
- "Is the location I'm testing free — not my stack or code?" → yes, scratch word.
- "Are caches off for this region?" → yes, accessor = uncached path.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (폭, 전부 모을지, 캐시) |
| 2–5 | 예시: D3–D4 OR-short를 패턴 0x08, 0x10으로 손으로 |
| 5–15 | 코드 (루프 하나, 마스크 두 개) |
| 15–18 | trace: i = 3, 4, 5 |
| 18–20 | 테스트 + 확장 (walking-0, 칩 없음) |

- 소리 내서: **"n lines need only n patterns, not 2^n — each pattern proves one line can go high on its own while all others stay low."**
- 소리 내서: **"`pat & ~r` is what I drove but lost, `r & ~pat` is what showed up uninvited — that split already tells stuck-low from stuck-high."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

패턴 i = `1u << i`. 읽은 값 `r`과 비교할 때 `r != pat` 하나로 끝내지 말고, 어느 비트가 어느 방향으로 틀렸는지 나눠서 OR로 누적한다.

</details>

<details><summary>힌트 2</summary>

`miss |= pat & ~r;` `ext |= r & ~pat;` — 루프가 끝난 뒤 한 번에 출력 포인터에 쓴다. `1 << 31`(int)은 UB이므로 `1u`.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stdbool.h>
/* provided: void mem_wr(uint32_t off, uint32_t v);  uint32_t mem_rd(uint32_t off); */

/* Walk a single 1 across all 32 data lines at ONE word offset.
 * *missing: lines that read 0 while driven 1   (stuck-low, wired-AND short)
 * *extra  : lines that read 1 while not driven (stuck-high, wired-OR short)
 * Returns true if the data bus looks clean. */
bool databus_walk1(uint32_t off, uint32_t *missing, uint32_t *extra)
{
    uint32_t miss = 0, ext = 0;

    for (unsigned i = 0; i < 32; i++) {
        uint32_t pat = 1u << i;          /* 1u: 1 << 31 on int is UB */
        mem_wr(off, pat);
        uint32_t r = mem_rd(off);
        miss |= pat & ~r;                /* driven, did not come back */
        ext  |= r & ~pat;                /* came back, was not driven */
    }
    *missing = miss;
    *extra   = ext;
    return (miss | ext) == 0;
}
```

- 시간 O(W) (W = 버스 폭 32) · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

고장: D3–D4 wired-OR short.

| i | pat | 읽은 r | pat & ~r | r & ~pat | miss | ext |
|---|---|---|---|---|---|---|
| 2 | 0x04 | 0x04 | 0 | 0 | 0 | 0 |
| 3 | 0x08 | 0x18 | 0 | 0x10 | 0 | 0x10 |
| 4 | 0x10 | 0x18 | 0 | 0x08 | 0 | 0x18 |
| 5 | 0x20 | 0x20 | 0 | 0 | 0 | 0x18 |

결과: false, missing 0, extra 0x18.

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상 버스 → true, 0/0, 정확히 32 write + 32 read (검증함).
- 경계 비트: D31 stuck-low → `missing = 0x80000000` (1u 안 쓰면 여기서 UB), D0 stuck-low → `missing = 1`.
- D5 stuck-high → `extra = 0x20` · D3–D4 OR-short → `extra = 0x18` · D3–D4 AND-short → `missing = 0x18`.
- **주소선 고장(A4 stuck-low)은 통과** — 한 주소만 쓰니까 주소선이 안 움직인다. (F2가 필요한 이유, 검증함.)
- 다른 word의 셀 고장은 통과, 그 word를 테스트하면 실패 — 셀 고장과 버스 고장을 구분하려면 주소 2–3곳에서 돌린다.

</details>

### 4. 꼬리 질문

**Q1. Your test passed but there is no memory chip on the board. How?**

<details><summary>답</summary>

"Write-then-immediately-read can pass on a floating bus because the trace capacitance holds the value I just drove for a moment. To catch that, I write the pattern to one address, write its inverse to a different address, then read the first one — now the bus was driven to something else in between. That's also why the data bus test only proves wiring, and the device test proves the chip is there."
- 한국어 핵심: **버스 커패시턴스가 방금 쓴 값을 붙잡음 → 다른 주소에 반전값을 끼워 쓰고 읽기.**

</details>

**Q2. Why also run walking zeros?**

<details><summary>답</summary>

"Walking ones drives one line high against 31 low lines, which is good at finding lines that can't go high or that pull a neighbor up. Walking zeros, `~(1u << i)`, drives one line low against 31 high ones — the mirror case — so a weak driver or a wired-AND short that only shows when most lines are high is easier to see. It's another 32 accesses, so it's cheap."
- 한국어 핵심: **walking-0 = 반대 극성, 32번 더 — 싸니까 같이 돌린다.**

</details>

**Q3. The bus is 64 bits wide and split into four x16 DRAM chips. What changes?**

<details><summary>답</summary>

"Each chip owns its own 16 data lines, but one 64-bit access already covers all four lanes, so one address is enough if I use a 64-bit word and `1ull << i`. If the controller has separate chip selects or ranks, I repeat the test once per rank, because each rank has its own wiring. I'd report failing lines as lane plus bit so the board engineer can find the pin."
- 한국어 핵심: **64-bit 패턴(`1ull`) + rank마다 한 번 + "lane/bit"로 보고.**

</details>

### 5. 채점 · 자주 틀리는 것

- `1u << i` (`1 << 31`은 int overflow → UB).
- `r != pat`에서 바로 return → 고장 선이 여러 개면 첫 개만 보고됨.
- `missing`/`extra` 방향을 헷갈림: `pat & ~r` = 잃은 것, `r & ~pat` = 생긴 것.
- 테스트 대상 주소가 스택·코드 위가 아닌지 말하기.
- "주소선 고장은 이 테스트로 못 잡는다"를 먼저 말하면 가산점.

## F2 · address bus 테스트 — power-of-two offset (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: offset 1, 2, 4, 8, …에만 패턴을 써서 **주소선** 하나하나가 stuck-high / stuck-low / 다른 선과 short인지 찾고, 의심되는 선을 마스크로 전부 돌려준다.
- **상황**: 주소선이 하나 끊기거나 붙으면 서로 다른 두 주소가 **같은 셀**을 가리킨다(aliasing). 데이터는 "써지긴 하는데" 엉뚱한 곳을 덮어써서, 부팅 후 랜덤 크래시로 나타난다. 전체를 다 쓰지 않고 **log₂N번** 접근으로 찾는 게 핵심.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `addrbus_test(nwords)` | power-of-two offset에서 3단계 검사 | 패턴 `0xAAAAAAAA` / 반전 `0x55555555` · 반환 bit k = 주소선 k 의심 · 0 = 통과 |

- **예시를 말로 풀면**: 256 word, A3이 0에 붙어 있으면(stuck-low) offset 8에 쓴 값이 offset 0에 써진다. 그래서 offset 0을 반전값으로 바꾸면 offset 8에서도 반전값이 읽힌다 → `0x08`.
- **지켜야 할 조건**: `nwords`는 2의 거듭제곱, base는 `nwords`에 정렬. 데이터 버스는 이미 통과했다고 가정(아니면 모든 선이 실패로 보임). 첫 실패에서 멈추지 말고 마스크 누적.

### 1. Problem (English)

Data bus passed. Now test the address lines of a region of `nwords` 32-bit words (power of two, base aligned to `nwords`). Use the classic power-of-two-offset algorithm and return a mask of all suspect word-address lines.

```c
#define PAT  0xAAAAAAAAu
#define ANTI 0x55555555u
/* provided: mem_wr(off, v), mem_rd(off), off = word offset */
uint32_t addrbus_test(uint32_t nwords);   /* bit k set = address line k suspect; 0 = pass */
```

- 256 words, clean → `0`.
- 256 words, A3 stuck-high → `0x08`.
- 256 words, A2 and A3 shorted (wired-OR: offsets 4 and 8 both land on 12) → `0x0C`.

### 2. 먼저 물어볼 것 (clarify)

- "Are the offsets word offsets or byte offsets?" → word offsets; the two lowest byte-address bits are byte lanes, not tested here.
- "Can I assume the data bus test already passed?" → yes.
- "Is the region size a power of two and the base aligned to it?" → yes (otherwise high lines are fixed and untestable).
- "Report the first failure or all suspect lines?" → all, as a mask.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (word offset, 정렬, data bus 통과 가정) |
| 2–5 | 3단계를 말로: fill → base만 반전 → offset 하나씩 반전 |
| 5–15 | 코드 (루프 4개, `off <<= 1`) |
| 15–18 | trace: A3 stuck-low |
| 18–20 | 테스트 (stuck-high, stuck-low, short, 범위 밖 선) + 확장 |

- 소리 내서: **"An address line fault makes two addresses that differ only in that bit hit the same cell. Offsets 1, 2, 4, 8 each flip exactly one line, so I only need log N locations."**
- 소리 내서: **"Step 2 writes the base and checks nobody else changed — that's a line stuck so the offset lands on the base. Step 3 writes each offset and checks the base and every other offset — that catches stuck-low and shorts between two lines."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

offset 루프는 `for (off = 1; off < nwords; off <<= 1)`. offset 값 자체가 `1 << k`이므로 실패 시 그냥 `bad |= off` 하면 "선 k" 비트가 된다.

</details>

<details><summary>힌트 2</summary>

3단계: offset `t`에 ANTI → base 읽기 → 다른 모든 offset(`off != t`) 읽기 → `t`를 PAT로 복구. 두 offset끼리 겹치면 `bad |= t | off` (둘 중 어느 선인지 모르니 둘 다).

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
/* provided: void mem_wr(uint32_t off, uint32_t v);  uint32_t mem_rd(uint32_t off); */
#define PAT  0xAAAAAAAAu
#define ANTI 0x55555555u

/* nwords: power of two, region base aligned to nwords (offsets are word offsets).
 * Returns a mask of suspect address lines: bit k set = word-address line k. 0 = pass.
 * Run the data bus test first: a bad data line makes every line look bad here. */
uint32_t addrbus_test(uint32_t nwords)
{
    uint32_t bad = 0;

    for (uint32_t off = 1; off < nwords; off <<= 1)      /* 1) PAT at every 2^k */
        mem_wr(off, PAT);

    mem_wr(0, ANTI);                                     /* 2) stuck-high check */
    for (uint32_t off = 1; off < nwords; off <<= 1)
        if (mem_rd(off) != PAT) bad |= off;              /*    2^k aliases base */
    mem_wr(0, PAT);

    for (uint32_t t = 1; t < nwords; t <<= 1) {          /* 3) stuck-low / short */
        mem_wr(t, ANTI);
        if (mem_rd(0) != PAT) bad |= t;                  /*    t aliases base   */
        for (uint32_t off = 1; off < nwords; off <<= 1)
            if (off != t && mem_rd(off) != PAT)
                bad |= t | off;                          /*    t aliases off    */
        mem_wr(t, PAT);
    }
    return bad;
}
```

- 시간 O(log² N) 접근 (3단계가 log N × log N) · 공간 O(1). 256 word면 write 26번, read 72번 (테스트에서 카운트 검증).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

고장: 256 word, A3 stuck-low → offset 8은 실제로 셀 0.

| 단계 | 동작 | 셀 0 | 셀 8 이하 다른 offset | 판정 |
|---|---|---|---|---|
| 1 | off 1,2,4,8,…,128에 PAT (8 → 셀 0) | PAT | PAT | — |
| 2 | 셀 0에 ANTI, off 8 읽기 → 셀 0 | ANTI | PAT | `bad = 0x08` |
| 2 끝 | 셀 0 = PAT 복구 | PAT | PAT | — |
| 3, t = 8 | off 8에 ANTI (→ 셀 0), base 읽기 | ANTI | PAT | `bad = 0x08` (중복) |

결과 `0x08`. 참고: base가 0에 정렬돼 있으면 stuck-**low**도 2단계에서 먼저 걸린다. 3단계가 꼭 필요한 건 **A2–A3 OR-short**처럼 base와는 안 겹치고 offset 4·8끼리만 셀 12에서 겹치는 경우 (테스트에서 `0x0C`, 2단계는 통과).

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상 → 0, 접근 수가 O(log N) (write 26 / read 72).
- 경계 선: A0 stuck-low → `0x01`, A7 stuck-high (256 word의 최상위 선) → `0x80`.
- short: A2–A3 wired-OR → `0x0C`, A5–A6 wired-AND (32·64 → 0) → `0x60`.
- 범위 밖 선: A7 stuck-high인데 128 word만 테스트 → 0 (그 선은 이 범위에서 안 움직인다).
- 데이터선 고장(D1 stuck-low)이면 → `0xFF` 전부 실패 — "data bus first"의 이유.
- `nwords = 1` → 선이 없음, 0 반환, base만 2번 씀.

</details>

### 4. 꼬리 질문

**Q1. Why 0xAA / 0x55 and not 0 / 1?**

<details><summary>답</summary>

"I want the pattern and anti-pattern to differ in every bit, so any alias flips all 32 bits and can't be confused with a single stuck data bit. 0xAA and 0x55 also alternate neighboring lines, so one value has every adjacent pair at opposite levels. Zero is also a bad pattern because erased or unpowered memory often reads zero."
- 한국어 핵심: **모든 비트가 서로 반대 + 이웃 선이 교대 + 0은 "빈 메모리"와 헷갈림.**

</details>

**Q2. This is DDR behind a memory controller. Does "address line 3" still mean pin A3?**

<details><summary>답</summary>

"Not directly. The controller maps the CPU address into bank, row and column bits, and often hashes or interleaves bits across channels. So a failing CPU address bit has to be translated through the controller's address map to find the real DRAM pin — I'd report the CPU bit plus the decoded bank, row and column. Also, DDR address pins are multiplexed between row and column, so one pin fault can show up as two CPU bits."
- 한국어 핵심: **CPU 주소 비트 → controller 매핑 → bank/row/col. row/col 다중화라 핀 하나가 비트 두 개로 보일 수 있다.**

</details>

**Q3. Which address lines can this test never check?**

<details><summary>답</summary>

"Lines above the region size, because they stay constant inside the region — I need a bigger region or a second region with that bit set. And the low byte-lane bits, because the test does word accesses. That's why base alignment matters: the tested bits of the base must be zero."
- 한국어 핵심: **범위보다 높은 선 · 바이트 레인 하위 비트는 못 봄 → base 정렬 + 영역 크기 선택.**

</details>

### 5. 채점 · 자주 틀리는 것

- 1단계에서 base(offset 0)에 PAT를 쓰지 않음 — Barr 원본도 power-of-two offset만 쓰고, base는 2단계에서 ANTI.
- 2단계 끝에 base를 PAT로 **복구 안 함** → 3단계의 base 읽기가 전부 실패.
- 3단계 안쪽 루프에서 `off != t` 빠뜨림 → 방금 ANTI를 쓴 칸을 실패로 셈.
- 3단계 끝에 `t` 복구 안 함 → 다음 t에서 오탐.
- 첫 실패에서 return → 한 번에 한 선만 보고 (모르면 "I'd collect all, here's why").
- 루프 변수 `uint32_t`, `off <<= 1` — `nwords`가 2³¹ 이상이면 오버플로를 말로 언급.

## F3 · address-in-address fill + verify + 반전 패스 (★ · 12분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 모든 word에 **자기 버스 주소**를 써 넣고 확인, 다시 **반전값**을 써 넣고 확인 — 실패 read 수와 첫 실패 위치를 돌려준다.
- **상황**: F1·F2는 배선만 본다. 이건 영역 **전체**를 한 번씩 훑는 가장 싼 "device test": 칩이 정말 있는지, aliasing이 없는지, 모든 비트가 0과 1을 한 번씩 보관하는지.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `own_addr_test(nwords, &first_bad)` | 3패스: fill → verify+반전 쓰기 → 반전 verify | 기대값 `0x80000000 + 4*i`, 반전 패스는 `~` · 반환 = 실패 read 수 · 실패 없으면 `first_bad = UINT32_MAX` |

- **예시를 말로 풀면**: word 5의 bit 31이 1에 붙어 있다. word 5의 주소 `0x80000014`는 bit 31이 원래 1이라서 첫 확인은 통과한다. 반전값 `0x7FFFFFEB`를 쓰면 bit 31이 0이어야 하는데 1로 읽혀 실패 → 반환 1, `first_bad = 5`.
- **지켜야 할 조건**: 정확히 3N 접근 (N write, N read+N write, N read). `nwords = 0`이면 아무것도 안 건드림.

### 1. Problem (English)

Write a whole-region device test: write each word's own bus address, verify, write the bitwise inverse, verify again. Count failing reads and report the first failing offset.

```c
#define MEM_BASE 0x80000000u    /* bus address of word offset 0 */
uint32_t own_addr_test(uint32_t nwords, uint32_t *first_bad);
```

- Clean, 256 words → 0, `first_bad = UINT32_MAX`, 512 writes + 512 reads.
- Word 5 bit 31 stuck-at-0 → 1, `first_bad = 5` (true-data pass).
- 64 words, A5 stuck-low → 96, `first_bad = 0`.

### 2. 먼저 물어볼 것 (clarify)

- "Is the data the full bus address or the word index?" → bus address, so `exp ^ act` maps straight to address bits.
- "Count failing words or failing reads?" → failing reads (a word can fail in both passes).
- "Can I destroy the contents?" → yes, destructive test.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify |
| 2–4 | 왜 반전 패스가 필요한지 예시로 (bit 31 stuck-at-1) |
| 4–10 | 코드 |
| 10–12 | trace + 테스트 |

- 소리 내서: **"Every word gets a unique value, so any alias shows up as reading someone else's address. The inverted pass makes sure every bit has held both a 0 and a 1."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

패스 2와 3은 같은 루프 모양: 기대값만 `exp` / `~exp`. `for (pass = 0; pass < 2; pass++)` 하나로 합치고, pass 0일 때만 확인 후 반전값을 쓴다.

</details>

<details><summary>힌트 2</summary>

`if (errs++ == 0) *first_bad = i;` — 첫 실패 기록을 한 줄로. `4u * i`는 unsigned.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
/* provided: void mem_wr(uint32_t off, uint32_t v);  uint32_t mem_rd(uint32_t off); */
#define MEM_BASE 0x80000000u            /* bus address of word offset 0 */

/* Each word holds its own bus address, then its inverse.
 * Returns the number of failing reads; *first_bad = word offset of the first one
 * (UINT32_MAX if none). 3N accesses: N writes, N read+writes, N reads. */
uint32_t own_addr_test(uint32_t nwords, uint32_t *first_bad)
{
    uint32_t errs = 0;
    *first_bad = UINT32_MAX;

    for (uint32_t i = 0; i < nwords; i++)            /* pass 1: fill with own address */
        mem_wr(i, MEM_BASE + 4u * i);

    for (int pass = 0; pass < 2; pass++) {           /* pass 2: verify + invert */
        for (uint32_t i = 0; i < nwords; i++) {      /* pass 3: verify inverse  */
            uint32_t exp = MEM_BASE + 4u * i;
            if (pass == 1) exp = ~exp;
            if (mem_rd(i) != exp) {
                if (errs++ == 0) *first_bad = i;
            }
            if (pass == 0) mem_wr(i, ~exp);
        }
    }
    return errs;
}
```

- 시간 O(N) (정확히 3N 접근) · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

고장: word 5 bit 31 stuck-at-1.

| 패스 | i | 쓴 값 / 기대값 | 읽은 값 | errs |
|---|---|---|---|---|
| 1 (fill) | 5 | 0x80000014 쓰기 | — | 0 |
| 2 (verify) | 5 | 기대 0x80000014 | 0x80000014 (bit 31은 원래 1) | 0 |
| 2 (invert) | 5 | 0x7FFFFFEB 쓰기 | — | 0 |
| 3 (verify ~) | 5 | 기대 0x7FFFFFEB | 0xFFFFFFEB | 1, first_bad = 5 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 정상 256 word → 0, 512 write / 512 read.
- 빈 영역 `nwords = 0` → 0, 접근 0.
- bit 31 stuck-at-1 (word 5) → 1 (반전 패스에서만), stuck-at-0 → 1 (참 패스에서).
- A5 stuck-low (64 word) → 96: 0..31은 두 패스 모두 실패, 32..63은 참 패스에서만 (검증함).
- A2–A3 OR-short (16 word) → 20, `first_bad = 4` (셀 그룹 4개 × (3 + 2)).
- D7 stuck-high → 256: word마다 bit 7이 0인 패스에서 딱 한 번.

</details>

### 4. 꼬리 질문

**Q1. On the board you use a pointer, not accessors. Show the fill line.**

<details><summary>답</summary>

"I store the pointer value itself, truncated to the bus width, through a `volatile` pointer so the compiler can't skip the reads. On a 64-bit CPU testing a 32-bit word I take the low 32 bits of the address — unique within any region under 4 GB."
- 한국어 핵심: **`p[i] = (uint32_t)(uintptr_t)&p[i]`, `volatile` 포인터.**

```c
/* follow-up: board version, the data IS the pointer value (truncated to 32 bits) */
#include <stdint.h>
static uint32_t own_addr_ptr(volatile uint32_t *p, size_t nwords)
{
    uint32_t errs = 0;
    for (size_t i = 0; i < nwords; i++) p[i] = (uint32_t)(uintptr_t)&p[i];
    for (size_t i = 0; i < nwords; i++) if (p[i] != (uint32_t)(uintptr_t)&p[i]) errs++;
    return errs;
}
```

</details>

**Q2. Why does this miss coupling faults that March C- catches?**

<details><summary>답</summary>

"It only walks upward and the background is the address, so whether a write to one cell disturbs a neighbor depends on luck. March C- reads each cell before rewriting it, in both directions, with solid 0 and 1 backgrounds, which guarantees the aggressor-victim cases. In the V1/BV09 fault runs this test missed the idempotent coupling fault; March C- found it."
- 한국어 핵심: **단방향 + 주소값 배경 → coupling 보장 없음 → [V1](hmScreenPrep/problems/V1_march_memtest/index.html) March C-.**

</details>

**Q3. 8 GB of DDR — how long, and what do you change?**

<details><summary>답</summary>

"3N with 64-bit words is three passes over 8 GB, about 24 GB of traffic — seconds with DMA, much longer from an uncached CPU loop. I'd use 64-bit accesses, run from SRAM, and for bring-up test sampled regions plus the bus tests, leaving the full sweep for overnight."
- 한국어 핵심: **3N × 8 GB ≈ 24 GB 트래픽 → 64-bit 접근 · DMA · 샘플링 [추정 — 대역폭은 보드마다 다름].**

</details>

### 5. 채점 · 자주 틀리는 것

- 반전 패스 빼먹음 → 기대값에 이미 1인 비트의 stuck-at-1을 놓침 (위 trace).
- 참 패스에서 확인과 동시에 다음 word까지 쓰면서 순서가 꼬임 — 확인 → 반전 쓰기는 **같은 word**.
- 테스트 포인터에 `volatile` 없음 → 컴파일러가 방금 쓴 값을 그대로 돌려줌.
- `first_bad` 초기화(UINT32_MAX) 안 함.

## F4 · checksum 3종 — 8-bit sum · 2의 보수 · Fletcher-16 (★★ · 15분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 바이트 합(mod 256), 합이 0이 되게 만드는 2의 보수 checksum, 위치까지 반영하는 Fletcher-16을 각각 짠다.
- **상황**: 부트로더 이미지 헤더, Intel HEX 레코드, 센서 패킷, EEPROM 설정 블록 — CRC를 쓸 여유가 없거나 프로토콜이 정해 둔 곳에서 아직 많이 쓴다. 면접에서는 "각각 뭘 못 잡나"가 포인트.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `sum8(p, n)` | 바이트 합 | mod 256 (자연스러운 uint8 wrap) |
| `cksum8_twos(p, n)` | 붙이면 전체 합이 0이 되는 바이트 | `0 - sum8` |
| `fletcher16(p, n)` | 두 누적합 | s1 = Σ data mod 255, s2 = Σ s1 mod 255, 반환 `(s2 << 8) or s1` |

- **예시를 말로 풀면**: "abcde"는 s1이 97, 195, 39, 139, 240으로, s2가 97, 37, 76, 215, 200으로 간다 → `0xC8F0`. Intel HEX 레코드 바이트 `03 00 30 00 02 33 7A`의 합은 0xE2, 2의 보수는 `0x1E`.
- **지켜야 할 조건**: `n = 0`이면 `p`가 NULL이어도 됨 → 0. 긴 입력에서도 중간 합이 넘치지 않을 것.

### 1. Problem (English)

Implement three lightweight checksums used in boot headers and serial protocols.

```c
uint8_t  sum8(const uint8_t *p, size_t n);
uint8_t  cksum8_twos(const uint8_t *p, size_t n);
uint16_t fletcher16(const uint8_t *p, size_t n);
```

- `sum8("123456789")` → `0xDD`, `cksum8_twos("123456789")` → `0x23`.
- Intel HEX `:0300300002337A1E` → `cksum8_twos(03 00 30 00 02 33 7A)` = `0x1E`.
- `fletcher16("abcdef")` → `0x2057`, `fletcher16("abcdefgh")` → `0x0627`.

### 2. 먼저 물어볼 것 (clarify)

- "For Fletcher, which byte is the high byte — sum2 or sum1?" → sum2 high (`(s2 << 8) or s1`), as in the common reference.
- "Is the receiver check 'recompute and compare' or 'sum everything including the checksum to zero'?" → both; show the zero-sum for the two's-complement one.
- "Max length?" → large (MB) — so no overflow tricks.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (바이트 순서, 수신측 검사법) |
| 2–5 | "abcde" 처음 2바이트 손으로 |
| 5–13 | 코드 3개 |
| 13–17 | trace (아래 표) |
| 17–20 | 약점 말하기 + 확장(Adler, CRC) |

- 소리 내서: **"A plain sum can't see reordering. Fletcher's second sum weights each byte by how far it is from the end, so swapping two bytes changes it."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

uint8 덧셈은 int로 승격된 뒤 다시 uint8로 잘린다 → `(uint8_t)(s + *p)` 캐스트가 곧 mod 256. 2의 보수는 `~s + 1` = `0u - s`.

</details>

<details><summary>힌트 2</summary>

Fletcher: 매 바이트마다 `% 255`를 하면 s1, s2가 항상 255 미만이라 `uint16_t`로 충분하다. (빠른 버전은 몇백 바이트 모아서 한 번에 mod.)

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stddef.h>

uint8_t sum8(const uint8_t *p, size_t n)            /* sum mod 256 */
{
    uint8_t s = 0;
    while (n--) s = (uint8_t)(s + *p++);            /* wraps mod 256 by design */
    return s;
}

uint8_t cksum8_twos(const uint8_t *p, size_t n)     /* byte that makes the total 0 */
{
    return (uint8_t)(0u - sum8(p, n));              /* = ~sum + 1 */
}

uint16_t fletcher16(const uint8_t *p, size_t n)
{
    uint16_t s1 = 0, s2 = 0;                        /* both stay < 255 */
    while (n--) {
        s1 = (uint16_t)((s1 + *p++) % 255u);        /* data sum     */
        s2 = (uint16_t)((s2 + s1) % 255u);          /* sum of sums: position-weighted */
    }
    return (uint16_t)((s2 << 8) | s1);
}
```

- 시간 O(n) · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`fletcher16("abcde")`

| 바이트 | s1 = (s1 + b) % 255 | s2 = (s2 + s1) % 255 |
|---|---|---|
| 'a' 97 | 97 | 97 |
| 'b' 98 | 195 | 292 % 255 = 37 |
| 'c' 99 | 294 % 255 = 39 | 76 |
| 'd' 100 | 139 | 215 |
| 'e' 101 | 240 = 0xF0 | 455 % 255 = 200 = 0xC8 |

→ `0xC8F0` (공개 테스트 벡터와 일치).

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 공개 벡터: Fletcher "abcde"/"abcdef"/"abcdefgh" → 0xC8F0 / 0x2057 / 0x0627, Intel HEX → 0x1E (검증함).
- 빈 입력 (NULL, 0) → 세 개 모두 0.
- round-trip: 데이터 + `cksum8_twos` 바이트의 `sum8` = 0.
- 순서 바꿈 `12 34` vs `34 12` → sum8 같음, Fletcher 다름.
- mod-255 맹점: `00 00`과 `FF FF`의 Fletcher가 같다 (0과 255가 mod 255에서 같은 값).
- 100 000바이트 0xFE → 넘침 없이 s1, s2 < 255.

</details>

### 4. 꼬리 질문

**Q1. When would you pick CRC over Fletcher?**

<details><summary>답</summary>

"Whenever the channel has burst errors or I need a guaranteed Hamming distance — serial links, flash pages, network frames. A CRC of width n catches every burst up to n bits, and a good polynomial catches all 2- and 3-bit errors up to a known length. Fletcher is cheaper on a CPU without a table or hardware CRC, and it's fine for detecting gross corruption in a boot header."
- 한국어 핵심: **burst·HD 보장이 필요하면 CRC, CPU 비용이 문제면 Fletcher.**

</details>

**Q2. Why mod 255 and not mod 256?**

<details><summary>답</summary>

"255 is one's-complement arithmetic: the carry out of the byte wraps back in, so high bits keep influencing the result. With mod 256 the carry is simply lost, and the second sum gets weaker. The price is that 0x00 and 0xFF look identical."
- 한국어 핵심: **mod 255 = end-around carry → 캐리를 버리지 않음, 대신 0x00 = 0xFF.**

</details>

### 5. 채점 · 자주 틀리는 것

- `uint8_t s; s += *p;` 자체는 맞지만 `-Wconversion` 경고 → 명시 캐스트로 의도 표시.
- 2의 보수를 `~s`(1의 보수)로 씀 → Intel HEX 검사에서 1 차이.
- Fletcher s1, s2의 바이트 위치 반대로 조립.
- `% 255`를 루프 끝에 한 번만 → 긴 입력에서 `uint16_t` 넘침.
- `n == 0`일 때 `p` 역참조 없음을 말로 확인.

## F5 · CRC-8 (poly 0x07) 비트 단위 + 1바이트 손계산 (★★ · 18분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 다항식 x⁸+x²+x+1 (0x07), init 0, MSB-first, 반사 없음, 최종 XOR 없음인 CRC-8을 비트 단위로 짜고, 바이트 `0x31` 하나를 종이에 8스텝 돌린다.
- **상황**: SMBus PEC, 많은 센서(온도·압력)의 레지스터 읽기 CRC, 작은 패킷 프레이밍. "CRC 짜 보세요"는 링크·센서 검증 면접의 단골이고, 손으로 1바이트 돌리는 게 이해했는지 확인하는 방법.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `crc8_07(p, n)` | CRC-8 계산 | 바이트를 crc의 위쪽에 XOR → 8번: 최상위 비트가 1이면 `(crc << 1) ^ 0x07`, 아니면 `crc << 1` |

- **예시를 말로 풀면**: 바이트 0x01 → 7번 왼쪽으로 밀리다가 마지막에 1이 위로 빠지면서 0x07이 XOR → `0x07`. 표준 확인 문자열 "123456789" → `0xF4`.
- **지켜야 할 조건**: 결과는 8비트로 유지 (`uint8_t` 캐스트). 빈 입력 → 0.

### 1. Problem (English)

A sensor returns each register read followed by a CRC-8 byte (poly 0x07, init 0x00, MSB-first, no reflection, no final XOR). Write the checker's CRC, bitwise, then compute the CRC of the single byte 0x31 by hand.

```c
uint8_t crc8_07(const uint8_t *p, size_t n);
```

- `"123456789"` → `0xF4` (catalogue check value, CRC-8/SMBUS).
- `{0x01}` → `0x07`, `{0xFF}` → `0xF3`, `{0x31}` → `0x97`.
- Message followed by its own CRC → `0x00` (residue).

### 2. 먼저 물어볼 것 (clarify)

- "Which variant exactly — init, reflected in/out, final XOR?" → init 0, not reflected, xorout 0 (CRC-8/SMBUS). 이걸 안 물으면 다른 답이 나온다.
- "Is the CRC over the data only, or over address + command bytes too?" → data only here (SMBus PEC는 주소 바이트 포함).
- "Bitwise is OK, or do you want the table version?" → bitwise first, table as follow-up.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (변형 파라미터 5개) |
| 2–5 | 다항식 나눗셈 그림: 위로 빠지는 비트 = 1이면 poly XOR |
| 5–11 | 코드 |
| 11–17 | **0x31 손계산 8스텝** (아래 trace) |
| 17–20 | 테스트(residue, 단일 비트) + 테이블 버전 |

- 소리 내서: **"CRC is the remainder of dividing the message by the polynomial in GF(2). Shifting left is multiplying by x; if the x^8 term pops out, I subtract — which is XOR — the polynomial's low 8 bits."**
- 소리 내서: **"Appending the CRC makes the whole frame divisible by the polynomial, so the receiver's CRC over data plus CRC is zero."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

MSB-first: 들어온 바이트를 `crc ^= byte`로 crc 전체에 겹친 뒤, 8번 시프트하면서 매번 **시프트 전** 최상위 비트(`crc & 0x80`)를 본다.

</details>

<details><summary>힌트 2</summary>

`crc << 1`은 int로 승격돼 9비트가 될 수 있다 → `(uint8_t)` 캐스트로 x⁸ 항을 버리는 것이 곧 "빼기"의 절반. 0x107의 x⁸ 항은 캐스트가, 0x07은 XOR가 처리.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stddef.h>

uint8_t crc8_07(const uint8_t *p, size_t n)
{
    uint8_t crc = 0x00;                         /* init */
    while (n--) {
        crc ^= *p++;                            /* byte enters at the MSB end */
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80u)                    /* top bit out = 1 -> subtract poly */
                crc = (uint8_t)((crc << 1) ^ 0x07u);
            else
                crc = (uint8_t)(crc << 1);
        }
    }
    return crc;                                 /* xorout 0 */
}
```

- 시간 O(8n) · 공간 O(1). 테이블 버전 O(n) + 256바이트.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

`crc8_07({0x31})`: `crc = 0x00 ^ 0x31 = 0x31` (실제 실행 출력과 일치)

| 스텝 | 시작 crc | 최상위 비트 | 계산 | 결과 |
|---|---|---|---|---|
| 0 | 0x31 | 0 | 0x31 << 1 | 0x62 |
| 1 | 0x62 | 0 | << 1 | 0xC4 |
| 2 | 0xC4 | 1 | 0x88 ^ 0x07 | 0x8F |
| 3 | 0x8F | 1 | 0x1E ^ 0x07 | 0x19 |
| 4 | 0x19 | 0 | << 1 | 0x32 |
| 5 | 0x32 | 0 | << 1 | 0x64 |
| 6 | 0x64 | 0 | << 1 | 0xC8 |
| 7 | 0xC8 | 1 | 0x90 ^ 0x07 | **0x97** |

다음 바이트 '2'(0x32)는 `0x97 ^ 0x32 = 0xA5`에서 시작 → "12"의 CRC = 0x72.

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- check 값 "123456789" → 0xF4 (reveng 카탈로그 CRC-8/SMBUS와 일치, 검증함).
- 빈 입력 → 0x00 (init 0이라 빈 입력과 0x00 바이트들이 구분 안 됨 — 꼬리 질문).
- residue: 메시지 + CRC 바이트 → 0x00.
- 4바이트 데이터 + CRC 프레임(40비트)에서 **1·2·3비트 에러 10 700가지 전부 검출** (검증함).
- 한계: 17바이트 프레임(136비트)에서 **127비트 떨어진 2비트 에러만 놓침**, 그보다 가까우면 전부 검출 (전수 검증) — g(x) = (x+1)·(7차 primitive) 이라 x¹²⁷ ≡ 1.

</details>

### 4. 꼬리 질문

**Q1. Make it faster.**

<details><summary>답</summary>

"Precompute a 256-entry table where entry i is the CRC of the single byte i, then each byte is one XOR and one lookup: `crc = tab[crc ^ byte]`. That works because an 8-bit CRC register is the same width as the byte, so the next state depends only on `crc ^ byte`. For CRC-16/32 it becomes `tab[(crc >> 24) ^ byte] ^ (crc << 8)`. Or use the hardware CRC unit most MCUs have."
- 한국어 핵심: **256 테이블, `crc = tab[crc ^ b]` — 비트 버전으로 테이블을 만들어 자기 검증.**

```c
/* follow-up: table-driven version (256-byte table, one lookup per byte) */
static uint8_t crc8_tab[256];
static void crc8_tab_init(void)
{
    for (unsigned i = 0; i < 256; i++) { uint8_t b = (uint8_t)i; crc8_tab[i] = crc8_07(&b, 1); }
}
static uint8_t crc8_fast(const uint8_t *p, size_t n)
{
    uint8_t crc = 0;
    while (n--) crc = crc8_tab[crc ^ *p++];
    return crc;
}
```

</details>

**Q2. Why do some CRCs start at 0xFF instead of 0?**

<details><summary>답</summary>

"With init zero, leading zero bytes don't change the CRC, so a frame with extra zeros in front — or an all-zero frame from a dead bus — passes. A non-zero init makes leading zeros count. The final XOR does the same for trailing zeros and makes an all-zero result less likely to be valid."
- 한국어 핵심: **init 0이면 앞의 0x00 바이트를 못 봄 → 죽은 버스(전부 0)가 통과. init 0xFF / xorout으로 해결.**

</details>

**Q3. How do you know which variant a datasheet means?**

<details><summary>답</summary>

"I take a known frame from the datasheet or a logic-analyzer capture and run it through each candidate parameter set — width, poly, init, refin, refout, xorout — until it matches, and I always test my code against the catalogue's check value for '123456789'. Bit order mistakes are the most common, so I check reflected versus not first."
- 한국어 핵심: **datasheet 예시 프레임 + 카탈로그 check 값으로 파라미터 6개 맞추기.**

</details>

### 5. 채점 · 자주 틀리는 것

- 최상위 비트를 시프트 **후**에 검사 → 전혀 다른 CRC.
- `(uint8_t)` 캐스트 없이 `crc`를 int에 담음 → 9비트 이상이 남아 결과 오염.
- 변형 파라미터(init/reflect/xorout)를 clarify 안 함.
- reflected 변형(LSB-first, 반사된 poly `0xE0`)과 섞어 씀 — 이 문제는 MSB-first.
- 손계산에서 XOR를 시프트 전 값에 함 (순서: shift → XOR 0x07).

## F6 · PRBS7 LFSR 생성 + self-sync 수신 체커 (★★ · 18분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: PRBS7 (x⁷+x⁶+1) 비트 생성기와, **받은 비트로 자기 레지스터를 채우는** self-synchronizing 체커를 짜서 에러 비트를 센다.
- **상황**: SerDes/PCIe/MIPI 링크 검증에서 TX가 PRBS를 보내고 RX 체커가 비트 에러를 센다. self-sync 체커는 시작 위치(phase)를 몰라도 7비트 후 자동으로 맞춰지는 게 장점 — 대신 에러 하나가 여러 번 세진다. (load-then-free-run 체커는 [V6](hmScreenPrep/problems/V6_prbs_crc_checker/index.html).)
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `prbs7_next(&s)` | LFSR 한 스텝, 새 비트 반환 | `b = s[6] ^ s[5]`, `s = ((s << 1) or b) & 0x7F` |
| `prbs7_chk_bit(&c, rx)` | 받은 비트 하나 검사 | 처음 7비트는 채우기만 · 이후: 레지스터로 예측 → 다르면 `errs++` · 레지스터엔 **받은 비트**를 넣음 |

- **예시를 말로 풀면**: seed 0x7F에서 처음 6비트는 0, 7번째가 1 → 첫 16비트를 MSB-first로 묶으면 `0x020C`. 깨끗한 스트림 1000비트 → 에러 0, 검사 993비트. 중간 비트 하나를 뒤집으면 에러가 **3**으로 세진다.
- **지켜야 할 조건**: state 0은 lock-up (영원히 0) — seed는 0이 아니어야. 체커는 어느 위치에서 시작해도 동기화.

### 1. Problem (English)

On a SerDes loopback, the TX sends PRBS7 and you implement the RX checker in firmware. Write the generator and a self-synchronizing checker that needs no seed and no alignment.

```c
unsigned prbs7_next(uint8_t *s);            /* returns the new bit */
typedef struct { uint8_t sr; unsigned fill; uint32_t errs, checked; } Prbs7Chk;
void prbs7_chk_bit(Prbs7Chk *c, unsigned rx);
```

- Seed `0x7F` → period 127 bits, 64 ones; first 16 bits = `0x020C`.
- 1000 clean bits → `errs = 0`, `checked = 993`. Starting at bit 333 → still 0.
- One flipped bit at position 500 → `errs = 3`. Every bit inverted (P/N swap) → `errs == checked`.

### 2. 먼저 물어볼 것 (clarify)

- "Which polynomial and which tap convention — x^7 + x^6 + 1 with the new bit from bits 6 and 5?" → yes (the common PRBS7 definition).
- "Is the stream inverted on the wire?" → assume not; detect it (follow-up).
- "Do I need an exact error count or is the 'triple count' OK?" → raw mismatch count; explain the multiplication.
- "How do I get bits — per bit or per byte?" → per bit for paper; bytes MSB-first in practice.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (탭, 비트 순서, 반전) |
| 2–5 | seed 0x7F로 3스텝 손으로 |
| 5–12 | 생성기 4줄 + 체커 |
| 12–17 | trace: 에러 1개가 왜 3번 세지는지 |
| 17–20 | 테스트 (위상 무관, 반전, all-zero 함정) |

- 소리 내서: **"The checker predicts each bit from the last seven received bits, then shifts in the received bit, not the prediction — so after seven clean bits it's locked at any phase."**
- 소리 내서: **"The price is error multiplication: a bad bit is wrong itself and then poisons the two predictions that use it, so one error counts three times for PRBS7."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

생성기와 체커의 예측식은 **같다**: `((sr >> 6) ^ (sr >> 5)) & 1`. 차이는 레지스터에 무엇을 넣느냐 — 생성기는 자기가 만든 비트, 체커는 받은 비트.

</details>

<details><summary>힌트 2</summary>

`fill < 7`이면 비교 없이 레지스터만 채우기. 7번째 비트까지 넣은 뒤부터 `checked++`.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

/* Fibonacci LFSR, state = last 7 bits, bit 0 newest. new = s[6] ^ s[5] */
unsigned prbs7_next(uint8_t *s)
{
    unsigned b = ((*s >> 6) ^ (*s >> 5)) & 1u;
    *s = (uint8_t)(((*s << 1) | b) & 0x7Fu);
    return b;
}

typedef struct {
    uint8_t  sr;        /* last 7 RECEIVED bits                */
    unsigned fill;      /* bits loaded so far (sync after 7)   */
    uint32_t errs;      /* mismatches after sync               */
    uint32_t checked;   /* bits compared after sync            */
} Prbs7Chk;

/* Self-sync checker: predict each bit from the previous 7 received bits,
 * then shift the RECEIVED bit in (not the predicted one). */
void prbs7_chk_bit(Prbs7Chk *c, unsigned rx)
{
    rx &= 1u;
    if (c->fill < 7) {
        c->fill++;                                   /* still loading */
    } else {
        unsigned exp = ((c->sr >> 6) ^ (c->sr >> 5)) & 1u;
        c->errs += (exp != rx);
        c->checked++;
    }
    c->sr = (uint8_t)(((c->sr << 1) | rx) & 0x7Fu);
}
```

- 시간 O(1)/비트 · 공간 O(1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

생성기 seed 0x7F (`s[6]` = bit 6):

| 스텝 | s (전) | s[6] ^ s[5] | 출력 b | s (후) |
|---|---|---|---|---|
| 1 | 0x7F | 1 ^ 1 | 0 | 0x7E |
| 2 | 0x7E | 1 ^ 1 | 0 | 0x7C |
| 6 | 0x60 | 1 ^ 1 | 0 | 0x40 |
| 7 | 0x40 | 1 ^ 0 | 1 | 0x01 |

→ 첫 바이트 비트 `0000 0010` = 0x02.

체커: 받은 비트 k가 틀렸을 때 예측 `rx[n] ≟ rx[n-7] ^ rx[n-6]`

| n | 예측에 쓰는 비트 | k가 끼어 있나 | 카운트 |
|---|---|---|---|
| k | rx[k-7], rx[k-6] | 아니오, rx[k] 자체가 틀림 | 1 |
| k+6 | rx[k-1], rx[k] | 예 | 2 |
| k+7 | rx[k], rx[k+1] | 예 | 3 |

sync 구간 안(k = 2)이면 자기 자신은 비교 안 되므로 k+6, k+7만 → 2 (검증함).

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 주기 127, 1의 개수 64, 최장 1 연속 7 · 0 연속 6 (m-sequence 성질, 검증함). Wikipedia C 예제(start 0x02)와 127스텝 상태 전부 일치.
- 깨끗한 스트림, 중간(333)부터 시작해도 에러 0.
- 단일 에러 → 3, sync 구간 안 에러 → 2, 20비트 burst 후 7비트 지나면 다시 0.
- 극성 반전(P/N swap) → 모든 비트 실패 (`errs == checked`) → "BER 100%면 먼저 lane 극성 의심".
- **함정**: 전부 0인 입력(죽은 링크) → 에러 0으로 통과! state 0이 자기 자신을 예측하기 때문.

</details>

### 4. 꼬리 질문

**Q1. The link is dead and outputs all zeros. What does your checker report, and how do you fix it?**

<details><summary>답</summary>

"Zero errors, because the all-zero state predicts zero forever — that's the LFSR lock-up state. So the checker must also prove there is a real PRBS: count ones in the window and require roughly half, 64 out of every 127 bits, or check for more than seven zeros in a row, which can't happen in PRBS7. If that fails I report loss of lock, not a perfect BER."
- 한국어 핵심: **all-zero = lock-up 상태라 0 에러 → 1 비율 ~50% 또는 0이 7개 이상 연속이면 LOS로 보고.**

</details>

**Q2. Self-sync versus load-and-free-run checkers — which do you use?**

<details><summary>답</summary>

"Self-sync recovers by itself after any burst and needs no seed, but it triples each error, so I divide by three for an estimate at low BER. Load-and-free-run counts each error exactly once, but if a seed bit is wrong it locks to the wrong phase and reports about half of all bits as errors, so it needs a re-sync rule. Hardware BERTs usually sync with the first method and then switch to the second." [추정]
- 한국어 핵심: **self-sync = 자동 복구·에러 ×3, free-run = 정확·위상 잘못 잡으면 50% → 둘 다 씀 ([V6](hmScreenPrep/problems/V6_prbs_crc_checker/index.html)).**

</details>

**Q3. How many bits must you check to claim BER below 1e-12?**

<details><summary>답</summary>

"With zero errors observed, about 3 / BER bits for 95% confidence — so 3e12 bits. At 10 Gb/s that's 300 seconds. That's why PRBS31 runs for minutes per lane, and why I report the confidence level along with the BER."
- 한국어 핵심: **0 에러 + 95% 신뢰 → 3/BER 비트. 1e-12 @ 10 Gb/s ≈ 5분.** [추정 — 통계 경험칙, 출처 미대조]

</details>

### 5. 채점 · 자주 틀리는 것

- 체커 레지스터에 **예측 비트**를 넣음 → 그건 free-run이고, 에러 하나에 위상이 안 깨지는 대신 burst 후 복구가 안 됨.
- 탭 인덱스를 1-based로 헷갈림: x⁷, x⁶ 항 = state의 bit 6, bit 5.
- `& 0x7F` 마스크 빠뜨림 → 8번째 비트가 남아 다음 예측 오염 (uint8은 `<< 1`에서 bit 7까지 감).
- seed 0 허용.
- 에러 수 ×3을 BER로 그대로 보고.

## F7 · Hamming(7,4) 인코드 / 1비트 정정 디코드 (★★ · 18분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 데이터 4비트에 parity 3비트를 붙여 7비트 codeword를 만들고, 수신 시 **syndrome = 틀린 비트의 위치 번호**를 이용해 1비트 에러를 고친다.
- **상황**: DRAM/SRAM ECC(SECDED), 플래시 메타데이터의 기본 원리. "ECC가 어떻게 에러 위치를 아나?"를 종이로 보여 주는 가장 작은 예. SSD 펌웨어 경험(LDPC/BCH)과 연결해서 말하면 좋다.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `ham74_encode(d)` | 4비트 → 7비트 | 위치 1..7 = p1 p2 d1 p4 d2 d3 d4 · codeword bit (pos-1) = 위치 pos · d1 = nibble bit 0 |
| `ham74_decode(cw, &d)` | 7비트 → 4비트, 1비트 정정 | 반환 0 = 깨끗, 1 = 하나 고침 · bit 7은 무시 |

- **예시를 말로 풀면**: d1..d4 = 1,0,1,1 → p1 = d1^d2^d4 = 0, p2 = d1^d3^d4 = 1, p4 = d2^d3^d4 = 0 → 위치 순서 `0110011` (공개 예제와 같음) = 이 표현으로 `0x66`. 위치 5를 뒤집은 `0x76`을 디코드하면 syndrome 5 → 고쳐서 0xD.
- **지켜야 할 조건**: 루프든 식이든 상관없지만 parity 위치(1, 2, 4)를 틀리면 안 됨.

### 1. Problem (English)

Implement Hamming(7,4) as used in the textbook layout: positions 1..7 are `p1 p2 d1 p4 d2 d3 d4`, and codeword bit (pos-1) holds position pos. Decode corrects any single-bit error.

```c
uint8_t ham74_encode(uint8_t d);          /* d: low 4 bits, d1 = bit 0 */
int     ham74_decode(uint8_t cw, uint8_t *d);  /* 0 clean, 1 corrected */
```

- `ham74_encode(0xD)` → `0x66` (positions `0110011`).
- `ham74_decode(0x76, &d)` → 1, `d = 0xD` (position 5 was flipped).
- `ham74_encode(0x0)` → `0x00`, `ham74_encode(0xF)` → `0x7F`.

### 2. 먼저 물어볼 것 (clarify)

- "Which bit layout — positional (parity at 1, 2, 4) or systematic (data first, parity last)?" → positional.
- "What should decode do on a double error?" → can't detect it with (7,4); it will miscorrect. Mention SECDED.
- "Is bit 7 of the byte used?" → ignore it.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (배치, 더블 에러) |
| 2–6 | 핵심 관찰 말하기 + 1011 예제 손으로 |
| 6–13 | `pos_xor` + encode + decode |
| 13–17 | trace (아래) |
| 17–20 | 전수 테스트 제안 + SECDED |

- 소리 내서: **"Parity bit p_k covers every position whose number has bit k set. So for a valid codeword, the XOR of the position numbers of all 1 bits is zero — and if one bit flips, that XOR becomes exactly its position."**
- 소리 내서: **"So encode is: place the data, compute that XOR, and put its three bits into positions 1, 2 and 4 to cancel it."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

함수 하나 `pos_xor(cw)` = 1인 비트들의 위치 번호(1..7)를 전부 XOR. 유효한 codeword는 0이다.

</details>

<details><summary>힌트 2</summary>

parity 위치 1, 2, 4는 각각 syndrome의 bit 0, 1, 2와 같다 → syndrome s를 그대로 bit 0, bit 1, bit 3(위치 4)에 넣으면 전체 XOR가 0이 된다. decode: `s != 0`이면 `cw ^= 1u << (s - 1)`.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>

/* Codeword bit (pos-1) holds position pos = 1..7:  p1 p2 d1 p4 d2 d3 d4
 * Parity positions 1, 2, 4; data d1 = bit 0 of the nibble. */
static unsigned pos_xor(uint8_t cw)          /* XOR of positions of all 1 bits */
{
    unsigned s = 0;
    for (unsigned pos = 1; pos <= 7; pos++)
        if ((cw >> (pos - 1)) & 1u) s ^= pos;
    return s;                                 /* 0 for every valid codeword */
}

uint8_t ham74_encode(uint8_t d)
{
    uint8_t cw = (uint8_t)(((d & 1u) << 2)            /* d1 -> pos 3      */
                         | (((d >> 1) & 7u) << 4));   /* d2..d4 -> pos 5..7 */
    unsigned s = pos_xor(cw);                         /* choose p1,p2,p4 so the */
    cw |= (uint8_t)((s & 3u) | ((s & 4u) << 1));      /* total XOR becomes 0    */
    return cw;
}

/* Returns 0 = clean, 1 = one bit corrected. *d = decoded nibble. */
int ham74_decode(uint8_t cw, uint8_t *d)
{
    cw &= 0x7Fu;
    unsigned s = pos_xor(cw);                         /* syndrome = bad position */
    if (s) cw ^= (uint8_t)(1u << (s - 1));
    *d = (uint8_t)(((cw >> 2) & 1u) | (((cw >> 4) & 7u) << 1));
    return s != 0;
}
```

- 시간 O(7) · 공간 O(1). (테이블 128칸이면 O(1) 룩업.)

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

인코드 `d = 0xD` (d1=1, d2=0, d3=1, d4=1):

| 단계 | 값 | 설명 |
|---|---|---|
| 데이터 배치 | 0x64 | d1 → 위치 3 (bit 2), d2..d4 = 110 → 위치 5..7 = bit 4..6 중 bit 5, 6 |
| pos_xor | 3 ^ 6 ^ 7 = 2 | 1인 위치 3, 6, 7 |
| parity 넣기 | 0x64 or 0x02 = 0x66 | s = 2 → 위치 2 (bit 1) |

디코드 `0x76` (위치 5 뒤집힘): 1인 위치 2, 3, 5, 6, 7 → 2^3 = 1, ^5 = 4, ^6 = 2, ^7 = 5 → s = 5 → bit 4 뒤집기 → 0x66 → d = 1 or (6 << 1) = 0xD.

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 공개 예제 1011 → 0110011 (= 0x66) 일치 (검증함).
- 0x0 → 0x00, 0xF → 0x7F.
- 전수: 16개 데이터 × (에러 없음 + 1비트 에러 7개) 전부 복원 (검증함).
- 최소 거리: 서로 다른 codeword 쌍 120개 모두 ≥ 3비트 차이.
- 2비트 에러: 16 × 21가지 **전부** "고쳤다(1)"고 하면서 데이터가 틀림 — 미검출 오정정 (검증함).
- bit 7이 1이어도 무시.

</details>

### 4. 꼬리 질문

**Q1. Make it detect double errors (SECDED).**

<details><summary>답</summary>

"Add an overall parity bit over all seven bits. Then: syndrome zero and parity OK means clean; parity wrong means a single error — fix it at the syndrome position, or it was the parity bit itself if the syndrome is zero; syndrome non-zero but parity OK means two bits flipped, which I report as uncorrectable. That's exactly the SECDED scheme DRAM ECC uses, just on 64 data bits with 8 check bits."
- 한국어 핵심: **전체 parity 1비트 추가 → (s≠0, parity OK) = 2비트 에러 검출. DRAM ECC = (72,64) SECDED.**

```c
/* follow-up: SECDED (8,4) = Hamming(7,4) + overall parity in bit 7.
 * returns 0 clean, 1 corrected single, -1 double error detected (uncorrectable) */
static int secded_decode(uint8_t cw8, uint8_t *d)
{
    unsigned s = pos_xor((uint8_t)(cw8 & 0x7Fu));
    unsigned odd = (unsigned)__builtin_parity(cw8);       /* 1 = odd number of flips */
    if (s && !odd) return -1;                              /* syndrome but parity OK: 2 bits */
    ham74_decode(cw8, d);                                  /* s == 0 && odd: bit 7 itself */
    return (s || odd) ? 1 : 0;
}
static uint8_t secded_encode(uint8_t d)
{
    uint8_t cw = ham74_encode(d);
    return (uint8_t)(cw | ((unsigned)__builtin_parity(cw) << 7));
}
```

</details>

**Q2. How would you validate ECC on a real memory controller?**

<details><summary>답</summary>

"Use the controller's error-injection register to flip one bit, then two bits, in a known word on write. Read it back and check: the single is corrected and the correctable-error counter and interrupt fire with the right address and syndrome; the double raises an uncorrectable error. Then run a scrub and confirm the counters. If there's no injection hook, write with ECC disabled and read with it enabled."
- 한국어 핵심: **injection 레지스터로 1비트/2비트 주입 → 정정·카운터·인터럽트·주소 확인 ([BV09 §2.4](2026-11-01_BV09_ddr_bringup_training_memtest.html)).**

</details>

**Q3. Why does a 2-bit error always miscorrect here?**

<details><summary>답</summary>

"Hamming(7,4) is a perfect code: every 7-bit word is within distance 1 of exactly one codeword. A 2-bit error lands at distance 1 from a different codeword, so the decoder confidently moves to it. Distance 3 gives single-error correction or double-error detection, not both."
- 한국어 핵심: **perfect code → 2비트 에러는 항상 다른 codeword의 1비트 이웃 → 오정정.**

</details>

### 5. 채점 · 자주 틀리는 것

- parity 위치를 0-based로 착각 (위치 1, 2, 4 = bit 0, 1, 3).
- syndrome으로 비트를 뒤집을 때 `1u << s` (정답은 `s - 1`).
- 데이터 추출 시 위치 3, 5, 6, 7 순서 뒤섞음.
- 2비트 에러도 고친다고 주장.
- 입력의 bit 7을 마스크 안 함 → syndrome 오염.

## F8 · 메모리 테스트 결과 요약 → 고장 비트·주소 라인 추정 (★★ · 18분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 실패 목록 `{addr, exp, act}`를 XOR·AND·OR로 요약하고, 그 마스크만 보고 "셀 하나 / 데이터선 / 주소선 / 기타"를 판정한다.
- **상황**: 테스트가 수백 개 실패를 쏟아낼 때 보드 엔지니어에게 필요한 건 목록이 아니라 **"D7 stuck-high"** 또는 **"A5 open"** 한 줄이다. validation 엔지니어의 진짜 일은 결과 해석.
- **구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `fail_summarize(f, n, &s)` | 마스크 7개 누적 | `x_or`/`x_and` = exp^act의 OR/AND · `rose` = 1로 잘못 읽힌 비트 · `fell` = 0으로 잘못 읽힌 비트 · `a_and`/`a_or` = 실패 주소의 AND/OR |
| `fail_diagnose(&s, &which)` | 분류 | 실패 0 → PASS · 주소가 하나 → ONE_CELL · 비트가 한 방향으로만 틀림 → DATA_HI / DATA_LO · 틀린 비트마다 양방향 + 하위 2비트 아님 → ADDR (`which = x_or >> 2`) · 그 외 OTHER |

- **예시를 말로 풀면**: 주소 3곳에서 실패했고 매번 bit 4만 0→1로 틀렸다 → 데이터선 D4가 high로 붙었다. 반면 own-address 패턴에서 bit 7이 어떤 때는 올라가고 어떤 때는 내려간다면 → 값이 다른 주소에서 온 것 = 주소 비트 (7 − 2) = A5.
- **지켜야 할 조건**: 데이터 패턴은 own-address(`exp = BASE + 4*addr`, 반전 패스 포함)라고 가정 — 그래야 `exp ^ act`가 주소 차이를 보여 준다. 빈 목록 가능.

### 1. Problem (English)

Your memory test produced a list of failures. Turn it into a one-line diagnosis for the board team. Assume the data pattern was each word's own byte address (then its inverse), so an alias shows `exp ^ act = (addr ^ alias) << 2`.

```c
typedef struct { uint32_t addr, exp, act; } MemFail;
typedef struct { uint32_t n, x_or, x_and, rose, fell, a_and, a_or; } FailSum;
typedef enum { DX_PASS, DX_ONE_CELL, DX_DATA_HI, DX_DATA_LO, DX_ADDR, DX_OTHER } Dx;
void fail_summarize(const MemFail *f, size_t n, FailSum *s);
Dx   fail_diagnose(const FailSum *s, uint32_t *which);
```

- `{0x10, 0, 0x10}`, `{0x24, 0xAAAAAAAA, 0xAAAAAABA}`, `{0x3C, 0x12345600, 0x12345610}` → `DX_DATA_HI`, `which = 0x10`.
- Every failure at address 0x42 → `DX_ONE_CELL`, `which = 0x42`.
- 64-word own-address run with A5 stuck-low → `DX_ADDR`, `which = 0x20`.

### 2. 먼저 물어볼 것 (clarify)

- "What data pattern produced the list?" → own address + inverse. (패턴이 달라지면 규칙도 바뀐다.)
- "Is the list complete or capped?" → complete for this exercise; real logs cap at N, which biases AND masks.
- "Is `addr` a word offset or a byte address?" → word offset.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (패턴, 주소 단위) |
| 2–6 | 예시 3줄을 손으로 XOR → 마스크 |
| 6–14 | summarize (루프 하나) + diagnose (if 체인) |
| 14–18 | trace |
| 18–20 | 한계·다음 단계(March C-) 말하기 |

- 소리 내서: **"OR of the XORs says which bits ever failed, AND says which failed every time. A bit that only ever reads too high is a stuck-high line; a bit that fails both ways means the data came from a different address."**
- 소리 내서: **"With an own-address pattern, the XOR between expected and actual literally is the address difference shifted by two — it names the address line."**

<details><summary>힌트 1 (막히면 5분 후)</summary>

AND 누적은 `0xFFFFFFFF`에서 시작(항등원). `rose = x & act` (틀린 비트 중 실제가 1), `fell = x & exp`.

</details>

<details><summary>힌트 2</summary>

판정 순서가 중요: 주소 하나(`a_and == a_or`) → 한 방향만(`fell == 0` 또는 `rose == 0`) → 양방향이고 `(rose & fell) == x_or`이면 주소. 하위 2비트(바이트 레인)가 섞이면 주소 설명이 안 맞으므로 OTHER.

</details>

<details><summary>모범 답 (C)</summary>

```c
#include <stdint.h>
#include <stddef.h>

typedef struct { uint32_t addr, exp, act; } MemFail;   /* addr = word offset */

typedef struct {
    uint32_t n;
    uint32_t x_or, x_and;   /* data bits that failed in ANY / in EVERY entry   */
    uint32_t rose, fell;    /* read 1 where 0 expected / read 0 where 1 expected */
    uint32_t a_and, a_or;   /* AND / OR of failing addresses                  */
} FailSum;

void fail_summarize(const MemFail *f, size_t n, FailSum *s)
{
    s->n = (uint32_t)n;
    s->x_or = s->rose = s->fell = s->a_or = 0;
    s->x_and = s->a_and = 0xFFFFFFFFu;               /* identity for AND */
    for (size_t i = 0; i < n; i++) {
        uint32_t x = f[i].exp ^ f[i].act;
        s->x_or |= x;              s->x_and &= x;
        s->rose |= x & f[i].act;   s->fell  |= x & f[i].exp;
        s->a_and &= f[i].addr;     s->a_or  |= f[i].addr;
    }
}

typedef enum { DX_PASS, DX_ONE_CELL, DX_DATA_HI, DX_DATA_LO, DX_ADDR, DX_OTHER } Dx;

/* Data pattern assumed: own byte address (exp = BASE + 4*addr, then ~exp),
 * so on an alias exp ^ act = (addr ^ alias_addr) << 2. */
Dx fail_diagnose(const FailSum *s, uint32_t *which)
{
    if (s->n == 0) return DX_PASS;
    if (s->a_and == s->a_or) { *which = s->a_and; return DX_ONE_CELL; } /* one address */
    *which = s->x_or;
    if (s->fell == 0) return DX_DATA_HI;          /* only ever extra 1s: stuck-high/OR short */
    if (s->rose == 0) return DX_DATA_LO;          /* only ever missing 1s                    */
    if ((s->rose & s->fell) == s->x_or && (s->x_or & 3u) == 0) {
        *which = s->x_or >> 2;                    /* byte-address bits -> word-address lines */
        return DX_ADDR;                           /* every bad bit seen both ways            */
    }
    return DX_OTHER;                              /* coupling / random: run March C- next   */
}
```

- 시간 O(n) · 공간 O(1) (요약 구조체만).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

손 예시 3줄:

| # | addr | exp ^ act | rose | fell | x_or | x_and | a_and | a_or |
|---|---|---|---|---|---|---|---|---|
| 시작 | — | — | 0 | 0 | 0 | FFFFFFFF | FFFFFFFF | 0 |
| 1 | 0x10 | 0x10 | 0x10 | 0 | 0x10 | 0x10 | 0x10 | 0x10 |
| 2 | 0x24 | 0x10 | 0x10 | 0 | 0x10 | 0x10 | 0x00 | 0x34 |
| 3 | 0x3C | 0x10 | 0x10 | 0 | 0x10 | 0x10 | 0x00 | 0x3C |

주소 여러 곳 → ONE_CELL 아님 · `fell == 0` → DATA_HI, `which = 0x10` (D4).

A5 stuck-low (64 word, 별도 fill/verify 두 패스): 실패는 offset 0..31에서만 → `a_or = 31` (bit 5가 항상 0), `x_or = 0x80`, 참 패스에서 rose, 반전 패스에서 fell → ADDR, `0x80 >> 2 = 0x20` = A5 (검증함).

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 빈 목록 → PASS · 깨끗한 메모리 → PASS.
- D7 stuck-high → DATA_HI 0x80 (`x_and`도 0x80) · D20 stuck-low → DATA_LO.
- D3–D4 OR-short → DATA_HI 0x18, 그런데 `x_and = 0` (매번 같은 비트가 아님) — short의 지문.
- 셀 하나(0x42, bit 9) → ONE_CELL 0x42.
- A5 stuck-low → ADDR 0x20, A3 stuck-high → ADDR 0x08, A2–A3 OR-short → ADDR 0x0C.
- 섞인 쓰레기(비트마다 방향 제각각) → OTHER → March C-로 다음 단계.

</details>

### 4. 꼬리 질문

**Q1. You ran F3's combined verify-and-invert loop instead of separate passes. Does the address rule still work?**

<details><summary>답</summary>

"Not cleanly. In the combined loop the inverse is written right after each check, so a later aliased read can see the inverse of a different address, and the XOR becomes the complement of the address difference — many bits, not one. That's why for diagnosis I log from a test with separate fill and verify passes, or I classify the two passes separately."
- 한국어 핵심: **확인 직후 반전 쓰기가 섞이면 `exp ^ act` = 주소 차이의 보수 → 진단용 로그는 패스 분리.** (A5 stuck-low 64 word: 실패 96개 중 32개가 `~0x80`, 판정 OTHER — 검증함)

</details>

**Q2. What does `a_or` / `a_and` tell you that the XOR doesn't?**

<details><summary>답</summary>

"Which address bits are constant across every failure. If every failing address has bit 5 clear, the bad region is exactly the lower half of a 2^6 block — consistent with A5 aliasing. If every failure shares the same high bits, it points to one bank, row or chip, which is a different root cause than a wire. For DDR I'd decode those addresses into rank, bank, row and column first."
- 한국어 핵심: **실패 주소에서 항상 같은 비트 = 영역의 지문 → bank/row/칩 단위 고장 구분.**

</details>

**Q3. The log only keeps the first 32 failures. What breaks?**

<details><summary>답</summary>

"The AND masks get optimistic: with only the first failures, which are usually low addresses, bits look constant that wouldn't be over the full list. I'd keep the full counters — OR, AND, rose, fell — updated for every failure, and only cap the stored list. The summary is constant size, so there's no reason to cap it."
- 한국어 핵심: **목록은 cap, 요약 마스크는 모든 실패에 대해 계속 누적 (크기 고정).**

</details>

### 5. 채점 · 자주 틀리는 것

- AND 누적을 0에서 시작 → 항상 0.
- `rose`/`fell`을 exp/act 반대로 정의.
- 판정 순서 뒤바뀜: ONE_CELL을 먼저 보지 않으면 셀 하나의 stuck-at이 "데이터선"으로 보고됨.
- `x_or >> 2` (바이트 주소 → word 주소) 빠뜨림.
- "확정"이 아니라 **추정**이라고 말하기 — 다음 단계(다른 패턴, March C-, 핀 측정)를 같이 제안.

## 출처

1. Barr Group, "Fast, Efficient Memory Test Suite in C" — memTestDataBus / memTestAddressBus / memTestDevice: https://barrgroup.com/embedded-systems/how-to/memory-test-suite-c
2. Greg Cook, CRC RevEng catalogue — CRC-8/SMBUS `width=8 poly=0x07 init=0x00 refin=false refout=false xorout=0x00 check=0xf4 residue=0x00`: https://reveng.sourceforge.io/crc-catalogue/1-15.htm
3. Wikipedia, "Pseudorandom binary sequence" — PRBS7 = x⁷+x⁶+1, C example (start 0x02, period 127), ITU-T O.150 reference: https://en.wikipedia.org/wiki/Pseudorandom_binary_sequence
4. Wikipedia, "Fletcher's checksum" — Fletcher-16 reference code and test vectors "abcde"/"abcdef"/"abcdefgh": https://en.wikipedia.org/wiki/Fletcher%27s_checksum
5. Wikipedia, "Intel HEX" — record `:0300300002337A1E` checksum example: https://en.wikipedia.org/wiki/Intel_HEX
6. Wikipedia, "Hamming(7,4)" — layout p1 p2 d1 p3(=위치 4) d2 d3 d4, example 1011 → 0110011, syndrome = bit position: https://en.wikipedia.org/wiki/Hamming(7,4)
7. 내부 자료: [V1 March C- 엔진](hmScreenPrep/problems/V1_march_memtest/index.html) · [V6 PRBS/CRC-32](hmScreenPrep/problems/V6_prbs_crc_checker/index.html) · [BV09 DDR bring-up · 메모리 테스트](2026-11-01_BV09_ddr_bringup_training_memtest.html)
- 검증 메모: CRC-8의 "127비트 간격 2비트 에러만 미검출", PRBS7 run-length 성질, Hamming 2비트 오정정, 각 메모리 테스트의 고장별 결과는 출처 인용이 아니라 `onsitePrep/n05/f/`에서 직접 전수 실행한 결과. BER 신뢰도 "3/BER 비트(95%)", 8 GB 시간 추정은 일반적 경험칙 [추정].
