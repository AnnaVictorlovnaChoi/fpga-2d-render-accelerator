#ifndef AD_VISIBLE_H
#define AD_VISIBLE_H
#include "renderer.h"
#include "runtime.h"
/* Global scene identity, never per-FB. Elapsed time is intentionally absent:
 * only resolved pose/source and raster destination determine visibility. */
typedef struct {
 const RuntimeGroup *group; const RuntimeFrame *frame;
 int32_t xq,yq,x,y; uint32_t action,dir,scene_version;
} VisibleSignature;
typedef struct {
 /* target/phase belong only to working; submitted identity never rotates them. */
 VisibleSignature staged,completed,submitted; unsigned valid,pending,phase,target,submitted_target;
} VisibleState;
/* Small pre-snapshot probe: individual versions cannot cancel each other. */
typedef struct {
 int32_t xq,yq; uint32_t action,dir;
 uint32_t arrow_version,dragon_version,enemy_version,scene_version;
 uint32_t flow_state,pause_state,countdown;
 const RuntimeFrame *frame;
} VisibleKey;
typedef struct { VisibleKey key; uint32_t physical_ms; unsigned valid; } VisibleGate;
int visible_gate(VisibleGate*,const VisibleKey*,uint32_t,int);
int visible_signature(const RendererSnapshot *s,VisibleSignature *v);
int visible_equal(const VisibleSignature *a,const VisibleSignature *b);
int visible_skip(const VisibleState *v,const VisibleSignature *candidate);
void visible_consume(VisibleState *v);
#endif
