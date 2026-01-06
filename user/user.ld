/**
 * Alcor2 User Program Linker Script
 * 
 * Links user programs at a fixed virtual address for Ring 3.
 * Compatible with mlibc requirements.
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* User programs load at 4MB */
USER_BASE = 0x400000;

SECTIONS
{
    . = USER_BASE;
    
    /* ELF header reference for mlibc */
    __ehdr_start = .;
    
    .text : {
        *(.text .text.*)
    }
    
    .rodata : {
        *(.rodata .rodata.*)
    }
    
    /* Pre-init array */
    .preinit_array : {
        __preinit_array_start = .;
        KEEP(*(.preinit_array))
        __preinit_array_end = .;
    }
    
    /* Init array */
    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    }
    
    /* Fini array */
    .fini_array : {
        __fini_array_start = .;
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = .;
    }
    
    .data : {
        *(.data .data.*)
    }
    
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}

/* DSO handle for C++ ABI (static linking) */
PROVIDE(__dso_handle = 0);
