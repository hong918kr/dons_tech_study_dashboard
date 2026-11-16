---
id: MEM-B02
time: 60분
updated: 2026-11-15
---
# MEM-B02 DRAM 명령과 타이밍: ACT/RD/PRE, tRCD·tRP·tRAS

## 학습 목표

- DRAM 뱅크(bank) 하나를 **IDLE / ACTIVE(row)** 두 상태의 상태머신(state machine)으로 보고, ACT · RD · WR · PRE 가 각각 어느 상태에서만 합법인지 말할 수 있다.
- tRCD · CL · tRAS · tRP · tRC · tWR · tRTP 가 **어느 명령에서 어느 명령까지**의 최소 간격인지, 그리고 왜 존재하는지(물리적 이유)를 설명한다.
- 데이터시트의 ns 값을 **정수 피코초(ps)** 로 사이클로 바꿔 DDR4-3200AA 표(22/22/22/52/74/24/12)를 정확히 재현하고, `double` 의 `ceil` 이 왜 위험한지 실제 예로 보인다.
- 사이클 단위 명령 트레이스를 받아 위반을 **전부**(규칙 이름, 필요/실제 간격, 기준 명령) 보고하는 체커를 짠다.
- 위반 트레이스를 순서를 지키며 "가장 이른 합법 사이클"로 미는 함수를 짜고, 그것이 최소임을 증명·검사한다.

## 1. 문제

DDR bring-up, 메모리 컨트롤러 검증, 실리콘 밸리데이션 면접에서 "row conflict 지연이 몇 ns 인가, tRAS 는 왜 있나"는 단골 질문이고, 보드에서 타이밍 레지스터 하나를 잘못 넣으면 부팅은 되는데 부하에서만 비트가 깨지는 최악의 버그가 된다.

DRAM 칩 안의 **뱅크**는 커다란 2차원 셀 배열이다. 읽기·쓰기는 항상 세 단계로 일어난다.

1. **ACT (activate)** — 행(row) 하나를 열어, 그 행 전체(예: x8 8 Gb 부품은 1 K 열 × 8 비트 = 1 KB)를 뱅크의 **행 버퍼(row buffer, = 센스 앰프 sense amplifier 들)** 로 끌어올린다. 셀의 전하는 아주 작아서, 센스 앰프가 증폭을 끝낼 때까지 열(column) 접근을 못 한다 → **tRCD** (ACT → RD/WR).
2. **RD / WR (column 명령)** — 열린 행 버퍼에서 8 바이트 × BL8 = 64 B 를 읽거나 쓴다. 읽기 데이터는 RD 뒤 **CL** (CAS latency) 사이클에 나오기 시작해 4 tCK 동안 나온다. 쓰기 데이터는 WR 뒤 **CWL** 사이클에 들어간다.
3. **PRE (precharge)** — 행을 닫고 비트라인(bitline)을 다시 중간 전압으로 맞춘다. 읽기는 셀 전하를 **파괴**하므로 센스 앰프가 행 전체를 다시 써 넣을(restore) 때까지 닫으면 안 된다 → **tRAS** (ACT → PRE). 쓴 데이터가 셀에 들어갈 시간 → **tWR**, 읽기 프리페치가 끝날 시간 → **tRTP**. 닫은 뒤 다음 ACT 까지 → **tRP**. 같은 뱅크의 ACT → ACT 는 **tRC**.

Don 의 SSD 펌웨어 경험에 빗대면, NAND 의 "page read → 페이지 버퍼(page register) → 데이터 전송" 과 거의 같은 그림이다. 행 버퍼 = 페이지 버퍼, ACT = tR, 다만 DRAM 은 이것을 수십 ns 단위로 하고, 열린 행을 닫을 때 **다시 써 넣어야** 한다는 점이 다르다.

**이 레슨의 정의 (JESD79-4 / 데이터시트 대표값 — 실제 부품은 데이터시트 확인)** — DDR4-3200AA (22-22-22), tCK = 0.625 ns (클럭 1600 MHz, 3200 MT/s), AL = 0, BL8 고정. MEM-B03 · MEM-B05 도 같은 표를 쓴다.

| 파라미터 | 기준 → 대상 | 데이터시트 | 사이클 = ceil(ps / 625) |
|---|---|---|---|
| CL (= RL, AL = 0) | RD → 첫 읽기 데이터 | 22 nCK (13.75 ns) | 22 |
| CWL (= WL) | WR → 첫 쓰기 데이터 | 16 nCK | 16 |
| BL8 데이터 | 버스 점유 | 8 beat = 4 tCK | 4 |
| tRCD | ACT → RD/WR | 13.75 ns | 22 |
| tRP | PRE → ACT | 13.75 ns | 22 |
| tRAS (min) | ACT → PRE | 32 ns | 51.2 → **52** |
| tRC | ACT → ACT (같은 뱅크) | 45.75 ns = tRAS + tRP | 73.2 → **74** |
| tWR | 쓰기 데이터 끝 → PRE | 15 ns | 24 (WR 명령 기준 16 + 4 + 24 = **44**) |
| tRTP | RD → PRE | max(4 nCK, 7.5 ns) | 12 |

지연 어휘 (마지막 data beat 까지, MEM-B03 · MEM-B05 와 같다): row hit = CL + 4 = 26 cyc (16.25 ns), row empty = tRCD + CL + 4 = 48 cyc (30 ns), row conflict = tRP + tRCD + CL + 4 = 70 cyc (43.75 ns) **+ 이전 ACT 의 tRAS 가 아직 안 끝났으면 그 대기**. 이 레슨의 체커와 Part C 는 tRAS 대기를 **정확히** 모델링한다 (연습 2 에서 92 cyc 사례).

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b02a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">단일 뱅크 상태머신 — 각 화살표에 "이 명령을 내려면 지켜야 할 간격"</text>
  <rect x="70" y="100" width="180" height="56" rx="28" class="f-mut" stroke="currentColor"/>
  <text x="160" y="124" fill="currentColor" text-anchor="middle" font-weight="bold">IDLE</text>
  <text x="160" y="143" fill="currentColor" text-anchor="middle" font-size="12">precharged, 열린 행 없음</text>
  <rect x="470" y="100" width="200" height="56" rx="28" class="f-acc" stroke="currentColor"/>
  <text x="570" y="124" fill="currentColor" text-anchor="middle" font-weight="bold">ACTIVE (row r)</text>
  <text x="570" y="143" fill="currentColor" text-anchor="middle" font-size="12">행 r 이 행 버퍼에 열림</text>
  <path d="M250 112 C 320 66, 400 66, 470 112" fill="none" stroke="currentColor" marker-end="url(#b02a)"/>
  <text x="360" y="62" fill="currentColor" text-anchor="middle">ACT r : tRP ≥ 22 (PRE 부터), tRC ≥ 74 (ACT 부터)</text>
  <path d="M470 146 C 400 194, 320 194, 250 146" fill="none" stroke="currentColor" marker-end="url(#b02a)"/>
  <text x="360" y="200" fill="currentColor" text-anchor="middle">PRE : tRAS ≥ 52 (ACT), tRTP ≥ 12 (RD), 44 (WR)</text>
  <path d="M620 100 C 650 52, 700 60, 662 108" fill="none" stroke="currentColor" marker-end="url(#b02a)"/>
  <text x="640" y="42" fill="currentColor" text-anchor="middle">RD / WR : tRCD ≥ 22</text>
  <path d="M120 100 C 90 50, 150 46, 150 98" fill="none" stroke="currentColor" marker-end="url(#b02a)"/>
  <text x="125" y="42" fill="currentColor" text-anchor="middle">PRE = NOP</text>
  <rect x="40" y="222" width="300" height="24" class="f-warn" stroke="currentColor"/>
  <text x="190" y="239" fill="currentColor" text-anchor="middle">RD / WR on IDLE → STATE 위반</text>
  <rect x="400" y="222" width="300" height="24" class="f-warn" stroke="currentColor"/>
  <text x="550" y="239" fill="currentColor" text-anchor="middle">ACT on ACTIVE → STATE 위반</text>
  <text x="10" y="264" fill="currentColor" font-size="12">숫자 = DDR4-3200AA 사이클. 44 = CWL 16 + 데이터 4 + tWR 24. RD 마다 CL 22 뒤 데이터(위반 아님, 보고만).</text>
</svg>
```

### Part A — ns(ps) ↔ cycle 변환

- `uint32_t ps_to_cyc(uint32_t ps, uint32_t tck_ps)` — ceil(ps / tCK) 를 **정수로만**. tCK = 0 이면 `CYC_INVALID`, `ps + tck − 1` 이 32 비트를 넘지 않게.
- `int timing_from_spec(const ddr_spec_ps *s, ddr_timing *t)` — 데이터시트 표(ps 단위) → 사이클 표. tRTP = max(4 nCK, tRTP(ns)). DDR4-3200AA 에서 **22/22/22/52/74/24/12** 가 나와야 한다. `cyc_to_ps` 는 주어진다.
- 검사: 대표값 각각, 경계(1 ps, 625 ps, 626 ps), 4가지 tCK × ps 0…200 000 전수로 "c·tCK ≥ ps > (c−1)·tCK" 정의 확인.

### Part B — 상태머신 + 타이밍 체커

- `bank_state_ok`, `bank_apply` — IDLE/ACTIVE 상태 규칙과 전이, 그리고 마지막 ACT/PRE/RD/WR 의 사이클·트레이스 인덱스를 기록.
- `timing_checks` — 명령 하나에 걸리는 규칙을 **전부** 검사해 `{명령 idx, 규칙, 기준 명령 idx, need, got}` 를 보고 (한 명령이 여러 규칙을 동시에 어길 수 있다).
- 주어진 드라이버 `check_trace` 가 순서(사이클당 명령 1개) → 상태 → 타이밍 순으로 부르고, RD 마다 데이터 반환 구간 `[RD + CL, RD + CL + 4)` 를 기록한다. 타이밍 위반 명령은 "발행된 것"으로 보고 상태에 반영하고, 상태/순서 위반 명령은 무시한다.
- 검사: 깨끗한 트레이스 0건, 규칙마다 위반 하나씩 (정확한 규칙·need·got·기준), 경계 전수 (gap = need 는 OK, need − 1 은 위반), 한 트레이스에 6건 동시, 보고 버퍼가 작아도 총수는 정확.

### Part C — 트레이스 고치기 (earliest legal issue)

- `earliest_issue(t, bank, cmd)` — 지금 상태에서 그 명령을 낼 수 있는 가장 이른 사이클 = 규칙마다 "기준 시각 + need" 의 최댓값.
- `fix_trace(...)` — 순서는 그대로, 각 명령을 max(원래 사이클, 이전 명령 + 1, earliest) 로 민다. 상태 위반은 지연으로 고칠 수 없으므로 −2 와 위치를 돌려준다.
- 검사: 고정 예제의 정확한 결과 사이클, 고친 트레이스가 체커를 통과, 2000개 랜덤 트레이스(고정 시드)에서 "합법 + 원래보다 빠르지 않음 + **최소**(밀린 명령을 1 사이클 당기면 반드시 위반)".

## 2. Problem (English)

You are writing a protocol checker for a DDR4 memory-controller verification environment (or for a post-silicon trace captured by a DDR protocol analyzer). Model **one bank**, DDR4-3200AA timing, AL = 0, BL8.

```c
typedef struct { uint32_t tck_ps, cl, cwl, trcd_ps, trp_ps, tras_ps, trc_ps, twr_ps, trtp_ps; } ddr_spec_ps;
typedef struct { uint32_t cl, cwl, bl2, trcd, trp, tras, trc, twr, trtp; } ddr_timing;   /* cycles */
typedef enum { CMD_ACT, CMD_RD, CMD_WR, CMD_PRE } cmd_kind;
typedef struct { uint32_t cyc; cmd_kind cmd; uint32_t row; } cmd_t;
typedef enum { R_TRCD, R_TRAS, R_TRP, R_TRC, R_TWR, R_TRTP, R_STATE, R_ORDER } rule_t;
typedef struct { uint32_t idx; rule_t rule; int32_t ref; uint32_t need, got; } viol_t;

uint32_t ps_to_cyc(uint32_t ps, uint32_t tck_ps);                  /* ceil, integer only        */
int      timing_from_spec(const ddr_spec_ps *s, ddr_timing *t);    /* -1 on bad input           */
uint32_t check_trace(const ddr_timing *t, const cmd_t *tr, uint32_t n, check_report *rep);
int      fix_trace(const ddr_timing *t, const cmd_t *in, uint32_t n, cmd_t *out, uint32_t cap,
                   uint32_t *err_idx, uint64_t *total_delay);      /* 0 ok, -1 args, -2 state   */
```

Rules: ACT needs IDLE, RD/WR need ACTIVE, PRE on IDLE is a NOP. ACT: tRP since the last PRE that closed a row, tRC since the last ACT. RD/WR: tRCD since ACT. PRE: tRAS since ACT, tRTP since the last RD, and CWL + 4 + tWR since the last WR (tWR counts from the end of write data). Report **every** violation; CL is not a rule — report each read's data window. No heap; caller-provided buffers with capacities. `fix_trace` keeps command order and must produce the **earliest** legal schedule.

Examples (from the reference solution):

```text
timing_from_spec(DDR4-3200AA) -> tRCD 22  tRP 22  tRAS 52  tRC 74  tWR 24  tRTP 12   (CL 22, CWL 16)
trace: ACT@0 r1, RD@10, PRE@20, ACT@30 r2, RD@31
  VIOL #1 RD@10: tRCD  need 22 got 10 (ref #0 ACT@0)
  VIOL #2 PRE@20: tRAS  need 52 got 20 (ref #0 ACT@0)
  VIOL #2 PRE@20: tRTP  need 12 got 10 (ref #1 RD@10)
  VIOL #3 ACT@30: tRP   need 22 got 10 (ref #2 PRE@20)
  VIOL #3 ACT@30: tRC   need 74 got 30 (ref #0 ACT@0)
  VIOL #4 RD@31: tRCD  need 22 got 1 (ref #3 ACT@30)
fix_trace -> ACT@0 RD@22 PRE@52 ACT@74 RD@96   (total delay 153 cycles)
```

Follow-ups: Why does tRAS exist? What dominates a row-conflict read? Why is tWR measured from the end of the write burst? Is tRC redundant here? How would a bootloader program these into a controller?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 일곱 군데다: Part A 2개(`ps_to_cyc`, `timing_from_spec`), Part B 3개(`bank_state_ok`, `bank_apply`, `timing_checks`), Part C 2개(`earliest_issue`, `fix_trace`). 위반 저장(`viol_push`), 간격 검사 도우미(`need_gap`), 드라이버 `check_trace`, 출력 함수와 하네스는 주어진다. 스텁은 안전한 틀린 값을 돌려준다 (모든 상태 허용, 위반 없음, `fix_trace` 는 −1). 처음 실행 결과 (실제 출력, 중간 생략, exit 1):

```text
FAIL starter.c:193  ps_to_cyc(13750, 625) == 22
FAIL starter.c:194  ps_to_cyc(32000, 625) == 52
FAIL starter.c:195  ps_to_cyc(45750, 625) == 74
…
clean trace: 0 violations
  DATA #1 RD row 0: beats [22, 22)
…
FAIL starter.c:300  run(&t, multi, 5, &rep) == 6
…
random fix: 2000 traces, fail=2000, violations-after-fix=0, earlier-than-asked=0, delayed cmds=0, non-minimal=0
FAIL starter.c:378  fail == 0 && after_bad == 0 && early == 0 && nonmin == 0 && delayed > 1000
8/60 passed
```

(통과한 8개는 주어진 코드(`cyc_to_ps`, 순서 검사)이거나 "위반 0건" 기대가 스텁과 우연히 맞은 경우다.)

## 4. 힌트

<details><summary>힌트 1 — 올림 나눗셈은 정수로</summary>

ceil(a / b) = (a + b − 1) / b (a, b 양의 정수). a 가 `UINT32_MAX` 근처면 덧셈이 넘치므로 64비트로 계산한다. 데이터시트의 13.75 ns 를 13750 ps 로 들고 다니면 소수가 아예 없다.

</details>

<details><summary>힌트 2 — 상태 규칙은 세 줄</summary>

ACT 는 IDLE 에서만, RD/WR 은 ACTIVE 에서만, PRE 는 언제나 (IDLE 이면 아무 일도 안 하는 NOP — 기준 시각도 바꾸지 않는다).

</details>

<details><summary>힌트 3 — 무엇을 기억해야 하나</summary>

마지막 ACT, 마지막 (행을 닫은) PRE, 현재 행 사이클 안의 마지막 RD, 마지막 WR 의 사이클과 인덱스. "아직 없음" 은 `int64_t` 의 아주 작은 값(`NEVER`)과 인덱스 −1 로 둔다. 0 으로 두면 "사이클 0 에 PRE 가 있었다" 가 되어 첫 ACT 가 tRP 위반으로 잡힌다.

</details>

<details><summary>힌트 4 — tWR 의 기준점</summary>

tWR 은 **마지막 쓰기 데이터가 들어온 뒤**부터다. 쓰기 데이터는 WR + CWL 에 시작해 4 tCK 동안 들어오므로, WR 명령 기준으로는 CWL + 4 + tWR = 44 사이클.

</details>

<details><summary>힌트 5 — 가장 이른 합법 사이클</summary>

`timing_checks` 의 규칙 하나하나는 "기준 시각 + need ≤ 지금" 꼴이다. 그러니 가장 이른 사이클은 그 좌변들의 최댓값. `fix_trace` 는 이것과 원래 사이클, 이전 명령 + 1 중 최댓값을 고르면 된다. 왜 이 탐욕(greedy)이 최소인지는 5절 Part C.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "뱅크 하나는 IDLE/ACTIVE 상태머신이고, 모든 타이밍은 **두 명령 사이의 최소 간격**이다" — 표를 먼저 그린다 (기준 명령 → 대상 명령 → 간격).
2. ns 는 정수 ps 로, 사이클은 올림. 3200AA 는 22-22-22, tRAS 52, tRC 74.
3. 체커 = 상태 + 마지막 ACT/PRE/RD/WR 시각. 명령마다 해당 규칙을 전부 검사, 위반이어도 발행된 것으로 보고 상태 갱신.
4. 고치기 = 각 명령의 earliest = max(기준 + need). 탐욕이 최소인 이유: 단조성.
5. 숫자로 마무리: hit 16.25 ns, empty 30 ns, conflict 43.75 ns, 한 뱅크 랜덤 행은 tRC 마다 64 B → 1.38 GB/s.

### Part A — ns → cycle 을 정수 ps 로

규칙: **nCK = ceil(t / tCK)**. 컨트롤러는 "최소 t ns 를 기다려야 한다" 를 사이클 수로 맞춰야 하므로 내림이 아니라 올림이다 (내림이면 스펙보다 짧게 기다린다).

| 값 | ps | ps / 625 | nCK |
|---|---|---|---|
| tRCD, tRP | 13 750 | 22.0 | 22 |
| tRAS | 32 000 | 51.2 | 52 |
| tRC | 45 750 | 73.2 | 74 |
| tWR | 15 000 | 24.0 | 24 |
| tRTP | 7 500 | 12.0 | max(4, 12) = 12 |

DDR4-3200 의 tCK 0.625 ns = 625 ps 는 정확한 정수라 이 표는 오차가 없다. 그런데 `ceil(13.75 / 0.625)` 를 `double` 로 해도 같지 않나? 이 두 값은 이진수로 정확히 표현되어(13.75 = 1101.11₂, 0.625 = 0.101₂) 운 좋게 22.0 이 나올 뿐이다. 0.938 ns(DDR4-2133 근사) 같은 tCK 에서는 `14.07 / 0.938 = 15.000000000000002` → `ceil` = 16 이 된다 (연습 5, 실측). 한 사이클을 더 기다리는 쪽이라 "안전" 해 보이지만, 같은 코드가 다른 값에서 반대로(아래로) 틀릴 수도 있고, 무엇보다 SPD/데이터시트와 레지스터 값이 1 차이 나면 bring-up 때 비교·검증이 안 된다.

**tCK 자체가 반올림된 값**이라는 함정도 있다. DDR4-2400 의 tCK 는 정확히 2/2400 µs = 833.33… ps 인데 데이터시트는 0.833 ns 로 적는다. 833 ps 로 나누면 tWR 15 ns 가 18.007 → **19**, 정확한 값은 15000 × 2400 / 2 000 000 = **18** 이다 (연습 1, 실측). JEDEC 이 SPD 해석에 작은 보정(guard band) 을 넣은 반올림 알고리즘을 따로 정해 둔 이유가 이것이다 — 정확한 보정 상수는 JESD21-C (SPD) 부록 확인.

세대가 올라가도 ns 는 거의 그대로다: DDR3-1600K (11-11-11) 의 CL 11 × 1.25 ns = 13.75 ns, DDR4-3200AA 의 CL 22 × 0.625 ns = 13.75 ns. **사이클 수가 두 배가 된 것은 인터페이스 클럭이 두 배가 되었기 때문**이지 배열이 느려진 게 아니다. 셀 배열의 물리(비트라인 RC, 센스 앰프 증폭 시간)는 공정이 바뀌어도 잘 줄지 않는다.

`timing_from_spec` 의 tRTP 는 max(4 nCK, 7.5 ns): 느린 클럭(가상의 tCK 2.5 ns)에서는 7.5 ns = 3 사이클 → 4 로 올라간다. 내부적으로 BL8 읽기의 프리페치(8n) 가 배열에서 빠져나가는 데 최소 4 클럭이 필요하다는 뜻으로 이해하면 된다.

### Part B — 상태머신과 규칙 표

| 명령 | 필요한 상태 | 검사하는 규칙 (기준 → need) | 적용 결과 |
|---|---|---|---|
| ACT r | IDLE | tRP: 마지막 PRE → 22 / tRC: 마지막 ACT → 74 | ACTIVE(r), t_act 갱신, t_rd/t_wr 지움 |
| RD | ACTIVE | tRCD: ACT → 22 | t_rd 갱신, 데이터 [RD+22, RD+26) 보고 |
| WR | ACTIVE | tRCD: ACT → 22 | t_wr 갱신 (데이터 [WR+16, WR+20)) |
| PRE | ACTIVE | tRAS: ACT → 52 / tRTP: RD → 12 / tWR: WR → 16+4+24 = 44 | IDLE, t_pre 갱신 |
| PRE | IDLE | 없음 (NOP) | 변화 없음 |

설계 결정 세 가지와 이유:

- **위반이어도 발행된 것으로 본다.** 체커는 "버스에 실제로 나간 것"을 검사한다. 위반한 PRE 를 무시해 버리면 다음 ACT 가 "ACTIVE 에 ACT" 라는 엉뚱한 상태 위반으로 바뀌어 원인(tRAS)이 가려진다.
- **상태 위반 명령은 무시한다.** IDLE 의 RD 는 어느 행을 읽는지 정의되지 않으므로 상태를 바꿀 근거가 없다. 데이터 반환도 기록하지 않는다 (테스트 `g`: `n_rd == 0`).
- **IDLE 의 PRE 는 NOP.** DDR4 데이터시트들은 열린 행이 없는 뱅크에 대한 PRECHARGE 를 NOP 처럼 취급한다고 적는다 (Micron DDR4 데이터시트 표현 기준 — 부품별 확인 권장). tRP 기준도 갱신하지 않는다 (테스트 `h`).

한 트레이스에 위반 6건이 있는 예를 손으로 따라가 보자 (`multi`, 실제 출력과 같다).

| # | 명령 | 상태(전) | 검사 | 기준 | need | got | 결과 |
|---|---|---|---|---|---|---|---|
| 0 | ACT r1 @0 | IDLE | tRP, tRC | 없음 | — | — | OK → ACTIVE(1) |
| 1 | RD @10 | ACTIVE | tRCD | #0 ACT@0 | 22 | 10 | **tRCD 위반**, 데이터 [32, 36) |
| 2 | PRE @20 | ACTIVE | tRAS / tRTP / tWR | #0 / #1 / 없음 | 52 / 12 | 20 / 10 | **tRAS, tRTP 위반** → IDLE |
| 3 | ACT r2 @30 | IDLE | tRP / tRC | #2 PRE@20 / #0 ACT@0 | 22 / 74 | 10 / 30 | **tRP, tRC 위반** → ACTIVE(2) |
| 4 | RD @31 | ACTIVE | tRCD | #3 ACT@30 | 22 | 1 | **tRCD 위반**, 데이터 [53, 57) |

같은 6건이 체커 출력에 그대로 나온다 (2절 예시). 아래는 이 트레이스를 **합법적으로** 낸 모습 — 행 충돌(row conflict) 한 번의 전체 타임라인을 축척 그대로 그렸다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">합법 타임라인: row 1 읽기 → row 2 로 충돌(conflict) 읽기 (5 px = 1 cycle)</text>
  <line x1="90" y1="44" x2="700" y2="44" stroke="currentColor"/>
  <g stroke="currentColor"><line x1="90" y1="40" x2="90" y2="48"/><line x1="140" y1="40" x2="140" y2="48"/><line x1="190" y1="40" x2="190" y2="48"/><line x1="240" y1="40" x2="240" y2="48"/><line x1="290" y1="40" x2="290" y2="48"/><line x1="340" y1="40" x2="340" y2="48"/><line x1="390" y1="40" x2="390" y2="48"/><line x1="440" y1="40" x2="440" y2="48"/><line x1="490" y1="40" x2="490" y2="48"/><line x1="540" y1="40" x2="540" y2="48"/><line x1="590" y1="40" x2="590" y2="48"/><line x1="640" y1="40" x2="640" y2="48"/><line x1="690" y1="40" x2="690" y2="48"/></g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="90" y="36">0</text><text x="140" y="36">10</text><text x="190" y="36">20</text><text x="240" y="36">30</text><text x="290" y="36">40</text><text x="340" y="36">50</text><text x="390" y="36">60</text><text x="440" y="36">70</text><text x="490" y="36">80</text><text x="540" y="36">90</text><text x="590" y="36">100</text><text x="640" y="36">110</text><text x="690" y="36">120</text></g>
  <text x="10" y="36" fill="currentColor" font-size="12">cycle</text>
  <text x="10" y="74" fill="currentColor">CMD</text>
  <rect x="90" y="58" width="4" height="24" class="f-acc" stroke="currentColor"/><text x="97" y="74" fill="currentColor" font-size="12">ACT r1</text>
  <rect x="200" y="58" width="4" height="24" class="f-acc" stroke="currentColor"/><text x="207" y="74" fill="currentColor" font-size="12">RD</text>
  <rect x="350" y="58" width="4" height="24" class="f-acc" stroke="currentColor"/><text x="357" y="74" fill="currentColor" font-size="12">PRE</text>
  <rect x="460" y="58" width="4" height="24" class="f-acc" stroke="currentColor"/><text x="467" y="74" fill="currentColor" font-size="12">ACT r2</text>
  <rect x="570" y="58" width="4" height="24" class="f-acc" stroke="currentColor"/><text x="577" y="74" fill="currentColor" font-size="12">RD</text>
  <text x="10" y="106" fill="currentColor">DQ</text>
  <rect x="310" y="92" width="20" height="22" class="f-ok" stroke="currentColor"/><text x="320" y="128" fill="currentColor" font-size="12" text-anchor="middle">BL8</text>
  <rect x="680" y="92" width="20" height="22" class="f-ok" stroke="currentColor"/><text x="690" y="128" fill="currentColor" font-size="12" text-anchor="middle">BL8</text>
  <g stroke="currentColor" fill="none">
    <path d="M90 145 L90 155 M90 150 L200 150 M200 145 L200 155"/>
    <path d="M200 145 L200 155 M200 150 L310 150 M310 145 L310 155"/>
    <path d="M350 145 L350 155 M350 150 L460 150 M460 145 L460 155"/>
    <path d="M460 145 L460 155 M460 150 L570 150 M570 145 L570 155"/>
    <path d="M570 145 L570 155 M570 150 L680 150 M680 145 L680 155"/>
    <path d="M90 180 L90 190 M90 185 L350 185 M350 180 L350 190"/>
    <path d="M90 215 L90 225 M90 220 L460 220 M460 215 L460 225"/>
    <path d="M350 250 L350 260 M350 255 L700 255 M700 250 L700 260"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="145" y="143">tRCD 22</text><text x="255" y="143">CL 22</text><text x="405" y="143">tRP 22</text><text x="515" y="143">tRCD 22</text><text x="625" y="143">CL 22</text>
    <text x="220" y="178">tRAS 52 (ACT → PRE 최소)</text>
    <text x="275" y="213">tRC 74 (ACT → ACT 최소) = tRAS + tRP</text>
    <text x="520" y="248">PRE → 마지막 beat = tRP + tRCD + CL + 4 = 70 cyc = 43.75 ns</text>
  </g>
  <text x="10" y="290" fill="currentColor" font-size="12">DDR4-3200AA. PRE@52: tRTP 로는 34 지만 tRAS(52)가 더 늦다. 데이터 [44,48), [118,122).</text>
</svg>
```

그림에서 보이는 것: **PRE 를 결정한 건 tRAS** 다. RD@22 만 보면 tRTP 로 34 에 닫을 수 있지만, 행 복원(restore)이 끝나는 52 까지 못 닫는다. 그래서 "conflict = tRP + tRCD + CL + 4" 공식은 **열린 행이 충분히 오래 열려 있었을 때만** 맞고, 방금 연 행에 충돌하면 tRAS 잔여 시간이 더해진다 (연습 2: 92 cyc).

**쓰기 쪽.** 쓰기는 데이터가 늦게 들어오고(CWL), 그 데이터가 센스 앰프를 거쳐 셀에 들어갈 시간(tWR)이 더 필요하다. 그래서 같은 뱅크에서 "쓰고 나서 다른 행 열기" 는 읽기보다 훨씬 비싸다.

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">쓰기 후 PRE: WR + CWL + 4 + tWR (8 px = 1 cycle)</text>
  <line x1="90" y1="44" x2="650" y2="44" stroke="currentColor"/>
  <g stroke="currentColor"><line x1="90" y1="40" x2="90" y2="48"/><line x1="170" y1="40" x2="170" y2="48"/><line x1="250" y1="40" x2="250" y2="48"/><line x1="330" y1="40" x2="330" y2="48"/><line x1="410" y1="40" x2="410" y2="48"/><line x1="490" y1="40" x2="490" y2="48"/><line x1="570" y1="40" x2="570" y2="48"/><line x1="650" y1="40" x2="650" y2="48"/></g>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="90" y="36">0</text><text x="170" y="36">10</text><text x="250" y="36">20</text><text x="330" y="36">30</text><text x="410" y="36">40</text><text x="490" y="36">50</text><text x="570" y="36">60</text><text x="650" y="36">70</text></g>
  <text x="10" y="36" fill="currentColor" font-size="12">cycle</text>
  <text x="10" y="74" fill="currentColor">CMD</text>
  <rect x="90" y="58" width="6" height="24" class="f-acc" stroke="currentColor"/><text x="100" y="74" fill="currentColor" font-size="12">ACT r1</text>
  <rect x="266" y="58" width="6" height="24" class="f-acc" stroke="currentColor"/><text x="276" y="74" fill="currentColor" font-size="12">WR</text>
  <rect x="618" y="58" width="6" height="24" class="f-acc" stroke="currentColor"/><text x="628" y="74" fill="currentColor" font-size="12">PRE</text>
  <text x="10" y="106" fill="currentColor">DQ</text>
  <rect x="394" y="92" width="32" height="22" class="f-ok" stroke="currentColor"/><text x="410" y="128" fill="currentColor" font-size="12" text-anchor="middle">BL8 쓰기</text>
  <line x1="506" y1="56" x2="506" y2="196" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="510" y="110" fill="currentColor" font-size="12">tRAS 끝(52)</text>
  <g stroke="currentColor" fill="none">
    <path d="M266 145 L266 155 M266 150 L394 150 M394 145 L394 155"/>
    <path d="M394 145 L394 155 M394 150 L426 150 M426 145 L426 155"/>
    <path d="M426 145 L426 155 M426 150 L618 150 M618 145 L618 155"/>
    <path d="M90 180 L90 190 M90 185 L506 185 M506 180 L506 190"/>
    <path d="M266 215 L266 225 M266 220 L618 220 M618 215 L618 225"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="330" y="143">CWL 16</text><text x="410" y="143">4</text><text x="522" y="143">tWR 24</text>
    <text x="298" y="178">tRAS 52 — 이미 만족</text>
    <text x="442" y="213">WR → PRE ≥ CWL + 4 + tWR = 16 + 4 + 24 = 44 → PRE@66</text>
  </g>
  <text x="10" y="250" fill="currentColor" font-size="12">PRE 를 막는 것은 tRAS(52)가 아니라 tWR(66). 이어서 ACT@88, RD@110 (연습 2).</text>
</svg>
```

**tRC 는 왜 따로 검사하나.** DDR4 속도 등급 표에서 tRC = tRAS + tRP (ns) 이다. 올림은 준가법적(subadditive)이라 ceil(a + b) ≤ ceil(a) + ceil(b) 이므로, 사이클로 바꾼 뒤에도 tRC ≤ tRAS + tRP (74 ≤ 52 + 22). 그러니 한 뱅크에서 tRAS 와 tRP 를 둘 다 지키면 tRC 는 자동으로 지켜진다 — 연습 3 이 ACT/PRE/ACT 전수(약 2만 개 트레이스)로 "tRC 단독 위반 0건" 을 확인한다. 그래도 검사하는 이유: (1) 데이터시트에 따라 tRC 가 tRAS(min) + tRP 보다 길게 적힌 부품·세대가 있을 수 있고 (그래서 테스트는 tRC = 80 인 가상의 부품으로 단독 위반을 확인한다), (2) 자동 프리차지(RDA/WRA)처럼 PRE 가 트레이스에 안 보이는 경우 tRC 가 유일한 안전망이며, (3) 체커는 스펙을 그대로 옮기는 것이 원칙이다 — "중복이라 뺐다" 는 검증 코드에서 버그가 숨는 곳이다.

**이 모델이 일부러 빼 놓은 것.** 뱅크 그룹·여러 뱅크 사이의 tRRD_S/L, tFAW, tCCD_S/L, 쓰기→읽기 전환 tWTR, 읽기↔쓰기 버스 방향 전환, AL ≠ 0, BC4/OTF, 자동 프리차지, 리프레시(tRFC, tREFI)와 tRAS(max). 다중 뱅크 cycle-accurate 모델은 MEM-A01, 리프레시는 MEM-B05, 페이지 정책은 MEM-B03.

**복잡도.** 명령 하나에 O(1) (규칙 최대 3개), 트레이스 전체 O(n), 상태는 뱅크당 정수 몇 개 — 하드웨어 체커(SVA assertion)나 FPGA 프로토콜 모니터로 옮기기 좋은 모양이다.

### Part C — earliest legal issue 와 최소성

`earliest_issue` 는 규칙 표를 그대로 "기준 시각 + need" 의 최댓값으로 바꾼 것이다. 기준이 없으면(`NEVER`) 아주 작은 수라서 max 에서 자연히 빠진다.

`fix_trace` 의 각 명령 i 의 발행 시각:

  out[i] = max( in[i], out[i−1] + 1, earliest(상태 after out[0..i−1]) )

**왜 이 탐욕이 최소인가 (귀납법).** 주장: 어떤 합법 스케줄 s (순서 같고 s[i] ≥ in[i]) 에 대해서도 모든 i 에서 out[i] ≤ s[i].
- earliest 는 이전 명령 시각들의 **단조 증가 함수**다 (각 항이 "기준 시각 + 상수" 이고 max 다). 상태(IDLE/ACTIVE, 어떤 명령이 기준인지)는 명령 순서로만 정해지므로 s 와 out 에서 같다.
- i 미만에서 out ≤ s 이면, earliest_out(i) ≤ earliest_s(i) ≤ s[i] (s 가 합법이므로), out[i−1] + 1 ≤ s[i−1] + 1 ≤ s[i], in[i] ≤ s[i]. 따라서 out[i] ≤ s[i].
- 그러니 탐욕 결과는 원소별 최소이고, 총 지연·마지막 명령 시각도 최소다.

solution.c 는 이것을 실험으로도 확인한다: 2000개 랜덤 트레이스(시드 0xB02, 명령 4~40개)를 고친 뒤 (a) 체커 위반 0, (b) 원래보다 빠른 명령 0, (c) 밀린 명령 16 121개 각각을 1 사이클 당기면 **그 명령에서** 위반이 생김(= 더 당길 수 없음).

`multi` 를 고친 결과를 그림으로:

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <line x1="90" y1="30" x2="700" y2="30" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="middle"><text x="90" y="24">0</text><text x="140" y="24">10</text><text x="190" y="24">20</text><text x="240" y="24">30</text><text x="290" y="24">40</text><text x="340" y="24">50</text><text x="390" y="24">60</text><text x="440" y="24">70</text><text x="490" y="24">80</text><text x="540" y="24">90</text><text x="590" y="24">100</text><text x="640" y="24">110</text><text x="690" y="24">120</text></g>
  <text x="10" y="24" fill="currentColor" font-size="12">cycle</text>
  <text x="10" y="66" fill="currentColor">요청</text>
  <rect x="90" y="50" width="4" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="140" y="50" width="4" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="190" y="50" width="4" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="240" y="50" width="4" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="245" y="50" width="4" height="22" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12"><text x="84" y="86">ACT</text><text x="184" y="86">PRE</text><text x="226" y="86">ACT</text><text x="134" y="100">RD</text><text x="246" y="100">RD</text></g>
  <g stroke="currentColor" stroke-dasharray="4 3" fill="none">
    <line x1="142" y1="104" x2="202" y2="148"/><line x1="192" y1="104" x2="352" y2="148"/>
    <line x1="242" y1="104" x2="462" y2="148"/><line x1="247" y1="104" x2="572" y2="148"/>
  </g>
  <g fill="currentColor" font-size="12"><text x="176" y="126">+12</text><text x="278" y="126">+32</text><text x="328" y="118">+44</text><text x="520" y="138">+65</text></g>
  <text x="10" y="166" fill="currentColor">고친 뒤</text>
  <rect x="90" y="150" width="4" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="200" y="150" width="4" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="350" y="150" width="4" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="460" y="150" width="4" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="570" y="150" width="4" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="680" y="150" width="20" height="22" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-size="12"><text x="84" y="188">ACT@0</text><text x="190" y="188">RD@22</text><text x="336" y="188">PRE@52</text><text x="446" y="188">ACT@74</text><text x="558" y="188">RD@96</text><text x="652" y="188">data 118–122</text></g>
  <text x="10" y="216" fill="currentColor" font-size="12">강조색 칸 = 위반이 있던 명령. 총 지연 153 cycle, 고친 트레이스 위반 0 (solution.c 실측).</text>
</svg>
```

각 명령이 왜 그 사이클인가: RD@22 = ACT + tRCD. PRE@52 = max(20, RD 22 + tRTP 12 = 34, ACT 0 + tRAS 52) → **tRAS 가 결정**. ACT@74 = max(30, PRE 52 + tRP 22 = 74, ACT 0 + tRC 74 = 74) → 둘이 동시에 결정 (tRC = tRAS + tRP 이므로 당연). RD@96 = 74 + 22.

**펌웨어 현실.** 이 함수가 실제로 쓰이는 곳은 세 군데다. (1) RTL/모델 검증의 프로토콜 체커(scoreboard, SVA) — 위반 목록이 그대로 디버그 로그다. (2) post-silicon: DDR 프로토콜 분석기나 컨트롤러 내부 트레이스 버퍼에서 뽑은 명령 로그를 오프라인으로 검사. (3) bring-up 때 부트로더(예: U-Boot SPL 의 DDR 초기화)가 컨트롤러 타이밍 레지스터에 넣을 값을 SPD/데이터시트에서 계산하는 코드 — Part A 의 변환이 바로 그것이다. 예를 들어 Synopsys uMCTL2 계열 컨트롤러는 `DRAMTMG*` 레지스터에 t_ras_min, t_rc, wr2pre(= WL + BL/2 + tWR, 이 레슨의 44 와 같은 꼴) 같은 필드를 두고, 컨트롤러:DRAM 클럭 비율이 1:2 이면 값을 2 로 나눠 올림해 넣는다 (필드 이름·단위는 컨트롤러 데이터북 확인).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan
```

실제 출력 (이 맥, ASan/UBSan 빌드도 같은 출력에 경고 없음, 실행 0.2 초 미만):

```text
clean trace: 0 violations
  DATA #1 RD row 5: beats [44, 48)
  DATA #2 RD row 5: beats [48, 52)
  DATA #8 RD row 3: beats [206, 210)
multi-violation trace: 6 violations
  VIOL #1 RD@10: tRCD  need 22 got 10 (ref #0 ACT@0)
  VIOL #2 PRE@20: tRAS  need 52 got 20 (ref #0 ACT@0)
  VIOL #2 PRE@20: tRTP  need 12 got 10 (ref #1 RD@10)
  VIOL #3 ACT@30: tRP   need 22 got 10 (ref #2 PRE@20)
  VIOL #3 ACT@30: tRC   need 74 got 30 (ref #0 ACT@0)
  VIOL #4 RD@31: tRCD  need 22 got 1 (ref #3 ACT@30)
  DATA #1 RD row 1: beats [32, 36)
  DATA #4 RD row 2: beats [53, 57)
fixed multi trace: ACT@0 RD@22 PRE@52 ACT@74 RD@96  (total delay 153 cyc)
fixed write trace: ACT@0 WR@22 PRE@66 ACT@88 RD@110
random fix: 2000 traces, fail=0, violations-after-fix=0, earlier-than-asked=0, delayed cmds=16121, non-minimal=0
60/60 passed
```

## 7. 자주 하는 실수

**① `double` 로 올림.** 값에 따라 1 사이클이 튄다.

```c
uint32_t n = (uint32_t)ceil(14.07 / 0.938);          /* 15.000000000000002 -> 16 (틀림) */
uint32_t m = (uint32_t)((14070ull + 938 - 1) / 938);  /* 15 (맞음) — 정수 ps */
```

연습 5 실측: tCK 938 ps 에서 1~60 ns (10 ps 간격) 중 8개 값이 틀린다. 그리고 tCK 자체를 반올림(833 ps)하면 정수로 해도 틀린다 → MT/s 로 정확한 유리수 계산 (연습 1).

**② tWR 을 WR 명령에서 잰다.** `need_gap(..., b->t_wr, ..., t->twr, ...)` 로 쓰면 PRE@46 을 허용한다. 실제로는 쓰기 데이터가 38~42 에 들어오고 그 뒤 24 사이클이 필요해 66 이 최소다. 데이터가 셀에 들어가기 전에 행을 닫아 **쓴 값이 사라지는** 버그 — 메모리 테스트(MEM-B08/B09)에서 "쓰기 직후 다른 행 접근" 패턴에서만 실패한다.

```c
need_gap(L, idx, R_TWR, b->t_wr, b->i_wr, t->cwl + t->bl2 + t->twr, c->cyc);   /* 44 */
```

**③ "없음" 을 0 으로 둔다.** `uint32_t t_pre = 0;` 이면 "사이클 0 에 PRE 가 있었다" 가 되어, 첫 ACT@10 이 tRP need 22 got 10 으로 잡힌다. 인덱스 −1 + `int64_t NEVER` 를 쓰거나 has-플래그를 둔다.

**④ 위반 명령을 적용하지 않거나 첫 위반에서 멈춘다.** `multi` 에서 위반한 PRE@20 을 무시하면 ACT@30 이 "ACTIVE 에 ACT" (STATE) 로 보고되어 진짜 원인(tRAS, tRTP, tRP, tRC)이 사라진다. 검증 로그는 **모든** 위반을 원인 그대로 보여야 한다.

**⑤ 부호 없는 뺄셈.** `uint32_t got = c->cyc - b->t_act;` 는 사이클이 거꾸로 온 트레이스에서 40억대가 되어 "위반 아님" 이 된다. 그래서 드라이버가 먼저 순서(ORDER)를 확인하고, 간격은 `int64_t` 로 계산한다.

**⑥ tRTP 의 4 nCK 하한, tRC 의 기준을 잊는다.** tRTP 는 max(4 nCK, 7.5 ns). tRC 의 기준은 마지막 **ACT** 다 (PRE 가 아니다 — PRE 기준은 tRP). ACT 에서 t_rd/t_wr 를 지우지 않으면 이전 행 사이클의 RD 가 남지만, 그 RD 는 이미 tRAS + tRP 전이라 결과에 영향은 없다 — 그래도 지우는 편이 의도가 분명하다.

## 8. 면접 Q&A

<details><summary>Q1. tRAS 는 왜 있나요? 최소만 있나요?</summary>

DRAM 읽기는 파괴적(destructive)이다. ACT 하면 셀 커패시터의 전하가 비트라인과 나뉘어(charge sharing) 셀 값이 사라지고, 센스 앰프가 그 작은 차이를 증폭해 0/1 로 확정하면서 **같은 값을 셀에 다시 써 넣는다**(restore). 이 복원이 끝나기 전에 PRE 로 워드라인을 내리면 행 전체 데이터가 반쯤 지워진 채 남는다. 그래서 ACT → PRE 최소 간격 tRAS 가 있다 (DDR4-3200 32 ns ≈ tRCD 13.75 ns + 복원 시간). 최대값도 있다: 행을 너무 오래 열어 두면 리프레시를 못 하므로 tRAS(max) 는 9 × tREFI 정도로 정해져 있다 (대표값, 데이터시트 확인).

</details>

<details><summary>Q2. 행 충돌(row conflict) 읽기 지연을 무엇이 지배하나요? 숫자로.</summary>

tRP + tRCD + CL ≈ 13.75 + 13.75 + 13.75 = 41.25 ns, 데이터 4 tCK(2.5 ns) 를 더해 43.75 ns. 세 항이 거의 같고 모두 배열 물리(비트라인 프리차지, 센스, 열 경로)라서 세대가 바뀌어도 잘 안 준다. 여기에 이전 ACT 의 tRAS 잔여, 직전 쓰기의 tWR 이 더해질 수 있다 (연습 2: 57.5 ns, 66.25 ns). CPU 에서 보이는 load-to-use 지연은 이보다 훨씬 크다 — 캐시 계층 미스 판정, 온칩 네트워크, 컨트롤러 큐 대기가 더해진다 (MEM-B10 에서 이 맥에서 실측).

</details>

<details><summary>Q3. DDR3-1600 CL11 에서 DDR4-3200 CL22 로 CL 숫자가 두 배인데, 왜 느려지지 않았죠? 빨라지지도 않았고요.</summary>

ns 로 보면 둘 다 13.75 ns 다 (11 × 1.25 = 22 × 0.625). 사이클 수가 커진 건 인터페이스 클럭이 두 배가 됐기 때문이다. 배열 접근 시간은 그대로이고, 대역폭(MT/s)만 두 배가 됐다. 그래서 "지연은 그대로, 대역폭은 증가" 가 DRAM 세대의 일반적 모양이고, 지연을 숨기려면 뱅크 병렬성(MEM-M02)과 행 버퍼 지역성(MEM-B03)이 필요하다.

</details>

<details><summary>Q4. tWR 은 왜 WR 명령이 아니라 쓰기 데이터의 끝에서 재나요?</summary>

쓰기 데이터는 WR 뒤 CWL 사이클에야 핀에 들어오고(그래서 WR 시점에는 아직 쓸 값이 없다), 들어온 데이터는 쓰기 드라이버가 센스 앰프를 뒤집고 그 값이 셀까지 충전되어야 한다. 그 마지막 단계에 필요한 시간이 tWR(15 ns) 이다. 그래서 WR 명령 기준 최소 간격은 CWL + BL/2 + tWR = 16 + 4 + 24 = 44 사이클(27.5 ns)이고, 컨트롤러 레지스터도 보통 이 합(write-to-precharge)으로 프로그램한다.

</details>

<details><summary>Q5. 이 체커에서 tRC 는 중복 아닌가요?</summary>

DDR4-3200AA 단일 뱅크에서는 그렇다: tRC(ns) = tRAS + tRP 이고 올림이 준가법적이라 74 ≤ 52 + 22. ACT/PRE/ACT 전수 검사에서 tRC 단독 위반은 0건이다 (연습 3). 그래도 검사한다: 부품·세대에 따라 표의 tRC 가 더 클 수 있고(가상의 tRC 80 이면 단독 위반 21가지), 자동 프리차지처럼 PRE 가 안 보이는 경우 유일한 보호이며, 검증 코드는 스펙의 규칙을 하나씩 옮겨야 리뷰가 된다.

</details>

<details><summary>Q6. IDLE 뱅크에 RD 를 보내면 실제 칩에서는 무슨 일이 일어나고, 어떻게 잡나요?</summary>

스펙상 금지된 명령이라 동작이 정의되지 않는다 — 칩이 무시할 수도, 이전 행 버퍼의 쓰레기를 내보낼 수도 있다. 컨트롤러 입장에서는 데이터가 오기를 기다리는데 엉뚱한 값이 와서 ECC 오류(MEM-B07)나 데이터 비교 실패로만 보인다. 그래서 (1) RTL 시뮬레이션에서는 메모리 모델(벤더 Verilog 모델)이 이런 명령에 경고를 내고, 이 레슨 같은 프로토콜 체커/SVA 로 잡고, (2) 실리콘에서는 프로토콜 분석기나 컨트롤러 트레이스로 명령 로그를 떠서 오프라인 검사한다. 원인은 대개 컨트롤러 스케줄러 버그나 잘못된 레지스터 값(페이지 정책·타이머)이다.

</details>

<details><summary>Q7. 부트로더/펌웨어에서 이 값들을 어떻게 넣나요? 너무 공격적이거나 보수적이면?</summary>

DIMM 이면 SPD EEPROM 에서 tAAmin · tRCDmin · tRPmin · tRASmin · tRCmin · tWRmin 등을 읽는다 (DDR4 SPD 는 중간 단위 MTB 125 ps + 미세 단위 FTB 1 ps 로 저장 — JESD21-C 확인). 납땜된 LPDDR/DDR 이면 보드 설정 테이블에 데이터시트 값을 넣는다. 그다음 Part A 처럼 동작 tCK 로 올림 변환(+ JEDEC 반올림 보정)해 컨트롤러 타이밍 레지스터에 쓴다 — 컨트롤러가 DRAM 클럭의 1/2 로 돌면 2 로 나눠 올림. 너무 공격적(짧게)이면 상온·저부하에서는 통과하고 고온·고부하·특정 패턴에서만 비트가 깨진다(memtest 가 몇 시간 뒤 실패). 너무 보수적이면 지연·대역폭 손해만 본다. 그래서 bring-up 은 스펙 값으로 시작해 메모리 테스트(MEM-B08/B09)와 스트레스를 돌리고, 마진 측정은 별도로 한다.

</details>

<details><summary>Q8. 자동 프리차지(RDA/WRA)는 이 모델에서 어떻게 표현하나요? open page vs close page 와 무슨 관계죠?</summary>

RDA 는 "RD + 가능한 가장 이른 PRE" 다. 칩이 내부적으로 PRE 를 max(ACT + tRAS, RD + tRTP) 에 낸다 — 바로 이 레슨의 `earliest_issue(PRE)` 와 같은 식이다 (WRA 는 WR + CWL + 4 + tWR 쪽). 그래서 close-page 정책 컨트롤러는 거의 모든 읽기를 RDA 로 내고, 다음 접근은 항상 row empty(48 cyc)가 된다. open-page 는 행을 열어 두어 hit(26)를 노리지만 conflict(70+)의 위험을 진다. 어느 쪽이 나은지는 히트율 문제이고, 손익분기 히트율 유도와 시뮬레이션은 MEM-B03.

</details>

## 9. 추가 연습

### 연습 1 — MT/s 로 정확히 변환 (DDR4-2400T)

DDR4-2400T (17-17-17, 대표값: tRCD = tRP = 14.16 ns, tRAS 32, tRC 46.16, tWR 15, tRTP 7.5 ns) 의 사이클 표를 (a) tCK = 833 ps 로 나누는 방법, (b) tCK = 2 000 000 / MTs ps 를 유리수 그대로 쓰는 `ps_to_cyc_mts(ps, mts) = ceil(ps · mts / 2 000 000)` 로 각각 구하라. 어디서 다른가?

<details><summary>답 보기</summary>

실측: (b) tRCD 17, tRP 17, tRAS 39, tRC 56, tWR **18**, tRTP **9**. (a) 는 tWR 19, tRTP 10 으로 하나씩 크다. 15 000 / 833 = 18.007, 7 500 / 833 = 9.004 — 833 이 833.33… 를 내림한 값이라 몫이 정수를 살짝 넘는다. DDR4-3200 은 625 ps 가 정확해 두 방법이 같다. 반올림된 tCK 로 생기는 이 문제 때문에 JEDEC 은 SPD 해석용 반올림 알고리즘에 작은 보정을 넣는다 (상수는 JESD21-C 확인).

</details>

### 연습 2 — 도착 시각 기준 읽기 지연

`read_latency(t, prefix, n, arrive, row)`: prefix 명령들을 재생한 뒤 `arrive` 에 `row` 읽기 요청이 오면 필요한 PRE/ACT/RD 를 가장 이른 합법 사이클에 내고, 마지막 data beat 끝 − arrive 를 돌려라. 다섯 경우를 구하라: hit (ACT@0 r1, 200 에 r1), empty (빈 뱅크, 10 에 r1), conflict (ACT@0 r1, 200 에 r2), conflict + tRAS (ACT@0 r1, RD@22, 30 에 r2), 쓰기 뒤 conflict (ACT@0 r1, WR@22, 30 에 r2).

<details><summary>답 보기</summary>

실측: **26 / 48 / 70 / 92 / 106** 사이클 = 16.25 / 30 / 43.75 / 57.5 / 66.25 ns. conflict + tRAS: PRE 는 max(30, 52, 34) = 52 → ACT 74 → RD 96 → 끝 122, 122 − 30 = 92. 쓰기 뒤: PRE = max(30, 52, 22 + 44 = 66) = 66 → ACT 88 → RD 110 → 끝 136 → 106. "conflict = 70" 은 행이 충분히 오래 열려 있던 경우의 하한일 뿐이다. MEM-B03 의 exact 모드가 이 대기를 넣는다.

</details>

### 연습 3 — tRC 는 중복 규칙인가

ACT@0, PRE@p, ACT@a (1 ≤ p ≤ 150, p < a ≤ 250) 를 전수 검사해 "tRC 는 위반인데 tRAS 와 tRP 는 지킨" 트레이스 수를 세라. tRC 를 80 으로 바꾼 가상의 부품에서도 세라. ceil 의 준가법성 ceil(x + y) ≤ ceil(x) + ceil(y) 도 ps 격자에서 확인하라.

<details><summary>답 보기</summary>

실측: tRC 위반 2628건 중 단독 **0**건. tRC = 80 이면 단독 **21**건 (p = 52…57, a = p + 22 … 79: 6 + 5 + 4 + 3 + 2 + 1). 준가법성 위반 0. 증명: x ≤ ceil(x), y ≤ ceil(y) 이므로 x + y ≤ ceil(x) + ceil(y) (정수), 따라서 ceil(x + y) ≤ ceil(x) + ceil(y). tRC(ns) = tRAS + tRP 이면 사이클에서도 tRC ≤ tRAS + tRP.

</details>

### 연습 4 — 한 뱅크의 대역폭

한 뱅크에 랜덤 행 읽기만 오면 tRC 마다 64 B 가 최선이다. MB/s (10^6 B/s) 를 정수로 구하고, 같은 행을 연속으로 읽을 때(4 tCK 마다 64 B)와 비교하라. 랜덤 행 읽기로 버스를 꽉 채우려면 뱅크가 몇 개 필요한가?

<details><summary>답 보기</summary>

실측: 64 B / (74 × 0.625 ns) = **1383 MB/s**, row hit 스트리밍 64 B / 2.5 ns = **25 600 MB/s** (= DDR4-3200 64비트 채널 피크, MEM-B04). 필요한 뱅크 수 ceil(74 / 4) = **19** 인데 x8 DDR4 는 16 뱅크다 — 게다가 실제로는 tRRD 와 tFAW(4 개 ACT 창)가 ACT 빈도를 더 묶는다 (MEM-A01). 그래서 랜덤 접근 대역폭은 피크에 못 미치고, 주소 매핑(MEM-B01, MEM-M02)이 뱅크를 고르게 쓰는 게 중요하다.

</details>

### 연습 5 — `double` 올림의 함정

`ceil(14.07 / 0.938)` 와 정수 `ps_to_cyc(14070, 938)` 을 비교하고, tCK 938 ps 에서 1~60 ns (10 ps 간격) 중 `double` 결과가 틀리는 값이 몇 개인지 세라.

<details><summary>답 보기</summary>

실측: 14.07 / 0.938 = 15.000000000000002 → ceil **16**, 정수는 **15**. 틀리는 값 **8**개 (4.69, 9.38, 14.07, 18.76, 28.14, 37.52, 51.59, 56.28 ns — 모두 tCK 의 정수배, 즉 정확히 나누어떨어져야 하는 값). 0.938 이 이진수로 정확하지 않아 몫이 정수 위로 살짝 뜬다. 해결: 정수 ps (또는 MT/s 유리수, 연습 1).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 13/13 passed
```

## 10. 핵심 정리

- 뱅크 = IDLE ↔ ACTIVE(row) 상태머신. ACT 는 IDLE 에서, RD/WR 는 ACTIVE 에서, PRE 는 언제나 (IDLE 이면 NOP).
- 모든 타이밍은 "기준 명령 → 대상 명령 최소 간격": tRCD ACT→RD/WR, tRAS ACT→PRE, tRP PRE→ACT, tRC ACT→ACT, tRTP RD→PRE, tWR 쓰기 데이터 끝→PRE (WR 기준 CWL + 4 + tWR). CL 은 규칙이 아니라 데이터가 나오는 시각.
- DDR4-3200AA: 22-22-22, tRAS 52, tRC 74, tWR 24 (WR→PRE 44), tRTP 12. 변환은 정수 ps 올림 (`double` · 반올림된 tCK 금지).
- 지연: hit 26 cyc (16.25 ns), empty 48 (30 ns), conflict 70 (43.75 ns) + tRAS/tWR 잔여. ns 는 세대가 바뀌어도 거의 그대로, 사이클 수만 커진다.
- 체커: 위반은 전부 보고, 위반 명령도 발행된 것으로 보고 상태 반영, 간격은 부호 있는 64비트, "없음" 은 0 이 아니라 NEVER.
- earliest = max(기준 + need). 순서 고정 탐욕은 단조성 때문에 원소별 최소 — 증명 + 2000 랜덤 트레이스 최소성 검사.
- tRC 는 DDR4 단일 뱅크에선 tRAS + tRP 로 함의되지만, 체커는 스펙을 그대로 옮긴다.

## 참고자료

- JEDEC JESD79-4 (DDR4 SDRAM Standard) — 명령 진리표, 상태도, 속도 등급별 tAA/tRCD/tRP/tRAS/tRC, tWR, tRTP, CWL. <https://www.jedec.org/standards-documents/docs/jesd79-4a> (무료 등록 필요할 수 있음; 이 레슨 수치는 대표값으로 표기)
- JEDEC JESD21-C, Annex L (DDR4 SPD) — MTB/FTB 단위, nCK 변환 반올림 알고리즘 (보정 상수 확인용).
- Micron DDR4 SDRAM 8Gb 데이터시트 (예: MT40A1G8) — 타이밍 표, PRECHARGE on idle bank, 쓰기 회복 그림. <https://www.micron.com/products/memory/dram-components/ddr4-sdram>
- B. Jacob, S. W. Ng, D. T. Wang, *Memory Systems: Cache, DRAM, Disk* (Morgan Kaufmann, 2008) — DRAM 명령·타이밍 파라미터 장(ch. 11 부근, 판본 확인).
- Ramulator 2.0 (CMU SAFARI) / DRAMsim3 (UMD) — 이 레슨의 규칙 표를 다중 뱅크로 확장한 오픈소스 cycle-accurate DRAM 시뮬레이터: <https://github.com/CMU-SAFARI/ramulator2>, <https://github.com/umd-memsys/DRAMsim3>
- O. Mutlu, Computer Architecture 강의 (ETH Zürich) DRAM 편 — 행 버퍼, 복원, 타이밍의 물리적 이유.
- 관련 레슨: MEM-B01 (주소 → bank/row/col), MEM-B03 (행 버퍼 hit/conflict, 페이지 정책), MEM-B04 (세대별 대역폭), MEM-B05 (리프레시), MEM-B07 (SECDED), MEM-B08 · MEM-B09 (메모리 테스트), MEM-M01 (FR-FCFS), MEM-M02 (매핑과 뱅크 병렬성), MEM-A01 (bank group · tFAW cycle-accurate), MEM-A04 (트레이닝), ARCH-B10 (AMAT), SIV-B07 (March C-)
