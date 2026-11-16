/* Holds a note on every model and looks for a waveform break tied to the host's block: the energy of the output's
 * second difference, by position inside the host block, must be about the same at every position. Before the
 * fixed-size chunking in freak_plugin.cpp's render_block (src/VENDORED.md) the three 6-op FM models broke at the
 * start of each 128-frame block (ratio 90 to 220000). Host block sizes 128, 64 and 37 frames.
 * TEST=tests/blockedge.c tests/run_c.sh */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "params.h"
typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
struct AEffect { int32_t magic; intptr_t (*d)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
 void*p; void (*setP)(AEffect*,int32_t,float); float (*getP)(AEffect*,int32_t);
 int32_t np,npar,ni,no,flags; intptr_t r1,r2; int32_t a,b,c; float io; void*obj,*user; int32_t uid,ver;
 void (*pr)(AEffect*,float**,float**,int32_t); void*pdr; char f[56]; };
typedef struct { int32_t type,byteSize,deltaFrames,flags,noteLength,noteOffset; unsigned char m[4]; char x[4]; } ME;
typedef struct { int32_t n; intptr_t r; void* ev[2]; } EV;
extern AEffect* VSTPluginMain(cb);
static intptr_t host(AEffect*e,int32_t op,int32_t i,intptr_t v,void*p,float o){
    static double ti[16];
    if (op == 7) { ti[4] = 120.0; ((int32_t*)&ti[8])[5] = 1 << 10; return (intptr_t)ti; }
    return 0;
}
static void note(AEffect *a, int on, int n) {
    ME m = {1, sizeof(ME), 0, 0, 0, 0, {(unsigned char)(on ? 0x90 : 0x80), (unsigned char)n, on ? 100 : 0, 0}, {0}};
    EV ev = {1, 0, {&m, 0}};
    a->d(a, 25, 0, 0, &ev, 0);
}
static int idx(const char *k) { for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, k)) return i; return -1; }
static int cmpd(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); }
int main(void) {
    static const int sizes[] = {128, 64, 37};
    int fails = 0;
    for (int s = 0; s < 3; s++) {
        int bs = sizes[s];
        AEffect *a = VSTPluginMain(host);
        a->d(a, 0, 0, 0, 0, 0); a->d(a, 10, 0, 0, 0, 44100.0f); a->d(a, 11, 0, bs, 0, 0); a->d(a, 12, 0, 1, 0, 0);
        int im = idx("model"), nm = PARAMS[im].nopts;
        for (int m = 0; m < nm; m++) {
            a->setP(a, im, (float)m / (nm - 1));
            char name[64] = ""; a->d(a, 7, im, 0, name, 0);
            float L[128], R[128], *o[2] = {L, R};
            double e[128] = {0}, sorted[128], p1 = 0, p2 = 0;
            for (int b = 0; b < 30; b++) a->pr(a, 0, o, bs);
            note(a, 1, 60);
            int blocks = 2 * 44100 / bs, counted = 0;
            for (int b = 0; b < blocks; b++) {
                a->pr(a, 0, o, bs);
                for (int i = 0; i < bs; i++) {
                    double d2 = L[i] - 2 * p1 + p2;
                    if (b >= 40) e[i] += d2 * d2;
                    p2 = p1; p1 = L[i];
                }
                if (b >= 40) counted++;
            }
            note(a, 0, 60);
            for (int b = 0; b < 20000 / bs; b++) a->pr(a, 0, o, bs);
            memcpy(sorted, e, sizeof(double) * bs);
            qsort(sorted, bs, sizeof(double), cmpd);
            double med = sorted[bs / 2], mx = sorted[bs - 1];
            int worst = 0; for (int i = 0; i < bs; i++) if (e[i] == mx) worst = i;
            if (med / counted < 1e-16) continue;   /* silent by then (struck and decayed): nothing to measure */
            double ratio = mx / med;
            int bad = ratio > 20.0;
            if (bad) fails++;
            if (bad || bs == 128) printf("%s block %3d %-18s worst position %3d, %8.1f x the median\n", bad ? "FAIL" : "ok  ", bs, name, worst, ratio);
        }
        a->d(a, 1, 0, 0, 0, 0);
    }
    printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails != 0;
}
