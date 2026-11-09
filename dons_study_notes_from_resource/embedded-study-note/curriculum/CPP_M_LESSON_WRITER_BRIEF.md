# Lesson writer brief — track CPP (임베디드 C++), **Medium** level (CPP-M01–CPP-M14)

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

This is the CPP Basic brief (ROOT/curriculum/CPP_LESSON_WRITER_BRIEF.md) adapted for Medium. Read that file too —
everything in it applies (output files, section headings, svg rules, code rules, platform reality, "never invent
output") EXCEPT where this file overrides it. You write exactly ONE lesson (the id given in your prompt).

## Read first (required)
1. ROOT/NOTES_SPEC.md and ROOT/curriculum/CPP_LESSON_WRITER_BRIEF.md.
2. ROOT/curriculum/tracks/CPP.json — your item. The lesson must do the item's `task`. Read the other CPP-M titles too.
3. Reference lessons: ROOT/lessons/CPP/CPP-B12/ (structure, harness, compile-fail demos) and ROOT/lessons/CPP/CPP-A04/
   (cross-target scripts, depth). Match style; Medium md is **450–650 lines**.

## Overrides for Medium
- Level: a real interview coding question at mid/senior firmware level. Assume CPP-B01–B14 and the C track are known;
  cross-reference by id ("(CPP-B12 참고)"), never re-teach: B03 enum class fields, B04 constexpr address/mask,
  B05 volatile/cv refs on registers, B06/B07 RAII guards, B08 layout/EBO, B09 static init/.init_array,
  B10 std::array, B11 span, B12 template reg_read/reg_write + type-rule concepts, B13 -fno-exceptions/-fno-rtti and a
  first Result<T,E>, B14 banning operator new. Broader-language topics are owned by MCPP (MCPP-B03 move impl,
  MCPP-M05 SFINAE→concepts, MCPP-M07 std::function SBO, MCPP-M10 exceptions vs expected) — reference, don't teach.
- CPP-M01..M14 are written IN PARALLEL by other agents right now. Stay in your lane (per-item notes below). Each lesson
  is SELF-CONTAINED: if you need Reg/Field/Pin from a sibling lesson, embed a short local version in your own files.
  Never `#include` a file from another lesson.
- Parts escalate: A = core implementation, B = the hard part interviewers probe, C = firmware-realistic scenario.
- Tests are substantial and deterministic: table-driven, boundary values, fixed-seed PRNG (xorshift32) for any random
  test. Timing/benchmark numbers NEVER go in CHECKs — they live in a demo_*.cpp and the md shows real measured numbers.
- Every claim of "zero cost / same instructions / smaller / inlined" must be backed by real output you ran and pasted
  (asm, `size`, `nm`). If the result contradicts the lesson's expected story, say so honestly and explain why —
  that is a better lesson than a tidy made-up one.
- Compile-fail requirements: prove in the harness with `requires`-expressions / traits inside static_assert where
  possible, AND demo_*.cpp with `#ifdef SHOW_ERROR` blocks, with the real trimmed compiler error pasted in the md.
- Front matter: `updated: 2026-11-08`, `time:` 60분–90분.
- Output: ROOT/lessons/CPP/<ID>/2026-11-08_<ID>_<english_snake_slug>.md + starter.cpp + solution.cpp + practice.cpp
  (+ demo_*.cpp, small *.sh as needed). Title line `# <ID> <title exactly as in CPP.json>`.
- 면접 Q&A ≥ 6, each `<details><summary>Q1. ...</summary>`. ≥2 inline ```svg figures.
- Use a PRIVATE temp dir: `$SCRATCH/<ID>/` where SCRATCH is given in your prompt. Writers sharing a dir have clobbered each
  other's files before. Do NOT edit anything outside ROOT/lessons/CPP/<your ID>/. Do NOT run build_site.py.

## Cross-target recipes (verified 2026-11-08 — use these, don't rediscover)
Host: Apple clang 21 (arm64). Host demo = fine, but firmware claims need a Cortex-M object. Note Mac has no ld.lld and no
bare-metal C++ headers, so `#include <cstdint>` fails on `--target=thumbv7em-none-eabi` unless you do one of these:

(a) Mac clang, no std headers — put target-only code in a demo with NO includes (`using u32 = unsigned int;`):
    clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -fno-exceptions -fno-rtti -S x.cpp -o -
(b) Mac clang + arm-none-eabi headers (already copied; read-only for you, ARM_INC given in your prompt):
    clang++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti \
      -ffreestanding -nostdinc++ -isystem $ARM_INC/c++/14.2.1 \
      -isystem $ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard -isystem $ARM_INC -S x.cpp -o -
    (scripts you leave in the lesson must default ARM_INC like ROOT/lessons/CPP/CPP-A04/asm_targets.sh does)
(c) Real link / LTO / size on ELF — Lima VM `elinux` has arm-none-eabi-g++ 14.2.1:
    F="-std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -nostartfiles --specs=nosys.specs"
    arm-none-eabi-g++ $F [-flto] [-Wl,--gc-sections -ffunction-sections -fdata-sections] a.cpp b.cpp -o a.elf
    arm-none-eabi-size a.elf; arm-none-eabi-nm -C --size-sort a.elf; arm-none-eabi-objdump -d --no-show-raw-insn a.elf
    The Mac home is mounted READ-ONLY in the VM — copy into VM /tmp/<ID>/ and work there:
      limactl shell elinux -- bash -c 'mkdir -p /tmp/<ID> && cp <lesson dir>/* /tmp/<ID>/'
    Provide `_start` as the entry point. The newlib `_read/_write not implemented` link warnings come from nosys.specs — ignore.
    The VM may be stopped by another session: check `limactl list`; if stopped, run `limactl start elinux`.
Tools on Mac: `xcrun llvm-objdump`, `xcrun llvm-size`, `xcrun llvm-nm` (never /usr/local/bin/llvm-size — ancient LLVM 3.2).
llvm-objdump reads the ELF .o from recipes (a)/(b). godbolt is not available — say "로컬 clang/arm-none-eabi-g++ 비교".

## Verify (you MUST run, using your private temp dir)
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
    F="-std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti"
    c++ $F starter.cpp  -o $T/st && $T/st   # builds with 0 warnings, some FAIL, exit 1
    c++ $F solution.cpp -o $T/so && $T/so   # all PASS, exit 0
    c++ $F practice.cpp -o $T/pr && $T/pr   # all PASS, exit 0
    c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o $T/sa && $T/sa
    (same sanitizer run for practice.cpp)                                                 # clean
- solution.cpp = starter.cpp with only TODO bodies filled, so the diff highlights just the changes.
- Self-check: headings verbatim & in order, ≥2 ```svg with viewBox (width ≤ 760), Q&A ≥ 6, 추가 연습 3–6 ending in
  `### 한 번에 검증` + `<!-- code: practice.cpp -->`, `<!-- code: starter.cpp -->` in §3,
  `<!-- code: solution.cpp diff=starter.cpp -->` in §6.

## Per-item lanes and known traps
- **CPP-M01** Reg<Addr,T,Access>. Owner of "access rights in the type" and of "swap the bus for a mock" design (e.g. a Bus
  policy template parameter; `reinterpret_cast` is not allowed in constant expressions, so the address is an integral
  NTTP and the cast happens at access time). B12 owns function-template width dispatch — do not redo it. Show real
  MMIO path and mock path produce same API. Compile fail: write to RO, read from WO.
- **CPP-M02** Field<Reg,Offset,Width>. Owner of multi-field single RMW `write(F1=a, F2=b)`: exactly one read + one write
  on the mock log, and fields from a different register must not compile. Range check: compile-time for constants (NTTP/
  consteval) and runtime policy for variables. Probe W1C/reserved-bit hazards of RMW. Compare with C bit-fields
  (layout is implementation-defined, [class.bit]).
- **CPP-M03** Flags<E>. Owner of flag sets (B03 did single-value register fields; check B13's demo_flags.cpp and don't repeat it).
  Mixing enums = compile error (requires-expression proof). Must opt-in per enum, not a global operator| on every enum.
  ~ must mask to valid bits or say why not. Compare asm with raw uint32_t locally on arm64 and thumbv7em.
- **CPP-M04** Pin<Port,N>. Owner of the single-store GPIO HAL. set/clear via BSRR-style write-only (single STR, ISR-safe)
  vs toggle via ODR RMW (not atomic — say so, show how ISR interleaving corrupts on mock). Real thumbv7em disassembly compared
  to hand-written C, counting instructions; report honestly any difference (literal pool load etc.). M14 owns AF validation.
- **CPP-M05** CRTP. Owner of CRTP mechanics: protected ctor/friend trick to catch `class B : UartBase<A>`, missing-method
  error quality, `deducing this` (C++23) as modern alternative — mention only. Compare inlining with a virtual version briefly; M06 owns
  vtable accounting, so keep this to "was it inlined, and what happens to template code size with N instantiations" (bloat measured).
  Heterogeneous container impossibility + workaround.
- **CPP-M06** virtual cost. Owner of vtable/typeinfo accounting. Multi-TU (LTO only shows when the call crosses TUs). Measure with
  recipe (c): rtti on vs off (typeinfo symbols/size), final class, -flto, --gc-sections. Note a vtable keeps all virtual
  functions alive against gc-sections (a real cost). Deleting-destructor pulling in operator delete on bare metal. Cycles: DWT
  not available on Mac → instruction-level reasoning only, labelled as such; indirect-branch cost on M4 stated with a source or flagged unverified.
- **CPP-M07** StaticInstance<T>. Owner of placement new + lifetime rules: alignas storage, emplace/destroy state, double-construct
  and missed-destroy detection via counters, `std::launder` — when it is needed and when storing the returned pointer avoids it, `std::construct_at`/
  `destroy_at`, implicit-lifetime. Over-aligned types. Destruction order at program end vs firmware never exiting.
- **CPP-M08** class operator new + pool. Key trap: an allocation function that returns nullptr must be declared `noexcept` — only then does
  the new-expression null-check before running the constructor ([expr.new]); show generated code. Free-list algorithm is owned by DS (grep DS.json `see:` / pool item, reference it);
  here it's the C++ language hooks: sized delete, array new not covered by the class overload unless declared, inheritance with larger derived size (show the bug, guard it), double free detect in pool.
- **CPP-M09** move cost. Owner of move-vs-copy cost in fixed-capacity members: instrumented type; vector steal vs inline array element-wise; `noexcept` move → `move_if_noexcept` on reallocation. Confirm what actually happens under `-fno-exceptions`
  by running it, rather than assuming. MCPP-B03 owns writing a move ctor. Vector is host-only here; say so.
- **CPP-M10** static_vector<T,N>. Owner of the container: storage (M07 owns its rules — cite), full API, erase shifting with correct lifetime, iterators (contiguous; pointers OK), capacity-overflow policy. Conditional trivial
  destructor (`~static_vector() requires std::is_trivially_destructible_v<T> = default;`) proven by trait. Copy/move ops counted. Exception-safety N/A under -fno-exceptions — say so. Mention C++26 `std::inplace_vector`, ETL.
- **CPP-M11** Result<T,E>. NOTE overlap: CPP-B13 lesson already built `[[nodiscard]] Result<T,E>` + and_then. Do the item's task (I2C sensor API, propagation chain, ignored-return warning) but the *new content* is:
  error-category design (hierarchical enum, mapping bus error → driver error → app error, keeping context), Result<void,E>, a `TRY`-style propagation macro with statement expressions (clang/gcc extension — flag it), and code-size/asm comparison on thumbv7em
  of error-code vs Result vs C++23 std::expected (needs -std=c++23; run recipe (b) or (c) and confirm it compiles there). `[[nodiscard]]` bypass via `(void)`. MCPP-M10 owns the exception cost comparison.
- **CPP-M12** Delegate. Owner of object-ptr + fn-ptr delegate: bind free function, member function (NTTP member pointer), const member, capture-less lambda; lifetime/dangling. `function_ref` (C++26) compare. Compare to std::function: sizeof on libc++ AND libstdc++ (recipe b),
  allocation count by replacing global operator new in a demo (the portable equivalent of a "malloc 훅" — say that), asm of call. Timer callback table firmware scenario. MCPP-M07 owns SBO implementation.
- **CPP-M13** constexpr LUT. CRC-32 table + Q15 sin. `std::sin` is not constexpr in C++20 (a compiler may fold it as an extension — don't depend on it): implement own constexpr series/recurrence and validate error bound against host `std::sin` at runtime in tests. Verify on ELF (recipe a/b + llvm-objdump -s -h): in .rodata, no .init_array, no .bss. Contrast with a non-constexpr (dynamic init) version that
  DOES emit init code. constinit vs constexpr. B04/B09 are cross-references, not topics.
- **CPP-M14** static_assert HAL. Owner of Pin×AF table validation with readable messages. C++20 static_assert message must be a string literal (user-generated messages are C++26, P2741) — so show techniques for making the error point at the offending pin/AF (e.g. template name in the "requested here" chain, table lookup at consteval). Dependent-false pitfall: `static_assert(false)` in a template (CWG2518, allowed in newer compilers — verify on Apple clang 21 and report). Real error texts pasted. Concepts rewrite only pointed to (see MCPP-M05). M04 owns Pin itself; use a tiny local one.

## Accuracy
Double-check every standard claim (C++17/20/23/26 availability, UB vs implementation-defined, what compilers guarantee). Cite [expr.new], [class.bit], [basic.life], [ptr.launder], [dcl.fct.def] etc. where useful. If you could not verify something, write "미확인" in the md rather than asserting it. Don will notice.

## When done
Reply (short): files written, PASS counts starter/solution/practice, sanitizer result, demo files + what they really showed (one line), anything you were unsure about or could not verify.
