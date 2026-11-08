#!/bin/bash
# RTOS-M01: Cortex-M4F 크기 측정 (lesson 디렉터리에서 실행: bash cm4/measure_cm4.sh)
# ① 객체 크기: configTASK_NOTIFICATION_ARRAY_ENTRIES 등을 바꿔 가며 demo_sizes_cm4.c 를 컴파일 → llvm-nm -S
# ② 코드 크기: tasks.c / queue.c / event_groups.c / timers.c 를 -Os 로 컴파일 → 함수별 바이트
K=~/.esn_scratch/FreeRTOS-Kernel
CC="clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16"
INC="-Icm4 -Icm4/stubs -I$K/include -I$K/portable/GCC/ARM_CM4F"
O=/tmp/RTOS-M01_cm4; mkdir -p $O
echo "== object sizes (bytes), Cortex-M4F, 32-bit TickType_t"
for v in "-DconfigUSE_TASK_NOTIFICATIONS=0" "-DconfigTASK_NOTIFICATION_ARRAY_ENTRIES=1" \
         "-DconfigTASK_NOTIFICATION_ARRAY_ENTRIES=2" "-DconfigTASK_NOTIFICATION_ARRAY_ENTRIES=3" \
         "-DconfigTASK_NOTIFICATION_ARRAY_ENTRIES=4" "-DconfigUSE_TRACE_FACILITY=1 -DconfigUSE_QUEUE_SETS=1"; do
  $CC -O2 -c $INC $v demo_sizes_cm4.c -o $O/sizes.o || exit 1
  printf '%-56s' "$v"
  xcrun llvm-nm -S --defined-only $O/sizes.o | while read -r a s t n; do printf '%s=%d ' "${n#sz_}" "$((16#$s))"; done
  echo
done
echo "== code size (bytes, -Os) of the API entry functions"
for f in tasks queue event_groups timers; do
  $CC -Os -ffunction-sections -c $INC $K/$f.c -o $O/$f.o 2> $O/$f.log || { cat $O/$f.log; exit 1; }
done
PAT=' (xQueueGenericSend|xQueueSemaphoreTake|xQueueGiveFromISR|xTaskGenericNotify|xTaskGenericNotifyFromISR|vTaskGenericNotifyGiveFromISR|ulTaskGenericNotifyTake|xTaskGenericNotifyWait|xEventGroupSetBits|xEventGroupWaitBits|xEventGroupSetBitsFromISR|xTimerPendFunctionCallFromISR)$'
xcrun llvm-nm -S --defined-only $O/tasks.o $O/queue.o $O/event_groups.o $O/timers.o | grep -E "$PAT" |
  while read -r a s t n; do printf '  %-32s %4d\n' "$n" "$((16#$s))"; done
