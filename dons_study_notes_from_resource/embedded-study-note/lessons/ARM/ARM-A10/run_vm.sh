#!/bin/bash
# ENV: linux (Lima VM elinux, aarch64 Ubuntu — CPU 가 paca pacg bti 를 노출)
# ARM-A10 run_vm.sh — 실제 하드웨어 PAC/BTI 로 "반환 주소 위조"와 "착지 패드 없는 간접 분기"가 막히는지 확인한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-A10/run_vm.sh pac'    # 체커가 돌리는 것
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-A10/run_vm.sh gen'    # gcc/clang 코드 생성 덤프 (gen_vm.txt)
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-A10/run_vm.sh qemu'   # 같은 BTI 표를 qemu-aarch64 -cpu max 로
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/ARM/ARM-A10/run_vm.sh stat'   # forge/replay2 1000회씩: PAC 우연 일치 빈도
# Mac 홈은 VM 에서 읽기 전용 → /tmp/ARM-A10_work 로 복사해서 빌드한다. 모든 위반은 fork 한 자식 안에서만 일어난다.
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/ARM-A10_work
mkdir -p "$W"
cp "$SRC"/demo_*.c "$SRC"/demo_*.S "$W"/ 2>/dev/null
cd "$W" || exit 1
CF="-std=c11 -O2 -Wall -Wextra -fno-omit-frame-pointer"

pass=0; total=0
check() {   # check <이름> <기대> <실제>
    total=$((total + 1))
    local shown=$3; case "$3" in *$'\n'*) shown="(표 $(printf '%s\n' "$3" | wc -l)줄 일치 여부)";; esac
    if [ "$2" = "$3" ]; then pass=$((pass + 1)); echo "PASS $1: $shown"
    else echo "FAIL $1: expected [$2] got [$3]"; fi
}
result() { ./"$1" "$2" | sed -n 's/^RESULT [a-z0-9]*: //p'; }   # "exit 42" / "signal 4 (Illegal instruction)"
# PAC 이 잡아야 하는 검사: 이 VM 의 PAC 필드는 7비트라 위조값이 우연히 맞을 확률이 1/128 (실측 forge 1000회 중 8회 exit 42).
# 실행마다(exec) 키가 새로 뽑히므로(fork_keys.txt) 최대 4번 실행해 기대 결과가 한 번이라도 나오면 통과,
# 그 전에 나온 다른 결과는 "우연 일치" 로 출력한다. 4번 모두 빗나갈 확률 ≈ (1/128)^4 ≈ 4e-9.
result_pac() {   # result_pac <bin> <mode> <기대>
    local r k
    for k in 1 2 3 4; do
        r=$(result "$1" "$2")
        [ "$r" = "$3" ] && break
        echo "  ($1 $2 attempt $k: [$r] — 7비트 PAC 우연 일치(1/128)로 인증 통과, 새 키로 다시 실행)" >&2
    done
    echo "$r"
}
feat() { readelf -n "$1" | sed -n 's/.*AArch64 feature: //p' | tr -d ' '; }

case "${1:-pac}" in
pac)
    echo "== CPU: $(grep -m1 -o -w -E 'paca|pacg|bti' /proc/cpuinfo | sort -u | tr '\n' ' ')(from /proc/cpuinfo)"
    for f in paca pacg bti; do check "cpuinfo has $f" yes "$(grep -m1 -q -w $f /proc/cpuinfo && echo yes || echo no)"; done
    gcc $CF -mbranch-protection=none     demo_forge.c -o forge_none || exit 1
    gcc $CF -mbranch-protection=standard demo_forge.c -o forge_std  || exit 1
    check "forge_none GNU property" "" "$(feat forge_none)"
    check "forge_std  GNU property" "BTI,PAC,GCS" "$(feat forge_std)"
    echo "== PAC: 저장된 LR 위조 / 재사용 (자식 프로세스)"
    for m in forge replay replay2; do echo "--- forge_none $m"; ./forge_none $m; done
    for m in forge replay replay2; do echo "--- forge_std $m";  ./forge_std $m;  done
    check "none: forged raw LR → lands in landing()"   "exit 42" "$(result forge_none forge)"
    check "none: replay same SP → redirected"          "exit 43" "$(result forge_none replay)"
    check "none: replay other SP → redirected"         "exit 43" "$(result forge_none replay2)"
    r=$(result_pac forge_std forge "signal 4 (Illegal instruction)")
    case "$r" in "signal 4 "*) echo "  (FEAT_FPAC: autiasp 자체가 예외 → SIGILL)";; "signal 11 "*) echo "  (FPAC 없음: 깨진 포인터로 ret → SIGSEGV)";; esac
    check "std: forged raw LR → killed by PAC (FPAC → SIGILL)" "signal 4 (Illegal instruction)" "$r"
    check "std: replay same SP → STILL redirected (SP modifier weakness)" "exit 43" "$(result forge_std replay)"
    check "std: replay other SP → killed by PAC" "signal 4 (Illegal instruction)" "$(result_pac forge_std replay2 "signal 4 (Illegal instruction)")"
    echo "== BTI: 착지 패드 × 분기 종류"
    gcc -O2 -mbranch-protection=standard -DWITH_NOTE demo_bti.c demo_bti_pads.S -o bti_note  || exit 1
    gcc -O2 -mbranch-protection=standard             demo_bti.c demo_bti_pads.S -o bti_nonote || exit 1
    gcc -O2 -mbranch-protection=standard demo_bti.c demo_bti_pads.S -Wl,-z,force-bti -o bti_force 2>force_warn.txt || exit 1
    sed 's/^/  ld: /' force_warn.txt | sed 's#/tmp/[^:]*\.o#<demo_bti_pads.o>#'
    check "bti_note   GNU property" "BTI,PAC" "$(feat bti_note)"
    check "bti_nonote GNU property (.S 에 노트 없음 → AND 로 탈락)" "" "$(feat bti_nonote)"
    check "bti_force  GNU property (-z force-bti)" "BTI" "$(feat bti_force)"
    expect_note='pad\via   | blr (10)     | br x16 (01)  | br x9 (11)  
none      | SIGILL       | SIGILL       | SIGILL      
bti       | SIGILL       | SIGILL       | SIGILL      
bti c     | ok(12)       | ok(12)       | SIGILL      
bti j     | SIGILL       | ok(13)       | ok(13)      
bti jc    | ok(14)       | ok(14)       | ok(14)      
paciasp   | ok(15)       | ok(15)       | SIGILL      '
    expect_off='pad\via   | blr (10)     | br x16 (01)  | br x9 (11)  
none      | ok(10)       | ok(10)       | ok(10)      
bti       | ok(11)       | ok(11)       | ok(11)      
bti c     | ok(12)       | ok(12)       | ok(12)      
bti j     | ok(13)       | ok(13)       | ok(13)      
bti jc    | ok(14)       | ok(14)       | ok(14)      
paciasp   | ok(15)       | ok(15)       | ok(15)      '
    for b in bti_note bti_nonote bti_force; do echo "--- $b"; ./$b; done
    check "bti_note matrix (BTI enforced)"     "$expect_note" "$(./bti_note)"
    check "bti_nonote matrix (not enforced)"   "$expect_off"  "$(./bti_nonote)"
    check "bti_force matrix (enforced)"        "$expect_note" "$(./bti_force)"
    echo "$pass/$total passed"
    [ "$pass" -eq "$total" ]; exit $?
    ;;
gen)
    for v in "gcc none|gcc -O2 -mbranch-protection=none" "gcc standard|gcc -O2 -mbranch-protection=standard" \
             "clang standard|clang -O2 -mbranch-protection=standard"; do
        n=${v%%|*}; c=${v#*|}
        $c demo_gen.c demo_gen_main.c -o gen_exe || exit 1
        ./gen_exe; rc=$?
        echo "##### $n: $c demo_gen.c demo_gen_main.c   (run exit=$rc, GNU property: $(feat gen_exe))"
        objdump -d gen_exe | sed -n -E '/<(leaf_add|nonleaf|early_exit|tail_call|call_ptr)>:/,/^$/p'
    done
    ;;
stat)   # 1000번씩 돌려 PAC 우연 일치 빈도를 센다 (pac_collision_stats.txt, 수 분 걸림)
    gcc $CF -mbranch-protection=standard demo_forge.c -o forge_std || exit 1
    for m in forge replay2; do
        for i in $(seq 1 1000); do result forge_std $m; done | sort | uniq -c | sed "s/^/$m: /"
    done
    ;;
qemu)
    gcc -O2 -mbranch-protection=standard -DWITH_NOTE demo_bti.c demo_bti_pads.S -o bti_note || exit 1
    timeout 60 qemu-aarch64 -cpu max ./bti_note 2>/dev/null
    ;;
*) echo "usage: $0 pac|gen|qemu|stat"; exit 2;;
esac
