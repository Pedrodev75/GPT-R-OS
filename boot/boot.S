.intel_syntax noprefix
.section .boot,"ax"
.code16
.global boot
boot:
    ljmp 0, start
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    cld
    sti
    push dx
    mov ax, 3
    int 0x10
    pop dx

    test dl, 0x80
    jnz read_hd

    # Split floppy reads at track boundaries and at physical 0x10000.
    .macro read_floppy segment, offset, count, track, head
        mov ax, \segment
        mov es, ax
        mov bx, \offset
        mov ax, 0x0200 | \count
        mov cx, \track
        push dx
        mov dh, \head
        int 0x13
        pop dx
        jc disk_error
    .endm
    read_floppy 0,      0x7e00, 17, 0x0002, 0
    read_floppy 0,      0xa000, 18, 0x0001, 1
    read_floppy 0,      0xc400, 18, 0x0101, 0
    read_floppy 0,      0xe800, 12, 0x0101, 1
    read_floppy 0x1000, 0x0000,  6, 0x010d, 1
    read_floppy 0x1000, 0x0c00, 18, 0x0201, 0
    jmp stage2
read_hd:
    # Keep the BIOS-selected drive number across the EDD capability check.
    push dx
    push ds
    mov ax, 0x4100
    mov bx, 0x55aa
    int 0x13
    pop ds
    pop dx
    jc disk_error
    cmp bx, 0xaa55
    jne disk_error
    test cx, 1
    jz disk_error

    mov si, offset disk_packet
    push ds
    push dx
    mov ax, 0x4200
    int 0x13
    pop dx
    pop ds
    jc disk_error
    mov si, offset disk_packet_high
    mov ax, 0x4200
    int 0x13
    jc disk_error
    jmp stage2

disk_error:
    mov ax, 0x0e45
    xor bx, bx
    int 0x10
    cli
1:  hlt
    jmp 1b

.balign 4
disk_packet:
    .byte 0x10, 0
    .word 65
    .word 0x7e00, 0
    .quad 1
disk_packet_high:
    .byte 0x10, 0
    .word 24
    .word 0, 0x1000
    .quad 66

# The HD image builder fills the partition table at bytes 446-509.
.org 446
.zero 64
.org 510
.word 0xaa55

.section .stage2,"ax"
stage2:
    call collect_e820
.ifdef GPT_R_GRAPHICS
    # Copy 128 BIOS ROM 8x8 glyphs above the E820 handoff and below the stack.
    mov ax, 0x1130
    mov bh, 3
    int 0x10
    push es
    pop ds
    mov si, bp
    xor ax, ax
    mov es, ax
    mov di, 0x6000
    mov cx, 1024
    cld
    rep movsb
    mov ds, ax

    # VBE mode info at 0x6400, after the 8x8 font at 0x6000.
    mov di, 0x6400
    xor ax, ax
    mov cx, 128
    rep stosw
    mov di, 0x6400
    mov ax, 0x4f01
    mov cx, 0x0112
    push ds
    int 0x10
    pop ds
    cmp ax, 0x004f
    jne vbe_error
    mov ax, word ptr [0x6400]
    and ax, 0x0091
    cmp ax, 0x0091
    jne vbe_error
    cmp word ptr [0x6412], 640
    jne vbe_error
    cmp word ptr [0x6414], 480
    jne vbe_error
    cmp byte ptr [0x6419], 24
    jne vbe_error
    cmp byte ptr [0x641b], 6
    jne vbe_error

    # Prefer the linear-layout fields when returned by a VBE 3 BIOS.
    mov si, 0x641f
    mov ax, word ptr [0x6432]
    test ax, ax
    jz .legacy_pitch
    mov word ptr [0x6410], ax
    cmp byte ptr [0x6436], 0
    je .legacy_pitch
    mov si, 0x6436
.legacy_pitch:
    cmp word ptr [si], 0x1008
    jne vbe_error
    cmp word ptr [si + 2], 0x0808
    jne vbe_error
    cmp word ptr [si + 4], 0x0008
    jne vbe_error
    cmp word ptr [0x6410], 1920
    jb vbe_error
    cmp word ptr [0x6410], 4096
    ja vbe_error
    cmp dword ptr [0x6428], 0x40000000
    jb vbe_error

    mov ax, 0x4f02
    mov bx, 0x4112
    int 0x10
    cmp ax, 0x004f
    jne vbe_error
.endif
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    cld

    # Three page tables at 0x1000, 0x2000 and 0x3000.
    mov di, 0x1000
    mov cx, 0x1800
    rep stosw
    mov dword ptr [0x1000], 0x2003
    mov dword ptr [0x2000], 0x3003
    mov dword ptr [0x3000], 0x0083  # Identity map the first 2 MiB.

.ifdef GPT_R_GRAPHICS
    # One more page directory maps the framebuffer window at virtual 4 GiB.
    # The allocator continues to manage only memory below 1 GiB.
    mov di, 0x4000
    mov cx, 0x0800
    rep stosw
    mov dword ptr [0x2020], 0x4003
    mov eax, dword ptr [0x6428]
    and eax, 0xffe00000
    or eax, 0x9b  # Present, writable, uncached 2 MiB page.
    mov dword ptr [0x4000], eax
    xor edx, edx
    add eax, 0x200000
    adc edx, 0
    mov dword ptr [0x4008], eax
    mov dword ptr [0x400c], edx
.endif

    lgdt [gdtr]
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    mov eax, 0x1000
    mov cr3, eax

    mov ecx, 0xc0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax
    ljmp 0x08, long_mode

.ifdef GPT_R_GRAPHICS
vbe_error:
    mov ax, 3
    int 0x10
    xor ax, ax
    mov ds, ax
    mov si, offset vbe_error_message
    cld
.vbe_print:
    lodsb
    test al, al
    jz .vbe_stop
    mov ah, 0x0e
    xor bx, bx
    int 0x10
    jmp .vbe_print
.vbe_stop:
    cli
    hlt
    jmp .vbe_stop
vbe_error_message:
    .asciz "GPT-R OS: modo VBE 640x480 indisponivel"
.endif

# Handoff at physical 0x5000: count (u16), status (u16), then
# up to 128 entries of 24 bytes. Entries end before 0x5c04.
collect_e820:
    xor ax, ax
    mov ds, ax
    mov word ptr [0x5000], ax
    mov word ptr [0x5002], ax
    xor ebx, ebx
    mov di, 0x5004
.e820_next:
    xor ax, ax
    mov es, ax
    mov dword ptr es:[di + 20], 1
    mov eax, 0xe820
    mov edx, 0x534d4150
    mov ecx, 24
    push ds
    push di
    int 0x15
    pop di
    pop ds
    jc .e820_incomplete
    cmp eax, 0x534d4150
    jne .e820_incomplete
    cmp ecx, 20
    jb .e820_incomplete
    # Older BIOSes return only the 20-byte descriptor.
    cmp ecx, 24
    jb .e820_length
    test byte ptr [di + 20], 1
    jz .e820_continue
.e820_length:
    mov eax, [di + 8]
    or eax, [di + 12]
    jz .e820_continue
    inc word ptr [0x5000]
    add di, 24
.e820_continue:
    test ebx, ebx
    jz .e820_done
    cmp word ptr [0x5000], 128
    je .e820_incomplete
    jmp .e820_next
.e820_done:
    mov word ptr [0x5002], 1
    ret
.e820_incomplete:
    cmp word ptr [0x5000], 0
    je .e820_return
    mov word ptr [0x5002], 2
.e820_return:
    ret

.balign 8
gdt:
    .quad 0
    .quad 0x00af9a000000ffff
    .quad 0x00cf92000000ffff
    .quad 0x00cff2000000ffff   # Ring 3 data (0x1b).
    .quad 0x00affa000000ffff   # Ring 3 code (0x23).
.global app_tss_descriptor
app_tss_descriptor:
    .quad 0, 0                # TSS descriptor (0x28), filled by the kernel.
gdtr:
    .word gdtr - gdt - 1
    .long gdt

.code64
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x7c00
    call kernel_main
2:  hlt
    jmp 2b

.section .note.GNU-stack,"",@progbits
