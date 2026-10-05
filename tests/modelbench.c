/* Per-model CPU cost on the device: modelbench <plugin.so> [voices] [seconds]
 * For each model, holds a chord of <voices> notes (re-struck every half second, Q-Link-style parameter drift)
 * and times every 128-frame block with this thread's CPU clock. Prints mean and p99 as % of the 2902 us budget.
 * Run pinned at normal priority next to MPC (taskset 2), like tools/bench.sh in mpc-vst-plugins. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
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
#define BUDGET_US (128 * 1e6 / 44100)
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
static double now_us(void) { struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static int cmpd(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); }
static int idx(const char *k) { for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, k)) return i; return -1; }
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: modelbench <plugin.so> [voices] [seconds]\n"); return 2; }
    int voices = argc > 2 ? atoi(argv[2]) : 6;
    double secs = argc > 3 ? atof(argv[3]) : 3;
    void *h = dlopen(argv[1], RTLD_NOW); if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    AEffect *a = ((AEffect *(*)(cb))dlsym(h, "VSTPluginMain"))(host);
    a->d(a, 0, 0, 0, 0, 0); a->d(a, 10, 0, 0, 0, 44100.0f); a->d(a, 11, 0, 128, 0, 0); a->d(a, 12, 0, 1, 0, 0);
    for (int i = 0; i < NPARAMS; i++) a->setP(a, i, PARAMS[i].def);
    int im = idx("model"), iv = idx("voice_mode"), nm = PARAMS[im].nopts;
    int nb = (int)(secs * 44100 / 128);
    double *t = malloc(nb * sizeof *t), all = 0;
    float L[128], R[128], *o[2] = {L, R};
    a->setP(a, iv, voices > 1 ? 0.5f : 0.0f);
    printf("%-18s %7s %7s %7s\n", "model", "mean%", "p99%", "max%");
    for (int m = 0; m < nm; m++) {
        a->setP(a, im, (float)m / (nm - 1));
        char name[64] = ""; a->d(a, 7, im, 0, name, 0);
        for (int b = 0; b < 40; b++) a->pr(a, 0, o, 128);           /* settle after the model switch */
        for (int b = 0; b < nb; b++) {
            if (b % 172 == 0) for (int k = 0; k < voices; k++) { note(a, 0, 48 + k * 4); note(a, 1, 48 + k * 4); }
            for (int p = 0; p < NPARAMS; p++) if (p != im && p != iv && PARAMS[p].nopts == 0 && (b + p) % 8 == 0)
                a->setP(a, p, 0.5f + 0.4f * (float)(((b + p * 13) % 200) - 100) / 100.0f);
            double t0 = now_us(); a->pr(a, 0, o, 128); t[b] = now_us() - t0;
        }
        for (int k = 0; k < voices; k++) note(a, 0, 48 + k * 4);
        double s = 0; for (int b = 0; b < nb; b++) s += t[b];
        qsort(t, nb, sizeof *t, cmpd); all += s / nb;
        printf("%-18s %7.1f %7.1f %7.1f\n", name, s / nb / BUDGET_US * 100, t[(int)(nb * 0.99)] / BUDGET_US * 100,
               t[nb - 1] / BUDGET_US * 100);
    }
    printf("%-18s %7.1f\n", "AVERAGE", all / nm / BUDGET_US * 100);
    return 0;
}
