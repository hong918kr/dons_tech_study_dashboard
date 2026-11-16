# Lesson writer brief — track TOOL (툴체인·빌드·디버깅), Medium level

ROOT = /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note

You write ONE lesson (the item id given in your prompt) for Don's embedded study notes.
This brief = **Medium overrides on top of ROOT/curriculum/TOOL_B_LESSON_WRITER_BRIEF.md**. Read the TOOL-B brief
fully first: its Forms (C / S) contract, environment facts, interview flavour and quality bar all apply unless
changed below.

TOOL-M is the **toolchain medium** track: linker scripts, map files, custom sections, CMake (+ cross toolchain
files), shared libraries, advanced debugger use, git bisect, leak tools, core dumps, static analysis, size diet,
DWARF symbolication — what a senior firmware engineer is expected to drive without help, and explain in
interviews ("링커 스크립트에서 .data VMA/LMA 설명해봐", "이 맵 파일에서 RAM 누가 먹어?", "스트립된 바이너리의
크래시 주소를 라인으로 바꿔봐", "회귀 커밋 어떻게 찾아?").

Already written (assume he knows them, cross-reference by id, don't re-teach): everything listed in the TOOL-B
brief + **all of TOOL-B01–B14** (B04 object anatomy, B05 link errors/weak, B06 size text/data/bss with fw.ld,
B08 Make + -MMD (proj/ subdir), B09 static libs & link order, B10 lldb basics, B11 ASan, B12 UBSan, B14 cross
compile & triples), TOOL-M09 (TSan), BM-B05 (MCU memory map, reading a map file by eye), BM-B06 (bootloader+app
flash split), BM-B08 (C startup boot on QEMU), BM-A09 (crash dump in .noinit), ARM-M12 (HardFault CFSR decode),
ARM-M14 (crash register dump back-trace). Skim the ones next to your lane (ls ROOT/lessons/<TRACK>/) so you
cite precisely and don't duplicate.
Not written yet (cite by id only): TOOL-A03 (gdb remote), TOOL-A05 (crash dump format+decoder), TOOL-A06
(reproducible builds/build-id), TOOL-A07 (advanced linker script: RAM funcs/KEEP/ASSERT/alignment in depth),
TOOL-A08 (stack usage), TOOL-A10 (-Rpass), TOOL-A11 (mini readelf).

## Overrides vs TOOL-B
- Output: ROOT/lessons/TOOL/<ID>/2026-11-15_<ID>_<english_snake_slug>.md, front matter `updated: 2026-11-15`,
  `time:` 60분–90분. Title line `# <ID> <title exactly as in TOOL.json>`.
- md **450–720 lines** (pasted real output counts, but trim long dumps to the lines that matter + "…").
  ≥ 2 inline ```svg figures (≥ 3 encouraged), **면접 Q&A ≥ 7** (aim 8), 추가 연습 3–6 with collapsible answers
  and exactly one `<!-- code: practice... -->` marker.
- Parts A/B/C: A = core mechanic with the real tool, B = traps (the starter's injected bugs, each one a thing
  that really bites in practice), C = firmware-realistic use (Cortex-M image, CI job, field crash, size budget).
  At least **3 distinct injected bugs** in the starter that the check catches (each its own FAIL line).
- Medium depth: explain the *why* underneath the tool (what the linker actually does with the script, how
  rpath search order works, why bisect needs 125, how DWARF line tables map PC→line). Not just recipes.
- Tool installs done on 2026-11-15 (verified): **Mac** `cmake 4.5.0` (/opt/homebrew/bin/cmake), `ninja`,
  `cppcheck 2.22.0`, `arm-none-eabi-gcc 16.2.0` + binutils (`arm-none-eabi-ld/-readelf/-objdump/-size/-nm/
  -objcopy/-addr2line/-gdb`) in /opt/homebrew/bin — **this Mac arm-none-eabi-gcc has NO newlib** (no
  nano.specs, no libc.a): bare-metal builds on the Mac must be `-nostdlib -ffreestanding` with your own
  startup/memcpy; anything needing newlib / newlib-nano / printf runs in the VM. clang-tidy = 
  `$HOME/.venvs/clangtidy/bin/clang-tidy` (LLVM 23.1.4, pip wheel; no hicpp-*). Apple clang has the static
  analyzer built in (`clang --analyze`, `-Xanalyzer -analyzer-output=text`); no scan-build on the Mac.
  **VM elinux**: cmake 4.2, arm-none-eabi-gcc 14.2.1 WITH newlib + nano.specs/nosys.specs, ld.lld, readelf,
  valgrind, gdb-multiarch, `scan-build-21`, `clang-tidy-21`, `cppcheck 2.19.0`. Prefer GNU tools on the Mac
  now that arm binutils exist there (faster than the VM); use the VM for newlib, ld.lld, valgrind, .so/ldd,
  gdb core analysis, scan-build.
- Form S check.sh may call the VM, but keep the whole run < 90 s and the VM part small; prefer making the
  graded checks Mac-only and putting VM comparisons in demo_vm*.sh with captured *.txt output.
- Lesson dir file size: every file < 200 KB; no binaries > 64 KB (no core files, no big .elf/.dSYM — those
  are generated into /tmp/<ID>_work by scripts).

## Lanes (stay in yours; cross-ref the others by id)
- **M01 (Form S)** linker script basics. fw.c (vector table array in `.isr_vector`, Reset_Handler that copies
  .data from LMA to VMA and zeroes .bss using linker symbols, a few initialized/zero/const globals) + startup
  in C; starter.ld / solution.ld / practice.ld. MEMORY FLASH 512K @0x08000000, RAM 128K @0x20000000;
  SECTIONS .isr_vector (KEEP) / .text / .rodata / .data `> RAM AT> FLASH` / .bss (NOLOAD semantics) +
  `_sidata/_sdata/_edata/_sbss/_ebss/_estack` (+ `LOADADDR()`). Link on the Mac with `arm-none-eabi-gcc
  -mcpu=cortex-m4 -mthumb -nostdlib -ffreestanding -Wl,--gc-sections -T x.ld`. check.sh asserts via
  arm-none-eabi-readelf -S/-l/-s: vector table at 0x08000000 and first word = _estack, .data VMA in RAM and LMA
  in FLASH (VMA≠LMA, LMA right after .rodata), .bss not in the file image (no Flash cost), symbols consistent,
  `objcopy -O binary` size == expected (data payload included). Starter bugs, e.g.: .data without `AT> FLASH`
  (→ VMA=LMA in RAM, a huge .bin or init values lost), missing KEEP (gc-sections deletes the vector table),
  `_sidata` taken as ADDR not LOADADDR, `.bss` placed before .data / missing alignment. readelf -l program
  headers (PhysAddr vs VirtAddr) explained. One VM comparison: same link with `ld.lld` (captured txt) + what
  differs. Practice: add a `.noinit` RAM section and a 16 KB bootloader offset (or CCMRAM region). Lane: ASSERT,
  RAM functions, alignment details in depth = TOOL-A07; booting = BM-B08 (don't run in QEMU).
- **M02 (Form S, Python)** map file analyzer. gen_maps.sh builds a small multi-file Cortex-M firmware twice
  (v1/v2: v2 adds a feature + bigger buffer) with `arm-none-eabi-gcc -Wl,-Map=… -Wl,--gc-sections
  -ffunction-sections -fdata-sections -nostdlib` on the Mac → commit the two .map files (trim if needed, < 200 KB
  each) as test data. starter.py / solution.py / practice.py (python3 stdlib only, run `python3 -I`): parse GNU
  ld map "Linker script and memory map" input-section lines (incl. the wrapped-name 2-line form when the
  section name is long, `*fill*`, `COMMON`, discarded sections in "Discarded input sections"), aggregate per
  output section / per object(.o, archive(member)) / per symbol, Top-N report, and `diff v1 v2` per object &
  symbol. check.sh compares numbers against ground truth from `arm-none-eabi-size -A` / `arm-none-eabi-nm -S
  --size-sort` on the same ELFs. Starter bugs: ignores wrapped lines, counts discarded sections, double-counts
  `*fill*`, hex parsing. Also show (no grading) that ld64 `-Wl,-map,x.map` on the Mac has a different format
  (captured sample). Lane: reading a map by eye = BM-B05; size-reduction steps = M13.
- **M03 (Form C)** custom section registration tables. starter.c/solution.c/practice.c on the Mac: `SHELL_CMD(name,
  fn, help)` macro placing a `const struct shell_cmd` into `__DATA,__shellcmds` (`used`, alignment), iterate with
  `section$start$__DATA$__shellcmds` / `section$end$…` (Mach-O asm-label extern), dispatch + help listing, sort by
  name at runtime, detect duplicates. Portability header picks Mach-O vs ELF (`__start_shellcmds/__stop_` —
  section name must be a C identifier; and linker-script `KEEP(*(.shellcmds))` form). Verify ASan/UBSan clean (ASan
  may add redzones to globals in custom sections → measure; explain the fix if needed, e.g. stride assumptions,
  `__attribute__((no_sanitize("address")))` or `aligned`). demo_elf.sh in the VM: same code as ELF with gcc,
  `--gc-sections` dropping the table without `used`/KEEP / `-z start-stop-gc` (verify on ld.bfd 2.4x vs lld),
  initcall levels (`.initcall1`… sorted by `SORT()` in a script, like Linux). Starter bugs: missing `used` (entry
  removed at -O2 / dead-stripped), wrong stride (pointer arithmetic on char*), section-name typo silently empty.
  Practice: initcall-style ordered init levels.
- **M04 (Form S)** CMake basics. Port the TOOL-B08 project idea (read lessons/TOOL/TOOL-B08/proj) to CMake in
  starter/ solution/ practice/ subdirs: `add_library(ringbuf STATIC …)`, app executable, test executable,
  `target_include_directories(PUBLIC include PRIVATE src)`, `target_compile_options` warnings via a
  generator expression per compiler, `option(FEATURE_CRC "…" ON)` → `target_compile_definitions`, `enable_testing()`
  + `add_test` + CTest labels. check.sh: configure (-G Ninja) into /tmp/TOOL-M04_work, build, `ctest
  --output-on-failure`, and asserts with real tool output: PRIVATE header not visible to app (compile must fail
  when app includes it — test via try-build), option OFF changes the symbol set (nm), `compile_commands.json`
  exported. Starter bugs: global `include_directories`/`add_definitions` leaking, PUBLIC vs PRIVATE wrong so a
  consumer can't build, tests not registered (ctest "No tests were found"), `CMAKE_C_FLAGS` string hacks. Explain
  build-tree vs source-tree, configure vs generate vs build, cache vars. Lane: cross toolchain files = M05.
- **M05 (Form S)** CMake toolchain file cross-build. One source tree (portable lib + tests + `fw/` startup,
  linker script, main) built twice: build-host (Mac clang, runs unit tests via ctest) and build-target with
  `-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake` (Mac arm-none-eabi-gcc 16: `CMAKE_SYSTEM_NAME Generic`,
  `CMAKE_SYSTEM_PROCESSOR arm`, `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY`, CPU flags, `-nostdlib`,
  linker script via `target_link_options`, `CMAKE_FIND_ROOT_PATH_MODE_*`), post-build custom commands generating
  .bin (objcopy) / .map / size output. check.sh asserts: target ELF is `ELF32 ARM` (readelf -h), host tests pass,
  .bin/.map exist, tests NOT built in target tree, flags not leaking into host build. Starter bugs: no
  try-compile setting (configure fails — capture the real error), toolchain file sets flags with `set(CMAKE_C_FLAGS
  …)` instead of `_INIT` (duplicated/overwritten), linker script dependency missing (`LINK_DEPENDS`). VM part
  (demo_vm.sh, captured): same tree with VM arm gcc 14 + newlib-nano (`--specs=nano.specs`). Lane: CMake basics
  = M04; size diet = M13.
- **M06 (Form S)** shared libraries. Mac: build `libfoo.dylib` (-fPIC is default on arm64 — show), `-install_name
  @rpath/libfoo.dylib`, app with `-Wl,-rpath,@loader_path/../lib`; `otool -L`, `otool -l | grep -A3 LC_RPATH`,
  `install_name_tool -change / -add_rpath`, `DYLD_PRINT_LIBRARIES` / `DYLD_LIBRARY_PATH` behaviour (verify what
  SIP strips), `-fvisibility=hidden` + `FOO_API __attribute__((visibility("default")))` → `nm -gU` exported set,
  compatibility/current version (`-compatibility_version`), interposition. Starter bugs: app finds the lib only
  from the build dir (absolute install name) → moving the bundle breaks (`dyld: Library not loaded`, capture),
  internal helpers exported, ABI break (struct size change) undetected. VM (demo_vm.sh captured + small graded
  part allowed): `.so` with `-Wl,-soname,libfoo.so.1`, symlink chain, `readelf -d` (NEEDED/SONAME/RUNPATH),
  `ldd`, `LD_DEBUG=libs`, `-fPIC` missing error on x86_64? (aarch64 VM — verify what happens), visibility +
  version script. Firmware tie: why MCUs don't use them, where embedded Linux does (rootfs size, plugin loaders).
- **M07 (Form S)** advanced debugger. buggy.c: ring buffer + a periodic "task" loop where index arithmetic
  corrupts a neighbour field only when count hits a specific value (deterministic, e.g. iteration 7313). lldb
  batch files: conditional breakpoint (`br set … -c 'rb->count == …'`), hardware watchpoint (`watchpoint set
  expression -w write -- &obj.guard`, `watchpoint modify -c`), `-o`/`-s` command lists, breakpoint commands
  (`breakpoint command add` printing then continue). lldb Python: `ringbuf_cmd.py` defining a `rbdump` command
  (`command script add -f`) and a summary provider (`type summary add -F`) that prints head/tail/count/
  contents in logical order. starter.py vs solution.py (TODOs in the formatter: wrap-around order, empty/full
  distinction, reading array via SBValue). check.sh runs `lldb -b` with each variant and asserts on output
  (watchpoint hit line = the real culprit line, rbdump output exact). Note hardware watchpoint count limit on
  Apple Silicon (measure: try setting N+1). gdb equivalents table (`watch`, `condition`, `commands`,
  pretty-printer API) + short VM gdb-multiarch/qemu-aarch64 demo captured if feasible. Lane: basics = TOOL-B10.
- **M08 (Form S)** git bisect run. gen_repo.sh creates a deterministic repo in /tmp (fixed GIT_AUTHOR_DATE/
  COMMITTER_DATE/names, `git -c init.defaultBranch=main`, no global config dependency: `GIT_CONFIG_GLOBAL=
  /dev/null`) with 100 commits on a small C project: one commit introduces a functional regression, a couple of
  commits don't build (must be skipped), one later commit adds a perf regression (practice). starter_test.sh /
  solution_test.sh: the bisect run script. Starter bugs: build failure returns 1 (→ blames the wrong commit),
  exits >127 or 128+ on crash (aborts bisect), script lives inside the repo so it changes per checkout, test
  depends on untracked artefacts from a previous build (no clean). check.sh runs `git bisect start BAD GOOD` +
  `git bisect run` with each variant and asserts the found commit == the planted one (by subject tag), number of
  steps ≈ log2(100) + skips (paste real `git bisect log`). Explain exit-code contract 0 / 1–124,126,127 / 125 /
  ≥128, `git bisect skip`, `--first-parent`, `terms`, perf regression threshold with noise (median of N). Firmware
  tie: HIL test as the bisect script, flashing time per step.
- **M10 (Form S)** leaks / Guard Malloc / ASan / valgrind. victim.c-style program with: a leak (lost pointer), a
  leak via reachable-but-forgotten cache, heap use-after-free write, 1-byte heap overflow, uninitialized read.
  starter.c (bugs) / solution.c (fixed) / practice.c (another buggy-then-fixed module). check.sh grades with real
  tools: `MallocStackLogging=1 leaks --atExit -- ./x` (0 leaks for solution; starter reports leaks — grep the
  real summary line), ASan build (no error for solution), and program self-test output. demo_tools.sh +
  captured txt: leaks with stack (MallocStackLogging=1 vs lite), `DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib`
  Guard Malloc (verify it still exists on macOS 27.2; if not, say so with the real error) + `MALLOC_*` env vars
  (MallocScribble/MallocGuardEdges/MallocPreScribble), ASan; VM: `valgrind --leak-check=full
  --track-origins=yes` on the same program (gcc -g -O0) + LSan. Detection matrix (bug × tool, ✓/✗ from real
  runs) incl. uninitialized read (only valgrind/MSan — MSan not on Mac, verify on VM clang). Cost table (slowdown
  ×, memory) measured. Lane: ASan basics = TOOL-B11; firmware heap → RTOS/C ids.
- **M11 (Form S)** core dumps + post-mortem. crash.c: 3 threads, one corrupts a heap object and another crashes
  later dereferencing it (deterministic). Mac: `ulimit -c unlimited`, `codesign -s - -f --entitlements ent.plist`
  (com.apple.security.get-task-allow), `/cores/core.<pid>` (check size; delete it at the end — macOS cores can be
  GBs; never into the lesson dir), `lldb -c core ./crash` → `thread list`, `bt all`, `frame variable`, memory read
  of the heap object, register state. starter.lldb / solution.lldb (+ practice): the post-mortem command script
  that must print the facts the check asserts (crashing thread name, faulting function, the corrupted field value,
  the thread/function that wrote it if recoverable). If making a Mac core is impossible in this sandbox, show the
  real error and do the graded part with the VM: `ulimit -c`, `core_pattern` is apport (use `sudo sysctl -w
  kernel.core_pattern=/tmp/TOOL-M11_work/core.%e.%p` only inside the VM and restore it afterwards), `gdb-multiarch
  ./crash core` (check whether core analysis works despite the native-gdb SVE problem), `eu-stack`/`coredumpctl`
  if present. Firmware tie: MCU "core dump" = RAM + registers snapshot (cite BM-A09, ARM-M14, TOOL-A05).
- **M12 (Form S)** static analysis. fw_bugs.c (~250 lines firmware-style: register struct, ring buffer, parser,
  state machine) with **exactly 10 planted bugs**, each tagged `// BUG01`…`// BUG10` (null deref on error path,
  leak on early return, OOB index, uninitialized var on one path, signed/unsigned compare, dead store, double
  lock / unlock-missing, integer overflow in size calc, use-after-free, format string mismatch). Run on the Mac:
  clang-tidy (venv), `clang --analyze`, cppcheck 2.22; VM: `scan-build-21`, cppcheck 2.19 — build the real
  matrix (bug × tool ✓/✗) from real output. starter/solution/practice = config files: `starter.clang-tidy` (e.g.
  `Checks: '*'` noise or too narrow) vs `solution.clang-tidy` (selected checks + `WarningsAsErrors` +
  `HeaderFilterRegex`), cppcheck suppression file / inline `// cppcheck-suppress` policy, plus `fw_clean.c` (the
  fixed code). check.sh asserts: solution config finds ≥ K of the planted BUG lines (exact line numbers), produces
  0 diagnostics on fw_clean.c (no false positives in CI baseline), and the suppression is scoped (one id, one
  line). Explain path-sensitive vs AST-matcher vs pattern tools, why each misses what it misses, CI baseline /
  ratcheting, MISRA mention (cppcheck misra addon — hedge). Lane: compiler warnings = TOOL-B02; clang-tidy for
  C++ = CPP-A09 (cite).
- **M13 (Form S)** code size diet. Demo Cortex-M4 firmware (a few modules + printf usage + unused functions +
  a lookup table). Stage table built from REAL runs: baseline -O0 → -Os → `-ffunction-sections -fdata-sections
  -Wl,--gc-sections` → `-flto` → newlib → newlib-nano (`--specs=nano.specs`) → printf removed/replaced by a tiny
  formatter (+ `-u _printf_float` effect). newlib stages need the **VM** arm gcc 14; Mac gcc 16 stages are
  nostdlib only — decide: run the whole graded pipeline in the VM (copy to /tmp first) or split, but every number
  in the table must come from a run you capture into a txt file. starter.flags/solution.flags (+ practice):
  check.sh builds with the variant's flags and asserts `.text`/`.data`/`.bss` under a budget AND that the image is
  still correct (vector table kept, required symbols present via nm, `--print-gc-sections` shows what went).
  Starter traps: gc-sections without -ffunction-sections (nothing removed), LTO with a non-LTO-aware ar (`ar`
  vs `arm-none-eabi-gcc-ar` — verify the real symptom), vector table eaten by gc (missing KEEP), `-Os` + `-g`
  confusion (debug info doesn't cost Flash — show size vs file size). Map-file diff between two stages (cite
  M02 tool). Lane: section meaning = TOOL-B06; `-O` levels in general = TOOL-B03.
- **M14 (Form S)** DWARF & symbolication. Mac: app built with `-g0/-gline-tables-only/-g1/-g/-g3` → table of
  binary size, `__DWARF` / dSYM size (dsymutil), `dwarfdump --debug-line` excerpt; strip (`strip -x`/`-S`),
  `dwarfdump --uuid` matching binary↔dSYM. A crash generator prints a backtrace (PCs) + the load address/slide
  of the main image (ASLR — the run differs each time; use `_dyld_get_image_vmaddr_slide` or
  `_dyld_get_image_header`); captured crash logs (crash1.txt…) committed. starter.sh/solution.sh/practice.sh
  (or .py, `python3 -I`): `symbolicate` script reading the crash log + stripped binary + dSYM, outputting
  `func (file:line)` using `atos -o X.dSYM/Contents/Resources/DWARF/X -arch arm64 -l <load addr> <pcs>` (and
  `llvm-symbolizer`/`llvm-addr2line` if CLT has it — check). Starter bugs: forgets the ASLR slide / load address,
  doesn't subtract 1 from return addresses for frames ≥ 1 (wrong line for calls at the end of a line / next
  line), uses the wrong dSYM (UUID mismatch not detected), inlined frames not shown (`atos -i` / `--inlines`).
  check.sh asserts exact `func file:line` per frame vs ground truth from the unstripped binary. ELF in the VM
  (+ arm-none-eabi on Mac): `objcopy --only-keep-debug`, `--strip-debug`, `--add-gnu-debuglink`, build-id
  (`readelf -n`), `addr2line -f -i -e`, Cortex-M Thumb bit (PC|1) trap. Lane: crash register decoding =
  ARM-M12/M14; crash dump formats = TOOL-A05; build-id policy = TOOL-A06.

## When done
Reply with: files written (paths), md line count, Form, PASS counts for starter/solution/practice, sanitizer
result (Form C), demo files + key real numbers, tools you found missing/broken, and every fact you were unsure
about. Do NOT edit files outside ROOT/lessons/TOOL/<your ID>/ (plus your private /tmp/<ID>_* paths). Do not
install software (if something essential is missing, say so in the reply and work around it).
