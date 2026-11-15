---
id: ARCH-M01
time: 90분
updated: 2026-11-11
---
# ARCH-M01 5단 파이프라인 시뮬레이터: 데이터 해저드와 스톨

## 학습 목표

- ARCH-B09 단일 사이클 CPU 를 **IF / ID / EX / MEM / WB 5단 파이프라인** 으로 바꾸고, 각 단계 사이의 **파이프라인 레지스터(IF/ID, ID/EX, EX/MEM, MEM/WB)** 가 무엇을 들고 가는지 코드로 말할 수 있다.
- **RAW 해저드(read-after-write)** 를 ID 단계에서 탐지하고, 포워딩 없이 **스톨(stall) + 버블(bubble)** 로 막는다. 거리 1/2/≥3 → 스톨 2/1/0 이 **split-phase 레지스터 파일** 에서 나온다는 것을 설명한다.
- r0, BEQ 의 두 소스, ST 의 데이터 레지스터, JAL 의 링크 레지스터 같은 **면접관이 찌르는 경우** 를 하나씩 테스트로 못 박는다.
- 사이클별 **파이프라인 다이어그램** 과 `cycles / retired / CPI / stalls / flushes` 요약을 출력하고, 최종 상태가 단일 사이클 모델과 같음을 **랜덤 프로그램 3000개 fuzz + 독립적인 해석 공식** 으로 검증한다.
- 배열 합 · memcpy 루프의 CPI 를 손으로 세고(10N+12 …), **명령 재배치(scheduling)** 로 스톨을 없애며, 단일 사이클(CPI 1, 긴 클럭)과 파이프라인(CPI ≈ 1.3–1.7, 짧은 클럭)의 실행 시간을 ARCH-B13 Fmax 와 엮어 비교한다.

## 1. 문제

"이 코드는 5단 파이프라인에서 몇 사이클? CPI 가 왜 1 이 아니죠?" — 펌웨어·실리콘 검증·CPU 설계 면접의 기본 질문이고, 실제로는 Cortex-M 의 분기 페널티, DSP 루프의 명령 순서, RTL 파이프라인의 사이클 정확 모델(cycle-accurate model)을 이해하는 바탕이다.

이미 아는 것(다시 가르치지 않는다): ISA 와 인코딩(ARCH-B08), 단일 사이클 인터프리터 · 정밀 폴트 · 배열 합 6N+6(ARCH-B09), 단계 지연 200/100/200/200/100 ps 와 단일 사이클의 낭비(ARCH-B09 Part C), setup slack · Fmax 내림 규칙(ARCH-B13), 클럭 에지에서 "다음 상태를 다 계산한 뒤 한꺼번에 반영" 하는 2단계 갱신(ARCH-B06). 이 레슨은 **포워딩 없는 기본 파이프라인** 만 만든다. 포워딩 · load-use · 2비트 분기 예측은 ARCH-M02, 파이프라인 안의 정밀 예외는 ARCH-M10, 더 큰 사이클 근사 모델은 SIM-M01 이 다음 단계다.

ISA 는 **ARCH-B08/B09 ISA를 그대로 넣었다** (16비트 명령, r0 = 0 고정, `imem[256]` / `dmem[256]` 워드 주소, mod 2^16, `0x0000` = `ADD r0,r0,r0` = NOP). 파이프라인 규칙은 ARCH-M02 와 공유하므로 정확히 이렇다:

| 항목 | 규칙 |
|---|---|
| 단계 | IF(imem) → ID(디코드 + 레지스터 읽기) → EX(ALU, **BEQ 비교, BEQ/JAL 목표**) → MEM(dmem) → WB(레지스터 쓰기, JAL 의 rd = PC+1 도 여기) |
| 레지스터 파일 | **split-phase**: WB 가 사이클 전반부에 쓰고 ID 가 후반부에 읽는다 → WB 에 있는 값은 같은 사이클 ID 에서 보인다 |
| 데이터 해저드 | 포워딩 없음. ID 의 소스(r0 제외)를 EX 또는 MEM 의 명령이 쓰려 하면 스톨: PC·IF/ID 유지, ID/EX 에 버블 |
| 제어 해저드 | 정적 not-taken. taken BEQ 와 **모든** JAL 은 EX 에서 IF·ID 두 명령을 flush → 2 사이클 손해 |
| HALT | EX 에 오면(앞선 분기가 모두 해결된 시점) IF·ID flush + fetch 중단. **HALT 가 WB 에 있는 사이클 번호 = 총 사이클** |
| 셈 | 사이클 1 = PC 0 첫 fetch. retired 는 HALT 포함, flush 된 명령 제외. 해저드 없으면 N 명령 = **N + 4** 사이클. CPI = cycles / retired |
| 투기적 fetch | PC ≥ 256 은 NOP 로 읽는다. 테스트 프로그램은 폴트가 없다(파이프라인 폴트는 ARCH-M10) |

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m01-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">5단 파이프라인 — 단계(회색) 사이의 세로 막대가 파이프라인 레지스터</text>
  <rect x="10" y="135" width="40" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="30" y="160" text-anchor="middle" fill="currentColor" font-weight="bold">PC</text>
  <g stroke="currentColor" class="f-mut">
    <rect x="62" y="115" width="80" height="80" rx="4"/>
    <rect x="180" y="115" width="100" height="80" rx="4"/>
    <rect x="318" y="115" width="100" height="80" rx="4"/>
    <rect x="456" y="115" width="80" height="80" rx="4"/>
    <rect x="574" y="115" width="70" height="80" rx="4"/>
  </g>
  <g stroke="currentColor" class="f-acc">
    <rect x="150" y="100" width="20" height="110"/>
    <rect x="288" y="100" width="20" height="110"/>
    <rect x="426" y="100" width="20" height="110"/>
    <rect x="544" y="100" width="20" height="110"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="102" y="145">IF</text><text x="102" y="165" font-size="12">imem</text>
    <text x="230" y="140">ID</text><text x="230" y="158" font-size="12">decode</text><text x="230" y="176" font-size="12">regfile 읽기</text>
    <text x="368" y="140">EX</text><text x="368" y="158" font-size="12">ALU, ==</text><text x="368" y="176" font-size="12">분기 목표</text>
    <text x="496" y="145">MEM</text><text x="496" y="165" font-size="12">dmem</text>
    <text x="609" y="145">WB</text><text x="609" y="165" font-size="12">regfile 쓰기</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="156" y="228" text-anchor="end">IF/ID</text><text x="298" y="228">ID/EX</text><text x="436" y="228">EX/MEM</text><text x="554" y="228">MEM/WB</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M50 155 L60 155" marker-end="url(#m01-arr)"/>
    <path d="M142 155 L148 155" marker-end="url(#m01-arr)"/>
    <path d="M170 155 L178 155" marker-end="url(#m01-arr)"/>
    <path d="M280 155 L286 155" marker-end="url(#m01-arr)"/>
    <path d="M308 155 L316 155" marker-end="url(#m01-arr)"/>
    <path d="M418 155 L424 155" marker-end="url(#m01-arr)"/>
    <path d="M446 155 L454 155" marker-end="url(#m01-arr)"/>
    <path d="M536 155 L542 155" marker-end="url(#m01-arr)"/>
    <path d="M564 155 L572 155" marker-end="url(#m01-arr)"/>
  </g>
  <rect x="200" y="26" width="220" height="62" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="310" y="44" text-anchor="middle" fill="currentColor">hazard unit (ID 에서 판단)</text>
  <text x="310" y="61" text-anchor="middle" fill="currentColor" font-size="12">ID 소스 == EX·MEM 의 rd ?</text>
  <text x="310" y="78" text-anchor="middle" fill="currentColor" font-size="12">stall: PC·IF/ID 유지, ID/EX ← 버블</text>
  <g stroke="currentColor" fill="none" stroke-dasharray="4 3">
    <path d="M436 100 L436 56 L422 56" marker-end="url(#m01-arr)"/>
    <path d="M298 100 L298 90" marker-end="url(#m01-arr)"/>
    <path d="M200 56 L30 56 L30 133" marker-end="url(#m01-arr)"/>
    <path d="M160 56 L160 98" marker-end="url(#m01-arr)"/>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M368 195 L368 252 L160 252 L160 212" marker-end="url(#m01-arr)"/>
    <path d="M160 252 L30 252 L30 177" marker-end="url(#m01-arr)"/>
    <path d="M609 195 L609 292 L230 292 L230 197" marker-end="url(#m01-arr)"/>
  </g>
  <text x="380" y="248" fill="currentColor" font-size="12">taken BEQ / JAL: IF·ID flush, PC ← 목표 (2 사이클)</text>
  <text x="250" y="286" fill="currentColor" font-size="12">WB → regfile: 전반부 쓰기 · ID 는 후반부 읽기 (split-phase)</text>
  <text x="10" y="322" fill="currentColor" font-size="12">시뮬레이터의 st[S_ID] / st[S_EX] / st[S_MEM] / st[S_WB] = 클럭 에지 직후 IF/ID · ID/EX · EX/MEM · MEM/WB 레지스터 내용</text>
</svg>
```

체크 테스트는 **결정적인 것만** 본다: 고정 프로그램의 사이클·스톨·flush 수, 다이어그램 문자열 전체 비교, 그리고 고정 시드 xorshift 로 만든 랜덤 프로그램에서 "최종 상태 == 단일 사이클 모델" 과 "사이클 == 해석 공식".

### Part A — 파이프라인 레지스터와 해저드 없는 흐름

- 시뮬레이터 뼈대 `pipe_cycle()` (IF fetch → WB → MEM → EX → ID 순서로 처리)와 다이어그램 출력은 주어진다. 사이클 끝의 클럭 에지 `pipe_advance(p, stall, redirect, target, halt_ex)` 를 구현한다: **리다이렉트/HALT > 스톨 > 정상** 우선순위로 `st[]` 를 옮긴다.
- 확인: 3 사이클 뒤 IF/ID = pc2, ID/EX = pc1, EX/MEM = pc0 (EX/MEM 의 ALU 결과 = 1). NOP N−1 개 + HALT 가 N = 1..60 모두 **N + 4** 사이클. HALT 뒤 투기적 NOP 2개와 taken BEQ 뒤 2개가 `xx` 로 끝나는 다이어그램이 기대 문자열과 글자 하나까지 같다. not-taken BEQ 는 공짜, `JAL r0, +0` 도 2 사이클. PC 255 의 HALT 뒤 PC 256, 257 fetch 는 NOP.

### Part B — RAW 탐지와 스톨 (면접관이 찌르는 곳)

- `insn_dst(d)` — WB 에서 쓰는 레지스터(r0 이거나 안 쓰면 0). `insn_srcs(d, s)` — ID 에서 읽는 레지스터 두 개(없으면 0).
- `hazard_stall(p)` — ID 의 소스가 EX/MEM 의 목적지와 같으면 true.
- `analytic_cycles(trace, n)` — 단일 사이클 모델이 남긴 retire 트레이스만으로 사이클 수를 계산하는 **해석 공식** (시뮬레이터와 독립인 두 번째 정답지).
- 확인: 거리 1/2/3/4 → 스톨 2/1/0/0, r0 무해저드, 두 소스는 **최댓값**(합 아님), BEQ 소스, ST 데이터·주소, LD 생산자, JAL → 목표(스톨 0!), taken BEQ 너머 생산자(스톨 0), 같은 레지스터 연속 갱신. 랜덤 프로그램 3000개에서 상태·retired·사이클이 모두 일치. 해저드 유닛을 끄면 `ADD r2, r1, r1` 이 r1 = 0 을 읽어 **r2 = 0**(정답 10) — 8 사이클에 틀린 답.

### Part C — 루프 프로그램, CPI, 단일 사이클과 비교

- B09 배열 합 · memcpy 를 파이프라인에서 돌려 다이어그램과 CPI 표(N = 1, 10, 100)를 낸다. 공식 **10N+12 / 11N+11** 을 손으로 세서 검증.
- `prog_sum_sched[]` — 같은 명령 6개로 루프 몸통을 재배치해 스톨 0 (8N+12). memcpy 재배치판은 주어진다.
- `pipe_tclk_ps()` / `single_tclk_ps()` — 단계 지연 + 레지스터 오버헤드(Tclk-q + Tsetup = 50 ps 가정)로 클럭 주기. 단일 사이클 850 ps vs 파이프라인 250 ps, Fmax(내림) 1176 vs 4000 MHz, 배열 합 N=100 의 실행 시간.

## 2. Problem (English)

> **Turn a single-cycle toy CPU into a 5-stage in-order pipeline without forwarding.**
>
> ISA: the 16-bit toy ISA from ARCH-B08 (`ADD/ADDI/LD/ST/BEQ/JAL/HALT`, `r0` = 0, word-addressed `imem[256]`/`dmem[256]`). Stages IF, ID, EX, MEM, WB. The register file is written in the first half of a cycle and read in the second half. Registers are read in ID; ALU, branch compare and BEQ/JAL target are computed in EX; memory is accessed in MEM; results (including JAL's link `PC+1`) are written in WB.
>
> 1. Implement the clock edge `pipe_advance()`: normal shift, stall (hold PC and IF/ID, insert a bubble into ID/EX), and flush (a taken BEQ or any JAL in EX squashes IF and ID and redirects fetch; HALT in EX squashes IF/ID and stops fetch). Cycle 1 is the first fetch; the run ends in the cycle HALT is in WB. With no hazards, N instructions take N + 4 cycles.
> 2. Implement RAW detection: `insn_dst`, `insn_srcs`, `hazard_stall` (a source register ≠ r0 that an instruction in EX or MEM will write ⇒ stall). Expected stalls by distance: 1 → 2, 2 → 1, ≥ 3 → 0.
> 3. Implement `analytic_cycles(trace)` that predicts the cycle count from the retired-instruction trace of a single-cycle reference, and verify on thousands of random terminating programs that the pipeline's final registers, memory and retired count match the reference and its cycle count matches the formula.
> 4. Print a pipeline diagram (one row per dynamic instruction, `IF ID EX ME WB`, repeated stage = stall, `xx` = flushed) and `cycles=.. retired=.. CPI=.. stalls=.. flushes=..`. Reschedule the array-sum loop to remove all stalls, and compare execution time against the single-cycle design using stage delays 200/100/200/200/100 ps plus 50 ps register overhead.
>
> Example: `ADDI r1,r0,5; ADD r2,r1,r1; ADDI r3,r0,1; HALT` → 10 cycles, 2 stalls, `r2 = 10`. Array sum with N = 10 → `cycles=112 retired=66 CPI=1.70 stalls=20 flushes=11`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 이 쭉 나오고 "2/36 passed" — TODO 8곳을 채워 36/36 을 만든다
```

TODO 는 `insn_dst`, `insn_srcs`, `hazard_stall`, `pipe_advance`, `analytic_cycles`, `prog_sum_sched[]`(몸통 4줄 재배치), `pipe_tclk_ps`, `single_tclk_ps` 8곳이다. 디코더 · 디스어셈블러 · `pipe_cycle()` 의 단계별 동작 · 다이어그램 기록/출력 · 단일 사이클 정답지 `ref_run()` 은 주어진다. 시작 코드에서는 `pipe_advance` 가 아무것도 옮기지 않으므로 첫 명령이 영원히 IF 에 머물고(다이어그램에 `IF IF IF …`), 실행은 사이클 한도(100000)에서 끊긴다 — 무한 루프에 빠지지 않는다.

## 4. 힌트

<details>
<summary>힌트 1 — pipe_advance 의 뼈대</summary>

```c
WB ← MEM; MEM ← EX;                 /* 뒤쪽은 언제나 진행 (스톨은 ID 에서 생기므로) */
if (redirect || halt_ex) { ID, IF squash; EX ← 버블; redirect ? (pc = target, flushes++) : fetch_on = false; }
else if (stall)          { EX ← 버블; ID, IF 그대로; stalls++; }
else                     { EX ← ID; ID ← IF; IF ← 빈 칸; }
```

IF 칸을 비워 두면 다음 사이클 시작에 `pipe_cycle` 이 `p->pc` 에서 새로 fetch 한다. 스톨일 때는 IF 칸이 차 있으니 fetch 하지 않는다 → "PC 쓰기 금지" 가 자동으로 된다.

</details>

<details>
<summary>힌트 2 — 왜 리다이렉트가 스톨보다 먼저인가</summary>

ID 에서 스톨을 일으킨 명령은 EX 에 있는 taken 분기 **뒤** 의 명령, 즉 잘못된 경로(wrong path)의 명령이다. 어차피 버려질 명령 때문에 기다리면 사이클만 날리고, 순서를 반대로 하면 분기가 flush 를 못 해서 잘못된 명령이 실행된다. 같은 이유로 HALT 도 EX 에서만 처리한다 — ID 의 HALT 는 앞선 분기에 의해 버려질 수 있다.

</details>

<details>
<summary>힌트 3 — 소스와 목적지</summary>

decode 가 "읽는 레지스터는 rs/rt, 쓰는 레지스터는 rd" 로 정규화해 두었다. 함정 두 개: **ST 의 11..9 비트(데이터)는 rt = 소스** 이고 목적지가 없다. **JAL 은 아무것도 읽지 않지만 rd 를 쓴다.** BEQ 는 둘 다 읽고 아무것도 안 쓴다. rd = 0 이면 목적지가 없는 것으로 친다.

</details>

<details>
<summary>힌트 4 — 해석 공식</summary>

`EX(k)` = 명령 k 가 EX 에 들어가는 사이클. 처음 EX(0) = 3. 순서 때문에 `EX(k) ≥ EX(k−1) + 1`, 바로 앞이 리다이렉트였으면 `+ 3`. 소비자는 ID(= EX(k) − 1)에서 읽고 생산자는 WB(= EX(p) + 2)에서 쓰며 split-phase 라 같은 사이클이면 되므로 `EX(k) ≥ EX(p) + 3`. 레지스터마다 "가장 최근 생산자 기준 준비 시각" `ready[r]` 만 들고 다니면 된다. 총 사이클 = EX(HALT) + 2.

</details>

<details>
<summary>힌트 5 — 재배치</summary>

`LD r4` → `ADD r3, r3, r4` 사이가 거리 1 이라 스톨 2. 사이에 r4 와 무관한 명령 두 개를 넣으면 거리 3 이 된다. 루프 안에 그런 명령이 마침 두 개 있다(포인터 증가, 카운터 감소). 카운터를 뒤로 미뤄도 루프 머리 `BEQ r1, r0` 까지는 거리가 충분한지 확인할 것.

</details>

## 5. 풀이

### Part A — 한 사이클을 "뒤에서부터" 처리하고, 에지에서 한꺼번에 옮긴다

`pipe_cycle()` 은 하드웨어의 한 클럭을 이렇게 흉내 낸다:

| 순서 | 하는 일 | 왜 이 순서인가 |
|---|---|---|
| 1 | IF 칸이 비었으면 `imem[pc]` fetch (PC ≥ 256 → NOP) | 스톨 중이면 IF 칸이 차 있어 fetch 안 함 = PC 유지 |
| 2 | WB: rd ≠ 0 이면 레지스터 쓰기, retired++ (HALT 면 끝) | **전반부 쓰기** — 아래 ID 읽기보다 먼저 |
| 3 | MEM: LD 읽기 / ST 쓰기 | |
| 4 | EX: ALU, 유효 주소, BEQ 비교 → redirect/target, JAL 링크, HALT 표시 | |
| 5 | ID: decode + `reg[rs]`, `reg[rt]` 읽기 → `hazard_stall()` | **후반부 읽기** — 2단계의 쓰기가 보인다 |
| 6 | `pipe_advance()` = 클럭 에지 | 모든 "다음 상태" 를 계산한 뒤 한 번에 (ARCH-B06 의 2단계 갱신) |

`st[s]` 는 "이번 사이클에 단계 s 에서 일하는 명령" 이고, 에지 직후에는 그대로 **그 단계 앞의 파이프라인 레지스터 내용** 이다. 그래서 `slot_t` 의 필드가 곧 파이프라인 레지스터의 필드다: ID/EX 는 `a`, `b`(읽은 레지스터 값), `d.imm`, `d.rd`, 제어(op); EX/MEM 은 `val`(ALU 결과/유효 주소), `b`(ST 데이터), `wb`; MEM/WB 는 `val`(쓸 값), `d.rd`. 테스트가 3 사이클 뒤 `st[S_MEM].pc == 0 && st[S_MEM].val == 1` 을 보는 이유.

**N + 4**: 명령 i(0부터)는 사이클 i+1 에 IF, i+5 에 WB. 마지막(HALT, i = N−1)의 WB = N+4. 처음 4 사이클은 파이프라인을 채우는 시간(fill)이고 그 뒤로는 매 사이클 하나씩 retire. 실제 출력(해저드 없는 3명령):

```text
 pc  instruction       1  2  3  4  5  6  7
  0  ADDI r1, r0, 1    IF ID EX ME WB
  1  ADDI r2, r0, 2       IF ID EX ME WB
  2  HALT                    IF ID EX ME WB
  3  NOP                        IF ID xx
  4  NOP                           IF xx
```

HALT 가 EX 에 있는 사이클 5 에 IF(pc4)와 ID(pc3)는 이미 투기적으로 들어와 있다 → 에지에서 squash, 사이클 6 칸에 `xx`. 이들은 retire 하지 않으므로 retired = 3, CPI = 7/3.

**taken 분기 (2 사이클 flush)**:

```text
 pc  instruction       1  2  3  4  5  6  7  8  9
  0  BEQ r0, r0, +2    IF ID EX ME WB
  1  ADDI r1, r0, 7       IF ID xx
  2  ADDI r1, r0, 8          IF xx
  3  ADDI r2, r0, 9             IF ID EX ME WB
  4  HALT                          IF ID EX ME WB
  5  NOP                              IF ID xx
  6  NOP                                 IF xx
```

BEQ 는 사이클 3 에 EX 에서 "taken, 목표 3" 을 안다. 그 사이 not-taken 으로 가정하고 가져온 pc1, pc2 가 버려지고 목표는 사이클 4 에 IF. 분기 바로 다음에 실행될 명령(pc3)이 원래라면 사이클 2 에 fetch 됐어야 하는데 4 에 fetch 됐다 → **2 사이클 손해**. 9 = 3 retired + 4 + 2. not-taken BEQ 는 예측이 맞았으니 0, JAL 은 무조건 점프인데도 EX 에서야 목표를 계산하므로 오프셋 0 이어도 항상 2 (B09 에서 "JAL r0, +0 = 다음 명령" 이던 것이 여기선 공짜가 아니다).

정리하면 이 파이프라인의 사이클 수는 항상

**cycles = retired + 4 + stalls + 2 × flushes**

이다(flushes = taken BEQ/JAL 수, HALT 의 squash 는 사이클을 늘리지 않으므로 세지 않는다). fuzz 테스트가 이 항등식도 매번 확인한다.

### Part B — 거리, split-phase, 그리고 함정들

**거리 d** = 생산자와 소비자의 프로그램 순서 차이(바로 다음이면 1). 소비자가 ID 에 들어올 때 생산자가 어디 있나:

| 거리 | 소비자가 처음 ID 에 있을 때 생산자 위치 | 기다릴 사이클 | 스톨 |
|---|---|---|---|
| 1 | EX | EX → MEM → WB | **2** |
| 2 | MEM | MEM → WB | **1** |
| 3 | WB | split-phase: 같은 사이클 전반부에 씀 | **0** |
| ≥ 4 | 이미 끝남 | — | 0 |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">거리 1 RAW: ADDI r1 → ADD r2, r1, r1 (포워딩 없음)</text>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="215" y="44">1</text><text x="275" y="44">2</text><text x="335" y="44">3</text><text x="395" y="44">4</text>
    <text x="455" y="44">5</text><text x="515" y="44">6</text><text x="575" y="44">7</text><text x="635" y="44">8</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="74">ADDI r1, r0, 5</text><text x="10" y="114">ADD r2, r1, r1</text>
    <text x="10" y="154">ADDI r3, r0, 1</text><text x="10" y="194">EX 칸</text>
  </g>
  <g stroke="currentColor">
    <rect x="185" y="56" width="60" height="28" class="f-mut"/><rect x="245" y="56" width="60" height="28" class="f-mut"/>
    <rect x="305" y="56" width="60" height="28" class="f-mut"/><rect x="365" y="56" width="60" height="28" class="f-mut"/>
    <rect x="425" y="56" width="30" height="28" class="f-ok"/><rect x="455" y="56" width="30" height="28" class="f-mut"/>
    <rect x="245" y="96" width="60" height="28" class="f-mut"/><rect x="305" y="96" width="60" height="28" class="f-warn"/>
    <rect x="365" y="96" width="60" height="28" class="f-warn"/><rect x="425" y="96" width="30" height="28" class="f-mut"/>
    <rect x="455" y="96" width="30" height="28" class="f-ok"/><rect x="485" y="96" width="60" height="28" class="f-mut"/>
    <rect x="545" y="96" width="60" height="28" class="f-mut"/><rect x="605" y="96" width="60" height="28" class="f-mut"/>
    <rect x="305" y="136" width="60" height="28" class="f-mut"/><rect x="365" y="136" width="60" height="28" class="f-warn"/>
    <rect x="425" y="136" width="60" height="28" class="f-warn"/><rect x="485" y="136" width="60" height="28" class="f-mut"/>
    <rect x="365" y="176" width="60" height="28" class="f-warn"/><rect x="425" y="176" width="60" height="28" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="215" y="75">IF</text><text x="275" y="75">ID</text><text x="335" y="75">EX</text><text x="395" y="75">ME</text>
    <text x="440" y="75">쓰기</text><text x="470" y="75">WB</text>
    <text x="275" y="115">IF</text><text x="335" y="115">ID</text><text x="395" y="115">ID</text><text x="440" y="115">ID</text>
    <text x="470" y="115">읽기</text><text x="515" y="115">EX</text><text x="575" y="115">ME</text><text x="635" y="115">WB</text>
    <text x="335" y="155">IF</text><text x="395" y="155">IF</text><text x="455" y="155">IF</text><text x="515" y="155">ID</text>
    <text x="395" y="195">버블</text><text x="455" y="195">버블</text>
  </g>
  <text x="185" y="232" fill="currentColor" font-size="12">사이클 3, 4: r1 의 생산자가 EX, MEM 에 있음 → 스톨 (색칠한 칸 = 기다리는 중)</text>
  <text x="185" y="252" fill="currentColor" font-size="12">사이클 5: WB 가 앞 절반에 r1 = 5 를 쓰고, ID 가 뒤 절반에 읽는다 → 스톨 2 로 충분</text>
  <text x="185" y="272" fill="currentColor" font-size="12">split-phase 가 없다면 사이클 5 도 기다려야 해서 스톨 3 (추가 연습 2)</text>
  <text x="10" y="292" fill="currentColor" font-size="12">총 10 사이클 = 4 명령 + 4 + 스톨 2. 실제 시뮬레이터 출력은 아래.</text>
</svg>
```

실제 출력(테스트가 이 문자열 전체를 비교한다):

```text
 pc  instruction       1  2  3  4  5  6  7  8  9  10
  0  ADDI r1, r0, 5    IF ID EX ME WB
  1  ADD r2, r1, r1       IF ID ID ID EX ME WB
  2  ADDI r3, r0, 1          IF IF IF ID EX ME WB
  3  HALT                             IF ID EX ME WB
  4  NOP                                 IF ID xx
  5  NOP                                    IF xx
```

ID 가 세 번(사이클 3·4·5) 반복되고 뒤 명령은 IF 에 세 번 머문다. 버블은 행이 아니므로 다이어그램엔 안 보이고, "ADD 의 EX 가 사이클 4 가 아니라 6" 인 것으로 드러난다.

**왜 버블을 EX 에 넣나**: 하드웨어에서 스톨은 (1) PC 쓰기 금지 (2) IF/ID 레지스터 쓰기 금지 (3) ID/EX 의 제어 신호를 0 으로(RegWrite = 0, MemWrite = 0, 분기 아님) — 즉 ID/EX 에 NOP 을 넣는 것이다. ID 의 명령을 EX 로 보내면서 ID 에도 남겨 두면 같은 명령이 두 번 실행된다.

**면접관이 찌르는 경우** — 모두 테스트에 있다:

| 경우 | 프로그램 | 스톨 / flush | 이유 |
|---|---|---|---|
| r0 | `ADDI r0,r0,5; ADD r1,r0,r0` | 0 / 0 | r0 에 쓰는 명령은 생산자가 아니다(`insn_dst` = 0). 검사하면 가짜 스톨 |
| 최댓값 | `ADDI r1; ADDI r2; ADD r3,r1,r2` | **2** / 0 | r1(거리 2)의 1 스톨은 r2(거리 1)를 기다리는 동안 저절로 해결. 합(3)이 아님 |
| BEQ 소스 | `ADDI r1; BEQ r1,r0,…` | 2 / 0 | BEQ 는 ID 에서 두 레지스터를 읽는다(비교는 EX). 분기도 소비자 |
| ST 데이터 | `ADDI r1,r0,7; ST r1,3(r0)` | 2 / 0 | ST 의 11..9 비트 = **저장할 값** = 소스. 목적지 아님 |
| ST 주소 | `ADDI r2,r0,9; ST r0,1(r2)` | 2 / 0 | 베이스 레지스터 |
| LD 생산자 | `LD r1,4(r0); ADD r2,r1,r1` | 2 / 0 | 포워딩이 없으니 ALU 생산자와 같다. load-use 의 "특별함" 은 포워딩이 생겨야 보인다(ARCH-M02) |
| JAL → 목표 | `JAL r7,+0; ADD r1,r7,r7` | **0** / 1 | JAL 이 EX 에서 리다이렉트 → 목표는 2 사이클 뒤 ID. 그때 JAL 은 WB → split-phase 로 읽힘 |
| JAL 건너뛰기 | `JAL r7,+1; (건너뜀); ADD r2,r7,r0` | 0 / 1 | 같은 이유. retired 3 |
| 분기 너머 | `ADDI r1,r0,3; BEQ r0,r0,+0; ADD r2,r1,r1` | 0 / 1 | taken BEQ 의 flush 2 사이클이 거리를 벌려 준다 |
| 연쇄 | `ADDI r1,r0,1; ADDI r1,r1,1; ADDI r1,r1,1` | 4 / 0 | 매번 거리 1. **가장 최근** 생산자만 보면 된다(WAW 는 이 파이프라인에선 순서대로 써져 문제 없음) |

**해석 공식 — 두 번째 정답지**. 시뮬레이터를 같은 시뮬레이터로 검증하면 같은 버그를 두 번 짜게 된다. 그래서 사이클 수를 완전히 다른 방법으로 계산한다: 단일 사이클 모델이 retire 순서대로 남긴 `(명령, 리다이렉트 여부)` 트레이스만 보고

```text
EX(0) = 3
EX(k) = max( EX(k-1) + (k-1 이 taken BEQ/JAL ? 3 : 1),
             max over sources r of k:  EX(마지막으로 r 을 쓴 p) + 3 )
cycles = EX(HALT) + 2
```

이 공식은 "스톨 = 소비자가 생산자의 WB 를 기다리는 것" 과 "flush = 리다이렉트 뒤 2 사이클" 이라는 규칙의 **정의** 를 그대로 옮긴 것이다. 파이프라인 래치를 옮기는 코드와 공통 부분이 `insn_dst/insn_srcs` 뿐이라, 둘이 3000개 프로그램에서 같으면 꽤 믿을 만하다(같은 실수를 할 여지가 그 두 함수로 좁혀지고, 그 둘은 표 테스트로 따로 확인한다).

**랜덤 프로그램 fuzz**: 길이 8–120, 무작위 ADD/ADDI/LD/ST/BEQ/JAL/NOP + 끝에 HALT. BEQ·JAL 은 **앞으로만**(목표 ≤ HALT 위치) → 반드시 끝난다. 주소 폴트가 없도록 r6 은 `ADDI r6, r0, k (0..31)` 로만 쓰고 LD/ST 베이스는 r0 또는 r6(주소 0..62). dmem 은 무작위로 채운다. 세 가지를 확인한다: 최종 r0–r7 · dmem 256워드 · retired 가 단일 사이클과 같다 / cycles == 해석 공식 / cycles == retired + 4 + stalls + 2·flushes. 실제 출력:

```text
  fuzz 3000 programs: retired=72029 cycles=113631 stalls=12984 flushes=8309 CPI=1.578
```

명령당으로 나누면 CPI 1.578 = 1 + 스톨 12984/72029 (0.180) + 2 × flush 8309/72029 (0.231) + 프로그램마다 채우기 4 사이클 × 3000/72029 (0.167). 랜덤 코드는 레지스터가 8개뿐이라 의존이 촘촘하고 분기가 많다 — 실제 코드는 컴파일러가 스케줄링하므로 이보다 낫다.

**해저드 유닛을 끄면**: 같은 `ADDI r1,r0,5; ADD r2,r1,r1; …` 를 `no_hazard_unit = true` 로 돌리면 8 사이클(= 4 + 4)에 끝나지만 r2 = 0 (ADD 가 ID 에서 아직 안 쓴 r1 = 0 을 읽음). 빠르고 틀린 CPU. 실제로 초기 MIPS 는 이 판단을 하드웨어 대신 **컴파일러에** 맡겼다(load delay slot — Q4).

### Part C — 루프의 CPI 와 재배치, 그리고 시간

배열 합 N = 1 의 실제 다이어그램 (한 바퀴 + 탈출):

```text
 pc  instruction       1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22
  0  LD r1, 0(r0)      IF ID EX ME WB
  1  ADDI r2, r0, 16      IF ID EX ME WB
  2  ADD r3, r0, r0          IF ID EX ME WB
  3  BEQ r1, r0, +5             IF ID EX ME WB
  4  LD r4, 0(r2)                  IF ID EX ME WB
  5  ADD r3, r3, r4                   IF ID ID ID EX ME WB
  6  ADDI r2, r2, 1                      IF IF IF ID EX ME WB
  7  ADDI r1, r1, -1                              IF ID EX ME WB
  8  BEQ r0, r0, -6                                  IF ID EX ME WB
  9  ST r3, 1(r0)                                       IF ID xx
 10  HALT                                                  IF xx
  3  BEQ r1, r0, +5                                           IF ID EX ME WB
  4  LD r4, 0(r2)                                                IF ID xx
  5  ADD r3, r3, r4                                                 IF xx
  9  ST r3, 1(r0)                                                      IF ID EX ME WB
 10  HALT                                                                 IF ID EX ME WB
 11  NOP                                                                     IF ID xx
 12  NOP                                                                        IF xx
cycles=22 retired=12 CPI=1.83 stalls=2 flushes=2
```

읽는 법: pc 5 의 ID 3번 = LD→ADD 거리 1 스톨 2. pc 8 의 뒤로 가는 BEQ 는 무조건 taken 인데 EX 에서야 알아서 pc 9, 10 이 버려진다. 두 번째 바퀴 머리 `BEQ r1, r0` 는 r1 의 생산자(pc 7)와 거리 2 지만 그 사이 flush 2 사이클 덕분에 스톨 0. 탈출 BEQ 가 taken 이라 pc 4, 5 가 버려진다.

**손으로 세기** — 반복 1회 = 명령 6 + 스톨 2 + 뒤로 가는 BEQ flush 2 = 10 사이클. 전체:

| 프로그램 | retired | stalls | flushes | cycles = retired + 4 + stalls + 2·flushes |
|---|---|---|---|---|
| 배열 합 | 6N + 6 | 2N | N + 1 | **10N + 12** |
| 배열 합 재배치 | 6N + 6 | 0 | N + 1 | **8N + 12** |
| memcpy | 7N + 5 | 2N (LD→ST 데이터) | N + 1 | **11N + 11** |
| memcpy 재배치 | 7N + 5 | 0 | N + 1 | **9N + 11** |

실제 출력의 CPI 표(cycles/retired, CPI):

```text
  program                      N=1               N=10              N=100
  sum                22/12    1.83     112/66    1.70    1012/606   1.67
  sum_sched          20/12    1.67      92/66    1.39     812/606   1.34
  memcpy             22/12    1.83     121/75    1.61    1111/705   1.58
  memcpy_sched       20/12    1.67     101/75    1.35     911/705   1.29
```

N 이 커지면 CPI 는 반복 1회의 비율로 수렴한다: 10/6 = 1.67, 8/6 = 1.33, 11/7 = 1.57, 9/7 = 1.29. 재배치(`prog_sum_sched`)는 `LD r4; ADDI r2; ADDI r1; ADD r3, r3, r4` — LD 와 ADD 사이를 **이미 루프에 있던 독립 명령** 으로 채웠다. 명령 수는 같고 사이클만 20% 줄었다. 카운터 감소를 뒤로 옮겨도 루프 머리 BEQ 까지 거리(ADD, BEQ, flush 2)는 충분하다. 남은 0.33 은 전부 뒤로 가는 BEQ 의 flush — 이건 재배치로 못 없앤다. 분기 예측(ARCH-M02), 루프 펼치기(B09 추가 연습 1), 하드웨어 루프 명령이 그 몫이다.

**시간: 단일 사이클 vs 파이프라인**. B09 의 단계 지연에 레지스터 오버헤드(Tclk-q + Tsetup, ARCH-B13 의 그 항목. 이 레슨의 가정값 30 + 20 = 50 ps)를 더하면:

| 설계 | Tclk | Fmax (내림) | 배열 합 N=100 | 시간 |
|---|---|---|---|---|
| 단일 사이클 (B09) | 200+100+200+200+100 + 50 = **850 ps** | 1176 MHz | 606 사이클 (CPI 1) | 515 100 ps |
| 파이프라인 (M01) | max 단계 200 + 50 = **250 ps** | 4000 MHz | 1012 사이클 (CPI 1.67) | 253 000 ps |
| 파이프라인 + 재배치 | 250 ps | 4000 MHz | 812 사이클 (CPI 1.34) | 203 000 ps |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">배열 합 N=100 실행 시간 = 사이클 수 × Tclk (막대 길이 ∝ 시간)</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="54">단일 사이클</text><text x="10" y="96">파이프라인</text>
    <text x="10" y="138">+ 재배치</text><text x="10" y="180">이상 (CPI 1)</text>
  </g>
  <g stroke="currentColor">
    <rect x="120" y="38" width="560" height="26" class="f-mut"/>
    <rect x="120" y="80" width="275" height="26" class="f-acc"/>
    <rect x="120" y="122" width="221" height="26" class="f-ok"/>
    <rect x="120" y="164" width="166" height="26" class="f-ok" stroke-dasharray="4 3"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="128" y="56">606 × 850 ps = 515 100 ps</text>
    <text x="405" y="98">1012 × 250 = 253 000 ps  (2.04배)</text>
    <text x="351" y="140">812 × 250 = 203 000 ps  (2.54배)</text>
    <text x="296" y="182">610 × 250 = 152 500 ps  (≈ 3.4배 = 850/250)</text>
  </g>
  <text x="10" y="220" fill="currentColor" font-size="12">5단인데 5배가 아닌 이유: ① 단계가 불균형(최장 200 vs 평균 160) ② 단계마다 레지스터 오버헤드 50 ps</text>
  <text x="10" y="240" fill="currentColor" font-size="12">③ 해저드로 CPI &gt; 1. 그래도 CPI 가 850/250 = 3.4 보다 작은 한 파이프라인이 이긴다 (추가 연습 5).</text>
</svg>
```

실제 출력: `speedup vs single: pipe 2.04x, sched 2.54x, ideal (CPI 1) 3.40x`. 지연(latency) 관점에선 명령 하나가 5 × 250 = 1250 ps 로 단일 사이클 850 ps 보다 **길어졌다** — 파이프라인은 한 명령을 빨리 끝내는 게 아니라 **처리량(throughput)** 을 올리는 기술이다. 그리고 Fmax 가 단계 하나의 경로로 정해지므로 ARCH-B13 의 STA 관점에서는 "가장 긴 단계 = critical path" 가 되고, 단계를 더 잘게 쪼갤수록 오버헤드 비율과 분기·해저드 페널티(사이클 수)가 커진다 — 깊은 파이프라인의 대가.

펌웨어 쪽 연결: Cortex-M 은 파이프라인이 짧다(Arm 자료 기준 M0+ 2단, M3/M4 3단, M7 은 6단 이중 발행). 짧은 파이프라인은 분기 페널티가 작고 인터럽트 지연이 예측 가능하지만 클럭을 크게 못 올린다. 플래시 대기 상태(ARCH-B12)까지 있으면 taken 분기마다 fetch 가 끊기는 비용이 더 커지고, 그래서 MCU 의 핫 루프는 분기를 줄이고(펼치기) 플래시 가속기/캐시나 TCM 에 둔다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 경고 없이 36/36
```

실제 출력에서 다이어그램 블록 4개(§5 의 Part A 두 개, Part B 하나, Part C 하나에 그대로 실었다)만 빼고 나머지 줄을 그대로 옮긴다:

```text
Part A — 파이프라인 레지스터 · N+4 · flush
Part B — RAW 탐지 · 스톨 · 해석 공식 · fuzz
  fuzz 3000 programs: retired=72029 cycles=113631 stalls=12984 flushes=8309 CPI=1.578
Part C — 루프 · CPI · 단일 사이클 대비 시간
cycles=22 retired=12 CPI=1.83 stalls=2 flushes=2
  sum_sched N=1: cycles=20 retired=12 CPI=1.67 stalls=0 flushes=2
  program                      N=1               N=10              N=100
  sum                22/12    1.83     112/66    1.70    1012/606   1.67
  sum_sched          20/12    1.67      92/66    1.39     812/606   1.34
  memcpy             22/12    1.83     121/75    1.61    1111/705   1.58
  memcpy_sched       20/12    1.67     101/75    1.35     911/705   1.29
  sum N=100: single 515100 ps (606 x 850), pipe 253000 ps (1012 x 250), sched 203000 ps (812 x 250)
  speedup vs single: pipe 2.04x, sched 2.54x, ideal (CPI 1) 3.40x
36/36 passed
```

ASan + UBSan 빌드도 같은 출력에 `36/36 passed`, 진단 메시지 없음.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| ID(읽기)를 WB(쓰기)보다 먼저 처리 | split-phase 가 깨진다: 거리 3 에서 옛 값을 읽어 상태가 틀린다(스톨 0 인데 값이 안 보임) | 한 사이클 안에서 WB → MEM → EX → ID 순서. 아니면 해저드 검사에 WB 까지 넣는다(= 스톨 3/2/1, 추가 연습 2) |
| `insn_dst` 가 rd 를 그대로 돌려줌 (ST/BEQ 에도) | decode 가 ST/BEQ 의 rd 를 0 으로 두긴 하지만, 필드를 "비트 위치" 로 보고 11..9 를 목적지로 읽으면 **ST 뒤에 가짜 스톨 + ST 데이터 해저드 누락** | 명령 종류별로: ADD/ADDI/LD/JAL 만 rd |
| r0 를 목적지로 인정 | `ADDI r0, …` 뒤 `ADD x, r0, r0` 가 스톨 — 값은 맞고 사이클만 틀려서 상태 비교로는 안 잡힌다(그래서 해석 공식/스톨 수 테스트가 필요) | `insn_dst` 가 rd = 0 이면 0, 소스도 0 이면 건너뜀 |
| 스톨 때 IF 를 비우거나 PC 를 증가 | 스톨 중에 fetch 된 명령 하나가 사라지거나 두 번 들어온다 | 스톨이면 IF·ID 칸을 그대로 둔다(IF 칸이 차 있으면 fetch 안 함) |
| 스톨 때 EX ← ID 복사 후 ID 유지 | 같은 명령이 두 번 실행(ADDI r1,r1,1 이 2 증가) | 스톨이면 EX 는 **버블**(memset 0) |
| 스톨을 리다이렉트보다 먼저 처리 | 잘못된 경로 명령 때문에 기다리고, flush 가 안 되어 그 명령이 실행된다 | 우선순위: 리다이렉트/HALT > 스톨 > 정상 |
| HALT 를 ID 나 IF 에서 처리 | 앞선 taken 분기가 버려야 할 wrong-path HALT 에서 멈춘다 (예: `BEQ r0,r0,+1; HALT; …`) | HALT 는 EX(비투기 지점)에서만 fetch 를 멈춘다 |
| 거리별 스톨을 소스마다 **더함** | `ADD r3, r1, r2` (r1 거리 2, r2 거리 1) 을 3 으로 셈. 기다림은 겹친다 | 해석 공식은 `max`, 시뮬레이터는 매 사이클 "둘 중 하나라도 걸리면 스톨" |
| flush 된 명령도 retired 로 셈 | CPI 가 실제보다 좋아 보인다(분모가 커짐) | retired++ 는 WB 에서만 |
| `flushes` 에 HALT 의 squash 를 포함하고 항등식에 2× | cycles = retired + 4 + stalls + 2·flushes 가 2 사이클 어긋난다 | HALT squash 는 사이클 비용이 없다 — 따로 세거나 빼라 |

## 8. 면접 Q&A

<details>
<summary>Q1. 5단 파이프라인이면 단일 사이클보다 5배 빠른가요?</summary>

아니다. 이상적으로 처리량이 "단계 수" 배가 되려면 ① 단계가 완벽히 균형 ② 파이프라인 레지스터 오버헤드 0 ③ CPI = 1 이어야 한다. 이 레슨 숫자: 단계 200/100/200/200/100 ps 는 불균형(최장 200)이고 오버헤드 50 ps → Tclk 250 vs 850 ps = 최대 3.4배. 해저드로 CPI 1.67 이면 2.04배, 재배치해서 CPI 1.34 면 2.54배.
**꼬리질문: 그럼 단계를 20개로 쪼개면?** Tclk 는 (총 지연/단계 수 + 오버헤드)로 줄지만 오버헤드 비율이 커지고, RAW 거리·분기 페널티가 사이클 단위로 커져 CPI 가 오른다. 그래서 "최적 깊이" 가 있다 — Pentium 4 의 깊은 파이프라인이 이후 설계에서 다시 얕아진 것이 자주 드는 예다. 전력(래치 수, 클럭 트리)도 늘어난다.

</details>

<details>
<summary>Q2. 거리 1 RAW 가 왜 정확히 2 사이클 스톨인가요? split-phase 가 없으면?</summary>

생산자는 WB 에서 쓰고 소비자는 ID 에서 읽는다. 거리 1 이면 소비자가 ID 에 처음 왔을 때 생산자는 EX → MEM → WB 까지 2 사이클 더 가야 한다. WB 사이클에 레지스터 파일이 전반부에 쓰고 후반부에 읽으므로(Tclk 250 ps 안에 쓰기 100 + 읽기 100 이 들어간다) 그 사이클에 바로 읽힌다 → 2. split-phase 가 없으면(쓰기가 사이클 끝 에지에 반영) WB 다음 사이클에야 읽을 수 있어 거리 1/2/3 → 3/2/1 (추가 연습 2: 배열 합 N=10 이 112 → 123 사이클).
**꼬리질문: 대신 레지스터 파일에 "내부 바이패스" 를 넣으면?** 같은 사이클에 쓰는 주소와 읽는 주소가 같으면 쓰는 값을 바로 읽기 포트로 넘기는 mux — split-phase 와 같은 효과를 에지 기반 레지스터 파일로 얻는 흔한 방법이다. 포워딩의 가장 작은 형태라고 볼 수 있다(본격적인 EX/MEM → EX 포워딩은 ARCH-M02).

</details>

<details>
<summary>Q3. 스톨은 하드웨어에서 어떻게 만드나요? 왜 ID 에서 판단하나요?</summary>

hazard unit 이 ID 의 rs/rt 와 ID/EX·EX/MEM 의 rd(+ RegWrite)를 비교해 스톨 신호를 낸다. 스톨이면 PC 와 IF/ID 의 write enable 을 끄고, ID/EX 의 제어 신호를 0 으로 만든다(버블 = 아무것도 안 쓰는 NOP). ID 에서 판단하는 이유: 거기서 레지스터를 읽으니 "지금 읽은 값이 유효한가" 를 그 자리에서 알 수 있고, 아직 아무 상태도 바꾸지 않은 명령을 붙잡는 게 가장 싸다.
**꼬리질문: 비교기가 몇 개 필요한가?** 소스 2개 × 위험한 단계 2개(EX, MEM) = 4개의 3비트 비교 + r0/RegWrite 조건. 포워딩(M02)을 넣으면 같은 비교기가 "스톨" 대신 "mux 선택" 신호가 되고, 스톨 조건은 load-use 하나로 줄어든다.

</details>

<details>
<summary>Q4. 소프트웨어로 스톨을 줄이는 방법은? 하드웨어가 아예 안 막아 주는 CPU 도 있나요?</summary>

명령 재배치(instruction scheduling): 생산자와 소비자 사이를 독립 명령으로 채운다. 이 레슨의 배열 합은 같은 6명령을 재배치해 반복당 10 → 8 사이클, memcpy 는 11 → 9. 컴파일러의 list scheduling 이 하는 일이고, 포워딩이 있는 코어에서도 load-use 를 숨기는 데 쓰인다.
**꼬리질문: 하드웨어 인터록이 없으면?** 초기 MIPS(MIPS I)는 load 바로 뒤 명령이 아직 load 결과를 못 본다는 **load delay slot** 을 ISA 규칙으로 정했다 — 해저드 검사를 컴파일러에 맡기고, 채울 게 없으면 NOP 를 넣는다. 이후 MIPS 세대에서 하드웨어 인터록으로 바뀌었다. 분기 지연 슬롯(branch delay slot)도 같은 발상이고, 파이프라인 구조가 바뀌면 ISA 에 남은 짐이 된다는 게 교훈.

</details>

<details>
<summary>Q5. 이 시뮬레이터가 맞다는 건 어떻게 보장했나요?</summary>

세 겹: ① **아키텍처 상태 비교** — 같은 프로그램을 단일 사이클 모델(ARCH-B09)로 돌려 r0–r7, dmem 전체, retired 가 같아야 한다. 파이프라인은 "언제" 만 바꾸고 "무엇" 은 바꾸면 안 되므로. ② **독립적인 타이밍 정답지** — retire 트레이스에서 사이클을 계산하는 해석 공식(`EX(k) = max(...)`). ③ **항등식** cycles = retired + 4 + stalls + 2·flushes. 그리고 이것들을 고정 시드 랜덤 프로그램 3000개 + 손으로 고른 경계 경우(r0, ST 데이터, JAL 링크 …)에 돌린다.
**꼬리질문: 상태 비교만으로는 무엇을 놓치나?** 가짜 스톨(r0 를 생산자로 본다든지)은 결과를 바꾸지 않고 사이클만 늘린다 → 성능 버그. 실리콘 검증에서도 기능 비교(ISS lockstep, ARCH-B09 Q4)와 성능 모델 비교는 별개의 체크다.

</details>

<details>
<summary>Q6. 분기는 왜 EX 에서 해결하나요? ID 로 당기면?</summary>

이 파이프라인은 비교와 목표 계산을 ALU 단계에 두었다 → taken 이면 IF·ID 두 개 flush, 2 사이클. 비교기와 목표 가산기를 ID 로 옮기면 1 사이클(추가 연습 1: 배열 합 N=10 이 112 → 101). 대가: ID 가 레지스터를 읽은 **직후** 비교까지 해야 해서 ID 단계 경로가 길어지고(ARCH-B13 critical path), 포워딩이 있는 코어에선 EX/MEM 결과를 ID 비교기로도 보내야 하며, 직전 명령에 의존하는 분기는 추가 스톨이 생긴다. 이 레슨 모델에서도 ID 해결로 바꾸면 `JAL r7; ADD r1, r7, r7` 에 **스톨 1** 이 새로 생긴다(EX 해결에선 0).
**꼬리질문: 그 2 사이클을 없애려면?** 예측 — 2비트 카운터 테이블(ARCH-M02)이나 BTB 로 ID/IF 단계에서 목표를 미리 알고, 틀렸을 때만 flush. MCU 급(Cortex-M3/M4)은 디코드 단계에서 분기 목표를 미리 fetch 해 두는 정도의 단순한 방법(Arm 자료의 branch speculation)을 쓰고, 큰 코어는 정교한 예측기와 BTB 를 쓴다.

</details>

<details>
<summary>Q7. WAR, WAW 해저드는 이 파이프라인에서 왜 안 생기나요?</summary>

WAR(뒤 명령이 앞 명령이 읽기 전에 씀): 모든 명령이 ID 에서 읽고 WB 에서 쓰며 순서대로 흐르므로, 뒤 명령의 WB 는 항상 앞 명령의 ID 보다 늦다. WAW(쓰기 순서 뒤바뀜): 쓰기 단계가 WB 하나뿐이고 순서대로 도착한다. 그래서 순차(in-order) 단일 쓰기 단계 파이프라인에선 RAW 만 신경 쓰면 된다 — 테스트의 "연쇄" 경우가 가장 최근 생산자만 보면 되는 이유.
**꼬리질문: 언제 생기나?** 지연이 다른 실행 유닛(곱셈기 3사이클, FPU 5사이클)이 섞이면 늦게 시작한 짧은 명령이 먼저 써서 WAW, 비순차 실행이면 WAR 도 생긴다. 해결은 스코어보드(쓰기 대기 추적)나 레지스터 리네이밍(ROB 와 함께, ARCH-A06).

</details>

<details>
<summary>Q8. 이걸 실제 펌웨어 성능 분석에 어떻게 연결하나요?</summary>

Cortex-M 에서 사이클을 셀 때 같은 사고방식을 쓴다: 명령 수 + (분기 taken 마다 파이프라인 재충전) + (load 결과를 바로 쓰는 경우 등 코어별 페널티) + (플래시 대기 상태, ARCH-B12). DWT CYCCNT 로 루프를 재 보면 "명령 수 × 1" 보다 많이 나오는데, 차이가 대부분 분기와 메모리 대기다. 정확한 명령별 사이클은 각 코어 TRM 의 instruction timing 표를 봐야 한다(코어마다 다르다; 실제 측정은 BM-A10 에서 DWT 로).
**꼬리질문: 그래서 핫 루프에서 무엇을 하나?** 펼치기로 분기 수를 줄이고, load 와 사용 사이를 띄우고(이 레슨의 재배치), 코드를 TCM/RAM 이나 캐시된 플래시에 둔다. 컴파일러 `-O2` 가 대부분 해 주지만, 인터럽트 핸들러처럼 짧고 결정적이어야 하는 코드에선 생성된 asm 을 직접 확인한다.

</details>

## 9. 추가 연습

`practice.c` 는 solution.c 의 시뮬레이터에 설정 스위치 2개(`split_phase`, `branch_in_id`)를 붙이고, 각 설정에서 해석 공식도 바꿔 **설정마다 시뮬레이터 == 해석 공식 == 단일 사이클 상태** 를 랜덤 프로그램 500개로 다시 확인한다. 기본 설정은 solution.c 와 같은 숫자(배열 합 N=10 = 112)를 낸다.

### 연습 1 — 분기를 ID 에서 해결

BEQ 비교·목표와 JAL 을 ID 에서 해결하도록 바꾸면(taken → IF 하나만 flush) 배열 합 N=10, N=100 의 사이클은? `JAL r7, +0; ADD r1, r7, r7` 의 스톨은 어떻게 바뀌나?

<details>
<summary>답 보기</summary>

flush 페널티 2 → 1 이므로 cycles = retired + 4 + stalls + 1·flushes = 6N+6 + 4 + 2N + (N+1) = **9N + 11** → N=10: **101**(실행 결과 `cycles=101 stalls=20 flushes=11`), N=100: 911. 해석 공식은 리다이렉트 뒤 `+3` 을 `+2` 로 바꾸면 된다. 대신 JAL 이 ID 에서 리다이렉트하면 목표 명령이 ID 에 올 때 JAL 은 아직 MEM → **스톨 1** (EX 해결에선 0). 즉 페널티가 "flush" 에서 "RAW 스톨" 로 일부 옮겨 간다. 실제 하드웨어에선 ID 비교기 때문에 ID 단계 경로가 길어지는 비용도 있다(Q6).

</details>

### 연습 2 — split-phase 레지스터 파일이 없다면

WB 의 쓰기가 사이클 끝에야 반영되면 거리 1/2/3/4 의 스톨은? 배열 합 N=10 은?

<details>
<summary>답 보기</summary>

소비자의 ID 가 생산자의 WB **다음** 사이클 이후여야 하므로 `EX(k) ≥ EX(p) + 4` → 거리 1/2/3/4 = **3/2/1/0**. 시뮬레이터에서는 해저드 검사 범위를 EX..MEM 에서 EX..WB 로 넓히기만 하면 된다. 배열 합 N=10 = **123 사이클, 스톨 31**: 반복당 LD→ADD 3 스톨 × 10 = 30, 그리고 머리말의 `LD r1, 0(r0)` → `BEQ r1, r0` (거리 3)에서 1 이 새로 생긴다. split-phase 하나가 이 루프에서 사이클 9% 를 아낀다.

</details>

### 연습 3 — 손으로 스톨 세기

```text
LD   r1, 0(r0)
LD   r2, 1(r0)
ADD  r3, r1, r2
ST   r3, 2(r0)
ADDI r4, r3, 1
HALT
```

스톨 수와 총 사이클은? (dmem[0] = 40, dmem[1] = 2)

<details>
<summary>답 보기</summary>

ADD: r2 거리 1 → 2 (r1 거리 2 의 1 은 그 안에 해결). ST: r3 거리 1 → 2. ADDI: r3 거리 2 이지만 ST 가 이미 2 사이클을 기다리는 동안 ADD 가 앞서 갔으므로 → **0**. EX 시각으로 보면 ADD 가 e 이면 ST 는 e+3, ADDI 는 e+4 ≥ e+3. 합계 스톨 **4**, 사이클 = 6 + 4 + 4 = **14**, dmem[2] = 42, r4 = 43. 함정: 프로그램 순서의 거리만 보면 ADDI 에 1 을 더 붙이기 쉽다 — 앞의 스톨이 뒤 의존의 거리를 벌려 준다.

</details>

### 연습 4 — 피보나치 루프 스케줄링

B09 피보나치 루프(`BEQ; ST r2,0(r5); ADD r4,r2,r3; ADD r2,r3,r0; ADD r3,r4,r0; ADDI r5; ADDI r1; BEQ`)의 사이클 공식을 세고, 스톨이 0 이 되도록 재배치하라.

<details>
<summary>답 보기</summary>

`ADD r3, r4, r0` 가 r4(거리 2)를 기다려 반복당 스톨 1, 그리고 첫 반복의 `ST r2, 0(r5)` 가 머리말 `ADDI r5, r0, 16` 과 거리 2 라 1 더. n ≥ 1 이면 retired 8n+6, 스톨 n+1, flush n+1 → **11n + 13** (n=20: 233, 스톨 21). 재배치: 몸통에서 `ADDI r5, r5, 1` 을 `ADD r2` 와 `ADD r3` 사이로(ST 가 r5 를 쓴 뒤라 안전), 머리말에서 `ADDI r5, r0, 16` 을 LD 바로 뒤로 → 스톨 0, **10n + 12** (n=20: 212). `ADD r2, r3, r0` 가 `ADD r3` 보다 앞이어야 옛 b 를 읽는다는 의존(WAR 성격의 순서 제약)은 그대로 지켰다 — 재배치는 RAW 뿐 아니라 이런 순서도 깨면 안 된다.

</details>

### 연습 5 — Iron law: 언제 파이프라인이 이기나

실행 시간 = 명령 수 × CPI × Tclk. 단일 사이클(CPI 1)과 파이프라인의 손익분기 CPI 를 레지스터 오버헤드 20/50/100 ps 에 대해 구하라. 배열 합(CPI 1.67)은 언제나 이기나?

<details>
<summary>답 보기</summary>

손익분기 CPI = Tsingle / Tpipe = (800 + o) / (200 + o) → o = 20: **3.73**, 50: **3.40**, 100: **3.00**. 오버헤드가 커질수록 파이프라인의 이득이 줄어든다(단계마다 내는 세금). 배열 합 N=100 의 CPI 1012/606 = 1.67 은 셋 다 이긴다. 명령 수가 같다는 가정이 핵심 — 파이프라인 때문에 NOP 를 넣어야 하는 ISA(지연 슬롯)라면 명령 수도 늘어난다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
```

```text
기준(M01) 설정 — solution.c 와 같은 숫자
연습 1 — 분기를 ID 에서
  sum N=10: cycles=101 stalls=20 flushes=11
연습 2 — split-phase 없음
  sum N=10: cycles=123 stalls=31
연습 3 — 손으로 세기
연습 4 — 피보나치 스케줄링
  fib n=20: 233 cycles (stalls 21) -> sched 212 cycles (stalls 0)
연습 5 — Iron law 손익분기 CPI
  overhead  20 ps: single 820 ps, pipe 220 ps, break-even CPI 3.73
  overhead  50 ps: single 850 ps, pipe 250 ps, break-even CPI 3.40
  overhead 100 ps: single 900 ps, pipe 300 ps, break-even CPI 3.00
14/14 passed
```

## 10. 핵심 정리

- 5단 = IF · ID(읽기) · EX(ALU, 분기 비교/목표) · MEM · WB(쓰기). 파이프라인 레지스터 IF/ID · ID/EX · EX/MEM · MEM/WB 가 명령과 중간값을 들고 간다. 해저드 없으면 **N + 4** 사이클.
- 시뮬레이터는 한 사이클을 **WB → MEM → EX → ID** 로 처리(split-phase: 전반부 쓰기, 후반부 읽기)하고, 에지에서 **리다이렉트/HALT > 스톨 > 정상** 순으로 한꺼번에 옮긴다.
- 포워딩 없는 RAW: 거리 1/2/≥3 → 스톨 **2/1/0** (split-phase 없으면 3/2/1). 스톨 = PC·IF/ID 유지 + ID/EX 버블. 소스가 둘이면 **max**.
- 소스/목적지 함정: r0 는 해저드 아님, **ST 의 데이터 레지스터는 소스**, BEQ 두 피연산자도 소스, **JAL 은 rd 를 쓴다** — 그러나 JAL → 목표는 flush 덕분에 스톨 0.
- taken BEQ / 모든 JAL = flush 2. **cycles = retired + 4 + stalls + 2·flushes**. HALT 는 EX 에서만 fetch 를 멈춘다(wrong-path HALT 방지).
- 검증 = 단일 사이클 모델과 아키텍처 상태 비교 + **독립 해석 공식** `EX(k) = max(EX(k−1)+1|+3, EX(p)+3)` + 랜덤 프로그램 fuzz. 상태 비교만으론 가짜 스톨(성능 버그)을 못 잡는다.
- 배열 합 10N+12 (CPI 1.67) → 재배치 8N+12 (1.34). memcpy 11N+11 → 9N+11. 남은 손해는 뒤로 가는 분기 flush → 예측(ARCH-M02)·펼치기.
- 단일 사이클 850 ps × CPI 1 vs 파이프라인 250 ps × CPI 1.67 → 2.04배 (이상 3.4배, 5배 아님: 단계 불균형 + 레지스터 오버헤드 + 해저드). 파이프라인은 지연이 아니라 처리량을 올린다.

## 참고자료

- Patterson & Hennessy, *Computer Organization and Design* (ARM / RISC-V Edition), Ch. 4.5–4.9 — 파이프라인 데이터패스, 파이프라인 레지스터, hazard detection unit, 레지스터 파일의 전반부 쓰기/후반부 읽기, 스톨과 버블, 분기 flush
- Hennessy & Patterson, *Computer Architecture: A Quantitative Approach*, Appendix C "Pipelining: Basic and Intermediate Concepts" — 해저드 분류(RAW/WAR/WAW), 파이프라인 성능 식, 깊이와 오버헤드
- Harris & Harris, *Digital Design and Computer Architecture, ARM Edition*, Ch. 7.5 — 파이프라인 프로세서, 해저드 유닛
- [Arm Cortex-M3 / M4 Technical Reference Manuals — 파이프라인과 instruction timing](https://developer.arm.com/documentation) (코어별 사이클 수는 각 TRM 에서 확인)
- Kane & Heinrich, *MIPS RISC Architecture* — MIPS I 의 load delay slot / branch delay slot
- 이 트랙: ARCH-B06 (2단계 갱신), ARCH-B08 (ISA), ARCH-B09 (단일 사이클 모델 · 단계 지연), ARCH-B12 (플래시 대기 상태), ARCH-B13 (setup slack · Fmax), ARCH-M02 (포워딩 · load-use · 2비트 예측, 다음 단계), ARCH-M10 (파이프라인 정밀 예외), ARCH-A06 (ROB · 리네이밍), SIM-M01 (사이클 근사 모델), BM-A10 (DWT 로 실측)
