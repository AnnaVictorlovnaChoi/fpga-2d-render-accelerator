#ifndef WAVE_SCHEDULE_H
#define WAVE_SCHEDULE_H
#include <stdint.h>
#include <stddef.h>
#ifndef EXTERNAL_WAVE
#define EXTERNAL_WAVE 9u
#endif
#define WAVE_STANDARD 0u
#define WAVE_HIGH 1u
#ifndef WAVE_SCHEDULE_DEFAULT_ASSET
#define WAVE_SCHEDULE_DEFAULT_ASSET WAVE_STANDARD
#endif
#define WAVE_SCHEDULE_ARENA_BYTES 473994u /* maximum generated ADWV1 raw: G219 high */
#define WAVE_SCHEDULE_STATE_MAGIC 0x57533218u
#define WAVE_SCHEDULE_NONE 0xffffffffu

typedef enum { WAVE_SCHEDULE_READY=0, WAVE_SCHEDULE_SPAWNING=1,
    WAVE_SCHEDULE_DRAIN=2, WAVE_SCHEDULE_FINISHED=3 } WaveSchedulePhase;
/* slot_id is an opaque commit token; entry_id is the precomputed route entry. */
typedef struct { uint32_t slot_id; uint16_t entry_id; uint8_t kind,side,variant; } WaveSpawnEvent;
typedef struct {
    uint32_t scheduled, considered, spawned, suppressed, expired, failed;
    uint32_t phase, pressure_latched;
} WaveScheduleStats;
/* Pointer-free snapshot. Do not modify fields; save/restore validates all invariants. */
typedef struct {
    uint32_t magic, asset_id, asset_crc, elapsed_ms, last_clock_ms;
    uint32_t last_consumed, last_success_ms, have_success, clock_seen;
    uint32_t pending_slot, pending_ms;
    WaveScheduleStats stats;
} WaveScheduleState;
/* Decode only selected asset; immutable thereafter. Same-id init is O(1).
 * Switching ids is a startup/config operation, not a poll operation. */
int wave_schedule_init(uint32_t asset_id);
/* Reset mutable schedule only; selected decoded asset remains immutable. */
int wave_schedule_reset(void);
int wave_schedule_start(uint32_t now_game_ms);
/* now_game_ms: dedicated UNPAUSED world clock, not simulation time.
 * Call once per changed physical millisecond, not once per simulation substep.
 * Duplicate timestamps never yield a second candidate. pressure is the actual
 * enemy + enemybullet + playerarrow + swordactive count (never clamp it).
 * Each slot is consumed immediately, even on suppression/failure. */
int wave_schedule_poll(uint32_t now_game_ms, uint32_t pressure, WaveSpawnEvent *out);
/* Exactly once, immediately after attempted spawn. Failure consumes the slot,
 * but only success advances the interval gate. Wrong/repeated token rejects. */
int wave_schedule_commit(uint32_t slot_id, int success);
const WaveScheduleStats *wave_schedule_stats(void);
/* FINISHED means observation time ended, not permission to delete objects.
 * Optional early completion only AFTER spawn_end, if live count is zero. */
void wave_schedule_observe_alive(uint32_t alive);
size_t wave_schedule_state_size(void);
int wave_schedule_save(WaveScheduleState *out);
/* Rebases clock to now_game_ms, retaining elapsed and gate age across pause.
 * Snapshot must refer to the already initialized immutable selected asset. */
int wave_schedule_restore(const WaveScheduleState *state, uint32_t now_game_ms);
#endif
