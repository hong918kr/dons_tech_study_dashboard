# Lesson writer brief — 동시성 가이드 6층 "교착 상태 (deadlock)"

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt). Context: Don is working through
ROOT/2026-11-08_concurrency_practice_guide.md layer by layer; layers 1–4 are written (layer 5 lock-free is not).
Layer 6 = OS-M10 ⭐, OS-M11, ALGO-A10, STOR-M08, STOR-M10, SOCX-M05, DCN-M05. The guide's idea for this layer:
the SAME "circular wait" shows up in software locks (OS), offline analysis (ALGO), databases (STOR), chip
interconnect (SOCX) and the network (DCN) — every lesson must make that link explicit (one short table/paragraph
"같은 순환 대기, 다른 층" pointing to the sibling ids) and let Don answer the guide's §6 면접 체크:
4 Coffman conditions + how to break each · runtime lock-order checking (lockdep) · cycles in a dependency
graph → SCC (Tarjan/Kosaraju).

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY.
2. C items: ROOT/curriculum/LESSON_WRITER_BRIEF.md (base) → C_M_LESSON_WRITER_BRIEF.md (Medium) →
   C_A_LESSON_WRITER_BRIEF.md ("Concurrency items" bullet; Advanced overrides for ALGO-A10) →
   CONC1_LESSON_WRITER_BRIEF.md ("Overrides / extras" + "Verify": pthreads, macOS gaps, forced schedules,
   starter must not hang, TSan). Python items (SOCX-M05, DCN-M05): LESSON_WRITER_BRIEF.md →
   HWLAB_B_LESSON_WRITER_BRIEF.md "Code & language" section (Python 3.9 stdlib only, `python3 -I`, CHECK harness,
   deterministic `random.Random(seed)`, gen_*.py for any data file ≤200KB). Ignore any "Do NOT edit outside
   lessons/<other>/..." path in those briefs.
3. ROOT/2026-11-08_concurrency_practice_guide.md §6 (your layer) and the 작성 메모 tables of §1–§4.
4. ROOT/curriculum/tracks/<TRACK>.json — your item (title/goal/task/tags/env). The lesson must do the `task`.
5. Already taught (skim, cross-reference by id, do NOT re-teach): ROOT/lessons/OS/OS-B05 + OS-M02 (mutex/cond),
   OS-M07 (spinlocks), OS-A01 (futex), RTOS-M03/M04/M05 (priority inversion / PI / ceiling — ceiling also
   prevents deadlock: say so in OS-M10), C-M14 (ISR vs main). Check `ls ROOT/lessons/*/` for what exists.
   Earlier items of ALGO / STOR / SOCX / DCN are NOT written: anything you need (a graph lib, a B+tree, a mesh
   simulator, a switch model) you embed in your own lesson — self-contained, no shared code.

## Lanes (written IN PARALLEL — stay in yours, cross-reference others by id)
- **OS-M10** (Mid, C11 + pthreads, mac) — owner of the 4 Coffman conditions table (mutual exclusion,
  hold-and-wait, no preemption, circular wait → which fix breaks which). Naive philosophers deadlock reproduced
  DETERMINISTICALLY (forced schedule: every philosopher holds the left fork, then a barrier you build with
  mutex+cond — macOS has no pthread_barrier_t — then all ask for the right fork) and DETECTED without hanging
  (macOS has no pthread_mutex_timedlock → trylock + bounded polling, or a watchdog that sees no progress) — the
  harness then reports `DEADLOCK: P0 holds F0 waits F1 … P4 holds F4 waits F0` and recovers so the starter FAILs
  and exits. Fixes: (1) global lock order (lowest fork first), (2) trylock + randomized bounded backoff
  (show livelock risk when backoff is not randomized — forced schedule or measured), (3) at most N−1 seated
  (semaphore built from mutex+cond; unnamed sem_init is unsupported on macOS). Also mention (not implement)
  waiter/arbitrator, Chandy–Misra. demo_bench.c: meals/s and per-philosopher meal fairness (min/max) for each
  fix with 5 and 12 philosophers, real numbers + `uptime` load + machine line; trust ratios. Firmware: RTOS
  task lock ordering, priority ceiling (see:RTOS-M05) as deadlock prevention, ISR can never wait on a mutex
  (see:C-M14), watchdog as last-resort "preemption". Boundary: no lockdep (OS-M11), no SCC (ALGO-A10).
- **OS-M11** (Mid, C11 + pthreads, mac) — mini lockdep. `ld_mutex_t` wrapper around pthread_mutex_t with a
  lock CLASS id (name string — explain class vs instance, why lockdep keys by class); `_Thread_local` held-lock
  stack; on acquire add edge held→new for every held lock into a global graph (protected by its own internal
  lock — explain why that internal lock must not itself be tracked), check reachability new→…→held by DFS when
  an edge is NEW (cache known edges so the steady state is cheap — measure cost per lock/unlock vs plain
  pthread_mutex in a demo), and on a cycle print both paths WITH the call stacks captured when each edge was first
  recorded (`backtrace()`/`backtrace_symbols()` from <execinfo.h> work on macOS — compile demos with
  `-g -rdynamic`-equivalent if needed; paste real output, trimmed). Key point: the ABBA bug is reported even
  when the two threads run SEQUENTIALLY and never actually deadlock — that's the whole value. Part B: trylock
  edges (don't add a blocking dependency — say why), recursive lock/self-deadlock, nested same-class locks
  (lockdep's subclass / nesting annotation idea), lock released out of order, false positives you'd see.
  Compare with real tools on this Mac: TSan's lock-order-inversion report (`TSAN_OPTIONS=detect_deadlocks=1`
  — try it, paste what really happens, honest if unsupported), helgrind/Linux lockdep (concept, hedged; Linux
  lockdep vocabulary: lock class, dependency chain, `CONFIG_PROVE_LOCKING`). Boundary: online/incremental cycle
  check at edge insert; the offline whole-log SCC analysis is ALGO-A10.
- **ALGO-A10** (Adv, C11, mac, 550–750 lines, Q&A ≥6) — offline tool: parse a per-thread lock event log
  (text format you define: `t=<thread> acq <lock> @<site>` / `rel <lock>`), build the lock-order graph with edge
  annotations (thread, site), run Tarjan (ITERATIVE — explicit stack; show the recursive version would blow the
  stack on a 1e5–1e6-node chain and measure/describe that honestly) and Kosaraju, report every non-trivial SCC
  (size>1, plus self-loops) with one concrete witness cycle per SCC and the threads/sites that create it.
  Cross-check Tarjan vs Kosaraju on many random graphs (xorshift seed, canonicalized SCC partitions equal) and on
  hand-made cases (DAG, single big cycle, nested cycles, self-loop, disconnected). Part B: why SCC ≠ "list all
  cycles" (Johnson's algorithm can be exponential — mention), gate locks (a cycle where every edge was taken
  while holding a common outer lock G cannot deadlock → filter those; show one), single-thread cycles (A→B and
  B→A both from the same thread at different times — still a bug? explain), O(V+E) proof sketch. Part C: a
  driver/firmware log (e.g. SPI bus lock, DMA channel lock, power-domain lock, logger lock across 4 threads)
  with one planted 3-lock cycle and one gated (harmless) cycle → report. Benchmark demo: 1e5–1e6 edges runtime
  for both algorithms (real numbers). Boundary: OS-M11 = runtime instrumentation; you are the offline analyzer.
- **STOR-M08** (Mid, C11 + pthreads, mac) — lock manager with S/X modes (compatibility matrix, per-object
  FIFO request queue, upgrade S→X and its classic upgrade-deadlock), strict 2PL (release at commit/abort only —
  show with a forced schedule why non-strict 2PL allows cascading aborts / dirty reads), wait-for graph built
  from the queues, cycle detection (periodic or on-block — say which and why), victim selection (youngest txn /
  fewest locks held / least work — implement one policy, compare in a demo), abort = undo log rollback +
  release + retry with the same timestamp (starvation avoidance — explain). Deterministic CHECKs: scripted
  transactions driven step-by-step reproduce the classic T1: X(A)→X(B), T2: X(B)→X(A) cycle → exactly one
  victim; then a multi-threaded bank transfer stress (N accounts, M threads, random transfers, xorshift seed):
  total balance invariant holds exactly, commit+abort counts add up, no hang (bounded runtime). Practice:
  wait-die vs wound-wait (prevention vs detection) and timeout-based detection trade-off. TSan clean. Firmware/
  storage tie-in: Don is ex-SSD FW — FTL metadata locks / NVMe multi-queue; mention where real DBs do this
  (Postgres deadlock_timeout + wait-for check, InnoDB detector — hedge details).
- **STOR-M10** (Mid, C11 + pthreads, mac) — small in-memory B+tree (int keys, fanout small e.g. 8–16, insert +
  lookup; no delete — say so) with per-node pthread_rwlock_t latches. (1) pessimistic crabbing: take latches
  top-down, release ancestors when the child is "safe" (won't split); (2) optimistic: read-latch down, write-latch
  only the leaf; if the leaf would split, release and restart pessimistically — count restarts. Deadlock angle
  (why this lesson is in layer 6): latches have NO deadlock detector, so the protocol must be deadlock-free by
  ORDER (always top-down, never upgrade a read latch in place — show what an in-place upgrade does with a forced
  two-thread schedule: both hold R, both want W → stuck; detect with bounded wait so the harness reports it).
  Latch vs lock distinction (vs STOR-M08). CHECKs: after concurrent inserts every key present exactly once,
  B+tree invariants (sorted, fill bounds, all leaves same depth, sibling links), deterministic key sets per thread.
  demo_bench.c: throughput insert/lookup mixes (read-heavy / write-heavy) × threads 1..8 for coarse global
  rwlock / crabbing / optimistic, real numbers + load + machine line; svg from real numbers. TSan clean on
  solution; a planted bug (release parent before child latched) shown in a demo with the real TSan/assert result.
  Mention B-link trees (Lehman–Yao) and OLC (optimistic lock coupling, Leis et al.) — concept, hedged.
- **SOCX-M05** (Mid, Python 3.9 stdlib, mac) — CDG tool: topology (k×k mesh, k-ary 2-cube torus), channels
  = directed links (× VC), routing function R(current, dest) → allowed next channels; CDG edge c1→c2 if some
  packet can hold c1 and request c2. Show: mesh XY → acyclic (topological order printed / proof idea), unrestricted
  minimal (fully adaptive without VCs) on mesh → cycle found (print it), torus DOR without VCs → cycle on each ring,
  dateline with 2 VCs (switch VC0→VC1 when crossing the dateline) → acyclic. Dally–Seitz theorem stated
  precisely (acyclic CDG ⇒ deadlock-free for deterministic routing; Duato's theorem for adaptive = mention).
  Then a small cycle-based wormhole simulator (flit buffers per input VC, credit/backpressure, k=4 ring or 4×4
  torus) where a planted traffic pattern deadlocks without VCs (detect: no flit moved for W cycles while flits
  are in flight → report the circular wait of buffers) and completes with dateline VCs; plus turn-model
  (west-first / odd-even) as one practice. Keep runs ≤ ~20 s. Boundary: ARCH/SIM items own generic NoC perf;
  you own deadlock.
- **DCN-M05** (Mid, Python 3.9 stdlib, mac) — PFC deadlock. Model a 2-tier leaf-spine (e.g. 4 leaf, 2 spine),
  lossless priority with per-(switch, ingress port, priority) buffers, XOFF/XON thresholds, PAUSE propagation
  (a hop-by-hop, cycle-stepped simulation). Normal up-down routing → buffer dependency graph acyclic. Link
  failure + reroute creates leaf→spine→leaf→spine bounce paths → cyclic buffer dependency (CBD) → with enough
  load, all buffers in the cycle full + all paused → deadlock (throughput 0 persists after sources stop =
  the defining symptom vs congestion). Checker: build BDG nodes (switch, in_port, prio), edges from routes, DFS
  cycle detection, print the cycle. Mitigations compared in a table from REAL sim output: (1) enforce up-down
  routing (drop/avoid bounce routes), (2) per-hop priority increase (each bounce moves to a higher lossless
  class; finite classes — Tagger idea), (3) PFC watchdog (detect stuck paused queue for T, drop/discard → breaks
  "no preemption" at the cost of losses). Map each to the Coffman condition it breaks. References: Microsoft
  "RDMA over Commodity Ethernet at Scale" (SIGCOMM 2016), Tagger (CoNEXT 2017), Hu et al. "Deadlocks in
  Datacenter Networks: Why Do They Form, and How to Avoid Them" (HotNets 2016) — titles from memory: hedge.
  Vendor PFC watchdog defaults = hedge. Boundary: DCN congestion control (DCQCN etc.) is other DCN items.

## Overrides for this set
- Output: ROOT/lessons/<TRACK>/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + starter + solution + practice
  (.c, or .py for SOCX-M05 / DCN-M05) (+ demo_*). Front matter `updated: 2026-11-08`; `time:` Mid 70–100분,
  Adv 90–120분. Title line `# <ID> <title exactly as in the track JSON>`. md length: Mid 500–700, Adv 550–750
  lines. Q&A ≥6 with 꼬리질문.
- Deadlocks must never hang a test run: every place a deadlock can happen in starter/solution/practice/demo
  uses bounded waits (trylock polling with a deadline, cond waits with pthread_cond_timedwait, sim step limits)
  and reports the circular wait it found. Starter FAILs with exit 1 within seconds.
- Deterministic CHECKs only (forced schedules, scripted step-driven transactions, fixed seeds, exact invariants).
  Throughput / fairness / runtime = demo output with real numbers.
- Benchmarks: several writers + other sessions share this Mac. Record `uptime` load next to every table, keep
  each run < 30 s, tell the reader to trust ratios. Machine line: `sysctl -n machdep.cpu.brand_string
  hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel2.physicalcpu`, `cc --version` line 1.
- PRIVATE scratch dir for helpers: /tmp/<ID>_work/. Never write helpers in a shared folder.
- Accuracy: Coffman, Elphick, Shoshani 1971 (cite); OSTEP ch. 32 "Common Concurrency Problems" (check the
  chapter number in the OSTEP PDF if you can find it under ~/workspace — else hedge); Dally & Seitz 1987;
  Tarjan 1972. Don't claim tool behavior you didn't run.

## Verify (you MUST run)
- C items: CONC1 "Verify" exactly (strict build with -pthread; starter FAIL exit 1 no hang; solution + practice
  PASS exit 0, solution 5+ runs; ASan+UBSan clean; TSan clean). ALGO-A10 has no threads → ASan/UBSan only
  (TSan n/a, say so).
- Python items: `python3 -I starter.py` (FAIL, exit 1), `python3 -I solution.py` / `practice.py` (PASS, exit 0),
  `python3 -I -W error -X dev solution.py` (no warnings), gen_*.py regenerates identical data (shasum).
- md self-check: headings verbatim & ordered (## 학습 목표, ## 1. 문제 with ### Part A — / ### Part B — /
  ### Part C —, ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수,
  ## 8. 면접 Q&A, ## 9. 추가 연습 + ### 한 번에 검증, ## 10. 핵심 정리, ## 참고자료), ≥2 ```svg (viewBox, width ≤760,
  currentColor + f-acc/f-ok/f-warn/f-mut only, escaped &lt; &gt; &amp;), Q&A ≥6 as `<details><summary>Q1. ...`,
  3–6 추가 연습 with answers in <details>, `<!-- code: starter.X -->` in §3, `<!-- code: solution.X diff=starter.X -->`
  in §6, `<!-- code: practice.X -->` in §9. Paste only real output you ran.

## When done
Reply (short): files written, PASS counts starter/solution/practice, sanitizer results, demo files + what they
showed (key numbers + load average), anything unverified/unsure as a list (the orchestrator copies it into the
guide's 작성 메모 table: 환경 한계 / 확인 안 된 주장). Do NOT edit files outside ROOT/lessons/<TRACK>/<ID>/
(don't run build_site.py).
