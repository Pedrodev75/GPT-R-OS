.intel_syntax noprefix
.section .text
.code64
.global _start

_start:
    mov eax, 1
    lea rdi, [rip + message]
    mov esi, message_end - message
    int 0x80

    mov eax, 2
    xor edi, edi
    int 0x80
    ud2

message:
    .ascii "Ola, GPT-R OS!\n"
message_end:

.section .note.GNU-stack,"",@progbits
