# Lesson writer brief — track HWLAB (보드 브링업 · 계측), **Basic** level (HWLAB-B01–HWLAB-B10)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering: ROOT/NOTES_SPEC.md (lesson rules) → ROOT/curriculum/LESSON_WRITER_BRIEF.md (base rules: headings verbatim
and in order, svg rules, "never invent output") → **this file** (HWLAB overrides). Read both + this.

## Who / why
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math. In the Rivian
"Staff Embedded SWE, Silicon & Sensing" pipeline (silicon + board validation) and targeting NVIDIA/Apple/Qualcomm
post-silicon / board validation roles. HWLAB lessons must read like what a board bring-up / validation engineer
actually does with a schematic, a datasheet, a scope and a bench supply — and how they *explain the numbers* in an
interview ("스코프로 무엇을 어떻게 봤고 그 수치가 무엇을 의미하는가"). Every lesson = one interview-style coding problem
that analyzes data (netlist, part JSON, synthetic or real capture CSV/VCD/.sr) with code he writes.

## Track context
- ROOT/curriculum/tracks/HWLAB.json — find your item (title/goal/task/tags/env_note). The lesson MUST do the item's
  `task` (including the planted-defect / comparison part: e.g. planted missing pull-up must be detected, naive vs
  IEEE-181 method error must be measured and shown).
- The other B items HWLAB-B01..B10 are written IN PARALLEL by other agents right now. Stay in your lane;
  cross-reference by id ("(HWLAB-B04 참고)") instead of re-teaching. Ownership: B01 KiCad netlist audit · B02 logic
  levels / noise margin / back-powering · B03 I2C pull-up window + rise time · B04 IEEE-181 waveform measurements ·
  B05 scope BW + probe + ground-lead RLC ringing · B06 trigger state machines · B07 LA capture formats + sample rate ·
  B08 rail ripple + 20MHz BW limit · B09 smoke test / golden board / inrush · B10 thermocouple CJC + Tj.
- see: tags → cite those ids (SIV, COMM, EMU, PLAT, DSP) by id; don't re-teach them. Written SIV lessons live in
  ROOT/lessons/SIV/ (SIV-B01 is the reference for a **Python** lesson: starter.py/solution.py/practice.py +
  data files, CHECK harness, `<!-- code: x.py -->` markers). C/DS/CPP lessons in ROOT/lessons/{C,DS,CPP}/.

## Code & language (HWLAB override)
- Default language: **Python 3, stdlib only** (math, statistics, random, json, csv, zipfile, struct, re, io, os, sys).
  No numpy/scipy (host has Python 3.9.6 + an ancient numpy; don't depend on it). Code must run on Python 3.9
  (no `match`, no `X | Y` type unions at runtime). Files: starter.py / solution.py / practice.py (+ data files,
  + optional demo_*.py). Run with `python3 -I <file>` from the lesson dir (resolve data paths via
  `os.path.dirname(os.path.abspath(__file__))`).
- Harness: CHECK-style function printing `PASS name` / `FAIL name: detail`, final "N/M passed", `sys.exit(1)` on any
  failure. starter.py runs (no exceptions crashing the harness — wrap TODO stubs to return None / raise
  NotImplementedError caught by the harness) and shows some FAIL, exit 1. solution.py = starter.py with only TODO
  bodies filled (same structure so the diff highlights only changed lines). practice.py = 추가 연습 answers, all PASS.
- Determinism: synthetic waveforms/captures use `random.Random(<fixed seed>)`; tests compare with explicit
  tolerances (`abs(x-ref) <= tol`) and print measured vs expected. Signal-processing pieces you need (RC/RLC step
  response, Butterworth biquad via bilinear transform, linear interpolation crossings, histogram mode) are
  implemented from the math in the lesson — that IS the learning. Explain the formulas in 풀이.
- Data files: if you generate data (CSV/VCD/.sr/.net/JSON), commit a SMALL generated file (≤ ~200 KB) and the generator
  (gen_*.py, deterministic) so it can be recreated. Large streams (e.g. B06's 10M samples) are generated in memory
  (or as a streaming generator) — don't write huge files. Keep each test run under ~20 s on this Mac
  (B06: if 10M samples in pure Python is too slow, process in chunks with a streaming state machine and show the
  measured runtime; reduce to e.g. 2M only if you must, and say so explicitly in the md with the reason).
- Real tools are OPTIONAL (not installed on this Mac: sigrok-cli, kicad-cli). Do NOT install anything. If the item's
  env_note mentions a tool, explain the real command in the md (clearly labeled "장비/도구가 있을 때") but all checked
  tests run on synthetic/committed data. Never paste output of a tool you didn't run.
- Physics/EE accuracy matters more than volume — Don's background is RF integration, he will notice. Use standard,
  defensible numbers (e.g. I2C spec values from NXP UM10204 tables, 0.35/tr for single-pole, 10–90%, IEEE 181
  histogram state levels, K-type ITS-90 coefficients from NIST — if you hard-code coefficients, take them from the
  NIST table structure accurately; if unsure of an exact constant, say so and keep the test self-consistent rather
  than inventing precision). State assumptions (single-pole vs Gaussian response, etc.).

## Depth & format
- Basic level, but audience is a senior engineer re-checking fundamentals for staff-level interviews: md
  **400–600 lines**. Front matter `updated: 2026-11-08`, `time:` realistic (50분–80분).
- Output: ROOT/lessons/HWLAB/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + starter.py/solution.py/practice.py
  (+ gen_*.py, data files, demo_*.py). Title line `# <ID> <title exactly as in HWLAB.json>`.
- Parts: A = core calculation/measurement, B = the tricky part (edge cases, measurement artifacts, why the naive
  method lies), C = bring-up-realistic scenario (named board/rail/bus, planted defect or real-ish capture, a report a
  hardware engineer can act on — e.g. `FAIL rail=VDD_1V8 Vpp=46.2mV > 30mV (BW=full); 21.7mV with 20MHz limit → PASS`).
  Each Part has checked tests.
- 풀이: explain WHY in board bring-up terms (what goes wrong on real first boards: missing pull-ups on strap pins,
  1.8V part back-powered through ESD diode, 15cm ground lead ringing mistaken for overshoot, ripple spec failed only
  because BW limit was off, CJC forgotten...). Tables (cause → symptom → measurement that reveals it). Mention the real
  bench practice (spring-tip/ground-spring, 1x vs 10x probe, differential/active probes, current probes, 4-wire
  measurement, PyVISA/SCPI) where relevant but accurately.
- 면접 Q&A ≥5 with real 꼬리질문 (e.g. "1ns 에지를 보려면 몇 MHz 스코프가 필요한가? 그 근사의 가정은?", "리플 측정에 BW limit
  을 켜는 게 정당한 경우와 아닌 경우는?").
- ≥2 inline ```svg figures (circuit sketch / waveform with annotated levels / state machine / measurement chain).
  svg rules per base brief (viewBox, width ≤ 760, currentColor + classes f-acc/f-ok/f-warn/f-mut only, font-size 12–14,
  escape &lt; &gt; &amp;).
- 1. 문제 first paragraph: one line on why this matters (first board bring-up, Rivian/NVIDIA/Apple validation interviews).
- §3 `<!-- code: starter.py -->` (and data/gen markers if useful, e.g. `<!-- code: gen_capture.py -->`),
  §6 `<!-- code: solution.py diff=starter.py -->`, §9 ends with `### 한 번에 검증` + `<!-- code: practice.py -->` + run cmd.
  Paste only real output you ran.

## Verify (you MUST run, from your lesson dir)
    python3 -I starter.py   ; echo "exit=$?"   # runs, some FAIL, exit 1
    python3 -I solution.py  ; echo "exit=$?"   # all PASS, exit 0
    python3 -I practice.py  ; echo "exit=$?"   # all PASS, exit 0
    python3 -I gen_*.py (if any) then re-run solution → data regenerates identically (diff/sha check)
    python3 -I -W error -X dev solution.py      # no warnings
  Self-check md: headings verbatim & in order (## 학습 목표, ## 1. 문제 with ### Part A — / ### Part B — /
  ### Part C —, ## 2. Problem (English), ## 3. 시작 코드, ## 4. 힌트, ## 5. 풀이, ## 6. 정답 코드, ## 7. 자주 하는 실수,
  ## 8. 면접 Q&A, ## 9. 추가 연습, ## 10. 핵심 정리, ## 참고자료); 추가 연습 3–6 with answers in <details>.

## When done
Reply (short) with: files written, PASS counts starter/solution/practice, runtime, data/demo files + what they showed,
anything you were unsure about (esp. EE constants). Do NOT edit files outside ROOT/lessons/HWLAB/<your ID>/.
