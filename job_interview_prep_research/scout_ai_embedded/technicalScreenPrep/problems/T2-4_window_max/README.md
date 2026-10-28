# T2-4 · Peak Temperature over the Last W Samples

> **ID**: T2-4 · **Priority**: P1 · **Pattern**: monotonic deque in a fixed array (LeetCode 239) · **Time**: 25 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T2-4` / `./run.sh T2-4 sol`

## 0. 문제 한국어 소개 (먼저 읽기)

> 영어 문제(§1)를 읽기 전에 **무엇을 만들라는 건지** 먼저 잡기 위한 요약입니다. 실제 풀이와 테스트는 §1 영어 문제 기준입니다.

- **한 줄 요약**: 샘플이 하나 들어올 때마다 "최근 `w`개 샘플의 최댓값"을 amortized O(1)로 돌려주는 sliding window max(monotonic deque)를 고정 배열로 만든다.
- **상황**: 모터 드라이버 열 감시기가 tick마다 온도 샘플 하나를 받는다. 팬 제어기는 평균이 아니라 최근 `w`개의 **최고 온도**에 반응한다. 매번 `w`개를 다시 훑는 것은 너무 느리다.
- **입력**: `wm_init(m, w)`의 창 길이 `w`(샘플 개수, `1 <= w <= 16`), `wm_push(m, temp)`의 온도 `temp`(`int16_t`, 0.1 °C, 음수 가능). 구조체 `WinMax`(`w`, 다음 샘플 번호 `seq`, free-running `front`/`back`, `dq[16]`).
- **출력**: `wm_push`는 최근 `w`개 샘플의 최댓값을 반환한다. 아직 `w`개가 안 모였으면 지금까지 들어온 전체의 최댓값.
- **규칙**
  - deque는 구조체 안의 `W_MAX`(16)칸 배열에 둔다. `malloc` 없음.
  - push 한 번은 amortized O(1).
  - `W_MAX`는 2의 거듭제곱이라 deque 칸 번호는 `DQ_MASK`로 마스킹.
- **예제 풀어 읽기**: `w = 3`, 입력 810 790 830 820 800 780 850. 처음 두 번은 지금까지의 최대 810, 810. 830이 오면 830. 이후 창(830,820,800)까지 830 유지. 780이 들어오면 830이 창 밖으로 나가 창(820,800,780)의 최대 820. 850이 오면 850. 결과 810 810 830 830 830 820 850.
- **놓치기 쉬운 것**: `w = 1`(현재 샘플이 곧 최대), 계속 감소하는 입력에서 `w = 16`이면 deque가 정확히 `w`개까지 차는 경우, 음수 온도와 같은 값(동점), 무작위 2000개 입력을 brute force와 비교, `seq`를 `UINT32_MAX - 1`에서 시작해 번호가 wrap하는 경우.
- **구현할 함수**: `wm_init`(창 길이 설정, 상태 초기화), `wm_push`(샘플 추가 후 창 최댓값 반환).

## 1. Problem

The motor-driver thermal monitor gets one temperature sample per tick and must report the **peak of the last `w` samples** after every sample (the fan controller reacts to the peak, not the average). Rescanning `w` samples on every tick is too slow at high rates; make each push **amortized O(1)**.

- `wm_init(m, w)` with `1 <= w <= W_MAX` (16).
- `wm_push(m, temp)` adds one sample and returns the max of the last `w` samples, or of all samples so far if fewer than `w` have arrived.
- Temperatures are `int16_t` in 0.1 °C and may be negative.
- Fixed memory: the deque lives in a `W_MAX`-entry array inside the struct. No `malloc`.

```c
#define W_MAX   16u                       /* power of two */
#define DQ_MASK (W_MAX - 1u)

typedef struct { uint32_t seq; int16_t temp; } Entry;
typedef struct {
    uint32_t w;                           /* window length in samples           */
    uint32_t seq;                         /* sequence number of the NEXT sample */
    uint32_t front, back;                 /* free-running deque indices         */
    Entry dq[W_MAX];
} WinMax;

void    wm_init(WinMax *m, uint32_t w);
int16_t wm_push(WinMax *m, int16_t temp);
```

### Example

```
w = 3
push:   810  790  830  820  800  780  850
return: 810  810  830  830  830  820  850
```

### Constraints

- Up to millions of pushes: `seq` will wrap past `UINT32_MAX`.
- `1 <= w <= W_MAX`.

### Clarifying questions to ask (first 3 minutes)

1. Is the window "last `w` samples" or "last `W` milliseconds"? (Time-based windows need timestamps in the deque.)
2. What should the first `w - 1` calls return — the max so far, or nothing?
3. Is `w` fixed at init, and what is its upper bound (so I can size a static array)?
4. Ties: does it matter which equal sample is kept?
5. Is O(w) per sample actually too slow here? (If `w = 8` at 10 Hz, brute force is fine — say so.)

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh T2-4`로 바로 테스트하세요. 테스트 6개가 들어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 다시는 최대가 될 수 없는 샘플</summary>

새 샘플 `x`가 들어왔을 때, 그보다 **먼저 들어왔고 값이 `x` 이하인** 샘플은 앞으로 영원히 최대가 될 수 없습니다(`x`보다 먼저 창을 떠나니까). 그런 샘플은 지금 버려도 됩니다.

</details>

<details>
<summary>Hint 2 — 무엇을 저장하나</summary>

남은 샘플들은 앞(front)에서 뒤(back)로 **값이 엄격하게 감소**합니다. 그래서 최대는 항상 front입니다. 값과 함께 **순번(`seq`)**을 저장해야 front가 창 밖으로 나갔는지 판단할 수 있습니다.

</details>

<details>
<summary>Hint 3 — 순서: 만료 → 뒤에서 pop → push</summary>

push 전에 front를 먼저 만료시키면 deque 크기가 항상 `w` 이하라서 `W_MAX` 배열이 넘치지 않습니다. 만료 조건은 wrap-safe 뺄셈 `seq - front.seq >= w`.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — deque 변화 보기

예제(w = 3)를 한 단계씩 따라가면 deque에는 "앞으로 최대가 될 가능성이 있는 샘플"만 남습니다. 빨간 상자(front)가 답입니다.

<div class="chart" data-src="chart1.svg" data-caption="i = 2에서 830이 790과 810을 밀어내고, i = 5에서 830(#2)이 창 밖으로 만료되어 820이 front가 됩니다"></div>

### 4.2 신호로 보기

같은 데이터를 시간축에 그리면, 출력(빨간 점선)은 창 안의 가장 높은 점을 따라가는 계단입니다.

<div class="chart" data-src="chart2.svg" data-caption="i = 5의 창은 i = 3..5(820, 800, 780). 830은 막 창을 떠나서 최대가 820으로 내려갑니다"></div>

### 4.3 손으로 따라가기 (trace)

`front`, `back`은 free-running 인덱스입니다(`size = back - front`). 표의 값은 `wm_push` 직후입니다.

| i | temp | 만료 검사 (seq − front.seq ≥ 3?) | 뒤에서 pop | deque (#seq temp) | front | back | 반환 |
|---|---|---|---|---|---|---|---|
| 0 | 810 | 비어 있음 | – | #0 810 | 0 | 1 | 810 |
| 1 | 790 | 1 − 0 = 1 ✗ | – | #0 810, #1 790 | 0 | 2 | 810 |
| 2 | 830 | 2 − 0 = 2 ✗ | 790, 810 | #2 830 | 0 | 1 | 830 |
| 3 | 820 | 3 − 2 = 1 ✗ | – | #2 830, #3 820 | 0 | 2 | 830 |
| 4 | 800 | 4 − 2 = 2 ✗ | – | #2 830, #3 820, #4 800 | 0 | 3 | 830 |
| 5 | 780 | 5 − 2 = **3** ✓ → #2 만료 | – | #3 820, #4 800, #5 780 | 1 | 4 | **820** |
| 6 | 850 | 6 − 3 = **3** ✓ → #3 만료 | 780, 800 | #6 850 | 2 | 3 | 850 |

i = 2에서 `back`이 2 → 0으로 줄었다가 push로 1이 되는 점에 주목하세요. `front`, `back` 모두 free-running이라 슬롯 위치는 `& DQ_MASK`로 접습니다.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `<=`로 pop하므로 같은 값이면 **새 샘플이 남습니다**. 새 샘플이 더 오래 창에 있으니 deque가 더 작아집니다(테스트 4번의 −200 두 개).
- 만료는 한 번의 `if`로 충분합니다. 매 push마다 seq가 1씩 늘어서, 한 번에 창을 벗어나는 샘플은 많아야 하나이기 때문입니다.
- **만료를 push 뒤에 하면** 순간적으로 deque에 `w + 1`개가 들어가서, `w == W_MAX`일 때 front 슬롯을 덮어씁니다. 테스트 3번(w = 16, 계속 감소하는 입력이라 deque가 꽉 참)이 이 버그를 잡습니다. 무작위 입력(테스트 5번)으로는 16개 연속 감소가 거의 안 나와서 못 잡습니다.
- 테스트 6번은 `seq`를 `UINT32_MAX - 1`에서 시작합니다. `seq - front.seq` 뺄셈이라 랩어라운드에도 맞습니다.

### 4.5 복잡도

- 시간: 각 샘플은 deque에 한 번 들어가고 많아야 한 번 나오므로 **amortized O(1)**. 단, 한 번의 push가 최악 O(w)일 수 있습니다(i = 6처럼).
- 메모리: `W_MAX × 8 B` + 16 B. 원본 샘플은 저장하지 않습니다.

---

## 5. 임베디드 관점으로 말할 것

- **worst-case vs amortized**: 하드 실시간 루프에서는 "평균 O(1)"보다 "최악 몇 사이클"이 중요합니다. 이 구조의 최악은 pop `w`번이고 `w <= 16`이라 상한이 명확하다고 말합니다.
- **작은 w면 brute force**: `w = 8`이면 8개 비교가 deque 관리보다 빠르고 코드도 짧습니다. 면접관에게 "w가 크거나 샘플 속도가 높을 때 이 구조가 이득"이라고 트레이드오프를 말하면 좋습니다.
- **고정 배열 + 마스크**: [T2-1 링버퍼](../T2-1_ring_buffer/index.html)와 같은 free-running 인덱스 기법입니다. 이 deque는 양쪽 끝에서 빠지므로 `back--`도 씁니다.
- **ISR 안에서 호출**: 샘플 ISR에서 바로 `wm_push`를 부르면, 최대값을 읽는 task와 경쟁하지 않도록 반환값만 `volatile int16_t peak`에 저장합니다(16비트 store는 원자적).

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Window is the last `W_ms` milliseconds, not `w` samples. | `Entry`에 `t_ms`를 저장하고 만료 조건을 `now - front.t_ms >= W_ms`로 바꿉니다. 이때 한 번에 여러 개가 만료될 수 있으니 `if`를 `while`로 바꾸고, 샘플 속도 상한으로 배열 크기를 정합니다. |
| Also report the minimum. | 오름차순 deque를 하나 더 둡니다(pop 조건 `>=`). 메모리 2배, 로직 동일. |
| Report the average instead. | 합계 하나와 링버퍼로 O(1): 새 값 더하고 나가는 값 빼기. `int32_t` 합계([T1-3](../T1-3_moving_average/index.html)). |
| Why not a max-heap? | 힙은 O(log w)이고, 창을 떠난 원소를 지우려면 lazy deletion이 필요합니다. deque가 더 단순하고 빠릅니다. |
| Batch version: given an array, output all window maxima. | 같은 구조로 `i >= w - 1`부터 출력하면 LeetCode 239 그대로, 출력 `n - w + 1`개. |

## 7. 자주 하는 실수

- 값만 저장하고 인덱스(seq)를 안 저장 → 만료 판단 불가
- 만료를 push **뒤에** 처리 → `w == W_MAX`에서 front 슬롯을 덮어씀(테스트 3번)
- pop 조건을 `<`로 써서 같은 값이 쌓임 → 결과는 맞지만 deque가 커지고 최악 시간이 늘어남. `<=`를 쓰는 이유를 말할 것
- 처음 `w - 1`개 샘플에서 아무것도 반환하지 않음 → 테스트 1번 실패. 정의를 물어볼 것
- `front`/`back`을 `uint8_t`로 두고 마스크 없이 인덱싱 → 범위 밖 접근
