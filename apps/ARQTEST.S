.intel_syntax noprefix
.section .text
.code64
.global _start

.equ SYS_OUTPUT, 1
.equ SYS_EXIT, 2
.equ SYS_ALLOC, 9
.equ SYS_FREE, 10
.equ SYS_OPEN, 11
.equ SYS_READ, 12
.equ SYS_WRITE, 13
.equ SYS_CLOSE, 14

_start:
    mov eax, SYS_ALLOC
    mov edi, 8192
    int 0x80
    test rax, rax
    js failure
    mov r12, rax
    # Use a buffer that spans two pages of the allocation.
    lea r15, [r12 + 4090]

    mov eax, SYS_OPEN
    lea rdi, [rip + input]
    xor esi, esi
    int 0x80
    test rax, rax
    js failure
    mov r13, rax

    mov eax, SYS_OPEN
    lea rdi, [rip + output]
    mov esi, 1
    int 0x80
    test rax, rax
    js failure
    mov r14, rax

copy:
    mov eax, SYS_READ
    mov rdi, r13
    mov rsi, r15
    mov edx, 1024
    int 0x80
    test rax, rax
    js failure
    jz done
    mov rdx, rax
    mov eax, SYS_WRITE
    mov rdi, r14
    mov rsi, r15
    int 0x80
    cmp rax, rdx
    jne failure
    jmp copy

done:
    mov eax, SYS_CLOSE
    mov rdi, r13
    int 0x80
    test rax, rax
    js failure
    mov eax, SYS_CLOSE
    mov rdi, r14
    int 0x80
    test rax, rax
    js failure

    mov eax, SYS_FREE
    mov rdi, r12
    int 0x80
    test rax, rax
    js failure
    mov eax, SYS_OUTPUT
    lea rdi, [rip + ok]
    mov esi, ok_end - ok
    int 0x80
    mov eax, SYS_EXIT
    xor edi, edi
    int 0x80
    ud2

failure:
    mov eax, SYS_OUTPUT
    lea rdi, [rip + error]
    mov esi, error_end - error
    int 0x80
    # A fault discards any still-open output so a partial copy is never published.
    ud2

input:
    .asciz "/HOME/DOCS/entrada.txt"
output:
    .asciz "/HOME/DOCS/saida.txt"
ok:
    .ascii "Copia salva em /HOME/DOCS/saida.txt\n"
ok_end:
error:
    .ascii "Falha ao copiar o arquivo.\n"
error_end:
.section .note.GNU-stack,"",@progbits
