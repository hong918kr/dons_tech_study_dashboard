# Lesson writer brief — 동시성 가이드 5층 "lock-free 자료구조"

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt). Context: Don is working through
ROOT/2026-11-08_concurrency_practice_guide.md layer by layer; layers 1–3 are written, layer 4 (locks: OS-M07
spinlocks, OS-A01 futex, MCPP-A03 wait/notify, LLAT-M02 seqlock, OS-A03 RCU/epoch, LLAT-M05 ping-pong) is being
written RIGHT NOW by another session (its files may be half-done — cross-reference by id, never depend on its code).
Layer 5 = DS-A01, DS-A03, DS-A04, LLAT-M01, LLAT-M03, RUST-M07, OS-A05, LLAT-A06 (+ DS-M10, already written).
The guide's idea for this layer: lock-free structures that interviewers ask about, each one with (1) a
correctness argument tied to specific memory_order choices, (2) a deterministic test that the broken version
fails, (3) measured numbers vs the obvious locked alternative.

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY.
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base) → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md (Medium
   overrides) → ROOT/curriculum/C_A_LESSON_WRITER_BRIEF.md (Advanced overrides + "Concurrency items" bullet)
   → ROOT/curriculum/CONC1_LESSON_WRITER_BRIEF.md ("Overrides / extras" + "Verify")
   → ROOT/curriculum/CONC3_LESSON_WRITER_BRIEF.md ("Overrides for this set" + "Verify")
   → ROOT/curriculum/CONC4_LESSON_WRITER_BRIEF.md ("Overrides for this set": benchmark/load rules, asm rule).
   C++ items also read ROOT/curriculum/CPP_LESSON_WRITER_BRIEF.md (.cpp markers). RUST-M07 also reads the
   "Rust item" section of ROOT/curriculum/CONC2_LESSON_WRITER_BRIEF.md. Everything applies unless overridden
   below. Ignore any "Do NOT edit outside lessons/C/..." path.
3. ROOT/2026-11-08_concurrency_practice_guide.md §5 — your layer and its "면접 체크" bullets (SPSC orders,
   ABA scenario + fixes, why Vyukov has per-slot sequence, lock-free vs wait-free): your lesson must let Don
   answer the ones relevant to your item. Skim the §3 "작성 메모" table (measured facts on this Mac).
4. ROOT/curriculum/tracks/<TRACK>.json — your item (title/goal/task/tags/env). The lesson must do the `task`.
5. Already taught (read md briefly, cross-reference by id, do NOT re-teach): ROOT/lessons/C/C-A03 (stdatomic,
   release/acquire publish, CAS, triple buffer), ROOT/lessons/C/C-M14 (ISR shared vars), ROOT/lessons/OS/OS-M08
   (message passing, DCLP), ROOT/lessons/MCPP/MCPP-M13 (fences, release sequences; TSan false positives on
   standalone fences), ROOT/lessons/ARM/ARM-M04 (LL/SC vs LSE: on this M6 `casal` CAS loops slower than LL/SC
   under contention), ROOT/lessons/LLAT/LLAT-B06 (atomic op costs), ROOT/lessons/DS/DS-M10 (variable-length
   log ring, single-threaded), ROOT/lessons/RUST/RUST-M06 (critical-section, atomics on thumbv6m).

## Machine facts (this Mac)
Apple M6: Super 2 + Performance 4 + Efficiency 6 = 12 cores (`sysctl hw.perflevel0/1/2.physicalcpu`), no core
pinning (QoS is only a hint), `hw.cachelinesize` = 128. Apple clang 21. Build needs
`SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk`. TSan exits 134 on races. Apple TSan does
not model standalone `atomic_thread_fence` (false positives when non-atomic data is ordered only by fences →
make shared slots atomics, or say why a report is a false positive). No arm-none-eabi-gcc on the Mac
(`clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S` works for asm). Lima VM `elinux` exists (only if
you really need Linux; check `limactl list`, home mount is READ-ONLY, work in VM /tmp/<ID>_work/).

## Lanes (written IN PARALLEL — stay in yours, cross-reference others by id)
- DS-A01 (Adv, C11, mac) — the OWNER of "SPSC ring with acquire/release". Power-of-2 capacity, free-running
  head/tail indices (unsigned wrap is defined; show why `tail - head` works), full/empty without a wasted slot.
  Exactly which load/store needs which order and WHY (producer: relaxed load of own tail, acquire load of head,
  data write, release store tail; consumer mirror) — draw the happens-before edges in an svg. head/tail on
  separate 128 B lines. Part B broken versions: (a) relaxed everywhere — say honestly whether it was observed on
  this arm64 (stress count of corrupted/stale payloads; ARM is weakly ordered so it may show) and show the asm
  difference (`ldar/stlr` vs `ldr/str`, real `-S`); (b) TODO stubs fail deterministically. Test: 2 threads, order
  + no loss + no duplicate (sequence numbers + checksum) — CHECK mode ~1e7 messages so the run is short; a
  demo_bench does the "수억" run (≥2e8) and the ONE optimization the task asks for: cached opposite index
  before/after (throughput Mops/s, several runs, load average). Deep latency tuning (padding/batching/slot flags,
  p99) belongs to LLAT-M01 → cross-ref, don't do it. Firmware: ISR producer → main consumer (UART RX), on a
  single-core Cortex-M what the atomics compile to (thumbv7em `-S`: is there a `dmb`?), DMA writes need cache
  maintenance not atomics (mention, see:C-M14). Also: what breaks with 2 producers (motivates DS-A03).
- DS-A03 (Adv, C11, mac) — Vyukov bounded MPMC (1024cores "Bounded MPMC queue"): per-slot `sequence`, enqueue
  pos CAS, the `dif = seq - pos` three cases, why the per-slot sequence is needed (a single head/tail pair can't
  tell "slot written" from "slot reserved"). 4×4 stress: every message consumed exactly once (per-message
  bitmap/counter), per-producer order seen by each consumer is increasing (verify whether that property really
  holds — prove or test, don't assume), final counts exact. Throughput vs a mutex+array queue (pthread_mutex) and
  optionally mutex+condvar, 1×1 / 2×2 / 4×4 / 6×6 (svg from real numbers, load avg). Honest progress property:
  Vyukov's queue is NOT lock-free in the strict sense (a producer preempted between CAS and the sequence store
  blocks consumers at that slot) — demonstrate deterministically with a forced pause hook, and define
  lock-free / wait-free / obstruction-free precisely (guide check bullet). CAS cost: weak vs strong CAS, the
  LL/SC vs LSE asm clang emits (real `-S`, cite ARM-M04's measurement instead of re-measuring).
- DS-A04 (Adv, C11, mac) — the OWNER of "ABA". Treiber stack over a node POOL with free-list reuse (index
  based, so freed nodes come back fast). Part B: force ABA DETERMINISTICALLY (forced schedule: thread 1 reads
  top=A, next=B, pauses via hook; thread 2 pops A, pops B, pushes A; thread 1's CAS succeeds → B, which is
  free/in use → corrupted list), show the corrupted state concretely (cycle / lost node / node in two places)
  with a list-integrity checker. Fix 1: 64-bit packed (32-bit index + 32-bit generation tag) single-word CAS.
  Fix 2: 128-bit CAS on a {pointer, tag} pair — check what clang emits on arm64 macOS for
  `__atomic_compare_exchange_n` on `unsigned __int128` / `_Atomic` 16-byte struct (CASPAL? LDAXP/STLXP? libcall
  `__atomic_compare_exchange_16`? — paste real `-S`, and `atomic_is_lock_free`). Tag wraparound math (how many
  ops for a 32-bit tag to wrap; is it realistic?). Stress test 8 threads push/pop: integrity + exact count.
  Mention the other ABA fixes as a table: hazard pointers (see:LLAT-M03), epoch/RCU (see:OS-A03), LL/SC
  (immune to ABA by design on ARM? explain why compilers' CAS-on-LL/SC still is not — the reread value), GC.
  Firmware: RTOS/ISR free-list pools (lock-free memory pools on Cortex-M, ISR alloc), M0 has no CAS.
- LLAT-M01 (Mid, C++20, mac, flags `-std=c++20 -Wall -Wextra -Werror -O2 -pthread`) — SPSC LATENCY tuning
  (owner of padding/caching/batching/slot-flag measurements). Write your own C++ baseline port (DS-A01 is written
  in parallel; cite it for the correctness argument, don't re-derive). Steps applied one at a time, each its own
  class/template variant in the code so the table is reproducible: v0 baseline (indices adjacent) → v1 128 B
  padding (`std::hardware_destructive_interference_size` — check what libc++ on this Mac defines it as, real
  value) → v2 cached opposite index (rigtorp/SPSCQueue style) → v3 batch push/pop (publish index once per batch)
  → v4 per-slot sequence/flag instead of shared indices. Measure per step: throughput (Mops/s) AND round-trip
  ping-pong latency median/p99/p99.9 (two queues, echo; batching N round trips per timestamp if timer resolution
  needs it — explain). Table + svg from REAL numbers, several runs, load average, ratios only. CHECKs:
  correctness of every variant (order/no loss) deterministic. Explain WHY each step helps in cache-line terms
  (coherence misses per message) and when batching hurts latency. QoS hints (P vs E cores) effect as demo.
- LLAT-M03 (Mid, C11, mac) — hazard pointers (Michael 2004). Apply to a Michael–Scott queue (or a lock-free
  linked list — pick one, say why). Per-thread HP slots (padded), retire list, scan when retired ≥ R (R = k·H
  threads), memory bound = H·N + R·N. Part B: free-immediately version → use-after-free forced
  deterministically (hook between reading the node pointer and dereferencing) → real ASan
  heap-use-after-free report pasted (trimmed). Solution ASan clean under stress. The publish-then-revalidate
  protocol (store HP, seq_cst fence or seq_cst store, re-read source) — why the re-check and why that ordering
  needs seq_cst (store→load). Compare with epoch-based reclamation: implement a MINIMAL EBR baseline inside a
  demo (OS-A03 is being written in parallel — cite it as the full treatment) and measure read-path latency
  (ns/op) + peak unreclaimed nodes when one reader stalls (EBR unbounded vs HP bounded) — svg, real numbers.
  Mention C++26 `std::hazard_pointer` / `std::rcu` (hedge: check what you can, say standard status as you
  understand it), folly hazptr. MPMC queue implementation itself belongs to DS-A03 (cross-ref).
- RUST-M07 (Mid, Rust, mac) — toolchain exactly as CONC2 "Rust item" (rustup PATH, SDKROOT, CARGO_TARGET_DIR=
  /tmp/RUST-M07_target, crates.io downloads OK: loom, heapless). `Queue<T, const N: usize>` with
  `AtomicUsize` head/tail + `UnsafeCell<MaybeUninit<T>>` buffer, `split(&mut self) -> (Producer<'_, T, N>,
  Consumer<'_, T, N>)`; explain why `&mut self` + lifetimes make "single producer, single consumer" a type
  property (show a real compile error when trying to split twice / clone a Producer). `unsafe impl Send/Sync`
  justification, Drop of remaining items. loom test (`#[cfg(loom)]`, `RUSTFLAGS="--cfg loom"`): with Acquire/
  Release passes; weaken to Relaxed → does loom find a failing interleaving? Run it and paste the real output;
  if loom does NOT catch it, say so honestly and explain loom's memory-model limits (it models some but not all
  relaxed behaviors). Compare with `heapless::spsc::Queue` (API, the N vs N-1 capacity detail, what orders it
  uses — read its source in ~/.cargo/registry, cite file+version). Cross-build `#![no_std]` lib for
  thumbv7em-none-eabihf AND thumbv6m-none-eabi: SPSC needs only atomic load/store, so it builds on M0 (contrast
  with RUST-M06's fetch_add failure) — paste real build output + key asm (`dmb`?). Also `cargo clippy` clean,
  Miri not installed (say so). Host "ISR" = a thread. C ring buffer ISR version = see:DS-A02 (not written yet).
  Starter: `cargo test` fails (exit ≠ 0, no hang); solution + practice pass.
- OS-A05 (Adv, C11, mac) — Chase–Lev deque in the C11 form of Lê, Pop, Cohen, Zappa Nardelli, PPoPP 2013
  "Correct and Efficient Work-Stealing for Weak Memory Models" (cite; the seq_cst fences in take/steal and why).
  Owner push/take at bottom, thieves steal top with CAS; the last-element race between take and steal resolved
  by CAS on top — show it with a FORCED schedule (deterministic) and the broken version (missing fence or plain
  load) failing that schedule. Buffer slots as atomics (relaxed) so TSan has no false positives; fixed-size
  array is fine (growth = explain + optional). Thread pool: workers with own deque, random victim steal,
  spawn/join for recursive parallel sum and fib (cutoff), termination detection. Every task executed exactly
  once (counters), results exact. Scalability 1..12 workers vs a single global mutex queue (svg, real numbers,
  load avg, note E/P core mix). false sharing measurement = see:MEM. Mention Cilk/TBB/Rayon/Go scheduler/Tokio
  as users (hedge details). Firmware/robotics: where work-stealing appears (ROS 2 executors? hedge) — keep short.
- LLAT-A06 (Adv, C++20, mac, flags as LLAT-M01) — NanoLog-style binary logger (Yang et al., USENIX ATC 2018 —
  cite). Hot path: per-call-site static format id (registered once via a function-local static or a macro),
  write {id, timestamp (raw counter, e.g. `mach_absolute_time` or `cntvct_el0` — check which you can read),
  raw args} into a per-thread SPSC byte ring (variable-length records: cite DS-M10 for the record layout idea);
  background thread drains and formats to a file (or keeps binary + offline decoder — say which you did).
  Deterministic CHECKs: decoded text equals expected exactly for a set of calls (ints, doubles, strings —
  string args copied, explain the lifetime problem with storing `const char*`), and full-buffer policy:
  drop (count drops exactly under a forced-full condition) vs block (no loss, producer stalls). Measured:
  per-call latency median/p99/p99.9 for this logger vs `fprintf` to a file vs `snprintf`-into-buffer vs spdlog
  (spdlog is NOT installed: you may `git clone --depth 1 --branch v1.15.0 https://github.com/gabime/spdlog`
  (or the latest v1.x tag) into /tmp/LLAT-A06_work/ and use it header-only from there — record the version; if
  that fails, say spdlog unmeasured. Do NOT `brew install`). Throughput and tail under a burst, drop counts.
  Firmware tie-in: this is exactly Trice/Segger RTT/ARM ITM-style "log ids, format on host" (hedge details),
  flash savings from not storing format strings on target.

## Overrides for this set
- Output: ROOT/lessons/<TRACK>/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + starter + solution + practice
  (.c, or .cpp for LLAT-M01 / LLAT-A06; RUST-M07 = starter/ solution/ practice/ crates) (+ demo_*). Front matter
  `updated: 2026-11-08`; `time:` Mid 70–100분, Adv 90–120분. Title line `# <ID> <title exactly as in the track
  JSON>`. md length: Mid 500–700, Adv 550–750 lines. Q&A ≥6. 1. 문제 starts with the Korean problem intro.
- Benchmarks: up to EIGHT writers of this layer + the layer-4 session run benchmarks on the same Mac at the
  same time. Record `uptime` load average next to every table, re-run final numbers when the load is lower if
  you can, tell the reader to trust RATIOS, not absolute ns. Machine info line as in CONC4. Each benchmark run
  < 30 s, no long CPU hogging. Default CHECK runs (starter/solution/practice main) must finish in < 20 s each,
  also under ASan/TSan (scale iterations down under sanitizers if needed, e.g. via `#if defined(__has_feature)`
  checks or an env var — document it).
- Deterministic CHECKs only (exact counts, integrity checks, exactly-once bitmaps, forced schedules via hooks /
  semaphores / step functions). Timings and observed-reorder counts = demo output. Starter must FAIL (exit 1)
  without hanging — TODO stubs must not spin forever (bounded loops / timeouts in the harness).
- Every asm claim = instruction you saw in real `-S`/objdump output; show the commands.
- PRIVATE scratch dir for helpers: /tmp/<ID>_work/. Never write helpers in a shared folder.

## Verify (you MUST run) — CONC1 "Verify" (C++: `c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread`):
strict build; starter FAIL exit 1 no hang; solution + practice PASS exit 0, run solution 5+ times; ASan+UBSan
clean; TSan clean. RUST-M07: cargo test starter fails / solution+practice pass, loom run, clippy, cross-builds.
Plus the md self-check (headings verbatim & ordered, ≥2 ```svg with viewBox / width ≤760 / currentColor / f-*
classes only, escaped &lt; &gt; &amp;, Q&A ≥6, 3–6 추가 연습 + `### 한 번에 검증` + `<!-- code: practice.c -->`
(or .cpp / crate path), starter marker in §3, `<!-- code: solution.c diff=starter.c -->` in §6, demo markers).

## When done
Reply (short): files written, PASS counts starter/solution/practice, ASan/UBSan + TSan results, demo files +
what they showed (key numbers + load average), anything unverified/unsure (list — the orchestrator copies it
into the guide's 작성 메모 table: 실측 한 줄 / env limits / unverified claims). Do NOT edit files outside
ROOT/lessons/<TRACK>/<ID>/ (don't run build_site.py).
