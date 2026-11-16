#!/usr/bin/env bash
# TOOL-M06 Mac observations (ld64 + dyld). Output captured in demo_mac.txt.
#   bash demo_mac.sh > demo_mac.txt 2>&1
# Works only in /tmp/TOOL-M06_demo_mac. Uses src/ + solution/ (foo.h, foo.c).
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
D=/tmp/TOOL-M06_demo_mac
rm -rf "$D"
mkdir -p "$D/src"
cp src/* solution/foo.h solution/foo.c "$D/src/"
cd "$D/src" || exit 2
CW="-std=c11 -Wall -Wextra -Werror -O2"
x() { echo "\$ $*"; eval "$@" 2>&1; }
hdr() { printf '\n===== %s =====\n' "$*"; }

hdr "1. -fPIC on arm64 macOS: always PIC (cc -### shows the cc1 relocation model)"
for f in "" "-fPIC" "-fno-pic"; do
	echo "cc $f -###: $(cc $f -### -c foo_util.c 2>&1 | grep -oE '"-mrelocation-model" "[a-z]+"|"-pic-level" "[0-9]"' | tr '\n' ' ')"
done
cc $CW -I. -c foo_util.c -o a.o && cc $CW -fPIC -I. -c foo_util.c -o b.o && cc $CW -fno-pic -I. -c foo_util.c -o c.o
x 'cmp a.o b.o && cmp a.o c.o && echo "default / -fPIC / -fno-pic objects: byte-identical"'

hdr "2. default install name = the -o path (relative or absolute)"
mkdir -p build
x "cc $CW -dynamiclib -I. foo.c foo_util.c -o build/libfoo.dylib"
x 'otool -D build/libfoo.dylib'
x "cc $CW -I. app.c app_util.c -Lbuild -lfoo -o app_rel"
x 'otool -L app_rel | grep libfoo'
x './app_rel | head -1'
mkdir -p elsewhere && cp app_rel elsewhere/
x 'cd elsewhere && ./app_rel; echo "rc=$?"; cd ..'

hdr "3. the fix: @rpath install name + LC_RPATH relative to the binary"
mkdir -p pkg/bin pkg/lib
x "cc $CW -fvisibility=hidden -dynamiclib -I. foo.c foo_util.c -install_name @rpath/libfoo.dylib -compatibility_version 1.0 -current_version 1.2.0 -o pkg/lib/libfoo.dylib"
x "cc $CW -I. app.c app_util.c -Lpkg/lib -lfoo -Wl,-rpath,@loader_path/../lib -o pkg/bin/app"
x 'otool -l pkg/lib/libfoo.dylib | grep -A5 LC_ID_DYLIB'
x 'otool -l pkg/bin/app | grep -A5 "cmd LC_LOAD_DYLIB" | head -6'
x 'otool -l pkg/bin/app | grep -A3 LC_RPATH'
mv pkg moved_pkg
x 'DYLD_PRINT_SEARCHING=1 ./moved_pkg/bin/app 2>&1 | grep -A4 "find path \"@rpath"'

hdr "4. DYLD_LIBRARY_PATH / DYLD_PRINT_LIBRARIES and SIP on this machine"
x 'csrutil status'
x 'DYLD_LIBRARY_PATH=$PWD/build ./elsewhere/app_rel | head -1'
x 'DYLD_PRINT_LIBRARIES=1 ./moved_pkg/bin/app 2>&1 | head -3'
x 'DYLD_LIBRARY_PATH=/x /bin/sh -c "echo \"inside /bin/sh: [\${DYLD_LIBRARY_PATH-unset}]\""'
x 'DYLD_PRINT_LIBRARIES=1 /usr/bin/true 2>&1 | head -2'

hdr "5. compatibility_version: recorded at link time, enforced at load time?"
mkdir -p cv/lib cv/bin
x "cc $CW -dynamiclib -I. foo.c foo_util.c -install_name @rpath/libfoo.dylib -compatibility_version 2.0 -current_version 2.1 -o cv/lib/libfoo.dylib"
x "cc $CW -I. app.c app_util.c -Lcv/lib -lfoo -Wl,-rpath,@loader_path/../lib -o cv/bin/app"
x 'otool -L cv/bin/app | grep libfoo'
x "cc $CW -dynamiclib -I. foo.c foo_util.c -install_name @rpath/libfoo.dylib -compatibility_version 1.0 -current_version 1.5 -o cv/lib/libfoo.dylib"
x 'otool -L cv/lib/libfoo.dylib | sed -n 2p'
x './cv/bin/app | head -1; echo "rc=$?"'

hdr "6. visibility: what -fvisibility=hidden does to the symbol table"
x "cc $CW -dynamiclib -I. foo.c foo_util.c -o vis_default.dylib"
x 'nm -gU vis_default.dylib'
x 'nm -m moved_pkg/lib/libfoo.dylib | grep -E "round_div|ring_put|foo_open"'
x 'dyld_info -exports moved_pkg/lib/libfoo.dylib'

hdr "7. interposition: two-level namespace (default) vs -flat_namespace"
cat >ip_lib.c <<'EOF'
__attribute__((noinline)) int helper(int x) { return x * 3; }
int foo(int x) { return helper(x) + 1; }
EOF
cat >ip_app.c <<'EOF'
#include <stdio.h>
int foo(int);
int helper(int x) { return x * 100; } /* app's own helper, same name */
int main(void) { printf("foo(2)=%d helper(2)=%d\n", foo(2), helper(2)); return 0; }
EOF
mkdir -p ip
x 'cc -O2 -dynamiclib ip_lib.c -install_name @rpath/libip.dylib -o ip/libip.dylib && cc -O2 ip_app.c -Lip -lip -Wl,-rpath,@loader_path -o ip/app && ./ip/app'
x 'otool -tV ip/libip.dylib | grep -A3 "^_foo:" | tail -2'
x 'cc -O2 -dynamiclib ip_lib.c -install_name @rpath/libip.dylib -Wl,-flat_namespace -o ip/libip.dylib && ./ip/app'
x 'otool -hv ip/libip.dylib | tail -1'

hdr "8. install_name_tool on a prebuilt binary"
mkdir -p fx/bin fx/lib
x "cc $CW -fvisibility=hidden -dynamiclib -I. foo.c foo_util.c -o $D/src/fxbuild.dylib"
x "cc $CW -I. app.c app_util.c $D/src/fxbuild.dylib -Wl,-rpath,@loader_path/../lib -o fx/bin/app"
cp fxbuild.dylib fx/lib/libfoo.dylib && rm fxbuild.dylib
x 'otool -L fx/bin/app | grep -E "fxbuild|libfoo"'
x 'codesign -dv fx/bin/app 2>&1 | grep -E "^CodeDirectory|^Signature"'
x "install_name_tool -change $D/src/fxbuild.dylib @rpath/libfoo.dylib -add_rpath @loader_path/../lib fx/bin/app; echo rc=\$?"
x 'otool -L fx/bin/app | grep -E "fxbuild|libfoo"'
x "install_name_tool -change $D/src/fxbuild.dylib @rpath/libfoo.dylib fx/bin/app; echo rc=\$?"
x 'install_name_tool -id @rpath/libfoo.dylib fx/lib/libfoo.dylib'
x './fx/bin/app | head -1'
x 'codesign -v fx/bin/app && codesign -dv fx/bin/app 2>&1 | grep -E "^CodeDirectory|^Signature"'

hdr "9. what a dylib costs: sizes"
x 'ls -l moved_pkg/lib/libfoo.dylib moved_pkg/bin/app | awk "{print \$5, \$9}"'

hdr "10. @rpath of an indirect dependency: dyld also uses the main executable's LC_RPATH"
mkdir -p tr/lib
echo 'int b(void){return 2;}' > tr/b.c; echo 'int b(void); int a(void){return b()+1;}' > tr/a.c
printf '#include <stdio.h>\nint a(void);\nint main(void){printf("a()=%%d\\n",a());return 0;}\n' > tr/m.c
x 'cd tr && cc -dynamiclib -install_name @rpath/libb.dylib b.c -o lib/libb.dylib && cc -dynamiclib -install_name @rpath/liba.dylib a.c -Llib -lb -o lib/liba.dylib && cc m.c -Llib -la -Wl,-rpath,@loader_path/lib -o m; cd ..'
x 'otool -l tr/lib/liba.dylib | grep -c LC_RPATH'
x 'DYLD_PRINT_SEARCHING=1 ./tr/m 2>&1 | grep -A3 "find path \"@rpath/libb" ; ./tr/m'
