/* TOOL-M13 starter.ld — 가상의 Cortex-M4 부품 (QEMU mps2-an386 메모리 맵에 맞춤).
 * FLASH 64 KB @0x00000000, RAM 16 KB @0x20000000. 문법·VMA/LMA 는 TOOL-M01, 정렬·ASSERT 심화는 TOOL-A07.
 * --gc-sections 와 함께 쓸 때의 규칙: "코드가 참조하지 않지만 필요한 것" 은 KEEP() 으로 뿌리(root)로 만든다.
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
    .isr_vector : { . = ALIGN(4); *(.isr_vector) . = ALIGN(4); } > FLASH
    .text       : { *(.text .text.*) . = ALIGN(4); } > FLASH
    .rodata     : { . = ALIGN(4); *(.rodata .rodata.*) . = ALIGN(4); } > FLASH
    .ARM.exidx  : { *(.ARM.exidx*) } > FLASH

    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data .data.*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss .bss.* COMMON) . = ALIGN(4); _ebss = .; } > RAM
}
