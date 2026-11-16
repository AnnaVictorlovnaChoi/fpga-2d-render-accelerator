#include "flow_data.h"
#include "external_assets.h"
#include "../resources/flow/layout.h"
#include <stddef.h>
_Static_assert(sizeof(FlowPlane)==16,"FlowPlane layout");
_Static_assert(sizeof(FlowFrame)==32,"FlowFrame layout");
static union { uint32_t align; unsigned char bytes[FLOW_PAYLOAD_BYTES]; } arena;
static const unsigned char *data;
static const unsigned counts[]=FLOW_GROUP_COUNTS;
static const unsigned offsets[]=FLOW_GROUP_OFFSETS;
static const unsigned sources[]=FLOW_SOURCE_INDICES;
_Static_assert(sizeof(counts)/sizeof(counts[0])==FLOW_GROUP_COUNT,"Flow group counts");
_Static_assert(sizeof(offsets)/sizeof(offsets[0])==FLOW_GROUP_COUNT,"Flow group offsets");
_Static_assert(sizeof(sources)/sizeof(sources[0])==FLOW_FRAME_COUNT,"Flow source indices");
#define PLANES ((const FlowPlane *)(const void *)data)
#define FRAMES ((const FlowFrame *)(const void *)(data+FLOW_FRAMES_OFFSET))
static uint32_t crc(const unsigned char *p,uint32_t n){uint32_t c=~0u,i;while(n--){c^=*p++;for(i=0;i<8;i++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return ~c;}
void flow_assets_reset(void){data=NULL;}
int flow_assets_bind(const void *decoded,uint32_t bytes){
 const unsigned char *b=decoded; const FlowPlane *p; const FlowFrame *f; unsigned i,j;
 data=NULL;
 if(!b||((uintptr_t)b&3u)||bytes!=FLOW_PAYLOAD_BYTES)return -1;
 if(crc(b,FLOW_META_BYTES)!=FLOW_META_CRC32)return -2;
 p=(const FlowPlane *)(const void *)b; f=(const FlowFrame *)(const void *)(b+FLOW_FRAMES_OFFSET);
 for(i=0;i<FLOW_TILES;i++){
  uint32_t n=(uint32_t)p[i].w*p[i].h*2u;
  if(!p[i].w||!p[i].h||p[i].w>24||p[i].h>24||!p[i].alpha_q5||p[i].alpha_q5>32||p[i].reserved[0]||p[i].reserved[1]||p[i].reserved[2]||(p[i].offset&1u)||p[i].offset>FLOW_RAW_BYTES||n>FLOW_RAW_BYTES-p[i].offset)return -3;
 }
 for(i=0;i<FLOW_FRAME_COUNT;i++){
  if(!f[i].w||!f[i].h||f[i].source_index!=sources[i]||f[i].reserved||f[i].span_first>FLOW_SPANS||f[i].span_count>FLOW_SPANS-f[i].span_first||f[i].plane_first>FLOW_TILES||f[i].plane_count>FLOW_TILES-f[i].plane_first)return -4;
  for(j=0;j<f[i].plane_count;j++){const FlowPlane *t=&p[f[i].plane_first+j];if((uint32_t)t->x+t->w>f[i].w||(uint32_t)t->y+t->h>f[i].h)return -5;}
  for(j=0;j<f[i].span_count;j++){const FlowSpan *s=(const FlowSpan *)(const void *)(b+FLOW_SPANS_OFFSET)+f[i].span_first+j;if(s->y>=f[i].h||s->x0>=s->x1||s->x1>f[i].w)return -6;}
 }
 data=b;return 0;
}
int flow_assets_init(void){if(data)return 0;if(external_decode(EXTERNAL_FLOW,0,arena.bytes,FLOW_PAYLOAD_BYTES))return -1;return flow_assets_bind(arena.bytes,FLOW_PAYLOAD_BYTES);}
unsigned flow_frame_count(FlowGroup g){return (unsigned)g<FLOW_GROUP_COUNT?counts[(unsigned)g]:0;}
const FlowFrame *flow_frame(FlowGroup g,unsigned i){return data&&i<flow_frame_count(g)?&FRAMES[offsets[(unsigned)g]+i]:NULL;}
static int frame_valid(const FlowFrame *f){uintptr_t a=(uintptr_t)f,start=(uintptr_t)FRAMES;return a>=start&&a<start+FLOW_FRAME_COUNT*sizeof(*f)&&(a-start)%sizeof(*f)==0;}
const FlowPlane *flow_plane(const FlowFrame *f,unsigned i){return data&&f&&frame_valid(f)&&i<f->plane_count?&PLANES[f->plane_first+i]:NULL;}
const uint16_t *flow_pixels(const FlowPlane *p){uintptr_t a=(uintptr_t)p,start=(uintptr_t)data;return data&&p&&a>=start&&a<start+FLOW_TILES*sizeof(*p)&&(a-start)%sizeof(*p)==0?(const uint16_t *)(const void *)(data+FLOW_META_BYTES+p->offset):NULL;}
const FlowSpan *flow_support(const FlowFrame *f,unsigned *count){int ok=data&&f&&frame_valid(f);if(count)*count=ok?f->span_count:0;return ok?(const FlowSpan *)(const void *)(data+FLOW_SPANS_OFFSET)+f->span_first:NULL;}
uint32_t flow_raw_bound(void){return FLOW_RAW_BYTES;}
uint32_t flow_payload_bound(void){return FLOW_PAYLOAD_BYTES;}
