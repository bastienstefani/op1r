/* Link the piano roll into the free SDRAM window, above op1r's block (01e00000-~01e20000).
 * Everything goes into one PROGBITS section so objcopy -O binary also emits the zeroed .bss. */
ENTRY(_rp_build_screens)
SECTIONS
{
    . = 0x01e80000;
    .blob : {
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*)
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(4);
        *(.tail)                 /* a trailing PROGBITS word so the .bss range is emitted as zeros */
        . = ALIGN(4);
    }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.BFIN.*) }
}

/* R246 firmware entry points (C symbols get a leading underscore on bfin-elf) */
_fw_mode          = 0x01de4c40;
_fw_cur_screen    = 0x018b65c8;
_fw_screen_reg    = 0x01df4e54;
_op1r_euclid_flag = 0x01e158d0;
_fw_scr_raster    = 0x01a3b490;
_fw_line          = 0x01a24a4c;
_fw_screen_ctor   = 0x01a3bab0;
_fw_note_on       = 0x01a85750;
_fw_note_off      = 0x01a85840;
_fw_arp_instance  = 0x01a46400;
_fw_synth_slot    = 0xff8016d4;
_fw_synth_oct     = 0x01dfe4e0;
_fw_text          = 0x01e026f0;

/* op1r 5 hook targets we chain to (build.sh verifies these against the image) */
_op1r_id_from_name  = 0x01e007a8;
_op1r_name_from_id  = 0x01e0083c;
_op1r_build_screens = 0x01e00c04;
_op1r_init_screens  = 0x01e00eb4;
_op1r_key           = 0x01e010f0;
_op1r_encoder       = 0x01e01384;
