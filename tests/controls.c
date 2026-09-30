/* Model buttons step and wrap; small nudges on NOTES/UNISON move one step (the data wheel case); a slow drag or
 * Q-Link sweep over a stepped knob moves steadily instead of flickering between two values. */
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
extern AEffect* VSTPluginMain(cb);
static int automated_idx = -1; static float automated_v;
static intptr_t host(AEffect*e,int32_t op,int32_t i,intptr_t v,void*p,float o){
    static double ti[16];
    if (op == 0 && i >= 0 && PARAMS[i].step_target < 0 && PARAMS[i].momentary == 0) { automated_idx = i; automated_v = o; }
    if (op == 7) { ti[4] = 120.0; ((int32_t*)&ti[8])[5] = 1 << 10; return (intptr_t)ti; }
    return 0;
}
static int idx(const char *k) { for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, k)) return i; return -1; }
static int fails;
#define CHECK(c, ...) do { printf("%s ", (c) ? "ok  " : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) fails++; } while (0)
static void block(AEffect *a) { float L[128], R[128], *o[2] = {L, R}; a->pr(a, 0, o, 128); }
static void disp(AEffect *a, int i, char *d) { d[0] = 0; a->d(a, 7, i, 0, d, 0); }
int main(void) {
    AEffect *a = VSTPluginMain(host);
    int im = idx("model"), ip = idx("model_prev"), in = idx("model_next"), ipoly = idx("polyphony"), iuni = idx("unison");
    char d[64], d2[64];
    disp(a, im, d);
    a->setP(a, in, 1.0f); block(a); disp(a, im, d2);
    CHECK(strcmp(d, d2), "next: %s -> %s", d, d2);
    CHECK(automated_idx == im, "host told the model changed (automate %d)", automated_idx);
    a->setP(a, ip, 1.0f); block(a); disp(a, im, d2);
    CHECK(!strcmp(d, d2), "prev: back to %s", d2);
    a->setP(a, im, 0.0f); a->setP(a, ip, 1.0f); block(a); disp(a, im, d2);
    CHECK(!strcmp(d2, PARAMS[im].opts[PARAMS[im].nopts - 1]), "prev from the first wraps to %s", d2);
    for (int k = 0; k < 2; k++) {
        int i = k ? iuni : ipoly;
        disp(a, i, d); float n0 = a->getP(a, i);
        a->setP(a, i, n0 + 0.01f); disp(a, i, d2);
        CHECK(atoi(d2) == atoi(d) + 1, "%s nudge up: %s -> %s", PARAMS[i].key, d, d2);
        n0 = a->getP(a, i); a->setP(a, i, n0 - 0.01f); disp(a, i, d2);
        CHECK(atoi(d2) == atoi(d), "%s nudge down: back to %s", PARAMS[i].key, d2);
    }
    /* A sweep: the host sends positions from where it started (not from the value the plugin settled on), in
     * small increments, 2.5 steps up then back down. The value must never go against the sweep. */
    const char *swept[] = {"polyphony", "unison", "bend_range", "trig_rate"};
    for (int k = 0; k < 4; k++) {
        int i = idx(swept[k]);
        float steps = PARAMS[i].nopts > 1 ? PARAMS[i].nopts - 1 : PARAMS[i].max - PARAMS[i].min;
        float start = 2.0f / steps, pos = start;
        a->setP(a, i, start);
        int flips = 0, prev = -1, first = -1, top = -1, last = -1;
        for (int dir = 1; dir >= -1; dir -= 2) {
            for (int t = 0; t < 250; t++) {
                pos += dir * 0.01f / steps;
                a->setP(a, i, pos);
                int v = (int)lroundf(a->getP(a, i) * steps);
                if (first < 0) first = v;
                if (prev >= 0 && (v - prev) * dir < 0) flips++;
                prev = v;
            }
            if (dir > 0) top = prev;
        }
        last = prev;
        CHECK(flips == 0 && top >= first + 2 && last <= top - 2, "%s sweep: %d -> %d -> %d, %d flips",
              PARAMS[i].key, first, top, last, flips);
    }
    printf("%s\n", fails ? "FAILED" : "PASSED");
    return fails != 0;
}
