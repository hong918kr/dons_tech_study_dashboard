/* TOOL-M02 demo linker script (concepts: TOOL-M01). STM32F4-like. */
ENTRY(Reset_Handler)

MEMORY
{
	FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
	RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_Min_Heap_Size  = 0x400;
_Min_Stack_Size = 0x800;
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
	.isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
	.text : { . = ALIGN(4); *(.text) *(.text*) . = ALIGN(4); _etext = .; } > FLASH
	.rodata : { . = ALIGN(4); *(.rodata) *(.rodata*) . = ALIGN(4); } > FLASH
	_sidata = LOADADDR(.data);
	.data : { . = ALIGN(4); _sdata = .; *(.data) *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
	.bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss) *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
	._user_heap_stack (NOLOAD) : {
		. = ALIGN(8);
		. = . + _Min_Heap_Size;
		. = . + _Min_Stack_Size;
		. = ALIGN(8);
	} > RAM
}
