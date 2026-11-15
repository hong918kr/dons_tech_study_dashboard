#!/bin/bash
# CPP-A10 budget.sh — Cortex-M4 이미지(fw_main.cpp + solution.cpp 라이브러리)의 크기 예산·힙 0·예외 0 검사.
#   bash budget.sh                               # 맥: Lima VM 'elinux' 로 복사해서 실행 (arm-none-eabi-g++ 14.2)
#   A10_EXTRA=-DA10_DEMO_HEAP bash budget.sh     # 일부러 new 를 넣은 빌드 → 금지 심볼로 exit 1 이 "정상"
#   BUDGET_TEXT=4096 bash budget.sh              # 예산 바꿔 보기
# exit 0 = .text ≤ 예산 + 금지 심볼 없음 + .init_array 없음. 하나라도 어기면 exit 1.
# (맥에 Arm GNU Toolchain 이 있으면 VM 없이 그대로 돈다. VM 안에서 맥 홈은 읽기 전용이라 /tmp 로 복사한다.)
set -u
BUDGET_TEXT=${BUDGET_TEXT:-8192}
cd "$(dirname "$0")"
if ! command -v arm-none-eabi-g++ >/dev/null 2>&1; then
    if [ -n "${A10_IN_VM:-}" ]; then echo "arm-none-eabi-g++ not found" >&2; exit 2; fi
    D=/tmp/CPP-A10-budget
    exec limactl shell elinux -- bash -c "rm -rf $D && mkdir -p $D && cp '$PWD'/solution.cpp '$PWD'/fw_main.cpp '$PWD'/fw.ld '$PWD'/budget.sh $D/ && cd $D && A10_IN_VM=1 BUDGET_TEXT=$BUDGET_TEXT A10_EXTRA='${A10_EXTRA:-}' bash budget.sh"
fi
CPU="-mcpu=cortex-m4 -mthumb"
F="-std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -ffunction-sections -fdata-sections"
L="-nostartfiles --specs=nosys.specs -T fw.ld -Wl,--gc-sections"
build() {   # build <opt> <out>
    arm-none-eabi-g++ $CPU $F $1 ${A10_EXTRA:-} fw_main.cpp $L -o $2 2>&1 | grep -v -E 'is not implemented and will always fail|^$' || true
    [ -f "$2" ]
}
echo "### $(arm-none-eabi-g++ --version | head -1)  ${A10_EXTRA:-}"
build -O2 fw_O2.elf || { echo "build -O2 failed"; exit 1; }
build "-Os -fstack-usage" fw.elf || { echo "build -Os failed"; exit 1; }
echo "### -O2 (참고)"; arm-none-eabi-size fw_O2.elf | tail -1
echo "### -Os (예산 대상) — Berkeley (text = .isr_vector+.text+.rodata+..., 플래시엔 + data)"
arm-none-eabi-size fw.elf
echo "### 섹션 (size -A)"
arm-none-eabi-size -A fw.elf | grep -E '^\.(isr_vector|text|rodata|ARM|data|bss|init_array|fini_array)'
echo "### 컴포넌트별 (nm -C --size-sort, 인라인된 코드는 호출자 몫으로 잡힌다)"
RO=$(arm-none-eabi-size -A fw.elf | awk '$1 == ".rodata" { print $3, $3 + $2 }')
DA=$(arm-none-eabi-size -A fw.elf | awk '$1 == ".data" { print $3, $3 + $2 }')
arm-none-eabi-nm -C --size-sort -S -t d fw.elf | awk -v ro="$RO" -v da="$DA" '
  BEGIN { split(ro, R, " "); split(da, D, " ") }
  { addr = $1 + 0; sz = $2 + 0; t = $3; name = ""; for (i = 4; i <= NF; i++) name = name (i > 4 ? " " : "") $i
    if (t !~ /[tTrRdDbBWwVu]/ || name ~ /^_(s|e)(data|bss|idata|stack)$|^__exidx_(start|end)$/) next
    if (addr >= 536870912) sec = (addr >= D[1] && addr < D[2]) ? "data" : "bss"
    else sec = (addr >= R[1] && addr < R[2]) ? "rodata" : "text"
    if (name == "g_vectors") sec = "vector"
    q = name; while (gsub(/<[^<>]*>/, "", q)) {}          # 템플릿 인자 제거
    sub(/\(.*/, "", q); n = split(q, P, " "); q = P[n]    # 반환형 제거 → 정규화된 함수/변수 이름
    c = "기타 (libc·startup)"
    if (q ~ /^a10::fw::|^(Reset_Handler|Default_Handler|SysTick_Handler|g_vectors)$/) c = "fw (MMIO I2C·UART·벡터·리셋)"
    else if (q ~ /^a10::app::/) c = "app (Pipeline::step)"
    else if (q ~ /^a10::tlm::/) c = "tlm (CRC·COBS·frame)"
    else if (q ~ /^a10::imu::/) c = "imu (probe·configure·read_sample)"
    else if (q ~ /^a10::bus::/) c = "bus (xfer)"
    else if (q ~ /^a10::dsp::/) c = "dsp (LowPass)"
    tot[c "|" sec] += sz; comp[c] = 1
    printf "  %6d %-6s %s %s\n", sz, sec, t, name }
  END { printf "  ---- 합계 %-34s %6s %6s %6s %6s %6s\n", "(컴포넌트)", "text", "rodata", "data", "bss", "vector"
        for (c in comp) printf "  %-44s %6d %6d %6d %6d %6d\n", c, tot[c "|text"], tot[c "|rodata"], tot[c "|data"], tot[c "|bss"], tot[c "|vector"] }'
echo "### 스택 (-fstack-usage, 함수별 프레임 바이트 — 호출 깊이 합은 아님)"
cat *.su 2>/dev/null | sed 's/^[^:]*:[0-9]*:[0-9]*://' | sort -t$'\t' -k2 -n -r | head -8 | awk -F'\t' '{ printf "  %5s %-8s %s\n", $2, $3, substr($1, 1, 90) }'
rc=0
TEXT=$(arm-none-eabi-size -A fw.elf | awk '$1 == ".text" { print $2 }')
echo "### 예산: .text = $TEXT / $BUDGET_TEXT 바이트"
if [ "$TEXT" -gt "$BUDGET_TEXT" ]; then echo "FAIL: .text 예산 초과"; rc=1; else echo "OK: .text 예산 이내"; fi
BAD=$(arm-none-eabi-nm fw.elf | grep -E ' (_Znwj|_Znaj|_ZdlPv|_ZdaPv|_Znwm|malloc|_malloc_r|free|_free_r|_sbrk|_sbrk_r|__cxa_[a-z_]+|__gxx_personality_v0|_Unwind_[A-Za-z_]+|__aeabi_unwind_cpp_pr[0-9]|abort)$' || true)
if [ -n "$BAD" ]; then echo "FAIL: 금지 심볼 (힙/예외/abort)"; echo "$BAD" | sed 's/^/  /'; rc=1; else echo "OK: 힙·예외 심볼 없음 (_Znwj/malloc/_sbrk/__cxa_*/_Unwind_*/__gxx_personality_v0/abort)"; fi
if arm-none-eabi-size -A fw.elf | grep -q '^\.init_array'; then echo "FAIL: .init_array 있음 (동적 초기화)"; rc=1; else echo "OK: .init_array 없음 (constinit)"; fi
echo "### exit $rc"
exit $rc
