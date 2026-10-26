ENTRY(_start)
MEMORY
{
    irom (rx) : ORIGIN = 0x42000000, LENGTH = 4M
    drom (r)  : ORIGIN = 0x3C100000, LENGTH = 4M
    dram (rw) : ORIGIN = 0x3FC90000, LENGTH = 0x60000
}
SECTIONS
{
    .text : {
        *(.literal._start) *(.text._start)
        *(.literal .literal.* .text .text.* .gnu.linkonce.t.*)
    } > irom
    .rodata : {
        *(.rodata .rodata.* .gnu.linkonce.r.*)
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array .ctors))
        __init_array_end = .;
        *(.eh_frame*)
    } > drom
    .data : {
        *(.data .data.* .sdata .sdata.* .gnu.linkonce.d.*)
    } > dram
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        __bss_end = .;
    } > dram
    __stack_top = 0x3FCF0000;
    /DISCARD/ : { *(.comment) }
}
