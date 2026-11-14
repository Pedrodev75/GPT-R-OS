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

    # Read sectors 2 through 17 on the first floppy track.
    mov bx, 0x7e00
    mov ax, 0x0210
    mov cx, 2
    xor dh, dh
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

.org 510
.word 0xaa55

.section .stage2,"ax"
stage2:
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

.balign 8
gdt:
    .quad 0
    .quad 0x00af9a000000ffff
    .quad 0x00cf92000000ffff
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
