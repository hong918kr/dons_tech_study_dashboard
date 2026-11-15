# Lesson writer brief — track ARCH (컴퓨터 구조), **Medium** level (ARCH-M01–M07, M09–M11, M13–M15)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering: ROOT/NOTES_SPEC.md (lesson rules) → ROOT/curriculum/LESSON_WRITER_BRIEF.md (base rules: output files,
headings verbatim and in order, svg rules, strict build, "never invent output") → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md
(Medium: deterministic PRNG tests, ASan/UBSan, demo_*.c, cross-target asm recipe, POSIX define) →
ROOT/curriculum/ARCH_B_LESSON_WRITER_BRIEF.md (ARCH track context, **shared toy ISA**, verify commands) → **this file**
(ARCH-M overrides). Read all of them.

## Who / why
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math, targeting firmware /
silicon-validation / AI-accelerator roles (Rivian silicon validation, NVIDIA, Apple, Anduril). ARCH Medium = the
mid/senior interview layer: "이 코드 CPI가 왜 1.4인가?", "4-way LRU로 바꾸면 어떤 미스가 줄어드나?", "VALID가 READY를
기다리면 안 되는 이유는?", "MPU 영역 겹치면 누가 이기나?". Every lesson = one interview-style coding problem, built as a
C model/simulator (or a real M6 measurement where the task says so), with checked tests.

## Track context / lanes
- ROOT/curriculum/tracks/ARCH.json — find your item. The lesson MUST do the item's `task` (every part of it).
- ARCH Basic B01–B14 are ALL written (ROOT/lessons/ARCH/ARCH-Bxx/) — cite them, don't re-teach: B02 NZCV,
  B05 priority encoder/CLZ, B06 two-phase clocking, B08 toy ISA assembler, **B09 single-cycle CPU (the base for
  M01/M02/M10)**, B10 AMAT, B11 memory map/bus fault, B12 bus wait states/prefetch, B13 timing slack.
- Already-written Medium/Advanced (cite, don't duplicate): ARCH-M08 false sharing, ARCH-M12 DMA cache coherency
  (clean/invalidate), ARCH-A04 MESI, ARCH-A05 memory-order litmus, SOCX-B03 AXI exclusive/EXOKAY, MEM-M04 coherence
  traffic, CPP/C/DS lessons. Not yet written but planned (mention by id as "다음 단계", don't build their content):
  BM-M01 NVIC register bits, BM-M02 priority design, BM-M08 DMA driver (circular/HT/TC), BM-A08 MPU on a real core,
  BM-A10 IRQ latency with DWT, ARM-M09 EXC_RETURN/stack frame, ARM-B05 CSEL, MEM-M03 multi-level cache, MEM-M06 RFO
  bandwidth, MEM-A08 roofline, SOCX-B02 AXI burst addr/WSTRB, SOCX-B06 width/protocol bridge, SOCX-M02 ID reorder
  buffer, RTLDV-B10/RTLDV-A04 valid/ready RTL + SVA, SIM-M01 cycle-approx model.
- The other ARCH-M items are being written IN PARALLEL by other agents right now. Stay in your lane. Where two items
  must agree (pipeline M01↔M02, cache M04↔M05↔M06↔M07), this brief FIXES the shared rules below — follow them
  exactly so the lessons are consistent; each lesson is still self-contained (no #include across lessons; embed a
  compact copy and say "ARCH-M04 모델을 축소해 넣었다").
- Host is **Apple M6** (arm64, 3 perf levels: 2 Super + 4 Performance + 6 Efficiency cores). Track text says "M2";
  say M6 / Apple Silicon when reporting measurements. Include `cc --version` first line + core type caveat in any
  benchmark table.

## Shared pipeline rules (M01 and M02 MUST both follow; M10 may reuse for its discussion)
- ISA = ARCH-B08/B09 toy ISA exactly (see ARCH_B brief). Embed B09's decoder compactly. 0x0000 = `ADD r0,r0,r0` = NOP.
- 5 stages IF ID EX MEM WB, one instruction per stage, in-order. Register file is **split-phase**: WB writes in the
  first half, ID reads in the second half → a value being written in WB is visible to ID in the same cycle.
- Registers read in ID; ALU + **branch compare + branch/JAL target resolution in EX**; dmem access in MEM; regfile
  write in WB. JAL writes rd (= PC+1) in WB like any other producer.
- **M01 baseline (no forwarding):** hazard unit in ID stalls the consumer (holding IF/ID, inserting a bubble into EX)
  until the producer is in WB. So RAW distance 1 → 2 stall cycles, distance 2 → 1, distance ≥3 → 0. r0 is never a
  hazard. Control: static predict-not-taken; a taken BEQ or any JAL, resolved in EX, flushes the 2 younger
  instructions (IF, ID) → 2-cycle penalty. BEQ's source registers are RAW consumers like any other.
- **HALT:** when HALT reaches EX (non-speculative point: all older branches are resolved), it flushes IF/ID and fetch
  stops. Simulation ends in the cycle HALT is in WB. Younger instructions never write regs/dmem.
- Cycle accounting: cycle 1 = first fetch of PC 0; total cycles = the cycle number in which HALT is in WB.
  Retired count includes HALT. With no hazards, N instructions take N + 4 cycles. CPI = cycles / retired.
- Speculative fetch beyond imem end (PC ≥ 256) returns NOP. Test programs are fault-free (faults in a pipeline =
  ARCH-M10's precise-exception topic; mention only).
- Pipeline diagram format: one row per dynamic instruction (incl. flushed ones), one column per cycle, cells
  `IF ID EX ME WB`; a stalled stage is repeated (`ID ID ID EX`), a flushed instruction ends with `xx`.
  Also print a summary line `cycles=.. retired=.. CPI=.. stalls=.. flushes=..`.
- Correctness oracle: final architectural state (r0–r7, dmem, retired count) must equal a single-cycle B09-style
  model on every test program, including a deterministic random-program fuzz (xorshift; random straight-line code +
  forward-only BEQs, ending in HALT, so it always terminates).
- **M02** adds (as config flags, so M01 behaviour is still reproducible): (1) forwarding EX/MEM→EX and MEM/WB→EX for
  both ALU inputs, BEQ compare inputs and ST data; (2) load-use: a consumer right after LD stalls exactly 1 cycle;
  (3) branch policies: `NT` (= M01 control exactly), `2BIT` = 16-entry table of 2-bit saturating counters indexed
  by PC & 15, init weakly-not-taken (01), consulted in **ID** (target known after decode): predicted-taken → redirect
  fetch from ID, 1-cycle penalty; misprediction found in EX → flush IF+ID, 2 cycles; JAL always redirects in ID
  (1 cycle) under 2BIT. Must hold: M02 with forwarding OFF and policy NT gives cycle counts IDENTICAL to M01's rules
  (check it on the benchmark programs). Benchmark ≥3 programs (e.g. array sum = load-use, dependent ALU chain,
  data-dependent branchy loop) → CPI table for {no-fwd, fwd} × {NT, 2BIT}.

## Shared cache-simulator rules (M04, M05, M06, M07)
- Byte addresses `uint32_t`; parameters: capacity C bytes, line B bytes, associativity W (all powers of two;
  direct-mapped = W 1). sets S = C / (B·W); offset bits = log2 B, index bits = log2 S, tag = rest.
- Trace record = `{uint32_t addr; char rw; uint8_t size}` (size ≤ B; accesses never straddle a line in tests unless
  a test is specifically about straddling).
- Stats: accesses, hits, misses, hit rate; M05 adds 3C: compulsory = first touch of that line address ever;
  capacity = miss that a fully-associative LRU cache of the same capacity ALSO misses; conflict = the rest
  (hits in FA-LRU). State the definition and note it's the standard Hill definition (Hill & Smith 1989) — and that
  "conflict" can go negative per-policy for non-LRU (explain if you see it).
- M04 = direct-mapped only (+ conflict-miss case studies: two arrays whose bases differ by a multiple of C, matrix
  column walk, stride = C/… ). M05 = N-way + LRU/FIFO/Random(fixed-seed xorshift)/tree-PLRU. M06 = write policies
  (state exactly what counts as DRAM read bytes and write bytes: line fill = B bytes read, dirty eviction = B bytes
  written, WT = store size bytes written per store (or per write-buffer drain if coalescing — define it), final
  flush of dirty lines reported separately). M07 = embeds a compact N-way LRU sim to explain measured GFLOP/s.

## Per-item lanes / notes
- **M01**: see pipeline rules. Parts: A = pipeline registers + no-hazard flow (N+4), B = RAW detection/stall
  counting (distance 1/2/3, r0, BEQ sources, ST data source, JAL as producer), C = loop programs (array sum,
  memcpy) → diagram + CPI, vs single-cycle cycle-time argument (B09 had CPI 1 at long clock; tie to B13 Fmax).
- **M02**: see pipeline rules. Explain why forwarding can't remove load-use; why the predictor helps loops (2-bit
  hysteresis: inner loop exit costs one mispredict, not two). Optional: always-taken/BTFN as an extra column.
- **M03 (real hardware)**: threshold-filter loop (sum of elements ≥ 128 over N≈1–16M random bytes / int32), sorted vs
  random, branchy vs branchless (CSEL via ternary or mask `-(x>=t) & x`). CRITICAL: clang -O2 may already emit CSEL
  or vectorize the branchy loop, making sorted == random. You MUST inspect `cc -O2 -S` (arm64) and make the "branchy"
  version actually branch (e.g. per-function `__attribute__((optnone))` is too much — prefer `-fno-vectorize
  -fno-slp-vectorize` + an opaque barrier `__asm__ volatile("" ::: "memory")` or `if (__builtin_expect(...))` in the
  body; whatever you do, show the asm proving a `b.lt/b.ge`/`tbz` in the loop and CSEL in the branchless one).
  Estimate mispredict cost (ns and cycles; get the clock via `sysctl hw.perflevel*` names or say "≈ 4+ GHz 추정"
  honestly). Pin nothing (macOS can't pin; use `taskpolicy`/QoS only if you verify it works). Repeat runs, report
  median of ≥5. Checked tests = correctness (branchy == branchless == reference on deterministic inputs); timing only
  in demo_bench.c. Mention `ARM-B05 CSEL` as cross-ref. Also: why `-O2` auto-branchless can be slower on sorted data.
- **M04**: see cache rules. Matrix traversal row vs column, stride sweep table, the "two arrays C apart thrash"
  case; show how adding a padding line fixes it. Tie to real parts: Cortex-M7 L1 D-cache (4-way, 32 B lines —
  mention as real-world contrast, it's not direct-mapped), many MCU flash caches/ART are small and simple.
- **M05**: see cache rules. Tree-PLRU bit update/victim rules exact for 4/8 ways; show a trace where PLRU ≠ LRU;
  Belady-style anomaly discussion for FIFO (increasing ways can increase misses — only claim it if you show one).
  Comparison table across ways {1,2,4,8,FA} × policies with 3C breakdown on ≥3 traces.
- **M06**: see cache rules. 4 combos WB/WT × WA/NWA + a write buffer (K entries, coalescing per line or not — your
  choice, defined) ; traces: memset of a big buffer, ring log buffer append (small writes), read-modify-write mix.
  Show DRAM read/write byte tables; explain why memset with WB+WA reads data it's about to overwrite (RFO) and how
  real cores avoid it (DC ZVA on AArch64 / write-streaming mode on Cortex-A/M7 — hedge details), cite MEM-M06.
  Firmware tie: WT for a DMA-shared log region vs WB + clean (ARCH-M12).
- **M07 (real hardware + sim)**: double or float matmul n ∈ {512, 1024, 2048} ijk / ikj / tiled (pick tile by
  reasoning, sweep a few). GFLOP/s = 2n³/time. n=2048 ijk may take long — measure once, note it. Optional ceiling:
  Accelerate `cblas_sgemm`/`dgemm` (`-framework Accelerate`, uses AMX/SME) — if you include it, label it clearly.
  Check vectorization differences via `-Rpass=loop-vectorize` or asm, so you don't attribute SIMD gains to cache.
  Sim part: small n (e.g. 64–128) address trace through the embedded LRU sim with M6-ish L1 parameters (say what
  you assumed: Apple doesn't publish all; hw.l1dcachesize / hw.cachelinesize from sysctl are real numbers → use them)
  to explain the miss-rate ordering. Checked tests = correctness of all variants vs reference on small n.
- **M09**: NVIC-like model: 32 IRQs, per-IRQ enable/pending/active, 8-bit priority with configurable implemented
  bits + PRIGROUP split (group priority decides preemption; subpriority only orders pending), global mask (PRIMASK
  style) and optionally BASEPRI. Discrete-event sim: arrivals at times, handler durations, a handler can set/clear
  pending of others. Implement preemption, nesting, tail-chaining (handler exit with pending eligible → next handler
  without returning to thread; model the saved-cycle difference symbolically, e.g. "entry 12 / tail-chain 6 cycles"
  as model parameters clearly labelled as Cortex-M3/M4 typical values — hedge), late arrival (higher-priority IRQ
  arrives during entry). Same priority + same group → lower IRQ number wins. Expected execution logs compared
  string-for-string. Lane: no NVIC register bit offsets (BM-M01), no stack-frame details (ARM-M09).
- **M10**: extend the toy ISA (strict superset: every B08 encoding keeps its meaning) using free opcodes
  0110–1110. Required: DIV (divide-by-zero → exception), a pair load/store `LDP/STP rd, imm(rs)` that loads/stores
  rd and rd+1 and requires an EVEN effective word address (misaligned → alignment fault; this is how a word-addressed
  machine gets a meaningful "미정렬" rule — say it mirrors LDRD/LDM-style alignment rules), SYSCALL imm (trap),
  ERET. Add system state: EPC, CAUSE (+ BADADDR for memory faults), vector table base (VBAR-like, in imem; one vector
  per cause or one entry + CAUSE dispatch — pick, justify), exception-in-handler policy (nested fault → double fault
  → halt). Rules: faults (illegal/DIV0/align/dmem range) are precise, EPC = faulting PC, no architectural side
  effect from the faulting instruction (re-execute after fix-up); traps (SYSCALL) EPC = PC+1. Test programs: handler
  that skips the faulting instruction (EPC+1), handler that fixes the operand and retries, syscall dispatcher.
  Discuss what precise exceptions cost in the M01/M02 pipeline (only in 풀이/Q&A).
- **M11**: DMA engine model (hardware side): channels with priority (fixed + round-robin among equals), descriptor
  `{src, dst, len, next, flags}` in a simulated memory, scatter-gather chain walking, flags: IRQ-on-complete per
  descriptor, src/dst increment vs fixed (peripheral FIFO), element size 1/2/4; mem→mem and periph→mem with a DREQ
  model (peripheral produces bytes at a rate, FIFO depth), completion + error IRQs (bad address / misaligned / len 0 /
  descriptor loop guard), channel stops on error with status (which descriptor, how many bytes done). Arbitration
  granularity = burst of N beats. Verify result buffers and IRQ sequence logs. Cache coherency → cite ARCH-M12, not
  re-done. Driver API (circular, HT/TC) → BM-M08.
- **M13**: cycle-level AHB-Lite (single manager) + APB (APB3/4 with PREADY) model. AHB: address phase overlaps the
  previous data phase; HTRANS IDLE/BUSY/NONSEQ/SEQ, HREADY low stretches the data phase (and holds the next address
  phase). APB: SETUP (PSEL=1, PENABLE=0) → ACCESS (PENABLE=1) until PREADY. Bridge: AHB transfer to APB region →
  bridge stalls HREADY while the APB access runs. Output ASCII waveform tables (cycle columns, one row per signal)
  and optionally a .vcd file (viewable in Surfer/GTKWave — say so). Compute: N back-to-back AHB writes with 0 wait →
  N+1 cycles; same to APB via bridge → per-access latency; table. Hedge spec details you aren't sure of
  (AMBA AHB-Lite IHI 0033, APB IHI 0024); don't invent signal semantics.
- **M14**: AXI4 5-channel model: VALID/READY handshake per channel with randomized (seeded) ready/valid delays,
  INCR bursts (AxLEN+1 beats, AxSIZE, address per beat; 4 KB boundary rule as a checker rule), multiple IDs with
  out-of-order responses across IDs and in-order within an ID, WLAST/RLAST. Protocol checker rules (each with a
  test that injects a violation and expects detection): VALID dropped before handshake; payload changed while
  VALID && !READY; WLAST on wrong beat; B before last W beat handshake (and AXI4: before AW handshake); RLAST
  wrong; same-ID response reordering; burst crosses 4 KB. Explain "VALID must not wait for READY" (deadlock) — a
  sim can't detect a combinational dependency directly; show the deadlock scenario instead. Lane: no exclusive
  access (SOCX-B03), no RTL/SVA (RTLDV), no WRAP/narrow/WSTRB detail beyond a mention (SOCX-B02).
- **M15**: ARMv7-M PMSAv7 MPU checker: 8 regions; SIZE field encodes 2^(SIZE+1) bytes, min 32 B; base aligned to
  size; 8 equal subregions with SRD bits for regions ≥ 256 B (smaller → SRD must be 0 / unpredictable — say what
  you model); AP (3 bits) priv/unpriv R/W table; XN; overlap → highest-numbered region wins; no match → privileged
  uses default map if PRIVDEFENA else fault, unprivileged faults; MemManage fault report (addr, access, reason).
  Validate region config (alignment/size errors). Tests: stack guard region (32 B no-access at stack bottom),
  flash RX-only, SRAM XN, peripheral region, overlaps, subregion holes. Mention ARMv8-M PMSAv8 (base/limit, 32-B
  granularity, no overlap allowed → fault) as the modern contrast. Real-core config → BM-A08. Hedge any encoding you
  aren't certain of and list it in your final reply.

## Code & language
- C for all: starter.c / solution.c / practice.c (+ demo_*.c), `cc -std=c11 -Wall -Wextra -Werror -O2`, CHECK harness,
  "N/M passed", exit 1 on failure. solution.c = starter.c with only TODO bodies filled (diff highlights them).
- Deterministic tests (fixed-seed xorshift), exhaustive where the task says so. No timing in CHECKs —
  benchmarks only in demo_bench.c (M03, M07; others optional). Benchmarks: report median of ≥5 runs, machine idle
  as far as you can tell (check `uptime` load average and report it).
- Inline asm / `__builtin_*` fine in tests and demos, never as the TODO answer.
- Asm recipes: arm64 `cc -O2 -S -o - demo_x.c`; Cortex-M `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb
  -O2 -S demo_x.c -o -` (freestanding headers only). Paste real output, trimmed.
- Use a private work dir for binaries/helpers: /tmp/esn_<ID>/ (other writers run at the same time — never write into
  a shared scratch path or into another lesson's dir).

## Depth & format
- Medium: md **450–650 lines**. Front matter `id`, `time:` realistic (60분–100분), `updated: 2026-11-11`.
- Output: ROOT/lessons/ARCH/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c/solution.c/practice.c (+ demo_*.c).
  Title line `# <ID> <title exactly as in ARCH.json>`.
- Parts: A = core model, B = the hard part interviewers probe, C = firmware/silicon-realistic use. Each Part has
  checked tests.
- 1. 문제 first paragraph: one line on why it matters (interviews, real chips).
- ≥2 inline ```svg figures (pipeline diagram / cache organisation / timing waveform / state machine / region map).
  svg rules per base brief (viewBox, width ≤ 760, currentColor + f-acc/f-ok/f-warn/f-mut, font-size 12–14,
  escape &lt; &gt; &amp;).
- 면접 Q&A **≥6** with real 꼬리질문. 추가 연습 3–6, answers in <details>, ending with `### 한 번에 검증` +
  `<!-- code: practice.c -->` + build cmd.
- Accuracy over volume. If unsure of a spec/datasheet detail, say it less specifically rather than invent it, and
  list what you were unsure of in your final reply.

## Verify (you MUST run)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    mkdir -p /tmp/esn_<ID>
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/esn_<ID>/st && /tmp/esn_<ID>/st   # builds, some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/esn_<ID>/so && /tmp/esn_<ID>/so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/esn_<ID>/pr && /tmp/esn_<ID>/pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/esn_<ID>/san && /tmp/esn_<ID>/san
    (same sanitizer run for practice.c)                                                         # clean
- Every demo_*.c must build with the command shown in the md (run it).
- Self-check the md: headings verbatim & in order (## 학습 목표, ## 1. 문제 with ### Part A — / ### Part B — /
  ### Part C —, ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수,
  ## 8. 면접 Q&A, ## 9. 추가 연습, ## 10. 핵심 정리, ## 참고자료); `<!-- code: starter.c -->` in §3,
  `<!-- code: solution.c diff=starter.c -->` in §6. No `## ` heading lines inside code fences other than real
  output. Paste only real output you ran.

## When done
Reply (short) with: files written, PASS counts starter/solution/practice, sanitizer result, demo files + what they
showed (key numbers), anything you were unsure about. Do NOT edit files outside ROOT/lessons/ARCH/<your ID>/.
