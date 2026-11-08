---
id: DS-B11
time: 60분
updated: 2026-11-07
---
# DS-B11 해시 테이블: 오픈 어드레싱과 Tombstone

## 학습 목표

- 선형 탐사(linear probing) 해시 테이블의 **조회·삽입 루프**를 경계 조건(랩어라운드, 꽉 찬 표)까지 정확히 쓴다.
- "그냥 비우는 삭제"가 왜 **뒤쪽 키 조회를 깨뜨리는지** 직접 재현하고, 그 원인을 불변식 한 줄로 설명한다.
- **tombstone(묘비)** 삭제 + "첫 묘비 재사용" 삽입을 구현하고, 중복 키 함정을 피한다.
- 부하율(load factor) α 별 평균 탐사 길이를 **직접 측정**해 Knuth 근사식과 비교하고, 펌웨어에서 표 크기를 고르는 근거로 쓴다.

## 1. 문제

오픈 어드레싱(open addressing)은 **포인터도 노드 풀도 없이 배열 하나**로 끝나는 해시 테이블이라 RAM이 빠듯한 MCU 펌웨어에서 체이닝(DS-B10)보다 자주 쓰인다. 면접에서는 "삭제를 어떻게 하나요?" 한 마디로 이 구조를 진짜 이해했는지 가려낸다 — 그래서 꼭 직접 버그를 내 봐야 한다.

규칙은 단순하다. 키의 **홈 슬롯(home slot)** `h = hash(key) mod cap` 이 이미 차 있으면 `h+1, h+2, ...` 로 옆 칸을 차례로 본다(선형 탐사). 조회도 같은 순서로 보다가 **빈칸(EMPTY)을 만나면 "없다"** 고 결론 낸다. 이 "빈칸에서 멈춘다"는 규칙 때문에 삭제가 까다로워진다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">cap=16, hash(k) = k &amp; 15 — 키 5, 21, 37 은 모두 홈 슬롯 5</text>
  <g fill="currentColor"><text x="10" y="72">① 넣은 직후</text><text x="10" y="162">② 그냥 비움</text><text x="10" y="252">③ tombstone</text></g>
  <g stroke="currentColor" fill="none">
    <rect x="140" y="50" width="80" height="34"/><rect x="220" y="50" width="80" height="34"/><rect x="300" y="50" width="80" height="34"/><rect x="380" y="50" width="80" height="34"/>
    <rect x="140" y="140" width="80" height="34"/><rect x="220" y="140" width="80" height="34"/><rect x="300" y="140" width="80" height="34"/><rect x="380" y="140" width="80" height="34"/>
    <rect x="140" y="230" width="80" height="34"/><rect x="220" y="230" width="80" height="34"/><rect x="300" y="230" width="80" height="34"/><rect x="380" y="230" width="80" height="34"/>
  </g>
  <rect x="220" y="140" width="80" height="34" class="f-warn"/>
  <rect x="220" y="230" width="80" height="34" class="f-mut"/>
  <rect x="300" y="230" width="80" height="34" class="f-ok"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="180" y="72">5</text><text x="260" y="72">21</text><text x="340" y="72">37</text><text x="420" y="72">.</text>
    <text x="180" y="162">5</text><text x="260" y="162">.</text><text x="340" y="162">37</text><text x="420" y="162">.</text>
    <text x="180" y="252">5</text><text x="260" y="252">T</text><text x="340" y="252">37</text><text x="420" y="252">.</text>
    <text x="180" y="44" font-size="12">슬롯5</text><text x="260" y="44" font-size="12">6</text><text x="340" y="44" font-size="12">7</text><text x="420" y="44" font-size="12">8</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M180 190 Q220 210 255 180"/>
    <path d="M180 280 Q220 300 260 270"/><path d="M260 280 Q300 300 340 270"/>
  </g>
  <g fill="currentColor">
    <text x="480" y="72">find(37): 5 → 6 → 7 찾음 (3칸)</text>
    <text x="480" y="155">find(37): 5 → 6 은 EMPTY</text>
    <text x="480" y="175">→ "없음" 으로 멈춤 ✗</text>
    <text x="480" y="245">find(37): 5 → 6(T, 지나감)</text>
    <text x="480" y="265">→ 7 찾음 ✓ (3칸)</text>
    <text x="10" y="315">② 에서도 37은 슬롯 7에 그대로 있다. 데이터가 아니라 "가는 길" 이 끊긴 것.</text>
  </g>
</svg>
```

### Part A — 선형 탐사 조회·삽입과 버그 재현

`uint32_t key → uint32_t val` 테이블. 슬롯은 `EMPTY / FULL / TOMB` 3가지 상태를 가진다. 용량 `cap` 은 2의 거듭제곱(최대 4096), 홈 슬롯은 `key & (cap-1)`.

- `ht_locate(t, key)` — 키가 있는 슬롯 번호, 없으면 -1. EMPTY에서 멈추고, TOMB는 건너뛰고, **최대 cap 칸만** 본다. 본 칸 수를 `t->last_probes` 에 남긴다.
- `ht_insert(t, key, val)` — 이미 있으면 값 갱신(`HT_UPDATED`), 없으면 삽입(`HT_OK`), 자리가 없으면 `HT_FULL`.
- 주어진 `ht_delete_naive()` (그냥 EMPTY로 되돌림)로 위 그림 ②의 **조회 실패를 재현**한다. 하네스가 `find(37) = NOT FOUND` 를 찍고, 불변식 검사기 `ht_check()` 가 깨짐을 잡아내야 정상이다.

### Part B — tombstone 삭제

- `ht_delete(t, key)` — 슬롯을 `TOMB` 로 표시. 조회는 묘비를 **지나가고**, 삽입은 묘비를 **재사용**한다.
- 삽입의 함정: 묘비를 만났다고 바로 넣으면, 같은 키가 묘비 **뒤쪽**에 이미 있을 때 중복이 생긴다. 체인 끝까지 확인한 뒤 "처음 본 묘비"에 넣어라.
- 꽉 찬 표, 묘비로 가득 찬 표에서도 루프가 끝나야 한다.
- 참조 모델(키 48종의 배열)과 20000번 랜덤 연산을 대조하고, **매 연산마다** `ht_check()` 로 불변식을 검사한다.

### Part C — 부하율별 평균 탐사 길이 측정

`measure_load(alpha, seed, &succ, &fail)` 를 완성한다. cap=4096 표를 무작위 키로 α 까지 채운 뒤,

- `succ` = 넣은 키 전부를 조회했을 때의 평균 탐사 수 (성공 조회)
- `fail` = 없는 키 20000개를 조회했을 때의 평균 탐사 수 (실패 조회)

α = 0.10 ~ 0.95, 시드 8개 평균을 Knuth 근사식과 나란히 출력한다.

## 2. Problem (English)

> **Open-addressing hash table with tombstones.**
>
> Implement a fixed-capacity hash map from `uint32_t` keys to `uint32_t` values using linear probing over a static array (no dynamic allocation). Capacity is a power of two up to 4096; the home slot is `key & (cap - 1)`.
>
> 1. `int32_t ht_locate(ht_t *t, uint32_t key)` returns the slot index of `key` or `-1`, examining at most `cap` slots and recording the number of examined slots in `t->last_probes`.
> 2. `int ht_insert(ht_t *t, uint32_t key, uint32_t val)` returns `HT_UPDATED` if the key existed, `HT_OK` if inserted, `HT_FULL` if no slot is available. It must never create duplicate keys.
> 3. Show that deleting by marking the slot `EMPTY` breaks lookups of other keys, then implement `bool ht_delete(ht_t *t, uint32_t key)` using tombstones.
> 4. Measure the average probe count of successful and unsuccessful lookups at load factors 0.1 to 0.95 and compare with Knuth's approximations `½(1 + 1/(1−α))` and `½(1 + 1/(1−α)²)`.
>
> Example (cap = 16): insert 5, 21, 37 → slots 5, 6, 7. Naive delete of 21 makes `find(37)` fail after 2 probes; tombstone delete keeps `find(37)` succeeding in 3 probes.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 이 쭉 나오고 "5/31 passed", exit 1 — TODO 4곳을 채워 31/31 을 만든다
```

TODO는 `ht_locate`, `ht_insert` (Part A), `ht_delete` (Part B), `measure_load` 의 평균 계산 (Part C) 네 곳이다. `ht_delete_naive`, `ht_check`, `ht_dump`, 측정용 표 채우기 코드는 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — 탐사 루프의 뼈대</summary>

`for (n = 0; n < cap; n++, i = (i + 1) & mask)` — 인덱스는 `& mask` 로 감아 돌리고(랩어라운드), 횟수는 `n < cap` 으로 묶는다. 이 두 줄이 "표 끝에서 0번으로 넘어가기"와 "EMPTY가 없는 표에서 무한 루프 방지"를 동시에 해결한다.

</details>

<details>
<summary>힌트 2 — 조회의 세 갈래</summary>

EMPTY → 즉시 -1 (체인 끝). FULL이고 키 일치 → 그 인덱스. 그 외(다른 키, TOMB) → 다음 칸. 묘비를 "없음"으로 취급하면 tombstone을 쓰는 의미가 없다.

</details>

<details>
<summary>힌트 3 — 삽입은 "기억하고 계속"</summary>

묘비를 처음 보면 인덱스를 `first_tomb` 에 적어두기만 하고 계속 간다. FULL이고 같은 키 → 갱신 후 리턴. EMPTY → 멈춤. 루프가 끝나면 `first_tomb` 가 있으면 거기, 없으면 멈춘 EMPTY 자리에 넣는다. 둘 다 없으면 `HT_FULL`.

</details>

<details>
<summary>힌트 4 — 카운터 두 개</summary>

`used` (FULL 수)와 `tombs` (TOMB 수)를 따로 센다. 묘비를 재사용하면 `tombs--`. `ht_check()` 가 이 둘을 실제 슬롯 상태와 대조하므로 하나라도 빠뜨리면 랜덤 테스트가 잡아낸다.

</details>

## 5. 풀이

### Part A — 조회가 기대는 불변식

선형 탐사가 맞게 동작하려면 다음 한 줄이 항상 참이어야 한다.

> **불변식**: FULL 슬롯 `j` 에 있는 키의 홈이 `h` 라면, `h, h+1, ..., j-1` 중에 **EMPTY 는 하나도 없다.**

삽입은 홈부터 처음 만난 빈자리에 넣으니 이 불변식을 저절로 지킨다. 조회는 이 불변식을 믿고 "EMPTY를 만나면 그 뒤엔 내 키가 없다"고 판단한다. `ht_check()` 는 바로 이 문장을 코드로 옮긴 것이다 (모든 FULL 키에 대해 홈부터 자기 자리까지 걸어가며 EMPTY·중복 검사).

그림 ②의 naive delete는 슬롯 6을 EMPTY로 만들어 키 37(홈 5, 자리 7)의 경로 `5 → 6 → 7` 중간에 구멍을 낸다. 실제 실행 출력:

```text
[A] before delete      [5:  5] [6: 21] [7: 37] [8:  .]
[A] naive delete(21)   [5:  5] [6:  .] [7: 37] [8:  .]
[A] find(37) = NOT FOUND (probes=2)  <- 37 is still in slot 7!
[B] tomb delete(21)    [5:  5] [6:  T] [7: 37] [8:  .]
[B] find(37) = found (probes=3)
```

단계별 추적:

| 단계 | 슬롯 5 | 슬롯 6 | 슬롯 7 | 조회 판단 |
|---|---|---|---|---|
| ① 5,21,37 삽입 | 5 | 21 | 37 | find(37): 5≠37, 21≠37, 37 ✓ — 3칸 |
| ② naive delete(21) | 5 | **EMPTY** | 37 | find(37): 5≠37, EMPTY → "없음" — 2칸 ✗ |
| ③ tomb delete(21) | 5 | **TOMB** | 37 | find(37): 5≠37, TOMB 지나감, 37 ✓ — 3칸 |

이 버그가 무서운 점: 삭제 직후 *삭제한 키*의 테스트는 통과한다. 깨지는 건 **같은 클러스터에 있던 다른 키**뿐이고, 충돌이 적은 작은 테스트에선 안 드러난다. 그래서 불변식 검사 + 충돌을 일부러 몰아주는 랜덤 테스트(키 = idx×8 → 홈 8곳)가 필요하다.

랩어라운드도 같은 규칙이다. 15와 31은 둘 다 홈 15 → 31은 `(15+1) & 15 = 0` 번 슬롯에 들어간다. `& mask` 로 감으면 분기 없이 처리된다.

### Part B — 슬롯 3상태와 삽입 규칙

```svg
<svg viewBox="0 0 720 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b11a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">슬롯 상태 전이 — 삭제로는 EMPTY 가 생기지 않는다 (재구성 전까지)</text>
  <rect x="40" y="80" width="120" height="50" rx="10" class="f-mut" stroke="currentColor"/>
  <rect x="300" y="80" width="120" height="50" rx="10" class="f-ok" stroke="currentColor"/>
  <rect x="560" y="80" width="120" height="50" rx="10" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="100" y="110">EMPTY</text><text x="360" y="110">FULL</text><text x="620" y="110">TOMB</text>
    <text x="230" y="96">insert</text>
    <text x="490" y="64">delete</text>
    <text x="490" y="161">insert (첫 묘비 재사용)</text>
    <text x="360" y="214">rebuild — 묘비를 EMPTY 로 되돌림 (연습 1)</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#b11a)">
    <path d="M160 105 L295 105"/>
    <path d="M420 95 Q490 55 555 95"/>
    <path d="M560 125 Q490 160 425 125"/>
    <path d="M620 130 C560 215 160 215 102 135" stroke-dasharray="4 3"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="248">조회: EMPTY = 멈춤, TOMB = 지나감, FULL = 키 비교.  삽입: TOMB = 후보, EMPTY = 끝.</text>
    <text x="10" y="270">묘비는 조회에겐 FULL 처럼, 삽입에겐 EMPTY 처럼 보이는 "반쯤 빈 칸" 이다.</text>
  </g>
</svg>
```

**삽입 순서도**:

1. 홈부터 탐사한다. FULL이고 키 같음 → 값 갱신, `HT_UPDATED`.
2. TOMB → 처음 본 것만 `first_tomb` 에 기억, **계속 간다**.
3. EMPTY → 키가 없다고 확정, 멈춘다.
4. `first_tomb` 가 있으면 거기, 없으면 3의 EMPTY에 넣는다. 둘 다 없으면 `HT_FULL`.

2번에서 바로 넣으면 생기는 버그를 테스트가 정확히 겨냥한다: 21 삭제 후(슬롯 6=TOMB) `insert(37, 3799)` 를 하면, 37은 슬롯 7에 이미 있다. 묘비에 바로 넣으면 37이 6과 7에 **두 개** 생기고, 이후 `delete(37)` 은 6번만 지워서 7번의 옛 값이 되살아난다. 체인 끝까지 확인하면 `HT_UPDATED` 가 되고 묘비는 그대로 남는다. 그 다음 새 키 53(홈 5)은 묘비 6을 재사용한다 (`tombs` 1 → 0).

**종료 보장**: 16칸을 0..15로 꽉 채우면 EMPTY가 하나도 없다. `n < cap` 경계가 없으면 없는 키 99 조회가 영원히 돈다. 16개를 모두 지우면 전부 TOMB — 이때도 EMPTY는 0개라서 실패 조회는 **정확히 16칸**을 본다. "다 지웠는데 표가 가장 느리다"는 묘비의 역설이다.

**랜덤 테스트**: 키 48종, 홈 8곳에 몰리는 키(idx×8)로 insert/delete/find를 20000번. 매번 결과를 참조 배열과 비교하고 `ht_check()` 를 돌린다. 실제 출력:

```text
[B] random 20000 ops vs reference: match (used=21 tombs=27)
```

살아 있는 키는 21개인데 묘비가 27개다 — 64칸 중 EMPTY는 16칸뿐. 묘비가 쌓이는 속도는 연습 1에서 측정한다.

### Part C — 측정 결과 (실제 실행값, cap=4096, 시드 8개 평균)

```text
[C] alpha | succ(meas) succ(Knuth) | fail(meas) fail(Knuth)
[C]  0.10 |       1.06        1.06 |       1.12        1.12
[C]  0.25 |       1.16        1.17 |       1.39        1.39
[C]  0.50 |       1.50        1.50 |       2.50        2.50
[C]  0.75 |       2.50        2.50 |       8.36        8.50
[C]  0.90 |       5.37        5.50 |      50.32       50.50
[C]  0.95 |       9.63       10.50 |     158.05      200.50
```

| α | 성공 조회 (측정) | Knuth ½(1+1/(1−α)) | 실패 조회 (측정) | Knuth ½(1+1/(1−α)²) |
|---|---|---|---|---|
| 0.10 | 1.06 | 1.06 | 1.12 | 1.12 |
| 0.25 | 1.16 | 1.17 | 1.39 | 1.39 |
| 0.50 | 1.50 | 1.50 | 2.50 | 2.50 |
| 0.75 | 2.50 | 2.50 | 8.36 | 8.50 |
| 0.90 | 5.37 | 5.50 | 50.32 | 50.50 |
| 0.95 | 9.63 | 10.50 | 158.05 | 200.50 |

```svg
<svg viewBox="0 0 720 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">평균 탐사 수 vs 부하율 (측정값)</text>
  <g stroke="currentColor" fill="none">
    <path d="M60 220 L680 220"/><path d="M60 220 L60 30"/>
    <path d="M56 160 L60 160"/><path d="M56 100 L60 100"/><path d="M56 40 L60 40"/>
  </g>
  <g fill="currentColor" text-anchor="end"><text x="52" y="224">0</text><text x="52" y="164">20</text><text x="52" y="104">40</text><text x="52" y="44">60</text></g>
  <g fill="currentColor" text-anchor="middle">
    <text x="120" y="238">0.10</text><text x="210" y="238">0.25</text><text x="360" y="238">0.50</text><text x="510" y="238">0.75</text><text x="600" y="238">0.90</text><text x="645" y="238">0.95</text>
    <text x="370" y="262">부하율 α</text>
  </g>
  <path d="M120 216.6 L210 215.8 L360 212.5 L510 194.9 L600 69" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M120 216.8 L210 216.5 L360 215.5 L510 212.5 L600 203.9 L630 191.1" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4" fill="none"/>
  <g class="f-warn" stroke="currentColor"><circle cx="510" cy="194.9" r="4"/><circle cx="600" cy="69" r="4"/></g>
  <path d="M600 69 L630 34" stroke="currentColor" fill="none" stroke-dasharray="2 3"/>
  <g fill="currentColor">
    <text x="470" y="64">실패 50.3</text>
    <text x="520" y="40">0.95 → 실패 158 (축 밖)</text>
    <text x="470" y="188">8.4</text>
    <rect x="90" y="50" width="250" height="56" rx="8" class="f-mut" stroke="currentColor"/>
    <text x="100" y="72">— 실선: 실패 조회 (없는 키)</text>
    <text x="100" y="94">- - 점선: 성공 조회 (있는 키)</text>
  </g>
</svg>
```

읽는 법:

- **α ≤ 0.5 에서는 체이닝과 차이가 없다**. 성공 1.5칸, 실패 2.5칸. 연속 배열이라 캐시 라인 하나에 슬롯 5개(12바이트 슬롯, 64B 라인)가 들어가므로 실제로는 체이닝보다 빠르다.
- **α = 0.75 → 0.90 에서 실패 조회가 8 → 50칸으로 6배** 뛴다. 원인은 1차 클러스터링(primary clustering): 클러스터 끝에 떨어진 키가 클러스터를 키우고, 큰 클러스터일수록 더 잘 맞아서 더 커진다 ("부자가 더 부자가 된다"). 실패 조회는 클러스터 끝(EMPTY)까지 가야 하므로 `1/(1−α)²` 로 폭발한다.
- **α = 0.95 의 측정값이 근사식보다 작은 이유**: Knuth 식은 cap→∞ 점근식이다. 4096칸 유한 표에서 이 영역은 클러스터 몇 개가 평균을 좌우해 분산이 매우 크다. 중요한 건 "이미 쓸 수 없는 영역"이라는 결론이다.
- **설계 결론**: 오픈 어드레싱은 α ≤ 0.7 정도로 잡는다. 키 최대 개수가 N이면 cap ≥ N/0.7 인 2의 거듭제곱 — 예: 연결 핸들 300개 → cap 512 (α=0.59). 그리고 **α 계산에 묘비도 넣는다** (`used + tombs`). 묘비는 실패 조회 입장에서 FULL과 같기 때문이다.

펌웨어 연결: 고정 배열이라 RAM이 링크 타임에 확정되고(`.bss` 48KB), 조회의 최악 시간은 `cap` 칸으로 **상한이 있다**. 하드 리얼타임 경로라면 평균이 아니라 이 최악값(또는 "최대 탐사 길이 ≤ K" 를 삽입 시 강제하는 정책)으로 WCET를 잡는다. SSD 펌웨어의 LPN→캐시 슬롯 매핑, 네트워크 MCU의 MAC 주소 테이블, BLE 연결 핸들 → 컨텍스트 표가 전형적인 사용처다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 31/31 passed
# 메모리 오류 확인(선택)
cc -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 삭제 시 `state = EMPTY` | 같은 클러스터 뒤쪽 키의 조회 경로가 끊긴다 (Part A 재현) | `state = TOMB; tombs++;` 또는 backward-shift (연습 2) |
| 조회에서 TOMB를 만나면 멈춤 | 묘비를 EMPTY처럼 다루면 naive delete와 같은 버그 | TOMB는 `continue` |
| 삽입에서 첫 TOMB에 즉시 넣기 | 같은 키가 묘비 뒤에 있으면 **중복 키** 발생 | `first_tomb` 기억 → 체인 끝까지 확인 후 넣기 |
| `while (state != EMPTY)` 무한 루프 | 꽉 찬 표나 묘비만 남은 표엔 EMPTY가 없다 | `for (n = 0; n < cap; n++)` 로 상한 |
| `i = (i + 1) % cap` 에 cap이 2의 거듭제곱 아님 | `& mask` 를 쓰면 틀리고, `%` 는 Cortex-M0에 나눗셈 명령이 없어 느림 | cap = 2^k, `i = (i + 1) & mask` |
| 부하율을 `used / cap` 으로만 계산 | 묘비가 EMPTY를 다 먹어도 재구성이 안 일어남 → 실패 조회 = cap칸 | `(used + tombs) / cap` 로 재구성 판단 |
| 정렬된 주소나 연속 ID를 `key & mask` 로 해싱 | 하위 비트가 같아 홈이 몇 곳에 몰림 (연습 3: 16.5칸) | Fibonacci 해싱 `(k * 2654435769u) >> (32 - bits)` |
| 키 0을 "빈칸" 표시로 사용 | 키 0을 저장할 수 없고, 묘비 표시도 따로 필요해짐 | 상태 바이트 분리 (또는 예약 키 2개를 명시) |

## 8. 면접 Q&A

<details>
<summary>Q1. 오픈 어드레싱에서 삭제한 슬롯을 그냥 비우면 안 되는 이유는?</summary>

조회는 "EMPTY를 만나면 키가 없다"고 판단한다. 삭제된 칸 뒤에 같은 클러스터의 다른 키가 있으면, 그 키의 조회가 빈칸에서 멈춰 실패한다. 데이터는 표에 남아 있는데 길이 끊긴 것이다. 해결은 ① tombstone ② backward-shift deletion(선형 탐사 한정) ③ 삭제가 드물면 주기적 재구성.

</details>

<details>
<summary>Q2. tombstone의 단점은? 어떻게 관리하나요?</summary>

묘비는 실패 조회 입장에서 FULL과 같아서 쌓일수록 실패 조회가 느려진다. 삽입/삭제를 반복하면 결국 EMPTY가 0개가 되어 실패 조회가 cap칸을 다 본다 — 연습 1에서 실측하면 1024칸 표가 2.31칸 → **1024칸**. 관리: `used + tombs` 가 임계(예: 3/4)를 넘으면 같은 크기로 재구성(rehash)하고, 삽입 시 첫 묘비를 재사용한다. 리얼타임 시스템에선 재구성을 유휴 시간에 하거나 점진적으로(몇 슬롯씩) 한다.

</details>

<details>
<summary>Q3. 체이닝과 오픈 어드레싱 중 MCU 펌웨어에 무엇을 쓰겠습니까?</summary>

키 수 상한이 있고 삭제가 드물면 오픈 어드레싱: 배열 하나, 포인터 없음, 캐시/프리페치 친화적, RAM 크기가 컴파일 타임에 결정. 단 α ≤ 0.7 을 지키고 묘비 관리가 필요. 삭제가 잦거나, α가 1 근처까지 갈 수 있거나, 항목이 크면(포인터 이동만으로 재배치하고 싶으면) 정적 노드 풀 기반 체이닝(DS-B10). 최악 시간을 엄격히 묶어야 하면 cuckoo 해싱(조회 최대 2칸)도 후보.

</details>

<details>
<summary>Q4. 부하율 0.5 와 0.9 에서 평균 탐사 수는 대략 얼마인가요? 왜 그렇게 차이가 나죠?</summary>

Knuth 근사: 성공 `½(1+1/(1−α))`, 실패 `½(1+1/(1−α)²)`. 0.5에서 1.5 / 2.5, 0.9에서 5.5 / 50.5 — 직접 잰 값도 1.50/2.50, 5.37/50.32 로 거의 같았다. 1차 클러스터링 때문이다: 클러스터에 맞은 키는 클러스터 끝에 붙어 클러스터를 키우고, 큰 클러스터는 더 많은 홈을 덮어 더 잘 맞는다. 실패 조회는 클러스터 끝까지 가야 하므로 제곱으로 나빠진다.

</details>

<details>
<summary>Q5. 선형 탐사 말고 이차 탐사(quadratic probing)나 이중 해싱을 쓰면?</summary>

1차 클러스터링이 줄어든다. 이차 탐사는 2^k 표에서 `h + n(n+1)/2` (삼각수)를 쓰면 모든 칸을 한 번씩 방문함이 보장된다 (`h + n²` 은 보장 안 됨 — 연습 4). 이중 해싱은 보폭을 두 번째 해시로 정해 2차 클러스터링까지 줄이지만, 보폭이 cap과 서로소여야 한다(2^k 표면 홀수 보폭). 대가는 캐시 지역성 손실과 backward-shift 삭제를 못 쓴다는 점. 실무(Swiss table, Robin Hood)는 선형 탐사 + 메타데이터 묶음 검사로 지역성을 살리는 쪽이 많다.

</details>

<details>
<summary>Q6. 이 테이블을 ISR과 메인 루프가 같이 쓴다면?</summary>

삽입은 키·값 쓰기와 상태 바이트 쓰기 사이에 중간 상태가 있다. 한쪽만 쓰고(예: ISR은 조회만) 다른 쪽이 갱신하는 구조라면 **키와 값을 먼저 쓰고 상태를 마지막에 FULL로** (필요하면 `__DMB()` 배리어) 바꾸면 조회 측이 반쯤 쓴 항목을 보지 않는다. 양쪽이 모두 갱신하면 임계 구역(PRIMASK)으로 묶는데, 탐사 길이가 cap까지 갈 수 있어 인터럽트 지연 상한이 커진다 — 이것도 α를 낮게 유지해야 하는 이유다. 재구성은 절대 ISR 안에서 하지 않는다.

</details>

## 9. 추가 연습

### 연습 1 — churn 후 묘비 누적과 재구성

cap 1024 표에 무작위 키 512개(α=0.5)를 넣고, "아무 키 하나 지우고 새 키 하나 넣기"를 20000번 반복한다. 실패 조회 평균 탐사 수를 처음 / churn 후 / 재구성 후 측정하라. `ht_rebuild()` (살아 있는 항목만 정적 scratch 배열로 옮겼다 다시 넣기)와 정책 `(used + tombs) * 4 > cap * 3` 을 구현하라.

<details>
<summary>답 보기</summary>

실제 측정:

```text
[1] miss probes: fresh 2.31 | after churn 1024.00 (tombs=512) | after rebuild 2.57 (tombs=0)
```

α는 계속 0.5인데 churn 후에는 **비어 있던 512칸이 전부 묘비**가 되어 EMPTY가 0개 → 실패 조회가 표 전체(1024칸)를 돈다. 새 키가 묘비가 아닌 EMPTY에 떨어질 때마다 EMPTY가 하나씩 줄고, 삭제는 EMPTY를 만들지 않기 때문이다. 재구성 후 2.57칸(무작위 키 배치에 따라 2.31과 약간 다름)으로 회복. 재구성용 scratch는 malloc 대신 `static slot_t g_scratch[CAP_MAX]` — RAM이 아깝다면 표 안에서 제자리 재배치하는 알고리즘도 있지만 훨씬 복잡하다.

</details>

### 연습 2 — tombstone 없는 삭제: backward-shift

선형 탐사에서는 삭제한 빈칸 뒤의 항목을 당겨 와서 묘비 없이 불변식을 지킬 수 있다. 빈칸 `i` 뒤의 `j` 에 있는 항목(홈 `h`)을 `i` 로 옮겨도 되는 조건은?

<details>
<summary>답 보기</summary>

`h` 가 순환 구간 `(i, j]` **밖**에 있을 때만 옮긴다. `h` 가 그 안에 있으면 그 항목의 경로는 `h..j` 라 빈칸 `i` 를 지나지 않으므로 그대로 둔다. 판정은 `((j - h) & mask) < ((j - i) & mask)` 면 "안". 옮겼으면 `i = j` 로 빈칸을 이동하고, EMPTY를 만날 때까지 반복한 뒤 마지막 `i` 를 EMPTY로. 5/21/37에서 21을 지우면 37이 슬롯 6으로 당겨져 조회가 2칸이 된다. 랩어라운드 예(15→15, 31→0, 1→1)에서 15를 지우면 31은 15로 오지만 1은 자기 홈이라 남는다. 묘비가 영원히 0개라 재구성이 필요 없다 — Robin Hood 해싱 구현들이 이 방식을 쓴다.

</details>

### 연습 3 — 정렬된 주소 키와 Fibonacci 해싱

DMA 버퍼 주소 `0x20000000 + i*64` (i=0..511)를 cap 1024 표에 넣고 평균 성공 탐사 수를 `key & mask` 와 Fibonacci 해싱으로 비교하라.

<details>
<summary>답 보기</summary>

```text
[3] 64B-aligned keys, alpha 0.5: avg hit probes  mask 16.50 | fibonacci 1.00
```

64바이트 정렬이라 하위 6비트가 항상 0 → `key & 1023` 의 서로 다른 값은 16개뿐. 홈 16곳에 32개씩 몰려 32칸 클러스터 16개가 되고 평균은 (1+32)/2 = 16.5칸. Fibonacci 해싱 `(key * 2654435769u) >> (32 - bits)` 는 곱셈으로 모든 비트를 섞은 뒤 **상위** 비트를 쓴다. 등차수열 키에 특히 강해서(세 거리 정리, three-distance theorem) 512개가 전부 다른 홈에 들어가 1.00칸. 곱셈 한 번이라 Cortex-M3/M4에서 1사이클(`MUL`) — 펌웨어 해시로 아주 싸다.

</details>

### 연습 4 — 삼각수 탐사가 모든 칸을 도는가

2^k 표에서 `home + n(n+1)/2` 와 `home + n²` 가 처음 cap번 안에 모든 칸을 방문하는지 검사하라.

<details>
<summary>답 보기</summary>

삼각수 오프셋은 mod 2^k 에서 0..cap−1 이 모두 다르다 (`n(n+1)/2 ≡ m(m+1)/2` 이면 `(n−m)(n+m+1) ≡ 0 mod 2^(k+1)` 인데 두 인수 중 하나는 홀수라 불가능). 그래서 16/64/1024 전부 통과. `n²` 은 제곱 잉여(quadratic residue)만 나오므로 절반도 못 덮는다 → 표에 빈칸이 있어도 삽입 실패가 날 수 있다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 13/13 passed
```

## 10. 핵심 정리

- 선형 탐사: `i = (i + 1) & mask`, **최대 cap칸**. 조회는 EMPTY에서 멈추고 TOMB는 지나간다.
- 불변식: FULL 키의 홈 → 자기 자리 사이에 EMPTY 없음. 삭제를 EMPTY로 하면 이게 깨져 **다른 키**를 못 찾는다.
- 삽입: 같은 키 확인을 위해 **체인 끝까지** 간 뒤, 처음 본 묘비(없으면 EMPTY)에 넣는다.
- 묘비는 실패 조회 입장에서 FULL — `(used + tombs)` 로 재구성 시점을 정한다. 안 하면 실패 조회 = cap칸.
- 실측: α=0.5 → 성공 1.50 / 실패 2.50, α=0.9 → 5.37 / 50.32. 설계는 α ≤ 0.7.
- 하위 비트가 고르지 않은 키(정렬 주소, 연속 ID)는 Fibonacci 해싱으로 섞는다.
- 선형 탐사는 backward-shift 로 묘비 없이 삭제할 수도 있다.

## 참고자료

- Donald Knuth, *The Art of Computer Programming Vol. 3*, 6.4 Hashing — 선형 탐사 분석과 `½(1+1/(1−α)²)` 식, Algorithm R(삭제)
- CLRS *Introduction to Algorithms*, 11.4 Open addressing
- [Malte Skarupke — Fibonacci Hashing: The Optimization that the World Forgot](https://probablydance.com/2018/06/16/fibonacci-hashing-the-optimization-that-the-world-forgot-or-a-better-alternative-to-integer-modulo/)
- [Abseil — Swiss Tables design notes](https://abseil.io/about/design/swisstables)
- [Emmanuel Goossaert — Robin Hood hashing: backward shift deletion](https://codecapsule.com/2013/11/17/robin-hood-hashing-backward-shift-deletion/)
- Linux kernel `include/linux/hashtable.h`, `include/linux/hash.h` (`hash_32` 는 Fibonacci 곱셈 해시)
