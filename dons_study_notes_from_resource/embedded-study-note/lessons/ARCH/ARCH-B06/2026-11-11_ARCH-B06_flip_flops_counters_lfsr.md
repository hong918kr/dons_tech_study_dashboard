---
id: ARCH-B06
time: 70분
updated: 2026-11-11
---
# ARCH-B06 순차 논리: 플립플롭·카운터·LFSR

## 학습 목표

- D 플립플롭(edge-triggered)과 D 래치(level-sensitive)의 차이를 파형으로 설명하고, 클럭 루프 시뮬레이터에서 **"모든 D를 옛 Q로 계산 → 에지에서 한꺼번에 반영"** 하는 2단계(two-phase) 갱신을 구현한다.
- 같은 회로를 in-place(계산하자마자 덮어쓰기)로 돌리면 왜 틀리는지 — swap 회로, 시프트 레지스터, LFSR에서 — 실제로 재현하고, Verilog blocking `=` / non-blocking `<=` 와 연결한다.
- 4비트 동기 카운터를 XOR/AND 토글 식(`d_i = q_i ⊕ EN·q_0⋯q_{i-1}`)으로 만들고, 터미널 카운트(TC)로 8비트로 이어 붙인다.
- 16비트 최대 길이 LFSR(x^16+x^14+x^13+x^11+1)을 Galois·Fibonacci 두 형태로 만들고, **65536개 상태 전수 조사**로 주기 65535 + 잠김 상태 0 을 확인한다. 비최대 다항식(x^16+1, CRC-16-CCITT)은 주기가 짧다는 것도 숫자로 본다.
- PRBS 링크 테스트의 자기동기(self-synchronizing) 검사기를 만들고, "비트 오류 1개 → 오류 카운트 = 다항식 항 수" 를 테스트로 확인한다.

## 1. 문제

카운터·시프트 레지스터·LFSR은 SoC 어디에나 있다 — 타이머(PSC/ARR), SPI 시프트 레지스터, SerDes PRBS 패턴 생성기·검사기, CRC 엔진, NAND 컨트롤러의 데이터 랜덤라이저(scrambler). 면접에서는 "LFSR 주기가 왜 2^n−1 이죠?", "시뮬레이터에서 레지스터를 어떤 순서로 갱신하나요?", "blocking 과 non-blocking 차이?" 로 바로 물어보고, 실리콘 검증에서는 PRBS 오류 카운트를 해석하는 일이 실제 업무다.

이 레슨의 모델은 하나다. 플립플롭 n개의 출력 `q[0..n-1]`(각 0/1)이 있고, 플립플롭 i의 D 입력은 조합 논리 함수 `dfn(q, i, in)` 이 정한다. **클럭 에지 한 번 = ① 모든 D를 에지 직전의 q로 계산 ② 한꺼번에 q ← D.** 실제 하드웨어에서는 모든 플립플롭이 같은 에지에 동시에 샘플링하니까 저절로 이렇게 된다. 소프트웨어는 순서대로 실행되므로 일부러 이렇게 만들어야 한다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">D 래치 vs D 플립플롭 — 같은 clk, 같은 D (Part A 테스트 순서 그대로)</text>
  <rect x="320" y="30" width="100" height="205" class="f-warn" opacity=".25"/>
  <g fill="currentColor" font-size="12">
    <text x="10" y="58">clk</text><text x="10" y="112">D</text>
    <text x="10" y="166">래치 Q (en=clk)</text><text x="10" y="220">FF Q (상승 에지)</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="2">
    <polyline points="120,64 220,64 220,40 420,40 420,64 520,64 520,40 620,40"/>
    <polyline points="120,94 320,94 320,118 420,118 420,94 520,94 520,118 620,118"/>
    <polyline points="120,172 220,172 220,148 320,148 320,172 620,172"/>
    <polyline points="120,226 220,226 220,202 520,202 520,226 620,226"/>
  </g>
  <g stroke="currentColor" stroke-dasharray="4 3">
    <line x1="220" y1="30" x2="220" y2="240"/><line x1="520" y1="30" x2="520" y2="240"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="220" y="252">↑ 에지 1: 둘 다 D=1 을 잡음</text>
    <text x="520" y="252">↑ 에지 2: FF 가 D=0 을 잡음</text>
    <text x="370" y="138">D 변화</text>
  </g>
  <text x="10" y="278" fill="currentColor">음영 구간: clk=1 인 채로 D 가 0 이 되자 래치는 따라가고(투명), FF 는 에지 때 값 1 을 지킨다.</text>
  <text x="10" y="296" fill="currentColor">하강 에지에서 FF 는 아무 일도 하지 않는다 — "에지 트리거" = 값이 바뀌는 순간은 상승 에지뿐.</text>
</svg>
```

### Part A — D 플립플롭, 2-phase 클럭 엔진, 4비트 동기 카운터

- `dff_eval(ff, clk, d)` — 상승 에지(prev_clk 0 → clk 1)에서만 `q ← d`. 언제나 `prev_clk ← clk`. (래치 `dlatch_eval` 은 비교용으로 주어진다.)
- `tick(q, n, dfn, in)` — 2-phase 엔진. 먼저 `d[i] = dfn(q, i, in)` 을 전부 계산하고, 그 다음 `q[i] = d[i]` 를 한꺼번에.
- `counter4_d(q, i, in)` — 4비트 동기 업 카운터의 비트 i D 입력. `in` 비트0 = EN, 비트1 = CLR(동기 클리어, EN보다 우선). 덧셈(`+`) 없이 토글 식으로.
- `counter4_tc(q, in)` — 터미널 카운트 TC = EN·q3·q2·q1·q0. 다음 단의 EN으로 써서 8비트 카운터를 만든다.
- 테스트: 20 클럭 카운트, **진리표 전수(16 상태 × 4 입력)**, TC, 300 클럭 후 8비트 cascade 값 = 300 mod 256.

### Part B — 같은 회로, 다른 결과: in-place 갱신 레이스

- 주어진 `tick_in_place` (i 오름차순으로 계산 즉시 덮어씀) / `tick_in_place_rev` (내림차순) 를 정답 엔진과 비교한다.
- `swap_d` (주어짐, d0 = q1, d1 = q0): 2-phase 에서는 01 → 10 → 01. in-place 는 순서에 따라 00 또는 11 — **어떤 순서도 정답이 아니다.**
- `shift8_d(q, i, in)` — 8비트 SIPO(직렬 입력·병렬 출력) 시프트 레지스터, 왼쪽으로 민다: `d[0] = in&1`, `d[i] = q[i-1]`. in-place 오름차순은 한 클럭에 직렬 입력이 8칸을 다 통과한다. 내림차순은 순수 시프트 체인에서는 *우연히* 맞는다.
- `sipo_receive(byte)` — SPI 수신기처럼 MSB부터 8 클럭 넣고 병렬 값을 돌려준다.

### Part C — 16비트 LFSR: 최대 주기 전수 검증 + PRBS 검사기

- `galois_step(s, mask)` — Galois(내부 XOR) 오른쪽 시프트. mask 0xB400 = x^16+x^14+x^13+x^11+1.
- `fib_step(s, taps)` — Fibonacci(외부 XOR) 오른쪽 시프트. 같은 다항식이 taps = 0x002D (비트 0,2,3,5). 패리티는 내장 함수 없이.
- `lfsr_period(step, param, seed, limit)` — seed로 돌아올 때까지 스텝 수 (limit 초과 시 0).
- `lfsr_census(step, param)` — **65536 상태 전수**: 사이클 수 / 최장 사이클 / 고정점 수 / 순열(permutation)인지.
- `fib16_d(q, i, in)` — 같은 Fibonacci LFSR을 플립플롭 16개로 → Part A 엔진으로 돌려 워드 모델과 1000 스텝 일치, in-place 엔진은 첫 에지부터 틀림.
- `prbs16_check(rx, n)` — 자기동기 PRBS 검사기. 깨끗한 열 → 0, 단일 비트 오류 → 5, 오류 2개 → 10, 인접 오류 2개 → 8 (상쇄), 중간부터 수신해도 16비트 후 lock.

## 2. Problem (English)

> **Clocked sequential logic in software.**
>
> A circuit has `n` D flip-flops with outputs `q[0..n-1]` and per-flop next-state functions `d_i = f(q, i, in)`.
>
> 1. Implement `tick(q, n, f, in)` so that one call models one rising clock edge: every flip-flop samples its D input simultaneously. Also implement an edge-triggered `dff_eval(ff, clk, d)`.
> 2. Using only AND/XOR on bits (no `+`), write the next-state function of a 4-bit synchronous up counter with enable and synchronous clear, plus its terminal-count output, and cascade two of them into an 8-bit counter.
> 3. Show (with tests) that updating flip-flops in place, in any fixed order, breaks a two-flop swap circuit and a 16-bit LFSR, and explain why a pure shift chain happens to work in reverse order.
> 4. Implement a 16-bit maximal-length LFSR for x^16+x^14+x^13+x^11+1 in both Galois (mask `0xB400`) and Fibonacci (taps `0x002D`) form. Prove by exhaustive search over all 65536 states that the nonzero states form a single cycle of length 65535 and 0 is a fixed point. Show that x^16+1 and the CRC-16-CCITT polynomial are not maximal.
> 5. Write a self-synchronizing PRBS checker for the Fibonacci output stream. How many error counts does a single bit error cause, and why?
>
> Example: `galois_step(0xACE1, 0xB400) == 0xE270`, `fib_step(0xACE1, 0x002D) == 0x5670`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
# FAIL 줄들 + "12/41 passed", exit 1 — TODO 12개(A1–A4, B1–B2, C1–C6)를 채워 41/41
```

## 4. 힌트

<details>
<summary>힌트 1 — tick 은 지역 배열 하나면 된다</summary>

`uint8_t d[MAXFF];` 에 모든 i의 `dfn(q, i, in)` 을 먼저 담는다. 이 루프 동안 `q` 를 건드리지 않는 것이 핵심이다. 두 번째 루프(또는 `memcpy`)에서 `q` 로 옮긴다. 이것이 "조합 논리 평가 → 클럭 에지" 두 단계다.

</details>

<details>
<summary>힌트 2 — 카운터 비트 i 는 "아래 비트가 전부 1일 때" 뒤집힌다</summary>

0111 + 1 = 1000: 비트 3은 아래 3비트가 모두 1이라 뒤집히고, 아래 비트들도 각각 자기 아래가 모두 1이라 뒤집힌다. 그래서 `t = EN; for k < i: t &= q[k]; d_i = q[i] ^ t`. 이건 B04의 반가산기 체인(carry = AND, sum = XOR)과 같은 구조다 (ARCH-B04 참고).

</details>

<details>
<summary>힌트 3 — Galois 는 "나가는 비트가 1이면 mask XOR"</summary>

`out = s & 1; s >>= 1; if (out) s ^= mask;`. mask의 비트 15가 x^16 항이라 out 이 맨 위로 다시 들어가고, 나머지 1인 비트 자리에서 지나가는 비트에 out 이 섞인다.

</details>

<details>
<summary>힌트 4 — Fibonacci 는 "탭들의 패리티가 맨 위로 들어간다"</summary>

`x = s & taps` 의 1 개수 홀짝이 새 비트. `while (x) { fb ^= x & 1; x >>= 1; }`. 그 다음 `(s >> 1) | (fb << 15)`. 탭 16·14·13·11 ↔ 비트 0·2·3·5 (오른쪽 시프트에서 탭 k ↔ 비트 16−k).

</details>

<details>
<summary>힌트 5 — census 는 방문 표시 + "돌아왔나?"</summary>

`g_seen[65536]` 을 0으로 지우고, 아직 안 본 s0 에서 걷기 시작해 s0 로 돌아올 때까지 표시하며 센다. 걷는 도중 **이미 표시된 상태**를 밟으면(출발점이 아니면) 두 상태가 같은 다음 상태로 가는 것 → 순열이 아니다 → 바로 반환 (무한 루프 방지).

</details>

<details>
<summary>힌트 6 — PRBS 검사기는 송신기의 점화식을 수신 비트로 다시 계산한다</summary>

Fibonacci 출력 b[k] 는 b[k+16] = b[k] ⊕ b[k+2] ⊕ b[k+3] ⊕ b[k+5] 를 만족한다. 수신기는 자기가 받은 비트로 예측 `rx[k-16]^rx[k-14]^rx[k-13]^rx[k-11]` 을 만들어 `rx[k]` 와 비교한다. 송신기 상태(seed)를 몰라도 된다 — 그래서 "자기동기".

</details>

## 5. 풀이

### Part A — 플립플롭은 "에지 직전 값"을 잡는다

`dff_eval` 은 이전 clk 를 기억해 0→1 일 때만 D를 받는다. 래치는 en=1 동안 D가 그대로 흐른다(투명). 위 그림의 음영 구간이 차이다. ASIC/FPGA 설계에서 래치를 피하는 이유도 여기 있다: 투명한 동안 경로가 다음 단까지 이어져 타이밍 분석이 복잡해진다(time borrowing). 셋업/홀드와 Fmax 계산은 ARCH-B13 이 맡는다.

**2-phase 엔진**:

```c
for (i = 0; i < n; i++) d[i] = dfn(q, i, in) & 1;   /* ① 조합 논리: 모두 옛 q 로 */
for (i = 0; i < n; i++) q[i] = d[i];                 /* ② 에지: 한꺼번에 */
```

**4비트 동기 카운터**: 모든 플립플롭이 같은 clk 에 묶여 있고(동기), 각 비트는 "아래 비트가 모두 1이고 EN" 일 때 뒤집힌다.

```svg
<svg viewBox="0 0 740 285" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">4비트 동기 업 카운터: d_i = q_i ⊕ (EN·q_0⋯q_(i-1)), TC = EN·q0·q1·q2·q3</text>
  <text x="10" y="54" fill="currentColor" font-size="12">EN</text>
  <g stroke="currentColor" fill="none">
    <line x1="30" y1="60" x2="160" y2="60"/><line x1="184" y1="60" x2="340" y2="60"/>
    <line x1="364" y1="60" x2="520" y2="60"/><line x1="544" y1="60" x2="700" y2="60"/>
    <line x1="724" y1="60" x2="736" y2="60"/>
    <rect x="160" y="48" width="24" height="24"/><rect x="340" y="48" width="24" height="24"/>
    <rect x="520" y="48" width="24" height="24"/><rect x="700" y="48" width="24" height="24"/>
    <line x1="50" y1="60" x2="50" y2="116"/><line x1="230" y1="60" x2="230" y2="116"/>
    <line x1="410" y1="60" x2="410" y2="116"/><line x1="590" y1="60" x2="590" y2="116"/>
    <circle cx="50" cy="130" r="14"/><circle cx="230" cy="130" r="14"/>
    <circle cx="410" cy="130" r="14"/><circle cx="590" cy="130" r="14"/>
    <line x1="64" y1="130" x2="80" y2="130"/><line x1="244" y1="130" x2="260" y2="130"/>
    <line x1="424" y1="130" x2="440" y2="130"/><line x1="604" y1="130" x2="620" y2="130"/>
    <rect x="80" y="105" width="70" height="60"/><rect x="260" y="105" width="70" height="60"/>
    <rect x="440" y="105" width="70" height="60"/><rect x="620" y="105" width="70" height="60"/>
    <line x1="150" y1="135" x2="172" y2="135"/><line x1="172" y1="135" x2="172" y2="72"/>
    <line x1="330" y1="135" x2="352" y2="135"/><line x1="352" y1="135" x2="352" y2="72"/>
    <line x1="510" y1="135" x2="532" y2="135"/><line x1="532" y1="135" x2="532" y2="72"/>
    <line x1="690" y1="135" x2="712" y2="135"/><line x1="712" y1="135" x2="712" y2="72"/>
    <polyline points="162,135 162,185 50,185 50,144"/><polyline points="342,135 342,185 230,185 230,144"/>
    <polyline points="522,135 522,185 410,185 410,144"/><polyline points="702,135 702,185 590,185 590,144"/>
    <polyline points="108,165 115,157 122,165"/><polyline points="288,165 295,157 302,165"/>
    <polyline points="468,165 475,157 482,165"/><polyline points="648,165 655,157 662,165"/>
    <line x1="20" y1="210" x2="655" y2="210"/>
    <line x1="115" y1="165" x2="115" y2="210"/><line x1="295" y1="165" x2="295" y2="210"/>
    <line x1="475" y1="165" x2="475" y2="210"/><line x1="655" y1="165" x2="655" y2="210"/>
  </g>
  <g fill="currentColor">
    <circle cx="50" cy="60" r="3"/><circle cx="230" cy="60" r="3"/><circle cx="410" cy="60" r="3"/><circle cx="590" cy="60" r="3"/>
    <circle cx="162" cy="135" r="3"/><circle cx="342" cy="135" r="3"/><circle cx="522" cy="135" r="3"/><circle cx="702" cy="135" r="3"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="172" y="64">&amp;</text><text x="352" y="64">&amp;</text><text x="532" y="64">&amp;</text><text x="712" y="64">&amp;</text>
    <text x="712" y="40">TC</text>
    <text x="50" y="135">⊕</text><text x="230" y="135">⊕</text><text x="410" y="135">⊕</text><text x="590" y="135">⊕</text>
    <text x="115" y="122">FF0</text><text x="295" y="122">FF1</text><text x="475" y="122">FF2</text><text x="655" y="122">FF3</text>
    <text x="90" y="142">D</text><text x="140" y="142">Q</text><text x="270" y="142">D</text><text x="320" y="142">Q</text>
    <text x="450" y="142">D</text><text x="500" y="142">Q</text><text x="630" y="142">D</text><text x="680" y="142">Q</text>
    <text x="160" y="128">q0</text><text x="340" y="128">q1</text><text x="520" y="128">q2</text><text x="700" y="128">q3</text>
  </g>
  <text x="10" y="228" fill="currentColor" font-size="12">clk (모든 FF 공통)</text>
  <text x="10" y="252" fill="currentColor" font-size="12">AND 체인이 "아래 비트 전부 1" 을 만들고 XOR 가 토글한다.</text>
  <text x="10" y="272" fill="currentColor" font-size="12">CLR(동기 클리어)은 그림에서 생략 — D 앞에서 0 을 강제하는 AND 하나.</text>
</svg>
```

진리표 전수 테스트(16 상태 × {EN, CLR} 4조합 = 64 행)로 덧셈 기준 `(v+1)&15` 와 비교한다. TC는 TC = EN·q3q2q1q0 이고, 8비트 cascade 는 상위 카운터의 EN = 하위의 TC. 여기서도 2-phase 가 시스템 수준에서 필요하다: **in_hi 는 하위 카운터가 tick 하기 전에 계산**해야 한다. tick 후에 계산하면 하위가 15가 되는 그 에지에 상위가 같이 올라가서 한 사이클 일찍 증가한다 (7. 자주 하는 실수).

| 카운터 종류 | 상태 수 | 특징 | 쓰임 |
|---|---|---|---|
| 동기 이진 (Part A) | 2^n | 모든 비트 같은 에지, 출력이 동시에 바뀜 | MCU 타이머 CNT, 주소 생성 |
| 리플 (비동기) | 2^n | 비트 i 의 clk = 비트 i−1 출력 → 중간 값(glitch)이 보인다 | 저전력 분주기, 면접 함정 (연습 3) |
| 링 (one-hot) | n | 디코더 불필요 | 상태기계 인코딩 |
| Johnson (꼬인 링) | 2n | 인접 상태 1비트 차이 | 위상 생성, glitch-free 디코드 (연습 1) |
| LFSR (Part C) | 2^n − 1 | XOR 몇 개뿐 → 가장 빠르고 작다 | PRBS, 스크램블러, CRC, 의사 난수 |

펌웨어 연결: STM32 타이머는 프리스케일러(PSC) → 카운터(CNT) → 자동 재적재 값(ARR) 비교 → 업데이트 이벤트 구조이고(연습 2가 그 뼈대), Cortex-M SysTick 은 24비트 다운 카운터 + 재적재 레지스터다. 둘 다 이 Part 의 동기 카운터 + TC 와 같은 생각이다.

### Part B — in-place 갱신은 "같은 에지에 두 번 클럭" 이다

`demo_race.c` 실제 출력 (같은 입력, 엔진만 다름):

```text
8-bit SIPO shift register, q[7..0]
edge sin | 2-phase   in-place↑  in-place↓
   1  1  | 00000001  11111111   00000001
   2  0  | 00000010  00000000   00000010
   3  1  | 00000101  11111111   00000101
   4  1  | 00001011  11111111   00001011
   5  0  | 00010110  00000000   00010110
   6  0  | 00101100  00000000   00101100
   7  0  | 01011000  00000000   01011000
   8  0  | 10110000  00000000   10110000
   9  1  | 01100001  11111111   01100001
  10  0  | 11000010  00000000   11000010

swap (d0=q1, d1=q0), start q1q0=01
edge 1: 2-phase 10  in-place↑ 00  in-place↓ 11
edge 2: 2-phase 01  in-place↑ 00  in-place↓ 11
edge 3: 2-phase 10  in-place↑ 00  in-place↓ 11

16-bit Fibonacci LFSR (taps 16,14,13,11), seed 0xACE1
edge 1: 2-phase 0x5670  in-place↑ 0xD670  in-place↓ 0x0000
edge 2: 2-phase 0xAB38  in-place↑ 0x6B38  in-place↓ 0x0000
edge 3: 2-phase 0x559C  in-place↑ 0x359C  in-place↓ 0x0000
edge 4: 2-phase 0x2ACE  in-place↑ 0x1ACE  in-place↓ 0x0000
```

읽는 법:

- **시프트 레지스터, 오름차순**: `q[0]=sin` 직후 `q[1]=q[0]` 이 *새* q[0]을 읽는다 → 직렬 입력이 한 에지에 8칸을 모두 통과(11111111). 8 클럭이 걸려야 할 지연이 0 클럭이 되었다.
- **시프트 레지스터, 내림차순**: `q[7]=q[6]` 부터 하면 아직 안 바뀐 값을 읽으므로 맞다. 의존 그래프(누가 누구를 읽나)가 체인(DAG)이면 "읽히는 쪽을 나중에" 갱신하는 순서가 존재한다.
- **swap / LFSR**: 의존 그래프에 **사이클**이 있다 (q0 ↔ q1, LFSR 은 q15 → … → q0 → 피드백 → q15). 사이클이 있으면 어떤 순서든 누군가는 이미 바뀐 값을 읽는다. LFSR 내림차순은 정답 피드백 비트를 q15에 쓴 뒤 그 비트를 15칸 아래로 복사해 0x0000 이 되고, 그 뒤로는 잠김 상태(lock-up)에 빠진다.

하드웨어 설계 언어에서 같은 실수가 바로 이것이다:

```verilog
always @(posedge clk) begin   // 틀림: blocking '=' 는 줄마다 즉시 갱신
  q1 = q0;
  q2 = q1;                    // q2 는 방금 바뀐 q1 (= q0) 을 받는다 → 시프트가 아니라 복사
end
always @(posedge clk) begin   // 맞음: non-blocking '<=' 는 우변을 모두 먼저 평가, 나중에 한꺼번에 갱신
  q1 <= q0;
  q2 <= q1;
end
```

non-blocking 대입은 "우변 평가 → 시간 스텝 끝에 일괄 갱신" 이라 이 레슨의 `tick()` 과 같은 의미다. 규칙: 순차(clocked) always 블록은 `<=`, 조합 always 블록은 `=`. 소프트웨어 모델(C 시뮬레이터, 펌웨어의 상태기계 시뮬레이션, 셀룰러 오토마타, 이중 버퍼 그래픽)도 같은 원리 — **읽는 버퍼와 쓰는 버퍼를 나눈다.**

### Part C — 16비트 LFSR: 왜 65535 인가

LFSR(linear-feedback shift register)은 시프트 레지스터 + XOR 피드백이다. 상태 갱신이 GF(2) 위의 선형 변환이므로 **0 → 0** (XOR만으로는 0에서 1을 만들 수 없다). 그래서 최대로 쓸 수 있는 상태는 0을 뺀 2^16 − 1 = 65535 개이고, 피드백 다항식이 **원시 다항식(primitive polynomial)** 일 때 0이 아닌 모든 상태가 한 사이클을 이룬다.

```svg
<svg viewBox="0 0 760 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">같은 다항식 x^16+x^14+x^13+x^11+1 의 두 가지 배선 (오른쪽 시프트, 비트 15 가 왼쪽)</text>
  <text x="10" y="44" fill="currentColor" font-size="12">Fibonacci (외부 XOR)</text>
  <g stroke="currentColor" fill="none">
    <rect x="60" y="52" width="640" height="28"/>
    <line x1="100" y1="52" x2="100" y2="80"/><line x1="140" y1="52" x2="140" y2="80"/><line x1="180" y1="52" x2="180" y2="80"/>
    <line x1="220" y1="52" x2="220" y2="80"/><line x1="260" y1="52" x2="260" y2="80"/><line x1="300" y1="52" x2="300" y2="80"/>
    <line x1="340" y1="52" x2="340" y2="80"/><line x1="380" y1="52" x2="380" y2="80"/><line x1="420" y1="52" x2="420" y2="80"/>
    <line x1="460" y1="52" x2="460" y2="80"/><line x1="500" y1="52" x2="500" y2="80"/><line x1="540" y1="52" x2="540" y2="80"/>
    <line x1="580" y1="52" x2="580" y2="80"/><line x1="620" y1="52" x2="620" y2="80"/><line x1="660" y1="52" x2="660" y2="80"/>
    <line x1="480" y1="80" x2="480" y2="112"/><line x1="560" y1="80" x2="560" y2="112"/>
    <line x1="600" y1="80" x2="600" y2="112"/><line x1="680" y1="80" x2="680" y2="112"/>
    <line x1="40" y1="112" x2="680" y2="112"/>
    <polyline points="40,112 40,66 60,66"/>
    <line x1="700" y1="66" x2="740" y2="66"/>
  </g>
  <rect x="460" y="52" width="40" height="28" class="f-acc" opacity=".5"/><rect x="540" y="52" width="40" height="28" class="f-acc" opacity=".5"/>
  <rect x="580" y="52" width="40" height="28" class="f-acc" opacity=".5"/><rect x="660" y="52" width="40" height="28" class="f-acc" opacity=".5"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="80" y="71">15</text><text x="120" y="71">14</text><text x="160" y="71">13</text><text x="200" y="71">12</text>
    <text x="240" y="71">11</text><text x="280" y="71">10</text><text x="320" y="71">9</text><text x="360" y="71">8</text>
    <text x="400" y="71">7</text><text x="440" y="71">6</text><text x="480" y="71">5</text><text x="520" y="71">4</text>
    <text x="560" y="71">3</text><text x="600" y="71">2</text><text x="640" y="71">1</text><text x="680" y="71">0</text>
    <text x="300" y="128">새 비트 = q0 ⊕ q2 ⊕ q3 ⊕ q5 (탭 4개를 한 번에 XOR) → 비트 15 로</text>
    <text x="728" y="58">out</text>
  </g>
  <text x="10" y="166" fill="currentColor" font-size="12">Galois (내부 XOR)</text>
  <g stroke="currentColor" fill="none">
    <rect x="60" y="174" width="640" height="28"/>
    <line x1="100" y1="174" x2="100" y2="202"/><line x1="140" y1="174" x2="140" y2="202"/><line x1="180" y1="174" x2="180" y2="202"/>
    <line x1="220" y1="174" x2="220" y2="202"/><line x1="260" y1="174" x2="260" y2="202"/><line x1="300" y1="174" x2="300" y2="202"/>
    <line x1="340" y1="174" x2="340" y2="202"/><line x1="380" y1="174" x2="380" y2="202"/><line x1="420" y1="174" x2="420" y2="202"/>
    <line x1="460" y1="174" x2="460" y2="202"/><line x1="500" y1="174" x2="500" y2="202"/><line x1="540" y1="174" x2="540" y2="202"/>
    <line x1="580" y1="174" x2="580" y2="202"/><line x1="620" y1="174" x2="620" y2="202"/><line x1="660" y1="174" x2="660" y2="202"/>
    <polyline points="700,188 720,188 720,236 60,236 60,202"/>
    <line x1="140" y1="236" x2="140" y2="202"/><line x1="180" y1="236" x2="180" y2="202"/><line x1="260" y1="236" x2="260" y2="202"/>
    <line x1="720" y1="188" x2="745" y2="188"/>
  </g>
  <g class="f-warn">
    <circle cx="60" cy="202" r="6"/><circle cx="140" cy="202" r="6"/><circle cx="180" cy="202" r="6"/><circle cx="260" cy="202" r="6"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="80" y="193">15</text><text x="120" y="193">14</text><text x="160" y="193">13</text><text x="200" y="193">12</text>
    <text x="240" y="193">11</text><text x="280" y="193">10</text><text x="320" y="193">9</text><text x="360" y="193">8</text>
    <text x="400" y="193">7</text><text x="440" y="193">6</text><text x="480" y="193">5</text><text x="520" y="193">4</text>
    <text x="560" y="193">3</text><text x="600" y="193">2</text><text x="640" y="193">1</text><text x="680" y="193">0</text>
    <text x="300" y="256">밀려나간 비트 out 을 비트 15·13·12·10 으로 들어가는 값에 XOR (● 자리) = mask 0xB400</text>
  </g>
  <text x="10" y="282" fill="currentColor" font-size="12">Fibonacci: 피드백 경로에 XOR 트리 (깊이 log2 탭 수).</text>
  <text x="10" y="302" fill="currentColor" font-size="12">Galois: XOR 이 비트 사이마다 하나씩 → 임계 경로가 XOR 1단 (HW 가 더 빠름).</text>
</svg>
```

**전수 검증 (`lfsr_census`)**: 65536 상태를 방문 표시하며 사이클로 나눈다. 체크 테스트 결과와 `demo_lfsr.c` 실제 출력:

```text
x^16+x^14+x^13+x^11+1        mask 0xB400  cycles     2  longest 65535  [1 x1, 65535 x1]
CRC-16-CCITT x^16+x^12+x^5+1 mask 0x8810  cycles     4  longest 32767  [1 x2, 32767 x2]
x^16+1 (rotate)              mask 0x8000  cycles  4116  longest    16  [1 x2, 16 x4080, 8 x30, 4 x3, 2 x1]
x^16+x^15+1                  mask 0xC000  cycles   260  longest   255  [1 x1, 255 x256, 85 x3]
census of 4 masks (4 x 65536 states): 0.37 ms

masks 0x8000..0xFFFF with period 65535: 2048 (theory phi(65535)/16 = 2048)  0.64 s
first few: 0x8016 0x801C 0x801F 0x8029 0x805E 0x806B 

Galois(0xB400, seed 1) output == Fibonacci(0x2D, seed 1) output shifted by 16 bits
ones in one period: 32768, zeros: 32767
first 32 output bits (Fibonacci, seed 1): 10000000000000001000000000010110
```

(Apple M6, Apple clang 21.0.0, `-O2`.) 해석:

| mask | 다항식 | 결과 | 이유 |
|---|---|---|---|
| 0xB400 | x^16+x^14+x^13+x^11+1 | 65535 한 사이클 + 고정점 0 | 원시 다항식 → 최대 길이 |
| 0x8810 | x^16+x^12+x^5+1 (CRC-16-CCITT) | 32767 × 2 + 고정점 2개 | (x+1) × 15차 원시 다항식으로 인수분해 → 최장 2^15−1 |
| 0x8000 | x^16+1 | 최장 16 | 그냥 회전: 16비트 패턴이 16 스텝 뒤 제자리 (사이클 4116개 = 길이 16 목걸이 수) |
| 0xC000 | x^16+x^15+1 | 최장 255 | 탭이 "가깝다고" 좋은 게 아니다 — 인수분해 구조가 주기를 정한다 |
| 0x3400 | x^16 항 없음 | 순열 아님 | 매 스텝 비트 15 가 0 이 되어 상태가 줄어든다 (되돌릴 수 없음) |

- 차수 16 원시 다항식의 개수는 φ(2^16−1)/16 = φ(3·5·17·257)/16 = (2·4·16·256)/16 = **2048**. 데모가 32768개 mask를 전부 돌려 정확히 2048개를 찾았다 — 이론과 전수 실험이 맞는다.
- Galois와 Fibonacci는 **같은 다항식이면 같은 m-sequence 를 위상만 다르게** 낸다 (데모: 16비트 차이). 상태 값의 순서는 다르므로 "Galois 상태 == Fibonacci 상태" 는 기대하면 안 된다.
- 한 주기에 1이 32768개, 0이 32767개 — m-sequence 의 균형 성질(0 상태가 빠져서 0 이 하나 적다).

**펌웨어에서 LFSR 한 스텝의 비용** (`demo_asm.c`, Cortex-M4 `-O2`, 실제 출력):

```text
lfsr16_galois:
	mov.w	r1, #46080
	eor.w	r1, r1, r0, lsr #1
	lsls	r2, r0, #31
	it	eq
	lsreq	r1, r0, #1
	mov	r0, r1
	bx	lr
```

`#46080` = 0xB400. `lsls r2, r0, #31` 은 lsb 를 비트 31 로 올려 Z 플래그를 만들고, `it eq; lsreq` 가 "lsb 가 0 이면 XOR 안 한 값" 을 고른다 — 분기 없이 6 명령 + `bx lr`. arm64 는 `lsr / mov / eor / tst / csel` 5개 + `ret`. 두 구현(if 버전, `-(s&1) & mask` 버전) 모두 Cortex-M4 에서 같은 코드로 컴파일됐다 — 컴파일러가 분기를 IT 블록 조건 실행으로 바꾼다.

**PRBS 링크 테스트와 자기동기 검사기**: SerDes·PCIe·이더넷 PHY 검증에서 송신기는 PRBS(예: PRBS7 = x^7+x^6+1, PRBS15, PRBS31)를 보내고, 수신기는 **송신기 seed 를 모른 채** 받은 비트로 점화식을 다시 계산해 비교한다. 오류 1비트가 들어오면 그 비트가 검사 식의 각 항 위치를 지나갈 때마다 한 번씩, 총 **다항식의 항 수**만큼 틀린 예측이 생긴다:

| 수신열 | 오류 카운트 | 설명 |
|---|---|---|
| 깨끗함 | 0 | 처음 16비트는 lock 용 |
| rx[100] 반전 | 5 | k = 100, 111, 113, 114, 116 (항 5개: x^16, x^14, x^13, x^11, 1) |
| rx[100], rx[1000] 반전 | 10 | 서로 멀어서 독립 |
| rx[100], rx[101] 반전 | 8 | k=114 에서 두 오류가 같은 식에 들어와 XOR 로 상쇄 |
| 중간(777번째)부터 수신 | 0 | 16비트만 받으면 lock — seed 불필요 |

그래서 BERT(bit error rate tester)의 "오류 카운트" 는 실제 비트 오류 수의 약 (항 수)배다. PRBS7 이면 ×3 (연습 4). 장비/IP 문서가 어느 쪽 숫자를 보고하는지 꼭 확인한다.

**CRC 와의 관계**: CRC 레지스터 = 입력 비트가 피드백에 XOR 되는 LFSR (나눗셈 회로). 입력이 0이면 그냥 자율(autonomous) LFSR 이고, CRC-16-CCITT(0x1021) 레지스터는 주기 32767 이다 (연습 5, 위 census 와 같은 숫자). CRC 다항식은 일부러 (x+1) 인수를 넣어 **홀수 개 비트 오류를 모두 검출**하게 만든 경우가 많다 — 최대 주기가 목표가 아니라 오류 검출 성질이 목표다. 반대로 PRBS·스크램블러는 최대 주기(원시 다항식)가 목표다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so          # 41/41 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 41/41, 경고 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_race.c -o demo_race && ./demo_race
cc -std=c11 -Wall -Wextra -Werror -O2 demo_lfsr.c -o demo_lfsr && ./demo_lfsr
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `for (i...) q[i] = f(q, i);` | 뒤쪽 플립플롭이 같은 에지에 이미 바뀐 값을 읽음 (blocking `=`) | 지역 배열 `d[]` 에 다 계산 후 복사 |
| "역순으로 갱신하면 되지" | 순수 시프트 체인만 맞고, 피드백(LFSR·swap·링 카운터)은 어떤 순서도 틀림 | 2-phase 가 일반 해법 |
| cascade 에서 `tick(lo); in_hi = tc(lo);` | 새 lo 로 TC 를 계산 → 상위가 한 사이클 일찍 증가 | `in_hi = tc(lo)` 를 tick **전에** |
| LFSR seed 0 | 0 → 0 잠김 (XOR LFSR). 랜덤 seed 가 0 이 나오면 영원히 0 | seed 가 0 이면 다른 값으로 바꿈 (XNOR 형이면 all-ones 가 잠김) |
| mask/탭 번호 혼동 | 다항식 항 x^k ↔ Galois mask 비트 k−1, Fibonacci 탭 비트 16−k. 하나라도 틀리면 비최대 다항식이 됨 | 전수 census 로 주기 65535 를 **테스트로** 고정 |
| 주기를 "0 이 될 때까지" 로 셈 | 최대 LFSR 은 0 에 절대 안 간다 → 무한 루프 | "seed 로 돌아올 때까지" + limit |
| `int s; s >>= 1;` 로 상태 저장 | 부호 있는 음수 오른쪽 시프트는 구현 정의(보통 산술 시프트, 1 이 채워짐) | `uint16_t` / `uint32_t` |
| census 에서 순열 검사 생략 | mask bit15 가 없으면 꼬리(rho) 모양 → 출발점에 안 돌아와 무한 루프 | 이미 본 상태를 밟으면 즉시 중단 |
| PRBS 오류 카운트 = 비트 오류 수로 보고 | 자기동기 검사기는 오류를 (항 수)배로 센다 | 항 수로 나누거나, 검사기 종류를 명시 |

## 8. 면접 Q&A

<details>
<summary>Q1. 래치와 플립플롭의 차이는? 왜 동기 설계는 플립플롭을 쓰나요?</summary>

래치는 enable 이 켜진 동안 투명(D→Q가 계속 흐름), 플립플롭은 클럭 에지 순간의 D만 잡는다. 플립플롭이면 "한 에지에서 다음 에지까지 조합 논리가 안정되면 된다" 는 한 가지 규칙(셋업/홀드)으로 타이밍을 검증할 수 있다 (ARCH-B13).
꼬리질문: "의도치 않은 래치는 어디서 생기나?" → 조합 always 블록/process 에서 어떤 분기에 출력을 대입하지 않으면 "값을 유지하라" 는 뜻이 되어 합성기가 래치를 만든다. 모든 분기에 기본값을 준다. 래치를 일부러 쓰는 경우: 클럭 게이팅 셀(래치 + AND), time borrowing 이 필요한 고속 경로.

</details>

<details>
<summary>Q2. 시뮬레이터에서 레지스터들을 어떤 순서로 갱신해야 하나요? Verilog blocking/non-blocking 과 무슨 관계죠?</summary>

순서가 아니라 단계로 푼다: 모든 다음 상태를 옛 상태로 계산한 뒤 한꺼번에 반영 (이중 버퍼). in-place 로 하면 의존 그래프에 사이클이 있는 회로(swap, LFSR, 링 카운터)는 어떤 순서로도 틀린다 — 이 레슨의 swap 은 오름차순 00, 내림차순 11. Verilog 의 non-blocking `<=` 가 정확히 이 의미(우변 먼저 평가, 시간 스텝 끝에 일괄 갱신)라서 순차 블록은 `<=`, 조합 블록은 `=` 가 규칙이다.
꼬리질문: "조합 블록에 `<=` 를 쓰면?" → 같은 시간 스텝 안에서 중간 값이 바로 반영되지 않아 시뮬레이션이 추가 델타 사이클을 돌거나, 감도 목록에 따라 합성 결과와 시뮬레이션이 다를 수 있다. 규칙을 섞지 않는 게 답.

</details>

<details>
<summary>Q3. n비트 LFSR 의 최대 주기가 2^n 이 아니라 2^n − 1 인 이유는? 2^n 을 다 쓰고 싶으면?</summary>

XOR 피드백은 GF(2) 선형 변환이라 0 상태는 0 으로만 간다(고정점). 그래서 0 을 뺀 2^n−1 개가 최대이고, 피드백 다항식이 원시 다항식일 때 달성된다. 16비트 원시 다항식은 φ(65535)/16 = 2048 개 — 데모에서 32768 개 mask 를 전수로 돌려 정확히 2048 개를 확인했다.
꼬리질문: "0 상태를 포함시키려면?" → 피드백에 "나머지 비트가 모두 0 인지" 검출 논리를 추가해 0 을 사이클에 끼워 넣는다 (de Bruijn 카운터). "XNOR LFSR 은?" → 잠김 상태가 all-ones 로 바뀐다 (리셋 값 0 을 그대로 쓸 수 있어 FPGA 에서 선호).

</details>

<details>
<summary>Q4. Galois 와 Fibonacci LFSR 의 차이는? 하드웨어/소프트웨어 각각 어느 쪽이 유리한가요?</summary>

Fibonacci: 여러 탭을 XOR 트리로 모아 한쪽 끝으로 넣는다 → 피드백 경로에 XOR 여러 단. Galois: 나가는 비트를 탭 자리마다 XOR → 비트 사이 XOR 한 단이라 임계 경로가 짧다(고속 HW). 소프트웨어도 Galois 가 유리하다: 패리티 계산 없이 `s >>= 1; if (out) s ^= mask;` (Cortex-M4 에서 6~7 명령, 분기 없음). 같은 다항식이면 출력 비트열은 같은 m-sequence 의 다른 위상 (데모: 16비트 차이).
꼬리질문: "한 클럭에 8비트(병렬)씩 만들려면?" → 8 스텝을 펼쳐 각 출력 비트를 상태 비트들의 XOR 식으로 미리 계산한다 (선형이라 행렬 거듭제곱 A^8). SW 에서는 256 엔트리 테이블로 바이트 단위 처리 — 바이트 단위 CRC 테이블과 같은 아이디어.

</details>

<details>
<summary>Q5. PRBS 로 링크를 테스트할 때 수신기는 어떻게 동기를 맞추나요? 오류 카운트는 어떻게 해석하죠?</summary>

자기동기 검사기는 받은 비트로 점화식을 다시 계산하므로 seed 를 몰라도 n 비트(PRBS16 이면 16)만 받으면 lock 된다. 대신 비트 오류 1개가 다항식 항 수만큼 예측 실패를 만든다 (x^16+x^14+x^13+x^11+1 → 5, PRBS7 → 3). 인접 오류는 서로 상쇄될 수도 있다 (테스트: 2개 → 8).
꼬리질문: "BER 1e-12 를 주장하려면 몇 비트를 봐야 하나?" → 오류 0개일 때 신뢰도 CL 에서 N ≈ −ln(1−CL)/BER, 95% 면 약 3/BER = 3×10^12 비트. "왜 PRBS 차수를 바꿔 가며 테스트하나?" → PRBS7 은 짧아서 연속 같은 비트(run)가 최대 7, PRBS31 은 31 — 긴 run 이 CDR·AC 커플링(기저선 변동)을 더 괴롭힌다.

</details>

<details>
<summary>Q6. CRC 와 LFSR 은 무슨 관계인가요? CRC-16-CCITT 는 왜 최대 주기 다항식이 아닌가요?</summary>

CRC 회로는 입력 비트를 피드백에 XOR 하는 LFSR (다항식 나눗셈의 나머지 계산기). 입력이 0 이면 자율 LFSR 이고, 0x1021 은 (x+1) × 15차 원시 다항식이라 주기 32767 (census: 32767 사이클 2개 + 고정점 2개, 연습 5 에서도 32767). (x+1) 인수는 홀수 개 비트 오류를 모두 잡게 해 주므로 CRC 설계에서는 장점이다.
꼬리질문: "메시지 뒤에 CRC 를 붙여 다시 계산하면 0 이 되는 이유?" → 나머지를 붙이면 전체가 생성 다항식의 배수가 되기 때문 (반사·xorout 없는 변형 기준. 연습 5: CCITT-FALSE 에서 0 확인).

</details>

<details>
<summary>Q7. 리플 카운터는 왜 실무에서 피하나요? 펌웨어에서 비슷한 문제는?</summary>

리플 카운터는 앞 단 출력이 다음 단 클럭이라 비트들이 순서대로 늦게 바뀐다. 0111→1000 사이에 0110, 0100, 0000 이 잠깐 보이고 (연습 3), `cnt == 0` 디코더가 거짓 펄스를 낸다. 동기 카운터는 모든 비트가 같은 에지에 바뀐다.
꼬리질문: "펌웨어에서 같은 종류 버그는?" → 버스 폭보다 넓은 카운터를 두 번에 나눠 읽기 (예: 32비트 MCU 에서 64비트 타임스탬프 = 상위·하위 두 레지스터). 하위가 넘치는 순간 읽으면 상위/하위가 서로 다른 시점 값 → hi-lo-hi 로 읽고 hi 가 바뀌었으면 다시 읽는다. (하드웨어가 하위를 읽을 때 상위를 래치해 주는 칩도 있다.)

</details>

<details>
<summary>Q8. 비동기 입력(버튼, 다른 클럭 도메인 신호)을 플립플롭에 바로 넣으면?</summary>

셋업/홀드 창 안에서 바뀌면 출력이 한동안 0 도 1 도 아닌 상태(metastability)에 머물 수 있다. 2단 동기화기(플립플롭 2개 직렬)로 해결 시간을 한 클럭 더 준다 — 확률을 줄일 뿐 0 으로 만들지는 않는다 (MTBF 계산). 이 레슨의 0/1 모델은 이것을 표현하지 못한다는 한계도 말할 수 있어야 한다. 타이밍 수치는 ARCH-B13.

</details>

## 9. 추가 연습

### 연습 1 — 4비트 Johnson 카운터

`d[0] = NOT q[3]`, `d[i] = q[i-1]`. 0000 에서 8 상태 순서를 확인하고, 불법 상태 0101 에서 출발하면 어떻게 되는지 보여라.

<details>
<summary>답 보기</summary>

0001 → 0011 → 0111 → 1111 → 1110 → 1100 → 1000 → 0000 (2n = 8 상태, 인접 상태가 항상 1비트만 다름 → 2-입력 AND 로 glitch 없이 디코드). 16 − 8 = 8 개의 불법 상태도 **따로 8-사이클**을 이루어(0101 에서 출발하면 0000 을 영원히 못 지남) 노이즈로 한번 들어가면 못 빠져나온다(lock-out). 그래서 실제 설계는 불법 상태를 정상 사이클로 끌어오는 자기 교정(self-correcting) 피드백을 넣는다.

</details>

### 연습 2 — 자동 재적재 카운터 (타이머 뼈대)

`cnt == arr` 이고 EN 이면 다음 에지에 0 으로 돌아가며 그 사이클에 TC(업데이트 이벤트) = 1. LOAD 는 동기 적재(최우선). ARR = 9 로 100 클럭 동안 TC 펄스 수를 세라.

<details>
<summary>답 보기</summary>

TC 는 10 클럭마다 한 번(첫 펄스 10번째 클럭) → 100 클럭에 10 펄스. 주기 = ARR + 1 이라 "10 MHz → 1 MHz 분주는 ARR = 9" (STM32 타이머의 PSC/ARR 설정에서 −1 을 빼먹는 실수가 흔하다). 다음 상태를 먼저 `next` 로 계산하고 마지막에 대입하는 구조가 2-phase 그대로다.

</details>

### 연습 3 — 리플 카운터의 과도 상태

비트 i 가 비트 i−1 의 하강 에지에서 토글한다고 하고, +1 할 때 거쳐가는 순간 값들을 기록하라. 0~15 에서 +1 하는 동안 "cnt == 0" 디코더가 거짓으로 켜지는 경우는 몇 번인가?

<details>
<summary>답 보기</summary>

0111 → 0110 → 0100 → 0000 → 1000. 중간에 0000 을 지나는 건 1→2, 3→4, 7→8 의 **3번** (15→0 은 최종 값이 0 이라 정상). 동기 카운터는 같은 에지에 모든 비트가 바뀌므로 (클럭→Q 지연 차이로 아주 짧은 glitch 는 있을 수 있지만) 다음 에지에서 샘플링하는 동기 회로에는 보이지 않는다.

</details>

### 연습 4 — PRBS7 주기와 오류 배수

PRBS7 (x^7+x^6+1): `b[k] = b[k-7] ⊕ b[k-6]`. 7비트 상태로 주기를 재고, 자기동기 검사기에 단일 오류를 넣어 오류 카운트를 확인하라.

<details>
<summary>답 보기</summary>

주기 127 = 2^7 − 1 (원시 다항식). 단일 오류 → 오류 카운트 3 (항 3개: x^7, x^6, 1). 이더넷/SerDes 장비의 PRBS7·PRBS31 카운터를 읽을 때 이 배수를 생각해야 한다.

</details>

### 연습 5 — CRC-16/CCITT-FALSE 는 입력이 섞이는 LFSR

poly 0x1021, init 0xFFFF, 반사 없음, xorout 0. `"123456789"` 의 CRC, CRC 를 빅 엔디언으로 붙인 11바이트의 CRC, 입력 0 일 때 레지스터의 주기(seed 0x0001)를 구하라.

<details>
<summary>답 보기</summary>

0x29B1 (이 변형의 표준 check 값), 붙인 뒤 다시 계산하면 0 (나머지 0 = 생성 다항식의 배수), 입력 0 레지스터 주기 32767 — Part C census 의 0x8810 결과(32767 사이클)와 같은 숫자다. 같은 다항식을 왼쪽 시프트로 돌렸을 뿐이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   # 17/17 passed
```

## 10. 핵심 정리

- 플립플롭 = 에지 순간의 D 를 잡는다. 래치 = enable 동안 투명. 동기 설계는 플립플롭 + 공통 클럭.
- 시뮬레이션 규칙: **모든 D 를 옛 Q 로 계산 → 한꺼번에 반영** (2-phase / 이중 버퍼 / Verilog `<=`). in-place 는 의존 사이클이 있으면 어떤 순서로도 틀린다.
- 동기 카운터: `d_i = q_i ⊕ EN·q_0⋯q_{i-1}`, TC = EN·all-ones → 다음 단 EN. TC 는 에지 **전** 값으로.
- n비트 XOR LFSR 최대 주기 2^n − 1 (0 은 잠김), 조건 = 원시 다항식. 16비트 원시 다항식 2048개, 예: x^16+x^14+x^13+x^11+1 (Galois 0xB400 / Fibonacci 탭 0x002D).
- 다항식이 인수분해되면 주기가 짧다: x^16+1 → 16, CRC-CCITT → 32767, x^16+x^15+1 → 255. 의심되면 65536 상태 전수 census (0.4 ms).
- Galois = 비트 사이 XOR (HW 임계 경로 짧음, SW 도 간단), Fibonacci = 탭 XOR 트리. 같은 다항식 → 같은 m-sequence, 위상만 다름.
- PRBS 자기동기 검사기: seed 없이 n 비트 후 lock, 비트 오류 1개 → 오류 카운트 = 다항식 항 수 (PRBS7 ×3).
- CRC = 입력이 섞이는 LFSR. CRC 다항식은 오류 검출 성질((x+1) 인수 등)이 목표, PRBS 는 최대 주기가 목표.

## 참고자료

- Harris & Harris, *Digital Design and Computer Architecture, ARM Edition* — 3장 Sequential Logic Design (래치·플립플롭·동기 설계), 5장 카운터·시프트 레지스터
- [Wikipedia — Linear-feedback shift register (Fibonacci/Galois, 0xACE1 예, 최대 길이 탭 표)](https://en.wikipedia.org/wiki/Linear-feedback_shift_register)
- [Xilinx XAPP052 (Peter Alfke) — Efficient Shift Registers, LFSR Counters, and Long Pseudo-Random Sequence Generators](https://docs.amd.com/v/u/en-US/xapp052)
- [Clifford E. Cummings — Nonblocking Assignments in Verilog Synthesis, Coding Styles That Kill! (SNUG 2000)](http://www.sunburst-design.com/papers/CummingsSNUG2000SJ_NBA.pdf)
- [ITU-T O.150 — 디지털 전송 장비 시험용 PRBS 패턴 일반 요구사항](https://www.itu.int/rec/T-REC-O.150)
- [CRC RevEng catalogue — CRC-16/IBM-3740 (= CCITT-FALSE), check 0x29B1](https://reveng.sourceforge.io/crc-catalogue/16.htm)
- 관련 레슨: ARCH-B04 (가산기 carry 체인), ARCH-B07 (Moore/Mealy FSM), ARCH-B13 (셋업/홀드·Fmax), C-B01 (비트 연산), SIV-B03 (리셋 값 테스트)
