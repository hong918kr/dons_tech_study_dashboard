#!/bin/bash
# RTOS-M08: FreeRTOS V11.3.2 커널 + ARM_CM4F 포트 + fw_tasks.c 를 Cortex-M4F 로 컴파일해 .su 와 호출 그래프를 모은다.
#   lesson 디렉터리에서: bash cm4/kernel_su.sh   → /tmp/RTOS-M08_k/all.su, all.edges
# 링크·실행은 하지 않는다 (스택 분석만). 결과를 solution 의 도구 모드로 읽는다:
#   ./solution --su /tmp/RTOS-M08_k/all.su --edges /tmp/RTOS-M08_k/all.edges --root xTaskDelayUntil
set -e
K=~/.esn_scratch/FreeRTOS-Kernel
O=/tmp/RTOS-M08_k; mkdir -p $O
CC="clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -O2 -fstack-usage"
INC="-Icm4 -Icm4/stubs -I. -I$K/include -I$K/portable/GCC/ARM_CM4F"
for f in tasks queue list; do $CC $INC -c $K/$f.c -o $O/$f.o; done
$CC $INC -c $K/portable/GCC/ARM_CM4F/port.c -o $O/port.o
$CC $INC -Wall -Wextra -Werror -c cm4/fw_tasks.c -o $O/fw_tasks.o
$CC -Wall -Wextra -Werror -c fw_graph.c -o $O/fw_graph.o
cat $O/{tasks,queue,list,port,fw_tasks,fw_graph}.su > $O/all.su
bash callgraph.sh $O/{tasks,queue,list,port,fw_tasks,fw_graph}.o > $O/all.edges
echo "functions: $(wc -l < $O/all.su)  edges: $(wc -l < $O/all.edges)"
grep -E ':(xTaskDelayUntil|xQueueReceive|xQueueGenericSend|ulTaskGenericNotifyTake|xTaskGetTickCount|xPortSysTickHandler|xTaskIncrementTick|vTaskSwitchContext|xPortPendSVHandler|ctrl_task|comms_task|rx_task)\b' $O/all.su | sed 's/^[^:]*:[0-9]*://'
echo "-- 직접 부르는 외부 함수(.su 없음) / 간접 호출:"
cut -d' ' -f2 $O/all.edges | sort -u | while read -r f; do
  grep -q ":$f	" $O/all.su || echo "  $f  (from: $(grep " $f\$" $O/all.edges | cut -d' ' -f1 | tr '\n' ' '))"
done
