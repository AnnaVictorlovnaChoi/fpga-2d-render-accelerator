#include "hud_data.h"
#include "external_assets.h"
#include "../resources/hud/layout.h"
#include <stddef.h>
_Static_assert(sizeof(HudPlane)==16,"HUD LE metadata layout");
_Static_assert(sizeof(HudFrame)==8,"HUD frame metadata layout");
static unsigned char arena[HUD_PAYLOAD_BYTES] __attribute__((aligned(64)));
static const unsigned char *data;
static const HudFrame frames[]=HUD_FRAME_INFO;
uint32_t hud_payload_bound(void){return HUD_PAYLOAD_BYTES;}
int hud_assets_bind(const void *p,uint32_t n){
 const unsigned char*b=p;uint32_t crc=~0u;data=0;
 if(!b||((uintptr_t)b&3u)||n!=HUD_PAYLOAD_BYTES)return -1;
 for(uint32_t i=0;i<n;i++){crc^=b[i];for(unsigned j=0;j<8;j++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}
 if(~crc!=HUD_CRC32)return -2;
 for(unsigned i=0;i<HUD_PLANES;i++){const HudPlane*t=(const HudPlane*)(const void*)b+i;uint32_t bytes=(uint32_t)t->w*t->h*2u;if(!t->w||!t->h||t->w>24||t->h>24||!t->alpha_q5||t->alpha_q5>32||t->reserved[0]||t->reserved[1]||t->reserved[2]||(t->offset&1u)||t->offset>HUD_PAYLOAD_BYTES-HUD_META_BYTES||bytes>HUD_PAYLOAD_BYTES-HUD_META_BYTES-t->offset)return -3;}
 data=b;return 0;
}
int hud_assets_init(void){if(data)return 0;if(external_decode(10,0,arena,sizeof(arena)))return -1;return hud_assets_bind(arena,sizeof(arena));}
int hud_tile(DragonSurface dst,unsigned frame,int x,int y,DragonClip clip){
 if(!data||frame>=sizeof(frames)/sizeof(frames[0]))return -1;
 const HudFrame*f=&frames[frame];
 for(unsigned i=0;i<f->count;i++){
  const HudPlane*p=(const HudPlane*)(const void*)data+f->first+i;
  DragonSurface src={(uint16_t*)(void*)(data+HUD_META_BYTES+p->offset),p->w,x+p->x,y+p->y,p->w,p->h};
  /* Planes partition source support: no transparent pixel or overlapping RMW. */
  int rc=dragon_gpu_copy(dst,src,clip,p->alpha_q5,0);if(rc)return rc;
 }
 return 0;
}
