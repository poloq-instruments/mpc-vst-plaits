/* A released key must go quiet on every model in GATE mode (the default). Holds note 60 for 0.5 s, releases it, and
 * measures the level 1.0 to 1.2 s after the release, with DECAY at its minimum so that models that ring out by design
 * have done so. Before the gate fix in plaits_move_engine.cpp (src/VENDORED.md) Chiptune stayed at full level.
 * Reports every model; fails on Chiptune only (the others' tails are the module's own behaviour).
 * TEST=tests/chiprelease.c tests/run_c.sh */
#include <stdio.h>
#include <stdint.h>
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
static double run(AEffect *a, int blocks) {
    float L[128], R[128], *o[2] = {L, R}; double s = 0;
    for (int k = 0; k < blocks; k++) { a->pr(a, 0, o, 128); for (int i = 0; i < 128; i++) s += (double)L[i] * L[i]; }
    return sqrt(s / (blocks * 128.0));
}
static int idx(const char *k) { for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, k)) return i; return -1; }
int main(void) {
    AEffect *a = VSTPluginMain(host);
    int im = idx("model"), nm = PARAMS[im].nopts, fails = 0;
    a->setP(a, idx("lpg_decay"), 0.0f);
    for (int m = 0; m < nm; m++) {
        a->setP(a, im, (float)m / (nm - 1));
        char name[64] = ""; a->d(a, 7, im, 0, name, 0);
        run(a, 40);
        note(a, 1, 60);
        double held = run(a, 172);
        note(a, 0, 60);
        run(a, 345);
        double tail = run(a, 69);
        int bad = m == 7 && !(held > 1e-3 && tail < 1e-4);
        if (bad) fails++;
        printf("%s %-18s held %7.1f dBFS, 1 s after release %7.1f dBFS\n", bad ? "FAIL" : (tail > 1e-3 ? "note" : "ok  "), name,
               held > 0 ? 20 * log10(held) : -200.0, tail > 1e-10 ? 20 * log10(tail) : -200.0);
        run(a, 3000);
    }
    printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails != 0;
}
