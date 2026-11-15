/* ENV: qemu */
/* BM-A03 부트로더 링커 스크립트 — qemu-system-arm -M mps2-an385 (Cortex-M3).
 * QEMU 보드: ZBT SSRAM1 0x00000000 4 MB (hw/arm/mps2.c) — 여기선 "플래시" 로 취급.
 * 부트로더 = 섹터 0 (16 KB) 하나. RAM 은 앱과 겹치지 않는 위쪽 4 KB 만 쓴다 (스택 꼭대기 0x2000E000).
 * 0x2000F800–0x20010000 (2 KB) = 메일박스 + 시험 계획: 아무 섹션도 두지 않는다 → 리셋을 건너 남는다.
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 16K
    RAM   (rwx) : ORIGIN = 0x2000D000, LENGTH = 4K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x2000E000 */
_Min_Stack_Size = 0x400;
ENTRY(Reset_Handler)
SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}
ASSERT(_ebss + _Min_Stack_Size <= _estack, "BM-A03 boot: RAM overflow")
ASSERT(_sidata + (_edata - _sdata) <= ORIGIN(FLASH) + LENGTH(FLASH), "BM-A03 boot: bootloader > 16 KB")
