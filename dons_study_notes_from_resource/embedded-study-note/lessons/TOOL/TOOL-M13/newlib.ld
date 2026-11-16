/* TOOL-M13 newlib.ld — demo_stages.sh (VM, newlib / newlib-nano 단계) 전용. solution.ld 에
 * newlib 이 요구하는 것만 더했다: .ARM.extab, .init_array/.fini_array, 그리고 _sbrk(nosys) 가 힙 시작으로
 * 쓰는 심볼 `end`. 메모리 맵은 같다 (FLASH 64 KB @0x0, RAM 16 KB @0x20000000, mps2-an386).
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 64K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 16K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
    .text       : { *(.text .text.*) . = ALIGN(4); } > FLASH
    .rodata     : { . = ALIGN(4); *(.rodata .rodata.*) . = ALIGN(4); } > FLASH
    .ARM.extab  : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx  : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > FLASH
    .init_array : { KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) } > FLASH
    .fini_array : { KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array)) } > FLASH

    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data .data.*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss .bss.* COMMON) . = ALIGN(4); _ebss = .; } > RAM
    PROVIDE(end = _ebss);   /* newlib _sbrk: 힙은 .bss 끝에서 스택 쪽으로 자란다 */
}
