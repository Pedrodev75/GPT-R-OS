.intel_syntax noprefix
.section .text
.code64
.global _start

_start:
    sub rsp, 160
    lea rdi, [rip + question]
    mov esi, question_end - question
    mov eax, 1
    int 0x80

    mov rdi, rsp
    mov esi, 160
    mov eax, 3
    int 0x80
    test rax, rax
    js read_failed
    mov r12, rax

    lea rdi, [rip + greeting]
    mov esi, greeting_end - greeting
    mov eax, 1
    int 0x80

    mov rdi, rsp
    mov rsi, r12
    mov eax, 1
    int 0x80

    lea rdi, [rip + ending]
    mov esi, ending_end - ending
    mov eax, 1
    int 0x80

    xor edi, edi
    jmp exit

read_failed:
    mov edi, 1

exit:
    mov eax, 2
    int 0x80
    ud2

question:
    .ascii "Qual e o seu nome?\n"
question_end:
greeting:
    .ascii "Ola, "
greeting_end:
ending:
    .ascii "!\n"
ending_end:

.section .note.GNU-stack,"",@progbits
