# Lesson writer brief — track SIV (실리콘 검증 · Post-silicon Validation), **Basic** level (SIV-B01–SIV-B14)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering: ROOT/NOTES_SPEC.md (lesson rules) → ROOT/curriculum/LESSON_WRITER_BRIEF.md (base rules: output files,
headings verbatim and in order, svg rules, strict build, "never invent output") → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md
(deterministic PRNG tests, ASan/UBSan, demo_*.c, POSIX define) → **this file** (SIV overrides). Read all of them.

## Who / why
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math. He is in the Rivian
"Staff Embedded SWE, Silicon & Sensing" pipeline (silicon + board validation written in C; HM screen asked PCIe
lspci/LTSSM and a C nibble-reverse) and targets NVIDIA/Apple/Qualcomm post-silicon validation roles. SIV lessons must
read like *what a validation engineer actually writes on day 1 of first silicon*: register-description-driven tests,
mock MMIO backends, fault injection, precise failure reports. Every lesson = one interview-style coding problem.

## Track context
- ROOT/curriculum/tracks/SIV.json — find your item (title/goal/task/tags). The lesson MUST do the item's `task`
  (including the fault-injection part: inject the named defects into the mock and show the test catches them).
- The other B items SIV-B01..B14 are being written IN PARALLEL by other agents right now. Read their title/task and
  stay in your lane; cross-reference by id ("(SIV-B02 참고)") instead of re-teaching.
  Rough ownership: B01 register-description JSON + codegen + lint · B02 mock MMIO + access-type semantics + bus ops ·
  B03 reset-value test · B04 R/W patterns per access type · B05 walking 1s/0s + stuck-at/short · B06 address
  decode/aliasing · B07 March C- SRAM · B08 TEST() framework + JSON log · B09 poll-with-timeout · B10 IP bring-up
  sequence · B11 interrupt trigger-check-clear · B12 test plan (I2C) · B13 pre- vs post-silicon overview ·
  B14 log → report + exit code.
- Related tracks you may cite by id (see: tags): BM-B02 (mock peripheral read/write hooks), C-M01 (MMIO volatile /
  register struct), C-M02 (RMW, W1C), C-B04 (GET/SET_FIELD macros), C-M06 (function-pointer tables),
  TEST-B01 (mini unit test framework), MEM (memory fault models), PCIE, EMU. Lessons already written live in
  ROOT/lessons/C/, ROOT/lessons/DS/, ROOT/lessons/CPP/ — skim ROOT/lessons/C/C-M01/ and C-M02/ for the house style
  of register mocks; ROOT/lessons/C/C-B01/ is the structural reference.

## Self-contained code (important)
Each lesson directory must build on its own: do NOT #include files from another lesson. If your item builds on
B01/B02 (register description, mock register file), embed a *small* version of what you need inside your own
starter/solution (e.g. a static `const reg_desc_t regs[]` table + a tiny mock with access types). Keep it
compact and say in the md "SIV-B02의 mock을 축약해 넣었다".

## Code & language
- Default: C. starter.c / solution.c / practice.c, `cc -std=c11 -Wall -Wextra -Werror -O2`, CHECK harness,
  "N/M passed", exit 1 on failure. solution.c = starter.c with only TODO bodies filled (diff highlights them).
- Mock register file: a host array (uint32_t regs[N]) behind `reg_read32(off)/reg_write32(off,val)` or bus-ops
  function pointers; access semantics (RW/RO/WO/W1C/W1S/RC) applied in the mock's write/read path; fault injection
  via a small config struct (e.g. stuck mask, ignored address bit, wrong reset value) so tests are deterministic.
- Failure reports should look real: `FAIL reg=CTRL off=0x004 exp=0x00000001 act=0x00000000 diff=0x00000001 bits=[0]`.
- Tests must show BOTH directions: the good mock passes; each injected defect is detected (CHECK that the test
  returned failure / the right register & bit were reported).
- **SIV-B01 only (Python codegen)**: the generator is Python 3 stdlib (json, no PyYAML). Use starter.py / solution.py /
  practice.py (python fixed names) + a register description `regmap.json` (+ a deliberately broken `bad_*.json` or two
  for the lint). The python solution must generate a C header and the lesson must ALSO compile a tiny C check
  (e.g. `check_header.c` that includes the generated header, `_Static_assert`s a few values, and prints PASS) with the
  strict cc command — run it and show output. Python harness: same CHECK style printing PASS/FAIL and "N/M passed",
  `sys.exit(1)` on failure. Run with `python3 -I solution.py`.
- **SIV-B08 (framework)**: Mach-O (this Mac) has no `__start_section/__stop_section`; ELF/GNU ld does. Use the
  `__attribute__((constructor))` registration in starter/solution/practice (portable to clang/gcc), and explain the
  linker-section approach (ELF `__start_<sec>`, Mach-O `getsectiondata` / `section("__DATA,__siv_tests")`) in the
  md; if you show a linker-section demo, put it in demo_*.c and show real output you ran.
- **SIV-B09 (timeout)**: no real sleeping in checked tests — inject a fake clock (`now_us()` function pointer that the
  mock advances per read) so timeouts are deterministic. Show the real-time version (clock_gettime CLOCK_MONOTONIC)
  only as a demo or code snippet.
- **SIV-B12 / SIV-B13 (planning/overview)**: still a coding problem — the plan/classification tables live in the md,
  and the code implements the 3 test cases (B12) / the backend-swap skeleton (B13) with checked tests.
- **SIV-B14 (report)**: write the report generator in C (parse the simple line-per-record JSON log with a small
  hand-written parser — keep the log format flat: one JSON object per line) OR Python if you justify it; the md shows
  the real Markdown/text report and exit codes you got.

## Depth & format
- Level Basic, but the audience is a senior engineer re-checking fundamentals for a staff-level interview: md
  **400–600 lines**. Front matter `updated: 2026-11-08`, `time:` realistic (50분–80분).
- Output: ROOT/lessons/SIV/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + starter/solution/practice (+ demo_*.c,
  data files). Title line `# <ID> <title exactly as in SIV.json>`.
- Parts: A = core test/mechanism, B = the tricky part (access-type corner cases, masks, undefined/volatile bits,
  fault model coverage, timeouts), C = validation-realistic scenario (named IP block, fault injection run, report
  that a debug engineer can act on). Each Part has checked tests.
- 풀이: explain WHY each check exists in real silicon validation (what bug it caught historically, e.g. reset-value
  mismatches from wrong tie-offs, W1C implemented as RW, address decoder ignoring a bit, clock-gated block reads
  0xDEADBEEF / bus error). Tables: fault model → which pattern detects it. Tie to real practice: SystemRDL/IP-XACT,
  UVM RAL `uvm_reg_hw_reset_seq` / `uvm_reg_bit_bash_seq` / `uvm_reg_access_seq` / `uvm_mem_walk_seq` (pre-silicon
  analogues), post-silicon C tests run via JTAG/OpenOCD or on-chip firmware, ATE vs SLT vs system validation.
  Be accurate: if unsure of a standard's detail, say it less specifically rather than invent it.
- 면접 Q&A ≥5 with real 꼬리질문 (e.g. "RC 레지스터는 리셋값 테스트에서 어떻게 다루나?", "bit-bash가 W1C를 망가뜨리지 않으려면?",
  "test가 timeout 났을 때 어떤 정보를 로그에 남기나?").
- ≥2 inline ```svg figures (register layout / bus+decoder / test flow / fault matrix). svg rules per base brief.
- 1. 문제 first paragraph: one line on why this matters (first-silicon bring-up, Rivian/NVIDIA validation interviews).

## Verify (you MUST run; own temp names e.g. /tmp/<ID>_st)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # builds, some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/<ID>_so && /tmp/<ID>_so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/<ID>_san && /tmp/<ID>_san
    (same sanitizer run for practice.c)                                                   # clean
  (SIV-B01: python3 -I starter.py → FAIL+exit 1; solution.py / practice.py → PASS exit 0; plus the C header check.)
- Self-check the md: headings verbatim & in order (## 학습 목표, ## 1. 문제 with ### Part A — / ### Part B — /
  ### Part C —, ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수,
  ## 8. 면접 Q&A, ## 9. 추가 연습, ## 10. 핵심 정리, ## 참고자료); `<!-- code: starter.c -->` in §3,
  `<!-- code: solution.c diff=starter.c -->` in §6, 추가 연습 3–6 with answers in <details>, ending with
  `### 한 번에 검증` + `<!-- code: practice.c -->` + build cmd. Paste only real output you ran.

## When done
Reply (short) with: files written, PASS counts starter/solution/practice, sanitizer result, demo/data files + what
they showed, anything you were unsure about. Do NOT edit files outside ROOT/lessons/SIV/<your ID>/.
