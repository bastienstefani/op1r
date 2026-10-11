/* Link drive together with the piano roll into one blob at 01e80000 (layout, R246 and op1r 10 symbols come
 * from the piano roll's script; build.sh passes -L ../pianoroll). */
INCLUDE fw.ld

/* R246 firmware (stock addresses only, see the rule in ../pianoroll/fw.ld) */
_fw_fx_ctx        = 0x01df4e3c;
_fw_drum_slot     = 0xff8016d8;
_fw_fx_synth      = 0xff801444;
_fw_fx_drum       = 0xff8015c4;
_fw_fx_master     = 0xff8016c4;
_fw_fxid_synth    = 0xff801734;
_fw_fxid_drum     = 0xff801754;
_fw_fxid_master   = 0xff801090;
_fw_fx_bounds     = 0x01de5c50;
_fw_patch_synth   = 0x01ddd7e8;
_fw_patch_drum    = 0x01ddd9a8;
_fw_patch_set_fx  = 0x019ff664;
_fw_base_screen_vt = 0x01911474;

/* op1r 10 (build.sh pins op1r's whole block by its sha256, so these cannot have moved) */
_op1r_fx_name_eq    = 0x01e00be8;
_op1r_fx_mono       = 0x01e008d0;
_op1r_fx_mono_pos   = 0x01e00734;
_op1r_fx_stereo     = 0x01e00778;
_op1r_fx_stereo_pos = 0x01e00760;
_op1r_fx_screen     = 0x01e01c8c;
_op1r_mono_cache    = 0x01e1678c;
_op1r_stereo_cache  = 0x01e16788;

/* the chain on the shared sites: drive -> piano roll -> op1r -> stock */
_dv_next_build_screens = _rp_build_screens;
_dv_next_init_screens  = _rp_init_screens;
_dv_next_key           = _rp_key;
_dv_next_encoder       = _rp_encoder;
