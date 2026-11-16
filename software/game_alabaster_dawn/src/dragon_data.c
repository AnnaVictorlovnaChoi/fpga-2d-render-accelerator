#include "dragon_data.h"
#include "external_assets.h"
#include "../resources/dragon/layout.h"
#include <stddef.h>
_Static_assert(sizeof(DragonPlane)==16,"tile layout");
_Static_assert(sizeof(DragonFrame)==32,"frame layout");
_Static_assert(sizeof(DragonSpan)==6,"span layout");
static union { uint32_t align; unsigned char bytes[DRAGON_PAYLOAD_BYTES]; } arena;
static const unsigned char *data;
static const unsigned group_counts[]=DRAGON_GROUP_COUNTS;
static const unsigned group_offsets[]=DRAGON_GROUP_OFFSETS;
static const unsigned source_indices[]=DRAGON_SOURCE_INDICES;
#define TILES ((const DragonPlane *)(const void *)data)
#define FRAMES ((const DragonFrame *)(const void *)(data+DRAGON_FRAMES_OFFSET))
#define SPANS ((const DragonSpan *)(const void *)(data+DRAGON_SPANS_OFFSET))
void dragon_assets_reset(void){data=NULL;}
int dragon_assets_bind(const void *decoded,uint32_t bytes){
 const unsigned char *b=(const unsigned char *)decoded; unsigned i,j;
 const DragonPlane *p;const DragonFrame *f;const DragonSpan *s;
 data=NULL;
 if(!b || ((uintptr_t)b&3u) || bytes!=DRAGON_PAYLOAD_BYTES)return -1;
 p=(const DragonPlane *)(const void *)b;
 f=(const DragonFrame *)(const void *)(b+DRAGON_FRAMES_OFFSET);
 s=(const DragonSpan *)(const void *)(b+DRAGON_SPANS_OFFSET);
 for(i=0;i<DRAGON_TILES;i++){
  uint32_t n=(uint32_t)p[i].w*p[i].h*2u;
  if(!p[i].w||!p[i].h||p[i].w>24||p[i].h>24||p[i].alpha_q5<1||p[i].alpha_q5>32||p[i].offset&1u||p[i].offset>DRAGON_RAW_BYTES||n>DRAGON_RAW_BYTES-p[i].offset)return -2;
 }
 for(i=0;i<DRAGON_FRAME_COUNT;i++){
  if(!f[i].w||!f[i].h||f[i].source_index!=source_indices[i]||f[i].plane_first>DRAGON_TILES||f[i].plane_count>DRAGON_TILES-f[i].plane_first||f[i].span_first>DRAGON_SPANS||f[i].span_count>DRAGON_SPANS-f[i].span_first)return -3;
  for(j=0;j<f[i].plane_count;j++){
   const DragonPlane *t=&p[f[i].plane_first+j];
   if((uint32_t)t->x+t->w>f[i].w||(uint32_t)t->y+t->h>f[i].h)return -4;
  }
  for(j=0;j<f[i].span_count;j++){
   const DragonSpan *t=&s[f[i].span_first+j];
   if(t->y>=f[i].h||t->x0>=t->x1||t->x1>f[i].w)return -5;
  }
 }
 data=b;return 0;
}
int dragon_assets_init(void){
 if(data)return 0;
 if(external_decode(EXTERNAL_DRAGON,0,arena.bytes,DRAGON_PAYLOAD_BYTES))return -1;
 return dragon_assets_bind(arena.bytes,DRAGON_PAYLOAD_BYTES);
}
unsigned dragon_frame_count(DragonGroup group){return (unsigned)group<DRAGON_GROUP_COUNT?group_counts[(unsigned)group]:0;}
const DragonFrame *dragon_frame(DragonGroup group,unsigned index){return data&&index<dragon_frame_count(group)?&FRAMES[group_offsets[(unsigned)group]+index]:NULL;}
const DragonPlane *dragon_plane(const DragonFrame *frame,unsigned index){return data&&frame&&index<frame->plane_count?&TILES[frame->plane_first+index]:NULL;}
const uint16_t *dragon_pixels(const DragonPlane *plane){return data&&plane?(const uint16_t *)(const void *)(data+DRAGON_META_BYTES+plane->offset):NULL;}
const DragonSpan *dragon_support(const DragonFrame *frame,unsigned *count){if(count)*count=data&&frame?frame->span_count:0;return data&&frame?&SPANS[frame->span_first]:NULL;}
uint32_t dragon_raw_bound(void){return DRAGON_RAW_BYTES;}
uint32_t dragon_payload_bound(void){return DRAGON_PAYLOAD_BYTES;}
