# Lesson writer brief — 동시성 가이드 2층 "임베디드 — ISR · RTOS 동기화"

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt). Context: Don is working through
ROOT/2026-11-08_concurrency_practice_guide.md layer by layer; layer 1 (OS-B04/B05/B06, TOOL-M09, OS-M02,
LPERF-B10) is done; layer 2 = C-M14 (done), C-A04 (done), RTOS-B08, B09, B10, M01, M03, M04, M05, M14, A08,
BM-M11, RUST-M06. These are the FIRST lessons in the RTOS / BM / RUST tracks (RTOS-B01–B07 are NOT written,
so "미니 커널" in the item task means a kernel YOU embed in your own lesson — self-contained, no shared lib).

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY.
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base) → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md (Medium
   overrides) → ROOT/curriculum/C_A_LESSON_WRITER_BRIEF.md (Advanced overrides; its "Concurrency items" bullet)
   → ROOT/curriculum/CONC1_LESSON_WRITER_BRIEF.md (layer-1 brief: its "Overrides / extras" and "Verify"
   sections apply to you too, except its lane list). Everything applies unless overridden below. Ignore any
   "Do NOT edit outside lessons/C/..." path — your dir is given below.
3. ROOT/2026-11-08_concurrency_practice_guide.md §2 — your layer, incl. its "면접 체크" bullets: your lesson
   must let Don answer the ones relevant to your item.
4. ROOT/curriculum/tracks/<TRACK>.json — your item (title/goal/task/tags/env). The lesson must do the `task`.
5. Reference lessons (style + what NOT to re-teach): ROOT/lessons/C/C-M14/ (ISR↔main shared data, signal-as-ISR,
   PRIMASK save/restore, seqlock) and ROOT/lessons/C/C-A04/ (strtok vs strtok_r, reentrant vs thread-safe vs
   async-signal-safe). Also ROOT/lessons/OS/OS-B06/ (condvars) for depth.

## Lanes (written IN PARALLEL — stay in yours, cross-reference others by id, don't re-teach them)
- RTOS-B08 (Basic): binary semaphore as ISR→task signal; events faster than the handler MERGE (lost) vs
  counting semaphore keeps the count. Give-from-ISR never blocks; take blocks.
- RTOS-B09 (Basic): counting semaphore with a wait list in a mini kernel: 3 DMA channels / 5 tasks (never >3
  holders, every task eventually served, wake order = priority then FIFO — say which and test it), and an
  event-count scenario (N gives before a take → N takes succeed without blocking). Include max-count saturation.
- RTOS-B10 (Basic): mutex vs semaphore: ownership (non-owner give), recursive mutex nested take/give count,
  semaphore-as-mutex problems (anyone can release, no priority inheritance → points to RTOS-M03/M04), why a
  mutex can't be used from an ISR (ISR has no task identity to own/inherit/block). Results table.
- RTOS-M01 (Mid): task notifications (overwrite / increment / bit-OR / no-overwrite) as light replacements for
  binary/counting semaphore and event group; RAM + time per call compared; single-receiver limit.
- RTOS-M03 (Mid): unbounded priority inversion reproduced with a plain (non-inheriting) semaphore; tick-by-tick
  timeline; H's blocking time grows linearly with M's run length (table + svg chart for several C_M values).
  Mars Pathfinder 1997 story (VxWorks, bc_dist/ASI-MET, watchdog reset, PI flag turned on via uplink) — cite
  Glenn Reeves' account. Do NOT implement inheritance (M04) or ceiling (M05) — just show the fix preview.
- RTOS-M04 (Mid): priority inheritance in a mini kernel mutex: boost owner to max waiter priority, restore on
  release with MULTIPLE held mutexes (restore = max(base, max waiter prio over still-held mutexes)), chained
  (transitive) inheritance A→B→C; rerun the M03 scenario → H's wait bounded (independent of C_M). Mention that
  FreeRTOS and Zephyr do NOT do transitive PI (FreeRTOS: vTaskPriorityDisinheritAfterTimeout; Linux rt_mutex
  does) — verify any such claim against source you can read, or hedge it.
- RTOS-M05 (Mid): Immediate Priority Ceiling (IPCP / "highest locker") mutex; two mutexes taken in opposite
  order: deadlock under plain/inheritance mutex (detect it deterministically, e.g. "all tasks blocked" check),
  avoided under IPCP; max blocking = one critical section (OPCP/PCP vs IPCP difference explained; POSIX
  PTHREAD_PRIO_PROTECT = IPCP, PTHREAD_PRIO_INHERIT = PI).
- RTOS-M14 (Mid): reentrancy under PREEMPTION in a mini kernel: static-buffer format function + strtok shared
  by two tasks, preempted mid-call → interleaved/garbled output; fixes: _r version, mutex wrapping, per-task
  context (newlib struct _reent / _impure_ptr switched per task, __malloc_lock/__malloc_unlock; FreeRTOS
  configUSE_NEWLIB_REENTRANT). C-A04 already owns strtok semantics + the 3-property classification — just
  cross-ref it; your new part is preemption in a kernel + the newlib/libc locking story.
- RTOS-A08 (Adv): SMP scheduler simulator: 2 pthreads = 2 "cores", global ready list + spinlock (C11 atomics),
  affinity masks, cross-core yield via IPI stand-in (per-core pending flag/condvar or pthread_kill signal),
  and a case where the single-core assumption "disable interrupts = mutual exclusion" breaks (per-core irq
  mask doesn't stop the other core) → fixed with spinlock + irq-disable (FreeRTOS SMP taskENTER_CRITICAL
  takes ISR+task locks; RP2040 hardware spinlocks). TSan must be clean for the fixed version.
- BM-M11 (Mid): power state machine simulator (Run / Sleep(WFI) / Stop / Standby with currents, wake latency,
  what's retained, where execution resumes) → average current vs duty cycle (table + svg chart, battery life);
  the sleep-entry race: `if (!flag) WFI()` loses a wakeup when the IRQ lands between check and WFI; correct
  pattern `__disable_irq(); if(!flag) __WFI(); __enable_irq();` works because a pending IRQ still wakes WFI
  with PRIMASK=1 (ARMv7-M ARM B1.5.19 / WFI wake-up conditions) — show with an event-timing test that injects
  the IRQ at EVERY possible point in the sequence. Also WFE + event register / SEV, SLEEPONEXIT. Real
  Cortex-M asm for the pattern from clang --target=thumbv7em-none-eabi.
- RUST-M06 (Mid): see "Rust item" (only if your prompt says so).

## Mini-kernel design (RTOS-B09/M03/M04/M05/M14; optional for B08/B10)
- DETERMINISM FIRST. Recommended: a single-OS-thread, tick-driven simulated kernel. Tasks are either
  (a) explicit step functions/state machines ("each tick, run one step of the highest-priority ready task"), or
  (b) ucontext coroutines switched at explicit preemption points. On macOS ucontext is deprecated: if you use
  it, `#define _XOPEN_SOURCE 600` and silence only that warning locally with
  `#pragma clang diagnostic ignored "-Wdeprecated-declarations"` (explain why in the md), give each task a
  ≥64 KB stack, and verify under ASan (ASan may need `detect_stack_use_after_return=0`; report what you saw).
  (a) is preferred unless the lesson needs real call stacks (M14 likely needs (b) or a step-wise strtok model).
- Preemption = the scheduler always runs the highest-priority READY task each tick (equal priority FIFO).
  ISR stand-in = a function the harness calls at scripted ticks.
- Timeline output format (shared by M03/M04/M05 so they read as one series):
      tick : 0 1 2 3 4 5 ...
      H    : . . R B B ...      (R=running, B=blocked on mutex/sem, r=ready but not running, . = not released/idle)
      M    : ...
      L    : ...
  plus an svg Gantt drawn from the REAL timeline your code printed.
- Canonical inversion scenario (M03 defines it, M04/M05 reuse it with the same numbers): priorities H=3, M=2,
  L=1. L released t=0, takes R at t=1, needs R for 4 ticks of work. H released t=2, requests R at its first
  step. M released t=3, pure CPU work for C_M ticks (no R). Report H's blocking time for C_M ∈ {0,2,4,8,16}.
  If your exact semantics shift a tick, keep the scenario and state the numbers you actually got.
- Checks are on the timeline/state (e.g. "H blocked ticks == 4 + C_M" for plain sem; "≤ L's remaining CS" with
  PI), never on wall-clock time.

## FreeRTOS items (RTOS-B08 / B10 / M01) — only when your prompt says FreeRTOS is allowed
- Kernel source: ~/.esn_scratch/FreeRTOS-Kernel (V11.3.2 tag, already cloned; do not re-clone, do not edit).
  macOS POSIX port: portable/ThirdParty/GCC/Posix (+ utils/wait_for_event.c), heap_3. Config template:
  ~/.esn_scratch/freertos-posix-mac/FreeRTOSConfig.h + hooks.c (copy into your lesson dir as needed; your
  lesson may ship its own FreeRTOSConfig.h). Build line pattern:
      K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
      cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_x.c hooks.c \
         $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c
  (kernel sources may warn; don't put -Werror on kernel files.) The POSIX port has no real-time behaviour —
  use it to verify ORDER and return values, not timing (except M01's relative per-call cost, labelled as host).
- FreeRTOS programs are demo_freertos_*.c (they need the kernel, so they are NOT the checked starter/solution).
  Checked starter/solution/practice stay standalone C11 (+pthreads): e.g. your own small model of the API
  semantics, tested deterministically, then the demo shows real FreeRTOS agrees. Paste real output only.
  Quote FreeRTOS source (file:line in V11.3.2) for any behaviour claim (e.g. xSemaphoreGive on a mutex by a
  non-owner → which path in queue.c / configASSERT).
- If you can't get the POSIX port to build/run on macOS within ~20 min, say so in your report and fall back to
  source-reading + your own model (label the md accordingly).

## Overrides for this set
- Output: ROOT/lessons/<TRACK>/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + starter.c + solution.c +
  practice.c (+ demo_*.c). Front matter `updated: 2026-11-08`; `time:` Basic 50–70분, Mid 70–100분, Adv 90–120분.
  Title line `# <ID> <title exactly as in the track JSON>`. md length: Basic 450–600 lines, Mid 500–700,
  Adv 550–750. Q&A ≥5 Basic, ≥6 Mid/Adv.
- Firmware tie-in: 1. 문제 first paragraph + Part C must be realistic MCU/RTOS scenarios (UART RX ISR → parser
  task, DMA-complete ISR, motor-control/sensor tasks, battery-powered sensor node for BM-M11). Map every
  host-model call to the real API (FreeRTOS xSemaphoreGiveFromISR + portYIELD_FROM_ISR, Zephyr k_sem_give,
  CMSIS __disable_irq/__WFI) in a table.
- Accuracy: cite FreeRTOS source/docs, ARMv7-M ARM sections, POSIX, Liu & Layland / Sha-Rajkumar-Lehoczky 1990
  (PI/PCP) where relevant. Hedge anything not verified ("대표값", "문서 기준, 미검증"). Don't claim an asm
  instruction you didn't observe.
- Use a PRIVATE scratch dir for helper files: /tmp/<ID>_work/ (writers share the machine; never write helpers
  in a shared folder).

## Rust item (RUST-M06 only)
- Toolchain: Homebrew rustup, stable rustc 1.99.1, targets thumbv7em-none-eabihf / thumbv6m-none-eabi /
  thumbv7m-none-eabi + clippy installed. Put `export PATH=/opt/homebrew/opt/rustup/bin:$PATH` in front of
  every command (and in the md's run commands note it as "rustup PATH"). crates.io downloads are OK. Lesson is a cargo crate:
  ROOT/lessons/RUST/RUST-M06/{starter,solution,practice}/ (each its own small crate or one crate with
  bin targets — pick one, document the run commands) + md. Starter tests fail (cargo test exit ≠ 0, no hang),
  solution + practice pass. Host: `critical-section` crate with feature `std` + a thread as the "ISR". Show the
  real rustc error for `&STATIC_MUT` in edition 2024 (static_mut_refs). Cross-build for thumbv7em-none-eabihf
  and thumbv6m-none-eabi (`cargo build --target ...`, `#![no_std]` lib part) to show AtomicU32::fetch_add
  missing on thumbv6m and portable-atomic fixing it — paste the real error. Also run `cargo clippy` clean.
  Don't put target/ dirs in the lesson (build with CARGO_TARGET_DIR=/tmp/RUST-M06_target).

## Verify (you MUST run) — same as CONC1 brief "Verify" (strict build; starter FAIL exit 1 no hang; solution +
practice PASS exit 0, run solution 5+ times; ASan+UBSan clean; TSan clean if any pthread). Plus the md
self-check (headings verbatim & ordered, ≥2 ```svg with viewBox/width ≤760/currentColor/f-* classes, escaped
&lt; &gt; &amp;, Q&A count, 3–6 추가 연습 + `### 한 번에 검증` + `<!-- code: practice.c -->`, starter marker
in §3, `<!-- code: solution.c diff=starter.c -->` in §6, demo markers).

## When done
Reply (short): files written, PASS counts starter/solution/practice, ASan/UBSan + TSan results, demo files +
what they showed (key numbers), anything unverified/unsure (list — the orchestrator copies it into the
guide's 작성 메모 table: env limits + unverified claims). Do NOT edit files outside ROOT/lessons/<TRACK>/<ID>/
(don't run build_site.py).
