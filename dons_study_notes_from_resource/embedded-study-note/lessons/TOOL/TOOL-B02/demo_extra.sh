#!/usr/bin/env bash
# TOOL-B02 demo: the side experiments quoted in the lesson (real output).
#   bash demo_extra.sh > extra.txt
# Mac = Apple clang 21 (+ --target=thumbv7em-none-eabi); VM = Lima `elinux`:
# gcc 15.2, clang 21.1.8, arm-none-eabi-gcc 14.2.1.
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-B02_work/extra
rm -rf "$W"
mkdir -p "$W"
BASE="-Wall -Wextra -Wshadow -Wconversion -Wimplicit-fallthrough -Wswitch-enum -Wsign-conversion -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes -Wundef"
GONLY="-Wlogical-op -Wduplicated-cond -Wduplicated-branches -Wjump-misses-init"
T="--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -ffreestanding"
d() { grep -E '(warning|error|note):' || true; }
sec() { echo; echo "=== $* ==="; }

echo "##### Mac ($(cc --version | head -1))"
sec "mac: demo_uninit.c clang -Wall -O0"; cc -std=c11 -Wall -O0 -c demo_uninit.c -o "$W/u.o" 2>&1 | d
sec "mac: demo_uninit.c clang -Wall -O2"; cc -std=c11 -Wall -O2 -c demo_uninit.c -o "$W/u.o" 2>&1 | d
sec "mac: demo_uninit.c clang -Wall -Wconditional-uninitialized -O2"; cc -std=c11 -Wall -Wconditional-uninitialized -O2 -c demo_uninit.c -o "$W/u.o" 2>&1 | d
sec "mac: demo_noise.c clang -Wconversion (host arm64, LP64)"; cc -std=c11 -O2 -Wconversion -c demo_noise.c -o "$W/n.o" 2>&1 | d
sec "mac: demo_noise.c clang -Wconversion $T"; cc -std=c11 -O2 -Wconversion $T -c demo_noise.c -o "$W/n.o" 2>&1 | d
sec "mac: demo_noise.c clang -Wdouble-promotion"; cc -std=c11 -O2 -Wdouble-promotion -c demo_noise.c -o "$W/n.o" 2>&1 | d
sec "mac: demo_gcc_only.c clang -Wall -Wextra"; cc -std=c11 -O2 -Wall -Wextra -c demo_gcc_only.c -o "$W/g.o" 2>&1 | d
sec "mac: demo_gcc_only.c clang -Wall -Wextra + GCC-only flags"; cc -std=c11 -O2 -Wall -Wextra $GONLY -c demo_gcc_only.c -o "$W/g.o" 2>&1 | d; echo "rc=${PIPESTATUS[0]}"
sec "mac: same + -Werror"; cc -std=c11 -O2 -Wall -Wextra $GONLY -Werror -c demo_gcc_only.c -o "$W/g.o" 2>&1 | d; echo "rc=${PIPESTATUS[0]}"
sec "mac: same + -Werror -Wno-unknown-warning-option"; cc -std=c11 -O2 -Wall -Wextra $GONLY -Wno-unknown-warning-option -Werror -c demo_gcc_only.c -o "$W/g.o" 2>&1 | d; echo "rc=${PIPESTATUS[0]}"
sec "mac: demo_format2.c clang -Wall"; cc -std=c11 -Wall -c demo_format2.c -o "$W/f.o" 2>&1 | d
sec "mac: demo_format2.c clang -Wall -Wformat=2"; cc -std=c11 -Wall -Wformat=2 -c demo_format2.c -o "$W/f.o" 2>&1 | d
sec "mac: demo_u32fmt.c clang -Wall (host)"; cc -std=c11 -Wall -c demo_u32fmt.c -o "$W/x.o" 2>&1 | d
sec "mac: demo_fallthrough.c clang -Wimplicit-fallthrough -Wpedantic"; cc -std=c11 -Wimplicit-fallthrough -Wpedantic -c demo_fallthrough.c -o "$W/t.o" 2>&1 | d
sec "mac: bugs12.c clang -Werror=return-type (only that one becomes an error)"; cc -std=c11 -Werror=return-type -c bugs12.c -o "$W/b.o" 2>&1 | grep -E '(warning|error):'; echo "rc=${PIPESTATUS[0]}"
sec "mac: good.c baseline -I third_party -Werror"; cc -std=c11 -O2 $BASE -I third_party -Werror good.c -o "$W/good" 2>&1 | grep -E '(warning|error):'; echo "rc=${PIPESTATUS[0]}"
sec "mac: good.c baseline -isystem third_party -Werror + run"; cc -std=c11 -O2 $BASE -isystem third_party -Werror good.c -o "$W/good" 2>&1 | d; "$W/good"; echo "rc=$?"
sec "mac: unknown flag -Wcast-align=strict (gcc spelling)"; cc -std=c11 -Wcast-align=strict -c good.c -isystem third_party -o "$W/x.o" 2>&1 | d

command -v limactl >/dev/null 2>&1 || exit 0
tar cf "$W/src.tar" demo_uninit.c demo_noise.c demo_gcc_only.c demo_u32fmt.c demo_format2.c demo_fallthrough.c bugs12.c good.c fw_bugs.c third_party
limactl shell elinux -- bash -lc "rm -rf /tmp/TOOL-B02_work/extra && mkdir -p /tmp/TOOL-B02_work/extra && cd /tmp/TOOL-B02_work/extra && tar xf -" <"$W/src.tar" || exit 1
limactl shell elinux -- bash -l -s <<VM
cd /tmp/TOOL-B02_work/extra || exit 1
d() { grep -E '(warning|error|note):' || true; }
sec() { echo; echo "=== \$* ==="; }
echo; echo "##### VM (\$(gcc --version | head -1); \$(arm-none-eabi-gcc --version | head -1); \$(clang --version | head -1))"
for o in -O0 -O1 -O2 -Os; do sec "vm: demo_uninit.c gcc -Wall \$o"; gcc -std=c11 -Wall \$o -c demo_uninit.c -o u.o 2>&1 | d; done
sec "vm: demo_uninit.c clang -Wall -O2"; clang -std=c11 -Wall -O2 -c demo_uninit.c -o u.o 2>&1 | d
sec "vm: demo_uninit.c -DONLY_MAYBE gcc -Wall -Werror -O0 (debug build)"; gcc -std=c11 -Wall -Werror -O0 -DONLY_MAYBE -c demo_uninit.c -o u.o 2>&1 | d; echo "rc=\${PIPESTATUS[0]}"
sec "vm: demo_uninit.c -DONLY_MAYBE gcc -Wall -Werror -O2 (release build)"; gcc -std=c11 -Wall -Werror -O2 -DONLY_MAYBE -c demo_uninit.c -o u.o 2>&1 | d; echo "rc=\${PIPESTATUS[0]}"
sec "vm: demo_noise.c gcc -Wconversion (aarch64 LP64)"; gcc -std=c11 -O2 -Wconversion -c demo_noise.c -o n.o 2>&1 | d
sec "vm: demo_noise.c arm-none-eabi-gcc -Wconversion (ILP32)"; arm-none-eabi-gcc -std=c11 -O2 -mcpu=cortex-m4 -mthumb -Wconversion -c demo_noise.c -o n.o 2>&1 | d
sec "vm: demo_noise.c arm-none-eabi-gcc -Wdouble-promotion"; arm-none-eabi-gcc -std=c11 -O2 -mcpu=cortex-m4 -mthumb -Wdouble-promotion -c demo_noise.c -o n.o 2>&1 | d
sec "vm: demo_gcc_only.c gcc -Wall -Wextra"; gcc -std=c11 -O2 -Wall -Wextra -c demo_gcc_only.c -o g.o 2>&1 | d
sec "vm: demo_gcc_only.c gcc -Wall -Wextra $GONLY"; gcc -std=c11 -O2 -Wall -Wextra $GONLY -c demo_gcc_only.c -o g.o 2>&1 | d
sec "vm: gcc unknown -Wno-unknown-warning-option, no other warning"; gcc -std=c11 -Wall -Wno-unknown-warning-option -c demo_u32fmt.c -o x.o 2>&1 | d; echo "rc=\${PIPESTATUS[0]}"
sec "vm: gcc unknown -Wno-unknown-warning-option + another warning"; gcc -std=c11 -Wall -Wno-unknown-warning-option -Wduplicated-cond -c demo_gcc_only.c -o x.o 2>&1 | d
sec "vm: gcc unknown positive flag -Wfoo-unknown"; gcc -std=c11 -Wfoo-unknown -c demo_u32fmt.c -o x.o 2>&1; echo "rc=\$?"
sec "vm: demo_format2.c gcc -Wall -Wformat=2"; gcc -std=c11 -Wall -Wformat=2 -c demo_format2.c -o f.o 2>&1 | d
sec "vm: demo_format2.c gcc -Wall -Wformat=2 -Wsuggest-attribute=format"; gcc -std=c11 -Wall -Wformat=2 -Wsuggest-attribute=format -c demo_format2.c -o f.o 2>&1 | d
sec "vm: demo_u32fmt.c arm-none-eabi-gcc -Wall"; arm-none-eabi-gcc -std=c11 -mcpu=cortex-m4 -mthumb -Wall -c demo_u32fmt.c -o x.o 2>&1 | d
sec "vm: demo_fallthrough.c gcc -Wimplicit-fallthrough -Wpedantic"; gcc -std=c11 -Wimplicit-fallthrough -Wpedantic -c demo_fallthrough.c -o t.o 2>&1 | d
sec "vm: demo_fallthrough.c gcc -Wimplicit-fallthrough=5"; gcc -std=c11 -Wimplicit-fallthrough=5 -c demo_fallthrough.c -o t.o 2>&1 | d
sec "vm: good.c gcc baseline -I third_party"; gcc -std=c11 -O2 $BASE -I third_party good.c -o good 2>&1 | d
sec "vm: good.c gcc baseline -isystem third_party -Werror + run"; gcc -std=c11 -O2 $BASE -isystem third_party -Werror good.c -o good 2>&1 | d; ./good; echo "rc=\$?"
sec "vm: good.c arm-none-eabi-gcc baseline -isystem -Werror -c"; arm-none-eabi-gcc -std=c11 -O2 -mcpu=cortex-m4 -mthumb $BASE -isystem third_party -Werror -c good.c -o good_m4.o 2>&1 | d; echo "rc=\${PIPESTATUS[0]}"
sec "vm: fw_bugs.c gcc (aarch64) baseline + fw flags"; gcc -std=c11 -O2 $BASE -Wdouble-promotion -Wvla -Wcast-align -Wcast-qual -Wpointer-arith -Wwrite-strings -c fw_bugs.c -o fw.o 2>&1 | d
sec "vm: fw_bugs.c gcc (aarch64) -Wcast-align=strict"; gcc -std=c11 -O2 -Wcast-align=strict -c fw_bugs.c -o fw.o 2>&1 | d
sec "vm: fw_bugs.c arm-none-eabi-gcc baseline + fw flags"; arm-none-eabi-gcc -std=c11 -O2 -mcpu=cortex-m4 -mthumb $BASE -Wdouble-promotion -Wvla -Wcast-align -Wcast-qual -Wpointer-arith -Wwrite-strings -c fw_bugs.c -o fw.o 2>&1 | d
VM
