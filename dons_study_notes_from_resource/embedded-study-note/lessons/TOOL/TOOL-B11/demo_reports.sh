#!/usr/bin/env bash
# TOOL-B11 demo — capture the REAL reports (Mac) into asan_*.txt / leaks_*.txt.
#   bash demo_reports.sh        (writes the txt files into this lesson dir)
set -u
cd "$(dirname "$0")" || exit 1
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
D=/tmp/TOOL-B11_work/demo; rm -rf "$D"; mkdir -p "$D"
F="-std=c11 -g -O1 -fno-omit-frame-pointer -fsanitize=address"
cc $F starter.c driver.c -o "$D/t_asan"
cc $F practice.c -DBUGGY -o "$D/p_bug"
cc $F practice.c -o "$D/p_asan"
legend_cut() { sed '/^Shadow byte legend/,$d'; }    # legend is identical every time

for c in heap stack global uaf dfree; do              # default ASAN_OPTIONS (rc 134)
	"$D/t_asan" $c >"$D/out" 2>&1; rc=$?
	{ legend_cut <"$D/out"; echo "[exit code $rc]"; } >asan_$c.txt
done
"$D/t_asan" heap 2>&1 | sed -n '/^Shadow byte legend/,$p' >asan_legend.txt

for c in scope overlap; do
	"$D/p_bug" $c >"$D/out" 2>&1; rc=$?
	{ legend_cut <"$D/out"; echo "[exit code $rc]"; } >asan_$c.txt
done
{ echo '$ ./p_bug ret                                   # default options'
  "$D/p_bug" ret 2>&1; echo "[exit code $?]"
  echo '$ ASAN_OPTIONS=detect_stack_use_after_return=1 ./p_bug ret'
  ASAN_OPTIONS=detect_stack_use_after_return=1 "$D/p_bug" ret 2>&1 | legend_cut
  echo "[exit code ${PIPESTATUS[0]}]"; } >asan_ret.txt
"$D/p_asan" pool_bad >"$D/out" 2>&1; rc=$?
{ legend_cut <"$D/out"; echo "[exit code $rc]"; } >asan_pool.txt

# Mac ASan has no LeakSanitizer
{ echo '$ ASAN_OPTIONS=detect_leaks=1 ./t_asan leak'
  ASAN_OPTIONS=detect_leaks=1 "$D/t_asan" leak 2>&1; echo "[exit code $?]"; } >asan_leak_mac.txt
# leaks(1) on a plain -g build: starter vs solution (cut the long "Binary Images" list)
for v in starter solution; do
	cc -std=c11 -g -O1 $v.c driver.c -o "$D/$v.plain"
	{ echo "\$ MallocStackLogging=1 leaks --atExit -- ./$v.plain leak"
	  MallocStackLogging=1 leaks --atExit -- "$D/$v.plain" leak 2>&1 | sed '/^Binary Images:/,$d'
	  echo "[exit code ${PIPESTATUS[0]}]"; } >leaks_$v.txt
done

# recover mode: keep running after the first error (global case has 2 bad kinds)
cc $F -fsanitize-recover=address starter.c driver.c -o "$D/t_rec"
{ echo '$ ASAN_OPTIONS=halt_on_error=0 ./t_rec global   # built with -fsanitize-recover=address'
  ASAN_OPTIONS=halt_on_error=0 "$D/t_rec" global 2>&1 | grep -E '^(==[0-9]+==ERROR|READ|WRITE|SUMMARY|global:)|located'
  echo "[exit code ${PIPESTATUS[0]}]"; } >asan_recover.txt

# instrumentation in the generated code
cc -O1 -fsanitize=address -S demo_instr.c -o - | grep -vE '^\s*\.|^Lfunc|^\s*;|^Lloh|^$' | sed -n '/^_get_byte:/,/brk/p' >asm_mac.txt
# shadow memory read by hand (Mac: dynamic shadow offset)
cc $F demo_shadow.c -o "$D/ds" && "$D/ds" >shadow_mac.txt
echo "wrote: $(ls asan_*.txt leaks_*.txt asm_mac.txt shadow_mac.txt | tr '\n' ' ')"
