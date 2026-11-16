.intel_syntax noprefix
.section .boot,"ax"
.code16
.equ LOADER_SECTORS, 16
.equ KERNEL_HEADER_LBA, 1 + LOADER_SECTORS
.equ KERNEL_BUFFER, 0x10000
.equ KERNEL_BASE, 0x100000
.equ KERNEL_LIMIT, 0x60000
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

    mov [boot_drive], dl
    test dl, 0x80
    jnz read_hd

    # LBA 1..16 are on the first track of a 1.44 MiB floppy.
    mov bx, 0x7e00
    mov cx, 2
    xor dh, dh
    mov ax, 0x0200 | LOADER_SECTORS
    int 0x13
    jc disk_error
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
    .word LOADER_SECTORS
    .word 0x7e00, 0
    .quad 1
boot_drive:
    .byte 0

# The HD image builder fills the partition table at bytes 446-509.
.org 446
.zero 64
.org 510
.word 0xaa55

.section .stage2,"ax"
.ifdef GPT_R_GRAPHICS
.equ VBE_MODE, 0x0118
.equ VBE_WIDTH, 1024
.equ VBE_HEIGHT, 768
.equ VBE_MAX_PITCH, 4096
.equ FRAMEBUFFER_LARGE_PAGES, 3
.endif
stage2:
    xor ax, ax
    mov ds, ax
    mov es, ax
    call load_kernel
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
    mov cx, VBE_MODE
    push ds
    int 0x10
    pop ds
    cmp ax, 0x004f
    jne vbe_error
    mov ax, word ptr [0x6400]
    and ax, 0x0091
    cmp ax, 0x0091
    jne vbe_error
    cmp word ptr [0x6412], VBE_WIDTH
    jne vbe_error
    cmp word ptr [0x6414], VBE_HEIGHT
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
    cmp word ptr [0x6410], VBE_WIDTH * 3
    jb vbe_error
    cmp word ptr [0x6410], VBE_MAX_PITCH
    ja vbe_error
    cmp dword ptr [0x6428], 0x40000000
    jb vbe_error

    mov ax, 0x4f02
    mov bx, VBE_MODE | 0x4000
    int 0x10
    cmp ax, 0x004f
    jne vbe_error
.endif
    cli
    # Enable A20 before copying above 1 MiB (PC-compatible fast gate).
    in al, 0x92
    and al, 0xfe
    or al, 2
    out 0x92, al
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
    # At most 3 MiB of pixels plus the physical base's offset in its first page.
    xor edx, edx
    mov di, 0x4000
    mov cx, FRAMEBUFFER_LARGE_PAGES
.map_framebuffer:
    mov dword ptr [di], eax
    mov dword ptr [di + 4], edx
    add eax, 0x200000
    adc edx, 0
    add di, 8
    loop .map_framebuffer
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
    .asciz "GPT-R OS: modo VBE 1024x768 indisponivel"
.endif

# One sector describes the payload independently of the bootstrap size.
load_kernel:
    mov word ptr [kernel_packet_lba], KERNEL_HEADER_LBA
    mov word ptr [kernel_packet_seg], 0x09e0
    mov word ptr [kernel_packet_count], 1
    call read_kernel_chunk
    cmp dword ptr [0x9e00], 0x4b525447  # "GTRK"
    jne disk_error
    mov eax, [0x9e04]
    test eax, eax
    jz disk_error
    cmp eax, KERNEL_LIMIT
    ja disk_error
    mov [kernel_bytes], eax
    add eax, 511
    shr eax, 9
    mov [kernel_sectors], ax
    inc word ptr [kernel_packet_lba]
    mov word ptr [kernel_packet_seg], KERNEL_BUFFER >> 4
.next_chunk:
    mov ax, [kernel_sectors]
    test byte ptr [boot_drive], 0x80
    jz .floppy_count
    cmp ax, 64
    jbe .read_count
    mov ax, 64
    jmp .read_count
.floppy_count:
    mov ax, 1
.read_count:
    mov [kernel_packet_count], ax
    push ax
    call read_kernel_chunk
    pop ax
    add [kernel_packet_lba], ax
    shl ax, 5
    add [kernel_packet_seg], ax
    shr ax, 5
    sub [kernel_sectors], ax
    jnz .next_chunk
    ret

read_kernel_chunk:
    push ds
    mov dl, [boot_drive]
    test dl, 0x80
    jz .read_floppy
    mov si, offset kernel_packet
    mov ax, 0x4200
    int 0x13
    jmp .after_read
.read_floppy:
    mov ax, [kernel_packet_lba]
    xor dx, dx
    mov cx, 18
    div cx
    mov cl, dl
    inc cl
    shr ax, 1
    mov ch, al
    setc dh
    mov dl, [boot_drive]
    mov ax, [kernel_packet_seg]
    mov es, ax
    xor bx, bx
    mov ax, 0x0201
    int 0x13
.after_read:
    pop ds
    jc disk_error
    xor ax, ax
    mov es, ax
    cld
    ret

.balign 4
kernel_packet:
    .byte 0x10, 0
kernel_packet_count:
    .word 1
    .word 0
kernel_packet_seg:
    .word 0
kernel_packet_lba:
    .quad 0
kernel_sectors:
    .word 0
kernel_bytes:
    .long 0

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
    mov esi, KERNEL_BUFFER
    mov edi, KERNEL_BASE
    mov ecx, [rip + kernel_bytes]
    cld
    rep movsb
    jmp KERNEL_BASE

.section .kernel_entry,"ax"
.code64
kernel_entry:
    mov esp, 0x200000
    lea rdi, [rip + __bss_start]
    lea rcx, [rip + __bss_end]
    sub rcx, rdi
    xor eax, eax
    rep stosb
    call kernel_main
2:  hlt
    jmp 2b

.section .note.GNU-stack,"",@progbits
