;;
;; Alcor2 Process Assembly Helpers
;;

section .text
global proc_enter_first_time
global proc_fork_child_entry

;;
;; proc_enter_first_time
;;
;; Called after context_switch to a new process.
;; The kernel stack has an iretq frame ready.
;; CR3 has already been switched by proc_switch before context_switch.
;;
proc_enter_first_time:
    ;; Set user data segments
    mov ax, 0x3B            ; User Data Segment | RPL 3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ;; Enable interrupts will happen via iretq (RFLAGS has IF)
    
    ;; iretq frame is already on stack from proc_create
    iretq

;;
;; proc_fork_child_entry
;;
;; Called after context_switch to a forked child process.
;; The kernel stack has a syscall_frame_t ready (with rax=0 for child return).
;; This mirrors syscall_entry's return path to restore registers and sysret.
;;
;; Stack layout (from top, same as syscall_entry):
;;   r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax,rip,rflags,rsp
;;
proc_fork_child_entry:
    ;; CR3 has been switched by proc_switch
    
    ;; Set user data segments (like proc_enter_first_time)
    mov ax, 0x3B            ; User Data Segment | RPL 3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ;; Disable interrupts before restoring (will be re-enabled by sysret)
    cli
    
    ;; Restore registers from syscall_frame_t (same as syscall_entry return)
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
    add rsp, 8              ; skip rcx (restored from rip slot via sysret)
    pop rbx
    pop rax                 ; = 0 (child's fork return value)
    pop rcx                 ; user RIP (for sysret)
    pop r11                 ; user RFLAGS (for sysret)
    pop rsp                 ; user RSP
    
    ;; Return to Ring 3
    o64 sysret

section .note.GNU-stack noalloc noexec nowrite progbits
