---
id: DS-A09
time: 120분
updated: 2026-11-08
---
# DS-A09 FTL 매핑 테이블과 GC 대상 선정 구조

## 학습 목표

- 페이지 매핑 FTL의 자료구조 다섯 개(**L2P 배열**, **P2L/OOB**, **블록별 valid 비트맵 + count**, **free 블록 풀**, **indexed heap**)가 서로 어떤 불변식으로 묶이는지 표로 말하고, 그 전부를 대조하는 검사기를 쓴다.
- greedy(valid 최소)와 cost-benefit(`(1-u)·age/(1+u)`) victim 키를 정의하고, **cost-benefit 키는 시간에 따라 순서가 뒤집혀서 정적 힙에 그냥 넣으면 틀린다**는 것을 두 직선의 교차로 보인 뒤, 얼린 시계(frozen clock) + Floyd 재빌드로 해결한다.
- GC 복사 중에 host가 같은 LPN을 덮어쓰는 경합을 **조건부 커밋**으로 처리하고, 예약 블록과 교착 방지 조건을 비둘기집 원리로 증명한다.
- GC 시작/정지 **워터마크**와 쓰기당 페이싱을 구현하고, 최악 쓰기 지연을 벽시계 대신 "host 쓰기 1회당 복사 페이지 수"로 잰다.
- WAF를 OP·정책·핫/콜드 편향으로 측정해 표로 만들고, **greedy ≈ cost-benefit인 경우와 아닌 경우**를 측정값으로 말한다. 해석식(Desnoyers 2012)과 대조한다.

## 1. 문제

"SSD FTL의 매핑 테이블과 GC victim 선정을 자료구조로 설계해 보세요"는 SSD 펌웨어 면접의 기본 질문이고, 시니어라면 "valid count 힙은 왜 그렇게 짰고, cost-benefit 키는 어떻게 유지하며, 측정한 WAF는 얼마인가"까지 답해야 한다. 임베디드 쪽에서도 NOR/NAND 위의 로그 구조 저장(see:DS-A10, STOR-B05)이 전부 같은 구조다. FTL 과정 자체(L2P 생명주기, 웨어 레벨링, 전원 손실)는 MEM-M10/M11/M12/M14가, 전체 설계는 CAP-M05가 맡는다. 이 레슨은 **자료구조**만 다룬다.

NAND의 규칙은 세 줄이다. 페이지 단위로 쓰고, 블록 단위로 지우며, 지우기 전에는 다시 쓸 수 없다. 그래서 덮어쓰기는 "새 페이지에 쓰고 옛 페이지를 invalid로 표시"가 되고, 결국 어떤 블록을 골라 살아 있는 페이지를 옮긴 뒤 지워야 한다(GC). **어느 블록을 고르느냐**가 쓰기 증폭(WAF, Write Amplification Factor)을 결정한다.

```svg
<svg viewBox="0 0 740 440" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a9a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">자료구조 지도 — 화살표는 서로 맞아야 하는 대응(불변식)</text>
  <text x="10" y="50" fill="currentColor">L2P[lpn] → ppa</text>
  <g class="f-mut" stroke="currentColor">
    <rect x="10" y="60" width="40" height="26"/><rect x="50" y="60" width="40" height="26"/><rect x="90" y="60" width="40" height="26"/><rect x="130" y="60" width="40" height="26"/><rect x="170" y="60" width="40" height="26"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="30" y="78">2</text><text x="70" y="78">9</text><text x="110" y="78">0</text><text x="150" y="78">NONE</text><text x="190" y="78">4</text>
  </g>
  <text x="10" y="104" fill="currentColor" font-size="12">lpn 0..4 (그림은 PG=6, RAM: 페이지당 4 B)</text>
  <text x="290" y="50" fill="currentColor" font-weight="bold">NAND 블록 (ppa = 블록 × PG + 페이지)</text>
  <text x="290" y="80" fill="currentColor">b0</text><text x="290" y="170" fill="currentColor">b1</text><text x="290" y="260" fill="currentColor">b2</text>
  <g stroke="currentColor">
    <rect x="320" y="62" width="30" height="24" class="f-ok"/><rect x="350" y="62" width="30" height="24" class="f-warn"/><rect x="380" y="62" width="30" height="24" class="f-ok"/><rect x="410" y="62" width="30" height="24" class="f-warn"/><rect x="440" y="62" width="30" height="24" class="f-ok"/><rect x="470" y="62" width="30" height="24" class="f-mut"/>
    <rect x="320" y="152" width="30" height="24" class="f-ok"/><rect x="350" y="152" width="30" height="24" class="f-ok"/><rect x="380" y="152" width="30" height="24" class="f-warn"/><rect x="410" y="152" width="30" height="24" class="f-ok"/><rect x="440" y="152" width="30" height="24" class="f-ok"/><rect x="470" y="152" width="30" height="24" class="f-ok"/>
    <rect x="320" y="242" width="30" height="24" class="f-mut"/><rect x="350" y="242" width="30" height="24" class="f-mut"/><rect x="380" y="242" width="30" height="24" class="f-mut"/><rect x="410" y="242" width="30" height="24" class="f-mut"/><rect x="440" y="242" width="30" height="24" class="f-mut"/><rect x="470" y="242" width="30" height="24" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="510" y="79">P2L = 2 7 0 7 4 -</text><text x="510" y="169">P2L = 5 9 3 1 6 8</text><text x="510" y="259">erased (free 풀)</text>
    <text x="320" y="104">bitmap 10101 · vcnt 3 · 닫힘(CLOSED)</text><text x="320" y="194">bitmap 110111 · vcnt 5 · 닫힘</text>
    <text x="320" y="284">bitmap 0 · vcnt 0 · FREE</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M30 60 C 40 25 370 25 395 60" marker-end="url(#a9a)"/>
    <path d="M70 86 C 120 150 340 130 425 152" marker-end="url(#a9a)" stroke-dasharray="4 3"/>
  </g>
  <rect x="10" y="130" width="220" height="86" fill="none" stroke="currentColor"/>
  <text x="20" y="150" fill="currentColor" font-weight="bold">프런티어 (쓰는 중인 블록)</text>
  <text x="20" y="172" fill="currentColor" font-size="12">host_blk: host 스트림</text>
  <text x="20" y="190" fill="currentColor" font-size="12">gc_blk: GC 스트림 (2-stream 일 때)</text>
  <text x="20" y="208" fill="currentColor" font-size="12">victim: GC 진행 중인 블록</text>
  <rect x="10" y="232" width="220" height="80" fill="none" stroke="currentColor"/>
  <text x="20" y="252" fill="currentColor" font-weight="bold">free 풀 (FIFO 링)</text>
  <text x="20" y="274" fill="currentColor" font-size="12">host 는 free_n &gt; GC_RESERVE 일 때만</text>
  <text x="20" y="292" fill="currentColor" font-size="12">GC 는 free_n &gt; 0 이면 꺼낼 수 있다</text>
  <rect x="10" y="328" width="710" height="104" fill="none" stroke="currentColor"/>
  <text x="20" y="348" fill="currentColor" font-weight="bold">indexed min-heap (닫힌 블록 전부) — DS-M11 의 pos[] 맵</text>
  <text x="20" y="370" fill="currentColor" font-family="monospace" font-size="12">greedy     key = (vcnt &lt;&lt; 48) | close_seq        // 무효화마다 값이 줄어든다 → sift-up</text>
  <text x="20" y="388" fill="currentColor" font-family="monospace" font-size="12">cost-ben.  key = UINT64_MAX - (age+1)(PG-v)·1024/(PG+v)   // age 는 "얼린 시계" 기준</text>
  <text x="20" y="406" fill="currentColor" font-family="monospace" font-size="12">공통       vcnt 가 바뀌면 heap_refresh(b): 키 재계산 후 위/아래로 sift</text>
  <text x="20" y="424" fill="currentColor" font-size="12">검사: pos[heap[i]] == i, 힙 순서, key[b] == gc_key(b), 힙의 블록 == CLOSED 인 블록</text>
  <text x="290" y="316" fill="currentColor" font-size="12">초록 = valid, 경고색 = invalid(덮어씀), 회색 = 안 쓴 페이지</text>
</svg>
```

**이 모델의 단순화.** 페이지 = 매핑 단위이고(실제로는 16 KB NAND 페이지에 4 KB 매핑 여러 개), 블록 = GC 단위다(실제로는 다이 여러 개를 묶은 슈퍼블록). 프로그램 실패, 읽기 교란, ECC는 없다. 쓰기 경로에 NAND 지연도 없다. 이 레슨의 숫자는 **자료구조와 정책의 WAF**이고, 메타데이터 쓰기(저널, 맵 플러시)가 만드는 WAF는 빠져 있다(Q7).

### Part A — 매핑과 비트맵, free 풀, host 경로

- `valid_set/valid_clear`: 비트맵과 `vcnt`를 같이 고친다. 닫힌 블록이면 힙 키도 갱신. 이미 켜진 비트를 또 켜면 위반으로 센다.
- `bm_next_set(b, from)`: from 이상의 첫 valid 페이지. 64비트 워드 경계(페이지 63/64)를 넘는다.
- `free_push/free_pop`: FIFO 링. `frontier_ready`: host는 예약 아래로 못 내려간다. `close_frontier`: 닫을 때 `close_seq`, `stamp`를 찍고 힙에 넣는다.
- `host_program(lpn, val)`, `ftl_read`, `ftl_trim`.

### Part B — victim 키, indexed heap, 재배치

- `cb_score(v, age)` = `(age+1)·(PG-v)·1024/(PG+v)` (나눗셈은 마지막). `gc_key(b)`: greedy / FIFO / cost-benefit.
- `heap_rebuild()`: `epoch_clk = host_clk`로 시계를 얼리고 모든 키를 다시 계산한 뒤 Floyd로 heapify.
- `gc_pick()`: epoch이 다 되었으면 재빌드, 루트를 victim으로. `v == PG`이면(FIFO 제외) 고르지 않는다.
- `gc_relocate(b, p)`: 한 페이지 복사. `gc_step()`: victim 선택 → 복사 하나 → 다 끝나면 erase.

### Part C — 워터마크, 페이싱, 경합, WAF

- `gc_tick()`: free ≤ `wm_start`이면 GC를 켜고, free ≥ `wm_stop`이면 끈다. 켜져 있으면 host 쓰기 한 번마다 `ceil(v/(PG-v))` 페이지까지만 복사한다.
- `ftl_write()`: tick → 프런티어를 못 열면 하드 플로어(hard floor)까지 GC를 강제로 돌림 → host 쓰기.
- `gc_relocate`의 조건부 커밋: 프로그램과 L2P 갱신 사이에 host가 같은 LPN을 덮어쓰면 복사본은 죽은 페이지가 된다.
- 측정: WAF vs OP, 정책, 편향. 답은 코드가 아니라 표다(§5).

## 2. Problem (English)

> **Page-mapped FTL with GC victim selection.**
>
> Model a NAND device of `nblk` blocks with `PG` pages per block. Pages are written in order within a block; a block must be erased before reuse. The host writes logical pages `[0, nlpn)`; `nlpn` is strictly less than the usable physical capacity.
>
> Maintain a page-level L2P map, a per-block valid bitmap and valid count, a P2L record (the OOB-equivalent) for every programmed page, a free-block pool, and an indexed min-heap of closed blocks keyed for GC. Use static arrays only.
>
> 1. `write(lpn, data)`, `read(lpn)`, `trim(lpn)`, with the host never consuming the GC reserve.
> 2. Pick the GC victim: (a) greedy (fewest valid pages), (b) cost-benefit, `score = (1-u) * age / (1+u)`, where `u` is the valid fraction and `age` is the host-write clock since the block closed. Explain why the cost-benefit key is not a stable heap key, and implement a scheme that keeps the heap valid (re-key on an epoch).
> 3. Relocation must be safe against a host overwrite that races with the copy: the L2P is updated only if it still points at the page being copied.
> 4. GC trigger: low/high free-block watermarks with hysteresis, plus an incremental pacing of copies per host write. Prove GC cannot deadlock given a one-block reserve.
> 5. Verify with a full invariant checker (L2P ↔ P2L ↔ bitmap ↔ count ↔ heap ↔ free list) and a data-integrity reference. Measure WAF vs over-provisioning for greedy and cost-benefit under uniform and hot/cold (e.g. 90/10) traffic, in steady state.
>
> Example: blocks hold 10, 128, 50 valid pages (PG = 128). Greedy picks the first (key `10 << 48 | seq`); relocation copies 10 pages and erases it (`gc_wr = 10`, `erases = 1`, one block back in the free pool).

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과(실제 출력, exit 1):

```text
== Part A: mapping, bitmap, free pool
PASS !ftl_init(MAX_BLK + 1u, 10u, POL_GREEDY)
PASS !ftl_init(16u, (16u - GC_RESERVE - 1u) * PG_PER_BLK, POL_GREEDY)
PASS ftl_init(16u, 1000u, POL_GREEDY)
PASS !ftl_read(7u, &v)
FAIL starter.c:497  do_write(7u, false) && ftl_read(7u, &v) && v == g_ref[7]  []
FAIL starter.c:499  p0 == ppa_make(0, 0) && F.vcnt[0] == 1u && valid_test(p0) && F.p2l[p0] == 7u  []
SKIP rest of Part A: host write stub
SKIP Part B/C: Part A failed (stubs would make the workload meaningless)
4/6 passed
```

TODO 18곳 (Part A 10, Part B 6, Part C 2). NAND 층(`nand_program`/`nand_erase`), 힙 기계 부분(`heap_swap`, `sift_up/down`, `heap_insert/remove/refresh`, DS-M11 복습), `ftl_init`, `ftl_write_fg`, 검사기 `ftl_check`, 하네스는 주어진다.

하네스 설계:
- 각 단계는 앞 단계가 통과해야 돈다. 스텁 상태에서는 Part A 앞부분까지만 돌고 무한 루프 없이 끝난다.
- 작은 구성(24블록)은 **매 연산 후** `ftl_check()`를 돌리고, 큰 구성(256블록, 110만 연산)은 8192 연산마다 + 끝에서 전체 검사와 데이터 대조를 한다.
- `g_copy_window`는 DS-A04의 `POP_WINDOW`와 같은 기법이다. 프로그램과 L2P 커밋 사이에 테스트가 host 쓰기를 끼워 넣어, 경합을 확률이 아니라 **결정적 스케줄**로 만든다.
- 모든 값은 정수 연산이라 결과가 결정적이다. WAF는 CHECK에서 **대소 관계**(OP가 크면 줄어든다, 90/10에서 CB < greedy)만 본다.

## 4. 힌트

<details>
<summary>힌트 1 — host_program의 순서</summary>

`old = l2p[lpn]`을 **먼저** 읽고, 프로그램 → L2P 갱신 → `valid_set(new)` → `valid_clear(old)` 순서로 간다. L2P를 먼저 고치고 옛 값을 읽으면 무효화할 ppa를 잃는다. 프런티어가 꽉 차면 마지막에 닫아서 힙에 넣는다.

</details>

<details>
<summary>힌트 2 — valid_clear에서 힙을 잊지 말 것</summary>

닫힌 블록(B_CLOSED)만 힙에 있다. 그 블록의 valid가 줄면 키가 바뀌므로 `heap_refresh(b)`를 불러야 한다. 프런티어나 victim 블록은 힙에 없으니 건드리면 안 된다. 힙 키가 어긋나면 `ftl_check`의 "heap key is stale"이 잡는다.

</details>

<details>
<summary>힌트 3 — 왜 cost-benefit 키가 힙에서 문제인가</summary>

`score(b, T) = (T - t_b + 1) · c_b`이고 `c_b = (PG-v)/(PG+v)`는 블록마다 다르다. 즉 블록마다 기울기가 다른 직선이다. 두 직선은 언젠가 만나므로 **순서가 시간에 따라 뒤집힌다**. 그래서 `gc_key`에서 지금 시계(`host_clk`)를 쓰면 힙 안의 키들이 서로 다른 시각에 계산된 값이 되어 힙 순서가 의미를 잃는다. 시계를 얼려(`epoch_clk`) 그 시각으로만 계산하고, 주기적으로 전부 다시 계산한다.

</details>

<details>
<summary>힌트 4 — 조건부 커밋이 지키는 것</summary>

GC가 페이지를 복사하는 동안 host가 같은 LPN을 새로 쓰면, L2P는 이미 더 새로운 위치를 가리킨다. 이때 GC가 그냥 L2P를 덮어쓰면 **새 데이터가 옛 데이터로 되돌아간다**(가장 흔하고 가장 나쁜 FTL 버그다). `l2p[lpn] == old`일 때만 커밋한다. 아니면 새 페이지는 처음부터 무효다.

</details>

<details>
<summary>힌트 5 — 페이싱 할당량</summary>

victim이 valid `v`개, invalid `PG-v`개를 가지면 host가 `PG-v`페이지를 쓰는 동안 GC는 `v`페이지를 옮겨야 블록 하나가 정확히 돌아온다. 그러니 host 쓰기 1회당 `ceil(v/(PG-v))` 페이지를 옮기면 free 블록 수가 줄지 않는다. 지나치면 GC가 앞서고, 모자라면 하드 플로어에 부딪혀 지연 스파이크가 난다.

</details>

<details>
<summary>힌트 6 — v == 0 블록과 v == PG 블록</summary>

v == 0이면 복사 없이 erase만 하면 되는 공짜 블록이다. 점수가 얼마든 가장 먼저 고른다(`key = 0`). v == PG면 아무것도 얻지 못하므로 고르지 않는다(FIFO 제외). `gc_key`의 `age + 1` 덕분에 방금 닫힌 블록(age 0)이라도 `v < PG`이면 점수가 0보다 크다. CB_SCALE ≥ 2·PG이어야 정수 나눗셈이 0이 되지 않는다.

</details>

## 5. 풀이

### Part A — 불변식 표

| # | 불변식 | 깨지면 생기는 일 |
|---|---|---|
| 1 | `l2p[lpn] = ppa ≠ NONE` ⇒ `ppa`의 블록이 FREE가 아니고, `p < wp`, `p2l[ppa] == lpn`, 비트 켜짐 | 지워진 블록을 읽는다 |
| 2 | 비트 켜짐 ⇒ `l2p[p2l[ppa]] == ppa` | GC가 죽은 페이지를 살린다 |
| 3 | `vcnt[b] == popcount(bitmap[b])`, 비트는 `wp` 미만에만 | victim 선택이 틀린다 |
| 4 | `Σ vcnt == #{lpn: l2p[lpn] ≠ NONE}` | 페이지 유실/이중 계산 |
| 5 | free 링의 블록은 전부 B_FREE이고 중복 없음, `wp = vcnt = 0` | 블록 이중 할당 |
| 6 | 힙 = CLOSED 블록 집합, `pos`와 `heap`은 역함수, `key[b] == gc_key(b)`, 힙 순서 | 엉뚱한 victim |

`p2l`은 **불변이다**. 페이지와 함께 프로그램되고 erase 전까지 바뀌지 않는다(실제 NAND의 OOB가 그렇다). 그래서 덮어쓴 페이지의 `p2l`은 stale로 남고, "이 페이지가 살아 있나?"는 항상 `l2p[p2l[ppa]] == ppa`로 재확인할 수 있다(연습 4).

**write 경로의 순서**: 프로그램이 끝난 뒤에 L2P를 고친다. 실제 FTL도 프로그램 완료 후 매핑을 갱신한다(그 전까지 읽기는 쓰기 버퍼에서 서비스한다). 이 모델은 프로그램이 즉시 끝나므로 Part C의 복사 창(`g_copy_window`)이 그 시간차를 흉내 낸다.

**free 풀을 FIFO로 둔 이유**: 먼저 지워진 블록을 먼저 쓰면 erase 횟수가 돌아가며 고르게 쌓인다. 웨어 레벨링 정책 자체는 MEM-M11이 맡고, 이 레슨은 그 구조를 넣을 자리만 잡는다.

### Part B — victim 키와 indexed heap

키는 전부 "**작을수록 먼저 고른다**"로 맞춘다. min-heap 하나로 세 정책을 쓸 수 있다.

| 정책 | 키 | 언제 바뀌나 | 비고 |
|---|---|---|---|
| greedy | `(vcnt << 48) \| close_seq` | 무효화마다 줄어든다 | 동점은 먼저 닫힌 순 |
| FIFO | `close_seq` | 안 바뀐다 | 분석용 기준선 (Desnoyers의 LRU) |
| cost-benefit | `UINT64_MAX - score` | 무효화 + 재빌드 | `v==0`이면 `0` |

`score = (age + 1)·(PG - v)·1024 / (PG + v)`. 나눗셈은 맨 마지막(먼저 나누면 `(PG-v)/(PG+v)`가 정수 0). LFS의 `(1-u)·age/(1+u)`와 같다. `u = v/PG`를 대입하면 `(PG-v)/(PG+v)`이고 `(1-u)/(1+u)`가 된다. age는 **host가 쓴 페이지 수**로 잰다(벽시계가 아니어서 유휴 시간이 흘러도 나이를 먹지 않는다).

**재배치 한 번의 추적**: 3블록(b0: v=10, b1: v=128, b2: v=50)에서 greedy `gc_pick` → `b0`. 복사 10회 + erase 1회 = 11 스텝. 검사기 통과 후 `gc_wr = 10`, `erases = 1`, b0는 FREE, GC 프런티어에 valid 10개.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a9b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 페이지 복사 (gc_relocate) — 창이 열린 동안 host 가 끼어들 수 있다</text>
  <text x="10" y="50" fill="currentColor">victim b</text>
  <g stroke="currentColor">
    <rect x="90" y="34" width="44" height="28" class="f-ok"/><rect x="134" y="34" width="44" height="28" class="f-warn"/><rect x="178" y="34" width="44" height="28" class="f-acc"/><rect x="222" y="34" width="44" height="28" class="f-ok"/>
  </g>
  <text x="112" y="52" fill="currentColor" text-anchor="middle" font-size="12">p0</text><text x="156" y="52" fill="currentColor" text-anchor="middle" font-size="12">p1</text><text x="200" y="52" fill="currentColor" text-anchor="middle" font-size="12">p2(old)</text><text x="244" y="52" fill="currentColor" text-anchor="middle" font-size="12">p3</text>
  <text x="10" y="120" fill="currentColor">GC 프런티어</text>
  <g stroke="currentColor">
    <rect x="130" y="104" width="44" height="28" class="f-ok"/><rect x="174" y="104" width="44" height="28" class="f-acc"/><rect x="218" y="104" width="44" height="28" class="f-mut"/>
  </g>
  <text x="196" y="122" fill="currentColor" text-anchor="middle" font-size="12">new</text>
  <path d="M200 62 L196 102" stroke="currentColor" fill="none" stroke-width="2" marker-end="url(#a9b)"/>
  <text x="208" y="86" fill="currentColor" font-size="12">① nand_program(new, lpn)</text>
  <rect x="410" y="34" width="320" height="100" fill="none" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="420" y="54" fill="currentColor" font-weight="bold">② 복사 창 (NAND 프로그램 소요 시간)</text>
  <text x="420" y="76" fill="currentColor">host 가 같은 lpn 을 덮어쓰면</text>
  <text x="420" y="96" fill="currentColor" font-family="monospace" font-size="12">l2p[lpn] = 더 새로운 ppa</text>
  <text x="420" y="116" fill="currentColor">→ old 의 valid 비트도 이미 꺼짐</text>
  <rect x="10" y="154" width="720" height="80" fill="none" stroke="currentColor"/>
  <text x="20" y="176" fill="currentColor" font-weight="bold">③ 커밋: if (l2p[lpn] == old)</text>
  <text x="20" y="198" fill="currentColor" font-family="monospace" font-size="12">  l2p[lpn] = new;  valid_set(new);  valid_clear(old);      // 평상시</text>
  <text x="20" y="218" fill="currentColor" font-family="monospace" font-size="12">else dead_copies++;                                       // new 는 처음부터 invalid, L2P 는 host 의 것</text>
  <text x="10" y="260" fill="currentColor" font-weight="bold">victim 의 모든 페이지가 끝나면</text>
  <text x="10" y="282" fill="currentColor" font-family="monospace" font-size="12">nand_erase(b)  →  state = B_FREE, free_push(b), victim = NONE</text>
  <text x="10" y="306" fill="currentColor" font-size="12">victim 은 힙에 없다. 진행 중 host 가 victim 의 페이지를 무효화해도 bm_next_set 이 건너뛴다 (Part C 첫 테스트).</text>
</svg>
```

복잡도와 결정성:

| 연산 | 비용 |
|---|---|
| `valid_clear` (hot path) | 힙 refresh: 최악 O(log nblk) swap. 실측 평균은 0.1 swap 안팎(아래 T3b) |
| `gc_pick` | 힙 루트 O(1) + 재빌드 시 O(nblk) |
| `gc_step` | 페이지 1개 복사 + 비트맵 스캔 O(BM_WORDS) |
| 전체 | 페이지당 4 B L2P + 4 B P2L, 블록당 O(PG/64) 비트맵 + count/heap/pos/key |

**cost-benefit 키가 왜 문제인가.** 정해진 시각 T에서 블록 b의 점수는 기울기 `c_b = (PG-v_b)/(PG+v_b)`, 절편이 stamp로 정해지는 직선이다. 기울기가 다른 두 직선은 한 번 교차한다. 아래 그림은 테스트에 들어 있는 실제 예다(A: v=32, stamp=1000; B: v=100, stamp=0).

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">cost-benefit 점수는 시간의 함수 — 힙 키로 그냥 쓰면 순서가 뒤집힌다</text>
  <line x1="60" y1="280" x2="680" y2="280" stroke="currentColor"/>
  <line x1="60" y1="280" x2="60" y2="40" stroke="currentColor"/>
  <text x="680" y="298" fill="currentColor" text-anchor="end">T (host 쓰기 시계)</text>
  <text x="62" y="36" fill="currentColor">score (CB_SCALE 없이)</text>
  <line x1="60" y1="270" x2="660" y2="60" stroke="currentColor" stroke-width="2"/>
  <line x1="60" y1="227" x2="660" y2="184" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3"/>
  <text x="530" y="90" fill="currentColor">A: v=32, stamp=1000 (기울기 0.60)</text>
  <text x="440" y="178" fill="currentColor">B: v=100, stamp=0 (기울기 0.123)</text>
  <line x1="120" y1="280" x2="120" y2="200" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="124" y="256" fill="currentColor" font-size="12">T=1100</text>
  <text x="124" y="240" fill="currentColor" font-size="12">B 가 위 (135 &gt; 61)</text>
  <line x1="600" y1="280" x2="600" y2="72" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="515" y="245" fill="currentColor" font-size="12">T=2000: A 가 위 (601 &gt; 246)</text>
  <circle cx="213" cy="216" r="5" class="f-warn" stroke="currentColor"/>
  <text x="222" y="212" fill="currentColor" font-size="12">교차 T ≈ 1256</text>
  <text x="60" y="320" fill="currentColor">해결: 키를 "지금 시계"가 아니라 얼린 시계 epoch_clk 로 계산 → 키끼리 비교가 항상 같은 시각의 비교다.</text>
</svg>
```

**얼린 시계(frozen clock).** 모든 키를 같은 `epoch_clk`로 계산한다. 그러면 힙 안 비교는 항상 "그 순간의 정확한 비교"이고, 힙 불변식은 시계가 흘러도 깨지지 않는다(`ftl_check`의 `key[b] == gc_key(b)`). 대가는 두 가지다. ① 고를 때 쓰는 시각이 조금 낡았다(몇 번의 pick 전의 시각). ② 가끔 전체를 재빌드한다(`heap_rebuild`, Floyd O(n)). `epoch_len = R` picks마다 재빌드하므로 R=1이면 매 pick이 정확하다(대신 O(n)). 아래 T3가 R의 효과다. 그 사이에 닫힌 블록은 `stamp > epoch_clk`이라 age가 0으로 잘려(점수 최소) 다음 재빌드까지 잘 안 뽑힌다. 이것이 부작용이자 작은 이득이다.

다른 설계도 있다. **v가 같으면 기울기가 같다**. 블록을 valid 수별 버킷으로 나누면 버킷 안에서는 시간에 따라 순서가 안 바뀐다(가장 오래된 것이 항상 최고점). 그러면 각 버킷 머리만 보는 O(PG) 정확 선택이 나온다(연습 1, 2. Desnoyers 2012가 greedy에 쓴 Np+1 bins와 같은 구조다).

### Part C — 워터마크, 페이싱, 교착 방지

`gc_active`는 `free ≤ wm_start`에서 켜지고 `free ≥ wm_stop`에서 꺼진다(히스테리시스). 그 사이에는 host 쓰기당 정해진 할당량만큼만 복사한다.

**교착 방지 증명.** GC가 멈출 수 있는 두 경우를 막는다.
- *복사할 공간이 없다*: host는 `free_n > GC_RESERVE`일 때만 블록을 가져간다. 그래서 victim을 고를 때 free ≥ 1이 남는다(`GC_RESERVE ≥ 1`이면 충분). 한 victim에는 많아야 `v ≤ PG`개 페이지가 있어 새 블록 하나로 복사가 끝나고, 곧이어 victim이 지워져 free가 복구된다. 진짜 시스템은 진행 중 복사(dead copy)가 추가로 소비될 수 있어 여유를 둔다(여기서는 `GC_RESERVE = 2`).
- *비울 게 없다*: 모든 닫힌 블록이 v == PG면 GC는 순이득이 0이다. `ftl_init`이 `nlpn < (nblk - GC_RESERVE - 1) · PG`를 요구하므로, 닫힌 블록 전체 용량이 사용자 페이지 수보다 크다. 비둘기집 원리로 **어떤 닫힌 블록에는 invalid 페이지가 반드시 있다**. 테스트 5가 여유 1페이지의 가장 빠듯한 구성(14블록)에서 멈추지 않음을 확인한다.

실제 출력(`./solution`의 관찰 줄):

```text
[B] greedy   small: 8000 ops, host 7055 gc 29721 erase 277
[B] cb       small: 8000 ops, host 7046 gc 32900 erase 302
[B] fifo     small: 8000 ops, host 7029 gc 35965 erase 326
[B] greedy   large 100/100 1100000 ops: WAF 2.644, erases 21902, dead 0, heap swaps/invalidate 0.10
[B] cb       large 90/10 1100000 ops: WAF 1.519, erases 12482, dead 0, heap swaps/invalidate 0.08
[C] watermarks: 446 activations in 200000 host writes, min free 3
[C] foreground: max GC copies per host write = 122, WAF 1.600
[C] paced    : max GC copies per host write = 1, WAF 1.634
[C] tightest legal config (14 blocks, spare 1 page(s) beyond reserve): 13407 host, WAF 10.74
```

`min free 3`은 여기서 측정한 값이다. 페이싱은 전경 대비 복사 폭주를 122 → 1로 줄이지만 WAF는 2% 올랐다(1.600 → 1.634. GC를 일찍 시작해 블록이 덜 무효화된 시점에 옮기기 때문이다).

### 측정 — WAF 표 (demo_waf.c)

조건: 1024 블록 × 128 페이지, 순차 채우기 + 40 DW 프리컨디셔닝 후 40 DW 측정, `WAF = (host + gc) / host`. 숫자는 3개 시드의 평균이고, 표 전체에서 시드별 min–max 폭은 최대 0.015였다. OP = `(T-U)/U`.

```bash
cc -std=c11 -O2 demo_waf.c -lm -o dw && ./dw t1   # (t1, t2, t3, t4 — 전부 정수 시뮬레이션, 이 맥에서 표마다 수 초~2 분)
```

**T1 — uniform (정책 비교 + 해석식)**

| OP | FIFO | greedy | CB R=1 | CB R=32 | Eq.4 LRU | Eq.12 greedy | Eq.4 (T-3) | Eq.12 (T-3) | Agarwal |
|---|---|---|---|---|---|---|---|---|---|
| 7% | 8.099 | 7.647 | 7.748 | 7.752 | 7.817 | 7.386 | 8.152 | 7.683 | 7.643 |
| 15% | 4.082 | 3.967 | 3.995 | 3.995 | 4.016 | 3.904 | 4.092 | 3.976 | 3.833 |
| 28% | 2.503 | 2.462 | 2.473 | 2.473 | 2.481 | 2.441 | 2.505 | 2.464 | 2.286 |
| 50% | 1.724 | 1.706 | 1.711 | 1.711 | 1.716 | 1.698 | 1.724 | 1.706 | 1.500 |

해석식의 출처는 Desnoyers 2012다. α = T/U일 때 Eq.4는 `A = α / (α + W(-α e^{-α}))`(LRU/FIFO), Eq.11–12는 `X0 = 1/2 - (Np/α) W(-(1+1/(2Np)) α e^{-(1+1/(2Np))α})`, `A = Np/(Np - (X0-1))`(greedy)이다. Agarwal & Marrow 2010의 `A = (1+ρ)/(2ρ)`는 이 표현을 정리한 Xiang & Kurkoski 2012의 인용으로 확인했다(원문은 직접 읽지 못했다). `T-3` 열은 예약 2블록 + 쓰는 중인 프런티어 몫을 뺀 근사다. Desnoyers도 free-list 크기가 Sf가 작을 때 결과를 눈에 띄게 바꾼다고 적었다. 측정값은 `T-3` 보정 시 FIFO에서 0.7% 이내, greedy에서 0.5% 이내로 맞는다. Agarwal 식은 OP가 커지면 낮게 나온다(50%: 1.5 vs 측정 1.706. Xiang & Kurkoski도 ρ=0.3에서 이전 식 2.17, 실제 2.35를 보고한다).

**T1 — 편향 쓰기 (평균 WAF, `r/s` = r%의 쓰기가 주소 공간의 s%로)**

| 워크로드 | OP | FIFO | greedy | CB R=1 | CB R=32 | greedy 대비 CB 감소 |
|---|---|---|---|---|---|---|
| 80/20 | 7% | 8.467 | 7.795 | 6.925 | 6.922 | 11% |
| 80/20 | 15% | 4.436 | 4.186 | 3.607 | 3.596 | 14% |
| 80/20 | 28% | 2.840 | 2.673 | 2.301 | 2.294 | 14% |
| 80/20 | 50% | 2.036 | 1.896 | 1.659 | 1.658 | 12% |
| 90/10 | 7% | 9.158 | 7.903 | 4.847 | 4.825 | 39% |
| 90/10 | 15% | 5.025 | 4.396 | 2.650 | 2.602 | 40% |
| 90/10 | 28% | 3.317 | 2.859 | 1.795 | 1.772 | 37% |
| 90/10 | 50% | 2.392 | 2.028 | 1.388 | 1.393 | 32% |
| 95/5 | 7% | 10.262 | 7.968 | 3.093 | 3.001 | 61% |
| 95/5 | 15% | 5.802 | 4.494 | 1.883 | 1.820 | 58% |
| 95/5 | 28% | 3.812 | 2.941 | 1.370 | 1.379 | 53% |
| 95/5 | 50% | 2.670 | 2.081 | 1.254 | 1.260 | 40% |

(마지막 열은 CB R=1 기준. 표의 숫자에서 계산했다.)

**T2 — 왜 CB가 이기나 (스트림 분리, age 정의; seed 1009, CB는 R=1)**

| 구성 | 80/20 7% | 90/10 7% | 80/20 28% | 90/10 28% |
|---|---|---|---|---|
| greedy 2-stream | 7.793 | 7.907 | 2.672 | 2.856 |
| greedy 1-stream | 7.951 | 8.654 | 2.799 | 3.286 |
| CB AGE_CLOSE 2-stream | 6.921 | 4.842 | 2.302 | 1.794 |
| CB AGE_CARRY 2-stream | 7.204 | 4.974 | 2.374 | 1.839 |
| CB AGE_LAST_INV 2-stream | 7.428 | 5.038 | 2.386 | 1.818 |
| CB AGE_CLOSE 1-stream | 8.043 | 8.705 | 2.802 | 3.274 |

**측정으로 말할 수 있는 것**:

- **균등 쓰기에서는 greedy ≈ cost-benefit이고 greedy가 약간 낫다** (7% OP에서 7.647 vs 7.748, 1.3% 차이. OP가 커지면 차이가 줄어든다). age는 hot/cold 정보가 없는 트래픽에서 아무것도 알려 주지 못한다. FIFO는 7% OP에서 greedy보다 6% 나쁘다.
- **편향이 있으면 CB가 크게 이긴다** (80/20에서 11–14%, 90/10에서 32–40%, 95/5에서 40–61%). greedy는 hot 블록을 너무 일찍 고른다. 곧 더 무효화될 블록을 지금 복사하는 셈이다.
- **단, 이 이득은 host/GC 스트림 분리가 있을 때만 나왔다**. 1-stream에서는 CB와 greedy가 같은 수준이었다(90/10 7%: 8.705 vs 8.654, 90/10 28%: 3.274 vs 3.286, 80/20 7%: 8.043 vs 7.951, 차이 ±1.2% 안쪽). GC로 살아남은 차가운 데이터가 host의 뜨거운 데이터와 한 블록에 섞이면 age가 가리키는 대상이 사라진다. 측정만 놓고 보면 **age 정책과 스트림 분리는 한 세트**다.
- age 정의 셋을 비교했을 때 이 시뮬레이션에서는 **닫힌 시각 기준(AGE_CLOSE)**이 네 칸 모두 가장 좋았고, 복사 원본의 시각을 물려받는 AGE_CARRY와 AGE_LAST_INV는 1–7% 나빴다. 가장 단순한 정의가 이겼으니 이를 기본으로 썼다. 다른 워크로드에서도 그럴지는 측정하지 않았다.

**T3 — 얼린 시계의 주기 R (key evals/pick은 전체 실행 기준, 재빌드 + 무효화 경로 합)**

| R | 90/10 7% WAF | key evals/pick | miss % vs 현재 시계 | 90/10 28% WAF | 80/20 7% WAF | uniform 7% WAF |
|---|---|---|---|---|---|---|
| 1 | 4.842 | 1048.3 | 0.00 | 1.794 | 6.921 | 7.751 |
| 4 | 4.890 | 282.3 | 0.00 | 1.809 | 6.933 | 7.754 |
| 16 | 4.868 | 91.0 | 0.00 | 1.790 | 6.929 | 7.751 |
| 32 | 4.825 | 59.3 | 2.85 | 1.770 | 6.917 | 7.753 |
| 64 | 4.733 | 43.9 | 34.75 | 1.754 | 6.914 | 7.753 |
| 256 | 4.593 | 32.7 | 76.21 | 1.791 | 6.878 | 7.769 |
| 1024 | 7.170 | 19.7 | 90.87 | 3.161 | 8.101 | 7.885 |
| 4096 | 5.791 | 23.2 | 81.13 | 1.968 | 7.660 | 7.703 |

"miss"는 얼린 시계의 선택이 현재 시계의 정확한 선택과 다른 비율이다(이 demo가 pick마다 O(n)으로 계산해 비교). **miss가 많다고 WAF가 나빠지지는 않았다.** R ≤ 32에서 WAF는 R=1과 ±1.3% 안쪽이었고, R=64–256에서는 90/10 7%처럼 오히려 더 낮은 칸도 있었다(그 사이 닫힌 블록은 age 0으로 눌려 뜨거운 블록이 더 늦게 뽑히는 효과로 보이지만, 이 원인은 실험으로 분리하지 않았다). R이 블록 수(1024)에 가까워지면 90/10 7%에서 4.842 → 7.170으로 망가졌다. 재빌드 간격을 블록 수보다 충분히 작게 두면(이 구성의 R=32) WAF 손실 없이(R=1 대비 세 편향 구성에서 −0.06 ~ −1.3%, 균등은 +0.03%) pick당 키 계산이 1048 → 59로 약 18배 줄었다. R=4096이 R=1024보다 나은 칸이 있는 것은 "측정한 값이 단조롭지 않다"는 뜻일 뿐, 그 원인을 짚지는 못했다.

**T3b — 무효화 경로의 힙 작업량** (블록 1024개, OP 7%, seed 1009)

| 정책 | 워크로드 | swap/무효화 | 한 refresh의 최대 swap | 무효화/host 쓰기 |
|---|---|---|---|---|
| greedy | uniform | 0.127 | 9 | 7.547 |
| greedy | 90/10 | 0.131 | 9 | 7.772 |
| CB R=32 | uniform | 0.105 | 8 | 7.652 |
| CB R=32 | 90/10 | 0.112 | 9 | 4.755 |

평균은 무효화당 0.1 swap 수준이고(대부분 v가 1 줄어도 힙 순서가 안 변한다), 최악은 `log2(1024) ≈ 10` 안쪽이다. 버킷 구조는 이 경로를 정확히 O(1)로 만든다(연습 1). 어느 쪽을 택할지는 pick 비용과 코드 복잡도의 문제다(Q3).

**T4 — 워터마크와 WAF** (greedy, uniform, OP 28%, 페이싱 GC)

| wm_start / wm_stop | WAF | 평균 free | 쓰기당 최대 복사 |
|---|---|---|---|
| 4 / 6 | 2.486 | 4.5 | 2 |
| 8 / 16 | 2.545 | 11.5 | 2 |
| 32 / 64 | 2.898 | 45.7 | 3 |
| 64 / 128 | 3.822 | 98.3 | 4 |
| 128 / 256 | 22.761 | 206.4 | 42 |
| (전경만) | 2.462 | 2.0 | 234 |

free 풀은 공짜가 아니다. free 블록은 invalid 페이지를 모으지 않는 OP이므로, 평균 free 206 블록(플래시의 20%)을 들고 있으면 유효 OP가 거의 사라지고 WAF가 22.8까지 튄다. 반대로 전경 GC만 쓰면 WAF는 최소(2.462)지만 한 host 쓰기가 최대 234 페이지 복사를 기다린다(victim이 연달아 잡히면 PG보다 많아진다). 워터마크는 **WAF와 지연의 트레이드오프**이고, 실제 파라미터는 지연 목표로 정한다.

**실제 SSD 규모 메모리 예산.** 1 TiB, 4 KiB 매핑이면 `2^40 / 2^12 = 2^28`개 L2P 엔트리, 4 B씩이면 1 GiB DRAM(용량의 약 0.1%)이다. valid 비트맵은 페이지당 1비트라 32 MiB. 이 모델의 전체 구조체는 256블록 × 128페이지일 때 `sizeof(ftl_t) = 409024` 바이트였다(l2p 131072 + p2l + data + bitmap 4096 + heap/pos/key 4096 등, 실측 sizeof). L2P 테이블을 DRAM에 전부 못 두는 경우의 매핑 캐시는 MEM-M10의 범위다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution             # 137/137 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o s_asan && ./s_asan   # 보고 없음, 137/137
```

검증 결과: 일반 빌드는 0.4 초, ASan+UBSan 빌드는 0.9 초에 끝나고 보고 없이 `137/137 passed`였다(새니타이저 빌드도 반복 수를 줄이지 않았다). 고의로 넣은 버그 14개(§9 끝 참고)를 전부 잡았다.

## 7. 자주 하는 실수

1. **GC가 L2P를 무조건 덮어쓴다.** 복사하는 동안 host가 같은 LPN을 쓰면 새 데이터가 옛 데이터로 되돌아간다. → `if (l2p[lpn] == old)`일 때만 커밋. 조건 없는 커밋 변형은 테스트 2에서 FAIL했다.
2. **cost-benefit 키를 `host_clk`로 계산한다.** 힙 안의 키들이 서로 다른 시각의 값이 되어 순서가 의미 없어진다. → `epoch_clk`로만 계산하고 주기적으로 재빌드. 이 변형은 "key[2]가 안 바뀐다"와 "heap key is stale"로 잡힌다.
3. **valid_clear에서 힙을 안 고친다.** 가장 valid가 적은 블록을 놓친다. → 닫힌 블록이면 `heap_refresh`.
4. **`vcnt`와 비트맵을 따로 관리한다.** 어느 경로에서 한쪽만 고쳐 어긋나면 GC가 이상해진다. → 한 함수(`valid_set/clear`)에서만 고치고, 검사기가 popcount와 대조.
5. **비트맵을 64비트 워드 하나로만 가정한다.** PG > 64이면 페이지 63/64 경계에서 틀린다. 시프트량이 64가 되면 UB다. → 워드별 스캔, `from % 64`, ctz는 0이 아닐 때만.
6. **host가 free 블록을 마지막까지 쓴다.** GC가 옮길 공간이 없어 교착한다. → 예약 블록, `frontier_ready`에서 host만 차단.
7. **"OP = 예비 / 전체"로 계산해 표를 잘못 읽는다.** 이 레슨과 업계 관행의 OP는 `(T-U)/U`다(7%면 U = T/1.07). Desnoyers의 `Sf`는 `(T-U)/T`라 값이 다르다(Sf 0.07 ≈ OP 7.5%). 실측과 식을 비교할 때 단위를 맞춘다.
8. **WAF를 프리컨디셔닝 없이 측정한다.** 빈 드라이브에서는 GC가 거의 돌지 않아 WAF ≈ 1이고 아무 의미가 없다. 편향 쓰기는 차가운 영역이 몇 번 돌아야 정상 상태가 되니, DW 수를 충분히 잡는다(여기서 40 DW + 40 DW).

## 8. 면접 Q&A

<details><summary>Q1. L2P 테이블을 배열로 둘 때 메모리는 얼마이고, 왜 4 B 엔트리면 되나요?</summary>

1 TiB에 4 KiB 매핑이면 `2^28` 엔트리다. 4 B 엔트리면 1 GiB, 즉 용량의 0.1% 정도이고 이게 "DRAM 1 GB per TB" 경험칙의 근거다. 4 B로 표현 가능한 페이지는 `2^32`개, 4 KiB 페이지로 16 TiB까지다. 더 크면 5 B 패킹이나 다른 매핑 단위가 필요하다. DRAM이 없거나 작은 경우는 전체 맵을 NAND에 두고 캐시한다(DFTL 계열, MEM-M10). 그때의 쟁점은 캐시 미스가 NAND 읽기를 부른다는 점이고, 맵 페이지도 FTL이 다시 관리해야 한다.
</details>

<details><summary>Q2. P2L은 왜 필요합니까? L2P만 있으면 GC가 못 합니까?</summary>

GC는 "이 블록의 p번 페이지에 어떤 LPN이 있나"를 알아야 그 페이지를 옮기고 L2P를 고칠 수 있다. L2P는 정방향(LPN → PPA)뿐이라 역방향 질의에 전체 탐색이 든다. 그래서 페이지와 함께 LPN을 적는다(OOB, 또는 블록 끝의 P2L 페이지). 비트맵은 없어도 된다. `valid(ppa) ⇔ l2p[p2l[ppa]] == ppa`로 알 수 있다(연습 4). 비트맵의 값은 그 조회를 O(1) 비트 연산으로 끝내 주고 P2L 읽기(NAND 또는 DRAM)를 아낀다는 데 있다. 전원 손실 뒤에는 P2L과 시퀀스 번호로 L2P를 복원하고(연습 3, MEM-M14), 비트맵은 L2P에서 재계산할 수 있으니 굳이 저장하지 않을 수 있다.
</details>

<details><summary>Q3. valid count별 버킷 리스트와 힙 중 어느 쪽을 쓰겠습니까?</summary>

greedy만이면 버킷이 낫다. Desnoyers 2012의 시뮬레이터도 `Np + 1` 버킷을 쓴다(무효화 시 인접 버킷으로 이동 O(1), pick은 최악 O(Np), 보통 O(1)). v가 0..PG 범위의 정수라서 힙의 비교 기반 O(log n)이 낭비다. 이 레슨의 측정에서 힙은 무효화당 평균 0.1 swap이라 실제 비용은 크지 않았지만, 최악 경로의 메모리 접근 수가 예측하기 쉬운 쪽은 버킷이다. 정책을 바꿔 가며 실험할 때나, 키가 v 외의 값(점수, 나이)을 포함할 때는 힙이 일반적이다. 인덱스 힙 하나로 세 정책을 표현한 이유가 그것이다. 최악 시간을 제한해야 하는 펌웨어라면 O(PG) pick은 곱셈 없는 짧은 루프로 끝나니 버킷 + 스캔이 유리하다(연습 2).
</details>

<details><summary>Q4. cost-benefit 키는 시간에 따라 변한다는데 힙을 어떻게 유지합니까?</summary>

우선 정확히 말하면, `score(b, T) = (T - t_b + 1)·c_b`는 블록마다 기울기가 다른 직선이라 순서가 뒤집힌다. 그래서 고정 키를 넣을 수 없다. 방법은 세 가지다. ① 재빌드 주기 R로 시계를 얼린다(이 레슨. 비교가 항상 같은 시각의 비교가 되어 힙 불변식이 유지된다, R을 블록 수보다 충분히 작게 둔다). ② v가 같으면 기울기가 같으니 v별 버킷으로 나누고 각 버킷의 가장 오래된 것만 본다(연습 2, O(PG) 정확). ③ 매번 선형 스캔한다(블록 수가 수천 개 이하이고 GC 빈도가 낮으면 충분히 저렴하다). 측정 결과 R=32에서는 R=1 대비 WAF 손실이 없고 키 계산이 18배 줄었다. 흔히 말하는 "kinetic heap"은 교차 시각까지 키를 유지하는 일반 구조이지만 여기서는 과하다.
</details>

<details><summary>Q5. greedy와 cost-benefit 중 무엇을 쓰겠습니까?</summary>

측정으로 답하자면 트래픽에 달렸다. 균등 쓰기에서는 greedy가 CB보다 1.3% 안쪽으로 낫다(표 T1). 핫/콜드 편향이 있으면 CB가 11–61% 낫다. **다만 host/GC 스트림 분리를 할 때만 그렇다** (표 T2). 1-stream이면 CB의 이득이 사라졌다. 실제 컨트롤러는 트래픽을 모르니 온도 분류, 여러 스트림, 또는 정책 전환을 쓴다. 또 FIFO는 비용은 싸지만 7% OP에서 greedy보다 6% 나빴다. 이 숫자는 이 시뮬레이션의 편향 모델(균일 hot/cold 2영역)에 대한 것이다. 실제 트레이스는 시간적 국소성이 다르니 대표 트레이스로 재측정해서 정해야 한다.
</details>

<details><summary>Q6. GC 복사와 host 쓰기가 겹치면 어떤 일이 생기고, 어떻게 막습니까?</summary>

GC가 페이지를 읽어 NAND에 프로그램하는 동안 host가 같은 LPN을 쓰면 두 쓰기의 완료 순서가 뒤바뀔 수 있다. 그래서 GC의 L2P 커밋을 "L2P가 아직 내가 복사한 old ppa를 가리킬 때만"으로 한다. 멀티코어에서는 그 비교와 쓰기가 원자적이어야 한다(LPN 범위 락 또는 CAS). 이 모델에서는 `g_copy_window` 훅으로 스케줄을 강제해 재현했고, 조건 없는 커밋 변형은 데이터 대조에서 FAIL했다. 읽기 쪽도 같은 문제가 있다. GC가 L2P를 바꾼 직후 옛 PPA를 읽는 진행 중 읽기는 erase가 끝나기 전에 완료되어야 하니, 재사용 전에 진행 중 읽기를 기다리거나 erase를 늦춘다.
</details>

<details><summary>Q7. 측정한 WAF와 실제 SSD의 WAF는 왜 다릅니까?</summary>

이 모델은 데이터 페이지 복사만 센다. 실제 WAF에는 저널과 맵 플러시, P2L 메타데이터, 읽기 재시도/읽기 교란으로 인한 재기록, 웨어 레벨링 이동, SLC 캐시 정리, 다중 플레인 부분 프로그램의 패딩이 더해진다. 그리고 호스트의 실제 트래픽은 균일 hot/cold 2영역보다 복잡하다. 그래서 측정 숫자는 "GC 정책과 OP가 얼마를 만드는가"의 기준선으로 쓰고, 실제 드라이브에서는 SMART 카운터로 다시 잰다. 해석식의 경우 Desnoyers Eq.4(LRU)와 Eq.11–12(greedy)는 이 시뮬레이션의 FIFO와 greedy 측정과 free-list 보정 후 잘 맞았다. Agarwal & Marrow의 `(1+ρ)/(2ρ)`는 OP가 커질수록 낮게 나왔다(50%에서 1.5 vs 측정 1.706).
</details>

<details><summary>Q8. 이 구조를 어떻게 테스트합니까? 경합은요?</summary>

세 층이다. ① 구조 불변식: L2P ↔ P2L ↔ 비트맵 ↔ count ↔ 힙 ↔ free 링을 서로 대조하는 `ftl_check`(작은 구성에서는 매 연산 후). ② 데이터 무결성: 모든 쓰기 값을 다르게 하고 참조 배열과 전체 대조. ③ 강제 스케줄: 프로그램과 커밋 사이 창에 훅으로 host 쓰기를 끼워 넣어 경합을 결정적으로 재현. 그리고 고의 버그(mutation)를 넣어서 하네스가 잡는지 확인했다. 본문의 조건부 커밋, 재빌드, 비트맵 경계 변형 14개를 모두 잡았다. 진짜 동시성(ISR/멀티코어)은 이 단일 스레드 모델의 범위 밖이다(see:MEM-M12). 페이싱의 지연도 벽시계가 아니라 "쓰기당 복사 페이지 수"라는 플랫폼 독립 값으로 쟀다.
</details>

<details><summary>Q9. host 쓰기 지연 스파이크를 줄이려면? 트레이드오프는요?</summary>

페이싱을 쓴다. host 쓰기 한 번에 `ceil(v/(PG-v))` 페이지만 복사하면 복사량이 쓰기 사이에 고르게 퍼진다. 측정에서 쓰기당 최대 복사가 122(전경) → 1(페이싱)로 줄었다(`./solution` 출력). 대가는 WAF다. 일찍 시작하면 블록이 덜 비워져서 WAF가 1.600 → 1.634로 올랐고, 표 T4에서는 워터마크를 크게 잡을수록 더 올랐다(free 풀이 OP를 먹는다). 또 v가 큰 블록을 만나면 quota가 커지므로(예: v=120이면 15), 최악 지연을 정말 제한하려면 quota 상한 + 하드 플로어에서의 전경 GC로 나눠 설계한다.
</details>

## 9. 추가 연습

### 연습 1 — valid-count 버킷 리스트

블록을 `v`(0..PG)별 침입형 이중 연결 리스트에 넣어 무효화 O(1), greedy pick O(PG)로 만든다. 선형 탐색과 모든 pick에서 v가 같음을 확인한다.

<details><summary>답</summary>

`bk_head/tail[v]`, `bk_next/prev[b]`. 무효화 시 현재 버킷에서 빼고 `v-1` 버킷 꼬리에 붙인다. pick은 v=0부터 올라가며 첫 비어 있지 않은 버킷의 머리. practice.c 출력: `mode 0: 4985 picks, max bucket scan 33 (PG+1 = 33)`, 선형 탐색과 불일치 0.
</details>

### 연습 2 — 정확한 cost-benefit을 O(PG)에

age의 기준을 "마지막 무효화 시각"으로 정의하고, 연습 1의 버킷 안에서 각 머리만 비교해 정확한 최댓값을 찾는다. 왜 머리만 봐도 되는가?

<details><summary>답</summary>

같은 v면 `c_b`가 같으니 순서는 `age`만으로 정해지고, 시간이 흘러도 안 뒤집힌다. 그리고 블록이 버킷에 들어가는 시각(`last_inv`)은 곧 그 순간이므로 꼬리에 붙이는 것만으로 버킷이 항상 시각 오름차순이다(정렬 삽입 불필요, `bk_sorted` 검사가 확인). 그러면 머리가 가장 오래되어 그 버킷의 최고점이다. 전체 비용은 버킷 수 PG+1이다(`max bucket scan 33`). 대가는 age 정의가 고정된다는 점(마지막 무효화 기준)이고, §5의 AGE_LAST_INV 측정치가 그 정의의 WAF다.
</details>

### 연습 3 — P2L + 시퀀스 번호로 L2P 다시 만들기

전 페이지를 스캔해 LPN별로 가장 큰 시퀀스 번호의 ppa를 골라 L2P를 복원한다. 복원 결과가 라이브 L2P와 같은지 확인하고, TRIM 뒤에는 왜 달라지는지 답한다.

<details><summary>답</summary>

TRIM은 플래시에 아무것도 쓰지 않으므로 그 LPN의 옛 페이지가 P2L에 그대로 남고, 스캔이 그것을 최신으로 되살린다(`rebuilt[17] == trimmed`). 그래서 실제 FTL은 trim 로그나 맵 스냅샷이 있어야 한다(전원 손실 처리, see:MEM-M14). 프로그램이 찢어진 페이지 처리도 이 스캔만으로는 안 되며, 이 연습은 복구 알고리즘이 아니라 P2L의 역할만 확인한다.
</details>

### 연습 4 — 비트맵 없이 유효 판정

`valid(ppa) ⇔ p < wp ∧ l2p[p2l[ppa]] == ppa`가 비트맵 판정과 모든 GC 시점에 일치하는지 확인한다.

<details><summary>답</summary>

쓰기·덮어쓰기·GC 재배치 모두에서 각 LPN에 대해 L2P가 가리키는 페이지는 정확히 하나이므로 두 판정이 같다. 이 사실이 비트맵을 "성능용 캐시"로 만든다. 실제로 비트맵을 저장하지 않고 재계산하는 설계도 가능하다. 다만 비트맵 없이는 GC 스캔마다 P2L(NAND 또는 DRAM)을 읽어야 한다. 조회 비용을 비트맵으로 바꾼 것이다.
</details>

### 연습 5 — 해석식 재현 (libm 없이)

Desnoyers 2012 Eq.4, Eq.11–12를 Lambert W까지 직접 구현하고 논문 표 값과 대조한다.

<details><summary>답</summary>

`my_exp`(축소 + 테일러 + 제곱 복원)와 이분법 W로 구현한다. practice.c는 논문 값 Table 1(`Sf` 0.03/0.07/0.23 → 16.837/7.318/2.371)과 greedy의 "free-list 보정 없는" 값(Sf 0.03 → 13.393, 본문 §3.2)을 확인한다. 실제 출력: `Eq.4 (Sf 0.07) = 7.318   Eq.12 (Sf 0.07, Np 64) = 6.600 (Table 2 with free-list correction: 6.623)`. 논문 Table 2의 6.623은 free-list 보정을 포함한 값이라 식의 순수 출력 6.600과 다르다. 식의 조건(보정 유무, Np)을 반드시 확인해야 한다는 교훈이다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 19/19 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o p_asan && ./p_asan   # 19/19, 보고 없음
```

버그 주입 확인(하네스가 잡는지): `solution.c` 사본에 다음 변형을 넣어 돌렸고, 전부 FAIL(또는 비정상 종료)이었다. `gc_key`가 `host_clk`를 씀, `valid_clear`가 heap_refresh를 안 함, L2P 무조건 커밋, 비트맵 워드 경계 어긋남, host가 예약을 무시, quota off-by-one, 재빌드에서 heapify 생략, free 풀 LIFO, GC가 P2L을 틀리게 기록, 빈 victim을 지우는 조건 오류, `v==PG` 가드 제거, 나눗셈 순서, trim의 무효화 누락, dead copy를 valid로 표시. 이 14개를 모두 잡았다. 15번째 변형(`gc_tick`의 정지 조건에서 `victim == NONE` 검사를 뺌)은 통과했는데, free 블록 수는 victim을 erase할 때만 늘고 그 순간 victim이 없어지므로 동치였다. (변형 파일은 레슨 폴더에 남기지 않았다.)

## 10. 핵심 정리

- FTL 자료구조 5개: L2P(정방향), P2L(역방향, 불변, OOB), 블록별 비트맵 + count, free 풀(FIFO), 닫힌 블록의 indexed 힙. 전부 한 불변식 집합으로 묶인다.
- 프로그램 후 L2P를 고치고, 옛 PPA를 무효화한다. GC 복사는 **`l2p[lpn] == old`일 때만** 커밋한다. 그렇지 않으면 새 데이터가 옛 데이터로 되돌아간다.
- greedy 키는 `(v<<48)|seq`로 힙에 그대로 들어간다. cost-benefit `(1-u)·age/(1+u)`는 시간에 따라 순서가 뒤집히므로 **얼린 시계 + 재빌드**(또는 v별 버킷 머리 스캔)로 유지한다.
- 교착 방지: host는 예약 아래로 못 가고, `nlpn < (nblk - RESERVE - 1)·PG`면 비둘기집 원리로 닫힌 블록에 invalid 페이지가 반드시 있다.
- 측정: 균등 쓰기는 greedy ≈ CB(greedy가 1.3% 안쪽으로 우세), 편향 쓰기는 CB가 11–61% 우세, **그 이득은 host/GC 스트림 분리가 있을 때만** 나왔다.
- free 풀은 OP를 먹는다(평균 free 20%이면 WAF 22.8). 워터마크는 WAF와 지연의 트레이드오프, 페이싱으로 쓰기당 복사량을 제한한다.
- 수치는 해석식(Desnoyers Eq.4, 12)과 free-list 보정 시 0.7% 이내로 맞았다. 실제 SSD WAF에는 메타데이터 쓰기 등이 더해진다.

## 참고자료

- P. Desnoyers, "Analytic Modeling of SSD Write Performance," SYSTOR 2012 — Eq.1–4 (LRU), Eq.11–12 (greedy), Table 1·2 값, `Np + 1` 버킷, 본문의 hot/cold 분석 (직접 읽음)
- M. Rosenblum, J. Ousterhout, "The Design and Implementation of a Log-Structured File System," ACM TOCS 10(1), 1992 — cost-benefit `(1-u)·age/(1+u)` (대학 강의 노트로 식을 확인)
- A. Kawaguchi, S. Nishioka, H. Motoda, "A Flash-Memory Based File System," USENIX 1995 — 플래시용 cost-benefit (`age·(1-u)/(2u)`로 알려져 있으나 2차 출처로만 확인)
- R. Agarwal, M. Marrow, "A Closed-Form Expression for Write Amplification in NAND Flash," IEEE GLOBECOM Workshops 2010 — `(1+ρ)/(2ρ)`: X. Xiang, B. Kurkoski, "An Improved Analytic Expression for Write Amplification in NAND Flash" (arXiv:1110.4245)의 인용으로 확인, 표현식과 ρ 정의
- 관련 레슨: DS-M11 (indexed heap), DS-A04 (강제 스케줄 훅), DS-A10 (NOR 로그 KV), MEM-M10/M11/M12/M14 (FTL 과정), CAP-M05 (캡스톤), STOR-B05 (Bitcask)
