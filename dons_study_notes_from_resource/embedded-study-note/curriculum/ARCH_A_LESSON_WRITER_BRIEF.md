# Lesson writer brief — track ARCH (컴퓨터 구조), **Advanced** level (ARCH-A01–A03, A06–A12)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering (read ALL, in order): ROOT/NOTES_SPEC.md (lesson rules) → ROOT/curriculum/LESSON_WRITER_BRIEF.md (base rules:
output files, headings verbatim and in order, svg rules, strict build, "never invent output") →
ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md (deterministic PRNG tests, ASan/UBSan, demo_*.c, cross-target asm recipe,
POSIX define) → ROOT/curriculum/ARCH_B_LESSON_WRITER_BRIEF.md (ARCH track context, toy ISA) →
ROOT/curriculum/ARCH_M_LESSON_WRITER_BRIEF.md (ARCH Medium: code/verify rules, benchmark rules) → **this file**
(ARCH-A overrides). Everything in the earlier briefs applies unless overridden here.

## Who / why
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math, targeting firmware /
silicon-validation / AI-accelerator roles (Rivian silicon & sensing validation, NVIDIA, Apple, Anduril). ARCH Advanced =
staff-level interview + real chip-team judgement: "TLB 미스 하나에 메모리 접근 몇 번?", "ASID가 8비트면 257번째 프로세스는?",
"ROB 없이 Tomasulo만 있으면 왜 정밀 예외가 깨지나?", "비동기 FIFO 포인터를 왜 Gray로?", "race-to-idle이 항상 이기나?",
"리셋을 PLL lock 전에 풀면?", "라운드로빈 최악 대기 시간은?", "이 커널은 메모리 바운드인가?", "NPU가 DRAM 보안 영역에 접근
못 하게 하려면?". Follow-ups probe *why this design, what it misses, how you'd know it's working, what it costs*.

## Track context / lanes
- ROOT/curriculum/tracks/ARCH.json — find your item. The lesson MUST do the item's `task` (every part of it).
- ARCH Basic B01–B14 and Medium M01–M15 are ALL written (ROOT/lessons/ARCH/). Assume known; cross-reference by id,
  never re-teach. Useful owners: B05 priority encoder/CLZ · B06 clocking · B09 single-cycle CPU · B10 AMAT ·
  **B11 memory map / address decoder / bus fault (A12 builds on it)** · B12 wait states · B13 timing slack/Fmax ·
  B14 reset cause · M01/M02 pipeline/hazards/forwarding/branch prediction (A06 builds on them) · M04–M07 caches ·
  M08 false sharing · M09 NVIC · M10 precise exceptions in the toy ISA (A06 cites) · M11 DMA engine (+ its channel
  arbitration; A10 goes deeper) · M12 DMA cache coherency (A12 cites) · M13 AHB/APB · M14 AXI valid/ready · M15 MPU.
- Already-written Advanced: **ARCH-A04 MESI simulator** and **ARCH-A05 memory-order litmus on Apple Silicon** — use
  them as the depth/style reference (ROOT/lessons/ARCH/ARCH-A04/). Other written lessons you may cite by id:
  MEM-M04, MEM-M07, RISCV-A04, SOCX-B03, SOCX-M05, SIV-A03 (power characterisation fit), SIV-A04 (thermal/throttle),
  SIV-A06 (board power sequencing & clocks), SIV-A11 (clock & power domain validation), SIV-A10, HWLAB-M08 (jitter),
  OS-M07/OS-A01, TEST-A05 (deterministic simulation). Skim the md of any you cite so the reference is accurate and your
  lesson does not duplicate it (A08 vs SIV-A03; A09 vs SIV-A06/SIV-A11 — read those two before writing A09).
- Planned, not written (mention by id as "다음 단계", don't build their content): MEM-A08 / AIPERF-B04/B06/M07 roofline,
  RTLDV-B06 reset synchronizer RTL, RTLDV-M03 arbiter RTL, RTLDV-M05/M06 CDC + async FIFO RTL, RTLDV-M07 static CDC,
  SIM-M05 interconnect contention, SOCX-B01 topologies, SOCX-B11 Q/P-channel, SOCX-M02 ID reorder, SOCX-M12 SMMUv3,
  SOCX-A10 chiplet SoC, NPU-A07 NPU power, AIPERF-A08 DVFS, BM-B03 RCC, BM-M11 low-power modes.
- The other ARCH-A items are being written IN PARALLEL by other agents right now. Stay in your lane. A01 and A02 must
  agree — this brief FIXES their shared format below; each lesson is still self-contained (no #include across lessons;
  A02 embeds a compact copy and says "ARCH-A01 모델을 축소해 넣었다").
- Host is **Apple M6** (arm64; 3 perf levels: 2 Super + 4 Performance + 6 Efficiency; hw.pagesize = 16384;
  hw.cachelinesize = 128; hw.l1dcachesize = 98304; hw.l2cachesize = 8388608; 32 GB). Track text says "M2"; say
  M6 / Apple Silicon. NOTE: the machine is shared with other sessions (load average ≈ 6–7 right now) — every benchmark
  table must state `uptime` load average at measurement time and be honest about noise.

## Shared MMU rules (A01 and A02 MUST both follow exactly)
- Model = "ARMv8-A VMSA 4KB granule을 단순화한 교육용 포맷" — say so; do not claim real ARM descriptor bit positions.
- 4 KB pages, 9 index bits per level, 512 × 8-byte descriptors per table page. Configurable `levels` ∈ {2,3,4} →
  VA bits = 12 + 9·levels (30 / 39 / 48). VA bits above that must be zero, else ADDRESS_SIZE fault (no walk).
  Level numbering: the LAST level is always L3; a 4-level walk is L0→L1→L2→L3, 3-level L1→L3, 2-level L2→L3.
- Physical memory: simulated byte array of frames; table pages come from a simple frame allocator; root = "TTBR".
  A walk reads one 8-byte descriptor per level from that simulated memory and counts it (`walk_reads`).
- 64-bit descriptor (own layout, document it in a table): bit0 V (valid), bit1 T (1 = points to next-level table;
  0 = block/leaf; at L3 a valid entry must have T=1 meaning "page" — T=0 at L3 is an invalid encoding → TRANSLATION
  fault, mirroring ARMv8's L3 rule), bit2 R, bit3 W, bit4 X, bit5 U (user-accessible), bit6 A (access flag; A02 may
  ignore), bit7 G (global — used by A02 TLB; A01 just stores it), bits 12..47 output address (frame / block base).
- Block (leaf at non-last level) allowed at L1 (1 GB) and L2 (2 MB) only; a leaf at L0 → TRANSLATION fault (invalid).
  Block output address must be aligned to block size (map API refuses otherwise).
- Permission check on the leaf only (no hierarchical APTable; mention ARM's APTable/x86's AND-of-levels as contrast).
  Access = {READ, WRITE, EXEC} × {USER, KERNEL}. USER access to U=0 → PERMISSION fault. KERNEL may access U=1 data
  (no PAN) but KERNEL EXEC of a U=1 page → PERMISSION fault (PXN-like; say "SMEP/PXN 개념"). W without R is allowed
  in the format but the map API rejects it (document).
- Fault report = {type ∈ {NONE, ADDRESS_SIZE, TRANSLATION, PERMISSION}, level at which it was detected, va,
  access}. Success = {pa, leaf level, page/block size, perms}.
- Map API: `map(va, pa, size, perms)` uses the largest aligned block that fits (2 MB / 1 GB) unless a flag forces
  4 KB pages; `unmap(va, size)`; splitting a block on partial unmap/remap is A01 Part B material (A01 decides whether
  to implement split or reject with error — must be explicit and tested).

## Per-item lanes / notes
- **A01 (C)**: Part A = walker + map API on 3-level and 4-level configs. Part B = block mappings and their edge
  cases (alignment, block-vs-table conflict, splitting or rejecting a partial unmap, 2-level config where only L2
  blocks exist), the 4 fault types each detected at the right level, walk_reads per translation (1 per level, fewer
  for blocks). Part C = a firmware/OS-realistic layout: kernel image RX + data RW-XN, user text/heap/stack with a
  guard page, device MMIO region (mention memory attributes/MAIR as out-of-scope), identity map for early boot —
  table of accesses and expected faults. Compare page-table memory overhead 4 KB pages vs 2 MB blocks for mapping
  1 GB. Cite MEM track / OS where relevant; A02 is "다음 단계".
- **A02 (C)**: embed a compact A01 walker (same format). TLB: fully associative, N entries (param), replacement LRU /
  FIFO / random (fixed-seed xorshift) param; entry = {valid, asid, vpn at the leaf's granularity, leaf size, pa base,
  perms, global}. Global entries match any ASID. TLB hit must give exactly the same result as a walk (CHECK on random
  addresses: cached result == fresh walk). Part A = TLB + stats (hits, misses, walks, walk_reads). Part B = two
  processes alternating (trace with a context switch every K accesses, K sweep) — full flush on switch vs ASID
  tagging: TLB misses and page-walk count/reads table; global kernel pages surviving flush-by-ASID. Correctness
  hazards: unmap without TLB invalidate → stale translation (show the bug, then fix with invalidate-by-VA+ASID);
  ASID reuse without flush → process B sees A's mapping (show, fix). Part C = ASID allocator with small width (e.g. 8
  bits, use 4 bits in a test to force rollover) + generation counter, rollover → global flush (Linux arm64-style; hedge
  details), measure flushes per 1000 switches vs number of processes. Mention page-walk caches, huge pages raising TLB
  reach, ARM TLBI broadcast + DSB/ISB, x86 PCID as contrast (only in 풀이/Q&A).
- **A03 (C, real hardware)**: macOS 16 KB pages (verify via `sysconf(_SC_PAGESIZE)` / `vm_page_size`). Part A =
  first-touch vs re-touch: mmap anonymous N pages, time touching one byte per page the first time (zero-fill fault)
  vs second time; per-page ns (median of ≥5 runs, fresh mapping per run); also show the effect of touching with a
  write vs read first (read of an untouched anonymous page may map a shared zero page then fault again on write —
  measure, don't assume). Part B = TLB reach: pointer-chase over P pages with one (or a few) cache line(s) per page, in
  a random cyclic order (single cycle — checked), P sweep (e.g. 4 … 65536 pages); keep the touched data cache-small
  where possible (vary the line offset per page to avoid cache-set aliasing — explain) so the latency rise is TLB, not
  cache; plot ns/access vs P (ascii or svg from the real numbers). Find knees and give the page count → "TLB coverage
  ≈ P × 16 KB" interpretation; do NOT state Apple's TLB sizes as fact (not published; may cite as "공개 측정치에 따르면"
  only if hedged). Part C = what firmware/OS does with this: huge/large pages (macOS user space has no general
  superpage control on arm64 — say what you verified), prefaulting (MAP_POPULATE doesn't exist on macOS — use
  memset/touch or `madvise(MADV_WILLNEED)` and measure whether it helps), mlock. Checked tests = correctness only
  (permutation is a single cycle covering all P, index math, page rounding). Timing only in demo_*.c. Report core type
  caveat (macOS scheduler may move you between P/E cores; QoS via `pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0)`
  — use it and say it is a hint, not pinning).
- **A06 (C)**: own small register ISA (not the toy ISA — say why: need multi-cycle FUs): ops LD, ST (optional), ADD,
  SUB, MUL, DIV, with FU latencies as params (e.g. ADD 2, MUL 6, DIV 12, LD 3 — labelled 가정). Tomasulo with
  reservation stations per FU class, register status/rename table pointing to ROB entries, single CDB (one broadcast
  per cycle, oldest first), ROB of size R, in-order commit (≤1 or W per cycle, param). Output per-instruction timeline
  table issue / exec-start / exec-end / write-CDB / commit (+ a pipeline-diagram-style svg from one real run).
  Part A = WAR/WAW eliminated by renaming (show a sequence where in-order scoreboard would stall and Tomasulo doesn't;
  cycle counts vs an in-order reference). Part B = precise exceptions: DIV-by-zero (and/or LD out-of-range) flagged at
  execute, taken at commit → flush younger, architectural regs/memory == sequential reference interpreter executed up
  to the faulting instruction (CHECK on random programs, fixed seed). Also show "Tomasulo without ROB" (write regfile
  at CDB time) produces an imprecise state on the same program. Part C = structural limits: ROB size / RS count sweep
  → IPC table on 2–3 kernels (dependent chain vs independent ops vs mixed); branch misprediction recovery
  mentioned/optionally modelled as flush-at-commit. Cite ARCH-M01/M02/M10. Mention real cores (Apple/Cortex-A big
  ROBs, Cortex-M in-order) hedged.
- **A07 (C)**: discrete-event sim of two clocks (e.g. write 100 MHz, read 77 MHz, ratio not rational-friendly; plus
  faster→slower and slower→faster cases). Metastability model: a flop sampling a signal whose last transition is within
  a setup/hold window around the capture edge captures a random value (fixed-seed xorshift) per bit — explicitly a
  *model*. 2-FF synchronizer: FF1 may go "metastable" (random), FF2 samples FF1 a full destination cycle later and
  is assumed resolved (state the MTBF formula MTBF = e^(t_r/τ) / (T_W · f_clk · f_data) with symbolic/대표값 params, why
  2 or 3 stages). Part A = single-bit sync and a 2-FF synchronizer latency (1–2 dst cycles). Part B = async FIFO
  depth 2^N with (N+1)-bit pointers: (1) binary pointers synchronized bitwise → wrong full/empty (demonstrate with a
  scoreboard: lost/duplicated/corrupted data, overflow/underflow), (2) Gray pointers + 2-FF → scoreboard clean across
  long random runs; full = Gray wptr == {~rptr_sync[N:N-1], rptr_sync[N-2:0]}, empty = rptr == wptr_sync — derive and
  CHECK against binary-difference definition exhaustively for small N. Also show pessimism: full/empty are conservative
  (latency), never wrong. Part C = other CDC patterns: pulse synchronizer (toggle) and a multi-bit bus via req/ack
  handshake; why "just 2-FF each bit of a bus" is wrong; reset sync (async assert, sync deassert) mention → RTLDV-B06.
  Next step RTLDV-M05/M06/M07 (HDL). Gray↔binary conversion functions tested exhaustively.
- **A08 (C)**: P_dyn = α·C·V²·f; leakage P_leak(V,T) = V · I_leak0 · e^(k·(V−V0)) · e^((T−T0)/T_s) or similar — state
  as model with 대표값 params; OPP table (e.g. 5 points f/V, labelled 대표값, monotone). Energy for a job of N cycles
  with deadline D: race-to-idle (max OPP then idle/sleep for the rest — distinguish idle (clock gated, leakage on) from
  sleep (power gated, P_sleep, entry/exit energy + latency)) vs pace (lowest OPP meeting D; "critical speed" where
  E/cycle is minimal). Part A = calculator + tests on hand-computed values. Part B = when each wins: sweep leakage
  fraction / sleep power / transition cost / memory-bound fraction (memory-bound work doesn't speed up with f) →
  crossover table; find energy-optimal OPP (critical frequency) and show it exists only because of leakage + static
  power. Part C = duty-cycled sensor node battery life: wake period, MCU active cycles, sensor + radio TX/RX energy
  (대표값 labelled), sleep current, regulator efficiency, battery capacity (mAh @ V), derating/self-discharge note →
  days; sensitivity table (which term dominates). Units must be right (J, W, mAh·V·3.6 = J). Cite SIV-A03 (they
  *fit* the model from measurements; you *use* it) and AIPERF-A08/BM-M11 as next steps. Numeric tests with stated
  tolerance.
- **A09 (C)**: dependency graph: rails (e.g. VDD_AON, VDD_IO, VDD_CORE, VDD_PLL/analog, VDD_DDR) with ramp time +
  power-good; PLL lock (time, may fail); clock enable per domain (gating); reset deassert per domain (requires its
  clock running ≥ K cycles for the reset synchronizer); peripheral init. Represent as data (nodes + edges + min delays
  + timeouts). Part A = power-up sequencer FSM (event-driven sim with time, timeouts → FAULT state + reason), power-down
  in reverse with isolation clamps enabled before a domain's rail drops and retention save before power-off (for a
  retention domain). Part B = checker: given ANY event log (from the sequencer or hand-written), report every
  violation with the edge violated and timestamps (order, min delay, reset-before-clock-stable, clock-before-rail-good,
  rail off before isolation, PLL used before lock) — test it on hand-written bad logs (each violation detected exactly
  once, good log passes). Part C = fault injection on the sequencer: PG never asserts, PLL never locks / loses lock
  mid-run, rail brown-out while running (→ ordered shutdown + reset cause recorded, cite ARCH-B14), late power-good
  glitch; each produces the expected safe state + log, and the checker passes on the sequencer's output. Also derive
  the sequence order automatically from the graph (topological sort, cycle detection → config error). READ SIV-A06 and
  SIV-A11 first and stay in lane: they are board/validation-side; you are the SoC-architecture/design-side sequencer.
  Cite RTLDV-B06, SOCX-B11, BM-B03, NPU-A07 as next steps.
- **A10 (C)**: cycle-level single shared resource (bus/memory port), masters CPU (short, latency-sensitive, Bernoulli
  arrivals), DMA (bursty, long bursts), NPU (streaming, high bandwidth), each with a request queue; a grant occupies
  the resource for the burst length (non-preemptive). Arbiters: fixed priority (configurable order), round robin,
  weighted RR (weights → share under saturation; define your WRR precisely — e.g. credit-based/deficit), optionally
  "RR + CPU urgent" or aging as an extension. Metrics per master: offered vs achieved bandwidth, mean / p99 / max
  latency (request arrival → grant start and → completion; define), starvation flag. Part A = arbiters + work-
  conserving CHECK (resource never idle while any request pending) + RR fairness bound CHECK (max wait ≤ sum of other
  masters' max burst). Part B = load sweep (light → saturated) table/svg; fixed priority starves the low-priority master
  at saturation; WRR achieved share within tolerance of weights under saturation (CHECK, tolerance justified). Part C =
  SoC scenario: camera/display-like real-time master with a deadline (e.g. line buffer must be served within X
  cycles) + NPU bulk; which policy meets the deadline; QoS ideas (priority escalation, bandwidth regulators/limits) —
  mention AXI QoS signals hedged. Cite ARCH-M11 (DMA channel arbitration), M13/M14; next RTLDV-M03, SIM-M05, SOCX-M02.
- **A11 (C, real hardware)**: Part A = Amdahl: a pthreads kernel with a controllable serial fraction; measured speedup
  on 1,2,4,…,12 threads vs Amdahl prediction; Karp–Flatt experimentally determined serial fraction; heterogeneous cores
  (2 Super/4 P/6 E) break the "identical cores" assumption — show it in data. Gustafson in 풀이. Part B = roofline:
  measure peak FLOP/s single-thread (NEON FMA with enough independent accumulators to cover latency — show the asm has
  `fmla` and count FLOPs correctly: 1 FMA = 2 FLOP; vector width 4 × fp32) and all-thread; memory bandwidth with a
  STREAM-like triad/copy on arrays ≫ L2 (8 MB; use ≥ 256 MB total), single and multi-thread; build roofline
  attainable = min(peak, AI × BW). Part C = kernels: vector add, naive vs tiled matmul, conv1d (fp32) — compute
  arithmetic intensity analytically (state DRAM-traffic assumption: compulsory only, or measured-ish), measure
  GFLOP/s, place on roofline (svg from real numbers), verdict memory/compute bound + gap explanation (vectorization,
  cache reuse). Checked tests: kernel correctness vs reference, AI calculator, roofline function, Amdahl/Karp-Flatt
  math. Timing only in demo_*.c. -O2 vs -O3/-ffast-math: say what you used and check vectorization via asm or
  `-Rpass=loop-vectorize`. Accelerate/AMX/SME optional as a labelled ceiling. Report load average; the machine is
  shared — use median of ≥5 and say if multi-thread numbers are contaminated by other load. Cite ARCH-M07 (matmul
  cache) and MEM-A08/AIPERF-B04/B06 as next steps.
- **A12 (C + design doc in the md)**: the md's Part A contains the design document: requirements (edge-AI SoC: camera
  input (MIPI CSI-2 → ISP-lite), NPU, 2 CPU clusters (e.g. 2×big + 4×little), DRAM (LPDDR, e.g. 2–4 GB window),
  on-chip SRAM, boot ROM, security (secure SRAM/key region), peripherals (UART/SPI/I2C/GPIO/timers), debug) → address
  map table (32- or 40-bit PA; justify), AXI main interconnect + APB peripheral subsystem via bridge hierarchy (svg),
  coherency domains (CPU clusters coherent via a coherent interconnect; NPU/ISP/DMA non-coherent or IO-coherent —
  justify and tie to ARCH-M12 cache maintenance and ARCH-A04), master→slave access matrix (who can reach what; NPU no
  access to secure region; debug only when unlocked). The C code is the B11-style decoder model extended into an
  automated checker: region table as data {name, base, size, slave port, attributes (device/normal, secure, cacheable),
  allowed masters}; checks: overlaps, base aligned to size (or to 4 KB min + power-of-2 rule for regions that need it —
  define), size power of two where required, fits the PA width, reserved gaps reported, every peripheral 4 KB-aligned
  within APB window, master access matrix consistency (e.g. non-coherent master marked able to access cacheable shared
  buffer → warning), decode(addr, master, secure) → slave or DECERR/SLVERR + reason. Part B tests inject each error
  (overlap, misalignment, out-of-range, access violation) and expect exactly that diagnosis. Part C = run the checker
  on your designed map (clean) and on a "revision 2" change request (e.g. grow DRAM window, add a second NPU) showing what
  breaks and the fix. Print the final map as a sorted table from the C data (real output in md). Cite ARCH-B11, M13,
  M14, M15, M12; next SOCX-B01, SOCX-M12, SOCX-A10. Use real AMBA terminology correctly (DECERR from interconnect
  default slave, SLVERR from slave); hedge anything else.

## Code & language
- C for all: starter.c / solution.c / practice.c (+ demo_*.c). Build `cc -std=c11 -Wall -Wextra -Werror -O2` (add
  `-pthread` where threads are used; `-D_DARWIN_C_SOURCE`/POSIX define as needed — per C_M brief), CHECK harness,
  "N/M passed", exit 1 on failure. solution.c = starter.c with only TODO bodies filled (the diff highlights them).
- Deterministic tests (fixed-seed xorshift), exhaustive where small. No timing in CHECKs; benchmarks only in demo_*.c
  (A03, A11; others optional), median of ≥5 runs, report `uptime` load average and `cc --version` first line.
- Inline asm / `__builtin_*` fine in tests and demos, never as the TODO answer.
- Private work dir for binaries/helpers: /tmp/esn_<ID>/ — never write into a shared scratch path or another lesson's dir.

## Depth & format
- Advanced: md **500–750 lines**. Front matter `id`, `time:` realistic (80분–130분), `updated: 2026-11-11`.
- Output: ROOT/lessons/ARCH/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c/solution.c/practice.c (+ demo_*.c).
  Title line `# <ID> <title exactly as in ARCH.json>`.
- Parts: A = core model, B = the hard part interviewers probe (edge cases, "what does it miss", correctness hazards),
  C = firmware/silicon-realistic use. Each Part has checked tests.
- 1. 문제 first paragraph: one line on why it matters (interviews, real chips).
- ≥2 inline ```svg figures (page-table walk / TLB / ROB timeline / CDC waveform / energy-vs-frequency / sequencing
  timeline / arbiter / roofline / SoC block diagram). svg rules per base brief (viewBox, width ≤ 760, currentColor +
  f-acc/f-ok/f-warn/f-mut, font-size 12–14, escape &lt; &gt; &amp;). Plots built from REAL numbers you produced.
- 면접 Q&A **≥6** with real 꼬리질문 (staff level). 추가 연습 3–6, answers in <details>, ending with `### 한 번에 검증` +
  `<!-- code: practice.c -->` + build cmd.
- Accuracy over volume. If unsure of a spec/datasheet detail, say it less specifically rather than invent it, and
  list what you were unsure of in your final reply. Representative numbers not from a spec are labelled 대표값/가정.

## Verify (you MUST run)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    mkdir -p /tmp/esn_<ID>
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/esn_<ID>/st && /tmp/esn_<ID>/st   # builds, some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/esn_<ID>/so && /tmp/esn_<ID>/so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/esn_<ID>/pr && /tmp/esn_<ID>/pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/esn_<ID>/san && /tmp/esn_<ID>/san
    (same sanitizer run for practice.c; threaded code also under -fsanitize=thread)              # clean
- Every demo_*.c must build with the command shown in the md (run it).
- Self-check the md: headings verbatim & in order (## 학습 목표, ## 1. 문제 with ### Part A — / ### Part B — /
  ### Part C —, ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수,
  ## 8. 면접 Q&A, ## 9. 추가 연습, ## 10. 핵심 정리, ## 참고자료); `<!-- code: starter.c -->` in §3,
  `<!-- code: solution.c diff=starter.c -->` in §6. No `## ` heading lines inside code fences other than real
  output. Paste only real output you ran.

## When done
Reply (short) with: files written, md line count, PASS counts starter/solution/practice, sanitizer result, demo files +
what they showed (key numbers), anything you were unsure about. Do NOT edit files outside ROOT/lessons/ARCH/<your ID>/.
Do NOT run build_site.py (the coordinator does).
