# Lesson writer brief — track DS (임베디드 자료구조), Medium level

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math,
prepping for embedded/firmware/silicon-validation interviews (Anduril, Apple, NVIDIA, Rivian, Tesla...).
He re-checks fundamentals by DOING: every lesson = one interview-style coding problem.
This is the **Medium** level: Basic (DS-B01–B14) is done; assume he knows those and build on them.

## Read first (required)
1. ROOT/NOTES_SPEC.md — the lesson rules. Follow EXACTLY (front matter, section headings verbatim and in order,
   svg rules, code rules, `<!-- code: ... -->` markers, `<details>` folding).
2. ROOT/curriculum/tracks/DS.json — find your item (title/goal/task/tags). The lesson must do the item's `task`.
   Skim the other items so you stay in your lane. DS-B01–B14 lessons exist in ROOT/lessons/DS/ — cross-reference
   them by id ("(DS-B06 참고)") instead of re-teaching. Other DS-M items are being written in parallel right now:
   reference them by id but don't depend on their files — every .c file must be self-contained (if you need an
   intrusive list or a hash, write a minimal one inline and say "DS-M01에서 자세히").
   `see:<TRACK>` tags point at the owner track elsewhere (OS, RTOS, ALGO, EML) — mention the link in one line.
3. Reference lessons for depth/tone/harness: ROOT/lessons/DS/DS-B06/ and ROOT/lessons/DS/DS-B12/
   (md + starter.c + solution.c + practice.c; CHECK macro, "N/M passed", return 1 on failure).
   Medium lessons should be a bit deeper than Basic: ~420–560 lines of md.

## Output
ROOT/lessons/DS/<ID>/2026-11-07_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
(front matter `updated: 2026-11-07`, `time:` realistic for Medium, e.g. 60분/75분/90분).
- md title line: `# <ID> <title exactly as in DS.json>`.
- Section headings verbatim: ## 학습 목표, ## 1. 문제 (with ### Part A — / ### Part B — / ### Part C —),
  ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수,
  ## 8. 면접 Q&A (≥5, each `<details><summary>Q1. ...</summary>`), ## 9. 추가 연습 (3–6, answers in <details>,
  ends with `### 한 번에 검증` + `<!-- code: practice.c -->` + build cmd), ## 10. 핵심 정리, ## 참고자료.
- ≥2 inline ```svg figures (viewBox, width ≤ 760, currentColor + classes f-acc/f-ok/f-warn/f-mut only,
  no font-family except monospace for code-ish text, font-size 12–14, nothing outside the viewBox,
  escape < > & as &lt; &gt; &amp; inside svg text). Data-structure lessons live on pictures: show the
  structure's layout and the before/after of the key operation (rotation, split/merge, unlink, wrap, ...).
- Korean, plain words, English term in parentheses on first use. First paragraph of 1. 문제: one line on why
  this matters in embedded/interviews. Tie to firmware reality (no malloc, ISR/critical sections, Cortex-M RAM
  budgets, RTOS kernels, Linux kernel, SSD/FTL where it fits — Don's background).

## Code rules (all must hold — you MUST actually run these)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # compiles, 0 warnings, some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/<ID>_so && /tmp/<ID>_so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o /tmp/<ID>_as && /tmp/<ID>_as   # clean
    (same ASan/UBSan run for practice.c)
- Use your own unique temp names (/tmp/<ID>_*) — 13 other agents run in parallel.
- No malloc in the data structure itself (static storage / caller-provided buffers), unless the task explicitly
  compares against malloc (then malloc only in the benchmark/test side).
- Tests must be real: invariant-check function run after every op + randomized test against a simple reference
  model (fixed-seed PRNG like xorshift32 so output is deterministic) + named edge cases. Stress counts large
  enough to mean something but the whole program must run in < 2 s.
- solution.c = starter.c with ONLY the TODO bodies filled (same harness, same line structure where possible)
  so a diff highlights just the changed lines. Header comment same in both. starter.c TODO stubs must compile
  warning-free (cast unused params to (void)) and return a wrong-but-safe value (no crashes, no UB).
- Measurements the task asks for (hit rate, probe lengths, fragmentation, false-positive rate, timing vs malloc,
  height vs n, memory bytes): print them from a program (solution.c or a demo_*.c in the lesson dir), run it,
  and paste the REAL output into the md as a table. Timing numbers: say it's this Mac (Apple Silicon, -O2) and
  that ratios matter, not absolute ns. Never invent output.
- Extra observe-files (demo_*.c) are fine; mention each in the md with its build command and its real output.
- Platform: macOS arm64, Apple clang. `__builtin_clz/ctz` OK where the task calls for CLZ/CTZ (note the
  arg==0 UB and Cortex-M `CLZ` instruction / `__CLZ()` CMSIS); keep a portable fallback where sensible.

## Quality bar
- Parts A/B/C escalate: A = core mechanic, B = tricky ops/edge cases (deletion, wrap, resize, cancel, merge),
  C = firmware-realistic use (RTOS scheduler, FTL cache, driver timer, packet router, trace logger, ...).
- 풀이 is detailed: invariants stated explicitly, step-by-step traces with tables, complexity (time AND worst-case
  / determinism, memory overhead per element), figures; explain WHY design choices are made.
- 면접 Q&A: real interview follow-ups ("꼬리질문") — complexity, worst cases, concurrency/ISR safety, how Linux /
  FreeRTOS / Zephyr do it (only claims you are sure of; name the file/function if you cite it), trade-offs vs
  alternatives.
- Accuracy over volume. Double-check every C-standard claim and every math formula. Don will notice.

## When done
Reply with: files written (paths), md line count, the PASS counts for starter/solution/practice, ASan/UBSan
result, demo files + what they showed, and anything you were unsure about. Do NOT edit files outside
ROOT/lessons/DS/<your ID>/.
