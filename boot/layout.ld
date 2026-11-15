ENTRY(boot)
SECTIONS
{
    .boot 0x7c00 : { *(.boot) }
    .stage2 0x7e00 : { *(.stage2) *(.text*) *(.rodata*) }
    ASSERT(SIZEOF(.stage2) <= 53 * 512, "Stage 2 exceeds BIOS read")
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    ASSERT(ADDR(.bss) + SIZEOF(.bss) <= 0x80000, "Kernel must fit below BIOS reserved memory")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
