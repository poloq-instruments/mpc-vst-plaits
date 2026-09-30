/* Sustain pedal: a key released with CC 64 down keeps sounding until the pedal comes up. */
#include <stdio.h>
#include <stdint.h>
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
int main(void) {
    int fails = 0;
    for (int pedal = 0; pedal < 2; pedal++) {
        AEffect *a = VSTPluginMain(host);
        midi(a, 0x90, 60, 100); run(a, 20);
        if (pedal) midi(a, 0xB0, 64, 127);
        midi(a, 0x80, 60, 0); run(a, 400);
        float after = run(a, 50);
        midi(a, 0xB0, 64, 0); run(a, 400);
        float lifted = run(a, 50);
        printf("pedal %s: 1.3 s after key up rms %.5f, after pedal up rms %.5f\n", pedal ? "down" : "up  ", after, lifted);
        if (pedal && !(after > 1e-3f && lifted < after * 0.05f)) fails++;
        if (!pedal && after > 1e-4f) fails++;
    }
    printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails != 0;
}
