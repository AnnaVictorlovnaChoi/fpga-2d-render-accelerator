#ifndef AD_G212_NEWWAVE_STATE_H
#define AD_G212_NEWWAVE_STATE_H
#include <stdint.h>
typedef enum { WAVE_WAIT_ALTAR, WAVE_ARMED, WAVE_PREPARATION, WAVE_ACTIVE } WaveState;
typedef struct {
 WaveState state;
 uint32_t began_ms,elapsed_ms;
 uint16_t phase_ms;
 uint8_t remaining;
 uint8_t reserved;
} NewWave;
/* All timestamps are compensated WORLD milliseconds, never physical/flow ms.
 * Arm only after altar completion; begin only at a GPU-idle safe world point. */
void newwave_init(NewWave*);
void newwave_arm(NewWave*);
int newwave_begin_safe(NewWave*,uint32_t world_ms);
int newwave_update(NewWave*,uint32_t world_ms);
int newwave_preparing(const NewWave*);
/* GPU-visible identity: changes only when Q5 fade or surviving count changes. */
typedef struct { uint8_t visible,remaining,alpha_q5,reserved; } WaveVisual;
WaveVisual newwave_visual(const NewWave*);
#endif
