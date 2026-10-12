/* Host test of drive.c's hooks against a mock firmware (the addresses themselves are checked by build.sh):
 *     cc -DDV_HOST -O1 -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover -o th test_hooks.c && ./th
 * - name aliasing: "drive" == "filter" only, everything else goes to op1r's compare;
 * - audio: drive runs only for its marker, otherwise op1r's dispatchers get the call unchanged;
 * - screen: our screen goes into slot 0xb only for a drive slot (synth, drum, master);
 * - encoders: page 1 green steps the mode, page 2 steps route / type and moves resonance / level, everything
 *   else goes down the chain; every edit is stored into the right patch; params stay in their bounds. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "drive.c"

/* ---- mock firmware ---- */
volatile int32_t fw_cur_screen, fw_fx_ctx, fw_synth_slot, fw_drum_slot, fw_fxid_synth[8], fw_fxid_drum[8], fw_fxid_master;
void *fw_screen_reg[0x40];
int16_t fw_fx_synth[8][8], fw_fx_drum[8][8], fw_fx_master[8];
int16_t fw_fx_bounds[16 * 24];
uint8_t fw_patch_synth[8 * 0x38], fw_patch_drum[8 * 0x38];
static void *last_patch; static int stores, lines;
void fw_patch_set_fx(void *patch, const int16_t *p) { (void) p; last_patch = patch; stores++; }
static int raster;
void *fw_scr_raster(void) { return &raster; }
void fw_line(void *r, const Pt *a, const Pt *b, int32_t w, const uint8_t c[4]) { (void) r; (void) a; (void) b; (void) w; (void) c; lines++; }
void fw_screen_ctor(void *obj, int32_t id, const char *svg) { (void) svg; fw_screen_reg[id] = obj; }
static int op1r_eq_calls, op1r_mono_calls, op1r_stereo_calls, next_key_calls, next_enc_calls;
int32_t op1r_fx_name_eq(const char *t, const te_string *s) { op1r_eq_calls++; return strcmp(t, s->u.buf) == 0; }
void op1r_fx_mono(void *self, int16_t *buf, const int16_t *p) { (void) self; (void) buf; (void) p; op1r_mono_calls++; }
void op1r_fx_mono_pos(void *self, int32_t pos) { (void) self; (void) pos; }
void op1r_fx_stereo(void *self, int16_t *l, int16_t *r, const int16_t *p) { (void) self; (void) l; (void) r; (void) p; op1r_stereo_calls++; }
void op1r_fx_stereo_pos(void *self, int32_t pos) { (void) self; (void) pos; }
static void *op1r_screen = (void *) 0x1234;
int32_t op1r_fx_screen(void) { fw_screen_reg[0xb] = op1r_screen; return 0xb; }
volatile int32_t op1r_mono_cache = 7, op1r_stereo_cache = 7;
const uintptr_t fw_base_screen_vt[10] = { 0, 0, 0, 1, 0, 2, 0, 3, 0, 4 };
void *dv_next_build_screens(void) { return 0; }
void dv_next_init_screens(void) {}
void dv_next_key(int32_t key, int32_t pressed) { (void) key; (void) pressed; next_key_calls++; }
void dv_next_encoder(int32_t enc, int16_t *delta) { (void) enc; (void) delta; next_enc_calls++; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static const int16_t DEF[8] = { 4403, 945, 11264, 32700, 21252, 1820, 6553, 26214 };

static int mode_of(const int16_t *p) { DvParams P; dv_params_p(&P, p); return P.mode; }

int main(void) {
    te_string s;
    int16_t buf[64], d;
    int i;
    /* bounds table: filter row = op1r's fallback values (as on the hardware, probably) */
    for (i = 0; i < 4; i++) { fw_fx_bounds[5 * 24 + i * 3] = FALLBACK[i][0]; fw_fx_bounds[5 * 24 + i * 3 + 1] = FALLBACK[i][1]; }

    /* names */
    memset(&s, 0, sizeof s); strcpy(s.u.buf, "drive"); s.len = 5; s.cap = 15;
    CHECK(dv_fx_name_eq("filter", &s) == 1 && op1r_eq_calls == 0);
    CHECK(dv_fx_name_eq("delay", &s) == 0 && op1r_eq_calls == 1);
    strcpy(s.u.buf, "tape"); s.len = 4;
    CHECK(dv_fx_name_eq("filter", &s) == 0 && op1r_eq_calls == 2);      /* op1r's mock: exact compare */

    /* audio */
    memcpy(fw_fx_synth[2], DEF, sizeof DEF);
    for (i = 0; i < 64; i++) buf[i] = (int16_t) (i * 400 - 12800);
    dv_fx_mono_pos(0, 0);
    dv_fx_mono(0, buf, fw_fx_synth[2]);
    CHECK(op1r_mono_calls == 0 && op1r_mono_cache == 0);
    { int16_t other[8] = { 1, 2, 3, 4, 0x5302, 0, 0, 0 }; dv_fx_mono(0, buf, other); CHECK(op1r_mono_calls == 1); }
    { int16_t l[64] = { 0 }, r[64] = { 0 }; int16_t other[8] = { 0 };
      dv_fx_stereo(0, l, r, other); CHECK(op1r_stereo_calls == 1);
      dv_fx_stereo(0, l, r, DEF); CHECK(op1r_stereo_calls == 1 && op1r_stereo_cache == 0); }

    /* screens */
    dv_build_screens();
    CHECK(fw_screen_reg[0xb] == 0);                     /* ctor's registration undone */
    dv_init_screens();
    fw_fx_ctx = 0; fw_synth_slot = 2; fw_fxid_synth[2] = 5;
    CHECK(dv_scr_id() == 0xb && fw_screen_reg[0xb] == dv_scr_obj);
    fw_fxid_synth[2] = 3;                               /* another FX id with the same marker: not ours */
    CHECK(dv_scr_id() == 0xb && fw_screen_reg[0xb] == op1r_screen);
    fw_fxid_synth[2] = 5;
    fw_fx_ctx = 1; fw_drum_slot = 4; fw_fxid_drum[4] = 5;
    CHECK(dv_scr_id() == 0xb && fw_screen_reg[0xb] == op1r_screen);   /* drum slot 4 has no marker */
    memcpy(fw_fx_drum[4], DEF, sizeof DEF);
    CHECK(dv_scr_id() == 0xb && fw_screen_reg[0xb] == dv_scr_obj);
    fw_fx_ctx = 3; fw_fxid_master = 5; memcpy(fw_fx_master, DEF, sizeof DEF);
    CHECK(dv_scr_id() == 0xb && fw_screen_reg[0xb] == dv_scr_obj);

    /* drawing through the vtable slot */
    fw_cur_screen = 0xb;
    ((void (*)(void *, int32_t)) dv_scr_vt[7])(dv_scr_obj, 16);
    CHECK(lines > 100);

    /* keys: encoder presses toggle the page on our screen only */
    fw_fx_ctx = 0;
    dv_scr_id();
    dv_key(0x3b, 1); dv_key(0x3b, 0);
    CHECK(dv_page == 1 && next_key_calls == 0);
    dv_key(0x20, 1); CHECK(next_key_calls == 1);
    dv_page = 0;

    /* encoders, page 1 */
    d = 1024; dv_encoder(0, &d); CHECK(next_enc_calls == 1);            /* drive: stock */
    for (i = 1; i < DV_NMODES + 3; i++) {
        d = 1024; dv_encoder(1, &d);
        CHECK(mode_of(fw_fx_synth[2]) == (i < DV_NMODES ? i : DV_NMODES - 1));
        CHECK(fw_fx_synth[2][1] >= 0 && fw_fx_synth[2][1] <= 24576);
    }
    CHECK(last_patch == fw_patch_synth + 2 * 0x38);
    for (i = 0; i < DV_NMODES + 3; i++) { d = -1024; dv_encoder(1, &d); }
    CHECK(mode_of(fw_fx_synth[2]) == 0 && next_enc_calls == 1);
    d = 512; dv_encoder(1, &d);                                         /* less than a detent: nothing */
    CHECK(mode_of(fw_fx_synth[2]) == 0);

    /* page 2 */
    dv_page = 1;
    { DvParams P;
      d = 1024; dv_encoder(0, &d); dv_params_p(&P, fw_fx_synth[2]); CHECK(P.route == R_PRE && P.ftype == F_LP);
      d = 2048; dv_encoder(1, &d); dv_params_p(&P, fw_fx_synth[2]); CHECK(P.route == R_PRE && P.ftype == F_HP);
      d = 5 * 1024; dv_encoder(0, &d); dv_params_p(&P, fw_fx_synth[2]); CHECK(P.route == R_POST && P.ftype == F_HP);
      d = -9 * 1024; dv_encoder(1, &d); dv_params_p(&P, fw_fx_synth[2]); CHECK(P.route == R_POST && P.ftype == F_LP);
      for (i = 0; i < 40; i++) { d = 1024; dv_encoder(3, &d); }
      CHECK(fw_fx_synth[2][7] == 32767);
      for (i = 0; i < 40; i++) { d = -1024; dv_encoder(2, &d); }
      CHECK(fw_fx_synth[2][6] == 0);
      CHECK(fw_fx_synth[2][4] == DV_MARK && next_enc_calls == 1); }
    /* drum and master stores */
    fw_fx_ctx = 1; dv_scr_id(); stores = 0; d = 1024; dv_encoder(3, &d);
    CHECK(stores == 1 && last_patch == fw_patch_drum + 4 * 0x38);
    fw_fx_ctx = 3; dv_scr_id(); stores = 0; d = 1024; dv_encoder(3, &d);
    CHECK(stores == 0 && fw_fx_master[7] == DEF[7] + 1024);
    /* another screen: everything goes down the chain */
    fw_cur_screen = 0x2c; d = 1024; dv_encoder(1, &d); dv_key(0x3b, 1);
    CHECK(next_enc_calls == 2 && next_key_calls == 2);

    printf("%s (%d failures)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
