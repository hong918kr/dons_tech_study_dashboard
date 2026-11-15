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

    # TODO: replace this one-shot build with four separate stages, run stage by stage
    #       over ALL files (every file through -E, then every .i through -S, ...):
    #   1) preprocess  $cc $flags -E  x.c  -o $d/x.i    fail -> echo preprocess
    #   2) compile     $cc $flags -S  x.i  -o $d/x.s    fail -> echo compile
    #   3) assemble    $cc -c         x.s  -o $d/x.o    fail -> echo assemble
    #   4) link        $cc  all .o files   -o $d/a.out  fail -> echo link
    #   all good -> echo ok
    # One-shot build: it knows THAT it failed, not WHERE.
    if $cc $flags "$@" -o "$d/a.out" >&2; then
        echo ok
    else
        echo compile
    fi
}
