#!/bin/bash
# RTOS-M08: 객체 파일(.o)에서 호출 그래프(caller callee 쌍)를 뽑는다.
#   clang 에는 GCC 의 -fcallgraph-info 가 없다 → 디스어셈블리의 직접 호출을 읽는다.
#   ① 재배치(relocation) 가 붙은 호출: R_ARM_THM_CALL(bl) / R_ARM_THM_JUMP24(b.w = 꼬리 호출) / ARM64_RELOC_BRANCH26
#   ② 같은 섹션 안에서 이미 해결된 호출: "bl 0xd0c <_fmt_u32_rec>" 처럼 +오프셋 없는 심볼 (arm64 자기 재귀가 이렇다)
#      단 "b <자기 자신>" 은 호출이 아니라 루프다 — rx_task 의 for(;;) 가 함수 첫 주소로 돌아간다 (md 7. 자주 하는 실수)
#   ③ 간접 호출 blx rN / blr xN → callee "*INDIRECT*" (도구가 에러로 처리 → 사람이 후보를 적어 줘야 함)
# Mach-O 심볼의 앞 '_' 한 개는 떼어 ELF 이름과 맞춘다.
# 사용: bash callgraph.sh file.o [more.o ...]   → stdout 에 "caller callee" 줄 (중복 제거)
for o in "$@"; do
  xcrun llvm-objdump -d -r --no-show-raw-insn "$o"
done | awk '
  function strip(s) { if (macho && substr(s, 1, 1) == "_") return substr(s, 2); return s }
  /file format mach-o/ { macho = 1 }
  /file format elf/    { macho = 0 }
  /^[0-9a-f]+ <[^>]+>:$/ { cur = $2; gsub(/[<>:]/, "", cur); cur = strip(cur); next }
  /R_ARM_THM_CALL|R_ARM_THM_JUMP24|ARM64_RELOC_BRANCH26/ { print cur, strip($NF); next }
  /\t(bl|b|b\.w)\t0x[0-9a-f]+ <[^+>]+>/ {
      t = $0; sub(/.*</, "", t); sub(/>.*/, "", t); t = strip(t)
      if ($0 ~ /\tbl\t/ || t != cur) print cur, t      # bl 자기 자신 = 진짜 재귀(도구가 잡는다)
      next }                                           # b 자기 자신 = for(;;) 루프의 뒤 분기 (스택 안 늘어남)
  /\t(blx|blr)\t(r[0-9]+|x[0-9]+|ip|lr)/ { print cur, "*INDIRECT*" }
' | sort -u
