/* Generated offline opaque RGB565; zero is valid black, no color key. */
#ifndef SCENE_DATA_H
#define SCENE_DATA_H
#include <stdint.h>
typedef struct { uint16_t w,h,count; } SceneAsset;
typedef struct { uint16_t x,w; } SceneSpan;
typedef struct { uint16_t x,y,w,h; } SceneMaskRect;
enum { SCENE_WALL_A,SCENE_WALL_B,SCENE_WALL_C,SCENE_WALL_D,SCENE_PIT_COLLAPSE,SCENE_PIT_RESTORE,SCENE_WALL_LEVER,SCENE_TRAP_LEVER,SCENE_HINT,SCENE_ASSET_COUNT };
extern const SceneAsset scene_assets[SCENE_ASSET_COUNT];
/* Startup-only puff plus reversible byte transforms; no RGBA or runtime render.
 * Static arena, no heap. NULL getters before successful init/invalid args.
 * Hint frames 0/1 select left/right base positions (754/844,101).
 * Wall patch must copy only mask support rects, not whole alpha-zero holes.
 * Restore pit aliases reversed collapse. Opaque black is never changed to 1. */
int scene_assets_init(void);
const uint16_t *scene_patch(unsigned asset,unsigned frame);
const uint16_t *scene_rgb565(unsigned asset,unsigned frame);
const SceneSpan *scene_mask_span(unsigned wall,unsigned frame,unsigned row,unsigned *count);
const SceneMaskRect *scene_mask_rects(unsigned wall,unsigned frame,unsigned *count);
#endif
