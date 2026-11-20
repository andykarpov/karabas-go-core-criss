/* The helper firmware in the SDRAM: program, data, stack in 0x10000000..0x100fffff; the data ROM of
   the .kg at 0x10100000 (fw/rom.h); MMIO at 0x80000000. */
OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY { RAM (rwx) : ORIGIN = 0x10000000, LENGTH = 0x100000 }

STACK_SIZE = 16K;

SECTIONS
{
    .text : {
        KEEP(*(.text.start))
        *(.text .text.*)
    } > RAM
    .rodata : ALIGN(8) {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } > RAM
    .data : ALIGN(8) {
        __data_start = .;
        *(.data .data.*)
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(8);
        __data_end = .;
    } > RAM
    .bss (NOLOAD) : ALIGN(8) {
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > RAM
    .data_save (NOLOAD) : ALIGN(8) {
        __data_save = .;
        . += __data_end - __data_start;
    } > RAM
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
