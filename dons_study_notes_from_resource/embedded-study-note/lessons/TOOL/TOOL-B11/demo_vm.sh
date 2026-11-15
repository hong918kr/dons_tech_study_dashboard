#!/usr/bin/env bash
# TOOL-B11 demo — the Linux/ELF side, in the Lima VM `elinux` (aarch64 Ubuntu).
#   bash demo_vm.sh        (run on the Mac; writes *_linux.txt into this lesson dir)
# Mac home is mounted read-only in the VM → copy sources to VM /tmp first.
set -u
cd "$(dirname "$0")" || exit 1
SRC=$(pwd)
VM="limactl shell elinux -- bash -lc"
$VM "rm -rf /tmp/TOOL-B11_work/vm && mkdir -p /tmp/TOOL-B11_work/vm && cd '$SRC' &&
     cp buf.h driver.c starter.c solution.c demo_shadow.c demo_instr.c /tmp/TOOL-B11_work/vm/" || exit 1
run() { $VM "cd /tmp/TOOL-B11_work/vm && { $1 ; } 2>&1" 2>&1; }   # one stream: keep order
F="-std=c11 -g -O1 -fno-omit-frame-pointer -fsanitize=address"

# 1) gcc 15 report for the heap bug: same format, ELF + llvm-symbolizer/addr2line style frames
{ echo '$ gcc '"$F"' starter.c driver.c -o t_gcc && ./t_gcc heap'
  run "gcc $F starter.c driver.c -o t_gcc && ./t_gcc heap; echo \"[exit code \$?]\"" |
	sed '/^Shadow byte legend/,/^==[0-9]*==ABORTING/d'; } >asan_heap_gcc_linux.txt

# 2) LeakSanitizer: on by default with ASan on Linux
{ echo '$ clang '"$F"' starter.c driver.c -o t_clang && ./t_clang leak'
  run "clang $F starter.c driver.c -o t_clang && ./t_clang leak; echo \"[exit code \$?]\""
  echo
  echo '$ clang '"$F"' solution.c driver.c -o s_clang && ./s_clang leak'
  run "clang $F solution.c driver.c -o s_clang && ./s_clang leak; echo \"[exit code \$?]\""
  echo
  echo '$ ASAN_OPTIONS=detect_leaks=0 ./t_clang leak      # LSan off'
  run "ASAN_OPTIONS=detect_leaks=0 ./t_clang leak; echo \"[exit code \$?]\""
  echo
  echo '$ clang -std=c11 -g -O1 -fsanitize=leak starter.c driver.c -o t_lsan && ./t_lsan leak   # LSan only, no ASan'
  run "clang -std=c11 -g -O1 -fsanitize=leak starter.c driver.c -o t_lsan && ./t_lsan leak; echo \"[exit code \$?]\""
} >lsan_leak_linux.txt

# 3) shadow mapping on Linux aarch64 (fixed offset) + the instrumentation gcc emits
{ echo '$ gcc '"$F"' demo_shadow.c -o ds && ./ds'
  run "gcc $F demo_shadow.c -o ds && ./ds"; } >shadow_linux.txt
run "gcc -O1 -fsanitize=address -S demo_instr.c -o - | grep -vE '^\s*\.|^\.L[A-Z]|^\$' | sed -n '/^get_byte:/,/bl\s/p'" >asm_linux_gcc.txt

# 4) what the instrumented ELF object needs from the runtime
run "gcc $F -c starter.c -o s.o && nm s.o | grep -i asan | sort -k2" >nm_asan_linux.txt
echo "wrote: asan_heap_gcc_linux.txt lsan_leak_linux.txt shadow_linux.txt asm_linux_gcc.txt nm_asan_linux.txt"
