---
id: OS-B11
time: 60분
updated: 2026-11-11
---
# OS-B11 페이지 교체 시뮬레이터: FIFO/LRU/Clock/OPT

## 학습 목표

- 참조 문자열(reference string)과 프레임 수를 받아 **FIFO · LRU · Clock(use bit 1개) · OPT** 의 hit/miss 와 희생자(victim)를 내는 시뮬레이터를 C 로 짜고, OSTEP 공식 시뮬레이터(`paging-policy.py`)와 **참조 하나하나** 대조해 맞춘다.
- 프레임 1..N 의 폴트 곡선을 그려 FIFO 의 **Belady 이상**(1,2,3,4,1,2,5,1,2,3,4,5 에서 3 프레임 9 폴트 → 4 프레임 10 폴트)을 직접 확인하고, LRU·OPT 에서는 왜 안 생기는지 **포함 관계(inclusion property)** 를 코드로 검사해 설명한다.
- Clock 이 LRU 를 얼마나 닮았는지 숫자로 말한다: 히트율 차이, "Clock 의 희생자가 LRU 순서로 몇 번째였나" 분포, 바늘 이동 비용.
- 세 가지 작업 부하(지역성 없음 / 80-20 / 반복)에서 정책별 히트율 곡선을 실측으로 그리고, "LRU 가 최악인 부하" 를 설명한다.
- 펌웨어 쪽 대응을 말한다: SSD FTL 의 매핑 캐시(DFTL), Linux 페이지 캐시, 하드웨어 Accessed/Dirty 비트.

## 1. 문제

"LRU 랑 FIFO 차이요? Belady 이상이 뭐죠? 진짜 OS 는 LRU 를 쓰나요?" — OS 면접 단골이고, 펌웨어 면접에서는 "SRAM 이 작아서 매핑 테이블 일부만 캐시한다면 무엇을 내보낼래?" 로 모양만 바뀌어 나온다. Don 이 했던 SSD 펌웨어의 **DFTL 매핑 캐시**(MEM-A09), 데이터베이스 버퍼 풀(STOR-B04), CPU 캐시의 교체(ARCH-M05), Linux 페이지 캐시(LPERF-B08)가 전부 같은 문제다. MMU 가 없는 Cortex-M 에는 페이지 교체가 없지만, 외부 플래시를 SRAM 에 올려 쓰는 소프트웨어 캐시나 오버레이를 만들면 똑같은 결정을 직접 짜야 한다.

설정은 단순하다. 물리 메모리에 프레임(frame)이 f 개 있고, 프로그램이 페이지 번호를 차례로 참조한다. 참조한 페이지가 프레임에 있으면 hit, 없으면 miss(page fault) — 빈 프레임이 있으면 거기 올리고, 꽉 찼으면 **누군가를 내보내야** 한다. 정책은 "누구를" 만 정한다. OSTEP 22장의 네 정책:

| 정책 | 내보낼 페이지 | 필요한 정보 | 한 줄 평 |
|---|---|---|---|
| FIFO | 가장 먼저 **적재된** 페이지 | 적재 순서 | 싸다. 자주 쓰는 페이지도 순서가 되면 내보낸다 |
| LRU | 가장 오래 **참조 안 된** 페이지 | 참조할 때마다 시각 갱신 | 과거로 미래를 추측. 매 hit 마다 장부를 고쳐야 해서 비싸다 |
| Clock | 바늘을 돌리며 use bit 가 0 인 첫 페이지 (1 이면 0 으로 지우고 한 번 봐줌) | 참조 시 use bit = 1 (하드웨어가 해 줌) | LRU 근사. hit 경로에 할 일이 거의 없다 |
| OPT (Belady MIN) | **다음 참조가 가장 먼** 페이지 | 미래 전체 | 실현 불가, 하지만 최솟값 — 비교 기준 |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Clock: 프레임을 원형으로 놓고 바늘(hand)이 돈다 — use bit 1개 = "최근에 쓰였나"</text>
  <circle cx="160" cy="160" r="95" fill="none" stroke="currentColor" stroke-dasharray="4 4"/>
  <rect x="125" y="48" width="70" height="34" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="133" y="70" fill="currentColor" font-family="monospace">p7 u=1</text>
  <rect x="215" y="88" width="70" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="223" y="110" fill="currentColor" font-family="monospace">p3 u=0</text>
  <rect x="215" y="198" width="70" height="34" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="223" y="220" fill="currentColor" font-family="monospace">p9 u=1</text>
  <rect x="125" y="238" width="70" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="133" y="260" fill="currentColor" font-family="monospace">p1 u=0</text>
  <rect x="35" y="198" width="70" height="34" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="43" y="220" fill="currentColor" font-family="monospace">p4 u=1</text>
  <rect x="35" y="88" width="70" height="34" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="43" y="110" fill="currentColor" font-family="monospace">p2 u=1</text>
  <line x1="160" y1="160" x2="218" y2="205" stroke="currentColor" stroke-width="3"/>
  <circle cx="160" cy="160" r="5" fill="currentColor"/>
  <text x="168" y="160" fill="currentColor">hand</text>
  <text x="320" y="62" fill="currentColor">miss 가 나서 희생자가 필요하면 바늘 위치부터:</text>
  <text x="330" y="88" fill="currentColor" font-family="monospace">p9 u=1 → u=0 으로 지우고 다음 칸 (두 번째 기회)</text>
  <text x="330" y="110" fill="currentColor" font-family="monospace">p1 u=0 → 희생자! 여기에 새 페이지, u=1</text>
  <text x="330" y="132" fill="currentColor" font-family="monospace">바늘은 희생자 다음 칸(p4)으로</text>
  <rect x="320" y="150" width="400" height="62" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="332" y="174" fill="currentColor">hit 때 하는 일: use bit = 1 — x86 PTE 의 A 비트처럼</text>
  <text x="332" y="198" fill="currentColor">하드웨어가 해 주면 OS 는 hit 경로에서 아무것도 안 한다</text>
  <text x="320" y="240" fill="currentColor">LRU 는 hit 마다 "가장 최근" 으로 옮겨야 한다 (리스트 조작).</text>
  <text x="320" y="262" fill="currentColor">Clock 은 그 비용을 miss 때의 바늘 스캔으로 미룬다.</text>
  <text x="320" y="284" fill="currentColor">모든 u=1 이면 한 바퀴 다 지우고 원위치 → 바늘 위치로 고른다.</text>
</svg>
```

### Part A — 시뮬레이터: 희생자 고르기와 참조 하나 처리

시뮬레이터 상태 `sim_t` 는 프레임마다 `page`, `loaded`(적재 시각), `last`(마지막 참조 시각), `nextuse`(다음 참조 위치), `use`(use bit)를 갖고, Clock 의 `hand` 와 `hand_steps`(바늘이 본 칸 수)를 갖는다. 하네스가 `next[t]` (참조 t 의 페이지가 다음에 나오는 위치, 없으면 n) 를 미리 계산해 준다.

- `int pick_victim(sim_t *s)` — 정책에 따라 내보낼 프레임. OPT 동점(둘 다 다시 안 쓰임)은 **마지막 참조가 오래된 쪽**.
- `acc_t sim_access(sim_t *s)` — `refs[t]` 하나 처리: hit / 빈 프레임 / 희생자 교체, 그리고 `last`, `nextuse`, `use` 갱신. 내보냈으면 그 페이지와 그 페이지의 `last` 를 돌려준다.
- 테스트: OSTEP 22장 예제(그림 22.1·22.2·22.5)의 hit 수와 희생자 순서, Clock 바늘 이동 수, 프레임 1개/충분할 때의 경계, **정의로 계산한 기준값**과 무작위 1200 사례 대조 — LRU 는 스택 거리(stack distance), OPT 는 가능한 모든 교체를 다 해 보는 DP. 그리고 `demo_ostep_check.sh` 로 OSTEP 시뮬레이터와 95회 대조(+ CLOCK 3회는 참고만).

### Part B — 폴트 곡선, Belady 이상, 포함 관계

- `void fault_curve(policy, refs, n, maxf, faults)` — `faults[f]` = f 프레임일 때 미스 수 (f = 1..maxf).
- `int belady_find(faults, maxf)` — 프레임을 늘렸는데 폴트가 늘어난 첫 f, 없으면 0.
- `bool inclusion_ok(policy, refs, n, maxf, &bad_f, &bad_t)` — 모든 f 와 모든 시각 t 에서 "f 프레임의 상주 집합 ⊆ f+1 프레임의 상주 집합" 인가. 깨지는 첫 (f, t).
- 테스트: Belady 문자열에서 FIFO 9 → 10 (직접 계산한 값, OSTEP 시뮬레이터로도 확인), `belady_find == 3`, LRU·OPT 는 0, FIFO 포함 관계는 f=3 에서 깨짐. 무작위 5000 문자열: LRU·OPT 포함 관계 위반 0, FIFO Belady 사례 > 0, **Belady 가 나온 문자열은 반드시 포함 관계가 깨져 있다**.

### Part C — Clock 은 LRU 를 얼마나 닮았나

- `long clock_lru_rank(refs, n, nf, hist, &st)` — Clock 을 돌리며 교체가 일어날 때마다 희생자의 **LRU 순위**(rank: 남은 페이지 중 희생자보다 마지막 참조가 더 오래된 것의 수, 0 = LRU 와 같은 선택)를 히스토그램에 센다.
- 테스트: 손 계산 예(1 2 3 4 2 3 5 → 희생자 1(rank 0), 2(rank 1); LRU 라면 1, 4), 반복 부하에서 LRU·FIFO·Clock 모두 hit 0 + Clock 은 매번 진짜 LRU 를 고르지만 교체당 평균 2칸을 본다, 균등 부하에서 미래를 모르는 정책은 모두 f% ± 3%p, 80-20 에서 OPT ≥ 나머지 + FIFO ≤ Clock + LRU−Clock < 4%p.
- 펌웨어 맥락: 같은 실험을 "SRAM 에 매핑 엔트리 f 개만 캐시하는 FTL" 로 읽으면 된다 — 80-20 은 hot/cold LBA, 반복 부하는 캐시보다 큰 범위의 순차 읽기다.

## 2. Problem (English)

> **Page replacement simulator.**
>
> Given a reference string of page numbers and a number of physical frames, simulate demand paging with four replacement policies and report hits, misses and the victim of every eviction:
>
> ```c
> int   pick_victim(sim_t *s);      // FIFO: oldest load; LRU: oldest reference;
>                                   // OPT: farthest next use (ties: oldest reference);
>                                   // CLOCK: one use bit per frame + a hand (second chance)
> acc_t sim_access(sim_t *s);       // process refs[t]: hit / fill a free frame / evict + load
> void  fault_curve(policy_t p, const int *refs, long n, int maxf, long *faults);   // f = 1..maxf
> int   belady_find(const long *faults, int maxf);   // first f with faults[f+1] > faults[f], else 0
> bool  inclusion_ok(policy_t p, const int *refs, long n, int maxf, int *bad_f, long *bad_t);
> long  clock_lru_rank(const int *refs, long n, int nf, long *hist, stats_t *st);
> ```
>
> Constraints: up to 128 frames, page numbers < 1024 (< 64 for the inclusion check), fully associative memory, frames start empty, a newly loaded page counts as referenced (use bit = 1).
>
> Example: refs `0 1 2 0 1 3 0 3 1 2 1`, 3 frames → FIFO 4 hits, LRU 6, OPT 6. Refs `1 2 3 4 1 2 5 1 2 3 4 5` → FIFO faults with 3 frames: 9, with 4 frames: 10.
>
> Follow-ups: Why can FIFO get worse with more memory while LRU cannot? Why do real kernels approximate LRU instead of implementing it? What happens to Clock when every use bit is set? Which workload is LRU's worst case, and how do real systems defend against it? How would dirty pages change the choice?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력, 일부 생략):

```text
FAIL starter.c:242  st[POL_FIFO].hits == 4 && same_ints(ev[POL_FIFO], e_fifo, n)
FAIL starter.c:243  st[POL_LRU].hits == 6 && same_ints(ev[POL_LRU], e_lru, n)
...
random small strings: 1200 cases, LRU vs stack-distance mismatches=1173, OPT vs brute-force mismatches=1177
Belady string faults, frames 1..7
  FIFO    0   0   0   0   0   0   0
...
FAIL starter.c:384  opt_pos
...
7/32 passed
```

통과하는 7개는 스텁이 우연히 맞히는 것들(예: "반복 부하에서 LRU hit 0", "Belady 가 나오면 포함 관계가 깨져 있다" 같은 함의)이다. 하네스 설계:

- **시간·프로세스·파일 없음.** 순수 계산이라 매 실행 같은 결과이고, 무작위는 전부 xorshift32 고정 시드다. 실행 0.1 초 안쪽.
- **기준값은 정책 구현과 독립적으로 계산한다.** LRU 는 "직전 참조 이후 나온 서로 다른 페이지 수 < f 면 hit" (스택 거리의 정의), OPT 는 "매 미스마다 내보낼 수 있는 모든 페이지를 다 해 보는" 상태 DP (페이지 < 8, 상태 = 상주 비트마스크 256개). 내 코드가 이 둘과 1200 사례에서 다르면 FAIL.
- `sim_run()` 은 하네스가 준다: 참조 문자열 전체에 `sim_access()` 를 부르며 hit/miss, 희생자 배열, 상주 집합 비트마스크를 모은다.
- demo 와 공유하려고 `#ifndef OSB11_NO_MAIN` 으로 테스트를 감쌌다. demo 는 `#define OSB11_NO_MAIN` 후 `#include "solution.c"` — 즉 **demo 결과는 정답 코드 그 자체의 결과** 다.

관찰용 파일 (레슨 폴더에서):

```bash
sh demo_ostep_check.sh                       # OSTEP paging-policy.py 와 참조 단위 대조 (python3 -I, 읽기 전용)
cc -std=c11 -Wall -Wextra -Werror -O2 demo_curves.c -o demo_curves && ./demo_curves      # 히트율 표 + Clock 근사 지표
./demo_curves csv > /tmp/OS-B11_curves.csv   # 프레임 1..100 전부 (그림용)
```

## 4. 힌트

<details>
<summary>힌트 1 — 네 정책은 "어떤 숫자의 최소/최대" 로 통일된다</summary>

FIFO = `loaded` 최소, LRU = `last` 최소, OPT = `nextuse` 최대. 그러니 `sim_access` 가 정책과 상관없이 **모든 필드를 항상 갱신** 하면 `pick_victim` 은 비교 대상만 바꾸면 된다. 주의: `loaded` 는 적재할 때만, `last`·`nextuse`·`use` 는 hit 일 때도 갱신한다. LRU 에서 hit 때 `last` 를 안 고치면 그건 FIFO 다.

</details>

<details>
<summary>힌트 2 — OPT 의 "미래" 를 매번 훑지 말 것</summary>

`next[t]` = t 이후 같은 페이지가 처음 나오는 위치. 뒤에서부터 한 번 훑으면 O(n) 에 만들어진다(하네스 `build_next`). 페이지가 참조될 때마다 그 프레임의 `nextuse = next[t]` 로 두면, 희생자 고르기는 프레임 f 개 중 최대 찾기다. 다시 안 쓰이는 페이지는 `nextuse == n` 으로 동점 — 이때 규칙은 **프레임 수와 무관한 것** 이어야 한다(Part B 에서 이유가 나온다).

</details>

<details>
<summary>힌트 3 — Clock 루프는 반드시 끝난다</summary>

바늘 위치에서 `use == 1` 이면 0 으로 지우고 다음 칸, `use == 0` 이면 그게 희생자. 최악은 전부 1 일 때: 한 바퀴 돌며 다 지우고 처음 칸으로 돌아와 그 칸을 고른다 (f+1 칸). 고른 뒤 바늘은 희생자 **다음** 칸으로. 새로 올린 페이지의 use 는 1 (방금 참조했으니까).

</details>

<details>
<summary>힌트 4 — 포함 관계는 비트마스크 AND 로</summary>

f 프레임과 f+1 프레임으로 각각 돌려 시각마다 상주 집합 비트마스크 `a[t]`, `b[t]` 를 얻는다. `a[t] & ~b[t]` 가 0 이 아니면 "작은 메모리에는 있는데 큰 메모리에는 없는 페이지" 가 있는 것 — 포함 관계 위반. 이것이 한 번도 없으면 모든 t 에서 작은 쪽의 hit 는 큰 쪽에서도 hit 이므로 폴트가 늘 수 없다.

</details>

## 5. 풀이

### Part A — 정답의 모양

```c
static acc_t sim_access(sim_t *s) {
    acc_t a = { false, -1, -1, -1 };
    long t = s->t++;  int p = s->refs[t];
    int f = sim_find(s, p);
    if (f >= 0) a.hit = true;                              /* hit: 적재 시각은 그대로 */
    else {
        if (s->used < s->nframes) f = s->used++;           /* 빈 프레임 (강제 미스 구간) */
        else { f = pick_victim(s); a.evicted = s->page[f]; a.evicted_last = s->last[f]; }
        s->page[f] = p;  s->loaded[f] = t;
    }
    s->last[f] = t;  s->nextuse[f] = s->next[t];  s->use[f] = 1;   /* hit 이든 miss 든 */
    a.frame = f;  return a;
}
```

`pick_victim` 의 네 갈래:

| 정책 | 비교 | 비고 |
|---|---|---|
| FIFO | `loaded[f]` 최소 | hit 은 아무것도 안 바꾼다 |
| LRU | `last[f]` 최소 | 실제 커널이라면 hit 마다 리스트 맨 앞으로 옮기기 (DS-M05 의 해시 + 이중 연결 리스트) |
| OPT | `nextuse[f]` 최대, 동점이면 `last[f]` 최소 | 동점은 "둘 다 다시 안 쓰임" 일 때만 생긴다 (서로 다른 페이지가 같은 위치에 나올 수 없다) |
| Clock | `use[hand]` 가 1 이면 0 으로 + `hand++`, 0 이면 희생자 | 본 칸마다 `hand_steps++`, 끝나면 `hand = 희생자 + 1` |

**OSTEP 예제 (0 1 2 0 1 3 0 3 1 2 1, 프레임 3) — 내 시뮬레이터 출력과 책 그림 비교:**

| t | 참조 | FIFO | LRU | OPT | Clock |
|---|---|---|---|---|---|
| 0–2 | 0 1 2 | 강제 미스 ×3 | 〃 | 〃 | 〃 (모두 u=1, 바늘 0) |
| 3–4 | 0 1 | hit hit | hit hit | hit hit | hit hit |
| 5 | 3 | miss, **0** 내보냄 | miss, **2** | miss, **2** | 한 바퀴 지우고 **0** (4칸) |
| 6 | 0 | miss, 1 | hit | hit | miss, 1 (1칸) |
| 7 | 3 | hit | hit | hit | hit |
| 8 | 1 | miss, 2 | hit | hit | miss, 2 (1칸) |
| 9 | 2 | miss, 3 | miss, 0 | miss, **0** (책은 3) | 한 바퀴, 3 (4칸) |
| 10 | 1 | hit | hit | hit | hit |
| hits | | **4** | **6** | **6** | **4** |

- Clock 은 첫 교체 때 모든 use bit 가 1 이라 정보가 없다 → 한 바퀴 다 지우고 바늘 자리(= 가장 먼저 적재된 프레임)를 고른다. 그래서 이 짧은 예에서는 FIFO 와 희생자까지 똑같다. 테스트: `hand_steps == 4+1+1+4`.
- OPT 의 t=9: 0 과 3 모두 다시 안 쓰인다. OSTEP 그림 22.1 은 3 을 내보내고 본문에 "0 would have been a fine choice too" 라고 적었다. 나는 동점이면 마지막 참조가 오래된 0 (t=6) 을 내보낸다. **미스 수는 동점 처리와 무관하다** — 다시 안 쓰일 페이지는 누굴 버려도 미래의 hit/miss 가 같다. 동점 규칙이 문제가 되는 곳은 Part B 의 포함 관계다.

**Clock 의 두 번째 기회가 실제로 통하는 예 (1 2 3 4 2 5 2, 프레임 3):** t=3 에서 한 바퀴 돌아 1 을 내보내면 2, 3 의 use 가 0 이 된다. t=4 의 hit 가 2 의 use 를 다시 1 로. t=5 에서 바늘은 2(u=1 → 지움) 를 건너뛰고 3(u=0) 을 고른다(2칸). FIFO 는 여기서 2 를 내보내고 t=6 에서 또 미스. 결과: FIFO 1 hit, Clock 2 hit, LRU 2 hit (희생자도 LRU 와 같음).

**정의로 만든 기준값과 대조 (실제 출력):**

```text
OSTEP ex (3 frames) hits: FIFO=4 LRU=6 OPT=6 CLOCK=4
random small strings: 1200 cases, LRU vs stack-distance mismatches=0, OPT vs brute-force mismatches=0
```

- LRU 의 정의를 바꿔 말하면: f 프레임 LRU 의 내용 = **가장 최근에 참조된 서로 다른 f 개 페이지**. 그러니 참조 t 가 hit ⇔ 같은 페이지의 직전 참조 이후 서로 다른 페이지가 f 개 미만 나왔다(스택 거리 ≤ f). 시뮬레이션 없이 계산되는 독립 기준이다.
- OPT 가 정말 최소인지는 "모든 교체 선택" 을 다 해 보는 DP 로 확인했다(Belady 1966 의 정리를 1200 사례로 검증한 셈). 그리고 같은 1200 사례에서 FIFO·LRU·Clock 중 OPT 보다 미스가 적은 경우는 0.

**OSTEP 시뮬레이터와 대조 — `sh demo_ostep_check.sh` (실제 출력, 발췌):**

`demo_ostep.c` 가 `python3 -I paging-policy.py ... -c` 의 출력을 stdin 으로 받아 `Access:` 줄의 참조·HIT/MISS·Replaced 를 읽고, 같은 문자열을 내 `sim_access()` 로 돌려 참조마다 비교한다.

```text
== OSTEP 22장 예제 / Belady 문자열 ==
FIFO  s=-a C=3 n=11  hits mine=4 ostep=4  hit/miss diff=0 victim diff=0 (OPT dead-page ties=0)  MATCH
LRU   s=-a C=3 n=11  hits mine=6 ostep=6  hit/miss diff=0 victim diff=0 (OPT dead-page ties=0)  MATCH
OPT   s=-a C=3 n=11  hits mine=6 ostep=6  hit/miss diff=0 victim diff=0 (OPT dead-page ties=1)  MATCH
FIFO  s=-a C=3 n=12  hits mine=3 ostep=3  hit/miss diff=0 victim diff=0 (OPT dead-page ties=0)  MATCH
FIFO  s=-a C=4 n=12  hits mine=2 ostep=2  hit/miss diff=0 victim diff=0 (OPT dead-page ties=0)  MATCH
== 무작위: seed 0..9, 40 참조, 페이지 0..9, 캐시 3/4/5 ==
FIFO  s=0  C=3 n=40  hits mine=11 ostep=11  hit/miss diff=0 victim diff=0 (OPT dead-page ties=0)  MATCH
LRU   s=0  C=3 n=40  hits mine=11 ostep=11  hit/miss diff=0 victim diff=0 (OPT dead-page ties=0)  MATCH
OPT   s=0  C=3 n=40  hits mine=18 ostep=18  hit/miss diff=0 victim diff=0 (OPT dead-page ties=0)  MATCH
OPT   s=1  C=4 n=40  hits mine=21 ostep=21  hit/miss diff=0 victim diff=0 (OPT dead-page ties=5)  MATCH
...
LRU   s=9  C=5 n=40  hits mine=17 ostep=17  hit/miss diff=0 victim diff=0 (OPT dead-page ties=0)  MATCH
OPT   s=9  C=5 n=40  hits mine=24 ostep=24  hit/miss diff=0 victim diff=0 (OPT dead-page ties=0)  MATCH
== CLOCK (참고: OSTEP 은 무작위 스캔 + 2비트 카운터) ==
CLOCK s=0  C=4 n=40  hits mine=14 ostep=10  (CLOCK: not compared — OSTEP picks frames at random)
CLOCK s=1  C=4 n=40  hits mine=19 ostep=17  (CLOCK: not compared — OSTEP picks frames at random)
CLOCK s=2  C=4 n=40  hits mine=13 ostep=13  (CLOCK: not compared — OSTEP picks frames at random)
runs=98 mismatches=0
```

- FIFO·LRU·OPT 95회 전부 **hit/miss 와 희생자까지 일치**. 희생자가 달랐던 51건(OPT 20회)은 전부 "두 페이지 모두 이후 다시 안 나옴" 인 동점이었다 — OSTEP 코드는 `if whenReferenced >= maxReplace` 라서 동점이면 리스트 뒤쪽(= 가장 최근에 적재된 페이지)을 고른다.
- CLOCK 은 대조할 수 없다: `paging-policy.py` 의 CLOCK 은 바늘 대신 `memory[int(random.random() * count)]` 로 **무작위 프레임** 을 보고, use 값이 0..`clockbits`(기본 2) 카운터다 (코드에 `# hack: for now, do random` 주석). OSTEP 그림 22.9 의 "randomly scans pages" 변형 그대로다. 그래서 hit 수만 참고로 나란히 둔다.

### Part B — Belady 이상: 메모리를 늘렸는데 폴트가 늘었다

`fault_curve` 는 프레임 수마다 `sim_run` 을 돌리면 끝이고, `belady_find` 는 곡선이 올라가는 첫 지점을 찾는다. 실제 출력:

```text
Belady string faults, frames 1..7
  FIFO   12  12   9  10   5   5   5
  LRU    12  12  10   8   5   5   5
  OPT    12   9   7   6   5   5   5
  CLOCK  12  12   9  10   5   5   5
  FIFO inclusion breaks first at f=3, t=6
```

OSTEP 시뮬레이터로도 같은 값(3 프레임 `hits 3 misses 9`, 4 프레임 `hits 2 misses 10`)이 나왔다. 두 실행을 나란히 놓으면 (OSTEP 출력의 메모리 상태):

| t | 참조 | 3 프레임 (FIFO 순) | 4 프레임 (FIFO 순) | 3 ⊆ 4 ? |
|---|---|---|---|---|
| 3 | 4 | [2,3,4] miss | [1,2,3,4] miss | ✓ |
| 4 | 1 | [3,4,1] miss | [1,2,3,4] **hit** | ✓ |
| 5 | 2 | [4,1,2] miss | [1,2,3,4] **hit** | ✓ |
| 6 | 5 | [1,2,5] miss | [2,3,4,5] miss (1 내보냄) | **✗ — 1 이 3 프레임에만 있다** |
| 7 | 1 | [1,2,5] **hit** | [3,4,5,1] miss | ✗ |
| 8 | 2 | [1,2,5] **hit** | [4,5,1,2] miss | ✗ |
| 9–11 | 3 4 5 | miss miss hit | miss miss miss | |

4 프레임은 t=4, 5 에서 hit 을 두 번 벌었는데, 그 덕분에 1, 2 가 "오래된 적재" 로 남아 t=6 에서 1 이 쫓겨난다. 3 프레임은 t=4, 5 에서 1, 2 를 **다시 적재** 했기 때문에 그 둘이 "새것" 이다. FIFO 의 우선순위(적재 시각)가 **메모리 크기에 따라 달라지는** 것이 원인이다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Belady 문자열 1,2,3,4,1,2,5,1,2,3,4,5 — 프레임 수별 폴트 (실측)</text>
  <line x1="50" y1="230" x2="560" y2="230" stroke="currentColor"/>
  <line x1="50" y1="50" x2="50" y2="230" stroke="currentColor"/>
  <text x="40" y="234" fill="currentColor" text-anchor="end">0</text>
  <text x="40" y="94" fill="currentColor" text-anchor="end">10</text>
  <line x1="46" y1="90" x2="560" y2="90" stroke="currentColor" stroke-width="0.4" stroke-dasharray="2 3"/>
  <g stroke="currentColor">
    <rect x="62"  y="62"  width="22" height="168" class="f-acc"/><rect x="86"  y="62"  width="22" height="168" class="f-ok"/><rect x="110" y="62"  width="22" height="168" class="f-mut"/>
    <rect x="147" y="62"  width="22" height="168" class="f-acc"/><rect x="171" y="62"  width="22" height="168" class="f-ok"/><rect x="195" y="104" width="22" height="126" class="f-mut"/>
    <rect x="232" y="104" width="22" height="126" class="f-acc"/><rect x="256" y="90"  width="22" height="140" class="f-ok"/><rect x="280" y="132" width="22" height="98"  class="f-mut"/>
    <rect x="317" y="90"  width="22" height="140" class="f-warn"/><rect x="341" y="118" width="22" height="112" class="f-ok"/><rect x="365" y="146" width="22" height="84"  class="f-mut"/>
    <rect x="402" y="160" width="22" height="70"  class="f-acc"/><rect x="426" y="160" width="22" height="70"  class="f-ok"/><rect x="450" y="160" width="22" height="70"  class="f-mut"/>
    <rect x="487" y="160" width="22" height="70"  class="f-acc"/><rect x="511" y="160" width="22" height="70"  class="f-ok"/><rect x="535" y="160" width="22" height="70"  class="f-mut"/>
  </g>
  <text x="97"  y="250" fill="currentColor" text-anchor="middle">f=1</text>
  <text x="182" y="250" fill="currentColor" text-anchor="middle">f=2</text>
  <text x="267" y="250" fill="currentColor" text-anchor="middle">f=3</text>
  <text x="352" y="250" fill="currentColor" text-anchor="middle">f=4</text>
  <text x="437" y="250" fill="currentColor" text-anchor="middle">f=5</text>
  <text x="522" y="250" fill="currentColor" text-anchor="middle">f=6</text>
  <text x="243" y="99"  fill="currentColor" text-anchor="middle" font-size="12">9</text>
  <text x="328" y="85"  fill="currentColor" text-anchor="middle" font-size="12">10</text>
  <text x="352" y="272" fill="currentColor" text-anchor="middle">FIFO 9 → 10</text>
  <rect x="590" y="60"  width="16" height="14" class="f-acc" stroke="currentColor"/><text x="612" y="72"  fill="currentColor">FIFO</text>
  <rect x="590" y="84"  width="16" height="14" class="f-ok"  stroke="currentColor"/><text x="612" y="96"  fill="currentColor">LRU</text>
  <rect x="590" y="108" width="16" height="14" class="f-mut" stroke="currentColor"/><text x="612" y="120" fill="currentColor">OPT</text>
  <rect x="590" y="132" width="16" height="14" class="f-warn" stroke="currentColor"/><text x="612" y="144" fill="currentColor">이상(anomaly)</text>
  <text x="590" y="180" fill="currentColor">LRU 12,12,10,8,5,5</text>
  <text x="590" y="200" fill="currentColor">OPT 12,9,7,6,5,5</text>
  <text x="590" y="220" fill="currentColor">→ 내려가기만 한다</text>
</svg>
```

**포함 관계(inclusion property)와 스택 알고리즘.** Mattson 등(1970)의 정의: 모든 시각 t 에서 "f 프레임의 상주 집합 ⊆ f+1 프레임의 상주 집합" 이면 **스택 알고리즘** 이다. 그러면 f 프레임에서 hit 인 참조는 f+1 프레임에서도 hit → 폴트 곡선은 절대 올라가지 않는다. 충분조건은 "매 시각 페이지들의 우선순위가 **메모리 크기와 무관하게** 정해지고, 항상 우선순위가 가장 낮은 페이지를 내보낸다" 이다.

| 정책 | 우선순위 | 크기와 무관? | 스택 알고리즘? |
|---|---|---|---|
| LRU | 마지막 참조 시각 — 참조 문자열만으로 정해짐 | ✓ | ✓ (f 프레임 = 최근 서로 다른 f 페이지 → 자명하게 포함) |
| OPT (동점: 마지막 참조) | (다음 참조 위치, 마지막 참조 시각) — 둘 다 문자열만으로 | ✓ | ✓ |
| FIFO | 적재 시각 — 언제 미스가 났는지에 달림 = 크기에 달림 | ✗ | ✗ (위 표 t=6) |
| Clock | use bit 와 바늘 위치 — 둘 다 미스 이력에 달림 | ✗ | ✗ (Belady 문자열에서 FIFO 와 똑같이 9 → 10) |
| OPT (동점: 최근 적재, OSTEP 식) | 동점 부분이 적재 시각 | ✗ | ✗ — 미스 수는 같지만 포함 관계는 깨진다 (연습 3: 2000 문자열 중 792개) |

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">t=6 (참조 5 직후) 의 상주 집합: LRU 는 겹쳐 쌓이고, FIFO 는 어긋난다</text>
  <text x="20" y="52" fill="currentColor" font-weight="bold">LRU 스택 (최근 → 오래됨)</text>
  <rect x="20"  y="64" width="44" height="32" class="f-ok" stroke="currentColor"/><text x="36"  y="85" fill="currentColor">5</text>
  <rect x="64"  y="64" width="44" height="32" class="f-ok" stroke="currentColor"/><text x="80"  y="85" fill="currentColor">2</text>
  <rect x="108" y="64" width="44" height="32" class="f-ok" stroke="currentColor"/><text x="124" y="85" fill="currentColor">1</text>
  <rect x="152" y="64" width="44" height="32" class="f-mut" stroke="currentColor"/><text x="168" y="85" fill="currentColor">4</text>
  <rect x="196" y="64" width="44" height="32" class="f-mut" stroke="currentColor"/><text x="212" y="85" fill="currentColor">3</text>
  <path d="M20 106 L152 106" stroke="currentColor" stroke-width="2"/><text x="20" y="124" fill="currentColor">3 프레임 = 앞 3개</text>
  <path d="M20 134 L196 134" stroke="currentColor" stroke-width="2"/><text x="20" y="152" fill="currentColor">4 프레임 = 앞 4개 ⊇ 3 프레임</text>
  <text x="20" y="184" fill="currentColor">크기와 상관없이 같은 한 줄 순서에서 앞부분을 자른다</text>
  <text x="20" y="204" fill="currentColor">→ 늘 포함, 폴트 곡선 단조 감소</text>
  <text x="400" y="52" fill="currentColor" font-weight="bold">FIFO</text>
  <text x="400" y="84" fill="currentColor">3 프레임:</text>
  <rect x="480" y="64" width="44" height="32" class="f-warn" stroke="currentColor"/><text x="496" y="85" fill="currentColor">1</text>
  <rect x="524" y="64" width="44" height="32" class="f-acc" stroke="currentColor"/><text x="540" y="85" fill="currentColor">2</text>
  <rect x="568" y="64" width="44" height="32" class="f-acc" stroke="currentColor"/><text x="584" y="85" fill="currentColor">5</text>
  <text x="400" y="134" fill="currentColor">4 프레임:</text>
  <rect x="480" y="114" width="44" height="32" class="f-acc" stroke="currentColor"/><text x="496" y="135" fill="currentColor">2</text>
  <rect x="524" y="114" width="44" height="32" class="f-mut" stroke="currentColor"/><text x="540" y="135" fill="currentColor">3</text>
  <rect x="568" y="114" width="44" height="32" class="f-mut" stroke="currentColor"/><text x="584" y="135" fill="currentColor">4</text>
  <rect x="612" y="114" width="44" height="32" class="f-acc" stroke="currentColor"/><text x="628" y="135" fill="currentColor">5</text>
  <text x="400" y="184" fill="currentColor">1 은 3 프레임에만 있다 → t=7, 8 의 1, 2 가</text>
  <text x="400" y="204" fill="currentColor">3 프레임에서는 hit, 4 프레임에서는 miss</text>
</svg>
```

**무작위로 찾아보기 (실제 출력):**

```text
random 30-ref strings (7 pages): 5000 trials, FIFO Belady cases=14, LRU/OPT inclusion violations=0/0
```

- FIFO 의 Belady 사례는 **드물다**: 무작위 5000 문자열(페이지 7종, 길이 30, 프레임 1..7)에서 14개(0.28%). 실무에서 "FIFO 는 Belady 때문에 나쁘다" 기보다 "FIFO 는 hot 페이지를 차별하지 않아서 나쁘다" 가 더 정확하다 (Part C 의 80-20 곡선).
- 테스트는 **함의** 도 확인한다: Belady 가 나온 14개 문자열은 전부 `inclusion_ok(FIFO) == false`. 포함 관계 ⇒ 단조이므로 대우(contrapositive)로 당연하지만, 코드 두 개가 서로를 검증한다.
- LRU/OPT 는 5000 × 6 쌍 × 30 시각 전부에서 포함 관계 위반 0, 곡선 상승 0.

### Part C — Clock 은 LRU 를 얼마나 닮았나

**손 계산 예 (1 2 3 4 2 3 5, 프레임 3).** 표기: 페이지(use).

| t | 참조 | 프레임 0,1,2 | 바늘 | 일 |
|---|---|---|---|---|
| 0–2 | 1 2 3 | 1(1) 2(1) 3(1) | 0 | 강제 미스 |
| 3 | 4 | **4(1)** 2(0) 3(0) | 1 | 전부 1 → 한 바퀴 지우고 프레임 0 의 1 을 내보냄 (4칸). 1 은 LRU 와 같은 선택: rank 0 |
| 4 | 2 | 4(1) 2(1) 3(0) | 1 | hit |
| 5 | 3 | 4(1) 2(1) 3(1) | 1 | hit |
| 6 | 5 | 4(0) **5(1)** 3(0) | 2 | 또 전부 1 → 바늘 1 부터 한 바퀴, 2 를 내보냄 (4칸). LRU 라면 4 (t=3 이 가장 오래됨). 2 보다 오래된 4 가 있으니 **rank 1** |

Clock 이 틀리는 이유가 보인다: use bit 는 "마지막 바늘 통과 이후 참조됐나" 1비트뿐이라, 모두 1 이면 순서 정보가 없고 **바늘 위치** 가 결정한다. 테스트: `hist = {1, 1, 0}`, `hand_steps == 8`.

**반복 부하 (0..49 를 1만 번).** 프레임 < 50 이면 LRU·FIFO·Clock 모두 hit 0 — 내보낸 페이지가 정확히 "가장 곧 다시 쓰일" 페이지다(OSTEP 그림 22.8). Clock 은 hit 이 없으니 use bit 가 적재 순서와 똑같이 움직여 **매번 진짜 LRU 페이지를 고른다**(rank 0 100%). 바늘 비용: f 번 교체마다 한 번은 전부 1 인 상태를 만나 한 바퀴(f+1칸), 나머지 f−1 번은 바로 다음 칸(1칸) → 교체당 평균 **2칸**. 테스트는 `2·ev ≤ hand_steps ≤ 2·ev + f` 로 확인한다 (f=5: 9995 교체에 19990칸).

**히트율 곡선 (`./demo_curves csv` 의 실측값으로 그림):**

```svg
<svg viewBox="0 0 740 350" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">히트율 곡선 — 실측 (demo_curves), x = 프레임 수, y = 히트율 %</text>
  <rect x="45" y="50" width="200" height="200" fill="none" stroke="currentColor" stroke-width="0.8"/>
  <text x="45" y="42" fill="currentColor" font-weight="bold">지역성 없음 (균등 100p)</text>
  <line x1="45" y1="200" x2="245" y2="200" stroke="currentColor" stroke-width="0.3" stroke-dasharray="1 3"/>
  <line x1="45" y1="150" x2="245" y2="150" stroke="currentColor" stroke-width="0.3" stroke-dasharray="1 3"/>
  <line x1="45" y1="100" x2="245" y2="100" stroke="currentColor" stroke-width="0.3" stroke-dasharray="1 3"/>
  <text x="40" y="254" fill="currentColor" text-anchor="end">0</text>
  <text x="45" y="265" fill="currentColor" text-anchor="middle">0</text>
  <text x="40" y="154" fill="currentColor" text-anchor="end">50</text>
  <text x="145" y="265" fill="currentColor" text-anchor="middle">50</text>
  <text x="40" y="54" fill="currentColor" text-anchor="end">100</text>
  <text x="245" y="265" fill="currentColor" text-anchor="middle">100</text>
  <polyline points="47.0,248.0 51.0,243.8 55.0,239.8 59.0,235.4 63.0,231.7 67.0,227.2 71.0,223.0 75.0,219.1 79.0,215.0 83.0,210.6 87.0,206.1 91.0,202.0 95.0,198.0 99.0,194.5 103.0,190.0 107.0,186.7 111.0,182.0 115.0,178.0 119.0,174.0 123.0,170.2 127.0,166.2 131.0,162.6 135.0,158.9 139.0,155.3 143.0,151.4 145.0,149.5 147.0,147.7 151.0,144.6 155.0,139.6 159.0,135.3 163.0,132.3 167.0,126.3 171.0,122.0 175.0,119.4 179.0,115.2 183.0,112.0 187.0,107.8 191.0,103.6 195.0,99.7 199.0,96.5 203.0,92.5 207.0,87.7 211.0,85.5 215.0,81.1 219.0,77.2 223.0,72.9 227.0,68.3 231.0,65.6 235.0,61.2 239.0,57.9 243.0,53.6 245.0,52.0" fill="none" stroke="currentColor" stroke-width="1.2" stroke-dasharray="2 3"/>
  <polyline points="47.0,248.0 51.0,243.8 55.0,239.7 59.0,235.5 63.0,231.7 67.0,227.3 71.0,223.2 75.0,218.9 79.0,215.0 83.0,210.7 87.0,205.9 91.0,202.2 95.0,197.9 99.0,194.1 103.0,190.3 107.0,185.6 111.0,181.8 115.0,177.9 119.0,174.0 123.0,169.9 127.0,166.5 131.0,162.7 135.0,158.9 139.0,155.3 143.0,151.8 145.0,149.7 147.0,147.5 151.0,143.3 155.0,139.4 159.0,136.2 163.0,130.7 167.0,126.7 171.0,123.3 175.0,119.9 179.0,115.4 183.0,112.5 187.0,108.1 191.0,104.0 195.0,100.1 199.0,96.1 203.0,92.0 207.0,88.7 211.0,84.9 215.0,79.8 219.0,75.8 223.0,72.4 227.0,68.5 231.0,64.6 235.0,61.3 239.0,57.6 243.0,53.7 245.0,52.0" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 3"/>
  <polyline points="47.0,248.0 51.0,243.8 55.0,239.7 59.0,235.5 63.0,231.6 67.0,227.1 71.0,223.1 75.0,219.0 79.0,214.8 83.0,210.7 87.0,206.1 91.0,202.0 95.0,197.8 99.0,194.1 103.0,190.1 107.0,186.1 111.0,182.4 115.0,178.6 119.0,174.6 123.0,171.0 127.0,166.6 131.0,162.5 135.0,159.0 139.0,155.3 143.0,152.1 145.0,150.1 147.0,148.4 151.0,144.4 155.0,140.4 159.0,136.1 163.0,132.3 167.0,128.1 171.0,123.6 175.0,119.7 179.0,115.6 183.0,111.7 187.0,107.2 191.0,103.6 195.0,99.6 199.0,95.8 203.0,92.4 207.0,88.6 211.0,84.8 215.0,80.9 219.0,76.8 223.0,72.7 227.0,68.7 231.0,64.8 235.0,60.9 239.0,57.6 243.0,54.1 245.0,52.0" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <polyline points="47.0,248.0 51.0,220.6 55.0,205.0 59.0,193.1 63.0,183.3 67.0,174.8 71.0,167.4 75.0,160.7 79.0,154.6 83.0,148.8 87.0,143.5 91.0,138.5 95.0,133.9 99.0,129.5 103.0,125.3 107.0,121.4 111.0,117.7 115.0,114.2 119.0,110.9 123.0,107.6 127.0,104.4 131.0,101.4 135.0,98.5 139.0,95.8 143.0,93.3 145.0,92.0 147.0,90.7 151.0,88.2 155.0,85.8 159.0,83.5 163.0,81.4 167.0,79.2 171.0,77.3 175.0,75.4 179.0,73.5 183.0,71.7 187.0,70.0 191.0,68.3 195.0,66.7 199.0,65.2 203.0,63.7 207.0,62.3 211.0,60.9 215.0,59.6 219.0,58.3 223.0,57.2 227.0,56.1 231.0,55.0 235.0,54.0 239.0,53.2 243.0,52.4 245.0,52.0" fill="none" stroke="currentColor" stroke-width="2.5"/>
  <rect x="280" y="50" width="200" height="200" fill="none" stroke="currentColor" stroke-width="0.8"/>
  <text x="280" y="42" fill="currentColor" font-weight="bold">80-20 (hot 20p)</text>
  <line x1="280" y1="200" x2="480" y2="200" stroke="currentColor" stroke-width="0.3" stroke-dasharray="1 3"/>
  <line x1="280" y1="150" x2="480" y2="150" stroke="currentColor" stroke-width="0.3" stroke-dasharray="1 3"/>
  <line x1="280" y1="100" x2="480" y2="100" stroke="currentColor" stroke-width="0.3" stroke-dasharray="1 3"/>
  <text x="275" y="254" fill="currentColor" text-anchor="end">0</text>
  <text x="280" y="265" fill="currentColor" text-anchor="middle">0</text>
  <text x="275" y="154" fill="currentColor" text-anchor="end">50</text>
  <text x="380" y="265" fill="currentColor" text-anchor="middle">50</text>
  <text x="275" y="54" fill="currentColor" text-anchor="end">100</text>
  <text x="480" y="265" fill="currentColor" text-anchor="middle">100</text>
  <polyline points="282.0,243.9 286.0,230.5 290.0,218.8 294.0,206.6 298.0,196.2 302.0,185.4 306.0,175.5 310.0,165.1 314.0,156.3 318.0,149.9 322.0,142.2 326.0,135.1 330.0,129.6 334.0,124.3 338.0,120.1 342.0,116.2 346.0,111.9 350.0,108.4 354.0,105.0 358.0,102.2 362.0,99.5 366.0,96.8 370.0,94.6 374.0,92.6 378.0,90.1 380.0,89.3 382.0,88.3 386.0,86.2 390.0,83.8 394.0,81.6 398.0,79.9 402.0,78.0 406.0,76.5 410.0,74.5 414.0,73.3 418.0,71.6 422.0,70.3 426.0,69.4 430.0,67.7 434.0,66.8 438.0,65.3 442.0,62.8 446.0,62.2 450.0,60.9 454.0,59.8 458.0,58.5 462.0,57.1 466.0,55.7 470.0,54.6 474.0,53.9 478.0,52.8 480.0,52.0" fill="none" stroke="currentColor" stroke-width="1.2" stroke-dasharray="2 3"/>
  <polyline points="282.0,243.9 286.0,230.5 290.0,218.2 294.0,206.3 298.0,194.9 302.0,183.1 306.0,173.1 310.0,162.5 314.0,152.7 318.0,143.3 322.0,134.6 326.0,126.9 330.0,118.8 334.0,113.0 338.0,107.2 342.0,102.0 346.0,97.9 350.0,94.0 354.0,89.9 358.0,86.8 362.0,84.9 366.0,82.6 370.0,80.9 374.0,79.3 378.0,78.4 380.0,77.6 382.0,76.4 386.0,75.4 390.0,74.4 394.0,73.3 398.0,72.1 402.0,70.8 406.0,69.4 410.0,68.6 414.0,67.4 418.0,66.8 422.0,65.9 426.0,64.9 430.0,63.5 434.0,62.3 438.0,61.4 442.0,60.9 446.0,60.0 450.0,58.9 454.0,57.9 458.0,57.1 462.0,55.8 466.0,55.0 470.0,54.0 474.0,53.1 478.0,52.4 480.0,52.0" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 3"/>
  <polyline points="282.0,243.9 286.0,230.5 290.0,217.5 294.0,205.2 298.0,193.5 302.0,182.3 306.0,170.3 310.0,159.1 314.0,148.7 318.0,138.5 322.0,129.1 326.0,119.9 330.0,112.4 334.0,105.5 338.0,99.9 342.0,95.4 346.0,91.4 350.0,88.4 354.0,85.5 358.0,83.3 362.0,81.4 366.0,80.1 370.0,78.8 374.0,77.8 378.0,76.8 380.0,76.2 382.0,75.7 386.0,74.7 390.0,73.8 394.0,72.9 398.0,71.7 402.0,70.8 406.0,69.9 410.0,68.7 414.0,67.4 418.0,66.3 422.0,65.4 426.0,64.3 430.0,63.4 434.0,62.9 438.0,61.8 442.0,60.9 446.0,59.9 450.0,59.0 454.0,58.1 458.0,56.9 462.0,56.0 466.0,55.0 470.0,54.1 474.0,53.2 478.0,52.4 480.0,52.0" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <polyline points="282.0,243.9 286.0,195.3 290.0,169.6 294.0,150.7 298.0,135.7 302.0,123.6 306.0,113.6 310.0,105.3 314.0,98.1 318.0,92.1 322.0,87.2 326.0,83.3 330.0,80.2 334.0,77.9 338.0,75.9 342.0,74.0 346.0,72.4 350.0,71.0 354.0,69.7 358.0,68.5 362.0,67.4 366.0,66.4 370.0,65.5 374.0,64.6 378.0,63.8 380.0,63.4 382.0,63.0 386.0,62.2 390.0,61.5 394.0,60.8 398.0,60.1 402.0,59.5 406.0,58.8 410.0,58.2 414.0,57.6 418.0,57.1 422.0,56.6 426.0,56.1 430.0,55.7 434.0,55.3 438.0,54.9 442.0,54.5 446.0,54.1 450.0,53.8 454.0,53.5 458.0,53.2 462.0,52.9 466.0,52.6 470.0,52.4 474.0,52.2 478.0,52.1 480.0,52.0" fill="none" stroke="currentColor" stroke-width="2.5"/>
  <rect x="515" y="50" width="200" height="200" fill="none" stroke="currentColor" stroke-width="0.8"/>
  <text x="515" y="42" fill="currentColor" font-weight="bold">반복 0..49</text>
  <line x1="515" y1="200" x2="715" y2="200" stroke="currentColor" stroke-width="0.3" stroke-dasharray="1 3"/>
  <line x1="515" y1="150" x2="715" y2="150" stroke="currentColor" stroke-width="0.3" stroke-dasharray="1 3"/>
  <line x1="515" y1="100" x2="715" y2="100" stroke="currentColor" stroke-width="0.3" stroke-dasharray="1 3"/>
  <text x="510" y="254" fill="currentColor" text-anchor="end">0</text>
  <text x="515" y="265" fill="currentColor" text-anchor="middle">0</text>
  <text x="510" y="154" fill="currentColor" text-anchor="end">50</text>
  <text x="615" y="265" fill="currentColor" text-anchor="middle">50</text>
  <text x="510" y="54" fill="currentColor" text-anchor="end">100</text>
  <text x="715" y="265" fill="currentColor" text-anchor="middle">100</text>
  <polyline points="517.0,250.0 521.0,250.0 525.0,250.0 529.0,250.0 533.0,250.0 537.0,250.0 541.0,250.0 545.0,250.0 549.0,250.0 553.0,250.0 557.0,250.0 561.0,250.0 565.0,250.0 569.0,250.0 573.0,250.0 577.0,250.0 581.0,250.0 585.0,250.0 589.0,250.0 593.0,250.0 597.0,250.0 601.0,250.0 605.0,250.0 609.0,250.0 613.0,250.0 615.0,51.0 617.0,51.0 621.0,51.0 625.0,51.0 629.0,51.0 633.0,51.0 637.0,51.0 641.0,51.0 645.0,51.0 649.0,51.0 653.0,51.0 657.0,51.0 661.0,51.0 665.0,51.0 669.0,51.0 673.0,51.0 677.0,51.0 681.0,51.0 685.0,51.0 689.0,51.0 693.0,51.0 697.0,51.0 701.0,51.0 705.0,51.0 709.0,51.0 713.0,51.0 715.0,51.0" fill="none" stroke="currentColor" stroke-width="1.2" stroke-dasharray="2 3"/>
  <polyline points="517.0,250.0 521.0,250.0 525.0,250.0 529.0,250.0 533.0,250.0 537.0,250.0 541.0,250.0 545.0,250.0 549.0,250.0 553.0,250.0 557.0,250.0 561.0,250.0 565.0,250.0 569.0,250.0 573.0,250.0 577.0,250.0 581.0,250.0 585.0,250.0 589.0,250.0 593.0,250.0 597.0,250.0 601.0,250.0 605.0,250.0 609.0,250.0 613.0,250.0 615.0,51.0 617.0,51.0 621.0,51.0 625.0,51.0 629.0,51.0 633.0,51.0 637.0,51.0 641.0,51.0 645.0,51.0 649.0,51.0 653.0,51.0 657.0,51.0 661.0,51.0 665.0,51.0 669.0,51.0 673.0,51.0 677.0,51.0 681.0,51.0 685.0,51.0 689.0,51.0 693.0,51.0 697.0,51.0 701.0,51.0 705.0,51.0 709.0,51.0 713.0,51.0 715.0,51.0" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 3"/>
  <polyline points="517.0,250.0 521.0,250.0 525.0,250.0 529.0,250.0 533.0,250.0 537.0,250.0 541.0,250.0 545.0,250.0 549.0,250.0 553.0,250.0 557.0,250.0 561.0,250.0 565.0,250.0 569.0,250.0 573.0,250.0 577.0,250.0 581.0,250.0 585.0,250.0 589.0,250.0 593.0,250.0 597.0,250.0 601.0,250.0 605.0,250.0 609.0,250.0 613.0,250.0 615.0,51.0 617.0,51.0 621.0,51.0 625.0,51.0 629.0,51.0 633.0,51.0 637.0,51.0 641.0,51.0 645.0,51.0 649.0,51.0 653.0,51.0 657.0,51.0 661.0,51.0 665.0,51.0 669.0,51.0 673.0,51.0 677.0,51.0 681.0,51.0 685.0,51.0 689.0,51.0 693.0,51.0 697.0,51.0 701.0,51.0 705.0,51.0 709.0,51.0 713.0,51.0 715.0,51.0" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <polyline points="517.0,250.0 521.0,241.8 525.0,233.7 529.0,225.6 533.0,217.5 537.0,209.3 541.0,201.2 545.0,193.1 549.0,185.0 553.0,176.9 557.0,168.7 561.0,160.6 565.0,152.5 569.0,144.4 573.0,136.3 577.0,128.1 581.0,120.0 585.0,111.9 589.0,103.8 593.0,95.7 597.0,87.5 601.0,79.4 605.0,71.3 609.0,63.2 613.0,55.1 615.0,51.0 617.0,51.0 621.0,51.0 625.0,51.0 629.0,51.0 633.0,51.0 637.0,51.0 641.0,51.0 645.0,51.0 649.0,51.0 653.0,51.0 657.0,51.0 661.0,51.0 665.0,51.0 669.0,51.0 673.0,51.0 677.0,51.0 681.0,51.0 685.0,51.0 689.0,51.0 693.0,51.0 697.0,51.0 701.0,51.0 705.0,51.0 709.0,51.0 713.0,51.0 715.0,51.0" fill="none" stroke="currentColor" stroke-width="2.5"/>
  <line x1="60" y1="295" x2="96" y2="295" stroke="currentColor" stroke-width="2.5"/>
  <text x="102" y="299" fill="currentColor">OPT</text>
  <line x1="210" y1="295" x2="246" y2="295" stroke="currentColor" stroke-width="1.5"/>
  <text x="252" y="299" fill="currentColor">LRU</text>
  <line x1="360" y1="295" x2="396" y2="295" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 3"/>
  <text x="402" y="299" fill="currentColor">Clock</text>
  <line x1="510" y1="295" x2="546" y2="295" stroke="currentColor" stroke-width="1.2" stroke-dasharray="2 3"/>
  <text x="552" y="299" fill="currentColor">FIFO</text>
  <text x="10" y="322" fill="currentColor">반복 부하: 프레임 ≤ 49 이면 LRU·Clock·FIFO 모두 0% (바닥선), 50 이면 99.5%.</text>
  <text x="10" y="340" fill="currentColor">OPT 만 프레임에 비례해 오른다. 균등 부하: 셋 다 f% 에 겹침.</text>
</svg>
```

`./demo_curves` 표 (실제 출력, 히트율 %):

```text
80-20 — hit rate %
  frames    OPT    LRU  CLOCK   FIFO
      10   60.4   30.9   30.5   29.8
      20   80.2   58.1   55.6   52.2
      30   87.5   76.2   72.7   66.0
      40   91.0   83.8   82.0   74.7
      50   93.3   86.9   86.2   80.3
      70   96.6   92.1   91.7   89.4
      90   98.5   96.8   96.8   96.3
looping — hit rate %
      10   18.3    0.0    0.0    0.0
      40   79.2    0.0    0.0    0.0
      50   99.5   99.5   99.5   99.5
```

(균등 부하는 OPT 를 뺀 셋이 모두 f% 근처: f=50 에서 49.9 / 50.1 / 50.2. 참조가 서로 독립이고 균등이면 과거가 미래에 대해 아무것도 알려 주지 않으니 어떤 온라인 정책이든 hit 확률 = f/100 이다. 테스트는 ±3%p 로 확인.)

**Clock 근사 지표 (80-20, 실제 출력 — solution 의 `gap-closed` 와 demo_curves 의 순위 분포):**

| 프레임 | LRU−FIFO 격차 중 Clock 이 메운 비율 | 희생자 = LRU 페이지 | 희생자가 오래된 1/4 안 | 평균 rank / f | 교체당 바늘 칸 |
|---|---|---|---|---|---|
| 10 | 69% | 74.6% | 81.8% | 0.070 | 2.19 |
| 30 | 65% | 57.9% | 85.5% | 0.077 | 3.08 |
| 50 | 89% | 60.5% | 91.4% | 0.060 | 3.55 |
| 70 | 87% | 36.6% | 95.4% | 0.070 | 3.85 |
| 90 | 107% (Clock 이 LRU 보다 살짝 나음) | 20.5% | 96.5% | 0.059 | 6.60 |

읽는 법:

- **희생자가 LRU 와 정확히 같은 경우는 20~75% 뿐** 인데 히트율은 LRU 와 0~3.5%p 차이다. 평균 rank/f ≈ 0.06~0.08 — Clock 은 "가장 오래된 하나" 는 자주 놓치지만 거의 늘 **오래된 쪽 몇 %** 안에서 고른다. 교체 정책의 품질은 "진짜 hot 페이지를 안 버리는가" 이고, 그건 1비트로 충분히 구분된다.
- 바늘 비용은 프레임이 클수록, hit 이 많을수록(use bit 가 많이 켜져 있을수록) 커진다 (2.2 → 6.6칸). 실제 커널이 바늘 하나 대신 **주기적으로 비트를 훑는 백그라운드 스캔**(Linux 의 kswapd 가 active/inactive 리스트를 나이 들이는 것)을 쓰는 이유 중 하나다.
- 위 수치는 시드 7 의 한 작업 부하다. 테스트가 고정 시드에서 확인하는 것은 OPT ≥ 나머지(정리), FIFO ≤ Clock, LRU − Clock < 4%p 뿐이고, 나머지는 관찰이다.

**펌웨어로 옮기면.** SSD 의 DFTL 은 전체 L2P 매핑 테이블(4 KB 페이지당 4 B → 1 TB 에 1 GB)을 DRAM 이나 SRAM 에 다 못 올리는 컨트롤러에서 일부만 캐시(CMT, cached mapping table)한다(MEM-A09). 호스트 I/O 의 hot/cold 가 80-20 곡선이고, 캐시보다 넓은 순차 읽기가 반복 부하다 — LRU 를 그대로 쓰면 큰 순차 스캔 한 번이 hot 엔트리를 다 밀어낸다. 그리고 매핑 엔트리가 **dirty** 면 내보낼 때 NAND 에 번역 페이지를 써야 한다 → 연습 4 의 enhanced Clock. ARM 쪽 하드웨어 지원: Armv8.0 에서는 Access Flag 가 0 인 페이지를 건드리면 **Access Flag fault** 가 나서 커널이 소프트웨어로 비트를 켜고, Armv8.1 의 FEAT_HAFDBS 가 있으면 하드웨어가 직접 켠다(x86 의 PTE A/D 비트와 같은 역할). OS-B10 의 PTE accessed/dirty 비트가 바로 이 정보다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 32/32 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan   # 32/32, 경고 0
```

실행 결과 끝부분 (실제 출력):

```text
80-20 workload (100 pages, 10000 refs) hit rate %
  frames   OPT   LRU CLOCK  FIFO  gap-closed%  victim=LRU%  mean-rank/f
      10  60.4  30.9  30.5  29.8          69        74.6       0.070
      30  87.5  76.2  72.7  66.0          65        57.9       0.077
      50  93.3  86.9  86.2  80.3          89        60.5       0.060
      70  96.6  92.1  91.7  89.4          87        36.6       0.070
      90  98.5  96.8  96.8  96.3         107        20.5       0.059
32/32 passed
```

어느 폴더에서 실행해도 같다 (입력 파일 없음). `demo_*.c` 는 `#include "solution.c"` 를 하므로 레슨 폴더에서 컴파일한다(소스 파일 기준 상대 경로라 다른 폴더에서 `cc .../demo_curves.c` 해도 된다).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 것 |
|---|---|---|
| LRU 에서 hit 때 `last` 를 안 고침 | 적재 시각만 남으니 그냥 FIFO 다. OSTEP 예제에서 LRU 가 6 이 아니라 4 hits | `last[f] = t` 는 hit/miss 공통 경로에 |
| FIFO 에서 hit 때 `loaded` 를 고침 | 그건 LRU 다 | `loaded` 는 적재할 때만 |
| OPT 가 미스마다 미래 전체를 훑음 | 프레임마다 O(n) → 전체 O(n²·f). 1만 참조 × 100 프레임이면 느리다 | `next[]` 를 뒤에서 한 번에 만들고 `nextuse[f] = next[t]` |
| OPT 동점을 "최근 적재" 로 | 미스 수는 맞지만 포함 관계가 깨진다(연습 3) — 크기에 따라 달라지는 값을 우선순위에 썼다 | 동점은 마지막 참조 시각 같은 **문자열만으로 정해지는** 값으로 |
| Clock 바늘을 희생자 자리에 그대로 둠 | 다음 교체 때 방금 넣은(u=1) 페이지부터 봐서 지우게 된다 → 새 페이지가 두 번째 기회를 바로 잃음 | `hand = (victim + 1) % nframes` |
| Clock 에서 새 페이지 use = 0 | 그것도 하나의 변형이지만 "방금 참조됨" 을 버리는 것. 새 페이지가 다음 교체에서 바로 희생자가 되기 쉽다 | 이 레슨은 1 (OSTEP 22.8, 참조 시 use = 1) |
| 빈 프레임이 있는데 `pick_victim` 호출 | 강제 미스 구간에 남의 페이지를 내보냄 → 프레임이 있는데도 폴트 증가 | `used < nframes` 면 빈 프레임부터 |
| 히트율 비교에 강제 미스를 잊음 | 100 페이지 1만 참조면 어떤 정책도 최대 99.0% (반복 0..49 는 99.5%) | 상한을 알고 비교, 또는 "강제 미스 제외 히트율" 을 따로 |
| Belady 를 hits 로 검사 | hits 는 늘어야 정상이니 부등호가 반대 | `faults[f+1] > faults[f]` |

## 8. 면접 Q&A

<details>
<summary>Q1. Belady 이상이 뭔가요? 왜 LRU 에는 안 생기죠?</summary>

FIFO 에서 프레임을 늘렸는데 폴트가 늘어나는 현상. 1,2,3,4,1,2,5,1,2,3,4,5 에서 3 프레임 9 폴트, 4 프레임 10 폴트 (직접 돌려 확인). LRU 는 **스택 알고리즘** 이다: f 프레임의 내용은 "가장 최근에 쓰인 서로 다른 f 페이지" 라서 f+1 프레임의 내용에 항상 포함된다 → 작은 메모리의 hit 는 큰 메모리에서도 hit → 폴트가 늘 수 없다. 일반화: 우선순위가 메모리 크기와 무관하게 정해지면 스택 알고리즘(Mattson 1970). FIFO 의 우선순위인 적재 시각은 언제 미스가 났는지, 즉 메모리 크기에 달려 있다. 꼬리: Clock 도 Belady 가 생긴다 (같은 문자열에서 9 → 10, 무작위 탐색에서는 FIFO 보다 더 자주: 5000 중 51 vs 14).

</details>

<details>
<summary>Q2. 실제 OS 는 왜 정확한 LRU 를 안 쓰나요?</summary>

hit 가 압도적으로 많고 hit 는 **하드웨어(MMU)가 처리** 한다 — 커널은 hit 를 보지도 못한다. 정확한 LRU 는 모든 메모리 접근마다 리스트를 고쳐야 하므로 불가능하다. 하드웨어가 해 줄 수 있는 건 PTE 의 Accessed(참조) 비트를 켜 주는 것뿐이고, 커널은 그 비트를 주기적으로 읽고 지우며 근사한다(Clock 계열). Linux 는 active/inactive 두 리스트와 referenced 플래그로 근사하고, 6.1 부터는 세대(generation) 여러 개로 나이를 매기는 MGLRU 를 선택할 수 있다. 소프트웨어 캐시(DB 버퍼 풀, FTL 매핑 캐시)는 hit 를 직접 보므로 진짜 LRU(해시 + 이중 연결 리스트, DS-M05)도 가능하지만, hit 마다 리스트 조작과 락이 필요해서 동시성이 높으면 Clock 이 이득이다.

</details>

<details>
<summary>Q3. Clock 알고리즘을 설명하고, 최악의 경우 비용은?</summary>

프레임을 원형으로 놓고 바늘을 돌린다. 희생자가 필요하면 바늘 위치의 use bit 가 1 이면 0 으로 지우고 다음 칸(두 번째 기회), 0 이면 그 페이지를 내보낸다. 최악은 모든 bit 가 1 일 때: 한 바퀴 돌며 다 지우고 원래 칸을 고른다 — f+1 칸, 그리고 그 선택은 이력 정보 없이 바늘 위치로만 정해진다(FIFO 처럼 "순서" 만 본다). 실측: 반복 부하에서는 교체당 평균 2칸, 80-20 에서 프레임 90 이면 6.6칸. 메모리가 수 GB 면 한 바퀴가 수백만 PTE 라서 실제 커널은 바늘 하나가 아니라 백그라운드에서 나눠 훑는다(Linux kswapd 의 LRU 리스트 aging).

</details>

<details>
<summary>Q4. 쓸 수도 없는 OPT 를 왜 배우나요?</summary>

① **하한(lower bound)**: "LRU 가 80% 인데 좋은 건가?" 에 답하려면 최선이 몇 %인지 알아야 한다(80-20, f=30: OPT 87.5 vs LRU 76.2 → 개선 여지 11%p). ② 오프라인에서는 실제로 쓴다: 트레이스를 다 가진 캐시 시뮬레이션, 미래 사용이 다 보이는 컴파일러 레지스터 스필 결정 등. ③ 구현이 간단한 검증 기준이다. 이 레슨에서는 OPT 를 다시 "모든 교체를 다 해 보는 DP" 로 검증했다. 구현 요령: `next[]` 를 뒤에서 한 번에 만들면 미스당 O(f).

</details>

<details>
<summary>Q5. LRU 가 최악인 작업 부하는? 실제 시스템은 어떻게 막나요?</summary>

캐시보다 조금 큰 범위를 반복해서 훑는 부하(looping sequential). 0..49 반복을 49 프레임으로 돌리면 LRU 히트율 0% — 항상 "곧 다시 쓸" 페이지를 내보낸다(실측: f=10..49 전부 0.0%, OPT 는 f=40 에서 79.2%). 큰 파일 한 번 읽기(scan)가 hot 페이지를 몰아내는 것도 같은 문제. 대책: **스캔 저항(scan resistance)** — 한 번만 쓰인 페이지는 별도 리스트(Linux inactive list, 2Q 의 A1in, ARC 의 T1)에 두고 두 번째 참조 때 승격; 애플리케이션 힌트 `posix_fadvise(POSIX_FADV_SEQUENTIAL / DONTNEED)`, `madvise(MADV_SEQUENTIAL)`; 반복 부하만 알면 MRU 나 Random 이 오히려 낫다(OSTEP 22.6). 펌웨어 예: FTL 매핑 캐시에서 대용량 순차 읽기 구간은 캐시에 넣지 않거나 낮은 우선순위로.

</details>

<details>
<summary>Q6. dirty 페이지는 교체 결정에 어떻게 들어가나요?</summary>

깨끗한(clean) 페이지는 그냥 버리면 되지만 dirty 페이지는 디스크/플래시에 써야 내보낼 수 있다 — 미스 한 번에 쓰기 한 번이 붙는다. Enhanced second chance(OSTEP 22.9): (use, dirty) 로 네 등급을 만들고 (0,0) → (0,1) 순으로 찾는다. 단 공짜가 아니다: 연습 4 실측(80-20 + 쓰기 30%) 에서 f=10, 30 에서는 미스+쓰기 비용이 줄었지만(9591 → 9445, 4090 → 3671) **f=50 에서는 오히려 늘었다(1876 → 1977)** — 깨끗한 hot 페이지를 먼저 버렸기 때문. 실무에서는 dirty 페이지를 미리 써 두는 백그라운드 writeback(Linux flusher, SSD 의 맵 플러시)으로 "dirty 라서 못 버리는" 상황 자체를 줄인다. SSD 쪽: dirty 매핑 엔트리를 내보내면 번역 페이지 쓰기 → WAF 증가(MEM-M11).

</details>

<details>
<summary>Q7. O(1) LRU 캐시를 짜 보세요. 그리고 그게 OS 페이지 교체와 뭐가 다르죠?</summary>

해시맵(key → 노드) + 이중 연결 리스트(MRU ↔ LRU). get: 해시로 찾고 노드를 맨 앞으로. put: 꽉 찼으면 꼬리를 떼어 해시에서도 지우고, 새 노드를 맨 앞에 (DS-M05). 다른 점: LeetCode LRU 는 **모든 접근을 코드가 본다**. OS 페이지는 hit 를 MMU 가 처리해서 커널은 Accessed 비트로만 사후에 안다 → 정확한 LRU 불가, Clock 근사. 그리고 커널은 동시성(여러 CPU 가 같은 리스트)도 문제라 리스트 갱신을 per-CPU 배치(folio_batch, 예전 이름 pagevec)로 모은다.

</details>

<details>
<summary>Q8. 쓰래싱(thrashing)은 교체 정책으로 해결되나요?</summary>

아니다. 동시에 필요한 페이지 집합(working set)이 물리 메모리보다 크면 어떤 정책이든 미스가 폭증한다 — 반복 부하에서 f=49 와 f=50 의 차이(0% vs 99.5%)가 극단적인 예. 해결은 수요를 줄이는 것: 프로세스를 줄이거나(admission control), 일부를 통째로 스왑아웃하거나, Linux 처럼 OOM killer 로 하나를 죽인다(OSTEP 22.11). 임베디드 Linux 는 보통 스왑이 없어서 페이지 캐시(코드 페이지 포함)만 교체 대상이고, 메모리가 바닥나면 실행 파일 페이지를 계속 다시 읽는 형태의 쓰래싱이 일어난다 — 그래서 cgroup 메모리 제한과 PSI(pressure stall information) 감시를 쓴다.

</details>

## 9. 추가 연습

### 연습 1 — Clock 도 Belady 이상을 보인다

Belady 문자열로 Clock 의 폴트 곡선을 그리고, 무작위 5000 문자열(페이지 7종, 길이 30, 프레임 1..7)에서 FIFO 와 Clock 의 Belady 사례 수를 세라.

<details>
<summary>답 보기</summary>

Belady 문자열에서 Clock 곡선은 12, 12, 9, 10, 5, 5 — FIFO 곡선과 똑같고, 3 → 4 에서 늘어난다. 무작위 탐색 실제 출력: `ex1: Belady cases in 5000 random strings (7 pages, 30 refs): FIFO=14 CLOCK=51`. Clock 이 더 자주 이상을 보인다 — use bit 와 바늘 위치 둘 다 미스 이력(= 메모리 크기)에 달려 있기 때문. "LRU 근사" 라고 LRU 의 성질(스택 성질)까지 물려받지는 않는다.

</details>

### 연습 2 — Mattson 한 번 훑기: 모든 프레임 수를 한 번에

`lru_all_sizes(refs, n, maxf, hits)` — 참조 문자열을 **한 번만** 훑어 f = 1..maxf 모든 경우의 LRU hit 수를 구하라. 프레임마다 시뮬레이션한 결과와 같아야 한다 (80-20, 1만 참조, f = 1..100).

<details>
<summary>답 보기</summary>

LRU 스택(최근 → 오래됨 순서의 서로 다른 페이지 목록)을 유지한다. 참조할 때 그 페이지의 스택 위치 d(1부터)를 찾아 `dist[d]++` 하고 맨 앞으로 옮긴다(처음이면 거리 ∞). 스택 성질 덕분에 "f 프레임에서 hit ⇔ d ≤ f" 이므로 `hits[f] = dist[1] + … + dist[f]`. 이것이 OSTEP 22장 참고문헌 [M+70] 끝의 질문("why the stack property might be useful for simulating a lot of different-sized caches at once")의 답이다. 실제 출력: `ex2: one-pass LRU hits f=10/30/50: 3086 7624 8692 (same as per-size simulation)` — 100개 크기 모두 일치, `hits[100] = 10000 − 100`(강제 미스만). FIFO·Clock 은 스택 알고리즘이 아니라서 이 방법이 안 된다. 캐시 크기 결정(CPU 캐시 설계, 서버 메모리 산정)에 실제로 쓰이는 기법이다.

</details>

### 연습 3 — OPT 의 동점 규칙과 포함 관계

OSTEP `paging-policy.py` 의 OPT 동점 규칙(가장 최근에 적재된 페이지)을 구현해, 본문 OPT(동점: 마지막 참조가 오래된 것)와 무작위 2000 문자열에서 비교하라: 미스 수, 포함 관계 위반 수.

<details>
<summary>답 보기</summary>

실제 출력: `ex3: OPT tie rule — miss diffs=0, inclusion violations mine=0 ostep-rule=2743 (792/2000 strings)`. 미스 수는 **항상 같다** — 동점은 "둘 다 다시는 안 쓰임" 일 때만 생기고, 그런 페이지는 누굴 버려도 미래에 영향이 없다. 그런데 OSTEP 규칙은 2000 문자열 중 792개에서 포함 관계가 깨진다: 적재 시각은 메모리 크기에 따라 다르므로 f 프레임과 f+1 프레임이 서로 다른 "죽은" 페이지를 남긴다. 폴트 곡선은 그래도 단조인데(OPT 는 어떤 크기에서든 최소이고, 최솟값은 크기에 대해 단조 — 큰 메모리는 작은 메모리의 전략을 흉내 낼 수 있으니까), 포함 관계는 그보다 강한 성질이라는 것을 보여 주는 예다. 교훈: 스택 성질을 쓰고 싶으면(연습 2 같은 한 번 훑기) 동점까지 크기와 무관하게 정하라.

</details>

### 연습 4 — dirty bit 를 보는 Clock (enhanced second chance)

참조마다 쓰기 여부가 붙는다. 쓰인 페이지는 dirty 이고, dirty 페이지를 내보내면 writeback 1회. ① (use 0, dirty 0) 을 한 바퀴 찾고 ② 없으면 (use 0, dirty 1) 을 찾으며 지나가는 use 를 지우고 ③ 그래도 없으면 ① 부터 다시. 손 계산 예 W1 R2 R3 R4 R1(프레임 3)과 80-20 + 쓰기 30% 부하에서 Clock 과 비교하라.

<details>
<summary>답 보기</summary>

손 계산: t=3 에서 모두 u=1. Clock 은 한 바퀴 지우고 dirty 인 1 을 내보낸다(writeback 1) → t=4 의 R1 은 미스. enhanced 는 ① 실패 → ② 에서 1(1,1)→(0,1), 2(1,0)→(0,0), 3→(0,0) 으로 지우며 한 바퀴 → ① 다시: 2 가 (0,0) → 깨끗한 2 를 내보냄 → R1 hit. Clock 5 미스·1 writeback, enhanced 4 미스·0 writeback.

80-20 실제 출력:

```text
ex4: 80-20 + 30% writes      misses  writebacks  cost(miss+wb)
     f=10  CLOCK            6956        2635           9591
     f=10  enhanced CLOCK   6909        2536           9445
     f=30  CLOCK            2707        1383           4090
     f=30  enhanced CLOCK   2555        1116           3671
     f=50  CLOCK            1322         554           1876
     f=50  enhanced CLOCK   1401         576           1977
```

f=30 에서 비용 10% 감소, 그러나 f=50 에서는 미스도 writeback 도 늘었다 — 깨끗하지만 hot 한 페이지를 dirty 한 cold 페이지보다 먼저 버렸다. 그리고 ③ 의 반복 때문에 최악 스캔이 Clock 보다 길다(전부 dirty 면 ①②①② 로 네 바퀴 가까이). 펌웨어 판단: writeback 비용이 미스보다 훨씬 크면(NAND read 수십~100 µs 대 program 수백 µs 이상, 그리고 마모) enhanced 가 이득이고, 비슷하면 그냥 Clock + 백그라운드 플러시가 낫다. 이 레슨의 비용 모델(미스 1 + writeback 1)은 단순화다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 13/13 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 13/13, 경고 0
```

## 10. 핵심 정리

- 교체 정책 = "꽉 찼을 때 누굴 내보내나". FIFO(적재 시각) · LRU(마지막 참조) · OPT(다음 참조가 가장 먼 것, 하한) · Clock(use bit 1개 + 바늘, LRU 근사).
- OSTEP 예제 0 1 2 0 1 3 0 3 1 2 1 / 3 프레임: FIFO 4, LRU 6, OPT 6, Clock 4 hits. 내 시뮬레이터는 `paging-policy.py` 와 95회 참조 단위로 일치(OPT 동점 51건은 둘 다 다시 안 쓰이는 페이지).
- Belady: FIFO 는 1,2,3,4,1,2,5,1,2,3,4,5 에서 3 프레임 9 → 4 프레임 10 폴트. Clock 도 같은 이상. LRU·OPT 는 **스택 알고리즘**(우선순위가 메모리 크기와 무관 → f ⊆ f+1) 이라 불가능.
- 포함 관계 ⇒ 폴트 단조, 역은 아님 (OSTEP 식 OPT 동점: 미스는 최소인데 포함 관계는 깨짐). 스택 성질이 있으면 한 번 훑어 모든 크기의 hit 를 구한다(Mattson).
- Clock: 희생자가 LRU 와 정확히 같은 경우는 20~75% 지만 거의 늘 오래된 쪽 몇 % 안 → 80-20 에서 LRU 와 0~3.5%p 차이. 모든 bit 가 1 이면 한 바퀴(f+1칸) 돌고 바늘 자리를 고른다(정보 없음, FIFO 처럼).
- 부하가 정책보다 중요하다: 지역성 없으면 전부 f%, 반복 부하에서 LRU·FIFO·Clock 은 프레임 < 루프면 0%. 대책은 스캔 저항(2Q/ARC/inactive list) 과 힌트(fadvise/madvise).
- 실제 커널은 hit 를 못 본다(MMU 가 처리) → Accessed/Dirty 비트 기반 근사. 펌웨어(FTL 매핑 캐시, 버퍼 풀)는 hit 를 보지만 dirty 비용(NAND 쓰기)까지 넣어 정한다.

## 참고자료

- OSTEP 22장 *Beyond Physical Memory: Policies* — 22.2 OPT(그림 22.1), 22.3 FIFO·Belady, 22.5 LRU, 22.6 세 작업 부하(그림 22.6~22.8), 22.8 Clock(그림 22.9), 22.9 dirty 페이지, 22.11 thrashing: <https://pages.cs.wisc.edu/~remzi/OSTEP/vm-beyondphys-policy.pdf> (로컬: `operating-system-three-easy-pieces/book-md/C22_beyond_physical_memory_policies.md`, Don 노트 `notes/2026-09-30_C22_swapping_policies.md`)
- OSTEP 21장 *Beyond Physical Memory: Mechanisms* — page fault 처리, swap: <https://pages.cs.wisc.edu/~remzi/OSTEP/vm-beyondphys.pdf>
- OSTEP 숙제 시뮬레이터 `vm-beyondphys-policy/paging-policy.py` (로컬 `.tools/ostep-homework/`, 읽기 전용으로 `python3 -I` 실행): <https://github.com/remzi-arpacidusseau/ostep-homework/tree/master/vm-beyondphys-policy>
- L. A. Belady, *A Study of Replacement Algorithms for a Virtual-Storage Computer*, IBM Systems Journal 5(2), 1966 — MIN(OPT)
- L. A. Belady, R. A. Nelson, G. S. Shedler, *An Anomaly in Space-Time Characteristics of Certain Programs Running in a Paging Machine*, CACM 12(6), 1969
- R. L. Mattson, J. Gecsei, D. R. Slutz, I. L. Traiger, *Evaluation Techniques for Storage Hierarchies*, IBM Systems Journal 9(2), 1970 — 스택 알고리즘, 한 번 훑기
- F. J. Corbató, *A Paging Experiment with the Multics System*, 1969 — Clock
- N. Megiddo, D. Modha, *ARC: A Self-Tuning, Low Overhead Replacement Cache*, FAST 2003 — 스캔 저항
- A. Gupta, Y. Kim, B. Urgaonkar, *DFTL: A Flash Translation Layer Employing Demand-based Selective Caching of Page-level Address Mappings*, ASPLOS 2009
- Linux kernel docs — Multi-Gen LRU: <https://docs.kernel.org/admin-guide/mm/multigen_lru.html>, Mel Gorman, *Understanding the Linux Virtual Memory Manager* 10장 Page Frame Reclamation (active/inactive 리스트): <https://www.kernel.org/doc/gorman/html/understand/>
- Arm Architecture Reference Manual (Armv8-A) — Access Flag, FEAT_HAFDBS (hardware Access flag / dirty state 관리)
- 관련 레슨: OS-B10 (페이지 테이블·PTE accessed/dirty), OS-M01 (다단계 테이블·TLB), OS-M03 (COW·페이지 폴트), DS-M05 (O(1) LRU 캐시), ARCH-M05 (하드웨어 캐시 교체·pseudo-LRU), MEM-M03 (다단계 캐시 시뮬레이터), MEM-A09 (DFTL 매핑 캐시), MEM-M11 (GC·WAF), STOR-B04 (버퍼 풀 LRU/Clock), LPERF-B08 (page cache 지표)
