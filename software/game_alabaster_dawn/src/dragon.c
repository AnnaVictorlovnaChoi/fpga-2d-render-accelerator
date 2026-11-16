#include "dragon.h"
static DragonPose idle(void){return (DragonPose){.halo_frame=-1,.beam_kind=DRAGON_BEAM_NONE};}
void dragon_init(Dragon*d,uint32_t events){*d=(Dragon){.pose=idle(),.seen_e_events=events,.version=1};}
int dragon_pose_changed(const DragonPose*a,const DragonPose*b){
 if(a->body_frame!=b->body_frame||a->halo_frame!=b->halo_frame||a->beam_kind!=b->beam_kind)return 1;
 return a->beam_kind!=DRAGON_BEAM_NONE&&(a->beam_frame!=b->beam_frame||a->beam_x_q8!=b->beam_x_q8||a->beam_y_q8!=b->beam_y_q8);
}
static DragonPose resolve(uint32_t t){
 DragonPose p=idle();p.active=1;p.elapsed_ms=t;
 if(t<500){p.stage=DRAGON_MIN_HOLD;return p;}
 if(t<800){p.stage=DRAGON_PREP;p.body_frame=p.halo_frame=(int32_t)((t-500)/60);return p;}
 if(t>=7442){
  if(t<7742){p.stage=DRAGON_POST;p.body_frame=p.halo_frame=4-(int32_t)((t-7442)/60);}
  else p.stage=DRAGON_TAIL;
  return p;
 }
 p.body_frame=p.halo_frame=4;
 unsigned vertical=t>=4952;uint32_t q=t-(vertical?4952u:800u),travel=vertical?890u:2552u;
 int32_t x=vertical?640:104,y=vertical?138:328;
 p.beam_kind=DRAGON_BEAM_GROW;
 uint32_t base=vertical?DRAGON_V_HOLD:DRAGON_H_HOLD;
 if(q<1000){p.stage=base;p.beam_frame=0;}
 else if(q<1300){p.stage=base+1;p.beam_frame=(int32_t)(((q-1000)/100)*2);}
 else if(q<1300+travel){p.stage=base+2;p.beam_kind=DRAGON_BEAM_LOOP;p.beam_frame=(int32_t)(((q-1300)/200)%6);}
 else {p.stage=base+3;p.beam_frame=4-(int32_t)(((q-1300-travel)/100)*2);}
 p.beam_x_q8=x*256;p.beam_y_q8=y*256;
 if(q>=1300){uint32_t move=q-1300;if(move>travel)move=travel;
  if(vertical)p.beam_y_q8+=(int32_t)(374u*256u*move/travel);
  else p.beam_x_q8+=(int32_t)(1072u*256u*move/travel);
 }
 return p;
}
void dragon_update(Dragon*d,uint32_t now,uint32_t events){
 DragonPose old=d->pose;
 if(d->pose.active){uint32_t elapsed=now-d->started_ms;d->pose=elapsed>=DRAGON_DURATION_MS?idle():resolve(elapsed);}
 uint32_t edges=events-d->seen_e_events;d->seen_e_events=events;
 if(edges){if(d->pose.active)d->rejected+=edges;else{++d->triggers;d->rejected+=edges-1;d->started_ms=now;d->pose=resolve(0);}}
 if(dragon_pose_changed(&old,&d->pose))++d->version;
}
