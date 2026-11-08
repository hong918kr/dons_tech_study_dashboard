# Lesson writer brief — 동시성 가이드 3층 "원자 연산과 메모리 순서"

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt). Context: Don is working through
ROOT/2026-11-08_concurrency_practice_guide.md layer by layer; layers 1–2 are written. Layer 3 = C-A02 (done),
C-A03 (done), OS-M08, CPP-A04, MCPP-M13, ARM-M04, ARM-M05, ARM-M11, RISCV-M04, LLAT-B06. The guide's idea for
this layer: the SAME concept seen one level lower each time — C11 → C++ → ARM instructions → RISC-V instructions
→ measured cost. These are the FIRST lessons in the MCPP / ARM / RISCV / LLAT tracks (no earlier items in those
tracks are written — anything you need from an earlier item, e.g. an ISS, you embed in your own lesson).

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY.
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base) → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md (Medium
   overrides) → ROOT/curriculum/C_A_LESSON_WRITER_BRIEF.md (Advanced overrides + "Concurrency items" bullet)
   → ROOT/curriculum/CONC1_LESSON_WRITER_BRIEF.md ("Overrides / extras" + "Verify" apply to you, not its lane
   list). C++ items also read ROOT/curriculum/CPP_LESSON_WRITER_BRIEF.md (.cpp markers, flags).
   Everything applies unless overridden below. Ignore any "Do NOT edit outside lessons/C/..." path.
3. ROOT/2026-11-08_concurrency_practice_guide.md §3 — your layer and its "면접 체크" bullets: your lesson must
   let Don answer the ones relevant to your item.
4. ROOT/curriculum/tracks/<TRACK>.json — your item (title/goal/task/tags/env). The lesson must do the `task`.
5. What is ALREADY taught (read the md + demos, cross-reference by id, do NOT re-teach):
   - ROOT/lessons/C/C-A02/ — compiler vs CPU reordering, `asm volatile("":::"memory")`, signal vs thread fence,
     a weak-memory simulator, DMA OWN-bit/doorbell ordering, demo_litmus.c.
   - ROOT/lessons/C/C-A03/ — stdatomic basics: atomic_flag spinlock, fetch_add counter, release/acquire publish,
     CAS max-watermark, triple buffer, Peterson needs seq_cst, demo_asm.c (arm64 + thumbv7em asm of atomics),
     demo_sb_litmus.c, TSan on relaxed misuse.
   - ROOT/lessons/C/C-M14/ (ISR↔main shared data, PRIMASK) and ROOT/lessons/OS/OS-B04/B05 (counter race,
     mutex/atomic throughput chart).

## Lanes (written IN PARALLEL — stay in yours, cross-reference others by id)
- OS-M08 (Mid, C11): PATTERNS, not primitives. (1) message passing with a relaxed flag → stress test on this
  Mac (many rounds, payload of several fields; count torn/stale reads) — report honestly whether the hardware
  reordering actually showed up and how often; if it never shows, say so and show that TSan still flags it
  (TSan = model violation, not observed reorder). (2) Double-checked locking lazy init: broken (plain pointer /
  relaxed) → fixed with acquire load + release store under mutex, and with `pthread_once` (macOS has no
  <threads.h>/call_once — say so; mention C11 call_once and C++ static-local magic statics). (3) one more real
  pattern of your choice (e.g. seq counter "config generation" publish, or one-shot init flag in a driver).
  Hardware litmus itself = ARCH-A05 / ARM-M05; C-A03 owns publish basics — start from where it stops.
- CPP-A04 (Adv, C++20, CPP flags `-std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti`):
  `std::atomic<uint32_t>` / `<uint64_t>` `fetch_add` / `compare_exchange` / load / store compiled with
  `clang++ --target=thumbv6m-none-eabi -mcpu=cortex-m0plus`, `thumbv7m-none-eabi -mcpu=cortex-m3`,
  `thumbv7em-none-eabihf -mcpu=cortex-m4` (+ arm64 host) `-S` → real asm table: inline LDREX/STREX loop vs
  `__atomic_fetch_add_4/_8` libcalls vs plain ldr/str + dmb; `is_always_lock_free` / `ATOMIC_INT_LOCK_FREE`
  values per target (print them via a static_assert-failing probe or `-dM -E`). Deliverable = a policy header
  (e.g. `isr_shared<T>`: static_assert(is_always_lock_free) or a documented fallback to a PRIMASK critical
  section, host-modelled) with host tests. volatile vs atomic for ISR sharing (volatile = no tearing guarantee,
  no RMW atomicity, no ordering). Say what M0 libatomic actually needs (who implements __atomic_* — usually you,
  by disabling IRQs; libgcc/compiler-rt status — verify or hedge).
- MCPP-M13 (Mid, C++20, host flags `-std=c++20 -Wall -Wextra -Werror -O2 -pthread`): standard rules that create
  happens-before: fence–fence, fence–op, op–fence synchronization ([atomics.fences]); release sequence via
  RMW (C++20 wording — only RMWs continue it); `std::atomic_ref` (works on this Mac's libc++ — verified by
  orchestrator) e.g. on a plain struct field / DMA buffer word. Deliverable task: a SMALL exhaustive SC-interleaving
  explorer (2–3 threads, a few ops each, modelled as step lists) that enumerates all interleavings and detects a
  data race on a non-atomic location under the C++ definition (two conflicting accesses not ordered by
  happens-before; compute hb from sb + sw edges). Run it on a racy version and a fixed version; then build the
  same program with real threads + TSan and compare (TSan agrees? misses?). Note SC enumeration cannot show
  weak behaviours (say so; litmus = OS/ARCH tracks, ARM-M05).
- ARM-M04 (Mid, C + GNU inline asm, arm64 host): hand-written `ldaxr/stlxr` loops for atomic_add, CAS
  (with `clrex` on failure path or explain why not needed), spinlock (with `wfe`/`sevl` optional, explain);
  pthread stress (exact final counts, mutual exclusion check); LSE versions `ldaddal`, `casal`, `swpal`
  (inline asm needs `.arch armv8.1-a` / `-march=armv8.1-a+lse`; check `sysctl hw.optional.arm.FEAT_LSE` = 1 on
  this Mac). Compare: what the compiler emits for C11 atomics with default flags vs `-mno-outline-atomics` /
  `-march=armv8.1-a` (verify Apple clang's default — on Apple arm64 LSE is baseline, so it may emit `ldaddal`
  directly; show real asm). Contention benchmark 1..8 threads (LL/SC vs LSE) in demo_bench.c with real numbers.
  Don't measure memory_order costs (LLAT-B06 owns that). Cross-ref RISCV-M04 (LR/SC) and ARM-M11 (M-profile).
- ARM-M05 (Mid, C + inline asm, arm64 host): Message Passing litmus in asm — writer: `str data; str flag`,
  reader: `ldr flag; ldr data` — versions: plain, `dmb ish` (both sides; also only one side), `stlr`/`ldar`,
  `ldapr` (FEAT_LRCPC present here), address/data dependency trick on the reader. Run many iterations with two
  pinned-as-possible threads (no affinity on macOS — say so) and report the REAL observed count of
  (flag==1 && data==0) per version. Also SB (store buffering) to show `dmb ish` vs `stlr/ldar` difference
  (stlr→ldar on different addresses is ordered on ARMv8 — RCsc; ldapr is RCpc — explain with what you observe).
  If plain MP shows 0 reorders on Apple M6, report that honestly (Apple cores are weak per arch but may not
  exhibit it) and explain why litmus absence ≠ guarantee. DSB/ISB part: table of situations (system register
  write then dependent instruction, SCTLR/TTBR/MPU changes, cache maintenance completion, before WFI, self-
  modifying/JIT code) + a REAL user-space macOS JIT demo: `mmap(MAP_JIT)` + `pthread_jit_write_protect_np` +
  `sys_icache_invalidate` running generated code twice with different instructions (show it works; explain
  that sys_icache_invalidate does dc cvau / ic ivau / dsb ish / isb — hedge if not verified). Cortex-M analogue:
  DSB+ISB after MPU/VTOR/CONTROL writes (ARMv7-M ARM). C-A02 owns the C-level fence choice.
- ARM-M11 (Mid, env qemu): Cortex-M3 bare metal on `qemu-system-arm -M mps2-an385` in Lima VM `elinux`
  (see "QEMU item" below). Thumb-2 LDREX/STREX `atomic_inc`, `cas`, `mutex_try_lock` (+ dmb). Force the ISR-in-
  the-middle path with gdb: break between LDREX and STREX, pend an interrupt (write NVIC ISPR or SCB ICSR
  PENDSVSET via gdb `set *(uint32_t*)0x...`), continue → ISR runs (it increments the same counter) → STREX
  returns 1 → loop retries → final count correct. Record the real gdb session (commands + output). Explain
  the architecture rule (ARMv7-M ARM: local monitor cleared on exception entry/return — find the section and
  quote it; CLREX's role in a context switch on A-profile vs M-profile). IMPORTANT: check how QEMU models the
  monitor (QEMU implements STREX as compare-of-value against the LDREX value, and may or may not clear on
  exception entry) — test it, report what QEMU did, and state silicon vs QEMU differences. Also: Cortex-M0
  (v6-M) has no LDREX → show the build error / alternative (PRIMASK critical section), cross-ref CPP-A04.
  Checked starter/solution/practice: host-side C model of the exclusive monitor + the three primitives with
  a scripted "exception at step k" injector (deterministic, runs on Mac) — the real Thumb-2 asm + QEMU/gdb run
  are demo files (`/* ENV: qemu */` first line) with real pasted output.
- RISCV-M04 (Mid, mac C): self-contained tiny RV32I+A ISS (only the instructions your programs need: e.g.
  addi/add/lw/sw/beq/bne/jal/jalr/lui, lr.w/sc.w (+aq/rl bits accepted), amoswap.w/amoadd.w, fence) with
  2 harts in round-robin interleaving AND a seeded random scheduler; reservation set (pick granularity, say
  which), invalidated by another hart's store/AMO/SC to the set; SC fails also when no reservation; optional
  spurious-failure injection. Programs (as machine code): AMOSWAP spinlock, LR/SC CAS, ticket lock (amoadd);
  stress with many seeds: mutual exclusion + exact count. Constrained LR/SC loop rule (unprivileged spec
  "A" chapter: ≤16 instructions, only base I, no loads/stores/backward jumps... — quote it from the spec text
  and cite version) and a deliberate violation case. Encodings: write a small encoder in C (functions per
  instruction format) and CROSS-CHECK every encoding against a real assembler: Mac clang has NO riscv backend;
  Lima VM `elinux` clang 21 does (`clang --target=riscv32 -march=rv32ia -c x.S && llvm-objdump -d`) — run it in
  the VM in /tmp (mount is read-only) and paste the real objdump. Optionally run the same asm on
  `qemu-riscv64` user mode in the VM (rv64 variant) as a sanity check. Compare with ARM LDXR/STXR/LSE
  (cross-ref ARM-M04): table of LR/SC vs LDXR/STXR vs AMO vs LSE.
- LLAT-B06 (Basic, mac C): measured costs on Apple M6: single-thread latency (dependent chain) of plain add,
  `fetch_add` relaxed / acquire / release / acq_rel / seq_cst, `load` seq_cst vs acquire, `store` seq_cst vs
  release, CAS loop, LSE `ldadd*` vs LL/SC (`-mno-outline-atomics`/inline asm variant if the compiler only emits
  one form — show the real asm for each measured function); contended throughput 1..8 threads (same cache line)
  vs per-thread counters; svg chart from REAL numbers. Then the hot-path techniques with measured effect:
  per-thread/per-core counters + aggregate, batching (local accumulate, one fetch_add per N), relaxed where
  enough, avoid CAS loops when fetch_add/fetch_or works, read-before-CAS (test-and-test-and-set). False sharing
  = see:ARCH-M08 (mention only). Basic level — explain timer/measurement method simply (clock_gettime_nsec_np,
  repeat & median, warm-up). Checked tests: correctness of the techniques (exact totals), never timings.

## QEMU item (ARM-M11) — Lima VM `elinux` (aarch64 Ubuntu, Running)
- Tools present in the VM: `qemu-system-arm` 10.2.1, `arm-none-eabi-gcc` 14.2, `gdb-multiarch` 17.1, clang 21.
  Run via `limactl shell elinux -- bash -lc '...'`. The Mac home is mounted READ-ONLY in the VM → copy sources to
  `/tmp/ARM-M11_work/` in the VM and build/run there; copy results back by printing them (cat) — don't try to
  write into ROOT from the VM.
- Bare metal: vector table + reset handler + linker script (mps2-an385: flash 0x00000000, SRAM 0x20000000 —
  check QEMU's board docs/source for the map you use), semihosting output (`-semihosting-config enable=on,target=native`
  or UART0 of the board), `-nographic`, exit via semihosting SYS_EXIT so runs terminate. gdb: `-s -S`, scripted
  with `gdb-multiarch -batch -x cmds.gdb` so the session is reproducible; paste real output.
- Demo files keep `/* ENV: qemu */` (or `@ ENV: qemu` for .S, `/* ENV: qemu */` for .ld) as first line.
  Ship a run script (e.g. run_qemu.sh) with the exact commands.

## Overrides for this set
- Output: ROOT/lessons/<TRACK>/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + starter + solution + practice
  (.c, or .cpp for CPP-A04 / MCPP-M13) (+ demo_*). Front matter `updated: 2026-11-08`; `time:` Basic 50–70분,
  Mid 70–100분, Adv 90–120분. Title line `# <ID> <title exactly as in the track JSON>`. md length: Basic 450–600,
  Mid 500–700, Adv 550–750 lines. Q&A ≥5 Basic, ≥6 Mid/Adv.
- Inline asm: GNU extended asm inside the .c so starter/solution diff works; constraints and clobbers correct
  ("memory" clobber where ordering matters, "cc" when flags change). Build strict (`-Werror`) like the others.
  Every asm claim in the md = instruction you saw in real `-S`/objdump output. Show the commands.
- Concurrency tests: deterministic CHECKs only (exact counts, mutual exclusion counters, explorer results).
  Observed-reorder counts and timings are demo output (several runs, real numbers, machine info:
  `sysctl -n machdep.cpu.brand_string hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu`, `cc --version` line 1).
  Starter must FAIL (exit 1) without hanging — TODO stubs must not spin forever (bounded retries or stubs
  that return a wrong value).
- TSan: hand-written asm atomics are invisible to TSan (not instrumented) — for asm lessons say so and
  rely on the stress CHECKs; run ASan/UBSan + TSan anyway and report.
- Firmware tie-in: 1. 문제 first paragraph + Part C realistic (dual-core MCU / Cortex-A + M mailbox, ISR↔main
  counters, DMA descriptor publish, driver lazy init, telemetry counters in a hot ISR, RISC-V MCU like ESP32-C3 /
  GD32VF103 — check whether a given part actually implements the A extension before claiming it).
- Accuracy: cite Arm ARM (DDI 0487 for A-profile, DDI 0403 ARMv7-M, DDI 0419 ARMv6-M) section names, RISC-V
  unprivileged spec version + chapter, C11 7.17 / 5.1.2.4, C++ [intro.races] / [atomics.fences] /
  [atomics.order]. Hedge anything you didn't check ("문서 기준, 미검증", "대표값").
- PRIVATE scratch dir for helpers: /tmp/<ID>_work/ (Mac and VM). Never write helpers in a shared folder.

## Verify (you MUST run) — same as CONC1 brief "Verify" (C++: `c++` with the item's flags):
strict build; starter FAIL exit 1 no hang; solution + practice PASS exit 0, run solution 5+ times; ASan+UBSan
clean; TSan clean if threads. Plus the md self-check (headings verbatim & ordered, ≥2 ```svg with viewBox /
width ≤760 / currentColor / f-* classes only, escaped &lt; &gt; &amp;, Q&A count, 3–6 추가 연습 + `### 한 번에 검증`
+ `<!-- code: practice.c -->` (or .cpp), starter marker in §3, `<!-- code: solution.c diff=starter.c -->` in §6,
demo markers). ENV-marked (qemu) files are verified in the VM instead, with real output pasted.

## When done
Reply (short): files written, PASS counts starter/solution/practice, ASan/UBSan + TSan results, demo files +
what they showed (key numbers), anything unverified/unsure (list — the orchestrator copies it into the
guide's 작성 메모 table: env limits + unverified claims). Do NOT edit files outside ROOT/lessons/<TRACK>/<ID>/
(don't run build_site.py).
