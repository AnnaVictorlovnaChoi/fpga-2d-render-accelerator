#include "enemy_gpu.h"
#include "v8_platform.h"
#include "render_templates.h"
typedef struct { const EnemyFrame *frame; const EnemyPlane *planes; const uint16_t *pixels[64]; unsigned template_id; } PreparedFrame;
static const EnemyCacheFrame *cache_frames[7];
static const uint16_t *cache_pixels[7];
static PreparedFrame prepared[ENEMY_GROUP_COUNT][3];
static unsigned metadata_ready;
static int prepare_metadata(void){
 if(metadata_ready)return 0;
 for(unsigned i=0;i<7;i++){cache_frames[i]=enemy_cache_frame(i);cache_pixels[i]=enemy_cache_pixels(cache_frames[i]);if(!cache_frames[i]||!cache_pixels[i])return -1;}
 for(unsigned g=0;g<ENEMY_GROUP_COUNT;g++){
  unsigned count=enemy_frame_count((EnemyGroup)g);if(count>3)return -1;
  for(unsigned j=0;j<count;j++){PreparedFrame*d=&prepared[g][j];d->frame=enemy_frame((EnemyGroup)g,j);if(!d->frame||d->frame->plane_count>64)return -1;
   for(unsigned k=0;k<d->frame->plane_count;k++){const EnemyPlane*q=enemy_plane(d->frame,k);if(!q||!q->w||!q->h||q->w>24||q->h>24||q->alpha_q5>32||(unsigned)q->x+q->w>d->frame->w||(unsigned)q->y+q->h>d->frame->h)return -1;if(!k)d->planes=q;d->pixels[k]=enemy_pixels(q);if(!d->pixels[k])return -1;}
    unsigned id=g==0?j:g+2;d->template_id=render_template_validate(id,d->frame,d->planes)?7:id;
  }
 }
 metadata_ready=1;return 0;
}
static EnemyGPUMetrics metrics;
const EnemyGPUMetrics *enemy_gpu_metrics(void){return &metrics;}
int enemy_gpu_fence(void){metrics.fences++;return dragon_gpu_fence();}
static void guard(unsigned us){uint64_t t=clint_getTime(SYSTEM_CLINT_CTRL);bsp_uDelay(us);gpu_wait_record(t,clint_getTime(SYSTEM_CLINT_CTRL));}
static uint16_t warm_scratch[1024] __attribute__((aligned(64)));
/* User-approved experimental cache32 choice: zero pacing, real fences kept. */
static unsigned resident,whole_guard=0;
void enemy_gpu_set_whole_guard(unsigned us){whole_guard=us;}
void enemy_gpu_reset(void){resident=0;metadata_ready=0;}
unsigned enemy_gpu_resident(void){return resident;}
static int emit(unsigned op,const uint16_t*src,uint16_t*dst,unsigned w,unsigned h,unsigned sp,unsigned dp,unsigned opt,DragonGPUStats*s){
  int rc;frame_pipeline_poll();rc=bitblt(op,(u32)(uintptr_t)src,(u32)(uintptr_t)dst,w,h,sp*2,dp*2,opt);
  while(rc==-2){if(s)s->retries++;int ready=gpu_wait_fifo_ready();if(ready)return ready;frame_pipeline_poll();rc=bitblt(op,(u32)(uintptr_t)src,(u32)(uintptr_t)dst,w,h,sp*2,dp*2,opt);}
 if(!rc){metrics.commands++;metrics.pixels+=w*h;if(s){s->commands++;s->pixels+=w*h;}}return rc;
}
int enemy_gpu_warm(void){
 if(resident==127)return 0;
 if(prepare_metadata())return -1;
 if(enemy_cache_count()!=7)return -1;
 /* Publish immutable decoded DDR source before GPU reads. Same fence primitive
  * as the existing GPU resource loaders; no CPU writes to GPU image targets. */
 __asm__ volatile("" ::: "memory");
#ifdef __riscv
 __asm__ volatile("fence rw,rw" ::: "memory");
#endif
 if(enemy_gpu_fence())return -1;
 for(unsigned i=0;i<7;i++){
  const EnemyCacheFrame*f=cache_frames[i];const uint16_t*p=cache_pixels[i];
  if(!f||!p||f->slot!=i+1||!f->w||!f->h||f->w>32||f->h>32||f->w*f->h>1024)return -1;
  if(resident&(1u<<i))continue;
  int rc=emit(GPU_OPERATION_COPY,p,warm_scratch,f->w,f->h,f->w,f->w,0x80000000u|((i+1)<<28),0);if(rc)return rc;
  /* Each slot is marked only after actual GPU completion, never after delay. */
  if((rc=enemy_gpu_fence()))return rc;resident|=1u<<i;
 }
 return 0;
}
int enemy_gpu_pose(const EnemyPose*p,const EnemyCacheFrame**c,const EnemyFrame**f){
 *c=0;*f=0;if(!p||prepare_metadata())return -1;
 if(p->cache_id){if(p->cache_id>7)return -1;*c=cache_frames[p->cache_id-1];return *c?0:-1;}
 EnemyGroup g;
 switch(p->source_group){case ENEMY_SOURCE_DK_ATTACK:g=ENEMY_DK_ATTACK;break;case ENEMY_SOURCE_DK_WHITE:g=ENEMY_DK_WHITE;break;case ENEMY_SOURCE_DK_RED:g=ENEMY_DK_RED;break;case ENEMY_SOURCE_WOLF_RED:g=ENEMY_WOLF_RED;break;case ENEMY_SOURCE_GARG_RED:g=ENEMY_GARG_RED;break;default:return -1;}
 *f=p->frame<3?prepared[g][p->frame].frame:0;return *f?0:-1;
}
int enemy_gpu_resolve(const EnemyPose*p,EnemyGPUResolved*d){
 if(!d||enemy_gpu_pose(p,&d->cache,&d->frame))return -1;
 const EnemyCacheFrame*c=d->cache;const EnemyFrame*f=d->frame;
 d->bounds=(DragonClip){dragon_gpu_origin(p->xq,c?c->anchor_x_q8:f->anchor_x_q8),50+dragon_gpu_origin(p->yq,c?c->anchor_y_q8:f->anchor_y_q8),c?c->w:f->w,c?c->h:f->h};
 d->template_id=7;d->planes=0;d->pixels=0;d->source=c?cache_pixels[p->cache_id-1]:0;
 if(f){unsigned g=p->source_group==ENEMY_SOURCE_DK_ATTACK?ENEMY_DK_ATTACK:p->source_group==ENEMY_SOURCE_DK_WHITE?ENEMY_DK_WHITE:p->source_group==ENEMY_SOURCE_DK_RED?ENEMY_DK_RED:p->source_group==ENEMY_SOURCE_WOLF_RED?ENEMY_WOLF_RED:ENEMY_GARG_RED;d->planes=prepared[g][p->frame].planes;d->pixels=prepared[g][p->frame].pixels;d->template_id=prepared[g][p->frame].template_id;}
 return 0;
}
DragonClip enemy_gpu_bounds(const EnemyPose*p){EnemyGPUResolved d;return enemy_gpu_resolve(p,&d)?(DragonClip){0}:d.bounds;}
const EnemySpan*enemy_gpu_support(const EnemyPose*p,unsigned*n){const EnemyCacheFrame*c;const EnemyFrame*f;*n=0;if(enemy_gpu_pose(p,&c,&f))return 0;return c?enemy_cache_support(c,n):enemy_support(f,n);}
static DragonClip intersect(DragonClip a,DragonClip b){int r=a.x+a.w,bt=a.y+a.h;if(r>b.x+b.w)r=b.x+b.w;if(bt>b.y+b.h)bt=b.y+b.h;if(a.x<b.x)a.x=b.x;if(a.y<b.y)a.y=b.y;a.w=r>a.x?r-a.x:0;a.h=bt>a.y?bt-a.y:0;return a;}
static int inside(DragonClip b,DragonClip c){return b.x>=c.x&&b.y>=c.y&&(int64_t)b.x+b.w<=(int64_t)c.x+c.w&&(int64_t)b.y+b.h<=(int64_t)c.y+c.h;}
int enemy_gpu_draw_resolved(DragonSurface dst,const EnemyGPUResolved*d,DragonClip clip,unsigned alpha,DragonGPUStats*s){
 if(!alpha||alpha>32)return -1;
 const EnemyCacheFrame*c=d->cache;const EnemyFrame*f=d->frame;
 DragonClip b=d->bounds;
 int full=inside(b,clip)&&inside(b,(DragonClip){dst.x,dst.y,dst.w,dst.h});
 DragonClip r=full?b:intersect(intersect(b,clip),(DragonClip){dst.x,dst.y,dst.w,dst.h});if(!r.w||!r.h)return 0;
 if(c){
  const uint16_t*src=d->source;
  /* No source tag or source offset exists in RTL. Only unchanged origin may
   * replay, preserving original fill pitch even for right/bottom cropping. */
  unsigned slot=(r.x==b.x&&r.y==b.y&&(resident&(1u<<(c->slot-1))))?c->slot:0;
  if(slot){if(whole_guard)guard(whole_guard);return emit(alpha==32?GPU_OPERATION_SPRITE:GPU_OPERATION_ALPHASPRITE,src,dst.pixels+(r.y-dst.y)*dst.stride+r.x-dst.x,r.w,r.h,c->w,dst.stride,(slot<<28)|(alpha==32?0:alpha),s);}
  for(int y=r.y;y<r.y+r.h;y+=24)for(int x=r.x;x<r.x+r.w;x+=24){unsigned w=r.x+r.w-x<24?r.x+r.w-x:24,h=r.y+r.h-y<24?r.y+r.h-y:24;guard(4);int rc=emit(alpha==32?GPU_OPERATION_SPRITE:GPU_OPERATION_ALPHASPRITE,src+(y-b.y)*c->w+x-b.x,dst.pixels+(y-dst.y)*dst.stride+x-dst.x,w,h,c->w,dst.stride,alpha==32?0:alpha,s);if(rc)return rc;}
 }else if(full){
   /* Prepared planes lie inside their frame. Keep the real DDR guard. */
   uint16_t *base=dst.pixels+(b.y-dst.y)*dst.stride+b.x-dst.x;
    unsigned count;const RenderTemplateOp*t=render_template_enemy(d->template_id,alpha,dst.stride,&count);
    if(t){metrics.template_hits++;for(unsigned i=0;i<count;i++){const RenderTemplateOp*q=t+i;guard(4);int rc=emit(q->op,d->pixels[q->plane],base+q->dst_offset,q->w,q->h,q->w,dst.stride,q->opt,s);metrics.template_planes++;if(rc)return rc;}return 0;}
    metrics.template_fallbacks++;
   for(unsigned i=0;i<f->plane_count;i++){
    const EnemyPlane*q=&d->planes[i];
    metrics.plane_alpha_runtime_ops++;unsigned a=(q->alpha_q5*alpha+16u)>>5;if(!a)continue;
    guard(4);int rc=emit(a==32?GPU_OPERATION_SPRITE:GPU_OPERATION_ALPHASPRITE,d->pixels[i],base+q->y*dst.stride+q->x,q->w,q->h,q->w,dst.stride,a==32?0:a,s);if(rc)return rc;
   }
  }else{
  for(unsigned i=0;i<f->plane_count;i++){
   const EnemyPlane*q=&d->planes[i];
   DragonClip z=intersect(r,(DragonClip){b.x+q->x,b.y+q->y,q->w,q->h});if(!z.w||!z.h||!q->alpha_q5)continue;
   const uint16_t*src=d->pixels[i];
   metrics.plane_alpha_runtime_ops++;unsigned a=(q->alpha_q5*alpha+16u)>>5;if(!a)continue;
    guard(4);int rc=emit(a==32?GPU_OPERATION_SPRITE:GPU_OPERATION_ALPHASPRITE,src+(z.y-b.y-q->y)*q->w+z.x-b.x-q->x,dst.pixels+(z.y-dst.y)*dst.stride+z.x-dst.x,z.w,z.h,q->w,dst.stride,a==32?0:a,s);if(rc)return rc;
  }
 }
 return 0;
}

int enemy_gpu_draw(DragonSurface d,const EnemyPose*p,DragonClip c,DragonGPUStats*s){return enemy_gpu_draw_alpha(d,p,c,32,s);}

int enemy_gpu_draw_alpha(DragonSurface dst,const EnemyPose*p,DragonClip clip,unsigned alpha,DragonGPUStats*s){EnemyGPUResolved d;if(enemy_gpu_resolve(p,&d))return -1;return enemy_gpu_draw_resolved(dst,&d,clip,alpha,s);}
