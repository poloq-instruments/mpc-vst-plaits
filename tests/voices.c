/* Holds four notes (one per voice), releases all but one, and measures that voice alone, per model. */
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
static void midi(AEffect *a, int s, int d1, int d2) {
    ME m = {1, sizeof(ME), 0, 0, 0, 0, {(unsigned char)s, (unsigned char)d1, (unsigned char)d2, 0}, {0}};
    EV ev = {1, 0, {&m, 0}};
    a->d(a, 25, 0, 0, &ev, 0);
}
static float run(AEffect *a, int blocks) {
    float L[128], R[128], *o[2] = {L, R}; double s = 0;
    for (int k = 0; k < blocks; k++) { a->pr(a, 0, o, 128); for (int i = 0; i < 128; i++) s += L[i] * L[i]; }
    return sqrt(s / (blocks * 128.0));
}
static int idx(const char *k) { for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, k)) return i; return -1; }
int main(int argc, char **argv) {
#if defined(__arm__)
    if (argc > 1) {   /* flush-to-zero (+ default NaN when argv[1] is 2), as a host's audio thread may set */
        unsigned int f; __asm__ volatile("vmrs %0, fpscr" : "=r"(f));
        f |= 1u << 24; if (argv[1][0] == '2') f |= 1u << 25;
        __asm__ volatile("vmsr fpscr, %0" : : "r"(f));
    }
#endif
    int pressure = 0;   /* MPC pads stream poly aftertouch */
    AEffect *a = VSTPluginMain(host);
    int im = idx("model"), nm = PARAMS[im].nopts;
    const int notes[4] = {60, 62, 64, 65};
    for (int m = 0; m < nm; m++) {
        a->setP(a, im, (float)m / (nm - 1));
        char d[64] = ""; a->d(a, 7, im, 0, d, 0);
        printf("%-18s", d);
        for (int keep = 0; keep < 4; keep++) {
            for (int i = 0; i < 4; i++) { midi(a, 0x90, notes[i], 100); run(a, 3); }
            for (int i = 0; i < 4; i++) if (i != keep) midi(a, 0x80, notes[i], 0);
            for (int k = 0; k < 400; k++) { if (pressure) midi(a, 0xA0, notes[keep], (k * 7) % 128); run(a, 1); }
            printf("  v%d %.4f", keep, run(a, 40));
            midi(a, 0x80, notes[keep], 0); run(a, 400);
        }
        printf("\n");
    }
    return 0;
}
