#ifndef ENEMY_DATA_H
#define ENEMY_DATA_H
#include <stdint.h>
typedef enum { ENEMY_DK_ATTACK, ENEMY_DK_WHITE, ENEMY_DK_RED, ENEMY_WOLF_RED, ENEMY_GARG_RED, ENEMY_GROUP_COUNT } EnemyGroup;
/* All offsets byte-based within decoded container. Cache is a complete contiguous
 * w*h RGB565 image, stride=w, key0 transparent, opaque black remapped to1.
 * Slots1..7: DK0/1 WOLF0/1 GARG0/1 BULLET; fixed facing, never mirror. */
typedef struct { uint16_t w,h; int32_t anchor_x_q8,anchor_y_q8; uint32_t dataoff,bytes,span_first,rowoff; uint8_t slot,alpha_q5; uint16_t reserved; } EnemyCacheFrame;
typedef struct { uint32_t offset; uint16_t w,h,x,y; uint8_t alpha_q5,reserved[3]; } EnemyPlane;
typedef struct { uint16_t w,h,source_x,source_y; int32_t anchor_x_q8,anchor_y_q8; uint32_t plane_first,plane_count,span_first; uint16_t span_count; uint8_t source_index,reserved; } EnemyFrame;
typedef struct { uint16_t y,x0,x1; } EnemySpan;
int enemy_assets_init(void);
int enemy_assets_bind(const void *decoded,uint32_t bytes);
void enemy_assets_reset(void);
unsigned enemy_cache_count(void);
const EnemyCacheFrame *enemy_cache_frame(unsigned index);
const uint16_t *enemy_cache_pixels(const EnemyCacheFrame *frame);
/* Metadata only. row mask bit x means nontransparent source pixel. Runs use
 * half-open [x0,x1). No runtime pixel reads needed for support intersections. */
uint32_t enemy_cache_row_mask(const EnemyCacheFrame *frame,unsigned row);
const EnemySpan *enemy_cache_support(const EnemyCacheFrame *frame,unsigned *count);
const EnemySpan *enemy_cache_support_row(const EnemyCacheFrame *frame,unsigned row,unsigned *count);
const EnemyFrame *enemy_frame(EnemyGroup group,unsigned index);
unsigned enemy_frame_count(EnemyGroup group);
const EnemyPlane *enemy_plane(const EnemyFrame *frame,unsigned index);
const uint16_t *enemy_pixels(const EnemyPlane *plane);
const EnemySpan *enemy_support(const EnemyFrame *frame,unsigned *count);
uint32_t enemy_raw_bound(void);
uint32_t enemy_payload_bound(void);
#endif
