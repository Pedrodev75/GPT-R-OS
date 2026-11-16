ENTRY(boot)
PHDRS
{
    bootstrap PT_LOAD FLAGS(5);
    kernel PT_LOAD FLAGS(5);
    reserved PT_LOAD FLAGS(6);
}
SECTIONS
{
    .boot 0x7c00 : { *(.boot) } :bootstrap
    .stage2 0x7e00 : { *(.stage2) } :bootstrap
    ASSERT(SIZEOF(.boot) == 512, "Invalid boot sector")
    ASSERT(SIZEOF(.stage2) <= 16 * 512, "Bootstrap exceeds 16 sectors")

    .kernel 0x100000 : { *(.kernel_entry) *(.text*) *(.rodata*) *(.data*) } :kernel
    ASSERT(SIZEOF(.kernel) <= 0x60000, "Kernel exceeds BIOS staging buffer")
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss*) *(COMMON)
        __bss_end = .;
    } :reserved
    ASSERT(__bss_end <= 0x1f0000, "Kernel overlaps the 64 KiB boot stack")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
