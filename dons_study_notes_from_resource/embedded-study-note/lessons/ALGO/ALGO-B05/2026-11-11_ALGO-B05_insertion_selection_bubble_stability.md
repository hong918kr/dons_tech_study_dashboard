---
id: ALGO-B05
time: 60분
updated: 2026-11-11
---
# ALGO-B05 기본 정렬 3종과 안정성: 삽입·선택·버블

## 학습 목표

- 삽입(insertion)·선택(selection)·버블(bubble, 조기 종료) 정렬을 (키, 원래 순서) 레코드 위에서 off-by-one 없이 5분 안에 쓴다.
- **안정성(stability)** 을 "같은 키끼리 원래 순서(idx)가 오름차순으로 남는가"로 검사하고, 선택 정렬이 불안정한 **가장 작은 반례**(n = 3)와 `<=` 한 글자로 삽입·버블이 불안정해지는 버그를 직접 보인다.
- 비교·교환·밀기(shift)·쓰기 횟수를 세어 닫힌 식과 맞춘다: 삽입 밀기 = 역전 수(inversions), 삽입 비교 = inv + n − 1 − z, 선택 비교 = n(n−1)/2, 버블 교환 = inv, 버블 비교 = 패스 수 공식.
- 펌웨어에서 "언제 O(n²) 정렬이 정답인가"를 말한다: N ≤ 32 중앙값 필터 창(거의 정렬된 입력 → 삽입 정렬 O(N)), EEPROM 쓰기 최소화(선택 정렬 ≤ n − 1 교환), 표준 라이브러리의 작은 구간 삽입 정렬(introsort cutoff).

## 1. 문제

O(n²) 정렬은 "느린 정렬"이 아니라 **작은 n 에서 가장 빠르고, 메모리 0, 재귀 0, 최악이 결정적인** 정렬이다 — 그래서 펌웨어 면접에서는 "구현해 봐 → 안정한가? → 거의 정렬된 입력이면? → 플래시에 쓴다면?" 순서로 꼬리질문이 이어진다.

정렬 대상은 숫자가 아니라 **레코드**다. 센서 이벤트 `(key = 측정값, idx = 들어온 순서)` 같은 것이다. 키가 같은 두 레코드가 정렬 뒤에도 원래 순서를 지키면 그 정렬은 **안정(stable)** 하다. 키만 보는 정렬에서는 안정성이 안 보이지만, 레코드에 다른 정보(타임스탬프, 채널 번호)가 붙는 순간 버그가 된다 — "같은 온도면 먼저 들어온 이벤트가 먼저"라는 요구가 그런 예다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">입력 키 [3,1,3,2,1], 글자 = 원래 순서(idx 0→a, 1→b …)</text>
  <g fill="currentColor"><text x="10" y="62">입력</text><text x="10" y="132">선택 i=1</text><text x="10" y="202">선택 결과</text><text x="10" y="262">삽입·버블 결과</text></g>
  <g stroke="currentColor" fill="none">
    <rect x="130" y="42" width="44" height="28"/><rect x="178" y="42" width="44" height="28"/><rect x="226" y="42" width="44" height="28"/><rect x="274" y="42" width="44" height="28"/><rect x="322" y="42" width="44" height="28"/>
    <rect x="130" y="112" width="44" height="28"/><rect x="274" y="112" width="44" height="28"/>
    <rect x="130" y="182" width="44" height="28"/><rect x="178" y="182" width="44" height="28"/><rect x="226" y="182" width="44" height="28"/>
    <rect x="130" y="242" width="44" height="28"/><rect x="178" y="242" width="44" height="28"/><rect x="226" y="242" width="44" height="28"/>
  </g>
  <rect x="178" y="112" width="44" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="226" y="112" width="44" height="28" class="f-mut" stroke="currentColor"/>
  <rect x="322" y="112" width="44" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="274" y="182" width="44" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="322" y="182" width="44" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="274" y="242" width="44" height="28" class="f-ok" stroke="currentColor"/>
  <rect x="322" y="242" width="44" height="28" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="152" y="61">3a</text><text x="200" y="61">1b</text><text x="248" y="61">3c</text><text x="296" y="61">2d</text><text x="344" y="61">1e</text>
    <text x="152" y="131">1b</text><text x="200" y="131">3a</text><text x="248" y="131">3c</text><text x="296" y="131">2d</text><text x="344" y="131">1e</text>
    <text x="152" y="201">1b</text><text x="200" y="201">1e</text><text x="248" y="201">2d</text><text x="296" y="201">3c</text><text x="344" y="201">3a</text>
    <text x="152" y="261">1b</text><text x="200" y="261">1e</text><text x="248" y="261">2d</text><text x="296" y="261">3a</text><text x="344" y="261">3c</text>
  </g>
  <path d="M200 112 Q272 84 344 112" stroke="currentColor" fill="none" stroke-width="1.6"/>
  <text x="272" y="88" fill="currentColor" text-anchor="middle" font-size="12">swap: 3a 가 3c 를 뛰어넘는다</text>
  <g fill="currentColor">
    <text x="390" y="62">5개 레코드, 같은 키 두 쌍: (1b,1e), (3a,3c)</text>
    <text x="390" y="125">선택 정렬은 최솟값 1e 를 i=1 자리로 "멀리" 교환한다.</text>
    <text x="390" y="143">그 순간 a[1]=3a 가 a[4] 로 날아가 3c 뒤에 선다.</text>
    <text x="390" y="201">3c 3a — 같은 키의 순서가 뒤집힘 = 불안정</text>
    <text x="390" y="261">3a 3c — 인접한 원소끼리만 움직이고, 같으면</text>
    <text x="390" y="279">멈추므로(엄격한 &lt;) 순서가 유지됨 = 안정</text>
  </g>
</svg>
```

### Part A — 정렬 3종 + 횟수 세기

`Rec { int16_t key; uint16_t idx; }` 배열을 키 오름차순으로 제자리(in-place) 정렬한다. 비교는 반드시 주어진 `rec_less()`(키만, 엄격한 `<`)로 하고, 교환은 `rec_swap()` 으로 한다. 그래야 `SortStats` 가 자동으로 맞게 세진다.

- `insertion_sort` — 꺼낸 원소보다 **큰** 앞 원소를 한 칸씩 민다(shift: 배열 쓰기 1번). 빈 자리에 넣는 쓰기는 자리가 바뀐 경우에만.
- `selection_sort` — `[i, n)` 의 최솟값(같으면 첫 번째)을 찾아 `a[i]` 와 교환. 이미 제자리면 교환하지 않는다.
- `bubble_sort` — 인접 쌍이 뒤집혀 있으면 교환. 한 패스에 교환이 0번이면 **조기 종료(early exit)**.

### Part B — 안정성: 검사기, 반례, 버그, 고치기

- `is_stable_sorted(a, n)` — 키가 오름차순이고, 같은 키끼리는 idx 가 오름차순이면 1.
- 선택 정렬이 불안정한 최소 반례 `[(2,0), (2,1), (1,2)]` 를 테스트로 확인한다 (n ≤ 2 로는 불가능 — 연습 3 에서 전수 탐색으로 증명).
- 주어진 버그 판 `insertion_sort_le`(`x <= a[j-1]` 이면 민다), `bubble_sort_ge`(`a[j] >= a[j+1]` 이면 교환)가 같은 키 **두 개**만으로 안정성을 깨는 것, 그리고 `>=` 버블은 전부 같은 키에서 조기 종료가 영영 안 걸리는 것을 확인한다.
- `selection_sort_stable` — 교환 대신 **회전(rotate)**: 최솟값을 꺼내 `a[i..m-1]` 을 한 칸씩 밀고 `a[i]` 에 넣는다.

### Part C — 펌웨어: 중앙값 필터 창과 EEPROM 쓰기 횟수

12비트 ADC 를 1 kHz 로 읽는 태스크가 있다. 가끔 EMI 로 0 이나 4095 가 튄다. 최근 N 개(N ≤ 32) 의 **중앙값(median)** 을 내보내는 필터를 만든다.

- `MedWin` 은 도착 순서 링 `ring[]` 과 **정렬된 사본** `sorted[]` 를 같이 들고 있다 (malloc 없음, 구조체 하나 144 B).
- `medwin_push(w, x)` — 창이 찼으면 가장 오래된 값을 `sorted[]` 에서 찾아 `x` 로 **덮어쓰고**, `sorted[]` 전체에 삽입 정렬을 한 번 돌린다. 한 원소만 틀린 배열이므로 비교가 **≤ 2N − 3** 으로 묶인다(테스트가 확인). 짝수 개면 아래쪽 중앙값 `sorted[(count-1)/2]`.
- 같은 하네스에서 32 개 레코드를 "EEPROM 안에서" 제자리 정렬할 때 세 정렬의 **쓰기 횟수**를 비교한다: 선택 ≤ 2(n − 1).

## 2. Problem (English)

> **Elementary sorts and stability.**
>
> ```c
> typedef struct { int16_t key; uint16_t idx; } Rec;      /* idx = original position */
> typedef struct { uint32_t cmp, swaps, shifts, writes; } SortStats;
> void insertion_sort(Rec *a, int n, SortStats *st);
> void selection_sort(Rec *a, int n, SortStats *st);
> void bubble_sort(Rec *a, int n, SortStats *st);          /* stop after a pass with no swap */
> int  is_stable_sorted(const Rec *a, int n);
> void selection_sort_stable(Rec *a, int n, SortStats *st);
> int16_t medwin_push(MedWin *w, int16_t x);               /* sliding median, window N <= 32 */
> ```
>
> Sort in place by `key` only, using the provided `rec_less` / `rec_swap` so that comparisons, swaps, shifts and array writes are counted. Constraints: `0 <= n <= 1024`, no dynamic allocation, O(1) extra memory, no recursion. Insertion and bubble sort must be stable; explain why selection sort is not and give the smallest counterexample. For the median filter, the cost per sample must be O(N) in the worst case.
>
> Example: keys `[3,1,3,2,1]` (idx 0..4)
> - insertion / bubble → `(1,1) (1,4) (2,3) (3,0) (3,2)`; insertion: cmp = 9, shifts = 6, writes = 9; bubble: cmp = 10, swaps = 6.
> - selection → `(1,1) (1,4) (2,3) (3,2) (3,0)` — cmp = 10, swaps = 3, **not stable**.
> - `is_stable_sorted` returns 1 for the first output, 0 for the second.
>
> Follow-ups: How many comparisons does insertion sort make on an input with k inversions? Which of the three minimizes writes to flash? Why do production sort routines switch to insertion sort for small partitions?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여섯 군데(`insertion_sort`, `selection_sort`, `bubble_sort`, `is_stable_sorted`, `selection_sort_stable`, `medwin_push`). 정렬 스텁은 아무것도 안 하고, 검사기 스텁은 0 을, 필터 스텁은 들어온 값을 그대로 돌려준다. 처음 실행 결과(실제 출력, 중간 생략):

```text
  insertion: edge case 4 wrong
…
random 600 arrays: sort mismatches ins=574 sel=574 bub=574, formula mismatches ins=579 sel=579 bub=579
…
median N=32: 6000 samples, mismatches=6000, max cmp/push=0 (bound 2N-3=61)
FAIL starter.c:330  bad == 0 && maxcmp <= (uint32_t)bound
FAIL starter.c:340  worst == 1000
EEPROM n=32 random: writes ins=0 sel=0 bub=0
FAIL starter.c:351  s3[1].writes <= 62u && s3[1].writes < s3[0].writes && s3[0].writes < s3[2].writes
5/33 passed
```

(exit 1. 통과한 5 개는 스텁과 우연히 맞는 것들 — 0 을 돌려줘야 하는 검사기 사례 2 개, 주어진 버그 판(`<=`, `>=`) 확인 2 개, "선택 정렬이 불안정한 입력이 있다" 1 개.) 하네스는 모든 정렬 결과를 `qsort` 로 (key, idx) 사전순 정렬한 **기준 답**과 비교하고, 횟수는 테스트 쪽에서 O(n²) 로 직접 센 역전 수 등으로 만든 공식과 비교한다.

## 4. 힌트

<details><summary>힌트 1 — 삽입 정렬: 꺼내 두고 민다</summary>

`Rec x = a[i]; int j = i;` 다음 `while (j > 0 && rec_less(x, a[j-1], st))` 동안 `a[j] = a[j-1]; j--;`. **`j > 0` 을 먼저** 써야 `a[-1]` 을 읽지 않는다(`&&` 단락 평가). 조건이 `x < a[j-1]` 이므로 같은 키에서 멈춘다 — 그게 안정성의 전부다.

</details>

<details><summary>힌트 2 — 선택 정렬: 교환은 바깥 루프에서 한 번</summary>

안쪽 루프는 `m` 만 갱신하고, 교환은 안쪽 루프가 끝난 뒤 `if (m != i)` 일 때 한 번. 안쪽에서 바로바로 교환하면 결과는 맞아도 교환 횟수가 O(n²) 가 된다 (7. 실수 4).

</details>

<details><summary>힌트 3 — 버블 정렬: 범위와 플래그</summary>

패스 p 가 끝나면 마지막 p+1 칸은 최종 자리다. 그래서 `j + 1 < n - pass`. `swapped` 는 **패스마다** 0 으로 초기화하고, 패스가 끝나서 0 이면 `break`.

</details>

<details><summary>힌트 4 — 안정 선택 정렬은 "멀리 교환"을 "회전"으로</summary>

불안정의 원인은 `a[i]` 가 `a[m]` 자리로 **날아가며** 같은 키를 뛰어넘는 것이다. `x = a[m]` 을 꺼내고 `a[i..m-1]` 을 오른쪽으로 한 칸씩 밀면 그 구간의 상대 순서가 보존된다. 대가: 쓰기가 O(n²).

</details>

<details><summary>힌트 5 — 중앙값 창: 덮어쓰고 다시 삽입 정렬</summary>

가장 오래된 값은 `ring[head]`. `sorted[]` 에서 같은 값을 아무거나 하나 찾아 새 값으로 덮어쓰면(정수 키라 같은 값은 구별할 필요가 없다) 배열은 "한 원소만 틀린" 상태다. 거기에 평범한 삽입 정렬을 돌리면 끝. 비교 수를 `cmp_last` 에 넣는 것을 잊지 말 것.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 질문으로 범위 확인 | n 범위? (≤ 1024, 필터는 ≤ 32) · 키만 비교? · 안정성 필요? · 제자리·malloc 금지? · 입력이 거의 정렬돼 있나? · 쓰기 비용이 비싼 매체인가? |
| 2. 무식한 방법 + 복잡도 | 셋 다 O(n²) 시간, O(1) 추가 메모리, 스택 O(1). "왜 O(n log n) 을 안 쓰나?" 에 대한 답을 미리: n 이 작고, 결정적이고, 코드가 짧다 |
| 3. 핵심 아이디어 + 불변식 | 삽입: `[0,i)` 는 정렬·안정. 선택: `[0,i)` 는 전체에서 가장 작은 i 개의 최종 배치. 버블: 패스 p 뒤 `[n-1-p, n)` 은 최종 |
| 4. 코드 | `j > 0 &&` 순서, 엄격한 `<`, `swapped` 초기화 위치 |
| 5. 소리 내어 말할 테스트 | n=0, 1, 2(뒤집힘), 전부 같은 키, 정렬됨, 역순, 같은 키 섞임 `[3,1,3,2,1]`, int16 극값 −32768/32767 |
| 6. 복잡도 다시 | 시간: 최선/평균/최악 (삽입 n−1 / ~n²/4 / n²/2 비교). 추가 메모리 O(1), 스택 O(1), 교환·쓰기 횟수까지 |

### Part A — 삽입 정렬: 불변식과 역전 수

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">삽입 정렬 한 단계 (i = 3, 키 [1b 3a 3c | 2d | 1e])</text>
  <rect x="40" y="50" width="240" height="34" class="f-ok" stroke="currentColor"/>
  <rect x="280" y="50" width="80" height="34" class="f-acc" stroke="currentColor"/>
  <rect x="360" y="50" width="80" height="34" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="80" y="72">1b</text><text x="160" y="72">3a</text><text x="240" y="72">3c</text><text x="320" y="72">2d</text><text x="400" y="72">1e</text>
    <text x="40" y="104" font-size="12">0</text><text x="280" y="104" font-size="12">i</text><text x="440" y="104" font-size="12">n</text>
  </g>
  <text x="160" y="44" fill="currentColor" text-anchor="middle">[0, i): 정렬 + 안정 (불변식)</text>
  <text x="400" y="44" fill="currentColor" text-anchor="middle">아직 안 봄</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M160 116 L232 116"/><path d="M226 111 L234 116 L226 121"/>
    <path d="M240 116 L312 116"/><path d="M306 111 L314 116 L306 121"/>
  </g>
  <g fill="currentColor">
    <text x="40" y="140">x = 2d 를 꺼낸다 → 3c, 3a 는 x 보다 크다 → 한 칸씩 오른쪽으로 (shift 2번, 비교 2번)</text>
    <text x="40" y="160">1b 는 x 보다 작다 → 멈춤 (비교 1번). 결과 [1b 2d 3a 3c | 1e]</text>
    <text x="40" y="180">x 와 키가 같은 원소를 만나도 멈춘다 → 같은 키는 절대 서로를 넘지 않는다 = 안정</text>
  </g>
  <rect x="470" y="40" width="260" height="78" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="482" y="62">shift 1번 = x 가 자기보다 큰</text>
    <text x="482" y="80">원소 하나를 넘는다 = 역전 1개 해소</text>
    <text x="482" y="104" font-weight="bold">⇒ 총 shift 수 = 역전 수 inv</text>
  </g>
</svg>
```

**불변식**: 바깥 루프 i 가 시작될 때 `a[0..i)` 는 원래 `a[0..i)` 의 원소들을 키 오름차순, 같은 키는 idx 오름차순으로 담고 있다. x 는 자기보다 **엄격히 큰** 원소만 넘으므로, 같은 키 원소(원래 x 보다 앞에 있던 것) 뒤에 선다 → 불변식 유지.

실제 실행 기록(이 레슨 작업용 trace 프로그램 출력):

| i | x | 비교 | shift | 결과 |
|---|---|---|---|---|
| 1 | 1b | 1 | 1 | 1b 3a 3c 2d 1e |
| 2 | 3c | 1 (3a 와 같음 → 멈춤) | 0 | 1b 3a 3c 2d 1e |
| 3 | 2d | 3 | 2 | 1b 2d 3a 3c 1e |
| 4 | 1e | 4 (1b 와 같음 → 멈춤) | 3 | 1b 1e 2d 3a 3c |
| 합 | | **9** | **6** | 안정 |

**횟수 공식 (정확한 식)**. 역전(inversion) = `i < j` 이고 `key[i] > key[j]` 인 쌍. 위 입력의 역전은 (3a,1b)(3a,2d)(3a,1e)(3c,2d)(3c,1e)(2d,1e) = 6 개.

- shift = **inv**. x 가 한 칸 넘을 때마다 x 와 "x 앞에 있던 더 큰 원소" 쌍 하나가 해소되고, 다른 쌍의 상대 순서는 안 바뀐다.
- 비교 = **inv + (n − 1) − z**. 단계마다 "shift 를 일으킨 비교" + "멈추게 한 비교 1번" 인데, x 가 맨 앞(j = 0)까지 가면 멈추는 비교가 없다. 그런 단계 수가 z = "앞의 모든 키보다 **엄격히** 작은 새 최솟값"의 개수(i ≥ 1). 위 예: 1b 만 해당(1e 는 1b 와 같으니 아님) → 6 + 4 − 1 = 9 ✓.
- 쓰기 = inv + (한 칸이라도 움직인 원소 수). 위 예: 6 + 3 = 9.

그래서 최선(정렬됨) n − 1 비교·쓰기 0, 최악(역순, 서로 다른 키) inv = n(n−1)/2 이고 z = n−1 이라 비교도 정확히 n(n−1)/2. 무작위 순열이면 E[inv] = n(n−1)/4, E[z] = H_n − 1 (H_n = 1 + 1/2 + … + 1/n) 이다. **입력이 거의 정렬돼 있으면 inv 가 작으니 O(n + inv)** — 이것이 이 레슨의 핵심 한 줄이다.

### Part A — 선택 정렬: 비적응형, 교환이 적다

불변식: 바깥 루프 i 가 시작될 때 `a[0..i)` 는 전체에서 가장 작은 i 개를 오름차순으로 담고 있고, 다시는 바뀌지 않는다. 안쪽 루프는 입력과 상관없이 n−1−i 번 비교 → 총 비교는 **항상 n(n−1)/2** (정렬된 입력이어도). 교환은 단계당 최대 1 번 → **≤ n − 1**, 쓰기 ≤ 2(n − 1). 역순 입력은 정확히 ⌊n/2⌋ 교환 (바깥쪽 쌍부터 맞춰지고 나머지는 이미 제자리).

### Part A — 버블 정렬: 교환 = 역전 수, 패스 수 = 왼쪽 큰 원소 수의 최댓값 + 1

인접 교환 1 번은 정확히 역전 1 개를 없앤다 → 교환 = **inv** (삽입의 shift 와 같은 수지만, 교환은 쓰기 2 번이라 쓰기는 2·inv). 비교 수는 패스 수 P 로 정해진다: 패스 p 는 n−1−p 번 비교하므로 비교 = Σ_{p<P} (n−1−p) = P(n−1) − P(P−1)/2.

P 는 이렇게 구한다. 원소마다 b = "왼쪽에 있는 **엄격히** 더 큰 원소 수"(역전 표, inversion table)를 두면, 버블 한 패스는 **0 이 아닌 b 를 모두 정확히 1 씩 줄인다** (Knuth 5.2.2). 그래서 교환이 있는 패스 수 = max b, 여기에 "교환 없음"을 확인하는 패스 1 번(단, 이미 n−1 패스면 루프가 끝나므로 없음) → **P = min(n − 1, max b + 1)**. 같은 키가 많은 무작위 입력(키 0..3)까지 600 개 배열에서 이 식이 비교 횟수와 한 번도 어긋나지 않았다 (solution.c 의 `formula mismatches … bub=0`).

```svg
<svg viewBox="0 0 740 220" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">버블의 거북이(turtle): [2 3 4 5 6 1] — 작은 1 은 패스마다 한 칸씩만 왼쪽으로</text>
  <g fill="currentColor"><text x="10" y="56">입력</text><text x="10" y="82">패스 1</text><text x="10" y="108">패스 2</text><text x="10" y="134">패스 3</text><text x="10" y="160">패스 4</text><text x="10" y="186">패스 5</text></g>
  <g stroke="currentColor" fill="none">
    <rect x="80" y="40" width="180" height="22"/><rect x="80" y="66" width="180" height="22"/><rect x="80" y="92" width="180" height="22"/>
    <rect x="80" y="118" width="180" height="22"/><rect x="80" y="144" width="180" height="22"/><rect x="80" y="170" width="180" height="22"/>
  </g>
  <rect x="230" y="40" width="30" height="22" class="f-warn"/><rect x="200" y="66" width="30" height="22" class="f-warn"/>
  <rect x="170" y="92" width="30" height="22" class="f-warn"/><rect x="140" y="118" width="30" height="22" class="f-warn"/>
  <rect x="110" y="144" width="30" height="22" class="f-warn"/><rect x="80" y="170" width="30" height="22" class="f-ok"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="95" y="56">2</text><text x="125" y="56">3</text><text x="155" y="56">4</text><text x="185" y="56">5</text><text x="215" y="56">6</text><text x="245" y="56">1</text>
    <text x="95" y="82">2</text><text x="125" y="82">3</text><text x="155" y="82">4</text><text x="185" y="82">5</text><text x="215" y="82">1</text><text x="245" y="82">6</text>
    <text x="95" y="108">2</text><text x="125" y="108">3</text><text x="155" y="108">4</text><text x="185" y="108">1</text><text x="215" y="108">5</text><text x="245" y="108">6</text>
    <text x="95" y="134">2</text><text x="125" y="134">3</text><text x="155" y="134">1</text><text x="185" y="134">4</text><text x="215" y="134">5</text><text x="245" y="134">6</text>
    <text x="95" y="160">2</text><text x="125" y="160">1</text><text x="155" y="160">3</text><text x="185" y="160">4</text><text x="215" y="160">5</text><text x="245" y="160">6</text>
    <text x="95" y="186">1</text><text x="125" y="186">2</text><text x="155" y="186">3</text><text x="185" y="186">4</text><text x="215" y="186">5</text><text x="245" y="186">6</text>
  </g>
  <g fill="currentColor">
    <text x="290" y="56">1 의 b = 5 (왼쪽에 더 큰 원소 5개) → 교환 있는 패스 5번</text>
    <text x="290" y="82">반대로 큰 값(토끼, rabbit)은 한 패스에 끝까지 간다</text>
    <text x="290" y="120">P = min(n−1, 5+1) = 5 → 비교 5+4+3+2+1 = 15 = n(n−1)/2</text>
    <text x="290" y="146">삽입 정렬이면: 1 을 한 번에 5칸 밀어 넣음 → 비교 9, shift 5</text>
    <text x="290" y="172">조기 종료가 있어도 거북이 하나가 버블을 O(n²) 로 만든다</text>
    <text x="10" y="212">고치는 법: 양방향 패스(칵테일 셰이커, 연습 1) — 하지만 그래도 삽입 정렬보다 나을 게 없다.</text>
  </g>
</svg>
```

### Part A — 실측: 횟수 표와 공식 대조 (`demo_bench.c`)

입력 4 종: random(키 0..29999 무작위), reversed(서로 다른 키 역순), nearly(정렬 상태에서 무작위 인접 교환 k = n/16 번), turtle(1,2,…,n−1,0). 각 행은 고정 시드 한 개 인스턴스이고, 마지막 열은 위의 공식 다섯 개(삽입 비교·shift, 선택 비교, 버블 교환·패스·비교)를 **모두** 대조한 결과다. 실제 출력:

```text
    n input               inv | ins.cmp  shifts writes | sel.cmp swaps    wr | bub.cmp   swaps  pass | formula
   16 random               63 |      76      63     76 |     120    11    22 |     114      63    12 | OK (z=2 maxL=11)
   16 reversed            120 |     120     120    135 |     120     8    16 |     120     120    15 | OK (z=15 maxL=15)
   16 nearly(k=n/16)        1 |      16       1      2 |     120     1     2 |      29       1     2 | OK (z=0 maxL=1)
   16 turtle               15 |      29      15     16 |     120    15    30 |     120      15    15 | OK (z=1 maxL=15)
   64 random             1022 |    1080    1022   1080 |    2016    62   124 |    1845    1022    45 | OK (z=5 maxL=44)
   64 reversed           2016 |    2016    2016   2079 |    2016    32    64 |    2016    2016    63 | OK (z=63 maxL=63)
   64 nearly(k=n/16)        4 |      67       4      8 |    2016     4     8 |     125       4     2 | OK (z=0 maxL=1)
   64 turtle               63 |     125      63     64 |    2016    63   126 |    2016      63    63 | OK (z=1 maxL=63)
  256 random            16336 |   16584   16336  16587 |   32640   251   502 |   32637   16336   253 | OK (z=7 maxL=252)
  256 reversed          32640 |   32640   32640  32895 |   32640   128   256 |   32640   32640   255 | OK (z=255 maxL=255)
  256 nearly(k=n/16)       16 |     271      16     31 |   32640    16    32 |     762      16     3 | OK (z=0 maxL=2)
  256 turtle              255 |     509     255    256 |   32640   255   510 |   32640     255   255 | OK (z=1 maxL=255)
 1024 random           265548 |  266566  265548 266562 |  523776  1016  2032 |  523451  265548   998 | OK (z=5 maxL=997)
 1024 reversed         523776 |  523776  523776 524799 |  523776   512  1024 |  523776  523776  1023 | OK (z=1023 maxL=1023)
 1024 nearly(k=n/16)       60 |    1082      60    119 |  523776    60   120 |    3066      60     3 | OK (z=1 maxL=2)
 1024 turtle             1023 |    2045    1023   1024 |  523776  1023  2046 |  523776    1023  1023 | OK (z=1 maxL=1023)
```

읽는 법 (숫자를 직접 대조해 보자):

- **삽입 = inv + n − 1 − z**: n=64 random → 1022 + 63 − 5 = 1080 ✓. 거의 정렬된 n=1024 → 60 + 1023 − 1 = 1082 ✓ — 정렬 하나에 비교 약 1 천 번, 선택은 52 만 번.
- **선택은 입력과 무관하게 n(n−1)/2** (120, 2016, 32640, 523776). 대신 교환 ≤ n − 1, 역순은 정확히 n/2.
- **버블 교환 = inv** (모든 행에서 shift 열과 같은 수). 비교는 패스 수가 결정: nearly 는 max b = 1~2 → 패스 2~3 번이라 거의 정렬된 입력에서도 버블이 나쁘지 않다. 하지만 turtle 은 max b = n−1 → 비교 n(n−1)/2. 삽입은 turtle 에서 2n − 3 비교(125, 509, 2045).

무작위 **순열**(서로 다른 키) 200 개 평균과 기댓값(같은 프로그램 2 절, 실제 출력):

| n | inv 평균 / n(n−1)/4 | z 평균 / H_n − 1 | 버블 패스 평균 / n − √(πn/2) | 선택 교환 평균 / n − H_n |
|---|---|---|---|---|
| 16 | 61.1 / 60.0 | 2.50 / 2.38 | 12.8 / 11.0 | 12.6 / 12.6 |
| 64 | 1013.7 / 1008.0 | 3.83 / 3.74 | 55.3 / 54.0 | 59.4 / 59.3 |
| 256 | 16309.7 / 16320.0 | 5.18 / 5.12 | 236.8 / 235.9 | 249.9 / 249.9 |
| 1024 | 262214.0 / 261888.0 | 6.38 / 6.51 | 984.7 / 983.9 | 1016.6 / 1016.5 |

inv 와 z 의 기댓값은 쉽게 증명된다(쌍마다 역전 확률 1/2; i 번째가 앞 i 개 중 최소일 확률 1/i). 버블 패스의 n − √(πn/2) 은 Knuth 5.2.2 의 점근식(상수항 생략)이다. 선택 교환이 n − H_n 과 소수 첫째 자리까지 맞는 것은 **측정으로만** 확인했다 (여기서 증명하지 않음). 요점: 무작위 입력에서 선택 정렬도 교환은 거의 n 번 한다 — "교환이 적다"는 건 **n² 이 아니라 n** 이라는 뜻이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">n = 64 에서 키 비교 횟수 (demo_bench 실측, 막대 360px = 2016)</text>
  <g fill="currentColor">
    <text x="10" y="66">random</text><text x="10" y="126">reversed</text><text x="10" y="186">nearly (k=4)</text><text x="10" y="246">turtle</text>
  </g>
  <rect x="130" y="44" width="193" height="12" class="f-ok"/><rect x="130" y="58" width="360" height="12" class="f-mut"/><rect x="130" y="72" width="329" height="12" class="f-warn"/>
  <rect x="130" y="104" width="360" height="12" class="f-ok"/><rect x="130" y="118" width="360" height="12" class="f-mut"/><rect x="130" y="132" width="360" height="12" class="f-warn"/>
  <rect x="130" y="164" width="12" height="12" class="f-ok"/><rect x="130" y="178" width="360" height="12" class="f-mut"/><rect x="130" y="192" width="22" height="12" class="f-warn"/>
  <rect x="130" y="224" width="22" height="12" class="f-ok"/><rect x="130" y="238" width="360" height="12" class="f-mut"/><rect x="130" y="252" width="360" height="12" class="f-warn"/>
  <g fill="currentColor" font-size="12">
    <text x="329" y="55">1080</text><text x="496" y="69">2016</text><text x="465" y="83">1845</text>
    <text x="496" y="115">2016</text><text x="496" y="129">2016</text><text x="496" y="143">2016</text>
    <text x="148" y="175">67</text><text x="496" y="189">2016</text><text x="158" y="203">125</text>
    <text x="158" y="235">125</text><text x="496" y="249">2016</text><text x="496" y="263">2016</text>
  </g>
  <rect x="580" y="50" width="14" height="12" class="f-ok"/><text x="600" y="61" fill="currentColor">삽입</text>
  <rect x="580" y="72" width="14" height="12" class="f-mut"/><text x="600" y="83" fill="currentColor">선택</text>
  <rect x="580" y="94" width="14" height="12" class="f-warn"/><text x="600" y="105" fill="currentColor">버블</text>
  <text x="10" y="290" fill="currentColor">삽입은 입력의 "무질서도"(inv)에 비례하고, 선택은 항상 같고, 버블은 거북이 하나에 무너진다.</text>
</svg>
```

### Part B — 안정성: 왜 깨지고 어떻게 고치나

**검사기.** 인접 쌍만 보면 된다: 키가 내려가면 정렬 실패, 키가 같은데 idx 가 내려가면 불안정. "정렬 + 안정" 은 (key, idx) 사전순으로 정렬됐다는 뜻이고, idx 가 서로 다르니 그 결과는 **유일**하다 — 그래서 하네스는 `qsort` 로 (key, idx) 정렬한 배열과 통째로 비교할 수 있다.

**선택 정렬의 최소 반례 `[2a, 2b, 1c]`.** i = 0 에서 최솟값 1c 가 위치 2 → `a[0]=2a` 와 교환 → `[1c, 2b, 2a]`. 멀리 교환된 2a 가 2b 를 넘었다. n ≤ 2 에서는 교환이 인접 쌍뿐이고 같은 키는 교환하지 않으므로(`m` 은 엄격한 `<` 로만 바뀜) 불가능하다. 키 {0,1,2} 로 길이 3 의 27 개 입력을 모두 돌리면 불안정한 것은 정확히 `[1,1,0] [2,2,0] [2,2,1]` 세 개다 (연습 3, 실측). 무작위 400 개 배열(키 0..4)에서는 339 개가 불안정했다.

**`<=` 버그.** 삽입 정렬의 조건을 `x <= a[j-1]` 로 쓰면 같은 키도 넘는다: `[5a, 5b]` → `[5b, 5a]`. 버블을 `a[j] >= a[j+1]` 로 쓰면 같은 키를 교환한다. 게다가 전부 같은 키 8 개에서 매 패스 교환이 생겨 **조기 종료가 영영 안 걸린다** — 비교 28 = 8·7/2 (정상 버블은 7). 정렬 결과(키 순서)는 맞으니 키만 보는 테스트로는 절대 안 잡힌다. 그래서 테스트 데이터에 **idx 를 붙인 레코드**가 필요하다.

**안정 선택 정렬 = 회전.** 최솟값 `a[m]` 을 꺼내 `a[i..m-1]` 을 한 칸 밀고 `a[i]` 에 넣는다. 구간 안 순서는 보존되고, 같은 키 중 **첫 번째**를 고르므로(엄격한 `<`) 안정하다. 비교는 그대로 n(n−1)/2 지만 쓰기가 O(n²) 로 늘어 선택 정렬의 유일한 장점(쓰기 적음)을 잃는다. 일반 해법은 **비교 키를 (key, idx) 로 확장**하는 것 — 어떤 불안정 정렬도 안정해지지만 레코드마다 idx 필드(여기선 2 B)가 든다.

### Part C — 중앙값 필터: "거의 정렬된" 입력을 일부러 만든다

임펄스 잡음(EMI 스파이크)에는 이동 평균(ALGO-B10)보다 **중앙값 필터**가 맞다: 창 N = 9 이면 연속 4 개 이하 스파이크는 출력에 아예 안 나온다 (테스트: 기저 1000 에 4095 를 4 번 연속 → 출력 최댓값 1000).

순진한 구현은 매 샘플 링을 복사해 정렬한다 — 도착 순서는 무작위에 가까우니 삽입 정렬로도 평균 ~N²/4. 대신 **정렬된 사본을 계속 들고 있으면**, 새 샘플마다 바뀌는 건 원소 하나다:

1. 가장 오래된 값 `ring[head]` 를 `sorted[]` 에서 선형 탐색으로 찾아 새 값 x 로 덮어쓴다. (중복 값은 구별할 필요 없음.)
2. `sorted[]` 에 삽입 정렬 → 틀린 원소가 한 개뿐이므로 비교 ≤ **2N − 3**.

왜 2N − 3 인가: x 가 왼쪽으로 d 칸 가면 다른 원소들은 비교 1 번씩(N−2), x 는 d+1 번(맨 앞까지 가면 d 번) → 최대 (N−2) + (N−1). x 가 오른쪽으로 가야 하면 x 를 넘는 원소 d 개가 각각 비교 2 번(넘기 1 + 멈춤 1) → 마찬가지로 최대 2N − 3. 6000 샘플 테스트에서 최댓값이 정확히 이 경계(N=9 → 15, N=32 → 61)에 닿았다. 결정적인 O(N) 최악 — 1 kHz 태스크의 WCET 예산을 계산할 수 있다.

실측 (`demo_bench.c` 3 절, 10000 샘플, 샘플당 키 비교 수):

| N | 증분(덮어쓰기 + 삽입) 평균 / 최대 | 매번 사본 삽입 정렬 평균 / 최대 | 매번 선택 정렬 |
|---|---|---|---|
| 9 | 10.4 / 15 | 23.9 / 35 | 36 |
| 32 | 40.9 / 61 | 274.0 / 372 | 496 |

더 줄이려면 바뀐 원소 하나만 좌/우로 옮기면 된다(비교 ≤ N, 연습 5). 힙 두 개로 O(log N) 도 되지만(ALGO-M11, LeetCode 480 Sliding Window Median) N ≤ 32 에서는 상수와 코드 크기 때문에 정렬 배열이 보통 이긴다 — 주장하기 전에 타깃에서 재 보라.

**EEPROM 쓰기 비교.** 외부 I2C EEPROM 이나 에뮬레이션 EEPROM 에 있는 32 개 레코드 표를 제자리 정렬한다고 하자 (쓰기가 느리고 셀 수명이 있다). 실측 (`demo_bench.c` 4 절, 배열 쓰기 횟수):

| 입력 (n=32) | 삽입 | 선택 | 버블 |
|---|---|---|---|
| random | 271 | 60 | 484 |
| reversed | 527 | 32 | 992 |
| nearly (k=2) | 4 | 4 | 4 |
| turtle | 32 | 62 | 62 |

선택은 언제나 ≤ 2(n−1) = 62. 하지만 **거의 정렬된 표나 거북이 하나**면 삽입이 같거나 더 적다 — "선택 = 쓰기 최소"는 무작위·역순일 때의 이야기다. 진짜 최소는 사이클 정렬(연습 4: 쓰기 = 제자리 아닌 원소 수). 그리고 현실 답은 보통 "**RAM 으로 읽어 정렬하고 한 번에 다시 쓴다**"(쓰기 n 번, 바뀐 칸만 쓰면 더 적음). NAND 처럼 덮어쓰기가 안 되는 매체라면 애초에 제자리 정렬이 아니라 새 위치에 쓰고 매핑만 바꾼다 — FTL 의 방식이다 (DS-A09).

### 시간 측정 (`demo_bench.c` 5 절)

이 맥(Apple Silicon, `sysctl` 표기 Apple M6, `-O2`), 무작위 키, 정렬 1 회당 ns, 9 회 측정의 중앙값, 입력 memcpy 포함. 세 번 돌렸고 다른 작업이 같이 돌아 절대값은 흔들렸다(예: 삽입 n=1024 가 84.8 ~ 170 µs). **순위와 비율만** 보라. 아래는 그중 한 번의 실제 출력:

```text
    n  insertion  selection    sel(mk)     bubble      qsort  ins(nearly)
    8       19.7       41.1       21.0       14.2       39.3          7.3
   16       45.4      226.6      162.6       91.3       80.6         11.2
   32      128.4      886.7      443.4      275.4      221.7         17.6
   64      398.4     3390.6     1418.0     1214.8      566.4         43.7
  256     5828.1    57500.0    24125.0    22812.5     3031.2        177.7
 1024    84750.0  1002000.0   393000.0   866000.0    17875.0        884.8
```

- n ≤ 64 에서는 삽입이 libc `qsort`(함수 포인터 비교) 보다 빠르고, 256 부터 역전된다. 표준 라이브러리가 작은 구간을 삽입 정렬로 넘기는 이유를 이 맥에서 직접 본 셈이다 (Q1).
- 같은 비교 수(n(n−1)/2)인데 선택이 버블보다 느린 n 이 있다. 디스어셈블(`clang -O2 -S`, arm64)을 보면 안쪽 루프가 `csel w13 …`(m 갱신) → `ldrsh … [x0, x17]`(a[m].key 다시 읽기) → `cmp` → `csel` 로 **루프마다 메모리 로드를 낀 의존 사슬**이다. 최솟값 키를 레지스터에 두는 `sel(mk)` 판은 n=1024 에서 2.5 배 빨라진다.
- 버블은 교환이 inv(≈ n²/4)번이고 분기 예측이 안 되는 분기가 n²/2 번 → n=1024 에서 삽입의 약 10 배.
- 거의 정렬된 입력의 삽입 정렬은 n=1024 에서 1 µs 미만 — 무작위 대비 약 100 배.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 33/33 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_san && ./solution_san   # 33/33, 보고 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench   # 위 표들 (약 0.6 초)
```

solution.c 실행 중 찍히는 요약 줄(실제 출력): `random 600 arrays: sort mismatches ins=0 sel=0 bub=0, formula mismatches ins=0 sel=0 bub=0` · `median N=32: 6000 samples, mismatches=0, max cmp/push=61 (bound 2N-3=61)` · `EEPROM n=32 random: writes ins=307 sel=54 bub=558`.

## 7. 자주 하는 실수

**1. 비교에 `<=` / `>=`** — 정렬 결과(키)는 맞고 안정성만 깨진다. 키만 보는 테스트는 통과한다. 버블은 덤으로 같은 키에서 조기 종료가 안 걸린다.

```c
while (j > 0 && x.key <= a[j - 1].key)   /* ✗ 같은 키도 넘는다 */
while (j > 0 && x.key <  a[j - 1].key)   /* ✓ */
```

**2. `&&` 순서** — `while (a[j-1].key > x.key && j > 0)` 은 j = 0 에서 `a[-1]` 을 먼저 읽는다 = 배열 밖 접근, **UB**. ASan 이 `heap/stack-buffer-overflow` 로 잡지만 MCU 에서는 조용히 옆 변수를 읽는다. 경계 검사를 왼쪽에.

**3. 버블 안쪽 범위 off-by-one** — `for (j = 0; j < n - pass; j++)` 에서 `a[j+1]` 은 마지막에 `a[n - pass]` → 첫 패스에 `a[n]` 을 읽는다. 경계를 `j < n - 1 - pass` 로 쓰면서 `n` 이 `size_t` 면 n = 0 에서 `n - 1` 이 SIZE_MAX 로 감싸져 루프가 폭주한다. `j + 1 < n - pass` 처럼 뺄셈이 음수가 될 수 없는 형태로 쓴다.

**4. 선택 정렬에서 안쪽 루프 안 교환** — 더 작은 걸 볼 때마다 `a[i]` 와 바꾸면 결과는 맞지만 교환이 최악 O(n²) 이다 (이건 사실상 "교환 정렬"). 선택 정렬의 존재 이유(교환 ≤ n−1)가 사라진다. `m` 만 갱신하고 끝에 한 번.

**5. `swapped` 를 루프 밖에서 한 번만 초기화** — 첫 패스에 1 이 되면 영영 1 → 조기 종료가 사라져 정렬된 입력도 n(n−1)/2 비교. 패스마다 `int swapped = 0;`.

**6. 횟수 공식을 `int` 로** — `int n = 65536; n * (n - 1) / 2` 는 곱셈에서 `int` 오버플로 = **UB** (C11 6.5p5). 16 비트 `int` MCU(AVR, MSP430)에서는 n = 182 만 돼도 넘는다. `uint32_t` 로 계산하고 곱하기 전에 캐스트.

**7. 중앙값 창에서 "값"으로 지우기를 레코드에 그대로 쓰기** — 정수 키만 있을 땐 같은 값 아무거나 지워도 되지만, (값, 타임스탬프) 레코드라면 정확히 그 샘플을 지워야 한다. 그때는 링에 `sorted` 위치를 같이 저장하거나 (값, 순번) 으로 비교한다.

## 8. 면접 Q&A

<details><summary>Q1. 표준 라이브러리 정렬은 왜 작은 구간에서 삽입 정렬로 바꾸나요? 몇 개부터인가요?</summary>

퀵소트/머지소트는 재귀 호출·피벗 선택·분할 루프 같은 **고정 비용**이 있다. 원소가 십수 개면 그 비용이 n log n 의 이득보다 크다. 삽입 정렬은 상수가 작고(비교 하나, 쓰기 하나), 메모리를 앞에서 뒤로 순차 접근하고, 재귀가 없고, 거의 정렬된 구간에 적응한다. 실측도 같다: 이 맥에서 n ≤ 64 는 삽입이 `qsort` 보다 빠르고 256 부터 역전(5 절 표). 예: introsort(Musser 1997) = 퀵소트 + 재귀 깊이 한도(≈ 2·log₂n)를 넘으면 힙소트 + 작은 구간은 삽입 정렬. libstdc++ `std::sort` 는 `_S_threshold = 16` 보다 작은 구간을 남겨 두었다가 마지막에 전체를 삽입 정렬 한 번으로 마무리한다(구간끼리는 이미 순서가 맞아 inv 가 작다 — 이 레슨의 "거의 정렬됨"). Bentley–McIlroy `qsort`(BSD 계열)는 n < 7 에서 삽입 정렬, CPython timsort 는 32~64 길이(minrun)까지 **이진 삽입 정렬**로 런을 만든다. 꼬리: "임계값은 어떻게 정하나?" → 타깃에서 측정. 캐시·분기 예측·비교 비용(함수 포인터냐 인라인이냐)에 따라 달라진다.

</details>

<details><summary>Q2. 버블 정렬은 언제 답이 되나요?</summary>

거의 없다. 교환 수는 삽입의 shift 수와 같은 inv 지만 교환은 쓰기 2 번(삽입 shift 는 1 번)이고, 거북이 하나에 O(n²) 이 되며(Part A 그림), 분기 예측이 안 되는 분기가 많아 이 맥에서 n=1024 무작위 입력에 삽입의 약 10 배 느렸다. Knuth 는 TAOCP 3 권 5.2.2 에서 버블 정렬에 "기억하기 쉬운 이름과 흥미로운 이론 문제 말고는 추천할 점이 없다"는 취지로 썼다(기억에 의존한 인용). 굳이 꼽으면: (1) 한 패스 = "정렬됐나 확인" 이라 거의 정렬된 데이터의 확인+수리, (2) 인접 비교만 하는 **홀짝 전치 정렬(odd-even transposition sort)** 은 SIMD 레인이나 FPGA 처럼 인접끼리만 연결된 병렬 하드웨어에서 n 단계에 끝나 실제로 쓰인다. 면접에서 "버블 짜 보세요"는 루프·경계·조기 종료를 보려는 것이고, "어떤 걸 쓰겠냐"에 버블이라고 답하면 감점이다.

</details>

<details><summary>Q3. 선택 정렬은 안정한가요? 안정하게 만들 수 있나요?</summary>

아니다. 최소 반례 `[2a, 2b, 1c]` → `[1c, 2b, 2a]`, n = 3 이 최소(n ≤ 2 는 불가능, 연습 3 전수 탐색). 원인은 a[i] 가 a[m] 자리로 **멀리** 교환되며 같은 키를 넘는 것. 고치는 법 두 가지: (1) 교환 대신 회전 — 안정하지만 쓰기 O(n²), (2) 비교 키를 (key, 원래 idx) 로 확장 — 어떤 정렬이든 안정해지지만 레코드마다 idx 저장 공간. 꼬리: "삽입 정렬은 왜 안정?" → 엄격한 `<` 로 같은 키를 넘지 않고 인접 이동만 하므로. `<=` 로 바꾸면 깨진다. 꼬리 2: "안정성이 실제로 필요한 예는?" → 다중 키 정렬: 2차 키로 먼저 정렬하고 1차 키로 **안정** 정렬하면 (1차, 2차) 순서가 된다 — LSD 기수 정렬의 원리(ALGO-B08).

</details>

<details><summary>Q4. 삽입 정렬이 "거의 정렬된 입력에 O(n)" 이라는 건 정확히 무슨 뜻인가요?</summary>

정확한 식은 비교 = inv + n − 1 − z, 이동 = inv 다. 그래서 시간은 Θ(n + inv). "거의 정렬됨"을 (a) 역전 수 ≤ k 로 정의하면 O(n + k), (b) 모든 원소가 최종 위치에서 d 칸 이내로 정의하면 역전 쌍의 두 원소는 2d 칸 미만 떨어져 있으므로 inv < 2nd → O(nd). 실측: n = 1024, 인접 교환 64 번 → inv = 60, 비교 1082 (무작위는 266566). 반례: 원소 하나가 반대쪽 끝에 있으면(turtle 의 거울판, 큰 값이 맨 앞) inv = n−1 이라 여전히 O(n) 이지만, k 개의 원소가 멀리 흩어져 있으면 O(nk) 다. 꼬리: "링크드 리스트로 하면?" → 이동이 포인터 바꾸기라 O(1) 이지만 자리 찾기가 앞에서부터 선형 탐색(LeetCode 147 Insertion Sort List). 이진 탐색으로 비교만 줄이는 건 배열에서만 되고, 그래도 이동은 inv 그대로(연습 2).

</details>

<details><summary>Q5. ISR 이나 실시간 태스크에서 정렬해도 되나요? 어떤 걸 고르나요?</summary>

ISR 에서는 하지 않는다 — 샘플만 링 버퍼(DS-B03)에 넣고 태스크에서 처리. 태스크에서 할 때 기준은 **최악 실행 시간(WCET)의 상한**과 스택이다. 셋 다 스택 O(1)·재귀 없음·malloc 없음. 선택은 비교 수가 입력과 무관하게 n(n−1)/2 라 실행 시간이 가장 예측 가능하다(지터 최소). 삽입은 평균이 훨씬 좋고 최악은 같은 n(n−1)/2 비교 — N = 32 면 496 비교 + 496 shift — 대략 수천 사이클, 168 MHz Cortex-M4 에서 수십 µs 이하로 추정한다(추정일 뿐, 타깃에서 DWT CYCCNT 로 잴 것). 중앙값 창처럼 구조적으로 "한 원소만 틀린" 상태를 유지하면 최악이 2N − 3 비교로 묶인다(Part C). 꼬리: "공유 배열을 ISR 이 쓰는 중에 태스크가 정렬하면?" → 정렬 전에 임계 구역에서 스냅샷 복사(C-M14), 또는 ISR 은 링에만 쓰게 설계.

</details>

<details><summary>Q6. 플래시나 EEPROM 에 있는 표를 정렬해야 한다면?</summary>

먼저 "왜 제자리에서?" 를 묻는다. RAM 이 표를 담을 수 있으면 읽어서 정렬하고 바뀐 칸만 다시 쓰는 게 정답(쓰기 ≤ n). RAM 이 모자라 매체 위에서 정렬해야 하면 쓰기 수가 비용이다: 선택 정렬은 교환 ≤ n − 1 → 쓰기 ≤ 2(n−1), 사이클 정렬은 쓰기 = 제자리 아닌 원소 수(최소, 연습 4). 다만 거의 정렬된 표에서는 삽입도 쓰기가 적다(실측 표: nearly 4 = 4 = 4). 또 하나: 레코드가 크면 **인덱스 배열을 정렬**하고 레코드는 안 옮긴다(쓰기 단위가 1 B 인덱스). NAND 는 덮어쓰기가 안 되고(erase-before-write, 블록 단위 지우기) 그래서 FTL 은 제자리 갱신 대신 새 페이지에 쓰고 매핑을 바꾼다(DS-A09) — "쓰기 최소 정렬"을 물으면 이 트레이드오프까지 말하면 좋다.

</details>

<details><summary>Q7. 8/16 비트 MCU 로 옮기면 무엇이 달라지나요?</summary>

(1) 레코드 이동 비용: `Rec` 는 4 B 라 Cortex-M 에서는 `ldr/str` 한 번이지만 8 비트 AVR 에서는 바이트 4 번 — 레코드가 크면 인덱스(`uint8_t`, N ≤ 255) 배열을 정렬한다. (2) 카운터와 공식: 16 비트 `int` 에서는 곱 n·(n−1) 이 n = 182 에서 32767 을 넘는다(나누기 전에 이미 오버플로) → `uint32_t`. (3) 캐시가 없는 코어(Cortex-M0/M3, 대부분의 M4)에서는 메모리 접근 지연이 균일해 이 맥에서 본 "로드 의존 사슬" 효과는 작지만, 플래시 대기 상태(wait state)가 있는 상수 표를 정렬 대상으로 쓰면 RAM 으로 복사부터. (4) 분기 비용: M4 는 짧은 파이프라인(3 단)이라 분기 예측 실패 비용이 작다 — 버블이 이 맥만큼 처참하진 않다. 그래도 비교·쓰기 수가 많은 건 같다.

</details>

## 9. 추가 연습

### 연습 1 — 칵테일 셰이커 정렬: 거북이 잡기

버블을 왼→오, 오→왼 번갈아 돌리고, 마지막 교환 위치를 다음 경계로 쓴다. 안정성을 유지하고, 교환 = inv 인지, turtle n = 64 에서 비교가 3n 이하인지 확인하라.

<details><summary>답</summary>

`cocktail_sort()` (practice.c). 역방향 패스에서 거북이가 한 번에 맨 앞으로 간다. 실측: `ex1 turtle n=64: bubble cmp=2016, cocktail cmp=186`. 무작위 300 개(키 범위 6 / 5000)에서 기준과 일치하고 교환 = inv. 하지만 무작위 입력에선 여전히 ~n²/2 비교라 삽입 정렬을 이기지 못한다 — 거북이만 고친 버블이다.

</details>

### 연습 2 — 이진 삽입 정렬: 비교 vs 이동

삽입 자리를 이진 탐색으로 찾는다. 안정하려면 lower_bound 가 아니라 **upper_bound**(x 보다 큰 첫 자리, ALGO-B02)여야 한다. 비교가 Σ⌈log₂(i+1)⌉ 이하인지, shift 는 여전히 inv 인지 확인하라.

<details><summary>답</summary>

`binary_insertion_sort()` — `if (x < a[mid]) hi = mid; else lo = mid + 1;` 로 같은 키 **뒤**에 넣는다. lower_bound(`a[mid] < x` 면 오른쪽)로 쓰면 같은 키 **앞**에 들어가 불안정. 실측: `ex2 random perm n=256: insertion cmp=16273 shifts=16021 | binary insertion cmp=1696 shifts=16021` — 비교 1/10, 이동 그대로. 비교가 비쌀 때(문자열, 함수 포인터 비교) 이득이고, timsort 가 짧은 런에 이걸 쓴다.

</details>

### 연습 3 — 최소 반례 전수 탐색

키 {0,1,2}, 길이 1~6 의 모든 입력(3ⁿ 개)에 세 정렬을 돌려라. 선택 정렬이 처음 불안정해지는 n, 길이 3 에서 불안정한 입력 수, 삽입·버블이 한 번이라도 불안정한지.

<details><summary>답</summary>

실측: `selection first unstable at n=3, unstable inputs at n=3: 3/27, insertion/bubble unstable: 0`. 세 입력은 `[1,1,0] [2,2,0] [2,2,1]` — "같은 키 두 개 뒤에 더 작은 키"가 유일한 패턴이다(i = 0 의 교환만 비인접이므로). 손 증명과 전수 탐색이 일치. 이렇게 **작은 정의역 전수 탐색**은 면접에서 "반례를 어떻게 찾았나"에 대한 좋은 답이다.

</details>

### 연습 4 — 사이클 정렬: 쓰기 최소

서로 다른 키의 순열에서, 각 원소를 최종 위치(자기보다 작은 원소 수)에 **한 번만** 쓰는 사이클 정렬을 짜라. 쓰기 = 제자리가 아닌 원소 수이고, 언제나 선택 정렬 쓰기 이하인지 확인하라.

<details><summary>답</summary>

`cycle_sort()` — 사이클을 따라 "내 자리에 있던 걸 들고 다음 자리로". 비교는 Θ(n²)(자리 찾을 때마다 세기). 선택 정렬의 교환 1 번은 원소를 최대 2 개 제자리에 놓으므로 2·교환 ≥ 제자리 아닌 수 → 사이클 정렬 쓰기 ≤ 선택 쓰기. 실측 300 개 순열: `total writes cycle=9560 selection=17404`. 중복 키가 있으면 같은 키를 건너뛰는 처리가 더 필요하다(여기선 다루지 않음).

</details>

### 연습 5 — 중앙값 창의 국소 수정

`medwin_push` 의 전체 삽입 정렬 대신, 덮어쓴 원소 하나만 왼쪽(더 작으면) 또는 오른쪽(더 크면)으로 옮겨라. 비교 ≤ N 인지, 기준(창 통째 정렬)과 일치하는지.

<details><summary>답</summary>

`medwin_push_local()` — 왼쪽으로 가는 루프와 오른쪽으로 가는 루프를 차례로(둘 중 하나만 실제로 움직인다). 실측 최대 비교: N = 9 → 9, N = 32 → 32 (왼쪽 루프의 멈춤 비교 1 + 오른쪽 최대 N − 1). 무작위 극값이 1/7 확률로 섞인 4000 샘플에서 N = 1, 2, 9, 32 모두 불일치 0.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_san && ./practice_san   # 12/12, 보고 없음
```

## 10. 핵심 정리

- 삽입: `j > 0 &&` 먼저, 엄격한 `<` → 안정. shift = inv, 비교 = inv + n − 1 − z → **Θ(n + inv)**: 거의 정렬된 입력·작은 n 의 왕.
- 선택: 비교 **항상 n(n−1)/2**, 교환 ≤ n − 1 (무작위 평균 ≈ n − H_n). 멀리 교환 → **불안정**, 최소 반례 `[2a, 2b, 1c]` (n = 3).
- 버블: 교환 = inv (쓰기 2·inv), 패스 = min(n−1, max b + 1), b = 왼쪽의 더 큰 원소 수. 거북이 하나에 O(n²). 실무 답으로는 거의 안 쓴다.
- `<=` 한 글자로 삽입·버블이 불안정해지고 버블은 조기 종료까지 잃는다. 테스트는 (key, idx) 레코드로, 기준은 (key, idx) 사전순 정렬.
- 안정하게 만들기: 비교 키에 원래 idx 추가(어떤 정렬이든), 또는 교환 → 회전(쓰기 O(n²)).
- 펌웨어: 중앙값 창은 정렬 사본을 유지해 "한 원소만 틀린" 상태 → 삽입 정렬 ≤ 2N − 3 비교, 결정적 O(N).
- 쓰기 비용이 비싸면 선택(≤ 2(n−1))·사이클 정렬(최소). 하지만 보통은 RAM 에서 정렬 후 한 번에 쓰기, NAND 면 FTL 식 out-of-place.
- 표준 정렬은 작은 구간을 삽입 정렬로(libstdc++ 16, BSD qsort 7, timsort minrun 32~64). 이 맥 실측도 n ≤ 64 에서 삽입 > qsort.

## 참고자료

- D. E. Knuth, *The Art of Computer Programming*, Vol. 3 *Sorting and Searching* (2nd ed.), 5.2.1 삽입 정렬 · 5.2.2 교환 정렬(버블의 역전 표 분석, 평균 패스 수) · 5.2.3 선택 정렬. 5.1.1 역전과 역전 표.
- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms*, 2 장 삽입 정렬(루프 불변식 증명), 문제 2-2(버블 정렬 정확성), 문제 2-4(역전).
- D. R. Musser, "Introspective Sorting and Selection Algorithms", *Software: Practice and Experience* 27(8), 1997 — introsort.
- J. L. Bentley, M. D. McIlroy, "Engineering a Sort Function", *Software: Practice and Experience* 23(11), 1993 — 작은 배열(n < 7) 삽입 정렬 컷오프.
- libstdc++ `bits/stl_algo.h` — `_S_threshold`, `__introsort_loop`, `__final_insertion_sort`: <https://gcc.gnu.org/onlinedocs/libstdc++/>
- Tim Peters, CPython `Objects/listsort.txt` — timsort, minrun, 이진 삽입 정렬: <https://github.com/python/cpython/blob/main/Objects/listsort.txt>
- AlgoMonster 노트 M01 §14 Intro to Sorting · §16 Sorting Summary (Don 정리본) — `algomaster-coding-pattern/original-md/2026-09-27_M01_getting_started.md`
- LeetCode 147 Insertion Sort List · LeetCode 480 Sliding Window Median · LeetCode 912 Sort an Array.
- 관련 레슨: ALGO-B01 (실측으로 Big-O), ALGO-B02 (upper_bound — 연습 2), ALGO-B06 (퀵소트, 작은 구간 컷오프), ALGO-B07 (머지소트, 안정 O(n log n)), ALGO-B08 (계수·기수 정렬, 안정성의 쓰임), ALGO-B10 (이동 평균 vs 중앙값), ALGO-M11 (스트리밍 중앙값, 힙), ALGO-A12 (외부 정렬), DS-B03 (링 버퍼), DS-A09 (FTL 매핑), C-M14 (ISR 공유 데이터)
