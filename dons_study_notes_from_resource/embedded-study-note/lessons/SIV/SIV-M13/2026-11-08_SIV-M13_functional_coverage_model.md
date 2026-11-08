---
id: SIV-M13
time: 90분
updated: 2026-11-08
---
# SIV-M13 검증 커버리지 모델

## 학습 목표

- coverpoint · bin(값/범위/집합/조건) · `ignore_bins` · `illegal_bins` · `at_least` · cross 를 **SystemVerilog covergroup 과 같은 의미**로 Python에 구현하고, 각 규칙(우선순위, 분모, 겹침)을 테스트로 증명한다.
- bin 정의 자체를 lint(겹침·빈틈)하고, FAIL 트랜잭션의 샘플을 커버리지에서 분리하며, 가중 평균(SV `get_coverage()`)과 flat 비율의 차이를 숫자로 설명한다.
- 야간 회귀 로그를 샘플링해 **hole 리포트**(never sampled / only in FAILED / IMPLIED / real cross hole)와 **illegal 리포트**를 만들고, 그 구멍이 자극 생성기 버그인지 DUT 버그인지 가른다.
- seed 수를 늘려 가며 **수렴 곡선**을 그리고, `1-(1-p)^n` 으로 "seed를 더 돌려도 안 덮이는 bin"을 정량적으로 판정한다.

## 1. 문제

"테스트 600개 전부 PASS"는 **무엇을** 테스트했는지 말해 주지 않는다 — 실리콘 검증 면접의 단골 질문 "언제 검증이 끝났다고 말할 수 있나?"의 답이 functional coverage이고, Rivian/NVIDIA 검증 엔지니어가 매일 아침 보는 숫자가 이것이다.

코드 커버리지(line/branch/toggle)는 RTL이나 펌웨어의 *어느 줄이 실행됐나* 를 잰다. 기능 커버리지(functional coverage)는 *테스트 플랜이 요구한 시나리오 중 어느 것이 실제로 일어났나* 를 잰다 — "LEN=0 전송", "16-beat 버스트에서 버스 에러", "에러 직후 SG 모드 전송" 같은 것. 이건 도구가 자동으로 알 수 없어서 **검증 엔지니어가 모델을 쓴다**. 프리실리콘에서는 SystemVerilog `covergroup`(IEEE 1800 19장)이 표준이고, 포스트실리콘에서는 RTL 내부 신호가 안 보이므로 **시퀀서가 남긴 로그(무엇을 썼고 무엇을 읽었나)** 를 샘플링해 같은 모델을 계산한다. 이 레슨이 그 후자다.

대상 IP는 SoC의 `DMA0`. 시퀀서(SIV-M12 같은 것)가 트랜잭션마다 한 줄씩 JSONL을 남긴다.

| 필드 | 의미 | 커버리지에서 쓰는 곳 |
|---|---|---|
| `ctrl` | DMA0.CTRL에 쓴 값: `MODE[1:0]` 0 M2M · 1 M2P · 2 P2M · 3 SG, `BURST[3:2]` 1/4/8/16 beat, `IE[4]` | `cp_mode`, `cp_burst` (필드 auto bin) |
| `len` | DMA0.LEN (16비트, 0 = 즉시 완료가 스펙상 합법) | `cp_len` 범위 bin 7개 |
| `int_status` | 전송 뒤 읽은 INT_STATUS: `DONE[0] HALF[1] ERR[2] TIMEOUT[3]` | `cp_irq` 조합 bin 5개 + illegal 3개 |
| `status` | 테스트 체커 판정 PASS/FAIL | FAIL이면 커버리지에 **안** 센다 |
| `seed`, `step`, `test` | 어느 랜덤 seed의 몇 번째 트랜잭션인가 | 수렴 곡선, illegal 위치 보고 |

```text
{"seed":0,"step":0,"test":"dma_len_max","status":"FAIL","ctrl":"0x15","len":65535,"int_status":"0x3","note":"dst mismatch at byte 65534 (len counter wrap)"}
{"seed":0,"step":2,"test":"dma_rand","status":"PASS","ctrl":"0x13","len":4125,"int_status":"0x3"}
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="sivm13-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">DMA0 커버리지 모델 — 로그 한 줄이 지나가는 길</text>
  <rect x="10" y="110" width="120" height="70" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="70" y="138" fill="currentColor" text-anchor="middle" font-weight="bold">runlog.jsonl</text>
  <text x="70" y="158" fill="currentColor" text-anchor="middle" font-size="12">642 트랜잭션</text>
  <line x1="130" y1="145" x2="175" y2="145" stroke="currentColor" marker-end="url(#sivm13-a)"/>
  <rect x="180" y="40" width="200" height="40" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="280" y="65" fill="currentColor" text-anchor="middle">cp_mode · 4 auto bin</text>
  <rect x="180" y="90" width="200" height="40" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="280" y="115" fill="currentColor" text-anchor="middle">cp_burst · 4 auto bin</text>
  <rect x="180" y="140" width="200" height="40" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="280" y="165" fill="currentColor" text-anchor="middle">cp_len · 7 범위 bin</text>
  <rect x="180" y="190" width="200" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="280" y="211" fill="currentColor" text-anchor="middle">cp_irq · 5 bin</text>
  <text x="280" y="229" fill="currentColor" text-anchor="middle" font-size="12">+ illegal 3 (none, DONE+ERR, DONE+TMO)</text>
  <rect x="440" y="70" width="140" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="510" y="96" fill="currentColor" text-anchor="middle">x_mode_len</text>
  <text x="510" y="116" fill="currentColor" text-anchor="middle" font-size="12">4×7−1 = 27 bin</text>
  <rect x="440" y="160" width="140" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="510" y="186" fill="currentColor" text-anchor="middle">x_burst_irq</text>
  <text x="510" y="206" fill="currentColor" text-anchor="middle" font-size="12">4×5 = 20 bin</text>
  <g stroke="currentColor" fill="none" marker-end="url(#sivm13-a)">
    <path d="M380 60 L438 90"/><path d="M380 160 L438 110"/><path d="M380 110 L438 180"/><path d="M380 215 L438 200"/>
    <path d="M580 100 L620 140"/><path d="M580 190 L620 160"/>
  </g>
  <rect x="625" y="110" width="110" height="80" rx="8" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-size="12"><text x="680" y="135">hole 리포트</text><text x="680" y="155">illegal 리포트</text><text x="680" y="175">수렴 곡선</text></g>
  <text x="10" y="270" fill="currentColor" font-size="12">총 bin = 4+4+7+5+27+20 = 67. status≠PASS 인 줄은 fail_hits 로만 간다 (커버리지 분자에 안 들어감).</text>
  <text x="10" y="290" fill="currentColor" font-size="12">illegal 값이 나온 샘플은 cp_irq 와 x_burst_irq 에 안 들어가고 '버그 후보'로 따로 보고된다.</text>
</svg>
```

자극 생성기(`gen_runlog.py`)에는 첫 야간 회귀(v1) 때 **생성기 버그 3개**가, DUT 모델에는 **DUT 버그 2개**가 심어져 있다. 커버리지 모델은 이것들을 찾아내야 한다.

| id | 심은 결함 | 커버리지에서 보이는 모양 |
|---|---|---|
| G1 | interesting-length 목록에 0이 없음 ("0은 불법" 착각) | `cp_len.zero` never sampled |
| G2 | 65535는 `randint(1, 65535)` 로만 나옴 (확률 ≈1e-5) | `cp_len.max` 가 랜덤으로는 안 맞음 |
| G3 | 버스 에러 주입 hook이 16-beat 경로에 없음 | `x_burst_irq` B16×err, B16×half_err |
| D1 | M2P + LEN=65535 → 마지막 beat 데이터 불일치 | `max` 가 FAIL 테스트에서만 맞음 |
| D2 | LEN=1에서 에러 → DONE과 ERR가 동시에 섬 | `illegal_bins done_with_err` 히트 (테스트는 PASS) |

### Part A — coverpoint와 bin

- `bin_match(spec, v)` — int(같은 값) · `(lo, hi)` tuple(양끝 포함 범위) · set/list(원소) · callable(조건).
- `Coverpoint.sample(value, passed, rec)` — 우선순위 **illegal > ignore > regular**. 값이 regular bin 여러 개에 걸치면 전부 +1 (SV와 같다). `passed=False` 면 `hits` 가 아니라 `fail_hits` 를 올린다.
- `Coverpoint.coverage()` — `(hits >= at_least 인 bin 수, regular bin 수)`.

### Part B — cross, 집계, bin lint, merge

- `Cross.is_ignored` / `Cross.sample` — `('SG', 'zero')`, `('*', 'max')` 같은 와일드카드 ignore. 두 coverpoint가 *이번 샘플에서 카운트한* bin들의 곱집합만 센다 (illegal/ignore 값이면 cross도 안 셈).
- `Covergroup.flat()` (전체 bin 비율) / `Covergroup.coverage()` (항목별 % 의 weight 가중 평균 = SV `get_coverage()`).
- `lint_bins(cp, domain)` — bin 정의의 겹침(`W_OVERLAP`)과 빈틈(`W_GAP`)을 연속 구간으로 보고.
- `merge_into(dst, src)` — seed/밤/보드 단위 DB를 **합**으로 병합 (OR 아님).

### Part C — 야간 회귀: hole 리포트와 수렴 곡선

- `holes(cg)` — `HOLE`(never sampled / only in FAILED tests / under goal), cross hole 중 성분 bin이 이미 coverpoint hole이면 `IMPLIED` 한 줄로 묶고, 나머지는 `(real cross hole)`.
- `convergence(records)` / `plateau_at(curve)` — seed를 0,1,2,… 순으로 누적하며 커버리지와 새로 덮인 bin 수를 기록, 마지막으로 새 bin이 나온 seed 수.
- v1 로그에서 G1·G2·G3·D1·D2가 **정확한 위치·이유**로 보고되고, 생성기만 고친 v2에서 D1(M2P×max, FAIL 3건)만 남는지 확인한다.

## 2. Problem (English)

> **Functional coverage model for a DMA block, sampled from a regression log.**
>
> Each log record is one DMA transaction: the CTRL value written (`MODE[1:0]`, `BURST[3:2]`), the 16-bit `len`, the
> `int_status` read back (`DONE/HALF/ERR/TIMEOUT` bits), the checker verdict `status`, and `seed/step/test`.
> Using only the Python 3 standard library, implement:
>
> 1. `Coverpoint(name, fn, bins, ignore, illegal, at_least, weight)` where a bin spec is a value, an inclusive `(lo, hi)`
>    range, a set, or a predicate. `sample()` gives illegal bins precedence over ignore bins over regular bins, counts a
>    value in every regular bin it falls into, and counts FAILED transactions separately (`fail_hits`).
> 2. `Cross(a, b, ignore)` with `'*'` wildcards, built only from the coverpoints' regular bins; covergroup coverage as
>    both a flat ratio and the SystemVerilog-style weighted average; `lint_bins()` that reports overlapping bins and
>    domain values that fall into no bin; `merge_into()` for combining databases.
> 3. `holes()` that classifies each uncovered bin (never sampled / only in failed tests / below `at_least`) and collapses
>    cross holes that are already explained by a coverpoint hole; `convergence()` over an increasing number of seeds.
>
> Example: on `runlog_v1.jsonl` the model reports 56/67 bins, `HOLE cp_len.zero ... never sampled`,
> `HOLE cp_len.max ... fail=1 only in FAILED tests`, `IMPLIED x_mode_len 7 bins`, two real cross holes at BURST=16 × ERR,
> and two `ILLEGAL cp_irq.done_with_err` records; coverage stops growing after seed 16.

## 3. 시작 코드

<!-- code: starter.py -->

로그 생성기 (결정적: seed마다 `random.Random(1000 + seed)`). 커밋된 `runlog_v1.jsonl`, `runlog_v2.jsonl` 은 각 642줄, 65 KB.

<!-- code: gen_runlog.py -->

```bash
cd lessons/SIV/SIV-M13
python3 -I starter.py; echo "exit=$?"        # TODO 상태
```

실제 출력 (일부):

```text
-- Part A: coverpoint / bin --
FAIL  bin_match int / range inclusive both ends
FAIL  bin_match set / callable
FAIL  overlapping value 5 counts in BOTH lo and mid
FAIL  illegal beats regular: 7 -> [] and recorded, lo/mid unchanged
PASS  ignore beats regular: 20 -> [] and hi not counted
PASS  at_least=2: lo hit once -> coverage (0, 3)
...
PASS  good LEN_BINS over 0..65535: no overlap, no gap
...
FAIL  v2 plateau at seed 16, illegal still 1 (D2 is a DUT bug, not a generator bug)
3/30 passed
exit=1
```

> 공짜 PASS 3개에 주목: 아무것도 세지 않는 모델도 "ignore 값은 안 센다", "한 번 맞으면 at_least=2 미달", "정상 bin 정의는 lint 경고 0개" 를 통과한다. 커버리지 도구는 **센 것이 맞는지** 와 **망가진 입력에 경고가 나는지** 로 검증해야 한다.

## 4. 힌트

<details>
<summary>힌트 1 — bin_match 의 검사 순서</summary>

`callable` 을 먼저 본다. lambda를 `v == spec` 으로 비교하면 조용히 False가 된다. 그다음 `tuple` (범위, 양끝 포함 — SV `[lo:hi]` 와 같다), set/list(`in`), 마지막에 `==`. tuple과 list를 구분하는 이유: `(2, 63)` 은 범위, `[2, 63]` 은 값 두 개.

</details>

<details>
<summary>힌트 2 — illegal 이 ignore 보다, ignore 가 regular 보다 먼저</summary>

illegal을 먼저 검사해야 "regular bin에도 걸리는 illegal 값"이 커버리지로 둔갑하지 않는다. illegal에 걸리면 `(이름, 값, rec)` 를 남기고 `[]` 를 돌려준다 — 돌려준 목록이 비어 있으니 cross도 자동으로 안 센다. ignore도 같은 이유로 regular보다 먼저.

</details>

<details>
<summary>힌트 3 — cross 는 '카운트된 bin 목록' 의 곱</summary>

`Covergroup.sample` 이 각 coverpoint의 `sample()` 반환값(카운트된 bin 이름 목록)을 모아 cross에 넘긴다. cross는 `a_bins × b_bins` 중 자기 테이블(ignore가 빠진 키)에 있는 것만 +1. 테이블 키 집합 자체가 분모다.

</details>

<details>
<summary>힌트 4 — lint 는 domain 을 한 번 훑는다</summary>

16비트 LEN이면 65536개 값 × bin 7개 — Python으로 0.1초대다. 값마다 regular bin 이름 목록을 만들고 2개 이상이면 쌍별로 값을 모은다. 어디에도(regular/ignore/illegal) 안 걸리면 gap. 마지막에 `_runs()` 로 `4097..4100` 같은 구간으로 묶는다.

</details>

<details>
<summary>힌트 5 — IMPLIED 판정과 수렴 곡선</summary>

coverpoint hole을 `(cp 이름, bin)` 집합으로 먼저 모은다. cross hole `(a, b)` 는 `(x.a.name, a)` 나 `(x.b.name, b)` 가 그 집합에 있으면 원인이 이미 보고된 것 → 첫 원인별로 개수만 센다. 수렴 곡선은 seed별로 묶어 **한 covergroup에 누적** 샘플하고, seed마다 `covered_set()` 의 차집합 크기를 `new` 로 남긴다.

</details>

## 5. 풀이

### Part A — bin 의 의미를 SV와 똑같이

SystemVerilog로 같은 모델을 쓰면 이렇다 (참고용 스케치 — 이 Mac에서 돌리지 않았다):

```systemverilog
covergroup dma0_cg with function sample(bit [1:0] mode, bit [1:0] burst, bit [15:0] len, bit [3:0] st);
  cp_mode  : coverpoint mode;                       // 2비트 → auto bin 4개
  cp_burst : coverpoint burst;
  cp_len   : coverpoint len {
    bins zero = {0};  bins one = {1};  bins small = {[2:63]};  bins medium = {[64:4095]};
    bins page = {4096};  bins large = {[4097:65534]};  bins max = {65535}; }
  cp_irq   : coverpoint st {
    bins done = {4'b0001};  bins half_done = {4'b0011};  bins err = {4'b0100};
    bins half_err = {4'b0110};  bins timeout = {4'b1000, 4'b1010};
    illegal_bins none = {0};
    illegal_bins done_with_err = {4'b0101, 4'b0111, 4'b1101, 4'b1111};
    illegal_bins done_with_tmo = {4'b1001, 4'b1011, 4'b1101, 4'b1111}; }
  x_mode_len  : cross cp_mode, cp_len { ignore_bins sg_zero = binsof(cp_mode) intersect {3} && binsof(cp_len.zero); }
  x_burst_irq : cross cp_burst, cp_irq;
endgroup
```

| 규칙 | SV | 이 구현 | 테스트 |
|---|---|---|---|
| 범위는 양끝 포함 | `[64:4095]` | `(64, 4095)` → `lo <= v <= hi` | `bin_match int / range inclusive` |
| 겹치는 bin은 둘 다 센다 | 값이 여러 bin에 속하면 각각 +1 | `got` 전부 +1 | `value 5 counts in BOTH` |
| illegal이 최우선 | illegal_bins는 다른 bin에 포함돼도 런타임 에러 | `bad` 먼저 검사, `[]` 반환 | `illegal beats regular` |
| ignore는 분모·분자 모두에서 빠짐 | ignore_bins 값은 커버리지에서 제외 | `[]` 반환, 분모는 regular만 | `ignore beats regular` |
| `at_least` | `option.at_least` (기본 1) | `hits >= at_least` | `at_least=2 ...` |
| FAIL 샘플 | (언어 규칙 아님 — 회귀 정책) | `fail_hits` 로 분리 | `FAILED sample goes to fail_hits only` |

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">cp_len — 16비트 LEN 영역을 7개 bin 으로 (빈틈·겹침 없음 = lint 통과)</text>
  <g stroke="currentColor">
    <rect x="20" y="50" width="30" height="40" class="f-warn"/><rect x="50" y="50" width="30" height="40" class="f-ok"/>
    <rect x="80" y="50" width="80" height="40" class="f-ok"/><rect x="160" y="50" width="150" height="40" class="f-ok"/>
    <rect x="310" y="50" width="40" height="40" class="f-ok"/><rect x="350" y="50" width="330" height="40" class="f-ok"/>
    <rect x="680" y="50" width="40" height="40" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="35" y="75">zero</text><text x="65" y="75">one</text><text x="120" y="75">small</text><text x="235" y="75">medium</text>
    <text x="330" y="75">page</text><text x="515" y="75">large</text><text x="700" y="75">max</text>
    <text x="35" y="108">0</text><text x="65" y="108">1</text><text x="120" y="108">2..63</text><text x="235" y="108">64..4095</text>
    <text x="330" y="108">4096</text><text x="515" y="108">4097..65534</text><text x="700" y="108">65535</text>
  </g>
  <text x="20" y="140" fill="currentColor" font-size="12">경계값(0, 1, 4096, 65535)을 단독 bin 으로 — 버그는 경계에 있다 (4KB 경계, 카운터 wrap, 0 길이).</text>
  <text x="20" y="162" fill="currentColor" font-size="12">경고색 = v1 에서 비어 있던 bin. 균등 난수 randint(1,65535) 로 max 를 맞힐 확률은 1/65535 / 트랜잭션.</text>
  <text x="20" y="184" fill="currentColor" font-size="12">bad 정의 예: medium=(64,4096) + page=4096 → W_OVERLAP 4096..4096, large=(4101,…) → W_GAP 4097..4100</text>
</svg>
```

**왜 FAIL 샘플을 빼나.** "M2P × LEN=65535" 를 실행했는데 데이터가 틀렸다면, 그 시나리오는 *검증된* 것이 아니라 *버그가 발견된* 것이다. 커버리지에 세면 RTL/실리콘 수정 후 그 시나리오를 다시 돌려 확인해야 한다는 사실이 숫자에서 사라진다. 그래서 회귀 도구들은 보통 PASS 테스트의 커버리지만 merge하고, 이 레슨은 거기에 더해 `fail_hits` 를 남겨 "안 돌린 것"과 "돌렸는데 실패한 것"을 리포트에서 구분한다.

**레지스터 필드 auto bin.** `field_bins(MODE_NAMES)` 는 2비트 필드의 값 4개마다 bin을 만든다 (SV에서 bins를 안 쓰면 생기는 `auto[0..3]`). `ctrl=0x1D` → `MODE = 0x1D & 3 = 1 (M2P)`, `BURST = (0x1D >> 2) & 3 = 3 (B16)` — SIV-B01에서 생성한 `_GET` 매크로와 같은 계산이다.

### Part B — 분모가 맞아야 숫자가 맞다

**cross 분모.** `x_mode_len` 은 4×7 = 28칸에서 `SG×zero` 를 ignore해 27. 와일드카드 `('*', 'max')` + `('SG', '*')` 를 주면 SG 행 전체와 max 열 전체가 빠져 3×6 = 18 — 테스트가 이 둘을 확인한다. ignore를 **분자에서만** 빼고 분모에 남기면 영원히 100%에 못 간다. 반대로 illegal인 조합을 ignore로 적으면, 그 조합이 실제로 일어나도 아무도 모른다.

**illegal 샘플과 cross.** `int_status=0x5`(DONE+ERR) 레코드는 `cp_irq.sample()` 이 `[]` 를 돌려주므로 `x_burst_irq` 에도 안 들어간다. 하지만 `cp_mode`·`cp_len` 은 정상 값이니 `x_mode_len` 에는 들어간다 — 테스트 `illegal irq sample: no x_burst_irq hit, but x_mode_len still counted`.

**bin lint가 찾은 것.** `cp_irq` 를 4비트 INT_STATUS 전체(0..15)에 대해 lint하면:

| 값 | 비트 | 정의된 곳 |
|---|---|---|
| 1, 3, 4, 6, 8, 10 | DONE / HALF+DONE / ERR / HALF+ERR / TMO / HALF+TMO | regular |
| 0, 5, 7, 9, 11, 13, 15 | 없음 / DONE+ERR… / DONE+TMO… | illegal |
| **2** | HALF만 | `W_GAP cp_irq 2..2` |
| **12, 14** | ERR+TIMEOUT (±HALF) | `W_GAP cp_irq 12..12`, `14..14` |

빈틈 = **테스트 플랜이 결정을 안 한 값**이다. "HALF만 서고 DONE이 안 선 상태로 전송이 끝난다"는 스펙상 불가능하니 illegal로 옮겨야 하고, "ERR와 TIMEOUT 동시"는 스펙 담당자에게 물어야 한다. lint 없이 두면 그런 값이 실리콘에서 나와도 regular에도 illegal에도 안 걸려 조용히 버려진다.

**가중 평균 vs flat.** v1 결과로 계산하면:

| 항목 | 덮임/전체 | % |
|---|---|---|
| cp_mode, cp_burst, cp_irq | 4/4, 4/4, 5/5 | 100, 100, 100 |
| cp_len | 5/7 | 71.4 |
| x_mode_len | 20/27 | 74.1 |
| x_burst_irq | 18/20 | 90.0 |
| **가중 평균 (weight 모두 1)** | (300 + 71.43 + 74.07 + 90.0) / 6 | **89.25** |
| **flat** | 56/67 | **83.6** |

SV의 covergroup 커버리지는 항목(coverpoint·cross)별 %를 `weight` 로 가중 평균한다. 그래서 bin 4개짜리 coverpoint 하나가 bin 27개짜리 cross와 같은 무게를 갖는다 — 작은 coverpoint가 100%면 전체가 부풀어 보인다. 리포트에 두 숫자를 같이 쓰고, 중요한 cross에 `weight` 를 더 주거나 관심 없는 coverpoint에 `weight=0` 을 주는 것이 실무 습관이다. 손 계산 테스트: a=50%(w1), b=100%(w3), cross 25%(w1) → (50+300+25)/5 = 75.0%, flat = 4/8.

**merge는 합.** 4개 shard(seed mod 4)로 나눠 샘플한 DB를 `merge_into` 로 합친 결과가 전체를 한 번에 샘플한 것과 hits·fail_hits·illegal·samples까지 같아야 한다 (테스트로 확인). OR(덮였나 여부)로 merge하면 `at_least=3` 같은 목표를 판정할 수 없다. 실무에서는 시뮬레이터별 DB(UCDB/VDB 등)를 도구의 merge 명령이나 UCIS API로 합친다.

### Part C — 야간 회귀 리포트 읽기

`python3 -I solution.py --report` 실제 출력 (테스트 줄 다음 부분):

```text
=== runlog_v1.jsonl: 642 records, 1 FAIL | flat 56/67 = 83.6% | weighted 89.25%
  cp_mode       4/4   100.0%
  cp_burst      4/4   100.0%
  cp_len        5/7    71.4%
  cp_irq        5/5   100.0%
  x_mode_len   20/27   74.1%
  x_burst_irq  18/20   90.0%
  HOLE cp_len.zero hits=0/1 fail=0 never sampled
  HOLE cp_len.max hits=0/1 fail=1 only in FAILED tests
  IMPLIED x_mode_len 7 bins <- cp_len.zero(3) cp_len.max(4)
  HOLE x_burst_irq.B16*err hits=0/1 fail=0 never sampled (real cross hole)
  HOLE x_burst_irq.B16*half_err hits=0/1 fail=0 never sampled (real cross hole)
  ILLEGAL cp_irq.done_with_err value=0x5 seed=0 step=8 test=dma_rand ctrl=0x18 len=1 status=PASS
  ILLEGAL cp_irq.done_with_err value=0x5 seed=7 step=6 test=dma_rand ctrl=0x1A len=1 status=PASS
  seeds: 1:34 2:43 3:45 4:48 6:52 8:53 10:53 12:55 16:56 19:56 24:56 32:56
  plateau_at = 16
=== runlog_v2.jsonl: 642 records, 3 FAIL | flat 66/67 = 98.5% | weighted 99.38%
  cp_mode       4/4   100.0%
  cp_burst      4/4   100.0%
  cp_len        7/7   100.0%
  cp_irq        5/5   100.0%
  x_mode_len   26/27   96.3%
  x_burst_irq  20/20  100.0%
  HOLE x_mode_len.M2P*max hits=0/1 fail=3 only in FAILED tests (real cross hole)
  ILLEGAL cp_irq.done_with_err value=0x5 seed=8 step=0 test=dma_rand ctrl=0x1B len=1 status=PASS
  seeds: 1:35 2:45 3:52 4:57 6:60 8:63 10:64 12:65 16:66 19:66 24:66 32:66
  plateau_at = 16
```

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">runlog_v1 cross 히트 맵 (PASS 샘플 수) — 칸 하나 = cross bin 하나</text>
  <text x="10" y="40" fill="currentColor" font-size="13">x_mode_len (cp_mode × cp_len) 20/27</text>
  <g fill="currentColor" text-anchor="middle"><text x="72" y="62">zero</text><text x="116" y="62">one</text><text x="160" y="62">small</text><text x="204" y="62">medium</text><text x="248" y="62">page</text><text x="292" y="62">large</text><text x="336" y="62">max</text></g>
  <text x="44" y="89" fill="currentColor" text-anchor="end">M2M</text><rect x="50" y="70" width="44" height="30" class="f-warn" stroke="currentColor"/><text x="72" y="89" fill="currentColor" text-anchor="middle">0</text><rect x="94" y="70" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="116" y="89" fill="currentColor" text-anchor="middle">8</text><rect x="138" y="70" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="160" y="89" fill="currentColor" text-anchor="middle">13</text><rect x="182" y="70" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="204" y="89" fill="currentColor" text-anchor="middle">22</text><rect x="226" y="70" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="248" y="89" fill="currentColor" text-anchor="middle">7</text><rect x="270" y="70" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="292" y="89" fill="currentColor" text-anchor="middle">114</text><rect x="314" y="70" width="44" height="30" class="f-warn" stroke="currentColor"/><text x="336" y="89" fill="currentColor" text-anchor="middle">0</text>
  <text x="44" y="119" fill="currentColor" text-anchor="end">M2P</text><rect x="50" y="100" width="44" height="30" class="f-warn" stroke="currentColor"/><text x="72" y="119" fill="currentColor" text-anchor="middle">0</text><rect x="94" y="100" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="116" y="119" fill="currentColor" text-anchor="middle">8</text><rect x="138" y="100" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="160" y="119" fill="currentColor" text-anchor="middle">9</text><rect x="182" y="100" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="204" y="119" fill="currentColor" text-anchor="middle">19</text><rect x="226" y="100" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="248" y="119" fill="currentColor" text-anchor="middle">10</text><rect x="270" y="100" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="292" y="119" fill="currentColor" text-anchor="middle">99</text><rect x="314" y="100" width="44" height="30" class="f-warn" stroke="currentColor"/><text x="336" y="119" fill="currentColor" text-anchor="middle">0/F1</text>
  <text x="44" y="149" fill="currentColor" text-anchor="end">P2M</text><rect x="50" y="130" width="44" height="30" class="f-warn" stroke="currentColor"/><text x="72" y="149" fill="currentColor" text-anchor="middle">0</text><rect x="94" y="130" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="116" y="149" fill="currentColor" text-anchor="middle">6</text><rect x="138" y="130" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="160" y="149" fill="currentColor" text-anchor="middle">21</text><rect x="182" y="130" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="204" y="149" fill="currentColor" text-anchor="middle">20</text><rect x="226" y="130" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="248" y="149" fill="currentColor" text-anchor="middle">6</text><rect x="270" y="130" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="292" y="149" fill="currentColor" text-anchor="middle">111</text><rect x="314" y="130" width="44" height="30" class="f-warn" stroke="currentColor"/><text x="336" y="149" fill="currentColor" text-anchor="middle">0</text>
  <text x="44" y="179" fill="currentColor" text-anchor="end">SG</text><rect x="50" y="160" width="44" height="30" class="f-mut" stroke="currentColor"/><text x="72" y="179" fill="currentColor" text-anchor="middle">ignore</text><rect x="94" y="160" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="116" y="179" fill="currentColor" text-anchor="middle">6</text><rect x="138" y="160" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="160" y="179" fill="currentColor" text-anchor="middle">13</text><rect x="182" y="160" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="204" y="179" fill="currentColor" text-anchor="middle">27</text><rect x="226" y="160" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="248" y="179" fill="currentColor" text-anchor="middle">8</text><rect x="270" y="160" width="44" height="30" class="f-ok" stroke="currentColor"/><text x="292" y="179" fill="currentColor" text-anchor="middle">114</text><rect x="314" y="160" width="44" height="30" class="f-warn" stroke="currentColor"/><text x="336" y="179" fill="currentColor" text-anchor="middle">0</text>
  <text x="410" y="40" fill="currentColor" font-size="13">x_burst_irq (cp_burst × cp_irq) 18/20</text>
  <g fill="currentColor" text-anchor="middle"><text x="479" y="62">done</text><text x="537" y="62">h_done</text><text x="595" y="62">err</text><text x="653" y="62">h_err</text><text x="711" y="62">tmo</text></g>
  <text x="444" y="89" fill="currentColor" text-anchor="end">B1</text><rect x="450" y="70" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="479" y="89" fill="currentColor" text-anchor="middle">19</text><rect x="508" y="70" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="537" y="89" fill="currentColor" text-anchor="middle">131</text><rect x="566" y="70" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="595" y="89" fill="currentColor" text-anchor="middle">9</text><rect x="624" y="70" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="653" y="89" fill="currentColor" text-anchor="middle">2</text><rect x="682" y="70" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="711" y="89" fill="currentColor" text-anchor="middle">6</text>
  <text x="444" y="119" fill="currentColor" text-anchor="end">B4</text><rect x="450" y="100" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="479" y="119" fill="currentColor" text-anchor="middle">24</text><rect x="508" y="100" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="537" y="119" fill="currentColor" text-anchor="middle">105</text><rect x="566" y="100" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="595" y="119" fill="currentColor" text-anchor="middle">10</text><rect x="624" y="100" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="653" y="119" fill="currentColor" text-anchor="middle">10</text><rect x="682" y="100" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="711" y="119" fill="currentColor" text-anchor="middle">9</text>
  <text x="444" y="149" fill="currentColor" text-anchor="end">B8</text><rect x="450" y="130" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="479" y="149" fill="currentColor" text-anchor="middle">23</text><rect x="508" y="130" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="537" y="149" fill="currentColor" text-anchor="middle">99</text><rect x="566" y="130" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="595" y="149" fill="currentColor" text-anchor="middle">6</text><rect x="624" y="130" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="653" y="149" fill="currentColor" text-anchor="middle">8</text><rect x="682" y="130" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="711" y="149" fill="currentColor" text-anchor="middle">6</text>
  <text x="444" y="179" fill="currentColor" text-anchor="end">B16</text><rect x="450" y="160" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="479" y="179" fill="currentColor" text-anchor="middle">37</text><rect x="508" y="160" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="537" y="179" fill="currentColor" text-anchor="middle">128</text><rect x="566" y="160" width="58" height="30" class="f-warn" stroke="currentColor"/><text x="595" y="179" fill="currentColor" text-anchor="middle">0</text><rect x="624" y="160" width="58" height="30" class="f-warn" stroke="currentColor"/><text x="653" y="179" fill="currentColor" text-anchor="middle">0</text><rect x="682" y="160" width="58" height="30" class="f-ok" stroke="currentColor"/><text x="711" y="179" fill="currentColor" text-anchor="middle">7</text>
  <text x="10" y="222" fill="currentColor">경고색 = hole. zero·max 열은 cp_len 자체가 hole 이라 생긴 IMPLIED hole 7칸 (0/F1 = FAIL 에서만 1번).</text>
  <text x="10" y="242" fill="currentColor">B16 × err / h_err: B16 행도, err 열도 다른 곳에선 덮였는데 조합만 빈 real cross hole (G3).</text>
  <text x="10" y="262" fill="currentColor">회색 = ignore_bins (SG×zero) — 분모 27 에서 이미 빠졌다.</text>
</svg>
```

리포트 한 줄 → 누가 무엇을 하나:

| 리포트 줄 | 분류 | 원인 | 조치 |
|---|---|---|---|
| `HOLE cp_len.zero ... never sampled` | 생성기 | G1: 0이 interesting 목록에 없음 | 제약 수정 (스펙 재확인: LEN=0 합법) |
| `HOLE cp_len.max ... fail=1 only in FAILED tests` | DUT 버그가 막음 | D1 + G2 | 버그 티켓, 수정 후 directed 재실행. 랜덤 쪽은 65535를 interesting에 추가 |
| `IMPLIED x_mode_len 7 bins` | 파생 | 위 두 줄 | 따로 조치 없음 — 묶지 않으면 리포트가 9줄로 부풀어 진짜 구멍을 가린다 |
| `HOLE x_burst_irq.B16*err` (+ half_err) | 생성기 | G3: 16-beat 경로에 주입 hook 없음 | 시퀀서 수정. **marginal은 다 덮였는데 조합만 빈** 전형적 cross hole |
| `ILLEGAL cp_irq.done_with_err ... len=1 status=PASS` | DUT 버그 | D2 | 데이터 체커는 통과했는데 상태 조합이 스펙 위반 — illegal_bins가 **체커 역할**을 했다. seed·step으로 재현 |

v2(생성기만 수정)에서는 G1~G3 구멍이 모두 닫히고, `M2P*max` 만 FAIL 3건으로 남는다 (D1은 RTL/실리콘 문제라 생성기로는 못 닫는다). D2 illegal도 여전히 1건 — 생성기를 고쳐도 DUT 버그는 그대로라는 것이 숫자로 보인다.

**수렴 곡선.** 같은 출력의 ASCII 곡선 (`*` v1, `o` v2, 행 = flat % 5% 구간, 열 = 누적 seed 수):

```text
100% |                                
 95% |        oooooooooooooooooooooooo
 90% |      oo                        
 85% |   ooo                          
 80% |          **********************
 75% |  o  *****                      
 70% |   **                           
 65% | o*                             
 60% | *                              
 55% |                                
 50% |o                               
     +--------------------------------
               1         2         3    seeds (x10)
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="22" fill="currentColor" font-weight="bold" font-size="13">누적 seed 수 → 덮인 bin 수 (67개 중) — 실제 측정값</text>
  <line x1="60" y1="250" x2="690" y2="250" stroke="currentColor"/><line x1="60" y1="40" x2="60" y2="250" stroke="currentColor"/>
  <line x1="60" y1="46" x2="690" y2="46" stroke="currentColor" stroke-dasharray="4 4"/>
  <g fill="currentColor" text-anchor="end"><text x="54" y="50">67</text><text x="54" y="111">56</text><text x="54" y="144">50</text><text x="54" y="199">40</text><text x="54" y="254">30</text></g>
  <g fill="currentColor" text-anchor="middle"><text x="60" y="268">1</text><text x="200" y="268">8</text><text x="360" y="268">16</text><text x="520" y="268">24</text><text x="680" y="268">32</text></g>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="60,228 80,178 100,168 120,151 140,140 160,129 180,124 200,124 220,124 240,124 260,112 280,112 300,112 320,112 340,112 360,107 380,107 400,107 420,107 440,107 460,107 480,107 500,107 520,107 540,107 560,107 580,107 600,107 620,107 640,107 660,107 680,107"/>
  <polyline fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3" points="60,222 80,168 100,129 120,102 140,90 160,85 180,74 200,68 220,63 240,63 260,58 280,58 300,58 320,58 340,58 360,52 380,52 400,52 420,52 440,52 460,52 480,52 500,52 520,52 540,52 560,52 580,52 600,52 620,52 640,52 660,52 680,52"/>
  <rect x="440" y="120" width="275" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="450" y="140" fill="currentColor">v1 실선: 56 에서 평평 (seed 16 이후 +0)</text>
  <text x="450" y="158" fill="currentColor">→ seed 를 늘려도 11칸은 안 닫힌다</text>
  <text x="450" y="174" fill="currentColor">v2 점선: 66 — 남은 1칸은 DUT 버그(D1)</text>
  <text x="300" y="290" fill="currentColor" text-anchor="middle">누적 seed 수 (seed 당 랜덤 트랜잭션 20개)</text>
</svg>
```

| 누적 seed | 1 | 2 | 3 | 4 | 6 | 8 | 10 | 12 | 16 | 32 |
|---|---|---|---|---|---|---|---|---|---|---|
| v1 덮인 bin | 34 | 43 | 45 | 48 | 52 | 53 | 53 | 55 | 56 | 56 |
| v2 덮인 bin | 35 | 45 | 52 | 57 | 60 | 63 | 64 | 65 | 66 | 66 |

곡선은 처음 몇 seed에서 가파르게 오르고(흔한 bin), 꼬리에서 드물게 한 칸씩 오른다. v1의 마지막 새 bin(seed 16)은 `x_burst_irq.B1*half_err` 이었다 — 1-beat × 에러 × 에러 위치가 후반부, 세 조건이 겹쳐야 하는 드문 칸. 그 뒤 16개 seed는 아무것도 더하지 않았다. **plateau의 의미가 핵심이다**:

트랜잭션마다 독립적으로 확률 p로 어떤 bin을 맞힌다면, n개 뒤에 한 번이라도 맞을 확률은 `1 - (1-p)^n`. 신뢰수준 CL로 맞히려면

    1 - (1-p)^n ≥ CL  ⇔  n ≥ ln(1-CL) / ln(1-p) ≈ -ln(1-CL) / p     (p 작을 때)

CL=95%면 `-ln 0.05 ≈ 3.0` 이므로 **n ≈ 3/p** (SIV-M06의 "BER 1e-12를 95%로 주장하려면 3e12비트"와 같은 식이다).

| bin | 생성 확률 p (트랜잭션당) | 95% 에 필요한 n | seed(20개) 환산 |
|---|---|---|---|
| v1 `cp_len.max` (randint만) | 0.7/65535 ≈ 1.07e-5 | 280,464 | ≈ 14,000 |
| v2 `cp_len.max` (interesting 10개 중 1개) | 0.03 + 1.07e-5 ≈ 0.030 | 99 | 5 |
| v2 `M2P*max` (MODE 1/4 추가) | ≈ 0.0075 | 398 | 20 |
| v1 `cp_len.zero` | 0 | ∞ | 영원히 |

(숫자는 practice.py 연습 2의 실제 출력.) 그래서 plateau가 보이면 **seed를 더 돌리는 게 아니라** hole을 보고 제약·가중치(interesting 값, 분포 bias)를 고치거나 directed 테스트를 추가한다. 이것이 coverage-driven verification(CDV)의 루프다: 모델 → 랜덤 회귀 → hole 분석 → 제약 수정 → 다시 회귀.

**포스트실리콘에서 다른 점.** 프리실리콘 covergroup은 RTL 내부 신호(FSM 상태, FIFO 수위)를 샘플할 수 있지만, 실리콘에서는 소프트웨어가 보는 것 — 쓴 레지스터 값, 읽은 상태·카운터, 디버그 버스/트레이스·성능 카운터 — 만 샘플할 수 있다. 그래서 같은 테스트 플랜이라도 실리콘 쪽 모델은 "자극 + 관측된 응답" 위주로 다시 쓰고, 내부 상태 커버리지는 프리실리콘 결과를 인용한다. 칩마다(보드·온도·전압 코너별) DB를 따로 두고 merge하면 "이 시나리오를 어느 코너에서 돌렸나"까지 볼 수 있다 (SIV-M08 PVT 매트릭스와 연결).

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py; echo "exit=$?"            # 30/30 passed, exit=0
python3 -I solution.py --report                   # §5 Part C 의 리포트·ASCII 곡선
python3 -I -W error -X dev solution.py            # 경고 없이 30/30
python3 -I gen_runlog.py                          # 로그 재생성 → sha256 동일 (아래)
```

실제 확인:

```text
$ python3 -I solution.py | tail -3
PASS  v2 (generator fixed): 66/67, only hole M2P*max blocked by D1
PASS  v2 plateau at seed 16, illegal still 1 (D2 is a DUT bug, not a generator bug)
30/30 passed
$ shasum -a 256 runlog_v*.jsonl > /tmp/SIV-M13_sha_before && python3 -I gen_runlog.py && shasum -a 256 -c /tmp/SIV-M13_sha_before
wrote runlog_v1.jsonl (642 records, 65187 bytes)
wrote runlog_v2.jsonl (642 records, 65720 bytes)
runlog_v1.jsonl: OK
runlog_v2.jsonl: OK
```

전체 테스트 실행은 이 Mac(Apple Silicon, Python 3.9.6)에서 0.42 s.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| FAIL 테스트 샘플도 커버리지에 셈 | "돌렸는데 실패한" 시나리오가 "검증됨"으로 둔갑 — D1이 v1에서 덮인 것처럼 보인다 | `passed=False` → `fail_hits`, 리포트에 `fail=N` |
| regular를 먼저 검사하고 illegal을 나중에 | 0x5가 어딘가 regular에 걸리면 버그가 커버리지로 기록됨 | illegal → ignore → regular 순서 |
| ignore를 분자에서만 뺌 | 분모 28에 영원히 27/28 | 테이블 키(`bins()`)에서 아예 제외 |
| 불가능한 조합을 `ignore` 로 적음 | 실리콘에서 그 조합이 나와도 침묵 | 스펙상 불가능 = `illegal`, 관심 없음 = `ignore` |
| 범위 bin을 반열림 `[lo, hi)` 로 구현 | `medium=(64,4095)` 가 4095를 놓침 — 경계값이 정확히 버그 자리 | `lo <= v <= hi` (SV `[lo:hi]` 와 같게) + lint로 gap 확인 |
| cross 구멍을 전부 나열 | cp_len.zero 하나 때문에 cross hole 3~4줄 — 진짜 cross hole이 묻힘 | 원인 coverpoint hole로 `IMPLIED` 묶기 |
| merge를 OR로 | `at_least` 판정 불가, 몇 번 맞았는지 사라짐 | 횟수 합 |
| 가중 평균 하나만 보고 | 작은 coverpoint 100%가 전체를 부풀림 (89.25% vs flat 83.6%) | 두 숫자 + 항목별 표 |
| 커버리지 plateau에 seed만 늘림 | p≈1e-5 bin은 seed 14,000개 필요 | hole 분석 → 제약/분포 수정, directed 추가 |
| `illegal` 히트를 리포트 맨 끝에 숫자로만 | 위치가 없으면 재현 불가 | seed·step·test·ctrl·len을 같이 출력 |

## 8. 면접 Q&A

<details>
<summary>Q1. 코드 커버리지 100%인데 버그가 남을 수 있나요? functional coverage와 무엇이 다른가요?</summary>

있다. 코드 커버리지는 "이 줄/분기가 실행됐나"만 본다. 예컨대 DMA 길이 카운터 코드는 LEN=4097로도 100% 실행되지만, LEN=65535에서의 wrap(D1)은 그 **값**이 들어와야 드러난다. functional coverage는 테스트 플랜의 시나리오(값·조합·순서)를 엔지니어가 모델로 적은 것이라, 코드에 없는 기능(빠진 기능)이나 값 의존 버그를 겨냥할 수 있다. 둘은 보완 관계 — 코드 커버리지 hole은 "죽은 코드이거나 테스트 부족", functional hole은 "플랜 대비 부족".
꼬리질문: "functional coverage 100%면 끝인가?" → 모델이 플랜을 정확히 반영했는지가 전제다. 모델 리뷰(bin이 경계값을 담았나, cross가 필요한 조합인가)와 lint가 필요하고, 버그 발견률(bug rate) 추이·코드 커버리지와 함께 sign-off 기준으로 쓴다.

</details>

<details>
<summary>Q2. 실리콘에서는 RTL 신호가 안 보이는데 커버리지를 어떻게 재나요?</summary>

소프트웨어가 관측할 수 있는 것을 샘플한다: 테스트가 쓴 설정(레지스터 값, 디스크립터 내용), 읽은 상태·에러·카운터 레지스터, 성능 카운터, 디버그 트레이스. 시퀀서가 이를 구조화된 로그(JSONL 등)로 남기면 이 레슨처럼 오프라인으로 같은 coverpoint/cross를 계산한다. 내부 상태(FSM 전이 등)는 프리실리콘 DB를 인용하고, 실리콘에서는 PVT 코너·보드·칩별로 DB를 나눠 "어느 조건에서 돌렸나"를 추가 축으로 본다.
꼬리질문: "관측 불가능한 내부 이벤트는?" → 칩에 디버그 카운터/이벤트 버스를 넣어 달라고 설계 단계에서 요구하거나(DFD, design-for-debug), 응답으로부터 간접 추론하고 그 한계를 리포트에 명시한다.

</details>

<details>
<summary>Q3. ignore_bins와 illegal_bins는 언제 각각 쓰나요?</summary>

`ignore_bins` = 일어날 수 있지만 관심 없거나 이 모델에서 셀 필요가 없는 값 (분모에서 뺀다). `illegal_bins` = 스펙상 일어나면 안 되는 값 (샘플되면 에러). 이 레슨의 `SG×zero` 는 ignore(의미 없는 조합), `DONE+ERR` 는 illegal(스펙 위반). 우선순위는 illegal > ignore > regular.
꼬리질문: "illegal이 FAIL 테스트에서 나왔다면?" → 그래도 보고한다. 실패의 원인 단서이고, PASS 테스트에서 나왔다면(D2처럼) 체커가 놓친 버그라 더 중요하다. 단, 시퀀서가 의도적으로 불법 설정을 넣는 네거티브 테스트라면 그 테스트의 샘플은 다른 covergroup으로 분리한다.

</details>

<details>
<summary>Q4. 커버리지가 89%에서 멈췄습니다. 무엇을 하나요?</summary>

seed를 무작정 늘리지 않는다. hole 리포트를 분류한다: ① never sampled → 생성기 제약/분포 문제(G1, G3) — 제약을 고치거나 interesting 값·가중치를 넣는다. ② only in FAILED → DUT 버그가 막음(D1) — 버그 수정 후 재실행. ③ IMPLIED → 원인 hole만 처리. ④ 정말 도달 불가능 → 스펙 확인 후 ignore로 옮기고 리뷰 기록. 남은 것은 directed 테스트로 닫는다.
꼬리질문: "seed가 몇 개면 충분한가?" → bin별 생성 확률 p를 알면 95% 확률로 한 번 맞히는 데 n ≈ 3/p 트랜잭션. p≈1e-5면 28만 트랜잭션이라, 랜덤으로 닫을 문제가 아니라는 것을 숫자로 보여 줄 수 있다.

</details>

<details>
<summary>Q5. FAIL 난 테스트의 샘플을 커버리지에 넣어야 하나요? flaky 테스트는요?</summary>

넣지 않는 것이 일반적이다. 실패한 트랜잭션은 그 시나리오가 동작함을 보여 주지 못했다. 이 레슨은 `fail_hits` 로 따로 세어 "only in FAILED tests" 로 보고 — 버그가 막고 있는 커버리지를 드러낸다. flaky(같은 seed가 가끔 실패)는 PASS 실행의 샘플만 넣되, 실패 실행을 triage(SIV-M10)·재현(SIV-M11)으로 보내고, 실패율을 따로 추적한다.
꼬리질문: "한 테스트 안에서 트랜잭션 100개 중 1개만 실패했으면?" → 테스트 단위 vs 트랜잭션 단위 정책을 정해야 한다. 트랜잭션별 판정이 로그에 있으면 이 레슨처럼 트랜잭션 단위로, 없으면 보수적으로 테스트 전체를 뺀다.

</details>

<details>
<summary>Q6. cross가 폭발합니다 (MODE 4 × BURST 4 × LEN 7 × IRQ 5 = 560). 어떻게 관리하나요?</summary>

3-way 이상 cross는 테스트 플랜에서 **이유가 있는 조합**만 만든다. 2-way cross 여러 개로 나누고(여기서는 mode×len, burst×irq), `ignore_bins` 와 `binsof(...) intersect` 로 의미 없는 영역을 잘라내고, 관심 영역만 명시적으로 bin을 정의한다. 숫자가 커지면 hole 리포트가 읽을 수 없게 되므로 IMPLIED 묶기 같은 요약이 필수다.
꼬리질문: "그래도 조합 버그가 걱정되면?" → pairwise(모든 2-조합) 생성 전략을 자극 쪽에서 쓰고, 커버리지는 2-way로 확인하는 절충이 흔하다.

</details>

<details>
<summary>Q7. 여러 밤·여러 보드의 커버리지를 어떻게 합치나요? 모델이 중간에 바뀌면?</summary>

같은 모델 정의(버전)로 만든 DB끼리 bin별 횟수를 더한다 (연습 4). EDA 쪽은 도구별 DB 포맷과 merge 명령, 그리고 교환용 Accellera UCIS 표준 API가 있다. 모델이 바뀌면(bin 추가/분할) 예전 DB의 횟수를 새 bin에 그대로 옮길 수 없으므로 모델 버전을 DB에 기록하고, 바뀐 항목은 새로 돌린 결과로만 계산한다.
꼬리질문: "merge 결과를 어떻게 믿나?" → shard 병합 결과 == 한 번에 샘플한 결과라는 동치성 테스트(Part B의 merge 테스트)를 도구에 넣어 둔다.

</details>

<details>
<summary>Q8. at_least를 1보다 크게 두는 이유는?</summary>

한 번 맞은 것이 우연일 수 있는 시나리오(예: 에러 복구, 경계 조건)는 여러 번, 가능하면 다른 seed·다른 조건에서 맞아야 신뢰할 수 있다. SV `option.at_least` 가 그 목표다. 그래서 merge는 OR이 아니라 합이어야 한다. 단, at_least를 올리면 꼬리 bin의 필요 트랜잭션이 대략 비례해 늘어나므로 선택적으로 쓴다.

</details>

## 9. 추가 연습

### 연습 1 — 회귀 압축 (seed 랭킹)

전체 32 seed와 **같은 covered set** 을 내는 seed 목록을 그리디(매 단계 새로 덮는 bin이 가장 많은 seed)로 고른다. v2 결과: `[(25, 38), (9, 10), (29, 7), (0, 4), (17, 3), (3, 2), (1, 1), (7, 1)]` — 8개.

<details>
<summary>답 보기</summary>

seed마다 covered set을 만들고 set cover를 그리디로 푼다 (최적해는 NP-hard지만 그리디가 실무 표준이며 ln(n) 근사 보장이 있다). 용도: 매일 밤 32 seed를 다 못 돌리는 짧은 회귀(smoke/sanity)용으로 이 8개를 고정 seed 목록으로 쓴다. 단 FAIL 샘플은 covered set에 안 들어가므로 DUT 버그로 막힌 bin(`M2P*max`)은 랭킹으로도 살릴 수 없다는 점, 그리고 RTL/펌웨어가 바뀌면 같은 seed가 같은 자극을 내지 않을 수 있어(제약 해석 변화) 주기적으로 다시 랭킹해야 한다는 점을 기억.

</details>

### 연습 2 — bin 하나를 95% 확률로 맞히는 데 필요한 트랜잭션 수

`txns_for_confidence(p, cl)` = 최소 n s.t. `1-(1-p)^n ≥ cl`. 실제 출력: `len=65535: v1 p=1.07e-05 -> n95=280464 ; v2 p=0.0300 -> n95=99 ; v2 M2P*max p=0.00750 -> n95=398`.

<details>
<summary>답 보기</summary>

`n = ceil(ln(1-cl) / ln(1-p))`. 작은 p에서 `ln(1-p)` 를 그냥 계산하면 정밀도가 깎이므로 `math.log1p(-p)` 를 쓴다. p=0.5 → n=5 (0.5^4 = 0.0625 > 0.05). 균등 1/65535 → 196,324. 검증으로 v2 로그에서 실제 len=65535 랜덤 트랜잭션 수(16)가 기대값 640×0.030 ≈ 19.2의 3σ 안인지도 CHECK한다 — 생성기가 문서대로 동작하는지 확인하는 습관.

</details>

### 연습 3 — 에러 복구 전이(transition) 커버리지

SV의 `bins t = (A => B)` 처럼 **순서** 를 커버한다: 같은 seed에서 연속한 PASS 트랜잭션 (prev, next) 중 prev가 err/timeout이면 bin `(prev 클래스, next MODE)` 를 센다 (8 bin). "에러 직후 SG 모드가 정상 동작하는가"는 단일 샘플 coverpoint로는 표현할 수 없다.

<details>
<summary>답 보기</summary>

seed별로 step 순 정렬 후 `zip(rs, rs[1:])`. v1 실제 결과: `err>M2M 13, err>M2P 12, err>P2M 7, err>SG 12, timeout>M2M 7, timeout>M2P 7, timeout>P2M 4, timeout>SG 6` — 8/8. 손으로 만든 4-트랜잭션 시퀀스(err→SG, timeout→M2P)로 정확히 두 bin만 맞는지 확인한다. 실리콘에서 에러 후 상태 정리(채널 리셋, W1C 클리어 — SIV-B11)가 불완전하면 다음 전송이 깨지는데, 이 전이 bin이 없으면 그 시나리오를 돌렸는지조차 모른다.

</details>

### 연습 4 — 커버리지 DB 저장·복원·병합

`to_json(cg)` → hits/fail_hits/samples만 저장 (모델 정의는 코드가 단일 소스), `from_json`, `merge(*cgs)`. v1+v2 두 밤을 합치면 66/67이고 `M2P*max` 는 fail=4(v1 1 + v2 3)로 여전히 막혀 있다.

<details>
<summary>답 보기</summary>

cross 키 `(a, b)` 는 JSON 키가 될 수 없으므로 `"a*b"` 문자열로 바꾼다. `sort_keys=True` 로 결정적 출력 → 같은 DB면 바이트가 같아 diff/CI 비교가 된다. 검증 3개: 왕복 후 flat 동일, merge(v1, v2) == sample(v1+v2), 직렬화 → 복원 → 직렬화가 같은 문자열. 실무 DB(UCDB/UCIS)도 같은 구조 — 모델(스코프·bin 정의)과 카운트를 분리해 저장하고, 모델 버전이 다르면 병합을 거부하거나 경고한다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py; echo "exit=$?"     # 15/15 passed, exit=0
```

실제 출력 (일부):

```text
-- 연습 1: regression compaction --
   v2 greedy: [(25, 38), (9, 10), (29, 7), (0, 4), (17, 3), (3, 2), (1, 1), (7, 1)]
PASS  v2: greedy seeds reproduce the full covered set
...
   len=65535: v1 p=1.07e-05 -> n95=280464 ; v2 p=0.0300 -> n95=99 ; v2 M2P*max p=0.00750 -> n95=398
...
PASS  merged nights: 66/67, M2P*max still blocked (fail=4)
PASS  json is deterministic (sort_keys)
15/15 passed
```

## 10. 핵심 정리

- functional coverage = **테스트 플랜의 코드판**. coverpoint(무엇을 볼까) · bin(어느 값/구간) · cross(어느 조합) · transition(어느 순서).
- 우선순위 illegal > ignore > regular. 겹치는 값은 맞은 bin 전부 +1. ignore는 분모에서도 뺀다. 스펙상 불가능 = illegal, 관심 없음 = ignore.
- 경계값은 단독 bin으로(0, 1, 4096, 65535). bin 정의도 lint한다 — gap은 "플랜이 결정 안 한 값".
- FAIL 샘플은 커버리지에 넣지 않고 따로 센다 → "안 돌림"과 "돌렸는데 실패"를 구분.
- 리포트는 숫자 두 개(가중 평균, flat) + 항목별 표 + 분류된 hole(never / only FAILED / IMPLIED / real cross) + 위치가 있는 illegal.
- 수렴 곡선이 평평해지면 seed가 아니라 생성기를 고친다: 95% 확률로 한 번 맞히려면 n ≈ 3/p.
- merge는 횟수의 합, DB는 결정적으로 저장, shard merge == 전체 샘플 동치성으로 도구를 검증.

## 참고자료

- IEEE Std 1800-2017 SystemVerilog, Clause 19 "Functional coverage" (covergroup, coverpoint, bins / ignore_bins / illegal_bins, cross, `binsof`, `option.at_least`, `option.weight`, `get_coverage()`)
- [Accellera UCIS (Unified Coverage Interoperability Standard)](https://www.accellera.org/downloads/standards/ucis) — 커버리지 DB 교환·병합 API
- C. Spear, G. Tumbush, *SystemVerilog for Verification*, 3rd ed., Springer — Ch. 9 Functional Coverage
- [cocotb-coverage](https://github.com/mciepluc/cocotb-coverage) — Python `CoverPoint` / `CoverCross` / `coverage_db` (cocotb용)
- [PyVSC](https://github.com/fvutils/pyvsc) — Python 임베디드 제약 랜덤 + covergroup
- P. Mishra, F. Farahmandi (eds.), *Post-Silicon Validation and Debug*, Springer 2019 — 포스트실리콘 커버리지·관측성
- [Python `random` — 결정적 seed](https://docs.python.org/3/library/random.html) · [`math.log1p`](https://docs.python.org/3/library/math.html#math.log1p)
- 관련 레슨: SIV-B01(필드 GET), SIV-B08/B14(로그·리포트), SIV-B11(인터럽트 clear), SIV-M06(같은 3/p 식, BER), SIV-M08(PVT), SIV-M10(triage), SIV-M11(bisect), SIV-M12(시퀀서)
