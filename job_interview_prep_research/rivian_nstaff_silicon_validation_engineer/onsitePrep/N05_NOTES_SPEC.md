# N05 paper-drill spec (writer agents read this first)

Owner: Don (Rivian Staff Embedded SWE, Silicon & Sensing validation — onsite prep). The onsite has a C coding round (silicon + board validation, mostly bare-metal C, logic-focused; RTL not needed). Screen question was "reverse nibble order of 0x1234567890abcdef". Don is an ex-SSD-controller firmware engineer (Solidigm Staff, SK hynix), now Apple wireless integration. He knows C but gets slow under pressure: naming, trace units, forgetting tests/extensions.

Index: `2026-11-01_N05_c_paper_drill_bank.md` — §2 lists every page and every problem ID/title/핵심. Implement EXACTLY those IDs and titles (you may sharpen the wording, but not drop or renumber). Don't edit the index or other pages.

## Goal of every problem
Solvable **on paper in ≤20 minutes** (no compiler). So: one function (or 2 small ones), ≤ ~40 lines of C in the model answer, no libraries beyond `<stdint.h> <stddef.h> <stdbool.h> <string.h>`. Hardware is simulated by plain variables / arrays / function stubs (e.g. `extern uint32_t reg_read(uint32_t off)` or a `volatile uint32_t *` into a fake register array). Interview tone: "logic-focused, explain while you write".

## Files
- md in the job dir ROOT: `/Users/donh/workspace/dons_tech_study_dashboard/job_interview_prep_research/rivian_nstaff_silicon_validation_engineer/2026-11-01_N05X_<slug>.md` (slug given in the index §2; must match exactly — cross-links depend on it).
- Code: `onsitePrep/n05/<x>/` (lowercase page letter). One file per problem `<ID>_<short>.c` containing the model answer + a `main()` with assert-based tests (edge cases included) that prints `<ID> PASS`. Plus `onsitePrep/n05/<x>/run.sh` that builds & runs all of them.
- Render ONLY your own md: `python3 ~/.claude/skills/job-research/scripts/render_html.py <md path>` (never with no args — others run in parallel). Check it prints `rendered`.

## Page layout (markdown subset — job-research renderer)
- Line 1 `# N05X · <title>`, line 2 `<!-- theme: pastel -->`, line 3 `>` header: `**묶음**: … · **문제 수**: … · **목표**: … · **선행**: [N05 목록](2026-11-01_N05_c_paper_drill_bank.html) · **이전/다음**: prev/next N05 page html · **코드**: onsitePrep/n05/<x>/`.
- `## 0. TL;DR · 사용법` — 5–7 bullets Korean: what this page trains, the 3 one-liners to memorize, which problems are ★ (most likely onsite).
- `## 1. 문제지 (인쇄용 — 답 없음)` — every problem's **English statement + function signature + 1 example** only, compact, one `###` per problem. Don prints this section and solves on paper. NO hints here.
- Then one `## <ID> · <title> (★/★★/★★★ · NN분)` per problem, containing in order:
  - `### 0. 문제 한국어로 먼저 보기` — 한 줄 요약 · 상황(왜 validation/firmware에서 나오나) · 구현할 것 표 (함수 · 하는 일 · 규칙) · 예시를 말로 풀면 · 지켜야 할 조건. **No solution hints.**
  - `### 1. Problem (English)` — interviewer-style statement, signature in ```c, 2–3 examples (input → output).
  - `### 2. 먼저 물어볼 것 (clarify)` — 2–4 English questions you'd ask + the assumed answer.
  - `### 3. 20분 타임박스` — tiny table: 0–2 clarify · 2–5 예시/접근 말하기 · 5–15 코드 · 15–18 trace · 18–20 테스트·확장. Add the 1–2 key ideas as **what to say out loud** (English sentence), not the code.
  - `<details><summary>힌트 1 (막히면 5분 후)</summary>` … `</details>` and `<details><summary>힌트 2</summary>` … `</details>`
  - `<details><summary>모범 답 (C)</summary>` — the verified code (```c), then 시간/공간 복잡도 one line. `</details>`
  - `<details><summary>Trace 표 (종이에 이렇게)</summary>` — a small table tracing 2–4 steps on the example. `</details>`
  - `<details><summary>테스트 케이스 (말로 할 것)</summary>` — 4–6 cases: normal, zero/empty, boundary (max width, wrap), error input, idempotence/round-trip. `</details>`
  - `### 4. 꼬리 질문` — 2–4 follow-ups the interviewer would ask (faster version? ISR-safe? 64-bit? buffer version? what if hardware does X?). Each: English Q, then `<details><summary>답</summary>` with 2–4 sentence English spoken answer + 한국어 핵심 한 줄 (+ code snippet if short) `</details>`.
  - `### 5. 채점 · 자주 틀리는 것` — 체크리스트 3–6 items (e.g. `1u` not `1`, `uint64_t` cast before shift, timeout, off-by-one, `volatile`), each one line.
- Last: `## 출처` — numbered list of URLs actually consulted (keep short; standard C facts can cite cppreference / C11 draft N1570 / Linux kernel headers / Hacker's Delight-style references / vendor app notes).

Renderer rules: headings `##`/`###`/`####` only; tables one line per row, no literal `|` inside cells (write "또는"; `\|` inside backticks OK); lists `-`/`1.` with 2-space nesting; fenced ```c ```text ```bash; only raw HTML allowed is `<details><summary>…</summary>` … `</details>` (blank line after `<summary>` line and before `</details>`) and `<br>`. Never put inline `code` inside link text. Existing related pages you may link (relative): `2026-10-29_c_basics_cram.html` (C1–C10), `2026-10-31_N02_screen_coding_retro.html`, `hmScreenPrep/problems/<V..>/index.html`, BV/LP notes `2026-11-01_BVxx_*.html` / `2026-10-31_LPxx_*.html` (see job dir listing). Link instead of re-explaining.

## Language
Korean prose; C identifiers & technical terms in English; interview-spoken parts (clarify, say-out-loud, follow-up answers) in English. Assume Don knows C but explain *why* traps matter in firmware/validation (e.g. W1C, MMIO ordering, ISR races).

## Verification (mandatory)
- Every model answer must be the exact code from your `.c` file, compiled and run:
  `cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined X.c -o /tmp/n05x_X && /tmp/n05x_X` (default SDK link is broken on this Mac; don't put flags in a quoted variable). Use `$TMPDIR` or /tmp for binaries.
- "What does this print / find the bug" problems (page H): compile each snippet and record the REAL output on this machine (macOS arm64, clang). Where behavior is implementation-defined/UB, say so explicitly and note platform differences (e.g. `char` is signed on Apple arm64 but unsigned on Linux aarch64) — tag claims you couldn't run `[미검증]`.
- Anything not compiled → `[미검증]`. No invented spec numbers; tag guesses `[추정]`.

## Final reply to orchestrator (short)
Files, line counts, problem count, `run.sh` output (PASS lines), anything [미검증]/[추정], open issues.
