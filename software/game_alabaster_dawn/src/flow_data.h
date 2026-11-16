#ifndef FLOW_DATA_H
#define FLOW_DATA_H
#include <stdint.h>
typedef enum { FLOW_ALTAR, FLOW_DEATH, FLOW_START_HINT, FLOW_GAMEOVER, FLOW_PAUSE=4, FLOW_GROUP_COUNT } FlowGroup;
typedef struct { uint32_t offset; uint16_t w,h,x,y; uint8_t alpha_q5; uint8_t reserved[3]; } FlowPlane;
typedef FlowPlane FlowTile;
typedef struct { uint16_t y,x0,x1; } FlowSpan;
typedef struct {
 uint16_t w,h,source_x,source_y;
 int32_t anchor_x_q8,anchor_y_q8;
 uint32_t plane_first,plane_count,span_first;
 uint16_t span_count; uint8_t source_index,reserved;
} FlowFrame;
/* Raw data is loader-decoded once; pixels are prebuilt direct RGB565 sparse
 * 24x24 tiles. Key 0 transparent, q32 Sprite, q1..31 AlphaSprite. */
int flow_assets_bind(const void *decoded,uint32_t bytes);
void flow_assets_reset(void);
int flow_assets_init(void);
const FlowFrame *flow_frame(FlowGroup group,unsigned index);
unsigned flow_frame_count(FlowGroup group);
const FlowPlane *flow_plane(const FlowFrame *frame,unsigned index);
const uint16_t *flow_pixels(const FlowPlane *plane);
const FlowSpan *flow_support(const FlowFrame *frame,unsigned *count);
uint32_t flow_raw_bound(void);
uint32_t flow_payload_bound(void);
#endif
