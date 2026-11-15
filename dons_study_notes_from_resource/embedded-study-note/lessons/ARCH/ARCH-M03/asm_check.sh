#!/bin/sh
# ARCH-M03 asm_check.sh — "branchy 는 정말 분기하고, branchless 는 정말 csel 인가" 를 -S 로 기계적으로 확인
# usage: sh asm_check.sh            (demo_kernels.c 와 같은 디렉터리에서)
# 규칙: 함수 본문에서 루프 끝 b.ne/b.eq 를 뺀 조건 분기(b.hi/b.lo/b.ls/b.lt/b.ge…)가 있으면 "분기 있음".
set -u
S=${TMPDIR:-/tmp}/esn_ARCH-M03_kernels.s
cc -std=c11 -O2 -S demo_kernels.c -o "$S" || exit 1

body() {  # $1 = 함수 이름 → 그 함수의 명령 줄만
  awk -v f="_$1:" '$1==f{on=1;next} on&&/^_[a-z]/{exit} on&&/^Lfunc_end/{exit} on' "$S" | grep -v '^[[:space:]]*[.;]'
}
fail=0
expect() {  # $1 함수  $2 기대(branch|csel|neon)
  b=$(body "$1")
  data_br=$(printf '%s\n' "$b" | grep -cE '^[[:space:]]+b\.(hi|hs|lo|ls|lt|le|gt|ge|mi|pl)[[:space:]]')
  csel=$(printf '%s\n' "$b" | grep -cE '^[[:space:]]+(csel|cinc|csinc|csneg)[[:space:]]')
  neon=$(printf '%s\n' "$b" | grep -cE '^[[:space:]]+cm(hi|hs|ge|gt)\.')
  case $2 in
    branch) ok=$([ "$data_br" -gt 0 ] && [ "$csel" -eq 0 ] && echo 1 || echo 0) ;;
    csel)   ok=$([ "$data_br" -eq 0 ] && [ "$csel" -gt 0 ] && echo 1 || echo 0) ;;
    neon)   ok=$([ "$neon" -gt 0 ] && echo 1 || echo 0) ;;
  esac
  st=PASS; [ "$ok" = 1 ] || { st=FAIL; fail=1; }
  printf '%s  %-16s expect %-6s  data-branches=%s csel=%s neon-cmp=%s\n' "$st" "$1" "$2" "$data_br" "$csel" "$neon"
}
expect k_auto_u8      neon
expect k_scalar_u8    csel
expect k_branchy_u8   branch
expect k_csel_u8      csel
expect k_mask_u8      csel
expect k_branchy_i32  branch
expect k_csel_i32     csel
expect k_max_branchy  branch
expect k_max_csel     csel
expect k_hash_natural csel
expect k_hash_branchy branch
expect k_hash_csel    csel
exit $fail
