/* ENV: qemu */
/* ARM-M08 — qemu-system-arm -M mps2-an385 (Cortex-M3).
 * QEMU 보드: ZBT SSRAM1 0x00000000 (코드·벡터, 실제 MCU 의 FLASH 자리) / SSRAM2&3 0x20000000 (데이터·스택).
 * ARM-B14 arm_b14.ld 의 축약판 — 차이: 벡터 테이블 섹션 이름이 .isr_vector (어셈블리 starter/solution.S 가 만든다).
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x20010000: 초기 MSP = 벡터 [0] */
ENTRY(Reset_Handler)
SECTIONS
{
    .isr_vector : {
        KEEP(*(.isr_vector))             /* 주소 0 = VTOR 리셋값. KEEP: --gc-sections 에도 살아남게 */
    } > FLASH
    .text : {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH
    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
ASSERT(SIZEOF(.isr_vector) == 48 * 4, "ARM-M08: vector table must have 16 + 32 entries")
ASSERT(_ebss + 0x800 <= _estack, "ARM-M08: .data + .bss + 2KB MSP stack do not fit in RAM")
