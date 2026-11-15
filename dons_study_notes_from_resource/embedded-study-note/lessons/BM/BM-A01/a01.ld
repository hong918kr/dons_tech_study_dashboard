/* ENV: qemu */
/* BM-A01 링커 스크립트 — qemu-system-arm -M mps2-an386 (Cortex-M4F). BM-B08 bm_b08.ld 축약판.
 * FLASH 256 KB @0x00000000 (QEMU 에선 SSRAM1 = 쓰기도 되는 RAM 이지만 "플래시" 로 취급),
 * RAM 64 KB @0x20000000. RAM 벡터 테이블은 .bss 안의 정렬된 배열 (C 의 aligned(256)).
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x20010000 */
_Min_Stack_Size = 0x1000;

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : { . = ALIGN(4); _svectors = .; KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH

    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH

    /* "생성자" 목록 (C++ static ctor / __attribute__((constructor)) 와 같은 자리) */
    .init_array : { . = ALIGN(4); __init_array_start = .; KEEP(*(.init_array*)) __init_array_end = .; } > FLASH

    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH

    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}

ASSERT(_ebss + _Min_Stack_Size <= _estack, "BM-A01: .data + .bss + stack do not fit in RAM")
