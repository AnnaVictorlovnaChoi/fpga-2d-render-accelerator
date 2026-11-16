#include "dragon_gpu.h"
#include "v8_platform.h"
static DragonPhaseStats perf[DRAGON_PERF_PHASES];
uint64_t dragon_gpu_clock(void){
#if defined(__riscv)
 return clint_getTime(SYSTEM_CLINT_CTRL)/(SYSTEM_CLINT_HZ/1000000u);
#else
 return 0;
#endif
}
DragonPhaseStats *dragon_gpu_perf(unsigned phase){return phase<DRAGON_PERF_PHASES?&perf[phase]:0;}
void dragon_gpu_record(unsigned phase,uint64_t begin,const DragonGPUStats*s){if(phase>=DRAGON_PERF_PHASES)return;DragonPhaseStats*p=&perf[phase];uint32_t us=(uint32_t)(dragon_gpu_clock()-begin);p->samples++;p->total_us+=us;if(us>p->max_us)p->max_us=us;if(s){p->commands+=s->commands;p->pixels+=s->pixels;}}
int dragon_gpu_origin(int center,int anchor){int64_t q=(int64_t)center-anchor;return (int)(q<0?-((-q+128)/256):(q+128)/256);}
static DragonClip intersect(DragonClip a,DragonClip b){int x=a.x>b.x?a.x:b.x,y=a.y>b.y?a.y:b.y,r=a.x+a.w<b.x+b.w?a.x+a.w:b.x+b.w,d=a.y+a.h<b.y+b.h?a.y+a.h:b.y+b.h;return (DragonClip){x,y,r>x?r-x:0,d>y?d-y:0};}
int dragon_gpu_fence(void){uint64_t begin=clint_getTime(SYSTEM_CLINT_CTRL);int rc;for(;;){u32 s=read_u32(GPU_BASE);frame_pipeline_poll();if(s&GPU_STATUS_ERROR){rc=-1;break;}if((s&GPU_STATUS_DONE)&&!(s&GPU_STATUS_BUSY)&&(s&GPU_STATUS_EMPTY)){rc=0;break;}if(clint_getTime(SYSTEM_CLINT_CTRL)-begin>=(uint64_t)SYSTEM_CLINT_HZ*2u){rc=-1;break;}}gpu_wait_record(begin,clint_getTime(SYSTEM_CLINT_CTRL));return rc;}
static int enqueue(unsigned op,const uint16_t*src,unsigned ss,uint16_t*dst,unsigned ds,int w,int h,unsigned alpha,DragonGPUStats*s){int rc;for(;;){frame_pipeline_poll();rc=bitblt(op,(u32)(uintptr_t)src,(u32)(uintptr_t)dst,(unsigned)w,(unsigned)h,ss*2,ds*2,alpha);if(rc!=-2)break;if(s)++s->retries;rc=gpu_wait_fifo_ready();if(rc)return rc;}if(!rc&&s){++s->commands;s->pixels+=(uint32_t)w*(uint32_t)h;}return rc;}
int dragon_gpu_copy(DragonSurface dst,DragonSurface src,DragonClip r,unsigned alpha,DragonGPUStats*s){
 if(!dst.pixels||!src.pixels||!alpha||alpha>32)return -1;
 r=intersect(r,(DragonClip){dst.x,dst.y,dst.w,dst.h});r=intersect(r,(DragonClip){src.x,src.y,src.w,src.h});if(!r.w||!r.h)return 0;
 if(alpha==32)return enqueue(GPU_OPERATION_COPY,src.pixels+(r.y-src.y)*src.stride+r.x-src.x,(unsigned)src.stride,dst.pixels+(r.y-dst.y)*dst.stride+r.x-dst.x,(unsigned)dst.stride,r.w,r.h,0,s);
 for(int yy=r.y;yy<r.y+r.h;yy+=24)for(int xx=r.x;xx<r.x+r.w;xx+=24){int w=r.x+r.w-xx,h=r.y+r.h-yy;if(w>24)w=24;if(h>24)h=24;bsp_uDelay(4);int rc=enqueue(GPU_OPERATION_ALPHA,src.pixels+(yy-src.y)*src.stride+xx-src.x,(unsigned)src.stride,dst.pixels+(yy-dst.y)*dst.stride+xx-dst.x,(unsigned)dst.stride,w,h,alpha,s);if(rc)return rc;}
 return 0;
}
int dragon_gpu_draw(DragonSurface dst,const DragonFrame*f,int cx,int cy,DragonClip clip,DragonGPUStats*s){
 if(!f||!dst.pixels)return -1;
 int x=dragon_gpu_origin(cx,f->anchor_x_q8),y=dragon_gpu_origin(cy,f->anchor_y_q8);unsigned previous_alpha=0;
 clip=intersect(clip,(DragonClip){dst.x,dst.y,dst.w,dst.h});
 for(unsigned i=0;i<f->plane_count;i++){
  const DragonPlane*p=dragon_plane(f,i);if(!p||!p->alpha_q5||p->alpha_q5>32||!p->w||!p->h||p->w>24||p->h>24)return -1;
  DragonClip r=intersect((DragonClip){x+p->x,y+p->y,p->w,p->h},clip);if(!r.w||!r.h)continue;
  const uint16_t*src=dragon_pixels(p);if(!src)return -1;
  /* Offline same-alpha tiles are disjoint. Cross-layer RMW needs real DONE;
   * never substitute a delay for the fence. Keep only v9 sprite 4us guard. */
  if(previous_alpha&&previous_alpha!=p->alpha_q5){int rc=dragon_gpu_fence();if(rc)return rc;if(s)++s->fences;}
  previous_alpha=p->alpha_q5;bsp_uDelay(4);
  int rc=enqueue(p->alpha_q5==32?GPU_OPERATION_SPRITE:GPU_OPERATION_ALPHASPRITE,src+(r.y-y-p->y)*p->w+r.x-x-p->x,p->w,dst.pixels+(r.y-dst.y)*dst.stride+r.x-dst.x,(unsigned)dst.stride,r.w,r.h,p->alpha_q5==32?0:p->alpha_q5,s);if(rc)return rc;
 }
 if(previous_alpha){int rc=dragon_gpu_fence();if(rc)return rc;if(s)++s->fences;}
 return 0;
}
