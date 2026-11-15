---
id: SIV-A10
time: 110분
updated: 2026-11-08
---
# SIV-A10 LFSR BIST와 MISR 시그니처

## 학습 목표

- 원시 다항식 LFSR 을 Galois/Fibonacci 두 형태로 구현하고 주기 `2^n-1` 을 시험으로 확인하며, **왜 원시(primitive)여야 하는지** 와 비원시 다항식이 어떻게 깨지는지(주기 5, CRC-16-CCITT 의 주기 32767)를 말할 수 있다.
- 조합 회로 넷리스트 모델에 net stuck-at 결함을 넣어 **검출률 곡선(패턴 수 vs 커버리지)** 을 재고, 전수 시험으로 "검출 불가능한 결함(redundant)" 과 "아직 못 잡은 결함" 을 구별한다.
- MISR 압축의 **aliasing** 을 정확 열거와 Monte-Carlo 로 측정해 `2^-n` 과 비교하고, 그 수치가 *균등 난수 오류* 에 대한 것임을, 그리고 **단일 비트 오류는 절대 alias 되지 않고 2비트 오류는 구조적으로 된다** 는 것을 증명·측정으로 구분한다.
- 넓은 AND 같은 **random-pattern-resistant** 결함의 패턴 수를 재고, weighted pattern 과 test point 가 왜 필요한지, 그 대가가 무엇인지 말할 수 있다.
- 펌웨어 관점에서 LBIST 를 돌리는 시퀀스(사전 점검 → 설정·readback → 시작 → poll+timeout → 비교 → 재실행 분류)를 mock 레지스터로 짜고, done-bit 고장·틀린 시그니처·X-source 를 **원인이 구별되는 리포트** 로 낸다.

## 1. 문제

자동차·서버 SoC 는 양산 후에도 부팅마다, 혹은 주기적으로 논리를 스스로 시험한다(ISO 26262 의 잠재 결함 검출) — 그 핵심인 LBIST(Logic Built-In Self-Test) 를 검증 엔지니어가 설명하고, 시그니처 불일치를 받았을 때 무엇을 먼저 의심할지 답하는 질문이 실리콘 검증 면접에 그대로 나온다.

SIV-B07 의 March 는 **RAM** 시험이고, SIV-M06 의 PRBS 는 **링크** 시험이다. 여기서는 **조합 논리** 를 시험한다: 칩 안에 패턴 발생기(PRPG, 보통 LFSR)와 응답 압축기(MISR)를 넣어, ATE 없이 칩이 스스로 수백만 패턴을 돌리고 16~64 비트 시그니처 하나만 내놓게 한다. 이 레슨은 그 세 부품과 펌웨어 쪽 시퀀스를 C 로 만들어 *직접 잰다*.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a10a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">LBIST0 구조: 펌웨어는 레지스터만 만진다</text>
  <rect x="10" y="40" width="720" height="60" class="f-mut"/>
  <text x="20" y="62" fill="currentColor">펌웨어: CTRL(START, XMASK_EN, ABORT) · STATUS(BUSY, DONE W1C) · SEED · PATCNT · XMASK · SIG(RO)</text>
  <text x="20" y="84" fill="currentColor">golden 표: (seed, patcnt, xmask) → 시그니처 (pre-silicon 시뮬이 만든다)</text>
  <line x1="130" y1="100" x2="130" y2="133" stroke="currentColor" marker-end="url(#a10a)"/>
  <line x1="600" y1="100" x2="600" y2="133" stroke="currentColor" marker-end="url(#a10a)"/>
  <rect x="50" y="135" width="160" height="60" class="f-acc"/>
  <text x="130" y="160" text-anchor="middle" fill="currentColor">PRPG (LFSR16)</text>
  <text x="130" y="178" text-anchor="middle" fill="currentColor" font-size="12">원시 다항식, 시드 ≠ 0</text>
  <line x1="210" y1="165" x2="268" y2="165" stroke="currentColor" marker-end="url(#a10a)"/>
  <text x="238" y="155" text-anchor="middle" fill="currentColor" font-size="12">패턴</text>
  <rect x="270" y="135" width="150" height="60" class="f-warn"/>
  <text x="345" y="160" text-anchor="middle" fill="currentColor">CUT (mini-ALU)</text>
  <text x="345" y="178" text-anchor="middle" fill="currentColor" font-size="12">net stuck-at 주입</text>
  <line x1="420" y1="165" x2="478" y2="165" stroke="currentColor" marker-end="url(#a10a)"/>
  <text x="450" y="155" text-anchor="middle" fill="currentColor" font-size="12">응답</text>
  <rect x="480" y="135" width="130" height="60" class="f-acc"/>
  <text x="545" y="160" text-anchor="middle" fill="currentColor">MISR16</text>
  <text x="545" y="178" text-anchor="middle" fill="currentColor" font-size="12">시그니처</text>
  <line x1="610" y1="165" x2="668" y2="165" stroke="currentColor" marker-end="url(#a10a)"/>
  <rect x="670" y="135" width="60" height="60" class="f-ok"/>
  <text x="700" y="162" text-anchor="middle" fill="currentColor" font-size="12">==</text>
  <text x="700" y="180" text-anchor="middle" fill="currentColor" font-size="12">golden?</text>
  <rect x="270" y="210" width="150" height="40" class="f-mut"/>
  <text x="345" y="235" text-anchor="middle" fill="currentColor" font-size="12">X-source (bit6) → XMASK</text>
  <line x1="420" y1="230" x2="488" y2="196" stroke="currentColor" marker-end="url(#a10a)"/>
  <text x="10" y="272" fill="currentColor" font-size="12">압축 비율이 엄청나게 크므로(수천 패턴 → 16비트) 정보가 줄어들고, 그 대가가 aliasing 이다.</text>
</svg>
```

실제 LBIST 는 scan 기반(STUMPS: 스캔 체인을 LFSR 로 채우고 MISR 로 받기, 위상 편이기 phase shifter 와 X-마스킹 포함)이고, 이 레슨의 모델은 조합 회로에 패턴을 직접 넣는 test-per-clock 형태다. 수학(주기, 선형성, aliasing)과 펌웨어 시퀀스는 같고, 스캔 시프트·캡처 타이밍·전력 문제는 Q&A 에서 말로 다룬다.

mock `LBIST0` 레지스터 맵(이 레슨의 가상 IP — SIV-B02 의 mock, SIV-B09 의 가짜 클럭을 축약해 넣었다):

| off | 이름 | access | 의미 |
|---|---|---|---|
| `0x00` | `CTRL` | RW | bit0 `START`(1 쓰면 시작, self-clear) · bit1 `XMASK_EN` · bit2 `ABORT` |
| `0x04` | `STATUS` | bit0 RO / bit1 W1C | bit0 `BUSY` · bit1 `DONE` |
| `0x08` | `SEED` | RW | LFSR 시드 |
| `0x0C` | `PATCNT` | RW | 패턴 수 |
| `0x10` | `XMASK` | RW | 응답에서 지울 비트 (`XMASK_EN` 일 때) |
| `0x14` | `SIG` | RO | 시그니처 (DONE 이후 유효) |

응답은 CUT 출력 6비트(Y[3:0], COUT, ZERO) + bit6 X-source(실행마다 값이 다른 미초기화 래치 출력). 1패턴 = 1사이클, 오버헤드 32사이클, MMIO 접근 8사이클(가짜 클럭, 대표값).

### Part A — PRPG, CUT, MISR, 커버리지 곡선

- `galois_step` / `fib_step` / `lfsr_period`: 다항식 표현은 `poly` 의 bit i = x^i 계수. n = 2..16, 20 의 원시 다항식에 대해 주기 `2^n-1` 을 확인한다. Galois 와 Fibonacci 가 같은 m-sequence(위상만 다름)임을 확인한다.
- `cut_eval(c, pi, f)`: 위상순 넷리스트 평가 + 넷 stuck-at 결함 주입.
- `misr_step`, `bist_signature`: `sig ← x·sig mod P ⊕ resp`.
- `first_detect`, `coverage_curve`, `bist_campaign`: 결함마다 첫 검출 패턴, 패턴 수별 커버리지, 시그니처 판정 vs 직접 비교.
- CUT = 4비트 mini-ALU(11 PI, 6 PO, 게이트 63개). ZERO 경로에 의도적 중복 게이트를 넣어 **정말 검출 불가능한 결함** 을 만들었다.

### Part B — aliasing 측정, 패턴 저항 결함

- `err_signature`, `alias_exact`, `alias_mc`, `single_bit_aliases`.
- 전수: `(n, m, L)` 표에 대해 alias 개수 = `2^(mL-n) - 1` 을 정확히 확인. MC: n = 4..12, 2^20 스트림, 4σ 허용.
- 구조적 aliasing: SISR 의 "주기 배수 간격" 2비트 오류, MISR 의 대각선 상쇄를 직접 시험.
- 결함 단위 aliasing: mini-ALU 의 실제 결함 오류 스트림으로 측정, 2^-n 과 비교(균등 가정이 깨진다는 점 포함).
- random-pattern-resistant: AND16/OR16, weighted 7/8 · 1/8, test point(control 2 + observe 2).

### Part C — 펌웨어 BIST 시퀀스 (mock `LBIST0`)

- `lbist_run(cfg, tbl, ntbl, r)`: 설정 검사 → golden 조회 → PRECHECK(BUSY, stale DONE clear & 확인) → 설정 readback → START → poll + timeout → 시간 하한 검사 → SIG 읽기 → DONE clear → 비교 → 불일치 시 1회 재실행 분류.
- `lbist_locate`: 체크포인트 패턴 수로 첫 불일치 구간 찾기.
- `lbist_format`: 한 줄 리포트.
- mock 결함: `DONE_STUCK1`, `DONE_STUCK0`, `BUSY_STUCK1`, `BUSY_HANG`, `EARLY_DONE`, `SEED_RO`, `LOGIC`(CUT stuck-at), `XMASK_BROKEN`, `SIG_BIT`. 각각 정확한 `lbist_status_t` 로 분류돼야 한다.

## 2. Problem (English)

> **LFSR BIST and MISR signature analysis.**
>
> 1. Implement `uint32_t galois_step(uint32_t s, uint32_t poly, int n)` (left-shift form: state as a polynomial, `s ← x·s mod P`) and `uint32_t fib_step(uint32_t s, uint32_t poly, int n)` (external-XOR form, bit0 newest). Implement `uint64_t lfsr_period(step_fn f, uint32_t poly, int n, uint32_t seed)`: steps until the state returns to `seed`, or 0 if it does not within 2^n steps. `poly` includes bits n and 0.
> 2. Implement a levelized combinational circuit evaluator with a single net stuck-at fault, `uint32_t cut_eval(const cut_t *c, uint32_t pi, fault_t f)`, a MISR `sig' = x·sig mod P ^ resp`, and `bist_signature` / `first_detect` / `coverage_curve` / `bist_campaign`.
> 3. Measure aliasing: for an n-bit signature register over all error streams of m bits × L cycles, count how many non-zero streams give a zero signature. Show it equals `2^(mL-n) - 1` and approaches a fraction `2^-n`. Show no single-bit error can alias, and exhibit two-bit errors that do.
> 4. Measure random-pattern resistance: coverage of a 16-input AND under 2048 uniform patterns vs weighted patterns vs test points.
> 5. Implement `lbist_status_t lbist_run(const lbist_cfg_t *cfg, const golden_t *tbl, int ntbl, lbist_result_t *r)` for a mock on-chip LBIST controller with START/BUSY/DONE/SEED/PATCNT/XMASK/SIG registers. Handle a stale or stuck DONE bit, timeout with abort, an early DONE (signature not trustworthy), config readback failure, unstable signatures (X leakage), and a reproducible wrong signature. Also implement `lbist_locate()` (first failing checkpoint window) and a single-line report.
>
> Example: if `DONE` is stuck high, the run must fail in `PRECHECK` with cause `DONE_STUCK` without starting the engine. If the XMASK hardware is broken and the X bit reaches the MISR, two runs give different signatures: cause `UNSTABLE`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 깨짐 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

실제 결과 (앞과 끝만):

```text
-- Part A: LFSR --
FAIL starter.c:405  lfsr_period(galois_step, PRIM[i].poly, PRIM[i].n, 1) == exp
FAIL starter.c:406  lfsr_period(fib_step, PRIM[i].poly, PRIM[i].n, 1) == exp
...
FAIL starter.c:782  lbist_locate(&cfg, tbl, nt, LB_CPS, NLB_CPS, &lo, &hi) == -1
FAIL starter.c:790  need > (int)sizeof tiny && strlen(tiny) == sizeof tiny - 1
26/120 passed
```

exit 1. 통과하는 26개는 스텁의 0 이 기대값과 우연히 맞는 것들(`err_signature == 0` 인 alias 시험, 커버리지 0 == 검출가능 0, 시작도 안 하는 잘못된 설정 거절 등)과 표·넷리스트 자체의 sanity 다 — 스텁이 전부 `0` 이니 "0 이 맞는 시험" 은 통과한다. 넷리스트 빌더, mock, golden 표, 시험 코드는 주어진다. 채울 곳: `galois_step`, `fib_step`, `lfsr_period`, `ps_next`, `cut_eval`, `misr_step`, `bist_signature`, `first_detect`, `coverage_curve`, `bist_campaign`, `err_signature`, `alias_exact`, `alias_mc`, `single_bit_aliases`, `lbist_run`, `lbist_locate`, `lbist_format`.

## 4. 힌트

<details>
<summary>힌트 1 — Galois 를 "다항식 곱하기 x" 로 읽기</summary>

상태 `s` 를 다항식 `S(x)` 라고 보면 왼쪽 시프트가 `x·S(x)`, bit n 이 서면 차수 n 항이 생긴 것이니 `P(x)` 를 더해(XOR) 차수를 내린다. MISR 도 같은 스텝 뒤에 응답을 XOR 할 뿐이다: `sig' = x·sig + r (mod P)`. 이 한 식에서 선형성, 시그니처 = 나머지, aliasing 이 전부 나온다.

</details>

<details>
<summary>힌트 2 — Fibonacci 탭 위치</summary>

점화식은 `a[t+n] = Σ c_i a[t+i]` 이다 (P = x^n + Σ c_i x^i). 상태 bit0 = 최신 = `a[t+n-1]` 이므로 `a[t+i]` 는 bit(n-1-i) 에 있다. 즉 다항식 계수 c_i 의 탭은 bit(n-1-i). 흔한 실수는 이걸 뒤집어 역수(reciprocal) 다항식을 구현하는 것인데, 주기 시험은 통과하고(역수 원시 다항식도 원시) Galois 와의 수열 일치 시험에서만 틀린다.

</details>

<details>
<summary>힌트 3 — `first_detect` 는 시그니처를 안 쓴다</summary>

직접 비교(결함 응답 ≠ good 응답)가 "진짜 검출" 의 정의이고, 시그니처 판정은 거기에 압축을 더한 것이다. 둘의 차이가 aliasing 이다. `patsrc_t` 는 값으로 복사해 넘기면 호출마다 같은 패턴 열이 된다.

</details>

<details>
<summary>힌트 4 — alias_exact 를 싸게</summary>

`m*L` 비트 전체를 `uint64_t E` 하나로 두고 사이클 t 의 워드를 `(E >> (t*m)) & mask` 로 뽑는다. `err_signature` 는 시드 0 시뮬레이션 — 선형성 때문에 이것만 재면 된다.

</details>

<details>
<summary>힌트 5 — lbist_run 의 순서</summary>

"이 결과를 믿어도 되는가" 를 앞에서부터 하나씩 닫는다: 설정이 말이 되나 → golden 이 있나 → 엔진이 놀고 있나 → 지난번 DONE 이 지워지나 → 내가 쓴 값이 들어갔나 → 끝났나(시간 제한) → 너무 빨리 끝나지 않았나 → 시그니처. 불일치면 재실행해서 재현성을 본다. 재실행은 `lbist_one` 헬퍼로 분리하면 깔끔하다. 타임아웃으로 나갈 때 `ABORT` 를 쓰지 않으면 엔진이 켜진 채로 남는다.

</details>

## 5. 풀이

### Part A — LFSR: 왜 원시여야 하나

Galois 스텝은 `S ← x·S mod P`. 상태 공간은 2^n, 0 은 고정점이므로 최대 주기는 `2^n-1` 이고, 이를 달성하는 필요충분조건이 **P 가 원시(primitive)** 라는 것 = `x` 의 위수가 `2^n-1` 이라는 것(`GF(2^n)` 의 곱셈군 생성원). 시험이 재는 것은 그 정의 그대로다:

| 다항식 | 성질 | 주기 (시드 1, 실측) |
|---|---|---|
| `0x13` = x⁴+x+1 | 원시 | 15 |
| `0x1F` = x⁴+x³+x²+x+1 | 기약, 비원시 (x⁵=1) | 5 |
| `0x15` = x⁴+x²+1 = (x²+x+1)² | 가약 | 6 |
| `0x11021` CRC-16-CCITT | (x+1) 인수 있음 | 32767 |
| `0x18` = x⁴+x³ | P(0)=0, x 가 비가역 | 0 (안 돌아옴) |

n = 2..16, 20 의 표 전부 Galois·Fibonacci 양쪽 주기 `2^n-1` 을 확인했다.

원시가 PRPG 에 필요한 이유는 "모든 0 아닌 상태를 한 번씩 방문" 이기 때문이다. 비원시면 시드에 따라 짧은 사이클에 갇혀 패턴이 반복되고, 그 뒤로 커버리지는 오르지 않는다. CRC-16-CCITT 는 일부러 (x+1) 인수를 넣어 **홀수 비트 오류를 전부 잡게** 만든 다항식이므로 PRPG 로 쓰면 주기가 절반이 된다 — "좋은 CRC 다항식" 과 "좋은 LFSR 다항식" 은 기준이 다르다.

Galois vs Fibonacci: 같은 P 면 출력 수열은 **같은 m-sequence 의 위상만 다른 것** 이다. 시험은 Galois 출력의 앞 n 비트로 Fibonacci 를 시드한 뒤 280비트를 비교한다(Galois 의 출력도 점화식을 만족함은 `L(x^t S0 · P) = L(0) = 0` 으로 나온다). 하드웨어 차이: Galois 는 플립플롭 사이 XOR 한 단(빠름), Fibonacci 는 탭 수만큼의 XOR 트리. 한편 m-sequence 의 balance(1 이 `2^(n-1)` 개)는 시험으로 확인했다.

**CUT 와 net stuck-at.** `cut_eval` 은 위상순으로 값을 채우고, 결함 넷은 값이 정해진 직후 덮어쓴다 — 그래서 결함 넷을 읽는 게이트는 전부 고장 값을 보고, 결함은 그 지점에서 뒤로 전파한다. 이 모델은 **넷(stem) stuck-at** 만이다. 팬아웃 가지(branch) 고장은 별도 결함인데 지금 모델에는 없다 — 넷리스트 기반 ATPG 도구의 "결함 수" 는 보통 더 많은 위치(게이트 입력 핀 포함)를 센다.

**의도적 중복 게이트.** ZERO 출력은 `z | (z & a0)` 로 만들어 두 번째 게이트의 s-a-0 은 절대 관측되지 않는다. 전수(2^11 패턴)로 정답을 먼저 구했다:

```text
faults=148 detectable=147 redundant=1: net72/sa0
```

**커버리지는 "결함 수 / 전체" 와 "결함 수 / 검출 가능" 을 구분해서 말한다.** 전자는 칩 스펙, 후자는 시험 품질이다. 리던던트 결함을 분모에 넣으면 영원히 100% 가 안 나오고, 그걸 "커버리지 부족" 으로 오해하면 없는 패턴을 찾아 헤맨다.

커버리지 곡선(LFSR16 시드 0xACE1, 실측):

```text
patterns  covered  coverage%  testable%
       1       60     40.5       40.8
       2       77     52.0       52.4
       4      107     72.3       72.8
       8      129     87.2       87.8
      16      130     87.8       88.4
      32      141     95.3       95.9
      64      146     98.6       99.3
     128      147     99.3      100.0
    4096      147     99.3      100.0
```

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">커버리지 곡선 (실측, mini-ALU 148 결함, x 축 log2)</text>
  <line x1="70" y1="230" x2="710" y2="230" stroke="currentColor"/>
  <line x1="70" y1="30" x2="70" y2="230" stroke="currentColor"/>
  <text x="66" y="234" text-anchor="end" fill="currentColor">0</text>
  <text x="66" y="134" text-anchor="end" fill="currentColor">74</text>
  <text x="66" y="34" text-anchor="end" fill="currentColor">148</text>
  <text x="70" y="248" text-anchor="middle" fill="currentColor">1</text>
  <text x="173" y="248" text-anchor="middle" fill="currentColor">4</text>
  <text x="276" y="248" text-anchor="middle" fill="currentColor">16</text>
  <text x="380" y="248" text-anchor="middle" fill="currentColor">64</text>
  <text x="483" y="248" text-anchor="middle" fill="currentColor">256</text>
  <text x="586" y="248" text-anchor="middle" fill="currentColor">1024</text>
  <text x="690" y="248" text-anchor="middle" fill="currentColor">4096</text>
  <line x1="70" y1="31.4" x2="710" y2="31.4" stroke="currentColor" stroke-dasharray="4 4"/>
  <text x="705" y="26" text-anchor="end" fill="currentColor">검출 가능 상한 147</text>
  <polyline fill="none" stroke="currentColor" stroke-width="2" points="70,148.9 121.7,125.9 173.3,85.4 225,55.7 276.7,54.3 328.3,39.5 380,32.7 431.7,31.4 483.3,31.4 535,31.4 586.7,31.4 638.3,31.4 690,31.4"/>
  <circle cx="70" cy="148.9" r="3" class="f-acc"/><circle cx="121.7" cy="125.9" r="3" class="f-acc"/><circle cx="173.3" cy="85.4" r="3" class="f-acc"/><circle cx="225" cy="55.7" r="3" class="f-acc"/><circle cx="276.7" cy="54.3" r="3" class="f-acc"/><circle cx="328.3" cy="39.5" r="3" class="f-acc"/><circle cx="380" cy="32.7" r="3" class="f-acc"/><circle cx="431.7" cy="31.4" r="3" class="f-acc"/>
  <text x="400" y="266" text-anchor="middle" fill="currentColor">패턴 수 (log2)</text>
  <text x="250" y="110" fill="currentColor">쉬운 결함은 패턴 몇 개로 끝나고</text>
  <text x="250" y="126" fill="currentColor">남은 건 조건이 좁은 결함 → 곡선의 긴 꼬리</text>
</svg>
```

곡선의 전형적 모양: 앞쪽에서 빠르게 오르고 뒤로 갈수록 평평하다. **평평해진 뒤에도 안 잡히는 결함이 있으면**, (a) 정말 redundant 인지 (b) random-pattern-resistant 인지 (Part B) 를 가려야 한다 — 패턴 수를 늘리는 건 (b) 의 일부에만 듣는다.

시그니처 캠페인 (패턴 4096, 16비트 MISR):

```text
campaign npat=4096 misr=16: sig_det=147 direct_det=147 aliased=0
```

### Part B — aliasing, 그리고 2^-n 이 뜻하지 않는 것

**선형성부터.** `sig' = x·sig + r` 은 GF(2) 위의 선형 사상이므로 `S(R ⊕ E) = S(R) ⊕ S(E)` (시드 0 기준). 오류 있는 회로의 시그니처가 golden 과 같다 ⟺ 오류 스트림만 넣은 시그니처 `S(E)` = 0. 즉 응답이 무엇이든 aliasing 은 **오류 스트림의 함수** 다. 시험은 `S(R1⊕R2) == S(R1)⊕S(R2)` 를 직접 확인한다.

**세는 법.** 시리얼(m=1) 에서 `S(E)` 는 오류 다항식 `E(x)` 의 나머지 `E(x) mod P(x)` 이고, 0 ⟺ `P | E`. 차수 L-1 이하 다항식 중 P 의 배수는 `2^(L-n)` 개(0 포함) 이므로 0 아닌 배수는 `2^(L-n)-1`개. m 입력 MISR 도 `mL` 비트 → n 비트 선형 사상의 kernel 크기라서 같은 형태 `2^(mL-n) - 1` 이다(전사 rank n 인 조건 하에).

정확 열거 결과(실측) — 비원시 다항식 `0x1F` 도 **균등 오류에서는 같은 숫자** 다:

```text
 n  m  L  alias   formula 2^(mL-n)-1  alias_rate   2^-n
 4  1 12    255                 255     0.06227  0.06250
 8  1 14     63                  63     0.00385  0.00391
 4  1  4      0                   0     0.00000  0.06250
 4  2  8   4095                4095     0.06249  0.06250
 5  3  5   1023                1023     0.03122  0.03125
 4  1 12    255                 255     0.06227  0.06250
```

`L ≤ n` 이면(세 번째 줄) `E(x)` 차수가 n 보다 작아 나머지가 자기 자신 — 절대 alias 안 된다. Monte-Carlo(L=32, 2^20 스트림, 4σ 허용, 시드 고정):

```text
 n  aliased  rate       2^-n      ratio  bound(4sigma)
 4    66009 0.062951 0.062500  1.007   +-991
 6    16378 0.015619 0.015625  1.000   +-508
 8     4085 0.003896 0.003906  0.997   +-255
10      953 0.000909 0.000977  0.931   +-128
12      240 0.000229 0.000244  0.938   +-64
```

4σ 는 "시드를 바꿔도 정상 표본오차는 통과" 하는 폭이다(이항 분포 정규근사). 값을 정답에 맞추려는 것이 아니라 코드의 대칭성/버그를 가리는 용도다.

**2^-n 이 뜻하지 않는 것.** `2^-n` 은 *오류 스트림이 균등 난수* 일 때의 값이고, 결함의 오류 스트림은 절대 균등 난수가 아니다.

- 단일 비트 오류는 절대 alias 안 된다: 시각 t 입력 j 의 단일 오류 → `x^(L-1-t) · x^j mod P`. `P(0)=1` 이면 `gcd(x, P) = 1` 이라 이 값은 0 이 아니다. 시험: 모든 (t, j) 전수, 원시·비원시 다항식 전부 0 alias.
- 2비트 오류는 구조적으로 alias 될 수 있다: SISR 에서 간격이 x 의 위수의 배수면 `x^a + x^b = x^b(x^(a-b)+1)` 이 P 로 나뉜다. 시험: `0x13`(위수 15)에 간격 15 → alias, 간격 5 → alias 아님; 비원시 `0x1F`(위수 5) 에 간격 5 → alias. **원시 다항식의 진짜 이점은 균등 aliasing 이 아니라 이 구조적 간격을 2^n-1 까지 밀어내는 것** 이다.
- MISR 대각선 상쇄: 입력 j 의 오류가 다음 사이클 입력 j+1 의 오류와 겹치면 0 이 된다 (아래 그림). 스캔 체인에서 인접 입력 사이 **상관된 오류** 가 흔하기 때문에 실제로 신경 쓰는 경우다.

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">MISR 2비트 오류의 대각선 상쇄 (시험: e[2]=1&lt;&lt;j, e[3]=1&lt;&lt;(j+1) → 시그니처 차이 0)</text>
  <text x="20" y="50" fill="currentColor">사이클</text>
  <text x="140" y="50" fill="currentColor">입력 j</text>
  <text x="280" y="50" fill="currentColor">입력 j+1</text>
  <text x="430" y="50" fill="currentColor">MISR 오류 (S_err)</text>
  <text x="20" y="85" fill="currentColor">t = 2</text>
  <rect x="130" y="65" width="70" height="30" class="f-warn"/><text x="165" y="85" text-anchor="middle" fill="currentColor">오류 1</text>
  <rect x="280" y="65" width="70" height="30" class="f-mut"/><text x="315" y="85" text-anchor="middle" fill="currentColor">0</text>
  <text x="430" y="85" fill="currentColor">x^j (bit j 에 1)</text>
  <text x="20" y="125" fill="currentColor">t = 3</text>
  <rect x="130" y="105" width="70" height="30" class="f-mut"/><text x="165" y="125" text-anchor="middle" fill="currentColor">0</text>
  <rect x="280" y="105" width="70" height="30" class="f-warn"/><text x="315" y="125" text-anchor="middle" fill="currentColor">오류 2</text>
  <text x="430" y="125" fill="currentColor">x·x^j ⊕ x^(j+1) = 0</text>
  <line x1="200" y1="90" x2="278" y2="112" stroke="currentColor"/>
  <text x="20" y="175" fill="currentColor">t=2 의 오류가 시프트되어 t=3 의 bit j+1 에 도착 → 그 시각 입력 j+1 의 오류와 정확히 상쇄된다.</text>
  <text x="20" y="193" fill="currentColor">균등 난수 가정 2^-16 과 무관하게, 이 패턴은 항상 alias 된다 — 결함은 "난수" 가 아니라 "구조" 다.</text>
</svg>
```

**실제 결함에서 측정 (mini-ALU, 32 시드 × 256 패턴, 실측):**

```text
 n  errored_faults  aliased  rate      2^-n
 6            4704       64 0.01361  0.01562
 8            4704       10 0.00213  0.00391
10            4704        2 0.00043  0.00098
12            4704        2 0.00043  0.00024
14            4704        0 0.00000  0.00006
16            4704        0 0.00000  0.00002
```

표의 자릿수는 2^-n 근처에 있지만 결함과 시드가 독립이 아니라(같은 결함이 시드만 바꾼 것) n≥10 에서는 표본이 작아 해석에 주의해야 한다. n=16 에서 관측 0 은 기대값 약 0.07 개이므로 "없다" 가 아니라 "이 표본에서는 안 보였다" 다. 시험은 6비트에서 `2^-6` 의 [1/3, 3] 배 범위 — 자릿수만 본다(이론이 보장하지 않으므로 좁은 tolerance 를 줄 근거가 없다).

**실무 결론:** 단일 패턴이 아니라 긴 시퀀스 뒤의 시그니처를 쓰고, 시그니처 폭은 양산에서 보통 16~64비트(대표값)이며 alias 확률은 `2^-n`, 그리고 **구조적 패턴은 별도로** — 보통 시그니처 폭은 `-log2(허용 alias 확률)` 로 정한다. 숫자 자체는 제품 기준이고 여기선 대표값이다.

**Random-pattern-resistant 결함.** AND(k) 출력 s-a-0 을 잡으려면 PI 가 전부 1 — 패턴당 확률 2^-k. 입력 하나의 s-a-1 은 그 입력만 0 이고 나머지 1, 역시 2^-k. 측정(2048 패턴, 실측):

```text
and16 uniform  : 10/62
and16 weight7/8: 62/62
or16  weight7/8: 4/62
or16  weight1/8: 62/62
and16+tp uniform: 70/70
```

Weighted(가중 패턴): 각 PI 를 LFSR 비트 3개의 OR 로 → `P(1)=7/8`, 전부-1 패턴 확률이 `0.875^16 ≈ 11.8%`, 입력 s-a-1 은 `0.125 × 0.875^15 ≈ 1.7%` 로 크게 오른다. **같은 가중은 OR 에 독** 이다(4/62) — 회로마다 가중 세트를 나눠 돌린다(Weight set 을 나눠 BIST 분할 실행).

Test point: 회로 안 어려운 노드에 **control point**(강제 1, OR 게이트+입력)와 **observation point**(내부 노드를 응답에 연결)를 넣는다. AND16 을 AND8·AND8 로 쪼개고 2+2 를 넣자 uniform 2048 패턴으로 70/70. 대가: 면적, 라우팅, 타이밍(크리티컬 패스에 OR 끼기), 전력 — 그래서 실제 LBIST 는 test-point 삽입을 커버리지가 정체되는 결함 위주로 수행한다. 패턴 수 vs k (demo, 시드 3개 중 최악, LFSR24, 실측):

```text
LFSR24 period = 16777215 (expect 16777215)
patterns to reach 100% net stuck-at coverage (worst of 3 seeds; cap 8388608)
k   faults  uniform      weighted(7/8)  uniform+TP(2CP+2OP)   2^k
8   30      587          111            64                    256
12  46      20974        108            328                   4096
16  62      289410       210            595                   65536
20  78      3071458      548            5911                  1048576
```

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_resistant.c -o demo_resistant && ./demo_resistant
```

uniform 은 입력 폭 k 에 대해 대략 지수적이다(`2^k` 이상, 시드에 따라 결함마다 기다리는 시간이 달라 최악값은 더 크다). test point 는 지수를 반으로, weighted 는 여기서 거의 평평하게 만든다 — 단 "이 회로의 weighted" 일 뿐이다.

**이 모델의 한계 (일부러 적음):**

| 빠진 것 | 현실에서는 |
|---|---|
| 넷(stem) stuck-at 만 | 게이트 입력 핀 결함, transition, bridge, small delay 는 별도 fault model + at-speed |
| 조합 회로 test-per-clock | 스캔 기반 STUMPS, shift/capture, launch-on-capture 타이밍 |
| X 없음 (Part A/B) | X-source (메모리, 아날로그, tri-state, false path) 는 바운딩·마스킹 필수 (Part C) |
| 단일 고착 | 실제 결함은 다중·비정적; stuck-at 커버리지는 "대리 지표" 다 |

### Part C — 펌웨어 시퀀스: 시그니처를 믿기 전에 확인할 것들

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a10f" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">lbist_run 흐름과 실패 지점별 원인</text>
  <rect x="10" y="30" width="150" height="34" class="f-mut"/><text x="85" y="52" text-anchor="middle" fill="currentColor">CHECK: cfg, golden</text>
  <rect x="10" y="85" width="150" height="34" class="f-mut"/><text x="85" y="107" text-anchor="middle" fill="currentColor">PRECHECK: BUSY? DONE?</text>
  <rect x="10" y="140" width="150" height="34" class="f-mut"/><text x="85" y="162" text-anchor="middle" fill="currentColor">CONFIG: write+readback</text>
  <rect x="10" y="195" width="150" height="34" class="f-acc"/><text x="85" y="217" text-anchor="middle" fill="currentColor">START → poll(timeout)</text>
  <rect x="10" y="250" width="150" height="34" class="f-acc"/><text x="85" y="272" text-anchor="middle" fill="currentColor">시간 하한 → SIG 읽기</text>
  <rect x="10" y="296" width="150" height="30" class="f-ok"/><text x="85" y="316" text-anchor="middle" fill="currentColor">COMPARE (재실행)</text>
  <line x1="85" y1="64" x2="85" y2="84" stroke="currentColor" marker-end="url(#a10f)"/>
  <line x1="85" y1="119" x2="85" y2="139" stroke="currentColor" marker-end="url(#a10f)"/>
  <line x1="85" y1="174" x2="85" y2="194" stroke="currentColor" marker-end="url(#a10f)"/>
  <line x1="85" y1="229" x2="85" y2="249" stroke="currentColor" marker-end="url(#a10f)"/>
  <line x1="85" y1="284" x2="85" y2="295" stroke="currentColor" marker-end="url(#a10f)"/>
  <text x="180" y="52" fill="currentColor">BAD_CFG (seed 0, patcnt 0, X 미마스크) / NO_GOLDEN</text>
  <text x="180" y="107" fill="currentColor">BUSY_AT_ENTRY / DONE_STUCK (W1C 후에도 1)</text>
  <text x="180" y="162" fill="currentColor">CFG_READBACK (reg_off 기록) — START 금지</text>
  <text x="180" y="217" fill="currentColor">TIMEOUT (busy=0/1 기록, ABORT 로 정리)</text>
  <text x="180" y="272" fill="currentColor">EARLY_DONE: 패턴×사이클 미달 → 시그니처 신뢰 불가</text>
  <text x="180" y="316" fill="currentColor">일치 PASS / 불일치 → 재실행: 같으면 SIG_MISMATCH, 다르면 UNSTABLE(X)</text>
</svg>
```

**왜 이 순서인가.** 각 단계는 그 다음 단계의 *전제* 를 확인한다. DONE 이 스턱인데 시그니처 비교로 가면 "미완료 시그니처 불일치" 로 잘못 분류돼 논리 결함을 찾아 헤맨다.

- **Stale DONE**: 이전 실행이 남긴 DONE 을 W1C 로 지우고 **다시 읽어 확인** 한다. 스턱-1 이면 여기서 잡힌다. 그렇지 않으면 첫 poll 에서 바로 "완료" 로 나가 쓰레기 시그니처를 비교한다.
- **Config readback**: SEED 가 안 써지면 다른 시드의 시그니처를 비교하게 된다 — 결함처럼 보이지만 설정 문제. readback 이 먼저 잡아낸다(시작 전).
- **Timeout**: `2 × 예상 + 여유`. `BUSY` 가 남아 있으면 엔진 hang, BUSY 가 내려갔는데 DONE 이 안 서면 완료 플래그 경로 고장. 나갈 때 **ABORT** — 엔진을 켜 둔 채로 다음 시험으로 넘어가면 다음 시험을 망친다. (SIV-B09 poll-with-timeout 을 축약)
- **시간 하한(EARLY_DONE)**: 패턴 수 × 사이클보다 빨리 끝났다면 시그니처를 믿을 근거가 없다. 흔한 원인은 `PATCNT` 해석 오류, 일찍 끝나는 FSM.
- **재실행 분류**: 불일치면 같은 설정으로 한 번 더 돌린다. 시그니처가 같다 → 결정적 원인(논리 결함, SIG 읽기 경로, 설정). 다르다 → 뭔가 실행마다 변한다(X 누수, 마진, 클럭 도메인 교차). 이 분류 하나가 디버그 경로를 갈라 준다.

실행 로그 (실측):

```text
PASS ip=LBIST0 seed=0xACE1 patcnt=4096 xmask=0x40 sig=0x1C96 elapsed=4136
FAIL ip=LBIST0 cause=DONE_STUCK step=PRECHECK seed=0xACE1 patcnt=4096 xmask=0x40
FAIL ip=LBIST0 cause=TIMEOUT step=POLL seed=0xACE1 patcnt=4096 xmask=0x40 busy=0 elapsed=10248
FAIL ip=LBIST0 cause=EARLY_DONE step=POLL seed=0xACE1 patcnt=4096 xmask=0x40 elapsed=520 min=4096
FAIL ip=LBIST0 cause=CFG_READBACK step=CONFIG seed=0xACE1 patcnt=4096 xmask=0x40 reg_off=0x08
FAIL ip=LBIST0 cause=SIG_MISMATCH step=COMPARE seed=0xACE1 patcnt=4096 xmask=0x40 exp=0x1C96 act=0x2F07 act2=0x2F07 diff=0x3391 runs=2
FAIL ip=LBIST0 cause=UNSTABLE step=COMPARE seed=0xACE1 patcnt=4096 xmask=0x40 exp=0x1C96 act=0x443E act2=0xCE6B diff=0x58A8 runs=2
```

시험표(전부 CHECK 됨):

| mock 결함 | 기대 결과 | 추가로 확인 |
|---|---|---|
| 없음 | `LB_OK`, 시그니처 = 모델 | `elapsed ≥ 4096`, `DONE` 지워짐, `starts == 1` |
| `DONE_STUCK1` | `DONE_STUCK`, PRECHECK | 엔진 시작 안 함 (`starts == 0`) |
| `BUSY_STUCK1` | `BUSY_AT_ENTRY` | |
| `DONE_STUCK0` | `TIMEOUT`, busy=0 | `elapsed > 2N`, ABORT 호출됨 |
| `BUSY_HANG` | `TIMEOUT`, busy=1 | ABORT 로 busy 풀림 |
| `EARLY_DONE` | `EARLY_DONE` | |
| `SEED_RO` | `CFG_READBACK`, reg=SEED | 시작 안 함 |
| `LOGIC`(Y1 s-a-0) | `SIG_MISMATCH` | `act == act2 ≠ exp`, `runs==2` |
| `SIG_BIT` (읽기 경로 비트 스턱) | `SIG_MISMATCH` | `exp ^ act` = 정확히 그 비트 |
| `XMASK_BROKEN` | `UNSTABLE` | `act ≠ act2` |
| 설정 오류 | `BAD_CFG` / `NO_GOLDEN` | seed 0, patcnt 0, X 미마스크, 없는 patcnt |

X-source 처리: 응답 bit6 은 매 패턴 값이 달라지는 X. `XMASK_EN` 으로 지워야만 시그니처가 정의된다. X 가 한 번이라도 MISR 에 들어가면 그 뒤 시그니처 전체가 망가지고(선형 → 오류가 영원히 남는다) 실행마다 달라진다.

**마스킹의 대가 (limits, 실측):**

```text
xmask limit: COUT/sa1 with bit4 masked -> PASS (missed); unmasked -> FAIL
mask PO bit -> faults lost: b0=0 b1=2 b2=0 b3=0 b4=4 b5=9
locate Y0/sa1: window=(0,64] true_first_detect=0
locate net22/sa1: window=(64,256] true_first_detect=68
```

COUT s-a-1 은 bit4 를 마스크한 설정에서는 **PASS** 다 — 그 비트로만 보이는 결함은 놓친다. 출력 비트 하나를 가렸을 때 1024 패턴 안에서 잃는 결함 수를 재 보면 비트마다 다르다: Y0·Y2·Y3 은 0 개, Y1 은 2 개, COUT(bit4) 4 개, ZERO(bit5) 9 개. Y 비트를 가려도 많은 결함이 ZERO 출력으로 다시 보이지만(ZERO 는 모든 Y 의 NOR 다), Y1 의 2 개처럼 패턴 조건이 맞지 않으면 그 안에서는 놓친다 — 이 숫자는 이 회로와 패턴 수에서만 의미가 있지만, "어느 출력을 가리면 커버리지가 얼마 깎이나" 는 실제 X-마스크 계획을 짤 때 정확히 묻는 질문이다.

**구간 찾기(`lbist_locate`).** 체크포인트마다(64 / 256 / 1024 / 4096 패턴) golden 표가 있어 앞에서부터 돌려 첫 불일치 구간을 찾는다. 진짜 첫 검출 패턴이 그 구간 안임을 시험이 확인한다(주입한 진실과 대조). 이 구간은 ATE/시뮬의 fail 패턴 진단(이후 ATPG diagnosis)으로 넘길 입력이고, 수 M 패턴을 전부 저장하지 않고 얻는 근사다. 구간 검색이 정말 맞으려면 그 prefix 시그니처끼리 aliasing 이 없어야 하는데 — 16비트에서는 무시할 만하지만 원리적으로 이 가정이 있다.

**실무 메모 (대표적으로):** 안전 등급 MCU/SoC 에는 부팅 시 혹은 주기적으로 도는 LBIST 컨트롤러가 들어 있고(TI 의 Hercules/Jacinto 계열 TRM 에서 STC/LBIST 블록을 볼 수 있다), 펌웨어가 하는 일은 대체로 이 레슨의 시퀀스와 닮았다. Pre-silicon 에서 golden 시그니처는 게이트 시뮬레이션(혹은 ATPG 도구의 LBIST flow)이 생성하고, 전압·주파수·온도 조건에 따라 패스/페일이 달라지면 그 자체가 shmoo 데이터다(SIV-M07). 실제 LBIST 의 관측 손실(스캔 체인 X, power-aware shift)은 이 레슨 범위 밖이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san
```

`<math.h>` 는 맥에서 libSystem 에 들어 있어 `-lm` 없이 링크되고, 리눅스 gcc 에서는 `-lm` 을 붙인다. 실행 결과: 위 표들 + `120/120 passed`, exit 0 (약 0.5 s). ASan/UBSan 빌드도 `120/120 passed`, 경고 없음.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 것 |
|---|---|---|
| LFSR 시드를 0 으로 둠 | 0 은 고정점 — 패턴이 전부 0 이고 커버리지가 얼어붙는다 | `seed != 0` 강제, 설정 시 `BAD_CFG` |
| 탭 비트를 뒤집어 구현 (Fibonacci) | 역수 다항식이 된다. 주기는 맞지만 Galois 와 수열이 달라 golden 이 틀어진다 | `tap bit = n-1-i` |
| `P(0)=0` 인 다항식 | x 가 비가역 → 주기가 돌아오지 않고 시그니처가 초기값을 잊는다 | 다항식 표 + 주기 시험 |
| 커버리지 분모에 redundant 결함 포함 | 영원히 100% 불가 → 없는 패턴을 찾아 헤맨다 | 전수/형식 검증으로 redundant 분리, "검출가능 대비" 로 보고 |
| "시그니처 틀림 = 칩 결함" 으로 단정 | 설정 readback, stale DONE, X 누수일 수 있다 | Part C 의 사전 점검 + 재실행 분류 |
| DONE 을 지우지 않고 poll | 이전 DONE 을 보고 즉시 완료 판정 → 미완료 시그니처 | PRECHECK 에서 W1C 후 확인, 끝나고도 clear |
| Timeout 뒤 그냥 return | 엔진이 살아 다음 시험을 망가뜨린다 | `ABORT` 쓰고 나간다 |
| X-source 를 안 막음 | 시그니처가 실행마다 달라진다 → 실제 고장이 안 보임 | `XMASK_EN`, X 미마스크 설정 거절 |
| 2^-n 을 "alias 는 그만큼만 생긴다" 로 인용 | 구조적 패턴(대각선, 주기 간격)은 그보다 훨씬 자주 alias | 폭 + 긴 시퀀스 + 다른 다항식 고려 |
| `coverage_curve` 를 패턴마다 다시 시뮬 | O(결함 × cp × 패턴) | 첫 검출 인덱스 1회 후 cp 로 분류 |

## 8. 면접 Q&A

<details>
<summary>Q1. BIST 를 왜 쓰나? ATE 로 스캔 패턴을 돌리면 되지 않나?</summary>

ATE(Automatic Test Equipment)는 양산 때 한 번 쓰는 값비싼 장비이고 핀 수·메모리에 패턴 수가 제한된다. BIST 는 패턴 저장이 없고(LFSR 이 생성), 판정이 칩 안에서 끝나며(시그니처 비교), **필드에서도** 돌릴 수 있다. 자동차 SoC 가 BIST 를 넣는 큰 이유가 이것 — 양산 뒤 에이징 고장을 키 온/주기 시험으로 잡기 위함(ISO 26262 잠재 결함). 대가는 면적·타이밍·전력·낮은 커버리지(랜덤 패턴 저항), 그리고 X 관리다.

**꼬리질문: 그럼 ATE 스캔은 필요 없나?** 양산 스캔은 결정적 ATPG 패턴으로 커버리지가 더 높다. 보통 둘 다 쓴다: ATE 스캔 + MBIST/LBIST(번인, SLT, 필드). 면접에서는 "역할이 다르다" 를 분명히 말한다.

</details>

<details>
<summary>Q2. 왜 원시 다항식인가? 안 쓰면 어떻게 되나?</summary>

원시이면 상태가 `2^n-1` 개 전부를 돈다: 패턴이 최대로 안 겹치고, 결함당 "기다리는 시간" 분포가 가장 좋다. 비원시면 시드에 따라 짧은 사이클에 갇힌다 — 실측으로 `x^4+x^3+x^2+x+1` 은 주기 5. CRC-16-CCITT 는 (x+1) 인수로 주기가 32767 이다. MISR 쪽에서 원시 다항식의 이점은 균등 aliasing 이 아니다 — 그건 어떤 차수 n 다항식이든 `2^-n` 이다(전수 결과). 이점은 **2비트 오류 aliasing 이 생기는 간격** 이 `2^n-1` 로 최대라는 것.

**꼬리질문: 그럼 왜 MISR 은 다항식이 이것저것인가?** 면적(탭 수 적은 trinomial/pentanomial), 2비트/구조 오류 alias 특성, 입력 폭 맞춤. 양산은 대개 32비트 이상(대표값)이라 숫자 aliasing 이 사실상 안 보인다.

</details>

<details>
<summary>Q3. Aliasing 확률 2^-n 은 어떻게 나오고, 언제 틀리나?</summary>

유도: 시리얼(SISR)에서 오류 다항식 E(x) 가 P 로 나뉘면 alias. 차수 < L 인 다항식의 배수 비율 = `(2^(L-n)-1)/(2^L-1)` ≈ `2^-n` (실측 전수 일치). 그건 **오류 스트림이 균등 난수** 일 때다. 틀리는 경우: (1) 단일 비트 오류는 0% (P(0)=1 → 정리), (2) 특정 2비트 패턴은 100% (MISR 대각선, 주기 간격), (3) 스캔 입력 사이 상관, 공통원인 오류는 구조적이라 aliasing 이 평균보다 높거나 낮다.

**꼬리질문: 그래서 시그니처를 몇 비트로 할 건가?** `-log2(허용 alias 확률)` + 구조 오류 대비 여유. 양산 시험은 대개 수십 비트(대표값). 하드웨어가 작아도 되는 BIST 에서는 16비트도 쓰지만, 시퀀스 뒤쪽 패턴에서 여러 번 비교(중간 시그니처)로 보완할 수 있다.

**꼬리질문 2: 왜 단일 비트 오류는 alias 가 안 되나?** `E(x) = x^i`, `P(0)=1` 이면 `x^i` 는 P 의 배수가 될 수 없다(`gcd(x,P)=1`). MISR 다중 입력이어도 단일 비트 오류는 `x^(t+j)` 형태이니 같다. 조건은 P(0)=1 — 그래서 상수항 없는 다항식을 쓰면 안 된다.

</details>

<details>
<summary>Q4. 넓은 AND 는 왜 random pattern 에 강한가? 해결책의 대가는?</summary>

AND(k) 출력 s-a-0 을 활성화하려면 모든 입력이 1 — 패턴당 2^-k. 측정: AND16, 2048 패턴이면 10/62 만 검출. 대응: weighted pattern (7/8) → 62/62 지만 같은 가중이 OR 에는 독(4/62) → 가중 세트 여러 개로 나눠서 실행해야 한다(시간 증가). Test point(AND 를 쪼개 control/observe 삽입) → uniform 으로 70/70 지만 면적·타이밍 비용, 크리티컬 패스면 못 넣는다. Reseeding (ATPG 큐브를 LFSR 시드로 인코딩)이 또 다른 선택.

**꼬리질문: 이 결함이 설계 어디에 있나?** 주소 디코더, 레지스터 write-enable, FSM 특정 상태 전환, 비교기 — "많은 비트가 특정 값이어야 동작" 하는 곳. 시뮬 없이도 SCOAP 같은 가테스트성 지표로 위험한 노드를 짚고 test-point 위치를 정한다.

</details>

<details>
<summary>Q5. 시그니처가 틀렸다. 어떤 순서로 디버그하나?</summary>

1) 환경: 같은 전압·주파수·온도인가, golden 이 이 칩 리비전/이 패턴 수의 것인가. 2) 재실행: 같은 값이면 결정적, 다르면 X/마진(`LB_UNSTABLE`). 3) 다른 시드·다른 패턴 수로 실행해 불일치가 사라지거나 움직이는지 — 틀린 시그니처가 다른 시드의 golden 과 맞으면 시드 레지스터 문제다(연습 포인트). 4) 첫 실패 구간 찾기(체크포인트) → 그 패턴 번호로 시뮬 재현 + fault diagnosis. 5) shmoo: Vmin/주파수 쪽으로 움직이면 타이밍 결함, 고정이면 stuck/구조 결함(SIV-M07, SIV-M11).

**꼬리질문: DONE 이 안 오고 BUSY 만 있으면?** BUSY 가 안 내려오면 엔진 FSM 자체 hang — 클럭 게이팅, 리셋, 파워 도메인 먼저. BUSY 는 내려갔는데 DONE 이 없다면 완료 플래그 경로 고장. 그래서 timeout 리포트에 `busy` 값을 남긴다.

</details>

<details>
<summary>Q6. X-source 는 왜 치명적이고, 어떻게 처리하나? 그 대가는?</summary>

MISR 는 선형이라 X 가 한 번 들어가면 그 뒤 시그니처 전체가 정의되지 않는다. 처리: **설계 단계에서 X-bounding** (메모리/아날로그 앞 mux, tri-state 막기), **X-마스킹** (응답을 게이팅), **test mode** 에서 false/multicycle path 무력화. 비용: 마스크한 관측점의 결함은 못 본다 — 측정에서 COUT bit 마스크 시 해당 결함 4개를 놓쳤다. "어느 칩을 얼마나 마스크했나" 는 커버리지 보고의 일부다.

**꼬리질문: 펌웨어는 X 를 어떻게 아나?** 직접 알 수 없다. golden 은 pre-silicon 시뮬이 X 를 어떻게 처리했는지에 의존한다. 그래서 bring-up 때 똑같은 설정을 **두 번 돌려 시그니처가 같은지** 먼저 본다(이 레슨의 `UNSTABLE`). 달라지면 X/마진 문제이고, 어느 비트에서 달라지는지 시드·체크포인트로 좁힌다.

</details>

<details>
<summary>Q7. Part C 에서 시간 하한(EARLY_DONE)을 왜 넣었나? 없으면 무슨 일이?</summary>

엔진이 패턴을 다 안 돌고 DONE 을 띄우면(잘못된 `PATCNT` 해석, FSM 일찍 종료) 시그니처는 짧은 시퀀스 것이라 golden 과 다르다. 하한 검사 없으면 `SIG_MISMATCH` 로 분류돼 논리 결함을 찾는다. 하한 검사로 **분류 비용을 앞으로 당긴다**. 다만 사이클 카운터 기준이라 실제 칩에서는 클럭 비율·타이머 분해능을 고려해 여유를 둬야 한다.

**꼬리질문: 비슷하게 재 볼 수 있는 값이 또 있나?** 패턴 카운터를 읽어 `PATCNT` 와 비교, 또는 BIST 클럭이 정말 돌았는지 타이머로 확인 — "엔진이 일을 했는가" 를 시그니처 외 채널로 교차 확인한다.

</details>

## 9. 추가 연습

### 연습 1 — 시그니처는 다항식 나머지다

GF(2)[x]/P 곱셈 `gf_mul`, 거듭제곱 `x_pow` 를 구현해, 시뮬레이션 없이 닫힌 식으로 시그니처 `Σ e[t](x) · x^(L-1-t) mod P` 를 계산하라. 무작위 스트림에서 시뮬과 일치하고, 원시 다항식에서 `x^(2^n-1) = 1` 임을 확인하라.

<details><summary>답</summary>

시각 t 의 입력은 이후 `L-1-t` 번 x 가 곱해지며 시그니처에 남는다(선형). `gf_mul` 은 시프트+XOR, `x_pow` 는 제곱-곱셈. `x_pow(65535, P16, 16) == 1` 이 원시의 다른 표현이다. 코드는 `practice.c`.

</details>

### 연습 2 — 시드를 바꿨을 때 golden 을 다시 시뮬레이션 없이

펌웨어가 MISR 시드를 바꿨다면 golden 표 전체를 재시뮬할까? `sig(seed) = sig(0) ⊕ seed · x^L mod P` 를 이용해 `golden_with_seed` 를 구현하라.

<details><summary>답</summary>

선형성 `S(seed, R) = x^L·seed ⊕ S(0, R)`. 패턴 수 L 만 알면 O(log L) 이다. 시험은 무작위 시드·길이로 시뮬과 비교. (단 시드가 *PRPG* 시드면 응답 자체가 바뀌므로 재시뮬이 필요하다 — MISR 시드에 한한 이야기.)

</details>

### 연습 3 — all-0 패턴이 필요한 결함: complete LFSR

n비트 LFSR 은 0 상태를 건너뛴다. NOR(n) 출력 s-a-0 은 all-0 패턴에서만 보이는데, 8비트 LFSR 전 주기(255 패턴)에서는 영영 못 잡는다. de Bruijn 수정으로 주기 `2^n`(모든 상태 방문)을 만들고 결함이 잡히는지 확인하라.

<details><summary>답</summary>

Fibonacci 의 궤환을 `f ⊕ (낮은 n-1 비트가 전부 0)` 로 바꾼다. 0...01 ← 1000...0 전이에 0000...0 을 끼워 넣는 효과다. n=3..12 에서 `2^n` 상태가 정확히 한 번씩 나오고, NOR 결함이 256 패턴 안에 잡힌다. 하드웨어 비용은 NOR 트리 하나다. 실제로는 Part A 처럼 LFSR 을 PI 보다 넓게(여기서 16비트 LFSR → 11 PI) 잡으면 PI 쪽 all-0 은 그냥 나오므로, 이 수정이 필요한 곳은 좁다.

</details>

### 연습 4 — 두 MISR: 정말 aliasing 이 곱해지나?

4비트 MISR 2개(`P1`, `P2`)를 병렬로 돌려 둘 다 0 이 되는 비율을 전수(L=12)로 재라. 같은 다항식 2개와 서로소 2개를 비교하고, 서로소라면 곱 다항식 8비트 MISR 하나와 같은 alias 집합임을 확인하라.

<details><summary>답</summary>

같은 다항식이면 두 레지스터가 똑같아 255/4095(이득 0). 서로소(`0x13`, `0x19`)면 15 — `2^(12-8)-1` 로 곱 다항식 8비트와 동일하다(중국인의 나머지 정리: 두 다항식 모두로 나누어떨어지는 것 ⟺ 곱으로 나누어떨어지는 것). 실제 결과: `dual: same_poly=255 coprime=15 product_8bit=15 expect 255/15/15`. 면적이 같으면 8비트 하나가 대개 낫지만, 입력 폭 분할이나 구간별 시그니처처럼 레지스터가 이미 둘일 때 다항식을 다르게 고르는 것은 공짜 이득이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
```

실제 결과: `dual: same_poly=255 coprime=15 product_8bit=15 expect 255/15/15` + `117/117 passed`, exit 0.

## 10. 핵심 정리

- PRPG = 원시 다항식 LFSR: 주기 `2^n-1`, 시드 ≠ 0. 비원시면 짧은 사이클(CRC-CCITT 는 32767). Galois 와 Fibonacci 는 같은 수열의 위상 차이.
- MISR: `sig' = x·sig + r mod P`. 선형 → 시그니처 불일치 ⟺ 오류 스트림 `S(E) ≠ 0`.
- 균등 오류 aliasing = `(2^(mL-n)-1)/(2^(mL)-1) ≈ 2^-n` (전수 일치). 원시성과 무관.
- 단일 비트 오류는 alias 불가 (`P(0)=1`). 2비트 오류는 대각선·주기 간격에서 항상 alias. **2^-n 은 "모든 경우의 평균" 이지 보장이 아니다.**
- 커버리지는 redundant 를 빼고 말한다. 곡선이 평평하면 패턴을 늘리기 전에 redundant 인지 resistant 인지 가린다.
- Random-pattern-resistant 결함(넓은 AND)은 패턴 수가 지수적: weighted(회로별 세트), test point(면적·타이밍 비용), reseeding.
- 펌웨어 시퀀스 순서: 설정 → golden → BUSY/DONE 사전 점검 → readback → START → poll+timeout(ABORT) → 시간 하한 → SIG → 재실행 분류(재현=하드, 비재현=X/마진).
- X 는 MISR 전체를 오염시킨다: 바운딩·마스킹, 대신 마스크한 출력의 결함은 놓친다.

## 참고자료

- M. L. Bushnell, V. D. Agrawal, *Essentials of Electronic Testing for Digital, Memory and Mixed-Signal VLSI Circuits* (BIST, LFSR, 시그니처 분석 장)
- L.-T. Wang, C.-W. Wu, X. Wen (eds.), *VLSI Test Principles and Architectures* (Logic BIST 장: STUMPS, weighted, test point)
- M. Abramovici, M. Breuer, A. Friedman, *Digital Systems Testing and Testable Design*
- Bardell, McAnney, Savir, *Built-In Test for VLSI: Pseudorandom Techniques* (STUMPS, aliasing 분석)
- 관련 레슨: SIV-B07(March, RAM), SIV-B09(poll+timeout), SIV-M02(CRC32), SIV-M06(PRBS), SIV-M07(shmoo), SIV-M11(bisect), SIV-M14(ECC)
