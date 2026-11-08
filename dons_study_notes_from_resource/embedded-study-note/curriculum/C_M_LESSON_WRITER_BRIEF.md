# Lesson writer brief — track C (임베디드 C 핵심), **Medium** level (C-M01–C-M15)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

This is the Basic brief (ROOT/curriculum/LESSON_WRITER_BRIEF.md) adapted for Medium. Read that file too —
everything in it applies (output files, section headings, svg rules, code rules, platform reality, "never invent
output") EXCEPT where this file overrides it.

## Overrides for Medium
- Level: Medium = a real interview coding question at mid/senior firmware level. Assume the Basic C items are
  known and cross-reference them instead of re-teaching: C-B01 bit ops, C-B02 integer promotion, C-B03 shifts,
  C-B04 GET/SET_FIELD macros, C-B05 pointers, C-B06 array decay, C-B07 strlen/strcpy, C-B08 const,
  C-B09 volatile, C-B10 static/linkage, C-B11 memory sections, C-B12 padding/offsetof, C-B13 endian/byte swap,
  C-B14 precedence/eval order, C-B15 macro pitfalls. (All are written: ROOT/lessons/C/C-Bxx/.)
- Neighbouring Medium items C-M01..C-M15 are being written IN PARALLEL by other agents right now — read their
  title/task in C.json and stay in your lane; cross-reference by id ("(C-M02 참고)") rather than duplicating.
- Reference lessons: ROOT/lessons/C/C-B01/ (structure/harness) and ROOT/lessons/C/C-B09/ (a deeper one with
  demo files). Match depth and style; Medium md is typically **450–650 lines**.
- Parts escalate: A = core implementation, B = edge cases / the hard part interviewers probe,
  C = firmware-realistic scenario (mock register block, driver, ISR, UART logger, protocol packet...).
- Tests should be substantial: table-driven cases, boundary values; where the task says "fuzz / hundreds of
  cases / compare with libc", do it with a deterministic PRNG (fixed seed, e.g. xorshift32) so runs are
  reproducible. Never put timing or benchmark numbers inside CHECKs — benchmarks go in a demo_bench.c and the
  md shows the real numbers you measured (state machine: Apple Silicon, clang version from `cc --version`).
- Front matter: `updated: 2026-11-07`, `time:` realistic (60분–90분 for Medium).
- Output filename: ROOT/lessons/C/<ID>/2026-11-07_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
  (+ demo_*.c as needed). Title line `# <ID> <title exactly as in C.json>`.
- Cross-target observations (asm, layout on Cortex-M): this Mac has no arm-none-eabi-gcc, but
  `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S file.c -o -` works (freestanding headers
  like <stdint.h>/<stddef.h>/<stdbool.h> only — no <stdio.h>/<string.h> on that target, so put target-only code in a
  small demo_*.c that includes nothing else). Show the real asm you got and explain it. `xcrun llvm-objdump` /
  `xcrun llvm-size` exist; /usr/local/bin/llvm-size is ancient — don't use it.
- POSIX in strict C11 (C-M14 etc.): put `#define _POSIX_C_SOURCE 200809L` (or `_DARWIN_C_SOURCE` if needed)
  before any #include so sigaction/sigprocmask/setitimer are declared under -std=c11. Tests must be
  deterministic: don't rely on a race actually happening for PASS/FAIL; make the race-reproduction a demo with
  printed observations, and make the checked tests deterministic (e.g. raise() the signal at controlled points,
  or call the "ISR" function at an injected point via a hook).
- UB demos (C-M12/C-M13 etc.): starter/solution/practice stay UB-free and pass strict build + ASan/UBSan;
  the UB reproductions live in demo_*.c, built WITHOUT -Werror if they must warn, and the md shows the
  real -O0 vs -O2 output and sanitizer messages you got. If clang on this Mac does NOT reproduce a classic
  "optimizer breaks it" result, say so honestly and show what it did instead (and, if you can, what
  `--target=thumbv7em-none-eabi -S` emits).

## Verify (you MUST run, own temp names e.g. /tmp/<ID>_s)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # builds, some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/<ID>_so && /tmp/<ID>_so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/<ID>_san && /tmp/<ID>_san
    (same sanitizer run for practice.c)                                                   # clean
- solution.c = starter.c with only TODO bodies filled (diff highlights changed lines).
- Self-check the md: headings verbatim & in order, ≥2 ```svg figures with viewBox (width ≤ 760), Q&A ≥5 as
  `<details><summary>Q1. ...`, 추가 연습 3–6 with `### 한 번에 검증` + `<!-- code: practice.c -->`,
  `<!-- code: starter.c -->` in §3 and `<!-- code: solution.c diff=starter.c -->` in §6.

## When done
Reply (short) with: files written, PASS counts starter/solution/practice, sanitizer result, demo files + what
they showed, and anything you were unsure about. Do NOT edit files outside ROOT/lessons/C/<your ID>/.
