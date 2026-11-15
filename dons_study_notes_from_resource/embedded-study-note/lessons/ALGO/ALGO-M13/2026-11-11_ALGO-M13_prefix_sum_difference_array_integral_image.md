---
id: ALGO-M13
time: 75분
updated: 2026-11-11
---
# ALGO-M13 누적합과 차분 배열: 1D·2D

## 학습 목표

- 1D 누적합(prefix sum) `P[i+1] = P[i] + a[i]` 로 반열린 구간 [l, r) 의 합을 `P[r] − P[l]` 한 번에 답하고, 왜 P 가 n+1 칸이어야 하는지(보초 sentinel P[0] = 0) 설명한다.
- 차분 배열(difference array)로 "구간 [l, r) 에 v 더하기"를 O(1) 에 기록하고 한 번의 누적으로 복원한다. 누적합과 차분이 서로 **역연산**이라는 것을 식으로 보인다. 전력 타임라인의 피크 전류를 이것으로 구한다.
- 2D 누적합(integral image, summed-area table)을 만들고 포함–배제(inclusion–exclusion) 4번 읽기로 직사각형 합을 구한다. 640×480 프레임에서 AE 존 평균과 적응 이진화(Bradley–Roth)에 쓴다.
- 넘침(overflow)을 **숫자로** 분석한다: 640×480×255 = 78,336,000(27비트), ×4095 = 1,257,984,000(31비트) → uint32 로 충분. 그리고 "부호 없는 누적합은 감겨도(wrap) 구간 합 < 2^w 이면 차가 정확하다"를 증명하고, 그 경계(uint16·8비트 → 257개, uint32·12비트 → 1,048,832개)를 실측으로 확인한다.

## 1. 문제

"센서 버퍼에서 구간 평균을 수천 번 물어본다", "카메라 프레임의 영역 밝기를 매 프레임 낸다" — 펌웨어 코딩 면접에서 누적합은 O(n·q) 를 O(n + q) 로 바꾸는 가장 짧은 답이고, 꼬리질문은 거의 항상 **off-by-one, 넘침 비트 수, MCU 메모리**로 간다.

아이디어는 하나다. 매 질의마다 더하지 말고, **처음부터 i 까지의 합**을 한 번 미리 만들어 두면 어떤 구간의 합도 두 값의 차가 된다. 거꾸로, "구간에 v 를 더하라"는 갱신이 많을 때는 **이웃한 값의 차이**만 기록해 두었다가 마지막에 한 번 누적한다. 2D 에서는 같은 일을 행과 열 두 방향으로 한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">누적합: 구간 [2, 5) 의 합 = P[5] − P[2]</text>
  <text x="10" y="54" fill="currentColor">a</text>
  <g stroke="currentColor">
    <rect x="60" y="38" width="50" height="26" class="f-mut"/><rect x="110" y="38" width="50" height="26" class="f-mut"/>
    <rect x="160" y="38" width="50" height="26" class="f-acc"/><rect x="210" y="38" width="50" height="26" class="f-acc"/>
    <rect x="260" y="38" width="50" height="26" class="f-acc"/><rect x="310" y="38" width="50" height="26" class="f-mut"/>
    <rect x="360" y="38" width="50" height="26" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="80" y="56">3</text><text x="130" y="56">1</text><text x="180" y="56">4</text><text x="230" y="56">1</text>
    <text x="280" y="56">5</text><text x="330" y="56">9</text><text x="380" y="56">2</text>
  </g>
  <text x="10" y="104" fill="currentColor">P</text>
  <g stroke="currentColor">
    <rect x="35" y="88" width="50" height="26" class="f-mut"/><rect x="85" y="88" width="50" height="26" class="f-mut"/>
    <rect x="135" y="88" width="50" height="26" class="f-warn"/><rect x="185" y="88" width="50" height="26" class="f-mut"/>
    <rect x="235" y="88" width="50" height="26" class="f-mut"/><rect x="285" y="88" width="50" height="26" class="f-ok"/>
    <rect x="335" y="88" width="50" height="26" class="f-mut"/><rect x="385" y="88" width="50" height="26" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="55" y="106">0</text><text x="105" y="106">3</text><text x="155" y="106">4</text><text x="205" y="106">8</text>
    <text x="255" y="106">9</text><text x="300" y="106">14</text><text x="350" y="106">23</text><text x="400" y="106">25</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="50" y="130">P[0]</text><text x="150" y="130">P[2]</text><text x="300" y="130">P[5]</text><text x="400" y="130">P[7]</text>
  </g>
  <text x="460" y="56" fill="currentColor">P 는 n+1 칸, P[i] = a[0..i) 의 합</text>
  <text x="460" y="80" fill="currentColor">P[5] − P[2] = 14 − 4 = 10</text>
  <text x="460" y="100" fill="currentColor">= a[2] + a[3] + a[4] = 4 + 1 + 5</text>
  <text x="460" y="124" fill="currentColor">빈 구간 [l, l) = 0, 전체 = P[n]</text>
  <line x1="10" y1="150" x2="730" y2="150" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="174" fill="currentColor" font-weight="bold">차분: [1, 4) 에 +5 → D[1] += 5, D[4] −= 5 → 누적하면 고원(plateau)</text>
  <text x="10" y="208" fill="currentColor">D</text>
  <g stroke="currentColor">
    <rect x="60" y="192" width="50" height="26" class="f-mut"/><rect x="110" y="192" width="50" height="26" class="f-ok"/>
    <rect x="160" y="192" width="50" height="26" class="f-mut"/><rect x="210" y="192" width="50" height="26" class="f-mut"/>
    <rect x="260" y="192" width="50" height="26" class="f-warn"/><rect x="310" y="192" width="50" height="26" class="f-mut"/>
    <rect x="360" y="192" width="50" height="26" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="80" y="210">0</text><text x="124" y="210">+5</text><text x="180" y="210">0</text><text x="230" y="210">0</text>
    <text x="274" y="210">−5</text><text x="330" y="210">0</text><text x="380" y="210">0</text>
  </g>
  <text x="10" y="262" fill="currentColor">누적</text>
  <g stroke="currentColor">
    <rect x="60" y="246" width="50" height="26" class="f-mut"/><rect x="110" y="246" width="50" height="26" class="f-acc"/>
    <rect x="160" y="246" width="50" height="26" class="f-acc"/><rect x="210" y="246" width="50" height="26" class="f-acc"/>
    <rect x="260" y="246" width="50" height="26" class="f-mut"/><rect x="310" y="246" width="50" height="26" class="f-mut"/>
    <rect x="360" y="246" width="50" height="26" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="80" y="264">0</text><text x="130" y="264">5</text><text x="180" y="264">5</text><text x="230" y="264">5</text>
    <text x="280" y="264">0</text><text x="330" y="264">0</text><text x="380" y="264">0</text>
  </g>
  <text x="460" y="210" fill="currentColor">갱신 q 개 = O(q), 복원 1번 = O(n)</text>
  <text x="460" y="234" fill="currentColor">D 는 n+1 칸: r = n 이면 D[n] 에 기록</text>
  <text x="460" y="258" fill="currentColor">누적합 ∘ 차분 = 항등 (서로 역연산)</text>
  <text x="10" y="306" fill="currentColor">2D 도 같다: 직사각형 합 = 모서리 4개의 ± (Part C), 직사각형 증가 = 모서리 4개에 ±v (연습 3)</text>
</svg>
```

### Part A — 1D 누적합: ADC 버퍼의 구간 합과 평균

- `prefix_build_u16(a, n, P)` — 12비트 ADC 샘플(`uint16_t`, 0..4095)의 누적합을 `uint32_t P[n+1]` 에. `range_sum_u32(P, l, r)` = [l, r) 의 합, `range_mean_u16(P, l, r)` = 반올림 평균(빈 구간은 0, 0 으로 나누지 않기).
- `prefix_build_i32` / `range_sum_i64` — 부호 있는 값(예: 엔코더 증분, 전류 센서의 ±)은 `int64_t` 에 누적. int32 로 누적하면 넘침이 **정의되지 않은 동작(UB)** 이다.
- 테스트: 크기 0..40 은 모든 (l, r) 전수, 그 이상은 랜덤 질의를 uint64 직접 합과 대조. LeetCode 303 예제, int32 극값, 그리고 **1.1 M 개 샘플로 P 가 2^32 를 넘어 감긴 상태**에서도 길이 ≤ 1,048,832 구간이 정확한지(100000 질의), 한 칸 더 길면 틀리는지(3839 가 나옴).

### Part B — 차분 배열: 구간 증가 여러 번, 전력 피크, 감긴 누적합

- `diff_add(D, n, l, r, v)` — [l, r) 에 v 를 더하는 갱신을 O(1) 로 기록 (빈·뒤집힌·범위 밖 구간은 false). `diff_apply(D, n, a)` — 한 번에 반영.
- `power_peak(loads, m, T, buf, &t)` — 부하마다 [t0, t1) ms 동안 mA 를 끄는 타임라인에서 시각별 총 전류와 **최대 전류·처음 시각**. 버퍼 하나로 차분 → 제자리 누적.
- `prefix_build_u8_wrap16` / `range_sum_wrap16` — 8비트 샘플을 **uint16** 누적합에 감아 저장(메모리 절반). 길이 ≤ 257 구간은 항상 정확(255 × 257 = 65535), 258 이면 틀린다.
- 테스트: LeetCode 1109 예제, n = 1..10 의 모든 단일 갱신 전수, 랜덤 400 배열 × 최대 300 갱신을 O(구간 길이) 브루트포스와 대조, 손 계산 타임라인(피크 155 mA @ 4 ms), 랜덤 300 타임라인, 8192 샘플 wrap16 의 길이 257 모든 창.

### Part C — Integral image: 640×480 프레임의 영역 합

- `integral_build(img, w, h, I)` — I 는 (w+1)×(h+1), `I[y][x]` = [0, x) × [0, y) 의 합. 픽셀은 `uint16_t`(8비트 또는 RAW12).
- `rect_sum(I, w, x0, y0, x1, y1)` — [x0, x1) × [y0, y1) 의 합, 4번 읽기.
- `ae_zone_means` — 자동 노출(AE) 측광용 zx × zy 존 평균 (나누어떨어지지 않는 크기 포함). `adaptive_threshold` — 픽셀이 주변 (2r+1)² 창 평균보다 t% 이상 어두우면 0 (Bradley–Roth), 나눗셈 없이.
- 테스트: 7×5 프레임의 모든 직사각형 756개 전수, 작은 랜덤 프레임 60개 × 300, 640×480 랜덤 300개, 전부 255 → 78,336,000, 전부 4095 → 1,257,984,000, 존 합 = 전체 합, 조명 그라데이션 위 획(stroke) 320×240 전 픽셀을 브루트포스와 대조.

속도 비교(직접 더하기 vs 누적합 vs 열 합)와 넘침 표는 `demo_bench.c`, `demo_overflow.c`, Cortex-M4 asm 은 `demo_target.c` (5. 풀이).

## 2. Problem (English)

You are writing the sample-statistics layer of a sensor/camera firmware. Answer many range queries in O(1) after O(n) preprocessing, and apply many range updates in O(1) each.

```c
void     prefix_build_u16(const uint16_t *a, size_t n, uint32_t *P);    /* P has n+1 entries */
uint32_t range_sum_u32(const uint32_t *P, size_t l, size_t r);           /* sum of a[l..r), 0 <= l <= r <= n */
uint16_t range_mean_u16(const uint32_t *P, size_t l, size_t r);          /* rounded mean, 0 for empty */
bool     diff_add(int32_t *D, size_t n, size_t l, size_t r, int32_t v);  /* add v to a[l..r) lazily */
void     diff_apply(const int32_t *D, size_t n, int32_t *a);
int32_t  power_peak(const load_t *L, size_t m, size_t T, int32_t *buf, size_t *t_at);
void     integral_build(const uint16_t *img, size_t w, size_t h, uint32_t *I);   /* (w+1)*(h+1) */
uint32_t rect_sum(const uint32_t *I, size_t w, size_t x0, size_t y0, size_t x1, size_t y1);
```

Constraints: no dynamic allocation; frames up to 640×480 with 8- or 12-bit pixels; justify every integer width you choose.

Examples:

```text
a = [3, 1, 4, 1, 5, 9, 2]      range_sum([2,5)) = 10        range_sum([3,3)) = 0
n = 5, add 10 on [0,2), 20 on [1,3), 25 on [1,5)  ->  [10, 55, 45, 25, 25]
loads (ms, mA): [0,10) 5, [2,5) 120, [4,8) 30, [7,9) 80  ->  peak 155 mA at t = 4 ms
img = [[1,2,3],[4,5,6],[7,8,9]]  rect_sum(x 1..3, y 1..3) = 5+6+8+9 = 28
```

Follow-ups: What if values change between queries? How many bits does the integral image of a 1080p RAW12 frame need? Can you keep it in 32 bits anyway? What if it doesn't fit in SRAM?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열네 군데(Part A 5개, Part B 5개, Part C 4개). 하네스·브루트포스 기준(`bf_*`)·xorshift32 는 주어진다. 스텁은 0 이나 "변화 없음"을 돌려주므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 FAIL 줄 생략):

```text
A: u16 prefix sum/mean vs brute force: 1031693 checks, mismatches=1026740
A: i32 prefix (int64) vs brute force: 40000 queries, mismatches=39887
A: wrapped u32 prefix (n=1100000, total=4397104437 > 2^32): 100000 queries len<=1048832, mismatches=100000
B: difference array vs brute force: 400 arrays (n<=200, <=300 updates), mismatches=398
B: power_peak vs brute force: 300 timelines (T<=2000 ms, <=64 loads), mismatches=287
B: wrap16 prefix of 8-bit samples: 27936 windows (len<=257), mismatches=27855
C: rect_sum vs brute force (small frames): 18756 rects, mismatches=15521
C: 640x480 8-bit frame: 300 random rects, mismatches=297
C: adaptive threshold 320x240 r=7 t=15%: mismatches=13909, dark=0, stroke px=13914, stroke px marked dark=0
6/43 passed
```

(exit 1. 통과한 6개는 스텁의 0/false 가 우연히 맞는 경우 — 빈 배열, 무시돼야 할 잘못된 구간, 폭·높이 0 프레임.)

## 4. 힌트

<details><summary>힌트 1 — P 를 한 칸 더 길게</summary>

`P[0] = 0` 을 두면 "앞에서 0개의 합"이 생겨 l = 0 을 특별 처리할 필요가 없다. [l, r) 의 합 = P[r] − P[l]. 닫힌 구간 [l, r] 로 생각하면 P[r+1] − P[l] — 면접에서는 처음에 어느 쪽인지 말하고 시작한다.

</details>

<details><summary>힌트 2 — 차분 배열은 "시작에서 켜고 끝에서 끈다"</summary>

[l, r) 에 v: `D[l] += v; D[r] -= v;`. 그러면 D 의 누적합은 l 이전엔 0, [l, r) 에선 v, r 부터 다시 0. 여러 갱신은 선형이라 겹쳐도 그냥 더해진다. r = n 이 허용되므로 D 는 n+1 칸.

</details>

<details><summary>힌트 3 — 전력 타임라인은 버퍼 하나로</summary>

buf 를 0 으로 → 부하마다 `diff_add(buf, T, t0, min(t1, T), mA)` → `buf[t] += buf[t-1]` 로 제자리 누적하면서 최대값을 갱신. "처음" 최대 시각이므로 `>` 로 비교(≥ 면 마지막 시각이 된다).

</details>

<details><summary>힌트 4 — 2D 는 "위 칸 + 이 행의 누적"</summary>

포함–배제 점화식 `I[y+1][x+1] = img + I[y][x+1] + I[y+1][x] − I[y][x]` 도 맞지만, 행마다 `row += img[y][x]` 를 들고 `I[y+1][x+1] = I[y][x+1] + row` 로 하면 덧셈 2개로 끝난다. 질의는 `I[y1][x1] − I[y0][x1] − I[y1][x0] + I[y0][x0]`.

</details>

<details><summary>힌트 5 — 넘침과 감김</summary>

부호 없는 덧셈·뺄셈은 C 에서 mod 2^N 으로 **정의된** 동작이다. P 가 감겨도 P[r] − P[l] ≡ 진짜 합 (mod 2^N). 진짜 합이 0 ≤ S &lt; 2^N 이면? 단, `uint16_t − uint16_t` 는 먼저 `int` 로 승격된다는 것을 잊지 말 것.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 질문으로 범위 확인 | 질의 수 q 와 n, 값이 **바뀌는가**(정적이면 누적합, 갱신+질의 섞이면 Fenwick/세그먼트 트리), 구간이 닫힌/반열린, 값의 범위·부호(→ 누산기 비트 수), 메모리 예산(MCU SRAM) |
| 2. 단순 해법 | 질의마다 직접 더하기 O(len) → 전체 O(n·q). 갱신마다 구간을 돌며 더하기 O(len). 2D 직사각형은 O(넓이) |
| 3. 개선 + 불변식 | P[i] = a[0..i) 의 합 (불변식). 질의 = P[r] − P[l]. 갱신이 많으면 쌍대(dual)인 차분 배열. 2D 는 포함–배제 |
| 4. 코드 | 아래 Part A/B/C |
| 5. 소리 내어 말할 테스트 | 빈 배열, 빈 구간 l = r, 전체 구간, r = n, 길이 1, 모든 값 최대(넘침 경계), 음수, 프레임 테두리의 잘린 창 |
| 6. 복잡도 | 만들기 O(n) / O(wh), 질의 O(1), 추가 메모리 n+1 / (w+1)(h+1) 워드, 재귀 없음. **비트 수**: 최대 합 = n·max → ⌈log2(n·max + 1)⌉ |

### Part A — 누적합: 불변식과 정확성

**정의(불변식).** P[0] = 0, P[i+1] = P[i] + a[i]. 귀납법으로 P[i] = Σ_{k&lt;i} a[k] (i = 0 이면 빈 합 0, i → i+1 은 점화식 그대로). 따라서 l ≤ r 에 대해

  P[r] − P[l] = Σ_{k&lt;r} a[k] − Σ_{k&lt;l} a[k] = Σ_{l≤k&lt;r} a[k].

반열린 구간을 쓰는 이유: 길이 = r − l, 빈 구간 l = r 이 자연스럽게 0, 이어 붙이기 [l, m) + [m, r) = [l, r) 가 그대로 성립(P[m] 이 상쇄). 닫힌 구간 [l, r] 이면 P[r+1] − P[l] 이 되고, r = n−1 일 때 P[n] 을 읽으므로 **P 가 n+1 칸이어야 하는 이유는 같다.**

**트레이스** (a = [3, 1, 4, 1, 5, 9, 2]):

| i | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| a[i] | 3 | 1 | 4 | 1 | 5 | 9 | 2 | — |
| P[i] | 0 | 3 | 4 | 8 | 9 | 14 | 23 | 25 |

질의 [2, 5) = 14 − 4 = 10, [0, 7) = 25, [3, 3) = 0.

**평균.** `(합 + len/2) / len` 은 .5 를 올리는 반올림. 정수 나눗셈이므로 Cortex-M0(나눗셈 명령 없음)에서는 `__aeabi_uidiv` 호출이 된다 — 창 길이가 2의 거듭제곱이면 시프트로 바꾸는 게 펌웨어 관례다(ALGO-B10 의 이동 평균, ALGO-M15 의 상수 나눗셈). 합에 len/2 를 더하는 반올림에도 넘침이 숨어 있다. 12비트 샘플이면 합 자체는 len ≤ 1,048,832 까지 uint32 에 들어가지만, len·4095 + ⌊len/2⌋ ≤ 2^32 − 1 은 len ≤ 1,048,704 까지만 성립한다(이 레슨에서 계산). 즉 **합은 맞는데 반올림 덧셈이 넘치는 창 길이 128개**가 있다. 그래서 `range_mean_u16` 은 덧셈만 uint64 로 한다. 테스트: 전부 4095 인 길이 1,048,832 창의 평균 = 4095 (uint32 로 더하면 4,295,491,456 − 2^32 = 524,160 → ÷ len = 0 이 나온다).

**부호 있는 값.** int32 값을 int32 P 에 누적하면 합이 2^31 을 넘는 순간 **UB** 다(C11 6.5p5). 결과가 "우연히 맞아 보여도" UB 다 — 이 레슨 작업 중 `{2e9, 2e9, −1e9}` 를 int32 로 누적해 UBSan 으로 돌린 실제 출력:

```text
trap.c:9:49: runtime error: signed integer overflow: 2000000000 + 2000000000 cannot be represented in type 'int32_t' (aka 'int')
trap.c:10:68: runtime error: signed integer overflow: -1294967296 - 2000000000 cannot be represented in type 'int32_t' (aka 'int')
int32 prefix P[3]-P[1] = 1000000000 (true 1000000000)
```

답은 맞게 나왔지만 컴파일러는 "넘치지 않는다"고 가정해 최적화할 자유가 있다. 해결: int64 에 누적(|P| ≤ n·2^31 &lt; 2^63 ⇔ n &lt; 2^32), 또는 uint32 에서 감기게 계산하고 마지막에 해석(아래 Part B 정리).

### Part B — 차분 배열: 누적합의 역연산

**정의.** 배열 a 의 차분 D[0] = a[0], D[i] = a[i] − a[i−1] (1 ≤ i &lt; n). 그러면 D 의 누적합은 망원 합(telescoping)으로 a[i] 가 된다. 즉 **누적 ∘ 차분 = 항등**. 이 관계 때문에 "a 의 구간 [l, r) 에 v 를 더하기"는 D 에서 두 칸만 바뀐다: a[l] − a[l−1] 이 v 늘고(D[l] += v), a[r] − a[r−1] 이 v 준다(D[r] −= v). 그 사이의 이웃 차는 둘 다 v 씩 늘어 그대로다.

**여러 갱신의 정확성.** 갱신 j = (l_j, r_j, v_j) 들을 모두 기록한 뒤 누적하면

  Σ_{k≤i} D[k] = Σ_j v_j · ([l_j ≤ i] − [r_j ≤ i]) = Σ_j v_j · [l_j ≤ i &lt; r_j]

(l_j &lt; r_j 이므로 [l_j ≤ i] − [r_j ≤ i] 는 i ∈ [l_j, r_j) 일 때 1, 아니면 0). 정확히 "i 를 덮는 갱신들의 v 합"이다. 덧셈이 교환·결합적이라 **갱신 순서는 상관없다.** 이 레슨의 `diff_apply` 는 D 를 0 에서 시작한 "증가분만의 차분"으로 쓰고 결과를 기존 a 에 더한다.

**트레이스** — LeetCode 1109 Corporate Flight Bookings 예제 (1-기준 포함 구간을 [l−1, r) 로 바꿈):

| 갱신 | D[0] | D[1] | D[2] | D[3] | D[4] | D[5] |
|---|---|---|---|---|---|---|
| 처음 | 0 | 0 | 0 | 0 | 0 | 0 |
| [0, 2) +10 | 10 | 0 | −10 | 0 | 0 | 0 |
| [1, 3) +20 | 10 | 20 | −10 | −20 | 0 | 0 |
| [1, 5) +25 | 10 | 45 | −10 | −20 | 0 | −25 |
| 누적 (i &lt; 5) | **10** | **55** | **45** | **25** | **25** | (0) |

D[5] 은 "배열 밖의 끝"이라 출력엔 안 쓰이지만, 이 칸이 없으면 r = n 갱신이 범위 밖 쓰기가 된다. D 의 마지막 누적(여기선 10+45−10−20+0−25 = 0)은 항상 0 — 모든 갱신이 켜진 만큼 꺼졌다는 검산이다.

**전력 타임라인.** 부하 [t0, t1) 동안 mA. MCU 5 mA 상시 [0, 10), 무선 TX 120 mA [2, 5), IMU 30 mA [4, 8), 모터 80 mA [7, 9):

| t (ms) | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| 차분 buf[t] | +5 | 0 | +120 | 0 | +30 | −120 | 0 | +80 | −30 | −80 |
| 누적 = 전류 | 5 | 5 | 125 | 125 | **155** | 35 | 35 | 115 | 85 | 5 |

(buf[10] = −5 는 끝 칸.) 피크 155 mA @ 4 ms — TX 와 IMU 가 겹친 1 ms. LDO/배터리의 피크 전류 예산(SYSD-B16)이나 전원 레일의 부하 과도(HWLAB-M02)를 따질 때 바로 이 숫자가 필요하다. 시간 해상도 1 ms × T 칸 대신 부하 시작·끝 사건 2m 개를 정렬해 쓸어가면(sweep) O(m log m) 이고 T 와 무관하다 — 그건 ALGO-M09(겹침 수) / ALGO-A09(스윕 라인)의 영역이라 여기선 언급만 한다. 차분 배열은 **T 가 작고 고정된 격자**일 때 정렬 없이 O(m + T) 인 것이 장점이다.

**제자리(in-place) 누적.** `buf[t] += buf[t−1]` 은 t 를 오름차순으로 돌 때 buf[t−1] 이 이미 "누적된 값"이라 맞다(내림차순이면 틀린다). 차분 버퍼와 출력 버퍼를 하나로 써서 T+1 워드만 든다.

### Part B — 감긴(wrapped) 누적합도 정확한 이유

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">uint16 누적합은 원 위를 돈다 (둘레 65536)</text>
  <circle cx="150" cy="160" r="105" fill="none" stroke="currentColor"/>
  <path d="M 150 55 A 105 105 0 0 1 216 78" fill="none" stroke="currentColor" stroke-width="6"/>
  <path d="M 129 57 A 105 105 0 0 1 150 55" fill="none" stroke="currentColor" stroke-width="6"/>
  <circle cx="129" cy="57" r="5" class="f-warn" stroke="currentColor"/>
  <circle cx="216" cy="78" r="5" class="f-ok" stroke="currentColor"/>
  <text x="140" y="45" fill="currentColor" font-size="12">0</text>
  <text x="40" y="52" fill="currentColor" font-size="12">P[l] = 65000</text>
  <text x="222" y="74" fill="currentColor" font-size="12">P[r] = 300</text>
  <text x="95" y="165" fill="currentColor" font-size="12">한 바퀴 = 2^16</text>
  <text x="70" y="185" fill="currentColor" font-size="12">호의 길이 = 진짜 합 S</text>
  <g fill="currentColor">
    <text x="300" y="56">진짜 합 S = 836 (P[l] 에서 0 을 지나 P[r] 까지)</text>
    <text x="300" y="82" font-family="monospace">(uint16_t)(300 − 65000) = 836   ✓</text>
    <text x="300" y="104" font-family="monospace">int d = 300 − 65000      = −64700 ✗</text>
    <text x="300" y="126">uint16 끼리 빼면 먼저 int 로 승격 → 다시 잘라야 한다</text>
    <text x="300" y="164" font-weight="bold">정리</text>
    <text x="300" y="186">저장값 p[i] = P[i] mod 2^w. 그러면</text>
    <text x="300" y="206" font-family="monospace">p[r] − p[l] ≡ P[r] − P[l] = S  (mod 2^w)</text>
    <text x="300" y="228">0 ≤ S &lt; 2^w 이면 mod 2^w 결과가 곧 S (유일한 대표)</text>
    <text x="300" y="250">S ≥ 2^w 이면 호가 한 바퀴를 넘어 S − 2^w 등으로 보인다</text>
    <text x="300" y="276">8비트 값: S ≤ 255·len &lt; 65536 ⇔ len ≤ 257</text>
  </g>
</svg>
```

**정리(감김 규칙).** w 비트 부호 없는 산술에서 저장값 p[i] = P[i] mod 2^w (C 의 부호 없는 덧셈은 정의상 mod 2^w — C11 6.2.5p9). 환 준동형 ℤ → ℤ/2^wℤ 이 덧셈·뺄셈을 보존하므로 (p[r] − p[l]) mod 2^w = (P[r] − P[l]) mod 2^w = S mod 2^w. 0 ≤ S &lt; 2^w 이면 [0, 2^w) 안의 대표가 유일하므로 결과 = S. ∎

같은 논증이 **2D 포함–배제**에도 그대로 간다: 네 항의 ± 결합도 준동형으로 보존되므로, 직사각형의 진짜 합이 2^w 미만이면 I 가 몇 번 감겼든 결과가 정확하다. 이것이 Part C 의 `rect_sum` 이 중간에 "음수"가 되어도(uint32 에서 감겨도) 괜찮은 이유다. 부호 있는 값이라면 −2^(w−1) ≤ S &lt; 2^(w−1) 일 때 2의 보수로 해석하면 된다 (uint → int 변환이 범위 밖이면 C11 에선 구현 정의 동작 — GCC/Clang 은 mod 2^w).

**경계가 빡빡한지 실측.** 전부 최대값인 입력에서 "처음 틀리는 길이"를 직접 세면 정확히 kmax + 1 이다 (`demo_overflow.c`, 실제 출력):

```text
value      acc        formula kmax    first wrong          check
8-bit      uint16              257            258      kmax+1 OK
12-bit     uint16               16             17      kmax+1 OK
8-bit      uint32         16843009       16843010      kmax+1 OK
12-bit     uint32          1048832        1048833      kmax+1 OK
16-bit     uint32            65537          65538      kmax+1 OK
8-bit^2    uint32            66051          66052      kmax+1 OK
12-bit^2   uint32              256            257      kmax+1 OK
```

solution.c 의 해당 테스트: 8192 개 8비트 샘플(총합 약 1 M, uint16 을 16번 넘게 감음)에서 길이 257 의 **모든** 시작점 + 랜덤 길이 ≤ 257 → 27936 창 불일치 0. 전부 255 일 때 길이 257 → 65535(정확), 길이 258 → 254(= 65790 mod 65536, 틀림). 1D uint32 쪽은 12비트 샘플 1.1 M 개(총합 4,397,104,437 > 2^32)에서 길이 ≤ 1,048,832 질의 100000개 불일치 0, 전부 4095 에서 길이 1,048,833 → 3839.

펌웨어에서 이 규칙의 쓸모: **메모리 절반**(8비트 샘플·창 ≤ 257 이면 uint16 누적합), 그리고 **무한 스트림**(1 MS/s 12비트 ADC 의 uint32 누적합은 약 1.049 초마다 감기지만, 1.049 초 이하 창의 합은 계속 정확 — 하드웨어 타이머 카운터를 `now − start` 로 빼는 것과 같은 원리).

### Part C — Integral image: 점화식, 포함–배제, 트레이스

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">직사각형 [x0, x1) × [y0, y1) = D − B − C + A</text>
  <rect x="40" y="40" width="300" height="230" fill="none" stroke="currentColor"/>
  <rect x="40" y="40" width="120" height="90" class="f-mut" stroke="currentColor"/>
  <rect x="160" y="40" width="140" height="90" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="40" y="130" width="120" height="100" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="160" y="130" width="140" height="100" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="70" y="90">위·왼 (A)</text>
    <text x="200" y="90">위만</text>
    <text x="70" y="185">왼쪽만</text>
    <text x="190" y="185">구하려는 합</text>
    <text x="150" y="128" font-size="12">A</text><text x="294" y="128" font-size="12">B</text>
    <text x="150" y="244" font-size="12">C</text><text x="294" y="244" font-size="12">D</text>
  </g>
  <circle cx="160" cy="130" r="4" class="f-warn" stroke="currentColor"/><circle cx="300" cy="130" r="4" class="f-warn" stroke="currentColor"/>
  <circle cx="160" cy="230" r="4" class="f-warn" stroke="currentColor"/><circle cx="300" cy="230" r="4" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="370" y="48" font-family="monospace">A = I[y0][x0]  B = I[y0][x1]</text>
    <text x="370" y="66" font-family="monospace">C = I[y1][x0]  D = I[y1][x1]</text>
    <text x="370" y="88">D = 원점부터 (x1, y1) 까지 전체</text>
    <text x="370" y="106">B·C 가 위·왼 블록 A 를 두 번 뺌 → +A</text>
    <text x="370" y="136" font-weight="bold">3×3 예: img = 1..9</text>
    <text x="370" y="156" font-family="monospace">I = 0  0  0  0</text>
    <text x="370" y="174" font-family="monospace">    0  1  3  6</text>
    <text x="370" y="192" font-family="monospace">    0  5 12 21</text>
    <text x="370" y="210" font-family="monospace">    0 12 27 45</text>
    <text x="370" y="236">[1,3)×[1,3) = 45 − 6 − 12 + 1 = 28</text>
    <text x="370" y="256">= 5 + 6 + 8 + 9 ✓</text>
  </g>
  <text x="10" y="295" fill="currentColor">0 행·0 열은 0 (P[0] 의 2D 판) → x0 = 0 이나 y0 = 0 인 직사각형도 특별 처리 없이 같은 식</text>
</svg>
```

**정의와 점화식.** I[y][x] = Σ_{y'&lt;y, x'&lt;x} img[y'][x']. 행 y 의 앞부분 합 row_y(x) = Σ_{x'≤x} img[y][x'] 를 쓰면 I[y+1][x+1] = I[y][x+1] + row_y(x) — "위쪽 행들의 같은 열까지 + 이번 행의 이 열까지". 귀납법으로 정의를 만족한다(y 에 대한 귀납, 기저는 0 행 = 0). 이 형태는 픽셀당 덧셈 2개, 위 행 하나만 읽어서 **행 단위 스트리밍**에 잘 맞는다(포함–배제형 점화식은 덧셈 3개).

**질의의 정확성.** 영역을 [0, x1) × [0, y1) 로 놓고 지시함수로 쓰면 [x0 ≤ x &lt; x1][y0 ≤ y &lt; y1] = ([x &lt; x1] − [x &lt; x0])([y &lt; y1] − [y &lt; y0]). 전개하면 네 항이 정확히 D − B − C + A 다. 1D 의 P[r] − P[l] 을 두 축에 곱한 것이다.

**행별 트레이스** (3×3, img 행 = [1 2 3], [4 5 6], [7 8 9]):

| y | 행 누적 row | I[y][1..3] (위) | I[y+1][1..3] = 위 + row |
|---|---|---|---|
| 0 | 1, 3, 6 | 0, 0, 0 | 1, 3, 6 |
| 1 | 4, 9, 15 | 1, 3, 6 | 5, 12, 21 |
| 2 | 7, 15, 24 | 5, 12, 21 | 12, 27, 45 |

**넘침 분석 (데이터형 고르기).** 최대 합 = W·H·(2^b − 1). `demo_overflow.c` 실제 출력:

```text
1) integral image max = W*H*(2^b-1): bits needed (uint32 ok?)
frame \ pixel      8-bit             10-bit             12-bit             16-bit           
320x240 QVGA           19584000 25b        78566400 27b       314496000 29b      5033088000 33b*
640x480 VGA            78336000 27b       314265600 29b      1257984000 31b     20132352000 35b*
1280x720 HD           235008000 28b       942796800 30b      3773952000 32b     60397056000 36b*
1920x1080 FHD         528768000 29b      2121292800 31b      8491392000 33b*   135893376000 37b*
4096x3072 12MP       3208642560 32b     12872318976 34b*    51527024640 36b*   824621137920 40b*
   (* = full-frame sum does not fit uint32; integral of squares adds b more bits)
   640x480 8-bit sum of squares = 19975680000 -> 35 bits
```

결론: **640×480 은 8비트(27비트)든 RAW12(31비트)든 uint32 로 전 프레임 합까지 정확**하다 — 테스트가 전부 255 → 78,336,000, 전부 4095 → 1,257,984,000 을 확인한다. 1080p RAW12(33비트)는 전체 합이 안 들어가지만, 감김 규칙 덕분에 **창 넓이 ≤ 1,048,832 픽셀**(예: 1024×1024)인 질의는 uint32 integral 로도 정확하다. 분산(Viola–Jones 정규화, 국소 대비)에 쓰는 **제곱의 integral** 은 640×480 8비트에서도 35비트라 uint32 를 넘는다 → uint64 이거나, 창 ≤ 66051 픽셀(≈ 257×257)이면 uint32 감김으로 충분 (연습 5).

**AE 존 평균.** 존 경계 x_i = ⌊i·w/zx⌋ 는 i = 0 → 0, i = zx → w 이고 단조라서, 나누어떨어지지 않아도 존들이 프레임을 **겹침 없이 빈틈없이** 덮는다(테스트: 존 합의 합 = 전체 합, 100×75 를 7×5 로). 존이 픽셀보다 많으면 넓이 0 인 존이 생기므로 0 으로 나누지 않게 0 을 낸다. 존 48개 × (4번 읽기 + 나눗셈 1번)이라 프레임당 비용은 무시할 수준 — 실제 카메라 ISP 는 이런 통계(AE/AWB 그리드)를 하드웨어 블록이 만들어 주는 경우가 많고, 펌웨어는 결과 그리드를 읽어 노출 제어 루프(CAM-M06)를 돈다.

**적응 이진화(Bradley–Roth).** 조명이 한쪽으로 밝아지는 프레임에서 전역 임계값(CV-B08 의 Otsu 등)은 어두운 쪽을 통째로 검게 만든다. 픽셀마다 주변 창 평균과 비교하면 조명 변화가 상쇄된다. 판정 `img < mean·(100 − t)/100` 을 나눗셈 없이 양변에 cnt·100 을 곱해 `img·cnt·100 <= sum·(100 − t)` 로 바꿨다 (식은 원 논문의 "≤ 이면 검정" 관례를 따름 — 세부 부등호 방향은 확인 권장). 곱의 크기: 4095 × (31·31) × 100 ≈ 3.9 × 10^8 이라 uint32 에도 들어가지만, r 이 커지면 넘으니 uint64 로 계산했다. 테두리에선 창을 잘라(clamp) 실제 픽셀 수 cnt 로 비교한다. 320×240, r = 7, t = 15% 결과: 전 픽셀 브루트포스와 불일치 0, 획 픽셀 13914 중 13909 를 검게 판정(놓친 5개는 판정식 경계 근처 — 원인은 따로 분석하지 않음), 배경을 검게 판정한 픽셀 0.

### 속도 — `demo_bench.c` (이 맥, Apple Silicon M 시리즈, -O2)

워밍업 1회 + 3~15회 중앙값, 결과 체크섬으로 세 방법이 같은지 확인. 다른 작업이 같이 돌던 상태라 실행마다 ±50% 흔들렸다 — **비율만** 볼 것. 실제 출력(한 번 실행):

```text
1D: n=1048576 samples, 65536 random range queries (avg len ~n/3)
   naive sum per query     :     871.85 ms
   prefix build + queries  :       0.63 ms   (speedup x1377, results equal)
integral image build 640x480: 0.170 ms = 0.55 ns/pixel, table 1233284 bytes
box sum over all pixels (640x480), clamped (2r+1)^2 window:
     r   window     naive ms    integral ms      colsum ms    equal
     1     3x3           1.47          0.461          0.194      yes
     3     7x7          13.95          0.436          0.283      yes
     7    15x15         46.66          0.444          0.214      yes
    15    31x31        150.75          0.419          0.344      yes
    31    63x63        620.21          0.443          0.427      yes
extra memory: integral 1233284 B (uint32 (W+1)(H+1)), colsum 2560 B (uint32 W) + 2r+2 input rows if streaming
```

- 1D: 질의 65536 개 × 평균 길이 약 n/3 을 직접 더하면 약 2.3 × 10^10 번 덧셈이고, 누적합은 만들기 10^6 + 질의 65536 이다. 천 배 이상.
- 박스 합: 직접 더하기는 창 넓이 (2r+1)² 에 비례(r = 1 → 31 에서 9 → 3969, 441배; 측정 1.47 → 620 ms, 약 420배). integral image 와 열 합은 **r 과 무관**하다.
- 열 합(column sum, 연습 4)이 integral 보다 빠른 것은 integral 표(1.2 MB)를 쓰고 다시 4군데에서 읽는 대신 W 칸 배열만 순차로 건드리기 때문이다. 메모리도 2560 B (+ 스트리밍이면 입력 2r+2 줄).

**MCU 메모리 현실.** 640×480 uint32 integral = 1,233,284 B. STM32F4 계열의 SRAM 은 수백 KB 수준, H7 계열도 총 1 MB 안팎이라(칩마다 다름 — 확인 권장) 프레임 하나(8비트 300 KB)도 빠듯하다. 그래서 MCU 비전 코드는 (1) 박스 필터는 **줄 버퍼 + 열 합**(카메라 DCMI/DMA 가 한 줄씩 주는 흐름과 맞음, CAM-M09), (2) 임의 직사각형 질의가 꼭 필요하면 타일 단위 integral 이나 창 ≤ 257 픽셀이면 uint16 감김 integral(절반), (3) 큰 해상도는 ISP 하드웨어 통계에 맡긴다.

### Cortex-M4 에서 본 루프 — `demo_target.c`

`clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S` 실제 출력(발췌). uint32 누적 (4개씩 펼침, 원소당 `ldrh` + `add` + `str`):

```text
.LBB0_4:
	ldrh.w	r6, [r0, r1, lsl #1]
	...
	add	r5, r6
	add.w	r6, r2, r1, lsl #2
	str	r5, [r6, #4]
	...
```

int64 누적은 원소마다 `adds` + `adc.w ... asr #31`(상위 워드에 부호 확장된 자리올림) + `str` 두 번 — 레지스터 2개, 저장 2배:

```text
	ldr	r7, [r1, #16]!
	adds	r5, r5, r7
	adc.w	r6, r6, r7, asr #31
	str	r5, [r3, #-24]
	str	r6, [r3, #-20]
```

`rect_sum` 은 `mul` 2개(행 오프셋) + `ldr.w` 4개 + 덧셈·뺄셈 3개; 컴파일러가 `D − B − C + A` 를 `D − (B + C) + A` 로 묶었다 — mod 2^32 에서 같은 값이라 자유롭게 바꿀 수 있다(부호 있는 정수였다면 UB 가능성 때문에 이런 재배치가 덜 자유롭다). 32비트로 충분한 곳에 64비트를 쓰지 않는 것이 메모리·대역폭 모두에서 이득이다.

### 복잡도

| 함수 | 시간 | 추가 메모리 | 메모 |
|---|---|---|---|
| prefix_build / range_sum | O(n) / O(1) | P: n+1 워드 | 정적 데이터. 값이 바뀌면 다시 만들기 O(n) |
| diff_add / diff_apply | O(1) / O(n) | D: n+1 워드 | 갱신 중엔 값을 못 읽는다(읽으려면 Fenwick) |
| power_peak | O(m + T) | T+1 워드(버퍼 하나) | 정렬 없음, T 는 격자 칸 수 |
| integral_build / rect_sum | O(wh) / O(1) | (w+1)(h+1) 워드 | 640×480 → 1.2 MB |
| ae_zone_means | O(zx·zy) | 0 | 나눗셈 zx·zy 번 |
| adaptive_threshold | O(wh) | 0 (I 재사용) | r 과 무관 |

재귀 없음, 스택은 지역 변수 몇 개. 최악 = 평균(입력 값과 무관하게 같은 연산 수) → 실시간 코드에 넣기 좋다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 43/43 passed, exit 0, 0.1 초 미만
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_overflow.c -o demo_overflow && ./demo_overflow
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench          # 약 7 초 (직접 더하기가 느림)
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -    # asm 만
```

solution 의 실제 요약 줄 (발췌 — 나머지 줄도 전부 mismatches=0):

```text
A: wrapped u32 prefix (n=1100000, total=4397104437 > 2^32): 100000 queries len<=1048832, mismatches=0
B: difference array vs brute force: 400 arrays (n<=200, <=300 updates), mismatches=0
B: wrap16 prefix of 8-bit samples: 27936 windows (len<=257), mismatches=0
C: rect_sum vs brute force (small frames): 18756 rects, mismatches=0
C: adaptive threshold 320x240 r=7 t=15%: mismatches=0, dark=13909, stroke px=13914, stroke px marked dark=13909
43/43 passed
```

## 7. 자주 하는 실수

**1. P 를 n 칸으로 만든다.** `P[i] = a[0..i]`(포함)로 정의하면 l = 0 질의가 `P[r] − P[−1]` 이 되어 `if (l == 0)` 분기가 생기고, 빈 구간을 표현할 수 없다. 고친 코드: P 를 n+1 칸, `P[0] = 0`, [l, r) = `P[r] − P[l]`. 차분 배열도 같다 — D 를 n 칸만 잡으면 r = n 갱신의 `D[n] −= v` 가 범위 밖 쓰기다(바로 옆 변수를 조용히 망가뜨리는, 펌웨어에서 가장 찾기 힘든 종류의 버그).

**2. uint16 누적합의 차를 int 로 쓴다.** `uint16_t` 두 개를 빼면 int 로 승격되어 음수가 나온다. 이 레슨 작업 중 실제 출력:

```text
int d = -64700, (uint16_t) = 836, (P16[r]-P16[l] > 1000) -> 0
```

함수 반환형이 `uint16_t` 면 반환할 때 잘려서 우연히 맞지만, `if (P16[r] - P16[l] > thr)` 처럼 식 안에서 쓰면 틀린다. 고친 코드: `(uint16_t)(P16[r] - P16[l])`. uint32 끼리는 승격이 없어서(int 가 32비트인 플랫폼) 이 함정이 없다 — 8/16비트 MCU(AVR, int = 16비트)에서는 uint16 이 unsigned int 로 가므로 동작이 또 다르다.

**3. int32 로 누적한다.** 합이 2^31 을 넘으면 UB (5. 풀이의 UBSan 출력). "2의 보수라 감기겠지"는 표준이 보장하지 않는다. 고친 코드: int64 누적, 또는 uint32 로 감기게 계산.

**4. 2D 질의에서 A 를 안 더한다 / 좌표를 섞는다.** `D − B − C` 만 하면 위·왼 블록이 두 번 빠진다. 또 I 의 폭은 w 가 아니라 **w+1** 이다 — `I[y * w + x]` 로 인덱싱하면 행이 한 칸씩 밀려 틀린다 — 이 레슨 작업 중 `rect_sum` 의 W 만 w 로 바꿔 돌리면 작은 프레임 직사각형 18756개 중 15520개, 640×480 랜덤 300개 중 297개가 틀렸다. 고친 코드: `W = w + 1` 을 지역 변수로 두고 한 곳에서만 쓴다.

**5. 제자리 누적을 거꾸로 돈다.** `for (t = T−1; t > 0; t−−) buf[t] += buf[t−1];` 은 buf[t−1] 이 아직 차분 값이라 "이웃 차의 합 두 개"만 더해진다. 누적합은 반드시 오름차순. (반대로 1D 0/1 배낭의 역순 루프는 ALGO-M08 — 방향이 의미를 바꾸는 같은 종류의 함정.)

**6. 반올림 덧셈의 넘침을 잊는다.** `(sum + len/2) / len` 에서 sum 이 uint32 상한 근처면 `+ len/2` 가 넘친다. 12비트 샘플이면 len ≤ 1,048,704 까지만 안전(len·4095 + ⌊len/2⌋ ≤ 2^32 − 1). 전부 4095 인 길이 1,048,832 창에서 uint32 로 더하면 평균이 4095 대신 0 이 된다(테스트가 이 경우를 확인). 고친 코드: `((uint64_t)sum + len/2) / len`. 같은 이유로 Bradley 식 `img·cnt·100` 도 uint64.

**7. float 로 누적합을 만든다.** float 는 가수 24비트라 합이 2^24 = 16,777,216 을 넘으면 정수도 정확히 못 담는다. 큰 P 두 개를 빼면 앞자리가 상쇄돼 상대 오차가 커진다(파국적 상쇄, catastrophic cancellation). 640×480 8비트 integral(최대 7.8 × 10^7)은 이미 2^24 를 넘는다. 정수 데이터는 정수 누적합, 실수 데이터면 double 이나 블록별 누적.

## 8. 면접 Q&A

<details><summary>Q1. 값이 중간에 바뀌면 누적합은 어떻게 하나요?</summary>

누적합은 **정적(static)** 데이터용이다: 값 하나 바뀌면 그 뒤 P 가 전부 바뀌어 O(n). 질의·갱신 비율로 고른다.

| 구조 | 점 갱신 | 구간 합 질의 | 구간 증가 |
|---|---|---|---|
| 누적합 | O(n) | O(1) | O(n) |
| 차분 배열 | — | 복원 O(n) 후 | O(1) |
| Fenwick 트리(BIT) | O(log n) | O(log n) | 두 개 쓰면 O(log n) |
| 세그먼트 트리 | O(log n) | O(log n) | 지연 전파로 O(log n) |

펌웨어에서는 "1초 버퍼를 다 채운 뒤 분석"처럼 쓰기 단계와 읽기 단계가 나뉘는 경우가 많아서 누적합/차분 배열로 충분한 경우가 대부분이다. 실시간 이동 창이면 링 버퍼 + 누적 합(ALGO-B10)이 O(1)/샘플이다.

</details>

<details><summary>Q2. 1080p RAW12 프레임의 integral image 는 몇 비트가 필요하죠? 32비트로 버틸 수 있나요?</summary>

1920·1080·4095 = 8,491,392,000 → 33비트(demo_overflow 표). 전체 합은 uint32 에 안 들어간다. 하지만 부호 없는 산술은 mod 2^32 이고 포함–배제는 환 준동형으로 보존되므로, **진짜 합이 2^32 미만인 직사각형**(넓이 ≤ ⌊(2^32 − 1)/4095⌋ = 1,048,832 픽셀)은 감긴 uint32 integral 로도 정확하다 — 증명은 5. 풀이, 경계는 실측으로 kmax + 1 에서 처음 틀림. 전체 프레임 합이 필요하면 그것만 uint64 로 따로 더하면 된다. 이 질문의 핵심은 "uint64 로 하면 돼요"보다 **필요 비트 수를 계산하고, 질의 크기 상한으로 32비트를 정당화**하는 것이다.

</details>

<details><summary>Q3. 합이 정확히 k 인 부분 배열 개수는요? (음수 포함)</summary>

LeetCode 560 Subarray Sum Equals K. 음수가 있으면 창을 늘리면 합이 커진다는 단조성이 깨져 슬라이딩 윈도(ALGO-B10)가 안 된다. 누적합으로 바꾸면 "P[r] − P[l] = k 인 쌍 (l &lt; r) 의 개수" → r 을 훑으며 지금까지 나온 P 값의 **개수**를 해시에 세고 `count += cnt[P − k]`. 평균 O(n), 최악(해시 충돌) O(n²). 펌웨어 판은 정적 배열 + 선형 탐사 해시(연습 1), 값 범위가 작으면 해시 대신 오프셋 배열. 0 으로 시작하는 `cnt[0] = 1` 을 잊으면 l = 0 인 부분 배열을 놓친다.

</details>

<details><summary>Q4. 640×480 integral image 는 MCU 에 안 들어가는데요?</summary>

1.2 MB(uint32). 선택지: (1) 박스 필터/이동 평균처럼 **창이 고정**이면 integral 이 필요 없다 — 열 합(W 칸) + 행 방향 이동 합, 입력은 2r+2 줄 링 버퍼(연습 4). 결과는 integral 과 같고 메모리는 수 KB. (2) 임의 직사각형 질의가 필요하지만 크기 상한이 작으면 uint16 감김 integral(8비트·넓이 ≤ 257) 로 절반. (3) 타일 단위로 integral 을 만들고 타일 경계는 두 타일 질의를 더하기. (4) 통계는 ISP 하드웨어에 맡김(AE/AWB 그리드). 그리고 프레임 자체(8비트 300 KB)를 둘 곳부터 따져야 한다 — 외부 SDRAM 이면 integral 의 랜덤 4번 읽기가 캐시 미스로 비싸다.

</details>

<details><summary>Q5. 차분 배열의 정확성을 증명해 보세요. 갱신 순서는 상관없나요?</summary>

누적 ∘ 차분 = 항등(망원 합). 갱신 (l, r, v) 는 D[l] += v, D[r] −= v 이고, 누적하면 i 에서 v·([l ≤ i] − [r ≤ i]) = v·[l ≤ i &lt; r]. 여러 갱신은 선형으로 합쳐져 Σ_j v_j [l_j ≤ i &lt; r_j] — "i 를 덮는 갱신의 v 합". 정수 덧셈은 교환·결합적이라 순서와 무관. 단 넘침이 있으면 얘기가 다르다: int32 D 의 중간값이 순서에 따라 넘칠 수 있다(UB). D 를 uint32 로 두면 순서와 무관하게 mod 2^32 로 정확하고, 최종값이 int32 범위면 해석만 하면 된다.

</details>

<details><summary>Q6. 박스 필터를 r 과 무관하게 빠르게 하는 방법 두 가지를 비교해 보세요.</summary>

integral image: 전처리 O(wh), 픽셀당 4번 읽기, **임의 크기·위치 직사각형**을 O(1) 로 → 크기가 다른 창 여러 개(Viola–Jones 의 Haar 특징)에 강하다. 메모리 (w+1)(h+1) 워드, 4번 읽기가 멀리 떨어진 두 행이라 캐시엔 덜 친화적. 열 합(running sum, 분리 가능): 창 크기 고정, 픽셀당 덧셈·뺄셈 몇 개, 메모리 O(w), 순차 접근이라 스트리밍·DMA·SIMD 친화적. 이 맥 실측(640×480): r = 31 에서 직접 620 ms, integral 0.44 ms, 열 합 0.43 ms; r = 7 에서 0.44 vs 0.21 ms. 둘 다 정수 누적이라 float 오차가 없다. 가우시안처럼 가중치가 있는 필터는 박스 필터를 몇 번 반복해 근사하기도 한다(CV-B04).

</details>

<details><summary>Q7. ISR 에서 샘플이 계속 들어오는데 "최근 1초 평균"을 아무 때나 묻는다면?</summary>

전체 누적합을 다시 만들 수는 없으니 **실행 누적합 하나**(uint32, 감김 허용)와 샘플 개수를 유지하고, 링 버퍼에 몇 ms 간격으로 누적합 스냅숏을 남긴다. 평균 = (지금 누적 − 1초 전 스냅숏) / 개수. 감김 규칙 덕분에 1초 창의 진짜 합이 2^32 미만이면(12비트·1 MS/s 면 1,048,832 샘플까지) 누적합이 몇 번 감겼든 맞다 — 타이머 카운터의 `now − start` 와 같다. ISR 이 32비트 누적합을 쓰고 태스크가 읽는 것은 Cortex-M 에서 정렬된 32비트 접근이 한 번에 일어나므로 찢어지지 않지만, 누적합과 개수 **두 값**의 일관성은 시퀀스 카운터나 짧은 임계 구역으로 맞춘다(C-M14, DS-A02).

</details>

## 9. 추가 연습

### 연습 1 — 합이 K 인 부분 배열 개수 (LeetCode 560)

`uint64_t subarray_sum_k(const int32_t *a, size_t n, int64_t k)`. 엔코더 증분처럼 음수가 섞인 스트림에서 "정확히 k 틱 움직인 연속 구간 수". malloc 없이 정적 해시 테이블로.

<details><summary>답 보기</summary>

```c
h_inc(0);                             /* P[0] = 0 */
for (size_t r = 0; r < n; r++) {
    P += a[r];                        /* int64 누적 */
    count += h_get(P - k);            /* 조회만 — 삽입하지 않는다 */
    h_inc(P);
}
```

해시는 용량 8192(2의 거듭제곱)의 선형 탐사, 키는 int64, 곱셈 해시(Fibonacci hashing 상수 0x9E3779B97F4A7C15 의 상위 비트). 항목은 서로 다른 P 값뿐이라 ≤ n+1 ≤ 4097 → 적재율 ≤ 1/2, 빈 칸이 늘 있어 탐사가 끝난다. 처음 짠 판은 조회 함수가 없는 키를 만나면 개수 0 짜리 칸을 **삽입**했는데, 그러면 항목이 최대 2(n+1) = 8194 개까지 늘어 표가 꽉 차면 탐사가 영원히 돈다 — 고정 크기 해시에서 "조회는 삽입하지 않는다"는 규칙이 중요한 이유. 테스트: LeetCode 예제 [1,1,1] k=2 → 2, [1,2,3] k=3 → 2, 전부 0 → n(n+1)/2, 값 −3..3 랜덤 300개를 O(n²) 와 대조, INT32_MAX/INT32_MIN 교대(int64 필요) → 4095.

</details>

### 연습 2 — 자기 자신을 뺀 곱 (LeetCode 238)

`out[i]` = a[i] 를 뺀 나머지 곱. 나눗셈 금지(0 이 있으면 나눌 수 없고, M0 엔 나눗셈 명령도 없다). 출력 외 추가 메모리 O(1).

<details><summary>답 보기</summary>

누적합의 "곱 버전": 앞쪽 곱(prefix product)을 out 에 먼저 쓰고, 뒤에서부터 뒤쪽 곱을 곱한다.

```c
int64_t left = 1;
for (size_t i = 0; i < n; i++) { out[i] = left; left *= a[i]; }
int64_t right = 1;
for (size_t i = n; i-- > 0;) { out[i] *= right; right *= a[i]; }
```

`i-- > 0` 은 size_t 역순 루프의 안전한 관용구(i = 0 에서 끝남). 테스트: [1,2,3,4] → [24,12,8,6], [−1,1,0,−3,3] → [0,0,9,0,0], 길이 1..12·값 −5..5(0 여러 개 포함) 2000개를 O(n²) 와 대조. 곱은 덧셈보다 넘침이 훨씬 빠르다: |a| ≤ 5 · 11개 → 5^11 ≈ 4.9 × 10^7 이라 int64 로 여유.

</details>

### 연습 3 — 2D 차분 배열: 센서 시야 겹침 지도

로봇 주변 320×240 격자에 센서마다 시야 직사각형 [x0, x1) × [y0, y1) 과 가중치 v 가 있다. 500개 직사각형을 다 더한 지도를 O(1)/직사각형 + O(wh) 로.

<details><summary>답 보기</summary>

모서리 4개: (x0, y0) +v, (x1, y0) −v, (x0, y1) −v, (x1, y1) +v. 2D 누적을 하면 [x0, x1) × [y0, y1) 안에서만 v 가 남는다 — 지시함수 ([x ≥ x0] − [x ≥ x1])([y ≥ y0] − [y ≥ y1]) 의 전개 그대로. D 는 (w+1)×(h+1). 제자리 누적 `D[y][x] += D[y−1][x] + D[y][x−1] − D[y−1][x−1]` 을 행 우선 오름차순으로. 테스트: 랜덤 크기 5장 × 500 직사각형(v −10..10)을 칸마다 더한 브루트포스와 memcmp, 3×2 손 계산.

</details>

### 연습 4 — 줄 버퍼 박스 필터 (열 합)

integral image 없이, `col[x]` = 현재 세로 창 안의 열 합(W 칸)만 들고 잘린 (2r+1)² 창의 합을 모든 픽셀에 대해 구하라. 결과가 integral image 의 `rect_sum` 과 같아야 한다.

<details><summary>답 보기</summary>

행 y 로 내려갈 때 행 y+r 을 더하고(있으면) 행 y−r−1 을 뺀다(있으면) → col[x] = Σ 행 [max(0, y−r), min(h, y+r+1)). 그다음 행 방향으로 같은 이동 합: x 가 늘 때 col[x+r] 더하고 col[x−r−1] 빼기. 불변식이 두 번 겹친 것이고, 각 픽셀은 O(1). 스트리밍이면 입력 행을 2r+2 개만 링 버퍼에 들고 있으면 된다(행 y+r 이 들어올 때 행 y−r−1 이 아직 남아 있어야 하므로 2r+2). 테스트: r = 0, 1, 3, 7, 15 (320×240) 와 r = 400 > w, h (37×23) 에서 모든 픽셀이 integral 결과와 같음. 속도는 demo_bench 의 colsum 열. 실제 ISP 파이프라인의 라인 버퍼 구조는 CAM-M09.

</details>

### 연습 5 — 국소 분산: 제곱의 integral image 를 uint32 로

Viola–Jones 류 검출기는 창마다 표준편차로 대비를 정규화한다. 창의 N²·분산 = N·Σx² − (Σx)² 를 integral 두 장(값, 제곱)으로 O(1) 에. 제곱 integral 은 640×480 8비트에서 35비트인데, uint32 로 저장해도 되는 조건은?

<details><summary>답 보기</summary>

감김 규칙: 창의 진짜 Σx² &lt; 2^32 이면 감긴 uint32 integral 의 포함–배제가 정확하다. 8비트면 x² ≤ 65025 → 창 넓이 ≤ ⌊(2^32 − 1)/65025⌋ = 66051 (257×257 = 66049 OK, 258×257 = 66306 은 2^32 만큼 모자란 값이 나옴 — 테스트가 두 경우를 다 확인). 12비트면 4095² → 넓이 ≤ 256 (16×16) 이라 uint32 는 사실상 못 쓰고 uint64. 분자 계산은 uint64: N ≤ 66051, Σx² &lt; 2^32 → N·Σx² &lt; 2^49. N·Σx² − (Σx)² ≥ 0 은 코시–슈바르츠 부등식이라 부호 없는 뺄셈이 안전하다. 테스트: 200×150 랜덤 8비트, 창 3/7/15 를 3000번 브루트포스와 대조, 300×300 전부 255 에서 제곱 integral 전체 = 5,852,250,000 mod 2^32 확인.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 16/16 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 오류 없음
```

## 10. 핵심 정리

- P 는 **n+1 칸**, P[0] = 0, [l, r) 의 합 = P[r] − P[l]. 반열린 구간이면 빈 구간·이어 붙이기·r = n 이 특별 처리 없이 된다.
- 차분 배열: [l, r) 에 v → D[l] += v, D[r] −= v (D 도 n+1 칸), 한 번 누적해 복원. 누적 ∘ 차분 = 항등, 갱신 순서 무관.
- 2D: I 는 (w+1)×(h+1), 직사각형 = D − B − C + A. 만들기는 "위 칸 + 이 행의 누적".
- 비트 수 = ⌈log2(n·max + 1)⌉. 640×480: 8비트 27비트, RAW12 31비트 → uint32 OK. 1080p RAW12 33비트, 제곱 integral 은 8비트도 35비트.
- **감김 규칙**: 부호 없는 누적합은 감겨도, 질의의 진짜 합 &lt; 2^w 이면 차가 정확(mod 2^w 준동형). uint16·8비트 → 257 개, uint32·12비트 → 1,048,832 개 — 실측 kmax+1 에서 처음 틀림. uint16 차는 `(uint16_t)` 로 다시 자를 것.
- int32 누적 넘침은 UB(UBSan 이 잡음). int64 로 누적하거나 uint32 로 감기게.
- MCU: integral 1.2 MB 는 안 들어간다 → 고정 창은 열 합 + 줄 버퍼(O(w)), 직사각형 질의는 타일/uint16 감김, 통계는 ISP 하드웨어. 이 맥에서 r = 31 박스 합: 직접 620 ms vs 0.4 ms.

## 참고자료

- F. C. Crow, "Summed-Area Tables for Texture Mapping", *SIGGRAPH* 1984 — 2D 누적합(summed-area table)의 원조.
- P. Viola, M. Jones, "Rapid Object Detection using a Boosted Cascade of Simple Features", *CVPR* 2001 — "integral image" 이름과 Haar 특징의 O(1) 계산, 제곱 integral 로 분산 정규화.
- D. Bradley, G. Roth, "Adaptive Thresholding Using the Integral Image", *Journal of Graphics Tools* 12(2), 2007 — 적응 이진화(권·호는 기억에 의존 — 확인 권장).
- OpenCV `cv::integral` — 출력이 (W+1)×(H+1) 이고 sum/sqsum/tilted 를 낸다: <https://docs.opencv.org/4.x/d7/d1b/group__imgproc__misc.html>
- ISO/IEC 9899:2011 (C11) — 6.2.5p9 부호 없는 산술은 mod 2^N, 6.3.1.1 정수 승격, 6.3.1.3 범위 밖 signed 변환은 구현 정의, 6.5p5 signed 넘침은 UB.
- LeetCode 303 Range Sum Query - Immutable, 304 Range Sum Query 2D - Immutable, 370 Range Addition, 1109 Corporate Flight Bookings, 560 Subarray Sum Equals K, 238 Product of Array Except Self.
- AlgoMonster 노트 M03 §20 Prefix Sum Introduction, §21 Subarray Sum Equals Target, §22 Range Sum Query - Immutable, §23 Product of Array Except Self (Don 정리본) — `algomaster-coding-pattern/original-md/2026-09-27_M03_two_pointers.md`.
- ARM, *Cortex-M4 Technical Reference Manual* / *ARMv7-M ARM* — `ADC`(자리올림 덧셈)와 시프트된 피연산자; 이 레슨의 asm 은 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S` 실제 출력.
- 관련 레슨: ALGO-B10 (슬라이딩 윈도·이동 평균 링 버퍼), ALGO-B09 (투 포인터), ALGO-M09 (구간 겹침·그리디), ALGO-A09 (스윕 라인), ALGO-M08 (배낭의 역순 루프), ALGO-M15 (상수 나눗셈), CV-B04 (분리 가능 필터·적분 영상), CV-B08 (Global/Otsu/Adaptive 임계값), CAM-M06 (AE 제어 루프), CAM-M09 (라인 버퍼 ISP), SYSD-B16 (전력 예산), HWLAB-M02·M04 (부하 과도·전류 프로파일), C-M14 (ISR 공유 데이터), DS-A02 (ISR↔태스크 링 버퍼)
