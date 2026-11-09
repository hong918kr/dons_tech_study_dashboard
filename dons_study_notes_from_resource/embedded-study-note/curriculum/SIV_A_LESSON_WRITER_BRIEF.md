# Lesson writer brief — track SIV (실리콘 검증 · Post-silicon Validation), **Advanced** level (SIV-A01–SIV-A11)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering (read ALL, in order): ROOT/NOTES_SPEC.md → ROOT/curriculum/LESSON_WRITER_BRIEF.md (base: headings verbatim & in
order, svg rules, "never invent output") → ROOT/curriculum/C_M_LESSON_WRITER_BRIEF.md (deterministic PRNG tests, demo_*.c,
benchmarks only in demos, POSIX define) → ROOT/curriculum/SIV_B_LESSON_WRITER_BRIEF.md (SIV who/why, mock MMIO, fault
injection, FAIL report format, self-contained code) → ROOT/curriculum/SIV_M_LESSON_WRITER_BRIEF.md (SIV Medium: injected-truth
tests, per-language verify commands) → **this file** (Advanced overrides). Python lessons also read the "Code & language"
section of ROOT/curriculum/HWLAB_B_LESSON_WRITER_BRIEF.md (Python 3.9 stdlib only, `python3 -I`, gen_*.py deterministic data
≤ 200 KB). `-I` removes the script dir from sys.path → keep each .py self-contained.

## Overrides for Advanced
- SIV Basic B01–B14 and Medium M01–M14 are ALL written (ROOT/lessons/SIV/). Assume known; cross-reference by id, never
  re-teach. Medium owners: M01 ATE/SLT/system classification · M02 DMA SG + CRC32 · M03 IRQ latency/lost/coalesce · M04 PCIe
  config/LTSSM · M05 DDR training + stress · M06 PRBS + BER · M07 shmoo/Vmin · M08 PVT schedule · M09 concurrent stress ·
  M10 triage clustering · M11 bisect · M12 Python sequencer + ctypes · M13 functional coverage · M14 ECC. Skim the md of the
  ones your item builds on. Code stays self-contained (embed compact versions; say "SIV-Mxx를 축약해 넣었다").
- Neighbour Advanced items A01..A11 are being written IN PARALLEL. Read their title/task in SIV.json and stay in your lane.
- Level: Advanced = staff-level interview + real validation-lead judgement. The follow-ups probe *why this strategy, what it
  misses, how you'd know it's working, what it costs on a real lab/schedule*. md **500–750 lines** (A09 up to 850),
  front matter `updated: 2026-11-08`, `time:` 80분–130분. Q&A ≥6 with real 꼬리질문.
- Parts: A = core mechanism, B = the hard part (statistics, identifiability, aliasing/masking, ordering, "what can this
  method NOT detect"), C = validation-realistic campaign/scenario (named SoC/IP/board, planted defect(s) detected, a report a
  debug engineer can act on). Each Part has checked tests.
- Tests: table-driven + deterministic random. Planted defects MUST be detected with the right location/signature; the good
  model MUST pass; also at least one "limits" test or measured table showing what the method misses (masking, aliasing,
  confounding), honestly. Numeric recovery tests CHECK against the injected truth with a stated tolerance and say why that
  tolerance. No wall-clock timing in CHECKs.
- Accuracy over volume: Don has an RF/SSD background and a Rivian silicon-validation pipeline. Any spec fact (see per-item
  notes) must be correct; if unsure say it less specifically and flag it in your final report. Representative numbers that
  are not from a spec are labelled 대표값/가정.
- Private workspace: use <session scratchpad>/<ID>/ or /tmp/<ID>_* for helpers. Never write a shared-name file in the
  scratchpad root (parallel writers clobbered each other before). Do NOT run build_site.py; the coordinator does it.

## Per-item language + notes
Language key: **C** = starter.c/solution.c/practice.c. **Py** = starter.py/solution.py/practice.py (+ gen_*.py + data).
- **SIV-A01 (C)**: Part A generator (weighted, legal-ordering constraints: e.g. cannot start DMA while busy, IRQ clear only
  after pending in constrained mode) + golden model + DUT + scoreboard with first-mismatch report (seed, step, expected vs
  actual, last-N-ops trace). Part B: the planted bug fires only in a narrow order (e.g. DMA-done coinciding with W1C of another
  status bit, or done while masked); show directed tests miss it, measure sequences-to-detect across fixed seeds (table in md);
  constraint-bias/weighting changes hit rate. Part C: seed replay + automatic sequence shrinking (ddmin-style) to a minimal
  repro; report. Distinguish scoreboard mismatch from model bug. UVM terms OK as analogues; don't claim UVM code.
- **SIV-A02 (Py)**: BSDL subset parser (`BOUNDARY_LENGTH`, `BOUNDARY_REGISTER` cell entries like
  `"0 (BC_1, PA0, output3, X, 1, 0, Z)"` — cell, port, function, safe, control cell, disable value, disable result; INSTRUCTION_OPCODE
  for EXTEST), netlist (net → device.pin), multi-device. Patterns: walking-1 and (modified) counting sequence with no all-0 /
  all-1 net codes, ⌈log2(N+2)⌉ vectors. Virtual board with open (reads pull state), short (wired-AND / wired-OR), stuck-at.
  Part B: diagnosis aliasing/confounding of counting vs walking; driver contention safety (only one driver per net, tri-state
  the rest, control cells, safe values). TAP FSM stays in TOOL-A01 (not written; only reference it): model shift-DR at the
  register level. Real BSDL semantics must be correct; IEEE 1149.1 detail only where confident.
- **SIV-A03 (C + Py, two sets)**: C (starter.c/solution.c/practice.c) = INA226-like driver on mock I2C. Py
  (starter.py/solution.py/practice.py) = analysis. INA226 facts (TI datasheet, verify from memory carefully): 16-bit big-endian
  registers; 0x00 Configuration, 0x01 Shunt Voltage (signed, LSB 2.5 µV), 0x02 Bus Voltage (LSB 1.25 mV), 0x03 Power
  (LSB = 25 × Current_LSB), 0x04 Current (signed), 0x05 Calibration, 0xFE Manufacturer ID 0x5449, 0xFF Die ID 0x2260;
  CAL = 0.00512 / (Current_LSB × Rshunt). Test scaling, sign, overflow and calibration rounding error; injected NAK/byte-swap
  faults. Analysis: synthetic workload×V×f×temperature sweep with known truth + noise. P = Pleak(T,V) + α·C·V²·f. Implement least
  squares yourself (stdlib): per-temperature fit of P vs V²f (intercept = leakage, slope = C_eff), then fit ln(Pleak) vs T.
  Part B must show identifiability: if the sweep has too few V/f points or V and f move together, or the die self-heats during
  high-power points (T not independent), what breaks; quantify with condition number or recovered-parameter error. Both sets are
  required in md (`<!-- code: starter.c -->` and `<!-- code: starter.py -->` etc.).
- **SIV-A04 (C)**: Firmware-style throttle governor under test + 2-3 node thermal RC model (compact, forward Euler with
  stable dt stated; full RC ownership is SIM-M08, not written). Check trip threshold, hysteresis (no oscillation/chatter),
  DVFS step order, ultimate shutdown. Part B: sensor offset/lag/stuck → quantify overshoot past Tj_max, time-to-throttle delay vs
  offset (table), required guard band. Part C: planted governor bugs (hysteresis sign, missed step, stale sensor not detected)
  plus a verdict report. Numbers = 대표값.
- **SIV-A05 (C)**: Table: {id, affected revision range, applies-if predicate, workaround fn, verify test, status}. Part A
  apply exactly the right set on A0/A1/B0 mocks. Part B: ordering/dependencies, idempotency, workaround conflicts, unknown
  revision policy (future silicon: fail-safe), ID register decode (major/minor metal spin), workaround that must be applied
  before MMU/cache/clock change. Part C: ECO removal — a B0 mock where the bug is fixed; the verify test proves the bug is really
  gone on B0 and still present (needs workaround) on A0; detect stale/unnecessary entries; workaround that masks the test. Real
  analogue to cite accurately: Linux arm64 `arch/arm64/kernel/cpu_errata.c` (MIDR range tables/capabilities), vendor errata sheets.
- **SIV-A06 (Py)**: gen_*.py synthetic multi-rail ADC logs with timestamps; checker for sequence order (threshold-cross times,
  min delay), ramp time (10–90%), tolerance ±5% after settling, glitch (duration + depth, using hysteresis so noise isn't flagged),
  power-down order. Clock part: ppm = (f_meas − f_nom)/f_nom × 1e6, frequency counter gate-time resolution, PLL ratio tree expected
  outputs. Part B: sampling-rate limits (a glitch shorter than sample period can't be seen → say so, measure), noise vs
  threshold, timestamp jitter/monotonicity, simultaneous rails tie-breaking. Part C: bad board logs (out-of-order rail, slow ramp,
  brownout glitch, off-frequency PLL) → report. Note lab tools needed for real capture (env_note).
- **SIV-A07 (Py)**: gen_*.py synthetic LA CSV (time, SCL, SDA samples; ≤ 200 KB each). I2C decoder: START/STOP/repeated START,
  address/RW, ACK/NACK, byte reassembly, clock stretching timeout, setup/hold violations, arbitration-lost (needs a
  multi-master model: driven-vs-observed). Timing limits from UM10204 — Standard-mode tSU;DAT 250 ns, tLOW 4.7 µs, tHIGH 4.0 µs; Fast-mode
  tSU;DAT 100 ns, tLOW 1.3 µs, tHIGH 0.6 µs, tr 1000 ns/300 ns max; double-check, flag anything else you state. Part B: what a
  digital LA can and cannot see (rise time is not directly observable from thresholded samples; weak pull-up appears as delayed
  low→high edges, so asymmetric timing: state exactly what your synthetic model does); sample-rate margin (measured). Part C: three
  captures (weak pull-up, timing violation, slave hang — SDA held low; 9-clock recovery) → cause report. HWLAB-M06 = sigrok decoder
  for other protocols; stay in the root-cause lane.
- **SIV-A08 (Py)**: gen_*.py synthetic wafer lots (x,y, Vmin, Fmax, leakage, test fail codes; ≤ 200 KB). Yield, bin assignment
  (speed/leakage bins, with bin priority), ASCII wafer map, spatial signature detection (edge ring/cluster). Test-order
  optimization: expected time under independence with stop-on-first-fail; sort by p_i/t_i descending (prove by exchange
  argument; verify vs brute force for small n). Part B: correlated fails (independence breaks: measure predicted vs empirical,
  order empirically/greedily), and what early-stop loses (full-fail datalog for diagnosis → sampling strategy). Part C:
  lot with a planted excursion + report. Tie to M01 (ATE/SLT). Poisson yield Y=exp(−A·D) if used; label a model as a model.
- **SIV-A09 (Py + design doc)**: The lesson IS the design doc plus Day-0 skeleton. Part A: validation plan in md (phases pre-si
  prep → Day-0 bring-up → feature → PVT → stress → production handoff; entry/exit criteria per phase; ownership); Part B:
  infrastructure + triage flow + schedule/staffing/metrics with stated assumptions, risk list, what you cut first when the
  schedule slips; Part C: Day-0 script skeleton in Python: gated dependency stages (power → clock → JTAG IDCODE → boot ROM/SRAM → DDR
  training → PCIe link → CSI-2/SerDes loopback) against a mock chip with injected faults; stops/skips dependents properly, classifies
  (blocked vs failed), emits a triage packet. Code owns gating/report only (cross-reference M04/M05/M06/M12 etc.). Also give a
  45-minute verbal answer outline. Rivian-relevant, but no claims about Rivian's internals. Spec facts about CSI-2/SerDes only
  if confident; label the rest 가정. Other than the code, md must be a genuinely usable doc, not filler.
- **SIV-A10 (C)**: Fibonacci/Galois LFSR with a primitive polynomial (verify period 2^n−1 in the tests), combinational CUT model,
  stuck-at fault injection on nets, MISR compaction, golden compare, coverage curve vs patterns. Part B: aliasing measured
  (small n, many error streams) vs 2^−n; note a single-bit error stream can't alias for a linear signature register; random-pattern-
  resistant faults (wide AND) motivates weighted/test points. Part C: firmware BIST sequence on mock registers (start, poll w/
  timeout, signature compare, X-sources/seed config, done-bit stuck, wrong signature). Don't confuse with March (B07 = RAM).
- **SIV-A11 (C)**: Mock SoC domains with clock gate / power switch / isolation / retention state and bus-error responses. Tests:
  gated access error, reset vs retention values after power cycle, isolation release ordering, first-access-after-clock-enable
  safety (N-cycle settle), and planted defects (retention skipped, isolation released early, clock enabled before reset released
  etc.). Sequence: state a representative power-down/up order and say that real order comes from the SoC's power spec/UPF intent
  (vary across designs); UPF is IEEE 1801. Part B: state coverage of ordering (permutation of the sequence steps: which orderings
  are unsafe — exhaustive enumeration with a safety oracle). RTL-level low-power checks stay in RTLDV-A08.

## Verify (you MUST run; own temp names /tmp/<ID>_*)
Exactly the SIV-M brief's C and Python verify blocks. Starter exit 1 and solution/practice exit 0 for every language set. SIV-A03: both sets.
md self-check as in the SIV-M brief, with Q&A ≥6, ≥2 ```svg. Paste only real output you ran.

## Output
ROOT/lessons/SIV/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + code/data files. Title `# <ID> <title exactly as in SIV.json>`.

## When done
Reply (short): files written, PASS counts starter/solution/practice, sanitizer result, demo/data files + what they showed, measured
limits, anything you were unsure about (spec details). Do NOT edit files outside ROOT/lessons/SIV/<your ID>/.
