---
id: HWLAB-B02
time: 70분
updated: 2026-11-08
---
# HWLAB-B02 데이터시트 로직 레벨: 노이즈 마진과 전압 도메인 호환성

## 학습 목표

- 데이터시트의 VIH/VIL(입력 임계), VOH/VOL@IOH/IOL(출력 보장값과 시험 전류), 절대최대정격(absolute maximum rating)으로 드라이버→리시버 쌍의 **DC 노이즈 마진**(NMH = VOH − VIH, NML = VIL − VOL)을 계산할 수 있다.
- 공칭(nominal) 계산이 왜 거짓말을 하는지 설명하고, **레일 코너 스윕**(같은 레일은 함께, 다른 레일은 반대 방향으로)으로 worst-case를 구할 수 있다.
- 1.8V↔3.3V 직결에서 "로직 레벨은 맞는데 칩이 상하는" 경우(ESD 클램프 주입 전류)와 "아예 High 인식이 안 되는" 경우를 숫자로 가를 수 있다.
- 전원 꺼진 칩으로의 **back-powering**(ESD 다이오드를 통한 phantom 레일)을 모델링하고 Ioff 버퍼·시퀀싱·펌웨어로 고치는 방법을 말할 수 있다.
- open-drain FET 레벨시프터(AN10441 구조)의 양방향 Low 레벨을 계산하고 게이트 레일 조건(Vgs(th))을 검사할 수 있다.

## 1. 문제

첫 보드 브링업에서 "UART가 가끔 깨진다", "슬립 전류가 2 mA 더 나온다", "GNSS가 재부팅 후 응답이 없다"의 상당수는 회로도 리뷰에서 숫자 세 줄(VOH vs VIH, VIN vs abs max, 꺼진 레일의 경로)로 잡을 수 있는 문제다. Rivian/NVIDIA/Apple 보드 검증 면접의 "1.8V SoC를 3.3V 센서에 직결해도 되나요? 수치로 설명해 보세요"가 바로 이 레슨이다.

데이터시트의 DC 특성 표는 두 종류의 약속이다. **출력 쪽**은 "이 전류(IOH/IOL)를 흘려도 최소 이만큼 높다/최대 이만큼 낮다"(VOH min, VOL max)를 보장하고, **입력 쪽**은 "이보다 높으면 반드시 1, 낮으면 반드시 0으로 읽는다"(VIH min, VIL max)를 보장한다. 둘 사이 여유가 노이즈 마진이다. 가운데 VIL~VIH 구간은 **불확정 영역**이다. 칩이 0이나 1로 읽기는 하지만 어느 쪽인지, 온도·로트마다 같은지는 아무도 보장하지 않는다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">DC 노이즈 마진: 출력 보장 띠 vs 입력 인식 띠 (세로축 = 전압, 1V = 60px)</text>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="40" y="284">0V</text><text x="40" y="224">1V</text><text x="40" y="164">2V</text><text x="40" y="104">3V</text>
  </g>
  <g stroke="currentColor" opacity=".35"><path d="M45 280 L720 280 M45 220 L720 220 M45 160 L720 160 M45 100 L720 100" stroke-dasharray="2 4"/></g>
  <text x="70" y="50" fill="currentColor" font-weight="bold">(a) SoC 1.8V → SoC 1.8V, 공칭</text>
  <g stroke="currentColor">
    <rect x="80" y="172" width="50" height="12" class="f-ok"/><rect x="80" y="184" width="50" height="84" class="f-mut"/><rect x="80" y="268" width="50" height="12" class="f-ok"/>
    <rect x="200" y="172" width="50" height="38" class="f-acc"/><rect x="200" y="210" width="50" height="32" class="f-warn"/><rect x="200" y="242" width="50" height="38" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="105" y="300">드라이버 출력</text><text x="225" y="300">리시버 입력</text></g>
  <g fill="currentColor" font-size="12">
    <text x="84" y="166">VOH 1.60</text><text x="254" y="214">VIH 1.17</text><text x="254" y="246">VIL 0.63</text><text x="84" y="264">VOL 0.20</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M165 184 L165 210"/><path d="M165 268 L165 242"/><path d="M130 184 L200 184 M130 268 L200 268" stroke-dasharray="3 3"/></g>
  <g fill="currentColor" font-size="12"><text x="140" y="202">NMH</text><text x="140" y="214">+0.43</text><text x="140" y="252">NML</text><text x="140" y="264">+0.43</text></g>
  <text x="410" y="50" fill="currentColor" font-weight="bold">(b) L2: SoC 1.8V → MCU 3.3V, worst</text>
  <g stroke="currentColor">
    <rect x="430" y="177" width="50" height="12" class="f-ok"/><rect x="430" y="189" width="50" height="79" class="f-mut"/><rect x="430" y="268" width="50" height="12" class="f-ok"/>
    <rect x="580" y="92" width="50" height="68" class="f-acc"/><rect x="580" y="160" width="50" height="72" class="f-warn"/><rect x="580" y="232" width="50" height="48" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="455" y="300">SoC @1.71V</text><text x="605" y="300">MCU @3.135V</text></g>
  <g fill="currentColor" font-size="12">
    <text x="634" y="164">VIH 2.00</text><text x="634" y="236">VIL 0.80</text><text x="634" y="196">불확정</text>
    <text x="400" y="186" text-anchor="end">VOH 1.51</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M480 189 L560 189" stroke-dasharray="3 3"/><path d="M545 189 L545 160"/><path d="M480 160 L580 160" stroke-dasharray="3 3"/></g>
  <text x="490" y="152" fill="currentColor" font-size="12">NMH −0.49 V</text>
  <text x="410" y="322" fill="currentColor" font-size="12">VOH 보장값이 불확정 영역 한가운데 → FAIL</text>
  <text x="70" y="322" fill="currentColor" font-size="12">초록 = 출력 보장, 강조 = 입력 확정, 주황 = 불확정</text>
</svg>
```

데이터는 두 파일이다. `parts.json` 은 가상 부품 5종의 DC 특성 표다(JEDEC JESD8-7A 1.8V, JESD8C 3.3V LVTTL/LVCMOS, 74LVC1G 계열 데이터시트의 **표 구조**를 흉내 냈다). `board.json` 은 가상 로봇 컨트롤러 보드 RB-A0의 레일 공차와 6개 인터페이스다.

| 부품 | VDD 범위 | VIH / VIL | VOH / VOL (시험 전류) | abs max VIN | ESD→VDD | Ioff | IINJ |
|---|---|---|---|---|---|---|---|
| `SOC18_GPIO` | 1.65–1.95 | 0.65·VDD / 0.35·VDD | VDD−0.2 / 0.2 @0.1mA, VDD−0.45 / 0.45 @2mA | VDD+0.3 | 있음 | 없음 | 0 (미기재) |
| `MCU33_GPIO` | 2.7–3.6 | 2.0 / 0.8 (LVTTL) | VDD−0.4 / 0.4 @8mA | VDD+0.3 | 있음 | 없음 | 5 mA |
| `SENS33_IO` | 1.62–3.6 | 0.7·VDD / 0.3·VDD | VDD−0.4 @2mA, VOL 0.4 @3mA | VDD+0.3 | 있음 | 없음 | 0 |
| `GNSS_IO` | 3.0–3.6 | 0.7·VDD / 0.3·VDD | VDD−0.4 / 0.4 @2mA | VDD+0.3 | 있음 | 없음 | 0 |
| `BUF_IOFF_3V3` | 3.0–3.6 | 2.0 / 0.8 | VDD−0.1 @0.1mA, 2.3 / 0.55 @24mA | 5.5 | 없음 | **있음** | 0 |

| 링크 | 넷 | 구성 | 드라이버@레일 → 리시버@레일 |
|---|---|---|---|
| L1 | UART3_RX | 직결 | MCU@VDD_3V3 (3.135–3.465) → SoC@VDD_1V8 (1.71–1.89) |
| L2 | UART3_TX | 직결 | SoC@VDD_1V8 → MCU@VDD_3V3 |
| L3 | I2C2_SDA | open-drain FET 시프터, 양쪽 4.7k | SoC@VDD_1V8 ↔ IMU@VDD_3V3_SNS (3.036–3.564) |
| L4 | UART2_TX | 직결, TX idle High | MCU@VDD_3V3 → GNSS@VDD_3V3_GNSS (**슬립 때 OFF**, 꺼진 레일 등가 1kΩ) |
| L5 | UART2_TX_BUF | L4 수정안 | MCU@VDD_3V3 → Ioff 버퍼@VDD_3V3_GNSS → GNSS |
| L6 | IMU_CS_N | 직결 | MCU VDDIO2 뱅크@VDD_3V0 (2.85–3.15) → IMU@VDD_3V3_SNS |

### Part A — 레벨 식, 부하 전류에 맞는 보장값, 노이즈 마진

- `eval_level(spec, vdd)` — `2.0`, `"0.45"`, `"0.65*VDD"`, `"VDD-0.45"`, `"0.7*VDD+0.1"` → 전압. 모르는 식(`"VCC-0.2"`), `nan`, bool → `ValueError`.
- `out_level(points, i_load_ma, vdd, high)` — `[[식, 시험전류], ...]` 중 **시험 전류 ≥ 부하 전류**인 점만 유효하다. 그중 VOH는 최댓값, VOL은 최솟값을 고른다. 유효 점이 없으면 `None`(보장 없음).
- `margins(drv, rcv, vdd_drv, vdd_rcv, i_load_ma)` → `(NMH, NML)` 또는 `None`. `classify(m)`: 음수/None → FAIL, `MIN_NM_V`(=0.10 V, **팀 정책값**) 미만 → MARGINAL, 그 이상 → PASS.

### Part B — 코너 스윕, 클램프 전류, back-powering

- `corners(rails, names)` — 레일 min/max 조합. **같은 이름은 한 번만** 흔든다(같은 레일 = 같은 전압).
- `worst_margins(fn, rails, rail_d, rail_r, correlated=True)` — 모든 코너의 최소 NMH/NML과 그 코너. `correlated=False`(같은 레일도 독립으로 흔드는 틀린 방법)와 비교해 가짜 MARGINAL이 생기는 것을 보인다.
- `r_out_ohm(part, vdd)` — VOH 점으로 출력 저항 추정: min (VDD − VOH)/IOH.
- `inject(v_src, r_src, v_rail, r_rail)` — 드라이버 → ESD 다이오드(Vf 0.6 V 가정) → 리시버 VDD 레일. 켜진 레일은 (V, 0 Ω), 꺼진 레일은 (0 V, off_load_ohm). 반환 (주입 mA, 레일 노드 V).

### Part C — RB-A0 6개 인터페이스 판정 리포트

- `check_power_off` — 리시버 레일이 꺼질 수 있으면 Ioff / ESD 경로 / 미기재를 판정한다(BACKPOWER, ABS_MAX_OFF, OFF_UNSPEC).
- `od_fet_dir` — FET 시프터에서 한쪽이 Low로 끌 때 반대쪽이 보는 (NMH, NML)을 계산한다.
- `board_report` — 표 + 링크별 finding(`FAIL ABS_MAX ...`, `FAIL BACKPOWER ...`)을 출력한다. 기대 결과는 L1 FAIL, L2 FAIL, L3 PASS, L4 FAIL, L5 PASS, L6 FAIL이다. 수정 시도 두 가지(L4에 10k 직렬 저항, L3에 Vgs(th) 1.8 V FET)도 판정한다.

## 2. Problem (English)

> **Logic-level compatibility checker.**
>
> You are given a parts database (per pin group: `vdd_range`, `vih`, `vil`, `voh`/`vol` as lists of `[level, test_current_mA]`,
> `vin_abs_max`, `esd_diode_to_vdd`, `ioff`, `iinj_max_ma`; levels are numbers or expressions like `"0.65*VDD"`, `"VDD-0.45"`)
> and a board description (rails with `nom/min/max`, optional `switchable` + `off_load_ohm`; links that are either a direct
> push-pull connection or an open-drain FET level shifter). Using only the Python standard library:
>
> 1. Compute the guaranteed VOH/VOL at a given load current (only datasheet points whose test current ≥ load count),
>    and the DC noise margins NMH = VOH − VIH, NML = VIL − VOL.
> 2. Find the worst case over rail corners. Each distinct rail goes to its min or max, and two ends on the same rail move together.
> 3. Flag: negative/insufficient margins; input above absolute maximum with an estimate of the ESD-clamp current
>    vs the allowed injection current; back-powering of a receiver whose rail is off (current through the ESD diode and the
>    resulting "phantom" rail voltage); FET-shifter gate-threshold problems.
> 4. Print a per-link report and a summary line.
>
> Example: a 1.8 V SoC GPIO (`VOH = VDD−0.2 @ 0.1 mA`) driving a 3.3 V MCU input with `VIH = 2.0 V` gives
> NMH = 1.51 − 2.00 = −0.49 V at the worst corner → FAIL. A 3.3 V MCU (`VOH = VDD−0.4 @ 8 mA`) driving the 1.8 V SoC has
> NMH = +1.51 V but injects ~23 mA into VDD_1V8 → FAIL.

## 3. 시작 코드

<!-- code: starter.py -->

부품 표와 보드 기술:

<!-- code: parts.json -->

<!-- code: board.json -->

```bash
cd lessons/HWLAB/HWLAB-B02
python3 -I starter.py          # TODO 상태: 5/39 passed, exit 1
python3 -I starter.py --report # 리포트도 찍어 본다 (TODO 상태에선 전부 UNSPEC_LOAD)
```

실제 출력 (일부):

```text
-- Part A: levels & noise margin --
      got 0.0, expected 1.17 +/- 0.001
FAIL  eval_level('0.65*VDD', 1.8) = 1.170
...
PASS  SoC VOH @3mA -> None (beyond every IOH test point)
...
PASS  margins at 5mA load -> None
FAIL  classify: -0.01 FAIL, 0.05 MARGINAL, 0.10 PASS, None FAIL
-- Part B: corners, clamp, back-powering --
...
PASS  worst_margins -> None when a corner is unspecified (5mA)
...
PASS  1.89V into 1.71V rail: diode stays off -> 0mA
-- Part C: RB-A0 interface audit --
FAIL  statuses L1..L6 = FAIL FAIL PASS FAIL PASS FAIL
...
PASS  L3 with Vgs(th)max 1.8V FET -> FET_VGS
FAIL  report summary line
5/39 passed
```

> 공짜 PASS 5개는 전부 "None/0을 돌려줘야 하는" 음성 테스트다. 스텁이 마침 `None` 과 `(0.0, v_rail)` 을 돌려줘서 통과한다. 그래서 각 기능에는 양성 테스트(정확한 숫자)도 같이 둔다.

## 4. 힌트

<details>
<summary>힌트 1 — 레벨 식 파서</summary>

공백을 지운 뒤 정규식 하나로 처리한다: `^(?:(\d+(?:\.\d+)?)\*)?VDD(?:([+-])(\d+(?:\.\d+)?))?$`. 매치되면 `k*VDD ± off`, 아니면 `float(s)` 로 시도한다. `float("nan")` 은 예외 없이 통과하므로 `math.isfinite` 로 따로 막는다.

</details>

<details>
<summary>힌트 2 — 시험 전류가 부하보다 작은 점은 쓸 수 없다</summary>

VOH는 부하 전류가 커질수록 떨어지고 VOL은 올라간다. 그래서 `VDD−0.45 @ 2 mA` 는 "2 mA **이하** 어떤 부하에서도 VOH ≥ VDD−0.45"라는 뜻이다. 1 mA 부하에서 쓸 수 있는 점은 2 mA 점뿐이다. 0.1 mA 점은 1 mA에 대해 아무것도 말해 주지 않는다. 유효한 점이 여러 개면 VOH는 max, VOL은 min이 가장 좋은 보장값이다.

</details>

<details>
<summary>힌트 3 — worst 코너는 어디인가</summary>

레벨 식이 VDD에 대해 1차(단조)라서 극값은 min/max 끝점에서 나온다. 그래서 레일마다 min/max 2개, 레일 n개면 2ⁿ 코너면 충분하다. 다른 레일이면 보통 NMH의 worst는 "드라이버 min, 리시버 max"다. 하지만 이런 규칙을 손으로 고르지 말고 **전부 돌려서 최솟값**을 찾는다. 같은 레일 이름은 `set()` 으로 한 번만 넣는다.

</details>

<details>
<summary>힌트 4 — 클램프와 back-powering은 같은 회로다</summary>

드라이버 = 전압원 `v_src` + 저항 `r_src`(= R_out + 직렬 R). ESD 다이오드 = Vf 고정 강하로 둔다. 리시버 레일이 켜져 있으면 이상적 전압원(V, 0 Ω)이고, 꺼져 있으면 0 V에 등가 부하 R_off가 붙은 노드다. 그러면 `I = (v_src − Vf − v_rail) / (r_src + r_rail)` 이다. I ≤ 0이면 다이오드가 꺼져 있다.

</details>

<details>
<summary>힌트 5 — FET 시프터 Low 레벨</summary>

드라이버 핀은 **양쪽 풀업 전류를 모두** sink 한다(반대쪽 전류가 FET를 지나 온다). 그래서 VOL은 (I_d + I_r) 전류의 보장값으로 고른다. 리시버 쪽 Low는 여기에 FET 채널 강하 I_r × Rds_on 을 더한 값이다. High는 각 쪽 풀업 레일 그 자체이고, 리시버 VDD와 같은 레일이므로 correlated로 처리한다.

</details>

## 5. 풀이

### Part A — 노이즈 마진은 "두 보장값의 차이"다

정의:

- **NMH = VOH(min, 그 부하 전류에서) − VIH(min)** : High를 보낼 때 잡음이 이만큼 끌어내려도 여전히 1로 읽힌다.
- **NML = VIL(max) − VOL(max, 그 부하 전류에서)** : Low를 보낼 때 잡음이 이만큼 밀어 올려도 여전히 0으로 읽힌다.

이 레슨이 흉내 낸 표준 값 (정확한 조건·각주는 표준 원문을 확인):

| 패밀리 | VDD | VIH min | VIL max | VOH min | VOL max | 비고 |
|---|---|---|---|---|---|---|
| JESD8-7A (1.8V) | 1.65–1.95 | 0.65·VDD | 0.35·VDD | VDD−0.2 @100µA, VDD−0.45 @2mA | 0.2 @100µA, 0.45 @2mA | 임계가 VDD 비례 |
| JESD8C LVTTL (3.3V) | 2.7–3.6 | 2.0 | 0.8 | 2.4 @2mA | 0.4 @2mA | 임계가 고정 |
| JESD8C LVCMOS (3.3V) | 2.7–3.6 | 2.0 | 0.8 | VDD−0.2 @100µA | 0.2 @100µA | |
| 이 레슨 MCU | 2.7–3.6 | 2.0 | 0.8 | VDD−0.4 @8mA | 0.4 @8mA | STM32류 "CMOS 포트" 표기 모사 |

실제 계산 (테스트로 확인한 값):

| 쌍 | VDD | NMH | NML | 판정 |
|---|---|---|---|---|
| SoC → SoC (1.8V 공칭, 0.1mA) | 1.8 / 1.8 | 1.60 − 1.17 = **+0.43** | 0.63 − 0.20 = **+0.43** | PASS |
| MCU → MCU (3.3V 공칭) | 3.3 / 3.3 | 2.90 − 2.00 = **+0.90** | 0.80 − 0.40 = **+0.40** | PASS |
| SoC → MCU (공칭) | 1.8 / 3.3 | 1.60 − 2.00 = **−0.40** | 0.80 − 0.20 = +0.60 | FAIL |
| SoC → SoC @5mA | | 시험점 없음 → `None` | | FAIL (UNSPEC_LOAD) |

"부하 전류"는 CMOS 입력 하나라면 µA 수준이다(입력 누설 전류). 하지만 같은 넷에 풀다운 저항, LED, 다른 칩의 입력, 디버그 헤더의 풀업이 붙으면 mA가 된다. 데이터시트는 **시험한 전류까지만** 보장한다. 2 mA 점밖에 없는 핀에 3 mA를 걸면 VOH가 얼마인지 데이터시트로는 말할 수 없다. 그래서 `None` 을 돌려주고 FAIL(UNSPEC_LOAD)로 판정한다. 외삽은 추정일 뿐이다(연습 4).

> **DC 마진의 한계.** 노이즈 마진은 정적(DC) 값이다. 실제 잡음은 그라운드 바운스, 크로스토크, 링잉(HWLAB-B05)처럼 AC다. 그래서 DC 마진이 +0.1 V뿐이면 신호 무결성 여유가 거의 없다는 뜻이다. `MIN_NM_V = 0.10` 은 표준이 정한 값이 아니라 이 레슨의 **팀 정책값**이다. 실무에서는 회사 설계 규칙(예: "VDD의 10%" 또는 "≥ 0.2 V")을 따른다. 느린 에지에는 슈미트 트리거 입력(히스테리시스)이 필요하다. 그 이유는 Q6에서 다룬다.

### Part B — 공칭은 거짓말을 한다: 코너 스윕

**L6** (MCU VDDIO2 뱅크 VDD_3V0 → IMU VDD_3V3_SNS)가 대표 예다. 공칭에서는 VOH 3.0−0.4 = 2.60 V, VIH 0.7×3.3 = 2.31 V로 NMH가 +0.29 V다. 회로도 리뷰에서 그냥 통과한다. 그런데 레일이 각자 반대 방향으로 가면 어떻게 될까.

| 코너 | VDD_3V0 | VDD_3V3_SNS | VOH min | VIH | NMH |
|---|---|---|---|---|---|
| 공칭 | 3.000 | 3.300 | 2.600 | 2.310 | +0.290 |
| 드라이버 min, 리시버 max | **2.850** | **3.564** | 2.450 | 2.495 | **−0.045** |

VIH가 리시버 VDD에 **비례**하는 CMOS 입력(0.7·VDD)이 다른 레일의 드라이버를 만나면, 두 레일 공차가 마진을 양쪽에서 갉아먹는다. 벤치에서는 아마 동작할 것이다. 실제 입력 임계는 보통 VDD/2 근처라 데이터시트의 0.7·VDD보다 훨씬 낮기 때문이다. 바로 그래서 위험하다. **보장이 없는 채로 동작하는 보드**는 온도 코너, 다른 로트, 레일이 공차 끝으로 간 양산 보드에서 실패한다.

**같은 레일은 같이 움직인다.** 반대로 같은 레일을 독립 변수처럼 흔들면 *가짜* 경고가 나온다. SoC → SoC, 둘 다 VDD_1V8, 부하 2 mA인 경우:

| 방법 | 계산 | NMH | 판정 |
|---|---|---|---|
| correlated (맞음) | 0.35·V − 0.45 at V = 1.71 | **+0.1485** | PASS |
| independent (틀림) | (1.71 − 0.45) − 0.65 × 1.89 | +0.0315 | 가짜 MARGINAL |

한 레일이 동시에 1.71 V이면서 1.89 V일 수는 없다. 이 차이를 놓치면 리뷰 리포트가 가짜 경고로 가득 차고, 그러면 사람들이 리포트를 안 읽게 된다. SIV-B01 lint의 "너무 엄격하면 팀이 lint를 끈다"와 같은 교훈이다.

### Part B — ESD 클램프와 back-powering: 같은 회로, 다른 레일 상태

대부분의 CMOS 입력에는 핀→VDD, VSS→핀 방향의 ESD 보호 다이오드가 있다. 절대최대정격 `VIN ≤ VDD + 0.3 V` 의 0.3 V는 "이 다이오드가 의미 있게 켜지기 전까지"라는 뜻이다(실리콘 다이오드 Vf는 mA에서 약 0.6–0.7 V).

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwb02-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">L4: MCU TX(idle High) → 전원 꺼진 GNSS 입력 → ESD 다이오드 → VDD_3V3_GNSS</text>
  <rect x="20" y="50" width="200" height="190" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="30" y="72" fill="currentColor" font-weight="bold">MCU (VDD_3V3 ON)</text>
  <text x="30" y="96" fill="currentColor" font-size="12">VDD 3.465V (max 코너)</text>
  <rect x="120" y="120" width="60" height="24" class="f-mut" stroke="currentColor"/>
  <text x="150" y="137" fill="currentColor" font-size="12" text-anchor="middle">Rout 50Ω</text>
  <text x="30" y="137" fill="currentColor" font-size="12">PMOS ON</text>
  <rect x="440" y="50" width="280" height="190" rx="8" class="f-mut" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="450" y="72" fill="currentColor" font-weight="bold">GNSS (VDD_3V3_GNSS OFF)</text>
  <g stroke="currentColor" fill="none">
    <path d="M180 132 L470 132"/>
    <path d="M470 132 L470 110"/><path d="M462 110 L478 110 L470 96 Z"/><path d="M462 96 L478 96"/>
    <path d="M470 96 L470 84 L680 84 L680 120"/>
    <path d="M672 120 L688 120 L688 168 L672 168 Z"/><path d="M680 168 L680 200"/>
    <path d="M664 200 L696 200 M670 206 L690 206 M676 212 L684 212"/>
    <path d="M610 84 L610 150 M598 150 L622 150 M598 158 L622 158 M610 158 L610 200 M598 200 L622 200"/>
  </g>
  <text x="450" y="152" fill="currentColor" font-size="12">ESD 다이오드 (Vf 0.6V)</text>
  <text x="482" y="108" fill="currentColor" font-size="12">핀 → VDD</text>
  <text x="694" y="148" fill="currentColor" font-size="12">Roff</text>
  <text x="694" y="162" fill="currentColor" font-size="12">1k</text>
  <text x="626" y="182" fill="currentColor" font-size="12">벌크 C</text>
  <text x="560" y="76" fill="currentColor" font-size="12" font-weight="bold">phantom 2.73V</text>
  <path d="M240 122 L420 122" stroke="currentColor" fill="none" marker-end="url(#hwb02-arr)"/>
  <text x="236" y="160" fill="currentColor" font-size="12">I = (3.465 − 0.6 − 0) / (50 + 1000)</text>
  <text x="236" y="178" fill="currentColor" font-size="12">  = 2.73 mA (꺼진 레일로)</text>
  <text x="30" y="262" fill="currentColor" font-size="12">켜진 레일(L1)이면 Roff 자리에 이상적 전압원 1.71V: I = (3.465 − 0.6 − 1.71) / 50 = 23.1 mA → 레일로 역류</text>
  <text x="30" y="280" fill="currentColor" font-size="12">Ioff 버퍼(L5)는 이 다이오드가 없다: 꺼진 상태에서도 입력 5.5V까지, 전류 경로 없음</text>
</svg>
```

모델과 가정:

| 요소 | 모델 | 가정 / 한계 |
|---|---|---|
| 드라이버 High | Thevenin: VDD(max 코너) + R_out | R_out = (VDD − VOH)/IOH. VOH는 보장 **하한**이라 이 R은 실제의 상한이다. 그래서 계산된 전류는 **하한**이고 실제는 더 클 수 있다. 다만 큰 전류에서는 출력 트랜지스터가 포화돼 제한된다 |
| ESD 다이오드 | Vf = 0.6 V 고정 | 실제는 지수 곡선이고, mA 영역에서 0.6–0.8 V |
| 켜진 레일 | 이상적 전압원 | LDO는 보통 전류를 **sink 못 한다**. 주입 전류가 레일 부하보다 크면 레일 전압이 올라간다(back-feed) |
| 꺼진 레일 | 0 V + R_off(DC 등가) | 실제 부하는 비선형이다(칩 내부가 반쯤 켜짐). 벌크 커패시터 때문에 시간 상수도 있다 |

결과:

| 링크 | 상황 | 계산 | 판정 |
|---|---|---|---|
| L1 | 3.465 V → 켜진 1.71 V 레일 | (3.465 − 0.6 − 1.71)/50 = **23.1 mA**, abs max 2.01 V 초과 | FAIL (ABS_MAX + CLAMP, IINJ 미기재 = 0) |
| L4 | 3.465 V → 꺼진 레일, R_off 1 kΩ | 2.865/1050 = **2.73 mA**, phantom **2.73 V** | FAIL (BACKPOWER) |
| L4 + 10k | 직렬 10 kΩ 추가 | 2.865/11050 = 0.26 mA, phantom 0.26 V | 여전히 FAIL — 직렬 R은 **줄일 뿐 없애지 못한다** |
| 3V3 → 3V0 MCU 핀 | IINJ 5 mA 허용 핀 | (3.465 − 0.6 − 2.85)/50 = 0.30 mA ≤ 5 mA | MARGINAL (CLAMP, 클램프에 의존하는 설계) |

L1의 결과가 핵심이다. **노이즈 마진은 NMH +1.51 V, NML +0.20 V로 완벽하다.** 로직 레벨만 보는 검사기는 이 링크를 통과시킨다. 하지만 SoC 핀에는 abs max를 넘는 전압이 걸리고, 수십 mA가 ESD 다이오드를 통해 VDD_1V8로 흘러든다. 결과는 장기 신뢰성 열화, 래치업(JESD78이 시험하는 그 현상), 그리고 1.8 V 레일이 LDO로 만들어졌다면 레일 자체가 밀려 올라가는 것이다.

back-powering(L4)의 phantom 2.73 V는 "GNSS가 꺼졌다"는 펌웨어의 믿음을 깨뜨린다. 칩 내부가 반쯤 켜진 상태라 슬립 전류가 mA 단위로 늘어난다. 다음 전원 인가 때 레일이 POR 임계 아래로 내려가지 않아 **리셋이 제대로 안 걸린다**("재부팅 후 GNSS 무응답"). 레일에 묶인 다른 부품까지 이상 상태로 간다.

**같은 3.3 V인데도 abs max를 넘는다 (L4, 전원 ON).** 리포트에 나온 실제 발견이다. VDD_3V3 max 3.465 V가 VDD_3V3_GNSS min 3.135 V + 0.3 = 3.435 V보다 30 mV 높다. 다이오드는 켜지지 않으므로(0.6 V 문턱 미만) 체커는 MARGINAL "paper violation"으로 보고한다. 해결은 같은 레일에서 파생시키거나(로드 스위치로 VDD_3V3을 끊는 구조라면 GNSS 레일 = VDD_3V3 − 스위치 강하가 되어 상관된다), 공차를 좁히거나, 레이아웃 리뷰에서 근거를 남기는 것이다.

### Part C — RB-A0 리포트와 수정안

`python3 -I solution.py --report` 의 실제 출력:

```text
RB-A0 (가상 로봇 컨트롤러 보드 rev A0) - logic-level audit (MIN_NM=0.10V, VF_ESD=0.6V, worst = rail corner sweep)
id  net          topo    direction                  NMH nom/worst   NML nom/worst   status
L1  UART3_RX     direct  MCU33_GPIO->SOC18_GPIO     +1.730/+1.506  +0.230/+0.198  FAIL
    FAIL     ABS_MAX     Vin 3.465V > abs max 2.010V (VDD_1V8=1.710V)
    FAIL     CLAMP       ~23.1mA into VDD_1V8 via ESD diode > IINJ 0mA
L2  UART3_TX     direct  SOC18_GPIO->MCU33_GPIO     -0.400/-0.490  +0.600/+0.600  FAIL
    FAIL     NM_HIGH     SOC18_GPIO->MCU33_GPIO NMH=-0.490V at Vdrv=1.710V Vrcv=3.135V (nominal -0.400V)
L3  I2C2_SDA     od_fet  SOC18_GPIO->SENS33_IO      +0.990/+0.911  +0.536/+0.458  PASS
                         SENS33_IO->SOC18_GPIO      +0.630/+0.599  +0.228/+0.197  
L4  UART2_TX     direct  MCU33_GPIO->GNSS_IO        +0.590/+0.310  +0.590/+0.540  FAIL
    MARGINAL ABS_MAX     Vin 3.465V > abs max 3.435V (VDD_3V3_GNSS=3.135V); below diode knee (0.6V), paper violation at opposite rail corners
    FAIL     BACKPOWER   VDD_3V3_GNSS OFF: 2.73mA via ESD diode, phantom VDD_3V3_GNSS=2.73V (Vdrv 3.465V, Rsrc 50 ohm, Roff 1000 ohm)
L5  UART2_TX_BUF direct  MCU33_GPIO->BUF_IOFF_3V3   +0.900/+0.735  +0.400/+0.400  PASS
L6  IMU_CS_N     direct  MCU33_GPIO->SENS33_IO      +0.290/-0.045  +0.590/+0.511  FAIL
    FAIL     NM_HIGH     MCU33_GPIO->SENS33_IO NMH=-0.045V at Vdrv=2.850V Vrcv=3.564V (nominal +0.290V)
6 links: 2 PASS, 0 MARGINAL, 4 FAIL
```

하드웨어 엔지니어가 바로 행동할 수 있게 원인과 수정안을 붙인다:

| 링크 | 무엇이 문제인가 | 수정안 (보드 rev B / 펌웨어) |
|---|---|---|
| L1 | 3.3 V push-pull이 1.8 V 핀을 abs max 이상으로 몰고, ~23 mA를 레일로 주입 | 방향 고정 트랜슬레이터(74LVC1T45류, VCCA/VCCB 분리). 단방향 저속이면 저항 분압기(연습 2) |
| L2 | VOH 1.51 V가 VIH 2.0 V에 못 미침. 입력이 불확정 영역에 머물러 입력단 관통 전류도 생긴다 | 같은 트랜슬레이터의 반대 방향. 또는 1.8 V 입력 임계를 지원하는 MCU VDDIO 뱅크로 옮김 |
| L3 | 통과. worst NML +0.197 V(IMU→SoC 방향)가 가장 빡빡하다 | 풀업 값과 rise time은 HWLAB-B03에서 검증 |
| L4 | GNSS 슬립 중 TX idle High가 레일을 2.73 V로 back-power | ① 펌웨어: 레일 OFF 전에 TX 핀을 Low 또는 Hi-Z(아날로그 모드)로 ② 하드웨어: Ioff 버퍼(L5) ③ 시퀀싱(연습 3) |
| L5 | 통과. Ioff + 5.5 V 허용 입력이라 꺼진 상태에서도 경로가 없다 | 버퍼 출력→GNSS 구간도 같은 레일이라 함께 꺼진다 |
| L6 | 다른 레일 + 0.7·VDD 입력 → worst NMH −0.045 V | IMU를 VDD_3V0에서 IO 전원 받게 하거나, MCU 뱅크를 VDD_3V3_SNS로, 또는 레일 공차를 좁힘 |

**open-drain FET 레벨시프터 (L3).** NXP AN10441의 고전적 구조다. 게이트를 낮은 쪽 레일에 묶고, 소스는 저전압 쪽, 드레인은 고전압 쪽에 둔다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">L3 FET 레벨시프터: 세 가지 상태</text>
  <g stroke="currentColor" fill="none">
    <path d="M60 130 L300 130"/><path d="M380 130 L620 130"/>
    <path d="M300 130 L300 112 L380 112 L380 130"/><path d="M300 104 L380 104"/><path d="M340 104 L340 60"/>
    <path d="M140 130 L140 100"/><path d="M500 130 L500 100"/>
  </g>
  <rect x="130" y="66" width="20" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="490" y="66" width="20" height="34" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="104" y="60">VDD_1V8</text><text x="156" y="88">4.7k</text>
    <text x="464" y="60">VDD_3V3_SNS</text><text x="516" y="88">4.7k</text>
    <text x="318" y="55">G = VDD_1V8</text><text x="292" y="148">S</text><text x="378" y="148">D</text>
    <text x="60" y="150">SoC SDA (1.8V 쪽)</text><text x="520" y="150">IMU SDA (3.3V 쪽)</text>
  </g>
  <rect x="10" y="168" width="720" height="74" rx="6" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="20" y="188">① 아무도 안 끌면: Vgs ≈ 0 → FET OFF, 양쪽 = 각자 풀업 레일 (리시버 VDD와 같은 레일 → correlated)</text>
    <text x="20" y="208">② SoC가 Low: FET ON → 3.3V 쪽 = VOL + I_pu(3.3V) × Rds_on, SoC 핀은 두 풀업 전류 합을 sink</text>
    <text x="20" y="228">③ IMU가 Low: 바디 다이오드가 소스를 끌어내림 → Vgs↑ → FET ON, 1.8V 쪽 = VOL + I_pu(1.8V) × Rds_on</text>
  </g>
</svg>
```

L3 숫자 (worst 코너): SoC 핀 sink 전류 = 1.89/4.7k + 3.564/4.7k = **1.16 mA** (≤ 2 mA 점 → VOL 0.45 V 보장). IMU 쪽 Low = 0.45 V + (0.65–0.76 mA × 5 Ω) ≈ 0.453 V, VIL 0.3 × 3.036 = 0.911 V → NML +0.458. IMU→SoC 방향은 VOL 0.4 V(@3 mA) + 0.36 mA × 5 Ω ≈ 0.402 V, VIL 0.35 × 1.71 = 0.5985 V → **NML +0.197 V**. 1.8 V 쪽은 VIL 자체가 낮아서 이 방향이 가장 빡빡하다.

게이트 조건도 본다: 게이트 레일 min 1.71 V − Vgs(th)max 1.0 V = **0.71 V 오버드라이브**. 흔한 BSS138은 Vgs(th)가 최대 1.5 V까지 갈 수 있고, Rds(on)은 주로 4.5 V·10 V 게이트 조건에서만 보장된다. 1.8 V 쪽 시프터에 BSS138을 쓰면 오버드라이브가 0.2 V 남짓이고 Rds(on) 보장은 없다. 체커는 `rds_on_vgs` 보다 게이트 레일이 낮으면 MARGINAL(FET_RDS)로, Vgs(th) 이하이면 FAIL(FET_VGS)로 판정한다. 테스트에서 Vgs(th) 1.8 V FET로 바꾸면 FET_VGS가 나온다.

**벤치에서 확인하는 법** (장비/도구가 있을 때):

| 원인 | 보드에서 보이는 증상 | 드러내는 측정 |
|---|---|---|
| L2형 NMH 부족 | 실온에선 동작, 고온·저전압에서 UART 프레이밍 에러 | 함수 발생기로 느린 삼각파를 (직렬 R 거쳐) 입력에 넣고 펌웨어가 IDR 비트 토글 시점의 전압을 기록 → 실제 VIH/VIL·히스테리시스. 레일을 min으로 내린 채 재시험 |
| L1형 클램프 주입 | 1.8 V 레일이 1.9 V 이상으로 올라감, SoC 발열 | VDD_1V8을 DMM으로 측정(UART 트래픽 유무 비교), 직렬 0 Ω을 떼고 전류계/전류 프로브로 핀 전류 측정 |
| L4형 back-power | 슬립 전류 과다, GNSS 재부팅 후 무응답 | 꺼진 레일 전압을 DMM/스코프로 측정(0 V여야 정상), TX 핀을 Low로 바꿨을 때 슬립 전류 변화, 레일 방전 파형(꺼지는 순간 스코프) |
| VOH 부하 의존 | 여러 입력이 붙은 넷에서만 실패 | SMU(소스-측정 장비)로 핀 전류를 스윕하며 VOH 기록, PyVISA + SCPI로 자동화 |

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py            # 39/39 passed, exit 0 (약 0.1초)
python3 -I solution.py --report   # + RB-A0 리포트 (§5 Part C 출력)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 / 습관 |
|---|---|---|
| 공칭 VDD로만 마진 계산 | L6처럼 공칭 +0.29 V, worst −0.045 V | `worst_margins` 로 모든 레일 코너 스윕 |
| 같은 레일을 독립 변수로 흔듦 | 한 레일이 동시에 min과 max일 수 없다 → 가짜 MARGINAL(+0.0315 V) | `corners(rails, names)` 에서 `set(names)` |
| 0.1 mA 시험점의 VOH를 3 mA 부하에 사용 | 시험 전류보다 큰 부하에서는 보장이 없다 | `i_test >= i_load` 인 점만, 없으면 `None` |
| 노이즈 마진만 보고 PASS | L1: 마진은 +1.5 V인데 23 mA가 레일로 역류 | abs max + 클램프 전류 검사를 별도로 |
| "직렬 1k 넣었으니 back-power 해결" | 전류만 줄고 phantom은 남음(10k에서도 0.26 V) | 경로를 없앤다: Ioff 부품, 핀 Low/Hi-Z, 시퀀싱 |
| 5V tolerant = 꺼져 있어도 5V OK | 많은 FT 핀은 VDD가 있을 때만 허용. 꺼진 상태는 Ioff나 별도 각주가 있어야 보장된다 | `ioff` 플래그가 없으면 OFF_UNSPEC |
| R_out 추정 전류를 상한으로 믿음 | VOH 하한으로 구한 R은 상한 → 전류는 **하한** | 리포트에 `~` 와 모델 가정 표기 |
| VOL 계산에 자기 풀업 전류만 사용 (FET 시프터) | 반대쪽 풀업 전류도 FET를 지나 드라이버로 온다 | `vol = out_level(..., (i_d + i_r) * 1000, ...)` |
| 3.3 V 레일이 둘이면 서로 호환이라고 가정 | 공차 반대 끝이면 VDD+0.3 V를 넘을 수 있다 (L4 30 mV) | 레일을 이름으로 구분해 코너 스윕 |

## 8. 면접 Q&A

<details>
<summary>Q1. 1.8V SoC GPIO를 3.3V MCU 입력에 직결해도 되나요? 수치로 설명해 보세요.</summary>

MCU 입력이 LVTTL 임계(VIH 2.0 V)라면 안 된다. SoC의 VOH 보장값은 VDD−0.2 V @100 µA인데, 레일 min 1.71 V에서는 1.51 V다. NMH = 1.51 − 2.0 = −0.49 V로 음수다. 반대로 0.7·VDD 임계 입력이면 2.2 V 이상이 필요해서 더 나쁘다.
꼬리질문: "벤치에서는 잘 되던데요?" → CMOS 입력의 실제 문턱은 대개 VDD/2 근처라 실온의 한 보드에서는 1로 읽힐 수 있다. 하지만 데이터시트는 VIH 이상만 보장한다. 게다가 입력이 불확정 영역에 머물면 입력단의 PMOS·NMOS가 동시에 반쯤 켜져 관통 전류(crowbar current)가 흐르고 슬립 전류가 는다. 해결은 트랜슬레이터, 또는 1.8 V 입력을 지원하는 VDDIO 뱅크다.

</details>

<details>
<summary>Q2. 반대로 3.3V 출력을 1.8V 입력에 직결하면? 1kΩ 직렬 저항이면 괜찮나요?</summary>

로직 레벨은 문제없다(NMH +1.5 V). 문제는 절대최대정격이다. 1.8 V 핀의 abs max는 VDD+0.3 V(레일 min에서 2.01 V)인데 3.3 V가 걸린다. 초과분은 ESD 다이오드로 흘러 VDD_1V8로 주입된다. 직결이면 R_out 50 Ω 기준 ~23 mA, 1 kΩ 직렬이면 (3.465 − 0.6 − 1.71)/1050 ≈ 1.1 mA다.
꼬리질문: "1.1 mA면 괜찮은 것 아닌가요?" → 데이터시트가 IINJ(주입 전류)를 명시적으로 허용할 때만 그렇다. 예컨대 STM32 계열은 표준 핀에 ±5 mA 같은 한도를 주고 FT 핀은 양의 주입을 허용하지 않는 식으로 표기한다. SoC 데이터시트에 주입 허용이 없으면 0으로 봐야 한다. 그리고 주입 전류는 레일로 간다. LDO는 보통 전류를 sink 못 하므로, 레일 부하가 작은 저전력 모드에서 레일 전압이 올라간다. 직렬 저항은 RC 지연도 만든다(1 kΩ × 10 pF = 10 ns 시정수).

</details>

<details>
<summary>Q3. Back-powering이 뭐고, 보드에서 어떻게 찾나요?</summary>

전원이 꺼진 칩의 I/O 핀에 다른 칩이 High를 걸면, 입력 ESD 다이오드를 통해 꺼진 레일이 충전되는 현상이다. L4 모델에서는 2.73 mA와 phantom 2.73 V였다. 증상은 슬립 전류 증가, 꺼졌어야 할 레일의 1~3 V 잔류 전압, 다음 전원 인가 때 POR이 정상적으로 안 걸려 칩이 무응답하는 것이다.
찾는 법: 저전력 모드에서 각 스위치드 레일을 DMM으로 재서 0 V가 아니면 의심한다. 레일에 연결된 신호를 하나씩 Low로 만들거나(펌웨어) 직렬 0 Ω을 떼어 범인을 찾는다. 레일 OFF 순간을 스코프로 찍으면 방전이 중간에서 멈추는 "plateau"가 보인다.
꼬리질문: "UART TX가 왜 단골 범인인가요?" → UART는 idle이 High라 꺼지기 직전까지, 그리고 꺼진 동안 내내 High를 유지한다. I2C 풀업을 always-on 레일에 달아 둔 경우도 같은 구조다.

</details>

<details>
<summary>Q4. Ioff 기능(partial power-down)은 무엇을 보장하나요? "5V tolerant"와 같은가요?</summary>

Ioff는 VCC = 0일 때 입력·출력 핀으로 들어가는 전류가 규정값 이하라는 사양이다. 74LVC1G 계열처럼 "Ioff supports partial-power-down mode" 로 표기한다. 핀→VCC 방향 다이오드가 없는 구조라서 꺼진 상태에서도 경로가 없다. 5V tolerant는 보통 "VDD가 켜져 있을 때 VIN이 5.5 V까지 가도 된다"는 뜻이다. 꺼진 상태의 동작은 별도로 명시되어 있어야 한다. 그래서 체커는 Ioff가 없는 FT 핀을 OFF_UNSPEC으로 경고한다.
꼬리질문: "Ioff 버퍼를 넣으면 끝인가요?" → 버퍼의 출력 쪽도 본다. 버퍼 출력이 꺼진 GNSS를 구동하지 않도록 버퍼 VCC를 GNSS 레일에 묶는다(L5). 버퍼의 OE 기본 상태와 전파 지연도 확인한다.

</details>

<details>
<summary>Q5. BSS138 같은 FET로 I2C 레벨 시프트를 할 때 주의할 점은?</summary>

① 게이트를 **낮은 쪽** 레일에 묶는다. 그 레일 min이 Vgs(th)max보다 충분히 커야 한다. BSS138은 Vgs(th)가 최대 1.5 V라 1.8 V 쪽에서는 오버드라이브가 ~0.2 V뿐이고, Rds(on)은 4.5 V/10 V 게이트 조건에서만 보장된다. 저전압용 low-Vth FET나 전용 IC(I2C 버퍼/시프터)를 쓴다. ② Low를 끄는 쪽이 **양쪽 풀업 전류 합**을 sink 하므로 VOL 시험 전류를 넘지 않게 풀업을 정한다(L3: 1.16 mA). ③ High는 풀업이 만드는 RC 상승이라 속도가 풀업과 버스 커패시턴스로 정해진다(HWLAB-B03).
꼬리질문: "TXB 같은 자동 방향 버퍼를 I2C에 쓰면?" → TXB류는 각 핀을 약한(수 kΩ급) 출력 버퍼와 에지 가속기로 구동한다. 그래서 강한 풀업이나 큰 부하가 붙으면 레벨이 제대로 안 나온다. I2C 같은 open-drain 버스에는 TXS류(패스 FET + one-shot)나 I2C 전용 버퍼/시프터를 쓴다. 정확한 조건은 해당 데이터시트의 풀업·부하 권장값으로 확인한다.

</details>

<details>
<summary>Q6. 노이즈 마진이 +0.3 V면 신호 무결성은 안전한가요?</summary>

DC 마진은 필요조건일 뿐이다. 그라운드 바운스(동시 스위칭 출력), 크로스토크, 반사·링잉(HWLAB-B05), 느린 에지에서 불확정 영역에 머무는 시간 같은 AC 효과가 이 여유를 깎는다. 느린 에지나 잡음 섞인 입력(긴 케이블, RC 필터 뒤)에는 슈미트 트리거 입력이 필요하다. 히스테리시스가 있어야 문턱 근처에서 여러 번 토글(글리치 → 가짜 인터럽트)하지 않는다.
꼬리질문: "스코프로 마진을 어떻게 확인하나요?" → 리시버 핀에서(프로브 그라운드 짧게, HWLAB-B05) 최저 High 레벨과 최고 Low 레벨을 측정하고, 링잉의 골·마루가 VIH/VIL을 넘는지 본다. IEEE 181 상태 레벨로 측정한다(HWLAB-B04).

</details>

<details>
<summary>Q7. 전원 시퀀싱 문서에 무엇을 넣어야 back-power를 막을 수 있나요?</summary>

레일별 on/off 순서와 시간, 그리고 **각 인터페이스의 드라이버 레일이 켜지고 리시버 레일이 꺼진 구간**이다(연습 3). 한 방향만 고치면 반대 방향 링크로 위험 구간이 옮겨 간다(연습 3에서 L1·L4·L6 대신 L2가 나타남). 그래서 시퀀싱만으로는 부족하다. 리셋 직후 GPIO 기본 상태(대부분의 MCU는 입력/Hi-Z라 리셋 직후에는 구동하지 않는다), 펌웨어가 핀을 설정하는 시점, 슬립 진입 순서(핀 먼저 Low/Hi-Z, 그다음 레일 OFF)를 같이 적는다.

</details>

## 9. 추가 연습

### 연습 1 — 직렬 저항으로 phantom을 0.3 V 이하로: 얼마나, 대가는?

L4에서 phantom 레일을 0.3 V 이하로 만들 직렬 R을 구하고, 그 R과 입력+배선 15 pF가 만드는 10–90% 상승 시간을 UART 115200 비트 폭과 비교하라.

<details>
<summary>답 보기</summary>

phantom = (Vd − Vf)·R_off / (R_out + Rs + R_off) ≤ V_lim 를 풀면 Rs ≥ (Vd − Vf)·R_off/V_lim − R_out − R_off = 2.865 × 1000/0.3 − 1050 = **8500 Ω** 이다. 상승 시간은 ln 9 × RC ≈ 2.197 × 8.55 kΩ × 15 pF = **281.8 ns** 로 UART 비트 8681 ns의 3% 수준이라 115200에서는 문제없다. 그러나 10 MHz SPI(주기 100 ns)는 불가능하다. 그리고 0.3 mA가 여전히 꺼진 칩으로 들어간다. 그 칩의 IINJ가 0(미기재)이면 이것도 사양 위반이다. 직렬 R은 "완화"이고 "해결"이 아니다.

</details>

### 연습 2 — 저항 분압기 3.3 V → 1.8 V: 1k/2k vs 1k/1.2k

MCU(VDD_3V3) → [Rt] → 노드 → [Rb] → GND, 노드 = SoC 입력이다. 1% 저항으로 노드 최대 전압(abs max 2.01 V와 비교), NMH/NML, Thevenin R, 10 pF에서의 상승 시간을 구하라.

<details>
<summary>답 보기</summary>

비율 k = Rb/(Rt+Rb)의 공차 극값은 k_max = Rb(1+t)/(Rt(1−t)+Rb(1+t)), k_min은 그 반대다. 노드 최대는 VDD max × k_max (High는 무부하에서 VDD까지 간다). 실제 출력:

```text
      1k/2k    Vnode_max 2.325V (abs 2.010V) NMH +0.583 NML +0.330 Rth 667 ohm t_r 14.6ns @10pF
      1k/1.2k  Vnode_max 1.907V (abs 2.010V) NMH +0.250 NML +0.378 Rth 545 ohm t_r 12.0ns @10pF
```

"3.3 V를 2/3로 나누면 2.2 V니까 1.8 V 입력에 대충 맞겠지"가 흔한 실수다. 1k/2k는 abs max를 넘는다. 1k/1.2k는 통과한다. 분압기는 단방향 전용이고, 상시 1.5 mA를 소모하며, Thevenin R × C 만큼 느려진다. 저속 신호(리셋, enable)에만 쓴다.

</details>

### 연습 3 — 전원 시퀀스에서 back-power 구간 찾기

이벤트 `[(0,VDD_3V3,on),(0,VDD_3V0,on),(2,VDD_1V8,on),(3,VDD_3V3_SNS,on),(5,VDD_3V3_GNSS,on),(100,VDD_3V3_GNSS,off)]` 에서 direct 링크의 "드라이버 ON, 리시버 OFF, ESD 경로 있음" 구간을 찾아라. 순서를 뒤집으면(리시버 레일 먼저, VDD_3V3 마지막) 어떻게 되나?

<details>
<summary>답 보기</summary>

```text
      L1 back-power window 0 ms .. 2 ms
      L4 back-power window 0 ms .. 5 ms
      L4 back-power window 100 ms .. end
      L6 back-power window 0 ms .. 3 ms
      (reversed) L2 back-power window 0 ms .. 1 ms
```

L5(Ioff)는 한 번도 나오지 않는다. 순서를 뒤집으면 위험이 **반대 방향 링크 L2**로 옮겨 간다. 양방향 인터페이스가 있으면 순서만으로 모든 구간을 없앨 수 없다. 실제로 이 구간이 위험한지는 드라이버 핀이 그 시간에 High를 구동하는지에 달려 있다. MCU GPIO는 리셋 직후 보통 입력/Hi-Z라 0–2 ms 구간의 L1은 펌웨어가 UART를 초기화하기 전이면 무해할 수 있다. 하지만 L4의 100 ms 이후 구간(슬립)은 UART가 이미 idle High라 실제로 위험하다.

</details>

### 연습 4 — 이 넷에 부하를 얼마나 더 달 수 있나?

worst 코너에서 NMH ≥ 0.1 V를 지키는 최대 High 부하 전류를 구하라. 데이터시트 IOH 시험점을 넘으면 "외삽"으로 표시한다.

<details>
<summary>답 보기</summary>

먼저 IOH 시험점의 보장 VOH로 worst NMH를 본다. 여기서 이미 0.1 V 미만이면(L6) 데이터시트로는 어떤 부하에서도 보장할 수 없다(`None`). 그 너머는 시험점을 지나는 선형 모델 VOH(I) = VDD − I·R_out 로 구한다.

```text
      MCU->GNSS 12.19mA (extrapolated=True), SoC->SoC 2.216mA (extrapolated=True), L6 None
```

MCU→GNSS는 (3.135 − 2.4255 − 0.1)/50 Ω = 12.19 mA다. 하지만 8 mA 시험점을 넘으므로 **보장값이 아니다**. 설계는 8 mA 이하로 하고, 그 이상이 필요하면 SMU로 실측한 특성(여러 보드, 온도)을 근거로 남긴다. 핀당·포트 합계 전류 한도(abs max 표의 IIO, ΣIIO)도 같이 본다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py      # 12/12 passed, exit 0
```

## 10. 핵심 정리

- NMH = VOH(min, 부하 전류에서) − VIH(min), NML = VIL(max) − VOL(max). VOH/VOL은 **시험 전류 이하에서만** 보장된다.
- worst-case는 레일 코너 스윕이다. 다른 레일은 반대 방향으로, **같은 레일은 함께** 흔든다. 공칭 +0.29 V가 worst −0.045 V가 될 수 있다(0.7·VDD 입력 + 다른 레일).
- 로직 레벨이 맞아도 끝이 아니다. VIN ≤ VDD+0.3 V(abs max)를 넘으면 ESD 다이오드로 전류가 주입된다. 3.3 V→1.8 V 직결은 마진 +1.5 V인데 ~23 mA를 주입한다.
- back-powering: 꺼진 칩 입력에 High를 걸면 ESD 다이오드가 레일을 phantom 전압으로 충전한다. 슬립 전류가 늘고 POR이 실패한다. 직렬 R은 완화일 뿐이고, 해결은 Ioff 부품, 핀 Low/Hi-Z, 시퀀싱이다.
- open-drain FET 시프터: 게이트는 낮은 레일, Vgs(th)max와 Rds(on) 조건을 확인한다. 드라이버는 양쪽 풀업 전류 합을 sink 한다.
- 단방향 저속이면 분압기도 가능하다(공차 포함 abs max 확인). 양방향·고속이면 전용 트랜슬레이터를 쓴다.
- 리포트는 "코드 + 숫자 + 코너 + 수정안"으로 쓴다. 예: `FAIL BACKPOWER VDD_3V3_GNSS OFF: 2.73mA, phantom 2.73V`.

## 참고자료

- JEDEC JESD8-7A — 1.8 V ± 0.15 V (Normal Range) and 1.2 V – 1.95 V (Wide Range) Power Supply Voltage and Interface Standard
- JEDEC JESD8C.01 — Interface Standard for Nominal 3 V/3.3 V Supply Digital Integrated Circuits (LVTTL/LVCMOS)
- JEDEC JESD78 — IC Latch-Up Test
- [NXP AN10441 — Level shifting techniques in I2C-bus design](https://www.nxp.com/docs/en/application-note/AN10441.pdf)
- [NXP UM10204 — I2C-bus specification and user manual](https://www.nxp.com/docs/en/user-guide/UM10204.pdf) (VOL 조건, HWLAB-B03)
- [TI SN74LVC1G125 데이터시트](https://www.ti.com/product/SN74LVC1G125) — Ioff / partial-power-down, 5.5 V 입력 허용, VCC별 VIH 표
- [TI SN74LVC1T45 데이터시트](https://www.ti.com/product/SN74LVC1T45) — 듀얼 서플라이 방향 고정 트랜슬레이터
- onsemi BSS138 데이터시트 — Vgs(th) 0.8–1.5 V, Rds(on) 시험 게이트 전압 조건
- TI Logic Guide / 전압 레벨 변환(voltage translation) 선택 가이드 — 로직 패밀리별 VIH/VIL·트랜슬레이터 종류(방향 고정, TXB, TXS) 비교
- 관련 레슨: HWLAB-B01(넷리스트 감사 — 레일·풀업), HWLAB-B03(I2C 풀업·rise time), HWLAB-B04(IEEE 181 상태 레벨), HWLAB-B05(프로브·링잉), HWLAB-B09(첫 전원 인가)
