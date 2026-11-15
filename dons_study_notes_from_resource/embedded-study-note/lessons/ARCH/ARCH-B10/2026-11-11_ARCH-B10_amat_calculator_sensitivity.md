---
id: ARCH-B10
time: 60분
updated: 2026-11-11
---
# ARCH-B10 평균 메모리 접근 시간(AMAT) 계산기

## 학습 목표

- 다단계 캐시의 **AMAT(Average Memory Access Time, 평균 메모리 접근 시간)** 재귀식을 **로컬(local) 히트율**로 세우고, 아래에서 위로 접어 올리는(Horner) 코드로 계산할 수 있다.
- **로컬 미스율 vs 전역(global) 미스율**을 구분하고, 프로파일러/PMU 가 주는 숫자를 재귀식에 넣기 전에 변환한다 (안 하면 AMAT 가 40% 낙관적으로 나온다).
- AMAT 를 **CPI(Cycles Per Instruction)** 로 옮겨 "메모리 때문에 명령당 몇 cycle 을 잃나" 를 말할 수 있다 (I-fetch 와 load/store 분리).
- **민감도 표**: 계층별 히트율 +1%p · 히트 시간 −1 cycle 이 AMAT 를 얼마나 줄이는지 계산하고, "1%" 를 로컬로 보느냐 전역으로 보느냐에 따라 **병목 계층이 뒤집힌다**는 것을 설명한다.
- 클럭을 2배로 올려도 2배 빨라지지 않는 이유(DRAM 은 ns 고정 = memory wall)와, 실시간 펌웨어에서 **평균(AMAT) 대신 최악 지연**을 봐야 하는 이유를 숫자로 보인다.

## 1. 문제

"L1 히트율 1% 올리기와 L3 히트율 1% 올리기, 어느 쪽이 더 이득인가?" — CPU/SoC 아키텍처·실리콘 검증·성능 펌웨어 면접의 단골이고, 답은 **공식 한 줄 + 정의 한 번 확인**("그 1% 는 로컬인가 전역인가?")으로 갈린다. 실제 일에서는 "캐시를 키울까, DRAM 을 빠르게 할까, 핫 데이터를 TCM 으로 옮길까" 를 고르는 계산이 바로 이것이다.

메모리 접근 하나는 L1 에서 시작해 히트하면 끝나고, 미스하면 한 단계 아래로 **내려간다**. 그래서 각 계층의 히트율은 "그 계층까지 **내려온** 접근 중 몇 % 가 히트했나" — 이것이 **로컬 히트율**이다. 반면 "전체 접근 중 몇 % 가 이 계층까지 미스했나" 는 **전역 미스율**이다.

```svg
<svg viewBox="0 0 740 345" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b10-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">접근 1,000,000 번이 내려가는 길 (계층 구조 A)</text>
  <rect x="20" y="36" width="360" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="34" y="56" fill="currentColor" font-weight="bold">L1  4 cyc</text>
  <text x="34" y="73" fill="currentColor" font-size="12">도착 1,000,000 (100%) · 로컬 히트 90%</text>
  <rect x="60" y="106" width="280" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="74" y="126" fill="currentColor" font-weight="bold">L2  12 cyc</text>
  <text x="74" y="143" fill="currentColor" font-size="12">도착 100,000 (10%) · 로컬 70%</text>
  <rect x="100" y="176" width="200" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="112" y="196" fill="currentColor" font-weight="bold">L3  40 cyc</text>
  <text x="112" y="213" fill="currentColor" font-size="12">도착 30,000 (3%) · 로컬 60%</text>
  <rect x="140" y="246" width="120" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="152" y="266" fill="currentColor" font-weight="bold">DRAM 200</text>
  <text x="152" y="283" fill="currentColor" font-size="12">도착 12,000</text>
  <g stroke="currentColor" fill="none">
    <path d="M200 80 L200 104" marker-end="url(#b10-arr)"/>
    <path d="M200 150 L200 174" marker-end="url(#b10-arr)"/>
    <path d="M200 220 L200 244" marker-end="url(#b10-arr)"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="208" y="96">미스 10% → 전역 미스 0.10</text>
    <text x="208" y="166">미스 30% → 전역 0.03</text>
    <text x="208" y="236">미스 40% → 전역 0.012</text>
  </g>
  <rect x="400" y="36" width="330" height="254" rx="8" fill="none" stroke="currentColor"/>
  <text x="414" y="60" fill="currentColor" font-weight="bold">재귀식 (안쪽부터 계산)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="414" y="86">AMAT = t1 + m1·(t2 + m2·(t3 + m3·tmem))</text>
    <text x="414" y="112">below3 = 200</text>
    <text x="414" y="132">below2 = 40 + 0.4·200 = 120</text>
    <text x="414" y="152">below1 = 12 + 0.3·120 = 48</text>
    <text x="414" y="172">AMAT   =  4 + 0.1·48  = 8.8</text>
  </g>
  <text x="414" y="204" fill="currentColor" font-weight="bold">합 형태 (같은 값)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="414" y="228">Σ reach_k · t_k</text>
    <text x="414" y="248">= 1·4 + .1·12 + .03·40 + .012·200</text>
    <text x="414" y="268">= 4 + 1.2 + 1.2 + 2.4 = 8.8</text>
  </g>
  <text x="10" y="316" fill="currentColor" font-size="12">m_k 는 반드시 LOCAL 미스율 (그 계층에 도착한 접근 기준).</text>
  <text x="10" y="336" fill="currentColor" font-size="12">reach_k = 계층 k 까지 내려오는 비율 = 위 계층 로컬 미스율의 곱 = 바로 위 계층의 전역 미스율.</text>
</svg>
```

### Part A — AMAT 계산기의 핵심

`level_t { name, hit_time(cycles), hit_rate(LOCAL) }` 배열로 계층을 표현한다 (마지막 = DRAM, 히트율 1.0).

- `amat(L, n)` — 재귀식을 맨 아래부터 위로 접어 올린다.
- `reach_frac(L, n, k)` — 전체 접근 중 계층 k 까지 내려오는 비율. `global_miss(L, n, k)` — 계층 k 의 전역 미스율.
- 검산: `Σ reach_k · t_k == amat` (계층별 **기여(contribution)** — 1.2% 만 가는 DRAM 이 2.4 cycle 로 가장 큰 덩어리 중 하나라는 걸 본다).
- `cpi_with_memory(cpi_base, refs_per_instr, amat, l1_hit)` — base CPI 는 "전부 L1 히트" 로 잰 값이므로 스톨은 `AMAT − t_L1` 만큼만 더한다.

### Part B — 면접관이 파고드는 함정

- `amat_validate` — n 범위, 음수/무한 시간, 히트율 범위 밖 **또는 NaN**, 마지막 계층 히트율 ≠ 1.0 을 각각 다른 에러 코드로.
- `local_from_global(gmiss, nc, local_hit)` — 측정 도구가 주는 전역 미스율을 로컬 히트율로 바꾼다. 전역 미스율이 아래로 갈수록 **커지면** 불가능한 입력(−1). 위 계층 전역 미스가 0 이면 아래 계층엔 아무도 안 오므로 로컬 1.0.
- 테스트가 보여 주는 흔한 실수: 전역 미스율을 재귀식에 그대로 넣으면 8.8 대신 **5.3272**.
- `sens_local(L, n, k, dp)` — 계층 k 의 로컬 히트율을 dp 올렸을 때 ΔAMAT (1.0 에서 자름). `sens_global(...)` — "전체 접근의 dp" 가 계층 k 에서 새로 히트할 때 ΔAMAT.
- `bottleneck(L, n, use_global)` — +1%p 의 효과가 가장 큰 계층. 같은 계층 구조 A 에서 로컬 정의면 **L1**, 전역 정의면 **L3**.

### Part C — 칩/펌웨어 현실: 클럭, CPI, 최악 지연

- `level_spec_t { cycles, ns, hit_rate }` — on-chip 캐시 지연은 **cycle 에 묶이고**, DRAM 지연은 **ns 에 묶인다**. `scale_to_freq(spec, n, f_ghz, out)` 로 주어진 클럭에서의 cycle 수로 바꾼다.
- `cpi_split(w, amat_i, t_i1, amat_d, t_d1)` — 명령어 fetch(I-side, 명령당 1번) 와 load/store(D-side, 명령당 0.35번) 를 따로 더한다. `exec_time_us(instr, cpi, f_ghz)`.
- 1 GHz → 2 GHz 에서 실행 시간이 **1.66배** 만 빨라지는 것을 테스트로 확인한다.
- `worst_case_latency(L, n)` — 직렬 조회 모델에서 모든 캐시가 미스할 때 = Σ t_k. Cortex-M7 급 예시(D-cache 1 cycle + 외부 SDRAM 75 ns @ 400 MHz)에서 평균 2.5 vs 최악 31 cycle, DTCM 은 평균 = 최악 = 1.

## 2. Problem (English)

> **Multi-level cache AMAT calculator.**
>
> A memory hierarchy is an array of levels `{hit_time (cycles), local_hit_rate}`; the last level (DRAM) always hits.
>
> 1. Implement `double amat(const level_t *L, int n)` using `AMAT = t1 + m1*(t2 + m2*(t3 + ... ))` where `m_k` is the **local** miss rate. Also return the fraction of accesses that reach level k, and the global miss rate of level k.
> 2. Validate input (NaN, out-of-range rates, negative times, last level must hit). Convert measured **global** miss rates to local hit rates. Compute the sensitivity `ΔAMAT` for a +1 percentage-point hit-rate improvement at each cache level, under both interpretations ("1% of accesses reaching that level" vs "1% of all accesses"), and report the bottleneck level.
> 3. Given cache latencies in cycles and DRAM latency in ns, compute AMAT and CPI (`CPI = CPI_base + ifetch/instr·(AMAT_I − t_L1I) + data/instr·(AMAT_D − t_L1D)`) at 1 GHz and 2 GHz and explain why doubling the clock does not halve execution time. Also give the worst-case single-access latency.
>
> Example: L1 4c/90%, L2 12c/70%, L3 40c/60%, DRAM 200c → AMAT = 8.8 cycles; with global miss rates (0.10, 0.03, 0.012) plugged in naively you get 5.3272 — wrong.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
# ... FAIL 줄들 ...
# 12/55 passed   (exit 1) — TODO 13개를 채워 55/55 를 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — 재귀식은 아래에서 위로</summary>

`acc = t[n-1]` (DRAM) 에서 시작해 `k = n-2 … 0` 으로 `acc = t[k] + (1 - h[k]) * acc`. 재귀 함수로 써도 되지만 루프 하나면 끝난다 (다항식 Horner 평가와 같은 모양).

</details>

<details>
<summary>힌트 2 — reach 와 전역 미스율은 같은 곱</summary>

`reach(k) = Π_{j<k} (1 − h_j)`. 계층 k 의 전역 미스율은 "계층 k 까지 다 미스" = `reach(k+1)`. DRAM(h=1) 의 전역 미스율은 0.

</details>

<details>
<summary>힌트 3 — NaN 은 범위 검사를 통과한다</summary>

`if (h < 0 || h > 1) return ERR;` 는 NaN 일 때 두 비교가 모두 거짓이라 **통과**한다. 거꾸로 "정상 조건" 을 쓰고 부정한다: `if (!(h >= 0 && h <= 1)) return ERR;`.

</details>

<details>
<summary>힌트 4 — 민감도는 복사본으로 다시 계산</summary>

`level_t tmp[]` 에 복사 → `tmp[k].hit_rate += dp` (1.0 에서 자름) → `amat(tmp) − amat(L)`. 해석식은 `−dp · reach_k · below_k` (below_k = 계층 k+1 부터의 AMAT). AMAT 는 각 h_k 에 대해 **1차식**이라 유한 차분이 해석식과 정확히 같다.

</details>

<details>
<summary>힌트 5 — 전역 +1%p 를 로컬로 환산</summary>

전체 접근의 1% 는 계층 k 에 도착하는 접근의 `0.01 / reach_k` 다. 그래서 `sens_global(k, dp) = sens_local(k, dp / reach_k)`. reach_k 가 0 이면 효과도 0.

</details>

<details>
<summary>힌트 6 — 클럭 스케일링은 단위를 섞지 마라</summary>

`hit_time(cycles) = cycles + ns × f_GHz` (1 GHz 에서 1 ns = 1 cycle). 실행 시간(us) = `instr × CPI / (f_GHz × 1000)`.

</details>

## 5. 풀이

### Part A — 재귀식 두 가지 모양

**재귀형**: 계층 k 의 "체감 지연" = 자기 히트 시간 + (미스하면) 아래 계층 전체의 체감 지연.

```
AMAT_k = t_k + m_k · AMAT_{k+1},   AMAT_{DRAM} = t_DRAM
```

이때 `m_k` 는 **계층 k 에 도착한 접근 기준** 미스율이다 — 아래 계층의 AMAT 가 "도착한 접근 하나" 당 비용이기 때문이다. 이것을 펴면 **합형**이 된다:

```
AMAT = Σ_k reach_k · t_k ,   reach_k = Π_{j<k} m_j
```

"각 계층의 히트 시간 × 그 계층까지 오는 접근 비율" 의 합. 이 모델은 **직렬 조회**(L1 에서 미스를 확인한 뒤 L2 를 본다) 를 가정한다. 그래서 L2 에서 히트한 접근은 `t1 + t2` 를 낸다 (L2 의 t 는 "L1 미스 이후 추가로 드는 시간").

| 계층 | t | 로컬 h | reach | 전역 미스 | 기여 reach·t | 비중 |
|---|---:|---:|---:|---:|---:|---:|
| L1 | 4 | 0.90 | 1.000 | 0.100 | 4.0 | 45.5% |
| L2 | 12 | 0.70 | 0.100 | 0.030 | 1.2 | 13.6% |
| L3 | 40 | 0.60 | 0.030 | 0.012 | 1.2 | 13.6% |
| DRAM | 200 | 1.00 | 0.012 | 0 | 2.4 | 27.3% |

(표는 `demo_sensitivity.c` 출력과 같다 — 아래 Part B 의 실제 출력 참고.) 접근의 1.2% 만 DRAM 에 가는데 AMAT 의 27% 를 차지한다. "희귀하지만 비싼 사건" 이 평균을 지배하는 전형.

**CPI 로 옮기기.** base CPI 를 잴 때 이미 "모든 load 가 L1 히트" 였다면 L1 히트 지연(load-to-use) 은 base 에 들어 있다. 그래서 명령당 메모리 스톨은

```
stall/instr = refs/instr × (AMAT − t_L1)        예: 0.35 × (8.8 − 4) = 1.68
CPI = 1.0 + 1.68 = 2.68
```

H&P 의 `CPI = CPI_base + 미스/명령 × 미스 페널티` 와 같은 식이다 — `AMAT − t_L1 = m_1 × below_1` 이 바로 "L1 미스율 × L1 미스 페널티" 다.

### Part B — 로컬/전역, 그리고 "1%" 의 정의

**전역 → 로컬.** perf/PMU 류 도구는 보통 "전체 load 중 L2 미스 비율" 같은 **전역** 숫자를 준다 (Arm PMU 이벤트로는 `L1D_CACHE`, `L1D_CACHE_REFILL`, `L2D_CACHE`, `L2D_CACHE_REFILL` 같은 접근/리필 카운트 — 추가 연습 4). 재귀식에 넣으려면 `local_miss_k = global_k / global_{k-1}` 로 바꾼다. 안 바꾸고 넣으면:

```
틀린 값: 4 + 0.1·(12 + 0.03·(40 + 0.012·200)) = 5.3272   ← L2·L3 미스율을 10배·30배 작게 본 셈
맞는 값: 8.8
```

**민감도 = 편미분.** AMAT 는 각 `h_k` 에 대해 1차식이다 (합형에서 `h_k` 는 `reach_{k+1}, reach_{k+2}, …` 에 `(1 − h_k)` 로 한 번씩만 곱해진다). 그래서

```
∂AMAT/∂h_k = −reach_k · below_k        (below_k = 계층 k+1 부터의 AMAT)
∂AMAT/∂t_k = reach_k
```

이고, +1%p 의 유한 차분은 미분 × 0.01 과 **정확히** 같다 (1.0 에서 잘릴 때만 예외). 테스트가 세 계층 모두에서 이것을 확인한다.

"1% 올린다" 에는 두 가지 뜻이 있다:

| 정의 | 뜻 | ΔAMAT | 큰 쪽 |
|---|---|---|---|
| 로컬 +1%p | 그 계층에 **도착한** 접근의 1% 가 추가 히트 | `−0.01 · reach_k · below_k` | reach 가 큰 **위쪽**(보통 L1) |
| 전역 +1%p | **전체** 접근의 1% 가 그 계층에서 추가 히트 | `−0.01 · below_k` | below 가 큰 **아래쪽**(보통 LLC) |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">계층 구조 A: 히트율 +1%p 일 때 AMAT 감소량 (cycles) — 정의가 답을 뒤집는다</text>
  <text x="10" y="50" fill="currentColor" font-weight="bold">로컬 +1%p</text>
  <g fill="currentColor"><text x="20" y="78">L1</text><text x="20" y="106">L2</text><text x="20" y="134">L3</text></g>
  <rect x="60" y="64" width="58" height="20" class="f-ok" stroke="currentColor"/>
  <rect x="60" y="92" width="14" height="20" class="f-mut" stroke="currentColor"/>
  <rect x="60" y="120" width="7" height="20" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12"><text x="126" y="79">0.48 (병목)</text><text x="82" y="107">0.12</text><text x="75" y="135">0.06</text></g>
  <text x="380" y="50" fill="currentColor" font-weight="bold">전역 +1%p</text>
  <g fill="currentColor"><text x="390" y="78">L1</text><text x="390" y="106">L2</text><text x="390" y="134">L3</text></g>
  <rect x="430" y="64" width="58" height="20" class="f-mut" stroke="currentColor"/>
  <rect x="430" y="92" width="144" height="20" class="f-mut" stroke="currentColor"/>
  <rect x="430" y="120" width="240" height="20" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12"><text x="496" y="79">0.48</text><text x="582" y="107">1.2</text><text x="676" y="135">2.0 (병목)</text></g>
  <text x="10" y="170" fill="currentColor" font-size="12">막대 길이: 0.01 cycle = 1.2 px (두 패널 같은 축척)</text>
  <rect x="10" y="184" width="720" height="104" rx="6" fill="none" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="24" y="206">로컬 L3 +1%p = L3 에 오는 3% 의 1% = 전체의 0.03% 만 DRAM 을 피한다 → 0.0003 × 200 = 0.06</text>
    <text x="24" y="228">전역 L3 +1%p = 전체의 1% 가 DRAM(200) 대신 L3 에서 끝난다 → 0.01 × 200 = 2.0</text>
    <text x="24" y="250">전역 L3 +1%p = 로컬 60% → 93.3% (0.01 / reach 0.03 = +33%p) 와 같다 — 훨씬 큰 개선.</text>
    <text x="24" y="272">면접에서는 계산 전에 "그 1% 가 어느 기준인가요?" 를 먼저 묻는 것이 정답의 절반.</text>
  </g>
</svg>
```

**워크로드가 바뀌면 로컬 기준 병목도 바뀐다.** 계층 구조 B (워킹셋이 L1 보다 크고 L2 에 들어감: L1 50%, L2 로컬 95%) 에서는 `reach_L2 = 0.5`, `below_L2 = 140` 이라 L2 로컬 +1%p 가 0.70 으로 L1(0.19) 보다 크다. 일반적으로 `S_k = reach_k·below_k` 에서 `S_k − S_{k+1} = reach_k · [t_{k+1} + (m_{k+1} − m_k) · below_{k+1}]` — **L1 미스율 m_1 이 크고 L2 미스율이 작으면** 음수가 될 수 있다.

`demo_sensitivity.c` 실제 출력 (Apple M6, `Apple clang version 21.0.0`):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sensitivity.c -o demo && ./demo
```

```text
== A: typical : AMAT = 8.8000 cycles ==
level  t(cyc)  local_h  reach    global_miss  contrib  share  | dLocal+1pp  dGlobal+1pp  d(-1cyc)
L1          4    0.900   1.0000       0.1000    4.000  45.5% |    -0.4800      -0.4800   -1.0000
L2         12    0.700   0.1000       0.0300    1.200  13.6% |    -0.1200      -1.2000   -0.1000
L3         40    0.600   0.0300       0.0120    1.200  13.6% |    -0.0600      -2.0000   -0.0300
DRAM      200    1.000   0.0120       0.0000    2.400  27.3% |          -            -   -0.0120

== B: working set > L1, fits L2 : AMAT = 13.5000 cycles ==
level  t(cyc)  local_h  reach    global_miss  contrib  share  | dLocal+1pp  dGlobal+1pp  d(-1cyc)
L1          4    0.500   1.0000       0.5000    4.000  29.6% |    -0.1900      -0.1900   -1.0000
L2         12    0.950   0.5000       0.0250    6.000  44.4% |    -0.7000      -1.4000   -0.5000
L3         40    0.500   0.0250       0.0125    1.000   7.4% |    -0.0500      -2.0000   -0.0250
DRAM      200    1.000   0.0125       0.0000    2.500  18.5% |          -            -   -0.0125

== C: hot loop + cold random tail : AMAT = 6.2400 cycles ==
level  t(cyc)  local_h  reach    global_miss  contrib  share  | dLocal+1pp  dGlobal+1pp  d(-1cyc)
L1          4    0.980   1.0000       0.0200    4.000  64.1% |    -1.1200      -1.1200   -1.0000
L2         12    0.500   0.0200       0.0100    0.240   3.8% |    -0.0400      -2.0000   -0.0200
L3         40    0.200   0.0100       0.0080    0.400   6.4% |    -0.0200      -1.6000   -0.0100
DRAM      200    1.000   0.0080       0.0000    1.600  25.6% |          -            -   -0.0080
```

읽는 법: B 의 L2 행(로컬 −0.70) 이 로컬 기준 병목. C 의 L3 전역 열 −1.6 은 **잘림** 이다 — L3 전역 미스가 0.8% 뿐이라 "전체의 1%" 를 더 히트시킬 수 없어 0.8%p 만 반영됐다 (`sens_local` 의 1.0 클램프). 마지막 열(히트 시간 −1 cycle) 은 항상 `−reach_k` 라 **L1 이 언제나 최대** (추가 연습 3).

### Part C — 클럭, CPI, 최악 지연

**왜 2배가 아닌가.** 클럭을 올리면 on-chip 캐시는 같은 cycle 수로 따라오지만 (같은 파이프라인 단계), DRAM 은 tRCD/tCL 같은 **ns 단위** 타이밍과 인터커넥트 지연이 그대로라 cycle 수가 늘어난다. 1 GHz 에서 100 cycle 이던 DRAM 이 2 GHz 에서 200 cycle.

```text
== clock scaling (L1 4c/90%, L2 12c/70%, L3 40c/60%, DRAM 100 ns; L1I 99%; base CPI 1.0, 0.35 data refs/instr) ==
f(GHz)  DRAM(cyc)  AMAT_D   AMAT_I   CPI     time(us,1M instr)  speedup_vs_1GHz
  1.0        100    7.600    4.360   2.620             2620.0            1.000
  0.5         50    7.000    4.300   2.350             4700.0            0.557
  2.0        200    8.800    4.480   3.160             1580.0            1.658
  3.0        300   10.000    4.600   3.700             1233.3            2.124
  4.0        400   11.200    4.720   4.240             1060.0            2.472
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">클럭 대비 속도향상 (1 GHz 기준) — 이상(점선) vs 모델(실선)</text>
  <g stroke="currentColor" fill="none">
    <path d="M80 260 L660 260"/><path d="M80 260 L80 50"/>
    <path d="M80 260 L640 60" stroke-dasharray="5 4" opacity=".6"/>
    <path d="M150 232 L220 210 L360 177 L500 154 L640 136" stroke-width="2"/>
  </g>
  <g class="f-acc" stroke="currentColor">
    <circle cx="150" cy="232" r="4"/><circle cx="220" cy="210" r="4"/><circle cx="360" cy="177" r="4"/>
    <circle cx="500" cy="154" r="4"/><circle cx="640" cy="136" r="4"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="150" y="278">0.5</text><text x="220" y="278">1</text><text x="360" y="278">2</text>
    <text x="500" y="278">3</text><text x="640" y="278">4 GHz</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="72" y="264">0</text><text x="72" y="214">1</text><text x="72" y="164">2</text>
    <text x="72" y="114">3</text><text x="72" y="64">4×</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="368" y="196">1.66×</text><text x="648" y="140">2.47×</text>
    <text x="240" y="130" opacity=".8">이상(점선): 클럭 비례</text>
    <text x="400" y="232">DRAM 100 ns 고정 → cycle 로는 클럭에 비례해 늘어난다</text>
    <text x="400" y="250">메모리 바운드일수록 곡선이 더 눕는다 (memory wall)</text>
  </g>
</svg>
```

**설계 옵션 비교.** 같은 데모의 마지막 표 — "무엇을 개선할까" 를 AMAT 로 줄 세운다 (실제 결정은 면적·전력·비용까지 본다):

```text
== design options vs A (AMAT 8.800) ==
  bigger L1: 5c / 93%      AMAT 8.360  (-0.440)
  faster L2: 10c           AMAT 8.600  (-0.200)
  bigger L3: 75% local     AMAT 7.900  (-0.900)
  faster DRAM: 160c        AMAT 8.320  (-0.480)
```

L3 로컬 60% → 75% 는 전역으로는 0.03 × 0.15 = 0.45%p 개선이고, `0.0045 × 200 = 0.9` 가 그대로 나온다. 민감도 표의 숫자만으로도 이 순위를 미리 맞힐 수 있다.

**펌웨어 관점: 평균이 아니라 최악.** AMAT 는 평균이다. 하드 실시간 ISR 의 마감은 **최악 지연** 으로 따진다. 직렬 조회 모델의 최악은 모든 계층을 다 거친 `Σ t_k` — 구조 A 에서 256 cycle (평균 8.8 의 29배). Cortex-M7 급 예시(파라미터는 예시: D-cache 1 cycle 95%, 외부 SDRAM 75 ns, 400 MHz → 30 cycle) 에서는 평균 2.5, 최악 31. 반면 **TCM(Tightly Coupled Memory)** 은 캐시가 아니라 항상 같은 지연이라 평균 = 최악. 그래서 실시간 루프의 코드/데이터는 ITCM/DTCM 에 두고, 큰 버퍼만 캐시 가능한 SDRAM 에 둔다 (DMA 버퍼라면 캐시 일관성 문제가 따라온다 — ARCH-M12). 그리고 실제 최악은 이 모델보다 **더 나쁘다**: 라인 전체 fill, dirty victim write-back (추가 연습 5), 버스에서 DMA 와의 경합, DRAM 리프레시 — 모델값은 하한이다.

**이 맥(Apple M6)의 캐시 크기** (지연이 아니라 크기만 sysctl 로 나온다):

```bash
$ sysctl hw.perflevel0.l1dcachesize hw.perflevel0.l2cachesize hw.cachelinesize
hw.perflevel0.l1dcachesize: 131072
hw.perflevel0.l2cachesize: 20971520
hw.cachelinesize: 128
$ sysctl hw.perflevel0.name hw.perflevel1.name hw.perflevel1.l1dcachesize hw.perflevel1.l2cachesize hw.perflevel2.name hw.perflevel2.l1dcachesize hw.perflevel2.l2cachesize hw.pagesize
hw.perflevel0.name: Super
hw.perflevel1.name: Performance
hw.perflevel1.l1dcachesize: 65536
hw.perflevel1.l2cachesize: 20971520
hw.perflevel2.name: Efficiency
hw.perflevel2.l1dcachesize: 98304
hw.perflevel2.l2cachesize: 8388608
hw.pagesize: 16384
```

Super 코어 L1D 128 KB, Performance 64 KB, Efficiency 96 KB, L2 20 MB(Super/Performance perflevel) / 8 MB(Efficiency), 라인 128 B (ARCH-M08 에서 잰 일관성 단위와 같은 숫자). `sysctl hw.l3cachesize` 는 아무것도 출력하지 않았다 — SoC 공용 SLC 는 여기에 안 나온다. 위 표의 지연(4/12/40/200 cycle) 은 이 칩의 값이 아니라 **교과서적 파라미터** 다. 이 맥의 실제 계층별 지연 계단은 MEM-B10 의 포인터 체이싱으로 잰다 — 그 값을 이 계산기에 넣으면 된다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
# ...
# 55/55 passed   (exit 0)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san
# 55/55 passed   (ASan/UBSan 경고 없음)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드/생각 |
|---|---|---|
| 프로파일러의 "L2 miss 3%" 를 재귀식의 `m2` 로 넣음 | 그 3% 는 **전체 접근 기준(전역)**. 재귀식은 L2 에 **도착한** 접근 기준(로컬) → 5.33 vs 8.8 | `m2 = 0.03 / 0.10 = 0.3` (`local_from_global`) |
| `CPI = base + refs × AMAT` | base CPI 에 L1 히트 지연이 이미 들어 있어 두 번 셈 (2.68 이 아니라 4.08) | `refs × (AMAT − t_L1)` |
| I-fetch 를 빼먹음 (`refs/instr = 0.35`) | 모든 명령은 fetch 를 한 번 한다. I-cache 미스도 스톨 | I-side 와 D-side 를 따로 (`cpi_split`) |
| "1% 올리면" 을 정의 없이 계산 | 로컬/전역에 따라 병목이 L1 ↔ L3 로 뒤집힘. 또 95% → 96% (+1%p) 와 95% × 1.01 (+1% 상대) 도 다르다 | 정의를 먼저 못 박는다 |
| DRAM 지연을 cycle 상수로 두고 클럭만 올림 | DRAM 은 ns 고정 → cycle 이 클럭에 비례해 늘어난다 | `cycles + ns × f_GHz` (`scale_to_freq`) |
| `if (h < 0 \|\| h > 1) error;` | NaN 은 모든 비교가 거짓 → 통과해서 AMAT 가 NaN | `if (!(h >= 0 && h <= 1)) error;` |
| 마지막 계층 히트율을 0.99 로 둠 | 확률 질량 1% 가 사라져 AMAT 가 작아진다 (재귀식은 마지막 계층 미스 비용을 모른다) | 마지막 = 1.0 강제 (`AMAT_E_LAST`) |
| AMAT 로 ISR 마감을 검증 | 평균은 꼬리를 숨긴다. 8.8 평균의 최악은 256 | 최악 = Σ t_k (+ write-back·경합) 또는 TCM |

## 8. 면접 Q&A

<details>
<summary>Q1. 3단 캐시의 AMAT 공식을 쓰고, 로컬/전역 미스율을 설명하세요.</summary>

`AMAT = t1 + m1(t2 + m2(t3 + m3·t_mem))`, m 은 **로컬** 미스율(그 계층에 도착한 접근 중 미스 비율). 전역 미스율은 전체 접근 중 그 계층까지 미스한 비율 = 로컬 미스율의 누적곱. 예: L1 10%, L2 로컬 30% → L2 전역 3%.
꼬리질문: "L2 로컬 미스율이 50% 로 높은데 L2 가 쓸모없는 건가요?" → 아니다. L2 에는 L1 이 걸러낸 **어려운** 접근만 오므로 로컬 미스율은 원래 높게 나온다. 판단은 전역 미스율과 `reach × below` (민감도) 로 한다.

</details>

<details>
<summary>Q2. L1 히트율 1% vs L3 히트율 1% — 어느 쪽 개선이 더 큰가요?</summary>

먼저 "로컬 1%p 인가, 전체 접근의 1% 인가" 를 묻는다. 로컬이면 `−0.01·reach_k·below_k` → reach 가 1 인 L1 이 보통 크다 (A: 0.48 vs 0.06). 전역이면 `−0.01·below_k` → DRAM 직전인 L3 가 크다 (A: 2.0 vs 0.48). 단, 로컬 기준도 워크로드에 따라 바뀐다 (B: L2 가 최대).
꼬리질문: "히트 시간 1 cycle 단축은?" → `∂AMAT/∂t_k = reach_k` 라 L1 이 **항상** 최대. 그래서 L1 은 크기보다 지연에 목숨을 건다.

</details>

<details>
<summary>Q3. 클럭을 2배로 올렸는데 왜 1.66배만 빨라지나요?</summary>

캐시 지연은 cycle 단위로 따라오지만 DRAM 은 ns 단위로 고정이라 cycle 로는 2배가 된다. 예시에서 CPI 가 2.62 → 3.16 으로 늘어 `2 × 2.62/3.16 = 1.66`. 메모리 스톨 비중이 클수록 더 눕는다 (memory wall).
꼬리질문: "그럼 DVFS 에서 메모리 바운드 구간은?" → 클럭을 낮춰도 성능 손실이 클럭 비율보다 작다 (예시에서 0.5 GHz 는 1 GHz 대비 0.557배 — 0.5배보다 덜 느려진다). 그래서 메모리 바운드 구간은 클럭을 낮춰 에너지를 아끼는 정책이 의미가 있다 (실제 이득은 전압·누설 전력에 따라 다르다).

</details>

<details>
<summary>Q4. L1 을 키우면 히트율이 오르는데 왜 무작정 키우지 않나요?</summary>

L1 히트 시간은 load-to-use 경로 그 자체라 모든 load 에 붙는다 (`∂AMAT/∂t_1 = 1`). 크게 만들면 배열이 커져 접근 시간이 늘고, VIPT(Virtually Indexed Physically Tagged) L1 은 인덱스 비트가 페이지 오프셋 안에 있어야 별칭(alias) 문제가 없어서 "크기 ≤ 페이지 크기 × way 수" 제약도 있다. 예시: 4c/90% → 5c/93% 는 이기지만(8.36 < 8.8) 5c/92% 면 진다(8.84) — 손익분기 92.08% (추가 연습 2).
꼬리질문: "Apple 코어는 L1D 가 128 KB 인데?" → 흔히 16 KB 페이지(이 맥 `hw.pagesize: 16384`) 덕분에 큰 VIPT L1 을 만들기 쉽다는 설명이 붙는다 (16 KB × 8 way = 128 KB). 공식 설계 문서로 확인된 것은 아니니 "그렇게 설명된다" 정도로 말한다.

</details>

<details>
<summary>Q5. AMAT 모델이 놓치는 것은? 실제로는 어떻게 재나요?</summary>

(1) 겹침: OoO 코어는 독립 미스 여러 개를 동시에 처리한다 (MLP, 추가 연습 6) → 실제 스톨은 AMAT 보다 작을 수 있다. (2) 프리페치가 미스를 숨긴다. (3) 대역폭/큐잉: 미스가 몰리면 DRAM 지연 자체가 커진다. (4) dirty write-back, TLB 미스, 코어 간 일관성 미스(ARCH-A04, ARCH-M08). 그래서 지연은 **의존 load 사슬(포인터 체이싱)** 로 MLP 를 0 으로 만들어 잰다 (MEM-B10), 히트율은 PMU 카운터로 잰다.
꼬리질문: "포인터 체이싱에서 워킹셋을 키우면 그래프가 어떻게 생기나요?" → L1 크기, L2 크기, (SLC), DRAM 에서 계단처럼 꺾인다. 계단 높이 = 각 계층의 지연, 계단 위치 = 용량.

</details>

<details>
<summary>Q6. (펌웨어) Cortex-M7 에서 ISR 이 쓰는 테이블을 캐시된 SDRAM 에 둘까요, DTCM 에 둘까요?</summary>

DTCM. 평균은 캐시가 비슷하게 좋을 수 있지만 (예시 2.5 cycle) ISR 마감은 **최악** (예시 31 cycle + 라인 fill + write-back + 버스 경합) 으로 본다. TCM 은 캐시 미스가 없어 지연이 결정적이다.
꼬리질문: "TCM 이 모자라면?" → 핫 루틴만 ITCM, 핫 데이터만 DTCM 으로 링커 스크립트에서 배치, 나머지는 캐시 + 필요한 경우 MPU 로 영역 속성(cacheable 여부) 조정. DMA 버퍼를 캐시 영역에 두면 clean/invalidate 가 필요하다 (ARCH-M12).

</details>

<details>
<summary>Q7. (실리콘 검증) 새 칩의 L2 가 스펙대로 동작하는지 어떻게 검증하나요?</summary>

알려진 접근 패턴으로 **예상 히트율과 예상 지연** 을 만들고 비교한다. 예: 워킹셋을 L1 크기 2배, L2 크기 절반으로 잡은 순차/랜덤 패턴 → L1 로컬 히트율 거의 0, L2 로컬 히트율 거의 1 이 예상값. PMU 카운터(접근/리필 수) 로 로컬 히트율을 계산하고 (추가 연습 4), 포인터 체이싱으로 L2 지연 계단을 잰다. 이 계산기에 넣은 AMAT 와 실측 평균 지연이 맞는지가 "모델 vs 실리콘" 상관 검증.
꼬리질문: "카운터 숫자가 이상하면 무엇부터 의심하나요?" → 프리페치/write-back 트래픽이 refill 카운트에 섞였는지, 카운터가 코어별인지 클러스터 공용인지, 측정 구간에 다른 코어 트래픽이 있었는지.

</details>

## 9. 추가 연습

1. **역산**: 구조 A 에서 AMAT 를 6.4 cycle 로 맞추려면 L1 히트율이 얼마여야 하나? 3 cycle 은 가능한가?

<details>
<summary>답</summary>

`6.4 = 4 + (1 − h)·48` → `h = 1 − 2.4/48 = 0.95`. 3 cycle 은 L1 히트 시간(4) 보다 작아 불가능 (−1 반환). 목표가 이미 충족이면 0 반환. 코드: `required_l1_hit`.

</details>

2. **큰 L1 vs 빠른 L1**: L1 을 4c/90% 에서 5c/93% 로 바꾸면? 5c 일 때 손익분기 히트율은?

<details>
<summary>답</summary>

`5 + 0.07·48 = 8.36 < 8.8` → 이득. 손익분기: `5 + (1 − h)·48 = 8.8` → `h = 1 − 3.8/48 ≈ 0.9208`. 92% 면 8.84 로 진다. 히트 시간 1 cycle 이 히트율 약 2.1%p 와 맞먹는다 (`1/48`).

</details>

3. **히트 시간 민감도**: 각 계층의 히트 시간을 1 cycle 줄이면 AMAT 는 얼마나 줄어드나? 왜 L1 이 항상 최대인가?

<details>
<summary>답</summary>

`ΔAMAT = −reach_k` → L1 −1, L2 −0.1, L3 −0.03, DRAM −0.012. reach 는 아래로 갈수록 곱해져 작아지므로 (`reach_0 = 1 ≥ reach_k`) L1 이 항상 최대. 그래서 L1 설계는 지연 우선, LLC 설계는 용량(히트율) 우선이라는 경험칙이 나온다.

</details>

4. **PMU 카운터 → AMAT**: L1 접근 1,000,000, L1 refill 100,000, L2 refill 30,000, L3 refill 12,000. 지연 4/12/40/200 일 때 AMAT 는?

<details>
<summary>답</summary>

계층 k+1 의 접근 = 계층 k 의 refill 로 근사 → 로컬 히트 `1 − 100000/1000000 = 0.9`, `1 − 30000/100000 = 0.7`, `1 − 12000/30000 = 0.6` → AMAT 8.8. 근사의 한계: 프리페치 요청과 write-back 도 아래 계층 접근을 만든다 (Q7 꼬리질문).

</details>

5. **dirty victim**: DRAM 으로 가는 미스의 25% 가 먼저 dirty 라인을 DRAM 에 써야 한다면 (write buffer 없음)?

<details>
<summary>답</summary>

DRAM 비용 `200 × 1.25 = 250` → `4 + 0.1·(12 + 0.3·(40 + 0.4·250)) = 9.4`. write buffer / write-back 버퍼가 이 비용을 배경으로 숨기는 이유.

</details>

6. **MLP**: 독립된 DRAM 미스 4개가 겹친다면 (미스당 체감 DRAM 지연 = 200/4)? 포인터 체이싱은?

<details>
<summary>답</summary>

DRAM 50 → `4 + 0.1·(12 + 0.3·(40 + 0.4·50)) = 7.0`. 포인터 체이싱은 다음 주소가 이번 load 결과에 달려 있어 겹칠 수 없다 → MLP 1 → 8.8. MEM-B10 이 지연 측정에 포인터 체이싱을 쓰는 이유.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
# ...
# 17/17 passed   (exit 0)
```

## 10. 핵심 정리

- `AMAT = t1 + m1·(t2 + m2·(… t_mem))`, m 은 **로컬** 미스율. 합형 `Σ reach_k · t_k` 로 계층별 기여를 본다.
- 전역 미스율(프로파일러 숫자) → 로컬: `m_k = G_k / G_{k−1}`. 안 바꾸면 크게 낙관적 (8.8 → 5.33).
- CPI = base + refs/instr × (AMAT − t_L1). I-fetch(명령당 1) 와 data(명령당 ~0.3) 를 따로 더한다.
- 민감도: `∂AMAT/∂h_k = −reach_k · below_k`, `∂AMAT/∂t_k = reach_k`. AMAT 는 각 변수에 1차식 → +1%p 유한 차분이 정확.
- "1%" 는 정의부터: 로컬이면 보통 L1, 전역이면 보통 LLC 가 병목. 워크로드가 바뀌면 로컬 기준도 바뀐다.
- 캐시는 cycle, DRAM 은 ns → 클럭 2배 ≠ 2배 성능 (예시 1.66배).
- 실시간 펌웨어는 평균이 아니라 최악(Σ t_k + write-back + 경합)으로 본다 → TCM.

## 참고자료

- Hennessy & Patterson, *Computer Architecture: A Quantitative Approach* — Appendix B (Review of Memory Hierarchy: AMAT, multilevel caches, local vs global miss rate), Ch. 2.
- Patterson & Hennessy, *Computer Organization and Design (ARM Edition)* — Ch. 5 (memory hierarchy, CPI with cache stalls).
- Ulrich Drepper, *What Every Programmer Should Know About Memory* — https://people.freebsd.org/~lstewart/articles/cpumemory.pdf
- Arm Architecture Reference Manual (A-profile), PMU common events `L1D_CACHE`, `L1D_CACHE_REFILL`, `L2D_CACHE`, `L2D_CACHE_REFILL` — https://developer.arm.com/documentation/ddi0487/latest
- Arm Cortex-M7 Technical Reference Manual (caches, TCM) — https://developer.arm.com/documentation/ddi0489/latest
- 관련 레슨: MEM-B10 (포인터 체이싱 실측), ARCH-M08 (128 B 일관성 단위), ARCH-M12 (DMA 와 캐시), ARCH-A04 (MESI), ARCH-B12 (버스 wait state — Flash 의 "미스 페널티").
