#ifndef AD_GAME_FLOW_H
#define AD_GAME_FLOW_H

#include <stdint.h>
#include "player.h"
#include "renderer.h"
#include "flow_data.h"
#include "newwave_state.h"
#include "game_rules.h"

/* G2.10 game-flow state machine. All timestamps are wrapping milliseconds.
 * World coordinates are map-local integer/Q8 coordinates; screen y = map y + 50.
 * The flow module never scans, resizes, mattes, or writes image pixels on CPU. */
typedef enum {
    FLOW_PLAY = 0,
    FLOW_ALTAR_START,
    FLOW_ENDING_FADE,
    FLOW_ENDING_DEATH,
    FLOW_ENDING_HOLD,
    FLOW_ENDING_GAMEOVER
} FlowState;

typedef enum {
    FLOW_END_NONE = 0,
    FLOW_END_DOUBLE_ESC,
    FLOW_END_HP_ZERO
} FlowEndReason;

typedef struct {
    FlowState state;
    FlowEndReason reason;
    uint32_t state_start_ms;
    uint32_t esc_first_ms;
    uint8_t esc_pending;
    uint8_t altar_triggered;
    uint8_t logic_locked;
    uint8_t world_frozen;
    uint8_t death_frame;
    uint8_t fade_q5;
    uint8_t single_reset_pending;
    uint32_t schedule_clock; uint8_t schedule_started;
    GameRules rules;
    NewWave wave; /* Full logic state is included in PauseSnapshot.flow checksum. */
    WaveVisual frozen_wave; uint32_t frozen_scene_version; /* actual GPU-built identity */
    uint32_t enemy_knight_events,enemy_bullet_hit_events,enemy_gate_events;
    uint32_t last_enemy_event_ms,last_enemy_event_id;
    RendererSnapshot frozen_snapshot;
} GameFlow;

/* Flow resource declarations are owned by flow_data.h. */

/* Lifecycle and input. Input is edge-level; no input is buffered while locked. */
void game_flow_init(GameFlow *f, uint32_t now_ms);
void game_flow_update(GameFlow *f, uint32_t now_ms, uint8_t key_edges,
                      Player *player, const RendererSnapshot *latest_safe);
/* Sole event consumer: reliable damage first, diagnostic queue next.
 * Economy is applied separately from authoritative cumulative counters. */
void game_flow_enemy_events(GameFlow *f);
int game_flow_is_locked(const GameFlow *f);
int game_flow_should_end(const GameFlow *f);

/* Altar map anchor/footprint/near60 derive from wave_helper.h. J is
 * accepted only in this near range; lever handling remains separate. */
int game_flow_near_altar(int player_x, int player_y);
int game_flow_try_start_altar(GameFlow *f, uint32_t now_ms,
                              int player_x, int player_y, uint8_t j_edge);

/* Safe frame hand-off: caller invokes only after the in-flight restore/draw
 * has completed and the latest frame has been submitted/consumed. */
int game_flow_commit_safe_frame(GameFlow *f, uint32_t now_ms, const RendererSnapshot *safe);

/* Time-based flow queries. Rendering remains GPU-only and uses these plans. */
int game_flow_altar_frame(const GameFlow *f, uint32_t now_ms);
int game_flow_death_frame(const GameFlow *f, uint32_t now_ms);
uint8_t game_flow_fade_q5(const GameFlow *f, uint32_t now_ms);

/* PC golden fade rule. CPU computes only tile weight; GPU applies it to RGB.
 * Source tile alpha is q5 (0..32), global fade is q5 (0..32). */
static inline uint8_t game_flow_alpha_effect(uint8_t tile_q5, uint8_t global_q5) {
    return (uint8_t)(((uint16_t)tile_q5 * global_q5 + 16u) >> 5);
}

#endif
