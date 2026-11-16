#!/usr/bin/env bash
# TOOL-M10 demo — Linux 쪽 (Lima VM `elinux`, aarch64 Ubuntu): valgrind memcheck, LSan, MSan
#   bash demo_vm.sh   (맥에서 실행) → valgrind_starter.txt valgrind_reachable.txt valgrind_solution.txt
#                                      lsan_vm.txt msan_vm.txt  (이 디렉터리에)
# 맥 홈은 VM 에서 읽기 전용 → 소스를 VM /tmp/TOOL-M10_work/vm 으로 복사해서 빌드한다.
set -u
cd "$(dirname "$0")" || exit 1
SRC=$(pwd)
VM="limactl shell elinux -- bash -lc"
$VM "rm -rf /tmp/TOOL-M10_work/vm && mkdir -p /tmp/TOOL-M10_work/vm && cp '$SRC'/starter.c '$SRC'/solution.c /tmp/TOOL-M10_work/vm/ &&
     cd /tmp/TOOL-M10_work/vm && gcc -std=c11 -g -O0 starter.c -o ev_g && gcc -std=c11 -g -O0 solution.c -o evs_g" || exit 1
run() { $VM "cd /tmp/TOOL-M10_work/vm && { $1 ; } 2>&1" 2>&1 | sed -E 's/^==[0-9]+==/==PID==/'; }

# 1) memcheck 한 번에 버그 전부 (valgrind 는 첫 오류에서 멈추지 않는다)
{ echo '$ valgrind --version'; run "valgrind --version"
  echo '$ gcc -std=c11 -g -O0 starter.c -o ev_g && valgrind --leak-check=full --track-origins=yes ./ev_g all'
  run "valgrind --leak-check=full --track-origins=yes ./ev_g all; echo \"[exit code \$?]\""; } >valgrind_starter.txt

# 2) still reachable 을 펼쳐 보기 (cache 케이스만)
{ echo '$ valgrind --leak-check=full --show-leak-kinds=all ./ev_g cache'
  run "valgrind --leak-check=full --show-leak-kinds=all ./ev_g cache; echo \"[exit code \$?]\""
  echo
  echo '$ valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=9 ./ev_g cache 2>&1 | tail -3   # CI 게이트로 쓰려면'
  run "valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --error-exitcode=9 ./ev_g cache 2>&1 | tail -3; echo \"[exit code \${PIPESTATUS[0]}]\""
} >valgrind_reachable.txt

# 3) 고친 코드: 오류 0, 누수 0
{ echo '$ gcc -std=c11 -g -O0 solution.c -o evs_g && valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=9 ./evs_g all'
  run "valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=9 ./evs_g all; echo \"[exit code \$?]\""; } >valgrind_solution.txt

# 4) LeakSanitizer: 단독(-fsanitize=leak) / ASan 에 포함 — 닿는 캐시는 안 보인다
{ echo '$ clang -std=c11 -g -O1 -fsanitize=leak starter.c -o ev_lsan && ./ev_lsan leak'
  run "clang -std=c11 -g -O1 -fsanitize=leak starter.c -o ev_lsan && ./ev_lsan leak; echo \"[exit code \$?]\""
  echo
  echo '$ ./ev_lsan cache'
  run "./ev_lsan cache; echo \"[exit code \$?]\""
  echo
  echo '$ clang -std=c11 -g -O1 -fno-omit-frame-pointer -fsanitize=address starter.c -o ev_asan && ./ev_asan leak 2>&1 | tail -2'
  run "clang -std=c11 -g -O1 -fno-omit-frame-pointer -fsanitize=address starter.c -o ev_asan && ./ev_asan leak 2>&1 | tail -2"
} >lsan_vm.txt

# 5) MemorySanitizer: 초기화 안 된 값이 "분기/출력에 쓰이는 순간" 을 잡는다 (+ 어디서 생겼는지)
{ echo '$ clang -std=c11 -g -O1 -fsanitize=memory -fsanitize-memory-track-origins starter.c -o ev_msan && ./ev_msan uninit'
  run "clang -std=c11 -g -O1 -fsanitize=memory -fsanitize-memory-track-origins starter.c -o ev_msan && ./ev_msan uninit; echo \"[exit code \$?]\""
  echo
  echo '$ ./ev_msan uaf      # MSan 은 해제 후 사용을 안 본다'
  run "./ev_msan uaf; echo \"[exit code \$?]\""
} >msan_vm.txt
wc -l valgrind_starter.txt valgrind_reachable.txt valgrind_solution.txt lsan_vm.txt msan_vm.txt
