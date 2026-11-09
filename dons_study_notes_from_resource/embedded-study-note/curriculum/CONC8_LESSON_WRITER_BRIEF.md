# Lesson writer brief — 동시성 가이드 8층 "실무 · 검증 응용"

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt). Context: Don is working through
ROOT/2026-11-08_concurrency_practice_guide.md layer by layer. Layers 1–6 are written, layer 7 (hardware coherency)
is written or being written by another session (half-done files possible — cross-reference by id, never depend on
their code). Layer 8 = ELX-M04, ELX-M05, ELX-M06, LPERF-M07, LPERF-A03, TEST-A05, CAM-M11, ROS-M03, DIST-B03,
DIST-A05, QFIN-M07, OS-A11 (SIV-M09 ⭐ already exists — read its md, cross-reference, don't redo). The idea of this
layer: take what layers 1–7 taught and FIND and VERIFY it in real systems (a real kernel, a real profiler, a
reproducible simulation, a checker). Each lesson needs (1) a bug or contention problem reproduced on purpose,
(2) a tool or test that detects it, (3) a fix whose effect is shown by that same tool or test.

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY.
2. C items: ROOT/curriculum/LESSON_WRITER_BRIEF.md (base) → C_M_LESSON_WRITER_BRIEF.md (Medium overrides) →
   C_A_LESSON_WRITER_BRIEF.md (Advanced overrides + "Concurrency items" bullet; Adv items only) →
   CONC1_LESSON_WRITER_BRIEF.md ("Overrides / extras" + "Verify": pthreads, macOS gaps, forced schedules, starter
   must not hang, TSan). C++ items also read CPP_LESSON_WRITER_BRIEF.md (.cpp markers). Python items read
   LESSON_WRITER_BRIEF.md → HWLAB_B_LESSON_WRITER_BRIEF.md "Code & language" (Python 3.9 stdlib only, `python3 -I`,
   CHECK harness, `random.Random(seed)`). Ignore any "Do NOT edit outside lessons/<other>/..." path in those briefs.
3. ROOT/2026-11-08_concurrency_practice_guide.md §8 — your layer and its "면접 체크" bullets (mutex in IRQ context and
   `spin_lock_irqsave`; hard IRQ / threaded IRQ / workqueue choice; making timing bugs reproducible with deterministic
   simulation + seed; what a concurrency stress test loads at once). Your lesson must let Don answer the ones
   relevant to your item. Skim the 작성 메모 tables of §1–§6: they hold facts already MEASURED on this Mac (cite the
   lesson id instead of re-measuring when that is all you need).
4. ROOT/curriculum/tracks/<TRACK>.json — your item (title/goal/task/tags/env). The lesson must do the `task`, except
   where a lane below says what replaces it because the environment can't.
5. Already taught (skim the md, cross-reference by id, do NOT re-teach): OS-B05/OS-M02 (mutex/cond), C-M14 (ISR shared
   vars), C-A03 (atomics), OS-M07 (spinlocks), OS-A01 (futex, Drepper), OS-A03 (RCU/EBR), OS-M10 (Coffman), OS-M11
   (mini lockdep), LLAT-M02 (seqlock), DS-A01/DS-A03 (SPSC/MPMC), ARCH-M08 + MEM-M04 (false sharing), ARCH-M12 (DMA
   coherency), RTOS-B08..B10 + RTOS-M01/M03 (RTOS sync, deferred ISR), SIV-M09 (concurrency stress). `grep -l` your
   topic in ROOT/lessons to find any other overlap. Items outside these (e.g. DIST-M05, earlier ELX/CAM/ROS/QFIN items)
   are NOT written: embed whatever you need — self-contained, no shared code.

## Machine facts
Mac: Apple M6 (Super 2 + Performance 4 + Efficiency 6 = 12 cores), no core pinning, Apple clang 21, build needs
`SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk`, TSan exits 134 on races and does not report
lock-order inversions (use the VM's clang for that).

Lima VM `elinux`: aarch64, 6 vCPU, Ubuntu 26.04, kernel 7.0.0-38-generic, running (check `limactl list`, other
sessions use it — don't stop it, don't reboot it). `limactl shell elinux -- bash -lc '...'`. Passwordless sudo.
The Mac home mount is READ-ONLY in the VM: copy files to VM `/tmp/<ID>_work/` and work there. perf 7.0.14,
bpftrace 0.25, gcc, clang, make, qemu-system-aarch64 (TCG only, no /dev/kvm). VM kernel has NO lockdep and NO PMU
memory sampling (SPE). **NEVER insmod a test module into the VM's running kernel** — a buggy module takes the shared
VM down for everybody. Kernel modules run only inside a QEMU guest.

QEMU lab in the VM (read-only, copy what you need): read `/var/lab/README.txt` first (Module test recipe, debug kernel
section). Key parts: `/var/lab/out/Image-dbg` = Linux 6.18.55-dbg with PROVE_LOCKING + LOCK_STAT, DEBUG_ATOMIC_SLEEP,
FTRACE (function, function_graph, irqsoff, preemptoff, wakeup), KPROBES, SOFTLOCKUP/HUNG_TASK/WQ_WATCHDOG,
CONFIG_GPIO_SIM=y. Build tree: `make -C /var/lab/out/linux-build-dbg M=$PWD modules` (vermagic 6.18.55-dbg; don't mix
with other Images). Rootfs: copy `/var/lab/build/rootfs` (BusyBox static, `/init` runs `LAB_CMD=cmd~with~tildes` then
powers off; debugfs mounted, tracefs + configfs are not — mount them yourself). Use `-smp 4 -m 512M -cpu cortex-a57`
(MTTCG = real parallel vCPU threads). No `-dtb virt.dtb` with -smp > 1. Userspace helpers for the guest: `gcc -static`
in the VM. Always wrap qemu in `timeout`.

## Lanes (written IN PARALLEL — stay in yours, cross-reference others by id)
- **ELX-M04** (Mid, kernel C, QEMU guest) — owner of "which lock in which context". Misc char device with a counter and
  a linked list, driven from process context (ioctl/write from N stress threads) AND from a real hard-IRQ-context path
  (`hrtimer` with HRTIMER_MODE_REL_HARD, or irq_work — pick one and say why). `mode` selects none / mutex / spin_lock /
  spin_lock_irqsave / atomic_t. Show REAL guest output for: (1) no lock → lost updates (final count ≠ N×K) under
  stress; (2) mutex used from the IRQ path → `BUG: sleeping function called from invalid context` (DEBUG_ATOMIC_SLEEP);
  (3) plain `spin_lock` shared with the IRQ path → lockdep `inconsistent {IN-HARDIRQ-W} -> {HARDIRQ-ON-W} usage`;
  (4) ABBA between two spinlocks → lockdep circular-dependency report (link OS-M11: the real lockdep, class vs
  instance); (5) spin_lock_irqsave and atomic_t → exact count, dmesg has no BUG/WARNING/lockdep lines. atomic_t covers
  the counter but not the list: show why. Mention `lock_stat` numbers if they are easy (real ones only). Interview:
  mutex in IRQ, why irqsave, spinlock on UP/PREEMPT_RT (hedge RT details), sleeping while holding a spinlock.
- **ELX-M05** (Mid, kernel C, QEMU guest) — `CONFIG_GPIO_SIM=y`: create a chip via configfs
  (`/sys/kernel/config/gpio-sim`), drive the line by writing the sysfs `pull` attribute, `gpiod_to_irq`,
  `request_threaded_irq` with hard handler (timestamp only, return IRQ_WAKE_THREAD) + thread handler (processing, may
  sleep), IRQF_ONESHOT and what breaks without it for a threaded-only handler (real error from request_irq —
  run it). Expose counts + hard→thread latency histogram (min/avg/p99/max) via sysfs; check against /proc/interrupts
  exactly (guest fires K edges, all three counts equal). Show the IRQ thread `irq/NN-...` and its SCHED_FIFO prio from
  ps/`/proc`. TCG latencies are NOT real-hardware latencies: label them, trust only relative comparisons. Link
  RTOS-B10 deferred-ISR pattern. Boundary: comparing tasklet/workqueue is ELX-M06.
- **ELX-M06** (Mid, kernel C, QEMU guest) — same work item done by tasklet, system workqueue, dedicated `WQ_HIGHPRI`
  workqueue, threaded IRQ (and, since 6.9, `WQ_BH` as the tasklet replacement — verify it in 6.18 source at
  `/var/lab/src`, don't assume). IRQ source = gpio-sim, or an hrtimer if that is cleaner (say which). Report each
  mechanism's context from the code itself (`in_hardirq()/in_serving_softirq()/in_task()`, can it sleep — show the real
  splat when a tasklet calls msleep), measure IRQ→handler latency distribution two ways: internal ktime stamps AND
  ftrace (`irq_handler_entry`, `softirq_entry`, `workqueue_execute_start`, `sched_switch` — parse trace with a small
  script). Table = mechanism, context, sleep?, p50/p99/max under idle and under CPU load (real TCG numbers; trust the
  ordering only). Decision guide for the interview bullet. Boundary: threaded-IRQ basics are ELX-M05.
- **LPERF-M07** (Mid, C11 + pthreads, runs in the Lima VM itself — userspace only) — server with a global-mutex map;
  `futex_wait.bt` bpftrace script (sys_enter/exit_futex, FUTEX_WAIT, wait histogram by uaddr and ustack) and `perf lock
  contention -b` (try `-b`; if BPF skeleton is missing or it fails, paste the real error and use what works, e.g.
  tracepoint mode). Name the hot lock from the addresses/stacks, fix with striping + an RCU-style or seqlock read path,
  show before/after p50/p99 and total wait. In the VM a vCPU can be preempted by the host, so trust ratios. Correctness
  CHECKs (map contents, counts) must be deterministic and separate from measurement. Bpftrace output goes to the md as
  real output. Code files start with `/* ENV: linux */`.
- **LPERF-A03** (Adv, C11 + Python 3.9, VM + Mac) — the task's real `perf c2c` needs PEBS/SPE, which this VM has
  no access to. Do this instead and SAY so at the top: (1) write the hidden-false-sharing stats program; run `perf c2c
  record` in the VM and paste the REAL failure; (2) real throughput before/after fixes (padding, thread-local aggregate)
  on Mac and VM; (3) a mini-c2c: `instrumented trace` (thread, address, code site, R/W, from a build that records
  sampled accesses) → per-cache-line coherence model (who holds M, load that finds M elsewhere = HITM) → report in
  the same layout as `perf c2c report` Shared Data Cache Line Table + Pareto offsets. Every such table is labelled
  "모델 출력 — perf 결과 아님". Starter = analyzer TODOs, checked on golden traces. Before/after shows HITM → 0 in the
  model; (4) teach the reading of the real report from `man perf-c2c` in the VM (quote real text) and how to run it on
  Graviton metal/Intel (hedged, marked unverified). Never fabricate a perf c2c output. 128 B granule on M6: see ARCH-M08.
- **TEST-A05** (Adv, C11, mac) — deterministic simulator: virtual clock + event queue, fake timer/UART/ISR, firmware
  logic under test written against that HAL so the same code could run for real. Seeded PRNG (xorshift) permutes event
  order and timing. A planted race (e.g. timeout timer vs RX-complete ISR both completing the same request) is found by
  a seed sweep; the failing seed replays bit-identically (compare event-trace hash); fixed code passes the sweep. Also
  show: shrink the failing trace, coverage of orderings vs real threads (SIV-M09 boundary: real stress finds what the
  sim doesn't model and vice versa). Mention FoundationDB, TigerBeetle VOPR, PCT (hedged). No wall clock in the core.
- **CAM-M11** (Mid, C11 + pthreads, mac) — V4L2-like buffer states (DEQUEUED/QUEUED/ACTIVE/DONE), qbuf/dqbuf API,
  a "DMA completion IRQ" producer thread, slow consumer. Policies: drop new frame (keep ACTIVE buffer, count drop) vs
  reuse the last buffer vs overwrite oldest DONE (compare). Sequence-number gap detection. Invariant checked
  continuously: each buffer has exactly one owner, app never touches a queued buffer (poison pattern catches it).
  Deterministic forced schedules for CHECKs. Hedge vb2 state names you can't verify; link ARCH-M12 for where cache
  maintenance belongs in the state machine.
- **ROS-M03** (Mid, C++20 + std::thread, mac) — ROS 2 is NOT installed (VM is Ubuntu 26.04; don't install it). Say
  so at the top. Build a mini executor reproducing rclcpp semantics: SingleThreadedExecutor, MultiThreadedExecutor,
  MutuallyExclusive and Reentrant callback groups, timer, service client/server. Reproduce the classic: a timer callback
  makes a synchronous service call while the response callback can't run (same thread / same MutuallyExclusive group);
  detection = bounded wait + wait-for report, never hang. Matrix table of the 2×2 (plus separate group) from REAL runs.
  Fixes: separate groups + MultiThreaded, or async call. Real rclcpp/rclpy code shown under "ROS 2가 있을 때", marked
  not run here. Map to Coffman (OS-M10). Build flags `-std=c++20 -Wall -Wextra -Werror -O2 -pthread`.
- **DIST-B03** (Basic, C11, mac, 400–600 lines, Q&A ≥5) — vector clock tick/merge/compare, 3-ECU log merge marking
  concurrent events, Lamport clock + the counterexample test. Firmware: CAN/Ethernet ECUs without synced clocks.
- **DIST-A05** (Adv, Python 3.9 stdlib, mac) — Wing-Gong backtracking linearizability checker with memoized (linearized
  set, state) cache (Lowe's), history = invoke/return records for a register/KV; per-key partition. Embedded mini quorum
  store simulator (DIST-M05 isn't written) with fault injection (R+W ≤ N, delayed replicas, no read repair) finds a
  real violating history; print the minimal counterexample; R+W > N passes the same seed set (note that quorum alone is
  not linearizable without read-repair/write-back — check which config really is, and report what the checker says).
  Mention Jepsen/Knossos/Porcupine (hedged). Exponential worst case: show runtime vs history length with real numbers.
- **QFIN-M07** (Mid, C++20, mac) — OMS: ClOrdID/OrigClOrdID chains, state per order, CumQty/LeavesQty/position.
  Scenario tests: too-late-to-cancel, partial fill during replace, out-of-order ExecutionReports, unsolicited cancel.
  This race is across asynchronous messages, not threads: say so and link it (the same "two completions for one
  request" shape as TEST-A05). FIX tag semantics from memory must be hedged. Build flags `-std=c++20 -Wall -Wextra
  -Werror -O2`, sanitizers ASan/UBSan.
- **OS-A11** (Adv, C11 + pthreads, mac, kqueue) — capstone KV server: kqueue event loop, bounded queue + condvar worker
  pool, slab allocator, SIGTERM graceful drain (no accepted request lost), SIGHUP atomic config reload (pointer swap +
  safe reclamation; link OS-A03), append-only log + restart recovery. Tests are deterministic and bounded: spawn the
  server, scripted clients, count every response, signal mid-run, SIGKILL + restart → recovered state. A load test
  demo with real p50/p99. TSan + ASan clean. Signals: self-pipe or EVFILT_SIGNAL, say which. Multi-file is allowed
  but keep starter/solution/practice naming.

## Kernel items (ELX-*): file layout and checking
- `starter.c` / `solution.c` / `practice.c` are module sources starting with `/* ENV: qemu */`, built by a `Makefile`
  (Kbuild objs); `stress.c` (or similar) = static userspace driver if needed; `check.sh <starter|solution|practice>` runs
  inside the VM from any directory: copies the lesson to `/tmp/<ID>_work/`, builds module + helpers, builds a private
  initramfs, boots Image-dbg under `timeout`, decides PASS/FAIL from guest output (counts + dmesg grep for
  `BUG:|WARNING:|lockdep|possible .* deadlock|inconsistent`), prints the verdict, exit 0 PASS / 1 FAIL. Starter exits 1
  within 2 minutes. Solution passes 3 runs in a row. Timing never decides PASS/FAIL.
- solution.c = starter.c with TODO bodies filled, same as all lessons. Kernel code uses the kernel's formatting.
- Show real dmesg/lockdep text in the md (trim with "…", never invent lines). Always say kernel version 6.18.55-dbg
  and "QEMU TCG cortex-a57 -smp 4".
- Parallel writers share VM CPU: QEMU runs may be slow, so use generous `timeout` and small iteration counts.

## Overrides for this set
- Output: ROOT/lessons/<TRACK>/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + starter + solution + practice (+ demo_*,
  helpers). Front matter `updated: 2026-11-08`; `time:` Basic 50–70분, Mid 70–100분, Adv 90–120분. Title line
  `# <ID> <title exactly as in the track JSON>`. md length: Basic 400–600, Mid 500–700, Adv 550–750 lines. Q&A ≥6
  (Basic ≥5) with 꼬리질문. Korean prose like the other lessons; first sentence of §1 connects to firmware/validation work.
- Items whose task can't be done as written (LPERF-A03, ROS-M03) state that in a short blockquote at the top of §1:
  what's real here, what's modeled, what needs other hardware.
- Deterministic CHECKs only. Timing, throughput and latency = demo output with real numbers.
- Benchmarks: other writers share this Mac. Record `uptime` load next to every table, keep each run < 30 s, tell the
  reader to trust ratios. Machine line: `sysctl -n machdep.cpu.brand_string hw.perflevel0.physicalcpu
  hw.perflevel1.physicalcpu hw.perflevel2.physicalcpu`, `cc --version` line 1 (VM: `uname -r`, `nproc`, `gcc --version`).
- Never hang: bounded waits and timeouts everywhere in starter/solution/practice/demo.
- PRIVATE scratch dir for helpers: /tmp/<ID>_work/ on the Mac and in the VM. Never write helpers in a shared folder.
- Accuracy: don't claim tool behavior you didn't run. Read kernel source at `/var/lab/src` before stating kernel internals.
  API details or history you can't check are hedged in the text and listed in your final reply.
- No web installs (no apt/brew/pip/git clone) unless your lane says so.

## Verify (you MUST run)
- C items on Mac: CONC1 "Verify" exactly (strict build with -pthread; starter FAIL exit 1 no hang; solution + practice
  PASS exit 0, solution 5+ runs; ASan+UBSan clean; TSan clean). C++ the same with its flags. Python:
  `python3 -I starter.py` (FAIL, exit 1), `python3 -I solution.py` / `practice.py` (PASS, exit 0),
  `python3 -I -W error -X dev solution.py`.
- LPERF-M07 / the C parts of LPERF-A03 in the VM: gcc AND clang strict builds, ASan/UBSan and TSan.
- ELX-*: `check.sh starter` exit 1; `check.sh solution` exit 0 three times; `check.sh practice` exit 0.
- md self-check: headings verbatim & ordered (## 학습 목표, ## 1. 문제 with ### Part A — / ### Part B — / ### Part C —,
  ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수, ## 8. 면접 Q&A,
  ## 9. 추가 연습 + ### 한 번에 검증, ## 10. 핵심 정리, ## 참고자료), ≥2 ```svg (viewBox, width ≤760, currentColor +
  f-acc/f-ok/f-warn/f-mut only, escaped &lt; &gt; &amp;), Q&A as `<details><summary>Q1. ...`, 3–6 추가 연습 with answers in
  <details>, `<!-- code: starter.X -->` in §3, `<!-- code: solution.X diff=starter.X -->` in §6, `<!-- code: practice.X
  -->` in §9. Paste only real output you ran.

## When done
Reply (short): files written, PASS counts starter/solution/practice, sanitizer results, demo files + what they showed
(key numbers + load), anything unverified/unsure as a list (the orchestrator copies it into the guide's 작성 메모 table:
환경 한계 / 확인 안 된 주장). Do NOT edit files outside ROOT/lessons/<TRACK>/<ID>/ (don't run build_site.py).
