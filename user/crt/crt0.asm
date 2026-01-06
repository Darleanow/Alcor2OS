;;
;; Alcor2 User CRT0 - C Runtime Startup (NASM)
;;
;; Sets up minimal TLS then calls musl's __libc_start_main
;; to properly initialize the C library before main().
;;

section .note.GNU-stack noalloc noexec nowrite progbits

section .bss
align 64
;; Minimal TLS block - musl expects FS:0 to point to itself
__tls_block:
    resb 256

section .text
global _start

extern main
extern __libc_start_main

_start:
    ;; Clear frame pointer
    xor rbp, rbp
    
    ;; Save argc/argv in callee-saved registers before we modify anything
    mov r12, [rsp]          ; r12 = argc
    lea r13, [rsp + 8]      ; r13 = argv
    
    ;; =====================================================
    ;; Setup minimal TLS using arch_prctl(ARCH_SET_FS)
    ;; musl expects FS:0 to contain a self-pointer
    ;; =====================================================
    lea rax, [rel __tls_block]
    mov [rax], rax          ; Self-pointer at FS:0
    
    mov eax, 158            ; SYS_ARCH_PRCTL
    mov edi, 0x1002         ; ARCH_SET_FS
    lea rsi, [rel __tls_block]
    syscall
    
    ;; =====================================================
    ;; Call __libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end)
    ;; =====================================================
    
    ;; Align stack to 16 bytes
    and rsp, -16
    sub rsp, 8              ; Alignment for the push below
    
    ;; Push stack_end (7th argument on stack)
    push rsp
    
    ;; Setup register arguments
    lea rdi, [rel main]     ; arg1: main function pointer
    mov rsi, r12            ; arg2: argc
    mov rdx, r13            ; arg3: argv
    xor ecx, ecx            ; arg4: init = NULL
    xor r8d, r8d            ; arg5: fini = NULL
    xor r9d, r9d            ; arg6: rtld_fini = NULL
    
    call __libc_start_main
    
    ;; Should never return, but if it does:
    mov edi, eax
    mov eax, 60             ; SYS_EXIT
    syscall
    ud2
