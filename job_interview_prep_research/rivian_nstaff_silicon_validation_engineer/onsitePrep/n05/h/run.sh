#!/bin/bash
# N05H · build & run everything. Part 1 = model answers (strict spec flags, PASS lines).
# Part 2 = the intentionally broken snippets: built WITHOUT -Werror, with
# -fsanitize=undefined (or plain -O0/-O2) so the REAL output / UBSan report is shown.
cd "$(dirname "$0")" || exit 1
SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
B=${TMPDIR:-/tmp}/n05h; mkdir -p "$B"
fail=0

echo "=== Part 1: model answers (-Wall -Wextra -Werror -fsanitize=address,undefined)"
for src in H1_promotion H2_sizeof_layout H3_volatile H4_macros H5_shifts H7_declarations H8_signed_unsigned H9_type_punning; do
  cc -isysroot $SDK -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $src.c -o "$B/$src" \
    && "$B/$src" | tail -1 || { echo "$src FAIL"; fail=1; }
done
cc -isysroot $SDK -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined H6_dma_fixed.c H6_sim.c -o "$B/H6" \
  && "$B/H6" | tail -1 || { echo "H6 FAIL"; fail=1; }
cc -isysroot $SDK -std=c11 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined H3_volatile.c -o "$B/H3o2" \
  && echo -n "(-O2) " && "$B/H3o2" | tail -1 || { echo "H3 -O2 FAIL"; fail=1; }

ub() {  # ub <name> <files...> : build without -Werror, with UBSan, run
  local n=$1; shift
  cc -isysroot $SDK -std=c11 -Wall -Wextra -fsanitize=undefined "$@" -o "$B/$n" 2>&1 | grep -E 'warning:|error:'
  "$B/$n" 2>&1 | grep -v '^SUMMARY'
}
watchdog() {  # watchdog <secs> <cmd...> : run, kill if still alive
  local s=$1; shift
  "$@" & local pid=$!
  for _ in $(seq $((s * 10))); do kill -0 $pid 2>/dev/null || { wait $pid; return; }; sleep 0.1; done
  kill -9 $pid 2>/dev/null; wait $pid 2>/dev/null; echo "HUNG -> killed by ${s}s watchdog"
}

echo; echo "=== Part 2: the broken snippets (no -Werror; UBSan / -O0 vs -O2)"
echo "--- H1 u16*u16";            ub H1ub H1_promotion_ub.c
echo "--- H3 asm (clang -O2 -S)"
cc -isysroot $SDK -std=c11 -O2 -S H3_volatile_asm.c -o "$B/H3.s"
awk '/^_(L1|L2|L3|L4|X1|V1|V2)_[a-z0-9_]*:/{p=1} p&&/^\t[a-z]/{print "   " $0} /End function/{p=0} /^_[LVX][0-9]/{print $1}' "$B/H3.s"
for O in -O0 -O2; do
  cc -isysroot $SDK -std=c11 -Wall -Wextra $O H3_novolatile_demo.c -o "$B/H3bad$O"
  for m in flag poll; do echo -n "--- H3 no-volatile $O $m: "; watchdog 2 "$B/H3bad$O" $m; done
done
echo "--- H4 SQUARE(i++)";        ub H4ub H4_macros_ub.c
echo "--- H4 BAD_RESET + else (compile error expected)"
cc -isysroot $SDK -std=c11 -Wno-unsequenced -DSHOW_ELSE_ERROR H4_macros_ub.c -o "$B/H4e" 2>&1 | grep error:
echo "--- H5 shifts (UBSan)";     ub H5ub H5_shifts_ub.c
for O in -O0 -O2; do echo "--- H5 no sanitizer $O"; cc -isysroot $SDK -std=c11 $O H5_shifts_ub.c -o "$B/H5p$O" && "$B/H5p$O"; done
echo "--- H6 buggy driver";      ub H6bug H6_dma_buggy.c H6_sim.c
echo -n "--- H6 buggy, DMA never completes: "; watchdog 2 "$B/H6bug" hang
echo "--- H6 Cortex-M4 busy-poll build, buggy dma0_copy (clang -O2):"
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -ffreestanding -std=c11 -O2 -DTARGET '-Dcpu_relax()=((void)0)' \
  -S H6_dma_buggy.c -o - 2>/dev/null | sed -n '/^dma0_copy:/,/^.Lfunc_end/p' | grep -E '^\s+[a-z]|^\.L' | sed 's/^/   /'
echo "--- H7 const errors (compile error expected)"
cc -isysroot $SDK -std=c11 -DSHOW_ERRORS H7_declarations.c -o "$B/H7e" 2>&1 | grep error:
echo "--- H8 snippets";          ub H8s H8_snippets.c
echo "--- H9 aliasing (UBSan, -O0)"; ub H9ub H9_aliasing_ub.c
for O in -O0 -O2 "-O2 -fno-strict-aliasing"; do
  echo -n "--- H9 plain $O: "; cc -isysroot $SDK -std=c11 $O H9_aliasing_ub.c -o "$B/H9p" && "$B/H9p" | head -1
done
exit $fail
