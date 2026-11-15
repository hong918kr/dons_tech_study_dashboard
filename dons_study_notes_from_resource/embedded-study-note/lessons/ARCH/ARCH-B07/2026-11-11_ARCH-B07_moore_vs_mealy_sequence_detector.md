---
id: ARCH-B07
time: 60분
updated: 2026-11-11
---
# ARCH-B07 하드웨어 FSM: Moore vs Mealy 시퀀스 검출기

## 학습 목표

- 비트열에서 `1011`(겹침 허용)을 찾는 **Moore**(출력 = 상태만의 함수)와 **Mealy**(출력 = 상태 + 현재 입력의 함수) FSM의 상태 전이표와 상태도를 손으로 그리고, 상태 하나가 "지금까지 맞은 접두사"라는 뜻을 설명할 수 있다.
- "사이클", "에지", "레지스터 출력"을 정확히 정의한 클럭 시뮬레이터(계산 → 에지에서 한꺼번에 커밋)를 만들고, 사이클별 텍스트 파형으로 **Mealy가 정확히 1사이클 먼저** 출력한다는 것을 길이 1~14 전수(32766개 비트열) 테스트로 확인한다.
- 겹침/비겹침, 동기 리셋, Mealy 출력을 플립플롭 하나로 다시 잡은 **registered Mealy == Moore**, 그리고 Mealy 출력의 **글리치(glitch)** 를 설명하고 테스트로 보인다.
- 임의 패턴(≤ 16비트)의 Mealy 표를 자동 생성(KMP 오토마톤)하고, DMA 버퍼 경계를 넘는 sync word를 놓치지 않는 펌웨어 스캐너(비트 루프 / 바이트 표)로 확장한다.

## 1. 문제

시퀀스 검출기는 디지털 설계 면접의 단골 첫 문제("1011 검출기를 Moore와 Mealy로 그려라, 차이는?")이고, 실리콘에서는 UART 시작 비트, 무선 칩의 sync word 검출, SerDes의 comma 정렬, 프로토콜 체커가 모두 같은 구조다 — 펌웨어 엔지니어가 파형을 보고 "출력이 왜 한 사이클 늦지?"를 설명하려면 이 차이를 정확히 알아야 한다.

이 레슨은 RTL이 아니라 **C로 만든 사이클 단위 모델**이다. 그래서 먼저 시간을 정확히 정의한다 (애매하게 두면 "1사이클 빠르다"는 말 자체가 의미가 없다).

| 용어 | 이 모델에서의 뜻 |
|---|---|
| edge k | k 번째 클럭 상승 에지. 모든 플립플롭(FF)이 이 순간에만 값을 바꾼다 |
| 사이클 k | edge k 와 edge k+1 사이 구간 |
| `in[k]` | 사이클 k 동안 안정된 입력 비트. edge k+1 에서 FF가 샘플한다 |
| `S[k]` | 사이클 k 동안 상태 레지스터 값 (edge k 에서 커밋). `S[0]` = 리셋 상태 S0 |
| Moore 출력 | `out[k] = f(S[k])` — 상태만 본다. 상태가 FF이므로 **레지스터 출력(registered output)** 이다 |
| Mealy 출력 | `out[k] = g(S[k], in[k])` — 같은 사이클의 입력이 조합 논리를 거쳐 **바로** 출력에 나온다 |
| registered Mealy | `q[k] = MealyOut[k-1]`, `q[0] = 0` — Mealy 출력 뒤에 FF 하나를 더 단 것 |
| 검출 시점 | 패턴의 마지막 비트가 `in[k]` 일 때 "k 에서 검출". Mealy는 사이클 k, Moore는 사이클 k+1 에 출력 1 |

```svg
<svg viewBox="0 0 760 444" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">입력 1011 0111 — 사이클별 파형 (solution.c 출력과 같은 값)</text>
<rect x="350" y="287" width="80" height="32" class="f-acc"/> <rect x="590" y="287" width="80" height="32" class="f-acc"/> <rect x="430" y="193" width="80" height="32" class="f-ok"/> <rect x="670" y="193" width="80" height="32" class="f-ok"/>
<rect x="430" y="334" width="80" height="32" class="f-ok"/> <rect x="670" y="334" width="80" height="32" class="f-ok"/> <line x1="110" y1="34" x2="110" y2="368" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/> <text x="150.0" y="40" fill="currentColor" text-anchor="middle" font-size="12">cycle 0</text>
<line x1="190" y1="34" x2="190" y2="368" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/> <text x="230.0" y="40" fill="currentColor" text-anchor="middle" font-size="12">cycle 1</text> <line x1="270" y1="34" x2="270" y2="368" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/> <text x="310.0" y="40" fill="currentColor" text-anchor="middle" font-size="12">cycle 2</text>
<line x1="350" y1="34" x2="350" y2="368" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/> <text x="390.0" y="40" fill="currentColor" text-anchor="middle" font-size="12">cycle 3</text> <line x1="430" y1="34" x2="430" y2="368" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/> <text x="470.0" y="40" fill="currentColor" text-anchor="middle" font-size="12">cycle 4</text>
<line x1="510" y1="34" x2="510" y2="368" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/> <text x="550.0" y="40" fill="currentColor" text-anchor="middle" font-size="12">cycle 5</text> <line x1="590" y1="34" x2="590" y2="368" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/> <text x="630.0" y="40" fill="currentColor" text-anchor="middle" font-size="12">cycle 6</text>
<line x1="670" y1="34" x2="670" y2="368" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/> <text x="710.0" y="40" fill="currentColor" text-anchor="middle" font-size="12">cycle 7</text> <line x1="750" y1="34" x2="750" y2="368" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/> <text x="10" y="72" fill="currentColor">clk</text>
<polyline points="110,80 110,56 150,56 150,80 190,80 190,56 230,56 230,80 270,80 270,56 310,56 310,80 350,80 350,56 390,56 390,80 430,80 430,56 470,56 470,80 510,80 510,56 550,56 550,80 590,80 590,56 630,56 630,80 670,80 670,56 710,56 710,80 750,80" fill="none" stroke="currentColor" stroke-width="2"/> <text x="10" y="119" fill="currentColor">in</text> <polyline points="110,103 190,103 190,127 270,127 270,103 430,103 430,127 510,127 510,103 750,103" fill="none" stroke="currentColor" stroke-width="2"/> <text x="150.0" y="119" fill="currentColor" text-anchor="middle" font-family="monospace" opacity=".7">1</text>
<text x="230.0" y="119" fill="currentColor" text-anchor="middle" font-family="monospace" opacity=".7">0</text> <text x="310.0" y="119" fill="currentColor" text-anchor="middle" font-family="monospace" opacity=".7">1</text> <text x="390.0" y="119" fill="currentColor" text-anchor="middle" font-family="monospace" opacity=".7">1</text> <text x="470.0" y="119" fill="currentColor" text-anchor="middle" font-family="monospace" opacity=".7">0</text>
<text x="550.0" y="119" fill="currentColor" text-anchor="middle" font-family="monospace" opacity=".7">1</text> <text x="630.0" y="119" fill="currentColor" text-anchor="middle" font-family="monospace" opacity=".7">1</text> <text x="710.0" y="119" fill="currentColor" text-anchor="middle" font-family="monospace" opacity=".7">1</text> <text x="10" y="166" fill="currentColor">Moore S</text>
<polygon points="114,162 120,150 180,150 186,162 180,174 120,174" fill="none" stroke="currentColor"/> <text x="150.0" y="167" fill="currentColor" text-anchor="middle" font-family="monospace">S0</text> <polygon points="194,162 200,150 260,150 266,162 260,174 200,174" fill="none" stroke="currentColor"/> <text x="230.0" y="167" fill="currentColor" text-anchor="middle" font-family="monospace">S1</text>
<polygon points="274,162 280,150 340,150 346,162 340,174 280,174" fill="none" stroke="currentColor"/> <text x="310.0" y="167" fill="currentColor" text-anchor="middle" font-family="monospace">S2</text> <polygon points="354,162 360,150 420,150 426,162 420,174 360,174" fill="none" stroke="currentColor"/> <text x="390.0" y="167" fill="currentColor" text-anchor="middle" font-family="monospace">S3</text>
<polygon points="434,162 440,150 500,150 506,162 500,174 440,174" fill="none" stroke="currentColor"/> <text x="470.0" y="167" fill="currentColor" text-anchor="middle" font-family="monospace">S4</text> <polygon points="514,162 520,150 580,150 586,162 580,174 520,174" fill="none" stroke="currentColor"/> <text x="550.0" y="167" fill="currentColor" text-anchor="middle" font-family="monospace">S2</text>
<polygon points="594,162 600,150 660,150 666,162 660,174 600,174" fill="none" stroke="currentColor"/> <text x="630.0" y="167" fill="currentColor" text-anchor="middle" font-family="monospace">S3</text> <polygon points="674,162 680,150 740,150 746,162 740,174 680,174" fill="none" stroke="currentColor"/> <text x="710.0" y="167" fill="currentColor" text-anchor="middle" font-family="monospace">S4</text>
<text x="10" y="213" fill="currentColor">Moore out</text> <polyline points="110,221 430,221 430,197 510,197 510,221 670,221 670,197 750,197" fill="none" stroke="currentColor" stroke-width="2"/> <text x="10" y="260" fill="currentColor">Mealy S</text> <polygon points="114,256 120,244 180,244 186,256 180,268 120,268" fill="none" stroke="currentColor"/>
<text x="150.0" y="261" fill="currentColor" text-anchor="middle" font-family="monospace">S0</text> <polygon points="194,256 200,244 260,244 266,256 260,268 200,268" fill="none" stroke="currentColor"/> <text x="230.0" y="261" fill="currentColor" text-anchor="middle" font-family="monospace">S1</text> <polygon points="274,256 280,244 340,244 346,256 340,268 280,268" fill="none" stroke="currentColor"/>
<text x="310.0" y="261" fill="currentColor" text-anchor="middle" font-family="monospace">S2</text> <polygon points="354,256 360,244 420,244 426,256 420,268 360,268" fill="none" stroke="currentColor"/> <text x="390.0" y="261" fill="currentColor" text-anchor="middle" font-family="monospace">S3</text> <polygon points="434,256 440,244 500,244 506,256 500,268 440,268" fill="none" stroke="currentColor"/>
<text x="470.0" y="261" fill="currentColor" text-anchor="middle" font-family="monospace">S1</text> <polygon points="514,256 520,244 580,244 586,256 580,268 520,268" fill="none" stroke="currentColor"/> <text x="550.0" y="261" fill="currentColor" text-anchor="middle" font-family="monospace">S2</text> <polygon points="594,256 600,244 660,244 666,256 660,268 600,268" fill="none" stroke="currentColor"/>
<text x="630.0" y="261" fill="currentColor" text-anchor="middle" font-family="monospace">S3</text> <polygon points="674,256 680,244 740,244 746,256 740,268 680,268" fill="none" stroke="currentColor"/> <text x="710.0" y="261" fill="currentColor" text-anchor="middle" font-family="monospace">S1</text> <text x="10" y="307" fill="currentColor">Mealy out</text>
<polyline points="110,315 350,315 350,291 430,291 430,315 590,315 590,291 670,291 670,315 750,315" fill="none" stroke="currentColor" stroke-width="2"/> <text x="10" y="354" fill="currentColor">Mealy q</text> <polyline points="110,362 430,362 430,338 510,338 510,362 670,362 670,338 750,338" fill="none" stroke="currentColor" stroke-width="2"/>
<text x="10" y="392" fill="currentColor">Mealy out = g(S, in): 마지막 1 이 들어온 <tspan font-weight="bold">그 사이클</tspan>(3, 6)에 1.</text>
<text x="10" y="412" fill="currentColor">Moore out = f(S): 에지를 지나 S4 가 된 <tspan font-weight="bold">다음 사이클</tspan>(4, 7)에 1.</text>
<text x="10" y="432" fill="currentColor">Mealy q = Mealy out 을 FF 하나로 다시 잡은 것 → Moore out 과 똑같다.</text>
</svg>
```

### Part A — 상태 전이표와 클럭 시뮬레이터

- `moore_next(s, in, overlap)` / `moore_out(s)` — 5상태 Moore 1011 검출기 (S4 = "1011을 봤다", 출력 1).
- `mealy_next(s, in, overlap)` / `mealy_out(s, in)` — 4상태 Mealy 1011 검출기 (S3 에서 입력 1 이 들어오는 **그 사이클**에 출력 1).
- `simulate(kind, in, rst, n, overlap, trace)` — 사이클마다 ① 현재 레지스터 값과 입력으로 출력·다음 상태를 계산하고 ② 에지에서 상태 레지스터와 출력 레지스터를 **한꺼번에** 커밋한다. trace 에 `state[k]`, `out[k]`, `q[k]` 를 기록한다.
- 하네스가 `1011 0111 0110 0` 의 파형을 출력하고, 길이 1~14 의 모든 비트열에 대해 ① Mealy 출력 == 참조 검출기 ② Moore 출력 == 참조 검출기를 1사이클 민 것 ③ `MooreOut[k+1] == MealyOut[k]` 를 확인한다.

### Part B — 면접관이 파고드는 경계

- **겹침/비겹침**: `1011011` 에서 겹침이면 2번(사이클 3, 6), 비겹침이면 1번. 바뀌는 건 "검출 전이"의 목적지 하나뿐이다.
- **동기 리셋**: `rst[k]=1` 이면 edge k+1 에서 S ← S0, 그 사이클 입력은 버리고 Mealy 출력도 0 으로 가린다. 리셋을 걸친 패턴은 검출되면 안 된다 (무작위 2000개 시나리오).
- **registered Mealy == Moore**: Mealy 출력을 FF로 한 번 더 잡으면 Moore 출력과 비트 하나 다르지 않다 (겹침/비겹침/리셋 모두).
- **글리치**: 사이클 하나를 앞 반/뒤 반으로 나눈 모델 `mealy_half_cycle(a, b, n, half)` — FF는 뒤 반 값 `b[k]` 만 샘플하지만 Mealy 조합 출력은 앞 반 값 `a[k]` 에도 반응한다. "앞 반 1 → 뒤 반 0" 인 가짜 펄스 수를 센다.

### Part C — 펌웨어/실리콘: 임의 패턴 + 버퍼 경계

- `kmp_build(pat, len, tab)` — 길이 1~16 패턴의 Mealy 표(겹침)를 자동 생성. `1011` 이면 Part A 의 손 표와 같아야 한다. 무작위 패턴 400개 × 64바이트 버퍼를 브루트포스와 비교.
- `scan_bits(tab, buf, n, &state)` — 바이트 버퍼를 MSB 먼저 먹이고, **상태를 호출 사이에 이어서** DMA 반쪽 버퍼 경계에 걸친 sync word(예: 16비트 `0xD391`)도 찾는다. 모든 분할점(0~64)에서 결과가 같아야 한다.
- `byte_tab_build(tab, bt)` — (상태, 바이트) → (다음 상태, 검출 수) 256열 표로 8비트를 한 번에 처리. 비트 루프와 결과·최종 상태가 같아야 한다.

## 2. Problem (English)

> **Sequence detector: Moore vs Mealy.**
>
> Design a synchronous FSM that asserts its output when the serial input `din` has just completed the pattern `1011` (overlapping matches allowed: `1011011` contains two matches).
>
> 1. Give the state-transition tables of a Moore machine (output is a function of state only) and a Mealy machine (output is a function of state and current input). How many states does each need? How many flip-flops with binary encoding?
> 2. Write a cycle-accurate simulator in C: in each cycle compute outputs and next state from the *current* register values, then commit all registers at the clock edge. Print a text waveform (`clk`, `din`, state, output) for both machines.
> 3. Prove with a test that, for every input sequence, the Moore output equals the Mealy output delayed by exactly one cycle, and that adding one output flip-flop to the Mealy machine makes it identical to the Moore machine.
> 4. Support non-overlapping detection and a synchronous reset. Explain why a Mealy output can glitch and when that matters.
> 5. Generalize: build the Mealy table for an arbitrary `len`-bit pattern (len ≤ 16) and scan a byte stream that arrives in chunks (the pattern may straddle a chunk boundary). Optionally process one byte per table lookup.
>
> Example: `din = 1 0 1 1 0 1 1 1 0 1 1 0 0` → Mealy output high in cycles 3, 6, 10; Moore output high in cycles 4, 7, 11.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있으면
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
# 상태는 S0, 출력 파형은 전부 0, FAIL 17줄, 마지막 줄 "7/24 passed", exit 1 → TODO 9곳을 채워 24/24
```

TODO는 9곳이다: 전이표 4개(`moore_next`, `moore_out`, `mealy_next`, `mealy_out`), `simulate` 의 출력·다음 상태·커밋, `mealy_half_cycle`, `kmp_build`, `scan_bits`, `byte_tab_build`. 파형 출력·참조 검출기·PRNG 는 주어져 있다.

## 4. 힌트

<details>
<summary>힌트 1 — 상태 = "지금까지 맞은 패턴 앞부분의 길이"</summary>

`1011` 이면 상태는 "아무것도 / 1 / 10 / 101" (+ Moore 는 "1011"). 입력 하나를 붙였을 때 패턴이 계속 맞으면 한 칸 전진, 틀리면 **지금까지 본 문자열의 접미사 중 패턴 접두사와 같은 가장 긴 것**으로 돌아간다. 예: "10" 에 0 → "100" → 접미사 "00", "0" 은 접두사가 아님 → S0. "101" 에 0 → "1010" → 접미사 "10" 이 접두사 → S2.

</details>

<details>
<summary>힌트 2 — Moore 에는 상태가 하나 더 필요한 이유</summary>

Moore 출력은 상태만 보므로 "방금 1011 을 끝냈다"가 **상태로** 존재해야 한다 → S4. S4 에서 다음 입력: 0 이면 "10110" 의 접미사 "10" → S2, 1 이면 "10111" 의 접미사 "1" → S1. Mealy 는 S3 에서 1 이 들어오는 *전이* 에 출력을 달면 되므로 S4 가 없다.

</details>

<details>
<summary>힌트 3 — simulate 의 순서</summary>

`out` 과 `next` 를 **둘 다** 현재 `S` 로 계산한 다음에 `S = next; Q = out;`. 순서를 바꿔 `S` 를 먼저 갱신하면 Mealy 는 1011 이 아니라 101 을 검출한다 (§7, `demo_inplace_bug.c`).

</details>

<details>
<summary>힌트 4 — 비겹침과 리셋</summary>

비겹침: Moore 는 S4 의 전이를 S0 의 전이와 같게(0→S0, 1→S1), Mealy 는 S3 --1--> S0. 리셋: `next = r ? 0 : ...`, Mealy 출력은 `!r && g(S,in)`.

</details>

<details>
<summary>힌트 5 — KMP 표와 버퍼 경계</summary>

상태 s, 입력 c: 패턴 앞 s비트 + c 로 만든 문자열 t 를 만들고, k = min(s+1, len−1) 부터 줄여가며 "t 의 마지막 k비트 == 패턴의 처음 k비트" 인 첫 k 가 다음 상태. t 가 패턴 전체면 출력 1. 버퍼 경계: `*state` 를 읽어 시작하고 끝에 다시 써 두면 된다 — 조각마다 0 으로 초기화하는 게 버그.

</details>

## 5. 풀이

### Part A — 상태 = 맞은 접두사, 출력은 어디에 다나

**Moore 전이표** (상태 옆 `/x` 는 그 상태의 출력)

| 상태 | 뜻 | in=0 | in=1 | 출력 |
|---|---|---|---|---|
| S0 | (없음) | S0 | S1 | 0 |
| S1 | `1` | S2 ("10") | S1 ("11"→"1") | 0 |
| S2 | `10` | S0 ("100"→"") | S3 | 0 |
| S3 | `101` | S2 ("1010"→"10") | S4 | 0 |
| S4 | `1011` | S2 ("10110"→"10") | S1 ("10111"→"1") | **1** |

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b07-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">Moore 1011 (겹침) — 원 안: 상태/출력, 지금까지 맞은 접두사</text>
  <g stroke="currentColor" stroke-width="1.5">
    <circle cx="70" cy="120" r="32" fill="none"/><circle cx="220" cy="120" r="32" fill="none"/>
    <circle cx="370" cy="120" r="32" fill="none"/><circle cx="520" cy="120" r="32" fill="none"/>
    <circle cx="670" cy="120" r="32" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="70" y="117" font-weight="bold">S0/0</text><text x="70" y="134" font-size="12" font-family="monospace">""</text>
    <text x="220" y="117" font-weight="bold">S1/0</text><text x="220" y="134" font-size="12" font-family="monospace">1</text>
    <text x="370" y="117" font-weight="bold">S2/0</text><text x="370" y="134" font-size="12" font-family="monospace">10</text>
    <text x="520" y="117" font-weight="bold">S3/0</text><text x="520" y="134" font-size="12" font-family="monospace">101</text>
    <text x="670" y="117" font-weight="bold">S4/1</text><text x="670" y="134" font-size="12" font-family="monospace">1011</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.5" marker-end="url(#b07-arr)">
    <line x1="102" y1="120" x2="186" y2="120"/><line x1="252" y1="120" x2="336" y2="120"/>
    <line x1="402" y1="120" x2="486" y2="120"/><line x1="552" y1="120" x2="636" y2="120"/>
    <path d="M 56 92 C 36 40, 104 40, 84 92"/><path d="M 206 92 C 186 40, 254 40, 234 92"/>
    <path d="M 505 92 Q 445 40 385 92"/>
    <path d="M 356 148 Q 220 215 84 148"/>
    <path d="M 656 148 Q 520 215 384 148"/>
    <path d="M 670 152 Q 445 300 220 152" stroke-dasharray="5 3"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="145" y="112">1</text><text x="295" y="112">0</text><text x="445" y="112">1</text><text x="595" y="112">1</text>
    <text x="70" y="50">0</text><text x="220" y="50">1</text><text x="445" y="58">0</text>
    <text x="140" y="190">0</text><text x="520" y="176">0</text>
    <text x="445" y="244">1 (끝의 1 을 다음 패턴의 첫 1 로 재사용)</text>
  </g>
  <text x="10" y="286" fill="currentColor" font-size="12">비겹침이면 S4 의 두 화살표만 바뀐다: 0 → S0, 1 → S1 (S0 와 똑같이). FF 수: 이진 3개, one-hot 5개.</text>
</svg>
```

**Mealy 전이표** (칸 = 다음 상태 / 출력)

| 상태 | 뜻 | in=0 | in=1 |
|---|---|---|---|
| S0 | (없음) | S0 / 0 | S1 / 0 |
| S1 | `1` | S2 / 0 | S1 / 0 |
| S2 | `10` | S0 / 0 | S3 / 0 |
| S3 | `101` | S2 / 0 | **S1 / 1** (겹침) · S0 / 1 (비겹침) |

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b07-arr2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">Mealy 1011 (겹침) — 화살표 라벨: 입력/출력</text>
  <g stroke="currentColor" stroke-width="1.5" fill="none">
    <circle cx="90" cy="120" r="32"/><circle cx="270" cy="120" r="32"/>
    <circle cx="450" cy="120" r="32"/><circle cx="630" cy="120" r="32"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="90" y="117" font-weight="bold">S0</text><text x="90" y="134" font-size="12" font-family="monospace">""</text>
    <text x="270" y="117" font-weight="bold">S1</text><text x="270" y="134" font-size="12" font-family="monospace">1</text>
    <text x="450" y="117" font-weight="bold">S2</text><text x="450" y="134" font-size="12" font-family="monospace">10</text>
    <text x="630" y="117" font-weight="bold">S3</text><text x="630" y="134" font-size="12" font-family="monospace">101</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.5" marker-end="url(#b07-arr2)">
    <line x1="122" y1="120" x2="236" y2="120"/><line x1="302" y1="120" x2="416" y2="120"/>
    <line x1="482" y1="120" x2="596" y2="120"/>
    <path d="M 76 92 C 56 40, 124 40, 104 92"/><path d="M 256 92 C 236 40, 304 40, 284 92"/>
    <path d="M 615 92 Q 540 40 465 92"/>
    <path d="M 436 148 Q 270 215 104 148"/>
    <path d="M 630 152 Q 450 300 270 152" stroke-width="3"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="180" y="112">1/0</text><text x="360" y="112">0/0</text><text x="540" y="112">1/0</text>
    <text x="90" y="50">0/0</text><text x="270" y="50">1/0</text><text x="540" y="58">0/0</text>
    <text x="170" y="190">0/0</text>
  </g>
  <text x="450" y="244" fill="currentColor" text-anchor="middle" font-weight="bold">1/1 — 검출! 출력은 이 전이(같은 사이클)에 붙는다</text>
  <text x="10" y="286" fill="currentColor" font-size="12">상태 4개 → 이진 FF 2개 (연습 2: s0' = in 으로 아주 단순). 출력이 상태가 아니라 화살표에 붙는 것이 Mealy.</text>
</svg>
```

**왜 Moore 가 1사이클 늦나.** 마지막 `1` 은 사이클 k 에 입력선에 있다. Moore 는 그 비트를 **edge k+1 에서 상태로 저장한 뒤에야** "1011 이다"를 알 수 있으므로 출력은 사이클 k+1. Mealy 는 S3 에 있고 입력이 1 이라는 사실을 조합 논리로 바로 보므로 사이클 k. 즉 Moore 출력 = Mealy 출력을 FF 하나로 지연시킨 것 — 이게 Part B 의 `bad_q == 0` 이다.

**두 단계 시뮬레이션.** 하드웨어에서 모든 FF 는 같은 에지에 *옛 값*을 보고 동시에 바뀐다. C 로는 "① 현재 값만 보고 `out`, `next` 를 지역 변수에 계산 ② 루프 끝에서 `S = next; Q = out;`" 으로 흉내 낸다. Verilog 의 비블로킹 대입 `<=` 가 정확히 이 의미다. (FF 체인에서 제자리 갱신이 만드는 경쟁은 ARCH-B06 에서 다룬다.)

solution.c 실행 결과의 파형 부분 (Apple M6, Apple clang 21.0.0):

```text
cycle    0   1   2   3   4   5   6   7   8   9   10  11  12  
clk      /‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_
in       ‾‾‾‾____‾‾‾‾‾‾‾‾____‾‾‾‾‾‾‾‾‾‾‾‾____‾‾‾‾‾‾‾‾________
in(bit)  1   0   1   1   0   1   1   1   0   1   1   0   0   
Moore S  S0  S1  S2  S3  S4  S2  S3  S4  S1  S2  S3  S4  S2  
Moore o  ________________‾‾‾‾________‾‾‾‾____________‾‾‾‾____
Mealy S  S0  S1  S2  S3  S1  S2  S3  S1  S1  S2  S3  S1  S2  
Mealy o  ____________‾‾‾‾________‾‾‾‾____________‾‾‾‾________
Mealy q  ________________‾‾‾‾________‾‾‾‾____________‾‾‾‾____
...
exhaustive: 32766 streams, mismatches mealy=0 moore=0 shift=0
```

읽는 법: 사이클 3 에서 `in=1` 이고 Mealy 상태가 S3 → Mealy 출력 1. 같은 사이클 Moore 는 아직 S3(출력 0), edge 4 에서 S4 가 되어 사이클 4 에 출력 1. 사이클 7 에서 Moore 는 S4 인데 입력이 1 이라 다음은 S1 (`10111` → `1`), Mealy 는 사이클 6 에 이미 검출하고 S1 로 갔다. 전수 32766개(= 2¹⁵ − 2, 길이 1~14) 비트열에서 불일치 0.

### Part B — 겹침, 리셋, registered Mealy, 글리치

**겹침 vs 비겹침.** 바뀌는 건 검출 전이의 목적지 하나: 겹침은 "1011 의 접미사이면서 접두사인 가장 긴 것" = `1` (S1), 비겹침은 처음부터(S0). `1011011` → 겹침 `3,6`, 비겹침 `3` (Moore 비겹침은 `4`). 비겹침은 "왼쪽부터 탐욕적으로, 쓴 비트는 재사용하지 않는" 참조와 길이 12 전수로 비교했다.

**동기 리셋.** 리셋 사이클의 입력을 버린다고 정했으므로, `1011` 의 두 번째 비트에서 리셋하면 아무것도 검출되지 않는다. 참조 검출기는 "마지막 4비트 구간 안에 rst 가 없을 때만"으로 정의했고 무작위 2000개 × 40사이클 시나리오에서 Moore·Mealy·registered Mealy 모두 일치. 실제 RTL 에서는 리셋 중 Mealy 출력을 가릴지(`& ~rst`) 말지가 설계 선택이다 — 안 가리면 리셋 사이클에도 출력이 나갈 수 있으니 명세에 적어야 한다.

**registered Mealy == Moore.** `q[k] = MealyOut[k−1]` 이고 `MooreOut[k] = detect(k−1)` 이므로 같다. 그런데도 둘 다 쓰는 이유는 비용 구조가 다르기 때문이다.

| | Moore | Mealy | registered Mealy |
|---|---|---|---|
| 상태 수 (1011) | 5 (FF 이진 3 / one-hot 5) | 4 (FF 2 / 4) | 4 + 출력 FF 1 |
| 출력 시점 (마지막 비트 = 사이클 k) | k+1 | **k** | k+1 |
| 출력 경로 | FF → (디코드) → out | **in → 조합 → out** | FF → out |
| 글리치 | 상태 디코드 글리치만 (보통 출력 FF 로 해결) | 입력 글리치가 그대로 통과 | 없음 |
| 타이밍 (ARCH-B13) | 모듈 경계에서 끊김 | in→out 경로가 앞뒤 모듈 경로와 이어져 critical path 가 길어진다 | 끊김 |

**글리치.** 반 사이클 모델에서 `a = 1 0 1 1 0 0`, `b = 1 0 1 0 1 1` (사이클 3 에서 입력이 늦게 1 → 0):

```svg
<svg viewBox="0 0 760 274" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">반 사이클 모델: 사이클 3 에서 입력이 늦게 1→0 으로 바뀌면 Mealy 출력에 가짜 펄스</text>
<rect x="410" y="34" width="50" height="190" class="f-warn"/>
<line x1="110" y1="34" x2="110" y2="224" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/>
<text x="160.0" y="48" fill="currentColor" text-anchor="middle" font-size="12">cycle 0</text>
<line x1="210" y1="34" x2="210" y2="224" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/>
<text x="260.0" y="48" fill="currentColor" text-anchor="middle" font-size="12">cycle 1</text>
<line x1="310" y1="34" x2="310" y2="224" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/>
<text x="360.0" y="48" fill="currentColor" text-anchor="middle" font-size="12">cycle 2</text>
<line x1="410" y1="34" x2="410" y2="224" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/>
<text x="460.0" y="48" fill="currentColor" text-anchor="middle" font-size="12">cycle 3</text>
<line x1="510" y1="34" x2="510" y2="224" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/>
<text x="560.0" y="48" fill="currentColor" text-anchor="middle" font-size="12">cycle 4</text>
<line x1="610" y1="34" x2="610" y2="224" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/>
<text x="660.0" y="48" fill="currentColor" text-anchor="middle" font-size="12">cycle 5</text>
<line x1="710" y1="34" x2="710" y2="224" stroke="currentColor" stroke-dasharray="3 4" opacity=".45"/>
<text x="10" y="78" fill="currentColor">in (a|b)</text>
<polyline points="110,62 210,62 210,86 310,86 310,62 460,62 460,86 560,86 560,62 610,62 610,86 660,86 660,62 710,62" fill="none" stroke="currentColor" stroke-width="2"/>
<text x="10" y="128" fill="currentColor">Mealy out</text>
<polyline points="110,136 410,136 410,112 460,112 460,136 660,136 660,112 710,112" fill="none" stroke="currentColor" stroke-width="2"/>
<text x="10" y="178" fill="currentColor">Moore out</text>
<polyline points="110,186 710,186" fill="none" stroke="currentColor" stroke-width="2"/>
<text x="435.0" y="72" fill="currentColor" text-anchor="middle" font-size="12">a=1</text>
<text x="485.0" y="72" fill="currentColor" text-anchor="middle" font-size="12">b=0</text>
<text x="10" y="242" fill="currentColor">FF 는 에지 직전 값 b 만 샘플 → 상태는 정상. 하지만 Mealy 조합 출력은 a=1 동안 1.</text>
<text x="10" y="262" fill="currentColor">사이클 5 처럼 입력이 늦게 1 이 되면 펄스가 짧을 뿐 진짜 검출이다.</text>
</svg>
```

```text
half-cycle   a: 1 0 1 1 0 0   b: 1 0 1 0 1 1
Mealy half ____________‾‾________‾‾   glitches=1
```

상태 레지스터는 b 만 보므로 경로는 `1010…` 으로 맞다. 그러나 Mealy 출력은 사이클 3 앞 반에 1 을 냈다. 다음 FF 가 에지에서만 샘플하면 문제없지만(그 순간엔 0), **에지 트리거 인터럽트 입력, 비동기 리셋, 다른 클럭 도메인, 칩 외부 핀**으로 나가면 가짜 이벤트가 된다. 규칙: 블록 밖(특히 비동기 소비자)으로 나가는 출력은 FF 로 끊는다 = Moore 또는 registered Mealy.

### Part C — 임의 패턴 자동 생성과 펌웨어 스캐너

**KMP 오토마톤.** 힌트 1 의 규칙을 그대로 코드로 옮긴 것이 `kmp_build` 다. 상태 s 에서 입력 c 를 붙인 t 의 "접미사 = 접두사" 최장 길이를 k = min(s+1, len−1) 부터 줄이며 찾는다(O(len²)/칸, 16비트 패턴도 32칸이라 충분히 빠르다). 문자열 매칭의 KMP 실패 함수를 쓰면 O(len) 으로 만들 수 있지만, 표 생성은 초기화 때 한 번이다. `1011` 로 만든 표는 손 표와 같고(`same`), 자기겹침이 큰 `111…10` 류 패턴과 1 이 많은 버퍼를 섞은 무작위 400개에서 브루트포스와 불일치 0.

**버퍼 경계.** UART/SPI 수신을 DMA 반쪽 버퍼(half/full transfer 인터럽트)로 받으면 패턴이 두 조각에 걸칠 수 있다. 상태 변수는 스캐너 컨텍스트에 두고 조각 사이에 넘긴다.

```text
sync 0xD391: whole=1, split mismatches=0, (state-reset bug would miss at 2 cut points)
```

`0xD391` 을 비트 오프셋 37~52 (바이트 4~6) 에 심었으므로 분할점 5, 6 에서 조각마다 상태를 0 으로 초기화하는 버전은 놓친다 — 테스트가 정확히 2곳을 잡았다. 무선 칩(예: TI CC1101 계열)은 이 sync word 검출을 하드웨어 FSM 으로 하고 "sync 찾음" 인터럽트를 올린다. 펌웨어가 같은 일을 할 때는 이 스캐너가 된다.

**바이트 표.** 비트 루프는 바이트당 8번 표를 찾는다. `(상태, 바이트) → (다음 상태, 검출 수)` 를 미리 만들면 1번. 메모리는 `len × 256 × 2` 바이트: `1011` 이면 2 KB, 16비트 패턴이면 8 KB — Cortex-M0 의 작은 SRAM 에선 플래시(const)에 두거나 4비트(니블) 표(`len × 16 × 2`)로 타협한다. CRC 의 바이트 표 계산과 같은 발상이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so          # 24/24 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 24/24, 경고 0
```

**demo_vcd.c — 같은 파형을 VCD 로.** 실리콘 쪽 사람들은 텍스트 파형보다 파형 뷰어를 본다. VCD(Value Change Dump, IEEE 1364)는 RTL 시뮬레이터가 내는 표준 텍스트 포맷이라 GTKWave/Surfer 에서 그대로 열린다 (zero-delay 단순화: 값은 에지 시각에 바뀐다).

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_vcd.c -o vcd && ./vcd /tmp/esn_ARCH-B07/b07.vcd
```

```text
wrote /tmp/esn_ARCH-B07/b07.vcd: 13 cycles, 91 value records (뷰어: gtkwave /tmp/esn_ARCH-B07/b07.vcd)
$timescale 1ns $end
$scope module b07 $end
$var wire 1 ! clk $end
...
#30
1!
1"
b011 #
0$
b011 %
1&
0'
#35
0!
#40
1!
0"
b100 #
1$
b001 %
0&
1'
```

t=30 ns(사이클 3): `mealy_out`(`&`)=1, `moore_out`(`$`)=0. t=40 ns(사이클 4): Moore 상태 `b100`(S4), `moore_out`=1, `mealy_q`(`'`)=1. (이 맥에는 GTKWave 가 없어 뷰어 화면은 확인하지 않았다.)

**같은 Mealy 를 Verilog 로 쓰면** (참고용 — 이 맥에 iverilog 가 없어 실행하지 않았다):

```verilog
module det1011_mealy(input clk, input rst, input din, output y);
  reg [1:0] s, ns;
  always @(posedge clk) s <= rst ? 2'd0 : ns;        // 비블로킹 <= : 에지에서 한꺼번에 커밋
  always @* begin
    case (s)
      2'd0:    ns = din ? 2'd1 : 2'd0;
      2'd1:    ns = din ? 2'd1 : 2'd2;
      2'd2:    ns = din ? 2'd3 : 2'd0;
      default: ns = din ? 2'd1 : 2'd2;                 // S3
    endcase
  end
  assign y = (s == 2'd3) & din & ~rst;                 // din 이 조합으로 바로 출력 → Mealy
endmodule
```

C 의 `simulate` 와 한 줄씩 대응한다: `always @*` = phase 1, `always @(posedge clk) ... <=` = phase 2. Moore 로 바꾸려면 상태를 3비트로 늘리고 `assign y = (s == 3'd4);`.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 상태를 먼저 갱신하고 출력 계산 (`S = next(S,in); out = g(S,in);`) | Mealy 는 S_next==S3 ∧ in==1 ⇔ "101" 이 막 끝남 → **101 을 검출**. Moore 는 입력과 같은 사이클에 출력 (in→out 조합 경로가 생김) | `out = g(S,in); next = ...; S = next;` (아래 demo 출력) |
| S3 --0--> S0 | "1010" 의 접미사 "10" 은 패턴 접두사 → S2 여야 한다. `101011` 을 놓친다 | 최장 접미사=접두사 규칙으로 표를 만든다 |
| Moore S4 --1--> S0 또는 S4 --0--> S0 (겹침인데) | `1011011`/`10110 11` 의 두 번째 검출을 놓친다 | S4: 0→S2, 1→S1 |
| "Mealy 가 1사이클 빠르니 항상 Mealy" | in→out 조합 경로가 다음 블록 경로와 이어져 Fmax 가 떨어지고(ARCH-B13), 입력 글리치가 출력으로 나간다 | 블록 경계·비동기 소비자 앞에선 출력 FF |
| 테스트를 `MooreOut[k+1]==MealyOut[k]` 하나만 | 둘 다 항상 0 이어도 통과한다 (starter 에서 `bad_shift == 0` 이 PASS 하는 이유) | 참조 검출기와의 비교를 같이 |
| 스트림을 조각으로 받을 때 조각마다 `state = 0` | 경계에 걸친 패턴을 놓친다 (분할점 2곳) | 상태를 컨텍스트에 두고 이어서 |
| 이진 인코딩 3비트 Moore 에서 S5~S7 무시 | 방사선 SEU·전원 글리치로 들어가면 영영 못 빠져나올 수 있다 | `default: ns = S0` (safe FSM), 합성 옵션 확인 |

`demo_inplace_bug.c` 실행 결과 (정답: Mealy `3,6,10,` / Moore `4,7,11,`):

```text
wrong Moore (out from S_next): 3,6,10,
wrong Mealy (out from S_next): 2,5,9,
```

Moore 버그는 "Mealy 와 같은 타이밍"이라 맞아 보이는 게 더 위험하다 — 레지스터 출력이어야 할 것이 조합 출력이 된 것이다.

## 8. 면접 Q&A

<details>
<summary>Q1. Moore 와 Mealy 의 차이는? 어느 쪽이 빠른가요?</summary>

Moore 는 출력 = f(상태), Mealy 는 출력 = g(상태, 입력). 같은 입력열에서 Mealy 는 마지막 비트가 들어온 그 사이클에, Moore 는 다음 사이클에 출력한다. **꼬리질문: "1사이클 빠르다"는 게 하류 블록 입장에서 진짜 이득인가?** — 하류가 같은 클럭으로 샘플하면 Mealy 출력은 edge k+1 에서 잡히고 Moore 출력은 edge k+2 에서 잡히므로 한 사이클 이득은 맞다. 대신 그 값은 사이클 k 의 *늦은* 시점에야 안정되므로(입력 도착 + 조합 지연), in→out 경로가 timing critical path 가 될 수 있다.

</details>

<details>
<summary>Q2. 1011 검출기에 상태가 몇 개 필요하고 FF 는 몇 개인가요?</summary>

Mealy 4개(FF 2), Moore 5개(이진 FF 3, one-hot 5). 일반적으로 n비트 패턴은 Mealy n개, Moore n+1개. **꼬리질문: one-hot 이 FF 를 더 쓰는데 FPGA 에선 왜 선호하나?** — 다음 상태 식이 짧은 AND-OR 하나씩이라(연습 1) LUT 깊이가 얕고 빠르며, FPGA 는 FF 가 넉넉하다. ASIC 에선 면적 때문에 이진/Gray 도 많이 쓴다. Gray 는 인접 전이에서 1비트만 바뀌어 전력·디코드 글리치가 줄어든다.

</details>

<details>
<summary>Q3. 겹침 허용과 비겹침은 설계에서 무엇이 다른가요?</summary>

검출 전이의 목적지 하나만 다르다. 겹침: 패턴 자기 자신의 최장 border(접미사=접두사, 1011 은 "1") 상태로, 비겹침: S0 로. **꼬리질문: 1111 이면?** — border "111" → 겹침 검출 후 S3 로 가서 1 이 계속 오면 매 사이클 검출. `11111` 에서 겹침 2번, 비겹침 1번.

</details>

<details>
<summary>Q4. Mealy 출력의 글리치는 왜 생기고 언제 문제가 되나요?</summary>

출력이 입력의 조합 함수라서 입력이 사이클 중간에 흔들리면(다른 FF 의 clk→q 차이, 조합 경로 해저드) 출력도 흔들린다. 같은 클럭 도메인의 FF 가 에지에서만 샘플하면 setup 만 맞으면 무해하다. 문제는 에지 트리거 인터럽트, 비동기 리셋/클리어, 클럭 게이팅 enable, 다른 클럭 도메인, 칩 외부 핀 — 가짜 펄스가 이벤트가 된다. **꼬리질문: 해결은?** — 출력 FF(registered Mealy = Moore 타이밍), 또는 Moore 로 설계.

</details>

<details>
<summary>Q5. registered Mealy 와 Moore 가 출력이 같다면 왜 둘 다 존재하나요?</summary>

출력 파형은 같지만(전수 테스트 `bad_q == 0`) 구조가 다르다. registered Mealy 는 상태 4개 + 출력 FF 1개, Moore 는 상태 5개 + 출력 디코드. 출력이 여러 개이거나 상태 인코딩과 출력 비트를 일치시키고 싶을 때(Moore 상태 비트를 그대로 출력으로 — 디코드 글리치도 없음) Moore 가, 상태 수를 줄이고 싶을 때 registered Mealy 가 유리하다. 합성 도구가 결국 비슷한 회로로 만들기도 한다.

</details>

<details>
<summary>Q6. 이진 인코딩 Moore 는 3비트인데 상태는 5개입니다. 나머지 3개 코드는요?</summary>

정상 동작에선 도달 불가지만, SEU(방사선 비트 플립), 전원·클럭 글리치, 리셋 해제 타이밍 문제로 들어갈 수 있다. `case` 에 `default` 를 두지 않거나 합성기가 "도달 불가"로 최적화하면 거기서 못 빠져나오는 lock-up 이 생긴다. 자동차·우주용은 safe FSM(불법 상태 → 리셋 상태)이나 해밍 인코딩 상태를 요구하기도 한다. 실리콘 검증에서는 "불법 상태 강제 주입 후 복귀" 가 테스트 항목이 된다.

</details>

<details>
<summary>Q7. 펌웨어에서 UART 바이트 스트림에서 sync 패턴을 찾는다면 어떻게 짜나요?</summary>

같은 Mealy 표를 소프트웨어로 돌린다. 핵심 세 가지: ① 상태를 호출 사이에 보존(ISR/DMA 콜백마다 새로 시작하면 경계에 걸친 패턴을 놓친다) ② 바이트 단위 표로 바이트당 한 번 조회(`len×256×2` B, 메모리가 빠듯하면 니블 표) ③ 상태는 ISR 과 태스크가 공유하면 race — 한쪽(보통 태스크 또는 ISR 하나)에서만 스캔하거나 컨텍스트를 분리. **꼬리질문: 바이트 정렬된 패턴이면?** — 비트 단위 FSM 대신 바이트 단위 상태 머신(헤더 파서)으로 충분하다. 비트 단위가 필요한 건 무선 프리앰블·sync 처럼 바이트 경계가 아직 없을 때.

</details>

<details>
<summary>Q8. 시뮬레이터에서 왜 "계산 → 커밋" 두 단계가 필요한가요? Verilog 로 치면?</summary>

하드웨어의 모든 FF 는 같은 에지에 *에지 이전* 값을 보고 바뀐다. 한 변수를 먼저 덮어쓰고 그 값으로 다른 걸 계산하면 "이번 사이클에 이미 바뀐 값"을 보는 셈이라 다른 회로가 된다 (§7 데모: 101 검출). Verilog 에서 순차 블록에 비블로킹 `<=` 를 쓰는 이유가 같다 — 블로킹 `=` 로 쓰면 시뮬레이션과 합성 결과가 달라지는 고전적 버그가 난다.

</details>

## 9. 추가 연습

### 연습 1 — one-hot Moore 의 다음 상태 식

Moore 1011 을 FF 5개(q0..q4, 리셋 `00001`)로 만들 때 각 FF 의 D 입력 식을 쓰고, 표 버전과 길이 1~14 전수로 비교하라.

<details>
<summary>답 보기</summary>

"어느 상태에서 어떤 입력으로 이 상태에 들어오나"를 OR 로 모은다 (전이표를 열 기준으로 읽기).

- d0 = !in & (q0 | q2), d1 = in & (q0 | q1 | q4), d2 = !in & (q1 | q3 | q4), d3 = in & q2, d4 = in & q3, 출력 = q4.

각 식이 AND 하나 + OR 하나 — FPGA LUT 한 단에 들어간다. 매 사이클 `q == 1 << s` (정확히 한 비트) 인지도 같이 검사한다.

</details>

### 연습 2 — 이진 인코딩 Mealy 의 식

S0=00, S1=01, S2=10, S3=11 로 인코딩해 s1', s0', out 을 곱의 합(SOP)으로 구하라.

<details>
<summary>답 보기</summary>

다음 상태가 S1 또는 S3(s0'=1)인 경우는 입력이 1 인 전이 전부다 → **s0' = in**. s1'=1 은 (S1,0)→S2, (S2,1)→S3, (S3,0)→S2 → `s1' = !in·s0 + in·s1·!s0`. 출력 `out = in·s1·s0`. 인코딩을 잘 고르면 식이 짧아진다는 예 — 상태 할당(state assignment)이 면적·속도를 바꾼다.

</details>

### 연습 3 — 상승 에지 검출기: Mealy vs Moore

입력의 0→1 변화마다 1사이클 펄스를 내는 검출기를 Mealy(FF 1개: `prev`)와 Moore(LOW / EDGE / HIGH 3상태)로 만들고, Moore 펄스가 정확히 1사이클 늦음을 전수로 보여라.

<details>
<summary>답 보기</summary>

Mealy: `pulse = in & !prev; prev' = in`. Moore: LOW --1--> EDGE(출력 1) --1--> HIGH, 어느 상태든 0 이면 LOW. 펌웨어의 버튼 "눌림 이벤트", GPIO 에지 인터럽트 로직, 동기화기 뒤의 에지 검출이 모두 이 회로다. 비동기 입력이면 앞에 2단 동기화기(2-FF synchronizer)를 먼저 둔다.

</details>

### 연습 4 — 디바운서 (Moore)

버튼 샘플을 받아 같은 값이 N=3 번 연속일 때만 출력 레벨을 바꾸는 Moore FSM(상태 = 레벨 + 카운트)을 만들라. 출력이 바뀐 사이클 직전 3개 입력은 모두 새 레벨이어야 한다.

<details>
<summary>답 보기</summary>

출력 = 현재 `level`(상태만 봄 → 바운스 중에도 출력이 흔들리지 않는다). 입력이 level 과 같으면 cnt=0, 다르면 cnt++, cnt==N 이면 level 반전. 실행 결과 (`deb in` / `deb out`):

```text
deb in : 0101111101110000
deb out: 0000001111111110
```

1 이 사이클 3·4·5 에 연속 → 사이클 6 부터 1. 8 의 0 한 번은 무시. 0 이 12·13·14 → 15 부터 0. 실제 펌웨어는 1~10 ms 주기 타이머 ISR 에서 이 FSM 을 돌린다.

</details>

### 연습 5 — 1001 의 Mealy 표

`1001`(겹침)의 4상태 Mealy 표를 손으로 쓰고 참조 검출기와 전수 비교하라.

<details>
<summary>답 보기</summary>

S0 {0→S0, 1→S1}, S1 "1" {0→S2, 1→S1}, S2 "10" {0→S3, 1→S1 ("101"→"1")}, S3 "100" {0→S0 ("1000"), 1→S1 / 출력 1 (border "1")}. `1001001` → 겹침 2번. Part C 의 `kmp_build(0x9, 4)` 가 같은 표를 낸다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   # 8/8 passed, exit 0
```

## 10. 핵심 정리

- 상태 = "지금까지 맞은 패턴 접두사의 길이". 틀리면 "본 문자열의 접미사 중 가장 긴 접두사"로 (KMP 오토마톤).
- Moore: 출력 = f(상태), 1011 에 5상태, 마지막 비트의 **다음 사이클**에 출력. Mealy: 출력 = g(상태, 입력), 4상태, **같은 사이클**.
- Moore 출력 = Mealy 출력을 FF 하나로 지연한 것 = registered Mealy (전수 테스트로 확인).
- 겹침/비겹침은 검출 전이의 목적지 하나 차이 (border 상태 vs S0).
- Mealy 의 대가: in→out 조합 경로(타이밍, ARCH-B13)와 입력 글리치 통과. 블록 밖·비동기 소비자 앞에는 출력 FF.
- 시뮬레이터는 "현재 값으로 계산 → 에지에서 한꺼번에 커밋" (= Verilog `<=`). 순서를 바꾸면 101 을 검출한다.
- 펌웨어 스캐너: 상태를 버퍼 경계 너머로 보존, 바이트 표(`len×256×2` B)로 바이트당 조회 1번.
- 이진 인코딩의 미사용 코드는 safe FSM 으로 복귀시킨다.

## 참고자료

- Harris & Harris, *Digital Design and Computer Architecture, ARM Edition* — 3.4 Finite State Machines (Moore/Mealy, 상태 인코딩, 패턴 인식기 예제)
- Patterson & Hennessy, *Computer Organization and Design, ARM Edition* — Appendix (Logic Design: 유한 상태 기계, 클로킹)
- Clifford E. Cummings, "Nonblocking Assignments in Verilog Synthesis, Coding Styles That Kill!" (SNUG 2000) — 비블로킹 `<=` 와 두 단계 갱신
- Clifford E. Cummings, "The Fundamentals of Efficient Synthesizable Finite State Machine Design using NC-Verilog and BuildGates" (ICU 2002) — FSM 코딩 스타일, registered 출력
- Knuth, Morris, Pratt, "Fast Pattern Matching in Strings", SIAM J. Comput. 6(2), 1977 — 패턴 오토마톤 / 실패 함수
- IEEE Std 1364 (Verilog) — VCD(Value Change Dump) 포맷
- 관련 레슨: ARCH-B06 (FF·두 단계 갱신·LFSR), ARCH-B13 (setup/hold, 조합 경로와 Fmax), C-B01 (비트 연산)
