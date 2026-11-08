# Lesson writer brief — 동시성 가이드 1층 "기본기" (OS-B04 · OS-B05 · OS-B06 · TOOL-M09 · OS-M02 · LPERF-B10)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt). Context: Don is working through
ROOT/2026-11-08_concurrency_practice_guide.md layer by layer; layer 1 = these 6 items. They are the
first lessons in the OS / TOOL / LPERF tracks.

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY.
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base rules) → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md (Medium
   overrides) → ROOT/curriculum/C_A_LESSON_WRITER_BRIEF.md (its "Concurrency items" bullet: pthreads, TSan,
   deterministic CHECKs). Everything there applies unless overridden below. Ignore their "Do NOT edit outside
   lessons/C/..." path — your dir is given below.
3. ROOT/2026-11-08_concurrency_practice_guide.md §1 — the layer this lesson belongs to, including its
   "면접 체크" bullets: your lesson must let Don answer the ones relevant to your item.
4. ROOT/curriculum/tracks/<TRACK>.json — your item (title/goal/task/tags/env). The lesson must do the `task`.
5. Reference lessons for depth/style: ROOT/lessons/C/C-A03/ (atomics, TSan, demos) and ROOT/lessons/C/C-M14/.

## Lanes (the 6 are written IN PARALLEL — stay in yours, cross-reference by id)
- OS-B04 (Basic): threads share the address space; `counter++` race reproduced; real arm64 asm of
  `counter++` (ldr/add/str) from `cc -O0/-O2 -S` + thumbv7em asm; lost-update interleaving table; TSan only
  "first sighting" (one report shown) — deep TSan report reading is TOOL-M09's job.
- OS-B05 (Basic): fix with (1) one global mutex, (2) sloppy/approximate counter (OSTEP ch.29, per-thread local
  + threshold flush), (3) atomic fetch_add; throughput for threads 1..8 → table + an svg chart from REAL
  measured numbers (demo_bench.c). Lock scope/granularity. Don't re-teach memory_order (C-A03 owns it).
- OS-B06 (Basic): bounded buffer with pthread_cond; reproduce OSTEP ch.30 broken versions — `if` instead of
  `while`, and a single condvar shared by producers and consumers — with multiple producers/consumers, then
  fix (while + two condvars `empty`/`fill`). signal vs broadcast. Spurious wakeup.
- TOOL-M09 (Mid): read TSan reports for 3 bugs: shared-counter race, flag read without lock (busy-wait on a
  plain bool), lock-order inversion (TSan "lock-order-inversion (potential deadlock)"); fix each with
  mutex/atomic. Annotate a real report line by line (stack of each access, "Previous write", "Location is
  global", mutex creation sites). Also: TSAN_OPTIONS (halt_on_error, second_deadlock_stack=1, history_size),
  suppressions, cost (~5–15x CPU, 5–10x mem), what TSan can't see (code not instrumented, races not executed).
- OS-M02 (Mid): pipe ping-pong between 2 processes and between 2 threads, hundreds of thousands of round
  trips; measure a plain syscall cost too (OS-B03 is not written yet — measure what you need, e.g. 1-byte
  write/read on a pipe without blocking, or getppid) and subtract to estimate switch cost. Error sources:
  no CPU pinning on macOS (no sched_setaffinity; thread_policy affinity tags are hints and ignored on Apple
  Silicon — verify what you claim), P vs E cores, timer resolution (clock_gettime_nsec_np / mach_absolute_time),
  warm-up, cache effects, cross-core wakeup vs same-core. Show real numbers with min/median/p99 over several runs.
- LPERF-B10 (Basic, env linux): see "Linux item" below.

## Overrides / extras for this set
- Output: ROOT/lessons/<TRACK>/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + starter.c + solution.c +
  practice.c (+ demo_*.c). Front matter `updated: 2026-11-08`; `time:` Basic 50–70분, Mid 70–100분.
  Title line `# <ID> <title exactly as in the track JSON>`. Basic md 450–600 lines, Mid 500–700.
- Language: C11 + pthreads. `#define _POSIX_C_SOURCE 200809L` (or `_DARWIN_C_SOURCE` when you need
  Darwin-only APIs like mach_absolute_time / clock_gettime_nsec_np) before includes. Add `-pthread` to every
  build command you show and run.
- macOS gaps to respect (state them where relevant): no `pthread_barrier_t` (build your own with mutex+cond if
  needed), no `sched_setaffinity`, unnamed POSIX semaphores (`sem_init`) are unsupported/deprecated on macOS
  (use mutex+cond or dispatch_semaphore), TSan on this Mac exits 134 when it reports a race.
- Checked tests must be deterministic: CHECK only what holds on every run (e.g. correct version's final count
  == N*K; buffer delivers every item exactly once; FIFO order per producer). Race/broken behaviour is shown
  via demo_*.c with the REAL output you got (several runs), or via a deterministic scripted interleaving
  (e.g. the harness drives "threads" step by step, or uses hooks/condvars to force the bad schedule) — the
  scripted approach is preferred for Part B so the failure is reproducible, but say clearly that it is a
  forced schedule. Starter must not hang: TODO stubs return dummy values; any wait in the harness has a
  timeout or bounded loop so a wrong starter FAILs instead of deadlocking.
- Timing numbers never go in CHECKs. Benchmarks live in demo_bench.c (or similar); md shows the real numbers
  with machine info (Apple M6, `sysctl -n hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu`,
  `cc --version` first line).
- Sanitizers: run solution/practice under ASan+UBSan AND separately under `-fsanitize=thread` (all clean).
  Show real TSan reports from demos (trim long stacks with "…" but never invent lines).
- Firmware tie-in (1. 문제 first paragraph + Part C): how the same thing appears on MCU/RTOS (ISR vs main loop,
  FreeRTOS tasks, Cortex-M without/with LDREX) — cross-reference C-M14 / C-A03 / RTOS items by id; don't
  re-teach them. Part C should be a realistic scenario (e.g. sensor-sample producer thread → logger consumer,
  per-core statistics counters, UART RX ring between threads, telemetry flag).
- Accuracy: cite POSIX (pthread_cond_wait spurious wakeups, mutex must be held), OSTEP chapter numbers,
  C11 5.1.2.4 (data race definition). Don't claim an asm instruction you didn't observe.

## Linux item (LPERF-B10 only)
- Lima VM `elinux` (aarch64 Ubuntu). It is Stopped: `limactl start elinux` (takes ~1 min), then run commands via
  `limactl shell elinux -- bash -lc '...'`. The Mac home dir is mounted in the VM (check `ls` of ROOT path from
  inside); if it is read-only, copy sources to `/tmp/lperf_b10/` in the VM and build/run there.
- Check schedstat availability first (`cat /proc/self/schedstat`, `/proc/sys/kernel/sched_schedstats`) and
  report what you found. Install missing tools with `sudo apt-get install -y stress-ng` if needed.
- Code files start with `/* ENV: linux */` as their first line (the Mac verifier skips them), but you MUST
  build and run starter/solution/practice inside the VM with the strict flags (`gcc -std=c11 -Wall -Wextra
  -Werror -O2 -pthread`) and show real output. Parsing logic (schedstat/status/stat line parsers, the
  accounting math) should be testable from fixed sample strings so CHECKs are deterministic; the live
  sampling tool is a demo whose real output (per-thread table: run ns / runqueue wait ns / sleep+block ns vs
  wall) goes in the md. Oversubscribe (threads > `nproc`) to make runqueue wait visible.
- When done, `limactl stop elinux` only if you started it.

## Verify (you MUST run; use your own temp names like /tmp/<ID>_st)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
    cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # some FAIL, exit 1, no hang
    cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o /tmp/<ID>_so && /tmp/<ID>_so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c   # clean (and practice.c)
    cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c                                       # clean (and practice.c)
  Run solution 5+ times to make sure it's not flaky. (LPERF-B10: same, with gcc inside the VM.)
- solution.c = starter.c with only TODO bodies filled (diff highlights changed lines).
- Self-check md: headings verbatim & in order, ≥2 ```svg figures (viewBox, width ≤ 760, currentColor +
  f-acc/f-ok/f-warn/f-mut only, escape &lt; &gt; &amp;), Q&A ≥5 (Basic) / ≥6 (Mid) as
  `<details><summary>Q1. ...`, 추가 연습 3–6 with `### 한 번에 검증` + `<!-- code: practice.c -->`,
  `<!-- code: starter.c -->` in §3, `<!-- code: solution.c diff=starter.c -->` in §6, demo files referenced
  with `<!-- code: demo_x.c -->` markers where you want them inlined.

## When done
Reply (short): files written, PASS counts starter/solution/practice, ASan/UBSan + TSan results, demo files +
what they showed (key numbers), anything you were unsure about. Do NOT edit files outside
ROOT/lessons/<TRACK>/<your ID>/ (don't run build_site.py — the orchestrator does).
