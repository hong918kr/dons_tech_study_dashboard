---
id: MEM-M07
time: 90분
updated: 2026-11-08
---
# MEM-M07 메모리 일관성 모델 시뮬레이터: SC·TSO·ARM 허용 결과 열거

## 학습 목표

- 리트머스(litmus) 프로그램 하나에서 **후보 실행(candidate execution)** 을 전부 만든다. 후보 실행은 rf(어느 write 를 읽었나)와 co(위치별 write 순서)를 고른 것이다. 그리고 fr = rf⁻¹;co 를 유도한다.
- SC · x86-TSO · ARMv8 을 **"이 관계들의 합이 사이클이 없어야 한다"** 는 공리(axiom)로 코드에 쓰고, MP/SB/LB/IRIW/2+2W 와 fence·acquire/release 변형의 허용 결과 집합을 계산한다. 결과가 논문의 알려진 결과와 같은지도 확인한다.
- "왜 금지인가" 를 **사이클 한 줄**(`po → rf → po → fr`)로, "왜 허용인가" 를 **"이 모델이 순서를 보장하지 않는 po 간선"** 으로 설명할 수 있다.
- 모델 예측과 Apple M6 실측(ARM-M05 · C-A02 · C-A03 숫자)을 대조한다. **허용 ≠ 관찰**, 그리고 **금지인데 관찰됨 = 버그** 라는 실리콘 검증의 판정 규칙을 코드로 적용한다.

## 1. 문제

ARM-M05 와 C-A03 에서 Apple M6 가 MP 와 SB 재배치를 실제로 보이는 것을 측정했다. 그런데 측정만으로는 "이 결과는 **원래 허용**되는가, 아니면 **칩 버그**인가" 를 판단할 수 없다. 판단 기준은 아키텍처의 **형식 메모리 모델** 이다. Arm 은 ARMv8 메모리 모델을 공리(axiomatic) 형태로 정의하고, 그 모델은 herd7 의 `aarch64.cat` 으로도 공개되어 있다. Intel/AMD 는 x86-TSO 로 정리된다. 이 레슨에서는 그 모델의 핵심을 Python 300줄 정도로 직접 짜서 "어떤 결과가 허용되는가" 를 손으로 계산한다. **왜 중요한가:** 실리콘 검증·SoC 아키텍처·커널 동기화 면접은 "MP 에 무슨 배리어가 최소로 필요한가", "x86 에서는 왜 안 보이나", "0번 관찰이면 안전한가" 를 묻는다. 정답은 모두 이 모델에서 나온다. 펌웨어 쪽에서도 같은 판단이 필요하다. Cortex-A + Cortex-M 메일박스, 듀얼 코어 MCU(STM32H7 의 M7↔M4), DMA 디스크립터 게시가 모두 MP 모양이다.

> 아이템 task 에는 "ARCH-A05 의 **M2** 실측 결과와 대조" 라고 적혀 있다. 이 맥은 **Apple M6** 이다(`sysctl -n machdep.cpu.brand_string` → `Apple M6`, perflevel 2/4/6). 그래서 M6 숫자로 대조한다. ARCH-A05 는 이 레슨과 동시에 쓰이고 있으므로 Part C 에는 지금 가진 숫자(ARM-M05 · C-A02 · C-A03, 가이드 §3·§4 작성 메모)를 넣었다. Part C 의 판정 출력에서 ARCH-A05 데이터가 필요한 줄은 **"ARCH-A05 측정값 참조"** 로 남아 있다. 그 뒤 대조 표에는 ARCH-A05 레슨의 실측값을 옮겨 적었다.

**리트머스 형식.** herd7 의 `.litmus` 와 비슷하게 열(column)을 `|` 로 나눈다.

```text
MP
{ x=0; y=0; }
 P0       | P1       ;
 W x 1    | R y r0   ;
 W y 1    | R x r1   ;
exists (1:r0=1 /\ 1:r1=0)
```

| 명령 | 뜻 | ARMv8 명령 | x86 |
|---|---|---|---|
| `W x 1` / `R x r0` | 일반 store / load | `str` / `ldr` | `mov` |
| `Wrel x 1` | release store | `stlr` | `mov` (이미 release) |
| `Racq x r0` | acquire load, **RCsc** | `ldar` | `mov` |
| `Rapc x r0` | acquire load, **RCpc** | `ldapr` (FEAT_LRCPC) | `mov` |
| `F` | full fence | `dmb ish` | `mfence` |
| `Fst` / `Fld` | store-store / load-any fence | `dmb ishst` / `dmb ishld` | (W→R 을 못 막음) |

`exists (...)` 는 "이상한(weird) 결과" 다. 이 결과가 모델에서 허용되는지가 판정의 핵심이다.

```svg
<svg viewBox="0 0 720 270" width="720" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="m7a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" fill="currentColor" font-size="14" font-weight="bold">MP 의 이상한 실행: r0=1 (flag 봄), r1=0 (data 못 봄)</text>
  <text x="90" y="52" fill="currentColor" font-size="13">P0 (writer)</text>
  <text x="470" y="52" fill="currentColor" font-size="13">P1 (reader)</text>
  <rect x="60" y="64" width="150" height="34" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="80" y="86" fill="currentColor" font-size="13">a: W x=1 (data)</text>
  <rect x="60" y="170" width="150" height="34" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="80" y="192" fill="currentColor" font-size="13">b: W y=1 (flag)</text>
  <rect x="440" y="64" width="150" height="34" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="458" y="86" fill="currentColor" font-size="13">c: R y → r0=1</text>
  <rect x="440" y="170" width="150" height="34" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="458" y="192" fill="currentColor" font-size="13">d: R x → r1=0</text>
  <line x1="135" y1="98" x2="135" y2="168" stroke="currentColor" stroke-width="2" marker-end="url(#m7a)"/>
  <text x="142" y="138" fill="currentColor" font-size="12">po</text>
  <line x1="515" y1="98" x2="515" y2="168" stroke="currentColor" stroke-width="2" marker-end="url(#m7a)"/>
  <text x="522" y="138" fill="currentColor" font-size="12">po</text>
  <line x1="210" y1="180" x2="438" y2="90" stroke="currentColor" stroke-width="2" marker-end="url(#m7a)"/>
  <text x="300" y="122" fill="currentColor" font-size="12">rf (c 가 b 를 읽음)</text>
  <line x1="440" y1="196" x2="212" y2="90" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4" marker-end="url(#m7a)"/>
  <text x="236" y="152" fill="currentColor" font-size="12">fr (d 는 init x=0 을 읽음, a 는 co 상 init 다음)</text>
  <rect x="20" y="220" width="680" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="237" fill="currentColor" font-size="12">SC: a -po-&gt; b -rf-&gt; c -po-&gt; d -fr-&gt; a 사이클 → 금지.  TSO: W→W·R→R po 가 순서 유지(ppo) → 같은 사이클 → 금지.</text>
  <text x="32" y="253" fill="currentColor" font-size="12">ARMv8: 일반 ldr/str 의 po 는 ob 에 없음 → 사이클 끊김 → 허용. b=stlr, c=ldar/ldapr 면 bob → 금지.</text>
</svg>
```

### Part A — 후보 실행 열거와 SC

1. `candidates(t)`: read 마다 같은 위치의 write(초깃값 포함) 하나를 고르고(rf), 위치마다 write 의 전체 순서를 고른다(co, init 이 항상 맨 앞). 둘의 곱집합이 후보 실행 전부다. MP/SB/LB 는 4개, IRIW 는 16개, 2+2W 는 4개 (CHECK).
2. `relations`: po, po-loc, rf, co 는 주어진다. **fr = rf⁻¹ ; co** 를 채운다. 각 관계는 다시 외부(e: 스레드가 다름)와 내부(i)로 나눈다.
3. `sc_axioms`: Lamport 의 SC 는 "po ∪ rf ∪ co ∪ fr 이 비순환" 이다. 이 공리로 걸러 MP={00,01,11}, SB={01,10,11}, LB={00,01,10}, IRIW 는 16개 중 15개(1010 만 금지), 2+2W 최종값={12,21,22} 가 나와야 한다. 덤으로 **모든 인터리빙을 직접 돌리는 운영(operational) SC** 와 23개 테스트 전부에서 같아야 한다.

### Part B — TSO 와 ARMv8, 그리고 "왜"

1. `tso_axioms`: 공리 둘. `uniproc` 는 po-loc ∪ rf ∪ co ∪ fr 비순환이다. `tso` 는 ppo ∪ **rfe** ∪ co ∪ fr ∪ mfence 비순환이고, 여기서 ppo = po − (W×R) 다. rfi(같은 스레드 안의 rf)를 빼는 것은 store forwarding 때문이다. 이 공리 모델은 store buffer 기계(주어진 `operational(t, True)`)와 23개 전부에서 같아야 한다.
2. `arm_bob` + `arm_axioms`: `internal` 공리는 po-loc ∪ rf ∪ co ∪ fr 비순환, `external` 공리는 obs ∪ bob 비순환이다. obs = rfe ∪ coe ∪ fre 다. bob 은 여섯 규칙으로 정의된다 (§5 표).
3. 23개 테스트 × 3개 모델의 이상 결과 허용 여부가 알려진 표와 같아야 한다. 그리고 `explain` 이 `SC forbids (sc): P0:Wx=1 -po-> P0:Wy=1 -rf-> P1:Ry=1 -po-> P1:Rx=0 -fr-> P0:Wx=1` 같은 한 줄을 정확히 내야 한다.

### Part C — Apple M6 실측과 대조

`classify(model_allows, runs)` 를 채운다. 다섯 가지로 나눈다: 데이터 없음 `no-data`, 허용이고 관찰됨 `seen`, 허용인데 관찰 0 `allowed-unseen`, 금지이고 관찰 0 `ok`, **금지인데 관찰됨 `VIOLATION`**. 이 분류를 다른 레슨에서 측정한 19행(`OBSERVED`)에 적용한다. ARMv8 모델 기준으로는 VIOLATION 이 0 이어야 한다. 같은 데이터를 TSO 모델로 판정하면 MP 계열 3종이 VIOLATION 이 되어야 한다. 이것은 M6 가 TSO 가 아니라는 증거다.

## 2. Problem (English)

> Write an exhaustive axiomatic litmus-test enumerator.
>
> **Input**: a litmus test with threads of `W x v`, `Wrel x v`, `R x r`, `Racq x r` (RCsc acquire, LDAR), `Rapc x r` (RCpc acquire, LDAPR), `F` (full fence), `Fst` (store-store fence), `Fld` (load-any fence), initial values, and an `exists` clause.
>
> **A.** Enumerate candidate executions: for every read choose an rf source among same-location writes (incl. the initial write); for every location choose a coherence order `co` (initial write first). Derive `fr = rf^-1 ; co`. Implement SC: `acyclic(po | rf | co | fr)`. Cross-check against an operational interleaving machine.
>
> **B.** Implement x86-TSO: `acyclic(po-loc | rf | co | fr)` and `acyclic(ppo | rfe | co | fr | mfence)` with `ppo = po \ (W x R)`; cross-check against a store-buffer machine. Implement simplified ARMv8: `internal = acyclic(po-loc | rf | co | fr)`, `external = acyclic(rfe | coe | fre | bob)` where bob covers DMB full / DMB LD / DMB ST, `[A|Q];po`, `po;[L]`, `[L];po;[A]`. Produce the allowed-outcome sets and the table MP/SB/LB/IRIW/2+2W (+ fenced/annotated variants) x SC/TSO/ARMv8, and print the cycle that forbids an outcome.
>
> **C.** Given observed weird-outcome counts from hardware runs, classify each row as seen / allowed-unseen / ok / VIOLATION / no-data.
>
> ```python
> def candidates(t) -> list            # [(rf: {read_id: write_id}, co: {loc: (init, w1, ...)})]
> def sc_axioms(t, R) -> list          # [(name, {(a, b): label})]  consistent iff every graph is acyclic
> def tso_axioms(t, R) -> list
> def arm_bob(t, R) -> set
> def arm_axioms(t, R) -> list
> def classify(model_allows: bool, runs) -> str
> ```
>
> Example: `MP` → SC `{r0r1 = 00, 01, 11}`, TSO same, ARMv8 all four. `SB` → TSO and ARMv8 allow `00`. `SB+rel+acq` (STLR;LDAR) → ARMv8 forbids, TSO allows (x86 `mov` gives no W→R order).
>
> Follow-ups: Why is plain IRIW allowed on ARMv8 although ARMv8 is multi-copy atomic? What is the minimal fix for SB on ARMv8 and on x86? Why does TSO exclude `rfi`? You ran LB 10⁸ times and saw 0 — is LB forbidden? What do `herd7` models leave out that real code relies on (dependencies)?

## 3. 시작 코드

`python3 -I` 로 실행한다(스크립트 폴더를 `sys.path` 에 넣지 않는 격리 모드). 표준 라이브러리만 쓰고 Python 3.9 에서 돈다. 리트머스 23개는 파일 안 `LITMUS_SRC` 에 있다. 파서·사이클 탐색·운영 모델·설명기·하네스는 주어진다. TODO 는 7곳이다: A1 `candidates`, A2 fr, A3 SC, B1 TSO, B2 bob, B3 ARMv8 공리, C1 `classify`.

<!-- code: starter.py -->

```bash
cd lessons/MEM/MEM-M07
python3 -I starter.py ; echo "exit=$?"        # 17/51 passed, exit=1
python3 -I solution.py ; echo "exit=$?"       # 51/51 passed, exit=0  (0.12 s)
python3 -I solution.py --table                # 23 tests x SC/TSO/ARMv8
python3 -I solution.py --sets MP              # 모델별 허용 결과 집합
python3 -I solution.py --explain SB           # 왜 금지/허용인가
python3 -I solution.py --compare              # M6 실측 대조 (Part C)
python3 -I solution.py --file example.litmus  # 직접 쓴 리트머스 (ISA2 예제)
```

시작 코드의 실제 출력(앞부분):

```text
== 파서
PASS  23 litmus tests parsed
PASS  IRIW has 4 threads, 6 memory events + 2 init writes
PASS  cond parsed: 2+2W exists (x=1 /\ y=1)
== Part A: 후보 실행과 SC
FAIL  candidate counts MP/SB/LB/IRIW/2+2W = 4/4/4/16/4
FAIL  candidate count SB+rfi-pos = 2^4 = 16 (each read: init or the one write)
FAIL  MP weird execution: fr = {P1:Rx=0 -> P0:Wx=1} (read init, co-later write)  (IndexError: list index out of range)
...
17/51 passed
```

(후보가 0개라서 "금지" 테스트 몇 개는 우연히 PASS 한다. A1 을 채우면 모델이 빈 목록이라 모든 결과가 허용되고, 그 테스트들이 FAIL 로 바뀐다. 그 다음 A3·B1–B3 를 채우면 다시 PASS 가 된다.)

## 4. 힌트

<details><summary>힌트 1 — 후보 실행은 "값" 이 아니라 "어느 write" 를 고른다</summary>

read 의 값은 고른 write 의 값으로 정해진다. `itertools.product(*[t.writes[r.loc] for r in t.reads])` 가 rf 의 모든 조합이다. co 는 위치마다 `(init,) + p for p in permutations(나머지)` 이고, 위치들끼리 다시 `product` 한다. 사이클 검사는 나중이다. 지금은 "물리적으로 말이 되는가" 를 따지지 말고 전부 만든다.
</details>

<details><summary>힌트 2 — fr 은 "내가 읽은 것보다 co 상 뒤에 오는 write"</summary>

`r` 이 `w` 를 읽었고 `co(w, w2)` 이면 `fr(r, w2)` 이다. init 을 읽은 read 는 그 위치의 모든 비-init write 로 fr 간선을 가진다. MP 의 이상 실행에서 fr 은 `{(P1:Rx, P0:Wx)}` 한 개뿐이다 (CHECK).
</details>

<details><summary>힌트 3 — 공리 = (이름, 라벨 붙은 간선 dict)</summary>

`lab(R, "po", "rf", "co", "fr")` 이 `{(a,b): "po", ...}` 를 만든다. SC 는 `[("sc", lab(R, "po", "rf", "co", "fr"))]` 한 줄이다. 공리를 리스트로 돌려주면 `consistent` 가 각각 `find_cycle` 하고, `explain` 이 어느 공리가 어떤 사이클로 깨졌는지 출력한다.
</details>

<details><summary>힌트 4 — TSO: 뺄 것 두 개, 더할 것 하나</summary>

po 에서 W→R 쌍을 뺀다 (store buffer 때문에 뒤의 load 가 먼저 메모리를 본다). rf 대신 rfe 만 넣는다 (자기 버퍼에서 읽은 값은 아직 전역에 보이지 않는다). po 쌍 사이에 `F` 가 있으면 mfence 간선을 더한다. coherence 는 따로 `uniproc` 공리로 지킨다.
</details>

<details><summary>힌트 5 — ARMv8 bob 여섯 규칙과 LDAR vs LDAPR</summary>

(a, b) ∈ po 에 대해 다음 중 하나면 bob 이다. ① 사이에 `F` ② a 가 R 이고 사이에 `Fld` ③ a, b 가 모두 W 이고 사이에 `Fst` ④ a 가 `Wrel` 이고 b 가 `Racq` ⑤ a 가 `Racq` 또는 `Rapc` ⑥ b 가 `Wrel`. ④ 에 `Rapc` 를 넣으면 안 된다. 그것이 바로 RCpc 다 (ARM-M05 실측 표: `stlr;ldapr` SB 13회 중 7회 관찰). obs 는 **외부** 간선만 쓴다 (rfe, coe, fre). 내부 순서는 `internal` 공리가 따로 맡는다.
</details>

## 5. 풀이

### Part A — 후보 실행, fr, SC

**후보 실행 = (rf, co).** 공리 모델은 기계를 흉내 내지 않는다. "어떤 그래프가 가능한가" 만 따진다. 이벤트는 프로그램에서 정해져 있고(위치별 init write 포함), 정해지지 않은 것은 두 가지뿐이다.

| 관계 | 뜻 | 정해지는 방법 |
|---|---|---|
| po (program order) | 같은 스레드 안의 순서 | 소스에서 고정 |
| rf (reads-from) | read 가 값을 가져온 write | **고른다** — 같은 위치의 write 중 하나 |
| co (coherence order) | 위치별 write 의 전체 순서 | **고른다** — init 이 맨 앞, 나머지 순열 |
| fr (from-reads) = rf⁻¹;co | read 가 "덮어써지기 전" 의 write → 그 뒤의 write | 유도 |
| po-loc | 같은 위치에 대한 po | 유도 |
| ·e / ·i | 외부(스레드 다름) / 내부 | 유도 (init 은 모든 스레드에 외부) |

MP 는 read 가 둘이고 각각 init 또는 그 위치의 write 하나를 고를 수 있다. 위치마다 비-init write 가 하나라 co 는 한 가지다. 그래서 후보는 2×2 = 4 개다. IRIW 는 read 4개 → 16개. 2+2W 는 read 가 없고 위치 2곳 × write 2개 → co 2×2 = 4개.

**SC = 사이클 없음.** SC(Lamport 1979)는 "모든 접근을 한 줄로 세울 수 있고 각 스레드의 순서가 그 줄에 지켜진다" 는 뜻이다. 한 줄로 세울 수 있다는 것은 po ∪ rf ∪ co ∪ fr 이 사이클이 없다는 것과 같다 (Shasha–Snir 의 "critical cycle" 관점, Herding cats 참고). fr 이 꼭 필요한 이유가 있다. "d 가 x=0 을 읽었다" 는 것은 d 가 a(x=1) 보다 **먼저** 줄에 섰다는 뜻이고, 그 정보를 그래프에 넣는 간선이 fr 이다.

실제 출력 (`python3 -I solution.py --sets MP` 등):

```text
MP: 4 candidate executions
  SC     3: 1:r0=0 1:r1=0 | 1:r0=0 1:r1=1 | 1:r0=1 1:r1=1
  TSO    3: 1:r0=0 1:r1=0 | 1:r0=0 1:r1=1 | 1:r0=1 1:r1=1
  ARMv8  4: 1:r0=0 1:r1=0 | 1:r0=0 1:r1=1 | 1:r0=1 1:r1=0 | 1:r0=1 1:r1=1
SB: 4 candidate executions
  SC     3: 0:r0=0 1:r1=1 | 0:r0=1 1:r1=0 | 0:r0=1 1:r1=1
  TSO    4: 0:r0=0 1:r1=0 | 0:r0=0 1:r1=1 | 0:r0=1 1:r1=0 | 0:r0=1 1:r1=1
  ARMv8  4: 0:r0=0 1:r1=0 | 0:r0=0 1:r1=1 | 0:r0=1 1:r1=0 | 0:r0=1 1:r1=1
2+2W: 4 candidate executions
  SC     3: x=1 y=2 | x=2 y=1 | x=2 y=2
  TSO    3: x=1 y=2 | x=2 y=1 | x=2 y=2
  ARMv8  4: x=1 y=1 | x=1 y=2 | x=2 y=1 | x=2 y=2
```

**두 가지 방법으로 같은 답.** `operational(t, False)` 는 모든 인터리빙을 DFS 로 돌리는 SC 기계다. 공리 SC 와 운영 SC 가 23개 테스트 전부에서 같은 집합을 낸다 (CHECK). 공리 모델이 맞다고 믿을 근거를 다른 방법으로 하나 더 얻은 셈이다. 이것은 MCPP-M13 의 SC 인터리빙 탐색기와 같은 기계다. 그러나 SC 기계로는 약한 결과가 **절대** 나오지 않는다. 그래서 Part B 가 필요하다.

### Part B — TSO 와 ARMv8

**x86-TSO.** Sewell et al.(CACM 2010)의 그림은 "코어마다 FIFO store buffer, 메모리는 하나" 다. 이 그림에서 두 가지가 나온다. ① store 가 버퍼에 있는 동안 뒤의 load 가 먼저 메모리를 읽는다 → W→R 재배치(SB). ② 자기 버퍼의 값은 자기만 먼저 본다 → store forwarding. 공리로 옮기면 `ppo = po − (W×R)` 이고, 전역 순서 그래프에 rf 대신 **rfe** 를 넣는다. `mfence`(여기서 `F`)는 버퍼를 비우므로 W→R 순서를 되돌린다.

```svg
<svg viewBox="0 0 720 266" width="720" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="m7b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" fill="currentColor" font-size="14" font-weight="bold">TSO 기계로 본 SB: 둘 다 0 을 읽는 실행 (operational(t, True))</text>
  <rect x="40" y="40" width="200" height="60" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="56" y="62" fill="currentColor" font-size="13">P0: W x 1 ; R y r0</text>
  <text x="56" y="84" fill="currentColor" font-size="12">① x=1 을 버퍼에 넣음</text>
  <rect x="480" y="40" width="200" height="60" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="496" y="62" fill="currentColor" font-size="13">P1: W y 1 ; R x r1</text>
  <text x="496" y="84" fill="currentColor" font-size="12">② y=1 을 버퍼에 넣음</text>
  <rect x="60" y="118" width="160" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="72" y="138" fill="currentColor" font-size="12">store buffer [x=1]</text>
  <rect x="500" y="118" width="160" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="512" y="138" fill="currentColor" font-size="12">store buffer [y=1]</text>
  <rect x="230" y="186" width="260" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="250" y="211" fill="currentColor" font-size="13">memory: x=0, y=0 (아직 drain 전)</text>
  <line x1="140" y1="148" x2="300" y2="184" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5 4" marker-end="url(#m7b)"/>
  <line x1="580" y1="148" x2="420" y2="184" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5 4" marker-end="url(#m7b)"/>
  <text x="250" y="170" fill="currentColor" font-size="12">③ R y → 0</text>
  <text x="400" y="170" fill="currentColor" font-size="12">④ R x → 0</text>
  <text x="20" y="242" fill="currentColor" font-size="12">⑤ 그 다음 drain. 결과 r0=0, r1=0 = SB 의 이상 결과.</text>
  <text x="20" y="258" fill="currentColor" font-size="12">F(mfence) = "버퍼가 빌 때까지 다음 명령 금지" → ③④ 앞에 drain 이 강제됨 → 금지.</text>
</svg>
```

공리 TSO 와 store buffer 기계도 23개 전부에서 같은 집합을 낸다 (CHECK). Owens–Sarkar–Sewell 은 두 모델이 동치임을 증명했다(TPHOLs 2009). 여기서는 23개 테스트로 그 결과를 확인만 한 것이다. 연습 3 에서 rfi 를 넣은 돌연변이 TSO 를 만들면 `SB+rfi-pos` 에서만 기계와 어긋난다. 이 테스트는 Intel SDM 의 "intra-processor forwarding is allowed" 예제와 같은 모양이다.

**ARMv8 (단순화한 aarch64.cat).** Pulte et al.(POPL 2018)은 ARMv8 을 **other-multi-copy-atomic** 으로 정리했다 (Arm 이 2017년경 아키텍처를 그렇게 바꿈 — 연도는 논문 기준, 확인 필요). 그래서 공리가 두 개로 짧다.

- `internal`: po-loc ∪ rf ∪ co ∪ fr 비순환. 위치 하나만 보면 SC 라는 뜻이다(coherence). CoRR 같은 테스트를 이 공리가 막는다.
- `external`: ob = obs ∪ dob ∪ aob ∪ bob 이 비순환. obs = rfe ∪ coe ∪ fre 는 "다른 코어가 관찰한 순서" 다. 이 레슨은 **dob(주소·데이터·제어 의존성)와 aob(원자 RMW)를 빼고** bob 만 구현한다. 의존성이 없는 MP/SB/LB/IRIW/2+2W 에서는 빠진 것이 결과를 바꾸지 않는다. 의존성이 들어간 MP+addr 같은 테스트는 이 열거기가 **틀리게** "허용" 이라고 답한다 (ARM-M05 의 판정기는 주소 의존성을 다룬다).

| bob 규칙 (aarch64.cat 표기) | 이 레슨 | 명령 | 막는 리트머스 |
|---|---|---|---|
| `po; [dmb.full]; po` | 사이에 `F` | `dmb ish` | 전부 (SB 포함) |
| `[R]; po; [dmb.ld]; po` | a=R, 사이에 `Fld` | `dmb ishld` | MP reader 쪽, LB |
| `[W]; po; [dmb.st]; po; [W]` | a,b=W, 사이에 `Fst` | `dmb ishst` | MP writer 쪽, 2+2W (SB 는 못 막음) |
| `[A\|Q]; po` | a=`Racq`/`Rapc` | `ldar`/`ldapr` | MP reader, LB, IRIW |
| `po; [L]` | b=`Wrel` | `stlr` | MP writer, LB, 2+2W |
| `[L]; po; [A]` | a=`Wrel`, b=`Racq` (**`Rapc` 제외**) | `stlr` → `ldar` | SB (RCsc) |

(실제 `aarch64.cat` 에는 `po;[L];coi`, `lob`, 원자 연산 규칙 등이 더 있다. 판(version)마다 정의 모양이 다르므로 "단순화" 로만 읽는다 — 확인 필요.)

**결과 표 (실제 출력, `python3 -I solution.py --table`):**

```text
test           SC     TSO    ARMv8    exists
MP             forbid forbid allow    1:r0=1 1:r1=0
MP+F+po        forbid forbid allow    1:r0=1 1:r1=0
MP+F+F         forbid forbid forbid   1:r0=1 1:r1=0
MP+Fst+Fld     forbid forbid forbid   1:r0=1 1:r1=0
MP+rel+po      forbid forbid allow    1:r0=1 1:r1=0
MP+rel+acq     forbid forbid forbid   1:r0=1 1:r1=0
MP+rel+apc     forbid forbid forbid   1:r0=1 1:r1=0
SB             forbid allow  allow    0:r0=0 1:r1=0
SB+F+F         forbid forbid forbid   0:r0=0 1:r1=0
SB+Fst+Fst     forbid allow  allow    0:r0=0 1:r1=0
SB+rel+acq     forbid allow  forbid   0:r0=0 1:r1=0
SB+rel+apc     forbid allow  allow    0:r0=0 1:r1=0
SB+rfi-pos     forbid allow  allow    0:r0=1 0:r1=0 1:r2=1 1:r3=0
LB             forbid forbid allow    0:r0=1 1:r1=1
LB+F+F         forbid forbid forbid   0:r0=1 1:r1=1
LB+rel+rel     forbid forbid forbid   0:r0=1 1:r1=1
IRIW           forbid forbid allow    2:r0=1 2:r1=0 3:r2=1 3:r3=0
IRIW+F+F       forbid forbid forbid   2:r0=1 2:r1=0 3:r2=1 3:r3=0
IRIW+acq+acq   forbid forbid forbid   2:r0=1 2:r1=0 3:r2=1 3:r3=0
2+2W           forbid forbid allow    x=1 y=1
2+2W+Fst+Fst   forbid forbid forbid   x=1 y=1
2+2W+rel+rel   forbid forbid forbid   x=1 y=1
CoRR           forbid forbid forbid   1:r0=1 1:r1=0
```

이 표가 Herding cats / Pulte 2018 / x86-TSO 논문의 알려진 결과와 같다는 것이 `EXPECTED_WEIRD` CHECK 23개다. 읽는 법:

- **SB 만 TSO 에서 허용, MP·LB·IRIW·2+2W 는 TSO 에서 금지.** "ARM 에서는 보이고 x86 에서는 안 보이는 재배치" 를 물으면 MP·LB·2+2W·IRIW 라고 답하면 된다. "x86 에서도 보이는 것" 은 SB 하나다 (가이드 §7 면접 체크).
- **IRIW 에 대해 흔히 하는 오해.** "ARMv8 은 multi-copy atomic 이라 IRIW 금지" 는 **reader 의 두 load 사이에 순서가 있을 때만** 맞다 (IRIW+F+F, IRIW+acq+acq). 일반 `ldr` 두 개는 서로 재배치되므로 plain IRIW 는 MCA 와 무관하게 허용이다. MCA 가 효과를 보이는 곳은 "순서를 걸었는데도 두 reader 가 다른 순서를 보는가" 다. POWER 는 IRIW+lwsync 에서 허용이고, ARMv8 은 IRIW+dmb 에서 금지다 (POWER 쪽은 Sarkar et al. PLDI 2011 기준).
- **모델 강도 SC ⊆ TSO ⊆ ARMv8 이 깨지는 테스트가 딱 하나 있다: `SB+rel+acq`.** ARM 은 `stlr; ldar` 사이에 순서를 보장하고(RCsc), x86 의 release/acquire 는 그냥 `mov` 라 W→R 순서가 없다. 그래서 같은 "rel/acq" 표기라도 ISA 마다 실제 명령이 다르다 (CHECK: 예외 목록 == `["SB+rel+acq"]`). 반대로 C11 seq_cst 는 x86 에서 store 뒤에 `xchg`/`mfence` 를 넣어 SB 를 막는다 (연습 5).

**"왜" 를 한 줄로 (실제 출력 `--explain`):**

```text
SC forbids (sc): P0:Wx=1 -po-> P0:Wy=1 -rf-> P1:Ry=1 -po-> P1:Rx=0 -fr-> P0:Wx=1
TSO forbids (tso): P0:Wx=1 -ppo-> P0:Wy=1 -rfe-> P1:Ry=1 -ppo-> P1:Rx=0 -fr-> P0:Wx=1
ARMv8 allows: SC cycle P0:Wx=1 -po-> P0:Wy=1 -rf-> P1:Ry=1 -po-> P1:Rx=0 -fr-> P0:Wx=1 is broken because ARMv8 does not order: P0:Wx=1 -po-> P0:Wy=1; P1:Ry=1 -po-> P1:Rx=0
```

```text
TSO allows: SC cycle P0:Wx=1 -po-> P0:Ry=0 -fr-> P1:Wy=1 -po-> P1:Rx=0 -fr-> P0:Wx=1 is broken because TSO does not order: P0:Wx=1 -po-> P0:Ry=0; P1:Wy=1 -po-> P1:Rx=0
ARMv8 forbids (external): P0:Wx=1 -bob-> P0:Wrel y=1 -rfe-> P1:Racq y=1 -bob-> P1:Rx=0 -fre-> P0:Wx=1
SC forbids (sc): P0:Rx=1 -po-> P0:Wy=1 -rf-> P1:Ry=1 -po-> P1:Wx=1 -rf-> P0:Rx=1
ARMv8 forbids (external): P0:Wx=1 -rfe-> P2:Rx=1 -bob-> P2:Ry=0 -fre-> P1:Wy=1 -rfe-> P3:Ry=1 -bob-> P3:Rx=0 -fre-> P0:Wx=1
```

(위에서부터 SB/TSO, MP+rel+acq/ARMv8, LB/SC, IRIW+F+F/ARMv8.) 모든 리트머스는 "SC 에서 사이클 하나" 로 만들어진다. 모델의 차이는 그 사이클의 **어느 po 간선을 순서로 인정하느냐** 뿐이다. 배리어를 고르는 일은 "끊긴 po 간선마다 그 간선을 ob 에 다시 넣는 가장 싼 bob 규칙을 찾는 것" 이다. 연습 4 가 이것을 전수 탐색으로 한다.

### Part C — 모델 예측 vs Apple M6 실측

`OBSERVED` 의 숫자는 이 레슨에서 다시 잰 것이 아니다. 이미 쓰인 레슨에서 측정한 값을 옮겼다: ARM-M05 의 `demo_litmus_asm` (MP 13조합 × 5회 × 2천만 회, SB 5조합 × 13회), C-A02 의 `demo_litmus` (MP 3회), C-A03 의 `demo_sb_litmus` (SB 500만 회 × 8회). 그 레슨들은 다른 벤치마크와 동시에 돌았으므로(가이드 §3·§4 작성 메모의 동시 실행 load 3–11) 횟수는 **0 이냐 아니냐** 만 의미가 있다. 이 레슨 자체는 벤치마크가 없다 (열거 0.12 s, 작성 시 `uptime` load 6.5). `python3 -I solution.py --compare` 실제 출력 (ARMv8 부분):

```text
-- model ARMv8
MP          allow  seen            ARM-M05 plain+plain               4306529,3667459,6307666,6631989,6250475
MP          allow  seen            C-A02 demo_litmus mode0           3,35,5911
MP+F+po     allow  seen            ARM-M05 dmb ish + plain           3560,309,583,450,940
MP+rel+po   allow  seen            ARM-M05 stlr + plain              28,849,695,110,1004
MP+F+F      forbid ok              ARM-M05 dmb ish + dmb ish         0,0,0,0,0
MP+Fst+Fld  forbid ok              ARM-M05 dmb ishst + dmb ishld     0,0,0,0,0
MP+rel+acq  forbid ok              ARM-M05 stlr + ldar               0,0,0,0,0
MP+rel+apc  forbid ok              ARM-M05 stlr + ldapr              0,0,0,0,0
SB          allow  seen            ARM-M05 str;ldr                   646,1506,337,1717,1,0,1,0,37,1,1591,1,63
SB          allow  seen            C-A03 relaxed                     91,87,0,5436,0,137,0,315
SB+Fst+Fst  allow  seen            ARM-M05 str;dmb ishst;ldr         2014,789,651,487,58,363,762,465,3034,11,596,628,680
SB+rel+apc  allow  seen            ARM-M05 stlr;ldapr                0,0,0,0,13,1,106,112,92,0,751,24,0
SB+rel+apc  allow  seen            C-A03 release/acquire             0,18,0,0,0,0,0,0
SB+F+F      forbid ok              ARM-M05 str;dmb ish;ldr           0,0,0,0,0,0,0,0,0,0,0,0,0
SB+rel+acq  forbid ok              ARM-M05 stlr;ldar                 0,0,0,0,0,0,0,0,0,0,0,0,0
SB+rel+acq  forbid ok              C-A03 seq_cst                     0,0,0,0,0,0,0,0
LB          allow  no-data         ARCH-A05 측정값 참조                   -
IRIW        allow  no-data         ARCH-A05 측정값 참조 (선택)              -
2+2W        allow  no-data         ARCH-A05 측정값 참조 (선택)              -
```

같은 데이터를 **TSO 모델** 로 판정하면 MP, MP+F+po, MP+rel+po 세 행이 `VIOLATION` 이 된다. SB+rel+acq 두 행은 `allowed-unseen` 이 된다. 판정이 말하는 것은 두 가지다. "M6 는 TSO 칩이 아니다". 그리고 "TSO 기준으로는 허용인 SB+rel+acq 를 M6 는 보이지 않는다 — `ldar` 가 RCsc 이기 때문이다".

```svg
<svg viewBox="0 0 720 230" width="720" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" fill="currentColor" font-size="14" font-weight="bold">판정 규칙: 모델(허용/금지) × 하드웨어(관찰/0)</text>
  <rect x="200" y="40" width="240" height="30" class="f-mut" stroke="currentColor"/>
  <text x="270" y="60" fill="currentColor" font-size="13">한 번이라도 관찰</text>
  <rect x="440" y="40" width="240" height="30" class="f-mut" stroke="currentColor"/>
  <text x="510" y="60" fill="currentColor" font-size="13">N회 모두 0</text>
  <rect x="40" y="70" width="160" height="70" class="f-mut" stroke="currentColor"/>
  <text x="56" y="110" fill="currentColor" font-size="13">모델: 허용</text>
  <rect x="40" y="140" width="160" height="70" class="f-mut" stroke="currentColor"/>
  <text x="56" y="180" fill="currentColor" font-size="13">모델: 금지</text>
  <rect x="200" y="70" width="240" height="70" class="f-ok" stroke="currentColor"/>
  <text x="214" y="98" fill="currentColor" font-size="13">seen — 허용의 실례</text>
  <text x="214" y="120" fill="currentColor" font-size="12">M6: MP, SB, SB+rel+apc ...</text>
  <rect x="440" y="70" width="240" height="70" class="f-acc" stroke="currentColor"/>
  <text x="454" y="98" fill="currentColor" font-size="13">allowed-unseen — 버그 아님</text>
  <text x="454" y="120" fill="currentColor" font-size="12">"이 칩이 더 강하다" 는 신호일 뿐</text>
  <rect x="200" y="140" width="240" height="70" class="f-warn" stroke="currentColor"/>
  <text x="214" y="168" fill="currentColor" font-size="13">VIOLATION — 칩 / 모델 /</text>
  <text x="214" y="190" fill="currentColor" font-size="12">하네스 중 하나가 틀렸다</text>
  <rect x="440" y="140" width="240" height="70" class="f-ok" stroke="currentColor"/>
  <text x="454" y="168" fill="currentColor" font-size="13">ok — 증명이 아니라 일치</text>
  <text x="454" y="190" fill="currentColor" font-size="12">증명은 아키텍처 규칙이 한다</text>
</svg>
```

**허용 ≠ 관찰.** 허용이라는 것은 "아키텍처가 이 결과를 내도 칩 버그가 아니다" 라는 뜻이지, "실제로 나온다" 는 뜻이 아니다. 같은 M6 에서도 plain SB 는 13회 중 2회가 0 이었다. `stlr;ldapr` 는 13회 중 6회가 0 이었다 (ARM-M05 표의 13개 숫자 기준. 그 레슨 본문의 "13회 중 9회" 와 다르다 — 표를 따랐다). LB 는 ARMv8 에서 허용이지만 실제 ARM 코어에서 보기 어렵다고 알려져 있다. 이 M6 에서도 ARCH-A05 합계 1600만 번 중 0 이었다. 그래서 위험은 이렇다. "M6 에서 테스트해 보니 됐다" 는 코드가 **아키텍처가 허용하는 다른 칩**(Cortex-A, Neoverse, 다음 세대 Apple 코어)에서 깨질 수 있다. 정확성은 모델로 판단하고, 하드웨어 리트머스는 두 용도로만 쓴다. ① 모델의 "허용" 이 실제로 쓰인다는 확인. ② **금지 결과가 한 번이라도 나오는지** 감시. ②가 실리콘 검증에서 하는 일이다. diy7 로 수천 개 리트머스를 생성하고, litmus7 로 새 코어에서 돌리고, herd7 로 금지/허용 오라클을 만든다. 그리고 VIOLATION 이 0 인지 본다. Herding cats 논문은 이렇게 해서 Cortex-A9 계열 칩에서 CoRR(같은 주소 load-load 순서) 위반을 보고했다 (Arm 정오표 번호는 확인 필요).

**ARCH-A05 와 맞춰 본 표** (ARCH-A05 세트 1, 100만 회당 이상 결과 수 3회. load 7.5–8.8 이라 **0 vs 0 아님** 만 비교한다):

| 리트머스 | 모델 SC / TSO / ARMv8 | M6 arm64 실측 (지금 가진 숫자) | M6 arm64 (ARCH-A05) | x86-64 바이너리 on Rosetta 2 (ARCH-A05) |
|---|---|---|---|---|
| SB (relaxed) | 금지 / **허용** / 허용 | 관찰 (ARM-M05 11/13회, C-A03 5/8회) | 345663 · 345000 · 346898 | 770911 · 765504 · 785648 (TSO 예측: 관찰 ✓) |
| SB seq_cst | 금지 / 금지 / 금지 | 0 (ARM-M05 `stlr;ldar` ×13, C-A03 ×8) | 0 ×3 (fence 변형도 0 ×3) | 0 ×3 (fence 변형도 0 ×3) (예측: 0 ✓) |
| MP (relaxed) | 금지 / 금지 / **허용** | 관찰 (ARM-M05 5/5회, 최대 6.6M/20M) | 41736 · 32035 · 28692 | **0 · 0 · 0** (예측: 0 ✓) |
| MP rel/acq | 금지 / 금지 / 금지 | 0 (ARM-M05 ×5) | 0 ×3 | 0 ×3 (예측: 0 ✓) |
| LB (relaxed) | 금지 / 금지 / **허용** | 데이터 없음 | 0 · 0 · 0 (세트 전체 합계 1600만 번 중 0 — 허용 ≠ 관찰) | 0 · 0 · 0 (예측: 0 ✓) |
| IRIW (relaxed) | 금지 / 금지 / 허용 | 데이터 없음 | 870 · 1156 · 2624 | **0 · 0 · 0** (예측: 0 ✓) |
| IRIW acq/rel · fence | 금지 / 금지 / 금지 | 데이터 없음 | 0 ×6 | 0 ×6 (예측: 0 ✓) |
| 2+2W (relaxed) | 금지 / 금지 / 허용 | 데이터 없음 | 103879 · 76299 · 64623 | **0 · 0 · 0** (예측: 0 ✓) |

Rosetta 2 열에 대해: Rosetta 는 번역한 코드를 Apple 칩의 하드웨어 TSO 모드로 돌린다고 알려져 있다 (메커니즘은 확인 필요). 이 열에서 MP 가 0 이고 SB 가 관찰되면 "TSO 예측과 일치" 다. Rosetta 에서 MP 가 한 번이라도 나오면 위 판정표로 VIOLATION 이다.

**펌웨어 연결.** 단일 코어 Cortex-M 은 자기 자신이 보기에 일반 메모리 접근 순서가 지켜진다. 이 레슨의 모델이 다루는 것은 **두 관찰자 이상** 인 경우다. 듀얼 코어 MCU(STM32H7 의 M7↔M4 공유 SRAM, RP2040 의 두 M0+), Cortex-A ↔ Cortex-M 메일박스, 그리고 CPU ↔ DMA 엔진(DMA 는 캐시를 거치지 않는 또 하나의 관찰자 — ARCH-M12) 이 그 예다. 메일박스의 "payload → doorbell" 은 정확히 MP 이고, 최소 수정은 연습 4 의 답과 같다: writer 는 `stlr`(또는 `dmb ishst`), reader 는 `ldapr`/`ldar`(또는 `dmb ishld`). Cortex-M 처럼 `stlr` 이 없는 v7-M 에서는 `dmb` 다. 주변장치 레지스터(Device 메모리)와 캐시 유지보수는 이 모델 밖이다 (ARM-M05 Part C, ARCH-M12).

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"      # 51/51 passed, exit=0
python3 -I -W error -X dev solution.py       # 경고 없음
```

## 7. 자주 하는 실수

**1. fr 을 빼고 SC 를 검사한다.** `po ∪ rf ∪ co` 만 쓰면 MP 의 이상 실행에서 사이클이 생기지 않는다 ("d 가 옛 값을 읽었다" 는 정보가 그래프에 없다). 그러면 SC 가 MP·SB·LB 를 전부 허용해 버린다.

```python
R["fr"] = {(r, w2) for r, w in rf_map.items() for (w1, w2) in R["co"] if w1 == w}   # rf⁻¹ ; co
```

**2. ARMv8 external 공리에 rf/co/fr 전체를 넣는다.** obs 는 **외부** 간선(rfe, coe, fre)이다. 내부 rf(rfi)를 넣으면 store forwarding 이 막혀 SB+rfi-pos 가 금지로 바뀐다. 이것은 연습 3 의 TSO 돌연변이와 같은 버그다. 내부 순서는 `internal` 공리가 따로 지킨다.

**3. `Rapc` 를 RCsc 로 취급한다.** `[L];po;[A]` 에 LDAPR 을 넣으면 SB+rel+apc 가 금지로 나온다. 그런데 ARM-M05·C-A03 에서 M6 는 그 결과를 실제로 보였다 (`stlr;ldapr` 13회 중 7회, C-A03 rel/acq 18회). 모델이 금지라고 하는데 칩이 보였다면 VIOLATION 이 되어야 하므로, 모델을 이렇게 잘못 짜면 정상 칩을 버그로 판정하게 된다. 판정 오라클이 틀리면 검증 전체가 틀린다.

```python
or (A.ann == "rel" and B.ann == "acq")     # STLR → LDAR 만. LDAPR(apc) 은 제외
```

**4. "dmb ishst 면 SB 도 막힌다".** `Fst` 는 W→W 만 묶는다. SB 의 끊긴 간선은 W→R 이다. 표에서 SB+Fst+Fst 는 TSO·ARMv8 모두 허용이고, M6 에서도 13회 전부 관찰됐다(11–3034회).

**5. "IRIW 는 ARMv8 에서 금지" 라고 외운다.** plain IRIW 는 허용이다 (load→load 재배치). 금지는 reader 쪽에 순서를 넣었을 때다 (Part B). 면접에서 MCA 를 설명하려면 반드시 "IRIW **+ 배리어**" 로 말한다.

**6. "0번 관찰 = 금지" 로 판정한다.** `classify(True, [0,0,0])` 은 `ok` 가 아니라 `allowed-unseen` 이다. LB 가 한 번도 안 나와도 LB 를 허용하는 코드는 아키텍처상 틀린 코드다.

**7. 모델 이름만 보고 ISA 사이에 명령을 옮긴다.** "release/acquire 로 SB 가 막힌다" 는 ARM(`stlr;ldar`)에서만 맞다. x86 에서는 틀리고, C11 에서도 틀리다 (C11 rel/acq SB 는 허용 — 연습 5). C11 seq_cst 가 막아 주는 것이고, 그 매핑이 ARM 에서 마침 `stlr;ldar` 인 것이다.

## 8. 면접 Q&A

<details><summary>Q1. 축(axiomatic) 메모리 모델이란? SC 를 한 줄로 정의해 보라.</summary>

실행을 이벤트와 관계(po, rf, co, fr)의 그래프로 본다. 모델은 "어떤 관계 합이 비순환이어야 한다" 는 조건 몇 개로 정의된다. SC 는 `acyclic(po ∪ rf ∪ co ∪ fr)` 이다. 이 레슨의 열거기는 rf·co 를 전부 골라 보고 공리를 통과한 실행의 결과만 모은다. 꼬리: "운영(operational) 모델과 차이는?" → 운영 모델은 기계(버퍼·큐)를 돌려서 결과를 만든다. 공리 모델은 그래프 조건만 본다. TSO 는 둘이 동치로 증명되어 있고(Owens et al. 2009), 이 레슨에서도 23개 테스트에서 일치했다. ARMv8 도 Pulte 2018 이 Flat 운영 모델과의 동치를 다룬다 (논문 기준).
</details>

<details><summary>Q2. ARM 에서는 보이고 x86 에서는 안 보이는 재배치는? x86 에서도 보이는 것은?</summary>

ARM 에서만: MP(W→W 또는 R→R), LB(R→W), 2+2W(W→W), 그리고 IRIW·WRC(R→R). x86 에서도: SB(W→R, store buffer) 하나뿐이다. x86-TSO 의 ppo 가 `po − (W×R)` 이기 때문이다. 꼬리: "x86 에서 SB 를 막으려면?" → `mfence` 또는 locked 명령(`xchg`). 그래서 C11 seq_cst store 가 x86 에서 `xchg`(또는 `mov`+`mfence`)로 번역된다. release/acquire 로는 x86 에서도 SB 가 막히지 않는다.
</details>

<details><summary>Q3. MP 를 ARMv8 에서 고치는 최소 방법은? 왜 그것이 최소인가?</summary>

writer 의 flag store 를 `stlr`(po;[L])로, reader 의 flag load 를 `ldapr` 또는 `ldar`([A|Q];po)로 바꾼다. 비용 2 이고, 연습 4 의 전수 탐색이 찾은 최소 수정이 정확히 이 두 개다. SC 사이클에서 끊긴 po 간선이 두 개(W x→W y, R y→R x)라서 각각 하나씩 bob 규칙이 필요하다. 꼬리: "`dmb ishst` + `dmb ishld` 는?" → 그것도 금지시키지만(MP+Fst+Fld) 배리어 두 개라 더 비싼 걸로 셌다. 실제 비용은 코어마다 측정해야 한다 (LLAT-B06 / ARM-M05).
</details>

<details><summary>Q4. 왜 SB 에는 acquire/release 가 부족하고 seq_cst 가 필요한가? ARM 명령으로 설명하라.</summary>

SB 에서 끊긴 간선은 각 스레드의 W→R 이다. ARMv8 에서 W→R 을 ob 에 넣는 규칙은 `dmb` full 이나 `[L];po;[A]`(STLR → LDAR, RCsc) 뿐이다. C11 acquire 는 Apple clang 에서 `ldapr`(RCpc)로 나오므로 이 규칙에 해당하지 않는다. 그래서 허용이고, C-A03 에서 실제로 18회 관찰됐다. seq_cst load 는 `ldar` 라 금지가 된다. 연습 4 의 최소 수정(비용 4)이 바로 "양쪽 다 `stlr` + `ldar`" = C11 seq_cst 매핑이다. 꼬리: "Peterson/Dekker 가 seq_cst 를 요구하는 이유" → 내 깃발 store 뒤에 상대 깃발 load 를 하는 SB 모양이기 때문이다.
</details>

<details><summary>Q5. ARMv8 은 multi-copy atomic 인데 왜 plain IRIW 가 허용되나? MCA 는 어디서 보이나?</summary>

plain IRIW 의 이상 결과는 reader 의 두 `ldr` 이 서로 재배치되기만 해도 나온다. 이것은 MCA 와 관계없다. MCA 의 의미는 "한 store 가 다른 코어 하나에게 보이면 모든 코어에게 보인다" 다. 그래서 reader 쪽 순서를 강제했을 때(IRIW+dmb, IRIW+acq) 두 reader 가 서로 다른 store 순서를 볼 수 없다. 이 열거기의 obs 에 rfe 가 "전역 사건" 으로 들어가는 것이 MCA 를 표현한 부분이다. 꼬리: "WRC 는?" → WRC+rel+acq 금지 (연습 1). POWER 는 non-MCA 라 IRIW+lwsync 가 허용이다 (논문 기준).
</details>

<details><summary>Q6. 리트머스를 1억 번 돌렸더니 LB 가 0번이었다. LB 를 허용하는 코드를 그대로 둬도 되나?</summary>

안 된다. ARMv8 모델은 LB 를 허용한다. 0번은 "이 칩, 이 부하, 이 타이밍에서 안 나왔다" 일 뿐이다. 판정표로는 `allowed-unseen` 이다. 같은 M6 에서도 plain SB 는 13회 중 2회 0 이었다. 다른 코어나 다음 세대에서는 나올 수 있다. 실무 처리: 코드는 모델 기준으로 고치고(LB 최소 수정 = 한쪽마다 `ldapr`/`ldar`/`stlr` 중 하나), 하드웨어 리트머스는 금지 결과 감시용으로 남긴다. 꼬리: "반대로 금지인데 한 번 나왔다면?" → VIOLATION 이다. 하네스 버그(변수 재사용, 컴파일러 재배치 — asm 확인), 모델/매핑 오류, 칩 버그 순서로 의심한다. 칩 버그면 정오표(errata)와 우회 배리어로 이어진다.
</details>

<details><summary>Q7. 이 열거기가 빠뜨린 것은? 실제 코드에서 그것이 왜 중요한가?</summary>

① 주소·데이터·제어 의존성(dob). Linux RCU 의 `rcu_dereference` 와 C11 `memory_order_consume` 이 기대는 "포인터 load → 그 포인터로 load" 순서다. 이 열거기는 MP+addr 를 틀리게 허용한다. ② RMW/exclusive 원자성(aob, `rmw ∩ (fre;coe) = ∅`). ③ Device 메모리, DSB/ISB, 캐시 유지보수, DMA. ④ mixed-size 접근. ⑤ C11 언어 모델 자체(데이터 레이스 = UB, out-of-thin-air) — MCPP-M13. ⑥ RVWMO — RISCV-A04. 꼬리: "실제 도구는?" → herd7(모델 시뮬레이터, `aarch64.cat`/`x86tso.cat`), litmus7(하드웨어 실행), diy7(테스트 생성), GenMC/CDSChecker(C11 모델 검사).
</details>

## 9. 추가 연습

<details><summary>연습 1 — WRC 3종으로 multi-copy atomicity 확인</summary>

WRC: P0 `W x 1` | P1 `R x r0; W y 1` | P2 `R y r1; R x r2`, exists (1:r0=1 ∧ 2:r1=1 ∧ 2:r2=0). plain 이면 ARMv8 허용이다. P2 만 `Racq` 를 쓰면 P1 의 R→W 가 순서 없으니 여전히 허용이다. P1 `Wrel y` + P2 `Racq y` 면 금지다. 사이클은 `P0:Wx=1 -rfe-> P1:Rx=1 -bob-> P1:Wrel y=1 -rfe-> P2:Racq y=1 -bob-> P2:Rx=0 -fre-> P0:Wx=1`. MCA 는 "P1 이 본 x 를 P1 의 release 를 받은 P2 도 본다" 는 형태로 나타난다. SC/TSO 는 셋 다 금지다.
</details>

<details><summary>연습 2 — coherence 테스트와 internal 공리를 뺀 돌연변이</summary>

CoWW(`W x 1; W x 2` → x=1?), CoRW1(`R x; W x 1` → r0=1?), CoWR, CoRW2 는 세 모델 모두 금지다. 같은 위치에 대해서는 모든 모델이 SC 다. `arm_external_only`(internal 공리 삭제)로 돌리면 CoRR 과 CoWW 가 허용으로 바뀐다. 같은 스레드의 같은 위치 순서는 obs(외부만) 어디에도 없기 때문이다. MP+rel+acq 는 여전히 금지다. 정리하면 internal 은 "위치 하나의 SC", external 은 "위치 사이의 순서" 를 맡는다. 실리콘 검증의 CoRR 이 이 공리를 시험한다 (Cortex-A9 계열 CoRR 위반 사례, §5 Part C).
</details>

<details><summary>연습 3 — TSO 에 rfi 를 넣으면 무엇이 틀리나</summary>

`tso_with_rfi` 는 ghb 에 rfe 대신 rf 를 넣은 돌연변이다. 23개 중 `SB+rfi-pos` 하나에서만 진짜 TSO 와 다르다(금지로 바뀜). store buffer 기계는 그 결과를 허용한다 (자기 버퍼에서 x=1 을 forwarding 받고, 메모리에서 옛 y 를 읽는다). Intel SDM "Memory Ordering" 절의 intra-processor forwarding 예제가 같은 모양이다 (장 번호는 판마다 8/10 — 확인 필요). 교훈: 자기 store 를 "먼저 본다" 는 것과 "전역에 보였다" 는 것은 다르다.
</details>

<details><summary>연습 4 — 최소 비용 수정 전수 탐색 (rel/acq/apc = 1, Fst/Fld = 2, F = 3)</summary>

`python3 -I practice.py --fixes` 실제 출력:

```text
MP    ARMv8 cost 2: P0:Wy->Wrel + P1:Ry->Racq | P0:Wy->Wrel + P1:Ry->Rapc
MP    TSO   cost 0: (이미 금지 — 고칠 것 없음)
SB    ARMv8 cost 4: P0:Ry->Racq + P0:Wx->Wrel + P1:Rx->Racq + P1:Wy->Wrel
SB    TSO   cost 6: P0:F after Wx + P1:F after Wy
LB    ARMv8 cost 2: P0:Rx->Racq + P1:Ry->Racq | P0:Rx->Racq + P1:Ry->Rapc | P0:Rx->Racq + P1:Wx->Wrel | P0:Rx->Rapc + P1:Ry->Racq | P0:Rx->Rapc + P1:Ry->Rapc | P0:Rx->Rapc + P1:Wx->Wrel | P0:Wy->Wrel + P1:Ry->Racq | P0:Wy->Wrel + P1:Ry->Rapc | P0:Wy->Wrel + P1:Wx->Wrel
LB    TSO   cost 0: (이미 금지 — 고칠 것 없음)
2+2W  ARMv8 cost 2: P0:Wy->Wrel + P1:Wx->Wrel
2+2W  TSO   cost 0: (이미 금지 — 고칠 것 없음)
```

MP 는 flag 쪽만 고치면 된다 (data store 를 `stlr` 로 바꾸는 것은 소용없다 — po;[L] 은 L **앞** 을 묶는다). SB 는 ARMv8 에서 양쪽 `stlr`+`ldar` 하나뿐이다 (= C11 seq_cst). TSO 에서는 `mfence` 양쪽이다. 2+2W 는 **두 번째** store 를 `stlr` 로 바꿔야 한다. 비용표는 교육용 가정이고 실제 비용은 측정해야 한다.
</details>

<details><summary>연습 5 — C11 memory_order 매핑을 모델에 넣기</summary>

ARMv8 매핑(Apple clang `-S` 실측, C-A03/ARM-M05): relaxed=`ldr`/`str`, acquire load=`ldapr`, seq_cst load=`ldar`, release·seq_cst store=`stlr`, fence(acquire)=`dmb ishld`, fence(release/seq_cst)=`dmb ish`. x86 매핑(Batty et al. 2011, cpp0xmappings 표 기준 — 이 레슨에서 x86 `-S` 는 안 돌림): 모든 load=`mov`, seq_cst store=`xchg`(모델에선 W + F), 그 외 store=`mov`, fence(seq_cst)=`mfence`, 그 외 fence=명령 없음. 결과: SB seq_cst → 둘 다 금지. SB rel/acq → 둘 다 허용 (C11 도 허용). relaxed + seq_cst fence → 둘 다 금지. MP rel/acq → 둘 다 금지. MP relaxed → ARMv8 만 허용. MP relaxed + release fence / acquire fence → ARMv8 금지. 이것으로 "C11 프로그램 → 매핑 → 하드웨어 모델" 이 C11 이 약속한 금지를 지키는지 확인했다. 이 검사가 컴파일러 매핑 검증의 원리다 (Batty et al.).
</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"     # 23/23 passed, exit=0
python3 -I practice.py --fixes              # 연습 4 표
```

```text
== 연습 1: WRC 와 multi-copy atomicity
PASS  WRC          SC/TSO/ARMv8 = forbid/forbid/allow
PASS  WRC+po+acq   ARMv8 allow (P1 의 R→W 순서 없음)
PASS  WRC+rel+acq  ARMv8 forbid (MCA: P1 이 본 x 는 P2 에게도 보인다)
PASS  WRC+rel+acq  cycle printout
== 연습 2: coherence 테스트와 internal 공리
...
== 연습 5: C11 매핑 → 모델
PASS  SB seq_cst: forbidden on ARMv8 mapping and x86 mapping
PASS  SB release/acquire: allowed on both (C11 also allows it)
PASS  SB relaxed + seq_cst fences: forbidden on both
PASS  MP release/acquire: forbidden on both ; MP relaxed: allowed on ARMv8 only
PASS  MP relaxed + release fence / acquire fence (dmb ish / dmb ishld): forbidden on ARMv8
23/23 passed
```

<!-- code: example.litmus -->

`example.litmus` (ISA2: CPU0 버퍼 → flag → CPU1 → doorbell → CPU2) 은 `python3 -I solution.py --file example.litmus` 로 돌린다. plain 이면 ARMv8 허용이고 po 간선 세 개가 끊긴다. 파일 안 주석대로 `Wrel`/`Racq` 로 바꾸면 `ARMv8 forbids (external): ... -bob-> ... -rfe-> ... -bob-> ...` 가 나온다 (직접 돌린 결과).

## 10. 핵심 정리

- 후보 실행 = rf(어느 write 를 읽나) × co(위치별 write 순서). fr = rf⁻¹;co 가 "옛 값을 읽었다" 를 그래프에 넣는다.
- SC = acyclic(po ∪ rf ∪ co ∪ fr). 모든 리트머스는 SC 사이클 하나이고, 모델은 그 사이클의 어느 po 를 인정하는지로 갈린다.
- TSO = po 에서 W→R 만 빼고 rfe 만 전역 순서에 넣는다 (store buffer + forwarding). x86 에서 보이는 재배치는 SB 하나, 막는 것은 `mfence`/`xchg`.
- ARMv8 = internal(위치별 SC) + external(rfe ∪ coe ∪ fre ∪ bob). bob 은 `dmb`(full/ld/st), `[A|Q];po`, `po;[L]`, `stlr→ldar`(RCsc, `ldapr` 제외).
- MP·LB·2+2W·IRIW 는 ARM 에서만 허용, SB 는 TSO·ARM 둘 다. plain IRIW 허용은 R→R 재배치 때문이고, MCA 는 IRIW+배리어 금지로 드러난다.
- 최소 수정: MP = `stlr` flag + `ldapr`/`ldar` flag. SB = 양쪽 `stlr`+`ldar`(= seq_cst) 또는 `dmb ish`. 2+2W = 두 번째 store `stlr`.
- 판정 규칙: 금지 + 관찰 = VIOLATION(버그), 허용 + 0 = allowed-unseen(버그 아님, 다른 칩에서 나올 수 있음). M6 실측 19행은 ARMv8 기준 VIOLATION 0, TSO 기준으로는 MP 3행이 VIOLATION 이다. M6 는 TSO 가 아니다.

## 참고자료

- J. Alglave, L. Maranget, M. Tautschnig, "Herding Cats: Modelling, Simulation, Testing, and Data Mining for Weak Memory", ACM TOPLAS 36(2), 2014 — 공리 프레임워크, po/rf/co/fr, 하드웨어 리트머스 결과. <https://doi.org/10.1145/2627752>
- C. Pulte, S. Flur, W. Deacon, J. French, S. Sarkar, P. Sewell, "Simplifying ARM Concurrency: Multicopy-Atomic Axiomatic and Operational Models for ARMv8", POPL 2018 — internal/external 공리, ob = obs ∪ dob ∪ aob ∪ bob. <https://doi.org/10.1145/3158107>
- P. Sewell, S. Sarkar, S. Owens, F. Zappa Nardelli, M. O. Myreen, "x86-TSO: A Rigorous and Usable Programmer's Model for x86 Multiprocessors", CACM 53(7), 2010. <https://doi.org/10.1145/1785414.1785443>
- S. Owens, S. Sarkar, P. Sewell, "A Better x86 Memory Model: x86-TSO", TPHOLs 2009 — 공리·운영 모델 동치.
- L. Lamport, "How to Make a Multiprocessor Computer That Correctly Executes Multiprocess Programs", IEEE Trans. Computers C-28(9), 1979 — SC 정의.
- L. Maranget, S. Sarkar, P. Sewell, "A Tutorial Introduction to the ARM and POWER Relaxed Memory Models", 2012 — MP/SB/LB/WRC/ISA2/IRIW/2+2W 이름과 해설. <https://www.cl.cam.ac.uk/~pes20/ppc-supplemental/test7.pdf>
- S. Sarkar, P. Sewell, J. Alglave, L. Maranget, D. Williams, "Understanding POWER Multiprocessors", PLDI 2011 — non-MCA(IRIW+lwsync).
- M. Batty, S. Owens, S. Sarkar, P. Sewell, T. Weber, "Mathematizing C++ Concurrency", POPL 2011; C/C++11 → 프로세서 매핑 표 <https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html>
- Arm Architecture Reference Manual for A-profile (DDI 0487), B2 "The AArch64 Application Level Memory Model" — 절 번호는 판마다 다름, 확인 필요. <https://developer.arm.com/documentation/ddi0487/latest/>
- herdtools7 (herd7 / litmus7 / diy7), `herd/libdir/aarch64.cat`, `x86tso.cat` — 이 맥에는 설치하지 않음. <https://github.com/herd/herdtools7>, <http://diy.inria.fr/>
- Intel 64 and IA-32 Architectures SDM Vol. 3A, "Memory Ordering" 절 (intra-processor forwarding 예제) — 판에 따라 8장/10장.
- 같은 노트 안: ARM-M05 (배리어 명령·MP/SB 실측·판정기), C-A02 (MP 실측), C-A03 (SB 실측, `ldapr`/`ldar`), MCPP-M13 (SC 인터리빙 탐색기, C++ 모델), OS-A03 (SB 모양 epoch 진입), ARCH-A05 (리트머스 하드웨어 실측 · Rosetta TSO), RISCV-A04 (RVWMO), ARCH-M12 (DMA 캐시 일관성).
