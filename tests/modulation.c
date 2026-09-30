/* v1.1 features: all 24 models, pitch bend, AMP modes, TRIG RATE at the host tempo, MOD / ASSIGN routing, mod wheel,
 * transport restart, the NOTES display. Exits 1 on any failure. */
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
typedef struct {
    double samplePos, sampleRate, nanoSeconds, ppqPos, tempo, barStartPos, cycleStartPos, cycleEndPos;
    int32_t timeSigNumerator, timeSigDenominator, smpteOffset, smpteFrameRate, samplesToNextClock, flags;
} VstTimeInfo;
extern AEffect* VSTPluginMain(cb);

static double host_tempo = 120.0;
static int host_playing = 0;
static intptr_t host(AEffect*e,int32_t op,int32_t i,intptr_t v,void*p,float o){
    static VstTimeInfo ti;
    if (op == 7) {
        ti.tempo = host_tempo;
        ti.flags = (1 << 10) | (host_playing ? (1 << 1) : 0);
        return (intptr_t)&ti;
    }
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
static int run(AEffect *a, int ms) {
    float L[BLK], R[BLK], *o[2] = {L, R};
    int n = (int)((long)ms * SR / 1000 / BLK), w = 0;
    for (int k = 0; k < n; k++) {
        a->pr(a, 0, o, BLK);
        if (w + BLK <= (int)(sizeof buf / sizeof buf[0])) { memcpy(buf + w, L, sizeof L); w += BLK; }
    }
    return w;
}
static double rms_at(int from, int n) { double s = 0; for (int i = from; i < from + n; i++) s += buf[i] * buf[i]; return sqrt(s / (n ? n : 1)); }
static double tone(int n, double hz) {
    double w = 2 * M_PI * hz / SR, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) { double s = buf[i] + c * s1 - s2; s2 = s1; s1 = s; }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}
static double hz(int note) { return 440.0 * pow(2.0, (note - 69) / 12.0); }
/* Strikes in the buffer: 5 ms windows whose level jumps to 3x the one before. */
static int strikes(int n) {
    int win = SR / 200, count = 0; double prev = 1e-9;
    for (int i = 0; i + win <= n; i += win) {
        double r = rms_at(i, win);
        if (r > 3 * prev && r > 1e-3) count++;
        prev = r;
    }
    return count;
}
static void check(const char *what, int ok, double x, double y) {
    printf("%s  %-60s (%.5f vs %.5f)\n", ok ? "PASS" : "FAIL", what, x, y);
    if (!ok) fails++;
}
static int idx(const char *k) { for (int i = 0; i < NPARAMS; i++) if (!strcmp(PARAMS[i].key, k)) return i; printf("no param %s\n", k); fails++; return 0; }
static void set_opt(AEffect *a, const char *key, const char *name) {
    int i = idx(key);
    for (int o = 0; o < PARAMS[i].nopts; o++)
        if (!strcmp(PARAMS[i].opts[o], name)) { a->setP(a, i, (float)o / (PARAMS[i].nopts - 1)); return; }
    printf("no option %s for %s\n", name, key); fails++;
}
static void set(AEffect *a, const char *key, float value) {   /* in the param's own units */
    int i = idx(key);
    a->setP(a, i, (value - PARAMS[i].min) / (PARAMS[i].max - PARAMS[i].min));
}
static void reset(AEffect *a) {
    midi(a, 0xB0, 123, 0); midi(a, 0xB0, 121, 0); run(a, 3000);
    for (int i = 0; i < NPARAMS; i++)   /* params.h defaults are normalized */
        if (PARAMS[i].popup_of < 0 && PARAMS[i].step_target < 0) a->setP(a, i, PARAMS[i].def);
}

int main(void) {
    AEffect *a = VSTPluginMain(host);
    int n;

    /* Every model sounds, 6-op FM, drums and Chiptune included. */
    int im = idx("model"), silent = 0;
    for (int m = 0; m < PARAMS[im].nopts; m++) {
        a->setP(a, im, (float)m / (PARAMS[im].nopts - 1));
        on(a, 57); n = run(a, 300); off(a, 57);
        double r = rms_at(0, n);
        if (r < 1e-3) { printf("      silent model: %s (%.6f)\n", PARAMS[im].opts[m], r); silent++; }
        run(a, 1500);
    }
    check("all 24 models make sound", silent == 0, silent, 0);
    reset(a);

    /* Pitch bend: full up with BEND 2 = two semitones. */
    set_opt(a, "model", "Virtual Analog");
    on(a, 57); run(a, 200); midi(a, 0xE0, 0x7F, 0x7F); run(a, 100);
    n = run(a, 300);
    check("bend up 2 st: A3 -> B3", tone(n, hz(59)) > 5 * tone(n, hz(57)), tone(n, hz(59)), tone(n, hz(57)));
    midi(a, 0xE0, 0x00, 0x40); off(a, 57); reset(a);

    /* AMP: GATE holds while the key is down, PING rings out on its own. */
    double late[2];
    for (int ping = 0; ping < 2; ping++) {
        set_opt(a, "model", "Virtual Analog"); set_opt(a, "amp_mode", ping ? "Ping" : "Gate");
        on(a, 57); n = run(a, 1500);
        late[ping] = rms_at(n - SR / 5, SR / 5) / rms_at(SR / 20, SR / 5);
        off(a, 57); reset(a);
    }
    check("GATE sustains while held", late[0] > 0.5, late[0], 0.5);
    check("PING decays while held", late[1] < 0.1, late[1], 0.1);

    /* DRONE: Modal with no trigger is excited by dust and keeps sounding. */
    set_opt(a, "model", "Modal Resonator"); set_opt(a, "amp_mode", "Drone");
    on(a, 57); n = run(a, 2000);
    check("DRONE: Modal keeps sounding with no trigger", rms_at(n - SR / 2, SR / 2) > 2e-4, rms_at(n - SR / 2, SR / 2), 2e-4);
    off(a, 57); reset(a);

    /* TRIG RATE 1/16 follows the host tempo: 8 strikes a second at 120 BPM, 4 at 60. */
    int st[2];
    for (int k = 0; k < 2; k++) {
        host_tempo = k ? 60.0 : 120.0;
        set_opt(a, "model", "Modal Resonator"); set(a, "morph", 0.2f); set_opt(a, "trig_rate", "1/16");
        on(a, 57); run(a, 20); n = run(a, 2000);
        st[k] = strikes(n);
        off(a, 57); reset(a);
    }
    host_tempo = 120.0;
    check("TRIG RATE 1/16 at 120 BPM: ~16 strikes in 2 s", abs(st[0] - 16) <= 1, st[0], 16);
    check("TRIG RATE 1/16 at 60 BPM: ~8 strikes in 2 s", abs(st[1] - 8) <= 1, st[1], 8);

    /* MOD page: LFO 1 -> PITCH wobbles the note off its centre. */
    double centre[2];
    for (int k = 0; k < 2; k++) {
        set_opt(a, "model", "Virtual Analog"); set(a, "lfo1_rate", 1.0f); set(a, "mod_lfo1_pitch", k ? 0.3f : 0.0f);
        on(a, 57); run(a, 100); n = run(a, 2000);
        centre[k] = tone(n, hz(57));
        off(a, 57); reset(a);
    }
    check("MOD LFO1 > PITCH moves the pitch", centre[1] < 0.5 * centre[0], centre[1], centre[0]);

    /* Its on/off switch mutes the path with the knob still turned up; the ASSIGN grid's switches the same. */
    double muted[2];
    for (int k = 0; k < 2; k++) {
        set_opt(a, "model", "Virtual Analog"); set(a, "lfo1_rate", 1.0f); set(a, "lfo2_rate", 1.0f);
        if (k) { set_opt(a, "asg_dst1", "Pitch"); set(a, "asg_1_1", 0.3f); set_opt(a, "asg_1_1_on", "Off"); }
        else { set(a, "mod_lfo1_pitch", 0.3f); set_opt(a, "mod_lfo1_pitch_on", "Off"); }
        on(a, 57); run(a, 100); n = run(a, 2000);
        muted[k] = tone(n, hz(57));
        off(a, 57); reset(a);
    }
    check("MOD switch off: LFO1 > PITCH muted", muted[0] > 0.8 * centre[0], muted[0], centre[0]);
    check("ASSIGN switch off: row 1 > Pitch muted", muted[1] > 0.8 * centre[0], muted[1], centre[0]);

    /* ASSIGN page: row 1 (LFO 2 by default) re-routed to Pitch. */
    for (int k = 0; k < 2; k++) {
        set_opt(a, "model", "Virtual Analog"); set(a, "lfo2_rate", 1.0f);
        set_opt(a, "asg_dst1", "Pitch"); set(a, "asg_1_1", k ? 0.3f : 0.0f);
        on(a, 57); run(a, 100); n = run(a, 2000);
        centre[k] = tone(n, hz(57));
        off(a, 57); reset(a);
    }
    check("ASSIGN LFO 2 > Pitch moves the pitch", centre[1] < 0.5 * centre[0], centre[1], centre[0]);

    /* Mod wheel (ASSIGN row 4) -> Volume (column 4) at -1: wheel up silences. */
    set_opt(a, "model", "Virtual Analog"); set(a, "asg_4_4", -1.0f);
    on(a, 57); run(a, 100); n = run(a, 300); double wheel0 = rms_at(0, n);
    midi(a, 0xB0, 1, 127); run(a, 100); n = run(a, 300); double wheel1 = rms_at(0, n);
    check("mod wheel -> volume via ASSIGN", wheel1 < 0.1 * wheel0, wheel1, wheel0);
    off(a, 57); reset(a);

    /* Transport: playing restarts the shared LFOs. LFO 1 as a 0.1 Hz saw on pitch sits at its centre after 5 s;
     * pressing play jumps it back to the bottom of the ramp, well below the note. */
    set_opt(a, "model", "Virtual Analog"); set_opt(a, "lfo1_shape", "Saw"); set(a, "lfo1_rate", 0.1f);
    set_opt(a, "lfo1_retrig", "On");   /* the note starts the ramp, so 5 s later it is at its centre */
    set(a, "mod_lfo1_pitch", 0.5f);
    on(a, 57); run(a, 4900); n = run(a, 200); double before = tone(n, hz(57));
    host_playing = 1; run(a, 20); n = run(a, 200); double after = tone(n, hz(57));
    check("transport play restarts LFO 1", after < 0.2 * before, after, before);
    host_playing = 0; off(a, 57); reset(a);

    /* NOTES shows the real note count when UNISON caps it. */
    char d[64] = "";
    set(a, "polyphony", 4); set(a, "unison", 3);
    a->d(a, 7, idx("polyphony"), 0, d, 0);
    printf("%s  %-60s (\"%s\")\n", !strcmp(d, "4 (2)") ? "PASS" : "FAIL", "NOTES 4 with UNISON 3 shows \"4 (2)\"", d);
    if (strcmp(d, "4 (2)")) fails++;

    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
