#!/usr/bin/env bash
# TOOL-M13 demo: which archiver can build libdrv.a (uart.o + sensor.o) so that the link still works?
#   Mac: /usr/bin/ar (Apple), arm-none-eabi-ar (Homebrew binutils, NO lto plugin), arm-none-eabi-gcc-ar,
#        arm-none-eabi-ar --plugin <liblto_plugin.so>, and -ffat-lto-objects
#   VM : arm-none-eabi-ar / native GNU ar / gcc-ar with the same LTO objects (Ubuntu has /usr/lib/bfd-plugins)
#   bash demo_ar.sh   -> ar_mac.txt, ar_vm.txt
set -u
cd "$(dirname "$0")" || exit 2
HERE=$PWD
export PATH=/opt/homebrew/bin:$PATH
W=/tmp/TOOL-M13_work/ar
rm -rf "$W"
mkdir -p "$W"
BASE="-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -std=c11 -ffreestanding -Wall -Wextra -Werror -Ifw -Os -ffunction-sections -fdata-sections"
PLUGIN=$(arm-none-eabi-gcc -print-prog-name=liblto_plugin.so)

case_run() { # case_run <label> <lto flags> <archiver command...>
	local label=$1 lto=$2 d
	shift 2
	d="$W/$(echo "$label" | tr -c 'a-zA-Z0-9\n' '_')"
	mkdir -p "$d"
	for f in startup main crc fmt libc_min uart sensor; do
		# shellcheck disable=SC2086
		arm-none-eabi-gcc $BASE $lto -c "fw/$f.c" -o "$d/$f.o"
	done
	echo "### $label"
	echo "\$ $* libdrv.a uart.o sensor.o"
	(cd "$d" && "$@" libdrv.a uart.o sensor.o 2>&1; echo "  (archiver exit code $?)")
	echo "\$ arm-none-eabi-nm -s libdrv.a | sed -n '/Archive index/,/^\$/p'"
	arm-none-eabi-nm -s "$d/libdrv.a" 2>/dev/null | sed -n '/^Archive index:/,/^$/p' | head -6
	# shellcheck disable=SC2086
	if arm-none-eabi-gcc $BASE $lto -nostdlib -T solution.ld -Wl,--gc-sections "$d"/startup.o "$d"/main.o "$d"/crc.o \
		"$d"/fmt.o "$d"/libc_min.o -L"$d" -ldrv -lgcc -o "$d/fw.elf" 2>"$d/link.err"; then
		echo "link: OK  $(arm-none-eabi-size "$d/fw.elf" | awk 'NR==2{print "text=" $1 " data=" $2 " bss=" $3}')"
	else
		echo "link: FAILED"
		sed -e "s#$d/##g; s#/var/folders/[^ ]*/cc[A-Za-z0-9]*\.#/var/folders/…/ccXXXXXX.#g; s#^/opt/homebrew/opt/arm-none-eabi-binutils/bin/##" "$d/link.err" | head -4
	fi
	echo
}

{
	echo "# Mac: $(arm-none-eabi-gcc --version | head -1), $(arm-none-eabi-ar --version | head -1)"
	echo "# lto plugin of this gcc: ${PLUGIN#/opt/homebrew/Cellar/}"
	echo "# bfd-plugins dir next to Homebrew binutils: $(ls -d /opt/homebrew/opt/arm-none-eabi-binutils/lib/bfd-plugins 2>&1 | sed 's#.*: ##')"
	echo
	case_run "A1 no LTO, Apple /usr/bin/ar" "" /usr/bin/ar rcs
	case_run "A2 no LTO, arm-none-eabi-ar" "" arm-none-eabi-ar rcs
	case_run "A3 -flto, arm-none-eabi-ar" "-flto" arm-none-eabi-ar rcs
	case_run "A4 -flto, arm-none-eabi-gcc-ar" "-flto" arm-none-eabi-gcc-ar rcs
	case_run "A5 -flto, arm-none-eabi-ar --plugin" "-flto" arm-none-eabi-ar --plugin "$PLUGIN" rcs
	case_run "A6 -flto -ffat-lto-objects, arm-none-eabi-ar" "-flto -ffat-lto-objects" arm-none-eabi-ar rcs
} >ar_mac.txt 2>&1

# ---- VM part: same sources, Ubuntu cross binutils + /usr/lib/bfd-plugins/liblto_plugin.so ----
VW=/tmp/TOOL-M13_work/vm_ar
limactl shell elinux -- bash -lc "rm -rf $VW && mkdir -p $VW && cp -r '$HERE/fw' '$HERE/solution.ld' $VW/" || exit 1
limactl shell elinux -- bash -lc '
cd '"$VW"'
BASE="'"$BASE"'"
echo "# VM: $(arm-none-eabi-gcc --version | head -1), $(arm-none-eabi-ar --version | head -1)"
echo "# /usr/lib/bfd-plugins: $(ls /usr/lib/bfd-plugins | tr "\n" " ")"
echo
for f in startup main crc fmt libc_min uart sensor; do arm-none-eabi-gcc $BASE -flto -c fw/$f.c -o $f.o; done
for ar in "arm-none-eabi-ar rcs" "ar rcs" "arm-none-eabi-gcc-ar rcs"; do
    rm -f libdrv.a
    echo "### -flto, $ar"
    $ar libdrv.a uart.o sensor.o 2>&1 | head -2; echo "  (archiver exit code ${PIPESTATUS[0]})"
    arm-none-eabi-nm -s libdrv.a 2>/dev/null | sed -n "/^Archive index:/,/^\$/p" | head -4
    if arm-none-eabi-gcc $BASE -flto -nostdlib -T solution.ld -Wl,--gc-sections startup.o main.o crc.o fmt.o libc_min.o -L. -ldrv -lgcc -o fw.elf 2>link.err; then
        echo "link: OK  $(arm-none-eabi-size fw.elf | awk "NR==2{print \"text=\" \$1 \" data=\" \$2 \" bss=\" \$3}")"
    else
        echo "link: FAILED"; sed "s#/tmp/cc#/tmp/…/cc#g; s#^/usr/lib/gcc/arm-none-eabi/[^ ]*/ld: #ld: #; s#^/usr/bin/arm-none-eabi-ld: #ld: #" link.err | head -3
    fi
    echo
done
' >ar_vm.txt 2>&1
cat ar_mac.txt ar_vm.txt
