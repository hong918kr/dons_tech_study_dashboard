# TOOL-M07 demo_vm_hw.gdb — qemu-aarch64(사용자 모드) gdbstub 에 하드웨어 워치포인트를 요청하면?
set pagination off
set confirm off
set debuginfod enabled off
set trace-commands on
target remote :23471
break main
continue
watch dev.link_state if dev.link_state > 3
continue
