/* drive: a Serum-style distortion FX for the OP-1 (R246), built on top of op1r 10 and the piano roll.
 *
 * Study example for docs/firmware-r246.md.  The original op1_246.op1 and the op1r image can always be
 * reflashed, so a lock-up only costs a reflash.
 *
 * Hosted on the hidden stock FX "filter" (internal id 5), exactly like op1r's tape / clouds / reverb:
 *   - fx_types row "drive" whose default params carry the marker 0x5304 in params[4] (op1r uses 0x5301..3);
 *   - the stock fx name compare (JUMP.L at 019aad8e) also accepts "drive" for "filter";
 *   - the filter's process / set_position vtable words (mono 01910ca8/b0, stereo 01910cc8/d0) run drive when
 *     the marker is ours and op1r's dispatcher otherwise;
 *   - ScreenId fx case 5 (table word 018f7ff8) shows our screen (id 0xb, op1r's FX screen id) for drive;
 *   - encoder presses switch page 1 / page 2; the green encoder steps the mode; page 2 is ours (as op1r does
 *     for its page 2); every edit is written into the patch (019ff664), so it is saved with the sound.
 * Each hook calls the next one in the chain (drive -> piano roll -> op1r -> stock), see drive.ld.
 *
 * Params (8 x int16, as stored in the patch):
 *   0 drive  1 mode  2 freq  3 mix      page 1, inside the filter's bounds (01de5c50 row 5), normalised here
 *   4 marker 0x5304
 *   5 route x type (9 steps)  6 resonance  7 level      page 2, 0..32767
 */
#include <stdint.h>
#include "dsp.h"
#include "ui.h"
#include "screen.h"

#define DV_MARK    0x5304
#define DV_SCREEN  0x0b                 /* op1r's FX screen id (ScreenId fx case 5) */
#define HOST_FX    5                    /* filter */

/* ---- firmware (R246) addresses: drive.ld.  bfin-elf adds a leading underscore to C symbols. ---- */
typedef struct { uint32_t a; union { char buf[16]; char *ptr; } u; uint32_t len, cap; } te_string;

extern volatile int32_t  fw_cur_screen;              /* 018b65c8: id of the screen being shown */
extern void             *fw_screen_reg[];            /* 01df4e54: screen registry, indexed by screen id */
extern volatile int32_t  fw_fx_ctx;                  /* 01df4e3c: whose FX the FX page edits: 0 synth, 1 drum, 3 master */
extern volatile int32_t  fw_synth_slot;              /* ff8016d4 */
extern volatile int32_t  fw_drum_slot;               /* ff8016d8 */
extern int16_t           fw_fx_synth[8][8];          /* ff801444: FX params per synth slot */
extern int16_t           fw_fx_drum[8][8];           /* ff8015c4: FX params per drum slot */
extern int16_t           fw_fx_master[8];            /* ff8016c4 */
extern volatile int32_t  fw_fxid_synth[8];           /* ff801734: internal FX id per synth slot */
extern volatile int32_t  fw_fxid_drum[8];            /* ff801754 */
extern volatile int32_t  fw_fxid_master;             /* ff801090 */
extern int16_t           fw_fx_bounds[];             /* 01de5c50: {min, max, scale} x 8 per FX id, set at boot */
extern uint8_t           fw_patch_synth[];           /* 01ddd7e8 + slot * 0x38 */
extern uint8_t           fw_patch_drum[];            /* 01ddd9a8 + slot * 0x38 */
extern const uintptr_t   fw_base_screen_vt[10];      /* 01911474: the base Screen vtable (draw at +0x1c) */

extern void  fw_patch_set_fx(void *patch, const int16_t *p);           /* 019ff664: params -> patch JSON */
extern void *fw_scr_raster(void);                                      /* 01a3b490 */
extern void  fw_line(void *raster, const Pt *a, const Pt *b, int32_t width, const uint8_t rgba[4]); /* 01a24a4c */
extern void  fw_screen_ctor(void *obj, int32_t id, const char *svg);   /* 01a3bab0 */

/* op1r 10: addresses in drive.ld, verified by build.sh (it pins op1r's block by its sha256) */
extern int32_t op1r_fx_name_eq(const char *table_name, const te_string *db_name);
extern void    op1r_fx_mono(void *self, int16_t *buf, const int16_t *p);
extern void    op1r_fx_mono_pos(void *self, int32_t pos);
extern void    op1r_fx_stereo(void *self, int16_t *l, int16_t *r, const int16_t *p);
extern void    op1r_fx_stereo_pos(void *self, int32_t pos);
extern int32_t op1r_fx_screen(void);
extern volatile int32_t op1r_mono_cache, op1r_stereo_cache;   /* op1r's "which of my FX ran last" */

/* the next hooks in the chain (the piano roll's), see drive.ld */
extern void *dv_next_build_screens(void);
extern void  dv_next_init_screens(void);
extern void  dv_next_key(int32_t key, int32_t pressed);
extern void  dv_next_encoder(int32_t enc, int16_t *delta);

static void dv_line(void *r, const Pt *a, const Pt *b, int32_t width, const uint8_t rgba[4]) {
    fw_line(r, a, b, width, rgba);
}

/* ------------------------------------------------------------------ params */
/* bounds of the host's params 0..3: the firmware's table, or op1r's fallback while it is not set up */
static const int16_t FALLBACK[4][2] = { { 64, 14528 }, { 0, 24576 }, { 0, 22528 }, { 8192, 32767 } };
static void bounds(int i, int32_t *lo, int32_t *hi) {
    const int16_t *b = &fw_fx_bounds[HOST_FX * 24 + i * 3];
    *lo = b[0]; *hi = b[1];
    if (*hi <= *lo) { *lo = FALLBACK[i][0]; *hi = FALLBACK[i][1]; }
}
static int32_t norm(int i, int32_t v) {          /* stored -> 0..32767 */
    int32_t lo, hi;
    bounds(i, &lo, &hi);
    return clamp32(((clamp32(v, lo, hi) - lo) * 32767) / (hi - lo), 0, 32767);
}
static int32_t denorm(int i, int32_t n) {        /* 0..32767 -> stored */
    int32_t lo, hi;
    bounds(i, &lo, &hi);
    return lo + (clamp32(n, 0, 32767) * (hi - lo)) / 32767;
}
static void raw_of(int32_t *raw, const int16_t *p) {
    int i;
    for (i = 0; i < 4; i++) raw[i] = norm(i, p[i]);
    raw[P_MARK] = p[P_MARK];
    for (i = 5; i < 8; i++) raw[i] = clamp32(p[i], 0, 32767);
}

/* the FX params the FX page currently edits, or 0 if they are not drive's */
static int16_t *dv_cur(int *pctx) {
    int ctx = fw_fx_ctx, s;
    int16_t *p;
    int32_t id;
    if (ctx == 3) { p = fw_fx_master; id = fw_fxid_master; }
    else if (ctx == 1) { s = fw_drum_slot & 7; p = fw_fx_drum[s]; id = fw_fxid_drum[s]; }
    else { ctx = 0; s = fw_synth_slot & 7; p = fw_fx_synth[s]; id = fw_fxid_synth[s]; }
    if (id != HOST_FX || p[P_MARK] != DV_MARK) return 0;
    if (pctx) *pctx = ctx;
    return p;
}
/* write the edited params into the sound (patch JSON), so they are saved with it.  The master's array is its
 * own state (op1r does the same for its page 2). */
static void dv_store(int ctx, const int16_t *p) {
    if (ctx == 0) fw_patch_set_fx(fw_patch_synth + 0x38 * (fw_synth_slot & 7), p);
    else if (ctx == 1) fw_patch_set_fx(fw_patch_drum + 0x38 * (fw_drum_slot & 7), p);
}

/* ------------------------------------------------------------------ audio (IVG11 interrupt, 64 samples) */
static DvState st[4];                   /* 0 synth, 1 drum (one mono instance lives at a time), 2-3 master L/R */
static volatile uint8_t dv_reset_req[4];
static volatile int32_t dv_mono_inst;   /* set_position: 0 synth, 1 drum (as op1r) */

static void dv_params_p(DvParams *P, const int16_t *p) {
    int32_t raw[8];
    raw_of(raw, p);
    dv_params_of(P, raw);
}
static void dv_run(int inst, int16_t *buf, const DvParams *P) {
    if (dv_reset_req[inst]) { dv_reset_req[inst] = 0; st[inst].inited = 0; }
    dv_block(&st[inst], P, buf, 64, 1);
}

void dv_fx_mono(void *self, int16_t *buf, const int16_t *p) {
    DvParams P;
    if (p[P_MARK] != DV_MARK) { op1r_fx_mono(self, buf, p); return; }
    op1r_mono_cache = 0;                /* as op1r does for a FX that is not its own: resets its FX on return */
    dv_params_p(&P, p);
    dv_run(dv_mono_inst & 1, buf, &P);
}
void dv_fx_mono_pos(void *self, int32_t pos) {
    dv_mono_inst = pos & 1;
    dv_reset_req[pos & 1] = 1;
    op1r_fx_mono_pos(self, pos);
}
void dv_fx_stereo(void *self, int16_t *l, int16_t *r, const int16_t *p) {
    DvParams P;
    if (p[P_MARK] != DV_MARK) { op1r_fx_stereo(self, l, r, p); return; }
    op1r_stereo_cache = 0;
    dv_params_p(&P, p);
    dv_run(2, l, &P);
    dv_run(3, r, &P);
}
void dv_fx_stereo_pos(void *self, int32_t pos) {
    dv_reset_req[2] = dv_reset_req[3] = 1;
    op1r_fx_stereo_pos(self, pos);
}

/* ------------------------------------------------------------------ name: "drive" is a "filter" */
int32_t dv_fx_name_eq(const char *table_name, const te_string *db_name) {
    const char *s = db_name->cap < 16 ? db_name->u.buf : db_name->u.ptr;
    const char *t = table_name;
    if (db_name->len == 5 && s[0] == 'd' && s[1] == 'r' && s[2] == 'i' && s[3] == 'v' && s[4] == 'e'
        && t[0] == 'f' && t[1] == 'i' && t[2] == 'l' && t[3] == 't' && t[4] == 'e' && t[5] == 'r' && !t[6])
        return 1;
    return op1r_fx_name_eq(table_name, db_name);
}

/* ------------------------------------------------------------------ screen */
static uint8_t  dv_ready;
static uint8_t  dv_page;
static uintptr_t dv_scr_obj[4];             /* the screen object: doc, id, loader, vptr (16 bytes) */
static uintptr_t dv_scr_vt[10];
static int32_t  dv_peak_shown, dv_phase;

static int dv_showing(void) {
    return dv_ready && fw_cur_screen == DV_SCREEN && fw_screen_reg[DV_SCREEN] == (void*) dv_scr_obj && dv_cur(0);
}

static void dv_draw(void *self, int32_t dt) {
    DvView v;
    int16_t *p;
    void *r;
    int ctx = 0;
    int32_t pk;
    (void) self; (void) dt;
    if (!dv_ready || !(p = dv_cur(&ctx)) || !(r = fw_scr_raster())) return;
    raw_of(v.raw, p);
    pk = ctx == 3 ? (st[2].peak > st[3].peak ? st[2].peak : st[3].peak) : st[dv_mono_inst & 1].peak;
    dv_peak_shown = pk > dv_peak_shown ? pk : dv_peak_shown - (dv_peak_shown >> 3);   /* fast up, slow down */
    v.peak = dv_peak_shown;
    v.page = dv_page;
    dv_phase = (dv_phase + 160) & 32767;
    v.phase = dv_phase;
    dv_draw_screen(r, &v);
}

/* ScreenId fx case 5 runs inside the switch function's frame: our case calls dv_scr_id, then returns for it
 * with the same epilogue as op1r's case (01e0d76c). */
#ifndef DV_HOST                         /* test_hooks.c builds this file on a PC */
__asm__(
"    .text\n"
"    .align 4\n"
"    .global _dv_scr_case\n"
"    .type _dv_scr_case, STT_FUNC\n"
"_dv_scr_case:\n"
"    CALL _dv_scr_id;\n"
"    P0 = [FP + 4];\n"
"    UNLINK;\n"
"    JUMP (P0);\n"
"    .size _dv_scr_case, .-_dv_scr_case\n"
);
#endif
int32_t dv_scr_id(void) {
    int32_t id = op1r_fx_screen();     /* op1r puts its own / the stock screen into slot 0xb */
    int16_t *p;
    if (dv_ready && (p = dv_cur(0))) {
        int i;
        for (i = 0; i < 4; i++) {       /* keep page 1 inside the host's bounds (as op1r's repair does) */
            int32_t lo, hi;
            bounds(i, &lo, &hi);
            p[i] = (int16_t) clamp32(p[i], lo, hi);
        }
        fw_screen_reg[DV_SCREEN] = dv_scr_obj;
    }
    return id;
}

void *dv_build_screens(void) {
    void *ret = dv_next_build_screens();
    void *keep = fw_screen_reg[DV_SCREEN];
    int i;
    fw_screen_ctor(dv_scr_obj, DV_SCREEN, "drive.svg");
    fw_screen_reg[DV_SCREEN] = keep;
    for (i = 0; i < 10; i++) dv_scr_vt[i] = fw_base_screen_vt[i];
    dv_scr_vt[7] = (uintptr_t) dv_draw;                  /* slot +0x1c = draw */
    dv_scr_obj[3] = (uintptr_t) dv_scr_vt;
    return ret;
}
void dv_init_screens(void) {
    dv_next_init_screens();
    dv_ready = 1;
}

/* ------------------------------------------------------------------ keys and encoders */
void dv_key(int32_t key, int32_t pressed) {
    if (key >= 0x3b && key <= 0x3e && dv_showing()) {  /* encoder presses: page 1 <-> page 2 */
        if (pressed) dv_page ^= 1;
        return;
    }
    dv_next_key(key, pressed);
}

static int32_t zone(int k, int n) { return ((2 * k + 1) * 32768) / (2 * n); }   /* centre of step k of n */

void dv_encoder(int32_t enc, int16_t *delta) {
    int16_t *p;
    int ctx = 0, n;
    DvParams P;
    int32_t raw[8];
    if (!dv_showing() || !(p = dv_cur(&ctx)) || enc < 0 || enc > 3) { dv_next_encoder(enc, delta); return; }
    if (!dv_page && enc != 1) { dv_next_encoder(enc, delta); return; }     /* drive, freq, mix: stock */
    raw_of(raw, p);
    dv_params_of(&P, raw);
    n = (int) *delta / 1024;                            /* one detent = 0x400 */
    if (!dv_page) {                                     /* green: one mode per detent */
        int m = clamp32(P.mode + n, 0, DV_NMODES - 1);
        if (!n || m == P.mode) return;
        p[P_MODE] = (int16_t) denorm(P_MODE, zone(m, DV_NMODES));
    } else if (enc <= 1) {                              /* blue: route, green: filter type */
        int route = P.route, type = P.ftype;
        if (!n) return;
        if (enc == 0) route = clamp32(route + n, 0, 2);
        else type = clamp32(type + n, 0, 2);
        p[P_ROUTE] = (int16_t) zone(route * 3 + type, 9);
    } else {                                            /* white: resonance, orange: level */
        int k = enc == 2 ? P_RES : P_LEVEL;
        p[k] = (int16_t) clamp32(p[k] + *delta, 0, 32767);
    }
    dv_store(ctx, p);
}

/* a trailing PROGBITS word so objcopy emits the zeroed .bss inside the blob */
__attribute__((section(".tail"), used)) static const uint32_t dv_tail = 0x64726976;   /* "driv" */
