# ENV: qemu
# BM-B08 — 첫 부팅을 gdb 로 한 걸음씩: 벡터 테이블 → Reset_Handler → .data 복사 → main
# 사용: qemu-system-arm -M mps2-an385 ... -kernel solution.elf -s -S &   gdb-multiarch -q -batch -x demo_boot.gdb solution.elf
set pagination off
set confirm off
target remote :1234
set output-radix 16

echo \n=== (1) -S 로 멈춘 리셋 직후: CPU 가 벡터 [0] [1] 에서 읽은 값 ===\n
x/2wx 0
printf "sp=0x%08x  pc=0x%08x  xpsr=0x%08x (bit24 = T)\n", $sp, $pc, $xpsr
info symbol $pc

echo \n=== (2) 복사 전: .data 의 RAM 주소(VMA) 와 FLASH 사본(LMA) ===\n
printf "&g_magic=0x%08x  g_magic(RAM)=0x%08x\n", &g_magic, g_magic
printf "LMA copy at 0x%08x = 0x%08x\n", (char *)&_sidata + ((char *)&g_magic - (char *)&_sdata), *(unsigned int *)((char *)&_sidata + ((char *)&g_magic - (char *)&_sdata))
printf "g_zero[0]=0x%08x  g_ctor_n=%u\n", g_zero[0], g_ctor_n

echo \n=== (3) reset_init 진입 (ram_poison 이 RAM 을 0xA5 로 더럽힌 뒤) ===\n
break reset_init
continue
printf "g_magic=0x%08x  g_zero[0]=0x%08x\n", g_magic, g_zero[0]

echo \n=== (4) watch g_magic: 복사 루프가 쓰는 순간 ===\n
watch g_magic
continue
bt
delete

echo \n=== (5) main 도착: .data 초기값, .bss 0, 생성자 ===\n
break main
continue
printf "g_magic=0x%08x g_neg=%d g_odd={0x%02x,0x%02x,0x%02x}\n", g_magic, g_neg, g_odd[0], g_odd[1], g_odd[2]
print g_msg
print/x g_zero
printf "g_ctor_n=%u log={%u,%u} ctor_saw_magic=0x%08x\n", g_ctor_n, g_ctor_log[0], g_ctor_log[1], g_ctor_saw_magic
printf "sp=0x%08x (_estack=0x%08x)\n", $sp, &_estack
delete
continue
