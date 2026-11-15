# Lesson writer brief — track TOOL (툴체인·빌드·디버깅), Basic level

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
Don: embedded/RF integration + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math,
prepping for embedded/firmware/silicon-validation interviews (Anduril, Apple, NVIDIA, Rivian, Tesla...).
He re-checks fundamentals by DOING: every lesson = one interview-style hands-on problem.
TOOL-B is the **toolchain basics** track: compile stages, warnings, -O levels, object files, link errors,
size, preprocessor variants, Make, static libs, lldb, ASan, UBSan, log macros, cross-compiling — the things
a firmware engineer must use daily and explain in interviews ("이 링크 에러 왜 나?", "map/size 보고 RAM 얼마 써?").

Already written (assume he knows them, cross-reference by id, don't re-teach): C Basic/Medium/Advanced
(C-B02 integer promotion, C track covers UB concepts), DS B/M/A, CPP B/M/A, ALGO-B/M/A, ARCH-B/M/A, ARM-B
(ARM-B02 reading clang -S output, ARM-B09 lldb on asm strlen, ARM-B12 Thumb code density with objdump),
OS-B/M, COMM-B/M/A, RTOS-B/M/A, BM-B/M (BM-B08 real Cortex-M3 boot on QEMU with linker script + vector table),
SIV-B/M/A, TOOL-M09 (TSan — already exists in lessons/TOOL/TOOL-M09, read it for tone).
Not written yet (cite by id only, don't teach their content): TOOL-M01 (linker script), TOOL-M02 (map file
analyzer), TOOL-M03 (custom sections), TOOL-M04/M05 (CMake), TOOL-M06 (shared libs), TOOL-M07 (watchpoints,
lldb scripting), TOOL-M08 (git bisect), TOOL-M10 (leaks/Guard Malloc/valgrind), TOOL-M11 (core dumps),
TOOL-M12 (static analysis), TOOL-M13 (size diet: gc-sections/LTO/newlib-nano), TOOL-M14 (DWARF/addr2line),
TOOL-A03 (gdb remote/OpenOCD), TOOL-A06 (reproducible builds), TOOL-A08 (stack usage), TOOL-A10 (-Rpass),
TOOL-A11 (mini readelf = full ELF parser).

## Read first (required)
1. ROOT/NOTES_SPEC.md — lesson rules. Follow EXACTLY (front matter, section headings verbatim and in order,
   svg rules, code rules, `<!-- code: ... -->` markers, `<details>` folding).
2. ROOT/curriculum/LESSON_WRITER_BRIEF.md (base brief) — its Output / Quality bar / platform-reality rules apply
   unless overridden here.
3. ROOT/curriculum/tracks/TOOL.json — your item (title/goal/task/tags/env/env_note). The lesson must do the
   item's `task` completely (every flag, tool, measurement, table it names). Skim all TOOL items (B/M/A) to
   stay in your lane.
4. Reference lessons: ROOT/lessons/C/C-B01/ (Basic depth, CHECK macro, "N/M passed"),
   ROOT/lessons/TOOL/TOOL-M09/ (existing TOOL lesson: tool-output pasting style, demo files),
   ROOT/lessons/ELX/ELX-M04/check.sh (an existing script-driven checker, for Form S).

## Output
ROOT/lessons/TOOL/<ID>/2026-11-15_<ID>_<english_snake_slug>.md + code files (see Forms below)
(+ demo_*.c / demo_*.sh, captured *.txt outputs, small project subdirs).
Front matter `updated: 2026-11-15`, realistic `time:` (40분–70분). Title line `# <ID> <title exactly as in TOOL.json>`.
Size ~420–650 md lines. Q&A ≥ 6 (aim 7–8), each `<details><summary>Q1. ...</summary>`. ≥ 2 inline ```svg figures
(viewBox, width ≤ 760, currentColor + f-acc/f-ok/f-warn/f-mut only, no font-family except monospace,
font-size 12–14, nothing outside the viewBox, escape &lt; &gt; &amp;). Good figures for this track: the
pipeline .c → .i → .s → .o → exe with tool per arrow, object file box (header / sections / symtab / relocs),
symbol resolution arrows across .o and .a members, Flash/RAM layout bars with text/data/bss (data copied
Flash→RAM at boot), Make dependency DAG with what `touch x.h` invalidates, sanitizer shadow-memory mapping,
debugger stop/step state, cross-compile triple anatomy (arch-vendor-os-abi). Numbers in figures must match
your real output.

Korean, plain words, English term in parentheses on first use. First paragraph of 1. 문제: one line on why it
matters in firmware work/interviews. Tie to Don's reality (SSD controller firmware builds, Cortex-M MCUs with
arm-none-eabi-gcc, Flash/RAM budgets, CI builds) — firmware uses ELF/GNU tools, this Mac uses Mach-O/ld64:
ALWAYS show the real Mac output AND the ELF/GNU equivalent (real output from the Lima VM where possible).

## Two lesson forms — pick the one your lane says; the checker uses exactly this contract
**Form C (C program)** — starter.c / solution.c / practice.c, each self-contained with the CHECK harness:
    export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk     # 27.2 SDK link is broken
    cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/<ID>_s && /tmp/<ID>_s   # some FAIL, exit 1 (never hang)
    cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/<ID>_x && /tmp/<ID>_x   # all PASS, exit 0, < 2 s
    (same for practice.c) + ASan/UBSan: cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined
  solution.c = starter.c with ONLY TODO bodies filled (same line structure → site diff highlights changed lines).
  Tool experiments live in demo_*.sh / demo_*.c; paste their REAL output in the md.

**Form S (script-driven)** — the lesson dir has `check.sh`; the checker runs ONLY:
    bash <dir>/check.sh starter     → must print PASS/FAIL lines + "N/M passed", exit ≠ 0
    bash <dir>/check.sh solution    → all PASS, exit 0
    bash <dir>/check.sh practice    → all PASS, exit 0
  check.sh (from any cwd: `cd "$(dirname "$0")"`) builds/links/runs the variant's files with real tools and
  asserts on results (exit codes, tool output via grep, nm/size output, make -n output…). Variant files are
  whatever fits the lane: starter.sh/solution.sh, starter.mk/solution.mk, starter.flags/solution.flags,
  starter.txt/solution.txt (prediction tables), starter/ and solution/ project subdirs, starter.c/solution.c…
  Rules: `set -u`, work only in a private dir `W=/tmp/<ID>_work/<variant>` (rm -rf + mkdir at start), never
  write into the lesson dir, export the SDKROOT above inside the script, deterministic, finishes < 60 s, no
  network, never hangs (`timeout`-free macOS: keep commands bounded). Each variant file must be readable on the
  site: use `<!-- code: starter.mk -->` and `<!-- code: solution.mk diff=starter.mk -->` markers (any extension
  works) and also `<!-- code: check.sh -->` once (in 3. 시작 코드). If check.sh needs the Lima VM, call
  `limactl shell elinux -- bash -lc '...'` (VM `elinux` is running — never stop it; Mac home is mounted
  READ-ONLY in the VM → copy to VM /tmp/<ID>_work first) and make the VM part a clearly separated section.
  Show `bash check.sh starter` / `solution` real output in the md.

## Environment facts (verified 2026-11-15 on this Mac — M6, macOS 27.2, Apple clang 21)
- Use `/usr/bin/objdump` (Apple LLVM 21 — reads Mach-O AND ELF: `-h -t -r -d --private-headers`),
  `/Library/Developer/CommandLineTools/usr/bin/llvm-size -A` (works on ELF .o), `llvm-nm` (CLT), `nm`, `otool`,
  `/usr/bin/size -m` (Mach-O only; no -A). **/usr/local/bin/llvm-objdump and /usr/local/bin/llvm-size are LLVM
  3.2 — broken, never use them** (they are first on PATH! use full paths). No readelf / ld.lld / arm-none-eabi /
  cmake / gdb / valgrind / cppcheck / clang-tidy on the Mac.
- Mac clang targets: aarch64/arm/thumb/x86 only — **no RISC-V on Mac**. `clang --target=aarch64-linux-gnu -c`,
  `--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -c`, `--target=armv7a-none-eabi` work
  (object only; `-ffreestanding`, only <stdint.h>/<stddef.h>/<stdbool.h> headers for bare targets).
  Rosetta 2 is present (`-arch x86_64` builds run).
- Mac make = **GNU Make 3.81** (2006: no `.ONESHELL`, no `$(file ...)`, no grouped targets `&:`, no
  `--output-sync`); VM make = 4.4.1. Write Makefiles that work on 3.81 and say which features differ.
- Mac ASan: `detect_leaks` is NOT supported ("AddressSanitizer: detect_leaks is not supported on this
  platform", abort rc 134) → leaks via VM clang/gcc LSan, or `MallocStackLogging=1 leaks --atExit -- ./prog`
  (works, no sudo; TOOL-M10 owns that tool in depth). UBSan trap mode → SIGTRAP, shell rc 133.
- lldb batch works: put commands in demo_*.lldb, run `lldb -b -s demo_x.lldb ./prog`; paste the real transcript.
- Lima VM `elinux` (aarch64 Ubuntu): gcc 15.2, clang 21.1.8 (has riscv32/riscv64 targets), ld.bfd, ld.lld,
  readelf, arm-none-eabi-gcc 14.2.1 (+ newlib/nano), aarch64-linux-gnu-gcc, riscv64-linux-gnu-gcc, cmake 4.2,
  make 4.4.1, valgrind, gdb-multiarch, qemu. **Native `gdb` in the VM fails** ("Unable to fetch SVE/SSVE vector
  length") → for gdb demos use `qemu-aarch64 -g <port>` + `gdb-multiarch`, or show gdb commands as a table only.
  Run: `limactl shell elinux -- bash -lc '...'`.
- Private temp names only: /tmp/<ID>_* and /tmp/<ID>_work/ (Mac and VM). 13 other writers run in parallel —
  never write to shared paths or another lesson dir.
- Paste only REAL output. Never invent numbers, warnings, error text, addresses. Timings are noisy (parallel
  jobs on this machine): median of several runs, hedge.

## Lanes (stay in yours; cross-ref the others by id)
- **B01 (Form S)** compile stages. Project: main.c + util.c + util.h + config.h (macros). demo_stages.sh runs
  `-E` (.i, line count, `#` linemarkers), `-S` (.s), `-c` (.o), link, plus `-save-temps`, `-ccc-print-phases`,
  `-###`, `-v` include search list, `-H` header tree — capture real output. Coding: `starter.sh` defines a bash
  function `first_failing_stage <file.c>` (TODO) that runs the stages in order (preprocess → compile
  (-fsyntax-only vs -S: say which errors only appear at -S/codegen) → assemble (-c) → link) and prints
  preprocess|compile|assemble|link|ok; check.sh sources the variant and checks it on ≥ 10 broken snippets
  (bad_*.c in the lesson dir: #error, missing header, unterminated #if, syntax error, type error, invalid inline
  asm mnemonic (verify: clang -S does not validate it but -c does?), undefined symbol, duplicate symbol across
  two TUs (bad pair), missing main, ok file) against an expected table. Diagnostics classifier table in md
  (which message text belongs to which stage: clang vs ld64 "Undefined symbols for architecture arm64" vs GNU
  ld "undefined reference to" — show the GNU one from the VM). Practice: `stage_artifact_kind` (file(1)/magic of
  .i/.s/.o/exe) or extra snippets.
- **B02 (Form S)** warning flags. `bugs12.c` = 12 deliberate bugs each on a line tagged `// BUG01`… (uninitialized
  use, signed/unsigned compare, shadowing, printf format mismatch, implicit int-conversion truncation, sign
  conversion, missing return, unused result of a warn_unused_result fn, fallthrough without annotation,
  `if (x = 0)`, sizeof on array parameter, implicit fallthrough/enum switch missing case, pointer-to-int cast,
  comparison always true due to type range, etc.). `good.c` = similar realistic code with no bugs.
  Variant = a flags file (starter.flags = `-Wall`; solution.flags = recommended set). check.sh compiles bugs12.c
  with the variant's flags and asserts EVERY BUGnn line gets a diagnostic (map diagnostic line numbers → tags),
  and good.c compiles warning-free with `-Werror` (no noise). The md table: bug × flag (-Wall, -Wextra,
  -Wshadow, -Wconversion, -Wsign-conversion, -Wformat=2, -Wimplicit-fallthrough, -Wswitch-enum, -Wcast-align,
  -Wpedantic, -Wstrict-prototypes, -Wmissing-prototypes, -Wnull-dereference, -Wvla, -Wdouble-promotion…) built
  from REAL runs (demo_matrix.sh generates it), plus GCC-only flags from the VM gcc (-Wlogical-op,
  -Wduplicated-cond, -Wduplicated-branches, -Wjump-misses-init) with real gcc output, and which bugs NO warning
  catches at -O0 vs -O2 (uninitialized analysis depends on optimization in GCC — verify). Final recommended
  project baseline + `-Werror` policy (CI only? third-party headers via -isystem). Practice: practice.flags for a
  stricter "firmware" set, or `#pragma clang diagnostic` scoped suppression test.
- **B03 (Form S)** optimization levels. demo_bench.c (deterministic CPU work: e.g. checksum + small sort + loop)
  built at -O0 -O1 -O2 -O3 -Os -Oz → `.text` size (`size -m` / objdump -h on Mac; and arm-none-eabi-gcc -O0..-Os
  + `arm-none-eabi-size` in the VM — firmware numbers) and run time (median) → real table. lldb at -O2: show the
  real "variable not available"/optimized-out experience (demo_optout.lldb transcript) vs -O0 and `-Og` (clang
  treats -Og like -O1 — verify/hedge). Coding: starter.c has 3–4 functions whose behaviour CHANGES with -O level
  because of UB (signed overflow check `x + 1 > x`, side-effect-free infinite loop, strict-aliasing type pun,
  uninitialized read / out-of-bounds read used as loop bound — ONLY ones you actually observe diverging);
  TODO = rewrite UB-free (__builtin_add_overflow or range check, memcpy pun, …). check.sh builds the variant at
  all 6 levels and asserts identical, correct output at every level (+ solution clean under UBSan). Starter must
  really diverge (verify!). Practice: `volatile` misuse vs correct use for a busy-wait delay measured at -O2.
- **B04 (Form S)** object-file anatomy. anatomy.c with: global init, global zero/uninit, static (file-scope),
  static local, const global, string literal, extern variable use, extern function call, static function,
  weak function, `__attribute__((used))` / unused static (dropped at -O2?), tentative definition (note -fno-common
  default → B not C). Build Mach-O (`cc -c`) and ELF (`clang --target=aarch64-linux-gnu -c`). Coding = prediction:
  starter.txt / solution.txt table `symbol  macho_nm_letter  elf_nm_letter  elf_section` (starter has `?`), and a
  relocation table `call/ref  macho_reloc_type  elf_reloc_type` (ARM64_RELOC_BRANCH26 / PAGE21 / PAGEOFF12 vs
  R_AARCH64_CALL26 / ADR_PREL_PG_HI21 / ADD_ABS_LO12_NC…). check.sh builds both objects and compares the
  predictions with real `nm` / `llvm-nm` / `objdump -r` output. md walks through real `nm -m`, `objdump -h -t -r -d
  --private-headers`, `otool -l` (load commands: LC_SEGMENT_64 / __TEXT,__text…), and VM `readelf -S -s -r -h`
  outputs; Mach-O section ↔ ELF section mapping table (__TEXT,__text ↔ .text, __TEXT,__cstring ↔ .rodata.str1.1,
  __TEXT,__const ↔ .rodata, __DATA,__data ↔ .data, __DATA,__bss/__common ↔ .bss). Explain why .o addresses are 0
  and relocations are "holes". Lane: no ELF parsing in C (TOOL-A11), no linker scripts (TOOL-M01).
- **B05 (Form S)** link errors. 4 scenario subdirs, each with starter/ and solution/ sources (or one starter and
  one solution tree): (1) undefined reference (declared, never defined / typo / missing .o in link line / static
  function used from another TU), (2) multiple definition from a variable DEFINED in a header included twice
  (fix: extern in .h + one definition; show `-fcommon` history hedged), (3) C/C++ name mangling: C++ caller of a C
  function without `extern "C"` → `__Z…` undefined (use c++filt), (4) weak default handler: startup-style
  `void USART1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")))` — Mach-O has no alias attribute
  (verify!) → use weak definition on Mac, show alias version for ELF in the VM with arm-none-eabi-gcc + nm
  showing `W`; the strong definition in another file overrides it; the classic bug: handler name typo → silently
  stays weak default (catch it with nm in check.sh). check.sh links each scenario variant, asserts link
  success/failure and runtime output, and greps nm for evidence (T vs W, U). md: real ld64 messages and real
  GNU ld messages (VM) side by side, how to read them (which .o references, which symbol). Practice: duplicate
  symbol in two static libs (first wins — cite B09), or `-Wl,-undefined,error` vs dynamic_lookup.
- **B06 (Form C)** size / sections. demo_size.sh: variants of a small firmware-like file (global array
  initialized vs zero vs uninit, `const` table, string literals, adding printf) compiled for
  `--target=thumbv7em-none-eabi -mcpu=cortex-m4 -Os -c` on the Mac (llvm-size -A) AND fully linked in the VM with
  arm-none-eabi-gcc (`--specs=nano.specs --specs=nosys.specs`, `arm-none-eabi-size` Berkeley + `-A`) — capture
  real outputs into size_*.txt in the lesson dir. Coding: starter.c parses `size -A`-style text (section name,
  size, addr) and Berkeley `text data bss dec hex` lines → `flash_bytes()` = text + data (+ .rodata/.isr_vector
  /.ARM.exidx… classified as Flash) and `ram_bytes()` = data + bss (+ heap/stack sections if present), plus a
  budget check `fits(flash_limit, ram_limit)`; tests run on the captured txt files embedded as strings (keep
  self-contained) with expected numbers from the real outputs. Explain why `.data` counts twice (LMA in Flash,
  VMA in RAM, copied by startup — cite BM-B08), why Berkeley `text` already includes .rodata, why printf
  costs KBs (real delta), Mac `size -m` segment view for contrast. Lane: no map-file parsing (TOOL-M02), no
  gc-sections/LTO (TOOL-M13).
- **B07 (Form S)** preprocessor build variants. board_config.h: BOARD_A / BOARD_B (exactly one required),
  BUILD_DEBUG / BUILD_RELEASE, feature flags (e.g. FEATURE_USB only on BOARD_B), derived macros (LED_PIN,
  UART_BAUD, LOG_LEVEL, ASSERT macro on/off) and `#error` / `_Static_assert` for illegal combos. starter's header
  has TODO checks missing; check.sh builds a matrix of -D combinations (both boards, none, DEBUG+RELEASE, USB on
  BOARD_A, valid ones…) and asserts: illegal → compile fails WITH your #error text; legal → `-E -dM` shows the
  expected derived values and `-E` output contains/omits the right code path (grep). md: `-dM -E - </dev/null`
  predefined macros (Mac arm64 vs `--target=thumbv7em-none-eabi` vs arm-none-eabi-gcc: __ARM_ARCH, __thumb__,
  __ARM_FP…, real diffs), `#if` vs `#ifdef` with undefined macros (-Wundef), `defined()` gotchas, IS_ENABLED-
  style trick (Linux Kconfig) as optional. Practice: a `BOARD` value macro (`-DBOARD=2`) style instead of flags.
- **B08 (Form S)** Make. Project src/ (5–6 .c), include/ (3 .h), build/ out-of-tree. starter.mk has TODOs (pattern
  rule `$(BUILD)/%.o: src/%.c`, automatic vars $@ $< $^, `-MMD -MP`, `-include $(DEPS)`, order-only `| $(BUILD)`
  dir prerequisite, `.PHONY`, CFLAGS change not tracked (mention)). check.sh copies the project to the work dir
  and asserts with real make (Mac GNU Make 3.81): full build OK; second `make` → nothing to do; `touch
  include/x.h` → `make -n` lists exactly the dependent objects (+ relink) and nothing else; delete a header that
  is no longer included → no "No rule to make target" error thanks to -MP; `make -j8` from clean works; `make
  clean`. Also run the solution in the VM with make 4.4.1 (`--trace` output is nice there). md: dependency DAG
  figure, `make -d`/`-p` snippets, recursive vs non-recursive make (brief), 3.81 vs 4.x differences. Practice:
  add a second binary (unit test target) sharing objects, or debug/release build dirs via variable.
- **B09 (Form S)** static libraries & link order. libfoo.a needs libbar.a and libbar.a needs back into libfoo.a
  (one member each way), plus an unused member. `ar rcs`, `ar t`, `nm -s`/`ranlib` index. check.sh: Mac ld64
  (order-insensitive? verify for real and explain how ld64 resolves archives), then in the VM GNU ld (gcc / -fuse-
  ld=bfd) with orders `main.o -lfoo -lbar` (fails? verify), `-lbar -lfoo`, `-lfoo -lbar -lfoo`,
  `-Wl,--start-group -lfoo -lbar -Wl,--end-group` (and ld.lld behaviour — lld remembers archive symbols: verify);
  variant files = the link lines to use (starter.link with the failing order / solution.link fixed). Evidence that
  only needed members are pulled: `-Wl,-map,x.map` (Mac) / `-Wl,-Map=x.map` + `--trace` / `-Wl,-y,sym` (GNU) →
  "Archive member included to satisfy reference by file (symbol)" section; unused member absent. md: algorithm
  figure (GNU ld single pass over archives with undefined set). Practice: duplicate symbol in two archives →
  first archive in order wins silently (real demo), or `-Wl,--whole-archive` for constructor/registration
  objects (cite TOOL-M03).
- **B10 (Form C)** debugger basics. starter.c = singly linked list (insert, find, delete, delete_all_matching,
  free) with a delete bug that crashes / corrupts (e.g. not updating head when deleting the first node, use of
  node after free in delete_all, prev pointer not advanced) — the TODO is to fix it. Harness runs each test in a
  fork()ed child so a crash becomes a FAIL line (report signal), starter exits 1 without hanging. demo_debug.lldb
  = REAL session on a debug build (-O0 -g) of the buggy version: `b list_delete`, `run`, `bt`, `frame variable`,
  `p *node`, `p node->next->value`, `n`/`s`/`finish`, `up`/`down`, `watchpoint` only if needed, crash stop →
  `bt` + `frame select` → find the dangling pointer. Paste the real transcript; step table. lldb ↔ gdb command
  table (break, run, bt, frame, info locals, print, x/…, finish, until, display, watch); show gdb for real if you
  can (VM: gdb-multiarch + qemu-aarch64 -g, since native gdb fails there) — else table only and say so.
  Practice: second planted bug (off-by-one in a list_nth / missing NULL check) found with lldb, test in
  practice.c. Lane: no watchpoints/scripting depth (TOOL-M07), no core dumps (TOOL-M11).
- **B11 (Form S)** ASan. A small "packet buffer" module (buf.c) + test driver with cases selectable by argv:
  heap-buffer-overflow, stack-buffer-overflow, global-buffer-overflow, use-after-free, double-free, leak.
  starter = buggy module, solution = fixed. check.sh builds the variant with `-fsanitize=address -g -O1` and
  runs each case in its own process → asserts exit 0 and no "ERROR: AddressSanitizer" (leak case: run in the VM
  with clang/gcc LSan, or Mac `leaks --atExit`, since Mac ASan has no detect_leaks). Capture the REAL ASan
  reports for each bug kind (asan_*.txt) and annotate in md: error type line, READ/WRITE of size N, the 3 stacks
  (access / freed by / previously allocated by), "located N bytes after M-byte region", shadow bytes legend
  (fa/fd/f1/f2/f3/f8/f9) and the shadow address formula (Shadow = (Addr >> 3) + Offset; hedge offset per
  platform). Cost: real slowdown + memory overhead numbers on a small benchmark, why ASan is not used on Cortex-M
  firmware (hedge: KASan/HWASan exist on Linux/Android). Lane: leaks/Guard Malloc/valgrind depth = TOOL-M10.
- **B12 (Form S)** UBSan. cases: signed overflow (+, *, negation of INT_MIN), shift (by ≥ width, negative,
  into sign bit — C vs C++ rules hedged), misaligned load via cast pointer, load of invalid bool/enum value
  (-fsanitize=bool,enum), NULL deref (-fsanitize=null), array index out of bounds (-fsanitize=bounds with known
  array size), division by zero, INT_MIN/-1, float→int overflow (-fsanitize=float-cast-overflow — not in
  -fsanitize=undefined by default? verify). starter = buggy functions (firmware-ish: ADC scaling, register
  shift, packet parsing with misaligned uint32 reads), solution = UB-free rewrites (memcpy, unsigned arithmetic,
  range checks). check.sh builds with `-fsanitize=undefined -fno-sanitize-recover=all` and runs each case →
  asserts no "runtime error:". Capture real UBSan messages (ubsan_*.txt). Trap mode: `-fsanitize=undefined
  -fsanitize-trap=undefined` (SIGTRAP, rc 133 — show `brk`/`udf` in the disassembly) and the code-size cost
  measured for real on a Cortex-M object (`clang --target=thumbv7em-none-eabi -Os -c` ± trap flags, llvm-size)
  and in the VM with arm-none-eabi-gcc (`-fsanitize=undefined -fsanitize-undefined-trap-on-error`); table of
  sizes; how firmware uses it (trap → HardFault → crash log). `-fsanitize-minimal-runtime` mention. Lane: UB
  theory is the C track (cite C ids you find in tracks/C.json by grep), don't re-teach it.
- **B13 (Form C)** compile-time removable log macros. log.h-style code inside starter.c: `LOG_E/W/I/D(fmt, ...)`
  with `LOG_LEVEL` compile-time threshold, `__FILE__`/`__LINE__`/`__func__` prefix (basename only via a
  compile-time trick or `__FILE_NAME__` (clang) — verify), level tag, output goes through a sink function
  pointer so tests capture the text into a buffer, `__attribute__((format(printf, …)))` on the sink so
  `LOG_I("%s", 42)` warns, disabled levels compile to `do { if (0) log_sink(...); } while (0)` (keeps format
  checking, evaluates no args) — test: `LOG_D("%d", n++)` leaves n unchanged when disabled, enabled-level text
  exact match. `##__VA_ARGS__` (GNU) vs C23 `__VA_OPT__` vs C11 strict (-Wpedantic) — pick one, explain.
  demo_size.sh: build at each LOG_LEVEL for thumbv7em (-Os -c, llvm-size) and Mac; count format strings in
  the object (`strings`/objdump -s of __cstring/.rodata) → real table showing strings disappear. md: tokenized/
  deferred logging idea (store format-string id + args, decode on host — e.g. Zephyr dictionary logging, trice;
  hedge) as Part C or 추가 연습. Practice: rate-limited LOG_ONCE / LOG_EVERY_N, or compile-time module levels.
- **B14 (Form C)** cross-compile first steps. demo_cross.sh: same `target.c` (freestanding: uses stdint, a struct,
  a float multiply, a loop) compiled to: aarch64-linux-gnu (Mac clang), thumbv7em-none-eabi -mcpu=cortex-m4
  -mfloat-abi=hard -mfpu=fpv4-sp-d16 (Mac clang; and soft for contrast), riscv32 (VM clang
  `--target=riscv32-unknown-elf -march=rv32imac -mabi=ilp32`, plus rv32imafc/ilp32f), x86_64 Mach-O (Mac) —
  capture `readelf -h` (VM) / `objdump --private-headers` / disassembly snippets into txt files; copy the small
  .o files into the lesson dir (objs/, each < 4 KB). Show REAL link failures: `clang --target=aarch64-linux-gnu
  t.c` on the Mac (no sysroot / crt1.o / libc / linker) and `--target=thumbv7em-none-eabi` link (no crt0/libc),
  explain triple anatomy, sysroot, `--gcc-toolchain`, why arm-none-eabi-gcc ships newlib. Coding: starter.c =
  `elf_target_info(const uint8_t *buf, size_t n, struct target_info *out)` reading ONLY the ELF header (e_ident
  class/data/OSABI, e_type, e_machine → name: EM_ARM 40, EM_AARCH64 183, EM_RISCV 243, EM_X86_64 62; e_flags
  decode: ARM EABI version (e_flags>>24) + EF_ARM_ABI_FLOAT_HARD 0x400 / SOFT 0x200; RISC-V EF_RISCV_RVC 0x1 +
  float ABI bits (0x0 soft / 0x2 single / 0x4 double)) — verify every constant against the real readelf -h
  output of your objects; tests embed the first 64 bytes of each real .o as byte arrays (self-contained) and
  compare with readelf's answers; malformed input (short buffer, bad magic) rejected. Also detect Mach-O magic
  (0xFEEDFACF) and say "not ELF". Lane: header only — sections/symbols/phdrs = TOOL-A11.

## Interview flavour
- 2. Problem (English) = real interview/on-the-job prompt: "Here is a build that fails with X — find the stage /
  fix the Makefile / make this warning-clean / explain this size output / why does this link only in one order".
  Give exact constraints (no new files, fix in the header only, keep -Werror…) and example output.
- 면접 Q&A = real follow-ups: what each stage outputs, what a relocation is, why `static` changes nm letter,
  common vs -fno-common, weak symbols and their dangers, why .data costs Flash AND RAM, -O2 vs -Os for firmware,
  why `volatile`, why -Werror only in CI, how the linker searches archives, ASan vs valgrind, UBSan trap in
  production, how to cross-compile and what a sysroot is.

## Quality bar
- Parts A/B/C escalate: A = core mechanic with the real tool, B = edge cases / traps (injected bugs the checker
  catches), C = firmware-realistic use (Cortex-M build, Flash/RAM budget, startup weak handlers, CI flags,
  board variants).
- 풀이 detailed: real tool output annotated line by line, tables built from real runs, why each fix works.
- Accuracy over volume. Every flag behaviour, message text, letter code, and size number comes from a real run
  on this Mac or in the VM (say which). From-memory facts (GNU ld internals, history like -fcommon default
  change in GCC 10 / Clang 11, Make 3.81 limitations) hedged ("확인 권장") unless verified. Don will notice.

## When done
Reply with: files written (paths), md line count, Form (C or S), PASS counts for starter/solution/practice,
ASan/UBSan result (Form C), demo files + key real numbers, and every fact you were unsure about. Do NOT edit
files outside ROOT/lessons/TOOL/<your ID>/ (plus your private /tmp paths).
