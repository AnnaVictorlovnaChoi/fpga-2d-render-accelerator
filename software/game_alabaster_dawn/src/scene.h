#ifndef G26_SCENE_H
#define G26_SCENE_H
#include <stdint.h>
#include "dragon.h"
#include "dragon_gpu.h"
#include "newwave_state.h"
void scene_dragon(const DragonPose*);
void scene_flow_changed(void);
void scene_flow_altar(int frame_index,int hint,unsigned q5);
DragonSurface scene_base(int include_dragon);
const DragonPose *scene_dragon_pose(void);
typedef struct { int x,y,w,h,base; } SceneWall;
typedef struct { int x,y,w,h; } SceneRect;
SceneRect scene_countdown_bounds(unsigned icon);
/* Cumulative metadata construction count; 48 only at first resource use. */
uint32_t scene_countdown_rect_recomputes(void);
/* Top overlay only; clip is intersected with dst. Caller must restore clean
 * pixels, redraw dynamic layers, then overlay each dirty pixel ONCE (Q5).
 * Alpha-only scene changes report slot bounds, never require whole-icon draw. */
int scene_countdown_overlay(DragonSurface dst,SceneRect clip,WaveVisual visual);
unsigned scene_changes(uint32_t since,SceneRect *out,unsigned cap);
/* Per-framebuffer accumulated damage, independent of animation journal age. */
unsigned scene_changes_fb(unsigned fb,SceneRect *out,unsigned cap);
typedef struct { uint32_t rebuilds,rebuild_us,rebuild_max_us,wall_steps,trap_steps,toggles,rejected,version; int nearest; } SceneStats;
extern const SceneWall scene_walls[4];
int scene_init(void);
/* GPU idle, no pending source users. Reuses decoded assets; caller rebuilds.
 * Does not touch framebuffers; version advances to invalidate geometry. */
int scene_reset_runtime(void);
/* Account logical objects already present in immutable composed background. */
void scene_metrics_visible(void);
uint32_t scene_alpha_objects(void);
int scene_mechanisms_busy(void);
void scene_update(uint32_t now,int x,int y);
/* Gate new interactions/hints only; already-started wall/pit animations finish. */
void scene_lever_enabled(int enabled);
void scene_wave(WaveVisual visual);
WaveVisual scene_wave_built(void);
uint32_t scene_built_version(void);

/* Debounced J edge only, caller must be idle/walk. 1 consumes J, even rejected. */
int scene_interact(uint32_t now,int x,int y);
int scene_toggle(unsigned control,uint32_t now,int occupant_x,int occupant_y);
int scene_walkable(int x,int y);
/* Enemy footpoint geometry: pit is a hazard, never a path obstacle.
 * entry=1 exempts only the exterior boundary, not raised-wall extensions. */
int scene_enemy_hard(int x,int y,int entry);
int scene_enemy_pit(int x,int y);
/* Read-only logical hazard enable; no render/cache side effects. */
int scene_enemy_pit_active(void);
uint32_t scene_enemy_wall_version(void);
int scene_nearest(int x,int y);
uint32_t scene_version(void);
/* Called ONLY with GPU idle; source stays immutable through submit. */
const uint16_t *scene_background(void);
int scene_rebuild(void);
int scene_wall_frame(void);
const SceneStats *scene_stats(void);
/* Logic only: GPU caches, damage journals and per-FB ownership stay live. */
typedef struct {
 int target[2],active[2],animating[2],frame[2]; uint32_t began[2];
 int altar_frame,altar_hint,altar_q5,lever_enabled; DragonPose dragon; SceneStats stats;
 WaveVisual wave_desired,wave_built; uint32_t built_version,built_alpha_mask;
} SceneSnapshot;
void scene_snapshot_save(SceneSnapshot *s);
void scene_snapshot_restore(const SceneSnapshot *s);
#endif
