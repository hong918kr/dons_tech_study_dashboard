---
id: SIV-A11
time: 120분
updated: 2026-11-08
---
# SIV-A11 클럭·전원 도메인 검증

## 학습 목표

- 클럭 게이트 · 전원 스위치 · isolation · retention 이 각각 **어떤 사고를 막는지**, 한 스텝이 빠지거나 순서가 바뀌면 칩에 무슨 일이 생기는지 말할 수 있다.
- 게이트된 도메인 접근, 전원 사이클 후 reset vs retention 값, isolation 해제 순서, 클럭 enable 직후 첫 접근(N cycle settle)을 **검사하는 테스트를 작성** 하고, 각 테스트가 잡는 결함과 잡지 못하는 결함을 구별한다.
- 시퀀스 스텝의 순열 전체(내림 720, 올림 120, 왕복 86,400)를 **물리 모델 + safety oracle** 로 돌려 안전한 순서를 전수 조사하고, 그 결과에서 필요한 선후 제약(부분순서)을 끌어낸다.
- "순서 전수 조사" 가 **못 보는 것** — 소프트웨어로 관측되지 않는 위반, 패턴이 reset 값과 같을 때의 retention skip, 순서는 맞고 시간이 틀린 경우 — 을 실측 표로 설명한다.
- 20가지 결함(하드웨어 14 + 잘못된 시퀀스 6)을 주입해, 첫 실패의 시험·단계·원인으로 디버그 엔지니어가 다음 행동을 고를 수 있는 리포트를 낸다.

## 1. 문제

전원 도메인이 있는 SoC 는 first silicon 에서 **가장 먼저 벽돌이 되는 부분** 이고, Rivian·NVIDIA·Apple 류 실리콘 검증 면접에서는 "전원 도메인을 어떻게 검증하겠느냐, 순서가 틀리면 무슨 증상이 나느냐" 로 그대로 나온다.

요즘 칩은 쓰지 않는 블록의 클럭을 끄고(clock gating), 더 오래 쉬면 전원 자체를 끈다(power gating). 그런데 전원이 꺼진 쪽에서 나온 신호는 아무 값도 아니다(floating, 시뮬레이션의 X) — 이 신호가 살아 있는 쪽으로 들어가면 살아 있는 쪽까지 망가진다. 그래서 경계에 **isolation 셀** (꺼진 쪽 출력을 정해진 값으로 고정 = clamp) 을 둔다. 다시 켤 때 빨리 복귀하려고 레지스터 일부를 **retention 플롭** (별도 전원의 그림자 래치에 값을 저장) 으로 만든다. 이 넷이 맞물려 동작하려면 **순서** 가 맞아야 하고, 그 순서를 잘못 지키는 것이 실리콘 버그와 펌웨어 버그 양쪽의 흔한 원인이다.

호스트(맥)에는 실리콘이 없으므로 mock SoC 위에서 한다. 가상의 SoC **SENS-A**: AON(항상 켜짐) + 세 도메인.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">SENS-A 도메인 구성과 AON 경계</text>
  <rect x="10" y="40" width="720" height="60" class="f-mut"/>
  <text x="22" y="62" fill="currentColor" font-weight="bold">AON (항상 켜짐)</text>
  <text x="22" y="82" fill="currentColor" font-size="12">PMU 시퀀서 · PD_STAT(PGOOD/CLK_ACK/ISO/RST) · 포트 샘플 레지스터 · 버스 fabric</text>
  <rect x="10" y="130" width="230" height="170" class="f-acc"/>
  <text x="125" y="150" fill="currentColor" font-weight="bold" text-anchor="middle">ISP</text>
  <text x="125" y="168" fill="currentColor" text-anchor="middle" font-size="12">clock gate + 전원 스위치 + retention</text>
  <rect x="26" y="180" width="94" height="36" class="f-mut"/><text x="73" y="203" fill="currentColor" text-anchor="middle" font-size="12">CTX (non-ret)</text>
  <rect x="130" y="180" width="94" height="36" class="f-ok"/><text x="177" y="203" fill="currentColor" text-anchor="middle" font-size="12">CFG (ret)</text>
  <rect x="130" y="226" width="94" height="36" class="f-ok"/><text x="177" y="249" fill="currentColor" text-anchor="middle" font-size="12">TUNE (ret)</text>
  <rect x="26" y="226" width="94" height="36" class="f-mut"/><text x="73" y="249" fill="currentColor" text-anchor="middle" font-size="12">ID (RO)</text>
  <text x="125" y="284" fill="currentColor" text-anchor="middle" font-size="12">shadow 래치 = 별도 전원</text>
  <rect x="255" y="130" width="230" height="170" class="f-acc"/>
  <text x="370" y="150" fill="currentColor" font-weight="bold" text-anchor="middle">NPU</text>
  <text x="370" y="168" fill="currentColor" text-anchor="middle" font-size="12">clock gate + 전원 스위치 (retention 없음)</text>
  <text x="370" y="210" fill="currentColor" text-anchor="middle" font-size="12">켜지면 항상 reset 값부터</text>
  <rect x="500" y="130" width="230" height="170" class="f-warn"/>
  <text x="615" y="150" fill="currentColor" font-weight="bold" text-anchor="middle">PERI</text>
  <text x="615" y="168" fill="currentColor" text-anchor="middle" font-size="12">clock gate 만 (전원 항상 켜짐)</text>
  <text x="615" y="210" fill="currentColor" text-anchor="middle" font-size="12">값은 보존, 접근만 막힘</text>
  <line x1="125" y1="130" x2="125" y2="100" stroke="currentColor"/>
  <rect x="60" y="105" width="130" height="18" class="f-warn"/>
  <text x="125" y="118" fill="currentColor" text-anchor="middle" font-size="12">isolation clamp = 0x00</text>
  <text x="370" y="118" fill="currentColor" text-anchor="middle" font-size="12">(동일)</text>
  <line x1="370" y1="130" x2="370" y2="100" stroke="currentColor"/>
</svg>
```

레지스터 (도메인마다 같은 배치, 값은 mock 스펙):

| off | 이름 | 접근 | reset 값 | 전원 사이클 뒤 |
|---|---|---|---|---|
| `0x00` | `ID` | RO | 도메인 ID | 항상 ID |
| `0x04` | `CTX` | RW | `0xA5A50000` | **항상 reset 값** (non-retention) |
| `0x08` | `CFG` | RW | `0x00000010` | ISP: 저장값(retention) / NPU: reset |
| `0x0C` | `TUNE` | RW | `0x000000FF` | ISP: 저장값(retention) / NPU: reset |

`CFG[7:0]` 이 AON 으로 나가는 출력 포트다. 이 mock 의 스펙:

| 도메인 상태 | 버스 응답 | 읽기 데이터 |
|---|---|---|
| 정상, settle 완료 | `OKAY` | 레지스터 값 |
| clock gate 꺼짐 / reset 중 / settle 창 안 (CLK_ON 뒤 `T_SETTLE` cycle 미만) | `SLVERR` | `0xDEADBEEF` |
| 전원 꺼짐 (rail good 전) | `DECERR` | `0xDEADBEEF` |

(SLVERR/DECERR 는 AXI 의 응답 이름을 빌렸을 뿐, 어떤 응답을 낼지는 SoC 스펙이 정한다. **응답을 안 주고 멈추는(hang) 칩도 있다** — 그래서 fabric timeout 이 중요하다.)

`T_SETTLE = 4`, `T_RAMP = 8` cycle 은 대표값. 대표 시퀀스 (이 레슨의 스펙. **실제 순서는 그 SoC 의 전원 설계서 / UPF(IEEE 1801) power intent 가 정한다** — 설계마다 다르다):

```text
내림: QUIESCE → CLK_OFF → ISO_ON → SAVE → RST_ON → PWR_OFF
올림: PWR_ON(+rail good) → RST_OFF → RESTORE → CLK_ON(+settle) → ISO_OFF
```

SIV-B02 의 mock 과 SIV-B09 의 가짜 시계를 축약해 넣었다. RTL 수준 low-power 검증(UPF-aware 시뮬레이션, isolation/retention SVA)은 RTLDV-A08 소관이고, 여기는 **칩이 나온 뒤 펌웨어/테스트 코드 관점의 시험** 이다.

### Part A — 도메인 접근·전원 사이클·isolation·첫 접근

- `check_gated_access(d, f)` — 클럭을 끄고 모든 레지스터 읽기가 `SLVERR` + `BAD_DATA`, 쓰기도 `SLVERR` 이고, 다시 켰을 때 **게이트 중 쓰기가 들어가지 않았는지** 확인.
- `check_power_cycle(d, cfg_pat, tune_pat, f)` — 패턴을 쓰고 내림, 꺼진 동안 접근은 `DECERR`, 올림, 그 뒤 `ID`/`CTX` 는 reset 값, `CFG`/`TUNE` 은 retention 도메인이면 패턴, 아니면 reset 값.
- `check_iso_trace(d, cfg_pat, f)` — 스텝마다 포트를 샘플링: isolation 이 켜져 있으면 clamp, 꺼져 있으면 기능 값(올림 후 최종값).
- `check_first_access(d, f)` — CLK_ON 뒤 경과 cycle k = 0..T_SETTLE+1 에 대해 ack·읽기·쓰기의 응답·값 검사 (k < N 이면 에러이고 값이 들어가지 않아야 한다).

### Part B — 순서 전수 조사와 한계

- `next_perm` + `enumerate_orders(base, n, is_down, pmask, e)` — 내림 6 스텝의 720 순서, 올림 5 스텝의 120 순서를 **물리 모델을 돌려서** 판정한다. 결과: 안전 순서 목록, 위반 종류별 개수, 모든 안전 순서에 공통인 선후 관계 `prec[i][j]`, 그리고 위반 순서 중 **관측 가능성** (최종 readback 으로 보이나 / 포트 샘플로 보이나 / 둘 다 안 보이나).
- 측정할 한계: 소프트웨어로 안 보이는 위반 / X 가 1비트 포트에서 우연히 맞는 경우 / 대기 시간 부족(순서는 맞음) / retention 패턴이 reset 값일 때 skip 이 가려지는 것.

### Part C — 결함 주입 캠페인과 리포트

- `classify(f)` — 실패 기록(단계 + 기대/실제 + 응답 + iso 상태)으로 원인 후보를 고른다.
- 주어진 `run_suite()` 는 도메인별 15 시험을 돌린다. 하드웨어 결함 14 + 시퀀스 결함 6 각각의 **첫 실패의 시험·단계·원인** 을 표와 대조, `seq:iso_early` 는 **레지스터 시험을 전부 통과하고 포트 샘플에서만 잡히는지** 를 CHECK 하고 리포트를 찍는다.

## 2. Problem (English)

> **Clock and power domain validation.**
>
> A mock SoC has an always-on domain plus three functional domains: `ISP` (clock gate, power switch, retention registers), `NPU` (clock gate, power switch, no retention), `PERI` (clock gate only). Each domain has registers `ID`(RO), `CTX`(RW, non-retained), `CFG`, `TUNE` (RW, retained in ISP). `CFG[7:0]` drives an output port observed from the always-on side through an isolation clamp (clamp value `0x00`).
>
> Representative sequences: power down `QUIESCE, CLK_OFF, ISO_ON, SAVE, RST_ON, PWR_OFF`; power up `PWR_ON (wait rail good), RST_OFF, RESTORE, CLK_ON (wait ack), ISO_OFF`. A clock-gated, in-reset, or not-yet-settled domain returns `SLVERR` and data `0xDEADBEEF`; a powered-off domain returns `DECERR`. Writes to a gated domain must have no effect.
>
> 1. `bool check_gated_access(int d, fail_t *f)` — with the clock gated, every read returns SLVERR + 0xDEADBEEF; a write returns SLVERR and has no effect after the clock returns.
> 2. `bool check_power_cycle(int d, uint32_t cfg, uint32_t tune, fail_t *f)` — power cycle the domain; while off, accesses return DECERR; afterwards ID and CTX hold reset values; CFG/TUNE hold the written values if the domain has retention, else reset values.
> 3. `bool check_iso_trace(int d, uint32_t cfg, fail_t *f)` — after every sequence step, sample the port: clamp while isolation is on, functional value otherwise.
> 4. `bool check_first_access(int d, fail_t *f)` — for elapsed cycles k = 0..T_SETTLE+1 after CLK_ON, the ack must equal `k >= T_SETTLE`, reads/writes must return the error response and not take effect before settle, and must work after it.
> 5. `bool next_perm(int *a, int n)` and `void enumerate_orders(const step_t *base, int n, bool is_down, uint8_t pmask, enum_t *e)` — run all permutations through the physical model and the safety oracle; report safe orders, violation histogram, and observability of the unsafe orders.
> 6. `int classify(const fail_t *f)` — map a failure record to a probable root cause.
>
> Example: if the SAVE pulse is ignored, `check_power_cycle` on `ISP` with a pattern must fail at step `RET`, register `CFG`, act = the reset value `0x00000010`, cause `RET_LOST`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 깨짐 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

실제로 돌린 결과 (앞부분과 끝):

```text
FAIL starter.c:560  check_gated_access(doms[i], &f)
FAIL starter.c:561  check_first_access(doms[i], &f)
FAIL starter.c:560  check_gated_access(doms[i], &f)
FAIL starter.c:561  check_first_access(doms[i], &f)
...
FAIL starter.c:715  S.n == 1 && S.log[0].dom == D_NPU && count_cause(&S, C_CROSS_DOMAIN) == 1
11/89 passed
```

주어진 것: 도메인 물리 모델 (`pmu_apply`, `soc_tick`, 포트/버스 접근), `fault_t`, `pd_step`, `run_steps`, `check_cross`, `oracle_roundtrip`, `run_suite`, 시퀀스 상수, 표와 리포트 출력. 채울 곳은 `check_gated_access`, `check_power_cycle`, `check_iso_trace`, `check_first_access`, `next_perm`, `enumerate_orders`, `classify` 일곱 개. 통과하는 11개는 mock sanity 와 스텁이 우연히 맞는 음성 CHECK 다.

## 4. 힌트

<details>
<summary>힌트 1 — 게이트 접근 시험의 마지막 단계가 핵심이다</summary>

클럭이 꺼진 동안 쓰기 응답이 SLVERR 이어도, 그 값이 **실제로 레지스터에 들어갔는지** 는 다시 켠 뒤 읽어 봐야 안다. 쓰기 전 패턴을 `pat` 으로 넣고, 게이트 중 `~pat` 을 써 보고, 켠 뒤 `pat` 이 그대로인지 비교한다. 에러 응답만 보면 `gate_wr_leak` 을 놓친다.

</details>

<details>
<summary>힌트 2 — 전원 사이클의 기대값은 도메인 능력에 따라 다르다</summary>

`DOM[d].has_ret` 로 갈린다. `CTX` 는 retention 이 없으므로 항상 reset 값, `CFG`/`TUNE` 은 ISP 에서만 패턴. 비교 순서는 ID → CTX → CFG → TUNE. `seq_of(d)` 가 도메인에 맞는 시퀀스를 준다 (ISP 는 시험이 교체할 수 있는 `g_seq_ret`).

</details>

<details>
<summary>힌트 3 — 포트 샘플은 "지금 isolation 상태" 로 기대값을 정한다</summary>

매 스텝 뒤 `pmu_status(d) & STAT_ISO` 를 읽어, 켜져 있으면 기대값 = clamp(`0x00`), 꺼져 있으면 = 기능 값. 기능 값은 내림에서는 `cfg_pat` 하위 바이트(hold), 올림의 ISO_OFF 뒤에는 최종 값 — retention 도메인이면 `hold`, 아니면 `REG[R_CFG].rst` 하위 바이트. 올림 도중 ISO_OFF 를 지났는지 `released` 플래그로 추적한다 (시퀀스가 틀릴 수 있으니 고정 인덱스로 하지 않는다).

</details>

<details>
<summary>힌트 4 — first_access 는 k 경계에서 ack 와 접근을 함께 본다</summary>

`pmu_apply(d, S_CLK_OFF); pmu_apply(d, S_CLK_ON); soc_tick(k);` 로 경과 시간을 정확히 k 로 맞춘다. `k >= T_SETTLE` 인 경우에만 `STAT_CLK_ACK` 가 1 이어야 하고, 읽기는 k < N 에서 SLVERR + BAD_DATA, k ≥ N 에서 OKAY + 패턴. 쓰기도 같은 방식으로 한 번 더 — 쓰기 전 게이트를 다시 toggle 해서 k 를 재설정한다. 잊기 쉬운 것: k < N 에서 쓰기는 **값이 안 들어가야** 한다 (`K_FIRST_WR`).

</details>

<details>
<summary>힌트 5 — enumerate 의 prec 갱신</summary>

처음에 `prec[i][j]=true` 로 다 켜고, 안전 순서를 볼 때마다 "지금 순서에서 j 번째 위치의 스텝이 i 번째 위치 앞에 있으면 prec 를 끈다": `for 위치 p<q: prec[idx[q]][idx[p]] = false`. 끝에 남은 true 만 "모든 안전 순서에서 지켜진 선후" 다.

</details>

<details>
<summary>힌트 6 — classify 는 실패 단계 + 값의 모양으로 나눈다</summary>

`K_RET` 에서 `act == reset` 이면 SAVE/RESTORE 가 안 일어난 것(`RET_LOST`), `exp^act` 가 1비트면 `RET_BIT`, 그 외는 `RET_CORRUPT`. `K_CTX_RESET` 에서 act 가 시험이 쓴 패턴이면 reset 이 안 먹은 것(`RST_KEPT`), 그 외 쓰레기면 `RST_CORRUPT`. 포트 쪽은 `f->iso_on` 이 clamp 값 문제(`CLAMP_VALUE`)인지 일찍 풀린 것(`ISO_EARLY`)인지 가른다.

</details>

## 5. 풀이

### Part A — 네 가지 시험이 각각 막는 사고

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a11m" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">ISP 전원 사이클 타임라인 (대표 순서 — 실제 순서는 SoC 전원 스펙)</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="66">CLK</text><text x="10" y="106">ISO</text><text x="10" y="146">SAVE/RESTORE</text><text x="10" y="186">RST</text><text x="10" y="226">PWR</text><text x="10" y="266">AON 포트</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M110,48 L180,48 L180,70 L570,70 L570,48 L700,48" /><text x="575" y="42" stroke="none" fill="currentColor" font-size="12">CLK_ON (+settle)</text>
    <path d="M110,110 L240,110 L240,88 L640,88 L640,110 L700,110" /><text x="648" y="84" stroke="none" fill="currentColor" font-size="12">ISO_OFF</text>
    <path d="M110,150 L330,150 L330,130 L350,130 L350,150 L520,150 L520,130 L540,130 L540,150 L700,150" />
    <path d="M110,190 L385,190 L385,170 L490,170 L490,190 L700,190" />
    <path d="M110,208 L405,208 L405,230 L450,230 L450,208 L700,208" />
  </g>
  <rect x="405" y="232" width="45" height="16" class="f-warn"/>
  <text x="427" y="245" fill="currentColor" text-anchor="middle" font-size="12">꺼짐</text>
  <rect x="110" y="250" width="130" height="22" class="f-ok"/><text x="175" y="266" fill="currentColor" text-anchor="middle" font-size="12">CFG 기능 값</text>
  <rect x="240" y="250" width="400" height="22" class="f-mut"/><text x="440" y="266" fill="currentColor" text-anchor="middle" font-size="12">clamp = 0x00 (안전)</text>
  <rect x="640" y="250" width="60" height="22" class="f-ok"/><text x="670" y="266" fill="currentColor" text-anchor="middle" font-size="12">복귀 값</text>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="145" y="300">① 게이트 접근</text><text x="145" y="316">SLVERR</text>
    <text x="340" y="300">② SAVE</text><text x="340" y="316">클럭 정지 뒤</text>
    <text x="440" y="300">③ 꺼진 동안</text><text x="440" y="316">DECERR</text>
    <text x="530" y="300">④ RESTORE</text><text x="530" y="316">reset 해제 뒤</text>
    <text x="650" y="300">⑤ 첫 접근</text><text x="650" y="316">settle 뒤</text>
  </g>
  <text x="10" y="346" fill="currentColor">핵심: ISO 는 전원이 없어지기 전에 켜지고, 값이 복귀한 뒤에야 풀린다. SAVE 는 클럭이 멈춘 뒤, RESTORE 는 reset 이 풀린 뒤.</text>
</svg>
```

**① 게이트된 접근 → 버스 에러.** 왜 시험하나: 클럭이 없는 블록은 응답 로직도 멈춰 있다. 그대로 두면 CPU 가 응답을 영원히 기다리는 hang — first-silicon 에서 JTAG 로도 잡기 어려운 최악의 증상이다. 그래서 좋은 설계는 fabric 또는 도메인 경계 브리지가 대신 에러를 돌려준다. 시험은 응답뿐 아니라 **값이 안 샌다** (`gate_wr_leak`) 와 **데이터 버스에 고정 패턴** (`0xDEADBEEF`, 정상 값을 읽었다고 착각하지 않게) 까지 본다. 실패 예 (실제 출력):

```text
[hw:gate_ok] failures=3/15 first: FAIL ISP gated step=GATED_RESP reg=ID off=0x000 exp=SLVERR act=OKAY cause=GATE_NO_ERR
[hw:gate_hang] failures=3/15 first: FAIL ISP gated step=GATED_RESP reg=ID off=0x000 exp=SLVERR act=HANG cause=GATE_HANG
[hw:gate_wr_leak] failures=3/15 first: FAIL ISP gated step=GATED_WR_LEAK reg=CTX off=0x004 exp=0x1234ABCD act=0xEDCB5432 diff=0xFFFFFFFF bits=[0,1,2,3,4,5,+26] cause=GATE_WR_LEAK
```

mock 은 HANG 을 즉시 돌려주지만 실제 칩에서는 **테스트가 안 돌아온다**. 그래서 진짜 테스트는 버스 타임아웃이 켜진 환경에서 돌리거나, 접근을 CPU 가 아닌 디버그 포트/다른 master 로 시도한다.

**② 전원 사이클 뒤 reset vs retention.** 전원을 끄면 모든 플롭 값이 날아가고, retention 플롭만 **그림자 래치** 에 값을 들고 있다가 RESTORE 때 돌려준다. 시험은 양쪽을 다 봐야 한다.

| 레지스터 | 기대 | 이게 틀리면 |
|---|---|---|
| `CTX` (non-ret) | reset 값 | 시험이 쓴 값이 살아 있으면 reset 미적용/ret 플롭으로 잘못 합성 (`ctx_retained`) — 드라이버가 "초기화됐다" 고 가정하다 터진다 |
| `CFG`, `TUNE` (ret) | 쓴 패턴 | 저장/복원 경로 고장, 혹은 시퀀스에서 SAVE/RESTORE 누락 |
| `ID` | 도메인 ID | 도메인 reset 해제 또는 디코드 문제 (SIV-B03 의 reset-value 시험과 같은 원리) |

`CFG_A = 0xA5A5C33C` 처럼 **reset 값과 다르고, 비트마다 0/1 이 섞인 패턴** 을 쓰는 이유는 Part B 의 retention 패턴 실측 참고.

전원이 꺼진 동안의 접근은 SLVERR 이 아니라 `DECERR` — "이 주소에 지금 응답할 수 있는 slave 가 없다" 는 뜻이라 게이트된 클럭과 원인을 구별할 수 있다 (이 mock 의 스펙이다. 어떻게 응답할지는 SoC 스펙).

**③ isolation 해제 순서.** 스텝마다 포트를 샘플한다. 내림에서는 ISO_ON 전까지 hold, 이후 clamp. 올림에서는 ISO_OFF 까지 clamp, 그 뒤 **최종 값(복귀 값)** 이어야 한다.

왜 ISO_OFF 가 RESTORE 뒤여야 하나: ISO 를 먼저 풀면 AON 은 아직 reset 값이거나 복원 전 값인 포트를 보고, RESTORE 순간 값이 바뀐다 (글리치). 그 포트가 AON 의 인터럽트 라인이나 enable 이면 사고다. 실측 (좋은 모델이 통과, 결함 모델이 잡힘):

```text
[seq:iso_early] failures=1/15 first: FAIL ISP iso step=PORT_OPEN reg=PORT off=0x000 exp=0x0000003C act=0x00000010 diff=0x0000002C bits=[2,3,5] cause=ISO_EARLY
```

`seq:iso_early` 는 **이 시험에서만 잡힌다** (failures = 1/15). 레지스터 값은 멀쩡하기 때문이다.

**④ 첫 접근 (N cycle settle).** 클럭 게이트를 열어도 클럭 트리와 CDC(clock domain crossing) 동기화기가 안정될 때까지 몇 cycle 이 필요하다. 흔한 실제 사례: STM32 계열 errata 의 "RCC 주변장치 클럭 enable 직후 레지스터 접근은 지연이 필요" — 이 때문에 드라이버들이 enable 레지스터를 **읽어 되돌려 보는 코드** (또는 DSB)를 넣는다. 시험은 k = 0..N+1 의 경계를 한 cycle 단위로 긁는다:

| k (CLK_ON 뒤 경과) | 0 | 1 | 2 | 3 | **4 = N** | 5 |
|---|---|---|---|---|---|---|
| `CLK_ACK` | 0 | 0 | 0 | 0 | **1** | 1 |
| 읽기 응답 | SLVERR | SLVERR | SLVERR | SLVERR | **OKAY** | OKAY |
| 쓰기 후 값 | 안 들어감 | 안 들어감 | 안 들어감 | 안 들어감 | **들어감** | 들어감 |

두 결함: `settle_no_err` (창 안에서 OKAY + 쓰레기 — 가장 위험), `ack_early` (ack 는 k=1 에서 1 인데 실제 안정은 N — 드라이버가 믿고 접근하면 터진다). `ack_early` 는 15 시험 중 12 개가 실패한다 — **ack 는 모든 시험의 기반** 이라 파급이 크고, 그래서 Part C 스위트는 `first` 시험을 먼저 돌려 첫 실패가 근본 원인(`ACK`)이 되게 한다.

### Part B — 순서를 전수 조사하면 보이는 것과 안 보이는 것

왜 순열 전수인가: 시퀀스는 스텝 몇 개짜리 짧은 프로그램이고, 잘못된 순서의 대부분은 **한 스텝의 위치가 틀린 것** 이다. 스텝이 6개면 720개뿐이니 직접 다 돌려 본다. 판정은 스텝 규칙표가 아니라 **물리 모델**(값 변화 + 모니터)이 낸다 — 규칙표를 코드로 옮겨서 다시 그 규칙표로 검사하면 아무것도 안 보이기 때문이다.

모델의 물리 규칙 (`pmu_apply` 의 동작을 그대로):

| 상황 | 결과 (위반 비트) |
|---|---|
| CLK_OFF/ISO_ON/SAVE/RST_ON/PWR_OFF 를 트랜잭션 진행 중에 | `UNQUIESCED` (데이터는 안 깨지지만 프로토콜 위반) |
| 도메인이 꺼졌거나 값이 정의 안 됐는데 isolation 없음 | `X_CROSS` (AON 으로 쓰레기 값) |
| isolation 이 꺼진 채 포트 값이 변함 | `ISO_GLITCH` |
| SAVE 를 클럭이 도는 중에 | `SAVE_RACE` (그림자에 잘못된 값) |
| SAVE 를 reset/전원 없이 | 값이 reset 이거나 저장 안 됨 → 나중에 `RET_LOST` |
| RST_OFF 를 클럭이 도는 중에 | `RECOVERY` (reset 해제 타이밍 위반 → CTX 쓰레기) |
| RESTORE 를 reset 중/전원 없음/클럭 중 | 효과 없음(reset 이 이김) 또는 `RESTORE_RACE` |
| rail good 때 reset 이 없음 | `POWERUP_X` (값 정의 안 됨) |
| 왕복 뒤 CFG/TUNE ≠ 쓴 값 | `RET_LOST` · CTX ≠ reset → `RST_BAD` |

실측 결과 (solution 을 실제로 돌린 출력):

```text
[B] down: 720 orders, safe=6 | up: 120 orders, safe=2
  safe down: QUIESCE > CLK_OFF > ISO_ON > SAVE > RST_ON > PWR_OFF
  safe down: QUIESCE > CLK_OFF > ISO_ON > SAVE > PWR_OFF > RST_ON
  safe down: QUIESCE > CLK_OFF > SAVE > ISO_ON > RST_ON > PWR_OFF
  safe down: QUIESCE > CLK_OFF > SAVE > ISO_ON > PWR_OFF > RST_ON
  safe down: QUIESCE > ISO_ON > CLK_OFF > SAVE > RST_ON > PWR_OFF
  safe down: QUIESCE > ISO_ON > CLK_OFF > SAVE > PWR_OFF > RST_ON
  safe up:   PWR_ON > RST_OFF > RESTORE > CLK_ON > ISO_OFF
  safe up:   PWR_ON > RST_OFF > RESTORE > ISO_OFF > CLK_ON
  violations down: UNQUIESCED=600 X_CROSS=360 ISO_GLITCH=240 SAVE_RACE=240 RECOVERY=0 RESTORE_RACE=0 POWERUP_X=0 RET_LOST=660 RST_BAD=0 PORT_BAD=660
  violations up:   UNQUIESCED=0 X_CROSS=89 ISO_GLITCH=3 SAVE_RACE=0 RECOVERY=60 RESTORE_RACE=30 POWERUP_X=60 RET_LOST=110 RST_BAD=100 PORT_BAD=110
  cross product: 86400 pairs, safe=12 (= 6 x 2)
```

읽는 법:

- **내림은 6개, 올림은 2개만 안전** 하다. 대표 시퀀스는 그 안에 있다.
- `CLK_ON` 과 `ISO_OFF` 의 순서는 이 물리 모델에서 자유다 (올림의 안전 순서가 2개인 이유). 대표 시퀀스는 그중 하나를 고른 것이고, 다른 설계는 ISO_OFF 를 클럭 재개 전에 둔다. **안전 집합이 하나의 순서가 아니라 부분순서** 라는 것이 전수 조사의 핵심 결론이다. 설계 스펙이 그중 하나를 고른다.
- 숫자로 확인되는 것: `X_CROSS`(내림)=360 = "ISO_ON 이 PWR_OFF 뒤" 인 순서 = 정확히 절반(720/2). `UNQUIESCED`=600 = QUIESCE 가 첫 스텝이 아닌 순서 전부(720−120). 이렇게 계산으로 예측되는 값이 나오면 oracle 자체를 믿어도 된다는 신호다(oracle 의 self-check).
- 내림 × 올림 86,400 왕복에서 안전 12 = 6 × 2. 곱이 된다는 것은 **두 단계가 OFF 상태 하나로만 이어져 독립적** 이라는 뜻이다 (내림 순서가 올림에 영향을 주는 유일한 통로가 shadow 값과 OFF 상태 하나뿐이다).

전체 순서에서 끌어낸 부분순서 (`prec` 결과, 모든 안전 순서에서 지켜진 쌍):

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a11p" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="16" fill="currentColor" font-weight="bold" font-size="13">전수 조사로 끌어낸 선후 제약 (안전 순서 전부에서 지켜지는 관계만)</text>
  <g fill="currentColor" text-anchor="middle">
    <rect x="10" y="30" width="80" height="32" class="f-acc"/><text x="50" y="50">QUIESCE</text>
    <rect x="150" y="30" width="80" height="32" class="f-mut"/><text x="190" y="50">CLK_OFF</text>
    <rect x="290" y="30" width="60" height="32" class="f-ok"/><text x="320" y="50">SAVE</text>
    <rect x="150" y="80" width="80" height="32" class="f-warn"/><text x="190" y="100">ISO_ON</text>
    <rect x="410" y="30" width="70" height="32" class="f-mut"/><text x="445" y="50">RST_ON</text>
    <rect x="410" y="80" width="80" height="32" class="f-mut"/><text x="450" y="100">PWR_OFF</text>
  </g>
  <g stroke="currentColor" fill="none">
    <line x1="90" y1="46" x2="148" y2="46" marker-end="url(#a11p)"/><line x1="90" y1="56" x2="148" y2="92" marker-end="url(#a11p)"/>
    <line x1="230" y1="46" x2="288" y2="46" marker-end="url(#a11p)"/><line x1="350" y1="46" x2="408" y2="46" marker-end="url(#a11p)"/>
    <line x1="350" y1="56" x2="408" y2="92" marker-end="url(#a11p)"/><line x1="230" y1="92" x2="408" y2="52" marker-end="url(#a11p)"/><line x1="230" y1="100" x2="408" y2="98" marker-end="url(#a11p)"/>
  </g>
  <text x="10" y="134" fill="currentColor">내림: QUIESCE 가 먼저, CLK_OFF 뒤 SAVE, SAVE 와 ISO_ON 이 RST_ON/PWR_OFF 앞. CLK_OFF↔ISO_ON, RST_ON↔PWR_OFF 는 자유.</text>
  <text x="10" y="158" fill="currentColor">올림: PWR_ON → RST_OFF → RESTORE → {CLK_ON, ISO_OFF}. CLK_ON↔ISO_OFF 는 자유 — 스펙이 고른다.</text>
  <text x="10" y="182" fill="currentColor">다이어그램은 전이 축약 — 코드의 prec[][] 는 간접 쌍(예: QUIESCE &lt; SAVE)까지 전부 true.</text>
</svg>
```

이 제약의 **이유** 를 말할 수 있어야 한다: SAVE 가 CLK_OFF 뒤인 것은 클럭 edge 와 경합하면 잘못된 값을 저장하기 때문이고, ISO_ON 이 RST_ON/PWR_OFF 앞인 것은 AON 이 reset 값·X 를 보지 않게 하기 위해서이고, 올림의 RST_OFF 가 RESTORE 앞인 것은 reset 이 살아 있는 동안 복원하면 reset 이 이기기 때문이다.

#### 한계 — 이 방법이 못 보는 것 (전부 실측)

**1. 소프트웨어로 안 보이는 위반 (관측 가능성).** 물리 모델(ground truth)이 위반이라고 한 순서 중, 실제 시험이 볼 수 있는 것:

```text
  observability down: unsafe=714 readback=660 port8=684 either=684 monitor_only=30
  observability up:   unsafe=118 readback=115 port8=116 either=118 monitor_only=0
```

내림의 `monitor_only=30` 은 UNQUIESCED 만 위반인 순서들이다 — 트랜잭션 중에 클럭을 멈췄는데 레지스터 값이 안 깨지니 **레지스터 readback 으로도, 포트 샘플로도 안 보인다**. 실제 칩이라면 NoC 타임아웃·유실 트랜잭션으로 일부만 간접적으로 드러난다. 그래서 프로토콜 위반은 pre-silicon(UPF-aware 시뮬레이션 + assertion, RTLDV-A08)이 맡고, post-silicon 은 "조건을 만드는" 시험(heavy traffic + 전원 사이클 반복 → 데이터 대조, SIV-M09 스트레스) 쪽으로 푼다.

**2. X 가 1비트 포트에서 우연히 맞는다.** 실리콘의 X 는 그 순간 임의의 0 또는 1 로 굳는다. 8비트 포트 샘플은 대부분 다르게 보이지만, 1비트 포트(실제로 많다: enable, 인터럽트 라인)는 한 번 볼 때 절반 확률로 맞다 — 측정:

| 포트 폭 | 내림 port-sample 이 본 위반 | 내림: 둘 다 못 본 순서 | 올림: 둘 다 못 본 순서 |
|---|---|---|---|
| 8비트 | 684 / 714 | 30 | 0 |
| 1비트 | 510 / 714 | 36 | 2 |

실측 출력: `1-bit port: down port=510 either=678 monitor_only=36 | up port=81 either=116 monitor_only=2`. 8비트에서 안 보이던 위반 6개가 1비트에서는 샘플값 우연 일치로 추가로 사라지고, 올림은 2개가 monitor_only 가 된다 — 시드를 고정했으므로 이 수치는 결정적이며, 반복 횟수(시드 수)를 늘리면 줄어든다. 실제 칩에서는 **X 가 늘 같은 쪽으로 굳는 경향** 이 있어 반복으로도 안 잡힐 수 있다 — 그래서 포트 쪽 AON 에 edge-capture sticky 를 넣어 달라고 설계 단계에서 요구하는 것이 validation 리드의 일이다.

**3. 순서는 맞지만 시간이 틀리다.** 순열은 시간을 표현하지 못한다. `pd_step` 이 `PGOOD`·`CLK_ACK` 를 기다리기 때문에 안전했던 순서도, 기다리지 않으면 깨진다:

```text
  rep order, wait=yes viol=0x0  wait=no viol=0x3C2 (RET_LOST=1 RST_BAD=1 X_CROSS=1)
```

대기 없이 같은 순서를 돌리면 rail good 전에 RESTORE/RST_OFF 가 나가 복원이 안 되고 X_CROSS 까지 생긴다 (viol = X_CROSS | POWERUP_X | RET_LOST | RST_BAD | PORT_BAD). 실제 시험은 스텝 사이 dwell 을 파라미터로 두고 경계를 긁는다 (SIV-M07 의 shmoo 와 같은 발상 — 이번엔 시간 축). 또 retention 전압/온도 코너에서 shadow 가 약해지는 것은 이 모델에 없다 (SIV-M08 PVT).

**4. retention 패턴 마스킹.** retention skip 이 있어도 패턴이 reset 값이면 안 보인다. `ret_skip` + `CFG=0x10, TUNE=0xFF` (reset 값 그대로): **통과** — 코드로 CHECK. 비트 고착 64종 (32비트 × stuck-at-0/1)을 패턴 조합별로 돌린 실측:

```text
  retention-bit coverage (64 stuck faults): reset-pat=32, A=32, A+~A=64
```

패턴 하나로는 비트마다 한 극성만 잡히고(32/64), `A`와 `~A` 쌍이면 64/64. 다시 말해 **retention 패턴 시험은 "reset 값과 다른 패턴" + "비트마다 0·1 둘 다" 가 동시에 필요** 하다. skip 이 잡히는 것과 비트별 커버리지는 다른 축이다 (추가 연습 3).

### Part C — 야간 캠페인: 첫 실패로 다음 행동을 고른다

스위트: 도메인별로 `first` → `gated` → `pwrcyc`×2(패턴 A/~A) → `iso`, 그리고 `cross` 3개 = 15 시험. 실패가 나면 첫 실패가 근본 원인에 가깝도록 정렬했다 (ack/타이밍 → 접근 → 값 → 포트 → 도메인 간섭).

| 결함(주입) | 실패 수 | 첫 실패: 시험 / 단계 | cause | 디버그 다음 행동 |
|---|---|---|---|---|
| `hw:gate_ok` | 3/15 | gated / GATED_RESP | GATE_NO_ERR | 도메인 브리지/fabric 의 에러 응답 경로 |
| `hw:gate_hang` | 3/15 | gated / GATED_RESP | GATE_HANG | 버스 타임아웃 설정, 도메인 브리지. JTAG 필요 시 |
| `hw:gate_wr_leak` | 3/15 | gated / GATED_WR_LEAK | GATE_WR_LEAK | 쓰기 경로의 클럭 게이팅, CTX 비트 전부 다름이면 쓰기 enable |
| `hw:off_slverr` | 4/15 | pwrcyc / OFF_RESP | OFF_RESP | 전원 꺼짐 디코드: "off" 와 "게이트" 응답 구별 경로 |
| `hw:ret_skip` | 3/15 | pwrcyc / RET | RET_LOST | 그림자 저장 신호 경로 (act = reset 값) |
| `hw:ret_bit5_0` | 2/15 | pwrcyc / RET | RET_BIT | CFG shadow 비트 5, ret 플롭 셀 또는 배선 (`diff` 가 1비트) |
| `hw:ctx_retained` | 4/15 | pwrcyc / CTX_RESET | RST_KEPT | CTX 의 retention 속성(UPF/합성) — 쓴 값이 그대로 |
| `hw:rst_skip` | 5/15 | pwrcyc / CTX_RESET | RST_CORRUPT | 도메인 reset 경로 (값이 쓰레기) |
| `hw:clamp_ff` | 2/15 | iso / PORT_CLAMP | CLAMP_VALUE | clamp 극성 — iso=on 인데 포트 ≠ 0 |
| `hw:iso_stuck` | 2/15 | iso / PORT_OPEN | ISO_STUCK | iso 해제 신호 — iso=on 인데 해제됐어야 함 |
| `hw:iso_early_hw` | 2/15 | iso / PORT_CLAMP | ISO_EARLY | PMU 의 해제 연결 — iso=off 인데 clamp 기대 |
| `hw:ack_early` | 12/15 | first / ACK k=1 | ACK_EARLY | ack 생성 카운터 — 파급이 넓다 |
| `hw:settle_no_err` | 3/15 | first / SETTLE_RESP k=0 | SETTLE_UNSAFE | settle 창 동안 응답을 막는 로직 |
| `hw:cross_gate` | 1/15 | cross / CROSS (NPU) | CROSS_DOMAIN | ISP 클럭 게이트 enable 의 배선 (희생 도메인이 NPU) |
| `seq:no_save` | 3/15 | pwrcyc / RET | RET_LOST | 전원 시퀀스 펌웨어: SAVE 빠짐 — `act`=reset 은 ret_skip 과 같다 |
| `seq:iso_early` | 1/15 | iso / PORT_OPEN | ISO_EARLY | **이 시험에서만** 보인다 — 레지스터 시험은 통과 |
| `seq:clk_before_rst` | 3/15 | pwrcyc / CTX_RESET | RST_CORRUPT | 시퀀스: 클럭이 reset 해제 전에 돈다 — `rst_skip` 과 증상 구별 안 됨 |
| `seq:restore_in_rst` | 3/15 | pwrcyc / RET | RET_LOST | RESTORE 를 reset 해제 후로 |
| `seq:pwr_before_iso` | 1/15 | iso / PORT_HOLD | ISO_LATE | PWR_OFF/RST_ON 앞에 ISO_ON — 포트가 clamp 전에 흔들렸다 |
| `seq:save_clk_on` | 3/15 | pwrcyc / RET | RET_CORRUPT | SAVE 전에 클럭 멈춤 — act 가 쓰레기(패턴도 reset 도 아님) |

읽을 점:

- **증상이 같고 원인이 다른 쌍이 있다** — 그게 이 분류기의 한계다: `hw:rst_skip` vs `seq:clk_before_rst` 는 둘 다 `CTX_RESET` 쓰레기. `hw:ret_skip` vs `seq:no_save` 는 둘 다 `act=reset`. 구별하려면 시퀀스 trace(무엇을 언제 했는지)를 같이 봐야 한다. 그래서 전원 시퀀스 펌웨어는 스텝마다 로그/sticky 를 남기게 만드는 것이 먼저다. 면접에서 "원인을 어떻게 가르나" 가 꼬리질문으로 나온다.
- `seq:iso_early` 는 실패 1/15. retention 값·reset 값 시험은 전부 통과하고 포트 샘플에서만 잡힌다 — **값이 맞다고 경계가 안전한 것은 아니다** 가 이 레슨의 한 줄 결론이다. 코드가 CHECK 한다 (`S.n == 1 && S.log[0].test == "iso"`).
- `hw:ack_early` 12/15 는 파급이 크다는 정보 자체다.

리포트 (실제 출력):

```text
---- report: SENS-A ISP seq=iso_early (pwr_on > iso_off > rst_off > restore > clk_on) ----
FAIL ISP iso step=PORT_OPEN reg=PORT off=0x000 exp=0x0000003C act=0x00000010 diff=0x0000002C bits=[2,3,5] cause=ISO_EARLY
89/89 passed
```

(`act=0x10` 은 reset 값의 하위 바이트 — RESTORE 전에 포트가 열렸다는 뜻. 디버그 엔지니어는 이 한 줄에서 "RESTORE 보다 ISO_OFF 가 먼저 나갔다" 를 바로 추론할 수 있다.)

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

검증 (Apple clang 21, arm64):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 89/89 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 89/89, ASan/UBSan 경고 없음
```

## 7. 자주 하는 실수

**① 게이트 시험을 "SLVERR 이면 끝" 으로 쓴다.** → `gate_wr_leak` 을 놓친다 (쓰기 에러를 돌려주면서 값은 들어감). 고침: 게이트 중 쓰기 시도 뒤, 켜고 나서 값 비교 (힌트 1).

**② retention 패턴을 reset 값과 같게 쓴다 / 패턴 하나만 쓴다.** `ret_skip` 이 `CFG=0x10, TUNE=0xFF` 로는 **통과** 한다 (측정). 패턴 하나로는 비트마다 한 극성만 본다.

```c
/* 틀림: pat == reset → retention skip 이 가려진다 */
check_power_cycle(D_ISP, 0x00000010u, 0x000000FFu, &f);
/* 고침: reset 과 다른 값, 그리고 그 보수까지 */
check_power_cycle(D_ISP, CFG_A, TUNE_A, &f);
check_power_cycle(D_ISP, ~CFG_A, ~TUNE_A, &f);
```

**③ 전원 복귀 뒤 기대값을 도메인 능력과 무관하게 쓴다.** `NPU` 는 retention 이 없으니 `CFG`/`TUNE` 이 reset 값이어야 맞다. `ISP` 와 같은 기대값을 쓰면 정상 모델을 FAIL 로 만든다. `DOM[d].has_ret` 으로 가른다.

**④ 포트를 시퀀스 끝에서 한 번만 본다.** 최종 값이 맞아도 도중에 reset 값이 AON 으로 새어 나갔을 수 있다 (`seq:iso_early`, `seq:pwr_before_iso`). 경계 검증은 **스텝마다 샘플** 이 핵심이다.

**⑤ 틀린 시퀀스를 올바른 시퀀스로 "고쳐" 시험하지 않는다.** 시험이 고정된 대표 시퀀스 대신 `seq_of(d)`/`q->up[i]` 를 쓰게 한다. iso 시험에서 "ISO_OFF 인덱스 = 4" 처럼 고정하면 잘못된 시퀀스를 넣었을 때 시험이 시퀀스의 의도와 다른 스텝을 본다 — 그래서 `released` 플래그로 추적.

**⑥ 순서 전수 조사 결과를 규칙표로 다시 판정한다.** 규칙표 ≡ oracle 이면 "규칙표가 틀렸다" 를 영원히 못 찾는다. 판정은 모델의 값과 모니터로, 그리고 계산으로 예측 가능한 개수(UNQUIESCED=600, X_CROSS=360)로 oracle 을 교차검증한다.

**⑦ 첫 실패가 파생 결과인 것을 그대로 보고한다.** `ack_early` 를 `gated` 보다 앞서 돌리지 않으면 `PRE` 단계 실패(`PRECOND`)가 먼저 찍혀서 ack 문제를 게이트 시험 결함으로 잘못 짚는다. 스위트는 기반부터 돌린다.

## 8. 면접 Q&A

<details>
<summary>Q1. 전원 도메인을 시험한다면 무엇부터 보겠나? (꼬리: 왜 게이트 접근이 hang 이 아니고 error 여야 하나? hang 이 나면 어떻게 디버그?)</summary>

순서: ① 도메인을 켜고 ID 를 읽을 수 있는지(기본 sanity) ② 클럭 게이트 + 접근 시 응답 ③ 전원 사이클 + reset/retention 값 ④ isolation 경계 ⑤ 첫 접근 ⑥ 다른 도메인 영향. hang 이 위험한 이유: CPU 가 응답을 기다리면 인터럽트도, 이후 로그도 없어 현장 증상이 "멈춤" 뿐이다. 좋은 설계는 fabric/브리지가 에러를 돌려준다 — 그게 없으면 버스 타임아웃(watchdog) 또는 AON debug 경로로 재현한다.

꼬리: 칩이 진짜로 hang 하면 → JTAG 으로 CPU halt 가 되는지, DAP(AP) 접근이 AON 경로로 되는지, 어떤 master 의 어떤 주소에서 멈췄는지(버스 트레이스, fabric 의 hang 감지 레지스터)를 본다. 평소에 전원 시험은 타임아웃이 켜진 빌드로 돌린다.
</details>

<details>
<summary>Q2. retention 시험 패턴을 어떻게 고르나? (꼬리: 왜 패턴이 reset 값과 같으면 안 되나? 모든 레지스터를 다 봐야 하나?)</summary>

두 축이 있다. ① skip(저장·복원이 안 일어남)을 잡으려면 패턴이 reset 값과 달라야 한다 — 같으면 "복원 안 됨" 과 "복원됨" 이 구별 안 된다 (이 레슨 측정: reset 패턴으로 `ret_skip` 이 통과). ② 비트별 저장 경로(그림자 래치 stuck-at)를 잡으려면 각 비트에 0 과 1 을 모두 줘야 한다 → `A` + `~A` 패턴 쌍 (측정: 패턴 1개 32/64, 쌍 64/64). 비트별 커버리지와 skip 탐지는 서로 다른 조건이다 (둘 다 만족하는 집합을 고른다).

꼬리: 모든 레지스터? 실제로는 retention 플롭 목록(레지스터 맵 or UPF 의 retain list)을 입력으로 받아 자동 생성하고, 레지스터 수가 크면 스캔 체인처럼 일부만 reset 값과 다른 값으로 샘플링한 뒤, 전수는 야간 회귀로 돌린다. "그림자 저장 시험" 은 그 자체로 저장 비용(시간)이 드는 시험이다.
</details>

<details>
<summary>Q3. isolation 을 왜 전원 끄기 전에 켜고 맨 마지막에 끄나? clamp 값은 어떻게 고르나? (꼬리: 0 으로 clamp 하면 항상 안전한가?)</summary>

전원이 꺼진 쪽 출력은 floating/X 다. isolation 을 먼저 켜야 살아 있는 쪽이 쓰레기를 안 보고, 해제는 값이 정의된 다음(복원 완료 뒤)이어야 AON 이 reset 값·복원 전 값을 보지 않는다. 이 레슨 실측: `seq:pwr_before_iso` 와 `seq:iso_early` 는 포트 샘플만 잡는다.

꼬리: clamp 값은 **그 신호의 "비활성" 값** 이다 — active-high 요청·인터럽트는 0, active-low reset(`rst_n`)이나 active-low enable 은 1 로 clamp 해야 안전하다. 0 clamp 가 항상 맞는 건 아니고, 신호별로 UPF 의 `-clamp_value` 로 지정한다. 틀린 clamp 는 `clamp_ff` 처럼 칩에서는 AON 쪽 오동작으로 나타난다.
</details>

<details>
<summary>Q4. 그 순서는 어디서 오나? 칩이 나온 뒤 순서를 어떻게 검증하나? (꼬리: UPF 가 있으면 시험이 필요 없지 않나?)</summary>

순서의 출처는 SoC 의 전원 설계서 + UPF power intent(IEEE 1801: `create_power_switch`, `set_isolation`, `set_retention` 등이 어떤 셀을 어디에 넣는지와 제어 신호를 기술) + PMU 시퀀서 구현이다. 이 레슨의 대표 순서도 그중 하나일 뿐이다 (예: 동기 reset 설계는 클럭을 reset 해제 전에 돌려야 하므로 순서가 달라진다 — 추가 연습 2).

꼬리: UPF 는 "셀이 어디에 있어야 하는가" 와 시뮬레이션의 정적 검사를 준다. 그러나 ① UPF-aware 시뮬레이션은 시퀀스 펌웨어 전부를 돌리지 않는다 ② 실리콘의 실제 ramp/아날로그 동작은 시뮬레이션에 없다 ③ PMU 펌웨어 버그는 RTL 이 맞아도 난다. 그래서 pre-silicon(RTLDV-A08) 과 post-silicon 시험이 둘 다 필요하고, 각자 다른 결함을 잡는다 (이 레슨의 표 — `monitor_only` 항목이 "post-si 만으로는 안 보이는 것" 이다).
</details>

<details>
<summary>Q5. 클럭 enable 후 N cycle 은 어떻게 정하고 시험하나? (꼬리: 폴링(ack)이 고정 지연보다 낫나?)</summary>

N 은 IP 스펙(클럭 트리 정착 시간 + CDC 동기화기 깊이) 또는 PMU 의 CLK_ACK 신호가 정한다. 시험은 k = 0..N+1 경계를 cycle 단위로 긁어 응답·값이 경계에서 정확히 바뀌는지 본다. 이 레슨은 일부러 k 를 고정 지연 대신 파라미터로 둔다.

꼬리: ack 가 있으면 ack 폴링이 낫다 — 고정 지연은 PVT 와 분주비에 취약하다. 단, ack 자체가 틀릴 수 있다 (`ack_early`) — 그래서 "ack 를 믿고 접근" 시험과 "ack 의 시점" 시험을 분리했다. 실제 사례: STM32 의 RCC 클럭 enable 직후 접근 errata → 드라이버의 read-back 습관.
</details>

<details>
<summary>Q6. 순서를 전부 돌려 보는 게 실용적인가? 스텝이 12개면? (꼬리: 안전 순서가 여러 개면 어느 것을 쓰나? 이 전수 조사가 놓치는 것은?)</summary>

스텝이 적을 때(≤7~8)는 전수가 쉽고 가장 설득력 있다 — 720 + 120 순서는 순식간이다. 12 스텝이면 4.8억이므로 ① 물리 모델 대신 전수 조사로 얻은 **부분순서** 로 count/구성(추가 연습 1: 비트마스크 DP) ② 쌍 위배 커버리지(pairwise: 모든 스텝 쌍의 앞뒤 순서를 뒤집는 최소 순서 집합)로 줄인다.

꼬리: 안전 순서가 여러 개면 스펙·타이밍·wake-up 지연 요구로 고른다. 놓치는 것: 측정한 네 가지 — ① 소프트웨어로 안 보이는 프로토콜 위반(`monitor_only=30`) ② 1비트 X 의 우연 일치 ③ 시간(dwell) ④ 값 의존 마스킹 — 그리고 물리 모델 밖의 것(공유 rail, IR drop, retention 전압 코너).
</details>

<details>
<summary>Q7. X 가 실리콘에서 어떻게 보이나? (꼬리: X 를 포트에서 잡는 시험이 왜 어려운가?)</summary>

실리콘에 X 는 없다. 플로팅 노드는 그 순간의 값(0 또는 1, 혹은 중간 전위 → 소비 전류 증가)으로 동작하고, 시뮬레이션의 X 는 "모른다" 를 표현한 것이다. 그래서 post-silicon 에서는 AON 이 샘플한 값이 기대와 다를 때만 드러난다 — 측정: 8비트 포트에서는 대부분 보이지만, 1비트 포트(이 레슨 표)에서는 일부가 우연히 맞아 놓친다.

꼬리: 해결책은 설계 단계의 관측성 요구(AON sticky edge-capture, 포트 샘플 레지스터)와, 반복 시험 + pre-silicon UPF-aware 시뮬레이션의 조합이다. X 관련 실패가 의심될 때 floating 소비 전류(전력 측정, SIV-A03)도 단서다.
</details>

<details>
<summary>Q8. 전원 시험 일정이 빠듯하면 무엇부터 자르나? (꼬리: 야간 회귀에 얼마나 넣나?)</summary>

자르지 않는 것: 게이트 접근 에러 응답(가장 위험한 hang 관련), 도메인 전원 사이클 + retention 값 (제품 기능), isolation 경계 — 펌웨어가 의존하는 약속이다. 자르는 것: 전체 순열(대표 + 위반 몇 개만), 모든 retention 레지스터 전수(샘플링 후 야간으로).

꼬리: 야간에는 `first` → `gated` → `pwrcyc` 스위트를 도메인 × 패턴 쌍 × 전압/온도 코너(SIV-M08)로 곱해서 돌린다. 스트레스 조건 (트래픽 + 반복 전원 사이클)은 SIV-M09 의 동시성 스트레스와 묶는다. 전원 사이클은 ramp·샘플 때문에 시험 시간이 길어 대량 cycling 은 전용 기간에 돌린다.
</details>

## 9. 추가 연습

**연습 1 — 부분순서 세기.** 전수 조사로 얻은 선후 제약을 poset 으로 입력받아, 안전 순서 개수를 **비트마스크 DP** 로 계산하고 전수 순열로 교차검증하라.

<details>
<summary>답</summary>

`dp[S]` = 집합 S 를 앞에 놓는 합법 순서 수. v 앞이어야 하는 모든 원소가 S 에 있으면 `dp[S|v] += dp[S]`. O(2^n · n²) 이므로 스텝 16개까지 현실적이다. 내림 포셋 6, 올림 2 — 전수 조사 결과와 일치. practice.c 의 `count_orders_dp` / `count_orders_brute`.

</details>

**연습 2 — 동기 reset 이면 어떻게 되나.** 이 레슨은 비동기 reset + 클럭 정지 상태에서 reset 해제를 가정했다. 플롭이 동기 reset 이면 클럭 펄스가 reset 중에 필요하다 (`CLK_ON < RST_OFF` 제약 추가). 이 제약을 넣으면 안전 순서는 몇 개가 되는가? 왜? 어떤 스텝을 추가해야 하는가?

<details>
<summary>답</summary>

0개 — `RST_OFF < RESTORE < CLK_ON` 와 `CLK_ON < RST_OFF` 가 **사이클** 이다. 스펙이 모순이고, 그런 스펙은 칩을 만들기 전에 잡혀야 한다. 해결: reset 중 유한 클럭 펄스(CLK_BURST)를 스텝으로 추가 — `PWR_ON < CLK_BURST < RST_OFF`. 그러면 안전 순서 2개로 복구. practice.c 실제 출력 `ex2: sync-reset without BURST: 0 orders (infeasible) / with BURST: 2 orders`. 면접 포인트: 순서 제약은 유향 그래프이고, 사이클 = 스펙 결함이다.

</details>

**연습 3 — 패턴 커버리지 판정.** reset 값 `rst` 인 retention 레지스터에 패턴 집합을 쓰면, (a) retention skip 과 (b) 비트별 stuck-at-0/1 를 잡는가? `pat_covers(pats, n, rst)` 를 작성하라.

<details>
<summary>답</summary>

(a) 패턴 중 하나라도 `rst` 와 달라야 skip 이 보인다. (b) 각 비트마다 그 비트가 0 인 패턴 하나, 1 인 패턴 하나가 있어야 stuck-at 양극성이 모두 보인다 (패턴과 다른 값으로 고착되면 드러나므로). `{rst}` 는 (a) 실패, `{~rst}` 는 (b) 실패, `{a, ~a}` (a ≠ rst 이거나 그 보수가 rst 와 달라 사실상 항상) 는 통과. `{0, 0xFFFFFFFF}` 도 통과.

</details>

**연습 4 — 도메인 의존성 → 전원 순서.** 도메인 사이에 의존이 있다 (NPU 는 NOC 없이 못 켠다 등). 의존 그래프에서 올림 순서를 구하고 (없으면 -1), 내림 순서는 역순으로 하라.

<details>
<summary>답</summary>

Kahn 위상 정렬. 작은 번호를 먼저 뽑아 결정적이게 하고, 진입차수 0 인 것이 없으면 사이클 = `-1`. 내림 = 올림의 역순 (의존하는 쪽을 먼저 끄고, 의존되는 쪽을 나중에 끈다). 실제 출력 `ex4: up order: AON NOC ISP NPU PERI | down order: PERI NPU ISP NOC AON`. 사이클은 스펙 오류다 (연습 2와 같은 논리).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 18/18 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o san_pr && ./san_pr   # 18/18, 경고 없음
```

실제 출력:

```text
ex1: down poset orders=6 up poset orders=2 (dp=brute)
ex2: sync-reset without BURST: 0 orders (infeasible) / with BURST: 2 orders
ex4: up order: AON NOC ISP NPU PERI | down order: PERI NPU ISP NOC AON
18/18 passed
```

## 10. 핵심 정리

- 클럭 게이트 = 값 보존·접근 금지 / 전원 스위치 = 값 소실 / isolation = 꺼진 쪽 값을 경계에서 고정 / retention = 그림자에 값 보관 후 복원.
- 대표 순서: 내림 `QUIESCE→CLK_OFF→ISO_ON→SAVE→RST_ON→PWR_OFF`, 올림 `PWR_ON→RST_OFF→RESTORE→CLK_ON(+settle)→ISO_OFF`. **실제 순서는 SoC 전원 스펙/UPF 가 정한다.**
- 게이트 시험은 응답 + 데이터 패턴 + "값이 안 샜는지" 까지. 꺼진 동안과 게이트는 응답이 달라야 원인을 가른다.
- retention 시험 패턴은 reset 값과 다르고, 비트마다 0/1 쌍 — reset 값이면 skip 이 안 보인다. 포트는 스텝마다 샘플 — 값이 맞아도 경계는 안전하지 않을 수 있다.
- 순서 전수 조사: 안전 집합은 하나의 순서가 아니라 **부분순서** 다 (내림 6, 올림 2). 판정은 물리 모델로, 계산 가능한 개수로 oracle 을 교차검증.
- 못 보는 것: 소프트웨어로 안 보이는 프로토콜 위반(`monitor_only`), 1비트 X 의 우연 일치, 순서는 맞고 시간이 틀린 경우, 값 의존 마스킹 → 설계 단계에서 sticky 를 요구한다.
- 첫 실패가 근본 원인이 되도록 기반(ack/settle) 시험을 먼저 돌린다. 증상이 같은 다른 원인은 시퀀스 trace 로 가른다.

## 참고자료

- IEEE 1801 (Unified Power Format, UPF) — power intent: `create_power_domain`, `create_power_switch`, `set_isolation`, `set_retention`, `-clamp_value`.
- Keating, Flynn, Aitken, Gibbons, Shi, *Low Power Methodology Manual* (Springer, 2007) — power gating 시퀀스, 설계 패턴.
- Arm AMBA Low Power Interface (Q-Channel / P-Channel) — 도메인 quiesce handshake.
- Linux: generic power domains (`genpd`), common clock framework (`clk_prepare_enable`) — 펌웨어 측 관점.
- ST 의 STM32 errata sheet 항목 "Delay after an RCC peripheral clock enabling" — 첫 접근 settle 의 실제 사례.
- 이 트랙: SIV-B02(mock MMIO), SIV-B09(poll-with-timeout), SIV-M07(shmoo), SIV-M08(PVT), SIV-M09(stress). 교차: RTLDV-A08(저전력 RTL, UPF-aware 검증).
