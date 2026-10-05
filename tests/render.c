/* Renders a fixed phrase on every model to raw float32 stereo, for comparing builds: render <plugin.so> <out.raw>
 * Loads the built plugin with dlopen, so the DSP runs with the exact production compiler flags. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include "params.h"
typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
struct AEffect { int32_t magic; intptr_t (*d)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
 void*p; void (*setP)(AEffect*,int32_t,float); float (*getP)(AEffect*,int32_t);
 int32_t np,npar,ni,no,flags; intptr_t r1,r2; int32_t a,b,c; float io; void*obj,*user; int32_t uid,ver;
 void (*pr)(AEffect*,float**,float**,int32_t); void*pdr; char f[56]; };
typedef struct { int32_t type,byteSize,deltaFrames,flags,noteLength,noteOffset; unsigned char m[4]; char x[4]; } ME;
typedef struct { int32_t n; intptr_t r; void* ev[2]; } EV;
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
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: render <plugin.so> <out.raw>\n"); return 2; }
    void *h = dlopen(argv[1], RTLD_NOW); if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    AEffect *(*entry)(cb) = (AEffect *(*)(cb))dlsym(h, "VSTPluginMain");
    AEffect *a = entry(host);
    a->d(a, 0, 0, 0, 0, 0); a->d(a, 10, 0, 0, 0, 44100.0f); a->d(a, 11, 0, 128, 0, 0); a->d(a, 12, 0, 1, 0, 0);
    for (int i = 0; i < NPARAMS; i++) a->setP(a, i, PARAMS[i].def);
    FILE *out = fopen(argv[2], "wb"); if (!out) return 1;
    int im = idx("model"), iv = idx("voice_mode"), nm = PARAMS[im].nopts;
    static const int chord[] = {48, 55, 60, 64, 67, 72};
    float L[128], R[128], *o[2] = {L, R};
    a->setP(a, iv, 0.5f);
    for (int m = 0; m < nm; m++) {
        a->setP(a, im, (float)m / (nm - 1));
        for (int step = 0; step < 6; step++) {
            for (int k = 0; k <= step; k++) note(a, 1, chord[k]);
            for (int b = 0; b < 40; b++) {
                /* slow deterministic sweep over the non-structural params */
                for (int p = 0; p < NPARAMS; p++) if (p != im && p != iv && PARAMS[p].nopts == 0)
                    a->setP(a, p, 0.5f + 0.45f * (float)((((m * 7 + step * 3 + b + p * 5) % 64) - 32) / 32.0));
                a->pr(a, 0, o, 128);
                for (int i = 0; i < 128; i++) { float s[2] = {L[i], R[i]}; fwrite(s, sizeof s, 1, out); }
            }
            for (int k = 0; k <= step; k++) note(a, 0, chord[k]);
            for (int b = 0; b < 20; b++) {
                a->pr(a, 0, o, 128);
                for (int i = 0; i < 128; i++) { float s[2] = {L[i], R[i]}; fwrite(s, sizeof s, 1, out); }
            }
        }
    }
    fclose(out);
    printf("%d models rendered\n", nm);
    return 0;
}
