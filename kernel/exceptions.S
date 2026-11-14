.intel_syntax noprefix
.section .text
.code64

.macro exception vector, cpu_error=0
exception_\vector:
    .if !\cpu_error
        push 0
    .endif
    push \vector
    jmp exception_common
.endm

exception 0
exception 1
exception 2
exception 3
exception 4
exception 5
exception 6
exception 7
exception 8, 1
exception 9
exception 10, 1
exception 11, 1
exception 12, 1
exception 13, 1
exception 14, 1
exception 15
exception 16
exception 17, 1
exception 18
exception 19
exception 20
exception 21, 1
exception 22
exception 23
exception 24
exception 25
exception 26
exception 27
exception 28
exception 29, 1
exception 30, 1
exception 31

exception_common:
    # Save the interrupted registers before passing the frame to C.
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
    cld
    mov rdi, rsp
    and rsp, -16
    call exception_panic

.section .rodata
.balign 8
.global exception_entries
exception_entries:
    .quad exception_0
    .quad exception_1
    .quad exception_2
    .quad exception_3
    .quad exception_4
    .quad exception_5
    .quad exception_6
    .quad exception_7
    .quad exception_8
    .quad exception_9
    .quad exception_10
    .quad exception_11
    .quad exception_12
    .quad exception_13
    .quad exception_14
    .quad exception_15
    .quad exception_16
    .quad exception_17
    .quad exception_18
    .quad exception_19
    .quad exception_20
    .quad exception_21
    .quad exception_22
    .quad exception_23
    .quad exception_24
    .quad exception_25
    .quad exception_26
    .quad exception_27
    .quad exception_28
    .quad exception_29
    .quad exception_30
    .quad exception_31

.section .note.GNU-stack,"",@progbits
