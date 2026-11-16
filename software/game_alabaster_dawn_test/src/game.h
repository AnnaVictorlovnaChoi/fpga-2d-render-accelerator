#ifndef G21_GAME_H
#define G21_GAME_H
#include <stdint.h>
#define G21_BULLET_CAP 1500
#define G21_EFFECT_CAP 150
#define G21_TICK_MS 20u
/* All player coordinates are map-local feet anchors, Q8. */
enum { GS_START, GS_PLAY, GS_JUMP, GS_PAUSE, GS_DEFEAT_ISLAND, GS_DEFEAT_GREY, GS_DEFEAT_CG, GS_GAMEOVER, GS_WIN, GS_QUIT };
enum { KEY_ESC=1, KEY_A=2, KEY_W=4, KEY_S=8, KEY_D=16, KEY_E=32, KEY_K=64, KEY_J=128 };
typedef struct { int16_t x,y; uint16_t age,life,traj; uint8_t alive,kind; } G21Bullet;
typedef struct { int16_t x,y; uint16_t age; uint8_t alive,kind; } G21Effect;
typedef struct {
 uint32_t game_ms,state_ms,question_ms,regen_ms,hit_ms,laser_tick;
 int32_t px_q8,py_q8,jump_x_q8,jump_y_q8;
 uint16_t hp,stamina; uint8_t state,resume_state,route,node,question_no,question_total;
 uint8_t dir,moving,dodging,near_option,option_count,answer_mask,jump_to;
 uint16_t dodge_ms,jump_ms; int32_t dodge_sx,dodge_sy;
 uint32_t physical_ms,slow_ms,effect_phase_ms;
 uint16_t table_phase; uint8_t laser_hit_mask,dir_tap_valid,esc_tap_valid;
 int32_t move_remainder_x,move_remainder_y;
 /* enemy_tick is the current PC frame: frame 0 is spawned on first PLAY. */
 uint32_t enemy_tick,shot_timer[16],restart_generation;uint8_t shot_dir[16];
 int32_t fire_remaining[16];uint8_t fire_started,restart_release;
 uint16_t bullet_count,effect_count;uint8_t defeat_frame,defeat_level,ui_dirty,exit_requested;
 uint8_t key_stable,key_candidate;uint32_t key_changed[8],last_dir_tap[4],last_esc;
 uint8_t escape_pending,prev_state;uint32_t animation_ms;
 G21Bullet bullets[G21_BULLET_CAP];G21Effect effects[G21_EFFECT_CAP];
} Game;
void game_init(Game*);
void game_step(Game*,uint32_t dt_ms,uint8_t raw_low_active);
void game_rendered(Game*);
/* Defined by generated resources.c and assets.h; renderer never changes simulation. */
#endif
