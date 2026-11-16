#ifndef PROJECTILE_H
#define PROJECTILE_H
#include "player.h"
#include "arrow_data.h"
#define PROJECTILE_CAPACITY 96
#define PROJECTILE_GLOBAL_CAPACITY 750
/* Absolute birth position/time avoids integration drift and coarse update loss. */
typedef struct {int32_t xq,yq; uint32_t sprite;} ArrowPose;
typedef struct {ArrowPose pose;int32_t origin_xq,origin_yq;uint32_t born_ms,fire_us;const ArrowEvent *event;uint8_t active,hit_count,pending_cull; uint32_t hit_ids[3]; int32_t collision_xq,collision_yq;
/* Magnitude quotient/remainder preserves absolute-time truncation for both
 * velocity signs. Metadata only; pause copies this complete state. */
uint32_t motion_ms,motion_q[2],motion_r[2];uint8_t motion_ready;} Projectile;
typedef struct {Projectile slots[PROJECTILE_CAPACITY];uint32_t version,active,culled,spawned,overflow,reset_seen,k_seen;uint32_t start_ms,next_event,dir;int32_t origin_xq,origin_yq;uint8_t scheduled,budget_set;uint16_t active_budget;} ProjectileSystem;
/* Main owner refreshes before update; budget counts arrows, not free slots. */
void projectile_set_budget(ProjectileSystem *s,unsigned enemy_bullets);
int projectile_init(void);
const uint8_t *projectile_pixels(unsigned sprite);
void projectile_update(ProjectileSystem *s,const Player *p,uint32_t now);
void projectile_clear(ProjectileSystem *s);
unsigned projectile_snapshot(const ProjectileSystem *s,ArrowPose *out);
int projectile_bbox(const ArrowPose *p,int32_t *x,int32_t *y);
#endif
