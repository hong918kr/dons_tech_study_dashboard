---
id: ARM-A08
time: 120분
updated: 2026-11-15
---
# ARM-A08 Cortex-M 인터럽트 지연 분석

## 학습 목표

- IRQ 세트(우선순위 바이트 + PRIGROUP, 실행 시간 C, 최소 도착 간격 T)와 크리티컬 섹션(PRIMASK / BASEPRI)이 주어지면 **최악 지연 L 과 최악 응답 R** 을 고정점 반복으로 손 계산할 수 있다: `L = E + B + Σ⌈L/Tj⌉(Cj + O)`.
- 블로킹 B 가 어디서 오는지(섹션, **같은 그룹의 뒤 순번 ISR**, 스태킹·복귀 중인 낮은 IRQ)와, 서브 우선순위가 왜 "선점" 이 아니라 "블로킹" 을 만드는지 설명할 수 있다.
- NVIC 규칙(그룹 선점, 서브·번호 동률 깨기, 꼬리 물기, late arrival, pop 선점, 펜딩 비트 1 개)을 사이클 단위 **이산 사건 시뮬레이터**로 구현하고, 그것을 정답 기준으로 삼아 해석 상한이 안전한지 깨 보려고 시도할 수 있다.
- 최적화(꼬리 물기, PRIMASK→BASEPRI, top/bottom half, 서브→그룹 우선순위, 섹션 단축, lazy FP, SRAM 벡터)의 **전후 수치 표**를 만들고 "누가 얻고 누가 잃나" 를 말할 수 있다.
- 20 kHz 모터 전류 루프가 데드라인을 지키는지 판정하고, 남은 여유로 "크리티컬 섹션은 몇 사이클까지 허용되나" 를 답할 수 있다.

## 1. 문제

**"이 IRQ 세트의 최악 인터럽트 지연을 계산해 보세요"** 는 모터 제어·전원·RF 타이밍을 다루는 펌웨어 스태프 면접의 단골이다. "12 사이클" 은 출발점일 뿐이고, 실제 최악은 **가장 긴 마스킹 구간 + 같은 그룹 ISR + 더 급한 ISR 들의 간섭** 이 정한다. Don 이 SSD 컨트롤러에서 "NVMe 완료 인터럽트가 NAND 채널 인터럽트 뒤에 얼마나 밀리나" 를 따졌던 일을, 이번엔 Cortex-M NVIC 규칙 위에서 사이클 단위로 한다.

이 레슨이 어디에 서 있는지 먼저 정리한다.

| 레슨 | 무엇을 | 숫자 단위 |
|---|---|---|
| BM-A10 | 예외 **하나** 의 사이클 모델 (진입 12, 꼬리 물기 6, late arrival, pop, lazy FP, 플래시 WS) + DWT 측정 펌웨어 | 예외 1 개 |
| RTOS-A04 | ISR 을 "태스크보다 높은 산발 태스크" 로 놓고 **태스크** 의 확장 RTA | 태스크 응답 |
| **ARM-A08 (이 레슨)** | **ISR 끼리**: NVIC 그룹/서브, 마스킹 섹션, 꼬리 물기·late arrival 이 세트 전체의 최악 지연에 주는 영향 | ISR 지연·응답 |

파라미터 이름은 BM-A10 의 `cm_params_t` 를 그대로 쓴다(`entry 12, tail 6, exit 12, late_min 6, fp_words 17` + 이번에 `fetch` 와 기능 스위치 둘을 더함). **모든 사이클 값은 모델이다** — 진입 12·꼬리 물기 6 은 ARM 문서 값(0 WS, 문서 확인 권장), 나머지는 BM-A10 과 같은 가정이다. 측정은 BM-A10 의 보드 펌웨어 몫이고, 이 레슨의 어떤 숫자도 실측이 아니다. 대신 해석과 시뮬레이터라는 **독립된 두 계산** 이 서로를 검사한다.

아래는 Part C 의 베이스라인 모터 세트에서 시뮬레이터가 찾은 전류 루프(CUR)의 최악 장면을 그대로 그린 것이다(`solution.c` 출력의 시간표).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">베이스라인 CUR 최악 장면 — 시뮬레이터 재생 (사이클, 모델). A = CUR 도착</text>
  <line x1="110" y1="40" x2="110" y2="232" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="96" y="36" fill="currentColor">A</text>
  <line x1="425" y1="40" x2="425" y2="232" stroke="currentColor" stroke-dasharray="6 3"/>
  <text x="380" y="36" fill="currentColor">D = 4200 (25 µs)</text>
  <text x="10" y="64" fill="currentColor">스레드 LOG</text>
  <rect x="110" y="50" width="252" height="20" class="f-warn" stroke="currentColor"/>
  <text x="118" y="64" fill="currentColor">PRIMASK 섹션 (A−1 … A+3359)</text>
  <text x="10" y="99" fill="currentColor">CAN (0x00)</text>
  <rect x="362" y="85" width="3" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="365" y="85" width="189" height="20" class="f-mut" stroke="currentColor"/>
  <text x="372" y="99" fill="currentColor">CAN 본문 2520</text>
  <text x="10" y="134" fill="currentColor">CUR (0x10)</text>
  <rect x="555" y="120" width="126" height="20" class="f-ok" stroke="currentColor"/>
  <text x="560" y="134" fill="currentColor">CUR 1680</text>
  <text x="10" y="169" fill="currentColor">ENC, UART</text>
  <rect x="682" y="155" width="12" height="20" class="f-mut" stroke="currentColor"/>
  <rect x="695" y="155" width="15" height="20" class="f-mut" stroke="currentColor"/>
  <path d="M110 150 L110 186" stroke="currentColor"/>
  <text x="114" y="182" fill="currentColor">CUR·ENC·CAN·UART·TICK 모두 A 에 도착 (펜딩, 마스킹됨)</text>
  <line x1="110" y1="205" x2="555" y2="205" stroke="currentColor"/>
  <text x="250" y="200" fill="currentColor">L = 5934 = 3359 + 39 + 2520 + 16</text>
  <line x1="110" y1="226" x2="681" y2="226" stroke="currentColor"/>
  <text x="440" y="222" fill="currentColor">R = 7614 (45.3 µs) &gt; D → 놓침</text>
  <text x="10" y="256" fill="currentColor">39 = 진입 12 + 플래시 10 + FP 17 (스레드 FPCA=1, LSPEN=0) · 16 = 꼬리 물기 6 + 플래시 10</text>
  <text x="10" y="276" fill="currentColor">CAN 은 같은 그룹 0 의 서브 0 → 동시 펜딩에서 CUR(서브 1) 보다 먼저 뽑힌다. 섹션 끝 = 줄 서 있던 순서대로.</text>
  <text x="10" y="294" fill="currentColor">가로 비율: 600 px ≈ 8000 사이클 (진입·꼬리 물기 막대는 실제보다 굵게)</text>
</svg>
```

### Part A — NVIC 규칙, 해석, 그리고 정답 기준 시뮬레이터

- **TODO A1** `prio_group(prio, prigroup)`: ARMv7-M 의 `groupvalue = 2 << PRIGROUP`, `group = prio − prio mod groupvalue`.
- **TODO A2** `exec_priority(active_group, primask, basepri, prigroup)`: 활성 핸들러 그룹, BASEPRI 의 **그룹** 값, PRIMASK(→0) 의 최소.
- **TODO A3** `nvic_select(s, pend, threshold)`: 그룹 < threshold 인 펜딩 중 승자 — prio 바이트(그룹→서브) 다음 번호.
- **TODO A4–A6** `blocking_bound`, `latency_bound`, `response_bound`.
- 주어진 엔진 `sim_run()` 은 A1–A3 를 써서 사건마다 NVIC 를 돈다. 검사: 규칙 하나하나의 단위 테스트, 손 계산 예, **무작위 300 개 세트 × 모든 도착 위상** 에서 시뮬 최대 ≤ 해석 상한, 구성한 빡빡한 4 경우에서 등호.

### Part B — 최적화 전후 표

- **TODO B1** `split_isr()`: 긴 ISR 을 top half + PendSV 식 bottom half 로 나누는 변환.
- 베이스라인 모터 세트에 최적화를 하나씩 적용한 V1–V8 과 조합 V9 — 모든 IRQ 의 R 상한·시뮬 최대를 표로. 누가 얻고 누가 잃는지 CHECK 로 못 박는다.

### Part C — 모터 제어 세트

- 168 MHz, 20 kHz 전류 루프(25 µs 데드라인), 엔코더, CAN RX, FIFO 없는 UART RX, SysTick, 로거 PRIMASK 섹션, RTOS 커널 BASEPRI 섹션.
- **TODO C1** `section_budget()`: 세트가 안전한 채로 섹션 하나를 몇 사이클까지 늘릴 수 있나 (이분 탐색).
- 최종 설계 표 + "면접에서 말하는 순서".

## 2. Problem (English)

> **Worst-case interrupt latency of a Cortex-M IRQ set.**
>
> Each IRQ *i* has an 8-bit priority byte, a WCET `C_i` (cycles), a minimum inter-arrival time `T_i` and a deadline `D_i` (0 = `T_i`). `AIRCR.PRIGROUP` splits priority into group (pre-emption) and sub-priority. Thread code contains critical sections of length `len`, masked either by PRIMASK or by BASEPRI = *b*. Core cost model (cycles): entry `E = 12 + fetch (+17 if the thread has an active FP context and LSPEN = 0)`, exit `X = 12 (+17)`, tail-chain `6 + fetch`, late-arrival minimum `6 + fetch`.
>
> 1. Implement `prio_group`, `exec_priority`, `nvic_select` (pre-emption by group only, ties by full priority byte then IRQ number).
> 2. Implement `blocking_bound`, `latency_bound` (`L = E + B + Σ_{j before i} ⌈L/T_j⌉ (C_j + E + X)`, fixed-point iteration) and `response_bound` (after it starts, only strictly higher *groups* can pre-empt).
> 3. A provided event-driven simulator implements the NVIC rules. Show that for small random sets the simulated maximum over **all** arrival phasings never exceeds your bound, and that the bound is exact for constructed cases.
> 4. `split_isr()` (top half + PendSV bottom half) and `section_budget()` (largest extra section length that keeps every `R_i ≤ D_i` and `R_i ≤ T_i`).
>
> Example: PRIGROUP 4, `hi(0x20, C40, T400)`, `mid(0x40, C60, T600)`, `mid2(0x50, C80, T900)`, PRIMASK 30, BASEPRI(0x40) 60 → `B = 29 / 91 / 59`, `L = 41 / 167 / 219`, `R(mid) = 227`, `R(mid2) = 299`.
>
> Follow-ups: Does a better sub-priority pre-empt? Why is utilization 57 % not enough? What does tail-chaining buy you in the *worst* case? How long may a critical section be? Where does this analysis lie?

## 3. 시작 코드

<!-- code: starter.c -->

맥에서 (호스트 C, 보드·VM 필요 없음):

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cd <ROOT>/lessons/ARM/ARM-A08
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/ARM-A08_st && /tmp/ARM-A08_st    # 14/63, exit 1
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/ARM-A08_so && /tmp/ARM-A08_so    # 63/63, exit 0
```

시작 코드 실행 결과 (실제 출력 일부): 스텁의 `nvic_select` 가 항상 -1 이라 시뮬레이터는 아무 ISR 도 시작하지 않고(멈추지 않고) 끝난다. 해석은 전부 "발산" 이라 무작위 세트가 하나도 통과하지 못한다.

```text
FAIL  A1 prio_group: 256 x 8 exhaustive vs shift formula  (line 410)
FAIL  SIM late arrival: high 9 (BM-A10 d=3), low tail-chains  (line 457)
FAIL  A4 B(mid) = max(PRIMASK 29, BASEPRI 59, same-group mid2 11+80) = 91  (line 524)
   exhaustive: 0 random sets, 0 (set, irq) pairs, 0 sim runs, violations=0, ...
   V0 baseline                          | diverge   | diverge   | diverge   | diverge   | diverge   | -
14/63 passed
```

## 4. 힌트

<details><summary>힌트 1 — PRIGROUP 은 "아래 몇 비트를 서브로 버리나"</summary>

`PRIGROUP = p` 면 바이트의 [p:0] 이 서브, [7:p+1] 이 그룹이다. 그래서 그룹 = 아래 p+1 비트를 지운 값. PRIGROUP 7 이면 8 비트 전부 서브 → 그룹은 모두 0 → 아무도 아무도 선점 못 한다. BASEPRI 도 같은 식으로 그룹만 본다.

</details>

<details><summary>힌트 2 — "앞 순번" 과 "선점자" 는 다른 집합이다</summary>

i 가 아직 시작 전이면, 동시에 펜딩된 것 중 **바이트가 더 작은(또는 같고 번호가 작은)** 모든 IRQ 가 i 보다 먼저 뽑힌다 → 지연 L 의 간섭. 하지만 i 가 시작한 뒤에는 **그룹이 더 작은** 것만 i 를 끊는다 → R 의 간섭. 같은 그룹·앞 순번은 L 에만, 같은 그룹·뒤 순번은 블로킹에만 나타난다.

</details>

<details><summary>힌트 3 — 블로킹은 한 번만 걸린다</summary>

i 가 펜딩된 순간 진행 중일 수 있는 "뒤 순번 일" 은 하나뿐이다: 스레드의 마스킹 섹션 **또는** 같은 그룹 ISR 하나(그 스태킹 남은 것 포함). 섹션이 끝날 때 펜딩된 IRQ 는 스레드가 새 섹션을 열기 전에 받아진다(이 모델의 규칙). 그래서 `B = max(...)` 이지 합이 아니다.

</details>

<details><summary>힌트 4 — 고정점은 아래에서부터</summary>

`L0 = E + B + Σ(Cj + O)` (모든 앞 순번이 한 번씩) 로 시작해 `L = E + B + Σ ceil(L/Tj)(Cj + O)` 를 같아질 때까지 반복. 단조 증가라 처음 같아지는 값이 최소 고정점. 이용률이 1 을 넘으면 끝없이 커지므로 상한(2³⁰)에서 -1.

</details>

<details><summary>힌트 5 — 예산은 이분 탐색</summary>

섹션 길이 S 가 늘면 어떤 R 도 줄지 않는다(B 가 늘거나 그대로). 그래서 "S 에서 됨, S+1 에서 안 됨" 인 S 를 이분 탐색으로. 0 에서도 안 되면 -1, 상한까지 다 되면(아무도 안 막는 BASEPRI) 상한.

</details>

## 5. 풀이

### Part A — NVIC 규칙 세 개 (A1–A3)

| 규칙 | 코드 | 시뮬 단위 테스트 (실제 통과) |
|---|---|---|
| 그룹 = prio − prio mod (2 ≪ PRIGROUP) | `prio_group` | 256 × 8 전수를 `(pr >> (pg+1)) << (pg+1)` 와 대조 |
| 선점은 **그룹이 엄격히 작을 때만** | `nvic_select(…, threshold)` 의 `>=` 거름 | "sub-priority: no preemption, waits + tail", "PRIGROUP 3: same bytes now preempt" |
| 동시 펜딩: 바이트 → 번호 | 같은 함수의 `<` (엄격) | "simultaneous: better sub first, other tail-chains" |
| 실행 우선순위 = min(활성 그룹, BASEPRI 그룹, PRIMASK→0) | `exec_priority` | "BASEPRI 0x50: group 0x20 passes, group 0x40 waits" |
| 펜딩 비트는 1 개 | `sim_run` 도착 처리 | 바쁜 동안 3 번 도착 → 1 번 실행, 2 번 놓침 |
| 꼬리 물기 / pop / late arrival | `sim_run` (주어짐) | 6 / 6 / 9 — BM-A10 의 d=3 값과 같다 |

`exec_priority` 는 ARMv7-M ARM 의 ExecutionPriority 의사코드를 축약한 것이다(기억으로 옮김 — B1.5.4 부근, 문서 확인 권장): BASEPRI 가 0 이 아니면 그 값에서 **서브 비트를 뺀 그룹 값**으로 끌어올리고, PRIMASK 면 0, 활성 예외 중 가장 급한 그룹과 비교해 최소. 그래서 PRIGROUP 4 에서 `BASEPRI = 0x50` 은 `0x40` 과 똑같이 동작한다 — 0x40 IRQ 도 막는다. ARM-M10 이 QEMU 에서 본 "BASEPRI 는 값 ≥ BASEPRI 를 막는다" 의 그룹 버전이다.

**모델이 고른 것 (확인 권장):** late arrival 은 "목표보다 **그룹이** 더 급한" 펜딩이 스태킹 중 올 때만 벡터를 바꾼다(같은 그룹·더 좋은 서브는 바꾸지 않음). 실제 코어가 서브까지 보고 바꾼다면 그건 지연을 줄이는 쪽이라 아래 상한은 여전히 안전하다. 같은 사이클에 도착한 IRQ 는 스레드의 `cpsid` 보다 먼저 받아진다.

### Part A — 해석: B, L, R (A4–A6)

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">i 의 최악 창 — 해석이 세는 것</text>
  <path d="M30 40 L30 70" stroke="currentColor"/>
  <text x="14" y="36" fill="currentColor">i 도착</text>
  <rect x="30" y="50" width="150" height="26" class="f-warn" stroke="currentColor"/>
  <text x="38" y="67" fill="currentColor">B: 뒤 순번 하나</text>
  <rect x="180" y="50" width="120" height="26" class="f-mut" stroke="currentColor"/>
  <text x="186" y="67" fill="currentColor">앞 순번 j: Cj + O</text>
  <rect x="300" y="50" width="90" height="26" class="f-mut" stroke="currentColor"/>
  <text x="306" y="67" fill="currentColor">k: Ck + O</text>
  <rect x="390" y="50" width="100" height="26" class="f-mut" stroke="currentColor"/>
  <text x="396" y="67" fill="currentColor">j 다시 (⌈L/Tj⌉)</text>
  <rect x="490" y="50" width="40" height="26" class="f-acc" stroke="currentColor"/>
  <text x="498" y="67" fill="currentColor">E</text>
  <rect x="530" y="50" width="190" height="26" class="f-ok" stroke="currentColor"/>
  <text x="538" y="67" fill="currentColor">i 본문 Ci (+선점자)</text>
  <line x1="30" y1="92" x2="530" y2="92" stroke="currentColor"/>
  <text x="200" y="108" fill="currentColor">L = E + B + Σ⌈L/Tj⌉(Cj + O)   (앞 순번 = 바이트가 더 작거나, 같고 번호가 작음)</text>
  <line x1="30" y1="122" x2="720" y2="122" stroke="currentColor"/>
  <text x="200" y="138" fill="currentColor">R = E + B + Ci + Σ같은 그룹 앞 순번 ⌈L/Tj⌉(Cj+O) + Σ더 급한 그룹 ⌈R/Tj⌉(Cj+O)</text>
  <rect x="10" y="154" width="720" height="88" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="172" fill="currentColor">B = max of:  ① i 를 막는 섹션 len − 1   ② 같은 그룹·뒤 순번 k: (E − 1) + Ck (+X, 꼬리 물기 없는 코어)</text>
  <text x="20" y="190" fill="currentColor">③ 꼬리 물기 없는 코어: 뒤 순번의 복귀 X   ④ late arrival 없는 코어: 낮은 그룹의 스태킹 E − 1</text>
  <text x="20" y="208" fill="currentColor">O = E + X = 진입 + 복귀 (보수적: 실제론 꼬리 물기로 6 에 끝나기도 한다)</text>
  <text x="20" y="226" fill="currentColor">E = 12 + fetch (+17 FP) · X = 12 (+17 FP) — 모델 값 (BM-A10 과 같은 파라미터)</text>
</svg>
```

각 항이 왜 거기 있나:

| 항 | 왜 | 언제 0 이 되나 |
|---|---|---|
| E | i 자신의 진입. 꼬리 물기로 들어오면 6 이지만 최악은 스레드에서 12 (+fetch, +FP) | 없음 |
| B ① 섹션 | i 가 마스킹된 동안 기다림. 같은 사이클 도착은 cpsid 전에 받으므로 최악은 다음 사이클 → `len − 1` | BASEPRI 가 i 의 그룹보다 위(더 급한 값)면 |
| B ② 같은 그룹 | 같은 그룹은 서로 못 끊는다. 뒤 순번 k 가 막 스태킹을 시작했으면 `E − 1 + Ck` 를 고스란히 기다림 | i 를 그 그룹에 혼자 두면 |
| 간섭 Σ | 앞 순번이 창 [0, L) 안에 도착하는 만큼. 스태킹 중 도착(late arrival)도 i 를 민다 | i 가 가장 앞 순번이면 |
| O | 앞 순번 job 하나가 i 에게 떠넘기는 진입 + 복귀 | — |

**손 계산** (`part_a_analysis`, PRIGROUP 4, `hi 0x20 C40 T400`, `mid 0x40 C60 T600`, `mid2 0x50 C80 T900`, PRIMASK 30, BASEPRI(0x40) 60, E = 12, O = 24):

| | B | L | R |
|---|---|---|---|
| hi | PRIMASK 29 (BASEPRI 0x40 은 0x20 을 안 막음) | 12 + 29 = 41 | 41 + 40 = 81 |
| mid | max(29, 59, mid2: 11 + 80) = **91** — 같은 그룹 뒤 순번이 섹션보다 크다 | 12 + 91 + (40+24) = 167 | 12 + 91 + 60 + 64 = 227 |
| mid2 | BASEPRI 59 (mid 는 **앞 순번** → 블로킹 아님) | 12 + 59 + 64 + 84 = 219 | 12 + 59 + 80 + 84 + 64 = 299 |

mid2 의 R 에서 mid(84)는 L 창으로만 센다(시작 뒤엔 같은 그룹이 못 끊음), hi(64)는 R 창으로 센다. `hi` 의 T 를 100 으로 줄이면 고정점이 돈다: `219 → 347 → 411 → 475 → 475`, hi 가 창 안에 5 번 — 테스트 "fixed point 219->347->411->475". T = 50 이면 이용률이 1 을 넘어 -1.

### Part A — 시뮬레이터가 정답 기준이다

`sim_run()` 은 이산 사건(discrete-event) 엔진이다. 시각은 정수 사이클이고, 다음 사건(도착, 스태킹/복귀 완료, ISR 본문 끝, 섹션 시작/끝)으로 점프한다. 사건마다 BM-A10 과 같은 상태기계(RUN / ENTRY / TAIL / EXIT)를 돌리되, 결정은 전부 A1–A3 를 통한다. 해석 코드와 **공유하는 것은 NVIC 규칙 세 함수뿐**이다 — 비용 합산·창·ceil 은 시뮬에 없다.

위상 탐색 `sim_search()`: 대상 IRQ i 는 시각 A 에 한 번, 나머지 j 는 주기 Tj 로 계속 도착하되 "A 와의 상대 위상" x_j 를 바꾼다. 작은 세트에선 **모든 x_j ∈ [0, Tj)** × (섹션 없음 + 각 섹션 시작 A−len … A) 를 다 돈다. 실제 출력:

```text
   exhaustive: 300 random sets, 600 (set, irq) pairs, 725883 sim runs, violations=0, L tight=250,
               mean sim/bound=856 permille, max gap=117 cycles, sim failures=0
   tight: PRIMASK 40 only                  bound L= 51  sim max L= 51  (R bound  61, sim  61)
   tight: hp interference, no tail/late    bound L= 66  sim max L= 66  (R bound  76, sim  76)
   tight: same-group blocker, no tail/late bound L= 65  sim max L= 65  (R bound  75, sim  75)
   tight: blocking + hp, no tail/late      bound L= 90  sim max L= 90  (R bound 100, sim 100)
   same set, tail-chain ON, simultaneous arrival only: lo latency 48
   tail-chain gain on 'hp interference': sim 66 -> 53 (bound 66)
```

무작위 세트는 PRIGROUP 3–5, IRQ 2–3 개, 섹션 0–2 개(PRIMASK/BASEPRI), FP 25 %, fetch 0/4, 그리고 **가상 코어 네 가지**(꼬리 물기 on/off × late arrival on/off)를 섞는다. 레슨 밖 스크래치에서 같은 코드로 시드를 바꿔 더 돌렸다(2–3 개 IRQ, C ≤ 60, 섹션 ≤ 40 으로 키운 것 포함): 4309 / 4217 / 4216 / 3705 / 3663 쌍, 위반 0. 증명은 아니고 "깨 보려 했는데 못 깼다" 다.

**시뮬레이터가 해석을 고친 사건.** 처음 `O = E + X` 만 썼을 때 무작위 검사(당시 120 세트)가 **21 건 위반**을 냈고, 전부 "꼬리 물기 없음 + late arrival 있음" 가상 코어였다. 시간표를 찍어 보니:

```text
   A    +0  arrive  r            <- i (낮음) 도착, 스태킹 시작 (E)
   A   +15  arrive  r            <- 높은 IRQ: i 의 스태킹 중 → late arrival 로 벡터 교체
   A   +25  start   r  late
   A   +42  end     r
   A   +54  resume  thread       <- 꼬리 물기가 없으니 복귀(X)
   A   +70  start   r  entry     <- i 는 스태킹을 처음부터 다시 (E)
```

(위는 스크래치 디버그 프로그램의 실제 시간표, `<-` 주석만 내가 붙임. E = 12 + fetch 4 = 16, 높은 쪽 ready = max(16, 15 + 6 + 4) = 25.) i 의 첫 스태킹(E − 1)이 높은 쪽에 "빼앗기고", 꼬리 물기가 없으니 i 는 복귀 뒤 다시 E 를 낸다. 앞 순번 job 하나가 떠넘기는 비용이 `E + X` 가 아니라 `E + X + late_min + fetch − 1` 까지 간다. 그래서 `cost_O()` 에 그 경우만 `late_min + fetch` 를 더했다(실제 Cortex-M 은 꼬리 물기가 늘 있으므로 실칩 분석엔 영향 없음). 이 수정 전 버전은 연습용 변이 m6 으로 남겨 두었고(아래), 지금 하네스가 잡는다.

**꼬리 물기 켠 코어의 최악은 "동시 도착" 이 아니다.** 'hp interference' 세트에서 꼬리 물기를 켜면 66 → 48 일 것 같고 실제로 **동시 도착 장면만 돌리면 48** 이지만(복귀 12 + 진입 12 → 꼬리 6, 18 절약) 위상 전수 탐색은 **53** 을 찾는다: 높은 IRQ 가 낮은 쪽 스태킹 11 사이클째에 오면 `11 + late 6 + 30 + tail 6 = 53`. late arrival 은 높은 쪽엔 이득, 낮은 쪽엔 최대 5 사이클 손해다. 임계 순간(critical instant)을 "모두 동시에" 로만 두는 손 계산이 낙관적일 수 있는 지점이고, 위상 전수 탐색이 필요한 이유다.

### Part B — 최적화 전후 표

베이스라인(V0)은 일부러 흔한 실수 셋을 넣었다: (1) CAN 을 "중요하니까" **같은 그룹 0, 더 좋은 서브** 로 둠, (2) 로거가 64 B 복사를 `cpsid i` 안에서 함(3360 사이클 = 20 µs), (3) 스레드가 FPU 를 쓰는데 "결정성" 을 이유로 LSPEN = 0. 플래시는 168 MHz 5 WS 최악(ART miss, BM-A10 `fetch_penalty` = 10). 각 V 는 하나만 바꾼다. **R 상한, µs @168 MHz, '!' = D 또는 T 초과** (실제 출력):

```text
   variant                              | CUR       | ENC       | CAN       | UART      | TICK      | PSV
   V0 baseline                          | 45.63!    | 46.92     | 35.22     | 74.33     | 77.11     | -
   V1 tail-chain OFF (hypothetical)     | 45.72!    | 73.11     | 35.22     | 74.80     | 77.69     | -
   V2 late-arrival OFF (hypothetical)   | 45.63!    | 46.92     | 35.22     | 74.33     | 77.11     | -
   V3 LOG PRIMASK -> BASEPRI 0x40       | 25.63!    | 26.93     | 25.45     | 74.33     | 77.11     | -
   V4 LOG section 3360 -> 336           | 27.63!    | 28.92     | 25.45     | 32.09     | 34.88     | -
   V5 CAN sub-prio 0x00 -> group 0x60   | 30.22!    | 31.52     | 48.52!    | 33.11     | 77.11     | -
   V6 CAN split: top 336 + PendSV       | 32.63!    | 33.92     | 22.22     | 35.52     | 38.30     | 64.72!
   V7 FP lazy (LSPEN=1)                 | 45.32!    | 46.42     | 35.12     | 73.22     | 75.80     | -
   V8 vectors+ISRs in SRAM (fetch 0)    | 45.51!    | 46.75     | 35.16     | 73.97     | 76.70     | -
   V9 = V3+V4+V5+V6+V7                  | 10.13     | 11.22     | 18.38     | 16.18     | 20.97     | 34.37
```

CUR 만 따로, 해석 상한 대 시뮬 최대(임계 순간 후보 = 섹션 수 + 1 개 + 무작위 위상 3000, **하한일 뿐**) — 사이클:

```text
   variant                              | L bound | L sim | R bound | R sim | D=4200
   V0 baseline                          |    5986 |  5934 |    7666 |  7614 | MISSES
   V1 tail-chain OFF (hypothetical)     |    6002 |  5986 |    7682 |  7666 | MISSES
   V2 late-arrival OFF (hypothetical)   |    5986 |  5934 |    7666 |  7614 | MISSES
   V3 LOG PRIMASK -> BASEPRI 0x40       |    2627 |  2575 |    4307 |  4255 | MISSES
   V4 LOG section 3360 -> 336           |    2962 |  2910 |    4642 |  4590 | MISSES
   V5 CAN sub-prio 0x00 -> group 0x60   |    3398 |  3398 |    5078 |  5078 | MISSES
   V6 CAN split: top 336 + PendSV       |    3802 |  3750 |    5482 |  5430 | MISSES
   V7 FP lazy (LSPEN=1)                 |    5935 |  5917 |    7615 |  7597 | MISSES
   V8 vectors+ISRs in SRAM (fetch 0)    |    5966 |  5914 |    7646 |  7594 | MISSES
   V9 = V3+V4+V5+V6+V7                  |      22 |    22 |    1702 |  1702 | meets
```

모든 V × 모든 IRQ 에서 시뮬 최대 ≤ 상한 (CHECK). 근거와 함께 읽으면:

| 변경 | 얻는 쪽 | 잃는 쪽 | 근거 (식의 어느 항) |
|---|---|---|---|
| V1 꼬리 물기 끔 (가상) | — | 전원. 시뮬 CUR 5934 → 5986 (+52), ENC 상한 7884 → **12284** | O 와 B 에 X 가 붙음. ENC 는 R 이 CAN 의 T(7896)를 20 사이클 넘어 CAN 이 창에 **두 번** 들어감 → 계단 (아래 그림) |
| V2 late arrival 끔 (가상) | — | 이 세트에선 변화 없음 | B ④ 는 E − 1 = 38 인데 섹션 3359 가 이미 지배 |
| V3 PRIMASK → BASEPRI 0x40 | CUR −3359, ENC, CAN | UART·TICK 는 그대로(아직 막힘) | B ① 이 0x40 이상 그룹에만. **그래도 CUR 놓침** — CAN 이 앞 순번이라 2588 이 남음 |
| V4 섹션 3360 → 336 | 모두 (UART 74 → 32 µs) | 없음 (단, 복사를 섹션 밖으로 → 로거 자체의 동기화 설계가 필요) | B ① 직선 감소 |
| V5 CAN 서브 → 그룹 0x60 | CUR (CAN 2588 이 간섭에서 빠짐), UART | **CAN 35 → 48.5 µs, T 초과** | CAN 이 이제 CUR·ENC·UART 에게 끊김 |
| V6 CAN top 336 + PendSV(0xF0) | CUR, CAN top (35 → 22 µs) | **bottom half 64.7 µs**, 일이 34 사이클 + 예외 1 번 늘어남 | 앞 순번의 Cj 가 2520 → 336 |
| V7 lazy FP | 모두 조금 (CUR −51) | ISR 이 FP 를 쓰면 지연이 ISR 안으로 이동 (BM-A10) | E 의 +17, X 의 +17 사라짐 |
| V8 벡터+ISR SRAM | 모두 조금 (CUR −20) | SRAM 사용, (STM32F4 는 SRAM 실행이 느릴 수 있음 — 확인 권장) | fetch 10 → 0 |
| V9 조합 | CUR 45.6 → **10.1 µs**, 모든 IRQ D 이내 | 없음 (PSV 34.4 µs, 여전히 T 47 µs 이내) | B(CUR) = 0, 앞 순번 0 |

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">CUR 응답 상한 R (µs @168 MHz) — 해석 값, 모델</text>
  <line x1="475" y1="28" x2="475" y2="290" stroke="currentColor" stroke-dasharray="6 3"/>
  <text x="480" y="300" fill="currentColor">D = 25 µs</text>
  <text x="10" y="45" fill="currentColor">V0 baseline</text><rect x="250" y="34" width="411" height="16" class="f-warn" stroke="currentColor"/><text x="666" y="46" fill="currentColor">45.63</text>
  <text x="10" y="70" fill="currentColor">V1 tail-chain off</text><rect x="250" y="59" width="412" height="16" class="f-warn" stroke="currentColor"/><text x="666" y="71" fill="currentColor">45.72</text>
  <text x="10" y="95" fill="currentColor">V2 late-arrival off</text><rect x="250" y="84" width="411" height="16" class="f-warn" stroke="currentColor"/><text x="666" y="96" fill="currentColor">45.63</text>
  <text x="10" y="120" fill="currentColor">V3 PRIMASK→BASEPRI</text><rect x="250" y="109" width="231" height="16" class="f-warn" stroke="currentColor"/><text x="486" y="121" fill="currentColor">25.63</text>
  <text x="10" y="145" fill="currentColor">V4 section 3360→336</text><rect x="250" y="134" width="249" height="16" class="f-warn" stroke="currentColor"/><text x="504" y="146" fill="currentColor">27.63</text>
  <text x="10" y="170" fill="currentColor">V5 CAN sub→group</text><rect x="250" y="159" width="272" height="16" class="f-warn" stroke="currentColor"/><text x="527" y="171" fill="currentColor">30.22</text>
  <text x="10" y="195" fill="currentColor">V6 CAN split</text><rect x="250" y="184" width="294" height="16" class="f-warn" stroke="currentColor"/><text x="549" y="196" fill="currentColor">32.63</text>
  <text x="10" y="220" fill="currentColor">V7 lazy FP</text><rect x="250" y="209" width="408" height="16" class="f-warn" stroke="currentColor"/><text x="666" y="221" fill="currentColor">45.32</text>
  <text x="10" y="245" fill="currentColor">V8 SRAM vectors/ISRs</text><rect x="250" y="234" width="410" height="16" class="f-warn" stroke="currentColor"/><text x="666" y="246" fill="currentColor">45.51</text>
  <text x="10" y="270" fill="currentColor">V9 V3+V4+V5+V6+V7</text><rect x="250" y="259" width="91" height="16" class="f-ok" stroke="currentColor"/><text x="346" y="271" fill="currentColor">10.13 (meets)</text>
  <text x="10" y="300" fill="currentColor">한 가지 고침으로는 아무것도 25 µs 안에 못 들어간다 — 원인이 셋이라서.</text>
</svg>
```

교훈 하나: **최적화 하나로 끝나지 않는다.** 원인이 셋(섹션, 같은 그룹 앞 순번 CAN, 긴 CAN ISR)이고 해석식의 서로 다른 항에 있으니, 각 항을 0 으로 만드는 수가 따로 필요하다. 꼬리 물기·late arrival·lazy FP·SRAM 같은 "코어 기능" 은 최악을 수십 사이클 바꾸고, 우선순위·섹션 설계는 수천 사이클을 바꾼다.

**해석의 비관성 — 계단.** `demo_sweep.c` (2): V0 에서 CAN 의 C 만 바꾸며 ENC 의 R 을 본다 (실제 출력):

```text
   C_can | R bound | R sim (crit+600) | bound - sim
    2490 |    7854 |  7759         |    95
    2520 |    7884 |  7780         |   104
    2550 |   12280 |  7810         |  4470
    2580 |   12340 |  7840         |  4500
    2610 |   12400 |  7870         |  4530
    2640 |   12460 | 12270         |   190
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">ENC 응답 vs CAN ISR 길이 (V0, 사이클) — 실선 해석 상한, 점선 시뮬 최대</text>
  <line x1="100" y1="262" x2="710" y2="262" stroke="currentColor"/>
  <line x1="100" y1="36" x2="100" y2="262" stroke="currentColor"/>
  <text x="44" y="264" fill="currentColor">7500</text>
  <text x="38" y="52" fill="currentColor">12500</text>
  <line x1="96" y1="243" x2="710" y2="243" stroke="currentColor" stroke-dasharray="2 4"/>
  <text x="104" y="238" fill="currentColor">CAN 의 T = 7896</text>
  <rect x="400" y="40" width="180" height="200" class="f-warn"/>
  <text x="410" y="140" fill="currentColor">상한만 계단</text>
  <text x="410" y="156" fill="currentColor">(비관 구간 90 사이클)</text>
  <polyline points="100,248.8 160,247.6 220,246.3 280,245.0 340,243.8 400,57.8 460,55.2 520,52.7 580,50.2 640,47.6 700,45.1" fill="none" stroke="currentColor" stroke-width="2"/>
  <polyline points="100,253.2 160,252.0 220,250.7 280,249.0 340,248.2 400,246.9 460,245.6 520,244.3 580,58.2 640,55.7 700,53.1" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3"/>
  <text x="92" y="280" fill="currentColor">2400</text><text x="332" y="280" fill="currentColor">2520</text>
  <text x="392" y="280" fill="currentColor">2550</text><text x="572" y="280" fill="currentColor">2640</text>
  <text x="680" y="280" fill="currentColor">2700</text>
  <text x="300" y="296" fill="currentColor">가로: CAN 의 C (사이클). 아래 두 선은 7660–7884 로 거의 겹친다</text>
</svg>
```

해석은 R 창이 CAN 의 T 를 넘는 순간(2550) CAN 을 **두 번** 센다. 시뮬은 실제 R 이 T 를 넘을 때(2640)에야 같은 계단을 밟는다. 그 사이 90 사이클은 해석의 O = 24 + FP 34 같은 보수적 오버헤드가 만든 거짓 계단이다. "분석은 놓친다는데 시뮬은 괜찮다" 면 이 구간을 의심하고, "시뮬은 괜찮다" 는 하한일 뿐이니 **판정은 상한으로** 한다.

### Part C — 모터 제어 세트: 데드라인을 지키나

세트(가정, 168 MHz STM32F4 류): CUR 20 kHz 전류 루프 C = 1680 (10 µs), D = 반주기 4200 (25 µs) / ENC 엔코더 캡처 C = 150, T = 100 µs / CAN RX 1 Mbit/s 최소 프레임 간격 47 µs (T = 7896), 파싱 C = 2520 / UART RX 115200 bps FIFO 없음, 1 글자 86.8 µs / SysTick 1 kHz C = 400 / 섹션: 로거, 커널 BASEPRI(0x50) 600 (FreeRTOS 의 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 자리 — ARM-M10).

최종 설계 V9 (실제 출력):

```text
== Part C: final design (V9) at 168 MHz
   IRQ                      prio grp |    C     T     D |  B     L bound   R bound  | slack(us)
   CUR  20kHz current loop  0x10 0x00 |  1680  8400  4200 |     0     22=0.13     1702=10.13  | 14.86
   ENC  encoder capture     0x20 0x20 |   150 16800 16800 |     0   1736=10.33    1886=11.22  | 88.77
   CAN  RX FIFO0 (parse)    0x60 0x60 |   336  7896  7896 |   599   2753=16.38    3089=18.38  | 28.61
   UART RX no FIFO          0x40 0x40 |   200 14583 14583 |   599   2519=14.99    2719=16.18  | 70.61
   TICK SysTick 1kHz        0x60 0x60 |   400 168000 168000 |   599   3123=18.58    3523=20.97  | 979.02
   PSV  CAN bottom half     0xF0 0xE0 |  2218  7896 23688 |   599   3557=21.17    5775=34.37  | 106.62
   budget: one more PRIMASK section <= 2499 cycles, BASEPRI 0x40 <= 2721, BASEPRI 0x20 <= 2721
   at budget+1 (PRIMASK 0x00) fails: CUR  (R 4201 vs D 4200, T 8400)
   at budget+1 (BASEPRI 0x40) fails: PSV  (R 8267 vs D 23688, T 7896)
   at budget+1 (BASEPRI 0x20) fails: PSV  (R 8267 vs D 23688, T 7896)
```

- **CUR 는 지킨다**: L = 22 (진입 12 + 플래시 10, 아무것도 안 막음), R = 1702 = 10.13 µs, 여유 14.86 µs. 최악 장면(시뮬 재생)은 "모두 A 에 도착 → CUR 22 에 시작 → 끝나면 ENC, UART 가 꼬리 물기" 다.
- **섹션 예산**(TODO C1): 섹션을 하나 더 넣는다면 PRIMASK 는 **2499 사이클(14.9 µs)** 까지 — 이걸 정하는 건 CUR: `22 + 2498 + 1680 = 4200`. BASEPRI 0x40(CUR·ENC 통과)이면 2721 까지, 0x20 으로 ENC 까지 막아도 2721 — 한계를 정하는 건 데드라인이 아니라 **PSV(bottom half)의 R ≤ T** 다(R 8267 > T 7896 → 다음 PendSV 요청이 펜딩 중인 것과 합쳐짐). 데드라인 23688 은 한참 남았는데도. "예산을 깨는 IRQ 가 누구인가" 까지 출력으로 확인했다. 테스트가 "S 에서 됨, S+1 에서 안 됨" 을 확인한다.
- `demo_sweep.c` (1): V5 설계에서 로거 PRIMASK 길이를 0 → 4000 으로 늘리면 CUR R 이 1719 → 5718 로 **정확히 직선**(해석 = 시뮬, 모든 점), 2400 에서 4118 로 지키고 2800 에서 4518 로 놓친다. 예산 함수는 2482 를 낸다 (= 4200 − 39 − 1680 + 1).

**면접에서 말하는 순서** (화이트보드 5 분):

| 순서 | 말할 것 | 이 세트에서 |
|---|---|---|
| 1 | 단위와 코어 비용부터: 진입 12, 꼬리 6 (0 WS, 문서), 복귀·late·FP·WS 는 가정이라고 밝힘 | E = 12 + 10 (+17), X = 12 (+17) |
| 2 | 우선순위 바이트 → PRIGROUP 으로 그룹/서브를 가른다 | PRIGROUP 4: 0x00 과 0x10 은 같은 그룹 0 |
| 3 | 대상 IRQ 의 **앞 순번 / 선점자 / 뒤 순번** 세 집합을 적는다 | CUR: 앞 순번 = CAN, 선점자 = 없음, 뒤 = 나머지 |
| 4 | B = max(i 를 막는 섹션 len − 1, 같은 그룹 뒤 순번 E − 1 + C) | 3359 (로거 PRIMASK) |
| 5 | L = E + B + Σ⌈L/T⌉(C + O), 한두 번 반복 | 39 + 3359 + 2588 = 5986 |
| 6 | R = L + C + 선점자 간섭, D 와 T 와 비교 | 7666 > 4200 → 놓침 |
| 7 | 어느 항이 큰지 보고 처방: 섹션 → BASEPRI/단축, 같은 그룹 → 그룹 분리, 긴 ISR → top/bottom | V3 + V4 + V5 + V6 → 1702 |
| 8 | 한계: 상한은 보수적(계단), 시뮬/측정은 하한, 모델 밖 항목 | Q7 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

검증 (맥):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/ARM-A08_so && /tmp/ARM-A08_so            # 63/63 passed, 0.19 s
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/ARM-A08_asan && /tmp/ARM-A08_asan   # 63/63, 경고 없음
```

**변이 검사** (스크래치의 사본에 버그를 넣고 하네스가 잡는지 — 레슨 디렉터리엔 남기지 않음):

| 변이 | 결과 | 처음 잡은 검사 |
|---|---|---|
| m1 `nvic_select` 가 서브를 무시(그룹만 비교) | 58/63 | A3, SIM simultaneous, **전수 검사 위반** |
| m2 같은 그룹 블로킹 누락 | 57/63 | A4 B(mid), A5, A6 |
| m3 ceil 대신 floor | 56/63 | A5 L(mid), 고정점 |
| m4 BASEPRI 를 그룹 대신 바이트 전체로 | 61/63 | A2, SIM BASEPRI 0x50 |
| m5 섹션 블로킹 len − 2 | 53/63 | A4 B(hi), A5 |
| m6 꼬리 물기 없음 + late 의 `late_min + fetch` 빼기 (원래 내 버그) | 61/63 | 전수 검사 위반, V1 표 |

## 7. 자주 하는 실수

1. **"서브 우선순위가 높으니 먼저 실행되겠지"** → 서브는 동시 펜딩일 때 순서만 정한다. 같은 그룹에 넣으면 서로 못 끊어서, 긴 ISR 이 서브가 더 좋은 짧은 ISR 의 **블로킹** 이 된다. V0 의 CAN(0x00)·CUR(0x10)이 정확히 이것. 고친 코드: 급한 것은 **그룹** 을 다르게 (`CAN 0x00 → 0x60`).
2. **블로킹을 합으로 센다** (`B = 섹션 + 같은 그룹 C`) → 한 번만 걸린다(힌트 3). 너무 비관적이면 멀쩡한 설계를 버리게 된다. 반대로 **같은 그룹 블로킹을 잊으면**(m2) 상한이 거짓말을 한다.
3. **BASEPRI 를 바이트로 비교** → 실행 우선순위는 BASEPRI 의 **그룹** 값. PRIGROUP 4 에서 0x50 은 0x40 IRQ 를 막는다(m4). 또 구현 비트가 3 개뿐인 칩에선 아래 비트가 RAZ 라 0x50 을 써도 0x40 이 읽힌다(연습 1).
4. **이용률이 70 % 미만이니 안전** → 이용률은 필요조건일 뿐. V0 는 56.9 % 인데 CUR 이 놓친다(연습 2). 블로킹과 순서는 이용률에 안 보인다.
5. **"동시 도착" 한 장면만 손으로 돌려 최악이라 한다** → late arrival 이 있는 코어에선 높은 IRQ 가 낮은 쪽 스태킹 **중간** 에 올 때가 더 나쁠 수 있다(53 > 48). 해석 상한을 쓰거나, 위상을 전부 돌린다.
6. **시뮬레이션·측정 최대를 "최악" 이라 보고** → 하한이다. V0 CUR: 상한 7666, 무작위 3000 위상 최대 7614. 데드라인 판정은 상한으로.
7. **R ≤ D 만 보고 R ≤ T 를 안 본다** → 펜딩 비트가 1 개라 다음 도착이 오기 전에 못 끝내면 **도착을 놓친다**(시뮬 실측: 3 번 도착 → 1 번 실행). FIFO 없는 UART 는 더 엄격: 다음 글자 전에 DR 을 읽어야 한다(연습 3).
8. **최적화 하나를 넣고 끝** → 원인이 여러 항에 있으면 하나로는 안 된다(표의 V3–V8 전부 놓침, V9 만 통과). 그리고 매 최적화마다 **잃는 쪽** 을 확인한다(V5 의 CAN, V6 의 bottom half).

## 8. 면접 Q&A

<details><summary>Q1. 이 IRQ 세트(우선순위·C·T·섹션)의 최악 인터럽트 지연을 계산해 보세요.</summary>

순서는 §5 의 표대로: 코어 비용(E = 12 + WS (+FP), O = 진입 + 복귀) → PRIGROUP 으로 그룹 → 대상의 앞 순번/선점자/뒤 순번 → B = max(막는 섹션 len − 1, 같은 그룹 뒤 순번 E − 1 + C) → `L = E + B + Σ⌈L/Tj⌉(Cj + O)` 고정점 → `R = … + Ci + Σ선점자⌈R/Tj⌉(Cj+O)` → D, T 와 비교. 베이스라인 CUR: E 39, B 3359, 앞 순번 CAN 2520 + 68 → L 5986, R 7666 (45.6 µs) > 25 µs. 꼬리: "그 12 사이클은 어디서?" → ARM 문서의 0 WS 진입 값, 나머지는 가정이라고 분리해 말한다(BM-A10).

</details>

<details><summary>Q2. 서브 우선순위가 더 높은 IRQ 는 현재 ISR 을 선점하나요? 그럼 서브는 언제 쓰죠?</summary>

안 한다. 선점은 그룹이 엄격히 더 급할 때만이고, 서브는 같은 그룹 안에서 **동시에 펜딩일 때 누가 먼저냐** 만 정한다(시뮬 테스트 "sub-priority: no preemption, waits + tail"; PRIGROUP 3 으로 같은 바이트를 다른 그룹으로 만들면 즉시 중첩). 그래서 같은 그룹은 서로에게 블로킹이 된다. 쓰는 곳: 서로 끊을 필요가 없고 끊으면 스택만 깊어지는 ISR 묶음(예: 여러 UART), 스택 깊이 상한을 줄이고 싶을 때 — 같은 그룹은 중첩이 안 되니 최대 중첩 깊이 = 그룹 수. 꼬리: "그룹 비트를 다 쓰면(PRIGROUP 작게)?" → 중첩 깊이와 스택 최악이 늘어난다. 트레이드오프다.

</details>

<details><summary>Q3. PRIMASK 대신 BASEPRI 를 쓰면 누가 얼마나 얻나요? 공짜인가요?</summary>

BASEPRI(b) 는 그룹이 b 의 그룹 이상인 것만 막는다. V3: CUR·ENC 의 블로킹 3359 가 0 이 되어 CUR R 7666 → 4307, 하지만 UART·TICK(0x40 이상)은 그대로 74 µs. 공짜가 아닌 점: (1) 섹션이 보호하던 데이터를 b 보다 급한 ISR 이 만지면 다시 경쟁이 생긴다(ARM-M10 에서 모터 ISR 이 같은 카운터를 올려 50/50 lost). (2) BASEPRI 0 은 "마스크 없음" 이라 그룹 0 은 BASEPRI 로 못 막는다. (3) M0/M0+ 에는 BASEPRI 가 없다. 꼬리: "FreeRTOS 의 configMAX_SYSCALL_INTERRUPT_PRIORITY 는?" → 이 b. 그보다 급한 ISR 은 커널 API 를 못 부르는 대신 커널 섹션에 안 막힌다.

</details>

<details><summary>Q4. tail-chaining 은 최악 지연을 얼마나 줄이나요?</summary>

평균은 크게, 최악은 상황에 따라. 연속 처리에서 복귀 12 + 진입 12 = 24 를 6 으로 바꾸니 job 당 18 (BM-A10 연습 5). 하지만 이 레슨의 상한식은 O = 24 로 보수적이라 꼬리 물기를 **안 센다** — 그래서 '꼬리 물기 없는 코어' 에선 상한이 정확히 맞는다(구성 경우 등호 66 = 66). 실제 코어의 시뮬 최대는 53: 18 이 아니라 13 만 준다, late arrival 이 낮은 쪽 스태킹을 빼앗는 위상이 있어서. 모터 세트에서 꼬리 물기를 끄면(V1) CUR 시뮬 최악 +52, ENC 상한은 계단을 넘어 7884 → 12284. 꼬리: "그럼 해석에 꼬리 물기를 넣으려면?" → 중첩 선점(진입+복귀)과 연쇄(꼬리) 횟수를 따로 세야 한다. 안전하게 하기 어렵고 이득은 작다.

</details>

<details><summary>Q5. CPU 이용률이 57 % 인데 왜 데드라인을 놓치죠?</summary>

이용률은 "평균적으로 일이 들어가나" 이고, 데드라인은 "가장 나쁜 순간에 줄이 얼마나 길어지나" 다. 베이스라인은 U = 569 ‰ 인데 CUR 의 창 안에 섹션 3359 + CAN 2588 + 자기 진입이 몰린다. 이용률은 블로킹·우선순위 순서·같은 그룹을 모른다. U > 100 % 면 반드시 실패(연습 2: CAN 을 4 배 자주 → 발산), U < 100 % 는 아무것도 보장하지 않는다(고정 우선순위에선 Liu-Layland 경계 같은 충분조건도 블로킹 없이만 성립 — RTOS-M11).

</details>

<details><summary>Q6. 긴 ISR 을 top/bottom half 로 나누면 무엇이 좋아지고 무엇이 나빠지나요?</summary>

좋아지는 것: 그 ISR 의 C 가 다른 IRQ 의 간섭/블로킹 항에서 336 으로 줄어든다(V6: CUR 45.6 → 32.6 µs, CAN top 35 → 22 µs). 나빠지는 것: (1) bottom half 의 응답은 가장 낮은 우선순위라 길어진다(64.7 µs, V6 에선 T 초과). (2) 예외가 하나 더(PendSV 진입·복귀 O) + 넘기는 코드(모델 34 사이클). (3) 데이터 넘기기가 새 공유 자원이 된다 — 링 버퍼면 SPSC 락 없이(DS 트랙), 아니면 또 섹션. (4) 모델에선 PendSV 를 CAN 과 같은 T 의 독립 IRQ 로 봤다 — 실제로는 top half 끝에서 pend 하므로 릴리즈 지터가 있다(RTOS-A04 의 J 항). PendSV 를 가장 낮게 두는 이유는 그 아래에 아무도 없어야 bottom half 가 다른 ISR 의 블로킹이 되지 않기 때문.

</details>

<details><summary>Q7. (스태프) 이 분석이 틀리는 경우는? 보드에서 놓치면 어디부터 의심하나요?</summary>

모델 밖 목록: (1) 코어 상수 — 복귀 12, late 6, FP 워드당 1, miss 당 WS 는 가정(BM-A10 측정으로 고쳐야). (2) C 가 진짜 WCET 인가 — 측정 최대는 하한(RTOS-A03). 플래시 캐시 hit/miss 쌍봉. (3) 버스 경합: DMA 가 같은 SRAM/플래시를 쓰면 스태킹·페치가 늘어난다. (4) 다중 사이클 명령: LDM/STM 은 중단·재개(ICI), 나눗셈은 버리고 재시작한다고 알려져 있지만(확인 권장) 다른 코어/명령은 끝까지 갈 수 있다. (5) ISR 안의 크리티컬 섹션·BASEPRI 올리기, ISR 이 FP 를 쓰는 lazy 멈춤 — 이 모델은 스레드 섹션과 "ISR 은 FP 안 씀" 만 다룬다. (6) NMI·HardFault·디버거. (7) 늦은 플래그 클리어로 같은 IRQ 재진입(BM-M01). (8) M7: 캐시·분기 예측·AXI 로 "12" 자체가 흔들림. 의심 순서는 "가장 큰 항부터": 섹션 길이를 실측(GPIO 토글/DWT), 같은 그룹 배치 재확인, 그다음 C.

</details>

<details><summary>Q8. (스태프) 전류 루프의 출력 지터를 거의 0 으로 만들라면 어떻게 바꾸겠나요?</summary>

1) 해석상 지터 = R_max − R_min. V0 는 5970 사이클(35.5 µs), V9 는 6 사이클 = 진입 12 와 꼬리 6 의 차(연습 4, 시뮬 [1696, 1702] 확인). 즉 소프트웨어로 갈 수 있는 바닥은 "가장 급한 그룹에 혼자 + 아무도 안 막음 + 고정 비용(FP 금지 또는 LSPEN 고정, ISR/벡터 0 WS 메모리)". 2) 남는 6 사이클(+ 진행 중 명령·버스 경합)도 없애려면 **소프트웨어를 지터 경로에서 빼라**: 타이머 비교 출력/PWM 섀도 레지스터가 갱신 시점을 하드웨어로 정하고, ISR 은 다음 주기 값을 미리 계산(BM-M05). ADC 도 타이머 트리거 + DMA. 3) 꼬리: "그래도 ISR 이 늦으면?" → 섀도 값이 한 주기 늦게 적용될 뿐 펄스 타이밍은 안 흔들린다 — 데드라인을 "다음 업데이트 이벤트 전까지" 로 다시 정의한다.

</details>

<details><summary>Q9. 펜딩 비트가 하나라는 게 분석에 왜 중요하죠?</summary>

펜딩 중에 같은 IRQ 가 또 오면 합쳐진다(카운트 안 됨). 시뮬 실측: 바쁜 ISR 이 200 사이클 도는 동안 T = 30 짜리가 3 번 도착 → 1 번 실행, 2 번 놓침. 그래서 해석의 "산발 job" 모델은 R ≤ T (또는 L ≤ T, 데이터 레지스터 읽기 기준)일 때만 성립하고, `sys_ok` 가 둘 다 확인한다. 놓침이 데이터 손실이 되는 곳: FIFO 없는 UART(연습 3: 베이스라인은 115200 까지, 230400 에선 시뮬이 overrun 위상을 찾음 — L 7796 + 읽기 20 > 7291), 엔코더 펄스를 IRQ 로 세는 설계(그래서 타이머 HW 카운트). 꼬리: "ISR 이 플래그를 여러 개 처리하면?" → 상태 레지스터를 다 읽어 비우는 루프(CAN FIFO 를 0 될 때까지)로 "합쳐짐" 을 견디게 짠다.

</details>

<details><summary>Q10. Cortex-M0 이나 M7 이면 이 분석에서 무엇이 바뀌나요?</summary>

M0/M0+: BASEPRI 가 없어 섹션은 PRIMASK 뿐(모든 IRQ 의 B 에 들어감), 우선순위 2 비트(4 단계), PRIGROUP 없음(서브 없음), 진입이 16/15 사이클로 알려져 있음(ARM 자료 기억 — 확인 권장). 해석식은 같고 파라미터와 B 가 커진다. M7: 진입 12 는 같다고 소개되지만 캐시·TCM·AXI·분기 예측 때문에 ISR 의 C 와 페치 비용의 편차가 크다 → ISR/벡터를 ITCM 에, 데이터를 DTCM 에 두어 모델의 fetch 를 0 으로 "만드는" 것이 실무 처방. 또 M7 의 쓰기 버퍼 때문에 플래그 클리어 후 DSB 가 더 중요하다(BM-M01).

</details>

## 9. 추가 연습

`practice.c` 는 `solution.c` 를 포함해(main 만 이름 바꿈) 아래 네 답을 검사한다.

### 연습 1 — 구현 우선순위 비트: 3 비트·2 비트 칩으로 옮기면?

QEMU mps2-an385 는 8 비트를 다 구현하지만 실제 AN385 M3 는 3 비트, STM32F4 는 4 비트다(ARM-M10, BM-M02). V9 설계의 바이트를 `prio_implemented(byte, bits)` (아래 비트 RAZ) 로 바꿔 다시 분석하라.

<details><summary>답</summary>

`byte & (0xFF << (8 − bits))`. 실제 출력:

```text
   8 bits: prio 10 20 60 40 60 F0 | R(us) 10.13 11.22 18.38 16.18 20.97 34.37 | sys_ok=1, CUR/ENC same group=0, B(CUR)=0
   3 bits: prio 00 20 60 40 60 E0 | R(us) 10.13 11.22 18.38 16.18 20.97 34.37 | sys_ok=1, CUR/ENC same group=0, B(CUR)=0
   2 bits: prio 00 00 40 40 40 C0 | R(us) 11.14 11.22 16.99 18.38 20.97 34.37 | sys_ok=1, CUR/ENC same group=1, B(CUR)=171
```

3 비트는 그대로. 2 비트에선 CUR 와 ENC 가 같은 바이트 0x00 → 같은 그룹 → ENC 가 CUR 의 블로킹(21 + 150 = 171)이 되고, CAN·UART·TICK 이 한 그룹으로 뭉쳐 UART 가 16.2 → 18.4 µs (번호가 작은 CAN 이 앞 순번). 여전히 통과하지만 "QEMU 에서 맞던 설계가 실칩에서 달라지는" 정확한 지점이다. 시뮬도 세 경우 모두 상한 이하.

</details>

### 연습 2 — 이용률 검사 vs 응답 시간 해석

`util_permille()` = Σ(Cj + O)/Tj 를 천분율로. V0 와 V9 를 비교하고, U 가 100 % 를 넘는 세트도 하나 만들어라.

<details><summary>답</summary>

`V0 = 569 permille (sys_ok=0), V9 = 565 permille (sys_ok=1)` — 이용률은 거의 같은데 하나는 놓치고 하나는 지킨다. 차이는 전부 B 와 순서. CAN 의 T 를 2000 으로(4 배 자주) 하면 U > 1000 ‰ 이고 해석이 발산(-1). 이용률은 "필요조건" 테스트로만 쓴다.

</details>

### 연습 3 — FIFO 없는 UART 의 최대 보레이트

8N1 한 글자 = 10 비트 = `10 · 168e6 / baud` 사이클. 다음 글자가 다 들어오기 전에 DR 을 읽어야 한다: `L_uart + read_off(20, 가정) ≤ T_char` 그리고 `R_uart ≤ T_char`. 표준 보레이트 중 최대를 구하고, 넘는 보레이트에서 시뮬이 overrun 위상을 찾는지 보라.

<details><summary>답</summary>

```text
== ex3: UART L bound V0 = 12288 cyc -> max 115200 bps; V9 = 2519 cyc -> max 460800 bps (char = 10 bits)
   V0 @230400: char = 7291 cyc, sim max UART latency = 7796 (+20 read) -> OVERRUN witnessed
```

UART 의 한계는 R 이 아니라 **L(+읽기 위치)** 이다 — ISR 이 시작해 DR 을 읽는 순간이 중요하니까. 베이스라인에선 앞 순번이 넷(CUR, ENC, CAN + 로거 섹션)이라 L 이 73 µs. V9 는 15 µs 라 460800 까지. 하드웨어 FIFO/DMA 가 있으면 데드라인이 "FIFO 깊이 × 글자 시간" 으로 늘어난다 — 그래서 실무 답은 대개 DMA + idle line 인터럽트.

</details>

### 연습 4 — 출력 지터

CUR 끝에서 PWM 듀티를 쓴다면 출력 지터 = R_max − R_min. R_min = (다른 ISR 에서 꼬리 물기로 들어오는 최선) `tail + fetch + C`. V0 와 V9 를 비교하고 시뮬의 관측 범위가 [R_min, R_max] 안인지 확인하라.

<details><summary>답</summary>

```text
== ex4: V0 baseline            CUR R in [1696, 7666] -> jitter bound 5970 cyc = 35.53 us; sim saw [1696, 7614] = 35.22 us
== ex4: V9 = V3+V4+V5+V6+V7    CUR R in [1696, 1702] -> jitter bound 6 cyc = 0.03 us; sim saw [1696, 1702] = 0.03 us
```

V9 의 지터 상한 6 = 진입 12 − 꼬리 6. 시뮬이 양 끝을 둘 다 만났다(1696 = 꼬리 물기로 시작, 1702 = 스레드에서 진입). 이게 소프트웨어로 갈 수 있는 바닥이고, 그 아래는 하드웨어 섀도 레지스터의 몫(Q8).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/ARM-A08_pr && /tmp/ARM-A08_pr      # 14/14 passed
# 스윕 데모 (레슨 디렉터리에서 — solution.c 를 #include):
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o /tmp/ARM-A08_demo && /tmp/ARM-A08_demo
```

`practice.c` 실행 끝 줄: `14/14 passed` (ASan/UBSan 빌드도 14/14). `demo_sweep.c` (1) 의 실제 출력 일부:

```text
== (1) V5 CAN sub-prio 0x00 -> group 0x60: LOG PRIMASK length -> CUR response (D = 4200)
   len  | R bound | R sim (crit+600 phasings) | verdict
      0 |    1719 |  1719                  | meets
   2400 |    4118 |  4118                  | meets
   2800 |    4518 |  4518                  | MISSES
   4000 |    5718 |  5718                  | MISSES
   section_budget(PRIMASK) without LOG = 2482 cycles
```

<!-- code: demo_sweep.c -->

## 10. 핵심 정리

- 최악 지연 `L = E + B + Σ⌈L/Tj⌉(Cj + O)`, 응답 `R = E + B + Ci + Σ같은그룹앞⌈L/Tj⌉(…) + Σ더급한그룹⌈R/Tj⌉(…)`. 12 사이클은 E 의 일부일 뿐이다.
- 세 집합: **앞 순번**(바이트·번호가 앞, L 에서 간섭) / **선점자**(그룹이 더 급함, R 에서 간섭) / **뒤 순번**(블로킹, 최대 하나).
- B = max(i 를 막는 섹션 len − 1, 같은 그룹 뒤 순번 E − 1 + C). 서브 우선순위는 선점이 아니라 블로킹을 만든다.
- BASEPRI 는 **그룹** 값으로 비교, 0 = 마스크 없음. PRIMASK → BASEPRI 는 위쪽 IRQ 의 B 를 지우지만 공유 데이터 설계가 맞아야 한다.
- 상한(해석)은 판정용, 시뮬·측정은 하한. 상한은 계단(ceil) 때문에 비관 구간이 있고, late arrival 때문에 "동시 도착" 이 최악이 아닐 수 있다.
- 최적화는 항별로: 섹션 → 단축/BASEPRI, 같은 그룹 → 그룹 분리, 긴 ISR → top/bottom, 코어 기능(꼬리 물기·lazy FP·0 WS)은 수십 사이클. 늘 잃는 쪽을 함께 본다.
- 펜딩 비트 1 개 → R ≤ T (UART 는 L + 읽기 ≤ 글자 시간)를 꼭 확인.
- 모터 세트 결론: V0 CUR 45.6 µs (놓침) → V9 10.1 µs, 지터 6 사이클, 추가 PRIMASK 섹션 예산 2499 사이클.

## 참고자료

- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403E) — B1.5.4 예외 우선순위·PRIGROUP·ExecutionPriority 의사코드(BASEPRI 의 그룹 값), B1.5.x late arrival·tail-chaining, B3.2.6 AIRCR, B5 MRS/MSR BASEPRI_MAX. 절 번호는 기억 — 확인 권장.
- Arm, *Cortex-M3 / Cortex-M4 Technical Reference Manual* (DDI 0337 / DDI 0439) — 0 WS 진입 12 사이클, tail-chaining 6 사이클, 인터럽트 가능한 다중 사이클 명령. 확인 권장.
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.) — 7–8 장 NVIC 우선순위 그룹, late arrival, pop pre-emption, 인터럽트 지연.
- Arm Application Note 298, *Cortex-M4(F) Lazy Stacking and Context Switching* — LSPEN/ASPEN, 확장 프레임.
- N. Audsley, A. Burns et al., "Applying new scheduling theory to static priority pre-emptive scheduling" (1993) — 응답 시간 해석(고정점)의 원전; Buttazzo, *Hard Real-Time Computing Systems* — 블로킹 항, 임계 순간.
- FreeRTOS, "Running the RTOS on a ARM Cortex-M Core" — `configMAX_SYSCALL_INTERRUPT_PRIORITY` 와 BASEPRI: https://www.freertos.org/RTOS-Cortex-M3-M4.html
- ST RM0090 (STM32F405/407) — FLASH_ACR 5 WS @168 MHz, USART(FIFO 없음, ORE), bxCAN FIFO 3 단. 확인 권장.
- 관련 레슨: BM-A10 (예외 하나의 사이클 모델·DWT 측정 — 같은 파라미터), ARM-M10 (PRIMASK/BASEPRI 실측, BASEPRI_MAX), BM-M02 (PRIGROUP·우선순위 설계를 QEMU 로 확인), BM-M01 (늦은 플래그 클리어), BM-M05 (PWM 섀도 레지스터), RTOS-M11 (RM·기본 RTA), RTOS-A04 (태스크 쪽 확장 RTA: B·J·ISR 부하), RTOS-A03 (WCET 측정·지터), ARM-M09 (예외 프레임·EXC_RETURN). 다음 단계: ARM-A01 (PendSV 문맥 전환).
