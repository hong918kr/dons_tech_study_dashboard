# Lesson writer brief — track CPP (임베디드 C++), Basic level

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (strong C, NAND/FTL, NVMe), UC Berkeley math,
prepping for embedded/firmware/silicon-validation interviews (Anduril, Apple, NVIDIA, Rivian, Tesla...).
He knows C well; C++ is the "safer C for firmware" he is now learning by DOING: every lesson = one
interview-style coding problem.

## Read first (required)
1. ROOT/NOTES_SPEC.md — the lesson rules. Follow EXACTLY (front matter, section headings verbatim and in order,
   svg rules, code rules, `<!-- code: ... -->` markers, `<details>` folding).
2. ROOT/curriculum/tracks/CPP.json — find your item (title/goal/task/tags). The lesson must do the item's `task`.
   Skim the other CPP-B items (B01–B14) — all are being written in parallel by other agents. Stay in your lane:
   cross-reference by id like "(CPP-B06 참고)" instead of re-teaching another item's topic. Also, C-track topics
   (volatile, bit ops, struct padding in C, linker sections) are owned by track C — reference "(C-Bxx 참고)".
3. ROOT/lessons/C/C-B01/ — the reference lesson (md + starter.c + solution.c + practice.c). Match its depth,
   tone, table/svg/details style, and harness (CHECK macro, "N/M passed", return 1 on failure).
   ~300–400 lines of md is the right size.

## Output
ROOT/lessons/CPP/<ID>/2026-11-07_<ID>_<english_snake_slug>.md + starter.cpp + solution.cpp + practice.cpp
(front matter `updated: 2026-11-07`, `time:` realistic e.g. 40분/50분).
- md title line: `# <ID> <title exactly as in CPP.json>`.
- Code markers use .cpp: `<!-- code: starter.cpp -->`, `<!-- code: solution.cpp diff=starter.cpp -->`,
  `<!-- code: practice.cpp -->`.
- Section headings verbatim: ## 학습 목표, ## 1. 문제 (with ### Part A — / ### Part B — / ### Part C —),
  ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수,
  ## 8. 면접 Q&A (≥5, each `<details><summary>Q1. ...</summary>`), ## 9. 추가 연습 (3–6, answers in <details>,
  ends with `### 한 번에 검증` + `<!-- code: practice.cpp -->` + build cmd), ## 10. 핵심 정리, ## 참고자료.
- ≥2 inline ```svg figures (viewBox, width ≤ 760, currentColor + classes f-acc/f-ok/f-warn/f-mut only,
  no font-family except monospace for code-ish text, font-size 12–14, nothing outside the viewBox,
  escape < > & as &lt; &gt; &amp; inside svg text).
- Korean, plain words, English term in parentheses on first use. First paragraph of 1. 문제: one line on why
  this matters in embedded/interviews. Tie to firmware reality (registers, ISR, Cortex-M, STM32/nRF,
  -fno-exceptions/-fno-rtti builds, no heap).

## Code rules (all must hold — you MUST actually run these)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    F="-std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti"
    c++ $F starter.cpp  -o /tmp/<ID>_s && /tmp/<ID>_s   # compiles, 0 warnings, some FAIL, exit 1
    c++ $F solution.cpp -o /tmp/<ID>_s && /tmp/<ID>_s   # all PASS, exit 0
    c++ $F practice.cpp -o /tmp/<ID>_p && /tmp/<ID>_p   # all PASS, exit 0
  (use your own unique temp names to avoid clashing with parallel agents; Apple clang 21, C++20 works,
  std::span / constinit / concepts available.)
- Default flags include -fno-exceptions -fno-rtti (embedded subset). If your item is ABOUT those flags
  (e.g. CPP-B13) the main harness still builds with them; put flag-comparison experiments in demo files.
- solution.cpp = starter.cpp with ONLY the TODO bodies filled (same harness, same line structure where possible)
  so a diff highlights just the changed lines. Header comment same in both.
- "Must not compile" checks (deleted copy ctor, wrong enum, write to read-only reg, non-unsigned template arg):
  prove them in the harness at compile time where possible (concepts / `requires` expressions /
  type traits in static_assert, e.g. `static_assert(!std::is_copy_constructible_v<ScopedChipSelect>)`),
  AND add a small demo_*.cpp with `#ifdef SHOW_ERROR` blocks; run it with -DSHOW_ERROR and paste the real
  (trimmed) compiler error into the md.
- If the item's task calls for asm (c++ -S / objdump -d), nm / c++filt, size, sections, multi-file linkage
  (a .c + .cpp), or flag comparisons: keep starter/solution/practice self-contained and PASSING; put the
  "observe it" experiments as extra files in the lesson dir (demo_*.cpp, hal.c/app.cpp, a small build.sh) and
  SHOW the real output you got in the md (run them!). Keep demo files small, name them clearly, mention each in
  the md with its exact build command. Also run the solution with
  `c++ -std=c++20 -g -O1 -fsanitize=address,undefined solution.cpp` when relevant and make sure it's clean.
- godbolt is not available — compare asm locally with `c++ -O2 -S -o - x.cpp` / `cc -O2 -S` (arm64) and say
  what arm-none-eabi-g++ (Cortex-M, Thumb-2) would emit similarly/differently, clearly labelled as expectation.
- Platform reality: macOS on Apple Silicon (arm64, Mach-O, clang/libc++). Sections are __TEXT,__text /
  __TEXT,__const / __DATA,__data / __DATA,__bss / __DATA,__mod_init_func (≈ .init_array) / __TEXT,__eh_frame /
  __TEXT,__gcc_except_tab / __TEXT,__unwind_info; mangled names get an extra leading underscore on Mach-O
  (`__Z3fooi`). Show the real Mac output you observed AND explain the ELF / arm-none-eabi-g++ equivalent
  (.text/.rodata/.data/.bss/.init_array/.ARM.exidx/.ARM.extab) — that's what firmware uses.
  Never invent output — paste what you ran; if you state ELF/ARM behaviour you couldn't run, say so.
- Mock hardware: simulate registers with a static `uint32_t` array / struct and pointers to it (no real MMIO).

## Quality bar
- Problem in Parts A/B/C escalates: A = core mechanic, B = tricky/edge cases, C = firmware-realistic use
  (mock register block / driver / ISR-ish scenario) like C-B01's GPIO.
- 풀이 is detailed: tables, step-by-step reasoning, figures; explain WHY (cite C++ standard sections like
  [dcl.constexpr], [class.mem], [basic.start.static] or cppreference where useful).
- Compare to the C way Don already knows (what the C++ feature replaces, and its runtime/code-size cost).
- 면접 Q&A: real interview-style follow-ups ("꼬리질문"), concise but precise answers.
- Accuracy matters more than volume. Double-check every C++ standard claim (C++17 vs C++20 availability, UB vs
  implementation-defined, what the compiler actually guarantees). Don will notice.

## When done
Reply with: files written (paths), the PASS counts for starter/solution/practice, demo files + what they showed,
and anything you were unsure about. Do NOT edit files outside lessons/CPP/<your ID>/.
