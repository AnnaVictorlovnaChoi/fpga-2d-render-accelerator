#include "game_flow.h"
#include "wave_helper.h"
#include "enemy.h"
void game_flow_enemy_events(GameFlow*f){
 static EnemyEvent batch[16];
 for(unsigned budget=0;budget<4;budget++){unsigned n=enemy_events(batch,16);for(unsigned i=0;i<n;i++){
  const EnemyEvent*e=&batch[i];
  if(e->kind==ENEMY_EVENT_KNIGHT_ATTACK||e->kind==ENEMY_EVENT_BULLET_PLAYER_HIT)
   (void)game_rules_damage(&f->rules,e->world_ms);
  if(e->kind==ENEMY_EVENT_KNIGHT_ATTACK)++f->enemy_knight_events;
  else if(e->kind==ENEMY_EVENT_BULLET_PLAYER_HIT)++f->enemy_bullet_hit_events;
  else if(e->kind==ENEMY_EVENT_GATE_ARRIVAL)++f->enemy_gate_events;
  f->last_enemy_event_ms=e->world_ms;f->last_enemy_event_id=e->id;
 }if(n<16)break;}

}

#define KEY_ESC (1u << 0)
#define KEY_J   (1u << 7)

#define ALTAR_POSE_MS       200u
#define ALTAR_POSE_COUNT    5u
#define ALTAR_TOTAL_MS      1800u
#define ALTAR_FINAL_FADE_MS 800u
#define END_FADE_MS         500u
#define DEATH_POSE_MS       200u
#define DEATH_POSE_COUNT    5u
#define DEATH_TOTAL_MS      1000u
#define DEATH_HOLD_MS       2000u
#define ESC_DOUBLE_MS       280u

static int elapsed(uint32_t now, uint32_t start, uint32_t span) {
    return (uint32_t)(now - start) >= span;
}

void game_flow_init(GameFlow *f, uint32_t now_ms) {
    *f = (GameFlow){0};
    f->state = FLOW_PLAY;
    game_rules_init(&f->rules);
    f->state_start_ms = now_ms;
}

int game_flow_is_locked(const GameFlow *f) {
    return f->logic_locked != 0;
}

int game_flow_should_end(const GameFlow *f) {
    return f->state >= FLOW_ENDING_FADE;
}

int game_flow_near_altar(int player_x, int player_y) {
    int dx=player_x-AD_ALTAR_X, dy=player_y-AD_ALTAR_Y;
    return dx>=-AD_ALTAR_NEAR && dx<=AD_ALTAR_NEAR && dy>=-AD_ALTAR_NEAR && dy<=AD_ALTAR_NEAR && dx*dx+dy*dy<=AD_ALTAR_NEAR*AD_ALTAR_NEAR;
}

int game_flow_try_start_altar(GameFlow *f, uint32_t now_ms,
                              int player_x, int player_y, uint8_t j_edge) {
    if (!j_edge || f->state != FLOW_PLAY || f->altar_triggered ||
        !game_flow_near_altar(player_x, player_y)) return 0;
    f->state = FLOW_ALTAR_START;
    f->state_start_ms = now_ms;
    f->altar_triggered = 1;
    f->logic_locked = 1;
    f->world_frozen = 1;
    f->esc_pending = 0;
    return 1;
}

static void begin_ending(GameFlow *f, uint32_t now_ms, FlowEndReason reason) {
    if (f->state >= FLOW_ENDING_FADE) return;
    f->state = FLOW_ENDING_FADE;
    f->state_start_ms = now_ms;
    f->reason = reason;
    f->logic_locked = 1;
    f->esc_pending = 0;
    f->fade_q5 = 0;
    f->world_frozen = 0; /* request only: commit after newest submitted frame consumes */
}

void game_flow_update(GameFlow *f, uint32_t now_ms, uint8_t key_edges,
                      Player *player, const RendererSnapshot *latest_safe) {
    (void)player;
    if(f->rules.status==GAME_RULES_VICTORY){f->logic_locked=1;return;}
    if(f->rules.status==GAME_RULES_LOST)begin_ending(f,now_ms,FLOW_END_HP_ZERO);
    if (f->state >= FLOW_ENDING_FADE) {
        if (!f->world_frozen) return;
        if (f->state == FLOW_ENDING_FADE &&
            elapsed(now_ms, f->state_start_ms, END_FADE_MS)) {
            f->state = FLOW_ENDING_DEATH;
            f->state_start_ms = now_ms;
            f->death_frame = 0;
        } else if (f->state == FLOW_ENDING_DEATH &&
                   elapsed(now_ms, f->state_start_ms, DEATH_TOTAL_MS)) {
            f->state = FLOW_ENDING_HOLD;
            f->state_start_ms = now_ms;
            f->death_frame = (uint8_t)(DEATH_POSE_COUNT - 1u);
        } else if (f->state == FLOW_ENDING_HOLD &&
                   elapsed(now_ms, f->state_start_ms, DEATH_HOLD_MS)) {
            f->state = FLOW_ENDING_GAMEOVER;
            f->state_start_ms = now_ms;
        }
        return;
    }

    /* ESC is intentionally handled before start-flow timing. A second edge
     * within 280 ms wins even while ALTAR_START is busy. The actual freeze is
     * committed separately, after the renderer reports safe completion. */
    if (key_edges & KEY_ESC) {
        if (f->esc_pending && (uint32_t)(now_ms - f->esc_first_ms) <= ESC_DOUBLE_MS) {
            f->esc_pending = 0;
            begin_ending(f, now_ms, FLOW_END_DOUBLE_ESC);
            if (latest_safe) f->frozen_snapshot = *latest_safe;
            return;
        }
        f->esc_pending = 1;
        f->esc_first_ms = now_ms;
    }

    /* Legacy direct-flow callers can still arm double ESC; the main owner
     * routes real-clock singles to PauseControl and never resets the player. */
    if (f->esc_pending && elapsed(now_ms, f->esc_first_ms, ESC_DOUBLE_MS + 1u)) {
        f->esc_pending = 0;
        /* main pause controller owns deferred single ESC; no legacy reset. */
    }

    if (f->state == FLOW_ALTAR_START &&
        elapsed(now_ms, f->state_start_ms, ALTAR_TOTAL_MS)) {
        f->state = FLOW_PLAY;
        f->state_start_ms = now_ms;
        newwave_arm(&f->wave); /* no world start until the next idle safe point */
        f->logic_locked = 0;
        f->world_frozen = 0;
    }
}

int game_flow_commit_safe_frame(GameFlow *f, uint32_t now_ms, const RendererSnapshot *safe) {
    if (!safe || f->state < FLOW_ENDING_FADE || f->world_frozen) return 0;
    f->frozen_snapshot = *safe;
    f->world_frozen = 1;
    f->state_start_ms = now_ms;
    return 1;
}

int game_flow_altar_frame(const GameFlow *f, uint32_t now_ms) {
    uint32_t elapsed_ms;
    if (f->state != FLOW_ALTAR_START) return -1;
    elapsed_ms = now_ms - f->state_start_ms;
    if (elapsed_ms >= ALTAR_TOTAL_MS) return -1;
    if (elapsed_ms < ALTAR_POSE_MS * ALTAR_POSE_COUNT)
        return (int)(elapsed_ms / ALTAR_POSE_MS);
    return (int)(ALTAR_POSE_COUNT - 1u);
}

int game_flow_death_frame(const GameFlow *f, uint32_t now_ms) {
    uint32_t elapsed_ms;
    if (f->state != FLOW_ENDING_DEATH && f->state != FLOW_ENDING_HOLD &&
        f->state != FLOW_ENDING_GAMEOVER) return -1;
    if (f->state == FLOW_ENDING_HOLD || f->state == FLOW_ENDING_GAMEOVER)
        return (int)(DEATH_POSE_COUNT - 1u);
    elapsed_ms = now_ms - f->state_start_ms;
    if (elapsed_ms < DEATH_TOTAL_MS)
        return (int)(elapsed_ms / DEATH_POSE_MS);
    return (int)(DEATH_POSE_COUNT - 1u);
}

uint8_t game_flow_fade_q5(const GameFlow *f, uint32_t now_ms) {
    uint32_t elapsed_ms;
    uint32_t q;
    if (f->state != FLOW_ENDING_FADE) return 0;
    elapsed_ms = now_ms - f->state_start_ms;
    if (elapsed_ms >= END_FADE_MS) return 20;
    q = (elapsed_ms * 20u) / END_FADE_MS;
    return (uint8_t)(q > 20u ? 20u : q);
}
