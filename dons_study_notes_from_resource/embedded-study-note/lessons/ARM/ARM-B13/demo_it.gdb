# ARM-B13 — IT 블록을 한 명령씩 실행하며 r0–r2, 플래그(xPSR[31:28]), IT 상태 비트(xPSR[26:25], [15:10])를 본다.
#   run_qemu.sh gdb   (QEMU -s -S 에 gdb-multiarch -batch 로 붙는다)
set pagination off
set confirm off
target remote :1234
define row
  printf "pc=0x%02x r0=%-6d r1=%-2d r2=%-5d xpsr=0x%08x  next: ", $pc, $r0, $r1, $r2, $xpsr
  x/i $pc
end
break clamp_s32
continue
printf "=== call 1: clamp_s32(5000, 0, 4095) ===\n"
row
stepi
row
stepi
row
stepi
row
stepi
row
stepi
row
stepi
row
continue
printf "=== call 2: clamp_s32(-7, 0, 4095) ===\n"
row
stepi
row
stepi
row
stepi
row
stepi
row
stepi
row
stepi
row
delete
continue
