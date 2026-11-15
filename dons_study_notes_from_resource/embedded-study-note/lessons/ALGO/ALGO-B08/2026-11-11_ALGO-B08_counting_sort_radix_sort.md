---
id: ALGO-B08
time: 60분
updated: 2026-11-11
---
# ALGO-B08 비교 없는 정렬: 계수 정렬·기수 정렬

## 학습 목표

- 비교 정렬의 하한 Ω(n log n) 이 왜 계수 정렬(counting sort)에는 적용되지 않는지 말하고, 12비트 ADC 샘플을 **히스토그램 → 다시 쓰기**(맨 키)와 **히스토그램 → 배타적 누적합 → 안정 배치**(레코드) 두 가지로 구현한다.
- uint32 타임스탬프를 8비트 자리 4 패스 **LSD 기수 정렬**(radix sort)로 구현하고, data ↔ scratch 두 버퍼를 오가는 ping-pong 이 짝수 패스 뒤 원래 버퍼에서 끝나는 이유를 설명한다.
- "각 패스가 안정(stable)해야 한다" 를 귀납법으로 설명하고, 불안정 패스가 실제로 정렬을 깨는 것을 손 계산과 실행으로 보인다.
- qsort 대비 속도·추가 메모리를 실측(n = 10³…10⁶)하고, **n 이 작고 키 범위가 클 때** 계수 정렬이 지는 지점을 숫자로 말한다.
- Cortex-M 에서 4096 칸 카운트 배열(uint32 이면 16 KB)을 스택/static/호출자 작업 공간 중 어디에 둘지, uint16 카운트의 넘침 위험, 256 칸(1 KB) 기수 카운트가 현실적인 이유를 설명한다.

## 1. 문제

임베디드 면접에서 "센서 값 백만 개를 정렬하라" 가 나오면 면접관이 기다리는 첫 질문은 **"값의 범위가 어떻게 되나요?"** 다 — ADC 코드, 타임스탬프, CAN ID 처럼 키가 폭이 정해진 정수라면 비교 없이 O(n) 에 정렬할 수 있고, 그 대가(카운트 배열·scratch 버퍼 RAM)를 MCU 예산으로 말할 수 있어야 한다.

비교 정렬(quicksort, mergesort, heapsort — ALGO-B05~B07)은 원소 두 개를 비교한 결과(<, ≥)만 써서 순서를 정한다. 가능한 순서는 n! 가지이고 비교 하나는 경우를 최대 절반으로 줄이므로, 최악에 ⌈log₂ n!⌉ ≈ n log₂ n − 1.44 n 번은 비교해야 한다(결정 트리 하한, CLRS 8.1). 계수 정렬은 비교를 하지 않는다: **키 자체를 배열 인덱스로 쓴다.** 그래서 하한의 전제(비교 모델)가 깨지고, 시간은 O(n + K)(K = 가능한 키 개수)가 된다. 대신 K 칸짜리 카운트 배열이 필요하다 — 이게 이 레슨의 거래(trade-off)다.

```svg
<svg viewBox="0 0 740 336" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">안정 계수 정렬 = 히스토그램 → 배타적 누적합(시작 위치) → 앞에서부터 배치</text>
  <text x="10" y="58" fill="currentColor">src</text>
  <g stroke="currentColor">
    <rect x="120" y="38" width="56" height="30" class="f-mut"/><rect x="180" y="38" width="56" height="30" class="f-acc"/>
    <rect x="240" y="38" width="56" height="30" class="f-mut"/><rect x="300" y="38" width="56" height="30" class="f-mut"/>
    <rect x="360" y="38" width="56" height="30" class="f-mut"/><rect x="420" y="38" width="56" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="148" y="58">3 a</text><text x="208" y="58">1 b</text><text x="268" y="58">2 c</text>
    <text x="328" y="58">3 d</text><text x="388" y="58">0 e</text><text x="448" y="58">1 f</text>
  </g>
  <text x="490" y="58" fill="currentColor">1b 가 1f 보다 먼저</text>
  <text x="10" y="118" fill="currentColor">cnt</text>
  <text x="10" y="168" fill="currentColor">pos</text>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="148" y="92">키 0</text><text x="208" y="92">키 1</text><text x="268" y="92">키 2</text><text x="328" y="92">키 3</text>
  </g>
  <g stroke="currentColor" class="f-mut">
    <rect x="120" y="98" width="56" height="30"/><rect x="180" y="98" width="56" height="30"/>
    <rect x="240" y="98" width="56" height="30"/><rect x="300" y="98" width="56" height="30"/>
    <rect x="120" y="148" width="56" height="30"/><rect x="180" y="148" width="56" height="30"/>
    <rect x="240" y="148" width="56" height="30"/><rect x="300" y="148" width="56" height="30"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="148" y="118">1</text><text x="208" y="118">2</text><text x="268" y="118">1</text><text x="328" y="118">2</text>
    <text x="148" y="168">0</text><text x="208" y="168">1</text><text x="268" y="168">3</text><text x="328" y="168">4</text>
  </g>
  <text x="370" y="118" fill="currentColor">1 패스: cnt[key]++ (n 번)</text>
  <text x="370" y="168" fill="currentColor">pos[k] = 키 &lt; k 인 원소 수 = k 의 첫 자리</text>
  <text x="10" y="258" fill="currentColor">dst</text>
  <g stroke="currentColor">
    <rect x="120" y="238" width="56" height="30" class="f-mut"/><rect x="180" y="238" width="56" height="30" class="f-acc"/>
    <rect x="240" y="238" width="56" height="30" class="f-ok"/><rect x="300" y="238" width="56" height="30" class="f-mut"/>
    <rect x="360" y="238" width="56" height="30" class="f-mut"/><rect x="420" y="238" width="56" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="148" y="258">0 e</text><text x="208" y="258">1 b</text><text x="268" y="258">1 f</text>
    <text x="328" y="258">2 c</text><text x="388" y="258">3 a</text><text x="448" y="258">3 d</text>
    <text x="148" y="290">0</text><text x="208" y="290">1</text><text x="268" y="290">2</text>
    <text x="328" y="290">3</text><text x="388" y="290">4</text><text x="448" y="290">5</text>
  </g>
  <text x="490" y="250" fill="currentColor">dst[pos[key]++] = src[i]</text>
  <text x="490" y="270" fill="currentColor">i = 0 → n-1 (앞에서부터)</text>
  <text x="10" y="312" fill="currentColor">같은 색 = 같은 원소. 같은 키는 pos 가 1 씩 늘며 왼쪽부터 채워진다 → 입력 순서 유지(안정).</text>
  <text x="10" y="328" fill="currentColor">시간 O(n + K), 추가 메모리 K 칸 + dst n 칸.</text>
</svg>
```

**메모리 예산이 먼저다.** 이 레슨의 모든 버퍼 크기:

| 버퍼 | 칸 수 × 크기 | 바이트 | 비고 |
|---|---|---|---|
| 12비트 계수 정렬 카운트 (uint32) | 4096 × 4 | **16 384** | 작은 MCU 의 SRAM 전체(예: STM32F103C8 은 20 KB)에 맞먹는다 |
| 같은 것 uint16 카운트 | 4096 × 2 | 8 192 | n ≤ 65535 일 때만 안전 (Part C1) |
| 8비트 기수 정렬 카운트 (uint32) | 256 × 4 | **1 024** | 스택에 둬도 되는 크기 |
| 기수 정렬 scratch (uint32 n 개) | n × 4 | 4n | 기수 정렬의 진짜 비용. n = 1024 이면 4 KB |

### Part A — 12비트 ADC 샘플 계수 정렬

- `count_sort_adc_inplace(a, n, cnt)` — uint16 샘플(유효 값 0..4095)을 **맨 키**로 정렬. 히스토그램을 만든 뒤 값 v 를 cnt[v] 번 다시 쓴다(rewrite from counts). 4095 를 넘는 값이 하나라도 있으면 **-1, 배열은 그대로**. 작업 공간 `cnt` 는 호출자가 준다.
- `count_sort_adc_stable(src, dst, n, cnt)` — `{code, ch, seq}` 레코드를 code 기준 **안정** 정렬 (위 그림). 범위 밖이면 -1, dst 는 그대로. 이 안정 배치가 Part B 의 한 패스와 똑같은 코드다.
- 테스트: random / all-equal / sorted / reversed / high-bits-only(위 4비트만 다름) × n ∈ {0,1,2,3,17,256,1000,5000} + 무작위 길이 300 회를 qsort 와 비교, 범위 밖(4096, 0xFFFF), 0 과 4095 섞기, 안정성은 qsort(code, seq) 와 비교.

### Part B — uint32 타임스탬프 8비트 LSD 기수 정렬

- `radix_pass_u32(src, dst, n, shift)` — 자리값 `(x >> shift) & 0xFF` 기준 안정 계수 패스 (카운트 256 칸).
- `radix_sort_u32(a, tmp, n)` — shift = 0, 8, 16, 24 네 패스. a→tmp→a→tmp→a 로 오가므로 **복사 없이 a 에서 끝난다**.
- 테스트: 같은 5 패턴(high-bits-only = 맨 위 바이트만 다르고 아래 24비트는 상수) + 경계값 {0, 1, 0xFF, 0x100, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF ×2} + n = 0, 1. 그리고 **주어진 불안정 패스**(`radix_pass_u32_unstable`)로 돌리면 `{0x0102, 0x0201, 0x0101, 0x0202}` 가 정렬되지 않는 것을 확인한다.

### Part C — 펌웨어 응용

- `adc_median_hist(a, n, cnt16, &median)` — DMA 로 받은 ADC 블록의 하위 중앙값(정렬했을 때 (n−1)/2 번째)을 **정렬 없이** uint16 히스토그램(8 KB)의 누적으로 찾는다. n == 0, **n > 65535(카운트 넘침 가능)**, 범위 밖 샘플이면 -1.
- `radix_sort_events(ev, tmp, n, t_ref)` — µs 타임스탬프가 붙은 CAN 이벤트 레코드를 시간순으로. uint32 µs 카운터는 2³² µs ≈ 71.6 분마다 0 으로 돌아가므로(wrap) 키를 `t - t_ref`(mod 2³², 기준 시각 이후 경과 시간)로 잡는다. 같은 µs 의 프레임은 **도착 순서 유지**(안정).

## 2. Problem (English)

You are writing the post-processing step of a data logger on a Cortex-M MCU (no heap).

```c
/* 12-bit ADC samples stored in uint16_t; valid range 0..4095. cnt is caller-provided (4096 entries). */
int  count_sort_adc_inplace(uint16_t *a, size_t n, uint32_t *cnt);              /* 0, or -1 if any a[i] > 4095 (a unchanged) */
int  count_sort_adc_stable(const adc_rec_t *src, adc_rec_t *dst, size_t n, uint32_t *cnt);  /* stable by .code */

/* LSD radix sort, 8-bit digits, 4 passes. tmp has room for n elements. Result must end up in a. */
void radix_pass_u32(const uint32_t *src, uint32_t *dst, size_t n, unsigned shift);
void radix_sort_u32(uint32_t *a, uint32_t *tmp, size_t n);

int  adc_median_hist(const uint16_t *a, size_t n, uint16_t *cnt16, uint16_t *median);   /* lower median */
void radix_sort_events(event_t *ev, event_t *tmp, size_t n, uint32_t t_ref);           /* key = t - t_ref (mod 2^32), stable */
```

Constraints: 0 ≤ n ≤ 10⁶ (n ≤ 65535 for the uint16 histogram). O(n + K) time for counting sort (K = 4096), O(4·(n + 256)) for radix sort. No recursion, no malloc, no division.

```text
count_sort_adc_inplace([7, 4095, 0, 12, 7])   -> 0,  a = [0, 7, 7, 12, 4095]
count_sort_adc_inplace([7, 4095, 0, 12, 4096]) -> -1, a unchanged
radix_sort_u32([0xFFFFFFFF, 0, 0x80000000, 0x7FFFFFFF]) -> [0, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF]
radix_sort_events(t = [0x00000005, 0xFFFFFFF8, 0x00000000, 0xFFFFFFFF], t_ref = 0xFFFFFFF0)
                                               -> t = [0xFFFFFFF8, 0xFFFFFFFF, 0x00000000, 0x00000005]
```

Follow-ups: Why must every radix pass be stable? When is counting sort *slower* than qsort? Where do you put a 16 KB count array on an MCU with a 1 KB main stack?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여섯 군데. 테스트 하네스, 패턴 생성기, 그리고 **불안정 패스 시연 코드**(`radix_pass_u32_unstable`)는 주어진다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
  A1 random          bad=305
FAIL starter.c:204  bad == 0
  A1 all-equal       bad=0
  A1 sorted          bad=0
…
  unstable LSD on {0x0102,0x0201,0x0101,0x0202} -> {0x0102,0x0101,0x0202,0x0201}
  C1 median bad=300
…
FAIL starter.c:324  w[0].id == 2 && w[1].id == 4 && w[2].id == 3 && w[3].id == 1
10/30 passed
```

(exit 1. 통과한 10 개는 "이미 정렬된 입력", "같은 값만", 원소 1 개, -1 을 기대하는 가드 검사 3 개, "전부 같은 시각" 안정성 검사, 불안정 시연처럼 아무것도 안 하는 스텁이 우연히 맞히는 것들 — 이런 테스트만으로는 아무것도 증명하지 못한다.)

## 4. 힌트

<details><summary>힌트 1 — 범위 검사는 "쓰기 전에"</summary>

히스토그램 패스는 입력을 **읽기만** 한다. 그 루프 안에서 `a[i] > 4095` 를 만나면 바로 -1 을 반환하면 배열은 아직 손대지 않은 상태다. 검사 없이 `cnt[a[i]]++` 를 하면 0xFFFF 같은 쓰레기 샘플이 카운트 배열 밖(64 KB 너머)에 쓴다.

</details>

<details><summary>힌트 2 — 배타적 누적합(exclusive prefix sum)</summary>

`sum = 0; for v: { c = cnt[v]; cnt[v] = sum; sum += c; }` — 이제 `cnt[v]` 는 "v 보다 작은 키의 개수" = v 가 들어갈 첫 자리. 앞에서부터 훑으며 `dst[cnt[key]++] = src[i]`. (포함 누적합 `cnt[v] += cnt[v-1]` 을 쓰면 그건 "끝 다음 자리"라서, 그때는 **뒤에서부터** `dst[--cnt[key]] = src[i]` 로 해야 안정하다 — CLRS 방식.)

</details>

<details><summary>힌트 3 — ping-pong 은 포인터 두 개만 바꾼다</summary>

`src = a, dst = tmp` 로 시작해서 패스마다 `radix_pass_u32(src, dst, n, shift)` 후 `src` 와 `dst` 를 맞바꾼다. 4 번 바꾸면 src 는 다시 a. 패스 수가 홀수라면? 결과가 tmp 에 있다 → 연습 3.

</details>

<details><summary>힌트 4 — 중앙값은 정렬 없이 누적으로</summary>

정렬된 배열의 k 번째 원소 = 누적 카운트가 처음으로 k 를 넘는 값 v. k = (n−1)/2. 카운트가 uint16 이면 한 칸이 65535 를 넘을 수 없도록 n 자체를 막는 게 가장 간단하다.

</details>

<details><summary>힌트 5 — 랩어라운드 키</summary>

uint32 뺄셈은 mod 2³² 로 정의돼 있다(부호 없는 정수는 넘치지 않고 돌아간다, C11 6.2.5p9). `t - t_ref` 는 "t_ref 에서 t 까지 앞으로 몇 µs" 이므로 모든 이벤트가 [t_ref, t_ref + 2³²) 안에 있으면 이 키로 정렬한 순서가 시간 순서다. 레코드 버전은 B1 패스를 그대로 쓰되 자리값을 `((src[i].t - t_ref) >> shift) & 0xFF` 로.

</details>

## 5. 풀이

**면접에서 말하는 순서**

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 질문으로 범위 확인 | 키의 폭/범위는? (12비트 ADC → K = 4096) 범위 밖 값은 어떻게? 같은 키의 순서가 중요한가(안정성)? RAM 예산은? n 의 최대는? |
| 2. 무식한 방법 + 복잡도 | qsort / 삽입 정렬: O(n log n) / O(n²), 추가 메모리 O(1)~O(log n). 비교 정렬 하한 ⌈log₂ n!⌉ |
| 3. 더 나은 아이디어 + 불변식 | 키를 인덱스로 → 계수 정렬 O(n + K). 키가 32비트면 K = 2³² 는 불가능 → 8비트씩 4 번 = LSD 기수 정렬. 불변식: "패스 k 뒤 배열은 아래 8k 비트 기준으로 정렬돼 있다" |
| 4. 코드 | 히스토그램 → 배타적 누적합 → 안정 배치. ping-pong |
| 5. 소리 내어 말할 테스트 | n = 0, 1 · 모두 같은 값 · 정렬/역순 · 위 바이트만 다른 값 · 0 과 최댓값 · 범위 밖 · 같은 키의 순서 |
| 6. 복잡도 (시간 + 메모리 + 스택) | 계수: Θ(n + K) 시간, K 칸 카운트. 기수: Θ(4(n + 256)), 1 KB 카운트 + n 칸 scratch. 재귀 없음 → 스택은 상수(카운트를 스택에 두면 1 KB) |

### Part A — 맨 키: 히스토그램에서 다시 쓰기

키만 있고 따라다니는 데이터(satellite data)가 없으면 "같은 키 둘 중 어느 쪽이 먼저냐" 가 의미 없다 — 4 와 4 는 구별할 수 없다. 그러니 원소를 옮길 필요 없이 **카운트만으로 배열을 새로 쓰면** 된다:

```c
for (size_t i = 0; i < n; i++) { if (a[i] > ADC_MAX) return -1; cnt[a[i]]++; }
size_t k = 0;
for (uint32_t v = 0; v < ADC_RANGE; v++)
    for (uint32_t c = cnt[v]; c > 0; c--) a[k++] = (uint16_t)v;
```

- 시간: 히스토그램 n 번 + 바깥 루프 K 번 + 안쪽 루프 총 n 번 = **Θ(n + K)**. 입력 분포와 무관하게 거의 같은 시간 → 최악 실행 시간(WCET)을 계산하기 쉽다. 펌웨어에선 이게 평균 속도보다 중요할 때가 많다.
- 추가 메모리: 카운트 K 칸뿐 (dst 불필요, in-place).
- 범위 검사는 히스토그램 루프 안에서 — 그 시점엔 a 를 아직 안 썼으므로 거부해도 입력이 보존된다. "반쯤 정렬된 채 에러" 가 없다.

### Part A — 레코드: 배타적 누적합과 안정 배치

레코드(`code` + 채널 + 순번)는 다시 쓰기가 불가능하다 — 카운트는 "code 1 이 2 개" 만 알지 어느 레코드인지 모른다. 그래서 각 키의 **시작 위치**를 계산해 레코드를 직접 옮긴다. 위 그림의 예를 한 줄씩 따라가면 (pos = 배타적 누적합 [0, 1, 3, 4] 에서 시작):

| i | src[i] | pos[key] 읽음 → dst 위치 | pos 갱신 후 [k0, k1, k2, k3] |
|---|---|---|---|
| 0 | (3, a) | pos[3] = 4 → dst[4] | [0, 1, 3, 5] |
| 1 | (1, b) | pos[1] = 1 → dst[1] | [0, 2, 3, 5] |
| 2 | (2, c) | pos[2] = 3 → dst[3] | [0, 2, 4, 5] |
| 3 | (3, d) | pos[3] = 5 → dst[5] | [0, 2, 4, 6] |
| 4 | (0, e) | pos[0] = 0 → dst[0] | [1, 2, 4, 6] |
| 5 | (1, f) | pos[1] = 2 → dst[2] | [1, 3, 4, 6] |

결과 `e b f c a d` (키 0 1 1 2 3 3). b 가 f 보다, a 가 d 보다 앞 — **안정**. 이유: 같은 키의 원소는 입력에서 나오는 순서대로 pos 를 1 씩 늘리며 왼쪽부터 채운다. 끝난 뒤 pos 는 [1, 3, 4, 6] = 포함 누적합 = 각 키의 "끝 다음 자리" — 계산이 맞았는지 확인하는 체크로 쓸 수 있다 (마지막 값이 n).

복잡도는 Θ(n + K) 시간, K 칸 + dst n 칸. dst 가 따로 필요하다는 점이 맨 키 버전과 다르다.

### Part B — LSD 기수 정렬: 왜 아래 자리부터, 왜 안정해야 하나

32비트 키를 한 번에 세면 K = 2³² 칸(16 GB)이 필요하다. 대신 8비트 자리(digit) 4 개로 쪼개 **가장 아래 자리부터** 한 자리씩 안정 계수 정렬을 한다(LSD = least significant digit first).

**불변식**: 패스 k(k = 1..4)가 끝나면 배열은 "아래 8k 비트" 기준으로 정렬돼 있다.
증명(귀납): k = 1 은 계수 정렬 그 자체. k−1 에서 성립한다고 하자. 패스 k 는 자리 k 로 정렬한다. 두 원소 x, y 의 자리 k 가 다르면 패스 k 가 바로 순서를 정한다. 자리 k 가 **같으면** 패스 k 는 둘의 순서를 그대로 둔다(**안정성**) — 그 순서는 귀납 가정에 의해 아래 8(k−1) 비트 기준 순서다. 둘을 합치면 아래 8k 비트 기준 순서. k = 4 면 전체 32 비트. ∎
안정성이 빠지면 "자리가 같으면 앞 패스의 순서를 지킨다" 가 무너져 앞 세 패스의 일이 모두 무효가 된다.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b08b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">4 패스 ping-pong: 버퍼 두 개를 오가고, 짝수 번이면 원래 자리 a 에서 끝난다</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="90" height="34" rx="5" class="f-acc"/><rect x="175" y="40" width="90" height="34" rx="5" class="f-mut"/>
    <rect x="330" y="40" width="90" height="34" rx="5" class="f-acc"/><rect x="485" y="40" width="90" height="34" rx="5" class="f-mut"/>
    <rect x="640" y="40" width="90" height="34" rx="5" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="65" y="62">a</text><text x="220" y="62">tmp</text><text x="375" y="62">a</text><text x="530" y="62">tmp</text><text x="685" y="62">a (결과)</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M110 57 L173 57" marker-end="url(#b08b)"/><path d="M265 57 L328 57" marker-end="url(#b08b)"/>
    <path d="M420 57 L483 57" marker-end="url(#b08b)"/><path d="M575 57 L638 57" marker-end="url(#b08b)"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="142" y="92">shift 0</text><text x="297" y="92">shift 8</text><text x="452" y="92">shift 16</text><text x="607" y="92">shift 24</text>
  </g>
  <text x="10" y="130" fill="currentColor" font-weight="bold">입력 {0x0102, 0x0201, 0x0101, 0x0202} — 아래 두 바이트만 표시</text>
  <text x="20" y="160" fill="currentColor" font-weight="bold">안정 패스 (정답)</text>
  <text x="390" y="160" fill="currentColor" font-weight="bold">불안정 패스 (끝 위치 + 앞에서부터)</text>
  <g fill="currentColor" font-family="monospace">
    <text x="20" y="190">입력     0102 0201 0101 0202</text>
    <text x="20" y="220">byte0 뒤 0201 0101 0102 0202</text>
    <text x="20" y="250">byte1 뒤 0101 0102 0201 0202</text>
    <text x="390" y="190">입력     0102 0201 0101 0202</text>
    <text x="390" y="220">byte0 뒤 0101 0201 0202 0102</text>
    <text x="390" y="250">byte1 뒤 0102 0101 0202 0201</text>
  </g>
  <rect x="20" y="262" width="300" height="26" class="f-ok" stroke="currentColor"/>
  <text x="30" y="280" fill="currentColor">정렬됨</text>
  <rect x="390" y="262" width="340" height="26" class="f-warn" stroke="currentColor"/>
  <text x="400" y="280" fill="currentColor">깨짐: 0102 &gt; 0101 인데 앞에 있다</text>
  <text x="20" y="312" fill="currentColor">불안정 byte1 패스가 자리 01 묶음 (0101, 0102) 을 (0102, 0101) 로 뒤집어</text>
  <text x="20" y="330" fill="currentColor">byte0 패스가 만든 순서를 지운다. byte2·byte3 패스(자리값 모두 0)는</text>
  <text x="20" y="348" fill="currentColor">불안정 버전에선 배열 전체를 두 번 뒤집어 제자리, 안정 버전에선 그대로 복사.</text>
</svg>
```

그림의 불안정 결과는 하네스가 실제로 찍는 줄과 같다: `unstable LSD on {0x0102,0x0201,0x0101,0x0202} -> {0x0102,0x0101,0x0202,0x0201}`. 이 버그 패스로 `radix_sort_u32` 를 바꿔 끼워 전체 테스트를 돌리면 **26/30** (random 308 건 중 298 건 불일치, sorted 3 건, reversed 4 건). 흥미로운 건 **high-bits-only 패턴은 통과**했다는 점이다: 아래 24 비트가 모두 같으니 앞 세 패스는 배열 전체를 뒤집기만 하고, 마지막 패스에서 뒤집히는 "같은 자리값 묶음" 은 값까지 똑같은 원소들이라 구별되지 않는다. 테스트 패턴 하나만 믿으면 이런 버그가 숨는다.

**왜 MSD(위 자리부터)가 아니라 LSD 인가**: LSD 는 모든 패스가 배열 전체를 똑같이 한 번 훑는다 — 재귀 없음, 버킷 관리 없음, 루프 두 개. MSD 는 위 자리로 256 개 버킷을 나눈 뒤 **각 버킷 안에서** 다음 자리를 재귀로 정렬해야 한다(문자열처럼 길이가 다른 키, 일찍 끝내기에는 MSD 가 유리 — Q3).

**ping-pong**: 패스마다 새 버퍼를 쓰지 않고 a 와 tmp 를 번갈아 쓴다. 4 는 짝수라 a→tmp→a→tmp→a, 마지막 결과가 a 에 있어 복사가 없다. 자리 폭을 11 비트로 바꾸면 3 패스(홀수) → 결과가 tmp 에 남으니 memcpy 한 번이 필요하다 (연습 3).

| 복잡도 | 계수 정렬 (K = 4096) | 8비트 LSD 기수 정렬 (32비트 키) |
|---|---|---|
| 시간 | Θ(n + K) | Θ(4 · (n + 256)) — 패스마다 히스토그램 n + 누적 256 + 배치 n |
| 추가 메모리 | K 칸 (+ 레코드면 dst n 칸) | 256 칸 + scratch n 칸 |
| 스택 | 상수 (카운트를 스택에 두면 16 KB!) | 상수 (카운트 1 KB) |
| 최악 = 평균? | 예 (분포 무관) | 예 (분포 무관) |
| 안정? | 배치 버전은 예 | 예 (패스가 안정하므로) |

자리 폭 r 비트, 키 폭 w 비트면 ⌈w/r⌉ 패스 × Θ(n + 2ʳ). r 을 키우면 패스는 줄지만 카운트가 2ʳ 칸으로 커진다. r = 16 이면 2 패스지만 카운트 65536 × 4 = 256 KB 라 캐시(그리고 MCU RAM)를 넘는다. r = 8 이 "1 KB, L1 에 쏙" 이라 실전 기본값이다 (Q4).

### Part C — 히스토그램 중앙값과 uint16 카운트

중앙값만 필요하면 정렬할 필요가 없다. 히스토그램을 누적하다 처음으로 (n−1)/2 를 넘는 값이 답이다 — 시간 Θ(n + K), 입력 배열은 읽기만 (const). 카운트를 uint16 으로 하면 8 KB 를 아끼지만 **한 값이 65535 번을 넘으면 0 으로 돌아간다** (`cnt16[v]++` 는 int 로 승격돼 65536 이 된 뒤 uint16 으로 변환 → 2¹⁶ 로 나눈 나머지 0. 부호 없는 변환이라 UB 는 아니고 정의된 동작, C11 6.3.1.3p2 — C-B02 참고). 조용히 틀린다는 뜻이다. 가드 없는 버전을 실제로 돌린 결과:

```text
naive uint16 median of 65536 x 100 + 1 x 4000: rc=-1 median=0 (cnt16[100]=0)
naive uint16 median of 70000x100 + 60000x200 + 10000x300 (true 100): rc=0 median=300 (cnt16[100]=4464)
```

두 번째 줄이 무섭다: 에러 없이 300 을 중앙값이라고 보고한다 (70000 − 65536 = 4464). 그래서 `n > UINT16_MAX` 면 거부한다 — 한 칸의 카운트는 n 을 넘을 수 없으니 n ≤ 65535 면 넘침이 불가능하다. 12비트 ADC 를 1 MSPS 로 돌리면 65 ms 분량이 한계라는 뜻이니, 그보다 큰 블록이면 uint32 카운트(16 KB)로 가야 한다.

### Part C — 랩어라운드 타임스탬프

uint32 µs 카운터(예: 1 MHz 로 도는 32비트 타이머)는 약 71.6 분마다 0xFFFFFFFF → 0 으로 넘어간다. 그 경계를 걸친 로그를 `t` 그대로 정렬하면 넘어간 뒤의 이벤트(0x00000005)가 넘어가기 전 이벤트(0xFFFFFFF8)보다 앞으로 간다. 키를 `t - t_ref` 로 바꾸면 "t_ref 부터 경과한 µs" 가 되어 순서가 맞다. 조건: 모든 이벤트가 [t_ref, t_ref + 2³²) 구간 안 (t_ref = 버퍼에서 가장 오래될 수 있는 시각, 예: 로그 창의 시작). 하네스는 t_ref 를 0xFFFFF000 근처에 두고 0x3000 µs 범위로 뽑아 랩을 넘나드는 200 회를 qsort(키, seq) 와 비교한다.

레코드(8 B)를 4 번 옮기는 비용이 아깝다면 (키, 인덱스) 쌍만 정렬한 뒤 인덱스로 한 번 모으는 방법도 있다 (DS-A11 의 AoS/SoA 이야기와 같은 고민).

### 벤치마크 — `demo_bench.c`

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench   # 약 15 초
```

이 맥(Apple Silicon M 계열, `-O2`), 워밍업 1 회 + 7 회 측정의 중앙값, memcpy(입력 복원)만 한 시간을 빼고, 결과 원소를 volatile 에 더해 작업이 지워지지 않게 했다. 입력은 서로 다른 배열을 돌려 쓴다(합 4M 원소) — 처음엔 같은 입력을 반복했는데 n = 1000 에서 qsort 가 15.8 ns/원소로 나왔다가, 입력을 바꿔 가며 재니 34.7 ns/원소가 됐다. 분기 예측기가 같은 비교 결과열을 외운 것으로 보인다 (ARCH-M03). 입력이 캐시에 남는 효과는 memcpy 기준선으로 빼 주었다. **절대 ns 가 아니라 비율을 볼 것.** 실제 출력:

```text
표 1: 12-bit ADC uint16 (0..4095), ns/elem (median of 7)
        n      qsort   counting  speedup  extra mem (cnt)
     1000      34.70       6.15     5.6x         16384 B
    10000      42.37       2.23    19.0x         16384 B
   100000      39.30       0.67    58.5x         16384 B
  1000000      38.53       0.39    98.8x         16384 B

표 2: uint32 random, ns/elem (median of 7)
        n      qsort   radix8x4  speedup    extra mem (cnt+tmp)
     1000      33.79       2.97    11.4x       1024 +      4000 B
    10000      47.36       2.76    17.2x       1024 +     40000 B
   100000      58.90       3.05    19.3x       1024 +    400000 B
  1000000      70.96       3.30    21.5x       1024 +   4000000 B
```

- 표 1: 계수 정렬의 원소당 시간은 n 이 클수록 **줄어든다** — 고정비(16 KB memset + 4096 칸 훑기, n = 1000 에서 호출당 약 6 µs)가 n 으로 나뉘기 때문. n = 10⁶ 에서 0.39 ns/원소, qsort 의 약 99 배.
- 표 2: 기수 정렬은 n 과 상관없이 약 3 ns/원소로 평평하다(4 패스 × 읽기 + 흩어 쓰기). qsort 는 log n 과 캐시 때문에 34 → 71 ns/원소. 대신 **scratch 가 4n 바이트**(10⁶ 이면 4 MB) — 속도의 값은 RAM 이다.
- 두 번째 실행(다른 컴파일 작업과 겹친 상태)에서는 큰 n 칸이 대체로 ±30% 안에서, 아주 작은 칸은 그 이상(표 3 의 n = 16·M = 4096 계수 정렬 1317 → 2271 ns) 흔들렸다. 순위와 대략의 배율은 같았다.

**계수 정렬이 지는 곳.** 비용은 n 이 아니라 n + M(키 범위)이다. n 은 작은데 M 이 크면 거의 빈 카운트 배열을 지우고 훑는 데 시간을 다 쓴다:

```text
표 3: counting sort with key range M vs qsort, ns per SORT CALL (not per elem)
      n         M     counting        qsort    insertion  cnt bytes
     16       256          179          214           97       1024
     16      4096         1317          221          103      16384
     16     65536        18557          213           96     262144
     16   1048576       262417          206          103    4194304
    256       256         1023         6712         5771       1024
    256      4096         2551         7000         5897      16384
    256     65536        20178         6705         5811     262144
    256   1048576       290012         6812         5906    4194304
   4096       256         2972        98440      1051158       1024
   4096      4096        15896       152907      1015092      16384
   4096     65536        41491       158375      1012733     262144
   4096   1048576       306502       156884      1020425    4194304

표 4: tiny n, uint32 random, ns per SORT CALL
      n    insertion        qsort     radix8x4
      8           37           70          308
     32          232          499          370
    128         1751         2853          551
    512        19463        14444         1604
```

- n = 16 이면 12비트 ADC(M = 4096)에서도 계수 정렬(1317 ns)이 삽입 정렬(103 ns)보다 **13 배 느리다**. M = 2²⁰ 이면 qsort 의 1270 배, 그리고 4 MB 카운트.
- qsort 와 비교하면 이 맥에서는 **n/M = 1/16 일 때 이기고 1/256 일 때 졌다**: n = 256 은 M = 4096 에서 이기고(2.7 배) M = 65536 에서 진다(3 배). n = 4096 은 M = 65536 에서 이기고(3.8 배) M = 2²⁰ 에서 진다(2 배). 삽입 정렬이 경쟁 상대인 아주 작은 n(16)에서는 M = 256 에서도 진다(179 vs 97 ns).
- 기수 정렬도 고정비(4 패스 × 1 KB 카운트 지우기 + 256 칸 누적)가 있어 n = 8 에서는 삽입 정렬의 8 배 느리다. 이 맥에서는 n ≈ 32~128 사이에서 역전된다. 실제 라이브러리 정렬이 작은 구간을 삽입 정렬로 넘기는 이유와 같다 (ALGO-B05).

### 펌웨어 — 카운트 배열을 어디에 두나 (`demo_stack.c`)

같은 정렬을 세 가지 배치로 Cortex-M4 용으로 컴파일해 프레임을 봤다 (실제 결과. 위 두 줄은 어셈블리 프롤로그를 한 줄로 모은 발췌, 아래 세 줄은 .su 파일 그대로):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S demo_stack.c -o - | grep -E 'sub(w|\.w)?\s+sp'
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -c -fstack-usage demo_stack.c -o /tmp/ALGO-B08_ds.o && cat /tmp/ALGO-B08_ds.su
```

```text
count_sort_local:   push.w {r4, r5, r6, r7, r8, r9, r10, r11, lr} / sub.w sp, sp, #16384 / sub sp, #4
radix_pass_local:   push.w {r4, ..., r11, lr} / subw sp, sp, #1028
demo_stack.c:14:count_sort_local	16424	static
demo_stack.c:25:count_sort_static	32	static
demo_stack.c:34:radix_pass_local	1064	static
```

`size` 로 본 오브젝트: text 1544, bss **16384** (static 버전의 `s_cnt`). 정리하면:

| 배치 | 스택 | RAM 고정 점유 | 재진입 | 언제 |
|---|---|---|---|---|
| 지역 `uint32_t cnt[4096]` | **16 424 B** | 0 | 예 | MCU 에선 사실상 금지. CubeMX 링커 스크립트의 흔한 기본 `_Min_Stack_Size = 0x400`(1 KB)의 16 배, FreeRTOS 태스크 스택(보통 수백 B~수 KB)도 즉시 넘친다 → MPU 가드가 없으면 조용한 메모리 오염, 있으면 HardFault/MemManage (ALGO-B11) |
| 함수 안/파일 `static` | 32 B | .bss 16 KB (링커 맵에 보임) | **아니오** — 두 태스크가 동시에 부르면 카운트가 섞인다 (C-A04) | 호출자가 하나뿐일 때 |
| 호출자가 주는 작업 공간 (이 레슨 API) | 상수 | 호출자가 결정 | 호출자 책임 | 라이브러리 함수의 기본형: 메모리 위치가 API 에 드러난다 |
| uint16 카운트 | — | 8 KB | — | n ≤ 65535 보장될 때 (Part C1) |
| 기수 256 칸 지역 배열 | **1 064 B** | 0 | 예 | 현실적인 선택. 12비트 키면 8비트 + 4비트 두 패스(연습 4 의 건너뛰기) 또는 4비트 자리 3 패스(카운트 16 칸 = 64 B) |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">SRAM 20 KB MCU (예: STM32F103C8) 에서 버퍼 크기 비교 — 1 KB = 30 px</text>
  <line x1="130" y1="35" x2="130" y2="215" stroke="currentColor"/>
  <line x1="730" y1="35" x2="730" y2="215" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="726" y="232" fill="currentColor" text-anchor="end">SRAM 전체 20 KB</text>
  <line x1="160" y1="35" x2="160" y2="215" stroke="currentColor" stroke-dasharray="2 3"/>
  <text x="164" y="232" fill="currentColor">기본 스택 1 KB</text>
  <text x="10" y="58" fill="currentColor">cnt uint32</text>
  <rect x="130" y="42" width="480" height="24" class="f-warn" stroke="currentColor"/>
  <text x="616" y="60" fill="currentColor">16 KB</text>
  <text x="10" y="96" fill="currentColor">cnt uint16</text>
  <rect x="130" y="80" width="240" height="24" class="f-mut" stroke="currentColor"/>
  <text x="376" y="98" fill="currentColor">8 KB (n ≤ 65535)</text>
  <text x="10" y="134" fill="currentColor">radix cnt</text>
  <rect x="130" y="118" width="30" height="24" class="f-ok" stroke="currentColor"/>
  <text x="166" y="136" fill="currentColor">1 KB</text>
  <text x="10" y="172" fill="currentColor">scratch</text>
  <rect x="130" y="156" width="120" height="24" class="f-acc" stroke="currentColor"/>
  <text x="256" y="174" fill="currentColor">4 KB = uint32 1024 개 (n 에 비례)</text>
  <text x="10" y="205" fill="currentColor">4비트 자리</text>
  <rect x="130" y="192" width="2" height="18" class="f-ok" stroke="currentColor"/>
  <text x="170" y="206" fill="currentColor">64 B (16 칸, 12비트 키 3 패스)</text>
</svg>
```

또 하나: MCU 에는 나눗셈기가 없을 수 있지만(Cortex-M0/M0+) 계수·기수 정렬은 **시프트·마스크·덧셈만** 쓴다. Cortex-M4 코어에는 데이터 캐시가 없고 내부 SRAM 접근 시간이 거의 균일하므로, 맥에서 기수 정렬의 발목을 잡는 "흩어 쓰기 캐시 미스" 가 MCU 에선 덜 문제다 (대신 RAM 자체가 작다).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 30/30 passed, 약 0.2 초
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_san && ./solution_san   # 30/30, 보고 없음
```

## 7. 자주 하는 실수

**1. 끝 위치(포함 누적합)를 쓰면서 앞에서부터 훑는다 → 불안정.**

```c
for (d = 1; d < 256; d++) end[d] += end[d - 1];
for (i = 0; i < n; i++) dst[--end[digit(src[i])]] = src[i];   /* 같은 자리값끼리 순서가 뒤집힌다 */
```

계수 정렬 하나만 보면 결과가 "정렬돼" 보여서 지나치기 쉽다. 기수 정렬 안에 넣으면 실제로 26/30 (5. 풀이). → 포함 누적합이면 **뒤에서부터**(`for i = n-1 … 0`), 배타적 누적합이면 앞에서부터 `dst[pos[d]++]`.

**2. 포함 누적합을 시작 위치로 착각한다.** `cnt[v] += cnt[v-1]` 후 `dst[cnt[key]++] = src[i]` — 모든 원소가 한 칸씩 오른쪽으로 밀리고 가장 큰 키는 dst 끝을 넘어 쓴다. 정확한 크기의 dst 로 돌리면 ASan 이 바로 잡는다 (실제): `ERROR: AddressSanitizer: stack-buffer-overflow … WRITE of size 2`. 하네스(여유 있는 정적 배열)에서는 넘침이 안 보이고 A2 검사 3 개만 FAIL(27/30) — 테스트 버퍼가 크면 오버런이 숨는다.

**3. 범위 검사 없이 `cnt[a[i]]++`.** DMA 버퍼 초기화 전 값(0xFFFF), 비트 정렬이 다른 ADC(왼쪽 정렬 16비트 결과) 하나면 카운트 배열 밖에 쓴다. 실제: `ERROR: AddressSanitizer: global-buffer-overflow … in count_sort_adc_inplace`. MCU 에는 ASan 이 없으니 그냥 옆 변수를 망가뜨린다. → 히스토그램 루프에서 거부(쓰기 전에).

**4. ping-pong 에서 포인터 맞바꾸기를 빠뜨린다.** 네 패스가 모두 a → tmp 로 가서 마지막 패스(최상위 바이트)만 남는다. 실제 26/30. 반대로 맞바꾸고 **홀수 패스**면 결과가 tmp 에 있는데 a 를 읽는다 → 연습 3 처럼 마지막에 `if (src != a) memcpy(a, src, …)`.

**5. uint16 카운트를 n 가드 없이 쓴다.** 위 실측: 진짜 중앙값 100 대신 300 을 "성공" 으로 반환. → `n > UINT16_MAX` 거부, 또는 uint32 카운트.

**6. 16 KB 카운트 배열을 함수 지역 변수로.** 맥에서는 8 MB 스택이라 아무 일도 없어서 호스트 테스트가 다 통과한다. MCU 에서는 16 424 B 프레임(위 `.su`). → 호출자 작업 공간 또는 static(재진입 주의).

**7. 부호 있는 키를 그대로 인덱스/자리값으로 쓴다.** `int16_t` -5 를 `cnt[-5]` 에 쓰면 배열 앞을 망가뜨리고, int32 를 `(uint32_t)x >> 24` 로 자르면 음수(0x80…–0xFF…)가 양수 뒤로 간다. → 오프셋을 더하거나(연습 1) 부호 비트를 뒤집는다(연습 2).

**8. 카운트를 패스마다 0 으로 지우지 않는다.** `static` 카운트를 재사용하면서 memset 을 빠뜨리면 두 번째 호출부터 위치가 밀린다. .bss 가 부팅 때 0 이라는 건 **첫 호출**에만 해당한다.

## 8. 면접 Q&A

<details><summary>Q1. 비교 정렬은 Ω(n log n) 이라던데, 계수 정렬은 어떻게 O(n) 이죠? 하한을 어긴 건가요?</summary>

하한은 "비교만으로 순서를 알아내는" 모델의 결과다: n! 가지 순서를 결정 트리의 잎으로 구별해야 하고 높이가 h 인 이진 트리의 잎은 2ʰ 개 이하이므로 h ≥ log₂ n! = Θ(n log n). 계수 정렬은 키 값을 **주소로** 쓴다(`cnt[key]++`) — 한 번의 연산이 K 갈래로 갈라지는 셈이라 비교 모델 밖이다. 공짜는 아니다: 시간 O(n + K), 공간 O(K). K 가 n 보다 훨씬 크면(32비트 키, K = 2³²) 쓸 수 없고, 그래서 키를 자리로 쪼갠 기수 정렬이 O(w/r · (n + 2ʳ)) 로 이어 받는다. 꼬리: "그럼 기수 정렬은 항상 O(n)?" → 키 폭 w 가 고정일 때만. 서로 다른 n 개 키를 담으려면 w ≥ log₂ n 이어야 하니 엄밀히는 Θ(n · w / r) 이고, r ≈ log₂ n 으로 잡으면 Θ(n · w / log n) 이다(CLRS 보조정리 8.4 의 논의).

</details>

<details><summary>Q2. LSD 기수 정렬에서 각 패스가 왜 안정해야 하나요? 불안정하면 어떻게 깨지는지 예로 보여 주세요.</summary>

불변식 "패스 k 뒤 아래 8k 비트 기준 정렬" 의 귀납 단계가 정확히 안정성을 쓴다: 자리 k 가 같은 두 원소는 패스 k 가 순서를 바꾸지 말아야 앞 패스가 정한 (아래 자리 기준) 순서가 살아남는다. 예: {0x0102, 0x0201, 0x0101, 0x0202} 에서 불안정 패스(끝 위치 + 앞에서부터)는 byte1 패스에서 자리 01 묶음 (0101, 0102) 을 (0102, 0101) 로 뒤집어 결과가 {0102, 0101, 0202, 0201}. 꼬리: "모든 패스가 다 안정해야 하나?" → 맨 키라면 **첫 패스만은** 불안정해도 된다. 첫 패스 전에는 지켜야 할 앞 순서가 없고, "아래 8 비트 기준 정렬" 은 같은 자리값끼리 어떤 순서여도 성립하기 때문이다. 실제로 shift 0 패스만 불안정 버전으로 바꾸면 B2 테스트가 전부 통과하고, shift 24 패스만 바꾸면 26/30 으로 깨진다 (이 레슨 작업 중 실행). 마지막 패스의 같은 자리 묶음은 **위 바이트만** 같은 원소들이라 그 안의 아래 자리 순서를 지켜야 한다. 레코드를 입력 순서까지 안정하게 정렬하려면 첫 패스도 안정해야 한다.

</details>

<details><summary>Q3. LSD 와 MSD 기수 정렬의 차이는? 언제 MSD 를 쓰나요?</summary>

LSD: 아래 자리부터, 매 패스 전체 배열, 모든 키가 같은 길이일 때 단순하고 분기 적음. 각 패스 안정 필수, scratch n 칸. MSD: 위 자리로 256 버킷으로 나눈 뒤 버킷마다 다음 자리를 재귀(또는 명시적 스택)로. 길이가 다른 키(문자열), 키 공간이 희소해 **위 몇 자리로 이미 다 갈리는** 경우 일찍 끝낼 수 있고, 버킷 안 원소 수가 작으면 삽입 정렬로 넘긴다. 제자리(in-place) 변형인 American flag sort(McIlroy·Bostic·McIlroy 1993)는 scratch 가 필요 없지만 불안정하다. 펌웨어 관점: MSD 는 재귀 깊이(최대 자리 수 = 4)와 버킷 스택이 생긴다 — 깊이는 작지만 프레임마다 256 칸 카운트를 두면 4 KB 다.

</details>

<details><summary>Q4. 자리 폭을 8 비트 말고 11 비트나 16 비트로 하면요?</summary>

32비트 키 기준: 8비트 → 4 패스, 카운트 1 KB. 11비트 → 3 패스(11+11+10), 카운트 8 KB, 홀수 패스라 마지막에 복사 1 번(연습 3). 16비트 → 2 패스, 카운트 256 KB — L1 데이터 캐시보다 크고 MCU 에선 RAM 초과. 패스가 줄면 메모리 트래픽(n 읽기 + n 쓰기 / 패스)이 줄지만, 버킷이 많아지면 흩어 쓰기 대상(write stream)이 늘어 캐시/TLB 미스가 늘어난다. 큰 n 에서 11비트가 빠르다는 보고(예: Herf 의 "Radix Tricks")가 있지만 플랫폼마다 다르니 재야 한다. 이 레슨은 MCU 를 기준으로 8비트(1 KB)를 고른다. 꼬리: "히스토그램 4 개를 한 번에?" → 첫 읽기 때 네 자리 히스토그램을 모두 만들면(4 KB) 패스마다 히스토그램용 읽기가 사라진다 — 연습 4.

</details>

<details><summary>Q5. 4096 칸 카운트 배열을 MCU 어디에 두겠어요?</summary>

uint32 면 16 KB. 지역 변수는 금지(Cortex-M4 프레임 16 424 B 를 실제로 확인, 기본 스택 1 KB). 선택지: (1) 호출자가 주는 작업 공간 — 메모리 위치와 수명이 API 에 드러나고 재진입은 호출자가 관리. (2) static/.bss — 간단하지만 재진입 불가, RTOS 두 태스크가 부르면 뮤텍스가 필요. (3) 카운트를 uint16 로(8 KB) — n ≤ 65535 를 보장할 때만. (4) 아예 기수 정렬로: 8비트 + 4비트 두 패스면 카운트 1 KB 지만 scratch n 칸이 필요하다 — "카운트 16 KB vs scratch 2n B" 를 n 으로 비교해 고른다(uint16 샘플 n = 1024 면 scratch 2 KB). 링커 맵(.map)으로 .bss 가 실제로 얼마인지 확인하고, 스택은 `-fstack-usage`(GCC 의 .su)나 워터마크(FreeRTOS `uxTaskGetStackHighWaterMark`)로 검증한다.

</details>

<details><summary>Q6. 계수 정렬이 qsort 보다 느린 경우는 언제인가요?</summary>

비용이 n + K 이므로 n ≪ K 일 때. 이 맥 실측: n = 16, K = 4096 에서 계수 1317 ns vs 삽입 103 ns(13 배 느림), K = 2²⁰ 이면 qsort 의 1270 배. qsort 상대로는 n/K = 1/16 에서 이기고 1/256 에서 졌다. 또 카운트 배열이 캐시를 넘으면(K = 2²⁰ → 4 MB) 원소마다 캐시 미스. 그리고 키가 정수가 아니거나 비교 함수로만 순서가 정의되면(문자열 사전순 일부, 사용자 정의 우선순위) 애초에 못 쓴다. 꼬리: "기수 정렬은?" → 고정비 4 × (1 KB 지우기 + 256 누적) 때문에 n = 8 에서 삽입 정렬의 8 배 느렸고 n ≈ 32~128 에서 역전.

</details>

<details><summary>Q7. 부호 있는 정수나 float 는 어떻게 기수 정렬하나요?</summary>

int32: 2의 보수 비트를 uint32 로 보고 **부호 비트만 뒤집으면**(x ^ 0x80000000) INT32_MIN → 0, −1 → 0x7FFFFFFF, 0 → 0x80000000 으로 순서가 보존된다 (연습 2). (`(uint32_t)x` 변환은 값 + 2³² 로 정의돼 있어 안전, C11 6.3.1.3p2.) float(IEEE 754): 양수는 비트 패턴 순서 = 값 순서, 음수는 반대이므로 "음수면 모든 비트 반전, 양수면 부호 비트만 반전" 매핑을 쓴다(NaN 은 별도 처리). 12비트 차동 ADC(−2048..2047)는 2048 을 더해 0..4095 로 옮기면 계수 정렬 그대로 (연습 1). 꼬리: "타임스탬프 랩어라운드는?" → 기준 시각을 빼서 경과 시간으로 (Part C2). `(int32_t)(a - b) < 0` 비교는 두 값이 2³¹ 안에 있을 때만 맞고, 2³¹ 이상인 uint32 를 int32 로 바꾸는 건 구현 정의 동작(C11 6.3.1.3p3)이라 주의.

</details>

<details><summary>Q8. ISR 에서 샘플이 들어오는데, 정렬은 언제 어디서 하죠?</summary>

ISR 에서 정렬하지 않는다 — Θ(n + K) 라도 수십 µs~ms 라 다른 인터럽트 지연을 늘린다. 패턴: (1) DMA 더블 버퍼(ping-pong)로 블록을 받고, 반쪽 완료 인터럽트에서 태스크에 알린 뒤 태스크가 다른 반쪽을 처리. (2) 중앙값·백분위만 필요하면 ISR 에서 `cnt[sample]++` 만 하는 **스트리밍 히스토그램**(샘플당 O(1), 결정적) — 태스크가 읽을 때는 히스토그램 두 벌을 바꿔 끼우거나(swap) 인터럽트를 잠깐 막는다 (C-M14). 카운트 넘침은 블록 크기로 막는다(Part C1). 계수/기수 정렬의 장점은 실행 시간이 입력 분포에 거의 무관해 WCET 를 잡기 쉽다는 것 — quicksort 최악 O(n²)(ALGO-B06)와 대비된다.

</details>

## 9. 추가 연습

### 연습 1 — 부호 있는 12비트 ADC 계수 정렬

차동 입력 ADC 는 −2048..2047 을 int16 로 준다. `count_sort_i12(int16_t *a, size_t n, uint32_t *cnt)` 를 구현하라 (범위 밖이면 -1, a 그대로).

<details><summary>답 보기</summary>

키 = x − (−2048) = x + 2048 (0..4095). 다시 쓸 때는 반대로 u + (−2048). 범위 검사는 `a[i] < -2048 || a[i] > 2047`. `cnt[a[i]]` 를 그대로 쓰면 음수 인덱스 → 배열 앞 메모리 파괴 (실수 7). 무작위 200 회를 qsort 와 비교, {2047, −2048, 0, −1, 1} → {−2048, −1, 0, 1, 2047}, −2049 거부.

</details>

### 연습 2 — int32 기수 정렬 (부호 비트 뒤집기)

`radix_sort_i32(int32_t *a, int32_t *tmp, size_t n)` — 8비트 4 패스, 결과는 a.

<details><summary>답 보기</summary>

자리값을 `i32_key(x) = (uint32_t)x ^ 0x80000000u` 에서 뽑는다. 원소는 원래 값 그대로 옮기고 키만 매핑한다(다시 되돌릴 필요 없음). 경계 {0, INT32_MIN, −1, INT32_MAX, 1, INT32_MIN} → {MIN, MIN, −1, 0, 1, MAX}. 뒤집기를 빼면 `(uint32_t)-1 = 0xFFFFFFFF > 1` 이라 음수가 맨 뒤로 간다 — practice.c 가 그 대소 관계도 확인한다.

</details>

### 연습 3 — 11비트 자리 3 패스와 홀짝

`radix_sort_u32_11(a, tmp, n)` — 자리 폭 11, 11, 10. 반환값 = 마지막 복사 횟수.

<details><summary>답 보기</summary>

shift = 0, 11, 22, 마스크 0x7FF (마지막 자리는 10 비트뿐이라 같은 마스크로도 위 비트가 0). 3 패스 = 홀수라 a→tmp→a→tmp 로 끝나 결과가 tmp 에 있다 → `if (src != a) memcpy(a, src, n * 4)`. 카운트 2048 × 4 = 8 KB 는 스택에 두지 않고 static 으로 (재진입 불가 주석). 무작위 100 회 모두 복사 1 번 (copies == 100).

</details>

### 연습 4 — 한 번 읽어 히스토그램 4 개 + 쓸모없는 패스 건너뛰기

`radix_sort_u32_skip(a, tmp, n, &passes)` — 어떤 자리의 값이 모든 원소에서 같으면 그 패스는 순서를 안 바꾸므로 건너뛴다. 실행한 패스 수를 돌려주고, 결과가 a 에 오도록 홀짝을 맞춘다.

<details><summary>답 보기</summary>

첫 읽기에서 `cnt[4][256]`(4 KB)를 모두 채운다. 패스 p 에서 `cnt[p][digit(a[0])] == n` 이면 건너뜀 (모든 원소가 그 자리값). 실행한 패스가 홀수 번이면 결과가 tmp 에 있으므로 마지막에 복사. uint32 에 담긴 12비트 ADC 값은 byte2·byte3 가 항상 0 → **2 패스, 복사 없음**. 0xC0FFEExx 처럼 아래 바이트만 다르면 1 패스 + 복사 1 번. 모두 같은 값이면 0 패스. 이게 "12비트 키에 8비트 기수 정렬" 을 해도 4 패스 비용을 다 내지 않는 방법이다.

</details>

### 연습 5 — LeetCode 75 Sort Colors

값이 0, 1, 2 뿐인 배열을 정렬하라. `sort_colors(int *a, size_t n)` (범위 밖이면 -1).

<details><summary>답 보기</summary>

K = 3 인 계수 정렬: 한 번 세고 한 번 다시 쓴다 — 2 패스, 추가 메모리 3 칸. 면접관이 "한 번만 훑어서?" 라고 꼬리질문하면 Dijkstra 의 Dutch national flag 3-way 파티션(투 포인터 세 개, ALGO-B09 / ALGO-B06 의 3-way 파티션)으로 넘어간다. [2,0,2,1,1,0] → [0,0,1,1,2,2].

</details>

같은 계열로 더 풀어 볼 문제: LeetCode 164 Maximum Gap (기수 정렬 또는 버킷으로 O(n)), LeetCode 912 Sort an Array (계수 정렬은 값 범위가 주어질 때만).

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 15/15 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_san && ./practice_san   # 15/15, 보고 없음
```

## 10. 핵심 정리

- 계수 정렬 = 키를 인덱스로: 히스토그램 → (맨 키) 다시 쓰기 / (레코드) 배타적 누적합 → `dst[pos[k]++]` 앞에서부터 = 안정. Θ(n + K) 시간, K 칸 메모리. 비교 모델 밖이라 n log n 하한과 무관.
- 범위 검사는 히스토그램 루프에서, 쓰기 전에 → 거부해도 입력 보존. 검사 없는 `cnt[a[i]]++` 는 메모리 파괴.
- LSD 기수 정렬: 8비트 자리 4 패스, 불변식 "패스 k 뒤 아래 8k 비트 정렬", 귀납 단계가 **안정성**을 쓴다. 불안정 패스(끝 위치 + 앞에서부터)는 26/30 으로 깨진다.
- ping-pong: a↔tmp 포인터만 맞바꾼다. 짝수 패스면 a 에서 끝, 홀수면 복사 1 번.
- 실측(이 맥): n = 10⁶ 에서 계수 ≈ qsort 의 99 배, 기수 ≈ 21 배. 하지만 n = 16, K = 4096 이면 계수가 삽입 정렬보다 13 배 느리다 — 비용은 n + K.
- 메모리: 4096 × uint32 = 16 KB (Cortex-M4 지역 배열이면 16 424 B 프레임), uint16 카운트 8 KB 는 n ≤ 65535 일 때만, 기수 카운트 256 칸 = 1 KB 가 현실적. 기수의 진짜 비용은 scratch 4n B.
- uint16 카운트 넘침은 조용하다: 실측에서 중앙값 100 대신 300 을 반환. 가드 = n ≤ UINT16_MAX.
- 랩어라운드 타임스탬프는 `t - t_ref` 키로, 같은 시각은 안정 정렬로 도착 순서 유지.

## 참고자료

- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms* (3rd ed.), 8장 "Sorting in Linear Time" — 8.1 비교 정렬 하한(결정 트리), 8.2 계수 정렬(뒤에서부터 배치), 8.3 기수 정렬(보조정리 8.3·8.4: 자리 폭 r 선택).
- D. E. Knuth, *The Art of Computer Programming*, Vol. 3, 5.2 "Internal Sorting" 의 distribution counting, 5.2.5 "Sorting by Distribution" — 기수 정렬의 고전 서술.
- P. M. McIlroy, K. Bostic, M. D. McIlroy, "Engineering Radix Sort", *Computing Systems* 6(1), 1993 — MSD, American flag sort(제자리). 서지는 기억에 의존 — 확인 권장.
- M. Herf, "Radix Tricks" (2001, 웹 문서) — float 키 매핑, 히스토그램 한 번에 만들기, 11비트 자리. 출처 URL 은 확인하지 못함.
- ISO/IEC 9899:2011 (C11) 6.2.5p9 (부호 없는 산술은 2ⁿ 로 나눈 나머지), 6.3.1.3 (정수 변환: 부호 없는 쪽은 정의됨, 부호 있는 쪽으로의 범위 밖 변환은 구현 정의).
- Clang 사용자 매뉴얼 — `-fstack-usage` (함수별 .su 파일): <https://clang.llvm.org/docs/ClangCommandLineReference.html>
- AlgoMonster 노트 M01 §16 Sorting Summary (Don 정리본) — 정렬 비교표의 counting sort 줄, "값이 0–100 인 정수 백만 개" 예.
- LeetCode 75 Sort Colors, LeetCode 164 Maximum Gap, LeetCode 912 Sort an Array.
- 관련 레슨: ALGO-B01 (실측으로 Big-O), ALGO-B05 (안정성·삽입 정렬), ALGO-B06 (퀵소트 최악·3-way 파티션), ALGO-B07 (머지소트, 안정 O(n) 추가 메모리), ALGO-B09 (투 포인터), ALGO-B11 (스택 넘침), ALGO-M13 (누적합), ALGO-A12 (외부 정렬), ARCH-M03 (분기 예측: 정렬 vs 무작위), C-B02 (정수 승격), C-B11 (.bss/스택 배치), C-A04 (재진입성), C-M14 (ISR 공유 데이터), DS-A11 (AoS vs SoA)
