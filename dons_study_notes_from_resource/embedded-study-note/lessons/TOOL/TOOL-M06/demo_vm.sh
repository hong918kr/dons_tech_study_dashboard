#!/usr/bin/env bash
# TOOL-M06 ELF observations in the Lima VM `elinux` (aarch64 Ubuntu, gcc 15.2, GNU ld 2.46, glibc 2.43).
#   bash demo_vm.sh > demo_vm.txt 2>&1
# The lesson dir is mounted read-only in the VM: sources are copied to VM /tmp/TOOL-M06_demo_vm first.
set -u
cd "$(dirname "$0")" || exit 2
LESSON=$(pwd)
{
	declare -p LESSON
	cat <<'VM'
set -u
D=/tmp/TOOL-M06_demo_vm
rm -rf "$D"; mkdir -p "$D/src"; cp "$LESSON"/src/* "$LESSON"/solution/foo.[ch] "$D/src/"; cd "$D/src" || exit 1
CW="-std=c11 -Wall -Wextra -Werror -O2"
x() { echo "\$ $*"; eval "$@" 2>&1; }
hdr() { printf '\n===== %s =====\n' "$*"; }
echo "$(gcc --version | head -1) | $(ld --version | head -1) | $(ldd --version | head -1) | $(uname -m)"

hdr "1. -fPIC on ELF: aarch64 vs x86_64"
printf 'extern int ext;\nint *p = &ext;\nint get(void) { return ext; }\n' > ext.c
printf 'int counter;\nint bump(void) { return ++counter; }\n' > cnt.c
x 'gcc -v 2>&1 | grep -o -- "--enable-default-pie"'
x 'gcc -O2 -c ext.c -o ext_pie.o && readelf -rW ext_pie.o | grep -E "R_AARCH64" | grep -v PREL32 && gcc -shared ext_pie.o -o libext_pie.so && echo "default (-fPIE) object -> .so: linked"'
x 'gcc -O2 -fno-pic -c ext.c -o ext_nopic.o && readelf -rW ext_nopic.o | grep -E "R_AARCH64" | grep -v PREL32'
x 'gcc -shared ext_nopic.o -o libext_nopic.so; echo rc=$?'
x 'gcc -O2 -fno-pic -c cnt.c -o cnt_nopic.o && gcc -shared cnt_nopic.o -o libcnt_nopic.so && echo "aarch64 -fno-pic, own global via ADRP (PC-relative): linked"'
x 'x86_64-linux-gnu-gcc -O2 -fno-pic -c cnt.c -o cnt_x86.o && x86_64-linux-gnu-gcc -shared cnt_x86.o -o libcnt_x86.so; echo rc=$?'
x 'x86_64-linux-gnu-gcc -O2 -fPIC -c cnt.c -o cnt_x86p.o && x86_64-linux-gnu-gcc -shared cnt_x86p.o -o libcnt_x86p.so && echo "x86_64 -fPIC: linked"'

hdr "2. real name / SONAME / linker name and who uses which"
mkdir -p build
x "gcc $CW -fPIC -fvisibility=hidden -I. -c foo.c foo_util.c"
x 'gcc -shared -Wl,-soname,libfoo.so.1 foo.o foo_util.o -o build/libfoo.so.1.2.0'
x 'ln -s libfoo.so.1.2.0 build/libfoo.so && /usr/sbin/ldconfig -n build && ls -l build | awk "NR>1 {print \$9, \$10, \$11}"'
x "gcc $CW -I. app.c app_util.c -Lbuild -lfoo -Wl,-rpath,'\$ORIGIN' -o build/app"
x 'readelf -dW build/libfoo.so.1.2.0 | grep -E "SONAME|NEEDED"'
x 'readelf -dW build/app | grep -E "NEEDED|RUNPATH|RPATH"'
x 'ldd build/app | sed "s/(0x[0-9a-f]*)//"'
x 'cd build && LD_DEBUG=libs ./app 2>&1 | grep -E "find library=libfoo|search path|trying file|calling init: .*libfoo" | sed "s/^ *[0-9]*://"; cd ..'

hdr "3. no SONAME: NEEDED records the file name the linker saw"
mkdir -p nosoname
x 'gcc -shared foo.o foo_util.o -o nosoname/libfoo.so.1.2.0 && ln -s libfoo.so.1.2.0 nosoname/libfoo.so'
x "gcc $CW -I. app.c app_util.c -Lnosoname -lfoo -o nosoname/app_l && gcc $CW -I. app.c app_util.c nosoname/libfoo.so.1.2.0 -o nosoname/app_path"
x 'readelf -dW nosoname/app_l | grep "NEEDED.*foo"; readelf -dW nosoname/app_path | grep "NEEDED.*foo"'

hdr "4. RPATH vs RUNPATH vs LD_LIBRARY_PATH (search order)"
mkdir -p order/a order/b
x 'gcc -fPIC -shared -Wl,-soname,libwho.so -x c - -o order/a/libwho.so <<< "const char *who(void){return \"a (rpath dir)\";}"'
x 'gcc -fPIC -shared -Wl,-soname,libwho.so -x c - -o order/b/libwho.so <<< "const char *who(void){return \"b (LD_LIBRARY_PATH dir)\";}"'
printf '#include <stdio.h>\nconst char *who(void);\nint main(void){puts(who());return 0;}\n' > who.c
x 'gcc who.c -Lorder/a -lwho -Wl,-rpath,$PWD/order/a -Wl,--enable-new-dtags -o order/app_runpath'
x 'gcc who.c -Lorder/a -lwho -Wl,-rpath,$PWD/order/a -Wl,--disable-new-dtags -o order/app_rpath'
x 'readelf -dW order/app_runpath | grep -E "R(UN)?PATH" | sed "s|$PWD|\$PWD|"; readelf -dW order/app_rpath | grep -E "R(UN)?PATH" | sed "s|$PWD|\$PWD|"'
x 'LD_LIBRARY_PATH=$PWD/order/b ./order/app_runpath'
x 'LD_LIBRARY_PATH=$PWD/order/b ./order/app_rpath'

hdr "4b. RUNPATH is not used for a library's own dependencies (RPATH is)"
mkdir -p tr/lib
echo 'int b(void){return 2;}' > tr/b.c; echo 'int b(void); int a(void){return b()+1;}' > tr/a.c
printf '#include <stdio.h>\nint a(void);\nint main(void){printf("a()=%%d\\n",a());return 0;}\n' > tr/m.c
x 'cd tr && gcc -fPIC -shared -Wl,-soname,libb.so b.c -o lib/libb.so && gcc -fPIC -shared -Wl,-soname,liba.so a.c -Llib -lb -o lib/liba.so; cd ..'
x "cd tr && gcc m.c -Llib -la -Wl,-rpath-link,lib -Wl,-rpath,'\$ORIGIN/lib' -Wl,--enable-new-dtags -o m_runpath && ./m_runpath; echo rc=\$?; cd .."
x "cd tr && gcc m.c -Llib -la -Wl,-rpath-link,lib -Wl,-rpath,'\$ORIGIN/lib' -Wl,--disable-new-dtags -o m_rpath && ./m_rpath; echo rc=\$?; cd .."

hdr "5. interposition: an app symbol with the same name as a library internal"
printf '#ifdef HIDE\n__attribute__((visibility("hidden")))\n#endif\n__attribute__((noinline)) int helper(int x) { return x * 3; }\nint foo(int x) { return helper(x) + 1; }\n' > ip_lib.c
printf '#include <stdio.h>\nint foo(int);\nint helper(int x) { return x * 100; }\nint main(void) { printf("foo(2)=%%d helper(2)=%%d\\n", foo(2), helper(2)); return 0; }\n' > ip_app.c
mkdir -p ip
for v in "default:" "hidden:-DHIDE" "nosemint:-fno-semantic-interposition" "bsymbolic:-Wl,-Bsymbolic"; do
	n=${v%%:*}; f=${v#*:}
	gcc -O2 -fPIC -shared $f ip_lib.c -o ip/libip_$n.so
done
cp ip/libip_default.so ip/libip.so && gcc -O2 ip_app.c -Lip -l:libip.so -Wl,-rpath,'$ORIGIN' -o ip/app
for n in default hidden nosemint bsymbolic; do
	cp ip/libip_$n.so ip/libip.so
	echo "lib built $n: $(./ip/app 2>&1) | foo calls: $(objdump -d --no-show-raw-insn ip/libip.so | grep -A3 '<foo>:' | grep -oE 'bl\s+[0-9a-f]+ <[^>]+>')"
done
x 'nm -D ip/app | grep -w helper'

hdr "6. visibility: exported dynamic symbols"
x 'gcc -fPIC -shared foo.c foo_util.c -I. -o libfoo_all.so 2>/dev/null; nm -D --defined-only libfoo_all.so | grep -E " T "'
x 'nm -D --defined-only build/libfoo.so.1.2.0 | grep -E " T "'
x 'readelf -sW foo.o | grep -E "round_div|foo_open"'
x 'size libfoo_all.so build/libfoo.so.1.2.0'

hdr "7. version script (symbol versioning)"
x "gcc -shared -Wl,-soname,libfoo.so.1 -Wl,--version-script=$LESSON/practice/foo.map foo.o foo_util.o -o vs_libfoo.so"
x 'readelf --dyn-syms -W vs_libfoo.so | grep -E "foo_open|FOO_1"'
mkdir -p vs && cp vs_libfoo.so vs/libfoo.so.1 && ln -s libfoo.so.1 vs/libfoo.so
x "gcc $CW -I. app.c app_util.c -Lvs -lfoo -o vs/app && readelf -V -W vs/app | grep -B1 FOO_1.0"
x 'readelf --dyn-syms -W vs/app | grep foo_open'
mkdir -p nov ov && cp build/libfoo.so.1.2.0 nov/libfoo.so.1
printf 'FOO_0.9 { global: foo_*; local: *; };\n' > old.map
x 'gcc -shared -Wl,-soname,libfoo.so.1 -Wl,--version-script=old.map foo.o foo_util.o -o ov/libfoo.so.1'
x 'LD_LIBRARY_PATH=nov ./vs/app | head -1; echo rc=$?'
x 'LD_LIBRARY_PATH=ov ./vs/app; echo rc=$?'

hdr "8. patchelf: repair a prebuilt ELF without relinking (counterpart of install_name_tool)"
mkdir -p pe/bin pe/lib
x "gcc $CW -I. app.c app_util.c -Lnosoname -lfoo -Wl,-rpath,$D/src/nosoname -o pe/bin/app"
cp nosoname/libfoo.so.1.2.0 pe/lib/
x 'readelf -dW pe/bin/app | grep -E "NEEDED.*foo|RUNPATH"'
x './pe/bin/app 2>&1 | head -1'
x 'patchelf --set-soname libfoo.so.1 pe/lib/libfoo.so.1.2.0 && ln -s libfoo.so.1.2.0 pe/lib/libfoo.so.1'
x "patchelf --replace-needed libfoo.so libfoo.so.1 --set-rpath '\$ORIGIN/../lib' pe/bin/app"
x 'readelf -dW pe/bin/app | grep -E "NEEDED.*foo|RUNPATH"'
x 'mv pe pe_moved && ./pe_moved/bin/app | head -1'

hdr "9. firmware tie: static link of the same objects, and what dynamic linking saves"
x "gcc $CW -I. app.c app_util.c foo.c foo_util.c -static -o app_static; echo rc=\$?"
printf '#include <stdio.h>\nint main(void){puts("hi");return 0;}\n' > hi.c
x 'gcc -O2 hi.c -o hi_dyn && gcc -O2 -static hi.c -o hi_static && ls -l hi_dyn hi_static | awk "{print \$5, \$9}"'
x 'ls -lL /lib/aarch64-linux-gnu/libc.so.6 | awk "{print \$5, \$9}"'
x 'readelf -lW build/libfoo.so.1.2.0 | grep LOAD'
x 'gcc -shared -Wl,-soname,libfoo.so.1 -Wl,-z,max-page-size=4096 foo.o foo_util.o -o l4.so && gcc -shared -Wl,-soname,libfoo.so.1 -Wl,-z,max-page-size=4096 -s foo.o foo_util.o -o l4s.so'
x 'ls -l build/libfoo.so.1.2.0 l4.so l4s.so | awk "{print \$5, \$9}"; getconf PAGESIZE'
VM
} | limactl shell elinux -- bash -s
