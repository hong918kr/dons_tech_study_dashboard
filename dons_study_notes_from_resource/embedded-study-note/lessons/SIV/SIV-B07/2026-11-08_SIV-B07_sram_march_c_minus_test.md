---
id: SIV-B07
time: 75분
updated: 2026-11-08
---
# SIV-B07 SRAM March 테스트 (March C-)

## 학습 목표

- March 표기법 `{⇕(w0); ⇑(r0,w1); ...}` 을 읽고, **element 테이블 + 엔진 하나**로 MATS+ / MATS++ / March C- 를 모두 돌릴 수 있다.
- SRAM 결함 모델 — stuck-at(SAF), transition(TF), address decoder(AF), coupling(CFin / CFid / CFst), 그리고 커버리지 밖인 DRDF — 을 **mock SRAM 에 주입**하고, 알고리즘별 **검출률을 직접 측정**한다.
- "왜 ⇑ 와 ⇓ 가 둘 다 필요한가", "왜 MATS+ 는 1→0 transition fault 를 못 잡나" 를 측정 결과로 설명한다.
- word 단위 온칩 SRAM 에 **data background** 를 붙여 March C- 를 돌리고, 디버그 엔지니어가 바로 행동할 수 있는 실패 리포트(`addr / exp / act / diff / bits`)를 만든다.
- 비용(10n vs 5n vs O(n²))과 커버리지의 트레이드오프, MBIST·ECC·코드가 올라간 SRAM 테스트 같은 실전 꼬리질문에 답한다.

## 1. 문제

첫 실리콘이 나오면 SoC 면적의 큰 부분을 차지하는 온칩 SRAM(캐시, ISP 라인버퍼, NPU 가중치 버퍼, NVMe 컨트롤러 데이터 버퍼)이 **제대로 저장하는지** 가 가장 먼저 확인할 것 중 하나다 — 그래서 NVIDIA·Apple·Rivian 류 실리콘 검증 면접에서 "SRAM 테스트를 짜 보세요 → walking 1s 로 충분한가요? → March C- 가 뭘 잡나요?" 는 단골 연쇄 질문이다.

walking 1s/0s(SIV-B05)는 **레지스터 하나 / 데이터 버스** 의 stuck-at·short 를 잡는다. 메모리 **배열** 은 다르다. 셀이 수만~수백만 개라 O(n²) 패턴(GALPAT 등)은 너무 느리고, 셀끼리의 간섭(coupling)과 주소 디코더 고장까지 잡아야 한다. **March 테스트** 는 이것을 **O(n)** 으로 해결한다: 모든 주소를 정해진 방향으로 *행진(march)* 하면서 각 주소에서 같은 짧은 연산열을 실행한다.

표기법 (van de Goor):

| 기호 | 뜻 |
|---|---|
| `⇑` / `⇓` | 주소 오름차순 / 내림차순으로 돌며 실행 (`⇓` 은 반드시 `⇑` 의 **정확한 역순**) |
| `⇕` | 방향 무관 (구현은 보통 ⇑) |
| `r0` / `r1` | 읽고 0 / 1 이 나와야 한다 (아니면 FAIL) |
| `w0` / `w1` | 0 / 1 을 쓴다 |
| `(r0,w1)` | **한 주소에서** r0 다음 w1 을 하고 나서 다음 주소로 → 이것이 March element |

**March C-** = `{⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕(r0)}` — element 6개(M0~M5), 셀당 연산 10개 → **10n**.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">March C- : 6 element, 셀당 10 연산 (10n)</text>
  <g stroke="currentColor">
    <rect x="10"  y="40" width="110" height="70" rx="6" class="f-mut"/>
    <rect x="132" y="40" width="110" height="70" rx="6" class="f-acc"/>
    <rect x="254" y="40" width="110" height="70" rx="6" class="f-acc"/>
    <rect x="376" y="40" width="110" height="70" rx="6" class="f-warn"/>
    <rect x="498" y="40" width="110" height="70" rx="6" class="f-warn"/>
    <rect x="620" y="40" width="110" height="70" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="65"  y="62" font-weight="bold">M0 ⇕</text><text x="65"  y="88" font-family="monospace">(w0)</text>
    <text x="187" y="62" font-weight="bold">M1 ⇑</text><text x="187" y="88" font-family="monospace">(r0,w1)</text>
    <text x="309" y="62" font-weight="bold">M2 ⇑</text><text x="309" y="88" font-family="monospace">(r1,w0)</text>
    <text x="431" y="62" font-weight="bold">M3 ⇓</text><text x="431" y="88" font-family="monospace">(r0,w1)</text>
    <text x="553" y="62" font-weight="bold">M4 ⇓</text><text x="553" y="88" font-family="monospace">(r1,w0)</text>
    <text x="675" y="62" font-weight="bold">M5 ⇕</text><text x="675" y="88" font-family="monospace">(r0)</text>
  </g>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <path d="M187 125 L187 185"/><path d="M181 131 L187 123 L193 131"/>
    <path d="M309 125 L309 185"/><path d="M303 131 L309 123 L315 131"/>
    <path d="M431 123 L431 183"/><path d="M425 177 L431 185 L437 177"/>
    <path d="M553 123 L553 183"/><path d="M547 177 L553 185 L559 177"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="145" y="200">주소 0 → n-1</text><text x="267" y="200">주소 0 → n-1</text>
    <text x="389" y="200">주소 n-1 → 0</text><text x="511" y="200">주소 n-1 → 0</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="65" y="130">전체 = 0</text><text x="675" y="130">전체 = 0 확인</text>
  </g>
  <text x="10" y="236" fill="currentColor">각 셀은 0→1→0→1→0 으로 두 번 오르내리고, 매번 "쓰기 전 읽기" 로 직전 상태를 확인한다.</text>
  <text x="10" y="260" fill="currentColor">⇑ 두 번 + ⇓ 두 번 = aggressor 가 victim 의 아래에 있든 위에 있든 영향을 읽을 기회가 생긴다.</text>
</svg>
```

결함 모델 (이번 레슨의 mock SRAM 이 구현하는 것):

| 이름 | 정의 | 예 (물리 원인) |
|---|---|---|
| SAF `<0>`/`<1>` | 셀이 항상 0/1 | 셀 노드가 VSS/VDD 로 short |
| TF `<↑/0>` (TF-up) / `<↓/1>` (TF-down) | 0→1 (또는 1→0) 전이에 실패, 반대 방향은 정상 | 풀업/풀다운 트랜지스터 open, 약한 write driver |
| AF | 주소 → 셀 연결이 틀림: 다른 셀로 감(alias) / 두 셀이 같이 선택됨(multi) | 디코더 게이트 결함, 워드라인 short |
| CFin `<↑;~>` | aggressor a 가 전이하면 victim v 가 **반전** | 셀 간 커패시턴스 결합 |
| CFid `<↑;0>` 등 | a 가 전이하면 v 가 **특정 값으로** 강제 | 결합 + 한쪽으로 치우친 셀 |
| CFst `<0;1>` 등 | a 가 **상태** x 에 있는 동안 v 가 y 로 고정 | 셀 사이 저항성 bridge |
| DRDF | 읽으면 맞는 값을 주지만 셀이 뒤집힘 (deceptive read destructive) | 읽기 중 비트라인 교란 |

### Part A — March 엔진과 March C- 테이블

1비트/셀 SRAM 모델(`sram_t`, 16셀)과 `sram_read / sram_write` 는 주어진다. 할 일:

- `MARCH_C_MINUS` element 테이블을 채운다 (`MATS_PLUS`, `MATS_PP` 가 예시로 이미 있다).
- `march_run(m, alg, r)` — element 마다 `dir` 방향으로 주소를 돌고, 각 주소에서 ops 를 순서대로 실행. 읽기는 기대값과 비교해 `nfail` 과 **첫 실패**(element, op, addr, exp, act)를 기록, 총 연산 수 `nops` 를 센다.
- 정상 SRAM 은 PASS + `nops == 10n`, ⇓ element 가 실제로 n-1 부터 도는지, SA1/SA0 셀이 정확한 위치에서 잡히는지 확인한다.

### Part B — 결함 주입과 검출률 측정

- `cell_store` 에 TF(전이 실패)와 동적 coupling(CFin/CFid) 의미를, `sram_settle` 에 CFst 의미를 구현한다. AF·SAF·DRDF 는 주어진다.
- `detects(alg, f, n)` — **"보장 검출(guaranteed detection)"**: 4가지 power-up 상태(all0, all1, 0101, 1010) **모두에서** 실패가 나와야 검출로 친다. 실제 칩의 power-up 값은 셀마다 제멋대로라, 운 좋게 power-up 값 덕에 잡힌 건 커버리지가 아니다.
- 16셀 메모리의 모든 단일 결함(쌍 결함은 a≠v 인 모든 순서쌍 240개)에 대해 MATS+ / MATS++ / March C- 의 검출률 표를 출력하고, March C- 가 SAF·TF·AF·CF 를 100% 잡고 DRDF 는 0% 임을 CHECK 한다.

### Part C — ISP 라인버퍼 SRAM: word 단위 March C- + 실패 리포트

`ISP_LBUF` (베이스 `0x40020000`, 256 word × 32비트)를 `bus_ops_t { rd32, wr32 }` 뒤의 mock 으로 둔다 (SIV-B02 의 bus ops 구조를 축약해 넣었다). 할 일:

- `wmarch_c_minus(region, bgs, nbg, rep)` — Part A 의 **같은 테이블** 을 word 단위로 실행. `0/1` 대신 **data background** `D` 와 `~D` 를 쓰고, background 마다 반복한다.
- 결함 3종을 주입: ① word 0x50 의 bit 2 stuck-at-1 ② 디코더가 word-index bit 6 을 무시(aliasing) ③ word 0x21 안의 intra-word coupling (bit3 ↑ → bit4 = 1).
- 리포트 첫 줄이 정확한 주소·비트를 가리키는지, ③ 이 solid background(0x00000000)만으로는 **빠져나가고** 6개 background 로는 잡히는지 CHECK 한다.

## 2. Problem (English)

> **SRAM March test.**
>
> You are bringing up first silicon. An on-chip SRAM is accessible through a memory-mapped window.
>
> 1. Represent a March algorithm as a table of elements `{direction ∈ {UP, DOWN, ANY}, ops[] ∈ {r0, r1, w0, w1}}`. Implement `unsigned march_run(sram_t *m, const march_alg_t *alg, march_result_t *r)` that executes it over all `n` addresses, compares every read with its expected value, and records the number of failures, the first failure (element, op, address, expected, actual) and the total operation count. Encode March C- = `{⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕(r0)}`.
> 2. Implement transition faults and coupling faults (inversion, idempotent, state) in a bit-oriented SRAM model. A fault counts as *detected* only if the test fails for every power-up state you try. Measure the detection rate of MATS+ (5n), MATS++ (6n) and March C- (10n) for every fault class over all single faults of a 16-cell memory.
> 3. Run March C- on a 256 × 32-bit memory using data backgrounds `{0x00000000, 0x55555555, 0x33333333, 0x0F0F0F0F, 0x00FF00FF, 0x0000FFFF}`. On mismatch, emit `FAIL mem=<name> bg=<D> elem=M<k>/r addr=<abs addr> exp=<> act=<> diff=<> bits=[...]`. Show that an intra-word coupling fault escapes a solid background but is caught by the full set.
>
> Follow-ups: Why is March C- 10n and not 11n like March C? Why must `⇓` be the exact reverse of `⇑`? What does March C- *not* detect?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # SDK 27.2 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 실제 출력 끝부분: "6/29 passed" (exit 1) — TODO 를 채워 29/29 를 만든다
```

시작 코드에서는 March C- 테이블이 비어 있고 `march_run` 이 아무것도 안 하므로, Part B 표는 모든 칸이 `0.0%` 로 찍히고 `ops/cell` 행의 March C- 는 `0n` 이다. 정상 SRAM PASS 같은 일부 CHECK 는 "아무것도 안 하는 테스트도 통과한다" 는 함정 때문에 처음부터 PASS 한다 — 그래서 `nops == 10n` 같은 **테스트가 실제로 일했는지** 확인하는 CHECK 를 같이 둔다. 관찰용 `demo_bench.c` (하네스와 별개)는 §5 Part C 끝에서 쓴다.

## 4. 힌트

<details>
<summary>힌트 1 — 엔진은 3중 루프</summary>

`for element → for k in 0..n-1 (addr = DOWN ? n-1-k : k) → for op in element.ops`. 쓰기면 `sram_write(m, addr, op == W1)`, 읽기면 `exp = (op == R1)` 과 비교. 첫 실패는 `nfail` 이 0 → 1 이 될 때만 기록한다.

</details>

<details>
<summary>힌트 2 — 전이(transition)는 "실제로 바뀐 것" 기준</summary>

`old = cell[c]` 를 먼저 저장한다. TF-up 은 `old==0 && d==1` 일 때 저장값을 0 으로 남긴다. coupling 은 aggressor 의 **저장 전후 값이 실제로 달라졌을 때만** 발동 — 0 을 이미 0 인 셀에 쓰는 것은 전이가 아니다 (`w0` 을 반복해도 CF 는 안 터진다).

</details>

<details>
<summary>힌트 3 — CFst 는 "상태" 결함</summary>

`<x;y>` 는 이벤트가 아니라 **조건** 이다: aggressor 가 x 인 동안 victim 은 y. 그래서 쓰기 경로가 아니라 매 접근 뒤 부르는 `sram_settle()` 에서 `if (cell[a] == x) cell[v] = y;` 로 강제한다 (SAF 와 같은 자리).

</details>

<details>
<summary>힌트 4 — 보장 검출</summary>

`for p in 0..3: sram_init(&m, n, f, p); if (march_run(...) == 0) return false;` → 끝까지 오면 `true`. 한 번이라도 PASS 하면 그 결함은 "운에 맡긴" 것이다.

</details>

<details>
<summary>힌트 5 — word 단위 = 0/1 을 D/~D 로</summary>

`d = (op == R1 || op == W1) ? ~bg : bg`. 주소는 `base + 4*idx`, 읽기 불일치는 `report_fail(rep, r, bg, e, addr, d, act)`. 엔진 구조는 Part A 와 똑같다 — **테이블을 재사용** 한다.

</details>

## 5. 풀이

### Part A — 테이블 + 엔진: 알고리즘은 데이터다

March 알고리즘을 코드로 하드코딩하지 않고 **테이블** 로 두면, 같은 엔진으로 MATS+·March C-·March SS 를 바꿔 끼울 수 있다. 상용 MBIST 컨트롤러가 알고리즘을 "프로그래밍 가능" 하게 만드는 것도 같은 발상이다.

```c
static const march_alg_t MARCH_C_MINUS = { "March C-", 6, {
    { ANY, 1, { W0 } }, { UP, 2, { R0, W1 } }, { UP, 2, { R1, W0 } },
    { DOWN, 2, { R0, W1 } }, { DOWN, 2, { R1, W0 } }, { ANY, 1, { R0 } } } };
```

엔진의 핵심은 `addr = (dir == DOWN) ? n-1-k : k` 한 줄과 "쓰기 전 읽기" 순서다. 테스트가 확인하는 것:

| CHECK | 왜 필요한가 |
|---|---|
| 정상 SRAM → `nfail == 0` | 오탐(false fail)이 없어야 한다 — 첫 실리콘에서 테스트 버그를 HW 버그로 보고하면 신뢰를 잃는다 |
| `nops == 10n` | **아무것도 안 하는 테스트도 PASS** 한다. 테스트가 실제로 일했는지를 같이 본다 |
| `log[80..82] == 15,15,14` | M3 시작 = 연산 5n 번째. ⇓ 를 ⇑ 로 잘못 구현하면 CF 절반을 놓친다 (Part B) |
| SA1@5 → M1 op0, addr 5, exp 0 act 1, nfail 3 | SA1 은 r0 하는 M1·M3·M5 에서만 걸린다 |
| SA0@9 → M2 op0, nfail 2 | SA0 은 r1 하는 M2·M4 에서만 걸린다 |

실제 출력의 Part A 줄:

```text
[Part A] SA0@9 first fail: M2 op0 addr=9 exp=1 act=0 (nfail=2)
```

**왜 "첫 실패" 와 "실패 수" 를 둘 다?** 첫 실패는 디버그 시작점(어느 element 의 어느 op 에서 처음 틀렸나), 실패 수·패턴은 결함 종류 추정(연습 3 의 syndrome)에 쓴다. 양산 테스트(ATE)는 첫 실패에서 멈춰 시간을 아끼지만(stop-on-first-fail), 검증·불량 분석은 끝까지 돌려 **fail bitmap** 을 모은다.

### Part B — 결함 의미 구현과 측정

결함 의미는 두 군데로 나뉜다.

| 위치 | 결함 | 구현 |
|---|---|---|
| `decode()` (주어짐) | AF-alias, AF-multi | 주소 v → 셀 a / 셀 v 와 a 동시 (읽기는 wired-AND) |
| `cell_store()` | TF, CFin, CFid | `old` 와 저장값 비교 → 전이 판단 → victim 수정 |
| `sram_settle()` | SAF, CFst | 매 접근 뒤 "항상 성립하는 조건" 강제 |
| `sram_read()` (주어짐) | DRDF | 맞는 값을 돌려준 뒤 셀 반전 |

```c
if (c == f->v && f->kind == F_TF_UP && old == 0 && d == 1) nw = 0;   /* 0→1 실패 */
...
if (c == f->a && nw != old && f->kind >= F_CFIN_UP && f->kind <= F_CFID_D1) {
    bool up = (nw == 1);          /* aggressor 가 '실제로' 전이했을 때만 */
    ...
}
```

`solution.c` 를 돌린 **실제 측정 결과** (16셀, 4가지 power-up 모두에서 잡혀야 검출):

```text
[Part B] guaranteed detection (n=16 cells, all 4 power-up states)
fault       #faults     MATS+    MATS++  March C-
SA0              16    100.0%    100.0%    100.0%
SA1              16    100.0%    100.0%    100.0%
TF-up            16    100.0%    100.0%    100.0%
TF-down          16      0.0%    100.0%    100.0%
AF-alias        240    100.0%    100.0%    100.0%
AF-multi        240    100.0%    100.0%    100.0%
CFin<u;~>       240    100.0%    100.0%    100.0%
CFin<d;~>       240     50.0%     50.0%    100.0%
CFid<u;0>       240     50.0%     50.0%    100.0%
CFid<u;1>       240     50.0%     50.0%    100.0%
CFid<d;0>       240     50.0%     50.0%    100.0%
CFid<d;1>       240      0.0%      0.0%    100.0%
CFst<0;0>       240     50.0%     50.0%    100.0%
CFst<0;1>       240    100.0%    100.0%    100.0%
CFst<1;0>       240    100.0%    100.0%    100.0%
CFst<1;1>       240     50.0%     50.0%    100.0%
DRDF             16      0.0%      0.0%      0.0%
ops/cell                   5n        6n       10n
```

이 표를 읽는 법:

**① MATS+ 의 TF-down 0%.** MATS+ 는 `{⇕(w0); ⇑(r0,w1); ⇓(r1,w0)}`. 1→0 전이는 M2 의 `w0` 에서 일어나는데, 그 뒤에 그 셀을 **다시 읽는 연산이 없다**. power-up 값이 1 이면 M0 의 `w0` 실패가 M1 `r0` 에서 우연히 잡히지만, all0 으로 켜지면 못 잡는다 → 보장 0%. MATS++ 가 마지막 element 에 `r0` 하나를 붙인(`⇓(r1,w0,r0)`) 이유가 정확히 이것이다 → 100%. 규칙: **모든 쓰기 전이 뒤에는 그 값을 확인하는 읽기가 있어야 한다.**

**② coupling 의 50% = 방향 문제.** CFid `<↑;1>` (a 가 0→1 이면 v=1) 을 보자.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">MATS+ 의 M1 ⇑(r0,w1) 에서 CFid&lt;↑;1&gt; — aggressor 위치에 따라 결과가 갈린다</text>
  <text x="10" y="56" fill="currentColor" font-weight="bold">a &lt; v (a=2, v=4)</text>
  <g stroke="currentColor">
    <rect x="160" y="40" width="40" height="28" class="f-mut"/><rect x="200" y="40" width="40" height="28" class="f-mut"/>
    <rect x="240" y="40" width="40" height="28" class="f-acc"/><rect x="280" y="40" width="40" height="28" class="f-mut"/>
    <rect x="320" y="40" width="40" height="28" class="f-warn"/><rect x="360" y="40" width="40" height="28" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="180" y="59">0</text><text x="220" y="59">1</text><text x="260" y="59">a</text>
    <text x="300" y="59">3</text><text x="340" y="59">v</text><text x="380" y="59">5</text>
  </g>
  <path d="M160 82 L400 82" stroke="currentColor" stroke-width="2"/><path d="M394 77 L402 82 L394 87" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="420" y="50" fill="currentColor">① a: r0, w1 → a 가 0→1, v 가 1 로 강제</text>
  <text x="420" y="72" fill="currentColor">② v: r0 → 1 읽힘 → <tspan class="f-ok" font-weight="bold">FAIL 검출</tspan></text>
  <text x="10" y="146" fill="currentColor" font-weight="bold">a &gt; v (a=10, v=4)</text>
  <g stroke="currentColor">
    <rect x="160" y="130" width="40" height="28" class="f-mut"/><rect x="200" y="130" width="40" height="28" class="f-warn"/>
    <rect x="240" y="130" width="40" height="28" class="f-mut"/><rect x="280" y="130" width="40" height="28" class="f-mut"/>
    <rect x="320" y="130" width="40" height="28" class="f-acc"/><rect x="360" y="130" width="40" height="28" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="180" y="149">3</text><text x="220" y="149">v</text><text x="260" y="149">5</text>
    <text x="300" y="149">…</text><text x="340" y="149">a</text><text x="380" y="149">11</text>
  </g>
  <path d="M160 172 L400 172" stroke="currentColor" stroke-width="2"/><path d="M394 167 L402 172 L394 177" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="420" y="140" fill="currentColor">① v: r0 OK, w1 → v 는 이미 1</text>
  <text x="420" y="162" fill="currentColor">② a: 0→1, v=1 강제 → 원래 1 이라 티가 안 남</text>
  <text x="420" y="184" fill="currentColor">③ M2 ⇓ 의 a↓ 는 무관 → <tspan class="f-warn" font-weight="bold">놓침</tspan></text>
  <rect x="10" y="206" width="720" height="104" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="24" y="230" fill="currentColor">March C- 는 M3 ⇓(r0,w1) 에서 같은 상황을 위→아래로 다시 만든다.</text>
  <text x="24" y="252" fill="currentColor">a(10) 가 먼저 0→1 → v(4) 강제 1 → v 차례의 r0 이 1 을 읽어 검출.</text>
  <text x="24" y="274" fill="currentColor">⇑ 와 ⇓ 각각에서 a↑ 와 a↓ 를 모두 만들어서 CF 전 종류 100%.</text>
  <text x="24" y="296" fill="currentColor" font-size="12">측정: MATS+ (v=4, a=2) 검출 · (v=4, a=10) 놓침 · March C- (v=4, a=10) 검출</text>
</svg>
```

MATS+ 는 a↑ 를 ⇑ 에서만, a↓ 를 ⇓ 에서만 만든다. 그래서 각 CF 종류에서 "a 가 v 보다 앞에 처리되는 절반" 만 잡는다(50%). CFid `<↓;1>` 은 0% — a↓ 는 (power-up 우연을 빼면) ⇓(M2) 에서만 생기는데 a>v 면 v 가 아직 1 이라 강제 1 이 티가 안 나고, a<v 면 v 가 이미 0 으로 바뀐 **뒤** 라 다시 읽히지 않는다. 반면 CFin `<↑;~>` 는 MATS+ 도 100%: a<v 면 M1 에서 v 의 r0 이 반전을 보고, a>v 면 반전된 v 를 M2 ⇓ 의 r1 이 본다.

**③ March C- 가 CF 를 다 잡는 조건.** 각 (aggressor 전이 방향 ↑/↓) × (aggressor 가 victim 의 아래/위) 4가지에 대해, 전이 직후 victim 을 **victim 이 다시 쓰이기 전에** 읽는 element 가 있어야 한다. 아래 표가 March C- 가 그 4칸을 모두 채우는 방식이다.

| 경우 | a 의 전이를 만드는 element | victim 을 읽는 곳 |
|---|---|---|
| a < v, a↑ | M1 ⇑(r0,w1) | 같은 M1 에서 v 의 r0 |
| a < v, a↓ | M2 ⇑(r1,w0) | 같은 M2 에서 v 의 r1 |
| a > v, a↑ | M3 ⇓(r0,w1) | 같은 M3 에서 v 의 r0 |
| a > v, a↓ | M4 ⇓(r1,w0) | 같은 M4 에서 v 의 r1 |

victim 이 0 일 때와 1 일 때 둘 다 확인되므로 `<↑;0>` 와 `<↑;1>` 모두 잡힌다 (강제값이 victim 의 현재값과 같으면 그 element 에서는 안 보이지만, 다른 element 에서는 victim 값이 반대다). 이것이 March C- 가 "unlinked CF 전부" 를 잡는다는 고전 결과의 직관이다.

**④ AF 는 MATS+ 도 100%.** 주소 디코더 결함 검출 조건은 `⇑(rx, …, wx̄)` 와 `⇓(rx̄, …, wx)` 가 둘 다 있는 것이다 — alias 된 두 주소 중 나중에 처리되는 쪽이 먼저 처리된 쪽이 바꿔 놓은 값을 읽게 된다. MATS+ 는 정확히 이 두 element 를 가진 최소 알고리즘이다.

**⑤ DRDF 0% — 커버리지 밖.** DRDF 셀은 읽기에 **맞는 값** 을 주고 뒤집힌다. March C- 의 모든 읽기 바로 뒤에는 그 셀에 대한 쓰기(뒤집힌 것을 덮음)가 오거나(M1~M4), 마지막 읽기(M5)라 다시 안 읽힌다. 같은 셀을 **연속 두 번** 읽어야 잡힌다 → 연습 2 의 March C-+R, 그리고 March SS 같은 알고리즘이 `r,r` 연산을 넣는 이유. 데이터 유지(retention) 결함도 March C- 는 못 잡는다 — 쓰고 나서 일정 시간(또는 저전압 retention 모드) 기다렸다 읽는 delay element 가 필요하다.

**알고리즘 비교 요약** (측정 + 고전 문헌):

| 알고리즘 | 복잡도 | SAF | AF | TF | CF (in/id/st) | 비고 |
|---|---|---|---|---|---|---|
| MATS+ | 5n | ✔ | ✔ | 반만 (TF-up) | 부분 | AF 검출 최소 조건 |
| MATS++ | 6n | ✔ | ✔ | ✔ | 부분 | MATS+ 에 r0 하나 |
| March C- | 10n | ✔ | ✔ | ✔ | ✔ (unlinked) | 산업 기본값 |
| March C (원조) | 11n | ✔ | ✔ | ✔ | ✔ | 중간 `⇕(r0)` 가 중복 → 빼서 C- |
| GALPAT 류 | O(n²) | ✔ | ✔ | ✔ | ✔ + α | 64Ki word 면 n² 만 해도 수십 초 (연습 1) |

### Part C — 온칩 SRAM 에서: word, background, 리포트

실제 SRAM 은 1비트가 아니라 **word** 단위(32/64/128비트 + ECC)로 접근한다. 한 번 쓰면 32개 셀이 동시에 쓰이므로, word **사이** 의 결함은 March C- 가 그대로 잡지만 word **안** 의 결함(인접 비트 coupling)은 쓰는 데이터 패턴에 달려 있다. 그래서 0/1 대신 data background `D` 와 `~D` 를 쓴다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Part C 구조 — 같은 엔진, bus ops 만 교체</text>
  <rect x="10" y="40" width="200" height="110" rx="8" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="22" y="62" font-weight="bold">wmarch_c_minus()</text>
    <text x="22" y="84" font-size="12">MARCH_C_MINUS 테이블</text>
    <text x="22" y="104" font-size="12">× background 6개</text>
    <text x="22" y="124" font-size="12">D / ~D 쓰고 읽기</text>
    <text x="22" y="142" font-size="12">불일치 → report_fail()</text>
  </g>
  <rect x="270" y="40" width="160" height="60" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="282" y="64" fill="currentColor" font-weight="bold">bus_ops_t</text>
  <text x="282" y="86" fill="currentColor" font-family="monospace" font-size="12">rd32 / wr32</text>
  <path d="M210 70 L264 70" stroke="currentColor" stroke-width="2"/><path d="M258 65 L266 70 L258 75" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="490" y="36" width="240" height="78" rx="8" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="502" y="58" font-weight="bold">LBUF_MOCK_BUS (호스트)</text>
    <text x="502" y="78" font-size="12">g_lbuf[256] + lbuf_faults_t</text>
    <text x="502" y="98" font-size="12">stuck bit / addr bit 무시 / intra-word CF</text>
  </g>
  <rect x="490" y="126" width="240" height="60" rx="8" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="502" y="148" font-weight="bold">MMIO_BUS (실보드)</text>
    <text x="502" y="170" font-family="monospace" font-size="12">*(volatile u32*)0x40020000</text>
  </g>
  <path d="M430 66 L484 66" stroke="currentColor" stroke-width="2"/><path d="M478 61 L486 66 L478 71" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M430 86 L484 150" stroke="currentColor" stroke-dasharray="5 4"/>
  <rect x="10" y="206" width="720" height="82" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="22" y="228">bg 0x00000000  bit3=0 bit4=0  같은 방향 전이  → CFid&lt;↑;0&gt;, &lt;↓;1&gt;, CFin 검출</text>
    <text x="22" y="250">bg 0x55555555  bit3=0 bit4=1  반대 방향 전이  → CFid&lt;↑;1&gt;, &lt;↓;0&gt; 검출</text>
    <text x="22" y="272">bg 0x33.. 0x0F.. 0x00FF.. 0x0000FFFF : 어떤 비트 쌍이든 한 번은 서로 다름</text>
  </g>
</svg>
```

왜 이 6개인가: background k (k=0..4)는 "비트 번호의 k 번째 자리가 0 인 비트" 를 1 로 둔다(`0x55555555` = 비트번호 bit0 이 0 인 비트들). 서로 다른 두 비트 번호는 어떤 자리에서든 다르므로, **모든 비트 쌍이 어떤 background 에서는 반대 값** 을 갖는다. 여기에 solid(0x00000000, 모든 비트 같은 값)를 더한 1 + log₂32 = 6개면 intra-word CFid 의 "같은 방향 전이" 와 "반대 방향 전이" 경우가 모두 만들어진다 (연습 4·5 에서 측정으로 확인).

실제 출력 (`solution.c` Part C):

```text
[Part C] word-oriented March C- on ISP_LBUF @0x40020000, 256 words
  [good, 6 bg] nfail=0 nops=15360 failbits=0x00000000
  [SA1 word0x50 bit2] nfail=14 nops=15360 failbits=0x00000004
    FAIL mem=ISP_LBUF bg=0x00000000 elem=M1/r addr=0x40020140 exp=0x00000000 act=0x00000004 diff=0x00000004 bits=[2]
    FAIL mem=ISP_LBUF bg=0x00000000 elem=M3/r addr=0x40020140 exp=0x00000000 act=0x00000004 diff=0x00000004 bits=[2]
    FAIL mem=ISP_LBUF bg=0x00000000 elem=M5/r addr=0x40020140 exp=0x00000000 act=0x00000004 diff=0x00000004 bits=[2]
    FAIL mem=ISP_LBUF bg=0x55555555 elem=M2/r addr=0x40020140 exp=0xAAAAAAAA act=0xAAAAAAAE diff=0x00000004 bits=[2]
  [addr bit6 ignored] nfail=3072 nops=15360 failbits=0xFFFFFFFF
    FAIL mem=ISP_LBUF bg=0x00000000 elem=M1/r addr=0x40020100 exp=0x00000000 act=0xFFFFFFFF diff=0xFFFFFFFF bits=[32 bits]
    FAIL mem=ISP_LBUF bg=0x00000000 elem=M1/r addr=0x40020104 exp=0x00000000 act=0xFFFFFFFF diff=0xFFFFFFFF bits=[32 bits]
    FAIL mem=ISP_LBUF bg=0x00000000 elem=M1/r addr=0x40020108 exp=0x00000000 act=0xFFFFFFFF diff=0xFFFFFFFF bits=[32 bits]
    FAIL mem=ISP_LBUF bg=0x00000000 elem=M1/r addr=0x4002010C exp=0x00000000 act=0xFFFFFFFF diff=0xFFFFFFFF bits=[32 bits]
  [intra-word CFid, solid bg only] nfail=0 nops=2560 failbits=0x00000000
  [intra-word CFid, 6 bg] nfail=7 nops=15360 failbits=0x00000010
    FAIL mem=ISP_LBUF bg=0x55555555 elem=M2/r addr=0x40020084 exp=0xAAAAAAAA act=0xAAAAAABA diff=0x00000010 bits=[4]
    FAIL mem=ISP_LBUF bg=0x55555555 elem=M4/r addr=0x40020084 exp=0xAAAAAAAA act=0xAAAAAABA diff=0x00000010 bits=[4]
    FAIL mem=ISP_LBUF bg=0x33333333 elem=M2/r addr=0x40020084 exp=0xCCCCCCCC act=0xCCCCCCDC diff=0x00000010 bits=[4]
    FAIL mem=ISP_LBUF bg=0x33333333 elem=M4/r addr=0x40020084 exp=0xCCCCCCCC act=0xCCCCCCDC diff=0x00000010 bits=[4]

29/29 passed
```

디버그 엔지니어가 이 리포트로 하는 일:

| 리포트 패턴 | 해석 | 다음 행동 |
|---|---|---|
| 한 주소, 한 비트, `exp` 비트=0 일 때만 실패 (SA1 줄: bg 0 의 M1/M3/M5, bg 0x5555 의 M2) | 그 셀 stuck-at-1. 14건 = 그 비트가 0 이어야 하는 읽기 수 | 같은 열(bit 2)의 다른 행도 실패하는지 → 열 결함이면 redundancy 열로 repair 가능한지 (BIRA/eFuse) |
| `idx 64` 부터 연속 주소, 32비트 전부 반전 (3072건) | 주소 0x100(word 64) 부터 다른 주소가 쓴 값이 보임 → 디코더 결함 / 주소선 short | 64 = 2⁶ → **word-index bit 6** 의심. 원인 비트 추정·walking-address 는 SIV-B06 참고 |
| solid 에선 PASS, 특정 bg 에서만 한 비트 | intra-word coupling (이웃 비트 간섭) — 데이터 의존 | 어떤 bg 에서 잡혔나 → 비트 3 과 4 의 관계 추정, 레이아웃(인접 열) 확인 |
| `failbits` 가 한 비트만 | 데이터 경로(sense amp/열) 결함 후보 | 다른 메모리 인스턴스에도 같은 비트면 공통 IP/버스 문제 |

리포트 형식이 이렇게 생긴 이유: `mem=` 로 어느 인스턴스인지(칩에 SRAM 이 수백 개), `bg=`/`elem=` 로 재현 조건, **절대 주소** 로 JTAG 에서 바로 `mdw 0x40020140` 해 볼 수 있게, `diff`/`bits` 로 사람이 비트를 세지 않게. 첫 4줄만 남기고 나머지는 `nfail`/`failbits` 로 요약 — 3072줄을 UART 로 뿜으면 로그가 테스트보다 오래 걸린다.

**실전에서 꼭 따라오는 조건들**

- **ECC 를 끄거나 raw 로 읽어라.** SECDED ECC 가 켜진 SRAM 은 1비트 stuck 을 조용히 고쳐서 돌려준다 → March 가 PASS. ECC bypass/diagnostic 모드로 읽거나, ECC corrected-error 카운터를 같이 확인한다.
- **캐시를 우회하라.** 캐시가 켜진 상태로 SRAM 영역을 테스트하면 캐시를 테스트하게 된다. 테스트 영역을 non-cacheable(MPU/MMU 속성)로 두거나 캐시 off.
- **테스트 코드가 사는 메모리는 테스트할 수 없다.** 스택·코드·변수가 올라간 SRAM 을 March 로 덮으면 자기 자신을 지운다. ROM/다른 SRAM 에서 실행하거나 레지스터만 쓰는 루틴으로, 또는 영역을 나눠 백업 → 테스트 → 복원(transparent test). IEC 60730 Class B 자기진단 라이브러리(예: ST X-CUBE-CLASSB)가 부팅 시 RAM 을 March 계열로 검사하고 런타임엔 조각 단위로 검사하는 구조가 이것이다.
- **누가 실행하나.** 양산 ATE 는 내장 MBIST(Tessent MemoryBIST, Synopsys STAR Memory System 같은 IP)를 JTAG/IJTAG 로 구동해 수백 개 인스턴스를 병렬로 돌리고 repair 정보를 eFuse 에 굽는다. post-silicon 검증 엔지니어의 SW March(이 레슨)는 **시스템 경로**(CPU → 인터커넥트 → SRAM 컨트롤러, 실제 클럭·전압)로 같은 메모리를 확인하고, MBIST 결과와 교차 검증하며, 전압/온도 corner(shmoo)에서 돌린다. JTAG 디버그 포트로 word 하나씩 읽고 쓰면 접근당 수 µs 가 걸리므로 큰 메모리는 on-chip 펌웨어로 돌리고 결과만 받아온다.
- **pre-silicon 대응물.** UVM RAL 의 `uvm_mem_walk_seq` / `uvm_mem_access_seq` 가 메모리 모델에 대한 walking·접근 테스트다. 같은 March 테이블을 C 로 두면 시뮬레이션(DPI)·에뮬레이터·실보드에서 재사용할 수 있다 (SIV-B13 참고).

**호스트 시간 측정** — `cc -std=c11 -Wall -Wextra -O2 demo_bench.c -o demo_bench && ./demo_bench` (실제 출력, Apple M6, Apple clang 21.0.0):

```text
words=4194304 (16 MiB), best of 5, fails=0
MATS+    5n:    2.78 ms  0.133 ns/access
March C- 10n:    5.58 ms  0.133 ns/access
ratio C-/MATS+ = 2.00 (theory 2.00)
```

비율은 정확히 이론대로 2배다. 절대값(0.13 ns/접근)은 순차 접근이 캐시·쓰기 버퍼·프리페처에 흡수된 **호스트 DRAM** 수치일 뿐이다 — 실제 칩에서 non-cacheable 버스 접근은 접근당 수~수십 ns 이므로 연습 1 의 10 ns 가정이 더 현실적이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 29/29 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san
# ASan/UBSan: 경고 없음, 29/29 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| ⇓ element 를 ⇑ 로 돌림 (방향 무시) | a > v 인 coupling 을 다 놓친다 — March C- 가 MATS+ 수준으로 떨어짐 | `addr = (dir == DOWN) ? n-1-k : k` |
| ⇓ 를 "아무 다른 순서" 로 돌림 | March 이론은 주소 순서가 임의여도 되지만 **⇓ 는 ⇑ 의 정확한 역순** 이어야 한다 | 같은 주소 생성기를 거꾸로 |
| element 단위로 "전체 읽기 → 전체 쓰기" | `(r0,w1)` 은 **한 주소에서** r0 다음 w1. 나눠 돌리면 CF 검출 조건이 깨진다 | 주소 루프 안에 op 루프 |
| CF 를 쓰기 이벤트마다 발동 | 0 을 0 에 쓰는 건 전이가 아니다. 모델이 과민하면 커버리지가 부풀려진다 | `nw != old` 일 때만 |
| 랜덤 초기값으로 한 번 돌려 커버리지 계산 | MATS+ TF-down 처럼 power-up 운으로 잡힌 것을 커버리지로 셈 | 여러 power-up 상태 모두에서 실패해야 검출 |
| `nfail == 0` 만 CHECK | 아무것도 안 하는 테스트도 PASS | `nops == 10n` 도 확인 |
| word 메모리에 0x00000000/0xFFFFFFFF 만 | intra-word CF 의 1/3 이 빠져나감 (연습 5: 66.7%) | background 1 + ⌈log₂W⌉ 개 |
| ECC 켠 채 테스트 | 1비트 결함이 정정되어 PASS | ECC bypass 또는 정정 카운터 확인 |
| 실패마다 printf | 디코더 결함 한 번에 수천 줄 → 로그가 테스트보다 느림 | 첫 K 줄 + `nfail` + `failbits` 요약 |

## 8. 면접 Q&A

<details>
<summary>Q1. March C- 를 써 보세요. 왜 10n 이고, March C 와 무엇이 다른가요?</summary>

`{⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕(r0)}` — 연산 1+2+2+2+2+1 = 10 개/셀. 원래 March C 는 가운데에 `⇕(r0)` 가 하나 더 있어 11n 인데, 그 element 가 잡는 결함은 다음 element(M3 의 r0)가 이미 잡으므로 중복이라 빼서 March C- 가 됐다(van de Goor). **꼬리질문: "1Mi word × 6 background, 접근당 10 ns 면?"** → 10 × 2²⁰ × 6 × 10 ns ≈ 0.63 s.

</details>

<details>
<summary>Q2. March C- 가 잡는 결함과 못 잡는 결함은?</summary>

잡음: SAF, TF, AF, unlinked CFin/CFid/CFst (측정: 16셀 전수에서 모두 100%). 못 잡음: DRDF 같은 연속 읽기 결함(측정 0%), data retention fault(대기 시간 필요), stuck-open 일부, linked fault(두 CF 가 서로 가리는 경우), neighborhood pattern sensitive fault, 동적(속도 의존) 결함 일부. **꼬리질문: "DRDF 를 잡으려면?"** → 같은 셀을 연속 두 번 읽는 op 추가 (`⇕(r0,r0)`, 연습 2 에서 0% → 100% 측정). March SS·March RAW 같은 알고리즘이 이 계열.

</details>

<details>
<summary>Q3. 왜 ⇑ 와 ⇓ 가 둘 다 필요한가요? 주소 순서는 꼭 0,1,2,… 여야 하나요?</summary>

coupling 의 aggressor 가 victim 보다 앞에 처리되는 경우만 같은 element 안에서 영향을 읽을 수 있다. ⇑ 만 있으면 a<v 절반만, ⇓ 를 더해야 a>v 절반도 잡힌다 (MATS+ 의 CF 50% 가 그 증거). 순서는 0,1,2… 일 필요가 없다 — **임의의 순서라도 ⇓ 가 그 정확한 역순이면** 이론이 성립한다. 그래서 물리 배치(row/column scrambling)를 몰라도 March 는 쓸 수 있고, MBIST 는 하드웨어가 만들기 쉬운 순서(예: column-fast vs row-fast)를 고른다. **꼬리질문: "그럼 물리 인접 셀 간 결함을 일부러 노리려면?"** → 논리-물리 주소 매핑(scramble)을 알아야 checkerboard 같은 물리 패턴을 만들 수 있다.

</details>

<details>
<summary>Q4. SRAM 에 ECC 가 있다면 March 테스트는 어떻게 하나요?</summary>

ECC 가 켜져 있으면 1비트 결함은 정정되어 PASS 로 보인다. ① ECC bypass / diagnostic 모드로 데이터+체크비트를 raw 로 쓰고 읽는다 — 체크비트 셀도 테스트 대상이다. ② 안 되면 ECC on 상태에서 돌리되 corrected/uncorrected 에러 카운터·상태 레지스터를 테스트 후 반드시 읽어 0 인지 확인한다(W1C 클리어 주의, SIV-B04). ③ ECC 로직 자체는 별도로 에러 주입(syndrome inject) 테스트를 한다. **꼬리질문: "ECC 있는데 왜 굳이 테스트?"** → 양산 결함이 있는 셀은 수명 중 두 번째 에러가 생기면 정정 불가가 된다. 결함 셀은 repair(redundancy) 하는 게 원칙.

</details>

<details>
<summary>Q5. 부팅 펌웨어가 자기가 쓰는 SRAM 을 March 로 테스트해야 한다면?</summary>

스택·.data·.bss 가 그 SRAM 에 있으므로 C 런타임 초기화 **전에**, 레지스터만 쓰는 루틴(보통 어셈블리)으로 돌리거나, 다른 메모리(ROM/TCM/다른 SRAM 뱅크)에서 실행한다. 런타임 중이면 영역을 작은 블록으로 나눠 인터럽트를 막고 백업 → March → 복원하는 transparent test 를 한다 (IEC 60730 Class B 라이브러리 방식). 캐시는 끄거나 영역을 non-cacheable 로. **꼬리질문: "테스트 루틴이 쓰는 스택은?"** → 없다. 레지스터만 쓰거나 테스트하지 않는 별도 영역에 둔다.

</details>

<details>
<summary>Q6. MBIST 가 있는데 SW March 테스트를 왜 또 하나요? ATE / SLT / 시스템 검증에서 각각 누가 무엇을?</summary>

ATE(웨이퍼/패키지 테스트)는 MBIST 를 스캔/JTAG 로 구동해 빠르게 pass/fail + repair 정보를 얻는다. SLT(system-level test)와 post-silicon 시스템 검증은 실제 펌웨어가 **CPU 와 인터커넥트를 통해** 메모리를 접근하는 경로 — 버스 브리지, 주소 맵, 클럭 도메인, 전압/온도 corner — 를 확인한다. MBIST 는 메모리 인스턴스 바로 옆에서 돌기 때문에 그 경로의 버그(예: 인터커넥트 주소 디코드, 잘못된 접근 폭)는 못 본다. 또 MBIST 결과와 SW 결과가 다르면 그것 자체가 중요한 단서다.

</details>

<details>
<summary>Q7. 테스트가 실패했을 때 로그에 무엇을 남기나요?</summary>

메모리 인스턴스 이름, 알고리즘·background·element·op(재현 조건), 절대 주소, expected/actual/diff 와 실패 비트 목록, 총 실패 수와 실패 비트 OR(failbits), 그리고 보드/칩 ID·전압·온도·클럭(조건). 첫 K 개만 상세히, 나머지는 요약 — 디코더 결함은 수천 건을 쏟아낸다. 리포트 집계·종료 코드는 SIV-B14, JSON 로그 프레임워크는 SIV-B08 참고. **꼬리질문: "실패 주소들이 2의 거듭제곱 간격이면?"** → 주소선/디코더 결함 (SIV-B06).

</details>

## 9. 추가 연습

### 연습 1 — 비용 계산

`ops_per_cell()` 과 `test_time_ms(alg, words, nbg, ns_per_access)` 로 64Ki word 메모리(256 KiB)에서 MATS+, March C-, March C- × 6 background 의 시간을 계산하고, O(n²) 패턴의 하한(n² 접근)과 비교하라. 접근당 10 ns.

<details>
<summary>답 보기</summary>

시간 = ops/cell × words × nbg × t. 실제 출력:
`MATS+ 3.2768 ms, March C- 6.5536 ms, March C- x6 bg 39.3216 ms, n^2 42949.7 ms`.
March C- 는 background 를 6번 돌려도 40 ms 인데, n² 패턴은 **하한만** 43 초다. 이것이 O(n) March 가 산업 표준이 된 이유.

</details>

### 연습 2 — DRDF 와 March C-+R

마지막 element 를 `⇕(r0,r0)` 로 바꾼 March C-+R(11n)을 정의하고 DRDF 검출률과, 기존 결함 커버리지가 유지되는지 측정하라.

<details>
<summary>답 보기</summary>

첫 `r0` 은 맞는 값 0 을 주고 셀을 1 로 뒤집는다 → 두 번째 `r0` 이 1 을 읽어 FAIL. 실제 출력: `DRDF guaranteed detection: March C- 0/16, March C-+R 16/16`, 그리고 SAF~CFst 전부 100% 유지(CHECK). 비용 +1n 으로 결함 클래스 하나를 산다 — 알고리즘 선택은 항상 "목표 결함 목록 → 최소 연산" 문제다.

</details>

### 연습 3 — syndrome 으로 결함 종류 구별

`march_result_t` 에 `synd`(bit `e*4+i` = element e 의 i번째 op 에서 실패)를 추가하고, victim 셀 7 의 SA0 / TF-up / SA1 / TF-down syndrome 을 power-up all0, all1 에서 비교하라.

<details>
<summary>답 보기</summary>

실제 출력:

```text
    SA0      power-up=all0 : M2.op0 M4.op0
    SA0      power-up=all1 : M2.op0 M4.op0
    TF-up    power-up=all0 : M2.op0 M4.op0
    TF-up    power-up=all1 : M2.op0 M4.op0
    SA1      power-up=all0 : M1.op0 M3.op0 M5.op0
    SA1      power-up=all1 : M1.op0 M3.op0 M5.op0
    TF-down  power-up=all0 : M3.op0 M5.op0
    TF-down  power-up=all1 : M1.op0 M3.op0 M5.op0
```

SA0 과 TF-up 은 March C- 로 **구별 불가** (1 이 되지 못한다는 관찰은 같다). TF-down 은 power-up 이 0 일 때만 SA1 과 구별된다(M1 에서 아직 1→0 전이가 없었으므로). 진단(diagnosis)이 목적이면 `⇕(w1); ⇕(r1)` 같은 element 를 추가해 "한 번이라도 1 이 써지는가" 를 따로 묻는다. 검출(detection)용 알고리즘과 진단용 알고리즘은 다르다.

</details>

### 연습 4 — data background 생성기

`make_backgrounds(w, out)` — 폭 w 비트 word 의 background 1 + ⌈log₂w⌉ 개를 만들고, 모든 비트 쌍이 어떤 background 에서는 서로 다른 값을 갖는지 검사하라 (w = 8, 12, 16, 32, 64).

<details>
<summary>답 보기</summary>

`bg_k` 의 비트 b = 1 iff `(b >> k) & 1 == 0`. 두 비트 번호 i≠j 는 어떤 자리 k 에서 다르므로 `bg_k` 에서 서로 다른 값. 개수는 4, 5, 5, 6, 7 (12비트는 ⌈log₂12⌉ = 4 → 5개). 실제 출력: `W=32 backgrounds: 0x00000000 0x55555555 0x33333333 0x0F0F0F0F 0x00FF00FF 0x0000FFFF`. solid 하나만으로는 분리 실패(CHECK).

</details>

### 연습 5 — intra-word coupling 커버리지 측정

1-word 메모리에 intra-word CF 6종(CFid 4종 + CFin 2종)을 모든 순서쌍(32×31 = 992)에 주입하고 solid background vs 6 background 의 보장 검출률을 측정하라.

<details>
<summary>답 보기</summary>

실제 출력:

```text
    fault        solid bg       6 bg
    CFid<u;0>      100.0%     100.0%
    CFid<u;1>        0.0%     100.0%
    CFid<d;0>        0.0%     100.0%
    CFid<d;1>      100.0%     100.0%
    CFin<u;~>      100.0%     100.0%
    CFin<d;~>      100.0%     100.0%
    all             66.7%     100.0%
```

solid 에서는 aggressor 와 victim 이 **항상 같은 방향으로 같이** 전이한다. `<↑;1>` 은 victim 도 1 로 쓰이는 중이라 강제가 안 보이고, `<↓;0>` 도 마찬가지 → 0%. 반대 방향 전이를 만드는 background(두 비트가 다른 값)가 있어야 잡힌다. 그래서 solid 를 **빼면 안 되고**(같은 방향 경우) 분리 background 도 필요하다(반대 방향 경우).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 18/18 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 경고 없음
```

## 10. 핵심 정리

- March C- = `{⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕(r0)}`, **10n**. March C(11n)에서 중복 `⇕(r0)` 를 뺀 것.
- 측정: March C- 는 SAF·TF·AF·CFin·CFid·CFst 100%, DRDF 0%. MATS+(5n)는 SAF·AF 100%, TF-down 0%, CF 대부분 50%.
- **모든 전이 뒤에 확인 읽기**(MATS+ → MATS++), **⇑ 와 ⇓ 둘 다**(aggressor 위/아래), ⇓ 는 ⇑ 의 정확한 역순 (순서 자체는 임의).
- 커버리지는 **power-up 상태와 무관하게** 잡혀야 진짜. 정상 PASS 만 보지 말고 `nops` 로 테스트가 실제로 일했는지 확인.
- word 메모리 = background 1 + ⌈log₂W⌉ 개 (32비트 → 6). solid 만 쓰면 intra-word CF 66.7%.
- 실전: ECC bypass, non-cacheable, 테스트 코드는 테스트 대상 밖, MBIST(ATE) ↔ SW March(시스템 경로) 교차 확인.
- 리포트 = mem / bg / elem / 절대 addr / exp / act / diff / bits + nfail·failbits 요약.

## 참고자료

- A. J. van de Goor, *Testing Semiconductor Memories: Theory and Practice*, Wiley, 1991 — 결함 모델, March C-, AF/CF 검출 조건
- A. J. van de Goor, "Using March Tests to Test SRAMs", *IEEE Design & Test of Computers*, 1993
- A. J. van de Goor, I. Tlili, "March Tests for Word-Oriented Memories", DATE 1998 — data background
- S. Hamdioui, *Testing Static Random Access Memories: Defects, Fault Models and Test Patterns*, Springer, 2004 — DRDF 등 읽기 결함, March SS
- L.-T. Wang, C.-W. Wu, X. Wen, *VLSI Test Principles and Architectures*, Morgan Kaufmann, 2006 — Ch. 8 Memory Testing and BIST
- [Accellera UVM 1.2 Class Reference — uvm_mem_walk_seq, uvm_mem_access_seq](https://www.accellera.org/downloads/standards/uvm)
- [ST X-CUBE-CLASSB — IEC 60730 Class B self-test library (RAM March test)](https://www.st.com/en/embedded-software/x-cube-classb.html)
- 관련 레슨: SIV-B02 (mock MMIO / bus ops), SIV-B05 (walking 1s/0s), SIV-B06 (주소 디코드·aliasing), SIV-B08 (테스트 프레임워크), SIV-B14 (리포트)
