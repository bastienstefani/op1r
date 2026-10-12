/* Piano-roll sequencer for the OP-1 (R246), built on top of op1r.
 *
 * Study example for docs/firmware-r246.md.  The original op1_246.op1 and the op1r
 * image can always be reflashed, so a lock-up only costs a reflash.
 *
 * New sequencer "roll" hosted on the stock arpeggio (internal id 5, screen id 0x2c), exactly as op1r hosts
 * euclid on finger (id 3, screen 0x2d).  Every hook chains to op1r's so euclid keeps working.
 *
 * Step 1 (flash tested): the entry appears and is selectable, a grid screen shows a moving playhead.
 * Step 2 (flash tested): a real note model with polyphonic playback and editing:
 *   - time cursor (blue), pitch scroll (green), note length (white), loop length (red);
 *   - a keyboard key places a note of that pitch at the cursor with the current length (step input);
 *     Shift + key removes it;
 *   - blue press cycles the grid, green press clears the cursor column, white press the velocity;
 *   - orange press starts / stops playback, starting on the next beat of the sequencer clock (HOLD box:
 *     faded red when held, red once started or armed);
 *   - each note keeps its own start and length ("customizable duration"); the loop length is settable.
 * Step 3 (flash tested): pattern and settings of each mode are saved to op1.db (table rollseq) in the
 * background and reloaded at boot.
 * Beat-aligned start: the stock sequencer-clock reset (01a5e7d0) is replaced by rp_clock_reset, which does the
 * same stores and flags the reset, so roll_tick knows which half-beat tick 0 is the beat.
 */
#include <stdint.h>

/* ---- firmware (R246) addresses; see fw.ld.  bfin-elf adds a leading underscore to C symbols. ---- */
typedef struct { int32_t x, y; } Pt;                 /* screen coords, 22.10 fixed point (1 px = 1<<10) */
typedef struct { int32_t note, vel; uint8_t midi, pad[3]; int32_t user; } MidiNote;  /* 16 bytes */
/* a std::string: inline buffer at +4 when capacity (+0x18) < 16, else a pointer; length at +0x14 */
typedef struct { uint32_t a; union { char buf[16]; char *ptr; } u; uint32_t len, cap; } te_string;

extern volatile int32_t  fw_mode;                    /* 01de4c40: 0 = synth, 1 = drum */
extern volatile int32_t  fw_cur_screen;              /* 018b65c8: id of the screen being shown */
extern void             *fw_screen_reg[];            /* 01df4e54: screen registry, indexed by screen id */
extern volatile int32_t  fw_synth_slot;              /* ff8016d4: current synth sound slot */
extern volatile int8_t   fw_synth_oct[];             /* 01dfe4e0: per-slot synth octave, -4..+4 */

extern void *fw_scr_raster(void);                                      /* 01a3b490 */
extern void  fw_line(void *raster, const Pt *a, const Pt *b, int32_t width, const uint8_t rgba[4]); /* 01a24a4c */
extern void  fw_screen_ctor(void *obj, int32_t id, const char *svg);   /* 01a3bab0 */
extern void  fw_note_on(const MidiNote *, int32_t drum_semi);          /* 01a85750 */
extern void  fw_note_off(const MidiNote *);                            /* 01a85840 */
extern void *fw_arp_instance(void);                                    /* 01a46400 (stock arp singleton) */

extern void *fw_db_op1(void);                                          /* 0196dd44: op1.db handle */
extern void  fw_db_exec(void *db, const char *sql);                    /* 0196f950 */
extern void  fw_db_get_string(te_string *out, void *db, const char *sql); /* 0196fbd0 */
extern void  fw_bg_register(void *saveable);                           /* 0193f010 */
extern void  fw_bg_dirty(void *saveable);                              /* 0193ef3c */
extern void  fw_free(void *p);                                         /* 01a5e310 (operator delete) */

/* op1r 12 hooks we chain to: addresses in fw.ld, verified against the op1r image by build.sh */
extern int32_t     op1r_id_from_name(const void *s);
extern const char *op1r_name_from_id(int32_t id);
extern void       *op1r_build_screens(void);
extern void        op1r_init_screens(void);
/* key / encoder: the next hook in the chain, op1r's unless the image is built on another patch (see fw.ld) */
extern void        rp_next_key(int32_t key, int32_t pressed);
extern void        rp_next_encoder(int32_t enc, int16_t *delta);

#define HOST_ID      5
#define HOST_SCREEN  0x2c

static const char *te_cstr(const te_string *s) { return s->cap < 16 ? s->u.buf : s->u.ptr; }
static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* ------------------------------------------------------------------ model (zeroed at load) */
#define MAXNOTES   128                  /* per mode */
#define POLY       16                   /* max notes sounding at once */
#define PPQ        96                   /* ticks per quarter note (the tick slot runs 96x per quarter) */
#define MAXLOOP    (PPQ * 64)           /* 16 bars of 4/4 */

typedef struct { uint16_t start, len; uint8_t pitch, vel, used, pad; } Note;   /* 8 bytes */

static const uint16_t GRIDS[4] = { PPQ, PPQ / 2, PPQ / 4, PPQ / 8 };   /* 1/4 1/8 1/16 1/32 */
static const uint8_t  VELS[3]  = { 127, 100, 64 };

typedef struct {
    Note     note[MAXNOTES];
    uint16_t loop_len;                  /* ticks */
    uint16_t cur_t;                     /* time cursor, ticks */
    uint16_t cur_len;                   /* length applied to new notes */
    uint8_t  grid_i;                    /* index into GRIDS */
    uint8_t  vel_i;                     /* index into VELS */
    int8_t   view_top;                  /* note number of the top grid row */
    uint8_t  inited;
    volatile uint8_t run;               /* play requested (orange press); not saved: boots held */
    /* playback (written only by the sequencer thread) */
    uint16_t play_t;
    uint8_t  playing;
    struct { uint8_t note, used; uint16_t off_t; } snd[POLY];
} Roll;

static Roll roll[2];                    /* [synth, drum] */

static uint8_t  roll_ready;
static void    *roll_stock_arp_scr;
static uint8_t  roll_flag[2];

static uint32_t roll_scr_obj[4];
static uint32_t roll_scr_vt[10];

static uint16_t grid_of(Roll *R) { return GRIDS[R->grid_i & 3]; }
static void roll_dirty(void);                           /* mark the pattern for background saving */

static void roll_defaults(Roll *R) {
    int i;
    for (i = 0; i < MAXNOTES; i++) R->note[i].used = 0;
    for (i = 0; i < POLY; i++) R->snd[i].used = 0;
    R->grid_i = 2;                      /* 1/16 */
    R->vel_i = 1;                       /* 100 */
    R->loop_len = PPQ / 4 * 16;         /* one bar of 16 steps */
    R->cur_len = PPQ / 4;
    R->cur_t = 0;
    R->view_top = 72;
    R->play_t = 0;
    R->playing = 0;
    R->run = 0;
    R->inited = 1;
}

/* ------------------------------------------------------------------ playback (sequencer thread) */
static void snd_off_all(Roll *R) {
    int i;
    for (i = 0; i < POLY; i++)
        if (R->snd[i].used) {
            MidiNote mn = { R->snd[i].note, 100, 1, {0,0,0}, 0 };
            fw_note_off(&mn);
            R->snd[i].used = 0;
        }
}

/* Beat tracking on the stock sequencer clock (96 ticks per quarter).  The player tick receives the global
 * counter 018c1884, which counts ticks modulo 48 (half a beat); count its wraps to know which half we are in.
 * When the clock is reset (01a5e7d0: tape PLAY, MIDI Start, a stock sequencer starting) the counter jumps to
 * 0: a beat starts there.  Any other jump (e.g. after a mode switch) keeps the current guess. */
static uint8_t clk_last48 = 0xff;               /* 0xff: nothing seen yet */
static uint8_t clk_half;                        /* 0: first half of the beat, 1: second half */
volatile uint8_t clk_reset_pending;             /* set by rp_clock_reset (any thread) */
static int clk_on_beat(int32_t tick48) {
    unsigned t = (unsigned) tick48 % 48;
    if (clk_reset_pending) {                    /* the clock was reset: beat 1 starts at its first tick */
        clk_reset_pending = 0;
        clk_half = 0;
    } else if (clk_last48 != 0xff && t == (clk_last48 + 1u) % 48) {
        if (t == 0) clk_half ^= 1;
    } else if (t == 0) {
        clk_half = 0;                           /* jumped to 0 without the hook seeing it: treat as a reset */
    }
    clk_last48 = (uint8_t) t;
    return t == 0 && clk_half == 0;
}

/* Replacement for the stock sequencer-clock reset 01a5e7d0 (10 callers: tape PLAY, MIDI Start, stock
 * sequencers starting).  Same three stores as the original, plus a flag so a reset that happens exactly when
 * the counter would wrap 47 -> 0 by itself is not missed.  Written in assembly to use only P1 and R0, the
 * registers the original clobbers, so its callers see no difference. */
__asm__(
"    .text\n"
"    .align 4\n"
"    .global _rp_clock_reset\n"
"    .type _rp_clock_reset, STT_FUNC\n"
"_rp_clock_reset:\n"
"    P1.L = 0x187c;\n"
"    P1.H = 0x018c;\n"
"    R0 = 0 (X);\n"
"    [P1] = R0;\n"
"    [P1 + 4] = R0;\n"
"    [P1 + 8] = R0;\n"
"    P1.H = _clk_reset_pending;\n"
"    P1.L = _clk_reset_pending;\n"
"    R0 = 1 (X);\n"
"    B [P1] = R0;\n"
"    rts;\n"
"    .size _rp_clock_reset, .-_rp_clock_reset\n"
);

static void roll_tick(void *self, int32_t tick48) {
    Roll *R = &roll[fw_mode == 1];
    uint16_t t;
    int i, beat;
    (void) self;
    beat = clk_on_beat(tick48);                 /* track every tick, even on HOLD */
    if (!R->run) {                              /* HOLD: release once, then wait for the orange press */
        if (R->playing) { snd_off_all(R); R->play_t = 0; R->playing = 0; }
        return;
    }
    if (!R->playing) {                          /* armed: start from the top of the loop on the next beat */
        if (!beat) return;
        R->play_t = 0;
        R->playing = 1;
    }
    t = R->play_t;

    for (i = 0; i < POLY; i++)                      /* note-off at gate end */
        if (R->snd[i].used && R->snd[i].off_t == t) {
            MidiNote mn = { R->snd[i].note, 100, 1, {0,0,0}, 0 };
            fw_note_off(&mn);
            R->snd[i].used = 0;
        }
    for (i = 0; i < MAXNOTES; i++) {                /* note-on at start */
        Note *n = &R->note[i];
        if (n->used && n->start == t && n->start < R->loop_len && n->pitch < 128) {
            uint16_t off = n->start + (n->len ? n->len : 1);
            int j;
            if (off > R->loop_len) off = R->loop_len;
            { MidiNote mn = { n->pitch, n->vel ? n->vel : 100, 1, {0,0,0}, 0 }; fw_note_on(&mn, 0); }
            for (j = 0; j < POLY; j++)
                if (!R->snd[j].used) { R->snd[j].note = n->pitch; R->snd[j].off_t = off; R->snd[j].used = 1; break; }
        }
    }
    if (++R->play_t >= R->loop_len) {               /* wrap: release everything to avoid hangs */
        R->play_t = 0;
        snd_off_all(R);
    }
}

static void roll_stop(void *self) {                 /* must be idempotent; releases both modes */
    int m;
    (void) self;
    for (m = 0; m < 2; m++) {
        snd_off_all(&roll[m]);
        roll[m].play_t = 0;
        roll[m].playing = 0;
        roll[m].run = 0;                            /* any external stop puts the roll on HOLD */
    }
}

/* ------------------------------------------------------------------ editing (KeyHandler thread) */
static int note_at(Roll *R, int pitch, uint16_t t) {    /* index of a used note of this pitch covering t, or -1 */
    int i;
    for (i = 0; i < MAXNOTES; i++) {
        Note *n = &R->note[i];
        if (n->used && n->pitch == pitch && n->start <= t && t < n->start + (n->len ? n->len : 1))
            return i;
    }
    return -1;
}

static void place_note(Roll *R, int pitch, uint16_t t, uint16_t len, uint8_t vel) {
    int i, slot = -1;
    if (pitch < 0 || pitch > 127) return;
    for (i = 0; i < MAXNOTES; i++)                      /* reuse a note with the same start+pitch */
        if (R->note[i].used && R->note[i].pitch == pitch && R->note[i].start == t) { slot = i; break; }
    if (slot < 0)
        for (i = 0; i < MAXNOTES; i++) if (!R->note[i].used) { slot = i; break; }
    if (slot < 0) return;                               /* full: ignore */
    R->note[slot].used = 0;                             /* publish fields before marking used */
    R->note[slot].start = t;
    R->note[slot].len = len;
    R->note[slot].pitch = pitch;
    R->note[slot].vel = vel;
    R->note[slot].used = 1;
}

/* player key_down: on the roll screen, a key places a note (Shift removes); otherwise it sounds normally. */
static int32_t roll_key_down(void *self, int32_t key, int32_t shift) {
    Roll *R = &roll[fw_mode == 1];
    int pitch;
    (void) self;
    if (!(roll_ready && fw_cur_screen == HOST_SCREEN && roll_flag[fw_mode == 1]))
        return 0;                                       /* not editing: let it sound live */
    if (key < 0 || key > 23) return 0;
    pitch = key + 53;
    if (fw_mode == 0) {                                 /* synth: apply the keyboard octave */
        int oct = fw_synth_oct[fw_synth_slot & 7];
        pitch += 12 * oct;
    }
    if (pitch < 0 || pitch > 127) return shift ? 1 : 0;
    if (shift) {
        int i = note_at(R, pitch, R->cur_t);
        if (i >= 0) { R->note[i].used = 0; roll_dirty(); }
        return 1;                                       /* swallow: no live sound on delete */
    }
    place_note(R, pitch, R->cur_t, R->cur_len, VELS[R->vel_i % 3]);
    R->view_top = (int8_t) (pitch + 2 > 127 ? 127 : pitch + 2);     /* keep the note in view */
    R->cur_t = (uint16_t) ((R->cur_t + R->cur_len) % R->loop_len);  /* step input: advance */
    roll_dirty();
    return 0;                                           /* also sound it live as feedback */
}

static void roll_nop(void) {}

/* ------------------------------------------------------------------ player vtable */
static const void *roll_player_vt[16] = {
    0, 0,
    0, (void*) roll_nop,         /* +0c dtor */
    0, (void*) roll_key_down,    /* +14 key down */
    0, (void*) roll_nop,         /* +1c key up */
    0, (void*) roll_stop,        /* +24 leave mode */
    0, (void*) roll_tick,        /* +2c tick */
    0, (void*) roll_nop,         /* +34 frame */
    0, (void*) roll_stop,        /* +3c stop */
};
static const void *roll_player[1] = { roll_player_vt };

/* ------------------------------------------------------------------ screen (display thread) */
#define X0    (14 << 10)
#define Y0    (22 << 10)
#define ROWS  16
#define RH    (8 << 10)
#define GW    (300 << 10)
#define WINS  16                        /* visible grid columns */

static const uint8_t COL_GRID[4] = { 0x6f, 0x71, 0x73, 0xff };
static const uint8_t COL_BAR [4] = { 0x00, 0x52, 0x34, 0xff };
static const uint8_t COL_NOTE[4] = { 0x00, 0xed, 0x95, 0xff };
static const uint8_t COL_CUR [4] = { 0x4d, 0x9e, 0xff, 0xff };
static const uint8_t COL_HEAD[4] = { 0xff, 0x3a, 0x5d, 0xff };
static const uint8_t COL_HOLD[4] = { 0x5c, 0x17, 0x25, 0xff };   /* faded red: held */
static const uint8_t COL_TEXT[4] = { 0xff, 0xff, 0xff, 0xff };

static void hline(void *r, int32_t x0, int32_t x1, int32_t yc, int32_t th, const uint8_t c[4]) {
    Pt a = { x0, yc }, b = { x1, yc }; fw_line(r, &a, &b, th, c);
}
static void vline(void *r, int32_t xc, int32_t y0, int32_t y1, int32_t th, const uint8_t c[4]) {
    Pt a = { xc, y0 }, b = { xc, y1 }; fw_line(r, &a, &b, th, c);
}

static void num_str(char *out, unsigned v) {            /* up to 3 digits */
    int i = 0; char t[4];
    if (!v) { out[0] = '0'; out[1] = 0; return; }
    while (v && i < 3) { t[i++] = '0' + v % 10; v /= 10; }
    { int j = 0; while (i) out[j++] = t[--i]; out[j] = 0; }
}
/* a duration in ticks as a reduced fraction of a whole note: 24 -> "1/16", 72 -> "3/16", 768 -> "2/1" */
static void frac_str(char *out, unsigned ticks) {
    unsigned d = PPQ * 4, a = ticks, b = d, t;
    while (b) { t = a % b; a = b; b = t; }              /* a = gcd(ticks, 384) */
    if (!a) { out[0] = '0'; out[1] = 0; return; }
    num_str(out, ticks / a);
    while (*out) out++;
    *out++ = '/';
    num_str(out, d / a);
}

/* Own 7-segment digits and '/', drawn with the stock fw_line only (no op1r-internal font: op1r's code block,
 * and every function inside it, moves with each op1r version). x, y = top-left in px. */
static const uint8_t SEG7[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };  /* a..g = bit 0..6 */
/* letters as strokes in the same 5x8 box: x0,y0,x1,y1 in px, -1 ends */
static const int8_t GL_H[] = { 0,0,0,8, 5,0,5,8, 0,4,5,4, -1 };
static const int8_t GL_O[] = { 0,0,5,0, 5,0,5,8, 5,8,0,8, 0,8,0,0, -1 };
static const int8_t GL_L[] = { 0,0,0,8, 0,8,5,8, -1 };
static const int8_t GL_D[] = { 0,0,0,8, 0,0,3,0, 3,0,5,2, 5,2,5,6, 5,6,3,8, 3,8,0,8, -1 };
static void strokes(void *r, int32_t X, int32_t Y, const int8_t *g, int32_t th, const uint8_t c[4]) {
    for (; g[0] >= 0; g += 4) {
        Pt p0 = { X + (g[0] << 10), Y + (g[1] << 10) }, p1 = { X + (g[2] << 10), Y + (g[3] << 10) };
        fw_line(r, &p0, &p1, th, c);
    }
}
static void seg_text(void *r, int x, int y, const char *s, const uint8_t c[4]) {
    const int32_t w = 5 << 10, h = 8 << 10, m = 4 << 10, th = 5 << 8;     /* 5x8 px glyphs, 1.25 px strokes */
    for (; *s; s++, x += 7) {
        int32_t X = x << 10, Y = y << 10;
        unsigned b;
        if (*s == '/') { Pt p0 = { X, Y + h }, p1 = { X + w, Y }; fw_line(r, &p0, &p1, th, c); continue; }
        if (*s == 'H') { strokes(r, X, Y, GL_H, th, c); continue; }
        if (*s == 'O') { strokes(r, X, Y, GL_O, th, c); continue; }
        if (*s == 'L') { strokes(r, X, Y, GL_L, th, c); continue; }
        if (*s == 'D') { strokes(r, X, Y, GL_D, th, c); continue; }
        if (*s < '0' || *s > '9') continue;
        b = SEG7[*s - '0'];
        if (b & 0x01) hline(r, X, X + w, Y, th, c);
        if (b & 0x02) vline(r, X + w, Y, Y + m, th, c);
        if (b & 0x04) vline(r, X + w, Y + m, Y + h, th, c);
        if (b & 0x08) hline(r, X, X + w, Y + h, th, c);
        if (b & 0x10) vline(r, X, Y + m, Y + h, th, c);
        if (b & 0x20) vline(r, X, Y, Y + m, th, c);
        if (b & 0x40) hline(r, X, X + w, Y + m, th, c);
    }
}

static void roll_draw(void *self, int32_t dt) {
    Roll *R;
    void *r;
    int m, i, s, win_ticks, win_start, grid, gy1;
    char tmp[12];
    (void) self; (void) dt;
    if (!roll_ready) return;
    r = fw_scr_raster();
    if (!r) return;
    m = (fw_mode == 1);
    R = &roll[m];
    grid = grid_of(R);
    win_ticks = WINS * grid;
    gy1 = (22 + ROWS * 8) << 10;

    /* horizontal scroll so the cursor stays on screen */
    win_start = (R->cur_t / win_ticks) * win_ticks;

    /* vertical grid: one line per grid step, brighter on the beat */
    for (s = 0; s <= WINS; s++) {
        int32_t x = X0 + (int32_t) ((int64_t) s * grid * GW / win_ticks);
        int beat = ((win_start + s * grid) % PPQ) == 0;
        vline(r, x, Y0, gy1, beat ? (2 << 9) : (1 << 9), COL_GRID);
    }
    /* a faint baseline every octave (C) */
    for (i = 0; i < ROWS; i++) {
        int note = R->view_top - i;
        if (note >= 0 && note % 12 == 0)
            hline(r, X0, X0 + GW, Y0 + i * RH + RH, 1 << 9, COL_BAR);
    }

    /* notes */
    for (i = 0; i < MAXNOTES; i++) {
        Note *n = &R->note[i];
        int row, e;
        int32_t x0, x1, yc;
        if (!n->used) continue;
        row = R->view_top - n->pitch;
        if (row < 0 || row >= ROWS) continue;
        e = n->start + (n->len ? n->len : 1);
        if (e <= win_start || n->start >= win_start + win_ticks) continue;   /* outside the window */
        { int a = n->start < win_start ? win_start : n->start;
          int b = e > win_start + win_ticks ? win_start + win_ticks : e;
          x0 = X0 + (int32_t) ((int64_t) (a - win_start) * GW / win_ticks);
          x1 = X0 + (int32_t) ((int64_t) (b - win_start) * GW / win_ticks); }
        yc = Y0 + row * RH + RH / 2;
        hline(r, x0 + (1 << 9), x1 - (1 << 9), yc, RH - (2 << 10), COL_NOTE);
    }

    /* time cursor */
    if (R->cur_t >= win_start && R->cur_t < win_start + win_ticks) {
        int32_t x = X0 + (int32_t) ((int64_t) (R->cur_t - win_start) * GW / win_ticks);
        vline(r, x, Y0, gy1, 2 << 9, COL_CUR);
    }
    /* playhead */
    if (R->run && R->playing && R->play_t >= win_start && R->play_t < win_start + win_ticks) {
        int32_t x = X0 + (int32_t) ((int64_t) (R->play_t - win_start) * GW / win_ticks);
        vline(r, x, Y0, gy1, 3 << 9, COL_HEAD);
    }

    /* header readouts: grid, length of new notes (fractions of a whole note), loop length in grid steps */
    frac_str(tmp, grid);              seg_text(r, 4,   5, tmp, COL_TEXT);
    frac_str(tmp, R->cur_len);        seg_text(r, 70,  5, tmp, COL_TEXT);
    num_str(tmp, R->loop_len / grid); seg_text(r, 140, 5, tmp, COL_TEXT);

    /* HOLD box, top right: faded red while held, red while playing (orange press toggles) */
    {
        const uint8_t *c = R->run ? COL_HEAD : COL_HOLD;
        const int32_t th = 5 << 8, e = th / 2;
        const int32_t bx0 = 277 << 10, bx1 = 311 << 10, by0 = 2 << 10, by1 = 16 << 10;
        hline(r, bx0 - e, bx1 + e, by0, th, c);
        hline(r, bx0 - e, bx1 + e, by1, th, c);
        vline(r, bx0, by0, by1, th, c);
        vline(r, bx1, by0, by1, th, c);
        seg_text(r, 281, 5, "HOLD", c);
    }
}

/* ------------------------------------------------------------------ persistence (op1.db table "rollseq") */
/* Same mechanism as op1r's euclid: a bg_saver object the FileWorker thread calls when marked dirty, and one
 * TEXT row per mode.  The payload is plain decimals separated by commas (no '%' or quote, which db2's double
 * vsnprintf would choke on).  Addresses are stock R246 (unchanged by op1r). */
static struct { uint8_t dirty, pending, pad[2]; const void *vptr; } roll_saver;
static char sqlbuf[8192];

static char *put_uint(char *p, unsigned v) {
    char t[10]; int i = 0;
    if (!v) { *p++ = '0'; return p; }
    while (v && i < 10) { t[i++] = '0' + v % 10; v /= 10; }
    while (i) *p++ = t[--i];
    return p;
}
static char *put_str(char *p, const char *s) { while (*s) *p++ = *s++; return p; }

static int rd_uint(const char **pp, unsigned *out) {
    const char *p = *pp; unsigned v = 0;
    while (*p && (*p < '0' || *p > '9')) p++;
    if (!*p) return 0;
    while (*p >= '0' && *p <= '9') v = v * 10 + (unsigned) (*p++ - '0');
    *pp = p; *out = v; return 1;
}

static void roll_save(void *obj) {
    void *db = fw_db_op1();
    int m, i;
    (void) obj;
    if (!db) return;
    for (m = 0; m < 2; m++) {
        Roll *R = &roll[m];
        char *p = sqlbuf;
        p = put_str(p, "INSERT OR REPLACE INTO rollseq VALUES (");
        p = put_uint(p, m);
        p = put_str(p, ",'");
        p = put_uint(p, R->loop_len); *p++ = ',';
        p = put_uint(p, R->grid_i);   *p++ = ',';
        p = put_uint(p, R->vel_i);    *p++ = ',';
        p = put_uint(p, (uint8_t) R->view_top); *p++ = ',';
        p = put_uint(p, R->cur_len);
        for (i = 0; i < MAXNOTES; i++) {
            Note *n = &R->note[i];
            if (!n->used) continue;
            if (p > sqlbuf + sizeof(sqlbuf) - 32) break;    /* keep well under db2's 8 KB limit */
            *p++ = ','; p = put_uint(p, n->start);
            *p++ = ','; p = put_uint(p, n->len);
            *p++ = ','; p = put_uint(p, n->pitch);
            *p++ = ','; p = put_uint(p, n->vel);
        }
        p = put_str(p, "')");
        *p = 0;
        fw_db_exec(db, sqlbuf);
    }
}

static void roll_load(void *db, int m) {
    te_string buf;
    const char *s;
    unsigned v;
    Roll *R = &roll[m];
    int g, i;
    char *p = sqlbuf;
    buf.cap = 0; buf.len = 0; buf.u.buf[0] = 0;             /* so te_cstr() is safe if nothing is written */
    p = put_str(p, "SELECT data FROM rollseq WHERE is_synth=");
    p = put_uint(p, m); *p = 0;
    fw_db_get_string(&buf, db, sqlbuf);
    s = te_cstr(&buf);
    if (rd_uint(&s, &v)) {                                  /* a row exists: rebuild the model */
        R->loop_len = v;                                    /* clamped below, once grid is known */
        if (rd_uint(&s, &v)) R->grid_i = v & 3;
        if (rd_uint(&s, &v)) R->vel_i = v % 3;
        if (rd_uint(&s, &v)) R->view_top = (int8_t) (v > 127 ? 127 : (v < ROWS - 1 ? ROWS - 1 : v));
        if (rd_uint(&s, &v)) R->cur_len = v;
        g = grid_of(R);
        if (R->loop_len < g) R->loop_len = g;
        if (R->loop_len > MAXLOOP) R->loop_len = MAXLOOP;
        if (R->cur_len < g) R->cur_len = g;
        if (R->cur_len > R->loop_len) R->cur_len = R->loop_len;
        for (i = 0; i < MAXNOTES; i++) R->note[i].used = 0;
        i = 0;
        while (i < MAXNOTES) {
            unsigned st, ln, pi, ve;
            if (!rd_uint(&s, &st) || !rd_uint(&s, &ln) || !rd_uint(&s, &pi) || !rd_uint(&s, &ve)) break;
            if (st >= MAXLOOP || pi > 127) continue;
            R->note[i].start = (uint16_t) st;
            R->note[i].len = (uint16_t) (ln ? ln : 1);
            R->note[i].pitch = (uint8_t) pi;
            R->note[i].vel = (uint8_t) (ve ? ve : 100);
            R->note[i].used = 1;
            i++;
        }
    }
    if (buf.cap >= 16) fw_free(buf.u.ptr);
}

static void roll_dirty(void) {
    if (roll_ready) fw_bg_dirty(&roll_saver);
}
static const void *roll_saver_vt[6] = { 0, 0, 0, (void*) roll_nop, 0, (void*) roll_save };

/* ------------------------------------------------------------------ hooks (each chains to op1r's) */
/* A valid stock name ("arpeggio", our host), as a std::string with the inline buffer (cap < 16).  Never pass
 * "roll" itself to op1r's hook: for a non-euclid name it jumps to the stock idFromName (01a42344), which asserts
 * "SeqIds.cpp:41 unknown sequencer name" and halts - at boot, when "roll" is the saved sequencer. */
static te_string host_name = { 0, { "arpeggio" }, 8, 15 };

int32_t rp_id_from_name(const te_string *s) {
    int m = (fw_mode == 1);
    if (streq(te_cstr(s), "roll")) {
        if (!roll[m].inited) roll_defaults(&roll[m]);
        op1r_id_from_name(&host_name);  /* side effect: op1r clears its euclid / dots flags for this mode */
        roll_flag[m] = 1;
        return HOST_ID;
    }
    roll_flag[m] = 0;
    return op1r_id_from_name(s);     /* sets/clears euclid's own flag and returns its id */
}
const char *rp_name_from_id(int32_t id) {
    int m = (fw_mode == 1);
    if (id == HOST_ID && roll_flag[m]) return "roll";
    return op1r_name_from_id(id);
}
void *rp_arp_instance(void) {
    int m = (fw_mode == 1);
    return roll_flag[m] ? (void*) roll_player : fw_arp_instance();
}

static void roll_update_slot(void) {
    if (roll_ready)
        fw_screen_reg[HOST_SCREEN] = roll_flag[fw_mode == 1] ? (void*) roll_scr_obj : roll_stock_arp_scr;
}
static int roll_showing(void) {
    return roll_ready && fw_cur_screen == HOST_SCREEN && roll_flag[fw_mode == 1];
}

void *rp_build_screens(void) {
    void *ret = op1r_build_screens();
    int i;
    /* The saved sequencer is loaded BEFORE the screens are built (OP_Main: Core init -> 01a42ba0 -> idFromName),
     * so roll_flag may already be set here: keep it (bss starts at 0) and only fill state not yet initialised. */
    if (!roll[0].inited) roll_defaults(&roll[0]);
    if (!roll[1].inited) roll_defaults(&roll[1]);
    roll_stock_arp_scr = fw_screen_reg[HOST_SCREEN];
    fw_screen_ctor(roll_scr_obj, HOST_SCREEN, "roll.svg");
    fw_screen_reg[HOST_SCREEN] = roll_stock_arp_scr;
    for (i = 0; i < 10; i++) roll_scr_vt[i] = ((const uint32_t*) 0x01911474)[i];
    roll_scr_vt[7] = (uint32_t) roll_draw;              /* slot +0x1c = draw */
    roll_scr_obj[3] = (uint32_t) roll_scr_vt;

    {                                                   /* persistence: register the saver, load saved rows */
        void *db = fw_db_op1();
        fw_bg_register(&roll_saver);
        roll_saver.vptr = roll_saver_vt;
        if (db) {
            fw_db_exec(db, "CREATE TABLE IF NOT EXISTS rollseq (is_synth INTEGER PRIMARY KEY, data TEXT NOT NULL)");
            roll_load(db, 0);
            roll_load(db, 1);
        }
    }
    return ret;
}
void rp_init_screens(void) {
    op1r_init_screens();
    roll_ready = 1;
    roll_update_slot();                                 /* roll may already be the saved sequencer */
}

/* encoder-press key codes (blue/green/white/red) while the roll screen is up */
#define K_LEFT 0x24
#define K_RIGHT 0x25
static void roll_press(int enc) {
    Roll *R = &roll[fw_mode == 1];
    int i;
    switch (enc) {
    case 0:                                             /* blue: cycle grid */
        R->grid_i = (R->grid_i + 1) & 3;
        if (R->cur_len < grid_of(R)) R->cur_len = grid_of(R);
        break;
    case 2:                                             /* white: cycle velocity */
        R->vel_i = (R->vel_i + 1) % 3;
        break;
    case 1:                                             /* green: clear the cursor column */
        for (i = 0; i < MAXNOTES; i++) {
            Note *n = &R->note[i];
            if (n->used && n->start <= R->cur_t && R->cur_t < n->start + (n->len ? n->len : 1))
                n->used = 0;
        }
        break;
    case 3:                                             /* orange: play / hold (the sequencer thread acts on it) */
        R->run = !R->run;
        return;                                         /* transport state is not saved */
    default: break;
    }
    roll_dirty();
}

void rp_key(int32_t key, int32_t pressed) {
    if (roll_showing() && pressed && key >= 0x3b && key <= 0x3e) {
        roll_press(key - 0x3b);                         /* encoder presses: handle and swallow */
        return;
    }
    rp_next_key(key, pressed);
    roll_update_slot();
}

void rp_encoder(int32_t enc, int16_t *delta) {
    Roll *R;
    int n;
    if (!roll_showing()) { rp_next_encoder(enc, delta); return; }
    R = &roll[fw_mode == 1];
    n = (int) *delta / 1024;                            /* one detent = 0x400 */
    if (!n) return;
    switch (enc) {
    case 0: {                                           /* blue: time cursor */
        int t = (int) R->cur_t + n * grid_of(R);
        while (t < 0) t += R->loop_len;
        R->cur_t = (uint16_t) (t % R->loop_len);
        break;
    }
    case 1: {                                           /* green: scroll pitch view */
        int v = R->view_top - n;
        if (v < ROWS - 1) v = ROWS - 1;
        if (v > 127) v = 127;
        R->view_top = (int8_t) v;
        break;
    }
    case 2: {                                           /* white: note length */
        int g = grid_of(R);
        int l = (int) R->cur_len + n * g;
        if (l < g) l = g;
        if (l > R->loop_len) l = R->loop_len;
        R->cur_len = (uint16_t) l;
        roll_dirty();
        break;
    }
    case 3: {                                           /* red: loop length */
        int g = grid_of(R);
        int l = (int) R->loop_len + n * g;
        if (l < g) l = g;
        if (l > MAXLOOP) l = MAXLOOP;
        R->loop_len = (uint16_t) l;
        if (R->cur_t >= R->loop_len) R->cur_t = 0;
        roll_dirty();
        break;
    }
    default: break;
    }
}

__attribute__((section(".tail"), used)) const uint32_t rp_tail = 0;
