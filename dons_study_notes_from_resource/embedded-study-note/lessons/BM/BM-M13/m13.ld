/* ENV: qemu */
/* BM-M13 링커 스크립트 — qemu-system-arm -M mps2-an385 (Cortex-M3). BM-B08 의 bm_b08.ld + NOINIT 영역.
 * FLASH 256 KB @0x00000000 (QEMU 에선 SSRAM1), RAM 63 KB @0x20000000, NOINIT 1 KB @0x2000FC00 (고정 주소).
 * 스택은 0x2000FC00 에서 아래로 자란다 → 넘쳐도 위쪽의 .noinit(크래시 기록)은 건드리지 않는다.
 */
MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM    (rwx) : ORIGIN = 0x20000000, LENGTH = 63K
    NOINIT (rw)  : ORIGIN = 0x2000FC00, LENGTH = 1K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x2000FC00 */
_Min_Stack_Size = 0x800;

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH

    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH

    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH

    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM

    /* 리셋을 건너 살아남아야 하는 것: reset_init 이 복사도 0 도 안 하는 영역.
     * NOLOAD → ELF 파일 안에 내용이 없다 (FileSiz 0) → QEMU -kernel 로더도, 우리 reset_init 도 안 건드린다. */
    .noinit (NOLOAD) : { . = ALIGN(4); _snoinit = .; KEEP(*(.noinit*)) . = ALIGN(4); _enoinit = .; } > NOINIT

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}

ASSERT(_ebss + _Min_Stack_Size <= _estack, "BM-M13: .data + .bss + stack do not fit in RAM")
ASSERT(_snoinit == ORIGIN(NOINIT), "BM-M13: .noinit must start at the fixed NOINIT address")
