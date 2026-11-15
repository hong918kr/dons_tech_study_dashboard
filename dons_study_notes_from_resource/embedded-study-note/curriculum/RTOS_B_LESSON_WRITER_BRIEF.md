# Lesson writer brief — track RTOS (실시간 운영체제), Basic level

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math,
prepping for embedded/firmware/silicon-validation interviews (Anduril, Apple, NVIDIA, Rivian, Tesla...).
He re-checks fundamentals by DOING: every lesson = one interview-style coding problem.
RTOS depth is one of his weak spots (learning roadmap: "RTOS-B → RTOS-M: 스케줄러 · 우선순위 역전 · 큐. 면접 단골").

Already written (assume he knows them, cross-reference by id, don't re-teach): RTOS-B08 (binary semaphore,
ISR→task, event merge), RTOS-B09 (counting semaphore + wait list), RTOS-B10 (mutex vs semaphore, recursive),
RTOS-M01 (task notifications), RTOS-M03/M04/M05 (priority inversion / inheritance / ceiling), RTOS-M14
(reentrancy), RTOS-A08 (SMP). Also C Basic/Medium/Advanced, DS B/M/A, CPP B/M/A, ALGO-B, ARCH-B, OS-B04–B06
(threads/mutex/condvar), C-M14 (ISR↔main shared data, signal as ISR, PRIMASK save/restore).

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY.
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base) and ROOT/curriculum/ALGO_B_LESSON_WRITER_BRIEF.md (latest Basic
   brief: its "Code rules" + "Quality bar" apply to you; ignore its ALGO-only "Interview flavour" except: 2.
   Problem (English) reads like a real interview prompt). Everything applies unless overridden below.
3. ROOT/curriculum/tracks/RTOS.json — your item (title/goal/task/tags/env/env_note). The lesson must do the
   `task` completely (every function, test and measurement it names). Skim all RTOS items to stay in your lane.
4. Reference lessons (style + depth + harness): ROOT/lessons/RTOS/RTOS-B08/ (Basic, FreeRTOS demo + standalone
   model) and ROOT/lessons/RTOS/RTOS-B09/ (Basic, tick-driven mini kernel). Their timeline format and
   FreeRTOS↔model mapping tables are the house style for this track.

## Lanes (written IN PARALLEL right now — stay in yours; every file self-contained, no shared lib)
- RTOS-B01 슈퍼루프 → 협력형 스케줄러: 3 periodic jobs (e.g. 10/50/100 ms: sensor read, control, telemetry)
  as (a) a superloop and (b) a timestamp-based run-to-completion scheduler (task table: fn, period, next_run;
  wrap-safe `(int32_t)(now - next) >= 0`; next_run += period, not now + period). Use a SIMULATED clock
  (each job "costs" scripted ticks) so jitter numbers are deterministic; measure release jitter / max latency
  per job with and without one long job (e.g. 30 ms telemetry), table + svg timeline. Show that cooperative
  scheduling bounds latency by the longest job (→ motivates preemption, RTOS-B05) and the fix of splitting the
  long job into a state machine. Optional demo with real clock_gettime + nanosleep for flavour (label host).
- RTOS-B02 TCB 설계와 상태 전이: TCB struct (sp/ctx placeholder, prio, base_prio, state, wake_tick, wait_obj,
  list node(s) — explain why FreeRTOS has xStateListItem + xEventListItem), state enum
  READY/RUNNING/BLOCKED/SUSPENDED (+ DELETED if you want), transition functions (make_ready, run, block_on,
  delay, suspend, resume, unblock/timeout), legal-transition table, illegal ones (BLOCKED→RUNNING direct,
  SUSPENDED→RUNNING, RUNNING→RUNNING...) rejected by a KERNEL_ASSERT. Testing an assert: make the assert hook
  replaceable (like configASSERT) so the test counts violations instead of aborting, AND show a real abort
  death test via fork()+waitpid (SIGABRT) in solution or a demo. Map to FreeRTOS eTaskState / tskTCB fields
  (quote tasks.c V11.3.2 lines) and Zephyr thread states briefly. No context switching here (B03 does it).
- RTOS-B03 ucontext 컨텍스트 스위치: getcontext/makecontext/swapcontext, two tasks with their own stacks
  yield back and forth; verify locals survive and stack addresses lie inside each task's own stack array
  (print + CHECK ranges); show what makecontext needs (uc_stack, uc_link) and what happens on return with/without
  uc_link. Explain what a context really is (callee-saved regs + sp + pc; on arm64 x19–x28, fp, lr, sp, d8–d15)
  — look at the real ucontext/mcontext layout on this Mac (`_STRUCT_ARM_THREAD_STATE64`) and say what you saw.
  Contrast with Cortex-M PendSV (hardware stacks r0–r3,r12,lr,pc,xPSR; software saves r4–r11 → RTOS-A01/ARM-A01).
  Optional demo: hand-written arm64 context switch is NOT required; a stack-canary demo (fill pattern, measure
  high-water mark) is a good Part C.
- RTOS-B04 우선순위 비트맵 ready list (CLZ): 32-priority bitmap + per-priority FIFO (intrusive doubly linked or
  ring), O(1) highest-ready via 31 - clz (own CLZ + compare against __builtin_clz in tests; exhaustive over all
  2^32 bitmaps is optional — at least random + all single/double-bit patterns), FIFO order within a priority,
  remove-from-middle keeps bitmap consistent. Use it as the scheduler of a small ucontext mini kernel (cooperative
  yield is enough here; preemption is B05). Show `clz` on arm64 and Cortex-M4 (`clang --target=thumbv7em-none-eabi
  -mcpu=cortex-m4 -O2 -S`) and Cortex-M0 (no CLZ → what the compiler emits; thumbv6m) — paste real asm.
  FreeRTOS configUSE_PORT_OPTIMISED_TASK_SELECTION / portGET_HIGHEST_PRIORITY (quote source).
- RTOS-B05 선점과 라운드 로빈 (SIGALRM tick): setitimer(ITIMER_REAL) SIGALRM as the tick; on tick the handler
  decides preemption / time-slice expiry and swapcontext()s (learning-only; not async-signal-safe per POSIX —
  say so, cite). Show an infinite-loop task no longer starves others; measure context-switch count vs slice length
  (1/2/5/10 ticks) in a table. Real-timer runs are nondeterministic → CHECKs must be robust (e.g. "every task ran
  ≥1 time", "switches within [expected*0.5, expected*1.5]"), run solution 5+ times, all PASS. Shared
  data between handler and tasks: volatile sig_atomic_t / sigprocmask critical sections (= the RTOS
  enter/exit critical). Also provide a deterministic "simulated tick" variant if it helps testing. Map to
  SysTick_Handler → PendSV, configUSE_PREEMPTION / configUSE_TIME_SLICING (quote tasks.c xTaskIncrementTick).
- RTOS-B06 Delay vs DelayUntil + tick wrap: delayed list sorted by wake tick (wrap-safe compare, or FreeRTOS's
  two-list overflow trick — explain both, implement one, say which FreeRTOS uses: pxDelayedTaskList /
  pxOverflowDelayedTaskList, quote source), delay(ticks) and delay_until(&last, period). Variable job time →
  measure drift of delay vs delay_until over N periods (table + svg). Tick counter starting at
  0xFFFFFFF0-ish so it wraps during the test; test both delays + ordering across the wrap. Deterministic sim
  kernel (tick-driven) is fine — no ucontext required. Mention xTaskDelayUntil returns pdFALSE when the deadline
  already passed (check source), and vTaskDelay(0) = yield.
- RTOS-B07 FreeRTOS POSIX 포트 첫 실행: write your own FreeRTOSConfig.h (every line commented: tick rate,
  priorities, stack, heap, hooks, trace facility, stats formatting), 3 tasks with different priorities, print
  vTaskList (or uxTaskGetSystemState) state + stack high-water mark, then vTaskPrioritySet and observe the change
  in run order. See "FreeRTOS lane" below. Explain the POSIX port model (each task = pthread, only one allowed to
  run; tick = signal/timer) and what is NOT real (timing, stack sizes in words vs pthread stacks). Also explain the
  vTaskList columns (state letters X/R/B/S/D, prio, stack HWM in words, task number).
- RTOS-B11 메시지 큐: sensor → process → log pipeline with xQueue (copy semantics: xQueueSend copies
  uxItemSize bytes), depth / block timeout / full policy (block, drop-newest with 0 timeout, overwrite with
  xQueueOverwrite on length-1 queue), and the large-buffer version: pointer + fixed-block pool (ownership passes
  with the pointer; free list). Standalone checked model of a copy-queue with blocking semantics + FreeRTOS
  demo(s) that confirm ordering/return values. Copy cost table (item size vs time, host, ratios only). ISR
  variant xQueueSendFromISR mention.
- RTOS-B12 소프트웨어 타이머: one-shot vs auto-reload, LED blink (auto-reload) + comms timeout (one-shot,
  xTimerReset on each RX), callbacks run in the timer service (daemon) task: a blocking callback delays the other
  timers (measure), timer-task priority vs app tasks (configTIMER_TASK_PRIORITY, timer command queue
  configTIMER_QUEUE_LENGTH, xTimerPendFunctionCall). Standalone checked model of a timer list in a tick sim + FreeRTOS
  demo(s).
- RTOS-B13 이벤트 그룹: implement event group in a mini kernel (set/clear/wait with ALL|ANY, clear-on-exit,
  timeout) incl. waking MULTIPLE waiters on one set and clear-on-exit applied after evaluating all waiters
  (FreeRTOS semantics — check event_groups.c xEventGroupSetBits and quote it). Scenario: 3 init tasks
  (sensor/radio/storage) each set a bit, main waits ALL. Race: separate check-then-wait (get bits, then wait)
  loses an event / clear-by-wrong-task — reproduce deterministically, fix with atomic wait-with-clear;
  xEventGroupSync rendezvous mention. Compare with task notifications (RTOS-M01) in one line.

## Mini-kernel vocabulary (keep consistent across B02–B06/B13 so the series reads as one kernel)
    typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED } task_state_t;
    typedef struct tcb { const char *name; uint8_t prio; task_state_t state; uint32_t wake_tick;
                         void *wait_obj; struct tcb *next, *prev; /* + ctx / stack as the lesson needs */ } tcb_t;
    names: kernel_tick(), task_yield(), task_delay(), sched_pick_next(); priorities: bigger number = more
    urgent (FreeRTOS convention — note that Zephyr/µC/OS use the opposite).
Timeline format (from RTOS-B09/M03): `tick : 0 1 2 ...` then one row per task with R=running, r=ready not
running, B=blocked, S=suspended, .=not released/idle; svg Gantt drawn from the REAL timeline your code printed.

## ucontext on macOS (B03/B04/B05)
- `#define _XOPEN_SOURCE 600` before includes (or `_DARWIN_C_SOURCE` if needed), silence ONLY deprecation locally
  with `#pragma clang diagnostic ignored "-Wdeprecated-declarations"` — explain why in the md. Build lines must
  still pass `-Wall -Wextra -Werror`. Stacks ≥ 64 KB, static arrays or malloc, aligned 16.
- ASan + swapcontext: ASan may warn about stack switching; try it, report exactly what happened, and if needed use
  `ASAN_OPTIONS=detect_stack_use_after_return=0` (state it in the md build command). UBSan alone must be clean.

## FreeRTOS lane (B07, B11, B12; optional demo for B13)
- Kernel: ~/.esn_scratch/FreeRTOS-Kernel (V11.3.2; don't edit, don't re-clone). Config template + hooks:
  ~/.esn_scratch/freertos-posix-mac/ (FreeRTOSConfig.h, hooks.c, main.c). Copy what you need into your lesson
  dir (lesson ships its own FreeRTOSConfig.h + hooks.c). Build pattern:
      K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
      cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_x.c hooks.c \
         $K/{tasks,list,queue,timers,event_groups}.c $K/portable/MemMang/heap_3.c $P/port.c $P/utils/wait_for_event.c
  (no -Werror on kernel files.) Programs end with exit(code) from a task (see RTOS-B08 demo_freertos_isr_sem.c).
  POSIX port = ORDER and return values, not timing.
- B11/B12: checked starter/solution/practice = standalone C11 models (tick-driven, deterministic) + demo_freertos_*.c
  showing real FreeRTOS agrees (paste real output, quote source file:line for behaviour claims).
- B07 only: starter/solution/practice MAY be FreeRTOS programs (first-run lesson). If so: first line comment
  `/* BUILD: freertos */`, put the exact build command in the md, the lesson's own .c files must compile with
  `-Werror` (compile them as separate objects with -Werror, kernel objects without), starter exits 1 with FAILs,
  solution/practice exit 0, and results must be identical over 5 runs (check only what priority scheduling
  guarantees on the POSIX port). If that proves flaky, fall back to standalone model + demo like B11/B12.
  Also run ASan+UBSan on the FreeRTOS build and report the result (if the port is incompatible with ASan, say so).
- If you can't make the POSIX port build/run within ~20 min, report it and fall back to source reading + model.

## Overrides for this set
- Output: ROOT/lessons/RTOS/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
  (+ demo_*.c, FreeRTOSConfig.h, hooks.c as needed). Front matter `updated: 2026-11-11`, `time:` 50–70분.
  Title line `# <ID> <title exactly as in RTOS.json>`. md 450–620 lines. Q&A ≥5 (aim 6–7).
- Firmware tie-in: 1. 문제 first paragraph = one line on why interviewers ask this; Part C = realistic MCU/RTOS
  scenario (STM32/nRF, UART RX, IMU sampling, motor control loop, BLE sensor node, watchdog kick). Include a table
  mapping your host-model calls to the real API (FreeRTOS / Zephyr / CMSIS-RTOS2 where you're sure).
- Accuracy: quote FreeRTOS V11.3.2 source (file:line from ~/.esn_scratch/FreeRTOS-Kernel) for every FreeRTOS
  behaviour claim; Zephyr / µC/OS claims hedged ("문서 기준, 미검증") unless you verified them. Don't claim an
  asm instruction you didn't observe. Never invent output — paste what you ran.
- Tests deterministic (except B05 real-timer part with robust bounds as above); starter FAIL exit 1 without
  hanging or crashing (a stubbed scheduler must not spin forever — add step/tick limits); solution + practice
  < 2 s each (B05 may take up to ~3 s).
- PRIVATE scratch: /tmp/<ID>_work/ and /tmp/<ID>_* binaries (9 other writers run in parallel).
- Build/verify (you MUST run):
      export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
      cc -std=c11 -Wall -Wextra -Werror -O2 [-pthread] starter.c -o /tmp/<ID>_st && /tmp/<ID>_st   # FAILs, exit 1
      same for solution.c (all PASS, exit 0, run 5×) and practice.c (all PASS, exit 0)
      cc -std=c11 -g -O1 -fsanitize=address,undefined [-pthread] solution.c / practice.c → clean
      NEVER run -fsanitize=thread (TSan): it hangs on the FreeRTOS POSIX port and leaves 100%-CPU zombie processes. Use ASan/UBSan only. Demos: build + run each, paste real output.
  (If a file needs _XOPEN_SOURCE/_DARWIN_C_SOURCE or gnu11, say so in its header comment and the md; the
  orchestrator's checker uses `-std=c11` unless the file's first lines contain `BUILD:` with a different line.)
- md self-check before reporting: headings verbatim & ordered, `### Part A — / B — / C —`, ≥2 ```svg with viewBox,
  width ≤760, currentColor + f-acc/f-ok/f-warn/f-mut only, &lt; &gt; &amp; escaped in svg text, Q&A count,
  3–6 추가 연습 + `### 한 번에 검증` + `<!-- code: practice.c -->`, `<!-- code: starter.c -->` in §3,
  `<!-- code: solution.c diff=starter.c -->` in §6, demo markers `<!-- code: demo_x.c -->` where shown.

## When done
Reply (short): files written, md line count, PASS counts starter/solution/practice, ASan/UBSan results,
demo files + key numbers, anything unverified/unsure (list). Do NOT edit files outside ROOT/lessons/RTOS/<ID>/
(and your /tmp/<ID>_* paths). Don't run build_site.py, don't git commit.
