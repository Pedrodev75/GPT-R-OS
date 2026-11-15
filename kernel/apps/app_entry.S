.intel_syntax noprefix
.section .text
.code64

.global app_enter
app_enter:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rip + kernel_rsp], rsp

    # Ring 3 SS, RSP, RFLAGS (IF set, IOPL zero), CS and RIP.
    push 0x1b
    push rsi
    push 0x202
    push 0x23
    push rdi
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx
    xor edi, edi
    xor esi, esi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d
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
    mov rdx, rsi
    mov rsi, rdi
    mov rdi, rax
    # CS follows the ten saved registers and the interrupted RIP.
    mov rcx, [rbx + 11 * 8]
    call app_syscall
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
    add rsp, 8
    iretq

.section .bss
.balign 8
kernel_rsp:
    .quad 0

.section .note.GNU-stack,"",@progbits
