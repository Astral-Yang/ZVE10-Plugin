OUTPUT_FORMAT("elf32-littlearm")
ENTRY(zvp_liro_header)

MEMORY
{
    TAILCODE (rwx) : ORIGIN = 0x6471AD00, LENGTH = 0x4000
}

SECTIONS
{
    .text 0x6471AD00 : {
        KEEP(*(.text.zvp_liro_header))
        *(.text*)
        *(.rodata*)
    } > TAILCODE

    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.ARM.attributes*)
        *(.comment*)
        *(.note*)
    }
}
