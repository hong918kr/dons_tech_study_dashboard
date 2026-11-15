---
id: ALGO-B12
time: 60분
updated: 2026-11-11
---
# ALGO-B12 비트 트릭 1: popcount·최하위 비트·2의 거듭제곱

## 학습 목표

- popcount(1인 비트 개수)를 **Kernighan 루프**(반복 = 1의 개수)와 **SWAR**(분기 없이 고정 12명령)로 구현하고, 각 단계에서 왜 자리 넘침(carry)이 이웃 칸으로 번지지 않는지 증명할 수 있다.
- `x & -x`(최하위 1비트 추출)와 `x & (x-1)`(최하위 1비트 제거)를 2의 보수로 유도하고, **unsigned 에서만** 쓰는 이유(`-INT_MIN` 은 UB)를 실제 UBSan 메시지로 확인한다.
- 2의 거듭제곱 판정, ctz/clz, **다음 2의 거듭제곱 올림**을 구현하면서 0 과 2^31 초과 같은 경계에서의 동작을 **먼저 정의**하고 테스트로 못 박는다.
- `ALIGN_UP(x, a)` 의 32비트 넘침(UINT32_MAX 근처)과 **`~(a-1)` 타입 폭 함정**(64비트 주소의 상위 32비트가 지워짐)을 설명하고 고친다.
- 위 도구로 펌웨어 코드 세 가지(DMA 버퍼의 캐시 라인 구간, 빈 슬롯 비트맵 할당기, 이벤트 그룹 비트 처리)를 짜고, `__builtin_*` 결과와 2^32 전 범위로 대조한다.

## 1. 문제

비트 트릭은 펌웨어 코딩 면접의 "5분 문제"다 — 짧아서 자주 나오고, 짧은 만큼 **0, 최상위 비트, 넘침** 같은 경계를 말하지 못하면 바로 감점된다.

C-B01 의 추가 연습 1~3 에서 `x & (x-1)`, `x & -x`, `is_pow2` 를 한 줄씩 봤다. 이 레슨은 그 식들을 **유도**하고, 경계에서의 동작을 **정의**하고, 컴파일러 내장 함수(`__builtin_popcount/ctz/clz`)와 **전 범위**로 대조한 뒤, 실제 펌웨어 코드에 쓴다.

핵심 아이디어 하나: 32비트 레지스터를 "작은 칸 여러 개"로 보고 칸마다 동시에 더한다 — **SWAR**(SIMD Within A Register). 아래는 8비트 `0xDB` 의 1을 세는 과정이다. 칸 크기가 1 → 2 → 4 → 8 비트로 커지면서 각 칸에 "그 칸 안의 1의 개수"가 들어간다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">SWAR popcount: 칸을 2배씩 키우며 이웃 칸을 더한다 (8비트 0xDB = 1101 1011, 답 6)</text>
  <g fill="currentColor">
    <text x="10" y="64">x (1비트 칸)</text><text x="10" y="129">① 2비트 칸</text><text x="10" y="194">② 4비트 칸</text><text x="10" y="259">③ 8비트 칸</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="130" y="44" width="40" height="30"/><rect x="170" y="44" width="40" height="30"/><rect x="210" y="44" width="40" height="30"/><rect x="250" y="44" width="40" height="30"/>
    <rect x="290" y="44" width="40" height="30"/><rect x="330" y="44" width="40" height="30"/><rect x="370" y="44" width="40" height="30"/><rect x="410" y="44" width="40" height="30"/>
  </g>
  <rect x="130" y="109" width="80" height="30" class="f-acc" stroke="currentColor"/><rect x="210" y="109" width="80" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="290" y="109" width="80" height="30" class="f-acc" stroke="currentColor"/><rect x="370" y="109" width="80" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="130" y="174" width="160" height="30" class="f-acc" stroke="currentColor"/><rect x="290" y="174" width="160" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="130" y="239" width="320" height="30" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="150" y="64">1</text><text x="190" y="64">1</text><text x="230" y="64">0</text><text x="270" y="64">1</text>
    <text x="310" y="64">1</text><text x="350" y="64">0</text><text x="390" y="64">1</text><text x="430" y="64">1</text>
    <text x="170" y="129">10 (2)</text><text x="250" y="129">01 (1)</text><text x="330" y="129">01 (1)</text><text x="410" y="129">10 (2)</text><text x="210" y="194">0011 (3)</text><text x="370" y="194">0011 (3)</text>
    <text x="290" y="259">0000 0110 (6)</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="470" y="98">x - ((x &gt;&gt; 1) &amp; 0x55)</text><text x="470" y="163">(x &amp; 0x33) + ((x &gt;&gt; 2) &amp; 0x33)</text><text x="470" y="228">(x + (x &gt;&gt; 4)) &amp; 0x0F</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="470" y="116">2비트 v=2a+b → v−a = a+b</text><text x="470" y="181">합 ≤ 4 → 4비트 칸에 들어감</text><text x="470" y="246">합 ≤ 8 → 마스크는 더한 뒤에</text>
  </g>
  <text x="10" y="300" fill="currentColor">32비트는 같은 3단계 + 네 바이트 합: (x * 0x01010101) &gt;&gt; 24. 분기 없음, 입력과 무관하게 같은 명령 수.</text>
  <text x="10" y="320" fill="currentColor" opacity=".7">색 = 칸 경계(번갈아 칠함). 각 칸의 값 = 그 칸이 덮는 원래 비트들의 1 개수.</text>
</svg>
```

모든 함수는 `uint32_t`/`uint64_t` 를 받고, TODO 안에서 `__builtin_*` 는 쓰지 않는다 (테스트 쪽 기준으로만 쓴다).

### Part A — popcount 두 가지 (Kernighan · SWAR)

- `pc_kernighan(x)`: `x &= x - 1` 로 1을 하나씩 지우며 센다. 반복 횟수 = 답.
- `pc_swar32(x)`: 마스크 `0x55555555 → 0x33333333 → 0x0F0F0F0F` 세 단계 + `* 0x01010101 >> 24`.
- `pc_swar64(x)`: 같은 방법을 64비트로 (`>> 56`).
- 테스트: 손 계산 값(`0x12345678` → 13 등), 16비트 전 범위를 아래 반쪽·위 반쪽·양쪽(`x * 0x10001`)에 놓고, 구조적 값(한 비트·두 비트 496쌍·2^k−1), xorshift32 랜덤 2^20 개를 `__builtin_popcount(ll)` 와 대조. **2^32 전 범위는 `demo_exhaustive.c`** — solution.c 는 2초 제한(ASan 빌드 포함)이 있어서 따로 뺐다.

### Part B — 최하위 비트와 2의 거듭제곱: 경계가 전부다

- `lowbit(x) = x & (0u - x)`, `clear_lowbit(x) = x & (x - 1u)`, `is_pow2(x)` (0 은 거짓).
- `ctz32(x)`: lowbit 과 popcount 만으로, **분기 없이** 0 → 32 가 나오게.
- `clz32(x)`: 최상위 1을 아래로 번지게(smear) 한 뒤 popcount.
- `next_pow2(x)`: 0 → 1, 2^31 초과 → 0 으로 **정의**하고 나머지는 smear 로.
- 기준: `__builtin_ctz/clz` 는 **0 에서 정의되지 않으므로** 테스트 쪽 기준 함수가 0 을 먼저 거른다.

### Part C — 펌웨어: 정렬 올림 · DMA 캐시 라인 · 비트맵 할당기 · 이벤트 그룹

- `align_up32(x, a, &out)`: `x + a - 1` 이 넘치면(결과가 2^32) `false`, `*out` 은 건드리지 않는다.
- `align_up64(addr, a)`: 64비트 주소, 32비트 정렬값. 마스크를 **먼저 64비트로 넓힌 뒤** `~`.
- `dma_buf_ok(addr, len, line)`, `cache_span(addr, len, line, &start)`: Cortex-M7 D-cache(라인 32바이트)에서 DMA 수신 버퍼를 무효화(invalidate)할 때 건드리는 라인 구간. 끝 주소 올림이 2^32 가 될 수 있다.
- `bm_alloc(map, nwords, nslots)`: 비트 1 = 사용 중. `~map[w]` 의 최하위 1 = 첫 빈 슬롯. 70 슬롯처럼 32의 배수가 아닐 때 마지막 워드의 남는 비트는 슬롯이 아니다.
- `ev_wait_ok(bits, mask, all)`, `ev_drain(pending, order)`: FreeRTOS 이벤트 그룹식 "하나라도/전부" 대기, 켜진 비트를 낮은 번호부터 처리 (반복 = popcount).

## 2. Problem (English)

> **Bit tricks for a firmware utility header.** Implement in C11, without compiler builtins:
>
> ```c
> unsigned pc_kernighan(uint32_t x);      unsigned pc_swar32(uint32_t x);   unsigned pc_swar64(uint64_t x);
> uint32_t lowbit(uint32_t x);            uint32_t clear_lowbit(uint32_t x); bool is_pow2(uint32_t x);
> unsigned ctz32(uint32_t x);             unsigned clz32(uint32_t x);        /* both return 32 for x == 0 */
> uint32_t next_pow2(uint32_t x);         /* smallest 2^k >= x; next_pow2(0) == 1; returns 0 if x > 2^31 */
> bool     align_up32(uint32_t x, uint32_t a, uint32_t *out);   /* a is a power of two; false if result >= 2^32 */
> uint64_t align_up64(uint64_t addr, uint32_t a);
> uint32_t cache_span(uint32_t addr, uint32_t len, uint32_t line, uint32_t *start); /* #lines covering [addr, addr+len) */
> int      bm_alloc(uint32_t *map, unsigned nwords, unsigned nslots);              /* lowest free slot or -1 */
> unsigned ev_drain(uint32_t pending, uint8_t order[32]);                           /* set bits, lowest first */
> ```
>
> Constraints: `pc_swar*` must be branch-free. `ev_drain` must loop `popcount(pending)` times, not 32. No undefined behaviour for any input (in particular `x == 0`, `x == 0x80000000`, `x == UINT32_MAX`).
>
> Examples:
> `pc_swar32(0x12345678) == 13` · `lowbit(0x58) == 0x08` · `clear_lowbit(0x58) == 0x50` · `ctz32(0) == 32` · `clz32(0x00010000) == 15` ·
> `next_pow2(1025) == 2048` · `next_pow2(0x80000001) == 0` · `align_up32(0x1001, 0x1000) → 0x2000` · `align_up32(0xFFFFFFC1, 64) → false` ·
> `align_up64(0x1_2345_6789, 64) == 0x1_2345_67C0` · `cache_span(0x20000010, 100, 32) == 4, start = 0x20000000` · `ev_drain(0x80000005) → {0, 2, 31}`.
>
> Follow-ups: Which popcount would you put in an ISR, and why? What does `next_pow2` return for 0 in your code, and why did you choose that? What breaks if `a` is a 32-bit unsigned constant and `addr` is 64-bit?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 Part A 3개, Part B 6개, Part C 7개 — 대부분 한두 줄이다. 하네스(기준 함수, 랜덤 생성기, `bm_free`/`bm_used`)는 주어진다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:171  pc_kernighan(0x12345678u) == 13 && pc_swar32(0x12345678u) == 13
…
A/B sweep (16-bit x3 + structured + 2^20 random): bad kern=1245836 swar=1245836 lowbit=1245836 clear=1245836 pow2=67 ctz=655771 clz=656958 next=656961
…
FAIL starter.c:323  ev_wait_ok(0x05u, 0x04u, false) && !ev_wait_ok(0x05u, 0x0Au, false)
FAIL starter.c:324  ev_wait_ok(0x07u, 0x05u, true) && !ev_wait_ok(0x05u, 0x06u, true)
7/57 passed
```

(exit 1. 통과한 7개는 "0 을 넣으면 0", "넘치면 false" 처럼 스텁의 0/false 가 우연히 맞는 경우다.)

## 4. 힌트

<details>
<summary>힌트 1 — x - 1 은 비트를 어떻게 바꾸나</summary>

`x = ...p 1 000` (최하위 1 아래에 0이 k개)이면 `x - 1 = ...p 0 111`. 위쪽 `...p` 는 그대로, 최하위 1 과 그 아래만 뒤집힌다. 이걸 `x` 와 AND 하면? `-x = ~x + 1` 도 같은 방식으로 써 보라.

</details>

<details>
<summary>힌트 2 — SWAR 첫 단계는 왜 빼기인가</summary>

2비트 칸 값 `v = 2a + b` 에서 1의 개수는 `a + b = v - a`. `(x >> 1) & 0x55555555` 는 칸마다 `a` 를 뽑는다. 칸마다 `v - a ≥ 0` 이라 빌림(borrow)이 옆 칸으로 안 넘어간다. 다음 단계의 마스크는 **더하기 전**인가 후인가? 4비트 칸에 들어갈 합의 최댓값(4 = `0b100`, 3비트)과 8비트 칸의 최댓값(8, 니블 안)을 비교해 보라.

</details>

<details>
<summary>힌트 3 — ctz 와 clz 를 popcount 로</summary>

`lowbit(x) - 1` 은 최하위 1 아래 비트만 전부 1 → 그 개수가 ctz. `x = 0` 이면 `0 - 1 = 0xFFFFFFFF` → 32, 분기가 필요 없다. clz 는 `x |= x >> 1; x |= x >> 2; … x |= x >> 16;` 으로 최상위 1 아래를 모두 채우면 popcount = 비트 길이.

</details>

<details>
<summary>힌트 4 — ALIGN_UP 의 넘침은 정확히 언제인가</summary>

답(올림 결과)이 2^32 가 되는 것은 x 가 마지막 정렬 블록 `(2^32 - a, 2^32)` 안에 있을 때뿐이고, 그때가 정확히 `x + (a-1)` 이 넘칠 때다. 그러니 `x > UINT32_MAX - (a-1)` 하나로 검사가 끝난다. 64비트판은 `~(a - 1)` 을 **어느 폭에서** 계산하는지 보라.

</details>

## 5. 풀이

**면접에서 말하는 순서** (화이트보드 5분 문제라도 이 순서를 지킨다)

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 질문으로 확인 | 입력 타입은 unsigned 32비트? 0 은? `next_pow2` 가 2^31 을 넘으면? 하드웨어 popcount 명령이 있나(코어)? ISR 안에서 쓰나? |
| 2. 무식한 방법 + 복잡도 | 32비트를 하나씩 검사 — 항상 32회, O(w) (w = 워드 폭) |
| 3. 더 나은 아이디어 + 불변식 | Kernighan: "매 반복 1이 정확히 하나 줄어든다" → O(popcount). SWAR: "단계 i 후 각 2^i 비트 칸 = 그 칸의 1 개수" → O(log w) 단계, 분기 없음 |
| 4. 코드 | Part A/B/C 의 함수들 (6. 정답 코드) — 대부분 한두 줄 |
| 5. 소리 내서 말할 테스트 | 0, 1, `0x80000000`, `0xFFFFFFFF`, `0xAAAAAAAA`, 2^k−1 과 2^k+1, `next_pow2(2^31+1)`, `align_up32(0xFFFFFFC1, 64)`, 64비트 주소 `0x1_2345_6789` |
| 6. 복잡도 | 시간: Kernighan O(k) (k ≤ 32, 데이터 의존), SWAR O(1) = 12명령(Cortex-M4 실제 asm). 추가 메모리 0, 스택 0 (재귀 없음) |

### Part A — Kernighan: 왜 `x & (x-1)` 은 1 하나만 지우나

`x ≠ 0` 을 `x = P·1·0^k` (위쪽 비트열 P, 최하위 1, 그 아래 0 이 k개)로 쓰면

- `x - 1 = P·0·1^k` (빌림이 최하위 1 까지만 전파)
- `x & (x - 1) = P·0·0^k` — P 는 그대로, 최하위 1 만 0.

불변식: 반복마다 popcount 가 정확히 1 줄고 0 이 되면 멈춘다 → 반복 = popcount(x). `x = 0x58` 추적:

| 반복 | x (하위 8비트) | x − 1 | x & (x−1) | c |
|---|---|---|---|---|
| 1 | `0101 1000` | `0101 0111` | `0101 0000` | 1 |
| 2 | `0101 0000` | `0100 1111` | `0100 0000` | 2 |
| 3 | `0100 0000` | `0011 1111` | `0000 0000` | 3 → 끝 |

### Part A — SWAR: 칸을 키우며 더하기, 넘침이 없는 이유

`0x12345678` (답 13) 을 32비트 그대로 추적 (Python 으로 계산한 값):

| 단계 | 식 | 결과 | 칸의 의미 |
|---|---|---|---|
| 0 | x | `0x12345678` | |
| ① | `x - ((x >> 1) & 0x55555555)` | `0x11245564` | 2비트 칸 16개, 각 0..2 |
| ② | `(x & 0x33333333) + ((x >> 2) & 0x33333333)` | `0x11212231` | 4비트 칸 8개, 각 0..4 (니블 = 원래 니블의 popcount: 1,1,2,1,2,2,3,1) |
| ③ | `(x + (x >> 4)) & 0x0F0F0F0F` | `0x02030404` | 바이트 4개, 각 0..8 |
| ④ | `x * 0x01010101` | `0x0D0B0804` | 최상위 바이트 = 2+3+4+4 = 13 |
| | `>> 24` | **13** | |

단계마다 "칸에 들어갈 최댓값 < 칸 용량" 을 확인하는 것이 증명의 전부다.

| 단계 | 칸 크기 | 최댓값 | 용량 | 마스크 위치 |
|---|---|---|---|---|
| ① | 2비트 | 2 | 3 | 빼기 — 칸마다 `v − a ≥ 0` 이라 빌림이 안 번짐 |
| ② | 4비트 | 4 (3비트) | 15 | **더하기 전** — 더한 뒤 마스크하면 합 4 = `0b100` 의 세 번째 비트가 잘림 (니블 `0xF` → 0) |
| ③ | 8비트 | 8 (4비트) | 255 | **더한 뒤** 한 번 — 합이 니블 안에 들어가서 위 니블로 안 넘침 |
| ④ | 32비트 | 32 | 255 (최상위 바이트) | `x·0x01010101 = x + (x<<8) + (x<<16) + (x<<24)`; 부분합 ≤ 32 라 바이트 사이 자리올림 없음 |

64비트판은 마스크를 64비트로 늘리고 `* 0x0101010101010101 >> 56` (여덟 바이트 합 ≤ 64 < 256). 곱셈이 비싼 코어용 shift-add 판은 추가 연습 1.

### Part A — 컴파일러가 실제로 하는 일 (asm)

`clang -O2 -S` 결과에서 지시어(`.cfi_*`, `.p2align` 등)만 빼고 옮긴다 (Apple clang 21.0.0, 레슨 폴더의 `asm_popcount.c` — `<stdint.h>` 만 쓰는 `__builtin_popcount(ll)`, Kernighan, SWAR, lowbit, `__builtin_ctz`. 타깃별 명령은 파일 머리 주석).

**arm64 (이 맥)** — 세 popcount 가 **모두 같은 코드**가 됐다 (보기 좋게 나란히 배치, 마지막 줄은 두 함수를 한 줄로 요약):

```text
_pc_builtin32:            _pc_kernighan:            _pc_swar32:
	fmov	s0, w0          	fmov	s0, w0          	fmov	s0, w0
	cnt.8b	v0, v0          	cnt.8b	v0, v0          	cnt.8b	v0, v0
	addv.8b	b0, v0          	addv.8b	b0, v0          	addv.8b	b0, v0
	fmov	w0, s0          	fmov	w0, s0          	fmov	w0, s0
	ret                     	ret                     	ret
_lowbit:  neg w8, w0 / and w0, w0, w8 / ret          _ctz_builtin:  rbit w8, w0 / clz w0, w8 / ret
```

- arm64 정수 레지스터에는 popcount 명령이 없어서, 값을 NEON 레지스터로 옮겨(`fmov`) 바이트별 개수(`cnt.8b`)를 구하고 가로로 더한다(`addv.8b`). ctz 명령도 없어서 비트 뒤집기(`rbit`) + `clz`.
- clang 이 Kernighan **루프**와 SWAR **식**을 알아보고 popcount 로 바꿨다. 최적화 단계별로 확인하니 `-O0`: 둘 다 그대로, `-O1`: Kernighan 만 `cnt`, `-O2`/`-Os`: 둘 다 `cnt`. (LLVM 의 루프 패턴 인식 LoopIdiomRecognize 와 AggressiveInstCombine 의 popcount 패턴 인식으로 알려져 있다.)

**Cortex-M4 (`clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S`)** — popcount 명령이 없다. `__builtin_popcount` 는 **라이브러리 호출이 아니라 인라인 SWAR** 로 펼쳐졌고, 손으로 쓴 `pc_swar32` 와 명령 하나까지 같다:

```text
pc_builtin32:  (pc_swar32 도 똑같음)
	mov.w	r1, #1431655765          @ 0x55555555
	and.w	r1, r1, r0, lsr #1
	subs	r0, r0, r1
	mov.w	r1, #858993459           @ 0x33333333
	and.w	r1, r1, r0, lsr #2
	bic	r0, r0, #-858993460          @ & 0x33333333 (= bic ~0x33333333)
	add	r0, r1
	add.w	r0, r0, r0, lsr #4
	bic	r0, r0, #-252645136          @ & 0x0F0F0F0F
	mov.w	r1, #16843009            @ 0x01010101
	muls	r0, r1, r0
	lsrs	r0, r0, #24
	bx	lr
```

(`@` 뒤 16진 주석은 내가 붙였다.) 12명령 + `bx lr`, 시프트가 `and.w ..., lsr #1` 처럼 피연산자에 붙는 Thumb-2 의 배럴 시프터 덕분에 짧다. Kernighan 은 M4 에서 popcount 로 바뀌지 **않고** 4번 펼친(unroll) 루프로 남았다 (`subs r2, r1, #1` / `ands r1, r2` 한 쌍이 1비트 지우기, 쌍마다 `it eq; bxeq lr` 로 탈출 검사).

`__builtin_popcountll` 은 M4 에서 32비트 SWAR 두 벌 + 합(`add.w r0, r0, r1, lsr #24`). Cortex-M0+ (`thumbv6m`) 에서도 같은 SWAR 이 인라인됐다(상수는 `ldr` 리터럴 풀에서). M0+ 에는 `clz` 도 없어서 `__builtin_ctz` 가 **de Bruijn 곱셈**으로 나왔다: `rsbs/ands` 로 `x & -x` → `* 0x077CB531` → `>> 27` → 32바이트 표 `ldrb` (0 은 분기로 32). 추가 연습 4 에서 직접 만든다.

> GCC(arm-none-eabi-gcc)는 이 맥에 없어서 확인하지 못했다. GCC 는 Cortex-M 에서 `__builtin_popcount` 를 libgcc 의 `__popcountsi2` 호출로 내는 경우가 있다고 알려져 있으니, 실제 프로젝트에서는 `.map`/역어셈블로 확인할 것.

### Part A — 전 범위 대조와 속도 (`demo_exhaustive.c`, `demo_bench.c`)

**함정부터.** `demo_exhaustive.c` 를 `-O2` 로 돌리면 popcount 류 대조가 **0.00 초**에 끝난다 (실제 출력 줄: `swar 2^32 values bad=0 0.00 s`, `kern … 0.00 s`, lowbit·clear·pow2·ctz 도 0.00 s, `clz … 4.13 s`, `next … 3.30 s`, `align64 … overflow-rejects=63 0.55 s`). `pc_swar32(x) == __builtin_popcount(x)` 의 양쪽이 모두 `cnt` 가 되니 컴파일러가 "항상 참"을 증명하고 루프를 지웠다 — **C 소스의 산술은 하나도 검사되지 않았다.** 그래서 `-O0` 으로 다시 돌렸다 (실제 출력을 표로, 이 맥, 다른 작업이 같이 돌던 중, 합계 2분 41초):

| 함수 | swar | kern | lowbit | clear | pow2 | ctz | clz | next | align (a=64) |
|---|---|---|---|---|---|---|---|---|---|
| 2^32 개 중 불일치 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 (거부 63개) |
| 시간 (s) | 12.27 | 53.92 | 11.14 | 11.95 | 9.85 | 20.67 | 18.69 | 13.18 | 9.43 |

4,294,967,296 개 전부 일치. `align_up32(x, 64)` 가 거부한 x 는 정확히 63개(`0xFFFFFFC1 ~ 0xFFFFFFFF`) — 힌트 4 의 보조정리 그대로다. solution.c 도 `-O0` 으로 따로 빌드해 57/57 을 확인했다.

**속도** — `demo_bench.c`: 값 65536 개 × 200회, 워밍업 1 + 7회 중앙값, 결과 합을 `volatile` 에 저장. "CNT 없는 코어가 하는 일" 을 재현하려고 SWAR·Kernighan 에 빈 `asm volatile` 장벽을 넣어 패턴 인식을 막았다. 이 맥(Apple M6, `-O2`), `uptime` load average 11.55 (다른 에이전트·빌드가 같이 돌던 중) — **절대값보다 비율**을 볼 것 (실제 출력, ns/값):

| 방식 | sparse (평균 4비트) | random (16비트) | dense (28비트) |
|---|---|---|---|
| builtin (`cnt`, NEON 으로 벡터화됨) | 0.086 | 0.086 | 0.083 |
| Kernighan 그대로 (컴파일러가 `cnt` 로 바꿈) | 0.090 | 0.087 | 0.085 |
| SWAR (장벽) | 0.699 | 0.770 | 0.762 |
| Kernighan (장벽) | 5.843 | 14.622 | 19.490 |
| 256칸 LUT | 0.866 | 1.035 | 0.875 |

두 번째 실행도 같은 모양(builtin 0.079~0.114, SWAR 0.686~0.865, Kernighan 6.8/13.4/19.5). 읽을 것:

- **Kernighan 은 1의 개수에 비례**한다 (5.8 → 14.6 → 19.5). 루프마다 `x & (x-1)` 이 앞 반복 결과에 의존(loop-carried dependency)하고 분기 예측도 데이터마다 다르다. ISR 에서 쓰면 **실행 시간이 입력에 따라 흔들린다**(jitter).
- SWAR 은 입력과 무관하게 일정. LUT 은 비슷하지만 256바이트 표가 필요하고 표 읽기가 캐시/플래시 대기 상태(wait state)에 따라 달라진다.
- 하드웨어 명령이 있으면 그게 압도적(여기선 벡터화까지 돼서 ~10배). → 실무 답: **`__builtin_popcount` 를 쓰고, 컴파일러가 코어에 맞는 코드를 내게 둔다**. 면접 답: 둘 다 구현할 수 있고 차이를 설명할 수 있어야 한다.

### Part B — `x & -x` 와 `x & (x-1)`: 2의 보수로 유도

`-x = ~x + 1` (mod 2^32). `x = P·1·0^k` 이면 `~x = ~P·0·1^k`, 여기에 1을 더하면 자리올림이 `0` 까지 가서 `~P·1·0^k`. 위쪽은 정확히 반대, 최하위 1 자리부터 아래는 x 와 같다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">x = 0x58: 최하위 1 (비트 3) 을 기준으로 위·아래가 어떻게 바뀌나</text><rect x="246" y="34" width="36" height="236" class="f-acc"/>
  <g fill="currentColor" font-family="monospace">
    <text x="10" y="58">x</text><text x="10" y="92">x - 1</text><text x="10" y="126">~x</text><text x="10" y="160">-x = ~x + 1</text><text x="10" y="200">x &amp; (x-1)</text><text x="10" y="240">x &amp; -x</text>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="128" y="58">0…0</text><text x="128" y="92">0…0</text><text x="128" y="126">1…1</text><text x="128" y="160">1…1</text><text x="128" y="200">0…0</text><text x="128" y="240">0…0</text>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="192" y="58">0 1 0 1</text><text x="264" y="58">1</text><text x="318" y="58">0 0 0</text><text x="192" y="92">0 1 0 1</text><text x="264" y="92">0</text><text x="318" y="92">1 1 1</text>
    <text x="192" y="126">1 0 1 0</text><text x="264" y="126">0</text><text x="318" y="126">1 1 1</text><text x="192" y="160">1 0 1 0</text><text x="264" y="160">1</text><text x="318" y="160">0 0 0</text>
    <text x="192" y="200">0 1 0 1</text><text x="264" y="200">0</text><text x="318" y="200">0 0 0</text><text x="192" y="240">0 0 0 0</text><text x="264" y="240">1</text><text x="318" y="240">0 0 0</text>
  </g>
  <line x1="10" y1="174" x2="360" y2="174" stroke="currentColor" stroke-dasharray="4 3"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="116" y="288">상위 24비트</text><text x="200" y="288">P (위쪽)</text><text x="264" y="288">최하위 1</text><text x="318" y="288">0^k</text>
  </g>
  <g fill="currentColor">
    <text x="390" y="58">x - 1: 위쪽 P 그대로, 아래만 뒤집힘</text><text x="390" y="92">→ x &amp; (x-1) 은 P 만 남김 = 최하위 1 제거</text><text x="390" y="126">~x: 전부 뒤집힘</text><text x="390" y="160">-x: 위쪽만 뒤집히고 아래는 x 와 같음</text>
    <text x="390" y="200">= 0x50 (clear_lowbit)</text><text x="390" y="240">= 0x08 (lowbit): 위쪽은 x 와 ~x 라 0</text>
  </g>
</svg>
```

`x = 0` 이면 둘 다 0 — 정의가 자연스럽게 맞는다. `is_pow2(x)` = "1이 정확히 하나" = `x != 0 && clear_lowbit(x) == 0`. `x != 0` 을 빼면 0 이 2의 거듭제곱이 된다 (가장 흔한 실수, LeetCode 231 Power of Two 의 함정).

**왜 unsigned 만?** C 에서 unsigned 산술은 2^N 으로 나눈 나머지로 정의돼 있어서 `0u - x` 는 항상 안전하다. signed `int` 에서 `-INT_MIN` 과 `INT_MIN - 1` 은 표현 범위 밖 → **UB**(C11 6.5p5). `demo_traps.c` 를 `-fsanitize=undefined` 로 돌린 실제 출력:

```text
demo_traps.c:33:50: runtime error: negation of -2147483648 cannot be represented in type 'int'; cast to an unsigned type to negate this value to itself
demo_traps.c:34:53: runtime error: signed integer overflow: -2147483648 - 1 cannot be represented in type 'int'
--- 1. signed int ---
INT_MIN & -INT_MIN      = -2147483648
INT_MIN & (INT_MIN - 1) = 0
as uint32_t: lowbit = 0x80000000, clear_lowbit = 0x00000000
```

값은 "맞아 보이지만" UB 라서 최적화기가 다른 코드를 내도 할 말이 없다. 또 하나: `uint16_t h` 에 `h & -h` 를 쓰면 `h` 가 `int` 로 승격(integer promotion)돼 int 로 계산된다 — 실제 출력 `h & -h = 0x10, sizeof(h & -h) = 4`. 값은 맞지만 타입이 int 다. 폭을 지키려면 `(uint16_t)(0u - h)` 처럼 unsigned 로 계산하고 캐스트.

### Part B — ctz/clz 를 popcount 로, 0 은 32

| 함수 | 식 | 왜 | x = 0 |
|---|---|---|---|
| `ctz32` | `popcount(lowbit(x) - 1)` | `0b1000 - 1 = 0b0111` → 아래 0 의 개수만큼 1 | `0 - 1 = 0xFFFFFFFF` → **32** (분기 없음) |
| `clz32` | `32 - popcount(smear(x))` | smear(x) = 2^L − 1, L = 비트 길이 | smear(0) = 0 → **32** |

`__builtin_ctz(0)`, `__builtin_clz(0)` 은 **정의되지 않는다**(GCC/Clang 문서). x86 의 옛 `BSF/BSR` 은 0 에서 목적지가 정의되지 않고, ARM `CLZ` 는 0 에 32 를 준다 — 그래서 "ARM 에서 돌려 보니 32 였다"는 근거가 못 된다. UBSan 은 이것도 잡는다 (실제 출력):

```text
demo_traps.c:39:53: runtime error: passing zero to __builtin_ctz(), which is not a valid argument
demo_traps.c:40:53: runtime error: passing zero to __builtin_clz(), which is not a valid argument
```

테스트 쪽 기준 함수가 `x ? __builtin_ctz(x) : 32` 로 0 을 먼저 거르는 이유다.

### Part B — 다음 2의 거듭제곱: smear, 그리고 0 과 2^31 초과의 정의

`x ∈ [2^(k-1)+1, 2^k]` 이면 `x - 1 ∈ [2^(k-1), 2^k - 1]` — 비트 길이가 정확히 k. smear 하면 `2^k - 1`, 더하기 1 → `2^k`. `x - 1` 을 먼저 하는 이유는 이미 2의 거듭제곱인 x 를 그대로 두기 위해서다. `x = 0x12345` 추적:

| 단계 | 값 |
|---|---|
| x − 1 | `0x00012344` |
| `x \|= x >> 1` | `0x0001B3E6` |
| `x \|= x >> 2` | `0x0001FFFF` (이후 `>>4, >>8, >>16` 은 변화 없음) |
| + 1 | `0x00020000` = 131072 |

5번의 `|=` 로 충분한 이유: 최상위 1 아래가 1 → 2 → 4 → 8 → 16 → 32 칸으로 채워진다(1+1+2+4+8+16 = 32).

경계는 **먼저 정의**한다:

| 입력 | 수학적 답 | 가드 없는 고전 코드 (실제 출력) | 이 레슨의 정의 |
|---|---|---|---|
| 0 | 1 (2^0 ≥ 0) | `0 - 1` → smear → `+1` 감김 → **0** | **1** (명시적 분기) |
| 1 | 1 | 1 | 1 |
| 2^31 + 1 ~ 2^32 − 1 | 2^32 (32비트에 없음) | **0** (`smear(0x80000001) = 0`) | **0** = "답 없음" 표시, 호출자가 검사 |

고전 코드가 0 에 0 을 돌려주는 게 왜 위험한가: 버퍼 크기를 `next_pow2(n)` 으로 정하고 인덱스 마스크를 `size - 1` 로 쓰면 `0 - 1 = 0xFFFFFFFF` → `idx & mask` 가 아무것도 자르지 못해 범위 밖 쓰기가 된다.

clz 로 쓰는 흔한 한 줄 `1u << (32 - __builtin_clz(x - 1))` 은 **두 군데**서 UB 다: x = 1 이면 `clz(0)`, x > 2^31 이면 `1u << 32`. 이 맥에서 실제로 돌리면 (UBSan 메시지 + 값):

```text
demo_traps.c:25:80: runtime error: passing zero to __builtin_clz(), which is not a valid argument
demo_traps.c:25:55: runtime error: shift exponent 32 is too large for 32-bit type 'unsigned int'
next_pow2_clz(1)          = 1
next_pow2_clz(0x80000001) = 1
```

`0x80000001` 의 답이 **1** 로 나왔다 — arm64 의 레지스터 시프트(`lsl`)는 시프트 양을 32 로 나눈 나머지만 쓰므로 `1u << 32` 가 `1u << 0` 이 됐다. 조용히 틀린 값. x = 1 이 "맞은" 것도 ARM `CLZ(0) = 32` 덕분인 우연이다.

### Part C — ALIGN_UP: 유도, 넘침, 타입 폭

a = 2^k 이면 "a 의 배수" = 하위 k 비트가 0. 내림은 `x & ~(a-1)`, 올림은 "a−1 을 더한 뒤 내림" `(x + a - 1) & ~(a - 1)`. (x 가 이미 배수면 a−1 을 더해도 다음 배수에 못 미치니 그대로.)

**넘침 보조정리.** 32비트에서 `x + (a-1)` 이 넘친다 ⟺ 수학적 답이 2^32 이다.
(⇐) 답이 2^32 면 x 는 마지막 블록의 배수가 아닌 점, 즉 `x > 2^32 − a` → `x + a − 1 ≥ 2^32`. (⇒) `x + a − 1 ≥ 2^32` 이면 `x ≥ 2^32 − a + 1` → 2^32 − a 보다 큰 첫 배수는 2^32. 그래서 검사는 `x > UINT32_MAX - (a-1)` 하나면 정확하다 — 거짓 경보도 놓침도 없다 (전 범위 a = 64 에서 거부 63개 = `0xFFFFFFC1..0xFFFFFFFF`, 위 실제 출력). 검사 없는 매크로는 감긴다: `ALIGN_UP_BAD(0xFFFFFFF0u, 64u) = 0x00000000` (실제 출력) — DMA 끝 주소가 0 이 되면 길이 계산이 음수/거대값이 된다.

```svg
<svg viewBox="0 0 740 370" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">DMA 수신 버퍼 [0x20000010, +100) 와 32바이트 캐시 라인 (눈금 = 라인 경계)</text><rect x="60" y="50" width="64" height="40" class="f-warn" stroke="currentColor"/>
  <rect x="124" y="50" width="400" height="40" class="f-acc" stroke="currentColor"/><rect x="524" y="50" width="48" height="40" class="f-warn" stroke="currentColor"/>
  <rect x="572" y="50" width="128" height="40" class="f-mut" stroke="currentColor"/>
  <g stroke="currentColor">
    <line x1="60" y1="42" x2="60" y2="98"/><line x1="188" y1="42" x2="188" y2="98"/><line x1="316" y1="42" x2="316" y2="98"/>
    <line x1="444" y1="42" x2="444" y2="98"/><line x1="572" y1="42" x2="572" y2="98"/><line x1="700" y1="42" x2="700" y2="98"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="60" y="114">..00</text><text x="188" y="114">..20</text><text x="316" y="114">..40</text><text x="444" y="114">..60</text><text x="572" y="114">..80</text><text x="700" y="114">..A0</text>
    <text x="124" y="40">..10</text><text x="524" y="40">..74</text>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="380" y="75">버퍼 100 B (DMA)</text><text x="636" y="75">다음 변수</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="60" y="140">start = addr &amp; ~31 = 0x20000000</text><text x="60" y="158">end = ALIGN_UP(0x20000074, 32) = 0x20000080</text>
    <text x="60" y="176">lines = (end - start) &gt;&gt; 5 = 4</text><text x="60" y="194">dma_buf_ok = ((addr | len) &amp; 31) == 0 → false</text>
  </g>
  <text x="60" y="220" fill="currentColor">경고색 = 버퍼와 라인을 같이 쓰는 이웃 바이트. 무효화하면 이웃의 dirty 데이터가 버려진다 (ARCH-M12).</text>
  <text x="10" y="262" fill="currentColor" font-weight="bold">~(a - 1) 의 타입 폭 함정 (64비트 주소 0x1_2345_6789, a = 64u)</text><rect x="200" y="276" width="250" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="450" y="276" width="250" height="28" class="f-mut" stroke="currentColor"/><rect x="200" y="316" width="250" height="28" class="f-ok" stroke="currentColor"/>
  <rect x="450" y="316" width="250" height="28" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="295">~(64u - 1)</text><text x="10" y="335">~(uint64_t)(64 - 1)</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="325" y="295">00000000 (0 확장!)</text><text x="575" y="295">FFFFFFC0</text><text x="325" y="335">FFFFFFFF</text><text x="575" y="335">FFFFFFC0</text>
  </g>
  <text x="10" y="362" fill="currentColor">32비트에서 ~ 를 한 뒤 64비트로 넓히면 상위가 0 → AND 결과 0x234567C0 (상위 1 이 사라짐)</text>
</svg>
```

**타입 폭 함정** (`demo_traps.c` 실제 출력):

```text
BAD(addr, 64u)            = 0x234567C0
BAD(addr, uint32_t a=64)  = 0x234567C0
BAD(addr, 64)  [int]      = 0x1234567C0
OK (addr, 64u)            = 0x1234567C0
~(64u - 1) as uint64_t    = 0x00000000FFFFFFC0
~(64 - 1)  as uint64_t    = 0xFFFFFFFFFFFFFFC0
```

`#define ALIGN_UP_BAD(x, a) (((x) + (a) - 1) & ~((a) - 1))` 에서 a 가 `64u` 나 `uint32_t` 면 `~(a-1)` 은 32비트 `0xFFFFFFC0` 이고, AND 하려고 64비트로 넓힐 때 **0 확장**된다 → 주소 상위 32비트가 지워진다. 재밌게도 a 가 그냥 `64`(signed int)면 `~63 = -64` 가 **부호 확장**돼 `0xFFFF…FFC0` 이 되어 "우연히" 맞는다. 그래서 테스트에서 안 걸리고 누군가 `64u` 나 `CACHE_LINE` 상수(`32u`)로 바꾼 날 터진다. 고치는 법: 마스크를 x 의 타입으로 먼저 만든다 — `align_up64` 의 `uint64_t m = (uint64_t)a - 1u;` 또는 리눅스식 `(__typeof__(x))(a) - 1` (리눅스 `__ALIGN_KERNEL`, `include/uapi/linux/const.h`).

### Part C — DMA 버퍼와 캐시 라인

Cortex-M7 의 D-cache 라인은 32바이트다(CMSIS `__SCB_DCACHE_LINE_SIZE`). DMA 가 RAM 에 쓴 뒤 CPU 가 읽기 전에 해당 구간을 무효화(invalidate)하는데, 무효화는 **라인 단위**다. 버퍼가 라인 경계에 안 맞으면 위 그림의 경고색 바이트(다른 변수)가 같은 라인에 있어서, CPU 가 거기 써 둔 dirty 값이 무효화와 함께 사라진다. 그래서:

- `cache_span` = 유지보수 연산이 실제로 건드릴 라인: 시작 내림, 끝 올림, 개수 = `(end - start) >> ctz(line)` (2^k 로 나누기 = k 비트 시프트, 나눗셈기 없는 코어에서도 1명령). 끝 `addr + len` 이 정확히 2^32 일 수 있어 64비트로 계산한다 (`cache_span(0xFFFFFFC0, 64, 64)` 테스트).
- `dma_buf_ok` = `((addr | len) & (line - 1)) == 0` — 시작과 길이를 OR 해서 한 번에 검사(둘 다 하위 비트가 0 이어야 OR 도 0). 실무에서는 버퍼를 `__attribute__((aligned(32)))` 로 두고 크기를 `ALIGN_UP(n, 32)` 로 잡아 이 조건을 **설계로** 만족시킨다. 캐시 일관성 자체는 ARCH-M12 참고.

### Part C — 비트맵 할당기와 이벤트 그룹

**비트맵 할당기** (DMA 디스크립터 풀, 메시지 버퍼 풀, FTL 의 빈 블록 맵과 같은 모양 — DS 트랙의 고정 크기 풀 참고). 비트 1 = 사용 중이면 `~map[w]` 의 1 = 빈 슬롯이고, 그중 최하위가 "가장 낮은 번호의 빈 슬롯":

```c
uint32_t freeb = ~map[w];
if (freeb == 0) continue;                 /* 꽉 찬 워드는 한 번 비교로 건너뜀 */
unsigned slot = w * 32u + ctz32(freeb);
if (slot >= nslots) return -1;            /* 70 슬롯이면 워드 2의 비트 6..31 은 패딩 */
map[w] |= lowbit(freeb);                  /* 그 비트 하나만 켬 */
```

추적: `map = {0xFFFFFFFF, 0xFFFFFFFD, 0x3F}` (슬롯 33만 비어 있음) → 워드 0 은 `~ = 0` 이라 건너뜀 → 워드 1: `~0xFFFFFFFD = 0x2`, ctz = 1 → 슬롯 33, `map[1] = 0xFFFFFFFF`. 최악 O(nwords) 워드 검사 = 슬롯 n 개에 O(n/32). 패딩 처리는 두 방법이 있다: 이 레슨처럼 `slot >= nslots` 검사, 또는 초기화 때 패딩 비트를 미리 1(사용 중)로 둬서 검사를 없애기.

**이벤트 그룹.** FreeRTOS `xEventGroupWaitBits` 의 `xWaitForAllBits` 와 같은 두 조건: 하나라도 = `(bits & mask) != 0`, 전부 = `(bits & mask) == mask`. FreeRTOS 이벤트 그룹은 `TickType_t` 가 32비트일 때 24비트만 이벤트로 쓰고 상위 8비트는 커널 제어용이다(공식 문서). mask = 0 은 FreeRTOS 가 assert 로 막는 잘못된 호출이라 여기선 false.

`ev_drain` 은 ISR 이 모아 둔 pending 비트를 태스크가 처리하는 루프다:

| 반복 | pending | ctz → 처리 | clear_lowbit 후 |
|---|---|---|---|
| 1 | `0x80000005` | 0 | `0x80000004` |
| 2 | `0x80000004` | 2 | `0x80000000` |
| 3 | `0x80000000` | 31 | `0` → 끝 (3회 = popcount) |

32비트를 다 훑는 `for (b = 0; b < 32; b++) if (p >> b & 1)` 은 항상 32회. 반대로 "가장 **높은** 번호부터"(우선순위가 높은 비트부터)가 필요하면 `31 - clz` — FreeRTOS 의 Cortex-M 포트가 `configUSE_PORT_OPTIMISED_TASK_SELECTION` 에서 준비 리스트 비트맵에 `clz` 를 쓰는 것과 같은 방식이다(추가 연습 3).

### 복잡도 정리

| 함수 | 시간 | 추가 메모리 | 비고 |
|---|---|---|---|
| `pc_kernighan` | O(k), k = popcount ≤ 32 | 0 | 데이터 의존 → 지터 |
| `pc_swar32/64` | O(log w) = 3단계 + 곱 1 (M4 에서 12명령) | 0 | 분기 없음, 일정 |
| 바이트 LUT | 4번 표 읽기 | 256 B | 플래시 wait state 영향 |
| `lowbit`, `clear_lowbit`, `is_pow2` | O(1), 2~3명령 | 0 | |
| `ctz32`, `clz32`, `next_pow2` | O(log w) | 0 | HW `clz` 가 있으면 1~2명령 |
| `align_up*`, `dma_buf_ok`, `cache_span` | O(1) | 0 | |
| `bm_alloc` | O(n/32) 워드 | n 비트 | 슬롯당 1비트 |
| `ev_drain` | O(popcount) | 0 | |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 57/57 passed, 0.13 s
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_san && ./solution_san   # 57/57, 보고 없음
cc -std=c11 -Wall -Wextra -Werror -O0 solution.c -o solution_O0 && ./solution_O0   # 57/57 — popcount 패턴 인식 없이도
```

요약 줄 (실제 출력): `A/B sweep (16-bit x3 + structured + 2^20 random): bad kern=0 swar=0 … next=0`, `pc_swar64: 2^21 random 64-bit values, bad=0`, `align_up32: 64416 cases … bad=0`, `bitmap: 20000 random alloc/free ops vs bool[100], bad=0`.

데모 세 개 (solution 과 별개, 위 표·출력의 출처):

```bash
cc -std=c11 -Wall -Wextra -Werror -O0 demo_exhaustive.c -o demo_exhaustive && ./demo_exhaustive        # 2^32 전 범위, 약 2분 40초
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
cc -std=c11 -Wall -Wextra -Werror -g -O0 -fsanitize=undefined demo_traps.c -o demo_traps && ./demo_traps   # 일부러 UB
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `int x; … x & -x` | `x = INT_MIN` 에서 `-x` 가 signed 오버플로 = UB (UBSan 실제 메시지 위) | `uint32_t` 로, `x & (0u - x)` |
| `is_pow2(x) = !(x & (x-1))` | 0 이 참이 된다 | `x && !(x & (x-1))` |
| `next_pow2` 고전 smear 를 가드 없이 | 0 → 0, 2^31 초과 → 0 (감김). 0 이 크기로 쓰이면 `size-1` 마스크가 전체가 됨 | 0 과 2^31 초과를 먼저 정의·분기 |
| `1u << (32 - __builtin_clz(x-1))` | x=1 에서 `clz(0)`, x>2^31 에서 32칸 시프트 — 둘 다 UB, arm64 에선 `0x80000001 → 1` 이 조용히 나옴 | 경계 분기 + smear, 또는 `x <= 1` 과 `x > 2^31` 을 먼저 거름 |
| `__builtin_ctz(x)` 를 x=0 가능성 있는 곳에 | 정의 안 됨 (ARM 은 32, x86 BSF 는 미정) | `x ? __builtin_ctz(x) : 32` 또는 popcount 식 |
| `(x + a - 1) & ~(a - 1)` 에 64비트 x, `32u` 상수 a | `~` 가 32비트에서 계산 → 0 확장 → 주소 상위 32비트 소실 | `uint64_t m = (uint64_t)a - 1; (x + m) & ~m` |
| 32비트 ALIGN_UP 넘침 무시 | `0xFFFFFFF0` → 0 으로 감김 | `if (x > UINT32_MAX - (a-1)) return false;` |
| SWAR ② 를 `(x + (x >> 2)) & 0x33333333` | 두 2비트 개수 합(최대 4 = 3비트)이 칸 밖으로 넘쳐 마스크에 잘림 → 니블 `0xF` (2+2=4=`0b100`) 가 0 이 됨 | 더하기 **전에** 각각 `& 0x33333333` |
| 곱셈 없는 판(추가 연습 1)에서 마지막 마스크를 빼먹음 | 위 바이트들에 부분합 찌꺼기가 남아 큰 값이 나옴 | `x += x>>8; x += x>>16; return x & 0x3F;` |
| a 가 2의 거듭제곱이 아닌데 마스크식 사용 | `a = 48` 이면 `~(a-1)` 은 배수 판정이 아님 | 2^k 를 assert(`is_pow2(a)`), 아니면 `(x + a-1) / a * a` |
| `-O2` 에서 `swar(x) == __builtin_popcount(x)` 전 범위 테스트로 "검증 끝" | 양쪽 다 `cnt` 가 되어 컴파일러가 루프를 지움 — 0.00초, 소스 산술 미검사 | `-O0` 빌드로도 돌리기 (위 demo) |
| ISR 에서 Kernighan | 1의 개수에 따라 시간이 3배 이상 흔들림 (5.8 → 19.5 ns) | SWAR 또는 HW 명령, 최악 시간이 고정된 코드 |

## 8. 면접 Q&A

<details>
<summary>Q1. 32비트 정수의 1 개수를 세는 방법을 가능한 만큼 말하고, 각각의 비용을 비교해 보세요.</summary>

① 비트마다 검사: 항상 32회. ② Kernighan `x &= x-1`: popcount 회, 성긴 값에 빠름, 시간이 데이터에 의존. ③ 바이트(또는 니블) 표: 4회(8회) 표 읽기, 256B(16B) 메모리. ④ SWAR: 분기 없이 12명령(M4 실제 asm), 메모리 0. ⑤ 하드웨어: x86 `POPCNT`, arm64 는 NEON `cnt` + `addv`(이 맥 실제 asm). Cortex-M 에는 없어서 clang 이 `__builtin_popcount` 를 SWAR 로 인라인한다. LeetCode 191 Number of 1 Bits. 꼬리: "Hamming distance 는?" → `popcount(a ^ b)` (LeetCode 461 Hamming Distance).

</details>

<details>
<summary>Q2. x & -x 가 왜 최하위 1 비트만 남기나요? 어떤 타입에서 써야 하나요?</summary>

`-x = ~x + 1`. `x = P 1 0^k` 이면 `~x = ~P 0 1^k`, +1 의 자리올림이 최하위 1 자리에서 멈춰 `~P 1 0^k`. 위쪽은 x 와 정반대, 그 자리부터 아래는 같다 → AND 하면 `0 1 0^k`. 반드시 unsigned: unsigned 산술은 mod 2^N 으로 정의되지만 signed `-INT_MIN` 은 UB. `uint8_t/uint16_t` 는 int 로 승격되므로 값은 맞아도 타입이 int — `0u - x` 로 쓰는 습관. 펜윅 트리(Fenwick tree, BIT)의 `i += i & -i` 도 같은 트릭.

</details>

<details>
<summary>Q3. next_pow2(0) 과 next_pow2(3_000_000_000) 은 당신 코드에서 무엇을 돌려주나요? 왜 그렇게 정했나요?</summary>

0 → 1 (2^0 ≥ 0 이 수학적으로 맞고, 크기로 쓰여도 안전). 2^31 초과 → 0 (32비트에 답이 없음을 알리는 값 — 0 은 정상 결과로 나올 수 없으니 오류 표시로 쓸 수 있다). 고전 smear 는 0 과 오버플로 둘 다 0 을 돌려주는데 그걸 "의도"라고 말할 수 있으면 그것도 답이다 — 중요한 건 **정의를 말하고 테스트로 고정**하는 것. 64비트판은 `x |= x >> 32` 한 줄을 더하고 2^63 초과를 거른다.

</details>

<details>
<summary>Q4. ALIGN_UP(x, a) 매크로를 쓰고, 이 매크로가 틀리는 경우를 모두 말해 보세요.</summary>

`((x) + (a) - 1) & ~((a) - 1)`. ① a 가 2의 거듭제곱이 아니면 의미 없음 → assert 또는 나눗셈판. ② 32비트에서 x 가 마지막 블록 안이면 감겨서 0 (넘침 ⟺ 답 = 2^32 이라 `x > UINT32_MAX - (a-1)` 로 정확히 검사). ③ x 가 64비트인데 a 가 `unsigned`/`uint32_t` 면 `~(a-1)` 이 32비트에서 계산돼 0 확장 → 상위 32비트 소실 (`64` 처럼 signed 상수면 부호 확장으로 우연히 맞아서 더 위험). ④ 매크로 인자 이중 평가(`ALIGN_UP(p++, 4)`). 넘침 없는 대안: `x + ((0u - x) & (a - 1))` 후 `결과 < x` 면 넘침 (추가 연습 2).

</details>

<details>
<summary>Q5. Cortex-M4 에는 popcount 명령이 없습니다. __builtin_popcount 를 쓰면 무엇이 나오나요? M0+ 의 ctz 는?</summary>

이 맥의 clang 으로 `thumbv7em` 을 컴파일해 보면 인라인 SWAR 12명령(손으로 쓴 SWAR 과 같음) — 라이브러리 호출 없음. 64비트판은 두 벌. M0+(`thumbv6m`)도 SWAR 인라인, 다만 상수를 리터럴 풀에서 `ldr`. M0+ 는 `clz` 도 `rbit` 도 없어서 `__builtin_ctz` 가 `x & -x` → `* 0x077CB531` → `>> 27` → 32바이트 표의 de Bruijn 방식으로 나왔다. M3/M4/M7 은 `rbit + clz` 2명령. GCC 는 libgcc 호출(`__popcountsi2`)을 내는 경우가 있다고 알려져 있으니 맵 파일로 확인. M0/M0+ 의 곱셈기는 1사이클/32사이클 구현 옵션이라, 느린 곱셈기면 shift-add 판(추가 연습 1)이 낫다.

</details>

<details>
<summary>Q6. ISR 안에서 쓸 popcount 를 고르라면?</summary>

최악 실행 시간(WCET)이 입력과 무관한 것: HW 명령 또는 SWAR. Kernighan 은 평균은 괜찮아도 최악 32회이고 이 맥 측정에서 1의 개수에 따라 5.8 → 19.5 ns 로 흔들렸다 — 지터. LUT 은 표가 플래시에 있으면 wait state, 캐시 미스로 흔들릴 수 있다. 그리고 "ISR 에서 정말 세야 하나?" 를 되묻는다 — 보통 ISR 은 비트만 세우고(이벤트 그룹, `FromISR` API) 세는 건 태스크에서.

</details>

<details>
<summary>Q7. DMA 수신 버퍼를 캐시 라인에 정렬해야 하는 이유와, 정렬 검사를 한 줄로 쓰면?</summary>

D-cache 유지보수(clean/invalidate)는 라인 단위라 버퍼와 라인을 공유하는 이웃 변수가 무효화로 dirty 값을 잃거나, 반대로 clean 이 DMA 가 쓴 데이터를 덮을 수 있다. 시작과 길이를 모두 라인 배수로: `((addr | len) & (LINE - 1)) == 0`. 정렬이 안 된 버퍼가 덮는 라인 수는 `(ALIGN_UP(addr+len) - ALIGN_DOWN(addr)) >> log2(LINE)` — 100바이트 버퍼도 4라인을 건드릴 수 있다. 정렬은 `aligned(32)` 속성 + 크기 올림으로 설계 단계에서 해결 (ARCH-M12).

</details>

<details>
<summary>Q8. 비트맵 할당기에서 슬롯이 수만 개면 첫 빈 슬롯 찾기가 느립니다. 어떻게 빠르게 하나요?</summary>

2단 비트맵: 상위 워드의 비트 i = "하위 워드 i 에 빈 슬롯이 있다". 상위에서 ctz 한 번, 하위에서 ctz 한 번 → 32×32 = 1024 슬롯을 2번의 ctz 로. 할당/해제 때 하위 워드가 꽉 차거나 비워지면 상위 비트를 갱신. "비트맵 + 첫 비트 찾기(ctz/clz)" 는 RTOS 준비 우선순위 비트맵(FreeRTOS `31 - clz`)과 같은 발상이고, 단을 늘리면 슬롯 수의 log₃₂ 단계로 찾는다. 연속 k 칸이 필요하면 `f &= f >> 1` 을 k−1 번(추가 연습 5).

</details>

## 9. 추가 연습

### 연습 1 — 곱셈 없는 SWAR

곱셈기가 느린 코어(Cortex-M0+ 의 32사이클 곱셈 옵션)를 위해 `* 0x01010101 >> 24` 를 시프트·덧셈으로 바꿔라. 마지막 마스크는 몇 비트여야 하나?

<details>
<summary>답 보기</summary>

③ 단계 뒤 바이트 4개(각 ≤ 8): `x += x >> 8;` → 바이트 0 = b0+b1, 바이트 2 = b2+b3 (각 ≤ 16). `x += x >> 16;` → 바이트 0 = 합 ≤ 32. 32 = `0b100000` 은 6비트 → `return x & 0x3F;`. 위쪽 바이트에는 부분합 찌꺼기가 남아 있으니 마스크를 빼면 틀린다. 16비트 전 범위(x 와 ~x) + 랜덤 2^20 개를 builtin 과 대조.

</details>

### 연습 2 — 넘침 없는 ALIGN_UP, ALIGN_DOWN, is_aligned

`pad_to(x, a)` = 다음 배수까지 더해야 하는 바이트 수를 한 식으로 쓰고, 그걸로 넘침을 검사하는 `align_up_pad` 를 만들어라.

<details>
<summary>답 보기</summary>

`pad = (0u - x) & (a - 1)` = (−x) mod a. `r = x + pad`, `r < x` 이면 감김(결과 2^32). 장점: `UINT32_MAX - (a-1)` 같은 상수 비교 없이 "더한 뒤 작아졌나" 하나로 검사 — 패킷 헤더 뒤 패딩 바이트 수 계산에도 그대로 쓴다. `align_down32(x, a) = x & ~(a - 1)`, `is_aligned32(x, a) = (x & (a - 1)) == 0`. 랜덤 20만 개(절반은 `0xFFFFFF00` 이상)를 64비트 나눗셈 결과와 대조.

</details>

### 연습 3 — floor/ceil log2 와 가장 높은 우선순위 준비 태스크

`ilog2_floor(x)` (x ≥ 1), `ilog2_ceil(x)`, 그리고 준비 비트맵 `ready` (비트 i = 우선순위 i 태스크 준비, 큰 수가 높은 우선순위)에서 실행할 우선순위 `top_ready(ready)` (없으면 −1).

<details>
<summary>답 보기</summary>

`floor = 31 - clz32(x)`, `ceil = x <= 1 ? 0 : 32 - clz32(x - 1)` (next_pow2 와 같은 `x-1` 트릭, `ceil(0x80000001) = 32`), `top_ready = ready ? 31 - clz32(ready) : -1`. FreeRTOS Cortex-M 포트의 최적화된 태스크 선택이 바로 `31 - clz(uxReadyPriorities)` 다 (그래서 그 모드에서는 우선순위가 32개로 제한). 링 버퍼 크기 n 에서 인덱스 시프트 양을 구할 때도 `ilog2_floor`.

</details>

### 연습 4 — de Bruijn 곱셈 ctz

clang 이 Cortex-M0+ 용으로 낸 방식을 C 로 재현하라: 상수 `0x077CB531`, 32칸 표.

<details>
<summary>답 보기</summary>

`0x077CB531` 은 길이 32 의 de Bruijn 수열 B(2,5) — 위에서부터 읽은 5비트 창 32개가 모두 다르다. `x & -x = 2^i` 를 곱하면 상수를 i 칸 왼쪽으로 민 것이고 `>> 27` 은 그 창 = i 마다 다른 5비트 값. 표는 `for i: tab[((1u << i) * 0x077CB531u) >> 27] = i` 로 만든다. `ctz = x ? tab[((x & (0u - x)) * 0x077CB531u) >> 27] : 32`. 32개 한 비트·`0xFFFFFFFF << i`·랜덤 2^20 개를 `__builtin_ctz` 와 대조. 표 32바이트, 곱셈 1번 — `clz` 명령이 없는 코어의 표준 답 (Bit Twiddling Hacks 에 같은 상수).

</details>

### 연습 5 — 연속 k 칸 빈 자리 (한 워드)

DMA 디스크립터 k 개를 연속으로 잡아야 한다. `used` (1 = 사용 중) 에서 연속 k 칸이 비어 있는 가장 낮은 시작 번호를 찾아라 (1 ≤ k ≤ 32, 없으면 −1).

<details>
<summary>답 보기</summary>

`f = ~used` 후 `f &= f >> 1` 을 k−1 번: 반복 후 비트 j 가 1 ⟺ j..j+k−1 이 모두 빈 칸 (위에서 0 이 들어오므로 워드 밖으로 넘어가는 구간은 자동 제외). 답 = `ctz(f)`, `f == 0` 이면 −1. O(k) 연산, 분기 없음. 시프트를 1,2,4… 로 두 배씩 키우면 O(log k) 로도 된다. 랜덤 10만 개를 이중 루프 브루트포스와 대조.

</details>

### 연습 6 — 0..n 의 popcount 표 (LeetCode 338 Counting Bits)

`out[i] = popcount(i)` 를 i = 0..n 에 대해 O(n) 에, 내장 함수 없이.

<details>
<summary>답 보기</summary>

`out[i] = out[i & (i - 1)] + 1` — 최하위 1 을 뺀 수는 i 보다 작아 이미 계산돼 있다 (또는 `out[i >> 1] + (i & 1)`). Part A 의 바이트 LUT 을 부팅 때 만드는 코드와 같다. 0..65535 를 builtin 과 대조.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 19/19 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_san && ./practice_san   # 19/19, 보고 없음
```

## 10. 핵심 정리

- `x & (x-1)` = 최하위 1 제거, `x & -x` = 최하위 1 만 남김 (`x = P 1 0^k` 로 유도). **unsigned 에서만** — `-INT_MIN` 은 UB, 작은 타입은 int 로 승격.
- popcount: Kernighan O(popcount) 데이터 의존 · SWAR 3단계(`0x55/0x33/0x0F`) + `*0x01010101>>24` 분기 없음 · HW(arm64 `cnt`). 마스크는 ② 는 더하기 전, ③ 은 뒤.
- `is_pow2 = x && !(x & (x-1))`. `ctz = popcount(lowbit - 1)`, `clz = 32 - popcount(smear)`, 둘 다 0 → 32. `__builtin_ctz/clz(0)` 은 정의 안 됨.
- `next_pow2`: `x-1` → smear(1,2,4,8,16) → `+1`. 0 과 2^31 초과는 **먼저 정의**(여기선 1 과 0). `1u << (32 - clz(x-1))` 는 x=1, x>2^31 에서 UB.
- `ALIGN_UP = (x + a-1) & ~(a-1)` (a = 2^k). 32비트 넘침 ⟺ 답 = 2^32 ⟺ `x > UINT32_MAX - (a-1)`. 64비트 x 에 32비트 unsigned a 면 `~(a-1)` 0 확장으로 상위 소실 → 마스크를 x 의 타입으로.
- 펌웨어: DMA 버퍼는 `((addr|len) & (LINE-1)) == 0`, 비트맵 할당은 `~map` 의 ctz, 이벤트 처리는 `ctz` + `clear_lowbit` 루프(반복 = popcount), 우선순위는 `31 - clz`.
- 검증 함정: `-O2` 에서 SWAR·Kernighan 이 `cnt` 로 바뀌어 builtin 대조가 0초에 "통과" — `-O0` 으로도 돌려 2^32 전 범위 0 불일치 확인.

## 참고자료

- Sean Eron Anderson, *Bit Twiddling Hacks* — popcount(병렬 방식), 최하위 비트, 다음 2의 거듭제곱, de Bruijn ctz (`0x077CB531`): <https://graphics.stanford.edu/~seander/bithacks.html>
- Henry S. Warren Jr., *Hacker's Delight* (2nd ed.), 2장 "Basics"(최하위 비트 조작), 3장 "Power-of-2 Boundaries"(flp2/clp2, 올림), 5장 "Counting Bits"(popcount, nlz/ntz).
- ISO/IEC 9899:2011 (C11) — 6.2.5p9 unsigned 산술은 mod 2^N, 6.5p5 signed 오버플로 UB, 6.3.1.1 정수 승격, 6.5.7p3 시프트 양 ≥ 폭이면 UB.
- GCC Manual, "Other Built-in Functions" — `__builtin_clz/ctz`: "if x is 0, the result is undefined": <https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html>
- Arm, *Arm Architecture Reference Manual for A-profile* — `CNT`(SIMD), `ADDV`, `RBIT`, `CLZ`, `LSLV`(시프트 양 mod 데이터 폭). *ARMv7-M Architecture Reference Manual* — `CLZ`, `RBIT` (popcount 명령 없음).
- Arm CMSIS-Core(M) `core_cm7.h` — `SCB_InvalidateDCache_by_Addr`, `__SCB_DCACHE_LINE_SIZE` (32): <https://arm-software.github.io/CMSIS_6/latest/Core/index.html>
- FreeRTOS 공식 문서 — Event Groups, `xEventGroupWaitBits` (이벤트 비트 24개 / 16비트 틱이면 8개): <https://www.freertos.org/>; Cortex-M 포트의 `portGET_HIGHEST_PRIORITY` (`configUSE_PORT_OPTIMISED_TASK_SELECTION`).
- Linux kernel `include/uapi/linux/const.h` — `__ALIGN_KERNEL(x, a)` 가 a 를 `__typeof__(x)` 로 캐스트하는 이유.
- LeetCode 191 Number of 1 Bits · 231 Power of Two · 338 Counting Bits · 461 Hamming Distance.
- AlgoMonster 노트 M01 §6 Math Basics (Don 정리본) — 진법·log₂ 기초 (이 레슨의 2^k·비트 길이 직관).
- 관련 레슨: C-B01 (비트 set/clear, 추가 연습 1~3 에서 이 레슨의 식을 처음 봄), ALGO-B13 (비트 반전·니블 스왑·패리티), ALGO-A07 (Gray 코드·SWAR haszero·Gosper), ALGO-M15 (정수 산술), ARCH-M12 (DMA 캐시 일관성 clean/invalidate).
