#!/usr/bin/env bash
# ENV: linux
# ELX-B02 check: libsensor.so.1 + app 를 3개 triplet 으로 크로스 빌드 → elfinfo(<variant>.c) 출력을
# readelf 와 비교 → qemu-user(-L sysroot)로 실행 → elfinfo 의 sysroot 판정이 실제 실행 결과와 같은지.
#   check.sh <starter|solution|practice>     (Lima VM elinux 안에서, 아무 cwd 에서나)
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
case "${1:-}" in
starter|solution|practice) VARIANT=$1 ;;
*) echo "usage: check.sh <starter|solution|practice>"; exit 2 ;;
esac
W=/tmp/ELX-B02_work/$VARIANT.$$
mkdir -p "$W"
cp "$SRC"/{starter.c,solution.c,practice.c,sensor.c,sensor.h,app.c} "$W/"
cd "$W" || exit 1

P=0; F=0
ok()  { echo "PASS: $*"; P=$((P + 1)); }
bad() { echo "FAIL: $*"; F=$((F + 1)); }
finish() {
	echo "$P/$((P + F)) passed"
	if [ "$F" -eq 0 ] && [ "$P" -gt 0 ]; then
		echo "RESULT: PASS ($VARIANT)"; cd /; rm -rf "$W"; exit 0
	fi
	if [ "${KEEP:-0}" = 1 ]; then echo "RESULT: FAIL ($VARIANT) work dir kept: $W"
	else cd /; rm -rf "$W"; echo "RESULT: FAIL ($VARIANT) (KEEP=1 로 다시 돌리면 작업 디렉터리를 남긴다)"; fi
	exit 1
}

# ---- 1. elfinfo 빌드 (엄격) + ASan/UBSan 빌드 -----------------------------------
STRICT="gcc -std=c11 -Wall -Wextra -Werror -O2"
if ! $STRICT "$VARIANT.c" -o elfinfo 2>build.log; then
	cat build.log; bad "build $VARIANT.c"; finish
fi
gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	"$VARIANT.c" -o elfinfo_asan 2>>build.log || { cat build.log; bad "asan build"; finish; }
ok "build $VARIANT.c (-Wall -Wextra -Werror) + asan build"

# ---- 2. selftest (CHECK 하네스, ASan 빌드로) -------------------------------------
./elfinfo_asan --selftest >selftest.log 2>&1; rc=$?
grep -E '^  FAIL' selftest.log
st=$(tail -1 selftest.log)
[ $rc -eq 0 ] && ok "selftest $st" || bad "selftest $st (rc=$rc)"

# ---- 3. 크로스 빌드: 3 triplet × (lib, app, app_norpath, app_rpath, app_nopie, app_static) ----
TRIPLETS="aarch64-linux-gnu arm-linux-gnueabihf x86_64-linux-gnu"
declare -A QEMU=([aarch64-linux-gnu]=qemu-aarch64 [arm-linux-gnueabihf]=qemu-arm [x86_64-linux-gnu]=qemu-x86_64)
declare -A SR=([aarch64-linux-gnu]=/ [arm-linux-gnueabihf]=/usr/arm-linux-gnueabihf [x86_64-linux-gnu]=/usr/x86_64-linux-gnu)
declare -A LIBC=([aarch64-linux-gnu]=/lib/aarch64-linux-gnu/libc.so.6
		 [arm-linux-gnueabihf]=/usr/arm-linux-gnueabihf/lib/libc.so.6
		 [x86_64-linux-gnu]=/usr/x86_64-linux-gnu/lib/libc.so.6)
declare -A LDSO=([aarch64-linux-gnu]=/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1
		 [arm-linux-gnueabihf]=/usr/arm-linux-gnueabihf/lib/ld-linux-armhf.so.3
		 [x86_64-linux-gnu]=/usr/x86_64-linux-gnu/lib/ld-linux-x86-64.so.2)
declare -A LINE=([aarch64-linux-gnu]="arch=aarch64 long=8 ptr=8 lib_arch=aarch64"
		 [arm-linux-gnueabihf]="arch=arm long=4 ptr=4 lib_arch=arm"
		 [x86_64-linux-gnu]="arch=x86_64 long=8 ptr=8 lib_arch=x86_64")
for T in $TRIPLETS; do
	O=$W/out/$T; mkdir -p "$O/lib"
	CC="$T-gcc -std=c11 -Wall -Wextra -Werror -O2"
	{ $CC -fPIC -shared -Wl,-soname,libsensor.so.1 sensor.c -o "$O/lib/libsensor.so.1" &&
	  ln -sf libsensor.so.1 "$O/lib/libsensor.so" &&
	  $CC app.c -L"$O/lib" -lsensor -Wl,-rpath,'$ORIGIN/lib' -o "$O/app" &&
	  $CC app.c -L"$O/lib" -lsensor -o "$O/app_norpath" &&
	  $CC app.c -L"$O/lib" -lsensor -Wl,--disable-new-dtags,-rpath,'$ORIGIN/lib' -o "$O/app_rpath" &&
	  $CC -no-pie app.c -L"$O/lib" -lsensor -Wl,-rpath,'$ORIGIN/lib' -o "$O/app_nopie" &&
	  $CC -static app.c sensor.c -o "$O/app_static"; } 2>>build.log \
		&& ok "cross build $T" || { bad "cross build $T"; cat build.log; }
	echo "INFO: $(file -b "$O/app" | cut -d, -f1-5)"
done

# ---- 4. elfinfo vs readelf ------------------------------------------------------
expect() {   # readelf (+od) 로 elfinfo 와 같은 형식의 기대값을 만든다
	local f=$1 h
	h=$(readelf -h "$f")
	echo "class: $(sed -n 's/^ *Class: *//p' <<<"$h")"
	grep -q 'little endian' <<<"$h" && echo "data: LE" || echo "data: BE"
	echo "type: $(sed -n 's/^ *Type: *\([A-Z]*\).*/\1/p' <<<"$h")"
	echo "machine: $(sed -n 's/^ *Machine: *//p' <<<"$h") ($(od -An -tu2 -j18 -N2 "$f" | tr -d ' '))"
	readelf -lW "$f" 2>/dev/null | sed -n 's/.*Requesting program interpreter: \(.*\)\]$/interp: \1/p'
	readelf -dW "$f" | sed -n -e 's/.*(NEEDED).*\[\(.*\)\]$/needed: \1/p'
	readelf -dW "$f" | sed -n -e 's/.*(RUNPATH).*\[\(.*\)\]$/runpath: \1/p'
	readelf -dW "$f" | sed -n -e 's/.*(RPATH).*\[\(.*\)\]$/rpath: \1/p'
	readelf -dW "$f" | sed -n -e 's/.*(SONAME).*\[\(.*\)\]$/soname: \1/p'
	if [ "$VARIANT" = practice ]; then expect_more "$f" "$h"; fi
}
expect_more() {   # 추가 연습: flags / kind / buildid
	local f=$1 h=$2 fl ty interp pie kind
	fl=$(sed -n 's/^ *Flags: *\(0x[0-9a-f]*\).*/\1/p' <<<"$h")
	if grep -q 'Machine: *ARM$' <<<"$h"; then
		local eabi fa=none
		eabi=$(sed -n 's/.*Version\([0-9]\) EABI.*/\1/p' <<<"$h")
		grep -q 'hard-float ABI' <<<"$h" && fa=hard
		grep -q 'soft-float ABI' <<<"$h" && fa=soft
		echo "flags: $fl eabi=${eabi:-0} float=$fa"
	else
		echo "flags: $fl"
	fi
	ty=$(sed -n 's/^ *Type: *\([A-Z]*\).*/\1/p' <<<"$h")
	interp=$(readelf -lW "$f" 2>/dev/null | grep -c 'Requesting program interpreter')
	pie=$(readelf -dW "$f" | grep -c '(FLAGS_1).*PIE')
	case "$ty" in
	REL)  kind=relocatable ;;
	EXEC) [ "$interp" -gt 0 ] && kind=exec || kind=static ;;
	DYN)  if [ "$pie" -gt 0 ]; then [ "$interp" -gt 0 ] && kind=pie || kind=static-pie
	      else kind=shared; fi ;;
	*)    kind=other ;;
	esac
	echo "kind: $kind"
	readelf -n "$f" | sed -n 's/.*Build ID: *\([0-9a-f]*\)$/buildid: \1/p'
}
MORE=; [ "$VARIANT" = practice ] && MORE=--more
compare() {   # $1 = 파일, $2 = 표시 이름
	expect "$1" >exp.txt 2>/dev/null
	./elfinfo_asan $MORE "$1" >got.txt 2>&1
	if diff -u exp.txt got.txt >diff.txt; then ok "elfinfo == readelf: $2"
	else bad "elfinfo != readelf: $2"; head -20 diff.txt; fi
}
for T in $TRIPLETS; do
	O=$W/out/$T
	for b in app app_norpath app_rpath app_nopie app_static lib/libsensor.so.1; do compare "$O/$b" "$T/$b"; done
	compare "${LIBC[$T]}" "${LIBC[$T]}"
	compare "${LDSO[$T]}" "${LDSO[$T]}"
	./elfinfo_asan --fuzz "$O/app" >fuzz.txt 2>&1 && ok "$(cat fuzz.txt)" || { bad "fuzz $T"; tail -5 fuzz.txt; }
done
compare /usr/riscv64-linux-gnu/lib/libc.so.6 "riscv64 libc.so.6 (4번째 아키텍처, 빌드 없이)"
if [ "$VARIANT" = practice ]; then
	for T in aarch64-linux-gnu x86_64-linux-gnu; do   # armhf 는 rcrt1.o 가 없어 static-pie 불가
		O=$W/out/$T
		$T-gcc -O2 -static-pie app.c sensor.c -o "$O/app_spie" 2>>build.log && compare "$O/app_spie" "$T/app_spie"
	done
	arm-linux-gnueabihf-gcc -c -O2 -mfloat-abi=soft sensor.c -o soft.o && compare soft.o "armhf soft.o"
fi

# ---- 5. qemu-user 실행: -L sysroot, rpath, LD_LIBRARY_PATH, 정적 ------------------------
run_ok() {   # $1 이름, 나머지 = 명령. 출력 한 줄이 기대값과 정확히 같아야 PASS
	local name=$1 want=$2; shift 2
	local out; out=$(timeout 20 "$@" 2>&1); local rc=$?
	if [ $rc -eq 0 ] && [ "$out" = "app: $want abi=1 t0=25000 t3=25375 OK" ]; then ok "run $name"
	else bad "run $name rc=$rc out=$out"; fi
}
for T in $TRIPLETS; do
	O=$W/out/$T; Q=${QEMU[$T]}; S=${SR[$T]}; WANT=${LINE[$T]}
	run_ok "$T/app ($Q -L $S)" "$WANT" "$Q" -L "$S" "$O/app"
	run_ok "$T/app_rpath" "$WANT" "$Q" -L "$S" "$O/app_rpath"
	run_ok "$T/app_nopie" "$WANT" "$Q" -L "$S" "$O/app_nopie"
	run_ok "$T/app_static (no -L)" "$WANT" "$Q" "$O/app_static"
	run_ok "$T/app_norpath -E LD_LIBRARY_PATH" "$WANT" "$Q" -L "$S" -E LD_LIBRARY_PATH="$O/lib" "$O/app_norpath"
done
run_ok "aarch64 app native (no qemu)" "${LINE[aarch64-linux-gnu]}" "$W/out/aarch64-linux-gnu/app"

# ---- 6. Part C: elfinfo 의 sysroot 판정 == 실제 실행 결과 ------------------------------
declare -A INTERP=([aarch64-linux-gnu]=/lib/ld-linux-aarch64.so.1 [arm-linux-gnueabihf]=/lib/ld-linux-armhf.so.3
		   [x86_64-linux-gnu]=/lib64/ld-linux-x86-64.so.2)
mkroot() {   # 손으로 만든 작은 타깃 rootfs: 로더 + libc (+ /opt/sensor/lib/libsensor.so.1)
	local T=$1 R=$2
	mkdir -p "$R/lib" "$R/opt/sensor/lib" "$R$(dirname "${INTERP[$T]}")"
	cp -L "${LDSO[$T]}" "$R${INTERP[$T]}"
	cp -L "${LIBC[$T]}" "$R/lib/libc.so.6"
	cp "$W/out/$T/lib/libsensor.so.1" "$R/opt/sensor/lib/"
}
mkdir -p mix/lib stage/lib
cp out/arm-linux-gnueabihf/app mix/app
cp out/aarch64-linux-gnu/lib/libsensor.so.1 mix/lib/          # 64-bit 라이브러리를 32-bit 앱 옆에
cp "${LDSO[arm-linux-gnueabihf]}" stage/lib/                   # 로더만 있고 libc 없는 rootfs
predict() {   # $1 이름, $2 기대 verdict, $3 기대 rc, $4 기대 문구, $5.. = elfinfo 인자 ; RUN=(실행 명령)
	local name=$1 wv=$2 wrc=$3 wtxt=$4; shift 4
	local v out rc
	v=$(./elfinfo_asan "$@" 2>&1 | sed -n 's/^verdict: //p')
	out=$(timeout 20 "${RUN[@]}" 2>&1); rc=$?
	if [ "$v" = "$wv" ] && [ $rc -eq "$wrc" ] && grep -qF -- "$wtxt" <<<"$out"; then
		ok "predict $name: '$v' and run rc=$rc"
	else
		bad "predict $name: verdict='$v' (want '$wv') rc=$rc (want $wrc) out=$out"
	fi
}
for T in $TRIPLETS; do
	O=$W/out/$T; Q=${QEMU[$T]}; S=${SR[$T]}; R=$W/rootfs/$T
	mkroot "$T" "$R"
	RUN=("$Q" -L "$S" "$O/app")
	predict "$T app" RUNNABLE 0 "OK" --sysroot "$S" "$O/app"
	RUN=("$Q" -L "$S" "$O/app_norpath")
	predict "$T app_norpath" "MISSING 1" 127 "libsensor.so.1: cannot open shared object file" \
		--sysroot "$S" "$O/app_norpath"
	RUN=("$Q" -L "$R" "$O/app_norpath")
	predict "$T app_norpath, mini rootfs" "MISSING 1" 127 "libsensor.so.1: cannot open shared object file" \
		--sysroot "$R" "$O/app_norpath"
	RUN=("$Q" -L "$R" -E LD_LIBRARY_PATH=/opt/sensor/lib "$O/app_norpath")
	predict "$T app_norpath, mini rootfs + LD_LIBRARY_PATH=/opt/sensor/lib" RUNNABLE 0 "OK" \
		--sysroot "$R" --libpath /opt/sensor/lib "$O/app_norpath"
done
RUN=(qemu-arm -L /usr/x86_64-linux-gnu out/arm-linux-gnueabihf/app)
predict "armhf app, wrong -L (x86_64 sysroot)" "MISSING 2" 255 "Could not open '/lib/ld-linux-armhf.so.3'" \
	--sysroot /usr/x86_64-linux-gnu out/arm-linux-gnueabihf/app
RUN=(qemu-x86_64 -L /usr/arm-linux-gnueabihf out/x86_64-linux-gnu/app)
predict "x86_64 app, wrong -L (armhf sysroot)" "MISSING 2" 255 "Could not open '/lib64/ld-linux-x86-64.so.2'" \
	--sysroot /usr/arm-linux-gnueabihf out/x86_64-linux-gnu/app
RUN=(qemu-arm -L /usr/arm-linux-gnueabihf mix/app)
predict "armhf app + 64-bit libsensor" "MISSING 1" 127 "libsensor.so.1: wrong ELF class: ELFCLASS64" \
	--sysroot /usr/arm-linux-gnueabihf mix/app
RUN=(qemu-arm -L "$W/stage" out/arm-linux-gnueabihf/app)
predict "armhf app, rootfs without libc" "MISSING 1" 127 "libc.so.6: cannot open shared object file" \
	--sysroot "$W/stage" out/arm-linux-gnueabihf/app

# ---- 7. (practice) 어디서 찾았나: elfinfo 의 via == LD_DEBUG=libs 의 search path 종류 -----------
if [ "$VARIANT" = practice ]; then
	A=$W/out/arm-linux-gnueabihf; S=/usr/arm-linux-gnueabihf; R=$W/rootfs/arm-linux-gnueabihf
	R2=$W/rootfs2; mkroot arm-linux-gnueabihf "$R2"
	mkdir -p "$R/opt/mix" "$R2/usr/lib"
	cp out/aarch64-linux-gnu/lib/libsensor.so.1 "$R/opt/mix/"    # 64-bit: ld.so 가 건너뛴다
	cp "$A/lib/libsensor.so.1" "$R2/usr/lib/"                    # 기본 디렉터리에만 있음
	ldkind() {   # LD_DEBUG=libs 에서 libsensor.so.1 을 실제로 연 search path 의 종류
		awk '/search path=/{k=$0; sub(/.*\(/,"",k); split(k,a," "); k=a[1]}
		     /search cache=/{k="cache"}
		     /trying file=.*libsensor\.so\.1$/{getline nx; if (nx !~ /\(/) {print k; exit}}' |
		sed -e 's/^RPATH$/rpath/' -e 's/^RUNPATH$/runpath/' -e 's/^LD_LIBRARY_PATH)*$/libpath/' -e 's/^system$/default/'
	}
	via_case() {   # $1 이름, $2 sysroot, $3 libpath(빈칸 가능), $4 바이너리
		local name=$1 sr=$2 lp=$3 bin=$4 mine theirs
		mine=$(./elfinfo_asan --more --sysroot "$sr" ${lp:+--libpath "$lp"} "$bin" |
			sed -n 's/^needed libsensor.so.1 -> .* (via \(.*\))$/\1/p')
		theirs=$(timeout 20 qemu-arm -L "$sr" ${lp:+-E LD_LIBRARY_PATH="$lp"} -E LD_DEBUG=libs "$bin" 2>&1 | ldkind)
		[ -n "$mine" ] && [ "$mine" = "$theirs" ] && ok "via $name: $mine (LD_DEBUG: $theirs)" \
			|| bad "via $name: elfinfo='$mine' LD_DEBUG='$theirs'"
	}
	via_case "app_rpath" "$S" "" "$A/app_rpath"
	via_case "app + LD_LIBRARY_PATH=/opt/sensor/lib" "$R" /opt/sensor/lib "$A/app"
	via_case "app_rpath + LD_LIBRARY_PATH (RPATH wins)" "$R" /opt/sensor/lib "$A/app_rpath"
	via_case "app + LD_LIBRARY_PATH=/opt/mix (64-bit, skipped)" "$R" /opt/mix "$A/app"
	via_case "app_norpath, lib only in rootfs /usr/lib" "$R2" "" "$A/app_norpath"
fi
finish
