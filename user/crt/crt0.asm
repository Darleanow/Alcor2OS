;;
;; Alcor2 User CRT0 - C Runtime Startup (NASM)
;;
;; Matches musl entry: no TLS before __libc_start_main — libc sets %fs in
;; __init_tls/__init_tp. A minimal self-pointer block here was misleading
;; pthread layout and broke %fs-relative access in libc/ncurses.
;;

section .note.GNU-stack noalloc noexec nowrite progbits

section .text
global _start

extern main
extern __libc_start_main
extern _init
extern _fini

_start:
    xor rbp, rbp

    mov r12, [rsp]
    lea r13, [rsp + 8]

    and rsp, -16

    lea rdi, [rel main]
    mov rsi, r12
    mov rdx, r13
    lea rcx, [rel _init]
    lea r8, [rel _fini]
    xor r9d, r9d

    call __libc_start_main

    mov edi, eax
    mov eax, 60
    syscall
    ud2
