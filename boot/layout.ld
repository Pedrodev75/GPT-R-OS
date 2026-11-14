ENTRY(boot)
SECTIONS
{
    .boot 0x7c00 : { *(.boot) }
    .stage2 0x7e00 : { *(.stage2) *(.text*) *(.rodata*) }
    ASSERT(SIZEOF(.stage2) <= 16 * 512, "Stage 2 exceeds BIOS read")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
