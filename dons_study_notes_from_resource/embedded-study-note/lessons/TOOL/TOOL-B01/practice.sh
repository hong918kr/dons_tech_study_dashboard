# TOOL-B01 — first_failing_stage: which of the 4 build stages breaks first?
#   usage:  . ./<variant>.sh ;  first_failing_stage file1.c [file2.c ...]
#   stdout: exactly one word  preprocess | compile | assemble | link | ok
#   stderr: the tool's own diagnostics (so you can read WHY)
#   scratch files (.i .s .o a.out) go to $STAGE_DIR, never next to the sources.
#   CC (default cc) and STAGE_CFLAGS (default "-std=c11 -Wall") can be overridden,
#   e.g.  CC=gcc first_failing_stage x.c   in the Linux VM.
# Sourced by check.sh (bash 3.2 on macOS: no associative arrays, no ${x,,}, no mapfile).

first_failing_stage() {
    local d=${STAGE_DIR:-/tmp/TOOL-B01_work/stage} cc=${CC:-cc} flags=${STAGE_CFLAGS:--std=c11 -Wall}
    local f b objs=""
    [ $# -gt 0 ] || { echo "usage: first_failing_stage file1.c [file2.c ...]" >&2; return 2; }
    mkdir -p "$d" || return 2

    for f in "$@"; do                         # 1) preprocess every file: .c -> .i
        b=$d/$(basename "${f%.c}")
        $cc $flags -E "$f" -o "$b.i" >&2 || { echo preprocess; return 0; }
    done
    for f in "$@"; do                         # 2) compile: .i -> .s  (parse + codegen)
        b=$d/$(basename "${f%.c}")
        $cc $flags -S "$b.i" -o "$b.s" >&2 || { echo compile; return 0; }
    done
    for f in "$@"; do                         # 3) assemble: .s -> .o  (layout, encoding)
        b=$d/$(basename "${f%.c}")
        $cc -c "$b.s" -o "$b.o" >&2 || { echo assemble; return 0; }
        objs="$objs $b.o"
    done
    $cc $objs -o "$d/a.out" >&2 || { echo link; return 0; }   # 4) link
    echo ok
}

# ---- practice: stage_artifact_kind <file> -------------------------------------------------
# Names the pipeline artifact by CONTENT (magic bytes / first lines), not by extension:
#   source | preprocessed | bitcode | asm | object | executable | unknown
# Mach-O 64 (bytes cf fa ed fe): filetype u32 @12  1=MH_OBJECT 2=MH_EXECUTE
# ELF       (bytes 7f 45 4c 46): e_type  u16 @16  1=ET_REL 2=ET_EXEC 3=ET_DYN (PIE exe or .so)
# LLVM bitcode: raw 42 43 c0 de ("BC"), or Apple wrapper de c0 17 0b (0x0B17C0DE little-endian)
stage_artifact_kind() {
    local f=$1 magic t lo
    [ -s "$f" ] || { echo unknown; return 0; }
    magic=$(od -An -tx1 -N4 "$f" | tr -d ' \n')
    case $magic in
    cffaedfe)
        t=$(od -An -tu1 -j12 -N1 "$f" | tr -d ' \n')          # low byte of filetype (LE)
        case $t in 1) echo object ;; 2) echo executable ;; *) echo unknown ;; esac ;;
    7f454c46)
        lo=16; [ "$(od -An -tu1 -j5 -N1 "$f" | tr -d ' \n')" = 2 ] && lo=17   # EI_DATA 2 = big-endian
        t=$(od -An -tu1 -j$lo -N1 "$f" | tr -d ' \n')
        case $t in 1) echo object ;; 2|3) echo executable ;; *) echo unknown ;; esac ;;
    4243c0de|dec0170b)
        echo bitcode ;;
    *)
        if head -1 "$f" | grep -Eq '^# [0-9]+ "'; then
            echo preprocessed                                   # linemarker first line: clang '# 1', gcc '# 0'
        elif grep -Eq '^[[:space:]]*\.(text|data|section|globl|global|p2align|build_version|syntax)([[:space:]]|$)' "$f"; then
            echo asm
        elif [ "$(LC_ALL=C tr -d '\000' < "$f" | wc -c)" != "$(wc -c < "$f")" ]; then
            echo unknown                                        # has NUL bytes: a binary we do not know
        else
            echo source
        fi ;;
    esac
}
