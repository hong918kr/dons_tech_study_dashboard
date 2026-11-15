---
id: SIV-A09
time: 130분
updated: 2026-11-08
---
# SIV-A09 검증 캠페인 설계: 신규 센싱 SoC (면접형)

## 학습 목표

- "새 칩의 검증을 처음부터 계획해 보라"는 질문에, 단계(pre-si 준비 → Day-0 → feature → PVT → stress → 양산 이관)마다 **측정 가능한 입구·출구 기준과 소유자**를 붙여 답한다.
- 시퀀서·랩 자동화·대시보드 구조와 triage 흐름을 그리고, **일정(critical path), bench 용량, 인력, 지표**를 가정을 밝힌 숫자로 말한다. "슬립하면 무엇을 먼저 자르나"에 근거를 댄다.
- Day-0 bring-up 스크립트를 **의존성 DAG**로 만들어 FAIL(칩이 틀림) · ERROR(계측이 틀림) · BLOCKED(측정 못 함) · FLAKY(재시도로 통과) · 안전 중단을 구분하고, 디버그 엔지니어가 바로 움직일 수 있는 **triage packet**을 낸다.
- 이 방법이 **못 보는 것**을 실측으로 말한다: 직선 게이팅의 과잉 차단, 게이팅이 만드는 결함 가림(masking), 상온 Day-0가 못 보는 저온 전용 결함, 표본 2장으로는 못 하는 원인 귀속.
- 45분 구두 답변의 시간 배분과 꼬리질문 대응을 갖춘다.

## 1. 문제

Staff급 실리콘 검증 면접의 마지막 라운드는 코드가 아니라 **"이 칩을 어떻게 검증하겠나, 처음부터 끝까지"** 다. 이 답에서 면접관은 테스트를 몇 개 아는지가 아니라, 일이 틀어졌을 때 무엇을 먼저 포기할지 판단할 수 있는 사람인지를 본다.

이 레슨은 가상 SoC **SENS-1**(아래, 전부 가정)로 설계 문서를 쓰고 Day-0 스크립트 골격을 구현한다. 특정 회사의 내부 방식을 설명하는 글이 아니다. 자동차 센싱 SoC라는 제품 범주에서 일반적으로 성립하는 판단만 적었고, 회사별 사정은 질문으로 확인해야 하는 빈칸으로 남겼다(§5 45분 개요).

| 항목 | SENS-1 가정 (전부 가상 · 대표값) |
|---|---|
| 카메라 입력 | MIPI CSI-2 RX 4포트, D-PHY 4-lane 가정 |
| 레이더 입력 | SerDes RX 2포트. 프로토콜은 미정으로 두고 PRBS BER만 다룬다 (벤더 SerDes와 MIPI A-PHY 등 선택지가 있고, 사양이 다르다) |
| 호스트 연결 | PCIe Gen4 x4 endpoint |
| 메모리 | LPDDR5 x32 + 온칩 SRAM |
| 연산 | CPU 클러스터, ISP, 레이더 DSP. 안전 MCU 섬은 별도 트랙 (본 문서 범위 밖) |
| 부팅 | 내장 ROM + secure boot. A0 샘플은 디버그 잠금 해제 상태로 받는다고 가정 |
| 일정 전제 | T0(첫 실리콘 도착) = W20, 금속 리스핀 A1 1회 가정 |
| 샘플 | A0 패키지 60개, 검증 보드 20장 |
| 온도 목표 | 자동차 등급 가정. AEC-Q100 Grade 1 = 주변온도 −40~125 ℃ (SIV-M08 참고). 챔버는 주변온도를 맞춘다 |

```svg
<svg viewBox="0 0 760 196" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a09a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">SENS-1 캠페인: 단계 사이마다 측정 가능한 게이트</text>
  <g stroke="currentColor">
    <rect x="10" y="36" width="114" height="74" rx="8" class="f-mut"/>
    <rect x="136" y="36" width="114" height="74" rx="8" class="f-acc"/>
    <rect x="262" y="36" width="114" height="74" rx="8" class="f-mut"/>
    <rect x="388" y="36" width="114" height="74" rx="8" class="f-warn"/>
    <rect x="514" y="36" width="114" height="74" rx="8" class="f-warn"/>
    <rect x="640" y="36" width="110" height="74" rx="8" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-weight="bold">
    <text x="67" y="58">pre-si 준비</text><text x="193" y="58">Day-0</text><text x="319" y="58">feature</text>
    <text x="445" y="58">PVT</text><text x="571" y="58">stress</text><text x="695" y="58">양산 이관</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="67" y="78">T0−20~0주</text><text x="193" y="78">T0~+2주</text><text x="319" y="78">+2~+8주</text>
    <text x="445" y="78">+8~+16주</text><text x="571" y="78">+8~+15주</text><text x="695" y="78">A1 회귀 후</text>
    <text x="67" y="98">에뮬레이터</text><text x="193" y="98">보드 3장 통과</text><text x="319" y="98">기능·IP별</text>
    <text x="445" y="98">코너·샘플</text><text x="571" y="98">장시간·동시</text><text x="695" y="98">ATE/SLT 상관</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#a09a)">
    <path d="M124 72 L135 72"/><path d="M250 72 L261 72"/><path d="M376 72 L387 72"/><path d="M502 72 L513 72"/><path d="M628 72 L639 72"/>
  </g>
  <rect x="262" y="128" width="366" height="40" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="445" y="146" text-anchor="middle" fill="currentColor" font-weight="bold">A1 금속 리스핀 (12주) + 회귀 (4주)</text>
  <text x="445" y="162" text-anchor="middle" fill="currentColor" font-size="12">이 모델에서 일정을 결정하는 경로</text>
  <text x="10" y="186" fill="currentColor" font-size="12">Day-0는 "칩이 살아 있나"만 답한다. 칩이 "맞는가"는 feature 이후에 답한다.</text>
</svg>
```

### Part A — 검증 계획서: 단계 · 입출구 기준 · 소유자

단계별 목적, 측정 가능한 입구·출구 기준, 소유자, IP × 단계 커버리지를 문서로 쓴다(§5 Part A). 코드로는 계획을 데이터로 두고 lint한다: 소유자 없음, 숫자 없는 기준, 끊어진 단계 연결, IP 구멍을 잡는 `lint_plan`, 게이트 판정 `gate_decision`.

### Part B — 인프라, triage, 일정·인력·지표, 무엇을 먼저 자를까

인프라 구조와 triage 흐름, 지표, 위험 목록을 문서로 쓴다(§5 Part B). 코드로는 `critical_path`, `capacity_weeks`, `triage_backlog`, `best_cut`을 구현해 일정 숫자를 계산으로 만든다. 순진한 greedy가 왜 틀릴 수 있는지도 측정한다.

### Part C — Day-0 스크립트 골격 (mock 칩 + 결함 주입)

```svg
<svg viewBox="0 0 760 232" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a09b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">선언한 의존성 DAG (실행 순서는 왼쪽에서 오른쪽)</text>
  <g stroke="currentColor">
    <rect x="10" y="100" width="96" height="40" rx="7" class="f-acc"/>
    <rect x="136" y="48" width="96" height="40" rx="7" class="f-mut"/>
    <rect x="136" y="152" width="96" height="40" rx="7" class="f-ok"/>
    <rect x="266" y="100" width="96" height="40" rx="7" class="f-mut"/>
    <rect x="392" y="100" width="86" height="40" rx="7" class="f-mut"/>
    <rect x="512" y="40" width="104" height="40" rx="7" class="f-warn"/>
    <rect x="512" y="100" width="104" height="40" rx="7" class="f-warn"/>
    <rect x="512" y="160" width="104" height="40" rx="7" class="f-warn"/>
    <rect x="648" y="40" width="102" height="40" rx="7" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="58" y="125">power</text><text x="184" y="73">clock</text><text x="184" y="177">jtag_idcode</text>
    <text x="314" y="125">boot_rom</text><text x="435" y="125">sram</text>
    <text x="564" y="65">ddr_training</text><text x="564" y="125">pcie_link</text><text x="564" y="185">serdes_loopback</text>
    <text x="699" y="65">csi2_loopback</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#a09b)">
    <path d="M106 110 L135 75"/><path d="M106 130 L135 165"/>
    <path d="M232 75 L265 110"/><path d="M232 165 L265 130"/>
    <path d="M362 120 L391 120"/>
    <path d="M478 110 L511 66"/><path d="M478 120 L511 120"/><path d="M478 130 L511 174"/>
    <path d="M616 60 L647 60"/>
  </g>
  <text x="184" y="214" text-anchor="middle" fill="currentColor" font-size="12">TCK는 프로브가 준다: PLL이 죽어도 IDCODE를 읽는다</text>
  <text x="600" y="222" text-anchor="middle" fill="currentColor" font-size="12">PCIe·SerDes는 DDR 없이 SRAM 펌웨어로 돈다 (가정)</text>
</svg>
```

`run_day0`는 스테이지를 돌며 의존성 게이팅을 하고, 모든 경로에서 `safe_state()`를 보장한다. `triage_packet`은 뿌리 원인·증거·소유자·다음 행동·시그니처를 담는다. 보드 8장 캠페인(결함 심음)에서 `summarize_campaign`이 시스템적/보드 개별/랩 문제를 가른다. 코드는 **게이팅과 보고만** 맡고, 각 측정의 내용은 SIV-M04(PCIe)·M05(DDR)·M06(PRBS)·M12(시퀀서)가 소유한다.

## 2. Problem (English)

> **You own validation for a new automotive sensing SoC: MIPI CSI-2 camera inputs, SerDes radar inputs, PCIe to a host, and LPDDR. Plan the campaign and implement the Day-0 bring-up skeleton.**
>
> 1. Produce a plan with phases (pre-silicon prep, Day-0, feature, PVT, stress, production handoff), each with an owner and *measurable* entry/exit criteria. Implement `lint_plan(plan) -> [(code, phase, detail)]` and `gate_decision(phase, metrics, waivers=()) -> dict`. A missing metric must never count as a pass.
> 2. Implement `critical_path(tasks) -> (total, path)`, `capacity_weeks(demands, benches, hours_per_day, days_per_week, uptime)`, `triage_backlog(...)`, and `best_cut(items, need)`, which selects the minimum-risk cut list meeting a weeks-saved target without touching `must_keep` items.
> 3. Implement `validate_stages(stages) -> order`, `run_day0(bench, stages, skip=(), gating=True) -> results`, `verdict_of(results) -> (verdict, exit_code)`, `triage_packet(results, meta) -> dict`, and `summarize_campaign(packets) -> rows`.
>
> Constraints: config errors must be caught before the bench is touched. A stage is `BLOCKED` only if a declared dependency did not pass, with the root cause recorded. An instrument failure is `ERROR`, never `FAIL`. A Python exception in a stage is `ERROR E_HARNESS_BUG`. A safety abort stops everything and the bench must reach its safe state on every path. Retries that eventually pass are reported as `FLAKY`. Output must be deterministic: same input, byte-identical JSON. Python 3.9 stdlib only.
>
> Example: with `faults=("ddr_lane",)` the DDR stage is `FAIL E_DDR_LANE` with lane 3 in the evidence, CSI-2 is `BLOCKED` by DDR, PCIe and SerDes still run and pass, verdict `FAIL`, exit 1.

## 3. 시작 코드

<!-- code: starter.py -->

```bash
cd lessons/SIV/SIV-A09
python3 -I starter.py; echo "exit=$?"
```

실제 출력 (일부):

```text
-- given mock sanity --
PASS  good mock: all rails read back in spec
PASS  JTAG bench: idcode good
PASS  fault injection: tdo_stuck1 reads all ones
PASS  fake clock: bootrom_hang consumes the budget, returns timeout
PASS  greedy_cut returns a covering set
-- Part A: plan lint + gates --
FAIL  good plan: no findings  (NotImplementedError: TODO lint_plan)
...
FAIL  limit: cold-only defect is invisible to a room-temp Day-0  (NotImplementedError: TODO run_day0)
FAIL  limit: without gating one dead rail yields 9 FAILs (8 false)  (NotImplementedError: TODO run_day0)
5/82 passed
exit=1
```

TODO는 11곳이다. `lint_plan`, `gate_decision` (Part A) · `critical_path`, `capacity_weeks`, `triage_backlog`, `best_cut` (Part B) · `validate_stages`, `run_day0`, `verdict_of`, `triage_packet`, `summarize_campaign` (Part C). mock 칩(`MockBench`), 스테이지 체크 함수(`chk_*`), `poll_until`, 리포트 렌더러는 주어진다. 측정 내용이 아니라 **판정과 게이팅**이 이 문제의 답이기 때문이다.

## 4. 힌트

<details><summary>힌트 1 — 숫자 없는 출구 기준은 기준이 아니다</summary>

`lint_plan`은 "안정적이면 통과" 같은 항목을 잡아야 한다. 판별식은 `metric`이 있고, `op`가 `>=`/`<=`이며, `value`가 숫자인가이다. 파이썬에서 `True`는 `int`의 인스턴스라서 `isinstance(v, (int, float))` 만으로는 `True`가 통과한다. bool을 따로 뺀다.

</details>

<details><summary>힌트 2 — 게이트에서 측정값이 없을 때</summary>

딕셔너리에 지표가 없으면 NO_GO다. 지표를 빠뜨리는 쉬운 방법이 "측정하지 않기"이기 때문이다. waiver는 `waivable` 항목에만 듣는다.

</details>

<details><summary>힌트 3 — critical path는 "늦은 선행"을 기억하면 끝</summary>

위상 정렬하며 `finish[t] = 기간 + max(finish[선행])`을 채우고, 그때 고른 선행을 `pred[t]`에 적어 두면 끝점에서 거슬러 경로가 나온다. 결정성을 위해 ready 큐를 정렬하고, 동률은 사전순으로 깬다.

</details>

<details><summary>힌트 4 — run_day0의 구조</summary>

`validate_stages`는 `try` 바깥, 맨 처음이다. 설정 오류면 벤치를 건드리지 않아야 하니 `safe_state()` 호출도 없어야 한다. `try` 안에서 스테이지마다: 이미 중단됐나 → skip인가 → 의존이 만족됐나 → 실행. 네 갈래를 이 순서로 본다.

</details>

<details><summary>힌트 5 — BLOCKED의 뿌리 찾기</summary>

의존 `d`가 BLOCKED이면 `d`의 `roots`를 물려받고, FAIL/ERROR/SKIPPED이면 `d` 자신이 뿌리다. 집합으로 모은 뒤 실행 순서로 정렬한다. `kind`는 뿌리들의 상태에서 온다: FAIL이 있으면 dut, ERROR만 있으면 infra, SKIPPED뿐이면 plan.

</details>

<details><summary>힌트 6 — 예외 세 종류</summary>

`SafetyAbort`는 재시도 금지, 전체 중단. `InstrumentError`는 ERROR로 기록하고 재시도 허용. 그 밖의 `Exception`은 스크립트 버그이므로 ERROR `E_HARNESS_BUG`, 재시도 없음. `except Exception`을 넓게 잡더라도 **FAIL로 돌리면 안 된다**.

</details>

<details><summary>힌트 7 — 시그니처에 뭘 넣나</summary>

`stage:code`만 넣는다. 측정값·보드 이름·시간이 들어가면 같은 원인이 보드마다 다른 시그니처가 된다 (SIV-M10). 정렬 후 CRC32처럼 결정적인 해시를 쓴다. 파이썬 `hash()`는 문자열에 대해 프로세스마다 달라진다.

</details>

## 5. 풀이

이 절이 곧 설계 문서다. 숫자는 표시가 없어도 전부 대표값/가정이다. 실제 프로젝트에서는 첫 주에 숫자를 그 프로젝트의 것으로 바꾸고, **가정을 적어 둔 표 자체를 리뷰 대상으로** 삼는다.

### Part A — 검증 계획서 (SENS-1)

**문서의 목적.** 이 계획의 산출물은 "테스트를 많이 돌렸다"가 아니라 네 가지 결정이다: (1) A0로 다음 단계에 갈 수 있나 (2) 금속 리스핀에 무엇을 넣나 (3) 어떤 errata와 동작 조건으로 내보내나 (4) 양산 테스트로 무엇을 넘기나. 단계 게이트는 이 결정을 내리는 시점이다.

**단계별 계획.** 출구 기준의 지표 이름은 코드의 `good_plan()`과 같다. `(waiver 가능)` 표시가 없는 항목은 예외 없이 지켜야 한다.

| 단계 | 기간 | 입구 기준 | 출구 기준 (전부 측정 가능) | 소유자 |
|---|---|---|---|---|
| **pre-si 준비** | T0−20주 ~ T0 | RTL 안정 후보, 레지스터 맵, bring-up 시퀀스 문서 | `day0_scripts_on_emulation_pct ≥ 100` · `debug_access_open_items ≤ 0` · `board_review_open_items ≤ 0` · `benches_commissioned ≥ 6`(waiver 가능) | validation lead |
| **Day-0** | T0 ~ +2주 | pre-si 게이트 통과, A0 샘플·보드 도착 | `boards_passing_day0 ≥ 3` · `open_blocker_bugs ≤ 0` · `os_boot_success_pct ≥ 95`(waiver 가능) | bring-up lead |
| **feature** | +2 ~ +8주 | Day-0 게이트 | `test_plan_executed_pct ≥ 95` · `test_plan_blocked_pct ≤ 5` · `test_plan_pass_pct ≥ 90`(waiver 가능) · `open_s1_bugs ≤ 0` | IP별 검증 리드 |
| **PVT** | +8 ~ +16주 | feature 게이트, 자동화 완료, 챔버 | `corners_completed_pct ≥ 100` · `samples_per_corner ≥ 5`(waiver 가능) · `vmin_guardband_mv ≥ 30` · `ddr_min_eye_ui_pct ≥ 30`(waiver 가능) | characterization lead |
| **stress** | +8 ~ +15주 | feature 게이트, 안정 펌웨어 고정 | `soak_hours_no_uncorrectable ≥ 168` · `serdes_ber_upper95_log10 ≤ −12` · `unexplained_failures ≤ 0` | system validation lead |
| **양산 이관** | A1 회귀 후 | PVT·stress 게이트 | `errata_documented_pct ≥ 100` · `ate_slt_correlation_pct ≥ 95`(waiver 가능) · `open_s1_bugs ≤ 0` | product test lead |

표에서 짚을 점:

- `test_plan_blocked_pct ≤ 5`가 pass율과 **따로** 있다. BLOCKED가 분모에서 빠지면 pass율은 얼마든지 올라간다.
- `unexplained_failures ≤ 0`은 "한 번 났다가 안 나는" 실패를 닫지 못하게 하는 조항이다. 이 항목은 waiver를 열어 두지 않았다. 원인을 모르는 실패를 들고 나가는 것은 결정이 아니라 도박이기 때문이다.
- BER 지표가 "측정값"이 아니라 **95% 상한**인 이유는 SIV-M06에서 유도했다. 오류 0개여도 비트 수가 모자라면 증명이 안 된다. 1e-12를 95%로 말하려면 약 3e12 비트가 필요하다.

**Pre-si 단계에서 만들어 두어야 하는 것.** 실리콘이 오기 전에 할 수 있는 일은 전부 그 전에 끝낸다. 도착한 다음 날 가장 비싼 자원은 사람이 아니라 칩 위의 시간이다.

| 산출물 | 왜 그때 해야 하나 |
|---|---|
| Day-0 스크립트를 에뮬레이터/FPGA에서 그대로 실행 (백엔드 교체, SIV-B13) | 실리콘 위에서 처음으로 스크립트 버그를 찾으면 칩 결함과 구분이 안 된다 |
| 디버그 접근 경로 확정: JTAG, UART, 부트 스트랩, 시큐어 부트 우회/테스트 키, 퓨즈 상태 | 잠긴 칩은 아무것도 말해주지 않는다. 설계 변경이 가능한 시점이 지나면 못 고친다 |
| 관측성 요청: PLL lock 상태, LTSSM 상태, DDR 훈련 결과, 부트 단계 표시(breadcrumb) | 디버그 훅은 테이프아웃 전에만 넣을 수 있다 |
| 보드 리뷰: 테스트 포인트, 소켓 여부, 전류 센스, 리셋·전원 시퀀스 제어 경로 | 칩 양쪽 신호에 프로브가 안 닿으면 보드와 칩을 분리할 수 없다 |
| bring-up 절차서: 무전원 저항 측정 → 낮은 전류 제한 첫 전원 → 열화상 → 클럭 확인 | 첫 전원 순간은 스크립트 이전의 사람 절차다. 샘플이 적을 때 태우면 일정이 사라진다 |
| 샘플 배분 계획, 계측기 예약, 챔버 확보, ID 체계(보드·칩 ECID·소켓) | 나중에 "어느 칩이었지?"가 되면 데이터가 쓸모 없어진다 |
| errata 추적기, 리비전별 워크어라운드 틀 (SIV-A05) | A1이 오는 날 무엇을 재검증할지가 여기서 나온다 |

**Day-0 순서.** 첫날의 원칙은 **낮은 에너지부터, 관측 수단이 먼저**다.

1. 무전원: 레일-GND 저항으로 단락 확인.
2. 첫 전원: 전류 제한을 낮게 잡고 레일을 하나씩 올린다. 전류 평상값과 열화상을 본다.
3. 스크립트 게이팅 시작: 전원 → 클럭 → JTAG IDCODE → 부트 ROM → SRAM → DDR 훈련 → PCIe → CSI-2/SerDes (Part C).
4. 첫 OS 부팅, 간단한 부하, 그리고 **보드 3장에서 같은 결과**를 확인. 한 장 통과는 우연일 수 있다.

**IP × 단계 커버리지.** 칸은 그 단계에서 답해야 하는 질문이다. 숫자가 들어가는 측정 방법은 괄호의 레슨이 소유한다.

| IP | Day-0 | feature | PVT | stress | 이관 |
|---|---|---|---|---|---|
| 전원·클럭 | 레일 범위, 전류, refclk ppm, PLL lock (SIV-A06) | 전원 시퀀스, 저전력 진입/복귀, 도메인 격리 (SIV-A11) | Vmin/Fmax shmoo (SIV-M07), 전력 특성화 (SIV-A03), 열·스로틀 (SIV-A04) | 전력 스텝 부하에서 droop | 스크린 항목 고정, 수율 연계 (SIV-A08) |
| 부트·보안 | 부트 ROM 완료, IDCODE, 부트 표시 | 부트 경로 전 조합, 실패 복구 | 코너별 부트 성공률 | 전원 사이클 수천 회 | 양산 키·퓨즈 절차 |
| DDR | 훈련 완료, lane별 eye (SIV-M05) | 전 속도 등급, 패턴 스트레스, ECC (SIV-M14) | 코너별 eye 폭 | 장시간 트래픽 + refresh, 온도 변동 | 양산 스크린 패턴 |
| PCIe | L0 도달, 속도·폭 (SIV-M04) | 전 Gen 전환, 저전력 상태, 오류 주입, 호스트 상호운용 | 수신 마진 (PCIe 4.0의 Lane Margining) | 장시간 DMA + 재학습 카운트 (SIV-M02) | ATE 커버리지 상관 |
| CSI-2 | 테스트 패턴 수신, CRC 0 | 레인 수·속도·데이터 타입·가상 채널 조합, 실제 센서 연동 | lane rate 마진, 코너별 오류 | 다중 카메라 + DDR 부하 동시 | 양산 BIST 경로 |
| SerDes | 링크 lock, PRBS 스모크 (SIV-M06) | 등화 설정, 케이블, lock 손실 복구 | 코너별 BER 마진 | 장시간 BER, 동시 트래픽 | ATE 패턴 |

CSI-2 관련 사실만 정리한다. CSI-2 긴 패킷은 헤더(Data ID, Word Count, D-PHY에서는 ECC)와 페이로드, 16비트 CRC로 구성된다. 그래서 **헤더 ECC 정정 카운트와 페이로드 CRC 오류는 다른 신호**다. RX인 SoC에는 자체 TX가 없으므로 "루프백"은 내부 테스트 패턴 소스, 외부 패턴 발생기, 또는 FPGA 기반 TX 중 하나로 만들어야 한다. SENS-1은 내부 테스트 패턴 소스가 있다고 **가정**했다. 그 경로는 실제 센서의 타이밍을 대신하지 못하므로, feature 단계에서 실제 센서 연동이 따로 필요하다.

**이 계획서가 다루지 않는 것.** 기능안전(ISO 26262의 FMEDA·안전 메커니즘 검증), 신뢰성 자격시험(AEC-Q100의 수명·스트레스 시험), 보안 검증은 각각 소유 팀이 있고, 이 계획과의 연결점(안전 메커니즘 주입 테스트 공유, 품질팀 샘플 분배)만 합의한다.

**코드로 계획을 lint하는 이유.** 계획서가 산문이면 "owner: TBD"와 "안정화되면 통과"가 살아남는다. 데이터로 두면 PR에서 잡힌다. 심어 둔 9종 결함을 모두 정확한 위치로 잡는다(§6 실행 결과).

### Part B — 인프라, triage, 일정, 인력, 지표

**인프라.** 핵심 요구는 네 가지다: (1) 사람 없이 밤에 돌고 멈춘 보드를 스스로 복구 (2) 실패 하나를 나중에 **똑같은 조건으로** 재현 (3) 계측 고장과 칩 결함을 구분 (4) 아침에 무엇을 먼저 볼지 알려준다.

```svg
<svg viewBox="0 0 760 266" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a09c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">랩 인프라: 계획에서 대시보드까지</text>
  <g stroke="currentColor">
    <rect x="10" y="44" width="138" height="72" rx="8" class="f-acc"/>
    <rect x="178" y="44" width="150" height="72" rx="8" class="f-mut"/>
    <rect x="358" y="44" width="178" height="72" rx="8" class="f-warn"/>
    <rect x="566" y="44" width="184" height="72" rx="8" class="f-mut"/>
    <rect x="358" y="150" width="178" height="96" rx="8" class="f-ok"/>
    <rect x="566" y="150" width="184" height="96" rx="8" class="f-acc"/>
    <rect x="10" y="150" width="318" height="96" rx="8" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-weight="bold">
    <text x="79" y="66">테스트 리스트</text><text x="253" y="66">시퀀서 (M12)</text>
    <text x="447" y="66">벤치 HAL</text><text x="658" y="66">DUT 보드</text>
    <text x="447" y="172">결과·아티팩트 저장</text><text x="658" y="172">대시보드 · triage</text>
    <text x="169" y="172">벤치 건강 관리</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="79" y="86">코너·seed·요구조건</text><text x="79" y="104">git 관리</text>
    <text x="253" y="86">재시도·타임아웃</text><text x="253" y="104">FLAKY·SKIP 기록</text>
    <text x="447" y="86">PSU·챔버·프로브·릴레이</text><text x="447" y="104">전부 mock으로 교체 가능</text>
    <text x="658" y="86">전원 릴레이 + 워치독</text><text x="658" y="104">ECID · 소켓 · 보드 rev</text>
    <text x="447" y="194">DB: 시도 단위 결과</text><text x="447" y="212">로그·덤프: 불변 저장</text><text x="447" y="230">fw·seed·ECID·온도 기록</text>
    <text x="658" y="194">시그니처 클러스터 (M10)</text><text x="658" y="212">소유자 자동 라우팅</text><text x="658" y="230">분모 포함 추세</text>
    <text x="169" y="194">golden 보드 매일 실행 → 벤치 자체 PASS 필요</text>
    <text x="169" y="212">계측기 자가 진단 · 교정 만료 추적</text>
    <text x="169" y="230">bench 건강이 나쁘면 그 bench 결과는 ERROR</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#a09c)">
    <path d="M148 80 L177 80"/><path d="M328 80 L357 80"/><path d="M536 80 L565 80"/>
    <path d="M447 116 L447 149"/><path d="M536 198 L565 198"/><path d="M357 198 L329 198"/>
  </g>
</svg>
```

재현 가능성을 위해 **결과와 함께 묶어 저장하는 것**: 칩 ECID, 보드 ID와 리비전, 펌웨어·스크립트 해시, seed, 설정 전압·온도와 *측정된* 전압·온도, 계측기 일련번호, 워크어라운드 목록. "그때 어떤 펌웨어였지?"라고 묻게 되면 그 결과는 버리는 것과 같다.

**Triage 흐름.** 첫 질문은 "무슨 버그인가"가 아니라 **"믿을 수 있는 실패인가"** 다.

```svg
<svg viewBox="0 0 760 232" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a09d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">실패 한 건의 경로</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="112" height="60" rx="8" class="f-warn"/>
    <rect x="146" y="40" width="130" height="60" rx="8" class="f-mut"/>
    <rect x="300" y="40" width="130" height="60" rx="8" class="f-mut"/>
    <rect x="454" y="40" width="130" height="60" rx="8" class="f-mut"/>
    <rect x="608" y="40" width="142" height="60" rx="8" class="f-acc"/>
    <rect x="146" y="138" width="130" height="60" rx="8" class="f-ok"/>
    <rect x="300" y="138" width="130" height="60" rx="8" class="f-ok"/>
    <rect x="454" y="138" width="130" height="60" rx="8" class="f-ok"/>
    <rect x="608" y="138" width="142" height="60" rx="8" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="66" y="66" font-weight="bold">실패 발생</text><text x="66" y="86" font-size="12">시그니처</text>
    <text x="211" y="66" font-weight="bold">벤치 건강?</text><text x="211" y="86" font-size="12">golden 보드</text>
    <text x="365" y="66" font-weight="bold">재현되나?</text><text x="365" y="86" font-size="12">같은 seed·조건</text>
    <text x="519" y="66" font-weight="bold">격리</text><text x="519" y="86" font-size="12">보드·칩·소켓 교체</text>
    <text x="679" y="66" font-weight="bold">원인 분류</text><text x="679" y="86" font-size="12">칩·보드·FW·테스트</text>
    <text x="211" y="164" font-weight="bold">랩 수리</text><text x="211" y="184" font-size="12">재실행, 버그 X</text>
    <text x="365" y="164" font-weight="bold">간헐 추적</text><text x="365" y="184" font-size="12">통계·반복 + 메모</text>
    <text x="519" y="164" font-weight="bold">보드 문제</text><text x="519" y="184" font-size="12">보드 팀, 리워크</text>
    <text x="679" y="164" font-weight="bold">errata / ECO</text><text x="679" y="184" font-size="12">워크어라운드·검증</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#a09d)">
    <path d="M122 70 L145 70"/><path d="M276 70 L299 70"/><path d="M430 70 L453 70"/><path d="M584 70 L607 70"/>
    <path d="M211 100 L211 137"/><path d="M365 100 L365 137"/><path d="M519 100 L519 137"/><path d="M679 100 L679 137"/>
  </g>
  <text x="10" y="222" fill="currentColor" font-size="12">아래로 내려가는 길은 각각 "아니요 / 재현 안 됨 / 보드 따라감 / 칩·설계 쪽"의 경우다.</text>
</svg>
```

| 원인 분류 | 근거로 쓰는 실험 | 처리 |
|---|---|---|
| 실리콘 설계 | 여러 칩, 여러 보드에서 동일. 시뮬/에뮬에서 재현되면 확정 (SIV-B13) | 버그 → errata 또는 ECO 후보 → 리스핀 판단 |
| 실리콘 개별 칩 | 그 칩만 따라다니고 소켓을 옮겨도 동일 | 샘플 격리, 불량 분석 |
| 보드 | 칩을 옮기면 사라지고 보드에 남음 | 보드 팀, 리워크, 해당 보드 데이터 제외 |
| 펌웨어 | 다른 칩·보드에서 동일, 펌웨어를 바꾸면 사라짐 (bisect, SIV-M11) | 펌웨어 수정 |
| 테스트/스크립트 | 스펙과 기대값이 다름, 또는 스크립트가 잘못됨 | 테스트 수정, 지표에서 false-fail로 집계 |
| 랩/계측 | golden 보드도 실패, 계측기 진단 실패 | 랩 수리, 결과는 ERROR |

처리 약속(대표값): S1 실패는 24시간 안에 소유자 지정, 재현 불가 건은 72시간 안에 "추적 방법" 명시, 칩 결함 후보는 주 1회 리스핀 후보 회의.

**일정.** 주 단위 모델을 계산했다(실행 결과 그대로):

```text
critical path (weeks): (47, ['silicon_fab', 'day0_bringup', 'ddr_feature', 'a1_metal_spin', 'a1_regression', 'production_handoff'])
critical path, no respin  : (39, ['silicon_fab', 'day0_bringup', 'ddr_feature', 'pvt_char', 'production_handoff'])
```

읽는 법: **리스핀이 있으면 일정은 PVT가 아니라 리스핀이 정한다.** 이 경로에서 검증팀이 줄일 수 있는 것은 리스핀 **결정 시점**, 즉 feature를 얼마나 빨리 끝내느냐이다. 그리고 한 가지 함정이 있다. 이 모델에서 PVT는 A0에 대해 돌지만, 실제 출하는 A1이다. 그래서 A0의 PVT는 **설계 마진 확인과 리스핀 입력**이 목적이고, A1 회귀 4주 안에 코너를 다시 확인할 항목(전압·타이밍에 영향 주는 ECO가 들어간 IP)을 미리 정해 둔다. 금속 ECO가 어디까지 영향을 주는지 모른 채 "A0 PVT 재사용"이라고 답하면 꼬리질문에 무너진다.

**벤치 용량.** 수요 = IP별 시간 × 코너 × 샘플이다. 대표값: DDR 16h, PCIe 6h, CSI-2 8h, SerDes 10h, 전력·열 4h (코너당·샘플당, 소크 포함), 9 코너, 샘플 5개, 벤치 가동률 70%, 하루 20시간, 주 6일.

```text
PVT capacity weeks @ 4/6/8 benches: ['5.9', '3.9', '2.9']
```

계획의 PVT 8주에 벤치 4대면 5.9주로, 재측정과 고장 여유로 약 2주가 남는다. **제약은 대개 벤치 대수가 아니라 챔버 소크와 사람이 한 코너의 결과를 소화하는 속도**라서, 벤치만 늘리면 2.9주가 되지 않는 경우가 많다.

**Triage 적체.** 하루 60건 실패, 분류 인력 4명, 인당 하루 1.5건 분류일 때 10일 뒤 대기열:

```text
   dedup 0.50 -> 240.0
   dedup 0.80 -> 60.0
   dedup 0.90 -> 0.0
   dedup 0.95 -> 0.0
```

중복 제거 비율이 80%에서 90%로 오르는 것이 사람 두 명 추가보다 싸다. 시그니처 품질(SIV-M10)이 인력 계획의 변수인 이유다. 여기 "분류"는 소유자 지정까지이고, 근본 원인 분석은 별도의 시간이다.

**인력 (피크, 대표값).**

| 역할 | 인원 | 주 책임 |
|---|---|---|
| 검증 리드 | 1 | 계획, 게이트, 위험, 리스핀 회의 |
| Bring-up / 보드 | 2 | Day-0, 보드 리워크, 전원·클럭 |
| 전원·열·특성화 | 2 | Vmin, 전력, 열, 데이터 분석 |
| DDR | 2 | 훈련 분석, 스트레스 |
| PCIe | 1.5 | 링크, DMA, 호스트 상호운용 |
| CSI-2 | 1.5 | 패턴·센서 연동, 레인 매트릭스 |
| SerDes | 1.5 | 링크, BER, 케이블 |
| 시스템·stress | 2 | 동시 부하, 장시간 |
| 랩 자동화 | 2 | 시퀀서, 벤치, 대시보드, 시그니처 |
| 랩 기술자 | 2 | 보드 세팅, 소켓, 프로브 |

합계 약 17.5명이다. 설계 팀 온콜 없이는 Day-0가 돌아가지 않으니, IP 설계자의 "1주간 온콜"을 일정에 미리 넣는다. 자동화 2명을 첫 번째로 깎으면 안 되는 이유는 Part B의 적체 표가 설명한다.

**지표.** 지표마다 어떻게 왜곡되는지까지 함께 적어 둔다.

| 지표 | 무엇을 말해주나 | 왜곡되는 방법 | 함께 볼 것 |
|---|---|---|---|
| 테스트 계획 실행률 | 얼마나 돌렸나 | 쉬운 테스트만 먼저 | 위험 가중 실행률 |
| pass율 | 현재 상태 | BLOCKED·SKIP 제외, 테스트 삭제 | BLOCKED 비율, 분모 변화 |
| 버그 유입/해결 추세 | 수렴 여부 | 안 돌면 유입도 준다 | 같은 기간 실행 시간 |
| false-fail 비율 | 인프라·테스트 품질 | 분류를 미룸 | 미분류 건수 |
| 평균 분류 시간, 평균 근본 원인 시간 | triage 건강 | 쉬운 건만 닫음 | 가장 오래된 미해결 |
| bench 가동률 | 자원 활용 | 쓸모없는 잡으로 채움 | 잡 중 유의미한 결과 비율 |
| 커버리지 (스펙 항목, SIV-M13) | 무엇을 안 봤나 | 항목을 넓게 정의 | 항목 정의 리뷰 |

**위험 목록.** 가능성·영향은 H/M/L 추정이다.

| 위험 | 가능성 | 영향 | 조기 신호 | 대응 |
|---|---|---|---|---|
| 첫 보드 전원/단락 | M | H | 저항 측정, 낮은 전류 제한 | 샘플 태우지 않는 절차, 보드 예비 |
| 디버그 접근 불가 (퓨즈/보안) | M | H | 에뮬레이션 단계 확인 | 잠금 해제 샘플, 테스트 키 |
| DDR 훈련 불안정 | H | H | lane별 eye 분포 | PHY 벤더 온콜, 속도 단계 낮춰 진행 |
| PCIe 상호운용 (호스트별) | M | H | 특정 호스트에서만 실패 | 호스트 2종 이상 |
| CSI-2 실제 센서 조합 | M | M | 패턴은 통과, 센서만 실패 | 초기 센서 확보 |
| SerDes 케이블·채널 | M | M | BER이 케이블 따라감 | 단계적 채널 격리 |
| 금속 ECO 필요 (리스핀) | H | H | feature 초기 S1 | 결정 회의, ECO 후보 목록 |
| 챔버·장비 고장 | M | M | 교정·유지보수 기록 | 외부 랩 계약 |
| 샘플 부족 | M | H | 소모량 추적 | 배분 우선순위 |
| 시그니처 품질 나쁨 | M | M | 미분류 건수 | 자동화 투자 유지 |

**슬립 시 자르는 순서.** `best_cut`의 실행 결과다.

```text
what to cut, weeks needed -> best_cut:
   1 -> risk 1   saves 1  dashboard_polish
   2 -> risk 3   saves 2  serdes_cable_matrix_trim
   3 -> risk 4   saves 3  dashboard_polish,serdes_cable_matrix_trim
   4 -> risk 6   saves 4  dashboard_polish,pcie_low_rate_char,serdes_cable_matrix_trim
   5 -> risk 9   saves 5  csi2_lane_rate_matrix_trim,dashboard_polish,serdes_cable_matrix_trim
   6 -> risk 11  saves 6  dashboard_polish,pvt_corners_9_to_5,serdes_cable_matrix_trim
```

세 가지를 같이 말한다.

- **먼저 자를 것**: 보고용 다듬기, 저위험 조합 매트릭스, 코너 축소의 앞부분(단, 축소한 코너는 "A1에서 보충"으로 기록). `must_keep`(전원 시퀀스·열 트립, 안전 중단 경로)은 절약 주수가 어떻든 자르지 않는다.
- **표의 "절약 주"는 임계 경로에서 줄어든다고 팀이 추정한 값**이다. 슬랙이 있는 작업을 잘라도 끝 날짜는 움직이지 않는다. 이 모델에서 PVT를 3주 잘라도 끝은 47주 그대로이고(연습 3에서 계산), 리스핀 경로를 3주 줄여야 3주가 당겨진다. 즉 PVT를 자르는 결정은 슬랙이 없을 때의 카드다.
- 일정을 정하는 사람이 내 범위 밖이면 "자른 항목 목록과 위험"을 들고 올라간다. 무엇을 포기했는지 기록이 남아야 나중에 그 결정의 책임을 물을 수 있다.

### Part C — Day-0 스크립트 골격

**상태값이 일곱 개인 이유.** 아침에 볼 사람의 다음 행동이 다 다르다.

| 상태 | 뜻 | 다음 행동 |
|---|---|---|
| PASS | 측정했고 스펙 안 | 없음 |
| FAIL | 측정했고 스펙 밖 | 칩/보드 디버그 |
| DEGRADED | 스펙 안이지만 목표 미달 (Gen1로 L0, 마진 작음) | 다음 스테이지는 진행, 기록 |
| FLAKY | 재시도로 통과 | 다음 스테이지 진행, 원인 추적 |
| ERROR | 계측·스크립트 문제. **칩에 대해 정보 없음** | 랩/스크립트 수리, 재실행 |
| BLOCKED | 앞이 안 돼서 못 돌림 | 뿌리를 고치면 자동 해소 |
| SKIPPED | 계획에서 제외 | 계획 검토 |

"BLOCKED"와 "FAIL"을 합치면 벌어지는 일: DDR이 죽은 보드 한 장이 CSI-2까지 FAIL로 올려 CSI-2 팀을 깨운다. 아래 측정에서 다시 확인한다.

**의존성이 사슬이 아니라 DAG인 이유.** 과제가 나열한 순서(전원 → 클럭 → JTAG → ROM/SRAM → DDR → PCIe → CSI-2/SerDes)는 **실행 순서**이고, 의존성은 물리와 관측 경로에서 나온다.

| 스테이지 → 의존 대상 | 근거 |
|---|---|
| clock → power | 레일이 있어야 오실레이터·PLL이 돈다 |
| jtag_idcode → power (clock 아님) | TCK는 프로브가 공급한다. PLL이 죽어도 IDCODE는 읽히고, PLL 상태를 읽는 통로가 남는다 |
| boot_rom → clock, jtag_idcode | ROM은 코어 클럭으로 돈다. JTAG은 **관측 경로**로 필요 (부트 표시를 읽는다) |
| sram → boot_rom | 펌웨어 적재·테스트 실행 전제 |
| ddr_training, pcie_link, serdes_loopback → sram | 훈련·링크 초기화 펌웨어가 SRAM에 있다고 가정 |
| csi2_loopback → ddr_training | 프레임 캡처 버퍼가 DDR에 있다고 가정 |

**결정 규칙 요약.** 스테이지는 이 순서로 판정한다: 이미 안전 중단 → 계획상 skip → 의존 불만족(BLOCKED, 뿌리 기록) → 실행. 실행 중 예외는 세 종류다: 안전 중단(재시도 금지, 전체 중단), 계측 오류(ERROR, 재시도 허용), 그 밖(스크립트 버그, ERROR). 재시도로 통과하면 FLAKY다. `safe_state()`는 `finally`에서 한 번.

`verdict`는 ABORTED > FAIL > INCONCLUSIVE > DEGRADED > PASS 순이고 exit code는 0(PASS) · 1(FAIL/ABORTED) · 2(설정 오류) · 3(INCONCLUSIVE) · 4(DEGRADED)다. **INCONCLUSIVE를 1과 분리한 이유**: CI가 칩 FAIL과 랩 고장을 같은 코드로 받으면 자동 재실행 규칙을 못 짠다.

**캠페인 실행 결과** (`python3 -I solution.py --demo`, 보드 8장, 결함 심음):

```text
=== Day-0 triage: BRD-03  chip=SENS-1 A0  script=day0-0.3  T=25C ===
verdict  : FAIL (exit 1)   signature 578c6aea
counts   : BLOCKED=1  FAIL=1  PASS=7
root     : ddr_training     FAIL   E_DDR_LANE           owner=memory attempts=1
  detail : lane 3 eye 0% UI < 15%
  evidence: {"die_c": 47.0, "eye_ui_pct": [41, 44, 39, 0], "lane": 3}
  next   : Dump per-lane training results; swap DRAM/board; check that lane's DQS routing.
blocked  : csi2_loopback    <- ddr_training             [dut]

=== Day-0 triage: BRD-07  chip=SENS-1 A0  script=day0-0.3  T=25C ===
verdict  : INCONCLUSIVE (exit 3)   signature 84553faf
counts   : BLOCKED=6  ERROR=1  PASS=2
root     : jtag_idcode      ERROR  E_PROBE_LOST         owner=lab-infra attempts=2
  detail : debug probe USB disappeared
  evidence: {}
  next   : Bench problem. Reconnect or swap probe, re-run; do not file a silicon bug.
blocked  : boot_rom         <- jtag_idcode              [infra]
blocked  : sram             <- boot_rom                 [infra]
blocked  : ddr_training     <- sram                     [infra]
blocked  : pcie_link        <- sram                     [infra]
blocked  : csi2_loopback    <- ddr_training             [infra]
blocked  : serdes_loopback  <- sram                     [infra]

=== Day-0 triage: BRD-08  chip=SENS-1 A0  script=day0-0.3  T=25C ===
verdict  : ABORTED (exit 1)   signature 815c48a5
counts   : BLOCKED=8  FAIL=1
root     : power            FAIL   E_OVERCURRENT        owner=power-integrity attempts=1
  detail : VDD_DDR draws 12.4 A > limit 4.0 A
  evidence: {"amps": 12.4, "rail": "VDD_DDR", "volts": 0.93}
  next   : Keep rail off. Measure rail-to-GND resistance unpowered, thermal-camera at low current limit.
blocked  : clock            <- power                    [safety]
  ...
Campaign summary (8 boards)
signature verdict       count attribution            root / boards
578c6aea  FAIL          4     SUSPECT_SYSTEMATIC     ddr_training:E_DDR_LANE  BRD-03,BRD-04,BRD-05,BRD-06
815c48a5  ABORTED       1     SUSPECT_BOARD_OR_DIE   power:E_OVERCURRENT  BRD-08
84553faf  INCONCLUSIVE  1     LAB_INFRA              jtag_idcode:E_PROBE_LOST  BRD-07
b77a700d  FAIL          1     SUSPECT_BOARD_OR_DIE   pcie_link:E_PCIE_STUCK_DETECT  BRD-02
```

BRD-06에는 DDR 결함과 **CSI-2 CRC 결함을 함께** 심어 두었다. 시그니처는 BRD-03과 똑같이 `578c6aea`이고, CSI-2는 BLOCKED로만 나타난다. 패킷은 이 보드의 CSI-2 상태를 **모른다**고 말할 뿐 정상이라고 말하지 않는다. 이것이 BLOCKED를 PASS로 세면 안 되는 이유다.

분모에 대한 주의: `INCONCLUSIVE` 보드 BRD-07은 분모에서 뺀다. 측정이 안 된 보드로 "4/8"을 말하면 시스템적 결함의 강도가 잘못 표현된다.

**게이팅의 한계 (측정값).** 단일 결함 9종, 결함 쌍 36개를 세 모드로 돌렸다.

```text
gating limits (9 single faults, 36 fault pairs):
mode       avg_measured  false_fails    both_detected
chain              5.00            0          0/36
dag                5.67            0          6/36
no_gating          9.00           30         29/36
```

| 지표 | 뜻 | 해석 |
|---|---|---|
| avg_measured | 단일 결함당 PASS/FAIL로 판정된 스테이지 평균 | DAG가 사슬보다 많다. PLL 실패 때 JTAG을, DDR 실패 때 PCIe를 잃지 않는다 |
| false_fails | 주입 결함이 없는 스테이지가 FAIL로 찍힌 횟수 | 게이팅이 없으면 30건, 즉 다른 팀을 잘못 깨운 횟수 |
| both_detected | 결함 쌍을 올바른 코드로 둘 다 찾은 수 | no_gating이 29/36으로 높지만 위의 거짓 FAIL 30건과 교환한 값이다 |

DAG의 6/36이 정직한 한계다. 게이팅은 **결함 가림을 줄일 뿐 없애지 못한다**: 앞 단계가 죽으면 뒤 단계의 사실을 알 수 없다. 사슬이 0/36인 것은 그 사슬이 "첫 결함 이후 아무것도 안 본다"는 뜻이다. 실제 대응은 첫 결함을 고친 다음 재실행(연습 1)이고, 진짜 칩 결함이 많은 A0에서는 이 재실행 횟수가 Day-0 기간을 정한다. "왜 2주인가"에 대한 답이기도 하다.

게이팅이 있어도 Day-0가 못 보는 것:

- **코너 전용 결함.** 테스트 `limit: cold-only defect is invisible`: 25 ℃에서 PASS, −40 ℃에서 FAIL. Day-0 PASS는 "칩이 살아 있다"이지 "칩이 맞다"가 아니다.
- **표본.** 보드 2장이면 `INSUFFICIENT_SAMPLES`다. 귀속은 표본 없이는 안 된다.
- **귀속 자체의 한계.** `SUSPECT_SYSTEMATIC`은 가설이다. 4장이 같은 보드 제조 lot, 같은 DRAM 벤더, 같은 라우팅이면 설계 결함과 보드 결함이 **교란**된다. 칩·보드 교체 실험 없이 설계 버그로 단정하면 안 된다.

<details><summary>작성 메모 — 사슬을 그대로 쓰지 않은 이유</summary>

과제의 사슬은 스테이지가 **나열되는 순서**로 해석했다. 코드도 `day0_stages()`의 순서가 그 사슬과 같다. 다만 물리적으로 필요 없는 의존까지 게이팅하면 진단 정보를 잃는다는 점이 면접에서 강한 답이 되므로, `chain_stages()`로 원래 사슬도 돌려 비교할 수 있게 했다.

</details>

### 45분 구두 답변 개요

면접 방식에 맞춰 말할 순서다. 칠판에 쓸 것을 괄호에 적었다.

| 시간 | 말할 내용 | 그릴 것 / 주의 |
|---|---|---|
| 0–3분 | 범위 확인 질문: 샘플 수, 리스핀 전제, 자동차 등급, 기능안전 범위, 팀 규모, 첫 실리콘 날짜, 내가 맡는 범위 | 질문 3~4개로 끝낸다. 답이 없으면 가정을 말하고 진행 |
| 3–7분 | 산출물은 네 가지 결정 (진행 / 리스핀 내용 / errata·동작조건 / 양산 이관) | 네 줄 |
| 7–14분 | 6단계와 게이트. 각 단계에 **측정 가능한 출구 기준 1개씩**, 소유자 | 단계 그림 |
| 14–22분 | Day-0 깊게: pre-si에서 스크립트 검증, 첫 전원 절차, 의존성 DAG, FAIL/ERROR/BLOCKED 구분, 안전 중단 | DAG 그림. 여기가 이 답의 중심 |
| 22–29분 | IP별 한 줄씩: 무엇이 Day-0, 무엇이 feature, 무엇이 PVT인가 | IP × 단계 표 |
| 29–35분 | 인프라와 triage: 믿을 수 있는 실패인가 → 재현 → 격리 → 분류 | 인프라 박스, 흐름도 |
| 35–40분 | 일정·인력·지표: 리스핀이 임계 경로, 벤치 용량, 시그니처가 인력 변수 | 숫자 3개만 |
| 40–44분 | 위험 3개와 자르는 순서, 절대 안 자르는 것 | 표 없이 말로 |
| 44–45분 | 요약 + "맡게 되면 첫 달에 확인할 것" | 한 문장 |

운영 팁: 30분이 지났는데 Day-0도 안 끝났다면 IP 표를 줄인다. 면접관이 한 영역을 파고들면 그 질문에 따라가되, 마지막 5분은 위험과 자르는 순서를 위해 남긴다. "회사에서는 실제로 어떻게 하세요?"는 숨길 필요 없이 물어볼 수 있는 질문이다. 자기 경험(SSD 펌웨어의 양산 이관, RF 통합 등)을 끌어올 지점도 미리 정해 둔다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py; echo "exit=$?"                # 82/82 passed, exit=0
python3 -I -W error -X dev solution.py > /dev/null; echo $?  # 경고 0개, 0
python3 -I solution.py --demo                         # 위의 캠페인 · 일정 · 한계 표
```

실제 출력 (일부):

```text
-- Part A: plan lint + gates --
PASS  good plan: no findings
PASS  lint: drop feature -> E_ORDER @*
PASS  lint: no owner day0 -> E_NO_OWNER @day0
PASS  lint: bool value -> E_VAGUE_EXIT @stress
PASS  lint: coverage hole -> E_COVERAGE_HOLE @pvt
PASS  gate: missing metric is NO_GO, not a pass
-- Part B: schedule, capacity, cuts --
PASS  critical path of SENS-1 schedule
PASS  best_cut risk == independent brute-force minimum, need 1..10
PASS  limit: greedy by risk/week can be worse than exact
-- Part C: gating + classification --
PASS  single fault ddr_lane       -> FAIL E_DDR_LANE, 1 blocked, FAIL
PASS  single fault probe_lost     -> ERROR E_PROBE_LOST, 6 blocked, INCONCLUSIVE
PASS  overcurrent: abort, no further bench op, safe_state once
PASS  script exception -> ERROR E_HARNESS_BUG, never silicon FAIL
-- limits: what gating and Day-0 cannot see --
PASS  masking: BRD-06 has a CSI-2 defect, packet can only say csi2 BLOCKED
PASS  limits: DAG measures more than chain, no false FAIL, masking remains
PASS  limit: cold-only defect is invisible to a room-temp Day-0
PASS  limit: without gating one dead rail yields 9 FAILs (8 false)
82/82 passed
```

수치 비교의 허용 오차: `capacity_weeks`와 `triage_backlog`은 부동소수 산술 결과를 정확한 값과 비교하므로 1e-9 (부동소수 반올림 오차만 허용, 측정 잡음이 없는 계산이다). 나머지 판정은 상태·코드 문자열의 정확한 일치다.

## 7. 자주 하는 실수

**1. 재시도 통과를 PASS로 기록.** 간헐 실패가 지표에서 사라진다. 첫 시도 코드를 남기고 FLAKY로 올린다.

```python
if last.status == "PASS" and len(attempts) > 1:
    r.update(status="FLAKY", code=attempts[0].code)
```

**2. `except Exception` → FAIL.** 파이썬 `KeyError`가 "실리콘 결함"이 되어 해당 IP 팀이 밤새 디버그한다. 스크립트 예외는 ERROR `E_HARNESS_BUG`, 소유자 validation-sw.

**3. 프로브 끊김을 FAIL로 집계.** "JTAG 불량 칩 1개"가 대시보드에 남고 나중에 ECO 후보에 오른다. `InstrumentError`를 분리하고 verdict는 INCONCLUSIVE, exit 3.

**4. 안전 중단 경로에 `safe_state()` 없음.** 과전류로 예외가 올라왔는데 레일이 켜진 채 끝나면 그것이 샘플을 태운다. 중단·예외·정상 경로 모두 `try/finally` 하나로 처리하고, 호출 횟수를 테스트한다.

**5. 물리에 없는 의존으로 게이팅.** 사슬을 그대로 쓰면 DDR이 죽었을 때 PCIe 진단을 못 얻는다. 간선마다 근거(물리 또는 관측 경로)를 적어 리뷰한다. 반대로 물리 의존을 빼면(under-gating) 거짓 FAIL이 생긴다. 연습 5의 lint가 둘 다 잡는다.

**6. BLOCKED를 "통과하지 않음"으로 뭉갬.** 분모에서 빠지면 pass율이 부풀고, FAIL에 합치면 거짓 경보다. 상태를 분리하고 `blocked_pct`를 게이트에 넣는다.

**7. 시그니처에 측정값 포함.** `lane 3 eye 0%`가 그대로 들어가면 lane·보드마다 시그니처가 갈라져 분류 인력이 곱으로 든다. `stage:code`만 해시한다.

**8. 지표 누락을 통과로 처리.** `metrics.get(m, 0) >= v`처럼 쓰면 기본값이 판정을 대신한다. 없으면 `missing`, 결과는 NO_GO.

**9. 슬랙 있는 작업을 잘라 놓고 일정 단축이라고 보고.** 끝 날짜가 안 움직인다. 먼저 critical path를 계산하고 그 위의 것만 자른다 (연습 3).

**10. 측정 없는 BER 통과.** 무오류 소량 샘플을 "BER 통과"로 기록. Day-0는 3e9 비트 스모크일 뿐이며(95% 상한 약 1e-9), 1e-12 주장은 stress의 상한 지표로만 한다.

## 8. 면접 Q&A

<details><summary>Q1. Day-0 스크립트에서 PLL이 lock 안 됐는데 왜 JTAG을 더 돌리나요?</summary>

TCK는 프로브가 공급하므로 IDCODE 읽기는 코어 PLL에 의존하지 않고, PLL 상태와 클럭 설정을 읽을 통로가 JTAG이기 때문이다. 게이팅은 "물리적으로 소용없는 측정"을 막는 것이지 진단 경로를 끊는 것이 아니다. 측정값으로는 사슬 대비 DAG가 단일 결함당 판정 스테이지가 5.00 → 5.67이다.

**꼬리질문: DAG가 항상 낫나요?** 간선이 틀리면 거짓 FAIL이나 진단 손실이 나오므로, 간선마다 물리·관측 근거를 적어 리뷰하고 에뮬레이터에서 확인해야 한다. 근거를 대지 못하면 사슬로 시작하는 게 안전한 기본값이다. 사슬은 정보를 잃을 뿐 거짓 경보는 만들지 않는다.

**꼬리질문: 물리 의존을 어떻게 아나요?** 설계 문서와 클럭·리셋 도메인 문서, 그리고 에뮬레이터에서 앞 단계를 고의로 끄는 실험이다. 스크립트의 가정을 칩이 도착하기 전에 시험할 수 있다.

</details>

<details><summary>Q2. BLOCKED, FAIL, ERROR를 왜 구분하나요? 결국 다 안 된 거 아닌가요?</summary>

다음 행동이 다르다. FAIL은 칩 또는 보드를 디버그하고, ERROR는 랩을 고치고, BLOCKED는 뿌리를 고치면 저절로 없어진다. 합치면 측정 결과가 말해주지 않는 칩 불량이 대시보드에 쌓인다. 측정값 기준으로 게이팅 없이 단일 결함 9종을 돌리면 거짓 FAIL이 30건이다.

**꼬리질문: 프로브가 끊긴 건지 칩이 죽은 건지 어떻게 아나요?** 프로브의 target voltage(VTref) 감지, golden 보드 또는 다른 프로브로 교차 확인, 프로브 자가 진단을 쓴다. 스크립트만으로 확신 못 하면 ERROR로 두고 사람을 부른다.

**꼬리질문: 스크립트 버그는요?** ERROR `E_HARNESS_BUG`. 이게 실리콘 FAIL로 찍히면 신뢰가 무너지는 데 하루면 충분하다.

</details>

<details><summary>Q3. 이 계획이 잘 되고 있는지 어떻게 아나요?</summary>

뒤따라오는 지표(pass율)보다 앞서는 지표를 본다: 실행 대비 새 버그 유입률, 재현 실패 비율, false-fail 비율, 미분류 건수, 가장 오래된 미해결 건의 나이, 실행 대비 BLOCKED 비율. 그리고 feature 게이트에 일찍 도달했는지가 리스핀 결정 시점과 직결된다.

**꼬리질문: 버그 유입이 줄면 좋은 건가요?** 두 가지 뜻이 있다. 칩이 수렴했거나, 테스트가 막혀 안 돌고 있거나. **실행 시간과 위험 커버리지를 분모로** 봐야 한다.

**꼬리질문: 지표를 조작하는 팀을 어떻게 다루나요?** 분모(실행 수)와 정의(무엇이 'blocked'인가)를 공개하고, 게이트 회의에서는 항목별로 원자료를 본다. 지표는 판단을 대신하지 않는다.

</details>

<details><summary>Q4. A0로 PVT를 얼마나 해야 하나요? A1이 오면 다시 해야 하지 않나요?</summary>

A0의 PVT는 설계 마진 확인과 리스핀 입력이 목적이다. 그래서 대표 코너 중심으로 하고, 전 코너의 최종 특성화는 출하 리비전에서 한다. 금속 ECO가 어떤 IP의 타이밍·전압에 영향을 주는지 ECO 목록과 함께 재검증 범위를 정한다.

**꼬리질문: 일정이 없으면 A1 코너를 건너뛰나요?** 코너를 줄일 수 있어도 **출하 리비전에서 최악 코너를 건너뛰는 선택은 위험을 문서로 올려 결정을 받아야** 한다. A0 데이터가 A1을 얼마나 대표하는지 근거가 있어야 하고, 금속 변경이 닿은 곳은 대표성이 약하다.

</details>

<details><summary>Q5. 일정이 3주 밀렸습니다. 무엇부터 자르시겠습니까?</summary>

먼저 **그 3주가 임계 경로 위인지** 본다. 슬랙이 있는 PVT를 잘라도 끝은 안 당겨진다. 임계 경로 안에서는 위험 대비 절약이 큰 것부터 자른다: 보고용 다듬기, 저위험 조합 매트릭스, 그다음 코너 축소. 안 자르는 것은 전원 시퀀스, 열 트립, 안전 중단 경로다. 자른 항목은 "언제 보충하는가"와 함께 기록하고 결정권자의 사인을 받는다.

**꼬리질문: greedy로 충분한가요?** 항목이 적으면 전수 탐색이 정확하고, 주당 위험으로 줄 세우는 greedy는 초과 절약에서 틀릴 수 있다(실제 테스트: 5주 필요 시 greedy 위험 11, 전수 10). 하지만 위험 점수 자체가 추정이므로 산출 숫자보다 **무엇을 포기하는지 공개하는 것**이 더 중요하다.

</details>

<details><summary>Q6. 보드 7장 중 4장에서 DDR lane 3이 죽었습니다. 설계 결함이라고 봐도 되나요?</summary>

"시스템적 의심"이지 결론이 아니다. 4장이 같은 보드 제조 lot, 같은 DRAM 벤더, 같은 라우팅이면 교란된다. 칩을 다른 보드로 옮기기, 다른 DRAM, 다른 보드 리비전 등의 교체 실험으로 분리하고, lane 3 라우팅과 eye 분포를 나머지 3장과 비교한다.

**꼬리질문: 칩이 소켓 없이 납땜돼 있다면요?** 교체가 비싸진다. 샘플 배분 때 일부 보드를 소켓형으로 잡는 이유다. 없으면 PHY 레벨 내부 루프백과 lane swap 설정으로 칩 안쪽/보드 쪽을 구분해 본다.

**꼬리질문: 한 장에서만 나왔다면?** `SUSPECT_BOARD_OR_DIE`지만 역시 가설이다. 보드를 바꿔 실험하기 전에는 설계 버그를 배제하지 말고, 한 장의 양산 불량일 수 있음을 기록한다.

</details>

<details><summary>Q7. CSI-2 RX의 루프백은 실제로 어떻게 합니까? 그걸로 충분한가요?</summary>

RX에는 자체 TX가 없다. 내부 테스트 패턴 소스, 외부 패턴 발생기, FPGA 기반 CSI-2 TX, 또는 실제 센서가 입력이 된다. 헤더 ECC 정정 수와 페이로드 CRC 오류를 분리해 센다. D-PHY 기준으로 레인별 수신 설정을 바꿔 가며 어떤 레인이 문제인지 좁힌다.

**꼬리질문: 내부 패턴 소스로 통과하면 충분한가요?** 아니다. PHY 전기 경로를 지나지 않을 수 있고, 실제 센서의 타이밍과 다르다. Day-0 스모크로만 쓰고 feature 단계에서 실제 센서와 외부 패턴을 쓴다. 이 경로가 어디를 거치는지 설계 팀에 먼저 확인한다.

</details>

<details><summary>Q8. 자동화에 왜 초기부터 2명이나 씁니까? 그 시간에 칩을 돌리면 안 되나요?</summary>

수동으로 돌린 실행은 재현 조건이 남지 않고, 밤 시간과 코너 시간을 쓰지 못한다. 위 표대로 시그니처 품질 하나가 분류 인력 몇 명 값을 한다. 다만 Day-0 주에 대시보드를 다듬는 일은 낭비다. 순서는 **재현 가능한 실행 → 결과 저장 → 시그니처 → 대시보드**이고, 슬립 시 맨 끝이 첫 번째로 잘린다.

**꼬리질문: 처음부터 다 만들면 비용이 크지 않나요?** 스크립트 형태로 시작하고, 같은 인터페이스(벤치 HAL)를 유지한 채 키운다. 처음 필요한 것은 전원 릴레이, 재현 정보, 로그 저장뿐이다. 스크립트가 쌓이는 시점에 시퀀서로 옮긴다.

</details>

## 9. 추가 연습

### 연습 1 — 재실행 범위 계산

`resume_plan(deps, prev_status, changed, power_cycled)`: 고친 뒤 다시 돌릴 스테이지를 선언 순서로 돌려준다. 전원 사이클이면 전부, 아니면 PASS가 아닌 것과 그 자손, `changed`와 그 자손.

<details><summary>답</summary>

FLAKY도 재실행 대상이다(PASS는 확인된 것이 아니다). 전원 사이클은 칩의 volatile 상태를 지우므로 앞 결과를 그대로 쓸 수 없다. 이력 전파는 `descendants`를 고정점까지 반복해서 구한다. 코드는 `practice.py`의 `resume_plan`.

</details>

### 연습 2 — 웨이버 적용 규칙

`apply_waivers(results, waivers, today)`: 웨이버는 stage·code가 정확히 맞고, ticket이 있고, 만료 전이어야 한다. 스테이지가 안 실패한 웨이버는 'not_failing'으로 보고해 낡은 항목을 잡는다.

<details><summary>답</summary>

핵심은 **다른 code에는 적용되지 않는다**는 점이다. `E_PCIE_STUCK_POLLING` 웨이버로 `E_PCIE_STUCK_DETECT`를 덮으면, 다른 실패가 웨이버로 숨는다. verdict는 `PASS_WITH_WAIVER`로 따로 둔다. `practice.py`의 `apply_waivers`와 5개 체크.

</details>

### 연습 3 — 일정 slack과 잘라도 소용없는 작업

전진/후진 패스로 `slack(tasks)`를 구하고, `slip_effect(tasks, name, delta)`로 검증한다. 정의를 코드로 검증: slack만큼 늦어지면 끝이 안 밀리고, slack+1이면 1주 밀린다 (전체 작업 전수).

<details><summary>답</summary>

실행 결과 slack: `a1_metal_spin` 0, `pvt_char` 8, `csi2_feature` 1, `lab_automation` 18, `stress_soak` 9. 임계 경로는 리스핀 경로이고 `serdes_feature`가 DDR과 동률이라 함께 0이다. PVT를 3주 줄여도 끝은 그대로(`slip_effect == 0`), 금속 스핀을 3주 줄이면 `-3`. 단, `lab_automation`의 slack 18은 "PVT 시작 전까지만 끝나면 된다"는 모델의 계산이고, 실제로는 Day-0 이전부터 일부 기능이 필요하다. 모델이 말하지 않는 것은 사람이 채운다.

</details>

### 연습 4 — 두 밤의 비교

`diff_nights(prev, cur)`: 보드별로 NEW / FIXED / PERSISTING / UNKNOWN을 낸다. 인프라 밤이나 안 돌린 보드의 이전 실패는 **FIXED가 아니라 UNKNOWN**이다.

<details><summary>답</summary>

FIXED의 조건은 "돌았고, 초록이고, 이전에 실패"이다. `INCONCLUSIVE` 또는 누락 보드는 이전 시그니처를 UNKNOWN으로 넘긴다. 시그니처가 바뀐 보드는 이전이 FIXED, 새것이 NEW다. 이 구분이 없으면 랩이 고장난 날 아침 대시보드에 버그가 전부 해결됐다고 뜬다.

</details>

### 연습 5 — 선언 의존 vs 물리 의존 lint

`gating_lint(declared, phys, observe)`: 조상 집합을 비교해 UNDER(물리 의존을 선언 안 함)와 OVER(물리에 없는 의존을 선언함)를 낸다. `observe`로 JTAG 같은 관측 경로는 면제한다.

<details><summary>답</summary>

실행 결과 선언 DAG는 0건, 사슬은 OVER 6건 (`jtag_idcode`가 `clock`에 걸리는 것, PCIe가 DDR에 걸리는 것 등), UNDER 0건이다. 사슬에는 under-gating이 없고 정보만 잃는다는 Q1의 답이 수치로 나온다. CSI-2의 DDR 간선을 빼면 UNDER로 잡힌다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py; echo "exit=$?"     # 23/23 passed, exit=0
```

실제 출력 (일부):

```text
-- 3. slack --
      slack: {'a1_metal_spin': 0, 'a1_regression': 0, 'board_design_fab': 4, 'csi2_feature': 1, 'day0_bringup': 0, 'day0_scripts_on_emu': 6, 'ddr_feature': 0, 'lab_automation': 18, 'pcie_feature': 1, 'production_handoff': 0, 'pvt_char': 8, 'serdes_feature': 0, 'silicon_fab': 0, 'stress_soak': 9}
PASS  cutting 3 weeks of PVT does not move the end date (slack, not critical)
PASS  cutting 3 weeks of the metal spin moves it 3 weeks
-- 5. gating lint --
PASS  declared DAG matches physics (JTAG observe path exempt)
      chain over-gates 6 ancestor edges
23/23 passed
```

## 10. 핵심 정리

- 검증 계획의 산출물은 네 가지 결정이다: 다음 단계 진행, 리스핀 내용, errata·동작 조건, 양산 이관.
- 출구 기준은 **숫자 지표 + 소유자**. 지표 누락은 NO_GO. waiver는 항목별로 지정된 것만.
- Pre-si에서 Day-0 스크립트를 에뮬레이터로 돌린다. 도착 전에 할 수 있는 일을 도착 후로 미루지 않는다.
- FAIL(칩) · ERROR(계측) · BLOCKED(못 봄) · FLAKY(재시도 통과)를 분리한다. 스크립트 예외는 실리콘 FAIL이 아니다.
- 의존성은 사슬이 아니라 물리 + 관측 경로의 DAG다. 게이팅 없음은 거짓 FAIL 30건, 사슬은 진단 손실, DAG도 결함 가림(6/36)은 남는다.
- 안전 중단은 전체를 멈추고 `safe_state()`는 모든 경로에서 한 번.
- 리스핀이 있으면 일정은 리스핀이 정한다. 자르는 항목은 임계 경로 위에 있어야 일정이 움직인다.
- Day-0 PASS는 "살아 있다"일 뿐이다. 코너 전용 결함과 귀속의 한계를 함께 말한다.

## 참고자료

- [MIPI CSI-2 스펙 소개 (MIPI Alliance)](https://www.mipi.org/specifications/csi-2) · [MIPI A-PHY](https://www.mipi.org/specifications/a-phy) — 패킷 구조, 물리 계층 선택지
- [PCI-SIG 스펙 목록](https://pcisig.com/specifications) — PCIe Base (LTSSM, Lane Margining at the Receiver는 4.0부터)
- [JEDEC LPDDR5 (JESD209-5)](https://www.jedec.org/standards-documents/docs/jesd209-5) — 훈련 항목은 DRAM 세대와 PHY 벤더별로 다르다
- IEEE 1149.1 (JTAG): IDCODE 구조 [31:28] 버전, [27:12] 부품, [11:1] 제조사, [0]=1
- [AEC-Q100 (Automotive Electronics Council)](http://www.aecouncil.com/AECDocuments.html) — 집적회로 스트레스 자격시험, 온도 등급
- ISO 26262 (기능안전, Part 11은 반도체 적용 지침)
- [OpenHTF (Google)](https://github.com/google/openhtf) · NI TestStand — 하드웨어 테스트 시퀀서의 실제 예
- P. Mishra, F. Farahmandi (eds.), *Post-Silicon Validation and Debug*, Springer 2019
- 관련 레슨: SIV-B10(bring-up 시퀀스), SIV-B13(pre/post silicon, 백엔드 교체), SIV-M04(PCIe 체커), SIV-M05(DDR 훈련), SIV-M06(PRBS, BER 상한), SIV-M07(shmoo), SIV-M08(PVT 스케줄), SIV-M10(시그니처 triage), SIV-M11(bisect), SIV-M12(Python 시퀀서), SIV-M13(functional coverage), SIV-A03(전력), SIV-A04(열), SIV-A05(errata), SIV-A06(전원·클럭), SIV-A08(수율), SIV-A11(전원 도메인)
