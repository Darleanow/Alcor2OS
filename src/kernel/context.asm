;;
;; Alcor2 Context Switch
;;
;; System V AMD64 ABI: callee-saved registers are rbx, rbp, r12-r15
;; We save these + rip (via call/ret mechanism)
;;

section .text
global context_switch

;; void context_switch(cpu_context_t **old_ctx, cpu_context_t *new_ctx)
;;
;; Arguments:
;;   rdi = pointer to old task's context pointer (cpu_context_t **)
;;   rsi = new task's context pointer (cpu_context_t *)
;;
context_switch:
    ;; Save callee-saved registers on current stack
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ;; Save current stack pointer to old task's context
    ;; *old_ctx = rsp
    mov [rdi], rsp

    ;; Switch to new task's stack
    ;; rsp = new_ctx
    mov rsp, rsi

    ;; Restore callee-saved registers from new stack
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ;; Return to new task (rip is at top of stack after pops)
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
