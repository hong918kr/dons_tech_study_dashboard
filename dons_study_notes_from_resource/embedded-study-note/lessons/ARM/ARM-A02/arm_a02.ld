/* ENV: qemu */
/* ARM-A02 arm_a02.ld — qemu-system-aarch64 -M virt,gic-version=2 베어메탈 (ARM-M13 arm_m13.ld 축약판).
 * RAM = 0x40000000 부터. QEMU 는 -kernel ELF 를 프로그램 헤더 주소에 올리고 PC = e_entry 로 시작한다.
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
    . = . + 0x10000;                 /* 64 KB 스택 = SP_EL1 (예외 프레임도 여기에 쌓인다) */
    __stack_top = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
