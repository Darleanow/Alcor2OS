;;
;; Alcor2 Syscall Entry Point
;;
;; The SYSCALL instruction (executed from Ring 3) does:
;;   - RCX = saved RIP (return address)
;;   - R11 = saved RFLAGS
;;   - RIP = LSTAR MSR value
;;   - CS  = STAR[47:32]
;;   - SS  = STAR[47:32] + 8
;;
;; SYSRET (return to Ring 3) does:
;;   - RIP = RCX
;;   - RFLAGS = R11
;;   - CS  = STAR[63:48] + 16
;;   - SS  = STAR[63:48] + 8
;;

section .data

;; User RSP saved during syscall
global syscall_user_rsp
syscall_user_rsp: dq 0

;; Per-process kernel stack pointer (set by proc_switch via TSS)
;; We get it from the TSS RSP0 field
extern tss_get_rsp0

section .text
global syscall_entry
extern syscall_dispatch

;;
;; syscall_entry - Entry point for SYSCALL instruction
;;
;; Builds syscall_frame_t on kernel stack, calls C dispatcher,
;; restores registers, and returns to userspace via SYSRET.
;;
syscall_entry:
    ;; Interrupts are disabled by SFMASK MSR
    
    ;; Save user RSP temporarily in a scratch location
    mov [rel syscall_user_rsp], rsp
    
    ;; Get kernel stack from TSS (set by proc_switch for current process)
    ;; We need to call tss_get_rsp0() but we have no stack yet!
    ;; Solution: use a small trampoline stack, then switch to real kernel stack
    ;; Actually, we saved the per-process kernel stack top in TSS RSP0
    ;; Let's read it directly from the TSS structure
    
    ;; For now, use a simple approach: each process kernel stack is set in TSS
    ;; SYSCALL doesn't automatically switch stacks (unlike interrupts), so we 
    ;; read RSP0 from TSS manually. But that's complex without a stack.
    ;; 
    ;; Simpler: keep using a global syscall stack, but save/restore user context
    ;; properly through the process structure.
    ;;
    ;; For a minimal working solution, we'll use the process's kernel stack
    ;; which was set in TSS RSP0. We read it via the gs segment (kernel gs base
    ;; can point to per-CPU data, but we don't have that yet).
    ;;
    ;; SIMPLEST FIX: The current process's kernel stack top is in TSS RSP0.
    ;; When an interrupt occurs, CPU loads RSP from TSS.RSP0 automatically.
    ;; But SYSCALL does NOT do this! SYSCALL keeps userspace RSP.
    ;; So we need to manually switch to kernel stack.
    ;;
    ;; We stored the kernel stack in proc_t->kernel_stack_top and set TSS RSP0.
    ;; Let's use a global that proc_switch updates:
    
    extern current_kernel_rsp
    mov rsp, [rel current_kernel_rsp]
    
    ;; Build syscall_frame_t on stack (matches struct in syscall.h)
    ;; Layout: r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax,rip,rflags,rsp
    push qword [rel syscall_user_rsp]   ; rsp (user)
    push r11                             ; rflags (saved by SYSCALL)
    push rcx                             ; rip (return address)
    push rax                             ; syscall number
    push rbx
    push rcx                             ; rcx (part of frame)
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11                             ; r11 (part of frame)
    push r12
    push r13
    push r14
    push r15
    
    ;; Call C dispatcher: u64 syscall_dispatch(syscall_frame_t *frame)
    mov rdi, rsp
    call syscall_dispatch
    
    ;; Store return value in frame's rax slot
    mov [rsp + 14*8], rax
    
    ;; CRITICAL: Disable interrupts before restoring registers
    ;; sys_read may have enabled them while waiting for keyboard
    cli
    
    ;; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    add rsp, 8                           ; skip rcx (restored below)
    pop rbx
    pop rax                              ; syscall return value
    pop rcx                              ; user RIP
    pop r11                              ; user RFLAGS
    pop rsp                              ; user RSP
    
    ;; Return to Ring 3 (SYSRET restores RFLAGS from R11, re-enabling interrupts)
    o64 sysret

section .note.GNU-stack noalloc noexec nowrite progbits
