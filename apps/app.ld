/* Links an app (apps/hhapp.h) to run from the app RAM block. The image
   starts with its hh_app_header_t (section .hh_header, HH_APP()); the
   firmware copies everything up to __hh_load_end and zeroes .bss after
   it. Keep ORIGIN and LENGTH in sync with HH_APP_RAM_BASE and
   HH_APP_RAM_SIZE. */
MEMORY
{
    APPRAM (rwx) : ORIGIN = 0x20002000, LENGTH = 12K
}

SECTIONS
{
    .image :
    {
        KEEP(*(.hh_header))
        *(.text .text.*)
        *(.rodata .rodata.* .srodata .srodata.*)
        *(.data .data.* .sdata .sdata.*)
        . = ALIGN(4);
        __hh_load_end = .;
    } > APPRAM

    .bss (NOLOAD) :
    {
        *(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(4);
        __hh_bss_end = .;
    } > APPRAM

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.riscv.attributes) }
}
