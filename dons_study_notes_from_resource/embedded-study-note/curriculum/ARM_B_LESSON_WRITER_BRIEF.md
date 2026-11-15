# Lesson writer brief — track ARM (ARM 아키텍처·어셈블리), Basic level

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math,
prepping for embedded/firmware/silicon-validation interviews (Anduril, Apple, NVIDIA, Rivian, Tesla...).
He re-checks fundamentals by DOING: every lesson = one interview-style coding problem.
ARM-B is the **first hands-on ARM assembly** track: write AArch64 `.S` functions on this Apple Silicon Mac
(M6, native), call them from C, read compiler output, debug in lldb; then Thumb-2 / Cortex-M basics
(code density on Mac, real execution in QEMU inside the Lima VM).

Already written (assume he knows them, cross-reference by id, don't re-teach): C Basic/Medium/Advanced
(C-B04 = bit-field GET/SET_FIELD with mask/shift), DS B/M/A, CPP B/M/A, ALGO-B/M/A, ARCH-B/M/A
(ARCH-B02 = NZCV flag calculator incl. real M6 `mrs nzcv` check; ARCH-B03 = IEEE 754 decomposer;
ARCH-B08 = toy ISA encoding), OS-B/M, COMM-B/M/A, RTOS-B/M/A, BM-B/M (BM-B08 = real Cortex-M3 boot on
QEMU mps2-an385 with C vector table + semihosting; BM-B09 = semihosting/UART printf), SIV-B/M/A,
ARM-M04 (LDXR/STXR/LSE), ARM-M05 (barriers), ARM-M11 (LDREX/STREX on QEMU mps2-an385), TOOL-M09.
Not written yet (cite by id only, don't teach their content): ARM-M01 (AAPCS64 deep: many args, struct
return, callee-saved), ARM-M02 (inline asm constraints), ARM-M03 (counter microbench), ARM-M06/M07 (NEON),
ARM-M08 (vector table + Thumb bit), ARM-M09 (exception entry, EXC_RETURN), ARM-M10 (PRIMASK/BASEPRI),
ARM-M12 (HardFault), ARM-M13 (EL/system regs), ARM-A09 (ADRP/GOT), ARM-A11 (vectorized compiler output),
TOOL-B04 (nm/objdump/otool), TOOL-A03 (gdb remote on QEMU).

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY (front matter, section headings verbatim and in order,
   svg rules, code rules, `<!-- code: ... -->` markers, `<details>` folding).
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base brief) — its Output / Quality bar apply unless overridden here.
3. ROOT/curriculum/tracks/ARM.json — your item (title/goal/task/tags/env/env_note). The lesson must do the
   item's `task` completely (every function, test, measurement it names). Skim all ARM items (B/M/A) to stay
   in your lane.
4. Reference lessons: ROOT/lessons/C/C-B01/ (Basic depth, CHECK macro, "N/M passed") and
   ROOT/lessons/ARM/ARM-M04/ (existing AArch64 lesson: tone, asm explanation style). QEMU lanes (B13, B14)
   also read ROOT/lessons/ARM/ARM-M11/run_qemu.sh and ROOT/lessons/BM/BM-B08/ (run_qemu.sh, linker script,
   vector table, semihosting) — reuse that recipe, embed compact copies (every lesson must be self-contained).

## Output
ROOT/lessons/ARM/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + code files (below)
(+ optional demo_*.c/.S/.sh, captured *.txt data files, run_qemu.sh for qemu lanes).
Front matter `updated: 2026-11-11`, realistic `time:` (40분–70분). Title line `# <ID> <title exactly as in ARM.json>`.
Size ~400–600 md lines. Q&A ≥ 5 (aim 6–8). ≥ 2 inline ```svg figures (viewBox, width ≤ 760, currentColor +
f-acc/f-ok/f-warn/f-mut only, no font-family except monospace, font-size 12–14, nothing outside the viewBox,
escape &lt; &gt; &amp;). Good figures for this track: register file (x0–x30/sp/pc, w-view lower 32 bits, v/q/d/s
views), arg-passing table x0–x7 / v0–v7, stack frame boxes with addresses (fp→[saved fp, saved lr], 16-byte
alignment), instruction bit layouts (MOVZ hw/imm16, logical imm N/immr/imms, B.cond), addressing-mode
before/after pointer arrows (pre/post index), flag → condition-code table, Thumb 16/32-bit halfword layout,
MSP/PSP/CONTROL mode diagram. Numbers in figures must match your code / real output.

Korean, plain words, English term in parentheses on first use. First paragraph of 1. 문제: one line on why it
matters in firmware interviews ("이 asm 뭐 하는 코드야?", crash dump reading, startup code, ISR). Tie to Don's
reality (SSD controller firmware on ARM cores, Cortex-M MCUs, Apple/NVIDIA AArch64 SoCs).

## Code file convention for asm lessons (IMPORTANT — the checker builds exactly this)
For each variant v ∈ {starter, solution, practice} the build is:
    sources(v) = [v.c if it exists, else harness.c]  +  [v.S if it exists]  +  [every shared_*.S / shared_*.c]
    cc -std=c11 -Wall -Wextra -Werror -O2 <sources> -o /tmp/<ID>_v && /tmp/<ID>_v
- Typical asm lesson: `harness.c` (CHECK macro tests + `extern` prototypes, shared by starter and solution),
  `starter.S` (asm with TODO stubs: e.g. `mov x0, #0` / `ret` — assembles, returns wrong-but-safe values,
  never loops forever, never touches memory it shouldn't), `solution.S` (same file with TODO bodies filled,
  same line structure so the diff highlights only the changed lines), `practice.c` + `practice.S` (추가 연습
  answers + their tests). If a lesson's TODO is C (e.g. B02, B03's encoder, B12), use starter.c/solution.c
  (each containing the harness, like the C track) and optionally a shared_*.S.
- md markers: `<!-- code: harness.c -->`, `<!-- code: starter.S -->`, `<!-- code: solution.S diff=starter.S -->`,
  `<!-- code: practice.S -->` / `<!-- code: practice.c -->` — show the exact build command under each.
- .S files are preprocessed: use ONLY `//` or `/* */` comments (never `;` comments — the site highlighter and
  portability both depend on it). Apple Mach-O symbol rules: `.globl _name`, `.p2align 2`, `_name:` (C sees
  `name`). Comment each instruction in Korean or English briefly (Basic level: Don is learning to read asm).
  In the md, show how the same file looks for Linux/ELF (`.global name`, `.type name,%function`, `.size`) —
  optionally via `#ifdef __APPLE__` + a SYM(name) macro; if you do that, build it in the Lima VM too
  (`limactl shell elinux -- bash -lc '...'`, aarch64 Linux, gcc/clang available) and say so.
- Starter must exit 1 with some FAIL lines; solution and practice must print all PASS and exit 0.
- Also run ASan/UBSan on solution and practice builds:
  `cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined <sources>`.
- Tests: deterministic (exhaustive where cheap, fixed-seed xorshift64 random vs a C reference model, named edge
  cases: 0, 1, -1, INT64_MIN/MAX, UINT64_MAX, empty arrays, unaligned pointers where relevant). solution and
  practice run < 2 s.
- Where C would hit UB (signed overflow, division by zero, INT_MIN/-1, out-of-range float→int, NaN→int),
  the TEST must call the asm and compare with a hand-written expected value — never evaluate the UB in C.
  Explain this in the md (C abstract machine vs ISA-defined behaviour).
- Environment (Mac):
      export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
  `clang -S -O2 -o - x.c`, `otool -tv`, `objdump -d` (llvm-objdump at /usr/local/bin) all work.
  `lldb -b -o 'b f' -o run -o 'register read x0' -o si ... ./prog` works in batch mode (verified) —
  for lldb sessions put the commands in a demo_*.lldb file and run `lldb -b -s demo_x.lldb ./prog`;
  paste the REAL transcript (trim addresses-noise if needed, but don't invent).
  Cross-compile (object only) works: `clang --target=thumbv7em-none-eabi -Os -c x.c` (and `-mthumb`,
  `--target=armv7a-none-eabi -marm`, `--target=aarch64-none-elf`); use `-ffreestanding` and only
  <stdint.h>/<stddef.h> for cross targets.
- Private temp names only: /tmp/<ID>_* and /tmp/<ID>_work/ (Mac and VM). 13 other writers run in parallel and
  have clobbered shared files before — never write to the shared scratchpad or another lesson dir.
- Paste only REAL output (asm listings, objdump, lldb/gdb transcripts, size, assembler errors, timings).
  Never invent numbers. M6 timings are noisy (other jobs run on this machine): report median of several runs,
  and hedge.

## QEMU lanes (B13, B14) — Lima VM `elinux` (running; do NOT stop it)
- Toolchain in VM: arm-none-eabi-gcc 14.2.1 (newlib/nano/rdimon specs), qemu-system-arm 10.2.1,
  gdb-multiarch. Run: `limactl shell elinux -- bash -lc '...'`. Mac home is mounted READ-ONLY in the VM →
  run_qemu.sh copies sources to VM `/tmp/<ID>_work` and builds there.
- Firmware .c/.S files start with `/* ENV: qemu */` (first line). Flags: `-mcpu=cortex-m3 -mthumb -std=c11 -O2
  -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns` (+ `-lgcc`), your own
  tiny linker script + C vector table (compact copy of BM-B08's), semihosting SYS_WRITE0/SYS_EXIT with
  `-semihosting-config enable=on,target=native`, `-M mps2-an385 -nographic -monitor none -serial none`.
- `run_qemu.sh [starter|solution|practice|gdb]`: builds that variant (sources = <v>.S + firmware harness, same
  convention as above), runs QEMU (must terminate on its own: SYS_EXIT; wrap in `timeout 20`), prints the
  firmware's PASS/FAIL lines, last line `qemu exit=<n>`, and returns the firmware's exit code (starter → 1,
  solution/practice → 0). The checker runs exactly
  `limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/<ID>/run_qemu.sh starter'` (and solution, practice).
  Never leave QEMU processes running.
- Also do a Mac-side "assembles?" check for your Thumb .S: `clang --target=thumbv7m-none-eabi -c solution.S`
  (show it in the md as the no-VM fallback).
- In the md, give the exact one-line command Don runs from the Mac.

## Lanes (stay in yours; cross-ref the others by id)
- **B01** First .S functions called from C: add3(long,long,long), max(long,long) (signed), sum_array(const long*,
  size_t) (a plain loop with `ldr x, [x0], #8` OK — addressing modes in depth = B04). x0–x7 args, x0 return,
  w-registers for int (upper 32 bits of x are zeroed by w writes; show an `int` vs `long` mismatch bug: asm
  that uses x for an `int` parameter whose upper bits are garbage — be careful + exact about what Apple/AAPCS64
  guarantee about upper bits; hedge and cite docs). Apple ARM64 vs AAPCS64 differences table (x18 reserved,
  `_` prefix, frame pointer x29 always kept, red zone 128 B, variadic args all on the stack (show a real
  printf-from-asm or va_arg demo), caller-side extension of char/short args to 32 bits, stack arg packing by
  natural alignment, `long double` = double) — every row hedged with the Apple doc link; demonstrate at least
  2 of them for real (e.g. the variadic one with -O2 asm of a call to a variadic fn, `-ffixed-x18` note).
  No frames/recursion (B08), no FP args (B11), no >8 args / struct return (ARM-M01).
- **B02** Reading clang output. 6 C functions (loop sum, switch → jump table or compare chain, struct field
  access with offsets, call to another function (prologue/epilogue, callee-saved), a bounds-checked array index,
  a simple multiply/divide by constant (shows `madd`/`umulh` magic number or shifts)). Compile -O0 vs -O2
  (real `clang -S -O0/-O2 -fno-asynchronous-unwind-tables`, trim `.cfi`), annotate line-by-line in the md,
  table of what disappeared at -O2 (stack spills, redundant loads, loop → unrolled/vectorized?, tail call `b`).
  Coding task ("what does this asm do?" interview round): `shared_mystery.S` = REAL clang -O2 output of 4–6
  hidden C functions (renamed `_mystery1`…; keep them small and non-trivial: e.g. popcount-ish loop,
  branchless clamp via csel, a strlen-like scan, an abs-diff, a struct-of-arrays lookup); starter.c has TODO C
  re-implementations `my_mystery1…` that return 0; harness compares them with the asm on exhaustive/random
  inputs. Keep vectorized output out (ARM-A11 owns that) — use `-fno-vectorize -fno-slp-vectorize` if needed.
- **B03** MOVZ/MOVK/MOVN + logical immediates. Part A: `movz_movk_plan(uint64_t v, insn out[4])` C function that
  returns the minimal MOVZ/MOVN + MOVK sequence (count + hw shifts + imm16s; choose MOVN when more 0xFFFF
  halfwords), and its 32-bit machine encodings `enc_movz/enc_movk/enc_movn(rd, imm16, hw)`. Verify encodings
  against the REAL assembler: put reference instructions in shared_ref.S under labels (`_ref_movs:` …) and read
  their 32-bit words from the text section at runtime (data-in-text read is fine), compare. Also hand-written
  asm functions returning constants (e.g. `_k_deadbeef`) built from your plan. Part B: `is_logical_imm64(v,
  &N,&immr,&imms)` encodability (DecodeBitMasks from the Arm ARM pseudocode as reference: enumerate ALL valid
  (N,immr,imms) → set of 5334 64-bit patterns, exhaustive cross-check both directions; 0 and ~0 not encodable),
  plus 32-bit variant. Show the real assembler error for `and x0, x0, #0x1234` and what clang does with a
  non-encodable constant (`mov`+`and`). Part C: firmware flavour — register masks like 0x0000FF00,
  0x00FF00FF00FF00FF (encodable) vs 0x12345678 (not) → instruction-count cost. Cite ARCH-B08 (encoding basics).
- **B04** Addressing modes: `[xn, #imm]`, `[xn, #imm]!` (pre-index), `[xn], #imm` (post-index), `[xn, xm, lsl #3]`,
  `[xn, wm, sxtw #2]`, `ldrb/ldrh/ldrsw` widths, LDP/STP. Functions: array sum (one variant per mode),
  array copy (ldr/str then ldp/stp 2-at-a-time with odd-length tail), struct-array field sum (e.g. struct
  {uint32_t id; int32_t temp; uint64_t ts;} — stride via offsetof, `_Static_assert` on layout), random tests vs
  C versions (lengths 0..N, random data, fixed seed). Figure: pointer before/after for each mode. Show the
  scaled-offset range limits (imm12 × size, imm9 unscaled → `ldur`) with a real assembler error. No memcpy perf
  (B10), no unaligned/SIMD loads.
- **B05** Flags & branches: CMP/CMN/TST, B.cond (EQ NE LT LE GT GE / LO LS HI HS / MI PL VS VC), CBZ/CBNZ, TBZ/TBNZ,
  CSEL/CSINC/CSINV/CSNEG/CSET. max/min/abs/clamp/sign — branch version + branchless version each (signed int64;
  plus unsigned max/min); boundary tests (INT64_MIN/MAX, abs(INT64_MIN) — define and test your choice: wraps
  to INT64_MIN as the ISA does; do not evaluate it in C). Reproduce the GT vs HI (signed vs unsigned condition)
  bug with an injected-bug function and a test that catches it (e.g. -1 vs 1). Condition-code table derived from
  NZCV (cite ARCH-B02 for flag arithmetic, don't redo it). Mention compare-and-branch fusion only hedged.
- **B06** Bit instructions: AND/ORR/EOR/BIC/ORN, LSL/LSR/ASR/ROR, UBFX/SBFX/BFI/BFXIL/UBFIZ, CLZ/CLS, RBIT,
  REV/REV16/REV32. Functions: field extract (UBFX with fixed lsb/width; and a variable-position version — UBFX
  needs immediates, so explain the lsr+and fallback), field insert (BFI), bit reverse (RBIT), highest set bit
  (63-CLZ, define x=0), nibble reverse within a 32-bit word (Don's Rivian screen question!: e.g. 0x12345678 →
  0x87654321 — REV + per-byte nibble swap), byte swap (REV) for a big-endian register/packet field. Compare with
  C-B04-style C reference functions (embed compact copy) on random inputs. Firmware: decode/patch a register
  value (e.g. a fake STATUS register fields). Show what clang -O2 emits for the C versions (often UBFX/BFI).
- **B07** MUL/MADD/MSUB/MNEG, SMULL/UMULL (32×32→64), SMULH/UMULH (64×64→128 high), UDIV/SDIV. Functions:
  `mul128(a,b,&hi,&lo)` (unsigned, and signed via SMULH), `mod_u64/mod_s64` via UDIV/SDIV + MSUB, `mulhi_u64`.
  Verify against `unsigned __int128` / `__int128` (clang supports it) on random + edge inputs. Measure ISA
  behaviour that is UB in C: SDIV x/0 → 0, UDIV x/0 → 0, SDIV INT64_MIN/-1 → INT64_MIN (no trap) — via the asm,
  expected values hand-written. x86 contrast: build a tiny x86_64 demo (`clang -arch x86_64`, runs under
  Rosetta 2 — check it is installed: `arch -x86_64 /usr/bin/true`) that executes `idiv` with 0 and
  INT64_MIN/-1 → SIGFPE; catch it with a signal handler and print the real result. If Rosetta is absent, say so
  and don't fake it. Firmware: fixed-point Q32.32 multiply using UMULH (Part C).
- **B08** Stack frames: `stp x29, x30, [sp, #-16]!` / `mov x29, sp` / `ldp x29, x30, [sp], #16`, 16-byte SP
  alignment, callee-saved x19–x28 (save/restore the ones you use), local array on the stack (`sub sp, sp, #N`,
  N rounded up to 16). Recursive factorial, recursive fibonacci (naive), and a function with a local array (e.g.
  fills a 10-element local array and sums it / calls a C callback with its address). Verification beyond the
  return value: the base case calls a C hook that walks the frame-pointer chain (`__builtin_frame_address(0)`,
  then [fp] → caller fp, [fp+8] → saved lr) and records depth + saved-LR addresses → test that depth == n+…
  and every saved LR lies inside your asm function (compare with `&fact_asm` .. end label). lldb batch session
  (demo_frames.lldb): break at the base case, `bt`, `register read fp sp lr`, `memory read -fx -s8 -c2 $fp`
  following 3 links — paste the real transcript and draw the matching stack figure with the REAL addresses
  (relative offsets ok). Also inject a bug variant (forgot to save x30, or misaligned sp) and show what happens
  for real (crash type / wrong return) — in a demo file, not in the tests. lldb skills in depth = B09.
- **B09** lldb debugging. starter.S = assembly `my_strlen` with a deliberate off-by-one (e.g. counts the NUL /
  increments before the check) — the TODO is to find and fix it; solution.S = fixed. harness tests strings of
  length 0..64 at every alignment 0..15, plus page-end strings (mmap 2 pages, mprotect the 2nd PROT_NONE, place
  the string so its NUL is the last byte of page 1 → a read past NUL crashes; this is how "reads one byte too
  far" becomes visible). demo_debug.lldb = the actual session: `b my_strlen`, `si`/`ni` (explain the
  difference with a `bl` inside), `register read x0 x1 cpsr` (decode NZCV from cpsr bits), `memory read -c16 -fx
  $x0`, `disassemble -n my_strlen`, `register write`, watchpoint `watchpoint set expression -- $x0`? (only if it
  works — check). Paste real transcript with the bug visible in register values; record a step table (pc, insn,
  x0, x1, flags). Also practice: a second planted bug (e.g. signed compare on bytes ≥ 0x80 / ldrsb vs ldrb).
  GDB command equivalents table (for Cortex-M/QEMU later, cite TOOL-A03). Lane: no performance (B10).
- **B10** strlen/memcpy in asm: memcpy byte loop; memcpy "align dst then 16-byte LDP/STP x2 regs loop + tail";
  strlen byte loop. (Word-at-a-time strlen with the haszero trick only as 추가 연습 if you want; SIMD = ARM-M07.)
  Fuzz: all (len 0..300) × (src align 0..15) × (dst align 0..15) with guard bytes before/after dst (canary must
  stay), compare with a C reference; overlapping regions are out of contract (say why = memmove). Benchmark in
  demo_bench.c (clock_gettime / mach_absolute_time, sizes 16 B, 256 B, 4 KB, 1 MB, median of N): byte vs 16B vs
  libc memcpy, byte strlen vs libc strlen → real table + why libc wins (SIMD, unrolled, alignment tricks;
  hedge). Mention that clang may turn your C reference loop into a memcpy call (use `-fno-builtin` or a volatile
  loop for the reference if needed). Note Apple libc strlen reads aligned 16 B chunks past the NUL (can't cross a
  page) — explain why that's legal for libc but would fail B09's page test idea only if crossing pages (hedge).
- **B11** FP/SIMD register file: v0–v31 with b/h/s/d/q views, FP args in v0–v7 (s/d), return in s0/d0, mixed
  int+FP args (independent counters x and v), callee-saved d8–d15 (lower 64 bits only). Functions: `dot3(const
  float*, const float*)` (ldr s / fmul / fmadd), `lerp(float a, float b, float t)` (fmadd/fmsub; discuss
  a+t*(b-a) vs (1-t)a+tb exactness at t=1), `f2i_sat(float)` with FCVTZS (round toward zero, saturating, NaN → 0)
  vs FCVTNS/FCVTMS/FCVTPS/FCVTAS rounding table (2.5, -2.5, 3.5…), plus `double` variants, scvtf/ucvtf. Tests:
  compare with C (fmaf for fused cases — careful: `a*b+c` vs fmaf differ; state clang's -ffp-contract default on
  Apple and test against the right one), NaN/±Inf/±0/huge/denormal inputs to FCVTZS — expected values
  hand-written (C cast would be UB). FPCR rounding-mode / FZ bits only mention (no fesetround games needed).
  Cite ARCH-B03 for IEEE layout. No NEON vectors (ARM-M06).
- **B12** Code density. Same C file (6–10 small realistic firmware functions: bit set/clear, ring-buffer push,
  CRC8 loop, switch-state-machine, struct copy, saturating add, small memcpy) compiled with -Os for
  `--target=thumbv7em-none-eabi -mthumb`, `--target=thumbv6m-none-eabi` (Cortex-M0, Thumb-1 mostly),
  `--target=armv7a-none-eabi -marm`, `--target=aarch64-none-elf` (all `-ffreestanding -c`). Capture REAL
  `llvm-objdump -d` (or `objdump -d`) + `size`/section sizes into data files in the lesson dir (trim, keep each
  < 20 KB). Coding task (Mac C): starter.c/solution.c parse the captured disassembly text → per-function and
  total byte size, instruction count, histogram of instruction lengths (2 vs 4 bytes; Thumb-2 32-bit insn =
  two halfwords; first halfword top 5 bits 0b11101/0b11110/0b11111 rule — implement `thumb_insn_len(uint16_t
  hw1)` and test it exhaustively against the objdump-derived lengths), then the comparison table. Explain
  `.n`/`.w` suffixes, IT blocks, why Thumb-2 is ~25–35% smaller than A32 (hedge, use your real numbers), why
  AArch64 has no Thumb. No execution needed. Cortex-M Thumb asm writing = B13.
- **B13** (qemu) Thumb-2 asm on Cortex-M3 (mps2-an385): `.syntax unified`, `.thumb`, `.thumb_func` (why the
  Thumb bit matters for function pointers — brief; vector table = ARM-M08), registers r0–r15 (sp/lr/pc), AAPCS
  (r0–r3 args, r0 return, r4–r11 callee-saved, push {r4-r7, lr} / pop {…, pc}). Functions: `max_s32`, `abs_s32`
  (both with IT blocks: `it gt`/`ite lt` + conditional instructions; show the 16-bit vs 32-bit encodings in
  objdump with `.n/.w`), `sum_array_s32` (ldr with post-index `ldr r3, [r0], #4`), plus `clamp_s32` with an
  ITE/ITT block. Firmware harness (C, semihosting) runs tests and prints PASS/FAIL + "N/M passed", exits via
  SYS_EXIT with status. starter.S stubs fail. IT rules: max 4 instructions, condition must match T/E pattern,
  branches only last in block, flags-setting behaviour inside IT (`adds` vs `add` 16-bit encodings), deprecated
  IT forms in ARMv8-A AArch32 (hedge). Mac fallback: assemble with clang --target=thumbv7m-none-eabi -c.
- **B14** (qemu) Cortex-M register model: MRS/MSR for MSP, PSP, xPSR (APSR/IPSR/EPSR views), CONTROL (nPRIV bit0,
  SPSEL bit1; FPCA = M4F only), PRIMASK read only as a register (critical sections = ARM-M10). asm helpers
  `get_msp/get_psp/set_psp/get_control/set_control/get_xpsr/get_ipsr` + a function that switches Thread mode to
  PSP (set PSP to a static stack's top (8-byte aligned), set CONTROL.SPSEL=1, ISB — explain why ISB), then
  verifies sp == psp. Then trigger SVC (or use SysTick/PendSV) to observe in the handler: IPSR = exception
  number, Handler mode always uses MSP, CONTROL.SPSEL reads… (verify for real what QEMU shows, and state the
  architectural rule; hedge), LR = EXC_RETURN value (just print it; decoding EXC_RETURN = ARM-M09). Also
  unprivileged thread (nPRIV=1): MSR CONTROL from unprivileged is ignored → observe, and get back via SVC.
  Firmware tests print PASS/FAIL. gdb session (run_qemu.sh gdb: `-s -S` + `gdb-multiarch -batch -x
  demo_regs.gdb`) showing `info registers` / `p $msp $psp $control $xpsr` (check which names gdb-multiarch
  exposes for M-profile on QEMU 10.2 — paste real output) before/after the PSP switch. Figure: Thread/Handler ×
  priv/unpriv × MSP/PSP state machine. No exception stack frame decoding (ARM-M09), no PendSV context switch
  (ARM-A01).

## Interview flavour
- 2. Problem (English) = real interview prompt: signatures (C prototype the asm must satisfy), constraints
  (no stack use / leaf function / must be branchless / must preserve x19…), example I/O.
- 면접 Q&A = real follow-ups: why x18 is reserved on Apple, caller- vs callee-saved and who pays, why 16-byte SP
  alignment, why LR must be saved in non-leaf functions, what `ret` actually does (br x30 + return predictor
  hint), how to load a 64-bit constant / address (MOVZ/MOVK vs ADRP+ADD vs literal pool), why ARM has no
  divide-by-zero trap, signed vs unsigned conditions, what IT does and why AArch64 dropped it, MSP vs PSP and
  why an RTOS uses PSP for tasks, Thumb bit in function pointers.

## Quality bar
- Parts A/B/C escalate: A = core mechanic, B = edge cases / injected bugs caught by tests, C = firmware-realistic
  use (register field patch, fixed-point math, boot/RTOS context, crash debugging).
- 풀이 detailed: instruction-by-instruction trace table on a small example (register values after each
  instruction), encoding bit layouts where relevant, why each instruction choice, cycle/size notes hedged.
- Accuracy over volume. Every ABI rule, encoding field, condition code and instruction semantic double-checked
  against the Arm ARM / Apple doc / real behaviour on this machine. From-memory facts hedged ("Arm ARM 기준,
  확인 권장"). Don (math major, reads asm in crash dumps) will notice.

## When done
Reply with: files written (paths), md line count, PASS counts for starter/solution/practice (and QEMU run
results for qemu lanes), ASan/UBSan result, demo files + key real numbers, and every from-memory spec fact you
were unsure about. Do NOT edit files outside ROOT/lessons/ARM/<your ID>/ (plus your private /tmp paths).
