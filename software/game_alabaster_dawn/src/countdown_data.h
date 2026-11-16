#ifndef COUNTDOWN_DATA_H
#define COUNTDOWN_DATA_H
#include <stdint.h>
typedef enum { COUNTDOWN_BASE, COUNTDOWN_SCALE, COUNTDOWN_GROUP_COUNT } CountdownGroup;
typedef enum { COUNTDOWN_LEFT=0, COUNTDOWN_RIGHT=1, COUNTDOWN_BOTTOM=2, COUNTDOWN_INSTANCE_COUNT=3 } CountdownInstance;
typedef struct { uint32_t offset; uint16_t w,h,x,y; uint8_t alpha_q5; uint8_t reserved[3]; } CountdownPlane;
typedef CountdownPlane CountdownTile;
typedef struct { uint16_t y,x0,x1; } CountdownSpan;
typedef struct {
 uint16_t w,h,source_x,source_y;
 int32_t anchor_x_q8,anchor_y_q8;
 uint32_t plane_first,plane_count,span_first;
 uint16_t span_count; uint8_t source_index,reserved;
} CountdownFrame;
#define COUNTDOWN_DURATION_MS 15000u
#define COUNTDOWN_SEGMENT_MS 1000u
#define COUNTDOWN_SCALE_COUNT 15u
#define COUNTDOWN_SLOT_COUNT 16u
#define COUNTDOWN_SCREEN_Y_OFFSET 50
/* Positions denote badge centers, not image centers; frames carry source Q8 anchors. */
extern const int16_t countdown_map_positions[3][2];
extern const int16_t countdown_screen_positions[3][2];
extern const int16_t countdown_heading_degrees[3];
extern const int8_t countdown_slot_dxdy[16][2];
extern const uint8_t countdown_scale_slots[3][15];
int countdown_assets_bind(const void *decoded,uint32_t bytes);
void countdown_assets_reset(void);
int countdown_assets_init(void);
const CountdownFrame *countdown_frame(CountdownGroup group,unsigned index);
unsigned countdown_frame_count(CountdownGroup group);
const CountdownPlane *countdown_plane(const CountdownFrame *frame,unsigned index);
const uint16_t *countdown_pixels(const CountdownPlane *plane);
const CountdownSpan *countdown_support(const CountdownFrame *frame,unsigned *count);
uint32_t countdown_raw_bound(void);
uint32_t countdown_payload_bound(void);
/* elapsed>=15000: all base and scale sprites hidden. No rotate/shrink. */
int countdown_visible(uint32_t elapsed_ms);
uint8_t countdown_scale_alpha(uint32_t elapsed_ms,unsigned sequence_index);
#endif
