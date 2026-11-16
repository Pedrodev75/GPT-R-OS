.intel_syntax noprefix
.section .text
.code64
.macro irq_entry entry, handler
.global \entry
\entry:
    # Preserve the interrupted code's caller-saved registers.
    push rax
    push rcx
    push rdx
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push rbx
    mov rbx, rsp
    and rsp, -16
    cld
    call \handler
    mov rsp, rbx
    pop rbx
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rax
    iretq
.endm

irq_entry keyboard_irq_entry, keyboard_irq_handler
irq_entry mouse_irq_entry, mouse_irq_handler

.section .note.GNU-stack,"",@progbits
