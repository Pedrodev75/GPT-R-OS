.intel_syntax noprefix
.section .text
.code64
.global _start

.equ SYS_EXIT, 2
.equ GUI_BEGIN, 4
.equ GUI_RECT, 5
.equ GUI_TEXT, 6
.equ GUI_PRESENT, 7
.equ GUI_WAIT, 8
.equ MAX_VALUE, 999999999
.equ KEY_X, 16
.equ KEY_Y, 72
.equ KEY_W, 100
.equ KEY_H, 39
.equ STEP_X, 105
.equ STEP_Y, 46

# r12: display, r13: left operand, r14b: operator, r15d: state.
_start:
    sub rsp, 40
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
.render:
    call paint
.wait:
    mov eax, GUI_WAIT
    int 0x80
    test rax, rax
    js .leave
    mov r11d, eax
    movzx eax, ax
    sub eax, KEY_X
    jb .wait
    xor edx, edx
    mov ecx, STEP_X
    div ecx
    cmp eax, 4
    jae .wait
    cmp edx, KEY_W
    jae .wait
    mov r10d, eax

    # Extract the y component from the saved event.
    mov eax, r11d
    shr eax, 16
    sub eax, KEY_Y
    jb .wait
    xor edx, edx
    mov ecx, STEP_Y
    div ecx
    cmp eax, 4
    jae .wait
    cmp edx, KEY_H
    jae .wait
    lea eax, [r10 + rax*4]
    lea rdx, [rip + keys]
    movzx ebx, byte ptr [rdx + rax]
    call enter_key
    jmp .render
.leave:
    mov edi, 1
    mov eax, SYS_EXIT
    int 0x80
    ud2

enter_key:
    cmp bl, 'C'
    je .clear
    cmp bl, '0'
    jb .operator
    cmp bl, '9'
    ja .operator
    cmp r15d, 1
    je .append
    cmp r15d, 2
    jb .start_digit
    xor r14d, r14d
.start_digit:
    xor r12d, r12d
.append:
    imul rax, r12, 10
    movzx edx, bl
    sub edx, '0'
    add rax, rdx
    cmp rax, MAX_VALUE
    ja .overflow
    mov r12, rax
    mov r15d, 1
    ret
.operator:
    cmp r15d, 2
    jae .return
    cmp r15d, 1
    jne .set
    test r14b, r14b
    je .set
    call calculate
    cmp r15d, 2
    jae .return
.set:
    cmp bl, '='
    je .equal
    mov r13, r12
    mov r14b, bl
    xor r15d, r15d
    ret
.equal:
    xor r14d, r14d
    xor r15d, r15d
.return:
    ret
.clear:
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
    ret
.overflow:
    mov r15d, 4
    xor r14d, r14d
    ret

calculate:
    mov rax, r13
    cmp r14b, '+'
    je .sum
    cmp r14b, '-'
    je .subtract
    cmp r14b, 'x'
    je .multiply
    test r12, r12
    je .divide_zero
    cqo
    idiv r12
    jmp .range
.sum:
    add rax, r12
    jmp .range
.subtract:
    sub rax, r12
    jmp .range
.multiply:
    imul rax, r12
.range:
    cmp rax, MAX_VALUE
    jg .overflow
    cmp rax, -MAX_VALUE
    jl .overflow
    mov r12, rax
    ret
.divide_zero:
    mov r15d, 2
    xor r14d, r14d
    ret

paint:
    mov edi, 0x0d192b
    mov eax, GUI_BEGIN
    int 0x80
    mov edi, 16
    mov esi, 12
    mov edx, 415
    mov ecx, 46
    mov r8d, 0x142941
    call rectangle
    mov edi, 16
    mov esi, 56
    mov edx, 415
    mov ecx, 2
    mov r8d, 0x386583
    call rectangle

    # Write decimal digits backwards into the app's own stack.
    lea r9, [rsp + 36]
    mov rax, r12
    test rax, rax
    jns .digits
    neg rax
.digits:
    mov ecx, 10
.digit:
    xor edx, edx
    div rcx
    add dl, '0'
    dec r9
    mov byte ptr [r9], dl
    test rax, rax
    jne .digit
    test r12, r12
    jns .number
    dec r9
    mov byte ptr [r9], '-'
.number:
    mov rdx, r9
    lea rcx, [rsp + 36]
    sub rcx, rdx
    cmp r15d, 2
    jne .check_limit
    lea rdx, [rip + zero_error]
    mov ecx, zero_error_end - zero_error
    jmp .display
.check_limit:
    cmp r15d, 4
    jne .display
    lea rdx, [rip + size_error]
    mov ecx, size_error_end - size_error
.display:
    mov edi, 415
    mov eax, ecx
    shl eax, 4
    sub edi, eax
    mov esi, 24
    mov r8d, 0xe5f2fa
    call label
    test r14b, r14b
    je .buttons
    mov byte ptr [rsp + 8], r14b
    lea rdx, [rsp + 8]
    mov edi, 32
    mov esi, 24
    mov ecx, 1
    mov r8d, 0x65daf1
    call label

.buttons:
    xor r10d, r10d
.draw_button:
    mov eax, r10d
    and eax, 3
    imul r11d, eax, STEP_X
    add r11d, KEY_X
    mov eax, r10d
    shr eax, 2
    imul r9d, eax, STEP_Y
    add r9d, KEY_Y
    mov edi, r11d
    mov esi, r9d
    mov edx, KEY_W
    mov ecx, KEY_H
    mov r8d, 0x1b304a
    mov eax, r10d
    and eax, 3
    cmp eax, 3
    jne .check_clear
    mov r8d, 0x254760
.check_clear:
    cmp r10d, 12
    jne .check_equal
    mov r8d, 0x593d3b
.check_equal:
    cmp r10d, 14
    jne .background
    mov r8d, 0x23657b
.background:
    call rectangle
    lea edi, [r11 + 42]
    lea esi, [r9 + 9]
    lea rdx, [rip + keys]
    add rdx, r10
    mov ecx, 1
    mov r8d, 0xe5f2fa
    call label
    inc r10d
    cmp r10d, 16
    jb .draw_button
    mov eax, GUI_PRESENT
    int 0x80
    ret

rectangle:
    mov eax, GUI_RECT
    int 0x80
    ret

label:
    mov eax, GUI_TEXT
    int 0x80
    ret

keys:
    .ascii "789/456x123-C0=+"
zero_error:
    .ascii "Divisao por zero"
zero_error_end:
size_error:
    .ascii "Fora do limite"
size_error_end:
.section .note.GNU-stack,"",@progbits
