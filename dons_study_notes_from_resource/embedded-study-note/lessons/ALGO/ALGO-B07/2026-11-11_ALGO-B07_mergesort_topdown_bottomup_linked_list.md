---
id: ALGO-B07
time: 60분
updated: 2026-11-11
---
# ALGO-B07 머지소트: Top-down·Bottom-up·연결 리스트

## 학습 목표

- 안정(stable) 병합 `merge_into` 하나로 배열 **top-down 재귀** 머지소트와 **bottom-up 반복** 머지소트를 둘 다 만든다. scratch 버퍼는 호출자가 한 번만 준다 (호출마다 `malloc` 없음).
- n 이 2의 거듭제곱이 아닐 때의 꼬리 run, `size_t` overflow 없는 경계 계산, ping-pong 버퍼의 "마지막 복사"를 실수 없이 처리한다.
- 단일 연결 리스트를 **bottom-up** 으로 정렬해 추가 메모리 O(1)·재귀 없음을 지키고, top-down 리스트 정렬은 왜 O(log n) 스택이 드는지 실측으로 말한다.
- 병합의 `<=` 와 `<` 한 글자가 안정성을 결정한다는 것을 (key, seq) 레코드로 증명하고, qsort 와는 **키 순서만** 대조해야 하는 이유를 설명한다.
- 플래시 요청 큐(ERASE → PROGRAM 순서)·외부 정렬(run 병합)에서 안정성과 메모리 예산이 왜 실제 문제인지 연결한다.

## 1. 문제

머지소트는 면접에서 "최악에도 O(n log n), 안정, 그리고 **연결 리스트에서 쓸 수 있는 정렬**"로 나온다 — LeetCode 의 "Sort List (O(1) 메모리)" 와 펌웨어의 "이미 풀(pool) 리스트에 들어 있는 요청을 정렬" 이 같은 문제다.

생각은 하나다: **정렬된 두 덩어리(run)를 합치는 것은 O(길이) 에 쉽다.** 그러니 크기 1 run 에서 시작해 두 배씩 합친다. 위에서 반으로 쪼개 내려가며 합치면 top-down, 아래에서 폭 1, 2, 4, … 로 올라가면 bottom-up 이다. 둘 다 한 층(패스)에서 원소 n 개를 한 번씩 옮기고 층이 ⌈lg n⌉ 개라 O(n log n) 이다.

배열은 병합할 때 결과를 쓸 **다른 자리**(scratch, n 칸)가 필요하다. 연결 리스트는 `next` 포인터만 바꾸면 되니 scratch 가 필요 없다. 이 차이가 이 레슨의 Part A/B 와 Part C 를 가른다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">n = 5 (키 3a 1a 3b 0 1b): 같은 정렬, 다른 병합 나무</text>
  <text x="20" y="44" fill="currentColor">top-down: 위에서 반씩 (mid = lo + (hi−lo)/2)</text>
  <g stroke="currentColor" fill="none">
    <line x1="185" y1="80" x2="85" y2="110"/><line x1="185" y1="80" x2="270" y2="110"/>
    <line x1="85" y1="134" x2="45" y2="160"/><line x1="85" y1="134" x2="115" y2="160"/>
    <line x1="270" y1="134" x2="215" y2="160"/><line x1="270" y1="134" x2="300" y2="160"/>
    <line x1="300" y1="184" x2="272" y2="210"/><line x1="300" y1="184" x2="328" y2="210"/>
  </g>
  <g stroke="currentColor">
    <rect x="125" y="56" width="120" height="24" rx="4" class="f-acc"/>
    <rect x="35" y="110" width="100" height="24" rx="4" class="f-acc"/>
    <rect x="215" y="110" width="110" height="24" rx="4" class="f-acc"/>
    <rect x="20" y="160" width="50" height="24" rx="4" class="f-mut"/>
    <rect x="90" y="160" width="50" height="24" rx="4" class="f-mut"/>
    <rect x="190" y="160" width="50" height="24" rx="4" class="f-mut"/>
    <rect x="260" y="160" width="80" height="24" rx="4" class="f-acc"/>
    <rect x="250" y="210" width="45" height="24" rx="4" class="f-mut"/>
    <rect x="305" y="210" width="45" height="24" rx="4" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="185" y="73">[0,5) 2+3</text><text x="85" y="127">[0,2) 1+1</text><text x="270" y="127">[2,5) 1+2</text>
    <text x="45" y="177">3a</text><text x="115" y="177">1a</text><text x="215" y="177">3b</text><text x="300" y="177">[3,5) 1+1</text>
    <text x="272" y="227">0</text><text x="328" y="227">1b</text>
  </g>
  <text x="20" y="262" fill="currentColor">병합 4번: 1+1, 1+1, 1+2, 2+3</text>
  <text x="20" y="282" fill="currentColor">최악 비교 1+1+2+4 = 8 (재귀 깊이 4)</text>
  <line x1="372" y1="34" x2="372" y2="290" stroke="currentColor" stroke-dasharray="4 4"/>
  <text x="390" y="44" fill="currentColor">bottom-up: 폭 w = 1, 2, 4 (재귀 없음)</text>
  <g fill="currentColor" font-family="monospace">
    <text x="390" y="77">입력</text><text x="390" y="127">w=1</text><text x="390" y="177">w=2</text><text x="390" y="227">w=4</text>
  </g>
  <g stroke="currentColor">
    <rect x="440" y="60" width="250" height="24" class="f-mut"/>
    <rect x="440" y="110" width="100" height="24" class="f-acc"/><rect x="540" y="110" width="100" height="24" class="f-acc"/><rect x="640" y="110" width="50" height="24" class="f-warn"/>
    <rect x="440" y="160" width="200" height="24" class="f-acc"/><rect x="640" y="160" width="50" height="24" class="f-warn"/>
    <rect x="440" y="210" width="250" height="24" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="465" y="77">3a</text><text x="515" y="77">1a</text><text x="565" y="77">3b</text><text x="615" y="77">0</text><text x="665" y="77">1b</text>
    <text x="465" y="127">1a</text><text x="515" y="127">3a</text><text x="565" y="127">0</text><text x="615" y="127">3b</text><text x="665" y="127">1b</text>
    <text x="465" y="177">0</text><text x="515" y="177">1a</text><text x="565" y="177">3a</text><text x="615" y="177">3b</text><text x="665" y="177">1b</text>
    <text x="465" y="227">0</text><text x="515" y="227">1a</text><text x="565" y="227">1b</text><text x="615" y="227">3a</text><text x="665" y="227">3b</text>
  </g>
  <text x="390" y="262" fill="currentColor">경고색 = 짝 없는 꼬리 run (그대로 복사)</text>
  <text x="390" y="282" fill="currentColor">병합: 1+1, 1+1, 2+2, 4+1 → 최악 비교 9</text>
</svg>
```

그림의 숫자는 `demo_bench.c` 가 n = 5 의 **모든 120개 순열**을 돌려 센 최대 비교 횟수(top-down 8, bottom-up 9)와 같다. 두 방식은 n 이 2의 거듭제곱이 아니면 병합 나무가 다르다 — bottom-up 은 마지막에 4+1 처럼 기울어진 병합을 한다.

### Part A — 안정 병합 + top-down 배열 머지소트

- `merge_into(dst, src, lo, mid, hi)` — 정렬된 `src[lo,mid)` 와 `src[mid,hi)` 를 `dst[lo,hi)` 로 병합. 키가 같으면 **왼쪽 먼저** (비교는 하네스가 주는 `KEY_LE` 매크로로 — 비교 횟수가 세진다). 한쪽 run 이 비어도 동작해야 한다.
- `msort_topdown(a, n, tmp)` — 재귀 `td_rec(a, tmp, lo, hi)`. `tmp` 는 호출자가 준 n 칸 scratch 하나뿐. 호출마다 `malloc` 하지 않는다.
- 테스트: 손 계산(키 2 가 세 번), INT32_MIN/MAX 극값, `n = 0` 에 NULL 포인터.

### Part B — bottom-up 반복 머지소트와 경계

- `msort_bottomup(a, n, tmp)` — 폭 w = 1, 2, 4, … 패스. 인접한 run 쌍을 `src → dst` 로 병합하고 버퍼를 바꾼다(ping-pong). 꼬리: 남은 원소가 w 이하면 짝 없이 복사, w 초과면 짧은 오른쪽 run 과 병합. 마지막 결과가 `tmp` 쪽에 있으면 `a` 로 복사.
- 경계 계산에서 `lo + w`, `2 * w` 가 `size_t` 를 넘지 않게 쓴다 (16비트 `size_t` MCU 에서 현실적인 문제).
- 테스트(Part A 와 공통): n = 0..64 **전수** × 5가지 입력(랜덤/정렬/역순/전부 같음/키 4종) + n = 1000, 4097, 100000. 매번 ① 키 순서 = qsort ② 레코드 보존(순열) ③ 안정성 ④ n ≤ 64 는 안정 삽입 정렬(ALGO-B05)과 **레코드 단위 완전 일치** ⑤ 배열·scratch 끝 바로 뒤 guard ⑥ 비교 횟수 ≤ n⌈lg n⌉. 그리고 top-down 과 bottom-up 결과가 레코드 단위로 같은지.

### Part C — 연결 리스트 bottom-up: 펌웨어 플래시 요청 큐

FTL 의 백그라운드 작업 큐를 생각하자. GC·호스트 쓰기가 만든 요청이 정적 풀(`fl_req_t pool[]`, DS-B07 참고)에서 할당되어 **도착 순서대로** 단일 연결 리스트에 매달려 있다. 블록 하나에는 `ERASE` 다음에 `PROGRAM page 0, 1, 2, …` 가 순서대로 도착하지만, 여러 블록의 요청이 뒤섞여 온다. 같은 블록의 일을 몰아서 처리하려고 **블록 번호로 정렬**한다.

- `list_split(head, k)` — 앞 k 개를 남기고 끊은 뒤 나머지의 head 를 돌려준다.
- `list_merge(a, b, &tail)` — 안정 병합(같은 블록이면 a 먼저), `next` 만 바꾼다. 결과의 마지막 노드도 알려 준다.
- `list_sort(head)` — bottom-up. 재귀 없음, 배열 없음, `malloc` 없음 → **추가 메모리 O(1)**.
- 테스트: 리스트판 전수/랜덤 대조(노드를 풀 안에 흩어 놓은 경우 포함), 모든 노드가 풀 안에 있고 정확히 n 개·NULL 로 끝나는지(사이클·유실 검사), 그리고 FTL 큐에서 정렬 뒤 **블록마다 ERASE → page 0, 1, 2 … 가 그대로**인지. 불안정 정렬이면 PROGRAM 이 ERASE 앞으로 갈 수 있고, NAND 는 지워지지 않은 페이지에 프로그램할 수 없으며 블록 안 페이지는 낮은 번호부터 순서대로 프로그램해야 한다.

## 2. Problem (English)

```c
typedef struct { int32_t key; uint32_t seq; } rec_t;

void merge_into(rec_t *dst, const rec_t *src, size_t lo, size_t mid, size_t hi);
void msort_topdown (rec_t *a, size_t n, rec_t *tmp);   /* tmp: caller-provided, n records */
void msort_bottomup(rec_t *a, size_t n, rec_t *tmp);   /* iterative, no recursion */

typedef struct fl_req { struct fl_req *next; uint32_t block, seq; uint16_t page; uint8_t op; } fl_req_t;
fl_req_t *list_sort(fl_req_t *head);                   /* O(1) extra memory, no recursion */
```

Sort ascending by key. All sorts must be **stable**: records with equal keys keep their input order. Array sorts may use only the scratch buffer passed in (no allocation); `tmp` may be NULL when `n < 2`. The list sort may only relink `next` pointers — no arrays, no allocation, no recursion. `n` can be any value including 0, 1 and non-powers of two; do not overflow `size_t` when computing run boundaries.

Example (key with input position as a letter):

```text
input : 5  2a 4  2b 1  2c
output: 1  2a 2b 2c 4  5        (2a, 2b, 2c keep their order)
list  : [blk 9: ERASE] [blk 3: ERASE] [blk 9: PROG p0] [blk 3: PROG p0] [blk 9: PROG p1]
sorted: [blk 3: ERASE] [blk 3: PROG p0] [blk 9: ERASE] [blk 9: PROG p0] [blk 9: PROG p1]
```

Follow-ups: How much extra memory and stack does each version use? Why must a *top-down* list sort use O(log n) stack? How do you verify stability when your reference `qsort` is not stable?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여섯 군데(`merge_into`, `td_rec`, `msort_bottomup`, `list_split`, `list_merge`, `list_sort`). 스텁은 아무것도 안 하거나 입력을 그대로 돌려주므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:264  rec_eq(dst, k, s, 6)
…
  top-down  340 runs: bad keys=196 perm=0 stable=0 ref=187 guard=0 cmp=0
FAIL starter.c:288  A.keys == 0
…
  list      335 runs: bad keys=193 perm=0 stable=0 ref=187 shape=0 cmp=0
  FTL queue: 8 blocks, 71 requests, block switches 49 -> 49
FAIL starter.c:367  grouped && seen == cnt && after == 7
FAIL starter.c:368  order_ok
16/32 passed
```

(exit 1. 통과한 16개는 "아무것도 안 하면 저절로 참" 인 검사 — 순열 보존, guard, 비교 횟수 상한, 안 움직인 배열의 안정성, 둘 다 안 움직여서 같은 top-down/bottom-up 결과 등.)

## 4. 힌트

<details><summary>힌트 1 — 병합의 불변식</summary>

커서 `i`(왼쪽), `j`(오른쪽), `k`(출력). 언제나 `dst[lo,k)` 는 지금까지 본 원소 중 가장 작은 `k−lo` 개가 정렬된 것이고, `k − lo = (i − lo) + (j − mid)` 다. 두 머리 중 작은 것을 내보낸다. **같으면 왼쪽** — 왼쪽 run 의 원소가 입력에서 먼저 왔기 때문이다. 한쪽이 끝나면 남은 쪽을 그대로 복사.

</details>

<details><summary>힌트 2 — top-down 에서 scratch 를 어떻게 쓰나</summary>

`td_rec(a, tmp, lo, hi)`: 두 쪽을 재귀로 정렬한 다음 `a[lo,hi)` 를 `tmp[lo,hi)` 로 `memcpy` 하고, `merge_into(a, tmp, lo, mid, hi)` 로 다시 `a` 에 병합한다. 같은 인덱스를 쓰므로 재귀 호출끼리 scratch 가 겹치지 않는다. mid 는 `lo + (hi − lo) / 2`.

</details>

<details><summary>힌트 3 — bottom-up 의 꼬리와 ping-pong</summary>

한 패스: `lo = 0` 부터 `mid = min(lo + w, n)`, `hi = min(mid + w, n)` 로 잘라 `merge_into(dst, src, lo, mid, hi)`, `lo = hi`. `mid == n` 이면 오른쪽이 빈 run — merge_into 가 그냥 복사해 준다. **이 복사를 건너뛰면** dst 의 그 칸은 지난 패스의 쓰레기로 남는다. 패스가 끝나면 `src`/`dst` 를 바꾸고, 다 끝났을 때 `src != a` 면 복사. min 은 `n − lo > w ? lo + w : n` 처럼 뺄셈으로 비교하면 덧셈 overflow 가 없다.

</details>

<details><summary>힌트 4 — 리스트: 포인터의 포인터</summary>

`list_split` 은 k−1 칸 가서 그 노드의 `next` 를 NULL 로 끊는다(안 끊으면 리스트가 이어져 병합이 사이클을 만든다). `list_merge` 는 `fl_req_t *head = NULL, **pp = &head;` 로 시작해 고른 노드를 `*pp` 에 걸고 `pp = &노드->next`. 남은 쪽은 통째로 `*pp = a ? a : b`. `list_sort` 는 길이 n 을 센 뒤 w = 1, 2, 4, … 마다 "w 개 떼기 → w 개 떼기 → 병합 → 결과 꼬리 뒤에 붙이기" 를 리스트 끝까지.

</details>

## 5. 풀이

**면접에서 말하는 순서**

| 단계 | 말할 것 |
|---|---|
| 1. 확인 질문 | 안정성이 필요한가? 메모리 예산(scratch n 칸 가능?) · 재귀 허용? · 데이터가 배열인가 리스트인가 · n 범위·키 타입(부호, 중복) |
| 2. 브루트포스 | 삽입 정렬 O(n²), 안정, 추가 메모리 O(1) — n ≤ 수십이면 이걸로 충분하다고 먼저 말한다 |
| 3. 더 나은 생각 + 불변식 | 정렬된 두 run 병합은 O(p+q). 크기를 두 배씩 → ⌈lg n⌉ 층 × 층당 n. 불변식: 출력 앞부분 = 본 것 중 가장 작은 것들, 같으면 왼쪽 |
| 4. 코드 | `merge_into` 먼저, 그 위에 top-down → bottom-up → 리스트 |
| 5. 소리 내어 말할 테스트 | n = 0, 1, 2, 5(2의 거듭제곱 아님), 전부 같은 키(안정성), 역순, 이미 정렬, INT32_MIN/MAX |
| 6. 복잡도 | 시간 O(n log n) 최악=평균. 추가: 배열 n 레코드 + 스택 ⌈lg n⌉+1 프레임(top-down) / 0(bottom-up). 리스트 bottom-up O(1) |

### Part A — 병합: 단계별 추적

`5 2a 4 2b 1 2c` 의 마지막 병합(왼쪽 `[2a 4 5]`, 오른쪽 `[1 2b 2c]`):

| 단계 | 왼쪽 머리 `src[i]` | 오른쪽 머리 `src[j]` | `KEY_LE` | 내보냄 | `dst` 까지 |
|---|---|---|---|---|---|
| 1 | 2a | 1 | 2 ≤ 1 거짓 | 1 (j++) | 1 |
| 2 | 2a | 2b | 2 ≤ 2 **참** | 2a (i++) | 1 2a |
| 3 | 4 | 2b | 4 ≤ 2 거짓 | 2b (j++) | 1 2a 2b |
| 4 | 4 | 2c | 4 ≤ 2 거짓 | 2c (j++) | 1 2a 2b 2c |
| 5 | 4 | (끝) | — | 4, 5 복사 | 1 2a 2b 2c 4 5 |

비교 4번 = 5 − 1 (오른쪽이 먼저 끝나서 왼쪽 2개는 비교 없이 복사). 병합 하나의 비교 횟수는 최소 min(p, q), 최대 p+q−1 이다. 2단계가 안정성의 전부다:

```svg
<svg viewBox="0 0 740 258" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 키를 만났을 때: &lt;= 는 왼쪽(먼저 온 것), &lt; 는 오른쪽</text>
  <text x="20" y="52" fill="currentColor">왼쪽 run</text>
  <g stroke="currentColor">
    <rect x="100" y="36" width="50" height="26" class="f-acc"/><rect x="150" y="36" width="50" height="26" class="f-mut"/><rect x="200" y="36" width="50" height="26" class="f-mut"/>
    <rect x="400" y="36" width="50" height="26" class="f-mut"/><rect x="450" y="36" width="50" height="26" class="f-acc"/><rect x="500" y="36" width="50" height="26" class="f-mut"/>
  </g>
  <text x="320" y="52" fill="currentColor">오른쪽 run</text>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="125" y="54">2a</text><text x="175" y="54">4</text><text x="225" y="54">5</text>
    <text x="425" y="54">1</text><text x="475" y="54">2b</text><text x="525" y="54">2c</text>
    <text x="125" y="84">i</text><text x="475" y="84">j</text>
  </g>
  <text x="580" y="52" fill="currentColor">1 은 이미 출력됨</text>
  <g stroke="currentColor" fill="none">
    <path d="M300 92 L170 128"/><path d="M300 92 L520 128"/>
  </g>
  <text x="250" y="110" fill="currentColor" text-anchor="middle">2 vs 2</text>
  <rect x="20" y="132" width="330" height="96" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="35" y="154" fill="currentColor" font-family="monospace">if (L.key &lt;= R.key) take L</text>
  <text x="35" y="178" fill="currentColor" font-family="monospace">1 2a 2b 2c 4 5</text>
  <text x="35" y="204" fill="currentColor">a 가 b 보다 먼저 → 입력 순서 유지 (안정)</text>
  <rect x="380" y="132" width="340" height="96" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="395" y="154" fill="currentColor" font-family="monospace">if (L.key &lt;  R.key) take L</text>
  <text x="395" y="178" fill="currentColor" font-family="monospace">1 2b 2c 2a 4 5</text>
  <text x="395" y="204" fill="currentColor">키 순서는 맞다 → qsort 대조로는 안 잡힌다</text>
  <text x="10" y="246" fill="currentColor">그래서 하네스는 키(qsort)와 안정성((key, seq) 검사)을 따로 본다</text>
</svg>
```

### Part A — top-down: 메모리와 깊이

`td_rec` 는 `[lo,mid)` 와 `[mid,hi)` 를 정렬한 뒤 `a[lo,hi)` 를 `tmp[lo,hi)` 로 복사하고 `tmp → a` 로 병합한다. 같은 인덱스 구간을 쓰므로 형제 호출이 scratch 를 겹쳐 쓰지 않고, scratch 는 전체에서 **n 칸 하나**면 된다. 호출마다 `malloc(hi−lo)` 하는 교과서 코드는 n−1 번 할당한다 — 펌웨어에선 단편화·실패 경로·비결정적 시간 때문에 금지(DS-M02).

- 시간: T(n) = T(⌈n/2⌉) + T(⌊n/2⌋) + O(n) → 층마다 복사 n + 비교 ≤ n, 층 ⌈lg n⌉ 개 → O(n log n), 입력과 무관(최악 = 평균).
- 스택: 깊이 d(n) = 1 + d(⌈n/2⌉), d(1) = 1 → **⌈lg n⌉ + 1** 프레임. `demo_bench.c` 실측: n = 10^6 에서 최대 깊이 21 (= ⌈lg 10^6⌉ + 1). 측정용 코드를 넣은 판에서 지역 변수 주소 차이로 어림한 스택 사용량 약 2.2 KB(프레임당 ~104 B, `-O2`, 이 맥 arm64) — 측정 코드가 프레임을 키우므로 대략값이다. 퀵소트(ALGO-B06)와 달리 입력이 무엇이든 깊이가 같다.

비교 횟수는 `demo_bench.c` 가 n = 1..10 의 **모든 순열**(10! = 3,628,800개)로 센 실제 값이다:

| n | top-down 최소 | top-down 최대 | 공식 n⌈lg n⌉ − 2^⌈lg n⌉ + 1 | bottom-up 최소 | bottom-up 최대 | n⌈lg n⌉ |
|---|---|---|---|---|---|---|
| 5 | 5 | 8 | 8 | 5 | **9** | 15 |
| 6 | 7 | 11 | 11 | 7 | 11 | 18 |
| 8 | 12 | 17 | 17 | 12 | 17 | 24 |
| 9 | 13 | 21 | 21 | 13 | **25** | 36 |
| 10 | 15 | 25 | 25 | 15 | **27** | 40 |

top-down 최악은 알려진 공식과 n = 1..10 전부에서 일치했다. bottom-up 은 n 이 2의 거듭제곱이면 같고, 아니면 기울어진 마지막 병합(n = 9: 8+1) 때문에 최악이 조금 크다. 하네스는 느슨한 상한 n⌈lg n⌉ 만 검사한다(층마다 비교 ≤ n). n = 1000 랜덤에서 실제 비교는 top-down 8718, bottom-up 8720 (상한 10000).

### Part B — bottom-up: 꼬리와 ping-pong 추적

```c
for (size_t w = 1; w < n; ) {
    for (size_t lo = 0; lo < n; ) {
        size_t mid = (n - lo > w) ? lo + w : n;    /* lo + w 를 먼저 계산하지 않는다 */
        size_t hi  = (n - mid > w) ? mid + w : n;
        merge_into(dst, src, lo, mid, hi);          /* mid == n 이면 그냥 복사 */
        lo = hi;
    }
    swap(src, dst);
    if (w > n / 2) break;                           /* 2w > n 이면 끝, 2w overflow 도 없음 */
    w *= 2;
}
if (src != a) memcpy(a, src, n * sizeof *a);
```

하네스의 n = 5 사례 (`3a 1a 3b 0 1b`, 위 그림 오른쪽):

| 패스 | w | src → dst | 구간 [lo,mid)+[mid,hi) | 결과 (dst) |
|---|---|---|---|---|
| 1 | 1 | a → tmp | [0,1)+[1,2), [2,3)+[3,4), [4,5)+∅ | 1a 3a · 0 3b · 1b |
| 2 | 2 | tmp → a | [0,2)+[2,4), [4,5)+∅ | 0 1a 3a 3b · 1b |
| 3 | 4 | a → tmp | [0,4)+[4,5) | 0 1a 1b 3a 3b |
| 끝 | — | 패스 3번(홀수) → 결과가 tmp | `memcpy(a, tmp)` | |

- **꼬리**: 패스 1·2 의 `1b` 는 짝이 없어도 dst 로 **복사돼야** 한다. ping-pong 에서는 안 옮긴 칸이 지난 패스의 값으로 남기 때문이다. 이걸 빠뜨린 변형은 실제로 340번 중 164번 키가 틀리고 105번은 레코드 자체가 사라졌다(7. 실수 3).
- **패스 수** = ⌈lg n⌉ (w 가 1 → 2^⌈lg n⌉−1 까지). 패스마다 정확히 n 개를 옮긴다(복사 포함). top-down 은 병합마다 `memcpy` + 병합으로 층마다 2n 번 옮기니, bottom-up ping-pong 이 옮기는 횟수가 대략 절반이다 — 아래 측정에서 bottom-up 이 10~14% 빨랐던 이유 중 하나로 보인다.
- **overflow**: 16비트 `size_t`(AVR 등)에서 n = 40000 이면 `lo + 2*w` 나 `(lo + hi) / 2` 가 65535 를 넘어 작은 값으로 돌아간다(부호 없는 정수는 모듈로 연산 — UB 는 아니지만 틀린 인덱스). 위 코드는 덧셈 전에 뺄셈으로 비교하고 `w > n/2` 에서 멈춰서 넘치지 않는다.
- **안정성은 공짜로 따라온다**: 항상 인접한 두 run 을 왼쪽·오른쪽 순서로 병합하니 `merge_into` 의 `<=` 만 지키면 된다. 그래서 하네스는 "안정 정렬의 결과는 하나뿐" 이라는 사실로 top-down 과 bottom-up 결과를 **레코드 단위로** 비교한다 (n = 100000, 키 4종 — 같은 키가 2.5만 개씩).

### Part C — 리스트 bottom-up: O(1) 메모리

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b07a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">패스 w = 2 의 중간: 앞 두 덩이는 병합 끝, 다음 덩이를 떼어 낸 순간</text>
  <text x="10" y="48" fill="currentColor" font-family="monospace">입력 5 1a 7 2 3 1b 4  →  w=1 뒤: [1a 5] [2 7] [1b 3] [4]</text>
  <text x="20" y="92" fill="currentColor" font-family="monospace">out</text>
  <g stroke="currentColor">
    <rect x="70" y="74" width="50" height="28" rx="4" class="f-ok"/><rect x="150" y="74" width="50" height="28" rx="4" class="f-ok"/>
    <rect x="230" y="74" width="50" height="28" rx="4" class="f-ok"/><rect x="310" y="74" width="50" height="28" rx="4" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="95" y="93">1a</text><text x="175" y="93">2</text><text x="255" y="93">5</text><text x="335" y="93">7</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M120 88 L148 88" marker-end="url(#b07a)"/><path d="M200 88 L228 88" marker-end="url(#b07a)"/><path d="M280 88 L308 88" marker-end="url(#b07a)"/>
    <path d="M360 88 L420 88" stroke-dasharray="4 3" marker-end="url(#b07a)"/>
  </g>
  <text x="315" y="124" fill="currentColor" font-family="monospace">tail</text>
  <text x="372" y="78" fill="currentColor" font-family="monospace">pp=&amp;tail-&gt;next</text>
  <text x="428" y="93" fill="currentColor">← 다음 병합 결과를 여기에 건다</text>
  <text x="20" y="172" fill="currentColor" font-family="monospace">a</text>
  <g stroke="currentColor">
    <rect x="70" y="154" width="50" height="28" rx="4" class="f-acc"/><rect x="150" y="154" width="50" height="28" rx="4" class="f-acc"/>
    <rect x="330" y="154" width="50" height="28" rx="4" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="95" y="173">1b</text><text x="175" y="173">3</text><text x="355" y="173">4</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M120 168 L148 168" marker-end="url(#b07a)"/><path d="M200 168 L240 168" marker-end="url(#b07a)"/>
    <path d="M380 168 L420 168" marker-end="url(#b07a)"/>
  </g>
  <text x="244" y="173" fill="currentColor" font-family="monospace">NULL</text>
  <text x="300" y="172" fill="currentColor" font-family="monospace">b</text>
  <text x="424" y="173" fill="currentColor" font-family="monospace">NULL</text>
  <text x="490" y="172" fill="currentColor" font-family="monospace">rest = NULL</text>
  <text x="10" y="214" fill="currentColor">a 는 2개에서 끊겼고 b 는 남은 1개뿐이라 rest = NULL. 다음: list_merge(a, b) = 1b 3 4 → *pp 에 건다.</text>
  <text x="10" y="236" fill="currentColor">w=4 패스: [1a 2 5 7] + [1b 3 4] → 1a 1b 2 3 4 5 7 (1a 가 1b 앞 = 안정)</text>
  <text x="10" y="258" fill="currentColor">쓰는 메모리: head·out·pp·a·b·rest·tail·last 포인터와 n·w 정수뿐 — 노드 수와 무관 = O(1)</text>
</svg>
```

**왜 bottom-up 이어야 O(1) 인가.** top-down 리스트 정렬(fast/slow 포인터로 가운데를 찾아 반씩 재귀)도 노드를 새로 만들지 않지만, 재귀 깊이가 ⌈lg n⌉ + 1 이라 **스택 O(log n)** 을 쓴다. 연습 3 에서 n = 100000 일 때 실제 깊이 18 을 쟀다. 면접에서 "O(1) 추가 메모리" 를 요구하면(LeetCode 148 Sort List 의 follow-up) 엄밀히는 이 bottom-up 판이 답이다. O(log n) 은 n = 10^6 에서도 21 프레임이라 실무에서는 대개 문제가 아니지만, 태스크 스택이 1 KB 인 RTOS 에서 프레임 ~100 B × 21 은 이미 2 KB 다.

**시간.** 패스마다 `list_split` 이 노드를 한 번 지나가고(끊을 자리 찾기), `list_merge` 가 한 번 더 지나가고, 남은 쪽 끝을 찾느라 일부를 또 지나간다. 노드 방문은 패스당 2n~3n, 패스 ⌈lg n⌉ 개 → O(n log n). 비교 횟수는 같은 병합 나무를 쓰는 배열 bottom-up 과 **정확히 같다** (아래 측정: 둘 다 18,715,228).

**펌웨어 큐 결과** (solution.c 실제 출력):

```text
  FTL queue: 8 blocks, 71 requests, block switches 49 -> 7
```

도착 순서에서는 인접한 두 요청의 블록이 49번 바뀌었고, 정렬 뒤에는 블록 8개 사이의 경계 7번뿐이다. 하네스는 블록마다 첫 요청이 ERASE 이고 그 뒤 PROGRAM 페이지가 0, 1, 2, … 로 이어지는지 검사한다. `list_merge` 의 `<=` 를 `<` 로 바꾼 변형을 실제로 돌리면 이 검사가 FAIL 한다(28/32).

실무 메모:

- 이 리스트에 ISR(또는 호스트 명령 처리 태스크)이 계속 요청을 붙인다면, 정렬하는 동안 리스트가 바뀌면 안 된다. 임계 구역에서 **리스트 전체를 떼어 내고**(head 포인터 하나를 NULL 로 바꾸기 — O(1)), 임계 구역 밖에서 정렬한 뒤 처리한다. O(n log n) 정렬을 인터럽트를 막은 채 하지 않는다(C-M14).
- 블록별로 묶는 것이 이득인지는 컨트롤러마다 다르다 — 다이(die)·플레인 병렬성을 살리려면 오히려 여러 다이에 번갈아 보내야 한다. 이 레슨이 보장하는 것은 "어떤 키로 묶든 같은 키 안의 순서는 그대로" 다.

### 측정 — `demo_bench.c`

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

이 맥(Apple M6, Apple clang 21.0.0, `-O2`), n = 10^6 레코드(int32 키 + uint32 seq, 8 B), 워밍업 1회 + 7회 중앙값. 측정할 때 load average 가 6~8 이었다(다른 작업이 같이 돌았음) — **비율**만 믿을 것. 세 번 돌린 결과:

| 알고리즘 | 1회 ms | 2회 ms | 3회 ms | top-down 대비 | 키 비교 |
|---|---|---|---|---|---|
| top-down (배열) | 49.3 | 46.1 | 55.0 | 1.00 | 18,673,399 |
| bottom-up (배열) | 42.8 | 41.5 | 47.3 | 0.86~0.90 | 18,715,228 |
| 리스트, 노드가 풀에 순서대로 | 166.6 | 169.2 | 207.8 | 3.4~3.8 | 18,715,228 |
| 리스트, 노드가 풀에 흩어짐 | 317.1 | 810.6 | 576.0 | 6.4~17.6 | 18,715,228 |
| qsort (배열, macOS libc) | 60.8 | 60.6 | 68.5 | 1.23~1.31 | — |

읽을 것:

- 비교 횟수는 셋 다 n⌈lg n⌉ = 2×10^7 의 93% 정도로 거의 같다. **시간 차이는 비교가 아니라 메모리 접근**에서 나온다.
- 리스트는 노드가 순서대로 놓여 있어도 3~4배 느리다. 다음 노드 주소를 알아야 다음 load 를 할 수 있는 **의존 load 사슬**(pointer chasing)이라 메모리 지연을 숨기기 어렵고, 첫 패스 뒤로는 리스트 순서와 메모리 순서가 어긋난다. 노드가 흩어져 있으면 거의 매 노드가 캐시 미스라 6~18배 — 그리고 다른 작업의 부하에 따라 크게 흔들렸다(317 ↔ 811 ms).
- 그래서 "리스트는 머지소트" 는 **이미 리스트로 존재하는 데이터를 옮기지 않고** 정렬할 때의 답이다. RAM 이 있으면 포인터를 배열에 모아 배열 정렬한 뒤 다시 잇는 편이 빠를 수 있다(대신 n 칸 메모리).
- qsort 는 비교 함수 간접 호출 비용이 있고, macOS 의 `qsort(3)` 매뉴얼은 qsort 가 안정적이지 않다고 적고 있다. 같은 매뉴얼에 BSD 확장 `mergesort()`(안정, nel × width 추가 메모리)가 있다.

### 정리 표

| 버전 | 시간 (최악 = 평균) | 추가 메모리 | 스택 | 안정 |
|---|---|---|---|---|
| top-down 배열 | O(n log n) | scratch n 레코드 | ⌈lg n⌉ + 1 프레임 | ✓ |
| bottom-up 배열 | O(n log n) | scratch n 레코드 | O(1) | ✓ |
| 리스트 bottom-up (Part C) | O(n log n) | **O(1)** | O(1) | ✓ |
| 리스트 top-down (연습 3) | O(n log n) | 노드 할당 없음 | ⌈lg n⌉ + 1 프레임 | ✓ |
| 참고: 퀵소트 (ALGO-B06) | 평균 O(n log n), 최악 O(n²) | O(1) | O(log n) (작은 쪽 먼저) | ✗ |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_san && ./solution_san   # 보고 없음
```

실제 출력:

```text
Part A/B suites (n=0..64 x 5 patterns + n=1000,4097,100000 x 5):
  top-down  340 runs: bad keys=0 perm=0 stable=0 ref=0 guard=0 cmp=0
  bottom-up 340 runs: bad keys=0 perm=0 stable=0 ref=0 guard=0 cmp=0
  comparisons: n=100000 4-keys td=1378087 bu=1416821 | n=1000 random td=8718 bu=8720 (bound n*ceil(lg n)=10000)
  list      335 runs: bad keys=0 perm=0 stable=0 ref=0 shape=0 cmp=0
  FTL queue: 8 blocks, 71 requests, block switches 49 -> 7
32/32 passed
```

(약 0.3 초. ASan/UBSan 빌드도 32/32, 보고 없음.)

## 7. 자주 하는 실수

아래 숫자는 solution.c 에서 해당 줄만 바꾼 변형을 실제로 돌린 결과다.

**1. 병합에서 `<` 를 쓴다.** 같은 키면 오른쪽이 먼저 나가 안정성이 깨진다. 키 순서는 여전히 맞아서 **qsort 대조는 통과**한다 — 실제로 `bad keys=0` 인데 `stable=130`(340번 중), 24/32. 리스트판도 같은 실수면 FTL 큐에서 PROGRAM 이 ERASE 앞으로 간다(`order_ok` FAIL). → `<=`. 안정성은 키 대조와 **따로** 검사한다.

**2. bottom-up 에서 마지막 복사를 잊는다.** 패스 수가 홀수(⌈lg n⌉ 이 홀수, 예: n = 5, 1000)면 결과가 tmp 에 있다. 실제: bottom-up 340번 중 67번 키 불일치, 28/32. → `if (src != a) memcpy(a, src, …)`.

**3. ping-pong 에서 짝 없는 꼬리 run 을 건너뛴다.**

```c
for (size_t lo = 0; n - lo > w; ) {   /* 틀림: 오른쪽 run 이 없으면 아예 안 옮긴다 */
```

top-down 이나 "복사 후 제자리 병합" 방식이면 꼬리는 이미 제자리라 괜찮지만, ping-pong 은 dst 의 그 칸이 지난 패스 값으로 남는다. 실제: 340번 중 키 불일치 164, 레코드 유실 105. → `lo < n` 까지 돌고 빈 오른쪽 run 은 복사.

**4. `list_split` 에서 끊지 않는다 (`head->next = NULL` 누락).** 앞 덩이가 리스트 나머지와 이어진 채 병합되어 사이클이 생긴다. 실제: 프로그램이 끝나지 않아 10초 타임아웃으로 죽였다(SIGALRM, exit 142), 출력 버퍼도 비워지지 않아 한 줄도 안 나왔다. → 끊고, 테스트는 "최대 n 걸음 안에 NULL" 로 사이클을 막는다.

**5. 병합 함수 안에서 매번 `malloc`.** 교과서 C 코드에 흔하다. n − 1 번 할당, 실패 경로, 단편화, 실행 시간 흔들림 — 펌웨어에서는 정렬 하나가 힙을 망친다. → scratch 를 호출자가 한 번 준다(정적 버퍼 가능). 리스트면 scratch 자체가 필요 없다.

**6. `mid = (lo + hi) / 2` 와 `2 * w`.** 32/64비트 `size_t` 에선 현실적인 n 으로 안 터지지만, 16비트 `size_t` 에서는 n > 32767 이면 넘친다. 부호 없는 덧셈은 UB 가 아니라 조용히 틀린 인덱스가 된다. → `lo + (hi − lo) / 2`, 덧셈 전에 `n − lo > w` 로 비교.

**7. qsort 비교 함수를 `return x - y;` 로 쓴다.** int32 차이는 넘칠 수 있다(부호 있는 overflow = UB). 이 하네스의 랜덤 키는 0..2^31−1 이라 차이가 int32 범위 안이어서 실제로 이 변형은 **잡히지 않았다**(32/32). 키에 INT32_MIN 과 양수가 섞이면 틀린다. → `(x > y) - (x < y)`. 테스트 데이터가 버그를 숨길 수 있다는 예.

**8. "리스트 머지소트는 O(1) 메모리" 라고 top-down 재귀판을 낸다.** 노드 할당은 없지만 스택이 ⌈lg n⌉ + 1 프레임이다(연습 3 실측). → O(1) 이 조건이면 bottom-up.

## 8. 면접 Q&A

<details><summary>Q1. 연결 리스트는 왜 머지소트로 정렬하나요? 퀵소트·힙소트는요?</summary>

머지소트는 두 run 을 **앞에서부터 순서대로** 읽기만 하면 된다 — 리스트가 잘하는 유일한 접근이다. 그리고 병합이 `next` 재연결만으로 되니 배열판의 n 칸 scratch 가 사라진다(bottom-up 이면 추가 메모리 O(1)). 안정성도 유지된다. 힙소트는 i → 2i+1 같은 **임의 접근**이 핵심이라 리스트에서는 접근마다 O(n) 이다. 퀵소트는 리스트에서도 파티션은 가능하지만(피벗보다 작은/큰 리스트로 나누기) 피벗을 무작위로 고르거나 median-of-3 을 하려면 임의 접근이 필요하고, 최악 O(n²)·불안정(3-리스트 분할로 안정하게 만들 수는 있음)·재귀 깊이 문제가 남는다. 꼬리: "실제로는?" → 측정에서 리스트 정렬은 배열의 3~18배 느렸다. RAM 이 있으면 포인터를 배열로 모아 정렬하고 다시 잇는 것도 답이다.

</details>

<details><summary>Q2. Linux 커널의 lib/list_sort.c 는 어떤 알고리즘인가요?</summary>

확실히 말할 수 있는 것: 커널의 `list_sort()` 는 `struct list_head` 이중 연결 리스트(DS-M01)를 정렬하는 **안정 머지소트**이고, 재귀나 메모리 할당 없이 **bottom-up** 으로 동작한다. 정렬하는 동안에는 리스트를 NULL 로 끝나는 단일 연결 리스트처럼 다루고(`next` 만 사용), 마지막 병합에서 `prev` 포인터를 다시 채운다. 비교 함수는 "a 가 b 뒤로 가야 하면 > 0" 을 돌려주고 같으면 원래 순서를 지킨다.

내 기억으로는(원문 주석 확인 권장) 2019년 무렵 재작성된 버전은 단순한 "패스마다 폭 두 배" 가 아니라, 대기 중인 run 들을 2의 거듭제곱 크기로 유지하며 크기 비가 2:1 을 넘지 않게 **가능한 한 일찍** 병합한다 — 이진 카운터처럼. 이유는 캐시: 패스 방식은 매 패스가 리스트 전체를 훑어 n 이 캐시보다 크면 매번 캐시를 다 갈아엎지만, 이 방식은 최근에 만든 작은 run 들이 캐시에 있을 때 병합한다. 함수 원형(비교 함수 타입)은 커널 버전마다 조금 다르다. 이 레슨의 `list_sort` 는 같은 계열의 가장 단순한 형태(길이를 세고 폭을 두 배씩)다.

</details>

<details><summary>Q3. 배열 머지소트의 O(n) 추가 메모리, 줄일 수 있나요? RAM 20 KB 짜리 MCU 라면?</summary>

단계별로: (1) **n/2 로**: 왼쪽 run 만 scratch 로 복사하고 앞에서부터 병합하면 오른쪽은 덮이기 전에 읽힌다(연습 2, 쓰는 위치 k ≤ j 가 불변식). (2) **레코드 대신 인덱스를 정렬**: 32 B 레코드 2000개 = 64 KB 는 불가능해도 `uint16_t` 인덱스 2000개 + scratch = 8 KB 면 된다. (3) **리스트로**: 이미 노드 풀이면 Part C, scratch 0. (4) 제자리(in-place) 안정 병합 알고리즘도 있지만(회전 기반, 블록 병합 등) 복잡하고 상수가 크다 — 면접에선 "존재한다, 하지만 보통 안 쓴다" 정도. (5) 안정성이 필요 없으면 힙소트(O(1), 최악 O(n log n)). 안정성이 필요한데 메모리가 없으면 키에 seq 를 붙여 `(key, seq)` 로 비교하면 어떤 정렬이든 안정해진다 — 레코드당 seq 바이트가 비용.

</details>

<details><summary>Q4. 안정성은 왜 중요하고, qsort 로 어떻게 검증하나요?</summary>

여러 키로 정렬할 때(먼저 페이지 순, 그다음 블록 순으로 안정 정렬 → 블록 안에서 페이지 순 유지 = 기수 정렬 ALGO-B08 의 원리), 그리고 입력 순서 자체가 의미일 때(같은 블록의 ERASE → PROGRAM, 같은 타임스탬프 로그 레코드의 도착 순서). C 표준은 qsort 에서 같다고 비교되는 원소의 순서를 정하지 않는다(C11 7.22.5.2: unspecified). macOS `qsort(3)` 매뉴얼도 안정적이지 않다고 적는다. 그래서 qsort 결과와는 **키 배열만** 비교하고, 안정성은 출력에서 "같은 키가 인접하면 seq 오름차순" 으로 따로 검사한다. 더 강한 검사: 안정 정렬의 결과는 유일하므로 작은 n 에서 안정 삽입 정렬과 레코드 단위로 같아야 한다. 꼬리: "`<` 로 짜면 어떻게 되나?" → 전부 같은 키 8개가 seq 7, 6, …, 0 으로 **완전히 뒤집힌다**(연습 1 실측) — 병합마다 오른쪽 run 전체가 왼쪽 앞으로 가기 때문.

</details>

<details><summary>Q5. RAM 64 KB 로 플래시의 로그 64 MB 를 타임스탬프 순으로 정렬하려면?</summary>

**외부 정렬(external merge sort)**. ① run 만들기: RAM 에 들어가는 만큼 읽어 정렬하고 플래시에 run 으로 쓴다. 배열 머지소트는 scratch 가 같은 크기만큼 필요하니 run 은 32 KB → run 2048개 (인덱스 정렬이나 힙으로 run 을 키울 수 있다 — 교체 선택(replacement selection)은 랜덤 입력에서 평균 RAM 의 2배 길이 run 을 만든다고 알려져 있다, Knuth). ② k-way 병합: run 마다 입력 버퍼 하나 + 출력 버퍼 하나. 4 KB 페이지 버퍼면 k = 15. 패스 수 = ⌈log_15 2048⌉ = 3 (15² = 225 < 2048 ≤ 15³ = 3375). 매 패스가 64 MB 를 읽고 쓴다. ③ k 개 머리 중 최소: k 가 작으면 선형(연습 5), 크면 힙(DS-B12) 으로 출력 하나당 O(log k). 같은 키면 앞 run 을 고르면 안정.

플래시 관점: 쓰기는 마모(P/E 사이클)와 GC 를 부르니 **패스 수를 줄이는 것**이 핵심 — k 를 키우려고 버퍼를 줄이면 NAND 는 어차피 페이지 단위로 읽으므로 같은 페이지를 여러 번 읽게 된다. 버퍼 크기 = 페이지 크기가 보통 균형점이다. 로그가 블록마다 이미 시간순이면(블록 = 정렬된 run) ①을 건너뛰고 바로 병합한다 — FTL 의 로그 블록 병합(merge)과 같은 그림이다.

</details>

<details><summary>Q6. top-down 과 bottom-up, 무엇을 고르겠어요?</summary>

같은 O(n log n)·같은 scratch. 차이: (1) **스택**: top-down ⌈lg n⌉ + 1 프레임(10^6 에서 21, 예측 가능해서 보통 괜찮음), bottom-up 0 — MISRA 처럼 재귀 금지 규칙이 있으면 bottom-up(C-A10). (2) **병합 나무**: n 이 2의 거듭제곱이 아니면 bottom-up 은 기울어진 병합을 해 최악 비교가 조금 많다(n = 9: 25 vs 21, 전 순열 실측). (3) **캐시**: top-down 은 깊이 우선이라 작은 부분 배열을 캐시에 있을 때 끝낸다. bottom-up 은 패스마다 전체를 훑는다. 그래도 이 맥에선 ping-pong 덕분에(옮기는 횟수 절반) bottom-up 이 10~14% 빨랐다. (4) **코드**: bottom-up 은 루프 두 개라 실행 시간 상한을 정적으로 분석하기 쉽다. 꼬리: "작은 구간은?" → 실무 구현은 run 이 16~32 이하면 삽입 정렬로 처리한다(ALGO-B05, 상수 감소).

</details>

<details><summary>Q7. 퀵소트가 평균적으로 빠르다던데, 펌웨어에서 머지소트를 고르는 때는?</summary>

**최악 시간이 보장돼야 할 때**: 머지소트는 입력과 무관하게 n⌈lg n⌉ 비교 이하·⌈lg n⌉ 패스 — 실행 시간 상한(WCET)을 말할 수 있다. 퀵소트는 피벗 운이 나쁘면 O(n²)·깊은 재귀(ALGO-B06). **안정성이 필요할 때**, **데이터가 리스트일 때**, **외부 정렬**(순차 접근만 하는 매체)일 때도 머지. 반대로 RAM 이 빠듯하고 안정성이 필요 없으면 힙소트나 introsort 계열(퀵 + 깊이 제한 시 힙)을 쓴다. 이 맥의 측정에서는 배열 머지소트가 libc qsort 보다 빨랐는데(1.2~1.3배), 이건 qsort 의 비교 함수 간접 호출 비용 영향이 커서 알고리즘 비교로 읽으면 안 된다.

</details>

<details><summary>Q8. 8/16비트 MCU 로 옮기면 무엇이 바뀌나요? 정렬 중 ISR 이 큐에 요청을 넣으면요?</summary>

16비트 `size_t` 에서 `(lo + hi) / 2`·`lo + 2w` overflow(7. 실수 6). 8비트 코어에서 int32 키 비교는 여러 바이트 비교라 비교 비용이 커진다 — 키를 `uint16_t` 로 줄일 수 있으면 줄인다. 하드웨어 나눗셈은 필요 없다: `/ 2` 는 시프트다(부호 없는 타입이면 컴파일러가 시프트로 바꾼다). ISR: 정렬 중인 리스트에 ISR 이 노드를 붙이면 경쟁 상태다. 인터럽트를 막고 head 를 떼어 와(`list = pending; pending = NULL;` O(1)) 막은 구간을 짧게 하고, 정렬은 밖에서. 새로 들어온 요청은 다음 라운드에. 정렬된 결과를 다시 큐 앞에 붙여야 하면 같은 방식으로 짧은 임계 구역에서 연결만 한다.

</details>

## 9. 추가 연습

### 연습 1 — `<` 병합이 무엇을 망가뜨리나

`merge_lt`(같으면 오른쪽 먼저)로 top-down 정렬을 만들어 ① 키 4종 랜덤 배열 200개에서 키 순서는 맞고 안정성은 깨짐 ② 키가 전부 다르면 `<=` 판과 결과가 같음 ③ 같은 키 8개면 seq 가 어떻게 되는지 확인하라.

<details><summary>답 보기</summary>

실제 출력: `'<' merge on 200 random 4-key arrays: keys sorted=yes, stability broken in 198/200`. 키가 다르면 같은 키 비교가 없으니 두 판은 같다. 같은 키 8개는 seq 7, 6, 5, …, 0 — 모든 병합에서 오른쪽 run 이 통째로 앞에 가므로 n = 2^k 이면 완전히 뒤집힌다. "키 대조만 하는 테스트는 안정성 버그를 못 잡는다" 의 가장 짧은 증거.

</details>

### 연습 2 — scratch 를 n/2 칸으로

top-down 에서 왼쪽 run 만 `tmp[0, L)` 로 복사하고 `tmp` 와 `a[mid,hi)` 를 `a[lo..)` 에 병합하라. scratch 는 ⌊n/2⌋ 칸이고 그 바로 뒤 guard 가 그대로여야 한다.

<details><summary>답 보기</summary>

쓰는 위치 `k = lo + i + (j − mid)` 이고 `i ≤ L = mid − lo` 이므로 `k ≤ j`. 즉 아직 안 읽은 오른쪽 원소 `a[j]` 를 덮지 않는다. 왼쪽이 다 나가면 오른쪽 나머지는 이미 제자리라 복사도 필요 없다. 같은 키에서 `tmp[i].key <= a[j].key` 로 왼쪽 먼저 → 안정. top-level 의 왼쪽 크기가 ⌊n/2⌋ 로 가장 크다(mid = lo + (hi−lo)/2). n = 0..64 × 5 패턴을 안정 삽입 정렬과 레코드 단위 대조, n = 100000 에서 guard 확인 — 전부 통과.

```c
size_t L = mid - lo, i = 0, j = mid, k = lo;
memcpy(tmp, a + lo, L * sizeof *a);
while (i < L && j < hi) a[k++] = (tmp[i].key <= a[j].key) ? tmp[i++] : a[j++];
while (i < L) a[k++] = tmp[i++];
```

</details>

### 연습 3 — top-down 리스트 정렬과 재귀 깊이 실측

fast/slow 포인터로 가운데를 찾아 반으로 끊고 재귀로 정렬해 병합하라. n = 0..2000 의 최대 재귀 깊이가 ⌈lg n⌉ + 1 (n < 2 이면 1) 과 같은지, n = 100000 에서 몇인지 재라.

<details><summary>답 보기</summary>

`slow = h, fast = h->next` 로 시작하면 앞쪽이 ⌈n/2⌉ 개 → d(n) = 1 + d(⌈n/2⌉) = ⌈lg n⌉ + 1. 실제 출력: `top-down list sort n=100000 max depth=18 (ceil(lg n)+1=18)`, n = 0..2000 전부 일치. 노드 할당은 없지만 스택은 O(log n) — Part C 가 bottom-up 인 이유. (`fast = h` 로 시작하면 n = 2 에서 앞쪽이 2개가 되어 무한 재귀 — 흔한 버그.)

</details>

### 연습 4 — 역전 쌍 세기: 타임스탬프가 얼마나 뒤섞였나

여러 센서 노드가 보낸 CAN 프레임을 받은 순서대로 기록했다. i < j 인데 `t[i] > t[j]` 인 쌍(역전, inversion)의 수는 "도착 순서가 얼마나 어긋났나" 의 지표다. 머지소트 병합 중에 세어 O(n log n) 으로 구하고 O(n²) 브루트포스와 대조하라.

<details><summary>답 보기</summary>

병합에서 오른쪽 원소 `t[j]` 를 내보낼 때, 왼쪽에 남은 `mid − i` 개가 모두 그보다 크다 → `c += mid - i`. 같은 키는 역전이 아니므로 반드시 `<=` 로 왼쪽을 먼저 내보내야 한다 — `<` 면 같은 키 쌍을 역전으로 센다(안정성과 같은 한 글자). 개수는 최대 n(n−1)/2 라 `uint64_t`. 확인: 역순이면 n(n−1)/2, 전부 같으면 0. 실제 출력 `n=3000 4-key inversions brute=1691380 merge=1691380`. 응용: LeetCode 493 Reverse Pairs, 315 Count of Smaller Numbers After Self 가 같은 "병합 중에 세기" 틀이다.

</details>

### 연습 5 — k-way 병합: 플래시의 정렬된 run 합치기

정렬된 run k 개(k ≤ 8, 빈 run 포함)가 이어져 있다. run 마다 커서 하나만 두고, 매번 k 개 머리 중 최소를 선형으로 골라 내보내라. 같은 키면 번호가 작은 run. 결과를 안정 정렬 기준과 레코드 단위로 대조하고, 비교 횟수 ≤ (k−1)·n 을 확인하라.

<details><summary>답 보기</summary>

"엄격히 작을 때만 best 를 바꾼다" 가 안정성 규칙이다 — run 번호가 작을수록 원래 앞쪽이므로. 출력 하나당 비교 ≤ k−1. 300번 랜덤(k = 1..8, run 길이 0..39) 전부 일치. k 가 크면 머리들을 최소 힙(DS-B12)에 넣어 출력당 O(log k), 키가 같을 때는 (key, run 번호) 로 비교해야 힙에서도 안정하다. 이것이 Q5 외부 정렬의 병합 단계이고 LeetCode 23 Merge k Sorted Lists 와 같은 문제다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_san && ./practice_san   # 11/11, 보고 없음
```

## 10. 핵심 정리

- 머지소트 = 정렬된 두 run 의 O(p+q) 병합을 ⌈lg n⌉ 층 반복. 최악 = 평균 O(n log n), 비교 ≤ n⌈lg n⌉ (top-down 최악 n⌈lg n⌉ − 2^⌈lg n⌉ + 1, n ≤ 10 전 순열로 확인).
- 안정성은 병합의 `<=` 한 글자. `<` 는 키 순서가 맞아서 qsort 대조로 안 잡힌다 → (key, seq) 로 따로 검사, 작은 n 은 안정 기준 정렬과 레코드 단위 비교(안정 정렬 결과는 유일).
- 배열: scratch n 칸을 **호출자가 한 번** 준다(호출마다 malloc 금지). top-down 스택 ⌈lg n⌉+1, bottom-up 0.
- bottom-up 경계: `mid = n − lo > w ? lo + w : n`, 짝 없는 꼬리도 복사(ping-pong), 패스 수가 홀수면 마지막에 tmp → a, `w > n/2` 에서 멈춰 overflow 방지.
- 리스트: split(끊기!) + merge(`**pp`) + 폭 두 배 = 추가 메모리 O(1)·재귀 없음. top-down 리스트는 스택 O(log n).
- 측정: 비교 횟수는 같아도 리스트는 pointer chasing 때문에 배열의 3~18배 느림 — 리스트 머지소트는 "옮기지 않고 정렬" 할 때의 답.
- 펌웨어: 같은 블록의 ERASE → PROGRAM 순서·로그 도착 순서를 지키려면 안정 정렬. 큰 데이터는 외부 정렬(run 만들기 + k-way 병합, 패스 수 = 플래시 쓰기량).

## 참고자료

- D. E. Knuth, *The Art of Computer Programming, Vol. 3: Sorting and Searching*, 2nd ed., 5.2.4 (Sorting by Merging) — 머지소트 비교 횟수, 5.4 (External Sorting) — run 생성·교체 선택·다방향 병합.
- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms*, 2.3 "Designing algorithms" — 분할 정복과 MERGE-SORT, 점화식 T(n) = 2T(n/2) + Θ(n).
- R. Sedgewick, K. Wayne, *Algorithms*, 4th ed., 2.2 Mergesort — top-down/bottom-up, 작은 부분 배열 삽입 정렬, 리스트 bottom-up 머지소트 연습문제.
- ISO/IEC 9899:2011 (C11) 7.22.5.2 The qsort function — 같다고 비교되는 원소의 순서는 unspecified.
- macOS `man 3 qsort` — qsort/heapsort 는 안정적이지 않고 mergesort 는 안정적이며 nel × width 추가 메모리가 필요하다는 설명 (이 맥에서 확인).
- Linux kernel `lib/list_sort.c` — 소스 상단 주석(안정성, 비교 함수 규약, 병합 균형과 캐시에 대한 설명). 이 레슨의 설명은 기억에 의존한 부분이 있으니 원문으로 확인: <https://github.com/torvalds/linux/blob/master/lib/list_sort.c>
- LeetCode 148 Sort List · 912 Sort an Array · 21 Merge Two Sorted Lists · 88 Merge Sorted Array · 23 Merge k Sorted Lists · 493 Reverse Pairs · 315 Count of Smaller Numbers After Self.
- AlgoMonster 노트 M01 §15 Advanced Sorting Algorithms — Merge Sort | Quick Sort, §16 Sorting Summary (Don 정리본).
- 관련 레슨: ALGO-B05 (삽입 정렬·안정성 검사), ALGO-B06 (퀵소트·스택 깊이), ALGO-B08 (계수·기수 정렬 — 안정 정렬을 단계로 쓰는 예), DS-B05/B07 (단일 연결 리스트·정적 노드 풀), DS-M01 (침입형 리스트), DS-M02 (고정 크기 메모리 풀), DS-B12 (이진 힙 — k-way 병합), C-M14 (ISR 공유 데이터), C-A10 (재귀 금지 규칙), DS-A09 (FTL 매핑·GC).
