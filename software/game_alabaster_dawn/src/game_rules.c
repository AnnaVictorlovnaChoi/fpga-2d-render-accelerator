#include "game_rules.h"
void game_rules_init(GameRules*r){*r=(GameRules){.hp=GAME_RULES_MAX_HP,.coins=1000,.revision=1};}
const GameRules *game_rules_get(const GameRules*r){return r;}
int game_rules_invulnerable(const GameRules*r,uint32_t now,int dodge){return dodge||(r->hurt_active&&(int32_t)(now-r->hurt_until_ms)<0);}
void game_rules_tick(GameRules*r,uint32_t now,uint8_t mask){
 if(r->status!=GAME_RULES_ALIVE)return;
 if(r->hurt_active&&(int32_t)(now-r->hurt_until_ms)>=0){r->hurt_active=0;++r->revision;}
 if(mask!=r->action_mask){r->action_mask=mask;++r->revision;}
}
void game_rules_apply_counters(GameRules*r,uint32_t kills,uint32_t gates){
 uint32_t dk=kills-r->rewarded_kills,dg=gates-r->gate_arrivals;
 if(r->status!=GAME_RULES_ALIVE)return;
 r->rewarded_kills=kills;r->gate_arrivals=gates;
 /* 64-bit intermediates preserve netting before clamp even at UINT32_MAX. */
 int64_t coins=(int64_t)r->coins+(int64_t)dk*20-(int64_t)dg*10;
 uint32_t next=coins<0?0u:coins>UINT32_MAX?UINT32_MAX:(uint32_t)coins;
 if(next!=r->coins){r->coins=next;++r->revision;}
 if(!r->coins){r->status=GAME_RULES_LOST;++r->revision;}
}
int game_rules_damage(GameRules*r,uint32_t when){
 if(r->status!=GAME_RULES_ALIVE||game_rules_invulnerable(r,when,0))return 0;
 if(r->hp)--r->hp;
 r->hurt_until_ms=when+500u;r->hurt_active=1;++r->revision;
 if(!r->hp)r->status=GAME_RULES_LOST;
 return 1;
}
int game_rules_start_wave(GameRules*r){
 if(r->status!=GAME_RULES_ALIVE||r->wave_active||r->wave>=GAME_RULES_WAVES)return 0;
 ++r->wave;r->wave_active=1;++r->revision;return 1;
}
int game_rules_clear_wave(GameRules*r,int ended,uint32_t enemies,uint32_t bullets){
 if(r->status!=GAME_RULES_ALIVE||!r->wave_active||!ended||enemies||bullets)return 0;
 r->wave_active=0;++r->revision;
 if(r->wave==GAME_RULES_WAVES){r->status=GAME_RULES_VICTORY;return 2;}
 return 1;
}
