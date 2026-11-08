---
id: SIV-M07
time: 90분
updated: 2026-11-08
---
# SIV-M07 Shmoo Plot 생성과 마진 분석

## 학습 목표

- 전압(V) × 주파수(F) 2차원 sweep 을 자동화해 셀마다 **N회 반복 → PASS / MIXED / FAIL** 로 분류하고, ASCII 와 CSV shmoo 를 만든다.
- 열(주파수)마다 **strict Vmin** ("이 전압부터 위로는 전부 PASS") 을 정의하고, 1 mV 해상도의 Vmin 을 **이진 탐색(bisect)** 으로 11번의 probe 만에 찾는다. 범위 검사(bracket)와 불변식도 함께.
- 노이즈가 있을 때 1회 측정 Vmin 이 왜 거의 항상 **낙관적**인지 설명하고, 반복 측정 + **새 측정으로 확인(confirm)** + **guard band** 로 탈출률을 0 으로 만든다. "0개 실패 n회 → p ≤ 3/n (95%)" 를 유도한다.
- shmoo 에서 **홀(hole, 비단조)** 과 **산발 실패(sporadic, 마진과 무관)** 를 자동으로 찾아, 약한 다이(weak die)와 구분하는 리포트를 만든다.
- 온도 역전(temperature inversion) 때문에 **최악 코너가 OPP 마다 바뀐다**는 것을 데이터로 보이고, OPP(DVFS 동작점) 마진표를 만든다.

## 1. 문제

first silicon 이 랩에 오면 기능이 도는 걸 확인한 바로 다음 질문이 "**얼마나 여유가 있나?**" 이다 — 이 칩이 1 GHz 를 몇 mV 에서 돌고, 그게 데이터시트 OPP 전압보다 얼마나 아래인가. 그 답을 그림 한 장으로 보여주는 것이 shmoo plot 이고, NVIDIA/Apple/Qualcomm 의 post-silicon 팀과 Rivian 같은 자체 SoC 팀에서 "Vmin 을 어떻게 찾나? 간헐 실패는 어떻게 구분하나?" 는 단골 면접 질문이다.

Shmoo 는 격자의 각 점 (V, F) 에서 같은 테스트(패턴, 메모리 테스트, 커널)를 돌려 PASS/FAIL 을 찍은 2차원 지도다. 이름은 만화 *Li'l Abner* 의 캐릭터 "Shmoo" 모양을 닮아서 붙었다는 이야기가 유명하다. ATE 소프트웨어(예: Advantest SmarTest, Teradyne IG-XL)에 shmoo 도구가 내장돼 있고, 보드/시스템 검증에서는 PMIC 전압(PMBus `VOUT_COMMAND` 등)과 PLL 설정을 펌웨어/스크립트로 바꿔가며 직접 돌린다.

진짜 칩 대신 이 레슨은 **임계 경로 지연 모델**을 DUT 로 쓴다 (전부 정수 연산이라 결정적이다):

| 항목 | 모델 | 의미 |
|---|---|---|
| 지연 | `delay = K · V / (V − Vth)^1.5 · mob(T)` | alpha-power law (Sakurai–Newton), alpha = 1.5 |
| Vth | `Vth0 − 0.8 mV/°C · (T − 25)` | 뜨거우면 Vth ↓ → **빨라짐** |
| 이동도 | `1 + 3500 ppm/°C · (T − 25)` | 뜨거우면 이동도 ↓ → **느려짐** |
| 공정 | SS: Vth0 +40 mV, K ×1.10 · TT: 350 mV, 12000 ps · FF: −40 mV, ×0.90 | 코너 |
| 판정 | `delay · (1 + noise) + 30 ps ≤ 1/F` | noise = 측정마다 ±2 % 균등분포 |
| 결함 | hole(F, V창) · sporadic(T≥, V/F 창, ppm) · extra_vth | `fault_t` 로 주입 |

노이즈는 `hash(seed, V, F, T, trial)` 인 **counter-based 난수**라서, 스윕 순서를 바꿔도 같은 (V,F,T,trial) 은 같은 결과다. 덕분에 모든 CHECK 가 결정적이다.

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">shmoo 한 장에서 읽는 것</text>
  <polygon points="80,300 80,40 520,40 520,62 470,80 420,102 370,128 320,158 270,192 220,230 170,268 130,300" class="f-ok" stroke="none"/>
  <polygon points="130,300 170,268 220,230 270,192 320,158 370,128 420,102 470,80 520,62 520,300" class="f-mut" stroke="none"/>
  <polyline points="130,300 170,268 220,230 270,192 320,158 370,128 420,102 470,80 520,62" fill="none" stroke="currentColor" stroke-width="7" stroke-opacity="0.25"/>
  <polyline points="130,300 170,268 220,230 270,192 320,158 370,128 420,102 470,80 520,62" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <rect x="80" y="40" width="440" height="260" fill="none" stroke="currentColor"/>
  <text x="300" y="325" text-anchor="middle" fill="currentColor">F (MHz) →</text>
  <text x="30" y="170" fill="currentColor" transform="rotate(-90 30 170)" text-anchor="middle">V (mV) →</text>
  <text x="140" y="70" fill="currentColor">PASS (*)</text>
  <text x="420" y="260" fill="currentColor">FAIL (.)</text>
  <rect x="248" y="105" width="22" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="100" y="52" width="9" height="9" class="f-warn" stroke="currentColor"/>
  <rect x="122" y="76" width="9" height="9" class="f-warn" stroke="currentColor"/>
  <rect x="150" y="58" width="9" height="9" class="f-warn" stroke="currentColor"/>
  <rect x="112" y="98" width="9" height="9" class="f-warn" stroke="currentColor"/>
  <line x1="320" y1="300" x2="320" y2="158" stroke="currentColor" stroke-dasharray="4 3"/>
  <circle cx="320" cy="158" r="4" class="f-acc" stroke="currentColor"/>
  <circle cx="320" cy="120" r="5" class="f-acc" stroke="currentColor"/>
  <line x1="332" y1="120" x2="332" y2="158" stroke="currentColor"/>
  <line x1="327" y1="120" x2="337" y2="120" stroke="currentColor"/>
  <line x1="327" y1="158" x2="337" y2="158" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="536" y="60">■ 노이즈 띠(#): 경계 근처</text>
    <text x="548" y="76">반복하면 섞인다 (정상)</text>
    <text x="536" y="104">■ 홀: PASS 속의 FAIL 섬</text>
    <text x="548" y="120">→ 비단조. bisect 가 속는다</text>
    <text x="536" y="148">■ 산발: 경계에서 먼 #</text>
    <text x="548" y="164">→ 마진과 무관한 기능 버그</text>
    <text x="536" y="192">● Vmin(F): 이 열의 경계</text>
    <text x="536" y="214">● OPP 점: 동작 전압</text>
    <text x="548" y="230">margin = Vopp − Vmin</text>
    <text x="536" y="258">경계 기울기 dV/dF =</text>
    <text x="548" y="274">F 를 올리는 전압 비용</text>
  </g>
  <text x="340" y="142" fill="currentColor" font-size="12">margin</text>
</svg>
```

### Part A — 2차원 sweep · ASCII/CSV · 열별 strict Vmin

- `cell_class(npass, trials)`: 0 회 → FAIL, 전부 → PASS, 그 사이 → MIXED (trials=0 은 증거 없음 → FAIL).
- `shmoo_sweep(chip, T, trials, s)`: V 650–1000 mV (10 mV, 36행) × F 400–1200 MHz (50 MHz, 17열), 셀마다 trial 0..N−1 을 **전부** 실행해 PASS 횟수를 센다.
- `shmoo_render_ascii` / `shmoo_render_csv`: 위가 높은 V. ASCII 는 `*`/`#`/`.`, CSV 는 PASS 횟수. 반환값은 snprintf 처럼 "필요한 길이".
- `shmoo_vmin_col(s, j)`: 위에서 내려오며 PASS 가 끊기기 직전의 V (strict Vmin), 맨 위부터 PASS 가 아니면 −1.

### Part B — Vmin 이진 탐색 · 반복 측정 · 확인과 guard band

- `vmin_bisect(chip, T, F, lo, hi, res, trials, r)`: 인자 검사(BAD_ARG 면 측정 0회) → `hi` FAIL 이면 NO_PASS → `lo` PASS 면 BELOW_RANGE → 불변식 "lo 는 FAIL 확인, hi 는 PASS 확인" 을 유지하며 반씩. probe = trials 회 전부 PASS (첫 FAIL 에서 중단).
- `vmin_robust(...)`: bisect 후, **탐색 때 안 쓴 새 trial 번호**로 `confirm_n` 회 확인; 실패하면 `res` 만큼 올려 다시 확인, 한도를 넘으면 UNSTABLE.
- 노이즈 ±2 % 칩에서: 1회 측정 Vmin 은 몇 개나 새 측정 1000회 확인을 통과하나? robust 는? robust + 10 mV 는?

### Part C — 야간 특성화: VDD_NPU 6개 부품 × 3온도, 홀·산발·약한 다이 찾기

- 부품: 양품 TT-01 / SS-05 / FF-06, 그리고 결함 주입된 **FF-02**(125 °C 이상에서만 400–800 MHz, ≥850 mV 영역에 5 % 산발 실패), **TT-03**(700 MHz 에서 800–840 mV 가 항상 FAIL 인 홀), **SS-04**(Vth +45 mV 약한 다이).
- `shmoo_analyze`: 열마다 아래에서 처음 만나는 PASS 행 p 를 찾고, 그 위의 FAIL 구간 = HOLE, p 에서 50 mV 넘게 떨어진 MIXED = SPORADIC.
- `opp_check`: OPP 표 {500 MHz @ 720 mV, 700 @ 820, 900 @ 975}, guard band 30 mV. fine(bisect, 1 mV) 과 shmoo(strict, 이상 반영)를 합쳐 margin 판정.
- `report_chip`: 디버그 엔지니어가 바로 행동할 수 있는 HOLE / SPORADIC / OPP / SUMMARY 줄. 양품 3개는 조용히 PASS, 결함 3개는 각각 **다른 시그니처**로 FAIL 해야 한다.

## 2. Problem (English)

You are characterizing the NPU core rail of a new SoC. A test at (V, F, T) is a black box returning pass/fail; it is noisy near the timing edge and may contain non-monotonic defects.

```c
void shmoo_sweep(chip_t *c, int t_c, uint32_t trials, shmoo_t *s);   /* 36 V rows x 17 F cols, count passes */
int  shmoo_render_ascii(char *buf, size_t n, const shmoo_t *s);      /* returns needed length, like snprintf */
int  shmoo_render_csv(char *buf, size_t n, const shmoo_t *s);
int  shmoo_vmin_col(const shmoo_t *s, int j);                        /* lowest V such that every V' >= V passes */
vmin_status_t vmin_bisect(chip_t *c, int t_c, int f, int lo, int hi, int res, uint32_t trials, vmin_result_t *r);
vmin_status_t vmin_robust(chip_t *c, int t_c, int f, int lo, int hi, int res, uint32_t trials,
                          uint32_t confirm_n, uint32_t max_steps, vmin_result_t *r);
int  shmoo_analyze(const shmoo_t *s, int edge_band_mv, anomaly_t *out, int max);   /* HOLE / SPORADIC */
void opp_check(chip_t *c, const shmoo_t *s, const opp_t *o, int guard_mv, opp_res_t *r);
int  report_chip(char *buf, size_t n, const shmoo_t *s, const anomaly_t *an, int nan,
                 const opp_res_t *opp, int nopp, int guard_mv);                    /* returns #failures */
```

Constraints: no timing in checks; every random draw is a pure function of (seed, V, F, T, trial). `vmin_bisect` must
validate its bracket (`NO_PASS`, `BELOW_RANGE`) and use at most `2 + ceil(log2((hi-lo)/res))` probes. A confirmation
must use fresh trial ids. Example: noiseless TT die at 25 °C, 900 MHz → strict shmoo Vmin 770 mV; good parts report
`SUMMARY ... -> PASS`; die TT-03 must report `HOLE ... F=700MHz V=800..840mV` and fail OPP 700 MHz.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

모델(`delay_ps`, `measure_once`, 정답지 `vmin_truth`), 격자, 문자열 버퍼(`sb_printf`), `probe_all`, `confirm_pass`, 테스트 하네스는 주어진다. TODO 는 10개 함수. 실제로 돌린 결과 (앞·뒤만):

```text
FAIL starter.c:267  cell_class(0, 8) == CELL_FAIL && cell_class(8, 8) == CELL_PASS && cell_class(3, 8) == CELL_MIXED
FAIL starter.c:283  bad == 0 && tt.runs == (uint32_t)NV * NF * 4
FAIL starter.c:285  vmin_mismatch == 0
...
FAIL starter.c:460  lim_t[2] == 125 && strcmp(lim_c[2], "SS-05") == 0
12/48 passed
```

(통과하는 12개는 스텁의 기본값(FAIL / −1 / BAD_ARG / 0개)과 우연히 맞는 것들이다 — `cell_class(0,0)`, BAD_ARG 인자 검사 3개, "이상 0개"·"탈출 0개" 류. exit 1.)

## 4. 힌트

<details>
<summary>힌트 1 — sweep 은 조기 종료 금지, probe 는 조기 종료</summary>

shmoo 셀은 "몇 번 중 몇 번" 이 필요하다 (MIXED 를 봐야 하니까). 반면 Vmin 탐색의 probe 는 "전부 PASS 인가?" 만 알면 되므로 첫 FAIL 에서 멈춘다 — 이미 주어진 `probe_all` 이 그렇다. 실행 횟수(`c->runs`)가 곧 테스트 시간이다.

</details>

<details>
<summary>힌트 2 — strict Vmin 은 위에서 내려온다</summary>

아래에서 올라가며 "처음 PASS" 를 찾으면 노이즈 띠의 운 좋은 PASS 나 홀 아래 PASS 를 Vmin 으로 착각한다. `for (i = NV-1; i >= 0; i--) { if (!PASS) break; v = V_AT(i); }`.

</details>

<details>
<summary>힌트 3 — bisect 의 불변식</summary>

먼저 `hi` 가 PASS 인지(아니면 NO_PASS), `lo` 가 FAIL 인지(아니면 BELOW_RANGE) 확인한다. 그 다음부터는 "lo = 본 적 있는 FAIL, hi = 본 적 있는 PASS" 를 깨지 않게 `mid` 결과로 한쪽만 옮기고, `hi - lo <= res` 이면 답은 `hi`. `mid = lo + (hi - lo) / 2`.

</details>

<details>
<summary>힌트 4 — 확인은 "새" 측정으로</summary>

`confirm_pass(..., salt)` 는 trial 번호 `CONFIRM_BASE + salt*n + k` 를 쓴다. 탐색에 쓴 trial 0 을 다시 돌리면 같은 노이즈가 재생될 뿐이라 확인이 아니다. step 을 올릴 때마다 salt(= `confirm_steps`)도 바꾼다.

</details>

<details>
<summary>힌트 5 — 홀과 산발을 가르는 기준점</summary>

열마다 **아래에서 처음 나오는 PASS 행 p** 를 기준으로 삼는다. p 위의 FAIL = "아래는 되는데 위는 안 됨" = 비단조 = HOLE. p 위의 MIXED 는 노이즈 띠 폭(여기선 최대 ~40 mV)보다 멀면 SPORADIC. 산발은 칸마다 따로 찍지 말고 열당 하나로 묶어(범위·개수·최악 pass) 보고한다.

</details>

## 5. 풀이

### Part A — sweep, 그림, strict Vmin

```c
static void shmoo_sweep(chip_t *c, int t_c, uint32_t trials, shmoo_t *s) {
    memset(s, 0, sizeof *s);
    s->chip = c->name; s->t_c = t_c; s->trials = trials;
    for (int j = 0; j < NF; j++)                       /* F 고정, V 는 높은 쪽(안전)부터 */
        for (int i = NV - 1; i >= 0; i--) {
            uint32_t k = 0;
            for (uint32_t t = 0; t < trials; t++) k += measure_once(c, V_AT(i), F_AT(j), t_c, t);
            s->npass[i][j] = (uint16_t)k;
        }
}
```

**순서에 대한 현실.** 실보드에서 V 변경은 PMIC 램프 + 안정화(수 ms), F 변경은 PLL 재잠금(수십 µs~ms)이라 바꾸는 비용이 다르다. 그리고 FAIL 은 칩을 hang 시키거나 상태를 오염시킬 수 있어서 보통 **안전한 쪽(높은 V)에서 시작해 내려가고**, FAIL 이 나면 리셋 후 다음 점으로 간다. 이 레슨의 counter-based 난수 덕분에 순서는 결과에 영향이 없지만, 실제 칩에선 순서가 결과를 바꿀 수 있다 (자기 발열, 이전 FAIL 의 잔여 상태) — 그래서 리포트에 스윕 순서와 seed 를 남긴다.

**strict Vmin 을 쓰는 이유.** "처음 PASS 가 나온 V" 는 노이즈 띠의 운 좋은 PASS 일 수도, 홀 아래일 수도 있다. 고객이 원하는 것은 "그 전압 **이상이면 항상** 된다" 이므로 위에서 내려온다. 노이즈 0 인 TT 칩에서 열별 strict Vmin 은 1 mV 진실값을 10 mV 격자로 올린 값과 17열 모두 일치한다 (CHECK `vmin_mismatch == 0`).

**그림과 CSV.** `sb_printf` 는 잘려도 "필요했던 길이" 를 계속 더하므로 32바이트 버퍼에 그려도 반환값은 전체 길이이고 버퍼는 NUL 종료 상태다 (CHECK). CSV 는 PASS 횟수를 그대로 남긴다 — 나중에 스크립트에서 pass 비율 heatmap 을 그리거나 두 런을 비교할 때(추가 연습 4) 정보가 남아 있어야 한다. `demo_shmoo.c` 가 그린 실제 shmoo (TT-03, 25 °C, 20회 반복; 아래 부분만):

```text
SHMOO chip=TT-03 T=25C trials=20  (* pass, # mixed, . fail)
   mV |  40  45  50  55  60  65  70  75  80  85  90  95 100 105 110 115 120   F[x10MHz]
  850 |   *   *   *   *   *   *   *   *   *   *   *   *   *   #   .   .   .
  840 |   *   *   *   *   *   *   .   *   *   *   *   *   *   #   .   .   .
  830 |   *   *   *   *   *   *   .   *   *   *   *   *   *   .   .   .   .
  820 |   *   *   *   *   *   *   .   *   *   *   *   *   #   .   .   .   .
  810 |   *   *   *   *   *   *   .   *   *   *   *   *   #   .   .   .   .
  800 |   *   *   *   *   *   *   .   *   *   *   *   *   .   .   .   .   .
  790 |   *   *   *   *   *   *   *   *   *   *   *   #   .   .   .   .   .
  ...
  700 |   *   *   *   *   *   *   *   *   .   .   .   .   .   .   .   .   .
  690 |   *   *   *   *   *   *   *   #   .   .   .   .   .   .   .   .   .
  680 |   *   *   *   *   *   *   *   .   .   .   .   .   .   .   .   .   .
  670 |   *   *   *   *   *   *   #   .   .   .   .   .   .   .   .   .   .
  660 |   *   *   *   *   *   *   .   .   .   .   .   .   .   .   .   .   .
  650 |   *   *   *   *   *   #   .   .   .   .   .   .   .   .   .   .   .
```

700 MHz 열(`70`)에 800–840 mV 의 FAIL 섬이 보인다. 경계 바로 위의 `#` 은 정상적인 노이즈 띠다. 데모가 쓴 CSV 3개(`shmoo_TT-01_25C.csv`, `shmoo_TT-03_25C.csv`, `shmoo_FF-02_125C.csv`)가 레슨 디렉터리에 들어 있다.

### Part B — 이진 탐색, 노이즈, 확인

```c
    if (lo >= hi || res <= 0 || trials == 0) return VMIN_BAD_ARG;            /* 측정 0회 */
    r->probes++;
    if (!probe_all(c, t_c, f, hi, trials, 0)) { r->status = VMIN_NO_PASS; goto out; }
    r->probes++;
    if (probe_all(c, t_c, f, lo, trials, 0)) { r->status = VMIN_BELOW_RANGE; r->vmin = lo; goto out; }
    while (hi - lo > res) {
        int mid = lo + (hi - lo) / 2;
        r->probes++;
        if (probe_all(c, t_c, f, mid, trials, 0)) hi = mid; else lo = mid;
    }
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m07-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">vmin_robust = bracket → bisect(N회) → 새 측정 확인 → step-up → + guard band</text>
  <g stroke="currentColor" stroke-width="1.2">
    <rect x="10" y="40" width="130" height="50" rx="6" class="f-mut"/>
    <rect x="170" y="40" width="130" height="50" rx="6" class="f-mut"/>
    <rect x="330" y="40" width="150" height="50" rx="6" class="f-acc"/>
    <rect x="510" y="40" width="210" height="50" rx="6" class="f-ok"/>
    <rect x="510" y="150" width="210" height="44" rx="6" class="f-warn"/>
    <rect x="300" y="150" width="180" height="44" rx="6" class="f-mut"/>
    <rect x="510" y="240" width="210" height="40" rx="6" class="f-ok"/>
    <rect x="10" y="150" width="150" height="44" rx="6" class="f-warn"/>
    <rect x="10" y="240" width="150" height="40" rx="6" class="f-warn"/>
  </g>
  <g stroke="currentColor" stroke-width="1.3" fill="none" marker-end="url(#m07-arr)">
    <line x1="140" y1="65" x2="168" y2="65"/>
    <line x1="300" y1="65" x2="328" y2="65"/>
    <line x1="480" y1="65" x2="508" y2="65"/>
    <line x1="615" y1="90" x2="615" y2="148"/>
    <line x1="510" y1="172" x2="482" y2="172"/>
    <path d="M390 150 C390 120 560 120 580 92"/>
    <line x1="615" y1="194" x2="615" y2="238"/>
    <line x1="75" y1="90" x2="75" y2="148"/>
    <line x1="235" y1="90" x2="160" y2="240"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="60">hi(1000 mV) PASS?</text>
    <text x="20" y="78">아니면 NO_PASS</text>
    <text x="180" y="60">lo(650 mV) FAIL?</text>
    <text x="180" y="78">아니면 BELOW_RANGE</text>
    <text x="340" y="60">mid PASS → hi=mid</text>
    <text x="340" y="78">FAIL → lo=mid (×9)</text>
    <text x="520" y="60">확인: vmin 에서 새 trial</text>
    <text x="520" y="78">200회 전부 PASS?</text>
    <text x="520" y="168">FAIL 하나라도 →</text>
    <text x="520" y="184">vmin += 1 mV, salt++</text>
    <text x="310" y="168">steps &gt; max 또는 &gt; hi</text>
    <text x="310" y="184">→ UNSTABLE</text>
    <text x="520" y="258">통과 → p ≤ 1.5 % (95 %)</text>
    <text x="520" y="274">제품 값 = vmin + guard band</text>
    <text x="20" y="168">NO_PASS: 이 F 불가</text>
    <text x="20" y="184">(Fmax 초과)</text>
    <text x="20" y="258">BELOW_RANGE:</text>
    <text x="20" y="274">vmin ≤ lo (더 내려 볼 것)</text>
  </g>
</svg>
```

**probe 수.** bracket 2번 + `ceil(log2(350/1)) = 9` 번 = 최대 11번. 1 mV 선형 스캔은 351점이다. 노이즈 0 칩, 17개 주파수에서 실제로 돌린 결과 (`solution.c` B1):

```text
B1 bisect: 17 freqs, max probes=11, total runs=125 (linear 1mV scan would be 351 per freq)
```

**왜 결과가 노이즈 띠 안에 있나.** 노이즈가 ±σ 로 유계(bounded)일 때, 노이즈를 −σ 로 고정한 Vmin 을 V_lo, +σ 로 고정한 Vmin 을 V_hi 라 하자. V ≥ V_hi 면 어떤 노이즈든 PASS, V < V_lo 면 어떤 노이즈든 FAIL. bisect 의 `lo` 는 실제로 FAIL 을 본 점이므로 `lo < V_hi`, `hi` 는 PASS 를 본 점이므로 `hi ≥ V_lo`. 끝나면 `hi ≤ lo + 1 ≤ V_hi` 이므로 **답은 항상 [V_lo, V_hi] 안**이다 (CHECK `out_of_band == 0`). 하지만 띠 안의 *어디*인지는 운이다 — 1회 측정이면 운 좋은 PASS 를 만나는 순간 hi 가 내려가므로 띠의 아래쪽(낙관적)으로 치우친다.

**재생은 확인이 아니다.** 1회 탐색으로 얻은 Vmin 에서 trial 0 을 다시 돌리면 11/11 PASS 한다 — 같은 난수를 다시 본 것뿐이다. 실칩에서는 "방금 그 조건 그대로 바로 다시 돌리기" 가 이것과 비슷하다 (온도·전원 상태·패턴이 상관돼 있음). 확인은 **새 trial 번호**, 실칩이면 다른 시점·다른 seed·충분한 횟수로.

**몇 번 확인해야 하나 (rule of three).** 실패 확률 p 인 점에서 n 회 연속 PASS 할 확률은 (1−p)^n. "p 가 이 값 이상이었다면 n 회 중 한 번은 FAIL 을 봤을 확률이 CL 이상" 이 되려면

```text
(1 − p)^n ≤ 1 − CL   →   n ≥ ln(1 − CL) / ln(1 − p)  ≈  −ln(1 − CL) / p      (p 작을 때 ln(1−p) ≈ −p)
CL = 95 %  →  −ln(0.05) = 2.996  →  n ≈ 3 / p          (0 실패 n 회 → p ≤ 3/n, "rule of three")
n = 200    →  p ≤ 1 − 0.05^(1/200) = 1.487 %
```

SIV-M06 의 BER 식 `N ≥ −ln(1−CL)/BER` 과 **같은 식**이다 (비트 대신 테스트 실행). 실제로 돌린 B3 (노이즈 ±2 % TT 칩, 경계가 범위 안인 11개 주파수):

```text
B3 noisy: freqs=11 escape(1000 fresh) naive=11 robust=2 robust+10mV=0 stepped=9 runs naive=116 robust=3214
```

- 1회 측정 bisect: 11개 모두 새 측정 1000회 확인에서 FAIL (탈출 11/11).
- 8회 반복 + 200회 확인 + step-up: 2/11 로 줄지만 0 은 아니다 — 200회 확인은 p ≤ 1.5 % 까지만 보장하고, 1000회 확인은 0.3 % 수준의 실패도 잡기 때문이다.
- 거기에 10 mV guard band: 0/11.

`demo_shmoo.c` 로 무작위 칩 400개 × OPP 3개(경계가 범위 안인 688 케이스)에 대해 같은 비교를 한 결과 (측정 1회 = 20 ms 가정):

```text
Vmin search strategies over 688 (chip, T, F) cases, noise +-2%, fresh confirm = 2000 runs
strategy                     escape     avg runs  avg time@20ms    avg V-Vhi
bisect 1 trial                99.6%         10.4          0.21s        -8.3
bisect 8 trials               86.0%         55.2          1.10s        -2.0
8 trials + confirm 200        15.7%        293.8          5.88s        -0.2
  ... + 10 mV guard            0.0%        293.8          5.88s        +9.8
```

`avg V-Vhi` 가 음수 = "항상 PASS 하는 전압(V_hi)보다 아래를 답했다" = 낙관적. 반복 수를 늘리면 V_hi 에 다가가지만 비용이 선형으로 늘고, 마지막 몇 mV 는 확률적으로만 좁혀진다. 그래서 실무는 **확인된 Vmin + guard band** 를 쓴다. guard band 에는 측정 불확도 외에도 테스터 전압 정확도, 테스터와 시스템의 IR drop / di/dt droop 차이, 온도 오차, 수명 열화(NBTI/HCI 로 인한 Vmin 상승)가 들어간다. 진짜 칩의 노이즈는 유계가 아니므로(가우시안 꼬리) "항상 PASS" 인 V_hi 가 존재하지 않는다는 점도 guard band 가 필요한 이유다.

### Part C — 야간 특성화와 리포트

**분석 규칙.** 열마다 아래에서 처음 PASS 인 행 p 를 찾는다. 노이즈 띠 폭은 "지연 ±2 % 가 몇 mV 에 해당하나" 로 정해진다: 900 MHz 근처(지연 ≈ 1080 ps, 기울기 ≈ 1.1 ps/mV)에서 4 % × 1080 ≈ 43 ps ≈ 39 mV. 그래서 edge band 를 50 mV 로 잡았고, 양품 3개 × 3온도에서 오탐 0 이다 (CHECK C1).

| 시그니처 | 판정 규칙 | 뜻 | 다음 행동 |
|---|---|---|---|
| HOLE | p 위의 FAIL 연속 구간 | 비단조: 특정 F·V 창에서만 실패 | PLL/VCO 대역 전환, 클럭 설정 표, PDN 공진 확인. bin 판정 보류 |
| SPORADIC | p 에서 50 mV 넘게 위의 MIXED | 전압을 올려도 안 사라짐 = 마진 문제 아님 | 온도·패턴·seed 와 함께 설계 디버그로 (CDC/메타안정성, SRAM, 열 관련) |
| OPP FAIL (이상 없음) | 지도는 깨끗한데 margin < guard | 다이가 전체적으로 느림 (weak die) | binning / AVS(per-die 전압 fuse) 결정 |

**OPP 판정의 vmin_eff.** fine(1 mV bisect)은 해상도가 좋지만 단조성을 가정하고, shmoo strict 는 10 mV 격자지만 홀·산발을 반영한다. 그래서 `shmoo − fine ≥ V_STEP` 이면(= 격자 반올림으로 설명이 안 될 만큼 shmoo 가 나쁘면) shmoo 를, 아니면 fine 을 쓴다.

실제로 돌린 `solution.c` 의 야간 런 (양품은 SUMMARY 한 줄만; 결함 부품 줄 일부):

```text
---- night run: VDD_NPU characterization, 6 parts x {-40,25,125}C ----
SUMMARY  chip=TT-01 T=-40C anomalies=0 opp_fail=0/3 -> PASS
...
SUMMARY  chip=FF-02 T=25C anomalies=0 opp_fail=0/3 -> PASS
SPORADIC chip=FF-02 T=125C F=400MHz V=850..990mV cells=10 worst=17/20 (+200mV above first clean pass) -> not margin-related
SPORADIC chip=FF-02 T=125C F=450MHz V=870..1000mV cells=8 worst=14/20 (+220mV above first clean pass) -> not margin-related
... (500–800 MHz 7줄 더)
OPP      chip=FF-02 T=125C F=500MHz opp=720mV vmin=990mV (fine=650 shmoo=990) margin=-270mV need>=30mV FAIL
OPP      chip=FF-02 T=125C F=700MHz opp=820mV vmin=none (fine=650 shmoo=-1) no stable pass <=1000mV FAIL
OPP      chip=FF-02 T=125C F=900MHz opp=975mV vmin=669mV (fine=669 shmoo=670) margin=306mV need>=30mV PASS
SUMMARY  chip=FF-02 T=125C anomalies=9 opp_fail=2/3 -> FAIL
HOLE     chip=TT-03 T=25C F=700MHz V=800..840mV cells=5 pass=0/20 (+120mV above first clean pass) -> non-monotonic, bisect answer depends on probe points
OPP      chip=TT-03 T=25C F=500MHz opp=720mV vmin=650mV (fine=650 shmoo=650) margin=70mV need>=30mV PASS
OPP      chip=TT-03 T=25C F=700MHz opp=820mV vmin=841mV (fine=841 shmoo=850) margin=-21mV need>=30mV FAIL
OPP      chip=TT-03 T=25C F=900MHz opp=975mV vmin=776mV (fine=776 shmoo=780) margin=199mV need>=30mV PASS
SUMMARY  chip=TT-03 T=25C anomalies=1 opp_fail=1/3 -> FAIL
OPP      chip=SS-04 T=125C F=500MHz opp=720mV vmin=703mV (fine=703 shmoo=710) margin=17mV need>=30mV FAIL
OPP      chip=SS-04 T=125C F=700MHz opp=820mV vmin=852mV (fine=852 shmoo=860) margin=-32mV need>=30mV FAIL
OPP      chip=SS-04 T=125C F=900MHz opp=975mV vmin=none (fine=-1 shmoo=-1) no stable pass <=1000mV FAIL
SUMMARY  chip=SS-04 T=125C anomalies=0 opp_fail=3/3 -> FAIL
WORST    OPP  500MHz@720mV: vmin=676mV by SS-05 at -40C -> margin 44mV
WORST    OPP  700MHz@820mV: vmin=778mV by SS-05 at 125C -> margin 42mV
WORST    OPP  900MHz@975mV: vmin=940mV by SS-05 at 125C -> margin 35mV
48/48 passed
```

읽는 법:

- **FF-02**: fine bisect 는 650 mV 라고 말한다 (실제 경계는 거기 맞다). 하지만 shmoo 는 그 위 200 mV 에 걸쳐 14/20~18/20 짜리 셀이 흩어져 있다. 전압을 올려도 안 사라지는 실패 = 마진 문제가 아니라 **기능 버그**. 25 °C 와 −40 °C 에서는 깨끗하므로 "뜨거울 때만" 이 중요한 단서다. bisect 만 돌렸다면 이 칩은 양품이었다.
- **TT-03**: bisect 의 첫 mid 가 (650+1000)/2 = 825 mV 로 **홀 한가운데**에 떨어졌다. FAIL 을 봤으니 위쪽만 탐색해 841 mV 를 답했고, 이것만 보면 "700 MHz 에서 Vmin 이 160 mV 높은 약한 칩" 으로 오분류된다. shmoo 는 실제 경계가 680 mV 근처이고 800–840 mV 에만 FAIL 섬이 있다는 것을 보여준다. 반대로 홀이 probe 들보다 위에 있으면 bisect 는 홀을 아예 못 보고 낙관적인 답을 낸다 (추가 연습 2: 홀 880–920 mV 에서 bisect 672 mV vs top-down 921 mV). **bisect 답은 probe 위치에 달려 있다** — 그래서 coarse shmoo 를 먼저 돌려 단조성을 확인하고, fine bisect 는 그 다음이다.
- **SS-04**: 지도에는 이상이 없다(단조, 깨끗). 그냥 전체가 위로 밀려 700 MHz OPP 를 모든 온도에서, 900 MHz 를 125 °C 에서 못 맞춘다. 이건 binning/AVS 문제다.

**온도 역전 — 최악 코너가 OPP 마다 바뀐다.** WORST 줄을 보면 500 MHz OPP 의 최악은 **−40 °C**, 700/900 MHz 는 **125 °C** 다. 저전압에서는 Vth 감소(뜨거우면 빨라짐)가, 고전압에서는 이동도 감소(뜨거우면 느려짐)가 이긴다. TT 모델에서 두 곡선은 695 mV 에서 교차한다 (추가 연습 5):

```svg
<svg viewBox="0 0 740 350" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">TT 지연 vs 전압 (모델 값): 저전압은 cold 가, 고전압은 hot 이 느리다</text>
  <rect x="80" y="40" width="600" height="260" fill="none" stroke="currentColor"/>
  <line x1="80" y1="174.9" x2="680" y2="174.9" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="560" y="168" fill="currentColor" font-size="12">900 MHz 한계 1081 ps</text>
  <line x1="80" y1="92.5" x2="680" y2="92.5" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="560" y="86" fill="currentColor" font-size="12">700 MHz 한계 1398 ps</text>
  <polyline points="80,55.1 165.7,128.1 251.4,177.5 337.1,213.2 422.9,240.2 508.6,261 594.3,277.6 680,291.4" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="2 3"/>
  <polyline points="80,65.7 165.7,122.7 251.4,163.5 337.1,194.7 422.9,218.9 508.6,238.4 594.3,254.5 680,267.8" fill="none" stroke="currentColor" stroke-width="1.2"/>
  <polyline points="80,86.5 165.7,125.5 251.4,155.7 337.1,179.9 422.9,199.9 508.6,216.3 594.3,230.6 680,242.5" fill="none" stroke="currentColor" stroke-width="3"/>
  <circle cx="157.1" cy="127" r="5" class="f-acc" stroke="currentColor"/>
  <text x="175" y="106" fill="currentColor" font-size="12">교차 ≈ 695 mV</text>
  <g fill="currentColor" font-size="12">
    <text x="80" y="318">650</text><text x="165" y="318">700</text><text x="251" y="318">750</text><text x="337" y="318">800</text>
    <text x="423" y="318">850</text><text x="508" y="318">900</text><text x="594" y="318">950</text><text x="660" y="318">1000 mV</text>
    <text x="40" y="58">1550</text><text x="40" y="196">1020</text><text x="40" y="300">600 ps</text>
    <text x="80" y="342">점선 −40 °C · 가는 선 25 °C · 굵은 선 125 °C</text>
  </g>
</svg>
```

700 MHz 한계선(1398 ps)은 교차점보다 낮은 전압에서 곡선과 만나므로 TT 칩에서는 cold 가 늦게(높은 V 에서) 통과하고, 900 MHz 한계선은 교차점 위라 hot 이 늦게 통과한다. 최악값을 정하는 SS 코너는 Vth 가 높아 교차점이 735 mV 로 올라가므로, SS 의 500 MHz Vmin(~676 mV)은 cold 쪽, 700 MHz(~750–780 mV) 이상은 hot 쪽이 최악이 된다 — 위 WORST 줄과 일치한다. 그래서 "상온에서만 특성화" 하거나 "뜨거울 때가 늘 최악" 이라고 가정하면 저전압 OPP 의 마진을 과대평가한다. FinFET 같은 최신 공정에서 실제로 문제 되는 현상이다.

**테스트 시간 표** (측정 1회 = 20 ms 가정; 실행 횟수는 실제로 센 값):

| 작업 | 실행 횟수 | 시간 |
|---|---|---|
| shmoo 36×17, 셀당 20회 (칩 1개·온도 1개) | 12240 | 4.1 분 |
| 이번 야간 런 shmoo 6칩 × 3온도 | 220320 | 73 분 |
| Vmin 1회 측정 bisect (OPP 1개) | 평균 10.4 | 0.2 초 |
| Vmin robust (8회 + 확인 200) | 평균 293.8 | 5.9 초 |

특성화(소수의 칩, 모든 코너)는 전체 shmoo 를 쓰고, 양산은 OPP 근처 몇 점만 확인하거나(spot check) 경계 근처만 따라가는(edge-tracking) shmoo 로 시간을 줄인다. 이 구분이 SIV-M01(ATE/SLT/시스템 역할), PVT 매트릭스의 시간 예산은 SIV-M08 의 주제다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san
cc -std=c11 -Wall -Wextra -Werror -O2 demo_shmoo.c -o demo_shmoo && ./demo_shmoo --csv .
```

실제 결과: 위의 B1/B3/야간 런 출력 + `48/48 passed`, exit 0, 실행 약 0.1 초. ASan/UBSan 빌드도 `48/48 passed`, 진단 없음. (Apple clang 21.0.0, Apple Silicon.) `demo_shmoo.c` 는 `#define main ...` 으로 같은 디렉터리의 `solution.c` 를 include 해서 그림 3장 + 전략 통계표를 출력하고, `--csv DIR` 이면 CSV 를 쓴다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| Vmin = 아래에서 "처음 PASS" | 노이즈 띠의 운 좋은 PASS, 홀 아래 PASS 를 Vmin 으로 보고 | 위에서 내려오며 PASS 가 끊기기 직전 (strict) |
| 셀마다 1회만 측정 | MIXED 가 안 보이고, 경계가 실행마다 흔들림 | 셀당 N회, k/N 저장 |
| sweep 에서도 첫 FAIL 에서 중단 | 셀이 PASS/FAIL 만 남아 산발 실패율을 못 봄 | sweep 은 전부 실행, probe 만 조기 종료 |
| bracket 확인 없이 bisect | hi 가 FAIL 이면 범위 밖 값을, lo 가 PASS 면 엉뚱한 경계를 "찾음" | NO_PASS / BELOW_RANGE 먼저 |
| `mid = (lo + hi) / 2` 로 res 무시, 종료 조건 `lo < hi` | 정수 V 에서 무한 루프 또는 off-by-one | 불변식 + `while (hi - lo > res)` → 답 `hi` |
| 확인을 탐색과 같은 seed/조건으로 | 같은 노이즈 재생 = 11/11 "확인" 통과, 실제 탈출 11/11 | 새 trial 번호, 충분한 n (3/p) |
| Vmin 을 그대로 제품 전압으로 | 확인된 Vmin 도 p ≤ 3/n 일 뿐, 수명·IR drop·온도 오차 없음 | + guard band, OPP margin 검사 |
| bisect 결과만으로 binning | 홀에 probe 가 떨어지면 약한 칩으로 오분류(841 mV), 위에 있으면 못 봄 | coarse shmoo 로 단조성 확인 후 fine bisect |
| 상온(25 °C)에서만 특성화 | 온도 역전: 저전압 OPP 는 −40 °C 가 최악 | 모든 OPP × {cold, room, hot} |
| FAIL 후 리셋 없이 다음 점 | 칩 상태 오염으로 그 뒤 점들이 연쇄 FAIL (순서 의존) | FAIL 마다 리셋/재초기화, 주기적 sanity point 재측정 |

## 8. 면접 Q&A

<details>
<summary>Q1. Shmoo plot 이 뭐고 어떻게 읽나요? 경계 모양에서 무엇을 알 수 있나요?</summary>

V×F (또는 V×T, F×T, 타이밍 strobe×V) 격자에서 같은 테스트의 PASS/FAIL 을 찍은 지도다. 경계가 주파수별 Vmin / 전압별 Fmax 이고, 기울기는 "주파수를 올리는 데 드는 전압 비용" 이다. OPP 점과 경계 사이 거리가 마진. 꼬리질문 "경계가 수직 벽이면?" → 주파수와 무관하게 특정 전압 아래에서 실패 = SRAM 비트셀 Vmin, 리텐션, 레벨 시프터처럼 지연이 아닌 메커니즘. "수평 벽이면?" → 전압과 무관한 주파수 한계 = PLL 잠금 범위, IO/테스터 타이밍 한계. 주파수를 낮춰도 안 고쳐지는 실패는 hold-time 위반을 의심한다 (hold 는 클럭 주기와 무관).

</details>

<details>
<summary>Q2. Vmin 을 빨리 찾으려면? 이진 탐색의 전제는 무엇이고 깨지면 어떻게 되나요?</summary>

[lo, hi] 에서 bracket 을 확인한 뒤 "lo=FAIL, hi=PASS" 불변식으로 반씩 줄인다. 350 mV 를 1 mV 해상도로 11 probe (선형이면 351). 전제는 **단조성**(V 를 올리면 PASS 가 유지)이다. 홀이 있으면 probe 가 홀에 떨어질 때 너무 높게(이 레슨 TT-03: 841 vs 실제 ~680), 홀이 probe 위에 있으면 홀을 못 보고 낮게 답한다. 꼬리질문 "그럼 어떻게?" → coarse shmoo(10~25 mV 격자)로 단조성과 이상을 먼저 보고, 경계 근처만 fine bisect. 혹은 위에서 내려오는 coarse 스캔으로 첫 FAIL 을 찾은 뒤 그 칸만 bisect(추가 연습 2) — strict Vmin 을 정직하게 준다.

</details>

<details>
<summary>Q3. 간헐 실패(intermittent)를 마진 문제와 어떻게 구분하나요?</summary>

같은 점을 N회 반복해 k/N 을 본다. 마진 문제는 경계 근처의 좁은 띠에서만 섞이고, 전압을 올리면 사라진다 (띠 폭 ≈ 지연 변동 / 지연의 전압 기울기, 여기선 ≤ ~40 mV). 경계에서 멀리 떨어진 곳(여기선 +200 mV)의 MIXED 는 전압으로 안 고쳐지는 실패 = 기능 버그(경합, CDC, 메타안정성, 특정 온도에서만 나는 SRAM 문제 등). 꼬리질문 "몇 번 반복해야 하나?" → 실패율 p 를 CL 로 잡으려면 n ≥ ln(1−CL)/ln(1−p) ≈ 3/p (95%). 5% 산발을 셀당 20회로 볼 확률은 64% 뿐이지만, 영역 전체(수십 셀)로 보면 거의 확실하다 — 그래서 셀이 아니라 **영역/열 단위로 묶어** 판정한다.

</details>

<details>
<summary>Q4. 1회 측정으로 찾은 Vmin 을 다시 한 번 돌려 PASS 했습니다. 믿어도 되나요?</summary>

아니다. 이진 탐색은 운 좋은 PASS 쪽으로 치우치므로 1회 측정 Vmin 은 거의 항상 노이즈 띠의 아래쪽이다 (데모: 99.6% 가 새 측정 2000회에서 FAIL). 그리고 같은 조건으로 한 번 더 돌리는 건 상관된 재측정이라 증거가 약하다 (레슨 모델에서는 같은 trial 을 재생해 11/11 PASS). 꼬리질문 "그럼 n=200 번 PASS 면?" → 95% 신뢰도로 p ≤ 1.5% 라는 뜻일 뿐이다. 실제로 robust 방식도 1000회 확인에서 2/11 이 FAIL 했다. 그래서 확인된 Vmin 에 guard band 를 더하고, 수명·IR drop·테스터 정확도를 guard band 에 넣는다.

</details>

<details>
<summary>Q5. 온도 역전(temperature inversion)이 뭐고, Vmin 특성화를 어느 온도에서 해야 하나요?</summary>

온도가 오르면 Vth 가 낮아져 빨라지고(오버드라이브 ↑), 이동도가 낮아져 느려진다. 고전압에선 이동도 효과가, 저전압(Vth 에 가까운 영역)에선 Vth 효과가 이겨서 **저전압에선 뜨거울수록 빠르다**. 그래서 최악 코너가 OPP 마다 바뀐다 — 이 레슨 데이터에서 500 MHz@720 mV 는 −40 °C, 900 MHz@975 mV 는 125 °C 가 최악. 결론: 모든 OPP 를 cold/room/hot 에서 본다. 꼬리질문 "양산에서 온도 3개는 비싼데?" → 특성화 데이터로 OPP 별 최악 온도를 정해 그 온도의 insertion 에서만 OPP 를 확인하고, 나머지는 guard band 로 덮는다.

</details>

<details>
<summary>Q6. guard band 는 어떻게 정하나요? 칩마다 다르게 할 수도 있나요?</summary>

구성 요소를 더한다: (1) 측정 통계(확인 n 에 따른 잔여 p, 노이즈 띠 폭), (2) 테스터/전원 정확도, (3) 테스트 환경과 실제 시스템의 IR drop · di/dt droop 차이 (실 워크로드가 테스트 패턴보다 전류 변화가 클 수 있음), (4) 온도 측정 오차, (5) 수명 열화(NBTI/HCI 로 Vmin 이 시간이 지나며 상승). 꼬리질문 "칩마다?" → AVS(adaptive voltage scaling): 양산 테스트에서 다이별 Vmin 이나 링 오실레이터 속도를 측정해 fuse 에 bin/전압을 기록하고, 부팅 시 PMIC 전압을 그에 맞춘다. 이 레슨의 SS-04 같은 느린 다이를 버리지 않고 더 높은 전압 bin 으로 살리는 방법이기도 하다.

</details>

<details>
<summary>Q7. 보드/시스템 레벨에서 shmoo 를 자동화한다면 루프 한 번에 무엇을 하나요? FAIL 뒤에는?</summary>

점 하나: PMIC 전압 설정(PMBus/I2C) → 안정화 대기 + ADC 로 실제 전압 확인 → PLL 재설정·잠금 대기(SIV-B09 의 timeout 헬퍼) → 테스트 실행(펌웨어 커널, 메모리 테스트, seed 기록) → 결과·온도·전압 실측값·seed 로그. FAIL 이 나면 칩이 hang 했을 수 있으므로 watchdog/타임아웃으로 빠져나와 **리셋/재초기화** 후 다음 점으로 가고, 주기적으로 안전한 "sanity point" 를 다시 측정해 칩이 열화되거나 상태가 오염되지 않았는지 본다. 꼬리질문 "리셋을 안 하면?" → 이전 FAIL 의 잔여 상태 때문에 뒤 점들이 연쇄로 FAIL 해서 shmoo 가 스윕 순서에 의존하게 된다. 결과 분석(클러스터링)은 SIV-M10, 실행 시퀀서는 SIV-M12.

</details>

## 9. 추가 연습

### 연습 1 — Fmax 이진 탐색 (고정 V)

같은 칩에서 V 를 고정하고 동작하는 최대 주파수를 1 MHz 해상도로 찾아라. PASS 는 F 에 대해 단조 감소다.

<details>
<summary>답 보기</summary>

불변식을 뒤집는다: `lo = PASS 확인, hi = FAIL 확인`, `while (hi - lo > res)`, 답은 `lo`. bracket: f_lo 에서도 FAIL 이면 −1, f_hi 에서도 PASS 면 f_hi. 노이즈 0 TT 칩 25 °C 에서 700~1000 mV (50 mV 간격) 7개 전압 모두 1 MHz 선형 스캔과 일치: 800 mV → 966 MHz, 1000 mV → 1326 MHz. Vmin 과 Fmax 는 같은 경계를 다른 축에서 자른 것이다 — 양산 speed binning 은 Fmax 쪽, 전력 최적화(DVFS 표)는 Vmin 쪽을 본다.

</details>

### 연습 2 — 홀이 probe 위에 있을 때: coarse(top-down) + fine

TT 칩 700 MHz 에 880–920 mV 홀을 넣고 1회 측정 bisect 와 비교하라. 위에서 10 mV 씩 내려오다 처음 FAIL 을 만나면 그 칸만 1 mV bisect 하는 `vmin_coarse_fine` 을 작성하라.

<details>
<summary>답 보기</summary>

bisect probe 는 1000 → 650 → 825 → 737 → 693 → ... 처럼 홀(880–920)보다 아래로만 내려가서 **홀을 못 보고 672 mV** 를 답한다 (10 runs). top-down 은 920 mV 에서 첫 FAIL 을 만나 [920, 930] 을 bisect 해 **921 mV** = strict Vmin (12 runs). 결함 없는 칩에서는 coarse+fine 이 1 mV 진실과 같다. 비용은 거의 같고, 답의 의미가 "홀이 없다면 맞는 값" 에서 "여기부터 위로는 확실히 되는 값" 으로 바뀐다.

</details>

### 연습 3 — 0 실패 신뢰도: 몇 번 돌려야 하나

`trials_needed(p, CL)`, `p_upper(n, CL)`, `detect_prob(p, n)` 을 `<math.h>` 로 작성하라.

<details>
<summary>답 보기</summary>

`n = ceil(ln(1−CL)/ln(1−p))`, `p_up = 1 − (1−CL)^(1/n)`, `detect = 1 − (1−p)^n`. 실행 결과: p=1% → 299회, 0.1% → 2995회, 5% → 59회 (95%). 200회 무실패 → p ≤ 1.4867% (≈ 3/200 = 1.5%). 5% 산발을 20회 안에 한 번이라도 볼 확률 64.15%. SIV-M06 의 "BER 1e-12 @95% → 3.0e12 비트" 와 같은 계산이다.

</details>

### 연습 4 — 두 shmoo 비교 (25 °C vs 125 °C)

두 shmoo 의 셀 분류가 바뀐 수, 열별 strict Vmin 변화, 가장 크게 나빠진 열을 구하라.

<details>
<summary>답 보기</summary>

셀별로 `cell_class` 를 비교하고 열별 `shmoo_vmin_col` 차이를 낸다 (한쪽만 −1 이면 9999 = "아예 못 감"). 노이즈 0 TT 칩: 바뀐 셀 42개, 700 MHz 열은 **−20 mV**(뜨거울 때 좋아짐, 온도 역전), 900 MHz 열은 **+20 mV**, 최악은 1200 MHz 열(25 °C 에선 되지만 125 °C 에선 1.0 V 로도 안 됨 → +9999). 펌웨어/보드 변경 전후, 번인(burn-in) 전후 비교에도 같은 함수를 쓴다.

</details>

### 연습 5 — 온도 역전 교차 전압

`delay(125 °C) ≥ delay(−40 °C)` 가 되는 최저 전압을 이진 탐색으로 찾고 선형 스캔과 비교하라.

<details>
<summary>답 보기</summary>

g(V) = delay_hot − delay_cold 의 부호가 바뀌는 점을 bisect (lo 에서 g<0, hi 에서 g≥0 인지 먼저 확인). TT 칩에서 **695 mV** (그 점에서 −40 °C 1284 ps, 125 °C 1285 ps), 선형 스캔과 같다. 이 아래 OPP 는 cold 가, 위 OPP 는 hot 이 최악이다. 실칩에서는 이 교차점(ZTC 근처)을 특성화 데이터로 찾아 OPP 별 최악 온도를 정한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice -lm && ./practice
```

실제 출력:

```text
P1 V=800mV Fmax=966MHz
P1 V=1000mV Fmax=1326MHz
P2 bisect=672mV (runs 10)  coarse+fine=921mV (runs 12)
P3 n(1%,95%)=299 n(0.1%,95%)=2995 n(5%,95%)=59 p_up(200)=1.4867% detect(5%,20)=64.15%
P4 changed=42 dVmin@500=0 @700=-20 @900=20 worst=1200MHz (+9999)
P5 TT crossover=695mV  d(-40)=1284 d(125)=1285 at 695mV
12/12 passed
```

(ASan/UBSan 빌드도 `12/12 passed`. macOS 에서는 libm 이 libSystem 에 들어 있어 `-lm` 없이도 링크되지만, Linux 에서는 필요하다.)

## 10. 핵심 정리

- shmoo = V×F 격자 × 셀당 N회 → PASS(`*`) / MIXED(`#`) / FAIL(`.`). CSV 에는 k/N 을 남긴다.
- **strict Vmin** = 위에서 내려와 PASS 가 끊기기 직전. "처음 PASS" 는 노이즈·홀에 속는다.
- bisect: bracket(NO_PASS / BELOW_RANGE) → 불변식 lo=FAIL, hi=PASS → `2 + ceil(log2(범위/res))` probe. 전제는 단조성.
- 1회 측정 Vmin 은 낙관적(데모 탈출률 99.6%). 반복 + **새 측정 확인** + step-up + **guard band** 로 0%.
- 0 실패 n회 → p ≤ −ln(1−CL)/n ≈ **3/n** (95%). BER 식과 같다.
- 경계에서 먼 MIXED = **산발(기능 버그)**, PASS 위의 FAIL = **홀(비단조)**, 깨끗하지만 밀린 지도 = **약한 다이(binning/AVS)**.
- 온도 역전: 저전압 OPP 는 cold, 고전압 OPP 는 hot 이 최악 → 모든 OPP × 모든 온도.
- 특성화는 전체 shmoo, 양산은 spot check / edge-tracking. 테스트 시간 = 실행 횟수 × 점당 시간.

## 참고자료

- T. Sakurai, A. R. Newton, "Alpha-power law MOSFET model and its applications to CMOS inverter delay and other formulas", IEEE JSSC 25(2), 1990
- J. A. Hanley, A. Lippman-Hand, "If nothing goes wrong, is everything all right? Interpreting zero numerators", JAMA 249(13), 1983 — rule of three
- [Linux `Documentation/devicetree/bindings/opp/opp-v2.yaml` — operating-points-v2 (opp-hz, opp-microvolt)](https://github.com/torvalds/linux/blob/master/Documentation/devicetree/bindings/opp/opp-v2.yaml)
- PMBus Power System Management Protocol Specification, Part II — VOUT_COMMAND, VOUT_MARGIN_HIGH/LOW
- Advantest SmarTest / Teradyne IG-XL 사용자 문서 — shmoo 도구 (ATE 쪽 구현 예)
- [splitmix64 (Steele, Lea, Flood, "Fast splittable pseudorandom number generators", OOPSLA 2014)](https://doi.org/10.1145/2660193.2660195) — counter-based 난수의 mixer
- 관련 레슨: SIV-B09 (timeout 헬퍼, PLL lock 대기), SIV-B14 (리포트·exit code), SIV-M01 (ATE/SLT/시스템 역할), SIV-M06 (BER 신뢰구간 — 같은 식), SIV-M08 (PVT 코너 매트릭스·시간 예산), SIV-M10 (실패 triage), SIV-M11 (bisect 일반화·flaky), SIV-M12 (시퀀서)
