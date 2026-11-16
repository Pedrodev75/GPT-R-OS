.intel_syntax noprefix
.section .text
.code64

.macro push_app_registers
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
.endm

.macro pop_app_registers
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
.endm

.global app_enter
app_enter:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rip + kernel_rsp], rsp

    # Copy the saved context, including RIP, CS, RFLAGS, RSP and SS.
    sub rsp, 20 * 8
    mov rsi, rdi
    mov rdi, rsp
    mov ecx, 20
    cld
    rep movsq
    pop_app_registers
    iretq

.global app_leave
app_leave:
    cli
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, [rip + kernel_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

.global app_syscall_entry
app_syscall_entry:
    push_app_registers
    mov rbx, rsp
    and rsp, -16
    cld
    mov rdi, rbx
    call app_syscall
    mov rsp, rbx
    pop_app_registers
    iretq

.global timer_irq_entry
timer_irq_entry:
    push_app_registers
    mov rbx, rsp
    and rsp, -16
    cld
    call timer_irq_handler
    mov rdi, rbx
    call app_preempt
    mov rsp, rbx
    pop_app_registers
    iretq

.section .bss
.balign 8
kernel_rsp:
    .quad 0

.section .note.GNU-stack,"",@progbits
