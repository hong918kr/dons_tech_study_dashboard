---
id: SIV-M08
time: 80분
updated: 2026-11-08
---
# SIV-M08 PVT 코너 매트릭스와 테스트 시간 예산

## 학습 목표

- PVT(Process·Voltage·Temperature) 특성화 매트릭스의 **시간 비용 모델**(챔버 램프 + soak, DUT 교체, 전압 변경, 테스트 시간)을 세우고, 같은 테스트 집합이라도 **실행 순서**에 따라 총 시간이 63시간 대 43시간처럼 크게 달라지는 이유를 숫자로 설명할 수 있다.
- "바꾸는 비용이 큰 축을 가장 바깥 루프로" 원칙과 안쪽 축의 **뱀(snake, reflected Gray) 순서**로 스텝마다 좌표 하나만 바뀌게 하는 스케줄러를 Python으로 구현한다.
- 온도 방문 순서를 완전 탐색(K!)으로 최적화하고, 1-D 램프 모델에서 **동률이 생기는 이유**(닫힌 투어의 대칭)와 시작 온도가 상온이 아닐 때 답이 바뀌는 경우를 다룬다.
- 예산 초과 시 **우선순위 tier + 한계비용(marginal cost)** 으로 축소 계획을 만들고, 비용이 더해지지 않는(non-additive) 이유 — 3분짜리 테스트 하나가 새 온도를 열면 77분이 된다 — 를 설명한다.
- 사람이 스프레드시트로 짠 계획을 **감사(audit)** 해 누락·오타·중복·온도 재방문·과소 견적·예산 초과를 시그니처로 보고한다.

## 1. 문제

첫 실리콘 특성화(characterization)에서 챔버는 가장 비싼 공유 자원이고, "이 매트릭스를 주말 24시간 예약 안에 어떻게 돌리나?"는 Rivian/NVIDIA 검증 리드가 실제로 매주 푸는 문제이자 면접에서 "테스트 시간 예산을 어떻게 짜나?"로 나오는 질문이다.

PVT 매트릭스의 세 축은 이렇다.

| 축 | 이 레슨의 값 | 실물 |
|---|---|---|
| P (공정 코너) | `TT-03`(typical), `SS-07`(slow-slow), `FF-11`(fast-fast) | 팹에 일부러 트랜지스터 파라미터를 코너 쪽으로 틀어 달라고 한 **코너 스플릿 랏(skew lot)** 에서 뽑은 칩. 보드마다 한 개씩 꽂혀 챔버 안 랙에 같이 들어간다 |
| V (코어 전압) | `VNOM` 0.80 V, `VMIN` 0.72 V, `VMAX` 0.88 V (±10%) | PMIC/벤치 전원으로 레일 설정 후 DMM으로 되읽어 확인 |
| T (온도) | −40, 25, 125 ℃ | AEC-Q100 Grade 1 의 동작 주변온도 범위(−40~125 ℃). 챔버 설정값은 주변(ambient) 온도다 (§5 Part C의 Tj 이야기 참고) |

각 셀(칩×전압×온도)마다 테스트 스위트 7개(smoke 3분 … ddr_stress 25분, 합 88분)를 돌리면 테스트 시간만 27 × 88 = **2376분(39.6시간)**. 여기에 오버헤드가 붙는다.

| 바뀌는 것 | 비용 (이 레슨의 모델) | 왜 드나 |
|---|---|---|
| 챔버 온도 a → b | 램프 \|b−a\| / 속도 (가열 5 ℃/min, 냉각 2.5 ℃/min) + 도착 온도 soak (−40: 30분, 25: 10분, 125: 20분) | 챔버 공기만이 아니라 보드·소켓·다이가 따라와야 한다. soak = 다이 온도센서 값이 안정될 때까지 |
| DUT(칩) 교체 | 4분 | 전원 끄기 → 보드 선택(릴레이/JTAG mux) → 전원 → 부팅 → 테스트 이미지 로드 |
| 전압 변경 | 1분 | 레일 설정 → 안정화 → DMM 되읽기 |
| 마지막 복귀 | 램프만 (end_c = 25 ℃) | 차가운 상태로 문을 열면 결로(condensation) — 상온으로 돌려놓고 내린다 |

같은 189개 실행 단위(item)라도 **루프 순서**가 시간을 결정한다. 처음 짜는 스크립트 `for chip: for volt: for temp:` 는 셀마다 온도를 바꾼다 (27번). 온도를 가장 바깥에 두면 2번(+복귀)이다.

```svg
<svg viewBox="0 0 760 345" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="60" y="24" fill="currentColor" font-weight="bold">naive: for chip &gt; volt &gt; temp — 온도 변경 27번, 3828분 (63:48)</text>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="54" y="44">125</text><text x="54" y="104">25</text><text x="54" y="144">−40</text>
    <text x="54" y="204">125</text><text x="54" y="264">25</text><text x="54" y="304">−40</text>
  </g>
  <g stroke="currentColor" stroke-width="0.5" opacity=".35">
    <line x1="60" y1="40" x2="715" y2="40"/><line x1="60" y1="100.6" x2="715" y2="100.6"/><line x1="60" y1="140" x2="715" y2="140"/>
    <line x1="60" y1="200" x2="715" y2="200"/><line x1="60" y1="260.6" x2="715" y2="260.6"/><line x1="60" y1="300" x2="715" y2="300"/>
  </g>
  <polyline fill="none" stroke="currentColor" stroke-width="1.6" points="60.0,100.6 64.4,140.0 85.3,140.0 87.5,100.6 104.2,100.6 107.6,40.0 126.0,40.0 137.2,140.0 157.4,140.0 159.6,100.6 176.3,100.6 179.7,40.0 198.0,40.0 209.3,140.0 229.5,140.0 231.7,100.6 248.4,100.6 251.8,40.0 270.1,40.0 281.3,140.0 302.2,140.0 304.5,100.6 321.1,100.6 324.5,40.0 342.9,40.0 354.1,140.0 374.3,140.0 376.5,100.6 393.2,100.6 396.6,40.0 415.0,40.0 426.2,140.0 446.4,140.0 448.6,100.6 465.3,100.6 468.7,40.0 487.0,40.0 498.3,140.0 519.2,140.0 521.4,100.6 538.0,100.6 541.4,40.0 559.8,40.0 571.0,140.0 591.2,140.0 593.5,100.6 610.1,100.6 613.5,40.0 631.9,40.0 643.1,140.0 663.3,140.0 665.5,100.6 682.2,100.6 685.6,40.0 704.0,40.0 710.8,100.6"/>
  <text x="60" y="184" fill="currentColor" font-weight="bold">최적: 온도 최외곽 (25 → −40 → 125 → 복귀) — 변경 2번, 2572분 (42:52)</text>
  <polyline fill="none" stroke="currentColor" stroke-width="2.4" points="60.0,260.6 197.9,260.6 202.3,300.0 344.4,300.0 350.0,200.0 490.4,200.0 497.2,260.6"/>
  <rect x="60" y="254" width="138" height="13" class="f-ok" opacity=".5"/>
  <rect x="202" y="293" width="142" height="13" class="f-acc" opacity=".5"/>
  <rect x="350" y="194" width="140" height="13" class="f-warn" opacity=".5"/>
  <line x1="304.8" y1="32" x2="304.8" y2="312" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="309" y="318" fill="currentColor" font-size="12">24h 예약 끝 (1440분)</text>
  <g stroke="currentColor"><line x1="60" y1="322" x2="715" y2="322"/></g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="60" y="338">0</text><text x="230" y="338">1000</text><text x="400" y="338">2000</text><text x="570" y="338">3000</text><text x="711" y="338">3828분</text>
  </g>
</svg>
```

가로축은 시간(분, 같은 축척), 세로축은 챔버 온도다. 위는 경사(램프)와 짧은 평지(soak+테스트)가 끝없이 반복되고, 아래는 평지 세 개뿐이다. 그래도 아래(2572분)조차 24시간 예약(점선)에 안 들어간다 → 우선순위로 **줄여야** 한다.

입력은 `pvt_matrix.json` 하나(단일 소스): 챔버 모델, 오버헤드, 칩/전압/온도 목록, 스위트(분, 우선순위), 셀 우선순위 규칙(`cell_prio`, 위에서부터 first-match, `"*"` 와일드카드), 예산 1440분.

### Part A — 비용 모델과 최적 실행 순서

- `transition_min(ch, a, b, soak=True)` — 가열/냉각 속도가 다른 램프 + 도착 온도 soak. `a == b` 면 0.
- `tour_cost(ch, order)` — `start_c` → 온도들 → `end_c`(복귀는 램프만).
- `best_temp_order(ch, temps, max_k=8)` — 순열 완전 탐색, 동률이면 사전순 첫 번째. K > 8 이면 `ValueError`.
- `order_items(cfg, items)` — 온도 최외곽(위 순서), 그 안에서 칩·전압을 **뱀 순서**로. 존재하는 셀만 방문.
- `sequence_cost(cfg, seq)` — 주어진 순서 그대로의 시간 내역(test/temp/dut/volt + 변경 횟수). naive 순서와 최적 순서를 **같은 함수**로 잰다.

### Part B — 어려운 부분: 동률, 시작 온도, 우선순위 규칙, 비가산 예산

- 온도 5개를 뒤섞어 줘도 최적 순서를 찾고, 비용이 같은 순서들 중 사전순 첫 번째를 고른다. 챔버가 85 ℃에 남아 있으면 "상온 먼저"가 더 이상 답이 아니다.
- `cell_prio` — 규칙 **first-match** (TT/VNOM/25 는 `*/VNOM/*` 규칙보다 먼저 나온 P1 규칙을 따른다).
- `reduce_plan(cfg, budget)` — FULL / REDUCED / INFEASIBLE. tier(=max(셀 우선순위, 스위트 우선순위)) 순서로 통째로 넣고, 안 들어가는 tier 는 한계비용이 가장 작은 item 부터 하나씩. 그 아래 tier 는 넣지 않는다(엄격한 우선순위).
- 고정 시드(`random.Random(808)`)로 뽑은 예산 12개에서 불변식(예산 이하, P1 전부, 엄격한 우선순위)을 확인한다.

### Part C — 24시간 예약 시나리오와 사람이 짠 계획 감사

- 금요일 18:00~토요일 18:00 챔버 TC-2 예약(1440분). 축소 계획 + 커버리지 표 + 블록 단위 런 시트 리포트.
- 동료가 스프레드시트('by chip' 시트)로 만든 `handmade_plan.csv`(`gen_handmade_plan.py` 로 결정적 생성, 결함 6개를 심어 둠)를 `audit_plan` 으로 검사: `E_UNKNOWN`, `W_DUP`, `E_MUST_MISSING`, `E_TEMP_REVISIT`, `E_CLAIM_MISMATCH`, `E_OVER_BUDGET`, `W_SUBOPTIMAL` 을 step 번호·셀 이름까지 정확히 보고해야 한다. 최적화기의 계획은 같은 감사를 **무결점**으로 통과해야 한다.

## 2. Problem (English)

> **PVT matrix scheduler with a test-time budget.**
>
> You are given a characterization matrix: chips at process corners (SS/TT/FF), supply voltages, chamber temperatures, and
> test suites (duration in minutes, priority 1–3). Changing chamber temperature costs `|ΔT| / rate` (different heating and
> cooling rates) plus a per-temperature soak; switching the powered DUT costs a fixed time; changing the voltage costs a fixed
> time. The chamber starts at `start_c` (already stable) and must return to `end_c` at the end (ramp only). Python 3 stdlib only.
>
> 1. `sequence_cost(cfg, seq)` — time breakdown of executing `seq` (a list of `(temp, chip, volt, suite)`) in that order.
> 2. `order_items(cfg, items)` — an execution order that minimizes chamber cost: temperature as the outermost loop, temperatures
>    visited in the order minimizing `tour_cost` (brute force over permutations, ties → lexicographically smallest, `ValueError`
>    above 8 temperatures), chips and voltages in boustrophedon (snake) order so consecutive cells differ in exactly one coordinate.
> 3. `reduce_plan(cfg, budget)` — if the full matrix does not fit, keep whole priority tiers in order; for the first tier that does
>    not fit, greedily add the item with the smallest marginal plan cost until nothing fits; drop all lower tiers. Report
>    `FULL` / `REDUCED` / `INFEASIBLE` (tier 1 alone exceeds the budget, with the deficit).
> 4. `audit_plan(cfg, steps, budget, claimed)` — check a human-written plan: unknown values, duplicates, missing tier-1 items,
>    temperature revisits, claimed vs computed time, over budget, and the saving available from reordering.
>
> Example: 3 chips × 3 volts × 3 temps × 7 suites (2376 test minutes). The naive `chip > volt > temp` loop costs 3828 min with
> 27 temperature changes; the optimized order costs 2572 min (`25 → −40 → 125`, 2 changes). With a 1440-min budget the plan keeps all
> 15 P1 and 63 P2 items and 47 of 111 P3 items (1429 min).

## 3. 시작 코드

<!-- code: starter.py -->

입력 매트릭스 (단일 소스):

<!-- code: pvt_matrix.json -->

```bash
cd lessons/SIV/SIV-M08
python3 -I starter.py; echo exit=$?      # TODO 상태: 5/37 passed, exit=1
```

실제 출력 (일부):

```text
-- Part A: cost model + order --
FAIL  transition 25 -> -40 = 65/2.5 + soak 30 = 56
FAIL  transition -40 -> 125 = 165/5 + soak 20 = 53
FAIL  transition 125 -> 25 ramp only = 40
PASS  transition 25 -> 25 = 0 (already stable)
...
PASS  order_items is a permutation of all 189 items
FAIL  first step = TT-03/VNOM/25 smoke (fail fast)
...
PASS  budget 1440: seq == order_items(items), dropped + items = 189
PASS  budget 1440: deterministic (2nd run identical)
...
FAIL  handmade: exactly 9 findings
PASS  optimizer's 24h plan passes its own audit (no findings)
FAIL  report: status line + temps line
5/37 passed
```

> 공짜 PASS 다섯 개에 주의: `return sorted(set(items))` stub 도 "순열이다"를 통과하고, 아무것도 안 하는 `audit_plan` 은 "최적화기 계획이 무결점"을 통과한다. 감사기는 반드시 **결함을 심은 입력이 걸리는지**로 검증해야 한다 (SIV-B01 lint와 같은 교훈).

## 4. 힌트

<details>
<summary>힌트 1 — 루프 순서는 "변경 비용" 순</summary>

온도 변경 ≈ 50~100분 ≫ DUT 교체 4분 ≫ 전압 변경 1분. 가장 비싼 축을 바깥에 두면 그 축의 변경 횟수가 (값의 개수 − 1) + 복귀로 최소가 된다. `sequence_cost` 는 상태 `(온도, 칩, 전압)` 하나를 들고 다니며 **바뀐 좌표마다** 비용을 더하면 끝이다.

</details>

<details>
<summary>힌트 2 — 온도 순서 완전 탐색과 동률</summary>

`itertools.permutations(sorted(temps))` 는 순열을 사전순으로 내놓는다. "엄격히 더 작을 때만(`c < best - EPS`) 갱신"하면 동률 중 사전순 첫 번째가 남는다. 첫 번째 방문 온도가 `start_c` 와 같으면 비용 0 — 상온 테스트를 맨 앞에 두는 것이 왜 공짜인지 보인다.

</details>

<details>
<summary>힌트 3 — 뱀 순서는 flip 플래그 두 개</summary>

칩 목록은 온도 블록에 들어갈 때마다, 전압 목록은 (온도, 칩) 블록에 들어갈 때마다 방향을 뒤집는다. 플래그는 블록 안에서 초기화하지 말고 **전역으로** 계속 토글해야 블록 경계에서도 좌표가 하나만 바뀐다 (25 ℃ 마지막 FF-11/VMAX → −40 ℃ 첫 FF-11/VMAX).

</details>

<details>
<summary>힌트 4 — 한계비용은 다시 계획해서 잰다</summary>

`plan_cost(sel + [c]) - plan_cost(sel)`. 공식으로 계산하려 하지 말 것 — 새 온도인지, 새 (온도,칩)인지, 뱀 방향이 바뀌는지에 따라 달라진다. 후보 100개 × 반복 수십 번이라도 Python으로 1초 안쪽이다. 동률은 `full_items` 순서 인덱스로 깬다 (결정성).

</details>

<details>
<summary>힌트 5 — 감사기의 "실제 시간"</summary>

사람의 순서를 **그대로** `sequence_cost` 에 넣는다 (재정렬 금지). 실행 불가한 행(E_UNKNOWN)은 빼고, 중복은 두 번 다 과금한다. 그다음 같은 유일 item 집합을 `order_items` 로 돌린 값과 비교하면 재정렬로 아낄 수 있는 시간(W_SUBOPTIMAL)이 나온다.

</details>

## 5. 풀이

### Part A — 비용 모델, 루프 순서, 뱀 순서

**전이 비용.** 25 → −40 은 냉각 65 ℃ / 2.5 = 26분 + soak 30 = **56**. −40 → 125 는 가열 165 / 5 = 33 + 20 = **53**. 125 → 25 복귀는 100 / 2.5 = **40**(soak 없음). 그래서

| 온도 순서 | 계산 | tour_cost |
|---|---|---|
| 25 → −40 → 125 → (25) | 0 + 56 + 53 + 40 | **149** |
| 25 → 125 → −40 → (25) | 0 + 40 + 96 + 13 | **149** |
| −40 → 25 → 125 → (25) | 56 + 23 + 40 + 40 | 159 |
| 125 → −40 → 25 → (25) | 40 + 96 + 23 + 0 | 159 |

상온을 **맨 앞**에 두면 이미 25 ℃에 안정된 챔버를 그대로 쓰므로 soak 10분이 사라진다. 실무적으로도 맞다: TT/VNOM/25 smoke가 첫 스텝이면 보드·케이블·이미지 문제를 **30분 냉각 soak 전에** 발견한다(fail fast). 그래서 `volts` 순서도 `VNOM` 을 먼저 적었다.

**루프 순서.** 같은 189 item을 두 순서로 쟀다 (`sequence_cost` 하나로):

| 순서 | 온도 변경 | 온도 비용 | DUT 교체 | 전압 변경 | 총 시간 |
|---|---|---|---|---|---|
| naive `chip > volt > temp` | 27 | 1431 | 3 (12분) | 9 (9분) | **3828분 (63:48)** |
| 최적 `temp > chip > volt` (뱀) | 2 | 149 | 7 (28분) | 19 (19분) | **2572분 (42:52)** |

naive의 1431 = 첫 사이클 25→−40→25→125 (56+23+40) + 이후 8 사이클 × (125→−40 96 + 23 + 40) + 마지막 복귀 40 = 119 + 1272 + 40. 최적 순서는 DUT 교체와 전압 변경이 오히려 늘었지만(값싼 축을 희생) 온도에서 1282분을 번다. 이것이 "변경 비용이 큰 축을 바깥으로"의 정량적 근거다. 추가 연습 2에서 6가지 중첩을 모두 재 보면 순위가 `temp>chip>volt` 2572 < `temp>volt>chip` 2608 < `chip>temp>volt` 2784 < … < `volt>chip>temp` 3468 로 나온다.

**뱀(boustrophedon) 순서.** 안쪽 두 축을 매번 같은 방향으로 돌면 블록 경계에서 좌표가 둘 이상 바뀐다 (TT/VMAX → SS/VNOM: 칩+전압). 방향을 번갈아 뒤집으면 연속한 셀은 항상 **좌표 하나만** 다르다 — mixed-radix reflected Gray code 와 같은 구조다. 테스트 `snake: every cell boundary changes exactly 1 coordinate` 가 이 성질을 직접 확인한다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="sivm08-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <g fill="currentColor" font-weight="bold" text-anchor="middle">
    <text x="160" y="28">① 25 ℃ (시작 온도, soak 없음)</text>
    <text x="400" y="28">② −40 ℃ (칩 역순)</text>
    <text x="640" y="28">③ 125 ℃</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="62" y="99">TT-03</text><text x="62" y="149">SS-07</text><text x="62" y="199">FF-11</text>
  </g>
  <g fill="none" stroke="currentColor" stroke-width="2" marker-end="url(#sivm08-arr)">
    <polyline points="95,95 160,95 225,95 225,145 160,145 95,145 95,195 160,195 220,195"/>
    <polyline points="465,195 400,195 335,195 335,145 400,145 465,145 465,95 400,95 340,95"/>
    <polyline points="575,95 640,95 705,95 705,145 640,145 575,145 575,195 640,195 700,195"/>
  </g>
  <g fill="none" stroke="currentColor" stroke-dasharray="5 4" marker-end="url(#sivm08-arr)">
    <path d="M225,200 Q345,240 460,200"/>
    <path d="M335,90 Q455,20 570,90"/>
  </g>
  <g stroke="currentColor">
    <circle cx="95" cy="95" r="9" class="f-acc"/><circle cx="160" cy="95" r="7" class="f-mut"/><circle cx="225" cy="95" r="7" class="f-mut"/>
    <circle cx="95" cy="145" r="7" class="f-mut"/><circle cx="160" cy="145" r="7" class="f-mut"/><circle cx="225" cy="145" r="7" class="f-mut"/>
    <circle cx="95" cy="195" r="7" class="f-mut"/><circle cx="160" cy="195" r="7" class="f-mut"/><circle cx="225" cy="195" r="7" class="f-mut"/>
    <circle cx="335" cy="95" r="7" class="f-mut"/><circle cx="400" cy="95" r="7" class="f-mut"/><circle cx="465" cy="95" r="7" class="f-mut"/>
    <circle cx="335" cy="145" r="7" class="f-mut"/><circle cx="400" cy="145" r="7" class="f-mut"/><circle cx="465" cy="145" r="7" class="f-mut"/>
    <circle cx="335" cy="195" r="7" class="f-mut"/><circle cx="400" cy="195" r="7" class="f-mut"/><circle cx="465" cy="195" r="7" class="f-mut"/>
    <circle cx="575" cy="95" r="7" class="f-mut"/><circle cx="640" cy="95" r="7" class="f-mut"/><circle cx="705" cy="95" r="7" class="f-mut"/>
    <circle cx="575" cy="145" r="7" class="f-mut"/><circle cx="640" cy="145" r="7" class="f-mut"/><circle cx="705" cy="145" r="7" class="f-mut"/>
    <circle cx="575" cy="195" r="7" class="f-mut"/><circle cx="640" cy="195" r="7" class="f-mut"/><circle cx="705" cy="195" r="7" class="f-mut"/>
  </g>
  <text x="80" y="76" fill="currentColor" font-size="12">start</text>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="95" y="250">VNOM</text><text x="160" y="250">VMIN</text><text x="225" y="250">VMAX</text>
    <text x="335" y="250">VNOM</text><text x="400" y="250">VMIN</text><text x="465" y="250">VMAX</text>
    <text x="575" y="250">VNOM</text><text x="640" y="250">VMIN</text><text x="705" y="250">VMAX</text>
  </g>
  <text x="10" y="280" fill="currentColor" font-size="12">실선 = 칩 또는 전압 하나만 변경 · 점선 = 온도만 변경 (칩·전압 그대로 이어짐)</text>
</svg>
```

DUT 교체 7번 = 25 ℃에서 3(첫 전원 인가 포함) + −40 ℃에서 2 + 125 ℃에서 2. 전압 변경 19번 = 9블록 × 2 + 첫 설정 1. 블록 경계에서 0이 되는 것이 뱀 순서의 효과다 (선형 순서였다면 블록마다 VMAX→VNOM 되감기가 추가된다).

### Part B — 동률·시작 온도·우선순위 규칙·비가산 예산

**왜 동률이 생기나 (1-D 닫힌 투어).** 시작=끝=25 ℃이고 모든 온도를 한 번씩 방문하는 최소 투어는 [−40, 125] 의 모든 구간을 **정확히 한 번 올라가고 한 번 내려간다**. 램프 비용이 "구간 길이/방향별 속도"의 합이면 가열 시간 합 = 165/5, 냉각 시간 합 = 165/2.5 로 순서와 무관하고, soak은 방문 온도마다 한 번이므로 역시 같다. 그래서 cold-first 와 hot-first 가 149로 비긴다. 5개 온도 `[125, −40, 85, 0, 25]` 에서도

| 순서 | 비용 |
|---|---|
| 25 → −40 → 0 → 85 → 125 → (25) | 56 + 28 + 37 + 28 + 40 = **189** ← 사전순 첫 번째 |
| 25 → 0 → −40 → 85 → 125 → (25) | 30 + 46 + 45 + 28 + 40 = 189 |
| 25 → 85 → 125 → 0 → −40 → (25) | 32 + 28 + 70 + 46 + 13 = 189 |

중간 온도(0, 85)는 "가는 길"이나 "오는 길" 어디서 방문해도 같은 구간을 지난다. 동률 처리 규칙을 **명시**해 두지 않으면 dict/set 순서에 따라 매 실행 계획이 바뀌고, 어젯밤 계획과 오늘 계획의 diff가 의미를 잃는다. 이 레슨은 "사전순 첫 번째 = 차가운 쪽 먼저"로 고정했고, 이는 마지막 구간이 뜨거운 쪽에서 상온으로 **식히며** 끝나 문을 열 때 결로 위험이 없다는 실무 선호와도 맞는다.

**시작 온도가 상온이 아니면.** 앞 작업이 챔버를 85 ℃에 두고 끝났다면: `[125, −40, 25]` = 28 + 96 + 23 + 0 = **147**, `[25, −40, 125]` = 34 + 56 + 53 + 40 = 183. 가장 가까운 끝(125)으로 먼저 갔다가 한 번에 반대쪽으로 가는 sweep이 이긴다. "항상 상온 먼저" 같은 규칙을 하드코딩하면 이 경우 36분을 버린다 — 그래서 규칙 대신 비용 함수 + 탐색을 쓴다.

**K!의 한계.** 온도 8개면 40320 순열 × 8 — 괜찮다. 9개부터 `ValueError` 를 던지고 Held-Karp DP(O(2^K·K²), 추가 연습 1)로 넘어가라고 말한다. 조용히 느려지는 것보다 명시적으로 거부하는 편이 야간 회귀에서 낫다.

**셀 우선순위는 first-match.** `TT/VNOM/25` 는 첫 규칙(P1, baseline)과 마지막 규칙 `*/VNOM/*`(P2) 둘 다 맞는다. first-match 로 P1. 이 규칙표에 담긴 엔지니어링 판단:

| 셀 | P | 왜 |
|---|---|---|
| TT/VNOM/25 | 1 | 기준선. 다른 모든 셀의 비교 대상 |
| SS/VMIN/−40 | 1 | 미세 공정·저전압에서는 저온에서 오히려 느려진다(**temperature inversion**: 문턱전압 상승 효과가 이동도 개선을 이김). 그래서 "SS는 뜨거울 때만 최악"이라고 가정하면 안 된다 |
| SS/VMIN/125 | 1 | 고전적인 setup 최악 (느린 공정 + 저전압 + 고온) |
| FF/VMAX/−40 | 1 | 가장 빠른 조건 → hold 마진 최악 후보 |
| FF/VMAX/125 | 1 | 누설 전류·전력·열 최악 (자체 발열로 Tj가 더 올라감) |
| */VNOM/* | 2 | 공칭 레일에서의 코너·온도 경향 |
| 나머지 | 3 | |

item tier = max(셀 P, 스위트 P) — P1 셀에서 돌리는 P3 스위트(serdes_ber)는 P3다. 결과 tier 크기 15 / 63 / 111.

**비가산(non-additive) 비용.** 계획에 `TT/VNOM/25 smoke` 하나만 있으면 3 + DUT 4 + 전압 1 = **8분**. 여기에 `SS/VMIN/−40 smoke`(3분짜리)를 더하면 3 + 3 + 온도(56 + 복귀 13) + DUT 8 + 전압 2 = **85분** — 한계비용 77분. 반대로 이미 열린 온도·칩·전압 위의 3분 테스트는 3분이다. 그래서 "스위트 시간 합 ≤ 예산"으로 고르는 것은 틀리고, 축소는 반드시 **다시 계획한 비용**으로 판단해야 한다.

**축소 알고리즘과 그 결과.**

```text
plan_cost(전체) ≤ 예산?                → FULL
tier 1 만으로 > 예산?                  → INFEASIBLE (deficit = 초과분), tier 1 계획은 그대로 보여 줌
tier k 통째로 들어감?                   → 넣고 다음 tier
아니면: 남은 후보 중 한계비용 최소(동률=인덱스) 하나씩, 가장 싼 것도 안 들어가면 멈춤 → REDUCED
                                        (k보다 아래 tier 는 전부 drop: 엄격한 우선순위)
```

| 예산 | 결과 | 계획 |
|---|---|---|
| 5000 | FULL | 2572분 |
| 1440 | REDUCED | P1 15/15, P2 63/63 (여기까지 1134분), P3 47/111 → **1429분**, slack 11 |
| 300 | INFEASIBLE | P1만으로 344분 → deficit **44분** |

P3 47개의 내역(실행해서 센 값): smoke 14, pcie_link 14, idle_power 14, mem_march 5, **serdes_ber 0**. "한계비용 최소 우선"은 개수를 최대화하므로 짧은 스위트를 넓게 뿌리고, 15분짜리 serdes_ber는 한 번도 안 들어간다. 커버리지 관점에서 "모든 P3 셀을 smoke로라도 한 번 밟는다"는 장점이 있지만 "SerDes 온도 특성 데이터 0건"은 구멍이다 — 면접 꼬리질문 단골(Q4). 해법은 가치 가중(value/marginal), 스위트별 최소 1회 보장, 또는 SerDes처럼 중요한 스위트의 P를 올리는 것.

**결정적 랜덤 검사.** `random.Random(808)` 로 344~2571 사이 예산 12개를 뽑아 매번 (a) 상태 REDUCED, (b) 총 시간 ≤ 예산, (c) P1 15개 전부, (d) drop된 item이 있는 가장 작은 tier 번호보다 큰 번호의 tier에서는 선택된 item이 하나도 없음(엄격한 우선순위)을 확인한다. 타이밍 숫자는 CHECK에 넣지 않는다 — 전체 `solution.py` 실행은 이 Mac(Python 3.9.6)에서 `time` 으로 약 4.0초였다.

### Part C — 24시간 예약 리포트와 스프레드시트 계획 감사

`python3 -I solution.py --report` 의 실제 출력 (런 시트는 일부):

```text
== SOC-A0 PVT characterization (corner-split lot, chamber TC-2) ==
matrix : 3 chips x 3 volts x 3 temps x 7 suites = 189 items, test-only 2376.0 min
naive  (chip>volt>temp loops): total 3828.0 min (63:48) = test 2376.0 + temp 1431.0 (27 changes) + dut 12.0 (3) + volt 9.0 (9)
full   (optimized order)     : total 2572.0 min (42:52) = test 2376.0 + temp 149.0 (2 changes) + dut 28.0 (7) + volt 19.0 (19)
budget : 1440.0 min (24:00)  -> status=REDUCED
plan   : total 1429.0 min (23:49) = test 1233.0 + temp 149.0 (2 changes) + dut 28.0 (7) + volt 19.0 (19)  slack 11.0
temps  : start 25 -> 25 -> -40 -> 125 -> end 25
tiers  : P1 15/15  P2 63/63  P3 47/111
coverage (suites selected / 7) per cell:
                   -40      25     125
  TT-03 VNOM    6/7 P2  6/7 P1  6/7 P2
  TT-03 VMIN    4/7 P3  4/7 P3  3/7 P3
  TT-03 VMAX    4/7 P3  3/7 P3  3/7 P3
  SS-07 VNOM    6/7 P2  6/7 P2  6/7 P2
  SS-07 VMIN    6/7 P1  3/7 P3  6/7 P1
  SS-07 VMAX    4/7 P3  3/7 P3  3/7 P3
  FF-11 VNOM    6/7 P2  6/7 P2  6/7 P2
  FF-11 VMIN    4/7 P3  3/7 P3  3/7 P3
  FF-11 VMAX    6/7 P1  3/7 P3  6/7 P1
run sheet (block = one temp/chip/volt; start offset h:mm):
  +  0:00  78.0 min    25C TT-03  VNOM  smoke,mem_march,cpu_stress,pcie_link,ddr_stress,idle_power
  +  1:18  29.0 min    25C TT-03  VMIN  smoke,mem_march,pcie_link,idle_power
  ...
  +  5:29  17.0 min    25C FF-11  VMAX  smoke,pcie_link,idle_power
  +  5:46 129.0 min   -40C FF-11  VMAX  smoke,mem_march,cpu_stress,pcie_link,ddr_stress,idle_power
  ...
  + 14:53 126.0 min   125C TT-03  VNOM  smoke,mem_march,cpu_stress,pcie_link,ddr_stress,idle_power
  ...
  + 21:55  74.0 min   125C FF-11  VMAX  smoke,mem_march,cpu_stress,pcie_link,ddr_stress,idle_power
audit of handmade_plan.csv: total 1494.0 min (24:54) = test 982.0 + temp 487.0 (10 changes) + dut 16.0 (4) + volt 9.0 (9)
  E_UNKNOWN step=37 suite=ddr_strss
  W_DUP step=83 item=TT-03/VNOM/25:smoke first=7
  E_MUST_MISSING cell=FF-11/VMAX/-40 suites=cpu_stress
  E_TEMP_REVISIT temp=-40 visits=3
  E_TEMP_REVISIT temp=25 visits=4
  E_TEMP_REVISIT temp=125 visits=3
  E_CLAIM_MISMATCH claimed=1019.0 computed=1494.0 diff=+475.0
  E_OVER_BUDGET total=1494.0 budget=1440.0 over=54.0
  W_SUBOPTIMAL total=1494.0 optimal=1164.0 saving=330.0
```

런 시트의 `+5:46 129.0 min −40C FF-11 VMAX` 블록 = 온도 변경 56 + DUT 0 + 전압 0 + 테스트 73. 칩·전압이 25 ℃ 마지막 블록과 같아서(뱀 순서) 온도만 바뀌었다.

**스프레드시트 계획에 심은 결함과 감사 결과.** 동료의 계획(83행)은 "중요 13셀 × serdes 뺀 6스위트, 여유 있으니 P1 셀엔 serdes도"이고 시트가 칩별(`for chip: for temp: for volt`)이다. `gen_handmade_plan.py` 가 결정적으로 만든다 (재생성 sha256 동일 확인, §6).

| 심은 결함 | 감사 시그니처 | 디버그/랩 엔지니어가 할 일 |
|---|---|---|
| 칩이 바깥 루프 → 온도 재방문 | `E_TEMP_REVISIT -40 x3, 25 x4, 125 x3`, `W_SUBOPTIMAL saving=330.0` | 같은 item을 온도 최외곽으로 재정렬 (1494 → 1164분) |
| 합계에 램프/soak/전압 변경 누락 | `E_CLAIM_MISMATCH claimed=1019.0 computed=1494.0 diff=+475.0` | 견적은 항상 비용 모델로 계산, 손 합계 금지 |
| 그 결과 예약 초과 | `E_OVER_BUDGET over=54.0` | 토요일 18:54까지 돌아 다음 팀 예약 침범 — 재정렬하면 276분 여유 |
| P1 셀의 P1 스위트 누락 | `E_MUST_MISSING cell=FF-11/VMAX/-40 suites=cpu_stress` | hold 최악 코너의 CPU 스트레스 — 반드시 추가 |
| 스위트 이름 오타 | `E_UNKNOWN step=37 suite=ddr_strss` | 시퀀서(SIV-M12)가 그 행에서 죽거나 조용히 건너뜀 → 이름은 매트릭스에서 드롭다운으로 |
| 재확인용 중복 행 | `W_DUP step=83 ... first=7` | 의도라면 주석, 아니면 삭제. 125 ℃에서 끝난 뒤 25 ℃ soak·DUT 교체·전압 변경까지 붙어 +18분 (실측: 이 행만 빼면 1476분) |

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">총 시간 비교 (분) — 점선 = 24h 예약 1440분</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="53">naive 189 items</text><text x="10" y="83">최적 순서 189 items</text>
    <text x="10" y="113">P1+P2 (78 items)</text><text x="10" y="143">축소 계획 REDUCED</text>
    <text x="10" y="173">P1만 (15 items)</text><text x="10" y="203">스프레드시트 계획</text>
    <text x="10" y="233">같은 행 재정렬</text>
  </g>
  <g stroke="currentColor">
    <rect x="200" y="40" width="535.9" height="18" class="f-warn"/>
    <rect x="200" y="70" width="360.1" height="18" class="f-warn"/>
    <rect x="200" y="100" width="158.8" height="18" class="f-ok"/>
    <rect x="200" y="130" width="200.1" height="18" class="f-acc"/>
    <rect x="200" y="160" width="48.2" height="18" class="f-ok"/>
    <rect x="200" y="190" width="209.2" height="18" class="f-warn"/>
    <rect x="200" y="220" width="163.0" height="18" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="728" y="54" text-anchor="end">3828</text>
    <text x="566" y="84">2572</text><text x="365" y="114">1134</text><text x="406" y="144">1429</text>
    <text x="254" y="174">344</text><text x="415" y="204">1494</text><text x="369" y="234">1164</text>
  </g>
  <line x1="401.6" y1="30" x2="401.6" y2="245" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="10" y="262" fill="currentColor" font-size="12">경고색 = 예산 초과 · 강조색 = 최적화기의 24h 계획 (slack 11분) · 같은 83행도 재정렬만으로 −330분</text>
</svg>
```

**Tj vs Ta — 125 ℃는 무엇의 125인가.** 챔버는 주변 공기 온도(Ta)를 맞춘다. 다이 온도 Tj ≈ Ta + θJA·P 이므로 θJA 10 ℃/W 보드에서 cpu_stress로 5 W를 쓰면 Tj는 약 175 ℃ — 절대 최대 정격을 넘을 수 있다. 그래서 실제 특성화 계획은 셀을 "Tj = 125"처럼 **접합 온도**로 정의하고, 다이 온도센서를 읽어 챔버 설정값을 낮추거나(전력이 큰 스위트일수록), 소켓 위 열 헤드로 다이를 직접 제어한다. soak 판정도 "챔버 설정 도달 + N분"이 아니라 "다이 센서 변화 < 1 ℃/5분" 같은 조건이 정확하다(1차 열 시정수 τ라면 5τ에서 99.3%). 이 레슨의 soak 분 값은 그 결과를 표로 뭉쳐 둔 것이고, 실측으로 교정해야 한다(Q5).

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py; echo exit=$?            # 37/37 passed, exit=0
python3 -I solution.py --report                 # + §5 Part C 리포트
python3 -I -W error -X dev solution.py          # 경고 없음 (exit 0)
python3 -I gen_handmade_plan.py --out /tmp/SIV-M08_hm.csv
shasum -a 256 handmade_plan.csv /tmp/SIV-M08_hm.csv
```

실제 출력 (마지막 부분과 sha 확인):

```text
-- Part C: 24h chamber booking + audit of handmade plan --
PASS  handmade: E_UNKNOWN suite=ddr_strss
PASS  handmade: W_DUP TT-03/VNOM/25:smoke
PASS  handmade: E_MUST_MISSING FF-11/VMAX/-40 cpu_stress
PASS  handmade: E_TEMP_REVISIT -40 x3, 25 x4 (dup at end), 125 x3
PASS  handmade: E_CLAIM_MISMATCH (spreadsheet forgot ramp/soak)
PASS  handmade: E_OVER_BUDGET
PASS  handmade: W_SUBOPTIMAL saving > 300 min
PASS  handmade: exactly 9 findings
PASS  optimizer's 24h plan passes its own audit (no findings)
PASS  report: status line + temps line
37/37 passed

wrote /tmp/SIV-M08_hm.csv (83 steps, claimed 1019 min)
9e071cc477236e2e6fd0c673b9f3a5aa2438b7071a313abd3d6b4ba0e343ce2f  handmade_plan.csv
9e071cc477236e2e6fd0c673b9f3a5aa2438b7071a313abd3d6b4ba0e343ce2f  /tmp/SIV-M08_hm.csv
```

결함 픽스처 생성기:

<!-- code: gen_handmade_plan.py -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 견적 = 스위트 시간 합 (+ DUT 교체) | 램프·soak이 한 번에 50~100분, 순서에 따라 수백 분 차이 | `sequence_cost(cfg, seq)` 로 **순서째** 계산 |
| `for chip: for volt: for temp:` 스크립트 | 셀마다 온도 변경 27번, +1256분 | 비싼 축을 바깥으로: `for temp: for chip: for volt:` |
| 안쪽 루프를 매번 같은 방향 | 블록 경계마다 칩+전압이 같이 바뀜 | flip 플래그를 **전역으로** 토글 (뱀 순서) |
| flip 플래그를 온도 블록마다 초기화 | 온도 경계에서 칩이 바뀌어 DUT 교체 1회씩 추가 | 플래그를 루프 밖에 두고 블록에 들어갈 때마다 토글 |
| 냉각·가열 같은 속도로 모델링 | 냉각은 보통 더 느림. −40 이 끼면 견적이 크게 빗나감 | `heat_c_per_min` / `cool_c_per_min` 분리 |
| 첫 온도도 soak 과금 / 복귀도 soak 과금 | 이미 안정된 시작 온도는 공짜, 복귀는 하역용이라 램프만 | `a == b → 0`, `soak=False` |
| 동률을 set/dict 순서에 맡김 | 실행마다 계획이 달라져 diff·재현 불가 | 정렬된 순열 + "엄격히 작을 때만 갱신" |
| "상온 먼저"를 규칙으로 하드코딩 | 챔버가 85 ℃에서 시작하면 36분 손해 | 비용 함수 + 탐색 |
| 우선순위 규칙을 "가장 낮은 P" 나 "마지막 매치"로 | TT/VNOM/25 가 P2 로 떨어져 기준선이 축소 대상이 됨 | 위에서부터 first-match, 문서에 명시 |
| 축소를 "스위트 분 합 ≤ 남은 예산"으로 | 새 온도/칩을 여는 비용(77분) 무시 → 예산 초과 | 한계비용 = `plan_cost(sel+[c]) - plan_cost(sel)` |
| P1 이 안 들어가도 조용히 일부만 | 반드시 돌아야 할 셀이 빠진 계획이 "정상"처럼 보임 | `INFEASIBLE` + deficit 보고 → 예약 연장/챔버 추가 결정 |
| 감사기가 사람 계획을 재정렬해서 잼 | 실제로 돌아갈 시간이 아님 | 순서 그대로 `sequence_cost`, 최적값은 별도 `W_SUBOPTIMAL` |

## 8. 면접 Q&A

<details>
<summary>Q1. PVT 매트릭스가 챔버 시간 안에 안 들어갑니다. 어떻게 접근하나요?</summary>

① 비용 모델부터: 테스트 시간 외에 온도 전이(램프+soak), DUT 교체, 전압 변경을 실측값으로 넣는다. ② 순서 최적화: 온도를 최외곽, 안쪽은 뱀 순서 — 이것만으로 이 예에서 63h → 43h. ③ 그래도 넘치면 셀·스위트 우선순위로 축소: 기준선(TT/nom/room)과 최악 코너(SS/Vmin/저온·고온, FF/Vmax/저온·고온)를 P1으로. ④ P1도 안 들어가면 계획을 깎지 말고 리소스(예약 연장, 챔버 추가, 칩 추가 — 추가 연습 3: 챔버 2대면 1762분)를 요청한다.
꼬리질문: "온도 셋 중 하나를 통째로 빼면?" → 한 온도를 빼면 그 온도의 모든 셀이 사라지지만 투어 비용도 크게 준다. 비가산 비용이라 이런 큰 덩어리 결정은 한계비용 표를 보고 사람이 내린다. 예를 들어 125 ℃를 빼면 temperature inversion 때문에 −40만 남기는 게 위험한지(저전압 공정이면 −40이 오히려 느린 쪽) 같은 공정 지식이 필요하다.

</details>

<details>
<summary>Q2. 왜 온도가 가장 바깥 루프인가요? 항상 그런가요?</summary>

바꾸는 비용이 가장 큰 축이기 때문이다(여기선 50~100분 vs DUT 4분 vs 전압 1분). 항상은 아니다: 소켓 하나에 열 헤드(ThermoStream류 forced-air 장치)를 얹어 쓰면 온도 전이는 수 분 이내로 짧고, 칩 교체는 사람이 소켓을 열고 바꾸는 15분짜리 작업이 된다. 추가 연습 2에서 그 설정으로 6가지 중첩을 모두 재면 `chip > temp > volt` 가 2497.5분으로 `temp > chip > volt`(2522.5분)를 이긴다. 원칙은 "온도 먼저"가 아니라 **"변경 비용 내림차순으로 바깥부터"**다.
꼬리질문: "양산 ATE에서는?" → 온도가 바뀌면 보통 별도 삽입(insertion, 예: 상온 final test와 고온 final test)으로 나눈다. 핸들러 soak이 비싸서 같은 원칙이 공정 흐름 수준에서 적용된 것이다 (단계별 역할은 SIV-M01).

</details>

<details>
<summary>Q3. 온도 방문 순서는 어떻게 정하나요? 동률은요?</summary>

온도 수 K가 작으면 순열 완전 탐색(K ≤ 8), 크면 Held-Karp DP(O(2^K·K²)). 1-D 선형 램프에서 시작=끝이면 최소 투어는 각 구간을 한 번 오르고 한 번 내리므로 방향만 다른 sweep들이 동률이다. 그래서 동률 규칙을 명시해야 결정적이다: 이 레슨은 사전순(차가운 쪽 먼저 → 뜨거운 쪽에서 식히며 종료 → 결로 위험 없음).
꼬리질문: "챔버가 85 ℃에서 시작하면?" → 가까운 끝(125)부터 가는 `[125, −40, 25]` 가 147분, 상온 먼저는 183분. 규칙이 아니라 비용 함수로 풀어야 하는 이유.

</details>

<details>
<summary>Q4. 예산 축소에서 한계비용 최소 우선(greedy)의 약점은?</summary>

개수를 최대화하지 가치를 최대화하지 않는다. 실제로 이 레슨의 24h 계획에서 P3 47개는 smoke/pcie/idle_power/mem_march이고 serdes_ber는 0개 — SerDes 온도 특성 데이터가 통째로 빈다. 대안: (a) 가치/한계비용 비율, (b) 스위트별·셀별 최소 1회 보장(coverage 하한, SIV-M13), (c) 중요한 스위트의 P 조정. 근본적으로는 예산 제약 하의 집합 선택이라 배낭 문제류(NP-hard)이고, greedy는 설명 가능하고 결정적이라 랩에서 선호된다.
꼬리질문: "엄격한 우선순위 대신 남는 시간에 P3를 채우면?" → P2 일부가 빠졌는데 P3가 돌면 리뷰에서 설명이 안 된다. 정책을 문서화하고, 필요하면 "P2 drop 목록 + 그 시간에 들어갈 수 있었던 P3" 를 리포트에 함께 보여 사람이 결정하게 한다.

</details>

<details>
<summary>Q5. soak 시간은 어떻게 정하나요? "챔버가 125 ℃ 표시하면 시작"은요?</summary>

챔버 공기 온도가 도달해도 보드·소켓·패키지·다이는 열 시정수만큼 늦다. 다이 온도센서(또는 열전대)를 읽어 변화율이 임계 이하(예: 5분간 1 ℃ 미만)가 될 때를 soak 끝으로 정의하고, 각 온도·보드 조합에서 한 번 실측해 표(`soak_min`)로 만든다. 1차 시스템이면 5τ에서 99.3%. 추가로 Ta가 아니라 Tj를 지정한 셀이면 전력이 큰 스위트(cpu_stress)에서 자체 발열로 Tj가 오르므로 챔버 설정을 낮추거나 열 헤드로 다이를 직접 제어한다.
꼬리질문: "−40 ℃에서 끝내고 바로 문을 열면?" → 결로·성에가 보드에 생겨 누설·단락 위험. 상온 복귀(+ 건조 공기 퍼지) 후 하역 — 그래서 모델의 `end_c` 복귀가 있다.

</details>

<details>
<summary>Q6. SS/Vmin/고온만 보면 setup 최악은 다 본 것 아닌가요?</summary>

아니다. 미세 공정에서 공급 전압이 낮으면 temperature inversion으로 저온이 더 느릴 수 있어 SS/Vmin/저온도 P1으로 둬야 한다. 또 FF/Vmax/저온은 가장 빠른 조건이라 hold 마진 최악 후보, FF/Vmax/고온은 누설·전력·열 최악이다. 결국 "최악"은 실패 메커니즘(setup/hold/누설/IR drop/메모리 Vmin)마다 다른 코너라서 매트릭스의 P1이 다섯 셀이 된다. 각 셀 안에서 Vmin을 정밀하게 찾는 건 shmoo/이진 탐색(SIV-M07)이다.

</details>

<details>
<summary>Q7. 사람이 짠 계획을 리뷰할 때 무엇을 자동으로 확인하나요?</summary>

`audit_plan` 이 하는 일: 매트릭스에 없는 이름(오타), 중복, P1 누락(셀 이름까지), 온도 재방문 횟수, 손 견적과 모델 견적의 차이, 예산 초과, 같은 행을 재정렬했을 때의 절약분. 메시지는 `E_MUST_MISSING cell=FF-11/VMAX/-40 suites=cpu_stress` 처럼 고칠 사람이 바로 찾아갈 수 있어야 하고(SIV-B14 리포트 원칙), 감사기 자체는 결함을 심은 픽스처로 회귀 테스트한다.
꼬리질문: "E_ 와 W_ 를 나눈 기준은?" → 실행하면 데이터가 틀리거나 예약을 침범하는 것은 E(차단), 낭비지만 결과는 맞는 것은 W(중복, 비최적). CI에서 E는 계획 승인을 막고 W는 리뷰 코멘트로.

</details>

## 9. 추가 연습

### 연습 1 — Held-Karp DP로 온도 순서

`best_temp_order_dp(ch, temps)`: `dp[mask][j]` = 시작 온도에서 출발해 `mask` 의 온도를 모두 방문하고 `j` 에서 끝나는 최소 비용. 끝에서 `end_c` 복귀 비용을 더해 최소 `j` 를 고르고 parent로 복원한다. 랜덤 챔버 200개(시드 808, K=1..7, 시작/끝/속도/soak 모두 랜덤)에서 완전 탐색과 **비용**이 같아야 하고, K=10(완전 탐색 3,628,800 순열)도 즉시 끝나야 한다.

<details>
<summary>답 보기</summary>

O(2^K·K²) — K=10 이면 약 10만 번 전이 계산. 순서 자체는 동률 처리 방식이 달라 완전 탐색과 다를 수 있으므로 **비용만** 비교한다(동률이 흔하다는 Part B의 결론). 실측으로 K=10, 시작 25 ℃ 에서 DP는 `[122, 104, …, −40]` (뜨거운 쪽부터) 를 냈다 — 25 ℃가 집합에 없으니 "상온 먼저" 이득도 없고, 두 방향 sweep이 동률인 경우다. 온도가 정말 많아지면(온도 sweep 특성화) 1-D 구조를 이용해 "한쪽 끝까지 갔다가 반대쪽" 후보만 보면 되지만, 측정된 비대칭 전이 행렬(챔버 특성이 구간마다 다름)이 들어오면 DP가 안전하다.

</details>

### 연습 2 — 루프 중첩 순서 6가지 비교

`nesting_costs(cfg)`: `(temp, chip, volt)` 의 6가지 순열 각각을 바깥→안 중첩으로 쓰고, 일반화한 뱀 순서 `snake(levels)` (레벨 d 의 목록은 들어갈 때마다 방향 반전)로 순서를 만든 뒤 `sequence_cost` 로 잰다. 챔버 설정에서 최적이 `temp > chip > volt` 2572분임을 확인하고, 열 헤드 설정(가열·냉각 20 ℃/min, soak 3분, 소켓 수동 교체 15분)에서는 `chip > temp > volt` 가 이기는지 확인한다.

<details>
<summary>답 보기</summary>

실측 순위 — 챔버: `temp>chip>volt` 2572 · `temp>volt>chip` 2608 · `chip>temp>volt` 2784 · `volt>temp>chip` 2832 · `chip>volt>temp` 3456 · `volt>chip>temp` 3468. 열 헤드: `chip>temp>volt` 2497.5 · `temp>chip>volt` 2522.5 · `chip>volt>temp` 2590.5 · `volt>chip>temp` 2646.5 · `temp>volt>chip` 2690.5 · `volt>temp>chip` 2721.5. 두 경우 모두 전압(1분)이 가장 안쪽이다. 열 헤드에서 차이가 25분뿐인 것도 정보다: 비용이 비슷해지면 순서보다 **사람 개입 횟수**(소켓 교체는 야간에 못 함) 같은 비용 외 제약이 결정한다.

</details>

### 연습 3 — 챔버 2대에 칩 나누기

`split_two_chambers(cfg)`: 칩을 챔버 A/B에 나누는 모든 경우(칩 0은 A 고정으로 대칭 제거)에서 각 챔버의 `plan_cost` 를 계산하고 makespan(늦게 끝나는 쪽)이 최소인 분할을 고른다.

<details>
<summary>답 보기</summary>

결과 makespan **1762분** (`['TT-03', 'FF-11']` / `['SS-07']`; 2+1 분할은 칩이 대칭이라 모두 동률). 칩 2개 챔버 = 1584 + 149 + DUT 16 + 전압 13 = 1762, 칩 1개 챔버 = 792 + 149 + 4 + 7 = 952. 단일 챔버 2572 대비 31% 단축이지만 두 배 빠르진 않다 — 투어 비용 149분이 챔버마다 **각각** 들고, 칩 3개는 2개로 균등 분할이 안 된다. 1762분도 24h를 넘으므로 "챔버 2대 + P3 일부 축소" 또는 "예약 30시간" 중에서 고르는 협상 자료가 된다.

</details>

### 연습 4 — 실제 시각이 붙은 런 시트

`wall_clock_sheet(cfg, seq, start, booking_end)`: 블록(온도,칩,전압)마다 시작 시각(`Fri 18:00`)과 블록 시간을 출력하고, 예약 종료 이후에 시작하는 블록엔 `OVER` 를 붙인다. 전체 계획(축소 안 함)을 금요일 2026-11-13 18:00에 시작하면 언제 끝나는가?

<details>
<summary>답 보기</summary>

`Sun 12:52` (2572분 후). 블록 27개 중 `Sat 18:55 −40C TT-03 VMIN` 부터 끝까지 11개가 OVER — 즉 축소 없이 시작하면 −40 ℃ 마지막 칩과 125 ℃ 전체가 예약 밖이다. 아래가 실제 출력이다. 운영 팁: 런 시트에 시각을 붙이면 "토요일 새벽 칩 교체가 수동이면 사람이 있어야 한다" 같은 제약도 바로 보인다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py; echo exit=$?            # 11/11 passed, exit=0
```

실제 출력:

```text
-- 연습 1: Held-Karp DP vs brute force --
PASS  200 random chambers (seed 808, K=1..7): DP cost == brute-force cost
PASS  matrix temps: DP order cost 149 like brute force
PASS  K=10 (brute force would be 3,628,800 orders): DP returns a permutation <= both sweeps
-- 연습 2: loop nesting order --
PASS  chamber: best nesting = temp > chip > volt, 2572 min
PASS  thermal head + manual socket swap (15 min): best nesting = chip > temp > volt
PASS  volt is innermost in the best order for both setups
-- 연습 3: two chambers --
PASS  two chambers: makespan 1762 (vs 2572 single)
PASS  two chambers: split is 2 chips + 1 chip
-- 연습 4: wall-clock run sheet --
PASS  full plan: 27 blocks, first 'Fri 18:00 25C TT-03 VNOM'
PASS  full plan ends Sun 12:52 (2572 min after Fri 18:00)
PASS  blocks starting after the 24h booking are marked OVER (and only those)
      Fri 18:00    25C TT-03  VNOM    93.0 min
      Fri 19:33    25C TT-03  VMIN    89.0 min
      Fri 21:02    25C TT-03  VMAX    89.0 min
      Fri 22:31    25C SS-07  VMAX    92.0 min
      Sat 00:03    25C SS-07  VMIN    89.0 min
      Sat 01:32    25C SS-07  VNOM    89.0 min
      Sat 03:01    25C FF-11  VNOM    92.0 min
      Sat 04:33    25C FF-11  VMIN    89.0 min
      Sat 06:02    25C FF-11  VMAX    89.0 min
      Sat 07:31   -40C FF-11  VMAX   144.0 min
      Sat 09:55   -40C FF-11  VMIN    89.0 min
      Sat 11:24   -40C FF-11  VNOM    89.0 min
      Sat 12:53   -40C SS-07  VNOM    92.0 min
      Sat 14:25   -40C SS-07  VMIN    89.0 min
      Sat 15:54   -40C SS-07  VMAX    89.0 min
      Sat 17:23   -40C TT-03  VMAX    92.0 min
      Sat 18:55   -40C TT-03  VMIN    89.0 min OVER
      Sat 20:24   -40C TT-03  VNOM    89.0 min OVER
      Sat 21:53   125C TT-03  VNOM   141.0 min OVER
      Sun 00:14   125C TT-03  VMIN    89.0 min OVER
      Sun 01:43   125C TT-03  VMAX    89.0 min OVER
      Sun 03:12   125C SS-07  VMAX    92.0 min OVER
      Sun 04:44   125C SS-07  VMIN    89.0 min OVER
      Sun 06:13   125C SS-07  VNOM    89.0 min OVER
      Sun 07:42   125C FF-11  VNOM    92.0 min OVER
      Sun 09:14   125C FF-11  VMIN    89.0 min OVER
      Sun 10:43   125C FF-11  VMAX    89.0 min OVER
11/11 passed
```

## 10. 핵심 정리

- PVT 시간 = 테스트 시간 + **전이 비용**(온도 램프+soak ≫ DUT 교체 ≫ 전압 변경). 견적은 손 합계가 아니라 순서째 시뮬레이션(`sequence_cost`).
- 루프 순서 = 변경 비용 내림차순으로 바깥부터. 챔버면 `temp > chip > volt`(63h → 43h), 열 헤드+수동 소켓이면 `chip` 이 바깥.
- 안쪽은 **뱀(reflected Gray) 순서** — flip 플래그를 전역으로 토글해 스텝마다 좌표 하나만 바뀌게.
- 온도 순서는 비용 함수 + 탐색(K ≤ 8 순열, 이상은 Held-Karp). 1-D 닫힌 투어는 방향만 다른 sweep이 동률 → 동률 규칙을 명시. 시작 온도가 상온이 아니면 답이 바뀐다. 상온 먼저 = soak 공짜 + fail fast.
- 우선순위: 셀 규칙 first-match, item tier = max(셀 P, 스위트 P). 기준선 + 실패 메커니즘별 최악 코너(SS/Vmin 저온·고온 — temperature inversion, FF/Vmax 저온·고온)가 P1.
- 비용은 **비가산**: 새 온도를 여는 3분 테스트 = 77분. 축소는 tier 통째 → 한계비용 최소 greedy → 엄격한 우선순위, P1도 안 되면 `INFEASIBLE` + deficit 로 리소스 요청.
- 사람 계획은 감사: 오타·중복·P1 누락·온도 재방문·과소 견적·예산 초과·재정렬 절약분. 감사기는 결함 심은 픽스처로 검증.
- 챔버 125 ℃ ≠ Tj 125 ℃ (Tj ≈ Ta + θJA·P). soak은 다이 센서 안정으로 정의하고 실측 교정, 차가운 챔버는 상온 복귀 후 하역.

## 참고자료

- AEC-Q100 (Automotive Electronics Council) — 집적회로 스트레스 시험 인증, 동작 온도 등급(Grade 0: −40~150 ℃, Grade 1: −40~125 ℃)
- JEDEC JESD51 시리즈 — 패키지 열저항(θJA 등) 측정 조건, Tj ≈ Ta + θJA·P 의 정의 배경
- P. Mishra, F. Farahmandi (eds.), *Post-Silicon Validation and Debug*, Springer 2019 — 포스트실리콘 특성화·검증 흐름
- M. Held, R. M. Karp, "A Dynamic Programming Approach to Sequencing Problems", *J. SIAM* 10(1), 1962 — 연습 1의 DP
- Temperature inversion: 저전압 미세 공정에서 지연이 저온에서 커지는 현상 — 공정 PDK 문서/정적 타이밍 분석(STA) 코너 정의에서 다룬다
- [Python `itertools.permutations`](https://docs.python.org/3/library/itertools.html#itertools.permutations) (사전순 생성 보장 — 입력이 정렬돼 있을 때) · [`random.Random`](https://docs.python.org/3/library/random.html) · [`csv`](https://docs.python.org/3/library/csv.html)
- 관련 레슨: SIV-B12(테스트 플랜), SIV-B14(실패 리포트), SIV-M01(ATE/SLT/시스템 단계), SIV-M07(shmoo·Vmin 탐색), SIV-M12(Python 시퀀서·mock 챔버), SIV-M13(functional coverage)
