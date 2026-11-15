/* ENV: qemu — BM-B06 부트로더 링커 스크립트: layout.ld 에서 파생 */
INCLUDE layout.ld
ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector ORIGIN(BOOT) : { KEEP(*(.isr_vector)) } > BOOT     /* 리셋 시 CPU 가 0x08000000 을 읽는다 */
    .text   : { *(.text .text.*) } > BOOT
    .rodata : { *(.rodata .rodata.*) . = ALIGN(4); } > BOOT
    .data   : { _sdata = .; *(.data .data.*) . = ALIGN(4); _edata = .; } > RAM AT > BOOT
    _sidata = LOADADDR(.data);
    .bss (NOLOAD) : { _sbss = .; *(.bss .bss.* COMMON) . = ALIGN(4); _ebss = .; } > RAM
    .noinit (NOLOAD) : { KEEP(*(.noinit.shared)) } > SHARED       /* 공유 부트 사유 — 앱과 같은 주소 */
    __boot_end = LOADADDR(.data) + SIZEOF(.data);
}

ASSERT(ADDR(.isr_vector) == ORIGIN(BOOT),        "boot: vector table must be at the reset address")
ASSERT(g_boot_share == ORIGIN(SHARED),           "boot: g_boot_share not at ORIGIN(SHARED)")
ASSERT(SIZEOF(.noinit) == 32,                    "boot: boot_share_t size changed (contract with app)")
ASSERT(__boot_end <= ORIGIN(BOOT) + LENGTH(BOOT), "boot: image larger than BOOT region")
