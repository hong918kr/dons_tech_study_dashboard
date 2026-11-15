/* ENV: qemu */
/* BM-A11 링커 스크립트 — qemu-system-arm -M mps2-an385 (Cortex-M3). BM-B08 / BM-M13 링커 스크립트 축약판 + .img_hdr.
 * FLASH 256 KB @0x00000000 (QEMU 에선 SSRAM1), RAM 63 KB @0x20000000, NOINIT 1 KB @0x2000FC00 (고정 주소).
 *
 *   0x00000000  .isr_vector  ─┐
 *               .text/.rodata  │ payload = [0, _img_hdr)  ← CRC-32 범위 (mkimg.py 가 계산)
 *               .data 초기값   ─┘ (LMA)
 *   _img_hdr    .img_hdr      64 B 이미지 헤더 (BM-A04 헤더 축약판; 빌드 뒤 mkimg.py 가 채움)
 *
 * 스택은 0x2000FC00 에서 아래로 자란다 → 넘쳐도 위쪽 .noinit (리셋 기록·덤프) 은 건드리지 않는다.
 */
MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    RAM    (rwx) : ORIGIN = 0x20000000, LENGTH = 63K
    NOINIT (rw)  : ORIGIN = 0x2000FC00, LENGTH = 1K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x2000FC00 */
_Min_Stack_Size = 0x800;                 /* 2 KB: 링크 시 남아 있어야 하는 최소 스택 */

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH

    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH

    _sidata = LOADADDR(.data);
    .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH

    /* .data 의 LMA 바로 뒤 (FLASH 영역의 현재 위치). 헤더는 payload 에 포함되지 않는다 → 자기 참조 없음 */
    .img_hdr : { . = ALIGN(4); _img_hdr = .; KEEP(*(.img_hdr)) . = ALIGN(4); } > FLASH

    .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM

    .noinit (NOLOAD) : { . = ALIGN(4); _snoinit = .; KEEP(*(.noinit*)) . = ALIGN(4); _enoinit = .; } > NOINIT

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) }
}

ASSERT(_ebss + _Min_Stack_Size <= _estack, "BM-A11: .data + .bss + stack do not fit in RAM")
ASSERT(_snoinit == ORIGIN(NOINIT), "BM-A11: .noinit must start at the fixed NOINIT address")
ASSERT(_img_hdr == LOADADDR(.data) + SIZEOF(.data), "BM-A11: header must follow the last payload byte")
ASSERT(SIZEOF(.img_hdr) == 64, "BM-A11: image header must be 64 bytes")
