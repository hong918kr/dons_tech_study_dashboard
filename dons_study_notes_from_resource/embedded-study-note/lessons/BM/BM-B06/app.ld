/* ENV: qemu — BM-B06 앱 링커 스크립트 (SLOT_A 에 실행되도록 링크): layout.ld 에서 파생 */
INCLUDE layout.ld
ENTRY(Reset_Handler)

SECTIONS
{
    .img_hdr ORIGIN(SLOT_A) : { KEEP(*(.img_hdr)) } > SLOT_A                    /* 헤더 = 슬롯 offset 0 */
    .isr_vector ORIGIN(SLOT_A) + __img_hdr_size : { KEEP(*(.isr_vector)) } > SLOT_A  /* VTOR 가 가리킬 곳 */
    .text   : { *(.text .text.*) } > SLOT_A
    .rodata : { *(.rodata .rodata.*) . = ALIGN(4); } > SLOT_A
    .data   : { _sdata = .; *(.data .data.*) . = ALIGN(4); _edata = .; } > RAM AT > SLOT_A
    _sidata = LOADADDR(.data);
    .bss (NOLOAD) : { _sbss = .; *(.bss .bss.* COMMON) . = ALIGN(4); _ebss = .; } > RAM
    .noinit (NOLOAD) : { KEEP(*(.noinit.shared)) } > SHARED                     /* 부트로더와 같은 주소 */
    __app_image_end  = LOADADDR(.data) + SIZEOF(.data);                         /* 마지막 LOAD 바이트 다음 */
    __app_image_size = __app_image_end - ORIGIN(SLOT_A);                        /* 헤더 img_size 로 들어감 */
}

ASSERT(ADDR(.img_hdr) == ORIGIN(SLOT_A),                     "app: .img_hdr section not at slot start")
ASSERT(g_img_hdr == ORIGIN(SLOT_A),                          "app: g_img_hdr is not the first word of the slot")
ASSERT(SIZEOF(.img_hdr) <= __img_hdr_size,                   "app: image header larger than reserved 0x200")
ASSERT(ADDR(.isr_vector) == ORIGIN(SLOT_A) + __img_hdr_size, "app: vector table not at slot + hdr_size")
ASSERT(ADDR(.isr_vector) % 512 == 0,                         "app: vector table breaks VTOR alignment")
ASSERT(g_boot_share == ORIGIN(SHARED),                       "app: g_boot_share not at ORIGIN(SHARED)")
ASSERT(SIZEOF(.noinit) == 32,                                "app: boot_share_t size changed (contract with boot)")
ASSERT(__app_image_end <= ORIGIN(SLOT_A) + LENGTH(SLOT_A),   "app: image larger than SLOT_A")
