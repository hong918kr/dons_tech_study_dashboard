#!/usr/bin/env bash
# C-A08 계약 돌연변이 테스트 (mutation test for compile-time contracts)
#
#   ./mutants.sh solution.c      # 기대: 15/15 killed
#   ./mutants.sh starter.c       # 계약이 비어 있으면 대부분 survived
#
# 각 돌연변이는 대상 파일의 사본에 perl 치환 하나를 적용한 것이다 (필드 추가, 재배열, 타입 변경, 설정값 변경...).
#   KILLED   : 빌드가 "static assertion failed" 로 실패 — 계약이 잡았다 (원하는 결과)
#   SURVIVED : 빌드 성공 — 아무 계약도 이 변경을 못 잡았다
#   ERROR    : 빌드는 실패했지만 static assertion 때문이 아니다 — 계약이 아니라 우연이 잡았다 (killed 로 안 침)
set -u
src=${1:?usage: mutants.sh <file.c>}
[ -f "$src" ] || { echo "no such file: $src"; exit 2; }
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
CC=${CC:-cc}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/C-A08_mut.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# 원본은 반드시 깨끗하게 컴파일돼야 한다 (아니면 모든 돌연변이가 가짜로 killed 가 된다)
if ! "$CC" -std=c11 -fsyntax-only "$src" 2>"$tmp/base.err"; then
    echo "baseline $src does not compile:"; cat "$tmp/base.err"; exit 2
fi

killed=0; total=0
mutant() {   # mutant <이름> <perl -0pe 치환식>
    local name=$1 expr=$2 out="$tmp/$1.c" msg
    total=$((total + 1))
    perl -0pe "$expr" "$src" > "$out"
    if cmp -s "$src" "$out"; then
        printf '%-20s NOT APPLIED (치환식이 아무것도 바꾸지 못함)\n' "$name"; exit 2
    fi
    if "$CC" -std=c11 -fsyntax-only "$out" 2>"$out.err"; then
        printf '%-20s SURVIVED\n' "$name"
    elif grep -q 'static assertion failed' "$out.err"; then
        killed=$((killed + 1))
        msg=$(grep -m1 'static assertion failed' "$out.err" \
              | perl -ne 'print $1 if /static assertion failed(?: due to requirement .*\x27)?: (.*)$/')
        printf '%-20s KILLED   <- %s\n' "$name" "$msg"
    else
        msg=$(grep -m1 'error:' "$out.err" | perl -ne 'print $1 if /error: (.*)$/')
        printf '%-20s ERROR    (%s)\n' "$name" "$msg"
    fi
}

mutant pkt-add-field      's/(    uint8_t  type;[^\n]*\n)/$1    uint8_t  flags;\n/'
mutant pkt-reorder        's/(    uint16_t len;[^\n]*\n)(    uint8_t  status;[^\n]*\n)/$2$1/'
mutant pkt-len-signed     's/    uint16_t len; /    int16_t  len; /'
mutant pkt-enum-field     's/    uint8_t  type; /    pkt_type_t type; /'
mutant err-add-no-string  's/(    ERR_COUNT )/    ERR_NOMEM,\n$1/'
mutant err-overflow-field 's/(    ERR_COUNT )/    ERR_NOMEM,\n    ERR_STATE,\n$1/; s/(    \[ERR_HW\][^\n]*\n)/$1    [ERR_NOMEM]   = "NOMEM",\n    [ERR_STATE]   = "STATE",\n/'
mutant spi-reserved-miss  's/RESERVED0\[4\]/RESERVED0[3]/'
mutant spi-mask-overlap   's/(#define SPI_CR1_SPE +)\(1u << 6\)/$1(1u << 5)/'
mutant spi-mask-typo      's/(#define SPI_CR1_BR +)\(0x7u << 3\)/$1(0x5u << 3)/'
mutant spi-br-too-big     's/(#define SPI_BR_DIV +)5u/${1}9u/'
mutant ring-not-pow2      's/(#define RING_N +)8u/${1}6u/'
mutant buf-too-big        's/(#define RX_BUF_SIZE +)512u/${1}65536u/'
mutant buf-too-small      's/(#define RX_BUF_SIZE +)512u/${1}256u/'
mutant desc-ptr-next      's/    uint32_t next; /    void    *next; /'
mutant desc-drop-alignas  's/_Alignas\(DESC_ALIGN\) //'

echo "$killed/$total killed"
[ "$killed" -eq "$total" ]
