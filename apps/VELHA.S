.intel_syntax noprefix
.section .text
.code64
.global _start

.equ EXIT, 2
.equ GUI_BEGIN, 4
.equ GUI_RECT, 5
.equ GUI_TEXT, 6
.equ GUI_PRESENT, 7
.equ GUI_WAIT, 8
.equ BOARD_X, 25
.equ BOARD_Y, 54
.equ STEP, 62
.equ CELL_SIZE, 55
.equ UI_TEXT, 0xe4edf8
.equ X_COLOR, 0x65daf1
.equ O_COLOR, 0xffcf88

.macro rect x, y, w, h, color
    mov edi, \x
    mov esi, \y
    mov edx, \w
    mov ecx, \h
    mov r8d, \color
    mov eax, GUI_RECT
    int 0x80
.endm

.macro text x, y, label, color
    mov edi, \x
    mov esi, \y
    lea rdx, [rip + \label]
    lea rcx, [rip + \label\()_end]
    sub rcx, rdx
    mov r8d, \color
    mov eax, GUI_TEXT
    int 0x80
.endm

# r12 points to nine cells in the stack, r13b is the current player,
# r14b holds the result, and r15d counts completed moves.
_start:
    sub rsp, 16
    mov r12, rsp
.reset:
    mov qword ptr [r12], 0
    mov byte ptr [r12 + 8], 0
    mov r13d, 'X'
    xor r14d, r14d
    xor r15d, r15d
.render:
    call paint
.wait:
    mov eax, GUI_WAIT
    int 0x80
    test rax, rax
    js .exit
    movzx r9d, ax
    shr eax, 16
    mov r10d, eax
    cmp r9d, 238
    jb .board
    cmp r9d, 419
    jae .wait
    cmp r10d, 169
    jb .wait
    cmp r10d, 206
    jb .reset
    cmp r10d, 218
    jb .wait
    cmp r10d, 251
    jb .exit
    jmp .wait
.board:
    test r14d, r14d
    jnz .wait
    mov eax, r9d
    sub eax, BOARD_X
    jb .wait
    xor edx, edx
    mov ecx, STEP
    div ecx
    cmp eax, 3
    jae .wait
    cmp edx, CELL_SIZE
    jae .wait
    mov r9d, eax
    mov eax, r10d
    sub eax, BOARD_Y
    jb .wait
    xor edx, edx
    div ecx
    cmp eax, 3
    jae .wait
    cmp edx, CELL_SIZE
    jae .wait
    lea eax, [rax + rax*2]
    add eax, r9d
    cmp byte ptr [r12 + rax], 0
    jne .wait
    mov byte ptr [r12 + rax], r13b
    inc r15d
    lea rsi, [rip + triples]
    mov ecx, 8
.wins:
    movzx eax, byte ptr [rsi]
    cmp byte ptr [r12 + rax], r13b
    jne .next
    movzx eax, byte ptr [rsi + 1]
    cmp byte ptr [r12 + rax], r13b
    jne .next
    movzx eax, byte ptr [rsi + 2]
    cmp byte ptr [r12 + rax], r13b
    jne .next
    mov r14d, r13d
    jmp .render
.next:
    add rsi, 3
    dec ecx
    jnz .wins
    cmp r15d, 9
    je .draw
    xor r13d, 'X' ^ 'O'
    jmp .render
.draw:
    mov r14d, 'D'
    jmp .render
.exit:
    xor edi, edi
    mov eax, EXIT
    int 0x80
    ud2

paint:
    mov edi, 0x0d192b
    mov eax, GUI_BEGIN
    int 0x80
    test rax, rax
    js .exit
    text 25, 12, title, UI_TEXT
    rect 229, 53, 190, 100, 0x142941
    test r14d, r14d
    jz .turn
    cmp r14b, 'D'
    je .tie
    cmp r14b, 'X'
    je .xwon
    text 241, 76, o_won, O_COLOR
    jmp .controls
.xwon:
    text 241, 76, x_won, X_COLOR
    jmp .controls
.tie:
    text 241, 76, tie, UI_TEXT
    jmp .controls
.turn:
    cmp r13b, 'X'
    je .xturn
    text 241, 76, o_turn, O_COLOR
    jmp .controls
.xturn:
    text 241, 76, x_turn, X_COLOR
.controls:
    rect 238, 169, 181, 37, 0x254765
    rect 238, 218, 181, 33, 0x1b3049
    text 249, 175, restart, UI_TEXT
    text 249, 223, quit, UI_TEXT
    xor ebx, ebx
.cell:
    mov eax, ebx
    xor edx, edx
    mov ecx, 3
    div ecx
    imul r10d, edx, STEP
    add r10d, BOARD_X
    imul r11d, eax, STEP
    add r11d, BOARD_Y
    rect r10d, r11d, CELL_SIZE, CELL_SIZE, 0x1b3049
    cmp byte ptr [r12 + rbx], 'X'
    je .mark_x
    cmp byte ptr [r12 + rbx], 'O'
    jne .cell_done
    add r10d, 13
    add r11d, 13
    rect r10d, r11d, 29, 5, O_COLOR
    rect r10d, r11d, 5, 29, O_COLOR
    add r11d, 24
    rect r10d, r11d, 29, 5, O_COLOR
    sub r11d, 24
    add r10d, 24
    rect r10d, r11d, 5, 29, O_COLOR
    jmp .cell_done
.mark_x:
    mov r9d, 0
.stripe:
    lea edi, [r10 + r9*4 + 14]
    lea esi, [r11 + r9*4 + 14]
    rect edi, esi, 7, 7, X_COLOR
    lea esi, [r11 + r9*4 + 14]
    mov edi, r10d
    lea edx, [r9*4]
    sub edi, edx
    add edi, 34
    rect edi, esi, 7, 7, X_COLOR
    inc r9d
    cmp r9d, 6
    jb .stripe
.cell_done:
    inc ebx
    cmp ebx, 9
    jb .cell
    mov eax, GUI_PRESENT
    int 0x80
    ret

triples:
    .byte 0,1,2, 3,4,5, 6,7,8, 0,3,6, 1,4,7, 2,5,8, 0,4,8, 2,4,6
title:   .ascii "Jogo da Velha"
title_end:
x_turn:  .ascii "Vez de X"
x_turn_end:
o_turn:  .ascii "Vez de O"
o_turn_end:
x_won:   .ascii "X venceu"
x_won_end:
o_won:   .ascii "O venceu"
o_won_end:
tie:     .ascii "Empate"
tie_end:
restart: .ascii "Reiniciar"
restart_end:
quit:    .ascii "Sair"
quit_end:
.section .note.GNU-stack,"",@progbits
