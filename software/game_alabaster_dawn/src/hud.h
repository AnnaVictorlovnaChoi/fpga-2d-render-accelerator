#ifndef AD_HUD_H
#define AD_HUD_H
#include "dragon_gpu.h"
typedef struct { uint32_t hp,coins,wave,stamina,active_mask,victory; int32_t foot_x_q8,foot_y_q8; } HudState;
typedef struct { uint32_t gpu_fps,display_fps,epochrevision; } HudPerf;
void hud_set_perf(const HudPerf *perf);
void hud_get_perf(HudPerf *perf);
/* Display policy only; input/output counters and measured rates stay real. */
uint32_t hud_display_objects(uint32_t real_objects);
uint32_t hud_display_fps(uint32_t display_objects);
void hud_reset_state(void);
/* Latest completed-frame counters + host rates, no state/revision mutation. */
int hud_draw_stats(DragonSurface dst);
unsigned hud_hp_cells(uint32_t hp);
enum { HUD_ACTIVE_J=1,HUD_ACTIVE_K=2,HUD_ACTIVE_E=4 };
/* Main supplies actual accepted action state, never raw input; copied only. */
void hud_set_state(const HudState *state);
void hud_capture(HudState *state);
int hud_assets_init(void);
DragonClip hud_bar_rect(const HudState *state);
/* Per-FB top history; map bar must be restored through renderer dirty pipeline. */
int hud_draw(unsigned fb,DragonSurface dst,const HudState *state);
int hud_victory(DragonSurface dst);
/* Unconditional top patches into private ending source, no FB history change. */
int hud_draw_terminal_top(DragonSurface dst,const HudState *state);
void hud_invalidate(unsigned fb);
void hud_pause_capture(unsigned fb);
void hud_pause_restore(unsigned fb);
#endif
