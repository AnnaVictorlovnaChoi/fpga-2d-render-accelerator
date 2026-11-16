#include "projectile.h"
#include "external_assets.h"
#if defined(__riscv)
#include "v8_platform.h"
#define BOOT_PUFF(index,rc) bsp_printf("AD BOOT STAGE arrows puff index=%d rc=%d\r\n",(int)(index),(int)(rc))
#else
#define BOOT_PUFF(index,rc) ((void)0)
#endif
static uint8_t arrow_arena[ARROW_RAW_BYTES] __attribute__((aligned(64)));
static int initialized;
typedef struct {uint32_t velocity[2],whole[2],fraction[2],fire_ms,bias_q[2],bias_r[2];uint8_t negative[2];} ArrowMotion;
static ArrowMotion motions[8][15];
static uint8_t motions_ready;
static void motion_init(void){
 if(motions_ready)return;
 for(unsigned d=0;d<8;d++)for(unsigned i=0;i<15;i++){
  const ArrowEvent*e=&arrow_events[d][i];ArrowMotion*m=&motions[d][i];
  m->fire_ms=e->fire_us/1000u+(e->fire_us%1000u!=0);
  uint32_t bias_us=e->fire_us%1000u?1000u-e->fire_us%1000u:0;
  for(unsigned k=0;k<2;k++){
   int32_t v=k?e->vy_q16:e->vx_q16;uint32_t mag=v<0?0u-(uint32_t)v:(uint32_t)v;
   m->negative[k]=(uint8_t)(v<0);m->velocity[k]=mag;m->whole[k]=mag/256000u;m->fraction[k]=mag%256000u;
   uint64_t bias=(uint64_t)mag*bias_us;
   m->bias_q[k]=(uint32_t)(bias/256000000u);m->bias_r[k]=(uint32_t)(bias%256000000u);
  }
 }
 motions_ready=1;
}
/* Common dt<=1000 path has no 64-bit multiply/divide. Write R as 1000*r+tail;
 * tail is invariant because subsequent observations advance whole milliseconds.
 * fraction*dt+r <= 255999*1000+255999 < 2^32. */
static void motion_position(Projectile*a,uint32_t now,int32_t*x,int32_t*y){
 uintptr_t addr=(uintptr_t)a->event,base=(uintptr_t)&arrow_events[0][0];
 uint32_t ms=now-a->born_ms;int32_t *out[2]={x,y};
 if(addr<base||addr-base>=sizeof arrow_events||(addr-base)%sizeof(ArrowEvent)){
  uint64_t age=(uint64_t)ms*1000-a->fire_us;
  *x=a->origin_xq+(int32_t)((int64_t)a->event->vx_q16*(int64_t)age/256000000);
  *y=a->origin_yq+(int32_t)((int64_t)a->event->vy_q16*(int64_t)age/256000000);a->motion_ready=0;return;
 }
 unsigned index=(unsigned)((addr-base)/sizeof(ArrowEvent));const ArrowMotion*m=&motions[index/15][index%15];
 uint32_t dt=ms-a->motion_ms;
 if(!a->motion_ready||ms<a->motion_ms||dt>1000u){
  /* Birth/rare coarse observation: exact absolute magnitude quotient. */
  for(unsigned k=0;k<2;k++){
   uint64_t numerator=(uint64_t)m->velocity[k]*(uint64_t)(ms-m->fire_ms)*1000u+m->bias_r[k];
   a->motion_q[k]=(uint32_t)(numerator/256000000u)+m->bias_q[k];
   a->motion_r[k]=(uint32_t)(numerator%256000000u)/1000u;
  }
  a->motion_ready=1;
 }else for(unsigned k=0;k<2;k++){
  uint32_t r=a->motion_r[k]+m->fraction[k]*dt;
  a->motion_q[k]+=m->whole[k]*dt+r/256000u;a->motion_r[k]=r%256000u;
 }
 a->motion_ms=ms;
 for(unsigned k=0;k<2;k++){
  uint32_t displacement=m->negative[k]?0u-a->motion_q[k]:a->motion_q[k];
  *out[k]=(int32_t)((uint32_t)(k?a->origin_yq:a->origin_xq)+displacement);
 }
}
int projectile_init(void){motion_init();if(initialized)return initialized==1?0:initialized;initialized=external_assets_init();if(initialized)return initialized;initialized=-1;
 for(unsigned i=0;i<104;i++){const ArrowAsset *a=&arrow_assets[i];unsigned long in=a->compressed,out=(unsigned long)a->w*a->h*2;
 const ExternalEntry *entry=external_find(EXTERNAL_ARROW,i);
 if(!entry||entry->codec!=1||entry->predictor!=0||entry->packed!=in||entry->raw!=out)return -1;
 if(a->raw_offset>ARROW_RAW_BYTES || out>ARROW_RAW_BYTES-a->raw_offset)return -1;
 int rc=external_decode(EXTERNAL_ARROW,i,arrow_arena+a->raw_offset,(uint32_t)out);
 if(rc || in!=a->compressed || out!=(unsigned long)a->w*a->h*2){BOOT_PUFF(i,rc);return -1;}
 if(i==103)BOOT_PUFF(i,rc);}
 initialized=1;return 0;}
const uint8_t *projectile_pixels(unsigned i){return initialized==1&&i<104?arrow_arena+arrow_assets[i].raw_offset:0;}
static int32_t rnd(int64_t q){return q<0?-(int32_t)((-q+128)/256):(int32_t)((q+128)/256);}
int projectile_bbox(const ArrowPose*p,int32_t*x,int32_t*y){if(p->sprite>=104)return 0;const ArrowAsset*a=&arrow_assets[p->sprite];*x=rnd((int64_t)p->xq-a->ax);*y=50+rnd((int64_t)p->yq-a->ay);return *x<1280&&*y<680&&*x+a->w>0&&*y+a->h>50;}
void projectile_clear(ProjectileSystem*s){for(unsigned i=0;i<PROJECTILE_CAPACITY;i++)s->slots[i].active=0;s->active=0;s->scheduled=0;s->version++;}
void projectile_set_budget(ProjectileSystem*s,unsigned bullets){unsigned n=bullets>=PROJECTILE_GLOBAL_CAPACITY?0:PROJECTILE_GLOBAL_CAPACITY-bullets;s->active_budget=(uint16_t)(n>PROJECTILE_CAPACITY?PROJECTILE_CAPACITY:n);s->budget_set=1;}
void projectile_update(ProjectileSystem*s,const Player*p,uint32_t now){
 motion_init();
 if(s->reset_seen!=p->reset_events){projectile_clear(s);s->reset_seen=p->reset_events;s->k_seen=p->k_events;}
 /* k_events increases only when player actually starts a locked ranged action. */
 if(s->k_seen!=p->k_events){s->k_seen=p->k_events;s->scheduled=1;s->next_event=0;s->start_ms=p->action_start;s->dir=p->dir;s->origin_xq=p->xq+arrow_origin_q8[p->dir][0];s->origin_yq=p->yq+arrow_origin_q8[p->dir][1];}
 uint64_t elapsed=(uint64_t)(uint32_t)(now-s->start_ms)*1000;
 while(s->scheduled&&s->next_event<15&&elapsed>=arrow_events[s->dir][s->next_event].fire_us){
  const ArrowEvent*e=&arrow_events[s->dir][s->next_event++];unsigned i;for(i=0;i<PROJECTILE_CAPACITY;i++)if(!s->slots[i].active)break;
  if(i==PROJECTILE_CAPACITY||(s->budget_set&&s->active>=s->active_budget)){s->overflow++;continue;} /* Explicit drop-new; never overwrite live arrows. */
  s->slots[i]=(Projectile){.pose={s->origin_xq,s->origin_yq,e->sprite},.origin_xq=s->origin_xq,.origin_yq=s->origin_yq,.born_ms=s->start_ms,.fire_us=e->fire_us,.event=e,.active=1,.collision_xq=s->origin_xq,.collision_yq=s->origin_yq};s->active++;s->spawned++;s->version++;
 }
 if(s->next_event==15)s->scheduled=0;
 for(unsigned i=0;i<PROJECTILE_CAPACITY;i++){Projectile*a=&s->slots[i];if(!a->active)continue;
  int32_t x,y;motion_position(a,now,&x,&y);
  if(x!=a->pose.xq||y!=a->pose.yq){a->pose.xq=x;a->pose.yq=y;s->version++;}
  int32_t bx,by;if(!projectile_bbox(&a->pose,&bx,&by)){/* Keep final swept segment until enemy collision consumes it. */a->pending_cull=1;}
 }
}
unsigned projectile_snapshot(const ProjectileSystem*s,ArrowPose*out){unsigned n=0;for(unsigned i=0;i<PROJECTILE_CAPACITY;i++)if(s->slots[i].active)out[n++]=s->slots[i].pose;return n;}
