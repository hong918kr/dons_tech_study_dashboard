# Lesson writer brief — track ARM (ARM 아키텍처·어셈블리), **Medium** level
# (ARM-M01–M03, M06–M10, M12–M14; M04/M05/M11 already written 11-08)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering: ROOT/NOTES_SPEC.md (lesson rules) → ROOT/curriculum/LESSON_WRITER_BRIEF.md (base) →
ROOT/curriculum/ARM_B_LESSON_WRITER_BRIEF.md (ARM track base: **asm file convention**, Mac tool facts, QEMU lane
recipe, interview flavour, quality bar — read it fully, its B-lanes tell you what Don already knows) →
**this file** (Medium overrides + per-item lanes). This file wins on conflicts.

## Who / why
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math, targeting firmware /
silicon-validation / robotics roles (Anduril, Rivian, Tesla, Apple, NVIDIA). He finished ARM-B01–B14 (AArch64 .S
from C, clang output reading, MOVZ/logical imm, addressing modes, flags/csel, bit ops, mul/div, stack frames, lldb,
memcpy/strlen, FP regs, code density, Thumb-2 IT on QEMU M3, M-profile MSP/PSP/CONTROL on QEMU). ARM Medium = the
layer an interviewer probes next: "struct 리턴은 어디로 가나?", "이 inline asm 의 clobber 빠진 거 보여?", "이 명령
지연 몇 사이클이야, 어떻게 쟀어?", "NEON 으로 memchr 짜봐", "벡터 테이블 엔트리가 짝수면?", "HardFault 에서 PC 어떻게
찾아?", "PRIMASK 랑 BASEPRI 차이?", "EL1 에서 SVC 받으면 ESR 에 뭐가?", "이 크래시 덤프 보고 원인 라인 찾아봐".

## Overrides for Medium
- Output: ROOT/lessons/ARM/<ID>/**2026-11-15**_<ID>_<english_snake_slug>.md + code files (ARM-B convention) +
  optional demo_*.c/.S/.sh/.gdb/.lldb + captured *.txt. Front matter `updated: 2026-11-15`, `time:` 60분–90분.
  Title line `# <ID> <title exactly as in ARM.json>`.
- md **450–700 lines**, ≥2 svg figures (≥3 encouraged), **면접 Q&A ≥ 6** (aim 7–9), 추가 연습 3–6 ending with
  `### 한 번에 검증` + `<!-- code: practice.* -->` + build/run cmd.
- Parts escalate: A = core mechanic, B = edge cases / injected bugs that tests catch (every bug the task names is
  reproduced for real and asserted), C = firmware/SoC-realistic use (driver/ISR/RTOS/bring-up/crash-triage context).
- Tests: deterministic (exhaustive where cheap, fixed-seed xorshift64 vs a C reference, named edge cases). "N/M
  passed", exit 1 on any failure, solution/practice < 2 s (Mac) / QEMU run < 20 s, **no wall-clock timing inside
  CHECKs** — benchmarks live in demo_bench.c (or demo_*.c) with REAL pasted output (median of ≥5 runs, report
  machine load `uptime` alongside; other writers run in parallel → hedge, ratios over absolutes).
- Starter = solution with TODO bodies stubbed (same line structure → diff highlights only changed lines); starter
  FAILS (exit 1), never hangs, never corrupts memory/stack (stubs return safe wrong values; asm stubs keep the ABI).
- Self-contained: no #include across lessons; embed compact copies and label them ("(ARM-B14 startup 축약판)").
- Private temp names only: Mac `/tmp/<ID>_*`, `/tmp/<ID>_work/`; VM `/tmp/<ID>_work/`. 10 other writers run in
  parallel right now — never write to another lesson dir, the shared scratchpad, or curriculum/. Never stop/restart
  the Lima VM; never leave QEMU/gdb processes running (wrap in `timeout`).

## Build conventions (checker builds exactly this)
Mac lanes (M01, M02, M03, M06, M07, M14): ARM-B convention —
    sources(v) = [v.c if exists, else harness.c (not for practice)] + [v.S if exists] + [all shared_*.S / shared_*.c]
    cc -std=c11 -Wall -Wextra -Werror -O2 <sources> -o /tmp/<ID>_v   (run from the lesson dir)
    + ASan/UBSan: cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined <sources>
  `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` (27.2 SDK link broken).
  If you need extra flags (e.g. `-march=armv8.2-a+dotprod`) the checker can't add them → use
  `__attribute__((target("dotprod")))` / `#pragma clang attribute` or `.arch_extension dotprod` in the .S instead,
  so the plain command above works. Tool facts: use `/usr/bin/objdump` or
  `/Library/Developer/CommandLineTools/usr/bin/llvm-objdump` (NOT /usr/local/bin/llvm-objdump = LLVM 3.2);
  `xcrun llvm-size`; `lldb -b -s cmds.lldb ./prog` works; M6 = 3 perf levels (2 Super + 4 Performance + 6
  Efficiency) — `sysctl hw.perflevel0/1/2…`; Rosetta present.
QEMU lanes (M08, M09, M10, M12 = Cortex-M on `mps2-an385`; M13 = AArch64 on `virt`): `run_qemu.sh
  [starter|solution|practice|gdb|…]` exactly as in ARM-B brief (copy sources to VM /tmp/<ID>_work, build, `timeout
  20` QEMU, print firmware PASS/FAIL + "N/M passed", last line `qemu exit=<n>`, return firmware exit code: starter 1,
  solution/practice 0). Checker runs `limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/<ID>/run_qemu.sh
  <v>'`. Firmware sources start with `/* ENV: qemu */`. Mac-side no-VM fallback: `clang --target=thumbv7m-none-eabi
  -c solution.S` (M13: `clang --target=aarch64-none-elf -c solution.S`) — the checker runs it, so it must assemble.
  Cortex-M recipe: compact copies of ARM-B13/B14 (ROOT/lessons/ARM/ARM-B14/: run_qemu.sh, arm_b14.ld,
  shared_startup.c) and BM-B08; flags `-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding
  -nostdlib -fno-tree-loop-distribute-patterns` + `-lgcc`, semihosting SYS_WRITE0/SYS_EXIT,
  `-M mps2-an385 -nographic -monitor none -serial none -semihosting-config enable=on,target=native`.
  VM toolchain: arm-none-eabi-gcc 14.2.1, qemu-system-arm/aarch64 10.2.1, gdb-multiarch, native aarch64 gcc/clang +
  ld.lld (VM is aarch64 Linux → native gcc with `-ffreestanding -nostdlib -nostartfiles -static -T your.ld` builds
  bare-metal AArch64). Mac home is mounted READ-ONLY in the VM.
  **Known QEMU 10.2 mps2-an385 facts (verified by BM-M13/BM-M02)**: num-irq 32, 8 NVIC priority bits implemented
  (real AN385 M3 = 3 bits) → say which your tests rely on; free IRQ lines for software-pended IRQs: 6, 7, 14–17,
  23, 25–31; stores to unmapped addresses → PRECISERR BusFault (real M3 often IMPRECISERR); 0x30000000 BusFaults,
  many "reserved" regions read 0; `-no-reboot` exits 0 on reset (looks like a pass — don't rely on it);
  SYSRESETREQ = full machine reset. Use `-accel tcg,one-insn-per-tb=on` when you need IRQs to land between any two
  instructions (ARM-M11 run1 pattern). For FPU frames use `-M mps2-an386` (Cortex-M4F; `-mcpu=cortex-m4
  -mfpu=fpv4-sp-d16 -mfloat-abi=hard`) — verify it boots before relying on it.

## Cross-references (check `ls ROOT/lessons/<TRACK>` before citing)
Written: ARM-B01–B14, ARM-M04 (LDXR/STXR/LSE), ARM-M05 (barriers DMB/DSB/ISB/LDAR/STLR), ARM-M11 (LDREX/STREX on
QEMU M3), C-B/M/A (C-M14 = ISR↔main shared data with PRIMASK in a C mock), CPP, DS, ALGO, ARCH-B/M/A (ARCH-M03 =
branch mispredict measured on M6; ARCH-M06 = DCZID/cacheline), OS-B/M, COMM-B/M/A, RTOS-B/M/A, BM-B/M (BM-B08 =
C vector table boot; BM-M01 = NVIC register model; BM-M02 = priority/PRIGROUP on QEMU; BM-M13 = fault POLICY:
SHCSR/CCR enables, safe state, .noinit record, SYSRESETREQ, escalation), SIV-B/M/A, TOOL-M09.
Not written (cite as "다음 단계" only): ARM-A01 (PendSV context switch), ARM-A02 (AArch64 vectors + IRQ context),
ARM-A07 (saturating/fixed-point SIMD), ARM-A09 (ADRP/GOT), ARM-A10 (PAC/BTI), ARM-A11 (vectorized compiler output),
BM-A01 (SystemInit/VTOR), BM-A09 (crash dump to .noinit), TOOL-B04 (nm/objdump/otool), TOOL-B10 (debugger basics),
TOOL-M14 (DWARF/addr2line/atos), TOOL-A03 (gdb remote on QEMU), TOOL-A05 (crash dump format).

## Lanes (stay in yours; the other items are being written IN PARALLEL)
- **M01** (mac) AAPCS64 deep. asm functions (.S): `sum10(long a0..a9)` (args 9–10 on the stack: read [sp], [sp+8] at
  entry), a mixed version with int/char/short stack args showing **Apple packs stack args by natural alignment
  (1/2/4 B slots) vs AAPCS64 8-byte slots** (real `clang -S` of a caller proves it; tested for real), `make_big()`
  returning a 24–40 B struct via **x8 indirect result** (caller-allocated; asm writes through x8), a ≤16 B struct
  returned in x0/x1, an HFA `struct {float a,b,c,d;}` passed in s0–s3 and returned in s0–s3 (and what happens with 5
  floats → memory), plus a variadic call note (Apple: all variadic args on the stack — ARM-B01 showed basics; here
  just contrast in one table row). **ABI conformance checker** (the core coding task): `abi_call_check(fn, args…)`
  in asm that loads sentinels into x19–x28 and d8–d15 (and checks sp alignment/unchanged), calls fn, then reports
  which callee-saved regs changed (bitmask) — tests: correct functions → 0; injected violators (clobber x19 without
  save, clobber d9, only upper half of v8 changed → must NOT be flagged since only d8–d15 low 64 bits are
  callee-saved) → exact mask. Figures: arg-assignment table (NGRN/NSRN/NSAA), stack layout of sum10 call with real
  offsets, x8 result buffer arrow. Cite AAPCS64 + Apple "Writing ARM64 code for Apple platforms" (hedge rules from
  memory). No inline asm (M02), no x18 deep-dive (B01 did).
- **M02** (mac) GCC/Clang extended inline asm. starter.c/solution.c contain wrappers: `rbit32/rbit64`, `clz64`
  (define clz(0)=64 — ISA result, no UB), `add128(a_hi,a_lo,b_hi,b_lo,&hi,&lo)` with ADDS/ADC (carry chain inside ONE
  asm statement — explain why splitting across two asm statements is wrong), `read_cntvct()` (`mrs %0, cntvct_el0`
  — verify EL0 access works on macOS; also CNTFRQ_EL0; compare with mach_absolute_time units for real), and one
  `memory`-clobber case (e.g. a "store then barrier" wrapper or an asm that writes through a pointer operand).
  Constraint table: `r`, `=r`, `+r`, `=&r` (early clobber), `w` (FP/SIMD), `I`/`K`/`L` immediates, `m`/`Q`, operand
  modifiers `%w0`/`%x0`/`%s0`/`%d0`, `"cc"`, `"memory"`, named operands `[x]`. Part B = **reproduce for real at -O2**
  every bug you claim: missing early-clobber (`=r` output assigned the same reg as an input that is read after the
  output is written) → wrong value; missing `memory` clobber → compiler keeps a stale value / moves a load across the
  asm; output declared but asm uses `%w` vs `%x` mismatch → upper bits; forgetting `volatile` on cntvct → reads
  hoisted/CSE'd out of a loop (show the asm). For `"cc"`: check what clang/GCC actually assume on AArch64 (flags may be
  treated as always clobbered) — if a missing-"cc" bug cannot be reproduced, say so honestly with the asm that shows
  why. Each bug: bad version in demo_bugs.c with real -O2 asm + real wrong output; tests in solution check the
  correct versions vs C references (`__builtin_clzll` guarded for 0, `unsigned __int128`, bit-reverse loop).
  Show the same wrappers compile for `--target=aarch64-linux-gnu` (or in the VM with gcc) — GCC vs clang differences
  hedged. No microbenchmarking (M03), no NEON intrinsics (M06).
- **M03** (mac) Counter-based microbenchmark harness. `bench.h`-style code inside starter/solution: ISB-serialized
  `read_cntvct()` (`isb; mrs`), CNTFRQ_EL0 (expect 24 MHz on Apple Silicon — measure, don't assume), overhead
  calibration (empty region), repetition with min/median, loop of N unrolled dependent instructions in .S (latency)
  vs independent chains (throughput): ADD, MUL, MADD, SDIV/UDIV (operand-dependent latency? test small vs large
  quotients — real), LDR L1-hit pointer-chase (`ldr x0, [x0]` on a self-pointing slot), FADD/FMUL/FMADD (d regs).
  Resolution problem: 1 tick = 41.7 ns ≫ 1 cycle → you need N ≥ 10^5–10^6 instructions per sample; convert to cycles
  only via a calibrated assumption (e.g. dependent ADD chain = 1 cycle/insn ⇒ effective GHz; cross-check with
  `sysctl` if it exposes frequency; hedge) and say which core type ran it (P/E/Super unknown → run with
  `taskpolicy`/QoS? only if it works; else report spread). The CHECKed coding task is the harness math and
  plumbing, deterministic: median/min of arrays, overhead subtraction with clamping, ticks→ns conversion with exact
  integer math (no overflow for 2^40 ticks; test with large values), stats over synthetic samples, "result sink" so
  the measured loop isn't deleted (verify via objdump that the loop survives at -O2). Real latency/throughput table
  in demo_bench.c output (pasted), compared with published Apple-core numbers only if you can cite (else omit).
  Contrast with x86 `rdtsc`/`lfence` in one Q&A. Lane: no inline-asm constraint tutorial (M02), no cache-hierarchy
  sweep (ARCH/MEM own that), no branch-mispredict (ARCH-M03).
- **M06** (mac) NEON basics. f32 vector add `vadd_f32(dst,a,b,n)`, f32 dot `dot_f32(a,b,n)`, int8 dot
  `dot_s8(a,b,n)` → int32 with SDOT (FEAT_DotProd: check `sysctl hw.optional.arm.FEAT_DotProd`; enable via
  `__attribute__((target("dotprod")))` in C and `.arch_extension dotprod` / `.arch armv8.2-a+dotprod` in .S — must
  build with the plain checker command). Three versions each: scalar C (compile with
  `__attribute__((optnone))` or `#pragma clang loop vectorize(disable)` so it stays scalar — verify in asm),
  intrinsics (`arm_neon.h`: vld1q_f32, vfmaq_f32, vaddvq_f32, vdotq_s32, vaddvq_s32), hand asm (.S: `ld1 {v0.4s}`,
  `fmla v.4s`, `faddp`/`faddv`? (no faddv for 4s on AArch64? check: `faddp` pairs; `fmaxv` exists — verify), `sdot
  v.4s, v.16b, v.16b`, `addv s`). Tail handling for n not multiple of 4/16 (n = 0..67 all tested). Float dot
  reassociation → compare with tolerance derived from n·eps·Σ|a_i b_i| (state the bound), exact for int8 (vs int64
  reference; also derive the int32-accumulator overflow horizon: worst product (−128)·(−128) = 16384, so one
  int32 lane holds ⌊(2^31−1)/16384⌋ = 131071 worst-case products — translate that into a max n for your lane
  layout and test right at / past the boundary with the asm vs int64 reference). Register-view figure (v0.16b / 8h / 4s / 2d lanes), SDOT lane
  diagram (4 bytes → 1 int32 per lane). demo_bench.c: GFLOP/s / GOPS for n = 1K, 64K, 4M (cache effects; median;
  hedge). Lane: no byte-search/mask tricks (M07), no auto-vectorization study (ARM-A11), no saturating/fixed-point
  (ARM-A07).
- **M07** (mac) NEON byte processing. `neon_memchr(p, c, n)` (CMEQ + mask extraction via `SHRN v.8b, v.8h, #4` →
  64-bit nibble mask + `rbit`/`clz` or `ctz`; explain why AArch64 has no PMOVMSKB and the alternatives: umaxv test
  then locate, addp tricks), `checksum_u8(p,n)` → uint32/uint64 byte sum (UADDLP/UADALP widening accumulate, explain
  overflow horizon of each accumulator width), `minmax_u16(samples, n, &mn, &mx)` (UMINV/UMAXV or vector umin/umax
  then reduce) — for 12-bit ADC samples. Both asm (.S) or intrinsics — at least memchr in .S. Correctness: n 0..300 ×
  alignment 0..15 × match position (none/first/last/every) vs scalar reference; **page-boundary safety**: mmap 2
  pages, PROT_NONE the second, buffer ending exactly at the page end — your memchr must not read past p+n into the
  guard page (aligned 16 B reads can't cross a page — explain the trick only if you implement aligned-down first
  load + masking; otherwise do tail scalar), and a test proving it. demo_bench.c vs scalar vs libc memchr/(sum loop)
  at 64 B, 4 KB, 1 MB (median, hedge). Firmware angle: UART frame delimiter search, packet checksum, ADC
  min/max/peak-to-peak. Cite ARM-B10 (scalar strlen/memcpy) and ARM-M06 (NEON basics) by id.
- **M08** (qemu, mps2-an385) Vector table in **assembly** + Thumb bit. `vectors.S` (or shared_vectors.S): initial SP,
  Reset, NMI, HardFault, MemManage, BusFault, UsageFault, SVCall, PendSV, SysTick (+ a few IRQ entries) via `.word`,
  in `.section .isr_vector,"a"`, handlers declared `.thumb_func` / `.type x,%function` so the linker sets LSB=1;
  reset handler in asm: copy .data, zero .bss, call main. Tests (firmware): read the table → every handler entry odd;
  `&handler` vs table value; table at 0x0 and SCB->VTOR reads 0 at reset; **relocate**: copy table to a RAM array
  aligned per VTOR rules (ARMv7-M: alignment ≥ table size rounded up to power of two, min 128 B — hedge, cite ARM
  ARM), write VTOR, DSB/ISB, patch the SysTick entry to a second handler at runtime, prove it's called (counter).
  Part B: clear the Thumb bit of one entry (e.g. SVCall or SysTick in the RAM table) → **INVSTATE UsageFault**
  (CFSR bit 17, UFSR bit 1) — with USGFAULTENA set (UsageFault handler) and without (escalates to HardFault, HFSR
  FORCED bit 30) — read and assert the real registers, then recover (fix entry + continue: e.g. the fault handler
  repairs the RAM entry and returns, or rewrite stacked PC/xPSR.T — whichever works for real; explain). Also
  `bx` to an even address from C (function pointer without the LSB) → INVSTATE. Show `arm-none-eabi-objdump -s -j
  .isr_vector` with real odd words, `nm` addresses. Mac fallback assemble. Cite BM-B08 (C vector table) — here it's
  asm + relocation + Thumb faults. No stack-frame decoding (M09), no full CFSR decoder (M12).
- **M09** (qemu) Exception entry/return. SysTick + SVC handlers in asm: entry stub picks MSP/PSP from EXC_RETURN bit 2
  (`tst lr, #4; ite eq; mrseq r0, msp; mrsne r0, psp`) and passes the frame pointer to C. Tests: stacked frame
  r0,r1,r2,r3,r12,LR,PC,xPSR matches known values (main loads sentinel values into r0–r3/r12 in an asm helper right
  before `svc #N`), stacked PC = address after the SVC (read SVC number via `ldrb [pc-2]` from the stacked PC — the
  classic SVC dispatcher: implement `svc_dispatch(frame)` with numbers 0..3, return value written to stacked r0),
  xPSR stack-alignment bit 9 (STKALIGN: force an SP that's 4-mod-8 before the exception and observe bit 9 set and the
  real padding — verify for real on QEMU, CCR.STKALIGN reset value on M3 r2p0+ = 1 hedge), Thread-mode PSP case
  (switch to PSP first, cite ARM-B14) → EXC_RETURN 0xFFFFFFFD vs MSP 0xFFFFFFF9 vs handler-nested 0xFFFFFFF1 (nest
  SysTick inside SVC via priorities, or SVC inside... check what's legal — SVC in a handler of higher/equal priority
  escalates to HardFault; that is a nice Part B fact, verify for real). `decode_exc_return(uint32_t)` C function
  (return mode/stack/FPU-frame bits: bit 4 = 0 → extended FP frame) with exhaustive-ish tests over valid values and
  rejection of invalid ones. FPU frame: run a variant on `-M mps2-an386` (Cortex-M4F): enable CP10/CP11 (CPACR), use
  an FP instruction in thread mode, take an exception → EXC_RETURN 0xFFFFFFE9/ED and 26-word frame (S0–S15, FPSCR,
  reserved) — real values; mention lazy stacking (FPCCR.LSPEN/ASPEN) hedged. gdb session (`run_qemu.sh gdb`) dumping
  the frame (`x/8wx $sp` at handler entry) — paste real transcript. No context switch (ARM-A01), no fault decoding
  (M12).
- **M10** (qemu) PRIMASK / BASEPRI critical sections. asm/inline-asm primitives: `uint32_t crit_enter(void)` (mrs
  primask; cpsid i; return old), `crit_exit(uint32_t)` (msr primask) — **nestable** (test nested enter/exit restore
  the right state, including when entered with interrupts already disabled), `basepri_set/get`, `BASEPRI_MAX`
  conditional-raise semantics (only raises), priority-mask helper that converts a logical priority to the
  implemented-bits value (QEMU implements 8 bits — make the conversion parameterized by `PRIO_BITS` and test both 3
  and 8; say what real AN385/STM32 have, hedged). Core experiment: SysTick (or a software-pended IRQ via NVIC ISPR on
  a free line, e.g. IRQ 6) increments a shared counter while main does read-modify-write on the same counter many
  times → **lost updates counted for real** without protection (use `-accel tcg,one-insn-per-tb=on` + a fast
  SysTick reload so interrupts land inside the RMW; report real counts), 0 lost with PRIMASK, 0 lost with BASEPRI
  when the ISR's priority is masked, and **still lost** when BASEPRI is set above (numerically ≤?) the ISR priority —
  get the direction right: BASEPRI masks priorities numerically ≥ BASEPRI (lower urgency); BASEPRI=0 masks nothing.
  Part B: a high-priority "motor" IRQ must keep running inside a BASEPRI section (measure: it fires) while low-priority
  is held off; `cpsid i` does NOT mask HardFault/NMI; the bug "crit_exit always does cpsie i" breaks nesting
  (injected, caught). Mention `__disable_irq` lacking a barrier/ordering note (cpsid is self-synchronizing on
  ARMv7-M? hedge; why CMSIS adds a "memory" clobber). Cite C-M14 (C mock PRIMASK), ARM-M11 (LDREX alternative),
  BM-M02 (priority grouping). No context switch, no RTOS port (RTOS-A owns).
- **M12** (qemu) HardFault analyzer. asm HardFault entry (MSP/PSP from EXC_RETURN) → `hardfault_c(uint32_t *frame,
  uint32_t exc_return)`; decoder `decode_fault(cfsr, hfsr, mmfar, bfar, out)` that names every set bit (MMFSR:
  IACCVIOL DACCVIOL MUNSTKERR MSTKERR MLSPERR MMARVALID; BFSR: IBUSERR PRECISERR IMPRECISERR UNSTKERR STKERR LSPERR
  BFARVALID; UFSR: UNDEFINSTR INVSTATE INVPC NOCP UNALIGNED DIVBYZERO; HFSR: VECTTBL FORCED DEBUGEVT) — bit positions
  per ARMv7-M ARM (double-check, hedge) — and only trusts MMFAR/BFAR when *VALID. The decoder is pure C and also gets
  exhaustive single-bit + random multi-bit tests. Faults to trigger for real on QEMU: divide by zero (CCR.DIV_0_TRP=1
  first; show that without it SDIV returns 0), unaligned (CCR.UNALIGN_TRP=1 with LDR; and LDRD/LDM unaligned always
  fault), bad address (PRECISERR + BFAR, e.g. 0x30000000 per BM-M13 measurement — re-verify), undefined instruction
  (`udf #0` / `.short 0xde00`), optionally execute-from-XN/0x0? only if real. For each: report the cause AND the
  faulting PC from the stacked frame; test asserts PC == the address of the known faulting instruction (use labels
  in asm helpers that perform the faulting access, e.g. `fault_div_insn:`), LR, and the decoded cause. To run all
  faults in ONE boot, the handler **skips** the faulting instruction (stacked PC += 2 or 4 by decoding the Thumb-2
  first halfword: top 5 bits 0b11101/0b11110/0b11111 → 32-bit), clears CFSR (W1C) and returns — explain why this is
  OK for a test harness but not for production (BM-M13 owns policy/reset). Both modes: faults escalated to HardFault
  (FORCED) and with SHCSR handlers enabled (MemManage/BusFault/UsageFault entries share the same analyzer). Also
  imprecise case explanation (write buffer; DISDEFWBUF in ACTLR) hedged — QEMU gives precise. gdb: `bt` from inside
  the handler doesn't show the faulting function → show how to reconstruct (`frame pc`, `info symbol`, addr2line).
- **M13** (qemu-system-aarch64 -M virt -cpu cortex-a53) AArch64 exception levels. Bare-metal image built in the VM
  (native aarch64 gcc/clang, `-ffreestanding -nostdlib -nostartfiles -mgeneral-regs-only -mstrict-align` (**MMU off
  ⇒ data accesses are Device memory ⇒ unaligned access faults** — explain), own linker script at 0x40000000 RAM (virt
  RAM base; check the QEMU `-kernel` load address for raw ELF on virt), output via **semihosting** (AArch64: `hlt
  #0xf000`, w0=op, x1=param; SYS_WRITE0 0x04, SYS_EXIT 0x18 with the 2-word param block {0x20026
  ADP_Stopped_ApplicationExit, code} — verify exit code propagates with `-semihosting-config enable=on,target=native`)
  or PL011 UART at 0x09000000. Tasks: print CurrentEL (bits [3:2]); install VBAR_EL1 (2 KB-aligned, 16 entries ×
  0x80: {Current EL with SP0, Current EL with SPx, Lower EL AArch64, Lower EL AArch32} × {Sync, IRQ, FIQ, SError});
  `svc #0x42` → handler reads ESR_EL1 (EC [31:26] = 0x15 for SVC from AArch64, IL bit 25, ISS imm16), ELR_EL1 (=
  address of the instruction after SVC — verify), SPSR_EL1 (M[3:0], DAIF), then ERET. Also `brk #N` (EC 0x3C, ELR =
  the BRK itself → handler must advance ELR by 4), an `hvc`/undefined `udf` (EC 0x00 unknown), and a data abort
  from an unmapped address (EC 0x25 same-EL, FAR_EL1, DFSC in ISS — record the REAL value QEMU gives).
  `decode_esr(uint32_t)` C function tested on known encodings. Part C: boot with `-M virt,virtualization=on` → starts
  at EL2: configure HCR_EL2.RW=1, SPSR_EL2 = EL1h with DAIF masked, ELR_EL2 = el1_entry, ERET → CurrentEL reads 1 (and
  `-M virt,secure=on` → EL3 if it works: SCR_EL3.RW/NS; else say so). Then drop to EL0 (SPSR_EL1 M=0b0000, ERET) and
  call back up with SVC (EL0→EL1 → "Lower EL AArch64" vector, offset 0x400). `run_qemu.sh [starter|solution|practice|
  el2|gdb]`, starter/solution/practice return firmware exit code. Mac fallback: `clang --target=aarch64-none-elf -c
  solution.S`. Lane: no IRQ/GIC/timer interrupts and no context save of all regs (ARM-A02), no MMU setup.
- **M14** (mac) Post-mortem from register dumps. Build three crashing programs (demo_crash_*.c, `-O1 -g
  -fno-stack-protector` etc. as needed; real AArch64 macOS processes) — (1) NULL struct member access (fault address
  = small offset like 0x18 → `offsetof` tells the member; `ldr x8, [x0, #0x18]` with x0 = 0), (2) stack overrun that
  overwrites the saved LR/FP (local buffer copy → `ret` to garbage → EXC_BAD_ACCESS with pc = attacker-ish pattern;
  check whether arm64 (non-arm64e) user binaries have PAC: they don't by default — verify; with arm64e/PAC the
  symptom changes — hedge), (3) use-after-free (deterministic: `MallocScribble=1` fills freed memory with 0x55 →
  pointer loaded from the freed struct = 0x5555555555555555 → crash; also mention `MallocPreScribble` 0xAA, Guard
  Malloc, ASan as the real tool). Capture REAL lldb batch transcripts (`lldb -b -o run -o 'register read' -o 'bt' -o
  'disassemble -p' -o 'memory read ...'`) into crash_*.txt data files (trim noise, keep real). The CHECKed coding task:
  starter.c/solution.c = a crash triage tool that parses those captured text dumps (and synthetic variants embedded
  in the test) → extracts pc, lr, fp, sp, fault address, the faulting instruction's base register + immediate offset
  from the disassembly line at pc, then classifies {NULL_MEMBER (base reg == 0, addr == imm → member name via an
  offsetof table), BAD_PC (pc not in __TEXT range / not 4-aligned / matches fill pattern), SCRIBBLE (address/reg
  contains 0x5555… or 0xAAAA…), UNKNOWN} and reports the source line by matching pc to a captured `image lookup -a`
  line. Tests: real dumps → correct class + member/line; malformed/partial dumps → graceful UNKNOWN. Write the
  "면접에서 말하는 순서" (crash reading procedure: fault addr → instruction at pc → base reg value → who set that reg →
  bt/fp chain → lr sanity) as a table. Cite ARM-B08/B09 (frames, lldb), ARM-M12 (Cortex-M analog), TOOL-M14 (atos).

## Interview flavour (additions to the ARM-B list)
x8 indirect result vs sret, HFA/HVA, who saves d8–d15 and why only 64 bits, Apple stack-arg packing pitfall when
calling asm from C, early-clobber, why `volatile` + `memory` on barriers, CNTVCT resolution vs cycle counters
(PMCCNTR not at EL0 on macOS), latency vs throughput, why NEON has no movemask, SDOT throughput math, VTOR
alignment, Thumb bit/INVSTATE, EXC_RETURN values, SVC number extraction, lazy FP stacking, PRIMASK vs BASEPRI vs
FAULTMASK, priority bits implemented, precise vs imprecise BusFault, skipping a faulting instruction, EL0–EL3 roles
(app/OS/hypervisor/secure monitor), ESR EC classes, ELR for SVC vs BRK vs abort, why MMU-off = Device memory.

## When done
Reply with: files written (paths), md line count, PASS counts for starter/solution/practice (and QEMU run results),
ASan/UBSan result (mac lanes), demo files + key real numbers, every from-memory spec fact you were unsure about, and
anything that did NOT reproduce as the item task predicted. Do NOT edit files outside ROOT/lessons/ARM/<your ID>/
(plus your private /tmp paths).
