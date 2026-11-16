#include "newwave_state.h"
void newwave_init(NewWave*w){*w=(NewWave){0};}
void newwave_arm(NewWave*w){if(w->state==WAVE_WAIT_ALTAR)w->state=WAVE_ARMED;}
int newwave_begin_safe(NewWave*w,uint32_t now){
 if(w->state!=WAVE_ARMED)return 0;
 w->state=WAVE_PREPARATION;w->began_ms=now;w->elapsed_ms=0;w->remaining=15;w->phase_ms=0;return 1;
}
int newwave_update(NewWave*w,uint32_t now){
 if(w->state!=WAVE_PREPARATION)return 0;
 uint32_t t=now-w->began_ms;if(t>15000u)t=15000u;
 w->elapsed_ms=t;w->phase_ms=(uint16_t)(t%1000u);w->remaining=(uint8_t)(15u-t/1000u);
 if(t==15000u){w->state=WAVE_ACTIVE;w->phase_ms=0;}
 return 1;
}
int newwave_preparing(const NewWave*w){return w->state==WAVE_PREPARATION;}
WaveVisual newwave_visual(const NewWave*w){
 WaveVisual v={0};if(w->state!=WAVE_PREPARATION)return v;
 v.visible=1;v.remaining=w->remaining;
 /* PC linear fade represented directly in Q5, no generated animation. */
 v.alpha_q5=(uint8_t)(32u*(1000u-w->phase_ms)/1000u);return v;
}
