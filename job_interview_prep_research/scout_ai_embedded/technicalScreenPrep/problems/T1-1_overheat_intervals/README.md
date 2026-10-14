# T1-1 · Overheat Intervals

> **ID**: T1-1 · **Priority**: P0 · **Pattern**: single pass + open/close interval · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T1-1` / `./run.sh T1-1 sol`

## 1. Problem

You are given temperature samples from one sensor, sorted by time. Each sample has a timestamp in milliseconds and a temperature in tenths of a degree Celsius (`815` means 81.5 °C).

Return every **overheat interval**: a maximal run of consecutive samples whose temperature is at or above `thresh`, lasting at least `min_ms`.

- An interval starts at the **first hot sample** of a run and ends at the **last hot sample** of that run.
- Duration is `end_ms - start_ms`. Keep the interval only if `duration >= min_ms`.
- Write at most `out_cap` intervals into `out`, but **always return the total number found**, so the caller can detect truncation. `out` may be `NULL` when `out_cap == 0`.
- Do not use `malloc`. Target O(n) time and O(1) extra memory.

```c
typedef struct { uint32_t t_ms; int16_t temp_c10; } Sample;   /* temp in 0.1 degC */
typedef struct { uint32_t start_ms, end_ms; } Interval;

size_t find_overheat(const Sample *s, size_t n, int16_t thresh, uint32_t min_ms,
                     Interval *out, size_t out_cap);
```

### Example

```
thresh = 800, min_ms = 1000
samples = (0,750) (500,810) (1000,820) (1500,830) (2000,790) (2500,850) (3000,700)

output  = 1 interval: {500, 1500}
          (the run at 2500 lasts 0 ms, so it is dropped)
```

### Constraints

- `0 <= n <= 100000`, timestamps strictly increasing, no wraparound (for this version).
- `temp_c10` is any `int16_t` value.

### Clarifying questions to ask (first 3 minutes)

1. Is "hot" `>= thresh` or `> thresh`?
2. Where does an interval end — at the last hot sample, or at the first cool sample after it?
3. Should a single hot sample count when `min_ms == 0`?
4. What should happen if more intervals are found than fit in `out`?
5. Could timestamps go backwards or wrap around?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh T1-1`로 바로 테스트하세요. 테스트 6개가 들어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 무엇을 기억해야 하나?</summary>

배열 전체를 다시 볼 필요가 없습니다. 한 번 순회하면서 **"지금 구간이 열려 있는가(`in_hot`)"**, **구간 시작 시각(`start`)**, **마지막 과열 샘플 시각(`last_hot`)** 세 개만 들고 다니면 됩니다.

</details>

<details>
<summary>Hint 2 — 구간이 닫히는 순간은 두 번</summary>

1. 과열 중에 **정상 샘플**을 만났을 때
2. **배열이 끝났는데 아직 과열 중**일 때 ← 가장 많이 빠뜨리는 케이스

</details>

<details>
<summary>Hint 3 — out_cap 처리</summary>

`if (cnt < out_cap) out[cnt] = …; cnt++;` 이 두 줄 패턴이면 버퍼가 작아도 넘치지 않고, 반환값은 항상 전체 개수입니다. `out == NULL && out_cap == 0`도 자동으로 안전합니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

예제 입력을 그래프로 그리면 이렇습니다. 빨간 점(≥ 800)이 연속으로 이어진 묶음이 "후보 구간"이고, 그중 길이가 `min_ms` 이상인 것만 남깁니다.

<div class="chart" data-src="chart1.svg" data-caption="예제 입력: 500~1500은 1000 ms라 통과, 2500은 샘플 1개(0 ms)라 탈락. 보라 점선은 '끝 = 첫 정상 샘플' 정의를 택했을 때의 구간"></div>

**정의가 답을 바꾼다**는 게 이 문제의 함정입니다. 끝을 "첫 정상 샘플"로 정의하면 같은 입력에서 `{500, 2000}`(1500 ms)과 `{2500, 3000}`(500 ms → 탈락)이 나옵니다. 그래서 질문 2번을 꼭 먼저 해야 합니다. 면접관은 이 질문을 하는지를 봅니다.

### 4.2 상태 머신

코드는 두 상태(COOL, HOT)를 오가는 작은 상태 머신입니다. 상태를 `bool in_hot` 하나로 표현합니다.

<div class="chart" data-src="chart2.svg" data-caption="상태 전이: 열기(OPEN) · 늘리기(EXTEND) · 닫기(CLOSE). 배열이 끝날 때의 CLOSE를 잊지 말 것"></div>

### 4.3 손으로 따라가기 (trace)

| i | t_ms | temp | hot? | 동작 | in_hot | start | last_hot | cnt |
|---|---|---|---|---|---|---|---|---|
| 0 | 0 | 750 | ✗ | stay | false | – | – | 0 |
| 1 | 500 | 810 | ✓ | **OPEN** | true | 500 | 500 | 0 |
| 2 | 1000 | 820 | ✓ | EXTEND | true | 500 | 1000 | 0 |
| 3 | 1500 | 830 | ✓ | EXTEND | true | 500 | 1500 | 0 |
| 4 | 2000 | 790 | ✗ | **CLOSE**: 1500−500 = 1000 ≥ 1000 → emit `{500,1500}` | false | | | 1 |
| 5 | 2500 | 850 | ✓ | **OPEN** | true | 2500 | 2500 | 1 |
| 6 | 3000 | 700 | ✗ | **CLOSE**: 0 < 1000 → 버림 | false | | | 1 |

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `last_hot - start`는 `uint32_t` 뺄셈입니다. 타임스탬프가 증가한다는 전제에서는 항상 0 이상이고, 나중에 랩어라운드를 허용해도 이 형태 그대로 안전합니다(§6 Follow-up).
- "닫고 내보내기" 코드가 루프 안과 루프 뒤에 두 번 나옵니다. 면접에서는 `static void emit(...)` 헬퍼로 빼도 되고, 그대로 두되 **"루프 뒤에 열린 구간 처리"**라고 말로 짚으면 됩니다.
- `(Interval){ start, last_hot }`는 C99 compound literal입니다. 낯설면 `out[cnt].start_ms = start; out[cnt].end_ms = last_hot;`로 써도 됩니다.

### 4.5 복잡도

- 시간 **O(n)** — 각 샘플을 한 번만 봅니다.
- 추가 메모리 **O(1)** — 변수 4개(`cnt`, `in_hot`, `start`, `last_hot`). 출력 버퍼는 호출자가 줍니다.

---

## 5. 임베디드 관점으로 말할 것

- **호출자 버퍼 + 전체 개수 반환**: MCU에서는 `malloc`을 쓰지 않습니다. 결과 크기를 모르면 "쓸 수 있는 만큼 쓰고, 필요한 개수를 돌려준다"가 표준 패턴입니다(`snprintf`와 같은 규칙).
- **정수 온도**: 0.1 °C 단위 `int16_t`라 FPU가 없어도 비교만으로 끝납니다. 범위는 ±3276.7 °C로 충분합니다.
- **스트리밍으로 바꾸기 쉬움**: 상태가 4개 변수뿐이라, 같은 로직을 `update(sample)` 함수로 바꾸면 ISR이 채운 링버퍼에서 샘플을 하나씩 꺼내 처리할 수 있습니다 → [T1-2](../T1-2_streaming_hysteresis/index.html)로 이어집니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| What if timestamps are `uint32_t` ticks that can wrap around? | 비교를 `last_hot - start >= min_ms`처럼 **뺄셈 결과**로만 하면 랩어라운드에도 맞습니다. `end > start + min_ms` 같은 덧셈 비교는 틀립니다. |
| What if two samples are far apart (a dropout)? | `max_gap_ms`를 받아서, 이전 샘플과 간격이 그보다 크면 열린 구간을 강제로 닫습니다. 센서가 죽은 동안을 과열로 쳐 주면 안 되기 때문입니다. |
| Make it streaming. | 상태 4개를 구조체에 넣고 `update()`로 샘플 하나씩 처리, 구간이 닫힐 때 이벤트를 반환 → T1-2. |
| Readings flicker around the threshold. | 높은 임계값으로 시작, 낮은 임계값으로 회복하는 **히스테리시스**, 또는 연속 N회 확인 → T1-2. |
| What is the complexity? Can it be better? | O(n)/O(1). 모든 샘플을 한 번은 봐야 하므로 이보다 나을 수 없습니다. |

## 7. 자주 하는 실수

- 루프가 끝난 뒤 **열린 구간을 닫지 않음** → "끝까지 과열" 테스트(3번) 실패
- `>`와 `>=` 혼동 → 테스트 4번 실패
- `out_cap`을 넘어서 씀(버퍼 오버런) 또는 `out_cap`에서 멈추고 개수도 멈춤 → 테스트 6번 실패
- 구간 길이를 샘플 개수로 셈 → 샘플 간격이 일정하지 않으면 틀림
