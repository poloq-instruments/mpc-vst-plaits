/* Voice allocation, mono note memory, legato, TRIG as a gate and release tails. Exits 1 on any failure. */
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
#define SR 44100
#define BLK 128
static float buf[SR * 4];
static int fails;
static void midi(AEffect *a, int s, int d1, int d2) {
    ME m = {1, sizeof(ME), 0, 0, 0, 0, {(unsigned char)s, (unsigned char)d1, (unsigned char)d2, 0}, {0}};
    EV ev = {1, 0, {&m, 0}};
    a->d(a, 25, 0, 0, &ev, 0);
}
static void on(AEffect *a, int n) { midi(a, 0x90, n, 100); }
static void off(AEffect *a, int n) { midi(a, 0x80, n, 0); }
/* Renders ms of audio; returns how many samples went into buf. */
static int run(AEffect *a, int ms) {
    float L[BLK], R[BLK], *o[2] = {L, R};
    int n = (int)((long)ms * SR / 1000 / BLK), w = 0;
    for (int k = 0; k < n; k++) {
        a->pr(a, 0, o, BLK);
        if (w + BLK <= (int)(sizeof buf / sizeof buf[0])) { memcpy(buf + w, L, sizeof L); w += BLK; }
    }
    return w;
}
static double rms(int n) { double s = 0; for (int i = 0; i < n; i++) s += buf[i] * buf[i]; return sqrt(s / (n ? n : 1)); }
static double tone(int n, int note) {   /* Goertzel magnitude at a MIDI note's fundamental */
    double w = 2 * M_PI * 440.0 * pow(2.0, (note - 69) / 12.0) / SR, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) { double s = buf[i] + c * s1 - s2; s2 = s1; s1 = s; }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}
static void check(const char *what, int ok, double x, double y) {
    printf("%s  %-58s (%.5f vs %.5f)\n", ok ? "PASS" : "FAIL", what, x, y);
    if (!ok) fails++;
}
static int idx(const char *k) { for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, k)) return i; return -1; }
static void set_opt(AEffect *a, const char *key, const char *name) {
    int i = idx(key);
    for (int o = 0; o < PARAMS[i].nopts; o++)
        if (!strcmp(PARAMS[i].opts[o], name)) { a->setP(a, i, (float)o / (PARAMS[i].nopts - 1)); return; }
    printf("no option %s for %s\n", name, key); fails++;
}
static void set(AEffect *a, const char *key, float norm) { a->setP(a, idx(key), norm); }
static void silence(AEffect *a) { midi(a, 0xB0, 123, 0); run(a, 3000); }

int main(void) {
    AEffect *a = VSTPluginMain(host);
    int n;

    /* Held keys survive fast playing on top: released voices are stolen first. */
    set_opt(a, "model", "Virtual Analog"); set_opt(a, "voice_mode", "Poly"); set(a, "lpg_decay", 0.9f);
    on(a, 60); on(a, 64); on(a, 67); run(a, 200);
    n = run(a, 300);
    double chord[3] = {tone(n, 60), tone(n, 64), tone(n, 67)};
    int stac[4] = {72, 74, 76, 77};
    for (int i = 0; i < 4; i++) { on(a, stac[i]); run(a, 50); off(a, stac[i]); run(a, 50); }
    n = run(a, 300);
    check("held C survives 4 staccato notes (poly 4)", tone(n, 60) > 0.5 * chord[0], tone(n, 60), chord[0]);
    check("held E survives", tone(n, 64) > 0.5 * chord[1], tone(n, 64), chord[1]);
    check("held G survives", tone(n, 67) > 0.5 * chord[2], tone(n, 67), chord[2]);
    off(a, 60); off(a, 64); off(a, 67); silence(a);
    set(a, "lpg_decay", 0.35f);

    /* Mono: releasing the sounding key goes back to the one still held. */
    set_opt(a, "voice_mode", "Mono");
    on(a, 57); run(a, 200); on(a, 61); run(a, 200); off(a, 61); run(a, 200);
    n = run(a, 200);
    check("mono: back to held A after releasing B", tone(n, 57) > 4 * tone(n, 61), tone(n, 57), tone(n, 61));
    off(a, 57); silence(a);

    /* Legato: an overlapping key doesn't re-excite (Modal only sounds when struck); mono does. */
    set_opt(a, "model", "Modal Resonator"); set(a, "morph", 0.4f);
    double r[2];
    for (int legato = 0; legato < 2; legato++) {
        set_opt(a, "voice_mode", legato ? "Legato" : "Mono");
        on(a, 57); run(a, 1500); on(a, 61);
        n = run(a, 200); r[legato] = rms(n);
        off(a, 61); off(a, 57); silence(a);
    }
    check("legato doesn't restrike, mono does", r[0] > 3 * r[1], r[0], r[1]);

    /* A held voice restruck by the same key gets a new TRIG edge (the gate drops for a block). */
    set_opt(a, "voice_mode", "Poly");
    on(a, 57); run(a, 1500);
    n = run(a, 200); double before = rms(n);
    on(a, 57);
    n = run(a, 200);
    check("same key restrikes a held voice", rms(n) > 3 * before, rms(n), before);
    off(a, 57); silence(a);

    /* Release tails run to silence: a long Modal decay was cut ~2.7 s after note-off (at DECAY 0.35). */
    set(a, "morph", 1.0f);
    on(a, 57); run(a, 50); off(a, 57); run(a, 3500);
    n = run(a, 200);
    check("long Modal tail still sounding 3.5 s after release", rms(n) > 1e-4, rms(n), 1e-4);
    silence(a);

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
