# Lesson writer brief — track CPP (임베디드 C++), **Advanced** level (CPP-A01–A03, CPP-A05–A10)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

Layering: ROOT/curriculum/CPP_LESSON_WRITER_BRIEF.md (Basic, base rules) → ROOT/curriculum/CPP_M_LESSON_WRITER_BRIEF.md
(Medium overrides + **verified cross-target recipes (a)/(b)/(c)**) → **this file** (Advanced overrides). Read all three plus
ROOT/NOTES_SPEC.md. Everything in the earlier briefs applies (output files, headings verbatim and in order, svg rules,
strict build, ASan/UBSan, xorshift determinism, compile-fail proofs + SHOW_ERROR demos, "never invent output") unless
overridden here. You write exactly ONE lesson (the id in your prompt). CPP-A04 is already written (reference lesson).

## Overrides for Advanced
- Level: senior/staff firmware interview depth. The question is the item's `task`; follow-ups probe *design judgement*
  (why this API, what it costs in flash/RAM/cycles, what breaks on another compiler/core/toolchain, how you'd test it).
  Assume CPP-B01–B14 and CPP-M01–M14 are known (all written: ROOT/lessons/CPP/CPP-Mxx/) and cross-reference by id
  instead of re-teaching. Read the 1–2 Medium lessons named in your lane so you build on them.
- Reference lesson for depth/style: ROOT/lessons/CPP/CPP-A04/ (md + asm_targets.sh + demos). Advanced md is
  **500–750 lines** (CPP-A10 capstone ≤ 850).
- Parts: A = core implementation, B = the hard part / edge cases / "what goes wrong", C = firmware-realistic integration
  — each with checked tests. Q&A ≥ 6. 추가 연습 3–6.
- Front matter `updated: 2026-11-11`, `time:` realistic (75분–120분; capstone up to 150분).
- Output: ROOT/lessons/CPP/<ID>/2026-11-11_<ID>_<english_snake_slug>.md + starter.cpp + solution.cpp + practice.cpp
  (+ demo_*.cpp, small *.sh as needed). Title line `# <ID> <title exactly as in CPP.json>`.
- Neighbouring CPP-A items are written IN PARALLEL right now — reference by id, never `#include` another lesson's file;
  every lesson is self-contained (embed compact local versions of Reg / Result / static_vector etc. and cite their owner).
- Every "zero cost / smaller / inlined / no runtime symbol" claim must be backed by real output you ran and pasted (asm,
  `xcrun llvm-size`, `xcrun llvm-nm`, arm-none-eabi-size). If the result contradicts the expected story, say so and
  explain why — that's the better lesson.
- Firmware claims need a Cortex-M object: use recipe (b) with **ARM_INC = <SCRATCH>/arm-inc** (already copied from the VM,
  read-only for you; contains c++/14.2.1 headers) and recipe (c) (Lima VM `elinux`, arm-none-eabi-g++ 14.2.1, work in VM
  /tmp/<ID>/ — the Mac home mount is read-only there). Scripts you leave in the lesson must default ARM_INC the way
  ROOT/lessons/CPP/CPP-A04/asm_targets.sh does (fetch from the VM to /tmp if missing) and run from the lesson dir.
  VM may be stopped by another session: `limactl list`; `limactl start elinux` if needed.
- Accuracy: cite [dcl.link], [temp.pre], [basic.start.dynamic], [stmt.dcl], [class.abstract], [expr.const],
  [dcl.constexpr], [basic.life] etc. where useful, and double-check. Itanium C++ ABI vs ARM EABI differences (e.g.
  `__aeabi_atexit`, guard variable size/bit, `__cxa_*` naming) must be observed, not recalled. Coding-standard rule
  numbers only if verified (MISRA is paid → say "MISRA-style"; AUTOSAR C++14 guideline PDF is public — verify via WebFetch
  before citing a rule id). If you couldn't verify something, write "미확인".

## Workspace
- Private scratch dir: `<SCRATCH>/<ID>/` (SCRATCH in your prompt). Other writers run concurrently — never write to /tmp
  top-level names or shared helper names. Don't edit anything outside ROOT/lessons/CPP/<your ID>/. Don't run
  build_site.py or git.

## Verify (you MUST run)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
    B=<SCRATCH>/<ID>; F="-std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti"
    c++ $F starter.cpp  -o $B/st && $B/st   # 0 warnings, some FAIL, exit 1, no hang/crash
    c++ $F solution.cpp -o $B/so && $B/so   # all PASS, exit 0
    c++ $F practice.cpp -o $B/pr && $B/pr   # all PASS, exit 0
    c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o $B/sa && $B/sa
    (same for practice.cpp)                                                                 # clean
  If your lane needs a different build (e.g. A03 flags study, A09 legacy file) the main three still build with $F;
  extra builds go in a script in the lesson dir that exits 0 on success — say its name in your reply.
- solution.cpp = starter.cpp with only TODO bodies filled (site highlights the diff). Stubs warning-free, wrong-but-safe.
- Bug-detection honesty: inject ≥ 2 plausible bugs into a private copy of solution.cpp and confirm the harness FAILs (or
  the compile-fail proof fires); report which. Don't leave mutants in the lesson dir.
- Self-check the md: headings verbatim & in order, ≥ 2 ```svg with viewBox (width ≤ 760), Q&A ≥ 6 as
  `<details><summary>Q1. ...`, `### 한 번에 검증` + `<!-- code: practice.cpp -->`, `<!-- code: starter.cpp -->` in §3,
  `<!-- code: solution.cpp diff=starter.cpp -->` in §6.

## Lanes (owner topics; cross-reference the rest)
- **CPP-A01 Typestate.** Builds on CPP-M01 (Reg access rights), CPP-B06 (RAII). RUST-M04 is the Rust analogue (one-line
  link; Rust moves make the old state unusable — C++ can't, so own this hole: moved-from `Spi<Disabled>` still names a
  usable object → show mitigations: `&&`-qualified transitions, consuming by value, `[[nodiscard]]`, clang-tidy
  `bugprone-use-after-move` real output via `~/.venvs/clangtidy/bin/clang-tidy ... -- -std=c++20 -isysroot <SDK>`).
  Must: Disabled→Configured→Enabled (+ disable() back), transfer only on Enabled (requires-clause / overload set),
  compile-fail proofs as requires-expressions in static_assert + SHOW_ERROR demo with real errors, zero cost proven
  (sizeof, thumbv7em asm of typestate driver vs plain C-style driver doing the same register writes — instruction
  counts side by side). Hard part: state known only at runtime (e.g. after reset-reason / bootloader handoff) →
  `std::variant` or runtime-checked escape hatch, and its cost; a singleton token so two Spi objects can't own SPI1.
- **CPP-A02 ISR trampoline.** Builds on CPP-M12 (delegate), C-M06 (fn-pointer tables); BM-M01/BM-A01 own NVIC and VTOR
  relocation — reference. Own: vector table of `extern "C"` functions → per-IRQ object pointer table → member handler.
  Facts to verify and state precisely: a template cannot have C language linkage ([temp.pre]); function types with
  different language linkage are distinct types per [dcl.link] but clang/gcc treat them the same (show what Apple
  clang does with a static member fn in a C-linkage fn-pointer table). Compare: (1) runtime registration table
  (spurious/unregistered IRQ counter, register-before-enable ordering, ISR-vs-registration race), (2) compile-time
  binding via template `Trampoline<Irq, &instance>` with a static member function (no table lookup) — show real thumbv7em
  asm of both handlers (instruction count, literal pool), (3) const table in flash vs RAM table. Weak default handler
  alias + `.isr_vector` section on a thumbv7em object (`xcrun llvm-objdump -h -t`). Host test: fake IRQ loop firing many
  IRQ numbers across ≥ 3 UART instances with deterministic xorshift sequence; check per-instance counts and that an
  unregistered IRQ goes to the default handler.
- **CPP-A03 __cxa_* runtime deps.** Builds on CPP-B09 (static init), CPP-B13 (-fno-exceptions/-fno-rtti), CPP-M06
  (deleting dtor → operator delete); MCPP-A05 owns mangling/ODR. Own: a table "source construct → undefined runtime
  symbol → how to remove", every row observed with `xcrun llvm-nm -u` on a `--target=thumbv7em-none-eabi -ffreestanding`
  object (recipe a or b): function-local static with non-trivial ctor (`__cxa_guard_acquire/release/abort`;
  `-fno-threadsafe-statics`), pure virtual (`__cxa_pure_virtual`), global with non-trivial dtor (observe whether ARM
  EABI emits `__aeabi_atexit` vs `__cxa_atexit`, `__dso_handle`; `-fno-use-cxa-atexit`; never-destroyed wrapper),
  virtual dtor → `_ZdlPv`/`_ZdlPvj`, `new` → `_Znwj`, typeid/dynamic_cast with rtti on, `throw` with exceptions on,
  64-bit division / memcpy → `__aeabi_*` (not cxa but same "who provides it" question). Then a real link in the VM
  (recipe c, `-nostdlib`/`-nostartfiles`) showing "undefined reference" and the fix by stubs (what each stub must do:
  guard semantics single-core vs with interrupts, pure_virtual = trap/fault log). Checked host harness: implement the
  stub layer (guard acquire/release/abort state machine, pure-virtual trap recording, atexit registry with capacity) +
  an nm-line classifier over pasted fixtures captured from your real runs. Leave `check_syms.sh` in the lesson that
  builds the freestanding object and FAILS if any undefined symbol outside an allow-list remains (exit 0 when clean).
- **CPP-A05 thin template.** Builds on CPP-M10 (static_vector owner — embed a compact local version) and CPP-M05 (CRTP
  bloat measured). MCPP-A09 owns build-time cost. Own: 5 types × 3 N instantiations, measure `.text` by function on
  thumbv7em object (recipe b) and linked ELF (recipe c) with/without `-ffunction-sections -Wl,--gc-sections`; then
  byte-level non-template core (`void*` + elem size + capacity at runtime) + thin typed wrapper; N must not create new
  code; restrict memcpy core to trivially copyable T (concept), non-trivial T via per-type ops table or keep full template
  — show the trade-off. Report before/after with real numbers. Identical code folding: check what GNU ld does (no ICF)
  vs lld `--icf=all` if available vs Mac ld64 (dedups by default — observe), and say which applies to firmware. Bounds/
  overflow behaviour identical before and after refactor (equivalence test over xorshift op sequences).
- **CPP-A06 constexpr clock tree / baud.** BM-B10 owns PLL math and BM-B12 baud divisor in C — cite; own the C++ part:
  constexpr exhaustive M/N/P(/Q) search with constraints (use STM32F4-class limits — VCO in/out ranges, P∈{2,4,6,8},
  48 MHz Q — labelled "RM0090 대표값" unless you verify the reference manual), consteval to force compile time, a
  template/`static_assert` gate failing when UART BRR error > 2% (oversampling 16 and 8, mantissa/fraction), integer
  ppm math (no float needed — explain choice). Show real compile errors for failing cases and how to make the message
  point at the culprit (CPP-M14 owns static_assert message techniques — cite). Measure constexpr evaluation limits:
  clang `-fconstexpr-steps` default and what happens when the search exceeds it (real error), compile time of the
  search. Prove zero runtime: thumbv7em asm shows only constant stores. Runtime-checked fallback for clocks only known at
  runtime, tested against the constexpr path for agreement over a table of HSE values.
- **CPP-A07 static active objects.** Builds on C-M07 (table FSM), CPP-M10, CPP-M12, DS ring buffers; RTOS-B13/RTOS-A06
  are RTOS-side neighbours. QP/C++ is GPL/commercial — describe the idea, write your own code, cite Samek's book/site only
  for what you verify. Own: fixed event pool (refcount for publish to many AOs, leak check = all blocks returned at end),
  per-AO fixed queue (overflow policy: assert vs drop+counter — choose and justify), flat state machine with
  member-function-pointer state handlers + entry/exit, run-to-completion, priority cooperative scheduler, time events
  driven by a tick, post-from-"ISR" path (critical-section model on host). Scenario: button press (with debounce) → LED
  blink pattern, verified as an exact event/action trace with virtual time. Show RAM accounting (sizeof each AO/queue/
  pool) and that no `operator new` is linked (replace global new to abort in harness or `nm` check).
- **CPP-A08 testable driver: DI vs link seam.** TEST-B04 owns C seams (link seam, fn-pointer, preprocessor) and TEST-B05
  CMock-style expectations — cite; CPP-M06 owns vtable accounting. Own the C++ comparison: one sensor driver (I2C temp
  sensor, register map labelled 대표값 unless verified) as (1) template-parameter bus, (2) virtual interface bus,
  (3) link-time substitution (separate i2c_hw.cpp / i2c_fake.cpp; `link_seam.sh` builds and runs it, exit 0). Same test
  suite against all three (fake with scripted responses + call log, error injection: NACK, timeout, bad CRC/ID).
  GoogleTest/doctest are NOT installed: use a tiny doctest-style harness (TEST_CASE/CHECK macros) in the lesson; a demo
  may fetch a pinned doctest.h into your private scratch only to prove the tests compile unchanged under the real
  framework — optional, never commit it. Trade-off table with REAL thumbv7em numbers per variant (code size of the
  production build, vtable/typeinfo presence, call asm) + qualitative rows (test ergonomics, compile time, header
  exposure, ability to inject per-test). State when you'd pick each.
- **CPP-A09 MISRA C++/AUTOSAR review.** C-A10 owns the C version (read it: legacy file + tidy + equivalence-tested
  refactor pattern — reuse that pattern). Own C++-specific findings: implicit conversions / narrowing, C-style &
  reinterpret casts, unions vs std::variant/bit_cast, dynamic memory, exceptions/RTTI, virtual dtor & rule of 5/0,
  uninitialised members, `goto`, recursion, varargs, magic numbers, owning raw pointers, non-const globals. Put the
  intentionally-violating firmware code in `legacy_*.cpp` (built only by a script) and run real
  `~/.venvs/clangtidy/bin/clang-tidy --checks='-*,cppcoreguidelines-*,cert-*,hicpp-*,bugprone-*,misc-*,readability-*,modernize-*'
  ... -- -std=c++20 -isysroot <SDK>` on legacy vs fixed; `tidy.sh` reproduces the counts. Classify each finding:
  real defect / safety-standard rule / style noise to suppress (with justification, NOLINT policy). MISRA C++:2023
  (which absorbed AUTOSAR C++14) — state the relationship only as verified. starter/solution = refactor target + old-vs-new
  equivalence tests over many xorshift inputs (C-A10 pattern). Also show what clang-tidy MISSES that the standard
  forbids (tool gaps), with a demo.
- **CPP-A10 capstone sensor pipeline.** Integrates CPP-M01 (Reg), CPP-M10 (static_vector), CPP-M11 (Result), CPP-B06
  (RAII), CPP-M13 (constexpr LUT/CRC) — embed compact local versions, cite owners. Pipeline: mock I2C bus (register file
  + fault injection) → IMU driver (WHO_AM_I check, config, burst read, register map labelled 대표값 unless verified) →
  fixed-point filter (Q15/Q16; overflow/saturation/rounding documented, tested against a double-precision reference with
  an error bound) → telemetry frame (little-endian packing, sequence number, CRC, optional COBS) with a golden-frame test
  and a decoder round-trip. Heap 0: global operator new replaced to trap + `nm` shows no `_Znwj`/malloc in the target
  build; exceptions 0. Size budget: build the target part for Cortex-M4 in the VM (recipe c, gc-sections) and report
  `.text/.rodata/.data/.bss` total + per-component (`arm-none-eabi-nm -C --size-sort`) vs an 8 KB `.text` budget —
  `budget.sh` exits 1 if over. End with an architecture svg and an "interview walkthrough" of design choices.

## When done
Reply (short): files written, md line count, PASS counts starter/solution/practice, sanitizer result, injected bugs and
whether they were caught, extra scripts + exit status, demo files + what they really showed (one line each), anything
unverified.
