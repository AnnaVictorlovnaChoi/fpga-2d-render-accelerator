/* Generated loader sizes; no incbin and no framebuffer aliases. */
#include "assets.h"
#include "external_assets.h"
#include <stddef.h>
static uint16_t decoded_pixels[8643404] __attribute__((aligned(64)));
static uint8_t decoded_masks[1814400] __attribute__((aligned(64)));
static TrajPoint decoded_traj[40527] __attribute__((aligned(64)));
static EffectPoint decoded_effects[34500] __attribute__((aligned(64)));
uint16_t *asset_pixels=decoded_pixels;
const uint8_t *asset_walk_masks=decoded_masks;
const TrajPoint *asset_traj_points=decoded_traj;
const EffectPoint *asset_effect_points=decoded_effects;
_Static_assert(sizeof(TrajPoint)==8,"TrajPoint wire stride");
_Static_assert(sizeof(EffectPoint)==8,"EffectPoint wire stride");
int assets_init(void){
 static int state; int rc; if(state)return state==1?0:state;
 state=-1; rc=external_assets_init();if(rc){state=rc;return rc;}
 rc=external_decode(3,0,decoded_pixels,sizeof decoded_pixels);if(rc){state=rc;return rc;}
 rc=external_decode(3,1,decoded_masks,sizeof decoded_masks);if(rc){state=rc;return rc;}
 rc=external_decode(3,2,decoded_traj,sizeof decoded_traj);if(rc){state=rc;return rc;}
 rc=external_decode(3,3,decoded_effects,sizeof decoded_effects);if(rc){state=rc;return rc;}
 state=1;return 0;
}
int asset_walkable(unsigned island,int x,int y){
 if(island>=18 || (unsigned)x>=1280u || (unsigned)y>=630u)return 0;
 unsigned bit=(unsigned)y*1280u+(unsigned)x;
 return (asset_walk_masks[asset_islands[island].mask_offset+(bit>>3)]>>(bit&7))&1;
}
const NodeAsset *asset_node(unsigned route,unsigned node){
 return route<2 && node<18?&asset_nodes[route][node]:NULL;
}
