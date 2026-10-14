# Scout coding practice set — authoring spec

Audience: Don (Korean, senior embedded/firmware engineer) practicing for a 30-min CoderPad C screen at Scout AI
(defense robotics). Recruiter hints: sensor / time-temp / overheating detection, "Array · Item · State · Edge",
debug, embedded constraints, test edge cases. Source plan: ../2026-10-12_scout_technical_screen_prep.md §3–§4.

**Exemplar to copy exactly in structure and quality: problems/T1-1_overheat_intervals/** (README.md, stub.c,
solution.c, chart1.svg, chart2.svg). Read all of it before writing anything.

## Language rule (strict)
- Everything the candidate sees as the *problem* is ENGLISH: title, §1 Problem text, signatures, examples,
  constraints, clarifying questions, all comments and test descriptions inside stub.c / solution.c,
  Follow-up questions (left column).
- All *explanation* is KOREAN (with English technical terms): hints, 풀이, trace commentary, 임베디드 관점,
  Follow-up answers, 자주 하는 실수, chart captions. Korean style: plain, concise, 합니다체, no fluff.

## Per-problem folder: problems/<DIR>/
- `stub.c`   — compiles cleanly with `cc -std=c11 -Wall -Wextra` (cast unused params to void), function bodies
               are TODO returning a dummy value, full test `main()` using the CHECK macro (normal case first,
               then 3–6 edge cases). Tests must FAIL on the stub (not crash). Top comment = English problem summary.
- `solution.c` — identical to stub except the implementations (and header says "Reference solution").
               Must print ALL PASS under `-fsanitize=address,undefined` with `-Werror`.
               Idiomatic embedded C: fixed-width ints, no malloc (unless the problem is about allocators),
               caller-provided buffers, wrap-safe time compare, integer math. Short focused comments.
- `chart*.svg` — 1–3 hand-written SVG diagrams that genuinely aid understanding (time-series plot with
               threshold, state machine, ring buffer index diagram, bit layout, deque evolution, etc.).
               White background, pastel palette only:
               blue line #5677c9 · hot/red #c4554e bg #fce5e2 · good/green #2f8a5f bg #e2f4e9 ·
               warn/amber #a9700f bg #fcf0d6 · violet #8a5cc0 bg #f0e7fb · axis #b9b0c6 · muted text #8a8297 ·
               ink #2e2939. font-family="-apple-system,'Apple SD Gothic Neo',sans-serif", font-size 11–14.
               Compute coordinates carefully; no overlapping labels; text inside viewBox.
               Chart text may be English or Korean (short).
- `README.md` — same sections as the exemplar:
  `# <ID> · <English title>` then the `> **ID**: … · **Priority**: P0|P1|P2 · **Pattern**: … · **Time**: N min`
  quote line (keep these exact keys — build.py parses them) + the 파일/실행 line;
  ## 1. Problem (English; code block with signature; ### Example; ### Constraints; ### Clarifying questions)
  ## 2. Your workspace  → `<div class="code" data-src="stub.c"></div>`
  ## 3. 힌트 → 2–3 `<details><summary>Hint N — …</summary>` blocks (blank line after <summary> line and before </details>)
  ## 4. 풀이 → 4.1 핵심 아이디어 with chart `<div class="chart" data-src="chart1.svg" data-caption="…"></div>`,
     state/diagram, ### trace table (손으로 따라가기), ### 정답 코드 inside
     `<details><summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>` … `<div class="code" data-src="solution.c"></div>` … `</details>`
     (the 💡 prefix makes it a spoiler box), 코드 읽는 포인트, ### 복잡도
  ## 5. 임베디드 관점으로 말할 것
  ## 6. Follow-up 질문과 답 (table: English question | Korean answer)
  ## 7. 자주 하는 실수
  Markdown subset only: headings, lists, tables, fenced code, **bold**, `code`, [links](x), <details>.
  No raw HTML other than the two <div> placeholders and <details>/<summary>. Cross-link related problems
  with relative links like `[T1-2](../T1-2_streaming_hysteresis/index.html)`.

## Directory names (fixed — use exactly)
T1-1_overheat_intervals · T1-2_streaming_hysteresis · T1-3_moving_average · T1-4_multi_sensor ·
T1-5_raw_register_to_temp · T1-6_sensor_fault_detect · T1-7_time_above_threshold · T1-8_blocking_api_wrapper ·
T2-1_ring_buffer · T2-2_packet_parser · T2-3_merge_intervals · T2-4_window_max · T2-5_lower_bound ·
T2-6_bit_ops · T2-7_debounce_edge · T2-8_running_median · T2-9_memory_pool · T2-10_top_k_sensors · D_debug_drills

## Verify before finishing (from technicalScreenPrep/)
1. `./run.sh <ID>` → compiles, tests fail, no crash.   2. `./run.sh <ID> sol` → ALL PASS.
3. `cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined problems/<DIR>/solution.c && ./a.out`
4. `python3 build.py problems/<DIR>` renders without "⚠ missing".
5. Every numeric value quoted in README (examples, trace tables, chart labels) must match what solution.c computes.
Do not touch files outside your assigned folders.
