/* ENV: qemu */
/* BM-A03 앱 링커 스크립트 — 앱 슬롯 0x00004000 (240 KB). 부트로더와 다른 건 ORIGIN 하나뿐이지만
 * 그 하나가 벡터 테이블 주소(→ VTOR), 모든 함수 주소(→ 리셋 벡터), .data 의 LMA 를 다 바꾼다.
 * RAM 은 0x20000000 부터 62 KB 전부 앱 것 (부트로더 RAM 은 점프 뒤 죽은 메모리) — 스택 꼭대기 0x2000F800.
 */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00004000, LENGTH = 240K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 62K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);     /* 0x2000F800 = 메일박스 바로 아래 */
_Min_Stack_Size = 0x800;
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
ASSERT(ADDR(.isr_vector) == 0x00004000, "BM-A03 app: vector table must be at the slot start")
ASSERT((ADDR(.isr_vector) & 0xFF) == 0, "BM-A03 app: VTOR needs 256 B alignment for 48 vectors")
ASSERT(_ebss + _Min_Stack_Size <= _estack, "BM-A03 app: RAM overflow")
