; asm -f v1.1 ASMTEST.ASM ASMTEST.APP
; run ASMTEST.APP FONTE.ASM SAIDA.APP [v1|v1.1]
.section .text
entry start
start:
    mov rax, [rsp]
    cmp eax, 3
    jl usage
    cmp eax, 4
    jg usage
    mov edx, 257
    jne assemble
    mov rbx, [rsp + 32]
    movzx eax, byte [rbx]
    cmp eax, 118
    jne usage
    movzx eax, byte [rbx + 1]
    cmp eax, 49
    jne usage
    movzx eax, byte [rbx + 2]
    test eax, eax
    jnz extended
    mov edx, 1
    jmp assemble
extended:
    cmp eax, 46
    jne usage
    movzx eax, byte [rbx + 3]
    cmp eax, 49
    jne usage
    movzx eax, byte [rbx + 4]
    test eax, eax
    jnz usage
assemble:
    mov eax, 15
    mov rdi, [rsp + 16]
    mov rsi, [rsp + 24]
    int 0x80
    mov ebp, edx
    test eax, eax
    jz success
    cmp eax, -2
    je missing
    cmp eax, -3
    je denied
    cmp eax, -4
    je reading
    cmp eax, -5
    je memory
    cmp eax, -6
    je limit
    cmp eax, -8
    je syntax
    cmp eax, -9
    je writing
    cmp eax, -10
    je uncertain
    lea rdi, [rip + invalid_text]
    mov esi, invalid_end - invalid_text
    jmp error
missing:
    lea rdi, [rip + missing_text]
    mov esi, missing_end - missing_text
    jmp error
denied:
    lea rdi, [rip + denied_text]
    mov esi, denied_end - denied_text
    jmp error
reading:
    lea rdi, [rip + read_text]
    mov esi, read_end - read_text
    jmp error
memory:
    lea rdi, [rip + memory_text]
    mov esi, memory_end - memory_text
    jmp error
limit:
    lea rdi, [rip + limit_text]
    mov esi, limit_end - limit_text
    jmp error
syntax:
    lea rdi, [rip + syntax_text]
    mov esi, syntax_end - syntax_text
    jmp error
writing:
    lea rdi, [rip + write_text]
    mov esi, write_end - write_text
    jmp error
uncertain:
    lea rdi, [rip + uncertain_text]
    mov esi, uncertain_end - uncertain_text
    jmp error
usage:
    xor ebp, ebp
    lea rdi, [rip + usage_text]
    mov esi, usage_end - usage_text
error:
    mov eax, 1
    int 0x80
    test ebp, ebp
    jz failure
    lea rdi, [rip + line_text]
    mov esi, line_end - line_text
    mov eax, 1
    int 0x80
    mov eax, ebp
    sub rsp, 48
    mov rbx, rsp
    mov ecx, 10
digits:
    xor edx, edx
    div ecx
    add edx, 48
    mov [rbx], edx
    add rbx, 4
    test eax, eax
    jnz digits
print_digit:
    sub rbx, 4
    mov rdi, rbx
    mov esi, 1
    mov eax, 1
    int 0x80
    cmp rbx, rsp
    jne print_digit
    add rsp, 48
    lea rdi, [rip + newline]
    mov esi, 1
    mov eax, 1
    int 0x80
failure:
    mov edi, 1
    jmp finish
success:
    lea rdi, [rip + ok_text]
    mov esi, ok_end - ok_text
    mov eax, 1
    int 0x80
    xor edi, edi
finish:
    mov eax, 2
    int 0x80

.section .data
ok_text: db "ASMTEST: APP montado e salvo", 10
ok_end:
usage_text: db "Uso: run ASMTEST.APP FONTE.ASM SAIDA.APP [v1|v1.1]", 10
usage_end:
invalid_text: db "ASMTEST: argumentos invalidos", 10
invalid_end:
missing_text: db "ASMTEST: fonte nao encontrado", 10
missing_end:
denied_text: db "ASMTEST: entrada protegida ou em uso", 10
denied_end:
read_text: db "ASMTEST: falha de leitura", 10
read_end:
memory_text: db "ASMTEST: memoria insuficiente", 10
memory_end:
limit_text: db "ASMTEST: limite excedido ou disco cheio", 10
limit_end:
syntax_text: db "ASMTEST: erro de sintaxe", 10
syntax_end:
write_text: db "ASMTEST: falha de gravacao", 10
write_end:
uncertain_text: db "ASMTEST: falha de disco, estado do destino incerto", 10
uncertain_end:
line_text: db "Linha "
line_end:
newline: db 10
