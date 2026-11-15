.intel_syntax noprefix
.section .text
.code64
.global _start

_start:
    lea rdi, [rip + message]
    mov esi, message_end - message
    mov eax, 1
    int 0x80

loop:
    pause
    jmp loop

message:
    .ascii "Programa em loop. Ctrl+C para sair.\n"
message_end:

.section .note.GNU-stack,"",@progbits
