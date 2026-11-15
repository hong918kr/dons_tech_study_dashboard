---
id: ALGO-M10
time: 80분
updated: 2026-11-11
---
# ALGO-M10 스케줄링 문제: 쿨다운 태스크·마감시간 작업 선택

## 학습 목표

- 쿨다운 태스크 스케줄러(LeetCode 621)의 답 `max(n, (f-1)(c+1) + m)` 을 **하한 증명 + 상한 증명**으로 설명하고, 최대 힙 라운드 시뮬레이션으로 **실제 실행 순서**(IDLE 포함)를 만들어 독립 검증기로 확인한다.
- 마감시간·이익이 있는 단위 작업 선택(job sequencing)을 두 방법 — 이익 내림차순 + "마감 이하 가장 늦은 빈 슬롯"(DSU), 마감 오름차순 + 이익 최소 힙 — 으로 풀고, 실행 가능 조건 `∀t: |{d ≤ t}| ≤ t` 와 교환 논증(exchange argument)으로 최적성을 증명한다.
- "그럴듯한" 틀린 그리디(개수 무시 라운드 로빈, 가장 이른 슬롯, 교체 없는 마감 순서 수락)의 **가장 작은 반례**를 브루트포스로 찾아 말할 수 있다.
- 펌웨어 상황(I2C EEPROM 쓰기 사이클 = 쿨다운, 다운링크 프레임 = 마감 슬롯)에 적용하고, 이것이 RTOS 의 RMS/EDF 스케줄 가능성 분석과 **다른 문제**(오프라인 배치 계획)라는 점을 구분한다.

## 1. 문제

스케줄링 문제는 "정렬 + 힙 + 그리디" 를 한 번에 묻고, 그리디가 **왜** 맞는지(교환 논증)까지 꼬리질문이 이어지는 단골 미디엄 문제다 — 펌웨어 면접에서는 "같은 장치를 연속으로 못 건드린다(쓰기 사이클, 정착 시간)" 와 "마감 안에 못 보내면 쓸모없는 패킷" 으로 그대로 나온다.

두 문제를 다룬다.

1. **쿨다운 스케줄러.** 작업 종류 `A..Z` 의 목록이 있다. 한 칸(slot)에 작업 하나 또는 IDLE. 같은 종류 두 작업 사이에는 **최소 c 칸**이 비어야 한다(= 시각 차이 ≥ c+1). 전부 끝내는 최소 칸 수와 실제 순서를 구한다. 가장 많은 종류의 개수를 f, 그 개수를 가진 종류 수를 m 이라 하면 답은 `max(n, (f-1)(c+1) + m)` 이다. 아래 그림의 "프레임" 그림이 공식의 직관이다.
2. **마감·이익 작업 선택.** 단위 시간 작업 n 개, 작업 j 는 마감 `d_j`(슬롯 1..d_j 중 하나에서 실행해야 함)와 이익 `p_j`. 한 슬롯에 하나. 총 이익을 최대로 하는 작업 집합과 슬롯 배치를 구한다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m10a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">AAABBBC, c = 2: 프레임 그림 → 길이 = (f-1)(c+1) + m = 2·3 + 2 = 8</text>
  <g stroke="currentColor">
    <rect x="60" y="50" width="44" height="34" class="f-acc"/><rect x="104" y="50" width="44" height="34" class="f-acc"/><rect x="148" y="50" width="44" height="34" class="f-ok"/>
    <rect x="60" y="84" width="44" height="34" class="f-acc"/><rect x="104" y="84" width="44" height="34" class="f-acc"/><rect x="148" y="84" width="44" height="34" class="f-mut"/>
    <rect x="60" y="118" width="44" height="34" class="f-acc"/><rect x="104" y="118" width="44" height="34" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="82" y="72">A</text><text x="126" y="72">B</text><text x="170" y="72">C</text>
    <text x="82" y="106">A</text><text x="126" y="106">B</text><text x="170" y="106">_</text>
    <text x="82" y="140">A</text><text x="126" y="140">B</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="60" y="44">c + 1 = 3 칸 (한 프레임)</text>
    <text x="200" y="72">프레임 1</text><text x="200" y="106">프레임 2 (f-1 = 2 개)</text>
    <text x="200" y="140">마지막 줄: m = 2 (A, B)</text>
    <text x="40" y="180">최대 종류는 매 프레임 맨 앞에 → 같은 열이라 간격이 정확히 c+1. 다른 작업은 빈칸을 채운다.</text>
    <text x="40" y="198">빈칸보다 작업이 많으면 프레임을 넓혀도 쿨다운은 지켜지고, IDLE 이 0 → 답 = n.</text>
  </g>
  <g stroke="currentColor">
    <rect x="420" y="50" width="34" height="30" class="f-acc"/><rect x="454" y="50" width="34" height="30" class="f-acc"/><rect x="488" y="50" width="34" height="30" class="f-ok"/>
    <rect x="522" y="50" width="34" height="30" class="f-acc"/><rect x="556" y="50" width="34" height="30" class="f-acc"/><rect x="590" y="50" width="34" height="30" class="f-mut"/>
    <rect x="624" y="50" width="34" height="30" class="f-acc"/><rect x="658" y="50" width="34" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="437" y="70">A</text><text x="471" y="70">B</text><text x="505" y="70">C</text><text x="539" y="70">A</text>
    <text x="573" y="70">B</text><text x="607" y="70">_</text><text x="641" y="70">A</text><text x="675" y="70">B</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="437" y="96">0</text><text x="471" y="96">1</text><text x="505" y="96">2</text><text x="539" y="96">3</text>
    <text x="573" y="96">4</text><text x="607" y="96">5</text><text x="641" y="96">6</text><text x="675" y="96">7</text>
  </g>
  <path d="M437 104 Q488 134 539 104" fill="none" stroke="currentColor" marker-end="url(#m10a)"/>
  <text x="440" y="140" fill="currentColor" font-size="12">A→A 간격 3 = c+1 (c 칸 비움)</text>
  <text x="420" y="40" fill="currentColor" font-size="12">프레임을 한 줄로 펴면 실행 순서</text>
  <g fill="currentColor" font-size="12">
    <text x="40" y="232">하한: 최대 종류 하나만 봐도 처음~마지막이 (f-1)(c+1) 칸 이상 떨어져야 한다. 최대 종류가 m 개면</text>
    <text x="40" y="250">그중 "처음 등장이 가장 늦은" 것은 시각 m-1 이후에 시작 → 길이 ≥ (f-1)(c+1) + m. 그리고 당연히 길이 ≥ n.</text>
  </g>
</svg>
```

### Part A — 쿨다운 스케줄러: 공식, 실제 순서, 검증기

종류는 `uint8_t` id `0..25`, IDLE 은 `0xFF`.

- `uint64_t sched_min_time(const uint8_t *task, size_t n, uint32_t cool)` — 공식. `(f-1)(cool+1)` 은 **64비트로** 곱한다 (cool 이 `UINT32_MAX` 면 32비트에서 `cool+1 = 0`).
- `uint64_t sched_build(task, n, cool, uint8_t *out, size_t cap)` — 최대 힙 라운드 시뮬레이션. 한 라운드 = cool+1 칸: 힙에서 **남은 개수가 많은 순**으로 서로 다른 종류를 최대 cool+1 개 꺼내 실행하고, 개수를 1 줄여 다시 넣고, 아직 남은 게 있으면 라운드를 IDLE 로 채운다. 반환은 전체 길이, `out` 에는 앞 `cap` 칸만 쓴다(`snprintf` 규약).
- `int sched_validate(task, n, cool, seq, len)` — 순서 검증기: 잘못된 id / 개수 불일치(빠짐·초과) / 쿨다운 위반 / 끝이 IDLE 을 구분해 돌려준다.

검증: 종류 ≤ 3·개수 ≤ 3·cool ≤ 3 **전수**(256 경우)와 무작위 작은 입력 600개를 **서로 다른 순열 전부**를 시도하는 브루트포스와 대조, 큰 무작위 입력 300개(n ≤ 3000, 26종, cool ≤ 40)는 공식·검증기와 대조, 검증기 자체는 올바른 순서를 한 칸씩 망가뜨린 3000개를 독립 기준 검증기와 대조.

### Part B — 마감·이익 작업 선택: 두 방법

`job_t { uint32_t deadline, profit; }`, 작업 id = 배열 인덱스, 슬롯 1..n (마감이 n 보다 크면 n 으로 잘라도 답이 같다 — 5. 풀이). 출력 `slot_job[t]` = 슬롯 t 의 작업 id 또는 -1.

- `jobseq_dsu` — 이익 내림차순(최대 힙)으로 보며 `slot_find(min(d, n))` = "d 이하에서 가장 늦은 빈 슬롯" 에 넣는다. 슬롯 DSU(서로소 집합, disjoint-set union) + 경로 반감(path halving). DSU 자체는 ALGO-M14 에서 자세히.
- `jobseq_heap` — 마감 오름차순(계수 정렬)으로 보며 이익 **최소** 힙에 넣고, 힙 크기가 지금 마감 d 를 넘으면 가장 작은 이익을 버린다. 남은 작업들을 마감 순서(EDF)로 슬롯 1, 2, ... 에 놓는다.

검증: n ≤ 12 무작위 2000개를 **부분집합 전부** 브루트포스와 대조(이익 동점 많음, 마감 0 과 n 초과 포함), n ≤ 4096 무작위 50개는 두 방법 이익 일치 + 배치 유효성(슬롯 ≤ 마감, 작업 중복 없음, 이익 합 일치).

### Part C — 펌웨어: EEPROM 쓰기 계획과 다운링크 프레임

1. I2C EEPROM 4개(E0..E3, 24LC256 류)에 페이지 쓰기 13개를 1 ms 버스 슬롯에 배치한다. 한 번 쓰면 그 칩은 **전송 ~0.79 ms + 쓰기 사이클 tWR 5 ms** 동안 바쁘다. `cooldown_from_busy(busy_us, slot_us)` = `ceil(busy/slot) - 1` 을 **넘침 없이** 구하고 (`(busy + slot - 1) / slot` 은 넘칠 수 있다), 쓰기 분포 {6,3,3,1} 과 {4,3,3,3} 의 계획 길이를 비교한다.
2. 무선 다운링크는 프레임마다 패킷 1개. 패킷 8종(배터리 경보, GPS, IMU 로그, ...)마다 "이 프레임까지 가야 의미 있음" 마감과 가치가 있다. 4 프레임 안에 최대 가치를 보내는 계획과 버려지는 패킷 목록을 출력한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m10b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">jobseq_dsu: 이익 순서로 "마감 이하 가장 늦은 빈 슬롯" — 고전 5작업 예 (답 142)</text>
  <g stroke="currentColor">
    <rect x="420" y="50" width="56" height="40" class="f-ok"/><rect x="476" y="50" width="56" height="40" class="f-ok"/><rect x="532" y="50" width="56" height="40" class="f-ok"/>
    <rect x="588" y="50" width="56" height="40" class="f-mut"/><rect x="644" y="50" width="56" height="40" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="448" y="75">c</text><text x="504" y="75">a</text><text x="560" y="75">e</text><text x="616" y="75">-</text><text x="672" y="75">-</text>
    <text x="448" y="108" font-size="12">1</text><text x="504" y="108" font-size="12">2</text><text x="560" y="108" font-size="12">3</text>
    <text x="616" y="108" font-size="12">4</text><text x="672" y="108" font-size="12">5</text>
    <text x="392" y="108" font-size="12">0 = 보초</text>
  </g>
  <path d="M500 118 Q476 140 452 118" fill="none" stroke="currentColor" marker-end="url(#m10b)"/>
  <path d="M444 118 Q420 140 400 118" fill="none" stroke="currentColor" marker-end="url(#m10b)"/>
  <text x="420" y="160" fill="currentColor" font-size="12">채운 슬롯 s 는 par[s] = s-1 → 왼쪽 빈 슬롯으로</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="56">작업 (d, p)  이익 순서로</text>
    <text x="20" y="80">a (2,100): find(2)=2 → 슬롯 2</text>
    <text x="20" y="100">c (2, 27): find(2)→1 → 슬롯 1</text>
    <text x="20" y="120">d (1, 25): find(1)→0 → 버림</text>
    <text x="20" y="140">b (1, 19): find(1)→0 → 버림</text>
    <text x="20" y="160">e (3, 15): find(3)=3 → 슬롯 3</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="200">왜 "가장 늦은" 슬롯? a 를 슬롯 1 에 넣으면 c 가 들어갈 자리가 없어진다. 늦게 넣을수록 마감이 짧은 작업에게</text>
    <text x="20" y="218">앞쪽 슬롯을 남긴다. 반례: (d,p) = (2,1), (1,1) — 가장 이른 슬롯이면 1, 최적은 2 (7. 실수 3, demo_wrong 실측).</text>
    <text x="20" y="240">답 = c + a + e = 27 + 100 + 15 = 142, 마감 1 짜리 d·b 는 슬롯 1 을 c 에게 졌다.</text>
  </g>
</svg>
```

## 2. Problem (English)

> **Part A — Task scheduler with cooldown.** Given `n` tasks, each of type `0..25`, and an integer `cool ≥ 0`, each time slot runs one task or is idle. Two tasks of the same type must be at least `cool + 1` slots apart. Implement
> `uint64_t sched_min_time(const uint8_t *task, size_t n, uint32_t cool)` (the minimum number of slots),
> `uint64_t sched_build(const uint8_t *task, size_t n, uint32_t cool, uint8_t *out, size_t cap)` that produces an optimal sequence (`0xFF` = idle, writes at most `cap` entries, returns the full length), and
> `int sched_validate(...)` that checks a sequence. No allocation; `cool` may be as large as `UINT32_MAX`.
>
> Example: `AAABBB`, cool = 2 → 8, e.g. `A B _ A B _ A B`. `AAAAAABCDEFG`, cool = 2 → 16. cool = 0 → n.
>
> **Part B — Job sequencing with deadlines.** `n ≤ 4096` unit-time jobs, job `j` has `deadline[j]` (it must run in a slot `1..deadline[j]`; 0 means impossible) and `profit[j]`. One job per slot. Maximize total profit; return it and fill `slot_job[1..n]`. Do it two ways: (1) profit-descending greedy with a "latest free slot ≤ d" union-find, (2) deadline-ascending scan with a min-heap of profits. Prove why the greedy is optimal.
>
> Example: `(d,p) = (2,100), (1,19), (2,27), (1,25), (3,15)` → 142 (jobs 2, 0, 4 in slots 1, 2, 3). `(4,20), (1,10), (1,40), (1,30)` → 60.
>
> **Part C.** Convert a device busy time (µs) to a cooldown in slots without overflow, plan EEPROM page writes, and pick downlink packets for 4 radio frames.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 7개 함수 본문 (`sched_min_time`, `sched_build`, `sched_validate`, `slot_find`, `jobseq_dsu`, `jobseq_heap`, `cooldown_from_busy`). 힙 도우미(`th_push/th_pop`, `ih_*`)는 이미 들어 있다 (DS-B12 / DS-M11 에서 자세히). 처음 실행 결과 (실제 출력, 중간 생략):

```text
A: AAABBB cool=2 ->   (len 0)
FAIL starter.c:258  sched_min_time(ex1, 6, 2) == 8 && L == 8 && memcmp(g_seq, "\x00\x01\xff\x00\x01\xff\x00\x01", 8) == 0
…
Part A exhaustive: 256 cases, len!=formula 0, invalid 252, !=brute 252
…
Part A validator mutations: 3000 cases, agree 0, ref says invalid 3000
…
Part B random small vs brute: 2000 cases, bad 1835
…
C-2 downlink: value dsu=0 heap=0 brute=250
…
5/34 passed
```

(exit 1. 통과한 5개는 스텁의 0 / `SV_OK` 가 우연히 맞는 것들 — 빈 입력, 잘못된 id → 0, "올바른 순서" 판정 두 개, 작업 0개.)

## 4. 힌트

<details><summary>힌트 1 — 공식은 "가장 많은 종류" 하나에서 나온다</summary>

개수가 f 인 종류 X 는 처음과 마지막 사이가 (f-1)(c+1) 칸 이상이다. 개수가 f 인 종류가 m 개면 마지막 프레임에 m 칸이 더 붙는다. 그리고 길이는 n 보다 짧을 수 없다. 둘 중 큰 것.

</details>

<details><summary>힌트 2 — 라운드 = c+1 칸, 한 라운드에 같은 종류는 한 번</summary>

힙에서 최대 c+1 개를 **꺼내 둔 채로** 실행하고, 다 실행한 뒤에 개수를 줄여 다시 넣는다. 꺼내면서 바로 다시 넣으면 같은 종류가 한 라운드에 두 번 나온다. 마지막 라운드는 IDLE 로 채우지 않는다.

</details>

<details><summary>힌트 3 — 검증기는 종류별 "마지막 실행 시각" 배열 하나</summary>

`t - last[s] <= cool` 이면 위반 (간격은 cool+1 이상이어야 한다). 개수는 `need[]` 를 세어 두고 하나씩 빼다가 음수가 되거나 끝에 0 이 아니면 위반.

</details>

<details><summary>힌트 4 — 왜 "가장 늦은" 빈 슬롯인가, 그리고 그걸 빨리 찾는 법</summary>

늦게 넣을수록 마감이 짧은 작업에게 앞자리를 남긴다. "s 이하에서 가장 늦은 빈 슬롯" 은 DSU 로: `par[s] == s` 면 빈 슬롯, 채우면 `par[s] = s - 1`. `find(s)` 의 루트가 답, 0 이면 없음.

</details>

<details><summary>힌트 5 — 마감 순서 + 최소 힙의 불변식</summary>

마감 d 까지 본 작업들 중 실행 가능한 최대 이익 집합을 힙에 들고 있다. 새 작업을 넣어 개수가 d 를 넘으면 가장 싼 것 하나만 빼면 다시 실행 가능하다 — 그게 최적인지가 증명 포인트 (5. 풀이).

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 쿨다운 스케줄러 | 작업 선택 |
|---|---|---|
| 1. 확인 질문 | 작업 순서를 바꿔도 되나? (안 되면 연습 3) 종류 수 상한? cool 의 정의 = 사이 빈칸 수? 실제 순서가 필요한가, 길이만? | 작업은 단위 시간? 슬롯 번호 1부터? 마감 0 / 마감 > n? 이익 동점? 이익만인가 개수인가? |
| 2. 무식한 방법 | 서로 다른 순열 전부 × 가장 이른 배치: O(n!·n) | 부분집합 전부 × 실행 가능 검사: O(2^n·n) |
| 3. 더 나은 생각 | 하한 `(f-1)(c+1)+m` 과 `n`; 최대 힙 라운드가 그 하한을 달성 | 이익 그리디 + 가장 늦은 빈 슬롯(DSU), 또는 마감 순서 + 최소 힙 교체 |
| 4. 불변식/증명 | 하한(프레임) + 라운드 위치 단조성(쿨다운 성립) + 이분법(IDLE 있으면 최대 종류가 매 라운드 1씩) | 실행 가능 ⟺ `∀t: N_t ≤ t`, 교환 보조정리 → 그리디 최적 |
| 5. 말할 테스트 | n=0, cool=0, cool 거대, 한 종류만, 모든 종류 동수, 26종 | 마감 0, 마감 > n, 모두 같은 마감, 이익 동점, 가장 이른 슬롯 반례 |
| 6. 복잡도 | 공식 O(n+K), 시뮬 O(n log K + L), 검증 O(L+K) | 둘 다 O(n log n), 메모리 정적 12~17 B/작업 |

### Part A — 하한: 왜 더 짧을 수 없나

어떤 올바른 순서든 길이 L 에 대해:

1. **L ≥ n** — 한 칸에 작업 하나.
2. **L ≥ (f-1)(c+1) + m** — 개수가 f 인 종류 X 의 실행 시각을 `t_1 < … < t_f` 라 하면 이웃 간격이 c+1 이상이므로 `t_f ≥ t_1 + (f-1)(c+1)`. 개수가 f 인 m 종류의 **처음 실행 시각**은 서로 다르다(한 칸에 하나). 그중 가장 늦은 것을 Y 라 하면 `t_1(Y) ≥ m-1` (0부터 센 시각). 그러면 `t_f(Y) ≥ (m-1) + (f-1)(c+1)` 이고 길이는 그보다 1 크다. ∎

### Part A — 라운드 힙 시뮬레이션과 추적

힙 키는 (남은 개수 큰 것, 같으면 id 작은 것). 한 라운드:

1. 힙에서 최대 c+1 개를 꺼내 `round[]` 에 둔다 (서로 다른 종류).
2. 그 순서대로 실행.
3. 개수를 1 줄이고 0 이 아니면 다시 넣는다.
4. 힙이 비지 않았으면 라운드를 c+1 칸까지 IDLE 로 채운다.

`AAABBBC`, c = 2 (라운드 폭 3) — solution.c 출력 `ABCAB_AB  (len 8)`:

| 라운드 | 시작 힙 (개수) | 꺼냄 | 출력 | 개수 갱신 후 | 채움 |
|---|---|---|---|---|---|
| 1 | A3 B3 C1 | A, B, C | `A B C` | A2 B2 (C 끝) | 3개 꺼냄 → 없음 |
| 2 | A2 B2 | A, B | `A B` | A1 B1 | 남음 → IDLE 1 |
| 3 | A1 B1 | A, B | `A B` | 끝 | 힙 빔 → 채우지 않음 |

길이 3 + 3 + 2 = 8 = 공식 `max(7, 2·3+2)`. 마지막 라운드를 채웠다면 10 이 되었다 (7. 실수 7).

### Part A — 상한: 라운드판이 정확히 그 길이를 낸다

**(1) 쿨다운이 지켜진다.** 라운드 안에서 종류가 다르니 같은 라운드 충돌은 없다. 종류 X 가 라운드 r 의 위치 p 에서 실행되고 다음 라운드에도 나온다고 하자. 라운드 r 에서 X 앞에 있던 종류 Y 는 개수가 X 이상(같으면 id 가 작음)이다. 둘 다 1씩 줄었으니 순서는 그대로이고, Y 가 사라지려면 Y 의 개수가 1 이었어야 하는데 그러면 X 도 1 이라 X 도 사라진다. 따라서 X 가 남아 있으면 X 앞의 종류도 전부 남아 있고 여전히 앞이다 → 다음 라운드 위치 `p' ≥ p`. 간격 = `(c+1) - p + p' ≥ c+1`. 라운드를 건너뛰면 간격은 더 크다. (그래서 동점 처리를 id 로 **일관되게** 해야 한다 — 매번 바뀌면 이 논증이 깨진다.)

**(2) 길이.** 라운드 시작의 개수(내림차순)를 `x_1 ≥ x_2 ≥ …`, 남은 종류 수를 r 이라 하자. 라운드는 위에서 `min(c+1, r)` 개를 1씩 줄인다. 성질 **P** = "개수가 `x_1 - 1` 이상인 (남은) 종류가 c+2 개 이상".

- **P 이면 그 뒤로 IDLE 이 없다.** P 면 r ≥ c+2 라 라운드가 꽉 찬다. 다음 라운드에도 P 가 유지된다: 최대 동점이 c+2 개 이상이면 최대값은 그대로 `x_1` 이고, 원래 최대였던 c+2 개 이상이 모두 `x_1 - 1` 이상으로 남는다. 아니면(`x_{c+2} = x_1 - 1`) 최대 종류가 전부 선택되어 새 최대가 `x_1 - 1`, 원래 `x_1 - 1` 이상이던 c+2 개 이상이 모두 `x_1 - 2` 이상이다. 예외는 개수가 0 이 되는 경우뿐인데, 그때는 남은 개수가 전부 1 이하이고, 모든 개수가 1 이면 `r ≤ c+1` 인 라운드가 곧 마지막 라운드라 IDLE 을 채우지 않는다.
- **P 가 아니면** 개수 `≥ x_1 - 1` 인 종류가 c+1 개 이하라 **전부 선택**된다 → 최대 종류 집합이 그대로 유지되고 최대값이 정확히 1 줄어든다 (원래 `x_1 - 1` 이던 것은 `x_1 - 2` 가 된다).
- IDLE 은 `r < c+1` 인 라운드에서만 생기고 r 은 줄기만 한다. 그러니 IDLE 이 한 번이라도 생기면, 그 앞 라운드들에서 P 는 없었고(있었다면 IDLE 이 없어야 함) 그 뒤로도 `r < c+2` 라 P 가 불가능하다. 즉 **매 라운드 P 가 아님** → 라운드 수 = f, 마지막 라운드에는 원래의 최대 종류 m 개만 남고, 마지막이 아닌 라운드는 정확히 c+1 칸 → `L = (f-1)(c+1) + m`.
- IDLE 이 한 번도 없으면 `L = n`.

어느 쪽이든 L 은 두 하한 중 하나와 같고, 하한 증명에서 L ≥ 둘 다이므로 `L = max(n, (f-1)(c+1)+m)` ∎. solution.c 가 이를 전수(256) + 무작위(600) 브루트포스로 확인한다.

```text
Part A exhaustive: 256 cases, len!=formula 0, invalid 0, !=brute 0
Part A random small vs brute: 600 cases, bad 0
Part A random large: 300 cases, bad 0, total idle slots 2157562
Part A validator mutations: 3000 cases, agree 3000, ref says invalid 2136
```

(브루트포스가 맞는 이유: 순서를 고정하면 각 작업을 가능한 가장 이른 칸에 놓는 것이 귀납적으로 모든 작업의 시각을 최소화한다. 최적 순서의 비-IDLE 순서도 어떤 순열이므로 "모든 서로 다른 순열 × 가장 이른 배치" 의 최솟값 = 최적.)

검증기는 시뮬레이션과 **독립**이어야 의미가 있다: 개수(`need[]`), 쿨다운(`last[]`), id 범위, 끝 IDLE 금지(길이 = 마지막 작업이 끝난 시각). 앞쪽 IDLE 은 낭비지만 규칙 위반은 아니어서 허용하고, 최적성은 길이를 공식과 비교해 따로 본다.

### Part B — 실행 가능 집합: `∀t: N_t(S) ≤ t`

작업 집합 S 에 대해 `N_t(S) = |{j ∈ S : d_j ≤ t}|`.

**보조정리 1 (실행 가능 조건).** S 를 마감 안에 배치할 수 있다 ⟺ 모든 t 에서 `N_t(S) ≤ t`.
(⇒) 마감 ≤ t 인 작업은 슬롯 1..t 의 서로 다른 칸에 있어야 한다. (⇐) 마감 순서로 정렬해 i 번째 작업을 슬롯 i 에 놓는다(EDF). i 번째 작업의 마감을 d 라 하면 앞의 i 개가 모두 마감 ≤ d 이므로 `i ≤ N_d ≤ d`. ∎
따름: 마감 > n 을 n 으로 잘라도 실행 가능성은 같다(EDF 배치는 슬롯 ≤ |S| ≤ n 만 쓴다). 그래서 슬롯 배열은 n+1 칸이면 충분하다.

**보조정리 2 (교환).** A, B 실행 가능, `|A| < |B|` 이면 `A + x` 가 실행 가능한 `x ∈ B \ A` 가 있다.
`N_T(A) = T` 인 가장 큰 T 를 잡는다(T = 0 은 항상 해당). 마감 > T 인 x 를 A 에 더하면 t < d_x 에서는 N_t 그대로, t ≥ d_x > T 에서는 `N_t(A) < t` 였으니 `N_t(A+x) ≤ t` → 실행 가능. 그런 x 가 B \ A 에 있는지 세면: B 에서 마감 > T 인 개수 = `|B| - N_T(B) ≥ |B| - T > |A| - T = |A| - N_T(A)` = A 에서 마감 > T 인 개수. 그러니 B 쪽이 더 많고, 그중 하나는 A 에 없다. ∎
(부분집합이 실행 가능하다는 성질과 합치면 실행 가능 집합족은 매트로이드(matroid)다 — CLRS 3판 §16.5 의 "task scheduling" 예와 같은 구조.)

**정리 (이익 그리디 최적).** 이익 내림차순으로 보며 "더해도 실행 가능하면 넣는" 그리디 G 가 최적이다 (이익 ≥ 0).
G 를 이익 순서로 `g_1, g_2, …`, 최적해 O 를 이익 내림차순 `o_1, o_2, …` 라 하자. 모든 i 에서 `p(g_i) ≥ p(o_i)` 임을 보이면 끝이다. 처음으로 `p(g_i) < p(o_i)` 인 i (또는 G 가 i-1 개에서 끝났는데 O 에 o_i 가 있는 경우)를 잡고 `A = {g_1..g_{i-1}}`, `B = {o_1..o_i}` 에 보조정리 2 를 쓰면 `A + x` 실행 가능한 `x ∈ B \ A` 가 있고 `p(x) ≥ p(o_i) > p(g_i)`. 그러면 그리디는 g_i 보다 **먼저** x 를 봤고, 그때 이미 넣은 집합은 A 의 부분집합이라 x 를 넣을 수 있었다 → x ∈ A, 모순. ∎

### Part B — "가장 늦은 빈 슬롯" 이 정확한 실행 가능성 검사인 이유

그리디는 "A + x 가 실행 가능한가" 를 물어야 한다. 매번 보조정리 1 을 검사하면 O(n) 이다. 대신 **각 작업을 마감 이하 가장 늦은 빈 슬롯에** 넣어 두면, "A + x 실행 가능 ⟺ 슬롯 1..d_x 중 빈 곳이 있음" 이 성립한다.
(⇐) 자명. (⇒) 대우: 1..d_x 가 다 찼다고 하자. 1..t* 가 다 찬 가장 큰 `t* ≥ d_x` 를 잡는다(슬롯 t*+1 은 비었거나 t* = n). 슬롯 s ≤ t* 의 작업 y 에 대해 `d_y > t*` 였다면 y 를 넣을 때 슬롯 t*+1 ≤ d_y 가 비어 있었으므로(슬롯은 비워지지 않는다) y 는 t*+1 이상에 들어갔어야 한다 — 모순. 따라서 슬롯 1..t* 의 t* 개 작업은 전부 마감 ≤ t*, x 도 마감 ≤ t* → `N_{t*}(A+x) = t*+1 > t*`, 실행 불가능. ∎

DSU 구현: `g_par[s] == s` ⟺ 슬롯 s 가 빔. 슬롯 s 를 쓰면 `g_par[s] = s - 1` 로 왼쪽 집합에 붙인다. `slot_find(s)` 는 루트 = "s 이하 가장 늦은 빈 슬롯", 슬롯 0 은 영원히 비어 있는 보초라 루트가 0 이면 자리 없음. 위 그림의 추적이 solution.c 출력 `dsu=142 slots[1..3]=2,0,4` (슬롯 1 = c, 2 = a, 3 = e) 와 같다.

복잡도 주의: 이 DSU 는 합치는 방향이 "항상 왼쪽" 으로 고정이라 union by rank 를 그대로 쓸 수 없고, **경로 압축(여기서는 반감)만** 쓴다. 그 경우 상각 상한은 연산당 O(log n) 이다 (Tarjan–van Leeuwen 1984; 확인 권장). 각 집합에 "가장 작은 슬롯" 라벨을 따로 들고 rank 로 합치면 α(n) 이 된다 — α(n) 이야기는 ALGO-M14. 실제로는 힙의 O(n log n) 이 지배한다(아래 벤치).

### Part B — 마감 순서 + 최소 힙: 불변식과 증명

마감 오름차순으로 작업을 보며 (같은 마감은 id 순) `J_k` = 처음 k 개. 불변식: **힙 `H_k` 는 `J_k` 의 실행 가능한 최대 이익 부분집합이고 `|H_k| ≤ d_k`**.

작업 j (마감 d, 앞의 모든 마감 ≤ d) 를 넣을 때, `H = H_{k-1}`:

- `|H| + 1 ≤ d` 면 `H + j` 는 실행 가능(t < d 에선 N_t 그대로, t ≥ d 에선 `|H|+1 ≤ d ≤ t`). 최적: `J_k` 의 실행 가능한 S 에 대해 `S - j ⊆ J_{k-1}` 이라 `p(S) ≤ p(H) + p(j)`.
- `|H| = d` 면 넣고 최솟값을 뺀 `H'` 는 크기 d, 마감 전부 ≤ d → 실행 가능(t < d 쪽은 H 의 부분이라 그대로). 최적: S 가 j 를 안 쓰면 `p(S) ≤ p(H) ≤ p(H')`. j 를 쓰면 `|S - j| ≤ d - 1 < |H|` 이므로 교환 보조정리로 `S - j + h` 가 실행 가능한 `h ∈ H` 가 있다. 만약 `p(S - j) > p(H) - min(H)` 라면 `p(S - j + h) > p(H) - min(H) + p(h) ≥ p(H)` 로 H 의 최적성에 모순. 따라서 `p(S) ≤ p(H) - min(H) + p(j) ≤ p(H')` (j 가 가장 싸면 `p(H') = p(H) ≥ p(H) - min(H) + p(j)`). ∎

고전 5작업 추적 (마감 순서 b, d, a, c, e):

| 단계 | 작업 (d, p) | push 후 힙 (이익) | 크기 > d? | 버림 | 힙 |
|---|---|---|---|---|---|
| 1 | b (1, 19) | {19} | 1 > 1 아니오 | — | {19} |
| 2 | d (1, 25) | {19, 25} | 2 > 1 예 | b(19) | {25} |
| 3 | a (2, 100) | {25, 100} | 아니오 | — | {25, 100} |
| 4 | c (2, 27) | {25, 27, 100} | 3 > 2 예 | d(25) | {27, 100} |
| 5 | e (3, 15) | {15, 27, 100} | 아니오 | — | {15, 27, 100} |

남은 {a, c, e} = 142 를 마감 순서로 슬롯 1, 2, 3 (`heap=142 slots[1..3]=0,2,4`). 두 방법의 **집합은 같고 배치만 다르다** — DSU 판은 뒤로, 힙 판은 앞으로 몰아 놓는다. 이익 동점이 있으면 집합이 달라질 수 있지만 총 이익은 같다(무작위 2000개 + 50개에서 확인). 이 방법은 작업이 **마감 순서로 도착하는 스트림**이면 정렬 없이 바로 온라인으로 돌릴 수 있다는 장점이 있다.

### Part C — 펌웨어 출력

```text
C-1 EEPROM pages {6,3,3,1}, cool=5: 31 slots, idle 18
  E0 E1 E2 E3 -- -- E0 E1 E2 -- -- -- E0 E1 E2 -- -- -- E0 -- -- -- -- -- E0 -- -- -- -- -- E0
C-1 EEPROM pages {4,3,3,3}, cool=5: 19 slots, idle 6
C-2 downlink: value dsu=250 heap=250 brute=250
  frame 1: BATT_ALARM | BATT_ALARM
  frame 2: PARAM_ACK  | PARAM_ACK
  frame 3: MOTOR_DIAG | MOTOR_DIAG
  frame 4: CAM_THUMB  | CAM_THUMB
  dropped: GPS_FIX IMU_LOG HEARTBEAT TEMP
```

```svg
<svg viewBox="0 0 740 220" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">C-1 실제 계획 (solution.c 출력): 31 슬롯 중 18 IDLE — E0 혼자 길이를 정한다</text>
  <g fill="currentColor" font-size="12"><text x="20" y="58">E0 (6)</text><text x="20" y="84">E1 (3)</text><text x="20" y="110">E2 (3)</text><text x="20" y="136">E3 (1)</text></g>
  <rect x="80" y="44" width="620" height="104" class="f-mut" stroke="currentColor"/>
  <g stroke="currentColor" class="f-acc">
    <rect x="80" y="46" width="20" height="20"/><rect x="200" y="46" width="20" height="20"/><rect x="320" y="46" width="20" height="20"/>
    <rect x="440" y="46" width="20" height="20"/><rect x="560" y="46" width="20" height="20"/><rect x="680" y="46" width="20" height="20"/>
  </g>
  <g stroke="currentColor" class="f-ok">
    <rect x="100" y="72" width="20" height="20"/><rect x="220" y="72" width="20" height="20"/><rect x="340" y="72" width="20" height="20"/>
    <rect x="120" y="98" width="20" height="20"/><rect x="240" y="98" width="20" height="20"/><rect x="360" y="98" width="20" height="20"/>
    <rect x="140" y="124" width="20" height="20"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="90" y="166">0</text><text x="210" y="166">6</text><text x="330" y="166">12</text><text x="450" y="166">18</text><text x="570" y="166">24</text><text x="690" y="166">30</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="190">슬롯 1 ms, 바쁨 0.79 + 5 ms → 같은 칩 간격 ≥ 6 슬롯 (cool 5). 공식 (6-1)·6 + 1 = 31.</text>
    <text x="20" y="208">같은 13 페이지를 {4,3,3,3} 로 나누면 (4-1)·6 + 1 = 19 슬롯 — 스케줄러보다 데이터 배치가 더 큰 손잡이다.</text>
  </g>
</svg>
```

해석:

- **cool 계산.** 1 ms 슬롯 시작에 쓰기를 시작하면 칩은 0.79 ms(32 B 쓰기: 3 B 헤더 + 32 B 데이터 × 9 클럭 / 400 kHz ≈ 0.79 ms) + tWR 동안 바쁘다. 24LC256 데이터시트의 tWR 은 최대 5 ms (확인 권장, 페이지는 64 B 라 여기선 절반 페이지 쓰기). 다음 쓰기는 `ceil(5790/1000) = 6` 슬롯 뒤부터 → cool = 5. `busy_us / slot_us + (busy_us % slot_us != 0)` 로 쓰면 `busy_us = UINT32_MAX` 에서도 넘치지 않는다 (테스트 `cooldown_from_busy(UINT32_MAX, 1) == UINT32_MAX - 1`).
- **공식이 설계를 말해 준다.** IDLE 은 전부 E0 의 개수 f 에서 나온다. Don 의 SSD 경험 그대로 — 채널/다이에 쓰기를 **스트라이핑**해 가장 바쁜 다이의 큐 깊이를 줄이는 것이 스케줄링 순서를 아무리 다듬는 것보다 크게 듣는다. 실제 칩은 ACK 폴링(쓰기 끝나면 ACK 를 돌려줌)으로 tWR 최대값보다 일찍 끝날 수 있지만, 오프라인 계획은 최악값으로 잡는다.
- **다운링크.** 마감 2 이하 패킷은 BATT_ALARM(90), PARAM_ACK(70), GPS_FIX(60), HEARTBEAT(30) 넷인데 앞 두 슬롯뿐이라 GPS 는 가치 60 인데도 버려진다 — 브루트포스 250 과 일치. 두 방법이 같은 배치를 냈다.

### RTOS 와의 관계 (see: RTOS)

이 레슨은 **오프라인 스케줄링**이다: 작업 전체(개수, 마감, 이익)를 미리 알고, 단위 시간이고, 한 번 계획을 세운다. RTOS 의 RMS(rate-monotonic, 고정 우선순위)·EDF(earliest deadline first, 동적 우선순위) 는 주기적으로 **계속 도착하는** 작업에 대해 "모든 마감을 항상 지킬 수 있는가(스케줄 가능성, schedulability)" 를 이용률 한계(Liu & Layland 1973, RMS 의 `n(2^{1/n} - 1)`, EDF 의 `U ≤ 1`)나 응답 시간 분석으로 묻는다. 연결점은 하나다: 보조정리 1 의 ⇐ 증명이 바로 "단일 프로세서·단위 작업에서는 마감 순서(EDF) 배치가 가능하면 언제나 성공한다" 는 EDF 최적성의 가장 단순한 형태다. 버려도 되는 작업(이익)을 고르는 job sequencing 은 과부하 시 "어떤 작업을 포기할까" 의 오프라인 판이고, 실제 RTOS 의 과부하 처리·우선순위 역전은 RTOS 트랙에서 다룬다.

### 벤치마크 — `demo_bench.c`

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench    # 약 22 초
```

이 맥(`sysctl` 상 Apple M6, Apple clang 21.0.0, -O2), 1회 워밍업 + 5회 중앙값, ns/작업. 절대값보다 **비율**을 본다. naive = 이익 그리디 + 마감에서 한 칸씩 내려가며 빈 슬롯 찾기. 실제 출력:

```text
dist            n |      naive        dsu       heap |    naive steps | profit equal
uniform      1024 |       49.8       42.0        7.8 |          16272 | yes
uniform     65536 |      154.8      132.4       12.7 |        4676381 | yes
uniform    262144 |      618.7      547.8       27.8 |       60877444 | yes
all-n        1024 |      262.7       23.4        3.9 |         524800 | yes
all-n       16384 |     7509.6       73.5        8.7 |      134225920 | yes
all-n       65536 |    16590.4      100.9        9.0 |     2147516416 | yes
all-n      262144 |     (skip)      145.6       11.1 |              0 | yes
tight       65536 |     1081.3      102.6       54.0 |      125906870 | yes
tight      262144 |     4437.1      151.2       67.3 |     2006083605 | yes

cooldown scheduler, n = 2^20 tasks, ns per task (median of 5)
distribution                 cool |      slots         idle |      build    formula
uniform 26 types                0 |    1048576            0 |      17.25       0.33
uniform 26 types              100 |    4108984      3060408 |      10.91       0.36
type A ~36%, rest uniform      10 |    4138751      3090175 |      10.97       0.76
type A ~36%, rest uniform     100 |   38001251     36952675 |        9.79       0.75
```

(표는 줄인 것. 전체 15 + 8 줄은 직접 실행.) 읽는 법:

- **naive 의 최악은 실제로 n²/2.** 모든 마감 = n 이면 k 번째 작업이 k 칸을 훑는다: n = 65536 에서 단계 수 2147516416 = n(n+1)/2 정확히. ns/작업이 n 에 비례해 커진다(262 → 16590). DSU 판은 같은 입력에서 23 → 101 ns 로 로그 수준.
- **uniform 에서는 DSU 가 naive 와 별 차이 없다.** 마감이 고르게 퍼져 있으면 naive 도 몇 칸만 훑는다. 두 방법 다 시간의 대부분이 **크기 n 최대 힙에서 n 번 pop** (매번 log n 단계 sift-down, 함수 포인터 비교, n = 2^18 에서 캐시 미스)에 쓰인다.
- **heap 판이 uniform·all-n 에서 5~20배, tight 에서 약 2배 빠르다.** 계수 정렬 O(n) + 무작위 이익을 최소 힙에 push (무작위 키 삽입의 sift-up 은 평균 상수 단계로 알려져 있다, 확인 권장) + 넘칠 때만 pop. pop 이 많은 tight(마감 ≤ n/16, 대부분 버려짐)에서는 그 차이가 줄어든다(54~67 ns). 이 맥 기준의 결론이고, 캐시 없는 Cortex-M 의 SRAM 에서는 비율이 다를 수 있다.
- **공식은 시뮬레이션보다 13~50배 빠르다** (개수 세기 한 번 vs 작업당 힙 pop+push; IDLE 이 많으면 시뮬레이션의 작업당 비용이 오히려 줄어 차이가 작아진다). 길이만 필요하면 공식, 실제 순서가 필요할 때만 시뮬레이션.

### 복잡도 요약

| 함수 | 시간 | 추가 메모리 | 스택 |
|---|---|---|---|
| `sched_min_time` | O(n + K), K = 26 | O(K) | O(1) |
| `sched_build` | O(n log K + min(L, cap)) — 라운드 IDLE 은 cap 밖이면 덧셈 한 번 | O(K) 지역 배열 (~130 B) | 재귀 없음 |
| `sched_validate` | O(L + K) | O(K) | O(1) |
| `jobseq_dsu` | O(n) heapify + O(n log n) pop + find 상각 O(log n) 상한 | 정적 `par`+`heap` 8 B/작업 + 출력 4 B | 재귀 없음 |
| `jobseq_heap` | O(n) 계수 정렬 + O(n log n) 최악 힙 | 정적 `bucket`+`order`+`heap`+`chosen` 13 B/작업 + 출력 4 B | 재귀 없음 |
| 브루트포스 (테스트) | 순열 O(n!·n), 부분집합 O(2^n·n) | — | — |

정적 배열은 재진입 불가(두 문맥이 동시에 부르면 안 됨)라는 대가가 있다 — ISR 이 아닌 계획 단계(부팅, 배치 시작 전)에서 한 번 부르는 용도다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 34/34 passed, 약 0.2 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 34/34, 보고 없음
```

실제 출력 (Part C 표는 위 5. 풀이에):

```text
A: AAABBB cool=2 -> AB_AB_AB  (len 8)
A: AAAAAABCDEFG cool=2 -> ABCADEAFGA__A__A  (len 16)
A: AAABBBC cool=2 -> ABCAB_AB  (len 8)
Part A exhaustive: 256 cases, len!=formula 0, invalid 0, !=brute 0
Part A random small vs brute: 600 cases, bad 0
Part A random large: 300 cases, bad 0, total idle slots 2157562
Part A validator mutations: 3000 cases, agree 3000, ref says invalid 2136
B: classic 5 jobs: dsu=142 slots[1..3]=2,0,4  heap=142 slots[1..3]=0,2,4
Part B random small vs brute: 2000 cases, bad 0
Part B random large: 50 cases, bad 0 (profit sum 2447139576)
…
34/34 passed
```

## 7. 자주 하는 실수

`demo_wrong.c` 가 틀린 그리디를 작은 입력부터 전수로 돌려 **가장 작은 반례**를 찾고, 무작위 입력에서 오답 비율을 잰다 (쿨다운은 공식 = 최적, 작업 선택은 부분집합 브루트포스 = 최적).

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_wrong.c -o demo_wrong && ./demo_wrong     # 약 0.5 초
```

```text
W1 round, id order           smallest counterexample: counts A..D={0,1,1,2} c=1 -> BCD_D (len 5, optimum 4)
W2 per-slot, smallest id     smallest counterexample: counts A..D={0,0,1,2} c=1 -> CD_D (len 4, optimum 3)
OK per-slot, max remaining   no counterexample with n <= 10, c <= 3 (all count vectors)
cooldown random (4 types, counts 0..7, c 0..5): 100000 cases
  W1 round, id order           wrong  14668 (14.7%), extra slots total 43480
  W2 per-slot, smallest id     wrong  58864 (58.9%), extra slots total 121995
  OK per-slot, max remaining   wrong      0 (0.0%), extra slots total 0
W3 profit, earliest slot     smallest counterexample: (d,p) = (2,1) (1,1) -> 1, optimum 2
W4 profit, exact slot d      smallest counterexample: (d,p) = (2,1) (2,1) -> 1, optimum 2
W5 deadline, accept-if-fits  smallest counterexample: (d,p) = (1,1) (1,2) -> 1, optimum 2
OK deadline + min-heap swap  no counterexample with n <= 3
job sequencing random (n 1..12, d 0..n, p 1..100): 20000 cases
  W3 profit, earliest slot     wrong  13501 (67.5%), profit lost 16.88% of optimum
  W4 profit, exact slot d      wrong  12850 (64.2%), profit lost 17.01% of optimum
  W5 deadline, accept-if-fits  wrong   6018 (30.1%), profit lost 5.16% of optimum
  OK deadline + min-heap swap  wrong      0 (0.0%), profit lost 0.00% of optimum
```

**1. 개수를 무시한 라운드 로빈 (W1).** 종류를 id 순서로 돌리면 `B C D _ D` (5) — 가장 많은 D 를 먼저 시작했으면 `D B D C` (4). 하한 증명이 말하듯 길이는 **가장 많은 종류**가 정한다. 고침: 개수 최대 힙.

**2. "준비된 것 아무거나" (W2).** 칸마다 준비된 종류 중 id 가 작은 것을 고르면 `C D _ D`; D 를 먼저 하면 `D C D`. 무작위 59% 가 틀린다. 반면 "준비된 것 중 **남은 개수 최대**" 는 반례가 없었다 — 연습 1 의 이벤트 루프판.

**3. 가장 이른 빈 슬롯 (W3).** `(2,1), (1,1)`: 첫 작업을 슬롯 1 에 넣으면 마감 1 작업이 갈 곳이 없다. 고침: 마감 이하 **가장 늦은** 빈 슬롯 (5. 풀이의 증명이 이 선택에 의존한다).

**4. 슬롯 d 만 보기 (W4).** `(2,1), (2,1)`: 둘 다 슬롯 2 를 원하고 두 번째는 버려진다 — 슬롯 1 이 비어 있는데도. "아래로 찾기" 가 DSU 의 역할이다.

**5. 마감 순서로 들어가면 넣기 (W5).** `(1,1), (1,2)`: 먼저 온 이익 1 이 자리를 차지. 꽉 찼을 때 **최솟값과 비교해 교체**해야 한다 — 그래서 최소 힙.

**6. 간격 off-by-one.** "cool 칸을 비운다" 는 시각 차이 ≥ cool+1. 검증기를 `t - last[s] < cool` 로 쓰면 간격 cool 을 허용한다. 테스트 `cd[]` (A 간격 2, cool 2 → 위반, cool 1 → 합법) 가 이 경계를 찍는다.

**7. 32비트 곱과 마지막 라운드 채우기.** `(f - 1) * (cool + 1)` 을 `uint32_t` 로 하면 cool = `UINT32_MAX` 에서 `cool + 1 = 0` → 공식이 `max(n, m)` 으로 무너진다 (테스트: `AA`, cool = `UINT32_MAX` → 4294967297). 그리고 마지막 라운드를 IDLE 로 채우면 길이가 최대 cool 만큼 늘고 검증기가 `SV_TRAILING_IDLE` 로 잡는다.

**8. 마감을 자르지 않음.** 마감 `UINT32_MAX` 를 슬롯 인덱스로 쓰면 `g_par[d]` 가 범위 밖이다. `min(d, n)` 으로 자르는 것이 정확하다는 근거가 보조정리 1 의 따름 정리다 (테스트 `edge[]`). 마감 0 은 어떤 슬롯도 못 쓰므로 `slot_find(0) = 0` 으로 자연히 버려진다.

## 8. 면접 Q&A

<details><summary>Q1. 공식에서 n 과 max 를 취하는 이유, 그리고 m 은 왜 더하나요?</summary>

`(f-1)(c+1)+m` 은 "최대 종류가 길이를 정하는" 경우의 하한이고, 종류가 많아 빈칸보다 작업이 많으면 IDLE 이 0 이 되어 답은 n 이다. 예: `AAABBBCCDDEE`, c = 2 → 프레임 하한 `2·3+2 = 8` 이지만 n = 12 → 12. m 은 마지막 프레임: 최대 개수를 가진 종류가 m 개면 마지막 실행들이 서로 다른 칸에 있어야 하니 마지막 줄에 m 칸. 증명은 5. 풀이 — "가장 늦게 시작한 최대 종류" 논증. 꼬리: "m 을 빼먹으면?" → `AAABBB`, c = 2 에서 7 이라고 답하게 된다 (정답 8).

</details>

<details><summary>Q2. 길이만 물었는데 왜 힙 시뮬레이션까지 짜죠? 종류가 26개가 아니라 수만 개면요?</summary>

길이만이면 공식 O(n) 이 최선이다(실측: 공식 0.3 ns/작업 vs 시뮬레이션 17 ns/작업). 하지만 펌웨어에서 필요한 건 대개 **계획표**(어느 슬롯에 어떤 명령)이고, 계획표는 검증기로 확인할 수 있어야 한다 — 공식은 증명이 필요하지만 순서는 기계적으로 검사된다. 종류 K 가 크면 시뮬레이션은 O(n log K + L), 공식은 종류별 개수를 세는 해시/정렬이 필요하다 (K 가 작으면 배열). 라운드 하나에서 꺼내는 개수는 `min(c+1, K)` 라 힙 크기가 작게 유지된다.

</details>

<details><summary>Q3. 라운드판과 "칸마다 준비된 것 중 최대" 이벤트 루프판, 어느 쪽이 맞나요? 작업이 실시간으로 도착하면요?</summary>

라운드판은 5. 풀이에서 최적성을 증명했다. 이벤트 루프판(준비된 종류 최대 힙 + 쿨다운 FIFO)은 전수(4종·개수 0..4·cool 0..4, 3125 경우)와 무작위 2000 경우에서 공식과 모두 같았다(연습 1). 다만 이 레슨에서 그 판의 일반 증명은 하지 않았다 — 면접에서는 "라운드판은 증명할 수 있고, 이벤트 루프판은 같은 그리디 원리이며 테스트로 확인했다" 고 구분해 말한다. 작업이 실시간으로 도착하면 오프라인 가정이 깨진다: 미래를 모르니 최적 보장이 없고, 그때부터는 경쟁비(competitive ratio) 같은 온라인 알고리즘 분석이나 RTOS 스케줄러의 영역이다.

</details>

<details><summary>Q4. job sequencing 그리디가 최적이라는 걸 30초 안에 어떻게 말하죠?</summary>

"실행 가능 조건은 모든 t 에서 마감 ≤ t 인 작업 수 ≤ t 이고, 이 집합족은 교환 성질을 가진다(크기가 작은 쪽에 큰 쪽 원소 하나를 더해 실행 가능하게 만들 수 있다). 교환 성질이 있으면 무게 큰 것부터 넣는 그리디가 최적이다 — 그리디가 처음 최적해보다 싼 걸 고른 자리에서 교환으로 더 비싼 원소를 끌어오면, 그리디가 그걸 먼저 봤을 때 넣었어야 한다는 모순." 그리고 "가장 늦은 빈 슬롯에 넣는 건 그 실행 가능성 검사를 O(n) 대신 DSU find 한 번으로 하기 위한 구현 장치" 를 덧붙인다. 매트로이드라는 단어는 면접관이 알면 보너스, 몰라도 위 논증이면 충분하다.

</details>

<details><summary>Q5. DSU 는 α(n) 아닌가요? 왜 O(log n) 이라고 하죠?</summary>

α(n) 은 **union by rank(또는 size) + 경로 압축** 둘 다 쓸 때의 상각 상한이다. 슬롯 DSU 는 "s 를 s-1 쪽에 붙인다" 로 합치는 방향이 고정이라 rank 를 볼 수 없고, 경로 압축만 쓰면 상각 O(log n) 이 알려진 상한이다 (Tarjan–van Leeuwen; 확인 권장). α 를 원하면 각 집합에 "가장 작은 빈 슬롯" 라벨을 따로 저장하고 rank 로 합친다. 실무적으로는 의미가 작다: 벤치에서 시간은 힙 pop 이 지배했다. DSU 의 정식 분석과 find 깊이 실측은 ALGO-M14.

</details>

<details><summary>Q6. MCU 에서 두 방법 중 무엇을 고르겠어요? 메모리와 결정성은요?</summary>

`jobseq_heap` 은 (1) 이 맥에서 2~20배 빨랐고, (2) 작업이 마감 순서로 들어오면 정렬 없이 **스트리밍**으로 돌아간다(힙 크기 ≤ 현재 마감), (3) 힙 크기 상한이 슬롯 수라 RAM 상한이 슬롯 수로 정해진다. `jobseq_dsu` 는 배치가 "뒤로 몰린" 슬롯 표를 바로 주고, 슬롯이 이미 일부 예약된 상태(점유된 슬롯을 `par[s] = s-1` 로 미리 표시)에 작업을 추가하기 쉽다. 정적 배열은 작업당 12 B(DSU 판)~17 B(힙 판), 출력 포함 — solution.c 처럼 두 방법을 다 링크하면 4096 작업 기준 약 70 KB 라 작은 MCU 에서는 n 을 줄이거나 배열을 공유해야 한다. 둘 다 재귀 없음, 최악 O(n log n) 이 결정적이다.

</details>

<details><summary>Q7. 이게 RTOS 의 EDF 와 무슨 관계죠? EDF 면 다 되는 거 아닌가요?</summary>

관계: 보조정리 1 의 증명이 "단일 코어·단위 작업에서는 EDF 순서 배치가 가능하면 항상 성공" 이라는 EDF 최적성의 가장 단순한 형태다. 차이: 여기서는 **무엇을 버릴지**(이익 최대) 와 **오프라인 단위 작업**을 다룬다. RTOS 의 EDF/RMS 분석은 주기 작업이 무한히 반복될 때 마감을 전부 지킬 수 있는지(마감 = 주기인 독립 주기 작업, 단일 코어에서 이용률 `U ≤ 1` 은 선점 EDF 의 필요충분조건, RMS 는 Liu–Layland 한계가 충분조건)를 묻고, 과부하가 되면 EDF 는 연쇄적으로 마감을 놓치는(domino effect) 성질이 있어 이런 "가치 기반 버리기" 정책이 따로 필요하다. 문맥 전환 비용, 선점, 공유 자원 차단은 이 모델에 없다.

</details>

<details><summary>Q8. 작업 길이가 1이 아니면요? (길이 t_j, 마감 d_j)</summary>

- **개수 최대화**(이익 없음): Moore–Hodgson — 마감 순서로 넣고 총 시간이 마감을 넘으면 지금까지 중 **가장 긴** 작업을 뺀다. O(n log n). LeetCode 630 Course Schedule III 이 이 문제 (연습 5, 브루트포스 3000 경우 일치).
- **이익(가중치) 최대화**: 마감을 지킨 작업의 가중치 합 최대(= 지각 작업 가중치 합 최소, `1||Σw_jU_j`)는 배낭 문제를 품고 있어 NP-hard 이고 의사다항 DP 가 있다 (확인 권장: Karp 1972, Lawler–Moore 1969). 작은 경우는 ALGO-M08 의 0/1 배낭 DP 를 마감 순서로 돌리는 꼴이 된다.
- 단위 길이일 때만 교환 성질(매트로이드)이 깨끗하게 성립한다는 것이 이 레슨 그리디의 전제다.

</details>

<details><summary>Q9. Cortex-M0 에서 cooldown_from_busy 의 나눗셈은요?</summary>

`demo_target.c` 를 `clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S` 로 보면 나눗셈기가 없어서 `bl __aeabi_uidiv` 호출이 나오고, 나머지는 몫으로 `muls`/`subs` 해서 구했다 (÷ 와 % 를 한 번의 호출로 합침). 슬롯이 상수 1000 이어도 M0 에서는 똑같이 `__aeabi_uidiv` 호출이었다 (32×32→64 곱 명령이 없다). Cortex-M4 에서는 변수 나눗셈이 `udiv` + `mls` 두 명령, 상수 1000 은 `umull` 로 0x10624DD3 을 곱하고 상위 워드를 `lsrs #6` 하는 곱셈 역수가 나왔다. 계획 단계에서 한 번 부르는 함수라 어느 쪽이든 문제없지만, ISR 안의 `%` 는 M0 에서 수십~수백 사이클짜리 라이브러리 호출이 될 수 있다는 것을 기억한다 (곱셈 역수 유도는 ALGO-M15).

</details>

## 9. 추가 연습

### 연습 1 — 이벤트 루프판 쿨다운 스케줄러

라운드 대신 칸마다: "준비된 종류(최대 힙) 중 남은 개수 최대" 를 실행하고, 실행한 종류는 쿨다운 FIFO 에 `(준비 시각, 종류)` 로 넣는다. 준비된 게 없으면 FIFO 맨 앞이 준비될 때까지 IDLE 을 한 번에 건너뛴다. 공식·검증기와 전수 대조.

<details><summary>답 보기</summary>

모든 종류의 쿨다운이 같으므로 FIFO 에 들어간 순서 = 준비 시각 순서 → 힙이 아니라 26칸 원형 버퍼면 충분하다. "IDLE 건너뛰기" 덕분에 cool 이 거대해도 루프는 작업 수 + IDLE 구간 수만큼만 돈다. 실제 출력 `연습 1 event-loop: 5125 cases, bad 0` (전수 3125 + 무작위 2000). RTOS 의 틱 없는(tickless) 대기와 같은 생각이다: 다음 사건 시각으로 바로 점프. 이 판의 최적성 일반 증명은 여기서 하지 않았다 (Q3).

</details>

### 연습 2 — Reorganize String (LeetCode 767)

같은 글자가 이웃하지 않게 재배열(불가능하면 빈 문자열). cool = 1 이고 IDLE 이 금지된 경우다.

<details><summary>답 보기</summary>

가능 ⟺ `f ≤ ⌈n/2⌉` (= 공식이 n 과 같음: `2(f-1)+m ≤ n`). 구성: 최빈 글자부터 짝수 칸 0, 2, 4, ... 를 채우고 넘치면 1, 3, 5, ... 로. 최빈 글자를 **먼저** 넣어야 짝수→홀수 경계에서 자기 자신과 이웃하지 않는다. O(n + 26). 길이 0..9 의 `{a,b,c}` 문자열 29524 개 전수: `possible 24706, bad 0` — 판정은 공식과, 출력은 이웃·개수 검사로 대조.

</details>

### 연습 3 — Task Scheduler II (LeetCode 2365)

순서를 **바꿀 수 없다**. 하루에 작업 하나 또는 쉼, 같은 종류 사이 space 일 이상. 마지막 날을 구하라. 예: `[1,2,1,2,3,1]`, space 3 → 9.

<details><summary>답 보기</summary>

순서가 고정이면 그리디 선택이 없다: 각 작업을 "어제+1" 과 "같은 종류 마지막 날 + space + 1" 중 큰 날에 놓는다 (= solution.c 브루트포스의 `ref_len_for_order`). 종류가 큰 정수면 해시맵, 여기선 `uint8_t` 라 256 칸 배열. O(n). 하루씩 시뮬레이션과 무작위 3000 경우 일치. 면접에서 621 과 2365 를 헷갈리지 않게 "순서를 바꿔도 되나?" 를 먼저 묻는 이유.

</details>

### 연습 4 — Rearrange String k Distance Apart (LeetCode 358)

같은 글자 간격 ≥ k, IDLE 없이. 불가능하면 실패.

<details><summary>답 보기</summary>

라운드판에서 폭을 k 로 두고, **채울 IDLE 이 필요해지는 순간**(남은 게 있는데 라운드가 k 개 미만) 실패를 돌려준다. 가능 ⟺ `max(n, (f-1)k + m) == n`. 4종·개수 0..4·k 0..5 전수 3750 경우: `possible 2197, bad 0` (판정은 공식과, 출력은 검증기로).

</details>

### 연습 5 — Course Schedule III (LeetCode 630) = Moore–Hodgson

작업마다 길이 `dur` 와 끝내야 하는 날 `last`. 최대 몇 개를 할 수 있나? 예: `[[100,200],[200,1300],[1000,1250],[2000,3200]]` → 3.

<details><summary>답 보기</summary>

마감 순서로 보며 길이를 **최대** 힙에 넣고 누적 시간에 더한다. 누적이 이번 마감을 넘으면 힙에서 가장 긴 것을 뺀다. 개수는 한 번에 최대 1 줄고, 같은 개수라면 총 시간이 가장 짧은 집합을 들고 가는 것이 불변식이다 — 본문 `jobseq_heap` 과 같은 "넣고, 넘치면 가장 나쁜 것 하나 빼기" 모양이다 (거기선 가장 싼 것, 여기선 가장 긴 것). 부분집합 브루트포스(각 부분집합은 EDF 순서로만 검사하면 충분)와 무작위 3000 경우 일치.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed, 약 0.2 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 12/12, 보고 없음
```

## 10. 핵심 정리

- 쿨다운 스케줄러 = `max(n, (f-1)(c+1) + m)`. 하한은 "가장 늦게 시작한 최대 종류", 상한은 최대 힙 라운드판(동점은 id 로 일관되게 → 위치 단조 → 쿨다운 성립).
- 라운드판: c+1 개 꺼내 둔 채 실행 → 개수-1 → 다시 넣기 → 남았으면 IDLE 채움, **마지막 라운드는 채우지 않음**. 곱은 64비트.
- 순서는 독립 검증기로: 개수·간격 ≥ c+1·id·끝 IDLE. 공식은 길이, 검증기는 순서를 보장.
- 단위 작업 실행 가능 ⟺ `∀t: N_t ≤ t` (EDF 배치). 교환 성질 → 이익 그리디 최적.
- 이익 순서 + **가장 늦은** 빈 슬롯(DSU, `par[s] = s-1`, 0 = 보초). 가장 이른 슬롯·정확히 d 만은 n = 2 반례.
- 마감 순서 + 최소 힙: 크기 > d 면 최솟값 버림. 스트리밍 가능, 이 맥에서 DSU 판보다 2~20배 빠름.
- 마감 > n 은 n 으로 자른다 (정당성 = EDF 배치). naive 슬롯 탐색은 실제로 n(n+1)/2 단계.
- 오프라인 계획 ≠ RTOS 스케줄 가능성(RMS/EDF 이용률). 길이를 줄이는 큰 손잡이는 데이터 배치(가장 바쁜 장치의 f).

## 참고자료

- AlgoMonster 노트 M08 §6 Reorganize String (Don 정리본) — `algomaster-coding-pattern/original-md/2026-09-27_M08_priority_queue_heap.md` (연습 2, "moving best" 힙 패턴과 Task Scheduler 언급). M11 §22 Greedy Introduction 도 참고. 같은 노트 M07 §17·§19 "Task Scheduling" 은 이름만 같고 **의존성 순서(위상 정렬)** 문제라 ALGO-M05 쪽이다.
- T. H. Cormen, C. E. Leiserson, R. L. Rivest, C. Stein, *Introduction to Algorithms* 3판, §16.4 (매트로이드와 그리디), §16.5 "A task-scheduling problem as a matroid" — 단위 작업·마감·벌점 문제와 실행 가능 조건 (4판에서는 장·절 위치가 다를 수 있음, 확인 권장).
- J. Kleinberg, É. Tardos, *Algorithm Design*, Ch. 4 (그리디: "greedy stays ahead" 와 교환 논증, interval scheduling / minimizing lateness).
- R. E. Tarjan, J. van Leeuwen, "Worst-case analysis of set union algorithms", *JACM* 31(2), 1984 — 경로 압축만 / 경로 반감의 상각 상한.
- J. M. Moore, "An n job, one machine sequencing algorithm for minimizing the number of late jobs", *Management Science* 15(1), 1968 (Hodgson 의 간소화 포함) — 연습 5.
- C. L. Liu, J. W. Layland, "Scheduling Algorithms for Multiprogramming in a Hard-Real-Time Environment", *JACM* 20(1), 1973 — RMS 이용률 한계, EDF 최적성 (RTOS 와의 구분).
- Microchip 24AA256/24LC256/24FC256 데이터시트 — 쓰기 사이클 시간 tWR(최대 5 ms), 64 B 페이지, ACK 폴링 (Part C 수치, 확인 권장).
- LeetCode 621 Task Scheduler · 767 Reorganize String · 358 Rearrange String k Distance Apart · 2365 Task Scheduler II · 630 Course Schedule III. Job sequencing with deadlines 는 LeetCode 번호가 없는 고전 문제(GeeksforGeeks 등의 예 `(2,100),(1,19),(2,27),(1,25),(3,15)` → 142).
- 관련 레슨: DS-B12 (이진 힙), DS-M11 (인덱스 우선순위 큐), ALGO-M05 (위상 정렬 — 의존성 있는 작업 순서), ALGO-M09 (구간 그리디와 교환 논증), ALGO-M14 (Union-Find, α(n)), ALGO-M08 (0/1 배낭 — 가중 지각 작업 변형), ALGO-M15 (상수 나눗셈의 곱셈 역수), RTOS 트랙 (RMS/EDF 스케줄 가능성).
