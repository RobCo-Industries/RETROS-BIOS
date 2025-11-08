/* Linker script for Raspberry Pi bare-metal kernel */

ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 0x8000 for all RPi models */
    . = 0x8000;

    .text : {
        KEEP(*(.text.boot))
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* Stack (grows downward) */
    . = ALIGN(8);
    . = . + 0x8000; /* 32KB stack */
    _stack_top = .;

    /DISCARD/ : {
        *(.comment)
        *(.gnu*)
        *(.note*)
        *(.eh_frame*)
    }
}
