#ifndef AD_ENEMY_GPU_H
#define AD_ENEMY_GPU_H
#include "enemy.h"
#include "enemy_data.h"
#include "dragon_gpu.h"
/* User-approved experimental whole-cache guard defaults to ZERO.
 * This is not a frame-rate guarantee. DDR planes retain existing 24px/4us. */
void enemy_gpu_set_whole_guard(unsigned microseconds);
void enemy_gpu_reset(void);
int enemy_gpu_warm(void);
unsigned enemy_gpu_resident(void);
int enemy_gpu_pose(const EnemyPose*,const EnemyCacheFrame**,const EnemyFrame**);
DragonClip enemy_gpu_bounds(const EnemyPose*);
typedef struct { uint64_t wait_us,enqueue_us; uint32_t fences,commands,pixels,template_hits,template_planes,plane_alpha_runtime_ops,template_fallbacks; } EnemyGPUMetrics;
const EnemyGPUMetrics *enemy_gpu_metrics(void);
int enemy_gpu_fence(void);
typedef struct { const EnemyCacheFrame *cache; const EnemyFrame *frame; DragonClip bounds; const EnemyPlane *planes; const uint16_t *source; const uint16_t *const *pixels; unsigned template_id; } EnemyGPUResolved;
int enemy_gpu_resolve(const EnemyPose*,EnemyGPUResolved*);
int enemy_gpu_draw_resolved(DragonSurface,const EnemyGPUResolved*,DragonClip,unsigned,DragonGPUStats*);
int enemy_gpu_draw_alpha(DragonSurface,const EnemyPose*,DragonClip,unsigned,DragonGPUStats*);
int enemy_gpu_draw(DragonSurface,const EnemyPose*,DragonClip,DragonGPUStats*);
/* Metadata support; never dereferences image pixels. */
const EnemySpan *enemy_gpu_support(const EnemyPose*,unsigned *count);
#endif
