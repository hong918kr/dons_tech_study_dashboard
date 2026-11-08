# Lesson writer brief — track C (임베디드 C 핵심), Basic level

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math,
prepping for embedded/firmware/silicon-validation interviews (Anduril, Apple, NVIDIA, Rivian, Tesla...).
He re-checks fundamentals by DOING: every lesson = one interview-style coding problem.

## Read first (required)
1. ROOT/NOTES_SPEC.md — the lesson rules. Follow EXACTLY (front matter, section headings verbatim and in order,
   svg rules, code rules, `<!-- code: ... -->` markers, `<details>` folding).
2. ROOT/curriculum/tracks/C.json — find your item (title/goal/task/tags). The lesson must do the item's `task`.
   Also skim neighbouring B items so you don't duplicate them (C-B01 bit set/clear/field, C-B02 integer promotion
   are already written; other B items are being written in parallel by other agents — stay in your lane,
   cross-reference by id like "(C-B03 참고)" instead of re-teaching).
3. ROOT/lessons/C/C-B01/ — the reference lesson (md + starter.c + solution.c + practice.c). Match its depth,
   tone, table/svg/details style, and harness (CHECK macro, "N/M passed", return 1 on failure).
   ~300–380 lines of md is the right size.

## Output
ROOT/lessons/C/<ID>/2026-11-07_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
(front matter `updated: 2026-11-07`, `time:` realistic e.g. 40분/50분).
- md title line: `# <ID> <title exactly as in C.json>`.
- Section headings verbatim: ## 학습 목표, ## 1. 문제 (with ### Part A — / ### Part B — / ### Part C —),
  ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수,
  ## 8. 면접 Q&A (≥5, each `<details><summary>Q1. ...</summary>`), ## 9. 추가 연습 (3–6, answers in <details>,
  ends with `### 한 번에 검증` + `<!-- code: practice.c -->` + build cmd), ## 10. 핵심 정리, ## 참고자료.
- ≥2 inline ```svg figures (viewBox, width ≤ 760, currentColor + classes f-acc/f-ok/f-warn/f-mut only,
  no font-family except monospace for code-ish text, font-size 12–14, nothing outside the viewBox,
  escape < > & as &lt; &gt; &amp; inside svg text).
- Korean, plain words, English term in parentheses on first use. First paragraph of 1. 문제: one line on why
  this matters in embedded/interviews. Tie to firmware reality (registers, ISR, Cortex-M, STM32/nRF).

## Code rules (all must hold — you MUST actually run these)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/x && ./x   # compiles, 0 warnings, some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/x && ./x   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/x && ./x   # all PASS, exit 0
  (use your own unique temp names, e.g. /tmp/<ID>_s, to avoid clashing with parallel agents)
- solution.c = starter.c with ONLY the TODO bodies filled (same harness, same line structure where possible)
  so a diff highlights just the changed lines. Header comment same in both.
- If the item's task calls for observing UB, sanitizer output, warnings, asm (clang -S), nm/size, or
  multi-file linkage: keep starter/solution/practice as self-contained, warning-free, UB-free programs that
  PASS under the strict command above; put the "observe it" experiments as extra files in the lesson dir
  (e.g. demo_ub.c, quiz.c, a.c/b.c/main.c, Makefile-less shell commands shown in the md) and SHOW the real
  output you got in the md (run them!). Also run the solution with
  `cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c` when relevant and make sure it's clean.
  Keep demo files small; name them clearly; mention each one in the md with its build command.
- Platform reality: this is macOS on Apple Silicon (arm64, Mach-O, clang). nm/size/otool output, section names
  (__TEXT,__text / __TEXT,__const / __DATA,__data / __DATA,__bss / __DATA,__common), and `size -m` differ from
  Linux ELF (.text/.rodata/.data/.bss). Show the real Mac output you observed AND explain the ELF/arm-none-eabi
  equivalent (that's what firmware uses). Never invent output — paste what you ran.
- No compiler builtins in the TODO parts unless the task asks for them (comparisons against builtins in tests ok).

## Quality bar
- Problem in Parts A/B/C escalates: A = core mechanic, B = tricky/edge cases, C = firmware-realistic use
  (fake register struct / driver / ISR-ish scenario) like C-B01's GPIO.
- 풀이 is detailed: tables, step-by-step reasoning, figures; explain WHY (C11 clause numbers where useful).
- 면접 Q&A: real interview-style follow-ups ("꼬리질문"), concise but precise answers.
- Accuracy matters more than volume. Double-check every C-standard claim (UB vs implementation-defined vs
  unspecified). Don will notice.

## When done
Reply with: files written (paths), the PASS counts for starter/solution/practice, demo files + what they showed,
and anything you were unsure about. Do NOT edit files outside lessons/C/<your ID>/.
