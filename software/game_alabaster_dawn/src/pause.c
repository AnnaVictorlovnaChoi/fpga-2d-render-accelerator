#include "pause.h"
#include <stddef.h>
#include <string.h>
#define PAUSE_MAGIC 0x50323131u
/* Metadata only, static DDR BSS; no image CPU reads/writes or large locals. */
static PauseSnapshot saved;
#include "enemy.h"
/* Fixed upper bound guarded against implementation growth at capture. */
static uint64_t saved_enemy[40000];
static uint32_t enemy_checksum;
static uint32_t enemy_hash(void){const unsigned char*p=(const unsigned char*)saved_enemy;uint32_t h=2166136261u;for(size_t i=0;i<enemy_save_size();i++)h=(h^p[i])*16777619u;return h;}
static uint32_t checksum(void){const unsigned char*p=(const unsigned char*)&saved.player;uint32_t h=2166136261u;for(size_t i=0;i<offsetof(PauseSnapshot,tail_canary)-offsetof(PauseSnapshot,player);i++)h=(h^p[i])*16777619u;return h;}
void pause_init(PauseControl*p){memset(p,0,sizeof(*p));memset(&saved,0,sizeof(saved));enemy_checksum=0;/* Invalid payload is never restored; no 320KiB clearing on restart. */}
int pause_input(PauseControl*p,uint32_t now,uint8_t edge,int enabled){
 if(p->state==PAUSE_ENTER_SUBMITTED||p->state==PAUSE_RESUME_SUBMITTED)return 0;
 if(!enabled){p->esc_pending=p->deferred_command=0;return 0;}
 /* Once a command resolves it cannot be overwritten by later/coincident
  * edges while the stats frame is pending. Execute only after consumption. */
 if(p->deferred_command){
  if(p->state!=PAUSE_STABLE)return 0;
  int command=p->deferred_command;p->deferred_command=0;return command;
 }
 int command=0;
 /* Expire before accepting a later edge: an edge at281ms cannot erase the
  * first single request. That transaction's coincident edge is discarded. */
 if(p->esc_pending&&now-p->esc_ms>280u){p->esc_pending=0;command=1;}
 else if(edge){if(p->esc_pending){p->esc_pending=0;command=2;}else{p->esc_pending=1;p->esc_ms=now;}}
 if(p->state==PAUSE_STATS_SUBMITTED){p->deferred_command=(uint8_t)command;return 0;}
 return command;
}
void pause_save(const Player*p,const ProjectileSystem*a,const Dragon*d,const GameFlow*f,const RendererSnapshot*w,const VisibleState*v,uint32_t wn,uint32_t fn){
 memset(&saved,0,sizeof(saved));saved.version=1;saved.canary=PAUSE_MAGIC;saved.tail_canary=~PAUSE_MAGIC;
 saved.player=*p;saved.arrows=*a;saved.dragon=*d;saved.flow=*f;saved.world=*w;scene_snapshot_save(&saved.scene);
 saved.completed=v->completed;saved.visible_valid=v->valid;saved.saved_target=v->target;saved.world_now=wn;saved.flow_now=fn;int wave_ok=wave_schedule_save(&saved.schedule)==0;saved.checksum=checksum();saved.valid=wave_ok&&enemy_save(saved_enemy,sizeof saved_enemy)==0;enemy_checksum=saved.valid?enemy_hash():0;
}
int pause_restore(Player*p,ProjectileSystem*a,Dragon*d,GameFlow*f,uint32_t*wn,uint32_t*fn){
 if(saved.valid!=1u||saved.version!=1||saved.canary!=PAUSE_MAGIC||saved.tail_canary!=~PAUSE_MAGIC||saved.checksum!=checksum())return -1;
 if(enemy_save_size()>sizeof saved_enemy||enemy_checksum!=enemy_hash()||enemy_restore(saved_enemy,sizeof saved_enemy))return -2;
 if(wave_schedule_restore(&saved.schedule,saved.flow.schedule_clock))return -3;
 *p=saved.player;*a=saved.arrows;*d=saved.dragon;*f=saved.flow;scene_snapshot_restore(&saved.scene);*wn=saved.world_now;*fn=saved.flow_now;return 0;
}
const PauseSnapshot *pause_snapshot(void){return &saved;}
