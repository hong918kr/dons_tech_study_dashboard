# Lesson writer brief — 동시성 가이드 7층 "하드웨어 일관성 — 왜 배리어가 필요한가"

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt). Context: Don is working through
ROOT/2026-11-08_concurrency_practice_guide.md layer by layer. Layers 1–4 are written; layers 5 (lock-free) and
6 (deadlock) are being written RIGHT NOW by other sessions (half-done files possible — cross-reference by id,
never depend on their code). Layer 7 = ARCH-M08, MEM-M04, ARCH-M12, ARCH-A04, ARCH-A05, MEM-M07, RISCV-A04,
SOCX-B03, GPGPU-B09, GPGPU-M03 — all written in parallel now, one writer each. The idea of this layer: answer
the "왜?" behind layers 3–5 from the hardware side — cache lines, coherence protocol, memory models, DMA,
interconnect exclusives, GPU atomics — each lesson with (1) a model or measurement you can trust,
(2) a deterministic test that the broken version fails, (3) a firmware / silicon-validation connection.

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY.
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base) → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md (Medium
   overrides) → ROOT/curriculum/C_A_LESSON_WRITER_BRIEF.md (Advanced overrides + "Concurrency items" bullet)
   → ROOT/curriculum/CONC1_LESSON_WRITER_BRIEF.md ("Overrides / extras" + "Verify")
   → ROOT/curriculum/CONC3_LESSON_WRITER_BRIEF.md ("Overrides for this set" + "Verify")
   → ROOT/curriculum/CONC4_LESSON_WRITER_BRIEF.md ("Overrides for this set": benchmark/load rules, asm rule).
   Python items also read the Python rules in ROOT/curriculum/HWLAB_B_LESSON_WRITER_BRIEF.md (stdlib only,
   Python 3.9-compatible, `python3 -I`). Everything applies unless overridden below. Ignore any
   "Do NOT edit outside lessons/C/..." path.
3. ROOT/2026-11-08_concurrency_practice_guide.md §7 — your layer and its "면접 체크" bullets (false sharing fix,
   one write through MESI S→I/RFO, reorders visible on ARM but not x86/TSO, DMA clean/invalidate order): your
   lesson must let Don answer the ones relevant to your item. Read the §3 and §4 "작성 메모" tables — they hold
   facts already MEASURED on this Mac (cite them by lesson id instead of re-measuring when that's all you need).
4. ROOT/curriculum/tracks/<TRACK>.json — your item (title/goal/task/tags/env). The lesson must do the `task`.
   Where the task says "M2", this Mac is an M6: measure on M6 and say so (keep the title as in the JSON).
5. Already taught (read md briefly, cross-reference by id, do NOT re-teach): ROOT/lessons/C/C-A03 (stdatomic,
   release/acquire), ROOT/lessons/C/C-M14 (ISR shared vars, DMA mention), ROOT/lessons/OS/OS-M08 (message
   passing/DCLP), ROOT/lessons/MCPP/MCPP-M13 (fences, release sequences), ROOT/lessons/ARM/ARM-M04 (LL/SC vs
   LSE), ROOT/lessons/ARM/ARM-M05 (dmb/dsb/isb), ROOT/lessons/ARM/ARM-M11 (Cortex-M local exclusive monitor),
   ROOT/lessons/RISCV/RISCV-M04 (LR/SC, AMO encodings, RV32IA ISS — sequentially consistent model),
   ROOT/lessons/LLAT/LLAT-B06 (atomic op costs), ROOT/lessons/OS/OS-M07 (spinlocks + coherence traffic),
   ROOT/lessons/LLAT/LLAT-M05 (ping-pong latency, P↔E, 128 B false sharing finding), ROOT/lessons/OS/OS-A03
   (demo_sb SB litmus numbers). `grep -l` for your topic in ROOT/lessons to find any other overlap.

## Machine facts (this Mac)
Apple M6: Super 2 + Performance 4 + Efficiency 6 = 12 cores (`sysctl hw.perflevel0/1/2.physicalcpu` — perflevel2
exists, don't miss E cores), no core pinning (QoS is only a hint; LLAT-M05: BACKGROUND QoS → 100% E cores),
`hw.cachelinesize` = 128. GPU: Apple M6, 12 cores, Metal 4. Apple clang 21. Build needs
`SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk`. TSan exits 134 on races; Apple TSan does not
model standalone `atomic_thread_fence`. **Rosetta 2 is installed**: `clang -arch x86_64 x.c -o x && ./x` runs an
x86_64 binary (Rosetta runs translated code with the hardware TSO mode — a real ARM-vs-TSO experiment on one
machine; hedge the mechanism, measure the effect). Only Command Line Tools (no Xcode): **no `metal` shader
compiler**, but Metal shaders compile at RUN time with `-[MTLDevice newLibraryWithSource:options:error:]` from
Objective-C — verified working:
`clang -fobjc-arc -O2 -Wall -Wextra -Werror x.m -framework Metal -framework Foundation -o x`.
No arm-none-eabi-gcc (`clang --target=thumbv7em-none-eabi -mcpu=cortex-m7 -O2 -S` works for asm). Mac clang has
NO RISC-V backend. Lima VM `elinux` (aarch64, 6 vCPU, running — check `limactl list`; other sessions may stop it)
has Ubuntu clang 21 with RISC-V targets, `riscv64-linux-gnu-gcc`, `qemu-riscv64`. VM home mount is READ-ONLY:
copy files to VM /tmp/<ID>_work/ and work there. No herd7 / litmus7 installed (don't install; cite them).

## Lanes (written IN PARALLEL — stay in yours, cross-reference others by id)
- **ARCH-M08** (Mid, C11 + pthreads, mac) — OWNER of the false-sharing stride sweep and struct-layout guide.
  Two (then N) threads each increment their OWN counter; counters placed at stride 4/8/16/32/64/128/256 B
  (alignas + padding), measure ns/increment and slowdown vs stride ≥ 256; estimate the coherence granule from
  where the cliff ends (expect 128 B on M6 — LLAT-M05 saw 64 B still sharing when an E core is involved; check
  P-only (USER_INTERACTIVE QoS) vs E-mixed (BACKGROUND QoS) separately and report both honestly). Increments must
  not be optimized away (relaxed `atomic_fetch_add` vs `volatile` store of a local — measure both, explain the
  difference: RMW vs plain store, LSE `ldadd`, real `-S`). Cross-check `sysctl hw.cachelinesize`,
  `std::hardware_destructive_interference_size` idea (C++; just mention, LLAT-M01 measures), Linux
  `____cacheline_aligned_in_smp`, Cortex-M7 32 B D-cache line (but single core → false sharing only matters
  vs DMA: see:ARCH-M12). Struct design guide: hot/cold split, per-thread/per-core arrays padded, read-mostly vs
  write-hot fields, `_Static_assert(offsetof(...) % 128 == 0)` pattern. CHECK (deterministic): a layout table
  (offsetof/sizeof/alignof of the student's padded structs must meet the rule) + exact counter totals; timings
  only in demo_bench.c. Starter's struct is unpadded → layout CHECK fails. Boundary: coherence traffic vs core
  count curve = MEM-M04; tool-based detection (perf c2c HITM) = LPERF-A03 (cross-ref).
- **MEM-M04** (Mid, C11 + pthreads, mac) — three patterns × 1..12 threads: (a) one shared atomic counter
  (`fetch_add` relaxed), (b) per-thread counters on separate 128 B lines then sum, (c) read-only shared data
  (all threads read the same lines). Throughput (total Mops/s) vs threads, svg scalability curve from real
  numbers, P vs E region visible (thread count 1–6 vs 7–12; QoS note). Also (a') shared counter with a CAS loop
  vs `ldadd` (cite ARM-M04 for LL/SC vs LSE — just one line, don't re-measure). Explain each curve in coherence
  terms: (a) the line ping-pongs in M state, every op = RFO/ownership transfer → flat or falling;
  (b) linear; (c) lines stay Shared in every cache → linear. Comparison with MESI prediction: ARCH-A04 is being
  written in parallel → put a SMALL self-contained MESI transaction COUNTER in your lesson (no timing, just
  counts: for k threads doing round-robin ops on pattern a/b/c, how many BusRdX/BusUpgr/invalidations/
  interventions per op) and compare the predicted "coherence messages per op" ratio with the measured
  slowdown ratio — say honestly where the simple bus model does NOT match (real M6 has a directory/LLC in
  clusters, not a snooping bus; hedge). CHECK (deterministic): the model's counts for fixed schedules equal
  the hand-derived formulas (e.g. pattern a with k threads round-robin = 1 transfer per op), counter totals
  exact. Timings = demo_bench.c. Boundary: stride sweep = ARCH-M08; full MESI simulator = ARCH-A04.
- **ARCH-M12** (Mid, C11, mac, single-threaded model) — OWNER of "non-coherent DMA cache maintenance". Model:
  RAM array + write-back, write-allocate D-cache (32 B lines like Cortex-M7, small set count) + a DMA engine that
  reads/writes RAM directly (bypasses cache) + the cache may evict dirty lines at any time (model: an explicit
  `cache_evict_some()` hook the test schedules deterministically — e.g. eviction during RX DMA) and may
  speculatively refill (Cortex-A / M7 prefetch — model as a hook too, hedge which cores do it). Bugs reproduced
  deterministically: TX without clean (device sends stale RAM), RX without invalidate (CPU reads stale cached
  data), RX where a dirty line is evicted DURING the DMA and overwrites fresh DMA data (why invalidate must be
  BEFORE the transfer too, not just after), unaligned / non-line-sized RX buffer sharing a line with another
  variable → invalidate destroys the neighbor's dirty update or clean+invalidate brings garbage back (adjacent
  data corruption). Fixes: TX = clean before start; RX = invalidate (or clean+invalidate for partial edge lines)
  before start AND invalidate after completion; buffers `alignas(32)` and size rounded to line multiple; MPU
  non-cacheable region alternative (cost/benefit table). Map to real APIs: CMSIS `SCB_CleanDCache_by_Addr` /
  `SCB_InvalidateDCache_by_Addr` / `SCB_CleanInvalidateDCache_by_Addr` (the M7 errata-free requirement that
  addr/size be line aligned — check CMSIS core_cm7.h source if you can fetch it; else hedge), Linux
  `dma_map_single(dir)` / `dma_sync_single_for_cpu/device` and which direction does clean vs invalidate
  (hedge), `dma_alloc_coherent`. Show real thumbv7em asm of a clean-by-address loop: write a tiny C function
  that stores to the SCB DCCMVAC register (0xE000EF68 — verify the address against CMSIS if possible) +
  `__DSB()` and paste `clang --target=thumbv7em-none-eabi -mcpu=cortex-m7 -O2 -S`. Also: why `volatile` /
  atomics do NOT solve DMA coherence (guide check bullet), barrier needed before kicking the DMA (dsb, see:
  ARM-M05). CHECK: each bug scenario detected by comparing against a golden model; starter (no maintenance)
  fails. Firmware tie-in: STM32H7 Ethernet/SDMMC RX buffers, CAM-M11 (capture buffers), SIV-M09 (stress).
- **ARCH-A04** (Adv, C11, mac, single-threaded simulator) — OWNER of the MESI simulator. 4 cores, private
  caches (direct-mapped or small set-assoc, configurable), one atomic snooping bus (one transaction per cycle,
  arbitration round-robin), messages BusRd / BusRdX / BusUpgr / Flush (write-back) and snoop responses
  (shared signal, intervention/flush from M). Trace input (text file and in-code arrays): `c<k> R|W addr [val]`.
  Output per step: transition `C1 S→M (BusUpgr)`, bus message, which caches invalidated, data source
  (memory / cache-to-cache). Invariants checked EVERY cycle: SWMR (at most one M/E holder, M/E ⇒ others I),
  data-value invariant (every read returns the value of the latest write in a global write order —
  keep a shadow "golden" memory), memory up to date unless some cache has M. Part B: mutation testing —
  at least 5 injected protocol bugs (e.g. forget to invalidate on BusUpgr, E on read when another has S,
  no flush on BusRd snoop of M, upgrade race when two S holders write in the same cycle, eviction of M without
  write-back) each caught by an invariant with the cycle number printed; table of mutant → which invariant
  caught it. Part C: MSI vs MESI (E saves the BusUpgr for private data — count it on a trace) and MOESI/MESIF
  (O avoids memory write-back on sharing; F = forwarder) — implement MOESI as an option or explain with a
  counted example; real chips use directories (scale) — hedge, short. Stats per run: bus transactions by type,
  cache-to-cache transfers, write-backs. Also produce the transition-count output for three canonical traces
  (shared counter, private counters, read-only shared) so MEM-M04's measured curves can be compared later
  (same pattern names a/b/c). Starter: transition function TODOs → invariants fail. Random traces (xorshift)
  with invariant checks as the stress test (deterministic seeds).
- **ARCH-A05** (Adv, C11 + pthreads, mac) — OWNER of litmus testing on real hardware. Harness in litmus7
  style (cite Alglave, Maranget et al., "Litmus: running tests against hardware", TACAS 2011): two threads,
  N iterations, per-iteration fresh variables (arrays, cache-line spaced), lightweight spin barrier per
  iteration (or batch of iterations), record results in arrays, histogram afterwards. Tests: SB, MP, LB
  (+ 2+2W or IRIW as optional extra, say ARMv8 is other-multi-copy-atomic → IRIW forbidden). For each test and
  each variant — relaxed / acquire-release / seq_cst / explicit `atomic_thread_fence` — count the "weird"
  outcome over ≥1e6 iterations (keep each run < 30 s). Expected: SB weird allowed unless seq_cst/full fence;
  MP weird needs relaxed (ARM allows; check whether M6 shows it); LB allowed by ARMv8 but often not observed —
  report the real count honestly (0 observed ≠ forbidden). Paste the asm of each variant (real `-S`:
  `ldar`/`stlr`/`ldapr`/`dmb ish`/`dmb ishld`). **TSO experiment**: build the SAME source with `-arch x86_64`
  and run under Rosetta 2: SB should still appear (TSO allows store→load reordering), MP/LB should vanish —
  measure and report; also note x86 asm (`mov` for release store, `xchg`/`mfence` for seq_cst store) from
  `clang -arch x86_64 -S`. This answers the guide bullet "ARM에서는 보이고 x86(TSO)에서는 안 보이는 재배치".
  CHECK (deterministic part): with seq_cst (SB) / acq-rel (MP) the forbidden outcome count must be exactly 0
  AND the harness must be able to produce the allowed outcomes (sanity: all three non-weird outcomes appear for
  SB) — the starter's broken harness/TODO fails. Weird-outcome counts themselves = demo output (nondeterministic).
  Cross-ref OS-A03 demo_sb numbers and LLAT-M02 torn reads (one line each). Firmware: single-core Cortex-M
  is effectively SC for normal memory w.r.t. itself, but device memory / DMA / multi-core (RP2040, STM32H7
  dual-core) is where it matters (hedge). Boundary: model enumeration = MEM-M07, RISC-V = RISCV-A04.
- **MEM-M07** (Mid, Python 3 stdlib, `python3 -I`) — OWNER of the axiomatic litmus enumerator. Input: small
  litmus programs in a text format you define (threads with `W x 1`, `R x r0`, `F` (full fence), `Racq`,
  `Wrel`), e.g. files `tests/MP.litmus` or an in-code dict. Enumerate all candidate executions (rf choices for
  each read, co order per location) and filter by model: SC (po ∪ rf ∪ co ∪ fr acyclic), TSO (x86-TSO style:
  ppo = po minus W→R to different locations; rfi not in global order; fences restore W→R), ARMv8 (simplified
  "aarch64.cat": internal = po-loc ∪ rf ∪ co ∪ fr acyclic; external: ob = obs ∪ bob acyclic where obs =
  rfe ∪ coe ∪ fre and bob covers DMB full, acquire (LDAR → po-later), release (po-earlier → STLR), STLR→LDAR;
  no dependencies needed for MP/SB/LB/IRIW without deps — say what is left out: dob/aob/addr/data/ctrl deps).
  Output: allowed final-state set per model and the table MP/SB/LB/IRIW/2+2W × SC/TSO/ARMv8 (+ fenced variants).
  Must match the known results (cite Alglave, Maranget, Tautschnig "Herding cats", TOPLAS 2014; Pulte et al.
  "Simplifying ARM concurrency", POPL 2018; Sewell et al. "x86-TSO", CACM 2010): e.g. SB weird allowed in TSO
  & ARM, MP/LB weird allowed in ARM only, IRIW forbidden in all three (ARMv8 multi-copy-atomic). Deterministic
  CHECKs = those expected sets. Part B: show WHY with a cycle printout ("forbidden under SC because cycle
  po→rf→po→fr"). Part C: compare with ARCH-A05's measurements on the M6 (written in parallel — use the
  guide's §4 작성 메모 numbers + OS-A03 SB numbers now, and leave a clearly marked comparison table with
  "ARCH-A05 측정값 참조" so Don fills it in from that lesson; don't invent numbers). Also "allowed ≠ observed"
  discussion. Starter: model filters TODO → expected sets fail. Boundary: RVWMO = RISCV-A04.
- **RISCV-A04** (Adv, Python 3 stdlib enumerator + C mapping demos built in the VM) — (1) C11 → RISC-V
  mapping table from REAL output: `demo_map.c` with load/store/RMW/CAS/fence for every memory_order, built in
  the VM with `clang --target=riscv64-linux-gnu -march=rv64gc -O2 -S` AND `-march=rv64gc_ztso` (Ztso changes
  the mapping — show it) and `riscv64-linux-gnu-gcc -O2 -S` (differences between compilers, e.g. seq_cst load
  `fence rw,rw; lw; fence r,rw` vs others — paste what you see). Compare with the mapping table in the RISC-V
  unprivileged ISA manual (Appendix A "RVWMO Explanatory Material", "Table: Mappings from C/C++ primitives to
  RISC-V primitives" — cite version you checked or hedge) and say whether the compilers follow it.
  (2) Explain RVWMO: the 13 preserved-program-order rules grouped (overlapping-address, explicit sync = fence/
  aq/rl, syntactic deps, pipeline deps) — only state the rules you are sure of, cite the chapter. FENCE pred/succ
  sets: what `fence rw,rw`, `fence r,rw`, `fence rw,w`, `fence.tso`, `fence w,w` each order (table + a litmus
  where each is the minimal fix). `.aq/.rl/.aqrl` on LR/SC/AMO. (3) Self-contained enumerator (MEM-M07 is written
  in parallel — embed your own compact axiomatic enumerator, same idea: candidate executions + acyclicity) with
  an RVWMO subset (same-address ordering, fences with pred/succ sets, aq/rl) + SC + TSO(=Ztso) + ARMv8-lite,
  and the MP/SB/LB comparison table; deterministic CHECKs = known results (RVWMO allows MP/SB/LB weird without
  fences; `fence rw,rw` forbids all three; `fence.tso` forbids MP but not SB — verify each with your enumerator
  and against the spec's litmus discussion). Optional: run a SB litmus under `qemu-riscv64` (MTTCG on the arm64
  VM host) and report counts honestly (QEMU translation may or may not expose weak behavior — whatever you see).
  Starter: RVWMO fence rules TODO → CHECKs fail. Cross-ref RISCV-M04 (encodings, LR/SC), MEM-M07, ARCH-A05.
- **SOCX-B03** (Basic, C11, mac, single-threaded model, 450–600 lines, Q&A ≥5) — AXI4 exclusive access
  (cite ARM IHI0022 AMBA AXI and ACE spec, the "Exclusive accesses" section — give the issue/version you checked,
  else hedge). Response codes OKAY / EXOKAY / SLVERR / DECERR table (what each means, who generates it, what the
  master does). Slave-side global exclusive monitor: per-master-ID reservation {address, size}; exclusive read
  (ARLOCK=1) sets/replaces that ID's reservation and gets EXOKAY (if exclusives supported) — what OKAY on an
  exclusive READ means (slave doesn't support exclusives → master must treat as failure); exclusive write
  (AWLOCK=1) succeeds with EXOKAY and updates memory only if that ID's reservation still matches, else OKAY and
  memory NOT updated; any successful write by any master to the reserved range clears other IDs' reservations
  (state the exact spec rule you rely on; hedge corner cases like size/alignment rules, max 128 bytes,
  power-of-2). Scenario: two masters run a spinlock acquire (excl-read lock; if 0 → excl-write 1; if not EXOKAY
  → retry, bounded) + critical section (plain write to data) + release (plain write 0). Enumerate ALL
  interleavings of the two masters' bus transactions (bounded retries) and check: mutual exclusion never
  violated, at most one EXOKAY per "lock epoch", final state consistent. Broken monitor variants (doesn't clear on
  other master's write; single global reservation shared by all IDs without ID check; ignores address) → the
  enumerator finds a concrete counter-example interleaving, printed step by step. Connection: core-local
  monitor LDREX/STREX = ARM-M11 (local + global monitors together: why a cacheable shareable location uses the
  coherence protocol instead of the global monitor on Cortex-A — hedge), ARM-M04 (LSE atomics → AXI5/CHI
  atomic transactions, AtomicStore/AtomicLoad/AtomicSwap/AtomicCompare — mention only, hedge), silicon
  validation: how you'd test an interconnect's exclusive monitor (directed tests from your enumerator's
  interleavings). Starter: monitor TODOs → enumerator reports a mutex violation → FAIL.
- **GPGPU-B09** (Basic, Objective-C host + Metal kernels compiled at run time, mac, 450–600 lines, Q&A ≥5) —
  Files: `starter.m` + `starter.metal`, `solution.m` + `solution.metal`, `practice.m` + `practice.metal`
  (+ demo_*.m / demo_*.metal). Host reads `<same basename>.metal` from the CURRENT directory (run from the
  lesson dir; say so in §3) and compiles it with `newLibraryWithSource`. Do: (1) global counter: plain
  `device uint*` `c[0]++` vs `atomic_fetch_add_explicit(..., memory_order_relaxed)` over e.g. 2^20 threads →
  lost-update count (demo, nondeterministic) vs exact (CHECK). (2) threadgroup memory kernel that writes
  `tg[lid]` and then reads a DIFFERENT simdgroup's element (e.g. `tg[tgsize-1-lid]`, tgsize ≥ 256 so readers
  cross simdgroups of 32) — with `threadgroup_barrier(mem_flags::mem_threadgroup)` exact (CHECK), without it →
  count wrong elements (demo, report honestly — may be 0 on this GPU; explain simdgroup lockstep and why
  "it worked" is not proof). (3) contention sweep: 2^22 atomic adds spread over 1, 2, 4, …, 1024 addresses →
  throughput (Gops/s), svg from real numbers. Explain Metal memory model words: `memory_order_relaxed` is the
  only order for device atomics in MSL (check the Metal Shading Language spec version you can cite, hedge),
  `threadgroup_barrier` vs `simdgroup_barrier`, mem_flags::mem_device / mem_threadgroup, no global grid barrier
  inside a kernel (split into dispatches). Compare with CUDA terms (`__syncthreads`, `atomicAdd`, warp = 32)
  briefly. Try `MTL_SHADER_VALIDATION=1` and say what it does/doesn't catch here (run it). CHECK: counts exact
  on the CPU side; starter (plain increments / missing barrier on purpose with a TODO) FAILS deterministically —
  design the starter so its failure does not depend on a race actually happening (e.g. starter kernel stubs
  return wrong values / TODO leaves output unwritten). Boundary: histogram privatization = GPGPU-M03.
- **GPGPU-M03** (Mid, Objective-C + Metal, mac, files as GPGPU-B09) — 256-bin histogram over N bytes
  (N ≥ 2^24): v1 global atomic per element, v2 threadgroup-local histogram (`threadgroup atomic_uint[256]`,
  zero → barrier → accumulate → barrier → merge with one device atomic per bin per threadgroup), v3 optional:
  per-simdgroup sub-histograms or SIMD-level aggregation (`simd_ballot`/`simd_sum`-style — only if you verify
  it compiles and helps). Inputs: uniform random, Zipf/skewed, all-same-bin worst case (deterministic xorshift
  generator). Measure GB/s and ms per version per input (several runs, median; svg from real numbers; GPU
  timing via `commandBuffer.GPUStartTime/GPUEndTime` — check availability), and CPU single-thread reference.
  CHECK: every version equals the CPU histogram exactly, sum = N. Explain why privatization helps (contention
  on 256 device addresses vs 256×#threadgroups threadgroup addresses, merge cost), when it doesn't (tiny
  inputs, many bins → threadgroup memory limit 32 KB — check `maxThreadgroupMemoryLength`), and the skewed-input
  collapse. Connection: same idea in CPU code = per-thread counters (MEM-M04 pattern b), image sensor
  statistics (AE/AWB histograms in ISP hardware — hedge), NPU/GPU validation of atomics. Starter: v2 merge/
  barrier TODOs → mismatch → FAIL deterministically.

## Overrides for this set
- Output: ROOT/lessons/<TRACK>/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + starter + solution + practice
  (.c; `.py` for MEM-M07 / RISCV-A04 enumerators — RISCV-A04 adds demo_map.c etc.; `.m` + `.metal` for GPGPU)
  (+ demo_*). Front matter `updated: 2026-11-08`; `time:` Basic 50–70분, Mid 70–100분, Adv 90–120분. Title line
  `# <ID> <title exactly as in the track JSON>`. md length: Basic 450–600, Mid 500–700, Adv 550–750 lines.
  Q&A ≥6 (Basic ≥5) with 꼬리질문. 1. 문제 starts with the Korean problem intro.
- Code markers: `<!-- code: starter.X -->` in §3, `<!-- code: solution.X diff=starter.X -->` in §6,
  `<!-- code: practice.X -->` in §9 with X = c / py / m. GPGPU also show the .metal files with their own markers
  (`<!-- code: solution.metal diff=starter.metal -->` in §6, starter.metal in §3, practice.metal in §9).
- Benchmarks: TEN writers of this layer + two other sessions (layers 5, 6) run on this Mac at the same time
  (load average already 7–8). Record `uptime` load average next to every table, re-run final numbers when load
  is lower if you can, tell the reader to trust RATIOS, not absolute ns. Machine line:
  `sysctl -n machdep.cpu.brand_string hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel2.physicalcpu`,
  `cc --version` line 1. Each benchmark run < 30 s. Default CHECK runs (starter/solution/practice main) finish in
  < 20 s each, also under ASan/TSan (scale iterations down under sanitizers — document how).
- Deterministic CHECKs only (exact counts, golden models, enumerated sets, invariants, forced schedules).
  Timings and observed-reorder / lost-update counts = demo output. Starter must FAIL (exit 1) without hanging.
- Every asm claim = instruction you saw in real `-S`/objdump output; show the commands. Every spec claim: cite
  document + section, or hedge ("확인 필요"). Don't claim tool behavior you didn't run.
- PRIVATE scratch dir for helpers: /tmp/<ID>_work/ (VM: /tmp/<ID>_work/ inside the VM). Never write helpers in
  a shared folder, never in another lesson's dir.

## Verify (you MUST run)
- C items: CONC1 "Verify" exactly (`cc -std=c11 -Wall -Wextra -Werror -O2 -pthread`; starter FAIL exit 1 no
  hang; solution + practice PASS exit 0, solution 5+ runs; ASan+UBSan clean; TSan clean when threads are used —
  single-threaded models: ASan/UBSan only, say TSan n/a).
- Python items: `python3 -I -W error starter.py` (FAIL exit 1), `python3 -I -W error solution.py` /
  `practice.py` (PASS exit 0), `python3 -I -W error -X dev solution.py` (no warnings). Deterministic output
  (run twice, identical).
- GPGPU items: `clang -fobjc-arc -O2 -Wall -Wextra -Werror X.m -framework Metal -framework Foundation -o /tmp/<ID>_work/X`
  from the lesson dir; starter FAIL exit 1, solution + practice PASS exit 0 (solution 5+ runs); host code under
  `-fsanitize=address,undefined` clean.
- md self-check: headings verbatim & ordered (## 학습 목표, ## 1. 문제 with ### Part A — / ### Part B — /
  ### Part C —, ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수,
  ## 8. 면접 Q&A, ## 9. 추가 연습 + ### 한 번에 검증, ## 10. 핵심 정리, ## 참고자료), ≥2 ```svg (viewBox, width ≤760,
  currentColor + f-acc/f-ok/f-warn/f-mut only, escaped &lt; &gt; &amp;), Q&A as `<details><summary>Q1. ...`,
  3–6 추가 연습 with answers in <details>, the code markers above. Paste only real output you ran.

## When done
Reply (short): files written, PASS counts starter/solution/practice, sanitizer results, demo files + what they
showed (key numbers + load average), anything unverified/unsure as a list (the orchestrator copies it into the
guide's 작성 메모 table: 실측 한 줄 / 환경 한계 / 확인 안 된 주장). Do NOT edit files outside
ROOT/lessons/<TRACK>/<ID>/ (don't run build_site.py).
