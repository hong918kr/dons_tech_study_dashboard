# Lesson writer brief — track ARM (ARM 아키텍처·어셈블리), **Advanced** level (ARM-A01–A11)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering (read ALL, this file wins on conflicts):
1. ROOT/NOTES_SPEC.md — lesson rules (front matter, 12 headings verbatim and in order, svg rules, markers, `<details>`).
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md — base brief.
3. ROOT/curriculum/ARM_B_LESSON_WRITER_BRIEF.md — ARM track base: **asm file convention** (sources(v) = [v.c or
   harness.c] + [v.S] + all shared_*.S/.c; `.S` uses only `//` comments; SYM/FUNC macros `#ifdef __APPLE__`), Mac tool
   facts, QEMU lane recipe, quality bar.
4. ROOT/curriculum/ARM_M_LESSON_WRITER_BRIEF.md — Medium overrides, **Build conventions** (checker command lines,
   QEMU facts, VM toolchain) — still valid unless overridden here. Its lanes tell you what Don already did in Medium.
5. **This file** (Advanced overrides + per-item lanes).

## Who / why
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math, targeting firmware /
silicon-validation / robotics / AI-accelerator roles (Anduril, Rivian, Tesla, Apple, NVIDIA). He finished ARM-B01–B14
and ARM-M01–M14 (AAPCS64 deep, inline asm, CNTVCT microbench, NEON basics + byte processing, asm vector table +
Thumb bit, exception entry/return + SVC dispatch + FP frame, PRIMASK/BASEPRI, HardFault analyzer, AArch64 EL0–EL2 +
VBAR/ESR/ERET with sync exceptions only, crash-dump post-mortem). ARM Advanced = staff-level follow-ups: "PendSV
핸들러를 화이트보드에 써봐, FPU 켜져 있으면 뭐가 달라져?", "AArch64 IRQ 진입에서 뭘 저장해야 하고 GIC 에 EOI 안 하면?",
"MMU 켜는 순서는? MAIR 인덱스 틀리면?", "JIT 코드 쓰고 왜 IC IVAU 가 필요해?", "int8 GEMM 이론 피크 대비 몇 %야?",
"CRC32 명령 3-way 인터리브 왜 빨라?", "SQDMULH 가 포화하는 유일한 입력은?", "이 IRQ 세트 최악 지연 계산해봐",
"ADRP 범위는? GOT 는 왜 필요해?", "PAC 가 ROP 를 어떻게 막아?", "이 루프 왜 벡터화 안 됐어?".

## Overrides for Advanced
- Output: ROOT/lessons/ARM/<ID>/**2026-11-15**_<ID>_<english_snake_slug>.md + code files (ARM-B convention) + optional
  demo_*.c/.S/.sh/.gdb/.lldb/.py + captured *.txt. Front matter exactly `---\nid: <ID>\ntime: <90분–130분>\nupdated:
  2026-11-15\n---`. Title `# <ID> <title exactly as in curriculum/tracks/ARM.json>`.
- md **500–760 lines**, **≥3 svg** figures, **면접 Q&A ≥ 7** (aim 8–9; ≥2 staff-level follow-ups: "…라면 어떻게
  바꾸겠나?", "이 설계가 틀리는 경우는?"), 추가 연습 3–6 ending with `### 한 번에 검증` + `<!-- code: practice.* -->`
  + exact build/run one-liner. §3 `<!-- code: starter.* -->`, §6 `<!-- code: solution.* diff=starter.* -->`.
- Parts: A = core mechanic, B = faults / corner cases interviewers probe (every bug the task names is INJECTED and
  its outcome ASSERTED or shown with real output), C = firmware/SoC/product-realistic use.
- Tests: exhaustive where cheap, else fixed-seed xorshift (≥ a few thousand cases) vs a plain C reference + named
  edge cases. Each harness prints `N/M passed`, exit 1 on any failure. Mac solution/practice < 2 s at -O2 (< 90 s
  under ASan/UBSan); QEMU runs < 20 s each. **No wall-clock numbers inside CHECKs.** Benchmarks → demo_bench.c with
  REAL pasted output (median of ≥ 5 runs; paste `uptime` load next to it — 10 other writers + other sessions run in
  parallel, Mac load is ~10–15 now → hedge, prefer ratios; say which perf level ran if you can't pin it).
- Starter = solution with ONLY the TODO bodies stubbed (same line structure → diff highlights only changed lines);
  starter compiles warning-free, FAILS (exit ≠ 0), never hangs (bounded loops / QEMU `timeout`), never corrupts the
  stack (asm stubs keep the ABI and return a safe wrong value).
- **Bug-detection honesty**: inject ≥ 2 plausible mutants into a PRIVATE copy of solution.* (your scratch dir) and
  confirm the harness FAILS for each; report which. No mutants left in the lesson dir.
- **Self-contained**: no #include across lessons. Embed compact copies and label them: "(ARM-M13 shared_start 축약판)",
  "(ARM-B14 startup 축약판)", "(ALGO-M01 비트 단위 CRC 축약판)".
- Accuracy: anything computable is printed by your code before it appears in the md. Spec facts you can't open
  (ARM ARM DDI 0487 / DDI 0403, Cortex-M3/M4 TRM, GIC-400 TRM, AAPCS64, Apple docs) → state as you know them +
  "(문서 확인 권장)". WebFetch/WebSearch (load via ToolSearch) allowed; cite URL in 참고자료. **QEMU/CPU behaviour
  claims must be OBSERVED (probe it) or quoted from source with file:line — never assumed.** If something the item
  task predicts does not reproduce (e.g. no stale I-cache observed), say so plainly and show what you did see.

## Build conventions (checker builds exactly this)
- **Mac lanes** (A04, A05, A06, A08, A09, A10, A11, and A07's main lane): ARM-M convention —
  `cc -std=c11 -Wall -Wextra -Werror -O2 <sources> -o /tmp/<ID>_v` from the lesson dir, plus ASan/UBSan build
  `cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined <sources>`.
  `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk`. **No extra flags available** → enable
  ISA features with `__attribute__((target("dotprod,i8mm")))`, `target("crc")` etc. in C, `.arch_extension` /
  `.arch armv8.6-a+i8mm` in .S. If a lesson needs a library, the checker auto-adds `-lz` only when a source file
  contains the string `zlib.h`; nothing else. Starter/solution/practice must not need MAP_JIT entitlements beyond
  what a plain `cc` build gets (A04: verify).
- **QEMU lanes** (A01 Cortex-M3/M4, A02 + A03 AArch64 virt): `run_qemu.sh [starter|solution|practice|…]` exactly as
  ARM-M08/M09/M13 (copy sources to VM `/tmp/<ID>_work`, build, `timeout 20` QEMU, print firmware PASS/FAIL +
  `N/M passed`, last line `qemu exit=<n>`, script returns the firmware exit code: starter 1, solution/practice 0).
  Checker: `limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/<ID>/run_qemu.sh <v>'`. Firmware sources start
  `/* ENV: qemu */`. Mac fallback the checker runs: `clang --target=thumbv7m-none-eabi -c solution.S` (A01) /
  `clang --target=aarch64-none-elf -c solution.S` (A02, A03) — must assemble. Templates to copy from:
  ROOT/lessons/ARM/ARM-M09 (Cortex-M exception asm, mps2-an386 FP-frame variant) and ARM-M13 (AArch64 bare metal on
  virt: shared_start.S, shared_rt.c, arm_m13.ld, semihosting exit, EL2 variant).
- **Extra lane scripts** (checker runs them too; must exit 0): A07 `run_qemu.sh m4` (Thumb-2 DSP part),
  A09 `check_reloc.sh` (real tool output vs your decoder), A10 `run_vm.sh pac` (real PAC crash in VM), A11
  `check_vec.sh` (vectorization remarks). Each prints its own PASS lines and exits non-zero on mismatch.
- VM facts (probed 11-15): Lima `elinux` aarch64, kernel 7.0, gcc 15.2 + clang native, `aarch64-linux-gnu-gcc`,
  `qemu-aarch64` (user mode) + `qemu-system-aarch64/arm` 10.2.1, gdb-multiarch, arm-none-eabi-gcc 14.2.1. **The VM
  CPU exposes `paca pacg bti i8mm asimddp sme2`** (/proc/cpuinfo) → PAC/BTI can be exercised natively in the VM.
  Mac M6 sysctl: FEAT_CRC32, DotProd, I8MM, BF16, PAuth/PAuth2/PAuth_LR, BTI, SME/SME2 = 1. Mac home is mounted
  READ-ONLY in the VM (work in VM `/tmp/<ID>_work`). Never stop/restart the VM; wrap QEMU/gdb in `timeout`.
- Private temp only: Mac `<SCRATCH>/<ID>/` given in your prompt (or `/tmp/<ID>_work/`), VM `/tmp/<ID>_work/`. Never
  write to another lesson dir, curriculum/, site/, or run build_site.py / git. Don't install packages.

## Cross-references (check `ls ROOT/lessons/<TRACK>` before citing; all below exist unless marked)
ARM-B01–B14, ARM-M01–M14 (M04 LDXR/STXR, M05 barriers, M06 NEON basics incl. SDOT dot, M07 NEON bytes, M08 asm vector
table + VTOR, M09 exception frame/EXC_RETURN/FP frame on an386, M10 PRIMASK/BASEPRI, M11 LDREX on M3, M12 HardFault
analyzer, M13 AArch64 EL/VBAR/ESR sync only, M14 crash dumps). RTOS-A01 = full mini RTOS in C (naked inline-asm
PendSV, bitmap scheduler, PI mutex, BASEPRI) — **A01 must not redo it**. BM-A10 = single-exception cycle MODEL
(entry 12/tail-chain 6/late arrival/lazy FP), DWT measurement firmware for a board, simple response-time formula —
**A08 must go beyond it**. BM-A01 (CPACR/VTOR/SystemInit), BM-A08 (MPU), BM-A09 (.noinit crash dump), ALGO-M01/M02
(bitwise/table/slicing CRC), ARCH-A (check `ls`), TOOL-B04 (nm/objdump/otool), TOOL-B05/B07 (linking, check titles),
TOOL-B03 (optimization levels), TOOL-B12 (UBSan), ARCH-M06 (DCZID/cacheline), SIV-*. Not written (cite as "다음
단계" only): SEC track, DSP track, EML track, TOOL-M14/A03/A05.

## Lanes (stay in yours; the other 10 items are being written IN PARALLEL)
- **A01 PendSV context switch in assembly (qemu: mps2-an385 + an386 variant)**. Pure `.S` switcher (not inline asm —
  RTOS-A01 did that in C): `PendSV_Handler` saves r4–r11 on PSP, stores PSP into `cur->sp`, picks next (simple
  round-robin over 2–3 TCBs written in C is fine), restores r4–r11, `bx lr` with EXC_RETURN 0xFFFFFFFD; SysTick sets
  `ICSR.PENDSVSET`; first-task start via SVC or direct EXC_RETURN trick (pick one, explain the other). Core test =
  **register-preservation checker**: each task is an asm loop that loads a task-unique pattern into r0–r12 (and lr)
  and between yields/ticks verifies all of them, counting mismatches; two tasks alternate output via semihosting;
  assert ≥ K switches each and 0 mismatches. Initial-frame builder `init_stack(tcb, entry, arg)` in C tested (xPSR
  0x01000000, PC LSB cleared, LR = exit trap, 8-byte alignment). Part B (each injected for real, outcome asserted /
  pasted): forget r11 in the save list → mismatch caught; PendSV priority NOT lowest (e.g. same as SysTick or higher
  than a busy IRQ) → show what breaks (switch inside an ISR's frame → return to wrong context / fault — observe and
  explain what QEMU actually does); missing `isb` after `msr psp`? (probably not observable — say so); stack overflow
  of a tiny task stack detected by a canary. Part C on **mps2-an386 (Cortex-M4F)**: FPU-aware switch — tasks use
  s16–s31 patterns too; handler checks EXC_RETURN bit 4 and `vstmdbeq r0!, {s16-s31}` / `vldmiaeq`, stores the
  EXC_RETURN per task; show that skipping s16–s31 corrupts the other task's FP regs (real), and lazy stacking note
  (FPCCR.LSPEN/ASPEN, hedged; observe FPCCR reset value on QEMU). `run_qemu.sh [starter|solution|practice|fpu|gdb]`
  (solution must include the M3 lane; `fpu` = an386 variant, must also exit 0). Cite RTOS-A01 (C/inline version +
  scheduler), ARM-M09 (frame/EXC_RETURN), ARM-M10.
- **A02 AArch64 exception vectors + IRQ context save (qemu: virt,gic-version=2, -cpu cortex-a53)**. Build on M13
  (compact copy of its start/semihosting/linker script). `SAVE_ALL/RESTORE_ALL` asm macros: x0–x30 + ELR_EL1 +
  SPSR_EL1 (+ SP_EL0 if you drop to EL0) into a 16-byte-aligned frame (state the size, e.g. 34×8 = 272 → round to
  288?, and why alignment matters), `struct trap_frame` in C with `_Static_assert(offsetof…)` matching the asm.
  GICv2 on virt: distributor 0x08000000, CPU interface 0x08010000 (verify from QEMU `-M virt,dumpdtb=` + dtc in VM);
  enable group/priority mask/PPI. Generic timer: EL1 physical (CNTP_*, PPI 14 → INTID 30) or virtual (CNTV_*, PPI 11
  → INTID 27) — pick, verify which INTID fires on QEMU, set TVAL from CNTFRQ_EL0 (real value printed), count N ticks
  with `wfi`, IAR → handle → EOIR; spurious 1023 handled. Core tests: tick count reached; **frame integrity**: main
  runs an asm loop holding sentinel values in x0–x30 (except sp) and v8? (only if you save FP — default
  `-mgeneral-regs-only`, say why kernels do lazy FP) while IRQs fire; mismatches = 0; `decode_iar`, GIC register
  address helpers tested in C. Part B injected: no EOI → exactly 1 IRQ then silence (count asserted with a bounded
  timeout); forget to save x30/ELR before calling C → return goes wrong (observe real symptom, then assert detection
  path); IRQ unmasked (DAIF.I clear) inside the handler without saving ELR/SPSR first → nested IRQ clobbers them
  (show real); level-triggered timer not re-armed/masked → IRQ storm (counted). Part C: nested IRQ done right (save
  ELR/SPSR to the frame, then `msr daifclr, #2`) or a second timer source; latency in CNTVCT ticks is NOT meaningful
  on QEMU (say so). `run_qemu.sh [starter|solution|practice|gdb]`. Lane: no MMU (A03), no GICv3.
- **A03 AArch64 MMU on: translation tables, MAIR, TCR (qemu: virt, -cpu cortex-a53)**. Compact M13 start. 4 KB
  granule, TCR.T0SZ for a 39-bit (or 32-bit) VA, L1 1 GB blocks + L2 table with 2 MB blocks: RAM 0x40000000 Normal
  WB (MAIR attr 0xFF), UART/GIC Device-nGnRE (0x04), one 2 MB region XN, one read-only (AP[2]=1), one unmapped hole,
  one alias VA mapping the same PA. Enable order: MAIR → TCR (IPS from ID_AA64MMFR0_EL1.PARange, read & print) →
  TTBR0 → `dsb ish; isb` → TLBI VMALLE1 → SCTLR.M (+C,I) → isb; identity map so the PC survives. Descriptor builders
  in C (`mk_block(pa, attr_idx, ap, sh, xn, af)`, `mk_table`) + a **software page-walk** `walk(ttbr_tables, va,
  &pa, &attr)` tested on the same tables against expected results (exhaustive over block-aligned VAs + random
  offsets) — run both in firmware and (optionally) host. Firmware tests: alias write/read coherence, then faults via
  the M13-style sync handler that records ESR/FAR and skips the instruction: translation fault (DFSC level n — real
  value), permission fault on write to RO (DFSC 0x0F/0x0E…? record real), **instruction abort EC 0x21** when
  branching into the XN region (ELR = FAR = target; return to a recovery label), access-flag fault with AF=0 (if
  QEMU doesn't do HW AF update on a53 → fault; verify), unaligned access now allowed on Normal memory vs fault on
  Device (contrast with M13's MMU-off note — verify both for real). Part B injected: wrong MAIR index for UART →
  (what happens on QEMU? QEMU doesn't model cache attrs → likely nothing — say so honestly, explain what real HW does);
  missing AF bit; T0SZ mismatch with table level → translation fault at level 0/1; forgetting `isb` (likely not
  observable on QEMU — say so). `run_qemu.sh [starter|solution|practice|gdb]` (gdb: `info registers` of TTBR0/TCR or
  monitor `info mtree`/`info tlb` if available — paste real). Lane: no IRQs (A02), no EL2 stage-2.
- **A04 I-cache coherency: JIT code generation (mac)**. `mmap(MAP_JIT)` + `pthread_jit_write_protect_np(0/1)` +
  `sys_icache_invalidate`. Encoders in C (core CHECK): `enc_add_imm(rd,rn,imm12,sh)`, `enc_movz/movk`, `enc_ret`,
  `enc_b(off)`, `enc_bl`, `enc_cbz` with range checks — verified against real assembler output (assemble a reference
  .S with clang once, store the words as golden table in the test, plus exhaustive imm12 / random fields roundtrip
  with a decoder). Then JIT tests: generate `f(x)=x+imm` for many imm, call through a function pointer (with
  `ptrauth`? plain arm64 target → no; mention arm64e needs `ptrauth_sign_unauthenticated`), patch in place repeatedly,
  results correct WITH maintenance. Also implement the **manual sequence** in asm: read CTR_EL0 (verify it's
  readable at EL0 on macOS, print raw value, decode IminLine/DminLine/IDC/DIC), `dc cvau` per D-line, `dsb ish`,
  `ic ivau` per I-line, `dsb ish`, `isb` — test it works as a replacement for sys_icache_invalidate. Part B: omit the
  maintenance and patch a hot function in a loop → **record honestly** whether stale execution is observed on M6
  (with IDC/DIC bits explaining it; try multiple-core variant: write on one thread, execute on another); W^X violation
  (write while execute-protected) → real SIGBUS/SIGSEGV caught in a child process (fork) with the signal asserted.
  Part C: tiny "expression JIT" for a filter coefficient (e.g. compile y = (a*x + b) >> s into mul/add/asr words)
  checked vs interpreter on random inputs; why firmware cares (code in RAM, flash patching, bootloaders copying code
  → `IC IALLU` / Cortex-M7 `SCB_InvalidateICache` analogue, hedged). demo_stale.c holds the noisy experiments.
- **A05 int8 GEMM micro-kernels: SDOT, SMMLA (mac)**. C[M×N] int32 = A[M×K] int8 × B[K×N] int8. Versions: scalar
  reference (int64 accum check), **SDOT** micro-kernel (e.g. 8×8 or 4×16 tile with `sdot v.4s, v.16b, v.4b[i]`
  by-element, B packed 4-deep), **SMMLA** (I8MM: 2×8 · 8×2 → 2×2 int32 per 128-bit reg; pack A and B in 2×8 row
  pairs) micro-kernel, each in intrinsics or .S (at least one in .S). Packing routines (pack_A/pack_B) + edge handling
  (M,N,K not multiples of tile — zero-padding packed panels) tested for all M,N ≤ 17, K ≤ 37 combos (or a dense
  subset) + random larger shapes vs reference; overflow horizon of int32 accumulators (K ≤ 131071 worst case — derive,
  test at boundary with ±128 extremes via reference). demo_bench.c: GOPS (2·M·N·K) for 64/256/1024 cubes, scalar vs
  SDOT vs SMMLA vs (optional) Accelerate/BNNS? only if trivially available; theoretical peak computed from your OWN
  measured cycle assumption (e.g. measure SDOT throughput with an independent-chain loop like ARM-M03 and convert) —
  hedged, never quote Apple numbers you can't cite. Mention SME2 exists on M6 (FEAT_SME2=1) as next step, no code.
  Lane: no general NEON tutorial (M06), no float GEMM.
- **A06 CRC acceleration with ARMv8 CRC32 instructions (mac)**. CRC-32 (IEEE, reflected 0xEDB88320) and CRC-32C
  (Castagnoli 0x82F63B78) via `__crc32b/h/w/d`, `__crc32cb/…/cd` (arm_acle.h) and a hand .S loop (`crc32cx`),
  alignment head/tail handling. Reference: compact bitwise CRC (labelled "(ALGO-M01 축약판)") + table; check values
  "123456789" → 0xCBF43926 / 0xE3069283 printed by code; vs zlib `crc32()` (include `zlib.h` → checker adds -lz).
  Advanced core: **3-way (or N-way) interleaving + combine**: split buffer into 3 independent streams to hide the
  CRC32X latency, combine with `crc32_combine`-style GF(2) shift (implement zero-extension via x^(8n) mod P using
  PMULL (`vmull_p64`) or a 32×32 matrix-power method — implement one, test vs single-stream for all lengths 0..4096
  and random offsets/alignments). Tests exhaustive over small lengths × alignments. demo_bench.c: GB/s for 1 KB, 64 KB,
  1 MB: bitwise / table / slicing-8 (compact) / crc32 1-way / 3-way / zlib (median, hedge); derive the speedup ratio
  from latency/throughput reasoning and compare with measurement. Firmware angle: STM32 CRC unit vs CPU, Cortex-M
  lacks CRC instructions (M55/M85? hedge), NVMe/iSCSI/ext4 use CRC32C.
- **A07 Saturating & fixed-point: SQADD, SQDMULH, SSAT (mac + qemu M4)**. Q15 FIR (e.g. 16–32 taps). Define the
  exact bit-exact semantics in the C reference: product (a·b)<<1 saturating (only −32768·−32768 saturates in
  SQDMULH), accumulate in Q31 with saturation or in 64-bit then round & SSAT to Q15 — choose ONE pipeline and make
  every implementation match it bit-for-bit. Mac: NEON version with `vqdmulhq_s16` / `vqrdmulhq_s16` /
  `vqdmlal_s16` / `vqaddq_s16` / `vqmovn_s32` (intrinsics or .S). Tests: exhaustive over all 2^32 (a,b) pairs is too
  big for the 2 s budget → exhaustive over all a with b ∈ {edge set} + 2^20 random, plus every saturation boundary
  (±32767, −32768, sums that overflow Q31); FIR vs reference on random + adversarial signals (full-scale square
  wave). Part B: wrong rounding (SQDMULH vs SQRDMULH), non-saturating ADD wrap — injected, caught. **QEMU lane
  `run_qemu.sh m4`** (mps2-an386 Cortex-M4, `-mcpu=cortex-m4`): the same FIR with Thumb-2 DSP `SMLABB`/`SMULBB`,
  `QADD`, `SSAT #16`, `SMLAD` (dual 16-bit MAC, packing two Q15 in a word) in asm or ACLE intrinsics (`__ssat`,
  `__smlad`, `__qadd` from arm_acle.h with arm-none-eabi-gcc), bit-exact vs the same C reference compiled for M4,
  Q-flag (APSR.Q sticky) read via `mrs` and asserted; `run_qemu.sh m4` exits 0 on PASS. starter/solution/practice =
  Mac lane. Cite ARM-M06 (NEON basics). Lane: no FIR design theory beyond what's needed (DSP track).
- **A08 Cortex-M interrupt latency analysis (mac)**. BM-A10 owns single-exception timing + DWT measurement; you own
  the **system-level worst case**. Model: IRQ set {prio (preempt group + sub-priority via PRIGROUP), WCET C_i,
  min inter-arrival T_i}, critical sections {length, kind PRIMASK | BASEPRI(threshold)}, core params (entry 12,
  tail-chain 6, return/pop 10–12?, late-arrival, lazy-FP extra, flash wait states → hedge all, one params struct with
  cites). Analytic worst-case latency L_i = entry + blocking B_i (longest section that masks i; lower-priority ISRs
  only block via non-preemptible same group or masking) + interference from higher-priority ISRs (busy-period
  fixed-point iteration, include the response time R_i too). **Cycle-level discrete-event simulator** of NVIC
  behaviour (pending, preemption by group priority, tail-chaining, late arrival, sub-priority tie-break) as the
  ground truth: tests assert analytic bound ≥ simulated max over exhaustive arrival phases on small sets (and equality
  on constructed tight cases), plus simulator unit tests for each NVIC rule. Part B: before/after table (real program
  output) for optimizations: tail-chaining on/off, PRIMASK → BASEPRI, splitting a long ISR into top/bottom half via
  PendSV, sub-priority vs preempt priority, shortening the longest critical section; show which IRQ gains and which
  loses. Part C: realistic motor-control set (PWM 20 kHz current loop, encoder, CAN, UART, SysTick) — does the
  current loop meet its deadline? Interview "면접에서 말하는 순서" table. Cite BM-A10, ARM-M10, BM-M02, RTOS-A03.
- **A09 PIC and relocations: ADRP/ADD, GOT, @PAGE (mac + VM)**. Mac: asm accessing a local global via
  `adrp x8, _g@PAGE; add x8, x8, _g@PAGEOFF`, extern from a dylib via `@GOTPAGE/@GOTPAGEOFF`; build an executable +
  a dylib; read `otool -rv` / `/usr/bin/objdump -r -d` / `dyld_info -fixups` (if present; chained fixups) and explain
  each entry (ARM64_RELOC_PAGE21, PAGEOFF12, GOT_LOAD_PAGE21/PAGEOFF12, BRANCH26) — real pasted output. VM (ELF):
  same with gcc, `-fPIC` shared vs `-fno-pic -static`, `readelf -r` (R_AARCH64_ADR_PREL_PG_HI21, ADD_ABS_LO12_NC,
  ADR_GOT_PAGE, LD64_GOT_LO12_NC, CALL26, JUMP_SLOT/GLOB_DAT/RELATIVE) and linker GOT relaxation (ADRP+LDR → ADRP+ADD
  in the linked output — observe with ld.bfd/lld if it happens). Core CHECK (C): a **relocation applier/decoder**:
  `apply_adrp(insn, P, S+A)`, `apply_add_lo12`, `apply_ldst_lo12(insn, size)` (scaled imm12, alignment check),
  `apply_call26` with range errors (ADRP ±4 GB, CALL26 ±128 MB) and decoders back — tested by encode/decode
  roundtrip (random) + golden words taken from a real linked binary (captured once, stored in the test with the
  addresses). `check_reloc.sh` (Mac; may call into VM if you document it) rebuilds the tiny objects, dumps real
  relocations/linked words with the tools, and compares against your decoder's computation → exit 0. Firmware angle:
  why Cortex-M firmware is usually non-PIC, position-independent bootloader/app slots (`-fropi/-frwpi` hedged),
  veneers when CALL26 is out of range. Cite TOOL-B04/TOOL-B05.
- **A10 Pointer authentication & BTI: PACIASP, AUTIASP (mac + VM)**. Mac: compile with `-mbranch-protection=standard`
  (plain arm64) and `-arch arm64e`; disassemble and show where PACIASP/AUTIASP/BTI c/RETAB(?) land (real output),
  arm64e running is restricted for 3rd-party binaries (verify: try to run, paste the real result). **VM (real HW
  PAC/BTI exposed: paca pacg bti)**: `run_vm.sh pac` builds a tiny program with `gcc -mbranch-protection=standard`
  whose function overwrites its own saved LR on the stack (deterministic, no real overflow needed: write via a
  pointer derived from `__builtin_frame_address`) → with PAC: AUTIASP produces a non-canonical pointer → SIGSEGV
  (or SIGILL with FEAT_FPAC — record which, check kernel `/proc/cpuinfo` fpac?) caught in a forked child; without PAC
  (`-mbranch-protection=none`) the forged return succeeds (lands on a chosen function) — both asserted; BTI: indirect
  call (`blr`) to an address that is not a `bti c` landing pad in a BTI-enabled binary (GNU property note,
  `readelf -n`) → SIGILL — only if the kernel enforces it for the binary (static binary needs the note on all
  objects; verify for real, else qemu-aarch64 -cpu max user mode as fallback; say which worked). `run_vm.sh` exits 0
  only if all expected outcomes matched. Core CHECK (Mac C): a **branch-protection linter** over raw AArch64 code
  words: given functions (word arrays from real compiled objects captured as test data + synthetic ones) verify
  every non-leaf function starts with PACIASP/BTI c (or BTI jc) and every RET is preceded by AUTIASP or is RETAA/RETAB,
  classify violations; plus a **toy PAC model** (`pac_sign(ptr, modifier, key)` with a simple keyed hash in the top
  bits, VA size 39/48) to show why a forged LR fails auth and how sp as modifier blocks replay across frames —
  clearly labelled as a model (QARMA not implemented). Cite ARM-B08 (frames), ARM-M14 (crash dumps).
- **A11 Reading optimized compiler output: vectorization & unrolling (mac)**. Kernels: saxpy (float), histogram
  (uint8 → 256 bins), conditional sum (sum of a[i] where a[i] > t), plus int16 dot or a strided loop. For each, -O2 /
  -O3 / -Os with `-Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize` (real remarks
  pasted), and the key asm (vector width, interleave count, runtime alias check `vector.memcheck` block, epilogue).
  Fixes: `restrict` removes the alias check (show), `#pragma clang loop vectorize_width/interleave_count`, float
  reduction needs `-ffast-math` or `-fassociative-math` (or `#pragma clang fp reassociate(on)`) — show remark change;
  histogram conflict → manual 4 sub-histograms + merge (scalar but ILP) and/or NEON — measured. Core CHECK: all
  variants vs reference on random inputs incl. **overlapping buffers** (non-restrict version correct; restrict version
  called with overlap = UB → do NOT call it in the test; explain), exact for ints, tolerance bound for float
  reassociation. `check_vec.sh` compiles the kernels file with remarks and asserts the expected vectorized /
  not-vectorized decisions per function (grep remarks) → exit 0. demo_bench.c: ns/elem per variant (median, hedge).
  Cite TOOL-B03 (levels), ARM-M06 (NEON), ARCH lessons on ILP if present.

## Interview flavour
PendSV lowest priority & why, r4–r11 vs hardware-stacked regs, EXC_RETURN bit 4 & FP context, first-task start, GIC
IAR/EOIR/spurious 1023, PPI vs SPI, what an IRQ entry stub must save on AArch64, MMU enable order + identity map,
MAIR/attr index, TCR T0SZ/IPS, AF/AP/XN, ESR EC 0x21 vs 0x25, DFSC, TLBI/barriers, I/D coherency (DC CVAU → IC
IVAU → ISB), W^X, int8 GEMM packing & roofline, SDOT vs SMMLA, CRC32 latency hiding + combine, Q15 rounding/
saturation, SQDMULH corner case, worst-case latency = blocking + interference, PRIMASK vs BASEPRI cost, ADRP range,
GOT vs PC-relative, veneers, PAC modifier/key, BTI landing pads, why a loop didn't vectorize.

## When done
Reply (short): files written, md line count, PASS counts starter/solution/practice (+ QEMU / extra lane script
results), ASan/UBSan, mutants injected + caught, demo files + key real numbers (with load), every QEMU/CPU behaviour
you verified (and how), every from-memory spec fact you were unsure about, anything that did NOT reproduce as the item
task predicted. Do NOT edit files outside ROOT/lessons/ARM/<your ID>/ (plus your private scratch paths).
