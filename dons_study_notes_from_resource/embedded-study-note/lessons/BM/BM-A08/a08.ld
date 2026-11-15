/* ENV: qemu */
/* BM-A08 링커 스크립트 — qemu-system-arm -M mps2-an385 (Cortex-M3, PMSAv7 MPU 8 영역).
 * BM-B08 의 bm_b08.ld 축약판 + "NULL 트랩 자리" 규칙 하나:
 *   0x000-0x0FF 은 MPU 가 no-access 로 막는다 → 벡터 표(48 개 x 4 B = 192 B)만 여기에 두고,
 *   .text 는 0x100 부터 시작해야 한다 (아니면 0xC0-0xFF 의 코드가 IACCVIOL 로 죽는다).
 * FLASH 256 KB @0x00000000 (QEMU 에선 쓰기 가능한 SSRAM1), RAM 64 KB @0x20000000, MSP = RAM 꼭대기.
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x20010000 */
_Min_Stack_Size = 0x800;
_null_trap_size = 0x100;                 /* MPU NULL 영역 크기 (solution.c 의 NULL_TRAP_SIZE 와 같아야 함) */

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH

    .text ALIGN(0x100) : { _stext = .; *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > FLASH

    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH

    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}

ASSERT(_stext >= _null_trap_size, "BM-A08: .text must start above the MPU NULL-trap region")
ASSERT(SIZEOF(.isr_vector) <= _null_trap_size, "BM-A08: vector table larger than the NULL-trap region")
ASSERT(_ebss + _Min_Stack_Size <= _estack, "BM-A08: .data + .bss + stack do not fit in RAM")
