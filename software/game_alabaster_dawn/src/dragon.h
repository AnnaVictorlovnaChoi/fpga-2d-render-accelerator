#ifndef AD_DRAGON_H
#define AD_DRAGON_H
#include <stdint.h>
enum { DRAGON_IDLE, DRAGON_MIN_HOLD, DRAGON_PREP, DRAGON_H_HOLD,
 DRAGON_H_GROW, DRAGON_H_TRAVEL, DRAGON_H_FADE, DRAGON_V_HOLD,
 DRAGON_V_GROW, DRAGON_V_TRAVEL, DRAGON_V_FADE, DRAGON_POST, DRAGON_TAIL };
enum { DRAGON_BEAM_NONE=-1, DRAGON_BEAM_GROW=0, DRAGON_BEAM_LOOP=1 };
#define DRAGON_DURATION_MS 8242u
/* Pose indices are compressed group-local: BODY/HALO/GROW 0..4, LOOP 0..5.
 * Spatial coordinates remain resolved from every millisecond, independently. */
typedef struct {
 int32_t beam_x_q8,beam_y_q8;
 int32_t body_frame,halo_frame,beam_kind,beam_frame;
 uint32_t stage,elapsed_ms,active;
} DragonPose;
typedef struct {
 DragonPose pose;
 uint32_t started_ms,seen_e_events,triggers,rejected,version;
} Dragon;
void dragon_init(Dragon*,uint32_t e_events);
/* Consume counter changes even during player actions. Busy edges are discarded;
 * an edge observed on/after completion starts a fresh action, never a queue. */
void dragon_update(Dragon*,uint32_t now_ms,uint32_t e_events);
int dragon_pose_changed(const DragonPose*,const DragonPose*);
#endif
