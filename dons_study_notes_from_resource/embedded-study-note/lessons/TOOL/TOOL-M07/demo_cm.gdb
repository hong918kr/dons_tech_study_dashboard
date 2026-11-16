# TOOL-M07 demo_cm.gdb — 같은 버그를 Cortex-M3 이미지에서: qemu-system-arm gdbstub = OpenOCD/J-Link GDB 서버와 같은 모양
set pagination off
set confirm off
set debuginfod enabled off
set trace-commands on
source ringbuf_gdb.py
target remote :23474
break main
continue
watch dev.link_state if dev.link_state > 3
info watchpoints
continue
bt 3
info line *($pc - 2)
info line *($pc - 4)
x/3i $pc - 4
print g_tick
print/x dev.link_state
print dev.tx
info watchpoints
kill
