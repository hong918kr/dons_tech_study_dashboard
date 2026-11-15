# Lesson writer brief — track OS (운영체제), **Medium** level (OS-M01, M03–M06, M09, M12–M18)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math, read OSTEP
(Korean notes exist), prepping for embedded/firmware/embedded-Linux/silicon-validation interviews.
He re-checks fundamentals by DOING: every lesson = one interview-style coding problem with tests.

Already written (assume known, cross-reference by id, don't re-teach): **OS-B01–B14 all** (B01/B02 mini shell,
B03 syscall cost, B04 threads, B05 mutex, B06 condvar, B07 signals + self-pipe, B08 file I/O + fsync, B09 mmap,
B10 paging translate, B11 page replacement, B12 schedulers, B13 /proc + resource limits, B14 IPC compare harness),
OS-M02 (ctx switch ping-pong), OS-M07 (TAS/TTAS/ticket), OS-M08 (atomics patterns), OS-M10 (dining philosophers),
OS-M11 (mini lockdep), OS-A01 (futex), OS-A03 (EBR/RCU), OS-A05, OS-A11 (kqueue daemon). Also ARCH-A01 (page-table
walker) / ARCH-A02 (TLB + ASID), DS-M03 (slab), C/DS/CPP/ALGO/ARCH/RTOS lessons. Check `ls ROOT/lessons/<TRACK>/`
before citing any id; cite only ids that exist in ROOT/curriculum/tracks/*.json.

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY (front matter, headings verbatim and in order, svg rules,
   code rules, `<!-- code: ... -->` markers, `<details>` folding).
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base) → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md (Medium overrides)
   → ROOT/curriculum/OS_B_LESSON_WRITER_BRIEF.md (OS Basic: process-test watchdogs, no stray /tmp files, timing
   only in demos, Lima VM usage, OSTEP paths). Everything there applies unless overridden below.
3. ROOT/curriculum/tracks/OS.json — your item. The lesson must do the item's `task` completely (every sub-feature
   and every measurement it names). Skim all OS-M/A items so you stay in your lane.
4. Reference lessons: ROOT/lessons/OS/OS-M10/ and ROOT/lessons/OS/OS-M11/ (same track, Medium, pthread + demo_bench),
   ROOT/lessons/OS/OS-B14/ (process/IPC harness), ROOT/lessons/OS/OS-A01/ (a `/* ENV: linux */` lesson built in
   the Lima VM — copy its header/verify conventions if your item is linux).
5. OSTEP on disk (cite chapter numbers, don't copy text):
   /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/operating-system-three-easy-pieces
   (book-md/, notes/, .tools/ostep-homework/ — run simulators READ-ONLY with `python3 -I`, copy to your private
   dir if needed; e.g. vm-freespace/malloc.py for M04/M05, file-implementation/vsfs.py for M14,
   threads-sema/ (rwlock) for M09).

## Lanes (13 lessons written IN PARALLEL right now — stay in yours; every lesson self-contained, no cross-lesson #include)
- **OS-M01 page-table manager** (dup review B10: ARCH-A01 owns the walker/format; you own the OS management ops).
  Embed a compact copy of ARCH-A01's model ("ARCH-A01 모델을 축소해 넣었다" — read its solution.c and keep its
  descriptor layout: bit0 V, bit1 T, bit2 R, bit3 W, bit4 X, bit5 U, bit7 G, bits 12..47 OA; 4-level, 4 KB pages,
  9 bits/level; simulated phys memory + frame allocator). Implement `map_range / unmap_range / protect_range`
  (4 KB pages only is fine — say blocks are ARCH-A01's), allocate intermediate tables on demand, **reclaim empty
  table pages** bottom-up after unmap (per-table live-entry count or scan — choose, document, test frame count
  returns to baseline), and a **mock TLB** that really caches translations (so a missing invalidate produces a
  STALE translation that a test catches: TLB result ≠ fresh walk). Record every invalidate call
  (`tlbi_va(asid, va)` vs `tlbi_asid(asid)` vs `tlbi_all`), with a policy: unmap/protect of ≤ N pages → per-VA,
  larger → whole ASID (Linux `tlb_single_page_flush_ceiling` = 33 on x86 — hedge/cite), and the rule "permission
  INCREASE needs no flush on most archs (spurious fault fixes it) vs DECREASE/unmap must flush BEFORE the frame
  is reused" (free-after-flush ordering = mmu_gather idea; test that a frame isn't returned to the allocator
  before its invalidate is recorded). Freed table pages also need a walk-cache invalidate — mention. Part C:
  munmap/mprotect in an RTOS-with-MMU / Linux driver context, ARM break-before-make (cite ARM ARM D8 hedged),
  TLB shootdown IPIs on SMP (describe; host model is single-core).
- **OS-M03 COW fork + page faults** (mac, real processes): parent mallocs/mmaps a big buffer, touches it,
  forks; child writes k of N pages; measure child `getrusage(RUSAGE_SELF).ru_minflt` before/after write loop
  (hw.pagesize = 16384 on this Mac! use sysconf(_SC_PAGESIZE) everywhere), show delta ≈ k (linear fit over several
  k). Checked tests use generous bounds (delta ≥ k and ≤ k + slack; parent's data unchanged after child writes —
  COW isolation; MAP_SHARED region IS visible to parent — contrast) — never exact fault counts for non-touched
  paths. Also show read-only touches in child cause ~0 faults? (verify — macOS may differ; report what you see).
  vfork/posix_spawn: measure fork vs posix_spawn latency with a large (e.g. 1 GB touched) parent in demo_bench.c
  (real numbers), explain why (page-table copy, not data copy). Linux comparison in Lima VM (`/proc/self/stat`
  minflt or getrusage) optional — say if done. Firmware tie-in: embedded Linux daemons forking from a big
  process, MMU-less (uClinux) has no fork → vfork only.
- **OS-M04 malloc 1** — SHARED ALLOCATOR RULES below. Implicit list first-fit as the baseline (Part A small),
  then explicit doubly-linked free list with boundary tags (header+footer), immediate coalescing of all 4 cases,
  split with a minimum block size, first-fit vs next-fit vs best-fit selectable, realloc (in-place grow into
  next free block, shrink-with-split, else move). Trace-driven tests + a heap checker (`mm_check`: every free
  block in list is marked free, no two adjacent free blocks, header==footer, list pointers in heap, counts match).
  Measure utilization (peak payload / heap high-water) and throughput (ops/s, demo_bench.c only) per fit policy
  on the shared traces. OSTEP ch.17 + vm-freespace/malloc.py cross-check of first/best/worst-fit on a small case
  (show command + output) is a nice-to-have.
- **OS-M05 malloc 2** — SHARED ALLOCATOR RULES. Self-contained: embed a compact M04 explicit-list allocator as the
  baseline ("OS-M04 할당기 축약판"). Add segregated free lists (size classes: document — e.g. power-of-two
  classes with exact small classes 32..128), first-fit within class ≈ best-fit; compare with M04 baseline on the
  SAME traces: utilization and throughput table. Instrumentation: internal fragmentation (sum(block_size −
  requested) over live blocks, incl. header/padding) and external fragmentation (e.g. 1 − largest_free / total_free,
  and "request that fails despite enough total free" counter) — define precisely and test on hand-built heaps
  with known answers. Heap checker extended to per-class invariants (block in the right class list).
- **OS-M06 thread caching allocator** — SHARED ALLOCATOR RULES. Self-contained: embed compact M05-style
  segregated allocator as the per-arena backend ("OS-M05 축약판"). Build: global-lock version (baseline), N arenas
  each with its own mutex (thread → arena by hash/round-robin), per-thread tcache (`_Thread_local` bins for small
  classes, bounded count per bin like glibc tcache 7, flush half to arena when full), and **remote free** (block
  freed by a thread whose arena ≠ owner: owner arena id stored in block header; push onto owner's lock-free
  remote-free stack (CAS / atomic exchange) that the owner drains on its next malloc — mimalloc "thread-delayed free"
  idea; or locked queue — choose and justify). Tests: producer/consumer threads (alloc in A, free in B) with
  pattern fill + verify, no block handed out twice (ownership bitmap), memory returns to arenas after drain;
  TSan clean. demo_bench.c: 1/2/4/8 threads × {global lock, arenas, arenas+tcache} ops/s with REAL numbers and
  `uptime` load. Slab/buddy are DS-M03/DS-M04 (cite only if they exist). Name jemalloc/mimalloc/glibc ptmalloc
  concepts carefully (hedge anything not verified).
- **OS-M09 rwlock**: mutex+condvar reader-preferring and writer-preferring (waiting-writers count blocks new
  readers), optionally fair/phase-fair (alternate). Deterministic CHECKs via forced schedules (e.g. a test
  harness with barriers/semaphores-by-condvar that keeps readers overlapping continuously and asserts the writer
  is NOT admitted under reader-pref within K reader handoffs but IS admitted next under writer-pref), mutual
  exclusion invariants (readers>0 ⇒ !writer) checked inside critical sections, TSan clean. demo_bench.c: 95/5
  workload, threads = 8, throughput + writer max/p99 wait for each policy and pthread_rwlock_t (macOS) — REAL
  numbers. Note macOS pthread_rwlock R→W self-deadlock (known from conc layer 6) and glibc
  PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP (Linux, hedge). Firmware: config tables read by many tasks.
- **OS-M12 I/O models (mac)** — SHARED SERVER RULES. Three echo servers in one program: thread-per-connection
  (blocking), select (non-blocking, FD_SETSIZE = 1024 limit — show what happens/guard), kqueue (EVFILT_READ,
  EV_CLEAR? state choice). Checked tests: in-process client threads (or fork) with ~64 concurrent connections,
  each sends varied-size messages incl. > socket buffer (partial write handling: EVFILT_WRITE / pending output
  buffer), echo verified byte-exact; all fds closed; servers on 127.0.0.1 port 0 (getsockname) — never fixed
  ports. demo_load.c: 1000 concurrent connections (raise RLIMIT_NOFILE; macOS kern.maxfilesperproc — report),
  throughput, p50/p99 RTT, RSS (task_info / getrusage ru_maxrss — macOS reports BYTES) per model, REAL numbers.
  epoll is OS-M13's (mention only); kqueue daemon architecture is OS-A11's.
- **OS-M13 epoll LT vs ET (linux)** — SHARED SERVER RULES, `/* ENV: linux */`. Self-contained epoll echo server
  ("OS-M12 kqueue 서버를 epoll로 포팅" — write it compactly yourself). Modes: LT; ET with drain-until-EAGAIN;
  BUGGY ET (single read per event) reproduced deterministically (client sends 2 chunks larger than the read size,
  waits; server stalls → test detects missing echo within timeout and reports, no hang); EPOLLONESHOT multi-thread
  worker pool (re-arm with EPOLL_CTL_MOD) + test that without ONESHOT two workers can get the same fd (demo with
  counters; deterministic check on ONESHOT version only). Write-side: EPOLLOUT only while output pending.
  Mention EPOLLEXCLUSIVE / thundering herd (hedge). Build/run in Lima VM.
- **OS-M14 VSFS** (mac): disk image = regular file (mkstemp in /tmp/OS-M14_work/, unlinked at exit) accessed only
  via `bread(blk)/bwrite(blk)` (4 KB blocks, count reads/writes). Layout like OSTEP ch.40: superblock, inode
  bitmap, data bitmap, inode table (e.g. 256 B inodes, 12 direct + 1 single-indirect), data. mkfs, path lookup
  (namei, multi-level dirs), create/mkdir, write (allocate blocks, grow into indirect), read, ls, unlink
  (free inode+blocks, link count), and `fsck`: recompute bitmaps from inodes/dirs, detect + optionally repair
  injected corruptions (leaked block, double-allocated block, inode with bad block pointer, dir entry → free inode,
  wrong link count). Tests check exact block-I/O counts for small ops (e.g. reading /a/b/c costs N reads — table
  like OSTEP Fig 40.3) and fsck findings for each injected fault. OSTEP vsfs.py cross-check optional. Journaling
  is out of scope (mention STOR/OSTEP ch.42). Firmware tie-in: littlefs/FatFS on SPI flash, NAND FTL below FS
  (Don's SSD past).
- **OS-M15 POSIX mq (linux)**, `/* ENV: linux */`: mq_open/send/receive (link `-lrt` if needed — check glibc
  2.43: likely in libc), producers 2 → consumer 1 (fork), priority ordering (equal prio FIFO), full queue blocking
  vs O_NONBLOCK EAGAIN, mq_timedreceive ETIMEDOUT (CLOCK_REALTIME absolute!), mq_notify SIGEV_SIGNAL and
  SIGEV_THREAD (one-shot registration, only on empty→non-empty, re-register; EBUSY if another process registered),
  mq_getattr/setattr, /proc/sys/fs/mqueue/{msg_max,msgsize_max,queues_max} real values + EINVAL when attr
  exceeds; mq_unlink cleanup (unique name with pid). Latency vs pipe vs Unix socket in demo_bench.c with an
  OS-B14-style compact harness — REAL VM numbers. Contrast with RTOS-B11 queue (copy semantics, priority).
- **OS-M16 Unix socket deep (mac)**: SCM_RIGHTS fd passing (socketpair + fork, and a named socket in
  /tmp/OS-M16_work/ unlinked), receiver reads the passed file / passes a pipe end; multiple fds in one cmsg;
  CMSG_SPACE vs CMSG_LEN and alignment — reproduce the classic bug (buffer sized with CMSG_LEN or misaligned char
  array → MSG_CTRUNC / wrong fd) deterministically; MSG_CTRUNC check; fd leak when receiver ignores cmsg (fd
  still installed — show fd count); peer credentials: getpeereid, LOCAL_PEERCRED (struct xucred),
  LOCAL_PEERPID (macOS — verify), access-control function `allow_peer(uid)` tested; SOCK_STREAM merges messages /
  SOCK_DGRAM (and SOCK_SEQPACKET if supported on macOS — verify) preserves boundaries — tests. Linux SO_PEERCRED
  + SCM_CREDENTIALS in Lima VM as an optional demo (`/* ENV: linux */` demo file) — show real output if done.
- **OS-M17 process-shared + robust mutex (linux)**, `/* ENV: linux */`: shm_open + ftruncate + mmap a struct
  with pthread_mutex_t + 2 pthread_cond_t (PTHREAD_PROCESS_SHARED attrs) + ring buffer; producer/consumer
  processes, checksum-verified. Kill test: child takes the lock then SIGKILLs itself (or parent kills it while
  it holds); normal mutex → other process blocks forever (detect with pthread_mutex_timedlock ETIMEDOUT, never
  hang); robust mutex → EOWNERDEAD → repair invariants → pthread_mutex_consistent → continue; not calling
  consistent → ENOTRECOVERABLE on next lock (test). Condvar caveat after owner death (hedge). Cleanup:
  shm_unlink with unique pid name. Mention futex robust list (set_robust_list; OS-A01 cite) and that macOS lacks
  robust mutex. OS-A12 (robust shm protocol) is the advanced follow-up — mention only.
- **OS-M18 eventfd/signalfd/pidfd (linux)**, `/* ENV: linux */`: one epoll loop: eventfd (counter semantics: two
  writes before read → read returns the sum; EFD_SEMAPHORE → returns 1 each; EFD_NONBLOCK EAGAIN), worker threads
  signal completion via eventfd; signalfd for SIGCHLD/SIGTERM (must block signals with sigprocmask/pthread_sigmask
  in ALL threads first — show the bug when not blocked); pidfd_open (syscall(SYS_pidfd_open) if no wrapper —
  check glibc) per child → EPOLLIN on exit, waitid(P_PIDFD); SIGCHLD coalescing vs pidfd per-child (deterministic
  test: 5 children exit together → signalfd may give fewer siginfo than 5 but pidfd gives exactly 5). Compare with
  OS-B07 self-pipe: LOC and wake latency (demo_bench.c, REAL VM numbers). Daemon skeleton with clean shutdown.

## SHARED ALLOCATOR RULES (M04, M05, M06 — so the three lessons read as one series)
- Heap = one `mmap(MAP_PRIVATE|MAP_ANON)` region of fixed max size (e.g. 64 MiB); `mem_sbrk(incr)` bumps a break
  inside it (CS:APP memlib idea, cite CS:APP 3e §9.9) and fails cleanly (NULL) past the max. `mem_heap_hi`
  = high-water mark for utilization. No real malloc inside the allocator.
- 16-byte alignment of payloads; word = 8 bytes; header/footer = 8 bytes holding size | alloc bit (bit0)
  (| prev_alloc bit1 optional for footer elision — if used, document). Minimum block = 32 bytes.
- API names: `mm_init(void)`, `mm_malloc(size_t)`, `mm_free(void*)`, `mm_realloc(void*, size_t)`,
  `mm_check(int verbose)` returning number of violations. malloc(0) → NULL; free(NULL) no-op; realloc(NULL,n) =
  malloc; realloc(p,0) = free + NULL (document as our choice; C23 made it UB — hedge).
- Trace format (in-memory array, like CS:APP traces): `{op ∈ 'a','f','r', id, size}`. Each lesson generates the
  SAME 4 deterministic traces with this exact generator so numbers are comparable across lessons:
  `xorshift32` (x ^= x<<13; x ^= x>>17; x ^= x<<5) seeded per trace; T1 "small-uniform" (seed 1: 2000 ops,
  sizes 1..128), T2 "mixed" (seed 2: 2000 ops, 70 % 1..256, 25 % 257..4096, 5 % 4097..65536), T3 "realloc-grow"
  (seed 3: 200 buffers grown by realloc in +1..+512 steps to ≤ 32 KiB, interleaved small allocs), T4
  "binary-pattern" (CS:APP binary-bal style: alloc 64,448,64,448,...; free all 448s; alloc 512s — fragmentation
  stress). Op mix for T1/T2: alloc when live < 50 or with p = 0.6, else free a random live id; free all at end.
  Replay checks payload integrity (fill with id-derived byte pattern on alloc/realloc, verify before free) and
  calls mm_check every K ops in tests.
- Utilization U = peak(live requested bytes) / heap high-water (bytes). Throughput only in demo_bench.c.
- M06 is multi-threaded: its backend arenas each have their own mem_sbrk region (or a shared region with a lock —
  document); tcache/arenas keep the same header format with an owner-arena field (state its bits).

## SHARED SERVER RULES (M12, M13)
- Echo protocol = raw bytes, server echoes exactly what it receives. Listen on 127.0.0.1 port 0, read back port.
- Every server runs in its own thread or child process and is stopped via a wake pipe/eventfd (not by killing
  mid-test); tests join it and assert no fd leaks (count open fds before/after with an fcntl(F_GETFD) scan).
- Clients verify byte-exact echo of messages with sizes {1, 100, 4096, 65536, 300000} (the big one forces partial
  writes → server must buffer pending output and wait for writability). Each test has a watchdog (poll/timeouts)
  so the starter FAILs instead of hanging.
- Load numbers (demo_load.c): N connections each doing M round trips of 64 B; report conn/s setup, total RTT
  p50/p99, msgs/s, server RSS, with machine + load avg.

## Overrides for this set
- Output: ROOT/lessons/OS/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
  (+ demo_*.c). Front matter `updated: 2026-11-11`, `time:` 70–100분. Title line `# <ID> <title exactly as in
  OS.json>`. md **500–700 lines**. Q&A ≥6 (aim 7–8).
- Language C11 (`-std=c11`); feature macros before includes (`_POSIX_C_SOURCE 200809L`, `_DARWIN_C_SOURCE` for
  Darwin APIs, `_GNU_SOURCE` on linux files when needed). If a checked file needs a non-default build line, put
  `/* BUILD: <full extra flags, e.g. -pthread -lrt> */` in its first 3 lines and say so in the md.
- Linux items (M13, M15, M17, M18): every checked/demo file's FIRST line is `/* ENV: linux */`. Lima VM `elinux`
  (aarch64 Ubuntu, gcc + clang) is RUNNING and shared with other sessions — never stop/restart it. Home mount is
  READ-ONLY → `limactl shell elinux -- bash -lc 'mkdir -p /tmp/<ID> && cp <lesson dir>/* /tmp/<ID>/ && cd /tmp/<ID>
  && gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread ...'`. ASan/UBSan with gcc or clang in the VM; TSan only with
  clang there (gcc TSan SIGILLs). Paste real VM output (uname -r, gcc --version line). Mac-side: the md may show a
  macOS contrast (e.g. kqueue / no mq) only if you ran it.
- Mac items: SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk (27.2 SDK link broken). Host = Apple
  M6 (2 Super + 4 Performance + 6 Efficiency cores, hw.pagesize 16384, cache line 128). The machine is SHARED and
  loaded (other writers + sessions) — every benchmark table states `uptime` load average and treats numbers as
  ratios.
- Timing never in CHECKs. Process/socket tests: every fork waited, every blocking op has a watchdog/timeout, no
  stray files/IPC names (unlink shm/mq/sockets; unique names with your ID + pid), no fixed TCP ports.
- Starter: TODO stubs compile warning-free, return wrong-but-safe values; starter run FAILs with exit 1 within a few
  seconds, never hangs or crashes. solution.c = starter.c with only TODO bodies filled (diff highlights).
  solution/practice < 3 s each, run solution 5× (not flaky), from the lesson dir and from another cwd.
- Accuracy: cite POSIX pages (pubs.opengroup.org), Linux man-pages (man7.org, e.g. epoll(7), eventfd(2),
  mq_overview(7), unix(7), pthread_mutexattr_setrobust(3)), macOS `man` pages you actually read on this Mac,
  OSTEP chapters, CS:APP 3e §9.9 for malloc. State which OS each claim is about. Never invent output — paste what
  you ran. List anything from memory that you couldn't verify in your final report.
- PRIVATE scratch: /tmp/<ID>_work/ and /tmp/<ID>_* binaries (VM: /tmp/<ID>/). 12 other writers run in parallel —
  never write helpers into a shared folder or another lesson's dir.

## Verify (you MUST run)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
    cc -std=c11 -Wall -Wextra -Werror -O2 [BUILD flags] starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # FAILs, exit 1, no hang
    cc ... solution.c -o /tmp/<ID>_so && /tmp/<ID>_so    # all PASS, exit 0 (5×)
    cc ... practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr    # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer [BUILD flags] solution.c / practice.c → clean
    cc -std=c11 -g -O1 -fsanitize=thread [BUILD flags] solution.c → clean   (if pthreads are used)
    every demo_*.c builds with the strict flags; run it and paste real output.
  (linux items: same commands inside the VM with gcc, + clang for TSan.)
  Harness prints a final `N/M passed` line.
- md self-check: headings verbatim & ordered (scan outside ``` fences), `### Part A — / B — / C —`, ≥2 ```svg
  (viewBox, width ≤ 760, currentColor + f-acc/f-ok/f-warn/f-mut only, &lt; &gt; &amp; escaped), Q&A ≥6 as
  `<details><summary>Q1. ...`, 3–6 추가 연습 + `### 한 번에 검증` + `<!-- code: practice.c -->`,
  `<!-- code: starter.c -->` in §3, `<!-- code: solution.c diff=starter.c -->` in §6, demo markers where shown.

## When done
Reply (short): files written, md line count, PASS counts starter/solution/practice, ASan/UBSan (+TSan) results,
demo files + key numbers, anything unverified (list). Do NOT edit files outside ROOT/lessons/OS/<your ID>/ (and
your private /tmp paths). Don't run build_site.py, don't git commit.
