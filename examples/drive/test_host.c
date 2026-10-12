/* Host test of dsp.h (the same code the OP-1 runs):
 *     cc -O2 -Wall -Wextra -fsanitize=undefined,signed-integer-overflow -fno-sanitize-recover -o t test_host.c && ./t
 * - sweeps every mode x drive x filter route/type x resonance x mix x level with sines, noise and full-scale
 *   steps: no signed overflow (UBSan aborts), output stays in int16, the filter does not blow up;
 * - prints per-mode figures (output RMS for a -6 dB sine, DC) and writes curves.csv (transfer curves). */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "dsp.h"

static const char *NAMES[DV_NMODES] = { "tube", "soft", "hard", "diode1", "diode2", "linfold", "sinfold",
                                         "zerosq", "downs", "asym", "rect", "xshp", "xshpa" };

static int16_t buf[64];

int main(void) {
    static const int32_t V[] = { 0, 1, 1000, 8192, 16384, 24576, 32000, 32767 };
    int nv = sizeof V / sizeof V[0], fails = 0;
    long blocks = 0;
    unsigned seed = 1;
    for (int mode = 0; mode < DV_NMODES; mode++)
    for (int di = 0; di < nv; di++)
    for (int rt = 0; rt < 9; rt++)
    for (int ri = 0; ri < nv; ri += 3)
    for (int fi = 0; fi < nv; fi += 2) {
        DvParams P;
        DvState S = { 0 };
        dv_decode(&P, V[di], mode * 32768 / DV_NMODES + 100, V[fi], 32767, rt * 32768 / 9 + 100, V[ri], 32767);
        if (P.mode != mode || P.route != rt / 3 || P.ftype != rt % 3) { printf("decode mismatch\n"); return 1; }
        for (int sig = 0; sig < 4; sig++) {
            double ph = 0;
            int32_t maxabs = 0;
            for (int b = 0; b < 400; b++) {
                for (int i = 0; i < 64; i++) {
                    int32_t s;
                    switch (sig) {
                    case 0: s = (int32_t) (32767 * sin(ph)); ph += 2 * M_PI * 110 / 44100; break;
                    case 1: s = (int32_t) (32767 * sin(ph)); ph += 2 * M_PI * 9000 / 44100; break;
                    case 2: seed = seed * 1103515245u + 12345u; s = (int16_t) (seed >> 16); break;
                    default: s = ((b * 64 + i) / 300) & 1 ? 32767 : -32768; break;
                    }
                    buf[i] = (int16_t) s;
                }
                dv_block(&S, &P, buf, 64, 1);
                blocks++;
                for (int i = 0; i < 64; i++) if (abs(buf[i]) > maxabs) maxabs = abs(buf[i]);
            }
            if (abs(S.lp) >= 65535 && abs(S.bp) >= 65535) {   /* both pinned: oscillation */
                printf("filter stuck: mode %s drive %d rt %d res %d f %d sig %d\n", NAMES[mode], V[di], rt, V[ri], V[fi], sig);
                fails++;
            }
            (void) maxabs;
        }
    }
    printf("%ld blocks, %d failures\n", blocks, fails);

    /* per-mode figures: -6 dB 220 Hz sine, drive 0 / 50 / 100 %, no filter, full wet, 0 dB level */
    for (int mode = 0; mode < DV_NMODES; mode++) {
        printf("%-8s", NAMES[mode]);
        for (int d = 0; d <= 2; d++) {
            DvParams P;
            DvState S = { 0 };
            double ph = 0, ss = 0, sum = 0;
            int cnt = 0;
            dv_decode(&P, d * 16383, mode * 32768 / DV_NMODES + 100, 0, 32767, 0, 0, 26214);
            for (int b = 0; b < 700; b++) {
                for (int i = 0; i < 64; i++) { buf[i] = (int16_t) (16384 * sin(ph)); ph += 2 * M_PI * 220 / 44100; }
                dv_block(&S, &P, buf, 64, 1);
                if (b >= 200) for (int i = 0; i < 64; i++) { ss += (double) buf[i] * buf[i]; sum += buf[i]; cnt++; }
            }
            printf("  d%3d%%: rms %5.1f dBFS dc %6.3f", d * 50, 10 * log10(ss / cnt / (32768.0 * 32768)), sum / cnt / 32768);
        }
        printf("\n");
    }

    FILE *f = fopen("curves.csv", "w");
    for (int i = -256; i <= 256; i++) {
        int32_t x = i * 128;
        fprintf(f, "%d", x);
        for (int mode = 0; mode < DV_NMODES; mode++)
            for (int d = 0; d <= 2; d++) {
                DvParams P;
                dv_decode(&P, d * 16383, mode * 32768 / DV_NMODES + 100, 0, 32767, 0, 0, 26214);
                fprintf(f, ",%d", mode == M_RECT ? dv_rect(x, P.amt_q15) : dv_shape(mode, (x * P.gain_q8) >> 8));
            }
        fprintf(f, "\n");
    }
    fclose(f);
    return fails != 0;
}
