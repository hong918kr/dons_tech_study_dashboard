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
