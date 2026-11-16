#ifndef AD_DRAGON_GPU_H
#define AD_DRAGON_GPU_H
#include <stdint.h>
#include "dragon_data.h"
typedef struct { uint16_t *pixels; int stride,x,y,w,h; } DragonSurface;
typedef struct { int x,y,w,h; } DragonClip;
typedef struct { uint32_t commands,pixels,fences,retries; } DragonGPUStats;
enum { DRAGON_PERF_BODYHALO,DRAGON_PERF_BEAM,DRAGON_PERF_MIX,DRAGON_PERF_RESTORE,DRAGON_PERF_PHASES };
typedef struct { uint32_t samples,max_us,commands,pixels; uint64_t total_us; } DragonPhaseStats;
uint64_t dragon_gpu_clock(void);
void dragon_gpu_record(unsigned phase,uint64_t begin,const DragonGPUStats*);
DragonPhaseStats *dragon_gpu_perf(unsigned phase);
int dragon_gpu_fence(void);
int dragon_gpu_draw(DragonSurface,const DragonFrame*,int center_x_q8,int center_y_q8,DragonClip,DragonGPUStats*);
int dragon_gpu_copy(DragonSurface,DragonSurface,DragonClip,unsigned alpha_q5,DragonGPUStats*);
/* Source crop anchor is PC integer half, positions round to nearest pixel. */
int dragon_gpu_origin(int center_q8,int anchor_q8);
#endif
