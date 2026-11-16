---
id: MEM-B10
time: 60분
updated: 2026-11-15
---
# MEM-B10 포인터 체이싱으로 메모리 지연 측정

## 학습 목표

- **의존 load 체인**(dependent load chain, `p = *p`)이 왜 "지연(latency)" 을 재고, 독립 load 가 왜 "처리량" 을 재는지 설명한다.
- **Sattolo 알고리즘**으로 한 사이클(single-cycle) 무작위 순열을 만들고, 사이클이 정확히 하나인지 검사한다. Fisher–Yates 와 한 글자 차이인데 결과가 왜 다른지 증명한다.
- 캐시라인(이 맥 128 B) 크기의 원소로 체인을 깔아 4 KiB … 1 GiB working set 의 ns/load 곡선을 실제로 재고, **평탄 구간(plateau)** 을 코드로 찾아 L1 / L2 / DRAM 에 대응시킨다. SLC 와 sysctl 숫자가 곡선과 왜 안 맞는지도 측정으로 따진다.
- 같은 사이클을 **순차 순서**로 바꾸면 (프리페처) 왜 빨라지는지, **페이지 stride 배치**가 TLB 비용을 어떻게 드러내는지, 커서 k 개를 겹치면 (MLP) 왜 load 당 시간이 줄어드는지 숫자로 보인다.
- bring-up 에서 이 곡선을 "캐시가 켜졌나 · 경계가 데이터시트대로 보이나" 점검에 쓰는 함수를 짠다.

## 1. 문제

"새 보드에서 DRAM 지연이 몇 ns 인지 어떻게 재겠습니까?" — 펌웨어·실리콘 검증 면접의 단골 질문이고, 보드 bring-up 에서 캐시/MMU 설정과 DDR 설정이 맞는지 처음 확인하는 측정이기도 하다.

지연은 "load 하나가 주소를 낸 뒤 값이 돌아올 때까지" 의 시간이다. 그런데 요즘 코어는 load 수십 개를 동시에 날린다 (MLP, memory-level parallelism). `sum += a[idx[i]]` 처럼 서로 독립인 load 를 재면 미스들이 겹쳐서 지연이 아니라 **처리량**이 나온다 (ARCH-A03 의 실수 6). 겹치지 못하게 하려면 **다음 주소가 이번 load 의 결과**여야 한다: 각 원소에 다음 원소의 주소를 넣어 두고 `p = *(void **)p` 를 반복한다. 이것이 포인터 체이싱(pointer chasing)이다 — lmbench 의 `lat_mem_rd` 가 30 년째 쓰는 방법이다.

체인을 어떻게 까느냐가 무엇을 재느냐를 정한다.

1. **원소 크기 = 캐시라인.** 원소 하나가 줄 하나를 차지해야 "W 바이트 working set = W / 줄 크기 개의 서로 다른 줄" 이 된다. 이 맥(Apple M6)은 `hw.cachelinesize` = **128 B** 다. x86 습관대로 64 B 로 하면 무엇이 달라지는지도 잰다.
2. **무작위 순서.** 순차·고정 stride 면 프리페처(prefetcher)가 다음 줄을 미리 가져와 지연이 숨는다. 순서를 섞어야 한다.
3. **한 사이클.** 섞은 순열이 사이클 여러 개로 쪼개지면 0 에서 출발한 체인은 그중 하나만 돈다 → 실제 working set 이 주장한 것보다 작다. Sattolo 알고리즘은 언제나 길이 n 인 사이클 **하나**를 만든다.

```svg
<svg viewBox="0 0 740 316" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="mb10a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">체인 세 가지 — 상자 = 128 B 원소 (줄 하나), 안의 수 = next[i] (다음 원소 번호)</text>
  <text x="10" y="62" fill="currentColor">rand128</text>
  <text x="10" y="76" fill="currentColor" font-size="12">Sattolo</text>
  <g stroke="currentColor" fill="none">
    <rect x="80" y="50" width="100" height="30"/><rect x="190" y="50" width="100" height="30"/><rect x="300" y="50" width="100" height="30"/><rect x="410" y="50" width="100" height="30"/><rect x="520" y="50" width="100" height="30"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="88" y="70">[0] → 2</text><text x="198" y="70">[1] → 3</text><text x="308" y="70">[2] → 4</text><text x="418" y="70">[3] → 0</text><text x="528" y="70">[4] → 1</text>
    <text x="88" y="44">+0</text><text x="198" y="44">+128</text><text x="308" y="44">+256</text><text x="418" y="44">+384</text><text x="528" y="44">+512</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#mb10a)">
    <path d="M130,80 Q240,118 346,82"/><path d="M350,80 Q460,118 566,82"/>
    <path d="M570,80 Q405,135 244,82"/><path d="M240,80 Q350,128 456,82"/><path d="M460,80 Q295,140 134,82"/>
  </g>
  <text x="630" y="62" fill="currentColor">0→2→4→1→3→0</text>
  <text x="630" y="78" fill="currentColor">한 사이클, 무작위</text>
  <text x="10" y="168" fill="currentColor">seq128</text>
  <g stroke="currentColor" fill="none">
    <rect x="80" y="152" width="100" height="26"/><rect x="190" y="152" width="100" height="26"/><rect x="300" y="152" width="100" height="26"/><rect x="410" y="152" width="100" height="26"/><rect x="520" y="152" width="100" height="26"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="88" y="170">[0] → 1</text><text x="198" y="170">[1] → 2</text><text x="308" y="170">[2] → 3</text><text x="418" y="170">[3] → 4</text><text x="528" y="170">[4] → 0</text>
  </g>
  <text x="630" y="170" fill="currentColor">프리페처가 맞힘</text>
  <text x="10" y="232" fill="currentColor">page</text>
  <g stroke="currentColor" fill="none">
    <rect x="80" y="214" width="120" height="30"/><rect x="210" y="214" width="120" height="30"/><rect x="340" y="214" width="120" height="30"/><rect x="470" y="214" width="120" height="30"/>
  </g>
  <g class="f-warn" stroke="currentColor">
    <rect x="81" y="215" width="6" height="28"/><rect x="217" y="215" width="6" height="28"/><rect x="353" y="215" width="6" height="28"/><rect x="489" y="215" width="6" height="28"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="92" y="262">16 KiB 페이지 0</text><text x="222" y="262">페이지 1, 칸 1</text><text x="352" y="262">페이지 2, 칸 2</text><text x="482" y="262">페이지 3, 칸 3</text>
  </g>
  <text x="600" y="226" fill="currentColor">페이지마다 원소 1 개</text>
  <text x="600" y="242" fill="currentColor">칸 = i mod 128 (회전)</text>
  <text x="10" y="288" fill="currentColor">걷기: p = *(void **)p — 다음 주소가 이번 load 의 값이라 load 들이 겹치지 못한다</text>
  <text x="10" y="306" fill="currentColor">→ 걸린 시간 / 걸음 수 = load 하나의 지연 (load-to-use latency)</text>
</svg>
```

### Part A — 체인 만들기 (결정적 부품)

1. `sattolo(next, n, seed)` — i → next[i] 가 길이 n 인 사이클 하나. `count_cycles(next, n, seen, &longest)` — 사이클 수 (순열이 아니면 0).
2. `node_offset(lay, i, node, page)` — 배치 LINE (원소 i = 줄 i) 과 PAGE (원소 i = 페이지 i 의 i mod (page/node) 번째 칸).
3. `chain_build(buf, cap, next, n, lay, node, page)` — 원소 i 자리에 원소 next[i] 의 주소를 쓴다. 용량·크기·정렬 검사. `chain_verify(...)` — n 걸음 동안 모든 원소를 정확히 한 번씩 보고 출발점으로 돌아오나.
4. `ws_sizes(lo, hi, node, out, cap)` — 4 KiB … 1 GiB 의 2^k 와 √2·2^k 단계 (node 배수로 내림): 37 점.

### Part B — 단위와 평탄 구간 찾기

1. `ticks_to_ns(ticks, numer, denom)` — `mach_absolute_time` tick → ns, 넘침 없이 (이 맥 timebase 125/3 = 41.67 ns/tick).
2. `fmt_bytes`, `median` — 표 출력과 중앙값-of-N.
3. `find_plateaus(y, n, ratio, min_len, out, maxout)` — 지연 곡선에서 레벨이 바뀌는 곳을 찾는다. 합성 곡선(깨끗한 계단, ±8 % 잡음, 튀는 점, 점진적 전이, 계속 오르는 곡선)으로 검사한다.
4. **실측** (`demo_chase.c`): rand128 · rand64 · seq128 · seq64 · page 다섯 모드의 4 KiB … 1 GiB 스윕, 중앙값-of-9, `clang -S` 로 루프가 남았는지 확인, MLP (커서 k = 1 … 16), 코어 이동 탐지.

### Part C — 계층 이름 붙이기와 bring-up 점검

1. `name_level(ws, cfg, ncfg)` — sysctl/데이터시트 용량으로 "이 working set 은 어디에 들어가야 하나".
2. `hierarchy_check(x, y, n, cfg, ncfg, l1_max_ns)` — 평탄 구간 수, 기대한 캐시 경계마다 "곡선에 보였나" 비트, 가장 작은 working set 도 느리면 "캐시 꺼짐 의심". 실측 곡선에 돌려서 sysctl 의 L2 20 MiB 와 실측이 어긋나는 것을 이 함수가 잡아내는지 본다.

## 2. Problem (English)

> You are bringing up a new SoC and must report its load-to-use latency per memory level.
>
> **A.** Implement `void sattolo(uint32_t *next, size_t n, uint64_t *seed)` producing a uniformly random permutation that is a *single* n-cycle, and `size_t count_cycles(const uint32_t *next, size_t n, uint8_t *seen, size_t *longest)` (0 if `next` is not a permutation). Implement `node_offset` for two layouts — `LINE` (node i at `i*node`) and `PAGE` (node i in page i at slot `i mod (page/node)`) — and `void *chain_build(uint8_t *buf, size_t cap, const uint32_t *next, size_t n, layout_t lay, size_t node, size_t page)` that stores, at node i, the address of node `next[i]`, returning node 0 (NULL on bad arguments or insufficient capacity). `chain_verify` walks n steps and checks every node is visited once and the walk closes. `ws_sizes(4096, 1<<30, 128, out, cap)` returns 37 sizes: 2^k and floor(√2·2^k) rounded down to the node size.
>
> **B.** Convert Mach ticks to ns without overflow, compute the median, and implement `size_t find_plateaus(const double *y, size_t n, double ratio, size_t min_len, plateau_t *out, size_t maxout)` that segments a latency curve into levels (a point more than `ratio` away from the running segment median starts a new segment; segments shorter than `min_len` are transitions; adjacent levels within `ratio` merge). Then measure ns/load for 4 KiB … 1 GiB on a real machine with a random chain of 128 B nodes, and compare with 64 B nodes, a sequential chain, and one node per 16 KiB page. Make sure the compiler did not delete the loop.
>
> **C.** Given expected cache sizes, `hierarchy_check` reports which boundaries appear in the measured curve and whether even the smallest working set is slow (caches disabled?). Explain every mismatch between your curve and the datasheet.

Examples:

```text
sattolo(n=5, seed=2026)      -> next = [2 3 4 0 1]   cycle 0->2->4->1->3->0, count_cycles = 1, longest = 5
node_offset(PAGE, 130, 128, 16384) = 130*16384 + 2*128
ws_sizes(4096, 1<<30, 128)  -> 4096, 5760, 8192, 11520, 16384, ... , 1073741824   (37 values)
ticks_to_ns(24000000, 125, 3) = 1000000000
find_plateaus([0.8 0.9 0.8 4.8 8.3 8.4 8.2 95 98 101], ratio 1.4, min_len 2)
                             -> [0..2]=0.80  [4..6]=8.30  [7..9]=98.00   (4.8 is a transition)
measured (Apple M6, set 4): 4-64 KiB ~0.8 ns | 181 KiB-8 MiB ~7-10 ns | 16 MiB-1 GiB ~96-144 ns
```

Follow-ups: Why must the permutation be a single cycle? Why is the sequential chain fast although every load is dependent? Why does your L2 edge sit at 8–16 MiB when sysctl says 20 MiB? How many misses must be in flight to reach a given bandwidth?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열두 군데다 (Part A 6개: `sattolo`, `count_cycles`, `node_offset`, `chain_build`, `chain_verify`, `ws_sizes` / Part B 4개: `ticks_to_ns`, `fmt_bytes`, `median`, `find_plateaus` / Part C 2개: `name_level`, `hierarchy_check`). 난수 `xs64` · `rand_below`, 대조군 `fisher_yates`, `seq_cycle`, `layout_span`, 측정 루프 `chain_walk`, `isqrt64`, 합성 곡선 `synth`, 하네스는 주어진다. 스텁은 안전한 틀린 값을 돌려준다 (`sattolo` 는 항등 순열 = 사이클 n 개, `chain_build` 는 NULL — 하네스가 NULL 이면 걷지 않는다). 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:225  count_cycles(id, 5, seen, &longest) == 5 && longest == 1
FAIL starter.c:226  count_cycles(sw, 4, seen, &longest) == 2 && longest == 2
sattolo n=1..300 x 6 seeds: 1800 not single-cycle
FAIL starter.c:240  bad == 0
…
chain build+verify: 0/40 layouts x nodes x n x order OK
…
FAIL starter.c:441  r.seen_mask == 1u && !r.cache_off_suspect && r.nlevels == 2
25/82 passed
```

(exit 1. 통과한 25개는 스텁 값이 우연히 맞은 것 — 잘못된 인자에 0/NULL, `name_level` 이 "DRAM" 인 경우 등. 정답은 84개: 스텁에서는 `chain_build` 가 NULL 이라 고장 주입 검사 두 개가 건너뛰어진다.)

## 4. 힌트

<details><summary>힌트 1 — Sattolo 는 Fisher–Yates 에서 한 글자</summary>

Fisher–Yates: i = n−1 … 1 에 대해 j ∈ [0, **i**] 를 뽑아 a[i] ↔ a[j]. Sattolo: j ∈ [0, **i**) — 자기 자신과는 절대 안 바꾼다. 난수 범위는 `rand_below(seed, i)` (모듈로 편향 없는 거부 방식, 주어짐). 결과 배열 a 를 "i 다음은 a[i]" 로 읽으면 한 사이클이다.

</details>

<details><summary>힌트 2 — 사이클 세기는 두 번 훑기</summary>

먼저 순열인지 본다: 모든 값이 n 보다 작고, 같은 값이 두 번 나오지 않는다 (`seen[next[i]]`). 그다음 seen 을 지우고, 아직 안 본 s 마다 `i = s; while (!seen[i]) { seen[i] = 1; i = next[i]; len++; }` — 한 번 돌 때마다 사이클 하나. 전체 O(n).

</details>

<details><summary>힌트 3 — 체인 만들기와 되돌리기</summary>

`*(void **)(buf + node_offset(i)) = buf + node_offset(next[i]);` 를 i = 0 … n−1. node 는 `sizeof(void *)` 의 배수여야 저장이 정렬된다. 검사할 때는 주소 p → off = p − buf → LINE 이면 i = off / node, PAGE 면 i = off / page, 그리고 `node_offset(i) == off` 인지 (칸 경계가 아닌 주소를 거른다).

</details>

<details><summary>힌트 4 — √2 단계는 정수로</summary>

2^k · √2 = √(2^(2k+1)). `isqrt64(1ull << (2k+1))` 로 정수 제곱근을 구하고 `h -= h % node` 로 내린다. k = 12 면 isqrt(2^25) = 5792 → 128 배수 5760. 부동소수 `sqrt` 를 써도 이 범위에선 맞지만 정수로 하면 "왜 맞는지" 를 따질 필요가 없다.

</details>

<details><summary>힌트 5 — 평탄 구간: 기준은 "지금까지 구간의 중앙값"</summary>

직전 점 하나와 비교하면 천천히 오르는 경사(1.2 배씩)가 한 구간으로 이어 붙는다. 구간 [s, i−1] 의 **중앙값** 과 비교하면 경사는 짧은 구간들로 잘리고, `min_len` 미만은 버려진다. 튀는 점 하나는 구간을 둘로 가르지만, 마지막에 "레벨이 ratio 안이면 합치기" 로 복구된다.

</details>

<details><summary>힌트 6 — hierarchy_check 의 "보였다" 정의</summary>

이웃 평탄 구간 k, k+1 사이의 경계는 한 점이 아니라 구간 [x[p_k.last], x[p_{k+1}.first]] 다. 이 구간이 [C/2, 2C] 와 겹치면 "용량 C 의 경계가 보였다" 로 친다 (곡선은 √2 간격이고 실제 경계는 교체 정책·코어 이동으로 번진다). 첫 레벨이 l1_max_ns 보다 크면 가장 작은 working set 도 느리다는 뜻 — 캐시가 꺼져 있을 때의 모양이다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. **무엇을 재나**: "load-to-use 지연 = 의존 load 하나의 왕복. 독립 load 는 겹치니 처리량이다."
2. **체인 설계**: 캐시라인 크기 원소, 무작위 한 사이클 (Sattolo), 만들기·예열은 타이밍 밖, 결과를 sink 에 써서 루프가 안 지워지게.
3. **스윕**: 4 KiB → 1 GiB, √2 간격, 크기마다 중앙값-of-N, 공유 기계면 부하와 선점을 같이 기록.
4. **읽기**: 평탄 구간 = 계층, 경사 = 용량 경계. 데이터시트(sysctl) 용량과 비교하고, 어긋나면 이유를 대조 실험으로 가른다 (TLB = 페이지 stride 배치, 프리페치 = 순차 체인, 코어 이동 = CPU 번호).
5. **숫자로 결론**: "L1 0.8 ns, L2 7~10 ns, DRAM 100~120 ns (TLB 워크 포함), 순차는 프리페치로 2~10 ns, 커서 16 개면 load 당 8 ns."

### 0. 측정 환경

`sysctl -a | grep -E 'perflevel[0-9].*(l1d|l2)cache|cachelinesize|pagesize'` 실제 출력 (발췌, 코어 수는 `hw.perflevelN.physicalcpu`):

```text
hw.perflevel0.l1dcachesize: 131072     hw.perflevel0.l2cachesize: 20971520   (Super 2 코어, cpusperl2 2)
hw.perflevel1.l1dcachesize: 65536      hw.perflevel1.l2cachesize: 20971520   (Performance 4 코어, cpusperl2 4)
hw.perflevel2.l1dcachesize: 98304      hw.perflevel2.l2cachesize: 8388608    (Efficiency 6 코어, cpusperl2 6)
hw.cachelinesize: 128                  hw.pagesize: 16384                    hw.memsize: 34359738368
```

`machdep.cpu.brand_string` = Apple M6, macOS 27.2, `Apple clang version 21.0.0`. 코어 종류가 셋이고 **L1D 가 64 / 96 / 128 KiB 로 다르며, L2 는 클러스터마다 따로** 있다 (Super 2 코어가 20 MiB 하나, Performance 4 코어가 20 MiB 하나, Efficiency 6 코어가 8 MiB 하나 — `cpusperl2` 기준). SLC(system level cache)는 sysctl 에 없다. 메모리 규격(LPDDR 종류·버스 폭)은 이 레슨에서 확인하지 않았다 — 지연 측정에는 필요 없고, 대역폭은 MEM-B11 몫이다.

**부하와 코어 이동.** 이 맥은 다른 레슨 작성 세션들과 공유 중이었다 (측정 동안 `uptime` load average 17~25, 12 코어). 스레드에 `pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0)` 을 걸었지만 macOS 에는 코어 고정 API 가 없다. 탐색 단계에서 `pthread_cpu_number_np` 로 2000 번 찍어 보니 **12 개 CPU 전부**에 나타났다 (cpu0 42 회 … cpu6 379 회). 그래서 demo 는 rep 마다 스레드 CPU 시간 / 벽시계 = **onCPU %** 를 같이 찍어 선점(preemption)을 드러내고, 코어 이동 탐지를 따로 한다. 숫자는 네 세트를 쟀고, 본문 표는 onCPU 가 가장 높았던 세트 4 다 (시작 load 23.02 → 끝 21.66). "한가할 때 demo 재실행 권장".

### Part A — Sattolo: 왜 한 사이클인가

실제 실행 (`n = 5`, seed 2026, 위 그림의 체인):

| 단계 i | 뽑은 j ∈ [0, i) | a (= next) |
|---|---|---|
| 시작 | — | [0 1 2 3 4] (사이클 5 개) |
| 4 | 1 | [0 4 2 3 1] |
| 3 | 0 | [3 4 2 0 1] |
| 2 | 1 | [3 2 4 0 1] |
| 1 | 0 | [2 3 4 0 1] → 0→2→4→1→3→0 |

**불변식** (i 단계 직전): "사이클이 정확히 i+1 개이고, 각 사이클에 {0, …, i} 의 원소가 **정확히 하나** 들어 있다." 처음(i = n−1)은 고정점 n 개라 성립. a[i] ↔ a[j] 맞바꾸기는 순열에 전치 (i j) 를 합성하는 것이고, 전치는 i 와 j 가 **다른** 사이클이면 두 사이클을 합친다. j < i 이므로 j 와 i 는 {0..i} 의 서로 다른 원소 → 불변식에 의해 다른 사이클 → 합쳐져 사이클이 i 개, 각각 {0..i−1} 의 원소를 하나씩 가진다. i = 1 단계 뒤 사이클 1 개. ∎

Fisher–Yates 는 j = i (아무것도 안 함) 를 허용하므로 i 가 혼자 남을 수 있다 → 사이클 수의 기댓값이 조화수 H_n (n = 1000 이면 7.49). 하네스 실측: 200 번 평균 7.21 개, 한 사이클이었던 경우 1 번 (확률 1/n). **균등성**: Sattolo 의 선택 열은 (n−1)·(n−2)···1 = (n−1)! 가지이고 n-사이클도 (n−1)! 개 — 서로 다른 선택 열이 서로 다른 사이클을 만들어 일대일이다 (연습 2 가 n = 4 에서 6 개가 각각 1/6 로 나오는지 잰다).

왜 체인 측정에 중요한가: Fisher–Yates 체인을 0 에서 출발시키면 0 이 속한 사이클 길이는 **1…n 위에서 균등**하다 (P(길이 = k) = (n−1)/n · (n−2)/(n−1) ··· 1/(n−k+1) = 1/n). 평균 (n+1)/2 — "64 MiB 를 쟀다" 고 썼는데 평균 32 MiB, 절반 확률로 그보다 작은 데를 돈 것이다 (연습 1 실측 평균 502.0 / 이론 500.5).

**체인 배치.** `LINE` = 줄 i 에 원소 i (빽빽). `PAGE` = 페이지 i 의 i mod 128 번째 칸 (16 KiB / 128 B = 128 칸): 데이터는 n 줄인데 페이지는 n 개 → 같은 데이터 발자국에서 TLB 부담만 키운다. 칸을 돌리지 않고 오프셋 0 에 몰면 모든 원소가 같은 L1 세트에 들어가 캐시 효과가 섞인다 (ARCH-A03 의 `same` 배치, 연습 5). `chain_build` 는 원소마다 포인터 하나를 쓰므로 그 자체가 모든 페이지를 만지는 prefault 다 → 페이지 폴트가 타이밍 구간에 들어가지 않는다.

**크기 목록.** 2 의 거듭제곱만 쓰면 경계(예: 8 → 16 MiB 사이) 를 한 점으로만 본다. √2 단계를 끼우면 4 KiB … 1 GiB 에 37 점. 정수 제곱근이라 값이 플랫폼과 무관하게 같다 (5760, 11520, … 를 하네스가 정확히 검사).

### Part B — 단위와 측정 루프

- **tick → ns**: Apple Silicon 의 `mach_timebase_info` = 125/3 (실제 출력 `timebase 125/3 (41.67 ns/tick)`). `ticks × 125` 는 2^57 tick 근처에서 넘친다 → `q = t / d`, `r = t % d`, `q·n + r·n/d`. 한 tick 이 41.67 ns 라 load 하나는 못 잰다 → rep 하나에 2^19 = 524,288 걸음 (L1 이면 약 0.4 ms = 1 만 tick, DRAM 이면 약 60 ms).
- **ns/load 와 "체인 대역폭"**: 1 GB = 10^9 B 로 두면 B/ns = GB/s. 128 B 줄을 110 ns 마다 하나 = 1.16 GB/s — 의존 체인 하나가 낼 수 있는 대역폭의 상한이다 (MLP 절).
- **루프가 남았나**: `clang -std=c11 -O2 -S demo_chase.c` 의 타이밍 구간 (실제 출력 발췌):

```text
	bl	_mach_absolute_time
	mov	x21, x0
LBB4_48:                                ;   Parent Loop BB4_47 Depth=1
                                        ; =>  This Inner Loop Header: Depth=2
	ldr	x24, [x24]
	adds	x22, x22, #1
	b.lo	LBB4_48
; %bb.49:                               ;   in Loop: Header=BB4_47 Depth=1
	bl	_mach_absolute_time
```

`ldr x24, [x24]` — 주소 레지스터에 그대로 load 결과가 들어가는 의존 체인이 524,288 번 (`mov x22, #-524288` 부터 1 씩) 돈다. 끝 포인터는 `g_sink` (volatile) 에 더해 지워지지 않는다.

### Part B — 평탄 구간 찾기: 손으로 한 번

`y = [0.8 0.9 0.8 4.8 8.3 8.4 8.2 95 98 101]`, ratio 1.4, min_len 2 (실행 결과 `[0..2] 0.80 / [4..6] 8.30 / [7..9] 98.00`):

| i | 구간 [s, i−1] 중앙값 ref | 허용 [ref/1.4, ref·1.4] | y[i] | 동작 |
|---|---|---|---|---|
| 1 | 0.8 | [0.57, 1.12] | 0.9 | 이어 붙임 |
| 2 | 0.85 | [0.61, 1.19] | 0.8 | 이어 붙임 |
| 3 | 0.8 | [0.57, 1.12] | 4.8 | [0..2] 닫음 (길이 3, 레벨 0.8) |
| 4 | 4.8 | [3.43, 6.72] | 8.3 | [3..3] 닫음 — 길이 1 → 전이, 버림 |
| 5, 6 | 8.3, 8.35 | | 8.4, 8.2 | 이어 붙임 |
| 7 | 8.3 | [5.93, 11.6] | 95 | [4..6] 닫음 (레벨 8.3) |
| 8, 9 | 95, 96.5 | | 98, 101 | 이어 붙임 → 끝에서 [7..9] (레벨 98) |

합치기: 0.8 vs 8.3, 8.3 vs 98 은 ratio 밖 → 세 구간. 레벨을 **평균이 아니라 중앙값**으로 두는 이유: 공유 기계에서는 한 rep 이 선점당해 5 배로 튀는 일이 흔하다 (아래 표의 [최소] 와 중앙값 차이). 합성 곡선 검사에서 계단 · ±8 % 잡음 · 튀는 점 하나는 정확히 [0..10] [11..24] [25..36] 로, 혼합 모델 곡선은 `[0..10]=0.90 [12..24]=4.74 [26..36]=100.72` 로 (경사의 첫 점 11, 25 는 전이) 나왔다. 계속 1.5 배씩 오르는 곡선은 평탄 구간 0 개. 복잡도: 점 n 개에 구간 중앙값을 매번 다시 구하므로 O(n² log n) 이지만 n ≤ 128 이라 상관없다.

### Part B — 실측: 4 KiB … 1 GiB

<!-- code: demo_chase.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_chase.c -o demo_chase && ./demo_chase      # 약 60 초, 1 GiB 매핑
```

세트 4 실제 출력 (load 23.02 22.33 18.75 → 끝 21.66). 칸 = 9 rep 의 ns/load 중앙값 [최소]. page 칸의 괄호 = 페이지 수 (같은 페이지 수가 반복되는 √2 점과 n = 0 은 `-`). onCPU % = rand128 rep 들의 (스레드 CPU 시간 / 벽시계) 중앙값.

| W | rand128 | rand64 | seq128 | seq64 | page (페이지 수) | onCPU % |
|---|---|---|---|---|---|---|
| 4 KiB | 0.81 [0.73] | 0.82 [0.75] | 1.03 [0.83] | 0.84 [0.78] | - | 98 |
| 5.6 KiB | 0.95 [0.73] | 1.02 [0.80] | 0.89 [0.73] | 1.03 [0.80] | - | 98 |
| 8 KiB | 1.05 [0.98] | 1.12 [0.78] | 0.78 [0.77] | 0.81 [0.78] | - | 98 |
| 11.2 KiB | 0.85 [0.78] | 0.93 [0.80] | 0.91 [0.78] | 0.78 [0.77] | - | 99 |
| 16 KiB | 0.93 [0.75] | 0.80 [0.72] | 0.82 [0.80] | 1.12 [0.92] | 0.27 [0.24] (1) | 96 |
| 22.6 KiB | 0.83 [0.72] | 0.83 [0.78] | 0.88 [0.78] | 0.82 [0.77] | - | 98 |
| 32 KiB | 0.79 [0.76] | 1.02 [0.75] | 0.78 [0.70] | 0.79 [0.68] | 0.76 [0.67] (2) | 100 |
| 45.2 KiB | 0.80 [0.71] | 1.08 [0.72] | 0.82 [0.69] | 0.88 [0.69] | - | 99 |
| 64 KiB | 0.85 [0.81] | 1.09 [0.80] | 1.15 [1.02] | 1.02 [0.77] | 0.89 [0.75] (4) | 98 |
| 90.5 KiB | 2.51 [1.15] | 2.92 [1.04] | 1.49 [0.84] | 1.28 [0.86] | 0.78 [0.78] (5) | 98 |
| 128 KiB | 3.40 [0.78] | 4.76 [0.78] | 1.58 [0.78] | 1.35 [1.28] | 0.78 [0.77] (8) | 98 |
| 181 KiB | 7.12 [4.17] | 7.03 [5.34] | 2.00 [1.68] | 1.64 [1.32] | 0.80 [0.78] (11) | 98 |
| 256 KiB | 6.87 [4.36] | 6.96 [5.02] | 2.02 [1.71] | 1.39 [1.27] | 1.14 [1.02] (16) | 98 |
| 362 KiB | 7.19 [6.47] | 7.50 [6.59] | 2.06 [1.70] | 1.49 [1.10] | 1.02 [0.77] (22) | 97 |
| 512 KiB | 7.22 [5.32] | 7.95 [7.03] | 1.99 [1.75] | 1.30 [1.24] | 0.81 [0.79] (32) | 96 |
| 724 KiB | 7.17 [5.04] | 7.29 [5.27] | 1.99 [1.66] | 1.30 [1.14] | 0.95 [0.71] (45) | 96 |
| 1 MiB | 8.43 [6.86] | 7.41 [6.12] | 2.01 [1.67] | 1.29 [1.11] | 0.87 [0.80] (64) | 98 |
| 1.4 MiB | 7.72 [6.42] | 6.94 [5.46] | 1.96 [1.88] | 1.19 [1.15] | 0.81 [0.77] (90) | 98 |
| 2 MiB | 7.72 [7.35] | 8.09 [7.24] | 2.02 [1.95] | 1.22 [1.15] | 0.81 [0.81] (128) | 100 |
| 2.8 MiB | 8.21 [7.74] | 9.95 [7.18] | 2.07 [1.98] | 1.04 [1.03] | 2.35 [1.85] (181) | 100 |
| 4 MiB | 8.90 [7.96] | 8.21 [7.92] | 1.92 [1.86] | 1.05 [0.96] | 2.30 [2.28] (256) | 98 |
| 5.7 MiB | 9.15 [8.62] | 8.91 [8.40] | 2.25 [2.02] | 1.20 [0.98] | 2.50 [2.35] (362) | 100 |
| 8 MiB | 10.45 [9.42] | 10.44 [9.64] | 2.07 [1.95] | 1.06 [1.04] | 2.44 [2.37] (512) | 99 |
| 11.3 MiB | 31.85 [19.77] | 24.97 [18.32] | 5.00 [2.85] | 1.21 [1.04] | 7.07 [2.39] (724) | 99 |
| 16 MiB | 96.78 [80.88] | 80.03 [73.42] | 3.97 [2.51] | 1.10 [1.00] | 9.93 [9.45] (1024) | 98 |
| 22.6 MiB | 96.41 [91.42] | 89.24 [84.89] | 6.23 [3.91] | 1.12 [1.05] | 9.58 [4.97] (1448) | 99 |
| 32 MiB | 101.87 [95.97] | 102.08 [92.22] | 6.95 [4.32] | 1.09 [1.09] | 10.58 [8.23] (2048) | 96 |
| 45.3 MiB | 108.06 [102.87] | 109.99 [93.77] | 6.77 [4.77] | 1.79 [1.04] | 17.38 [6.72] (2896) | 93 |
| 64 MiB | 102.09 [97.31] | 105.29 [97.35] | 4.87 [3.36] | 1.37 [1.08] | 20.46 [17.10] (4096) | 98 |
| 90.5 MiB | 114.66 [102.82] | 126.83 [112.41] | 8.40 [4.50] | 2.11 [1.35] | 20.44 [18.06] (5792) | 90 |
| 128 MiB | 110.99 [103.91] | 110.56 [101.87] | 5.53 [3.64] | 1.08 [1.03] | 29.49 [21.96] (8192) | 93 |
| 181.0 MiB | 130.98 [105.40] | 110.32 [103.04] | 3.79 [3.34] | 1.07 [1.05] | 20.39 [18.86] (11585) | 81 |
| 256 MiB | 117.75 [108.29] | 134.82 [112.47] | 5.15 [3.97] | 1.29 [1.05] | 36.95 [27.15] (16384) | 91 |
| 362.0 MiB | 110.88 [106.16] | 124.40 [118.83] | 6.62 [4.43] | 1.19 [1.10] | 34.69 [20.47] (23170) | 97 |
| 512 MiB | 121.77 [109.95] | 136.66 [115.57] | 8.72 [5.68] | 1.70 [1.13] | 46.36 [26.55] (32768) | 91 |
| 724.1 MiB | 125.33 [117.08] | 146.07 [133.99] | 8.89 [4.21] | 1.63 [1.10] | 128.11 [114.28] (46340) | 90 |
| 1 GiB | 144.41 [118.99] | 212.19 [152.12] | 9.92 [7.60] | 1.64 [1.13] | 177.74 [131.88] (65536) | 83 |

```svg
<svg viewBox="0 0 740 396" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">ns/load vs working set — demo_chase 실측 (M6, 세트 4, 중앙값-of-9)</text>
  <g stroke="currentColor" stroke-width="0.5" stroke-dasharray="2 3" fill="none">
    <line x1="70" y1="271" x2="700" y2="271"/><line x1="70" y1="174.7" x2="700" y2="174.7"/><line x1="70" y1="78.3" x2="700" y2="78.3"/>
  </g>
  <g stroke="currentColor" fill="none"><line x1="70" y1="40" x2="70" y2="300"/><line x1="70" y1="300" x2="700" y2="300"/></g>
  <g fill="currentColor" font-size="12"><text x="36" y="275">1 ns</text><text x="30" y="179">10 ns</text><text x="24" y="82">100 ns</text></g>
  <g fill="currentColor" font-size="12"><text x="58" y="316">4K</text><text x="196" y="316">64K</text><text x="340" y="316">1M</text><text x="478" y="316">16M</text><text x="614" y="316">256M</text><text x="690" y="316">1G</text></g>
  <g stroke="currentColor" stroke-width="0.8" stroke-dasharray="5 3" fill="none">
    <line x1="210" y1="44" x2="210" y2="300"/><line x1="245" y1="44" x2="245" y2="300"/><line x1="455" y1="44" x2="455" y2="300"/><line x1="501" y1="44" x2="501" y2="300"/>
  </g>
  <g fill="currentColor" font-size="12"><text x="150" y="40">L1D P 64K</text><text x="250" y="40">Super 128K</text><text x="372" y="40">E L2 8M</text><text x="506" y="40">L2 20M (sysctl)</text></g>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="70,280 88,273 105,269 122,278 140,274 158,279 175,281 192,280 210,278 228,232 245,220 262,189 280,190 298,188 315,188 332,189 350,182 368,185 385,185 402,183 420,180 438,178 455,173 472,126 490,80 508,80 525,78 542,75 560,77 578,73 595,74 612,67 630,71 648,74 665,70 682,69 700,63"/>
  <polyline fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="4 3" points="70,266 88,266 105,267 122,265 140,279 158,260 175,269 192,259 210,262 228,237 245,194 262,182 280,187 298,185 315,181 332,183 350,182 368,183 385,178 402,159 420,165 438,157 455,85 472,81 490,80 508,79 525,82 542,81 560,79 578,79 595,76 612,74 630,75 648,77 665,73 682,75 700,77"/>
  <polyline fill="none" stroke="currentColor" stroke-width="1" points="70,270 88,276 105,281 122,275 140,279 158,276 175,281 192,279 210,265 228,254 245,252 262,242 280,242 298,241 315,242 332,242 350,242 368,243 385,242 402,241 420,244 438,237 455,241 472,204 490,213 508,194 525,190 542,191 560,205 578,182 595,199 612,215 630,202 648,192 665,180 682,180 700,175"/>
  <polyline fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="1 2" points="175,282 210,276 228,281 245,281 262,280 280,266 298,270 315,280 332,273 350,277 368,280 385,280 402,235 420,236 438,233 455,234 472,189 490,175 508,176 525,172 542,152 560,145 578,145 595,129 612,145 630,120 648,123 665,110 682,68 700,54"/>
  <g class="f-acc" stroke="currentColor">
    <circle cx="70" cy="280" r="2.5"/><circle cx="210" cy="278" r="2.5"/><circle cx="245" cy="220" r="2.5"/><circle cx="262" cy="189" r="2.5"/><circle cx="350" cy="182" r="2.5"/><circle cx="455" cy="173" r="2.5"/><circle cx="472" cy="126" r="2.5"/><circle cx="490" cy="80" r="2.5"/><circle cx="700" cy="63" r="2.5"/>
  </g>
  <g class="f-ok" stroke="currentColor"><rect x="452" y="238" width="6" height="6"/><rect x="697" y="172" width="6" height="6"/></g>
  <g class="f-warn" stroke="currentColor"><rect x="399" y="232" width="6" height="6"/><rect x="539" y="149" width="6" height="6"/><rect x="697" y="51" width="6" height="6"/></g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="342">굵은 선 + ● = rand128 (세트 4)    가는 점선 = rand128 (세트 2, 비교용)</text>
    <text x="20" y="360">가는 실선 + ■ = seq128    잔 점선 + ■ = page (페이지당 1 줄, n = 1 점 제외)</text>
    <text x="20" y="378">x = working set (√2 간격, 로그), y = ns/load (로그), 세로 점선 = sysctl 용량</text>
  </g>
  <g fill="currentColor" font-size="12"><text x="96" y="262">L1 ≈ 0.8</text><text x="300" y="172">L2 ≈ 7~10</text><text x="560" y="96">DRAM ≈ 100~140</text><text x="560" y="222">seq128 2~10</text></g>
</svg>
```

**읽는 법 — 평탄 구간 셋, 경사 둘.**

1. **L1: 4~64 KiB, 0.8 ns 안팎.** 64 KiB 를 넘자마자 오르기 시작하지만 128 KiB 의 [최소] 는 0.78 ns 다. 코어마다 L1D 가 64 / 96 / 128 KiB 이고 스레드가 코어 사이를 옮겨 다니므로, 90~128 KiB 는 "Super 코어에 있던 rep 은 L1 히트, Performance 코어에 있던 rep 은 L2" 가 섞인 구간이다. `find_plateaus` 도 세트 4 에서 이 구간을 따로 `[90.5 KiB..128 KiB] 2.95 ns` 로 잡았다 — 캐시 단이 아니라 **코어 섞임**이 만든 가짜 레벨이다. (0.8 ns 가 몇 사이클인지는 코어 클럭을 확인하지 않아 말하지 않는다.)
2. **L2: 181 KiB ~ 8 MiB, 7~10 ns.** 2 → 2.8 MiB 근처에서 1 ns 쯤 더 오르는 건 TLB 다 (아래 page 비교).
3. **DRAM: 16 MiB ~ 1 GiB, 96~144 ns (중앙값), 최소 81~119.** 이 값에는 TLB 미스 + 페이지 테이블 워크가 들어 있다 (1 GiB = 65,536 페이지, TLB 가 덮는 범위의 수십 배). ARCH-A03 은 PTE 가 캐시에 있을 때 워크가 +8~22 ns 라고 쟀다 → 순수한 DRAM 왕복은 이보다 약간 작다 (추정).
4. **SLC 단은 따로 보이지 않았다.** L2 (약 10 ns) 와 DRAM (약 100 ns) 사이에 중간 평탄 구간이 없다 — 11.3 MiB 의 31.85 ns 는 [최소] 19.77 과 함께 두 레벨이 섞인 경사의 점이다. SLC 는 모든 클러스터·GPU·다른 프로세스와 공유되고 크기가 sysctl 에 없다. "이 부하에서는 SLC 를 가를 수 없었다" 까지만 말한다.

**경계가 sysctl 보다 왼쪽이다.** L2 는 20 MiB 인데 rand128 은 8 MiB 와 16 MiB 사이 (세트 4), 세트 2 에서는 5.7 ~ 8 MiB, 세트 1 에서는 4 ~ 8 MiB, onCPU 가 가장 낮았던 세트 3 에서는 2.8 ~ 4 MiB 에서 무너졌다. `hierarchy_check` 도 세트 2 에서 `seen_mask=0x1` (L2 경계 미검출) 을 냈다 — 경계 구간 [5.7 MiB, 8 MiB] 가 [10 MiB, 40 MiB] 와 겹치지 않는다. 원인 후보를 대조 실험으로 갈랐다.

| 후보 | 대조 실험 | 결과 |
|---|---|---|
| TLB 가 L2 를 가린다 | page 배치 (데이터는 1/128) | 8 MiB 에서 page 2.44 ns — TLB 만으로는 +1.5 ns 남짓. 수십 ns 가 아니다 → 기각 |
| 코어 이동 (다른 클러스터 L2 로 가면 데이터가 없다) | rep 400 개를 CPU 번호로 "머묾 / 옮김" 분류 | 아래 표: 옮긴 rep 이 2~5 배 느림 → **일부 원인** |
| 다른 프로세스와 L2·SLC 공유 | 직접 대조 불가 (한가한 기계 필요) | 머문 rep 도 16 MiB 에서 93~129 ns → 남는 원인 후보 (추정) |

코어 이동 탐지 (`migration probe`, 2^16 load × 400 rep, 중앙값 ns/load, 실제 출력):

| W | 세트 3 머묾 (n) | 세트 3 옮김 (n) | 세트 4 머묾 (n) | 세트 4 옮김 (n) |
|---|---|---|---|---|
| 2 MiB | 7.34 (291) | 20.34 (109) | 7.46 (206) | 20.58 (194) |
| 4 MiB | 8.42 (298) | 33.26 (102) | 8.44 (177) | 20.81 (223) |
| 8 MiB | **9.53** (287) | 77.60 (113) | **91.99** (69) | 109.56 (331) |
| 16 MiB | 92.60 (115) | 101.63 (285) | 128.94 (46) | 134.33 (354) |
| 64 MiB | 98.31 (74) | 110.47 (326) | 128.01 (39) | 130.09 (361) |

읽기: 한 코어에 머무는 동안은 8 MiB 까지 L2 히트(9.5 ns)가 될 수 있다 (세트 3). 옮겨 간 rep 은 새 클러스터의 L2 를 다시 데우느라 느리다 (8 MiB = 65,536 줄 × 약 100 ns ≈ 6.5 ms — rep 하나 길이와 같은 크기). 같은 8 MiB 가 세트 4 에서는 머물러도 92 ns 였다 — 그때 그 클러스터를 다른 작업이 함께 쓰고 있었다고 본다 (추정, 확인 수단 없음). 16 MiB 는 어느 세트에서도 들어가지 않았다. 결론: **"이 스레드가 이 부하에서 실제로 쓸 수 있는 L2" 는 세트에 따라 3~11 MiB 이고, 20 MiB 는 클러스터 전체 용량**이다. Apple 이 L2 를 코어별로 나누는지 같은 내부 정책은 공개되지 않았다 — 말하지 않는다.

**page 배치 — TLB 를 데이터와 떼어 보기.** page 모드는 주소 범위 S 에 데이터가 S/128 바이트다. 같은 **데이터 발자국**의 rand128 과 빼면 페이지가 많아서 생긴 비용이 남는다 (세트 4):

| page 주소 범위 (페이지 수) | 데이터 | page | rand128 (같은 데이터) | 차이 |
|---|---|---|---|---|
| 2 MiB (128) | 16 KiB | 0.81 | 0.93 | ≈ 0 |
| 2.8 MiB (181) | 22.6 KiB | 2.35 | 0.83 | +1.5 |
| 32 MiB (2048) | 256 KiB | 10.58 | 6.87 | +3.7 |
| 45.3 MiB (2896) | 362 KiB | 17.38 | 7.19 | +10.2 |
| 256 MiB (16384) | 2 MiB | 36.95 | 7.72 | +29.2 |

첫 무릎은 128 → 181 페이지 (2 → 2.8 MiB): 가장 가까운 데이터 TLB 가 약 128~181 개 페이지를 덮는다는 뜻이다 (ARCH-A03 은 192 → 256 페이지에서 봤다 — 측정 간격과 코어 섞임을 생각하면 "약 128~256 페이지, 2~4 MiB" 로 말한다). 두 번째 무릎 2048 → 2896 페이지 (32 → 45 MiB) 는 ARCH-A03 의 2048 → 3072 (32~48 MB) 와 같다. Apple 은 TLB 항목 수를 공개하지 않는다. page 모드의 16 KiB 한 점 (n = 1, 원소가 자기 자신을 가리킴) 은 0.27 ns 로 L1 지연보다도 짧았다 — 같은 주소·같은 값이 반복되면 코어가 의존을 어떻게든 줄이는 것으로 보이며 (원인 미확인), 그래서 체인은 n ≥ 2 여야 의미가 있다.

**순차 체인은 왜 빠른가.** seq128 도 rand128 과 똑같이 의존 load 다 — 다음 주소는 load 가 끝나야 안다. 그런데 1 GiB 에서 9.92 ns (rand128 144 ns 의 1/15). 주소 순서가 +128, +128, … 로 일정하니 하드웨어 프리페처가 **load 가 주소를 알기 전에** 다음 줄들을 미리 가져다 놓는다: 의존 load 가 기다리는 건 DRAM 이 아니라 L1/L2 다. seq64 는 1~2 ns: 줄 하나에 load 두 번이라 두 번째는 무조건 L1 히트이고, 바이트당으로도 seq128 보다 빨랐다 (1 GiB: 64 B / 1.64 ns ≈ 39 GB/s vs 128 B / 9.92 ns ≈ 12.9 GB/s) — 그 차이가 왜 2 배를 넘는지는 확인 못 했다 (가설: 프리페처가 줄당 두 번 접근하는 패턴에서 더 멀리 앞서간다). 교훈: 지연을 재려면 무작위, 실제 코드(배열 순회)가 빠른 이유도 바로 이것.

**rand64 vs rand128.** 같은 W 에서 rand64 는 원소가 두 배지만 줄 수(W/128)는 같다. 곡선의 경계가 **바이트 기준으로 같은 자리**에 오고 레벨도 잡음 범위에서 같다 (16 MiB 80 vs 97, 32 MiB 102 vs 102 — 세트마다 엇갈림). 같은 줄의 짝 원소는 무작위로 먼 미래에 방문되니 그 사이에 쫓겨난다. 즉 64 B 원소로도 무작위 지연 곡선은 맞게 나오지만, "원소 수 = 줄 수" 라는 가정이 깨져 줄 단위 계산(연습 3 같은 혼합 모델)이 틀어진다. 순차에서는 64 B 가 지연을 절반 이하로 숨긴다 (위). 원소 크기는 `hw.cachelinesize` 에서 읽는다.

### Part B — MLP: 커서 k 개 (Little 의 법칙)

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">한 체인 vs 독립 커서 k 개 — 256 MiB rand128, demo_chase 실측 (세트 2, load 19.8→17.2)</text>
  <text x="10" y="48" fill="currentColor">k = 1</text>
  <g class="f-acc" stroke="currentColor"><rect x="60" y="36" width="100" height="16"/><rect x="160" y="36" width="100" height="16"/><rect x="260" y="36" width="100" height="16"/></g>
  <text x="365" y="48" fill="currentColor">load 하나씩, 앞 load 가 끝나야 다음 주소</text>
  <text x="10" y="84" fill="currentColor">k = 4</text>
  <g class="f-ok" stroke="currentColor">
    <rect x="60" y="66" width="100" height="10"/><rect x="66" y="78" width="100" height="10"/><rect x="72" y="90" width="100" height="10"/><rect x="78" y="102" width="100" height="10"/>
    <rect x="160" y="66" width="100" height="10"/><rect x="166" y="78" width="100" height="10"/><rect x="172" y="90" width="100" height="10"/><rect x="178" y="102" width="100" height="10"/>
  </g>
  <text x="290" y="92" fill="currentColor">같은 시간에 미스 4 개가 겹침 (커서마다는 여전히 의존)</text>
  <g fill="currentColor" font-size="12"><text x="10" y="150">ns/load</text></g>
  <g class="f-mut" stroke="currentColor">
    <rect x="80" y="160" width="279" height="12"/><rect x="80" y="176" width="159" height="12"/><rect x="80" y="192" width="73" height="12"/><rect x="80" y="208" width="44" height="12"/><rect x="80" y="224" width="20" height="12"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="40" y="170">k=1</text><text x="40" y="186">k=2</text><text x="40" y="202">k=4</text><text x="40" y="218">k=8</text><text x="40" y="234">k=16</text>
    <text x="365" y="170">111.70 ns → 1.15 GB/s</text><text x="245" y="186">63.42 → 2.02</text><text x="159" y="202">29.35 → 4.36</text><text x="130" y="218">17.69 → 7.23</text><text x="106" y="234">7.99 → 16.03 GB/s</text>
  </g>
  <text x="400" y="204" fill="currentColor">Little: 동시 미스 = BW × 지연 / 128 B</text>
  <text x="400" y="220" fill="currentColor">16.03 × 111.7 / 128 ≈ 14 줄</text>
</svg>
```

커서 k 개를 한 사이클 위 n/k 간격에 놓고 번갈아 한 걸음씩 가면 (연습 4 가 "k 구간이 사이클을 정확히 나눈다" 를 검사), 커서마다는 의존이지만 커서끼리는 독립이라 미스가 겹친다. 세 세트 실측 (ns/load 중앙값):

| k | 세트 2 | 세트 3 | 세트 4 |
|---|---|---|---|
| 1 | 111.70 | 110.10 | 221.23 |
| 2 | 63.42 | 57.55 | 99.37 |
| 4 | 29.35 | 30.16 | 50.07 |
| 8 | 17.69 | 18.97 | 27.03 |
| 16 | 7.99 | 12.61 | 16.34 |

(세트 4 의 k = 1 은 그 시점 부하가 튄 값 — 같은 세트 표의 256 MiB rand128 은 117.75 다. 세트 비교는 비율로 본다: k = 1 → 16 에서 8.7 ~ 14 배.) 의존 체인 하나는 1.15 GB/s 밖에 못 낸다 — **단일 스레드 메모리 대역폭은 지연 ÷ 동시 미스 수로 묶인다**. 100 GB/s 를 120 ns 지연으로 내려면 128 B 줄 기준 약 94 개가 동시에 날아다녀야 한다 (연습 4, 대역폭 수치는 가정 — 이 칩의 공식 피크는 확인하지 않았다). 그래서 STREAM (MEM-B11) 은 여러 스레드 + 독립 load 로 재고, 이 레슨의 숫자는 그 "다른 한쪽" 이다. 지연 · 대역폭을 루프라인(roofline)에 넣는 건 MEM-A08 · AIPERF-B04.

### Part C — 이름 붙이기와 bring-up 점검

`name_level` 은 데이터시트/sysctl 용량만 본다. 세트 4 실제 출력:

```text
rand128 plateaus (ratio 1.4, min_len 2):
  4 KiB    .. 64 KiB   level    0.85 ns  (sysctl capacity of first W says: L1D)
  90.5 KiB .. 128 KiB  level    2.95 ns  (sysctl capacity of first W says: L1D)
  181 KiB  .. 8 MiB    level    7.72 ns  (sysctl capacity of first W says: L2)
  16 MiB   .. 1 GiB    level  110.99 ns  (sysctl capacity of first W says: L2)
hierarchy_check: levels=4 seen_mask=0x3 cache_off_suspect=0
```

마지막 줄의 "L2" 는 틀렸다 — 16 MiB ≤ 20 MiB 라 용량표는 L2 라고 하지만 레벨은 111 ns 로 DRAM 이다. 그래서 이름은 **용량이 아니라 레벨 크기**로 붙이고 (몇 ns 대인가), 용량표는 `hierarchy_check` 처럼 "경계가 기대한 자리 근처에 보였나" 를 묻는 데만 쓴다. 세트 4 는 L2 경계 구간 [8 MiB, 16 MiB] 가 [10, 40 MiB] 와 겹쳐 0x3, 세트 2 는 [5.7, 8 MiB] 라 0x1 — 같은 기계에서 부하에 따라 판정이 갈린다. 실제 bring-up 에서는 부하가 없으니 (bare-metal 이나 U-Boot 에서 코어 하나) 이 점검이 더 날카롭다.

**bring-up 에서 쓰는 법.** 새 보드에서 DDR 학습(training)이 끝나고 캐시·MMU 를 켠 뒤, 같은 체인을 bare-metal 테스트나 U-Boot 명령으로 돌린다 (MEM-B08 의 memtest 다음 단계).

| 곡선 모양 | 의심 | 확인 |
|---|---|---|
| 4 KiB 부터 100 ns 대, 평탄 1 개 (`cache_off_suspect`) | D-cache 꺼짐 / MMU 꺼짐 (Device 메모리 속성) / 캐시 불가 영역 | SCTLR_EL1.C/M 비트, 페이지 테이블 속성 (U-Boot 의 `dcache` 명령으로 켜고 끈 비교) |
| L1 은 보이는데 L2 경계 없이 바로 DRAM | L2 미설정 · 비활성, way lock | L2 컨트롤러 레지스터, 데이터시트의 L2 enable 순서 |
| 레벨은 맞는데 DRAM 레벨이 기대보다 수십 % 큼 | DDR 타이밍(CL/tRCD 등) 보수적 설정, 낮은 클럭 | DDR 컨트롤러 설정 vs MEM-B02/B03 의 사이클 계산 |
| 경계가 데이터시트의 절반 | 캐시 일부만 켜짐, 다른 마스터와 공유 | 혼자 돌릴 때와 비교 |

### 복잡도와 실행 시간

| 함수 | 시간 | 공간 |
|---|---|---|
| `sattolo` / `count_cycles` | O(n) | next n × 4 B, seen n B |
| `chain_build` / `chain_verify` | O(n) (무작위 순서 쓰기·읽기라 줄마다 미스 — 따로 재지 않음) | 버퍼 = 주소 범위 |
| `find_plateaus` | O(n² log n), n ≤ 128 | 스택 배열 |

`solution.c` 0.4 초 (1 M 원소 Sattolo 포함), `practice.c` 0.1 초 미만, `demo_chase.c` 33 ~ 65 초 (1 GiB mmap + next 배열 64 MiB; 부하에 따라).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution      # 84/84 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_chase.c -o demo_chase && ./demo_chase   # 약 1 분
```

실제 출력 (`solution`):

```text
sattolo n=1..300 x 6 seeds: 0 not single-cycle
fisher-yates n=1000 x 200: single-cycle 1, mean cycles 7.21 (H_1000 = 7.49)
chain build+verify: 40/40 layouts x nodes x n x order OK
mix-model curve: 3 plateaus: [0..10]=0.90 [12..24]=4.74 [26..36]=100.72
84/84 passed
```

## 7. 자주 하는 실수

**1. 독립 load 로 지연을 잰다.** `for (i) sum += a[idx[i]];` 는 다음 주소가 이전 load 와 무관해 미스가 겹친다. 이 레슨의 MLP 표가 그 크기다: 커서 16 개면 load 당 8 ns — "DRAM 지연 8 ns" 라고 보고하게 된다. 고친 코드: `p = *(void **)p;`.

**2. Fisher–Yates 로 섞는다.** 0 에서 출발한 체인은 평균 절반만 돈다 (연습 1). 곡선이 오른쪽으로 밀려 "L2 가 더 크다" 는 엉뚱한 결론이 나온다. `rand_below(seed, i)` (j < i) 로 고치고 `count_cycles == 1` 을 측정 전에 확인한다 (demo 는 매 점마다 확인).

**3. 결과를 안 쓴다.** `chain_walk(p, steps);` 의 반환값을 버리면 `-O2` 가 루프를 지울 수 있다 → 0 ns. 끝 포인터를 `volatile` sink 에 쓰고, `clang -S` 에서 `ldr xN, [xN]` 루프를 눈으로 확인한다.

**4. 원소 크기를 64 B 로 박아 둔다.** 이 맥의 줄은 128 B 다. 순차 체인이면 load 의 절반이 같은 줄이라 지연이 반 이하로 숨고 (seq64 1~2 ns), 무작위여도 "원소 수 = 줄 수" 가 깨진다. `sysctlbyname("hw.cachelinesize")` (리눅스는 `sysconf(_SC_LEVEL1_DCACHE_LINESIZE)` 또는 `/sys/devices/system/cpu/cpu0/cache/`) 에서 읽는다.

**5. 만들기·폴트를 타이밍에 넣는다.** 1 GiB 체인을 만들 때 페이지 65,536 개가 처음 만져진다 (ARCH-A03: 폴트 하나 µs 대). 만들기 → 예열 (최소 한 바퀴, 최대 2^21 걸음) → 그다음 잰다.

**6. 공유 기계에서 평균 하나만 낸다.** 선점 한 번이면 rep 이 몇 배로 튄다 (세트 3 의 90.5 MiB: 중앙값 447 vs 최소 121, onCPU 28 %). 중앙값-of-N + [최소] + onCPU % + load average 를 같이 적고, 세트를 반복해 같은 결론만 믿는다.

**7. sysctl 용량으로 이름을 붙인다.** "16 MiB ≤ 20 MiB 니까 L2" — 실측 레벨은 111 ns 였다. 용량은 클러스터 전체 값이고 공유 · 코어 이동이 실효 용량을 줄인다. 레벨(ns 대)로 이름 붙이고, 어긋나면 대조 실험(page 배치, CPU 번호)으로 원인을 가른다.

**8. 2 의 거듭제곱 크기만 잰다.** 경계가 8 → 16 MiB 사이에 있으면 점 두 개로는 어디서 무너지는지 모른다. √2 단계를 넣으면 11.3 MiB 의 섞인 점(31.85 ns)이 경계를 보여 준다.

## 8. 면접 Q&A

<details><summary>Q1. 메모리 지연을 어떻게 재겠습니까? 왜 그냥 배열을 읽으면 안 되죠?</summary>

캐시라인 크기 원소의 무작위 한-사이클 링크드 리스트를 깔고 `p = *p` 를 수백만 번 돌려 걸음당 시간을 낸다. 배열을 읽으면 (1) 프리페처가 다음 줄을 미리 가져오고 (실측 seq128 1 GiB 9.92 ns), (2) load 들이 독립이라 미스가 겹친다 (커서 16 개 7.99 ns). 둘 다 지연이 아니라 처리량이다. 무작위 의존 체인이면 1 GiB 에서 100~140 ns 가 나온다. 꼬리: "타이머는?" → `mach_absolute_time` 한 tick 이 41.67 ns 라 load 하나는 못 잰다. rep 당 50 만 걸음, 중앙값-of-9.

</details>

<details><summary>Q2. 왜 무작위 순열이 "한 사이클" 이어야 하죠? 어떻게 만듭니까?</summary>

여러 사이클로 쪼개지면 출발점이 속한 사이클만 돈다. 균등 무작위 순열에서 그 길이는 1…n 균등이라 평균 절반 (연습 1 실측 502 / 1000). Sattolo: Fisher–Yates 에서 j 범위를 [0, i] 대신 [0, i) 로. 증명: 맞바꾸기 = 전치 합성, j < i 인 두 원소는 늘 다른 사이클에 있으므로 매 단계 사이클 두 개가 합쳐져 n 개 → 1 개. (n−1)! 개 선택 열 ↔ (n−1)! 개 n-사이클, 일대일이라 균등.

</details>

<details><summary>Q3. 측정 곡선에서 L2 경계가 8~16 MiB 에 나왔는데 sysctl 은 20 MiB 라고 합니다. 측정이 틀렸나요?</summary>

용량 20 MiB 는 클러스터(Performance 4 코어 또는 Super 2 코어)가 함께 쓰는 L2 전체다. 내 스레드 하나가 쓸 수 있는 몫은 (1) 같은 클러스터의 다른 코어가 쓰는 만큼 줄고, (2) 스레드가 다른 클러스터로 옮겨 가면 (macOS 는 고정 불가) 데이터가 거기 없다 — Efficiency 클러스터 L2 는 8 MiB 이기도 하다. CPU 번호로 rep 을 나눠 보니 머문 rep 은 8 MiB 에서 9.5 ns (L2 히트), 옮긴 rep 은 77.6 ns 였다. TLB 는 page 배치로 따로 재서 +1.5 ns 수준이라 기각. 그래서 "측정이 틀린 게 아니라 실효 용량을 잰 것" 이고, 한가한 기계에서 다시 재야 하드웨어 경계에 더 가까워진다.

</details>

<details><summary>Q4. 순차 체인도 load 가 서로 의존하는데 왜 빠릅니까?</summary>

의존성은 "다음 주소를 이번 load 가 끝나야 안다" 는 것이지만, 프리페처는 주소를 load 에서 얻지 않는다 — 지금까지의 미스 주소 패턴(+128, +128, …)에서 다음 줄을 **추측**해 미리 가져온다. 그래서 의존 load 가 기다리는 대상이 DRAM(100 ns 대) 에서 L1/L2(수 ns) 로 바뀐다. 실측 1 GiB: rand128 144 ns, seq128 9.9 ns, seq64 1.6 ns. 꼬리: "포인터 기반 자료구조(트리, 리스트)가 느린 이유?" → 노드가 흩어져 있으면 프리페처가 못 맞히고 매 노드가 지연 한 번. 배열 기반 / 노드 풀을 순서대로 할당하면 빨라진다.

</details>

<details><summary>Q5. DRAM 미스 하나가 110 ns 라면 그 안에 무엇이 들어 있나요?</summary>

DRAM 칩 안의 시간은 MEM-B03 의 DDR4-3200 예로 row conflict 가 tRP + tRCD + CL + 버스트 = 22 + 22 + 22 + 4 사이클 × 0.625 ns ≈ 44 ns, row hit 면 CL + 4 = 16 ns 쯤이다 (LPDDR 은 수치가 다르다 — 이 칩의 규격은 확인 안 함). 나머지는 칩 안이다: L1 → L2 → (SLC) 조회와 미스 판정, 인터커넥트(fabric) 왕복, 메모리 컨트롤러 큐와 스케줄링, 그리고 이 측정에서는 TLB 미스 + 페이지 테이블 워크 (ARCH-A03: PTE 가 캐시에 있으면 +8~22 ns). 그래서 "DRAM 이 느리다" 보다 "미스 경로가 길다" 가 맞다. 부하가 있으면 큐 대기가 늘어 loaded latency 가 커진다 (MEM-M04, Intel MLC 의 loaded latency 측정).

</details>

<details><summary>Q6. 단일 스레드가 메모리 대역폭을 다 못 쓰는 이유를 숫자로 설명해 보세요.</summary>

Little 의 법칙: 처리량 = 동시에 진행 중인 요청 수 / 지연. 의존 체인 하나는 동시 미스 1 개 → 128 B / 111.7 ns = 1.15 GB/s. 커서 16 개로 16.03 GB/s (실측, 약 14 줄 겹침). 100 GB/s 를 120 ns 에서 내려면 128 B 줄 94 개가 동시에 떠 있어야 하는데, 코어 하나의 미스 처리 슬롯(MSHR/LFB)과 프리페처가 그만큼 못 띄우면 여러 코어가 필요하다. 그래서 STREAM 은 스레드 수를 늘려 가며 잰다 (MEM-B11).

</details>

<details><summary>Q7. TLB 효과와 캐시 효과를 같은 곡선에서 어떻게 분리하죠?</summary>

배치를 바꾼 대조군. page 배치는 16 KiB 페이지마다 원소 하나 (칸 회전) — 주소 범위 S 에 데이터는 S/128. 같은 데이터 발자국의 rand128 과 빼면 페이지 수 때문에 생긴 비용만 남는다: 128 → 181 페이지에서 +1.5 ns (첫 단계 TLB 를 넘음), 2048 → 2896 페이지에서 +4 → +10 ns (둘째 단계를 넘어 페이지 워크), 16384 페이지 +29 ns. 깊은 분석(quad 대조군, 큰 페이지 시도)은 ARCH-A03. 꼬리: "16 KiB 페이지라서 좋은 점?" → 같은 TLB 항목으로 4 KiB 페이지의 4 배를 덮는다.

</details>

<details><summary>Q8. 새 보드 bring-up 에서 이 측정을 어디에 씁니까?</summary>

DDR 학습과 memtest (MEM-B08/B09) 가 끝난 뒤, 캐시·MMU 를 켠 상태에서 체인을 돌려 곡선 모양을 본다. 처음부터 100 ns 대로 평탄하면 D-cache 가 꺼졌거나 메모리가 Device/Non-cacheable 로 매핑된 것 (페이지 테이블 속성 확인). L1 다음 바로 DRAM 이면 L2 가 안 켜졌다. DRAM 레벨이 기대보다 크면 DDR 타이밍을 보수적으로 잡았거나 클럭이 낮다. 숫자 자체도 펌웨어 성능 예산 (ISR 안에서 캐시 미스 몇 번까지 허용되나) 의 입력이다. Cortex-M 처럼 캐시가 작거나 없는 MCU 에서는 TCM 과 외부 SDRAM/PSRAM 의 지연 차이를 같은 방법으로 잰다.

</details>

<details><summary>Q9. 결과가 실행마다 다릅니다. 무엇을 보고하죠?</summary>

중앙값-of-N 과 [최소], 그리고 측정 조건: load average, onCPU % (스레드 CPU 시간 / 벽시계), 코어 종류. 최소는 "방해가 없을 때" 에 가깝고 중앙값은 전형적 값이다. 결론은 여러 세트에서 반복된 것만: 이 레슨에서는 L1 0.8 ns, L2 7~10 ns, DRAM 100 ns 대, 경계 위치(L1 64~128 KiB, 실효 L2 경계 3~16 MiB)가 네 세트에서 반복됐고, SLC 단과 정확한 L2 경계는 부하 때문에 말하지 않는다.

</details>

## 9. 추가 연습

### 연습 1 — Fisher–Yates 체인이 실제로 도는 크기

n = 1000 Fisher–Yates 순열 4000 개에서 0 이 속한 사이클 길이의 평균과 P(길이 ≤ 500) 을 재고 이론값과 비교하라.

<details><summary>답 보기</summary>

실측: 평균 **502.0** (이론 (n+1)/2 = 500.5), P(≤ 500) = **0.492** (이론 0.500). 이론: 0 → x1 → … 가 k 걸음째 처음 0 으로 돌아올 확률 = (n−1)/n · (n−2)/(n−1) ··· 1/(n−k+1) = 1/n → 길이는 1…n 균등. 즉 Fisher–Yates 로 "64 MiB 체인" 을 만들면 평균 32 MiB 만 돈다. Sattolo 는 4000 개 모두 1000.

</details>

### 연습 2 — Sattolo 의 균등성

n = 4 로 Sattolo 를 60,000 번 돌려 나온 순열을 세라. 몇 가지가 나오고, 각각 몇 번인가?

<details><summary>답 보기</summary>

**6 가지** (= (4−1)!), 모두 4-사이클, 각각 10000 ± 400 안 (σ = √(60000 · 1/6 · 5/6) ≈ 91). 선택 열 (j3, j2, j1) ∈ [0,3) × [0,2) × [0,1) = 6 가지가 6 개 사이클에 일대일. Fisher–Yates 라면 24 가지 순열이 나오고 그중 4-사이클은 6 개뿐 (1/4 = 1/n).

</details>

### 연습 3 — 혼합 모델과 "반쯤 오른 점"

캐시 용량 C, 안에서 t_in, 밖에서 t_out. W > C 일 때 줄이 캐시에 있을 확률을 C/W 로 보는 단순 근사에서 지연이 (t_in + t_out)/2 가 되는 W 를 구하고, `half_rise_size` (log2 선형 보간) 로 확인하라. 실측 곡선은 이 모델보다 가파른가?

<details><summary>답 보기</summary>

(C/W)·t_in + (1 − C/W)·t_out = (t_in + t_out)/2 ⇔ C/W = 1/2 ⇔ **W = 2C**. C = 128 KiB → 262,144, C = 16 MiB → 33,554,432 이 목록에 정확히 있어 보간도 정확히 그 값. 이 모델에서 90 % 까지 오르려면 C/W = 0.1, 즉 W = 10C — 경사가 3 옥타브 넘게 이어진다. 실측 세트 4 의 L2 → DRAM 은 8 MiB 10.45 → 11.3 MiB 31.85 → 16 MiB 96.78 로 **한 옥타브 안에서** 거의 다 올랐다 (모델보다 훨씬 가파르다). 같은 순서로 도는 사이클 접근에서 LRU 에 가까운 교체는 "용량을 넘는 순간 거의 다 미스" (절벽) 를 만들고, 무작위 교체여야 C/W 처럼 완만해진다. 이 근사는 직관용이지 교체 정책의 정확한 모델이 아니다 (ARCH-M05 · M06 의 시뮬레이터로 확인 가능).

</details>

### 연습 4 — k 커서 출발점과 Little 의 법칙

n = 4096 Sattolo 사이클에서 원소 0 부터 n/k 걸음마다 출발점을 하나씩 잡는 `spaced_starts` 를 짜고, k = 1, 2, 4, 8, 16 에서 각 커서가 n/k 걸음 동안 밟는 원소가 사이클을 정확히 나누는지 확인하라. 그리고 100 GB/s 를 120 ns 지연, 128 B 줄로 내려면 동시 미스가 몇 개 필요한가?

<details><summary>답 보기</summary>

모든 k 에서 겹침 없이 n 개를 정확히 한 번씩, 커서 j 는 커서 j+1 의 출발점에서 끝난다 (그래서 커서들이 서로 따라잡지 않는다). 필요한 동시 미스 = 100 × 120 / 128 = **93.75 줄**. 실측 커서 16 개 7.99 ns/load = 16.03 GB/s 에서 거꾸로 계산하면 16.03 × 111.7 / 128 ≈ 14 줄이 겹친 셈 — 커서 수(16)에 가깝다. (100 GB/s 는 계산용 가정이다.)

</details>

### 연습 5 — 페이지 배치의 칸 회전

P = 1000 페이지에 원소를 하나씩 둘 때 (i mod 128) 칸 회전과 오프셋 0 고정의 칸별 원소 수를 세라. 데이터 발자국과 주소 범위는?

<details><summary>답 보기</summary>

회전: 칸마다 7 또는 8 개 (1000 = 128·7 + 104). 고정: 칸 0 에 1000 개 — L1 세트 번호가 페이지 안 오프셋에서 나온다면 (16 KiB 페이지 · VIPT 구조상 추정, ARCH-A03) 1000 줄이 한 세트에 몰려 캐시 미스가 TLB 효과와 섞인다. 데이터 128,000 B (125 KiB) vs 주소 범위 16,384,000 B (15.6 MiB) — 그래서 page 배치는 "같은 데이터의 rand128" 과 빼서 읽는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 16/16 passed
```

## 10. 핵심 정리

- 지연 = 의존 load 체인 `p = *p` 의 걸음당 시간. 독립 load · 순차 접근은 MLP · 프리페처 때문에 처리량을 잰다.
- 체인 = 캐시라인(이 맥 128 B) 원소 + Sattolo 한 사이클 (j ∈ [0, i)). Fisher–Yates 면 평균 절반만 돈다. 측정 전 `count_cycles == 1`.
- 만들기·예열은 타이밍 밖, 끝 포인터는 volatile sink, `clang -S` 로 `ldr xN, [xN]` 루프 확인, 중앙값-of-N + [최소] + onCPU % + load avg.
- M6 실측 (세트 4): L1 0.8 ns (≤ 64 KiB, 64~128 KiB 는 코어 섞임), L2 7~10 ns (181 KiB ~ 8 MiB), DRAM 96~144 ns (TLB 워크 포함). SLC 단은 이 부하에서 안 보였다.
- 실효 L2 경계 (세트마다 3~16 MiB) < sysctl 20 MiB: 클러스터 공유 + 코어 이동 (머문 rep 8 MiB 9.5 ns vs 옮긴 rep 77.6 ns). 이름은 용량이 아니라 레벨로.
- 대조군: page 배치 (TLB 무릎 128~181 · 2048~2896 페이지), seq128 (1 GiB 9.9 ns), 커서 16 개 (111.7 → 8.0 ns, Little 의 법칙: 동시 미스 = BW × 지연 / 줄).
- bring-up: 처음부터 평탄 100 ns = 캐시/MMU 꺼짐, L2 경계 없음 = L2 미설정, DRAM 레벨 큼 = DDR 타이밍·클럭 확인.

## 참고자료

- L. W. McVoy, C. Staelin, "lmbench: Portable Tools for Performance Analysis", USENIX ATC 1996 — `lat_mem_rd` (포인터 체이스 지연 곡선의 원조): <http://www.bitmover.com/lmbench/>
- Ulrich Drepper, "What Every Programmer Should Know About Memory" (2007), 3장 — 링크드 리스트 워킹 셋 스윕, 순차 vs 무작위, 프리페치: <https://people.freebsd.org/~lstewart/articles/cpumemory.pdf>
- S. Sattolo, "An algorithm to generate a random cyclic permutation", Information Processing Letters 22 (1986) — 서지 세부는 확인 권장. Fisher–Yates · Sattolo 정리는 Wikipedia "Fisher–Yates shuffle" 의 Sattolo 절.
- J. D. C. Little, "A Proof for the Queuing Formula L = λW", Operations Research 9(3), 1961 — Little 의 법칙.
- Intel Memory Latency Checker (MLC) — idle / loaded latency 측정 도구 (x86): <https://www.intel.com/content/www/us/en/developer/articles/tool/intelr-memory-latency-checker.html>
- Apple Developer: `mach_absolute_time` / `mach_timebase_info`, `pthread_set_qos_class_self_np`, `sysctlbyname("hw.perflevel*")` — 코어 고정 API 는 없다 (ARCH-A03 에서 `thread_policy_set` 시도).
- U-Boot `dcache` / `icache` 명령 (cmd/cache.c) — bring-up 에서 캐시를 켜고 끄며 비교.
- 관련 레슨: ARCH-B10 (AMAT), ARCH-M04 · M05 · M06 (캐시 시뮬레이터 · 교체 정책), ARCH-M07 (루프 순서), ARCH-A03 (TLB reach 실측 — 같은 Sattolo 체이스), ARCH-M08 (false sharing), OS-B10 (페이징), MEM-B03 (row hit/miss 사이클), MEM-B08 · B09 (memtest), MEM-B11 (STREAM 대역폭), MEM-M04 (코히어런스 트래픽 실측), MEM-A08 · AIPERF-B04 (루프라인)
