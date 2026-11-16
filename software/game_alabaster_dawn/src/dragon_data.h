#ifndef DRAGON_DATA_H
#define DRAGON_DATA_H
#include <stdint.h>
typedef enum { DRAGON_BODY, DRAGON_HALO, DRAGON_GROW, DRAGON_LOOP, DRAGON_GROUP_COUNT } DragonGroup;
/* Plane is one nonempty offline tile, never larger than 24x24. x/y are
 * tight-frame-local. Key 0 is transparent, q32 Sprite, q1..31 AlphaSprite.
 * Q5 alpha is quantized; exact 8-bit source RGBA remains in archive only. */
typedef struct { uint32_t offset; uint16_t w,h,x,y; uint8_t alpha_q5; uint8_t reserved[3]; } DragonPlane;
typedef DragonPlane DragonTile;
typedef struct { uint16_t y,x0,x1; } DragonSpan;
typedef struct {
 uint16_t w,h,source_x,source_y;
 int32_t anchor_x_q8,anchor_y_q8;
 uint32_t plane_first,plane_count,span_first;
 uint16_t span_count; uint8_t source_index,reserved;
} DragonFrame;
/* Loader-owned decompressed buffer must remain live. Bind validates metadata
 * only, never examines pixels. This is the sole commit point (no heap/BSS blob).
 * Call reset before reclaiming storage. init uses external_decode(kind4,id0)
 * into one aligned arena; loader performs sole puff/CRC, no pixel conversion. */
int dragon_assets_bind(const void *decoded,uint32_t bytes);
void dragon_assets_reset(void);
int dragon_assets_init(void);
const DragonFrame *dragon_frame(DragonGroup group,unsigned index);
unsigned dragon_frame_count(DragonGroup group);
const DragonPlane *dragon_plane(const DragonFrame *frame,unsigned index);
const uint16_t *dragon_pixels(const DragonPlane *plane);
const DragonSpan *dragon_support(const DragonFrame *frame,unsigned *count);
uint32_t dragon_raw_bound(void);
uint32_t dragon_payload_bound(void);
#endif
