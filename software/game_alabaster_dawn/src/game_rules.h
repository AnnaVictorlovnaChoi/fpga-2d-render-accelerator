#ifndef AD_GAME_RULES_H
#define AD_GAME_RULES_H
#include <stdint.h>
#define GAME_RULES_MAX_HP 160u
#define GAME_RULES_WAVES 3u
#define GAME_RULES_MELEE 1u
#define GAME_RULES_RANGED 2u
#define GAME_RULES_DRAGON 4u
typedef enum { GAME_RULES_ALIVE=0, GAME_RULES_LOST=1, GAME_RULES_VICTORY=2 } GameRulesStatus;
/* Pointer-free: included verbatim in GameFlow and its pause checksum. */
typedef struct {
 uint32_t hp,coins,wave,hurt_until_ms,revision;
 uint32_t rewarded_kills,gate_arrivals;
 uint8_t status,action_mask,hurt_active,wave_active;
} GameRules;
void game_rules_init(GameRules *r);
const GameRules *game_rules_get(const GameRules *r);
int game_rules_invulnerable(const GameRules *r,uint32_t world_ms,int dodge);
void game_rules_tick(GameRules *r,uint32_t world_ms,uint8_t action_mask);
/* Monotonic uint32 counters: modulo differences, one net economic tick. */
void game_rules_apply_counters(GameRules *r,uint32_t rewarded_kills,uint32_t gate_arrivals);
int game_rules_damage(GameRules *r,uint32_t event_world_ms);
int game_rules_start_wave(GameRules *r);
/* Returns 1 for next preparation, 2 for final victory, otherwise zero. */
int game_rules_clear_wave(GameRules *r,int spawn_ended,uint32_t enemies,uint32_t bullets);
#endif
