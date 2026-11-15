#!/usr/bin/env bash
# TOOL-B07 demo: predefined macros (-dM -E - </dev/null) — Mac arm64 vs Cortex-M4 targets.
#   bash demo_predef.sh > predef_table.txt
# Mac: Apple clang native + --target=thumbv7em-none-eabi (hard / softfp / soft float ABI)
# VM : arm-none-eabi-gcc (hard / soft) + host gcc (aarch64 Linux), via limactl.
# Full dumps go to /tmp/TOOL-B07_work/predef/*.txt; stdout gets counts + a key-macro table.
set -u
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-B07_work/predef
rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 1
M4="--target=thumbv7em-none-eabi -mcpu=cortex-m4"
cc -dM -E - </dev/null | sort >mac_arm64.txt
clang $M4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -dM -E -x c /dev/null | sort >clang_m4_hard.txt
clang $M4 -mfloat-abi=softfp -mfpu=fpv4-sp-d16 -dM -E -x c /dev/null | sort >clang_m4_softfp.txt
clang $M4 -mfloat-abi=soft -dM -E -x c /dev/null | sort >clang_m4_soft.txt
limactl shell elinux -- bash -lc '
M4="-mcpu=cortex-m4 -mthumb"
arm-none-eabi-gcc $M4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -dM -E - </dev/null | sort
echo "@@SPLIT@@"
arm-none-eabi-gcc $M4 -mfloat-abi=soft -dM -E - </dev/null | sort
echo "@@SPLIT@@"
gcc -dM -E - </dev/null | sort' >vm_all.txt
awk -v RS='@@SPLIT@@\n' '{ f = (NR==1 ? "gcc_m4_hard.txt" : NR==2 ? "gcc_m4_soft.txt" : "gcc_host_aarch64.txt"); printf "%s", $0 > f }' vm_all.txt
FILES="mac_arm64 clang_m4_hard clang_m4_softfp clang_m4_soft gcc_m4_hard gcc_m4_soft gcc_host_aarch64"

echo "## number of predefined macros"
for f in $FILES; do printf '%-18s %4d\n' "$f" "$(grep -c '^#define' $f.txt)"; done
echo
echo "## key macros (blank = not defined)"
printf '%-22s' macro; for f in $FILES; do printf '|%-14s' "${f:0:14}"; done; echo
for m in __APPLE__ __MACH__ __ELF__ __linux__ __aarch64__ __arm__ __thumb__ __thumb2__ \
	__ARM_ARCH __ARM_ARCH_7EM__ __ARM_ARCH_PROFILE __ARM_FP __ARM_PCS __ARM_PCS_VFP __SOFTFP__ \
	__VFP_FP__ __ARM_FEATURE_DSP __ARM_NEON __ARM_EABI__ __SIZEOF_POINTER__ __SIZEOF_LONG__ \
	__CHAR_UNSIGNED__ __clang__ __GNUC__ __STDC_VERSION__ __OPTIMIZE__; do
	printf '%-22s' "$m"
	for f in $FILES; do printf '|%-14s' "$(grep -m1 "^#define $m " $f.txt | cut -d' ' -f3- | cut -c1-14)"; done
	echo
done
echo
echo "## optimization level -> predefined macros (Mac clang)"
for o in -O0 -O1 -O2 -Os -Oz -Og; do
	printf '%-4s ' "$o"
	cc $o -dM -E - </dev/null | grep -E '__OPTIMIZE__|__OPTIMIZE_SIZE__|__NO_INLINE__' | sort | tr '\n' ' '
	echo
done
echo
echo "## -std changes __STDC_VERSION__ / __STRICT_ANSI__ (Mac clang)"
for s in -std=c11 -std=gnu11 -std=c17 -std=c23; do
	printf '%-10s ' "$s"
	cc $s -dM -E - </dev/null | grep -E '__STDC_VERSION__|__STRICT_ANSI__' | sort | tr '\n' ' '
	echo
done
