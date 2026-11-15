# ARM-M12 demo_fault.gdb — run_qemu.sh gdb 가 쓴다 (QEMU -S -gdb tcp::3412 에 gdb-multiarch 접속).
# 핸들러 안에서 bt 가 무엇을 보여 주나, 그리고 쌓인 프레임으로 PC/LR 을 직접 복원하는 법.
set pagination off
set confirm off
target remote :3412
break hardfault_c
continue
echo \n=== stop 1: 첫 fault (B0 의 LDRD, MSP) — gdb 의 bt ===\n
bt
echo \n=== 진입 인자: r0 = frame, r1 = EXC_RETURN ===\n
p/x $r0
p/x $r1
echo \n=== 쌓인 8 워드 {r0 r1 r2 r3 r12 lr pc xpsr} ===\n
x/8wx $r0
echo \n=== 쌓인 PC / LR 이 가리키는 곳 ===\n
info symbol ((unsigned *)$r0)[6]
info line *((unsigned *)$r0)[6]
x/2i ((unsigned *)$r0)[6]
info symbol ((unsigned *)$r0)[5]
echo \n=== 컨텍스트를 fault 지점으로 바꿔서 보기 (frame 의 pc/lr/sp 를 직접 넣는다) ===\n
set $f = (unsigned *)$r0
set $save_pc = $pc
set $save_lr = $lr
set $save_sp = $sp
set $pc = $f[6]
set $lr = $f[5]
set $sp = (unsigned)$f + 32
bt
set $pc = $save_pc
set $lr = $save_lr
set $sp = $save_sp
echo \n=== stop 2: PSP 태스크 안의 fault (#18) ===\n
ignore 1 16
continue
p/x $r1
p/x $r0
p/x $psp
p/x $msp
bt
x/8wx $r0
info symbol ((unsigned *)$r0)[6]
