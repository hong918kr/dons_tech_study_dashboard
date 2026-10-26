# V7 · Shmoo Sweep + Margin Analysis (PVT characterization)

> **ID**: V7 · **Priority**: P0 · **Pattern**: 2-D grid sweep + repeat voting + monotonic-edge scan · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh V7` / `./run.sh V7 sol`

## 1. Problem

You are writing the characterization tool for first silicon. The DUT exposes one call, `run_test(v_mv, f_mhz)`, which sets the core supply and clock, runs a functional pattern and returns pass/fail. It is implemented at the bottom of `stub.c` by a hidden model (a voltage/frequency wall, plus an optional deterministic **hole** and an optional **flaky** point). Do not edit the fake.

Build a **shmoo** tool on top of it:

- **`shmoo_sweep`**: visit every grid point (V from `v_lo` to `v_hi`, F from `f_lo` to `f_hi`), run the test `repeats` times per point, and store one cell per point in `map[vi * nf + fi]` (`vi = 0` is `v_lo`, `fi = 0` is `f_lo`): `CELL_PASS` if all runs passed, `CELL_FAIL` if all failed, `CELL_MARGINAL` if mixed. Bad grid (a step of 0, `hi < lo`, `repeats == 0`) → `-EINVAL`; `cap < nv * nf` → `-ENOSPC`.
- **`shmoo_fmax`**: for row `vi`, the highest F such that F **and every lower grid frequency** are `CELL_PASS` ("contiguous pass from the bottom"). `0` if `f_lo` does not pass.
- **`shmoo_vmin`**: the lowest grid voltage whose fmax ≥ `target_mhz`; `0` if no row reaches it.
- **`shmoo_margin`**: at `nominal_mv` (must be a grid voltage, else `-EINVAL`): `margin_mhz = fmax − target` (may be negative) and `margin_pct10 = margin_mhz * 1000 / target` (percent × 10, truncated toward zero).
- **`shmoo_holes`**: a **hole** is a `CELL_FAIL` point with a `CELL_PASS` point on both sides of it in its row (lower F and higher F) **or** in its column (lower V and higher V). Report in map order; write at most `cap`, return the total.
- **`shmoo_render`**: ASCII plot, highest voltage first, one line per row: `"%4u |"` + one char per F (`*` pass, `.` fail, `?` marginal) + `'\n'`. `snprintf` rules: never write past `cap`, NUL-terminate when `cap > 0`, return the full length needed.
- No `malloc`, integer math only.

```c
bool run_test(uint16_t v_mv, uint16_t f_mhz);                 /* provided (fake DUT) */
enum { CELL_FAIL = 0, CELL_PASS = 1, CELL_MARGINAL = 2 };
typedef struct { uint16_t v_lo_mv, v_hi_mv, v_step_mv;
                 uint16_t f_lo_mhz, f_hi_mhz, f_step_mhz;
                 uint8_t  repeats; } ShmooCfg;
typedef struct { uint16_t v_mv, f_mhz; } ShmooPt;

int      shmoo_sweep (const ShmooCfg *c, uint8_t *map, size_t cap);
uint16_t shmoo_fmax  (const ShmooCfg *c, const uint8_t *map, size_t vi);
uint16_t shmoo_vmin  (const ShmooCfg *c, const uint8_t *map, uint16_t target_mhz);
int      shmoo_margin(const ShmooCfg *c, const uint8_t *map, uint16_t nominal_mv,
                      uint16_t target_mhz, int32_t *margin_mhz, int32_t *margin_pct10);
size_t   shmoo_holes (const ShmooCfg *c, const uint8_t *map, ShmooPt *out, size_t cap);
int      shmoo_render(const ShmooCfg *c, const uint8_t *map, char *buf, size_t cap);
```

### Example

```
grid: V 700..1000 step 50 (7 rows), F 200..1000 step 100 (9 cols), repeats = 3
DUT : pass iff f <= 2 * (v - 500); hole at (950 mV, 400 MHz); flaky at (800 mV, 600 MHz)

shmoo_render ->                fmax per row
1000 |*********                1000
 950 |**.*****.                 300   <- hole at 400 cuts the contiguous run
 900 |*******..                 800
 850 |******...                 700
 800 |****?....                 500   <- marginal is not a pass
 750 |****.....                 500
 700 |***......                 400

shmoo_vmin(700)               = 850 mV
shmoo_margin(900, 700)        = +100 MHz, pct10 = 142   (14.2 %)
shmoo_holes                   = { (950, 400) }          (the '?' is not a hole)
run_test calls                = 63 points x 3 = 189
```

### Constraints

- Grid up to a few thousand points; `map` is caller-provided, one byte per point.
- The fake counts `run_test` calls (`F.calls`), so the number of runs is tested exactly.
- The flaky point fails on every 2nd call to that point: one run looks fine, three runs do not.

### Clarifying questions to ask (first 3 minutes)

1. How is fmax defined when a row is not monotonic — the highest passing F, or the top of the contiguous pass run from the bottom?
2. How many repeats per point, and how should mixed results be reported?
3. Does a marginal point count as pass for fmax/Vmin? (Here: no — be conservative.)
4. Is margin measured in MHz, in %, or in mV (voltage margin at the target frequency)?
5. What do you call a hole — row-wise only, or also column-wise?
6. Is the full grid affordable in test time, or should I search for the wall?

## 2. Your workspace

`./run.sh V7`로 바로 테스트하세요. 테스트 7묶음이 있고, 파일 아래쪽 fake DUT는 건드리지 않습니다. 격자 크기 헬퍼 `shmoo_nv()` / `shmoo_nf()` / `row_mv()` / `col_mhz()`는 이미 들어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — repeat는 "통과 횟수"만 세면 된다</summary>

한 점에서 `pass`를 0부터 세고 `repeats`번 돌립니다. `pass == repeats`면 PASS, `pass == 0`이면 FAIL, 그 사이면 MARGINAL. 첫 실패에서 멈추면(early exit) 호출 횟수 테스트(189회)가 틀어지고, 무엇보다 "몇 번 중 몇 번 실패했나"라는 정보를 잃습니다.

</details>

<details>
<summary>Hint 2 — fmax는 아래에서부터 첫 non-PASS에서 멈춘다</summary>

`for (fi = 0; fi < nf; fi++) { if (map[...] != CELL_PASS) break; fmax = F(fi); }`. 위쪽에서 마지막 PASS를 찾으면 hole을 건너뛰어 버려서 950 mV 줄이 900 MHz로 나옵니다. 실리콘에서는 "그 주파수까지 **모든** 주파수가 안전해야" 쓸 수 있으므로 연속 구간이 맞습니다.

</details>

<details>
<summary>Hint 3 — render는 pos를 항상 증가, 쓰기만 조건부</summary>

`if (pos + 1 < cap) buf[pos] = ch; pos++;` 패턴을 모든 글자에 쓰면 버퍼가 작아도 안전하고, 반환값은 항상 전체 길이(112)입니다. 마지막에 `cap > 0`이면 `buf[min(pos, cap-1)] = '\0'`. 줄 머리는 `snprintf(head, 8, "%4u |", v)`로 만들면 됩니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

Shmoo plot은 "어떤 전압·주파수 조합에서 칩이 동작하는가"를 한 장에 보여 줍니다. V가 올라가면 트랜지스터가 빨라져서 더 높은 F까지 통과하므로, 정상적인 칩은 **오른쪽 위로 올라가는 계단(wall)** 하나로 PASS/FAIL이 나뉩니다. 계단 모양이 깨진 곳(hole, `?`)이 바로 validation 엔지니어가 찾아야 하는 버그 후보입니다.

<div class="chart" data-src="chart1.svg" data-caption="예제(테스트 7) 격자: 파란 계단이 wall. 900 mV(보라 점)에서 fmax 800, target 700 → margin +100 MHz(14.2%). 700 MHz를 처음 통과하는 전압 Vmin = 850 mV. 950/400 hole 때문에 950 mV 줄의 fmax는 300으로 떨어지고, 800/600은 3번 중 1번 실패라 ?"></div>

세 가지 숫자의 의미:
- **fmax(V)**: 그 전압에서 쓸 수 있는 최고 클럭. 오른쪽으로 걸어가다 처음 막히는 곳 바로 앞.
- **Vmin(F)**: 그 클럭을 쓰기 위한 최저 전압. 아래에서 위로 올라가며 처음 fmax ≥ F가 되는 줄. 전력은 V²에 비례하므로 Vmin을 낮추는 것이 곧 전력 절감입니다.
- **margin**: 정격(nominal) 전압에서 target 클럭보다 얼마나 여유가 있는가. 이 여유가 공정 편차·온도·노화·전원 노이즈를 버텨 줍니다.

### 4.2 PVT 코너에서 Vmin이 어떻게 움직이나

같은 sweep을 공정 코너(SS/TT/FF 칩) × 온도(cold/room/hot)마다 반복하면 아래 같은 그림이 나옵니다. 출하용 Vmin은 **가장 나쁜 코너 + guardband**로 정합니다.

<div class="chart" data-src="chart2.svg" data-caption="예시 값(설명용). 5 nm급 공정의 저전압 영역에서는 temperature inversion 때문에 cold가 최악(SS −40 °C = 780 mV). 예전 공정 습관대로 hot만 보면 놓칩니다. ship Vmin = 780 + 30 mV guardband = 810 mV"></div>

### 4.3 손으로 따라가기 (trace)

**sweep, 800 mV 줄, repeats = 3** (fake 모델 fmax(800) = 2 × (800 − 500) = 600):

| F | run 1 | run 2 | run 3 | pass | cell |
|---|---|---|---|---|---|
| 200 … 500 | P | P | P | 3 | PASS `*` |
| 600 (flaky) | P (call 1) | **F** (call 2, 짝수) | P (call 3) | 2 | **MARGINAL `?`** |
| 700 … 1000 | F | F | F | 0 | FAIL `.` |

repeats = 1이었다면 600은 call 1(P) 한 번만 보고 PASS가 됩니다(테스트 3번 앞부분). **한 번 통과는 증거가 아닙니다.**

**fmax 스캔** (테스트 7 격자):

| 줄 | 왼쪽부터 셀 | 멈춘 곳 | fmax |
|---|---|---|---|
| 950 | `*` `*` `.` … | fi = 2 (400, hole) | **300** |
| 900 | `*`×7 `.` | fi = 7 (900) | **800** |
| 850 | `*`×6 `.` | fi = 6 (800) | 700 |
| 800 | `*`×4 `?` | fi = 4 (600, marginal) | **500** |

**Vmin(700)**: 700 → 400 ✗, 750 → 500 ✗, 800 → 500 ✗, 850 → 700 ✓ → **850 mV**.

**margin** (테스트 숫자 그대로):

| nominal | target | fmax | margin_mhz | margin_pct10 = m × 1000 / target |
|---|---|---|---|---|
| 900 | 700 | 800 | +100 | 100000 / 700 = 142.8 → **142** (14.2 %) |
| 950 (hole 있음) | 700 | 300 | −400 | −400000 / 700 = −571.4 → **−571** |
| 900 | 1100 | 800 | −300 | −300000 / 1100 = −272.7 → **−272** |
| 925 | – | – | – | 격자에 없음 → `-EINVAL` |

**hole 판정** (950, 400): 같은 줄에 왼쪽 300 PASS, 오른쪽 500 PASS → hole. 열 방향으로도 900/400 PASS, 1000/400 PASS. 반면 800/600 `?`는 FAIL이 아니므로 hole 목록에 들어가지 않고, wall 바깥 `.`들은 왼쪽(낮은 F)과 위쪽(높은 V)에만 PASS가 있고 반대편에는 없으므로 hole이 아닙니다.

**render 길이**: 줄당 `"%4u |"` 6자 + 9자 + `\n` = 16자, 7줄 = **112**. 단일 줄 격자(900 mV만)는 `" 900 |*******..\n"` = 16.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `map[vi * nf + fi]` 행 우선(row-major) 1차원 배열입니다. 2차원 VLA를 피하고, 호출자가 크기를 검증받은 버퍼 하나만 넘기면 됩니다. `-ENOSPC` 검사가 **sweep 전에** 있어서 DUT를 한 번도 건드리지 않습니다.
- `shmoo_vmin`은 `shmoo_fmax`를 재사용합니다. "Vmin은 fmax 곡선의 역함수"라는 관계를 코드가 그대로 말해 줍니다.
- `margin_pct10`은 정수 나눗셈입니다. C99부터 음수 나눗셈은 0 쪽으로 잘리므로 −571.4 → −571. 반올림을 원하면 면접관에게 먼저 물어봅니다.
- `shmoo_holes`는 셀마다 줄·열을 다시 훑는 O(nv·nf·(nv+nf))입니다. 격자가 수천 점이면 충분합니다. 더 크면 줄마다 "첫 PASS / 마지막 PASS" 위치를 미리 구해 O(nv·nf)로 줄입니다.
- `shmoo_render`는 `snprintf`와 같은 계약(필요 길이 반환)이라 호출자가 `render(…, NULL, 0)`로 크기를 먼저 알아낼 수 있습니다.

### 4.5 복잡도

- sweep: `run_test` 호출 **nv × nf × R**회(예제 189회). 실제 시간은 점마다 전원 settle + 패턴 실행이라 이 숫자가 곧 테스트 비용입니다.
- fmax/vmin: O(nv·nf). holes: O(nv·nf·(nv+nf)). render: O(nv·nf). 추가 메모리 O(1).

---

## 5. 임베디드 관점으로 말할 것

- **SK hynix에서 직접 한 일과 연결**: "At SK hynix I built shmoo testing: sweep voltage against a timing parameter, repeat each point, and look for holes, not just the edge." 숫자 하나(margin)보다 **그림의 모양**이 디버그 정보라는 점을 강조합니다.
- **run_test 한 번에 실제로 일어나는 일**: PMIC(I2C/PMBus)로 VDD 설정 → settle 대기(수 ms) 및 ADC로 실제 전압 readback → PLL 재설정 후 lock 확인 → 테스트 패턴(메모리 테스트, NPU 커널, CRC 체크) 실행 → 결과 판정. 설정값이 아니라 **측정값**을 기록해야 IR drop을 반영합니다.
- **안전 순서**: 전압은 올릴 때 "V 먼저, F 나중", 내릴 때 "F 먼저, V 나중"입니다. 낮은 V에서 높은 F를 걸면 타이밍 위반으로 hang이 날 수 있으므로, fail 후에는 watchdog/리셋으로 DUT를 복구하고 다음 점으로 갑니다.
- **flaky를 버리지 않는다**: MARGINAL은 wall 근처 노이즈일 수도 있지만, wall 안쪽의 `?`는 race condition, 전원 노이즈 민감 회로, 특정 데이터 패턴 의존 버그일 가능성이 큽니다. 위치·횟수·패턴을 로그로 남깁니다.

## 5.1 면접관(HW 출신 Sr Manager)이 파고들 포인트

- **"A full grid is too slow for production. What would you do?"** — wall은 줄마다 단조(monotonic)라고 가정하고 **이분 탐색**(줄당 log₂ nf 회)이나, 이전 줄의 fmax에서 시작해 한두 칸만 보는 **edge tracking**을 씁니다. 단, 이렇게 하면 hole을 못 봅니다 → 특성 평가(char) 단계는 full grid, 양산(SLT)은 빠른 탐색으로 나눕니다.
  *"Full grid for characterization to find holes, then a binary search on the wall per voltage for production, because the edge is monotonic once the design is clean."*
- **"How do you know your test itself is correct?"** — 알려진 모델(지금 fake처럼 wall·hole·flaky를 심은 DUT)에 돌려서 정확히 찾아내는지 먼저 검증합니다. 실리콘에서는 정격 조건 PASS 확인, 강제로 fail시키는 조건(아주 낮은 V) 확인, 같은 칩을 두 번 돌려 재현성 확인.
  *"I validate the tool against a fake DUT with planted holes and flaky points before trusting it on silicon."*
- **"FPGA vs first silicon?"** — FPGA/HAPS에서는 전압·주파수를 바꿀 수 없어서 shmoo 자체는 의미가 없고, 도구의 흐름(제어, 로그, 리포트)만 미리 검증합니다. 실제 margin은 실리콘에서만 나옵니다.
  *"On FPGA I can only validate the flow; the V/F physics only exists on silicon."*
- **"How do results feed PVT and SLT?"** — 코너 칩(SS/TT/FF) × 온도(thermal forcer로 −40/25/125 °C)마다 Vmin/fmax를 모아 최악 코너 + guardband로 출하 조건과 binning 기준을 정하고, SLT에는 그 기준 근처 몇 점만 넣습니다.
  *"Characterization data across corners sets the guardband and the bins; SLT then checks only a few points near that edge."*
- **"Why repeats? Isn't the chip deterministic?"** — 디지털 로직도 wall 근처에서는 전원 노이즈·온도·데이터 패턴에 따라 결과가 흔들립니다. 1번 PASS로는 확신할 수 없고, R번 중 1번이라도 실패하면 그 점은 쓸 수 없습니다.
  *"Near the edge, a single pass proves nothing; any failure out of R runs makes the point unusable."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| What does PVT stand for, and what are the corners? | Process · Voltage · Temperature. 공정 코너는 NMOS/PMOS 속도 조합: SS(둘 다 느림), FF(둘 다 빠름), TT(typical), SF/FS(한쪽만 빠름 — 레벨 시프터·SRAM 읽기/쓰기 마진에 특히 중요). 전압은 nominal ±5–10 %, 온도는 −40 °C ~ 125 °C(차량용 AEC-Q100 Grade 1). |
| Isn't hot always the worst case? | 아닙니다. **temperature inversion**: 온도가 오르면 캐리어 이동도는 떨어지지만(느려짐) 문턱 전압 Vth도 떨어집니다(빨라짐). 공급 전압이 Vth에 가까운 저전압·첨단 공정(FinFET, 7/5 nm)에서는 Vth 효과가 이겨서 **cold가 더 느립니다**. 그래서 저전압 Vmin은 cold에서, 누설·발열 관련 문제는 hot에서 봅니다. 두 쪽 다 돌려야 합니다. |
| What is Vmin/Fmax binning? | 같은 웨이퍼에서도 칩마다 속도가 다릅니다. 측정한 fmax(또는 target F에서의 Vmin)로 칩을 등급(bin)으로 나눠 빠른 칩은 높은 클럭 SKU로, 느린 칩은 낮은 클럭 SKU나 높은 전압(AVS 테이블)으로 출하합니다. 칩별 Vmin을 eFuse에 기록해 런타임 AVS(adaptive voltage scaling)에 쓰기도 합니다. |
| The full grid takes too long. How do you cut test time? | ① 줄마다 wall을 이분 탐색(nf=9면 9회 → 4회), ② 이전 전압의 fmax 근처부터 탐색(edge tracking), ③ repeats는 wall 근처 점만 늘림, ④ 양산은 pass/fail 한두 점(target F @ ship Vmin, guardband 점)만. 대가는 hole을 못 본다는 것 — char 단계에서만 full grid. |
| How do you pick the guardband? | 측정 최악 코너 Vmin 위에 노화(aging, NBTI/HCI로 수년간 Vth 상승 → 수십 mV), 전원 레귤레이터 오차·리플, 측정 장비 오차, 테스트 패턴이 실제 워크로드보다 덜 가혹할 가능성을 더합니다. 너무 크면 전력 낭비, 너무 작으면 필드 불량. |
| SLT vs ATE? | ATE(automatic test equipment)는 스캔/BIST 패턴으로 구조적 결함을 빠르게 봅니다(stuck-at, at-speed transition). SLT(system level test)는 칩을 실제 보드에 올려 OS·드라이버·실제 워크로드(NPU 추론, DDR 트래픽)를 돌립니다. ATE가 놓치는 전원 노이즈·워크로드 의존 결함을 잡지만 느립니다. |
| What about aging? | 시간이 지나면 Vth가 올라가 fmax가 떨어집니다. HTOL(고온 가속 수명 시험) 전후로 shmoo를 다시 찍어 wall 이동량을 측정하고 guardband에 반영합니다. 차량용은 15년 수명 요구라 특히 중요합니다. |
| How would you automate this as a SW engineer? | Python 호스트 스크립트가 랩 장비를 제어: 전원 공급기(SCPI over LAN/USB, 예: `VOLT 0.85`), thermal forcer(ThermoStream 온도 설정 후 soak 대기), DUT는 UART/JTAG로 테스트 펌웨어 실행. 칩 안에서는 bare-metal C가 PMIC 레지스터(I2C/PMBus)와 PLL/클럭 디바이더 레지스터로 V/F를 바꿉니다. 결과는 CSV/DB에 (칩 ID, 코너, 온도, V, F, pass 수) 형태로 쌓아 자동 리포트. |
| Can the wall ever go the wrong way (higher V fails)? | 정상은 아니지만 실제로 나옵니다: 높은 V에서 hold-time 위반(데이터가 너무 빨리 도착), 발열로 인한 thermal throttling, 전원 레일 간 순서 문제. 그래서 column 방향 hole도 검사합니다. |

## 7. 자주 하는 실수

- fmax를 "그 줄에서 가장 높은 PASS"로 계산 → hole을 뛰어넘어 950 mV 줄이 900 MHz로 나옴 (테스트 2번)
- MARGINAL을 PASS로 취급 → 800 mV fmax가 600이 됨 (테스트 3번). 반대로 MARGINAL을 hole로 보고함
- 첫 실패에서 repeat를 멈춤 → 호출 횟수 189가 틀어지고 "몇 번 중 몇 번" 정보 손실
- `map[fi * nv + vi]` 처럼 인덱스 순서를 섞음 → 단일 줄 격자(테스트 5번)나 render에서 바로 드러남
- render를 아래 줄(낮은 V)부터 출력 → shmoo 관례(위로 갈수록 높은 V)와 반대
- `margin_pct10`을 `(m / target) * 1000`으로 계산 → 정수 나눗셈이 먼저 0이 됨
- nominal 전압이 격자에 없는데 가장 가까운 줄을 조용히 고름 → 잘못된 margin 보고. `-EINVAL`로 알리는 것이 맞음
- 버퍼 크기 검사를 sweep 뒤에 함 → DUT를 다 돌린 뒤 실패, 또는 오버런
