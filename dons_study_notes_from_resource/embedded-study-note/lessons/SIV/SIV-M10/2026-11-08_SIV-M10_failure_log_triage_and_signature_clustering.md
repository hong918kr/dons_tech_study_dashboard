---
id: SIV-M10
time: 90분
updated: 2026-11-08
---
# SIV-M10 실패 로그 Triage와 시그니처 클러스터링

## 학습 목표

- 실패 레코드에서 **밤마다 바뀌는 값**(주소, 반복 횟수, 시드, 데이터 패턴, 에러코드 표기)과 **원인을 가르는 값**(테스트, 레지스터, 틀린 비트, 정규화된 에러코드)을 구분해 결정적인 **실패 시그니처(failure signature)**와 ID를 만들 수 있다.
- 클러스터마다 **분모(그 테스트가 실제로 실행된 횟수)**를 세고 보드·칩·온도·FW 버전별 실패율 표를 만들 수 있다.
- "특정 보드에서만", "특정 온도 이상에서만", "FW x.y 이후부터"를 **순도(purity) + 단측 Fisher exact test + Bonferroni 보정**으로 판정하고, 데이터로 가를 수 없는 경우(교란, confounding)를 AMBIGUOUS로 정직하게 보고할 수 있다.
- triage 결과를 디버그 엔지니어가 바로 움직일 수 있는 다음 액션(socket swap, 온도 shmoo, FW bisect, flaky 등록)으로 바꿀 수 있다.

## 1. 문제

야간 회귀(nightly regression)가 보드 8장 × 온도 6단계 × 5일을 돌리고 나면 실패 줄이 수백 개 쌓인다. 아침에 할 일은 그 줄을 하나씩 읽는 게 아니라 **"원인이 몇 개이고, 각각 어떤 조건에서만 나는가"**를 30분 안에 말하는 것이다. NVIDIA·Apple·Rivian 같은 포스트실리콘 검증 조직에서 "로그 수천 개를 받으면 어떻게 triage 하나?"는 시니어 면접의 단골 질문이고, 대답의 핵심은 **정규화 → 시그니처 → 분모 있는 상관 → 통계적으로 조심스러운 판정**이다.

입력은 SIV-B14 형식을 닮은 JSON Lines다. 런(run)마다 헤더 한 줄(보드·칩·챔버 온도·FW·시드)과 테스트마다 한 줄. 실제 파일(`nightly_regress.jsonl`, 2160줄, 181,616 B)의 몇 줄:

```text
{"type":"run","run":"R001","night":"2026-11-03","board":"EVB-01","chip":"A0-0111","temp_c":-40,"fw":"1.5.0","seed":"0x0EAEA02F"}
{"type":"test","run":"R001","test":"boot.rom.id","status":"PASS"}
{"type":"test","run":"R001","test":"irq.dma.done","status":"FAIL","code":"ETIMEDOUT","reg":"DMA_INT_STATUS","addr":"0x4002000C","exp":"0x00000001","act":"0x00000000","msg":"DMA ch0 done IRQ not seen within 100 ms"}
{"type":"test","run":"R021","test":"spi.flash.read","status":"FAIL","code":"E_TIMEOUT","reg":"SPI_STATUS","addr":"0x40013008","exp":"0x00000000","act":"0x00000001","msg":"flash read 256 B timed out after 51 ms (BUSY)"}
{"type":"test","run":"R038","test":"ddr.stress.march[seed=0xE223]","status":"FAIL","code":"E_MISCOMPARE","reg":"DDR_RDATA","addr":"0x804FAE5C","exp":"0x2D2D2D2D","act":"0x2D2D0D2D","msg":"miscompare at 0x804FAE5C iter 25 after 3903 ms (DQ13)"}
```

데이터는 `gen_logs.py`가 고정 시드로 만든다 (`random.Random(0x5EED10)`, 다시 돌리면 바이트 단위로 같은 파일). 안에 **원인 몇 개와 함정 몇 개**가 심어져 있다 — 정답은 생성기 머리 주석에 있지만, 먼저 보지 말고 코드로 찾아내는 게 이 레슨이다. 함정 예: 같은 에러를 FW 1.4.x는 `E_TIMEOUT`, 1.5.x는 `ETIMEDOUT`으로 찍는다; EVB-04에는 USB PHY가 없어 USB 테스트가 항상 SKIP이다; 보드 리셋으로 UART 콘솔이 끊겨 중간에서 잘린 줄이 2개 있다; 3일째에 EVB-07의 칩을 새 것으로 교체(socket swap)했다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="sivm10-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">야간 로그 → 원인별 클러스터 → "어떤 조건에서만?" → 다음 액션</text>
  <rect x="10" y="44" width="120" height="70" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="70" y="70" text-anchor="middle" fill="currentColor" font-weight="bold">JSONL</text>
  <text x="70" y="90" text-anchor="middle" fill="currentColor" font-size="12">240 runs</text>
  <text x="70" y="106" text-anchor="middle" fill="currentColor" font-size="12">1918 test 줄</text>
  <rect x="160" y="44" width="140" height="70" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="230" y="70" text-anchor="middle" fill="currentColor" font-weight="bold">Part A 정규화</text>
  <text x="230" y="90" text-anchor="middle" fill="currentColor" font-size="12">test·code·비트 패턴</text>
  <text x="230" y="106" text-anchor="middle" fill="currentColor" font-size="12">→ 시그니처 · ID</text>
  <rect x="330" y="44" width="130" height="70" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="395" y="70" text-anchor="middle" fill="currentColor" font-weight="bold">클러스터</text>
  <text x="395" y="90" text-anchor="middle" fill="currentColor" font-size="12">106 실패 → 4개</text>
  <text x="395" y="106" text-anchor="middle" fill="currentColor" font-size="12">+ 분모(exposure)</text>
  <rect x="490" y="44" width="240" height="70" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="610" y="70" text-anchor="middle" fill="currentColor" font-weight="bold">Part B 판정</text>
  <text x="610" y="90" text-anchor="middle" fill="currentColor" font-size="12">후보 조건 27개 × (purity, Fisher p)</text>
  <text x="610" y="106" text-anchor="middle" fill="currentColor" font-size="12">p &lt; 0.01/27 (Bonferroni)</text>
  <g stroke="currentColor" fill="none" marker-end="url(#sivm10-a)">
    <path d="M130 79 L158 79"/><path d="M300 79 L328 79"/><path d="M460 79 L488 79"/><path d="M610 114 L610 140"/>
  </g>
  <g stroke="currentColor">
    <rect x="10" y="142" width="170" height="40" rx="6" class="f-warn"/>
    <rect x="190" y="142" width="170" height="40" rx="6" class="f-warn"/>
    <rect x="370" y="142" width="170" height="40" rx="6" class="f-warn"/>
    <rect x="550" y="142" width="180" height="40" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="95" y="167">BOARD / CHIP</text><text x="275" y="167">TEMP_HIGH</text>
    <text x="455" y="167">FW (&gt;= 버전)</text><text x="640" y="167">SPREAD · AMBIGUOUS · INSUFF.</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="206">→ socket swap, 보드 HW 점검</text><text x="190" y="206">→ 그 온도에서 shmoo (M07)</text>
    <text x="370" y="206">→ good..bad bisect (M11)</text><text x="550" y="206">→ flaky 등록 / 교차 실험</text>
  </g>
  <text x="10" y="238" fill="currentColor" font-size="12">Part C: 이 전체를 nightly_regress.jsonl 에 돌려 Markdown 리포트 + 다음 액션을 만든다.</text>
</svg>
```

### Part A — 정규화와 시그니처

- `norm_code(code)` — `None`/빈 문자열 → `NONE`, 대문자화, `-`→`_`, 앞의 `E_` 제거, `CODE_ALIAS` 표 적용. `E_TIMEOUT`, `ETIMEDOUT`, `timeout` → 모두 `TIMEOUT`.
- `norm_test(name)` — `ddr.stress.march[seed=0x1A2B]` → `ddr.stress.march`. 단 원인을 가르는 파라미터(`KEEP_PARAMS = lane, ch, port`)는 남긴다: `pcie.eye[lane=3,seed=7]` → `pcie.eye[lane=3]`.
- `bit_pattern(exp, act)` — XOR 해서 틀린 비트: `"b5+b6"`. 같으면 `none`, 파싱 불가 `-`, 4비트 넘게 틀리면 `multi`.
- `norm_msg(msg)` — 리포트용 템플릿: `0x..` → `<HEX>`, 홀로 선 숫자 → `<N>`. `DQ13`, `Gen4`, `x2` 처럼 글자에 붙은 숫자는 신호 이름이라 남긴다.
- `signature(rec)` = `"test|reg|code|bits"`, `cluster(tests)` — FAIL/ERROR만 묶고 개수 내림차순. ID는 시그니처의 sha1 앞 8자리(주어짐).

### Part B — 상관과 판정

- `ver_key("1.10.0")` → `(1, 10, 0)` (문자열 비교 금지).
- `fisher_greater(a, n_in, k, n)` — 시도 n번 중 실패 k번이 무작위로 흩어졌을 때, 크기 n_in인 그룹에 실패가 a번 이상 몰릴 확률 (초기하분포 꼬리).
- `classify(trials, fail_is)` — 주어진 후보 조건(`board == v`, `chip == v`, `temp_c >= T`, `fw == v`, `fw >= v`)마다 2×2 표를 만들고, **purity ≥ 0.95 이고 p < α/m** 인 조건만 설명으로 인정한다. 결과: `BOARD / CHIP / TEMP_HIGH / FW / SPREAD / AMBIGUOUS / INSUFFICIENT`.
- 합성 데이터로 꼭 확인할 것: 칩을 옮기지 않은 보드 실패는 AMBIGUOUS(보드냐 칩이냐 못 가름), socket swap 후에도 보드를 따라가면 BOARD, 칩을 따라가면 CHIP, 한 보드가 105 °C에서만 돌았으면 AMBIGUOUS, 실패 1건이면 INSUFFICIENT, "40런 중 B2에서 4/4"는 보정 후 SPREAD.

### Part C — 야간 회귀 triage 리포트

- `corr_table(trials, fail_is, dim)` — 값별 `(값, 실패, 시도)`, fw는 버전 순.
- `triage(tests)` — 클러스터 → 분모 → 판정 → 요약 dict. `next_action(summary)` — BOARD는 swap, TEMP_HIGH는 shmoo, FW `>= v`는 "bisect 직전버전(good)..v(bad)", SPREAD는 flaky.
- 실제 데이터에서 **원인 4개를 정확한 조건까지** 되찾고, 에러코드 정규화를 빼면 가짜 "FW 1.4.2에서만" 클러스터가 생기는 것까지 CHECK 한다. `render_md`(주어짐)로 리포트를 찍는다.

## 2. Problem (English)

> **Nightly regression triage.**
>
> You get a JSON Lines log of a nightly validation campaign: one `run` header per run (`board`, `chip`, `temp_c`, `fw`, `night`, `seed`) followed by one `test` record per executed test (`status` ∈ PASS/FAIL/SKIP; failures add `code`, `reg`, `addr`, `exp`, `act`, `msg`). A few lines are truncated. Using the Python 3 standard library only:
>
> 1. Normalize each failure into a signature `test|reg|code|bits`: drop non-causal test parameters (keep `lane/ch/port`), canonicalize error codes (`E_TIMEOUT`, `ETIMEDOUT` → `TIMEOUT`), and replace `exp/act` by the set of differing bits (`"b5+b6"`, or `"multi"` if more than 4). Addresses, iteration counts and timestamps must not affect the signature. Cluster failures by signature; give each cluster a stable ID.
> 2. For each cluster, the denominator is the number of runs in which that test actually executed (SKIPs and lost lines excluded). For every candidate condition (`board == v`, `chip == v`, `temp_c >= T`, `fw == v`, `fw >= v`) compute the 2×2 table, the purity (fraction of the cluster's failures inside the condition) and a one-sided Fisher exact p-value. Accept a condition if purity ≥ 0.95 and p < α/m (α = 0.01, m = number of candidates). Report `BOARD/CHIP/TEMP_HIGH/FW` when exactly one dimension explains the cluster (pick the narrowest condition), `AMBIGUOUS` when several do, `SPREAD` when none does, `INSUFFICIENT` below 3 failures.
> 3. Produce a Markdown report: cluster table, fail-rate matrices by board/chip/temperature/firmware, and a next action per cluster.
>
> Example: a cluster `pcie.link.width|LNKSTA|LINK_DEGRADED|b5+b6` with 15/30 failures on `EVB-07` and 0/210 elsewhere — while EVB-07's chip was replaced mid-campaign and failures continued — must be classified `BOARD` with condition `board == EVB-07`, not `CHIP`.

## 3. 시작 코드

<!-- code: starter.py -->

데이터 생성기 (결정적). 레슨 폴더의 `nightly_regress.jsonl` 은 이것으로 만든 파일이다.

<!-- code: gen_logs.py -->

```bash
cd lessons/SIV/SIV-M10
python3 -I starter.py; echo "exit=$?"        # TODO 상태: 3/36 passed, exit=1
python3 -I gen_logs.py /tmp/SIV-M10_regen.jsonl && cmp nightly_regress.jsonl /tmp/SIV-M10_regen.jsonl && echo identical
```

실제 출력 (일부):

```text
-- Part A: normalize + signature --
FAIL  norm_code: E_TIMEOUT / ETIMEDOUT / timeout -> TIMEOUT
...
PASS  norm_test: plain name unchanged
...
-- Part C: nightly triage --
PASS  loader: 240 runs, 1918 test records, bad lines [778, 1501]
PASS  exposure excludes SKIP (EVB-04 has no USB PHY) -> usb.hs.enum 210 runs
FAIL  4 unique clusters (one per root cause)
FAIL  pcie.link.width -> BOARD 'board == EVB-07' (chip swap ruled out CHIP)  (KeyError: 'pcie.link.width')
...
3/36 passed
exit=1
```

TODO는 12개다: `norm_code`, `norm_test`, `bit_pattern`, `norm_msg`, `signature`, `cluster` (Part A), `ver_key`, `fisher_greater`, `classify` (Part B), `corr_table`, `triage`, `next_action` (Part C). 로더 `load_jsonl`, `sig_id`, 분모 `exposure`, 후보 생성 `candidates`, 2×2 계산 `evaluate`, 리포트 `render_md` 는 주어진다. 데이터 경로는 스크립트 위치 기준이라 어느 cwd에서 돌려도 된다.

## 4. 힌트

<details>
<summary>힌트 1 — 정규화는 "밤마다 바뀌나?" 한 질문으로</summary>

레코드의 필드마다 "같은 버그인데 다음 실패에서 값이 달라지나?"를 묻는다. 주소·반복 횟수·시간·시드는 바뀐다 → 빼거나 가린다. 레지스터 이름, 틀린 비트 위치는 안 바뀐다 → 남긴다. `exp/act` 원값은 데이터 패턴(0xAAAA…, 0x3C3C…)에 따라 바뀌지만 **XOR은 안 바뀐다** — DQ13이 0으로 빠지면 어느 패턴이든 diff는 `0x00002000`.

</details>

<details>
<summary>힌트 2 — 정규식 두 개, 순서가 중요</summary>

`norm_msg` 는 16진수를 먼저 `<HEX>` 로 바꾸고 그다음 `\b\d+\b` 를 `<N>` 으로. 순서를 바꾸면 `0x804FAE5C` 의 `0` 이 먼저 `<N>` 이 되어 `<N>x804FAE5C` 같은 쓰레기가 나온다. `\b` 덕분에 `DQ13` 의 13은 안 건드린다 (Q와 1 사이에는 단어 경계가 없다).

</details>

<details>
<summary>힌트 3 — Fisher 꼬리 확률은 math.comb 한 줄</summary>

`P(X >= a) = Σ_{x=a}^{min(k, n_in)} C(k, x)·C(n-k, n_in-x) / C(n, n_in)`. Python 정수는 무한 정밀도라 `C(240, 30)` 도 정확히 계산되고, 마지막 `/` 한 번만 float가 된다. 손 검산: n=10, k=3, n_in=3, a=3 → 1/C(10,3) = 1/120.

</details>

<details>
<summary>힌트 4 — "에서만"은 p가 아니라 purity가 말한다</summary>

p-value는 "이 그룹에 실패가 우연 이상으로 몰렸나"이지 "실패가 전부 이 그룹 안에 있나"가 아니다. EVB-07에 꽂혔던 칩 A0-0117 도 p=5.3e-7로 "유의"하다 — EVB-07의 부분집합이기 때문. 그래서 purity(= 그룹 안 실패 / 전체 실패) ≥ 0.95 를 먼저 요구하고, 같은 차원 안에서는 n_in이 가장 작은(가장 좁은) 조건을 고른다 (`temp_c >= 85` 가 `>= 60`, `>= 25` 를 이긴다).

</details>

<details>
<summary>힌트 5 — 차원이 둘 이상이면 고르지 말고 AMBIGUOUS</summary>

class별로 가장 좁은 설명 하나씩을 모은다. class가 하나면 그것, 둘 이상이면 데이터가 원인을 가르지 못한 것이다 (보드에 칩이 납땜돼 있으면 board와 chip은 항상 같은 런 집합). p 순으로 정렬해 `alts` 에 모두 남기고 판정은 `AMBIGUOUS` — 사람이 교차 실험을 설계하게 한다.

</details>

## 5. 풀이

### Part A — 무엇을 남기고 무엇을 지우나

필드별 처리 (이 레슨의 규칙):

| 필드 | 예 | 같은 버그인데 바뀌나 | 시그니처 처리 |
|---|---|---|---|
| `test` | `ddr.stress.march[seed=0xA866]` | 시드 파라미터가 매번 다름 | `norm_test`: `lane/ch/port` 만 남김 |
| `reg` | `DDR_RDATA`, `LNKSTA` | 아니오 | 그대로 |
| `code` | `E_TIMEOUT` / `ETIMEDOUT` | FW 버전마다 표기가 다름 | `norm_code`: `E_` 제거 + 별칭 표 |
| `exp`/`act` | `0xAAAAAAAA`/`0xAAAA8AAA` | 데이터 패턴마다 다름 | XOR → `b13` |
| `addr` | `0x822DCC4C` | 매번 다름 | 시그니처에서 제외 |
| `msg` | `iter 154 after 3431 ms` | 매번 다름 | 제외. 템플릿만 리포트에 |

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">원본 두 줄 → 같은 시그니처 (DQ13 이 고온에서 0 으로 빠짐)</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="10" y="50">test=ddr.stress.march[seed=0x0001] code=E_MISCOMPARE exp=0xAAAAAAAA act=0xAAAA8AAA addr=0x80000010</text>
    <text x="10" y="70">test=ddr.stress.march[seed=0xBEEF] code=E_MISCOMPARE exp=0xFFFFFFFF act=0xFFFFDFFF addr=0x81230000</text>
  </g>
  <g stroke="currentColor">
    <rect x="10" y="92" width="170" height="54" rx="6" class="f-acc"/>
    <rect x="190" y="92" width="120" height="54" rx="6" class="f-acc"/>
    <rect x="320" y="92" width="150" height="54" rx="6" class="f-acc"/>
    <rect x="480" y="92" width="120" height="54" rx="6" class="f-acc"/>
    <rect x="610" y="92" width="120" height="54" rx="6" class="f-mut" stroke-dasharray="4 3"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="95" y="112">norm_test</text><text x="95" y="132" font-family="monospace">ddr.stress.march</text>
    <text x="250" y="112">reg</text><text x="250" y="132" font-family="monospace">DDR_RDATA</text>
    <text x="395" y="112">norm_code</text><text x="395" y="132" font-family="monospace">MISCOMPARE</text>
    <text x="540" y="112">exp XOR act</text><text x="540" y="132" font-family="monospace">0x2000 → b13</text>
    <text x="670" y="112">addr · msg</text><text x="670" y="132">버림</text>
  </g>
  <rect x="10" y="166" width="590" height="34" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="20" y="188" fill="currentColor" font-family="monospace" font-size="12.5">ddr.stress.march|DDR_RDATA|MISCOMPARE|b13   →  id d9359f37 (sha1[:8])</text>
  <text x="10" y="226" fill="currentColor" font-size="12">act 끝을 0xAAAAAA8A 로 바꾸면 b5 → 다른 DQ, 다른 시그니처 (다른 버그를 합치지 않는다).</text>
  <text x="10" y="248" fill="currentColor" font-size="12">ID 는 입력만으로 정해진다 → 내일 밤에도 같은 ID → 버그 티켓과 자동 연결 (연습 3).</text>
</svg>
```

**너무 세게 vs 너무 약하게.** 정규화는 두 방향으로 다 틀릴 수 있다.

| 실수 | 결과 | 이 데이터에서 실제로 생기는 일 |
|---|---|---|
| 정규화 부족 (주소/시드를 남김) | 실패 하나 = 클러스터 하나 → triage 불가 | DDR 34건이 34개 클러스터 |
| 에러코드 별칭 미처리 | 한 원인이 FW별로 쪼개짐 → 가짜 FW 상관 | spi 가 2개로 쪼개지고 하나가 `fw == 1.4.2` 로 판정 (Part C) |
| 메시지까지 시그니처에 | 채널 번호(`ch0`~`ch3`)마다 쪼개짐 | irq 33건이 템플릿 4종 |
| `\d+` 를 경계 없이 가림 | `DQ13`·`DQ5` 가 같은 `DQ<N>` → 다른 비트 합쳐짐 | 다른 DQ 결함이 한 버그로 보임 |
| exp/act 원값 사용 | 데이터 패턴마다 쪼개짐 | DDR 이 패턴 5종으로 쪼개짐 |
| 비트 수까지 넣음 (`multi:7`) | 데이터 의존 개수로 쪼개짐 | 그래서 4비트 초과는 그냥 `multi` |

주소를 시그니처에서 뺀다고 주소 정보를 버리는 건 아니다. DRAM에서는 실패 주소가 한 row/bank에 몰리는지가 원인(셀 결함 vs 신호 무결성)을 가르므로, 클러스터 **안에서** 주소를 bank/row/column으로 디코드해 분포를 보는 2차 분석을 한다 (SIV-M05의 비트 오류 맵). 시그니처는 "같은 증상" 까지만 묶는 1차 키다.

**왜 결정적 ID인가.** `sig_id` 는 시그니처 문자열의 sha1 앞 8자리라서 같은 증상은 어느 밤, 어느 머신에서 돌려도 같은 ID다. 그래야 "3bff4eb5 = HW-1201 (EVB-07 PCIe 레인)" 같은 매핑을 버그 트래커에 두고 다음 날 아침 NEW/KNOWN/GONE 을 자동으로 가를 수 있다 (연습 3). 실행 순서에 따라 붙는 일련번호(`cluster #3`)는 이 용도로 쓸 수 없다.

### Part B — 분모, 2×2 표, 그리고 "에서만"

**분모가 먼저다.** "EVB-07에서 실패 15건"은 아무 의미가 없다 — EVB-07이 런을 제일 많이 돌았을 수도 있다. 분모는 *그 테스트가 실제로 실행된 횟수*다 (`exposure`). SKIP은 뺀다: EVB-04는 USB PHY가 없어 `usb.hs.enum` 이 항상 SKIP이므로 그 테스트의 분모는 240이 아니라 210이다. 잘린 줄 때문에 `spi.flash.read`, `irq.dma.done` 은 239다.

후보 조건 하나에 대한 2×2 표 (PCIe 클러스터, `board == EVB-07`):

| | 실패 | 통과 | 합 |
|---|---:|---:|---:|
| 그룹 안 (EVB-07) | a = 15 | 15 | n_in = 30 |
| 그룹 밖 | k − a = 0 | 210 | 210 |
| 합 | k = 15 | 225 | n = 240 |

- **purity** = a/k = 15/15 = 1.00 — 실패가 *전부* 그 그룹 안에 있나 ("에서만").
- **Fisher 단측 p** = P(X ≥ a), X ~ 초기하(n=240, k=15, n_in=30). "실패 15건이 240칸에 무작위로 흩어졌다면 30칸짜리 EVB-07 칸에 15건이 다 들어갈 확률" = C(30,15)/C(240,15) ≈ 6.3e-16. 행·열 합(margin)을 고정한 정확 검정이라 기대도수가 5 미만인 칸이 있어도 카이제곱 근사처럼 무너지지 않는다.
- **Bonferroni**: 후보가 m = 27개(보드 8 + 칩 9 + 온도 임계 5 + `fw ==` 3 + `fw >=` 2)이니 하나라도 우연히 p < 0.01이 될 확률을 0.01 이하로 묶으려면 각 후보의 기준을 0.01/27 = 3.7e-4 로 낮춘다.

**p만 보면 안 되는 이유** — 실제 PCIe 클러스터의 상위 후보 (프로브 스크립트로 출력한 값):

| 후보 | a / n_in | purity | p |
|---|---:|---:|---:|
| `board == EVB-07` | 15 / 30 | 1.00 | 6.3e-16 |
| `chip == A0-0117` (EVB-07, 1~2일째) | 7 / 12 | 0.47 | 5.3e-07 |
| `chip == A0-0142` (EVB-07, 3일째~) | 8 / 18 | 0.53 | 8.8e-07 |
| `fw == 1.5.1` | 9 / 81 | 0.60 | 2.9e-02 |

두 칩 모두 Bonferroni 기준(3.7e-4)을 넘는 "유의한" 상관이다 — EVB-07의 부분집합이라서. 하지만 각자 실패의 절반만 담는다. purity 조건이 "칩은 원인이 아니다, 칩을 바꿔도 계속 났다"를 정확히 표현한다. 이것이 3일째 socket swap의 값어치다: swap이 없었다면 `board == EVB-07` 과 `chip == A0-0117` 이 같은 런 집합이 되어 데이터로는 절대 못 가른다.

같은 차원 안에서는 **가장 좁은 조건**을 고른다. DDR 클러스터:

| 후보 | a / n_in | purity | p |
|---|---:|---:|---:|
| `temp_c >= 85` | 34 / 80 | 1.00 | 1.8e-19 |
| `temp_c >= 60` | 34 / 120 | 1.00 | 3.8e-12 |
| `temp_c >= 25` | 34 / 160 | 1.00 | 2.7e-07 |
| `temp_c >= 105` | 17 / 40 | 0.50 | 7.5e-07 |

`>= 85`, `>= 60`, `>= 25` 모두 순도 1이지만 n_in이 가장 작은 `>= 85` 가 경계를 정확히 짚는다. `>= 105` 는 p는 작아도 실패 절반을 놓친다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="sivm10-b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">classify(): 클러스터 하나의 판정 흐름</text>
  <rect x="10" y="40" width="170" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="95" y="67" text-anchor="middle" fill="currentColor">실패 k &lt; 3 ?</text>
  <rect x="10" y="110" width="170" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="95" y="135" text-anchor="middle" fill="currentColor">INSUFFICIENT</text>
  <rect x="220" y="40" width="250" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="345" y="60" text-anchor="middle" fill="currentColor">후보 m 개마다 2×2 표</text>
  <text x="345" y="77" text-anchor="middle" fill="currentColor" font-size="12">purity ≥ 0.95 그리고 p &lt; α/m ?</text>
  <rect x="220" y="110" width="250" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="345" y="135" text-anchor="middle" fill="currentColor">class 별 가장 좁은 것 1개 (n_in 최소)</text>
  <rect x="510" y="40" width="220" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="620" y="67" text-anchor="middle" fill="currentColor">통과 0개 → SPREAD</text>
  <rect x="220" y="180" width="160" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="300" y="201" text-anchor="middle" fill="currentColor">class 1개</text>
  <text x="300" y="220" text-anchor="middle" fill="currentColor" font-size="12">BOARD·CHIP·TEMP_HIGH·FW</text>
  <rect x="420" y="180" width="200" height="50" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="520" y="201" text-anchor="middle" fill="currentColor">class 2개 이상</text>
  <text x="520" y="220" text-anchor="middle" fill="currentColor" font-size="12">AMBIGUOUS (alts 전부 보고)</text>
  <g stroke="currentColor" fill="none" marker-end="url(#sivm10-b)">
    <path d="M95 84 L95 108"/><path d="M180 62 L218 62"/><path d="M470 62 L508 62"/>
    <path d="M345 84 L345 108"/><path d="M320 150 L300 178"/><path d="M370 150 L500 178"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="102" y="100">예</text><text x="186" y="56">아니오</text><text x="476" y="56">없음</text><text x="352" y="100">있음</text>
  </g>
  <text x="10" y="262" fill="currentColor" font-size="12">purity = "실패가 전부 이 조건 안에 있나"(에서만) · p = "우연으로 이만큼 몰릴 확률" · m = 후보 수 (다중 비교)</text>
  <text x="10" y="284" fill="currentColor" font-size="12">AMBIGUOUS 는 실패가 아니라 정직한 답이다: "이 데이터로는 못 가른다 → 이 실험을 해라".</text>
</svg>
```

합성 데이터로 확인한 판정 (모두 checked test, 숫자는 실제 실행 값):

| 시나리오 (보드 4장) | 결과 | 왜 |
|---|---|---|
| B3에서만 8/10 실패, 칩 고정 | `AMBIGUOUS` (board == B3 / chip == C3, 둘 다 p = 5.85e-07) | 보드와 칩이 같은 런 집합 |
| 같은데 B3 칩을 rep 3부터 C9로 swap | `BOARD board == B3` | swap 후에도 실패가 보드를 따라감 |
| 칩 C3를 B3 → B1 로 옮기고 실패가 따라감 | `CHIP chip == C3` | 보드 둘에 걸쳐 칩 하나 |
| 모든 보드에서 85 °C 이상에서만 | `TEMP_HIGH temp_c >= 85` | 가장 좁은 임계 |
| B3가 105 °C에서만 돌았음 | `AMBIGUOUS` (BOARD·CHIP·TEMP_HIGH 포함) | 보드와 온도가 완전 교란 |
| 실패 1건 | `INSUFFICIENT` | k < 3 |
| 40런 중 B2에서 4/4 | `SPREAD` | p = 0.0023 < 0.01 이지만 ≥ 0.01/11 = 9.1e-4 |

마지막 줄이 다중 비교의 요점이다. 후보 11개를 다 보면 그중 하나에 "4건이 몰리는" 일은 꽤 흔하다. 보정 없이 BOARD 라고 찍으면 엔지니어가 멀쩡한 보드를 뜯는다. 반대로 Bonferroni는 보수적이어서 진짜 원인도 표본이 적으면 놓친다 — 그래서 SPREAD/INSUFFICIENT 다음 액션은 "더 돌려라"다. (더 덜 보수적인 Holm, FDR(Benjamini–Hochberg)도 있다. triage 도구에서는 "틀린 보드를 뜯는 비용"이 커서 보수적 쪽이 보통 낫다.)

**한계를 알고 쓰기.** 이 판정은 *관찰* 데이터 위의 휴리스틱이다. 시도들이 독립이 아니고(같은 밤·같은 챔버), 온도는 챔버 설정값이지 접합 온도(Tj)가 아니며, 한 조건 안에서 다른 조건이 섞이는 상호작용(보드 × 온도)은 단일 차원으로 못 본다 (연습 2). 인과는 다음 실험(swap, 온도 스윕, bisect)이 확정한다. triage의 목표는 그 실험을 **올바르게 고르는 것**이다.

### Part C — 실제 야간 데이터 triage

```bash
python3 -I solution.py --report > /tmp/SIV-M10_report.md
```

실제 출력의 첫 줄과 클러스터 표 (Markdown 표는 그대로 옮겼다):

```text
runs=240 test_records=1918 failures=106 clusters=4 bad_lines=778,1501 orphans=0
```

| id | count | test | reg | code | bits | class | condition | p | rate in/out |
|---|---:|---|---|---|---|---|---|---:|---|
| d9359f37 | 34 | ddr.stress.march | DDR_RDATA | MISCOMPARE | b13 | TEMP_HIGH | temp_c >= 85 | 1.8e-19 | 34/80 vs 0/160 |
| 3f741e14 | 33 | irq.dma.done | DMA_INT_STATUS | TIMEOUT | b0 | FW | fw >= 1.5.0 | 3.3e-07 | 33/158 vs 0/81 |
| c0a72c69 | 24 | spi.flash.read | SPI_STATUS | TIMEOUT | b0 | SPREAD | - | - | 24/239 |
| 3bff4eb5 | 15 | pcie.link.width | LNKSTA | LINK_DEGRADED | b5+b6 | BOARD | board == EVB-07 | 6.3e-16 | 15/30 vs 0/210 |

리포트의 상관 표 중 칩·FW 두 개 (fails/runs, 0이 아닌 칸은 굵게):

| chip | d9359f37 | 3f741e14 | c0a72c69 | 3bff4eb5 |
|---|---:|---:|---:|---:|
| A0-0116 | **5/30** | **2/30** | **3/30** | 0/30 |
| A0-0117 | **1/12** | **1/12** | **4/11** | **7/12** |
| A0-0118 | **4/30** | **4/30** | **4/30** | 0/30 |
| A0-0142 | **3/18** | **2/17** | **1/18** | **8/18** |

| fw | d9359f37 | 3f741e14 | c0a72c69 | 3bff4eb5 |
|---|---:|---:|---:|---:|
| 1.4.2 | **14/82** | 0/81 | **11/81** | **3/82** |
| 1.5.0 | **7/77** | **14/77** | **3/77** | **3/77** |
| 1.5.1 | **13/81** | **19/81** | **10/81** | **9/81** |

(칩 표는 A0-0111~0115 행을 생략. 실제 리포트에는 보드·온도 표도 있다.)

클러스터별 해석과 다음 액션 (리포트의 `Next actions` 그대로):

- `d9359f37` ddr.stress.march — `temp_c >= 85 에서만: 그 온도에서 V×F shmoo (SIV-M07), 온도 경계 세분화`. 8장 모두에서 85·105 °C 각각 17/40, 그 아래 0/160. 틀린 비트가 항상 b13 하나 → "고온에서 DQ13 마진 부족" 가설. 다음: 85 °C에서 DQ13의 read/write 윈도우를 다시 보고(SIV-M05 training 결과), 60~85 °C를 촘촘히 스윕.
- `3f741e14` irq.dma.done — `FW 회귀: bisect 1.4.2(good)..1.5.0(bad) (SIV-M11)`. 1.4.2에서 0/81, 1.5.0 14/77, 1.5.1 19/81. `fw == 1.5.0` 은 purity 0.42라 탈락, `fw >= 1.5.0` 이 순도 1. 1.4.2와 1.5.0 사이 커밋을 bisect.
- `c0a72c69` spi.flash.read — `퍼진 간헐 실패: 재현율 측정·동시성 stress (SIV-M09), flaky 목록에 등록`. 어느 조건도 실패를 다 담지 못함 (가장 작은 p도 `chip == A0-0117` 의 1.6e-2, purity 0.17).
- `3bff4eb5` pcie.link.width — `board == EVB-07 에서만: 이 보드의 칩을 다른 보드로 옮겨(swap) 재현 확인, 보드 HW(레인·커넥터·전원) 점검`. LNKSTA(Link Status)의 [3:0] Current Link Speed, [9:4] Negotiated Link Width: `0x0044` = 속도 코드 4(16 GT/s)·x4, `0x0024` = 16 GT/s·x2 → diff `0x60` = b5+b6. 속도는 그대로, 폭만 반토막 = 일부 레인이 학습에 실패. 칩을 바꿔도 계속 → 보드 쪽 레인(AC 커플링 캡, 커넥터, 납땜)을 본다. (config space 파싱과 LTSSM 감시는 SIV-M04.)

**함정 재현: 에러코드 정규화를 빼면.** `--naive` 는 code를 원문 그대로 시그니처에 넣는다. 실제 출력:

| id | count | test | reg | code | bits | class | condition | p | rate in/out |
|---|---:|---|---|---|---|---|---|---:|---|
| bd2da61e | 13 | spi.flash.read | SPI_STATUS | ETIMEDOUT | b0 | SPREAD | - | - | 13/239 |
| 57c57db1 | 11 | spi.flash.read | SPI_STATUS | E_TIMEOUT | b0 | FW | fw == 1.4.2 | 4.2e-06 | 11/81 vs 0/158 |

SPI 클러스터가 둘로 쪼개지고, 그중 하나는 "FW 1.4.2에서만 나는 SPI 버그"(p = 4.2e-6, 순도 1)로 판정된다. 당연하다 — 1.4.2만 `E_TIMEOUT` 이라고 *찍기* 때문이다. 통계는 완벽하게 맞는데 결론은 틀렸다. 로그 포맷의 변화(FW가 에러 enum을 POSIX 스타일로 바꿈)가 그대로 가짜 상관이 된다. 그래서 정규화 별칭 표는 FW 릴리스 노트와 함께 관리하고, "FW 상관"이 나오면 **그 FW에서 로그 포맷·테스트 코드가 바뀌지 않았나**를 먼저 확인한다.

실행 시간: `python3 -I solution.py` 전체(36개 테스트, 1918 레코드 triage 2회 포함) 0.12 s (Apple Silicon, Python 3.9.6, `time` 실측). 수십만 줄이면 클러스터 단위 `exposure` 를 테스트 이름으로 미리 인덱싱하면 된다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py; echo "exit=$?"            # 36/36 passed, exit=0
python3 -I -W error -X dev solution.py            # 경고 없이 36/36
python3 -I solution.py --report                   # 위 Part C 리포트
python3 -I solution.py --report --naive           # 에러코드 정규화 없이 (함정)
```

실제 출력 (끝부분):

```text
-- Part C: nightly triage --
PASS  loader: 240 runs, 1918 test records, bad lines [778, 1501]
PASS  exposure excludes SKIP (EVB-04 has no USB PHY) -> usb.hs.enum 210 runs
PASS  4 unique clusters (one per root cause)
PASS  pcie.link.width -> BOARD 'board == EVB-07' (chip swap ruled out CHIP)
PASS  ddr.stress.march -> TEMP_HIGH 'temp_c >= 85'
PASS  irq.dma.done -> FW 'fw >= 1.5.0'
PASS  spi.flash.read -> one cluster, SPREAD
PASS  trap: without code normalization spi splits in 2 and one looks like 'fw == 1.4.2'
PASS  corr_table(board) for pcie: EVB-07 has all fails, others 0/30
PASS  corr_table(fw) sorted by version
PASS  next_action(FW) suggests bisect 1.4.2..1.5.0
PASS  report: cluster table row for pcie with BOARD
PASS  report: ids are deterministic (sha1 of signature)
36/36 passed
exit=0
```

데이터 재생성 확인: `gen_logs.py` 를 다시 돌린 파일과 커밋된 파일의 sha256이 같다 (`46c8c6b82cff5a97206935e4840837d8690118ae18edc876aa4e51c7b5b9cbc3`, `cmp` → identical).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 실패 건수만 세서 "EVB-07 15건" | 분모가 없으면 런 많이 돈 보드가 범인처럼 보인다 | `exposure()` 로 실행 횟수, `a/n_in vs (k-a)/(n-n_in)` |
| SKIP을 분모에 넣음 | USB 없는 보드가 "USB 통과율 높음"처럼 보임 | `status in ("PASS","FAIL","ERROR")` 만 |
| p-value만으로 "칩 상관" | EVB-07의 부분집합인 칩도 p = 5e-7 | purity ≥ 0.95 를 먼저 요구 |
| 후보 27개를 α = 0.01 그대로 | 하나쯤은 우연히 걸린다 (B2 4/4 사례) | `p < alpha / m` |
| `fw >= "1.5.0"` 문자열 비교 | `"1.10.0" < "1.9.2"` | `ver_key` 정수 튜플 |
| `re.sub(r"\d+", "<N>")` | `DQ13` → `DQ<N>`, 다른 비트가 한 클러스터로 | `\b\d+\b`, 16진수를 먼저 |
| 에러코드 원문을 시그니처에 | FW별 표기 차이 → 가짜 FW 상관 | `norm_code` + 별칭 표 |
| 파라미터를 다 버림 | `lane=0` 과 `lane=3` 결함이 합쳐짐 | `KEEP_PARAMS` 화이트리스트 |
| 여러 차원이 설명하면 p 최소 하나만 보고 | 보드·칩 교란을 "보드"로 단정 | class 2개 이상 → AMBIGUOUS + alts |
| 실행 순번으로 클러스터 ID | 밤마다 바뀌어 티켓 연결 불가 | sha1(시그니처)[:8] |
| 깨진 줄에서 예외로 전체 중단 | 콘솔 한 줄 끊김에 아침 리포트가 없음 | 줄 번호만 모으고 계속 (`bad_lines`) |

## 8. 면접 Q&A

<details>
<summary>Q1. 야간 회귀에서 실패 로그 3,000개를 받았습니다. 첫 30분에 무엇을 하나요?</summary>

① 파싱하고 깨진 줄·잘린 런을 센다 (리포트가 믿을 만한지부터). ② 실패를 정규화된 시그니처로 묶어 **고유 원인 수**를 본다 — 3,000개가 보통 한 자릿수~수십 개 클러스터가 된다. ③ 큰 클러스터부터 분모 있는 조건별 실패율(보드/칩/온도/FW/전압)을 본다. ④ 각 클러스터를 "어떤 조건에서만"으로 분류하고 다음 실험을 배정한다: 보드 → swap, 온도 → shmoo, FW → bisect, 퍼짐 → flaky 추적. ⑤ 이미 아는 시그니처(KNOWN)와 새것(NEW)을 가른다 — 아침 회의는 NEW만 다룬다.
꼬리질문: "시그니처에 실패 주소를 넣나요?" → 1차 키에는 넣지 않는다(매번 달라 클러스터가 폭발). 대신 클러스터 안에서 주소를 bank/row/column으로 디코드한 분포를 본다 — 한 row에 몰리면 셀 결함, 퍼지면 신호 무결성·타이밍 쪽.

</details>

<details>
<summary>Q2. 서로 다른 버그 두 개가 같은 시그니처로 합쳐졌다는 걸 어떻게 알아채나요?</summary>

클러스터의 조건 분포가 "혼합" 모양이 된다: 어떤 조건도 순도가 높지 않은데 두 개의 서로 다른 조건(예: `board == EVB-07` 과 `temp_c >= 85`)이 각각 유의하게 몰린다. 또는 메시지 템플릿·실패 주소 분포가 두 덩어리로 갈린다. 그러면 시그니처에 특징을 하나 더 넣는다 (레인 번호 파라미터를 KEEP_PARAMS에, 비트 패턴을 더 세밀하게). 반대 방향(한 버그가 여러 클러스터)은 클러스터들이 같은 조건 분포·같은 시점에 나타나는 것으로 보이고, 별칭 표나 파라미터 정규화로 합친다.
꼬리질문: "그럼 메시지 전체를 클러스터링(Drain 같은 로그 템플릿 마이닝)하면 안 되나요?" → 자유 텍스트 로그에는 좋다. 검증 로그는 이미 구조화(reg/exp/act/code)돼 있으니 구조화 필드가 더 정확하고, 템플릿은 보조(리포트 예시, 서브 클러스터)로 쓴다.

</details>

<details>
<summary>Q3. "EVB-07에서만 실패"라고 했는데, 보드 문제인지 칩 문제인지 어떻게 확신하나요?</summary>

보드에 칩이 고정돼 있으면 데이터로는 **절대 못 가른다** — 두 조건이 같은 런 집합이라 어떤 통계도 같은 숫자를 낸다 (이 레슨의 합성 사례에서 둘 다 p = 5.85e-07, AMBIGUOUS). 가르는 방법은 실험이다: 소켓 보드면 칩을 다른 보드로 옮기고 다른 칩을 EVB-07에 꽂는다. 실패가 보드에 남으면 보드(레인 배선, AC 커플링 캡, 커넥터, 전원), 칩을 따라가면 칩(→ ATE 데이터, FA). 이 데이터에서는 3일째 swap 후에도 EVB-07에서 8/18 실패해서 BOARD로 판정됐다.
꼬리질문: "BGA로 납땜돼 소켓이 없으면?" → 같은 웨이퍼·로트의 다른 칩이 실린 보드 여러 장과 비교하고, 보드 쪽은 레인별 eye/BER 측정(SIV-M06), 커넥터·케이블 교체, 다른 엔드포인트 카드로 교차한다.

</details>

<details>
<summary>Q4. p-value가 작으면 원인이라고 해도 되나요? 다중 비교는 어떻게 다루나요?</summary>

아니다. ① p는 "실패가 이 조건에 우연 이상으로 몰렸다"일 뿐 "이 조건에서만"이 아니다 — 원인 그룹의 부분집합(EVB-07에 꽂혔던 칩)도 p가 작다. 그래서 순도를 같이 본다. ② 후보를 m개 보면 우연히 하나쯤은 p < α가 된다 → Bonferroni(α/m)나 Holm, FDR로 보정한다. 40런 중 한 보드에 4/4가 몰린 사례는 p = 0.0023으로 α = 0.01은 통과하지만 후보 11개 보정 기준 9.1e-4는 못 넘는다. ③ 관찰 데이터의 상관은 인과가 아니다 — 교란(보드가 고온에서만 돌았음)을 찾고, 확정은 실험으로 한다.
꼬리질문: "Bonferroni가 너무 보수적이라 진짜 원인을 놓치면?" → 놓친 클러스터는 SPREAD/INSUFFICIENT로 남고 다음 액션이 "반복 실행"이다. 런을 더 모으면 진짜 원인은 p가 지수적으로 작아져 결국 넘는다 (이 데이터의 PCIe는 6.3e-16).

</details>

<details>
<summary>Q5. DDR 미스컴페어가 85 °C 이상에서만 납니다. 다음에 뭘 하나요?</summary>

① 경계를 좁힌다: 60~85 °C를 5 °C 간격으로. ② 그 온도에서 전압×주파수 shmoo(SIV-M07)를 그려 마진이 어디서 닫히는지 본다. ③ 틀린 비트가 항상 DQ13 하나면 그 DQ의 training 결과(읽기/쓰기 DQS 윈도우, SIV-M05)를 고온/상온에서 비교 — 한 비트만 윈도우가 좁으면 그 신호의 PCB 배선·패키지 쪽, 모든 DQ가 고르게 줄면 컨트롤러/PHY 타이밍이나 refresh 쪽. ④ 고온 refresh 설정(고온에서 refresh 주기를 줄이는 동작)이 FW에서 켜져 있는지 확인.
꼬리질문: "챔버 85 °C면 칩은 몇 도인가요?" → 챔버 설정값은 공기 온도다. 접합 온도는 소비 전력 × 열저항만큼 더 높다. 온도 센서(on-die thermal sensor)를 같이 로그에 남겨 Tj 기준으로 다시 그려야 경계가 정확하다.

</details>

<details>
<summary>Q6. 다른 보드에서는 0/210이었습니다. "다른 보드는 문제없다"고 말해도 되나요?</summary>

"0건"은 "0%"가 아니다. 실패율 p에서 n번 연속 통과할 확률은 (1−p)^n이고, 이것을 1−CL로 놓으면 95% 단측 상한은 p ≤ 1 − 0.05^(1/n) ≈ 3/n (rule of three). n = 210이면 1.42% — "다른 보드의 실패율은 95% 신뢰도로 1.4% 미만"이 정확한 표현이다. 실패율 1% 미만을 주장하려면 0건으로 299런이 필요하다 (연습 4).
꼬리질문: "SerDes BER과 같은 얘기인가요?" → 같은 식이다. 오류 0개로 BER < 1e-12를 95%로 주장하려면 N ≥ −ln(0.05)/1e-12 ≈ 3.0e12 비트 (SIV-M06). 작은 p에서 −ln(1−p) ≈ p 이므로 런 수 식과 같은 꼴이다.

</details>

<details>
<summary>Q7. FW 버전 상관이 나왔습니다. 가짜일 수 있는 경우는?</summary>

① 로그 포맷이 바뀐 경우 — 이 레슨의 함정처럼 에러코드 이름이 바뀌면 시그니처가 FW별로 쪼개져 각자 "그 FW에서만"이 된다. ② 테스트 코드 자체가 FW와 같이 바뀐 경우 (timeout 값, 판정 기준). ③ FW 배포가 다른 조건과 교란된 경우 — 새 FW를 새 보드에만 깔았다면 FW와 보드를 못 가른다. 그래서 회귀 팜은 매 런 FW 빌드를 무작위로(또는 교차로) 배정한다. ④ 버전 비교를 문자열로 해서 `>=` 조건 자체가 틀린 경우 (`"1.10.0" < "1.9.2"`).
꼬리질문: "진짜 FW 회귀면?" → `fw >= v` 로 판정되면 직전 버전이 last good, v가 first bad → 그 사이 커밋을 `git bisect run` (SIV-M11). 실패율이 21%(33/158)처럼 간헐적이면 커밋마다 여러 번 돌려야 bisect가 틀리지 않는다 — 한 번 통과는 good의 증거가 약하다 (0.79^10 ≈ 9% 확률로 10번 연속 통과).

</details>

<details>
<summary>Q8. SPREAD(퍼진 간헐 실패)는 어떻게 다루나요? 그냥 flaky로 무시하나요?</summary>

무시하지 않는다. ① 재현율을 측정한다 (24/239 ≈ 10%). ② 모든 조건에 고르게 퍼졌다는 것 자체가 정보다 — HW 조건이 아니라 타이밍·동시성(공유 버스, 인터럽트 경합) 쪽일 가능성이 크다 → 동시성 stress(SIV-M09)로 재현율을 올려 본다. ③ CI 게이트에서는 "알려진 flaky(KNOWN 시그니처)"를 격리(quarantine)하되, 재현율이 올라가면 경보를 낸다. ④ 시그니처가 하나라도 바뀌면(NEW) 다시 triage.
꼬리질문: "재시도해서 통과하면 PASS로 처리하나요?" → 런 판정은 sticky fail(SIV-B14)이 원칙이다. 재시도 통과는 "간헐"이라는 정보로 남기고, 마진 문제(전압·온도 경계)일 수 있으니 shmoo로 넘긴다.

</details>

## 9. 추가 연습

### 연습 1 — 저온 쪽 임계 (TEMP_LOW)

후보에 `temp_c <= T` 를 추가해 "−40·0 °C에서만" 나는 실패를 `TEMP_LOW temp_c <= 0` 으로 판정하라. 기존 후보만으로는 같은 데이터가 SPREAD가 되는 것도 확인하고, 야간 데이터의 4개 판정은 그대로인지 확인한다.

<details>
<summary>답 보기</summary>

`candidates_cold` = 기존 후보 + 정렬된 온도 중 마지막(최고)을 뺀 각 T에 대해 `("TEMP_LOW", "temp_c <= T", ...)`. 최고 온도를 빼는 이유는 `temp_c <= 105` 가 모든 시도를 포함하는 자명한 조건이라서다. 후보가 6개 늘어 m도 커지므로(Bonferroni가 조금 더 엄격해짐) 기존 판정이 바뀌지 않는지 재확인하는 CHECK가 필요하다. 저온 실패의 예: 저온에서 PLL lock 시간 증가, 플래시 쓰기 실패, 커넥터 접촉, 결로.

</details>

### 연습 2 — 상호작용: "B3 이면서 85 °C 이상에서만"

B3에서, 그리고 85 °C 이상에서만 실패하는 합성 데이터에서 단일 차원 `classify` 는 AMBIGUOUS가 된다. 1차 설명 A 안에서 다른 차원 조건 B가 실패를 다시 가르는지 보는 드릴다운 `classify2` 를 만들어 `INTERACTION board == B3 and temp_c >= 85` 를 내라. 단, B3가 105 °C에서만 돌았던 교란 데이터는 계속 AMBIGUOUS여야 한다.

<details>
<summary>답 보기</summary>

`classify` 의 `alts[0]`(p 최소 설명) A를 잡고, A에 속한 시도만으로 `candidates(A)` 를 다시 만든다 (A와 같은 class는 제외). A 안의 실패만으로 purity와 조건부 Fisher p를 계산하고 A 안 후보 수로 Bonferroni. 통과하는 가장 좁은 B가 있으면 `"A and B"`. 교란 데이터에서는 A(B3) 안의 온도가 105 하나뿐이라 온도 후보가 아예 안 만들어진다 (자명한 조건 제거) → AMBIGUOUS 유지. 실제 출력: `classify2 -> INTERACTION board == B3 and temp_c >= 85 (dims BOARD×TEMP_HIGH)`. 이 합성 데이터는 칩이 고정이라 엄밀히는 `chip == C3 and temp_c >= 85` 와도 못 가른다는 점을 리포트에 같이 남겨야 한다. 드릴다운이 야간 데이터 4개 클러스터에서 가짜 상호작용을 만들지 않는 것도 CHECK 한다. 이것은 결정 트리(decision tree)의 깊이 2와 같은 발상이다.

</details>

### 연습 3 — 베이스라인 대비 NEW / KNOWN / GONE

`diff_baseline(tonight_ids, baseline)` — 베이스라인은 `{ID: 티켓}`. 오늘 밤 ID가 베이스라인에 있으면 `KNOWN <티켓>`, 없으면 `NEW`, 베이스라인에만 있으면 `GONE <티켓>`.

<details>
<summary>답 보기</summary>

dict 두 번 순회. 야간 데이터의 ID 4개는 항상 `3bff4eb5, 3f741e14, c0a72c69, d9359f37` 이다 (sha1이라 어디서 돌려도 같음). 베이스라인 `{3bff4eb5: HW-1201, c0a72c69: SW-88, 0badc0de: HW-999}` 이면 PCIe·SPI는 KNOWN, DDR·IRQ는 NEW, `0badc0de` 는 GONE. GONE은 "고쳐졌다"의 증거가 아니라 "오늘 안 났다"일 뿐이다 — 재현율이 10%면 하룻밤 0건은 흔하다 (연습 4의 상한으로 판단).

</details>

### 연습 4 — 실패 0건일 때 실패율 상한

`upper_zero(n, cl=0.95)` 와 `runs_needed(p, cl=0.95)` 를 구현하라. 다른 보드의 PCIe 0/210 에 대한 상한과 rule of three(3/n)를 비교하고, 실패율 1%·0.1% 미만을 주장하는 데 필요한 런 수를 구하라.

<details>
<summary>답 보기</summary>

(1−p)^n = 1−CL → p = 1 − (1−CL)^(1/n). n = 210 → 0.014164 (3/210 = 0.014286; −ln(0.05) = 2.996 ≈ 3 이라서 rule of three). 필요한 런 수 n = ⌈ln(1−CL)/ln(1−p)⌉ → p = 1%면 ⌈298.07⌉ = 299, 0.1%면 ⌈2994.2⌉ = 2995. 같은 식이 BER에서는 −ln(0.05)/1e-12 ≈ 3.0e12 비트다 (SIV-M06). 출처: Hanley & Lippman-Hand (1983).

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py; echo "exit=$?"     # 12/12 passed, exit=0
```

```text
-- 연습 2: BOARD x TEMP interaction --
      classify2 -> INTERACTION board == B3 and temp_c >= 85 (dims BOARD×TEMP_HIGH)
PASS  fails only on B3 at >= 85 -> INTERACTION 'board == B3 and temp_c >= 85'
PASS  single-dimension classify alone says AMBIGUOUS
PASS  B3 only ever ran at 105C -> stays AMBIGUOUS (cannot separate)
PASS  nightly clusters: drill-down finds no false interaction
...
12/12 passed
exit=0
```

## 10. 핵심 정리

- triage = **정규화 → 시그니처 → 분모 있는 상관 → 판정 → 다음 실험**. 줄을 읽지 말고 원인 수를 세라.
- 시그니처 = `test|reg|code|bits`. 밤마다 바뀌는 값(주소·시드·반복·시간·데이터 패턴)은 빼고, exp/act는 XOR 비트로, 에러코드는 별칭 표로, 파라미터는 화이트리스트로.
- ID는 시그니처의 해시 → 밤이 바뀌어도 같은 ID → 티켓 연결, NEW/KNOWN/GONE.
- 분모는 "그 테스트가 실제로 실행된 횟수" (SKIP·유실 제외). 실패 건수만으로는 아무것도 말할 수 없다.
- "에서만" = **purity**(실패가 전부 그 조건 안) + **Fisher 단측 p** < α/m (Bonferroni). 같은 차원에서는 가장 좁은 조건.
- 두 차원 이상이 설명하면 AMBIGUOUS — 보드와 칩은 swap 없이는 못 가른다. 통계가 아니라 실험이 가른다.
- 0/n 은 0%가 아니다: 상한 ≈ 3/n. 가짜 FW 상관의 1순위 용의자는 로그 포맷 변경.

## 참고자료

- R. A. Fisher, *The Design of Experiments* (1935) — exact test (차 맛보기 실험); 초기하분포 꼬리 확률
- [Python `math.comb`](https://docs.python.org/3/library/math.html#math.comb) · [`hashlib`](https://docs.python.org/3/library/hashlib.html) · [`json`](https://docs.python.org/3/library/json.html) · [`re`](https://docs.python.org/3/library/re.html)
- Bonferroni 보정, Holm (1979) "A simple sequentially rejective multiple test procedure", Benjamini & Hochberg (1995) FDR
- J. A. Hanley, A. Lippman-Hand, "If nothing goes wrong, is everything all right? Interpreting zero numerators", *JAMA* 249(13), 1983 — rule of three
- P. He, J. Zhu, Z. Zheng, M. R. Lyu, "Drain: An Online Log Parsing Approach with Fixed Depth Tree", ICWS 2017 — 자유 텍스트 로그 템플릿 마이닝
- PCI Express Base Specification — PCI Express Capability의 Link Status 레지스터 (Current Link Speed [3:0], Negotiated Link Width [9:4])
- P. Mishra, F. Farahmandi (eds.), *Post-Silicon Validation and Debug*, Springer 2019
- 관련 레슨: SIV-B14 (로그 → 리포트, sticky fail), SIV-M04 (PCIe 링크), SIV-M05 (DDR training), SIV-M06 (BER 신뢰구간), SIV-M07 (shmoo), SIV-M09 (동시성 stress), SIV-M11 (bisect)
