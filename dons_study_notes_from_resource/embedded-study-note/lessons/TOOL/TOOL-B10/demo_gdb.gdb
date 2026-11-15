# TOOL-B10 demo_gdb.gdb — demo_debug.lldb 와 같은 세션을 gdb 로 (Lima VM, aarch64 Linux)
# 이 VM 의 네이티브 gdb 는 "Unable to fetch SVE/SSVE vector length" 로 run 을 못 해서
# qemu-aarch64 사용자 모드 gdbstub 에 원격으로 붙는다 (OpenOCD/J-Link 원격과 같은 모양, TOOL-A03):
#   gcc -std=c11 -g -O0 -static starter.c -o starter_g
#   qemu-aarch64 -g 23410 ./starter_g delete_head &   gdb-multiarch -q -batch -x demo_gdb.gdb ./starter_g
set pagination off
set confirm off
set debuginfod enabled off
set trace-commands on
target remote :23410
break list_delete
continue
bt
info args
info locals
display cur
next
next
undisplay 1
print cur
print *cur
print cur->next->value
print prev
next
next
next
step
bt 2
finish
print *cur
up
print l
down
continue
bt
frame 1
print l.head
print *l.head
x/4xw l.head
kill
