;;
;; Alcor2 User Mode Test Program
;;
;; Demonstrates Ring 3 execution and syscall functionality.
;; This code is copied to user-accessible memory at 0x400000.
;;

section .text
global user_test_code
global user_test_code_end

;;
;; Entry point - called from Ring 3
;;
;; Syscall convention (Linux-compatible):
;;   RAX = syscall number
;;   RDI, RSI, RDX, R10, R8, R9 = arguments
;;   Return value in RAX
;;
user_test_code:
    ;; SYS_EXIT(42) - syscall 0
    ;; Demonstrates successful Ring 3 -> Ring 0 transition
    mov rax, 0              ; SYS_EXIT
    mov rdi, 42             ; exit code (42 = "The Answer")
    syscall
    
    ;; Should never reach here
.hang:
    jmp .hang

user_test_code_end:

section .note.GNU-stack noalloc noexec nowrite progbits
