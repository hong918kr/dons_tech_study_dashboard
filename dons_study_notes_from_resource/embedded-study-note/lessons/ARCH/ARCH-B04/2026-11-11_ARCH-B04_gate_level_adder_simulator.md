---
id: ARCH-B04
time: 70분
updated: 2026-11-11
---
# ARCH-B04 게이트 수준 가산기 시뮬레이터

## 학습 목표

- AND/OR/XOR 게이트 함수만으로 **반가산기 → 전가산기 → n비트 리플 캐리 가산기(ripple-carry adder)** 를 만들고, 8비트 전수(65536쌍 × cin 2) 테스트로 C 의 `+` 와 같음을 보인다.
- generate/propagate(`g = a·b`, `p = a⊕b`)로 carry 식을 펼쳐 **4비트 블록 캐리 룩어헤드 가산기(carry-lookahead adder, CLA)** 를 만들고, 같은 전수 테스트를 통과시킨다.
- 게이트마다 "도착 시각"을 들고 다니는 모델로 **임계 경로 깊이(gate-delay levels)와 게이트 수** 를 실제로 재서, 리플 17단 vs CLA 6단(8비트)과 `2n+1` vs `2(n/4)+2` 공식을 설명한다.
- fan-in 이 큰 게이트를 2입력 게이트 트리로 바꾸면 CLA 의 이득이 얼마나 줄어드는지(6 → 9단) 말할 수 있다.
- 덧셈기 하나로 ADD/ADC/SUB/SBC 를 만드는 ALU 슬라이스와, 8비트 MCU·Cortex-M 이 넓은 덧셈을 **ADD + ADC 사슬** 로 하는 이유(= carry 가 슬라이스 사이를 시간 축으로 리플)를 연결한다.

## 1. 문제

덧셈기의 carry 사슬은 ALU 의 **임계 경로(critical path)** 이고, 임계 경로가 클럭 주파수(Fmax)를 정한다 — 그래서 실리콘·펌웨어 면접에서 "리플 vs 룩어헤드 지연이 몇 단인가", "뺄셈은 덧셈기로 어떻게 하나", "C 플래그는 하드웨어 어디서 나오나" 가 단골로 나온다.

이 레슨은 "게이트 수준 시뮬레이터"를 C 로 만든다. 핵심 아이디어: **신호 하나 = 값 + 도착 시각**.

| 필드 | 뜻 |
|---|---|
| `v` | 논리 값 0/1 |
| `t` | 단위 지연 모델(unit-delay model): 게이트 하나를 지날 때마다 +1, 입력 수(fan-in)와 무관. 출력 시각 = max(입력 시각) + 1 |
| `t2` | 같은 게이트를 **2입력 게이트 트리** 로 바꿨을 때의 시각 (가장 일찍 도착한 두 입력부터 묶는 최적 트리) |

입력 비트는 시각 0 에 도착한다. 게이트 함수 `AND(...)`, `OR(...)`, `XOR(...)`, `NOT(x)` (주어짐) 를 부를 때마다 전역 카운터가 올라가므로, **덧셈을 한 번 돌리면 결과 값 + 게이트 수 + 각 출력의 도착 시각** 이 같이 나온다. 이건 STA(정적 타이밍 분석)의 아주 작은 버전이다: 도착 시각은 데이터 값과 무관하게 회로 구조만으로 정해진다 (그래서 TODO 안에서 `if (c.v)` 처럼 **값으로 분기하면 안 된다** — 회로는 값에 따라 모양이 바뀌지 않는다).

규약: XOR 도 1단으로 센다 (교과서 단위 지연 관례. 실제 CMOS XOR 는 NAND 보다 크고 느려서 1.5~2단 정도로 보기도 한다). NOT 은 1단. 와이어 지연과 fan-out 부하는 무시한다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">전가산기 = 반가산기 2개 + OR 1개 (괄호 안은 도착 시각 t, 입력은 t=0)</text>
  <g fill="currentColor">
    <text x="16" y="70">a</text><text x="16" y="100">b</text><text x="16" y="190">cin</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M40 66 H90"/><path d="M40 96 H90"/>
    <path d="M240 100 L320 136"/>
    <path d="M240 66 L550 56"/>
    <path d="M470 180 H700"/>
    <path d="M640 66 H700"/>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="3">
    <path d="M50 186 H320"/>
    <path d="M470 150 L550 78"/>
  </g>
  <rect x="90" y="46" width="150" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <rect x="320" y="116" width="150" height="84" rx="6" class="f-mut" stroke="currentColor"/>
  <rect x="550" y="40" width="90" height="50" rx="6" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="102" y="66" font-weight="bold">반가산기 1</text>
    <text x="102" y="86" font-family="monospace" font-size="12">s1 = a XOR b (1)</text>
    <text x="102" y="104" font-family="monospace" font-size="12">c1 = a AND b (1)</text>
    <text x="332" y="138" font-weight="bold">반가산기 2</text>
    <text x="332" y="160" font-family="monospace" font-size="12">s  = s1 XOR cin (2)</text>
    <text x="332" y="180" font-family="monospace" font-size="12">c2 = s1 AND cin (2)</text>
    <text x="568" y="62" font-weight="bold">OR</text>
    <text x="560" y="80" font-family="monospace" font-size="12">c1 + c2</text>
    <text x="650" y="58">cout (3)</text>
    <text x="650" y="172">s (2)</text>
  </g>
  <text x="10" y="232" fill="currentColor">굵은 선 = carry 경로: cin → AND → OR = 2단. 비트마다 2단씩 쌓이므로 n비트 리플은 첫 g/p 1단 + 2n = 2n+1 단.</text>
  <text x="10" y="254" fill="currentColor">게이트 5개 (XOR 2, AND 2, OR 1). s1 이 곧 propagate p, c1 이 곧 generate g — Part B 의 재료가 이미 들어 있다.</text>
</svg>
```

### Part A — 반가산기 → 전가산기 → 리플 캐리

`sig` 와 게이트 함수만으로 (TODO 안에 `+`, `^`, `&` 같은 C 산술/비트 연산 금지 — 게이트를 불러라):

- `half_add(a, b, &s, &c)` — `s = a XOR b`, `c = a AND b`.
- `full_add(a, b, cin, &s, &cout)` — 반가산기 2개 + OR. 게이트 정확히 5개, `s` 는 2단, `cout` 은 3단.
- `ripple_add(a[], b[], cin, s[], n)` — n비트, 비트 i 의 carry-out 이 비트 i+1 의 carry-in. 마지막 carry-out 을 돌려준다.
- 검증: 8비트 **전수 65536쌍 × cin{0,1}** 을 C 의 `a + b + cin` 과 비교. 8비트 리플 = 게이트 40개, 깊이 17 (s7 은 16, cout 은 17).

### Part B — 4비트 블록 캐리 룩어헤드

- `cla4(g[4], p[4], c0, c[5], &G, &P)` — `c[i+1] = g_i + p_i·c_i` 를 **펼쳐서** c1~c4 를 모두 g/p/c0 만으로 (2단 AND-OR) 만든다. c3 을 써서 c4 를 만들면 안 된다 (그러면 리플). G/P 포인터가 NULL 이 아니면 블록 generate `G` 와 블록 propagate `P` 도 만든다.
- `cla_add(a[], b[], cin, s[], n)` — n 은 4의 배수. 비트마다 `g = AND(a,b)`, `p = XOR(a,b)`, 4비트 블록마다 `cla4`, `s = XOR(p, c)`. 블록 사이는 블록의 c4 가 다음 블록의 c0 (블록 리플).
- 검증: `cla4` 는 g·p·c0 의 2^9 조합 전수를 반복식과 비교, `cla_add` 는 8비트 전수. 8비트 CLA = 게이트 52개, 깊이 6 (cout 5), 2입력 환산 깊이 9 (리플은 17 그대로).

### Part C — ALU 슬라이스와 다중 정밀도 덧셈

- `alu8(a, b, cin, sub, &cout)` — 비트마다 `b' = b XOR sub`, 결과 `a + b' + cin` (cla_add 사용). ADD(sub 0, cin 0) · ADC(sub 0, cin C) · SUB(sub 1, cin 1) · SBC(sub 1, cin C). Arm 처럼 **C = NOT borrow** (ARCH-B02 참고).
- `mp_addsub(a[], b[], r[], nbytes, sub)` — 리틀 엔디언 바이트 배열. 첫 바이트는 ADD/SUB, 나머지는 ADC/SBC 로 carry 하나만 넘긴다 (8비트 MCU 가 32비트 덧셈을 하는 방식).
- 검증: alu8 은 4가지 모드 × 65536쌍 전수, mp_addsub 는 32/64비트에서 경계값 11×11 + xorshift 2만 쌍을 `uint32_t`/`uint64_t` 연산과 비교.
- 관찰: `demo_carry_asm.c` — Cortex-M4 의 64비트 덧셈이 `adds`/`adcs` 두 개, arm64 의 128비트 덧셈이 `adds`/`adc` 두 개로 나오는 것을 실제 `-S` 로 본다.

## 2. Problem (English)

> **Gate-level adder simulator.**
>
> You are given a signal type `sig { v, t, t2 }` (logic value, unit-delay arrival time, 2-input-tree arrival time) and gate primitives `AND(...)`, `OR(...)`, `XOR(...)`, `NOT(x)` that count gates and propagate arrival times. Using **only these gates** (no C arithmetic on the values):
>
> 1. Implement `half_add`, `full_add` (two half adders + one OR), and an n-bit `ripple_add`. Verify the 8-bit adder against `a + b + cin` for all 65,536 operand pairs and both carry-ins.
> 2. Implement a 4-bit carry-lookahead unit `cla4` (carries c1..c4 as flattened two-level AND-OR expressions of g, p, c0; optional group G/P), and an n-bit block CLA `cla_add` (n multiple of 4, block carries ripple between blocks). Verify exhaustively.
> 3. Report gate count and critical-path depth for both 8-bit adders. Expected: ripple 40 gates / 17 levels, block CLA 52 gates / 6 levels (9 levels if every gate is limited to 2 inputs).
> 4. Build an 8-bit ALU slice supporting ADD/ADC/SUB/SBC with Arm carry semantics (C = NOT borrow), then a multi-precision byte-wise add/sub that chains the carry like `ADD; ADC; ADC; ADC`.
>
> Example: `ripple_add(0xFF, 0x01, cin=0)` → sum `0x00`, carry-out `1`. `alu8(0x00, 0x01, cin=1, sub=1)` → `0xFF`, carry `0` (borrow happened).

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
```

처음 실행 (실제 출력, 줄여서):

```text
FAIL starter.c:173  ha_ok
FAIL starter.c:180  fa_ok
...
FAIL starter.c:255  R[0] == 0xFF && R[3] == 0xFF && c == 0

adder       gates gates2in   depth depth2in
ripple          0        0       0        0
cla4-blk        0        0       0        0
...
2/19 passed
```

통과하는 2개는 주어진 게이트 모델 자체의 테스트다 (NOT 진리표, 5입력 AND 의 시각 계산).

## 4. 힌트

<details>
<summary>힌트 1 — 전가산기는 반가산기 두 번</summary>

첫 반가산기: `(a, b) → (s1, c1)`. 두 번째: `(s1, cin) → (s, c2)`. carry-out 은 `c1 OR c2` (둘이 동시에 1이 될 수는 없어서 XOR 로 써도 값은 같지만 관례는 OR).

</details>

<details>
<summary>힌트 2 — 리플은 carry 변수 하나를 돌려 쓴다</summary>

`sig c = cin; for (i...) full_add(a[i], b[i], c, &s[i], &c);` — `full_add` 의 cout 출력 포인터와 cin 입력이 같은 변수여도 된다 (값으로 받은 뒤 쓰므로).

</details>

<details>
<summary>힌트 3 — carry 식을 펼치는 법</summary>

`c1 = g0 + p0·c0` 를 `c2 = g1 + p1·c1` 에 대입하면 `c2 = g1 + p1·g0 + p1·p0·c0`. 같은 식으로 `c3`, `c4` 를 끝까지 펼친다. 항 하나 = AND 게이트 하나, 전체 = OR 하나. 그래서 c1~c4 는 모두 "AND 1단 + OR 1단" 이다.

</details>

<details>
<summary>힌트 4 — 블록 G, P</summary>

`G` = "이 블록 안에서 carry 가 생겨서 밖으로 나간다" = c4 식에서 c0 항만 뺀 것. `P` = "들어온 carry 가 그대로 통과한다" = `p3·p2·p1·p0`. 그러면 `c4 = G + P·c0` — 한 단계 위의 룩어헤드가 같은 cla4 를 그대로 쓸 수 있다 (추가 연습 2).

</details>

<details>
<summary>힌트 5 — 뺄셈 = 반전 + 1</summary>

`a − b = a + (~b) + 1`. `~b` 는 비트마다 `XOR(b_i, sub)` (sub=1 이면 반전, 0 이면 그대로), `+1` 은 cin 으로 넣는다. SBC 는 cin 자리에 이전 carry 를 넣으면 끝.

</details>

## 5. 풀이

### Part A — 반가산기, 전가산기, 리플

| (a, b) | g = ab | p = a⊕b | s | cout | 읽는 법 |
|---|---|---|---|---|---|
| (0, 0) | 0 | 0 | cin | 0 | carry 죽임(kill) |
| (0, 1) 또는 (1, 0) | 0 | 1 | ¬cin | cin | 들어온 carry 를 통과(propagate) |
| (1, 1) | 1 | 0 | cin | 1 | carry 생성(generate) |

`s = a ⊕ b ⊕ cin`, `cout = a·b + cin·(a ⊕ b)`. 반가산기 1 의 출력 s1, c1 이 바로 propagate·generate 다.

도착 시각 (입력 t=0):

| 신호 | 식 | t |
|---|---|---|
| p = s1 | XOR(a, b) | 1 |
| g = c1 | AND(a, b) | 1 |
| s | XOR(p, cin) | max(1, t_cin) + 1 |
| c2 | AND(p, cin) | max(1, t_cin) + 1 |
| cout | OR(g, c2) | max(1, t_cin) + 2 |

비트 0 은 t_cin = 0 이라 cout = 3. 그 다음부터는 cin 이 늦게(≥3) 도착하므로 **비트마다 +2**: c1=3, c2=5, …, c8 = 2·8+1 = **17**. 합 비트 s_i 는 c_i 보다 1 늦다 → s7 = 16. 측정 결과(solution 출력)가 정확히 그렇다:

```text
arrival s0..s7,cout  ripple: 2 4 6 8 10 12 14 16 | 17
```

게이트 수는 비트당 5개 → 8비트 40개. 리플은 **작고(O(n) 게이트) 느리다(O(n) 지연)**.

전수 테스트: 8비트는 256×256 = 65536쌍이라 cin 두 값까지 131072번 돌려도 0.1초 남짓이다 (`time ./so` → 0.14 s user, Apple M6). 32비트 이상은 전수가 불가능하므로 Part C 처럼 경계값 + 결정적 난수로 간다.

### Part B — generate / propagate 와 룩어헤드

carry 점화식 `c_{i+1} = g_i + p_i·c_i` 는 "carry 는 여기서 생기거나(g), 아래에서 와서 통과한다(p)" 는 뜻이다. 이걸 끝까지 펼치면 모든 carry 가 **입력 g, p, c0 만의 2단 AND-OR 식** 이 된다:

```text
c1 = g0 + p0·c0
c2 = g1 + p1·g0 + p1·p0·c0
c3 = g2 + p2·g1 + p2·p1·g0 + p2·p1·p0·c0
c4 = g3 + p3·g2 + p3·p2·g1 + p3·p2·p1·g0 + p3·p2·p1·p0·c0
G  = g3 + p3·g2 + p3·p2·g1 + p3·p2·p1·g0          (c4 에서 c0 항만 뺀 것)
P  = p3·p2·p1·p0                                   →  c4 = G + P·c0
```

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">8비트 리플 vs 4비트 블록 CLA — carry 가 지나가는 길 (숫자 = 도착 시각)</text>
  <text x="10" y="56" fill="currentColor" font-weight="bold">리플</text>
  <g stroke="currentColor" class="f-mut">
    <rect x="80" y="38" width="54" height="34" rx="4"/><rect x="160" y="38" width="54" height="34" rx="4"/>
    <rect x="240" y="38" width="54" height="34" rx="4"/><rect x="320" y="38" width="54" height="34" rx="4"/>
    <rect x="400" y="38" width="54" height="34" rx="4"/><rect x="480" y="38" width="54" height="34" rx="4"/>
    <rect x="560" y="38" width="54" height="34" rx="4"/><rect x="640" y="38" width="54" height="34" rx="4"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="107" y="60">FA7</text><text x="187" y="60">FA6</text><text x="267" y="60">FA5</text><text x="347" y="60">FA4</text>
    <text x="427" y="60">FA3</text><text x="507" y="60">FA2</text><text x="587" y="60">FA1</text><text x="667" y="60">FA0</text>
    <text x="147" y="92">15</text><text x="227" y="92">13</text><text x="307" y="92">11</text><text x="387" y="92">9</text>
    <text x="467" y="92">7</text><text x="547" y="92">5</text><text x="627" y="92">3</text>
    <text x="60" y="92">cout 17</text>
  </g>
  <g stroke="currentColor" stroke-width="2.5" fill="none">
    <path d="M640 55 H614"/><path d="M560 55 H534"/><path d="M480 55 H454"/><path d="M400 55 H374"/>
    <path d="M320 55 H294"/><path d="M240 55 H214"/><path d="M160 55 H134"/><path d="M80 55 H50"/>
  </g>
  <text x="10" y="120" fill="currentColor" font-size="12">carry 가 FA 를 하나씩 지나며 2단씩 늘어난다 → 8비트 17단, 32비트 65단, 64비트 129단</text>
  <text x="10" y="160" fill="currentColor" font-weight="bold">CLA</text>
  <g stroke="currentColor" class="f-mut">
    <rect x="80" y="146" width="60" height="28" rx="4"/><rect x="150" y="146" width="60" height="28" rx="4"/>
    <rect x="220" y="146" width="60" height="28" rx="4"/><rect x="290" y="146" width="60" height="28" rx="4"/>
    <rect x="400" y="146" width="60" height="28" rx="4"/><rect x="470" y="146" width="60" height="28" rx="4"/>
    <rect x="540" y="146" width="60" height="28" rx="4"/><rect x="610" y="146" width="60" height="28" rx="4"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="110" y="165">g7 p7</text><text x="180" y="165">g6 p6</text><text x="250" y="165">g5 p5</text><text x="320" y="165">g4 p4</text>
    <text x="430" y="165">g3 p3</text><text x="500" y="165">g2 p2</text><text x="570" y="165">g1 p1</text><text x="640" y="165">g0 p0</text>
  </g>
  <text x="680" y="165" fill="currentColor" font-size="12">t=1</text>
  <rect x="80" y="196" width="270" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <rect x="400" y="196" width="270" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="215" y="218">cla4: c5 c6 c7 c8 동시에 → t=5</text>
    <text x="535" y="218">cla4: c1 c2 c3 c4 동시에 → t=3</text>
  </g>
  <path d="M400 213 H350" stroke="currentColor" stroke-width="2.5" fill="none"/>
  <text x="355" y="250" fill="currentColor" font-size="12">c4 (t=3)</text>
  <g fill="currentColor" font-size="12">
    <text x="80" y="270">s = p XOR c → s5..s7 t=6 (s4 는 c4 를 바로 써서 4)</text>
    <text x="400" y="270">s0=2, s1..s3 = 4</text>
  </g>
  <text x="10" y="306" fill="currentColor">블록 안은 c1~c4 가 같은 시각(AND 1단 + OR 1단). 블록 사이만 c4 → c0 로 리플: 블록당 +2 → 8비트 6단, 32비트 18단.</text>
</svg>
```

도착 시각을 손으로 따라가면:

| 단계 | 신호 | t |
|---|---|---|
| 1 | 모든 g_i, p_i | 1 |
| 2 | 블록 1 의 AND 항 (`p0·c0`, `p1·g0`, …) | 2 |
| 3 | 블록 1 의 c1~c4 (OR) | 3 |
| 4 | 블록 1 의 s1~s3 = p XOR c, 블록 2 의 `p·c4` 항 | 4 |
| 5 | 블록 2 의 c5~c8 | 5 |
| 6 | s5~s7 | 6 |

측정:

```text
adder       gates gates2in   depth depth2in
ripple         40       40      17       17
cla4-blk       52       84       6        9
arrival s0..s7,cout  ripple: 2 4 6 8 10 12 14 16 | 17
arrival s0..s7,cout  cla   : 2 4 4 4 4 6 6 6 | 5
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">출력별 도착 시각 (단위 지연, 실측) — 왼쪽 막대 리플, 오른쪽 막대 CLA</text>
  <rect x="470" y="32" width="14" height="14" class="f-warn" stroke="currentColor"/><text x="490" y="44" fill="currentColor" font-size="12">리플</text>
  <rect x="540" y="32" width="14" height="14" class="f-ok" stroke="currentColor"/><text x="560" y="44" fill="currentColor" font-size="12">4비트 블록 CLA</text>
  <path d="M70 250 H710" stroke="currentColor"/>
  <g stroke="currentColor" class="f-warn">
    <rect x="90" y="226" width="24" height="24"/><rect x="160" y="202" width="24" height="48"/>
    <rect x="230" y="178" width="24" height="72"/><rect x="300" y="154" width="24" height="96"/>
    <rect x="370" y="130" width="24" height="120"/><rect x="440" y="106" width="24" height="144"/>
    <rect x="510" y="82" width="24" height="168"/><rect x="580" y="58" width="24" height="192"/>
    <rect x="650" y="46" width="24" height="204"/>
  </g>
  <g stroke="currentColor" class="f-ok">
    <rect x="118" y="226" width="24" height="24"/><rect x="188" y="202" width="24" height="48"/>
    <rect x="258" y="202" width="24" height="48"/><rect x="328" y="202" width="24" height="48"/>
    <rect x="398" y="202" width="24" height="48"/><rect x="468" y="178" width="24" height="72"/>
    <rect x="538" y="178" width="24" height="72"/><rect x="608" y="178" width="24" height="72"/>
    <rect x="678" y="190" width="24" height="60"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="102" y="222">2</text><text x="172" y="198">4</text><text x="242" y="174">6</text><text x="312" y="150">8</text>
    <text x="382" y="126">10</text><text x="452" y="102">12</text><text x="522" y="78">14</text><text x="592" y="54">16</text>
    <text x="640" y="42">17</text>
    <text x="130" y="222">2</text><text x="200" y="198">4</text><text x="270" y="198">4</text><text x="340" y="198">4</text>
    <text x="410" y="198">4</text><text x="480" y="174">6</text><text x="550" y="174">6</text><text x="620" y="174">6</text>
    <text x="690" y="186">5</text>
    <text x="116" y="268">s0</text><text x="186" y="268">s1</text><text x="256" y="268">s2</text><text x="326" y="268">s3</text>
    <text x="396" y="268">s4</text><text x="466" y="268">s5</text><text x="536" y="268">s6</text><text x="606" y="268">s7</text>
    <text x="676" y="268">cout</text>
  </g>
  <text x="10" y="292" fill="currentColor" font-size="12">클럭 주기는 가장 늦은 출력이 정한다: 리플 17단, CLA 6단 — 조합 지연만 보면 약 2.8배 짧다.</text>
</svg>
```

**대가 (공짜가 아니다)**:

1. **게이트 수** — 블록당 g/p/s 12개 + carry 14개(c1:2, c2:3, c3:4, c4:5) = 26개. 8비트 52개 (리플 40). 폭이 커질수록 펼친 식의 항 수가 제곱으로 늘어서, 실제 CLA 는 4비트 블록을 **계층(hierarchy)** 으로 쌓는다.
2. **fan-in** — c4 의 마지막 항은 5입력 AND, 전체는 5입력 OR. 실제 CMOS 에서 4~5입력을 넘는 게이트는 느려서(직렬 트랜지스터) 쪼갠다. `t2` 열이 "모든 게이트를 2입력으로 쪼갰다면" 이다: 5입력 OR = 2입력 OR 3단. 그래서 CLA 6 → **9**, 리플은 원래 2입력뿐이라 17 그대로. 2입력 환산 게이트 수도 52 → 84.
3. 그래도 9 vs 17 — 그리고 폭이 커질수록 차이가 벌어진다 (추가 연습 3: 64비트 리플 129단 vs 블록 CLA 34단, 2단 계층이면 더 줄어든다).

`t2` 계산 방식: n입력 게이트를 2입력 트리로 바꿀 때, **가장 일찍 도착한 두 입력을 먼저 묶는(max+1)** 것을 하나 남을 때까지 반복하면 출력 시각이 최소가 된다 (허프만 부호와 같은 탐욕법). 늦게 오는 c0 는 트리의 맨 위로 가게 된다 — 실제 합성 도구가 타이밍 기반으로 트리를 재배열하는 것과 같은 생각.

**propagate 를 OR 로 써도 되나?** carry 만 보면 된다: `p = a + b` 로 해도 `g + p·c` 값은 같다 (a=b=1 이면 g 가 이미 1). 그래서 carry 회로에서는 OR 를 "transmit" 신호로 쓰기도 한다. 하지만 합 `s = p ⊕ c` 에는 **반드시 XOR** 이 필요하다. 이 레슨은 p 하나를 둘 다에 쓰므로 XOR.

참고로 74x283 은 내부 fast carry 를 가진 4비트 가산기 칩, 74x182 는 블록 G/P 를 받아 다음 단 carry 들을 한꺼번에 만드는 **룩어헤드 carry 생성기** 칩이다 — 추가 연습 2 의 2단 CLA 가 그 구조다. 현대 CPU 의 64비트 덧셈기는 같은 g/p 아이디어를 **병렬 prefix 트리(Kogge-Stone, Brent-Kung, Sklansky 등)** 로 일반화해 O(log n) 단으로 만든다.

### Part C — ALU 슬라이스, ADC 사슬

덧셈기 하나로 4가지를 한다:

| 명령 | sub | cin | 결과 | carry-out 의 뜻 (Arm) |
|---|---|---|---|---|
| ADD | 0 | 0 | a + b | 부호 없는 오버플로 |
| ADC | 0 | C | a + b + C | 〃 |
| SUB / CMP | 1 | 1 | a + ~b + 1 = a − b | 1 = borrow **없음** (a ≥ b) |
| SBC | 1 | C | a + ~b + C = a − b − (1 − C) | 1 = borrow 없음 |

그래서 Arm 의 C 플래그는 뺄셈에서 "borrow 의 반대" 다 — 덧셈기의 carry-out 을 **그대로** 플래그로 내보내기 때문이다. x86 은 뺄셈 뒤 CF 를 뒤집어 "borrow" 로 저장한다 (ARCH-B02 가 플래그와 조건 코드를 맡는다). 테스트에서 SUB 의 carry 를 `a >= b`, SBC(C=0) 의 carry 를 `a > b` 와 비교하는 이유다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">32비트 덧셈을 8비트 ALU 로: 공간 대신 시간으로 carry 를 넘긴다</text>
  <g stroke="currentColor" class="f-mut">
    <rect x="80" y="44" width="120" height="52" rx="6"/><rect x="250" y="44" width="120" height="52" rx="6"/>
    <rect x="420" y="44" width="120" height="52" rx="6"/><rect x="590" y="44" width="120" height="52" rx="6"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="650" y="66" font-weight="bold">ADD (cin=0)</text><text x="650" y="86" font-size="12">바이트 0</text>
    <text x="480" y="66" font-weight="bold">ADC</text><text x="480" y="86" font-size="12">바이트 1</text>
    <text x="310" y="66" font-weight="bold">ADC</text><text x="310" y="86" font-size="12">바이트 2</text>
    <text x="140" y="66" font-weight="bold">ADC</text><text x="140" y="86" font-size="12">바이트 3</text>
  </g>
  <g stroke="currentColor" stroke-width="2.5" fill="none">
    <path d="M590 70 H540"/><path d="M420 70 H370"/><path d="M250 70 H200"/><path d="M80 70 H50"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="565" y="62">C</text><text x="395" y="62">C</text><text x="225" y="62">C</text><text x="40" y="62">C</text>
  </g>
  <text x="10" y="130" fill="currentColor">8비트 MCU: 명령 4개, 슬라이스 사이 carry = C 플래그 (레지스터에 저장 → 다음 명령이 cin 으로 읽음).</text>
  <text x="10" y="154" fill="currentColor">Cortex-M4 의 64비트 덧셈: adds r0,r0,r2 → adcs r1,r3 (실측 asm). arm64 의 128비트: adds → adc.</text>
  <text x="10" y="178" fill="currentColor">하드웨어 32비트 덧셈기 안에서는 같은 사슬이 한 클럭 안에 "공간으로" 펼쳐져 있다 — 리플이든 CLA 든.</text>
  <text x="10" y="208" fill="currentColor" font-size="12">뺄셈이면 첫 명령 SUB(cin=1), 나머지 SBC. 마지막 C = 0 이면 전체가 borrow (a &lt; b).</text>
</svg>
```

`demo_carry_asm.c` 실측 (Apple clang 21, M6):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_carry_asm.c -o -
cc -O2 -S demo_carry_asm.c -o -
```

```text
=== thumbv7em ===              === arm64 ===
add64:                         _add128:
	adds	r0, r0, r2             	adds	x0, x2, x0
	adcs	r1, r3                 	adc	x1, x3, x1
	bx	lr                         	ret
sub64:
	subs	r0, r0, r2
	sbcs	r1, r3
	bx	lr
```

(arm64 에서 `add64` 는 `add x0, x1, x0` 한 개 — 64비트 덧셈기가 한 번에 한다.) C 에는 carry 플래그가 없어서, 같은 파일의 `add_u32x4` 처럼 `r < s` 비교로 carry 를 다시 계산해 써야 한다. 컴파일러가 그 패턴을 알아보고 thumbv7em 에선 `adds.w`/`adcs`/`adc` 를, arm64 에선 `adds`/`adcs` + `cset hs`/`csinc` 을 섞어 내보낸다 (출력 전체는 demo 를 직접 돌려 보라). `mp_addsub` 가 하는 일이 정확히 이 "C 플래그를 변수로 들고 다니기" 다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 깨끗
```

실제 출력:

```text

adder       gates gates2in   depth depth2in
ripple         40       40      17       17
cla4-blk       52       84       6        9
arrival s0..s7,cout  ripple: 2 4 6 8 10 12 14 16 | 17
arrival s0..s7,cout  cla   : 2 4 4 4 4 6 6 6 | 5

19/19 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| CLA 에서 `c[2] = OR(g[1], AND(p[1], c[1]))` | 값은 맞지만 c1 을 기다리므로 **리플과 같은 깊이**. 전수 테스트는 통과하고 깊이 CHECK 만 깨진다 | c2 를 g/p/c0 만으로 펼친다: `OR(g1, AND(p1,g0), AND(p1,p0,c0))` |
| `if (c.v) ... else ...` 로 합을 고름 | 회로는 값에 따라 모양이 바뀌지 않는다. 게이트 수·도착 시각이 데이터에 따라 달라져 측정이 무의미 | 항상 같은 게이트를 부른다 (MUX 도 게이트로: 추가 연습 4) |
| 합에도 `p = OR(a, b)` 사용 | carry 에는 OR 로 충분하지만 `s = p ⊕ c` 는 a=b=1 일 때 틀린다 | 합에는 XOR propagate |
| SUB 에서 cin = 0 | `a + ~b` 는 1의 보수 뺄셈 → 결과가 1 작다 | SUB 는 cin = 1, SBC 는 cin = C |
| SUB carry 를 borrow 로 해석 | Arm 은 C = 1 이 "borrow 없음". x86 CF 와 반대 | 64비트 뺄셈 끝의 C=0 → a &lt; b (ARCH-B02) |
| 전수 테스트를 cin=0 만 | ADC/SBC 경로(cin=1)를 안 본다 — 리플의 c0 배선 실수를 놓친다 | cin {0,1} 모두, 4가지 ALU 모드 모두 |
| 도착 시각 계산에 `kraft += 1ull << t2` | 이 레슨을 쓰며 실제로 밟은 버그: 64비트 리플에서 t2=129 → **시프트 양 ≥ 64 = UB**, 그 뒤 `while ((1ull<<t2) < kraft) t2++` 가 영원히 돌았다 (uint8_t 랩) | 시프트 대신 "가장 이른 두 입력 묶기" 탐욕법 (지금 코드) |
| `unpack(unsigned x, ...)` 로 64비트 펼치기 | `x >> i` 에서 i ≥ 32 → UB | `uint64_t` 로 받고 `(unsigned)(x >> i)` |

## 8. 면접 Q&A

<details>
<summary>Q1. n비트 리플 캐리 가산기와 CLA 의 지연을 게이트 단으로 비교해 보세요.</summary>

리플: g/p 1단 + 비트당 2단(AND, OR) → **2n+1**, 8비트 17, 32비트 65. 4비트 블록 CLA(블록 사이 리플): g/p 1 + 블록당 2 + 마지막 XOR 1 → **2(n/4)+2**, 8비트 6, 32비트 18. 단, 게이트당 fan-in 이 5까지 커진다는 전제. 2입력 게이트만 쓰면 8비트 CLA 는 9단.

꼬리질문: "64비트면?" → 블록 리플 CLA 도 34단으로 선형 증가. 그래서 블록 G/P 로 한 단 더 룩어헤드(2단, 3단 계층 → 대략 log₄ n 에 비례)하거나 병렬 prefix(Kogge-Stone: log₂ n 단의 prefix + 앞뒤 g/p·XOR)로 간다. Kogge-Stone 은 가장 빠르지만 배선·게이트가 많고, Brent-Kung 은 단 수가 약 2배지만 면적이 작다.

</details>

<details>
<summary>Q2. 덧셈기로 뺄셈을 어떻게 하나요? 그때 carry 와 overflow 는?</summary>

`a − b = a + ~b + 1`: b 의 비트마다 XOR(sub) 로 반전하고 cin 에 1. carry-out 은 Arm 에서 그대로 C 플래그 → C=1 이면 borrow 없음(a ≥ b, 부호 없음). 부호 있는 오버플로 V 는 **MSB 로 들어간 carry 와 나온 carry 의 XOR** (c7 ⊕ c8) — 추가 연습 1 이 전수로 확인한다.

꼬리질문: "x86 은?" → 같은 덧셈기를 쓰지만 CF 를 뒤집어 borrow 로 저장한다. 그래서 Arm 의 `HS`(C=1) = 부호 없는 ≥ 이고, x86 의 `JAE`(CF=0) = 부호 없는 ≥ 다. 조건 코드 표는 ARCH-B02.

</details>

<details>
<summary>Q3. 덧셈기 지연이 CPU 클럭과 무슨 관계인가요?</summary>

ALU 는 보통 실행 단(EX)의 임계 경로 후보다. 플립플롭 사이 조합 논리 지연 + clk-to-q + setup 이 클럭 주기보다 짧아야 하므로, 덧셈기 깊이가 Fmax 의 상한을 정한다 (식과 slack 계산은 ARCH-B13). 같은 게이트 지연이면 이 모델에서 8비트 CLA 는 리플보다 단 수가 17/6 ≈ 2.8배 짧다.

꼬리질문: "덧셈이 한 클럭에 안 들어가면?" → 더 빠른 구조(prefix), 파이프라인으로 덧셈을 두 단에 나누기, 또는 곱셈기처럼 carry-save 로 carry 전파를 마지막 한 번으로 미루기.

</details>

<details>
<summary>Q4. 정적 깊이(17단)와 실제 데이터에서 carry 가 퍼지는 길이는 같은가요?</summary>

아니다. 정적 깊이는 **최악 경로** 다 (0xFF + 0x01 처럼 carry 가 끝까지 통과할 때). 무작위 데이터에서 가장 긴 carry 사슬의 평균은 log₂ n 을 넘지 않을 만큼 짧다 (Burks–Goldstine–von Neumann 의 고전적 분석) — 8비트 전수에서 평균 2.162, 길이 8 은 65536쌍 중 128쌍뿐이었다 (추가 연습 5). 동기식 회로는 최악을 기준으로 클럭을 잡아야 하고, 비동기(self-timed) 가산기는 완료 신호로 평균 지연을 쓴다.

꼬리질문: "그럼 실리콘 검증에서 최악 경로 테스트 벡터는?" → carry 가 전 비트를 전파하는 패턴(0xFF…F + 1, 0x7F…F + 1 로 V 까지)과 그 반대 전이. 지연 고장(transition fault) 테스트는 이런 경로를 일부러 켠다.

</details>

<details>
<summary>Q5. FPGA 에서 덧셈기를 짜면 CLA 가 더 빠른가요?</summary>

보통 아니다. FPGA 는 LUT 옆에 **전용 carry 체인**(빠른 배선 + MUX/XOR)이 있어서, 합성 도구는 `+` 를 그 체인 위의 리플로 매핑한다. 일반 LUT 로 짠 CLA 는 LUT 사이 범용 배선 지연 때문에 오히려 느린 경우가 많다. 게이트 단 수 모델은 "단 하나의 지연이 모두 같다" 는 가정 위에 있다는 걸 잊지 말 것 — 기술(ASIC/FPGA, 배선)이 바뀌면 답이 바뀐다.

꼬리질문: "ASIC 에서는?" → 합성 도구가 타이밍 제약을 보고 덧셈기 구조(리플/CLA/prefix 계열)를 고른다. RTL 에는 그냥 `a + b` 를 쓰는 게 보통이다.

</details>

<details>
<summary>Q6. 32비트 Cortex-M 에서 uint64_t 덧셈은 어떻게 컴파일되나요? C 로 carry 를 얻으려면?</summary>

`adds r0, r0, r2` (하위 32비트, C 플래그 갱신) → `adcs r1, r3` (상위 + C). 뺄셈은 `subs`/`sbcs`. 실측 asm 은 5. 풀이 Part C. C 언어엔 carry 가 없어서 `s = a + b; c = s < a;` 로 다시 계산하거나 `__builtin_add_overflow`/`__builtin_addc`(clang) 를 쓴다; 컴파일러는 `s < a` 패턴을 보고 carry 플래그를 그대로 쓰는 코드(`adds`/`adc`, arm64 `cset hs`)를 낸다.

꼬리질문: "8비트 AVR 에서 32비트 덧셈은?" → ADD 1개 + ADC 3개. 단 AVR·x86 은 뺄셈에서 C 가 borrow 라서 SBC 의 의미가 Arm 과 반대 방향이다 — 다중 정밀도 루틴을 다른 ISA 로 옮길 때 실수하는 곳.

</details>

<details>
<summary>Q7. 곱셈기 안에서는 왜 carry-save 가산기를 쓰나요?</summary>

부분곱 여러 개를 더할 때마다 carry 를 끝까지 전파하면 매번 O(n) (또는 log n) 이 든다. carry-save(3:2 압축기 = 전가산기를 비트마다 독립적으로)는 세 수를 "합 벡터 + carry 벡터" 두 수로 바꾸는데 지연이 **전가산기 1개 분량, 폭과 무관**. Wallace/Dadda 트리로 부분곱을 두 수까지 줄인 뒤 마지막에 한 번만 빠른 덧셈기(CLA/prefix)를 쓴다. MAC 이 핵심인 AI 가속기 데이터패스의 기본 부품.

</details>

## 9. 추가 연습

### 연습 1 — 게이트로 V 플래그

리플 가산기에서 MSB 로 들어가는 carry(c7)와 나오는 carry(c8)를 잡아 `V = XOR(c7, c8)` 를 만들고, ADD/SUB 둘 다 65536쌍 전수에서 "int8 로 계산한 정확한 값이 −128..127 을 벗어남" 과 같은지 확인하라.

<details>
<summary>답 보기</summary>

`add8_v`: 루프에서 i == 7 일 때 들어가는 carry 를 저장해 두고, 끝난 뒤 `XOR(c7, c)`. 이유: 두 양수 덧셈에서 MSB 로 carry 가 들어가고(c7=1) 나가지 않으면(c8=0) 부호 비트가 1 이 되어 음수처럼 보인다 — 오버플로. 두 음수에서 c7=0, c8=1 이면 양수처럼 보인다. 둘이 같으면 정상. ARCH-B02 의 `V = (a^r)&(b^r)` 식과 같은 값이다 (거기서는 결과 비트로, 여기서는 carry 로 본다).

</details>

### 연습 2 — 16비트 2단 CLA (74x182 스타일)

4비트 블록마다 G/P 만 만들고(`gp4`), **같은 `cla4`** 에 g 대신 G[0..3], p 대신 P[0..3] 를 넣어 c4·c8·c12·c16 을 한꺼번에 만든 뒤, 각 블록 안쪽 carry 를 블록 cin 으로 다시 cla4 하라. 깊이를 블록 리플 CLA, 리플과 비교.

<details>
<summary>답 보기</summary>

g/p 1 → 블록 P 2, G 3 → 그룹 carry (AND 4, OR 5) → 블록 안 carry (AND 6, OR 7) → 합 XOR **8**. 블록 리플 16비트 10, 리플 33. 실측 `r2.depth == 8 && rb16.depth == 10 && rr16.depth == 33` PASS. 16비트에선 차이가 작지만 64비트에서 3단 계층이면 블록 리플의 34 와 크게 벌어진다. 실습 코드의 블록 안 cla4 는 cc[4] 를 또 만드는데, 그 값은 그룹 carry 와 같으니 실제 회로라면 지운다 (게이트 수를 셀 때 주의).

</details>

### 연습 3 — 폭 스윕 8 / 16 / 32 / 64

`ripple_add` 와 `cla_add` 를 n = 8, 16, 32, 64 로 돌려 게이트 수와 깊이를 표로 찍고, 공식 `2n+1`, `2(n/4)+2`, 게이트 `5n`, `26·(n/4)` 을 CHECK 하라. 64비트는 xorshift 5000쌍 + `~0 + 1` 로 결과도 확인.

<details>
<summary>답 보기</summary>

실측:

```text
    n | ripple gates ripple depth |    cla gates    cla depth
    8 |           40           17 |           52            6
   16 |           80           33 |          104           10
   32 |          160           65 |          208           18
   64 |          320          129 |          416           34
```

두 구조 모두 게이트도 깊이도 **선형** 이다 — 블록 CLA 는 기울기를 2 → 0.5 로 줄였을 뿐. 깊이를 log 로 만들려면 계층(연습 2)이나 prefix 트리가 필요하다.

</details>

### 연습 4 — 8비트 캐리 셀렉트 가산기

하위 4비트는 리플, 상위 4비트는 cin=0 과 cin=1 두 벌을 **미리** 리플로 계산한 뒤 하위 carry c4 로 MUX(`OR(AND(x1,sel), AND(x0,NOT(sel)))`)하라. 전수 검증하고 게이트/깊이를 리플·CLA 와 비교.

<details>
<summary>답 보기</summary>

실측 `carry-select 8-bit: gates=80 depth=12 (ripple 40/17, cla4-blk 52/6)`. c4 는 9단(4비트 리플), 상위 두 벌은 그동안 이미 끝나 있고(합 ≤ 8, carry 9), MUX 가 NOT·AND·OR 3단을 더해 12. 게이트는 2배 이상(상위를 두 번 + MUX 5개). 상수 cin 으로 만든 사본은 실제 합성에선 상수 전파로 줄어든다 — 이 시뮬레이터는 줄이지 않는다. "면적으로 시간을 산다" 의 가장 단순한 예.

</details>

### 연습 5 — 데이터에 따라 달라지는 carry 사슬

cin=0 으로, 모든 carry 를 동시에 `c_{i+1} = g_i | p_i·c_i` 로 갱신하는 반복을 **바뀌는 게 없을 때까지** 몇 번 도는지(`settle_steps`) 세고, "generate 비트 i 에서 시작해 위로 이어지는 propagate 의 수 + 1" 의 최댓값(`longest_chain`)과 65536쌍 모두 같은지 확인하라. 분포와 평균을 찍어라.

<details>
<summary>답 보기</summary>

두 방법은 65536쌍 모두 일치. 실측 분포:

```text
carry chain length histogram (8-bit, cin=0, all 65536 pairs):
  len 0:  6561
  len 1: 15727
  len 2: 20208
  len 3: 12864
  len 4:  6080
  len 5:  2560
  len 6:  1024
  len 7:   384
  len 8:   128
  average = 2.162
```

길이 0 = generate 가 하나도 없는 쌍 = 비트마다 (0,0),(0,1),(1,0) 세 가지 → 3⁸ = 6561. 길이 8 = g0 이고 p1..p7 → 비트 1~7 마다 (0,1),(1,0) 두 가지 → 2⁷ = 128. 둘 다 CHECK 했다. 평균 2.16 ≈ log₂ 8 근처 — Q4 의 "평균은 짧고 최악만 길다".

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   # 10/10 passed
```

## 10. 핵심 정리

- 전가산기: `s = a⊕b⊕cin`, `cout = ab + cin(a⊕b)` = 반가산기 2개 + OR, 게이트 5개, carry 경로 2단.
- `g = ab` (생성), `p = a⊕b` (전파), `c_{i+1} = g_i + p_i c_i`. carry 에는 p 를 OR 로 써도 되지만 합에는 XOR.
- 리플 깊이 **2n+1** (8비트 17), 4비트 블록 CLA **2(n/4)+2** (8비트 6). 둘 다 선형 — log 가 되려면 계층/prefix.
- CLA 의 대가: 게이트 수(52 vs 40)와 fan-in. 2입력 게이트로만 지으면 8비트 CLA 6 → 9단.
- 뺄셈 = `a + ~b + 1`. Arm 은 carry-out 을 그대로 C 로 → SUB 뒤 C=1 은 borrow 없음. V = c_in(MSB) ⊕ c_out(MSB).
- 넓은 덧셈 = ADD + ADC 사슬 (Cortex-M4 64비트 `adds`/`adcs`, arm64 128비트 `adds`/`adc`). 슬라이스 사이 carry 가 시간 축으로 리플한다.
- 정적 깊이는 최악 경로. 실제 데이터의 평균 carry 사슬은 짧다 (8비트 평균 2.16) — 동기식 클럭은 최악 기준.

## 참고자료

- Harris & Harris, *Digital Design and Computer Architecture, ARM Edition* — 5.2 산술 회로 (ripple, CLA, prefix adder, 지연 계산)
- Patterson & Hennessy, *Computer Organization and Design, ARM Edition* — 부록 A (논리 설계, carry lookahead)
- Weste & Harris, *CMOS VLSI Design* — 11장 Datapath Subsystems (Kogge-Stone, Brent-Kung, carry-select, fan-in 과 지연)
- Texas Instruments SN74LS283 (4-bit binary full adder with fast carry), SN74S182 (look-ahead carry generator) 데이터시트
- [Arm Architecture Reference Manual for A-profile — ADDS/ADC/SUBS/SBC, NZCV](https://developer.arm.com/documentation/ddi0487/latest)
- [Armv7-M Architecture Reference Manual — Thumb ADC/SBC, APSR 플래그](https://developer.arm.com/documentation/ddi0403/latest)
- 관련 레슨: ARCH-B02 (NZCV 플래그와 조건 코드), ARCH-B13 (setup slack 과 Fmax), ARCH-B05 (MUX/디코더), C-B01 (비트 연산)
