.text
.section .entry,"ax"
.globl _start
_start:
    mov (%rsp), %rdi
    lea 8(%rsp), %rsi
    call kust_main
    mov %eax, %edi
    mov $2, %eax
    int $0x80
    ud2

.text
.globl memcpy
memcpy:
    mov %rdi, %rax
    mov %rdx, %rcx
    rep movsb
    ret

.globl memset
memset:
    mov %rdi, %r8
    mov %esi, %eax
    mov %rdx, %rcx
    rep stosb
    mov %r8, %rax
    ret
