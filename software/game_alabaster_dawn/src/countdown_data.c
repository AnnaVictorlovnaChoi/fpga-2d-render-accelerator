#include "countdown_data.h"
#include "external_assets.h"
#include "../resources/countdown/layout.h"
#include <stddef.h>
_Static_assert(sizeof(CountdownPlane)==16,"CountdownPlane layout");
_Static_assert(sizeof(CountdownFrame)==32,"CountdownFrame layout");
static union { uint32_t align; unsigned char bytes[COUNTDOWN_PAYLOAD_BYTES]; } arena;
static const unsigned char *data;
static const unsigned counts[]=COUNTDOWN_GROUP_COUNTS;
static const unsigned offsets[]=COUNTDOWN_GROUP_OFFSETS;
static const unsigned sources[]=COUNTDOWN_SOURCE_INDICES;
_Static_assert(sizeof(counts)/sizeof(counts[0])==COUNTDOWN_GROUP_COUNT,"Countdown group counts");
_Static_assert(sizeof(offsets)/sizeof(offsets[0])==COUNTDOWN_GROUP_COUNT,"Countdown group offsets");
_Static_assert(sizeof(sources)/sizeof(sources[0])==COUNTDOWN_FRAME_COUNT,"Countdown source indices");
#define PLANES ((const CountdownPlane *)(const void *)data)
#define FRAMES ((const CountdownFrame *)(const void *)(data+COUNTDOWN_FRAMES_OFFSET))
static uint32_t crc(const unsigned char *p,uint32_t n){uint32_t c=~0u,i;while(n--){c^=*p++;for(i=0;i<8;i++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return ~c;}
void countdown_assets_reset(void){data=NULL;}
int countdown_assets_bind(const void *decoded,uint32_t bytes){
 const unsigned char *b=decoded; const CountdownPlane *p; const CountdownFrame *f; unsigned i,j;
 data=NULL;
 if(!b||((uintptr_t)b&3u)||bytes!=COUNTDOWN_PAYLOAD_BYTES)return -1;
 if(crc(b,COUNTDOWN_META_BYTES)!=COUNTDOWN_META_CRC32)return -2;
 p=(const CountdownPlane *)(const void *)b; f=(const CountdownFrame *)(const void *)(b+COUNTDOWN_FRAMES_OFFSET);
 for(i=0;i<COUNTDOWN_TILES;i++){
  uint32_t n=(uint32_t)p[i].w*p[i].h*2u;
  if(!p[i].w||!p[i].h||p[i].w>24||p[i].h>24||!p[i].alpha_q5||p[i].alpha_q5>32||p[i].reserved[0]||p[i].reserved[1]||p[i].reserved[2]||(p[i].offset&1u)||p[i].offset>COUNTDOWN_RAW_BYTES||n>COUNTDOWN_RAW_BYTES-p[i].offset)return -3;
 }
 for(i=0;i<COUNTDOWN_FRAME_COUNT;i++){
  if(!f[i].w||!f[i].h||f[i].source_index!=sources[i]||f[i].reserved||f[i].span_first>COUNTDOWN_SPANS||f[i].span_count>COUNTDOWN_SPANS-f[i].span_first||f[i].plane_first>COUNTDOWN_TILES||f[i].plane_count>COUNTDOWN_TILES-f[i].plane_first)return -4;
  for(j=0;j<f[i].plane_count;j++){const CountdownPlane *t=&p[f[i].plane_first+j];if((uint32_t)t->x+t->w>f[i].w||(uint32_t)t->y+t->h>f[i].h)return -5;}
  for(j=0;j<f[i].span_count;j++){const CountdownSpan *s=(const CountdownSpan *)(const void *)(b+COUNTDOWN_SPANS_OFFSET)+f[i].span_first+j;if(s->y>=f[i].h||s->x0>=s->x1||s->x1>f[i].w)return -6;}
 }
 data=b;return 0;
}
int countdown_assets_init(void){if(data)return 0;if(external_decode(EXTERNAL_COUNTDOWN,0,arena.bytes,COUNTDOWN_PAYLOAD_BYTES))return -1;return countdown_assets_bind(arena.bytes,COUNTDOWN_PAYLOAD_BYTES);}
unsigned countdown_frame_count(CountdownGroup g){return (unsigned)g<COUNTDOWN_GROUP_COUNT?counts[(unsigned)g]:0;}
const CountdownFrame *countdown_frame(CountdownGroup g,unsigned i){return data&&i<countdown_frame_count(g)?&FRAMES[offsets[(unsigned)g]+i]:NULL;}
static int frame_valid(const CountdownFrame *f){uintptr_t a=(uintptr_t)f,start=(uintptr_t)FRAMES;return a>=start&&a<start+COUNTDOWN_FRAME_COUNT*sizeof(*f)&&(a-start)%sizeof(*f)==0;}
const CountdownPlane *countdown_plane(const CountdownFrame *f,unsigned i){return data&&f&&frame_valid(f)&&i<f->plane_count?&PLANES[f->plane_first+i]:NULL;}
const uint16_t *countdown_pixels(const CountdownPlane *p){uintptr_t a=(uintptr_t)p,start=(uintptr_t)data;return data&&p&&a>=start&&a<start+COUNTDOWN_TILES*sizeof(*p)&&(a-start)%sizeof(*p)==0?(const uint16_t *)(const void *)(data+COUNTDOWN_META_BYTES+p->offset):NULL;}
const CountdownSpan *countdown_support(const CountdownFrame *f,unsigned *count){int ok=data&&f&&frame_valid(f);if(count)*count=ok?f->span_count:0;return ok?(const CountdownSpan *)(const void *)(data+COUNTDOWN_SPANS_OFFSET)+f->span_first:NULL;}
uint32_t countdown_raw_bound(void){return COUNTDOWN_RAW_BYTES;}
uint32_t countdown_payload_bound(void){return COUNTDOWN_PAYLOAD_BYTES;}

const int16_t countdown_map_positions[3][2]={{115,365},{1165,365},{640,540}};
const int16_t countdown_screen_positions[3][2]={{115,415},{1165,415},{640,590}};
const int16_t countdown_heading_degrees[3]={270,90,180};
const int8_t countdown_slot_dxdy[16][2]={{0,-26},{10,-24},{18,-18},{24,-10},{26,0},{24,10},{18,18},{10,24},{0,26},{-10,24},{-18,18},{-24,10},{-26,0},{-24,-10},{-18,-18},{-10,-24}};
const uint8_t countdown_scale_slots[3][15]={{13,14,15,0,1,2,3,4,5,6,7,8,9,10,11},{5,6,7,8,9,10,11,12,13,14,15,0,1,2,3},{9,10,11,12,13,14,15,0,1,2,3,4,5,6,7}};
int countdown_visible(uint32_t ms){return ms<COUNTDOWN_DURATION_MS;}
uint8_t countdown_scale_alpha(uint32_t ms,unsigned i){unsigned past=ms/COUNTDOWN_SEGMENT_MS;if(i>=15||ms>=COUNTDOWN_DURATION_MS||i<past)return 0;if(i>past)return 255;return (uint8_t)(255u-(ms%1000u)*255u/1000u);}
