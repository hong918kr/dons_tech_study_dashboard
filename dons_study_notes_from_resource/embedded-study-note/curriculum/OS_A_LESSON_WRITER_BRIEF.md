# Lesson writer brief — track OS (운영체제), **Advanced** level (OS-A02, A04, A06–A10, A12–A14)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math, read OSTEP
(Korean notes exist), prepping for embedded/firmware/embedded-Linux/silicon-validation interviews (Apple, NVIDIA,
Rivian, Tesla, Anduril...). OS Advanced = staff-level: "디버거 브레이크포인트는 실제로 뭘 하나?", "CFS는 왜 깨어난
태스크의 vruntime을 보정하나?", "저널이 있는데 왜 fsck가 필요 없나 / 언제 여전히 필요하나?", "SSD 쓰기 증폭을 어떻게
줄이나?", "공유 메모리 상대가 죽은 걸 어떻게 아나?". Follow-ups probe *why this design, what it misses, how you'd
know it's working, what it costs*.

## Read first (required, in order)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY (front matter, headings verbatim and in order, svg rules,
   code rules, `<!-- code: ... -->` markers, `<details>` folding).
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base) → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md (Medium overrides)
   → ROOT/curriculum/OS_B_LESSON_WRITER_BRIEF.md (process-test watchdogs, no stray /tmp files, timing only in demos,
   Lima VM usage, OSTEP paths) → ROOT/curriculum/OS_M_LESSON_WRITER_BRIEF.md (Medium lanes + shared rules) →
   **this file**. Everything applies unless overridden here.
3. ROOT/curriculum/tracks/OS.json — your item. The lesson MUST do the item's `task` completely (every sub-feature
   and every measurement it names). Skim all OS items so you stay in your lane.
4. Reference lessons (style + depth): ROOT/lessons/OS/OS-A01/ (linux, Lima VM), OS-A03, OS-A05, OS-A11 (Advanced,
   same track), ROOT/lessons/ELX/ELX-M04/ (`check.sh <starter|solution|practice>` kernel-lab pattern — A09/A10 copy
   this idea). Skim the md of every lesson you cite so the reference is accurate and you don't re-teach it.
5. OSTEP on disk (cite chapter numbers, don't copy text):
   /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/operating-system-three-easy-pieces
   (book-md/, notes/, .tools/ostep-homework/ — run simulators READ-ONLY with `python3 -I`; e.g. file-journaling/,
   file-lfs/ (lfs.py) for A07/A08 cross-checks if useful).

## Already written (cite by id, don't re-teach)
OS-B01–B14, OS-M01–M18 (all), OS-A01 (futex), OS-A03 (EBR/RCU), OS-A05 (Chase-Lev), OS-A11 (kqueue daemon).
Key owners: B07 signals + self-pipe · B08 file I/O + fsync · B09 mmap · B10 paging translate · B11 page replacement ·
B12 schedulers (FIFO/SJF/RR/MLFQ) · B14 IPC compare harness · M01 page-table manager + mock TLB · M03 COW fork with
real processes (ru_minflt) · M12 kqueue/select echo · M14 VSFS + fsck · M16 Unix socket SCM_RIGHTS/peer cred ·
M17 robust pshared mutex (linux). Also ARCH-A01/A02 (page walker, TLB), DS-A01 (shm ring), RTOS-M12 (EDF),
ELX-M04–M06 (kernel modules). Check `ls ROOT/lessons/<TRACK>/` before citing any id; cite only ids that exist in
ROOT/curriculum/tracks/*.json; items not yet written (e.g. DIST-B06, BMC-M04, ROS-A05, most STOR) → "다음 단계" only.

## Shared resources (READ-ONLY — never modify, never write into)
- Lima VM `elinux` (aarch64 Ubuntu 26.04, 6 CPU, gcc + clang + gdb-multiarch + riscv64-linux-gnu-gcc +
  qemu-system-riscv64 + python3.14) is RUNNING and shared — never stop/restart it, never install packages, never
  insmod. Home mount is READ-ONLY inside the VM → work in the VM's /tmp/<ID>_work/.
- Linux 6.18.55 source: VM `/var/lab/src/linux-6.18.55/` — quote file:line for Linux claims (sched, ptrace,
  arch/arm64). Never write under /var/lab.
- xv6-riscv clone: VM `~/.esn_scratch/xv6-riscv` at commit fa2ddcb7ca87a77af579380a6eafd5aff163b44f (2026-11-05,
  upstream main). Copy it; never edit it in place. Verified: `make kernel/kernel fs.img` builds with the VM's
  riscv64-linux-gnu- toolchain; `test-xv6.py` exists (study it for the boot/drive pattern).

## Lanes (10 lessons written IN PARALLEL right now — stay in yours; every lesson self-contained, no cross-lesson #include)

- **OS-A02 ptrace 미니 디버거** (linux, `/* ENV: linux */` first line on every .c). The VM is **aarch64** → software
  breakpoint = `BRK #0` (0xd4200000, 4 bytes), registers via `PTRACE_GETREGSET` + `NT_PRSTATUS` (`struct
  user_pt_regs`; arm64 has no PTRACE_GETREGS), PEEKTEXT/POKETEXT are 8-byte words (patch 4 bytes inside a word —
  read-modify-write). Verify from source/experiment what PC is after the BRK trap on arm64 (x86 int3 leaves PC =
  addr+1 → rewind; arm64?) — state what you observed. Self-contained design: the debugger forks; child does
  `ptrace(PTRACE_TRACEME)`, `raise(SIGSTOP)`, then runs target functions compiled into the SAME binary (fork keeps
  the layout, so `&target_fn` is valid in the child; mark targets `noinline`, use `__attribute__((used))` volatile
  globals). Features: set/clear breakpoint (save orig word, restore), continue, hit counter in a loop (assert hit
  count == loop iterations), step-over-breakpoint (restore orig → SINGLESTEP → re-insert), register dump (x0..x30,
  sp, pc, pstate) + read a variable with PEEKDATA, single-step instruction count of a small known function (assert
  equals count measured by disassembly — compute expected from `objdump -d` once and justify, or assert ≥/range if
  compiler-dependent; say which), detach/kill cleanup, waitpid status decoding (WIFSTOPPED/SIGTRAP, si_code
  TRAP_BRKPT vs TRAP_TRACE via PTRACE_GETSIGINFO). exec path: optional demo debugging a separately built
  program after execve (PTRACE_O_EXITKILL, find load base via /proc/<pid>/maps for PIE). Watchdogs: alarm()/timed
  waitpid loop so starter FAILs not hangs; child always killed. Mention hardware breakpoints/watchpoints
  (NT_ARM_HW_BREAK — demo if it works in the VM, else hedge), Yama ptrace_scope (read the VM's real value),
  macOS (PT_DENY_ATTACH, no PEEK/POKE, lldb uses Mach task ports / exception ports — hedge), gdb remote serial
  protocol + OpenOCD/J-Link on MCUs (FPB breakpoint unit on Cortex-M, BKPT instr) as firmware tie-in. Build/run
  in the VM (gcc + clang ASan/UBSan; TSan not needed if single-threaded).
- **OS-A04 CFS 시뮬레이터** (mac): discrete-time simulator (µs ticks): `sched_prio_to_weight[40]` copied from
  Linux 6.18.55 (quote file:line), `vruntime += delta * NICE_0_LOAD / weight` (fixed-point like the kernel's
  `__calc_delta` inverse-weight table optional), pick = min vruntime (min-heap or RB tree — your choice; test the
  structure on its own), `sched_latency` / `min_granularity` period computation (CFS-era defaults: 6 ms / 0.75 ms ×
  (1+log2 ncpu) scaling — read what 6.18 actually has: EEVDF replaced CFS pick in 6.6, `sysctl_sched_base_slice`;
  report honestly what the source says), min_vruntime tracking, sleeper placement on wakeup (place_entity — CFS-era
  `max(se->vruntime, min_vruntime − latency/2)` vs no placement; show starvation/burst without it). Tests
  (deterministic): CPU share for nice {0,0} = 50/50, {0,5} ≈ 1024:335 within tolerance, {−5,0,5}; a sleeper
  waking after a long sleep does not monopolize CPU longer than a bound with placement but does without
  (assert both); fairness lag bound for always-runnable tasks; heap/RB invariants. Part C / 추가 연습: EEVDF
  concept — lag, eligibility (vruntime ≤ avg_vruntime), virtual deadline = vruntime + slice/weight, pick earliest
  eligible deadline; implement a mini EEVDF pick in practice.c and compare latency for a short-slice task (cite
  Stoica & Abdel-Wahab 1995 EEVDF tech report — hedge exact title; quote 6.18 fair.c pick_eevdf). Firmware tie-in:
  embedded Linux tasks with nice/SCHED_FIFO vs CFS, RT throttling (sched_rt_runtime_us — read real VM value).
  Cite OS-B12 (MLFQ) and RTOS-M12 (EDF) for contrast.
- **OS-A06 mprotect + SIGSEGV 사용자 공간 페이지 폴트** (mac; hw.pagesize 16384 — use sysconf). Region reserved
  with PROT_NONE; SA_SIGINFO handler for SIGSEGV **and SIGBUS** (macOS: write to PROT_READ page = SIGBUS si_code 1,
  verify for PROT_NONE too and report), page-align si_addr, check it's inside the managed region (else restore
  default action + re-raise — test with a deliberate out-of-region fault in a forked child: dies with the original
  signal), mprotect to open the page. (1) Lazy allocation: first touch → map READ (count read faults), first write
  → upgrade RW (count write faults) → assert fault counts == pages touched exactly. (2) Dirty tracking + incremental
  checkpoint: checkpoint() re-protects RW pages to READ and saves only dirty pages (copy to a snapshot store),
  restore(epoch) reconstructs; random write workload (seeded) → after each epoch, saved pages == pages written
  (exact set compare), restore round-trips byte-exact. Read vs write discrimination on arm64 macOS: try
  `uc->uc_mcontext->__es.__esr` WnR bit (ESR_ELx bit 6) and `__far` — if it works, use it + cross-check against the
  READ-then-upgrade scheme; report. Discuss mprotect not being on the POSIX async-signal-safe list (but used by
  GCs/JITs/libsigsegv in practice — hedge), sigaltstack for stack-overflow detection, cost per fault (demo_bench.c:
  faults/s, µs/fault vs plain memcpy snapshot — REAL numbers + uptime). Linux alternatives (userfaultfd,
  soft-dirty `/proc/pid/clear_refs` = 4, `PAGEMAP_SCAN`) — optional VM demo `/* ENV: linux */` or hedge. Firmware:
  Cortex-M MPU + MemManage fault (MMFAR) as the same idea; QEMU/CRIU dirty logging. Distinct from OS-M03 (kernel
  COW) — cite.
- **OS-A07 저널링 + 크래시 주입** (mac): self-contained compact VSFS ("OS-M14 VSFS 축약판" — read
  ROOT/lessons/OS/OS-M14/solution.c and keep its layout/fsck vocabulary, shrink it). Disk = in-memory block array
  behind `bwrite/bread` (no real file needed; count I/O). Journal region: TxB(tid, block list) → logged blocks →
  TxE(tid, checksum) commit; checkpoint to home locations; free journal (superblock journal head/tail). Modes:
  (a) data journaling, (b) ordered metadata journaling (data blocks written to home BEFORE TxB/commit), (c)
  unordered metadata journaling (to SHOW the garbage-data bug: fsck passes but file contains stale/other data).
  Crash injection: `crash_after(N)` makes bwrite #N and later writes vanish (power cut); EXHAUSTIVE over all N for
  several workloads (create+write, append growing into indirect, unlink, rename/mkdir if you have them) → mount →
  replay → fsck must report 0 errors for every N in modes a/b, and file content must be old-or-new (atomic) in
  mode a; mode c shows content violations at specific N (print them). Also: commit-before-data reordering
  (disk write cache) — model a reorder window to show why a barrier/FLUSH (or ext4 journal checksum) is needed
  (TxE checksum detects torn transaction → replay skips it). Revoke records / replaying a freed+reused block
  = "다음 단계" or 추가 연습. Measure write amplification per mode (blocks written / user blocks) — table.
  OSTEP ch.42 (cite §), ext3/ext4 JBD2 data=journal/ordered/writeback names (hedge details), littlefs (COW, no
  journal), and Don's SSD past: FTL L2P table journaling + power-loss tests. Distinct from A08 (LFS) and M14.
- **OS-A08 LFS + FTL 세그먼트 클리닝** (mac): log-structured store: N segments × S blocks, logical block → (seg,
  off) map (imap/L2P), out-of-place write appends to the current segment, segment summary (owner LBA per slot) used
  by the cleaner to test liveness. Cleaner triggered at free-segment watermark; policies greedy (min live ratio) and
  cost-benefit (Rosenblum & Ousterhout 1992: (1−u)·age/(1+u), age = now − seg last-mod time); hot/cold separation
  of cleaner output optional (study effect). Workloads (seeded xorshift): uniform random overwrite, hot-cold
  (e.g. 90 % writes to 10 % LBAs), sequential. Over-provisioning / utilization sweep (u = 50..90 %). Metrics: write
  amplification WA = (host + cleaner-copied blocks)/host blocks, cleaner segments/GB, per-policy table + one
  ASCII/SVG curve of WA vs u. Tests: map consistency invariant after every clean (each live LBA referenced exactly
  once, summary agrees), data integrity (block content = LBA+version stamp verified on read), greedy/cost-benefit
  victim selection on hand-built segment states, uniform-workload WA within a band of a known analytic/simulated
  reference (e.g. LFS paper write cost 2/(1−u) for victim u — careful: that's per-victim; or Desnoyers 2012 greedy
  model — hedge), cost-benefit beats greedy on hot-cold for the seeded run (assert the ordering, print numbers).
  FTL mapping: page-level FTL, block erase = segment clean, TRIM effect (practice: add TRIM and show WA drop),
  wear leveling interplay (mention). Don's SSD past = Part C. OSTEP ch.43 + lfs.py cross-check optional. Cite
  OS-M14; STOR items only if written.
- **OS-A09 xv6 해킹 1: trace + sysinfo** (qemu via Lima VM). Lesson dir layout (no starter.c): `check.sh`
  (bash, runs INSIDE the VM: `limactl shell elinux -- bash -lc 'bash <lesson dir>/check.sh starter|solution|practice'`),
  `run_xv6.py` (python3 stdlib driver: boots `make qemu CPUS=1` (or default — choose, justify) with nonblocking
  stdout, waits for `$ ` prompt, sends commands, matches expected output with timeouts, kills QEMU, prints PASS/FAIL
  lines + `N/M passed`, exit 0/1), and overlay dirs `starter/`, `solution/`, `practice/` — each mirrors xv6 paths
  (kernel/syscall.h, kernel/syscall.c, kernel/sysproc.c, kernel/proc.c, kernel/proc.h, kernel/kalloc.c,
  kernel/defs.h, user/user.h, user/usys.pl, Makefile, user/trace.c, user/sysinfotest.c, kernel/sysinfo.h ...),
  FULL copies of every changed file (so `solution/X diff=starter/X` highlights changes). check.sh: rsync pristine
  `~/.esn_scratch/xv6-riscv` → `/tmp/OS-A09_work/<variant>/`, overlay, `make`, run run_xv6.py; never touches the
  pristine clone. starter = all plumbing that would make it compile but syscalls return -1 / trace prints nothing →
  tests FAIL fast (exit 1, no hang; QEMU killed). Implement: `trace(int mask)` (per-proc mask, inherited by fork,
  print `<pid>: syscall <name> -> <ret>` on return), `sysinfo(struct sysinfo*)` (freemem bytes by walking kalloc
  freelist under lock, nproc = non-UNUSED procs; copyout to user, bad pointer → -1). Tests: user programs you
  write (tracetest: trace of a known command sequence matches exactly; fork inheritance; sysinfotest: freemem drops
  by exactly k pages after sbrk(k·PGSIZE) and comes back; nproc +1 after fork; bad pointer → -1) + `usertests -q`
  still passes (measure its time in the VM; if > ~3 min, run it in the solution check only and say so). The MIT
  6.1810 syscall lab defines these two syscalls — cite the lab page, write your own code/tests (don't paste lab
  skeleton files). Walk the path in 5. 풀이 with real file:line from the pinned commit: user/usys.pl stub → ecall →
  kernel/trampoline.S uservec → usertrap (trap.c) → syscall() dispatch (syscall.c) → sys_* → argint/argaddr →
  copyout → return a0. practice/: 추가 연습 e.g. trace prints arguments for selected syscalls, `sysinfo.nopenfiles`
  or `getprocs`, a `uptime_ms` — each with run_xv6.py tests. Firmware tie-in: RTOS SVC-based syscalls (cite
  RTOS-A01 if written), Linux syscall table, seccomp. Give the gdb-multiarch `make qemu-gdb` recipe (run it once,
  paste real output of a breakpoint at syscall()).
- **OS-A10 xv6 해킹 2: COW fork** (qemu via Lima VM). SAME layout/infra as A09 but fully independent copies
  (`/tmp/OS-A10_work/`, your own check.sh + run_xv6.py — don't share files with A09). Implement: per-physical-page
  refcount array (indexed by (pa − KERNBASE)/PGSIZE, protected by a lock; kalloc sets 1, kfree decrements and only
  frees at 0), uvmcopy maps parent pages into child read-only with an RSW bit (PTE_COW = 1<<8, verify RSW bits in
  kernel/riscv.h) and clears PTE_W in both, increments refcount; usertrap handles r_scause() == 15 (store/AMO
  page fault) on a COW page → if refcount == 1 just restore PTE_W, else kalloc+copy+remap; kill the process on
  invalid/out-of-range addresses or OOM; copyout() must handle COW pages (kernel writes to user memory don't
  trap through usertrap!). Tests (your own cowtest, written from scratch; cite the 6.1810 cow lab page): (1)
  `simple`: sbrk > half of free memory then fork — fails/panics without COW, succeeds with COW; (2) three-way
  fork with writes, data isolation verified; (3) pipe/read into a COW page (exercises copyout); (4) free memory
  returns to baseline after children exit (no refcount leak) — measure free pages via a counter you expose
  (e.g. a tiny `freepages()` syscall within this lesson's overlay — say it's a test hook) ; + `usertests -q`
  passes. Starter = plumbing compiles, behaviour = original eager copy or broken stubs so cowtest FAILs (no
  hang: run_xv6.py timeouts). Part C: Linux COW (do_wp_page, mm/memory.c in 6.18.55 — quote), OS-M03 measurement
  link, embedded: MMU-less systems have no COW → vfork/posix_spawn. 풀이 walks scause/stval/sepc, PTE flags,
  sfence.vma after PTE change (where xv6 does it).
- **OS-A12 견고한 공유 메모리 IPC** (mac; no robust mutex on macOS → lock-free protocol). Self-contained compact
  SPSC ring in `shm_open` memory ("DS-A01 링 버퍼 축약판" — read DS-A01 first). Header: magic, version, layout
  size/slot size/capacity (consumer refuses on mismatch — test with a v2 writer), producer/consumer pid +
  heartbeat counters + generation (producer bumps generation on (re)start → consumer resyncs from head), per-slot
  sequence numbers + payload checksum (detect torn/partial writes), message seq numbers → loss detection (count
  gaps). Full policies: BLOCK (bounded wait with timeout via polling/backoff — macOS has no futex; mention
  __ulock_wait/os_sync_wait_on_address as private/newer APIs, hedge), DROP_NEWEST, OVERWRITE_OLDEST (needs
  per-slot seqlock so a reader detects an overwritten slot — test it). Liveness: heartbeat staleness timeout +
  kill(pid, 0) ESRCH + pid-reuse caveat. Crash-injection tests: fork producer that self-kills (raise(SIGKILL)) at
  deterministic crash points (after payload write before publish, mid-header write, between seq and checksum
  ...), consumer must never accept a torn message, must detect producer death within a bound, a restarted producer
  (new generation) resumes and the consumer resyncs; same for consumer death (producer detects, policy).
  All shm names unique `/OS-A12.<pid>.<n>` (≤31 bytes on macOS) and unlinked; every fork waited. TSan clean on the
  threaded variant (if you have one). demo_bench.c: msgs/s and latency vs OS-B14-style pipe baseline (REAL numbers
  + uptime). Part C: iceoryx / Android FMQ / Chromium shared memory (hedge), robust mutex alternative on Linux =
  OS-M17 (cite), firmware: dual-core MCU mailbox/IPC shared RAM (STM32H7/nRF5340 IPC — hedge) with heartbeat.
- **OS-A13 미니 RPC** (mac): Unix stream socket (socketpair for in-process tests; a named socket in
  /tmp/OS-A13_work/ for the cross-process test, unlinked). Frame: u32 length (LE) + header {u8 version, u8 type
  (REQ, RESP, ERR, STREAM_ITEM, STREAM_END, CANCEL), u16 method, u32 req_id, u16 flags} + TLV payload (u16 tag, u32
  len, value); decoder skips unknown tags (forward compat) and rejects malformed lengths (fuzz-ish table of bad
  frames → error, never crash; ASan/UBSan). Partial read/write handling (frames split across reads — test with a
  1-byte-at-a-time transport shim). Client: single reader thread + pending table (req_id → slot with condvar),
  many caller threads issue concurrent calls; server answers out of order (artificial per-method delay) → results
  matched by req_id. Deadlines: call(timeout) returns ETIMEDOUT, pending entry removed, late response dropped and
  counted. Cancel: client sends CANCEL for a streaming call, server stops within K items. Server streaming: N
  STREAM_ITEMs then STREAM_END. Fault injection: server slow, server closes socket / process killed mid-call →
  every pending call returns an error promptly (EPIPE/ECONNRESET/EOF → fail all), no hang, no leak (threads joined,
  fds closed). Version compat test: v1 client ↔ v2 server with extra TLV fields both ways. demo_bench.c: RTT
  p50/p99 and calls/s at 1/4/16 concurrent callers (REAL + uptime). Compare in Part C with gRPC (HTTP/2 streams,
  deadline propagation, cancellation, protobuf unknown fields), D-Bus (serial / REPLY_SERIAL header fields — hedge
  if not verified), Binder (OS-A14 mentions). Firmware: MCU↔host RPC over UART (nanopb/erpc — hedge), framing with
  COBS/SLIP (cite COMM lessons that exist).
- **OS-A14 Mach 메시지와 XPC** (mac only). Mach part: `mach_port_allocate(MACH_PORT_RIGHT_RECEIVE)`,
  `mach_port_insert_right(..., MACH_MSG_TYPE_MAKE_SEND)`, server thread receives, client thread sends request
  with a reply port (`MACH_MSG_TYPE_MAKE_SEND_ONCE` in msgh_local_port), server replies to the send-once right;
  complex message (`MACH_MSGH_BITS_COMPLEX`) carrying a port descriptor (pass a right) and an OOL memory descriptor
  (large buffer — virtual copy; measure vs inline copy size in demo). Tests: request/reply correctness for many
  iterations, send-once right consumed (second use fails — check kr), port right refcounts via
  `mach_port_get_refs` before/after, no-senders or dead-name notification
  (`mach_port_request_notification` MACH_NOTIFY_NO_SENDERS / DEAD_NAME) delivered when the other side drops its
  right — deterministic, receive timeout (`MACH_RCV_TIMEOUT` → MACH_RCV_TIMED_OUT). Cross-process: try to pass a
  send right to a forked child (e.g. via task special port / bootstrap tricks) — if a simple supported way works,
  include it as a demo; if not, explain launchd/bootstrap (bootstrap_check_in requires a launchd job) and stay
  in-process — report what you tried. XPC part: in-process anonymous listener (`xpc_connection_create(NULL, q)` +
  `xpc_endpoint_create` + `xpc_connection_create_from_endpoint`) — dictionary request/reply with
  `xpc_connection_send_message_with_reply_sync`, error objects (XPC_ERROR_CONNECTION_INVALID on cancel) — verify it
  builds/runs (`-framework` not needed? check), else demo-only + hedge. demo_bench.c: round-trip latency mach_msg vs
  pipe vs Unix socket vs XPC (REAL numbers + uptime). Diagrams: port rights/name space, launchd service lookup,
  XPC layering. Linux Binder comparison (single copy via mmap'd receive buffer, object references + death
  notification, /dev/binder) — hedge, doc-based. Read /usr/include/mach/message.h and the SDK headers
  (SDKROOT 26.4) and quote them; macOS man pages that exist on this Mac only.

## Overrides for this set
- Output: ROOT/lessons/OS/<ID>/2026-11-15_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
  (+ demo_*.c) — except A09/A10 which use the overlay-dir layout above. Front matter `updated: 2026-11-15`, `time:`
  90–120분. Title line `# <ID> <title exactly as in OS.json>`. md **520–750 lines**. **Q&A ≥7 (aim 8–9)**, with at
  least 2 staff-level follow-ups ("…라면 어떻게 바꾸겠나?", "이 설계가 틀리는 경우는?"). ≥2 svg (aim 3).
- A09/A10 markers: §3 `<!-- code: starter/<path> -->` for the key 2–4 starter files + `<!-- code: check.sh -->`
  (+ run_xv6.py); §6 `<!-- code: solution/<path> diff=starter/<path> -->` for every changed file; §9
  `<!-- code: practice/<path> -->` for practice files. Every other lesson uses the standard starter.c /
  solution.c diff=starter.c / practice.c markers.
- Language C11 (`-std=c11`); feature macros before includes (`_POSIX_C_SOURCE 200809L`, `_DARWIN_C_SOURCE` for
  Darwin APIs, `_GNU_SOURCE` on linux files). Non-default build line → `/* BUILD: <extra flags> */` in the first 3
  lines (after `/* ENV: linux */` if present) and say so in the md.
- Mac items: `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` (27.2 SDK link broken). Host
  = Apple M6 (2 Super + 4 Performance + 6 Efficiency cores, hw.pagesize 16384, cache line 128). Machine is SHARED
  and loaded (other writers) — every benchmark table states `uptime` load average and treats numbers as ratios.
- Timing never in CHECKs. Every fork waited, every blocking op has a watchdog/timeout, no stray files/IPC names
  (unique names with your ID + pid, unlinked), no fixed TCP ports. Crash reports: avoid deliberate crashes of the
  test binary itself; children that die by signal are fine (macOS may still write .ips — mention if so).
- Starter: TODO stubs compile warning-free, return wrong-but-safe values; starter run FAILs with exit 1 within a
  few seconds, never hangs or crashes. solution = starter with only TODO bodies filled. solution/practice < 3 s each
  on the host (VM/QEMU checks < 3 min; say the real time), run solution 5× (not flaky).
- Accuracy: cite POSIX (pubs.opengroup.org), Linux man-pages (man7.org) + Linux 6.18.55 source file:line, xv6
  file:line at the pinned commit, macOS headers/man pages you actually read, OSTEP chapters, papers by
  author/year/title (hedge if unsure). State which OS each claim is about. Never invent output — paste what you
  ran. List anything from memory that you couldn't verify in your final report.
- PRIVATE scratch: Mac `/tmp/<ID>_work/` + `/tmp/<ID>_*` binaries; VM `/tmp/<ID>_work/`. 9 other writers run in
  parallel — never write helpers into a shared folder or another lesson's dir.

## Verify (you MUST run)
Mac items:
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
    cc -std=c11 -Wall -Wextra -Werror -O2 [BUILD flags] starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # FAILs, exit 1, no hang
    cc ... solution.c -o /tmp/<ID>_so && /tmp/<ID>_so    # all PASS, exit 0 (5×, also from another cwd)
    cc ... practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr    # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer [BUILD flags] solution.c / practice.c → clean
    cc -std=c11 -g -O1 -fsanitize=thread [BUILD flags] solution.c → clean   (only if pthreads are used)
    every demo_*.c builds with the strict flags; run it and paste real output.
A02 (linux): same commands inside the VM (gcc strict, gcc or clang ASan/UBSan; ptrace + ASan: if ASan interferes
  with the tracee, run sanitizers on the debugger with the child built plain, or explain and show what you ran).
A09/A10: `limactl shell elinux -- bash -lc 'bash <lesson dir>/check.sh starter'` → exit ≠ 0 fast;
  `... check.sh solution` → exit 0 (run 2×); `... check.sh practice` → exit 0. Paste the `N/M passed` lines.
md self-check: headings verbatim & ordered (scan outside ``` fences), `### Part A — / B — / C —`, ≥2 ```svg
  (viewBox, width ≤ 760, currentColor + f-acc/f-ok/f-warn/f-mut only, &lt; &gt; &amp; escaped), Q&A ≥7 as
  `<details><summary>Q1. ...`, 3–6 추가 연습 + `### 한 번에 검증` + practice marker, starter marker(s) in §3,
  solution diff marker(s) in §6, demo markers where shown. Every marker path must exist.

## When done
Reply (short): files written, md line count, PASS counts starter/solution/practice, ASan/UBSan (+TSan) results,
demo files + key numbers, anything unverified (list). Do NOT edit files outside ROOT/lessons/OS/<your ID>/ (and
your private /tmp paths). Don't run build_site.py, don't git commit.
