; Monte com: asm ola.asm feito.APP
entry inicio

inicio:
    mov eax, 1
    lea rdi, [rip + mensagem]
    mov esi, fim - mensagem
    int 0x80

    mov eax, 2
    mov edi, 0
    int 0x80
    ud2

mensagem: db "Ola do GPT-R OS!", 10
fim:
