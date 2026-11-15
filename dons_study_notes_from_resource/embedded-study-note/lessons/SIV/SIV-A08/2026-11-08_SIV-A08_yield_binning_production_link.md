---
id: SIV-A08
time: 120분
updated: 2026-11-08
---
# SIV-A08 생산 연계: 수율·Binning 분석

## 학습 목표

- 웨이퍼 로트 데이터(좌표, Vmin, Fmax, 누설, fail 코드)로 **수율, bin 분포, ASCII wafer map** 을 계산하고, **bin 우선순위** 와 **테스트 순서** 가 왜 다른 개념인지 설명한다.
- 공간 시그니처를 찾는다: **edge ring**(두 비율 z 검정) 과 **cluster**(5×5 창 scan + 이항 꼬리 + Bonferroni). 못 찾는 크기도 측정한다.
- stop-on-first-fail 기대 시간 `Σ t·Π(1−p)` 와 **p/t 내림차순** 규칙을 인접 교환으로 증명하고 brute force 로 검증한다.
- 독립 가정이 깨질 때(상관 fail) **예측 vs 실측** 을 재고, 순서를 greedy·부분집합 DP 로 고르고, **early-stop 이 잃는 것**(마스킹) 과 샘플 datalog 전략, **multi-site 에서 절약이 사라지는 이유** 를 숫자로 말한다.
- excursion 4종이 심어진 로트에서 종류·웨이퍼·테스트·위치를 맞히고, debug 엔지니어가 바로 쓸 리포트를 만든다.

## 1. 문제

검증팀의 산출물(Vmin·Fmax 특성화, PVT 결과, errata)은 양산에서 **테스트 리밋과 bin 표** 가 되어 돌아온다 — 이 연결을 말할 수 있느냐가 Rivian·NVIDIA 급 시니어 validation 면접에서 "랩 사람" 과 "제품을 아는 사람" 을 가른다.

SIV-M01 은 CP / FT / SLT / 시스템 검증의 역할을 나눴다. 이 레슨은 그 CP 단계에서 나온 **다이별 데이터를 어떻게 읽는가** 다. 대상은 M01 의 가상 차량용 센싱 SoC **RX7**, CP 프로그램 **P2.1**, 다이 16×16 mm(2.56 cm²), 300 mm 웨이퍼 한 장에 221 다이, 로트 25장이다. 항목은 8개 — CONT(접촉), IDDQ, PLL, SA(scan stuck-at), TD(scan transition), MBIST, VMIN(기능 최저전압), SERDES 다. **숫자는 전부 합성 대표값/가정** 이고, 실제 공정이나 제품의 값이 아니다. 제품에 따라 정밀 Fmax 판정은 FT/SLT 로 미루고 CP 는 링오실레이터 추정치로 대신하기도 한다. 여기서는 Fmax/Vmin 열이 CP 에 있다고 가정한다.

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">검증 데이터 → 양산 리밋 → 다이 데이터 → 다시 검증으로</text>
  <rect x="10" y="40" width="150" height="74" rx="6" class="f-ok" stroke="currentColor"/>
  <rect x="210" y="40" width="150" height="74" rx="6" class="f-acc" stroke="currentColor"/>
  <rect x="410" y="40" width="150" height="74" rx="6" class="f-acc" stroke="currentColor"/>
  <rect x="610" y="40" width="120" height="74" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="85" y="63" font-weight="bold">검증 랩</text>
    <text x="85" y="83" font-size="12">Vmin shmoo (M07)</text>
    <text x="85" y="101" font-size="12">PVT (M08)</text>
    <text x="285" y="63" font-weight="bold">bin 표 · 테스트 리밋</text>
    <text x="285" y="83" font-size="12">guardband</text>
    <text x="285" y="101" font-size="12">S1/S2/S3, 순서</text>
    <text x="485" y="63" font-weight="bold">CP/FT/SLT 실행</text>
    <text x="485" y="83" font-size="12">모든 다이</text>
    <text x="485" y="101" font-size="12">STDF datalog</text>
    <text x="670" y="63" font-weight="bold">출하</text>
    <text x="670" y="83" font-size="12">수율, ASP</text>
    <text x="670" y="101" font-size="12">DPPM (M01)</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M160 77 L208 77 M360 77 L408 77 M560 77 L608 77"/>
    <path d="M485 114 L485 172"/>
    <path d="M410 200 L170 200 L170 116"/>
    <path d="M85 114 L85 200 L168 200" stroke-dasharray="4 3"/>
  </g>
  <rect x="300" y="172" width="370" height="104" rx="6" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="312" y="194" font-weight="bold">이 레슨: 로트 분석</text>
    <text x="312" y="214" font-size="12">수율 · bin 분포 · wafer map · 공간 시그니처</text>
    <text x="312" y="232" font-size="12">테스트 순서 · full datalog 샘플링</text>
    <text x="312" y="250" font-size="12">excursion 리포트 → debug · 공정 · 검증 재현</text>
    <text x="20" y="232" font-size="12">리밋 재조정,</text>
    <text x="20" y="250" font-size="12">마진 재측정 요청</text>
  </g>
</svg>
```

데이터 파일은 `gen_wafer_lot.py` 가 만든다 (시드 고정, 바이트까지 같게 재생성):

- `lot_L7K12.csv` — 기준 로트. excursion 없음.
- `lot_L7K31.csv` — 같은 공정에 excursion 4종을 심은 로트. 정답은 `lot_L7K31_truth.json` (solution 은 **테스트에서만** 읽는다).
- 각 줄: `wafer,x,y,vmin_mv,fmax_mhz,leak_ma,fails` — `fails` 는 전 항목을 끝까지 돌린 **full datalog** 결과 (`SA+TD`). 파일당 약 172 KB.

### Part A — 수율 · bin · map · 시그니처 · 순서

`yield_summary` · `d0_from_yield`(Poisson 모델) · 우선순위 있는 `assign_bin` / `bin_counts` / `guardband_table` · `wafer_map` · `edge_ring` · `find_clusters` · 독립 가정의 `expected_time` · `ratio_order`.

### Part B — 상관 · 데이터로 순서 고르기 · early-stop 의 대가

`marginals` · `die_time` · `empirical_time`(독립 가정 없는 실측) · `greedy_order` · `dp_order`(선후 제약 포함, 정확 최적) · `first_fail_counts` · `masked_table` · `pick_hash` / `sampled_cost` / `estimate_rates` · `touchdown_time`(multi-site).

### Part C — L7K31 excursion 로트 리포트

`detect_findings` (ring → cluster → 설명된 다이 제거 → 항목별 비율 → MASKED → 공정 이동) · `value_index` · `lot_report`. 심은 결함 4종을 종류·웨이퍼·항목·위치까지 맞히고, 기준 로트에서는 아무것도 내면 안 된다.

## 2. Problem (English)

> You receive per-die CP results for lots of an SoC (`wafer,x,y,vmin_mv,fmax_mhz,leak_ma,fails`; `fails` is a `+`-joined list from `CONT,IDDQ,PLL,SA,TD,MBIST,VMIN,SERDES`, measured with a *full* datalog). Test times are given.
>
> **Part A.** Implement in Python 3.9 stdlib only:
> 1. `yield_summary(dies) -> {"n","good","yield","by_wafer"}`; `d0_from_yield(y, area)` from the Poisson model `Y = exp(-A*D)`.
> 2. `assign_bin(die, table=BIN_TABLE)` — first matching rule wins (conditions are ANDed); `bin_counts`; `guardband_table(table, gb_mv)` tightens every `vmin_max` by `gb_mv`.
> 3. `wafer_map(dies, wafer)` — ASCII map (y up, x right, `1/2/3/x/-`).
> 4. `edge_ring(dies_w, bit, rn_min)` — two-proportion z of the fail rate in the edge zone vs inner. `find_clusters(dies_w, bit)` — scan all 5×5 windows; adjusted p = Binomial tail(k, n, background) × windows (Bonferroni).
> 5. `expected_time(order, p, t)` for stop-on-first-fail under independence and `ratio_order(p, t)` = sort by `p/t` descending. Prove optimality by exchange; verify against brute force for n ≤ 7.
>
> **Part B.** `empirical_time`; `greedy_order` (conditional fail rate given survivors, with precedence constraints); exact `dp_order` over subsets; `masked_table` (true fail rate vs first-fail rate); sampling for full datalogs (`pick_hash`, `sampled_cost`, `estimate_rates`); `touchdown_time(dies, order, t, sites)`. Show predicted-vs-empirical gap on correlated data and that parallel sites erase the saving.
>
> **Part C.** `detect_findings(lot, baseline, order)` must recover 4 planted excursions (edge ring on MBIST, a cluster co-failing SA+TD, a masked MBIST failure hidden behind SA, a parametric Fmax/Vmin/leak shift with no hard-fail signature) with the right wafers, tests and location, and report nothing on a clean lot split against itself. `lot_report(...)` prints the report below.
>
> Constraints: deterministic, no wall-clock in checks, thresholds are named constants. Example: `ratio_order({"A":0.1,"B":0.5},{"A":2,"B":8}) -> ["B","A"]`, `expected_time(["A","B"],...) = 2 + 0.9*8 = 9.2`.

## 3. 시작 코드

<!-- code: starter.py -->

데이터 생성기 (이미 만들어 둔 csv 가 있으므로 다시 돌릴 필요는 없다):

<!-- code: gen_wafer_lot.py -->

```bash
cd lessons/SIV/SIV-A08
python3 -I starter.py; echo exit=$?       # 2/50 PASS, 나머지는 TODO 로 FAIL, exit 1
python3 -I solution.py                    # 50/50
python3 -I solution.py --report           # L7K31 리포트
python3 -I solution.py --map L7K31 6      # wafer map
python3 -I solution.py --tables           # §5 의 측정표
```

`python3 -I` 는 스크립트 디렉터리를 sys.path 에서 뺀다. 파일은 전부 자기완결이고, 데이터 경로는 스크립트 위치 기준이라 어디서 돌려도 된다. 실제 출력 (앞뒤만):

```text
-- Part A: 수율 · bin · map · 공간 시그니처 · p/t --
PASS  loader: 5525 dies, 25 wafers, 221 sites each
FAIL  yield_summary: table-driven (5 dies, 3 good, W1 1/3, W2 2/2)  (TODO: yield_summary)
FAIL  yield_summary: empty input -> yield 0.0  (TODO: yield_summary)
...
FAIL  null wafers: false-alarm rate <= 1% (ring or cluster)  (TODO: find_clusters)
FAIL  limit: strong signatures found >= 90%, weak ones mostly missed  (TODO: find_clusters)
2/50 passed
exit=1
```

TODO 24개: Part A 10개(`yield_summary`, `d0_from_yield`, `assign_bin`, `bin_counts`, `guardband_table`, `wafer_map`, `edge_ring`, `find_clusters`, `expected_time`, `ratio_order`) · Part B 11개(`marginals`, `die_time`, `empirical_time`, `greedy_order`, `dp_order`, `first_fail_counts`, `masked_table`, `pick_hash`, `sampled_cost`, `estimate_rates`, `touchdown_time`) · Part C 3개(`detect_findings`, `value_index`, `lot_report`). 로더, `is_ring`, `binom_tail`, `wafer_rates`, `baseline_model`, `merge_runs`, 테스트는 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — bin 우선순위는 표의 "위에서부터 첫 일치"</summary>

`assign_bin` 은 표를 위에서 아래로 훑어 **첫 번째로 모든 조건이 맞는 rule** 을 고른다. hard-fail rule 을 맨 위에, speed bin 을 그 아래에, 표의 마지막 catch-all(`SB97_SPEED`)은 조건 없음. 조건은 AND 이고 경계는 포함이다(`fmax >= fmax_min`, `leak <= leak_max`, `vmin <= vmin_max`). `leak_gt` 는 초과(`leak > leak_gt`).

</details>

<details>
<summary>힌트 2 — cluster 창의 p-value</summary>

창 안 fail 수 `k`, 창 다이 수 `n`, 창 밖 비율 `bg` 일 때 `P(X >= k)`, `X ~ Binomial(n, bg)`. 창이 221개이므로 `p * 221` (Bonferroni) 이 임계 `alpha` 보다 작을 때만 cluster 다. 하나 찾으면 그 창의 다이를 지우고 다시 찾아야 같은 cluster 를 두 번 세지 않는다.

</details>

<details>
<summary>힌트 3 — p/t 증명은 이웃 둘만 바꿔 본다</summary>

`a` 바로 뒤에 `b` 가 오는 순서와 뒤바뀐 순서를 비교한다. 앞쪽 항목의 생존 확률과 뒤쪽 항목의 시간은 똑같이 나타나므로 지워진다. 남는 차이는 `S·(p_b·t_a − p_a·t_b)`.

</details>

<details>
<summary>힌트 4 — DP 상태는 "이미 끝낸 항목 집합"</summary>

`alive(S)` = 집합 S 의 어떤 항목도 fail 하지 않은 비율. `cost[S] = min_{i∉S, i 가능} alive(S)*t[i] + cost[S∪{i}]`. 다이 수 × 8 비트를 다시 돌 필요 없이 `Counter(d.mask)` 로 유일 마스크만 세면 된다.

</details>

<details>
<summary>힌트 5 — 왜 '설명된 다이'를 먼저 빼나</summary>

ring·cluster 가 큰 웨이퍼의 MBIST 비율은 당연히 튄다. 공간 finding 으로 설명된 다이를 뺀 뒤 남은 다이로 비율 z 를 재면, 같은 원인을 RATE 로 또 보고하지 않는다. 또 `true` 비율은 튀는데 `first`(첫 fail) 비율은 평소대로라면, 그 항목의 fail 이 앞 항목 뒤에 숨은 것이다 — 그게 MASKED 다.

</details>

## 5. 풀이

### Part A — 수율, bin, 지도, 시그니처, p/t

**수율 두 종류.** 여기서 `yield` 는 hard 수율 = fail 코드 0개 다이의 비율이다. 하지만 돈은 bin 으로 번다. `value_index` 가 그 차이를 잡는다 (Part C). 수율 모델 `Y = exp(−A·D)` 는 **모델** 이다. 결함이 다이에 독립적으로 Poisson 분포로 떨어진다는 가정이며, 실제 결함은 군집하고, 수율 손실에는 결함이 아닌 parametric fail 도 섞인다. 기준 로트에서 `D0_hat=0.1200 /cm²` 가 나왔고, 생성기가 쓴 평균 결함 밀도는 `0.1248` 이다 — 15% 허용으로 CHECK 한 이유는 이 두 편향(군집 → 낮게, parametric·오판정 fail → 높게)을 반영한 것이다. 연습 4 에서 군집을 직접 측정한다.

**bin 표는 우선순위 목록이다.** hard fail 이 있으면 SB91~SB95 로, 없으면 누설 → 속도 순서로 본다.

| 우선 | bin | HBIN | 조건 | 의미 |
|---|---|---|---|---|
| 1 | SB91_CONT | 7 | CONT fail | 접촉 불량 → 재프로브 후보 |
| 2 | SB92_LOGIC | 7 | SA / TD / IDDQ | 로직 결함 |
| 3 | SB93_MBIST | 7 | MBIST | SRAM (repair 후보) |
| 4 | SB94_ANALOG | 7 | PLL / SERDES | 아날로그 |
| 5 | SB95_VMIN | 7 | VMIN | 저전압 기능 |
| 6 | SB96_LEAK | 5 | leak > 170 mA | 누설 초과 |
| 7 | S1 | 1 | fmax ≥ 2400, leak ≤ 135, vmin ≤ 725 | 상위 SKU |
| 8 | S2 | 1 | fmax ≥ 2280, leak ≤ 150, vmin ≤ 745 | 중위 |
| 9 | S3 | 2 | fmax ≥ 2080, leak ≤ 170, vmin ≤ 770 | 하위 |
| 10 | SB97_SPEED | 5 | (나머지 전부) | 속도 미달 |

**HBIN 과 SBIN.** 실제 테스터는 다이마다 둘 다 기록한다. STDF V4 의 Part Results Record(PRR)에 `HARD_BIN`, `SOFT_BIN`, `X_COORD`, `Y_COORD` 가 있다. HBIN 은 핸들러가 물리적으로 분류하는 소수의 묶음, SBIN 은 원인 분석용 세부 코드다.

**bin 우선순위 ≠ 테스트 순서.** 다이 하나가 SA 와 MBIST 를 모두 fail 하면 full datalog 에서는 bin 표의 우선순위가 SB92 를 준다. stop-on-first-fail 양산에서는 **먼저 돌린 항목** 이 bin 을 정한다. 그래서 순서를 바꾸면 Pareto 가 움직인다 (Part B).

**속도와 누설은 같이 움직인다.** 빠른 다이(낮은 Vt)는 누설도 크다. 생성기에서 `Fmax ∝ z`, `ln(leak) ∝ z` 로 같은 잠재 변수 `z` 를 쓴다. 그래서 기준 로트에서 `fmax ≥ 2400` 인 clean 다이 121개 중 44개(36%)는 **S1 을 받지 못한다**. 누설이나 Vmin 조건에서 탈락한다. "제일 빠른 칩이 제일 비싼 칩" 이 아닌 이유다.

**Guardband 는 숫자로 값을 매긴다.** 검증(M07)이 "S1 은 725 mV" 라고 정한 리밋에 측정 오차와 수명 여유를 얼마나 더 빼느냐다.

```text
== guardband (L7K12), counts per bin ==
gb_mv     S1     S2     S3    rej  value
    0     77   1531   2413   1504  1.033
    5     77   1530   2412   1506  1.032
   10     76   1523   2415   1511  1.030
   20     52   1420   2465   1588  0.988
   30     26    944   2612   1943  0.829
```

S1 수는 0~10 mV 구간에서 거의 그대로(77→76), 20 mV 부터 급감(52), 30 mV 면 26 — S1 다이의 Vmin 이 리밋(725) 보다 한참 아래(실측: 사분위 692~706 mV, 최대 718 mV)에 몰려 있어서 10 mV 까지는 거의 건드리지 않기 때문이다. value index 는 1.033 → 0.829. 이 표 하나가 "guardband 10 mV 는 거의 공짜, 그 이상은 ASP 를 깎는다" 는 결정 자료다.

**ASCII map.** y 큰 쪽이 위. `1/2/3` 은 speed bin, `x` 는 hard fail, `-` 는 parametric reject. 웨이퍼 6 (edge ring 심음):

```text
            x 3 x x x
        x x x 3 2 2 3 x x
      x x 3 x x 3 2 3 2 1 2
    x x x 2 2 3 3 2 2 x 3 2 2
  3 x 3 2 2 3 3 2 3 2 3 2 3 2 x
  x 1 2 x x 3 2 x x 3 3 x x x x
x 2 2 3 x 2 2 3 3 x 3 2 x 2 2 2 2
x 3 2 x 3 3 2 x 2 2 x 1 3 2 x 3 x
x 2 2 3 2 x x x 3 3 x 3 2 2 2 2 3
x 1 3 2 2 2 3 x 3 3 2 3 2 3 x x x
x 2 2 2 3 x 1 3 2 3 2 3 2 2 3 3 x
  x x 3 3 x 3 2 x 3 2 x x x 2 x
  x x 3 2 3 3 x 3 2 x 2 x 3 3 x
    x x x 2 2 2 2 x x 2 3 x x
      3 x x 3 3 3 2 3 3 2 x
        x x x 3 3 x 3 3 x
            2 x x x x
```

웨이퍼 3 (cluster 심음, 중심 (−4, 3) 은 위쪽 왼편):

```text
            3 2 3 3 2
        x 3 x 2 2 x x 3 x
      2 x 3 2 x 3 x 2 2 x 3
    2 x x x 1 2 x x x 3 x 3 x
  3 x x x x 2 3 1 2 3 2 2 3 x 3
  2 x x x x x 2 x x 3 2 3 x 3 2
3 - x x x x x x 2 x 2 x 2 3 3 x 3
3 2 2 x x x 2 x 2 3 2 2 2 x 3 3 x
x 3 2 3 3 3 2 2 3 x x 3 2 x 3 x x
x x 2 3 x 2 3 3 1 x 3 2 2 2 2 3 x
2 3 3 3 3 2 2 2 3 x x 2 3 3 3 3 3
  3 3 2 3 2 2 3 2 1 3 3 3 3 3 3
  x 2 2 x x 3 2 2 3 2 3 2 x 3 2
    3 1 2 2 x 2 2 3 2 2 2 3 2
      x 2 2 x 2 2 1 x 3 x 2
        3 x 3 2 3 3 3 3 1
            x 2 x 1 3
```

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">세 가지 공간 시그니처 — 각각 다른 원인, 다른 통계</text>
  <circle cx="120" cy="125" r="80" class="f-mut" stroke="currentColor"/>
  <circle cx="120" cy="125" r="64" class="f-ok" stroke="currentColor"/>
  <text x="120" y="130" text-anchor="middle" fill="currentColor" font-size="12">inner</text>
  <text x="120" y="222" text-anchor="middle" fill="currentColor" font-size="12">edge ring: 두 비율 z</text>
  <circle cx="370" cy="125" r="80" class="f-ok" stroke="currentColor"/>
  <ellipse cx="330" cy="95" rx="26" ry="20" class="f-warn" stroke="currentColor"/>
  <rect x="312" y="79" width="36" height="32" fill="none" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="370" y="222" text-anchor="middle" fill="currentColor" font-size="12">cluster: 5×5 창 + 이항 꼬리 + Bonferroni</text>
  <circle cx="620" cy="125" r="80" class="f-ok" stroke="currentColor"/>
  <g class="f-warn" stroke="currentColor">
    <rect x="552" y="75" width="136" height="12"/>
    <rect x="545" y="100" width="150" height="12"/>
    <rect x="540" y="125" width="160" height="12"/>
    <rect x="545" y="150" width="150" height="12"/>
    <rect x="555" y="175" width="130" height="12"/>
  </g>
  <text x="620" y="222" text-anchor="middle" fill="currentColor" font-size="12">reticle repeater: 샘플링과 위상이 맞으면 안 보인다</text>
</svg>
```

**edge ring.** `rn = r/rmax ≥ 0.80` 을 edge 로 두고 fail 비율을 비교한다. 두 비율 z 는 `(p̂_e − p̂_i)/√(p̄(1−p̄)(1/n_e+1/n_i))`, 판정은 `z ≥ 4` **AND** `edge ≥ 2×inner`. z 만 쓰면 다이가 많을 때 사소한 차이도 유의해지고, 비율 조건만 쓰면 inner 가 0.3% 인 희귀 fail 에서 잡음이 걸린다. 정상 웨이퍼에도 에지가 조금 나쁘다(생성기 `λ·(1+0.6·r⁸)`). "정상 수준의 에지 열화" 를 울리지 않으려면 기준이 필요하다.

**cluster.** 창 하나의 fail 수를 이항 꼬리로 보고, 창이 221개이므로 Bonferroni 로 221배를 곱한다. 임계 `1e-5` — 로트당 (웨이퍼 25 × 항목 8 = 200회) × 1e-5 = 0.002 번 정도의 false alarm 을 목표로 잡았다 (보수적 상한). 찾으면 그 창을 지우고 다시 스캔, `k ≥ 6`, `n ≥ 12` 하한으로 희박한 창의 작은 k 를 막는다. **2단계 확인**: 위치가 정해진 뒤 다른 항목은 "그 창에서만" 보므로 다중 비교 보정이 필요 없다 (`p < 1e-3`). SA 는 스캔에서 찾았고 TD 는 이 2단계에서 붙었다 — 16/25 의 TD fail 은 스캔 임계(`p×221 < 1e-5`)를 넘지 못하지만, 위치를 안 다음에는 분명하다.

**p/t 규칙 — 인접 교환 증명.** 어떤 순서에서 `a` 바로 다음이 `b` 라고 하자. 앞쪽 항목의 생존 확률 곱을 `S`, 뒤쪽 남은 기대 시간을 `R` 이라 하면:

```text
E(…a b…) = 앞 + S·[ t_a + (1−p_a)·t_b + (1−p_a)(1−p_b)·R ]
E(…b a…) = 앞 + S·[ t_b + (1−p_b)·t_a + (1−p_a)(1−p_b)·R ]
차이 = E(ab) − E(ba) = S·( p_b·t_a − p_a·t_b )
```

`R` 이 같고 나머지는 사라진다. `p_a/t_a > p_b/t_b` 이면 `차이 < 0`, 즉 a 를 먼저 두는 게 낫다. 임의의 순서에서 이 조건을 어기는 이웃을 찾아 바꾸면 비용이 줄어들거나 같고(같은 비율이면 동률), 바꿀 쌍이 없을 때 정렬이 끝난다 — 그래서 p/t 내림차순이 최적이다. 작업 순서 문제의 Smith 규칙(WSPT, 1956)과 같은 교환 논증이다. `p=0` 인 항목은 ratio 0 이라 마지막, `p=1` 은 앞쪽에 간다. brute force 400 인스턴스(n=2~7)로 CHECK 한다.

p 만으로 정렬하거나 t 만으로 정렬하면 틀린다. 대표 사례가 `{F: 0.02/5ms, S: 0.5/400ms, M: 0.3/30ms}` — p 순(S,M,F)은 416.8 ms, p/t 순(M,F,S)은 307.9 ms 다.

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">MBIST 만 fail 인 다이 — 앞에 있는 만큼 시간이 짧아진다</text>
  <text x="10" y="58" fill="currentColor" font-size="12">program</text>
  <g stroke="currentColor">
    <rect x="70" y="42" width="8" height="24" class="f-mut"/><rect x="78" y="42" width="23" height="24" class="f-mut"/>
    <rect x="101" y="42" width="47" height="24" class="f-mut"/><rect x="148" y="42" width="107" height="24" class="f-mut"/>
    <rect x="255" y="42" width="160" height="24" class="f-mut"/><rect x="415" y="42" width="200" height="24" class="f-warn"/>
    <rect x="615" y="42" width="74" height="24" fill="none" stroke-dasharray="3 3"/><rect x="689" y="42" width="48" height="24" fill="none" stroke-dasharray="3 3"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="201" y="59">SA</text><text x="335" y="59">TD</text><text x="515" y="59">MBIST ✗ stop</text></g>
  <text x="70" y="86" fill="currentColor" font-size="12">소요 12+35+70+160+240+300 = 817 ms (회색 점선 = 안 돌림)</text>
  <text x="10" y="134" fill="currentColor" font-size="12">DP 순서</text>
  <g stroke="currentColor">
    <rect x="70" y="118" width="8" height="24" class="f-mut"/><rect x="78" y="118" width="23" height="24" class="f-mut"/>
    <rect x="101" y="118" width="47" height="24" class="f-mut"/><rect x="148" y="118" width="74" height="24" class="f-mut"/>
    <rect x="222" y="118" width="200" height="24" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="185" y="135">VMIN</text><text x="322" y="135">MBIST ✗ stop</text></g>
  <text x="70" y="162" fill="currentColor" font-size="12">소요 12+35+70+110+300 = 527 ms</text>
  <text x="10" y="186" fill="currentColor" font-size="12">단, pass 다이는 어떤 순서든 1017 ms 전부를 쓴다 → 절약 상한은 fail 다이 몫뿐이다 (Part B)</text>
</svg>
```

### Part B — 상관, 순서, early-stop 이 잃는 것

**독립 가정은 틀렸고, 얼마나 틀렸는지 잴 수 있다.** 기준 로트의 `∏(1−p_i)` = 0.455 — 독립이라면 수율이 45.5% 여야 한다. 실제 hard 수율은 73.6%. 항목들이 같은 결함에 **같이** 깨지기 때문이다 (로직 결함 하나가 SA, TD, IDDQ 를 함께 깬다, 접촉 불량이면 전 항목). 그래서 `Σt·Π(1−p)` 는 시간을 과소평가한다 — 실제로는 더 많은 다이가 살아남아 뒤 항목을 돈다.

```text
== marginals L7K12 ==
CONT=0.031 IDDQ=0.096 PLL=0.057 SA=0.120 TD=0.144 MBIST=0.118 VMIN=0.103 SERDES=0.075
== order table L7K12 (ms per die, single site) ==
full_test=1017 ms  yield=0.7356  bound: pass dies always cost 748.1 ms of the mean
program       pred_indep=  705.1  empirical=  845.2  CONT IDDQ PLL SA TD MBIST VMIN SERDES
ratio(prec)   pred_indep=  674.0  empirical=  838.4  CONT IDDQ PLL VMIN SERDES SA TD MBIST
greedy(cond)  pred_indep=  703.6  empirical=  825.3  CONT IDDQ PLL VMIN MBIST SERDES SA TD
dp(prec)      pred_indep=  703.6  empirical=  825.3  CONT IDDQ PLL VMIN MBIST SERDES SA TD
dp(free)      pred_indep=  709.6  empirical=  822.0  IDDQ VMIN SERDES CONT MBIST TD PLL SA
== same order applied to L7K31 (excursion lot) ==
program=825.8  dp-of-L7K12=805.6  dp-of-L7K31=805.2
```

"예측" 열은 각 순서의 독립 가정 값이다. program 순서의 예측 705.1 vs 실측 845.2 — **140 ms 어긋남**. p/t 로 고른 순서(`ratio`)는 예측 674.0 으로 가장 좋아 보이지만 실측은 838.4 로 평범하다. 독립 가정의 "최적" 은 상관 앞에서 무효다. 통제 실험으로 같은 marginal 을 독립으로 뽑아 재면 예측 705.9, 실측 706.7 (±6 ms, die_time 표본 표준오차 약 1.5 ms 의 4σ) 로 일치한다.

**그래서 순서는 데이터로 고른다.** 두 방법:
- `greedy_order(conditional=True)`: 매 단계 "**살아남은 다이 안에서** 의 fail 비율 / t" 최대를 고른다. 독립의 p 대신 조건부 p 다. 이것은 상관 있는 필터 정렬 문제(pipelined set cover)의 고전적 greedy 로, 단위 비용의 min-sum set cover 에서 4배 근사가 보장된다는 결과가 있다 (Feige–Lovász–Tetali 2004 의 min-sum set cover, Munagala 외 2005 의 pipelined set cover 가 이 계열의 고전 결과다). L7K12 에서는 DP 와 같지만, 일반적으로 greedy ≠ 최적이다 — 무작위 상관 사례 200개 중 3개에서 greedy 가 DP 보다 최대 0.55% 느렸다(CHECK).
- `dp_order`: 항목 8개 → 256 상태, 각 8 전이 — 부분집합 DP 로 **정확 최적**. 항목이 20개면 100만 상태 × 20 — 여전히 되지만, 그 이상이면 greedy 로 간다.

선후 제약 `PRECEDENCE`(접촉이 맨 앞, at-speed 전 PLL 필수)를 지키면서 고른 DP 순서는 **CONT IDDQ PLL VMIN MBIST SERDES SA TD** 로 825.3 ms. 제약 없이는 822.0 ms — 제약의 대가는 3.3 ms 다. 실제 테스트 프로그램에서는 전원 시퀀스, 패턴 로드, 온도 설정 같은 **순서 의존 준비 시간** 까지 고려해야 하고, 그러면 `t_i` 합이 더 이상 순서와 독립이 아니다.

**절약 상한 — 먼저 계산해야 할 숫자.** `E[T] = Y·Σt + (1−Y)·E[T|fail]`. pass 다이는 어떤 순서든 전 항목 1017 ms 를 쓴다. 수율 73.6% 면 평균 845.2 ms 중 **748.1 ms 는 순서와 무관하다**. 순서로 건드릴 수 있는 몫은 97.1 ms 뿐이고, DP 는 그중 19.9 ms(20%)를 줄여 전체 2.35% 를 아낀다. 수율이 90% 면 움직일 몫은 더 작다. 큰 다이의 초기 수율(50% 이하)에서 순서 최적화가 값어치 있다.

**Multi-site 는 절약을 지운다.** ATE 는 x4~x64 로 동시에 프로브한다. touchdown 시간 = 묶인 다이 중 **최댓값** 이다. 한 site 라도 pass 면 그 touchdown 은 전 항목 시간을 다 쓴다.

```text
== multi-site saving, program -> dp(prec), L7K12 ==
x1   program= 845.22 dp= 825.32 ms/die  save= 2.35%
x2   program= 489.40 dp= 484.47 ms/die  save= 1.01%
x4   program= 253.76 dp= 253.19 ms/die  save= 0.22%
x8   program= 127.19 dp= 127.19 ms/die  save= 0.00%
x16  program=  63.69 dp=  63.69 ms/die  save= 0.00%
x32  program=  31.84 dp=  31.84 ms/die  save= 0.00%
```

x8 부터 절약이 정확히 0.00% 다. x8 이상이면 touchdown 마다 pass 다이가 거의 항상 하나 있으므로 max = 전 시간이다. **단일 site 기대 시간 식은 다이당 시간만 재고, 실제 비용은 touchdown 당 시간이다.** 그래서 면접에서 "p/t 로 정렬하면 얼마 줄어드나요?" 의 정답은 숫자에 앞서 "site 몇 개, 수율 얼마?" 를 되묻는 것이다. (x4 이하의 소수 site, 큰 다이의 낮은 수율, 또는 fail 다이를 뒤에서 site-level 로 skip 할 수 있는 테스터 구성에서는 여전히 의미가 있다.)

**Early-stop 이 잃는 것.** stop-on-first-fail 은 다이마다 **첫 fail 하나** 만 남긴다. 나머지 항목의 결과는 없다.

```text
== masked table L7K12, program order ==
CONT    true=0.031 first=0.031 masked=  0.0%
IDDQ    true=0.096 first=0.068 masked= 29.5%
PLL     true=0.057 first=0.026 masked= 54.4%
SA      true=0.120 first=0.036 masked= 70.2%
TD      true=0.144 first=0.022 masked= 84.7%
MBIST   true=0.118 first=0.070 masked= 40.9%
VMIN    true=0.103 first=0.002 masked= 97.9%
SERDES  true=0.075 first=0.010 masked= 86.8%
```

TD 는 실제 fail 이 14.4% 인데 첫 fail 로 보이는 것은 2.2% — **84.7% 가 앞 항목(주로 SA) 뒤에 가려져 있다**. 그 결과:

1. **Pareto 가 순서의 함수가 된다.** 순서를 program → dp 로 바꾸면 총 fail 1461개는 그대로인데 TD 의 첫 fail 은 122 → 56 으로 움직인다(CHECK). 프로그램을 수정한 날 "TD 수율이 좋아졌다" 는 그래프가 나오면 물리가 아니라 **순서** 를 먼저 의심한다.
2. **진단이 어려워진다.** 다이가 어느 패턴에서 깨지는지, 어떤 항목들이 같이 깨지는지는 full datalog 에서만 알 수 있다. Part C 의 MASKED 가 그 사례.
3. **추정량이 편향된다.** 뒤 항목의 fail 률을 앞에서 살아남은 다이로만 재면 `P(fail | 앞 항목 pass)` 를 재게 된다. 앞 항목과의 상관이 양(+)이면 true marginal 보다 낮게 나온다 (TD: 앞에 SA 가 있으면 조건부로 작아진다).

**샘플링 전략.** 전수를 full datalog 로 돌리면 다이당 1017 ms (+171.8 ms). 다이의 일부만 끝까지 돌리면:

```text
== sampling cost (L7K12, program order) ==
1/1    n=5525  mean=1017.0 ms (+171.8 ms)  max|p_hat-p|=0.0000
1/2    n=2756  mean=929.0 ms (+83.8 ms)  max|p_hat-p|=0.0069
1/8    n=677   mean=865.9 ms (+20.7 ms)  max|p_hat-p|=0.0109
1/32   n=173   mean=850.4 ms (+5.2 ms)  max|p_hat-p|=0.0334
1/128  n=41    mean=846.9 ms (+1.7 ms)  max|p_hat-p|=0.0938
```

1/8 샘플(다이 677개)이면 +20.7 ms 로 비용이 전수의 1/8 이고, marginal 은 3σ 안에 들어온다(CHECK; 항목별 이항 표준오차 사용). **샘플 선택의 원칙이 비용보다 중요하다:**

| 방식 | 문제 | 이 레슨에서 측정 |
|---|---|---|
| 해시(`crc32(wafer,x,y,salt) % den`) | 결과를 보기 전에 정해지고, 좌표와 무관하게 흩어진다 | 1/2 샘플 p̂ = 0.201 (true 0.206) |
| 한 줄 건너 / x%k 같은 규칙 격자 | 레티클 repeater 와 위상이 맞으면 전멸하거나 2배로 부푼다 | y 홀수 줄만 → 0.000, y 짝수 줄만 → 0.402 (true 0.206) |
| 웨이퍼당 "처음 K 개 fail 만" 전체 기록 | 프로브 순서(아래쪽부터)에 맞물려 공간적으로 치우친다 | MBIST 가 위쪽에만 있는 웨이퍼에서 share = 0.00 (true 0.74) |
| 결과를 보고 "재미있는" 다이 고르기 | 분모가 사라진다 | (측정 없음 — 원칙) |

레티클 필드 1개가 16×32 mm(다이 2개, 일반 스캐너 최대 필드 26×33 mm 안)일 때, 필드 안 한 위치의 레티클 결함은 **격자에 고정된 주기** 로 반복된다. 샘플링 주기가 공정 주기와 맞물리면 aliasing 이 일어난다는 점에서 SerDes 패턴·샘플링과 같은 문제다.

### Part C — L7K31 excursion

```text
LOT L7K31 product=RX7 program=P2.1 wafers=25 dies=5525
yield 68.9% (baseline 73.6%, -4.6pp)  D0(Poisson model) 0.145/cm2
bins S1=62 S2=1202 S3=2353 reject=1908 value_index=0.89 (baseline 1.03)
FINDINGS 4
F1 CLUSTER W3 tests=SA,TD center=(-4.1,3.0) k=20/25 p=3.5e-11
   action: 입자/스크래치 후보 → 해당 웨이퍼 인라인 결함 맵과 좌표 대조, 물리 FA 후보 다이 선정
F2 EDGE_RING W5..8 tests=MBIST edge=54.2% inner=11.3% z>=6.0
   action: 에지 영역 공정(베벨 etch, 에지 노광)·프로브 카드 에지 사이트 확인 → 같은 chamber/run 웨이퍼 추적
F3 MASKED W13..16 tests=MBIST behind=SA true=29.5% first=7.5% z>=5.4
   action: 먼저 fail 하는 항목 뒤에 원인이 가려졌다 → 해당 웨이퍼 full-datalog 재측정, 순서 임시 변경으로 진짜 Pareto 확인
F4 PARAM_SHIFT W19..24 tests=fmax,vmin,leak delta fmax-115.2 vmin+19.1 leak-21.5 |z|>=5.0
   action: hard 수율로는 안 보이는 공정 이동 → 해당 웨이퍼 그룹의 Vt/PSRO/공정 레시피 로그 확인, speed bin 재예측
ORDER program=825.8ms dp=805.2ms save=2.5%  (x16 touchdown save=0.0%)
```

심은 4가지와 찾아낸 근거:

| 심음 | 진실 (truth.json) | 찾은 것 | 왜 가능했나 |
|---|---|---|---|
| Edge ring | W5..8, MBIST, r≥0.85, p=0.55 | F2 EDGE_RING W5..8 MBIST, edge 54.2% vs inner 11.3% | 웨이퍼별·항목별 z + 비율 조건 |
| Cluster | W3, 중심 (−4,3), r=2.6, SA+TD | F1 CLUSTER W3 center (−4.1, 3.0), SA+TD 병합 | 5×5 스캔 + 위치 고정 후 2단계 확인 |
| Masked | W13..16, 18% 다이가 SA+MBIST 동시 fail | F3 MASKED MBIST behind=SA, true 29.5% / first 7.5% | true 와 first 의 불일치. 실제 양산은 stop-first 라 `first` 만 있다 → **이 진단은 full datalog 샘플이 있어야 가능** |
| Param shift | W19..24, dz=−0.7 → Fmax −119 MHz 주입 | F4 PARAM_SHIFT, Fmax −115.2 MHz, Vmin +19.1, leak −21.5 | 웨이퍼 중앙값의 기준 대비 z, 연속 2장 이상 |

탐지 순서가 중요하다. **① 공간 패턴을 먼저** 본다 — ring/cluster 웨이퍼는 항목 비율이 당연히 튀므로, 먼저 공간으로 설명하고 그 다이를 뺀다. **② 남은 다이로 항목 비율** 을 잰다. **③ true 는 튀는데 first 는 평소대로** → MASKED (이때 `behind` 는 그 다이들이 실제로 먼저 fail 한 항목). SA 쪽의 RATE finding 은 MASKED 가 설명하므로 흡수한다. **④ 공정 이동은 hard fail 없이 온다** — 그래서 hard 수율만 보는 대시보드는 이것을 절대 못 본다. CHECK: W19..24 의 hard 수율은 기준 대비 +0.4pp 로 평평한데, `value_index` 는 1.03 → 0.64, 38% 하락이다. 웨이퍼 21 의 map 은 3 으로 가득하다:

```text
            3 3 3 3 3
        3 - 3 3 3 3 3 3 -
      x x x 3 3 3 3 3 x 3 3
    3 x 3 3 3 x 3 3 3 3 3 3 2
  x x 3 x x 3 - 3 2 3 3 - x x x
  x x 3 3 3 3 3 3 3 2 3 3 3 3 3
3 3 3 3 3 3 3 3 3 - 3 3 x 3 x 3 x
3 x x 3 3 3 3 3 2 3 x x x - 2 - 3
x - - 3 3 x 3 3 3 3 3 3 3 3 3 - -
x 2 x 3 3 x 3 3 x x 3 x x 3 3 2 x
3 3 3 x 3 3 3 3 x 3 3 3 3 x 3 x 3
  x - 3 - 3 3 3 3 x 3 3 3 3 - 3
  x 3 3 x 3 3 3 x 3 x 3 3 3 3 x
    3 3 - x 3 3 - x 3 3 - x -
      - x 3 3 3 x x 3 3 3 3
        x 3 x 3 3 3 3 3 3
            x 3 3 3 -
```

**이 방법이 못 잡는 것 (측정).** 배경 fail 8%, 웨이퍼 200장씩 합성:

```text
== detection power (synthetic bg 8%, 200 wafers each) ==
cluster radius \ peak p   0.30   0.50   0.80
           1.1             0.0%   0.0%   0.0%
           1.5             0.0%   0.5%  13.0%
           2.0             0.0%   3.5%  38.0%
           3.0             3.5%  48.0% 100.0%
           4.0             5.5%  48.0%  99.0%
ring p (edge zone r>=0.85)  detected
                      0.12    0.0%
                      0.15    1.0%
                      0.20    4.5%
                      0.30   46.5%
                      0.45   97.0%
                      0.55  100.0%
null wafers flagged: 0/1000
```

- 1000장의 null 웨이퍼에서 오탐 0회.
- 크기 r=1.1 (반경 1.1 이면 다이 5개) cluster 는 p=0.8 이어도 0% — 창 안에 fail 이 5개를 넘을 수 없어 `k ≥ 6` 하한이 막는다. 스크래치처럼 **선형으로 얇은 패턴** 은 정사각형 창의 약점이다.
- r=3.0, p=0.5 는 48%, r=4.0, p=0.5 도 48% — 창이 5×5 로 고정이라, cluster 가 창보다 커져도 검출력이 늘지 않는다 (창 밖 '배경' 비율이 같이 올라간다). 실무에서는 여러 창 크기를 스캔하거나 경계 추적(연결 성분)을 쓴다.
- ring p=0.20(배경 8%의 2.5배)은 4.5%만 검출 — 약한 ring 은 한 장으로는 안 보이고 **여러 웨이퍼를 합쳐** 야 보인다. 이 레슨의 판정은 장별이다.
- 기준 모델이 단일 로트다. 진짜 시스템은 수개월의 SPC 이력으로 로트 간 분산을 얻는다. 이 레슨의 PARAM z 는 "같은 공정 내 웨이퍼 간 변동"(실측 σ = 15.7 MHz) 이라 로트 간 이동은 전부 excursion 으로 나올 수 있다.
- 다이 하나가 원인과 무관한 fail 을 내면(프로브 흔들림 등) 물리적 원인과 구분하지 못한다 — retest 나 재프로브 데이터가 필요하다.

**검증으로의 피드백.** F2·F1 은 공정/FA 쪽 일이지만 F3 은 프로그램 일(순서, 공유 wrapper 커버리지), F4 는 speed bin 예측과 guardband(M07, M08)를 다시 봐야 하는 일이다. 양산 데이터가 랩의 다음 실험 조건이 되는 고리다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py; echo exit=$?
```

실제 출력:

```text
-- Part A: 수율 · bin · map · 공간 시그니처 · p/t --
PASS  loader: 5525 dies, 25 wafers, 221 sites each
PASS  yield_summary: table-driven (5 dies, 3 good, W1 1/3, W2 2/2)
PASS  yield_summary: empty input -> yield 0.0
PASS  yield_summary: L7K12 good count == dies with empty fail field in CSV  [good=4064]
-- Part B: 상관 · 순서 · early-stop · multi-site --
PASS  independent control data: predicted == empirical within 6 ms  [pred=705.9 emp=706.7]
PASS  correlated L7K12: independence underestimates time by >= 20 ms  [pred=705.1 emp=845.2 gap=140.1 ms]
PASS  L7K12: dp <= greedy/ratio and beats program order  [program=845.2 ratio=838.4 greedy=825.3 dp=825.3]
PASS  limit: conditional greedy is a heuristic -- strictly worse than dp on some instances  [greedy > dp on 3/200 random correlated instances, max gap 0.55%]
PASS  masked_table: TD mostly hidden behind SA (>= 50%)
PASS  reordering moves first-fail Pareto but not the total fail count  [fails=1461, TD 122 -> 56]
PASS  limit: systematic row-skip sampling aliases with a reticle repeater; hash does not  [true=0.206 row-skip odd=0.000 even=0.402 hash=0.201]
PASS  limit: 'first K fails' sampling is spatially biased  [first-8 MBIST share=0.00, real=0.74]
PASS  limit: order saving collapses with parallel sites (max over the touchdown)  [save% x1=2.35 x4=0.22 x16=0.00]
-- Part C: L7K31 excursion --
PASS  finding: 4 findings total, no false alarm on the 11 clean wafers  [['CLUSTERW3', 'EDGE_RINGW5..8', 'MASKEDW13..16', 'PARAM_SHIFTW19..24']]
PASS  finding: EDGE_RING W5..8 on MBIST
PASS  finding: CLUSTER W3 center within 1.5 die, SA+TD merged  [dist=0.11 tests=['SA', 'TD']]
PASS  finding: MASKED W13..16 MBIST behind SA (SA RATE finding absorbed)  [first=0.075 true=0.295]
PASS  finding: no extra RATE findings
PASS  finding: PARAM_SHIFT W19..24 fmax about -119 MHz, one merged finding  [delta=-115.2 MHz wafers=[19, 20, 21, 22, 23, 24] tests=['fmax', 'vmin', 'leak']]
PASS  good model: L7K12 halves against each other -> 0 findings  [findings 0/0]
PASS  lot_report: key lines present
PASS  param-only excursion: hard yield flat (±4pp) but value falls > 25%  [dyield=+0.4pp value 0.64 vs 1.03]
PASS  null wafers: false-alarm rate <= 1% (ring or cluster)  [false alarms 0/300]
PASS  limit: strong signatures found >= 90%, weak ones mostly missed  [cluster weak 0/60 strong 60/60; ring weak 0/60 strong 60/60]
50/50 passed
(위는 발췌: 50 CHECK 중 대표 줄, 전체는 python3 -I solution.py)
exit=0
```

실측 런타임 1.8 초(Apple Silicon, 전 CHECK 포함), `python3 -I -W error -X dev solution.py` 경고 없음. 생성기 재실행 시 파일이 바이트까지 같은 것을 §9 끝에서 확인했다.

## 7. 자주 하는 실수

| # | 실수 | 왜 틀렸나 | 고친 것 |
|---|---|---|---|
| 1 | bin 조건을 부동소수점 `>` 와 `>=` 혼용 | 리밋 경계 다이가 판정기마다 다른 bin | 표 하나에 "포함/초과" 를 명시 (`fmax_min >=`, `leak_gt >`), 테스트로 경계 고정 |
| 2 | 테스트 순서로 bin 을 매김 | 순서를 바꾸면 bin 이 바뀌고 Pareto 가 깨진다 | bin 표 우선순위와 테스트 순서를 분리 (full datalog 있으면 표로, stop-first 면 "첫 fail" 임을 알고 해석) |
| 3 | `p_i` 만으로 정렬 | 비싼 항목이 앞으로 와서 평균 시간 증가 | `p/t` |
| 4 | 독립 가정으로 수율을 예측 | 상관 fail 에서 ∏(1−p) 가 수율을 45.5% 로 (실제 73.6%) 틀리게 잡음 | 실측 `empirical_time`, 조건부 p |
| 5 | 순서 최적화 이득을 다이당 시간으로 보고 | multi-site touchdown 은 max — x16 에서 이득 0.00% | `touchdown_time` 로, site 수와 수율을 먼저 묻는다 |
| 6 | "한 줄 건너" 체계 샘플링 | 레티클 주기와 맞물려 0.000 또는 2배 | 해시로 좌표 분산 |
| 7 | Python `hash()` 로 샘플 선택 | 실행마다 달라 재현 불가 (`PYTHONHASHSEED`) | `zlib.crc32` |
| 8 | 다중 비교 없이 창 하나의 p 값으로 판정 | 창이 221개 × 웨이퍼 25 × 항목 8 이면 우연히 작은 p 가 나온다 | Bonferroni, 위치가 정해진 뒤의 확인은 보정 없이 |
| 9 | 공간 finding 과 비율 finding 을 따로 보고 | 같은 원인을 RATE 로 두 번 보고 | 공간으로 설명된 다이를 빼고 비율을 잰다 |
| 10 | hard 수율만 대시보드에 | parametric 이동을 절대 못 본다 (+0.4pp, 가치는 −38%) | bin mix · 상대 가치 지표를 같이 |

코드로 흔한 함정 하나 (`assign_bin` 의 AND 를 OR 로 착각):

```python
# 틀림: any_fail 이 있는 rule 에서, 다른 조건이 안 맞으면 바로 다음 rule 로 넘어가야 하는데
#       "이 조건만 맞으면 매치" 로 처리하면 S1 조건이 사실상 OR 이 된다
if "fmax_min" in rule and die.fmax >= rule["fmax_min"]:
    return rule["name"]          # leak_max, vmin_max 를 보지도 않음

# 맞음: 어긋나는 조건이 하나라도 있으면 continue, 끝까지 살아남아야 return
if "fmax_min" in rule and die.fmax < rule["fmax_min"]:
    continue
```

## 8. 면접 Q&A

<details>
<summary>Q1. 테스트 순서를 어떻게 정합니까?</summary>

독립 가정과 단일 site 면 `p_i/t_i` 내림차순이고, 인접 교환 `E(ab) − E(ba) = S·(p_b·t_a − p_a·t_b)` 로 증명된다. 하지만 fail 은 독립이 아니다. 기준 로트에서 ∏(1−p)=0.455 인데 실제 수율 0.736, 예측 시간은 140 ms 어긋난다. 그래서 full datalog 로 조건부 p 를 재서 greedy 나 부분집합 DP(이 레슨 항목 8개면 256 상태)로 고른다. 전제로는 접촉 → PLL 같은 선후 제약이 있다.

꼬리질문: "그 최적화로 얼마나 아끼죠?" → 먼저 site 수와 수율을 묻는다. 수율 Y 면 `Y·Σt` 는 건드릴 수 없다. 이 데이터에서 x1 은 2.35%, x4 는 0.22%, x8 이상은 0.00%. touchdown 시간이 site 들의 **max** 이기 때문이다. 항목 비용이 큰 낮은 수율 큰 다이, 소수 site 에서만 값이 있다.

</details>

<details>
<summary>Q2. 왜 bin 우선순위가 테스트 순서와 달라야 합니까?</summary>

테스트 순서는 시간을 최적화하는 변수이고, 그걸 바꿀 때마다 Pareto 가 움직인다(이 데이터에서 TD 첫 fail 122 → 56). bin 은 원인 분류이므로 **안정적** 이어야 한다. full datalog 가 있으면 bin 표의 우선순위로 정하고, stop-first 양산에서는 bin 이 "첫 fail" 임을 알고 해석해야 한다. HBIN 은 물리 분류, SBIN 은 원인 코드로 나눈다.

꼬리질문: "실제 테스터에서 다이 하나에 bin 이 몇 개 기록되나요?" → STDF V4 의 PRR 에 HARD_BIN 과 SOFT_BIN 이 하나씩 있다. 그 외 항목별 결과는 PTR/FTR 로 남긴다.

</details>

<details>
<summary>Q3. Full datalog 를 전수로 돌리지 않는 이유는? 그럼 어떻게 진단합니까?</summary>

시간이다. pass 다이는 이미 전 항목을 다 쓰지만 fail 다이도 끝까지 돌리면 이 데이터에서 다이당 +171.8 ms, 즉 평균 시간의 약 20% 다. 하지만 첫 fail 만 남기면 뒤 항목의 실제 fail 률을 잃는다 (TD 는 84.7% 가 가려짐). 해법은 **결과를 보기 전에 정한** 샘플을 끝까지 돌리는 것이다. 다이 1/8 이면 +20.7 ms 로 marginal 을 3σ 안에 추정한다. excursion 이 의심되는 웨이퍼는 전수 full datalog 로 전환하는 정책을 같이 둔다.

꼬리질문: "왜 해시로 뽑고 한 줄씩 건너뛰지 않죠?" → 레티클 주기와 위상이 맞으면 전멸(0.000)하거나 2배(0.402) 로 나온다. "웨이퍼당 처음 K개 fail" 도 프로브 순서와 맞물려 치우친다. 해시는 좌표에 비의존적이다.

</details>

<details>
<summary>Q4. wafer map 의 cluster 를 어떻게 자동 검출합니까? 임계는 어떻게 정합니까?</summary>

고정 크기 창(5×5)을 모든 다이에 놓고, 창 안 fail 수를 창 밖 비율로 이항 꼬리 p 를 계산해 창 수(221)를 곱한다(Bonferroni). 임계 1e-5 는 로트당(200 웨이퍼·항목 조합) false alarm 이 약 0.002회 이하가 되게 잡은 값이다. 측정: null 웨이퍼 1000장 오탐 0회, r=3·p=0.8 cluster 100% 검출. 한 번 찾은 창의 다이를 지우고 재스캔해 중복을 막는다.

꼬리질문: "그 방법이 못 찾는 패턴은?" → 얇은 스크래치(창에 소수만 걸린다), 창보다 큰 cluster(창 밖 배경을 부풀림 — r=4 에서 p=0.5 는 48% 로 r=3 과 차이 없음), 약한 대비(ring p=0.20 은 장별로는 4.5%). 그래서 다중 창 크기, 연결 성분, 여러 웨이퍼 합산(stacked map)을 같이 쓴다. 그리고 패턴 분류기의 false alarm 비용이 더 큰 쪽(Bonferroni vs FDR)도 제품 단계에 따라 조정한다.

</details>

<details>
<summary>Q5. 수율은 멀쩡한데 매출이 떨어졌습니다. 어떻게 찾습니까?</summary>

hard 수율만 보면 안 보인다. L7K31 의 W19..24 는 hard 수율이 기준 대비 +0.4pp 인데 `value_index` 는 1.03 → 0.64, Fmax 중앙값 −115 MHz. 웨이퍼별 parametric 중앙값(Fmax/Vmin/leak)을 기준 분포 대비 z 로 추적하고, 연속 웨이퍼 run 으로 판정한다(공정 이동은 chamber/run 단위로 온다). 세 parametric 이 같은 방향으로 움직이면 하나의 공정 사건으로 묶는다(여기서는 Fmax↓ Vmin↑ leak↓ = 느린 쪽 Vt 이동).

꼬리질문: "z 의 기준 분산은 어디서 오나요?" → 이 레슨은 단일 기준 로트(웨이퍼 중앙값 간 σ = 15.7 MHz)라서 로트 간 변동이 빠져 있다. 실제로는 수개월의 SPC 이력과 공정 측정(PCM/PSRO)을 함께 봐야 한다. 그렇지 않으면 정상 로트 간 이동을 excursion 으로 오판한다.

</details>

<details>
<summary>Q6. 검증 데이터가 양산 리밋으로 어떻게 넘어갑니까? guardband 는 어떻게 정합니까?</summary>

랩(M07 shmoo, M08 PVT)은 Vmin·Fmax 와 그 온도/전압 민감도를 제공한다. 양산 리밋 = 스펙 동작점 + 측정 불확도 + 온도·전압 코너 차이 + 수명 여유. 이 값이 bin 표의 `vmin_max` 가 된다. guardband 를 키우면 escape(불량 통과)가 줄고 overkill(양품 폐기)이 늘어난다. 이 데이터에서 S1 수는 0~10 mV 는 거의 공짜(77→76)이지만 30 mV 에서 26 — 연습 3 의 측정 잡음 시뮬레이션에서는 gb 4 mV 에서 overkill 4.9%/escape 5.8%, 12 mV 에서 21.3%/0.02% 다.

꼬리질문: "차량용이면?" → AEC-Q001 PAT(robust mean ± 6 robust sigma) 로 분포 이상치를 버리고, 업계 관행으로 GDBN(나쁜 동네의 좋은 다이)을 스크린한다 — 연습 1, 2. "SLT 는요?" → M01 의 역할 분담에 따라 시스템 조건에서만 나는 것을 잡는 단계다.

</details>

<details>
<summary>Q7. Y = exp(−A·D) 로 큰 다이의 수율을 예측해도 됩니까?</summary>

Poisson 모델은 결함이 독립 균일하다는 가정이고, 그 전제에서는 `Y(2A) = Y(A)²` 다. 이 데이터에서 Y(A)=0.7356, Y(A)²=0.5411 인데 실측 2-다이 창 수율은 0.5710 — 결함이 군집해서 큰 다이가 Poisson 예측보다 **덜** 나쁘다 (연습 4). 그래서 음이항 `Y=(1+AD/α)^{−α}`(Stapper) 를 쓰고 α 를 fitting 한다. 어느 쪽이든 모델이고, 설계 시 die size 추정용이지 생산 제어용이 아니다. 또 parametric 손실(Y_sys)은 결함 밀도와 무관하므로 `Y = Y_sys · Y_random` 으로 분리한다.

꼬리질문: "D0 를 재는 다른 방법은?" → 레이아웃의 critical area 와 fab 의 결함 검사 데이터로 예측하거나, 같은 웨이퍼의 다른 크기 다이(window method)로 면적-수율 곡선을 그린다.

</details>

<details>
<summary>Q8. Lot 에서 excursion 을 찾았습니다. 자동 리포트에 무엇을 넣겠습니까?</summary>

종류·웨이퍼 범위·항목·위치(중심/영역)·통계 근거(k/n, z, p)·기준 대비 차이, 그리고 다음 액션. 추측을 사실처럼 쓰지 않는다 — "베벨 etch 원인" 이 아니라 "에지 영역 공정 확인 후보" 로. MASKED 처럼 양산 first-fail 로는 안 보이는 항목은 "full datalog 필요" 를 액션으로 쓴다. 숫자의 근거(`z>=6.0`, `p=3.5e-11`)를 남기면 다음 사람이 임계를 재검토할 수 있다.

꼬리질문: "오탐 비용은 어떻게 봅니까?" → 양산 팀이 잘못된 경보로 공정 hold 를 걸면 큰 비용이다. 그래서 z 조건 **AND** 효과 크기(비율 조건)로 묶고, `L7K12` 를 절반씩 서로 돌려 0건이 나오는지 정상 데이터로 회귀 시험을 건다(CHECK).

</details>

## 9. 추가 연습

### 연습 1 — GDBN (Good Die in Bad Neighborhood)

passing 다이 중 8-이웃에서 fail 이 `min_bad`(4) 개 이상인 좌표를 돌려주는 `gdbn(dies_w, min_bad=4)`.

<details><summary>답</summary>

fail 좌표 집합을 만들고, pass 다이마다 8-이웃을 세면 된다. cluster 근처의 '살아남은' 다이는 잠재 결함이 있을 확률이 높다는 품질 논리다 (차량용 관행). 표준의 고정 규칙이라기보다 고객/제품별 기준으로 운영한다. L7K31 W3 은 21개, 정상 W2 는 10개다.

</details>

### 연습 2 — Static PAT

`pat_limits(values, k=6)`: robust 평균 = 중앙값, robust σ = IQR/1.35, 리밋 = ± kσ. 양품 누설에서 이상치를 찾고, 400 mA 이상치를 심어 잡히는지 본다.

<details><summary>답</summary>

중앙값과 IQR 은 이상치에 영향을 거의 받지 않는다 — 400 mA 를 심어도 UPL 은 201.1 → 201.2 로 거의 그대로다 (AEC-Q001 의 정의에 맞춘 식). 양품 4064개에서는 아무것도 걸리지 않고 LPL 은 −19.2 로 음수 — 누설은 치우친 분포라 대칭 리밋의 아래쪽은 의미가 없다. 실제로 PAT 는 정규성을 가정하지 않는 비대칭 리밋을 허용하고, 로그 변환을 쓰기도 한다.

</details>

### 연습 3 — guardband: overkill vs escape

참 Vmin ~ N(spec−20, 10), 측정 = 참 + N(0, 4), 테스트 리밋 = spec − gb. gb = 0, 4, 8, 12 mV 에서 overkill(양품 중 폐기), escape(불량 중 통과).

<details><summary>답</summary>

측정 잡음(σ=4 mV) 때문에 리밋이 흐려진다. 출력의 표가 곧 답이다 — gb=0 은 escape 24%. 양산 리밋을 스펙 그대로 쓰면 불량의 약 1/4 이 통과한다. gb 12 mV 면 escape 는 사실상 0 이지만 overkill 이 21% 다. 리밋을 정하는 일은 비용(필드 고장)과 수율의 교환이다. SIV-M01 의 Williams–Brown 관점과 이어진다.

</details>

### 연습 4 — Poisson 검사 (2-다이 창)

`window_yields(dies)`: Y1 = 다이 수율, Y2 = 인접 2-다이 창(겹치지 않게 x 짝수에서 x+1)이 둘 다 pass 인 비율. Poisson 이면 Y2 = Y1². 기준 로트에서 확인하고 독립 합성 데이터로 대조한다.

<details><summary>답</summary>

실측 Y2 = 0.5710 > Y1² = 0.5411 — 결함 군집 때문이다 (생성기: 2×2 블록이 Gamma 배율을 공유). 독립 합성 데이터는 Y2 ≈ Y1² (3σ 이내). 작은 면적 차로 그린 ln Y vs 면적 곡선의 휘는 정도가 α 를 준다.

</details>

### 연습 5 — 순서를 샘플 datalog 로 고르기

1/8 해시 샘플로만 `best_order`(CONT 고정, 나머지 전수 탐색)를 구해 전체 로트에서 평가한다. 전체 데이터 최적 대비 손해는?

<details><summary>답</summary>

full=822.4 vs sampled=822.9 ms — 0.06% 차이. 실제 차이를 만드는 것은 몇 개 항목의 큰 조건부 p 이고, 그건 다이 687개면 충분히 안정적이다. 순서 결정은 p 의 정확한 값보다 **상대 순위** 만 필요로 해서 샘플이 잘 먹힌다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py; echo exit=$?
```

```text
-- 연습 1: GDBN --
PASS  center die with 4 failing neighbours flagged, corners are not
PASS  wafer 3 (cluster) has GDBN dies inside the blob, more than a clean wafer  [W3=21 W2=10]
-- 연습 2: Static PAT --
PASS  pat_limits hand value: median 102, IQR 2 -> 102 +- 6*2/1.35
PASS  L7K12 clean dies: nothing outside PAT, and the symmetric LPL is negative (skewed leak -> lower limit is useless)  [LPL=-19.2 UPL=201.1 flagged=0/4064]
PASS  planted 400 mA outlier is outside PAT limits even though median/IQR barely move  [UPL 201.1 -> 201.2]
-- 연습 3: guardband overkill vs escape --
  gb= 0.0 mV  overkill=  1.52%  escape= 24.26%
  gb= 4.0 mV  overkill=  4.93%  escape=  5.79%
  gb= 8.0 mV  overkill= 11.37%  escape=  0.44%
  gb=12.0 mV  overkill= 21.29%  escape=  0.02%
PASS  overkill rises and escape falls monotonically with guardband
PASS  gb = 0 lets a real share of bad dies through (escape > 10%)  [24.3%]
-- 연습 4: 창으로 Poisson 검사 --
PASS  L7K12: Y(2A) > Y(A)^2 by > 1 pp -> defects cluster (Poisson under-predicts big-die yield)  [Y1=0.7356 Y1^2=0.5411 Y2=0.5710 pairs=2550]
PASS  control: independent fails -> Y2 == Y1^2 within 3 sigma of the pair sample  [Y2=0.5407 Y1^2=0.5407 se=0.0059]
-- 연습 5: 샘플로 순서 고르기 --
PASS  order learned from a 1/8 full-datalog sample is within 1% of the full-data optimum on the full lot  [full=822.4 sampled-order=822.9 n=687]
PASS  and both beat the as-written program order
11/11 passed
exit=0
```

데이터 재생성 검증 (`python3 -I gen_wafer_lot.py` 후 동일 sha1):

```text
bbe4c8e160a022283c76998a6b52cdd294f09b42  lot_L7K12.csv
ee75e62b79b8649d63c56e4063a94915a067d7c5  lot_L7K31.csv
8810ef1dee1306ce2d13300453c5a141331d48d2  lot_L7K31_truth.json
```

## 10. 핵심 정리

- 수율은 숫자 하나가 아니다: hard 수율, bin mix, 가치 지표를 같이 본다. **hard 수율만 보면 공정 이동을 놓친다**.
- 테스트 순서 ≠ bin 우선순위. 순서는 시간 변수, 우선순위는 원인 코드다.
- stop-on-first-fail 기대 시간 = `Σ t·Π(1−p)`, 독립이면 `p/t` 내림차순이 최적 (교환 증명: `S·(p_b t_a − p_a t_b)`). 실제 fail 은 상관 → 실측·greedy·DP.
- 절약 상한: `Y·Σt` 는 건드릴 수 없다. **touchdown 은 site 들의 max** — 다site 면 이득이 0 으로 사라진다.
- early-stop 은 datalog 를 잃는다: 앞 항목이 뒤 항목을 가리고(마스킹), Pareto 는 순서의 함수. 결과 보기 전 **해시 샘플** 로 full datalog 를 받는다.
- 공간 시그니처: edge ring = 두 비율 z + 효과 크기, cluster = 창 + 이항 꼬리 + Bonferroni. 못 잡는 크기를 측정해 안다.
- 검증 데이터 → guardband·bin 표 → 양산 → excursion 리포트 → 다시 검증. Vmin 리밋 한 줄이 overkill/escape 교환이다.
- 모델은 모델로 부른다: Poisson `Y=exp(−AD)` 는 군집을 무시한다.

## 참고자료

- Teradyne, *STDF (Standard Test Data Format) Specification V4* — PRR(HARD_BIN, SOFT_BIN, X/Y_COORD), PTR, FTR, WIR/WRR
- AEC-Q001 *Guidelines for Part Average Testing* · AEC-Q002 *Guidelines for Statistical Yield Analysis* (Automotive Electronics Council)
- C. H. Stapper, "Defect density distribution for LSI yield calculations", *IEEE Trans. Electron Devices*, 1973 (음이항 수율) · Murphy, "Cost-size optima of monolithic integrated circuits", *Proc. IEEE*, 1964
- W. E. Smith, "Various optimizers for single-stage production", *Naval Research Logistics Quarterly* 3, 1956 (WSPT, 인접 교환)
- U. Feige, L. Lovász, P. Tetali, "Approximating Min Sum Set Cover", *Algorithmica* 40, 2004 · K. Munagala, S. Babu, R. Motwani, J. Widom, "The Pipelined Set Cover Problem", *ICDT* 2005 (상관 있는 필터 순서)
- T. W. Williams, N. C. Brown, "Defect level as a function of fault coverage", *IEEE Trans. Computers*, 1981
- 관련 레슨: SIV-M01 (CP/FT/SLT, DPPM), SIV-M07 (shmoo, Vmin, guardband), SIV-M08 (PVT), SIV-M10 (triage, 다중 비교), SIV-M13 (커버리지 수렴)
