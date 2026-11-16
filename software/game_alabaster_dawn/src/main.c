#include "v8_platform.h"
#include "runtime.h"
#include "renderer.h"
#include "player.h"
#include "visible.h"
#include "scene.h"
#include "dragon.h"
#include "player_support.h"
#include "external_assets.h"
#include "game_flow.h"
#include "pause.h"
#include "enemy.h"
#include "precomputed_data.h"
#include "cpu_probe.h"
#include "cpu_scheduler.h"
#include "hud.h"
#include "run_session.h"
#include "render_metrics.h"
#ifndef AD_RUNTIME_LOG
#define AD_RUNTIME_LOG 1
#endif
static RunRates run_rates;
static uint8_t flow_key_stable,flow_key_candidate;
static uint32_t flow_key_changed[8];
static void flow_keys_reset(void){flow_key_stable=flow_key_candidate=0;for(unsigned i=0;i<8;i++)flow_key_changed[i]=0;}
static uint8_t flow_keys(uint32_t now,uint8_t raw){
 uint8_t stable=flow_key_stable,candidate=flow_key_candidate;uint32_t *changed=flow_key_changed;uint8_t keys=(uint8_t)~raw,edge=0;
 for(unsigned i=0;i<8;i++){uint8_t b=(uint8_t)(1u<<i);if((keys^candidate)&b){candidate=(candidate&~b)|(keys&b);changed[i]=now;}if(((stable^candidate)&b)&&now-changed[i]>=8u){if(candidate&b)edge|=b;stable^=b;}}
 flow_key_stable=stable;flow_key_candidate=candidate;return edge;
}
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* Preserve G2.2 restore -> completion -> draw -> completion -> publish protocol.
 * No scan guard, frame-tail delay or arbitrary completion sleep is introduced. */
static volatile uint32_t vsync_ticks;
void frame_pipeline_tick_isr(void) { ++vsync_ticks; }
static uint64_t ticks(void) { return clint_getTime(SYSTEM_CLINT_CTRL); }
static uint32_t millis(void) { return (uint32_t)(ticks()/(SYSTEM_CLINT_HZ/1000u)); }
static uint32_t elapsed_us(uint64_t start) { return (uint32_t)((ticks()-start)/(SYSTEM_CLINT_HZ/1000000u)); }
/* Sole main-loop UART producer during play. MMIO uses the existing BSP UART,
 * status bits 23:16 are free TX bytes (platform/driver/uart.h). No busy wait. */
#if AD_RUNTIME_LOG
#define LOG_RING_BYTES 8192u
#define LOG_DRAIN_BYTES 64u
static unsigned char log_ring[LOG_RING_BYTES];
static uint32_t log_head,log_tail,log_queued,log_dropped,log_sent;
static uint8_t log_nonblocking;
static void log_drain(void){
 if(!log_nonblocking||!log_queued)return;
 unsigned free_bytes=(read_u32(BSP_UART_TERMINAL+4u)>>16)&255u;
 unsigned n=log_queued;if(n>free_bytes)n=free_bytes;if(n>LOG_DRAIN_BYTES)n=LOG_DRAIN_BYTES;
 for(unsigned i=0;i<n;i++){write_u32(log_ring[log_tail],BSP_UART_TERMINAL);log_tail=(log_tail+1u)&(LOG_RING_BYTES-1u);}
 log_queued-=n;log_sent+=n;
}
static void logf_line(const char *fmt,...) {
#ifndef G27_QUIET
    /* Static formatting buffer avoids a 4KiB stack spike. A failed/oversize
     * line is dropped whole, not silently emitted as a truncated proof. */
    static char b[768]; va_list ap; va_start(ap,fmt); int n=vsnprintf(b,sizeof(b),fmt,ap); va_end(ap);
    if(n<0||(unsigned)n>=sizeof b){++log_dropped;return;}
    if(!log_nonblocking){bsp_printf("%s",b);return;}
    if((unsigned)n>LOG_RING_BYTES-log_queued){++log_dropped;return;}
    for(unsigned i=0;i<(unsigned)n;i++){log_ring[log_head]=(unsigned char)b[i];log_head=(log_head+1u)&(LOG_RING_BYTES-1u);}
    log_queued+=(unsigned)n;
#else
    (void)fmt;
#endif
}
#else
#define logf_line(...) ((void)0)
#define log_drain() ((void)0)
#endif
static void fail(const char *stage,int rc) {
    (void)stage;(void)rc;
    /* Fatal path stops simulation and has no performance acceptance window.
     * Discard queued diagnostics rather than wait to flush them before error. */
#if AD_RUNTIME_LOG
    log_nonblocking=0;
#endif
    logf_line("AD FAIL %s rc=%d gpu=%08x fifo=%08x\r\n",stage,rc,read_u32(GPU_BASE),read_u32(GPU_BASE+4));
    for(;;) frame_pipeline_poll();
}
/* Physical-time watchdog, not pacing. Timeout never releases protected FBs. */
#define FRAME_CONSUME_TIMEOUT_MS 2000u
static uint32_t consume_submitted_ms;
static uint32_t loop_now,lean_loops;
static uint64_t loop_raw;
static uint32_t wake_irq;
static uint8_t scheduler_running;
static void scheduler_idle(unsigned busy){
 uint64_t input=(loop_raw/(SYSTEM_CLINT_HZ/1000u)+1u)*(SYSTEM_CLINT_HZ/1000u);
 uint64_t next=busy?cpu_scheduler_earliest(input,loop_raw+100u*(SYSTEM_CLINT_HZ/1000000u)):input;
 wake_irq=vsync_ticks;
 (void)cpu_scheduler_arm_deadline(next);
}
/* One pending display request plus one independently owned GPU frame.
 * Metadata is frozen once at handoff, never on simulation ticks. */
static VisibleState *pipeline_visible;
static RendererSnapshot *pipeline_working_snapshot;
static EnemySnapshot *pipeline_working_enemy;
static struct {
 RendererSnapshot snapshot;
 RenderMetricsSnapshot ready_metrics;
 uint32_t render_complete,submitted,consumed,render_while_pending,ready_wait,aborted;
 uint64_t overlap_ticks,work_begin,ready_at,present_delay_ticks;
} pipeline;
static inline int submit_tracked(u8 frame) {
    int result=frame_pipeline_submit(frame);
    if(!result){consume_submitted_ms=scheduler_running?loop_now:millis();run_rates_submit(&run_rates);if(scheduler_running){
      if(pipeline_visible->phase==5)render_metrics_commit_snapshot(&pipeline.ready_metrics);else render_metrics_commit();
      if(pipeline_visible->phase==5&&renderer_record_submitted(frame,pipeline_working_snapshot,pipeline_working_enemy))fail("submitted metadata",-1);
      pipeline_visible->submitted=pipeline_visible->staged;
      pipeline_visible->submitted_target=frame;
      pipeline.snapshot=*pipeline_working_snapshot;++pipeline.submitted;
    }}
    return result;
}
static inline void consume_deadline(void) {
    if((uint32_t)((scheduler_running?loop_now:millis())-consume_submitted_ms)>=FRAME_CONSUME_TIMEOUT_MS)
        fail("frame consume timeout",-2000);
}
typedef struct {
    uint32_t n,max_total,max_restore,max_sprite,over_budget;
    uint64_t total,restore,sprite,copy_cmd,sprite_cmd,retries,pixels;
} Perf;
static Perf perf[5];
static uint32_t reported_uart_us;
static uint32_t render_skipped,gate_probes,gate_candidates,gate_skips;
#ifndef AD_PERF_VERBOSE
#define AD_PERF_VERBOSE 0
#endif
#ifndef AD_PERF_REPORT_MS
#define AD_PERF_REPORT_MS 5000u
#endif
#ifndef AD_PERF_RUNTIME_LOG
#define AD_PERF_RUNTIME_LOG 1
#endif
/* Exclusive wall-time estimate, not retired CPU cycles. Blocking GPU polls
 * are counted by platform; only idle tails of asynchronous polling are excluded.
 * Logic serviced while GPU/VSYNC is pending remains CPU work. */
extern uint64_t gpu_wait_ticks_total(void);
typedef struct {
 uint64_t wall,gpu,present,work,logic,enemy,snapshot,sleep;
 uint64_t sum_work,sum_gpu,sum_present,sum_wall,sum_sleep;
 uint64_t window_work,window_gpu,window_present,window_wall,background;
 uint64_t window_pending_work,window_frame_work;
 uint32_t frames,exceed,max_work,max_objects,objects,enemy_count,arrow_count,bullet_count,sword_count;
 uint64_t loop_tick,idle_tick;uint8_t idle_kind,started,frame_open;
 uint32_t accounting_error,wait_overlap,frame_invalid,invalid_frames;
 uint64_t gpu_mark,idle_gpu_mark,sleep_mark,window_sleep;
} Budget;
static Budget budget;
static uint32_t load_logic_calls,load_max_active,load_enemy,load_bullet,load_arrow,load_sword;
static void load_sample(const Player *p,const ProjectileSystem *a){
 const EnemyStats *e=enemy_stats();
 load_enemy=e->active;load_bullet=e->bullets;load_arrow=a->active;load_sword=p->action==ACT_MELEE;
 uint32_t total=load_enemy+load_bullet+load_arrow+load_sword;
 if(total>load_max_active)load_max_active=total;
 ++load_logic_calls;
}
static EnemyStats load_previous;
#if AD_RUNTIME_LOG
static void load_report(void){
 #define previous load_previous
 const EnemyStats *e=enemy_stats();
#define LOAD_DIFF(field) (e->field-previous.field)
 logf_line("AD LOAD units=counters window=5s enemy=%u bullets=%u arrows=%u sword=%u maxactive=%u logic_calls=%u sim_substeps=%u replan_used=%u urgent=%u queue_push=%u queue_pop=%u collisioncandidate=%u hits=%u cursor_reads=%u queue_empty=%u replan_scan=%u hardchecks_saved=%u pit_skipped=%u broadphase_reject=%u divisions=%u expiryops=%u\r\n",load_enemy,load_bullet,load_arrow,load_sword,load_max_active,load_logic_calls,LOAD_DIFF(substeps),LOAD_DIFF(replan_used),LOAD_DIFF(replan_urgent),LOAD_DIFF(replan_queue_push),LOAD_DIFF(replan_queue_pop),LOAD_DIFF(collision_candidates),LOAD_DIFF(bullet_hits),LOAD_DIFF(route_fast_reads),LOAD_DIFF(replan_empty_returns),LOAD_DIFF(replan_scan),LOAD_DIFF(hardchecks_saved),LOAD_DIFF(pit_steps_skipped),LOAD_DIFF(broadphase_reject),LOAD_DIFF(divisions),LOAD_DIFF(expiryops));
#undef LOAD_DIFF
 previous=*e;load_logic_calls=load_max_active=0;
#undef previous
}
#endif
#if AD_CPU_PROBE
uint64_t cpu_probe_clock(void){return ticks();}
uint64_t cpu_probe_gpu(void){return gpu_wait_ticks_total();}
#endif
static uint64_t budget_tick(void){return ticks();}
static uint32_t budget_us(uint64_t t){uint64_t us=t/(SYSTEM_CLINT_HZ/1000000u);return us>UINT32_MAX?UINT32_MAX:(uint32_t)us;}
/* All interval endpoints are raw CLINT uint64_t. Reject a reversed clock
 * before subtraction; an invalid sample must never appear as a passing one. */
static uint64_t budget_span(uint64_t start){uint64_t end=budget_tick();if(end<start){++budget.accounting_error;budget.frame_invalid=1;return 0;}return end-start;}
static void budget_loop_at(uint64_t t){
 uint64_t g=gpu_wait_ticks_total();
 if(budget.started){
   int invalid=t<budget.loop_tick||g<budget.gpu_mark;
   uint64_t dt=t>=budget.loop_tick?t-budget.loop_tick:0;
   uint64_t total_wait=g>=budget.gpu_mark?g-budget.gpu_mark:0;
   uint64_t idle=0,pre=dt,pre_wait=total_wait,idle_wait=0;
   if(budget.idle_kind){
    if(budget.idle_tick<budget.loop_tick||budget.idle_tick>t||budget.idle_gpu_mark<budget.gpu_mark||budget.idle_gpu_mark>g)invalid=1;
    else{idle=t-budget.idle_tick;pre=dt-idle;pre_wait=budget.idle_gpu_mark-budget.gpu_mark;idle_wait=g-budget.idle_gpu_mark;}
    if(budget.idle_kind>2)invalid=1;
   }
  if(pre_wait>pre||idle_wait>idle)invalid=1;
  uint64_t oldwork=budget.work,oldgpu=budget.gpu,oldpresent=budget.present;
  budget.wall+=dt;
  if(invalid){
   /* Invalid evidence is never a free wait: retain all prior work and
    * conservatively charge this entire interval, preserving conservation. */
   ++budget.accounting_error;budget.frame_invalid=1;budget.work+=dt;
  }else{
    uint64_t asleep=cpu_scheduler_sleep_ticks()-budget.sleep_mark;
    if(asleep>dt-total_wait){++budget.accounting_error;budget.frame_invalid=1;asleep=0;}
    budget.work+=dt-total_wait-asleep;
    budget.gpu+=total_wait;
    budget.window_sleep+=asleep;budget.sleep+=asleep;
  }
  budget.window_wall+=dt;budget.window_work+=budget.work-oldwork;
   budget.window_pending_work+=budget.work-oldwork;
  budget.window_gpu+=budget.gpu-oldgpu;budget.window_present+=budget.present-oldpresent;
 }
 budget.sleep_mark=cpu_scheduler_sleep_ticks();
  budget.started=1;budget.loop_tick=t;budget.idle_tick=t;
 budget.gpu_mark=g;budget.idle_gpu_mark=g;budget.idle_kind=0;
}
static void budget_loop(void){budget_loop_at(budget_tick());}
static void budget_idle(unsigned kind){scheduler_idle(kind);}
static void budget_frame(void){
 /* Include input/logic executed in this consume iteration in this frame. */
 budget_loop();
 uint32_t work=budget_us(budget.work);
 budget.window_frame_work+=budget.window_pending_work;budget.window_pending_work=0;
 ++budget.frames;
 budget.exceed+=budget.frame_invalid||budget.work>=(uint64_t)ENEMY_FRAME_CPU_BUDGET_US*(SYSTEM_CLINT_HZ/1000000u);
 budget.invalid_frames+=budget.frame_invalid!=0;
 if(work>budget.max_work)budget.max_work=work;
 if(budget.objects>budget.max_objects)budget.max_objects=budget.objects;
 budget.sum_sleep+=budget.sleep;budget.sum_work+=budget.work;budget.sum_gpu+=budget.gpu;budget.sum_present+=budget.present;budget.sum_wall+=budget.wall;
 budget.sleep=budget.work=budget.wall=budget.gpu=budget.present=0;budget.frame_invalid=0;budget.frame_open=0;
}
/* Only a proven unchanged candidate may close a no-render segment.
 * Continuous changed combat frames retain all since-consume gameplay work. */
static int budget_static_idle(const Player*p,const ProjectileSystem*a,const Dragon*d,const GameFlow*f){
 return !enemy_stats()->active&&!enemy_stats()->bullets&&!a->active&&p->action==ACT_IDLE&&d->pose.stage==DRAGON_IDLE&&!d->pose.active&&!game_flow_is_locked(f)&&!scene_mechanisms_busy();
}
static void budget_background(void){
 budget_loop();budget.background+=budget.window_pending_work;budget.window_pending_work=0;
 budget.sleep=budget.work=budget.wall=budget.gpu=budget.present=0;
}
#if AD_RUNTIME_LOG
static void budget_report(void){
 /* Reporting is observational: no-frame does not imply idle. Only the
  * explicit static-idle/pause/ending paths may classify background work. */
 budget_loop();
#if AD_CPU_PROBE
 unsigned n=budget.frames?budget.frames:1;
 logf_line("AD BUDGET measurement_window_only=1 exact_perframe=0 sleep_us=%u framecpu_work_us=%u waitgpu_us=%u waitpresent_us=%u framewall_us=%u maxcpu_us=%u budget_exceed=%u frames=%u counts=%u enemy=%u arrow=%u bullet=%u sword=%u maxcounts=%u accounting_error=%u wait_overlap=%u frame_invalid=%u invalid_frames=%u target_enemy=600 limit_us=5000 strict_lt=1 estimate=1 board_verified=0 logic_us=%u enemy_us=%u snapshot_us=%u\r\n",
 budget_us(budget.sum_sleep/n),budget_us(budget.sum_work/n),budget_us(budget.sum_gpu/n),budget_us(budget.sum_present/n),budget_us(budget.sum_wall/n),budget.max_work,budget.exceed,budget.frames,budget.objects,budget.enemy_count,budget.arrow_count,budget.bullet_count,budget.sword_count,budget.max_objects,budget.accounting_error,budget.wait_overlap,budget.frame_invalid,budget.invalid_frames,budget_us(budget.logic),budget_us(budget.enemy),budget_us(budget.snapshot));
 logf_line("AD WINDOW sleep_us=%u cpu_us=%u gpu_us=%u present_us=%u wall_us=%u background_cpu_us=%u average_cpu_per_displaywindow_us=%u frames=%u background_excluded=1 noframe_window_only=1 gate_probes=%u candidates=%u gate_skips=%u\r\n",budget_us(budget.window_sleep),budget_us(budget.window_work),budget_us(budget.window_gpu),budget_us(budget.window_present),budget_us(budget.window_wall),budget_us(budget.background),budget.frames?budget_us(budget.window_work/n):0,budget.frames,gate_probes,gate_candidates,gate_skips);
 logf_line("AD CLASSIFY cpu_us=%u background_us=%u frame_us=%u unclassified_us=%u raw_ticks_conserved=%u\r\n",budget_us(budget.window_work),budget_us(budget.background),budget_us(budget.window_frame_work),budget_us(budget.window_pending_work),(unsigned)(budget.window_work==budget.background+budget.window_frame_work+budget.window_pending_work));
#else
  static uint32_t last_timer_irqs;
  uint32_t timer_irqs=cpu_scheduler_timer_irqs();
  logf_line("AD CPU probe=off work_scope=wall_minus_wait units=us_u32_saturated totals=window window_us=%u cpu_us=%u gpuwait_us=%u sleep_us=%u frames=%u maxcpu_interval_us=%u periodmean_us=%u measurement_window_only=1 exact_perframe=0 logic_us=%u enemy_us=%u snapshot_us=%u loops=%u timer_irq=%u errors=%u conserved=%u\r\n",budget_us(budget.window_wall),budget_us(budget.window_work),budget_us(budget.window_gpu),budget_us(budget.window_sleep),budget.frames,budget.max_work,budget.frames?budget_us(budget.sum_wall/budget.frames):0,budget_us(budget.logic),budget_us(budget.enemy),budget_us(budget.snapshot),lean_loops,timer_irqs-last_timer_irqs,budget.accounting_error,(unsigned)(budget.window_wall==budget.window_work+budget.window_gpu+budget.window_present+budget.window_sleep));
  last_timer_irqs=timer_irqs;
#endif
  budget.window_sleep=0;lean_loops=0;
 budget.window_pending_work=budget.window_frame_work=0;
 budget.window_work=budget.window_gpu=budget.window_present=budget.window_wall=budget.background=0;gate_probes=gate_candidates=gate_skips=0;
 logf_line("AD UART mode=nonblocking queued=%u dropped_lines=%u sent_bytes=%u drain_max=64 cpu_included=1\r\n",log_queued,log_dropped,log_sent);
 budget.sum_sleep=budget.sum_work=budget.sum_gpu=budget.sum_present=budget.sum_wall=0;budget.frames=budget.exceed=budget.max_work=budget.max_objects=0;
 budget.accounting_error=budget.wait_overlap=budget.invalid_frames=0;budget.logic=budget.enemy=budget.snapshot=0;
 /* frame_invalid belongs to the current, possibly incomplete frame. */
}
/* One compact group per physical 5s; all conversions are report-only. */
static void probe_report(unsigned state){
#if AD_CPU_PROBE
 static CpuProbeReport r;
 static const char *names[CP_PHASES]={"book","uart","pipe","input","manage","diag","render","gate","snapshot","candidate","ready_true","ready_busy","consume_true","consume_busy"};
 cpu_probe_take(&r);
 uint64_t sum=0,wait=0;for(unsigned i=0;i<CP_PHASES;i++){sum+=r.phase[i].ticks;wait+=r.phase[i].wait;}
 logf_line("AD PROBE state=%u window_us=%u total_us=%u work_us=%u wait_us=%u residual_us=%u gapwait_us=%u conserved=%u valid=%u errors=%u reads=%u loopcount=%u checkcount=%u ready_false=%u pending_false=%u same_ms=%u gate_skip=%u newcandidate=%u draw_call=%u continue=%u\r\n",state,budget_us(r.wall),budget_us(sum),budget_us(sum>=wait?sum-wait:0),budget_us(wait),budget_us(r.residual),budget_us(r.wait_residual),(unsigned)(sum+r.residual==r.wall),(unsigned)(!r.errors&&sum>=wait),r.errors,(unsigned)r.reads,r.event[CP_LOOPS],r.event[CP_CHECK],r.event[CP_READY_FALSE],r.event[CP_PENDING_FALSE],r.event[CP_SAME_MS],r.event[CP_GATE_SKIP],r.event[CP_NEW_CANDIDATE],r.event[CP_DRAW],r.event[CP_CONTINUE]);
 /* Enqueue whole lines even when the UART ring is full. */
 for(unsigned i=0;i<CP_PHASES;i++){
  CpuProbeStat *p=&r.phase[i];
  logf_line("AD PROBE_PHASE name=%s wall_us=%u maxwall_us=%u avgwall_us=%u in=%u out=%u cont=%u wait_us=%u\r\n",names[i],budget_us(p->ticks),budget_us(p->max),p->exits?budget_us(p->ticks/p->exits):0,p->calls,p->exits,p->continues,budget_us(p->wait));
 }
#else
 (void)state;
#endif
}
#endif
static uint64_t enemy_cpu_total,enemy_resolve_total;
static uint32_t enemy_cpu_calls,enemy_cpu_max;
typedef struct {uint32_t frames,max_us;uint64_t done_ticks,cpu_ticks,wait_ticks,commands,ops,restore_us,countdown_us,plan_us,pw_us,clear_us,build_us,union_us;uint32_t bounds,lookups,objects,spans,rows,walls,words,pairs,roots;} EnemyRenderPerf;
static EnemyRenderPerf enemy_render_perf;
static void sample_enemy_render(uint32_t restore,uint32_t countdown){const RendererEnemyStats*r=renderer_enemy_stats();EnemyRenderPerf*p=&enemy_render_perf;++p->frames;p->plan_us+=r->plan_touch_ticks;p->pw_us+=r->playerwall_ticks;p->clear_us+=r->bins_clear_ticks;p->build_us+=r->bins_build_ticks;p->union_us+=r->candidate_union_ticks;p->bounds+=r->rect_bound_recomputes;p->lookups+=r->bounds_lookups;p->objects+=r->enemy_objects;p->spans+=r->span_visits;p->rows+=r->player_row_queries;p->walls+=r->wall_span_visits;p->words+=r->binwords;p->pairs+=r->candidatepairs;p->roots+=r->rootsteps;p->done_ticks+=r->total_ticks;p->cpu_ticks+=r->render_cpu_work_us;p->wait_ticks+=r->fence_ticks;p->commands+=r->commands;p->ops+=r->node_visits;p->restore_us+=restore;p->countdown_us+=countdown;/* Renderer *_ticks are dragon_gpu_clock microseconds, NOT raw CLINT. */uint32_t us=(uint32_t)r->total_ticks;if(us>p->max_us)p->max_us=us;}
static void sample(const RendererStats *s,uint32_t restore,uint32_t draw,uint32_t total) {
    unsigned a=s->resolved_action; if(a>=5)return; Perf *p=&perf[a]; ++p->n;
    p->total+=total; p->restore+=restore;p->sprite+=draw;
    if(total>p->max_total)p->max_total=total;
    if(restore>p->max_restore)p->max_restore=restore;
    if(draw>p->max_sprite)p->max_sprite=draw;
    p->over_budget+=total>16667u;p->copy_cmd+=s->copy_cmds;p->sprite_cmd+=s->sprite_cmds;
    p->retries+=s->fifo_retries;p->pixels+=s->sprite_pixels;
}
#if AD_RUNTIME_LOG
static int bounded(uint32_t x){return (int)(x>2147483647u?2147483647u:x);}
#endif
#if AD_RUNTIME_LOG && AD_PERF_VERBOSE
static void report(const Player *player,uint32_t submits,uint32_t irqs,uint32_t span_ms) {
    static const char *names[5]={"idle","walk","melee","ranged","dodge"};
    uint64_t begin=ticks();
    logf_line("AD PERF span_ms=%d submits=%d vsync_sw=%d uart_prev_us=%d pos=%d,%d dir=%d action=%d blocked=%d render_skipped=%d sent0=%d\r\n",
      bounded(span_ms),bounded(submits),bounded(irqs),bounded(reported_uart_us),player->x,player->y,player->dir,player->action,(int)player->dodge_blocked,bounded(render_skipped),(int)(submits==0));
    for(unsigned a=0;a<5;a++) {
        Perf *p=&perf[a]; if(!p->n)continue;
        uint32_t n=p->n,avg=(uint32_t)(p->total/n);
        logf_line("AD RENDER action=%s n=%d restore_avg_us=%d sprite_avg_us=%d total_avg_us=%d total_max_us=%d headroom_avg_us=%d headroom_worst_us=%d over_16ms=%d copy_cmd_avg=%d sprite_cmd_avg=%d sprite_px_avg=%d fifo_retries=%d\r\n",
          names[a],bounded(n),bounded((uint32_t)(p->restore/n)),bounded((uint32_t)(p->sprite/n)),bounded(avg),bounded(p->max_total),
          16667-(int)avg,16667-(int)p->max_total,bounded(p->over_budget),bounded((uint32_t)(p->copy_cmd/n)),bounded((uint32_t)(p->sprite_cmd/n)),
          bounded((uint32_t)(p->pixels/n)),bounded((uint32_t)p->retries));
        *p=(Perf){0};
    }
    reported_uart_us=elapsed_us(begin);render_skipped=0;
}
#endif
int main(void) {
    bsp_init();
    logf_line("AD G2.25_V10Display clock=%u icache=%u dcache=%u profile=%s probe=%u formal_wfi=%u rtl_metadata=unknown_newRTL dirs=8 dodge_dirs=4 sprite_tile24_guard_us=4 copy_guard=0 frame_tail=0 ranged_speed=1.5x timeline=simulation_us control_clock=physical_ms\r\n",(unsigned)SYSTEM_CLINT_HZ,(unsigned)SYSTEM_CORES_0_ICACHE_SIZE,(unsigned)SYSTEM_CORES_0_DCACHE_SIZE,AD_CPU_CONFIG_PROFILE,(unsigned)AD_CPU_PROBE,(unsigned)AD_SCHEDULER_WFI);
    logf_line("AD BOOT STAGE main entered\r\n");
    logf_line("AD PERF MODE runtime_log=%u uart_mode=nonblocking uart_cpu_included=1 benchmark_log_toggle_only=1 auto_acceptance=0 target_enemy=600 cpu_work_strict_lt_us=5000\r\n",(unsigned)AD_RUNTIME_LOG);
    logf_line("AD PROBE MODE probe_enabled=%u sample=all_phase_boundaries phases=14 rawticks=CLINT overhead_estimate=1 residual_not_exact_overhead=1 board_verified=0\r\n",(unsigned)AD_CPU_PROBE);
#if AD_RUNTIME_LOG
    const EnemyMotionRate *knight_rate=enemy_motion_rate(0),*rogue_rate=enemy_motion_rate(1),*dark_rate=enemy_motion_rate(2);
    logf_line("AD MOTION knight=%u/%u rogue=%u/%u dark=%u/%u units=px_per_s online_scale=3/5 ptr_scale=3/5 wave_spawn_scale=13/10 wave_duration_unchanged=1\r\n",(unsigned)knight_rate->numerator,(unsigned)knight_rate->denominator,(unsigned)rogue_rate->numerator,(unsigned)rogue_rate->denominator,(unsigned)dark_rate->numerator,(unsigned)dark_rate->denominator);
    uint64_t boot=ticks();
#endif
    int rc;
    logf_line("AD BOOT STAGE assets begin\r\n");
    rc=external_assets_init();if(rc)fail("external",rc);
    rc=precomputed_data_init();if(rc)fail("precomputed",rc);
    rc=wave_schedule_init(WAVE_SCHEDULE_DEFAULT_ASSET);if(rc)fail("wave schedule",rc);
    rc=runtime_init();
    logf_line("AD BOOT STAGE assets end rc=%d\r\n",rc);if(rc)fail("assets",rc);
    logf_line("AD BOOT STAGE arrows begin\r\n");
    rc=projectile_init();
    logf_line("AD BOOT STAGE arrows end rc=%d\r\n",rc);if(rc)fail("arrows",rc);
    logf_line("AD ASSETS groups=%d arena_bytes=%d inflate_us=%d source=%s\r\n",
      bounded(runtime_group_count()),bounded(runtime_arena_bytes()),bounded(elapsed_us(boot)),runtime_source_sha256);
    logf_line("AD KEYS low_active left=bit1 esc=bit0 up=bit2 down=bit3 right=bit4 E=bit5 K=bit6 J=bit7 spawn=640,150\r\n");
    logf_line("AD PERF_NOTE done_time_not_enqueue; 16667us_budget_reference_only; init_frames_excluded; render_skipped_unit=candidate_ms; UART_safe_phase0_pending0; no_physical_HDMI_ACK\r\n");
    if((rc=scene_init()))fail("scene",rc);
    if((rc=dragon_assets_init()))fail("dragon assets",rc);
    if((rc=player_support_init()))fail("player support",rc);
    if((rc=flow_assets_init()))fail("flow assets",rc);
    if((rc=countdown_assets_init()))fail("countdown assets",rc);
    if((rc=hud_assets_init()))fail("hud assets",rc);
    logf_line("AD BOOT STAGE enemy assets kind=7 begin\r\n");
    rc=enemy_assets_init();
    logf_line("AD BOOT STAGE enemy assets kind=7 end rc=%d cache_sources=%d\r\n",rc,(int)enemy_cache_count());
    if(rc)fail("enemy assets",rc);
#if defined(__riscv)
    __asm__ volatile("fence rw,rw" ::: "memory");
#endif
    if((rc=frame_pipeline_init(0)))fail("pipeline",rc);
    /* Single main owner: keep long-lived state in BSS, not the 4 KiB stack.
     * Explicit reset also permits host pipeline tests to re-enter main. */
    static Player player;
    static Dragon dragon;
    static ProjectileSystem arrows;
    static VisibleState scene;
    static RendererSnapshot snapshot;
    static EnemySnapshot enemy_frame;
    static VisibleSignature candidate;
    static VisibleGate gate;static VisibleKey key;
    gate=(VisibleGate){0};
    static GameFlow flow;
    static PauseControl pause;
    uint8_t hud_stamina_seen=PLAYER_STAMINA_MAX;
    pause_init(&pause);
    static RendererSnapshot latest_safe;
    unsigned displayed_fb=1;int end_last_key=-1;
    uint32_t terminal_rate_seen=0,pause_rate_seen=0;
    int have_safe_world=0;
    uint32_t end_last_ms=0;
    /* Warm every non-playing framebuffer through complete submit/consume cycles.
     * GPU owns all surface writes; no 20-second boot or scene pacing budget. */
    if((rc=scene_rebuild()))fail("scene init GPU",rc);
    for(;;){int ready=renderer_ready();if(ready<0)fail("warm gpu",ready);if(ready)break;frame_pipeline_poll();}
    /* Scene GPU work is DONE here; fill all seven resident slots at startup,
     * before any playable frame. Each fill uses the real completion fence. */
    if((rc=enemy_gpu_warm()))fail("enemy cache startup",rc);
    if(enemy_gpu_resident()!=127u)fail("enemy cache resident",-1);
    logf_line("AD BOOT GPU enemy_cache_resident_mask=%d cache_sources=%d startup_warm=1\r\n",(int)enemy_gpu_resident(),(int)enemy_cache_count());
    for(unsigned warm=0;warm<3;warm++){
        unsigned fb=warm==0?2u:warm==1?3u:1u;
        if((rc=renderer_restore((unsigned)fb)))fail("warm restore",rc);
        for(;;){int ready=renderer_ready();if(ready<0)fail("warm gpu",ready);if(ready)break;frame_pipeline_poll();}
        unsigned irq=csr_read(mstatus);csr_clear(mstatus,MSTATUS_MIE);
        uint32_t consumed=frame_pipeline_consumed_seq();rc=submit_tracked((u8)fb);
        if(irq&MSTATUS_MIE)csr_set(mstatus,MSTATUS_MIE);
        if(rc)fail("warm submit",rc);
        while(frame_pipeline_consumed_seq()==consumed){consume_deadline();frame_pipeline_poll();}
    }
    logf_line("AD BOOT GPU prewarm=3 runtime_render_CPU_pixels=0 enemy_cache_resident=7 cache_slots=7 diag=full resource_inflate_not_render=1\r\n");
    uint32_t now=millis();player_init(&player,now);dragon_init(&dragon,player.e_events);game_flow_init(&flow,now);
    memset(&arrows,0,sizeof(arrows));enemy_init(now);enemy_prepare_navigation(&player);
    memset(&scene,0,sizeof(scene));scene.target=2;
    flow_keys_reset();
    latest_safe=(RendererSnapshot){.world_x_q8=player.xq,.world_y_q8=player.yq,.action=player.action,.dir8=player.dir,.dragon=dragon.pose};
    memset(&snapshot,0,sizeof(snapshot));
    memset(&candidate,0,sizeof(candidate));
    uint32_t submitted_tick=0,last_input=now;
    uint32_t world_now=now,world_real_last=now,flow_now=now;
    int pause_need_world=0,pause_from_saved=0; uint8_t blocked_actions=0;
    uint32_t resume_real=0;
    uint32_t skip_ms=now-1u;
    uint32_t diagnostic_start=now,wave_report_start=now;
    uint32_t report_start=now,report_tick=vsync_ticks,submits=0;unsigned fb_seen[3]={0,0,0};
    uint64_t restore_start=0,draw_start=0;uint32_t restore_us=0;
    #if AD_RUNTIME_LOG
    log_nonblocking=1;
    #endif
    static RunRestart restart;restart=(RunRestart){0};
    run_rates_reset(&run_rates,ticks(),frame_pipeline_consumed_seq());
    cpu_probe_start();
    pipeline_visible=&scene;pipeline_working_snapshot=&snapshot;pipeline_working_enemy=&enemy_frame;
    memset(&pipeline,0,sizeof pipeline);
    scheduler_running=1;
    for(;;) {
        /* Timer ISR/VSYNC drive this RAM-only spin. No CLINT/MMIO here.
         * Busy spinning still costs CPU; it is never recorded as sleep. */
        if(!cpu_scheduler_is_due()&&wake_irq==vsync_ticks)continue;
        cpu_scheduler_cancel();
        loop_raw=ticks();
        loop_now=now=(uint32_t)(loop_raw/(SYSTEM_CLINT_HZ/1000u));++lean_loops;
        CP_EVENT(CP_LOOPS);CP_PHASE(CP_BOOK);
        budget_loop_at(loop_raw);
        CP_PHASE(CP_UART);
        log_drain();
        CP_PHASE(CP_PIPE);
        frame_pipeline_poll();
        if(run_rates_poll(&run_rates,loop_raw,SYSTEM_CLINT_HZ,frame_pipeline_consumed_seq())){
            HudPerf rates={run_rates.processing_fps,run_rates.display_fps,run_rates.revision};hud_set_perf(&rates);
        }
        CP_PHASE(CP_BOOK);
        if(now!=last_input) {
            CP_PHASE(CP_INPUT);
            uint64_t logic_tick=budget_tick();
            uint8_t raw=(uint8_t)gpio_getInput(GPIO0),edges=flow_keys(now,raw);
            run_restart_input(&restart,now,flow.state==FLOW_ENDING_GAMEOVER,raw,edges);
            edges&=(uint8_t)~blocked_actions;
            uint32_t delta=now-world_real_last;world_real_last=now;
            int command=pause_input(&pause,now,edges&1u,!game_flow_should_end(&flow)&&flow.rules.status!=GAME_RULES_VICTORY);
            if(command==2){
                /* Reuse flow's authoritative double-ESC ending transition. */
                flow.esc_pending=1;flow.esc_first_ms=flow_now;
                game_flow_update(&flow,flow_now,1u,&player,&latest_safe);
                if(pause.state!=PAUSE_RUNNING){
                    pause_from_saved=pause.state==PAUSE_STABLE&&pause_snapshot()->valid;
                    if(pause_from_saved){latest_safe=pause_snapshot()->world;have_safe_world=1;}
                    pause.state=PAUSE_RUNNING;
                }
            }else if(command==1){
                if(pause.state==PAUSE_RUNNING){pause.state=PAUSE_DRAIN;pause_need_world=1;}
                else if(pause.state==PAUSE_STABLE){
                    if((rc=pause_restore(&player,&arrows,&dragon,&flow,&world_now,&flow_now)))fail("pause checksum",rc);
                    budget.frame_open=1;
                    CP_PHASE(CP_RENDER);CP_EVENT(CP_DRAW);
                    render_metrics_begin();
                    int resume_target=frame_pipeline_acquire(0);if(resume_target<1)fail("resume acquire",resume_target);
                     scene.target=(unsigned)resume_target;
                     if((rc=renderer_pause_restore(scene.target)))fail("resume copy",rc);
                    unsigned irq=csr_read(mstatus);csr_clear(mstatus,MSTATUS_MIE);submitted_tick=frame_pipeline_consumed_seq();rc=submit_tracked((u8)scene.target);scene.pending=rc==0;if(irq&MSTATUS_MIE)csr_set(mstatus,MSTATUS_MIE);if(rc)fail("resume submit",rc);
                    pause.state=PAUSE_RESUME_SUBMITTED;resume_real=now;CP_PHASE(CP_INPUT);
                }
            }
            if(pause.state!=PAUSE_RUNNING){blocked_actions|=(uint8_t)(~raw)&0xe0u;last_input=now;}
            if(pause.state==PAUSE_RUNNING){
            if(resume_real){delta=1;resume_real=0;}
            flow_now+=delta;
            if(!game_flow_is_locked(&flow))world_now+=delta;
            game_flow_update(&flow,flow_now,0,&player,&latest_safe);
            if(!game_flow_is_locked(&flow)&&player.action<ACT_MELEE)
                (void)game_flow_try_start_altar(&flow,flow_now,player.x,player.y,edges&(1u<<7)&(uint8_t)~blocked_actions);
            if(!game_flow_should_end(&flow)){
                int af=flow.state==FLOW_ALTAR_START?game_flow_altar_frame(&flow,flow_now):(flow.altar_triggered?-1:0);
                unsigned aq=32;uint32_t at=flow_now-flow.state_start_ms;
                if(flow.state==FLOW_ALTAR_START&&at>=1000u)aq=at>=1800u?0:(unsigned)((1800u-at)*32u/800u);
                scene_flow_altar(af,!flow.altar_triggered&&game_flow_near_altar(player.x,player.y),aq);
            }
            if(!game_flow_should_end(&flow)){
                if(!game_flow_is_locked(&flow))newwave_update(&flow.wave,world_now);
                scene_lever_enabled(!game_flow_is_locked(&flow)&&newwave_preparing(&flow.wave));
                scene_wave(newwave_visual(&flow.wave));
            }
            player_set_flow_locked(&player,game_flow_is_locked(&flow));
            blocked_actions&=(uint8_t)~raw;
            if(!game_flow_is_locked(&flow))player_update(&player,world_now,(uint8_t)(raw|blocked_actions));
            if(hud_stamina_seen!=player_stamina(&player)){hud_stamina_seen=player_stamina(&player);++flow.rules.revision;}
            if(!game_flow_is_locked(&flow)){uint64_t resolve_begin=ticks();if(flow.wave.state==WAVE_ACTIVE)enemy_resolve_projectiles(&arrows);enemy_resolve_total+=elapsed_us(resolve_begin);projectile_set_budget(&arrows,enemy_stats()->bullets);projectile_update(&arrows,&player,world_now);dragon_update(&dragon,world_now,player.e_events);scene_dragon(&dragon.pose);scene_update(world_now,player.x,player.y);
                 EnemyStorm storm={0};
                 if(dragon.pose.beam_kind>=0){const DragonFrame*f=dragon_frame(dragon.pose.beam_kind==DRAGON_BEAM_GROW?DRAGON_GROW:DRAGON_LOOP,(unsigned)dragon.pose.beam_frame);if(f){storm.active=1;storm.x=dragon_gpu_origin(dragon.pose.beam_x_q8,f->anchor_x_q8);storm.y=dragon_gpu_origin(dragon.pose.beam_y_q8,f->anchor_y_q8);storm.w=f->w;storm.h=f->h;}}
                 uint64_t enemy_begin=budget_tick();
                 if(flow.wave.state!=WAVE_ACTIVE&&!scene_mechanisms_busy())enemy_prepare_navigation(&player);
                  game_rules_tick(&flow.rules,world_now,(uint8_t)((player.action==ACT_MELEE?GAME_RULES_MELEE:0u)|(player.action==ACT_BOW?GAME_RULES_RANGED:0u)|(dragon.pose.stage!=DRAGON_IDLE?GAME_RULES_DRAGON:0u)));
                   enemy_set_player_damage_guard(game_rules_invulnerable(&flow.rules,world_now,player.action==ACT_DODGE));
                   enemy_update(&player,&arrows,world_now,flow.wave.state==WAVE_ACTIVE&&!scene_mechanisms_busy(),&storm);
                 uint64_t enemy_dt=budget_span(enemy_begin);budget.enemy+=enemy_dt;
                  uint32_t enemy_us=budget_us(enemy_dt);enemy_cpu_total+=enemy_us;++enemy_cpu_calls;if(enemy_us>enemy_cpu_max)enemy_cpu_max=enemy_us;
                  if(flow.wave.state==WAVE_ACTIVE&&!scene_mechanisms_busy()){
                      static WaveSpawnEvent event;
                      if(!flow.schedule_started){
                          /* Preparation can finish before the last wall animation.
                           * Refresh its final navigation identity once, before any spawn. */
                          enemy_prepare_navigation(&player);
                          if((rc=wave_schedule_start(flow.schedule_clock)))fail("wave start",rc);
                          flow.schedule_started=1;
                           (void)game_rules_start_wave(&flow.rules);
                      }
                      else flow.schedule_clock+=delta;
                      uint32_t pressure=enemy_stats()->active+enemy_stats()->bullets+arrows.active+(player.action==ACT_MELEE);
                      int due=wave_schedule_poll(flow.schedule_clock,pressure,&event);
                      if(due<0)fail("wave poll",due);
                      if(due>0){int spawned=enemy_spawn_scheduled(event.kind,event.side,event.entry_id,event.variant);if((rc=wave_schedule_commit(event.slot_id,spawned==0)))fail("wave commit",rc);}
                  }
                  game_rules_apply_counters(&flow.rules,enemy_stats()->rewarded_kills,enemy_stats()->gate_arrivals);
                   game_flow_enemy_events(&flow);
                   int cleared=game_rules_clear_wave(&flow.rules,flow.schedule_started&&wave_schedule_stats()->phase>=WAVE_SCHEDULE_DRAIN,enemy_stats()->active,enemy_stats()->bullets);
                   if(cleared==1){
                       if((rc=wave_schedule_reset()))fail("wave reset",rc);
                       flow.schedule_started=0;flow.schedule_clock=0;
                       newwave_init(&flow.wave);newwave_arm(&flow.wave);
                       /* Player HP, stamina, action and monotonic world time survive. */
                       projectile_clear(&arrows);
                   }
                   game_flow_update(&flow,flow_now,0,&player,&latest_safe);
             }
            last_input=now;
            }
            budget.logic+=budget_span(logic_tick);
            load_sample(&player,&arrows);
        }
        else {CP_EVENT(CP_SAME_MS);}
        CP_PHASE(CP_DIAG);
        (void)diagnostic_start;(void)report_tick;
        /* Nonblocking queue only: report before pause/ending/pending early exits. */
#if AD_RUNTIME_LOG
            if(AD_PERF_RUNTIME_LOG&&now-diagnostic_start>=AD_PERF_REPORT_MS) {
                (void)report_start;(void)report_tick;
                uint64_t uart_begin=ticks();uint32_t skipped_before_report=render_skipped;
                probe_report((unsigned)pause.state|((unsigned)flow.state<<8));
                budget_report();
                logf_line("AD PIPELINE render_while_pending_count=%u ready_wait=%u display_consume=%u gpu_complete=%u submit=%u aborted=%u working_id=%u pending_id=%u phase=%u overlap_ticks=%u presentdelay_us=%u overlap_lower_bound=1 measurement_window_only=1 physical_ack=0\r\n",pipeline.render_while_pending,pipeline.ready_wait,pipeline.consumed,pipeline.render_complete,pipeline.submitted,pipeline.aborted,scene.target,scene.pending?scene.submitted_target:0,scene.phase,(unsigned)pipeline.overlap_ticks,budget_us(pipeline.present_delay_ticks));
                load_report();
                logf_line("AD GAME hp=%u coins=%u wave=%u/3 stamina=%u invulnerable=%u kills=%u gates=%u knight=%u bullet=%u win=%u lost=%u\r\n",flow.rules.hp,flow.rules.coins,flow.rules.wave,(unsigned)player_stamina(&player),(unsigned)game_rules_invulnerable(&flow.rules,world_now,player.action==ACT_DODGE),enemy_stats()->rewarded_kills,enemy_stats()->gate_arrivals,enemy_stats()->knighthits,enemy_stats()->bullethits,(unsigned)(flow.rules.status==GAME_RULES_VICTORY),(unsigned)(flow.rules.status==GAME_RULES_LOST));
#if AD_PERF_VERBOSE
                report(&player,submits,vsync_ticks-report_tick,now-report_start);
                logf_line("AD ARROWS active=%d scheduled=%d spawned=%d culled=%d dropped=%d player_cmd=%d player_px=%d arrow_cmd=%d arrow_px=%d fences=%d\r\n",bounded(arrows.active),bounded(arrows.scheduled),bounded(arrows.spawned),bounded(arrows.culled),bounded(arrows.overflow),bounded(renderer_stats()->player_cmds),bounded(renderer_stats()->player_pixels),bounded(renderer_stats()->arrow_cmds),bounded(renderer_stats()->arrow_pixels),bounded(renderer_stats()->sprite_fences));
                const EnemyStats *es=enemy_stats();
                 logf_line("AD ENEMIES active=%d bullets=%d spawned=%d killed=%d attacks=%d bullet_hits=%d gate=%d events_dropped=%d nav=%d\r\n",(int)es->active,(int)es->bullets,bounded(es->spawned),bounded(es->killed),bounded(flow.enemy_knight_events),bounded(flow.enemy_bullet_hit_events),bounded(flow.enemy_gate_events),bounded(es->event_overflow),bounded(es->nav_rebuilds));
                 const SceneStats *ss=scene_stats();const RendererStats *rs=renderer_stats();
                logf_line("AD SCENE version=%d rebuilds=%d scene_gpu_us=%d scene_gpu_max_us=%d wall_anim=%d trap_anim=%d alpha_cmd=%d alpha_px=%d alpha_fences=%d stable_skip=%d reject=%d\r\n",(int)(ss->version&0x7fffffff),(int)(ss->rebuilds&0x7fffffff),bounded(ss->rebuild_us),bounded(ss->rebuild_max_us),(int)(ss->wall_steps&0x7fffffff),(int)(ss->trap_steps&0x7fffffff),(int)rs->alpha_cmds,(int)rs->alpha_pixels,(int)rs->alpha_fences,bounded(skipped_before_report),(int)(ss->rejected&0x7fffffff));
                for(unsigned phase=0;phase<DRAGON_PERF_PHASES;phase++){DragonPhaseStats*dp=dragon_gpu_perf(phase);if(!dp->samples)continue;logf_line("AD DRAGON phase=%d n=%d done_mean_us=%d done_max_us=%d cmd=%d pixels=%d triggers=%d rejected=%d\r\n",(int)phase,bounded(dp->samples),bounded((uint32_t)(dp->total_us/dp->samples)),bounded(dp->max_us),bounded(dp->commands),bounded(dp->pixels),bounded(dragon.triggers),bounded(dragon.rejected));*dp=(DragonPhaseStats){0};}
#else
                (void)skipped_before_report;
#if AD_CPU_PROBE
                const EnemyStats *es=enemy_stats();
                EnemyRenderPerf *erp=&enemy_render_perf;
                uint32_t frames=0;uint64_t frame_us=0;for(unsigned action=0;action<5;action++){frames+=perf[action].n;frame_us+=perf[action].total;perf[action]=(Perf){0};}
                logf_line("AD CPU ms=%u vs=%u fr=%u us=%u resolve=%u max=%u n=%u a=%u b=%u nav=%u edge=%u hard=%u ops=%u ok=%u stuck=%u rec=%u fixed=%u unreach=%u inside=%u reject=%u drop=%u frame=%u uart=%u\r\n",now-report_start,vsync_ticks-report_tick,frames,(uint32_t)enemy_cpu_total,(uint32_t)enemy_resolve_total,enemy_cpu_max,(unsigned)es->active,(unsigned)arrows.active,(unsigned)es->bullets,es->nav_rebuilds,es->nav_edge_tests,es->nav_hard_queries,es->nav_field_ops,es->nav_reachable,es->nav_stuck,es->nav_recover,es->nav_recovered,es->nav_unreachable,es->nav_inside,es->nav_entry_reject,es->discarded_ms,frames?(uint32_t)(frame_us/frames):0,reported_uart_us);
                uint32_t rn=erp->frames;uint64_t div=rn?rn:1;
                logf_line("AD RENDER cpu_source=render_cpu_work_us subphase_timing=legacy_not_measured fr=%u done=%u max=%u cpu=%u wait=%u plan=%u pw=%u clr=%u build=%u union=%u bounds=%u lookup=%u obj=%u span=%u row=%u wall=%u words=%u pairs=%u roots=%u cmd=%u restore=%u count=%u\r\n",rn,(uint32_t)(erp->done_ticks/div),erp->max_us,(uint32_t)(erp->cpu_ticks/div),(uint32_t)(erp->wait_ticks/div),(uint32_t)(erp->plan_us/div),(uint32_t)(erp->pw_us/div),(uint32_t)(erp->clear_us/div),(uint32_t)(erp->build_us/div),(uint32_t)(erp->union_us/div),erp->bounds,erp->lookups,erp->objects,erp->spans,erp->rows,erp->walls,erp->words,erp->pairs,erp->roots,(uint32_t)erp->commands,(uint32_t)(erp->restore_us/div),(uint32_t)(erp->countdown_us/div));
#endif
                render_skipped=0;
#endif
                /* Software request/retirement diagnostics, never a physical display ACK. */
#if AD_CPU_PROBE
                static FramePipelineStats pipe_report;
                frame_pipeline_get_stats(&pipe_report);
                logf_line("AD PIPE selected_request=%u retiring=%u pending=%u consumed_seq=%u rejected=%u busy=%u gpu_error=%u duplicate=%u stale=%u\r\n",(unsigned)pipe_report.selected_request,(unsigned)pipe_report.retiring,(unsigned)pipe_report.pending,(unsigned)pipe_report.consumed_seq,(unsigned)pipe_report.rejected_submit,(unsigned)pipe_report.busy_pending,(unsigned)pipe_report.gpu_error,(unsigned)pipe_report.duplicate_irq,(unsigned)pipe_report.stale_irq);
#endif
                enemy_render_perf=(EnemyRenderPerf){0};
                enemy_cpu_total=enemy_resolve_total=0;enemy_cpu_calls=enemy_cpu_max=0;
                reported_uart_us=elapsed_us(uart_begin);
                diagnostic_start=now;report_start=now;
                report_tick=vsync_ticks;submits=0;
                /* Input/ESC is always sampled at the single top-of-loop owner.
                 * Do not bypass flow locks or world-clock freeze after UART. */
            }
        if(AD_PERF_RUNTIME_LOG&&now-wave_report_start>=AD_PERF_REPORT_MS){
             const WaveScheduleStats*ws=wave_schedule_stats();
             logf_line("AD WAVE counts=cumulative_not_live phase=%u scheduled=%u actual=%u suppress=%u expired=%u pressure=%u\r\n",ws->phase,ws->scheduled,ws->spawned,ws->suppressed,ws->expired,ws->pressure_latched);
             wave_report_start=now;
         }
#endif
         CP_PHASE(CP_MANAGE);
         if(scene.pending) {CP_EVENT(CP_CHECK);CP_PHASE(CP_CONSUME);
            if(frame_pipeline_consumed_seq()==submitted_tick){CP_LABEL(CP_CONSUME_BUSY);CP_EVENT(CP_PENDING_FALSE);consume_deadline();}
            else {
              ++pipeline.consumed;displayed_fb=scene.submitted_target;
              if(pause.state==PAUSE_ENTER_SUBMITTED||pause.state==PAUSE_RESUME_SUBMITTED||pause.state==PAUSE_STATS_SUBMITTED){
                int resumed=pause.state==PAUSE_RESUME_SUBMITTED;
                visible_consume(&scene);
                scene.completed=pause_snapshot()->completed;scene.valid=pause_snapshot()->visible_valid;
                pause.state=resumed?PAUSE_RUNNING:PAUSE_STABLE;
                if(resumed){world_real_last=now;resume_real=world_real_last;}
              }else{
                if(!flow.world_frozen&&flow.state!=FLOW_ALTAR_START){latest_safe=pipeline.snapshot;have_safe_world=1;}
                visible_consume(&scene);
              }
            }
         }
         if(scene.pending&&!scene.phase&&(pause.state!=PAUSE_RUNNING||game_flow_should_end(&flow)||flow.rules.status==GAME_RULES_VICTORY)){
             budget_idle(2);CP_CLOSE_CONTINUE();continue;
         }
         if(!scene.phase){
             int acquired=frame_pipeline_acquire(0);
             if(acquired<0){if(acquired!=-1)fail("acquire",acquired);budget_idle(2);CP_CLOSE_CONTINUE();continue;}
             scene.target=(unsigned)acquired;
         }
         CP_PHASE(CP_MANAGE);
        /* Never cancel an in-flight FB or publish a partially reset world. */
        if(restart.requested&&!scene.pending&&!scene.phase&&
           end_last_key==(int)FLOW_ENDING_GAMEOVER*100+game_flow_death_frame(&flow,flow_now)){
            int ready=renderer_ready();if(ready<0)fail("restart gpu",ready);
            if(!ready){scheduler_idle(1);CP_CLOSE_CONTINUE();continue;}
            static FramePipelineStats restart_pipe;
            frame_pipeline_get_stats(&restart_pipe);
            if(restart_pipe.pending){consume_deadline();scheduler_idle(1);CP_CLOSE_CONTINUE();continue;}
            if((rc=dragon_gpu_fence()))fail("restart fence",rc);
            if((rc=renderer_reset_for_new_game()))fail("restart renderer",rc);
            if((rc=scene_reset_runtime()))fail("restart scene",rc);
            if((rc=wave_schedule_reset()))fail("restart schedule",rc);
            player_init(&player,0);dragon_init(&dragon,player.e_events);
            game_flow_init(&flow,0);pause_init(&pause);
            memset(&arrows,0,sizeof arrows);enemy_init(0);enemy_prepare_navigation(&player);
            unsigned next_target=scene.target;memset(&scene,0,sizeof scene);scene.target=next_target;
            memset(&snapshot,0,sizeof snapshot);memset(&enemy_frame,0,sizeof enemy_frame);
            memset(&candidate,0,sizeof candidate);gate=(VisibleGate){0};key=(VisibleKey){0};
            latest_safe=(RendererSnapshot){.world_x_q8=player.xq,.world_y_q8=player.yq,.action=player.action,.dir8=player.dir,.dragon=dragon.pose};
            have_safe_world=0;end_last_key=-1;end_last_ms=0;terminal_rate_seen=pause_rate_seen=0;
            pause_need_world=pause_from_saved=0;resume_real=0;
            now=millis();loop_now=now;loop_raw=ticks();last_input=world_real_last=now;
            world_now=flow_now=0;hud_stamina_seen=PLAYER_STAMINA_MAX;
            blocked_actions=(uint8_t)~gpio_getInput(GPIO0);flow_keys_reset();
            restart=(RunRestart){0};submitted_tick=frame_pipeline_consumed_seq();
            memset(perf,0,sizeof perf);memset(fb_seen,0,sizeof fb_seen);
            memset(&pipeline,0,sizeof pipeline);
            budget=(Budget){0};load_previous=(EnemyStats){0};enemy_render_perf=(EnemyRenderPerf){0};
            enemy_cpu_total=enemy_resolve_total=0;enemy_cpu_calls=enemy_cpu_max=0;
            load_logic_calls=load_max_active=load_enemy=load_bullet=load_arrow=load_sword=0;
            render_skipped=gate_probes=gate_candidates=gate_skips=reported_uart_us=lean_loops=0;
            for(unsigned i=0;i<DRAGON_PERF_PHASES;i++)*dragon_gpu_perf(i)=(DragonPhaseStats){0};
            diagnostic_start=wave_report_start=report_start=now;report_tick=vsync_ticks;submits=0;
            skip_ms=now-1u;restore_start=draw_start=0;restore_us=0;
            run_rates_reset(&run_rates,loop_raw,submitted_tick);render_metrics_reset_session();hud_reset_state();cpu_probe_start();
        }
         if(pause.state==PAUSE_STABLE){
             if(!pause.deferred_command&&pause_rate_seen!=run_rates.revision){
                 render_metrics_begin();
                 if((rc=renderer_pause_show(scene.target)))fail("pause stats",rc);
                 if((rc=dragon_gpu_fence()))fail("pause stats done",rc);
                 unsigned irq=csr_read(mstatus);csr_clear(mstatus,MSTATUS_MIE);
                 submitted_tick=frame_pipeline_consumed_seq();rc=submit_tracked((u8)scene.target);scene.pending=rc==0;
                 if(irq&MSTATUS_MIE)csr_set(mstatus,MSTATUS_MIE);if(rc)fail("pause stats submit",rc);
                 pause_rate_seen=run_rates.revision;pause.state=PAUSE_STATS_SUBMITTED;
             }
             budget_background();scheduler_idle(0);CP_CLOSE_CONTINUE();continue;
         }
         if(pause.state==PAUSE_ENTER_SUBMITTED||pause.state==PAUSE_RESUME_SUBMITTED||pause.state==PAUSE_STATS_SUBMITTED){CP_CLOSE_CONTINUE();continue;}
        if(pause.state==PAUSE_DRAIN&&!scene.phase&&!scene.pending&&!pause_need_world){
            budget.frame_open=1;
            pause_save(&player,&arrows,&dragon,&flow,&pipeline.snapshot,&scene,world_now,flow_now);
            CP_PHASE(CP_RENDER);CP_EVENT(CP_DRAW);
            if((rc=renderer_pause_capture(displayed_fb)))fail("pause capture",rc);
            render_metrics_begin();
                    if((rc=renderer_pause_show(scene.target)))fail("pause plaque",rc);
            unsigned irq=csr_read(mstatus);csr_clear(mstatus,MSTATUS_MIE);submitted_tick=frame_pipeline_consumed_seq();rc=submit_tracked((u8)scene.target);scene.pending=rc==0;if(irq&MSTATUS_MIE)csr_set(mstatus,MSTATUS_MIE);if(rc)fail("pause submit",rc);
            pause_rate_seen=run_rates.revision;pause.state=PAUSE_ENTER_SUBMITTED;{CP_CLOSE_CONTINUE();continue;}
        }
        if(flow.rules.status==GAME_RULES_VICTORY&&have_safe_world&&!scene.phase&&!scene.pending){
            if(!flow.world_frozen){
                /* The final clear tick may also change HP/coins; publish its
                 * terminal state before freeze captures the private HUD. */
                static HudState victory_hud;
                victory_hud=(HudState){.hp=flow.rules.hp,.coins=flow.rules.coins,.wave=flow.rules.wave,.stamina=player_stamina(&player),.active_mask=flow.rules.action_mask,.victory=1,.foot_x_q8=player.xq,.foot_y_q8=player.yq};
                hud_set_state(&victory_hud);
                if((rc=renderer_flow_freeze(displayed_fb,&latest_safe)))fail("victory freeze",rc);
                flow.frozen_snapshot=latest_safe;flow.world_frozen=1;
                terminal_rate_seen=UINT32_MAX;
            }
            if(terminal_rate_seen!=run_rates.revision){
                render_metrics_begin();
                    if((rc=renderer_flow_restore(scene.target,0)))fail("victory restore",rc);
                if((rc=renderer_victory(scene.target)))fail("victory overlay",rc);
                if((rc=dragon_gpu_fence()))fail("victory done",rc);
                unsigned irq=csr_read(mstatus);csr_clear(mstatus,MSTATUS_MIE);
                submitted_tick=frame_pipeline_consumed_seq();rc=submit_tracked((u8)scene.target);scene.pending=rc==0;
                if(irq&MSTATUS_MIE)csr_set(mstatus,MSTATUS_MIE);
                if(rc)fail("victory submit",rc);
                terminal_rate_seen=run_rates.revision;
            }
            budget_background();scheduler_idle(0);CP_CLOSE_CONTINUE();continue;
        }
        if(game_flow_should_end(&flow)&&have_safe_world&&!scene.phase&&!scene.pending){
            if(!flow.world_frozen){
                CP_PHASE(CP_RENDER);CP_EVENT(CP_DRAW);
                /* Freeze captures final HUD into private surfaces before gray;
                 * never repaint the displayed framebuffer or mutate frame history. */
                static HudState terminal_hud;
                terminal_hud=(HudState){.hp=flow.rules.hp,.coins=flow.rules.coins,.wave=flow.rules.wave,.stamina=player_stamina(&player),.active_mask=flow.rules.action_mask,.victory=0,.foot_x_q8=player.xq,.foot_y_q8=player.yq};
                hud_set_state(&terminal_hud);
                if((rc=pause_from_saved?renderer_pause_end_freeze(&latest_safe):renderer_flow_freeze(displayed_fb,&latest_safe)))fail("freeze",rc);
                flow.frozen_wave=scene_wave_built();flow.frozen_scene_version=scene_built_version();
                pause_from_saved=0;(void)game_flow_commit_safe_frame(&flow,flow_now,&latest_safe);
                end_last_key=-1;CP_PHASE(CP_MANAGE);
            }
            int death=game_flow_death_frame(&flow,flow_now);
            unsigned gray=flow.state==FLOW_ENDING_FADE?game_flow_fade_q5(&flow,flow_now):20u;
            int key=(int)flow.state*100+(death>=0?death:(int)gray);
            if(key==end_last_key&&terminal_rate_seen==run_rates.revision){budget_background();scheduler_idle(0);{CP_CLOSE_CONTINUE();continue;}}
            if(flow.state==FLOW_ENDING_FADE&&end_last_key>=0&&now-end_last_ms<33u){scheduler_idle(0);CP_CLOSE_CONTINUE();continue;}
            budget.frame_open=1;
            CP_PHASE(CP_RENDER);CP_EVENT(CP_DRAW);
            render_metrics_begin();
            if(flow.state==FLOW_ENDING_FADE){
                if((rc=renderer_flow_restore(scene.target,0)))fail("end restore",rc);
                if((rc=dragon_gpu_fence()))fail("end restore done",rc);
                if((rc=renderer_flow_gray(scene.target,gray)))fail("gray",rc);
            }else if((rc=renderer_flow_death_restore(scene.target,&flow.frozen_snapshot)))fail("death restore",rc);
            if(death>=0&&(rc=renderer_flow_sprite(scene.target,FLOW_DEATH,(unsigned)death,flow.frozen_snapshot.world_x_q8,flow.frozen_snapshot.world_y_q8+50*256,32)))fail("death",rc);
            if(flow.state==FLOW_ENDING_GAMEOVER&&(rc=renderer_flow_sprite(scene.target,FLOW_GAMEOVER,0,640*256,360*256,32)))fail("gameover",rc);
            if((rc=dragon_gpu_fence()))fail("end done",rc);
            unsigned irq=csr_read(mstatus);csr_clear(mstatus,MSTATUS_MIE);submitted_tick=frame_pipeline_consumed_seq();rc=submit_tracked((u8)scene.target);scene.pending=rc==0;if(irq&MSTATUS_MIE)csr_set(mstatus,MSTATUS_MIE);if(rc)fail("end submit",rc);
            end_last_key=key;end_last_ms=now;terminal_rate_seen=run_rates.revision;{CP_CLOSE_CONTINUE();continue;}
        }
        if(scene.phase==5){
            if(scene.pending){++pipeline.ready_wait;budget_idle(2);CP_CLOSE_CONTINUE();continue;}
            unsigned irq=csr_read(mstatus);csr_clear(mstatus,MSTATUS_MIE);
            submitted_tick=frame_pipeline_consumed_seq();rc=submit_tracked((u8)scene.target);scene.pending=rc==0;
            if(irq&MSTATUS_MIE)csr_set(mstatus,MSTATUS_MIE);
            if(rc)fail("ready submit",rc);
            pipeline.present_delay_ticks+=ticks()-pipeline.ready_at;
            ++submits;scene.phase=0;
            /* Fall through: acquire and start the next frame without waiting
             * for display consumption. One GPU command stream remains serial. */
            int acquired=frame_pipeline_acquire(0);
            if(acquired<0){if(acquired!=-1)fail("next acquire",acquired);budget_idle(2);CP_CLOSE_CONTINUE();continue;}
            scene.target=(unsigned)acquired;
            if(pause.state!=PAUSE_RUNNING||game_flow_should_end(&flow)||flow.rules.status==GAME_RULES_VICTORY){CP_CLOSE_CONTINUE();continue;}
        }
        if(scene.phase) {

            CP_PHASE(CP_READY);CP_EVENT(CP_CHECK);int ready=renderer_ready();if(ready<0)fail("gpu",ready);if(!ready){CP_LABEL(CP_READY_BUSY);CP_EVENT(CP_READY_FALSE);budget_idle(1);{CP_CLOSE_CONTINUE();continue;}}
            CP_PHASE(CP_RENDER);CP_EVENT(CP_DRAW);
            if(scene.phase==1) {
                restore_us=elapsed_us(restore_start);draw_start=ticks();
                DragonGPUStats restored={.commands=renderer_stats()->copy_cmds,.pixels=renderer_stats()->copy_pixels};
                DragonPhaseStats *rp=dragon_gpu_perf(DRAGON_PERF_RESTORE);++rp->samples;rp->total_us+=restore_us;if(restore_us>rp->max_us)rp->max_us=restore_us;rp->commands+=restored.commands;rp->pixels+=restored.pixels;
                rc=renderer_draw(scene.target,&snapshot);if(rc)fail("draw",rc);scene.phase=2;{CP_CLOSE_CONTINUE();continue;}
            }
            if(scene.phase==2){rc=renderer_enemy_draw(scene.target);if(rc)fail("enemy draw",rc);scene.phase=3;{CP_CLOSE_CONTINUE();continue;}}
             if(scene.phase==3){rc=renderer_occlude(scene.target);if(rc)fail("alpha",rc);scene.phase=4;{CP_CLOSE_CONTINUE();continue;}}
            rc=renderer_draw_next(scene.target,&snapshot);if(rc<0)fail("arrow",rc);if(rc){CP_CLOSE_CONTINUE();continue;}
            uint64_t countdown_begin=ticks();
            if((rc=renderer_countdown_finish(scene.target)))fail("countdown overlay",rc);
            uint32_t countdown_us=elapsed_us(countdown_begin);
            if(fb_seen[scene.target-1])sample_enemy_render(restore_us,countdown_us);
            uint32_t draw_us=elapsed_us(draw_start),total=elapsed_us(restore_start);
            if(fb_seen[scene.target-1])sample(renderer_stats(),restore_us,draw_us,total);
            fb_seen[scene.target-1]=1;
            /* Full frame (including HUD/countdown) is fenced before READY. */
            if((rc=dragon_gpu_fence()))fail("frame done",rc);
            ++pipeline.render_complete;pipeline.ready_at=ticks();
            render_metrics_capture(&pipeline.ready_metrics);
            budget_frame();scene.phase=5;{CP_CLOSE_CONTINUE();continue;}
        }
        if(pause.state==PAUSE_RUNNING&&!game_flow_is_locked(&flow)&&newwave_begin_safe(&flow.wave,world_now))
            {scene_lever_enabled(1);scene_wave(newwave_visual(&flow.wave));}
        /* Frame pointer lookup is O(1); cache it through same-physical-ms spins.
         * No large snapshot, projectile copy or signature before this gate. */
        /* Same-ms gate precedes key reconstruction. Invalid FB/pause bypass. */
        if(gate.valid&&gate.physical_ms==now&&scene.valid&&pause.state==PAUSE_RUNNING){scheduler_idle(0);CP_CLOSE_CONTINUE();continue;}
        CP_PHASE(CP_GATE);
        key.xq=player.xq;key.yq=player.yq;key.action=player.action;key.dir=player.dir;
        key.arrow_version=arrows.version;key.dragon_version=dragon.version;
        key.enemy_version=enemy_version();key.scene_version=scene_version()+flow.rules.revision+run_rates.revision;
        key.flow_state=flow.state;key.pause_state=pause.state;
        WaveVisual visual=newwave_visual(&flow.wave);
        key.countdown=visual.visible|((uint32_t)visual.remaining<<8)|((uint32_t)visual.alpha_q5<<16);
        if(!gate.valid||gate.physical_ms!=now||gate.key.action!=key.action||gate.key.dir!=key.dir){
            ++gate_probes;key.frame=runtime_get_frame(key.action,key.dir,player.anim_elapsed_us);
        }
        if(!visible_gate(&gate,&key,now,!scene.valid||pause.state==PAUSE_DRAIN)){
            CP_EVENT(CP_GATE_SKIP);++gate_skips;scheduler_idle(0);if(budget_static_idle(&player,&arrows,&dragon,&flow))budget_background();{CP_CLOSE_CONTINUE();continue;}
        }
        CP_EVENT(CP_NEW_CANDIDATE);CP_PHASE(CP_SNAPSHOT);
        ++gate_candidates;
        uint64_t snapshot_tick=budget_tick();
        snapshot=(RendererSnapshot){.world_x_q8=player.xq,.world_y_q8=player.yq,.action=(uint32_t)player.action,.dir8=(uint32_t)player.dir,.elapsed_us=player.anim_elapsed_us};
        snapshot.dragon=dragon.pose;
        snapshot.scene_version=arrows.version+scene_version()+dragon.version+enemy_version()+flow.rules.revision+run_rates.revision;snapshot.arrow_count=projectile_snapshot(&arrows,snapshot.arrows);
        if(visible_signature(&snapshot,&candidate))fail("signature",-1);
        budget.snapshot+=budget_span(snapshot_tick);
        CP_PHASE(CP_CANDIDATE);
        /* Pause freezes exact simulation metadata, including elapsed_us which
         * raster visibility intentionally omits. Publish that final candidate. */
        if(visible_skip(&scene,&candidate)&&!(pause.state==PAUSE_DRAIN&&pause_need_world)){
            if(budget_static_idle(&player,&arrows,&dragon,&flow))budget_background();
            if(pause.state==PAUSE_DRAIN){pause_need_world=0;{CP_CLOSE_CONTINUE();continue;}}
            if(now!=skip_ms){++render_skipped;skip_ms=now;}scheduler_idle(0);{CP_CLOSE_CONTINUE();continue;}
        }
        if(pause.state==PAUSE_DRAIN)pause_need_world=0;
        budget.frame_open=1;
        scene.staged=candidate;
        CP_PHASE(CP_RENDER);CP_EVENT(CP_DRAW);
        restore_start=ticks();pipeline.work_begin=restore_start;
        /* Count actual restore calls with a live driver request below. */
        render_metrics_begin();
        if((rc=scene_rebuild()))fail("scene rebuild",rc);
        CP_PHASE(CP_SNAPSHOT);
        snapshot_tick=budget_tick();
        enemy_snapshot(&enemy_frame,world_now);
        budget.snapshot+=budget_span(snapshot_tick);
        budget.enemy_count=enemy_frame.enemy_count;budget.bullet_count=enemy_frame.bullet_count;budget.arrow_count=snapshot.arrow_count;budget.sword_count=player.action==ACT_MELEE?1u:0u;
        budget.objects=budget.enemy_count+budget.bullet_count+budget.arrow_count+budget.sword_count;
         CP_PHASE(CP_RENDER);
         if((rc=renderer_enemy_snapshot_view(&enemy_frame)))fail("enemy snapshot",rc);
         static HudState hud;
          hud=(HudState){.hp=flow.rules.hp,.coins=flow.rules.coins,.wave=flow.rules.wave,.stamina=player_stamina(&player),.active_mask=flow.rules.action_mask,.victory=flow.rules.status==GAME_RULES_VICTORY,.foot_x_q8=player.xq,.foot_y_q8=player.yq};
          hud_set_state(&hud);
          renderer_countdown_plan(&snapshot);
        static FramePipelineStats overlap_before,overlap_after;
        frame_pipeline_get_stats(&overlap_before);uint64_t overlap_begin=ticks();
        rc=renderer_restore(scene.target);if(rc)fail("restore",rc);scene.phase=1;
        frame_pipeline_get_stats(&overlap_after);
        if(overlap_before.pending){++pipeline.render_while_pending;
          /* Conservative lower bound: only whole calls whose request survived. */
          if(overlap_after.pending==overlap_before.pending)pipeline.overlap_ticks+=ticks()-overlap_begin;
        }
        CP_PHASE(CP_BOOK);
    }
}

