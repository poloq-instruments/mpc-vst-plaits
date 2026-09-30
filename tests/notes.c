/* Plays every note 12..120 on every model, in poly and mono, and reports the notes that stay silent. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
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
static float run(AEffect *a, int blocks) {
    float L[128], R[128], *o[2] = {L, R}; double s = 0;
    for (int k = 0; k < blocks; k++) { a->pr(a, 0, o, 128); for (int i = 0; i < 128; i++) s += L[i] * L[i] + R[i] * R[i]; }
    return sqrt(s / (blocks * 256.0));
}
int main(int argc, char **argv) {
    AEffect *a = VSTPluginMain(host);
    if (argc > 2 && atoi(argv[2])) for (int i = 0; i < NPARAMS; i++) a->setP(a, i, PARAMS[i].def);   /* host pushes reported defaults */
    int im = idx("model"), iv = idx("voice_mode"), nm = PARAMS[im].nopts;
    const char *modes[] = {"mono", "poly", "legato"};
    int only_mode = argc > 1 ? atoi(argv[1]) : -1;
    for (int mode = 0; mode < 3; mode++) {
        if (only_mode >= 0 && mode != only_mode) continue;
        a->setP(a, iv, mode / 2.0f);
        for (int m = 0; m < nm; m++) {
            a->setP(a, im, (float)m / (nm - 1));
            char silent[512] = ""; int ns = 0; char d[64]; a->d(a, 7, im, 0, d, 0);
            for (int n = 12; n <= 120; n++) {
                note(a, 1, n); float r = run(a, 30); note(a, 0, n); run(a, 80);
                if (r < 1e-4f) { ns++; char t[8]; snprintf(t, 8, "%d ", n); if (strlen(silent) < 480) strcat(silent, t); }
            }
            printf("%-6s %-18s silent %3d/109 %s\n", modes[mode], d, ns, silent);
        }
    }
    return 0;
}
