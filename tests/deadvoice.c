/* Dead-voice check: fuzz (random Q-Link turns and notes), then reset every param to its default and play
 * each voice alone -- a voice left with NaN/garbage state stays silent.
 * Based on the parameter/MIDI fuzzer: random Q-Link turns (4 random params per block, sometimes every param) while
 * notes come and go, like tools/bench.c's sweep but deterministic and long. Link with the wrapper,
 * adapter and engine objects (see tests/run_fuzz.sh) and run under ASan/UBSan. */
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

static float run(AEffect *a, int blocks) {
    float L[128], R[128], *o[2] = {L, R}; double s = 0;
    for (int k = 0; k < blocks; k++) { a->pr(a, 0, o, 128); for (int i = 0; i < 128; i++) s += L[i] * L[i]; }
    return sqrt(s / (blocks * 128.0));
}
int main(int argc, char **argv) {
    long blocks = argc > 1 ? atol(argv[1]) : 20000;
    rng = argc > 2 ? (uint32_t)atol(argv[2]) : 1;
    AEffect *a = VSTPluginMain(host);
    float L[128], R[128], *o[2] = {L, R};
    int held[8] = {0};
    for (long k = 0; k < blocks; k++) {
        int slot = (int)(frand() * 8);
        if (frand() < 0.3f) {
            if (held[slot]) { note(a, 0, held[slot], 0); held[slot] = 0; }
            else { held[slot] = 24 + (int)(frand() * 84); note(a, 1, held[slot], 1 + (int)(frand() * 126)); }
        }
        for (int j = 0; j < 4; j++) {
            int i = (int)(frand() * a->npar) % a->npar;
            if (PARAMS[i].step_target >= 0) continue;
            float v = frand(); if (frand() < 0.1f) v = frand() < 0.5f ? 0.0f : 1.0f;
            a->setP(a, i, v);
        }
        a->pr(a, 0, o, 128);
    }
    for (int s = 0; s < 8; s++) if (held[s]) note(a, 0, held[s], 0);
    for (int i = 0; i < NPARAMS; i++) if (PARAMS[i].step_target < 0 && !PARAMS[i].momentary) a->setP(a, i, PARAMS[i].def);
    run(a, 800);
    const int notes[4] = {60, 62, 64, 65};
    int dead = 0;
    printf("seed %u:", rng);
    for (int keep = 0; keep < 4; keep++) {
        for (int i = 0; i < 4; i++) { note(a, 1, notes[i], 100); run(a, 3); }
        for (int i = 0; i < 4; i++) if (i != keep) note(a, 0, notes[i], 0);
        run(a, 400);
        float r = run(a, 40);
        printf(" v%d %.4f", keep, r);
        if (r < 1e-3f) dead++;
        note(a, 0, notes[keep], 0); run(a, 400);
    }
    printf("  %s\n", dead ? "DEAD VOICE" : "ok");
    return 0;
}
