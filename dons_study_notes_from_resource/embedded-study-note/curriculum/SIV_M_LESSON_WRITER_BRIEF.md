# Lesson writer brief — track SIV (실리콘 검증 · Post-silicon Validation), **Medium** level (SIV-M01–SIV-M14)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering: ROOT/NOTES_SPEC.md → ROOT/curriculum/LESSON_WRITER_BRIEF.md (base: headings verbatim & in order, svg rules,
"never invent output") → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md (Medium: deterministic PRNG tests, demo_*.c,
benchmarks only in demos, POSIX define) → ROOT/curriculum/SIV_B_LESSON_WRITER_BRIEF.md (SIV: who/why, mock MMIO,
fault injection, FAIL report format, self-contained code) → **this file** (SIV Medium overrides). Read all of them.
For Python lessons also read ROOT/curriculum/HWLAB_B_LESSON_WRITER_BRIEF.md "Code & language" (Python 3.9 stdlib rules).

## Overrides for Medium
- SIV Basic B01–B14 are ALL written (ROOT/lessons/SIV/SIV-Bxx/). Assume them known; cross-reference by id instead of
  re-teaching: B01 regmap JSON/codegen · B02 mock MMIO + access types · B03 reset test · B04 R/W per access type ·
  B05 walking 1/0 · B06 aliasing · B07 March C- · B08 TEST() framework + JSON log · B09 poll-with-timeout (fake clock) ·
  B10 bring-up sequence · B11 interrupt trigger-check-clear · B12 test plan · B13 pre vs post silicon · B14 report.
  Skim the md of the ones your item builds on (e.g. M02/M14 → B02/B09/B11, M03 → B11, M10 → B14, M12 → B08/B14).
- Neighbour Medium items SIV-M01..M14 are being written IN PARALLEL right now. Read their title/task in SIV.json and
  stay in your lane. Ownership: M01 ATE/SLT/system classification · M02 DMA SG descriptors + CRC32 · M03 IRQ
  latency/lost/coalesce · M04 PCIe config-space/LTSSM checker · M05 DDR training window + stress patterns ·
  M06 PRBS + BER confidence · M07 shmoo + Vmin search · M08 PVT schedule (Python) · M09 concurrent stress (pthread) ·
  M10 triage clustering (Python) · M11 bisect (commit + parameter, flaky) · M12 Python sequencer + ctypes ·
  M13 functional coverage (Python) · M14 ECC inject + CE/UE.
- Other tracks: cite by id from see: tags (PCIE, MEM, TEST, ARCH...). Those lessons are mostly NOT written yet, so
  code stays self-contained (e.g. M14 embeds its own small SECDED(72,64) or (39,32) codec — say "MEM-B07의 코덱을
  축약해 넣었다"). Do not #include from other lesson dirs.
- Level: Medium = a real interview / day-to-day validation-engineer problem at senior level. md **450–650 lines**,
  front matter `updated: 2026-11-08`, `time:` 70분–100분. Q&A ≥6 with real 꼬리질문.
- Parts: A = core mechanism, B = the hard part interviewers probe (boundaries, statistics, flaky/intermittent,
  races, corner cases), C = validation-realistic scenario (named IP / board / regression night, planted defect(s)
  detected, a report a debug engineer can act on). Each Part has checked tests.
- Tests: table-driven + deterministic random (xorshift32/64 fixed seed in C; `random.Random(seed)` in Python).
  Planted defects MUST be detected (CHECK that the test reports failure with the right location/signature), and the
  good model MUST pass. No timing numbers inside CHECKs (measurements → demo + md table with real numbers).
- Accuracy over volume: Don has an RF/SSD background and was asked PCIe lspci/LTSSM in a Rivian HM screen. If you
  state a spec detail (PCIe capability IDs, Link Status bit layout, LTSSM states, JEDEC training terms, PRBS
  polynomials, BER confidence formula, CRC32 polynomial) it must be correct; if unsure, say it less specifically.
  Derive math (e.g. BER: N ≥ -ln(1-CL)/BER for zero errors → ≈3.0e12 bits for 1e-12 @95%) in 풀이.

## Language per item
- C (`cc -std=c11 -Wall -Wextra -Werror -O2`; starter.c/solution.c/practice.c): M01, M02, M03, M04, M05, M06, M07,
  M09, M14.
  - M01: still a coding problem — encode the 20 defect scenarios as data (attributes: defect class, needs at-speed,
    needs thermal, needs full system/SW, intermittent...), implement the stage classifier + a test-time/cost/coverage
    calculator, CHECK against the expected table in the md; the 1-page 설계 메모 lives in the md (§5 or Part C).
  - M04: parse an `lspci -xxx`-style hex text dump (commit 2–3 small .txt dumps: good Gen4 x4, downgraded Gen1 x4 /
    Gen4 x1, broken capability chain loop) — walk the capability list from 0x34 with loop/bounds protection, find
    PCIe cap (ID 0x10), decode Link Capabilities / Link Status speed+width. LTSSM is a mock register sequence with a
    fake clock (SIV-B09 style).
  - M06: PRBS7 (x^7+x^6+1), PRBS15 (x^15+x^14+1), PRBS31 (x^31+x^28+1) — verify period for 7/15 in tests; checker
    self-synchronizes (seed from received bits); BER-time calculator for e.g. 10/25/56/112 Gb/s. Use <math.h> (-lm ok).
  - M09: pthreads, `#define _POSIX_C_SOURCE 200809L` (or _DARWIN_C_SOURCE). Checked tests must be deterministic: the
    good harness passes, the deliberately racy variant is a compile-time/run-time switch whose detection is shown in a
    demo (and/or TSan) with real output — or detected deterministically via an injected hook. Run TSan:
    `cc -std=c11 -g -O1 -fsanitize=thread solution.c -o /tmp/<ID>_tsan && /tmp/<ID>_tsan` must be clean for the good
    harness (TSan exits 134 on races on this Mac). Keep total runtime of checked tests < 10 s ("장시간 실행" = a CLI flag
    in a demo, explained in md).
  - M05/M07: buffers and sweeps sized so each checked run is < 5 s.
- Python 3.9 stdlib only (starter.py/solution.py/practice.py, `python3 -I`, no PyYAML/numpy): M08, M10, M13.
  - M10: deterministic `gen_logs.py` → commit a small JSONL (≤ 200 KB, a few thousand records) + planted correlations
    (one board, one temperature threshold, one firmware version) that the solution must recover.
- M11: Python 3 stdlib (starter.py/solution.py/practice.py). Part A builds a throwaway git repo in a
  `tempfile.TemporaryDirectory()` (commits a tiny C or Python program, plants a regression at a known commit, plus a
  flaky test), runs real `git bisect run` with a retry/majority wrapper script, CHECKs the found commit. Set
  `GIT_CONFIG_GLOBAL=/dev/null`, author env vars, `-c init.defaultBranch=main` so it never touches Don's git config.
  Part B = generic parameter bisect (monotonic predicate) + flaky model showing naive bisect gives wrong answer at a
  measured rate (deterministic seeds), and the improved version (repeat/majority or confidence-based) fixes it.
- M12: Python 3 stdlib + a C test library. YAML: PyYAML is NOT installed — implement a minimal YAML-subset parser
  (list of mappings, scalars, nested `params:` mapping; document the subset) OR accept a `.yaml` restricted subset;
  say why in md. Build the C side as a dylib from the lesson dir: `cc -std=c11 -Wall -Wextra -Werror -O2 -dynamiclib
  siv_tests.c -o /tmp/<ID>_siv_tests.dylib` (the python harness builds it into a tempdir itself via subprocess, so
  `python3 -I solution.py` alone is the full test). Mock power-supply/chamber classes, SQLite results DB (in-memory
  or tempdir), timeouts via subprocess timeout for the binary-runner path, retries, summary report. Strict build of
  the .c too.
- M13: coverpoints/bins/cross in Python; sampling from a sequencer-like JSONL log you generate deterministically;
  hole report; convergence curve as an ASCII plot + the real numbers table in md.

## Verify (you MUST run; own temp names /tmp/<ID>_*)
C items:
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/<ID>_st && /tmp/<ID>_st   # builds, some FAIL, exit 1
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/<ID>_so && /tmp/<ID>_so   # all PASS, exit 0
    cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/<ID>_pr && /tmp/<ID>_pr   # all PASS, exit 0
    cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/<ID>_san && /tmp/<ID>_san
    (same for practice.c; add -lm if you use math.h; M09 also TSan as above)          # clean
  If solution/practice read data files, run them from the lesson dir and resolve paths relative to it (document cwd).
Python items (from lesson dir):
    python3 -I starter.py; echo exit=$?   # some FAIL, exit 1
    python3 -I solution.py; echo exit=$?  # all PASS, exit 0
    python3 -I practice.py; echo exit=$?  # all PASS, exit 0
    python3 -I -W error -X dev solution.py   # no warnings
    (gen_*.py regenerates identical data — sha check)
- solution = starter with only TODO bodies filled (diff highlights them).
- md self-check: headings verbatim & in order (## 학습 목표, ## 1. 문제 with ### Part A — / ### Part B — / ### Part C —,
  ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수, ## 8. 면접 Q&A,
  ## 9. 추가 연습, ## 10. 핵심 정리, ## 참고자료); ≥2 ```svg with viewBox (width ≤ 760); Q&A as
  `<details><summary>Q1. ...`; `<!-- code: starter.X -->` in §3, `<!-- code: solution.X diff=starter.X -->` in §6,
  추가 연습 3–6 with answers in <details>, ending `### 한 번에 검증` + `<!-- code: practice.X -->` + run cmd.
  Paste only real output you ran. Never put `## ` lines inside code fences if avoidable.

## Output
ROOT/lessons/SIV/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + code/data files. Title `# <ID> <title exactly as in SIV.json>`.

## When done
Reply (short): files written, PASS counts starter/solution/practice, sanitizer/TSan result, demo/data files + what they
showed, anything you were unsure about (spec details). Do NOT edit files outside ROOT/lessons/SIV/<your ID>/.
