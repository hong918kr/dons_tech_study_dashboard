# Lesson writer brief — track C (임베디드 C 핵심), **Advanced** level (C-A01–C-A11)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering: ROOT/curriculum/LESSON_WRITER_BRIEF.md (Basic, base rules) → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md
(Medium overrides) → **this file** (Advanced overrides). Read all three plus ROOT/NOTES_SPEC.md. Everything in the
earlier briefs applies (output files, headings verbatim and in order, svg rules, strict build, ASan/UBSan, PRNG
determinism, demo_*.c for observations, "never invent output") unless overridden here.

## Overrides for Advanced
- Level: Advanced = senior/staff firmware interview depth. The question is the item's `task`; the interviewer's
  follow-ups probe *design judgement* (why this API, what breaks on another core/compiler, how you'd test it).
  Assume all C-B and C-M items are known and cross-reference them by id instead of re-teaching:
  C-M01 MMIO volatile/reg struct, C-M02 RMW/W1C, C-M03 bitfields, C-M04 packed/unaligned, C-M05 endian serialize,
  C-M06 fn-pointer tables, C-M07 table FSM, C-M08 memcpy/memmove, C-M09 strtol, C-M10 itoa/mini printf,
  C-M11 macro/inline/_Generic, C-M12 UB catalog, C-M13 strict aliasing, C-M14 ISR shared data/atomicity,
  C-M15 goto cleanup. (All written: ROOT/lessons/C/C-Mxx/.) Read the 1–2 Medium lessons closest to your item
  (e.g. C-A02/C-A03/C-A11 → C-M14 and C-M01; C-A06 → C-B12; C-A07/C-A08 → C-M11/C-B15; C-A04 → C-M14/C-M10;
  C-A05 → C-M04; C-A09 → C-B02/C-M12; C-A10 → C-M12/C-M15) so you build on them, not repeat them.
- Neighbouring Advanced items C-A01..C-A11 are being written IN PARALLEL right now — read their title/task in
  C.json and stay in your lane.
- Reference lesson for depth/style: ROOT/lessons/C/C-M14/ (md + demo files). Advanced md is **500–750 lines**.
- Parts: A = core implementation, B = the hard part / edge cases / "what goes wrong", C = firmware-realistic
  integration (driver, DMA descriptor ring, ISR + main loop, register map, protocol) — each with checked tests.
- Front matter: `updated: 2026-11-08`, `time:` realistic (75분–120분).
- Output filename: ROOT/lessons/C/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + starter.c + solution.c +
  practice.c (+ demo_*.c). Title line `# <ID> <title exactly as in C.json>`.
- Concurrency items (C-A02/C-A03/C-A04/C-A11): pthreads are fine in starter/solution/practice (add `-pthread`
  to every build command you show and run). Checked tests must be deterministic — a multithreaded test may
  only CHECK things that hold on every run (e.g. final counter == N*K with a correct atomic; published data
  seen intact after acquire). Racy/broken variants go in demo_*.c; show the REAL ThreadSanitizer report
  (`cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_x.c`) — TSan works on this Mac (verified). Note that
  TSan can't be combined with ASan; run solution under TSan too when it uses threads, and ASan/UBSan separately.
  Apple Silicon is weakly ordered (ARMv8) but don't promise a reorder will be *observed* — if a litmus demo
  doesn't show it in N runs, say so honestly.
- Asm comparisons (C-A02 especially, also C-A03/C-A09 if useful): show real output from both
  `cc -O2 -S` (arm64 host) and `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S -o -` on a
  freestanding demo_*.c (only <stdint.h>/<stddef.h>/<stdbool.h>/<stdatomic.h>; no libc). Explain dmb/dsb/ldar/stlr
  you actually see. Never claim an instruction appears that you didn't observe.
- C-A10 (MISRA/clang-tidy): clang-tidy is installed at `~/.venvs/clangtidy/bin/clang-tidy` (LLVM 23, pip wheel).
  On this Mac pass the SDK: `clang-tidy file.c --checks='-*,bugprone-*,cert-*,misc-*,readability-*' --
  -std=c11 -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk`. Also `cc -Weverything` (expect
  some noise like -Wdeclaration-after-statement / -Wpadded / -Wunsafe-buffer-usage — explain which to keep).
  MISRA C:2012 rule numbers: only cite ones you are confident of (e.g. 10.x essential types, 15.7/16.x switch,
  17.2 recursion, 20.7 macro params parenthesised, 21.3 no malloc, 21.6 no stdio); MISRA is paid —
  say "MISRA-style", don't quote rule text verbatim. The 300-line legacy driver lives in a separate file
  (e.g. legacy_driver.c, built only in a demo) and the md shows real tool output; starter/solution keep the
  refactor-target + equivalence tests (old vs new behaviour on many inputs, PRNG).
- C-A11 (interview code review): the 5 snippets appear in §1 for Don to review under a 15-minute timer
  (suggest he hide §4+ first). Each bug → a deterministic test that exposes it (starter has the buggy versions
  → FAIL; solution fixes them → PASS); where a bug only shows under TSan/UBSan/ASan, show that real report from
  a demo_*.c.
- Accuracy: Advanced claims are easy to get subtly wrong (memory model, signal-safety lists, aligned_alloc
  rules, container_of with const, Q15 rounding of -1×-1). Cite C11 clauses (e.g. 7.17.4 fences, 5.1.2.3,
  7.22.3.1 aligned_alloc, 7.14.1.1 signal handlers) and POSIX async-signal-safe list where relevant, and
  double-check. Where behaviour is implementation-defined (e.g. >> of negative, Q15 conversions), say so and
  write code that avoids relying on it, or document the assumption with a _Static_assert.

## Verify (you MUST run, own temp names e.g. /tmp/<ID>_s)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
    cc -std=c11 -Wall -Wextra -Werror -O2 [-pthread] starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 [-pthread] solution.c -o /tmp/<ID>_so && /tmp/<ID>_so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 [-pthread] practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer [-pthread] solution.c -o /tmp/<ID>_san && /tmp/<ID>_san
    (same for practice.c; plus -fsanitize=thread if threads are used)                                # clean
- Starter must not hang or crash (a TODO returning a dummy value must still let the harness finish).
- solution.c = starter.c with only TODO bodies filled (diff highlights changed lines).
- Self-check the md: headings verbatim & in order, ≥2 ```svg figures with viewBox (width ≤ 760), Q&A ≥6 as
  `<details><summary>Q1. ...`, 추가 연습 3–6 with `### 한 번에 검증` + `<!-- code: practice.c -->`,
  `<!-- code: starter.c -->` in §3 and `<!-- code: solution.c diff=starter.c -->` in §6.

## When done
Reply (short) with: files written, PASS counts starter/solution/practice, sanitizer results (ASan/UBSan, TSan if
used), demo files + what they showed, and anything you were unsure about. Do NOT edit files outside
ROOT/lessons/C/<your ID>/.
