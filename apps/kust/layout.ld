ENTRY(_start)
SECTIONS {
    . = 0x80000000;
    .text : { *(.entry) *(.text .text.*) *(.rodata .rodata.*) }
    . = 0x80011000;
    .data : { *(.data .data.*) }
    .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
