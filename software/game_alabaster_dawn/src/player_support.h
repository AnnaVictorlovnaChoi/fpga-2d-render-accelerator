#ifndef AD_PLAYER_SUPPORT_H
#define AD_PLAYER_SUPPORT_H
#include "runtime.h"
typedef struct { uint16_t x0,x1; } PlayerSpan;
int player_support_init(void);
typedef struct { const uint32_t *row; unsigned height; } PlayerFrameView;
PlayerFrameView player_support_visual(const RuntimeVisual*);
PlayerFrameView player_support_view(const RuntimeGroup*,const RuntimeFrame*);
const PlayerSpan *player_support_view_row(PlayerFrameView,int,unsigned*);
/* Lookup one immutable row, then intersect intervals, never inspect pixels. */
const PlayerSpan *player_support_row(const RuntimeGroup*,const RuntimeFrame*,int y,unsigned *count);
#endif
