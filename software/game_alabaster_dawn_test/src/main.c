#include <stdint.h>
#include <limits.h>
#include "v8_platform.h"
#include "assets.h"
#include "game.h"
#include "render.h"
#include "log_config.h"
#include "perf_metrics.h"
static Game game;
static volatile uint32_t irq_count;
void frame_pipeline_tick_isr(void){++irq_count;}
static uint64_t ticks(void){return clint_getTime(SYSTEM_CLINT_CTRL);}
static uint32_t micros(uint64_t n){return (uint32_t)(n/(SYSTEM_CLINT_HZ/1000000u));}
#if G24_SERIAL
static int log_int(uint64_t value){return (int)(value>(uint64_t)INT_MAX?(uint64_t)INT_MAX:value);}
#endif
static void fail(const char*where,int rc){
#if G24_SERIAL
 const LastGpuCommand*c=render_last_gpu_command();
 G24_LOG("G24 FAIL %s rc=%d\r\n",where,rc);
 G24_LOG("G24 GPU stage=%x op=%x src=%x dst=%x wh=%x,%x stride=%x,%x opts=%x status=%x fifo=%x rc=%d\r\n",(unsigned)c->stage,(unsigned)c->op,(unsigned)c->src,(unsigned)c->dst,(unsigned)c->w,(unsigned)c->h,(unsigned)c->src_stride,(unsigned)c->dst_stride,(unsigned)c->color,(unsigned)c->status,(unsigned)c->fifo,c->rc);
#else
 (void)where;(void)rc;
#endif
 for(;;)__asm__ volatile("wfi");
}
int main(void){
 bsp_init();
 G24_LOG("G24 BOOT player_idle70 HP30 stamina8 CPU_LIMIT_US=2000 board_verified=0\r\n");
 G24_LOG("G24 CLOCK clint_hz=%d budget_ticks=%d\r\n",log_int(SYSTEM_CLINT_HZ),log_int((SYSTEM_CLINT_HZ/1000u)*2u));
 int rc=assets_init();if(rc)fail("assets",rc);
 __asm__ volatile("fence rw,rw\n\t.word 0x0000500f\n\tfence rw,rw" ::: "memory");
 game_init(&game);if((rc=frame_pipeline_init(0)))fail("pipeline",rc);
 if((rc=render_init()))fail("render init",rc);
 uint64_t previous=ticks(),report_at=previous,carry_ticks=0,rate_begin=previous;
 uint32_t frames=0,maxwork=0,maxactive=0,over=0,maxwall=0,skipped=0,dropped_ms=0;
 uint64_t sumwait=0,sumactive=0,draw_sum=0;
 uint32_t draw_frames=0,consume_base=frame_pipeline_consumed_seq(),generation=game.restart_generation;
 uint64_t submit_seq=0,submit_base=0;
 uint32_t pending_consumed=consume_base;
 int pending=0;
 uint32_t gpu_rate=0,display_raw=0,consumer_rate=0;
 for(;;){
  uint64_t begin=ticks(),elapsed=begin-previous;previous=begin;carry_ticks+=elapsed;
  uint32_t dt=(uint32_t)(carry_ticks/(SYSTEM_CLINT_HZ/1000u));carry_ticks%=SYSTEM_CLINT_HZ/1000u;
  if(dt>50u){dropped_ms+=dt-50u;dt=50u;++skipped;}
  uint64_t wait_before=gpu_wait_ticks_total();
  game_step(&game,dt,(uint8_t)gpio_getInput(GPIO0));
  if(generation!=game.restart_generation){
   generation=game.restart_generation;draw_sum=0;draw_frames=0;rate_begin=ticks();consume_base=frame_pipeline_consumed_seq();submit_base=submit_seq;gpu_rate=display_raw=consumer_rate=0;render_set_rates(0,0);
   /* Keep ownership/pending intact: one old-generation frame may still display.
    * No asset reload/cache reset and no accumulated real-time catch-up. */
   carry_ticks=0;
  }
  int fb=frame_pipeline_acquire(0);if(fb<1)fail("no free framebuffer",fb);
  uint64_t draw_begin=ticks();
  rc=render_frame((unsigned)fb,&game);if(rc)fail("draw",rc);
  /* render_frame returns only after its final true GPU completion fence. */
  uint64_t draw_elapsed=ticks()-draw_begin;draw_sum+=draw_elapsed;++draw_frames;
  uint64_t render_wall=ticks()-begin,wait=gpu_wait_ticks_total()-wait_before;
  uint64_t work=render_wall>=wait?render_wall-wait:render_wall;
  /* Render the third free FB while the previous request is pending. Only a
   * completed prepared frame applies backpressure; never overwrite the slot. */
  uint64_t consume_begin=ticks();
  while(pending && frame_pipeline_consumed_seq()==pending_consumed){frame_pipeline_poll();if(ticks()-consume_begin>(uint64_t)SYSTEM_CLINT_HZ*2u)fail("consume timeout",-1);}
  pending=0;
  pending_consumed=frame_pipeline_consumed_seq();
  if((rc=frame_pipeline_submit((u8)fb)))fail("submit",rc);
  pending=1;++submit_seq;
  game_rendered(&game);
  /* No post-submit wait: the next iteration can render immediately. */
  uint64_t now=ticks(),active=now-begin;
  ++frames;sumwait+=wait;sumactive+=active;
  if(micros(work)>maxwork)maxwork=micros(work);
  if(micros(render_wall)>maxwall)maxwall=micros(render_wall);
  if(micros(active)>maxactive)maxactive=micros(active);
  if(active>(uint64_t)(SYSTEM_CLINT_HZ/1000u)*2u)++over;
  if(now-rate_begin>=(uint64_t)SYSTEM_CLINT_HZ){
   gpu_rate=perf_rate_x10(draw_frames,draw_sum,SYSTEM_CLINT_HZ);
   display_raw=perf_rate_x10((uint32_t)(submit_seq-submit_base),now-rate_begin,SYSTEM_CLINT_HZ);
   consumer_rate=perf_rate_x10(frame_pipeline_consumed_seq()-consume_base,now-rate_begin,SYSTEM_CLINT_HZ);
   render_set_rates(gpu_rate,perf_display_estimate_x10(display_raw));
   draw_sum=0;draw_frames=0;rate_begin=now;submit_base=submit_seq;consume_base=frame_pipeline_consumed_seq();
  }
  if(now-report_at>=(uint64_t)SYSTEM_CLINT_HZ*5u){
   G24_LOG("G24 BUDGET frames=%d strict_active_avg_us=%d strict_active_max_us=%d cpu_work_max_us=%d render_wall_max_us=%d gpu_poll_avg_us=%d exceed_2000=%d bullets=%d effects=%d commands=%d dropped_dt=%d state=%d irq=%d board_verified=0\r\n",log_int(frames),log_int(micros(sumactive/(frames?frames:1))),log_int(maxactive),log_int(maxwork),log_int(maxwall),log_int(micros(sumwait/(frames?frames:1))),log_int(over),log_int(game.bullet_count),log_int(game.effect_count),log_int(render_commands()),log_int(skipped),log_int(game.state),log_int(irq_count));
#if G24_SERIAL
   {const RenderCacheStats*c=render_cache_stats();G24_LOG("G24 CACHE fill=%d replay=%d ddr_fallback=%d timeout=%d resident=%x\r\n",log_int(c->fill),log_int(c->use),log_int(c->fallback),log_int(c->timeout),(unsigned)c->resident);}
   {const RenderStats*s=render_stats();G24_LOG("G24 RATE gpu_x10=%d present_raw_x10=%d display_est_x10=%d submit_raw_x10=%d consumer_x10=%d sprites=%d alpha_objects=%d alpha_commands=%d sim_ms=%d dropped_ms=%d restart=%d\r\n",log_int(gpu_rate),log_int(display_raw),log_int(perf_display_estimate_x10(display_raw)),log_int(display_raw),log_int(consumer_rate),log_int(s->sprite_count),log_int(s->alpha_objects),log_int(s->alpha_commands),log_int(game.game_ms),log_int(dropped_ms),log_int(generation));}
#endif
   report_at=ticks();frames=maxwork=maxactive=over=maxwall=0;sumwait=sumactive=0;
  }
 }
}
