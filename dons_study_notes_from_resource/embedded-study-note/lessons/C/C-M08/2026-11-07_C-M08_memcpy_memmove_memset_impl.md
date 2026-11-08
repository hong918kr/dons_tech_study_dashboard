---
id: C-M08
time: 90분
updated: 2026-11-07
---
# C-M08 memcpy / memmove / memset 직접 구현

## 학습 목표

- 바이트 버전 `memcpy` / `memmove` / `memset` 을 표준 계약(겹침 UB, `restrict`, 반환값, `c` 의 `unsigned char` 변환)대로 구현하고, `memmove` 의 복사 방향을 **비교 한 번**으로 정할 수 있다.
- 워드 단위(word-at-a-time) 버전을 **머리(head) 정렬 → 워드 루프 → 꼬리(tail)** 구조로 쓰고, src/dst 의 상대 정렬이 어긋난 경우를 처리할 수 있다.
- `char` 버퍼를 `uint32_t` 로 읽고 쓰는 것이 ISO C 의 유효 타입(effective type) 규칙 위반이라는 점과, 실제 libc 가 `__attribute__((may_alias))` 로 이를 피하는 이유를 실제로 깨지는 예로 설명할 수 있다.
- 컴파일러가 내 루프를 `memcpy`/`memset` 호출로 바꾸는 현상(loop idiom recognition)을 asm 으로 확인하고 `-fno-builtin` / `-ffreestanding` 으로 막을 수 있다.
- xorshift32 고정 시드 퍼즈로 libc 와 수만 케이스를 대조하고, 벤치마크 숫자를 컴파일 플래그와 함께 해석할 수 있다 (그리고 언제 DMA 가 답인지).

## 1. 문제

`memcpy` 직접 구현은 펌웨어 면접의 단골이다 — 겹침(overlap), 정렬(alignment), 앨리어싱(aliasing), 컴파일러 최적화라는 네 개의 함정이 한 함수에 다 들어 있어서 지원자의 C 이해도를 한 번에 드러낸다. 부트로더·RTOS·베어메탈에서는 libc 없이 이 함수들을 **실제로** 직접 제공해야 하기도 한다.

세 함수의 계약부터 정리하자 (C11 7.24).

| 함수 | 시그니처 | 겹치면 | 핵심 |
|---|---|---|---|
| `memcpy` | `void *memcpy(void *restrict s1, const void *restrict s2, size_t n)` | **UB** (7.24.2.1p2) | 겹침 검사 없음 → 가장 빠를 수 있다 |
| `memmove` | `void *memmove(void *s1, const void *s2, size_t n)` | 정의됨 | "임시 배열에 먼저 복사한 것처럼" (7.24.2.2p2) |
| `memset` | `void *memset(void *s, int c, size_t n)` | — | `c` 를 **`unsigned char` 로 변환**해서 채움 (7.24.6.1p2) |

세 함수 모두 첫 인자(`dst`)를 그대로 반환한다. 바이트 루프는 쉽다. 면접관이 보고 싶은 건 그 다음 — "더 빠르게 해 보세요" 에서 나오는 워드 단위 버전이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">my_memcpy(dst=0x1001, src=0x2006, n=17) — 4바이트 워드 기준</text>
  <g stroke="currentColor">
    <rect x="50" y="40" width="32" height="30" class="f-mut"/>
    <rect x="82" y="40" width="32" height="30" class="f-warn"/><rect x="114" y="40" width="32" height="30" class="f-warn"/><rect x="146" y="40" width="32" height="30" class="f-warn"/>
    <rect x="178" y="40" width="128" height="30" class="f-acc"/><rect x="306" y="40" width="128" height="30" class="f-acc"/><rect x="434" y="40" width="128" height="30" class="f-acc"/>
    <rect x="562" y="40" width="32" height="30" class="f-warn"/><rect x="594" y="40" width="32" height="30" class="f-warn"/>
    <rect x="626" y="40" width="32" height="30" class="f-mut"/><rect x="658" y="40" width="32" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="54" y="60">00</text><text x="86" y="60">01</text><text x="118" y="60">02</text><text x="150" y="60">03</text>
    <text x="210" y="60">04..07</text><text x="338" y="60">08..0B</text><text x="466" y="60">0C..0F</text>
    <text x="566" y="60">10</text><text x="598" y="60">11</text><text x="630" y="60">12</text><text x="662" y="60">13</text>
  </g>
  <text x="10" y="60" fill="currentColor">dst</text>
  <g fill="currentColor">
    <text x="82" y="90">① 머리 3B</text>
    <text x="252" y="90">② 워드 루프: 4B × 3번 (정렬된 str)</text>
    <text x="560" y="90">③ 꼬리 2B</text>
    <text x="82" y="110" font-size="12">head = (-dst) &amp; 3 = 3</text>
    <text x="560" y="110" font-size="12">n 이 4 미만 남음</text>
  </g>
  <text x="10" y="148" fill="currentColor" font-weight="bold">src: 머리 3B 복사 후 s = 0x2009, (s &amp; 3) = 1 → dst 정렬, src 1B 어긋남</text>
  <g fill="currentColor">
    <text x="20" y="174">(s - d) &amp; 3 == 0 → 머리만 맞추면 둘 다 정렬 → word_t 로 읽고 쓴다</text>
    <text x="20" y="196">그 외 → 쓰기는 정렬(word_t), 읽기는 비정렬. 선택지 3가지:</text>
    <text x="40" y="218">a) 비정렬 로드 uword_t — arm64, Cortex-M3/M4/M7 LDR 이 처리 (정답 코드)</text>
    <text x="40" y="240">b) 시프트 병합(shift-merge) — 정렬 로드 2개를 합침, Cortex-M0 용 (연습 2)</text>
    <text x="40" y="262">c) 바이트 루프로 후퇴 — 가장 단순, 가장 느림</text>
  </g>
  <text x="10" y="290" fill="currentColor">워드 루프는 n &gt;= 4 일 때만 → 버퍼 밖 접근 0 (C-B07 워드 strlen 과 다른 점)</text>
</svg>
```

### Part A — 바이트 버전

- `my_memcpy_b(dst, src, n)` — 바이트 하나씩. 겹치지 않는다고 가정 (`restrict`).
- `my_memmove_b(dst, src, n)` — 겹칠 수 있다. `dst` 가 `src` 보다 뒤에서 겹치면 **뒤에서부터** 복사해야 한다. 방향 판단에 포인터 `<` 비교를 쓰지 말고 `uintptr_t` 로 한다 (이유는 풀이).
- `my_memset_b(dst, c, n)` — `c` 를 `unsigned char` 로 바꿔 채운다. `my_memset_b(p, 0x141, 3)` 는 `'A'`(0x41) 3개.

### Part B — 워드 단위 버전 (이 문제의 본체)

- `head_bytes(addr)` — 다음 4바이트 경계까지 남은 바이트 수 (정렬이면 0).
- `copy_fwd(d, s, n)` — 앞에서부터 복사하는 엔진: ① `dst` 가 정렬될 때까지 바이트 ② 워드 루프 — `src` 도 정렬이면 `word_t`, 아니면 비정렬 허용 `uword_t` 로 읽기 ③ 꼬리 바이트. `my_memcpy` 는 이것을 부르기만 한다 (주어짐).
- `my_memmove(dst, src, n)` — 앞에서부터가 안전하면 `copy_fwd`, 아니면 뒤에서부터: **dst 끝을 정렬** → 워드를 뒤로 → 머리 바이트.
- `my_memset(dst, c, n)` — 바이트를 워드 4칸에 복제한 패턴(`0xAB → 0xABABABAB`)으로 머리 → 워드 → 꼬리.
- 두 typedef 는 주어진다: `typedef uint32_t __attribute__((may_alias)) word_t;` 와 `aligned(1)` 을 더한 `uword_t`. 왜 필요한지가 풀이의 절반이다.
- 테스트: 길이 0..40 × src/dst 오프셋 0..7 **전수** + xorshift32 고정 시드 퍼즈 5,000회씩 (길이 0..300, 겹침 방향 3종) — 결과를 libc 와 **버퍼 전체** 비교해서 범위 밖 쓰기까지 잡는다.

### Part C — 펌웨어 장면: UART 수신 버퍼와 LCD 프레임버퍼

- `rx_consume(rb, n)` — 수신 버퍼 앞의 프레임 `n` 바이트를 처리했으니 버리고 나머지를 앞으로 당긴다 (`dst < src` 겹침).
- `rx_insert(rb, pos, p, k)` — `pos` 에 `k` 바이트를 끼워 넣는다 (예: 로그 줄 앞에 `[1]` 태그). 뒤를 밀어야 하므로 `dst > src` 겹침. 공간이 없거나 `pos > len` 이면 `-1`, 버퍼는 그대로.
- `fb_scroll_up(fb, rows, cols, k, bg)` — 8줄 × 20칸 문자 LCD 를 `k` 줄 위로 스크롤하고 아래 `k` 줄을 `bg` 로 지운다 (`memmove` + `memset`). `k > rows` 면 전부 지운다.
- 풀이에서 같은 일을 **DMA** 로 하면 무엇이 달라지는지 비교한다.

## 2. Problem (English)

> **Implement `memcpy`, `memmove`, `memset` — byte-wise first, then word-at-a-time.**
>
> 1. Byte versions `my_memcpy_b`, `my_memmove_b`, `my_memset_b` with the exact C11 semantics: `memcpy` may assume no overlap, `memmove` must behave as if copying through a temporary buffer, `memset` stores `(unsigned char)c`. All return `dst`.
> 2. Word versions `my_memcpy`, `my_memmove`, `my_memset` using 32-bit words: copy head bytes until `dst` is 4-byte aligned, then copy whole words, then the remaining tail bytes. If `src` is not aligned after the head, use an unaligned load. Never read or write outside `[src, src+n)` / `[dst, dst+n)`. For `memmove`, choose the direction with a single unsigned comparison and, for the backward case, align the *end* of `dst`.
> 3. Using them, implement a UART receive buffer (`rx_consume`, `rx_insert`) and a character-LCD `fb_scroll_up`.
>
> Constraints: no calls to libc `mem*` inside your implementations; must be clean under `-Wall -Wextra -Werror`, ASan and UBSan. Verify against libc with a deterministic fuzzer (fixed-seed xorshift32, thousands of random lengths/offsets/overlaps).
>
> Follow-ups: Why is `*(uint32_t *)buf` on an `unsigned char buf[]` undefined behavior even when `buf` is aligned? Why can `-O2` turn your `memset` into an infinite recursion? When would you use a DMA channel instead?
>
> Examples: `memmove(m + 2, m, 6)` on `"abcdefgh"` → `"ababcdef"`; `memmove(m, m + 2, 6)` → `"cdefghgh"`; `memset(p, 0x141, 3)` → `"AAA"`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# FAIL 줄이 쭉 나오고 "4/26 passed" — TODO 를 채워 26/26 을 만든다
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer starter.c -o st_san && ./st_san   # 다 채운 뒤 반드시
```

같은 폴더의 관찰용 파일 (하네스와 별개, 결과는 풀이에 실었다):

```bash
cc -std=c11 -O2 -S demo_idiom.c -o - | grep -E '^_|b(l)?\s+_'          # 루프 → memcpy/memset 호출 변환
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -ffreestanding -S demo_cm4.c -o -   # M4 워드 루프
cc -std=c11 -O2 demo_alias.c -o alias && ./alias                        # may_alias 없으면 틀린 값
cc -std=c11 -O2 -fno-builtin demo_bench.c -o bench && ./bench           # 벤치마크 (solution.c 를 #include)
cc -std=c11 -O2 -S demo_deadstore.c -o -                                # 연습 4: 사라지는 memset
```

## 4. 힌트

<details>
<summary>힌트 1 — memmove 방향은 비교 한 번</summary>

앞에서부터 복사가 **위험한 경우는 딱 하나**: `src < dst < src + n` (dst 가 src 의 뒤쪽과 겹침). 부호 없는 산술로 `(uintptr_t)d - (uintptr_t)s >= n` 이면 안전하다 — `d < s` 이면 뺄셈이 랩어라운드되어 아주 큰 수가 되고, `d >= s + n` 이면 안 겹친다. 이 둘이 한 비교에 들어간다.

</details>

<details>
<summary>힌트 2 — head_bytes 는 음수의 하위 비트</summary>

`addr = ...01` 이면 3, `...02` 면 2, `...03` 이면 1, `...00` 이면 0. 즉 `(4 - addr % 4) % 4` 이고, 2의 거듭제곱이므로 `(-addr) & 3` 한 줄. `uintptr_t` 는 부호 없는 타입이라 `-addr` 은 잘 정의된 모듈러 연산이다. 그리고 `h > n` 이면 `h = n` 으로 자르는 것을 잊지 말 것.

</details>

<details>
<summary>힌트 3 — 워드 접근은 typedef 로</summary>

`*(word_t *)d = *(const word_t *)s;` — `uint32_t *` 로 바로 캐스트하지 않는다. `src` 가 정렬인지는 머리를 복사한 **뒤의** `s` 로 판단: `((uintptr_t)s & 3) == 0`. 아니면 `*(const uword_t *)s` 로 읽는다 (쓰기 쪽 `d` 는 항상 정렬). 루프 조건은 `n >= 4` — `n > 0` 이면 마지막 워드가 버퍼를 넘는다.

</details>

<details>
<summary>힌트 4 — 뒤에서부터는 "끝" 을 정렬</summary>

`d += n; s += n;` 한 뒤, `d` 의 끝 주소 하위 2비트 `t = (uintptr_t)d & 3` 만큼 바이트로 `*--d = *--s` 하면 `d` 가 정렬된다. 그다음 `d -= 4; s -= 4;` 하며 워드, 마지막에 남은 머리 바이트. 한 워드 안에서는 **읽기를 먼저** 하고 쓰므로 겹쳐도 안전하다.

</details>

<details>
<summary>힌트 5 — memset 패턴과 Part C 순서</summary>

`word_t w = (word_t)(unsigned char)c * 0x01010101u;` — **먼저** `unsigned char` 로 줄인 뒤 곱한다 (`c = -1` 을 그대로 곱하면 틀린 패턴, 7. 자주 하는 실수). `rx_insert` 는 뒤를 먼저 밀고(`memmove`) 그다음 빈자리에 `memcpy`. 넘침 검사는 `rb->len + k > CAP` 대신 `k > CAP - rb->len` (덧셈 오버플로가 없다).

</details>

## 5. 풀이

### Part A — 바이트 버전과 겹침 방향

`memcpy_b` 와 `memset_b` 는 `while (n--) *d++ = *s++;` 한 줄이다. `memset` 의 `c` 는 `int` 지만 표준은 "`unsigned char` 로 변환한 값" 을 저장하라고 한다 (7.24.6.1p2) → `0x141` 은 `0x41`, `-1` 은 `0xFF`. 이 변환은 모듈러로 잘 정의돼 있다 (6.3.1.3p2).

`memmove` 는 방향이 전부다:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">memmove(m + 2, m, 6) on "abcdefgh" — dst 가 src 의 뒤쪽과 겹침</text>
  <text x="10" y="54" fill="currentColor">처음</text>
  <g stroke="currentColor" class="f-mut">
    <rect x="130" y="36" width="30" height="26"/><rect x="160" y="36" width="30" height="26"/><rect x="190" y="36" width="30" height="26"/><rect x="220" y="36" width="30" height="26"/>
    <rect x="250" y="36" width="30" height="26"/><rect x="280" y="36" width="30" height="26"/><rect x="310" y="36" width="30" height="26"/><rect x="340" y="36" width="30" height="26"/>
  </g>
  <g fill="currentColor" font-family="monospace"><text x="140" y="54">a</text><text x="170" y="54">b</text><text x="200" y="54">c</text><text x="230" y="54">d</text><text x="260" y="54">e</text><text x="290" y="54">f</text><text x="320" y="54">g</text><text x="350" y="54">h</text></g>
  <text x="390" y="46" fill="currentColor" font-size="12">src = [0..6) = abcdef</text>
  <text x="390" y="62" fill="currentColor" font-size="12">dst = [2..8)</text>
  <text x="10" y="104" fill="currentColor">앞에서부터 ✘</text>
  <g stroke="currentColor">
    <rect x="130" y="86" width="30" height="26" class="f-mut"/><rect x="160" y="86" width="30" height="26" class="f-mut"/>
    <rect x="190" y="86" width="30" height="26" class="f-warn"/><rect x="220" y="86" width="30" height="26" class="f-warn"/><rect x="250" y="86" width="30" height="26" class="f-warn"/>
    <rect x="280" y="86" width="30" height="26" class="f-warn"/><rect x="310" y="86" width="30" height="26" class="f-warn"/><rect x="340" y="86" width="30" height="26" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace"><text x="140" y="104">a</text><text x="170" y="104">b</text><text x="200" y="104">a</text><text x="230" y="104">b</text><text x="260" y="104">a</text><text x="290" y="104">b</text><text x="320" y="104">a</text><text x="350" y="104">b</text></g>
  <text x="390" y="96" fill="currentColor" font-size="12">1단계 m[2]=m[0] 이 아직 안 읽은 'c' 를 덮는다</text>
  <text x="390" y="112" fill="currentColor" font-size="12">→ "abababab" (복사본이 자기 자신을 복사)</text>
  <text x="10" y="154" fill="currentColor">뒤에서부터 ✔</text>
  <g stroke="currentColor">
    <rect x="130" y="136" width="30" height="26" class="f-mut"/><rect x="160" y="136" width="30" height="26" class="f-mut"/>
    <rect x="190" y="136" width="30" height="26" class="f-ok"/><rect x="220" y="136" width="30" height="26" class="f-ok"/><rect x="250" y="136" width="30" height="26" class="f-ok"/>
    <rect x="280" y="136" width="30" height="26" class="f-ok"/><rect x="310" y="136" width="30" height="26" class="f-ok"/><rect x="340" y="136" width="30" height="26" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace"><text x="140" y="154">a</text><text x="170" y="154">b</text><text x="200" y="154">a</text><text x="230" y="154">b</text><text x="260" y="154">c</text><text x="290" y="154">d</text><text x="320" y="154">e</text><text x="350" y="154">f</text></g>
  <text x="390" y="146" fill="currentColor" font-size="12">m[7]=m[5], m[6]=m[4], ... 덮이는 칸은</text>
  <text x="390" y="162" fill="currentColor" font-size="12">이미 읽은 칸뿐 → "ababcdef"</text>
  <line x1="10" y1="186" x2="730" y2="186" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="10" y="210" fill="currentColor" font-weight="bold">방향 규칙 (비교 한 번):  (uintptr_t)d - (uintptr_t)s &gt;= n  ?  앞에서부터  :  뒤에서부터</text>
  <g fill="currentColor">
    <text x="20" y="236">d &lt; s            → 뺄셈이 랩어라운드(큰 값) → 앞에서부터 (쓰는 칸은 이미 읽은 칸)</text>
    <text x="20" y="258">s ≤ d, d - s ≥ n   → 안 겹침 → 앞에서부터</text>
    <text x="20" y="280">s &lt; d &lt; s + n     → 뒤쪽 겹침 → 뒤에서부터 (Part C 의 rx_insert 가 이 경우)</text>
  </g>
</svg>
```

**왜 `uintptr_t` 인가.** `d < s` 처럼 포인터를 관계 연산자로 비교하는 것은 **같은 배열(객체) 안의 포인터끼리만** 정의된다 (C11 6.5.8p5). `memmove` 는 서로 다른 두 객체를 받을 수 있으니, 엄밀히는 포인터 `<` 가 UB 다. 정수로 바꾼 비교는 포인터→정수 변환이 **구현 정의**(6.3.2.3p6)일 뿐이고, 평평한 주소 공간인 모든 현실 타깃(arm64, Cortex-M)에서 기대대로 동작한다. musl 의 `memmove` 도 `(uintptr_t)s-(uintptr_t)d-n <= -2*n` 같은 부호 없는 비교 한 번으로 "memcpy 로 넘겨도 되나" 를 판단한다.

> `memmove` 를 "임시 버퍼에 복사 후 다시 복사" 로 구현하면 표준 문장 그대로지만, MCU 에서 `n` 바이트 VLA/`malloc` 은 스택·힙을 터뜨린다. 방향 선택이 정답이다.

### Part B — 워드 단위: 머리 · 몸통 · 꼬리

**① 머리.** `head_bytes(a) = (-a) & 3`. 표로 확인:

| 주소 하위 | `-a` 하위 2비트 | 머리 | 정렬 후 |
|---|---|---|---|
| `...00` | 00 | 0 | 그대로 |
| `...01` | 11 | 3 | `...04` |
| `...10` | 10 | 2 | `...04` |
| `...11` | 01 | 1 | `...04` |

`n` 이 머리보다 짧으면(`n = 2`, 머리 3) 머리를 `n` 으로 잘라야 한다 — 이걸 빠뜨리면 `n -= h` 가 언더플로해서 워드 루프가 수 엑사바이트를 돈다. 테스트의 "길이 0..40 × 오프셋 0..7 전수" 가 이 경계를 다 밟는다.

**② 왜 dst 를 정렬하나.** 쓰기가 비정렬이면 한 번의 `str` 이 두 워드(때로는 두 캐시 라인)에 걸치고, 일부 코어에서는 폴트다. 그래서 **쓰기 쪽을 먼저 맞추고**, 읽기 쪽은 상대 정렬 `(s - d) & 3` 에 따라 갈린다:

| 머리 후 `s & 3` | 읽기 방법 | arm64 / Cortex-M4 | Cortex-M0 |
|---|---|---|---|
| 0 (상대 정렬 일치) | `*(const word_t *)s` | `ldr` | `ldm r1!,{r3}` |
| 1..3 | `*(const uword_t *)s` (aligned(1)) | `ldr` 그대로 (하드웨어가 비정렬 처리) | **`ldrb` 4개 + 시프트 3개** |
| 1..3 | 시프트 병합 (연습 2) | 정렬 `ldr` + `lsr`/`lsl`/`orr` | 정렬 `ldm` + 시프트 |

`demo_cm4.c` 를 `-Os -ffreestanding` 으로 컴파일한 실제 출력 (지시어 줄 생략):

```text
; --target=thumbv7em-none-eabi -mcpu=cortex-m4        ; --target=thumbv6m-none-eabi -mcpu=cortex-m0
copy_words_u:                                          copy_words_u:
	cbz	r2, .LBB1_2                                    	ldrb	r3, [r1]
.LBB1_1:                                               	ldrb	r4, [r1, #1]
	ldr	r3, [r1], #4      ; 비정렬이어도 ldr 하나         	lsls	r4, r4, #8
	subs	r2, #1                                      	adds	r3, r4, r3
	str	r3, [r0], #4                                   	ldrb	r4, [r1, #2] ... (총 ldrb 4, lsls 3, adds 3)
	bne	.LBB1_1                                        	stm	r0!, {r3}
```

Cortex-M3/M4/M7 의 `LDR/STR/LDRH/STRH` 는 `CCR.UNALIGN_TRP = 0`(기본값)일 때 비정렬 주소를 하드웨어가 처리한다 (여러 버스 사이클로 쪼개 느리다). 단 `LDM/STM/LDRD/STRD/LDREX` 는 비정렬이면 항상 UsageFault. Cortex-M0/M0+ (ARMv6-M) 는 비정렬 접근 자체가 HardFault 라, 컴파일러는 `aligned(1)` 로드를 바이트 4개로 쪼갰다 — 그러면 워드화의 이득이 사라지므로 M0 용 libc 는 시프트 병합을 쓴다 (musl `memcpy.c` 의 `switch ((uintptr_t)d % 4)` 가 이 방식).

**③ 꼬리.** 루프 조건이 `n >= 4` 이므로 워드 루프는 절대 범위 밖을 건드리지 않는다. C-B07 의 워드 `strlen` 은 길이를 몰라 "정렬된 워드는 페이지를 안 넘는다" 는 핑계로 끝을 넘어 읽지만(UB, ASan 보고), `mem*` 은 `n` 을 알기 때문에 그럴 필요가 없다.

**④ 뒤에서부터 (`memmove`).** 같은 구조를 거꾸로: `d += n; s += n;` → 끝 정렬 바이트 `t = d & 3` → `d -= 4; s -= 4;` 워드 → 머리 바이트. 한 반복 안에서 `*(word_t *)d = *(const uword_t *)s;` 는 **로드가 끝난 뒤 스토어** 하므로, 4바이트 미만으로 겹쳐도(예: `d = s + 1`) 안전하다. 앞쪽 엔진 `copy_fwd` 는 `restrict` 가 없어야 `memmove` 의 `d < s` 경우에 재사용할 수 있다 — `restrict` 포인터 둘이 겹친 바이트를 (한쪽이 수정하며) 접근하면 UB 다 (6.7.3.1).

**⑤ memset.** `(word_t)(unsigned char)c * 0x01010101u` 는 바이트를 네 칸에 복제한다 (`0xAB × 0x01010101 = 0xABABABAB`, 자리올림 없음). 64비트 워드면 `0x0101010101010101`, 이식성 있게 쓰면 `(word_t)-1 / 0xFF * b`.

#### 유효 타입(effective type)과 `may_alias` — 정직하게

`unsigned char buf[64]` 를 `*(uint32_t *)buf` 로 접근하면 **정렬이 맞아도 ISO C 에서는 UB** 다:

- C11 6.5p6: 선언된 객체의 유효 타입은 선언 타입 (`unsigned char`).
- C11 6.5p7: 객체는 호환 타입, 부호만 다른 타입, 그것을 담은 집합체, 또는 **문자 타입** lvalue 로만 접근해야 한다. `char` 는 무엇이든 읽고 쓸 수 있지만, 반대 방향(`char` 배열을 `uint32_t` 로)은 허용 목록에 없다.
- 덤으로 비정렬 주소를 `uint32_t *` 로 **변환하는 것만으로도** UB (6.3.2.3p7) — 접근하기 전에.

그런데 실제 libc 는 정확히 그 일을 한다. 방법은 셋이다: ① 어셈블리로 작성 (glibc, Apple, ARM 최적화 라이브러리 대부분) ② `typedef uint32_t __attribute__((__may_alias__)) u32;` 같은 GNU 확장 (musl `src/string/memcpy.c`) ③ 라이브러리 전체를 `-fno-strict-aliasing` 으로 빌드 (Linux 커널은 전체가 이 플래그). `may_alias` 는 GCC 문서 표현으로 "이 타입을 통한 접근은 타입 기반 앨리어스 분석(TBAA)의 대상이 아니며, 다른 어떤 타입의 객체와도 앨리어스될 수 있다고 가정" 한다 — 즉 `char` 처럼 취급된다. ISO C 가 아니라 **GNU C 방언에서 정의된 동작** 이라는 점을 면접에서 그대로 말하면 된다. 정렬은 `may_alias` 가 해결해 주지 않으므로 `aligned(1)` 을 따로 붙인다 (typedef 에서는 정렬을 낮출 수 있다).

진짜로 깨지는지 `demo_alias.c` 로 확인했다. 같은 RGB565 프레임버퍼를 `uint16_t *px` 와 `void *fb` 두 포인터로 받아, `px[0] = 0xF800` 한 뒤 `fb` 를 워드로 0 채우고 `px[0]` 을 읽는다 (실제 출력):

```text
== -O0
plain uint32_t* : px[0] = 0x0000
may_alias word_t: px[0] = 0x0000
== -O2
plain uint32_t* : px[0] = 0xF800      ← 0 으로 지웠는데 빨강이 그대로
may_alias word_t: px[0] = 0x0000
== -O2 -fno-strict-aliasing
plain uint32_t* : px[0] = 0x0000
may_alias word_t: px[0] = 0x0000
```

`-O2` asm 이 이유를 보여 준다:

```text
_clear_then_read_plain:                 _clear_then_read_alias:
	mov	w8, #63488     ; 0xF800          	mov	w8, #63488
	strh	w8, [x0]                        	strh	w8, [x0]
	str	wzr, [x1]      ; 워드 0 쓰기        	str	wzr, [x1]
	mov	w0, #63488     ; 다시 안 읽고 상수!  	ldrh	w0, [x0]       ; 메모리에서 다시 읽음
	ret                                     	ret
```

TBAA: "`uint32_t` 스토어는 `uint16_t` 객체를 바꿀 수 없다" → 방금 쓴 0xF800 을 그대로 반환. 주의 — 같은 코드를 `-O1 -fsanitize=address,undefined` 로 빌드하면 0x0000 이 나오고 **아무 보고도 없다**. UBSan 에는 strict aliasing 검사가 없고, 이 맥의 Apple clang 21 은 `-fsanitize=type`(TySan) 플래그는 받지만 런타임 라이브러리가 없어 링크에 실패했다. 이런 버그는 테스트가 아니라 **코드 리뷰와 규칙** 으로 막아야 한다. 더 깊은 punning 이야기는 C-M13.

> 참고: 표준은 `memcpy`/`memmove` 에 특별한 지위를 준다 — 선언 타입이 없는 객체(malloc 메모리)에 `memcpy` 로 복사하면 유효 타입이 원본 것을 따라간다 (6.5p6). 직접 만든 `my_memcpy` 는 이 특권이 없다. 이것도 libc 함수가 "그냥 C 함수" 가 아닌 이유다.

#### 컴파일러가 내 루프를 `memcpy` 로 바꾼다

`solution.c` 를 그냥 `-O2` 로 컴파일한 실제 asm (발췌):

```text
_my_memcpy_b:                       ; 바이트 루프 전체가...
	cbz	x2, LBB0_2
	b	_memcpy                     ; ...libc memcpy 로의 꼬리 호출(tail call)
_my_memcpy:                         ; 워드 버전도
	...                             ; 머리 바이트 루프 → bl _memcpy
	bl	_memcpy                     ; 워드 루프       → bl _memcpy
	ldrb/strb ×3                    ; 꼬리만 남음
_my_memset:
	bl	_memset                     ; 두 군데
```

LLVM 의 루프 관용구 인식(LoopIdiomRecognize)이 "연속 복사/연속 같은 값 저장" 루프를 라이브러리 호출로 바꾼 것이다. 호스트에서는 무해하다 — 오히려 빠르다. 하지만 **내가 libc 를 제공하는 쪽** 이면 사정이 다르다. `demo_idiom.c` 는 함수 이름을 진짜 `memset`, `memmove` 로 지었다 (실제 출력):

```text
== arm64 -O2                         == thumbv7em (Cortex-M4) -O2
_memset:        (변환 안 됨)          memset:          (변환 안 됨)
_copy_fwd_bytes:  b _memcpy          copy_fwd_bytes:  b  __aeabi_memcpy
_memmove:         b _memcpy          memmove:         bl __aeabi_memcpy
_zero_bss:        b _bzero           zero_bss:        bl __aeabi_memclr4
== -fno-builtin (arm64) / -ffreestanding (thumb): 위의 호출이 모두 사라지고 루프 그대로
```

관찰:

1. 함수 이름이 정확히 `memset`/`memcpy` 이면 clang 은 변환하지 않았다 (LoopIdiomRecognize 에 "함수 이름이 memset/memcpy 면 건너뜀" 검사가 있다). 하지만 **헬퍼 함수** (`copy_fwd_bytes`) 나 **다른 이름의 함수** (`memmove`, `zero_bss`) 는 그대로 바뀐다.
2. 그래서 `memcpy` 를 `return memmove(d, s, n);` 으로 구현한 작은 libc 라면 `memmove → memcpy → memmove → ...` **상호 무한 재귀** 로 스택 오버플로 → HardFault. GCC 도 `-ftree-loop-distribute-patterns` 로 같은 변환을 하며, "memcpy 구현이 memcpy 호출로 최적화됨" 이라는 버그 보고가 실제로 있다 (GCC Bugzilla 56888). glibc 는 이를 막으려고 `inhibit_loop_to_libcall` 속성 매크로를 쓴다.
3. Cortex-M 타깃은 ARM 런타임 ABI 의 `__aeabi_memcpy`, `__aeabi_memclr4` (4바이트 정렬 버전) 를 부른다. 링커 스크립트·startup 에서 `.bss` 를 지우는 루프 (C-B11 Part C) 가 `__aeabi_memclr4` 가 된다는 뜻 — 그 함수가 링크되어 있어야 한다.
4. 해결: libc·startup 코드는 `-ffreestanding` (clang/GCC 에서 `-fno-builtin` 을 함의) 또는 `-fno-builtin`, GCC 는 추가로 `-fno-tree-loop-distribute-patterns`. 그래도 GCC 문서는 "freestanding 환경도 `memcpy`, `memmove`, `memset`, `memcmp` 는 제공해야 한다" 고 못 박는다 — 구조체 대입 등에서 컴파일러가 직접 호출을 만들기 때문이다.

#### 퍼즈 테스트 설계

- **결정적 난수**: xorshift32 (`x ^= x<<13; x ^= x>>17; x ^= x<<5`), 시드 `0xC0FFEE11` 고정 → 실패가 나면 매번 같은 케이스로 재현된다. `rand()` 는 구현마다 수열이 달라 피했다.
- **오라클(oracle)**: 같은 초기 내용의 버퍼 두 개에 내 함수와 libc 를 각각 적용한 뒤, 복사 범위만이 아니라 **512바이트 버퍼 전체** 를 `memcmp` → 범위 밖 쓰기(off-by-one)도 잡힌다. 반환값 `== dst` 도 함께 검사.
- **분포**: 길이 0..300 (워드 0~75개 + 꼬리), 오프셋 0..15 → 16×16 = 모든 정렬 조합. `memmove` 는 절반을 `|dst - src| ≤ 15` 로 뽑아 심한 겹침을 강제했고, 방향별 개수를 세어 CHECK 한다. 실제 출력: `memmove fuzz: fwd-overlap 1944, bwd-overlap 2027, disjoint 1029`.
- **규모**: Part A 2,000×3 + 전수 41×8×8×3 = 7,872 + Part B 5,000×3 → 약 **29,000 케이스**, `-O2` 에서 순식간.
- **테스트를 테스트하기(mutation)**: 정답을 일부러 망가뜨려 보았다. `memmove` 를 항상 앞에서부터로 바꾸면 → `21/26 passed` (퍼즈·Part C 가 잡음). 비정렬 워드 루프 조건을 `n >= 1` 로 바꾸면 → ASan `global-buffer-overflow ... in copy_fwd`. 정확한 크기의 `malloc` 블록을 src 로 쓰는 이유는 연습 2 답 참고.

#### 벤치마크 — 실제 숫자

`demo_bench.c` 는 `solution.c` 를 `MEMLAB_NO_MAIN` 으로 `#include` 해서 **같은 코드** 를 잰다. 함수는 `volatile` 함수 포인터로 불러 크기 특수화·호출 제거를 막고, 총 256 MiB 를 옮기는 반복을 3번 해 최고값(GB/s, 10⁹ B/s)을 취했다. 기계: `sysctl machdep.cpu.brand_string` = **Apple M6**, 32 GB, Apple clang 21.0.0 (clang-2100.0.123.102), arm64. 실행마다 ±10~20% 흔들리므로 자릿수와 경향만 본다.

memcpy, 정렬된 버퍼 (GB/s, libc / byte / word):

| 크기 | ① `-O2` | ② `-O2 -fno-builtin` | ③ ② + `-fno-vectorize -fno-slp-vectorize` |
|---|---|---|---|
| 16 B | 10.6 / 8.5 / 6.3 | 10.3 / 11.8 / 7.3 | 10.6 / 7.3 / 8.4 |
| 4 KiB | 134.9 / 98.4 / 98.8 | 94.9 / 97.6 / 91.2 | 127.0 / **10.7** / **15.2** |
| 64 KiB | 123.4 / 106.7 / 105.9 | 93.3 / 89.6 / 113.5 | 123.4 / **10.7** / **16.6** |
| 1 MiB | 112.0 / 88.0 / 108.6 | 81.0 / 89.8 / 74.3 | 104.6 / 11.9 / 17.0 |
| 32 MiB | 55.6 / 56.9 / 60.0 | 52.9 / 52.2 / 47.4 | 60.1 / 11.3 / 14.2 |

빌드 ③ (순수 스칼라) 의 나머지, 64 KiB: memcpy `dst+1, src+3` 비정렬 102.0 / 11.6 / 16.8, memmove 겹침(`d = s+5`) 98.9 / 7.1 / 15.2, memset 141.7 / **4.35** / **17.7**.

해석:

- **① 은 내 코드를 잰 게 아니다.** 바이트·워드 버전이 libc 호출로 바뀌어 libc 와 비슷하게 나온다. `-fno-builtin` 없이 잰 `my_memcpy` 벤치마크는 거짓말이다.
- **② 에서는 바이트 루프도 빠르다.** 변환 대신 자동 벡터화가 `ldp q0, q1` / `stp q0, q1` (한 반복 64 B) 루프를 만들었다 (asm 확인). 워드 버전은 오히려 비정렬 256 B 에서 39 vs 85 GB/s 로 바이트 버전보다 느렸다 — 머리/꼬리 분기와 벡터화 런타임 검사 비용. **SIMD 가 있는 호스트에서 "손으로 워드화" 의 이득은 컴파일러가 이미 가져간다.**
- **③ 이 Cortex-M 에 가까운 그림이다** (NEON 없음 = 스칼라만). memset: 바이트 4.35 → 워드 17.7 GB/s ≈ **4배** (스토어 하나가 1 B → 4 B, 사이클당 스토어 1개 수준). memcpy 바이트가 10.7 로 memset 바이트보다 빠른 건 clang 이 바이트 루프를 `ldrb`+`ldurh` (반복당 3 B) 로 합쳤기 때문 (asm 확인). 워드 대비 libc 가 7~8배 빠른 것은 NEON 128비트 레지스터 쌍(반복당 32~64 B)과 펼치기 덕분.
- 32 MiB 에서는 모든 memcpy 가 50~60 GB/s 로 수렴 — 캐시를 넘으면 메모리 대역폭이 상한이다. 16 B 는 호출 오버헤드(~1.5 ns)가 지배한다.
- Cortex-M4 (스칼라, 32비트 버스, 0-wait SRAM) 에서도 워드 루프는 바이트 루프보다 대략 워드 폭만큼 빠르고, 펼치기와 `LDM/STM` 다중 레지스터로 조금 더 짜낸다. 반대로 newlib 의 범용 C `memcpy` 는 크기 우선 빌드(`PREFER_SIZE_OVER_SPEED` / `__OPTIMIZE_SIZE__`)에서 **바이트 루프** 로 컴파일된다 — 플래시가 귀한 MCU 의 선택이다.

### Part C — UART 버퍼, 프레임버퍼, 그리고 DMA

```c
size_t rx_consume(rxbuf_t *rb, size_t n) {
    if (n >= rb->len) { rb->len = 0; return 0; }
    my_memmove(rb->buf, rb->buf + n, rb->len - n);   /* dst < src: 앞에서부터 */
    rb->len -= n;
    return rb->len;
}
int rx_insert(rxbuf_t *rb, size_t pos, const uint8_t *p, size_t k) {
    if (pos > rb->len || k > RX_CAP - rb->len) return -1;  /* 덧셈 대신 뺄셈: 오버플로 없음 */
    my_memmove(rb->buf + pos + k, rb->buf + pos, rb->len - pos);  /* dst > src: 뒤에서부터 */
    my_memcpy(rb->buf + pos, p, k);
    ...
```

`rx_insert(rb, 0, "[1]", 3)` 은 `"HELLO"` 를 3칸 뒤로 미는 **뒤쪽 겹침** 이다 — 바이트 버전의 그림과 똑같은 상황이고, 앞에서부터 복사하면 `"[1]HELHE"` 같은 쓰레기가 된다. `fb_scroll_up` 은 `k*cols` 바이트 떨어진 같은 버퍼 안의 앞쪽 이동(`memmove`) + 아래 줄 채우기(`memset`). `k > rows` 클램프가 없으면 `(rows - k) * cols` 가 `size_t` 언더플로로 거대한 길이가 된다.

> 실무 메모: 수신 버퍼를 매번 `memmove` 로 당기는 건 O(남은 길이) 복사다. 프레임이 잦으면 링 버퍼(읽기/쓰기 인덱스만 이동)가 낫다. `memmove` 방식은 파서가 "버퍼 앞에서부터 연속된 바이트" 를 원할 때(예: 길이 필드 파싱, `strtol` 류 호출)의 단순한 선택이다.

**레지스터에는 `memcpy` 금지.** `memcpy` 는 접근 폭·횟수·순서·비정렬 여부를 마음대로 정한다 (위에서 본 것처럼 1 B, 3 B 합치기, 16 B NEON 까지). 32비트 접근만 허용하는 주변장치 레지스터, 같은 주소에 계속 써야 하는 TX FIFO, Device 메모리(비정렬 접근 금지)에는 C-B09 의 `volatile` 32비트 루프를 쓴다. 비슷하게 `memset` 으로 W1C 레지스터 블록을 "지우는" 코드도 위험하다 (C-M02).

**DMA 라는 대안.** 큰 블록을 CPU 대신 DMA 컨트롤러가 옮기게 할 수 있다.

| | CPU `memcpy` | DMA 메모리→메모리 |
|---|---|---|
| 설정 비용 | 없음 | 채널 레지스터 설정 + 완료 인터럽트/폴링 (수십~수백 사이클) |
| 작은 블록 (수십 B) | 이득 | 손해 (설정 비용이 복사보다 큼) |
| 큰 블록 | CPU 가 묶임 | CPU 는 다른 일·WFI 저전력 대기 가능 |
| 버스 | — | CPU 와 버스 매트릭스 경합, 우선순위 설정 필요 |
| 캐시 (Cortex-M7) | 일관성 자동 | **clean 후 시작, invalidate 후 읽기**, 32 B 라인 정렬 (C-B09 Q6) |
| 완료 전 버퍼 | — | 건드리면 안 됨 (수명·스코프 주의 — 스택 버퍼 금지) |
| 칩 제약 | 없음 | STM32F4 는 DMA2 만 메모리→메모리 가능. 프레임버퍼 채우기·복사는 DMA2D (memset 에 해당하는 register-to-memory 모드). nRF52 처럼 주변장치별 EasyDMA 만 있고 범용 M2M 채널이 없는 칩도 있다 |

판단 기준: "크기가 크고, 그동안 CPU 가 할 일이 있거나 자고 싶을 때" DMA. 수백 바이트 이하나 결과를 바로 써야 하면 `memcpy`. 실제 드라이버 초기화의 DMA 채널 할당·해제 순서는 C-M15.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
#   memmove fuzz: fwd-overlap 1944, bwd-overlap 2027, disjoint 1029
#   26/26 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 26/26, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 겹치는 영역에 `memcpy` | UB (7.24.2.1p2). "지금 libc 에선 되던데" 는 근거가 아니다 — glibc 2.13 이 x86-64 `memcpy` 를 뒤에서부터 복사하도록 바꾸자, 겹친 `memcpy` 를 쓰던 Adobe Flash 플러그인 소리가 깨진 유명한 사례 | `memmove` |
| `if (d < s)` 로 방향 판단 | 서로 다른 객체 포인터의 관계 비교는 UB (6.5.8p5) | `(uintptr_t)d - (uintptr_t)s >= n` |
| `*(uint32_t *)p` 로 워드 접근 | 유효 타입 위반 (6.5p7) → `-O2` 에서 실제로 틀린 값 (demo_alias). 비정렬이면 변환부터 UB (6.3.2.3p7), M0 HardFault | `may_alias` typedef, 비정렬은 `aligned(1)` |
| 워드 루프 `while (n > 0)` / `n >= 1` | 마지막 워드가 버퍼를 1~3 B 넘어 읽고 쓴다 | `while (n >= 4)` + 꼬리 바이트 루프 |
| 머리 길이를 `n` 으로 자르지 않음 | `n = 2`, 머리 3 → `n -= 3` 언더플로 → 메모리 전체를 쓸어버림 | `if (h > n) h = n;` |
| `w = (uint32_t)c * 0x01010101u` | `c = -1` → `0xFFFFFFFF × 0x01010101 = 0xFEFEFEFF` (mod 2³²) — 틀린 패턴. `c = 0x141` 도 틀림 | 먼저 `(unsigned char)c` |
| `memmove` 를 임시 버퍼(VLA/malloc)로 | MCU 에서 스택/힙 고갈, 할당 실패 처리 불가 | 방향 선택 |
| `-fno-builtin` 없이 벤치마크 | 내 루프가 libc 호출로 바뀌어 libc 를 잰다 (빌드 ①) | `-fno-builtin` (+ 필요시 벡터화 끄기) |
| 직접 만든 libc 를 기본 플래그로 빌드 | 루프 → `memcpy` 호출 → 상호 재귀 → 스택 오버플로 | `-ffreestanding` / `-fno-builtin` (+ GCC `-fno-tree-loop-distribute-patterns`) |
| 비밀 키를 `memset(key, 0, n)` 로 지우고 반환 | 이후 읽지 않는 저장은 dead store 로 삭제됨 (연습 4 asm) | `memset_explicit` (C23), `memset_s` (Annex K), volatile 루프 |
| 레지스터 블록에 `memcpy`/`memset` | 접근 폭·횟수·순서가 보장되지 않음 | `volatile` 32비트 루프 (C-B09) |
| `memset(&s, 0, sizeof s)` 로 포인터·`float` 멤버를 "NULL·0.0" 으로 믿음 | ISO C 는 모든 비트 0 이 널 포인터·0.0 이라 보장하지 않는다 (현실 타깃은 대부분 그렇다) | 초기화는 `= {0}`; `memset` 은 패딩까지 지울 때 |

## 8. 면접 Q&A

<details>
<summary>Q1. memcpy 와 memmove 의 차이는? memcpy 가 따로 있는 이유는?</summary>

`memmove` 는 겹쳐도 "임시 배열을 거친 것처럼" 동작하고, `memcpy` 는 겹치면 UB 다. `memcpy` 의 매개변수에는 `restrict` 가 붙어 있어 구현이 방향 검사 없이, 또 자유로운 순서(뒤에서부터, 큰 블록 단위)로 복사할 수 있다. 실제로는 방향 검사 비용이 비교 한 번이라 많은 libc 가 둘을 거의 같은 코드로 구현하지만, 계약은 다르다 — 그래서 glibc 가 `memcpy` 순서를 바꿨을 때 겹친 `memcpy` 를 쓰던 프로그램이 깨졌다. 꼬리질문 "그럼 항상 memmove 쓰면 되나?" → 정확성 면에선 안전하다. 차이는 비교 한 번의 비용과 "겹치지 않는다" 는 의도 표현 정도. 반대로 겹칠 수 있는 곳에 `memcpy` 를 쓰는 것은 언제든 깨질 수 있는 버그다.

</details>

<details>
<summary>Q2. memmove 의 복사 방향을 어떻게 정하나요? 포인터를 그냥 < 로 비교하면 안 되나요?</summary>

위험한 건 `src < dst < src+n` 하나뿐이고, `(uintptr_t)dst - (uintptr_t)src >= n` 이면 앞에서부터, 아니면 뒤에서부터. 포인터 `<` 는 같은 배열 안의 포인터끼리만 정의(6.5.8p5)라서 두 인자가 다른 객체일 수 있는 `memmove` 에서는 엄밀히 UB 다. `uintptr_t` 변환은 구현 정의지만 평평한 주소 공간에서는 의도대로 동작하고, 실제 libc(musl 등)도 부호 없는 비교를 쓴다.

</details>

<details>
<summary>Q3. 워드 단위 복사에서 char 버퍼를 uint32_t* 로 캐스트해도 되나요?</summary>

ISO C 로는 안 된다: ① 유효 타입 규칙(6.5p7) — `char` 배열을 `uint32_t` lvalue 로 접근하는 건 허용 목록에 없다 ② 비정렬이면 포인터 변환 자체가 UB(6.3.2.3p7). 실측으로 `-O2` 에서 `uint32_t*` 스토어 뒤의 `uint16_t` 읽기가 옛 값을 반환했다 (TBAA). 실무 해법: GNU C `__attribute__((may_alias))` typedef (musl), 어셈블리 (glibc·Apple·ARM), 또는 `-fno-strict-aliasing` (Linux 커널). 이식성 있는 사용자 코드라면 `memcpy(&w, p, 4)` 가 정답이고 컴파일러가 `ldr` 하나로 만든다 — 다만 `memcpy` **를 구현하는 중** 에는 그걸 쓸 수 없다.

</details>

<details>
<summary>Q4. 직접 만든 memset 이 -O2 에서 스택 오버플로를 냈습니다. 왜죠?</summary>

컴파일러의 루프 관용구 인식이 바이트 루프를 `memset`/`memcpy` 호출로 바꿨기 때문이다. 내가 libc 라면 그 호출은 나 자신(또는 서로)을 부른다. clang 은 함수 이름이 정확히 `memset`/`memcpy` 일 때는 건너뛰지만, 헬퍼 함수나 `memmove` 안의 루프는 바꾼다 (demo_idiom: `memmove → b _memcpy`, Cortex-M 은 `__aeabi_memcpy`/`__aeabi_memclr4`). 해결: `-ffreestanding`/`-fno-builtin`, GCC 는 `-fno-tree-loop-distribute-patterns` 도. 꼬리질문 "freestanding 이면 memcpy 는 안 불리나?" → 아니다. 구조체 대입·초기화에서 컴파일러가 여전히 `memcpy`/`memset` 을 부를 수 있어서 펌웨어는 이 네 함수(`memcmp` 포함)를 반드시 링크해야 한다.

</details>

<details>
<summary>Q5. src 와 dst 의 정렬이 서로 다르면 워드 복사를 어떻게 하나요? Cortex-M0 에서는요?</summary>

쓰기(dst)를 먼저 정렬하고, 읽기는 ① 비정렬 로드 (arm64·Cortex-M3/M4/M7 의 `LDR` 은 `UNALIGN_TRP=0` 이면 하드웨어가 처리. 단 `LDM/STM/LDRD` 는 폴트) ② 시프트 병합 — 정렬 워드 두 개를 읽어 `(prev >> 8k) | (next << (32-8k))` (리틀 엔디안) ③ 바이트 후퇴. M0 는 비정렬 접근이 HardFault 라 `aligned(1)` 로드가 `ldrb` 4개로 쪼개지므로 ② 가 답이다 (연습 2). 꼬리질문 "시프트 병합에서 첫 워드는?" → `src` 앞쪽의 정렬 워드를 그냥 읽으면 객체 밖 읽기가 될 수 있어서, 첫 부분 워드는 바이트로 조립했다.

</details>

<details>
<summary>Q6. memcpy(dst, NULL, 0) 은 괜찮나요?</summary>

C11 에서는 UB 다. 7.24.1p2: 길이 `n` 이 0 이어도 포인터 인자는 7.1.4 의 "유효한 값" 이어야 한다. 그리고 glibc 헤더는 `memcpy` 에 `nonnull` 속성을 달아서, GCC 가 "`memcpy(p, q, n)` 를 지났으니 `p` 는 NULL 이 아니다" 라고 추론해 뒤의 NULL 검사를 지운 사례가 있다. C2y 작업 초안에 N3322 가 채택되어 `n == 0` 이면 널 포인터를 허용하는 방향으로 바뀌는 중이다. 펌웨어 코드는 `if (n) memcpy(...)` 로 방어하거나 빈 버퍼도 유효한 주소를 갖게 설계한다.

</details>

<details>
<summary>Q7. 큰 버퍼 복사를 DMA 로 하면 항상 더 빠른가요?</summary>

아니다. DMA 는 채널 설정·완료 처리 오버헤드가 있어 작은 블록은 CPU 가 빠르고, 메모리 대역폭 자체가 병목이면 둘 다 같은 버스를 쓴다. DMA 의 진짜 이득은 **CPU 를 놓아주는 것** — 그동안 계산하거나 WFI 로 잔다. 함정: Cortex-M7 D-cache (DMA 전 clean, 후 invalidate, 32 B 정렬), 전송 중 버퍼 수정 금지, 스택 버퍼를 DMA 에 넘기고 함수 반환, STM32F4 는 DMA2 만 M2M 가능. 프레임버퍼라면 DMA2D 같은 2D 엔진이 채우기·복사·색 변환까지 한다.

</details>

<details>
<summary>Q8. 비밀 키를 지우려고 memset 했는데 바이너리에 그 코드가 없어요.</summary>

함수 끝에서 다시 읽지 않는 지역 버퍼에 대한 `memset` 은 관찰 가능한 효과가 없는 dead store 라 제거된다 (demo_deadstore: `session_plain` 에 `memset` 이 없음). 해결: C23 `memset_explicit`, C11 Annex K `memset_s` (선택 기능, macOS 에는 있음), BSD/glibc `explicit_bzero`, 또는 `volatile unsigned char *` 루프 (생략 불가, 6.7.3p7). 펌웨어에서 키 슬롯·세션 키를 정리할 때 단골 리뷰 포인트다.

</details>

## 9. 추가 연습

### 연습 1 — 워드 단위 memcmp

`my_memcmp(a, b, n)` 를 워드로 같은 구간을 빨리 건너뛰고, 다르면 첫 번째 다른 바이트로 부호를 정하게 작성하라. 워드 값끼리 빼서 부호를 내면 왜 틀리나?

<details>
<summary>답 보기</summary>

`memcmp` 의 부호는 **첫 번째로 다른 바이트를 `unsigned char` 로 본 차이** 다 (7.24.4.1). 리틀 엔디안에서 워드의 최하위 바이트가 메모리상 **첫** 바이트라서, `0x00000001` vs `0x00000100` (바이트열 `01 00..` vs `00 01..`) 을 워드로 비교하면 1 < 256 이지만 정답은 `01 > 00` → 양수. 그래서 워드가 다르면 루프를 빠져나와 그 워드 안을 바이트로 다시 본다 (빅 엔디안이면 워드 비교가 바로 맞다 — C-B13). 테스트는 3,000 랜덤 케이스에서 libc 와 **부호** 만 비교한다 (값은 구현마다 다를 수 있다). 프로덕션에서 `char` 를 `signed` 로 비교하는 실수(`0x80` 이 음수)도 테스트 `"\x80" vs "\x01"` 이 잡는다.

</details>

### 연습 2 — Cortex-M0 용 시프트 병합 memcpy

비정렬 로드를 쓰지 않고 정렬 로드만으로 `memcpy_shift` 를 작성하라 (리틀 엔디안). src 객체 밖은 1바이트도 읽으면 안 된다.

<details>
<summary>답 보기</summary>

dst 를 정렬한 뒤 src 어긋남 `k = s & 3` (1..3). 리틀 엔디안에서 `s[0..4)` 는 정렬 워드 `P`(=`s-k` 위치)의 상위 `4-k` 바이트와 다음 정렬 워드 `N` 의 하위 `k` 바이트로 이루어지므로 `out = (P >> 8k) | (N << (32 - 8k))`, 그리고 `P = N` 으로 넘어간다. 함정은 **첫 `P`**: `s - k` 에서 정렬 워드를 그냥 읽으면 src 시작 앞 `k` 바이트를 읽는다 (객체 밖일 수 있고, `s - k` 포인터 계산 자체가 배열 밖이면 UB). 그래서 첫 `P` 는 `s[0..4-k)` 바이트로 조립하고, 루프는 `N` 의 끝(`s + 8 - k`)이 범위 안일 때만(`n >= 8 - k`) 돈다. 나머지는 꼬리 바이트.

조건을 `n >= 4` 로 느슨하게 바꾸면 결과 바이트는 맞게 나와서 **일반 테스트는 통과** 한다 (넘어 읽은 바이트는 저장되지 않으니까). 그래서 practice.c 는 src 를 **정확히 len 바이트짜리 `malloc` 블록** 으로 주는 테스트를 넣었다. 실제로 조건을 `n >= 4` 로 바꾸고 ASan 으로 돌리면:

```text
==60548==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000dd8 ...
READ of size 4 at 0x602000000dd8 thread T0
```

512 B 정적 배열의 끝(4바이트 정렬)에 붙인 테스트로는 못 잡았다 — 정렬된 워드 읽기는 정렬된 끝을 넘지 않기 때문. 경계 테스트는 "끝이 비정렬인 정확한 크기 객체" 로 해야 한다.

</details>

### 연습 3 — RGB565 LCD 채우기 `fill_u16`

16비트 픽셀 버퍼(2바이트 정렬)를 색 `v` 로 `n` 픽셀 채워라. 워드 하나에 픽셀 2개를 쓴다.

<details>
<summary>답 보기</summary>

`p` 가 4바이트 정렬이 아니면(`p & 2`) 픽셀 하나를 먼저 쓰고, `w = v | v << 16` 으로 두 픽셀씩, 홀수 개가 남으면 마지막 하나. 리틀 엔디안에서 `v | v << 16` 은 두 픽셀이 같은 값이라 엔디안과 무관하다. `uint16_t` 배열을 `uint32_t` 로 쓰는 것도 유효 타입 위반이라 `word_t`(may_alias) 를 쓴다 — `demo_alias.c` 가 바로 이 상황에서 깨지는 예다. 오프셋 0..3 × 길이 0..40 전수로 단순 루프와 대조. (STM32 라면 이 일을 DMA2D register-to-memory 모드가 한다.)

</details>

### 연습 4 — 지워지지 않는 0 채우기

비밀 키 버퍼를 확실히 0 으로 지우는 `secure_zero(p, n)` 를 작성하고, 일반 `memset` 이 사라지는 것을 asm 으로 확인하라.

<details>
<summary>답 보기</summary>

`volatile unsigned char *v = p; while (n--) *v++ = 0;` — volatile 접근은 생략할 수 없다 (C-B09). `cc -std=c11 -O2 -S demo_deadstore.c -o -` 실제 출력에서 `session_plain` 은 `bl _get_key`, `bl _use_key` 다음 바로 스택 가드 검사와 `ret` — `memset` 이 **없다**. `session_secure` 는 `strb wzr, [sp, #8]` … `[sp, #39]` 32개가 그대로 남았다 (바이트 단위라 느리지만 정확). 표준 해법은 C23 `memset_explicit`. 테스트로는 "0 이 됐다" 만 확인할 수 있고 "제거되지 않았다" 는 asm 으로만 보인다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 10/10 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_san && ./practice_san   # 10/10, 보고 없음
```

## 10. 핵심 정리

- `memcpy` 겹침 = UB, `memmove` = 방향 선택: `(uintptr_t)d - (uintptr_t)s >= n` 이면 앞에서부터, 아니면 뒤에서부터. 포인터 `<` 는 다른 객체 사이에서 UB.
- 워드 버전 = **dst 머리 정렬 `(-d) & 3`(n 으로 클램프) → `n >= 4` 워드 루프 → 꼬리**. 뒤에서부터는 dst **끝** 을 정렬. 범위 밖 접근 0.
- src 상대 정렬이 다르면: 비정렬 로드(M3/M4/M7·arm64 OK) / 시프트 병합(M0) / 바이트 후퇴.
- `char` 버퍼를 `uint32_t*` 로 접근 = 유효 타입 위반 (6.5p7) → `-O2` 에서 실제로 틀린 값. libc 는 `may_alias` typedef·asm·`-fno-strict-aliasing`. 정렬은 `aligned(1)` 로 별도 처리.
- `-O2` 는 내 루프를 `memcpy`/`memset`/`__aeabi_memclr4` 호출로 바꾼다 → libc 구현자에겐 무한 재귀. `-ffreestanding`/`-fno-builtin`.
- `memset` 패턴은 `(unsigned char)c * 0x01010101u` — 변환을 먼저.
- 퍼즈: xorshift32 고정 시드, 버퍼 **전체** 를 libc 결과와 비교, 경계는 정확한 크기 객체 + ASan.
- 벤치마크는 `-fno-builtin` 필수. 스칼라에서 워드 ≈ 바이트의 4배 (memset 4.35 → 17.7 GB/s), libc NEON 은 그 7~8배. 큰 블록 + CPU 를 쉬게 하고 싶으면 DMA (M7 캐시 관리 필수).

## 참고자료

- C11 (N1570) 7.24.1p2 (n=0 이어도 유효 포인터), 7.24.2.1 memcpy, 7.24.2.2 memmove, 7.24.4.1 memcmp, 7.24.6.1 memset, 6.5p6–7 유효 타입, 6.3.2.3p6–7 포인터 변환·정렬, 6.5.8p5 관계 비교, 6.7.3.1 restrict — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- WG14 N3322 — Allow zero length operations on null pointers (C2y) — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3322.pdf>
- [GCC — Common Type Attributes: `may_alias`, `aligned`](https://gcc.gnu.org/onlinedocs/gcc/Common-Type-Attributes.html)
- [GCC — Standards: freestanding 환경도 memcpy/memmove/memset/memcmp 제공 필요](https://gcc.gnu.org/onlinedocs/gcc/Standards.html)
- [musl `src/string/memcpy.c` / `memmove.c` (may_alias typedef, 시프트 병합)](https://git.musl-libc.org/cgit/musl/tree/src/string/memcpy.c)
- [LLVM LoopIdiomRecognize.cpp](https://github.com/llvm/llvm-project/blob/main/llvm/lib/Transforms/Scalar/LoopIdiomRecognize.cpp), [GCC Bug 56888 — memcpy implementation optimized as a call to memcpy](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=56888)
- [ARM — Run-time ABI for the Arm Architecture (`__aeabi_memcpy`, `__aeabi_memclr4`)](https://github.com/ARM-software/abi-aa/blob/main/rtabi32/rtabi32.rst)
- ARMv7-M Architecture Reference Manual — A3.2 Alignment support (`CCR.UNALIGN_TRP`, LDM/STM 비정렬 폴트); ARMv6-M ARM — 비정렬 접근 HardFault
- STM32F4 Reference Manual RM0090 — DMA (메모리→메모리는 DMA2 만), DMA2D (Chrom-ART)
- 관련 레슨: C-B07 (워드 strlen·idiom 인식), C-B09 (volatile, DMA 캐시), C-B11 (Reset_Handler 복사·0 채우기), C-B13 (엔디안), C-M02 (W1C), C-M04 (packed·비정렬), C-M13 (strict aliasing), C-M15 (드라이버 초기화)
