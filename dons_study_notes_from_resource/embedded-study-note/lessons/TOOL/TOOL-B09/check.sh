#!/usr/bin/env bash
# TOOL-B09 checker: static libraries & link order.
#   bash check.sh <starter|solution|practice>
# Sources <variant>.link (link lines + predictions), builds the archives from src/,
# links with Apple ld64 on the Mac and with GNU ld.bfd / ld.lld / arm-none-eabi-gcc
# in the Lima VM `elinux`, and checks results + map/nm/-y evidence.
set -u
cd "$(dirname "$0")" || exit 2
LESSON=$(pwd)
V=${1:-}
case "$V" in
starter | solution | practice) ;;
*) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;;
esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk

# shellcheck disable=SC1090
source "./$V.link" || { echo "FAIL cannot source $V.link"; exit 1; }

W=/tmp/TOOL-B09_work/$V
rm -rf "$W"
mkdir -p "$W/objs"
OUT=$W/results.txt
: >"$OUT"
pass() { echo "PASS $*" | tee -a "$OUT"; }
fail() { echo "FAIL $*" | tee -a "$OUT"; }
check() { if [ "$1" = 1 ]; then pass "$2"; else fail "$2"; fi; }
pred() { # pred <label> <predicted> <actual ok|fail>
	if [ "$2" = "$3" ]; then pass "$1: predicted $2, actual $3"; else fail "$1: predicted $2, actual $3"; fi
}
has_word() { case " $1 " in *" $2 "*) return 0 ;; esac; return 1; }
forced_load() { case "$1" in *whole-archive* | *all_load* | *force_load*) return 0 ;; esac; return 1; }

# ===================== Mac: Apple ld64 =====================
echo "--- Mac (Apple ld64) ---"
(
	cd "$W" || exit 1
	for f in "$LESSON"/src/a/*.c "$LESSON"/src/b/*.c "$LESSON"/src/p/*.c; do
		cc -std=c11 -O2 -Wall -Wextra -Werror -c "$f" -o "objs/$(basename "${f%.c}").o" || exit 1
	done
	cp objs/main.o objs/main_b.o objs/main_p.o .
	ar rcs libfoo.a objs/foo_a.o objs/foo_b.o objs/foo_unused.o
	ar rcs libbar.a objs/bar_a.o objs/bar_unused.o
	ar rcs libcrc_fast.a objs/crc_fast.o
	ar rcs libcrc_ref.a objs/crc_ref.o objs/crc_vectors.o
	ar rcs libtests.a objs/test_uart.o objs/test_crc.o objs/test_ring.o 2>/dev/null
) || { echo "FAIL Mac build"; echo "0/1 passed"; exit 1; }
cd "$W" || exit 2

mac_link() { cc "$@" -o mac_tmp >/dev/null 2>&1 && echo ok || echo fail; rm -f mac_tmp; }
# shellcheck disable=SC2086
pred "A ld64 main.o -lfoo -lbar" "${PRED_LD64_FOO_BAR-?}" "$(mac_link main.o -L. -lfoo -lbar)"
pred "A ld64 main.o -lbar -lfoo" "${PRED_LD64_BAR_FOO-?}" "$(mac_link main.o -L. -lbar -lfoo)"
pred "A ld64 -lfoo -lbar main.o" "${PRED_LD64_LIBS_FIRST-?}" "$(mac_link -L. -lfoo -lbar main.o)"

ok=1
has_word "${B_MAC-}" -lcrc_fast && has_word "${B_MAC-}" -lcrc_ref || ok=0
forced_load "${B_MAC-}" && ok=0
check $ok "B ld64 rules: both -lcrc_fast and -lcrc_ref on the line, no forced load"
# shellcheck disable=SC2086
cc ${B_MAC-} -Wl,-map,b_mac.map -o b_mac >b_mac.log 2>&1
got=$([ -x b_mac ] && ./b_mac)
check "$([ "$got" = "B: crc32 impl=fast vectors=3/3 ok" ] && echo 1)" "B ld64 links and runs: '${got:-link failed}'"
ok=0
[ -f b_mac.map ] && grep -q 'libcrc_fast.a(crc_fast.o)' b_mac.map &&
	grep -q 'libcrc_ref.a(crc_vectors.o)' b_mac.map && ! grep -q 'crc_ref.o)' b_mac.map && ok=1
check $ok "B ld64 map: crc_fast.o + crc_vectors.o loaded, crc_ref.o not loaded"

if [ -n "${P_MAC-}" ]; then
	# shellcheck disable=SC2086
	cc ${P_MAC} -o p_mac >p_mac.log 2>&1
	got=$([ -x p_mac ] && ./p_mac)
	check "$([ "$got" = "P: 3 tests registered, 3 passed" ] && echo 1)" "P ld64 self-registering tests: '${got:-link failed}'"
fi

# ===================== Lima VM: GNU ld.bfd, ld.lld, arm-none-eabi-gcc =====================
echo "--- Lima VM elinux (GNU ld.bfd 2.46 / ld.lld 21 / arm-none-eabi-gcc 14.2) ---"
{
	declare -p LESSON V
	for n in PRED_BFD_FOO_BAR PRED_BFD_BAR_FOO PRED_BFD_BAR_FOO_BAR PRED_BFD_LIBS_FIRST \
		PRED_LLD_BAR_FOO PRED_LLD_LIBS_FIRST A_GNU B_GNU C_GNU P_GNU; do
		printf '%s=%q\n' "$n" "${!n-}"
	done
	cat <<'VM'
set -u
W=/tmp/TOOL-B09_work/$V
rm -rf "$W"; mkdir -p "$W/objs"; cd "$W" || exit 1
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; }
check() { if [ "$1" = 1 ]; then pass "$2"; else fail "$2"; fi; }
pred() { if [ "$2" = "$3" ]; then pass "$1: predicted $2, actual $3"; else fail "$1: predicted $2, actual $3"; fi; }
has_word() { case " $1 " in *" $2 "*) return 0 ;; esac; return 1; }
forced_load() { case "$1" in *whole-archive*|*all_load*|*force_load*) return 0 ;; esac; return 1; }
members() { awk '/^Archive member included/{f=1;next} f&&/^(Merging|As-needed|Discarded|Memory|Allocating)/{exit} f' "$1" |
	grep -oE '^\./lib[a-z_]+\.a\([a-z_]+\.o\)' | sort | tr '\n' ' '; }

for f in "$LESSON"/src/a/*.c "$LESSON"/src/b/*.c "$LESSON"/src/p/*.c; do
	gcc -std=c11 -O2 -Wall -Wextra -Werror -c "$f" -o "objs/$(basename "${f%.c}").o" || { echo "FAIL VM build $f"; exit 1; }
done
for f in "$LESSON"/src/c/*.c; do
	arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os -Wall -Wextra -Werror -c "$f" -o "objs/$(basename "${f%.c}").o" ||
		{ echo "FAIL VM build $f"; exit 1; }
done
cp objs/main.o objs/main_b.o objs/main_p.o objs/fw_main.o .
ar rcs libfoo.a objs/foo_a.o objs/foo_b.o objs/foo_unused.o
ar rcs libbar.a objs/bar_a.o objs/bar_unused.o
ar rcs libcrc_fast.a objs/crc_fast.o
ar rcs libcrc_ref.a objs/crc_ref.o objs/crc_vectors.o
ar rcs libtests.a objs/test_uart.o objs/test_crc.o objs/test_ring.o
arm-none-eabi-ar rcs libuart.a objs/uart_hw.o objs/uart_retarget.o

lk() { local ld=$1; shift; gcc -fuse-ld=$ld "$@" -o vm_tmp >/dev/null 2>&1 && echo ok || echo fail; rm -f vm_tmp; }
pred "A bfd main.o -lfoo -lbar"       "${PRED_BFD_FOO_BAR:-?}"     "$(lk bfd main.o -L. -lfoo -lbar)"
pred "A bfd main.o -lbar -lfoo"       "${PRED_BFD_BAR_FOO:-?}"     "$(lk bfd main.o -L. -lbar -lfoo)"
pred "A bfd main.o -lbar -lfoo -lbar" "${PRED_BFD_BAR_FOO_BAR:-?}" "$(lk bfd main.o -L. -lbar -lfoo -lbar)"
pred "A bfd -lfoo -lbar main.o"       "${PRED_BFD_LIBS_FIRST:-?}"  "$(lk bfd -L. -lfoo -lbar main.o)"
pred "A lld main.o -lbar -lfoo"       "${PRED_LLD_BAR_FOO:-?}"     "$(lk lld main.o -L. -lbar -lfoo)"
pred "A lld -lfoo -lbar main.o"       "${PRED_LLD_LIBS_FIRST:-?}"  "$(lk lld -L. -lfoo -lbar main.o)"

# ---- Part A: the fixed line on GNU ld.bfd ----
gcc -fuse-ld=bfd $A_GNU -Wl,-Map=a.map -o a_bfd >a_bfd.log 2>&1
got=$([ -x a_bfd ] && ./a_bfd)
check "$([ "$got" = "A: foo_entry(5) = 71" ] && echo 1)" "A bfd links and runs: '${got:-link failed}'"
ok=0
if [ -x a_bfd ]; then
	m=$(members a.map)
	[ "$m" = "./libbar.a(bar_a.o) ./libfoo.a(foo_a.o) ./libfoo.a(foo_b.o) " ] && ! nm a_bfd | grep -q _unused && ok=1
	echo "     map members: $m"
fi
check $ok "A bfd only needed members pulled (foo_a, foo_b, bar_a; no *_unused in nm)"
gcc -fuse-ld=lld $A_GNU -o a_lld >/dev/null 2>&1
got=$([ -x a_lld ] && ./a_lld)
check "$([ "$got" = "A: foo_entry(5) = 71" ] && echo 1)" "A lld same line still links: '${got:-link failed}'"

# ---- Part B ----
ok=1
has_word "$B_GNU" -lcrc_fast && has_word "$B_GNU" -lcrc_ref || ok=0
forced_load "$B_GNU" && ok=0
check $ok "B GNU rules: both -lcrc_fast and -lcrc_ref on the line, no forced load"
gcc -fuse-ld=bfd $B_GNU -Wl,-Map=b.map -o b_bfd >b_bfd.log 2>&1
got=$([ -x b_bfd ] && ./b_bfd)
check "$([ "$got" = "B: crc32 impl=fast vectors=3/3 ok" ] && echo 1)" "B bfd links and runs: '${got:-link failed}'"
ok=0
[ -x b_bfd ] && [ "$(members b.map)" = "./libcrc_fast.a(crc_fast.o) ./libcrc_ref.a(crc_vectors.o) " ] && ok=1
check $ok "B bfd map: crc_fast.o + crc_vectors.o pulled, crc_ref.o not"
gcc -fuse-ld=lld $B_GNU -o b_lld >/dev/null 2>&1
got=$([ -x b_lld ] && ./b_lld)
check "$([ "$got" = "B: crc32 impl=fast vectors=3/3 ok" ] && echo 1)" "B lld links and runs: '${got:-link failed}'"

# ---- Part C: Cortex-M firmware ----
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb --specs=nano.specs --specs=nosys.specs $C_GNU \
	-Wl,-y,_write -Wl,-Map=fw.map -o fw.elf >c.log 2>&1
crc=$?
ok=0
[ $crc = 0 ] && [ -f fw.elf ] && has_word "$C_GNU" -luart && ! grep -q 'uart_[a-z]*\.o' <<<"$C_GNU" && ok=1
check $ok "C links with libuart.a as an archive (no loose uart_*.o on the line)"
def=$(grep -oE '[^ /]+\.a\([^)]+\): definition of _write' c.log | head -1)
check "$([ "$def" = "libuart.a(uart_retarget.o): definition of _write" ] && echo 1)" "C -y _write: '${def:-none}'"
check "$(grep -q '_write is not implemented' c.log || echo 1)" "C no '_write is not implemented' warning (libnosys stub not used)"
[ -f fw.elf ] && echo "     $(arm-none-eabi-size fw.elf | tail -1 | awk '{print "fw.elf text=" $1 " data=" $2 " bss=" $3}')"

# ---- Practice: self-registering tests ----
if [ -n "$P_GNU" ]; then
	for ld in bfd lld; do
		gcc -fuse-ld=$ld $P_GNU -o p_$ld >p_$ld.log 2>&1
		got=$([ -x p_$ld ] && ./p_$ld)
		check "$([ "$got" = "P: 3 tests registered, 3 passed" ] && echo 1)" "P $ld self-registering tests: '${got:-link failed}'"
	done
fi
echo VM_DONE
VM
} >"$W/vm_script.sh"

limactl shell elinux -- bash -s <"$W/vm_script.sh" >"$W/vm_out.txt" 2>&1
grep -E '^(PASS|FAIL|     )' "$W/vm_out.txt"
grep -E '^(PASS|FAIL)' "$W/vm_out.txt" >>"$OUT"
grep -q '^VM_DONE' "$W/vm_out.txt" || { echo "FAIL VM part did not finish (see $W/vm_out.txt)"; echo "FAIL vm" >>"$OUT"; }

P=$(grep -c '^PASS' "$OUT")
T=$(grep -cE '^(PASS|FAIL)' "$OUT")
echo "$P/$T passed"
[ "$P" = "$T" ] && [ "$T" -gt 0 ] && exit 0
exit 1
