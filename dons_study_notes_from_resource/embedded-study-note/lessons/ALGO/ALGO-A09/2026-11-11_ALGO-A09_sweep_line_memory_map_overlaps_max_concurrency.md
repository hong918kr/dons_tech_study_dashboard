---
id: ALGO-A09
time: 110분
updated: 2026-11-11
---
# ALGO-A09 스위프 라인: 메모리 영역 겹침과 최대 동시 이벤트

## 학습 목표

- 구간의 시작·끝을 **이벤트**(event)로 바꿔 정렬하고, 왼쪽에서 오른쪽으로 훑으며 **활성 집합**(active set)을 유지하는 스위프 라인(sweep line)을 세 문제에 같은 뼈대로 쓴다.
- 메모리 맵 영역 `[base, base+size)` 의 **겹치는 쌍 전부**를 출력 민감(output-sensitive) O(n log n + k) 로 찾고, 부분 겹침 / 포함 / 동일로 분류한다. 동점 규칙 "같은 좌표면 END 먼저" 가 왜 반개구간에서 정확히 맞는지 증명한다.
- 끝 좌표의 32비트 넘침(0xFFFFF000 + 0x1000 = 0), 크기 0 영역, 맞닿음 같은 경계 사례를 코드와 테스트로 막고, 그 실수가 실제로 어떻게 터지는지(무한 루프 · ASan 경계 밖 접근) 본다.
- ISR 구간 로그에서 **최대 동시 활성 수(또는 스택 바이트 합)** 와 그 값이 처음 나타나는 구간 `[from, to)` 를 구하고, "같은 시각 사건을 묶어서 본다" 가 왜 창(window) 계산에 필요한지 설명한다. 관측 최댓값과 우선순위 기반 정적 상한의 차이를 말한다.
- 직사각형 합집합 넓이를 슬랩법 O(n² log n) 과 세그먼트 트리 O(n log n) 로 구하고, 펌웨어(LCD 부분 갱신)에서는 왜 슬랩법이 오히려 맞는 선택일 수 있는지 판단한다.

## 1. 문제

스위프 라인은 "정렬 + 한 번 훑기 + 지금 걸쳐 있는 것들의 집합" 이라는 단순한 틀인데, 면접에서는 **경계**를 얼마나 정확히 다루는지로 시니어를 가른다. 펌웨어 쪽 단골 문제는 링커 맵·파티션 표·MPU 표의 겹침 검사, ISR/DMA 로그의 동시성 최댓값, 화면 부분 갱신 영역이다.

ALGO-M09 가 이미 다룬 것: 반개구간 `[s, e)` 규약, 최대 겹침 깊이 = 최소 채널 수(하한 증명), 시작·끝 배열을 따로 정렬하는 두 포인터 스위프. 이 레슨은 그 위에서 (1) 깊이 하나가 아니라 **겹치는 쌍 전부**를 내놓는 출력 민감 스위프, (2) 최댓값의 **구간**과 가중치(스택 바이트), (3) 2차원(직사각형 합집합)으로 올라간다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">메모리 맵 스위프: 주소 축을 왼쪽→오른쪽으로 훑으며 START 마다 활성 집합 전부와 쌍을 만든다</text>
  <g stroke="currentColor" stroke-width="1">
    <rect x="60" y="45" width="640" height="22" class="f-mut"/>
    <rect x="60" y="75" width="200" height="22" class="f-acc"/>
    <rect x="220" y="105" width="80" height="22" class="f-warn"/>
    <rect x="300" y="135" width="240" height="22" class="f-acc"/>
    <rect x="540" y="165" width="160" height="22" class="f-acc"/>
    <rect x="460" y="195" width="120" height="22" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="66" y="61">flash [0,64)</text><text x="66" y="91">boot [0,20)</text>
    <text x="224" y="121">cfg</text><text x="306" y="151">app [24,48)</text>
    <text x="546" y="181">log [48,64)</text><text x="466" y="211">nvm</text>
  </g>
  <line x1="460" y1="38" x2="460" y2="240" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4"/>
  <text x="380" y="258" fill="currentColor">스위프 x=40: START(nvm)</text>
  <g stroke="currentColor"><line x1="60" y1="270" x2="700" y2="270"/></g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle" font-size="12">
    <text x="60" y="286">0</text><text x="220" y="286">16</text><text x="260" y="286">20</text><text x="300" y="286">24</text>
    <text x="460" y="286">40</text><text x="540" y="286">48</text><text x="580" y="286">52</text><text x="700" y="286">64</text>
  </g>
  <text x="10" y="308" fill="currentColor">활성 {flash, app} → (flash,nvm) 포함, (app,nvm) 부분 겹침. cfg|app, app|log = 맞닿음</text>
  <text x="10" y="325" fill="currentColor">회색 = 바깥 메모리, 파랑 = 정상 섹션, 경고색 = 다른 섹션과 부분 겹침</text>
</svg>
```

### Part A — 메모리 맵의 겹치는 쌍 전부

- `ov_classify(x, y, &outer)` — 겹치는 두 영역을 **EQUAL**(완전히 같음) / **CONTAIN**(한쪽이 다른 쪽을 포함, `outer` 가 바깥쪽) / **PARTIAL** 로 분류.
- `mm_overlaps(r, n, out, cap)` — 겹치는 쌍의 **총수 k** 를 돌려주고 앞의 `min(k, cap)` 개를 쓴다(`snprintf` 와 같은 계약). 크기 0 영역은 빈 집합이라 무시. O(n log n + k), malloc 없음.
- `pairs_canon()`(주어짐) — 쌍을 (a, b) 오름차순으로: 키가 0..n−1 이라 계수 정렬 두 번, O(n + k).
- 테스트: 맞닿음, 크기 0, 3단 중첩, 주소 공간 꼭대기(`0xFFFFF000 + 0x1000 = 2^32`), 2^32 를 넘는 영역, 동일 영역, 무작위 맵 3000개를 O(n²) 기준과 **분류까지** 대조, `cap` 이 작을 때 총수, k = n(n−1)/2 (2000개 동일 영역).

### Part B — 최대 동시 이벤트와 직사각형 합집합

- `isr_peak(iv, n, w, &out)` — ISR 구간 `[t0, t1)` 들에서 `peak = max_t Σ w_i`(w = NULL 이면 개수), 그 값이 **처음 나타나는 극대 구간 `[from, to)`**, peak 구간의 수 `nwin`, peak 상태 총시간 `at_peak`.
- `rect_union_slab(r, n, &pieces)` — x 좌표 사이 띠(slab)마다 y 구간을 정렬·병합. O(n² log n). `pieces` = 합집합을 서로소 직사각형으로 나눈 개수.
- `st_update` + `rect_union_segtree(r, n)` — x 스위프 + 좌표 압축된 y 위의 세그먼트 트리(덮인 횟수 cnt, 덮인 길이 len). O(n log n).
- 테스트: 손 계산(맞닿은 사슬, 창 2개, 길이 0, 가중치 최대 ≠ 개수 최대, `UINT32_MAX` 근처), 무작위 ISR 로그 4000개를 정수 격자 전수와 5개 값 모두 대조, 사각형 1500개 격자 전수 + 큰 좌표 300개 두 방법 교차, 넓이 2^62.

### Part C — 펌웨어 시나리오

- **링커 메모리 맵 검사** (`mm_report`, 주어짐): 물리 메모리(MEM) 18개 영역 표에서 섹션끼리 겹침, MEM 경계 걸침(straddle), 어느 MEM 에도 안 든 섹션(orphan), 2^32 를 넘는 영역, 빈 영역.
- **MPU 영역 표**: PMSAv7(겹침 허용, 번호 큰 영역이 이김, 크기 2^k·정렬) vs PMSAv8(겹침 금지, 32 B 단위).
- **중첩 ISR 트레이스**: 최대 중첩 깊이와 최대 ISR 스택 바이트가 **다른 시각**에 나온다. 우선순위 기반 정적 상한과 비교.
- **LCD 더티 사각형**: 240×320 RGB565 에서 사각형별 전송 · 외접 사각형 · 정확한 합집합(슬랩 조각 = LCD 창 명령 수) 바이트 비교.

## 2. Problem (English)

```c
typedef struct { const char *name; uint32_t base, size; uint8_t kind; } region_t;   /* [base, base+size) */
typedef enum { OV_PARTIAL, OV_CONTAIN, OV_EQUAL } ov_kind_t;
typedef struct { uint16_t a, b; int8_t outer; uint8_t kind; } ovpair_t;           /* a < b */

ov_kind_t ov_classify(const region_t *x, const region_t *y, int *outer);
size_t    mm_overlaps(const region_t *r, size_t n, ovpair_t *out, size_t cap);    /* returns k (total) */

typedef struct { uint32_t t0, t1; uint8_t irq; } isr_iv_t;                         /* [t0, t1) ticks */
typedef struct { uint64_t peak; uint32_t from, to, nwin; uint64_t at_peak; } peak_t;
int       isr_peak(const isr_iv_t *iv, size_t n, const uint32_t *w, peak_t *out);

typedef struct { int32_t x1, y1, x2, y2; } rect_t;                                 /* [x1,x2) x [y1,y2) */
uint64_t  rect_union_slab(const rect_t *r, size_t n, size_t *npieces);
uint64_t  rect_union_segtree(const rect_t *r, size_t n);
```

1. Given a firmware memory map of `n ≤ 4096` named regions, report **every** pair of regions that share at least one byte, classified as partial / containment / equal. Touching regions (`[0,10)` and `[10,20)`) do not overlap; zero-size regions overlap nothing; `base + size` may reach or exceed `2^32`. Target O(n log n + k) time and O(n) extra memory, no dynamic allocation. If the output buffer is too small, still return the exact total.
2. Given an interrupt log of `[entry, exit)` intervals (possibly weighted by stack bytes), return the maximum number (or weight) of simultaneously active ISRs, and the first maximal time interval where that maximum holds.
3. Given `n` axis-aligned rectangles with integer coordinates in `[-2^30, 2^30]`, return the area of their union.

Example (1): `flash[0,64) boot[0,20) cfg[16,24) app[24,48) log[48,64) nvm[40,52)` → 8 pairs: `(flash,boot)`, `(flash,cfg)`, `(flash,app)`, `(flash,log)`, `(flash,nvm)` CONTAIN; `(boot,cfg)`, `(app,nvm)`, `(log,nvm)` PARTIAL.
Example (2): `[0,10) [5,15) [10,20) [12,14)` → peak 3 on `[12,14)`. `[0,5) [5,10) [10,15)` → peak 1 on `[0,15)` (one window, not three).
Example (3): `[[0,0,2,2],[1,0,2,3],[1,0,3,1]]` → 6 (LeetCode 850 Rectangle Area II, Example 1).

Follow-ups: what if `k` is Θ(n²)? What if regions arrive one at a time? Is the observed max ISR stack a safe stack size? Which union algorithm would you put on the MCU itself?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여섯 군데(`ov_classify`, `mm_overlaps`, `isr_peak`, `rect_union_slab`, `st_update`, `rect_union_segtree`). 정렬(`sort_u64`), 이벤트 키 함수, 좌표 압축, 쌍 정렬, 보고서·MPU 검사기, 기준 구현은 주어진다. 스텁은 "겹침 없음 · 0" 을 돌려줘서 멈추거나 죽지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:346  ov_classify(&big, &in, &o1) == OV_CONTAIN && o1 == 0
…
random maps: 3000 maps, 281819 overlapping pairs total, mismatches=2856
FAIL starter.c:395  bad == 0 && total > 10000
…
random ISR logs: 4000 cases (1057 with >1 peak window), mismatches=3843
…
  summary: pairs=0 (expected containment 0) overlap=0 mem-overlap=0 straddle=0 orphan=13 oob=1 empty=1
…
10/44 passed
```

(0.04 초, exit 1. 통과한 10개는 "없음 / 0" 이 정답인 사례들이다 — 맞닿음, 크기 0, 길이 0 ISR, 빈 입력, `n > MAX` 한계, PARTIAL 분류 하나, 큰 좌표에서 두 스텁이 똑같이 0 을 내는 교차 비교. 그래서 테스트는 "없음" 사례만으로 구성하면 안 된다.)

## 4. 힌트

<details><summary>힌트 1 — 이벤트 하나를 64비트 정수 하나로</summary>

`key = (좌표 << 17) | (종류 << 16) | id`, END = 0, START = 1. 좌표는 2^33 미만(끝 = base + size 는 33비트), id 는 16비트. 정수 비교 한 번이 (좌표, END<START, id) 사전순이 되므로 비교 함수도, 동점 처리 분기도 필요 없다.

</details>

<details><summary>힌트 2 — 활성 집합은 배열 + 위치표</summary>

`act[0..na)` 에 활성 id, `pos[id]` 에 그 칸 번호. 넣기 = 맨 뒤에 추가, 빼기 = 맨 뒤 원소를 빈칸으로 옮기기(swap-remove). 둘 다 O(1). START 이벤트에서 `act` 전체를 훑으면 훑는 원소 하나하나가 **출력할 쌍**이다 — 헛일이 없어서 O(n log n + k).

</details>

<details><summary>힌트 3 — 크기 0 과 끝 좌표</summary>

크기 0 영역은 이벤트를 아예 만들지 않는다. 만들면 END 키가 같은 좌표의 자기 START 보다 **먼저** 정렬돼서, 아직 넣지도 않은 id 를 빼게 된다. 끝은 `(uint64_t)base + size`.

</details>

<details><summary>힌트 4 — ISR: 같은 시각의 사건은 한 묶음</summary>

시각 t 의 사건을 전부 적용한 다음에만 값을 본다. 그 값이 `[t, 다음 사건 시각)` 동안의 활성 합이다. 값이 best 보다 커지면 새 최댓값(창 다시 시작), best 에서 떨어지면 창 닫기, 다시 best 가 되면 새 창.

</details>

<details><summary>힌트 5 — 세그먼트 트리는 내려보내지 않는다</summary>

노드에 `cnt`(이 노드 구간 **전체**를 덮는 사각형 수)만 더하고 빼며, 자식으로 내려보내지 않는다. `len = cnt > 0 ? 노드 길이 : (잎이면 0, 아니면 두 자식 len 합)`. 넓이는 `루트 len × (x − 직전 x)` 를 **갱신 전에** 더한다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 확인 질문 | 끝은 포함인가(반개 `[base, base+size)`)? 맞닿음은 겹침인가? 크기 0 은? `base+size` 가 2^32 를 넘을 수 있나? 쌍 전부인가 개수만인가? 출력 순서? n, k 의 크기? |
| 2. 무식한 방법 | 모든 쌍 O(n²) — 이게 **테스트 기준**이 된다. ISR 은 정수 시각마다 세기 O(n·T), 사각형은 격자 칸 칠하기 |
| 3. 더 나은 생각 + 정당성 | 이벤트 정렬 + 활성 집합. 동점 END 먼저 ⇒ START 가 보는 활성 집합 = 정확히 자기와 겹치는 먼저 시작한 영역들 (아래 보조정리) |
| 4. 코드 | 64비트 키 정렬, swap-remove 활성 집합, uint64 끝, 크기 0 거르기 |
| 5. 소리 내어 말할 테스트 | 맞닿음, 크기 0, 동일 영역, 3단 포함, 0xFFFFF000+0x1000, 2^32 넘음, n=0/1, k 가 n² 급, cap 부족 |
| 6. 복잡도 | 시간 O(n log n + k), 추가 메모리 O(n) (이벤트 2n + 활성 n), 재귀 없음, 출력 O(k) 는 호출자 몫 |

### Part A — 이벤트 순서 하나가 정확성의 전부다

각 영역 i 를 `START(base_i)`, `END(end_i)` 두 이벤트로 만들고 키 `(좌표, 종류, id)` 로 정렬한다. START 를 만나면 지금 활성인 영역 전부와 쌍을 만들고 자기를 넣는다. END 를 만나면 뺀다.

**보조정리.** 키 순서가 (좌표 오름차순, 같은 좌표면 END 가 START 보다 먼저, 그다음 id) 일 때, 영역 i 의 START 를 처리하는 순간 활성 집합은 정확히
`{ j : START_j 의 키 < START_i 의 키, 그리고 end_j > base_i }` 이다.

증명. j 가 활성이려면 START_j 는 이미 처리됐고(키가 더 작고) END_j 는 아직이어야 한다. END_j 의 키 > START_i 의 키 ⇔ `end_j > base_i`, 또는 `end_j == base_i` 이면서 END 가 START 뒤 — 그런데 같은 좌표에서는 END 가 항상 먼저이므로 후자는 불가능. 따라서 활성 ⇔ `end_j > base_i`. ∎

**정리.** 겹치는 쌍 {i, j} 은 정확히 한 번, START 키가 더 큰 쪽의 START 에서 보고된다. 보고되는 쌍은 모두 진짜 겹침이다.

증명. START_j 키 < START_i 키라 하자(그러면 `base_j ≤ base_i`). 두 반개구간이 겹침 ⇔ `base_j < end_i ∧ base_i < end_j`. 크기가 양수라 `base_j ≤ base_i < end_i` 이므로 앞 조건은 자동, 겹침 ⇔ `end_j > base_i` ⇔ (보조정리) START_i 에서 j 가 활성. START_j 에서는 i 가 아직 안 들어왔으니 두 번 보고되지 않는다. ∎

END 를 START 보다 **뒤에** 두면 보조정리의 마지막 단계가 깨져서 `end_j == base_i`(맞닿음)가 보고된다. 실제로 그렇게 바꾼 변형은 Part C 맵에서 `app_a|app_b`, `app_b|log` 같은 맞닿음을 겹침으로 보고해 쌍이 15 → 19 개가 된다(7. 실수 1). **닫힌구간** `[first, last]` 로 표현했다면 반대로 START 먼저가 맞다 — 동점 규칙은 구간 규약에서 유도하는 것이지 외우는 게 아니다(M09 Q2 와 같은 이야기).

1장 그림 예제의 실제 실행 (스위프 순서로 나온 쌍, 이 레슨 작업용 trace 프로그램 출력과 같은 순서):

| 이벤트 (좌표, 종류, 영역) | 처리 전 활성 | 보고 | 처리 후 활성 |
|---|---|---|---|
| 0 START flash | {} | — | {flash} |
| 0 START boot | {flash} | (flash,boot) CONTAIN | {flash, boot} |
| 16 START cfg | {flash, boot} | (flash,cfg) CONTAIN, (boot,cfg) PARTIAL | {flash, boot, cfg} |
| 20 END boot | | — | {flash, cfg} (cfg 가 boot 자리로) |
| 24 END cfg | | — | {flash} |
| 24 START app | {flash} | (flash,app) CONTAIN — cfg 는 이미 빠짐 | {flash, app} |
| 40 START nvm | {flash, app} | (flash,nvm) CONTAIN, (app,nvm) PARTIAL | {flash, app, nvm} |
| 48 END app | | — | {flash, nvm} |
| 48 START log | {flash, nvm} | (flash,log) CONTAIN, (log,nvm) PARTIAL | {flash, nvm, log} |
| 52 END nvm / 64 END flash, log | | — | {} |

시각 24 와 48 의 두 줄이 동점 규칙이 일하는 곳이다.

### Part A — 왜 O(n log n + k) 인가, 그리고 그게 최선인 범위

정렬 O(n log n). 훑기에서 END 는 O(1), START 는 O(1 + |활성|) 인데 **활성 원소 하나 = 보고 쌍 하나**이므로 START 들의 합은 O(n + k). 출력 자체가 k 이므로 k 항은 피할 수 없다. 비교 기반 모형에서 Ω(n log n) 하한도 알려져 있다(원소 유일성 문제에서 환원 — 교과서 수준 결과로 알고 있음, 확인 권장).

출력 민감이라는 말의 뜻을 숫자로 보자 (`demo_bench.c`, 아래 벤치마크): 성긴 맵(k ≈ n)에서 n = 16000 이면 스위프 1.2 ms vs 모든 쌍 90–133 ms(78–110배). 하지만 2000개 동일 영역처럼 **k = n(n−1)/2** 면 스위프도 Θ(n²) 이고 모든 쌍보다 1.8–2.7배 빠를 뿐이다. 그래서 면접 꼬리질문 "k 가 n² 이면?" 의 답은 "쌍을 다 내놓는 한 n² 이 하한. 개수만 필요하면 START 에서 `k += 활성 수` 로 O(n log n)" (연습 1).

다른 구현과 비교:

| 방법 | 시간 | 메모리 | 메모 |
|---|---|---|---|
| 모든 쌍 | Θ(n²) | O(1) | 기준 구현. n ≤ 수십이면 펌웨어에서 이게 정답일 수도 |
| base 로 정렬 후 i 마다 `base_j < end_i` 인 동안 j 를 훑기 | O(n log n + k) | O(n) | 이것도 출력 민감 — 훑는 j 는 마지막 하나 빼고 전부 겹침. 단, 크기 0 인 j 를 건너뛰어도 계속 훑어야 함에 주의 |
| 이벤트 + 활성 집합 (이 레슨) | O(n log n + k) | O(n) | 같은 뼈대가 ISR·사각형으로 그대로 확장 |
| 구간 트리(interval tree) 에 하나씩 삽입하며 질의 | O(n log n + k) | O(n) 노드 | **온라인**(영역이 하나씩 등록될 때) — Q4 |

활성 집합에서 순서를 유지하려고 `memmove` 로 지우면 END 하나가 O(n) 이 되어 최악 O(n²) 이다. 순서는 필요 없다 — 정규 순서는 마지막에 `pairs_canon` 의 계수 정렬 두 번(O(n + k))으로 만든다. 정렬된 출력을 원한다고 비교 정렬 O(k log k) 를 쓰면 출력 민감성이 깨진다.

### Part A — 분류와 끝 좌표

겹친다는 전제에서 `ov_classify` 는 세 비교면 된다: 양 끝이 같으면 EQUAL, x 가 y 를 감싸면 CONTAIN(outer 0), 반대면 CONTAIN(outer 1), 나머지 PARTIAL. **EQUAL 검사가 먼저**여야 한다 — 같은 영역은 "x 가 y 를 감싼다" 도 참이라 CONTAIN 으로 새어 나간다(7. 실수 5). 테스트 쪽 기준 구현은 일부러 다른 공식(교집합 `[max base, min end)` 이 어느 쪽과 같은가)을 써서 같은 버그를 공유하지 않게 했다.

**끝 좌표는 33비트다.** `base, size ≤ 2^32 − 1` 이면 `base + size ≤ 2^33 − 2`. Cortex-M 의 시스템 영역 `0xE0000000 + 0x20000000` 은 정확히 2^32 이고, uint32 로 계산하면 0 이 된다. 그러면 END 키(좌표 0)가 START 보다 먼저 정렬되어 빈 활성 집합에서 빼기를 시도한다 — `na` 가 0 아래로 내려가 `size_t` 가 감싸고, `-O2` 빌드는 20초 타임아웃까지 끝나지 않았고 ASan 빌드는 `g_act[--na]` 에서 global-buffer-overflow 로 멈췄다(7. 실수 3, 실제 실행). 해결은 둘 중 하나다.

- `uint64_t` 끝 (이 레슨). 모든 입력에서 맞다.
- 끝 포함 `last = base + size − 1` (uint32). `size != 0` 이고 `base + size ≤ 2^32` 일 때만 맞다 — 즉 "2^32 를 넘는 영역" 을 먼저 따로 걸러야 한다. 이 레슨 작업 중 두 판정을 무작위 1878만 쌍에서 대조해 불일치 0 을 확인했다(그 조건 안에서).

Cortex-M4 에서 비용 차이는 작다. `demo_target.c` 의 실제 출력 (`clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -`, 지시어 줄 생략, 크기 0 검사 뒤 본체만):

```text
ov_u64 (본체):                         ov_last (본체):
  push    {r4, lr}                       add     r3, r2
  adds.w  r12, r1, r0                    add     r1, r0
  mov.w   r1, #0                         subs    r3, #1
  adc     lr, r1, #0                     subs    r1, #1
  adds    r3, r3, r2                     cmp     r0, r3
  adc     r4, r1, #0                     mov.w   r0, #0
  subs    r0, r0, r3                     mov.w   r3, #0
  sbcs.w  r0, r1, r4                     it      ls
  mov.w   r0, #0                         movls   r3, #1
  it      lo                             cmp     r2, r1
  movlo   r0, #1                         it      ls
  subs.w  r2, r2, r12                    movls   r0, #1
  sbcs.w  r2, r1, lr                     and.w   r12, r0, r3
  it      lo                             mov     r0, r12
  movlo   r1, #1                         bx      lr
  and.w   r12, r0, r1
  pop.w   {r4, lr}
  mov     r0, r12
  bx      lr
```

64비트 끝은 `adds`/`adc` 로 33번째 비트(캐리)를 만들고 `subs`/`sbcs` 로 64비트 비교를 한다 — 레지스터가 하나 모자라 `push {r4, lr}` 까지. 19 대 15 명령. 런타임 검사(예: DMA 디스크립터가 허용 창 안인지 매번 확인)라면 끝 포함 방식 + "넘는 영역은 등록 시점에 거부" 가 깔끔하고, 빌드 타임 호스트 도구라면 uint64 가 실수할 여지가 없다. `ov_wrap`(uint32 끝)은 명령이 가장 적지만 틀린다.

### Part B — ISR 로그: 값은 "묶음" 뒤에만 본다

`v(t) = Σ_{i : t0_i ≤ t < t1_i} w_i` 는 사건 시각에서만 값이 바뀌는 오른쪽 연속 계단 함수다. 시각 t 의 사건을 **전부** 적용하고 나면

`cur = Σ_{t0_i ≤ t} w_i − Σ_{t1_i ≤ t} w_i = v(t)`  (각 i 는 `t0_i < t1_i` 라 END 가 START 보다 먼저 빠지는 일이 없다)

이고 이 값은 `[t, 다음 사건 시각)` 동안 그대로다. 그러니 max_t v(t) = 묶음 값들의 최댓값이고, peak 구간 = peak 값을 갖는 **연속된 묶음들의 극대 연속 구간**이다. 묶음 안의 처리 순서는 값에 영향이 없다 — 이 방식에서는 동점 규칙조차 필요 없다.

그럼 M09 의 "END 먼저 + 사건 하나마다 best 갱신" 은? **최댓값과 처음 시각에는 맞다.** END 먼저면 한 묶음 안의 중간값은 "직전 묶음 값에서 내려갔다가 v(t) 까지 단조 증가" 이므로 max(v(직전), v(t)) 를 넘지 않는다(가중치 ≥ 0 필요 — 음수 가중치면 이 논증이 깨진다). 하지만 **창 경계**에는 틀린다. `[0,5) [5,10) [10,15)` 에서 시각 5 의 중간값이 잠깐 0 이 되어 창이 셋으로 쪼개진다. 실제 깊이는 `[0,15)` 내내 1 인데. 사건별로 판정하는 변형은 무작위 4000개 중 **291개**를 틀렸다(7. 실수 2). 펌웨어 의미로 이건 **tail-chaining** 이다: ISR 하나가 끝나는 바로 그 틱에 다음 ISR 이 들어오면 "동시" 가 아니지만 "쉬는 틈" 도 아니다.

손 계산 (`[0,10) [5,15) [10,20) [12,14)`, 묶음 단위 — 최종 다섯 값은 하네스가 확인):

| t | 사건 | v(t) | best | 창 |
|---|---|---|---|---|
| 0 | +[0,10) | 1 | 1 | 시작 0 |
| 5 | +[5,15) | 2 | 2 | 새 최댓값, 시작 5 |
| 10 | −[0,10), +[10,20) | 2 | 2 | 묶음이라 끊기지 않음 |
| 12 | +[12,14) | **3** | 3 | 새 최댓값, from = 12 |
| 14 | −[12,14) | 2 | 3 | 닫힘, to = 14, at_peak = 2 |
| 15, 20 | … | 1, 0 | 3 | — |

결과 `peak=3 from=12 to=14 nwin=1 at_peak=2` (하네스가 확인). 최댓값은 언제나 어떤 **시작 시각**에서 처음 나온다 — 값은 START 에서만 오른다.

**경계.** `t1 ≤ t0` 인 구간(길이 0, 또는 로거가 거꾸로 찍은 것)은 활성인 순간이 없으므로 이벤트를 만들지 않는다. `t1 = 0xFFFFFFFF` 까지는 키(좌표 33비트)에 그대로 들어간다. 타이머가 감싸는 로그는 먼저 64비트로 풀어야 한다(연습 5: 168 MHz DWT CYCCNT 는 2^32 / 168·10^6 ≈ 25.6 초마다 감싼다).

**가중치.** w = 스택 바이트면 peak = 관측된 최대 ISR 스택 사용량. 개수 최대와 가중치 최대는 다른 시각에 나올 수 있다(테스트 `wv`: 개수는 [24,26) 에서 3, 가중치는 [0,10) 에서 100). Part C 트레이스가 그 실제 예다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Part C 트레이스: 최대 중첩 깊이(4)와 최대 ISR 스택(584 B)은 다른 시각이다</text>
  <g stroke="currentColor" stroke-width="1">
    <rect x="40" y="200" width="300" height="24" class="f-mut"/>
    <rect x="65" y="170" width="225" height="24" class="f-mut"/>
    <rect x="90" y="140" width="175" height="24" class="f-mut"/>
    <rect x="115" y="110" width="8" height="24" class="f-acc"/>
    <rect x="420" y="200" width="300" height="24" class="f-warn"/>
    <rect x="570" y="170" width="10" height="24" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="200" y="217">TIM2 72 B (prio 5)</text>
    <text x="170" y="187">EXTI0 48 B (3)</text>
    <text x="140" y="157">ADC 40 B (1)</text>
    <text x="130" y="100">SysTick 64 B (0)</text>
    <text x="440" y="217">I2C1_EV 520 B (prio 6)</text>
    <text x="590" y="187">SysTick 64 B (0)</text>
  </g>
  <rect x="112" y="60" width="14" height="168" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="567" y="60" width="16" height="168" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="20" y="56" fill="currentColor">[330,333): 깊이 4, 스택 224 B</text>
  <text x="470" y="56" fill="currentColor">[530,532): 깊이 2, 스택 584 B</text>
  <g stroke="currentColor"><line x1="40" y1="240" x2="340" y2="240"/><line x1="420" y1="240" x2="720" y2="240"/></g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="40" y="256">300</text><text x="115" y="256">330</text><text x="340" y="256">420 µs</text>
    <text x="420" y="256">500</text><text x="570" y="256">530</text><text x="720" y="256">560 µs</text>
  </g>
  <text x="10" y="282" fill="currentColor">층 = 중첩 단계(아래가 바깥). 괄호 = 선점 우선순위(작을수록 급함). 두 구간 배율 다름</text>
</svg>
```

**중첩 ISR 현실.** 단일 코어 Cortex-M 에서 "동시에 활성" 인 ISR 들은 서로를 선점해 쌓인 **한 줄의 사슬**이다. 그래서 (a) 로그 구간들은 적층(laminar: 서로소이거나 포함)이어야 하고 — 아니면 로그가 틀렸다(연습 3), (b) 깊이 ≤ 서로 다른 **선점 우선순위 레벨 수**다(NVIC 우선순위 비트가 4개면 최대 16 레벨, PRIGROUP 로 일부를 서브우선순위로 쓰면 더 적다). 같은 레벨끼리는 서로 선점하지 못하고 tail-chaining 으로 이어 붙는다. 그래서 정적 상한 = 레벨마다 가장 큰 ISR 스택의 합(`isr_static_bound`). 듀얼 코어(예: STM32H7 의 M7+M4)라면 코어별 로그를 따로 봐야 한다 — 다른 코어의 ISR 은 이 스택을 쓰지 않는다.

### Part B — 직사각형 합집합 (1차원으로 줄이기)

**슬랩법.** 서로 다른 x 좌표 x_0 < … < x_{m−1} 사이 띠 `[x_s, x_{s+1})` 안에서는 어떤 사각형이 띠를 덮는지가 변하지 않는다. 그러니 넓이 = Σ_s (띠 폭) × (그 띠를 덮는 사각형들의 y 구간 합집합 길이) — 2차원 문제가 띠마다 M09 의 **구간 병합**으로 줄어든다. 띠 O(n) 개 × (훑기 O(n) + 정렬 O(n log n)) = O(n² log n). 병합할 때 맞닿은 구간(`a == ce`)도 이어 붙이면, 병합된 (띠, y 구간) 하나하나가 서로소 직사각형이 된다 — `pieces`. 넓이는 맞닿음을 안 이어도 같지만 조각 수가 늘어난다(7. 실수 8).

**세그먼트 트리.** x 로 스위프하면서 "지금 x 에서 덮인 y 길이" 를 사건마다 다시 계산하지 않고 O(log n) 에 유지한다. 잎 = 압축된 기본 y 구간 `[ys[i], ys[i+1])`.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">LeetCode 850 예제 1: 띠별 덮인 길이 2·3·1 → 넓이 6. 오른쪽: x ∈ [1,2) 의 트리</text>
  <g stroke="currentColor" stroke-width="1">
    <rect x="40" y="130" width="120" height="120" class="f-mut"/>
    <rect x="100" y="190" width="120" height="60" class="f-warn"/>
    <rect x="100" y="70" width="60" height="180" fill="none" stroke-width="2.5"/>
  </g>
  <g stroke="currentColor" stroke-dasharray="3 3"><line x1="100" y1="50" x2="100" y2="262"/><line x1="160" y1="50" x2="160" y2="262"/><line x1="220" y1="50" x2="220" y2="262"/></g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="40" y="276">0</text><text x="100" y="276">1</text><text x="160" y="276">2</text><text x="220" y="276">3</text>
    <text x="70" y="44">len 2</text><text x="130" y="44">len 3</text><text x="190" y="44">len 1</text>
  </g>
  <g fill="currentColor" font-size="12"><text x="44" y="146">R0</text><text x="104" y="86">R1</text><text x="194" y="206">R2</text></g>
  <g stroke="currentColor" stroke-width="1.2" fill="none">
    <line x1="520" y1="88" x2="420" y2="150"/><line x1="520" y1="88" x2="610" y2="150"/>
    <line x1="610" y1="178" x2="560" y2="220"/><line x1="610" y1="178" x2="680" y2="220"/>
  </g>
  <g stroke="currentColor">
    <rect x="455" y="60" width="130" height="28" class="f-acc"/>
    <rect x="355" y="150" width="130" height="28" class="f-ok"/>
    <rect x="545" y="150" width="130" height="28" class="f-mut"/>
    <rect x="500" y="220" width="120" height="28" class="f-ok"/>
    <rect x="630" y="220" width="100" height="28" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="520" y="79">y[0,3) cnt1 len3</text>
    <text x="420" y="169">y[0,1) cnt2 len1</text>
    <text x="610" y="169">y[1,3) cnt0 len1</text>
    <text x="560" y="239">y[1,2) cnt1 len1</text>
    <text x="680" y="239">y[2,3) 0 0</text>
  </g>
  <text x="250" y="276" fill="currentColor" font-size="12">R1 y[0,3) → 루트. R0 y[0,2) → [0,1)+[1,2). R2 y[0,1) → [0,1).</text>
  <text x="250" y="292" fill="currentColor" font-size="12">루트 cnt&gt;0 이므로 len = 3 (자식은 안 본다)</text>
</svg>
```

불변식: 각 노드에서 `len` = "이 노드 구간 안에서, 이 노드와 그 **아래**에 등록된 사각형들이 덮는 길이". `cnt > 0` 이면 노드 구간 전체가 덮였고, 아니면 덮인 부분 = 두 자식이 덮은 부분의 합(자식 구간은 서로소). 루트에는 위가 없으니 루트 `len` = 지금 활성인 모든 y 구간의 합집합 길이. 갱신은 경로 위 노드만 다시 계산하면 불변식이 유지된다.

왜 **내려보내기(push-down)가 필요 없나**: 한 사각형의 y 구간 `[l, r)` 이 트리에서 쪼개지는 노드 집합(정규 분해)은 `[l, r)` 만으로 결정된다. 그래서 닫기(−1)는 열기(+1)가 올려 둔 **바로 그 노드들**에서 뺀다 — `cnt` 는 음수가 되지 않고, 자식 정보는 손댈 일이 없다. 일반적인 "구간 더하기 + 구간 최솟값" 트리와 다른 점이고, 이 문제가 특별히 쉬운 이유다.

넓이는 **갱신하기 전에** `루트 len × (x − 직전 x)` 를 더한다: 직전 사건부터 지금 x 까지 덮인 길이는 직전 상태의 것이다. 순서를 바꾸면 1500개 격자 사례 중 1373개가 틀린다(7. 실수 6). 같은 x 의 열기/닫기 순서는 상관없다 — 그 사이 폭이 0 이니까. (ISR 은 "구간이 몇 개 겹치나" 를 보므로 같은 시각 순서가 중요했고, 넓이는 폭을 곱하므로 중요하지 않다. 같은 스위프라도 무엇을 재느냐가 동점 규칙을 정한다.)

복잡도: 사건 2n 개 × 갱신 O(log n) + 정렬 → O(n log n). 노드 4m 개(m = 기본 구간 수 < 2n) × (`int32 cnt` + `uint64 len`) = RECT_MAX 1024 에서 정적 배열 96 KiB, 재귀 깊이 ⌈log2 m⌉ + 1 ≤ 12. **넘침**: 좌표 |c| ≤ 2^30 이면 폭·길이 < 2^31, 넓이 ≤ 외접 사각형 ≤ 2^62 → uint64 에 들어간다(테스트 `huge` 가 정확히 2^62). LeetCode 850 은 넓이를 10^9+7 로 나머지 내라고 하는데, 그건 언어 독립성을 위한 것이고 64비트 C 에서는 이 범위면 필요 없다.

### Part C — 링커 메모리 맵 검사

F4 계열을 흉내 낸 가상 보드의 맵(FLASH 1 MiB, SRAM 128 KiB, CCM 64 KiB, 시스템 영역)에 실수 몇 개를 넣었다: 부트로더가 1 KiB 커져 설정 섹터를 침범, 힙 끝이 DMA 버퍼를 덮음, CCM 끝을 넘는 `fastcode`, 2^32 를 넘는 `dwt_rom`, 크기 0 `.noinit`. 실제 보고서:

```text
== memory map check: 18 regions ==
  [EMPTY]        .noinit   at 0x20003800 (size 0: ignored)
  [OUT-OF-SPACE] dwt_rom   ends at 0x100001000 > 2^32 (uint32 end would wrap to 0x00001000)
  [STRADDLE]     ccm       x fastcode  [0x1000f000, 0x10010000)   4096 B  PARTIAL
  [STRADDLE]     sysreg    x dwt_rom   [0xfffff000, 0x100000000)   4096 B  PARTIAL
  [OVERLAP]      boot      x cfg       [0x08010000, 0x08010400)   1024 B  PARTIAL
  [OVERLAP]      heap      x dma_buf   [0x2000b000, 0x2000b800)   2048 B  PARTIAL
  [ORPHAN]       fastcode  not inside any memory region
  [ORPHAN]       dwt_rom   not inside any memory region
  summary: pairs=15 (expected containment 11) overlap=2 mem-overlap=0 straddle=2 orphan=2 oob=1 empty=1
```

읽는 법: 15쌍 중 11쌍은 "섹션이 자기 메모리 안에 있다" 는 **정상 포함**이라 보고하지 않는다 — 분류가 있어야 경보를 걸러 낼 수 있다. `app_a`(끝 0x08080000)와 `app_b`(시작 0x08080000)는 맞닿음이라 쌍이 아니다. A/B 슬롯 OTA 에서 두 슬롯 사이 한 바이트도 겹치면 안 되는데, 반대로 맞닿음을 겹침이라 보고하는 도구는 쓸모가 없다.

이 검사가 링커와 겹치지 않는 지점: GNU ld 는 출력 섹션끼리의 주소 겹침을 기본으로 검사하는 것으로 안다(`--check-sections`, 확인 권장). 하지만 부트로더·앱 슬롯·설정 섹터·로그 영역처럼 **여러 이미지에 걸친 파티션 표**, 런타임에 잡는 DMA 버퍼 풀, MPU 표, 디바이스트리의 `reg`/`reserved-memory` 같은 것은 링커가 모른다. 이 레슨의 도구는 CI 에서 "모든 이미지의 맵 + 파티션 표" 를 한 번에 넣고 돌리는 용도다.

### Part C — MPU 영역 표: v7 은 겹침이 기능, v8 은 오류

ARMv7-M(PMSAv7, Cortex-M3/M4/M7)의 MPU 영역은 크기가 2^k(≥ 32 B)이고 base 가 크기에 정렬돼야 하며, **겹쳐도 되고 번호가 큰 영역이 이긴다**. 그래서 "SRAM 전체 RW + 그 안의 DMA 비캐시 창 + 스택 가드 32 B" 를 겹쳐서 표현하는 게 정석이다. ARMv8-M(PMSAv8, Cortex-M23/M33/M55)은 base/limit 를 32 B 단위로 자유롭게 주는 대신 **영역 겹침을 허용하지 않는다**(ST AN4838 및 Arm 자료 기준. 겹친 주소 접근 시 MemManage fault 가 난다고 알려져 있음 — 확인 권장). 실제 출력:

```text
== MPU: PMSAv7 table ==
    region 2 dma_nc inside/over region 1 sram: region 2 wins (CONTAIN)
    region 3 stk_grd inside/over region 1 sram: region 3 wins (CONTAIN)
== MPU: PMSAv7 table with mistakes ==
    region 1 dma_nc    base/size not legal for PMSAv7
    region 2 periph    base/size not legal for PMSAv7
    region 1 dma_nc inside/over region 0 sram: region 1 wins (CONTAIN)
== MPU: same v7 table ported to PMSAv8 as-is ==
    region 1 sram overlaps region 2 dma_nc: not allowed on PMSAv8
    region 1 sram overlaps region 3 stk_grd: not allowed on PMSAv8
== MPU: PMSAv8 table split around the holes ==
   v7: errors=0 overlaps=2 | v7bad: errors=2 | v8 ported: errors=2 | v8 split: errors=0 overlaps=0
```

v7 표를 그대로 v8 로 옮기면 겹침 2개가 오류가 되고, SRAM 을 구멍 주위로 세 조각(`sram_lo`, `sram_mid`, `sram_hi`)으로 쪼개야 한다 — 영역 5개가 7개가 된다. MPU 영역 수는 구현마다 8 또는 16 정도로 적으므로(Cortex-M4 는 8개) 이 "쪼개기 비용" 이 v8 포팅의 실제 고민거리다. v7 의 서브영역 비활성(SRD, 256 B 이상 영역을 8등분해 일부를 끄기)도 v8 에는 없다.

### Part C — 중첩 ISR 트레이스와 스택 크기

12개 ISR 진입/종료(µs), 스택 = 함수 스택(`-fstack-usage` 로 얻는다고 가정) + 기본 예외 프레임 32 B. 실제 출력:

```text
== ISR trace: 12 entries ==
  max nesting depth 4 in [330, 333) us, windows=1, active: TIM2 EXTI0 ADC SysTick
  max ISR stack   584 B in [530, 532) us, active: I2C1_EV SysTick
  static bound (sum over priority levels of max stack) = 1048 B
```

- 깊이 최대(4)와 스택 최대(584 B)는 다른 곳이다. 스택 크기를 "최대 중첩 깊이 × 평균 프레임" 으로 잡으면 I2C 핸들러 하나가 큰 경우를 놓친다.
- **관측 584 B 는 안전한 크기가 아니다.** 이 로그에는 I2C(레벨 6) 위에 DMA(레벨 1, 200 B)가 얹힌 적이 없을 뿐이다. 우선순위 레벨마다 많아야 하나씩 쌓이므로 정적 상한은 Σ_레벨 max = 64 + 200 + 48 + 96 + 520 + 120 = **1048 B**. 여기에 스레드 모드 → 핸들러 진입 시 MSP 를 쓰는지, FPU 를 쓰는 ISR 이면 확장 프레임(32 B 대신 104 B, 정렬 패딩 4 B 가능)인지를 더한다(확장 프레임 크기는 Armv7-M 문서 기준 — 확인 권장).
- 실무 순서: 정적 상한으로 MSP 크기를 잡고, 스택 페인팅(0xDEADBEEF 칠하고 최고 수위 확인)과 이 레슨의 로그 분석으로 상한이 지나치게 비관적인지 본다. 둘이 크게 다르면 우선순위 설계를 다시 본다(큰 스택 ISR 을 낮은 레벨로 몰기, 무거운 일은 태스크로 미루기).

### Part C — LCD 더티 사각형: 무엇을 SPI 로 보낼까

240×320 RGB565(픽셀당 2 B) 화면에서 한 프레임에 7개 위젯이 다시 그려졌다. 실제 출력:

```text
== LCD 240x320 RGB565 dirty rects: 7 ==
  each rect sent separately:  60376 px = 120752 B
  bounding box             :  60000 px = 120000 B
  exact union (slab pieces):  47200 px =  94400 B in 19 windows
  full frame               :  76800 px = 153600 B
```

SPI 40 MHz 로 환산하면(바이트 × 8 / 40·10^6) 각각 24.2 / 24.0 / 18.9 / 30.7 ms. 정확한 합집합은 사각형별 전송보다 22% 적다. 대가는 LCD 창 명령 19번 — ILI9341 류 컨트롤러에서 창 하나는 CASET(0x2A)+4 B, 페이지/행 주소(0x2B)+4 B, RAMWR(0x2C) 정도라 약 11 B × 19 = 209 B 에 CS/DC 토글 비용이 붙는다(컨트롤러마다 명령 이름이 조금 다름 — 데이터시트 확인). 연습 4 의 overdraw(두 번 이상 덮인 넓이) = 11400 px 가 "사각형별 전송" 에서 낭비되는 픽셀이다.

MCU 에서 이 계산을 매 프레임 한다면 **슬랩법**이 맞다: 더티 사각형은 보통 수십 개라 n² log n 이 작고, 세그먼트 트리의 정적 배열(노드 4m 개)이 필요 없으며, 무엇보다 **보낼 창 목록 자체**를 만들어 준다. 세그먼트 트리는 넓이만 준다. LVGL 같은 GUI 라이브러리는 겹치는 무효 영역을 합치는 더 단순한 휴리스틱을 쓰는 것으로 안다(정확한 규칙은 확인 권장).

### 벤치마크 — `demo_bench.c`

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

Apple M6 (`hw.perflevel0/1.physicalcpu` = 2/4), Apple clang version 21.0.0 (clang-2100.0.123.102), `-O2`. 측정 직전 `uptime` load averages: **58.71 67.34 37.87** — 다른 작성 에이전트 10개가 같이 돌고 있어서 매우 시끄럽다. 절대값보다 **비율**을 볼 것. 각 칸 = 워밍업 1회 + 5회의 중앙값 [최소–최대] (실제 출력, 열 정리):

| 성긴 맵 n | k | 스위프 ms | 모든 쌍 ms | 모든 쌍/스위프 |
|---|---|---|---|---|
| 1000 | 966 | 0.038 [0.037–0.042] | 0.31 [0.31–0.74] | 8 |
| 4000 | 3995 | 0.188 [0.187–0.206] | 5.91 [5.15–8.69] | 31 |
| 16000 | 16040 | 1.206 [0.872–1.574] | 133.10 [90.75–190.05] | 110 |
| 64000 | 64444 | 4.881 [4.421–7.105] | (생략: 2·10^9 쌍) | — |

| 빽빽한 맵 n (전부 동일, 개수만) | k | 스위프 ms | 모든 쌍 ms | 비 | ns/쌍 (스위프) |
|---|---|---|---|---|---|
| 1000 | 499500 | 0.193 | 0.32 | 1.7 | 0.39 |
| 2000 | 1999000 | 0.794 | 2.00 | 2.5 | 0.40 |
| 4000 | 7998000 | 2.613 | 6.96 | 2.7 | 0.33 |

| 사각형 n | 슬랩 ms | 세그먼트 트리 ms | 슬랩/트리 |
|---|---|---|---|
| 125 | 0.045 [0.041–0.072] | 0.017 [0.015–0.024] | 2.6 |
| 250 | 0.135 [0.098–0.207] | 0.048 [0.045–0.067] | 2.8 |
| 500 | 0.607 [0.561–0.737] | 0.111 [0.097–0.143] | 5.5 |
| 1000 | 2.753 [2.542–3.489] | 0.339 [0.301–5.611] | 8.1 |
| 2000 | 16.384 [12.155–22.385] | 1.175 [0.932–1.478] | 13.9 |
| 4000 | 53.425 [51.136–56.480] | 2.015 [1.865–4.336] | 26.5 |

바로 다음 실행(load 60.05)에서는 성긴 맵 비율 30/31/78, 빽빽한 맵 2.7/1.9/1.8, 사각형 비율 1.6/3.0/4.9/5.0/9.6/26.1(n = 125/250/500/1000/2000/4000) 이었다 — 작은 n 의 비율은 부하에 따라 크게 흔들리고, 큰 n 의 경향만 믿을 만하다. 읽을 것:

- 성긴 맵에서 n 이 4배일 때 스위프는 4.0–6.4배(n log n), 모든 쌍은 첫 실행에서 19–23배(n² = 16배에 캐시 효과). 차이가 n 과 함께 벌어진다. (두 번째 실행의 n = 1000 모든 쌍 1.01 ms 처럼 작은 값은 부하에 크게 흔들린다.)
- k = n² 급이면 둘 다 Θ(n²) 이고 스위프는 2–3배 빠를 뿐 — 출력 민감 알고리즘은 출력이 크면 이길 수 없다. 쌍 하나당 0.3–0.4 ns 는 "활성 배열을 순서대로 읽으며 카운터 증가" 비용이다(cap = 0 이라 쓰기 없음).
- 사각형 n = 4000 에서 슬랩/트리 26배. n 이 2배일 때 슬랩은 3–6배(n² log n 이면 4배 남짓), 트리는 1.7–3.5배(n log n 이면 2배 남짓) — 측정이 시끄러워 범위가 넓다. n = 1000 의 트리 최대 5.6 ms 같은 튀는 값은 부하 때문이다.

### 복잡도 요약

| 함수 | 시간 | 추가 메모리 | 스택 |
|---|---|---|---|
| `mm_overlaps` | O(n log n + k) | 이벤트 2n·8 B ×2 + 활성 2·2n B (static) | O(1), 재귀 없음 |
| `pairs_canon` | O(n + k) | 카운트 n+1, tmp k | O(1) |
| `isr_peak` | O(n log n) | 이벤트 2n·8 B ×2 | O(1) |
| `rect_union_slab` | O(n² log n) | 키 2n·8 B ×2 + 좌표 2n·4 B | O(1) |
| `rect_union_segtree` | O(n log n) | 노드 4m·12 B (m < 2n) | 재귀 ⌈log2 m⌉+1 |

모든 작업 공간이 static 이라 **재진입 불가**(두 태스크가 동시에 부르면 안 됨, ISR 에서 부르면 안 됨). 펌웨어 안에서 쓰려면 작업 공간을 호출자가 넘기는 형태로 바꾼다(Q8).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 44/44 passed (0.07 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 44/44, 보고 없음 (0.4 초)
```

무작위 대조 줄의 실제 출력:

```text
random maps: 3000 maps, 281819 overlapping pairs total, mismatches=0
random ISR logs: 4000 cases (1057 with >1 peak window), mismatches=0
random rects: 1500 grid cases mismatches=0, 300 large-coordinate cases slab!=segtree=0
```

## 7. 자주 하는 실수

아래 결과는 모두 `solution.c` 의 한두 줄만 바꾼 변형을 이 레슨 작업 중 실제로 돌린 것이다 (변형은 레슨 폴더 밖 작업 디렉터리에서만 만들었다).

**1. 같은 좌표에서 START 를 먼저 처리한다.** (`EV_END`/`EV_START` 값을 바꿈) 맞닿음이 겹침으로 보고된다: 39/44, 무작위 맵 3000개 중 **1988개** 불일치, Part C 보고서 `pairs=19 overlap=6`(정답 15, 2), v8 MPU 분할 표(맞닿은 조각들)가 `errors=4 overlaps=4`. → 반개구간이면 END 먼저. 키에 박아 두면 실수할 자리가 없다.

**2. ISR 에서 사건 하나마다 창을 판정한다.** 최댓값은 맞는데 창이 쪼개진다: 41/44, 무작위 4000개 중 **291개** 불일치, 맞닿은 사슬 `[0,5)[5,10)[10,15)` 과 tail-chaining `CAN1_RX0`→`CAN1_TX` 가 FAIL. → 같은 시각 사건을 다 적용한 뒤에 본다.

```c
do { /* 시각 t 의 사건 적용 */ e++; } while (e < ne && ev_coord(g_iev[e]) == t);
```

**3. 끝을 `uint32_t` 로 계산한다.** `0xE0000000 + 0x20000000` → 0. END 키가 START 앞으로 가서 빈 활성 집합에서 빼기 → `na` 가 `SIZE_MAX` 로 감싼다. 실제: `-O2` 빌드는 20 초 안에 끝나지 않았고, ASan 빌드는 `AddressSanitizer: global-buffer-overflow … in mm_overlaps` (`g_act[--na]` 줄) + UBSan 경고로 멈췄다. "틀린 답" 이 아니라 **멈춤/메모리 손상**이라는 점이 펌웨어에서 더 나쁘다. → `(uint64_t)base + size`, 또는 끝 포함 + 넘는 영역 사전 거부.

**4. 크기 0 영역을 거르지 않는다.** 같은 이유(END(b) 와 START(b) 가 같은 좌표, END 가 먼저)로 3번과 똑같이 ASan global-buffer-overflow. 활성 집합 코드에 "없는 id 를 빼면 무시" 같은 방어를 넣는 대신 **전제조건을 입력에서 보장**하는 게 맞다: 이벤트 쌍은 언제나 START 키 < END 키.

**5. 분류에서 EQUAL 을 나중에 검사한다.** 동일 영역이 CONTAIN(outer 0)으로 나온다: 41/44, 무작위 맵 **2489개** 불일치(생성기가 1/8 확률로 같은 영역을 복제하므로). 두 섹션이 완전히 같은 주소를 쓰는 건 "포함" 이 아니라 거의 항상 **복사·붙여넣기 버그**라 따로 보고할 가치가 있다.

**6. 세그먼트 트리 넓이를 갱신 뒤에 더한다.** `area += len × (x − px)` 를 `st_update` 다음으로 옮김: 단일 사각형부터 틀린다(넓이 0). 격자 1500개 중 **1373개**, 큰 좌표 300개 전부 불일치. → 직전 사건~지금 사이의 길이는 **갱신 전** 루트 len.

**7. 슬랩에서 마지막 열린 y 구간을 더하지 않는다.** 루프가 끝난 뒤 `if (open) cov += ce − cs;` 를 빠뜨림: 사각형 하나짜리부터 넓이 0, 격자 **1382/1500** 불일치. 병합 루프의 고전적인 "꼬리" 버그(M09 병합과 같다).

**8. 맞닿은 y 구간을 이어 붙이지 않는다.** (`a <= ce` → `a < ce`) 넓이는 맞지만 조각 수가 늘어난다: 43/44, `adj` 사례의 `pieces == 2` 만 FAIL. 넓이만 보는 테스트는 이걸 못 잡는다 — LCD 에선 창 명령이 늘어나는 성능 버그다. 테스트는 함수가 내놓는 **모든** 값을 검사해야 한다.

## 8. 면접 Q&A

<details><summary>Q1. 영역 끝을 왜 uint64_t 로 계산하나요? 32비트 MCU 에서 64비트 연산은 비싸지 않나요?</summary>

`base + size` 는 33비트까지 간다. 시스템 영역 `0xE0000000 + 0x20000000 = 2^32` 처럼 주소 공간 끝에 닿는 영역은 실제로 흔하고, uint32 로는 0 이 된다 — 이 레슨에서는 정렬 순서가 깨져 무한 루프/경계 밖 접근이 났다. 비용: Cortex-M4 실제 asm 에서 uint64 판정 19 명령(adds/adc, subs/sbcs, push 포함) vs 끝 포함 uint32 판정 15 명령. 끝 포함(`last = base + size − 1`)은 size ≠ 0 이고 영역이 2^32 를 넘지 않을 때만 맞으므로, 등록 시점에 그 두 조건을 검사하고 거부하는 설계와 짝이다. 꼬리: "size 가 2^32 인 영역(전체 주소 공간)은?" → uint32 size 로는 표현 불가. PMSAv7 은 SIZE 필드(로그 크기)로, PMSAv8 은 limit 주소로 표현하므로 끝 포함 표현이 하드웨어와도 맞는다.

</details>

<details><summary>Q2. 같은 좌표에서 END 를 먼저 처리하는 이유를 증명해 보세요. 닫힌구간이면요?</summary>

5장 보조정리: 키 순서 (좌표, END<START, id) 에서 START_i 시점의 활성 집합 = `{j : START_j 가 먼저, end_j > base_i}` 이고, 먼저 시작한 j 와 i 의 겹침 ⇔ `end_j > base_i`. END 가 뒤면 `end_j == base_i`(맞닿음)까지 들어간다. 닫힌구간 `[first, last]` 는 `last_j == first_i` 가 한 바이트 공유 = 겹침이므로 START 먼저가 맞다. 실제 변형(START 먼저)은 3000개 중 1988개를 틀렸다. 꼬리: "id 까지 키에 넣는 이유?" → 같은 좌표·같은 종류에서 순서를 고정해 출력이 **결정적**이 된다(비교 정렬이 불안정해도). 64비트 정수 하나로 패킹하면 비교 한 번에 사전순.

</details>

<details><summary>Q3. 겹치는 쌍이 n² 개 가까이 되면요? 개수만 필요하면요?</summary>

쌍을 전부 내놓는 한 출력 크기 Θ(n²) 가 하한이라 어떤 알고리즘도 못 이긴다 — 실측도 동일 영역 4000개에서 스위프가 모든 쌍보다 2–3배 빠를 뿐. 개수만이면 START 에서 `k += 활성 수` 로 O(n log n)(연습 1, 4000개 동일 영역 → 7998000 을 바로). 실무 판단: 메모리 맵에서 k 가 크다는 건 "맵이 엉망" 이란 뜻이므로, 상위 몇 개만 보고하고 개수를 같이 주는 `snprintf` 식 계약(`cap` + 총수 반환)이 맞다. 꼬리: "쌍마다 겹친 바이트 합은?" → 쌍 열거 없이도 가능: 스위프하며 Σ C(활성 수, 2) × 구간 길이.

</details>

<details><summary>Q4. 영역이 런타임에 하나씩 등록된다면(동적 DMA 버퍼, MPU 재설정) 어떻게 하죠?</summary>

오프라인 스위프 대신 **온라인** 질의 자료구조가 필요하다. 일반해는 구간 트리(interval tree, 끝점 최댓값을 보강한 균형 이진 탐색 트리): 삽입 O(log n), "이 영역과 겹치는 것 전부" 질의 O(log n + k_i). 하지만 펌웨어에서 등록되는 영역은 수십 개다. 그러면 **겹침을 허용하지 않는 정렬 배열**이 가장 단순하고 결정적이다: base 로 이진 탐색해 바로 앞·뒤 이웃 둘과만 비교(겹침이 없다는 불변식 덕분에 그 둘만 보면 된다), 삽입은 O(n) memmove. 고장 주소 조회(MMFAR/BFAR → 섹션)도 같은 배열에서 O(log n)(연습 2). 꼬리: "ISR 에서도 등록되면?" → 배열 갱신을 임계 구역으로, 또는 등록은 태스크에서만 하도록 설계.

</details>

<details><summary>Q5. ISR 로그에서 본 최대 스택을 MSP 스택 크기로 써도 되나요?</summary>

안 된다. 로그는 일어난 중첩만 담는다. 이 레슨 트레이스: 관측 최대 584 B, 우선순위 레벨별 최댓값의 합(정적 상한) 1048 B. 단일 코어에서 같은 선점 레벨끼리는 서로 선점하지 못하므로 레벨마다 많아야 하나가 쌓인다는 게 상한의 근거다. 여기에 FPU 확장 프레임(ISR 이 FPU 를 쓰면 프레임이 커짐), 폴트 핸들러(HardFault 가 맨 위에 하나 더), 컴파일러 버전·최적화 수준에 따른 `-fstack-usage` 변화를 더한다. 관측값은 상한이 너무 비관적인지 판단하는 데, 스택 페인팅은 현장 검증에 쓴다. 꼬리: "깊이 최대인 시각에 스택도 최대 아닌가요?" → 아니다. 이 트레이스에서 깊이 4 는 224 B, 깊이 2 가 584 B 였다 — 그래서 가중치 스위프를 따로 돌린다.

</details>

<details><summary>Q6. "사건마다 최댓값 갱신" 과 "같은 시각 묶음 뒤 갱신" 은 언제 다른 답을 내나요?</summary>

최댓값과 처음 시각은 END 먼저면 같다(묶음 안 중간값이 직전·현재 묶음 값의 최댓값을 넘지 않음, 가중치 ≥ 0 전제). 다른 것은 **창 경계와 개수**: 맞닿은 구간 사슬에서 사건별 판정은 중간에 잠깐 떨어진 값을 보고 창을 쪼갠다. 실측 4000개 중 291개가 달랐다. 가중치가 음수일 수 있는 문제(예: 충전/방전 이벤트)면 END 먼저 규칙 자체가 최댓값을 부풀리거나 줄일 수 있으니 묶음 방식만 안전하다. 꼬리: "타임스탬프 해상도가 거칠면?" → 1 µs 틱에서 '같은 시각' 인 두 사건은 실제로는 순서가 있다. 반개 규약 + 묶음은 "같은 틱이면 동시에 바뀐 것으로 본다" 는 보수적 해석이다. 더 정확한 순서가 필요하면 로거가 시퀀스 번호를 같이 찍어야 한다.

</details>

<details><summary>Q7. 직사각형 합집합: 슬랩과 세그먼트 트리 중 무엇을 MCU 에 넣겠어요?</summary>

n 이 수십(더티 사각형, 터치 영역)이면 슬랩. 이유: (1) n² log n 이 작다 (2) 세그먼트 트리는 노드 4m 개 정적 배열(RECT_MAX 1024 에서 96 KiB)이 필요하고 재귀가 있다 (3) 슬랩은 넓이뿐 아니라 **서로소 사각형 목록**(보낼 LCD 창)을 바로 준다. 세그먼트 트리는 n 이 수천 이상(지도 타일, 레이아웃 검사 도구)일 때 — 실측 n = 4000 에서 26배 빠름. 트리에 push-down 이 없는 이유: 닫기는 열기가 올려 둔 같은 노드들에서 빼므로 cnt 가 음수가 되지 않는다(정규 분해가 구간만으로 결정). 꼬리: "둘레(perimeter)는?" → 같은 스위프에서 루트 len 의 변화량 |Δlen| 합 + 덮인 조각 수 × 2 × 폭(조각 수를 노드에 유지) — 연습으로 남긴다.

</details>

<details><summary>Q8. 이 코드를 RTOS 펌웨어 안에서(여러 태스크·ISR) 쓰려면 무엇을 바꾸죠?</summary>

(1) static 작업 공간 → 호출자가 넘기는 `workspace_t`(크기는 MAX 로 컴파일 타임에 계산). 지금은 재진입 불가라 두 태스크가 동시에 부르면 이벤트 배열이 섞인다. (2) 정렬은 비재귀 병합 정렬이라 스택은 O(1)이지만 O(n) 버퍼가 필요하다 — RAM 이 빠듯하면 힙 정렬(제자리, 불안정 — 키에 id 가 있어 결과는 여전히 결정적). (3) ISR 로그는 ISR 에서 링 버퍼(DS-M10)에 진입/종료만 기록하고 분석은 태스크나 호스트에서. (4) 타임스탬프가 32비트로 감싸면 분석 전에 풀기(연습 5), 코어가 둘이면 코어별 타임라인. (5) 최악 실행 시간이 입력에 따라 O(k) 로 커지므로, 실시간 경로에서는 `cap` 으로 출력을 자르고 총수만 받는다.

</details>

<details><summary>Q9. ARMv7-M MPU 와 ARMv8-M MPU 에서 "영역 겹침" 의 의미는 어떻게 다르죠?</summary>

PMSAv7: 겹침 허용, 번호가 큰 영역의 속성이 이긴다. 그래서 큰 배경 영역 위에 작은 예외(스택 가드, 비캐시 DMA 창)를 얹는 게 관용구고, 크기 2^k·정렬 제약 때문에 서브영역 비활성(SRD)으로 모양을 다듬는다. PMSAv8: base/limit 32 B 단위로 자유로운 대신 겹침 금지 — 이 레슨의 검사기에서 v7 표를 그대로 옮기면 오류 2, SRAM 을 세 조각으로 쪼개면 0. 겹친 주소에 접근하면 MemManage fault 가 난다고 알려져 있다(확인 권장). 꼬리: "쪼개다 영역 수가 모자라면?" → 속성이 같은 인접 영역 합치기, 기본 메모리 맵(PRIVDEFENA)을 배경으로 쓰기, 태스크 전환 때 일부 영역만 다시 쓰기(RTOS 의 태스크별 MPU 영역).

</details>

## 9. 추가 연습

### 연습 1 — 겹치는 쌍의 개수만, O(n log n)

쌍을 열거하지 않고 k 만 구하라. 4000개 동일 영역(2^32 에서 끝남)에서 7998000 이 바로 나와야 한다.

<details><summary>답 보기</summary>

본문 스위프에서 START 를 만날 때 `k += active; active++`, END 에서 `active--`. 보조정리에 따라 START 시점의 활성 수 = 그 영역과 겹치는 먼저 시작한 영역 수이므로 합이 정확히 k. 키는 `(좌표 << 1) | START` 로 충분하다(id 는 개수에 영향 없음). 무작위 1500개를 모든 쌍과 대조, 맞닿음·크기 0 사례 포함. 시간 O(n log n), k 가 n² 이어도 같다.

</details>

### 연습 2 — 고장 주소 → 섹션 (HardFault 디코더)

MemManage/BusFault 가 나면 MMFAR/BFAR 에 고장 주소가 남는다(유효 비트 확인 필요). 겹침 검사를 통과해 서로소이고 base 로 정렬된 섹션 표에서, 주소를 담은 섹션 번호를 O(log n) 에 찾아라(없으면 −1).

<details><summary>답 보기</summary>

`base ≤ addr` 인 마지막 섹션만 후보다 — 서로소이고 정렬돼 있으니 그보다 앞 섹션들은 그 섹션 base 이전에 끝났다. upper_bound 로 `lo` 를 찾고 `addr < end(lo−1)` 확인(B02 의 반개구간 이진 탐색 불변식). 끝 비교는 uint64 라 `0xFFFFF000 + 0x1000` 섹션의 `0xFFFFFFFF` 도 찾는다. 20만 개 무작위 주소(섹션 근처 3/4, 아무 데나 1/4)를 선형 탐색과 대조. 표가 서로소가 아니면(포함 관계) "가장 안쪽" 을 찾는 문제가 되어 이 방법은 틀린다 — 그게 Part A 검사를 먼저 하는 이유다.

</details>

### 연습 3 — 단일 코어에서 가능한 ISR 로그인가 (적층 검사)

ISR 로그가 적층(laminar: 두 구간은 서로소이거나 포함)인지 검사하고, 맞으면 최대 중첩 깊이를 돌려라(아니면 −1). Part C 트레이스 → 4, `[100,160) [150,170)` → −1.

<details><summary>답 보기</summary>

(t0 오름차순, 같으면 t1 내림차순 = 바깥 먼저)으로 정렬하고 끝 시각 스택을 유지한다: 새 구간 시작 전에 `top ≤ t0` 인 것을 모두 꺼내고(반개: 끝 == 시작이면 끝남), 남은 top 보다 늦게 끝나면 부분 겹침 → −1, 아니면 push. 최대 스택 높이 = 최대 깊이. 정렬 키는 `(t0 << 32) | ~t1` 로 한 번에. 적층 생성기(구간 안에 서로소 자식을 재귀적으로 배치)로 만든 로그 2000개 중 1/3 을 일부러 깨뜨려(308개가 실제로 비적층) 모든 쌍 검사·격자 깊이와 대조. 비적층 로그는 단일 코어에서 불가능하므로 **로거 버그**(진입/종료 짝 오류, 다른 코어 로그 섞임, 타임스탬프 감싸기 미처리)를 뜻한다.

</details>

### 연습 4 — 겹쳐 그린 넓이 (두 번 이상 덮인 넓이)

LCD 더티 사각형에서 두 번 이상 덮인 픽셀 수를 구하라(사각형별 전송의 낭비량).

<details><summary>답 보기</summary>

슬랩마다 y 끝점을 +1/−1 사건으로 정렬하고 1D 깊이 스위프로 "깊이 ≥ 2 인 길이" 를 잰다 — 각 띠가 M09 의 깊이 문제가 된다. 같은 y 에서 순서는 길이 0 이라 무관. 무작위 800개를 격자 전수와 대조, Part C 화면 = **11400 px**(실제 출력). 세그먼트 트리로 하려면 노드마다 len1(≥1 덮임)·len2(≥2 덮임)를 유지: cnt ≥ 2 면 len2 = 전체, cnt == 1 이면 len2 = 자식 len1 합, cnt == 0 이면 자식 len2 합.

</details>

### 연습 5 — 32비트 타임스탬프 풀기

진입 시각이 단조 증가하고 이웃 간격이 2^31 미만인 uint32 시각열을 uint64 타임라인으로 풀어라. `0xFFFFFFF0, 0xFFFFFFFE, 0x00000005, 0x00000100` → `…, 0x100000005, 0x100000100`.

<details><summary>답 보기</summary>

`acc += (uint32_t)(t[i] − t[i−1])`. 부호 없는 뺄셈은 모듈러 2^32 차이이고, 실제 간격이 2^32 미만이면 그게 실제 경과 시간이다(C11 에서 부호 없는 산술의 감싸기는 정의된 동작). 간격이 2^32 이상이면 정보가 사라져 복원 불가 — 그래서 로거는 최소 한 주기 안에 한 번은 무언가를 찍어야 한다(유휴 heartbeat). 168 MHz DWT CYCCNT 면 약 25.6 초. 무작위 200개 시각열(2^32 근처 시작 포함)을 생성기의 64비트 값과 대조. 종료 시각은 `t0 + duration` 으로 같은 방식으로 푼 뒤 본문 `isr_peak` 를 64비트 좌표로 바꿔 쓰면 된다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 14/14 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 14/14, 보고 없음
```

## 10. 핵심 정리

- 스위프 = 이벤트 정렬 + 한 번 훑기 + 활성 집합. 이벤트를 `(좌표 << 17) | (종류 << 16) | id` 64비트 하나로 만들면 동점 규칙과 결정성이 정수 비교 한 번에 들어간다.
- 반개구간이면 같은 좌표에서 END 먼저 → START 가 보는 활성 집합 = 정확히 자기와 겹치는 먼저 시작한 영역(보조정리). 닫힌구간이면 START 먼저.
- 겹침 쌍 전부: START 에서 활성 전부와 쌍 → O(n log n + k). k = n² 이면 하한도 n². 개수만이면 `k += 활성 수` 로 O(n log n).
- 끝은 33비트: `(uint64_t)base + size`. 크기 0 은 이벤트를 만들지 않는다. 둘 다 어기면 틀린 답이 아니라 **멈춤/경계 밖 접근**(실측).
- ISR 최대 동시 활성: 같은 시각 사건을 다 적용한 뒤 값을 본다 → 창 [from, to) 가 정확. 가중치 = 스택 바이트. 관측 최댓값 ≠ 안전 크기 → 레벨별 최댓값 합(정적 상한, 예: 584 vs 1048 B).
- 사각형 합집합: 띠마다 1D 병합(O(n² log n), 서로소 조각 = LCD 창 목록) 또는 세그먼트 트리(cnt/len, push-down 없음, 넓이는 갱신 전에 누적, O(n log n)).
- 펌웨어: 링커 맵·파티션 표는 분류로 정상 포함을 걸러 내고 겹침·걸침·고아·주소 공간 밖을 보고. MPU 는 v7 = 겹침 허용(큰 번호 승), v8 = 겹침 금지(쪼개기 비용).

## 참고자료

- M. de Berg, O. Cheong, M. van Kreveld, M. Overmars, *Computational Geometry: Algorithms and Applications*, 3rd ed., Springer 2008 — 2장 선분 교차의 평면 스위프, 10장 구간 트리·세그먼트 트리.
- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms* — 구간 트리(augmented red-black tree) 절, 33장 선분 교차 스위프.
- LeetCode 850 Rectangle Area II (예제 1 = 이 레슨의 `r3`, 답 6), 253 Meeting Rooms II, 56 Merge Intervals, 218 The Skyline Problem.
- STMicroelectronics, AN4838 "Introduction to memory protection unit management on STM32 MCUs" — Armv6/v7-M MPU 는 영역 겹침·중첩 허용(번호 큰 영역 우선), Armv8-M 은 겹침 불허: <https://www.st.com/resource/en/application_note/dm00272912-managing-memory-protection-unit-stm32-mcus-stmicroelectronics.pdf>
- Arm Community, "What's new with the Memory Protection Unit (MPU) in Cortex-M23 and Cortex-M33" — PMSAv8 의 base/limit 32 B 단위 영역: <https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/what-s-new-with-the-memory-protection-unit-mpu-in-cortex-m23-and-cortex-m33>
- Arm, *Armv7-M Architecture Reference Manual* — 예외 진입 스택 프레임(기본 8 워드, FP 확장 프레임), NVIC 우선순위 그룹(PRIGROUP), PMSAv7 영역·SRD. (프레임 크기 104 B 는 기억 기준 — 확인 권장)
- GNU ld 문서 `--check-sections` (출력 섹션 주소 겹침 검사) — 동작 세부 확인 권장.
- AlgoMonster 노트 M11 §1~8 Intervals, §5 Meeting Rooms II 의 이벤트 스윕, §19~20 Line Sweep · Union Area of Rectangles (Don 정리본).
- 관련 레슨: ALGO-M09 (반개구간 규약, 최대 깊이 = 최소 채널, 구간 병합 — 이 레슨의 1D 기반), ALGO-B02 (반개구간 이진 탐색 불변식, 연습 2), ALGO-B12 (비트 패킹), DS-M10 (트레이스 링 버퍼), C-M14 (ISR 공유 데이터), C-A10 (재귀·스택 예산), ALGO-A10 (같은 "로그 → 오프라인 분석" 형식).
