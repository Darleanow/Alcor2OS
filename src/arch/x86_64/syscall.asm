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

;; User RSP saved during syscall (single-CPU shortcut).
global syscall_user_rsp
syscall_user_rsp: dq 0

section .text
global syscall_entry
extern syscall_dispatch
extern proc_check_signals
extern current_kernel_rsp

;;
;; syscall_entry - Entry point for SYSCALL instruction
;;
;; SYSCALL does NOT switch stacks the way INT does, so we land here with
;; user RSP still in RSP. The kernel stack top for the current process is
;; published by proc_switch into the C global `current_kernel_rsp` (kept in
;; sync with TSS.RSP0). We park user RSP in a scratch global and load the
;; kernel stack before doing anything else; once we have a kernel stack we
;; build syscall_frame_t, call the C dispatcher, and SYSRET back.
;;
;; Single-CPU only: a multi-CPU port would use swapgs + per-cpu_data instead
;; of two file-scope globals.
;;
syscall_entry:
    ;; Interrupts are disabled by SFMASK MSR (IF cleared on entry).

    ;; Park user RSP; load this proc's kernel stack.
    mov [rel syscall_user_rsp], rsp
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

    ;; Check for pending signals — may redirect frame to a signal handler
    mov rdi, rsp
    call proc_check_signals
    
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
