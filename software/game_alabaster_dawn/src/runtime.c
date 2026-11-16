#include "runtime.h"
#include "runtime_data.h"
#include "external_assets.h"
#include "../resources/render_tables/render_time_tables.h"
#if defined(__riscv)
#include "v8_platform.h"
#define BOOT_PUFF(stage,index,rc) bsp_printf("AD BOOT STAGE %s puff index=%d rc=%d\r\n",stage,(int)(index),(int)(rc))
#else
#define BOOT_PUFF(stage,index,rc) ((void)0)
#endif
static uint8_t arena[RUNTIME_ARENA_BYTES] __attribute__((aligned(64)));
/* 0=not attempted, 1=ready, negative=permanent failure. Startup is single-core. */
static int init_status;
int runtime_init(void) {
 uint32_t i;
 if(init_status) return init_status==1?0:init_status;
 init_status=external_assets_init();
 if(init_status)return init_status;
 init_status=-1;
 for(i=0;i<38;i++) {
  const RuntimeGroup *g=runtime_groups[i];
  unsigned long in=g->compressed_bytes,out=g->raw_bytes;
  int rc;
  const ExternalEntry *entry=external_find(EXTERNAL_PLAYER,i);
  if(!entry||entry->codec!=1||entry->predictor!=0||entry->packed!=in||entry->raw!=out){init_status=-3;return init_status;}
  if(g->pack_offset>RUNTIME_PACK_BYTES || in>RUNTIME_PACK_BYTES-g->pack_offset || runtime_offsets[i]>RUNTIME_ARENA_BYTES || out>RUNTIME_ARENA_BYTES-runtime_offsets[i]) {init_status=-2;return init_status;}
  rc=external_decode(EXTERNAL_PLAYER,i,arena+runtime_offsets[i],g->raw_bytes);
  if(rc || out!=g->raw_bytes || in!=g->compressed_bytes) {BOOT_PUFF("assets",i,rc);init_status=-(int)(100+i);return init_status;}
  if(i==37)BOOT_PUFF("assets",i,rc);
 }
#if defined(__riscv)
 __asm__ volatile("fence rw,rw" ::: "memory");
#endif
 init_status=1;return 0;
}
uint32_t runtime_group_count(void){return 38;}
const RuntimeGroup *runtime_group_at(uint32_t i){return i<38?runtime_groups[i]:0;}
uint32_t runtime_arena_bytes(void){return RUNTIME_ARENA_BYTES;}
const RuntimeGroup *runtime_get_group(uint32_t a,uint32_t d){
 static const uint32_t cardinal[8]={0,0,1,2,2,2,3,0};
 if(a>RUNTIME_ACT_DODGE || d>7)return 0;
 return runtime_groups[a==RUNTIME_ACT_DODGE?34+cardinal[d]:2+a*8+d];
}
uint32_t runtime_dodge_duration(void){return runtime_dodge_steps[15].end_us;}
const RuntimeDodgeStep *runtime_dodge_step(uint32_t t){
 if(t>=runtime_dodge_steps[15].end_us)return &runtime_dodge_steps[15];
 uint32_t i=render_time_indices[render_time_offsets[32]+(t>>RENDER_TIME_SHIFT)];
 if(t>=runtime_dodge_steps[i].end_us)++i;
 return &runtime_dodge_steps[i];
}
uint32_t runtime_action_duration(uint32_t a,uint32_t d){
 const RuntimeGroup *g=runtime_get_group(a,d);
 if(!g)return 0;
 return a==RUNTIME_ACT_DODGE?runtime_dodge_duration():g->frames[g->count-1].end_us;
}
const RuntimeFrame *runtime_get_frame(uint32_t a,uint32_t d,uint32_t t){
 const RuntimeGroup *g=runtime_get_group(a,d);uint32_t i,duration;
 if(!g)return 0;
 if(a==RUNTIME_ACT_DODGE)return &g->frames[runtime_dodge_step(t)->state];
 if(g->count==1)return g->frames;
 duration=g->frames[g->count-1].end_us;
 if((a==RUNTIME_ACT_IDLE || a==RUNTIME_ACT_WALK) && duration)t%=duration;
 if(t>=duration)return &g->frames[g->count-1];
 i=render_time_indices[render_time_offsets[a*8+d]+(t>>RENDER_TIME_SHIFT)];
 /* Bucket seed is exact at its left edge; generator proves at most one
  * transition per bucket. Compare original microsecond end, never round t. */
 if(t>=g->frames[i].end_us)++i;
 return &g->frames[i];
}
uint32_t runtime_frame_stride(const RuntimeGroup*g,const RuntimeFrame*f){return g&&f?(uint32_t)f->w*g->bytes_per_pixel:0;}
const uint8_t *runtime_pixels(const RuntimeGroup*g,const RuntimeFrame*f){
 uint32_t i,n;if(init_status!=1 || !g || !f)return 0;
 for(i=0;i<38;i++)if(g==runtime_groups[i]){
  uint32_t j;for(j=0;j<g->count;j++)if(f==&g->frames[j])break;
  if(j==g->count)return 0;
  n=runtime_frame_stride(g,f)*(uint32_t)f->h;
  if(f->offset>g->raw_bytes || n>g->raw_bytes-f->offset)return 0;
  return arena+runtime_offsets[i]+f->offset;
 }return 0;
}
const uint16_t *runtime_map(void){return (const uint16_t*)runtime_pixels(runtime_groups[0],runtime_groups[0]->frames);}
const uint8_t *runtime_mask(void){return runtime_pixels(runtime_groups[1],runtime_groups[1]->frames);}
int runtime_walkable(int32_t x,int32_t y){const uint8_t*m=runtime_mask();return m && x>=0 && x<1280 && y>=0 && y<630 ? m[(uint32_t)y*1280+(uint32_t)x]!=0:0;}

static uint32_t visual_queries;
uint32_t runtime_visual_queries(void){return visual_queries;}
int runtime_visual(unsigned i,const RuntimeFrame*f,RuntimeVisual*out){
 if(!out)return -1;
 *out=(RuntimeVisual){0};
 if(init_status!=1||i>=38||!f)return -1;
 const RuntimeGroup*g=runtime_groups[i];uintptr_t a=(uintptr_t)f,b=(uintptr_t)g->frames;
 if(a<b||a-b>=(uintptr_t)g->count*sizeof(*f)||(a-b)%sizeof(*f))return -1;
 uint32_t stride=(uint32_t)f->w*g->bytes_per_pixel,n=stride*f->h;
 if(f->offset>g->raw_bytes||n>g->raw_bytes-f->offset)return -1;
 *out=(RuntimeVisual){g,f,arena+runtime_offsets[i]+f->offset,i,(uint32_t)((a-b)/sizeof(*f)),stride};
 visual_queries++;return 0;
}
int runtime_get_visual(uint32_t a,uint32_t d,uint32_t t,RuntimeVisual*out){
 static const unsigned cardinal[8]={0,0,1,2,2,2,3,0};
 if(a>RUNTIME_ACT_DODGE||d>7)return -1;
 unsigned i=a==RUNTIME_ACT_DODGE?34+cardinal[d]:2+a*8+d;
 return runtime_visual(i,runtime_get_frame(a,d,t),out);
}
