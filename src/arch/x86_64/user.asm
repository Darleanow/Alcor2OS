;;
;; Alcor2 User Mode Entry/Exit
;;
;; user_enter: Jump to Ring 3, saves kernel context for return
;; user_return: Return to kernel context (called by sys_exit)
;;
;; Supports nested calls (exec from userspace) via context stack.
;;

section .data

;; Context stack for nested user_enter calls (max 8 levels)
;; Each context is 64 bytes (8 registers * 8 bytes)
align 16
context_stack: times 512 db 0       ; 8 contexts * 64 bytes each
context_depth: dq 0                 ; Current depth (0 = empty)

section .text
global user_enter
global user_return

;;
;; u64 user_enter(void *entry, void *user_rsp)
;;
;; Saves kernel context to stack, then jumps to Ring 3.
;; Returns exit code when user_return() is called.
;;
user_enter:
    ;; Calculate context slot address: context_stack + depth * 64
    mov rax, [rel context_depth]
    shl rax, 6                      ; * 64
    lea r10, [rel context_stack]
    add r10, rax
    
    ;; Save kernel context to slot
    mov [r10 + 0],  rbp
    mov [r10 + 8],  rbx
    mov [r10 + 16], r12
    mov [r10 + 24], r13
    mov [r10 + 32], r14
    mov [r10 + 40], r15
    mov [r10 + 48], rsp
    mov rax, [rsp]                  ; Return address
    mov [r10 + 56], rax
    
    ;; Increment depth
    mov rax, [rel context_depth]
    inc rax
    mov [rel context_depth], rax
    
    ;; Set user data segments
    mov ax, 0x3B            ; User Data (0x38 | RPL 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ;; Build IRETQ frame
    push 0x3B               ; SS
    push rsi                ; RSP (user stack)
    push 0x202              ; RFLAGS (IF enabled)
    push 0x43               ; CS (User Code 0x40 | RPL 3)
    push rdi                ; RIP (user entry)
    
    iretq

;;
;; NORETURN void user_return(u64 exit_code)
;;
;; Called from sys_exit to return to kernel.
;; Pops context from stack.
;;
user_return:
    mov rax, rdi            ; Save exit_code in rax
    
    ;; Restore kernel segments
    mov cx, 0x30
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx
    
    ;; Decrement depth and calculate slot address
    mov rcx, [rel context_depth]
    dec rcx
    mov [rel context_depth], rcx
    shl rcx, 6                      ; * 64
    lea r10, [rel context_stack]
    add r10, rcx
    
    ;; Restore kernel context from slot
    mov rbp, [r10 + 0]
    mov rbx, [r10 + 8]
    mov r12, [r10 + 16]
    mov r13, [r10 + 24]
    mov r14, [r10 + 32]
    mov r15, [r10 + 40]
    mov rsp, [r10 + 48]
    
    ;; Push return address and use ret (fixes stack alignment)
    push qword [r10 + 56]
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
