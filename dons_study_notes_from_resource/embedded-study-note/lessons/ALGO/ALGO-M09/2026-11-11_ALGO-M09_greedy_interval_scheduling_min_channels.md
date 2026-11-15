---
id: ALGO-M09
time: 75분
updated: 2026-11-11
---
# ALGO-M09 그리디: 구간 스케줄링과 최소 자원 수

## 학습 목표

- **반개구간(half-open interval) `[s, e)`** 규약 하나로 "겹침 / 맞닿음" 을 정확히 구분하고, 그 규약이 코드의 `<` 와 `<=` 를 하나씩 어떻게 정하는지 말할 수 있다.
- 최대 비겹침 선택(activity selection)을 **끝 시각이 가장 이른 것부터** 고르는 그리디로 풀고, 그것이 최적인 이유를 **교환 논증(exchange argument)** 으로 증명한다. 이른 시작·짧은 것 먼저·충돌 적은 것 먼저가 왜 틀리는지 **반례**로 보인다.
- 필요한 자원(DMA 채널, 회의실) 최소 개수 = **최대 겹침 깊이(depth)** 임을 두 방향(하한: 깊이만큼은 꼭 필요 / 상한: 시작 순서 그리디가 깊이를 넘지 않음)으로 증명하고, 스위프(sweep)와 최소 힙(min-heap) 두 가지로 구현한다.
- 구간 병합(merge intervals)을 불변식으로 짜고, 포함된 구간·맞닿은 구간·`UINT32_MAX` 끝점을 처리한다.
- 모든 함수를 부분집합 전수·셀 비트맵·백트래킹 색칠 같은 **브루트포스**와 대조해서 "그리디가 맞다" 를 증명과 테스트 양쪽으로 확인한다.

## 1. 문제

그리디는 면접에서 "코드는 10줄, 증명은 한 문단" 인 문제다 — 면접관은 코드보다 "왜 그 기준으로 정렬했죠? 반례는요?" 를 묻고, 펌웨어에서는 그 답이 DMA 채널·타이머·버스 슬롯 같은 **개수가 정해진 하드웨어 자원**을 몇 개 써야 하는지로 바로 이어진다.

이 레슨의 모든 구간은 **반개구간 `[s, e)`, `s < e`**, 시각은 `uint32_t`(예: µs 틱)다. `[1,3)` 과 `[3,5)` 는 **겹치지 않는다** (3 에서 앞 전송이 끝나고 뒤 전송이 시작 — 같은 채널을 이어 쓸 수 있다). 두 구간이 겹친다 ⇔ `a.s < b.e && b.s < a.e`. 이 규약 하나가 코드의 비교 연산자를 모두 정한다 (7. 실수 2·4·5 가 전부 여기서 나온다). 반개구간을 고르는 이유는 ALGO-B02 와 같다: 길이 = `e - s`, 맞닿음이 겹침이 아님, 빈 구간이 없음.

아래 8개 구간이 이 레슨 내내 쓰는 예제다. 끝 시각 순서로 위에서 아래로 그렸다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">예제 8 구간 (끝 순서) — 녹색 = 끝이 이른 그리디가 고른 것, 점선 = 깊이 4 인 시각 t = 4</text>
  <g stroke="currentColor">
    <rect x="170" y="40" width="100" height="18" class="f-ok"/>
    <rect x="220" y="66" width="150" height="18" class="f-mut"/>
    <rect x="120" y="92" width="300" height="18" class="f-mut"/>
    <rect x="320" y="118" width="100" height="18" class="f-ok"/>
    <rect x="270" y="144" width="200" height="18" class="f-mut"/>
    <rect x="370" y="170" width="150" height="18" class="f-mut"/>
    <rect x="470" y="196" width="100" height="18" class="f-ok"/>
    <rect x="520" y="222" width="100" height="18" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="54">I1 [1,3)</text><text x="10" y="80">I2 [2,5)</text><text x="10" y="106">I0 [0,6)</text><text x="10" y="132">I7 [4,6)</text>
    <text x="10" y="158">I3 [3,7)</text><text x="10" y="184">I4 [5,8)</text><text x="10" y="210">I5 [7,9)</text><text x="10" y="236">I6 [8,10)</text>
    <text x="640" y="54">take</text><text x="640" y="80">skip 2&lt;3</text><text x="640" y="106">skip 0&lt;3</text><text x="640" y="132">take</text>
    <text x="640" y="158">skip 3&lt;6</text><text x="640" y="184">skip 5&lt;6</text><text x="640" y="210">take</text><text x="640" y="236">skip 8&lt;9</text>
  </g>
  <line x1="320" y1="34" x2="320" y2="248" stroke="currentColor" stroke-dasharray="5 4"/>
  <line x1="120" y1="258" x2="620" y2="258" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="120" y="274">0</text><text x="170" y="274">1</text><text x="220" y="274">2</text><text x="270" y="274">3</text><text x="320" y="274">4</text><text x="370" y="274">5</text>
    <text x="420" y="274">6</text><text x="470" y="274">7</text><text x="520" y="274">8</text><text x="570" y="274">9</text><text x="620" y="274">10</text>
  </g>
  <text x="120" y="294" fill="currentColor" font-size="12">최대 비겹침 = 3 (I1, I7, I5). t = 4 에는 I2, I0, I7, I3 가 동시에 활성 → 채널 최소 4 개.</text>
</svg>
```

### Part A — 최대 비겹침 선택과 병합

1. `size_t max_nonoverlap(const ival_t *iv, size_t n, uint16_t *work, uint16_t *pick)` — 서로 겹치지 않는 구간을 **최대 개수** 고른다. 고른 원래 인덱스를 `pick[]` 에 시간 순으로 쓰고 개수를 돌려준다. `work` 는 정렬용 n 칸 작업 공간 (malloc 없음). 예: CLRS 16.1 의 11개 활동 → 4개 `{a1, a4, a8, a11}`.
2. `size_t merge_intervals(const ival_t *iv, size_t n, uint16_t *work, ival_t *out)` — 겹치거나 **맞닿는** 구간을 합쳐 시작 순으로 쓴다. 결과는 정규형: `out[k].e < out[k+1].s` (사이에 진짜 빈틈). 예: `[1,3) [2,6) [8,10) [15,18)` → `[1,6) [8,10) [15,18)`, `[1,4) [4,5)` → `[1,5)`.

### Part B — 최대 겹침 깊이와 잘못된 그리디

1. `unsigned max_depth(const ival_t *iv, size_t n, uint32_t *starts, uint32_t *ends, uint32_t *at)` — 어떤 시각에 동시에 활성인 구간 수의 최댓값(**깊이**)과 그 값에 처음 도달한 시각 `*at`. 시작/끝을 따로 정렬하고 두 포인터로 훑는다. 같은 시각에 끝과 시작이 있으면 **끝을 먼저** 처리한다.
2. 깊이가 필요한 자원 수의 **하한**임을 증명한다. 그리고 틀린 그리디 세 개(이른 시작 / 짧은 것 먼저 / 충돌 적은 것 먼저)의 반례를 찾고 무작위 입력에서 실패율을 잰다 (하네스 + `demo_counterexamples.c`).

### Part C — DMA 채널 배정

1 ms 프레임 안에 ADC 스캔, IMU SPI, GPS UART, I2S 오디오, 플래시 읽기/쓰기, 로그 UART, I2C, memcpy 전송이 시간 구간으로 잡혀 있다. 전송 하나는 그 구간 동안 DMA 채널 하나를 점유한다.

- `int dma_assign(const ival_t *x, size_t n, unsigned hw_ch, uint16_t *work, uint8_t *chan)` — 시작 순서로 보며 **(끝 시각, 채널) 최소 힙**의 맨 위 채널이 이미 비었으면 재사용, 아니면 새 채널. 쓴 채널 수를 돌려주고, `hw_ch` 개로 부족하면 `-1`. 쓴 채널 수가 **정확히 깊이**(= 최소)임을 증명하고 백트래킹 최소 색칠과 대조한다.
- 같은 채널의 두 전송이 겹치지 않는지 검증기(`chan_valid`)로 확인하고, 채널이 하나 모자랄 때(`hw_ch = 깊이 - 1`) 거절되는지 본다.

## 2. Problem (English)

> **Interval greedy toolkit.** Intervals are half-open `[s, e)` with `uint32_t` endpoints and `s < e`; `[1,3)` and `[3,5)` do **not** overlap. No dynamic allocation, no recursion; the caller provides scratch buffers of `n` entries. `n <= 1024`.
>
> 1. `max_nonoverlap(iv, n, work, pick)` — return the maximum number of pairwise non-overlapping intervals and write their indices to `pick` in time order. (LeetCode 435 asks for `n - this`.)
> 2. `merge_intervals(iv, n, work, out)` — merge overlapping **or touching** intervals; output sorted by start with `out[k].e < out[k+1].s`.
> 3. `max_depth(iv, n, starts, ends, &at)` — the maximum number of intervals active at one instant and the earliest instant it occurs.
> 4. `dma_assign(x, n, hw_ch, work, chan)` — assign each transfer a channel so that transfers on one channel never overlap, using the minimum number of channels; return that number, or `-1` if more than `hw_ch` are needed.
>
> Examples: `[[1,2],[2,3],[3,4],[1,3]]` → max non-overlapping 3. `[[1,3],[2,6],[8,10],[15,18]]` → merged `[[1,6],[8,10],[15,18]]`. `[[0,30],[5,10],[15,20]]` → depth 2 (at 5), 2 channels. `[[0,1],[1,2],[2,3]]` → depth 1, 1 channel.
>
> Follow-ups: prove each greedy; give counterexamples for "earliest start first" and "shortest first"; what if only `k` channels exist; what if intervals have weights.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 함수 4개의 본문. 정렬(`sort_idx`, `sort_u32` — 인덱스 힙 정렬, 재귀·malloc 없음)과 최소 힙(`slot_push`, `slot_pop` — DS-B12에서 자세히)은 **주어진 도구**다. 스텁은 0 을 돌려준다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:315  k == 4 && g_pick[0] == 0 && g_pick[1] == 3 && g_pick[2] == 7 && g_pick[3] == 10
FAIL starter.c:319  4 - max_nonoverlap(e1, 4, g_work, g_pick) == 1
…
exhaustive grid: 13616 instances, mismatches select=13615 merge=13615 depth=13615 dma=13615
…
random n=1000: 36 runs (3 coordinate maps), depth up to 17 (6 runs > MAX_CH=16), mismatches select=36 merge=36 depth=36 dma=36
Part C frame: 10 transfers, channels used=0, depth=0 at t=0 us, brute chromatic=5
…
8/42 passed
```

(exit 1. 통과한 8개: 빈 입력 4개 — 스텁의 0 이 우연히 정답 — 와 테스트 쪽 그리디 규칙 통계 2개, "두 경로 모두 시험됨" 1개, 채널 수 = 깊이(둘 다 0) 1개.)

## 4. 힌트

<details><summary>힌트 1 — 무엇으로 정렬할지는 "남는 여유" 로 정한다</summary>

하나를 골랐을 때 나머지가 쓸 수 있는 시간은 `[그 구간의 끝, ∞)` 이다. 그러면 무엇을 먼저 골라야 미래가 가장 넓게 남을까? 시작이나 길이가 아니라 **끝**이다.

</details>

<details><summary>힌트 2 — 끝 순서로 훑으면 "마지막으로 고른 것" 하나만 기억하면 된다</summary>

끝 순서로 고르면 고른 것들의 끝은 오름차순이다. 새 후보가 앞서 고른 모든 것과 안 겹칠 조건 = 마지막으로 고른 것의 끝 이후에 시작 = `s >= last_end`. `>=` 인 이유는 반개구간.

</details>

<details><summary>힌트 3 — 병합은 시작 순서, 비교 대상은 열린 덩어리 하나</summary>

시작 순으로 보면 새 구간은 지금 열린 마지막 덩어리 `out[m]` 하고만 닿을 수 있다 (그 앞 덩어리들은 `out[m].s` 보다 앞에서 끝났다). `c.s <= out[m].e` 면 늘리고 — 늘릴 때 `max` — 아니면 새 덩어리.

</details>

<details><summary>힌트 4 — 깊이: 시작은 +1, 끝은 -1, 같은 시각이면 -1 먼저</summary>

시작 시각 배열과 끝 시각 배열을 따로 정렬하면 "어느 구간의 시작/끝인지" 는 필요 없다. 두 포인터로 더 이른 쪽을 처리한다. `starts[i] < ends[j]` (엄격) 이면 시작, 아니면 끝 — 같은 시각이면 끝이 먼저 처리된다.

</details>

<details><summary>힌트 5 — 채널 배정: 시작 순서 + "가장 일찍 비는 채널"</summary>

채널마다 마지막 전송의 끝 시각을 최소 힙에 넣는다. 새 전송의 시작 `s` 에서 힙 맨 위 `end <= s` 면 그 채널이 비었다 → pop 해서 재사용. 맨 위가 아직 바쁘면 **모든** 채널이 바쁘다 → 새 채널. 어느 경우든 (새 끝, 채널) 을 push.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 확인 질문 | 끝점 포함? (반개/닫힌) 맞닿으면 겹침인가? 정렬돼 들어오나? n 범위, 시각 타입(32비트 wrap?)? 개수만 / 실제 선택·배정도? |
| 2. 무식한 방법 | 부분집합 2^n 전수 (선택), 모든 정수 시각에서 깊이 세기 O(n·T), 백트래킹 색칠 — 테스트 기준으로 남긴다 |
| 3. 더 나은 생각 + 정당성 | 끝 순서 그리디 + 교환 논증 / 깊이 = 하한, 시작 순서 그리디 = 상한 / 병합 불변식 |
| 4. 코드 | 정렬 한 번 + 한 번 훑기. `>=` / `<=` / `<` 를 규약에서 유도 |
| 5. 소리 내어 말할 테스트 | n = 0, 1개, 전부 같은 구간, 맞닿음 사슬 `[0,1)[1,2)`, 포함 `[1,10)[2,3)`, `UINT32_MAX` 끝점, 반례 3개 |
| 6. 복잡도 | 시간 O(n log n) (정렬이 지배, 훑기는 O(n) / 힙은 O(n log k)), 추가 메모리 O(n) 작업 공간 + O(k) 힙, 재귀 없음 |

### Part A — 끝이 가장 이른 것부터: 교환 논증

알고리즘: 끝 순서로 정렬하고 `last_end = 0` 에서 시작해 `s >= last_end` 면 고르고 `last_end = e`. (`last_end = 0` 이 "아직 아무것도 안 고름" 을 겸한다 — 모든 `s >= 0` 이라 첫 구간은 항상 들어간다. 별도 플래그가 필요 없는 이유.)

**정리.** 그리디가 고른 수 k 는 최적값과 같다.

**증명 (교환 논증).** 그리디가 고른 것을 끝 순서대로 g₁, g₂, …, g_k 라 하자. "어떤 최적해 O 가 g₁…g_r 을 포함하고, O 의 나머지 원소는 모두 `e(g_r)` 이후에 시작한다" 가 r = 0, 1, …, k 에 대해 성립함을 귀납으로 보인다 (r = 0 은 아무 최적해나).

- r → r+1: O 에서 g₁…g_r 다음 원소(끝 순서로 첫째)를 o 라 하자. o 는 `s(o) >= e(g_r)` 이므로 그리디가 g_{r+1} 을 고를 때 **후보였다**. 그리디는 후보 중 끝이 가장 이른 것을 고르므로 `e(g_{r+1}) <= e(o)`. O 에서 o 를 g_{r+1} 로 **바꾼다**: g_{r+1} 은 g_r 과 안 겹치고(`s >= e(g_r)`), O 의 나머지(o 다음 것들)는 `e(o)` 이후, 즉 `e(g_{r+1})` 이후에 시작하므로 그것들과도 안 겹친다. 크기가 같으니 여전히 최적이다.
- 끝: r = k 에서 O 에 원소가 더 남아 있다면 그 원소 o 는 `s(o) >= e(g_k)` 이고 끝 순서로 g_k 보다 뒤 — 그리디가 그것을 볼 때 `last_end = e(g_k)` 이므로 골랐어야 한다. 모순. 따라서 |O| = k. ∎

같은 논증을 "그리디가 앞선다(greedy stays ahead): 모든 r 에서 `e(g_r) <= e(o_r)`" 형태로 써도 된다 (Kleinberg–Tardos 4.1). 끝 시각이 같은 구간 사이 순서는 증명 어디에도 안 쓰였다 — 동점은 아무렇게나 깨도 된다.

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">교환 논증 한 걸음: O 의 다음 원소 o 를 g(r+1) 로 바꿔도 O 는 여전히 유효·최적</text>
  <line x1="250" y1="36" x2="250" y2="170" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="200" y="185" fill="currentColor" font-size="12">e(g_r) = last_end</text>
  <g stroke="currentColor">
    <rect x="80" y="45" width="170" height="22" class="f-ok"/>
    <rect x="290" y="80" width="140" height="22" class="f-ok"/>
    <rect x="270" y="115" width="230" height="22" class="f-warn"/>
    <rect x="540" y="150" width="150" height="22" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="90" y="61">g1 … g_r (공통)</text>
    <text x="300" y="96">g(r+1)</text><text x="440" y="96">← 후보 중 끝이 가장 이름</text>
    <text x="280" y="131">o: O 의 다음 원소</text><text x="510" y="131">← 역시 후보</text>
    <text x="550" y="166">O 의 나머지: e(o) 이후 시작</text>
    <text x="300" y="200">e(g(r+1)) ≤ e(o) ≤ 나머지의 시작 → 바꿔도 안 겹친다</text>
  </g>
</svg>
```

손 계산 (위 8 구간, 실제 트레이스 출력):

| 끝 순서 | 구간 | last_end | 판단 |
|---|---|---|---|
| 1 | I1 [1,3) | 0 | 1 ≥ 0 → **고름**, last_end = 3 |
| 2 | I2 [2,5) | 3 | 2 &lt; 3 → 버림 |
| 3 | I0 [0,6) | 3 | 버림 |
| 4 | I7 [4,6) | 3 | 4 ≥ 3 → **고름**, last_end = 6 |
| 5 | I3 [3,7) | 6 | 버림 |
| 6 | I4 [5,8) | 6 | 버림 |
| 7 | I5 [7,9) | 6 | 7 ≥ 6 → **고름**, last_end = 9 |
| 8 | I6 [8,10) | 9 | 버림 → 답 3 |

### Part A — 병합: 열린 덩어리 하나만 본다

시작 순으로 정렬. 불변식: 앞의 t 개를 처리한 뒤 `out[0..m]` 은 그 t 개의 합집합의 **정규형**(정렬, 덩어리 사이 진짜 빈틈)이고, `out[m]` 이 열려 있다. 다음 구간 c 는 `c.s >= out[m].s > out[m-1].e` 이므로 앞 덩어리들과는 닿을 수 없다 → `out[m]` 하고만 비교. `c.s <= out[m].e` (겹침 **또는 맞닿음**) 면 `out[m].e = max(out[m].e, c.e)`, 아니면 새 덩어리. `max` 를 빼먹으면 `[1,10) [2,3)` → `[1,3)` 이 된다 (7. 실수 3, 무작위 1752/4000 오답).

맞닿음을 합치는 이유: 병합의 목적은 보통 "덮인 영역" (메모리 영역, 바쁜 시간대) 이고 `[1,4) ∪ [4,5) = [1,5)` 는 빈틈 없는 한 덩어리다. "겹칠 때만 합친다" 를 원하면 `<` 로 바꾸면 되는데, 그때 결과는 정규형이 아니다 (같은 합집합에 표현이 여럿). 면접에서는 어느 쪽인지 먼저 묻는다. 테스트는 셀 비트맵(`[t, t+1)` 칸을 칠하고 연속 run 을 읽음)과 **정확히** 같은지 비교한다 — 비트맵의 run 이 바로 정규형이다.

### Part B — 깊이는 자원 수의 하한, 스위프로 O(n log n)

깊이 `depth(t) = #{i : s_i ≤ t < e_i}`, D = max_t depth(t).

**하한.** 시각 t* 에 활성인 D 개 구간은 모두 t* 를 포함하므로 서로 겹친다 → 서로 다른 채널이 D 개 필요하다. 어떤 배정이든 채널 ≥ D.

**최댓값은 어떤 시작 시각에서 나온다.** depth(t) 는 계단 함수이고 시작 시각에서만 올라간다 (끝에서는 내려간다). 그래서 브루트포스 기준은 "각 시작점에서 세기" O(n²) 로도, 격자 정수 시각 전부 세기로도 된다 — 정수 끝점 반개구간에서는 실수 t 의 깊이가 ⌊t⌋ 의 깊이와 같기 때문이다.

**스위프.** 시작 배열과 끝 배열을 각각 정렬하고 두 포인터로 더 이른 사건을 처리한다. `starts[i] < ends[j]` (엄격) 면 +1, 아니면 -1. 같은 시각이면 끝(-1)이 먼저다: `[s, e)` 는 e 를 포함하지 않으므로 시각 e 에 끝나는 구간과 시각 e 에 시작하는 구간은 동시에 활성인 적이 없다. 시각 t 의 마지막 시작을 처리한 직후 d = (t 이하 시작 수) − (t 이하 끝 수) = depth(t) 이고, 같은 t 의 중간 값들은 그보다 작으니 최댓값이 부풀지 않는다. `*at` 은 best 가 처음 갱신된 시작 시각이다.

**`j < i` 보조정리 (-1 이 음수로 안 간다).** 정렬된 i 번째 시작 < 정렬된 i 번째 끝: 끝이 가장 작은 i+1 개 구간은 각자 `s < e ≤ ends[i]` 이므로 `ends[i]` 보다 작은 시작이 적어도 i+1 개 → `starts[i] < ends[i]`. 따라서 i == j 이면 반드시 시작 쪽으로 가고, else 분기에서는 늘 `j < i` — d 는 음수가 되지 않고 `ends[j]` 는 범위 안. 시작을 다 처리하면 깊이는 줄기만 하므로 루프를 끝낸다.

손 계산 (8 구간, 실제 트레이스):

| 사건 | 시작 0 | 시작 1 | 시작 2 | **끝 3** | 시작 3 | 시작 4 | 끝 5 | 시작 5 | 끝 6 | 끝 6 | 끝 7 | 시작 7 | 끝 8 | 시작 8 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| d | 1 | 2 | 3 | 2 | 3 | **4** | 3 | 4 | 3 | 2 | 1 | 2 | 1 | 2 |

시각 3 에서 I1 의 끝이 I3 의 시작보다 먼저 처리됐다 — 시작을 먼저 했다면 d 가 잠깐 4 가 되어 *at* 이 3 으로 틀렸을 것이다. 최댓값 4 는 시각 4 에서 처음 (`depth=4 at=4`).

### Part B — 잘못된 그리디의 반례

모든 규칙을 같은 틀("남은 것 중 규칙상 최선을 고르고, 그것과 겹치는 것을 지운다")로 구현해 무작위 4000 인스턴스(n ≤ 12, 좌표 &lt; 52)에서 부분집합 전수 최적과 비교했다 (solution.c 실제 출력):

```text
  rule earliest end      suboptimal in    0 / 4000 (max shortfall 0)
  rule earliest start    suboptimal in  742 / 4000 (max shortfall 3)
  rule shortest first    suboptimal in  111 / 4000 (max shortfall 2)
  rule fewest conflicts  suboptimal in    0 / 4000 (max shortfall 0)
  rule latest start      suboptimal in    0 / 4000 (max shortfall 0)
```

`demo_counterexamples.c` 가 n, 격자 크기 G 가 작은 순으로 전수해서 찾은 **가장 작은 반례**:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_counterexamples.c -o demo_ce && ./demo_ce    # 이 맥에서 약 1분 (n = 7 전수가 대부분)
```

```text
earliest start   n=3 G=3: [0,3) [1,2) [2,3)  greedy=1 optimal=2
shortest first   n=3 G=5: [0,3) [2,4) [3,5)  greedy=1 optimal=2
fewest conflicts: no counterexample with n=5 on grid 0..7 (201376 multisets checked)
fewest conflicts: no counterexample with n=6 on grid 0..7 (1107568 multisets checked)
fewest conflicts: no counterexample with n=7 on grid 0..7 (5379616 multisets checked)
fewest conflicts random search n=8..14: 1400000 instances, 20 counterexamples; first (n=11): [6,10) [1,4) [9,13) [14,17) [1,5) [19,23) [3,7) [11,15) [1,5) [12,16) [2,3)  greedy=4 optimal=5
```

- **이른 시작**: 일찍 시작하는 긴 구간 하나가 전부를 막는다. 증명이 깨지는 곳: "후보 중 끝이 가장 이름" 이 없으니 `e(g) <= e(o)` 가 없다.
- **짧은 것 먼저**: 짧은 `[2,4)` 가 양옆 둘과 겹친다. 짧다는 것은 "끝이 이르다" 와 다르다.
- **충돌 적은 것 먼저**: 가장 끈질기다 — n ≤ 7 전수(격자 0..7, 동점은 끝이 이른 쪽으로 유리하게 깸)에서는 반례가 **없었고**, 무작위 140만 개 중 20개에서야 나왔다 (n = 11; 이것이 가장 작은 반례라는 보장은 없다). Kleinberg–Tardos 4.1 의 그림에도 이 규칙의 반례가 있다. "무작위 4000 개에서 0 실패" 는 **증명이 아니다** — 이 줄이 이 레슨에서 가장 중요한 실측이다.
- **늦은 시작**은 끝이 이른 그리디의 거울상(시간을 뒤집으면 시작 ↔ 끝)이라 역시 최적이다 (0 실패).

### Part C — 채널 배정: 깊이만큼만 쓰는 이유

**상한 (그리디가 D 를 넘지 않음).** 시작 순서로 처리한다. 전송 I(시작 s)에 새 채널 k 를 열었다면 그 순간 기존 k−1 개 채널이 **모두 바쁘다**: 각 채널의 마지막 전송은 끝 > s 이고, 시작 순서로 먼저 처리됐으니 시작 ≤ s. 그러므로 그 k−1 개 전송과 I 가 모두 시각 s 를 포함 → depth(s) ≥ k. 따라서 쓴 채널 수 ≤ D. 하한(≥ D)과 합치면 **정확히 D** — 최소. ∎

**유효성.** 시작 순서로 한 채널에 붙이므로 그 채널의 마지막 전송이 그 채널에서 끝이 가장 늦다. `end <= s` 면 그 채널의 모든 전송이 s 이전에 끝났다. 힙 맨 위만 보는 이유: 맨 위가 가장 일찍 비는 채널이다. 그게 아직 바쁘면 전부 바쁘다. 빈 채널이 여럿이면 **아무거나** 써도 증명이 성립한다 (상한 증명은 "새 채널은 전부 바쁠 때만" 만 썼다). `demo_counterexamples.c` 의 "start order, first-fit" 도 0 실패였다.

**`hw_ch` 초과.** 그리디는 정확히 D 개를 쓰므로 `-1` ⇔ D > `hw_ch`. 어디서 막혔는지는 `max_depth` 의 `*at` 이 알려 준다.

실제 출력 (solution.c, Part C 프레임):

```text
Part C frame: 10 transfers, channels used=5, depth=5 at t=100 us, brute chromatic=5
  ch0  [   0,  400)  ADC1 scan->RAM
  ch2  [  50,  120)  SPI1 IMU RX
  ch3  [  50,  120)  SPI1 IMU TX
  ch1  [   0, 1000)  UART2 GPS RX
  ch4  [ 100,  600)  I2S audio TX
  ch2  [ 130,  380)  SPI2 flash rd
  ch2  [ 400,  900)  UART1 log TX
  ch3  [ 380,  450)  I2C1 baro RX
  ch3  [ 600,  800)  memcpy frame
  ch0  [ 450,  700)  SPI2 flash wr
```

힙 트레이스 (실제, 처음 7 단계):

| 시작 순 | 전송 | 힙 top (end, ch) | 결정 | 힙 크기 |
|---|---|---|---|---|
| 1 | ADC1 [0,400) | — | 새 ch0 | 1 |
| 2 | GPS RX [0,1000) | (400, ch0) 바쁨 | 새 ch1 | 2 |
| 3 | IMU RX [50,120) | (400, ch0) 바쁨 | 새 ch2 | 3 |
| 4 | IMU TX [50,120) | (120, ch2) 바쁨 | 새 ch3 | 4 |
| 5 | I2S [100,600) | (120, ch2) 바쁨 | 새 ch4 | 5 |
| 6 | flash rd [130,380) | (120, ch2) 비었음 | **재사용 ch2** | 5 |
| 7 | baro [380,450) | (120, ch3) 비었음 | 재사용 ch3 | 5 |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Part C 실제 배정 (solution.c 출력) — 1 ms 프레임, 5 채널 = 깊이 5 (t = 100 µs)</text>
  <g stroke="currentColor">
    <rect x="120" y="40" width="240" height="24" class="f-acc"/><rect x="390" y="40" width="150" height="24" class="f-acc"/>
    <rect x="120" y="72" width="600" height="24" class="f-ok"/>
    <rect x="150" y="104" width="42" height="24" class="f-warn"/><rect x="198" y="104" width="150" height="24" class="f-warn"/><rect x="360" y="104" width="300" height="24" class="f-warn"/>
    <rect x="150" y="136" width="42" height="24" class="f-mut"/><rect x="348" y="136" width="42" height="24" class="f-mut"/><rect x="480" y="136" width="120" height="24" class="f-mut"/>
    <rect x="180" y="168" width="300" height="24" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="57">ch0</text><text x="10" y="89">ch1</text><text x="10" y="121">ch2</text><text x="10" y="153">ch3</text><text x="10" y="185">ch4</text>
    <text x="126" y="57">ADC1 scan</text><text x="396" y="57">flash wr</text>
    <text x="126" y="89">UART2 GPS RX (전 구간)</text>
    <text x="154" y="121">IMU</text><text x="204" y="121">flash rd</text><text x="366" y="121">UART1 log TX</text>
    <text x="154" y="153">IMU</text><text x="351" y="153">baro</text><text x="486" y="153">memcpy</text>
    <text x="186" y="185">I2S audio TX</text>
  </g>
  <line x1="180" y1="34" x2="180" y2="198" stroke="currentColor" stroke-dasharray="5 4"/>
  <line x1="120" y1="206" x2="720" y2="206" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="120" y="222">0</text><text x="180" y="222">100</text><text x="240" y="222">200</text><text x="360" y="222">400</text><text x="480" y="222">600</text><text x="600" y="222">800</text><text x="720" y="222">1000</text><text x="60" y="222">(µs)</text>
  </g>
  <text x="10" y="242" fill="currentColor" font-size="12">점선: t = 100 에 ADC1, GPS RX, IMU RX, IMU TX, I2S 가 동시 활성 → 어떤 배정도 5 채널 미만은 불가능.</text>
</svg>
```

ch2 의 flash rd 는 IMU RX 가 120 µs 에 끝나고 130 µs 에 시작, ch3 의 baro 는 120 µs 에 빈 채널을 380 µs 에 쓴다. 테스트는 `hw_ch = 4` 로 다시 불러 `-1` 이 나오는 것도 확인한다.

**펌웨어 현실.** 이 모델은 "어떤 요청이든 어떤 채널에나 갈 수 있다" 를 가정한다. STM32 계열이라면 DMAMUX 가 있는 부품(G4, H7, L4+ 등)이 그에 가깝고, STM32F4 의 DMA 는 스트림마다 연결 가능한 요청이 고정된 표로 정해져 있다 (부품별 레퍼런스 매뉴얼의 request mapping 표 — 확인 권장). 고정 매핑이 붙으면 "아무 빈 채널" 이 아니므로 이 증명이 더는 성립하지 않는다 (Q7). 실무에서 이 그리디는 보통 **설계 시점 도구**다: 프레임의 최악 전송 계획이 N 채널에 들어가는지 검사하고 배정표를 만든다. 채널 재설정 시간이 있으면 각 전송의 끝에 가드 시간을 더해 `[s, e + guard)` 로 넣으면 된다 (덧셈 넘침 주의, Q8).

**스택 (Cortex-M4).** 힙 `slot_t heap[MAX_CH]` 는 지역 배열이다. `slot_t` 는 패딩 포함 8 B. 알고리즘 부분만 `demo_target.c` 로 떼어 이 맥의 clang 으로 Cortex-M4 용 `-fstack-usage` 를 뽑았다 (각 함수 자기 프레임만, 호출 체인 합은 아님):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -fstack-usage -c demo_target.c -o /tmp/ALGO-M09_t.o
cat /tmp/ALGO-M09_t.su     # clang 은 .su 를 -o 파일 옆에 쓴다
```

```text
demo_target.c:39:sort_idx	32	static
demo_target.c:29:idx_sift	36	static
demo_target.c:56:sort_u32	24	static
demo_target.c:64:slot_push	20	static
demo_target.c:73:slot_pop	28	static
demo_target.c:90:max_nonoverlap	44	static
demo_target.c:106:merge_intervals	32	static
demo_target.c:125:max_depth	48	static
demo_target.c:148:dma_assign	192	static
```

`-DMAX_CH=64` 로 바꾸면 `dma_assign 576` — 힙 배열이 스택의 대부분이다. 작은 태스크 스택(예: 512 B)에서 부를 거라면 힙을 호출자 버퍼로 받거나 정적 배열로 옮기고(재진입 불가가 됨) 그 트레이드오프를 말한다. Cortex-M0(`thumbv6m`)에서는 `dma_assign 216`.

### 벤치마크 — `demo_bench.c`

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

이 맥(Apple Silicon M 시리즈, `Apple clang 21.0.0`, -O2), 워밍업 1회 + 7회 중앙값. 다른 작업이 동시에 돌던 중이라 실행마다 최대 2배 정도 흔들렸다 — **비율만** 본다. 실제 출력 한 번:

```text
-- n=10000: depth=22, dma channels=22, max_nonoverlap=1969
sort_idx only             10000        695.0         69.5
max_nonoverlap            10000        739.0         73.9
merge_intervals           10000        741.0         74.1
max_depth sweep           10000        426.0         42.6
dma_assign heap           10000        873.0         87.3
naive depth O(n^2)        10000     216366.0      21636.6
-- n=60000: depth=23, dma channels=23, max_nonoverlap=11985
sort_idx only             60000       8577.0        142.9
max_nonoverlap            60000      10884.0        181.4
merge_intervals           60000      10221.0        170.3
max_depth sweep           60000       4311.0         71.8
dma_assign heap           60000       8186.0        136.4
```

읽는 법: `max_nonoverlap`, `merge_intervals`, `dma_assign` 은 **정렬만** 한 시간과 거의 같다 — 그리디 훑기 자체는 공짜에 가깝고 정렬이 O(n log n) 을 지배한다. 스위프가 더 빠른 것은 인덱스를 거쳐 구조체를 비교하지 않고 `uint32_t` 를 직접 정렬하기 때문이다. O(n²) 깊이는 n = 10⁴ 에서 이미 약 500배 느리다.

### 복잡도 요약

| 함수 | 시간 (최악) | 추가 메모리 | 스택 |
|---|---|---|---|
| max_nonoverlap | O(n log n) 정렬 + O(n) | 작업 공간 n × 2 B (호출자) | O(1), 재귀 없음 |
| merge_intervals | O(n log n) + O(n) | 작업 공간 n × 2 B + 출력 | O(1) |
| max_depth (스위프) | O(n log n) + O(n) | 시작·끝 배열 n × 4 B × 2 | O(1) |
| dma_assign (힙) | O(n log n) 정렬 + O(n log D) 힙 | 작업 공간 + 힙 D ≤ MAX_CH 슬롯 | 192 B (M4, MAX_CH = 16) |
| 브루트포스 (테스트) | 2^n·n, O(n·T), 백트래킹 | — | 재귀 깊이 n |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 42/42 passed, 약 0.1초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 42/42, 보고 없음
```

실제 출력 (Part C 표는 위에 있음):

```text
exhaustive grid: 13616 instances, mismatches select=0 merge=0 depth=0 dma=0
random n<=12: 4000 instances, mismatches select=0 merge=0 depth=0 dma=0
random n=1000: 36 runs (3 coordinate maps), depth up to 17 (6 runs > MAX_CH=16), mismatches select=0 merge=0 depth=0 dma=0
Part C frame: 10 transfers, channels used=5, depth=5 at t=100 us, brute chromatic=5
42/42 passed
```

검증 구성: 13616 = 격자 0..5 의 15 가지 구간으로 만든 n = 0..3 의 모든 순서쌍(1 + 15 + 225 + 3375) + 격자 0..4 의 10 가지로 만든 n = 4 순서쌍 10⁴. 각 인스턴스에서 선택 = 부분집합 전수, 병합 = 셀 비트맵, 깊이 = 모든 정수 시각, 채널 = **백트래킹 최소 색칠**(정리 "채널 = 깊이" 를 쓰지 않는 독립 기준)과 비교한다. n = 1000 은 끝 순서 단위 가중치 DP(그리디와 무관한 O(n²) 기준)와 비트맵으로 대조하고, 같은 입력을 `t × 10⁶` 과 `t + (UINT32_MAX − 4095)` 로 옮겨 다시 돌린다 — 순서를 보존하는 변환이라 답도 똑같이 옮겨져야 하고, 끝점이 `UINT32_MAX` 까지 간다. 밀도를 바꿔 6 개 실행은 깊이가 16 을 넘으므로 `-1` 경로도 시험된다.

## 7. 자주 하는 실수

`demo_bugs.c` 가 버그 변형 6개를 정답 함수와 같은 입력(격자 n ≤ 3 전수 3616개 + 무작위 4000개)에 넣는다. 실제 출력:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c -o demo_bugs && ./demo_bugs
```

```text
variant                                    | grid n<=3: wrong / 3616 | random n<=12: wrong / 4000 | first failing input
B1 select: sort by start                   |      210            |      584              | [2,3)[1,2)[0,3)
B2 select: s > last_end (strict)           |     1126            |      867              | [1,2)[0,1)
B3 merge: e = c.e (no max)                 |      786            |     1752              | [1,2)[0,3)
B4 sweep: start before end on tie          |     1126            |      655              | [1,2)[0,1)
B5 dma: reuse only if end < s              |     1126            |      655              | [1,2)[0,1)
B6 depth = max overlaps-per-interval + 1   |      882            |     1834              | [1,2)[0,2)[0,1)
```

**1. 시작 순으로 정렬해서 고르기 (B1).** 코드 모양은 정답과 똑같고 정렬 키만 다르다 — 그래서 면접에서 가장 흔하다. `[0,3)` 이 먼저 뽑혀 `[1,2)`, `[2,3)` 을 막는다. 고침: `sort_idx(..., BY_END)`.

**2. 맞닿음을 겹침으로 (B2, `s > last_end`).** `[0,1) [1,2)` 에서 1개만 고른다. 닫힌구간 코드(LeetCode 452 화살 문제)를 반개구간 문제에 옮기면 생긴다. 규약을 먼저 정하고 비교를 유도한다: 반개구간에서 "안 겹침" ⇔ `s >= last_end`.

**3. 병합에서 `out[m].e = c.e` (B3).** 포함된 구간 `[0,3) ⊃ [1,2)` 를 만나면 덩어리가 **줄어든다**. 고침: `if (c.e > out[m].e) out[m].e = c.e;` — 시작 순 정렬은 끝 순서를 보장하지 않는다.

**4. 스위프에서 같은 시각이면 시작 먼저 (B4, `<=`).** `[0,1) [1,2)` 의 깊이를 2 로 센다 → 채널을 하나 더 산다. 이벤트 정렬의 동점 규칙은 구간 규약에서 나온다: 반개구간 = 끝 먼저, 닫힌구간 = 시작 먼저.

**5. 힙 재사용 조건 `end < s` (B5).** 4 와 같은 실수를 힙 쪽에서 한 것 — 실패 수(655)도 정확히 같다. 정확히 s 에 비는 채널을 못 쓴다. 고침: `heap[0].end <= x[i].s`.

**6. "각 구간과 겹치는 개수 + 1" 의 최댓값을 깊이로 (B6).** `[0,2)` 는 `[0,1)`, `[1,2)` 와 겹쳐 "3" 이지만, 그 둘은 서로 안 겹치므로 동시에 활성인 것은 최대 2 다. 겹침은 **쌍** 관계이고 깊이는 **한 시각**에서의 개수다. 구간에서는 쌍마다 겹치는 집합은 공통점을 가진다(헬리 성질, 1차원) — 그래서 "서로 다 겹치는 최대 집합 = 깊이" 지만, "한 구간과 겹치는 것들" 은 서로 다 겹치는 집합이 아니다.

**7. (테스트 함정) 무작위 테스트만 믿기.** 위 Part B 표에서 "충돌 적은 것 먼저" 는 무작위 4000 개 0 실패였다. 그리디의 정당성은 증명으로, 테스트는 구현 실수를 잡는 용도로.

## 8. 면접 Q&A

<details><summary>Q1. 왜 끝이 가장 이른 것부터죠? 그냥 직관 아닌가요?</summary>

교환 논증으로 증명된다 (5. 풀이). 요지: 어떤 최적해의 첫 원소를 그리디의 첫 원소(끝이 가장 이름)로 바꿔도 나머지와 안 겹친다 — 그리디 원소의 끝이 더 이르거나 같으니까. 이걸 귀납으로 반복하면 최적해를 그리디 해로 바꿀 수 있고 크기는 그대로다. 꼬리: "시작이 이른 것은?" → `[0,3) [1,2) [2,3)` (1 vs 2). "짧은 것은?" → `[0,3) [2,4) [3,5)`. "충돌 적은 것은?" → 작은 n 에서는 반례가 안 나오지만(실측 n ≤ 7 전수 0개) n = 11 반례가 있다. 그래서 "테스트로 확인했다" 가 아니라 증명을 말해야 한다. "늦게 시작하는 것부터" 는 시간을 뒤집은 같은 알고리즘이라 맞다.

</details>

<details><summary>Q2. 구간이 닫힌구간 [s, e] 면 무엇이 바뀌나요?</summary>

비교 연산자 세 군데가 바뀐다. (1) 선택: `s > last_end` (닿으면 겹침). (2) 스위프 동점: 시작 먼저 (시각 e 에 끝나는 것과 e 에 시작하는 것이 동시에 활성). (3) 힙 재사용: `end < s`. 병합은 "닿으면 합친다" 를 정수 닫힌구간에서 유지하려면 `c.s <= out.e + 1` 까지 볼지도 정해야 한다 (`[1,3]` 과 `[4,5]` 는 정수로는 빈틈이 없다 — 이때 `out.e + 1` 의 넘침 주의). LeetCode 435(Non-overlapping Intervals)는 맞닿음을 허용하고, 452(Minimum Number of Arrows to Burst Balloons)는 닿으면 같이 터진다 — 연습 1 이 두 규약을 나란히 둔다. 펌웨어 시간 구간은 `[start_tick, end_tick)` 가 자연스럽다: 길이 = 차이, 연속 구간이 빈틈·중복 없이 이어진다.

</details>

<details><summary>Q3. 최소 채널 수가 최대 깊이와 같다는 걸 증명해 보세요. 일반 그래프 색칠은 NP-hard 아닌가요?</summary>

하한: 깊이 D 인 시각의 D 개는 서로 다 겹치므로 D 채널이 필요. 상한: 시작 순서로 배정하며 "모든 채널이 바쁠 때만 새 채널" — 새 채널 k 를 여는 순간 기존 k−1 개의 마지막 전송과 지금 전송이 모두 시작 시각 s 를 포함하므로 D ≥ k. 일반 그래프 색칠은 NP-hard 맞다. 구간 그래프(interval graph)는 **완전 그래프(perfect graph)** 류여서 색 수 = 최대 클리크 크기이고, 구간에서는 최대 클리크 = 깊이(1차원 헬리 성질: 쌍마다 겹치는 구간들은 공통점을 가진다)라 다항 시간에 된다. 순서가 중요하다: 끝 순서나 입력 순서로 first-fit 하면 깊이보다 많이 쓴다 — 실측 가장 작은 반례 `[2,3) [1,3) [0,2) [0,1)` 를 끝 순서 first-fit 은 3 색, 깊이는 2 (`demo_counterexamples.c`, 무작위 20만 개 중 8602 개에서 초과).

</details>

<details><summary>Q4. 개수만 필요하면요? 힙과 스위프 중 무엇을 쓰죠?</summary>

개수만이면 스위프: 시작·끝 배열을 각각 정렬하고 두 포인터. 구조체 비교 없이 `uint32_t` 정렬이라 이 맥에서 n = 6×10⁴ 에 힙판의 약 절반 시간이었다. 실제 배정(어느 전송이 어느 채널)이 필요하면 힙 — O(n log n) 정렬 + O(n log D). LeetCode 253 Meeting Rooms II 의 두 표준 풀이가 정확히 이 둘이다. 꼬리: "요청이 시간 순으로 하나씩 들어오면?" → 힙판은 과거만 보므로 그대로 **온라인** 알고리즘이고 여전히 최적이다 (증명이 미래를 안 썼다). 시간 순서가 아니라 임의 순서로 들어오면 first-fit 이 최적이 아니다 (Q3 실측).

</details>

<details><summary>Q5. DMA 채널이 k 개로 고정이면, 받을 수 있는 전송을 최대화하려면요?</summary>

k = 1 이면 Part A 그 자체다. 일반 k: 끝 순서로 보며, 지금 비어 있는 채널 중 **마지막 끝이 가장 늦은** 채널(best fit)에 넣고, 빈 채널이 없으면 거절. 이 규칙은 최적으로 알려져 있다 (Carlisle & Lloyd 1995, Faigle & Nawijn 1995 — 출처 확인 권장). 증명은 교환 논증: 최적해와 그리디가 앞부분 배정이 같다고 하고 다음 전송 I 에서 (a) 최적해가 I 를 다른 빈 채널 m′ 에 두면 m 과 m′ 의 "미래 부분" 을 맞바꾸고 (best fit 이라 `last(m) >= last(m′)` 이므로 m′ 은 m 의 미래를 받을 수 있다), (b) 최적해가 I 를 거절했으면 채널 m 의 다음 전송 J (끝이 I 이상)를 I 로 바꾼다 (m 에 미래 전송이 없으면 I 를 그냥 더해 모순), (c) 그리디가 I 를 거절했다면 같은 앞부분을 가진 최적해에서도 모든 채널의 마지막 끝 > s 라 I 가 들어갈 자리가 없다. 함정: Part C 의 "가장 일찍 빈 채널"(힙 top)은 **최소 채널 수**에는 최적이지만 이 문제에서는 틀린다 — `[0,1) [0,3) [3,6) [2,7)`, k = 2 에서 best fit 4 개, earliest-free 3 개 (연습 4, 무작위 3000 중 214 개 열세).

</details>

<details><summary>Q6. 전송마다 우선순위(가중치)가 있으면 그리디가 되나요?</summary>

안 된다. `[0,10)` 가중치 10 과 `[0,5)`, `[5,10)` 각 1: 끝이 이른 그리디는 2, 최적은 10. 가중치 구간 스케줄링은 DP: 끝 순서로 `dp[i+1] = max(dp[i], w_i + dp[p(i)])`, `p(i)` = `e <= s_i` 인 앞 구간 수 = upper_bound 이진 탐색(ALGO-B02). O(n log n). (LeetCode 1235 Maximum Profit in Job Scheduling 이 이 문제.) 연습 5 실측: 무작위 3000 개 중 1563 개에서 무가중 그리디가 열세. 0/1 배낭(ALGO-M08)과 같은 "고르냐 마냐" 점화식이고, 구간이 정렬 순서를 주기 때문에 1차원으로 줄어든다. 꼬리: "가중치 합 타입?" → `uint32_t` 가중치 n 개 합은 `uint64_t` (연습 5 의 `3 × UINT32_MAX` 테스트).

</details>

<details><summary>Q7. 실제 MCU 의 DMA 는 요청마다 쓸 수 있는 채널이 정해져 있는데요?</summary>

그러면 "아무 빈 채널" 이 아니라 **허용 목록이 있는 색칠**이 되어, 위 증명(상한 쪽)이 성립하지 않는다 — 깊이만큼 채널이 있어도 배정이 안 될 수 있다. 실무 대처: (1) DMAMUX 가 있는 부품이면 매핑 제약이 거의 사라진다 (부품별 확인 권장), (2) 고정 매핑이면 채널마다 그 채널에만 갈 수 있는 요청들을 먼저 따로 검사(채널별로 겹침 = 충돌), (3) 선택지가 있는 요청이 적으면 백트래킹(ALGO-M03)으로 전수 — 설계 시점 도구라 n 이 작다. 런타임 할당이라면 Linux dmaengine 처럼 드라이버가 채널을 요청·반납(`dma_request_chan()`)하고, 실패하면 PIO 로 떨어지는 경로를 둔다.

</details>

<details><summary>Q8. 시각이 32비트 µs 틱이면 무엇을 조심하죠?</summary>

(1) `uint32_t` µs 는 약 71.6 분(2³² µs)마다 한 바퀴 돈다. 프레임 안 상대 시각(프레임 시작 기준 오프셋)으로 바꿔 넣으면 비교가 안전하다. 절대 틱을 그대로 쓰면 wrap 을 지나는 구간에서 `s < e` 가 깨진다. (2) 가드 시간 `e + guard`, 닫힌구간 병합의 `e + 1` 은 `UINT32_MAX` 근처에서 넘친다 — 부호 없는 덧셈은 mod 2³² 로 정의돼 있어(C11 6.2.5p9) 크래시는 없지만 조용히 틀린다. 이 레슨 테스트는 같은 입력을 `UINT32_MAX − 4095` 만큼 옮겨 끝점이 `UINT32_MAX` 인 경우를 돌린다. (3) 길이 `e - s` 는 `s < e` 면 안전. (4) 가중치 합·개수 합은 더 넓은 타입.

</details>

<details><summary>Q9. 정렬은 뭘 썼고, 펌웨어에서 qsort 를 쓰면 안 되나요?</summary>

인덱스 배열을 힙 정렬했다: O(n log n) 최악 보장, 추가 메모리 O(1), 재귀 없음 → 스택 상한이 정해진다 (Cortex-M4 `sort_idx` 32 B + `idx_sift` 36 B). 불안정 정렬이지만 비교 키에 인덱스를 마지막으로 넣어 **전순서**로 만들었으니 결과가 결정적이다. 라이브러리 `qsort` 도 쓸 수 있지만 구현마다 재귀·스택 사용·최악 시간이 다르고(확인 권장), 비교를 함수 포인터로 부른다. 구조체 대신 인덱스(uint16_t, 2 B)를 정렬하는 이유: 원본 순서를 보존해서 `pick[]`, `chan[]` 을 원래 인덱스로 돌려줄 수 있고, 옮기는 데이터가 작다. n 이 이미 시간 순으로 들어오는 스트림이면 정렬 자체가 필요 없다 → O(n).

</details>

## 9. 추가 연습

### 연습 1 — 최소 제거 수와 최소 화살 수 (LeetCode 435, 452)

(a) `min_removals`: 겹침을 없애려고 지워야 하는 최소 구간 수 (반개구간). (b) `min_arrows`: 닫힌구간 `[s, e]` 풍선을 한 점 x 에서 쏘면 `s ≤ x ≤ e` 인 풍선이 모두 터진다. 최소 화살 수. LeetCode 예제 `[[10,16],[2,8],[1,6],[7,12]]` → 2, `[[1,2],[2,3],[3,4],[4,5]]` → 2. 각각 부분집합 전수 / 격자 점 집합 전수(0..11 의 2¹² 부분집합)와 대조.

<details><summary>답 보기</summary>

(a) `n − max_nonoverlap`. 남길 최대 집합의 여집합이 최소 제거다. (b) 끝 순서로 정렬, 첫 화살을 가장 먼저 끝나는 풍선 b₁ 의 **끝** e₁ 에 쏜다. 교환 논증: 어떤 최적해든 b₁ 을 어떤 화살 x ≤ e₁ 로 맞힌다. x 가 맞히는 풍선은 모두 `s ≤ x ≤ e₁` 이고, e₁ 이 가장 이른 끝이므로 `e ≥ e₁` — 즉 e₁ 도 포함한다. 그러니 x 를 e₁ 로 옮겨도 맞히는 풍선이 줄지 않는다. 다음 풍선 `s > pos` (엄격 — 닿으면 같이 터짐)면 새 화살. 사실상 "닫힌구간판 최대 비겹침" 이고 답도 같다. 실제 출력:

```text
ex1: 3000 random cases, removal mismatches=0 arrow mismatches=0
```

</details>

### 연습 2 — 정렬·병합된 목록에 하나 삽입 (LeetCode 57)

MPU/메모리 보호 영역 목록이 정규형(정렬, 덩어리 사이 빈틈)으로 있다. 새 영역 x 를 넣고 다시 정규형으로 만들어라 — **O(n)**, 정렬 없이. `[1,3) [6,9)` + `[2,5)` → `[1,5) [6,9)`. `[1,2) [3,5) [6,7) [8,10) [12,16)` + `[4,8)` → `[1,2) [3,10) [12,16)`. 무작위 5000 회 비트맵 대조.

<details><summary>답 보기</summary>

세 구역으로 한 번 훑는다: (1) `a[i].e < x.s` — 완전히 왼쪽(닿지도 않음), 그대로 복사. (2) `a[i].s <= x.e` 인 동안 — 겹치거나 닿음, x 를 `min s / max e` 로 키운다. (3) 나머지 — 그대로 복사. 입력이 정규형이라 (1)·(3) 구역은 서로 닿지 않는다. 두 번째 예제에서 `[8,10)` 은 x 를 키운 뒤의 끝 8 에 닿아 합쳐진다 — 경계 비교 `<=` 가 그 역할. 뒤에 붙이고 `merge_intervals` 를 다시 부르는 O(n log n) 도 맞지만, 면접관은 "이미 정렬돼 있다" 를 쓰라고 이 문제를 낸다.

```text
ex2: 5000 random inserts vs bitmap, mismatches=0
```

</details>

### 연습 3 — 두 구간 목록의 교집합 (LeetCode 986, 반개구간판)

센서 A 가 유효한 시간대 목록과 B 가 유효한 시간대 목록(각각 정규형)에서 **둘 다 유효한** 시간대를 구하라. 두 포인터, O(na + nb). LeetCode 986 예제를 반개구간으로 읽으면 점 교집합 `[5,5] [24,24] [25,25]` 가 사라져 `[1,2) [8,10) [15,23)` 이 된다.

<details><summary>답 보기</summary>

`lo = max(a.s, b.s)`, `hi = min(a.e, b.e)`, `lo < hi` 면 출력. 그다음 **끝이 이른 쪽**을 전진 — 그 구간은 상대 목록의 다음 구간들과 더 겹칠 수 없다(상대의 다음 구간은 지금 상대 구간보다 뒤에서 시작). 출력이 다시 정규형인 이유: 연속한 두 조각 사이에는 한쪽 목록의 진짜 빈틈이 끼어 있다. 그래서 테스트는 비트맵 AND 의 run 과 **정확히** 같다. 닫힌구간 원문(LeetCode)에서는 `lo <= hi` 이고 점 구간이 나온다 — Q2 와 같은 규약 차이.

```text
ex3: 5000 random list pairs vs bitmap AND, mismatches=0
```

</details>

### 연습 4 — 채널 k 개로 받을 수 있는 최대 전송 수

`max_with_k(x, n, k, best_fit, work, chan)`: 끝 순서로 보며 빈 채널 중 (best_fit = 1) 마지막 끝이 **가장 늦은** 것 / (0) **가장 이른** 것에 넣는다. 거절된 전송은 `chan = 0xFF`. 부분집합 전수(깊이 ≤ k 인 최대 부분집합 — Part C 정리로 "k 채널 가능" ⇔ "깊이 ≤ k")와 대조. 반례 `[0,1) [0,3) [3,6) [2,7)`, k = 2.

<details><summary>답 보기</summary>

best fit 이 최적인 이유는 Q5 의 교환 논증. 직관: `[3,6)` 을 넣을 때 마지막 끝 1 인 채널과 3 인 채널이 둘 다 비었다. 3 쪽(best fit)에 넣으면 1 쪽이 `[2,7)` 을 받을 수 있게 남는다. 1 쪽에 넣으면 시작 2 인 `[2,7)` 이 갈 곳이 없다. **여유가 더 큰 채널을 아껴 두는 것**이 핵심이다. 실제 출력:

```text
ex4: 3000 random cases (k=1..3), best-fit mismatches=0, earliest-free suboptimal=214
```

k 가 작으면 채널 스캔 O(k) 로 충분하다 (O(nk)). k 가 크면 "s 이하인 마지막 끝 중 최대" 를 균형 트리/정렬 배열 이진 탐색으로 O(log k).

</details>

### 연습 5 — 가중치 구간 스케줄링: 그리디가 깨지는 곳

`wis_max(iv, w, n, ...)`: 비겹침 부분집합의 가중치 합 최대와 고른 구간(역추적). 반례 `[0,10)` w = 10, `[0,5)`, `[5,10)` w = 1. 부분집합 전수(n ≤ 12)와 대조, 무가중 그리디가 몇 번 지는지도 센다.

<details><summary>답 보기</summary>

끝 순서 DP. 상태 `dp[t]` = 끝 순서 앞 t 개만 쓸 때 최대 가중치. 전이 `dp[t+1] = max(dp[t], w_t + dp[p_t])`, `p_t` = 앞 t 개 중 `e <= s_t` 인 개수 (끝이 정렬돼 있으므로 그 개수 = 이진 탐색 upper_bound, ALGO-B02). 기저 `dp[0] = 0`, 순서 t 증가. 역추적: `dp[t] != dp[t-1]` 이면 t−1 번째를 골랐고 `t = p` 로 점프. 합은 `uint64_t`. 실제 출력:

```text
ex5: 3000 random weighted cases, DP mismatches=0, unweighted greedy suboptimal=1563
```

가중치가 모두 같으면 DP 답 = 끝 순서 그리디 답 — 그리디가 이 DP 의 "항상 고르는 쪽이 이긴다" 특수 경우라는 것이 교환 논증의 다른 얼굴이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 22/22 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 22/22, 보고 없음
```

## 10. 핵심 정리

- 규약부터: 반개구간 `[s, e)`. 겹침 ⇔ `a.s < b.e && b.s < a.e`. 선택 `s >= last_end`, 병합 `c.s <= out.e`, 스위프 동점은 끝 먼저, 힙 재사용 `end <= s`.
- 최대 비겹침 = **끝 순서** 그리디. 교환 논증: 최적해의 다음 원소를 끝이 더 이른 그리디 원소로 바꿔도 유효.
- 반례 셋: 이른 시작 `[0,3)[1,2)[2,3)`, 짧은 것 `[0,3)[2,4)[3,5)`, 충돌 적은 것은 n = 11 에서야 (무작위 4000 개는 0 실패 — 테스트는 증명이 아니다).
- 최소 자원 = 최대 깊이. 하한 = 그 시각의 구간들이 서로 다 겹침. 상한 = 시작 순서 + "전부 바쁠 때만 새 채널".
- 개수만: 시작·끝 따로 정렬 + 두 포인터. 배정까지: (끝, 채널) 최소 힙. 끝 순서·입력 순서 first-fit 은 깊이보다 많이 쓴다.
- 병합: 시작 순, 열린 덩어리 하나와만 비교, 늘릴 땐 `max`.
- 변형: k 채널 고정 → 끝 순서 + best fit (힙 top 은 틀림). 가중치 → DP + 이진 탐색.
- 펌웨어: 정렬이 시간을 지배, 힙은 스택(M4 에서 192 B), 매핑 제약·가드 시간·틱 wrap 을 말로 확인.

## 참고자료

- AlgoMonster 노트 M11 §1~§7 (Intervals · Merge Intervals · Insert Interval · Meeting Rooms · Meeting Rooms II · Non-overlapping Intervals · Minimum Number of Arrows) (Don 정리본) — `algomaster-coding-pattern/original-md/2026-09-27_M11_miscellaneous.md`
- T. H. Cormen et al., *Introduction to Algorithms* 3rd ed., §16.1 "An activity-selection problem" (4th ed. §15.1) — 반개구간 활동, 11개 예제와 답 {a1, a4, a8, a11}, 최적 부분 구조 + 그리디 선택 성질.
- J. Kleinberg, É. Tardos, *Algorithm Design*, §4.1 "Interval Scheduling: The Greedy Algorithm Stays Ahead" (틀린 규칙들의 반례 그림, Interval Partitioning 과 깊이 하한), §6.1 "Weighted Interval Scheduling".
- M. C. Golumbic, *Algorithmic Graph Theory and Perfect Graphs* — 구간 그래프는 perfect, 색 수 = 최대 클리크.
- M. C. Carlisle, E. L. Lloyd, "On the k-coloring of intervals", *Discrete Applied Mathematics* 59 (1995); U. Faigle, W. M. Nawijn, "Note on scheduling intervals on-line", *Discrete Applied Mathematics* 58 (1995) — k 채널 최대 선택의 best-fit 그리디 (서지 정보 확인 권장).
- ISO/IEC 9899:2011 (C11) 6.2.5p9 — 부호 없는 산술 wrap.
- STMicroelectronics 레퍼런스 매뉴얼: STM32F4 (RM0090) DMA request mapping 표, DMAMUX 가 있는 계열(G4/H7 등)의 DMAMUX 장 — 부품별 확인 권장. Linux kernel 문서 "DMA Engine API Guide" (`dma_request_chan`).
- LeetCode 56 Merge Intervals · 57 Insert Interval · 252 Meeting Rooms · 253 Meeting Rooms II · 435 Non-overlapping Intervals · 452 Minimum Number of Arrows to Burst Balloons · 986 Interval List Intersections · 1235 Maximum Profit in Job Scheduling.
- 관련 레슨: ALGO-B02 (반개구간 불변식, upper_bound), ALGO-B09 (투 포인터), ALGO-B11 (재귀 깊이), ALGO-M03 (백트래킹 — 매핑 제약 배정), ALGO-M08 (0/1 배낭 DP), ALGO-M10 (쿨다운·마감시간 스케줄링), ALGO-A09 (스위프 라인: 메모리 맵 겹침 쌍 전부, 최대 동시 이벤트), DS-B12 (이진 힙), DS-M11 (인덱스 우선순위 큐).
