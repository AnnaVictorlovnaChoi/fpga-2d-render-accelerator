#ifndef ARROW_DATA_H
#define ARROW_DATA_H
#include <stdint.h>
typedef struct {uint32_t offset,compressed,raw_offset; int32_t w,h,ax,ay;} ArrowAsset;
typedef struct {uint32_t fire_us,sprite;int32_t vx_q16,vy_q16;} ArrowEvent;
extern const ArrowAsset arrow_assets[104];
extern const ArrowEvent arrow_events[8][15];
extern const int32_t arrow_origin_q8[8][2];
#define ARROW_RAW_BYTES 40960
#endif
