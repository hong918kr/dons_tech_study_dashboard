/* ENV: qemu */
/* BM-B09 — qemu-system-arm -M mps2-an385 (Cortex-M3) 용 링커 스크립트.
 * QEMU hw/arm/mps2.c (v10.2.0): ZBT SSRAM1 0x00000000 4MB (코드·벡터), SSRAM2&3 0x20000000 4MB (데이터·스택).
 * newlib 이 쓰는 섹션(.init_array/.fini_array/.ARM.exidx)과 힙 심볼(end, __HeapLimit)을 둔다. */
MEMORY
{
    CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}
STACK_SIZE = 16K;
_estack = ORIGIN(RAM) + LENGTH(RAM);
__StackLimit = _estack - STACK_SIZE;
ENTRY(Reset_Handler)
SECTIONS
{
    .text : {
        KEEP(*(.isr_vector))            /* 0x0: [0]=MSP 초기값, [1]=Reset_Handler ... */
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > CODE
    .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > CODE
    .init_array    : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > CODE
    .fini_array    : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > CODE
    .ARM.exidx     : { *(.ARM.exidx*) } > CODE
    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > CODE
    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; __bss_end__ = .; } > RAM
    . = ALIGN(8);
    end = .;                            /* newlib/libgloss _sbrk 가 찾는 힙 시작 심볼 */
    __heap_start = .;
    __HeapLimit = __StackLimit;         /* 힙은 스택 바닥(16 KB 위)까지만 */
    ASSERT(__heap_start < __StackLimit, "RAM overflow: .data+.bss reach the stack")
    /DISCARD/ : { *(.comment) }
}
