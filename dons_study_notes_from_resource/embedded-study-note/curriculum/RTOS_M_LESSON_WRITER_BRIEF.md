# Lesson writer brief — track RTOS (실시간 운영체제), Medium level

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math,
prepping for embedded/firmware/silicon-validation interviews (Anduril, Apple, NVIDIA, Rivian, Tesla...).
He re-checks fundamentals by DOING: every lesson = one interview-style coding problem.
RTOS depth is one of his weak spots (learning roadmap: "RTOS-B → RTOS-M: 스케줄러 · 우선순위 역전 · 큐. 면접 단골").

Already written (assume he knows them, cross-reference by id, don't re-teach): **RTOS-B01–B13 all** (B01 superloop/
cooperative, B02 TCB + states, B03 ucontext switch, B04 prio bitmap CLZ, B05 SIGALRM preemption/RR, B06 delay vs
delay_until + tick wrap + FreeRTOS overflow list, B07 FreeRTOS POSIX port first run, B08 binary sem ISR→task,
B09 counting sem + wait list, B10 mutex vs sem, B11 message queue copy semantics + pool, B12 software timers,
B13 event groups), RTOS-M01 (task notifications), RTOS-M03/M04/M05 (priority inversion / inheritance / ceiling),
RTOS-M14 (reentrancy), RTOS-A08 (SMP). Also C B/M/A, DS B/M/A, CPP B/M/A, ALGO B/M, ARCH B/M, OS-B, COMM-B,
C-M14 (ISR↔main shared data, signal as ISR, PRIMASK save/restore).

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY.
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base) → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md (Medium overrides)
   → ROOT/curriculum/RTOS_B_LESSON_WRITER_BRIEF.md (RTOS Basic brief: its mini-kernel vocabulary, timeline
   format, ucontext-on-macOS notes and FreeRTOS lane all apply to you). Everything applies unless overridden below.
3. ROOT/curriculum/tracks/RTOS.json — your item (title/goal/task/tags/env/env_note). The lesson must do the
   `task` completely (every function, test and measurement it names). Skim all RTOS items to stay in your lane.
4. Reference lessons (style + depth + harness): ROOT/lessons/RTOS/RTOS-M04/ (Mid, mini kernel, timeline, policy
   comparison) and ROOT/lessons/RTOS/RTOS-M01/ (Mid, FreeRTOS demo + standalone model). For the series feel also
   skim the Basic lesson your item builds on (named in your lane).

## Lanes (written IN PARALLEL right now — stay in yours; every file self-contained, no shared lib)
- RTOS-M02 ISR 지연 처리 (builds on B08): mini kernel with an `in_isr` context flag (+ nesting count),
  `sem_give_from_isr(s, &higher_woken)` that only SETS the flag (no switch inside the ISR), `isr_exit(higher_woken)`
  = portYIELD_FROM_ISR → switch happens exactly at ISR exit; blocking APIs (`sem_take` with timeout ≠ 0,
  `task_delay`, mutex take) called with in_isr → KERNEL_ASSERT (replaceable hook that counts, like B02).
  Measure (in ticks/steps, deterministic): latency from IRQ to handler-task first instruction with (a) yield at ISR
  exit, (b) no yield request (handler runs only at next tick / next scheduling point), (c) "all work in ISR" — and
  what (c) does to a lower-prio IRQ / other tasks' latency (long ISR = everyone's jitter). Nested ISRs: yield only
  at the OUTERMOST exit. Part C: UART RX ISR → ring buffer → parser task, or DMA-complete ISR. Quote FreeRTOS
  xSemaphoreGiveFromISR path (queue.c xQueueGiveFromISR, pxHigherPriorityTaskWoken), portYIELD_FROM_ISR /
  portEND_SWITCHING_ISR, configMAX_SYSCALL_INTERRUPT_PRIORITY + portASSERT_IF_INTERRUPT_PRIORITY_INVALID (port
  dependent — say which port has it), xTimerPendFunctionCallFromISR as "deferred function call". Optional FreeRTOS
  demo: give-from-ISR from vApplicationTickHook (the house's ISR stand-in on the POSIX port, see B08) with and
  without portYIELD_FROM_ISR → order of prints.
- RTOS-M06 블로킹 큐 (builds on B09 wait list + B11 copy queue): fixed-size message queue in a tick-driven mini
  kernel with TWO wait lists (waiting-to-send, waiting-to-receive), each sorted by priority then FIFO;
  full → sender blocks, empty → receiver blocks; on send wake the HIGHEST-prio receiver; timeout removes the task
  from the wait list (and from the delayed list — both lists, like xStateListItem+xEventListItem) and returns
  errQUEUE_EMPTY/FULL. Race scenarios to test deterministically: (1) a woken low-prio receiver loses the item to a
  higher-prio task that runs first → must re-check and re-block with the REMAINING timeout (FreeRTOS retry loop in
  xQueueReceive — quote queue.c), (2) timeout and give in the same tick (define and test the tie rule),
  (3) multiple senders blocked on full queue woken in prio order, (4) task deleted/suspended while waiting (optional).
  Invariant check every tick (no task in a wait list that isn't BLOCKED, count ∈ [0,len]). FreeRTOS demo with 3
  receivers of different prio on one queue → wake order matches the model. Quote vTaskPlaceOnEventList
  (listINSERT sorted by item value = configMAX_PRIORITIES - prio) + xTaskRemoveFromEventList.
- RTOS-M07 Tick 처리와 지연 리스트 (B06 owns wrap basics — don't re-teach): (a) sorted delayed list with
  FreeRTOS-style current/overflow list swap, (b) hashed timing wheel (N slots, per-slot unsorted list with
  rounds or sorted, document the choice; Varghese & Lauck 1987), optionally (c) hierarchical wheel note (Linux
  timer wheel — hedge details). Correctness: both produce the IDENTICAL expiry sequence as a brute-force reference
  over random insert/cancel/tick workloads incl. a tick-counter wrap. Benchmarks ONLY in demo_bench.c: 10,000
  timers, per-tick cost and insert/cancel cost (ns and/or operation counts — operation counts are deterministic and
  belong in solution checks; ns are host ratios). Discuss xNextTaskUnblockTime early-exit in xTaskIncrementTick
  (quote tasks.c), tickless idle link (next expiry = O(1) for sorted list, not for wheel), and when each wins.
- RTOS-M08 태스크 스택 예산 (TOOL-A08 is NOT written — do the per-function stack part yourself): real per-function
  stack usage from `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -O2 -fstack-usage -c`
  (.su files) for a small firmware-like call graph (e.g. control task: read_imu → filter → pid → log/printf-like
  formatter with a local buffer); checked code = a budget calculator: parse .su-like table + call graph (given as
  data in the C file, since clang has no -fcallgraph-info; GCC does — if you use Lima VM arm-none-eabi-gcc
  `-fcallgraph-info=su` say so) → worst path (DFS, recursion detected → error), + context frame (Cortex-M4
  basic frame 8 words; extended FP frame 26 words + alignment; FreeRTOS port saves r4–r11 + EXC_RETURN and s16–s31
  when the task used FPU — quote portable/GCC/ARM_CM4F/port.c PendSV handler from the kernel source tree),
  + ISR nesting: ON CORTEX-M ISRs RUN ON MSP — only the FIRST exception frame lands on the task's PSP stack;
  model both "ISRs on task stack" (ports without separate interrupt stack) and "ISRs on MSP" and show the
  difference in the table, + margin policy (e.g. +25 % or +N bytes; justify). Compare with measurement:
  (1) FreeRTOS POSIX port uxTaskGetStackHighWaterMark (RTOS-B07 found HWM = depth − 5 words because code runs on
  the pthread stack — show it, explain why it's meaningless here), (2) a REAL host measurement: run the same call
  graph on a painted ucontext stack (fill 0xA5, measure untouched bytes) on arm64 and compare with clang's
  arm64 -fstack-usage numbers for the same code. Table: function | .su bytes (M4) | .su (arm64) | measured.
- RTOS-M09 스택 오버플로 탐지: mini kernel with task stacks carved from ONE big arena (so an "overflow" writes into
  memory we own — no real OOB/UB, ASan stays clean) and a simulated or real SP. Method 1: at every context switch
  check saved SP within [stack_base, stack_top]; method 2: also check the last 16 bytes (check the exact FreeRTOS
  byte count + fill value in include/stack_macros.h V11.3.2 and quote it) still hold the pattern. Show: deep
  recursion / big local array caught by method 1 or 2; a case BOTH miss (frame jumps over the guard and SP is back
  in range at switch time, e.g. a large array written sparsely below the guard and released before the switch);
  a case method 1 misses but method 2 catches. Also: detection happens only at switch time → damage already done
  (neighbor task's TCB/stack corrupted — show it in the arena). Real fix options: MPU stack guard region / PSPLIM
  (ARMv8-M stack limit register, hedge "문서 기준"), guard page analog on host: mprotect(PROT_NONE) page below a
  ucontext stack → real SIGSEGV/SIGBUS caught in a demo (death test via fork). FreeRTOS POSIX port: the overflow
  hook never fires (code runs on pthread stack, B07 finding) — demonstrate or state with evidence.
- RTOS-M10 RTOS 힙 전략: implement a heap_4-style allocator (address-ordered free list, first fit, split with
  minimum block size, insert-with-coalesce on free, xFreeBytesRemaining / xMinimumEverFreeBytesRemaining) and a
  heap_2-style one (no coalescing; check what heap_2.c V11.3.2 actually does — size-ordered best fit — quote it)
  on the SAME static arena size. Deterministic xorshift alloc/free workload (sizes e.g. 8–512 B, mixed lifetimes):
  report fragmentation (largest free block / total free), first allocation failure step, and min-ever-free, as a
  table over several seeds; plus a pathological pattern where heap_2 fails early. Invariant checker after every
  op (free list sorted, no adjacent free blocks for heap_4, sizes sum to arena). Summarize heap_1/3/5 in a
  comparison table (determinism, free support, coalescing, multiple regions) with source quotes. FreeRTOS demo:
  build POSIX port with heap_4.c instead of heap_3.c, run the SAME workload through pvPortMalloc/vPortFree,
  compare vPortGetHeapStats with your model (byte-exact match is a great check if achievable; explain any diff:
  heap struct size, alignment portBYTE_ALIGNMENT). OS-B/OS-M own general malloc — only see:OS.
- RTOS-M11 Rate Monotonic 분석: C tool: parse (C,T,D) sets (D = T default; D < T → Deadline Monotonic note),
  RM priority assignment, U and Liu–Layland bound n(2^{1/n} − 1) (Liu & Layland 1973), hyperbolic bound
  Π(Ui+1) ≤ 2 (Bini, Buttazzo, Buttazzo 2003 — hedge if unsure), exact RTA iteration R = C + Σ⌈R/Tj⌉Cj (Joseph &
  Pandya 1986 / Audsley et al.) with integer ceil, stop when R > D. Find & show a set that fails LL bound but passes
  RTA, and a set with U ≤ 1 that fails RTA. Cross-check RTA by a small fixed-priority simulation from the critical
  instant (worst observed response == R for every task over the hyperperiod) — M12 owns the full RM-vs-EDF
  simulator, keep yours minimal. Extend with blocking term B (from M04/M05: max one critical section under
  PCP/IPCP) and release jitter J in Part C (R = C + B + Σ⌈(R+Jj)/Tj⌉Cj). Firmware scenario: motor control 1 kHz,
  IMU 500 Hz, comms, logging. Integer math only (ticks/µs), no float compare for schedulability decisions.
- RTOS-M12 EDF 시뮬레이터와 RM 비교: deterministic discrete-time simulator over the hyperperiod (lcm, overflow
  guarded) for RM and EDF with explicit tie rules (EDF tie → lower task index; document). Count deadline misses,
  preemptions, context switches, max response per task; policy on miss (job continues / is dropped — implement
  both, show the difference). Show: a set with U ≤ 1 that RM misses but EDF meets (e.g. U ≈ 0.97 two-task set);
  U > 1 overload where EDF's misses cascade across many tasks (domino effect) while RM confines misses to the
  lowest-prio tasks; svg Gantt from real output. EDF optimality on uniprocessor (Dertouzos 1974) + Liu & Layland
  U ≤ 1. Mention FreeRTOS has no EDF (fixed prio); Linux SCHED_DEADLINE = EDF + CBS (hedge details); Zephyr
  CONFIG_SCHED_DEADLINE (hedge). M11 owns RTA — reference it.
- RTOS-M13 FreeRTOS 내부 읽기: source-reading lesson WITH a checked coding problem. Reading path in V11.3.2
  (quote file:line for every step): xTaskCreate → prvCreateTask/prvInitialiseNewTask → prvAddNewTaskToReadyList
  → prvAddTaskToReadyList (+ taskRECORD_READY_PRIORITY) ; xTaskIncrementTick (delayed list, xNextTaskUnblockTime,
  time slicing) ; vTaskSwitchContext → taskSELECT_HIGHEST_PRIORITY_TASK (generic vs port-optimised) ; list.c
  vListInsert / vListInsertEnd / listGET_OWNER_OF_NEXT_ENTRY ; queue.c touchpoints. Call diagram as svg.
  Checked coding problem: re-implement FreeRTOS list.c semantics in a standalone C model (xListEnd sentinel with
  portMAX_DELAY, vListInsert stable after equal values, vListInsertEnd inserts BEHIND pxIndex, round-robin via
  listGET_OWNER_OF_NEXT_ENTRY skipping the end marker, uxListRemove fixing pxIndex) and test it against expected
  behaviours (incl. the subtle ones). Demo: real FreeRTOS on the POSIX port with trace macros defined in the
  lesson's FreeRTOSConfig.h (traceTASK_CREATE, traceMOVED_TASK_TO_READY_STATE, traceTASK_SWITCHED_IN,
  traceTASK_INCREMENT_TICK… — check which exist in FreeRTOS.h) printing the actual path; optionally lldb
  breakpoints on prvAddNewTaskToReadyList / vTaskSwitchContext (paste real session if you run it). Then "미니 커널과의
  차이 5가지" table with code evidence (e.g. two list items per TCB, pxIndex round robin, uxTopReadyPriority,
  xPendedTicks/scheduler suspension, critical nesting, deferred yield xYieldPendings).

## Shared conventions (same as Basic so the series reads as one kernel)
- Mini-kernel vocabulary from RTOS_B brief (task_state_t, tcb_t, kernel_tick(), sched_pick_next(); bigger prio =
  more urgent, note Zephyr/µC/OS opposite). Timeline format `tick : 0 1 2 ...` with R/r/B/S/. rows; svg Gantt
  drawn from the REAL output. Deterministic single-thread tick-driven kernels preferred; ucontext only where real
  stacks matter (M08, M09 demo).
- Benchmarks (ns) only in demo_*.c, reported as host ratios with load caveat; checked files assert deterministic
  quantities (ticks, op counts, bytes).

## Overrides for this set
- Output: ROOT/lessons/RTOS/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
  (+ demo_*.c, FreeRTOSConfig.h, hooks.c as needed). Front matter `updated: 2026-11-11`, `time:` 70–100분.
  Title line `# <ID> <title exactly as in RTOS.json>`. md 500–700 lines. Q&A ≥6 (aim 7–8).
- Firmware tie-in: 1. 문제 first paragraph = one line on why interviewers ask this; Part C = realistic MCU/RTOS
  scenario. Include a table mapping your host-model calls to the real API (FreeRTOS / Zephyr / CMSIS-RTOS2 where
  you're sure).
- Accuracy: quote FreeRTOS V11.3.2 source (file:line from ~/.esn_scratch/FreeRTOS-Kernel) for every FreeRTOS
  behaviour claim; papers by author/year/title; Zephyr / µC/OS / Linux claims hedged ("문서 기준, 미검증") unless
  verified. Don't claim an asm instruction you didn't observe. Never invent output — paste what you ran.
- Tests deterministic; starter FAIL exit 1 without hanging or crashing (stubbed scheduler/loops need step limits);
  solution + practice < 2 s each.
- PRIVATE scratch: /tmp/<ID>_work/ and /tmp/<ID>_* binaries (8 other writers run in parallel). Never write helpers
  into a shared folder or another lesson's dir.
- Build/verify (you MUST run):
      export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
      cc -std=c11 -Wall -Wextra -Werror -O2 [-pthread] starter.c -o /tmp/<ID>_st && /tmp/<ID>_st   # FAILs, exit 1
      same for solution.c (all PASS, exit 0, run 5×) and practice.c (all PASS, exit 0)
      cc -std=c11 -g -O1 -fsanitize=address,undefined [-pthread] solution.c / practice.c → clean
      NEVER run -fsanitize=thread (TSan): it hangs on the FreeRTOS POSIX port and leaves 100%-CPU zombie processes. Use ASan/UBSan only. Demos: build + run each, paste real output.
  Harness prints a final `N/M passed` line. (If a file needs _XOPEN_SOURCE/_DARWIN_C_SOURCE or gnu11, say so in its
  header comment and the md; the orchestrator's checker uses `-std=c11` unless the file's first lines contain
  `BUILD:` with a different line.) Checked files must NOT need the FreeRTOS kernel (only demo_freertos_*.c do).
- md self-check before reporting: headings verbatim & ordered, `### Part A — / B — / C —`, ≥2 ```svg with viewBox,
  width ≤760, currentColor + f-acc/f-ok/f-warn/f-mut only, &lt; &gt; &amp; escaped in svg text, Q&A count ≥6,
  3–6 추가 연습 + `### 한 번에 검증` + `<!-- code: practice.c -->`, `<!-- code: starter.c -->` in §3,
  `<!-- code: solution.c diff=starter.c -->` in §6, demo markers `<!-- code: demo_x.c -->` where shown.

## When done
Reply (short): files written, md line count, PASS counts starter/solution/practice, ASan/UBSan results,
demo files + key numbers, anything unverified/unsure (list). Do NOT edit files outside ROOT/lessons/RTOS/<ID>/
(and your /tmp/<ID>_* paths). Don't run build_site.py, don't git commit.
