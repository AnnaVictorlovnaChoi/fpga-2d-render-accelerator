#ifndef AD_PAUSE_H
#define AD_PAUSE_H
#include "game_flow.h"
#include "visible.h"
#include "scene.h"
#include "wave_schedule.h"
/* Explicit transactions; render/consume and input real clock remain live. */
typedef enum { PAUSE_RUNNING,PAUSE_DRAIN,PAUSE_ENTER_SUBMITTED,PAUSE_STABLE,PAUSE_RESUME_SUBMITTED,PAUSE_STATS_SUBMITTED } PauseState;
typedef struct { PauseState state; uint32_t esc_ms; uint8_t esc_pending,deferred_command; } PauseControl;
typedef struct {
 uint32_t version,canary,valid,checksum;
 Player player; ProjectileSystem arrows; Dragon dragon; GameFlow flow;
 SceneSnapshot scene; RendererSnapshot world;
 VisibleSignature completed; uint32_t visible_valid,saved_target,world_now,flow_now;
 WaveScheduleState schedule;
 uint32_t tail_canary;
} PauseSnapshot;
void pause_init(PauseControl*);
/* Returns 1 toggle, 2 double/end. Initial enter/resume edges are discarded.
 * Stats submissions retain physical ESC timing but defer commands until STABLE. */
int pause_input(PauseControl*,uint32_t real_ms,uint8_t esc_edge,int enabled);
void pause_save(const Player*,const ProjectileSystem*,const Dragon*,const GameFlow*,const RendererSnapshot*,const VisibleState*,uint32_t,uint32_t);
int pause_restore(Player*,ProjectileSystem*,Dragon*,GameFlow*,uint32_t*,uint32_t*);
const PauseSnapshot *pause_snapshot(void);
#endif
