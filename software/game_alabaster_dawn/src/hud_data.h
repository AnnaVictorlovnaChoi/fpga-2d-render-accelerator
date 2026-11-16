#ifndef AD_HUD_DATA_H
#define AD_HUD_DATA_H
#include <stdint.h>
#include "dragon_gpu.h"
typedef struct { uint32_t offset; uint16_t x,y,w,h; uint8_t alpha_q5,reserved[3]; } HudPlane;
typedef struct { uint16_t first,count,w,h; } HudFrame;
int hud_assets_init(void);
int hud_assets_bind(const void *,uint32_t);
uint32_t hud_payload_bound(void);
int hud_tile(DragonSurface,unsigned frame,int x,int y,DragonClip);
#endif
