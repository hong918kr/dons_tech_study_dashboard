# Lesson writer brief — track ARCH (컴퓨터 구조), **Basic** level (ARCH-B01–ARCH-B14)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering: ROOT/NOTES_SPEC.md (lesson rules) → ROOT/curriculum/LESSON_WRITER_BRIEF.md (base rules: output files,
headings verbatim and in order, svg rules, strict build, "never invent output") → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md
(deterministic PRNG tests, ASan/UBSan, demo_*.c, cross-target asm recipe, POSIX define) → **this file** (ARCH-B overrides).
Read all of them.

## Who / why
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math, targeting firmware /
silicon-validation / AI-accelerator roles (Rivian silicon validation, NVIDIA, Apple, Anduril). ARCH Basic re-checks the
computer-architecture fundamentals that interviewers probe ("왜 C 플래그는 뺄셈에서 borrow의 반대인가?", "FP16 반올림은?",
"setup slack 음수면?"). Every lesson = one interview-style coding problem, built as a small C model/simulator.

## Track context
- ROOT/curriculum/tracks/ARCH.json — find your item (title/goal/task/tags). The lesson MUST do the item's `task`
  (all parts of it: e.g. "전수(65536쌍) 테스트", "LFSR 주기 65535 검증", "민감도 표", "리셋 시나리오 시퀀스").
- The other B items ARCH-B01..B14 are being written IN PARALLEL by other agents right now. Stay in your lane;
  cross-reference by id ("(ARCH-B02 참고)") instead of re-teaching. Ownership:
  B01 radix/2's complement/sign_extend · B02 NZCV flags + condition codes · B03 IEEE 754 float32/FP16 ·
  B04 gate-level adders (ripple vs CLA) · B05 MUX/decoder/priority encoder (=CLZ) · B06 DFF/counter/shift reg/LFSR ·
  B07 Moore vs Mealy 1011 detector · B08 toy ISA assembler/disassembler · B09 single-cycle CPU interpreter on B08 ISA ·
  B10 AMAT calculator + sensitivity · B11 memory map + address decoder + bus fault · B12 bus wait states + throughput +
  flash prefetch · B13 setup/hold slack + Fmax + skew · B14 reset causes + reset-cause register decoding.
- Medium/Advanced ARCH lessons already written (cite, don't duplicate): ARCH-M08 false sharing, ARCH-M12 DMA cache
  coherency, ARCH-A04 MESI simulator, ARCH-A05 memory-order litmus tests (ROOT/lessons/ARCH/). Other cross-refs you may
  cite by id when relevant: C-B01 bit ops, C-B02 integer promotion, C-B03 shifts, C-B04 GET/SET_FIELD, C-M01 MMIO
  volatile, C-M02 RMW/W1C, BM-B10 clock tree, MEM-B10 pointer chasing, ARM (track) Cortex-M/AArch64, SIV-B03 reset
  value test, SIV-B06 address decode/aliasing (SIV ones are written: ROOT/lessons/SIV/).
- Structural reference: ROOT/lessons/C/C-B01/ (harness, depth). Same-track tone: skim ROOT/lessons/ARCH/ARCH-M08/.
- Track intro says "M2"; the real host is **Apple M6** (arm64). Say M6 / Apple Silicon when you report measurements.

## Shared toy ISA (B08 and B09 MUST both use exactly this — they are written in parallel)
16-bit fixed-length instructions, 8 registers r0–r7 (**r0 hardwired to 0**, writes ignored), 16-bit data words,
Harvard: imem[256] and dmem[256] of uint16_t, both **word-addressed**. PC counts instructions. Arithmetic mod 2^16.
```
bits     15..12  11..9  8..6   5..3   2..0
ADD      0000    rd     rs     rt     000      rd ← rs + rt
ADDI     0001    rd     rs     imm6 (signed, -32..31)     rd ← rs + sext(imm6)
LD       0010    rd     rs     imm6                        rd ← dmem[rs + sext(imm6)]
ST       0011    rt     rs     imm6                        dmem[rs + sext(imm6)] ← rt   (bits 11..9 = data source)
BEQ      0100    rs     rt     imm6                        if rs == rt: PC ← PC + 1 + sext(imm6)
JAL      0101    rd     imm9 (signed, -256..255)           rd ← PC + 1; PC ← PC + 1 + sext(imm9)
HALT     1111    000000000000                              stop
```
Unused opcodes / nonzero ADD funct / nonzero HALT low bits = illegal instruction (decoder reports an error).
Assembly syntax: `ADD r1, r2, r3` · `ADDI r1, r1, -1` · `LD r1, 4(r2)` · `ST r1, 4(r2)` · `BEQ r1, r0, loop` ·
`JAL r7, func` · `HALT`; `;` starts a comment; `label:` on its own line or before an instruction; branch/jump operand may
be a label or a signed number (offset relative to PC+1). Disassembler prints canonical form with numeric offsets.
Assembler must report (not silently truncate) out-of-range immediates. Unconditional jump idiom: `BEQ r0, r0, label`
or `JAL r0, label`. Return idiom is not available (no JALR) — B09 programs must not need it; B08 may discuss
"why a real ISA needs JALR/indirect jump" in 풀이/Q&A. B09 embeds a compact copy of the decoder (say "ARCH-B08 ISA를
그대로 넣었다") and may hand-assemble programs as uint16_t arrays (with the asm source in comments) or embed a tiny
assembler; no #include across lessons.

## Per-item lanes / notes
- **B01**: n = 1..64 must work, including n = 64 (no `1ull << 64` UB!). to_str in base 2/10/16, signed and unsigned
  views, sign_extend(value, bits). Hand-calculation problems (10) can be in Part B or 추가 연습 with answers in details.
- **B02**: model flags in pure C (no inline asm in TODOs). Arm semantics: SUB sets C = NOT borrow (C=1 means no borrow);
  x86 CF = borrow — explain the difference. Condition table EQ NE CS/HS CC/LO MI PL VS VC HI LS GE LT GT LE AL with
  flag formulas, verified against C comparisons over many (deterministic PRNG + boundary) operand pairs.
  **Hardware cross-check demo (strongly wanted):** demo_hw_flags.c with AArch64 inline asm (`adds`/`subs`/`cmp`, then
  `mrs %0, nzcv`) comparing the model to the real M6 flags on many pairs; show the real output. Optional extra: the same
  model idea under Rosetta (`-arch x86_64`, `pushf`/`lahf` or setc/seto) to show x86 CF=borrow — only if you can run it.
  Mention ADC/SBC chains for 64-bit add on a 32-bit core (Cortex-M ADDS/ADC).
- **B03**: float32 and FP16 decompose/compose; float→FP16 round-to-nearest-even including subnormal results,
  overflow to Inf, NaN preserved as quiet NaN (state your NaN payload policy). Hardware reference: Apple clang arm64
  supports `_Float16` (fcvt). Checked tests may compare against `(_Float16)f` — run an **exhaustive all-2^32 float
  comparison in a demo** (demo_exhaustive.c, measure time) and paste the result; checked tests use boundary cases +
  a deterministic random sample. Show 0.1f bits, denormal min, FLT_MIN, 65504, 65520 → Inf boundary, ties-to-even.
- **B04**: gates only (AND/OR/XOR/NOT functions on 0/1 values) — no `+` in the adder TODOs. Exhaustive 65536 pairs
  vs `+`. Count gate-delay depth (critical-path levels) for ripple vs 4-bit-block CLA (or full 8-bit CLA) and gate
  count; show table. Optional: thumbv7em asm of a 64-bit add (ADDS/ADC) as the "carry chain in real ISA" link.
- **B05**: truth-table based models, exhaustive tests; priority encoder vs CLZ: compare with `__builtin_clz` in tests
  and show `clz` in arm64 / thumbv7em asm (`clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S`) in a demo.
  Firmware ties: NVIC/interrupt priority pick, address decoder chip-selects, RTOS ready-bitmap (find highest priority).
- **B06**: clock-loop simulation with explicit "compute next state from current state, then commit all at the edge"
  (two-phase) — show the bug when you update in place (race between flip-flops). 16-bit maximal LFSR (e.g. taps
  16,14,13,11 / 0xB400 Galois or the Fibonacci equivalent — state which form and verify period 65535 exhaustively;
  also show a non-maximal polynomial has a shorter period). Firmware ties: PRBS for link test, CRC relation.
- **B07**: overlapping 1011; print per-cycle text waveforms (clk / in / state / out) for Moore and Mealy; checked test
  that Mealy's output for the same input arrives one cycle earlier than Moore's registered output (define clearly what
  "cycle" and "registered" mean in your model). State-transition tables + svg state diagrams.
- **B08**: assembler (text → uint16_t) + disassembler + round-trip tests (asm → bin → asm text → bin equal; and all
  65536 words: decode either errors or re-encodes to the same word). Error cases: bad register, out-of-range imm,
  unknown label, illegal opcode. Discuss encoding trade-offs (why imm6, why rd position fixed, RISC-V/Thumb comparison).
- **B09**: interpreter with fetch/decode/execute/writeback, PC update, r0 hardwired; programs: array sum and Fibonacci
  (plus one more in Part C, e.g. memcpy or max-search), final register/memory state checked, instruction count (and per
  opcode histogram) measured; detect illegal instruction, PC out of range, step limit (infinite loop guard).
- **B10**: AMAT recursive formula (local miss rates!) vs global miss rate confusion; CPI impact (memory refs per
  instruction); sensitivity table: +1% hit rate at each level → ΔAMAT. You may quote this Mac's cache sizes from
  `sysctl hw.perflevel0.l1dcachesize hw.perflevel0.l2cachesize hw.cachelinesize` (show real output); latency numbers are
  parameters (cite as typical/textbook, measured version is MEM-B10).
- **B11**: memory map table (Cortex-M-like: Flash 0x0800_0000, SRAM 0x2000_0000, GPIO/UART in 0x4000_0000 peripheral
  region — say "STM32 style"), decoder routes to device read/write callbacks; unmapped → bus fault, misaligned (per
  region policy: e.g. peripherals word-only, SRAM allows unaligned halfword?) → fault with report (addr, size, r/w,
  reason). Mention Cortex-M BusFault/HardFault, BFAR, PRECISERR/IMPRECISERR, UNALIGN_TRP, without inventing register bit
  positions you are not sure of.
- **B12**: cycle-level model of a simple synchronous bus (address phase + data phase + N wait states), bandwidth =
  bytes / cycles; Flash 3 WS vs SRAM 0 WS; prefetch buffer (sequential fetch hits vs branch flush) comparison with
  instruction-stream traces (sequential, branchy). Tie to STM32 FLASH_ACR LATENCY/PRFTEN/ART accelerator (hedge exact
  numbers: "e.g. STM32F4 at 168 MHz needs 5 WS" only if you're sure — otherwise say "수 WS").
- **B13**: setup slack = Tclk − (Tclk-q + Tcomb,max + Tsetup) + skew (define skew sign convention clearly:
  skew = capture clock arrival − launch clock arrival); hold slack = Tclk-q + Tcomb,min − Thold − skew. Fmax from setup.
  Tests show positive skew helps setup and hurts hold. Multi-path table (pick the critical path). No real STA tool here
  — mention what OpenSTA/PrimeTime report looks like only generically.
- **B14**: reset-cause register model with sticky bits and clear mechanism (STM32 RCC_CSR + RMVF style "write 1 to a
  clear-all bit" AND nRF RESETREAS "write 1 to clear each bit" — model both or pick one and explain the other);
  multiple bits set at once (POR sets everything / PIN bit set together with WDT on some parts) → priority rules;
  boot code: read, log, clear, decide (e.g. WDT reset count → safe mode). Scenario sequences checked.
  Hedge exact bit names/positions unless sure (say "e.g." and keep the model generic).

## Code & language
- C for all 14: starter.c / solution.c / practice.c (+ demo_*.c), `cc -std=c11 -Wall -Wextra -Werror -O2`, CHECK harness,
  "N/M passed", exit 1 on failure. solution.c = starter.c with only TODO bodies filled (diff highlights them).
- Deterministic tests (fixed-seed xorshift), exhaustive where the task says so. No timing in CHECKs.
- Inline asm / `_Float16` / `__builtin_*` are fine in tests and demos, never as the TODO answer.
- Cross-target asm recipe: `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_x.c -o -`
  (freestanding headers only). arm64 asm: `cc -O2 -S -o - demo_x.c`. Paste real output, trimmed.

## Depth & format
- Basic level, but the reader is a senior engineer re-checking fundamentals: md **400–600 lines**.
  Front matter `id`, `time:` realistic (50분–80분), `updated: 2026-11-11`.
- Output: ROOT/lessons/ARCH/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c/solution.c/practice.c (+ demo_*.c).
  Title line `# <ID> <title exactly as in ARCH.json>`.
- Parts: A = core model, B = the tricky edge cases interviewers probe, C = firmware/silicon-realistic use. Each Part
  has checked tests.
- 1. 문제 first paragraph: one line on why it matters (firmware/silicon interviews, real chips).
- ≥2 inline ```svg figures (circuit / bit layout / timing diagram / state diagram / pipeline of the model). svg rules
  per base brief (viewBox, width ≤ 760, currentColor + f-acc/f-ok/f-warn/f-mut, font-size 12–14, escape &lt; &gt; &amp;).
- 면접 Q&A ≥5 with real 꼬리질문. 추가 연습 3–6, answers in <details>, ending with `### 한 번에 검증` +
  `<!-- code: practice.c -->` + build cmd.
- Accuracy over volume. If unsure of a spec/datasheet detail, say it less specifically rather than invent it, and
  list what you were unsure of in your final reply.

## Verify (you MUST run; use your own temp dir, e.g. mkdir -p /tmp/esn_<ID> and put all binaries there)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/esn_<ID>/st && /tmp/esn_<ID>/st   # builds, some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/esn_<ID>/so && /tmp/esn_<ID>/so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/esn_<ID>/pr && /tmp/esn_<ID>/pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/esn_<ID>/san && /tmp/esn_<ID>/san
    (same sanitizer run for practice.c)                                                         # clean
- Self-check the md: headings verbatim & in order (## 학습 목표, ## 1. 문제 with ### Part A — / ### Part B — /
  ### Part C —, ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수,
  ## 8. 면접 Q&A, ## 9. 추가 연습, ## 10. 핵심 정리, ## 참고자료); `<!-- code: starter.c -->` in §3,
  `<!-- code: solution.c diff=starter.c -->` in §6. Paste only real output you ran.

## When done
Reply (short) with: files written, PASS counts starter/solution/practice, sanitizer result, demo files + what they
showed, anything you were unsure about. Do NOT edit files outside ROOT/lessons/ARCH/<your ID>/.
