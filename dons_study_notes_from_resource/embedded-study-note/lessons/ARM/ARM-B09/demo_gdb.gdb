# ARM-B09 demo_gdb.gdb — 같은 관찰을 gdb 로 (Lima VM, aarch64 Linux).
# 이 VM(M6, SME 있음·SVE 없음)에서는 네이티브 gdb 17.1 이 "Unable to fetch SVE/SSVE vector length" 로
# run 을 못 해서, qemu-aarch64 사용자 모드의 gdbstub 에 붙는다 (Cortex-M QEMU 원격 디버깅과 같은 모양, TOOL-A03):
#   gcc -g -O0 -static demo_debug.c starter.S -o dbg
#   qemu-aarch64 -g 1234 ./dbg &  gdb-multiarch -q -batch -x demo_gdb.gdb ./dbg
set pagination off
set debuginfod enabled off
target remote :1234
break my_strlen
continue
disassemble my_strlen
info registers x0 x1 cpsr
x/8xb $x0
stepi
stepi
info registers x1 x2 cpsr
p/t $cpsr >> 28
continue
stepi
set $x1 = $x0 - 1
info registers x0 x1
finish
delete
continue
