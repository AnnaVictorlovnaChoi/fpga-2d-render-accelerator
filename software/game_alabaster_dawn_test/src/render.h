#ifndef G21_RENDER_H
#define G21_RENDER_H
#include <stdint.h>
#include "game.h"
/* assets_init and frame_pipeline_init must precede render_frame. Initialization
 * touches journals only, NEVER the playing framebuffer. fb is an acquired 1..3.
 * A successful frame includes a real GPU completion fence; caller then submits.
 * Failure invalidates that FB journal and must NOT be submitted. */
int render_init(void);
int render_frame(unsigned fb, const Game *game);
uint32_t render_commands(void);
uint32_t render_fences(void);
/* Visible dynamic instances, not commands; alpha_objects counts each bullet or
 * effect once. Rates are previous-frame tenths/second supplied by main. */
typedef struct { uint32_t sprite_count,alpha_objects,alpha_commands; } RenderStats;
const RenderStats *render_stats(void);
void render_set_rates(uint32_t gpu_fps_x10,uint32_t display_fps_x10);
/* Foreground-owned snapshots; stages identify the failing batch, no printf ABI. */
enum { RENDER_STAGE_IDLE, RENDER_STAGE_WARM, RENDER_STAGE_RESTORE,
       RENDER_STAGE_BULLETS, RENDER_STAGE_EFFECTS, RENDER_STAGE_ACTORS,
       RENDER_STAGE_UI, RENDER_STAGE_DEFEAT, RENDER_STAGE_FINAL };
typedef struct {
 uint32_t op,src,dst,w,h,src_stride,dst_stride,color;
 uint32_t status,fifo,stage; int rc;
} LastGpuCommand;
typedef struct { uint32_t fill,use,fallback,timeout,resident; } RenderCacheStats;
const LastGpuCommand *render_last_gpu_command(void);
const RenderCacheStats *render_cache_stats(void);
#endif
