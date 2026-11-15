/* ENV: qemu */
/* ARM-B13 링커 스크립트 — mps2-an385 (QEMU hw/arm/mps2.c): 0x00000000 SSRAM1 (코드·벡터), 0x20000000 SSRAM2/3 (데이터·스택).
 * BM-B08 의 bm_b08.ld 를 줄인 사본. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x20010000: 초기 MSP (8 바이트 정렬) */
ENTRY(Reset_Handler)
SECTIONS
{
    .text : {
        KEEP(*(.isr_vector))             /* VTOR = 0 → 벡터 테이블이 맨 앞 */
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH
    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
