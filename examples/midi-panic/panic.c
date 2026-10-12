/* MIDI panic for the OP-1 (R246): Shift + a key sends "All Sound Off" (CC 120) and "All Notes Off" (CC 123)
 * on the current MIDI channel. Study example for docs/firmware-r246.md, section 11 - not tested on hardware.
 *
 * Hook: the KeyHandler thread (019c2cc0) calls key_dispatch(key, pressed) at 019c2d3e for every key event;
 * build.sh redirects that CALL here. Every other event goes to the previous target (the stock dispatcher
 * 019b6c74, or op1r's own key hook when built on top of op1r).
 */

/* firmware functions/variables (addresses in fw.ld; build.sh defines fw_key_dispatch_prev) */
extern void fw_key_dispatch_prev(int key, int pressed);
extern void fw_midi_cc(int cc, int value);        /* 019e3004: cc and value must be < 128 */
extern volatile unsigned char fw_shift_held;      /* 01dd7f78: set while Shift (key 0x26) is down */

#ifndef PANIC_KEY
#define PANIC_KEY 0x1f                            /* F in the middle of the keyboard (note index 12) */
#endif

static unsigned char eaten;                       /* the press was ours: swallow its release too */

void key_hook(int key, int pressed)
{
    if (key == PANIC_KEY) {
        if (pressed && fw_shift_held) {
            eaten = 1;
            fw_midi_cc(120, 0);
            fw_midi_cc(123, 0);
            return;
        }
        if (!pressed && eaten) {
            eaten = 0;
            return;
        }
    }
    fw_key_dispatch_prev(key, pressed);
}
