/* ENV: qemu */
/* ARM-M13 arm_m13.ld — qemu-system-aarch64 -M virt 베어메탈 (RAM = 0x40000000 부터, 기본 128 MB).
 * QEMU 는 -kernel 로 받은 ELF 를 프로그램 헤더의 물리 주소에 그대로 올리고 PC = e_entry 로 시작한다.
 * 0x40000000 에 링크해도 실측으로 동작한다. 0x80000 을 띄운 것은 arm64 Linux Image 관례(text_offset)를 따른 것
 * — Linux Image 부팅 때 QEMU 가 RAM 앞쪽을 DTB 등에 쓰는 것과 겹치지 않게 (확인 권장). ELF 부팅 실측: x0 = 0, 0x40000000 = 0.
 */
ENTRY(_start)
SECTIONS
{
    . = 0x40080000;
    .text : {
        KEEP(*(.text.boot))          /* _start 를 맨 앞에 */
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss (NOLOAD) : {
        . = ALIGN(16);
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
    . = ALIGN(16);
    . = . + 0x10000;                 /* 64 KB 부트 스택 (EL1 이면 SP_EL1) */
    __stack_top = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
