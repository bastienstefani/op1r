/* Piano roll: beat tracking on the stock sequencer clock.  Included by roll.c and by test_beat.c (host test). */
#include <stdint.h>

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
