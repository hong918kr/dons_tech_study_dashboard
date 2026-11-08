# Lesson writer brief — 동시성 가이드 4층 "락을 직접 만들기"

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt). Context: Don is working through
ROOT/2026-11-08_concurrency_practice_guide.md layer by layer; layers 1–3 are written. Layer 4 = OS-M07, OS-A01,
MCPP-A03, LLAT-M02, OS-A03, LLAT-M05. The guide's idea for this layer: build real synchronization primitives
out of layer-3 atomics, and answer "why this design?" WITH MEASURED NUMBERS.

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY.
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base) → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md (Medium
   overrides) → ROOT/curriculum/C_A_LESSON_WRITER_BRIEF.md (Advanced overrides + "Concurrency items" bullet)
   → ROOT/curriculum/CONC1_LESSON_WRITER_BRIEF.md ("Overrides / extras" + "Verify" + its "Linux item" section
   for OS-A01) → ROOT/curriculum/CONC3_LESSON_WRITER_BRIEF.md ("Overrides for this set" + "Verify").
   C++ items also read ROOT/curriculum/CPP_LESSON_WRITER_BRIEF.md (.cpp markers). Everything applies unless
   overridden below. Ignore any "Do NOT edit outside lessons/C/..." path.
3. ROOT/2026-11-08_concurrency_practice_guide.md §4 — your layer and its "면접 체크" bullets: your lesson must
   let Don answer the ones relevant to your item. Also skim the §3 "작성 메모" table (measured facts on this Mac).
4. ROOT/curriculum/tracks/<TRACK>.json — your item (title/goal/task/tags/env). The lesson must do the `task`.
5. What is ALREADY taught (read the md + demos briefly, cross-reference by id, do NOT re-teach):
   - ROOT/lessons/C/C-A03/ — stdatomic basics, atomic_flag spinlock, release/acquire publish, CAS, triple buffer.
   - ROOT/lessons/ARM/ARM-M04/ — LDXR/STXR vs LSE measured on this Mac: `ldaddal` fast, but `casal` CAS loops and
     `swpa` locks 2–4× SLOWER than LL/SC under contention. OS-M07 must build on (and re-check) that, not repeat it.
   - ROOT/lessons/LLAT/LLAT-B06/ — atomic op cost, contention scaling, per-thread counters, read-before-write.
   - ROOT/lessons/OS/OS-M08/ — message passing / DCLP / pthread_once patterns. ROOT/lessons/MCPP/MCPP-M13/ —
     fences, release sequences, atomic_ref, SC-interleaving explorer. ROOT/lessons/OS/OS-B05, OS-M02 — mutex/cond.
   - ROOT/lessons/RTOS/RTOS-M03, RTOS-M04 — priority inversion / PI (firmware side of locks).

## Lanes (written IN PARALLEL — stay in yours, cross-reference others by id)
- OS-M07 (Mid, C11, mac): TAS (atomic_exchange / atomic_flag), TTAS + bounded exponential backoff
  (`__builtin_arm_yield`/`isb`-style pause — show which instruction you actually emitted), ticket lock
  (next/serving, fetch_add). Throughput vs threads 1..12 (svg from REAL numbers) AND fairness: per-thread acquire
  counts → min/max/stddev (TAS unfair, ticket FIFO). Real asm table: `-S` of each lock with default flags and
  `-mno-outline-atomics`/`-march=armv8-a` vs `+lse` variants → LDAXR/STLXR vs SWPA/CASA/LDADDA — say which one
  Apple clang emits by default on arm64-apple-macos (check, don't assume). Ticket lock's weakness: all waiters
  spin on one line, preemption of the next-in-line ⇒ convoy (oversubscribe threads > cores to show it).
  MCS/CLH queue locks = mention + see:the lock-free/scalable items only if they exist (grep tracks JSON) — no full
  implementation. Firmware: RP2040 SIO hardware spinlocks, Linux qspinlock (hedge details), single-core MCU =
  disable IRQ not spin (see:C-M14). Deterministic CHECKs: mutual exclusion (in-CS counter never >1), exact totals,
  ticket FIFO order under a forced schedule.
- OS-A01 (Adv, C11, linux — Lima VM `elinux`): Drepper "Futexes Are Tricky" mutex 0/1/2 (mutex2/mutex3 in the
  paper — cite version/date you rely on, hedge). Raw `syscall(SYS_futex, ...)` FUTEX_WAIT/FUTEX_WAKE (+ the
  _PRIVATE flag). Part B: the naive 0/1 version → (a) lost wakeup shown via a FORCED schedule (deterministic,
  e.g. hooks between the steps), (b) unnecessary FUTEX_WAKE syscalls when uncontended counted (count syscalls in
  your wrapper; optionally `strace -c -f` real output). Benchmark vs pthread_mutex_t (glibc) 1..N threads,
  uncontended + contended, syscall counts per op. Explain EINTR/EAGAIN handling, why WAIT checks the value
  atomically in-kernel (that is what kills lost wakeup), FUTEX_PRIVATE, and glibc's lowlevellock (hedge exact
  code). macOS counterpart: compare only — `os_sync_wait_on_address` (public API on macOS 14.4+, check the header
  exists in MacOSX26.4.sdk) / `os_unfair_lock`, small demo on the Mac marked mac. Firmware tie-in: RTOS mutex =
  same fast path idea (uncontended = no kernel call? check FreeRTOS: it is always a kernel call → contrast),
  Zephyr k_mutex, Linux PREEMPT_RT rt_mutex/PI-futex (FUTEX_LOCK_PI) — mention, see:RTOS-M04.
  Files: starter/solution/practice.c start with `/* ENV: linux */`; build+run IN THE VM (copy to /tmp/OS-A01_work/
  in the VM; Mac home mount is READ-ONLY) with `gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread` AND clang
  (gcc TSan in that VM SIGILLs on cond_wait — use `clang -fsanitize=thread` there). ASan/UBSan + TSan in the VM.
- MCPP-A03 (Adv, C++20, mac, host flags `-std=c++20 -Wall -Wextra -Werror -O2 -pthread`): counting_semaphore,
  latch, simple mutex built only on `std::atomic<T>::wait/notify_one/notify_all`. Teach wait's contract (blocks
  while value == old, compare is atomic w.r.t. notify ⇒ no lost wakeup; spurious returns allowed ⇒ loop), why
  notify cost matters (track waiter count to skip notify — measure the win). The mutex: use the 3-state idea in
  ~10 lines and cite OS-A01 for the full Drepper story (don't re-derive). What libc++ on this Mac actually calls
  under wait/notify (disassemble / `nm -m` the binary, or read libc++ headers in the SDK: __libcpp_atomic_wait,
  `__ulock_wait`/`os_sync_wait_on_address`, the spin-then-backoff phase) — paste what you saw. Latency compare
  vs `std::counting_semaphore`, `std::latch`, `std::mutex` (handoff latency median/p99, several runs). TSan
  stress (many threads, exact count CHECKs). Firmware: RTOS semaphore / event group (see:RTOS-B09/B10 if those
  are the semaphore items — check JSON), C++20 on MCU: `wait` needs an OS → freestanding has no wait.
- LLAT-M02 (Mid, C++20, mac, host flags as MCPP-A03): seqlock for a market-data snapshot (e.g. 64–128 B struct
  with bid/ask/qty/seq/ts + checksum field for torn detection). Correct C++ form per Boehm 2012 "Can Seqlocks Get
  Along With Programming Language Memory Models?" — atomic seq, data copied through relaxed atomics (or
  atomic_ref per word), reader `acquire` load → copy → `atomic_thread_fence(acquire)` → relaxed re-load; writer
  odd → release fence → data → release store even. Broken variants: plain memcpy (= data race, TSan reports),
  missing fence (show what can go wrong; observed or not — be honest). Torn-read stress: checksum mismatches
  counted (must be 0 for correct; report broken count real). Reader latency vs `pthread_rwlock_t` and vs a
  `std::mutex` copy, with 1 writer at several rates; writer never blocks. Writer starvation of readers (retry
  counts histogram). Linux `seqcount_t` / `read_seqbegin` (mention, hedge). Single writer only — say what breaks
  with two. Firmware: ISR writes 64-bit timestamp/encoder snapshot, main reads (seq counter instead of disabling
  IRQs; see:C-M14).
- OS-A03 (Adv, C11, mac): read-mostly config table, writer swaps pointer (atomic_exchange release), readers
  enter/exit epoch (per-reader slot: active flag + local epoch, cache-line padded), grace period = all active
  readers observed in ≥ current epoch → free the old version (classic 3-epoch EBR or simpler "wait until all
  readers pass" — explain which). Part B: free-immediately version → use-after-free shown deterministically
  (forced schedule) and caught by ASan (paste real ASan report trimmed). Benchmark: read throughput vs
  `pthread_rwlock_t` with 1..N readers + periodic writer (svg, real numbers); memory held by unreclaimed versions
  when a reader stalls (the EBR weakness; hazard pointers = layer 5, mention only). ASan clean run of solution.
  Explain kernel RCU vocabulary (rcu_read_lock, synchronize_rcu, call_rcu, quiescent state) and liburcu
  flavours — concept level, hedged. Firmware: config/calibration table swap read by a high-rate control loop;
  RTOS without MMU → reclaim via "every task passed its loop top" (quiescent state).
- LLAT-M05 (Mid, C11, mac): two threads ping-pong one cache line (atomic store/load spin, padded to 128 B —
  check this Mac's line size via `sysctl hw.cachelinesize`). Round-trip latency: median/p50/p99/min over many
  rounds (clock_gettime_nsec_np or mach_absolute_time, explain timer resolution + batching N round trips per
  sample). QoS classes `pthread_set_qos_class_self_np` QOS_CLASS_USER_INTERACTIVE / UTILITY / BACKGROUND for
  each side (P–P, E–E, P–E mixes; the Mac can't pin — QoS is a hint; this M6 has 3 perf levels: Super 2 +
  Performance 4 + Efficiency 6 — `sysctl hw.perflevel0/1/2`). Also: same-line vs separate-lines (two flags) and
  spin vs `os_sync_wait_on_address`/condvar handoff (µs vs ns — order of magnitude). Linux plan: the
  `pthread_setaffinity_np` experiment matrix (same core SMT? / same cluster / cross cluster), actually run it in
  the Lima VM if quick but label it "VM vCPU — not physical cores". svg from REAL numbers. CHECKs: protocol
  correctness only (each side sees every sequence value exactly once, final count).

## Overrides for this set
- Output: ROOT/lessons/<TRACK>/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + starter + solution + practice
  (.c, or .cpp for MCPP-A03 / LLAT-M02) (+ demo_*). Front matter `updated: 2026-11-08`; `time:` Mid 70–100분,
  Adv 90–120분. Title line `# <ID> <title exactly as in the track JSON>`. md length: Mid 500–700, Adv 550–750
  lines. Q&A ≥6.
- Benchmarks: SIX writers run benchmarks on the same Mac at the same time (plus other sessions). Record
  `uptime` load average next to every table, re-run your final numbers when the load is lower if you can, and
  tell the reader to trust RATIOS, not absolute ns. Machine info line: `sysctl -n machdep.cpu.brand_string
  hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel2.physicalcpu`, `cc --version` line 1. Keep
  each benchmark run short (< 30 s) — no long CPU hogging.
- Deterministic CHECKs only (exact counts, mutual-exclusion counters, FIFO order under forced schedules, 0 torn
  reads for correct versions). Timings, fairness spreads, observed-reorder counts = demo output.
  Starter must FAIL (exit 1) without hanging — TODO lock stubs must not spin forever (return a wrong value, or
  bounded loops / timeouts in the harness).
- Every asm claim = instruction you saw in real `-S`/objdump output; show the commands.
- Lima VM is shared with other sessions: check `limactl list` before use; if Stopped, start it and stop it again
  when done only if you started it. Work only in /tmp/<ID>_work/ inside the VM.
- PRIVATE scratch dir for helpers: /tmp/<ID>_work/ (Mac and VM). Never write helpers in a shared folder.

## Verify (you MUST run) — CONC1 "Verify" (C++: `c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread`):
strict build; starter FAIL exit 1 no hang; solution + practice PASS exit 0, run solution 5+ times; ASan+UBSan
clean; TSan clean. Plus the md self-check (headings verbatim & ordered, ≥2 ```svg with viewBox / width ≤760 /
currentColor / f-* classes only, escaped &lt; &gt; &amp;, Q&A ≥6, 3–6 추가 연습 + `### 한 번에 검증` +
`<!-- code: practice.c -->` (or .cpp), starter marker in §3, `<!-- code: solution.c diff=starter.c -->` in §6,
demo markers). OS-A01: all of that in the VM, real output pasted.

## When done
Reply (short): files written, PASS counts starter/solution/practice, ASan/UBSan + TSan results, demo files +
what they showed (key numbers + load average), anything unverified/unsure (list — the orchestrator copies it
into the guide's 작성 메모 table: 실측 한 줄 / env limits / unverified claims). Do NOT edit files outside
ROOT/lessons/<TRACK>/<ID>/ (don't run build_site.py).
