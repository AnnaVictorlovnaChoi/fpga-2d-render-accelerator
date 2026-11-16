#ifndef G23_RENDERER_H
#define G23_RENDERER_H
#include <stdint.h>
#include "projectile.h"
#include "dragon.h"
/* Main owns this immutable render snapshot, independent of live input updates.
 * World Y is map-local (screen Y = world Y + 50). All times are microseconds. */
typedef struct {
    int32_t world_x_q8, world_y_q8;
    uint32_t action, dir8, elapsed_us;
    uint32_t scene_version, arrow_count;
    DragonPose dragon;
    ArrowPose arrows[PROJECTILE_CAPACITY];
} RendererSnapshot;
typedef struct { int32_t x, y, w, h; } RendererRect;
typedef struct {
    uint32_t copy_cmds, sprite_cmds, fifo_retries;
    uint32_t copy_prepare_calls, copy_enqueue_calls;
    uint32_t copy_pixels, sprite_pixels;
    uint32_t player_cmds, player_pixels, arrow_cmds, arrow_pixels, sprite_fences;
    uint32_t alpha_cmds,alpha_pixels,alpha_fences;
    uint32_t framebuffer, resolved_action, resolved_dir, resolved_frame;
    uint32_t source_stride;
    int32_t dest_x, dest_y; /* Rounded, UNCLIPPED frame origin. */
    RendererRect clipped;
    RendererSnapshot snapshot;
    int draw_valid;
} RendererStats;
/* Enqueue only, NOT GPU fences. Main must sequence:
 * restore -> DONE/!BUSY/EMPTY -> draw -> DONE/!BUSY/EMPTY -> submit.
 * Return zero on success, negative on error; only enqueue -2 is retried.
 * Valid framebuffer IDs are 1..3. Never restore a displaying framebuffer. */
#include "enemy_gpu.h"
/* Call once with immutable capture BEFORE restore. NULL clears the plan.
 * draw enemies after player, before storm/arrow/countdown. Copies into BSS. */
typedef struct { uint32_t span_insert,node_visits,peak_row_nodes,fast_commands,complex_commands; uint32_t fast_objects,complex_objects,bullet_commands,commands,total_pixels,sprite_commands,copy_commands,alpha_commands,fences,owner_runs; uint64_t total_ticks,classify_ticks,owner_ticks,source_draw_ticks,composite_ticks,fence_ticks;
 uint64_t plan_touch_ticks,playerwall_ticks,bins_clear_ticks,bins_build_ticks,candidate_union_ticks;
 uint64_t render_cpu_work_us,render_wait_us,enqueue_us,wait_us; uint32_t cpu_ops;
 uint32_t rect_bound_recomputes,bounds_lookups,enemy_objects,span_visits,player_row_queries,wall_span_visits,binwords,candidatepairs,rootsteps; } RendererEnemyStats;
const RendererEnemyStats *renderer_enemy_stats(void);
int renderer_enemy_snapshot(const EnemySnapshot*);
/* Borrow capture until replaced/cleared: caller must keep the complete object
 * alive and immutable through restore/draw/flow freeze and GPU completion.
 * Pause capture clones the submitted FB cache (legacy fallback: current plan);
 * pause_end_freeze temporarily selects the clone and restores the working plan.
 * Ordinary pause_restore restores pixels/history only, as before.
 * NULL clears. Invalid input leaves the previous plan unchanged. */
int renderer_enemy_snapshot_view(const EnemySnapshot*);
int renderer_enemy_draw(unsigned framebuffer);
int renderer_restore(unsigned framebuffer);
void renderer_countdown_plan(const RendererSnapshot*);
int renderer_countdown_finish(unsigned framebuffer);
int renderer_draw(unsigned framebuffer, const RendererSnapshot *snapshot);
/* Call only after GPU complete. 1=arrow enqueued, 0=all complete. */
int renderer_draw_next(unsigned framebuffer,const RendererSnapshot *snapshot);
int renderer_ready(void);
/* Caller guarantees GPU DONE/FIFO empty and all pending submits consumed.
 * Metadata invalidation only: no framebuffer writes/selection or asset decode. */
int renderer_reset_for_new_game(void);
/* GPU-owned, registration/debug only: CPU must not dereference pixels. */
#include "dragon_gpu.h"
DragonSurface renderer_scratch(int storm);
int renderer_occlude(unsigned framebuffer);
/* Counters reset at restore; accepted commands/pixels only, retries separately.
 * Stats remain valid until the next restore. Main measures completion timing. */
const RendererStats *renderer_stats(void);
/* Sum of main render stages since restore. External VSYNC not included.
 * Uninstrumented helper-internal waits remain conservatively charged to work. */
uint64_t renderer_cpu_work_us(void);
uint64_t renderer_wait_us(void);
#include "flow_data.h"
#include "countdown_data.h"
int renderer_countdown_draw_surface(DragonSurface,CountdownGroup,unsigned,int,int,unsigned);
int renderer_countdown_draw_resolved(DragonSurface,const CountdownFrame*,int,int,unsigned);
/* Drain ALL working/ready/pending frames first: GPU idle and latest scene
 * background/base/wave epoch must correspond to displayed_fb. Per-FB snapshots
 * own enemy/world metadata, not historical scene pixels. GPU-only snapshots. */
int renderer_flow_freeze(unsigned displayed_fb,const RendererSnapshot *frozen);
DragonSurface renderer_flow_snapshot(int without_player);
int renderer_flow_restore(unsigned fb,int without_player);
int renderer_flow_gray(unsigned fb,unsigned q5);
/* Overlay only, caller restores immutable last safe world and owns target. */
int renderer_victory(unsigned fb);
int renderer_flow_death_restore(unsigned fb,const RendererSnapshot*);
int renderer_flow_draw_surface(DragonSurface,FlowGroup,unsigned,int,int,unsigned);
int renderer_flow_sprite(unsigned fb,FlowGroup group,unsigned index,int x_q8,int y_q8,unsigned global_q5);
/* Pause-only full-frame GPU transactions. Targets must not be displaying.
 * Capture history belongs to the copied clean image, not old target contents. */
/* Successful-submit metadata only: owns copies per physical FB, never changes
 * the working enemy plan. Call with the submitted PendingFrame snapshots. */
int renderer_record_submitted(unsigned fb,const RendererSnapshot*,const EnemySnapshot*);
int renderer_pause_capture(unsigned displayed_fb);
int renderer_pause_restore(unsigned target_fb);
int renderer_pause_show(unsigned target_fb);
int renderer_pause_end_freeze(const RendererSnapshot *world);
unsigned renderer_dragon_alpha(const DragonFrame *frame,int cx,int cy,DragonClip clip);
unsigned renderer_flow_alpha(FlowGroup group,unsigned index,int cx,int cy,unsigned global,DragonClip clip);
#endif
