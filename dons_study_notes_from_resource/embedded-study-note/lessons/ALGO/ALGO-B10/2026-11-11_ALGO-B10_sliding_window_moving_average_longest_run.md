---
id: ALGO-B10
time: 60분
updated: 2026-11-11
---
# ALGO-B10 슬라이딩 윈도우: 이동 평균과 최장 구간

## 학습 목표

- **고정 윈도우(fixed window)** 이동 평균을 링 버퍼 + 정수 누적합(running sum)으로 샘플당 O(1)에 유지하고, 누적합 타입이 넘치지 않는 범위를 **증명**한다 (uint16 샘플 + uint32 합 → N ≤ 65537, 반올림 항까지 넣으면 N ≤ 65536).
- 웜업(warm-up, 샘플 < N) 동작과 반올림을 **명시적으로 정의**하고, N = 2^k 일 때 나눗셈 대신 시프트를 쓰는 조건(창이 꽉 찼을 때만)을 말한다.
- **가변 윈도우(variable window)** 두 문제 — 합 ≤ S 최장 구간, 중복 없는 최장 바이트 구간 — 를 O(n)에 풀고, "왼쪽 포인터가 뒤로 안 간다"는 불변식이 **비음수 값**에서만 성립함을 반례 `{3, −5, 3}` 으로 보인다.
- ISR 이 샘플을 넣고 태스크가 평균을 읽을 때 **무엇이 원자적이어야 하는지**((sum, count) 쌍)를 짚고, 찢어진 읽기(torn read)를 시퀀스 카운터로 막는다.
- 세 함수 모두 브루트포스와 고정 시드 무작위로 대조하고, 실수 누적합이 실제로 드리프트(drift)하는 것을 이 맥에서 측정한다.

## 1. 문제

센서 값 다듬기(이동 평균), "최근 N개" 통계, UART 스트림의 구간 검사 — 펌웨어 코딩 면접에서 슬라이딩 윈도우는 **O(N)짜리 루프를 ISR 에 넣지 않는 법**을 묻는 문제다.

슬라이딩 윈도우는 배열(또는 스트림) 위의 구간 `[l, r]` 을 왼쪽·오른쪽 끝만 한 칸씩 움직이며, 구간의 요약값(합, 개수 표)을 **들어온 것 더하고 나간 것 빼기**로 O(1)에 갱신하는 기법이다.

- **고정 윈도우**: 길이가 N 으로 고정. 매 샘플마다 새 것 하나 들어오고 가장 오래된 것 하나 나간다 → 이동 평균.
- **가변 윈도우**: 조건(합 ≤ S, 중복 없음)을 지키는 한 오른쪽을 넓히고, 깨지면 왼쪽을 줄인다 → 최장 구간.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">N=4 이동 평균: 링 버퍼 + 누적합 (샘플 10, 20, 31, 40, 50 을 차례로 push)</text>
  <text x="20" y="44" fill="currentColor">입력 스트림 (시간 →)</text>
  <rect x="20" y="52" width="50" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="76" y="52" width="50" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="132" y="52" width="50" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="188" y="52" width="50" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="244" y="52" width="50" height="28" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="45" y="71">10</text><text x="101" y="71">20</text><text x="157" y="71">31</text><text x="213" y="71">40</text><text x="269" y="71">50</text>
  </g>
  <path d="M76 90 L76 96 L294 96 L294 90" stroke="currentColor" fill="none"/>
  <text x="185" y="114" fill="currentColor" text-anchor="middle">창 = 최근 4개 (10 은 밀려남)</text>
  <text x="20" y="144" fill="currentColor">링 버퍼 buf[4] (push(50) 직후)</text>
  <rect x="20" y="152" width="50" height="28" class="f-ok" stroke="currentColor"/>
  <rect x="76" y="152" width="50" height="28" class="f-mut" stroke="currentColor"/>
  <rect x="132" y="152" width="50" height="28" class="f-mut" stroke="currentColor"/>
  <rect x="188" y="152" width="50" height="28" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="45" y="171">50</text><text x="101" y="171">20</text><text x="157" y="171">31</text><text x="213" y="171">40</text>
    <text x="45" y="196" font-size="12">[0]</text><text x="101" y="196" font-size="12">[1]</text><text x="157" y="196" font-size="12">[2]</text><text x="213" y="196" font-size="12">[3]</text>
  </g>
  <path d="M101 228 L101 204" stroke="currentColor" fill="none"/>
  <path d="M96 210 L101 202 L106 210" stroke="currentColor" fill="none"/>
  <text x="101" y="244" fill="currentColor" text-anchor="middle">head = 가장 오래된 칸</text>
  <text x="330" y="44" fill="currentColor" font-weight="bold">push(50) 이 하는 일 — 루프 없음</text>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="330" y="66">① 꽉 참: sum -= buf[head]   101 - 10 = 91</text>
    <text x="330" y="86">② buf[head] = 50; sum += 50  91 + 50 = 141</text>
    <text x="330" y="106">③ head = head+1 (끝이면 0)    0 → 1</text>
    <text x="330" y="126">avg = (141 + 4/2) / 4 = 35    (35.25)</text>
    <text x="330" y="160">push  count  sum   avg</text>
    <text x="330" y="180">10    1      10    10</text>
    <text x="330" y="198">20    2      30    15</text>
    <text x="330" y="216">31    3      61    20   (20.33)</text>
    <text x="330" y="234">40    4      101   25   (25.25)</text>
    <text x="330" y="252">50    4      141   35   (10 이 빠짐)</text>
  </g>
  <text x="10" y="286" fill="currentColor">웜업(count &lt; 4) 동안은 받은 개수로 나눈다. 시프트(&gt;&gt; 2)는 창이 꽉 찬 뒤에만 나눗셈과 같다.</text>
</svg>
```

### Part A — 고정 윈도우: N 샘플 이동 평균

12비트 ADC(또는 16비트 센서)의 샘플 `uint16_t` 가 계속 들어온다. 창 크기 `n` 은 1..256.

- `ma_init(m, n)` — 범위 밖이면 −1. `n` 이 2의 거듭제곱이면 `m->shift = log2(n)`, 아니면 −1.
- `ma_push(m, x)` — **O(1)**, 루프 없음. 꽉 찼으면 가장 오래된 값을 빼고 새 값을 더한다.
- `ma_avg(m)` — **웜업 정의**: 지금까지 받은 k = min(count, n) 개의 평균. 샘플 0 개면 0. **반올림**: 0.5 올림(round half up) = ⌊sum/k + 1/2⌋.
- `ma_avg_shift(m)` — 전제 n = 2^k 이고 창이 꽉 참. 나눗셈 없이 `ma_avg` 와 **같은 값**.
- 검증: 20만 샘플 스트림 × 창 크기 10종(1, 2, 3, 5, 8, 16, 64, 100, 255, 256)에서 매 샘플마다 창 합을 **처음부터 다시 더한** 브루트포스와 `sum`, 평균, 시프트 평균을 대조한다.

### Part B — 가변 윈도우: 합 ≤ S 최장 구간, 중복 없는 최장 바이트 구간

- `longest_sum_le(a, n, S, &start)` — 값은 **비음수**(`uint16_t` 로 강제: 예를 들어 전류 mA 샘플, 패킷 크기). 합 ≤ S 인 가장 긴 연속 구간의 길이와 시작. 같은 길이가 여럿이면 **가장 왼쪽**. 없으면 0.
- `longest_unique_run(s, n, &start)` — 바이트 스트림(0x00 도 보통 바이트, C 문자열 아님)에서 같은 바이트가 두 번 나오지 않는 가장 긴 구간. **256칸 last-seen 표**.
- 주어진 `window_sum_le_signed()` 는 같은 축소 윈도우를 부호 있는 값에 그대로 쓴 것이다. 반례 `{3, −5, 3}, S = 1` 에서 틀리는 것을 테스트가 확인한다.

### Part C — ISR 이 넣고 태스크가 읽는다

ADC 변환 완료 인터럽트가 `isr_adc_sample(sh, x)` 를 부르고(O(1)), 제어 태스크가 `task_read_avg(sh)` 로 평균을 읽는다. ISR 은 갱신을 마친 뒤 `(pub_sum, pub_cnt)` 를 공개하고 `seq` 를 1 올린다. 태스크는 `seq → sum → count → seq` 순서로 읽고, 두 `seq` 가 다르면 다시 읽는다. 테스트는 `ld32()` 훅으로 **모든 읽기 지점 뒤에** 인터럽트를 끼워 본다. 잠금 없이 두 번 읽는 `task_read_avg_naive()` 는 주어지며, 웜업 중 찢어진 값을 낸다.

## 2. Problem (English)

> **Sliding windows on a sensor stream.**
>
> Implement in C11, no dynamic allocation, no compiler builtins:
>
> 1. A moving average over the last `n` samples (`1 <= n <= 256`) of a `uint16_t` stream.
>    `int ma_init(ma_t *m, uint32_t n)`, `void ma_push(ma_t *m, uint16_t x)` in **O(1)** per sample,
>    `uint16_t ma_avg(const ma_t *m)` returning the mean of the `k = min(count, n)` samples seen so far,
>    rounded half up (`floor(sum/k + 1/2)`), or 0 when no sample has arrived, and
>    `uint16_t ma_avg_shift(const ma_t *m)` that returns the same value without a divide when `n` is a power of two and the window is full.
>    Prove your running-sum type cannot overflow.
> 2. `size_t longest_sum_le(const uint16_t *a, size_t n, uint32_t S, size_t *start)`: the longest contiguous subarray with sum `<= S` (leftmost on ties; 0 if none) in O(n) time, O(1) extra space. Explain why your algorithm needs non-negative values and give a counterexample with negatives.
> 3. `size_t longest_unique_run(const uint8_t *s, size_t n, size_t *start)`: the longest run of bytes with no repeated value (leftmost on ties). The input is binary and may contain `0x00`.
> 4. The ADC ISR calls `isr_adc_sample(x)`; a task calls `task_read_avg()`. Make the task read consistent without disabling interrupts.
>
> Examples: pushes `10, 20, 31, 40, 50` with `n = 4` → averages `10, 15, 20, 25, 35`.
> `longest_sum_le({1,6,3,1,2,4,5}, 7, 10)` → `4`, start `2` (`[3,1,2,4]`).
> `longest_unique_run("pwwkew")` → `3`, start `2` (`"wke"`); `"abba"` → `2`.
>
> Constraints: `n <= 10^6` for parts 2–3. Target time O(n), extra memory O(1) (part 3: a 256-entry table).

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여덟 군데(`ma_init`, `ma_push`, `ma_avg`, `ma_avg_shift`, `longest_sum_le`, `longest_unique_run`, `isr_adc_sample`, `task_read_avg`). 스텁은 "아무것도 안 함 / 0 반환"이라 멈추거나 터지지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:189  ma_init(&m, 256) == 0 && m.shift == 8
Part A: 200000 samples x 10 window sizes: sum mismatches=1999979 avg mismatches=1999979 shift mismatches=0 (checked 0)
Part B sum<=S: 4000 random arrays, mismatches vs brute force = 3875
naive read with ISR between loads: 0 (before=15, after=40)
10/42 passed
```

(중간 FAIL 줄 생략, exit 1. 통과한 10개는 범위 검사 `-1`, 주어진 반례 코드, "안 맞는 게 없음" 처럼 스텁으로도 우연히 맞는 것들.)

## 4. 힌트

<details>
<summary>힌트 1 — 이동 평균은 "합"을 들고 다닌다</summary>

평균을 매번 다시 구하지 말고 **창 안의 합** `sum` 하나를 유지한다. 새 샘플이 오면 `sum += new`, 창이 꽉 찼으면 그 전에 `sum -= 가장 오래된 값`. 가장 오래된 값은 링 버퍼의 `head` 칸에 있다 (꽉 찬 뒤에는 "다음에 쓸 칸 = 가장 오래된 칸").

</details>

<details>
<summary>힌트 2 — 0.5 올림을 정수로</summary>

⌊sum/k + 1/2⌋ = ⌊(sum + ⌊k/2⌋) / k⌋. k 가 2^s 이면 `/ k` 는 `>> s`. 단, **창이 꽉 찼을 때만** k = n 이 2^s 다 — 웜업 중엔 k 가 3, 5, 7… 일 수 있다.

</details>

<details>
<summary>힌트 3 — 합 ≤ S: 언제 왼쪽을 줄이나</summary>

오른쪽 `r` 을 하나씩 넣고 `sum += a[r]`. `sum > S` 인 동안 `sum -= a[l++]`. 빠져나오면 `[l, r]` 은 **반드시 유효**하니 그때 길이를 기록. 값이 비음수라 줄이면 합이 줄어든다 — 이게 정당성의 전부다.

</details>

<details>
<summary>힌트 4 — 중복 없는 구간: l 을 한 번에 점프</summary>

`last[c]` = 바이트 c 가 마지막으로 나온 위치 + 1 (0 = 아직 없음). c 가 지금 창 `[l, r)` 안에 있으면(`last[c] > l`) `l = last[c]`. 창 **밖**의 옛 위치로 `l` 을 되돌리면 안 된다 (`"abba"`). 인덱스는 `uint8_t` — `char` 로 받으면 0x80 이상이 음수 인덱스가 된다.

</details>

<details>
<summary>힌트 5 — ISR/태스크: 하나만 쓰고, 하나는 확인하고</summary>

ISR 은 끝에 `seq++`. 태스크는 `s0 = seq; sum; cnt; if (seq != s0) 다시`. 한 코어에서 ISR 은 태스크를 끊고 끝까지 돈다 — 그러니 두 `seq` 가 같으면 그 사이에 ISR 이 없었다.

</details>

## 5. 풀이

**면접에서 말하는 순서** (이동 평균 + 최장 구간을 한 번에 받았다고 가정)

| 단계 | 말할 것 |
|---|---|
| ① 확인 질문 | 샘플 타입·범위(12비트? 부호?), 창 크기 최대값, 웜업 때 뭘 돌려줄지, 반올림 규칙, 값에 음수가 있나, 입력이 문자열인가 바이트인가, 같은 길이면 어느 것 |
| ② 브루트포스 | 이동 평균: 매 샘플 창 합을 다시 더함 O(N)/샘플. 최장 구간: 모든 (l, r) O(n²) |
| ③ 더 나은 생각 + 불변식 | 합을 들고 다니며 들어온 것 +, 나간 것 −. 가변 창: "`[l, r]` 은 r 로 끝나는 가장 긴 유효 구간" → l 은 뒤로 안 감 |
| ④ 코드 | 링 버퍼 + `count`, 축소 while, 256칸 표 |
| ⑤ 소리 내어 말할 테스트 | 샘플 0개, N=1, 웜업 중간, 정확히 .5, 최대값×N, 빈 배열, 전부 > S, S=0, `"abba"`, 0x00·0xFF 바이트 |
| ⑥ 복잡도 | 이동 평균 O(1)/샘플, 메모리 N×2 B. 최장 구간 O(n) (각 원소 들어오고 나가기 최대 1번), 추가 메모리 O(1) / 256칸. 재귀 없음 → 스택 고정 |

### Part A — 불변식과 O(1) 갱신

t 번째 push 가 끝난 뒤 성립해야 하는 것 (불변식):

- `count = min(t, n)`, 그리고 꽉 차기 전에는 `head = count` (다음 빈칸), 꽉 찬 뒤에는 `head = t mod n` = 가장 오래된 샘플 칸.
- `sum = x[t−count] + … + x[t−1]` (마지막 `count` 개의 **정확한** 합).

push 는 위 그림 ①②③ 세 줄이다. 꽉 찼을 때 `buf[head]` 가 정확히 "창에서 나갈 샘플"인 이유: head 는 n 번 push 마다 한 바퀴 돌므로, 지금 head 칸에 쓰인 값은 정확히 n 번 전에 들어온 샘플이다.

**합 타입의 범위 증명.** 샘플 ≤ 65535 = 2^16 − 1 이고 창에 최대 n 개 → `sum ≤ n·(2^16 − 1)`. `uint32_t` 최대는 2^32 − 1 = (2^16 + 1)(2^16 − 1) 이므로 **n ≤ 65537** 이면 안 넘친다. 이 레슨은 n ≤ 256 → sum ≤ 16,776,960 < 2^24. 반올림 항 ⌊k/2⌋ 까지 더하면 조건은 n·65535 + ⌊n/2⌋ ≤ 2^32 − 1 → n = 65536 일 때 4,294,934,528 로 통과, n = 65537 이면 넘친다 → **n ≤ 65536**. 12비트 ADC(≤ 4095)라면 합만 따질 때 n ≤ ⌊(2^32 − 1)/4095⌋ = 1,048,832, 반올림 항까지 1,048,704.

중간 과정은 걱정할 필요가 없다. `uint32_t` 연산은 mod 2^32 로 정의되어 있으므로(C11 6.2.5p9) `sum - old + new` 의 **최종 참값이 [0, 2^32)** 안에 있으면, 중간에 잠깐 감싸 돌아도(wrap) 결과는 정확하다. 부호 있는 `int32_t` 였다면 중간 오버플로 자체가 UB 다.

**웜업 정책 — 무엇을 돌려줄지 정해야 한다.**

| 정책 | 처음 N−1 샘플 동안 | 장점 | 단점 |
|---|---|---|---|
| ① 받은 개수로 나눔 (이 레슨) | 정확한 부분 평균 | 처음부터 의미 있는 값 | 웜업 중엔 나눗셈이 필요 (k 가 2^s 아님) |
| ② 첫 샘플로 창을 채움 (prefill, 연습 1) | 첫 샘플 쪽으로 쏠린 평균 | 처음부터 끝까지 시프트만 | 첫 샘플이 튀면 N 샘플 동안 끌려감 |
| ③ "준비 안 됨" 표시 | 값 없음 (`ma_ready()` 가 false) | 의미가 가장 분명 | 호출자가 처리해야 함 |
| ④ 0 으로 채운 것처럼 sum/N | 0 에서 서서히 올라옴 | 코드 가장 단순 | 시작 직후 가짜 저전압 경보 같은 버그 |

④가 실무에서 흔한 버그다: 전원 투입 직후 배터리 전압 평균이 0 V 근처로 나와 저전압 셧다운이 걸리는 식. 면접에서 "웜업을 어떻게 정의하겠다"고 먼저 말하면 점수가 된다.

**반올림 증명.** sum = qk + r (0 ≤ r < k) 로 쓰면 ⌊(sum + ⌊k/2⌋)/k⌋ = q + [r ≥ k − ⌊k/2⌋] = q + [r ≥ ⌈k/2⌉], 그리고 ⌊sum/k + 1/2⌋ = q + [r/k ≥ 1/2] = q + [r ≥ k/2] — r 이 정수라 [r ≥ k/2] = [r ≥ ⌈k/2⌉]. 같은 식이다. 테스트의 기준값은 일부러 다른 식 ⌊(2·sum + k)/(2k)⌋ 로 계산해 대조했다. 결과가 65535 를 넘지 않는 것도: (sum + k/2)/k ≤ 65535 + 1/2 이므로 바닥은 ≤ 65535.

**시프트는 언제 되나.** n = 2^s 이고 count = n 이면 `(sum + (n>>1)) >> s`. 이 맥에서 Cortex-M 으로 크로스 컴파일한 실제 출력 (`demo_cm_avg.c`, 명령어만 추림):

| C 식 | Cortex-M4 (`-mcpu=cortex-m4 -O2`) | Cortex-M0 (`-mcpu=cortex-m0 -O2`) |
|---|---|---|
| `(sum + count/2) / count` (웜업) | `add.w r0, r0, r1, lsr #1` · `udiv r0, r0, r1` | `lsrs` · `adds` · `bl __aeabi_uidiv` |
| `(sum + 32) / 64` | `adds r0, #32` · `lsrs r0, r0, #6` | 같음 (시프트) |
| `(sum + 50) / 100` | `movw/movt r1, 0x51EB851F` · `umull` · `lsrs r1, #5` | `movs r1, #100` · `bl __aeabi_uidiv` |
| `sum / 64` (int32_t) | `asrs r1, r0, #31` · `add.w r0, r0, r1, lsr #26` · `asrs r0, r0, #6` | 같은 보정 4줄 |

읽을 것: ① **상수** 2^k 로 나누면 컴파일러가 알아서 시프트로 바꾼다 — 손으로 `>>` 를 쓸 이유는 "n 이 런타임 변수일 때" 다. ② 2^k 가 아닌 상수(100)는 M4 에선 곱셈-상위(`umull`)로 바뀌지만, 32×32→64 곱셈이 없는 M0 에선 라이브러리 나눗셈 호출이다. ③ M0(ARMv6-M)에는 나눗셈 명령 자체가 없다. M3/M4 의 `UDIV` 는 2~12 사이클(Cortex-M4 TRM). ④ 부호 있는 `/ 64` 는 0 쪽 버림(truncation, C11 6.5.5)이라 음수일 때 63 을 더하는 보정이 붙는다. 음수의 `>>` 는 **구현 정의**(C11 6.5.7p5) — 위 `asrs` 처럼 보통 산술 시프트(= floor)라서 `/` 와 음수에서 결과가 다르다.

**브루트포스 대조 결과** (`solution.c` 실제 출력): `Part A: 200000 samples x 10 window sizes: sum mismatches=0 avg mismatches=0 shift mismatches=0 (checked 1199659)`. 20만 샘플 뒤에도 `sum` 이 처음부터 다시 더한 값과 **비트 단위로 같다** — 정수 덧셈·뺄셈은 오차가 없으니 드리프트가 있을 수 없다. 실수는 다르다. `demo_float_drift.c` 는 같은 스트림(12비트, N=64)을 볼트로 바꿔 `float` 누적합 `s += v_new − v_old` 로 유지한다 (실제 출력, 오차 = 평균의 절대 오차):

| 샘플 수 | 정수 합 오차 (V) | float 오차 (V) | double 오차 (V) | float 오차 (LSB) |
|---|---|---|---|---|
| 10^3 | 0 | 1.203e-06 | 6.661e-16 | 0.0015 |
| 10^4 | 0 | 2.302e-07 | 2.220e-15 | 0.0003 |
| 10^5 | 0 | 6.698e-06 | 6.883e-15 | 0.0083 |
| 10^6 | 0 | 2.221e-05 | 1.066e-14 | 0.0276 |
| 10^7 | 0 | 7.797e-05 | 6.484e-14 | 0.0968 |
| 10^8 | 0 | 1.495e-03 | 5.884e-14 | **1.8552** |

(1 LSB = 3.3/4095 V = 8.06e-4 V.) 덧셈·뺄셈마다 반올림 오차가 생기고, 더한 값과 뺀 값의 오차가 상쇄되지 않아 **누적**된다. 단조롭게 늘지는 않지만(10^3 → 10^4 에서 줄어듦) 10^8 샘플(10 kHz 로 약 2.8시간)이면 평균이 ADC 1.9 LSB 만큼 틀린다 — 원래 신호보다 큰 오차다. double 은 같은 현상이 10^-14 수준이라 보이지 않을 뿐이다. 처방: ① 합은 **정수 코드**로 들고, 단위 변환은 평균을 낸 뒤 한 번 ② 실수를 꼭 써야 하면 N 샘플마다 창을 다시 더해 합을 재동기화. 참고로 정수 코드를 float 로 더하는 것은 합이 2^24 미만인 동안은 정확하다 — 드리프트의 원인은 `3.3/4095` 처럼 **표현 불가능한 값**을 더하는 것이다.

**O(1) vs O(N) 실측** (`demo_bench.c`, Apple M6, Apple clang 21.0.0, `-O2`, 워밍업 + 7회 중앙값, 측정 직전 load average 10.00 — 다른 작업이 같이 돌고 있으니 비율을 볼 것, 실제 출력):

| N | 누적합 O(1) ns/샘플 | 매번 다시 더하기 O(N) ns/샘플 | 비율 |
|---|---|---|---|
| 4 | 0.90 | 1.35 | 1.5 |
| 16 | 0.94 | 1.67 | 1.8 |
| 64 | 0.98 | 2.07 | 2.1 |
| 256 | 0.81 | 5.66 | 6.9 |
| 1024 | 0.81 | 21.57 | 26.5 |
| 4096 | 0.89 | 83.98 | 94.8 |

누적합은 N 과 무관하게 ~0.9 ns. 다시 더하기는 N 에 비례한다 (N 이 작을 때 차이가 작은 건 M 시리즈 NEON 이 안쪽 합 루프를 벡터화하기 때문 — Cortex-M0/M3 에는 그런 게 없다). 바로 다음 실행(load 8.10)의 비율은 1.4 / 1.7 / 2.4 / 6.4 / 28.3 / 91.1.

### Part B — 합 ≤ S 최장 구간: 왜 l 이 뒤로 안 가나

**불변식**: r 번째 반복이 끝나면 `[l, r]` 은 "r 로 끝나는 구간 중 합 ≤ S 인 **가장 긴** 것"이고 `sum` 은 그 합이다 (빈 구간 l = r+1 도 허용, 합 0 ≤ S).

**왜 l 을 되돌릴 필요가 없나 (단조성).** 값이 비음수면 구간을 **넓히면 합이 줄지 않는다**. r 에서 l 이 l* 까지 밀렸다는 건 `[l*−1, r]` 의 합 > S 라는 뜻이고, 그러면 모든 r' > r 에 대해 `[l*−1, r']` 의 합 ≥ `[l*−1, r]` 의 합 > S 다. 즉 "지금 너무 크다 = 앞으로도 영원히 너무 크다". 그래서 l 은 앞으로만 움직이고, l 과 r 이 각각 최대 n 번 움직이므로 전체 **O(n)** (while 이 안에 있어도 총 반복은 ≤ 2n — 분할 상환, amortized).

```svg
<svg viewBox="0 0 740 345" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">a = [1, 6, 3, 1, 2, 4, 5], S = 10 — 각 r 이 끝난 뒤의 창 [l, r]</text>
  <g fill="currentColor" text-anchor="middle" font-size="12" opacity=".8">
    <text x="130" y="46">0</text><text x="174" y="46">1</text><text x="218" y="46">2</text><text x="262" y="46">3</text><text x="306" y="46">4</text><text x="350" y="46">5</text><text x="394" y="46">6</text>
  </g>
  <g stroke="currentColor">
    <rect x="110" y="56" width="40" height="26" class="f-acc"/><rect x="154" y="56" width="40" height="26" fill="none"/><rect x="198" y="56" width="40" height="26" fill="none"/><rect x="242" y="56" width="40" height="26" fill="none"/><rect x="286" y="56" width="40" height="26" fill="none"/><rect x="330" y="56" width="40" height="26" fill="none"/><rect x="374" y="56" width="40" height="26" fill="none"/>
    <rect x="110" y="90" width="40" height="26" class="f-acc"/><rect x="154" y="90" width="40" height="26" class="f-acc"/><rect x="198" y="90" width="40" height="26" fill="none"/><rect x="242" y="90" width="40" height="26" fill="none"/><rect x="286" y="90" width="40" height="26" fill="none"/><rect x="330" y="90" width="40" height="26" fill="none"/><rect x="374" y="90" width="40" height="26" fill="none"/>
    <rect x="110" y="124" width="40" height="26" class="f-acc"/><rect x="154" y="124" width="40" height="26" class="f-acc"/><rect x="198" y="124" width="40" height="26" class="f-acc"/><rect x="242" y="124" width="40" height="26" fill="none"/><rect x="286" y="124" width="40" height="26" fill="none"/><rect x="330" y="124" width="40" height="26" fill="none"/><rect x="374" y="124" width="40" height="26" fill="none"/>
    <rect x="110" y="158" width="40" height="26" class="f-mut"/><rect x="154" y="158" width="40" height="26" class="f-acc"/><rect x="198" y="158" width="40" height="26" class="f-acc"/><rect x="242" y="158" width="40" height="26" class="f-acc"/><rect x="286" y="158" width="40" height="26" fill="none"/><rect x="330" y="158" width="40" height="26" fill="none"/><rect x="374" y="158" width="40" height="26" fill="none"/>
    <rect x="110" y="192" width="40" height="26" class="f-mut"/><rect x="154" y="192" width="40" height="26" class="f-mut"/><rect x="198" y="192" width="40" height="26" class="f-acc"/><rect x="242" y="192" width="40" height="26" class="f-acc"/><rect x="286" y="192" width="40" height="26" class="f-acc"/><rect x="330" y="192" width="40" height="26" fill="none"/><rect x="374" y="192" width="40" height="26" fill="none"/>
    <rect x="110" y="226" width="40" height="26" class="f-mut"/><rect x="154" y="226" width="40" height="26" class="f-mut"/><rect x="198" y="226" width="40" height="26" class="f-ok"/><rect x="242" y="226" width="40" height="26" class="f-ok"/><rect x="286" y="226" width="40" height="26" class="f-ok"/><rect x="330" y="226" width="40" height="26" class="f-ok"/><rect x="374" y="226" width="40" height="26" fill="none"/>
    <rect x="110" y="260" width="40" height="26" class="f-mut"/><rect x="154" y="260" width="40" height="26" class="f-mut"/><rect x="198" y="260" width="40" height="26" class="f-mut"/><rect x="242" y="260" width="40" height="26" class="f-mut"/><rect x="286" y="260" width="40" height="26" class="f-mut"/><rect x="330" y="260" width="40" height="26" class="f-acc"/><rect x="374" y="260" width="40" height="26" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="130" y="74">1</text><text x="174" y="74">6</text><text x="218" y="74">3</text><text x="262" y="74">1</text><text x="306" y="74">2</text><text x="350" y="74">4</text><text x="394" y="74">5</text>
    <text x="130" y="108">1</text><text x="174" y="108">6</text><text x="218" y="108">3</text><text x="262" y="108">1</text><text x="306" y="108">2</text><text x="350" y="108">4</text><text x="394" y="108">5</text>
    <text x="130" y="142">1</text><text x="174" y="142">6</text><text x="218" y="142">3</text><text x="262" y="142">1</text><text x="306" y="142">2</text><text x="350" y="142">4</text><text x="394" y="142">5</text>
    <text x="130" y="176">1</text><text x="174" y="176">6</text><text x="218" y="176">3</text><text x="262" y="176">1</text><text x="306" y="176">2</text><text x="350" y="176">4</text><text x="394" y="176">5</text>
    <text x="130" y="210">1</text><text x="174" y="210">6</text><text x="218" y="210">3</text><text x="262" y="210">1</text><text x="306" y="210">2</text><text x="350" y="210">4</text><text x="394" y="210">5</text>
    <text x="130" y="244">1</text><text x="174" y="244">6</text><text x="218" y="244">3</text><text x="262" y="244">1</text><text x="306" y="244">2</text><text x="350" y="244">4</text><text x="394" y="244">5</text>
    <text x="130" y="278">1</text><text x="174" y="278">6</text><text x="218" y="278">3</text><text x="262" y="278">1</text><text x="306" y="278">2</text><text x="350" y="278">4</text><text x="394" y="278">5</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="20" y="74">r=0</text><text x="20" y="108">r=1</text><text x="20" y="142">r=2</text><text x="20" y="176">r=3</text><text x="20" y="210">r=4</text><text x="20" y="244">r=5</text><text x="20" y="278">r=6</text>
    <text x="430" y="74">sum 1          len 1  best 1</text>
    <text x="430" y="108">sum 7          len 2  best 2</text>
    <text x="430" y="142">sum 10         len 3  best 3</text>
    <text x="430" y="176">11 -1 → 10     len 3  best 3</text>
    <text x="430" y="210">12 -6 → 6      len 3  best 3</text>
    <text x="430" y="244">sum 10         len 4  best 4 @2</text>
    <text x="430" y="278">15 -3-1-2 → 9  len 2  best 4</text>
  </g>
  <text x="10" y="312" fill="currentColor">진한 칸 = 현재 창, 흐린 칸 = 버린 왼쪽(다시 안 봄), 초록 = 최장 [3,1,2,4] (start 2)</text>
  <text x="10" y="332" fill="currentColor">l : 0 → 0 → 0 → 1 → 2 → 2 → 5 — 앞으로만 움직인다</text>
</svg>
```

**같은 길이면 가장 왼쪽.** 길이 갱신을 `>` 로만 하면, 최장 길이 L 이 **처음** 나온 r 의 창이 기록된다. 더 왼쪽에 길이 L 인 구간 `[l', r']` 이 있다면 r' < r 이고, r' 에서 알고리즘의 창은 "r' 로 끝나는 가장 긴 유효 구간" 이므로 길이 ≥ L — 그러면 r' 에서 이미 기록됐어야 한다. 모순. 브루트포스도 l 오름차순 + `>` 라 같은 규칙이다.

**합을 64비트로 둔 이유.** 축소하기 직전 `sum` 은 최대 S + 65535 다. S 가 `uint32_t` 최대값 근처면 32비트가 넘친다. 32비트로 버티려면 `a[r] > S − sum` 처럼 빼기 쪽으로 비교해야 한다 (Q&A 3). 테스트에 `S = 4294967295` 가 있는 이유.

**음수가 섞이면 틀린다.** 위 단조성 논증이 "넓히면 합이 줄지 않는다"에 기대고 있으므로, 음수가 있으면 깨진다.

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">반례: a = [3, -5, 3], S = 1</text>
  <text x="20" y="50" fill="currentColor">축소 윈도우 (window_sum_le_signed)</text>
  <g stroke="currentColor">
    <rect x="20" y="62" width="44" height="26" class="f-warn"/><rect x="68" y="62" width="44" height="26" fill="none"/><rect x="116" y="62" width="44" height="26" fill="none"/>
    <rect x="20" y="96" width="44" height="26" class="f-mut"/><rect x="68" y="96" width="44" height="26" class="f-acc"/><rect x="116" y="96" width="44" height="26" fill="none"/>
    <rect x="20" y="130" width="44" height="26" class="f-mut"/><rect x="68" y="130" width="44" height="26" class="f-acc"/><rect x="116" y="130" width="44" height="26" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="42" y="80">3</text><text x="90" y="80">-5</text><text x="138" y="80">3</text>
    <text x="42" y="114">3</text><text x="90" y="114">-5</text><text x="138" y="114">3</text>
    <text x="42" y="148">3</text><text x="90" y="148">-5</text><text x="138" y="148">3</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="175" y="80">r=0: 3 &gt; 1 → 3 을 영원히 버림</text>
    <text x="175" y="114">r=1: -5 ≤ 1, len 1</text>
    <text x="175" y="148">r=2: -2 ≤ 1, len 2  → 답 2</text>
  </g>
  <text x="440" y="50" fill="currentColor">정답 (브루트포스)</text>
  <g stroke="currentColor">
    <rect x="440" y="62" width="44" height="26" class="f-ok"/><rect x="488" y="62" width="44" height="26" class="f-ok"/><rect x="536" y="62" width="44" height="26" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="462" y="80">3</text><text x="510" y="80">-5</text><text x="558" y="80">3</text>
  </g>
  <text x="440" y="114" fill="currentColor" font-family="monospace" font-size="12.5">3 - 5 + 3 = 1 ≤ 1, len 3</text>
  <text x="10" y="180" fill="currentColor">r=0 에서 "너무 크다"던 3 이, 뒤에 -5 가 오자 다시 들어갈 수 있게 됐다.</text>
  <text x="10" y="200" fill="currentColor">넓히면 합이 줄 수 있으니 "지금 너무 크다 = 영원히 너무 크다" 가 깨진다.</text>
</svg>
```

테스트의 실제 출력:

```text
counterexample {3,-5,3} S=1: sliding window=2 brute force=3
signed sliding window vs brute force: with negatives 307/1000 wrong, non-negative 0/1000 wrong
```

무작위 배열(값 −10..10)의 약 31%에서 틀린다. **일반 해법**(구현하지 않음): 누적합 P[i] = a[0] + … + a[i−1] 로 바꾸면 조건은 P[r+1] − P[l] ≤ S, 즉 P[l] ≥ P[r+1] − S 인 가장 작은 l 을 찾는 문제다. 접두 최대값 M[l] = max(P[0..l]) 는 단조 증가하므로 r 마다 이진 탐색 → O(n log n). 또는 "앞쪽 최대값 후보"만 남기는 단조 스택(monotonic stack)으로 O(n). 누적합 자체는 ALGO-M13, 같은 계열의 유명 문제는 LeetCode 862 Shortest Subarray with Sum at Least K (음수 때문에 단조 덱이 필요).

### Part B — 중복 없는 최장 바이트 구간

불변식: r 번째 반복 뒤 `[l, r]` 은 "r 로 끝나는, 중복 없는 가장 긴 구간". 새 바이트 c 가 창 안에 이미 있다면 그 위치 p 이하에서 시작하는 구간은 모두 c 가 두 번이다 → l = p + 1. `last[c]` 에 "위치 + 1" 을 저장하면 0 을 "아직 없음"으로 쓸 수 있고, "창 안에 있다" ⇔ p ≥ l ⇔ `last[c] > l` 이다. `"pwwkew"` 추적:

| r | 바이트 | `last[c]` (전) | l | 창 | 길이 | best (start) |
|---|---|---|---|---|---|---|
| 0 | p | 0 | 0 | p | 1 | 1 (0) |
| 1 | w | 0 | 0 | pw | 2 | 2 (0) |
| 2 | w | 2 > 0 | **2** | w | 1 | 2 (0) |
| 3 | k | 0 | 2 | wk | 2 | 2 (0) |
| 4 | e | 0 | 2 | wke | 3 | **3 (2)** |
| 5 | w | 3 > 2 | 3 | kew | 3 | 3 (2) |

**`"abba"` 함정**: `if (last[c] > l)` 없이 `l = last[c]` 로 쓰면 r=3 의 `a` 에서 `last[a] = 1` 이라 l 이 2 → 1 로 **뒤로** 가서 `"bba"`(길이 3)를 답한다. 옛 위치가 이미 창 밖이면 무시해야 한다 (`l = max(l, last[c])`).

**바이트라서 생기는 것**: ① 입력이 0x00 을 포함할 수 있으니 `strlen` 금지, 길이를 받는다. ② `char` 는 부호가 구현 정의라 0x80 이상이 음수 → `last[s[r]]` 가 음수 인덱스 (ARM 의 `char` 는 보통 unsigned, x86/Apple arm64 는 signed — 같은 코드가 보드에선 되고 PC 에선 터진다). `uint8_t` 로 받는다. ③ 표는 256 × `size_t` = 이 맥 2 KB, Cortex-M(32비트 size_t) 1 KB — 태스크 스택이 1 KB 인 RTOS 에선 이것만으로 넘친다. n < 65536 이면 `uint16_t last[256]` (512 B) 또는 `static`.

브루트포스(각 l 에서 반복이 나올 때까지)와 3000개 무작위 바이트열(길이 0..599, 알파벳 크기 1..256, 0x80 이상 포함)로 대조: `mismatches vs brute force = 0`.

### Part C — ISR 은 쓰고, 태스크는 확인하고 읽는다

```svg
<svg viewBox="0 0 740 285" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">웜업 중 찢어진 읽기: 샘플 10, 20 (평균 15) → ISR 이 90 을 넣음 (평균 40)</text>
  <text x="705" y="22" fill="currentColor" text-anchor="end" font-size="12">시간 →</text>
  <rect x="80" y="36" width="650" height="62" rx="8" class="f-mut"/>
  <rect x="80" y="108" width="650" height="62" rx="8" class="f-mut"/>
  <g fill="currentColor" font-weight="bold"><text x="14" y="72">태스크</text><text x="14" y="144">ADC ISR</text></g>
  <rect x="92" y="46" width="190" height="42" rx="6" class="f-acc" stroke="currentColor"/>
  <rect x="292" y="118" width="230" height="42" rx="6" class="f-warn" stroke="currentColor"/>
  <rect x="532" y="46" width="190" height="42" rx="6" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12.5">
    <text x="187" y="72">① sum = pub_sum → 30</text>
    <text x="407" y="136">push(90): sum=120</text>
    <text x="407" y="152">cnt=3, seq 2→3</text>
    <text x="627" y="72">② cnt = pub_cnt → 3</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="20" y="198">naive: (30 + 1) / 3 = 10   ← 전(15)도 후(40)도 아닌 값</text>
    <text x="20" y="220">seq:   s0=2 … sum=30 … cnt=3 … seq=3 ≠ 2 → 다시: (120, 3) → 40</text>
  </g>
  <text x="20" y="250" fill="currentColor">창이 꽉 찬 뒤엔 cnt 가 안 바뀌어 32비트 MCU 에선 sum 한 번 읽기로 충분하다.</text>
  <text x="20" y="270" fill="currentColor">8/16비트 MCU 에선 32비트 sum 자체가 두 번에 읽혀 찢어진다 (연습 2).</text>
</svg>
```

**무엇이 원자적이어야 하나.** 평균은 `sum` 과 `count` **두 값의 쌍**으로 계산된다. 각각은 정렬된 32비트 워드라 Cortex-M 에서 한 번의 `LDR`/`STR` 이 단일 복사 원자적(single-copy atomic)이지만, **쌍**은 아니다. 태스크가 sum 을 읽고 count 를 읽기 전에 ISR 이 끼면 "옛 sum ÷ 새 count" 가 된다. 테스트 실제 출력:

```text
naive read with ISR between loads: 10 (before=15, after=40)
Part C: 20000 reads with random ISR point: seq-read inconsistent=0, naive inconsistent=1007
```

**선택지 비교.**

| 방법 | 태스크 쪽 비용 | ISR 지연 | 메모 |
|---|---|---|---|
| 인터럽트 잠깐 끄기 (`PRIMASK` 저장 → 두 값 복사 → 복원) | 몇 사이클 | 복사 동안 ISR 지연 | 가장 단순·확실. 멀티코어엔 안 통함 |
| **시퀀스 카운터** (이 레슨) | 드물게 재시도 | 없음 | ISR 이 태스크를 끊는 한 코어 구조에 딱 맞음. 작성자는 ISR 하나뿐 |
| ISR 이 평균까지 계산해 **워드 하나**로 공개 | `LDR` 1번 | ISR 이 나눗셈까지 (M0 는 `__aeabi_uidiv`) | 꽉 찬 뒤 2^k 면 시프트라 공짜. 가장 깔끔한 경우가 많다 |
| C11 `_Atomic` / LDREX·STREX | — | — | 64비트 쌍 원자성은 M 에서 보장 안 됨. C-A03 |

시퀀스 카운터의 정당성 (한 코어): ISR 은 태스크를 끊고 **끝까지** 돈 다음에 돌아간다. 태스크가 읽은 두 `seq` 가 같다면 그 사이에 ISR 이 한 번도 끝나지 않았다는 뜻이고, ISR 이 "중간에 멈춘" 상태를 태스크가 볼 수는 없으므로, 그 사이에 읽은 `(sum, cnt)` 는 한 시점의 쌍이다. `seq` 가 32비트라 두 읽기 사이에 ISR 이 2^32 번 돌아 같은 값으로 돌아오는 일은 현실에서 없다. 멀티코어(ISR 이 다른 코어에서 돌거나 DMA 가 쓰는 경우)라면 작성자가 "쓰기 시작 시 홀수, 끝나면 짝수"로 표시하고 메모리 배리어(acquire/release)를 넣어야 한다 — 리눅스 seqlock 이 그 형태다 (C-A02, C-A03).

`volatile` 은 컴파일러가 `pub_sum`, `seq` 읽기를 생략하거나 레지스터에 캐시하지 못하게 할 뿐이다 (C-B09). volatile 접근끼리의 순서는 컴파일러가 지키지만, 원자성은 주지 않는다. 그리고 **ISR 의 작업량**: push 는 루프 없는 10여 개 명령이다. 10 kHz ADC 를 64 MHz 코어에서 받으면 샘플당 6400 사이클 예산인데, N=256 창을 매번 다시 더하는 ISR 은 그 예산의 상당 부분을 루프에 쓴다 — 샘플당 O(1) 이 "빠르다"를 넘어 **최악 시간이 N 과 무관하다**는 점이 핵심이다. 링 버퍼 자체(크기 판별, 마스크 인덱스)는 DS-B03·DS-B04, ISR↔태스크 큐는 DS-A02, 공유 변수 일반론은 C-M14.

### 복잡도 정리

| 함수 | 시간 | 추가 메모리 | 스택 |
|---|---|---|---|
| `ma_push` / `ma_avg` | O(1) | 버퍼 2n B + 상태 20 B | 고정, 재귀 없음 |
| 브루트포스 이동 평균 | O(N)/샘플 | 같음 | — |
| `longest_sum_le` | O(n), l·r 각각 ≤ n 번 이동 | O(1) | 고정 |
| `longest_unique_run` | O(n) | 256 × `size_t` (맥 2 KB / M 1 KB) | 표가 스택에 있으면 그만큼 |
| 브루트포스 최장 구간 | O(n²) / O(n·min(n,256)) | O(1) / 256 B | — |
| `task_read_avg` | O(1) + 재시도 (ISR 이 끼어든 횟수만큼) | O(1) | 고정 |

가변 윈도우 실측 (`demo_bench.c`, 같은 조건, 값 0..4095, S = 102400): n = 1000 / 4000 / 16000 에서 슬라이딩 0.0010 / 0.0040 / 0.0400 ms, 브루트포스 0.34 / 5.63 / 89.56 ms. 브루트포스는 n 이 4배면 16배 (O(n²)), 슬라이딩 쪽은 타이머 해상도(~1 µs) 근처라 비율이 들쭉날쭉하다 (다음 실행: 0.0010 / 0.0050 / 0.0210 ms).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 42/42 passed, 0.15 s
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_san && ./solution_san   # 42/42, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 웜업 중에도 `sum / N` | 0 으로 채운 것처럼 평균이 낮게 나옴 → 가짜 저전압 경보 | `sum / count` (또는 prefill, "준비 안 됨") |
| 웜업 중에도 `sum >> shift` | count 가 2^k 가 아니라 틀린 값 | 시프트는 `count == n` 일 때만 |
| `if (count < n) count++; sum -= buf[head];` 순서 뒤섞기 | 꽉 차기 전에 빈 칸(쓰레기/0) 을 빼거나, 꽉 찬 뒤에 안 뺌 | `if (count == n) sum -= buf[head]; else count++;` |
| 누적합을 `uint16_t` 로 | 샘플 2개만 더해도 넘칠 수 있음 | 범위 증명 후 `uint32_t` (n ≤ 65537) |
| 누적합을 `float` 로, 단위 변환 후 더함 | 반올림 오차 누적: 10^8 샘플에 1.86 LSB (위 표) | 정수 코드로 더하고 마지막에 한 번 변환 |
| `sum / count` 를 반올림 없이 | 항상 버림 → 평균이 최대 1 LSB 아래로 치우침 (편향) | `(sum + count/2) / count` |
| 음수 샘플에 `(sum + k/2) / k` | `/` 는 0 쪽 버림이라 음수에서 반올림 방향이 틀림 | 부호별로 `± k/2` (Q&A 2) |
| 최장 구간 갱신을 while 안에서 | while 안의 창은 **무효**(합 > S) 일 수 있음 | while 을 빠져나온 뒤 기록 |
| `l = last[c];` (max 없이) | 창 밖의 옛 위치로 l 이 뒤로 감 (`"abba"` → 3) | `if (last[c] > l) l = last[c];` |
| `const char *s; last[s[r]]` | 0x80 이상 바이트가 음수 인덱스 | `const uint8_t *s` |
| 음수 값에 축소 윈도우 | 단조성이 없어 답이 짧게 나옴 (`{3,−5,3}`) | 누적합 + 접두 최대 + 이진 탐색 |
| 태스크가 `sum`, `count` 를 따로 읽음 | ISR 이 사이에 끼면 찢어진 쌍 | 시퀀스 카운터 / IRQ 잠깐 끄기 / 워드 하나로 공개 |

## 8. 면접 Q&A

<details>
<summary>Q1. 이동 평균 합의 타입은 어떻게 고르나요? 2^k 샘플이 아니면 어떻게 하나요?</summary>

최대 샘플 × N 이 타입에 들어가야 한다. 16비트 샘플이면 `uint32_t` 로 N ≤ 65537 ((2^16+1)(2^16−1) = 2^32−1), 반올림 항까지 넣으면 N ≤ 65536. 12비트면 N ≤ 1,048,832 (반올림 항까지 1,048,704). 부호 있는 샘플이면 `int32_t` 와 |샘플| ≤ 32768 기준으로 같은 계산. N 이 2^k 가 아니면: 상수라면 컴파일러가 곱셈-상위로 바꿔 준다 (`/100` → `umull` + 0x51EB851F, 위 표). 나눗셈이 없는 M0 에서 매 샘플 나누기 싫으면 ① N 을 2^k 로 바꾸자고 제안 (필터 특성이 약간 다름 — DSP-B06) ② 평균 대신 합 자체를 임계값×N 과 비교 (연습 5) ③ 출력 주기를 낮춰 나눗셈 횟수를 줄인다.

</details>

<details>
<summary>Q2. 샘플이 부호 있는 값(자이로 int16)이면 반올림은?</summary>

C11 의 정수 나눗셈은 0 쪽 버림(6.5.5)이라 `(sum + k/2) / k` 는 음수에서 틀린다. 예: 평균 −2.7 (sum = −27, k = 10) → (−27 + 5)/10 = −22/10 = −2 (0 쪽 버림), 가장 가까운 정수는 −3. 0 에서 먼 쪽 반올림이면 `sum >= 0 ? (sum + k/2)/k : (sum − k/2)/k`. `>>` 로 바꾸려면 음수의 `>>` 가 **구현 정의**(6.5.7p5)라는 점을 말해야 한다 — 실제로는 산술 시프트(floor)라 `(sum + k/2) >> s` 는 "+∞ 쪽 0.5 올림" 이 된다. 어떤 규칙이든 **문서로 정하고** 테스트로 고정하는 게 답이다. 정해지지 않은 버림은 평균에 −0.5 LSB 편향을 만든다.

</details>

<details>
<summary>Q3. 합 ≤ S 에서 sum 을 32비트로만 유지할 수 있나요?</summary>

넣기 전에 비교하면 된다: `while (a[r] > S - sum) sum -= a[l++];` 를 먼저 돌리고 그 다음 `sum += a[r]`. 불변식 sum ≤ S 이므로 `S − sum` 은 음수가 안 된다. 단 a[r] > S 인 원소는 창을 비우고도 못 넣으니 `l = r + 1, sum = 0` 처리가 필요하다. 64비트 덧셈이 Cortex-M3/M4 에서 `ADDS`+`ADC` 두 명령이라 큰 비용은 아니지만, "넘침을 피하려면 빼기 쪽으로 비교한다"는 것이 면접관이 듣고 싶은 말이다 (`lo + (hi − lo)/2` 와 같은 발상).

</details>

<details>
<summary>Q4. 왜 O(n) 인가요? 안에 while 이 있는데요.</summary>

l 은 0 에서 시작해 증가만 하고 n 을 넘지 않는다. 그러니 while 본문은 **전체 실행 동안** 최대 n 번. r 루프도 n 번 → 합 ≤ 2n. 이걸 분할 상환(amortized) 분석이라 한다. 최악 한 단계는 O(n) 일 수 있지만(r=6 에서 3번 줄임) 전체는 O(n). ISR 에서 한 샘플씩 받는 스트리밍 버전이라면 "샘플 하나에 최악 O(창 길이)" 라는 점은 말해 둬야 한다 — 실시간 최악 시간은 분할 상환과 다르다.

</details>

<details>
<summary>Q5. 값에 음수가 있으면 어떻게 하나요?</summary>

반례 `{3,−5,3}, S=1`: 축소 윈도우 2, 정답 3. 넓히면 합이 줄 수 있어 "지금 너무 크다 = 영원히 너무 크다"가 깨진다. 일반 해법은 누적합 P 로 바꿔 "P[l] ≥ P[r+1] − S 인 가장 작은 l" — 접두 최대값이 단조라 이진 탐색 O(n log n), 단조 스택으로 O(n). 합 = K 개수 세기(LeetCode 560 Subarray Sum Equals K)는 누적합 + 해시. 음수가 있는 최단 구간 합 ≥ K 는 LeetCode 862 (단조 덱).

</details>

<details>
<summary>Q6. ISR 이 넣고 태스크가 평균을 읽을 때 무엇을 보호해야 하나요? 창이 꽉 찬 뒤에도요?</summary>

평균은 (sum, count) 쌍의 함수 → 쌍이 한 시점의 값이어야 한다. 웜업 중엔 둘 다 바뀌므로 찢어진다 (10 = 30/3, 전 15·후 40). 꽉 찬 뒤엔 count 가 상수라 32비트 MCU 에서는 sum 의 정렬된 32비트 읽기 한 번이 원자적이라 안전하다. 하지만 8/16비트 MCU(AVR, MSP430)에선 32비트 sum 을 여러 번에 읽으므로 꽉 찬 뒤에도 찢어진다 — 연습 2 에서 0xFFFF → 0x10003 사이에 읽어 0x1FFFF (평균 32768, 원래 16384) 가 나왔다. 32비트 MCU 에서도 64비트 값(`LDRD`)은 원자적이지 않다. 해결: 인터럽트 잠깐 끄기, 시퀀스 카운터, 또는 ISR 이 평균을 워드 하나로 공개.

</details>

<details>
<summary>Q7. 이동 평균 말고 "최근 N 샘플의 최대값"(피크 홀드)은 O(1) 로 되나요?</summary>

최대값은 빼기가 없어서 누적합처럼 못 한다 (나간 값이 최대였으면 다음 최대를 모른다). 단조 덱(monotonic deque) — 앞으로 최대가 될 가능성이 있는 값만 내림차순으로 보관 — 으로 분할 상환 O(1), 메모리 O(N). LeetCode 239 Sliding Window Maximum. 최악 한 샘플은 O(N) 이므로 ISR 에 넣을 땐 그 점을 말한다. 분산이 필요하면 Welford(ALGO-A08), 이상치 제거면 메디안(SENS-B07). 메모리가 N 샘플도 아까우면 1차 IIR(EMA) `y += (x − y) >> k` — 상태 하나뿐이지만 정수로 하면 버림 때문에 정상 상태 오차가 남아 소수 비트를 붙인 고정소수점으로 들고 다닌다 (DSP-B07, C-A09). 이동 평균의 주파수 응답(fs/N 배수의 0점)은 DSP-B06.

</details>

## 9. 추가 연습

### 연습 1 — prefill 웜업: 처음부터 시프트만

N = 2^k 전용 이동 평균에서, 첫 샘플이 오면 창 전체를 그 값으로 채워 두자 (sum = x₀ << k). 그 뒤로는 매번 `(sum + N/2) >> k` 만. 기준값은 "앞에 x₀ 가 N−1 개 더 있었던 가상 스트림"의 창 평균.

<details>
<summary>답 보기</summary>

첫 push 에서만 O(N) 채우기, 이후 O(1). `pf_push(100)` 뒤 평균 100, `pf_push(200)` 뒤 (100·3 + 200)/4 = 125 — 나눗셈 정책이었다면 150. N 샘플이 지나면 두 정책은 완전히 같다. 장점: 첫 push 이후로는 웜업 분기·나눗셈이 없어 ISR 이 항상 같은 경로·같은 사이클 수. 단점: 첫 샘플이 스파이크면 N 샘플 동안 끌려간다 → 첫 샘플을 몇 개 버리거나 메디안으로 시드하기도 한다. 마스크 인덱스 `head = (head + 1) & (N − 1)` 은 DS-B04.

</details>

### 연습 2 — 16비트 MCU 에서 32비트 합 읽기

`sum_lo`, `sum_hi` 두 반쪽으로 저장된 합을 하위 → 상위로 읽는 동안 ISR 이 끼면? 창 `{0x4000, 0x4000, 0x3FFF, 0x4000}` (합 0xFFFF) 에 0x4004 가 들어오면 합은 0x10003 이 된다. 시퀀스 카운터(16비트 = 한 번에 읽힘)로 고쳐라.

<details>
<summary>답 보기</summary>

실제 출력: `naive sum read = 0x1FFFF (before 0x0FFFF, after 0x10003) -> avg 32768 instead of 16384/16385`. 옛 하위 0xFFFF + 새 상위 0x0001 → 원래 값의 두 배. 노이즈를 지우려는 필터가 **스파이크를 만든다**. 창이 꽉 차 있어도 생긴다는 게 32비트 MCU 와의 차이. 시퀀스 버전은 모든 끼어들기 지점(1..6)에서 0xFFFF 또는 0x10003 만 낸다. 참고: 단조 증가 카운터(타이머)라면 "상위-하위-상위 다시 읽기"(hi-lo-hi)도 쓰지만, 합처럼 오르내리는 값은 ISR 두 번에 상위가 원래 값으로 돌아오는 ABA 가 가능해 시퀀스 카운터가 안전하다.

</details>

### 연습 3 — 합 ≥ S 인 가장 짧은 구간

비음수 배열에서 합 ≥ S 인 가장 짧은 구간 (LeetCode 209 Minimum Size Subarray Sum, 시작 위치까지). 펌웨어 예: 누적 충전량이 임계값에 도달하는 가장 짧은 버스트.

<details>
<summary>답 보기</summary>

"최장" 과 반대로 **유효한 동안** 줄이면서 기록한다: `sum += a[r]; while (sum >= S) { 기록; sum -= a[l++]; }`. 최장은 while 이 끝난 뒤(유효해진 뒤) 기록, 최단은 while 안(유효한 동안) 기록 — 이 대칭이 템플릿의 핵심. `{2,3,1,2,4,3}, S=7` → 2 (`[4,3]`, start 4). S = 0 이면 l ≤ r 가드가 필요하다.

</details>

### 연습 4 — 서로 다른 바이트가 K 개 이하인 최장 구간

`cnt[256]` 과 "종류 수" `distinct` 를 들고 다니는 가변 창. `"eceba", K=2` → 3 (`"ece"`). (LeetCode 340 Longest Substring with At Most K Distinct Characters 의 바이트 판.)

<details>
<summary>답 보기</summary>

`if (cnt[s[r]]++ == 0) distinct++; while (distinct > K) { if (--cnt[s[l]] == 0) distinct--; l++; }`. 중복 없는 구간은 "모든 바이트 개수 ≤ 1" 이라는 조건의 가변 창이었고, 여기선 "종류 수 ≤ K". 조건이 **넓히면 나빠지기만 하고 줄이면 좋아지기만** 하면(단조) 같은 템플릿이 통한다. K = 0 이면 답 0.

</details>

### 연습 5 — 나눗셈 없는 과전류 경보

길이 N 창의 **정확한** 평균이 limit 을 넘는 창의 개수. 평균 > limit ⇔ sum > limit·N 이므로 나눗셈이 필요 없다.

<details>
<summary>답 보기</summary>

`thr = limit * N` 을 한 번 계산하고 `sum > thr` 비교. `{10,10,10,11}, N=4, limit=10` → 평균 10.25 라 경보 1개 — 반올림한 평균(10)을 `> 10` 과 비교했다면 **놓친다**. 경보 판정은 반올림 전의 합으로 해야 한다. `limit * N` 도 범위 증명이 필요하다 (65535 × N ≤ 2^32 − 1 → N ≤ 65537). double 브루트포스와 무작위 40개 스트림에서 일치.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 18/18 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_san && ./practice_san   # 18/18, 보고 없음
```

## 10. 핵심 정리

- 고정 윈도우 = 링 버퍼 + 누적합: `if (full) sum -= buf[head]; else count++; buf[head] = x; sum += x; head++`. 샘플당 O(1), **최악 시간이 N 과 무관**.
- 합 타입은 증명한다: uint16 × N ≤ uint32 ⇔ N ≤ 65537 (= (2^16+1)(2^16−1) = 2^32−1), 반올림 항까지 N ≤ 65536. unsigned 는 중간 wrap 이 무해, signed 오버플로는 UB.
- 웜업 정의(받은 개수로 / prefill / 준비 안 됨)를 먼저 말한다. 0 으로 채운 sum/N 은 버그.
- 0.5 올림 = `(sum + k/2) / k`. 시프트는 N = 2^k **그리고** 창이 꽉 찼을 때만. 음수: `/` 는 0 쪽 버림, `>>` 는 구현 정의.
- 정수 누적합은 드리프트 0, float 누적합은 이 맥에서 10^8 샘플에 1.86 LSB.
- 가변 윈도우: 넓히고, 깨지면 줄이고, (최장은) 줄인 뒤 기록. l 은 뒤로 안 간다 → O(n) 분할 상환. 근거는 **단조성**(비음수) — `{3,−5,3}, S=1` 이 반례, 일반 해법은 누적합 + 접두 최대/단조 구조.
- 중복 없는 구간: `last[c]` = 위치+1, `if (last[c] > l) l = last[c]` ("abba"), 입력은 `uint8_t` + 길이.
- ISR/태스크: (sum, count) **쌍**이 원자적이어야 한다 → 시퀀스 카운터, IRQ 잠깐 끄기, 또는 평균을 워드 하나로 공개. 8/16비트 MCU 는 꽉 찬 뒤에도 찢어진다.

## 참고자료

- AlgoMonster 노트 M03 §13~17 (Don 정리본) — Sliding Window Family Map, Subarray Sum Fixed, Sliding Window Longest, Longest Substring without Repeating Characters (`algomaster-coding-pattern/original-md/2026-09-27_M03_two_pointers.md`)
- LeetCode 3 Longest Substring Without Repeating Characters · 209 Minimum Size Subarray Sum · 239 Sliding Window Maximum · 340 Longest Substring with At Most K Distinct Characters · 346 Moving Average from Data Stream · 560 Subarray Sum Equals K · 643 Maximum Average Subarray I · 862 Shortest Subarray with Sum at Least K
- ISO/IEC 9899:2011 (C11) — 6.2.5p9 unsigned 연산은 mod 2^N, 6.5.5p6 정수 나눗셈은 0 쪽 버림, 6.5.7p5 음수 오른쪽 시프트는 구현 정의
- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403) — "Atomicity in the ARM architecture" 절, A3.5.3 (정렬된 워드 접근의 단일 복사 원자성; 절 번호는 기억에 의존 — 확인 권장) · *Cortex-M4 Technical Reference Manual* — 명령어 사이클 (UDIV/SDIV 2~12 사이클)
- D. Goldberg, "What Every Computer Scientist Should Know About Floating-Point Arithmetic", *ACM Computing Surveys* 23(1), 1991 — 반올림 오차 누적
- Linux kernel — Sequence counters and sequential locks: <https://docs.kernel.org/locking/seqlock.html>
- 이 레슨의 데모: `demo_float_drift.c` (float 드리프트), `demo_bench.c` (O(1) vs O(N), O(n) vs O(n²)), `demo_cm_avg.c` (Cortex-M4/M0 어셈블리: `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S demo_cm_avg.c -o -`)
- 관련 레슨: DS-B03·DS-B04 (링 버퍼, 마스크 인덱스), DS-A02 (ISR↔태스크 링 버퍼), C-M14 (ISR 공유 변수와 원자성), C-B09 (volatile), C-A02·C-A03 (배리어, C11 atomics), C-A09 (고정소수점), DSP-B06 (이동 평균 주파수 응답), DSP-B07 (EMA), SENS-B02 (오버샘플링·평균화), SENS-B07 (메디안), ALGO-B09 (투 포인터), ALGO-B12 (2의 거듭제곱 판별), ALGO-M13 (누적합), ALGO-A08 (Welford), ALGO-A11 (슬라이딩 로그 rate limiter)
