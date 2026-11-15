#!/usr/bin/env bash
# TOOL-B01 checker:  bash check.sh <starter|solution|practice>
#   Sources <variant>.sh, then asks first_failing_stage about 18 inputs (bad_*.c snippets, pairs,
#   the real main.c+util.c project) and compares with the expected table below.
#   practice: also checks stage_artifact_kind on real .c/.i/.bc/.s/.o/exe and ELF objects.
#   Works only in /tmp/TOOL-B01_work/<variant>; never writes into the lesson directory.
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
V=${1:-}
case "$V" in starter|solution|practice) ;; *) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;; esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-B01_work/$V
rm -rf "$W" && mkdir -p "$W" || exit 2
export STAGE_DIR=$W/stage
unset CC STAGE_CFLAGS
before=$(ls -A "$L" | wc -l | tr -d ' ')
. "$L/$V.sh"

pass=0 total=0
ok_line()   { pass=$((pass + 1)); total=$((total + 1)); printf 'PASS  %s\n' "$1"; }
fail_line() { total=$((total + 1)); printf 'FAIL  %s\n' "$1"; }

expect_stage() {            # expect_stage <want> file.c [file2.c ...]
    local want=$1 got why label
    shift
    label=$(echo "$*" | sed "s#$L/##g")
    got=$(first_failing_stage "$@" 2>"$W/err.txt")
    why=$(grep -m1 -E 'error|Undefined symbols|duplicate symbol' "$W/err.txt" \
          | sed -e "s#$L/##g" -e 's#/var/folders/[^:]*/#$TMPDIR/#' | cut -c1-72)
    if [ "$got" = "$want" ]; then
        ok_line "$(printf '%-34s -> %-10s | %s' "$label" "$got" "$why")"
    else
        fail_line "$(printf '%-34s -> %-10s (want %s) | %s' "$label" "$(echo $got)" "$want" "$why")"
    fi
}

echo "== first_failing_stage ($V.sh, cc = $(cc --version | head -1 | sed 's/ (.*//'))"
expect_stage preprocess "$L/bad_error.c"
expect_stage preprocess "$L/bad_missing_header.c"
expect_stage preprocess "$L/bad_unterminated_if.c"
expect_stage preprocess "$L/bad_macro_args.c"
expect_stage compile    "$L/bad_syntax.c"
expect_stage compile    "$L/bad_type.c"
expect_stage compile    "$L/bad_macro_paren.c"
expect_stage compile    "$L/bad_implicit.c"
expect_stage compile    "$L/bad_attr_error.c"
expect_stage compile    "$L/bad_asm_mnemonic.c"
expect_stage assemble   "$L/bad_asm_org.c"
expect_stage link       "$L/bad_undefined.c"
expect_stage link       "$L/bad_dup_a.c" "$L/bad_dup_b.c"
expect_stage link       "$L/bad_no_main.c"
expect_stage link       "$L/main.c"
expect_stage ok         "$L/ok_single.c"
expect_stage ok         "$L/main.c" "$L/util.c"
echo "== flags reach the stages, stage-major order over several files"
STAGE_CFLAGS="-std=c11 -Wall -DBOARD_REV=2" expect_stage ok "$L/bad_error.c"
expect_stage compile    "$L/bad_no_main.c" "$L/bad_syntax.c"
expect_stage preprocess "$L/bad_asm_org.c" "$L/bad_macro_args.c"
out=$(first_failing_stage "$L/ok_single.c" 2>/dev/null)
if [ "$out" = ok ] && [ "$(printf '%s\n' "$out" | wc -l | tr -d ' ')" = 1 ]; then
    ok_line "stdout is exactly one word"
else
    fail_line "stdout is exactly one word (got: $(printf '%s' "$out" | tr '\n' '|'))"
fi

if [ "$V" = practice ]; then
    echo "== extra snippets"
    expect_stage link       "$L/bad_static_main.c" "$L/bad_static_helper.c"
    STAGE_CFLAGS="-std=c89 -Wall" expect_stage compile "$L/bad_implicit.c"   # Apple clang: -Werror=implicit-function-declaration by default
    echo "== stage_artifact_kind (decided by content; file(1) shown for comparison)"
    A=$W/art
    mkdir -p "$A" && cp "$L"/main.c "$L"/util.c "$L"/util.h "$L"/config.h "$A"/
    (cd "$A" && cc -std=c11 -save-temps main.c util.c -o fw \
        && cc --target=aarch64-linux-gnu -ffreestanding -c util.c -o util_a64.o \
        && cc --target=thumbv7em-none-eabi -mcpu=cortex-m4 -ffreestanding -c util.c -o util_m4.o \
        && cp main.o firmware.bin && cp main.s notes.txt && : > empty.dat && printf 'BL\000\001' > blob.bin) 2>"$W/art_err.txt" \
        || { cat "$W/art_err.txt"; fail_line "build artifacts"; }
    expect_kind() {
        local want=$1 f=$2 got
        got=$(stage_artifact_kind "$A/$f" 2>/dev/null)
        if [ "$got" = "$want" ]; then
            ok_line "$(printf '%-14s -> %-12s | file: %s' "$f" "$got" "$(file -b "$A/$f" | cut -c1-48)")"
        else
            fail_line "$(printf '%-14s -> %-12s (want %s)' "$f" "$got" "$want")"
        fi
    }
    expect_kind source       main.c
    expect_kind source       config.h
    expect_kind preprocessed main.i
    expect_kind bitcode      main.bc
    expect_kind asm          main.s
    expect_kind object       main.o
    expect_kind executable   fw
    expect_kind object       firmware.bin
    expect_kind asm          notes.txt
    expect_kind object       util_a64.o
    expect_kind object       util_m4.o
    expect_kind unknown      empty.dat
    expect_kind unknown      blob.bin
fi

after=$(ls -A "$L" | wc -l | tr -d ' ')
if [ "$before" = "$after" ]; then ok_line "lesson dir untouched ($after entries)"; else fail_line "lesson dir changed: $before -> $after entries"; fi

echo "$pass/$total passed"
[ "$pass" = "$total" ]
