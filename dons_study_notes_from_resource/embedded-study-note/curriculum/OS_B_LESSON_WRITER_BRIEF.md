# Lesson writer brief — track OS (운영체제), Basic level (OS-B01–B03, B07–B14)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math, read OSTEP
(Korean notes exist), prepping for embedded/firmware/embedded-Linux/silicon-validation interviews.
He re-checks fundamentals by DOING: every lesson = one interview-style problem with tests.
OS-B04/B05/B06 (threads, mutex, condvar) and OS-M02, M07, M08, M10, M11, A01, A03, A05, A11 already exist;
C/DS/CPP/ARCH/ALGO Basic–Advanced lessons exist — cross-reference by id ("(OS-B04 참고)"), don't re-teach.

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY (front matter, headings verbatim and in order, svg rules,
   code rules, `<!-- code: ... -->` markers, `<details>` folding).
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base rules). Everything there applies unless overridden below.
3. ROOT/curriculum/tracks/OS.json — your item (title/goal/task/tags). The lesson must do the item's `task`
   completely (every sub-feature and every measurement it names). Skim the M/A items so you stay in your lane.
4. Reference lessons for depth/tone/harness: ROOT/lessons/OS/OS-B04/ and ROOT/lessons/OS/OS-B06/ (same track,
   Basic, demos + real output), ROOT/lessons/C/C-B01/ (CHECK macro, "N/M passed", return 1 on failure).
5. OSTEP material on this disk (cite chapter numbers; don't copy text):
   OSTEP = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/operating-system-three-easy-pieces
   - OSTEP/book-md/ (chapters as md), OSTEP/notes/ (Don's Korean notes)
   - OSTEP/.tools/ostep-homework/ — the official homework simulators (vm-paging/paging-linear-translate.py,
     vm-beyondphys-policy/paging-policy.py, cpu-sched/scheduler.py, cpu-sched-mlfq/mlfq.py, ...).
     Run them READ-ONLY with `python3 -I <path> ...` (copy into /tmp/<ID>_work/ if you need to). Never edit them.

## Lanes (11 lessons written IN PARALLEL right now — stay in yours, cross-reference by id)
- OS-B01 mini shell 1: tokenizer (whitespace, simple quotes optional — state what you support), fork→execvp→
  waitpid, status classification (WIFEXITED/WEXITSTATUS vs WIFSIGNALED/WTERMSIG, shell convention 128+sig),
  command-not-found (execvp fails in child → `_exit(127)`), background `&` + SIGCHLD reaping with
  `waitpid(-1, &st, WNOHANG)` loop; zombie (`ps -o pid,stat,comm` shows Z) and orphan (re-parented to launchd/
  pid 1 on macOS — verify) demo with REAL ps output. Tests run real /bin/true, /bin/false, `sh -c 'kill -9 $$'`,
  a nonexistent command. No pipes/redirection (OS-B02). Signal-handler safety details → OS-B07.
- OS-B02 mini shell 2: self-contained (re-include a compact tokenizer); `a | b | c` N-stage pipeline, `<`, `>`,
  `2>&1` via dup2; fd inheritance diagram (fd table per process → open file table → vnode); the classic hang:
  parent/child keeps a write end open → reader never sees EOF — reproduce it in a demo with a timeout
  (alarm or waitpid polling + kill) so nothing hangs forever, then fix. Tests compare pipeline output captured
  through a pipe to expected strings (e.g. `printf 'b\na\n' | sort | head -1`), check no fd leaks (count open
  fds before/after via fcntl(F_GETFD) scan). Starter must FAIL, not hang (use a watchdog).
- OS-B03 syscall cost: empty function call (noinline + volatile/asm barrier so it isn't deleted), getpid,
  read(fd, buf, 0), clock_gettime (macOS: commpage-based, no syscall — verify, e.g. with `dtruss` impossible
  under SIP, so reason from timing + `otool -tV`/disasm of libsystem only if you actually saw it), plus one
  definitely-trapping call for contrast (e.g. getppid or a 1-byte write to /dev/null). Measure ns/call with
  mach_absolute_time or clock_gettime_nsec_np, warm-up, median of ≥5 runs, loop-overhead subtraction.
  Deterministic CHECKs go on the stats helpers (median/percentile, ns-per-call math, overhead subtraction)
  with fixed inputs; timing lives in demo_bench.c. Linux part: in the Lima VM `elinux` run the same bench
  + `strace -c` (and note vDSO clock_gettime there). Context-switch cost is OS-M02's lane.
- OS-B07 signals: sigaction (sa_mask, SA_RESTART, SA_RESTART vs EINTR shown for a blocking read),
  SIGINT/SIGTERM/SIGCHLD, async-signal-safe list (POSIX 2.4.3), why printf/malloc in a handler can deadlock
  (lock held by interrupted code) — show it deterministically or explain honestly if it doesn't reproduce;
  `volatile sig_atomic_t` flag; self-pipe trick (non-blocking pipe, handler writes 1 byte saving errno, main
  loop poll()s). Tests send signals to self (raise/kill) and check delivery/coalescing (standard signals don't
  queue — show 2 SIGUSR1 while blocked → 1 delivery). kqueue EVFILT_SIGNAL is OS-A11's; signalfd is OS-M18's —
  mention only. Firmware tie-in: handler = ISR rules (C-M14).
- OS-B08 address space: print addresses of text/data/bss/rodata/heap(small + large malloc)/stack/mmap/
  shared-lib function (e.g. &printf) and thread stack; run several times → table of which change.
  macOS facts to VERIFY, not assume: main binary PIE slide per run; dyld shared cache slide is per BOOT (same
  across processes until reboot); `vmmap <pid>` output (run on a child that sleeps) — paste real trimmed
  regions; `otool -l`/`size -m` segments (__TEXT/__DATA/__LINKEDIT, __PAGEZERO 4 GB). Linux: /proc/self/maps
  in Lima VM + `setarch -R` to disable ASLR. Deterministic CHECKs: ordering facts that always hold (e.g. a
  parser for "start-end perms name" lines of a fixed /proc/maps sample, region classification of an address,
  page-rounding helpers), not raw addresses. Firmware tie-in: Cortex-M has no MMU/ASLR — linker script layout
  (cross-ref TOOL/BM items by id if they exist; check JSON).
- OS-B09 mmap: MAP_SHARED vs MAP_PRIVATE × (write visible in file? visible to other mapping? after fork visible
  to parent/child?) → table produced by REAL test code (CHECKs), anonymous MAP_SHARED between parent/child,
  msync, munmap, SIGBUS when mapping beyond EOF (catch it in a child process, show real signal). mmap file copy
  vs read/write copy speed (demo_bench.c, file size e.g. 64–256 MB in /tmp/<ID>_work, cold vs warm cache caveat;
  `purge` needs sudo — don't run it, say so). COW internals beyond observation → OS-M03.
- OS-B10 linear page table simulator: params addr-space size, page size, phys mem size; VPN/offset split,
  PTE with valid/R/W/X/dirty/accessed, translate(va, access type) → PA or fault kind (invalid, protection),
  set accessed/dirty on success. Cross-check against OSTEP vm-paging/paging-linear-translate.py with several
  seeds (`-s N -c`): your parser reads its output (or you transcribe its PTE dump) and you show that your
  translations match its answers — paste real output. Multi-level tables/TLB are OS-M01's lane.
- OS-B11 page replacement: FIFO, LRU, Clock (one use-bit), OPT; hit/miss counts for a reference string and
  frame counts 1..N; Belady's anomaly with FIFO on 1,2,3,4,1,2,5,1,2,3,4,5 (3 vs 4 frames — verify the
  counts yourself, don't quote from memory); LRU/OPT are stack algorithms (no anomaly) — CHECK the inclusion
  property on random strings. Cross-check against OSTEP vm-beyondphys-policy/paging-policy.py (`-p FIFO|LRU|
  OPT|CLOCK -c`) for several seeds — paste real output. Hit-rate curve svg from REAL numbers (e.g. 80-20
  workload, looping workload where LRU is worst).
- OS-B12 CPU scheduling: FCFS, SJF, STCF, RR (quantum), MLFQ (OSTEP rules 1–5: priority, RR within level,
  new job top, allotment demotion, periodic boost); jobs with arrival, length, optional I/O pattern; metrics
  turnaround/response/waiting avg. Cross-check FCFS/SJF/RR vs OSTEP cpu-sched/scheduler.py (`-c`) and MLFQ vs
  cpu-sched-mlfq/mlfq.py for at least one config (match their tie-breaking/IO semantics or explain exactly
  where yours differs). Gantt-chart svg. Real-time scheduling → see RTOS (mention only).
- OS-B13 atomic save: write tmp → fsync(tmp) → rename → fsync(dir); a crash-injection harness: fork a child
  that runs the save with a hook that does `_exit`/`kill(getpid(),SIGKILL)` after step k, parent verifies the
  target is exactly old or exactly new (never partial, never missing) for every k. Also show the broken
  variants (truncate+write in place; rename without fsync) and what the harness can/can't catch — be honest
  that killing a process does NOT simulate power loss (page cache survives); explain what does (dm-flakey /
  VM power-off / hardware). macOS: fsync vs fcntl(F_FULLFSYNC) (cite Apple man page), F_BARRIERFSYNC if it
  exists in the SDK headers (check). Measure fsync vs F_FULLFSYNC latency in demo_bench.c (real numbers,
  APFS on this Mac's SSD). Tie to Don's SSD FW (FUA, flush cache, PLP). Journaling → OS-A07.
- OS-B14 IPC comparison: same ping-pong (1 B .. 64 KB) and bulk (e.g. 256 MB) transfer over pipe, named FIFO
  (mkfifo in /tmp/<ID>_work), AF_UNIX SOCK_STREAM socketpair, shm_open+mmap ring + named semaphores
  (sem_open — sem_init is unsupported on macOS). Correctness CHECKs: every message intact & in order
  (checksums), clean-up of names. Benchmarks in demo_bench.c: RTT min/median/p99 and MB/s table + svg from
  REAL numbers; selection guide table. Context-switch math is OS-M02's; SCM_RIGHTS → OS-M16; robust shm →
  OS-A12; mq → OS-M15 (mention only).

## Overrides / extras for this set
- Output: ROOT/lessons/OS/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
  (+ demo_*.c). Front matter `updated: 2026-11-11`; `time:` realistic for Basic (50–70분).
  Title line `# <ID> <title exactly as in OS.json>`. md ~450–600 lines.
- Language: C11. Put `#define _POSIX_C_SOURCE 200809L` (or `#define _DARWIN_C_SOURCE` when you need Darwin APIs
  like mach_absolute_time, clock_gettime_nsec_np, MAP_ANON, F_FULLFSYNC) before includes. Add `-pthread` only
  if you use threads.
- Process-based tests must be deterministic and must not hang or leak: every fork is waited for; any test that
  could block (pipes, signals, FIFOs) has a watchdog (alarm() in the child, or parent polling waitpid with
  WNOHANG + kill after a timeout) so a wrong starter FAILs instead of hanging. No stray files left in /tmp
  after a run (unlink what you create; use /tmp/<ID>_work/ or mkstemp names with your ID).
- Timing numbers never go in CHECKs. Benchmarks live in demo_bench.c (or demo_*.c); md shows the REAL numbers
  with machine info (Apple M6; `sysctl -n machdep.cpu.brand_string`, `cc --version` first line), warm-up +
  median of several runs, compiler kept from deleting work. Ratios matter, not absolute ns — say so.
- Simulators (B10/B11/B12): pure C, deterministic; tests = hand-computed small cases + brute-force/reference
  checks + fixed-seed xorshift32 random cases; the OSTEP-simulator cross-check is shown in the md with the real
  command lines and output (you may also add a demo script, e.g. demo_ostep_check.sh, that runs both and
  diffs — it must work from the lesson dir with `python3 -I`).
- Linux (optional for B03/B08, only where the lane says): Lima VM `elinux` (aarch64 Ubuntu 26.04). Check
  `limactl list` first; it may be running (used by other sessions — never stop it if you didn't start it).
  Home mount is READ-ONLY in the VM → copy sources to VM /tmp/<ID>/ and build there with `gcc -std=c11 -Wall
  -Wextra -Werror -O2`. If the VM isn't usable, say so and skip — don't fake output.
- Firmware tie-in (1. 문제 first paragraph + Part C): where this shows up in embedded Linux / RTOS / MCU work
  (init system & watchdog daemons, sensor pipelines via pipes, config storage on flash, MMU-less Cortex-M,
  RTOS task scheduling) — cross-ref RTOS/ELX/BM ids from the JSON only if they exist (check).
- Accuracy: cite POSIX (pubs.opengroup.org pages), OSTEP chapter numbers, man pages (macOS `man 2 fsync` etc.).
  macOS ≠ Linux in many details here — state which OS each claim is about, and VERIFY on this Mac what you
  claim about macOS. Don't claim an asm instruction or a kernel behaviour you didn't observe or cite.

## Verify (you MUST run; use your own temp names like /tmp/<ID>_st)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # some FAIL, exit 1, no hang
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/<ID>_so && /tmp/<ID>_so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/<ID>_as && /tmp/<ID>_as  # clean (and practice.c)
  Run solution 5+ times (not flaky). Run from the lesson dir AND from another cwd (the checker runs binaries
  with cwd = lesson dir; if a file needs data next to it, say so in the md). solution/practice each < 3 s.
  Every demo_*.c must compile with the strict flags too.
- solution.c = starter.c with only TODO bodies filled (diff highlights changed lines). Starter TODO stubs
  compile warning-free ((void) params) and return wrong-but-safe values (no crash, no UB, no hang).
- Self-check md: headings verbatim & in order, ≥2 ```svg figures (viewBox, width ≤ 760, currentColor +
  f-acc/f-ok/f-warn/f-mut only, escape &lt; &gt; &amp;), Q&A ≥5 as `<details><summary>Q1. ...`, 추가 연습 3–6
  with `### 한 번에 검증` + `<!-- code: practice.c -->`, `<!-- code: starter.c -->` in §3,
  `<!-- code: solution.c diff=starter.c -->` in §6, demo files inlined with `<!-- code: demo_x.c -->` where useful.
- Use a PRIVATE scratch dir /tmp/<ID>_work/ — 10 other writers run in parallel.

## When done
Reply (short): files written, md line count, PASS counts starter/solution/practice, ASan/UBSan result, demo files
+ what they showed (key numbers), OSTEP cross-check result (B10–B12), anything from memory you couldn't verify.
Do NOT edit files outside ROOT/lessons/OS/<your ID>/ (don't run build_site.py — the orchestrator does).
