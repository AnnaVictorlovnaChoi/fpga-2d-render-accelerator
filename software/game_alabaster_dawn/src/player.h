#ifndef AD_PLAYER_H
#define AD_PLAYER_H
#include <stdint.h>
#include "runtime.h"
enum { DIR_E, DIR_NE, DIR_N, DIR_NW, DIR_W, DIR_SW, DIR_S, DIR_SE };
enum { ACT_IDLE=RUNTIME_ACT_IDLE, ACT_WALK=RUNTIME_ACT_WALK,
       ACT_MELEE=RUNTIME_ACT_MELEE, ACT_BOW=RUNTIME_ACT_RANGED,
       ACT_DODGE=RUNTIME_ACT_DODGE };
/* Coordinates are map-local foot/base coordinates, Q8 and integer snapshots.
 * Renderer adds its screen y offset; player never applies the +50 offset.
 * All public timestamps are wrapping uint32 milliseconds except *_us. */
typedef struct {
    int32_t xq, yq;
    int x, y, dir, action;
    uint32_t action_start, anim_elapsed_us, last_ms;
    uint8_t stable, candidate;
    uint32_t changed_ms[8];
    uint32_t tap_ms;
    int tap_dir;
    uint8_t tapped, dodge_blocked;
    uint32_t dodge_offset_q8;
    uint32_t walk_remainder;
    uint32_t e_events, j_events, k_events, reset_events;
    uint8_t flow_locked;
    uint8_t stamina; /* 0..8; consumed only by a successful dodge start. */
    uint32_t stamina_regen_ms; /* world-clock recovery anchor, wraps naturally */
} Player;
enum { PLAYER_STAMINA_MAX = 8, PLAYER_STAMINA_REGEN_MS = 2000 };
uint8_t player_stamina(const Player *p);
void player_init(Player *p, uint32_t now_ms);
void player_update(Player *p, uint32_t now_ms, uint8_t raw_active_low);
void player_set_flow_locked(Player *p, int locked);
int player_flow_locked(const Player *p);
/* Called only after game_flow's deferred 280 ms single-ESC window. */
int player_legacy_reset_after_esc_window(Player *p, uint32_t now_ms);
#endif
