---
id: ARCH-B08
time: 70분
updated: 2026-11-11
---
# ARCH-B08 미니 ISA 설계: 명령어 인코딩·디코딩

## 학습 목표

- 16비트 고정 길이 toy ISA 의 R형·I형·분기형 형식을 비트 단위로 손으로 인코딩·디코딩할 수 있다 (`ADD r1, r2, r3` = `0x0298`).
- 인코더는 범위 밖 즉시값을 **잘라내지 않고 보고**하고, 디코더는 불법 워드를 걸러낸다. 이것을 **65536 워드 전수 왕복**으로 증명한다.
- 레이블이 있는 2-pass 어셈블러와 정규형 디스어셈블러를 만들고, 오류(잘못된 레지스터, 범위 밖 즉시값, 모르는 레이블, 문법)를 줄 번호와 함께 보고한다.
- 바이너리 패치(명령어 끼워 넣기)에서 PC 상대 오프셋을 다시 고치는 일(relocation fix-up)과 "relocation truncated to fit" 이 생기는 이유를 설명한다.
- 즉시값 폭, 레지스터 필드 위치, 16/32비트 혼합(Thumb-2)과 고정 32비트(AArch64·RISC-V) 의 트레이드오프를 실제 기계어 워드로 비교한다.

## 1. 문제

"16비트 명령어에 레지스터 8개면 즉시값은 몇 비트 남나요? 분기는 얼마나 멀리 가나요?" — CPU·가속기·실리콘 검증 면접에서 ISA 감각을 보는 단골 질문이고, 펌웨어에서는 디스어셈블리 읽기, HardFault 때 PC 주소 해석, 링커의 `relocation truncated to fit` 오류가 모두 이 지식 위에 서 있다.

이 레슨은 아주 작은 ISA 하나를 정해 놓고 **텍스트 ↔ 기계어** 를 오가는 도구 세 개(인코더/디코더, 어셈블러, 디스어셈블러)를 만든다. 같은 ISA 를 ARCH-B09 가 단일 사이클 CPU 로 실행한다 — 그래서 아래 인코딩은 한 비트도 바꾸면 안 된다.

**공용 toy ISA** — 16비트 고정 길이, 레지스터 r0–r7 (**r0 는 항상 0**, 쓰기 무시), 16비트 데이터 워드, 하버드 구조 imem[256] / dmem[256] (둘 다 **워드 주소**), PC 는 명령어 번호, 산술은 mod 2^16.

| 명령 | 비트 15 ··· 0 (4 · 3 · 3 · 3 · 3) | 의미 |
|---|---|---|
| ADD | `0000 rd rs rt 000` | rd ← rs + rt |
| ADDI | `0001 rd rs imm6` (부호 있음, −32..31) | rd ← rs + sext(imm6) |
| LD | `0010 rd rs imm6` | rd ← dmem[rs + sext(imm6)] |
| ST | `0011 rt rs imm6` (bits 11..9 = 저장할 데이터) | dmem[rs + sext(imm6)] ← rt |
| BEQ | `0100 rs rt imm6` | rs == rt 이면 PC ← PC + 1 + sext(imm6) |
| JAL | `0101 rd imm9` (−256..255) | rd ← PC + 1; PC ← PC + 1 + sext(imm9) |
| HALT | `1111 0000 0000 0000` | 정지 |

나머지 opcode(0110–1110), ADD 의 bits 2..0 ≠ 0, HALT 의 하위 12비트 ≠ 0 은 **불법 명령어(illegal instruction)** 다.
어셈블리 문법: `ADD r1, r2, r3` · `ADDI r1, r1, -1` · `LD r1, 4(r2)` · `ST r1, 4(r2)` · `BEQ r1, r0, loop` · `JAL r7, func` · `HALT`, `;` 이후는 주석, `label:` 은 혼자 있거나 명령어 앞에. 분기 피연산자는 레이블이거나 **PC+1 기준** 부호 있는 숫자.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">7가지 형식의 비트 배치 (16비트, 한 칸 = 1비트 = 32px)</text>
  <g fill="currentColor" font-size="12" text-anchor="middle" opacity=".7">
    <text x="164" y="42">15..12</text><text x="276" y="42">11..9</text><text x="372" y="42">8..6</text>
    <text x="468" y="42">5..3</text><text x="564" y="42">2..0</text>
  </g>
  <g stroke="currentColor">
    <rect x="100" y="50" width="128" height="28" class="f-mut"/><rect x="228" y="50" width="96" height="28" class="f-acc"/>
    <rect x="324" y="50" width="96" height="28" class="f-mut"/><rect x="420" y="50" width="96" height="28" class="f-mut"/>
    <rect x="516" y="50" width="96" height="28" class="f-mut"/>
    <rect x="100" y="84" width="128" height="28" class="f-mut"/><rect x="228" y="84" width="96" height="28" class="f-acc"/>
    <rect x="324" y="84" width="96" height="28" class="f-mut"/><rect x="420" y="84" width="192" height="28" class="f-ok"/>
    <rect x="100" y="118" width="128" height="28" class="f-mut"/><rect x="228" y="118" width="96" height="28" class="f-acc"/>
    <rect x="324" y="118" width="96" height="28" class="f-mut"/><rect x="420" y="118" width="192" height="28" class="f-ok"/>
    <rect x="100" y="152" width="128" height="28" class="f-mut"/><rect x="228" y="152" width="96" height="28" class="f-warn"/>
    <rect x="324" y="152" width="96" height="28" class="f-mut"/><rect x="420" y="152" width="192" height="28" class="f-ok"/>
    <rect x="100" y="186" width="128" height="28" class="f-mut"/><rect x="228" y="186" width="96" height="28" class="f-warn"/>
    <rect x="324" y="186" width="96" height="28" class="f-mut"/><rect x="420" y="186" width="192" height="28" class="f-ok"/>
    <rect x="100" y="220" width="128" height="28" class="f-mut"/><rect x="228" y="220" width="96" height="28" class="f-acc"/>
    <rect x="324" y="220" width="288" height="28" class="f-ok"/>
    <rect x="100" y="254" width="128" height="28" class="f-mut"/><rect x="228" y="254" width="384" height="28" class="f-mut"/>
  </g>
  <g fill="currentColor" font-weight="bold">
    <text x="10" y="69">ADD</text><text x="10" y="103">ADDI</text><text x="10" y="137">LD</text><text x="10" y="171">ST</text>
    <text x="10" y="205">BEQ</text><text x="10" y="239">JAL</text><text x="10" y="273">HALT</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">
    <text x="164" y="69">0000</text><text x="276" y="69">rd</text><text x="372" y="69">rs</text><text x="468" y="69">rt</text><text x="564" y="69">000</text>
    <text x="164" y="103">0001</text><text x="276" y="103">rd</text><text x="372" y="103">rs</text><text x="516" y="103">imm6</text>
    <text x="164" y="137">0010</text><text x="276" y="137">rd</text><text x="372" y="137">rs</text><text x="516" y="137">imm6</text>
    <text x="164" y="171">0011</text><text x="276" y="171">rt (src)</text><text x="372" y="171">rs</text><text x="516" y="171">imm6</text>
    <text x="164" y="205">0100</text><text x="276" y="205">rs (src)</text><text x="372" y="205">rt</text><text x="516" y="205">imm6 (PC+1 기준)</text>
    <text x="164" y="239">0101</text><text x="276" y="239">rd</text><text x="468" y="239">imm9 (PC+1 기준)</text>
    <text x="164" y="273">1111</text><text x="420" y="273">0000 0000 0000</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="622" y="69">R형</text><text x="622" y="103">I형</text><text x="622" y="137">I형 (load)</text>
    <text x="622" y="171">I형 (store)</text><text x="622" y="205">분기형</text><text x="622" y="239">점프형</text>
    <text x="622" y="273">시스템</text>
  </g>
</svg>
```

색: 진한 칸(rd)은 **쓰는** 레지스터, 경고색 칸은 같은 bits 11..9 인데 **읽는** 레지스터(ST 의 데이터, BEQ 의 비교 대상), 초록 칸은 즉시값. 이 "같은 자리, 다른 역할" 이 5. 풀이의 트레이드오프 이야기의 출발점이다.

### Part A — 인코더 · 디코더 · 디스어셈블러

- `Err encode(const Insn *in, uint16_t *out)` — 의미 필드(`op, rd, rs, rt, imm`)를 비트 자리에 넣는다. 레지스터가 0..7 밖이면 `E_BAD_REG`, 즉시값이 imm6/imm9 범위 밖이면 `E_IMM_RANGE` (**잘라내기 금지**).
- `Err decode(uint16_t w, Insn *out)` — 불법 워드면 `E_ILLEGAL`. 즉시값은 부호 확장(sign extension, ARCH-B01 참고).
- `Err disasm(uint16_t w, char *buf, size_t n)` — 정규형 텍스트. 불법 워드는 objdump 처럼 `.word 0x6000`.
- 검증: 손 계산 인코딩 + **65536 워드 전수**: decode 가 실패하거나, 다시 encode 하면 같은 워드. 합법 워드 수는 정확히 20993 개.

### Part B — 2-pass 어셈블러 (텍스트 → 기계어)

토크나이저(`p_reg`, `p_int`, `p_char`, `p_ident`), 레이블 표, 2-pass 드라이버는 주어진다. 채울 것:

- `p_target` — 분기 피연산자: 레이블이면 `주소 − (pc + 1)`, 숫자면 그대로.
- `parse_operands` — 형식별 피연산자 문법 (`LD rd, imm(rs)` 등), 끝에 남는 글자는 `E_SYNTAX`.
- 검증: 배열 합 프로그램이 기대한 11 워드로, asm → bin → 정규형 텍스트 → bin 왕복, **65536 워드 전부** disasm 텍스트를 다시 어셈블하면 같은 워드, 오류 10종의 원인 + 줄 번호, 그리고 소스를 1–3 글자 망가뜨린 2000개 퍼징(결정적 xorshift32).

### Part C — 펌웨어 이미지 도구: 로더 검사 + 바이너리 패치

- `image_check(img, n, &bad)` — 부트로더가 imem 에 올리기 전에: 모든 워드가 합법인가, 모든 BEQ/JAL 목표가 `[0, n)` 안인가.
- `insert_insn(prog, &n, max, pos, w)` — 명령어 하나를 끼워 넣고 기존 분기들의 PC 상대 오프셋을 고친다. 고친 오프셋이 imm6/imm9 에 안 들어가면 **전체를 거부**(원본 그대로)하고 `E_IMM_RANGE`. 결과가 "소스에 한 줄 넣고 다시 어셈블" 한 것과 **비트 단위로 같아야** 한다.

## 2. Problem (English)

> **Design a 16-bit toy ISA toolchain.**
>
> Eight registers r0–r7 (r0 reads as zero), 16-bit fixed-length instructions, word-addressed Harvard memories, PC counts instructions. Formats: `ADD 0000 rd rs rt 000`, `ADDI/LD 000x/0010 rd rs imm6`, `ST 0011 rt rs imm6`, `BEQ 0100 rs rt imm6` (target = PC+1+imm), `JAL 0101 rd imm9`, `HALT 0xF000`. Everything else is illegal.
>
> 1. Implement `Err encode(const Insn*, uint16_t*)`, `Err decode(uint16_t, Insn*)` and `Err disasm(uint16_t, char*, size_t)`. The encoder must reject (not truncate) out-of-range registers and immediates. Prove `decode` and `encode` are inverse on the legal set by checking all 65536 words.
> 2. Write a two-pass assembler with labels and comments that reports the first error with its line number (bad register, immediate out of range, unknown label, unknown mnemonic, syntax, duplicate label). Show that `assemble(disasm(w)) == w` for every 16-bit word.
> 3. Write a loader check (illegal words, branch targets outside the image) and a binary patcher that inserts one instruction and re-relocates every PC-relative branch, failing atomically if any offset no longer fits.
>
> Example: `ADD r1, r2, r3` → `0x0298`, `ADDI r1, r1, -1` → `0x127F`, `BEQ r1, r0, -3` → `0x423D`, `0x6000` → illegal.
> Follow-ups: why is the immediate only 6 bits? How would you branch farther? Why do real ISAs keep register fields at fixed positions?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # SDK 27.2 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
# 처음엔 FAIL 이 쭉 나오고 "15/64 passed" (이 맥 실측) — TODO 7개를 채워 64/64 를 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — 인코딩은 "op 별로 bits 11..9 / 8..6 에 무엇을 넣나" 표 하나</summary>

ADDI/LD 는 (rd, rs), ST 는 (rt, rs), BEQ 는 (rs, rt). 이 둘만 `a`, `b` 변수에 골라 담으면 네 형식이 같은 한 줄 `op<<12 | a<<9 | b<<6 | (imm & 0x3F)` 로 끝난다. ADD, JAL, HALT 만 따로.

</details>

<details>
<summary>힌트 2 — 음수 즉시값은 반드시 마스크</summary>

`imm = -1` 을 `(unsigned)imm` 하면 `0xFFFFFFFF`. 마스크 없이 OR 하면 opcode 까지 1로 덮인다. `& 0x3F` (imm6), `& 0x1FF` (imm9). 단, 마스크 **전에** `fits_signed(imm, 6)` 로 범위를 확인해야 "잘라내기" 가 아니라 "오류 보고" 가 된다.

</details>

<details>
<summary>힌트 3 — 디코드의 부호 확장과 불법 워드</summary>

`sext(FIELD(w, 5, 0), 6)` — 주어진 `sext` 는 "부호 비트를 뒤집고 빼기" 라 시프트 UB/구현 정의 동작이 없다. 불법: `default:` (opcode 6–14), ADD 의 `FIELD(w,2,0) != 0`, HALT 의 `FIELD(w,11,0) != 0`. 이 둘을 빼먹으면 전수 왕복이 깨진다 — 다시 encode 하면 그 비트가 0이 되니까.

</details>

<details>
<summary>힌트 4 — 레이블은 PC+1 기준, 앞으로 가는 참조는 pass 1 덕분</summary>

pass 1 은 명령어 줄 수만 세면서 `label → pc` 를 등록한다. pass 2 에서 `off = addr − (pc + 1)`. `loop:` 가 3, `BEQ ... loop` 가 8 이면 `3 − 9 = −6`.

</details>

<details>
<summary>힌트 5 — 패치는 "옛 인덱스 → 새 인덱스" 함수 두 개</summary>

명령어 위치: `ni = i < pos ? i : i + 1`. 목표 위치: `nt = t <= pos ? t : t + 1` (pos 를 노리던 분기는 새로 넣은 명령어로 간다 = 소스에서 레이블 바로 뒤에 한 줄 넣은 것). 새 오프셋 `nt − (ni + 1)` 을 임시 배열에 encode 하고, 하나라도 실패하면 원본은 건드리지 않는다.

</details>

## 5. 풀이

### Part A — 손 인코딩과 전수 왕복

배열 합 프로그램(ARCH-B09 가 실행하는 바로 그 모양)을 손으로 인코딩해 보면 형식이 몸에 붙는다. 이 표의 워드가 테스트의 `SUM_BIN` 이다.

| pc | 어셈블리 | 비트 (op · a · b · 나머지) | 워드 |
|---|---|---|---|
| 0 | `ADDI r1, r0, 16` | 0001 · 001 · 000 · 010000 | `0x1210` |
| 1 | `ADDI r2, r0, 5` | 0001 · 010 · 000 · 000101 | `0x1405` |
| 2 | `ADD r3, r0, r0` | 0000 · 011 · 000 · 000 000 | `0x0600` |
| 3 | `loop: BEQ r2, r0, done` | 0100 · 010 · 000 · 000101 (9−4=5) | `0x4405` |
| 4 | `LD r4, 0(r1)` | 0010 · 100 · 001 · 000000 | `0x2840` |
| 5 | `ADD r3, r3, r4` | 0000 · 011 · 011 · 100 000 | `0x06E0` |
| 6 | `ADDI r1, r1, 1` | 0001 · 001 · 001 · 000001 | `0x1241` |
| 7 | `ADDI r2, r2, -1` | 0001 · 010 · 010 · 111111 | `0x14BF` |
| 8 | `BEQ r0, r0, loop` | 0100 · 000 · 000 · 111010 (3−9=−6) | `0x403A` |
| 9 | `done: ST r3, 0(r0)` | 0011 · 011 · 000 · 000000 | `0x3600` |
| 10 | `HALT` | 1111 · 0000 0000 0000 | `0xF000` |

**부호 확장.** imm6 `111010` 은 부호 없는 값으로 58, 6비트 2의 보수로 −6. 주어진 `sext(v, bits)` 는 `(v ^ m) − m` (m = 부호 비트): 부호 비트를 뒤집으면 값이 `+32` 만큼 이동하고(58 → 26), 거기서 32 를 빼면 −6. 시프트로 하는 `((int)(v << 26)) >> 26` 은 음수 int 의 오른쪽 시프트(구현 정의)와 범위 밖 unsigned → int 변환(구현 정의)에 기대므로 이식성이 떨어진다 (ARCH-B01, C-B03 참고).

**왜 전수 왕복이 강한가.** 16비트 ISA 는 워드가 65536 개뿐이라 "모든 입력" 을 다 볼 수 있다. 검사 식은 단 하나: `decode(w) == OK ⇒ encode(decode(w)) == w`. 이것 하나로 잡히는 버그 — ADD 의 bits 2..0 무시(불법 워드를 ADD 로 받아들이면 다시 encode 할 때 그 비트가 0 이 되어 불일치), 부호 확장 누락, ST 의 rt/rs 자리 바뀜(손 인코딩 테스트와 함께), imm9 를 6비트로 마스크. 합법 워드 수도 산수로 맞춰 본다.

| opcode | 자유 비트 | 합법 워드 |
|---|---|---|
| ADD | rd·rs·rt 9비트 (bits 2..0 = 0 고정) | 512 |
| ADDI, LD, ST, BEQ | 12비트 각각 | 4 × 4096 = 16384 |
| JAL | 12비트 | 4096 |
| HALT | 0비트 | 1 |
| **합계** | | **20993** (65536 의 32.03 %) |

불법 44543 개 = 빈 opcode 9개 × 4096 (36864) + ADD funct ≠ 0 (3584) + HALT 하위 ≠ 0 (4095). 인코딩 공간의 2/3 가 비어 있다는 것은 **확장 여지** (추가 연습 4 의 JALR) 이기도 하고, 반대로 "16비트에 더 넣을 수 있었는데 imm 을 아꼈다" 는 뜻이기도 하다.

**불법 워드 정책.** `0x0000` 은 `ADD r0, r0, r0` 로 합법 = NOP 이다. `0xFFFF` 는 불법. RISC-V 는 일부러 **all-zero 16비트와 all-one 워드를 불법** 으로 정해 두었다 — 0 으로 채워진 메모리나 지워진 플래시로 PC 가 튀면 NOP 미끄럼틀을 타지 않고 즉시 트랩이 나게 하려는 것. 우리 ISA 는 0x0000 = NOP 라서 그 안전장치가 반쪽이다 (8. Q&A Q5).

### Part B — 2-pass 어셈블러

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b08-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">2-pass: pass 1 은 레이블 주소만, pass 2 가 인코딩 (앞쪽 참조 해결)</text>
  <rect x="10" y="36" width="220" height="250" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="58">pc 소스 줄</text>
    <text x="20" y="80">0  ADDI r1, r0, 16</text>
    <text x="20" y="100">1  ADDI r2, r0, 5</text>
    <text x="20" y="120">2  ADD  r3, r0, r0</text>
    <text x="20" y="140">3  loop: BEQ r2,r0,done</text>
    <text x="20" y="160">4  LD   r4, 0(r1)</text>
    <text x="20" y="180">5  ADD  r3, r3, r4</text>
    <text x="20" y="200">6  ADDI r1, r1, 1</text>
    <text x="20" y="220">7  ADDI r2, r2, -1</text>
    <text x="20" y="240">8  BEQ  r0,r0,loop</text>
    <text x="20" y="260">9  done: ST r3, 0(r0)</text>
    <text x="20" y="280">10 HALT</text>
  </g>
  <rect x="270" y="36" width="190" height="96" rx="8" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="282" y="58" font-weight="bold">pass 1: 레이블 표</text>
    <text x="282" y="84" font-family="monospace">loop → 3</text>
    <text x="282" y="106" font-family="monospace">done → 9</text>
    <text x="282" y="126" font-size="12">명령어 줄마다 pc++ 만</text>
  </g>
  <path d="M232 140 L268 100" stroke="currentColor" fill="none" marker-end="url(#b08-arr)"/>
  <rect x="270" y="150" width="460" height="136" rx="8" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="282" y="172" font-weight="bold">pass 2: off = 레이블 주소 − (pc + 1)</text>
    <text x="282" y="196" font-family="monospace" font-size="12">pc 3: BEQ r2,r0,done → 9 − 4 = +5 → 0x4405</text>
    <text x="282" y="218" font-family="monospace" font-size="12">pc 8: BEQ r0,r0,loop → 3 − 9 = −6 → 0x403A</text>
    <text x="282" y="242" font-size="12">PC+1 기준인 이유: 하드웨어가 fetch 하면서 이미 PC+1 을 만들어 둔다</text>
    <text x="282" y="262" font-size="12">(Thumb 는 PC+4 바이트, AArch64·RISC-V 는 분기 명령 자신의 주소 기준)</text>
    <text x="282" y="280" font-size="12">범위 검사는 encode 한 곳에서: off = 32 → E_IMM_RANGE</text>
  </g>
  <path d="M232 240 L268 220" stroke="currentColor" fill="none" marker-end="url(#b08-arr)"/>
</svg>
```

**왜 2-pass 인가.** `BEQ r2, r0, done` 을 만날 때 `done` 은 아직 안 나왔다(전방 참조, forward reference). 1-pass 로 하려면 빈칸을 남기고 "나중에 고칠 목록(fix-up list)" 을 들고 다녀야 한다 — 그게 바로 Part C 의 relocation 과 같은 일이다. 고정 길이 ISA 라서 pass 1 이 쉽다: 명령어 줄 하나 = 워드 하나. 가변 길이(x86, Thumb-2 의 16/32비트 선택)라면 "오프셋이 작으면 짧은 형식" 을 고르는 순간 뒤의 주소가 모두 바뀌어서 **수렴할 때까지 반복** 하는 branch relaxation 이 필요하다.

**문법 구현.** 주어진 토큰 파서 + `TRY()` 매크로로 각 형식이 한 줄씩이다.

```c
case OP_LD: case OP_ST:
    TRY(p_reg(c, o->op == OP_LD ? &o->rd : &o->rt)); TRY(p_char(c, ','));
    TRY(p_int(c, &v)); o->imm = (int)v;
    TRY(p_char(c, '(')); TRY(p_reg(c, &o->rs)); TRY(p_char(c, ')'));
```

모든 형식이 마지막에 `p_end` 로 "남은 글자 없음" 을 확인한다 — 그래야 `HALT r1` 같은 줄이 조용히 HALT 로 통과하지 않는다. 범위 검사는 파서가 아니라 `encode` 한 곳에서만 해서, 레이블에서 온 오프셋이든 숫자든 같은 규칙을 받는다.

**오류 보고표 (테스트에서 확인한 것).**

| 입력 | 오류 | 줄 |
|---|---|---|
| `HALT` / `ADD r1, r2, r8` | `E_BAD_REG` | 2 |
| `ADD r1, r2, 5` | `E_BAD_REG` (레지스터 자리에 숫자) | 1 |
| `ADDI r1, r1, 32` | `E_IMM_RANGE` (imm6 최대 31) | 1 |
| 빈 줄 2개 + `BEQ r1, r0, nowhere` | `E_UNKNOWN_LABEL` | 3 |
| `FOO r1` | `E_UNKNOWN_MNEMONIC` | 1 |
| `LD r1, 4 r2` · `ADD r1, r2` · `HALT r1` | `E_SYNTAX` | 1 |
| `a: HALT` / `a: HALT` | `E_DUP_LABEL` | 2 |
| `.word 0x10000` | `E_IMM_RANGE` | 1 |
| `BEQ r0, r0, far` + HALT 32개 + `far:` | `E_IMM_RANGE` (오프셋 32) | 1 |

**`.word` 와 전수 텍스트 왕복.** 디스어셈블러가 불법 워드를 `.word 0x6000` 으로 찍고 어셈블러가 `.word` 를 받으므로, 65536 워드 **전부** `assemble(disasm(w)) == w` 가 성립한다 — objdump 출력이 다시 어셈블 가능한 것과 같은 성질이다.

**퍼징.** `SUM_SRC` 의 아무 자리 1–3 글자를 `r0123456789,()-+:;xADEHILJQSTW .\t` 중 하나로 바꾼 2000개를 어셈블했다. 불변식: 실패면 원인 ≠ OK 이고 줄 번호가 1..12, 성공이면 나온 모든 워드가 다시 왕복된다. 측정용으로 카운터를 찍어 본 실제 분포:

```text
fuzz ok=329 err=1671 broken=0
```

(같은 시드라 매번 같다. ASan/UBSan 빌드에서도 깨끗 — 버퍼 넘침이나 부호 오버플로가 있었다면 여기서 터진다.)

### Part C — 로더 검사와 바이너리 패치

`image_check` 는 단순하다: 모든 워드 decode, BEQ/JAL 이면 `t = i + 1 + imm` 이 `[0, n)` 인지. 테스트는 세 가지를 본다 — 5번 워드를 `0x7000` 으로 오염(→ `E_ILLEGAL`, bad = 5), 이미지를 9 워드로 잘라 `done`(9) 이 사라짐(→ pc 3 의 BEQ 가 `E_TARGET_RANGE`), JAL 목표가 정확히 n(끝 바로 다음, 흔한 off-by-one). 실제 부트로더라면 여기에 CRC/서명 검사, "마지막 명령어 뒤로 흘러 나가지 않나" 검사가 더해진다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b08-arr2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">pos = 3 에 ADDI r5, r5, 1 을 끼우면 — 분기 두 개의 오프셋은?</text>
  <text x="10" y="82" fill="currentColor">이전</text>
  <text x="10" y="202" fill="currentColor">이후</text>
  <g stroke="currentColor" class="f-mut">
    <rect x="70" y="64" width="50" height="28"/><rect x="120" y="64" width="50" height="28"/><rect x="170" y="64" width="50" height="28"/><rect x="220" y="64" width="50" height="28"/><rect x="270" y="64" width="50" height="28"/><rect x="320" y="64" width="50" height="28"/><rect x="370" y="64" width="50" height="28"/><rect x="420" y="64" width="50" height="28"/><rect x="470" y="64" width="50" height="28"/><rect x="520" y="64" width="50" height="28"/><rect x="570" y="64" width="50" height="28"/>
    <rect x="70" y="184" width="50" height="28"/><rect x="120" y="184" width="50" height="28"/><rect x="170" y="184" width="50" height="28"/><rect x="270" y="184" width="50" height="28"/><rect x="320" y="184" width="50" height="28"/><rect x="370" y="184" width="50" height="28"/><rect x="420" y="184" width="50" height="28"/><rect x="470" y="184" width="50" height="28"/><rect x="520" y="184" width="50" height="28"/><rect x="570" y="184" width="50" height="28"/><rect x="620" y="184" width="50" height="28"/>
  </g>
  <rect x="220" y="184" width="50" height="28" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="95" y="83">0</text><text x="145" y="83">1</text><text x="195" y="83">2</text><text x="245" y="83">3 BEQ</text><text x="295" y="83">4</text><text x="345" y="83">5</text><text x="395" y="83">6</text><text x="445" y="83">7</text><text x="495" y="83">8 BEQ</text><text x="545" y="83">9 ST</text><text x="595" y="83">10</text>
    <text x="95" y="203">0</text><text x="145" y="203">1</text><text x="195" y="203">2</text><text x="245" y="203">3 NEW</text><text x="295" y="203">4 BEQ</text><text x="345" y="203">5</text><text x="395" y="203">6</text><text x="445" y="203">7</text><text x="495" y="203">8</text><text x="545" y="203">9 BEQ</text><text x="595" y="203">10 ST</text><text x="645" y="203">11</text>
  </g>
  <path d="M250 64 C 330 30, 470 30, 540 62" stroke="currentColor" fill="none" marker-end="url(#b08-arr2)"/>
  <path d="M495 92 C 440 130, 300 130, 250 94" stroke="currentColor" fill="none" stroke-dasharray="5 3" marker-end="url(#b08-arr2)"/>
  <path d="M545 184 C 470 148, 300 148, 248 182" stroke="currentColor" fill="none" stroke-dasharray="5 3" marker-end="url(#b08-arr2)"/>
  <path d="M300 212 C 380 256, 520 256, 590 214" stroke="currentColor" fill="none" marker-end="url(#b08-arr2)"/>
  <g fill="currentColor" font-size="12">
    <text x="370" y="54">+5 (3→9 done)</text>
    <text x="320" y="140">−6 (8→3 loop)</text>
    <text x="320" y="153">−7 (분기만 밀림, 목표 3 = 새 명령어)</text>
    <text x="380" y="268">+5 그대로 (분기·목표 둘 다 밀림)</text>
    <text x="10" y="290">규칙: 위치 i → i &lt; pos ? i : i+1, 목표 t → t ≤ pos ? t : t+1, 새 오프셋 = nt − (ni + 1)</text>
  </g>
</svg>
```

결과는 `SUM_PATCHED_SRC` (소스에서 `loop:` 바로 뒤에 `ADDI r5, r5, 1` 한 줄을 넣은 것) 를 어셈블한 12 워드와 `memcmp` 로 같다 — **패치 도구 = 소스 수정 + 재어셈블** 이라는 가장 강한 기준으로 검사했다. 끼우는 자리가 모든 분기 *앞* (pos 0) 이면 분기와 목표가 함께 밀려 오프셋이 그대로인 것도 확인한다.

**원자적 실패.** 오프셋 +31 짜리 분기(0 → 32)를 가로질러 pos 5 에 끼우면 새 오프셋이 +32 → imm6 초과. 이때 일부만 고친 채로 두면 이미지가 반쯤 깨진 상태가 된다. 그래서 임시 배열에서 모두 성공했을 때만 `memcpy` 한다. 테스트는 실패 후 `n == 33` 이고 원본과 `memcmp` 같음을 확인한다. 실제 툴체인에서는 이것이 링커의 `relocation truncated to fit: R_ARM_THM_JUMP11` 류 오류이고, 해결은 (1) 긴 형식으로 바꾸기(relaxation, 추가 연습 2), (2) 링커가 중간에 점프 섬(veneer/trampoline)을 넣기, (3) 코드를 가깝게 배치하기.

### 인코딩 트레이드오프 — 실제 ISA 와 비교

**imm 폭.** 16비트에서 opcode 4 + 레지스터 2개 × 3 = 10비트를 쓰면 6비트가 남는다. 레지스터를 16개(4비트)로 늘리면 imm 은 4비트(−8..7)로 줄어 거의 쓸모가 없다. 레지스터 수 ↔ 즉시값 폭 ↔ 형식 수가 16비트 안에서 서로 빼앗는 관계 — 그래서 Thumb-1 과 RISC-V C 확장이 대부분의 16비트 명령에서 **레지스터 8개만**(3비트 필드) 쓴다 (Thumb: r0–r7, RVC: x8–x15).

**레지스터 필드 위치.** 우리 ISA 는 bits 8..6 이 항상 "읽는 레지스터" 지만, bits 11..9 는 ADD/ADDI/LD/JAL 에서는 *쓰는* rd, ST/BEQ 에서는 *읽는* 레지스터다. 하드웨어로 보면 레지스터 파일의 두 번째 읽기 포트 주소에 `opcode ∈ {ST, BEQ} ? bits 11..9 : bits 5..3` 같은 MUX 가 필요하고, 그 MUX 가 opcode 디코드를 기다린다. RISC-V 는 rs1 (19..15), rs2 (24..20), rd (11..7) 위치를 **모든 형식에서 고정** 하고, 대신 store 의 즉시값을 imm[11:5] / imm[4:0] 두 조각으로 쪼갰다 — 디코드를 기다리지 않고 바로 레지스터를 읽기 시작할 수 있도록. 디스어셈블러를 짜 보면 즉시값 조각 맞추기는 소프트웨어 쪽 비용이라는 게 보인다.

**실제 기계어 (이 맥, Apple M6에서 실측).** 같은 모양의 C 를 Cortex-M4 Thumb-2 와 AArch64 로 컴파일해 objdump 한 결과 (`demo_real_encoding.c`, 다듬음):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O1 -c demo_real_encoding.c -o /tmp/esn_ARCH-B08/t.o
xcrun llvm-objdump -d /tmp/esn_ARCH-B08/t.o
cc -O1 -c demo_real_encoding.c -o /tmp/esn_ARCH-B08/a.o && xcrun llvm-objdump -d /tmp/esn_ARCH-B08/a.o
```

```text
/tmp/esn_ARCH-B08/t.o:	file format elf32-littlearm
00000000 <add3>:
       0: 4408         	add	r0, r1
00000004 <addi>:
       4: 3801         	subs	r0, #0x1
00000008 <load4>:
       8: 6900         	ldr	r0, [r0, #0x10]
0000000c <sum>:
       c: b141         	cbz	r1, 0x20 <sum+0x14>     @ imm = #0x10
      ...
      14: f852 3f04    	ldr	r3, [r2, #4]!
      18: 3901         	subs	r1, #0x1
      1a: 4418         	add	r0, r3
      1c: d1fa         	bne	0x14 <sum+0x8>          @ imm = #-0xc

/tmp/esn_ARCH-B08/a.o:	file format mach-o arm64
0000000000000000 <ltmp0>:
       0: 0b000020     	add	w0, w1, w0
0000000000000008 <_addi>:
       8: 51000400     	sub	w0, w0, #0x1
0000000000000010 <_load4>:
      10: b9401000     	ldr	w0, [x0, #0x10]
0000000000000018 <_sum>:
      ...
      20: b8404409     	ldr	w9, [x0], #0x4
      24: 0b080128     	add	w8, w9, w8
      28: 71000421     	subs	w1, w1, #0x1
      2c: 54ffffa1     	b.ne	0x20 <_sum+0x8>
```

읽는 법:
- Thumb `add r0, r1` (`0x4408`) 은 **2-주소** 형식(rdn ← rdn + rm) — 16비트에 3-주소를 다 넣으면 비트가 모자라서 목적지와 소스 하나를 겸한다. `subs r0, #1` (`0x3801`) 도 2-주소 + imm8. 우리 ISA 는 3-주소를 고집한 대가로 imm 이 6비트다.
- `ldr r0, [r0, #0x10]` (`0x6900`) 의 imm5 는 4 이고 하드웨어가 ×4 한다 (워드 정렬 load 라 하위 2비트는 늘 0 → 버린다). 우리 ISA 는 메모리가 워드 주소라 처음부터 같은 효과다.
- `bne` (`0xd1fa`) = `1101 cond(0001=NE) imm8(0xFA=−6)`, 단위가 halfword 라 −12 바이트, 기준은 PC+4: 0x1c + 4 − 12 = 0x14. 조건 분기 imm8 → ±256 B 정도밖에 못 간다.
- `ldr r3, [r2, #4]!` 는 첫 halfword `0xf852` 의 상위 5비트가 `11111` 이라 **32비트 Thumb-2 명령**이다 (ARMv7-M: 상위 5비트가 `11101`/`11110`/`11111` 이면 32비트의 첫 halfword). 이게 16/32 혼합 ISA 의 디코더가 매 halfword 마다 하는 첫 질문이다.
- AArch64 는 전부 32비트 고정. `b.ne` (`0x54ffffa1`) 의 imm19 는 워드 단위라 ±1 MiB 를 가고, 기준은 분기 자신의 주소다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so          # 64/64 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 64/64 passed, 경고 없음
```

이 맥에서 `time ./so` 는 0.016 s (65536 워드 두 번 전수 + 퍼징 2000 개 포함) — 전수 테스트가 비싸지 않다는 것이 16비트 ISA 의 장점이다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `w = op<<12 \| rd<<9 \| rs<<6 \| imm;` | imm = −1 이면 `0xFFFFFFFF` 가 OR 되어 opcode·레지스터 전부 1 | `\| ((unsigned)imm & 0x3Fu)` |
| 범위 검사 없이 `& 0x3F` 만 | `ADDI r1, r1, 32` 가 조용히 −32 가 됨 (잘라내기) | `if (!fits_signed(imm, 6)) return E_IMM_RANGE;` 를 마스크 **전에** |
| decode 에서 `imm = w & 0x3F` | 부호 확장 누락: −3 이 61 로 | `imm = sext(FIELD(w,5,0), 6)` |
| ADD 의 bits 2..0, HALT 의 하위 비트 무시 | 불법 워드를 합법으로 받음 → 재인코딩 불일치, 전수 테스트에서 3584 + 4095 건 실패 | `if (FIELD(w,2,0)) return E_ILLEGAL;` |
| 분기 오프셋을 `target − pc` 로 | 하드웨어 규칙은 PC+1 기준 → 모든 분기가 한 칸 밀림 | `target − (pc + 1)` |
| ST 를 `rd, rs` 로 인코딩하며 rd 자리에 base 를 넣음 | ST 의 11..9 는 **데이터(rt)**, 8..6 이 주소 base | `a = rt; b = rs;` |
| 1-pass 어셈블러 | 전방 레이블 `done` 을 모름 | 2-pass 또는 fix-up 목록 |
| `HALT r1` 을 통과 | 피연산자 뒤 남은 글자 검사 누락 | 모든 형식 끝에 `p_end` |
| 패치 중 분기를 제자리에서 고침 | 중간에 범위 오류 나면 반쯤 고친 이미지가 남음 | 임시 배열 → 전부 성공 시 커밋 |

## 8. 면접 Q&A

<details>
<summary>Q1. 고정 길이 명령어와 가변 길이 명령어의 장단점은?</summary>

고정 길이: 다음 PC = PC + 상수, 명령어 경계가 자명 → fetch/decode 가 단순하고 여러 개를 병렬로 디코드하기 쉽다 (AArch64, RISC-V 기본 32비트). 단점은 코드 밀도 — 마이크로컨트롤러에서 플래시 크기 = 돈이다. 가변 길이: 밀도가 좋지만 경계를 찾는 디코드가 순차적이다 (x86 은 1–15 바이트).
꼬리질문: "Cortex-M 은?" → Thumb-2 는 16/32비트 혼합. 첫 halfword 상위 5비트가 `11101`/`11110`/`11111` 이면 32비트 명령. 자주 쓰는 연산은 16비트로 밀도를 얻고, 큰 즉시값·먼 분기는 32비트로. RISC-V 도 C 확장으로 같은 선택을 한다(하위 2비트 `11` 이 아니면 16비트).

</details>

<details>
<summary>Q2. 왜 실제 ISA 는 레지스터 필드 위치를 형식마다 고정하나요? 이 toy ISA 는?</summary>

레지스터 파일 읽기를 opcode 디코드와 **동시에** 시작하려면 읽기 포트 주소가 늘 같은 비트에서 와야 한다. RISC-V 는 rs1/rs2/rd 를 고정하고 store/branch 의 즉시값을 조각내서 이걸 지켰다. 이 toy ISA 는 bits 11..9 가 ADD 류에선 rd, ST/BEQ 에선 소스라서 두 번째 읽기 포트 앞에 opcode 로 제어되는 MUX 가 필요하다 (단일 사이클 ARCH-B09 에서는 문제가 없지만 파이프라인에선 디코드 단계가 길어진다).
꼬리질문: "즉시값을 조각내면 누가 손해?" → 디스어셈블러·어셈블러·사람. 하드웨어에서는 배선만 바뀔 뿐이고, RISC-V 는 부호 비트를 항상 bit 31 에 두어 부호 확장도 디코드 전에 시작한다.

</details>

<details>
<summary>Q3. 분기 오프셋이 즉시값 범위를 넘으면 어떻게 하나요? "relocation truncated to fit" 은 무슨 뜻인가요?</summary>

링커가 레이블 주소를 확정한 뒤 PC 상대 오프셋을 명령어 필드에 넣으려는데 필드 폭에 안 들어간다는 뜻이다. 해결: (1) 긴 형식으로 교체 — 조건을 뒤집은 짧은 분기 + 무조건 긴 점프 (추가 연습 2 의 3워드 시퀀스; 이 ISA 는 BNE 가 없어 한 개 더 든다), (2) 링커가 근처에 veneer(점프 섬)를 넣음 — ARM 링커가 BL 범위 밖 호출에 쓰는 방법, (3) 섹션 배치를 바꿔 가깝게.
꼬리질문: "Cortex-M 범위는?" → 16비트 조건 분기 imm8 → 약 ±256 B, 16비트 무조건 B imm11 → 약 ±2 KB, 32비트 B.W/BL → 약 ±16 MB. 플래시가 그보다 크거나 RAM 함수(0x2000_0000)를 플래시(0x0800_0000)에서 BL 로 부르면 veneer 가 생긴다.

</details>

<details>
<summary>Q4. 16비트(또는 32비트) 상수를 레지스터에 어떻게 넣나요?</summary>

이 ISA 는 imm6 뿐이라 ADD(×2)와 ADDI 를 섞어 비트 덩어리로 만든다 — 최악 19 명령 (추가 연습 3, 65536 값 전수 검증). 실제 ISA: RISC-V `LUI` (상위 20비트) + `ADDI` (하위 12비트, 부호 확장 때문에 상위에 +1 보정), ARMv7-M `MOVW` + `MOVT` (16비트씩), AArch64 `MOVZ` + `MOVK` ×3, 그리고 Thumb 의 literal pool — `LDR r0, =0x40021000` 은 코드 근처에 상수를 두고 PC 상대 load 한다.
꼬리질문: "literal pool 이 디스어셈블러에 왜 골치인가?" → 코드 사이에 데이터가 끼어 있어서 선형으로 디코드하면 상수를 명령어로 읽는다 (Q6).

</details>

<details>
<summary>Q5. CPU 가 불법 명령어를 만나면? 지워진 플래시로 점프하면 어떻게 되나요?</summary>

정의되지 않은 인코딩은 예외(트랩)를 낸다 — Cortex-M 에서는 UsageFault(UNDEFINSTR, CFSR 의 UFSR 부분)이고, UsageFault 가 활성화되어 있지 않으면 HardFault 로 승격된다. 핸들러에서 스택에 쌓인 PC 를 보고 그 주소의 워드를 디스어셈블하는 것이 첫 디버깅 단계다.
지워진 NOR 플래시는 보통 모두 1 이고, 그 워드가 무엇으로 디코드되는지는 ISA·코어마다 다르니 "항상 X fault" 라고 단정하지 말고 그 칩에서 확인해야 한다. ISA 설계 관점의 교훈: RISC-V 는 all-zero 와 all-one 워드를 일부러 불법으로 정해 빈 메모리 실행이 바로 트랩되게 했다. 이 toy ISA 는 `0xFFFF` 는 불법이지만 `0x0000` 은 NOP(`ADD r0, r0, r0`) 라 0 으로 채워진 RAM 에서는 미끄러져 내려간다.
꼬리질문: "NOP 를 따로 정의할 필요가 있나?" → r0 쓰기 무시 덕분에 `ADD r0, r0, r0` 가 공짜 NOP. RISC-V 의 NOP 도 `ADDI x0, x0, 0` 이다.

</details>

<details>
<summary>Q6. 디스어셈블러는 코드와 데이터를 어떻게 구분하나요?</summary>

선형 스윕(linear sweep, objdump 기본)은 섹션을 처음부터 끝까지 디코드한다 — literal pool, 점프 테이블 같은 데이터를 명령어로 읽거나, 가변 길이 ISA 에서 경계가 어긋난다. 재귀 순회(recursive traversal)는 진입점에서 분기를 따라가며 도달 가능한 곳만 디코드한다. ARM ELF 에는 매핑 심볼(`$a`/`$t`/`$d`)이 있어 ARM/Thumb/데이터 구간을 알려 준다. 이 레슨의 `.word` 출력이 "디코드 못 함 = 데이터일 수 있음" 의 가장 단순한 표현이다.
꼬리질문: "HardFault 덤프의 PC 근처를 디스어셈블할 때 주의점?" → Thumb 이면 halfword 정렬부터 시작해야 하고, 32비트 명령 중간에서 시작하면 앞 몇 개가 쓰레기로 나온다.

</details>

<details>
<summary>Q7. 이 ISA 로 함수 호출에서 돌아올 수 있나요?</summary>

없다. `JAL r7, func` 로 복귀 주소를 r7 에 넣을 수는 있지만, **레지스터 값으로 점프** 하는 명령(JALR / BX lr / RET)이 없어서 돌아갈 방법이 없다. 그래서 ARCH-B09 의 프로그램은 함수 복귀가 필요 없게 짠다. 실제 ISA 는 반드시 간접 점프가 있다 — 함수 복귀, 함수 포인터, switch 점프 테이블, 인터럽트 벡터 디스패치에 필요하기 때문. 빈 opcode `0110` 에 JALR 을 넣는 확장이 추가 연습 4.
꼬리질문: "Cortex-M 의 예외 복귀는?" → `BX lr` 인데 lr 에 EXC_RETURN 이라는 특수 값(0xFFFFFFxx 꼴)이 들어 있어서 하드웨어가 "일반 점프가 아니라 예외 복귀" 로 알아본다.

</details>

## 9. 추가 연습

### 연습 1 — 인코딩 공간 회계

opcode 별 합법 워드 수, 빈 opcode 수, 전체 사용률을 전수로 세라.

<details>
<summary>답 보기</summary>

ADD 512, ADDI/LD/ST/BEQ/JAL 각 4096, HALT 1 → 20993 (32.03 %). 빈 opcode 9개(0110–1110). 불법 44543 = 36864 + 3584 + 4095. 설계 회고: ADD 의 funct 3비트(2..0)는 SUB/AND/OR/XOR/SLT 같은 ALU 연산 7개를 더 넣을 자리로 남겨 둔 것 — RISC-V 의 funct3/funct7 과 같은 발상이다.

</details>

### 연습 2 — 먼 BEQ 늘이기 (branch relaxation)

`emit_beq_far(pc, rs, rt, target, out[3])`: 오프셋이 imm6 에 들어가면 1워드, 아니면 JAL 을 써서 3워드 시퀀스로. JAL 로도 안 되면 −1. 제어 흐름만 보는 `next_pc` 로 같을 때/다를 때 도착 PC 를 확인한다.

<details>
<summary>답 보기</summary>

```text
pc  : BEQ rs, rt, +1    ; 같으면 pc+2 로
pc+1: BEQ r0, r0, +1    ; 다르면 JAL 을 건너뛰어 pc+3
pc+2: JAL r0, target    ; offset = target − (pc+3), imm9 ±256
```

BNE 가 있었다면 `BNE rs, rt, +1; JAL r0, target` 2워드로 끝난다 — 조건 반전 분기가 없는 ISA 의 비용. pc 10 → 200 (같으면 200, 다르면 13), pc 250 → 3 (뒤로), pc 0 → 300 은 −1.

</details>

### 연습 3 — LUI 없이 16비트 상수 만들기

`li_seq(rd, value, out)` 가 ADDI/ADD 만으로 rd 에 임의의 16비트 값을 만든다. 65536 값 전부를 미니 실행기로 검증하고, 최대 길이와 `0x1234` 의 길이를 구하라.

<details>
<summary>답 보기</summary>

−32..31 이면 `ADDI rd, r0, s` 하나. 아니면 1 + 5 + 5 + 5 비트 덩어리로 나눠, 첫 0 아닌 덩어리를 ADDI 로 넣고 이후 덩어리마다 `ADD rd, rd, rd` ×5 (= 왼쪽 시프트 5) 후 `ADDI rd, rd, 덩어리` (0 이면 생략). 덩어리가 0..31 이라 imm6 에 늘 들어간다. 최대 19 명령, `0x1234` = 4 · 17 · 20 → 13 명령, 32 → 6 명령. RISC-V 는 `LUI + ADDI` 2개, ARMv7-M 은 `MOVW` 1개 — "상수 만들기 전용 명령" 의 가치가 숫자로 보인다.

</details>

### 연습 4 — 빈 opcode 에 JALR 확장

`JALR rd, rs` = `0110 rd rs 000000` (rd ← PC+1; PC ← rs). 인코더와 확장 디코더를 작성하고, 기본 디코더는 여전히 이 워드를 불법으로 보는지 확인하라. (공용 ISA 가 아니다 — ARCH-B09 는 쓰지 않는다.)

<details>
<summary>답 보기</summary>

`JALR r0, r7` = `0x61C0` = 함수 복귀. 하위 6비트 ≠ 0 이면 불법(`0x61C1`). 확장 디코더는 opcode 6 만 가로채고 나머지는 기본 decode 로 넘긴다 — 기존 바이너리의 의미가 하나도 안 바뀌는 "후방 호환 확장" 이다. RISC-V 의 JALR 은 rs1 + imm12 로 점프해서 점프 테이블과 먼 호출(`AUIPC + JALR`)까지 하나로 처리한다.

</details>

### 연습 5 — Verilog `$readmemh` 이미지

어셈블 결과를 RTL 테스트벤치의 `$readmemh("imem.hex", imem)` 가 읽는 형식(한 줄에 16진 4자리)으로 쓰고, 다시 읽어라. `//` 주석과 빈 줄은 건너뛰고, 16진 아닌 글자나 4자리 초과는 오류.

<details>
<summary>답 보기</summary>

`snprintf("%04x\n")` 로 11 워드 → 55 바이트(`1210\n1405\n0600\n...`), 파서는 줄마다 공백을 걷고 16진 숫자 1–4 자리만 받는다. 실리콘 검증에서는 같은 이미지를 C 모델(ARCH-B09)과 RTL 시뮬레이션에 똑같이 먹이고 최종 레지스터/메모리를 비교한다 — 어셈블러가 그 흐름의 입구다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   # 20/20 passed (ASan/UBSan 빌드도 20/20, 경고 없음)
```

## 10. 핵심 정리

- 16비트 = opcode 4 + 레지스터 3×2 + imm6. 레지스터 수 ↔ 즉시값 폭 ↔ 형식 수가 서로 비트를 빼앗는다 (Thumb-1·RVC 도 16비트에선 레지스터 8개).
- 인코딩: 범위 검사 → `& 0x3F` 마스크 → 자리 시프트. **잘라내지 말고 보고**. 디코딩: 필드 추출 → 부호 확장 → 불법 워드 거르기.
- 16비트 ISA 는 65536 워드 전수 왕복으로 인코더/디코더/디스어셈블러/어셈블러를 증명할 수 있다 (합법 20993 개).
- 분기 오프셋은 ISA 가 정한 기준(여기 PC+1, Thumb PC+4 바이트, AArch64·RISC-V 분기 자신)에서 잰다. off-by-one 의 1순위.
- 어셈블러는 2-pass (또는 fix-up 목록) 로 전방 참조를 푼다. 패치/링크 때는 PC 상대 오프셋을 다시 계산하고, 안 들어가면 relaxation·veneer.
- 레지스터 필드 위치 고정(RISC-V)은 디코드 전에 레지스터 읽기를 시작하려는 하드웨어 최적화 — 대가는 조각난 즉시값.
- 불법 인코딩 정책은 안전장치: all-zero/all-one 을 불법으로 두면 빈 메모리 실행이 즉시 트랩된다. 간접 점프(JALR) 없이는 함수 복귀가 불가능하다.

## 참고자료

- Harris & Harris, *Digital Design and Computer Architecture, ARM Edition* — 6장 명령어 형식과 인코딩, 7장 단일 사이클 프로세서 (ARCH-B09 로 이어짐)
- Patterson & Hennessy, *Computer Organization and Design* — 2장 "Representing Instructions in the Computer", 어셈블러·링커 설명
- [The RISC-V Instruction Set Manual, Volume I: Unprivileged ISA](https://riscv.org/technical/specifications/) — 2.2/2.3 기본 형식과 즉시값 배치, 16비트 C 확장, all-zero 불법 인코딩
- [Arm v7-M Architecture Reference Manual (DDI 0403)](https://developer.arm.com/documentation/ddi0403/latest) — A5 Thumb 명령어 인코딩, 16/32비트 판별
- [Arm Cortex-M4 Devices Generic User Guide — Fault handling (UsageFault/HardFault)](https://developer.arm.com/documentation/dui0553/latest)
- [LLVM `llvm-objdump` 문서](https://llvm.org/docs/CommandGuide/llvm-objdump.html) — 이 레슨의 실측 디스어셈블에 사용
- 관련 레슨: ARCH-B01 (부호 확장), ARCH-B02 (플래그 기반 조건 분기와의 비교), ARCH-B09 (이 ISA 를 실행하는 CPU), C-B01 (비트 필드)
