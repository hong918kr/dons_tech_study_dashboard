# Lesson writer brief — track HWLAB (보드 브링업 · 계측), **Medium** level (HWLAB-M01–HWLAB-M12)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering: ROOT/NOTES_SPEC.md (lesson rules) → ROOT/curriculum/LESSON_WRITER_BRIEF.md (base rules: headings verbatim
and in order, svg rules, "never invent output") → ROOT/curriculum/HWLAB_B_LESSON_WRITER_BRIEF.md (HWLAB overrides:
Python 3.9 stdlib-only, CHECK harness, deterministic data, gen_*.py, real tools optional) → **this file** (Medium
overrides). Read all four. Everything in the HWLAB Basic brief applies unless overridden here.

## Medium overrides
- Level: Medium = what a board/SI/PI validation engineer is asked at mid/senior level ("TDR에서 이 dip은 뭐고 몇 cm
  지점인가", "phase noise 표를 RMS jitter로 바꿔라", "eye를 직접 만들어라"). Assume HWLAB Basic is known and
  cross-reference instead of re-teaching: B01 netlist audit · B02 logic levels/noise margin · B03 I2C pull-up/rise
  time · B04 IEEE-181 state levels / rise / overshoot (use it for edge/crossing measurements, cite it) · B05 scope
  BW / probe / ground-lead ringing · B06 trigger state machines · B07 LA capture formats (.sr/CSV/VCD loader) +
  sample rate · B08 rail ripple + 20MHz BW limit · B09 smoke test / inrush · B10 thermocouple CJC. All written:
  ROOT/lessons/HWLAB/HWLAB-Bxx/ — read B04 (and B07 if you parse LA captures, B08 for rails) for house style.
- Neighbouring Medium items M01..M12 are written IN PARALLEL right now. Ownership:
  M01 transmission line / bounce diagram / TDR / termination · M02 load transient droop / recovery / ζ→PM ·
  M03 PDN target impedance + decap selection · M04 low-power current profile / dynamic range / burden ·
  M05 LA timing compliance SPI/UART vs datasheet · M06 custom sigrok protocol decoder · M07 period / c2c / TIE
  jitter · M08 phase noise → RMS jitter · M09 eye diagram + golden-PLL folding + mask · M10 RJ/DJ decomposition +
  dual-Dirac · M11 BER bathtub + Q-scale · M12 channel ISI + FFE/CTLE/DFE.
  Advanced (not yet written, cite only): A01 CDR/JTOL · A02 lane margining 2D eye · A03 analog+digital correlation.
- **Self-contained** (important): every lesson dir runs on its own; do NOT import from another lesson dir. If your
  item builds on another item (M10 uses an M09-style capture, M11 uses a PRBS checker + M10 dual-Dirac, M12 uses
  M09 eye metrics, M05 uses a COMM-A10/B07-style decoder, M06 uses B07 loader), embed a compact version of what you
  need in your own files and say so in the md ("HWLAB-M09의 폴딩을 축약해 넣었다"). see: ids (COMM-A10, RF-A04,
  DSP-A06, SIV-A06, SIV-M06, MEM-A04, NETHW) are not written yet — cite by id only.
- Depth: md **450–650 lines**, front matter `updated: 2026-11-08`, `time:` 60분–90분. Q&A ≥6 with 꼬리질문.
  ≥2 svg (≥3 encouraged: e.g. bounce diagram lattice, |Z(f)| sketch with anti-resonance, eye with mask polygon,
  bathtub on Q-scale, jitter tree RJ/DJ/DDJ/PJ).
- Parts: A = core computation, B = the hard part interviewers probe (measurement artifact / why the naive method
  lies / model limits), C = bring-up/validation-realistic scenario with a planted defect or a decision between two
  candidates (named board/net/part) and a one-line actionable report, e.g.
  `FAIL net=PCIE_REFCLK osc=Y2 RJ_rms=1.21ps (12k–20M) > 1.0ps; Y3 0.38ps → PASS` or
  `TDR: Z=38.6Ω @ 11.8cm (via stub / connector) …`.
- Tests: table-driven + deterministic synthetic data with *known injected truth* (injected Z, injected RJ/PJ/DDJ,
  injected ζ, injected anti-resonance...) and the test compares the recovered value to the injected truth with an
  explicit, justified tolerance. Print measured vs truth. Where the item asks for a sweep / comparison table, the
  table printed in the md must be real output from your code (demo_*.py or the solution's report).
- Python 3.9 stdlib only (no numpy/scipy). You will need small numeric kernels — implement them in-file and explain
  the math in 풀이: complex arithmetic via `complex`/`cmath`, a radix-2 FFT or direct DFT on short records, linear
  least squares (normal equations, 2–3 params), log-log interpolation, Gaussian tail fit via inverse erfc
  (implement `erfc` via `math.erfc` and its inverse by bisection/Newton), FIR convolution, bilinear-transform
  biquads, 1st/2nd-order PLL loop. Keep every `python3 -I <file>` run ≤ ~20 s (pure Python is slow: choose sizes
  accordingly — e.g. 2^11–2^13 bits × 16–32 samples/UI for eye work — and state sizes + measured runtime in md).
- EE/SI accuracy: Don is an RF integration engineer — numbers must be defensible. Use standard relations and say the
  assumptions: ρ=(ZL−Z0)/(ZL+Z0), v=c/√εeff, TDR distance = v·t/2; 2nd-order step response & log-decrement
  ζ=δ/√(4π²+δ²), PM≈100·ζ (valid roughly ζ<0.6–0.7 — say so); Ztarget = V·ripple%/ΔI; capacitor Z = ESR + j(ωL − 1/ωC);
  σ_t = √(2∫L(f)df)/(2πf0) with L in linear (10^(L/10)); dual-Dirac TJ(BER)=DJδδ+2·Q(BER)·RJ with Q(1e-12)≈7.034;
  bathtub Q-scale Q = √2·erfc⁻¹(2·BER/ρ_T) (state the transition-density convention you use); CTLE zero/pole form.
  If a spec number (PCIe refclk jitter limit, SPI datasheet tSU/tH, PPK2/Joulescope ranges, golden PLL BW = bitrate/1667)
  is quoted from memory, label it "(대표값 — 데이터시트/스펙 확인 필요)" rather than inventing precision; tests must
  stay self-consistent with your own JSON spec file.
- Real tools optional (not installed: sigrok-cli, ngspice, kicad-cli). Do NOT install anything. HWLAB-M06: implement
  the decoder exactly against the libsigrokdecode Python API shape (class Decoder(srd.Decoder) with api_version=3,
  id/name/inputs/outputs/channels/annotations, `self.wait({...})` conditions, `self.put(ss, es, self.out_ann, [cls, [txt]])`),
  and test it with a small local shim `srd_shim.py` in your lesson dir that emulates `wait()` conditions
  ('r','f','e','h','l', {'skip': n}) + `put()` over a sample list, so the real decoder file is runnable unchanged
  inside PulseView later. Show the real `sigrok-cli -i x.sr -P ...` command under "장비/도구가 있을 때" without output.
- Files: ROOT/lessons/HWLAB/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + starter.py/solution.py/practice.py
  (+ gen_*.py deterministic, small data ≤200KB, demo_*.py). Title `# <ID> <title exactly as in HWLAB.json>`.
  solution.py = starter.py with only TODO bodies filled. §3 `<!-- code: starter.py -->`, §6
  `<!-- code: solution.py diff=starter.py -->`, §9 `### 한 번에 검증` + `<!-- code: practice.py -->` + run cmd.

## Verify (you MUST run, from your lesson dir)
    python3 -I starter.py   ; echo "exit=$?"   # runs, some FAIL, exit 1
    python3 -I solution.py  ; echo "exit=$?"   # all PASS, exit 0
    python3 -I practice.py  ; echo "exit=$?"   # all PASS, exit 0
    python3 -I -W error -X dev solution.py      # no warnings
    regenerate data with gen_*.py → identical (shasum before/after)
  Self-check md headings verbatim & in order (see Basic brief), svg viewBox + width ≤ 760 + classes only
  f-acc/f-ok/f-warn/f-mut, Q&A as `<details><summary>Q1. ...`, 추가 연습 3–6 with answers in <details>.
  Paste only real output you ran.

## When done
Reply (short): files written, PASS counts starter/solution/practice, runtime, data/demo files + what they showed
(key numbers), anything unsure (esp. EE/spec constants). Do NOT edit files outside ROOT/lessons/HWLAB/<your ID>/.
