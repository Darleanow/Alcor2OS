;;
;; Alcor2 GDT Load Routine
;;

bits 64

section .text
global gdt_load

;;
;; gdt_load - Load GDT and reload segment registers
;;
;; Input: RDI = pointer to GDT descriptor (limit + base)
;;
gdt_load:
    lgdt [rdi]              ; Load GDT register
    
    ;; Reload data segment registers with kernel data selector (0x30)
    mov ax, 0x30
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ;; Load TSS (selector 0x48)
    mov ax, 0x48
    ltr ax
    
    ;; Far return to reload CS with kernel code selector (0x28)
    pop rdi                 ; Get return address
    mov rax, 0x28           ; Kernel code selector
    push rax
    push rdi
    retfq

section .note.GNU-stack noalloc noexec nowrite progbits
