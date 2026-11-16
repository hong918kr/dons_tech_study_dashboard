#!/usr/bin/env bash
# TOOL-M14 checker:  bash check.sh <starter|solution|practice>
#   1. builds crashgen.c (-g -O1), splits the DWARF into a dSYM (dsymutil), strips the shipped binary
#      and builds a "v2" (different code -> different UUID) as the wrong-dSYM trap;
#   2. runs the STRIPPED binary 3 times (null / panic / isr) -> fresh crash logs, new ASLR slide each run;
#   3. sources <variant>.sh and checks its helpers + `symbolicate` against the "@T" ground-truth lines
#      in crashgen.c.  practice: also atos -offset and the Cortex-M ELF (Thumb bit, build-id) tasks.
#   Works only in /tmp/TOOL-M14_work/<variant>; never writes into the lesson directory.
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
V=${1:-}
case "$V" in starter|solution|practice) ;; *) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;; esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-M14_work/$V
rm -rf "$W" && mkdir -p "$W"/build "$W"/ship "$W"/syms "$W"/v2 || exit 2
before=$(ls -A "$L" | wc -l | tr -d ' ')
. "$L/$V.sh"

pass=0 total=0
ok_line()   { pass=$((pass + 1)); total=$((total + 1)); printf 'PASS  %s\n' "$1"; }
fail_line() { total=$((total + 1)); printf 'FAIL  %s\n' "$1"; }
expect()    { if [ "$2" = "$3" ]; then ok_line "$1"; else fail_line "$1  (got: $(echo "$2" | tr '\n' '|'))"; fi; }
ln_of()     { grep -n "// @T $2\( \|\$\)" "$L/$1" | head -1 | cut -d: -f1; }   # ground truth line

echo "== build (cc = $(cc --version | head -1 | sed 's/ (.*//'))"
F="-std=c11 -Wall -Wextra -Werror -g -O1 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fno-optimize-sibling-calls"
( cd "$W/build" && cc $F -c "$L/crashgen.c" -o crashgen.o && cc crashgen.o -o crashgen \
  && dsymutil crashgen -o "$W/syms/crashgen.dSYM" && cp crashgen "$W/ship/crashgen" && strip "$W/ship/crashgen" \
  && cd "$W/v2" && cc $F -DFW_V2 -c "$L/crashgen.c" -o crashgen.o && cc crashgen.o -o crashgen \
  && dsymutil crashgen -o crashgen.dSYM ) > "$W/build.log" 2>&1 || { cat "$W/build.log"; echo "build failed"; exit 2; }
for m in null panic isr; do "$W/ship/crashgen" $m > "$W/crash_$m.txt"; done
"$W/v2/crashgen" null > "$W/crash_v2.txt"
SHIP=$W/ship/crashgen DSYM=$W/syms/crashgen.dSYM
U=$(macho_uuid "$SHIP")
if [ -n "$U" ] && [ "$U" = "$(macho_uuid "$DSYM")" ] && [ "$U" = "$(crash_uuid "$W/crash_null.txt")" ] \
   && [ "$(nm "$SHIP" | grep -c parse_record)" = 0 ]; then
    ok_line "stripped binary has no parse_record symbol; binary = dSYM = crash log UUID $U"
else
    fail_line "build facts (UUID $U, stripped?)"
fi
NLA=$(awk '$1 == "load_address:" {print $2}' "$W/crash_null.txt")
NPC0=$(awk '$1 == "0" {print $2}' "$W/crash_null.txt")
echo "   crash_null.txt: load_address $NLA  slide $(awk '$1 == "slide:" {print $2}' "$W/crash_null.txt")  pc $NPC0"

echo "== helpers ($V.sh)"
expect "crash_load_address = where __TEXT was loaded in this run (not text_vmaddr)" \
       "$(crash_load_address "$W/crash_null.txt")" "$NLA"
expect "lookup_address: frame 0 keeps pc, frames >= 1 use return address - 1" \
       "$(lookup_address 0 0x100000fc4) $(lookup_address 1 0x100000f14) $(lookup_address 4 0x18753e054)" \
       "0x100000fc4 0x100000f13 0x18753e053"
uuid_check "$W/crash_null.txt" "$SHIP" "$DSYM" 2>/dev/null; r1=$?
uuid_check "$W/crash_null.txt" "$SHIP" "$W/v2/crashgen.dSYM" 2>/dev/null; r2=$?
uuid_check "$W/crash_v2.txt" "$SHIP" "$DSYM" 2>/dev/null; r3=$?
expect "uuid_check accepts the matching dSYM" "$r1" 0
expect "uuid_check rejects the v2 dSYM (wrong build)" "$([ $r2 -ne 0 ] && echo rejected || echo accepted)" rejected
expect "uuid_check rejects a crash log from the v2 binary" "$([ $r3 -ne 0 ] && echo rejected || echo accepted)" rejected
got=$(atos_frame "$DSYM/Contents/Resources/DWARF/crashgen" "$NLA" "$NPC0" | grep -v '^$' | sed 's/ (in [^)]*)//')
expect "atos_frame shows the inlined rd_le16 frame, then parse_record" "$got" \
       "rd_le16 (crashgen.c:$(ln_of crashgen.c rd16))
parse_record (crashgen.c:$(ln_of crashgen.c parsecall))"

echo "== symbolicate: stripped binary + dSYM + fresh crash log, frame by frame"
check_trace() {            # check_trace MODE "expected line" ...
    local m=$1 out i e g
    shift
    out=$(symbolicate "$W/crash_$m.txt" "$SHIP" "$DSYM" 2>"$W/err_$m.txt" | sed -E 's/0x[0-9a-f]{16}/ADDR/')
    i=1
    for e in "$@"; do
        g=$(echo "$out" | sed -n "${i}p")
        if [ "$g" = "$e" ]; then ok_line "$(printf '%-6s %s' "$m" "$e")"
        else fail_line "$(printf '%-6s %s  (got: %s)' "$m" "$e" "${g:-<nothing>}")"; fi
        i=$((i + 1))
    done
}
c() { echo "crashgen.c:$(ln_of crashgen.c "$1")"; }
check_trace null  "#0 rd_le16 ($(c rd16)) [inlined]" "#0 parse_record ($(c parsecall))" \
                  "#1 process_packet ($(c ppparse))" "#2 main ($(c mainnull))" "#3 ?? ADDR (not in image)"
check_trace panic "#0 fw_panic ($(c panicw))" "#1 check_header ($(c chkcall))" \
                  "#2 process_packet ($(c ppchk))" "#3 main ($(c mainpanic))" "#4 ?? ADDR (not in image)"
check_trace isr   "#0 rb_put ($(c rbput)) [inlined]" "#0 uart_rx_isr ($(c isrput))" \
                  "#1 dispatch_irq ($(c dispcall))" "#2 main ($(c mainisr))" "#3 ?? ADDR (not in image)"
out=$(symbolicate "$W/crash_null.txt" "$SHIP" "$W/v2/crashgen.dSYM" 2>"$W/err_v2.txt"); rc=$?
if [ $rc -eq 3 ] && [ -z "$out" ] && grep -q 'UUID mismatch' "$W/err_v2.txt"; then
    ok_line "wrong dSYM -> exit 3, 'UUID mismatch' on stderr, no frames printed"
else
    fail_line "wrong dSYM -> exit 3 + UUID mismatch  (got rc=$rc, first line: $(echo "$out" | head -1))"
fi

if [ "$V" = practice ]; then
    echo "== practice 1: atos -offset (imageOffset form, as in .ips reports)"
    got=""
    for k in 0 1 2; do
        pc=$(awk -v k=$k '$1 == k {print $2}' "$W/crash_null.txt")
        got="$got$(offset_frame "$DSYM/Contents/Resources/DWARF/crashgen" $k $(printf '0x%x' $(( pc - NLA ))))
"
    done
    expect "offset_frame == symbolicate for frames 0..2 of crash_null" "$got" \
           "$(symbolicate "$W/crash_null.txt" "$SHIP" "$DSYM" | head -4)
"
    echo "== practice 2/3: Cortex-M4 ELF (arm-none-eabi $(arm-none-eabi-gcc -dumpversion)), Thumb bit, build-id"
    M=$W/m4
    mkdir -p "$M" "$M/v2"
    MF="-mcpu=cortex-m4 -mthumb -g -O1 -ffreestanding -nostdlib -Wall -Wextra -Werror -Wl,--build-id=sha1 -T $L/fw_m4.ld"
    ( cd "$M" && arm-none-eabi-gcc $MF "$L/fw_m4.c" -o fw.elf \
      && arm-none-eabi-objcopy --only-keep-debug fw.elf fw.debug \
      && arm-none-eabi-objcopy --strip-debug --add-gnu-debuglink=fw.debug fw.elf fw.stripped \
      && cd v2 && arm-none-eabi-gcc ${MF/-O1/-O2} "$L/fw_m4.c" -o fw.elf ) > "$M/build.log" 2>&1 \
      || { cat "$M/build.log"; fail_line "Cortex-M build"; }
    arm-none-eabi-objdump -d -l "$M/fw.elf" > "$M/dis.txt"
    first_insn() {         # first instruction address attributed to fw_m4.c:<line>
        awk -v want="fw_m4.c:$1" '/^\// {c = $1; sub(/.*\//, "", c); next}
             /^ +[0-9a-f]+:\t/ && c == want {a = $1; sub(/:/, "", a); print "0x" a; exit}' "$M/dis.txt"
    }
    lr_after_bl() {        # value lr holds after "bl <fn>": next instruction | 1 (Thumb)
        local a
        a=$(awk -v fn="<$1>" '/\tbl\t/ && $NF == fn {a = $1; sub(/:/, "", a); print a; exit}' "$M/dis.txt")
        printf '0x%x' $(( (0x$a + 4) | 1 ))
    }
    m() { echo "fw_m4.c:$(ln_of fw_m4.c "$1")"; }
    LR_PARSE=$(lr_after_bl parse_frame) LR_TASK=$(lr_after_bl sensor_task)
    echo "   vectors from objdump: pc $(first_insn "$(ln_of fw_m4.c m4rd)")  lr(parse) $LR_PARSE  lr(task) $LR_TASK  lr(fault) $(lr_after_bl fw_fault)"
    got=$( { m4_frame "$M/fw.debug" 0 "$(first_insn "$(ln_of fw_m4.c m4rd)")"; m4_frame "$M/fw.debug" 1 "$LR_PARSE"
             m4_frame "$M/fw.debug" 2 "$LR_TASK"; m4_frame "$M/fw.debug" 3 0xFFFFFFF9; } )
    expect "M4 NULL body: inline + Thumb lr - 1 + EXC_RETURN" "$got" \
"#0 rd_be16 ($(m m4rd)) [inlined]
#0 parse_frame ($(m m4parse))
#1 sensor_task ($(m m4task_parse))
#2 Reset_Handler ($(m m4reset_call))
#3 <exception return 0xfffffff9>"
    got=$( { m4_frame "$M/fw.debug" 0 "$(first_insn "$(ln_of fw_m4.c m4reset)")"
             m4_frame "$M/fw.debug" 1 "$(lr_after_bl fw_fault)"; m4_frame "$M/fw.debug" 2 "$(lr_after_bl check_crc)"; } )
    expect "M4 fw_fault: noreturn call is the last insn of check_crc" "$got" \
"#0 fw_fault ($(m m4reset))
#1 check_crc ($(m m4chk))
#2 sensor_task ($(m m4task_chk))"
    expect "m4_lookup clears the Thumb bit (frame 0) and also subtracts 1 (frame >= 1)" \
           "$(m4_lookup 0 0x80000bb) $(m4_lookup 1 0x80000bb)" "0x80000ba 0x80000b9"
    elf_debug_ok "$M/fw.stripped" "$M/fw.debug"; r1=$?
    elf_debug_ok "$M/fw.stripped" "$M/v2/fw.elf"; r2=$?
    expect "build-id: fw.stripped matches fw.debug, not the -O2 rebuild" "$r1 $([ $r2 -ne 0 ] && echo rejected)" "0 rejected"
    if [ -n "$(elf_build_id "$M/fw.stripped")" ] && ! arm-none-eabi-readelf -S "$M/fw.stripped" | grep -q debug_info \
       && arm-none-eabi-readelf -p .gnu_debuglink "$M/fw.stripped" 2>/dev/null | grep -q fw.debug; then
        ok_line "fw.stripped: no .debug_info, has build-id + .gnu_debuglink -> fw.debug"
    else
        fail_line "fw.stripped: debug sections / build-id / debuglink"
    fi
fi

after=$(ls -A "$L" | wc -l | tr -d ' ')
if [ "$before" = "$after" ]; then ok_line "lesson dir untouched ($after entries)"; else fail_line "lesson dir changed: $before -> $after entries"; fi

echo "$pass/$total passed"
[ "$pass" = "$total" ]
