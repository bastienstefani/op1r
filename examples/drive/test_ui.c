/* Screen preview on a PC: draws drive's screen with the same code as the OP-1 and prints the lines.
 *     cc -O2 -Wall -o tu test_ui.c && ./tu r0 14000 r1 100 k 22000 > lines.txt && python3 render_ui.py lines.txt out.png
 * Arguments (any order): rN V = param N (0..7) set to V (0..32767, page-1 params as normalised values),
 * p 0|1 = page, k V = signal peak (Q15), h V = animation phase. */
#include <stdio.h>
#include <stdlib.h>
#include "dsp.h"
#include "ui.h"
#include "screen.h"

static void dv_line(void *r, const Pt *a, const Pt *b, int32_t w, const uint8_t c[4]) {
    (void) r;
    printf("%d %d %d %d %d %d %d %d\n", a->x, a->y, b->x, b->y, w, c[0], c[1], c[2]);
}

static void (*const keep_dv_block)(DvState *, const DvParams *, int16_t *, int, int) __attribute__((unused)) = dv_block;

int main(int argc, char **argv) {
    DvView v = { { 0, 0, 16384, 32767, 0x5304, 0, 6553, 26214 }, 0, 0, 0 };
    int i;
    for (i = 1; i + 1 < argc; i += 2) {
        if (argv[i][0] == 'r') v.raw[atoi(argv[i] + 1) & 7] = atoi(argv[i + 1]);
        else if (argv[i][0] == 'p') v.page = atoi(argv[i + 1]);
        else if (argv[i][0] == 'k') v.peak = atoi(argv[i + 1]);
        else if (argv[i][0] == 'h') v.phase = atoi(argv[i + 1]);
    }
    dv_draw_screen(0, &v);
    return 0;
}
