---
id: DS-M08
time: 90분
updated: 2026-11-07
---
# DS-M08 해시 테이블 리사이즈와 Robin Hood 해싱

## 학습 목표

- Robin Hood 삽입("부자에게서 뺏기")과 **조기 종료 조회**를 구현하고, 조기 종료가 맞는 이유를 불변식 `psl[j] ≤ psl[j−1] + 1` 로 설명한다.
- tombstone 없이 지우는 **backward-shift 삭제**를 랩어라운드까지 정확히 쓴다.
- malloc 없이 **호출자가 준 두 번째 버퍼로 rehash** 하는 리사이즈 API를 설계하고, 성장 7/8 · 축소 1/4 의 **히스테리시스(hysteresis)** 를 숫자로 정당화한다.
- 부하율 0.5~0.95 에서 선형 탐사와 Robin Hood 의 평균/최대 탐사 길이를 **직접 측정**하고, "평균은 정확히 같고 최대·분산만 줄어든다"는 결과를 증명한다.
- 리얼타임 펌웨어용 변형인 **점진적 리해싱(incremental rehash)** 과 **PSL 상한 강제**로 연산당 최악 시간을 묶는다.

## 1. 문제

DS-B11 의 선형 탐사 표는 평균은 빠르지만 **최악이 나쁘다** — 펌웨어와 면접이 묻는 건 늘 최악(WCET)이다. 이 레슨은 같은 배열 하나짜리 표를 그대로 두고 ① 탐사 길이의 **분산**을 줄이는 Robin Hood 해싱, ② 부하율이 넘칠 때 **malloc 없이** 키우는 리사이즈를 붙인다. "해시 테이블이 꽉 차면 어떻게 하나요?" "최악 조회 시간은?" 이 두 꼬리질문에 숫자로 답하는 게 목표다.

핵심 아이디어. 각 항목에 **PSL(probe sequence length)** = "홈 슬롯에서 몇 번째 칸에 있나"(홈이면 1)를 저장한다. 삽입 중인 키가 자기보다 PSL 이 **작은**(=홈에 더 가까운, "부자") 항목을 만나면 그 자리를 뺏고, 쫓겨난 항목을 들고 계속 간다. 부자는 조금 손해 보고 가난한 항목이 덜 멀리 간다 — 그래서 Robin Hood.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">cap=16, home = key &amp; 15.  1,17,33 → 홈 1 / 2 → 홈 2.  칸 안: 키 / psl</text>
  <g fill="currentColor"><text x="10" y="72">① 1,17,2 삽입</text><text x="10" y="152">② 선형 탐사 +33</text><text x="10" y="232">③ Robin Hood +33</text></g>
  <g stroke="currentColor" fill="none">
    <rect x="150" y="50" width="90" height="34"/><rect x="240" y="50" width="90" height="34"/><rect x="330" y="50" width="90" height="34"/><rect x="420" y="50" width="90" height="34"/>
    <rect x="150" y="130" width="90" height="34"/><rect x="240" y="130" width="90" height="34"/><rect x="330" y="130" width="90" height="34"/><rect x="420" y="130" width="90" height="34"/>
    <rect x="150" y="210" width="90" height="34"/><rect x="240" y="210" width="90" height="34"/><rect x="330" y="210" width="90" height="34"/><rect x="420" y="210" width="90" height="34"/>
  </g>
  <rect x="420" y="130" width="90" height="34" class="f-warn"/>
  <rect x="330" y="210" width="90" height="34" class="f-ok"/>
  <rect x="420" y="210" width="90" height="34" class="f-acc"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="195" y="44" font-size="12">슬롯1</text><text x="285" y="44" font-size="12">2</text><text x="375" y="44" font-size="12">3</text><text x="465" y="44" font-size="12">4</text>
    <text x="195" y="72">1 / 1</text><text x="285" y="72">17 / 2</text><text x="375" y="72">2 / 2</text><text x="465" y="72">.</text>
    <text x="195" y="152">1 / 1</text><text x="285" y="152">17 / 2</text><text x="375" y="152">2 / 2</text><text x="465" y="152">33 / 4</text>
    <text x="195" y="232">1 / 1</text><text x="285" y="232">17 / 2</text><text x="375" y="232">33 / 3</text><text x="465" y="232">2 / 3</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M375 250 Q420 278 462 250"/>
  </g>
  <g fill="currentColor">
    <text x="530" y="146">psl 합 1+2+2+4 = 9</text><text x="530" y="164">최대 4</text>
    <text x="530" y="218">슬롯3: 2(psl 2) &lt; 33(psl 3)</text><text x="530" y="236">→ 33 이 뺏고 2 는 슬롯4로</text>
    <text x="530" y="254">psl 합 1+2+3+3 = 9, 최대 3</text>
    <text x="10" y="295">같은 칸들이 차고 psl 합도 같다 (평균 동일). 바뀌는 건 분포 — 최대가 4 → 3 으로 줄었다.</text>
    <text x="10" y="318">find(49) 홈 1: psl 1,2,3 통과, 슬롯4 psl 3 &lt; 4 → 없음 (4칸). 선형은 5칸.</text>
  </g>
</svg>
```

### Part A — Robin Hood 삽입과 조기 종료 조회

슬롯은 `{key, val, psl}`, `psl == 0` 이 EMPTY. 홈은 DS-B11 과 같은 `key & mask` (손으로 추적하기 쉽게; 실전은 Fibonacci 해싱, DS-B11 연습 3).

- `rh_locate(t, key)` — 홈부터 `d = 1, 2, ...` 로 걸으며 `slot.psl < d` 이면 즉시 -1. EMPTY 도 psl 0 이라 **비교 하나로** 함께 처리된다.
- `rh_put(t, key, val)` — 같은 키면 갱신(`HT_UPDATED`). 새 키면 자리 뺏기 삽입(`HT_OK`). `used >= limit` 이면 **아무것도 바꾸지 않고** `HT_FULL` (= 리사이즈 신호).
- 위 그림의 1, 17, 2, 33 시나리오와 랩어라운드(15, 31, 47, 0) 배치를 하네스가 확인한다.

### Part B — backward-shift 삭제와 리사이즈

- `rh_del(t, key)` — 지운 칸 뒤 항목들을 `psl > 1` 인 동안 한 칸씩 당긴다(psl−1). 묘비(tombstone)가 영원히 0개.
- `rh_rehash(t, newbuf, newcap)` — 표 전체를 **호출자가 준 버퍼**로 다시 넣고, 이제 안 쓰는 옛 버퍼를 돌려준다. 표 자신은 메모리를 할당하지 않는다.
- 하네스의 `map_put`/`map_del` 이 버퍼 두 개를 핑퐁하며 7/8 에서 2배, 1/4 미만에서 절반으로 바꾼다. 16 ↔ 4096 을 오가는 30000번 랜덤 연산을 참조 모델과 대조하고, **매 연산마다** 불변식 검사 `rh_check()`.

### Part C — 부하율 0.5~0.95 측정: 선형 탐사 vs Robin Hood

cap 16384 표 두 개에 **같은 키 열**을 넣고(`lp_put` 은 주어진 DS-B11 방식) `psl_stats()` 로 평균·최대·분산 psl 을, 없는 키 20000개로 실패 조회 탐사 수를 잰다. 시드 4개 평균. 그 숫자로 "ISR 안의 조회 예산이 N칸일 때 표를 얼마나 크게 잡나"를 정한다.

## 2. Problem (English)

> **Resizable Robin Hood hash map without dynamic allocation.**
>
> Extend a linear-probing hash map (`uint32_t` key → `uint32_t` value, capacity a power of two, home slot `key & (cap − 1)`) that lives in a caller-provided buffer.
>
> 1. Store a probe sequence length per slot (`psl`, 0 = empty, 1 = at home). Implement `int32_t rh_locate(ht_t *t, uint32_t key)` that stops as soon as it sees a slot whose `psl` is smaller than the current probe distance, and `int rh_put(ht_t *t, uint32_t key, uint32_t val)` using Robin Hood displacement. `rh_put` returns `HT_UPDATED`, `HT_OK`, or `HT_FULL` when `used` has reached `limit` (7/8 of `cap`); on `HT_FULL` the table must be unchanged.
> 2. Implement `bool rh_del(ht_t *t, uint32_t key)` with backward-shift deletion (no tombstones), and `slot_t *rh_rehash(ht_t *t, slot_t *newbuf, uint32_t newcap)` that moves every entry into `newbuf` and returns the old buffer, or returns `NULL` and leaves the table untouched if `newcap` is not a power of two or too small.
> 3. Insert the same random keys into a plain linear-probing table and a Robin Hood table at load factors 0.5–0.95 and report the mean, maximum and variance of the probe length and the mean cost of unsuccessful lookups.
>
> Example (cap = 16): insert 1, 17, 2, 33 → slots 1..4 hold `1/1, 17/2, 33/3, 2/3` (key/psl). `find(49)` fails after 4 probes. Deleting 17 gives `1/1, 33/2, 2/2, empty`.
>
> Follow-ups: why is the mean probe length identical for both tables? How would you bound the worst-case time of a single operation, including resizing, in a hard real-time system?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 이 쭉 나오고 "9/44 passed", exit 1 — TODO 5곳을 채워 44/44 를 만든다
```

TODO는 `rh_locate`, `rh_put` (Part A), `rh_del`, `rh_rehash` (Part B), `psl_stats` (Part C) 다섯 곳. 불변식 검사기 `rh_check`, 덤프 `ht_dump`, 선형 탐사 대조군 `lp_put`/`lp_find`, 핑퐁 맵 `map_put`/`map_del`, 측정 루프는 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — 조회는 비교 하나로 끝난다</summary>

`for (d = 1; d <= cap; d++, i = (i + 1) & mask)` 안에서 `if (slot[i].psl < d) return -1;` 다음에 `if (slot[i].key == key) return i;`. EMPTY 는 psl 이 0 이라 어떤 `d ≥ 1` 보다도 작다 — EMPTY 검사를 따로 할 필요가 없다. 키가 같으면 psl 은 자동으로 `d` 다.

</details>

<details>
<summary>힌트 2 — 삽입은 두 단계</summary>

들고 다니는 항목 `cur = {key, val, psl = 1}`. **1단계**: `slot.psl >= cur.psl` 인 동안 같은 키인지 보며 전진(`cur.psl++`). 멈춘 순간 새 키가 확정 — 여기서 `used >= limit` 이면 아무것도 안 바꾼 채 `HT_FULL`. **2단계**: EMPTY 면 `cur` 를 내려놓고 끝. `slot.psl < cur.psl` 이면 `swap(*slot, cur)` — 이제 쫓겨난 항목을 들고 다음 칸으로.

</details>

<details>
<summary>힌트 3 — 왜 2단계에서는 키 비교가 필요 없나</summary>

1단계가 멈춘 칸은 조회가 "없음"으로 멈출 바로 그 칸이다. 조회가 그 너머에 내 키가 없다고 확신할 수 있다면 삽입도 그렇다. 2단계에서 들고 다니는 항목은 이미 표에 있던 항목이라 다른 칸의 키와 같을 수 없다.

</details>

<details>
<summary>힌트 4 — backward-shift 의 멈춤 조건</summary>

다음 칸이 EMPTY(psl 0) 이거나 **자기 홈에 있는 항목(psl 1)** 이면 멈춘다. psl 1 항목을 당기면 홈보다 앞에 놓여 영영 못 찾는다. 당긴 항목은 홈에 한 칸 가까워졌으니 `psl--`.

</details>

<details>
<summary>힌트 5 — rehash 는 "새 표 init + 옛 항목 전부 put"</summary>

옛 버퍼 포인터와 옛 cap 을 지역 변수에 먼저 저장하고 `ht_init(t, newbuf, newcap)`. 그다음 옛 버퍼를 0..oldcap−1 로 훑어 psl != 0 인 항목을 `rh_put`. 실패 조건(2의 거듭제곱 아님, `used > limit_for(newcap)`, 같은 버퍼)은 **init 전에** 검사해야 표가 망가지지 않는다.

</details>

## 5. 풀이

### Part A — 불변식 하나가 조기 종료를 정당화한다

> **Robin Hood 불변식**: 모든 칸 `j` 에 대해 `psl[j] ≤ psl[j−1] + 1` (EMPTY 는 psl 0).

말로 하면 "앞 칸보다 psl 이 2 이상 크게 튈 수 없다". 특히 EMPTY 바로 뒤 항목은 psl ≤ 1, 즉 홈에 있어야 한다 — DS-B11 의 "홈 → 자리 사이에 EMPTY 없음" 이 이 불변식에 포함된다.

**조기 종료가 맞는 이유**. 키 K 가 홈 `h` 에서 `D` 번째 칸 `p = h + D − 1` 에 있다고 하자 (psl = D). 불변식을 p 에서 거꾸로 한 칸씩 적용하면 `psl[p − k] ≥ D − k`. 조회가 `d` 번째 칸(= `p − (D − d)`)을 볼 때 그 칸의 psl ≥ `D − (D − d) = d`. 즉 K 에 도달하기 전에는 **`psl < d` 인 칸을 절대 만나지 않는다**. 거꾸로 `psl < d` 칸을 만났다면 K 는 그 뒤에 없다. ∎

`rh_check()` 는 이 불변식 + psl 값이 실제 거리와 맞는지 + 중복 없음 + `used` 카운트를 매번 검사한다.

**삽입 추적** (그림 ③, 실제 실행 출력):

```text
[A] 1,17,2             [ 1: 1/1] [ 2:17/2] [ 3: 2/2] [ 4:  .  ] [ 5:  .  ]
[A] +33 (robin hood)   [ 1: 1/1] [ 2:17/2] [ 3:33/3] [ 4: 2/3] [ 5:  .  ]
[A] find(49) miss after 4 probes (linear probing would need 5)
```

| 단계 | 슬롯 | 슬롯의 항목 / psl | 들고 있는 것 / psl | 판단 |
|---|---|---|---|---|
| 1 | 1 | 1 / 1 | 33 / 1 | 1 ≥ 1, 키 다름 → 전진 |
| 2 | 2 | 17 / 2 | 33 / 2 | 2 ≥ 2, 키 다름 → 전진 |
| 3 | 3 | 2 / 2 | 33 / 3 | **2 < 3 → 33 이 확정 새 키, 자리 뺏기**. 이제 2/2 를 든다 |
| 4 | 4 | EMPTY | 2 / 3 | 내려놓기, used++ |

동점(psl 같음)에서는 **바꾸지 않는다**. 같은 칸에서 psl 이 같으면 홈도 같다는 뜻이라 바꿔 봐야 이득이 없고, 1단계에서 같은 키를 놓칠 수 있다. 1단계의 키 비교가 `psl >= cur.psl` 인 칸에서만 일어나는 것도 이 때문이다.

`HT_FULL` 은 **1단계가 끝난 뒤, 첫 쓰기 전에** 결정한다. 2단계 중간에 실패하면 항목 하나가 손에 들린 채 사라진다. 그래서 limit 은 항상 cap 보다 작게(`limit_for`) 잡아 EMPTY 가 최소 1칸 있도록 한다 — 이것이 2단계 무한 루프와 삭제 루프의 **종료 보장**이다.

### Part B — backward-shift 삭제

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">del(15): 랩어라운드를 건너 당기기 — 15,31,47 은 홈 15, 0 은 홈 0</text>
  <g fill="currentColor"><text x="10" y="72">전</text><text x="10" y="172">후</text></g>
  <g stroke="currentColor" fill="none">
    <rect x="60" y="50" width="100" height="34"/><rect x="180" y="50" width="100" height="34"/><rect x="280" y="50" width="100" height="34"/><rect x="380" y="50" width="100" height="34"/><rect x="480" y="50" width="100" height="34"/>
    <rect x="60" y="150" width="100" height="34"/><rect x="180" y="150" width="100" height="34"/><rect x="280" y="150" width="100" height="34"/><rect x="380" y="150" width="100" height="34"/><rect x="480" y="150" width="100" height="34"/>
  </g>
  <rect x="60" y="50" width="100" height="34" class="f-warn"/>
  <rect x="480" y="150" width="100" height="34" class="f-mut"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="110" y="44" font-size="12">슬롯15</text><text x="230" y="44" font-size="12">0</text><text x="330" y="44" font-size="12">1</text><text x="430" y="44" font-size="12">2</text><text x="530" y="44" font-size="12">3</text>
    <text x="110" y="72">15 / 1</text><text x="230" y="72">31 / 2</text><text x="330" y="72">47 / 3</text><text x="430" y="72">0 / 3</text><text x="530" y="72">.</text>
    <text x="110" y="172">31 / 1</text><text x="230" y="172">47 / 2</text><text x="330" y="172">0 / 2</text><text x="430" y="172">.</text><text x="530" y="172">.</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-dasharray="4 3">
    <path d="M225 88 L118 146"/><path d="M325 88 L238 146"/><path d="M425 88 L338 146"/>
  </g>
  <g fill="currentColor">
    <text x="592" y="64">다음 칸 psl &gt; 1</text><text x="592" y="82">→ 당기고 psl−1</text>
    <text x="592" y="164">슬롯3 psl 0 → 멈춤</text><text x="592" y="182">슬롯2 를 EMPTY 로</text>
    <text x="10" y="222">멈춤: 다음 칸이 EMPTY(psl 0) 또는 자기 홈(psl 1). psl 1 을 당기면 홈 앞에 놓인다.</text>
    <text x="10" y="246">구간이 통째로 한 칸 이동 → 불변식 psl[j] ≤ psl[j−1]+1 유지, 끝은 EMPTY.</text>
    <text x="10" y="270">묘비 0개 → DS-B11 연습 1 의 churn 문제(실패 조회 = cap칸)가 없다.</text>
  </g>
</svg>
```

실제 출력 (홈 1 클러스터에서 17 삭제):

```text
[B] del 17 (shift)     [ 1: 1/1] [ 2:33/2] [ 3: 2/2] [ 4:  .  ] [ 5:  .  ]
```

33 은 3 → 2(psl 3 → 2), 2 는 4 → 3(psl 3 → 2), 슬롯 4 는 EMPTY. 이어서 1 을 지우면 33 이 홈(슬롯 1)으로, 2 도 홈(슬롯 2)으로 돌아가고 슬롯 3 에서 멈춘다 — 하네스가 이 두 단계를 모두 검사한다.

**왜 Robin Hood 에서 특히 자연스러운가**: psl 이 저장돼 있어 "당겨도 되나?"가 `psl > 1` 비교 하나다. psl 이 없는 선형 탐사(DS-B11 연습 2)는 홈을 다시 계산해 순환 구간 판정을 해야 한다.

### Part B — 리사이즈: 표는 메모리를 소유하지 않는다

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m08a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">핑퐁 버퍼: 7/8 에서 2배 (stop-the-world) vs 점진적 리해싱 (연습 1)</text>
  <g fill="currentColor"><text x="10" y="62">buf A (cap C)</text><text x="10" y="122">buf B (cap 2C)</text></g>
  <rect x="120" y="45" width="280" height="26" class="f-warn" stroke="currentColor"/>
  <rect x="400" y="45" width="40" height="26" fill="none" stroke="currentColor"/>
  <rect x="120" y="105" width="280" height="26" class="f-ok" stroke="currentColor"/>
  <rect x="400" y="105" width="320" height="26" fill="none" stroke="currentColor"/>
  <g fill="currentColor"><text x="130" y="63">7/8 찼음 → rh_put = HT_FULL</text><text x="130" y="123">한 번에 전부 rehash → α = 7/16</text></g>
  <path d="M260 74 L260 100" stroke="currentColor" fill="none" marker-end="url(#m08a)"/>
  <text x="270" y="92" fill="currentColor">rh_rehash(t, B, 2C) → A 를 돌려받아 spare 로</text>
  <text x="10" y="158" fill="currentColor" font-weight="bold">점진적: 옛 표를 얼리고(읽기 전용) 연산마다 step 칸씩만 옮긴다</text>
  <g fill="currentColor"><text x="10" y="202">old (얼림)</text><text x="10" y="262">new</text></g>
  <rect x="120" y="185" width="140" height="26" class="f-mut" stroke="currentColor"/>
  <rect x="260" y="185" width="180" height="26" class="f-warn" stroke="currentColor"/>
  <rect x="120" y="245" width="600" height="26" fill="none" stroke="currentColor"/>
  <rect x="120" y="245" width="210" height="26" class="f-ok" stroke="currentColor"/>
  <path d="M260 170 L260 214" stroke="currentColor" stroke-width="2"/>
  <g fill="currentColor"><text x="130" y="203">옮김</text><text x="270" y="203">아직 (조회 대상)</text><text x="266" y="180">cursor</text></g>
  <path d="M300 214 L300 240" stroke="currentColor" fill="none" marker-end="url(#m08a)"/>
  <text x="310" y="233" fill="currentColor">step 칸/연산</text>
  <g fill="currentColor">
    <text x="10" y="296">조회: new → (없으면) old.  삽입: 항상 new.  삭제: new 에서 지우고 old 사본엔 dead 표시.</text>
    <text x="10" y="318">메모리는 같다(두 버퍼). 바뀌는 건 "한 연산이 하는 최대 일" — 실측 6970칸 → 55칸.</text>
  </g>
</svg>
```

malloc 없는 펌웨어에서 "리사이즈"는 **호출자가 다른 버퍼를 건네는 것**이다. API를 이렇게 나누면 표 코드는 메모리 출처를 모른다 — 정적 배열 두 개(핑퐁), 메모리 풀(DS-M02), 버디 할당기 블록(DS-M04), Linux 라면 kvmalloc 무엇이든 된다.

```c
int     rh_put(ht_t *t, uint32_t key, uint32_t val);          /* 7/8 이면 HT_FULL = "더 큰 버퍼를 줘" */
slot_t *rh_rehash(ht_t *t, slot_t *newbuf, uint32_t newcap);  /* 옛 버퍼를 돌려준다 (실패면 NULL, 표 그대로) */
```

**부하율 정책**:

| 정책 | 값 | 이유 |
|---|---|---|
| 성장 문턱 | used ≥ 7/8·cap 이면 새 키 거절 (`limit = cap − cap/8`) | Robin Hood 는 α 0.85~0.9 에서도 평균 psl 3.85~5.34, 최대 22.8~28.5 (Part C). 정수 시프트로 계산 |
| 성장 후 | α = 7/16 ≈ 0.44 | 두 배 |
| 축소 문턱 | used < 1/4·cap | 축소 후 α < 1/2 — 성장 문턱(7/8)과 거리가 충분 |
| 축소 하한 | cap ≥ 16 | 작은 표를 계속 반으로 쪼개지 않기 |

축소 문턱을 성장 문턱의 절반(7/16)으로 잡으면 경계에서 put/del 이 번갈아 올 때 **매 연산마다 rehash** 한다 — 연습 4 실측: 1000번 연산에 rehash 1000번, 항목 896000개 이동. 1/4 이면 1번. CLRS 17.4(동적 테이블)의 "가득 차면 2배, 1/4 이면 절반 → 분할 상환(amortized) O(1)" 과 같은 논리다.

**랜덤 테스트 결과** (실제 출력):

```text
[B] random 30000 ops vs reference: match (grows=9 shrinks=1 max cap=4096, now cap=4096 used=1928)
[B] worst single put: rehash moved 1792 entries (stop-the-world)
```

키 3000종 중 1/4 은 하위 8비트가 0 인 키(`i << 6`, i 가 4의 배수)라 홈이 256의 배수에만 떨어진다 — 작은 cap 에서는 전부 홈 0 으로 몰려 랩어라운드하는 거대 클러스터가 생긴다. 이런 악조건에서 성장 → 축소 → 재성장을 거치며 매 연산 `rh_check()` 통과.

마지막 줄이 리얼타임의 문제다: 2048 → 4096 성장을 일으킨 **put 한 번이 1792개를 옮겼다**. 평균은 분할 상환 O(1) 이지만 그 한 번의 put 은 O(n) — 10 µs 예산의 ISR 이나 제어 루프에서는 데드라인을 놓친다.

**점진적 리해싱(incremental rehash)** — RT 친화적 변형 (연습 1 에서 구현):

1. 성장 시 옛 표를 **얼린다**(더 이상 이동 없음 → 옛 표의 조기 종료 조회가 계속 맞다).
2. 모든 연산이 먼저 옛 표의 `step` 칸만 새 표로 옮긴다 (`cursor` 증가). 새 표에 같은 키가 있으면(더 새 값) 건너뛴다.
3. 조회는 새 표 → 없으면 옛 표. 삽입은 새 표에만. 삭제는 새 표에서 지우고 옛 표 사본엔 `dead` 표시 (옛 표 전용 묘비 — 얼린 표에서 backward-shift 를 하면 cursor 뒤의 항목이 이미 지나간 칸으로 밀려와 누락된다).
4. cursor 가 끝에 닿으면 옛 버퍼 해제.

새 표 용량 2C, 시작 시 항목 7/8·C. 이주는 C/step 연산 동안 끝나고 그동안 새 키는 최대 C/step 개 들어온다. `7/8·C + C/step ≤ 7/4·C` 이려면 `step ≥ 8/7`, 즉 **step ≥ 2 면 이주 중 새 표가 다시 차지 않는다**. 같은 연산 열의 실측: stop-the-world 의 최악 연산 6970칸 방문, 점진적(step 4) 이주 중 최악 55칸. Redis 의 `dict`(두 테이블 + `rehashidx`, 연산마다 `_dictRehashStep`)와 Linux `rhashtable`(lib/rhashtable.c, `future_tbl` 로 두 테이블 조회, 워크큐 `rht_deferred_worker` 에서 버킷 단위 이주)이 같은 생각이다 — 둘 다 체이닝이라 버킷 단위로 옮기기가 더 쉽다.

### Part C — 측정 결과 (실제 실행값, cap 16384, 시드 4개 평균)

```text
[C] alpha | avg psl  lp     rh | max psl  lp     rh | var  lp      rh | miss  lp      rh
[C]  0.50 |      1.50   1.50 |      23.0    8.5 |    1.58    0.67 |     2.50    1.75
[C]  0.60 |      1.75   1.75 |      34.2   10.2 |    3.48    1.15 |     3.67    2.06
[C]  0.70 |      2.16   2.16 |      60.2   13.5 |    8.87    2.17 |     5.96    2.51
[C]  0.80 |      3.00   3.00 |     120.8   19.8 |   35.23    5.50 |    12.74    3.41
[C]  0.85 |      3.85   3.85 |     184.0   22.8 |   84.16   10.27 |    21.43    4.28
[C]  0.90 |      5.34   5.34 |     314.0   28.5 |  235.80   20.44 |    43.83    5.83
[C]  0.95 |      9.22   9.22 |    1113.0   40.0 | 1344.80   56.78 |   170.68    9.79
```

| α | 평균 psl = 성공 조회 (선형 / RH) | 최대 psl (선형 / RH) | psl 분산 (선형 / RH) | 실패 조회 평균 (선형 / RH) |
|---|---|---|---|---|
| 0.50 | 1.50 / 1.50 | 23.0 / 8.5 | 1.58 / 0.67 | 2.50 / 1.75 |
| 0.60 | 1.75 / 1.75 | 34.2 / 10.2 | 3.48 / 1.15 | 3.67 / 2.06 |
| 0.70 | 2.16 / 2.16 | 60.2 / 13.5 | 8.87 / 2.17 | 5.96 / 2.51 |
| 0.80 | 3.00 / 3.00 | 120.8 / 19.8 | 35.23 / 5.50 | 12.74 / 3.41 |
| 0.85 | 3.85 / 3.85 | 184.0 / 22.8 | 84.16 / 10.27 | 21.43 / 4.28 |
| 0.90 | 5.34 / 5.34 | 314.0 / 28.5 | 235.80 / 20.44 | 43.83 / 5.83 |
| 0.95 | 9.22 / 9.22 | 1113.0 / 40.0 | 1344.80 / 56.78 | 170.68 / 9.79 |

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">최대 psl vs 부하율 (실측, 로그 눈금, cap 16384)</text>
  <g stroke="currentColor" fill="none">
    <path d="M70 240 L690 240"/><path d="M70 240 L70 40"/>
    <path d="M66 179.4 L70 179.4"/><path d="M66 118.8 L70 118.8"/><path d="M66 58.2 L70 58.2"/>
  </g>
  <g fill="currentColor" text-anchor="end"><text x="62" y="244">1</text><text x="62" y="183">10</text><text x="62" y="123">100</text><text x="62" y="62">1000</text></g>
  <g fill="currentColor" text-anchor="middle">
    <text x="80" y="258">0.50</text><text x="213" y="258">0.60</text><text x="347" y="258">0.70</text><text x="480" y="258">0.80</text><text x="547" y="258">0.85</text><text x="613" y="258">0.90</text><text x="680" y="258">0.95</text>
    <text x="380" y="280">부하율 α</text>
  </g>
  <path d="M80 157.5 L213 147.1 L347 132.2 L480 113.9 L547 102.8 L613 88.7 L680 55.4" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M80 183.7 L213 178.9 L347 171.5 L480 161.4 L547 157.7 L613 151.9 L680 142.9" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4" fill="none"/>
  <g class="f-warn" stroke="currentColor"><circle cx="680" cy="55.4" r="4"/><circle cx="547" cy="102.8" r="4"/></g>
  <g class="f-ok" stroke="currentColor"><circle cx="680" cy="142.9" r="4"/><circle cx="547" cy="157.7" r="4"/></g>
  <g fill="currentColor">
    <text x="600" y="50">1113</text><text x="500" y="96">184</text>
    <text x="640" y="132">40</text><text x="520" y="175">22.8</text>
    <rect x="90" y="44" width="250" height="56" rx="8" class="f-mut" stroke="currentColor"/>
    <text x="100" y="66">— 실선: 선형 탐사</text>
    <text x="100" y="88">- - 점선: Robin Hood</text>
  </g>
</svg>
```

읽는 법:

1. **평균 psl 은 두 표가 소수점까지 같다** — 하네스가 Σpsl 정수 합이 정확히 같은지 `CHECK` 한다. 우연이 아니라 정리다 (아래 증명). 선형 탐사 평균은 DS-B11 의 Knuth 식 `½(1+1/(1−α))` 과도 맞는다 (0.8 → 3.0).
2. **최대와 분산이 확 줄어든다**. α=0.85 에서 최대 184 → 22.8 (8배), 0.95 에서 1113 → 40 (28배). 선형 탐사에서는 클러스터 끝에 늦게 온 키가 혼자 멀리 가지만, Robin Hood 는 그 거리를 클러스터 전체가 나눠 진다.
3. **실패 조회도 짧아진다** (조기 종료). α=0.9 에서 43.8 → 5.8칸. 선형 탐사의 실패 조회는 EMPTY 까지 클러스터 끝을 다 걸어야 한다.
4. **공짜는 아니다**: 삽입은 두 표 모두 "홈 → 첫 EMPTY" 구간을 걷는다 (Robin Hood 의 1단계 + 2단계가 정확히 그 구간). 즉 RH 삽입 비용 ≈ 선형 탐사 실패 조회 비용이고, 거기에 swap 쓰기가 붙는다. 비용을 조회에서 삽입으로 옮긴 셈 — 조회가 압도적으로 많은 표(라우팅, 캐시 매핑)에 맞는 거래다.

**평균이 같은 이유 (증명)**. 두 방식 모두 새 키는 결국 "홈부터 처음 만나는 EMPTY" 칸을 채운다 (RH 는 다른 항목이 그 칸으로 밀려날 뿐). 그러니 **차는 칸의 집합 = 클러스터가 같다**. 항목은 자기 홈이 있는 클러스터 안에만 있고(홈 → 자리 사이에 EMPTY 가 없으므로), 클러스터 시작을 s 라 하면 `psl − 1 = (자리 − s) − (홈 − s)`. 클러스터별로 더하면 Σ(자리 − s) 는 차 있는 칸들로, Σ(홈 − s) 는 그 클러스터 키들의 홈으로 정해진다 — 배치 방식과 무관. 따라서 Σpsl 이 같고 평균 성공 조회 비용이 같다. ∎ (표가 완전히 꽉 차 클러스터가 원형 하나가 되는 경우는 예외지만 limit < cap 이라 생기지 않는다. 연습 3 에서 30가지 삽입 순서로 확인.)

**펌웨어 설계로 옮기기** — 예: 이더넷 스위치 MCU 의 MAC 학습 테이블 약 14000항목을 cap 16384 표(슬롯 12B → 192KB)에 둔다 (α ≈ 0.85). 수신 경로의 조회 예산이 32칸이라면, 측정 최대 psl 이 선형 184 / RH 22.8 이므로 **RH 만 예산 안**이다. 선형 탐사로 같은 예산을 맞추려면 α 0.5(측정 최대 23)까지 낮춰야 하고, 같은 항목 수에 cap 이 0.85/0.5 = 1.7배 필요하다 (2의 거듭제곱으로 올리면 32768, 384KB).

주의: 이 최대값은 **관측값**이지 보장이 아니다 (cap 이 커지면 최대도 천천히 커진다). 하드 리얼타임이면 삽입 때 psl 상한을 **강제**해야 한다 — 연습 2: psl ≤ 16 을 강제하면 선형은 α 0.483 에서, Robin Hood 는 0.848 에서 처음 거절했다. 그 대신 모든 조회가 16(실패는 17)칸 이내로 **보장**된다.

**복잡도와 메모리**:

| 연산 | 평균 | 최악 | 비고 |
|---|---|---|---|
| 조회 성공 | `½(1+1/(1−α))` (선형과 같음) | max psl (측정: 0.85 에서 ~23) | 분산 작음 → 예측 가능 |
| 조회 실패 | 선형보다 훨씬 짧음 (0.9: 5.8 vs 43.8) | max psl + 1 | 조기 종료 |
| 삽입 | 선형 실패 조회와 같은 칸 수 + swap 쓰기 | 클러스터 길이 | 쓰기가 여러 칸 |
| 삭제 | 조회 + 클러스터 꼬리 당기기 | 클러스터 길이 | 묘비 0 |
| rehash | O(oldcap + n) | 한 연산에 전부 (1792개 이동) | 점진적이면 연산당 O(step) |

슬롯 12바이트(키 4 + 값 4 + psl 2 + 패딩 2). psl 을 `uint8_t` 로 줄이고 상한 255 를 강제하거나, Swiss table 처럼 메타데이터를 별도 바이트 배열로 빼면 캐시 라인당 더 많은 슬롯을 본다. 핑퐁 버퍼 방식은 RAM 이 **최대 표의 2배** 필요하다는 점을 잊지 말 것 — RAM 이 정해진 MCU 라면 그냥 최대 크기로 잡고 리사이즈하지 않는 게 보통 정답이다(아래 Q&A).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 44/44 passed
# 메모리 오류 확인(선택)
cc -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 동점(`psl == cur.psl`)에서도 swap, 그 검사를 키 비교보다 먼저 | 같은 키는 그 칸에서 psl 이 반드시 같다 → 갱신 대신 뺏기 → **중복 키**. 아니어도 같은 홈끼리 쓰기만 늘어남 | 엄격한 `<` 로만 뺏고, 키 비교가 먼저 |
| psl 필드를 `uint8_t` 로 줄이고 상한 강제 없음 | 선형 탐사 실측 최대 psl 1113 (α 0.95) — 255 를 넘으면 값이 감겨 조기 종료가 틀린다 | `uint16_t`, 또는 상한 강제(연습 2) |
| 2단계 도중 꽉 참을 발견하고 `HT_FULL` 반환 | 손에 든 항목이 표에서 **사라진다** | 1단계 끝(첫 쓰기 전)에 `used >= limit` 검사, limit < cap 으로 EMPTY 보장 |
| 조회에서 EMPTY 만 검사하고 `psl < d` 조기 종료 없음 | 맞긴 하지만 실패 조회가 선형 탐사와 똑같이 느림 (0.9: 43.8칸) | `if (s->psl < d) return -1;` |
| backward-shift 에서 psl 1 항목까지 당김 | 홈보다 앞에 놓여 조회가 영영 못 찾음 | `while (next.psl > 1)` |
| 당긴 항목의 psl 을 안 줄임 | 불변식 (2) 위반 → 다음 조회의 조기 종료가 틀어짐 | `slot[i].psl--` |
| rehash 에서 `ht_init(newbuf)` 후 크기 검사 | 실패해도 표가 이미 빈 새 버퍼를 가리킴 → 데이터 유실 | 검사 먼저, 옛 포인터 저장, 그다음 init |
| `newbuf == t->slot` 허용 | init 의 memset 이 옛 항목을 지운 뒤 읽음 | 같은 버퍼면 NULL |
| 축소 문턱 = 성장 문턱 / 2 | 경계에서 put/del 마다 rehash (연습 4: 1000번) | 축소 1/4, 성장 7/8 |
| 부하율 계산 `used / cap > 0.875` 를 float 로 | FPU 없는 Cortex-M0 에서 소프트웨어 부동소수 | `used >= cap - cap/8` 정수 |

## 8. 면접 Q&A

<details>
<summary>Q1. Robin Hood 해싱이 평균 탐사 길이를 줄이나요?</summary>

아니다 — **성공 조회의 평균은 정확히 같다**. 선형 탐사든 Robin Hood 든 차는 칸의 집합과 각 클러스터에 속한 키가 같아서 Σpsl 이 같다 (5. 풀이의 증명, 실측도 0.5~0.95 전 구간에서 정수 합 일치). 줄어드는 건 **분산과 최대**(α=0.9 에서 최대 314 → 28.5)와, 조기 종료 덕분의 **실패 조회** 평균(43.8 → 5.8)이다. 면접관이 "평균이 줄어든다"는 답을 유도하는 경우가 많아서 이 구분이 점수가 된다.

</details>

<details>
<summary>Q2. 조기 종료 조회가 왜 맞는지 증명해 보세요.</summary>

불변식 `psl[j] ≤ psl[j−1] + 1`. 키 K 가 psl D 로 칸 p 에 있으면, 거꾸로 적용해 `psl[p−k] ≥ D−k`. 조회가 d 번째 칸에서 보는 psl 은 ≥ d. 그러니 K 에 닿기 전엔 `psl < d` 를 못 만난다.

꼬리질문 "삽입이 불변식을 지키나?": 들고 있는 항목 cur 가 칸 i 에 올 때 항상 `psl[i−1] ≥ cur.psl − 1` 이다 — i−1 에서 안 뺏었으면 그 칸 psl ≥ 당시 cur.psl(= 지금 cur.psl − 1), 뺏었으면 거기 놓인 항목의 psl 이 쫓겨난 항목의 옛 psl + 1(= 지금 cur.psl) 이상이기 때문. 그래서 i 에 cur 를 내려놓아도 `psl[i] ≤ psl[i−1] + 1`. 뒤 칸 i+1 은 옛 `psl[i]` 기준으로 만족했고 새 psl[i] 는 더 크므로 계속 만족. `rh_check()` 가 매 연산 이걸 검사한다.

</details>

<details>
<summary>Q3. 해시 테이블이 꽉 차면? malloc 이 없는 펌웨어에서 리사이즈는?</summary>

먼저 "리사이즈가 필요한가"를 묻는다. RAM 이 링크 타임에 정해지는 MCU 라면 최대 항목 수 N 에 맞춰 cap 을 고정(α ≤ 0.85 RH)하는 게 보통 정답이다 — 핑퐁 리사이즈는 최대 표의 2배 RAM 이 필요하기 때문. 리사이즈가 의미 있는 경우: 메모리를 다른 서브시스템과 나눠 쓰는 경우(부팅 중엔 작게, 이후 풀에서 큰 블록), SSD 처럼 SKU 마다 DRAM 크기가 달라 런타임에 정하는 경우, 해시 시드를 바꿔야 하는 경우(same-size rehash). 그때도 표는 메모리를 **할당하지 않고** `HT_FULL` 로 신호만 주고, 호출자가 버퍼를 건네는 `rh_rehash(t, newbuf, newcap)` 구조로 만든다.

</details>

<details>
<summary>Q4. 리사이즈 중 지연 시간이 문제라면? (꼬리질문: 하드 리얼타임)</summary>

stop-the-world rehash 는 그 put 한 번이 O(n) — 실측: 본문 랜덤 테스트에서 1792개 이동, 연습 1 에서 한 연산 6970칸 방문. 해법은 점진적 리해싱: 옛 표를 얼리고 연산마다 step 칸만 옮기며 조회는 두 표를 본다. 실측 이주 중 최악 연산 55칸. 비용은 이주 동안 두 버퍼 유지, 조회가 최대 두 번, 그리고 새 버퍼 memset(O(newcap)) — 이건 유휴 시간에 미리 지우거나 DMA 로 지운다. Redis `dict` 가 두 테이블 + `rehashidx` 로, Linux `rhashtable` 이 `future_tbl` + 워크큐(`rht_deferred_worker`)로 같은 일을 한다. 완전히 묶고 싶으면 PSL 상한 강제(연습 2)까지 더해 "조회 ≤ pmax+1칸, 연산당 이주 ≤ step칸"이라는 WCET 를 문서화한다.

</details>

<details>
<summary>Q5. ISR 이 조회만 하고 메인 루프가 갱신하면 Robin Hood 표는 안전한가요?</summary>

락 없이는 **안전하지 않다**, 선형 탐사+묘비보다 더 위험하다. Robin Hood 삽입의 2단계에서 쫓겨난 항목은 잠시 **지역 변수에만** 있다 — 그 순간 ISR 조회는 표에 있는 키를 "없음"으로 본다. backward-shift 삭제도 항목을 뒤에서 앞으로 옮기므로 이미 그 칸을 지나간 조회가 놓친다. 묘비 방식은 기존 항목을 절대 옮기지 않아 "값 먼저 쓰고 상태 바이트 나중에 + 배리어"로 단일 writer/다중 reader 를 만들 수 있다. RH 를 쓰려면 갱신을 임계 구역(PRIMASK, 길이 = 클러스터 길이라 상한 확인 필요)으로 묶거나, seqlock(쓰기 전후 카운터 증가, 읽는 쪽은 홀수거나 바뀌었으면 재시도)을 쓴다. ISR 안에서 rehash 는 절대 하지 않는다.

</details>

<details>
<summary>Q6. Robin Hood, Swiss table, cuckoo 해싱을 비교하면?</summary>

Robin Hood: 선형 탐사 + psl, 최대·분산 작음, 묘비 없는 삭제, 단순. 삽입 쓰기가 많다. Rust 표준 `HashMap` 이 1.36 이전까지 Robin Hood 였고 이후 hashbrown(Swiss table)로 바뀌었다. Swiss table(Abseil): 슬롯마다 1바이트 제어 바이트(해시 7비트 + 상태)를 그룹 단위(SSE2 면 16개)로 한 번에 비교 — x86 에선 SSE2, 그 외에는 워드 단위 비트 트릭의 portable 경로로 돈다. Cortex-M 에서는 SIMD 이득이 줄어든다. cuckoo: 조회 최악 2칸 **보장**(WCET 최강)이지만 삽입이 실패/재시도 사슬을 가질 수 있고 α ≈ 0.5(2-way) 근처가 한계. 펌웨어에서 조회 WCET 가 절대적이면 cuckoo 또는 PSL 상한 RH, 메모리가 절대적이면 RH(α 0.85).

</details>

<details>
<summary>Q7. 성장 문턱 7/8, 축소 1/4 은 어떻게 정했나요?</summary>

성장: Part C 에서 RH 는 0.85~0.9 에서도 평균 psl 4~5, 최대 23~29 로 버틴다. 0.95 부터 최대 40, 분산 57 로 나빠지므로 그 전인 7/8. 정수로 `cap − cap/8` 이라 FPU 가 없어도 된다. 축소: 축소 후 α 가 성장 문턱에서 충분히 떨어져야 한다. 1/4 에서 절반으로 줄이면 α < 1/2, 다음 성장까지 cap·3/8 이상의 삽입이 필요해 rehash 비용이 분할 상환된다. 문턱 사이가 좁으면(7/16) 경계에서 연산마다 rehash — 연습 4 실측 1000번.

</details>

## 9. 추가 연습

### 연습 1 — 점진적 리해싱 (incremental rehash)

Part B 의 핑퐁 맵을, 옛 표를 얼리고 연산마다 `step` 칸씩만 옮기는 맵으로 바꿔라 (조회 new → old, 삽입 new, 삭제는 new 에서 지우고 old 사본에 `dead`). 같은 20000개 연산 열을 stop-the-world(step = ∞)와 step = 4 로 돌려 참조 모델과 대조하고, **연산당 최대 슬롯 방문 수**를 비교하라.

<details>
<summary>답 보기</summary>

실제 측정:

```text
[1] 20000 ops vs reference: match | worst slot visits per op: stop-the-world 6970, incremental(step=4) 257
[1] incremental: 1188 ops while migrating (worst 55 visits), other ops worst 257 visits
```

- stop-the-world 의 최악 연산(2048 → 4096 이주를 떠안은 연산)은 6970칸. 점진적은 **이주 중 최악이 55칸** — rehash 비용이 사라진 게 아니라 1188개 연산에 나눠졌다.
- 점진적 쪽의 전체 최악 257칸은 이주와 무관한 연산이다 (디버그로 확인: cap 2048, used 1745 — α ≈ 0.85 에서의 새 키 삽입). RH 삽입은 홈부터 첫 EMPTY 까지 클러스터 끝을 걷는다 — 조회 최악은 psl 로 묶여도 **삽입·삭제 최악은 클러스터 길이**다. 이것까지 묶으려면 연습 2 의 상한이 필요.
- 옛 표에서 backward-shift 를 하지 않고 `dead` 를 쓰는 이유: 당겨진 항목이 이미 cursor 가 지나간 칸으로 들어가면 이주에서 빠진다. 얼린 표는 위치가 안 바뀌어야 cursor 가 의미가 있다.
- `step ≥ 2` 면 이주가 끝나기 전에 새 표(2C, limit 7/4·C)가 다시 차지 않는다 (5. 풀이). 새 버퍼 memset 은 성장을 일으킨 연산에서 O(2C) — `g_visits` 에 안 잡히는 비용이라 실제 펌웨어에선 미리 지워 둔다.

</details>

### 연습 2 — PSL 상한 강제로 WCET 보장

`rh_put_bounded(t, key, val, pmax)` 를 만들어라: 쓰기 전에 자리 뺏기 사슬을 **시뮬레이션**해서 어떤 항목이든 내려놓을 때 psl 이 pmax 를 넘으면 `HT_PSL` 로 거절(표는 그대로). cap 4096, pmax 16 에서 무작위 키를 넣어 처음 거절되는 α 를 선형 탐사(새 키의 거리만 검사)와 비교하라.

<details>
<summary>답 보기</summary>

```text
[2] psl<=16 enforced: first reject at alpha linear 0.483 | robin hood 0.848 (avg of 8 seeds)
[2] robin hood worst hit 16, worst miss 17 probes | linear miss walking to EMPTY: worst 33
```

- 같은 상한에서 Robin Hood 는 **α 0.848 까지**, 선형은 0.483 까지 — 같은 WCET 보장에 RAM 이 약 43% 덜 든다 (0.483/0.848 ≈ 0.57).
- 보장: 모든 psl ≤ 16 이면 성공 조회 ≤ 16칸, 조기 종료 실패 조회는 d = 17 에서 반드시 `psl < d` → ≤ 17칸. 실측 최악 16 / 17.
- 선형 탐사의 실패 조회는 EMPTY 까지 걸어 33칸까지 갔다. 상한을 강제한 표라면 선형 탐사도 "pmax 칸 보고 멈춤" 으로 조회를 묶을 수 있다 — 중요한 건 RH 가 **같은 상한을 훨씬 높은 α 에서** 지킨다는 점.
- 거절은 "리사이즈하라" 신호로 쓰거나(Skarupke 의 `flat_hash_map` 도 탐사 길이 상한을 넘으면 키운다), 고정 RAM 이면 항목 수 상한을 설계 문서에 적는다.

</details>

### 연습 3 — 삽입 순서와 무관한 배치 (정규형 Robin Hood)

swap 조건을 `slot.psl < cur.psl || (slot.psl == cur.psl && slot.key > cur.key)` 로 바꾸면(같은 홈끼리는 키 순서) 배치가 **삽입 순서와 무관**해짐을 확인하라. cap 1024 에 키 700개를 30가지 순서로 넣고, 700개 넣고 300개 지운 표가 남은 400개만 넣은 표와 같은지도 보라. 선형 탐사는?

<details>
<summary>답 보기</summary>

```text
[3] 30 orders: canonical RH layouts identical=yes, after 300 deletes=yes | linear layouts differ=yes, occupancy & sum(psl) identical=yes
```

- 같은 칸에서 psl 이 같으면 홈이 같다 → 동점 규칙은 **같은 홈 그룹 안의 순서**만 정한다. Robin Hood 는 클러스터 안에서 항목을 홈 순서로 유지하므로, 동점을 키로 깨면 (홈, 키) 전순서 정렬 = 유일한 배치. backward-shift 삭제는 구간을 통째로 한 칸 당길 뿐이라 정렬 순서를 지킨다.
- 선형 탐사(먼저 온 키가 자리 차지)는 순서마다 배치가 다르지만 **차는 칸과 Σpsl 은 30번 모두 같았다** — 5. 풀이의 "평균 동일" 정리의 실험적 확인.
- 쓸모: 같은 집합이면 메모리 이미지가 같으므로 표 전체를 CRC 로 비교하거나(이중화 MCU 간 상태 대조), 순서에 따라 덤프가 달라지지 않아 테스트가 결정적이 된다. 이런 성질을 history independence 라 부른다.

</details>

### 연습 4 — 리사이즈 thrash

cap 1024 에 정확히 896개(7/8)를 채우고 "새 키 put → 그 키 del" 을 500번 반복한다. 축소 문턱이 `used ≤ 7/16·cap` 일 때와 `used ≤ 1/4·cap` 일 때 rehash 횟수와 옮긴 항목 수를 세라.

<details>
<summary>답 보기</summary>

```text
[4] 500 x (put,del) at the 7/8 boundary: shrink<=7/16 -> 1000 rehashes (896000 entries moved) | shrink<=1/4 -> 1 rehash (896 moved)
```

성장 직후 α = 897/2048 ≈ 0.438 이고 del 하나로 896/2048 = 7/16 이 되어 바로 축소 → 다음 put 이 다시 성장. 연산 하나에 896개 이동, 분할 상환 O(1) 이 무너져 O(n)/연산이 된다. 1/4 이면 성장 후 축소까지 385개(897 → 512)를 더 지워야 해서 rehash 비용이 그 연산들에 나눠진다. 일반 규칙: **축소 후 α < 성장 문턱** 이고, 두 문턱 사이에 Θ(cap) 개 연산의 여유가 있어야 한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 9/9 passed
```

## 10. 핵심 정리

- Robin Hood: psl 을 저장하고, 들고 있는 항목보다 psl 이 **작은** 칸을 뺏는다 (동점은 안 뺏음). 키 비교는 첫 뺏기 전까지만.
- 불변식 `psl[j] ≤ psl[j−1] + 1` → 조회는 `psl < d` (EMPTY = 0 포함) 에서 조기 종료.
- 평균 psl(성공 조회)은 선형 탐사와 **정확히 같다**. 줄어드는 건 최대·분산·실패 조회 — 실측 α=0.9: 최대 314 → 28.5, 실패 43.8 → 5.8.
- 삭제는 backward-shift: 다음 칸 psl > 1 인 동안 당기고 psl−1. 묘비 0개.
- 리사이즈는 표가 `HT_FULL` 로 신호, 호출자가 버퍼를 주는 `rh_rehash(t, newbuf, newcap)`. 성장 7/8, 축소 1/4 (히스테리시스).
- stop-the-world rehash 한 번 = O(n) (실측 6970칸). RT 면 점진적 리해싱(옛 표 얼림 + 연산당 step 칸, 실측 55칸) + PSL 상한 강제(조회 ≤ pmax+1칸).
- RH 는 항목을 **옮기므로** ISR 무락 읽기에 불리하다 — 임계 구역이나 seqlock.

## 참고자료

- Pedro Celis, *Robin Hood Hashing*, PhD thesis, University of Waterloo, 1986
- Donald Knuth, *The Art of Computer Programming Vol. 3*, 6.4 Hashing — 선형 탐사 분석
- CLRS *Introduction to Algorithms* 3판, 17.4 Dynamic tables (확장·축소 문턱과 분할 상환 분석), 11.4 Open addressing
- [Emmanuel Goossaert — Robin Hood hashing: backward shift deletion](https://codecapsule.com/2013/11/17/robin-hood-hashing-backward-shift-deletion/)
- [Malte Skarupke — I Wrote The Fastest Hashtable](https://probablydance.com/2017/02/26/i-wrote-the-fastest-hashtable/)
- [Abseil — Swiss Tables design notes](https://abseil.io/about/design/swisstables)
- Linux kernel `lib/rhashtable.c`, `include/linux/rhashtable.h` (리사이즈 가능한 해시, `future_tbl`, `rht_deferred_worker`) / `include/linux/hashtable.h` (고정 크기)
- Redis `src/dict.c` — 두 테이블 점진적 리해싱 (`rehashidx`, `dictRehash`)
- Guy Blelloch, Daniel Golovin, *Strongly History-Independent Hashing with Applications*, FOCS 2007
