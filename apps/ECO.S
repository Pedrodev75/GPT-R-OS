.intel_syntax noprefix
.section .text
.code64
.global _start

_start:
    mov r12, [rsp]
    lea r13, [rsp + 16]             # argv[1], after argc and argv[0].
    dec r12
    jz newline

next_argument:
    mov rdi, [r13]
    xor esi, esi
count_bytes:
    cmp byte ptr [rdi + rsi], 0
    je print_argument
    inc rsi
    jmp count_bytes

print_argument:
    mov eax, 1
    int 0x80
    add r13, 8
    dec r12
    jz newline

    lea rdi, [rip + space]
    mov esi, 1
    mov eax, 1
    int 0x80
    jmp next_argument

newline:
    lea rdi, [rip + line_break]
    mov esi, 1
    mov eax, 1
    int 0x80

    xor edi, edi
    mov eax, 2
    int 0x80
    ud2

space:
    .ascii " "
line_break:
    .byte 10

.section .note.GNU-stack,"",@progbits
