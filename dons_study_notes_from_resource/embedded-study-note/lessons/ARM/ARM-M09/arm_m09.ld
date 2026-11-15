/* ENV: qemu */
/* ARM-M09 — qemu-system-arm -M mps2-an385 (Cortex-M3) / mps2-an386 (Cortex-M4F, 같은 메모리 맵).
 * ARM-B14 arm_b14.ld 축약판.
 * QEMU 보드: ZBT SSRAM1 0x00000000 (4 MB, 코드·벡터) / SSRAM2&3 0x20000000 (4 MB, 데이터·스택).
 * 실제 MCU 처럼 앞쪽을 "FLASH", 뒤쪽을 "RAM" 으로 쓴다 (BM-B08 의 축약판).
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x20010000: 초기 MSP (벡터 [0]) */
ENTRY(Reset_Handler)
SECTIONS
{
    .text : {
        KEEP(*(.vectors))                /* 벡터 테이블은 반드시 주소 0 (VTOR 리셋값) */
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH
    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
ASSERT(_ebss + 0x800 <= _estack, "ARM-M09: .data + .bss + 2KB MSP stack do not fit in RAM")
