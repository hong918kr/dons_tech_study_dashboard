#!/bin/bash
# CPP-A08 link_seam.sh — 링크 심을 진짜 파일 분리로 빌드·실행하고, 링커가 무엇을 고르는지 확인한다 (맥에서 실행, 성공 시 exit 0)
#   1) sensor.hpp / fake_tmp.hpp 가 solution.cpp 의 해당 구간과 같은지 (복사 오차 방지)
#   2) 호스트 테스트: sensor.o + i2c_fake.o + test_seam.o → 실행
#   3) nm: sensor.o 는 i2c::write_read 를 U(미정의)로만, i2c_fake.o / i2c_hw.o 는 T(정의)
#   4) 둘 다 링크 → duplicate symbol, 둘 다 빼면 → undefined symbol (둘 다 "실패해야 정상")
#   5) 정적 라이브러리(.a)에 넣으면 중복 에러가 사라진다 (ld64)
#   6) Cortex-M4 오브젝트 (clang --target=thumbv7em, arm-none-eabi 헤더): 같은 U/T 구조
set -u
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
cd "$(dirname "$0")"
O=$(mktemp -d /tmp/cpp_a08_seam.XXXXXX); trap 'rm -rf "$O"' EXIT
F="-std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti"
bad=0
ok()   { echo "  ok   $*"; }
fail() { echo "  FAIL $*"; bad=1; }

echo "== 1. 제품 헤더 = solution.cpp 구간"
python3 - <<'PY' && ok "sensor.hpp, fake_tmp.hpp 본문이 solution.cpp 와 같다" || fail "헤더가 solution.cpp 와 다르다"
s = open('solution.cpp').read()
def cut(a, b): return s[s.index(a):s.index(b)].rstrip()
h1 = open('sensor.hpp').read(); h2 = open('fake_tmp.hpp').read()
assert cut('/* ===================== 센서 프로토콜', '/* ===================== 가짜 장치 FakeTmp') in h1
assert cut('/* ===================== 가짜 장치 FakeTmp', '/* [B3] (3) 링크 심 가짜') in h2
PY

echo "== 2. 호스트 테스트 빌드 (sensor.o + i2c_fake.o + test_seam.o)"
for f in sensor i2c_fake test_seam i2c_hw; do c++ $F -c $f.cpp -o $O/$f.o || { fail "compile $f"; exit 1; }; done
c++ $O/sensor.o $O/i2c_fake.o $O/test_seam.o -o $O/test_seam || fail "link test"
"$O/test_seam" && ok "test_seam 실행 성공" || fail "test_seam"

echo "== 3. 누가 정의하고 누가 부르나 (xcrun llvm-nm -C)"
for f in sensor i2c_fake i2c_hw; do
    echo "-- $f.o"; xcrun llvm-nm -C $O/$f.o | grep -E 'i2c::write_read|TempSensor<LinkBus>::init|TempSensor<II2c>::init' | sed 's/^/     /'
done
xcrun llvm-nm -C $O/sensor.o | grep -qE '^ +U i2c::write_read' && ok "sensor.o: i2c::write_read 는 U" || fail "sensor.o 에 U 가 없다"
xcrun llvm-nm -C $O/i2c_fake.o | grep -qE ' T i2c::write_read' && ok "i2c_fake.o: T" || fail "i2c_fake.o 정의 없음"

echo "== 4. 링커는 하나만 받는다"
if c++ $O/sensor.o $O/i2c_fake.o $O/i2c_hw.o $O/test_seam.o -o $O/both 2>$O/both.log; then fail "둘 다 링크했는데 성공?"
else grep -m2 -E 'duplicate symbol|i2c::write' $O/both.log | sed 's/^/     /'; ok "fake + hw → 링크 실패 (기대대로)"; fi
if c++ $O/sensor.o $O/test_seam.o -o $O/none 2>$O/none.log; then fail "정의 없이 링크 성공?"
else grep -m3 -E 'Undefined|i2c::write|link_fake' $O/none.log | sed 's/^/     /'; ok "정의 없음 → 링크 실패 (기대대로)"; fi

echo "== 5. 정적 라이브러리 함정: libhw.a 에 넣으면 '중복' 이 조용히 사라진다 (Mac ld64)"
ar rcs $O/libhw.a $O/i2c_hw.o
for order in "OBJ LIB" "LIB OBJ"; do
    if [ "$order" = "OBJ LIB" ]; then args="$O/sensor.o $O/i2c_fake.o $O/test_seam.o $O/libhw.a"; else args="$O/libhw.a $O/sensor.o $O/i2c_fake.o $O/test_seam.o"; fi
    if c++ $args -o $O/ar 2>$O/ar.log && "$O/ar" >/dev/null; then
        ok "[$order] 링크 성공 + 테스트 통과 — 가짜가 이겼고 libhw.a(i2c_hw.o) 는 끌려오지 않았다 (경고 없음)"
    else fail "[$order] $(head -1 $O/ar.log)"; fi
done
echo "     (GNU ld 는 순서에 민감: 'sensor.o libhw.a i2c_fake.o' → multiple definition — size_targets.sh vm archive)"

echo "== 6. Cortex-M4 오브젝트 (recipe b)"
if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
    echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
    mkdir -p "$ARM_INC"
    limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
fi
T="--target=thumbv7em-none-eabihf -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -ffreestanding \
   -nostdinc++ -isystem $ARM_INC/c++/14.2.1 -isystem $ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard -isystem $ARM_INC"
for f in sensor i2c_hw; do clang++ $T -c $f.cpp -o $O/m4_$f.o || { fail "m4 compile $f"; continue; }; done
xcrun llvm-nm -C $O/m4_sensor.o | grep -E 'i2c::write' | sed 's/^/     /'
xcrun llvm-nm -C $O/m4_i2c_hw.o | grep -E 'i2c::write' | sed 's/^/     /'
xcrun llvm-nm -C $O/m4_sensor.o | grep -qE '^ +U i2c::write_read' && ok "m4 sensor.o: U i2c::write_read" || fail "m4 sensor.o"

[ $bad = 0 ] && echo "link_seam.sh: ALL OK" || echo "link_seam.sh: FAILED"
exit $bad
