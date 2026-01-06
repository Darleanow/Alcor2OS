OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(kmain)

KERNEL_VADDR = 0xffffffff80000000;

PHDRS {
    text    PT_LOAD FLAGS((1 << 0) | (1 << 2));
    rodata  PT_LOAD FLAGS((1 << 2));
    data    PT_LOAD FLAGS((1 << 1) | (1 << 2));
    dynamic PT_DYNAMIC FLAGS((1 << 1) | (1 << 2));
}

SECTIONS {
    . = KERNEL_VADDR;

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(4096);

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(4096);

    .data : {
        *(.data .data.*)
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :data

    . = ALIGN(4096);

    .dynamic : {
        *(.dynamic)
    } :dynamic

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.eh_frame .note .note.* .comment)
    }
}
