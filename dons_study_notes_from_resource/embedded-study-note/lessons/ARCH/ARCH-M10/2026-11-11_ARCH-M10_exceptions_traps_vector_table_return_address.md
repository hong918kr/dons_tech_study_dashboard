---
id: ARCH-M10
time: 90분
updated: 2026-11-11
---
# ARCH-M10 예외와 트랩: 벡터 테이블과 복귀 주소

## 학습 목표

- B08 toy ISA 를 **엄격한 상위집합(strict superset)** 으로 확장한다 (DIV, LDP/STP, SYSCALL, ERET, MFSR/MTSR). 65536 워드를 전부 돌려서 "B08 에서 합법이던 워드는 뜻이 그대로" 임을 증명한다.
- **폴트(fault)** 와 **트랩(trap)** 의 복귀 주소 규칙을 말하고 코드로 지킨다: 폴트는 EPC = 그 명령(고친 뒤 재실행), 트랩은 EPC = 다음 명령. 폴트 난 명령은 레지스터·메모리에 흔적 0.
- EPC / CAUSE / BADADDR / VBAR / STATUS.EXL 로 하드웨어 예외 진입 → 벡터 슬롯 → 핸들러 → ERET 를 구현하고, 핸들러 안 예외 = **이중 폴트 → 정지** 정책을 둔다.
- 핸들러 3종(건너뛰기, 고치고 재시도, SYSCALL 디스패처)을 손 어셈블해서 돌리고, 같은 개념을 Cortex-M(UsageFault/BusFault/SVCall, VTOR), AArch64(ELR/ESR/FAR/VBAR) 와 진짜 M6 시그널로 확인한다.
- 정밀 예외가 5단 파이프라인(ARCH-M01/M02)에서 무엇을 요구하고 얼마가 드는지 설명한다.

## 1. 문제

"0 으로 나누면 CPU 는 무엇을 하나요? 핸들러에서 돌아가면 어디로 가죠? SVC 번호는 어떻게 꺼내죠?" — 펌웨어 면접(HardFault 디버깅, SVC 기반 RTOS), 실리콘 검증(예외 진입 시퀀스 검증, ISS 와 RTL 비교), 커널 면접(시스템 콜, 페이지 폴트 재시도)에서 모두 나오는 질문이다. 핵심은 **"어떤 명령까지 완료됐고, 어디로 돌아가는가"** 를 정확히 말하는 것.

B09 의 CPU 는 폴트가 나면 그냥 멈췄다(정밀 폴트 정지). 이 레슨에서는 멈추는 대신 **하드웨어가 상태를 저장하고 벡터로 점프** 하게 만든다. ISA 는 **B08 ISA 의 상위집합** 이고, 코어는 **ARCH-B09 모델을 축소해 넣었다**.

| 명령 | 15..12 | 11..9 | 8..6 | 5..3 | 2..0 | 의미 |
|---|---|---|---|---|---|---|
| ADD/ADDI/LD/ST/BEQ/JAL/HALT | 0000–0101, 1111 | | | | | **B08 그대로** |
| DIV | 0110 | rd | rs | rt | 000 | rd ← rs / rt (부호 없음, 버림). rt = 0 → **DIV0 폴트** |
| LDP | 0111 | rd(짝수) | rs | imm6 | | rd ← dmem[ea], rd+1 ← dmem[ea+1]. ea 홀수 → **ALIGN 폴트** |
| STP | 1000 | rt(짝수) | rs | imm6 | | dmem[ea] ← rt, dmem[ea+1] ← rt+1. ea 홀수 → ALIGN |
| SYSCALL | 1001 | 0000 | imm8 | | | **트랩**: EPC ← PC+1, CAUSE ← imm8≪8 \| 6 |
| ERET | 1010 | 0 | 0 | 0 | 0 | PC ← EPC, EXL ← 0. EXL = 0 에서 실행하면 ILLEGAL |
| MFSR | 1011 | rd | 000 | 000 | sr | rd ← SR[sr] (sr 0..4) |
| MTSR | 1100 | 000 | rs | 000 | sr | SR[sr] ← rs (STATUS 는 bit0 EXL 만) |
| (예약) | 1101, 1110 | | | | | 여전히 illegal |

시스템 레지스터: `SR0 EPC` · `SR1 CAUSE` · `SR2 BADADDR` · `SR3 VBAR` · `SR4 STATUS(bit0 = EXL, 핸들러 실행 중)`. 원인 코드 = 벡터 슬롯 번호: **1 ILLEGAL · 2 IFETCH · 3 DIV0 · 4 ALIGN · 5 DMEM · 6 SYSCALL** (슬롯 0 은 리셋 자리로 비움). VBAR 리셋 값 = `0xFFFF`(없음) → 예외가 나면 B09 처럼 정밀하게 **멈춘다**(UNHANDLED).

**왜 "짝수 주소" 규칙인가**: 이 머신은 워드 주소라서 LD/ST 에는 "미정렬" 이 존재할 수 없다. 두 워드를 한 번에 옮기는 페어 명령에 "짝수 워드 주소" 를 요구하면 의미 있는 정렬 규칙이 생긴다 — ARMv7 의 LDRD/STRD·LDM/STM 이 워드 정렬을 요구하는 것(Cortex-M3/M4 에서 미정렬이면 UNALIGN_TRP 설정과 무관하게 UsageFault)과 같은 구조다. 덤으로 짝수 페어는 dmem 끝(256)이나 "줄/페이지 경계" 를 절대 걸치지 않아 **전부 아니면 없음** 이 공짜로 보장된다 (추가 연습 3).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m10-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">예외 진입과 복귀 — take_exception() 과 ERET 이 하는 일</text>
  <g stroke="currentColor">
    <rect x="15" y="50" width="160" height="70" rx="8" class="f-ok"/>
    <rect x="235" y="40" width="250" height="118" rx="8" class="f-acc"/>
    <rect x="545" y="50" width="180" height="70" rx="8" class="f-mut"/>
    <rect x="545" y="175" width="180" height="60" rx="8" class="f-mut"/>
    <rect x="235" y="175" width="250" height="60" rx="8" class="f-ok"/>
    <rect x="15" y="175" width="160" height="60" rx="8" class="f-warn"/>
    <rect x="15" y="265" width="160" height="50" rx="8" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="72" font-weight="bold">thread (EXL = 0)</text>
    <text x="30" y="92">사용자 프로그램</text>
    <text x="30" y="110">폴트 or SYSCALL</text>
    <text x="248" y="58" font-weight="bold">하드웨어 진입 (한 번에)</text>
    <text x="248" y="76">EPC ← 폴트: pc / 트랩: pc + 1</text>
    <text x="248" y="93">CAUSE ← iss≪8 | 원인</text>
    <text x="248" y="110">BADADDR ← 주소 또는 명령 워드</text>
    <text x="248" y="127">EXL ← 1, pc ← VBAR + 원인</text>
    <text x="248" y="146">레지스터·dmem·retired 그대로</text>
    <text x="558" y="72" font-weight="bold">벡터 슬롯</text>
    <text x="558" y="92">VBAR+원인: JAL r0, h</text>
    <text x="558" y="110">(SYSCALL 슬롯은 마지막)</text>
    <text x="558" y="197" font-weight="bold">핸들러</text>
    <text x="558" y="215">MFSR/MTSR, 레지스터 저장</text>
    <text x="558" y="230">EPC 그대로 / EPC+1</text>
    <text x="248" y="197" font-weight="bold">ERET</text>
    <text x="248" y="215">pc ← EPC, EXL ← 0</text>
    <text x="248" y="230">EXL = 0 에서 실행하면 ILLEGAL</text>
    <text x="28" y="197" font-weight="bold">핸들러 안 예외</text>
    <text x="28" y="215">EXL = 1 → DOUBLE_FAULT</text>
    <text x="28" y="230">EPC/CAUSE 는 첫 예외 유지</text>
    <text x="28" y="287" font-weight="bold">VBAR = 0xFFFF</text>
    <text x="28" y="304">UNHANDLED (B09 정지)</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M175 85 L233 85" marker-end="url(#m10-a)"/>
    <path d="M485 85 L543 85" marker-end="url(#m10-a)"/>
    <path d="M635 120 L635 173" marker-end="url(#m10-a)"/>
    <path d="M545 205 L487 205" marker-end="url(#m10-a)"/>
    <path d="M235 200 L195 200 L195 105 L177 105" marker-end="url(#m10-a)"/>
    <path d="M635 235 L635 255 L95 255 L95 237" stroke-dasharray="4 3" marker-end="url(#m10-a)"/>
    <path d="M250 158 L218 170 L218 290 L177 290" stroke-dasharray="4 3" marker-end="url(#m10-a)"/>
  </g>
  <text x="300" y="270" fill="currentColor" font-size="12">점선: 정지 경로 (핸들러 안 예외 / 벡터 없음)</text>
</svg>
```

### Part A — 상위집합 디코드와 예외 진입·복귀

- `decode()` 에 7개 명령을 추가한다(**TODO 1**). 예약 비트 ≠ 0, 홀수 페어 레지스터, sr ≥ 5 는 illegal.
- `take_exception()` (**TODO 2**): 이중 폴트 검사 → EPC/CAUSE/BADADDR 기록 → VBAR 없으면 UNHANDLED → EXL = 1, pc = VBAR + 원인.
- `cpu_step()` 의 확장 명령 실행 (**TODO 3**): DIV, LDP/STP, SYSCALL(완료 후 트랩), ERET, MFSR/MTSR.
- 테스트: 65536 워드 전수 상위집합 검사(B08 합법 20993개가 같은 필드로 디코드되는지), DIV/LDP/STP 경계, `prog_a`(스스로 VBAR 설정 → DIV → SYSCALL 5 → 핸들러가 CAUSE/EPC/STATUS 를 읽고 ERET).

### Part B — 정밀 예외: 면접관이 찌르는 경계

- 원인 14가지 각각에서 **레지스터·dmem·retired 가 그대로**, EPC = 폴트 명령, BADADDR = 주소 또는 명령 워드, pc = VBAR + 원인.
- 원인 우선순위: LDP `ea = 0x101` 은 홀수이면서 범위 밖 → **ALIGN 이 이긴다** (정렬 검사가 먼저). `0xFFFE` 는 짝수라 DMEM.
- SYSCALL 은 트랩이라 retire 하고 EPC = PC + 1.
- VBAR 미설정 = B09 동작 (B09 배열 합 66 명령 그대로). B09 에서 illegal 이던 `0x6000` 은 이제 `DIV r0,r0,r0` 라서 **DIV0** — 상위집합의 대가.
- 이중 폴트 3종: 핸들러가 폴트, 핸들러 안 SYSCALL, 벡터 주소가 imem 밖(VBAR = 250).
- 퍼즈 20만 회: 무작위 상태 + 무작위 명령 1개 → 예외면 정밀 불변식, B08 합법이면 B09 기준 모델과 결과 일치.

### Part C — 펌웨어다운 핸들러 3종

- **C1 건너뛰기**: 버킷 평균 `avg[i] = sum[i] / cnt[i]`, cnt = 0 이면 DIV0 → 핸들러가 에러 수를 세고 `EPC + 1` → DIV 는 없던 일, 결과 자리에 센티널 0xFFFF 가 남는다.
- **C2 고치고 재시도**: 같은 프로그램, 핸들러가 피연산자(규약상 제수 레지스터 r4)를 1 로 고치고 EPC 그대로 ERET → 같은 DIV 를 다시 실행. 원인을 안 고치는 핸들러는 **재시도 폭주**.
- **C3 SYSCALL 디스패처** (**TODO 4**, 손 어셈블): putc / 호출 수 / exit / 모르는 번호 → 0xFFFF. CAUSE 상위 바이트를 꺼내려고 **DIV 를 시프트 대신** 쓴다.

## 2. Problem (English)

> **Add precise exceptions and traps to a toy CPU.**
>
> Start from a single-cycle interpreter for a 16-bit ISA (eight registers, `r0 = 0`, word-addressed `imem[256]`/`dmem[256]`). Extend the ISA as a strict superset using free opcodes: `DIV rd,rs,rt` (unsigned; divide-by-zero faults), `LDP/STP rd,imm(rs)` (register pair rd, rd+1; rd must be even, the effective word address must be even or an alignment fault is raised), `SYSCALL imm8`, `ERET`, `MFSR/MTSR` for system registers `EPC, CAUSE, BADADDR, VBAR, STATUS(EXL)`.
>
> 1. Faults (illegal instruction, fetch out of range, DIV0, alignment, data address out of range) are **precise**: the faulting instruction has no architectural side effect and does not retire; `EPC` = its PC. `SYSCALL` is a **trap**: it retires and `EPC = PC+1`. On entry: `CAUSE = iss<<8 | code`, `BADADDR` = faulting address (or instruction word for ILLEGAL), `EXL = 1`, `PC = VBAR + code`. `ERET` returns to `EPC` and clears `EXL`; executing it with `EXL = 0` is illegal.
> 2. Any exception while `EXL = 1` is a double fault: stop, keep the first exception's `EPC/CAUSE`. If `VBAR` is unset, stop with the state recorded (same as the base CPU).
> 3. Write handlers that (a) skip the faulting instruction, (b) fix the operand and retry, (c) dispatch system calls by number.
>
> Example: with `VBAR = 0x40`, `SYSCALL 5` at PC 6 enters at `0x46` with `EPC = 7`, `CAUSE = 0x0506`; the handler's `ERET` resumes at 7.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
# 처음엔 FAIL 이 쭉 나오고 "2/39 passed" — TODO 4곳을 채워 39/39 를 만든다
```

TODO 는 `decode()` 확장 case(1), `take_exception()`(2), `cpu_step()` 의 확장 명령 case(3), `prog_sys_handler[]` 손 어셈블(4). B08 명령의 실행, 이미지 배치 도우미 `vec_init / vec_jump / place`, 손 어셈블 매크로 `I_DIV(...)` 등, 비교 기준 모델(B08 디코더, B09 한 스텝)과 테스트는 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — 폴트는 "검사 → return", 트랩은 "commit → 진입"</summary>

B09 의 `cpu_step` 구조(검사는 commit 앞)가 그대로 정밀 폴트를 준다. 바뀌는 건 `return c->status = CPU_ILLEGAL` 자리가 `return take_exception(c, EXC_ILLEGAL, w, pc, 0)` 이 되는 것뿐. SYSCALL 은 반대로 명령이 **완료된 뒤** (retired++, pc = pc+1) 진입해야 EPC = 다음 명령이 된다.

</details>

<details>
<summary>힌트 2 — take_exception 의 순서</summary>

EXL 검사를 **맨 먼저**. EPC 를 먼저 쓰고 EXL 을 보면 이중 폴트 때 첫 예외의 EPC 가 덮인다 — 디버거가 원래 원인을 잃는다. VBAR 없음 검사는 기록 뒤에 (멈춰도 CAUSE/EPC 는 남아야 사후 분석이 된다).

</details>

<details>
<summary>힌트 3 — LDP/STP: 정렬 → 범위 → 쓰기</summary>

`ea & 1` 을 먼저, 그다음 `ea >= 256`. 짝수 ea 가 범위 안이면 ea ≤ 254 라서 ea+1 도 자동으로 범위 안 → 두 번째 워드에 대한 검사가 필요 없다. STP 의 두 번째 데이터 레지스터는 `reg[rt + 1]` — 디코더가 rt 를 짝수로 보장하므로 7 을 넘지 않는다. LDP 의 rd = r0 이면 r0 쪽만 버리고 r1 은 쓴다.

</details>

<details>
<summary>힌트 4 — ERET 과 MTSR</summary>

ERET 은 EXL = 0 이면 ILLEGAL 예외(이때 `w` 를 BADADDR 로). 아니면 `next_pc = EPC`, EXL 을 내린다. MTSR STATUS 는 bit0 만 저장 — `a & STATUS_EXL`.

</details>

<details>
<summary>힌트 5 — 디스패처 손 어셈블</summary>

오프셋 = 목표 − (pc + 1). 주석에 주소가 다 있다: 0x51 의 `BEQ r4, r0, putc` 는 0x58 − 0x52 = +6. 번호 꺼내기는 `CAUSE / 256` — 256 은 `ADDI r5, r0, 16` 후 `ADD r5, r5, r5` 네 번.

</details>

## 5. 풀이

### Part A — 상위집합: "옛 프로그램은 그대로, 옛 illegal 은 새 의미"

전수 검사 결과 (§6 출력 첫 줄):

| 분류 | 워드 수 | 계산 |
|---|---|---|
| B08 합법 (뜻 그대로) | 20993 | ADD 2⁹ + (ADDI·LD·ST·BEQ) 4×2¹² + JAL 2¹² + HALT 1 |
| 새로 합법 | 4945 | DIV 512 · LDP 2048(rd 짝수 4 × rs 8 × imm 64) · STP 2048 · SYSCALL 256 · ERET 1 · MFSR 40 · MTSR 40 |
| 여전히 illegal | 39598 | 1101/1110 전부, 예약 비트 ≠ 0, 홀수 페어 레지스터 … |

"상위집합" 은 **합법 → 합법(같은 뜻)** 만 보장한다. 반대 방향(옛 illegal → 여전히 illegal)은 보장하지 않는다: B09 테스트가 illegal 로 쓰던 `0x6000` 은 이제 `DIV r0, r0, r0` 이다. 그래서 실제 ISA 는 "예약(reserved) 인코딩은 미래에 쓰일 수 있으니 **의존하지 말라**" 고 못 박고(RISC-V, Arm 모두), 확장 존재 여부는 별도 레지스터(RISC-V `misa`, Arm ID 레지스터)로 알린다. B09 연습 4 의 SLTU 트랩 에뮬레이션이 가능했던 것도 예약 인코딩이 **반드시 트랩** 했기 때문 — 확장을 넣을 자리를 illegal 로 남겨 두는 것이 ISA 설계의 핵심 규율이다.

`prog_a` 실행 결과: `r4 = 142` (1000/7), 핸들러가 읽은 `CAUSE = 0x0506`(번호 5, 원인 6), `EPC = 7`(SYSCALL 은 pc 6), 핸들러 안 STATUS = 1, ERET 뒤 STATUS = 0, retired = 사용자 10 + 핸들러 4 = 14. SYSCALL 슬롯을 **마지막(6번)** 에 둔 덕분에 핸들러가 점프 없이 슬롯 자리에서 바로 시작한다 — AArch32 고전 벡터 테이블에서 FIQ 가 마지막 슬롯이라 핸들러를 그 자리에 바로 이어 쓸 수 있던 것과 같은 트릭.

**벡터 테이블 설계 선택: 원인별 슬롯(1 명령) + 슬롯 안에 `JAL r0, handler`**. 대안은 "진입점 하나 + 핸들러가 CAUSE 로 분기" 다.

| 방식 | 예 | 장점 | 단점 |
|---|---|---|---|
| 원인별 슬롯 (이 레슨) | AArch32 고전 벡터(슬롯마다 분기 명령) | 디스패치 코드 0, 원인별 지연 최소 | 슬롯 수 = 원인 수, 슬롯이 1 명령이라 점프 필요 |
| 주소 테이블 | Cortex-M (VTOR + 4×예외 번호, 하드웨어가 주소를 **읽어서** 점프) | 핸들러가 그냥 C 함수 (하드웨어가 레지스터를 스택에 쌓음) | 진입 시 메모리 읽기가 더 필요 |
| 진입점 하나 + 소프트웨어 디스패치 | RISC-V mtvec direct, AArch64 VBAR_EL1 의 동기 예외 진입점 + ESR.EC 디스패치 | 테이블 작음, 원인을 무한히 늘릴 수 있음 | 매 예외마다 비교·분기 비용 |

이 ISA 에는 간접 점프(JALR)가 없어서 "CAUSE 로 점프 테이블" 을 만들 수 없다. 단일 진입점이면 BEQ 비교 사슬이 필요하고 원인이 늘수록 느려진다 — 추가 연습 1 에서 재보면 DIV0 는 예외당 10 → 12 명령, SYSCALL 은 6 → 11 명령. 그래서 원인별 슬롯을 골랐다. (AArch64 의 VBAR_EL1 은 "어느 EL 에서 왔나 × 동기/IRQ/FIQ/SError" 16 개 진입점(각 0x80 바이트)이고, 동기 예외 안의 세부 원인은 소프트웨어가 ESR_EL1.EC 로 가른다 — 두 방식의 혼합.)

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m10-b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">imem 배치와 복귀 지점 — 폴트는 같은 명령으로, 트랩은 다음 명령으로</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="230" height="150" rx="4" class="f-ok"/>
    <rect x="320" y="40" width="190" height="150" rx="4" class="f-acc"/>
    <rect x="560" y="40" width="160" height="150" rx="4" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="58" font-weight="bold">0x00 사용자 (prog_avg)</text>
    <text x="30" y="80">5: LD   r4, 16(r2)   ; cnt</text>
    <text x="30" y="98">6: ADDI r5, r0, -1   ; 0xFFFF</text>
    <text x="30" y="118" font-weight="bold">7: DIV  r5, r3, r4  ← DIV0</text>
    <text x="30" y="138">8: ST   r5, 8(r2)</text>
    <text x="30" y="156">9: ADDI r2, r2, 1</text>
    <text x="30" y="178">(SYSCALL 이면 EPC = 다음 명령)</text>
    <text x="330" y="58" font-weight="bold">0x40 벡터 (VBAR)</text>
    <text x="330" y="78">+0 (리셋 자리) HALT</text>
    <text x="330" y="94">+1 ILLEGAL   HALT</text>
    <text x="330" y="110">+2 IFETCH    HALT</text>
    <text x="330" y="126" font-weight="bold">+3 DIV0  JAL r0, 0x48</text>
    <text x="330" y="142">+4 ALIGN     HALT</text>
    <text x="330" y="158">+5 DMEM      HALT</text>
    <text x="330" y="174">+6 SYSCALL → 그 자리에서 시작</text>
    <text x="570" y="58" font-weight="bold">0x48 핸들러</text>
    <text x="570" y="80">C1: EPC ← EPC + 1</text>
    <text x="570" y="98">C2: r4 ← 1, EPC 그대로</text>
    <text x="570" y="118">ERET</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M250 114 L318 124" marker-end="url(#m10-b)"/>
    <path d="M510 124 L558 100" marker-end="url(#m10-b)"/>
    <path d="M640 190 L640 250 L130 250 L130 141" marker-end="url(#m10-b)"/>
    <path d="M600 190 L600 230 L200 230 L200 121" stroke-dasharray="4 3" marker-end="url(#m10-b)"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="210" y="225">C2 재시도: ERET → 7 (DIV 다시)</text>
    <text x="140" y="268">C1 건너뛰기: ERET → 8 (DIV 는 retire 안 됨, r5 = 0xFFFF 남음)</text>
    <text x="10" y="290">하드웨어는 원인을 기록할 뿐, "건너뛸지 다시 할지" 는 핸들러가 EPC 로 정한다.</text>
  </g>
</svg>
```

### Part B — 정밀성: 검사는 commit 앞, 원인은 정해진 순서로

`cpu_step` 의 예외 출구는 B09 의 폴트 출구 자리 그대로다 (fetch → decode → execute 검사 → commit). 테스트가 확인하는 14가지 (실제 출력):

```text
  case              cause      EPC BADADDR      pc retired  state
  opcode 1101       ILLEGAL 0x0001  0xD000  0x0041       1  regs/dmem unchanged
  ADD funct!=0      ILLEGAL 0x0000  0x0001  0x0041       0  regs/dmem unchanged
  LDP odd rd        ILLEGAL 0x0000  0x7600  0x0041       0  regs/dmem unchanged
  SYSCALL rsv bits  ILLEGAL 0x0000  0x9100  0x0041       0  regs/dmem unchanged
  MFSR sr=5         ILLEGAL 0x0000  0xB205  0x0041       0  regs/dmem unchanged
  ERET in thread    ILLEGAL 0x0000  0xA000  0x0041       0  regs/dmem unchanged
  BEQ to 0xFFFF     IFETCH  0xFFFF  0xFFFF  0x0042       1  regs/dmem unchanged
  DIV by r0         DIV0    0x0001  0x0000  0x0043       1  regs/dmem unchanged
  LDP ea=3          ALIGN   0x0001  0x0003  0x0044       1  regs/dmem unchanged
  STP ea=255        ALIGN   0x0001  0x00FF  0x0044       1  regs/dmem unchanged
  LDP ea=0x101      ALIGN   0x0001  0x0101  0x0044       1  regs/dmem unchanged
  STP ea=0x100      DMEM    0x0001  0x0100  0x0045       1  regs/dmem unchanged
  LDP ea=0xFFFE     DMEM    0x0001  0xFFFE  0x0045       1  regs/dmem unchanged
  LD -1(r0)         DMEM    0x0000  0xFFFF  0x0045       0  regs/dmem unchanged
```

읽는 법: "retired" 는 **폴트 명령 앞의 준비 명령 수** 다 (폴트 명령 자체는 0 기여). `BEQ to 0xFFFF` 는 BEQ 자체는 완료(retired 1)되고 **다음 fetch** 에서 IFETCH — EPC = 0xFFFF 라 이 예외는 재실행으로 고칠 수 없다(분기 대상이 잘못된 것; 이런 경우 핸들러는 보통 "죽은 태스크 정리" 로 간다). Cortex-M 에서는 같은 상황이 IACCVIOL/IBUSERR 류로, 스택에 쌓인 PC 가 잘못된 대상 주소이고 **LR 이 마지막 정상 호출 지점** 을 가리켜 HardFault 분석의 단서가 된다.

**BADADDR 에 명령 워드를 넣은 이유**: 이 머신은 Harvard 라 핸들러가 imem 을 LD 로 읽을 수 없다. 그래서 ILLEGAL 일 때는 하드웨어가 명령 워드를 넘겨줘야 에뮬레이션이 가능하다 — RISC-V 의 `mtval` 이 illegal instruction 에서 명령 비트를 담을 수 있게 한 것과 같은 이유다. Cortex-M 의 SVC 핸들러는 반대로 "스택의 PC − 2 에서 명령을 **읽어** imm8 을 꺼낸다" (코드와 데이터가 한 주소 공간이라 가능).

**원인 우선순위**: 한 명령에 여러 원인이 겹칠 때(0x101 = 홀수 + 범위 밖) 무엇을 보고할지는 아키텍처가 **정해야** 한다 — RTL 과 ISS 가 다른 원인을 내면 lockstep 비교가 깨진다. 이 모델은 fetch > decode > (DIV0 | ALIGN > DMEM). Arm A-profile 도 정렬 폴트를 MMU 변환 폴트보다 먼저 보는 것으로 알고 있지만, 정확한 우선순위 표는 Arm ARM 의 해당 절을 직접 확인할 것.

**이중 폴트**: 세 테스트 모두 `CPU_DOUBLE_FAULT` 로 멈추고 EPC/CAUSE 는 **첫 예외** 를 가리킨다 (`df_cause` = 두 번째 원인). 실제 대응:

| 아키텍처 | 핸들러 안 예외 |
|---|---|
| 이 모델 | EXL = 1 이면 무조건 정지 (Cortex-M lockup 과 비슷) |
| Cortex-M | 처리 못 하는 폴트는 HardFault 로 승격(escalation), HardFault/NMI 안에서 또 폴트 → **lockup** (코어 정지, 보통 워치독/시스템 리셋으로 회복) |
| x86 | 예외 처리 중 예외 → #DF(double fault), 그 안에서 또 → triple fault = 리셋 |
| RISC-V | 하드웨어는 그냥 mepc/mcause 를 덮어씀 → 핸들러가 스스로 조심해야 함 (최근 double-trap 검출 확장이 추가됨 — 이름·세부는 스펙 확인) |

**퍼즈** (실제 출력): `fuzz 200000: ILLEGAL 102270 IFETCH 5337 DIV0 305 ALIGN 5369 DMEM 9744 SYSCALL 667 double 17590, B08 vs B09 ref 53420, mismatches 0`. 무작위 상태(레지스터는 0 / 0..255 / 0..511 / 16비트 섞음, EXL 은 1/8 확률로 켜서 이중 폴트도 섞음)에서 한 스텝씩, 예외면 "레지스터·dmem 불변, retired 는 SYSCALL 일 때만 +1, EPC·pc 규칙", 이중 폴트면 "시스템 레지스터까지 불변", B08 명령이면 B09 기준 모델과 결과 동일을 확인했다. ILLEGAL 이 많은 건 무작위 하위 12비트가 예약 비트를 자주 건드리기 때문이다.

**파이프라인에서 정밀 예외의 값** (ARCH-M01/M02 규칙 위에서 생각):

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">5단 파이프라인: 예외는 발견한 곳이 아니라 commit 지점에서, 프로그램 순서대로</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="120" height="44" rx="4" class="f-mut"/>
    <rect x="160" y="40" width="120" height="44" rx="4" class="f-mut"/>
    <rect x="300" y="40" width="120" height="44" rx="4" class="f-mut"/>
    <rect x="440" y="40" width="120" height="44" rx="4" class="f-acc"/>
    <rect x="580" y="40" width="120" height="44" rx="4" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="80" y="67">IF</text><text x="220" y="67">ID</text><text x="360" y="67">EX</text>
    <text x="500" y="67">MEM</text><text x="640" y="67">WB</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="80" y="104">IFETCH</text>
    <text x="220" y="104">ILLEGAL, ERET 권한</text>
    <text x="220" y="120">SYSCALL 인식</text>
    <text x="360" y="104">DIV0 (rt == 0)</text>
    <text x="360" y="120">ALIGN / DMEM (ea)</text>
    <text x="500" y="104">commit 지점:</text>
    <text x="500" y="120">여기서 예외를 받음</text>
    <text x="640" y="104">레지스터 쓰기</text>
    <text x="640" y="120">(더 오래된 명령은 완료)</text>
  </g>
  <path d="M500 140 L500 160 L80 160 L80 140" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="140" y="178" fill="currentColor" font-size="12">더 젊은 IF · ID · EX 3개 flush + fetch 를 VBAR+원인으로 → 분기 실패보다 1 사이클 더 비싼 리다이렉트</text>
  <text x="10" y="206" fill="currentColor" font-size="12">함정: 젊은 명령의 ILLEGAL(ID 에서 발견)이 늙은 LD 의 DMEM(EX/MEM 에서 발견)보다 "먼저" 보인다.</text>
  <text x="10" y="226" fill="currentColor" font-size="12">→ 예외를 파이프라인 레지스터에 "표시" 만 하고 실어 나르다가, commit 에서 가장 오래된 것 하나만 받는다.</text>
</svg>
```

- 레지스터 쓰기가 WB, 메모리 쓰기가 MEM 에서 **순서대로** 일어나는 in-order 5단에서는, 예외 명령이 MEM 에 도착했을 때 더 젊은 명령은 아직 아무것도 쓰지 않았다 → flush 만 하면 정밀하다. 비용은 진입마다 3~4 사이클 버블 + 핸들러 명령.
- **ST 는 MEM 에서 쓰므로**, ST 보다 늙은 명령의 예외는 그 ST 가 MEM 에 들어가기 전에 확정돼야 한다 — 위 그림처럼 늙은 명령이 항상 앞서 있으니 in-order 에선 자동. 비순차(OoO) 코어는 ROB 로 "완료는 순서대로" 를 강제하고, store 는 retire 뒤에 store buffer 에서 내보낸다.
- **반복형 나눗셈기**(여러 사이클)가 있으면 DIV 가 늦게 끝나는 동안 뒤 명령이 먼저 완료되지 않게 스톨하거나 스코어보드가 필요하다. 다행히 DIV0 는 제수만 보면 되니 **시작할 때** 판정할 수 있다 — 설계자들이 0 검사를 앞에 두는 이유.
- **MTSR EPC 바로 뒤 ERET**: MTSR 이 WB 에서 EPC 를 쓰는데 ERET 이 EX 에서 EPC 를 읽으면 시스템 레지스터 RAW 해저드다. 포워딩을 만들거나, 시스템 레지스터 쓰기를 직렬화(serializing)한다. Arm 은 시스템 레지스터 쓰기 뒤 효과를 보장하려면 ISB 같은 컨텍스트 동기화가 필요하고, ERET 자체가 컨텍스트 동기화 이벤트로 정의된다.

### Part C — 건너뛰기 · 재시도 · 디스패처

실제 로그 (§6 출력에서 발췌):

```text
  C1 skip (EPC+1):
    exc DIV0    n=0   EPC=0x0007 BADADDR=0x0000 -> pc 0x0043  | handler 10 instr
    ...
    avg = 10 65535 22 65535 65535 21845, errcnt 3, retired 86
  C2 fix + retry (EPC):
    exc DIV0    n=0   EPC=0x0007 BADADDR=0x0000 -> pc 0x0043  | handler 8 instr
    ...
    avg = 10 0 22 7 0 21845, errcnt 3, retired 83
  C2' storm: 3000 steps -> DIV0 x995, DIV retired 1, pc 0x0048
```

| | C1 건너뛰기 | C2 재시도 |
|---|---|---|
| 복귀 | EPC + 1 | EPC 그대로 |
| 폴트 난 DIV | retire 안 됨 (op_hist[DIV] = 3) | 고친 뒤 retire (op_hist[DIV] = 6) |
| 결과 | 결과 레지스터에 **이전 값**(센티널) — 정밀성 덕분 | 고친 피연산자로 계산한 값 |
| 명령 수 | 9n+5 − 3 + 3×10 = 86 | 9n+5 + 3×8 = 83 |
| 실제 예 | 미정렬 접근/미구현 명령 에뮬레이션 후 다음으로 (Linux arm32 alignment fixup), "에러 기록하고 계속" 정책 | 페이지 폴트(demand paging, CoW), lazy FPU (Cortex-M NOCP / 첫 FP 명령에서 FPU 켜기), 권한 고치고 재시도 |

C1 에서 r5 = 0xFFFF 가 남는 것은 **정밀 예외가 보장한 것** 이다: DIV 가 rd 를 "반쯤" 쓰지 않았으니 프로그램이 미리 넣어 둔 센티널이 그대로다. C2 의 "피연산자 고치기" 는 핸들러가 **어느 레지스터가 제수인지 알아야** 가능하다 — 이 ISA 엔 그 정보를 주는 필드가 없어 "이 루틴의 제수는 r4" 라는 규약을 썼다. 실제 하드웨어는 이 정보를 **syndrome** 으로 준다: AArch64 데이터 어보트의 ESR ISS 에는 (유효할 때) 전송 레지스터 번호(SRT)와 읽기/쓰기(WnR)가 들어 있어서, 하이퍼바이저가 MMIO 접근을 명령을 다시 디코드하지 않고 에뮬레이션할 수 있다.

**재시도 폭주** (C2'): 원인을 안 고치고 ERET 만 하면 같은 DIV 가 영원히 폴트 — 3000 스텝 동안 995 번 진입, 프로그램은 한 발짝도 안 나간다. 실제 커널에서도 "페이지 폴트 핸들러가 매핑을 못 고쳤는데 재시도" 는 무한 루프가 된다. 방어: 같은 EPC 로 N 번 연속 들어오면 포기(태스크 종료).

**C3 디스패처** — 경로별 명령 수 (로그의 `handler N instr`):

| 번호 | 경로 | 명령 수 |
|---|---|---|
| 0 putc | 공통 11 + BEQ 1 + putc 6 + 복귀 2 | 20 |
| 1 count | 공통 11 + 3 + count 2 + 복귀 2 | 18 |
| 2 exit | 공통 11 + 5 + ST, HALT | 18 (돌아오지 않음) |
| 9 (없음) | 공통 11 + 7 + 복귀 2 | 20 |

공통 11 명령 중 6 개가 "CAUSE ≫ 8" 을 위한 256 만들기 + DIV 다. 시프트가 없는 ISA 의 대가이자, **실제 OS 들이 시스템 콜 번호를 명령의 즉시값이 아니라 레지스터로 넘기는 이유** 다 (Linux: arm64 는 x8, arm EABI 는 r7, RISC-V 는 a7). 즉시값을 쓰면 핸들러가 명령을 읽거나(Cortex-M SVC) syndrome 을 디코드해야(AArch64 SVC #imm16 → ESR ISS) 한다. 사용자 레지스터 보존은 `STP r4, 28(r0)` / `LDP r4, 28(r0)` 페어로 — AArch64 프롤로그의 `stp x29, x30, [sp, #-16]!` 과 같은 쓰임새. exit 는 핸들러 안에서 HALT 하므로 EXL = 1 로 남고 r4/r5 는 복원되지 않는다(테스트는 dmem[28..29] 에 저장된 원래 값을 확인).

### 진짜 M6 에서 같은 일 — `demo_hw_traps.c`

사용자 공간에서는 동기 예외가 커널을 거쳐 **시그널** 로 온다. macOS 는 `ucontext` 에 AArch64 의 ESR(원인)과 FAR(주소)을 넘겨준다 (Apple clang 21.0.0, Apple M6, `cc -std=c11 -O1 demo_hw_traps.c`):

```text
1) divide: AArch64 UDIV/SDIV do not trap
  udiv 1234 / 0       = 0
  sdiv INT32_MIN / -1 = -2147483648
2) synchronous exceptions -> signals (child processes)
  udf #0
    sig=SIGILL  ESR=0x02000000 EC=0x00 (Unknown reason (UDF 등)) ISS=0x000000 FAR=0x0 si_addr=0x102f947e4 *pc=0x00000000
  brk #0x42
    sig=SIGTRAP ESR=0xf2000042 EC=0x3c (BRK (AArch64)) ISS=0x000042 FAR=0x0 si_addr=0x102f947ec *pc=0xd4200840
  ldxr x, [buf+1]  (exclusive, misaligned inside 16 B)
    (no trap)
  ldxr x, [buf+9]  (exclusive, crosses 16 B boundary)
    sig=SIGBUS  ESR=0x92000021 EC=0x24 (Data Abort, lower EL) ISS=0x000021 FAR=0x102f9c009 si_addr=0x102f9c009 *pc=0xc85f7d08
  ldr  x, [0]      (NULL)
    sig=SIGSEGV ESR=0x92000006 EC=0x24 (Data Abort, lower EL) ISS=0x000006 FAR=0x0 si_addr=0x0 *pc=0xf9400108
3) misaligned plain LDR / LDP on normal memory
  ldr [buf+1] = 0x0807060504030201
  ldp [buf+3] = 0x0a09080706050403 0x1211100f0e0d0c0b
4) skip handler (SIGILL: pc += 4)
  skipped 3 UDFs, continued: after=1
5) fix + retry handler (PROT_NONE page -> mprotect -> same STR again)
  page 16384 B, faults fixed 1, p[5] = 0xC0FFEE
```

- **ESR = EC(원인 클래스) + ISS(syndrome)** — 우리 CAUSE = 원인 + iss≪8 과 같은 구조. `brk #0x42` 의 즉시값 0x42 가 ISS 에 그대로 들어간다(SVC #imm16 도 같은 방식). `*pc` 를 읽어 보면 저장된 PC 가 **폴트 난 명령 자체** 다 (`0xd4200840` = brk #0x42, `0x00000000` = udf #0). AArch64 도 all-zero 워드를 영구 undefined(UDF #0)로 두었다 — B09 의 "0 으로 지워진 메모리로 뛰면 바로 트랩" 논의와 같다.
- 데이터 어보트의 ISS 하위 6비트(DFSC): `0x21` = 정렬 폴트, `0x06` = 변환 폴트(레벨 2). FAR = 우리 BADADDR.
- **나눗셈은 트랩하지 않는다**: AArch64 UDIV 0 → 0, SDIV INT_MIN/−1 → INT_MIN. (C 에서는 둘 다 UB 라서 `a / b` 로는 이 관찰을 하면 안 된다 — 그래서 asm 으로 했다.)
- 미정렬 LDXR 이 16 B 블록 **안** 에서는 트랩하지 않았고 경계를 넘을 때만 SIGBUS 가 났다. 이 맥은 `sysctl hw.optional.arm.FEAT_LSE2` = 1 이고, LSE2 가 "16 B 안의 미정렬" 을 허용하는 것과 맞아 보이는 관찰이다 — 교과서식 "exclusive 는 항상 정렬 폴트" 와 다르니 정확한 규칙은 Arm ARM 으로 확인할 것.
- 4)·5) 는 Part C 의 C1/C2 를 진짜 커널 위에서 한 것: SIGILL 핸들러가 `pc += 4`(= EPC + 1 명령) → 계속, SIGSEGV/SIGBUS 핸들러가 `mprotect` 로 원인을 고치고 pc 를 그대로 두면 **같은 STR 이 다시 실행** 되어 성공. 페이지 폴트 재시도와 같은 모양이다.

### 컴파일러 쪽: 하드웨어 나눗셈이 없으면 — `demo_div_asm.c`

```text
=== thumbv7em / cortex-m4 ===          === thumbv6m / cortex-m0 ===        === arm64 (M6) ===
div_checked:                           div_checked:                        _div_checked:
	cmp	r1, #0                         	push	{r7, lr}                    	cbz	w1, LBB1_2
	ite	ne                             	cmp	r1, #0                      	udiv	w0, w0, w1
	udivne	r0, r0, r1                 	beq	.LBB1_2                     	ret
	moveq.w	r0, #-1                    	bl	__aeabi_uidiv               LBB1_2:
	bx	lr                             	pop	{r7, pc}                    	mov	w0, #-1
                                       .LBB1_2:                            	ret
                                       	movs	r0, #0
                                       	mvns	r0, r0
                                       	pop	{r7, pc}
```

(`div_u32` 는 M4 `udiv r0, r0, r1`, M0 `bl __aeabi_uidiv`, arm64 `udiv w0, w0, w1` 한 줄씩 — 지시어를 걸러낸 실제 출력을 세 열로 배치.) Cortex-M4 의 UDIV 도 기본은 0 으로 나누면 결과 0 이고, `CCR.DIV_0_TRP` 를 켜야 UsageFault(DIVBYZERO)가 난다. Cortex-M0 에는 나눗셈기가 없어 런타임 라이브러리를 부른다. 펌웨어의 실용 결론: **0 검사는 소프트웨어가 한다**(div_checked), 트랩은 디버그용 안전망.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 진단 없이 39/39
cc -std=c11 -O1 demo_hw_traps.c -o demo_hw_traps && ./demo_hw_traps                                 # arm64 전용
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_div_asm.c -o -
clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_div_asm.c -o -
```

```text
Part A — 확장 디코드 · DIV/LDP/STP · SYSCALL/ERET
  65536 words: B08-legal 20993 (unchanged), newly legal 4945 (DIV 512 LDP 2048 STP 2048 SYSCALL 256 ERET 1 MFSR 40 MTSR 40), illegal 39598
  prog_a: r4=142 CAUSE=0x0506 EPC=7 retired=14 (user 10 + handler 4)
Part B — 정밀 예외 · 우선순위 · VBAR 미설정 · 이중 폴트 · 퍼즈
  (원인 14가지 표 — §5 Part B 참고)
  fuzz 200000: ILLEGAL 102270 IFETCH 5337 DIV0 305 ALIGN 5369 DMEM 9744 SYSCALL 667 double 17590, B08 vs B09 ref 53420, mismatches 0
Part C — 건너뛰기 · 재시도 · SYSCALL 디스패처
  (C1/C2 로그 — §5 Part C 참고)
  C3 syscall dispatcher:
    exc SYSCALL n=0   EPC=0x0002 BADADDR=0x0000 -> pc 0x0046  | handler 20 instr
    exc SYSCALL n=0   EPC=0x0004 BADADDR=0x0000 -> pc 0x0046  | handler 20 instr
    exc SYSCALL n=9   EPC=0x0006 BADADDR=0x0000 -> pc 0x0046  | handler 20 instr
    exc SYSCALL n=1   EPC=0x0008 BADADDR=0x0000 -> pc 0x0046  | handler 18 instr
    exc SYSCALL n=2   EPC=0x000A BADADDR=0x0000 -> pc 0x0046  | handler 18 instr (HALT)
    out="HI" r6=0xFFFF r1=4 exit=3 calls=5 retired=106
39/39 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| SYSCALL 의 EPC = pc | ERET 이 SYSCALL 로 돌아가 **같은 시스템 콜을 무한 반복** | 완료(retire, pc+1) 뒤 진입 → EPC = next_pc |
| 폴트의 EPC = pc + 1 | 재시도 불가, 그리고 핸들러가 아무것도 안 해도 폴트 명령이 조용히 사라짐 | 폴트는 `take_exception(..., pc, ...)` |
| 폴트 검사 전에 `retired++` / `reg[rd] = …` | 폴트 명령이 반쯤 실행됨 → C1 의 센티널이 깨지고 재시도하면 두 번 실행 | 검사 → return 을 commit 앞에 |
| LDP 에서 rd 를 먼저 쓰고 ea+1 검사 | 범위 폴트 때 rd 만 바뀜 (찢어진 페어) | 정렬·범위를 다 보고 두 워드를 같이 commit |
| 범위 검사를 정렬보다 먼저 | 0x101 이 DMEM 으로 보고됨 — 스펙(ALIGN 우선)과 다르면 ISS↔RTL 비교가 깨진다 | 우선순위를 정하고 코드 순서로 표현 |
| take_exception 에서 EPC 를 쓰고 나서 EXL 검사 | 이중 폴트 때 첫 원인을 잃음 → HardFault 분석 불가 | EXL 검사를 맨 앞에 |
| 핸들러가 레지스터를 저장하지 않음 | 사용자 r4/r5 가 예외 한 번에 바뀐다 (시스템 콜 규약 위반) | 진입 직후 `STP r4, 28(r0)`, 복귀 직전 `LDP` |
| 중첩 허용 핸들러가 EPC 를 저장하지 않음 | 안쪽 예외가 EPC 를 덮어 ERET 이 핸들러 안으로 돌아감 (추가 연습 2, STEP_LIMIT) | EPC 저장 → EXL 해제 … EXL 설정 → EPC 복원 |
| ERET 을 thread 모드에서 허용 | 사용자 코드가 MTSR EPC + ERET 으로 아무 데나 점프하는 "권한 상승" 통로 | EXL = 0 이면 ILLEGAL |
| 벡터 슬롯의 JAL 오프셋을 `handler − slot` 으로 | PC+1 기준이라 한 칸 어긋나 핸들러 첫 명령(레지스터 저장)을 건너뜀 | `handler − (slot + 1)` (`vec_jump`) |

## 8. 면접 Q&A

<details>
<summary>Q1. 폴트, 트랩, 인터럽트의 복귀 주소는 각각 어디인가요?</summary>

폴트(정밀): **폴트 난 명령** — 원인을 고치고 재실행(페이지 폴트)하거나 핸들러가 EPC 를 넘겨 건너뛴다. 트랩: 의도된 명령이 완료된 뒤라 **다음 명령** (Arm 의 SVC 는 ELR/스택된 PC 가 다음 명령). 인터럽트(비동기): **아직 실행 안 된 첫 명령** — 경계에서 받으므로 그 앞은 모두 완료.
**꼬리질문: RISC-V ECALL 은?** RISC-V 는 ECALL/EBREAK 를 "완료되지 않는(retire 하지 않는)" 동기 예외로 정의해 **mepc = ECALL 자신의 주소** 다. 그래서 핸들러가 `mepc += 4` 를 해야 한다 — 빼먹으면 §7 첫 줄의 실수(무한 시스템 콜). 어느 쪽이든 "누가 +1 을 하느냐" 를 ISA 가 정해야 한다.

</details>

<details>
<summary>Q2. 정밀 예외란? 파이프라인에서 어떻게 보장하고 무엇이 드나요?</summary>

예외 보고 시점에 "앞의 명령은 모두 완료, 예외 명령과 뒤는 하나도 반영 안 됨" 이 성립하는 것. 5단 in-order: 예외를 발견한 단계에서 표시만 하고 commit 지점(MEM→WB)에서 프로그램 순서대로 하나만 받으며, 젊은 명령 3개를 flush 하고 fetch 를 벡터로 돌린다(진입마다 수 사이클). 젊은 명령의 ILLEGAL 이 ID 에서 늙은 명령의 DMEM 보다 먼저 보이는 경우가 함정. OoO 는 ROB 로 순서대로 retire, store 는 retire 뒤 버퍼에서 내보낸다.
**꼬리질문: 정밀하지 않은 예외의 실제 예는?** Cortex-M3/M4 의 imprecise BusFault: 쓰기 버퍼로 넘긴 store 가 나중에 버스 에러를 받으면 스택된 PC 는 원인 명령이 아니고 BFAR 도 유효하지 않다. 디버깅 땐 쓰기 버퍼를 끄는 설정(ACTLR 의 DISDEFWBUF 류 — 해당 코어 TRM 확인)으로 precise 로 바꿔 원인 명령을 찾는다.

</details>

<details>
<summary>Q3. 벡터 테이블을 "원인별 슬롯" 과 "진입점 하나" 중 무엇으로? Cortex-M 은 왜 주소 테이블인가요?</summary>

원인별: 디스패치 비용 0, 지연이 예측 가능(인터럽트에 중요). 진입점 하나: 테이블이 작고 원인을 마음대로 늘릴 수 있지만 매번 비교·분기(이 레슨 연습 1: SYSCALL 6 → 11 명령). Cortex-M 은 **주소** 테이블(VTOR + 4×예외 번호: 1 Reset, 2 NMI, 3 HardFault, 4 MemManage, 5 BusFault, 6 UsageFault, 11 SVCall, 14 PendSV, 15 SysTick, 16+ IRQ)이라 하드웨어가 주소를 읽어 점프하고, 진입 시 r0–r3, r12, LR, PC, xPSR 을 스택에 쌓으므로 핸들러가 **그냥 C 함수** 가 된다(어셈블리 래퍼 불필요).
**꼬리질문: VTOR 은 왜 바꾸나?** 부트로더 → 애플리케이션 점프 시 앱의 테이블로 옮기거나, 테이블을 RAM 에 복사해 런타임에 핸들러를 바꾸려고. 테이블 엔트리는 Thumb 주소라 LSB = 1 이어야 한다(아니면 INVSTATE 폴트). 정렬 요구가 있으니 링커 스크립트에서 맞출 것.

</details>

<details>
<summary>Q4. 0 으로 나누면 CPU 는 어떻게 하나요? (x86, Arm, RISC-V, Cortex-M)</summary>

x86: DIV/IDIV 가 #DE 예외(리눅스에선 SIGFPE). AArch64: **트랩 없음**, 결과 0 (이 맥에서 `udiv 1234/0 = 0` 실측). Cortex-M3/M4: 기본 결과 0, `CCR.DIV_0_TRP` 를 켜면 UsageFault(DIVBYZERO). RISC-V: 트랩 없음, 몫 = 모든 비트 1, 나머지 = 피제수. Cortex-M0: 나눗셈 명령이 없어 `__aeabi_uidiv` 라이브러리 호출(0 나누기 처리는 런타임 구현에 따름).
**꼬리질문: INT_MIN / −1 은?** x86 은 이것도 #DE. AArch64 는 INT_MIN 을 돌려준다(실측). C 에서는 두 경우 모두 UB 라 컴파일러가 가정을 세울 수 있다 — 펌웨어에서는 0 과 INT_MIN/−1 을 **명시적으로 검사** 하는 것이 정답(demo_div_asm.c 의 div_checked 는 M4 에서 분기 없이 `cmp` + `ite ne` 조건 실행으로 끝남).

</details>

<details>
<summary>Q5. LDP 에 짝수 주소를 요구한 이유는? 실제 코어의 미정렬 규칙은?</summary>

워드 주소 머신에서 의미 있는 정렬 규칙 + 페어 접근이 경계(메모리 끝, 줄, 페이지)를 걸치지 않아 **검사 한 번으로 전부 아니면 없음** 이 된다(연습 3: STP 는 255 에서 ALIGN 로 아무것도 안 씀, ST 두 번은 255 를 쓰고 256 에서 폴트 → 찢어진 쓰기). 실제: Cortex-M3/M4 는 LDR/STR(하프워드/워드) 미정렬을 허용(느림, UNALIGN_TRP 로 트랩 가능)하지만 LDRD/STRD/LDM/STM 미정렬은 항상 UsageFault, Cortex-M0 은 모든 미정렬이 HardFault. AArch64 일반 메모리는 LDR/LDP 미정렬 허용(이 맥 실측), Device 메모리 미정렬은 정렬 폴트, exclusive 는 정렬 요구(이 맥은 16 B 안은 허용 — FEAT_LSE2 관련으로 보임).
**꼬리질문: 미정렬이 허용되는데 왜 신경 쓰나?** 원자성(두 번의 버스 접근 사이에 다른 마스터가 끼어듦), 성능(줄/페이지 경계를 넘으면 두 번 접근), 그리고 DMA·주변장치 레지스터는 대개 정렬 접근만 받는다.

</details>

<details>
<summary>Q6. 핸들러 안에서 또 예외가 나면? 중첩을 허용하려면?</summary>

하드웨어가 EPC 를 하나만 가지면 안쪽 예외가 바깥 EPC 를 덮는다. 정책은 둘: (1) 이 모델처럼 EXL = 1 이면 **이중 폴트로 정지**(Cortex-M lockup, x86 #DF→triple fault 와 같은 계열), (2) 핸들러가 EPC/CAUSE 를 메모리(스택)에 저장한 뒤 EXL 을 내려 중첩을 허용하고, 끝나면 **EXL 을 먼저 올리고** EPC 를 복원해 ERET(연습 2). 순서를 거꾸로 하면 EPC 복원과 EXL 설정 사이에 예외가 나서 복원한 EPC 가 또 덮인다.
**꼬리질문: Cortex-M 은 왜 이 문제가 덜한가?** 진입 때 하드웨어가 복귀 주소를 **스택** 에 쌓고 LR 에 EXC_RETURN 을 넣으므로 중첩마다 프레임이 따로 생긴다 (스택 프레임 상세는 ARM-M09, 우선순위로 선점하는 규칙은 ARCH-M09 NVIC 모델).

</details>

<details>
<summary>Q7. 시스템 콜 번호는 어디로 넘기나요? 왜 리눅스는 SVC 즉시값을 안 쓰나요?</summary>

세 방법: 명령 즉시값을 syndrome 으로(AArch64 SVC #imm16 → ESR.ISS, 이 레슨의 CAUSE≫8), 명령을 다시 읽어서(Cortex-M: 스택된 PC−2 의 하프워드 하위 8비트; 어느 스택인지 EXC_RETURN 으로 판단), 레지스터로(Linux: arm64 x8, arm EABI r7, RISC-V a7). 레지스터 방식이 가장 빠르고 단순하다 — 명령 메모리를 데이터로 읽을 필요가 없고(Harvard/XOM 코드에서도 됨) 번호 범위 제한도 없다. 이 레슨에서 "CAUSE≫8" 에만 6 명령(256 만들기 + DIV)이 든 것이 그 비용의 축소판.
**꼬리질문: FreeRTOS 의 Cortex-M 포트는 SVC 를 뭐에 쓰나?** 첫 태스크 시작 등 특권 작업 진입에 쓰고, 문맥 교환 자체는 PendSV 로 미룬다(우선순위 최저라 다른 ISR 을 방해하지 않음) — 포트마다 다르니 해당 port.c 확인.

</details>

<details>
<summary>Q8. "고치고 재시도" 와 "건너뛰기" 의 실제 사례를 하나씩?</summary>

재시도: 페이지 폴트(매핑 후 같은 load 재실행), CoW, lazy FPU(첫 FP 명령에서 FPU 를 켜고 재실행; Cortex-M 은 CPACR 로 FPU 가 꺼져 있으면 NOCP UsageFault), 그리고 이 맥의 demo 5) `mprotect` 후 같은 STR. 건너뛰기: 미구현 명령 에뮬레이션(계산해 넣고 EPC+1), Linux 의 user 접근 실패 처리 — 폴트 PC 를 **예외 테이블**(fault_pc → fixup_pc)에서 찾아 EPC 를 fixup 코드로 바꿔 -EFAULT 를 돌려준다(연습 4).
**꼬리질문: 재시도의 위험은?** 원인을 못 고치면 무한 재진입(C2' 에서 3000 스텝에 995 회). 같은 EPC 연속 횟수 제한, 또는 "고칠 수 없는 원인" 분류(권한 위반은 재시도가 아니라 SIGSEGV)가 필요하다.

</details>

## 9. 추가 연습

### 연습 1 — 원인별 벡터 vs 단일 진입점 + CAUSE 디스패치

`vec_direct` 스위치(켜면 모든 예외가 VBAR 로)를 추가하고, DIV0 건너뛰기와 SYSCALL 0 카운터를 한 진입점에서 처리하는 디스패처를 써라. 같은 사용자 프로그램(DIV0 2번, SYSCALL 2번)의 retired 를 비교하라.

<details>
<summary>답 보기</summary>

`take_exception` 한 줄: `pc = VBAR + (vec_direct ? 0 : cause)`. 디스패처는 `STP` 저장 → `MFSR r4, CAUSE` → `ADDI −3; BEQ div0` → `ADDI −3; BEQ sys` → 모르면 HALT. 결과: 원인별 37 retired(DIV0 10 = 슬롯 JAL + 9, SYSCALL 6 = 슬롯에서 바로), direct 51 retired(DIV0 12, SYSCALL 11). 원인이 늘수록 비교 사슬이 길어지고, 번호가 있는 SYSCALL 은 CAUSE 하위 8비트만 비교해야 하는데(마스크/나머지 연산 필요) 이 ISA 엔 AND 가 없다 — 실제 ISA 가 원인 코드를 **별도 필드**(ESR.EC, mcause)로 두는 이유. 진입 지연을 줄이고 싶은 인터럽트는 원인별(RISC-V 도 vectored 모드는 인터럽트에만 적용), 동기 예외는 진입점 하나가 흔한 절충이다.

</details>

### 연습 2 — 중첩 예외를 허용하는 SYSCALL 핸들러

SYSCALL 0 = `r1 ← r2 / r3` (제수 0 이면 0xFFFF). 핸들러 안의 DIV 가 DIV0 를 낼 수 있게 EXL 을 내리고 실행하라. EPC 를 저장/복원하지 않는 버전이 어떻게 망가지는지 보여라.

<details>
<summary>답 보기</summary>

올바른 순서: `MFSR r4, EPC; ST r4, 31(r0)` → `MTSR STATUS, r0`(중첩 허용) → `ADDI r1, r0, −1; DIV r1, r2, r3` → `ADDI r4, r0, 1; MTSR STATUS, r4`(**EXL 먼저**) → `LD r4, 31(r0); MTSR EPC, r4` → `ERET`. r3 = 0 이면 안쪽 DIV0 를 건너뛰기 핸들러가 처리하고 결과 0xFFFF, 바깥으로 정상 복귀(pc 4). 버그 버전은 r3 ≠ 0 이면 멀쩡하다가(테스트 통과!) r3 = 0 에서 안쪽 예외가 EPC 를 0x49(핸들러 안)로 덮어 ERET 이 핸들러로 돌아가고, 거기서 EXL 을 올리고 또 ERET → 같은 자리 → STEP_LIMIT. "드물게 나는 중첩에서만 터지는 버그" 의 전형이라 리뷰로 잡아야 한다.

</details>

### 연습 3 — STP(전부 아니면 없음) vs ST 두 번

dmem[255] 에 두 워드를 쓰려는 STP 와 ST 두 번을 비교하라. 254 에서는?

<details>
<summary>답 보기</summary>

STP @255 → ALIGN, dmem[255] 은 0xEEEE 그대로. `ST; ST` @255 → 첫 ST 가 dmem[255] = 1 을 쓰고 두 번째가 256 에서 DMEM — **찢어진 쓰기**(정밀 예외는 "명령 하나" 단위라 앞 ST 는 이미 완료). STP @254 → 정상(254 = 1, 255 = 2). 64비트 카운터나 (주소, 길이) 디스크립터를 32비트 버스로 두 번에 나눠 쓰면 DMA/ISR 가 반쪽 값을 볼 수 있는 것과 같은 문제다 (ARMv7 에서 LDRD/STRD 의 원자성 조건은 아키텍처 확장에 따라 다르니 Arm ARM 확인; Cortex-M 은 ISR/DMA 와 공유하는 64비트 값에 임계 구역이나 시퀀스 카운터를 쓴다).

</details>

### 연습 4 — 예외 테이블 fixup (Linux `__ex_table` 방식)

DMEM 폴트 핸들러가 dmem[16..] 의 `(fault_pc, fixup_pc)` 쌍 표를 **LDP** 로 훑어, 일치하면 EPC ← fixup, r1 ← 0xFFFF 로 복귀하고, 없으면 oops(HALT)하게 하라. 좋은 포인터 / 나쁜 포인터 / 표에 없는 폴트를 시험하라.

<details>
<summary>답 보기</summary>

`MFSR r3, EPC; ADDI r6, r0, 16` → loop: `LDP r4, 0(r6)` (r4 = fault_pc, r5 = fixup) → `BEQ r4, r3, found` → 끝 표시 0xFFFF 는 `ADDI r4, r4, 1` 후 0 비교 → `ADDI r6, r6, 2` 반복. 결과: good = 0xCAFE, bad = 0xFFFF(-EFAULT 같은 값), 표에 없는 pc 7 의 폴트 → pc 0x50 에서 oops, EPC 7, BADADDR 0xFFFF. 표가 짝수 주소의 쌍이라 LDP 정렬 규칙이 자연스럽게 맞는다. 리눅스의 실제 표는 정렬되어 이진 검색되고, 엔트리 형식(상대 오프셋, 타입 필드 등)은 아키텍처·버전마다 다르다. 핵심은 "폴트가 날 수 있는 **알려진 PC** 만 복구하고, 그 밖은 커널 버그(oops)" 라는 정책.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
```

```text
  ex1: vectored 37 retired (DIV0 10, SYSCALL 6 each), direct 51 retired (DIV0 12, SYSCALL 11 each)
  ex2: nested ok -> dmem[2]=0xFFFF; buggy handler: STEP_LIMIT, EPC stuck at 0x0049 (inside handler)
  ex3: STP @255 -> ALIGN, dmem[255] untouched; ST,ST @255 -> DMEM @256 after dmem[255]=1 (torn)
  ex4: get_user good=0xCAFE bad=0xFFFF, unlisted fault -> oops at pc 0x0050 (EPC 7)
15/15 passed
```

## 10. 핵심 정리

- **폴트 EPC = 그 명령**(흔적 0, retire 안 함 → 고쳐서 재실행 또는 EPC+1 로 건너뛰기), **트랩 EPC = 다음 명령**(SYSCALL 은 완료 후 진입). RISC-V ECALL 은 예외적으로 자기 주소 → 핸들러가 +4.
- 진입 = EXL 검사(이중 폴트) → EPC/CAUSE(원인 + syndrome)/BADADDR 기록 → EXL ← 1 → pc ← VBAR + 원인. 복귀 = ERET(pc ← EPC, EXL ← 0), thread 에서 ERET 은 ILLEGAL.
- 원인 우선순위를 정하고 코드 순서로 지킨다: fetch > decode > ALIGN > DMEM. 페어 접근은 짝수 정렬 → 경계를 안 걸쳐 전부 아니면 없음.
- 상위집합 = "합법은 뜻 그대로" 만 보장 (B08 합법 20993 워드 동일, 0x6000 은 illegal → DIV). 예약 인코딩은 반드시 트랩해야 미래 확장·에뮬레이션이 된다.
- 벡터: 원인별(지연 0) vs 진입점 하나 + 디스패치(유연). Cortex-M = 주소 테이블 + 하드웨어 스태킹, AArch64 = VBAR 16 진입점 + ESR.EC, RISC-V = mtvec direct/vectored.
- 0 나누기: x86 트랩, AArch64/RISC-V 트랩 없음, Cortex-M3/M4 는 DIV_0_TRP 로 선택, M0 는 라이브러리 → 펌웨어는 소프트웨어로 검사.
- 파이프라인 정밀 예외 = 발견 시 표시, commit 에서 순서대로 받기, 젊은 명령 flush (진입마다 수 사이클). 시스템 레지스터 쓰기 → ERET 은 해저드/직렬화 대상.
- M6 실측: ESR(EC+ISS)·FAR 가 시그널로 보이고, `pc += 4` 건너뛰기와 `mprotect` 재시도가 진짜 커널 위에서 그대로 동작한다.

## 참고자료

- [Arm ARMv7-M Architecture Reference Manual (DDI 0403)](https://developer.arm.com/documentation/ddi0403/latest) — 예외 모델, 벡터 테이블/VTOR, UsageFault(UNDEFINSTR, DIVBYZERO, UNALIGNED), lockup
- [Cortex-M4 Devices Generic User Guide (DUI 0553)](https://developer.arm.com/documentation/dui0553/latest) — Fault handling, CCR(DIV_0_TRP, UNALIGN_TRP), CFSR/BFAR, 예외 번호
- [Arm Architecture Reference Manual for A-profile (DDI 0487)](https://developer.arm.com/documentation/ddi0487/latest) — ESR_ELx(EC/ISS, DFSC), FAR_ELx, ELR_ELx, VBAR_ELx 벡터 오프셋, 정렬 규칙, FEAT_LSE2
- [RISC-V Privileged Architecture](https://github.com/riscv/riscv-isa-manual) — mepc/mcause/mtval/mtvec(direct/vectored), ECALL 의 mepc 규칙; unprivileged 스펙의 M 확장 0 나누기 결과
- Patterson & Hennessy, *Computer Organization and Design*, Ch. 4 "Exceptions" 절 / Hennessy & Patterson, *Computer Architecture: A Quantitative Approach*, Appendix C — 파이프라인에서의 정밀 예외
- [Linux kernel — Exception tables (Documentation/arch/x86/exception-tables.rst)](https://docs.kernel.org/arch/x86/exception-tables.html) — fault_pc → fixup 방식
- ARCH-B08 (ISA·인코딩 예약), ARCH-B09 (단일 사이클 CPU, 연습 4 트랩 에뮬레이션), ARCH-B11 (버스 폴트), ARCH-M01/M02 (파이프라인), ARCH-M09 (NVIC 선점·tail-chaining), 다음 단계: ARM-M09 (EXC_RETURN·스택 프레임), BM-A10 (IRQ 지연 측정)
