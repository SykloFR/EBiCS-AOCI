MEMORY { RAM (rwx) : ORIGIN = 0x20002000, LENGTH = 0x8000 }
ENTRY(test_start)
SECTIONS
{
  .text : { KEEP(*(.entry)) *(.text*) *(.rodata*) . = ALIGN(4); } > RAM
  .data : { *(.data*) . = ALIGN(4); } > RAM
  .bss (NOLOAD) : { *(.bss*) *(COMMON) . = ALIGN(4); } > RAM
}
