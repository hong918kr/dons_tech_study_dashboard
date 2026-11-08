---
id: SIV-M01
time: 90분
updated: 2026-11-08
---
# SIV-M01 ATE vs SLT vs 시스템 레벨 검증의 역할

## 학습 목표

- 양산 테스트 단계 **CP(웨이퍼 프로브) · FT(패키지 ATE) · SLT(System-Level Test)** 와 랩의 **보드·시스템 검증(SYSVAL)** 이 각각 무엇을 할 수 있고 무엇을 못 하는지를 능력(capability) 비트 9개로 말할 수 있다.
- 결함을 "**누가 갖고 있나**(일부 유닛 = 제조/마진 결함, 모든 유닛 = 설계 결함)" 와 "**보려면 무엇이 필요한가**" 로 나눠, 책임 단계를 코드(`classify`)로 정하고 못 잡는 이유(`why_not`)를 비트로 출력한다.
- 테스트 시간·비용·커버리지를 숫자로 비교한다: 유닛당 테스트 비용, Williams–Brown 결함 수준 `DL = 1 − Y^(1−T)`, 간헐 결함 재현 횟수 `k ≥ ln(1−CL)/ln(1−p)`, SLT 추가의 손익분기 필드 비용.
- 고객 반품(RMA)을 STAT_ESCAPE / TEST_HOLE / FLOW_GAP / NO_SCREEN / VALIDATION_ESCAPE 로 나누고, **테스트 프로그램 구멍**(심어 둔 FT 온도 insertion 누락)을 "어느 단계에 어떤 능력을 복구하라" 는 액션으로 묶는다.
- 위 숫자를 근거로 한 1페이지 **테스트 플로우 설계 메모**를 쓸 수 있다.

## 1. 문제

Silicon validation 면접(Rivian Silicon & Sensing, NVIDIA/Apple/Qualcomm post-silicon)에서 "이 버그는 ATE 에서 잡아야 하나, SLT 인가, 너희 랩인가?" 는 단골 질문이다 — 답을 틀리면 설계 버그를 양산 스크린으로 막으려다 수율을 날리거나, 제조 결함을 랩 샘플 몇 개로 "검증 끝" 이라고 선언하게 된다.

SIV-B13 은 *설계 단계*(SIM → EMU → FPGA → first silicon)를 따라 "버그가 처음 보이는 곳" 을 정했다. 이 레슨은 실리콘이 나온 **뒤** 의 이야기다. 칩은 웨이퍼에서 한 번(CP), 패키지 후 한 번(FT), 필요하면 실제 시스템 보드 소켓에서 한 번 더(SLT) **모든 유닛이** 테스트된다. 그와 별개로 validation 랩은 **수십 개 샘플** 로 설계가 스펙을 만족하는지 본다. 두 세계의 질문이 다르다.

- 양산 스크린(CP/FT/SLT): "**이 유닛** 이 제대로 만들어졌나?" — 결함은 일부 유닛에 무작위로 있다. 모든 유닛을 싸게, 빨리.
- 시스템 검증(SYSVAL): "**이 설계** 가 모든 조건에서 맞나?" — 결함은 모든 유닛에 있다. 샘플 몇 개를 오래, 깊게.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">양산 플로우(모든 유닛)와 검증 랩(샘플)의 관계</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="150" width="90" height="56" rx="6"/>
    <rect x="130" y="150" width="100" height="56" rx="6"/>
    <rect x="260" y="150" width="90" height="56" rx="6"/>
    <rect x="380" y="150" width="110" height="56" rx="6"/>
    <rect x="520" y="150" width="100" height="56" rx="6"/>
    <rect x="650" y="150" width="80" height="56" rx="6"/>
    <path d="M100 178 L128 178 M230 178 L258 178 M350 178 L378 178 M490 178 L518 178 M620 178 L648 178"/>
  </g>
  <rect x="130" y="150" width="100" height="56" rx="6" class="f-acc" opacity=".3"/>
  <rect x="380" y="150" width="110" height="56" rx="6" class="f-acc" opacity=".3"/>
  <rect x="520" y="150" width="100" height="56" rx="6" class="f-acc" opacity=".3"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="55" y="174" font-weight="bold">웨이퍼 fab</text><text x="55" y="194" font-size="12">공정</text>
    <text x="180" y="174" font-weight="bold">CP (ATE)</text><text x="180" y="194" font-size="12">scan·MBIST</text>
    <text x="305" y="174" font-weight="bold">조립</text><text x="305" y="194" font-size="12">다이싱·패키지</text>
    <text x="435" y="174" font-weight="bold">FT (ATE)</text><text x="435" y="194" font-size="12">at-speed·hot/cold</text>
    <text x="570" y="174" font-weight="bold">SLT</text><text x="570" y="194" font-size="12">부팅+워크로드</text>
    <text x="690" y="174" font-weight="bold">출하</text><text x="690" y="194" font-size="12">고객·필드</text>
  </g>
  <rect x="130" y="40" width="490" height="62" rx="6" class="f-ok" opacity=".3"/>
  <rect x="130" y="40" width="490" height="62" rx="6" fill="none" stroke="currentColor"/>
  <text x="375" y="62" fill="currentColor" text-anchor="middle" font-weight="bold">SYSVAL: 보드·시스템 검증 랩 (수십 유닛, 코너 랏, 몇 달)</text>
  <text x="375" y="84" fill="currentColor" text-anchor="middle" font-size="12">shmoo·PVT 특성화, interop, 장시간 스트레스, errata — 설계가 맞는지</text>
  <g stroke="currentColor" fill="none" stroke-dasharray="4 3">
    <path d="M435 102 L435 148"/><path d="M570 102 L570 148"/><path d="M690 206 L690 250 L240 250 L240 104"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="430" y="128" text-anchor="end">특성화 → 리밋·guardband</text>
    <text x="576" y="128">테스트 콘텐츠</text>
    <text x="20" y="268">RMA(반품) → 고장 분석 → triage → 스크린 추가 / errata / 테스트 프로그램 수정</text>
    <text x="10" y="236">음영 = 모든 유닛을 테스트하는 screen 단계</text>
  </g>
</svg>
```

이 레슨의 모델은 의도적으로 단순하다. 결함을 **데이터**(종류 + "보려면 필요한 능력" 비트마스크 + 발생률 ppm + 1회 검출 확률)로 적고, 단계도 **데이터**(능력 비트마스크 + 장비 비용)로 적는다. 분류·계산·triage 는 그 데이터 위의 작은 함수들이다. 숫자(ppm, 비용, 검출 확률)는 교육용 가정 값이고, 실무에서는 제품의 STDF 로그·RMA 데이터베이스에서 온다.

### Part A — 결함 20개를 책임 단계로 분류

능력 비트: `STRUCT`(scan/MBIST/IDDQ 등 DFT 접근) · `ATSPEED` · `TEMP` · `PKG` · `MISSION`(실 FW/OS/DRAM/워크로드) · `PARTNER`(실제 상대 장치·보드) · `LONG`(수 시간·수백 회) · `STRESS`(번인류) · `VOLUME`(모든 유닛).

- `eff_needs(d)` — 제조·마진 결함은 일부 유닛에만 있으므로 `needs | VOLUME`, 설계 결함은 `needs` 그대로.
- `classify(d, caps, flow)` — `flow` 에 있는 단계 중 **kind 가 맞는**(제조→SCREEN, 설계→VALIDATION) 첫 단계에서 `caps ⊇ eff_needs` 이면 그 단계. 없으면 `ESCAPE`.
- `why_not(s, d, caps)` — 단계 s 가 못 보는 이유 비트. `stage_histogram` — 단계별 개수와 ESCAPE 수.

### Part B — 시간·비용·커버리지 계산기

- `unit_cost(e)` = 시간당 비용 / 3600 × 유닛당 초 / 동시 site 수.
- `defect_level(Y, T)`, `coverage_for_dl(Y, DL)` — Williams–Brown 과 그 역함수.
- `p_detect_runs(p, k)`, `runs_needed(p, conf)` — 간헐 결함을 k 번 돌려 한 번 이상 볼 확률, 필요한 최소 k.
- `flow_eval(...)` — 투입 100만 다이 기준 단계별 fallout, 출하 수율, 출하 DPPM, 출하 1개당 테스트 비용·유출 비용. `breakeven_field_cost(a, b)` — 플로우 b(스크린 추가)가 이득이 되는 필드 고장 1건 비용.

### Part C — RX7 양산 RMA triage + 설계 메모

가상의 차량용 센싱 SoC **RX7** 이 CP+FT+SLT 플로우로 양산 중이다. 고객 반품 14건이 고장 분석(FA)을 거쳐 원인 결함이 밝혀졌다. FT 테스트 프로그램 **P1.3** 에는 결함이 하나 심어져 있다 — 장비 일정 문제로 **hot/cold insertion 이 빠졌다**(`p13[FT] &= ~TEMP`).

- `rma_verdict(d, actual, flow)` — 반품 1건을 5가지로 판정, TEST_HOLE 이면 잃어버린 능력까지.
- `rma_holes(...)` — TEST_HOLE 을 (단계, 잃은 능력)으로 묶어 "FT 에 [TEMP] 복구 → 반품 5건(SRAM_HOT×3 …)" 액션으로.
- 정상 프로그램(`NOMINAL_CAPS`)이면 구멍 0개여야 하고, 두 번째로 심은 결함(SLT 가 워크로드 생략)도 따로 잡혀야 한다.

## 2. Problem (English)

> **Which stage owns this defect: ATE (wafer sort / final test), SLT, or system validation?**
>
> 1. Stages `CP, FT, SLT` are per-unit screens; `SYSVAL` is sample-based validation. Each stage has a capability mask over `STRUCT, ATSPEED, TEMP, PKG, MISSION, PARTNER, LONG, STRESS, VOLUME`. A defect has a class (`MFG`, `MARGINAL`, `DESIGN`) and a `needs` mask. Manufacturing and marginal defects affect only some units, so they implicitly need `VOLUME`. Implement `classify(d, caps, flow)`: the earliest stage present in `flow`, of the right kind (screen for MFG/MARGINAL, validation for DESIGN), whose caps cover the effective needs; else `ESCAPE`. Also `why_not(stage, d, caps)` and a histogram.
> 2. Implement the economics: per-unit cost `rate/3600 * secs / sites`; Williams–Brown defect level `DL = 1 - Y^(1-T)` and its inverse; `runs_needed(p, conf)` for intermittent failures (robust at exact-integer boundaries); and `flow_eval` that, for 1e6 started dies, computes per-stage fallout, shipped DPPM, test cost and escape cost per shipped unit. A defect is screened only at its owner stage with probability `p_obs` (coverage holes are deterministic). Design defects are not DPPM.
> 3. Triage customer returns given the actual test program capabilities: `DESIGN → VALIDATION_ESCAPE`; no stage can see it → `NO_SCREEN`; a capable stage exists but not in this flow → `FLOW_GAP`; the owner stage lost a needed capability in the actual program → `TEST_HOLE` (+ missing bits); otherwise `STAT_ESCAPE`. Group test holes by (stage, missing caps).
>
> Example: with FT program P1.3 missing `TEMP`, `RMA-2601 SRAM_HOT → TEST_HOLE FT missing=[TEMP]`, and the report must say `ACTION FT: restore [TEMP] ... -> 5 returns`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # SDK 27.2 링크가 깨진 맥에서만
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter -lm && ./starter   # 4/46 passed, exit 1
```

`-lm` 은 macOS 에선 없어도 링크되지만(libm 이 libSystem 안에 있다) Linux glibc 에선 필요하니 붙여 둔다. 시작 코드의 실제 출력 일부:

```text
  mismatch SA0        got=ESCAPE exp=CP
  ...
FAIL starter.c:267  a_ok == NDEF
FAIL starter.c:268  eff_needs(&DEFECTS[D_SA0]) == (C_STRUCT | C_VOLUME)
...
FAIL starter.c:361  nh == 1 && holes[0].stage == ST_SLT && holes[0].missing == C_MISSION && holes[0].count == 1
...
  summary: STAT_ESCAPE=0 TEST_HOLE=0 FLOW_GAP=0 NO_SCREEN=0 VALIDATION_ESCAPE=0
4/46 passed
```

통과하는 4개는 "ESCAPE 나 0 이 정답인" 검사들이다(예: 설계 결함을 screen 단계만 있는 플로우에서 분류). 아무것도 안 하는 분류기도 "못 잡는다" 는 답은 맞힌다 — 그래서 테스트는 반드시 **잡아야 하는 경우와 못 잡아야 하는 경우를 둘 다** 넣는다.

## 4. 힌트

<details>
<summary>힌트 1 — VOLUME 을 "필요한 능력"으로 만들면 규칙이 하나로 줄어든다</summary>

"제조 결함은 SYSVAL 이 못 맡는다" 를 특별 규칙으로 쓰지 말고, 제조·마진 결함의 needs 에 `VOLUME` 을 더하라. SYSVAL 에는 VOLUME 이 없으니 부분집합 검사만으로 걸러진다. 반대 방향(설계 결함이 SLT 로 분류되는 것)은 단계의 kind 로 막는다 — SLT 는 설계 결함을 **볼 수는 있지만**, 그때는 모든 유닛이 떨어져 이미 늦었다.

</details>

<details>
<summary>힌트 2 — flow_eval 은 두 번 돈다</summary>

첫 루프: 결함마다 책임 단계를 정해 `caught[s] += ppm·p_obs`, `escaped += ppm·(1−p_obs)` (책임 단계가 없으면 ppm 전부 유출). 둘째 루프: 플로우의 screen 단계를 순서대로 돌며 `entered = 남은 수`, `비용 += 남은 수 × unit_cost`, `남은 수 −= caught[s]`. 앞 단계에서 버린 유닛은 뒤 단계 비용을 내지 않는다 — CP 가 싸게 많이 버릴수록 SLT 비용이 준다.

</details>

<details>
<summary>힌트 3 — runs_needed 의 올림</summary>

`(1−p)^k ≤ 1−CL` 에 로그를 씌우면 `k ≥ ln(1−CL)/ln(1−p)` (둘 다 음수라 부등호 방향 유지). `ceil` 하기 전에 몫이 2.0000000000000004 처럼 정수를 살짝 넘는 경우가 있다 (p=0.2, CL=0.36). 작은 epsilon 을 빼고 올린다.

</details>

<details>
<summary>힌트 4 — RMA 판정은 세 번의 classify</summary>

① 전체 카탈로그·명목 능력으로(`FLOW_ALL`) — 아무도 못 보면 NO_SCREEN. ② 이 제품 플로우·명목 능력으로 — 없으면 FLOW_GAP. ③ 이 제품 플로우·**실제 프로그램 능력** 으로 — ②와 같으면 STAT_ESCAPE, 다르면 TEST_HOLE 이고 잃은 능력 = `eff_needs & NOMINAL[des] & ~actual[des]`. 설계 결함은 맨 앞에서 VALIDATION_ESCAPE.

</details>

## 5. 풀이

### Part A — 단계의 능력과 결함 20개

먼저 단계별 특성. 숫자는 제품·공장마다 크게 달라지는 **어림값** 이다.

| 단계 | 무엇에 꽂히나 | 유닛당 시간 | 주로 하는 것 | 강점 | 약점 |
|---|---|---|---|---|---|
| CP (wafer sort) | 프로브 카드 → 웨이퍼 다이 | 초 단위 | scan stuck-at, MBIST + 리던던시 repair(퓨즈), IDDQ, 파라메트릭 | 가장 싸다, 패키지 비용 전에 불량 제거(멀티다이·칩렛의 KGD) | 프로브로 공급할 수 있는 전류·속도 한계, 온도 insertion 제한 |
| FT (final test) | 핸들러 소켓 → 패키지 칩 | 초~수십 초 | at-speed scan(LOC/LOS), hot/cold MBIST, IO 파라메트릭, SerDes loopback, Vmin/Fmax 빈 | 결함 모델 + 커버리지 숫자, 온도 insertion, 진단 가능 | 실 OS·DRAM·워크로드가 없다 |
| SLT | 실제 시스템과 비슷한 보드 소켓 | 수 분~수십 분 | 부팅, 드라이버, 메모리 training, 실워크로드, 전원 상태 전환 | 미션 모드에서만 보이는 결함 (전원망 di/dt, 스캔 밖 경로, 상호작용) | 커버리지를 정의할 결함 모델이 없다, 느리다, 진단성 낮음 |
| SYSVAL | 평가보드·고객 레퍼런스 보드, 챔버 | 몇 달 (샘플 수십 개) | shmoo/PVT, interop, 장시간 스트레스, 전원 시퀀스, errata | 설계 결함을 찾고 리밋의 근거를 만든다 | 유닛 수가 적다 → 일부 유닛 결함은 통계적으로 못 본다 |

능력 비트로 줄이면 아래 행렬이 된다 (`NOMINAL_CAPS[]`).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">단계 × 능력 (채운 칸 = 있음)</text>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="165" y="48">STRUCT</text><text x="235" y="48">ATSPEED</text><text x="305" y="48">TEMP</text>
    <text x="375" y="48">PKG</text><text x="445" y="48">MISSION</text><text x="515" y="48">PARTNER</text>
    <text x="585" y="48">LONG</text><text x="655" y="48">STRESS</text><text x="712" y="48">VOLUME</text>
  </g>
  <g fill="currentColor" font-weight="bold">
    <text x="10" y="80">CP</text><text x="10" y="116">FT</text><text x="10" y="152">SLT</text><text x="10" y="188">SYSVAL</text>
  </g>
  <g class="f-acc" opacity=".55">
    <rect x="135" y="62" width="60" height="26"/><rect x="690" y="62" width="45" height="26"/>
    <rect x="135" y="98" width="60" height="26"/><rect x="205" y="98" width="60" height="26"/><rect x="275" y="98" width="60" height="26"/>
    <rect x="345" y="98" width="60" height="26"/><rect x="690" y="98" width="45" height="26"/>
    <rect x="205" y="134" width="60" height="26"/><rect x="275" y="134" width="60" height="26"/><rect x="345" y="134" width="60" height="26"/>
    <rect x="415" y="134" width="60" height="26"/><rect x="690" y="134" width="45" height="26"/>
  </g>
  <g class="f-ok" opacity=".55">
    <rect x="135" y="170" width="60" height="26"/><rect x="205" y="170" width="60" height="26"/><rect x="275" y="170" width="60" height="26"/>
    <rect x="345" y="170" width="60" height="26"/><rect x="415" y="170" width="60" height="26"/><rect x="485" y="170" width="60" height="26"/>
    <rect x="555" y="170" width="60" height="26"/>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="135" y="62" width="600" height="134"/>
    <path d="M135 98 L735 98 M135 134 L735 134 M135 170 L735 170"/>
  </g>
  <text x="10" y="222" fill="currentColor" font-size="12">STRESS 는 아무 단계에도 없다 (번인을 넣는 건 연습 1).</text>
  <text x="10" y="240" fill="currentColor" font-size="12">SYSVAL 만 VOLUME 이 없고, SLT 는 STRUCT 가 없다.</text>
</svg>
```

모델 단순화 세 가지를 밝혀 둔다. ① CP 를 단일 온도·저속으로 뒀다. 실제로는 hot chuck 로 고온 CP 를 하거나 on-chip PLL 로 at-speed scan 을 CP 에서 일부 돌리는 제품도 있다 — 다만 프로브 니들의 전류 공급·노이즈 한계 때문에 at-speed 와 온도는 FT 에 몰아 두는 경우가 많다. ② SLT 에 STRUCT 를 주지 않았다. SLT 보드에서 MBIST/LBIST 를 실행하기도 하지만, SLT 의 존재 이유는 구조 테스트가 아니라 미션 모드다. ③ 결함마다 "책임 단계 하나" 를 정한다. SA0 같은 결함이 SLT 기능 테스트에 우연히 걸릴 수도 있지만 **그 확률을 숫자로 말할 수 없다** — 그래서 책임은 결함 모델과 커버리지가 있는 단계에 둔다.

결함 20개 분류 — solution 실행 출력 그대로다 (`FT-missing` 은 `why_not(FT, …)`).

```text
== Part A: 20 defect scenarios -> owner stage
  SA0        MFG      needs=[STRUCT,VOLUME]              -> CP      FT-missing=[]
  BRIDGE     MFG      needs=[STRUCT,VOLUME]              -> CP      FT-missing=[]
  SRAM_HARD  MFG      needs=[STRUCT,VOLUME]              -> CP      FT-missing=[]
  TDF        MFG      needs=[STRUCT,ATSPEED,VOLUME]      -> FT      FT-missing=[]
  BOND       MFG      needs=[STRUCT,PKG,VOLUME]          -> FT      FT-missing=[]
  SRAM_HOT   MFG      needs=[STRUCT,TEMP,VOLUME]         -> FT      FT-missing=[]
  VMIN       MARGINAL needs=[ATSPEED,TEMP,VOLUME]        -> FT      FT-missing=[]
  PLL_COLD   MARGINAL needs=[TEMP,PKG,VOLUME]            -> FT      FT-missing=[]
  SERDES_LB  MARGINAL needs=[ATSPEED,PKG,VOLUME]         -> FT      FT-missing=[]
  PDN_DROOP  MFG      needs=[ATSPEED,PKG,MISSION,VOLUME] -> SLT     FT-missing=[MISSION]
  SCAN_HOLE  MFG      needs=[ATSPEED,MISSION,VOLUME]     -> SLT     FT-missing=[MISSION]
  DDR_IF     MARGINAL needs=[ATSPEED,PKG,MISSION,VOLUME] -> SLT     FT-missing=[MISSION]
  OS_HANG    MFG      needs=[ATSPEED,TEMP,MISSION,VOLUME] -> SLT     FT-missing=[MISSION]
  OXIDE      MFG      needs=[STRESS,VOLUME]              -> ESCAPE  FT-missing=[STRESS]
  LINK_RARE  MFG      needs=[MISSION,LONG,VOLUME]        -> ESCAPE  FT-missing=[MISSION,LONG]
  PCIE_EQ    DESIGN   needs=[MISSION,PARTNER,LONG]       -> SYSVAL  FT-missing=[MISSION,PARTNER,LONG]
  ROM_ERRATA DESIGN   needs=[MISSION]                    -> SYSVAL  FT-missing=[MISSION]
  COHERENCY  DESIGN   needs=[MISSION,LONG]               -> SYSVAL  FT-missing=[MISSION,LONG]
  THERM_CAL  DESIGN   needs=[TEMP,MISSION]               -> SYSVAL  FT-missing=[MISSION]
  PWR_SEQ    DESIGN   needs=[PARTNER]                    -> SYSVAL  FT-missing=[PARTNER]
```

히스토그램 `{CP 3, FT 6, SLT 4, SYSVAL 5, ESCAPE 2}` — 테스트가 그대로 확인한다. 줄마다 왜 그런지:

| 결함 | 근거 (왜 이 단계, 왜 앞 단계는 아닌가) |
|---|---|
| SA0, BRIDGE | stuck-at scan 패턴이 잡는다. bridging 은 stuck-at 모델로 일부만 잡혀 1회 검출률을 0.998 로 낮게 뒀다 (cell-aware·bridging 전용 패턴이 개선책). |
| SRAM_HARD | MBIST 가 찾고 **CP 에서 리던던시 행/열로 repair**(퓨즈 프로그래밍) — 패키지 뒤엔 퓨즈 방식에 따라 repair 가 어렵거나 불가능하다. 그래서 CP 가 맡는 게 경제적으로도 맞다. |
| TDF | 작은 지연 결함은 느린 클럭에선 안 보인다. at-speed launch→capture(LOC/LOS)가 필요 → FT. |
| BOND | 패키지 조립 결함은 패키지 후에만 존재한다. open/short(continuity) 테스트. |
| SRAM_HOT | 고온 누설로 약한 셀이 데이터를 잃는다. hot insertion MBIST → FT. Part C 의 주인공. |
| VMIN, PLL_COLD, SERDES_LB | **마진 결함** — 설계는 맞지만 공정 산포의 끝(느린 유닛, 저온)에서 마진이 모자란다. 모든 유닛을 봐야 하니 screen 이지만, 리밋(Vmin 몇 mV, loopback BER 몇)은 **SYSVAL 특성화에서 온다** (SIV-M07 shmoo). |
| PDN_DROOP | 기판 decap 누락은 정적 테스트·scan 에서 티가 안 난다. 실워크로드의 di/dt 가 있어야 전압이 꺼진다 → SLT 의 대표 사례. |
| SCAN_HOLE | 비동기 경계·하드 매크로 인터페이스처럼 scan 이 닿지 않는 경로. FT 의 커버리지 숫자 **밖** 이다. |
| DDR_IF | 실제 DRAM 과 training 해야 마진이 드러난다 (SIV-M05). FT 엔 DRAM 이 없다. |
| OS_HANG | 고온 + 멀티코어 + 실 OS 상호작용. 구조 테스트로는 재현 조건을 못 만든다. |
| OXIDE | t=0 에는 정상. 시간·전압·온도 스트레스로 가속해야 드러난다 (bathtub 곡선의 초기 고장 구간). 본문 플로우엔 STRESS 가 없어 ESCAPE → 연습 1 에서 번인 추가. |
| LINK_RARE | 일부 유닛에서 몇 시간에 한 번 — **VOLUME 과 LONG 을 동시에** 요구한다. 모든 유닛을 몇 시간씩 돌릴 단계는 없다. `why_not(SLT) = [LONG]`, `why_not(SYSVAL) = [VOLUME]` 이 이 딜레마를 정확히 보여 준다. 해법은 "기다리지 말고 마진을 재기": PCIe 4.0 부터 있는 Lane Margining at the Receiver 로 SLT 에서 수신 마진을 직접 측정하면 LONG 이 필요 없어진다. |
| PCIE_EQ ~ PWR_SEQ | **설계 결함** — 모든 유닛이 갖고 있다. SLT 가 ROM_ERRATA 를 볼 수는 있다(`why_not(SLT)=0`). 하지만 SLT 에서 처음 보면 **양산 첫날 수율 0%** 다. 그래서 kind 규칙으로 SYSVAL 에 둔다. |

핵심 코드는 짧다:

```c
static stage_t classify(const defect_t *d, const uint32_t caps[ST_COUNT], uint32_t flow) {
    uint32_t need = eff_needs(d);                       /* 제조·마진: needs | VOLUME */
    stage_kind_t want = d->cls == DC_DESIGN ? K_VALIDATION : K_SCREEN;
    for (int s = 0; s < ST_COUNT; s++) {
        if (!(flow & FLOW(s)) || STAGES[s].kind != want) continue;
        if ((caps[s] & need) == need) return (stage_t)s;
    }
    return ST_ESCAPE;
}
```

`caps` 와 `flow` 를 인자로 받는 게 Part B·C 의 열쇠다. 같은 함수가 "SLT 없는 소비자용 플로우"(`FLOW_CONS` → PDN_DROOP 은 ESCAPE)와 "온도 insertion 이 빠진 실제 FT 프로그램"(Part C)을 그대로 처리한다. CP 를 건너뛰는 플로우(`FLOW(ST_FT)`)에서는 SA0 가 FT 로 넘어간다 — 그것도 테스트가 확인한다.

### Part B — 시간·비용·커버리지

**유닛당 비용.** ATE 비용은 "장비 시간" 으로 계산한다. `unit_cost = rate/3600 × secs / sites`. 가정 값: CP $100/h·16 site·3 s → $0.0052, FT $150/h·8 site·6 s → $0.03125, SLT $400/h·128 슬롯·600 s → $0.521. SLT 는 슬롯이 싸고 많지만 **시간이 두 자릿수 길어** 유닛당으로는 가장 비싸다. 이 구조 때문에 "SLT 시간을 몇 분 줄이는 것" 이 테스트 엔지니어링의 큰 숙제가 된다 (연습 4).

**Williams–Brown 결함 수준.** 칩 하나에 결함 후보(fault)가 n 개, 각각 독립적으로 확률 p 로 존재한다고 하자. 수율(결함 0개일 확률)은 `Y = (1−p)^n`. 테스트가 그중 `m = T·n` 개를 본다면 테스트를 통과할 확률은 `(1−p)^m = Y^T`. 통과한 칩 중 실제로는 결함이 있는 비율이 결함 수준(DL)이다:

```text
DL = 1 − P(좋은 칩) / P(통과) = 1 − Y / Y^T = 1 − Y^(1−T)
예: Y = 0.90, T = 0.99  →  DL = 1 − 0.9^0.01 = 1 − e^(0.01·ln 0.9) = 1 − e^(−0.0010536) ≈ 0.001053  →  1053 DPPM
역: T = 1 − ln(1 − DL) / ln Y,  DL = 100 ppm, Y = 0.9  →  T = 1 − (−1.00005e-4)/(−0.10536) ≈ 0.999051
```

커버리지 99% 가 "1% 놓친다" 가 아니라 **출하품 1000개 중 1개 불량** 이라는 뜻이 되는 게 이 식의 요점이다. 수율이 낮을수록(결함이 많을수록) 같은 커버리지에서 DL 이 커진다 (연습 2). 한계도 말할 수 있어야 한다: 결함의 독립·균일 가정(실제 결함은 웨이퍼 위에서 뭉친다 — clustering 을 넣은 모델이 따로 있다), 모든 fault 가 같은 확률이라는 가정.

**간헐 결함.** 1회 관측 확률 p 인 현상을 k 번 돌려 한 번이라도 볼 확률은 `1 − (1−p)^k`. 신뢰도 CL 로 보려면:

```text
(1 − p)^k ≤ 1 − CL   →   k ≥ ln(1 − CL) / ln(1 − p)
PCIE_EQ (p = 1/50, CL = 95%):  ln 0.05 / ln 0.98 = −2.9957 / −0.020203 = 148.28  →  149 번 부팅
작은 p 근사:  k ≈ −ln(1 − CL) / p = 3.0 / p   ("rule of three": 149 번 무사하면 p < 약 3/149 ≈ 2% 를 95% 로 주장)
```

같은 계산을 BER 쪽으로 가져가면 "BER < 1e-12 를 95% 로 주장하려면 오류 0개로 약 3e12 비트" 가 된다 (SIV-M06). 이 레슨에선 "랩에서 1/50 버그를 재현하려면 부팅 149번, 그러니 자동화가 필요하다(SIV-M12)" 와 "**양산 SLT 에서 1/50 버그를 screen 하는 건 불가능**(유닛마다 149 부팅)" 두 결론이 중요하다.

`runs_needed` 의 함정: `ln 0.64 / ln 0.8` 은 정확히 2 여야 하지만 이 맥에서 `2.0000000000000004` 가 나와 `ceil` 이 3 을 돌려준다 (실제로 돌려 확인했다). 그래서 `ceil(x − 1e-9)`.

**플로우 계산.** 투입 100만 다이, 결함 하나는 **자기 책임 단계에서만** `p_obs` 로 잡힌다고 둔다. 이 보수적 가정의 이유: 커버리지 구멍은 결정적이다. CP 의 scan 패턴이 못 건드린 bridge 는 FT 에서 같은 패턴을 다시 돌려도 못 건드린다. "독립 시행 두 번이니 0.2% × 0.2%" 로 계산하면 유출을 수백 배 과소평가한다 (7. 자주 하는 실수 2). 실제 출력:

```text
== Part B: flow comparison (per 1e6 dies started)
  CP+FT  $30   yield=89.2207%  DPPM=  775.6  test$/unit=0.0374  escape$/unit=0.0233  total=0.0606  (CP out 99888, FT out 7905, SLT out 0)
  +SLT   $30   yield=89.1823%  DPPM=  345.4  test$/unit=0.5584  escape$/unit=0.0104  total=0.5688  (CP out 99888, FT out 7905, SLT out 384)
  CP+FT  $5k   yield=89.2207%  DPPM=  775.6  test$/unit=0.0374  escape$/unit=3.8780  total=3.9154  (CP out 99888, FT out 7905, SLT out 0)
  +SLT   $5k   yield=89.1823%  DPPM=  345.4  test$/unit=0.5584  escape$/unit=1.7268  total=2.2852  (CP out 99888, FT out 7905, SLT out 384)
  SLT break-even field cost = $1211.1 per failure
  DL(Y=0.90,T=0.99)=1053.1 DPPM  T for 100 DPPM=0.999051  runs for p=1/50 @95% = 149
```

손으로 검산해 보자. CP fallout = 50000·0.999 + 30000·0.998 + 20000·0.9999 = 49950 + 29940 + 19998 = **99888**. SLT fallout = 100·0.9 + 150·0.8 + 120·0.95 + 80·0.75 = 90 + 120 + 114 + 60 = **384**. 유출: ATE 책임 9종 합 207, SLT 책임 4종은 SLT 없으면 450 · 있으면 66, ESCAPE 2종 35. 그래서 CP+FT 는 692 개, +SLT 는 308 개가 출하품에 섞인다. 출하 수 892207 / 891823 으로 나누면 775.6 / 345.4 DPPM. 손익분기 = Δ테스트비 / ΔDPPM = (0.5584 − 0.0374) / (430.2e-6) ≈ **$1211**.

| 읽을 것 | 의미 |
|---|---|
| SLT 는 유닛 1개에 $0.52 를 더 쓰고 DPPM 을 775 → 345 로 줄인다 | SLT 의 가치는 "필드 고장 1건이 얼마인가" 에 달렸다 |
| 필드 고장 $30 (소비자 가전, 교환) | CP+FT 가 총비용 $0.061 vs $0.569 — SLT 는 손해 |
| 필드 고장 $5000 (차량용: 모듈 교체, 조사, 리콜 위험) | +SLT 가 $2.29 vs $3.92 — SLT 가 이득 |
| 손익분기 $1211 | 이 숫자가 경영진에게 가져갈 한 줄이다. 실제 필드 비용은 평판·리콜 위험 때문에 선형이 아니다 |
| 수율이 89.22% → 89.18% 로 0.04%p 떨어진다 | SLT 가 버린 384개는 **원래 불량** 이다. 진짜 수율 손실은 guardband 로 좋은 칩을 버리는 overkill 쪽에서 온다 (Q5) |

### Part C — RX7 RMA triage

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">반품 1건의 판정 순서 (rma_verdict)</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="40" width="150" height="40" rx="6"/><rect x="190" y="40" width="160" height="40" rx="6"/>
    <rect x="380" y="40" width="160" height="40" rx="6"/><rect x="570" y="40" width="160" height="40" rx="6"/>
    <path d="M160 60 L188 60 M350 60 L378 60 M540 60 L568 60"/>
    <path d="M85 80 L85 130 M270 80 L270 130 M460 80 L460 130 M650 80 L650 130"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="85" y="58">설계 결함?</text><text x="85" y="74">(모든 유닛)</text>
    <text x="270" y="58">카탈로그 어디서든</text><text x="270" y="74">볼 수 있나?</text>
    <text x="460" y="58">이 제품 플로우에</text><text x="460" y="74">그 단계가 있나?</text>
    <text x="650" y="58">실제 프로그램이</text><text x="650" y="74">필요 능력을 갖췄나?</text>
    <text x="175" y="34">아니오</text><text x="365" y="34">예</text><text x="555" y="34">예</text>
    <text x="96" y="108">예</text><text x="282" y="108">아니오</text><text x="472" y="108">아니오</text>
  </g>
  <rect x="10" y="132" width="150" height="54" rx="6" class="f-mut" opacity=".5"/>
  <rect x="190" y="132" width="160" height="54" rx="6" class="f-mut" opacity=".5"/>
  <rect x="380" y="132" width="160" height="54" rx="6" class="f-warn" opacity=".45"/>
  <rect x="570" y="132" width="75" height="54" rx="6" class="f-ok" opacity=".5"/>
  <rect x="655" y="132" width="75" height="54" rx="6" class="f-acc" opacity=".55"/>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="85" y="154" font-weight="bold">VALIDATION_ESCAPE</text><text x="85" y="172">errata + 검증 계획</text>
    <text x="270" y="154" font-weight="bold">NO_SCREEN</text><text x="270" y="172">새 스크린(번인·margining)</text>
    <text x="460" y="154" font-weight="bold">FLOW_GAP</text><text x="460" y="172">플로우 결정 재검토</text>
    <text x="607" y="150" font-weight="bold">STAT_</text><text x="607" y="164" font-weight="bold">ESCAPE</text><text x="607" y="180">예: 커버리지</text>
    <text x="692" y="150" font-weight="bold">TEST_</text><text x="692" y="164" font-weight="bold">HOLE</text><text x="692" y="180">아니오: 복구</text>
  </g>
  <text x="10" y="216" fill="currentColor" font-size="12">TEST_HOLE 만 "누가 실수했나" 가 분명하다 — 테스트 프로그램 버전·insertion 기록과 연결해 즉시 고칠 수 있다.</text>
  <text x="10" y="236" fill="currentColor" font-size="12">STAT_ESCAPE 는 예상된 유출: 비율이 모델(1 − p_obs)보다 높아지면 그때 커버리지 개선 과제가 된다.</text>
</svg>
```

실제 출력 (FT 프로그램 P1.3, 플로우 CP+FT+SLT):

```text
== RMA triage: RX7, flow CP+FT+SLT, FT program P1.3
  RMA-2601  SRAM_HOT   TEST_HOLE         FT     missing=[TEMP]
  RMA-2602  SA0        STAT_ESCAPE       CP    
  RMA-2603  PCIE_EQ    VALIDATION_ESCAPE SYSVAL
  RMA-2604  SRAM_HOT   TEST_HOLE         FT     missing=[TEMP]
  RMA-2605  OXIDE      NO_SCREEN         ESCAPE
  RMA-2606  PLL_COLD   TEST_HOLE         FT     missing=[TEMP]
  RMA-2607  PDN_DROOP  STAT_ESCAPE       SLT   
  RMA-2608  VMIN       TEST_HOLE         FT     missing=[TEMP]
  RMA-2609  ROM_ERRATA VALIDATION_ESCAPE SYSVAL
  RMA-2610  LINK_RARE  NO_SCREEN         ESCAPE
  RMA-2611  SRAM_HOT   TEST_HOLE         FT     missing=[TEMP]
  RMA-2612  TDF        STAT_ESCAPE       FT    
  RMA-2613  BRIDGE     STAT_ESCAPE       CP    
  RMA-2614  SERDES_LB  STAT_ESCAPE       FT    
  summary: STAT_ESCAPE=5 TEST_HOLE=5 FLOW_GAP=0 NO_SCREEN=2 VALIDATION_ESCAPE=2
  ACTION FT: restore [TEMP] in test program -> 5 returns (SRAM_HOT x3, VMIN x1, PLL_COLD x1)
46/46 passed
```

**VMIN 이 흥미롭다.** P1.3 에서 FT 가 온도를 잃자 VMIN 의 실제 책임 단계는 SLT 로 밀려난다 (SLT 도 ATSPEED+TEMP 를 가졌으니). 그런데도 판정은 `TEST_HOLE FT` 다 — 설계된 책임 단계가 능력을 잃었다는 사실이 핵심이고, "SLT 가 대신 잡아 주겠지" 는 ① SLT 의 Vmin 측정은 FT 만큼 정밀하지 않고 ② 유닛당 $0.52 짜리 단계에서 $0.03 짜리 단계의 불량을 버리게 되며 ③ 이 반품이 보여 주듯 SLT 도 놓친다. 같은 날 SLT fallout 에서 VMIN 류가 늘었다면 그것이 **반품보다 먼저 온 경보** 였을 것이다 (Q8).

테스트가 확인하는 것: 14건 판정 전부, hole 1개 = (FT, [TEMP], 5건, 결함별 3/1/1), 판정 분포. **정상 프로그램**(`NOMINAL_CAPS`)으로 같은 반품을 돌리면 hole 0개, 그 5건은 STAT_ESCAPE 로 바뀐다 (`counts[STAT_ESCAPE] == 10`). 두 번째로 심은 결함 — SLT 가 부팅만 하고 워크로드를 생략(`p14[SLT] &= ~MISSION`) — 은 (SLT, [MISSION], 1건: PDN_DROOP)으로 따로 잡힌다. SLT 없는 플로우에서 PDN_DROOP 반품은 `FLOW_GAP SLT` 다.

| 판정 | 이 반품들 | 디버그/테스트 엔지니어의 다음 행동 |
|---|---|---|
| TEST_HOLE FT [TEMP] | SRAM_HOT×3, PLL_COLD, VMIN | P1.3 의 hot/cold insertion 복구. P1.3 으로 테스트된 랏(ECID → 랏 → 프로그램 버전으로 추적)을 식별해 재테스트 또는 격리. 프로그램 릴리스 체크리스트에 "insertion 목록 diff" 추가 |
| STAT_ESCAPE | SA0, BRIDGE, TDF, SERDES_LB, PDN_DROOP | 개별로는 예상된 유출. 종류별 반품률을 모델 예측(ppm × (1 − p_obs))과 비교 — BRIDGE 가 예측보다 많으면 bridging/cell-aware 패턴 추가 |
| VALIDATION_ESCAPE | PCIE_EQ, ROM_ERRATA | 설계 결함: errata 문서화 + FW 우회(EQ preset 고정, ROM 패치/OTP 설정). **검증 계획의 구멍** 보고 — 그 EP 가 interop 매트릭스에 없었나, 149 부팅을 돌렸나 |
| NO_SCREEN | OXIDE, LINK_RARE | 새 스크린의 비용·효과 평가: 번인/HVST(연습 1), SLT 에 lane margining 추가 |

#### 1페이지 설계 메모 — RX7 양산 테스트 플로우 (v0.1)

> **목적.** RX7(차량용 센싱 SoC)의 양산 테스트 플로우와 단계별 책임을 정한다. 숫자는 이 레슨 모델의 가정 값이며 첫 양산 랏 데이터로 갱신한다.
>
> **결정.** CP → FT(hot/cold 2 insertion) → SLT(10분) 플로우를 채택한다. SYSVAL 은 양산 전 특성화·interop·errata 를 책임지고, 그 결과를 FT 리밋(Vmin guardband, PLL lock 온도)과 SLT 콘텐츠(DDR training, 멀티코어 hot 부하)로 넘긴다.
>
> **근거.**
> 1. 단계별 역할: 구조 결함(stuck-at, bridge, SRAM)은 CP 에서 싸게 제거·repair, 지연·패키지·온도 의존 결함은 FT, 미션 모드에서만 보이는 결함(PDN, scan 밖 경로, DDR, 상호작용)은 SLT. 설계 결함 5종은 SYSVAL 의 몫이고 양산 스크린으로 막지 않는다.
> 2. 비용: 출하 1개당 테스트비 CP+FT $0.037, +SLT $0.558. SLT 는 DPPM 을 776 → 345 로 줄인다. 손익분기 필드 비용 $1211/건 — 차량용 필드 고장 비용(가정 $5000)보다 훨씬 낮으므로 SLT 를 넣는다. 소비자용 파생 제품(가정 $30)은 SLT 없이 간다.
> 3. SLT 시간: 필드 비용 $5000 기준 SLT 가 이득인 한계는 약 2477 s(41 분). 10 분은 여유가 있으나, 시간보다 **콘텐츠**(어떤 결함을 미션 모드로 재현하나)를 먼저 관리한다.
>
> **리스크·열린 질문.**
> - 345 DPPM 은 차량용 목표(보통 한 자릿수 DPPM 수준을 요구)에 한참 못 미친다. 유출 상위: BRIDGE 60, TDF 60, SA0 50, OXIDE 30, SCAN_HOLE 30 (5종이 308 중 230, 75%). 대책 후보: cell-aware·bridging 패턴과 TDF 패턴 추가(Williams–Brown 상 Y=0.9 에서 100 DPPM 이면 T ≥ 0.99905), PAT(AEC-Q001) 이상치 스크린, 번인 또는 고전압 스트레스(OXIDE), SLT lane margining(LINK_RARE).
> - 번인은 유닛당 $0.78 로 OXIDE 하나만 잡는다면 손익분기 $24k/건 — 단독 근거로는 약하다. HVST 와 비교 후 결정.
> - 마진 결함 리밋은 SYSVAL 특성화 데이터(코너 랏 shmoo)가 나오기 전엔 임시값이다. overkill(좋은 칩 버림)과 유출을 같이 추적한다.
>
> **다음 행동.** ① 테스트 프로그램 릴리스마다 insertion·패턴 세트 diff 를 자동 비교(P1.3 사고 재발 방지). ② RMA 를 이 레슨의 5분류로 주간 리뷰. ③ SLT-only fail 유닛을 FT 로 되돌려 재진단해 "FT 로 옮길 수 있는 SLT 콘텐츠" 를 찾는다 (SLT 시간 절감).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution -lm && ./solution          # 46/46 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san -lm && ./san   # 깨끗, 46/46
```

## 7. 자주 하는 실수

**1. 설계 결함을 양산 스크린으로 막는다.** ROM_ERRATA 를 "SLT 에서 잡히니까 SLT 테스트 항목" 으로 등록하면 모든 유닛이 떨어진다. 설계 결함은 errata·FW 우회·ECO 로 처리하고, 양산 테스트는 "우회가 적용됐는지" 를 확인할 뿐이다. 반대 실수도 있다: 마진 결함(VMIN)을 "설계 문제니 SYSVAL 몫" 으로 두면 느린 유닛이 그대로 나간다 — 특성화는 SYSVAL, screen 은 FT.

**2. 같은 종류 테스트의 반복을 독립 시행으로 계산한다.**

```c
/* 틀림: CP 와 FT 가 같은 scan 패턴인데 독립이라고 가정 */
escaped = ppm * (1 - p_cp) * (1 - p_ft);   /* BRIDGE: 30000 × 0.002 × 0.002 = 0.12 ppm */
/* 맞음(보수적): 커버리지 구멍은 결정적 — 책임 단계 한 번만 */
escaped = ppm * (1 - p_obs);               /* 30000 × 0.002 = 60 ppm */
```

500 배 차이다. 독립 가정은 **간헐** 현상(매번 다른 결과)에만 맞다.

**3. DPPM 분모를 투입 수로 쓴다.** 고객이 받는 건 출하품이다. `escaped / shipped × 1e6`. 수율이 낮은 초기 양산에선 차이가 커진다 (이 모델은 출하 89% 라 775.6 vs 692.0).

**4. `ceil(log(1−CL)/log(1−p))` 를 그대로 쓴다.** p=0.2, CL=0.36 에서 몫이 `2.0000000000000004` 라 3 이 나온다 (실측). 반대 방향 함정도 있다: 경계를 `<=` 로 비교하는 site 수 탐색에서 `10·(1+18·0.05)/19 = 1.0000000000000004` 라 19 대신 20 이 나온다 (연습 3). 고친 코드: `ceil(x − 1e-9)`, `t <= target + 1e-12`.

**5. "다음 단계가 잡았으니 괜찮다".** P1.3 에서 VMIN 일부는 SLT 가 잡는다. 하지만 FT 의 $0.03 불량을 SLT 의 $0.52 에서 버리고, SLT 의 측정 정밀도는 FT 보다 낮으며, 실제로 반품이 왔다. 뒤 단계 fallout 의 **구성 변화** 는 앞 단계 구멍의 신호다.

**6. 반품을 결함 종류만 보고 분류한다.** 같은 SRAM_HOT 도 정상 프로그램이면 STAT_ESCAPE, P1.3 이면 TEST_HOLE 이다. 판정에는 그 유닛이 **어느 프로그램 버전·어느 insertion 으로 테스트됐는지** 가 필요하다 — 그래서 ECID(전자 칩 ID 퓨즈)와 STDF 로그로 유닛 이력을 추적한다.

## 8. 면접 Q&A

<details>
<summary>Q1. ATE, SLT, 시스템 검증의 차이를 한 문장씩.</summary>

ATE(CP/FT)는 **모든 유닛** 을 결함 모델(stuck-at, transition, 메모리 셀)로 초 단위에 테스트하는 구조 테스트, SLT 는 **모든 유닛** 을 실제 시스템과 비슷한 보드에서 부팅·워크로드로 수 분 돌리는 미션 모드 테스트, 시스템 검증은 **샘플 수십 개** 로 설계가 PVT·interop·장시간 조건에서 스펙을 만족하는지 보는 엔지니어링 활동이다. 꼬리질문 "SLT 는 왜 최근 더 중요해졌나?" → SoC 가 커지고 공정이 미세해지면서 구조 테스트 밖 결함(scan 이 닿지 않는 경로, 전원망·di/dt, 상호작용, 작은 지연 결함)이 DPPM 에서 차지하는 비중이 커졌고, 칩렛·고가 패키지에선 출하 후 고장의 비용이 크기 때문이다. 데이터센터의 silent data corruption 보고(Meta "Silent Data Corruptions at Scale", Google "Cores that don't count")는 ATE·SLT 를 통과한 결함 코어가 필드에 있음을 보여 줘, in-field 테스트까지 논의가 넓어졌다.

</details>

<details>
<summary>Q2. 기능 테스트(부팅, 워크로드)를 잘 돌리면 scan 은 필요 없지 않나?</summary>

세 가지 이유로 아니다. ① **측정 가능한 커버리지**: scan 은 fault 목록 대비 "몇 % 를 검사했다" 를 숫자로 낸다(Williams–Brown 에 넣을 T). 기능 테스트는 무엇을 검사했는지 말할 수 없다. ② **시간**: 같은 커버리지를 기능 패턴으로 얻으려면 훨씬 길다. ③ **진단**: scan fail 은 어느 플롭·어느 넷인지 좁혀 수율 학습(yield learning, 공정 피드백)에 쓴다. 꼬리질문 "그럼 SLT 의 효과는 어떻게 재나?" → 결함 모델이 없으니 **실측** 으로: SLT fallout 률, SLT-only fail(ATE 를 통과했는데 SLT 에서 떨어진) 유닛의 FA 결과, SLT 를 뺀 랏과 넣은 랏의 필드 반품률 비교. SLT fail 유닛을 ATE 로 되돌려 재진단해 ATE 패턴을 보강하는 루프가 일반적이다.

</details>

<details>
<summary>Q3. 수율 90%, 커버리지 99% 면 출하 불량률은?</summary>

Williams–Brown 으로 `DL = 1 − 0.9^(1−0.99) ≈ 0.00105`, 약 1053 DPPM. 유도: 수율 `Y = (1−p)^n`, 테스트 통과 확률 `Y^T`, 통과품 중 양품 비율 `Y / Y^T`. 100 DPPM 으로 낮추려면 `T = 1 − ln(1−1e-4)/ln 0.9 ≈ 0.99905`. 꼬리질문 "수율이 80% 로 떨어지면?" → 같은 커버리지에서 DL 이 커지고, 100 DPPM 에 필요한 T 는 0.99955 로 올라간다(연습 2). 초기 양산(수율 낮음)에 출하 품질이 가장 위험한 이유다. "이 식의 한계는?" → 결함 독립·균일 가정. 실제 결함은 웨이퍼에서 뭉쳐(clustering) 같은 커버리지에서 DL 이 이 식보다 낮게 나오는 경향이 있어, 클러스터링 파라미터를 넣은 변형 모델을 쓰기도 한다.

</details>

<details>
<summary>Q4. 부팅 50번에 1번 PCIe 링크가 Gen4 로 안 올라온다. 고쳤다는 걸 어떻게 증명하나?</summary>

수정 전 재현에 95% 신뢰도로 149번 부팅이 필요하다 (`ln 0.05 / ln 0.98 = 148.3`). 수정 후 **149번 연속 무사** 하면 "실패율 < 약 2%" 를 95% 로 주장할 수 있다 — 하지만 그건 "원래 비율보다 낮다" 일 뿐이고, 1/500 로 줄었는지 0 인지는 구분 못 한다. 더 강하게 주장하려면 3/p_target 번(예: 1/1000 → 약 3000번)이 필요하다. 꼬리질문 "149번은 같은 조건이어야 하나?" → 독립·동일 분포 가정이다. 같은 보드·같은 EP·같은 온도에서만 의미가 있다. 실패가 특정 EP·온도·전원 사이클 방식(cold boot vs warm reset)에 몰려 있다면 조건별로 따로 센다. 간헐 버그 증명의 실무 해법은 반복 횟수보다 **원인 메커니즘(EQ preset 협상 로그, LTSSM 상태 기록)으로 수정을 설명하는 것** 이다 (SIV-M04).

</details>

<details>
<summary>Q5. Vmin 이 높은 유닛이 가끔 나온다. 설계 버그인가, 양산 스크린 대상인가?</summary>

모든 유닛이 스펙 전압에서 실패하면 설계(타이밍 closure, 전원망) 문제 → SYSVAL·설계팀. 일부 유닛(공정 산포의 느린 꼬리)만이면 **마진 결함** → SYSVAL 이 코너 랏(SS/TT/FF)과 온도로 shmoo 를 그려(SIV-M07) Vmin 분포를 얻고, FT 에 **guardband** 를 둔 리밋으로 screen 한다. 꼬리질문 "guardband 는 얼마나?" → 테스터 정확도, 테스터-시스템 상관(같은 칩이 ATE 와 실보드에서 다른 Vmin 을 보이는 차이), 온도 차이, 수명 동안의 열화(aging) 여유를 더한다. 너무 크면 좋은 칩을 버리는 overkill(수율 손실), 너무 작으면 유출 — 둘 다 숫자로 추적한다. 빈닝(speed/voltage bin)으로 버리지 않고 낮은 등급으로 파는 것도 선택지다.

</details>

<details>
<summary>Q6. 고객 반품이 들어왔다. validation 엔지니어로서 어떻게 처리하나?</summary>

① ECID 로 유닛 이력(랏, 웨이퍼 좌표, 각 insertion 결과, 테스트 프로그램 버전)을 찾는다. ② 고객 조건으로 재현 → 랩에서 같은 실패 확인. ③ ATE 로 되돌려 현재 프로그램으로 재테스트: 지금 떨어지면 **당시 프로그램 구멍 또는 그 후 열화**, 지금도 통과하면 커버리지 밖. ④ 고장 분석(FA)으로 물리 원인. ⑤ 이 레슨의 5분류로 판정 → 액션(프로그램 수정·랏 격리 / 패턴 추가 / 새 스크린 / errata). 꼬리질문 "재현이 안 되면(NTF, No Trouble Found)?" → NTF 가 흔하다. 고객 보드의 전원·신호 무결성·FW 버전, 사용 온도, 간헐성(Q4 의 반복 수), ESD/EOS 같은 고객 측 손상을 의심한다. NTF 비율 자체를 추적해 고객 지원·레퍼런스 설계 문제를 찾는다.

</details>

<details>
<summary>Q7. 차량용과 소비자용의 양산 테스트는 무엇이 다른가?</summary>

① 목표 DPPM 이 훨씬 낮다(차량용은 "zero defect" 를 지향). ② 온도: 소비자용은 상온 위주, 차량용은 hot/cold 다중 insertion(예: −40 ~ 125 °C 등급에 맞춘). ③ 이상치 스크린: AEC-Q001 PAT(Part Average Testing) — 스펙 안이지만 분포에서 튀는 유닛을 버린다(이상치는 잠재 결함의 신호). ④ 초기 고장: 번인 또는 고전압 스트레스. ⑤ 인증: AEC-Q100 신뢰성 시험(HTOL 등). ⑥ 추적성: ECID·랏 이력으로 리콜 범위를 좁힌다. ⑦ 기능 안전(ISO 26262): 키온 때 LBIST/MBIST 같은 in-field 자가 테스트. 꼬리질문 "PAT 은 정상 칩도 버리지 않나?" → 버린다. 수율 손실을 감수하고 잠재 결함 유닛을 줄이는 거래다. 리밋은 랏마다 분포에서 다시 계산하는 dynamic PAT 이 흔하다.

</details>

<details>
<summary>Q8. 양산 램프 중 SLT fallout 이 0.04% 에서 0.3% 로 뛰었다. 무엇부터 보나?</summary>

먼저 **분해**: 어떤 SLT 테스트에서 떨어졌나(시그니처 클러스터링, SIV-M10), 어떤 랏·웨이퍼·테스터·소켓·SLT 랙에 몰렸나, 언제부터인가(테스트 프로그램 릴리스, FT 프로그램 변경, 공정 변경과 날짜 비교). 이 레슨의 P1.3 이 정확히 이 패턴이다 — FT 가 온도를 잃자 VMIN 류가 SLT 로 밀려와 SLT fallout 구성이 바뀐다. 반품보다 몇 주 먼저 오는 경보다. 꼬리질문 "SLT 소켓 문제와 칩 문제는 어떻게 구분하나?" → 같은 유닛을 다른 소켓에서 재시험(재현이 소켓을 따라가나, 유닛을 따라가나), 소켓별 fallout 통계, 골든 유닛 주기 투입. 장비 문제는 B13 의 INFRA 와 같은 원칙으로 칩 판정에서 분리한다.

</details>

## 9. 추가 연습

### 연습 1 — 번인 단계 추가

단계 표에 BI(번인: `STRESS|TEMP|PKG|VOLUME`, $50/h, 512 소켓, 8 시간)를 FT 와 SLT 사이에 넣어라. OXIDE 는 어디로 가고, 필드 비용 $5000 에서 BI 는 이득인가? 손익분기 필드 비용은?

<details>
<summary>답 보기</summary>

OXIDE 가 ESCAPE → BI 로 옮겨 가 유출이 30·0.95 = 28.5 개 줄어든다. 실제 출력: `ex1: DPPM 345.4 -> 313.4, BI $/unit=0.7812, break-even field cost=$24466`. BI 는 유닛당 $0.78 로 SLT 보다 비싼데 결함 한 종류만 잡는다 → $5000 에서는 손해. 그래서 많은 제품이 번인 대신 FT 의 고전압 스트레스(HVST)나 PAT 로 초기 고장을 줄이고, 번인은 고신뢰 제품이나 신공정 초기에 쓴다. 모델 관점 교훈: 단계를 데이터로 두면 "단계 추가" 가 표에 한 줄 넣는 일이 된다.

</details>

### 연습 2 — 수율과 필요 커버리지

목표 100 DPPM 에 필요한 커버리지를 Y = 0.95 / 0.90 / 0.80 에서 구하고, Y = 0.90 에서 10 DPPM 은?

<details>
<summary>답 보기</summary>

실제 출력: `ex2: T for 100 DPPM: Y=0.95 0.998050  Y=0.90 0.999051  Y=0.80 0.999552 ; 10 DPPM @Y=0.90 0.999905`. 수율이 낮을수록 필요한 T 가 올라가고, DPPM 을 10 배 낮추려면 **놓치는 비율**(1−T)을 약 10 배 줄여야 한다(0.000949 → 0.000095). 99.9% 와 99.99% 의 차이가 패턴 수·테스트 시간으로는 몇 배가 될 수 있다 — 커버리지 곡선은 끝에서 포화하기 때문이다.

</details>

### 연습 3 — 멀티 사이트 효율

흔히 쓰는 정의 `MSE = 1 − (T_N − T_1) / ((N−1)·T_1)` 에서 `T_N = T_1·(1 + (N−1)(1−MSE))`. T_1 = 6 s 일 때 8 site·MSE 0.9 의 유닛당 시간과, 유닛당 1.0 s 이하가 되는 최소 site 수를 MSE 0.90 / 0.85 / 0.80 에서 구하라.

<details>
<summary>답 보기</summary>

실제 출력: `ex3: T/unit(8 sites,MSE .9)=1.275 s  min sites for 1.0 s: MSE .90 -> 14, .85 -> 51, .80 -> -1`. 이상적이면 0.75 s 인데 1.275 s 다 — FT 비용이 $0.031 이 아니라 $0.053 가 된다. 유닛당 시간 `T_1(1−MSE) + T_1·MSE/N` 은 N 이 커져도 `T_1(1−MSE)` 아래로 못 내려간다: MSE 0.8 이면 1.2 s 가 바닥이라 불가능(-1). site 를 늘리는 것보다 **직렬 구간(공유 자원, 순차 측정)을 줄여 MSE 를 올리는 것** 이 먼저인 이유. 경계 함정: T_1 = 10 s·MSE 0.95 에서 19 site 는 `1.0000000000000004` s 라 epsilon 없이 비교하면 20 이 나온다 (practice 의 CHECK).

</details>

### 연습 4 — SLT 시간 상한

필드 비용 $5000 에서 SLT 가 이득인 최대 SLT 시간을 이진 탐색으로 구하라 ("SLT 가 이득인가" 는 시간에 대해 단조). $30 이면?

<details>
<summary>답 보기</summary>

실제 출력: `ex4: SLT break-even time @ $5000 = 2477 s (41.3 min), @ $30 = 14.8 s`. 근사식으로 검산: `t ≈ ΔDPPM/1e6 × 필드비용 × 3600 × sites / rate = 430.2e-6 × 5000 × 3600 × 128 / 400 ≈ 2478 s`. 소비자용에선 15 초짜리 SLT 만 정당화된다 — 사실상 SLT 를 하지 말라는 뜻이다. 이진 탐색의 불변식(`lo` 는 이득, `hi` 는 손해)을 지키면 60 번 반복으로 충분히 정밀하다. 일반화된 경계 bisect 는 SIV-M11.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice   # 20/20 passed
```

## 10. 핵심 정리

- 양산 스크린(CP/FT/SLT)은 **이 유닛** 을, 시스템 검증은 **이 설계** 를 본다. 제조·마진 결함 → 모든 유닛(VOLUME) 필요 → screen. 설계 결함 → 모든 유닛이 가짐 → SYSVAL, 양산 스크린으로 막지 않는다.
- CP: scan stuck-at, MBIST+repair, 가장 쌈. FT: at-speed, hot/cold, 패키지. SLT: 미션 모드(전원망 di/dt, scan 밖 경로, DRAM, 상호작용), 느리고 비쌈. SYSVAL: PARTNER·LONG, 샘플뿐.
- 마진 결함은 SYSVAL 이 특성화 → FT 가 guardband 리밋으로 screen. overkill 과 유출을 같이 본다.
- `DL = 1 − Y^(1−T)`: Y 0.9·T 0.99 → 약 1053 DPPM. 100 DPPM 이면 T ≥ 0.99905.
- 간헐 결함: `k ≥ ln(1−CL)/ln(1−p)` ≈ 3/p (95%). 1/50 → 149 회. VOLUME+LONG 을 동시에 요구하는 결함은 어떤 단계도 못 본다 → 마진 측정(lane margining)이나 스트레스로 바꿔 푼다.
- 커버리지 구멍은 결정적: 같은 테스트 반복을 독립 시행으로 계산하지 않는다.
- SLT 가치 = ΔDPPM × 필드 비용 vs 유닛당 SLT 비용. 이 모델: 손익분기 $1211/건, $5000 기준 SLT 상한 약 41 분.
- RMA 5분류: TEST_HOLE(프로그램 구멍, 즉시 수정·랏 추적) / STAT_ESCAPE / FLOW_GAP / NO_SCREEN / VALIDATION_ESCAPE. 판정엔 ECID·프로그램 버전 이력이 필요하다.

## 참고자료

- T. W. Williams, N. C. Brown, "Defect Level as a Function of Fault Coverage", *IEEE Transactions on Computers*, 1981 — `DL = 1 − Y^(1−T)`
- M. Bushnell, V. Agrawal, *Essentials of Electronic Testing for Digital, Memory and Mixed-Signal VLSI Circuits*, Springer — fault model, scan, ATE, 결함 수준
- L.-T. Wang, C.-W. Wu, X. Wen (eds.), *VLSI Test Principles and Architectures*, Morgan Kaufmann 2006 — at-speed scan(LOC/LOS), MBIST·repair, IDDQ
- IEEE 1149.1 (JTAG), IEEE 1500 (embedded core test), IEEE 1687 (IJTAG) — DFT 접근 표준
- [AEC 문서](http://www.aecouncil.com/AECDocuments.html) — AEC-Q100 (집적회로 스트레스 시험 인증), AEC-Q001 (Part Average Testing)
- PCI Express Base Specification 4.0 이후 — Lane Margining at the Receiver
- J. A. Hanley, A. Lippman-Hand, "If nothing goes wrong, is everything all right?", *JAMA* 1983 — rule of three
- H. D. Dixit et al., "Silent Data Corruptions at Scale", arXiv 2021 (Meta); P. H. Hochschild et al., "Cores that don't count", HotOS 2021 (Google)
- STDF (Standard Test Data Format) — ATE 결과 로그 형식
- 관련 레슨: SIV-B13 (설계 단계별 발견), SIV-B12 (테스트 계획), SIV-B14 (리포트), SIV-M04 (PCIe 링크·LTSSM), SIV-M05 (DDR training), SIV-M06 (PRBS·BER 신뢰구간), SIV-M07 (shmoo·Vmin), SIV-M10 (triage 클러스터링), SIV-M11 (bisect), SIV-M12 (시퀀서), SIV-A08 (수율·binning·테스트 순서 최적화), SIV-A10 (LFSR BIST·MISR)
