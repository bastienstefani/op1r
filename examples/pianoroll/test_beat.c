/* Host test of the beat-aligned start (beat.h, the same code the OP-1 runs):
 *     cc -O2 -Wall -Wextra -o tb test_beat.c && ./tb
 * Simulates the stock clock: a counter modulo 48 read by the player tick each tick (96 per quarter), and clock
 * resets (rp_clock_reset) at random times, including the race where the reset lands between the read and the
 * increment.  An armed roll must always start on a quarter-note beat (a multiple of 96 ticks since the last
 * reset), and wait at most 95 ticks when no reset happens while it waits. */
#include <stdio.h>
#include <stdlib.h>
#include "beat.h"

int main(void) {
    long since = 0, at = 0, maxw = 0;
    int c48 = 0, armed = 0, rw = 0, fails = 0, starts = 0, resets = 0, races = 0;
    srand(7);
    for (long tick = 0; tick < 5000000; tick++) {
        int r = rand() % 4000, beat;
        if (r == 0) { c48 = 0; since = 0; clk_reset_pending = 1; resets++; rw = 1; }     /* reset before the read */
        if (!armed && rand() % 600 == 0) { armed = 1; at = tick; rw = 0; }            /* orange press */
        beat = clk_on_beat(c48);
        if (armed && beat) {
            starts++;
            if (since % 96) fails++;
            if (!rw && tick - at > maxw) maxw = tick - at;
            armed = 0;
        }
        if (r == 1) {                                                                 /* reset between the read */
            c48 = 0; since = 0; clk_reset_pending = 1; resets++; races++; rw = 1;     /* and the increment */
            c48 = 1; since = 1;
        } else {
            c48 = (c48 + 1) % 48; since++;
        }
    }
    printf("resets=%d (races=%d) starts=%d off-beat=%d max wait=%ld\n", resets, races, starts, fails, maxw);
    return fails != 0 || maxw > 95;
}
