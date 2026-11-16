# TOOL-M14 symbolicator (bash 3.2, macOS).  Source this file, then call
#     symbolicate CRASH_LOG STRIPPED_BINARY DSYM
# Output, one line per frame (inlined frames first, innermost first, same frame number):
#     #0 rd_le16 (crashgen.c:39) [inlined]
#     #0 parse_record (crashgen.c:64)
#     #3 ?? 0x000000018753e054 (not in image)
# Exit 3 + "error: UUID mismatch ..." on stderr (nothing on stdout) if log / binary / dSYM disagree.

crash_field() {            # crash_field LOG KEY  -> first word after "KEY:"
    awk -v k="$2:" '$1 == k {print $2; exit}' "$1"
}
crash_uuid() {             # UUID the crashing process reported for its own image
    awk '$1 == "image:" {for (i = 1; i < NF; i++) if ($i == "UUID") {print $(i + 1); exit}}' "$1"
}
macho_uuid() {             # UUID of a Mach-O file or dSYM bundle (arm64 slice)
    dwarfdump --uuid "$1" 2>/dev/null | awk '$1 == "UUID:" && $3 == "(arm64)" {print $2; exit}'
}

# TODO 1: the address where __TEXT really was in THIS run (ASLR moves it every run).
crash_load_address() {     # crash_load_address LOG -> 0x...
    crash_field "$1" text_vmaddr   # TODO
}

# TODO 2: which address to look up for frame N.  Frame 0 = the faulting pc itself;
#         frames >= 1 hold RETURN addresses (the instruction after the call).
lookup_address() {         # lookup_address FRAME PC -> 0x...
    printf '0x%x\n' $(( $2 ))   # TODO
}

# TODO 3: refuse to symbolicate with debug info from a different build.
uuid_check() {             # uuid_check LOG BINARY DSYM -> 0 if all three UUIDs are equal
    local c b d
    c=$(crash_uuid "$1"); b=$(macho_uuid "$2"); d=$(macho_uuid "$3")
    if [ -n "$c" ] && [ "$c" = "$b" ]; then return 0; fi   # TODO
    echo "error: UUID mismatch: crash log ${c:-none}, binary ${b:-none}, dSYM ${d:-none}" >&2
    return 1
}

# TODO 4: ask atos for one address, including inlined frames.
atos_frame() {             # atos_frame DWARF_FILE LOAD_ADDRESS ADDRESS -> atos lines
    atos -o "$1" -arch arm64 -l "$2" "$3"   # TODO
}

symbolicate() {
    local log=$1 bin=$2 dsym=$3 dwarf la lo hi n pc tag addr
    dwarf=$(ls -d "$dsym"/Contents/Resources/DWARF/* 2>/dev/null | head -1)
    [ -f "$dwarf" ] || { echo "error: no DWARF file in $dsym" >&2; return 2; }
    uuid_check "$log" "$bin" "$dsym" || return 3
    la=$(crash_load_address "$log")
    lo=$(crash_field "$log" load_address); hi=$(crash_field "$log" text_end)
    awk '/^backtrace:/ {f = 1; next} /^end/ {f = 0} f {print $1, $2, $3}' "$log" |
    while read -r n pc tag; do
        if [ $(( pc >= lo && pc < hi )) -eq 0 ]; then
            echo "#$n ?? $pc (not in image)"; continue
        fi
        addr=$(lookup_address "$n" "$pc")
        atos_frame "$dwarf" "$la" "$addr" | grep -v '^$' | sed -e 's/ (in [^)]*)//' |
            awk -v n="$n" '{l[NR] = $0} END {for (i = 1; i <= NR; i++) print "#" n " " l[i] (i < NR ? " [inlined]" : "")}'
    done
}
