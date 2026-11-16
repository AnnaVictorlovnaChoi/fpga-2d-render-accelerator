#ifndef AD_ENEMY_H
#define AD_ENEMY_H
#include <stdint.h>
#include <stddef.h>
#include "player.h"
#include "projectile.h"
#define ENEMY_CAPACITY 1000
#define ENEMY_BULLET_CAPACITY 750
#define ENEMY_STEP_MS 20u
#define ENEMY_REPLAN_QUOTA 16u
#define ENEMY_REPLAN_NEAR_MARGIN 32u
#define ENEMY_MAX_STEPS 2u
#define ENEMY_BUDGET_OBJECTS 700u
#define ENEMY_TARGET_ENEMIES 600u
#define ENEMY_FRAME_CPU_BUDGET_US 5000u
#define ENEMY_VISIBLE_CAPACITY (ENEMY_CAPACITY+ENEMY_BULLET_CAPACITY)
typedef enum { ENEMY_DK, ENEMY_WOLF, ENEMY_GARGOYLE, ENEMY_RED_BULLET } EnemyType;
/* Pose source namespace differs deliberately from DDR EnemyGroup. */
enum { ENEMY_SOURCE_DK_WALK=0, ENEMY_SOURCE_WOLF_WALK=1, ENEMY_SOURCE_GARG_WALK=2, ENEMY_SOURCE_BULLET=3, ENEMY_SOURCE_DK_ATTACK=4, ENEMY_SOURCE_DK_WHITE=5, ENEMY_SOURCE_DK_RED=8, ENEMY_SOURCE_WOLF_RED=9, ENEMY_SOURCE_GARG_RED=10 };
enum { ENEMY_WHITE=1, ENEMY_RED=2, ENEMY_ATTACK=4, ENEMY_ENTER=8 };
/* All geometry map-local Q8; renderer adds +50 screen Y. source_group:
 * 0 DK walk, 1 wolf walk, 2 gargoyle walk, 3 bullet, 4 DK attack,
 * 5..7 white DK/wolf/gargoyle, 8..10 red DK/wolf/gargoyle.
 * frame zero-based, cache_id 1..7 only normal walk/bullet, otherwise 0.
 * Bounds conservative map-local sprite bounds, override with asset metadata. */
typedef struct { int32_t xq,yq; int16_t x,y,w,h; uint32_t id,spawn_serial; uint8_t type,source_group,frame,flags,cache_id; } EnemyPose;
typedef struct { uint32_t version,world_ms; uint16_t count,enemy_count,bullet_count; EnemyPose visibleSprites[ENEMY_VISIBLE_CAPACITY]; } EnemySnapshot;
/* speed[] is legacy nominal px/s (84/126/105), NOT the current movement rate.
 * Use enemy_motion_rate() for exact movement metadata; bullet_speed stays px/s. */
typedef struct { uint16_t speed[3],walk_ms,attack_frame_ms,attack_cooldown_ms,attack_radius,fire_ms,bullet_speed,gate_radius,melee_radius; int16_t melee_front_dot; uint16_t spawn_rate[3],melee_window_start_numerator,melee_window_end_numerator,melee_window_denominator; uint32_t ramp_ms; uint8_t bullet_wall_collision; } EnemyConfig;
typedef struct { uint32_t version,spawned,killed,arrived,spawn_deferred,bullet_deferred,knight_attacks,bullet_hits,nav_rebuilds,event_overflow,nav_edge_rebuilds,nav_edge_tests,nav_field_ops,sim_ms,discarded_ms,substeps,nav_entry_tests,nav_entry_reject,nav_recover,nav_recover_tests,nav_recovered,nav_reachable,nav_stuck,nav_unreachable,nav_inside,nav_hard_queries,route_steps,route_invalid,route_deferred,replan_used,replan_deferred,replan_urgent,replan_peak,replan_step_used,replan_step_urgent,replan_queue_push,replan_queue_pop,replan_empty_returns,replan_scan,route_fast_reads,hardchecks_saved,pit_steps_skipped,collision_candidates,broadphase_reject,divisions,expiryops; uint32_t rewarded_kills,gate_arrivals,knighthits,bullethits,field_initial,field_periodic,field_displacement,ray_builds,replan_checks,replan_cache_hits,replan_reason_wall,replan_reason_field,replan_reason_ray; uint16_t active,bullets; } EnemyStats;
/* knight_attacks counts windups; knighthits counts eligible end contacts, not HP.
 * KNIGHT_ATTACK is damage only: emitted once at the end of a 3 x 150ms swing,
 * in current radius 36, non-dodge/non-guard. No windup damage/event. */
typedef enum { ENEMY_EVENT_KNIGHT_ATTACK=1, ENEMY_EVENT_BULLET_PLAYER_HIT, ENEMY_EVENT_GATE_ARRIVAL, ENEMY_EVENT_KILLED } EnemyEventKind;
typedef struct { uint32_t world_ms,id; int32_t xq,yq; uint8_t kind,type,source,reason; } EnemyEvent;
typedef enum { ENEMY_DAMAGE_NONE, ENEMY_DAMAGE_MELEE, ENEMY_DAMAGE_ARROW, ENEMY_DAMAGE_STORM, ENEMY_DAMAGE_PIT, ENEMY_DAMAGE_KNIGHT, ENEMY_DAMAGE_BULLET } EnemyDamageSource;
/* source identifies weapon/actor; reason distinguishes rewarded combat from pit. */
typedef enum { ENEMY_KILL_NONE, ENEMY_KILL_PLAYER, ENEMY_KILL_PIT } EnemyKillReason;
/* Call before update. Hurt shield consumes colliding bullets; dodge does not.
 * Economy uses uint32 differences of rewarded_kills/gate_arrivals, not events.
 * Damage events are coalesced per world timestamp; drain every update. */
void enemy_set_player_damage_guard(int invulnerable);
typedef struct { int active; int x,y,w,h; } EnemyStorm;
/* Singleton system is internal static BSS, no heap and no huge main stack.
 * Fixed 20ms internal clock, max 2 steps/call (approved bounded catch-up); excess whole-step debt is
 * discarded and counted. World/player/dragon clocks are NOT modified.
 * Snapshot world_ms is capture time; pose ages use the saved internal clock.
 * Event timestamps are world observation time, not enemy simulation time. */
void enemy_resolve_projectiles(ProjectileSystem*);
const EnemyConfig *enemy_config(void);
enum { ENEMY_MOTION_ONLINE_DK=0, ENEMY_MOTION_PTR9=1 };
/* Exact pixels/second, not Q8 or rounded integer pixels. PTR9 metadata describes
 * the required PC asset rate; runtime consumes one point per 20ms unchanged. */
typedef struct { uint16_t numerator,denominator; uint8_t source; } EnemyMotionRate;
const EnemyMotionRate *enemy_motion_rate(unsigned type); /* NULL for non-mobs */
void enemy_init(uint32_t world_ms);
/* Build wall/entry navigation before active simulation, without moving pools. */
void enemy_prepare_navigation(const Player*);
/* Main polls and commits wave events; update never spawns. Birth/fire use the
 * internal simulation clock. Requires prepared navigation and absent altar.
 * type DK=0/WOLF=1/GARGOYLE=2, side left=0/right=1/bottom=2;
 * entry_id 0..429 on sides, 0..14 bottom; variant 0..1; no bottom gargoyle.
 * Failure leaves pools intact; no fallback entry except DK nearest legal row. */
enum { ENEMY_SPAWN_OK=0, ENEMY_SPAWN_CAPACITY=1, ENEMY_SPAWN_NOT_READY=2,
       ENEMY_SPAWN_ROUTE=3, ENEMY_SPAWN_INVALID=-1 };
int enemy_spawn_scheduled(unsigned type,unsigned side,unsigned entry_id,unsigned variant);
void enemy_update(const Player*,ProjectileSystem*,uint32_t world_ms,int wave_active,const EnemyStorm*);
uint32_t enemy_version(void);
void enemy_snapshot(EnemySnapshot*,uint32_t world_ms);
const EnemyStats *enemy_stats(void);
unsigned enemy_events(EnemyEvent *out,unsigned capacity);
/* Complete pointer-free metadata including navigation/RNG/pools/timers.
 * Caller supplies static DDR storage, size from enemy_save_size(). */
size_t enemy_save_size(void);
int enemy_save(void *dst,size_t size);
int enemy_restore(const void *src,size_t size);
#endif
