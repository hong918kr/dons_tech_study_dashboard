---
id: SIV-A02
time: 110분
updated: 2026-11-08
---
# SIV-A02 바운더리 스캔(EXTEST)으로 보드 연결 테스트

## 학습 목표

- BSDL의 `BOUNDARY_REGISTER` 항목(셀 번호, 셀 종류, 포트, 기능, safe, 제어 셀, disable value, disable result)을 읽고, 핀이 어떤 셀로 구동·관측되는지 표로 만든다.
- walking-1과 수정 카운팅(modified counting) 코드의 벡터 수와 진단력을 비교하고, **aliasing / confounding** 을 숫자로 설명한다.
- EXTEST 흐름(PRELOAD → EXTEST → 벡터 + 한 번의 플러시, 응답은 한 스캔 늦게 나옴)을 레지스터 수준으로 구현한다.
- **드라이버 충돌 안전**: 넷당 드라이버 하나, 제어 셀 공유, `output2`, hold 넷(`PMIC_EN`, `RST_N`), PRELOAD 없는 EXTEST의 위험을 코드로 강제한다.
- open · stuck · short 를 심은 가상 보드에서 **2단계 진단**(카운팅 → 의심 넷만 walking)으로 결함 위치와 종류를 찾아 디버그 엔지니어용 리포트를 만들고, 이 방법이 못 보는 것을 말한다.

## 1. 문제

첫 보드가 왔는데 부팅이 안 된다. 펌웨어 탓인가, 납땜 탓인가? BGA 아래 볼은 프로브가 닿지 않는다 — **바운더리 스캔(boundary scan, IEEE 1149.1)** 은 칩의 핀 바로 안쪽에 있는 시프트 레지스터로 핀을 직접 구동·관측해서 이 질문에 답하는 방법이고, 보드 브링업/실리콘 검증 면접에서 "first board에서 무엇부터 보나"의 표준 답이다.

JTAG 포트로 **BSR(Boundary-Scan Register)** 을 다룬다. EXTEST 명령을 넣으면 코어 로직은 핀에서 떨어지고, BSR 출력 셀이 핀을 구동하며 입력 셀이 핀을 캡처한다. 그러면 칩 A의 출력 → PCB 배선 → 칩 B의 입력이 그대로 시험 경로가 된다. 시험 대상은 **칩이 아니라 칩 사이의 연결**(납땜 불량, 끊긴 배선, 이웃 넷 단락)이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="siva02-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">체인과 셀: TDI → U1 → U2 → U3 → TDO (cell 0 은 TDO 쪽)</text>
  <rect x="20" y="40" width="190" height="130" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="115" y="60" fill="currentColor" text-anchor="middle">U1 SNS_SOC (52비트)</text>
  <rect x="40" y="72" width="70" height="36" class="f-warn" stroke="currentColor"/>
  <text x="75" y="88" fill="currentColor" text-anchor="middle" font-size="12">OUT 셀</text>
  <text x="75" y="102" fill="currentColor" text-anchor="middle" font-size="12">output3</text>
  <rect x="120" y="72" width="70" height="36" class="f-mut" stroke="currentColor"/>
  <text x="155" y="88" fill="currentColor" text-anchor="middle" font-size="12">제어 셀</text>
  <text x="155" y="102" fill="currentColor" text-anchor="middle" font-size="12">0 = Z</text>
  <rect x="40" y="120" width="150" height="36" class="f-ok" stroke="currentColor"/>
  <text x="115" y="143" fill="currentColor" text-anchor="middle" font-size="12">IN 셀: 핀을 캡처 (read-back)</text>
  <rect x="285" y="40" width="190" height="130" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="380" y="60" fill="currentColor" text-anchor="middle">U2 IO_FPGA (63비트)</text>
  <rect x="305" y="72" width="150" height="36" class="f-warn" stroke="currentColor"/>
  <text x="380" y="95" fill="currentColor" text-anchor="middle" font-size="12">BC_7 bidir 셀 + 제어 셀 (1 = Z)</text>
  <rect x="305" y="120" width="150" height="36" class="f-ok" stroke="currentColor"/>
  <text x="380" y="143" fill="currentColor" text-anchor="middle" font-size="12">IN 셀 (A, nCS, Y, ...)</text>
  <rect x="550" y="40" width="170" height="130" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="635" y="60" fill="currentColor" text-anchor="middle">U3 LVL_BUF (17비트)</text>
  <rect x="570" y="72" width="130" height="36" class="f-warn" stroke="currentColor"/>
  <text x="635" y="95" fill="currentColor" text-anchor="middle" font-size="12">B(0..7): OE 1개 공유</text>
  <rect x="570" y="120" width="130" height="36" class="f-ok" stroke="currentColor"/>
  <text x="635" y="143" fill="currentColor" text-anchor="middle" font-size="12">A(0..7) 입력</text>
  <line x1="210" y1="90" x2="303" y2="90" stroke="currentColor" stroke-width="2"/>
  <text x="256" y="84" fill="currentColor" text-anchor="middle" font-size="12">DATA 넷</text>
  <line x1="210" y1="137" x2="303" y2="137" stroke="currentColor" stroke-width="2"/>
  <text x="256" y="131" fill="currentColor" text-anchor="middle" font-size="12">ADDR 넷</text>
  <line x1="475" y1="137" x2="568" y2="137" stroke="currentColor" stroke-width="2"/>
  <text x="522" y="131" fill="currentColor" text-anchor="middle" font-size="12">BUF / ADDR</text>
  <text x="10" y="203" fill="currentColor">TDI</text>
  <line x1="42" y1="198" x2="705" y2="198" stroke="currentColor" marker-end="url(#siva02-a)"/>
  <text x="710" y="203" fill="currentColor" font-size="12">TDO</text>
  <text x="115" y="192" fill="currentColor" text-anchor="middle" font-size="12">비트가 시프트되어 체인을 지나간다</text>
  <rect x="20" y="218" width="700" height="104" rx="6" fill="none" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="32" y="238">BSDL 규칙: cell 0 은 TDO 에 가장 가깝다.</text>
    <text x="32" y="256">시프트: U3 의 cell 0 이 맨 먼저 들어가 맨 끝까지 가고, 맨 처음 나온다.</text>
    <text x="32" y="274">DR 스캔 1회 = Capture-DR(핀 값 잠금) → Shift → Update-DR(새 값이 핀으로 나감).</text>
    <text x="32" y="292">벡터 k 의 응답은 스캔 k+1 의 Capture 에서 잡힌다. 벡터 N 개는 스캔 N+1 번.</text>
    <text x="32" y="310">출력 전용 셀의 캡처는 코어 쪽 값이다. 핀 정보가 아니므로 비교에 쓰지 않는다.</text>
  </g>
</svg>
```

테스트 대상 보드 `VAL-R2`(가상): BSDL 3개(부품 실물 파일이 아니라 표준 문법으로 쓴 가상 부품)와 넷리스트 하나.

| 넷 묶음 | 연결 | 왜 넣었나 |
|---|---|---|
| `DATA0..15` | U1.D ↔ U2.D | 드라이버 2개(양방향 버스) · U1은 바이트당 제어 셀 공유 · 양쪽 다 read-back |
| `ADDR0..7` | U1.A → U2.A, U3.A | 수신 2개 → open 핀을 **어느 쪽인지** 구분 가능 · U1 출력 전용(read-back 없음) |
| `nCS nWE nOE` | U1 → U2 | 2핀 넷, read-back 없음 |
| `BUF0..7` | U3.B → U2.Y | OE 하나를 8핀이 공유 |
| `IRQ_N` | U2.IRQ → U1 | `output2`(끌 수 없음) |
| `RST_N`, `PMIC_EN` | 시스템 필수 | **패턴 금지**: hold 넷 |
| `CLK_25M` | 오실레이터 → U2.CLK | 드라이버 없음: 관측만 |
| `TP_VSENSE` | 스캔 없는 테스트 포인트 | 못 본다 |

BSDL 한 줄은 이렇게 생겼다. 왼쪽부터 `셀 번호 (셀 종류, 포트, 기능, safe [, 제어 셀, disable value, disable result])`.

```text
"1 (BC_1, B(0), output3, X, 0, 0, Z)," &     -- 셀 1 은 핀 B(0) 를 구동, 셀 0 이 0 이면 Z
"0 (BC_1, *, control, 0)," &                 -- 셀 0 은 포트 없는 제어 셀, safe 값 0
```

### Part A — BSDL, 넷, 코드, EXTEST

- `parse_cells` / `parse_opcodes` / `lint_device` — 위 형식과 `INSTRUCTION_OPCODE`("EXTEST (0000)")를 읽고 길이·제어 셀·코드 길이를 검사한다. `bit_vector` 의 `to` / `downto` 와 PIN_MAP 은 뼈대가 해 준다.
- `net_roles` — 넷마다 드라이버/수신 핀과 분류(testable, drive_only, observe_only, unscannable).
- `gen_walking_1`, `gen_walking_0`, `gen_counting_modified`, `gen_true_complement` — 넷별 코드(STV)와 벡터 수.
- `safe_image`, `vector_bits`, `run_extest`, `diagnose` — 안전값 위에 계획된 드라이버만 켜서 벡터를 만들고, 응답으로 결함을 분류한다.

### Part B — 충돌 안전과 진단력의 한계

- `plan_drivers` — 넷당 드라이버 하나(제어 셀 공유, `output2`, hold, 드라이버 rotation).
- `short_read`, `diagnose_codes`, `connect_groups`, `adaptive_codes` — 코드 수준 aliasing/confounding 측정과 walking 2단계 분해.
- 한계 측정: 단순 카운팅의 all-0 코드, 다중 결함 마스킹, 비-스캔 넷 단락.

### Part C — VAL-R2 보드 캠페인

- `resolve_shorts`, `adaptive_test`, `format_report` — 1단계(수정 카운팅 6 벡터) → 의심 넷만 2단계(walking-1, walking-0, 배경 벡터) → 리포트. 결함 4개를 심은 보드에서 위치와 종류를 찾고, 무작위 단일 결함 120개를 전부 맞힌다.

## 2. Problem (English)

> **Board interconnect test with IEEE 1149.1 boundary scan (register-level).**
>
> You are given BSDL files for the devices in a JTAG chain (subset: `BOUNDARY_LENGTH`, `INSTRUCTION_LENGTH`, `INSTRUCTION_OPCODE`, `BOUNDARY_REGISTER`, `PIN_MAP`) and a netlist mapping net → device.pin. A `VirtualBoard` model implements IR scan and DR scan at the register level (Capture-DR, Shift, Update-DR). Faults: an open pin, a dead output, a stuck net, or a short (wired-AND, wired-OR, or dominant driver). Using only the Python 3 standard library, implement:
>
> 1. `parse_cells(text)`, `parse_opcodes(text)`, `lint_device(dev)`. A boundary register entry is `num (cell, port, function, safe [, ccell, disval, rslt])`. Cell 0 is closest to TDO.
> 2. `gen_walking_1(n)`, `gen_walking_0(n)`, `gen_counting_modified(n)` (no all-0 or all-1 net codes, `k = ceil(log2(n+2))` vectors), `gen_true_complement(n)`.
> 3. `plan_drivers(roles, devs, prefer, hold)`: exactly one driver per tested net; control cells may be shared by several pins (enabling one enables all); `output2` cannot be disabled. Held nets are driven to a fixed value and never toggled.
> 4. `run_extest(board, plan, roles, codes, n_vec)`: PRELOAD safe values, enter EXTEST, shift the vectors, flush with safe values. The response to vector *k* appears on scan *k+1*.
> 5. `diagnose(plan, roles, codes, n_vec, resp)`: classify OPEN_RX, OPEN_NET/OPEN_DRV, STUCK, STUCK_OR_OPEN, SHORT, SHORT_OR_STUCK.
> 6. `adaptive_test(board, plan, roles, codes, k)`: re-test only the suspect nets with walking-1 and walking-0 plus an all-0 / all-1 background vector; `resolve_shorts` must report the exact shorted groups and whether the short is wired-AND, wired-OR, or dominant.
>
> **Example.** On the VAL-R2 board with four planted defects (an open pin on `ADDR3` at U2, a wired-AND short `DATA5`–`DATA6`, a stuck-at-0 `nCS`, a wired-OR short `BUF2`–`BUF3`) the tool reports four findings using 6 counting vectors plus 12 step-2 vectors. `BUF6`, whose code happens to equal another short's response, is a suspect after step 1 and is cleared by step 2.

## 3. 시작 코드

<!-- code: starter.py -->

데이터는 결정적으로 다시 만들 수 있다.

<!-- code: gen_board.py -->

```text
cd lessons/SIV/SIV-A02
python3 -I gen_board.py          # u1_sns_soc.bsd u2_io_fpga.bsd u3_lvl_buf.bsd board.net 재생성 (커밋된 파일과 동일)
python3 -I starter.py; echo "exit=$?"
```

시작 코드의 실제 출력 (`TODO` 는 `NotImplementedError` 로 표시되고 테스트 묶음 단위로 FAIL 된다):

```text
FAIL t_parse (TODO not implemented)
FAIL t_lint (TODO not implemented)
FAIL t_chain (TODO not implemented)
FAIL t_roles (TODO not implemented)
FAIL t_codes (TODO not implemented)
FAIL t_extest (TODO not implemented)
FAIL t_diag (TODO not implemented)
FAIL t_safety (TODO not implemented)
FAIL t_shared_control (TODO not implemented)
FAIL t_rotation (TODO not implemented)
FAIL t_aliasing (TODO not implemented)
PASS L1 plain counting misses SA0 on all-zero code
PASS L1 plain counting misses SA1 on all-one code (n=2^k)
FAIL t_limits (TODO not implemented)
FAIL t_campaign (TODO not implemented)
2/15 passed
exit=1
```

`VirtualBoard`, `to_stream`, `ir_stream`, `parse_netlist`, `parse_pin_map` 은 정답 도구가 검사하는 **고정 코드**다. 벡터는 `BC_7` 처럼 핀을 읽는 셀과 코어만 읽는 출력 셀을 구분해 모델링되어 있다. 채울 곳은 `TODO` 18군데다.

## 4. 힌트

<details><summary>힌트 1 — BSDL 파서의 함정 세 가지</summary>

`--` 주석을 먼저 지운다(주석 안에도 셀처럼 생긴 문자열이 있다, `TINY` 테스트 첫 줄). 속성 값은 `"..." & "..."` 로 쪼개져 있으니 이어 붙인다. 포트 칸은 `*` 또는 `D(3)` 처럼 괄호를 포함하므로 포트 정규식이 `\w+(\(\d+\))?` 여야 전체 항목이 끝까지 맞는다. 없는 필드는 `None`.
</details>

<details><summary>힌트 2 — 벡터 k 의 응답은 언제 나오나</summary>

Capture-DR 은 스캔의 맨 앞, Update-DR 은 맨 뒤다. 스캔 k 의 Update 로 나간 값은 다음 스캔 k+1 의 Capture 로 잡혀 TDO 로 나온다. 그래서 벡터 N 개에는 스캔 N+1 번(마지막은 안전값 플러시)이 필요하고, 첫 EXTEST 스캔이 내놓는 값은 버린다.
</details>

<details><summary>힌트 3 — 코드 수 공식</summary>

전부 0 / 전부 1 코드를 빼면 쓸 수 있는 코드는 2^k − 2 개다. 2^k − 2 ≥ N 이 되는 가장 작은 k 는 ⌈log2(N+2)⌉. 코드는 그냥 1..N 을 쓰면 된다.
</details>

<details><summary>힌트 4 — 진단은 "응답이 같으면 묶는다"</summary>

수신 핀 일부만 틀리면 그 핀만의 문제(open). 전부 틀리고 값이 같으면 넷 전체의 문제고, 틀린 값으로 넷을 묶으면 단락 후보가 나온다. 틀린 넷 하나가 어떤 다른 넷의 코드와 같은 값을 읽는다면 그 다른 넷은 멀쩡해 보이는 단락 짝일 수 있다(alias).
</details>

<details><summary>힌트 5 — 제어 셀 공유가 왜 백트래킹을 부르나</summary>

제어 셀을 켜면 형제 핀이 **모두** 켜진다. 그래서 넷 하나의 드라이버 선택이 다른 넷의 드라이버를 강제한다. 넷을 순서대로 보며 선택을 시도하고(형제까지 전파), 충돌하면 다음 후보로. 끝까지 안 되면 `PlanError` — 보드가 시험 불가능하다고 알리는 것도 답이다.
</details>

<details><summary>힌트 6 — 2단계에 왜 벡터가 하나 더 필요한가</summary>

의심 넷 S 가 정확히 2개이고 wired-AND 면, walking-1 의 두 벡터(10, 01)는 AND 결과가 모두 0, walking-0 의 두 벡터(01, 10)도 모두 0 이다. "둘 다 stuck-at-0" 과 구별이 안 된다. 전부 1을 넣는 벡터가 하나 있어야 "둘이 1을 낼 때 1이 읽히는지"를 볼 수 있다. walking-1 쪽에도 전부 0 벡터를 대칭으로 넣는다.
</details>

## 5. 풀이

### Part A — 핀이 어떤 셀인지 먼저 안다

**기능 칸이 핀의 모든 것을 정한다.**

| `function` | 뜻 | 캡처로 핀을 읽나 | 끌 수 있나 |
|---|---|---|---|
| `input`, `clock` | 입력 핀 관측 | 예 | — |
| `output2` | 2상태 출력 (제어 셀 없음) | 아니오 | **못 끈다** |
| `output3` | 3상태 출력 (제어 셀 + disable result) | 아니오(코어 쪽 값) | 예 |
| `bidir` | 셀 하나가 구동+캡처 (`BC_7` 등, 표준 패키지에 셀별 동작이 정의됨) | 예 | 예 |
| `control`, `controlr` | 출력 enable 을 쥔 셀 (`*` 포트) | — | — |
| `internal` | 핀이 없는 내부 셀 | — | — |

`safe` 는 "소프트웨어가 값을 정하지 않았을 때 update 래치에 넣을 값"이고 `X` 는 상관없음이다. 제어 셀의 `disable value` 는 그 값이면 출력이 꺼진다는 뜻으로, 부품마다 극성이 다르다. U1은 0일 때 Z, U2는 1일 때 Z다 — `vector_bits` 가 드라이버를 켤 때 `1 - disval` 을 쓰는 이유이고, `A6 enable polarity` 테스트가 이것을 잡는다.

**EXTEST 코드는 파일에서 읽는다.** 구버전 부품은 EXTEST 가 전부 0인 경우가 많지만 U2 는 `100110` 이다. 하드코딩하면 칩 하나가 다른 명령에 들어가 버린다. (IR 시프트는 BSDL 표기의 오른쪽 비트가 TDO 쪽, 즉 먼저 시프트된다.)

**시프트 순서**를 틀리면 조용히 모든 비트가 어긋난다. BSDL cell 0 이 TDO 에 가장 가깝다. 그래서 체인 전체를 비트 시간 순으로 쓰면:

```text
TDI → [U1 cell51..0] → [U2 cell62..0] → [U3 cell16..0] → TDO
tdo[0] = U3.cell0, ..., tdo[16] = U3.cell16, tdo[17] = U2.cell0 ...
tdi[0] 은 같은 자리에 도착한다 → to_stream / from_stream 은 같은 순서
```

**EXTEST 흐름**: PRELOAD 없이 EXTEST 에 들어가면 update 래치의 전원 투입 쓰레기 값이 그대로 핀으로 나간다(Part B).

```text
load_ir(PRELOAD)  → shift_dr(안전값)            # 구동 없음, 래치만 채움
load_ir(EXTEST)                                 # 이 순간부터 래치가 핀을 구동
for k = 0..N-1: shift_dr(벡터 k)                 # 첫 스캔의 출력은 버린다
shift_dr(안전값)                                # 마지막 응답을 받고 다시 안전값으로
```

스캔 수는 preload 1 + N + 플러시 1 이다. `A6 good board: scans = 1 preload + k + 1 flush` 가 확인한다. 응답 비트는 벡터 인덱스로 STV(Sequential Test Vector, 넷 하나의 시간축 코드)에 모은다. 반대로 벡터 하나의 전 넷 값 묶음은 PTV(Parallel Test Vector)다.

**넷 역할.** `drivers` 는 출력 셀이 있는 핀, `receivers` 는 입력 캡처가 가능한 핀이다(양방향은 양쪽 다). 분류 규칙은 "드라이버 d 와, d 아닌 다른 핀 r 이 캡처할 수 있으면 testable"이다. `DATA0` 는 드라이버 2, 수신 2, testable. `PMIC_EN` 은 U1 출력에 스캔 없는 PMIC 입력 하나라 `drive_only`. `CLK_25M` 은 드라이버가 스캔에 없어 `observe_only`.

**코드 선택.** 넷 N개에 코드를 주는 방식:

| 방식 | 벡터 수 | 코드 조건 | 잡는 것 / 약점 |
|---|---|---|---|
| Walking-1 | N | one-hot | wired-OR 단락을 그룹까지 정확히. wired-AND 는 전부 0 (stuck 과 같다) |
| Walking-0 | N | one-cold | walking-1 의 쌍대 |
| 단순 카운팅 (Kautz) | ⌈log2 N⌉ | 0..N−1 | 짧다. all-0 코드 넷의 SA0 을 못 잡는다 |
| 수정 카운팅 | ⌈log2(N+2)⌉ | 1..N | stuck/open 에 안전. aliasing 많음 |
| 참/보수 카운팅 | 2⌈log2 N⌉ | (c, ~c) | 2넷 단락 aliasing 없음. confounding 남음 |

단순 카운팅의 약점은 한 줄로 끝난다: 코드 0 인 넷이 stuck-at-0 이면 응답도 0 이다. `L1` 이 이 동치를 코드로 보여 주고, 수정 카운팅이 왜 2^k 를 넘겨 한 벡터 더 쓰는지(N=8 이면 3이 아니라 4)도 확인한다.

**진단(`diagnose`) 표.** 핵심은 "모든 수신 핀이 같은 상수를 읽는가"와 "드라이버 read-back 이 맞는가"다.

| 증상 | 판정 | 왜 |
|---|---|---|
| 수신 일부만 틀림 | `OPEN_RX` 그 핀 | 나머지 수신이 정상이면 드라이버와 넷은 살아 있다 |
| 수신 전부 같은 상수, read-back = 코드 | `OPEN_NET`(수신 1) / `OPEN_DRV`(수신 2+) | 드라이버 패드는 자기 값을 보고 있는데 넷이 안 움직인다 |
| 수신 전부 같은 상수, read-back 도 상수 | `STUCK` | 패드까지 고정 — 넷이 전원/GND 에 붙음(또는 죽은 출력단) |
| 수신 전부 같은 상수, read-back 없음 | `STUCK_OR_OPEN` | 출력 전용 셀은 핀을 못 읽는다 |
| 2개+ 넷이 같은 틀린 값 | `SHORT` | 묶어서 하나의 전기 노드 |
| 2개+ 넷이 같은 상수 | `SHORT_OR_STUCK` | 이 패턴만으로는 구분 불가 |

2핀 넷의 open 은 **어느 쪽 끝인지 원리적으로 모른다**(`OPEN_NET`). 수신이 하나뿐이라 U2 쪽이 끊겼는지 U1 쪽이 끊겼는지 드러날 다른 관측점이 없다. `DATA4` 의 U1/U2 양쪽 케이스가 같은 판정이 되는 것을 테스트로 확인한다. 핀 단위 위치가 필요하면 측정기를 대야 한다.

**시뮬레이터가 말해 주는 물리.** `add_open` 은 핀을 넷에서 떼어 낸다. 떨어진 핀은 자기 패드 값을 본다: 출력단이 살아 있으면 자기 구동값(그래서 read-back 이 맞다), 아니면 자신의 disable result(pull) 또는 보드 기본값. 끊긴 핀이 읽는 값과 넷의 풀업은 다르다 — 풀업 저항은 끊긴 곳 건너편에 있다. 이것을 틀리면 "open 인데 pull 값이 안 읽힌다"에 시달리게 된다. 키퍼(bus-hold)는 직전 값을 잡고 있다(`float_default="K"`). 코드가 상수가 아닌 한 키퍼 open 도 잡힌다(`A7 keeper open`, SIV-B05의 bus-hold 함정과 같은 현상이다).

### Part B — 안전이 먼저, 그다음 진단력

바운더리 스캔은 **핀을 강제로 구동**하는 도구다. 잘못 쓰면 시험이 보드를 망가뜨린다.

**규칙 1: 넷당 드라이버 하나.** `plan_drivers` 가 보장한다. 켜지 않은 출력은 전부 제어 셀을 disable 로 둔다. 제어 셀이 없는 `output2` 는 항상 구동하므로 그 핀이 곧 드라이버여야 하고, 한 넷에 `output2` 둘이면(`B6`) 안전한 패턴이 없어 `unsafe` 로 빼고 시험하지 않는다.

**규칙 2: 제어 셀 공유.** U1 의 `D(0..7)` 는 제어 셀 하나, U3 의 `B(0..7)` 도 하나다. 켜면 형제가 다 켜진다. 그래서 `place()` 는 선택을 형제에게 전파하고, 형제 핀이 계획 밖 넷에 있으면 거부한다(`B7`, `PlanError`). 무접속 핀은 켜져도 건드릴 게 없으니 허용. 전파가 없으면 시험 도중 예상 못 한 넷을 구동한다.

**규칙 3: 안전값부터.** 전원 투입 시 update 래치는 쓰레기다.

```text
EXTEST 진입(Update-IR) 순간 BSR 값이 핀으로 나간다.
PRELOAD 없이 EXTEST 로 가면: seed=7 인 보드에서 Update-IR 직후 충돌 발생 (B2)
```

**규칙 4: hold 넷은 EXTEST 진입 순간부터 구동.** `RST_N`, `PMIC_EN` 을 패턴으로 토글하면 보드가 리부트되거나 전원이 꺼진다. 그런데 `safe_image` 로 그냥 tri-state 해도 안 된다 — EXTEST 에 들어가는 순간 코어가 핀을 놓으므로 구동자가 없어진다. 그래서 PRELOAD 이미지에 hold 넷의 드라이버를 켠 채 hold 값을 넣고, 플러시도 같다. `B3 held nets actively driven` 이 EXTEST 구간 내내 드라이버가 있었는지 검사한다(이 줄을 빼는 변이를 넣으면 이 테스트 하나만 깨진다).

**규칙 5: 드라이버 rotation.** 넷 하나에 드라이버가 둘이면 한 번의 패턴으로는 한쪽만 시험한다. U1 이 구동하는 동안 U2 의 **출력단이 죽어** 있어도 U2 는 입력으로 멀쩡하다. `B8` 이 보여 주는 것: prefer=U1 계획에서 `DATA11` 의 U2 dead 는 `findings == []`, prefer=U2 계획에서 잡힌다. 대가는 벡터가 2배.

**이제 진단력.** 단락 모델이 wired-AND 면 넷 a, b 는 둘 다 `a&b` 를 읽고, wired-OR 면 `a|b` 를 읽는다.

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">같은 단락, 코드에 따라 다른 모습 (넷 6개, 3비트 수정 카운팅: 넷 i 의 코드 = i+1)</text>
  <g fill="currentColor" font-family="monospace">
    <text x="20" y="44">net code</text>
    <text x="20" y="62">N3  100</text>
    <text x="20" y="80">N4  101</text>
    <text x="20" y="98">N5  110</text>
  </g>
  <rect x="150" y="30" width="270" height="84" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="165" y="50" font-weight="bold">Aliasing: N4 와 N5 가 wired-AND 단락</text>
    <text x="165" y="68" font-family="monospace">101 &amp; 110 = 100</text>
    <text x="165" y="86">응답이 N3 의 코드와 같다.</text>
    <text x="165" y="104">N3 도 단락에 끼었는지 모른다.</text>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="20" y="150">walking-1</text>
    <text x="20" y="168">a  1000</text>
    <text x="20" y="186">b  0100</text>
    <text x="20" y="204">c  0010</text>
    <text x="20" y="222">d  0001</text>
  </g>
  <rect x="150" y="134" width="280" height="100" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="165" y="154" font-weight="bold">Confounding: {a,b} 와 {c,d} 가 wired-AND</text>
    <text x="165" y="172">a&amp;b = c&amp;d = 0000: 네 넷이 같은 값</text>
    <text x="165" y="190">한 덩어리인지 두 쌍인지 모르고,</text>
    <text x="165" y="208">stuck-at-0 과도 구별이 안 된다</text>
    <text x="165" y="226">wired-OR: 1100, 0011 → 완벽히 분리</text>
  </g>
  <rect x="445" y="30" width="285" height="204" rx="6" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="458" y="50" font-weight="bold">2단계 walking 의 해석 (의심 넷 S)</text>
    <text x="458" y="70">walk-1: i 만 1. j 가 1 이면 i 를 따름</text>
    <text x="458" y="88">walk-0: i 만 0. j 가 0 이면 i 를 따름</text>
    <text x="458" y="108">wired-OR: walk-1 에서 모두 올라감</text>
    <text x="458" y="126">wired-AND: walk-0 에서 모두 내려감</text>
    <text x="458" y="144">dom:a: b 만 a 를 양방향으로 따라감</text>
    <text x="458" y="166">+배경 벡터(전부 0 / 전부 1):</text>
    <text x="458" y="184">|S|=2 wired-AND: 두 walk 모두 0</text>
    <text x="458" y="202">"전부 1" 에서만 1 → stuck 과 갈린다</text>
    <text x="458" y="224">→ S 만 2(|S|+1) 벡터로 그룹·종류 확정</text>
  </g>
  <text x="10" y="262" fill="currentColor">aliasing: 틀린 응답이 제3의 정상 코드와 일치. confounding: 다른 단락들이 같은 응답으로 합쳐짐.</text>
  <text x="10" y="280" fill="currentColor">물리 단락은 AND/OR 를 미리 모르므로 walking 은 양쪽을 다 돌린다.</text>
</svg>
```

**측정값**: 넷 수와 결함 모델별로, 추정 그룹이 심은 그룹과 정확히 같고 모호하지 않은 비율(`python3 -I solution.py --report`, 실제 출력).

```text
--- aliasing/confounding study: n=14 nets, exact diagnosis %
scheme         pair-AND   pair-OR  2pairs-AND  2pairs-OR  triple-AND  avg_vec
counting-mod      39.6      39.6      15.2      15.2      18.1      4.0
walking-1          0.0     100.0       0.0     100.0       0.0     14.0
walking-0        100.0       0.0     100.0       0.0     100.0     14.0
true-compl        93.4      93.4      82.5      82.5      63.7      8.0
2-step           100.0     100.0     100.0     100.0     100.0     13.7

--- aliasing/confounding study: n=36 nets, exact diagnosis %
scheme         pair-AND   pair-OR  2pairs-AND  2pairs-OR  triple-AND  avg_vec
counting-mod      30.5      51.6       7.8      24.8      12.0      6.0
walking-1          0.0     100.0       0.0     100.0       0.0     36.0
walking-0        100.0       0.0     100.0       0.0     100.0     36.0
true-compl        99.4      99.4      97.5      97.5      93.0     12.0
2-step           100.0     100.0     100.0     100.0     100.0     14.6
```

읽는 법:

- **검출(detection)** 은 모든 행이 100% 다(`B10 counting detects every pair`). 네 방식 모두 단락이 있으면 *뭔가 틀렸다*는 건 안다. 표는 **진단(diagnosis)** — 어떤 넷끼리인지 — 의 차이다.
- walking-1 은 wired-AND 에서 0%, walking-0 은 wired-OR 에서 0% — 단락 물리를 모르면 어느 하나만으로는 위험하다. walking-1 + walking-0 둘 다 돌리면 N 이 2배가 된다.
- 수정 카운팅은 빠르지만 정확 진단이 낮다. 응답이 다른 코드와 겹치거나(aliasing) 두 단락이 합쳐지기(confounding) 때문이고, wired-AND 두 쌍은 넷이 늘면 더 떨어졌다(n=14 → 36 에서 15.2% → 7.8%).
- 참/보수는 2넷 단락에서 제3 코드 aliasing 이 없다. 남은 실패는 서로 보수인 두 코드가 단락되어 전부 0 을 읽는 경우로, stuck 과 모호해진다(n=14 에서 코드합이 15 인 쌍 6개, 6/91 = 6.6% → 표의 93.4%).
- 2-step 은 측정한 시나리오 전부 100% 인데 벡터는 넷 수에 거의 비례하지 않는다(n=36: 평균 14.6 vs walking-1 36). 이 평균은 쌍·두 쌍·삼중 단락 5개 시나리오의 평균이고, 의심 넷이 많으면 커진다.
- 표의 exact 는 "추정 그룹 = 심은 그룹, 모호하지 않음" 이라는 코드 수준 정의다. `2-step` 의 100% 는 **모델 안의 결과**다. 측정한 시나리오에서만이고, 물리 단락이 임계값 근처에서 비논리 값을 내면 틀릴 수 있다.

**한계 (테스트로 확인)**

| 한계 | 테스트 | 왜 못 보나 |
|---|---|---|
| all-0/all-1 코드 | `L1` | 코드가 상수면 stuck 과 같은 응답 |
| 다중 결함 마스킹 | `L2` | `nCS` 의 유일한 수신 핀이 끊기면 `nCS`↔`nWE` 단락의 한쪽 흔적이 사라진다 — `nCS` 는 `STUCK_OR_OPEN`, 단락 그룹이 안 나온다 |
| 비-스캔 넷과 단락 | `L3` | 드라이버가 없는 넷에 붙어도 전기값이 안 바뀌면 응답이 그대로다 |
| 강한 드라이버 단락 | `L4` | wired-AND/OR 어느 쪽도 아니다. 카운팅은 "이상함"만 알고, 2단계가 방향까지 찾는다 |
| 2핀 넷 open 위치 | `A7` | 관측점이 하나뿐 |
| 출력 전용 드라이버의 stuck vs open | `A7` | read-back 이 없다 |

스캔이 아예 못 하는 것도 분명히 말할 수 있어야 한다: **정적(static) 시험**이다. TCK 속도로 값을 놓고 읽으므로 타이밍 마진, 신호 무결성, 크로스토크, 임피던스는 보지 못한다. 끊어진 배선은 찾지만 "간신히 붙어 있는 납땜"은 지나칠 수 있다. AC 결합·차동 고속 링크(SerDes)는 DC 패턴이 통과하지 못하므로 IEEE 1149.6(AC EXTEST) 같은 별도 방법이 필요하고, DRAM 등 스캔 없는 부품은 그 부품의 프로토콜로 넷을 시험하는 클러스터 테스트로 푼다. 이 범위는 SIV-M04(PCIe)나 HWLAB 계측 실습과 나뉜다.

**단락 물리에 대해.** "wired-AND/OR" 은 편의상 모델이다. CMOS 두 출력이 싸우면 실제 전압은 드라이브 세기로 정해지는 중간값일 수 있고, 수신 쪽은 그 전압을 입력 임계값으로 해석한다(같은 넷의 수신 두 개가 다르게 읽을 수도 있다). 그래서 2단계는 AND/OR/dom 을 **가정하지 않고** walk-1, walk-0 응답으로 판정한다. 단락 동안 드라이버끼리 싸우므로 시험 시간은 짧아야 한다(연습 2: `L + 4` TCK 마다 값이 바뀐다).

### Part C — 2단계 진단과 리포트

```svg
<svg viewBox="0 0 740 222" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="siva02-b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <rect x="15" y="60" width="130" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="80" y="88" fill="currentColor" text-anchor="middle">1단계</text>
  <text x="80" y="108" fill="currentColor" text-anchor="middle" font-size="12">수정 카운팅 k=6</text>
  <line x1="145" y1="95" x2="195" y2="95" stroke="currentColor" marker-end="url(#siva02-b)"/>
  <rect x="200" y="60" width="150" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="275" y="88" fill="currentColor" text-anchor="middle">diagnose</text>
  <text x="275" y="108" fill="currentColor" text-anchor="middle" font-size="12">open/stuck 확정</text>
  <line x1="350" y1="95" x2="400" y2="95" stroke="currentColor" marker-end="url(#siva02-b)"/>
  <rect x="405" y="60" width="160" height="70" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="485" y="88" fill="currentColor" text-anchor="middle">의심 S (단락 + alias)</text>
  <text x="485" y="108" fill="currentColor" text-anchor="middle" font-size="12">2(|S|+1) 벡터</text>
  <line x1="565" y1="95" x2="610" y2="95" stroke="currentColor" marker-end="url(#siva02-b)"/>
  <rect x="615" y="60" width="115" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="672" y="88" fill="currentColor" text-anchor="middle">리포트</text>
  <text x="672" y="108" fill="currentColor" text-anchor="middle" font-size="12">넷·핀·종류</text>
  <text x="15" y="30" fill="currentColor">검출은 log N 벡터로, 진단은 의심 넷에만 walking — 벡터가 단락 넷 수에만 비례한다</text>
  <text x="15" y="166" fill="currentColor" font-size="12">S = 틀린 넷 + 그 틀린 응답과 코드가 같은 (멀쩡해 보이는) 넷.</text>
  <text x="15" y="184" fill="currentColor" font-size="12">단락 그룹이 같은 값을 읽는 한 이 집합이 빠뜨리는 넷은 없다.</text>
  <text x="15" y="202" fill="currentColor" font-size="12">비-S 넷은 배경값(walk-1: 0, walk-0: 1)으로 구동을 유지해 제어 셀 공유 계획을 안 바꾼다.</text>
</svg>
```

**왜 S 가 빠뜨리지 않나.** 단락 그룹의 넷은 모두 같은 값 r 을 읽는다. 그룹의 어떤 넷 x 가 틀리지 않았다면 r = code(x) 라는 뜻이다. 코드는 넷마다 다르므로 그런 x 는 "응답과 코드가 같은 넷" 하나뿐이다. 1단계 finding 의 `maybe` 가 이것이다. Part C 리포트의 `BUF6` 은 다른 단락의 응답과 코드가 우연히 같아 S 에 들어갔다가 2단계에서 `cleared` 로 풀려난다.

```text
=== VAL-R2 board S/N 0042 ===
chain TDI -> U1(SNS_SOC,52b) -> U2(IO_FPGA,63b) -> U3(LVL_BUF,17b) -> TDO  length=132
nets total=40 drive_only=1 observe_only=1 testable=37 unscannable=1 | tested=36 held=2
vectors step1=6 step2=12 suspects=BUF2,BUF3,BUF6,DATA5,DATA6 cleared=BUF6
FAIL net=ADDR3 class=OPEN_RX pin=U2.A(3) exp=001000 got=111111
FAIL net=nCS class=STUCK_OR_OPEN pin=- exp=010001 got=000000
FAIL net=BUF2+BUF3 class=SHORT pin=- type=or
FAIL net=DATA5+DATA6 class=SHORT pin=- type=and
not tested: CLK_25M,TP_VSENSE  held: PMIC_EN=1,RST_N=1
driver fights during test (because of shorts): 12 events
VERDICT FAIL (4 findings)
scan bits: 3168 in 24 DR scans
```

`exp`/`got` 은 벡터 0 이 왼쪽이다.

이 리포트로 디버그 엔지니어가 다음에 할 일이 정해진다.

| 줄 | 해석 | 다음 동작 |
|---|---|---|
| `ADDR3 OPEN_RX U2.A(3) got=111111` | U3.A(3)은 정상이고 U2 만 풀업을 읽음 → U2 쪽 납땜/비아 | U2 볼 X-ray, 또는 해당 볼 패드 측정 |
| `nCS STUCK_OR_OPEN got=000000` | 출력 전용이라 드라이버 쪽을 못 봄 | nCS 넷 멀티미터로 GND 쇼트 확인, 안 되면 U1 패드 재납 |
| `BUF2+BUF3 ... or` | 인접 BUF 넷 단락, wired-OR 쪽 (1이 이김) | 해당 구간 납땜 브리지 확인 |
| `DATA5+DATA6 ... and` | 단락, wired-AND 쪽 | 구역 재검사 (브리지) |
| `driver fights 12` | 단락이 만든 충돌 이벤트 수 | 시험 시간 관리 — 재시험 전에 먼저 고친다 |

**실제 도구 흐름**(장비가 있을 때, 여기서는 돌리지 않았다): BSDL·넷리스트로 ATPG 도구가 패턴을 만들고, 흔히 SVF(`SIR`/`SDR` 문장) 같은 벡터 파일로 내보낸 뒤 프로그래머(XJTAG, JTAG Technologies, Corelis 등 상용, 또는 OpenOCD 의 SVF 재생 같은 오픈소스)가 재생한다. 그 앞에 체인 무결성 테스트(체인 길이·IDCODE, TOOL-A01의 몫)와 전원 레일 확인(SIV-A06)이 온다.

**캠페인 테스트(`C4`).** 넷·핀·종류를 고정 seed 로 뽑은 단일 결함 120개를 돌렸다. open, stuck, dead driver, 2넷 단락(AND/OR)마다 **정답 분류**(`expected_for`)와 대조해 전부 맞았다. 단, 정답 분류 표에 이미 `STUCK_OR_OPEN` 처럼 "이 정보로는 여기까지" 가 들어가 있다 — 측정기를 대기 전에는 절대 갈 수 없는 구간을 맞혔다고 치는 것이 아니라, 그 구간을 **정직하게 표시하는지**를 검사한다.

**벡터 수**: 이 보드는 1단계 6 + 2단계 2×(5+1) = 18 벡터다. `run_extest` 가 세 번 불리고 각각 PRELOAD 1 + 벡터 6 + 플러시 1 = 8 스캔이라 합계 24 스캔, 3168 비트(= 24 × 132)다. walking-1 과 walking-0 을 전체 넷에 돌렸다면 72 벡터였다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```text
cd lessons/SIV/SIV-A02
python3 -I solution.py; echo "exit=$?"
```

실제 출력(마지막 부분):

```text
PASS C3 report: chain header
PASS C3 report: open line names the pin
PASS C3 report: AND short with type
PASS C3 report: excluded nets listed
PASS C3 report: verdict
PASS C4 random single-fault campaign: 120 boards localized
102/102 passed
exit=0
```

```text
python3 -I -W error -X dev solution.py | tail -1 → 102/102 passed (no warnings)
python3 -I gen_board.py 후 shasum -c 로 네 파일 모두 OK (결정적 재생성)
```

테스트가 검사하는 것을 스스로 믿기 위해 정답에 일부러 버그를 넣어 돌려 보기도 했다(변이 시험, 파일은 커밋하지 않음). 벡터 응답을 한 칸 일찍 읽기, 제어 셀 극성 뒤집기, PRELOAD 생략, hold 넷을 안전값에서 빼기, 2단계 배경 벡터 제거, all-1 코드 허용, 수신 부분 판정 제거, 형제 핀 전파 제거, 출력 셀을 수신으로 읽기, alias 제거 — 10개 변이 모두 테스트가 exit 1 로 잡았다.

## 7. 자주 하는 실수

**1. 응답을 같은 스캔에서 읽는다.** 벡터 k 를 넣은 스캔의 TDO 는 이전 값이다.
```python
# 틀림
for k in range(n):
    out = board.shift_dr(vec(k))
    resp[k] = out
# 맞음: k > 0 일 때 out 이 벡터 k-1 의 응답, 마지막에 플러시 한 번 더
```

**2. 출력 전용 핀을 수신으로 읽는다.** `output3` 셀의 캡처는 코어 쪽 값이지 핀이 아니다. 비교하면 항상 틀리거나, 우연히 맞으면 더 나쁘다. `in_cell` 만 읽는다.

**3. 제어 셀 극성을 0=비활성으로 가정.** `disval` 은 부품마다 다르다.
```python
# 틀림: bits[ctrl] = 1
# 맞음: bits[ctrl] = 1 - dev.disval()[ctrl]
```

**4. PRELOAD 없이 EXTEST.** update 래치 쓰레기 값이 핀으로 나간다. 시험 순서는 항상 PRELOAD(안전값) → EXTEST.

**5. hold 넷을 그냥 tri-state.** 코어가 EXTEST 로 놓는 순간 RST_N/PMIC_EN 을 구동할 사람이 없다. hold 는 PRELOAD 이미지부터 켠다.

**6. 2단계에 배경 벡터가 없다.** |S|=2 wired-AND 는 walk-1, walk-0 이 모두 0 이라 "둘 다 stuck" 과 같아진다. `ones` 를 `n+1` 비트로.

**7. 공유 제어 셀을 잊고 드라이버를 고른다.** 형제 핀까지 전파하지 않으면 시험 도중 다른 넷을 구동해 충돌한다.

**8. 진단 표를 "어느 쪽이 끊겼나"까지 약속한다.** 2핀 넷은 어느 끝인지 모르고 출력 전용 드라이버는 read-back 이 없다. 리포트에 `STUCK_OR_OPEN` 처럼 정직하게 쓰고 다음 측정을 적는다.

## 8. 면접 Q&A

<details><summary>Q1. Walking-1 과 카운팅 패턴은 어떻게 고르나? 넷이 많으면?</summary>

Walking 은 N 벡터, 카운팅은 ⌈log2(N+2)⌉ 벡터다. 넷 1000개, 체인 5000비트, TCK 10 MHz 이면 벡터당 대략 `5000+4` TCK 라서 walking 은 약 0.5초, 수정 카운팅은 k=10 으로 약 5 ms 다. 시간은 스캔 비트 수에 지배되므로 카운팅으로 검출하고, **의심 넷만** walking 으로 진단하는 2단계가 실전 패턴이다.

**꼬리: 그러면 왜 walking 을 아예 안 쓰나?** 카운팅만으로는 aliasing/confounding 때문에 어떤 넷끼리 단락인지 못 정한다(n=36, pair-AND 정확 진단 30.5%). 진단 시간을 조금 쓰고 벡터 수를 의심 넷 비례로 줄이는 것이 거의 항상 낫다.

**꼬리: 수정 카운팅이 2개 코드를 왜 버리나?** 전부 0 코드는 stuck-at-0/pull-down open 과 같은 응답이고, 전부 1 코드는 stuck-at-1/pull-up open 과 같다. 코드 공간이 빠듯해 비트를 하나 더 써야 해도(N=8 이면 3→4) 이 두 코드는 쓰지 않는다.
</details>

<details><summary>Q2. EXTEST 진입 전에 왜 PRELOAD 를 하나? SAMPLE 과는 뭐가 다른가?</summary>

EXTEST 는 Update-IR 시점에 BSR 값이 핀으로 나가기 시작한다. 그전에 쓰레기로 차 있던 update 래치가 그대로 구동되면 두 칩이 같은 넷을 반대로 구동할 수 있다. PRELOAD 는 코어 동작에 간섭 없이 래치를 채워 안전값으로 맞추는 명령이다.

**꼬리: SAMPLE?** 시스템이 도는 상태에서 핀을 스냅샷으로 읽는 관측 명령. 1149.1-2001 부터 SAMPLE 과 PRELOAD 가 별도 명령으로 정의되고 같은 코드를 공유해도 된다. BSDL 에서 읽는다.
</details>

<details><summary>Q3. read-back 없는 드라이버에서 stuck-at-0 과 open 을 어떻게 구분하나?</summary>

순수 `output3` 은 핀을 못 읽는다. 수신이 모두 같은 상수이고 드라이버 쪽 정보가 없으면 패턴만으로는 구분 불가 — 그래서 `STUCK_OR_OPEN`. 양방향 셀이나 같은 핀의 입력 셀이 있으면 read-back 이 코드와 같을 때 open(패드는 구동 중인데 넷이 안 움직임), 상수면 stuck 이다.

**꼬리: 2핀 넷의 open 은 어느 쪽 끝인가?** 관측점이 수신 하나뿐이라 알 수 없다. 대안은 드라이버 rotation(반대 방향으로도 구동), 또는 측정기.

**꼬리: 왜 넷 풀업이 끊긴 핀에서 안 읽히나?** 풀업 저항은 끊긴 곳 건너편 넷에 달려 있다. 끊긴 핀은 자기 쪽 상태(내부 약한 풀, 키퍼, 플로팅)만 본다.
</details>

<details><summary>Q4. aliasing 과 confounding 을 설명하라.</summary>

aliasing: 단락 넷의 응답이 정상 넷의 코드와 같아져, 그 정상 넷이 단락에 끼었는지 모르는 것. 예: 코드 5&6=4 는 코드 4인 넷과 구별 안 됨. confounding: 서로 다른 단락이 같은 응답으로 합쳐지는 것. 예: walking-1 에서 wired-AND 단락 두 쌍은 모두 0. 표로 보면 n=36 수정 카운팅의 2pairs-AND 는 7.8%만 정확.

**꼬리: 왜 walking-1 은 wired-AND 에 약하나?** 원-핫 코드끼리 AND 는 항상 0 이라 정보가 사라진다. wired-OR 은 비트가 살아남아 그룹이 그대로 읽힌다. 단락 물리를 모르면 쌍대인 walking-0 도 돌린다.

**꼬리: 진단 시간을 더 쓰지 않고 aliasing 을 줄이려면?** 연습 1의 등중 코드(constant-weight)는 2넷 단락에서 제3 코드 alias 가 없다. 단 벡터 수가 늘고 다중 단락은 여전히 confound 된다.
</details>

<details><summary>Q5. 드라이버 충돌을 어떻게 막나? 그래도 단락이 있으면?</summary>

넷당 드라이버 하나를 계획으로 보장하고, 나머지 출력은 제어 셀을 disable value 로 둔다. 제어 셀을 공유하는 핀은 같이 켜지므로 계획 단계에서 형제까지 검사하고, 끌 수 없는 `output2` 가 넷에 둘이면 그 넷은 빼고 시험하지 않는다.

**꼬리: 단락 자체는 어쩌나?** 단락 넷에서는 어차피 드라이버끼리 싸운다. 값이 유지되는 시간은 벡터당 `(L+4)/f_TCK` 다(연습 2). 긴 체인이나 느린 TCK 는 싸우는 시간을 늘리므로 벡터 수를 줄이고, 표준에 수치가 박혀 있는 건 아니지만 부품 드라이브 전류와 패키지 열 특성으로 팀이 기준을 정한다.

**꼬리: 드라이버가 둘인 넷은 각각 다 시험해야 하나?** 하나만 켜면 다른 쪽 출력단의 결함을 못 본다(`B8`). 드라이버 rotation 으로 벡터가 드라이버 수만큼 늘어난다.
</details>

<details><summary>Q6. 전원 시퀀스 핀(리셋, PMIC 인에이블)은 어떻게 다루나?</summary>

패턴에서 빼서 hold 넷으로 두고 고정값으로 묶는다. BSDL 의 `COMPLIANCE_PATTERNS` 처럼 스캔을 켜기 위해 특정 핀이 어떤 값이어야 하는 경우도 있다. 그리고 hold 값은 EXTEST 들어가기 전, PRELOAD 이미지부터 구동해야 한다. EXTEST 가 되는 순간 코어가 핀을 놓기 때문이다.

**꼬리: 어떻게 hold 목록을 만드나?** 회로도 검토로 리셋 트리, 전원 enable, 쓰기 보호, 부팅 스트랩을 표시한다. 자동 판단은 위험하다. 이 항목을 놓치면 시험이 보드 전원을 끈다.

**꼬리: hold 넷의 연결은 시험 못 하나?** 값이 안 바뀌니 대부분 안 된다. SAMPLE 로 관측만 하거나, 부팅 후 펌웨어로 따로 본다. 리포트에 "not tested"로 남긴다.
</details>

<details><summary>Q7. 바운더리 스캔이 못 잡는 것은? 왜 그걸 쓰나?</summary>

정적 시험이라 at-speed 타이밍, 신호 무결성, 크로스토크, 임피던스는 못 본다. AC 결합 차동 링크는 1149.6, 아날로그는 1149.4 등 별도 표준이 있다. 스캔 없는 메모리는 SoC 가 메모리 프로토콜로 패턴을 쓰고 읽는 클러스터 테스트로 푼다. 값이 맞는데 마진이 없는 불량은 통과한다.

**꼬리: 그럼 ICT 나 플라잉 프로브는?** 물리 접근이 있으면 저항·캐패시터 값까지 재지만 BGA 볼 아래 넷은 접근이 안 된다. 스캔은 그 접근 불가 영역을 커버한다. 상호 보완이다.

**꼬리: 하드웨어가 준비되기 전에 이 도구를 믿을 근거는?** 우리는 결함을 심은 모델로 검출·위치를 검증했고, 한계 테스트로 못 보는 것도 측정했다. 그래도 첫 보드에서는 표준 샘플(골든 보드)로 false-positive 율을 먼저 잰다.
</details>

<details><summary>Q8. 2단계 방식의 위험은? 1단계에서 못 잡으면?</summary>

단락 넷은 모두 같은 값을 읽는다는 가정이 깨지면 S 가 불완전할 수 있다. 전기적으로 중간값이면 두 수신이 다르게 읽어 `UNRESOLVED` 가 된다. 그 경우 전체 walking 으로 fallback 하는 것이 맞다. 또 2단계에서 비-S 넷은 배경값 구동을 유지하므로(공유 제어 셀 계획을 안 바꾼다) 2단계의 전기 조건이 1단계와 달라 간헐 불량이 사라질 수 있다.

**꼬리: 왜 2단계에 전부 0/1 배경 벡터가 필요한가?** |S|=2 wired-AND 는 walking-1, walking-0 이 다 0 이라 stuck 과 같다. 전부 1을 내면 둘이 동시에 1일 때 읽히는 값으로 구분된다.

**꼬리: 시간 예산이 있으면?** 1단계는 수정 카운팅, 그다음 단락 후보만. 생산에서는 양품 시간이 중요하므로 양품은 1단계에서 끝나고, 불량 진단에만 시간을 쓴다.
</details>

## 9. 추가 연습

### 연습 1 — 등중 코드(constant-weight)로 aliasing 없애기
무게가 같은 코드만 쓰면 두 코드의 AND 는 더 가벼워지고 OR 은 더 무거워져 제3의 코드가 될 수 없다. `gen_constant_weight(n)` 을 만들고, 넷 14개에서 제3 넷 alias 가 0 인 것을 확인하라. 대신 벡터가 몇 개 필요한가?

<details><summary>답</summary>
C(k, k//2) ≥ n 인 가장 작은 k 를 고른다. n=14 면 k=6 (C(6,3)=20). 벡터 6 개로 수정 카운팅의 4 개보다 많고 walking 14 개보다 적다. 2넷 단락의 제3 코드 alias 는 없지만, 응답이 전부 0 이면(보수인 두 코드끼리) 여전히 stuck 과 모호하고 다중 단락의 confounding 도 남는다.
</details>

### 연습 2 — TCK 시간과 구동 충돌 지속시간
체인 132비트, TCK 10 MHz 일 때 벡터 하나가 유지되는 시간과, 벡터당 50 µs 를 넘지 않는 최대 체인 길이를 구하라.

<details><summary>답</summary>
Update-DR 에서 다음 Update-DR 까지 `L + 4` TCK (Select-DR → Capture-DR → Shift-DR 전이 3, 시프트 L 클록 중 마지막이 Exit1 으로, Update 1). 132비트 → 136 TCK = 13.6 µs. 50 µs × 10 MHz = 500 TCK → 최대 L = 496비트. TAP 전이는 TOOL-A01 참고.
</details>

### 연습 3 — BYPASS 로 체인 줄이기
시험 대상이 아닌 장치를 BYPASS 로 두면 DR 체인은 어떻게 변하나? U3 만 시험한다면 벡터당 비트 수는?

<details><summary>답</summary>
BYPASS 된 장치는 DR 에서 1비트. U3 만 시험하면 52, 63 비트 대신 1+1 이 되어 17+2 = 19비트. 체인이 짧아져 값 유지 시간과 싸우는 시간이 줄어든다.
</details>

### 연습 4 — confounding 쌍 세기
서로소인 두 쌍 {a,b}, {c,d} 의 응답이 같은 경우를 세는 `confounded_pairs` 를 작성하라. walking-1 에서 AND, OR 은 어떻게 나오나?

<details><summary>답</summary>
walking-1 + wired-AND 는 모든 쌍이 0 이므로 전부 confound, wired-OR 은 비트가 살아남아 0 건이다. 수정 카운팅은 AND/OR 모두 일부 경우에 confound 된다. 코드 수가 코드 공간을 채울수록 늘어난다.
</details>

### 연습 5 — 커버리지 요약
넷 분류 딕셔너리로부터 시험 비율(%)과 못 잰 넷 목록을 내라. 리포트에 왜 못 잰 넷을 반드시 나열해야 하나?

<details><summary>답</summary>
`testable` 비율과 나머지 이름 목록. "시험 통과"는 "연결이 확인됐다"가 아니라 "확인한 것 중 문제 없다"이므로, 무엇을 안 봤는지 적지 않으면 디버그가 엉뚱한 곳을 믿게 된다.
</details>

### 한 번에 검증

<!-- code: practice.py -->

```text
cd lessons/SIV/SIV-A02
python3 -I practice.py; echo "exit=$?"
```

실제 출력:

```text
PASS E1 constant-weight n=14 needs k=6
PASS E1 no three-net aliasing on AND
PASS E1 no three-net aliasing on OR
PASS E1 modified counting does alias
PASS E1 cost: 6 vectors vs 4 (modified counting) vs 14 (walking)
PASS E2 ticks per vector
PASS E2 13.6 us per vector at 10 MHz (L=132)
PASS E2 136 us per vector at 1 MHz
PASS E2 chain budget for 50 us at 10 MHz
PASS E3 all active
PASS E3 U1-U2 only: U3 in BYPASS
PASS E3 U3 only
PASS E4 walking-1 + AND: every disjoint pair-of-pairs confounds (all read 0)
PASS E4 walking-1 + OR: none confound
PASS E4 modified counting n=6 confounds some under AND and OR
PASS E5 coverage percent
PASS E5 untested list for the report
17/17 passed
exit=0
```

## 10. 핵심 정리

- EXTEST = BSR 이 핀을 구동·캡처, 칩이 아니라 **칩 사이 배선**을 시험한다. BSDL `cell 0` 은 TDO 쪽, 응답은 한 스캔 늦다.
- 순서: **PRELOAD 안전값 → EXTEST → 벡터 N + 플러시 1**. hold 넷은 PRELOAD 이미지부터 구동.
- `disable value` 는 부품마다 다르다. 드라이버는 넷당 하나, 제어 셀 공유는 형제까지 전파, `output2` 는 항상 구동.
- 수정 카운팅 k = ⌈log2(N+2)⌉ 는 all-0/all-1 코드를 버려 stuck/open 을 안전하게 잡지만, **진단력은 약하다**(aliasing/confounding).
- Walking-1 은 wired-OR, walking-0 은 wired-AND 진단. 단락 물리를 모르면 둘 다. 2단계는 카운팅으로 검출하고 의심 넷만 walking + 배경 벡터.
- 2핀 넷 open 은 끝을 모르고, 출력 전용 드라이버는 stuck vs open 을 모른다. 리포트에 정직하게 쓰고 다음 측정을 적는다.
- 정적 시험이므로 at-speed, SI, AC 결합, 비-스캔 넷은 범위 밖.

## 참고자료

- IEEE Std 1149.1 (Standard Test Access Port and Boundary-Scan Architecture, 2001/2013 판) — BSDL 은 Annex B. 셀 동작은 `STD_1149_1_2001` 패키지.
- IEEE Std 1149.6 (AC-coupled/differential EXTEST), IEEE Std 1149.4 (mixed-signal).
- Kenneth P. Parker, *The Boundary-Scan Handbook*.
- W. H. Kautz, "Testing for faults in wiring networks," IEEE Trans. Computers, 1974 (카운팅).
- N. Jarwala, C. W. Yau, "A new framework for analyzing test generation and diagnosis algorithms for wiring interconnects," ITC 1989 (aliasing/confounding, walking 계열).
- P. Goel, M. McMahon, "Electronic chip-in-place test," 1982 (수정 카운팅) · P. T. Wagner, "Interconnect testing with boundary scan," ITC 1987 (참/보수).
- 관련 레슨: SIV-B05(walking 1s/0s, bus-hold), SIV-M04(PCIe), SIV-A06(전원 시퀀싱), SIV-A10(BIST), HWLAB-B01(넷리스트 감사), TOOL-A01(TAP 상태 기계).
