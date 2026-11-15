---
id: ARCH-B09
time: 70분
updated: 2026-11-11
---
# ARCH-B09 단일 사이클 CPU 시뮬레이터

## 학습 목표

- 명령 하나를 **fetch → decode(+레지스터 읽기) → execute(+메모리) → writeback → PC 갱신** 으로 쪼개서 C 인터프리터로 구현하고, 각 단계가 하드웨어 데이터패스의 어느 부분인지 말할 수 있다.
- **r0 고정**, **mod 2^16 덧셈**, **읽고 나서 쓰기(read-before-write)**, **분기 오프셋은 PC+1 기준** 같은 규칙을 테스트로 못 박는다.
- illegal 명령 · PC 범위 밖 · 데이터 주소 범위 밖을 **정밀 폴트(precise fault)** 로 보고하고(상태를 하나도 바꾸지 않음), 무한 루프를 **step limit** 로 끊는다. Cortex-M UsageFault/BusFault 와 연결해서 설명한다.
- 배열 합 · 피보나치 · memcpy · 선형 검색을 손으로 어셈블해서 돌리고, **명령 수 공식(6N+6, 8n+6 …)** 과 opcode 히스토그램·PC 프로파일로 검증한다.
- 히스토그램으로 **단일 사이클 클럭(가장 느린 명령이 결정)** 의 낭비를 계산해, 왜 실제 CPU는 멀티사이클/파이프라인(ARCH-M01)으로 가는지 숫자로 보여준다.

## 1. 문제

"이 ISA 를 실행하는 시뮬레이터를 30분 안에 짜 보세요" 는 펌웨어·실리콘 검증 면접의 단골 코딩 문제다 — 실리콘 검증에서는 이런 **명령어 집합 시뮬레이터(ISS, instruction set simulator)** 가 RTL 의 정답지(golden model)가 되고(RISC-V 의 Spike 가 대표 예), 펌웨어 쪽에서는 QEMU·Renode 같은 에뮬레이터와 부트 ROM 디버깅의 기본 원리다.

ISA 는 **ARCH-B08 ISA를 그대로 넣었다** (설계 이유·어셈블러·디스어셈블러는 ARCH-B08 참고). 여기서는 실행만 한다.

| 명령 | 15..12 | 11..9 | 8..6 | 5..3 | 2..0 | 의미 |
|---|---|---|---|---|---|---|
| ADD | 0000 | rd | rs | rt | 000 | rd ← rs + rt |
| ADDI | 0001 | rd | rs | imm6 | | rd ← rs + sext(imm6) |
| LD | 0010 | rd | rs | imm6 | | rd ← dmem[rs + sext(imm6)] |
| ST | 0011 | **rt** | rs | imm6 | | dmem[rs + sext(imm6)] ← rt |
| BEQ | 0100 | rs | rt | imm6 | | rs == rt 이면 PC ← PC + 1 + sext(imm6) |
| JAL | 0101 | rd | imm9 | | | rd ← PC + 1; PC ← PC + 1 + sext(imm9) |
| HALT | 1111 | 0 | 0 | 0 | 0 | 정지 |

레지스터 8개(r0 = 0 고정), 데이터는 16비트, **Harvard**: `imem[256]`, `dmem[256]` 둘 다 워드 주소. PC 는 명령 단위로 센다. 나머지 opcode, ADD 의 하위 3비트 ≠ 0, HALT 하위 12비트 ≠ 0 은 illegal.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b09-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">단일 사이클 데이터패스 — 한 클럭 안에 왼쪽에서 오른쪽까지 다 지나간다</text>
  <rect x="20" y="125" width="50" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="45" y="150" text-anchor="middle" fill="currentColor" font-weight="bold">PC</text>
  <rect x="100" y="105" width="95" height="80" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="147" y="140" text-anchor="middle" fill="currentColor">imem[256]</text>
  <text x="147" y="160" text-anchor="middle" fill="currentColor" font-size="12">워드 = 명령</text>
  <rect x="225" y="95" width="125" height="100" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="287" y="125" text-anchor="middle" fill="currentColor">decode</text>
  <text x="287" y="145" text-anchor="middle" fill="currentColor" font-size="12">regfile r0..r7</text>
  <text x="287" y="165" text-anchor="middle" fill="currentColor" font-size="12">rs, rt 읽기</text>
  <text x="287" y="183" text-anchor="middle" fill="currentColor" font-size="12">sext(imm)</text>
  <rect x="380" y="110" width="80" height="70" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="420" y="140" text-anchor="middle" fill="currentColor">ALU</text>
  <text x="420" y="160" text-anchor="middle" fill="currentColor" font-size="12">+ , ==</text>
  <rect x="490" y="105" width="95" height="80" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="537" y="140" text-anchor="middle" fill="currentColor">dmem[256]</text>
  <text x="537" y="160" text-anchor="middle" fill="currentColor" font-size="12">LD / ST</text>
  <rect x="615" y="115" width="50" height="60" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="640" y="150" text-anchor="middle" fill="currentColor" font-size="12">mux</text>
  <g stroke="currentColor" fill="none">
    <path d="M70 145 L98 145" marker-end="url(#b09-arr)"/>
    <path d="M195 145 L223 145" marker-end="url(#b09-arr)"/>
    <path d="M350 145 L378 145" marker-end="url(#b09-arr)"/>
    <path d="M460 145 L488 145" marker-end="url(#b09-arr)"/>
    <path d="M585 145 L613 145" marker-end="url(#b09-arr)"/>
    <path d="M440 180 L440 200 L600 200 L600 165 L613 165" marker-end="url(#b09-arr)"/>
    <path d="M665 145 L700 145 L700 235 L287 235 L287 197" marker-end="url(#b09-arr)"/>
    <path d="M45 125 L45 60 L98 60" marker-end="url(#b09-arr)"/>
    <path d="M170 60 L218 60" marker-end="url(#b09-arr)"/>
    <path d="M350 60 L403 60" marker-end="url(#b09-arr)"/>
    <path d="M440 108 L440 82" stroke-dasharray="4 3" marker-end="url(#b09-arr)"/>
    <path d="M478 50 L520 50 L520 28 L30 28 L30 123" marker-end="url(#b09-arr)"/>
  </g>
  <rect x="100" y="45" width="70" height="30" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="135" y="65" text-anchor="middle" fill="currentColor" font-size="12">PC + 1</text>
  <rect x="220" y="45" width="130" height="30" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="285" y="65" text-anchor="middle" fill="currentColor" font-size="12">PC + 1 + sext(imm)</text>
  <rect x="405" y="40" width="73" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="441" y="57" text-anchor="middle" fill="currentColor" font-size="12">next PC</text>
  <text x="441" y="72" text-anchor="middle" fill="currentColor" font-size="12">mux</text>
  <text x="528" y="58" fill="currentColor" font-size="12">BEQ taken / JAL → 목표</text>
  <text x="528" y="74" fill="currentColor" font-size="12">그 밖 → PC + 1</text>
  <text x="470" y="228" fill="currentColor" font-size="12">writeback: rd ≠ 0 일 때만</text>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="147" y="262">① fetch</text>
    <text x="287" y="262">② decode · read</text>
    <text x="420" y="262">③ execute</text>
    <text x="537" y="262">③' memory</text>
    <text x="640" y="262">④ writeback</text>
  </g>
  <text x="10" y="288" fill="currentColor" font-size="12">cpu_step() 한 번 = 이 그림 한 바퀴 = 1 클럭 → CPI 1. 클럭 주기는 가장 긴 경로(LD)가 정한다.</text>
</svg>
```

이 레슨의 약속(규칙)은 다음과 같다. 면접에서는 **이런 규칙을 먼저 말하고 짜는 것** 자체가 점수다.

| 항목 | 이 레슨의 규칙 |
|---|---|
| HALT | 완료된 명령으로 센다(`retired++`). PC 는 HALT 자리에 그대로. 이후 `cpu_step` 은 아무것도 안 하고 `CPU_HALTED` |
| 폴트 3종 | `CPU_ILLEGAL`(fault_info = 명령 워드), `CPU_PC_FAULT`(= 잘못된 PC), `CPU_DMEM_FAULT`(= 계산된 주소) |
| 정밀 폴트 | 폴트 난 명령은 레지스터·메모리·PC·카운터를 **하나도** 바꾸지 않는다. PC 는 그 명령을 가리킨다. 폴트는 끈적하다(sticky) |
| 분기 목표 | PC 는 `uint16_t` 라 mod 2^16 으로 계산. 범위 검사는 **다음 fetch 때** (분기 자체는 완료) |
| step limit | `cpu_run(c, max)` 가 max 개를 다 쓰면 `CPU_STEP_LIMIT` 를 **반환만** 한다. `c->status` 는 RUNNING 그대로 → 이어서 실행 가능 |

### Part A — fetch · decode · execute · writeback 루프

- `cpu_step(cpu_t *c)` — 명령 하나를 실행한다(위 그림 한 바퀴). `retired`, `op_hist[opcode]`, `pc_prof[pc]` 를 갱신한다.
- `cpu_run(cpu_t *c, uint64_t max_steps)` — 멈추거나 max 에 닿을 때까지 반복.
- 주어진 두 프로그램(손 어셈블, 소스는 주석)을 돌려서 최종 레지스터/메모리와 **명령 수 · opcode 히스토그램** 을 기대값과 비교한다.
  - 배열 합: `dmem[0] = N`, 배열 `dmem[16..]`, 결과 `dmem[1]` → 명령 수 **6N + 6**
  - 피보나치: `dmem[0] = n`, `dmem[16+i] = F(i)`, `r2 = F(n)`, `r3 = F(n+1)` → 명령 수 **8n + 6**. n = 24 에서 F(25) = 75025 가 16비트를 넘어 `r3 = 9489` 가 된다(mod 2^16).

### Part B — 면접관이 찌르는 경계 조건

같은 `cpu_step` 이 다음을 모두 지켜야 한다 (테스트가 하나씩 확인).

- r0 에 쓰는 ADDI / LD / JAL 은 버려지고 r0 는 항상 0.
- `0x7FFF + 1 = 0x8000`, `0xFFFF + 1 = 0`, `ADDI r2, r0, -32 = 0xFFE0` (imm6 부호 확장).
- `LD r1, 0(r1)`, `ADD r1, r1, r1` — 피연산자는 쓰기 **전에** 읽는다.
- `JAL r7, +2` 는 `r7 = PC + 1`, `PC = PC + 1 + 2`.
- `BEQ r0, r0, -1` (제자리 무한 루프) → step limit. 정확히 max 번째에 HALT 하면 LIMIT 이 아니라 HALTED.
- illegal(opcode 6~14, ADD funct ≠ 0, `0xF001`, 지워진 플래시 `0xFFFF`), 프로그램 끝에서 떨어지기, 음수 쪽 분기, dmem 256 이상/음수 주소 → 정밀 폴트.

### Part C — 펌웨어다운 사용

- `prog_memcpy` (주어짐) 로 ① 정상 복사 ② **겹치는 복사**(dst = src + 1 → 값이 번지는 memcpy vs memmove 함정) ③ **RAM 끝을 넘는 복사**(254, 255 는 써지고 256 에서 BusFault 처럼 정밀 폴트, fault_info = 256 = "BFAR") 를 확인한다.
- `prog_find[]` — 주석의 선형 검색 어셈블리를 **직접 손으로 기계어로** 옮긴다(라벨 → PC+1 기준 오프셋).
- `hottest_pc()` — PC 프로파일에서 가장 뜨거운 명령 찾기 (펌웨어의 PC 샘플링 프로파일러와 같은 생각).
- `single_cycle_ps()` / `variable_clock_ps()` — 히스토그램 × 명령별 지연으로 실행 시간을 계산하고 단일 사이클의 낭비를 숫자로 본다.

## 2. Problem (English)

> **Write an interpreter for a 16-bit toy ISA (single-cycle CPU model).**
>
> The machine has eight 16-bit registers (`r0` reads as 0, writes ignored), a 16-bit PC counting instructions, and separate word-addressed `imem[256]` / `dmem[256]`. Instructions: `ADD rd,rs,rt`, `ADDI rd,rs,imm6`, `LD rd,imm6(rs)`, `ST rt,imm6(rs)`, `BEQ rs,rt,off6`, `JAL rd,off9`, `HALT` (encoding given). Branch/jump offsets are relative to `PC+1`. All arithmetic is modulo 2^16.
>
> 1. Implement `cpu_status_t cpu_step(cpu_t *c)` that fetches, decodes, executes and writes back one instruction, and `cpu_status_t cpu_run(cpu_t *c, uint64_t max_steps)`. Count retired instructions, a per-opcode histogram and a per-PC profile.
> 2. Faults must be **precise**: on an illegal instruction, a fetch from `pc >= 256`, or a data access at an effective address `>= 256`, leave registers, memory, PC and counters unchanged, record the offending word/address, and stop. `cpu_run` returns `CPU_STEP_LIMIT` if the budget runs out, without corrupting state, so execution can be resumed.
> 3. Run the provided array-sum and Fibonacci programs and verify final state and instruction counts (`6N+6`, `8n+6`). Hand-assemble a linear search. Using per-opcode latencies (IM 200, RR 100, ALU 200, DM 200, WB 100 ps), compute single-cycle time (`retired × slowest instruction`) vs an ideal variable-length clock.
>
> Example: array sum with N = 10 and `a[i] = 1000(i+1)` halts with `dmem[1] = 55000` after 66 instructions (LD 11, ADD 11, ADDI 21, BEQ 21, ST 1, HALT 1); single-cycle 52 800 ps vs variable 39 400 ps.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄이 쭉 나오고 "6/48 passed" — TODO 6곳을 채워 48/48 을 만든다
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

TODO 는 `cpu_step`, `cpu_run`, `prog_find[]`, `hottest_pc`, `single_cycle_ps`, `variable_clock_ps` 6곳이다. 디코더 `decode()` 와 손 어셈블 매크로 `I_ADD(...)` 등은 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — cpu_step 의 뼈대</summary>

```c
if (멈춘 상태) return status;
if (pc >= 256) → PC_FAULT;            /* fetch */
w = imem[pc]; if (!decode(w,&d)) → ILLEGAL;
a = reg[d.rs]; b = reg[d.rt];         /* 먼저 읽는다 */
next = pc + 1; switch (d.op) {...}    /* execute: wb_val, wb, next 를 "계산만" */
if (wb && d.rd) reg[d.rd] = wb_val;   /* writeback */
retired++, hist, prof; pc = next;     /* commit */
```

`decode()` 가 ST 의 데이터 레지스터를 `d.rt` 에, BEQ 의 두 피연산자를 `d.rs/d.rt` 에 넣어주므로 execute 는 "rs 와 rt 를 읽는다" 로 통일된다.

</details>

<details>
<summary>힌트 2 — 정밀 폴트는 "검사 먼저, 쓰기 나중"</summary>

폴트가 날 수 있는 곳은 fetch, decode, 그리고 LD/ST 의 주소 계산 세 곳이다. 셋 다 **어떤 상태도 쓰기 전에** 확인하고 바로 `return` 한다. `retired++` 나 `pc = next` 를 switch 위에 두면 폴트 난 명령이 완료된 것처럼 세진다.

</details>

<details>
<summary>힌트 3 — mod 2^16 과 부호 확장</summary>

`uint16_t a, b; (uint16_t)(a + b)` — `a + b` 는 int 로 승격돼 계산되고(C-B02 참고), 캐스트가 mod 2^16 으로 자른다. 음수 imm 은 `(uint16_t)d.imm` 로 바꾸면 `0xFFFF` 같은 값이 되어 더하면 뺄셈이 된다. 유효 주소도 이렇게 계산한 뒤 `>= 256` 인지 본다 — `0 + (-1)` 은 `0xFFFF` 라서 자동으로 범위 밖이다.

</details>

<details>
<summary>힌트 4 — step limit 와 HALT 의 경계</summary>

루프 안에서 `cpu_step` 결과가 RUNNING 이 아니면 즉시 그 값을 돌려준다. 루프를 다 돌고 나서야 LIMIT. 그래야 "정확히 max 번째가 HALT" 일 때 HALTED 가 나온다. LIMIT 을 `c->status` 에 저장하면 이어서 실행할 수 없다.

</details>

<details>
<summary>힌트 5 — 손 어셈블 오프셋</summary>

오프셋 = 목표 라벨 주소 − (분기 명령 주소 + 1). 예: pc 4 의 `BEQ r1, r0, nf` 에서 nf = 11 이면 `11 - 5 = +6`. 뒤로 가는 `BEQ r0, r0, loop` (pc 10 → 4) 는 `4 - 11 = -7`. imm6 범위(-32..31)를 넘지 않는지도 확인.

</details>

## 5. 풀이

### Part A — 한 명령 = 4단계, 명령마다 쓰는 길이 다를 뿐

`cpu_step` 은 모든 명령에 대해 같은 4단계를 지나고, 명령 종류는 **각 단계에서 무엇을 하느냐** 만 바꾼다. 하드웨어에서는 이 표의 각 칸이 제어 신호(control signal)다 — P&H 교과서의 RegWrite, MemRead, MemWrite, MemtoReg, ALUSrc, Branch 가 바로 이 표를 비트로 만든 것.

| 명령 | ② 읽기 | ③ execute | ③' memory | ④ writeback | 다음 PC |
|---|---|---|---|---|---|
| ADD | rs, rt | rs + rt | — | rd ← ALU | PC+1 |
| ADDI | rs | rs + sext(imm6) | — | rd ← ALU | PC+1 |
| LD | rs | ea = rs + sext(imm6) | 읽기 dmem[ea] | rd ← 메모리 | PC+1 |
| ST | rs, rt(데이터) | ea | 쓰기 dmem[ea] ← rt | — | PC+1 |
| BEQ | rs, rt | rs == rt ? | — | — | 같으면 PC+1+sext(imm6) |
| JAL | — | PC+1+sext(imm9) | — | rd ← PC+1 | 목표 |
| HALT | — | — | — | — | PC (멈춤) |

핵심 구현 포인트 세 가지:

1. **읽기를 먼저 지역 변수로** (`a = reg[d.rs]; b = reg[d.rt];`). 하드웨어 레지스터 파일은 클럭 앞쪽에서 읽고 클럭 에지에서 쓰기 때문에 `ADD r1, r1, r1` 은 옛 r1 을 두 번 더한다. 인터프리터도 그 순서를 지켜야 한다.
2. **execute 는 계산만**: `wb_val`, `wb`, `next_pc` 를 정하고, 실제 상태 변경은 마지막에 한꺼번에(commit). 이게 Part B 의 정밀 폴트를 공짜로 만든다.
3. **r0**: writeback 에서 `rd != 0` 일 때만 쓴다. (다른 방법: 쓰고 나서 `reg[0] = 0` 으로 되돌리기. 결과는 같지만 "쓰기 자체가 없다" 가 하드웨어에 더 가깝다.)

**명령 수를 손으로 세기** — 면접에서 "이 루프는 몇 명령?" 을 바로 답할 수 있어야 한다.

| 프로그램 | 준비 | 반복 1회 | 탈출 | 합계 |
|---|---|---|---|---|
| 배열 합 | LD, ADDI, ADD = 3 | BEQ(안 감), LD, ADD, ADDI, ADDI, BEQ(뒤로) = 6 | BEQ(감), ST, HALT = 3 | **6N + 6** |
| 피보나치 | LD, ADDI×3 = 4 | BEQ, ST, ADD×3, ADDI×2, BEQ = 8 | BEQ(감), HALT = 2 | **8n + 6** |

실제 실행 결과는 §6 출력의 report 줄에 있다 (예: `sum N=200 retired=1206 LD=201 ST=1 ADD=201 ADDI=401 BEQ=401`). 배열 합 N=200 의 명령 구성을 보면 실제 일(LD + ADD)은 6개 중 2개뿐이고 나머지 4개(포인터 증가, 카운터 감소, 분기 2개)는 **루프 오버헤드** 다. 이게 루프 펼치기(추가 연습 1: 4N+6), DSP 의 zero-overhead loop, Armv8.1-M 의 저오버헤드 루프 명령(WLS/LE)이 존재하는 이유다. 피보나치 n=24 에서 `r3 = 9489` 는 75025 − 65536 — 16비트 머신에서 오버플로는 예외가 아니라 그냥 잘림이다 (ADD 에 플래그가 없으므로; 플래그 계산은 ARCH-B02 참고).

`demo_trace_bench.c` 로 피보나치 n=2 를 한 명령씩 추적한 실제 출력 (바뀐 레지스터/메모리만 표시; 6번째 줄 ST 는 0 을 0 위에 써서 변화가 안 보인다. 두 번째 바퀴는 줄임):

```text
trace: fib n=2  (변경된 레지스터/메모리만 표시)
step  pc  word  instruction        effect
   1   0  2200  LD   r1, 0(r0)     r1=2 
   2   1  1400  ADDI r2, r0, 0     
   3   2  1601  ADDI r3, r0, 1     r3=1 
   4   3  1A10  ADDI r5, r0, 16    r5=16 
   5   4  4207  BEQ  r1, r0, +7    
   6   5  3540  ST   r2, 0(r5)     
   7   6  0898  ADD  r4, r2, r3    r4=1 
   8   7  04C0  ADD  r2, r3, r0    r2=1 
   9   8  0700  ADD  r3, r4, r0    
  10   9  1B41  ADDI r5, r5, 1     r5=17 
  11  10  127F  ADDI r1, r1, -1    r1=1 
  12  11  4038  BEQ  r0, r0, -8    -> pc=4
  (13–20: 두 번째 바퀴 — dmem[17]=1, r4=2, r3=2, r5=18, r1=0 … 생략)
  21   4  4207  BEQ  r1, r0, +7    -> pc=12
  22  12  F000  HALT               
retired=22 (8n+6 = 22), halted=1
```

이런 "retire 트레이스"(pc, 명령 워드, 바뀐 레지스터/메모리)는 실리콘 검증에서 RTL 과 ISS 를 한 명령씩 비교(lockstep co-simulation)할 때 쓰는 형식과 같은 생각이다 (면접 Q4).

### Part B — 정밀 폴트: 검사는 앞에, 쓰기는 뒤에

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b09-arr2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">cpu_step 의 흐름 — 폴트 출구 3개는 모두 "아무것도 쓰기 전"</text>
  <g stroke="currentColor">
    <rect x="10" y="50" width="100" height="50" rx="6" class="f-mut"/>
    <rect x="135" y="50" width="100" height="50" rx="6" class="f-mut"/>
    <rect x="260" y="50" width="100" height="50" rx="6" class="f-mut"/>
    <rect x="385" y="50" width="110" height="50" rx="6" class="f-mut"/>
    <rect x="520" y="50" width="210" height="50" rx="6" class="f-ok"/>
    <rect x="10" y="170" width="100" height="50" rx="6" class="f-warn"/>
    <rect x="135" y="170" width="100" height="50" rx="6" class="f-warn"/>
    <rect x="385" y="170" width="110" height="50" rx="6" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="60" y="72">① fetch</text><text x="60" y="90">pc &lt; 256 ?</text>
    <text x="185" y="72">② decode</text><text x="185" y="90">legal ?</text>
    <text x="310" y="72">read rs, rt</text><text x="310" y="90">(지역 변수로)</text>
    <text x="440" y="72">③ execute</text><text x="440" y="90">LD/ST ea &lt; 256 ?</text>
    <text x="625" y="68">④ commit — 여기서만 상태 변경</text>
    <text x="625" y="86">reg · dmem · pc · 카운터</text>
    <text x="60" y="192">PC_FAULT</text><text x="60" y="210">info = pc</text>
    <text x="185" y="192">ILLEGAL</text><text x="185" y="210">info = 워드</text>
    <text x="440" y="192">DMEM_FAULT</text><text x="440" y="210">info = ea</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M110 75 L133 75" marker-end="url(#b09-arr2)"/>
    <path d="M235 75 L258 75" marker-end="url(#b09-arr2)"/>
    <path d="M360 75 L383 75" marker-end="url(#b09-arr2)"/>
    <path d="M495 75 L518 75" marker-end="url(#b09-arr2)"/>
    <path d="M60 100 L60 168" stroke-dasharray="4 3" marker-end="url(#b09-arr2)"/>
    <path d="M185 100 L185 168" stroke-dasharray="4 3" marker-end="url(#b09-arr2)"/>
    <path d="M440 100 L440 168" stroke-dasharray="4 3" marker-end="url(#b09-arr2)"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="66" y="140">아니오</text><text x="191" y="140">아니오</text><text x="446" y="140">아니오</text>
  </g>
  <text x="520" y="190" fill="currentColor" font-size="12">폴트: status 와 fault_info 만 쓴다.</text>
  <text x="520" y="208" fill="currentColor" font-size="12">pc 는 폴트 난 명령을 그대로 가리킨다.</text>
  <text x="10" y="252" fill="currentColor">→ 핸들러가 고쳐서 재실행하거나, 대신 실행하고 pc+1 로 넘어갈 수 있다 (추가 연습 4).</text>
  <text x="10" y="274" fill="currentColor" font-size="12">Cortex-M: UNDEFINSTR → UsageFault, 정밀 BusFault → BFAR. 스택에 쌓인 PC = 폴트 난 명령.</text>
</svg>
```

정밀(precise)하다는 것은 "폴트 직전 명령까지는 모두 완료, 폴트 난 명령과 그 뒤는 하나도 실행 안 됨" 이 보장된다는 뜻이다. 그래야 예외 핸들러가 **같은 명령을 다시 실행** 하거나(페이지 폴트 후 재시도) **대신 실행하고 건너뛸**(미구현 명령 에뮬레이션) 수 있다. 인터프리터에서는 "검사 → return" 을 commit 앞에 두기만 하면 된다. 반대로 Cortex-M3/M4 의 **imprecise BusFault**(쓰기 버퍼에 들어간 store 가 나중에 실패)는 이미 뒤 명령이 실행된 뒤에 보고되어 쌓인 PC 가 원인 명령이 아니고 BFAR 도 유효하지 않다 — 디버깅이 어려운 이유. (디버깅할 때 쓰기 버퍼를 끄는 설정이 있다는 것 정도만 기억하고, 정확한 비트는 해당 코어 TRM 으로 확인.)

Part B 테스트가 확인하는 것:

| 상황 | 결과 |
|---|---|
| `ADDI r0,r0,5`, `LD r0,0(r0)`, `JAL r0,+0` | r0 = 0, JAL r0 은 그냥 점프 (링크 버림) |
| `LD r1,0(r0)` (0x7FFF) → `ADDI r1,r1,1` | 0x8000 (16비트에선 부호 개념 없음, 그냥 덧셈) |
| `ADDI r4,r0,-1` → `ADDI r4,r4,1` | 0xFFFF → 0 |
| `ADDI r1,r0,5`; `LD r1,0(r1)`; `ADD r1,r1,r1` | 7 → 14 (읽고 나서 쓰기) |
| `BEQ r0,r0,-1` 1000 스텝 | LIMIT, retired 1000, status RUNNING → 5 더 → 1005 |
| ADDI, ADDI, HALT 를 max = 2 → 1 | LIMIT(2) → HALTED(3), 다시 step 해도 3 |
| pc 1 의 `0x6000` | ILLEGAL, pc = 1, retired = 1, r1 은 앞 명령의 값 9 그대로 |
| 프로그램 1줄 + 빈 imem(0x0000) | **255개 NOP** 실행 후 pc 256 에서 PC_FAULT, retired = 256 |
| 같은 프로그램 + 빈 imem 을 0xFFFF 로 | pc 1 에서 즉시 ILLEGAL |
| `BEQ r0,r0,-2` at pc 0 | pc = 0xFFFF → 다음 fetch 에서 PC_FAULT, 분기는 retired 1 로 셈 |
| `ST r1, 6(r1)` (r1 = 250), `LD r2, -1(r0)` | DMEM_FAULT info 256 / 0xFFFF, 저장·로드 안 일어남 |

**0x0000 이 NOP 라는 함정**: 이 ISA 에서 0x0000 = `ADD r0, r0, r0` = 아무 일도 안 하는 정상 명령이다. 그래서 0 으로 지워진 메모리로 뛰어들면 조용히 NOP 를 255개 실행하고 한참 뒤에야 폴트가 난다. 이 맥의 (오래된) `/usr/local/bin/llvm-mc` 로 확인해 보면 Thumb 에서도 0x0000 은 정상 명령이다:

```text
$ echo "0x00 0x00" | llvm-mc --disassemble -triple=thumbv7em-none-eabi -mcpu=cortex-m4
	movs	r0, r0
```

RISC-V 는 이 문제 때문에 **모든 비트가 0 인 명령을 일부러 illegal 로 정의** 했다(스펙에 "0 으로 채워진 메모리로 잘못 점프하면 바로 트랩되게" 라는 이유가 적혀 있다; 모든 비트가 1 인 것도 예약). 플래시를 지운 값(0xFF..)이 illegal 인 것도 같은 이유로 좋은 설계다 — 우리 ISA 에서 0xFFFF 는 HALT opcode 에 하위 비트가 1 이라 illegal.

**step limit 는 시뮬레이터의 워치독**: 펌웨어의 워치독(IWDG 등)이 "정해진 시간 안에 킥이 없으면 리셋" 이듯, 테스트용 ISS 는 "N 스텝 안에 HALT 안 하면 중단" 이 있어야 CI 가 무한 루프에서 멈추지 않는다. 다만 이것은 CPU 폴트가 아니므로 상태를 망가뜨리지 않고 **이어서 실행 가능** 해야 한다(디버거의 "일시정지" 와 같다).

### Part C — memcpy 함정, 손 어셈블, 프로파일, 시간 모델

**겹치는 복사**: 앞에서부터 복사하는 memcpy 에 `dst = src + 1` 을 주면 `{1,2,3,4}` 가 `{1,1,1,1,1}` 로 번진다 — 읽기 전에 이미 덮어썼기 때문. C 표준에서도 겹치는 `memcpy` 는 정의되지 않은 동작이고, 겹칠 수 있으면 `memmove`(dst > src 이면 뒤에서부터 복사)를 써야 한다. DMA 로 버퍼를 한 칸 미는 코드에서 실제로 나는 버그.

**RAM 끝 넘기**: `dst = 254, n = 4` → 254, 255 는 써지고 세 번째 ST 에서 `DMEM_FAULT`, `fault_info = 256`, `pc = 5`(ST 자리), `retired = 3 + 7 + 7 + 2 = 19`. 앞의 두 번 저장은 이미 완료된 명령이므로 남아 있다 — 정밀 폴트는 "프로그램 전체 롤백" 이 아니라 "폴트 난 명령 하나만 없던 일" 이다. 실제 칩에서는 이 주소가 BusFault 의 BFAR 에 남는다 (메모리 맵·버스 폴트 모델은 ARCH-B11 참고).

**선형 검색 손 어셈블** — 오프셋 계산표:

| pc | 명령 | 목표 | 오프셋 = 목표 − (pc+1) | 워드 |
|---|---|---|---|---|
| 4 | `BEQ r1, r0, nf` | 11 | 11 − 5 = +6 | 0x4206 |
| 6 | `BEQ r5, r2, found` | 12 | 12 − 7 = +5 | 0x4A85 |
| 10 | `BEQ r0, r0, loop` | 4 | 4 − 11 = −7 | 0x4039 |
| 11 | `ADDI r4, r0, -1` | — | imm6 = 0x3F | 0x183F |

명령 수: k 번째에서 찾으면 `4 + 7k + 3 + 2 = 7k + 9`, 못 찾으면 `4 + 7n + 1 + 3 = 7n + 8`. 키가 0xFFFF 인 경우(배열에 실제로 0xFFFF 가 있음)도 테스트한다 — "못 찾음 = 0xFFFF" 라는 반환 규약이 데이터와 겹치는, 실제 API 에서도 흔한 모호함이다(인덱스 4 를 돌려주므로 여기선 구분됨).

**PC 프로파일**: 배열 합 N=10 에서 `pc_prof[3] = 11`(루프 머리 BEQ 는 N+1 번), 루프 몸통은 10 번 → `hottest_pc = 3`, PC 별 합계 = retired. 펌웨어에서는 같은 일을 Cortex-M DWT 의 PC 샘플링(SWO 로 내보냄)이나 리눅스 `perf` 가 통계적으로 한다.

**시간 모델** — 교과서(P&H) 단일 사이클 예제의 단계 지연을 그대로 쓴다: 명령 메모리 200, 레지스터 읽기 100, ALU 200, 데이터 메모리 200, 레지스터 쓰기 100 ps.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">명령별 필요한 시간 vs 단일 사이클 클럭 (1 칸 = 100 ps)</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="58">ADD/ADDI</text><text x="10" y="93">LD</text><text x="10" y="128">ST</text>
    <text x="10" y="163">BEQ</text><text x="10" y="198">JAL</text>
  </g>
  <g stroke="currentColor" class="f-acc">
    <rect x="100" y="42" width="120" height="24"/><rect x="220" y="42" width="60" height="24"/><rect x="280" y="42" width="120" height="24"/><rect x="400" y="42" width="60" height="24"/>
    <rect x="100" y="77" width="120" height="24"/><rect x="220" y="77" width="60" height="24"/><rect x="280" y="77" width="120" height="24"/><rect x="400" y="77" width="120" height="24"/><rect x="520" y="77" width="60" height="24"/>
    <rect x="100" y="112" width="120" height="24"/><rect x="220" y="112" width="60" height="24"/><rect x="280" y="112" width="120" height="24"/><rect x="400" y="112" width="120" height="24"/>
    <rect x="100" y="147" width="120" height="24"/><rect x="220" y="147" width="60" height="24"/><rect x="280" y="147" width="120" height="24"/>
    <rect x="100" y="182" width="120" height="24"/><rect x="220" y="182" width="120" height="24"/><rect x="340" y="182" width="60" height="24"/>
  </g>
  <g stroke="currentColor" class="f-mut" stroke-dasharray="3 3">
    <rect x="460" y="42" width="120" height="24"/><rect x="520" y="112" width="60" height="24"/>
    <rect x="400" y="147" width="180" height="24"/><rect x="400" y="182" width="180" height="24"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="160" y="59">IM</text><text x="250" y="59">RR</text><text x="340" y="59">ALU</text><text x="430" y="59">WB</text><text x="520" y="59">낭비</text>
    <text x="160" y="94">IM</text><text x="250" y="94">RR</text><text x="340" y="94">ALU</text><text x="460" y="94">DM</text><text x="550" y="94">WB</text>
    <text x="160" y="129">IM</text><text x="250" y="129">RR</text><text x="340" y="129">ALU</text><text x="460" y="129">DM</text><text x="550" y="129">낭비</text>
    <text x="160" y="164">IM</text><text x="250" y="164">RR</text><text x="340" y="164">ALU(==)</text><text x="490" y="164">낭비</text>
    <text x="160" y="199">IM</text><text x="280" y="199">adder</text><text x="370" y="199">WB</text><text x="490" y="199">낭비</text>
  </g>
  <path d="M580 32 L580 216" stroke="currentColor" stroke-width="2"/>
  <text x="588" y="44" fill="currentColor" font-size="12">Tclk = 800 ps</text>
  <g fill="currentColor" font-size="12">
    <text x="600" y="62">600 ps</text><text x="600" y="97">800 ps (최장)</text><text x="600" y="132">700 ps</text>
    <text x="600" y="167">500 ps</text><text x="600" y="202">500 ps</text>
  </g>
  <text x="10" y="242" fill="currentColor">단일 사이클 CPU 는 모든 명령에 800 ps 를 준다 — 프로그램에 LD 가 하나도 없어도.</text>
  <text x="10" y="264" fill="currentColor" font-size="12">IM 명령 메모리 · RR 레지스터 읽기 · DM 데이터 메모리 · WB 레지스터 쓰기 (HALT 는 IM 만 = 200 ps 로 둔다)</text>
</svg>
```

- `single_cycle_ps = retired × max(lat_ps[])` — 클럭 주기는 **ISA 가 지원하는 가장 느린 명령** 이 정한다. 테스트의 `p_alu`(ADDI, ADD, HALT) 도 3 × 800 ps.
- `variable_clock_ps = Σ op_hist[op] × lat_ps[op]` — 명령마다 딱 필요한 만큼만 쓰는 상상의 클럭.

| 프로그램 | 단일 사이클 | 가변 클럭 | 비율 |
|---|---|---|---|
| 배열 합 N=10 | 52 800 ps | 39 400 ps | 1.34 |
| 배열 합 N=200 | 964 800 ps | 723 400 ps | 1.33 |
| 피보나치 n=24 | 158 400 ps | 116 100 ps | 1.36 |
| memcpy n=8 | 48 800 ps | 37 500 ps | 1.30 |

약 25% 가 낭비다. 하지만 "명령마다 다른 길이의 클럭" 은 실제로 만들 수 없다 → 해법은 ① 명령을 여러 짧은 사이클로 나누는 멀티사이클, ② 단계를 겹치는 **파이프라인**(같은 데이터패스를 5조각으로 잘라 매 클럭 하나씩 완료 → 클럭은 가장 긴 *단계* 200 ps 근처). 파이프라인과 그 대가(해저드)는 ARCH-M01/M02 에서 다룬다. 이 레슨의 `pc_prof`/`op_hist` 는 거기서 CPI 를 재는 출발점이 된다.

**디코더의 필드 추출은 하드웨어에선 전선, 소프트웨어에선 명령 하나** — `demo_decode_asm.c` 를 Cortex-M4 와 이 맥(Apple M6, arm64)으로 컴파일한 실제 asm (Apple clang 21.0.0; 지시어 줄과 `f_op`/`f_imm9` 는 생략하고 두 열로 나란히 배치):

```text
=== thumbv7em / cortex-m4 ===          === arm64 (M6) ===
f_rs:   ubfx  r0, r0, #6, #3           _f_rs:   ubfx  w0, w0, #6, #3
f_imm6: and   r0, r0, #63              _f_imm6: and   w8, w0, #0x3f
        eor   r0, r0, #32                       eor   w8, w8, #0x20
        subs  r0, #32                           sub   w0, w8, #32
f_imm6_shift:                          _f_imm6_shift:
        sbfx  r0, r0, #0, #6                    sbfx  w0, w0, #0, #6
next_pc_beq:                           _next_pc_beq:
        lsls  r1, r1, #26                       sbfx  w8, w1, #0, #6
        cmp   r2, r3                            cmp   w2, w3
        it    eq                                csel  w8, w8, wzr, eq
        addeq.w r0, r0, r1, asr #26             add   w8, w0, w8
        adds  r0, #1                            add   w8, w8, #1
        uxth  r0, r0                            and   w0, w8, #0xffff
```

비트 필드 꺼내기(`(w >> 6) & 7`)는 두 ISA 모두 `ubfx` 한 개. 부호 확장은 이식성 있는 `(x ^ 0x20) - 0x20` 형태는 3명령, 시프트 관용구(`(int32_t)(w << 26) >> 26`, 구현 정의에 기대는 형태)는 `sbfx` 한 개가 됐다. 흥미롭게도 `next_pc_beq` 안에서는 XOR 형태도 문맥 덕분에 arm64 `sbfx` / M4 `lsls`+`asr #26` 로 바뀌었다. 하드웨어 디코더에서는 이것들이 전부 **배선**(부호 확장 = 비트 5 를 위쪽 10개 선에 복사)이라 시간이 거의 들지 않는다.

**인터프리터 속도** (`demo_trace_bench.c`, 배열 합 N=240 을 20만 번 = 2.892억 명령, Apple M6, `-O2`). 다른 작업이 함께 돌던 상태(load average ≈ 8)라 실행마다 흔들려서 5번 잰 범위를 적는다 (한 번의 실제 출력 예: `decode every step: 289200000 instr in 0.849 s = 340.8 M instr/s (2.93 ns/instr)`, `predecoded table: … 0.629 s = 459.6 M instr/s (2.18 ns/instr)`):

| 방식 | 5회 범위 |
|---|---|
| 매 스텝 디코드 | 2.8 – 4.4 ns/명령 (227 – 355 M/s) |
| 미리 디코드한 표 | 2.0 – 2.8 ns/명령 (363 – 507 M/s) |

매번 같은 결과(`chk` 동일)이고 미리 디코드가 매번 1.3–1.6배 빨랐다. Harvard 라서 imem 이 실행 중에 바뀌지 않으니 미리 디코드가 안전하다 — 코드가 바뀔 수 있는 기계라면 이 표를 무효화해야 한다(면접 Q5).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 경고 없이 48/48
```

```text
Part A — 배열 합 · 피보나치
  sum N=10     retired=66    LD=11   ST=1   ADD=11   ADDI=21   BEQ=21   JAL=0 HALT=1  single=52800   var=39400   ps
  sum N=200    retired=1206  LD=201  ST=1   ADD=201  ADDI=401  BEQ=401  JAL=0 HALT=1  single=964800  var=723400  ps
  fib n=10     retired=86    LD=1    ST=10  ADD=30   ADDI=23   BEQ=21   JAL=0 HALT=1  single=68800   var=50300   ps
  fib n=24     retired=198   LD=1    ST=24  ADD=72   ADDI=51   BEQ=49   JAL=0 HALT=1  single=158400  var=116100  ps
Part B — r0 · wrap · 폴트 · step limit
Part C — memcpy · 선형 검색 · 프로파일 · 시간 모델
  memcpy n=8   retired=61    LD=11   ST=8   ADD=0    ADDI=24   BEQ=17   JAL=0 HALT=1  single=48800   var=37500   ps
  find miss    retired=78    LD=12   ST=1   ADD=1    ADDI=32   BEQ=31   JAL=0 HALT=1  single=62400   var=45800   ps
48/48 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 분기 목표를 `pc + imm` 으로 | 스펙은 PC+1 기준. 루프가 한 칸씩 어긋나 무한 루프나 엉뚱한 탈출 | `next = pc + 1; if (taken) next += imm;` |
| `retired++`, `pc = next` 를 switch 앞에 | dmem 폴트 난 명령도 완료로 세짐, pc 가 다음 명령을 가리켜 정밀성 깨짐 | 검사 → return 을 모두 끝낸 뒤 commit |
| writeback 을 execute 중간에 (`reg[rd] = ...` 후 `reg[rt]` 읽기) | `ADD r1, r1, r1` 같은 명령이 새 값을 읽는다 | rs/rt 를 먼저 지역 변수로 읽기 |
| `reg[d.rd] = v;` (r0 검사 없음) | `LD r0, ...` 뒤 r0 ≠ 0 → 이후 모든 `BEQ x, r0` 과 `ADD rd, rs, r0` 가 망가짐 | `if (wb && d.rd != 0)` |
| imm 을 부호 없는 0..63 으로 | `ADDI r1, r1, -1` 이 +63 이 되고 뒤로 가는 분기가 앞으로 간다 | `sext(w, 6)` / `sext(w, 9)` |
| 손 어셈블에서 `ADDI r5, r0, 32` | imm6 범위는 −32..31. 매크로는 조용히 `32 & 0x3F = 0x20` → sext = **−32** → 주소 0xFFE0 에서 폴트 (이 레슨 초안에서 실제로 했던 실수) | 범위 안의 값(16)을 쓰거나 `ADDI` 두 번. 어셈블러는 범위를 검사해 보고해야 한다(ARCH-B08) |
| ST 의 11..9 를 "목적지" 로 해석 | ST 에는 레지스터 목적지가 없다. 11..9 는 저장할 **데이터** 레지스터 | 디코더에서 ST 의 A 필드 → `rt` |
| STEP_LIMIT 을 `c->status` 에 저장 | 한 번 limit 에 걸리면 이어서 실행 불가, 디버거 "계속" 이 안 됨 | 반환값으로만 알리기 |
| HALT 에서도 `pc = pc + 1` | 멈춘 PC 가 HALT 다음을 가리켜 "어디서 멈췄나" 가 흐려진다 (규약 문제 — 정했으면 문서화) | `next_pc = c->pc` |
| `if (a + b == x)` 처럼 16비트 결과를 캐스트 없이 비교 | `uint16_t` 둘의 합은 int 로 승격 → 65536 이 그대로 남아 비교가 틀린다 (C-B02 참고) | `(uint16_t)(a + b) == x` |

## 8. 면접 Q&A

<details>
<summary>Q1. 단일 사이클 CPU 의 CPI 와 클럭 주기는? 왜 실제로는 안 쓰나요?</summary>

CPI = 1 (모든 명령이 한 클럭). 대신 클럭 주기 = 가장 긴 명령 경로(보통 load: 명령 메모리 → 레지스터 읽기 → ALU → 데이터 메모리 → 레지스터 쓰기). 그래서 짧은 명령도 그 시간을 다 쓴다 — 이 레슨 숫자로 약 25% 낭비. 또 명령 메모리와 데이터 메모리가 한 클럭에 둘 다 필요하니 Harvard(분리된 메모리)가 강제된다.
**꼬리질문: 파이프라인으로 바꾸면 무엇이 좋아지고 무엇이 생기나?** 처리량은 이상적으로 단계 수만큼 좋아지지만(클럭 ≈ 가장 긴 단계), 데이터 해저드(포워딩/스톨), 제어 해저드(분기 예측/플러시), 구조적 해저드가 생긴다 (ARCH-M01, ARCH-M02). 지연(latency)은 오히려 약간 늘어난다(파이프라인 레지스터 오버헤드).

</details>

<details>
<summary>Q2. r0 를 0 으로 고정하면 무엇이 좋은가요? ARM 은 왜 없나요?</summary>

명령 수 없이 여러 의사 명령이 공짜로 생긴다: `ADD rd, rs, r0` = MOV, `ADDI rd, r0, k` = 상수 로드, `BEQ r0, r0, L` = 무조건 분기, `BEQ rx, r0, L` = 0 비교, `JAL r0, L` = 링크 없는 점프, `LD rd, k(r0)` = 절대 주소 로드. 대가는 레지스터 하나를 잃는 것(8개 중 1개는 큼). MIPS·RISC-V 는 x0/$zero 를 쓴다.
**꼬리질문: AArch64 는?** 레지스터 번호 31 이 명령에 따라 XZR(0 레지스터) 또는 SP 로 해석된다 — 고정 0 레지스터를 "번호 하나를 문맥에 따라 재사용" 하는 방식으로 얻었다. 32비트 Arm/Thumb 에는 0 레지스터가 없고 `MOVS r0, #0` 같은 즉시값을 쓴다.

</details>

<details>
<summary>Q3. 정밀 예외(precise exception)란? 시뮬레이터에서 어떻게 보장했나요?</summary>

폴트가 보고될 때 "폴트 명령 앞의 모든 명령은 완료, 폴트 명령과 뒤 명령은 상태를 하나도 안 바꿈" 이 성립하는 것. 그래야 핸들러가 원인을 고치고 같은 명령을 재실행(페이지 폴트)하거나 대신 실행하고 PC+1 로 복귀(미구현 명령 에뮬레이션, 추가 연습 4)할 수 있다. 구현: 폴트 가능한 검사(fetch 범위, decode, 유효 주소)를 모두 commit 전에 하고, commit(레지스터/메모리/PC/카운터) 은 마지막에 한꺼번에.
**꼬리질문: 하드웨어에서 정밀성이 깨지는 예는?** Cortex-M3/M4 의 imprecise BusFault — 쓰기 버퍼에 넣고 다음 명령을 진행한 store 가 나중에 버스 에러를 받으면, 이미 뒤 명령들이 실행됐고 BFAR 도 유효하지 않다. 비순차 실행 코어는 ROB 로 "완료(retire)는 순서대로" 를 지켜 정밀성을 되찾는다(ARCH-A06).

</details>

<details>
<summary>Q4. 이런 ISS 를 RTL 검증에 어떻게 쓰나요?</summary>

RTL 시뮬레이션이 명령을 하나 retire 할 때마다 (pc, 명령 워드, 쓴 레지스터와 값, 메모리 쓰기) 를 내보내고, ISS 를 같은 프로그램으로 한 스텝 돌려 비교한다(lockstep co-simulation). 첫 불일치가 버그 위치다. RISC-V 진영에선 Spike 가 흔한 기준 모델이고(예: lowRISC Ibex 의 co-simulation 환경), 수만 개의 랜덤 명령 프로그램(riscv-dv 같은 생성기)으로 돌린다.
**꼬리질문: 인터럽트나 타이머처럼 비결정적인 것은?** ISS 는 사이클 타이밍을 모르므로 RTL 이 "이 명령 경계에서 인터럽트를 받았다", "이 MMIO 읽기 값은 이것" 을 알려주면 ISS 가 그대로 따라가게(주입) 한다. 그래서 비교 단위는 사이클이 아니라 retire 된 명령이다.

</details>

<details>
<summary>Q5. 인터프리터를 더 빠르게 하려면? (실측 숫자로)</summary>

① 미리 디코드(predecode): imem 을 한 번 디코드한 표를 쓰면 이 맥에서 2.8–4.4 → 2.0–2.8 ns/명령 (1.3–1.6배). ② threaded code / computed goto: switch 의 범위 검사와 간접 분기 하나를 없애고 분기 예측기가 명령별 다음 핸들러를 배우게 한다. ③ 블록 단위 동적 이진 변환(DBT/JIT): QEMU TCG 처럼 기본 블록을 호스트 코드로 번역해 캐시.
**꼬리질문: 코드가 실행 중에 바뀌면?** 미리 디코드 표/번역 캐시를 무효화해야 한다. 실제 CPU 도 같다 — Arm 에서 코드를 메모리에 쓴 뒤 실행하려면 D-cache clean, I-cache invalidate, 배리어(DSB/ISB)를 순서대로 해야 한다(부트로더가 RAM 으로 코드 복사 후 점프할 때, DMA 일관성은 ARCH-M12 참고). 이 ISA 는 Harvard 라 ST 로 imem 을 바꿀 수 없어 문제가 없다.

</details>

<details>
<summary>Q6. 이 ISA 로 함수 호출/복귀가 되나요? 인터럽트 복귀는?</summary>

JAL 로 호출하고 링크 레지스터에 PC+1 을 남길 수는 있지만, **레지스터 값으로 점프하는 명령(JALR/BX)이 없어서 복귀할 수 없다**. 그래서 이 레슨의 프로그램은 모두 함수 없이 쓴다 (왜 실제 ISA 에 간접 점프가 꼭 필요한지는 ARCH-B08 참고). 우회책은 "복귀 지점마다 다른 코드 복사(인라인)" 뿐.
**꼬리질문: 인터럽트 복귀는?** 하드웨어가 예외 진입 시 복귀 주소를 특별한 곳(RISC-V mepc, AArch64 ELR_ELx, Cortex-M 은 스택 프레임 + LR 의 EXC_RETURN 값)에 저장하고 전용 복귀 동작(mret/eret/EXC_RETURN 로 분기)으로 돌아온다 (ARCH-M10).

</details>

<details>
<summary>Q7. Harvard 와 von Neumann 의 차이와 Cortex-M 은 어느 쪽인가요?</summary>

Harvard: 명령과 데이터가 다른 메모리/버스 → 한 사이클에 fetch 와 load 를 동시에, 대신 코드를 데이터처럼 쓰기 어렵다(이 모델에서 ST 는 imem 을 못 바꾼다). von Neumann: 하나의 메모리 → 단순하지만 fetch 와 데이터 접근이 버스를 다툰다. Cortex-M3/M4 는 **주소 공간은 하나**(코드도 데이터처럼 읽을 수 있음)이고 **버스는 I-Code / D-Code / System 으로 분리** 된 "modified Harvard" 다.
**꼬리질문: 그래서 펌웨어에서 생기는 일은?** 플래시의 상수 테이블을 읽는 load 와 명령 fetch 가 같은 플래시를 두고 대기 상태(wait state)를 나눠 쓴다 → 플래시 대기 상태와 프리페치 버퍼가 성능을 좌우한다 (ARCH-B12).

</details>

## 9. 추가 연습

### 연습 1 — 루프 2배 펼치기: 6N+6 → 4N+6

N 이 짝수라고 가정하고 배열 합을 반복 1회에 원소 2개씩 처리하도록 다시 어셈블하라. N = 0, 2, …, 40 에서 결과가 원본과 같고 명령 수가 4N+6 인지 확인. 왜 이 ISA 에서는 "루프 회전(조건을 아래로)" 으로는 이득이 없나?

<details>
<summary>답 보기</summary>

반복 1회 = `BEQ`(가드) + `LD, ADD, LD 1(r2), ADD` + `ADDI r2,+2` + `ADDI r1,-2` + `BEQ`(뒤로) = 8명령 / 원소 2개 → 4N. 준비 3 + 탈출 3 은 그대로 → **4N + 6**. N=40: 246 → 166 명령. 루프 회전은 아래쪽에 "아직 남았으면 위로" = **BNE** 가 필요한데 이 ISA 에는 BEQ 뿐이라 결국 `BEQ 끝` + `BEQ r0,r0,위` 두 개가 되어 줄지 않는다 — ISA 에 BNE 가 왜 있는지 보여주는 예. N 이 홀수일 수 있으면 남은 1개를 처리하는 꼬리(epilogue)가 필요하다.

</details>

### 연습 2 — MMIO UART: 주소 0xFFFF

주소 디코더에 "유효 주소 0xFFFF = UART" 를 추가하라. ST 는 하위 8비트를 송신 버퍼에 넣고, LD 는 상태(bit0 = TX ready, 항상 1)를 돌려준다. 상태를 폴링한 뒤 `"OK\n"` 을 보내는 프로그램을 쓰고, MMIO 를 끄면 같은 프로그램이 첫 LD 에서 `DMEM_FAULT(0xFFFF)` 가 되는지 확인하라.

<details>
<summary>답 보기</summary>

`ST r1, -1(r0)` 의 유효 주소는 `0 + sext(0x3F) = 0xFFFF` — r0 와 음수 오프셋으로 주소 공간 맨 위를 바로 가리킨다. 'O' = 79 는 imm6 로 한 번에 못 만들어 `31 + 31 + 17` 로 쌓는다. 디코더 쪽 변경은 LD/ST 의 범위 검사 **앞에** `if (mmio && ea == 0xFFFF)` 분기 하나. dmem 은 하나도 안 바뀐다. 이게 메모리 맵 I/O 의 본질 — 같은 LD/ST 명령이 주소에 따라 RAM 또는 장치로 간다 (메모리 맵·디코더·버스 폴트는 ARCH-B11, `volatile` 이 왜 필요한지는 C-M01 참고).

</details>

### 연습 3 — 하드웨어 브레이크포인트와 step over

`run_until(c, bp, max)` 를 만들어라: pc == bp 인 명령을 **실행하기 전에** 멈춘다. 피보나치 n=5 에서 루프 머리(pc 4)에 걸면 몇 번 멈추나? 브레이크포인트에서 "계속" 을 누르면 어떻게 같은 자리에서 다시 멈추지 않게 하나?

<details>
<summary>답 보기</summary>

루프 머리 BEQ 는 n+1 = **6번** 실행되므로 6번 멈춘다(첫 정지 retired = 4, 다음 12 = 한 바퀴 8명령). 이어가기는 "첫 명령은 검사 없이 한 번 실행(step over)" — 구현에서 `i > 0 && pc == bp` 일 때만 멈춘다. 실제 디버거도 같은 일을 한다: Cortex-M 의 FPB(Flash Patch and Breakpoint) 비교기에 걸려 멈춘 뒤 계속하려면 디버거는 보통 그 브레이크포인트를 잠시 끄고 한 스텝 실행한 뒤 다시 켠다.

</details>

### 연습 4 — ISA 확장 SLTU 와 트랩 에뮬레이션

(B08 스펙 밖의 실험) ADD 의 funct = 001 을 `SLTU rd, rs, rt` (rs < rt 이면 1, 부호 없음) 로 정의하라. 이것으로 최댓값 찾기를 쓰고, ① 확장을 지원하는 코어 ② 지원하지 않는 코어 + illegal 트랩 핸들러가 SLTU 를 소프트웨어로 대신 실행 ③ 핸들러 없는 기본 코어, 세 경우를 비교하라.

<details>
<summary>답 보기</summary>

BEQ 만으로는 크기 비교가 사실상 불가능해서(값만큼 감소 루프를 돌아야 함) max 찾기에 SLT 류가 필요하다. 결과: ① 65 retired ② 57 retired + **8 traps** (= n, 트랩된 SLTU 는 retired 에 안 들어가 합이 같음), 결과 둘 다 0xFFFE ③ pc 5 에서 ILLEGAL, fault_info = SLTU 워드. ②가 가능한 이유가 **정밀 폴트** 다: pc 가 SLTU 를 가리키고 레지스터가 그 직전 상태라서 핸들러가 계산 → rd(≠0) 에 쓰기 → pc+1 → 재개. B08 이 "안 쓰는 funct 는 illegal" 로 예약해 둔 덕분에 나중에 확장을 넣을 수 있고, 옛 코어에서도 트랩으로 호환된다 (FPU 없는 코어의 부동소수점 명령 에뮬레이션과 같은 구조). 실제로는 트랩 한 번이 수십~수백 사이클이라 느리다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
```

```text
  N=40: rolled 246, unrolled 166 instructions
  max n=8: native 65 retired, emulated 57 retired + 8 traps
14/14 passed
```

## 10. 핵심 정리

- `cpu_step` = fetch(pc 범위) → decode(illegal) → **피연산자 먼저 읽기** → execute(+유효 주소 범위) → **commit 한 번에**(rd≠0 일 때만 쓰기, retired/hist/prof, pc = next).
- 분기 목표 = **PC + 1 + sext(imm)**. 산술·주소·PC 모두 mod 2^16 (`(uint16_t)(a + b)`).
- 정밀 폴트 = 검사를 commit 앞에 → 폴트 명령은 상태 변화 0, pc 는 그 명령. 이게 있어야 재실행·트랩 에뮬레이션이 된다. Cortex-M: UsageFault(UNDEFINSTR), 정밀 BusFault + BFAR / imprecise 는 PC·주소가 어긋남.
- step limit 은 반환값일 뿐 상태가 아니다(이어서 실행 가능) — 시뮬레이터의 워치독.
- 명령 수는 손으로 센다: 배열 합 6N+6, 피보나치 8n+6, memcpy 7n+5, 검색 7k+9 / 7n+8. 루프 오버헤드가 일의 2배 → 펼치기(4N+6), BNE·저오버헤드 루프 명령의 존재 이유.
- 단일 사이클: CPI 1, Tclk = 가장 느린 명령(LD 800 ps) → 이 레슨 프로그램에서 약 25% 낭비 → 멀티사이클/파이프라인(ARCH-M01).
- 0x0000 이 NOP 인 ISA 는 지워진 메모리로 뛰어들어도 조용하다(Thumb 도 `movs r0, r0`). RISC-V 는 all-zero 를 일부러 illegal 로 정의.
- 인터프리터 가속: 미리 디코드(이 맥에서 1.3–1.6배) → threaded code → DBT(QEMU TCG). 코드가 바뀌면 표/캐시 무효화.

## 참고자료

- Patterson & Hennessy, *Computer Organization and Design* (ARM / RISC-V Edition), Ch. 4 "The Processor" — 단일 사이클 데이터패스, 제어 신호, 단계 지연 예제(200/100/200/200/100 ps)
- Harris & Harris, *Digital Design and Computer Architecture, ARM Edition*, Ch. 7 — 단일 사이클 / 멀티사이클 / 파이프라인 프로세서
- [RISC-V ISA Manual (unprivileged) — 명령 길이 인코딩과 all-zero / all-one illegal 규약](https://github.com/riscv/riscv-isa-manual)
- [Spike RISC-V ISA Simulator](https://github.com/riscv-software-src/riscv-isa-sim)
- [QEMU 문서 — TCG (Tiny Code Generator) 동적 번역](https://www.qemu.org/docs/master/devel/tcg.html)
- [Arm ARMv7-M Architecture Reference Manual (DDI 0403) — 예외 모델, UsageFault/BusFault, 정밀/비정밀 폴트](https://developer.arm.com/documentation/ddi0403/latest)
- [Cortex-M4 Devices Generic User Guide (DUI 0553) — Fault handling, CFSR/BFAR](https://developer.arm.com/documentation/dui0553/latest)
- ARCH-B08 (ISA 인코딩·어셈블러), ARCH-B11 (메모리 맵·버스 폴트), ARCH-B12 (플래시 대기 상태), ARCH-M01/M02 (파이프라인), ARCH-M10 (예외 복귀), ARCH-A06 (ROB 와 정밀 예외)
