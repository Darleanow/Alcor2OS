bits 64

extern exception_handler
extern irq_handler

section .text

%macro push_regs 0
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro pop_regs 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
%endmacro

%macro isr_no_err 1
isr_stub_%1:
    push 0
    push %1
    push_regs
    mov rdi, rsp
    call exception_handler
    pop_regs
    add rsp, 16
    iretq
%endmacro

%macro isr_err 1
isr_stub_%1:
    push %1
    push_regs
    mov rdi, rsp
    call exception_handler
    pop_regs
    add rsp, 16
    iretq
%endmacro

isr_no_err 0
isr_no_err 1
isr_no_err 2
isr_no_err 3
isr_no_err 4
isr_no_err 5
isr_no_err 6
isr_no_err 7
isr_err    8
isr_no_err 9
isr_err    10
isr_err    11
isr_err    12
isr_err    13
isr_err    14
isr_no_err 15
isr_no_err 16
isr_err    17
isr_no_err 18
isr_no_err 19
isr_no_err 20
isr_err    21
isr_no_err 22
isr_no_err 23
isr_no_err 24
isr_no_err 25
isr_no_err 26
isr_no_err 27
isr_no_err 28
isr_err    29
isr_err    30
isr_no_err 31

%macro irq_stub 1
irq_stub_%1:
    push 0
    push (%1 + 32)
    push_regs
    mov rdi, %1
    call irq_handler
    pop_regs
    add rsp, 16
    iretq
%endmacro

%assign i 0
%rep 16
    irq_stub i
%assign i i+1
%endrep

section .data
global isr_stub_table
isr_stub_table:
%assign i 0
%rep 32
    dq isr_stub_%+i
%assign i i+1
%endrep

global irq_stub_table
irq_stub_table:
%assign i 0
%rep 16
    dq irq_stub_%+i
%assign i i+1
%endrep
section .note.GNU-stack noalloc noexec nowrite progbits
