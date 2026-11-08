---
id: DS-B08
time: 50분
updated: 2026-11-07
---
# DS-B08 비트맵(Bitset): 할당 상태 관리

## 학습 목표

- `uint32_t` 워드 배열로 N비트 비트맵을 만들고 `i / 32`, `i % 32` 로 **set / clear / test** 를 구현한다.
- 한 비트씩 보는 **단순 루프 버전**과, 워드 단위로 건너뛰며 `__builtin_ctz` 로 위치를 바로 뽑는 **빠른 버전**을 둘 다 쓰고 랜덤 대조 테스트로 일치를 확인한다.
- **연속 k개 빈 비트**(first fit)를 "다음 0 → 다음 1" 점프로 찾는다.
- N이 32의 배수가 아닐 때 마지막 워드의 **꼬리 비트**, `ctz(0)` UB 같은 경계 함정을 피한다.

## 1. 문제

"어떤 블록/채널/ID 가 비어 있나?" 를 기억하는 가장 작은 자료구조가 **비트맵**이다 — 항목 하나에 1비트. SSD FTL 의 free block 비트맵, ext4·FAT 의 블록 비트맵, RTOS 의 준비 태스크 우선순위 비트맵, DMA 채널·USB 엔드포인트 할당표, 심지어 NVIC 의 인터럽트 pending 레지스터도 비트맵이다. 면접에서는 "4096개 블록 중 빈 블록을 빨리 찾아라" → 루프로 짜면 꼬리질문으로 "더 빠르게?" 가 반드시 나온다.

비트 `i` 는 워드 `i / 32` 의 비트 `i % 32` 에 산다. 이 레슨에서 **1 = 사용 중, 0 = 비어 있음** 이다. 찾는 함수는 못 찾으면 `n` 을 돌려준다 (Linux `find_next_bit()` 과 같은 관례 — `-1` 대신 "범위 끝").

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">N = 200 비트 비트맵 = uint32_t bm[7]  (7 × 32 = 224 비트, 마지막 24비트는 꼬리)</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="44" width="96" height="34"/><rect x="120" y="44" width="96" height="34"/>
    <rect x="220" y="44" width="96" height="34"/><rect x="320" y="44" width="96" height="34"/>
    <rect x="420" y="44" width="96" height="34"/><rect x="520" y="44" width="96" height="34"/>
    <rect x="620" y="44" width="96" height="34"/>
  </g>
  <rect x="644" y="44" width="72" height="34" class="f-warn" stroke="currentColor"/>
  <line x1="644" y1="44" x2="644" y2="78" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="68" y="66">bm[0]</text><text x="168" y="66">bm[1]</text><text x="268" y="66">bm[2]</text>
    <text x="368" y="66">bm[3]</text><text x="468" y="66">bm[4]</text><text x="568" y="66">bm[5]</text>
    <text x="632" y="66">[6]</text><text x="680" y="66">꼬리</text>
    <text x="68" y="96">0..31</text><text x="168" y="96">32..63</text><text x="568" y="96">160..191</text>
    <text x="632" y="96">192..</text><text x="680" y="96">≥200</text>
  </g>
  <text x="10" y="132" fill="currentColor">bm[0] 의 아래 16비트를 펼치면 (오른쪽이 비트 0):</text>
  <rect x="380" y="144" width="80" height="34" class="f-ok"/>
  <rect x="580" y="144" width="40" height="34" class="f-ok"/>
  <rect x="140" y="144" width="160" height="34" class="f-ok"/>
  <rect x="60" y="144" width="640" height="34" stroke="currentColor" fill="none"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="80" y="166">1</text><text x="120" y="166">1</text><text x="160" y="166">0</text><text x="200" y="166">0</text>
    <text x="240" y="166">0</text><text x="280" y="166">0</text><text x="320" y="166">1</text><text x="360" y="166">1</text>
    <text x="400" y="166">0</text><text x="440" y="166">0</text><text x="480" y="166">1</text><text x="520" y="166">1</text>
    <text x="560" y="166">1</text><text x="600" y="166">0</text><text x="640" y="166">1</text><text x="680" y="166">1</text>
    <text x="80" y="196" font-size="12">15</text><text x="600" y="196" font-size="12">2</text><text x="680" y="196" font-size="12">0</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="226">ffz = 2,  fns(from=2) = 3,  연속 2칸 = 6 ([6,8)) — 비트 2 는 한 칸뿐이라 안 된다.</text>
    <text x="10" y="250">연속 4칸 = 10 ([10,14)).  꼬리 비트(200..223)는 값이 무엇이든 "없는 비트"로 취급해야 한다.</text>
  </g>
</svg>
```

(그림의 초록 칸이 빈 비트다. 비트 2, 6~7, 10~13 이 0.)

### Part A — set / clear / test

`bm_set(bm, i)`, `bm_clear(bm, i)`, `bool bm_test(bm, i)`. 워드 인덱스 `i / 32u`, 비트 위치 `i % 32u`, 마스크는 반드시 `1u << …`.

### Part B — 빈 비트·사용 비트 찾기: 루프 버전 vs ctz 버전

| 함수 | 의미 | 못 찾으면 |
|---|---|---|
| `ffz(bm, n)` | 첫 0비트 (find first zero) | `n` |
| `fns(bm, n, from)` | `from` 이상에서 첫 1비트 (find next set) | `n` |
| `fnz(bm, n, from)` | `from` 이상에서 첫 0비트 (find next zero) — Part C 의 도우미 | `n` |

각각 `_loop` (비트 하나씩 `bm_test`) 와 `_ctz` (워드 단위, `__builtin_ctz`) 두 버전을 만든다. `from >= n` 이면 바로 `n`. 마지막 워드의 `n` 이상 비트(꼬리)에 쓰레기가 있어도 결과가 `n` 을 넘으면 안 된다.

### Part C — 연속 k개 빈 비트 (first fit)

`zrun(bm, n, k)` — 0이 k개 연달아 있는 **가장 앞** 구간의 시작 인덱스. 없으면 `n`. (k ≥ 1)

- `zrun_loop`: 연속 0 개수를 세다가 1을 만나면 0으로 리셋.
- `zrun_ctz`: `s = fnz(…, 0)` → `e = fns(…, s)` → 빈 구간 `[s, e)` 길이가 k 이상이면 `s`, 아니면 `s = fnz(…, e)` 로 다음 빈 구간으로 **점프**.

테스트는 정해진 경우 외에, 밀도 1/8~7/8 · n = 1~200 · 꼬리 쓰레기 비트가 섞인 랜덤 비트맵 5,000개에서 루프 버전, ctz 버전, 하네스의 정답지(oracle) 셋을 대조한다 (고정 시드 xorshift32).

## 2. Problem (English)

> **Bitmap allocator primitives.**
>
> A bitmap of `n` bits is stored in `uint32_t bm[(n+31)/32]`; bit `i` lives in word `i/32`, bit `i%32`. A set bit means "in use". Bits at positions `>= n` in the last word are undefined and must be ignored.
>
> 1. Implement `bm_set`, `bm_clear`, `bm_test`.
> 2. Implement `find_first_zero(bm, n)`, `find_next_set(bm, n, from)` and `find_next_zero(bm, n, from)`, each returning `n` if nothing is found. Write a bit-by-bit reference version and a word-at-a-time version using `__builtin_ctz`, and cross-check them on random inputs.
> 3. Implement `find_zero_run(bm, n, k)` that returns the lowest index `s` such that bits `s .. s+k-1` are all zero (`s + k <= n`), or `n`.
>
> Example: `n = 200`, all bits set except 10–12, 40–47 and 100–199: `find_zero_run(k=3) == 10`, `k=4 → 40`, `k=9 → 100`, `k=101 → 200`.
>
> Follow-ups: what does `__builtin_ctz(0)` return? How would you do this on a Cortex-M0 without a CLZ instruction?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "8/25 passed" (답이 "못 찾음 = n" 인 경우만 우연히 통과) — TODO를 채워 25/25 를 만든다
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

## 4. 힌트

<details>
<summary>힌트 1 — Part A 는 C-B01 과 같다</summary>

`bm[i / 32u] |= 1u << (i % 32u);` 처럼 워드를 고른 뒤 단일 비트 연산. 컴파일러는 `/ 32u`, `% 32u` 를 `>> 5`, `& 31` 로 바꿔준다 (unsigned 일 때만 깔끔하게).

</details>

<details>
<summary>힌트 2 — "0을 찾기" = "뒤집어서 1을 찾기"</summary>

`__builtin_ctz(x)` 는 x 의 가장 낮은 1비트 위치(trailing zero 개수)를 준다. 0비트를 찾으려면 `~bm[w]` 에 ctz. 워드가 `0xFFFFFFFF`(꽉 참)이면 `~` 가 0이라 다음 워드로 넘어가면 된다. **`x == 0` 일 때 ctz 를 부르면 UB** 다.

</details>

<details>
<summary>힌트 3 — from 이 워드 중간일 때</summary>

첫 워드에서는 `from` 아래 비트를 지워야 한다: `x = bm[w] & (0xFFFFFFFFu << (from % 32u))`. 그다음 워드부터는 마스크 없이.

</details>

<details>
<summary>힌트 4 — 꼬리 비트</summary>

마지막 워드의 `n` 이상 비트는 1일 수도 0일 수도 있다. 찾은 인덱스 `i` 가 `i >= n` 이면 "못 찾음" (`n`) 으로 돌려주면 된다. 마지막 워드이므로 더 볼 곳도 없다.

</details>

<details>
<summary>힌트 5 — 연속 k개는 "빈 구간" 단위로</summary>

빈 구간의 시작 `s` 는 `fnz(s')`, 끝(첫 사용 비트) `e` 는 `fns(s)`. 길이 `e - s`. 모자라면 `e` 부터 다시 `fnz`. 꽉 찬 워드와 빈 워드를 모두 32비트씩 건너뛴다.

</details>

## 5. 풀이

### Part A — 인덱스 쪼개기

| i | `i / 32` (워드) | `i % 32` (비트) | 마스크 |
|---|---|---|---|
| 0 | 0 | 0 | `0x00000001` |
| 31 | 0 | 31 | `0x80000000` |
| 32 | 1 | 0 | `0x00000001` |
| 199 | 6 | 7 | `0x00000080` |

테스트 첫 줄 `bm[0] == 0x80000001 && bm[1] == 0x1 && bm[6] == 1u << 7` 이 이 표 그대로다. 비트 31 때문에 `1 << 31` (signed, UB) 이 아니라 `1u << 31`.

### Part B — 루프 버전: 정답지 역할

루프 버전은 4줄이고 틀리기 어렵다. 대신 **비트마다** `bm_test` (나눗셈·시프트·AND·분기)를 한다. 빠른 버전을 만들 때 이것을 **정답지**로 두고 랜덤 입력에서 같은 답이 나오는지 보는 것이 이 레슨의 테스트 전략이다.

### Part B — ctz 버전: 워드 단위로 건너뛰기

```svg
<svg viewBox="0 0 720 262" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">fnz_ctz — 0비트 찾기 = 반전 후 가장 낮은 1비트 (8비트만 표시)</text>
  <g fill="currentColor">
    <text x="10" y="62">bm[w]</text><text x="10" y="102">~bm[w]</text><text x="10" y="142">&amp; (~0u &lt;&lt; from%32)</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="170" y="44" width="320" height="28"/><rect x="170" y="84" width="320" height="28"/><rect x="170" y="124" width="320" height="28"/>
  </g>
  <rect x="250" y="124" width="40" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="370" y="84" width="40" height="28" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="190" y="63">1</text><text x="230" y="63">1</text><text x="270" y="63">0</text><text x="310" y="63">1</text>
    <text x="350" y="63">1</text><text x="390" y="63">0</text><text x="430" y="63">1</text><text x="470" y="63">1</text>
    <text x="190" y="103">0</text><text x="230" y="103">0</text><text x="270" y="103">1</text><text x="310" y="103">0</text>
    <text x="350" y="103">0</text><text x="390" y="103">1</text><text x="430" y="103">0</text><text x="470" y="103">0</text>
    <text x="190" y="143">0</text><text x="230" y="143">0</text><text x="270" y="143">1</text><text x="310" y="143">0</text>
    <text x="350" y="143">0</text><text x="390" y="143">0</text><text x="430" y="143">0</text><text x="470" y="143">0</text>
    <text x="190" y="170" font-size="12">7</text><text x="270" y="170" font-size="12">5</text>
    <text x="390" y="170" font-size="12">2</text><text x="470" y="170" font-size="12">0</text>
  </g>
  <g fill="currentColor">
    <text x="510" y="63">사용 중 = 1</text>
    <text x="510" y="103">빈 칸이 1이 됨</text>
    <text x="510" y="143">from = 3 이면 비트 2 제거</text>
    <text x="10" y="200">ctz(0b0010_0000) = 5 → 답 = w × 32 + 5.</text>
    <text x="10" y="222">x == 0 이면 이 워드엔 답이 없다 → 다음 워드로 (32비트를 한 번에 건너뜀).</text>
    <text x="10" y="244">from 없이(ffz) 찾으면 비트 2 가 답 — 루프는 비트 0, 1, 2 를 차례로 검사, ctz 버전은 ctz 한 번.</text>
  </g>
</svg>
```

`fns_ctz` 는 `~` 만 빼면 똑같다. 핵심 순서:

1. `from >= n` → `n` (범위 밖 워드를 읽지 않게 가장 먼저).
2. 첫 워드: `x = (뒤집거나 그대로) & (0xFFFFFFFFu << (from % 32u))`.
3. `x != 0` 이면 `i = w*32 + ctz(x)`, `i < n ? i : n`. — **`x != 0` 확인 없이 ctz 금지.**
4. 아니면 `++w`, 워드가 끝났으면 `n`.

비용 차이를 실제로 세어봤다 (추가 연습 5, 4096비트가 꽉 차 있고 빈 비트가 4000번 하나):

| 버전 | 세는 단위 | 횟수 (실행 결과) |
|---|---|---|
| `ffz_loop` | 비트 검사 | 4001 |
| `ffz_ctz` | 워드 읽기 | 126 |

```text
ffz on 4096 bits (free bit 4000): loop 4001 bit tests, ctz 126 word reads
```

약 32배. Cortex-M3/M4/M7 에는 `CLZ` 와 `RBIT` 명령이 있어서 `__builtin_ctz(x)` 가 `RBIT` + `CLZ` 두 명령(2사이클)이 된다. **Cortex-M0/M0+ (ARMv6-M) 에는 CLZ 가 없다** → 컴파일러가 라이브러리 함수(`__ctzsi2`)를 부르거나, 직접 de Bruijn 곱셈 표를 써야 한다 (면접 Q3).

꼬리 처리는 "결과가 n 이상이면 n" 한 줄로 해결된다. 테스트는 이걸 두 방향으로 찌른다: `bm[6] = 0xFFFFF000u` (비트맵은 비었고 꼬리 204~223 에만 쓰레기 1) 에서 `fns(…, 0)` 이 204 를 돌려주면 FAIL, `bm[6] = 0x00000FFFu` (꽉 찼고 꼬리 204~ 가 0) 에서 `ffz` 가 204 를 돌려주면 FAIL. 쓰레기가 정확히 비트 200 에서 시작하면 마스크를 빼먹어도 우연히 `n` 이 나와서 버그가 숨는다 — 그래서 204 에서 시작하게 했다. 실제로 `i < n ? i : n` 을 `i` 로 바꾸는 돌연변이(mutation)를 넣어보면 이 고정 테스트와 랜덤 대조가 함께 FAIL 한다.

### Part C — 연속 k개: 빈 구간 점프

```svg
<svg viewBox="0 0 720 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">zrun_ctz(k = 9) — n = 200, 빈 구간 [10,13) [40,48) [100,200)</text>
  <rect x="20" y="50" width="680" height="30" class="f-mut"/>
  <rect x="54" y="50" width="10" height="30" class="f-ok"/>
  <rect x="156" y="50" width="27" height="30" class="f-ok"/>
  <rect x="360" y="50" width="340" height="30" class="f-ok"/>
  <rect x="20" y="50" width="680" height="30" stroke="currentColor" fill="none"/>
  <defs><marker id="b08a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="20" y="98">0</text><text x="54" y="98">10</text><text x="156" y="98">40</text><text x="360" y="98">100</text><text x="700" y="98">200</text>
  </g>
  <path d="M59 108 Q110 140 158 110" stroke="currentColor" fill="none" marker-end="url(#b08a)"/>
  <path d="M170 108 Q265 150 358 110" stroke="currentColor" fill="none" marker-end="url(#b08a)"/>
  <g fill="currentColor">
    <text x="20" y="160">① s=fnz(0)=10, e=fns(10)=13 → 길이 3 &lt; 9 → s=fnz(13)=40</text>
    <text x="20" y="182">② e=fns(40)=48 → 길이 8 &lt; 9 → s=fnz(48)=100    ③ e=fns(100)=200(n) → 길이 100 ≥ 9 → 답 100</text>
    <text x="20" y="204">점프 3번. 루프 버전은 비트 108개를 하나씩 본다.</text>
  </g>
</svg>
```

루프 버전의 함정은 두 가지다: 1을 만나면 `run = 0` 리셋, 그리고 답은 현재 위치 `i` 가 아니라 **시작 위치 `i + 1 - k`**. ctz 버전은 `e - s >= k` (`>` 아님) 과, `e == n` 일 때 다음 `fnz(e)` 가 `n` 을 돌려줘서 자연히 루프가 끝난다는 점만 맞으면 된다. 워드 경계를 걸친 빈 구간(테스트의 28~36)도 `fnz`/`fns` 가 워드를 넘어 찾아주므로 따로 처리할 필요가 없다.

이 "빈 구간 점프" 는 Linux `bitmap_find_next_zero_area()` 와 같은 구조이고, SSD 펌웨어에서 연속된 free 블록/페이지 영역을 찾을 때, 파일시스템이 연속 블록을 할당할 때(ext4 mballoc 의 비트맵 스캔) 그대로 쓰인다.

### 왜 대조 테스트인가

| 정답지 | 장점 | 한계 |
|---|---|---|
| 손으로 쓴 정해진 경우 | 의도(꼬리, 경계, from 중간)를 명확히 검사 | 생각 못 한 경우는 못 잡음 |
| 랜덤 + 루프 버전/oracle | 수천 개 입력, 밀도·n·from·k 를 섞음 | 정답지 자체가 틀리면 같이 틀림 → 둘 다 쓴다 |

랜덤 테스트에서 `n` 을 1~200 으로 바꾸고 꼬리 비트에 쓰레기를 남겨둔 것이 핵심이다. `n = 200` 하나만 썼다면 꼬리 처리 버그 일부는 숨는다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 25/25 passed
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g solution.c -o sol_asan && ./sol_asan   # 범위 밖 워드 읽기·시프트 UB 확인
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `bm[i/32] \|= 1 << (i%32)` | `1` 은 signed int → 비트 31 에서 UB | `1u << (i % 32u)` |
| `return w*32 + __builtin_ctz(~bm[w]);` 를 검사 없이 | 꽉 찬 워드에서 `ctz(0)` → **UB** (x86 `BSF` 는 결과 미정, `TZCNT` 는 32) | `if (x) …` 로 0 워드는 건너뛴다 |
| 찾은 인덱스를 그대로 반환 | 꼬리 비트 때문에 `n` 이상 값이 나옴 → 호출자가 범위 밖 블록 할당 | `return i < n ? i : n;` |
| 첫 워드에 from 마스크를 안 함 | `from` 보다 앞의 비트를 답으로 돌려줌 | `x &= 0xFFFFFFFFu << (from % 32u)` |
| `from >= n` 검사를 뒤에 둠 | `bm[from / 32]` 이 배열 밖을 읽는다 (ASan 이 잡음) | 함수 첫 줄에서 `return n` |
| zrun 에서 `return i;` | 구간의 **끝**을 돌려줌 | `return i + 1u - k;` |
| zrun 에서 `e - s > k` | 정확히 k 칸인 구간을 놓침 | `>=` |
| 64비트 워드(`uint64_t`)에 `__builtin_ctz` | `unsigned int` 로 잘려서 상위 32비트를 못 봄 | `__builtin_ctzll` (Linux 는 `unsigned long` + `__ffs`) |
| ISR 과 메인이 같은 워드에 `\|=` | RMW 경쟁 → 다른 비트의 변경이 사라짐 (C-B01 의 BSRR 이야기와 같다) | 임계 구역, `atomic_fetch_or`, Cortex-M3/M4 bit-band |

## 8. 면접 Q&A

<details>
<summary>Q1. 4096개 블록의 사용 비트맵에서 빈 블록을 가장 빨리 찾는 방법은?</summary>

워드 단위로 `0xFFFFFFFF` 가 아닌 워드를 찾고(비교 하나로 32블록 건너뜀), 그 워드에서 `ctz(~w)` 로 위치를 바로 뽑는다 — 이 레슨 실측으로 비트 검사 4001번 → 워드 읽기 126번. 더 빠르게 하려면 **2단계 비트맵**: 워드 128개 각각이 "빈칸 있음"인지를 요약 비트맵 4워드에 두고, 요약에서 ctz → 해당 워드에서 ctz. TLSF 할당기가 이 2단계 비트맵 + ffs/fls 로 O(1) 를 얻는다. 64비트 CPU 면 `uint64_t` 워드 + `ctzll` 로 반만큼.

</details>

<details>
<summary>Q2. __builtin_ctz(0) 은 무엇을 반환하나요?</summary>

**정의되지 않음(UB)**. GCC/Clang 문서가 명시한다. 하드웨어마다 다르다: x86 `BSF` 는 목적지 레지스터가 미정, `TZCNT`(BMI1) 는 32, ARM `RBIT+CLZ` 는 32. 컴파일러는 "0이 아니다"를 가정하고 최적화할 수 있으므로 반드시 앞에서 0 을 거른다. C23 은 `stdc_trailing_zeros()` 를 표준화했는데 이것은 0 에 대해 폭(32)을 돌려주도록 정의되어 있다.

</details>

<details>
<summary>Q3. Cortex-M0 처럼 CLZ 명령이 없으면 ctz 를 어떻게 구하나요?</summary>

① 가장 낮은 1비트만 남김: `b = x & -x` (2의 거듭제곱). ② de Bruijn 수 `0x077CB531` 을 곱하면 상위 5비트가 비트 위치마다 서로 다른 값이 된다 → 32칸 표로 변환: `tbl[(b * 0x077CB531u) >> 27]`. 곱셈 1번 + 표 1번이라 분기가 없고 결정적이다. 아니면 이진 탐색(`if (!(x & 0xFFFF)) { n += 16; x >>= 16; }` …) 5단계. M0 의 곱셈기가 1사이클(MULS 옵션) 인지 32사이클인지에 따라 선택이 달라진다.

</details>

<details>
<summary>Q4. 비트맵 할당기와 free list(DS-B07) 는 어떻게 다르고 언제 무엇을 쓰나요?</summary>

free list: alloc/free O(1), 노드 안의 링크를 쓰므로 추가 메모리 0 (빈 노드 안에 저장), 하지만 **연속 k개**를 찾을 수 없고 상태를 스캔·저장하기 어렵다. 비트맵: 항목당 1비트 별도 메모리, alloc 은 O(n/32) 스캔, 대신 연속 구간 탐색·사용량 집계(popcount)·**영속 저장**(플래시/디스크에 그대로 기록)이 쉽다. 그래서 파일시스템·FTL 의 블록 상태는 비트맵, 고정 크기 객체 풀은 free list 를 쓰고, 둘을 섞기도 한다 (free list + double-free 검출용 비트맵 — DS-B07 추가 연습 2).

</details>

<details>
<summary>Q5. ISR 과 태스크가 같은 비트맵을 set/clear 한다면?</summary>

`bm[w] |= m` 은 load-modify-store 라서 사이에 ISR 이 같은 워드의 다른 비트를 바꾸면 그 변경이 사라진다. 해결: ① `__disable_irq` 짧은 임계 구역 ② C11 `atomic_fetch_or` / `atomic_fetch_and` (Cortex-M3+ 에서 `LDREX/STREX` 루프) ③ Cortex-M3/M4 의 **bit-band**: SRAM 비트 하나를 별칭 주소의 워드 하나로 매핑 (`alias = 0x22000000 + (byte_offset × 32) + (bit × 4)`), 거기에 1/0 을 쓰면 하드웨어가 원자적으로 한 비트만 바꾼다 (M7·M33 에는 없음). ④ 하드웨어 레지스터라면 set/clear 전용 레지스터(NVIC `ISER`/`ICER` 처럼 1을 쓴 비트만 동작). find + set 을 한 번에 원자적으로 해야 하는 할당은 ①이나 CAS 루프가 필요하다.

</details>

<details>
<summary>Q6. 연속 k개 빈 블록을 찾는 first fit 은 단편화에 어떤 영향이 있나요? 대안은?</summary>

first fit 은 앞쪽에 작은 구멍을 남기기 쉽다 (추가 연습 4 에서 8블록 요청이 4블록 구멍을 건너뛰는 것). 대안: best fit (가장 딱 맞는 구멍 — 모든 구간 스캔 필요), next fit (마지막 위치부터 — 빠르지만 단편화 분산), **버디 할당기** (크기를 2의 거듭제곱으로 맞추고 차수별 비트맵/리스트, 합치기 쉬움). 펌웨어에서는 크기 등급이 몇 개뿐이면 등급별 고정 풀로 단편화 자체를 없애는 것이 보통 최선이다. SSD FTL 은 블록 단위 할당이라 연속성보다 마모 평준화(erase count)가 우선이다.

</details>

## 9. 추가 연습

### 연습 1 — 범위 set/clear 를 워드 단위로

`bm_range(bm, start, len, val)` — `[start, start+len)` 을 모두 1 또는 0 으로. 비트 하나씩이 아니라 워드마다 마스크 하나로 처리하고, 비트 단위 결과와 랜덤 대조하라.

<details>
<summary>답 보기</summary>

워드마다 이번에 처리할 비트 수 `take = min(32 - start%32, len)`, 마스크 `lomask(take) << (start%32)`. `lomask(32)` 는 `1u << 32` UB 를 피하려고 분기해서 `0xFFFFFFFF`. 예: 꽉 찬 비트맵에서 `[28, 68)` 을 지우면 `bm[0] = 0x0FFFFFFF, bm[1] = 0, bm[2] = 0xFFFFFFF0`. 블록 할당기에서 찾은 구간을 표시하는 데 쓴다 (Linux `bitmap_set`).

</details>

### 연습 2 — 사용 중 블록 수 (popcount)

`bm_weight(bm, n)` — 1비트 개수. 꼬리 비트는 세지 않는다.

<details>
<summary>답 보기</summary>

꽉 찬 워드들은 `__builtin_popcount(bm[w])`, 마지막 워드는 `bm[full] & lomask(n % 32)` 로 꼬리를 지우고 센다. 꼬리까지 1 인 200비트 비트맵에서 224 가 아니라 200 이 나와야 한다. Kernighan 루프(`x &= x - 1`)와 랜덤 대조. Cortex-M 에는 popcount 명령이 없어 SWAR 로 컴파일된다.

</details>

### 연습 3 — 가장 높은 1비트 (fls) 와 RTOS 우선순위

`bm_last_set(bm, n)` — 가장 높은 사용 비트. 뒤 워드부터 보며 `31 - __builtin_clz(x)`.

<details>
<summary>답 보기</summary>

마지막 워드는 꼬리를 먼저 지운다 (`clz(0)` 도 UB). FreeRTOS 의 `configUSE_PORT_OPTIMISED_TASK_SELECTION` 이 바로 이것이다: 준비된 우선순위마다 비트 하나, `uxTopReadyPriority = 31 - __clz(ready)` 로 최고 우선순위를 1~2 명령에 찾는다 — 우선순위 32개 제한이 생기는 이유. 테스트: `ready = (1<<3)|(1<<17)` → 17.

</details>

### 연습 4 — 블록 할당기와 단편화

블록 32개짜리 `blk_alloc(bm, n, k)` (Part C 의 점프 탐색 + `bm_range` 로 표시) 와 `blk_free`. 4, 4, 4 할당 → 가운데 해제 → 8 요청 → 3 요청 → 13 요청 → 12 요청의 결과를 예측해보라.

<details>
<summary>답 보기</summary>

0, 4, 8 → 가운데 `[4,8)` 해제 → 8 은 구멍에 안 맞아 **12** → 3 은 구멍에 맞아 **4** → 남은 빈칸은 7 한 칸과 `[20,32)` 12칸이라 13 은 **실패(32)** → 12 는 **20**. 빈 블록은 총 13개인데 13 요청이 실패하는 것이 외부 단편화다.

</details>

### 연습 5 — 비용 측정

4096비트가 꽉 차 있고 비트 4000 하나만 빈 비트맵에서, 루프 버전의 비트 검사 횟수와 ctz 버전의 워드 읽기 횟수를 세어 출력하라.

<details>
<summary>답 보기</summary>

실행 결과 `loop 4001 bit tests, ctz 126 word reads`. 4000 / 32 = 125 → 워드 0~125 를 읽는다. 비트 검사 하나도 나눗셈·시프트·분기라 실제 차이는 횟수 비율(약 32배)보다 크게 나는 경우가 많다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 15/15 passed
```

## 10. 핵심 정리

- 비트 `i` → 워드 `i / 32`, 비트 `i % 32`, 마스크 `1u << (i % 32)`. 이 레슨은 1 = 사용, 0 = 빈칸, 못 찾으면 `n`.
- 0 찾기 = `~word` 에서 1 찾기. 꽉 찬 워드(`~w == 0`)는 비교 하나로 32비트 건너뛰기.
- `__builtin_ctz(0)` / `__builtin_clz(0)` 은 **UB** — 항상 0 을 먼저 거른다.
- 첫 워드는 `from` 아래를 마스크, 결과가 `n` 이상이면 `n` (꼬리 비트 무시).
- 연속 k칸 = 빈 구간 `[fnz(s), fns(s))` 단위로 점프, 길이 `>= k` 면 시작점 반환.
- 단순 루프 버전을 정답지로 두고 랜덤(밀도·n·from·k·꼬리 쓰레기) 대조 테스트.
- Cortex-M3+ 는 `RBIT`+`CLZ` 로 ctz 2명령, M0 는 de Bruijn 표. ISR 공유 시 RMW 경쟁 → 임계 구역 / atomic / bit-band.

## 참고자료

- [GCC — Other Built-in Functions (`__builtin_ctz`, `__builtin_clz`, `__builtin_popcount`)](https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html)
- [Linux kernel include/linux/bitmap.h, lib/find_bit.c](https://github.com/torvalds/linux/blob/master/lib/find_bit.c)
- [Bit Twiddling Hacks — Count the consecutive zero bits (trailing) with multiply and lookup](https://graphics.stanford.edu/~seander/bithacks.html#ZerosOnRightMultLookup)
- [ARM Cortex-M4 Technical Reference Manual (DDI 0439) — Bit-banding 절](https://developer.arm.com/documentation/ddi0439/b/)
- [FreeRTOS — configUSE_PORT_OPTIMISED_TASK_SELECTION](https://www.freertos.org/a00110.html)
- Masmano et al., *TLSF: a New Dynamic Memory Allocator for Real-Time Systems* (ECRTS 2004) — 2단계 비트맵 + ffs/fls
- Henry S. Warren, *Hacker's Delight* 2nd ed., 5장 — 비트 세기, 앞/뒤 0 개수
