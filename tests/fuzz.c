/* Parameter/MIDI fuzzer: random Q-Link turns (4 random params per block, sometimes every param) while
 * notes come and go, like tools/bench.c's sweep but deterministic and long. Link with the wrapper,
 * adapter and engine objects (see tests/run_fuzz.sh) and run under ASan/UBSan. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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
static uint32_t rng = 1;
static float frand(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }
static intptr_t host(AEffect*e,int32_t op,int32_t i,intptr_t v,void*p,float o){
    static double ti[16];
    if (op == 7) { ti[4] = 120.0; ((int32_t*)&ti[8])[5] = 1 << 10; return (intptr_t)ti; }
    return 0;
}
static void note(AEffect *a, int on, int n, int vel) {
    ME m = {1, sizeof(ME), 0, 0, 0, 0, {(unsigned char)(on ? 0x90 : 0x80), (unsigned char)n, (unsigned char)vel, 0}, {0}};
    EV ev = {1, 0, {&m, 0}};
    a->d(a, 25, 0, 0, &ev, 0);
}
int main(int argc, char **argv) {
    long blocks = argc > 1 ? atol(argv[1]) : 200000;
    rng = argc > 2 ? (uint32_t)atol(argv[2]) : 1;
    long trace_from = argc > 3 ? atol(argv[3]) : blocks;
    AEffect *a = VSTPluginMain(host);
    float L[128], R[128], *o[2] = {L, R};
    int held[8] = {0};
    for (long k = 0; k < blocks; k++) {
        int slot = (int)(frand() * 8);
        if (frand() < 0.3f) {
            if (held[slot]) { note(a, 0, held[slot], 0); held[slot] = 0; }
            else { held[slot] = 24 + (int)(frand() * 84); note(a, 1, held[slot], 1 + (int)(frand() * 126)); }
        }
        int all = frand() < 0.01f;
        int n = all ? a->npar : 4;
        for (int j = 0; j < n; j++) {
            int i = all ? j : (int)(frand() * a->npar) % a->npar;
            float v = frand();
            if (frand() < 0.1f) v = frand() < 0.5f ? 0.0f : 1.0f;
            if (k % 50000 == 0 && j == 0) { fprintf(stderr, "block %ld\n", k); }
            if (k >= trace_from) fprintf(stderr, "k=%ld set %d %s = %.4f\n", k, i, PARAMS[i].key, v);
            a->setP(a, i, v);
        }
        a->pr(a, 0, o, 128);
        if (k % 1000 == 0) fprintf(stderr, "at %ld\n", k);
        for (int q = 0; q < 128; q++) if (L[q] != L[q]) { fprintf(stderr, "NaN out at block %ld\n", k); return 2; }
    }
    fprintf(stderr, "done %ld blocks, no crash\n", blocks);
    return 0;
}
