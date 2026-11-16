# TOOL-M07 demo_vm_hunt.gdb — solution.lldb 와 같은 사냥을 gdb 로 (qemu-aarch64 -g 사용자 모드)
# 사용자 모드 gdbstub 은 하드웨어 워치포인트가 없다(demo_vm_hw.gdb) → 조건부 BP 로 좁힌 뒤 소프트웨어 워치포인트
set pagination off
set confirm off
set debuginfod enabled off
set trace-commands on
source ringbuf_gdb.py
target remote :23472
break rb_put_escaped if rb->head == 15
continue
print g_tick
print *rb
set can-use-hw-watchpoints 0
watch dev.link_state
continue
bt 3
info line *($pc - 4)
print/x dev.link_state
x/20xb &dev.tx.buf[0]
delete
break rb_put_escaped if rb->head == 15
commands
silent
printf "[log] tick %u head=%u count=%u\n", g_tick, rb->head, rb->count
continue
end
continue
