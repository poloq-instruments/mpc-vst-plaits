/* Prints every parameter's value in a fresh instance (key, normalized value): the engine's real defaults. */
#include <stdio.h>
#include <stdint.h>
#include "params.h"
typedef struct AEffect AEffect;
typedef intptr_t (*cb)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
struct AEffect { int32_t magic; intptr_t (*d)(AEffect*,int32_t,int32_t,intptr_t,void*,float);
 void*p; void (*setP)(AEffect*,int32_t,float); float (*getP)(AEffect*,int32_t);
 int32_t np,npar,ni,no,flags; intptr_t r1,r2; int32_t a,b,c; float io; void*obj,*user; int32_t uid,ver;
 void (*pr)(AEffect*,float**,float**,int32_t); void*pdr; char f[56]; };
extern AEffect* VSTPluginMain(cb);
static intptr_t host(AEffect*e,int32_t op,int32_t i,intptr_t v,void*p,float o){ return 0; }
int main(void) {
    AEffect *a = VSTPluginMain(host);
    for (int i = 0; i < NPARAMS; i++) { char d[64] = ""; a->d(a, 7, i, 0, d, 0); printf("%s\t%.6f\t%s\n", PARAMS[i].key, a->getP(a, i), d); }
    return 0;
}
