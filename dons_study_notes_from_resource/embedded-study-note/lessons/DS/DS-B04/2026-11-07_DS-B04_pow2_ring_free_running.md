---
id: DS-B04
time: 45분
updated: 2026-11-07
---
# DS-B04 2의 거듭제곱 링 버퍼: 마스크 인덱스와 자유 증가 카운터

## 학습 목표

- 크기가 2^n 일 때 `i % N` 을 `i & (N-1)` 로 바꿀 수 있는 이유와 조건을 설명한다.
- head/tail 을 0..N-1 로 자르지 않고 `uint32_t` 로 **계속 증가(free-running)** 시키는 링 버퍼를 구현하고, `len = head - tail` 한 줄로 길이를 구한다.
- 카운터가 `UINT32_MAX` 를 넘어 0 으로 돌아가도 길이와 칸 번호가 맞는다는 것을 **모듈러 산술로 증명**하고, N 이 2의 거듭제곱이 아니면 깨지는 반례를 든다.
- 카운터를 `UINT32_MAX` 근처에서 시작시키는 테스트 기법으로 2^32 번 돌리지 않고 경계를 검증한다.
- 배열 끝에서 둘로 나뉘는 **일괄 쓰기/읽기**(memcpy 최대 2번)를 구현한다 — DMA 친화 API.

## 1. 문제

Linux 커널의 `kfifo`, Zephyr 의 `ring_buf`, 대부분의 고성능 UART/DMA 드라이버는 DS-B03 의 세 방식 대신 **크기를 2의 거듭제곱으로 강제하고 카운터를 그냥 계속 더하는** 방식을 쓴다. 면접에서 "왜 이게 넘쳐도 안전한가요?" 를 증명까지 할 수 있으면 확실히 차별화된다.

DS-B03 의 미러 비트를 떠올려 보자. 미러 비트는 "몇 바퀴째인가" 의 홀짝 1비트였다. 그 비트를 1개가 아니라 **32비트 전부**로 키우면 head 는 그냥 "지금까지 넣은 총 바이트 수", tail 은 "지금까지 뺀 총 바이트 수" 가 된다.

- 길이: `len = head - tail` (분기 없음)
- 칸 번호: `head & (N - 1)` (나눗셈 없음)
- empty: `head == tail`, full: `len == N` — 모호함 없음, N 칸 모두 사용
- head 는 생산자만, tail 은 소비자만 쓴다 — 공유 쓰기 변수 없음

그런데 head 는 언젠가 `0xFFFFFFFF` 다음에 `0` 이 된다. 115200 baud UART 라면 약 4.3일, 100 MB/s DMA 라면 **43초**다. 그 순간에도 맞을까?

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">N = 8, 카운터가 2^32 를 넘는 순간 (tail = 0xFFFFFFFC, head = 0x00000004)</text>
  <text x="10" y="64" fill="currentColor">카운터</text>
  <text x="10" y="106" fill="currentColor">&amp; 7</text>
  <g stroke="currentColor" fill="none">
    <rect x="70" y="44" width="64" height="30"/><rect x="646" y="44" width="64" height="30"/>
  </g>
  <g class="f-acc" stroke="currentColor">
    <rect x="134" y="44" width="64" height="30"/><rect x="198" y="44" width="64" height="30"/><rect x="262" y="44" width="64" height="30"/><rect x="326" y="44" width="64" height="30"/>
    <rect x="390" y="44" width="64" height="30"/><rect x="454" y="44" width="64" height="30"/><rect x="518" y="44" width="64" height="30"/><rect x="582" y="44" width="64" height="30"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="102" y="64">FFFFFFFB</text><text x="166" y="64">FFFFFFFC</text><text x="230" y="64">FFFFFFFD</text><text x="294" y="64">FFFFFFFE</text>
    <text x="358" y="64">FFFFFFFF</text><text x="422" y="64">00000000</text><text x="486" y="64">00000001</text><text x="550" y="64">00000002</text>
    <text x="614" y="64">00000003</text><text x="678" y="64">00000004</text>
    <text x="102" y="106">3</text><text x="166" y="106">4</text><text x="230" y="106">5</text><text x="294" y="106">6</text><text x="358" y="106">7</text>
    <text x="422" y="106">0</text><text x="486" y="106">1</text><text x="550" y="106">2</text><text x="614" y="106">3</text><text x="678" y="106">4</text>
    <text x="166" y="132">↑ tail</text><text x="678" y="132">↑ head</text>
  </g>
  <line x1="390" y1="36" x2="390" y2="116" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="396" y="34" fill="currentColor" font-size="12">2^32 경계</text>
  <g fill="currentColor">
    <text x="10" y="168">head(4) &lt; tail(0xFFFFFFFC) 이지만 uint32_t 뺄셈 4 - 0xFFFFFFFC = 8 = 정확한 길이.</text>
    <text x="10" y="194">칸 번호 &amp; 7 도 7 → 0 으로 끊김 없이 이어진다 (2^32 가 8 의 배수이기 때문).</text>
    <text x="10" y="220">N = 5 였다면? 0xFFFFFFFF % 5 = 0, 다음 카운터 0 % 5 = 0 → 같은 칸을 두 번 쓴다.</text>
    <text x="10" y="244">이 레슨의 테스트는 카운터를 이 근처에서 시작해 경계를 직접 지나가게 한다.</text>
  </g>
</svg>
```

### Part A — 마스크 인덱스

- `is_pow2(x)` — 2의 거듭제곱인가 (0 은 아님). `RB_SIZE` 는 `_Static_assert` 로 컴파일 타임에 검사한다.
- `prb_idx(counter)` — 카운터 → 칸 번호. `%` 대신 `& RB_MASK`.
- 테스트: `UINT32_MAX - 20` 부터 40개 카운터에 대해 `prb_idx(c) == c % 8` 이고, `prb_idx(c + 1)` 이 언제나 "다음 칸" 인지 (경계에서 끊기지 않는지).

### Part B — 자유 증가 카운터

`prb_len / prb_empty / prb_full / prb_push / prb_pop` 을 구현한다.

- head, tail 은 0..N-1 로 **자르지 않는다**. push 는 `buf[prb_idx(head)] = b; head++;` 끝.
- `prb_init(r, start)` 로 카운터를 아무 값에서 시작할 수 있다. 테스트는 `start = UINT32_MAX - 3` (= `0xFFFFFFFC`) 에서 8개를 넣어 head 만 먼저 2^32 를 넘긴 상태(head = 4 < tail)에서 len/full 이 맞는지, 이어서 tail 도 넘어갈 때 맞는지 확인한다.

### Part C — 일괄 쓰기/읽기 + 경계 랜덤 테스트

- `prb_write(r, src, n)` — 빈 칸만큼만 쓰고 실제로 쓴 수를 반환. 배열 끝까지 한 번, 앞에서부터 한 번, **memcpy 최대 2번**.
- `prb_read(r, dst, n)` — 그 거울상.
- 하네스의 `fuzz_wrap()` 이 시작점 4곳(`0`, `UINT32_MAX-3`, `UINT32_MAX-100`, `UINT32_MAX-5000`)에서 30,000개의 무작위 push/pop/write/read 를 참조 모델과 대조하고, 매 연산 뒤 **`head == start + 넣은 총량`, `tail == start + 뺀 총량` (mod 2^32)** 까지 확인한다. 경계 근처 시작점에서는 head 와 tail 이 각각 정확히 한 번 2^32 를 넘어야 통과다.

## 2. Problem (English)

> **Power-of-two ring buffer with free-running indices.**
>
> Implement a byte FIFO backed by `uint8_t buf[N]` where `N = 2^k` (use `N = 8`). Keep `uint32_t head` (total bytes ever written) and `uint32_t tail` (total bytes ever read). Never reduce them modulo `N`; let them overflow naturally.
>
> 1. `bool is_pow2(uint32_t x)` and `uint32_t prb_idx(uint32_t counter)` that maps a counter to a slot without division.
> 2. `len`, `empty`, `full`, `push`, `pop`. `len` must be a single subtraction. `push` returns `false` when full, `pop` returns `false` when empty.
> 3. `uint32_t prb_write(prb_t *r, const uint8_t *src, uint32_t n)` and `prb_read(...)` that transfer as many bytes as possible using at most two `memcpy` calls each, and return the count transferred.
>
> Prove that `len = head - tail` is correct even after `head` has wrapped past `UINT32_MAX` but `tail` has not. Explain why `N` must be a power of two, and what the largest allowed `N` is for 32-bit counters. Show tests that exercise the wrap without performing 2^32 operations.
>
> Example: starting from `head = tail = 0xFFFFFFFC`, after 8 pushes `head == 4`, `tail == 0xFFFFFFFC`, and `len == 8`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "4/29 passed" — TODO 를 채워 29/29 를 만든다
```

참조 모델·`fuzz_wrap()`·통계 출력은 하네스에 이미 있다 (랜덤 대조 테스트를 직접 짜는 연습은 DS-B03 Part C). 이 레슨의 TODO 는 링 버퍼 함수들뿐이다.

## 4. 힌트

<details>
<summary>힌트 1 — 2의 거듭제곱 판별과 마스크</summary>

2^k 는 1비트가 하나뿐 → `x & (x - 1)` 이 0 (가장 낮은 1 비트를 지우면 0). 0 은 따로 거른다. `RB_MASK = RB_SIZE - 1 = 0b0111` 이므로 `c & RB_MASK` 는 하위 3비트 = `c % 8`.

</details>

<details>
<summary>힌트 2 — len 에 if 를 쓰고 싶어지면</summary>

`head >= tail ? ... : ...` 같은 분기는 필요 없다. unsigned 뺄셈은 C 표준이 **mod 2^32 로 정의**한다 (오버플로가 아니다). 그냥 `head - tail`.

</details>

<details>
<summary>힌트 3 — full 은 head == tail 이 아니다</summary>

자유 증가 카운터에서 `head == tail` 은 오직 empty. full 은 `len == RB_SIZE`. (B03 의 모호성이 여기선 아예 생기지 않는다.)

</details>

<details>
<summary>힌트 4 — 일괄 쓰기의 두 조각</summary>

`off = prb_idx(head)`, 배열 끝까지 연속된 칸 = `RB_SIZE - off`. 첫 조각 `first = min(n, RB_SIZE - off)`, 둘째 조각 `n - first` 은 `buf[0]` 부터. 길이 0 인 memcpy 는 문제없다. 마지막에 `head += n` 한 번.

</details>

## 5. 풀이

### Part A — `%` 대신 `&`

| counter | `c % 8` | `c & 7` | 이진 하위 4비트 |
|---|---|---|---|
| 9 | 1 | 1 | `1001` |
| 15 | 7 | 7 | `1111` |
| 0xFFFFFFFF | 7 | 7 | `1111` |
| 0x00000000 | 0 | 0 | `0000` |

N = 2^k 로 나눈 나머지는 하위 k 비트 그 자체다 (상위 비트들은 모두 2^k 의 배수). 그래서 마스크 하나로 끝난다.

- 비용: Cortex-M0/M0+ 에는 나눗셈 명령이 없어 `%` 가 라이브러리 호출(수십 사이클), M3/M4 의 `UDIV` 도 2~12 사이클. `AND` 는 1 사이클. ISR 에서는 차이가 크다.
- 컴파일러는 상수 2^k 로 나누는 **unsigned** `%` 를 알아서 `&` 로 바꾼다. 하지만 크기가 런타임 변수이거나 인덱스가 **signed** 이면 (음수 나머지 규칙 때문에) 못 바꾼다. 그래서 명시적으로 마스크를 쓰고, 크기는 `_Static_assert` 로 보장한다.
- `contiguous` 테스트: `UINT32_MAX` 의 칸은 7, 다음 카운터 0 의 칸은 0 — 7 다음 0 은 정상적인 "다음 칸" 이다.

### Part B — 왜 넘쳐도 안전한가 (증명)

```c
static inline uint32_t prb_len(const prb_t *r) { return r->head - r->tail; }
static bool prb_push(prb_t *r, uint8_t b) {
    if (prb_full(r)) return false;
    r->buf[prb_idx(r->head)] = b;
    r->head++;                       /* UINT32_MAX 다음은 0 — unsigned 라 정의된 동작 */
    return true;
}
```

수학적으로 무한히 커지는 진짜 누적 개수를 **H** (넣은 총량), **T** (뺀 총량) 라 하자. 링 버퍼가 지키는 불변식은 `0 ≤ H − T ≤ N` 이다. 메모리에 있는 값은 `head = H mod 2^32`, `tail = T mod 2^32`.

1. **길이**: C 의 unsigned 뺄셈은 `head − tail ≡ H − T (mod 2^32)` 를 0..2^32−1 범위로 돌려준다. 그런데 `H − T` 는 이미 `0..N` 안에 있고 `N < 2^32` 이므로, mod 를 해도 **값이 그대로** 다. → `head − tail == H − T`. head 만 넘어갔든, 둘 다 넘어갔든 상관없다.
2. **칸 번호**: `head & (N−1) = head mod N = (H mod 2^32) mod N`. N 이 2^32 의 약수(= 2의 거듭제곱)이면 이것은 `H mod N` 과 같다. → "H 번째 바이트는 H mod N 칸" 이라는 규칙이 카운터가 넘어가도 유지된다.
3. **N 이 2의 거듭제곱이 아니면**: 2 가 깨진다. H = 2^32 일 때 head = 0 → 칸 0 이지만, 그 직전 H = 2^32 − 1 도 N = 5 이면 `4294967295 % 5 = 0` → **칸 0**. 연속한 두 바이트가 같은 칸에 쓰여 데이터가 덮인다 (추가 연습 1). 1번(길이)은 여전히 맞지만 칸 배치가 망가진다.
4. **최대 크기**: len 이 0..N 의 N+1 가지 값을 서로 다르게 표현해야 하므로 `N ≤ 2^32 − 1`, 2의 거듭제곱 중에서는 **N ≤ 2^31**. 일반적으로 카운터가 W 비트면 N ≤ 2^(W−1) (추가 연습 3: uint8_t 카운터로 N = 256 이면 가득 찼는데 len = 0).

DS-B03 의 미러 비트는 이 설계에서 카운터 폭을 `k + 1` 비트로 줄인 특수한 경우다 (N = 2^k 칸 + 바퀴 홀짝 1비트). 비트 수를 늘려도 증명은 같으니, CPU 레지스터 폭(32비트) 을 그냥 다 쓰는 것이 가장 간단하다.

테스트가 실제로 밟는 상태 (`start = 0xFFFFFFFC`):

| 단계 | head | tail | `head - tail` | 비고 |
|---|---|---|---|---|
| 초기 | 0xFFFFFFFC | 0xFFFFFFFC | 0 | empty, 칸 4 부터 |
| push 0..7 | 0x00000004 | 0xFFFFFFFC | 8 | **head 만 넘어감**, head &lt; tail 인데 full |
| push 99 | — | — | 8 | full → false |
| buf 내용 | | | | 칸 4,5,6,7 = 0,1,2,3 / 칸 0,1,2,3 = 4,5,6,7 |
| pop ×3 | 0x00000004 | 0xFFFFFFFF | 5 | 꺼낸 값 0,1,2 |
| pop ×1 | 0x00000004 | 0x00000000 | 4 | **tail 도 넘어감**, 꺼낸 값 3 |
| pop 나머지 | 0x00000004 | 0x00000004 | 0 | 마지막 값 7, empty |

이 테스트가 핵심 기법이다: **2^32 번 연산할 필요 없이 카운터를 경계 바로 앞에서 시작**하면 된다. 실제 펌웨어에서도 `head = tail = 0xFFFFFF00` 으로 초기화하는 디버그 빌드를 두면 몇 초 만에 랩어라운드 버그가 드러난다 (Linux 커널이 jiffies 를 부팅 5분 뒤에 넘치도록 초기화하는 것과 같은 아이디어).

### Part C — 일괄 쓰기: 끝에서 두 조각

```svg
<svg viewBox="0 0 720 220" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">prb_write(r, src, 5): head = 0xFFFFFFFE → off = 6</text>
  <g stroke="currentColor" fill="none">
    <rect x="200" y="50" width="56" height="34"/><rect x="256" y="50" width="56" height="34"/>
    <rect x="312" y="50" width="56" height="34"/>
  </g>
  <g class="f-ok" stroke="currentColor">
    <rect x="32" y="50" width="56" height="34"/><rect x="88" y="50" width="56" height="34"/><rect x="144" y="50" width="56" height="34"/>
  </g>
  <g class="f-acc" stroke="currentColor">
    <rect x="368" y="50" width="56" height="34"/><rect x="424" y="50" width="56" height="34"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="60" y="72">12</text><text x="116" y="72">13</text><text x="172" y="72">14</text>
    <text x="396" y="72">10</text><text x="452" y="72">11</text>
    <text x="60" y="102" font-size="12">0</text><text x="116" y="102" font-size="12">1</text><text x="172" y="102" font-size="12">2</text>
    <text x="228" y="102" font-size="12">3</text><text x="284" y="102" font-size="12">4</text><text x="340" y="102" font-size="12">5</text>
    <text x="396" y="102" font-size="12">6</text><text x="452" y="102" font-size="12">7</text>
  </g>
  <g fill="currentColor">
    <text x="500" y="64">① first = 8 - 6 = 2</text>
    <text x="500" y="84">   memcpy(&amp;buf[6], src, 2)</text>
    <text x="500" y="112">② n - first = 3</text>
    <text x="500" y="132">   memcpy(&amp;buf[0], src+2, 3)</text>
    <text x="10" y="150">③ head += 5 → 0x00000003. 칸 번호는 마스크로 다시 구하니 head 를 자르지 않는다.</text>
    <text x="10" y="176">다음 prb_write(…, 5) 는 빈 칸 8 - 5 = 3 개만 쓰고 3 을 반환한다 (나머지는 호출자가 다시 시도).</text>
    <text x="10" y="202">DMA 로 바꾸면 memcpy 두 번 = DMA 전송 두 번. 조각 길이는 "연속 공간" 계산 하나로 나온다.</text>
  </g>
</svg>
```

`prb_read` 는 정확히 거울상이다 (`off = prb_idx(tail)`, `n = min(n, len)`). 공간/데이터 계산을 **함수 시작에서 한 번** 하고 카운터를 **끝에서 한 번** 갱신하는 것이 중요하다 — SPSC 에서 상대편은 카운터가 바뀌는 순간에만 새 상태를 보므로, 반쯤 쓴 데이터가 노출되지 않는다. (멀티코어에서는 그 갱신 앞에 release 배리어가 필요하다. kfifo 는 `smp_wmb()` 를 쓴다.)

경계 랜덤 테스트 실제 실행 결과 (30,000 연산, 시드 `0xB04B + k`, 연산 하나에 최대 12바이트):

| start | bytes_in | bytes_out | head_wraps | tail_wraps | final_head |
|---|---|---|---|---|---|
| 0x00000000 | 23893 | 23893 | 0 | 0 | 0x00005D55 |
| 0xFFFFFFFC | 24332 | 24332 | 1 | 1 | 0x00005F08 |
| 0xFFFFFF9B | 23583 | 23582 | 1 | 1 | 0x00005BBA |
| 0xFFFFEC77 | 23841 | 23841 | 1 | 1 | 0x00004998 |

- 경계 근처 세 시작점 모두 head 와 tail 이 **정확히 한 번씩** 2^32 를 넘었고, 그 전후 모든 연산에서 모델과 일치했다.
- `0xFFFFFFFC` 는 첫 몇 연산 만에, `0xFFFFEC77` (= UINT32_MAX − 5000) 은 약 5000 바이트 뒤에 넘어간다 — head 만 넘고 tail 은 아직인 "head &lt; tail" 구간이 시작점마다 다른 버퍼 상태에서 생긴다.
- `final_head = start + bytes_in` (mod 2^32): 예) `0xFFFFFFFC + 24332 = 0x5F08`. 카운터가 "총량" 이라는 정의가 그대로 검증된다.
- 셋째 줄 bytes_in − bytes_out = 1 은 끝날 때 버퍼에 1바이트가 남아 있었다는 뜻이다.

### DS-B03 과 비교

| | B03 one-slot-empty | B03 mirror bit | **B04 자유 증가** |
|---|---|---|---|
| 크기 제약 | 아무 N | 아무 N | **2^k** |
| 쓸 수 있는 칸 | N-1 | N | N |
| 인덱스 전진 | 비교 후 0 | 비교 후 0 + 비트 반전 | `++` 만 |
| len | 분기 2개 | 분기 2개 | `head - tail` |
| 칸 번호 | 인덱스 그대로 | 인덱스 그대로 | `& (N-1)` |
| 공유 쓰기 | 없음 | 없음 | 없음 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 29/29 passed
cc -std=c11 -Wall -Wextra -g -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan
```

UBSan 은 unsigned 래핑을 문제 삼지 않는다 (정의된 동작이므로). `-fsanitize=unsigned-integer-overflow` 를 따로 켜면 의도된 래핑도 경고하니, 이 코드에는 쓰지 않는다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `head = (head + 1) & MASK` (저장할 때 마스크) | 카운터가 0..N-1 로 잘려 B03 의 head==tail 모호성이 되돌아옴 | 카운터는 `head++`, 마스크는 **인덱싱할 때만** |
| `if (head > tail)` 로 데이터 유무 판단 | head 만 넘어가면 head &lt; tail 인데 데이터가 있다 | `head != tail` 또는 `head - tail` |
| `full = (head == tail)` | 자유 증가에선 head==tail 은 empty 뿐 | `head - tail == N` |
| `int head, tail;` | signed 오버플로는 **UB** — 컴파일러가 "넘치지 않는다" 고 가정해 최적화 | 반드시 unsigned (`uint32_t`) |
| `uint16_t` 카운터에 `len = head - tail` (int 반환) | 둘 다 int 로 승격돼 음수 (`4 - 0xFFFC = -65528`) | `(uint16_t)(head - tail)` — C-B02 의 정수 승격 |
| `#define RB_SIZE 100` 에 마스크 | `& 99` 는 나머지가 아니다 (칸 일부를 영원히 안 씀/겹침) | `_Static_assert` 로 2의 거듭제곱 강제 |
| `% N` 은 넘침과 무관하다고 생각해 N=5 로 자유 증가 | 2^32 가 5 의 배수가 아니라 경계에서 칸 번호가 튄다 | N 을 2^k 로 하거나, 인덱스를 0..N-1 로 자르는 B03 방식 |
| 일괄 쓰기에서 조각마다 head 갱신 | 소비자가 첫 조각 뒤의 중간 상태를 보고, 두 번째 조각이 아직 안 쓴 칸을 읽을 수 있다 | 계산 한 번, 복사 두 번, `head += n` 한 번 |

## 8. 면접 Q&A

<details>
<summary>Q1. head 와 tail 을 계속 증가시키면 언젠가 넘치는데, 왜 len = head - tail 이 맞나요?</summary>

unsigned 산술은 mod 2^32 로 정의된다. 실제 누적량 H, T 에 대해 `head - tail ≡ H - T (mod 2^32)` 이고, 링 버퍼 불변식 때문에 `0 ≤ H - T ≤ N < 2^32` 이라 mod 결과가 진짜 차이와 같다. head 만 넘어간 상태(head &lt; tail)에서도 마찬가지. 꼬리질문 "signed 로 하면?" → signed 오버플로는 UB 라 컴파일러가 이상한 최적화를 할 수 있다. 반드시 unsigned.

</details>

<details>
<summary>Q2. 왜 크기가 2의 거듭제곱이어야 하나요? 길이 계산만 보면 아무 N 이나 되지 않나요?</summary>

길이(`head - tail`)는 아무 N 에서도 맞다. 깨지는 건 **칸 번호**다. `counter mod N` 이 카운터가 2^32 에서 0 으로 돌아갈 때도 연속이려면 2^32 가 N 의 배수여야 하고, 그건 N 이 2의 거듭제곱일 때뿐이다. N = 5 면 `0xFFFFFFFF % 5 == 0` 이고 다음 `0 % 5 == 0` → 같은 칸을 두 번 쓴다. 덤으로 `%` 대신 `&` 를 써서 나눗셈도 없앤다.

</details>

<details>
<summary>Q3. 32비트 카운터일 때 최대 버퍼 크기는? 16비트, 8비트 카운터라면?</summary>

len 은 0..N 의 N+1 가지를 구별해야 하므로 N &lt; 2^W, 2의 거듭제곱 중 최대는 2^(W-1): 32비트 → 2^31 (2 GiB), 16비트 → 32768, 8비트 → 128. 8비트 카운터에 256 칸이면 가득 찼을 때 head == tail 이 되어 len 0 — B03 의 모호성으로 되돌아간다. 작은 MCU 에서 RAM 을 아끼려고 `uint8_t` 카운터를 쓸 때 실제로 나는 버그.

</details>

<details>
<summary>Q4. 2^32 번 돌리지 않고 랩어라운드를 어떻게 테스트하나요?</summary>

카운터를 `UINT32_MAX - k` 에서 시작시킨다 (`prb_init(r, start)` 같은 테스트 훅). 그러면 k 바이트만에 경계를 지나간다. 여러 시작점(경계 바로 앞, 수백·수천 앞)에서 랜덤 연산을 돌려 "head 만 넘은 상태" 가 다양한 버퍼 상태에서 생기게 하고, 통계로 실제로 넘었는지(`head_wraps == 1`) 확인한다. 같은 원리로 Linux 는 `jiffies` 를 부팅 5분 뒤 넘치도록 `INITIAL_JIFFIES` 로 초기화해 랩어라운드 버그를 일찍 드러낸다. 타이머 tick 비교도 같은 기법 (C-B02 의 `now - start`).

</details>

<details>
<summary>Q5. ISR(생산자)와 main(소비자) 사이에서 이 구조는 왜 잠금 없이 안전한가요? 멀티코어라면?</summary>

head 는 생산자만, tail 은 소비자만 쓴다 (단일 쓰기 주체). 상대편 카운터는 읽기만 하고, 정렬된 32비트 읽기/쓰기는 Cortex-M 에서 원자적이다. 생산자는 데이터를 쓴 **뒤** head 를 올리고, 소비자는 데이터를 읽은 **뒤** tail 을 올린다. 단일 코어 + 인터럽트면 `volatile` 과 이 순서로 충분. 멀티코어(또는 DMA·캐시)면 CPU/컴파일러가 순서를 바꿀 수 있으므로 head 갱신 전 release, head 읽은 뒤 acquire 배리어가 필요하다 — Linux kfifo 의 `smp_wmb()`, C11 의 `atomic_store_explicit(..., memory_order_release)`. MPMC 로 가면 이 방식만으로는 안 되고 CAS 가 필요하다 (Vyukov 큐).

</details>

<details>
<summary>Q6. 크기를 2의 거듭제곱으로 강제하면 메모리 낭비 아닌가요? 1000 바이트가 필요하면?</summary>

1024 를 쓰면 24 바이트(2.4%) 낭비 — 보통 받아들인다. 1100 이 필요하면 2048 은 46% 낭비라 고민이 된다. 선택지: ① 2의 거듭제곱이 아닌 B03 one-slot-empty (분기 비용 감수) ② 64비트 카운터 + `%` (넘칠 일이 사실상 없어 칸 번호 문제도 사라짐, 대신 나눗셈과 비원자적 64비트 접근) ③ 레코드 크기를 조정. 실시간 ISR 경로면 ①, 대부분은 그냥 2의 거듭제곱.

</details>

## 9. 추가 연습

### 연습 1 — N 이 2의 거듭제곱이 아니면 어디서 깨지나

`bool wrap_is_contiguous(uint32_t n)`: 카운터 `UINT32_MAX` 와 그다음 `0` 의 칸 번호(`% n`)가 연속인지 판별하라. n = 1..64 에서 결과가 `is_pow2(n)` 과 정확히 같음을 확인.

<details>
<summary>답 보기</summary>

`(uint32_t)(c + 1u) % n == (c % n + 1u) % n` (c = UINT32_MAX). 왼쪽은 항상 0, 오른쪽은 `2^32 mod n`. 둘이 같으려면 n | 2^32 → n 은 2의 거듭제곱. n = 5, 3, 6 은 모두 깨진다. "한 번 넘칠 때 한 번 데이터가 덮인다" 는 버그라, 115200 baud 에선 4일에 한 번 — 현장에서만 재현되는 최악의 유형.

</details>

### 연습 2 — uint16_t 카운터와 정수 승격

`uint16_t head, tail` 로 같은 링을 만들고 `start = 0xFFFC` 에서 8개 push. `int len16_wrong() { return head - tail; }` 와 `uint16_t len16_right() { return (uint16_t)(head - tail); }` 의 값을 비교하라.

<details>
<summary>답 보기</summary>

wrong = `4 - 65532 = -65528` (두 uint16_t 가 int 로 승격돼 signed 뺄셈), right = 8. uint32_t 에서는 승격이 없어 문제가 안 보이다가 RAM 아끼려고 16비트로 줄이는 순간 터진다. full 판정도 `len16_right` 로 해야 한다.

</details>

### 연습 3 — 카운터 폭과 최대 크기

uint8_t 카운터로 size 바이트를 채운 뒤 `(uint8_t)(head - tail)` 를 계산하는 `u8_len_after_fill(size)`. size = 128, 256 일 때 값은?

<details>
<summary>답 보기</summary>

128 → 128 (정상), 256 → 0 (가득 찼는데 empty 로 보임). W 비트 카운터의 최대 크기는 2^(W-1). 32비트면 2^31 이니 실무에선 걱정할 일이 없지만, 면접에서 "한계가 있나?" 에 답할 수 있어야 한다.

</details>

### 연습 4 — peek / skip 으로 프레임 파싱

16칸 링 (카운터 시작 `UINT32_MAX - 2`) 에 `00 13 A5 03 'a' 'b' 'c' A5 02 'x'` 가 들어왔다. `prb_peek(r, i, &b)` (tail + i 번째를 꺼내지 않고 보기), `prb_skip(r, n)` 을 만들고, sync `0xA5` + 길이 + payload 프레임을 꺼내는 `take_frame()` 을 작성하라. 두 번째 프레임은 `'y'` 가 올 때까지 -1 이어야 한다.

<details>
<summary>답 보기</summary>

peek: `i < len` 이면 `buf[(tail + i) & MASK]` — `tail + i` 도 그냥 더하고 마스크하면 경계 걱정이 없다. take_frame: ① 맨 앞이 0xA5 가 아니면 skip(1) 반복 ② 길이 바이트를 peek, `len < 2 + L` 이면 **아무것도 소비하지 않고** -1 ③ payload 를 peek 로 복사 후 skip(2 + L). 덜 온 프레임을 소비하지 않는 것이 핵심 (B03 연습 2 의 getline 과 같은 원칙).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 15/15 passed
```

## 10. 핵심 정리

- 크기 N = 2^k, `uint32_t head/tail` 은 **자르지 않고 계속 ++**. 칸 = `c & (N-1)`, 길이 = `head - tail`.
- empty = `head == tail`, full = `head - tail == N`. N 칸 모두 사용, 분기 없음, 나눗셈 없음.
- 증명: unsigned 뺄셈은 mod 2^32, 진짜 길이는 0..N &lt; 2^32 → 그대로. 칸 번호는 N | 2^32 일 때만 연속.
- N 이 2의 거듭제곱이 아니면 경계에서 같은 칸을 두 번 쓴다. W 비트 카운터면 N ≤ 2^(W-1).
- 마스크는 **인덱싱할 때만**, 카운터는 반드시 **unsigned** (signed 넘침 = UB, uint16_t 는 승격 주의).
- 랩어라운드 테스트 = 카운터를 `UINT32_MAX - k` 에서 시작 + 여러 시작점 + 넘었는지 통계로 확인.
- 일괄 전송: 공간 계산 1번 → memcpy(또는 DMA) 최대 2번 → 카운터 갱신 1번. head 는 생산자만, tail 은 소비자만.

## 참고자료

- [Linux kernel include/linux/kfifo.h](https://github.com/torvalds/linux/blob/master/include/linux/kfifo.h) · [lib/kfifo.c](https://github.com/torvalds/linux/blob/master/lib/kfifo.c) — `in`/`out` 자유 증가, `size` 2의 거듭제곱, `smp_wmb()`
- [Linux kernel Documentation — Circular Buffers](https://docs.kernel.org/core-api/circular-buffers.html) — 생산자/소비자 배리어 규칙
- [Zephyr RTOS — Ring Buffers](https://docs.zephyrproject.org/latest/kernel/data_structures/ring_buffers.html)
- C11 표준 (N1570) 6.2.5p9 — "A computation involving unsigned operands can never overflow" (mod 2^N)
- [SEI CERT C — INT30-C. Ensure that unsigned integer operations do not wrap](https://wiki.sei.cmu.edu/confluence/display/c/INT30-C.+Ensure+that+unsigned+integer+operations+do+not+wrap) — 의도된 래핑은 예외로 명시
- DS-B03 링 버퍼 기초 (full/empty 판별 3가지 방식), C-B02 정수 승격과 랩어라운드 타이머
