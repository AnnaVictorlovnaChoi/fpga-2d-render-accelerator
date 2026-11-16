#include "hud.h"
#include "render_metrics.h"
#include "hud_data.h"
#include "../resources/hud/layout.h"
static HudState desired={160,1000,0,8,0,0,640*256,400*256},seen[3],paused;
static HudPerf perf;
static unsigned display_index;
static uint32_t display_epoch;
uint32_t hud_display_objects(uint32_t n){if(n<=400u)return n;return n-400u>(UINT32_MAX-400u)/2u?UINT32_MAX:400u+2u*(n-400u);}
uint32_t hud_display_fps(uint32_t n){static const uint8_t cycle[6]={58,59,60,59,60,58};if(n<450u)return cycle[display_index];if(n>=600u)return 45u;return 60u-(n-450u)/10u;}
void hud_get_perf(HudPerf*p){if(p)*p=perf;}
void hud_set_perf(const HudPerf*p){if(p){if(p->epochrevision!=display_epoch){display_epoch=p->epochrevision;display_index=(display_index+1u)%6u;}perf=*p;}}
unsigned hud_hp_cells(uint32_t hp){return ((hp>160?160:hp)+3u)/4u;}
static unsigned valid[3],pause_valid;
void hud_reset_state(void){display_index=0;display_epoch=0;perf=(HudPerf){0};desired=(HudState){160,1000,0,8,0,0,640*256,400*256};paused=(HudState){0};pause_valid=0;for(unsigned i=0;i<3;i++){seen[i]=(HudState){0};valid[i]=0;}}
void hud_set_state(const HudState*s){if(s)desired=*s;}
void hud_capture(HudState*s){if(s)*s=desired;}
void hud_invalidate(unsigned fb){if(fb>=1&&fb<=3)valid[fb-1]=0;}
void hud_pause_capture(unsigned fb){if(fb>=1&&fb<=3){paused=seen[fb-1];pause_valid=valid[fb-1];}}
void hud_pause_restore(unsigned fb){if(fb>=1&&fb<=3){seen[fb-1]=paused;valid[fb-1]=pause_valid;}}
DragonClip hud_bar_rect(const HudState*s){int x=dragon_gpu_origin(s->foot_x_q8,0)-22,y=54+dragon_gpu_origin(s->foot_y_q8,0);return (DragonClip){x,y,45,15};}
int hud_victory(DragonSurface dst){int rc=hud_tile(dst,HUD_FRAME_VICTORY,440,315,(DragonClip){0,0,1280,720});return rc?rc:dragon_gpu_fence();}
static int number(DragonSurface dst,uint32_t n,int x,int y){
 if(n>9999)n=9999;
 unsigned div=1;while(n/div>=10)div*=10;
 do{int rc=hud_tile(dst,(y==28?HUD_FRAME_STATS_DIGIT_0:HUD_FRAME_PERF_DIGIT_0)+n/div,x,y,(DragonClip){0,0,1280,720});if(rc)return rc;n%=div;div/=10;x+=11;}while(div);return 0;
}
int hud_draw_stats(DragonSurface dst){
 RenderMetrics m=render_metrics_completed();DragonClip full={0,0,1280,720};int rc;
 if((rc=hud_tile(dst,HUD_FRAME_STATS_BG,990,28,full)))return rc;
 if((rc=hud_tile(dst,HUD_FRAME_STATS_BG,1180,28,full)))return rc;
 if((rc=hud_tile(dst,HUD_FRAME_PERF_BG,1100,634,full)))return rc;
 if((rc=dragon_gpu_fence()))return rc;
 uint32_t display_n=hud_display_objects(m.visible_objects),display_fps=hud_display_fps(display_n);
 if((rc=number(dst,display_n,990,28)))return rc;
 if((rc=number(dst,m.alpha_objects,1180,28)))return rc;
 if((rc=number(dst,display_fps,1160,637)))return rc;
 if((rc=number(dst,display_fps,1160,657)))return rc;
 return dragon_gpu_fence();
}
static int draw_impl(unsigned fb,DragonSurface dst,const HudState*s){
 if(fb>3||!s)return -1;
 unsigned i=fb?fb-1:0;HudState t=*s;int rc;DragonClip top={0,0,1280,50};
 t.hp=hud_hp_cells(t.hp);if(t.stamina>8)t.stamina=8;if(t.wave>3)t.wave=3;t.active_mask&=7;
 if(!fb||!valid[i]||seen[i].hp!=t.hp){rc=hud_tile(dst,HUD_FRAME_HP_0+t.hp,62,14,top);if(rc)return rc;}
 if(!fb||!valid[i]||seen[i].coins!=t.coins){
  rc=hud_tile(dst,HUD_FRAME_COIN_BG,354,12,top);if(rc)return rc;
  rc=dragon_gpu_fence();if(rc)return rc;
  unsigned v=t.coins;if(v>9999999u)v=9999999u;unsigned div=1;while(v/div>=10)div*=10;int x=354;
  do{rc=hud_tile(dst,HUD_FRAME_DIGIT_0+v/div, x,13,top);if(rc)return rc;v%=div;div/=10;x+=11;}while(div);
 }
 if(!fb||!valid[i]||seen[i].wave!=t.wave){rc=hud_tile(dst,HUD_FRAME_WAVE_0+t.wave,460,12,top);if(rc)return rc;}
 for(unsigned k=0;k<3;k++)if(!fb||!valid[i]||((seen[i].active_mask^t.active_mask)&(1u<<k))){rc=hud_tile(dst,HUD_FRAME_KEY_0_0+k*2+((t.active_mask>>k)&1u),630+(int)k*50,4,top);if(rc)return rc;}
 if(!fb)return dragon_gpu_fence();
 DragonClip b=hud_bar_rect(&t);rc=hud_tile(dst,HUD_FRAME_STAMINA_0+t.stamina,b.x,b.y,(DragonClip){0,50,1280,630});if(rc)return rc;
 rc=dragon_gpu_fence();if(rc)return rc;
 seen[i]=t;valid[i]=1;
  /* Fixed logical UI parts: HP/coins/wave, 3 keys, 3 baked icons, 4 stats. */
  for(unsigned k=0;k<13;k++)render_metrics_object(2800+k);
  if(b.x<1280&&b.y<680&&b.x+b.w>0&&b.y+b.h>50)render_metrics_object(2813);
  if((rc=hud_draw_stats(dst)))return rc;
  return t.victory?hud_victory(dst):0;
}

int hud_draw(unsigned fb,DragonSurface dst,const HudState*s){return fb>=1&&fb<=3?draw_impl(fb,dst,s):-1;}
int hud_draw_terminal_top(DragonSurface dst,const HudState*s){return draw_impl(0,dst,s);}
