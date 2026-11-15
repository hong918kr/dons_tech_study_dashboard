---
id: ARCH-B02
time: 70분
updated: 2026-11-11
---
# ARCH-B02 ALU 플래그 계산기: N Z C V

## 학습 목표

- 가산기 하나로 ADD / ADC / SUB / SBC 를 모두 계산하고, 그때 서는 **N Z C V** 를 8비트·32비트에서 비트 수준으로 맞힐 수 있다.
- "Arm 은 뺄셈에서 **C = NOT borrow**, x86 은 **CF = borrow**" 를 가산기 구조로 설명하고, 실제 M6(AArch64 `mrs nzcv`)와 x86(Rosetta `pushfq`) 플래그로 확인한다.
- 조건 코드 15개(EQ NE CS/HS CC/LO MI PL VS VC HI LS GE LT GT LE AL)를 플래그 식으로 쓰고, **MI 와 LT 가 왜 다른지** 예로 보인다.
- 32비트 코어(Cortex-M)에서 64비트 덧셈·비교를 **ADDS/ADCS, CMP/SBCS 체인**으로 만들고, 체인 뒤의 Z 가 왜 믿을 수 없는지 설명한다.
- wrap-safe 틱 비교 `(int32_t)(b - a) < 0` 이 플래그로는 **MI** (차이의 부호)라는 것을 연결한다.

## 1. 문제

"`cmp r0, r1` 뒤에 `blo` 와 `blt` 는 뭐가 다른가요?", "왜 Arm 은 뺄셈에서 캐리가 1 이면 borrow 가 *없는* 건가요?" — 펌웨어·실리콘 검증 면접에서 어셈블리 한 줄을 보여 주고 바로 묻는 질문이다. 디버거에서 xPSR 를 읽을 때, 컴파일러가 만든 `sbcs`/`it lt` 를 읽을 때, ALU RTL 테스트벤치의 기대값(golden model)을 만들 때 모두 이 네 비트를 정확히 알아야 한다.

ALU 는 결과 말고도 **상태 플래그(status flags)** 네 개를 낸다. 모두 결과 비트와 캐리만 보고 만든다 — 피연산자가 "부호 있는 수였는지"는 ALU 가 모른다. 해석은 그다음 명령(조건 분기)이 고른다.

| 플래그 | 이름 | 뜻 (width = w 비트) | 누구를 위한 것 |
|---|---|---|---|
| N | Negative | 결과의 MSB (비트 w−1) | 부호 있는 해석 |
| Z | Zero | 결과 == 0 | 둘 다 |
| C | Carry | 덧셈: MSB 밖으로 나간 캐리. **Arm 뺄셈: borrow 가 없으면 1** | 부호 없는 해석 |
| V | oVerflow | 부호 있는 결과가 w 비트 범위를 넘음 | 부호 있는 해석 |

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="ab02-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">가산기 하나로 ADD / ADC / SUB / SBC — 플래그는 결과와 캐리에서 나온다</text>
  <text x="20" y="66" fill="currentColor" font-family="monospace">a</text>
  <text x="20" y="136" fill="currentColor" font-family="monospace">b</text>
  <line x1="36" y1="62" x2="300" y2="62" stroke="currentColor" marker-end="url(#ab02-a)"/>
  <line x1="36" y1="132" x2="110" y2="132" stroke="currentColor" marker-end="url(#ab02-a)"/>
  <rect x="112" y="108" width="120" height="48" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="172" y="128" fill="currentColor" text-anchor="middle">SUB 이면 ~b</text>
  <text x="172" y="146" fill="currentColor" text-anchor="middle" font-size="12">(XOR 게이트 줄)</text>
  <line x1="232" y1="132" x2="300" y2="132" stroke="currentColor" marker-end="url(#ab02-a)"/>
  <rect x="112" y="190" width="120" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="172" y="210" fill="currentColor" text-anchor="middle" font-size="12">cin 선택</text>
  <text x="172" y="228" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">ADD 0  SUB 1</text>
  <text x="172" y="246" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">ADC/SBC = C</text>
  <path d="M232 225 L280 225 L280 160 L300 160" stroke="currentColor" fill="none" marker-end="url(#ab02-a)"/>
  <rect x="302" y="40" width="150" height="140" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="377" y="100" fill="currentColor" text-anchor="middle" font-weight="bold">w 비트 가산기</text>
  <text x="377" y="122" fill="currentColor" text-anchor="middle" font-family="monospace">a + b' + cin</text>
  <line x1="452" y1="80" x2="540" y2="80" stroke="currentColor" marker-end="url(#ab02-a)"/>
  <text x="458" y="72" fill="currentColor" font-size="12">res[w-1:0]</text>
  <line x1="452" y1="150" x2="540" y2="150" stroke="currentColor" marker-end="url(#ab02-a)"/>
  <text x="458" y="142" fill="currentColor" font-size="12">c_out (비트 w)</text>
  <line x1="452" y1="170" x2="540" y2="196" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ab02-a)"/>
  <text x="458" y="200" fill="currentColor" font-size="12">c_in(MSB)</text>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="545" y="62">N = res[w-1]</text>
    <text x="545" y="84">Z = (res == 0)</text>
    <text x="545" y="154">C = c_out</text>
    <text x="545" y="200">V = c_in(MSB) ^ c_out</text>
  </g>
  <rect x="10" y="272" width="720" height="50" rx="6" fill="none" stroke="currentColor"/>
  <text x="22" y="292" fill="currentColor">SUB a,b = a + ~b + 1 (2의 보수). 같은 가산기라 C 는 "캐리"로 그대로 나온다 → borrow 가 없을 때 1.</text>
  <text x="22" y="312" fill="currentColor">x86 은 뺄셈 뒤 캐리를 뒤집어 CF = borrow 로 저장한다. Arm(과 6502)은 뒤집지 않는다.</text>
</svg>
```

이 레슨은 이 그림을 C 로 옮긴다. 플래그는 Arm 의 순서대로 4비트 하나에 담는다: `nzcv = N<<3 | Z<<2 | C<<1 | V` — AArch64 `mrs x0, nzcv` 의 비트 31..28, Cortex-M xPSR(APSR) 의 비트 31..28 과 같은 순서라서 `>> 28` 하면 그대로 비교할 수 있다.

이미 아는 것(다시 가르치지 않는다): 2의 보수와 부호 확장(ARCH-B01), 게이트로 만든 리플/CLA 가산기(ARCH-B04 — 여기서는 `+` 를 써도 된다), 정수 승격(C-B02). 부동소수점 플래그는 ARCH-B03, ISA 인코딩은 ARCH-B08 이 맡는다.

### Part A — 가산기 하나로 ADD / ADC / SUB / SBC + NZCV

- `alu_adc(a, b, cin, width)` — width 비트(1..32)로 자른 `a + b + cin` 의 결과와 NZCV. 다른 연산은 전부 이것을 부른다.
- `alu_sub(a, b, width)` = `a + ~b + 1`, `alu_sbc(a, b, cin, width)` = `a + ~b + cin` (= `a − b − !cin`).
- 검증: 8비트는 **전수**(256×256 쌍 × cin 2 × ADC/SBC), 32비트는 경계값 12개의 모든 쌍 + 고정 시드 xorshift32 200,000쌍. 기대값은 가산기 공식이 아니라 **넓은 정수(int64_t)로 직접 계산한 독립 오라클**에서 나온다.

### Part B — 조건 코드 표와 면접 함정

- `cond_holds(cc, nzcv)` — 15개 조건을 플래그 식으로. enum 순서는 Arm 인코딩(EQ=0000 … AL=1110)이라 `cc ^ 1` 이 반대 조건이 된다(연습 5).
- 검증: `alu_sub(a, b)` 의 플래그로 판정한 조건이 C 비교 연산자(`==`, `<` 의 부호 없는/있는 버전)와 같은지 8비트 전수 + 32비트 경계/무작위.
- 함정: `CMP 0x80, 0x01` (8비트) → LT 참인데 MI 거짓, `CMP x, #0` → C=1, `NEG INT_MIN` → V=1.
- `arm_sub_to_x86(nzcv)` — 같은 뺄셈의 x86 EFLAGS(CF bit0, ZF bit6, SF bit7, OF bit11). CF 는 C 의 반대.

### Part C — 32비트 코어에서 64비트 연산 (Cortex-M)

- `add64_chain(a, b, &nzcv)` — `ADDS lo; ADCS hi`. 64비트 결과와 마지막 플래그.
- `cmp64_flags(a, b)` — `CMP lo; SBCS hi`. 이 플래그로 64비트 LO/HS/LT/GE 가 맞는지 100,000쌍(lo 같은 쌍·hi 같은 쌍 섞음) 검증. **EQ 는 틀린다**는 것도 테스트로 박아 둔다.
- `tick_after(a, b)` — 32비트 틱 카운터가 랩어라운드해도 "a 가 b 보다 나중인가"를 판정 (리눅스 `time_after` 와 같은 식). 플래그로는 `SUBS b, a` 의 MI.

## 2. Problem (English)

> **ALU flag model.**
>
> Model an Arm-style ALU in portable C11 (no inline asm, no compiler builtins in the implementation).
>
> 1. `alu_t alu_adc(uint32_t a, uint32_t b, unsigned cin, unsigned width)` for `1 <= width <= 32` returns the `width`-bit result of `a + b + cin` and a 4-bit `nzcv` (N in bit 3 … V in bit 0). Build `alu_add`, `alu_sub` (`a - b`) and `alu_sbc` (`a - b - !cin`) on top of it. Subtraction must use Arm semantics: `C = 1` means *no borrow*.
> 2. `bool cond_holds(cond_t cc, uint8_t nzcv)` for EQ, NE, HS/CS, LO/CC, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL. After `alu_sub(a, b)`, each condition must agree with the corresponding C comparison on unsigned / signed interpretations of the operands.
> 3. On a 32-bit core, implement 64-bit add (`ADDS`/`ADCS`) and 64-bit compare (`CMP`/`SBCS`) using only the 32-bit primitives. State which conditions are valid after the compare chain and why.
> 4. Implement a wrap-safe `tick_after(a, b)` for a free-running 32-bit counter.
>
> Examples (8-bit): `0x7F + 0x01 = 0x80, N--V`; `0xFF + 0x01 = 0x00, -ZC-`; `0x03 - 0x05 = 0xFE, N---` (borrow → C=0); `0x80 - 0x01 = 0x7F, --CV` (LT true, MI false).

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과(실제, 끝부분):

```text
FAIL starter.c:238  bad == 0
FAIL starter.c:240  cond_holds(CC_EQ, cmp64_flags(0x0000000500000001ull, 0x0000000500000000ull)) == true
FAIL starter.c:243  tick_after(0x00000010u, 0xFFFFFFF0u) == true
FAIL starter.c:245  tick_after(100, 100) == false && tick_after(101, 100) == true
FAIL starter.c:246  tick_after(0x80000001u, 1u) && tick_after(1u, 0x80000001u)
2/34 passed
```

TODO 는 8곳: `alu_adc` 본문, `alu_sub`, `alu_sbc`, `cond_holds`, `arm_sub_to_x86`, `add64_chain`, `cmp64_flags`, `tick_after`. 오라클(`oracle`, `sx`)과 하네스는 건드리지 않는다.

## 4. 힌트

<details>
<summary>힌트 1 — 캐리를 잃지 않으려면</summary>

`uint32_t` 끼리 더하면 비트 32 캐리가 사라진다. `(uint64_t)a + b + cin` 으로 더하고 `>> width` 하면 C 가 나온다. width 마스크도 `(UINT64_C(1) << width) - 1` 로 만들면 width=32 에서 `1u << 32` UB 가 없다.

</details>

<details>
<summary>힌트 2 — V 는 "같은 부호 둘을 더했는데 결과 부호가 다르다"</summary>

`a` 와 `r` 의 부호가 다르고 **동시에** `b` 와 `r` 의 부호도 다르면 오버플로다: `((a ^ r) & (b ^ r))` 의 MSB. 다른 표현: MSB 로 *들어간* 캐리 XOR MSB 에서 *나간* 캐리 (연습 1).

</details>

<details>
<summary>힌트 3 — 뺄셈은 새로 만들지 않는다</summary>

`a - b = a + ~b + 1`. 그러면 V 공식도 `b` 대신 `~b` 를 넣은 덧셈 공식 그대로 맞다. `a + (-b)` 처럼 **먼저 -b 를 계산하고 cin=0 으로 더하면 틀린다** — b=0 일 때 무슨 일이 생기는지 손으로 해 보라.

</details>

<details>
<summary>힌트 4 — 부호 있는 비교는 N 하나로 안 된다</summary>

`a - b` 가 넘치면(V=1) 결과 부호가 뒤집혀 나온다. 그래서 `a < b` (signed) 는 `N != V`. 부호 없는 비교는 borrow 하나면 충분: `a < b` (unsigned) ⇔ borrow ⇔ Arm 에서 `C == 0`.

</details>

<details>
<summary>힌트 5 — 64비트 체인</summary>

lo 워드는 cin 없이(ADDS / SUBS), hi 워드는 lo 의 C 를 cin 으로(ADCS / SBCS). 마지막 명령의 N·C·V 는 64비트 전체에 대해 맞지만, Z 는 hi 워드 결과만 보고 만든다.

</details>

## 5. 풀이

### Part A — 플래그 네 개를 결과·캐리에서 꺼낸다

```c
uint64_t sum = (uint64_t)a + b + cin;          /* a, b 는 이미 width 비트로 자름 */
uint32_t r   = (uint32_t)sum & m;
N = r >> (w-1) & 1;   Z = r == 0;   C = sum >> w & 1;
V = ((a ^ r) & (b ^ r)) >> (w-1) & 1;
```

8비트 손계산 표 (`alu_add`/`alu_sub` 테스트와 같은 값):

| 연산 | 부호 없음 | 부호 있음 | 결과 | N Z C V | 해설 |
|---|---|---|---|---|---|
| 0x7F + 0x01 | 127+1=128 | 127+1=−128 ✗ | 0x80 | 1 0 0 1 | 부호 있는 오버플로만 |
| 0xFF + 0x01 | 255+1=256 ✗ | −1+1=0 | 0x00 | 0 1 1 0 | 부호 없는 캐리만 |
| 0x80 + 0x80 | 128+128 ✗ | −128−128 ✗ | 0x00 | 0 1 1 1 | 둘 다 넘침 |
| 0x05 − 0x03 | 5−3=2 | 2 | 0x02 | 0 0 1 0 | borrow 없음 → C=1 |
| 0x03 − 0x05 | 3−5 → borrow | −2 | 0xFE | 1 0 0 0 | borrow → C=0 |
| 0x80 − 0x01 | 128−1=127 | −128−1 ✗ | 0x7F | 0 0 1 1 | V=1, N=0 (아래 Part B 함정) |
| 0xFF + 0x00 + cin1 | 256 ✗ | 0 | 0x00 | 0 1 1 0 | ADC |
| 0x00 − 0x00 − !C(=1) | borrow | −1 | 0xFF | 1 0 0 0 | SBC, C 입력 0 = "이미 빌려 감" |

**C 와 V 는 서로 독립**이다. 표의 첫 두 줄처럼 한쪽만 설 수도, 셋째 줄처럼 둘 다 설 수도 있다. "캐리가 났으니 오버플로" 는 틀린 말이다 — 캐리는 부호 없는 해석의 넘침, V 는 부호 있는 해석의 넘침이다.

**왜 SUB 의 C 가 "NOT borrow" 인가.** `a − b = a + ~b + 1` 을 w 비트 가산기로 하면, `a ≥ b` (부호 없음)일 때 정확히 `2^w` 을 넘어서 캐리가 1 이 나온다: `a + (2^w − 1 − b) + 1 = 2^w + (a − b)`. 즉 **캐리 = borrow 없음**. Arm 은 이 캐리를 그대로 C 에 저장한다(그래서 SBC 는 `a + ~b + C`, 별도 회로 없음). x86 은 SUB/CMP/SBB 에서 이걸 뒤집어 CF = borrow 로 저장한다 — 사람에게 직관적이지만 SBB 는 `a − b − CF` 로 정의된다. 6502 의 SBC 도 Arm 과 같은 쪽이라 뺄셈 전에 `SEC`(carry set = borrow 없음)를 한다.

**독립 오라클.** 테스트는 가산기 공식과 *다른 방법*으로 기대값을 만든다: 부호 없는 합 `(int64_t)a + b + cin` 이 `2^w` 이상이면 C, 부호 해석 `sx(a) + sx(b) + cin` 이 `[−2^(w−1), 2^(w−1)−1]` 밖이면 V. 뺄셈은 `a − b − !cin ≥ 0` 이면 C. 같은 공식을 두 번 쓰면 같은 버그를 두 번 넣는 것이라 검증이 되지 않는다.

**하드웨어 대조 — `demo_hw_flags.c`.** 같은 모델(압축본)을 실제 `adds/subs/adcs/sbcs` + `mrs %1, nzcv` 결과와 비교한다. ADCS/SBCS 앞에서는 `msr nzcv, x` 로 C 입력(비트 29)을 직접 넣는다. AArch64 에는 8비트 플래그 연산이 없으므로 8비트는 **피연산자를 `<< 24` 해서** 32비트로 계산한다 — 아래 24비트가 0 이라 캐리가 그 안에서 생기지 않고, 비트 31 이 8비트의 MSB 역할을 하므로 N Z C V 가 8비트 연산과 같다(연습 3 에서 모델로도 증명).

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_hw_flags.c -o demo_hw_flags && ./demo_hw_flags
```

실제 출력 (Apple M6, Apple clang 21.0.0):

```text
sample rows (32-bit)      op    a          b          cin  model       hw
                          ADDS  0x7FFFFFFF 0x00000001 0    80000000 N--V 80000000 N--V ok
                          ADDS  0xFFFFFFFF 0x00000001 0    00000000 -ZC- 00000000 -ZC- ok
                          SUBS  0x00000005 0x00000003 0    00000002 --C- 00000002 --C- ok
                          SUBS  0x00000003 0x00000005 0    FFFFFFFE N--- FFFFFFFE N--- ok
                          SUBS  0x00001234 0x00000000 0    00001234 --C- 00001234 --C- ok
                          SUBS  0x80000000 0x00000001 0    7FFFFFFF --CV 7FFFFFFF --CV ok
                          ADCS  0xFFFFFFFF 0x00000000 1    00000000 -ZC- 00000000 -ZC- ok
                          SBCS  0x00000000 0x00000000 0    FFFFFFFF N--- FFFFFFFF N--- ok
32-bit  : 8001152 cases, 0 mismatches (model vs mrs nzcv)
8-bit   : 131072 cases (ADDS/SUBS, operands << 24), 0 mismatches
```

모델과 실리콘이 800만 케이스에서 한 비트도 다르지 않다. (데모가 "아무것도 안 보는" 테스트가 아니라는 확인: 모델의 SUB 를 `a + (-b)` 로 바꾸면 같은 데모가 `32-bit : 8001152 cases, 48 mismatches`, `8-bit : 131072 cases ..., 512 mismatches` 를 낸다 — §7 첫 줄.)

### Part B — 조건 코드: 같은 플래그, 다른 해석

`CMP a, b` 는 결과를 버리는 `SUBS`. 그 뒤 조건 코드가 "어떤 해석으로 볼지"를 고른다.

| cond | 인코딩 | 식 | CMP a,b 뒤 의미 | x86 대응 |
|---|---|---|---|---|
| EQ / NE | 0000 / 0001 | Z / !Z | a == b / a != b | JE / JNE |
| HS(=CS) / LO(=CC) | 0010 / 0011 | C / !C | 부호 없음 a ≥ b / a &lt; b | JAE(JNC) / JB(JC) |
| MI / PL | 0100 / 0101 | N / !N | (a−b) 결과가 음수 / 아님 | JS / JNS |
| VS / VC | 0110 / 0111 | V / !V | 부호 있는 오버플로 남 / 안 남 | JO / JNO |
| HI / LS | 1000 / 1001 | C && !Z / !C \|\| Z | 부호 없음 a &gt; b / a ≤ b | JA / JBE |
| GE / LT | 1010 / 1011 | N == V / N != V | 부호 있음 a ≥ b / a &lt; b | JGE / JL |
| GT / LE | 1100 / 1101 | !Z && N==V / Z \|\| N!=V | 부호 있음 a &gt; b / a ≤ b | JG / JLE |
| AL | 1110 | 1 | 항상 | JMP |

인코딩의 마지막 비트만 다른 두 조건이 서로 반대다(AL 제외; 1111 은 A32 에서 무조건 실행 확장 공간, AArch64 에서는 NV 라는 이름이지만 항상 참으로 동작한다). 컴파일러가 `if (a < b) X else Y` 를 `b.ge Y` 로 뒤집는 것이 이 `cc ^ 1` 이다.

x86 쪽 이름을 보면 HS/LO 가 "above/below", GE/LT 가 "greater/less" — 부호 없는 쪽과 부호 있는 쪽을 **단어부터 다르게** 쓴다. 다만 x86 은 CF 가 borrow 라서 "below = CF=1", Arm 은 "LO = C=0" 으로 같은 조건의 플래그 값이 반대다. 이 차이를 실제 x86 명령으로 확인한 것이 `demo_x86_flags.c` 다 (Rosetta 2 로 번역 실행 — 진짜 Intel/AMD 칩이 아니라 의미 확인용):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -arch x86_64 demo_x86_flags.c -o demo_x86_flags && ./demo_x86_flags
```

```text
SUB a-b     x86 CF ZF SF OF | Arm N Z C V
00000005-3       0  0  0  0 |     0 0 1 0
00000003-5       1  0  1  0 |     1 0 0 0
00000007-7       0  1  0  0 |     0 1 1 0
00001234-0       0  0  0  0 |     0 0 1 0
80000000-1       0  0  0  1 |     0 0 1 1
SUB: x86 CF == !Arm C  mismatches = 0 / 1000000
ADD: x86 CF ==  Arm C  mismatches = 0 / 1000000
```

덧셈에서는 CF 와 C 가 **같고**, 뺄셈에서만 **반대**다. Z/N/V 는 두 ISA 가 같다(ZF/SF/OF).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 비트, 두 해석 — CMP 0x80, 0x01 (8비트)</text>
  <g font-family="monospace" fill="currentColor">
    <text x="20" y="54">a = 1000 0000</text><text x="20" y="76">b = 0000 0001</text>
    <text x="20" y="104">a - b = 0111 1111   N=0 Z=0 C=1 V=1</text>
  </g>
  <rect x="20" y="124" width="330" height="120" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="34" y="146" fill="currentColor" font-weight="bold">부호 없는 해석 (C, Z)</text>
  <text x="34" y="170" fill="currentColor" font-family="monospace">128 vs 1</text>
  <text x="34" y="194" fill="currentColor">C=1 → borrow 없음 → HS</text>
  <text x="34" y="216" fill="currentColor">C=1 &amp;&amp; Z=0 → HI (128 &gt; 1) 참</text>
  <text x="34" y="236" fill="currentColor" font-size="12">LO 거짓</text>
  <rect x="380" y="124" width="350" height="120" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="394" y="146" fill="currentColor" font-weight="bold">부호 있는 해석 (N, V)</text>
  <text x="394" y="170" fill="currentColor" font-family="monospace">-128 vs 1 → -129 는 범위 밖</text>
  <text x="394" y="194" fill="currentColor">결과 0x7F 는 "+127" 로 부호가 뒤집힘</text>
  <text x="394" y="216" fill="currentColor">N=0, V=1 → N != V → LT 참</text>
  <text x="394" y="236" fill="currentColor" font-size="12">MI(N=1)는 거짓 — MI 로 "a &lt; b" 를 하면 틀린다</text>
  <text x="10" y="276" fill="currentColor">ALU 는 한 번만 계산한다. 어느 플래그를 볼지는 조건 코드(= 소스의 타입)가 고른다.</text>
</svg>
```

**MI 와 LT.** MI 는 "뺄셈 결과의 부호", LT 는 "진짜 부호 있는 대소". 넘침이 없으면(V=0) 같고, 넘치면(V=1) 정반대다. 그래서 `N != V`. 테스트의 `CHECK(cond_holds(CC_LT, f) && !cond_holds(CC_MI, f))` 가 이 한 줄이다. GT 는 여기에 "같지 않음(!Z)" 을 더한 것, HI 는 "borrow 없음(C) + 같지 않음(!Z)".

**나머지 함정 세 개 (모두 CHECK 있음).**
- `CMP x, #0` 은 **항상 C=1** — 0 을 빼면 borrow 가 날 수 없다. x86 의 `cmp x, 0` 은 CF=0. 같은 "캐리" 이름이라도 의미가 반대인 대표 예.
- `CMP 0, 0` → Z=1, C=1 → HS 참, HI 거짓.
- `NEG INT_MIN` (`0 − 0x80000000`) → 결과 0x80000000, N=1 V=1, C=0. `-INT_MIN` 이 C 에서 UB 인 이유를 하드웨어가 V 로 알려 준다.

**컴파일러가 실제로 고르는 조건 — `demo_asm.c`.** 같은 `<` 가 타입에 따라 다른 조건이 된다:

```bash
cc -O2 -S -o - demo_asm.c                                                     # arm64
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -   # Cortex-M4
```

| C 함수 | arm64 (실제) | thumbv7em (실제) |
|---|---|---|
| `lt_s32: a < b` (int32_t) | `cmp w0, w1` / `cset w0, lt` | `cmp r0, r1` / `it lt` / `movlt r0, #1` |
| `lt_u32: a < b` (uint32_t) | `cmp w0, w1` / `cset w0, lo` | `cmp r0, r1` / `it lo` / `movlo r0, #1` |
| `gt_u32: a > b` (uint32_t) | `cmp w0, w1` / `cset w0, hi` | `cmp r0, r1` / `it hi` / `movhi r0, #1` |

(Thumb 쪽은 각각 `mov.w r0, #0` 이 `cmp` 와 `it` 사이에 있고 `bx lr` 로 끝난다. 표에는 핵심만 옮겼다.)

### Part C — 32비트 코어의 64비트 연산

Cortex-M3/M4/M7 의 레지스터는 32비트다. `uint64_t` 덧셈은 두 명령으로 쪼개진다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="ab02-b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">64비트 = hi:lo 두 워드 — 캐리가 C 플래그를 타고 건너간다</text>
  <rect x="380" y="44" width="200" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="480" y="66" fill="currentColor" text-anchor="middle" font-family="monospace">ADDS r0, r0, r2</text>
  <text x="480" y="84" fill="currentColor" text-anchor="middle" font-size="12">lo + lo, cin = 0</text>
  <rect x="100" y="44" width="200" height="50" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="200" y="66" fill="currentColor" text-anchor="middle" font-family="monospace">ADCS r1, r1, r3</text>
  <text x="200" y="84" fill="currentColor" text-anchor="middle" font-size="12">hi + hi + C</text>
  <path d="M380 70 L302 70" stroke="currentColor" fill="none" marker-end="url(#ab02-b)"/>
  <text x="318" y="62" fill="currentColor" font-size="12">C</text>
  <text x="600" y="74" fill="currentColor" font-size="12">① 먼저</text>
  <text x="20" y="74" fill="currentColor" font-size="12">② 다음</text>
  <rect x="100" y="104" width="480" height="30" rx="4" fill="none" stroke="currentColor"/>
  <text x="340" y="124" fill="currentColor" text-anchor="middle">최종 N, C, V = 64비트 전체 기준으로 맞다 / Z = hi 워드만 봤다</text>
  <text x="10" y="166" fill="currentColor" font-weight="bold">비교: CMP lo ; SBCS hi → LO/HS/LT/GE 는 맞고 EQ/NE/HI/LS/GT/LE 는 틀릴 수 있다</text>
  <g font-family="monospace" fill="currentColor" font-size="12.5">
    <text x="20" y="192">a = 0x00000005_00000001   b = 0x00000005_00000000   (a != b)</text>
    <text x="20" y="214">CMP  lo: 1 - 0          → C=1 (borrow 없음)</text>
    <text x="20" y="236">SBCS hi: 5 - 5 - !C = 0 → Z=1   ← "같다"고 말한다. 틀림</text>
  </g>
  <rect x="10" y="252" width="720" height="40" rx="6" fill="none" stroke="currentColor"/>
  <text x="22" y="276" fill="currentColor">그래서 컴파일러는 64비트 == 를 EOR/EOR/ORRS (또는 CMP + IT EQ + CMPEQ / AArch64 CCMP) 로 만든다.</text>
</svg>
```

- **덧셈:** lo 를 `ADDS`(플래그 갱신), hi 를 `ADCS`(C 를 cin 으로). `demo_asm.c` 의 `add64` 를 thumbv7em 으로 컴파일하면 실제로 `adds r0, r0, r2` / `adcs r1, r3` 두 줄이 나온다 (arm64 는 64비트 레지스터라 `add x0, x1, x0` 한 줄).
- **비교:** `lt_s64` 는 thumbv7em 에서 `subs r0, r0, r2` / `sbcs.w r0, r1, r3` / `it lt` / `movlt r0, #1`. SBCS 의 N·V 는 hi 워드 뺄셈에 lo 의 borrow 까지 반영한 값이라 64비트 부호 있는 비교(LT/GE)가 맞고, C 는 64비트 전체의 borrow 라서 LO/HS 도 맞다. Z 만 hi 워드 결과가 0 인지 볼 뿐이다.
- **그래서 64비트 == 는 따로 만든다.** 같은 데모의 `eq_u64` 실제 출력: `eors r1, r3` / `eors r0, r2` / `orrs r0, r1` / `clz r0, r0` / `lsrs r0, r0, #5` — XOR 두 개를 OR 해서 0 인지 본다(`clz` 가 32 이면 0 → `>> 5` 가 1). 연습 6 은 다른 방법(`CMP lo; IT EQ; CMPEQ hi`)을 모델로 검증한다. 참고로 AVR 은 `CPC`/`SBC` 가 "결과가 0 이면 Z 를 이전 값 그대로 둔다"로 정의되어 있어 다중 바이트 비교 체인 끝의 Z 가 전체 EQ 가 된다 — ISA 설계자가 이 문제를 플래그 규칙으로 푼 예.

테스트는 lo 가 같은 쌍과 hi 가 같은 쌍을 일부러 섞는다 — 무작위 64비트 쌍만으로는 "hi 가 같고 lo 만 다른" 경우가 거의 나오지 않아 체인 버그(예: hi 에서 SBCS 대신 SUBS)를 놓친다.

**tick_after — 랩어라운드 안전 비교.** SysTick 으로 올리는 32비트 `uint32_t ticks` 는 1 kHz 에서 약 49.7일마다 0 으로 돌아온다. `if (now >= deadline)` 은 deadline 이 랩어라운드 너머에 있으면 틀린다. 리눅스 `time_after(a, b)` 는 `(long)((b) - (a)) < 0` — **뺄셈은 부호 없이(모듈러) 하고, 차이의 부호만 본다.** 플래그로는 `SUBS tmp, b, a` 의 N, 즉 **MI** 다(LT 가 아니다! LT 는 a, b 자체를 부호 있는 수로 비교해서 0x7FFFFFFF→0x80000000 경계에서 틀린다). 실제 컴파일 결과도 그렇다 — `demo_asm.c` 의 `tick_after` 는 arm64 `sub w8, w1, w0` / `lsr w0, w8, #31`, thumbv7em `subs r0, r1, r0` / `lsrs r0, r0, #31`: N 플래그를 조건 코드로 쓰는 대신 비트 31 을 직접 꺼냈다(값이 필요할 때는 이게 더 짧다).

| a (나중?) | b | b − a (모듈러) | 부호 | tick_after |
|---|---|---|---|---|
| 0x00000010 | 0xFFFFFFF0 | 0xFFFFFFE0 | 음 | 참 (랩어라운드 너머 0x20 틱 뒤) |
| 0xFFFFFFF0 | 0x00000010 | 0x00000020 | 양 | 거짓 |
| 100 | 100 | 0 | 0 | 거짓 |
| 0x80000001 | 0x00000001 | 0x80000000 | 음 | 참 |
| 0x00000001 | 0x80000001 | 0x80000000 | 음 | **참 — 둘 다 "뒤"** |

마지막 두 줄: 정확히 2^31 떨어지면 판정이 모호하다. 이 방식은 **두 시각의 차이가 2^31 미만**일 때만 의미가 있다 (1 kHz 틱이면 약 24.8일). 더 긴 타임아웃은 64비트 틱이나 오버플로 횟수 카운터를 함께 둔다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
# 34/34 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san
# 34/34 passed (ASan/UBSan 메시지 없음)
```

포인트:
- `alu_adc` 하나에 모든 산술이 모인다. `alu_sub = alu_adc(a, ~b, 1)`, `alu_sbc = alu_adc(a, ~b, cin)` — 실제 ALU 와 같은 구조라 플래그가 저절로 Arm 규칙을 따른다.
- `~b` 는 32비트 전체가 뒤집히지만 `alu_adc` 가 처음에 width 마스크로 자르므로 8비트에서도 맞다.
- `cond_holds` 는 표를 그대로 옮긴 `switch`. enum 순서를 Arm 인코딩과 맞춰 둔 덕에 반대 조건이 `cc ^ 1`.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| SUB 를 `alu_adc(a, ~b + 1u, 0)` (= `a + (-b)`) 로 | b=0 이면 `~0+1` 이 0 으로 넘쳐 C=0 이 된다(진짜는 borrow 없음 → C=1). b=INT_MIN 이면 `-b` 가 자기 자신이라 V 가 틀린다. solution.c 를 이렇게 바꾸면 **28/34** (실제 실행), hw 데모는 32비트 48건·8비트 512건 불일치 — 전부 b=0 또는 b=MSB 만 1 인 경우라 **무작위 테스트로는 거의 안 걸린다** | `alu_adc(a, ~b, 1, w)` — +1 은 cin 으로 |
| `uint32_t sum = a + b; C = sum < a;` 를 ADC 에도 사용 | cin=1, b=0xFFFFFFFF 이면 `a + b + 1 == a` 라 `sum < a` 가 거짓인데 캐리는 났다 | 64비트로 더하거나 `(sum < a) \|\| (cin && sum == a)` |
| V = C 로 생각 | C 는 부호 없는 넘침, V 는 부호 있는 넘침. 0xFF+0x01(C만), 0x7F+0x01(V만) | V = `((a^r)&(b^r))` 의 MSB |
| 부호 있는 `a < b` 를 MI(N) 로 판정 | 뺄셈이 넘치면 N 이 뒤집힌다 (0x80−0x01 → N=0) | LT = `N != V` |
| 부호 없는 비교를 x86 습관대로 "C=1 이면 작다" | Arm 뺄셈의 C 는 NOT borrow. `a < b` (unsigned) 는 **C=0** (LO) | LO = `!C`, HS = `C` |
| 64비트 비교 체인 뒤 `beq` | Z 는 hi 워드만 반영 (0x5_00000001 vs 0x5_00000000 → Z=1) | EQ 는 XOR/ORR, `CMP lo; IT EQ; CMPEQ hi`, 또는 AArch64 `CCMP` |
| 틱 비교를 `if (now >= deadline)` | deadline 이 0 을 넘어가면 틀림 | `(int32_t)(now - deadline) >= 0` |
| width=32 마스크를 `(1u << width) - 1` | 시프트 양 32 = UB (C-B01) | `(uint32_t)((UINT64_C(1) << width) - 1)` |

## 8. 면접 Q&A

<details>
<summary>Q1. `cmp r0, r1` 뒤에 `blo` 와 `blt` 의 차이는? 각각 어떤 플래그를 보나요?</summary>

`blo` 는 부호 없는 "less than" — `C == 0` (borrow 가 났다). `blt` 는 부호 있는 — `N != V`. C 소스에서 `unsigned` 비교면 컴파일러가 LO/HS/HI/LS, `int` 비교면 LT/GE/GT/LE 를 고른다 (`demo_asm.c` 실제 출력: `cset w0, lo` vs `cset w0, lt`).
꼬리질문: "그럼 `int` 와 `unsigned` 를 섞어 비교하면?" → 정수 승격/일반 산술 변환으로 int 쪽이 unsigned 로 바뀌어 **LO** 가 선택된다 → `-1 < 1u` 가 거짓 (C-B02).

</details>

<details>
<summary>Q2. Arm 은 왜 뺄셈에서 C=1 이 "borrow 없음" 인가요? x86 은?</summary>

뺄셈을 `a + ~b + 1` 로 같은 가산기에서 하기 때문이다. `a ≥ b` 면 합이 2^w 을 넘어 캐리가 1 → 그대로 C 에 저장. 회로 추가가 없고 SBC 도 `a + ~b + C` 로 같은 가산기를 쓴다. x86 은 뺄셈 때 캐리를 뒤집어 CF = borrow 로 저장하고 SBB 를 `a − b − CF` 로 정의했다 — 사람이 보기 쉬운 쪽. 덧셈에서는 두 ISA 의 C/CF 가 같다 (Rosetta 로 100만 쌍 확인: SUB 에서 `CF == !C`, ADD 에서 `CF == C`, 불일치 0).
꼬리질문: "Cortex-M 에서 `cmp r0, #0` 뒤 C 는?" → 항상 1. "x86 `cmp eax, 0` 뒤 CF 는?" → 항상 0.

</details>

<details>
<summary>Q3. 오버플로(V)는 하드웨어에서 어떻게 계산하나요? C 와 무슨 관계인가요?</summary>

V = MSB 로 들어간 캐리 XOR MSB 에서 나간 캐리 (연습 1 에서 1~8비트 전수 확인). 소프트웨어로는 `((a ^ r) & (b ^ r)) >> (w-1)` — 같은 부호 둘을 더했는데 결과 부호가 다르다. C 와는 독립: 0xFF+0x01 은 C 만, 0x7F+0x01 은 V 만, 0x80+0x80 은 둘 다.
꼬리질문: "부호 있는 두 수 덧셈에서 피연산자 부호가 다르면?" → 절대 넘치지 않는다 (V=0). "뺄셈이면?" → 부호가 **다를 때만** 넘칠 수 있다.

</details>

<details>
<summary>Q4. Cortex-M4 에서 `uint64_t` 덧셈과 `int64_t a < b` 는 어떤 명령이 되나요? `a == b` 는?</summary>

덧셈: `adds r0, r0, r2` / `adcs r1, r3`. 비교: `subs r0, r0, r2` / `sbcs.w r0, r1, r3` / `it lt` / `movlt r0, #1` — lo 의 borrow 가 C 로 hi 에 전달되어 최종 N·V·C 가 64비트 기준으로 맞다. 그러나 Z 는 hi 결과만 보므로 `==` 는 쓸 수 없어, 컴파일러는 `eors` 두 번 + `orrs` 로 만든다 (실제 출력: `eors r1, r3 / eors r0, r2 / orrs r0, r1 / clz / lsrs #5`).
꼬리질문: "128비트 암호용 big-int 덧셈은?" → `ADDS` 한 번 + `ADCS` 를 워드 수만큼 (연습 4). 루프로 만들면 카운터 감소·비교가 C 를 덮어쓰므로, 언롤하거나 캐리를 레지스터에 0/1 로 저장했다가 다음 반복에서 다시 C 로 되돌린다 (x86 은 `dec` 가 CF 를 건드리지 않아 이 루프가 쉽다).

</details>

<details>
<summary>Q5. 32비트 틱 카운터가 랩어라운드해도 타임아웃을 맞게 판정하는 방법은? 플래그로는?</summary>

`(int32_t)(now - deadline) >= 0` — 모듈러 뺄셈 후 차이의 부호만 본다. 플래그로는 `SUBS` 뒤 PL/MI (LT/GE 아님). 조건: 두 시각 차이가 2^31 미만이어야 한다; 정확히 2^31 이면 양쪽 다 "나중"이 된다(테스트로 확인). C 언어 관점에서 `uint32_t` 뺄셈은 정의된 모듈러 연산이고, 2^31 이상인 `uint32_t` 를 `int32_t` 로 바꾸는 것은 C11 6.3.1.3 에 따라 **구현 정의**(gcc/clang 은 모듈러) — 이식성을 원하면 `(now - deadline) < 0x80000000u` 로 쓴다.
꼬리질문: "`now >= deadline` 이 실제로 언제 깨지나?" → deadline 계산이 0xFFFFFFxx 근처에서 넘어가 작은 수가 될 때 — `now` 는 아직 큰 수라 즉시 만료된 것으로 판정.

</details>

<details>
<summary>Q6. RISC-V 에는 플래그가 없습니다. 64비트 덧셈과 부호 없는 비교는 어떻게 하나요?</summary>

`slt`/`sltu` 가 비교 결과를 레지스터에 0/1 로 쓰고, 분기는 `blt`/`bltu`/`beq` 가 두 레지스터를 직접 비교한다. 캐리는 `add lo, a_lo, b_lo; sltu c, lo, a_lo` (합이 피연산자보다 작으면 캐리)로 만들고 hi 에 더한다. 장점: 플래그가 암묵적 의존성(모든 명령이 쓰고 읽는 공유 상태)을 만들지 않아 아웃오브오더/재정렬이 쉽다. 단점: 다중 정밀도 연산에 명령이 더 든다. Arm 은 S 접미사(ADDS vs ADD)로 "플래그를 쓸지"를 명령마다 고르게 해 이 의존성을 줄였다.
꼬리질문: "Cortex-M0(ARMv6-M)에서는?" → 16비트 Thumb 데이터 처리 명령 대부분이 플래그를 항상 갱신하고(그래서 `adds`/`movs` 로 표기), IT 명령이 없어 조건부 실행은 조건 분기로만 한다.

</details>

<details>
<summary>Q7. 검증 엔지니어로서 ALU 플래그 RTL 을 어떻게 테스트하겠습니까?</summary>

1) 독립 오라클(넓은 정수로 부호/무부호 결과를 계산)로 골든 모델을 만든다 — 같은 식을 두 번 쓰지 않는다. 2) 8비트(또는 축소 폭) 파라미터 버전은 전수, 32/64비트는 경계값 전쌍(0, 1, MAX, MIN, MIN+1, −1, −2, 0x4000_0000 …) + 고정 시드 무작위. 3) cin 0/1, 연산 종류 전부. 4) 커버리지: N/Z/C/V 16조합 각각과 15개 조건의 참/거짓이 모두 나왔는지. 이 레슨의 `a + (-b)` 버그처럼 무작위로는 거의 안 걸리는 버그가 경계값에서 걸린다(b=0, b=MIN).
꼬리질문: "실리콘에서 확인은?" → 이 레슨의 `demo_hw_flags.c` 처럼 `mrs nzcv`/APSR 를 읽어 모델과 대조하는 디렉티드 테스트를 부트 ROM·post-silicon 테스트에 넣는다.

</details>

## 9. 추가 연습

### 연습 1 — V = (MSB 로 들어간 캐리) XOR (MSB 에서 나간 캐리)

`v_from_carries(a, b, cin, w)` 를 캐리 두 개로 구현하고, w = 1..8 전수에서 `alu_adc` 의 V 와 같음을 보여라.

<details>
<summary>답 보기</summary>

MSB 아래 (w−1) 비트만 더해 비트 w−1 로 넘어가는 캐리 = c_in(MSB), 전체를 더해 비트 w 로 넘어가는 캐리 = c_out. 두 피연산자 MSB 가 0,0 이면 c_out=0 이고 c_in=1 이면 결과 MSB 가 1 → 양+양=음 (V). 1,1 이면 c_out=1 이고 c_in=0 이면 결과 MSB 0 → 음+음=양 (V). 부호가 다르면 c_in = c_out 이라 V=0. 하드웨어 가산기에서 XOR 게이트 하나로 V 를 만드는 이유. (ARCH-B04 의 리플 가산기 마지막 단에 그대로 붙는다.)

</details>

### 연습 2 — 포화 덧셈 (QADD)

`sat_add32(a, b)` : 넘치면 INT32_MAX / INT32_MIN 으로 고정. V 플래그로 판정하라.

<details>
<summary>답 보기</summary>

`alu_add` 의 V 가 0 이면 결과 그대로, 1 이면 `a` 의 부호 쪽 끝 (V=1 이면 a, b 부호가 같으므로 a 만 보면 된다). Cortex-M4/M7 DSP 확장의 `QADD` 가 이 동작을 한 명령으로 하고, 포화가 일어났으면 APSR 의 끈끈한(sticky) Q 비트를 세운다. 오디오/제어 루프에서 랩어라운드 대신 포화가 필요한 이유: 0x7FFF 다음이 0x8000 으로 뒤집히면 최대 양수 출력이 최대 음수로 튄다.

</details>

### 연습 3 — 8비트 플래그를 32비트 ALU 로

`sub8_via32(a, b) = alu_sub(a<<24, b<<24, 32).nzcv` 가 모든 8비트 쌍에서 `alu_sub(a, b, 8).nzcv` 와 같음을 보여라 (ADD 도).

<details>
<summary>답 보기</summary>

아래 24비트가 둘 다 0 이라 그 안에서 캐리/borrow 가 생기지 않고, 비트 31..24 의 계산이 8비트 계산과 똑같다. 비트 31 이 MSB 이므로 N, V, C 가 같고, 결과 하위 24비트가 0 이라 Z 도 같다. `demo_hw_flags.c` 가 실제 AArch64 에서 8비트 플래그를 얻는 방법이 이것이다. ADC 에는 그대로 안 된다(cin 이 비트 0 에 들어가므로) — 그때는 a 의 하위 24비트를 모두 1 로 채워 cin 이 비트 24 까지 전파되게 하는 변형이 필요하고, 그러면 Z 가 깨진다.

</details>

### 연습 4 — 128비트 덧셈 (ADDS + ADCS 체인)

`addn(dst, a, b, n)` : 리틀 엔디언 워드 배열 덧셈, 마지막 캐리 반환. uint64_t 두 개로 만든 오라클과 50,000쌍 비교.

<details>
<summary>답 보기</summary>

`c = 0` 에서 시작해 각 워드마다 `alu_adc(a[i], b[i], c)` 의 C 를 다음 cin 으로. 첫 워드는 cin=0 이라 ADDS, 이후는 ADCS 와 같다. 오라클 쪽 128비트 캐리: `hi < ahi || (c0 && hi == ahi)` — 연습의 실수 항목(ADC 에서 `sum < a` 만 보는 것)을 피한 식. 암호 라이브러리(RSA/ECC big-int)의 핵심 루프다.

</details>

### 연습 5 — 반대 조건 = cc ^ 1

EQ..LE 14개 조건과 16개 NZCV 조합 전부에서 `cond_holds(cc ^ 1, f) == !cond_holds(cc, f)` 를 보여라.

<details>
<summary>답 보기</summary>

Arm 조건 필드 4비트의 상위 3비트가 "무엇을 볼지", 최하위 비트가 "참/거짓 뒤집기" 다. 그래서 EQ(0000)/NE(0001), HS/LO, MI/PL, VS/VC, HI/LS, GE/LT, GT/LE 가 짝. 컴파일러가 분기를 뒤집거나, Thumb `ITE` 블록의 "else" 쪽 조건을 만들 때 이 비트 하나만 바꾼다. AL(1110)^1 = 1111 은 반대("never")가 아니다 — 앞의 Part B 표 설명 참고.

</details>

### 연습 6 — 64비트 EQ: CMP lo; IT EQ; CMPEQ hi

`eq64_ccmp(a, b)` : lo 를 비교해서 같을 때만 hi 를 비교한 플래그로 EQ 를 판정하라. Part C 에서 체인 Z 가 틀리던 쌍에서 맞는지 확인.

<details>
<summary>답 보기</summary>

lo 가 다르면 Z=0 그대로 → NE. lo 가 같으면 hi 비교의 Z 가 곧 64비트 EQ. Thumb-2 에서는 `cmp r0, r2` / `it eq` / `cmpeq r1, r3`, AArch64 에서는 `ccmp`(조건이 거짓이면 즉시값 nzcv 로 플래그를 채움)로 한 흐름에 만든다. 이 플래그로는 **EQ/NE 만** 유효하다 — lo 가 같지 않을 때 남는 C/N/V 는 lo 비교의 것이라 64비트 대소 판정이 아니다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
# 16/16 passed   (ASan/UBSan 빌드도 16/16, 메시지 없음)
```

## 10. 핵심 정리

- ALU 는 가산기 하나: ADD `a+b+0`, ADC `a+b+C`, SUB `a+~b+1`, SBC `a+~b+C`. 플래그는 결과와 캐리에서만 나온다.
- N = 결과 MSB, Z = 결과 0, C = MSB 밖 캐리, V = 같은 부호 둘 → 다른 부호 (= c_in(MSB) ^ c_out). C 와 V 는 독립.
- **Arm 뺄셈 C = NOT borrow** (`cmp x,#0` → C=1). x86 CF = borrow (반대). 덧셈은 둘이 같다.
- 부호 없는 비교 HS/LO/HI/LS (C, Z), 부호 있는 비교 GE/LT/GT/LE (N==V, Z). **LT = N≠V, MI 아님.**
- 조건 인코딩 최하위 비트를 뒤집으면 반대 조건 (`cc ^ 1`).
- 32비트 코어 64비트: `ADDS/ADCS`, `CMP/SBCS`. 체인 뒤 N·C·V 는 맞고 **Z 는 hi 만** → EQ 는 XOR/ORR 나 CMPEQ/CCMP.
- 랩어라운드 틱 비교는 `(int32_t)(b - a) < 0` = 차이의 부호(MI), 유효 범위는 차이 &lt; 2^31.
- 실측: M6 `mrs nzcv` 와 모델이 32비트 8,001,152건 + 8비트 131,072건에서 불일치 0.

## 참고자료

- [Arm Architecture Reference Manual for A-profile (DDI 0487) — NZCV, condition codes, ADDS/ADCS/SUBS/SBCS/CCMP](https://developer.arm.com/documentation/ddi0487/latest)
- [Armv7-M Architecture Reference Manual (DDI 0403) — APSR, IT, 조건 실행](https://developer.arm.com/documentation/ddi0403/latest)
- Arm Community 블로그 "Condition Codes 1: Condition Flags and Codes" (Jacob Bramley) — 조건 코드 해설
- [Intel 64 and IA-32 Architectures Software Developer's Manual, Vol. 1 §3.4.3 EFLAGS](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- Harris & Harris, *Digital Design and Computer Architecture, ARM Edition* — 5장 ALU 와 플래그, 6장 조건 실행
- Linux `include/linux/jiffies.h` — `time_after()` / `time_before()`
- 관련 레슨: ARCH-B01 (2의 보수), ARCH-B04 (게이트 가산기), ARCH-B08 (ISA 인코딩), C-B02 (정수 승격)
