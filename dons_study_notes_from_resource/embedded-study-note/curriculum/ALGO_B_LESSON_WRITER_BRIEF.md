# Lesson writer brief — track ALGO (알고리즘 · 코딩 면접), Basic level

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math,
prepping for embedded/firmware/silicon-validation interviews (Anduril, Apple, NVIDIA, Rivian, Tesla...).
He re-checks fundamentals by DOING: every lesson = one interview-style coding problem.
ALGO-B is the **coding-round** track: classic patterns implemented in C, each re-framed as a firmware /
sensor / comms / robotics situation. C Basic/Medium/Advanced and DS Basic/Medium/Advanced lessons exist —
assume he knows them and cross-reference by id ("(DS-B06 참고)") instead of re-teaching.

## Read first (required)
1. ROOT/NOTES_SPEC.md — the lesson rules. Follow EXACTLY (front matter, section headings verbatim and in order,
   svg rules, code rules, `<!-- code: ... -->` markers, `<details>` folding).
2. ROOT/curriculum/tracks/ALGO.json — find your item (title/goal/task/tags). The lesson must do the item's
   `task` completely (every sub-function and every measurement it names). Skim the other ALGO items so you stay
   in your lane: the other 14 B items are being written in parallel right now — reference them by id, don't
   depend on their files. Every .c file must be self-contained.
3. Reference lessons for depth/tone/harness: ROOT/lessons/C/C-B01/ (Basic depth, CHECK macro, "N/M passed",
   return 1 on failure) and ROOT/lessons/ALGO/ALGO-A10/ (this track's tone; Advanced, so deeper than you need).
4. Optional source for interview-pattern framing: Don's Korean AlgoMonster notes in
   /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/algomaster-coding-pattern/original-md/
   (M01 getting started/sorting, M02 binary search, M03 two pointers/sliding window, M04 DFS/recursion).
   ROOT/2026-11-07_mapping_coding_pattern.md maps those sections to ALGO ids. If a section maps to your item,
   cite it in 참고자료 as "AlgoMonster 노트 Mxx §n (Don 정리본)". Don't copy text from it.

## Output
ROOT/lessons/ALGO/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.c + solution.c + practice.c
(front matter `updated: 2026-11-11`, `time:` realistic for Basic, e.g. 40분/50분/60분).
- md title line: `# <ID> <title exactly as in ALGO.json>`.
- Section headings verbatim: ## 학습 목표, ## 1. 문제 (with ### Part A — / ### Part B — / ### Part C —),
  ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수,
  ## 8. 면접 Q&A (≥5, each `<details><summary>Q1. ...</summary>`), ## 9. 추가 연습 (3–6, answers in <details>,
  ends with `### 한 번에 검증` + `<!-- code: practice.c -->` + build cmd), ## 10. 핵심 정리, ## 참고자료.
- Size: ~400–560 lines of md.
- ≥2 inline ```svg figures (viewBox, width ≤ 760, currentColor + classes f-acc/f-ok/f-warn/f-mut only,
  no font-family except monospace for code-ish text, font-size 12–14, nothing outside the viewBox,
  escape < > & as &lt; &gt; &amp; inside svg text). Algorithm lessons live on traces: draw the invariant
  (e.g. [lo, hi) with "all < x" / "all ≥ x" zones), a step-by-step state trace, the partition / merge / window
  movement, the bit layout before/after. If the task asks for a graph of measurements (ALGO-B01 log-log),
  draw it as svg from your REAL measured numbers (state that in a caption).
- Korean, plain words, English term in parentheses on first use. First paragraph of 1. 문제: one line on why
  this matters in embedded coding interviews. Tie to firmware reality (ISR/RAM/stack budgets on Cortex-M,
  no malloc, deterministic worst case, sensor/UART/CAN/flash scenarios, Don's SSD/FTL background where it fits).

## Interview flavour (this track's special part)
- 2. Problem (English) reads like a real coding-round prompt (signature, constraints, examples with I/O).
- In 5. 풀이, include a short "면접에서 말하는 순서" box/table: clarify questions → brute force + complexity →
  better idea + invariant → code → test cases you'd say out loud → complexity (time AND extra memory / stack).
- If your problem is (a variant of) a well-known LeetCode problem, name it in 8 or 9 as "LeetCode NNN <title>"
  — ONLY numbers you are certain of; otherwise just give the title.
- 면접 Q&A = real follow-ups ("꼬리질문"): edge cases, overflow (`lo + (hi-lo)/2`, int vs size_t), stability,
  worst case, stack depth, what changes on an 8/16/32-bit MCU without a divider/FPU, ISR-safety, cache.

## Code rules (all must hold — you MUST actually run these)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # compiles, 0 warnings, some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/<ID>_so && /tmp/<ID>_so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o /tmp/<ID>_as && /tmp/<ID>_as   # clean
    (same ASan/UBSan run for practice.c)
- Use your own unique temp names (/tmp/<ID>_*) and, if you need scratch files, a PRIVATE dir /tmp/<ID>_work/ —
  14 other agents run in parallel and have clobbered shared helper files before.
- Tests must be real and deterministic: every function is checked against a brute-force / reference
  implementation (exhaustive where the task says "all lengths 0~50" / "전 범위", otherwise fixed-seed xorshift32
  random tests with enough cases to mean something) + named edge cases (empty, one element, all equal,
  duplicates, max/min values, overflow boundary). Comparisons against compiler builtins / qsort / known standard
  test vectors are fine on the TEST side. solution.c and practice.c must each run in < 2 s.
- solution.c = starter.c with ONLY the TODO bodies filled (same harness, same line structure where possible)
  so a diff highlights just the changed lines. Header comment same in both. starter.c TODO stubs must compile
  warning-free (cast unused params to (void)) and return a wrong-but-safe value (no crashes, no UB, no hangs).
- No compiler builtins inside the TODO parts unless the task asks for them.
- Measurements the task asks for (time vs n, comparison/swap counts, recursion depth, stack bytes, detection
  rate, speed vs qsort/LUT): put long-running benchmarks in demo_bench.c (or demo_*.c) in the lesson dir, not in
  solution.c. Run them, paste the REAL output into the md as tables. Timing: say it's this Mac (Apple Silicon
  M-series, -O2), use a warm-up + median of several runs, keep the compiler from deleting the work
  (volatile sink / checksum print), and say that ratios matter, not absolute ns. Never invent output.
- Stack-overflow / crash demos (ALGO-B06, ALGO-B11): run them in a subshell (`(ulimit -s 512; ./demo)`),
  capture the real signal/exit code (macOS shows SIGSEGV / SIGBUS, exit 139/138), paste it, and explain the
  Cortex-M equivalent (MSP/PSP overflow → HardFault or silent corruption without MPU guard; FreeRTOS
  configCHECK_FOR_STACK_OVERFLOW). On macOS the main-thread stack limit via ulimit -s may only take effect for
  child processes — verify what actually happens; a pthread with explicit pthread_attr_setstacksize is a
  reliable alternative. Report honestly.
- Platform: macOS arm64, Apple clang. If you show asm (`clang -O2 -S`), show arm64 and, where it matters for
  firmware, `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S` (works on this Mac for C without libc
  headers — use <stdint.h>/<stddef.h> only in that file). Never claim what an instruction does unless you saw it.
- Standard test vectors (checksums): use the published check values and cite where they come from
  (RFC 1071 example, Fletcher examples, RevEng catalogue "check" field). If you can't verify one, say so.

## Quality bar
- Parts A/B/C escalate: A = core mechanic, B = tricky variants / edge cases / boundary bugs, C = firmware-realistic
  use (sensor LUT, UART command, packet scheduler, flood fill on an occupancy grid, ADC sample sort, ...).
- 풀이 is detailed: invariant stated explicitly, step-by-step trace table on a small example, complexity
  (time, extra memory, stack depth, worst case vs average), why each design choice.
- Accuracy over volume. Double-check every complexity claim, every math formula, every C-standard claim
  (UB vs implementation-defined). Don (math major) will notice.

## When done
Reply with: files written (paths), md line count, the PASS counts for starter/solution/practice, ASan/UBSan
result, demo files + what they showed (key numbers), and anything you were unsure about. Do NOT edit files
outside ROOT/lessons/ALGO/<your ID>/ (and your private /tmp/<ID>_* paths).
