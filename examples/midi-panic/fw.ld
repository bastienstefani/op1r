/* Link the example at a fixed address in the free SDRAM window (01dfff34-01f00000, see the docs), above the
 * op1r block (01e00000-~01e20000) so it also works on top of op1r. Everything goes into one PROGBITS
 * section so that objcopy -O binary also emits the zero-initialised variables. */
ENTRY(_key_hook)
SECTIONS
{
    . = 0x01e80000;
    .blob : {
        KEEP(*(.text.key_hook)) *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*)
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(4);
    }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
/* R246 addresses (C symbols get a leading underscore on bfin-elf) */
PROVIDE(_fw_midi_cc = 0x019e3004);
PROVIDE(_fw_shift_held = 0x01dd7f78);
