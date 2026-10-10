/* Piano-roll sequencer for the OP-1 (R246), built on top of op1r.  STEP 1: skeleton.
 *
 * Study example for docs/firmware-r246.md.  NOT TESTED ON HARDWARE.  The original op1_246.op1 and the op1r
 * image can always be reflashed, so a lock-up only costs a reflash.
 *
 * This step registers a new sequencer "roll" next to op1r's euclid, hosted on the stock arpeggio (internal id 5,
 * screen id 0x2c).  It:
 *   - makes "roll" selectable in the sequencer list (the (8,'roll') row is added to op1_factory.db by build.sh),
 *   - installs a custom screen that draws a pitch x time grid with a moving playhead,
 *   - plays a fixed one-bar demo pattern in a loop (so you can hear and see that playback works),
 *   - sends the notes to the internal engine and MIDI, and releases them cleanly on stop / sequencer change.
 * Editing, custom loop length and saving come in the next steps.
 *
 * Integration mirrors op1r's euclid exactly (euclid is hosted on finger id 3, screen id 0x2d, and is flash
 * tested).  Every hook chains to op1r's hook so euclid keeps working.
 */
#include <stdint.h>

/* ---- firmware (R246) addresses; see fw.ld.  bfin-elf adds a leading underscore to C symbols. ---- */
typedef struct { int32_t x, y; } Pt;                 /* screen coords, 22.10 fixed point (1 px = 1<<10) */
typedef struct { int32_t note, vel; uint8_t midi, pad[3]; int32_t user; } MidiNote;  /* 16 bytes */

extern volatile int32_t  fw_mode;                    /* 01de4c40: 0 = synth, 1 = drum (current mode) */
extern volatile int32_t  fw_cur_screen;              /* 018b65c8: id of the screen being shown */
extern void             *fw_screen_reg[];            /* 01df4e54: screen registry, indexed by screen id */
extern volatile uint8_t  op1r_euclid_flag[];         /* 01e158d0: op1r's euclid-active flag [synth, drum] */

extern void *fw_scr_raster(void);                                      /* 01a3b490 */
extern void  fw_line(void *raster, const Pt *a, const Pt *b, int32_t width, const uint8_t rgba[4]); /* 01a24a4c */
extern void  fw_screen_ctor(void *obj, int32_t id, const char *svg);   /* 01a3bab0 */
extern void  fw_note_on(const MidiNote *, int32_t drum_semi);          /* 01a85750 */
extern void  fw_note_off(const MidiNote *);                            /* 01a85840 */
extern void *fw_arp_instance(void);                                    /* 01a46400 (stock arp singleton) */

/* op1r hooks we chain to (fixed in the op1r 5 image; build.sh checks them) */
extern int32_t     op1r_id_from_name(const void *s);   /* 01e007a8 */
extern const char *op1r_name_from_id(int32_t id);      /* 01e0083c */
extern void       *op1r_build_screens(void);           /* 01e00c04 */
extern void        op1r_init_screens(void);            /* 01e00eb4 */
extern void        op1r_key(int32_t key, int32_t pressed);   /* 01e010f0 */
extern void        op1r_encoder(int32_t enc, int16_t *delta);/* 01e01384 */

#define HOST_ID      5       /* internal id of arpeggio (we host on it) */
#define HOST_SCREEN  0x2c    /* arpeggio's screen id */

/* a std::string: inline buffer at +4 when capacity (+0x18) < 16, else a pointer; length at +0x14 */
typedef struct { uint32_t a; union { char buf[16]; char *ptr; } u; uint32_t len, cap; } te_string;
static const char *te_cstr(const te_string *s) { return s->cap < 16 ? s->u.buf : s->u.ptr; }
static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* ------------------------------------------------------------------ state (zeroed at load) */
#define NSTEPS         16
#define TICKS_PER_STEP 24    /* 1/16 note = 96/4 ticks; the tick slot is called 96x per quarter */

static uint8_t  roll_flag[2];        /* roll active for [synth, drum] */
static uint8_t  roll_ready;          /* screen built and initialised */
static void    *roll_stock_arp_scr;  /* arpeggio's own screen object (to swap back) */

static uint32_t roll_tick_count[2];
static uint8_t  roll_playstep[2];
static uint8_t  roll_on_note[2];     /* 0 = none, else sounding note + 1 */

/* one-bar demo pattern per mode; -1 = rest.  Values are engine note numbers (synth: 0..127; drum: 53..76). */
static const signed char demo[2][NSTEPS] = {
    { 60, -1, 64, -1, 67, -1, 64, -1, 69, -1, 67, 64, 62, -1, 60, -1 },   /* synth */
    { 53, -1, -1, 55, 53, -1, 55, -1, 53, -1, -1, 55, 53, 55, 53, -1 },   /* drum  */
};
static const int32_t disp_top[2] = { 72, 60 };   /* top grid row note, per mode */

/* screen object (16 bytes: doc, id, loader, vptr) built by fw_screen_ctor, plus a RAM vtable copy */
static uint32_t roll_scr_obj[4];
static uint32_t roll_scr_vt[10];

/* ------------------------------------------------------------------ playback */
static void note_off(int m) {
    if (roll_on_note[m]) {
        MidiNote mn = { roll_on_note[m] - 1, 100, 1, {0,0,0}, 0 };
        fw_note_off(&mn);
        roll_on_note[m] = 0;
    }
}

static void roll_tick(void *self, int32_t tick48) {
    int m = (fw_mode == 1);
    uint32_t t = roll_tick_count[m];
    (void) self; (void) tick48;
    if (t % TICKS_PER_STEP == 0) {
        int step = (t / TICKS_PER_STEP) % NSTEPS;
        note_off(m);
        int n = demo[m][step];
        if (n >= 0 && n < 128) {                 /* fw_note_on asserts 0<=note<128, so guard it */
            MidiNote mn = { n, 100, 1, {0,0,0}, 0 };
            fw_note_on(&mn, 0);
            roll_on_note[m] = n + 1;
        }
        roll_playstep[m] = step;
    }
    if (++roll_tick_count[m] >= TICKS_PER_STEP * NSTEPS)
        roll_tick_count[m] = 0;
}

static void roll_stop(void *self) {              /* release everything; must be idempotent */
    (void) self;
    for (int m = 0; m < 2; m++) {
        note_off(m);
        roll_tick_count[m] = 0;
        roll_playstep[m] = 0;
    }
}

static int32_t roll_key_down(void *self, int32_t key, int32_t shift) {
    (void) self; (void) key; (void) shift;
    return 0;                                    /* step 1: let the keyboard sound live */
}
static void roll_nop(void) {}

/* player vtable: 8 slots of {int32 this-adjust, fn}; adjusts stay 0 */
static const void *roll_player_vt[16] = {
    0, 0,                        /* +00 */
    0, (void*) roll_nop,         /* +0c dtor */
    0, (void*) roll_key_down,    /* +14 key down */
    0, (void*) roll_nop,         /* +1c key up */
    0, (void*) roll_stop,        /* +24 leave mode (release all, safe superset) */
    0, (void*) roll_tick,        /* +2c tick */
    0, (void*) roll_nop,         /* +34 frame */
    0, (void*) roll_stop,        /* +3c stop */
};
static const void *roll_player[1] = { roll_player_vt };

/* ------------------------------------------------------------------ screen */
#define X0   (16 << 10)
#define Y0   (16 << 10)
#define RH   (8  << 10)
#define GW   (300 << 10)           /* grid width */
#define SW   (GW / NSTEPS)

static const uint8_t COL_GRID[4] = { 0x6f, 0x71, 0x73, 0xff };
static const uint8_t COL_NOTE[4] = { 0x00, 0xed, 0x95, 0xff };
static const uint8_t COL_HEAD[4] = { 0xff, 0x3a, 0x5d, 0xff };

static void hline(void *r, int32_t x0, int32_t x1, int32_t yc, int32_t th, const uint8_t c[4]) {
    Pt a = { x0, yc }, b = { x1, yc };
    fw_line(r, &a, &b, th, c);
}
static void vline(void *r, int32_t xc, int32_t y0, int32_t y1, int32_t th, const uint8_t c[4]) {
    Pt a = { xc, y0 }, b = { xc, y1 };
    fw_line(r, &a, &b, th, c);
}

static void roll_draw(void *self, int32_t dt) {
    void *r;
    int m, s, top;
    (void) self; (void) dt;
    if (!roll_ready) return;
    r = fw_scr_raster();
    if (!r) return;
    m = (fw_mode == 1);
    top = disp_top[m];

    for (s = 0; s <= NSTEPS; s++)                /* vertical grid lines */
        vline(r, X0 + s * SW, Y0, Y0 + NSTEPS * RH, (s % 4 == 0 ? (2 << 9) : (1 << 9)), COL_GRID);

    for (s = 0; s < NSTEPS; s++) {               /* demo notes as filled bars */
        int n = demo[m][s];
        int row = top - n;
        if (n >= 0 && row >= 0 && row < NSTEPS) {
            int32_t yc = Y0 + row * RH + RH / 2;
            hline(r, X0 + s * SW + (1 << 10), X0 + (s + 1) * SW - (1 << 10), yc, RH - (2 << 10), COL_NOTE);
        }
    }

    vline(r, X0 + roll_playstep[m] * SW + SW / 2, Y0, Y0 + NSTEPS * RH, (3 << 9), COL_HEAD);
}

/* ------------------------------------------------------------------ hooks (each chains to op1r's) */

/* idFromName: own the name "roll" on the arpeggio id; otherwise defer to op1r (which handles euclid). */
int32_t rp_id_from_name(const te_string *s) {
    int m = (fw_mode == 1);
    if (streq(te_cstr(s), "roll")) {
        roll_flag[m] = 1;
        op1r_euclid_flag[m] = 0;                 /* we win this mode; make sure euclid releases it */
        return HOST_ID;
    }
    roll_flag[m] = 0;
    return op1r_id_from_name(s);                 /* sets/clears euclid's own flag */
}

/* nameFromId: report "roll" for our id when our flag is set; otherwise defer to op1r. */
const char *rp_name_from_id(int32_t id) {
    int m = (fw_mode == 1);
    if (id == HOST_ID && roll_flag[m])
        return "roll";
    return op1r_name_from_id(id);
}

/* the arpeggio singleton getter: return our player when roll is active for this mode. */
void *rp_arp_instance(void) {
    int m = (fw_mode == 1);
    return roll_flag[m] ? (void*) roll_player : fw_arp_instance();
}

/* keep the screen registry slot pointing at our screen while roll is active for the current mode */
static void roll_update_slot(void) {
    if (roll_ready)
        fw_screen_reg[HOST_SCREEN] = roll_flag[fw_mode == 1] ? (void*) roll_scr_obj : roll_stock_arp_scr;
}
static int roll_showing(void) {
    return roll_ready && fw_cur_screen == HOST_SCREEN && roll_flag[fw_mode == 1];
}

/* build our screen after op1r builds its own (runs once at boot) */
void *rp_build_screens(void) {
    void *ret = op1r_build_screens();
    int i;
    roll_flag[0] = roll_flag[1] = 0;
    roll_on_note[0] = roll_on_note[1] = 0;
    roll_tick_count[0] = roll_tick_count[1] = 0;
    roll_playstep[0] = roll_playstep[1] = 0;

    roll_stock_arp_scr = fw_screen_reg[HOST_SCREEN];      /* save arpeggio's own screen */
    fw_screen_ctor(roll_scr_obj, HOST_SCREEN, "roll.svg");/* this overwrites the slot with ours */
    fw_screen_reg[HOST_SCREEN] = roll_stock_arp_scr;      /* restore it; we swap in on demand */

    for (i = 0; i < 10; i++)                              /* base screen vtable (01911474) + our draw */
        roll_scr_vt[i] = ((const uint32_t*) 0x01911474)[i];
    roll_scr_vt[7] = (uint32_t) roll_draw;                /* slot +0x1c = draw */
    roll_scr_obj[3] = (uint32_t) roll_scr_vt;             /* obj+0xc = vptr */
    return ret;
}

/* after op1r initialises its screens */
void rp_init_screens(void) {
    op1r_init_screens();
    roll_ready = 1;
}

void rp_key(int32_t key, int32_t pressed) {
    op1r_key(key, pressed);                               /* euclid shortcuts + stock dispatch */
    roll_update_slot();
}

void rp_encoder(int32_t enc, int16_t *delta) {
    if (roll_showing())
        return;                                          /* step 1: swallow (no editing yet) */
    op1r_encoder(enc, delta);
}

/* force objcopy to emit the whole blob (incl. zeroed bss) even if it ends in .bss */
__attribute__((section(".tail"), used)) const uint32_t rp_tail = 0;
