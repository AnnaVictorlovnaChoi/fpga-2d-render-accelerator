#include "game.h"
#include "assets.h"
#include "fire_schedule.h"
#include <string.h>

/* Simulation only: map-local feet anchors, integer Q8, no GPU or libm. */
#define JUMP_MS 533u
#define DODGE_MS 533u
#define TAP_MS 280u
#define ESC_MS 280u
#define HIT_MS 500u
static const int8_t dx[8]={1,1,0,-1,-1,-1,0,1};
static const int8_t dy[8]={0,-1,-1,-1,0,1,1,1};
static uint32_t down(uint32_t n,uint32_t dt){return n>dt?n-dt:0;}
static int absi(int x){return x<0?-x:x;}
static void set_state(Game*g,unsigned s){g->state=(uint8_t)s;g->state_ms=0;g->ui_dirty=1;}
static void node_ui(Game*g){
 const NodeAsset*n=asset_node(g->route,g->node); unsigned i;
 g->option_count=n->count;g->answer_mask=n->correct_mask;g->near_option=G21_NO_OPTION;
 for(i=0;i<n->count && i<3;i++){
  int x=(int)(g->px_q8/256)-n->ox[i],y=(int)(g->py_q8/256)-n->oy[i];
  if(x*x+y*y<=18*18){g->near_option=(uint8_t)i;break;}
 }
}
void game_init(Game*g){
 memset(g,0,sizeof(*g));g->hp=30;g->stamina=8;g->near_option=G21_NO_OPTION;g->effect_phase_ms=UINT32_MAX;
 g->px_q8=(int32_t)asset_islands[0].cx*256;g->py_q8=(int32_t)asset_islands[0].cy*256;
 g->ui_dirty=1;node_ui(g);
}
void game_rendered(Game*g){g->ui_dirty=0;}
static void defeat(Game*g){g->dodging=0;g->moving=0;g->defeat_level=0;g->defeat_frame=0;set_state(g,GS_DEFEAT_ISLAND);}
static void hurt(Game*g,unsigned damage,int blue){
 if(g->dodging || g->hit_ms)return;
 g->hp=(uint16_t)(g->hp>damage?g->hp-damage:0);g->hit_ms=HIT_MS;
 if(blue)g->slow_ms=3000;
 g->ui_dirty=1;if(!g->hp)defeat(g);
}
static void axis(Game*g,int32_t delta,int yaxis){
 int32_t *p=yaxis?&g->py_q8:&g->px_q8;
 while(delta){
  int32_t step=delta>256?256:(delta< -256?-256:delta);
  int x=(int)((yaxis?g->px_q8:*p+step)/256);
  int y=(int)((yaxis?*p+step:g->py_q8)/256);
  if(!asset_walkable(g->node,x,y))break;
  *p+=step;delta-=step;
 }
}
static void move(Game*g,uint32_t dt,uint8_t keys){
 int x=((keys&KEY_D)!=0)-((keys&KEY_A)!=0);
 int y=((keys&KEY_S)!=0)-((keys&KEY_W)!=0);
 if(g->dodging){
  uint32_t end=(uint32_t)g->dodge_ms+dt;
  int32_t distance;
  if(end>DODGE_MS)end=DODGE_MS;
  distance=(int32_t)((uint32_t)asset_dodge_distance*256u*end/DODGE_MS)-
           (int32_t)((uint32_t)asset_dodge_distance*256u*g->dodge_ms/DODGE_MS);
  x=dx[g->dir];y=dy[g->dir];if(x&&y)distance=distance*181/256;
  axis(g,distance*x,0);axis(g,distance*y,1);g->dodge_ms=(uint16_t)end;
  if(end==DODGE_MS)g->dodging=0;
  return;
 }
 g->moving=(uint8_t)(x!=0||y!=0);
 if(x||y){
  int32_t speed=g->slow_ms?50:100;
  int32_t amount=speed*256*(int32_t)dt;
  if(x&&y)amount=amount*181/256;
  if(y<0)g->dir=(uint8_t)(x<0?3:(x>0?1:2));
  else if(y>0)g->dir=(uint8_t)(x<0?5:(x>0?7:6));else g->dir=(uint8_t)(x<0?4:0);
  g->move_remainder_x+=amount*x;g->move_remainder_y+=amount*y;
  axis(g,g->move_remainder_x/1000,0);axis(g,g->move_remainder_y/1000,1);
  g->move_remainder_x%=1000;g->move_remainder_y%=1000;
 }
}
static void start_dodge(Game*g,unsigned slot){
 static const uint8_t dirs[4]={4,2,6,0};uint8_t bit=(uint8_t)(1u<<slot);
 if((g->dir_tap_valid&bit) && g->physical_ms-g->last_dir_tap[slot]<=TAP_MS && g->stamina && !g->slow_ms && !g->dodging){
  g->dir=dirs[slot];g->dodging=1;g->dodge_ms=0;g->stamina--;g->regen_ms=0;
  g->dodge_sx=g->px_q8;g->dodge_sy=g->py_q8;g->dir_tap_valid&=(uint8_t)~bit;g->ui_dirty=1;
 }else{g->last_dir_tap[slot]=g->physical_ms;g->dir_tap_valid|=bit;}
}
static void clear_landing(Game*g){
 const IslandAsset*a=&asset_islands[g->node];unsigned i;
 for(i=0;i<G21_BULLET_CAP;i++){G21Bullet*b=&g->bullets[i];if(b->alive&&b->x>=a->x0&&b->x<a->x1&&b->y>=a->y0&&b->y<a->y1){b->alive=0;g->bullet_count--;}}
 for(i=0;i<G21_EFFECT_CAP;i++){G21Effect*e=&g->effects[i];if(e->alive&&e->x>=a->x0&&e->x<a->x1&&e->y>=a->y0&&e->y<a->y1){e->alive=0;g->effect_count--;}}
}
static void choose(Game*g,unsigned option){
 const NodeAsset*n=asset_node(g->route,g->node);
 if(option>=n->count||g->near_option!=option)return;
 if(g->node==0){
  if(option>1)return;
  g->route=(uint8_t)option;g->question_total=(uint8_t)(option?10:6);n=asset_node(g->route,0);
 }else if(!(n->correct_mask&(1u<<option))){defeat(g);return;}
 g->jump_to=n->next[option];if(g->jump_to>=18){defeat(g);return;}
 {const IslandAsset*a=&asset_islands[g->jump_to];int x=a->cx-(int)(g->px_q8/256),y=a->cy-(int)(g->py_q8/256);
  if(absi(x)>absi(y)*2)g->dir=(uint8_t)(x<0?4:0);
  else if(absi(y)>absi(x)*2)g->dir=(uint8_t)(y<0?2:6);
  else g->dir=(uint8_t)(y<0?(x<0?3:1):(x<0?5:7));}
 g->jump_x_q8=g->px_q8;g->jump_y_q8=g->py_q8;g->jump_ms=0;
 g->question_ms=0;g->moving=0;set_state(g,GS_JUMP);
}
static void spawn(Game*g,unsigned tower){
 unsigned j;const TurretAsset*t=&asset_turrets[tower];unsigned tr=t->traj[g->shot_dir[tower]&3u];
 g->shot_dir[tower]=(uint8_t)((g->shot_dir[tower]+1u)&3u);
 if(tr>=G21_TRAJECTORIES||!asset_trajs[tr].count)return;
 for(j=0;j<G21_BULLET_CAP;j++)if(!g->bullets[j].alive){
  G21Bullet*b=&g->bullets[j];const TrajPoint*p=&asset_traj_points[asset_trajs[tr].first];
  b->alive=1;b->kind=t->kind;b->traj=(uint16_t)tr;b->life=asset_trajs[tr].count;b->age=0;b->x=p->x;b->y=p->y;g->bullet_count++;break;
 }
}
static void effect_spawn(Game*g){
 unsigned i;g->effect_count=0;g->effect_phase_ms=0;
 for(i=0;i<G21_EFFECT_CAP;i++){
  G21Effect*e=&g->effects[i];e->alive=(uint8_t)(i<asset_effect_count&&asset_effect_frames!=0);
  e->age=0;e->kind=0;e->x=640;e->y=380;if(e->alive)g->effect_count++;
 }
}
static int touching(Game*g,int x,int y,int radius){
 return absi((int)(g->px_q8/256)-x)<radius+2 && absi((int)(g->py_q8/256)-35-y)<radius+2;
}
static void fire_frame(Game*g){
 unsigned i;uint16_t mask=0;
 if(g->enemy_tick<FIRE_SCHEDULE_FRAMES){
  mask=fire_schedule[g->enemy_tick];
  if(g->enemy_tick==FIRE_SCHEDULE_FRAMES-1u)
   for(i=0;i<16;i++)g->fire_remaining[i]=fire_tail_units[i];
 }else{
  /* Continuous rational tail (1/3000s): never replay or reset direction. */
  for(i=0;i<16;i++){
   g->fire_remaining[i]-=100;
   if(g->fire_remaining[i]<=0){g->fire_remaining[i]+=fire_interval_units[i];mask|=(uint16_t)(1u<<i);}
  }
 }
 for(i=0;i<16;i++)if(mask&(1u<<i))spawn(g,i);
}
static void enemies(Game*g,uint32_t dt){
 unsigned i,ticks,first=!g->fire_started;uint32_t before=g->game_ms-dt;
 /* Bounded game_step dt <=20 means at most one common 30Hz boundary.
  * On the first PLAY call, show PC frame 0 immediately, phase still advances. */
 g->table_phase=(uint16_t)(g->table_phase+dt*30u);ticks=g->table_phase/1000u;g->table_phase=(uint16_t)(g->table_phase%1000u);
 if(first){g->fire_started=1;ticks=0;}
 else g->enemy_tick+=ticks;
 if(before/10000u!=g->game_ms/10000u)g->laser_hit_mask=0;
 g->laser_tick=(g->game_ms%10000u)*60u/1000u;
 if(before/12000u!=g->game_ms/12000u)effect_spawn(g);
 else if(g->effect_count)g->effect_phase_ms+=dt;
 for(i=0;i<G21_BULLET_CAP;i++){
  G21Bullet*b=&g->bullets[i];const TrajPoint*p;unsigned radius;
  if(!b->alive)continue;
  if((unsigned)b->age+ticks>=b->life){b->alive=0;g->bullet_count--;continue;}
  b->age=(uint16_t)(b->age+ticks);p=&asset_traj_points[asset_trajs[b->traj].first+b->age];b->x=p->x;b->y=p->y;
  radius=b->kind==4?2:5;
  if(!g->dodging&&!g->hit_ms&&touching(g,b->x,b->y,(int)radius)){
   b->alive=0;g->bullet_count--;hurt(g,b->kind==1?2:1,b->kind==2);
   if(g->state!=GS_PLAY)return;
  }
 }
 if(ticks||first)fire_frame(g); /* old bullets step first; new sample is age 0 */
 for(i=0;i<G21_EFFECT_CAP;i++){
  G21Effect*e=&g->effects[i];const EffectPoint*p;
  if(!e->alive)continue;
  if((unsigned)e->age+ticks>=asset_effect_frames){e->alive=0;g->effect_count--;continue;}
  e->age=(uint16_t)(e->age+ticks);p=&asset_effect_points[(unsigned)e->age*asset_effect_count+i];e->x=p->visible?p->x:-30000;e->y=p->visible?p->y:-30000;e->kind=p->kind;
  if(!g->dodging&&!g->hit_ms&&p->visible&&touching(g,e->x,e->y,p->kind==4?2:5)){e->alive=0;g->effect_count--;hurt(g,1,0);if(g->state!=GS_PLAY)return;}
 }
 if(g->laser_tick>=84u&&g->laser_tick<156u){
  unsigned width=asset_laser_width[g->laser_tick];int x=(int)(g->px_q8/256),y=(int)(g->py_q8/256)-35;
  i=(g->game_ms/10000u)%8u;
  {
   const LaserAsset*l=&asset_lasers[i];uint8_t bit=(uint8_t)(1u<<i);
   int left=l->x<l->end_x?l->x:l->end_x,right=l->x>l->end_x?l->x:l->end_x;
   if(width && !(g->laser_hit_mask&bit) && !g->dodging && !g->hit_ms && x+2>=left&&x-2<=right&&absi(y-(int)l->y)<(int)(width+1u)/2+2){g->laser_hit_mask|=bit;hurt(g,1,0);}
  }
 }
}
static uint8_t input(Game*g,uint8_t raw,uint32_t dt){
 uint8_t want=(uint8_t)~raw,pressed=0;unsigned i;g->physical_ms+=dt;
 if(g->restart_release){
  if(want&KEY_ESC)want&=(uint8_t)~KEY_ESC;
  else g->restart_release=0;
 }
 for(i=0;i<8;i++){
  uint8_t bit=(uint8_t)(1u<<i);
  if((want^g->key_candidate)&bit){g->key_candidate^=bit;g->key_changed[i]=g->physical_ms;}
  else if(((g->key_stable^want)&bit)&&g->physical_ms-g->key_changed[i]>=8u){g->key_stable^=bit;if(want&bit)pressed|=bit;}
 }
 return pressed;
}
static void tick(Game*g,uint32_t dt,uint8_t raw){
 uint8_t pressed=input(g,raw,dt);unsigned i;static const uint8_t direction_keys[4]={KEY_A,KEY_W,KEY_S,KEY_D};
 if(pressed&KEY_ESC){
  if(g->state==GS_WIN||g->state==GS_GAMEOVER||g->state==GS_QUIT){
   uint32_t generation=g->restart_generation+1u;
   game_init(g);g->restart_generation=generation;g->restart_release=1;
   return;
  }
  if(g->escape_pending&&g->physical_ms-g->last_esc<=ESC_MS){g->escape_pending=0;g->exit_requested=1;set_state(g,GS_QUIT);return;}
  g->escape_pending=1;g->last_esc=g->physical_ms;
 }
 if(g->escape_pending&&g->physical_ms-g->last_esc>ESC_MS){
  g->escape_pending=0;
  if(g->state==GS_PAUSE)g->state=g->resume_state;
  else if(g->state!=GS_QUIT){g->resume_state=g->state;g->state=GS_PAUSE;}
  g->ui_dirty=1;
 }
 if(g->state==GS_PAUSE||g->state==GS_QUIT||g->state==GS_WIN||g->state==GS_GAMEOVER)return;
 if(g->state==GS_DEFEAT_ISLAND||g->state==GS_DEFEAT_GREY||g->state==GS_DEFEAT_CG){
  g->state_ms+=dt;
  if(g->state==GS_DEFEAT_ISLAND){g->defeat_level=(uint8_t)(g->state_ms/100u);if(g->state_ms>=900u){g->defeat_level=8;set_state(g,GS_DEFEAT_GREY);}}
  else if(g->state==GS_DEFEAT_GREY){if(g->state_ms>=500u)set_state(g,GS_DEFEAT_CG);}
  else{g->defeat_frame=(uint8_t)(g->state_ms/200u);if(g->defeat_frame>4)g->defeat_frame=4;if(g->state_ms>=3000u)set_state(g,GS_GAMEOVER);}
  return;
 }
 if(g->state==GS_JUMP){
  const IslandAsset*a=&asset_islands[g->jump_to];uint32_t age=g->jump_ms+dt;if(age>JUMP_MS)age=JUMP_MS;
  g->jump_ms=(uint16_t)age;g->px_q8=g->jump_x_q8+((int32_t)a->cx*256-g->jump_x_q8)*(int32_t)age/(int32_t)JUMP_MS;
  g->py_q8=g->jump_y_q8+((int32_t)a->cy*256-g->jump_y_q8)*(int32_t)age/(int32_t)JUMP_MS;
  if(age==JUMP_MS){g->node=g->jump_to;clear_landing(g);if(g->node<=16)g->question_no++;set_state(g,g->node==17?GS_WIN:GS_PLAY);node_ui(g);}return;
 }
 for(i=0;i<4;i++)if(pressed&direction_keys[i])start_dodge(g,i);
 g->hit_ms=down(g->hit_ms,dt);g->slow_ms=down(g->slow_ms,dt);
 if(!g->dodging&&g->stamina<8){g->regen_ms+=dt;if(g->regen_ms>=2000u){g->regen_ms-=2000u;g->stamina++;g->ui_dirty=1;}}
 move(g,dt,g->key_stable);node_ui(g);
 /* Opening island movement must advance its animation without starting the
  * combat/question clocks. Pause/jump/defeat returned above remain frozen. */
 g->animation_ms+=dt;
 if(pressed&KEY_J)choose(g,0);else if(pressed&KEY_K)choose(g,1);else if(pressed&KEY_E)choose(g,2);
 if(g->state!=GS_PLAY)return;
 g->game_ms+=dt;g->question_ms+=dt;
 {const NodeAsset*n=asset_node(g->route,g->node);uint32_t limit=(uint32_t)n->tier*1000u;if(g->question_ms>=limit){defeat(g);return;}}
 enemies(g,dt);
}
void game_step(Game*g,uint32_t dt_ms,uint8_t raw_low_active){
 /* A bounded hardware tick also prevents tunnelling through narrow masks. */
 while(dt_ms){uint32_t dt=dt_ms>G21_TICK_MS?G21_TICK_MS:dt_ms;tick(g,dt,raw_low_active);dt_ms-=dt;}
}
