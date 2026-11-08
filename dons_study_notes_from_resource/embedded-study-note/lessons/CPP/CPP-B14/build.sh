#!/usr/bin/env bash
# CPP-B14 — 하네스 3개 + 데모 전부 빌드/실행 (macOS, Apple clang)
# 사용: bash build.sh
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 문제 회피
F="-std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti"
T=${TMPDIR:-/tmp}/cpp_b14; mkdir -p "$T"
cd "$(dirname "$0")"

step() { printf '\n### %s\n' "$*"; }

step "starter (FAIL 이 나와야 정상)";  c++ $F starter.cpp  -o "$T/s" && "$T/s"; echo "exit $?"
step "solution";                       c++ $F solution.cpp -o "$T/s" && "$T/s"; echo "exit $?"
step "practice";                       c++ $F practice.cpp -o "$T/p" && "$T/p"; echo "exit $?"

step "abort 스텁: 힙 안 씀";           c++ $F demo_abort_stub.cpp -o "$T/a" && "$T/a"; echo "exit $?"
step "abort 스텁: std::vector 사용";   c++ $F -DUSE_HEAP demo_abort_stub.cpp -o "$T/a" && "$T/a"; echo "exit $?"

step "링크 에러 방법1 (POISON + dead_strip): 힙 안 씀"
c++ $F -DPOISON -Wl,-dead_strip demo_link_error.cpp -o "$T/l" && "$T/l"; echo "exit $?"
step "링크 에러 방법1: vector/string/function 사용"
c++ $F -DPOISON -DSHOW_ERROR -Wl,-dead_strip demo_link_error.cpp -o "$T/l"; echo "exit $?"
step "링크 에러 방법2 (-nostdlib++): 힙 안 씀"
c++ $F -nostdlib++ demo_link_error.cpp -o "$T/l" && "$T/l"; echo "exit $?"
step "링크 에러 방법2: vector/string/function 사용"
c++ $F -nostdlib++ -DSHOW_ERROR demo_link_error.cpp -o "$T/l"; echo "exit $?"
step "poison 을 별도 TU 로: 누가 불렀나 (-why_live)"
c++ $F -Wl,-dead_strip demo_poison_new.cpp demo_poison_app.cpp -o "$T/two"; echo "exit $?"
c++ $F -Wl,-dead_strip -Wl,-why_live,__Znwm demo_poison_new.cpp demo_poison_app.cpp -o "$T/two" 2>&1 | c++filt | sed -E 's#/[^ ]*/##'; echo "exit ${PIPESTATUS[0]}"
step "함정: delete 스텁 없이 virtual 소멸자만 있어도"
c++ $F -nostdlib++ -DNO_DELETE_STUBS demo_link_error.cpp -o "$T/l"; echo "exit $?"

step "= delete: 정상 빌드";            c++ $F demo_delete_error.cpp -o "$T/d" && "$T/d"; echo "exit $?"
step "= delete: SHOW_ERROR";           c++ $F -DSHOW_ERROR -c demo_delete_error.cpp -o /dev/null; echo "exit $?"

step "심볼 스캔 (연습 6): 힙 안 쓰는 링크 결과에 operator new / malloc 참조가 있나?"
c++ $F -nostdlib++ demo_link_error.cpp -o "$T/fw"
nm -u "$T/fw" | c++filt | grep -E "operator new|operator delete|malloc" || echo "(참조 없음)"
step "비교: solution 바이너리"
nm "$T/s" | grep -E " __Znwm$| __ZnwmSt11align_val_t$| _malloc$"
