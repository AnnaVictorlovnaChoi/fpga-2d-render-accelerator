#include "player.h"
#include "scene.h"
#include <limits.h>
#define KEY_ESC   (1u << 0)
#define KEY_LEFT  (1u << 1)
#define KEY_UP    (1u << 2)
#define KEY_DOWN  (1u << 3)
#define KEY_RIGHT (1u << 4)
#define KEY_E     (1u << 5)
#define KEY_K     (1u << 6)
#define KEY_J     (1u << 7)
#define DIR_KEYS (KEY_LEFT | KEY_UP | KEY_DOWN | KEY_RIGHT)
static const int dx[8]={1,1,0,-1,-1,-1,0,1};
static const int dy[8]={0,-1,-1,-1,0,1,1,1};
static int intent(uint8_t keys) {
    int x=!!(keys&KEY_RIGHT)-!!(keys&KEY_LEFT);
    int y=!!(keys&KEY_DOWN)-!!(keys&KEY_UP);
    if (!x && !y) return -1;
    if (x>0) return y<0?DIR_NE:y>0?DIR_SE:DIR_E;
    if (x<0) return y<0?DIR_NW:y>0?DIR_SW:DIR_W;
    return y<0?DIR_N:DIR_S;
}
static uint32_t elapsed_us(uint32_t now,uint32_t start) {
    uint32_t ms=now-start;
    return ms>UINT32_MAX/1000u?UINT32_MAX:ms*1000u;
}
static int busy(const Player *p) { return p->action>=ACT_MELEE; }
static void start(Player *p,int action,uint32_t now) {
    p->action=action;p->action_start=now;p->anim_elapsed_us=0;
    if (action==ACT_DODGE) {
        --p->stamina;p->stamina_regen_ms=now;
        p->dodge_offset_q8=0;p->dodge_blocked=0;
    }
}
/* At most one pixel per collision query: no tunnelling across mask holes.
 * A failed dodge permanently blocks further displacement for this action. */
static int move(Player *p,int32_t mx,int32_t my) {
    int32_t ox=p->xq,oy=p->yq;
    uint32_t ax=(uint32_t)(mx<0?-mx:mx),ay=(uint32_t)(my<0?-my:my);
    unsigned steps=((ax>ay?ax:ay)+255u)/256u;
    for(unsigned i=1;i<=steps;i++) {
        /* Walk <=15px/update, dodge <=110px: products fit int32 and
         * avoid expensive 64-bit division on the RV32 target. */
        int32_t qx=ox+mx*(int32_t)i/(int32_t)steps;
        int32_t qy=oy+my*(int32_t)i/(int32_t)steps;
        /* Floor negative Q8 positions; truncation would permit -1/256 outside. */
        int x=qx>=0?qx/256:-1+(qx+1)/256;
        int y=qy>=0?qy/256:-1+(qy+1)/256;
        if(!runtime_walkable(x,y)||!scene_walkable(x,y)) return 0;
        p->xq=qx;p->yq=qy;p->x=x;p->y=y;
    }
    return 1;
}
static void dodge_move(Player *p,uint32_t elapsed) {
    const RuntimeDodgeStep *s=runtime_dodge_step(elapsed);
    uint32_t target=s?s->offset_q8:p->dodge_offset_q8;
    if(target>p->dodge_offset_q8 && !p->dodge_blocked) {
        int32_t delta=(int32_t)(target-p->dodge_offset_q8);
        if(!move(p,dx[p->dir]*delta,dy[p->dir]*delta))p->dodge_blocked=1;
    }
    p->dodge_offset_q8=target;
}
void player_init(Player *p,uint32_t now) {
    *p=(Player){0};p->x=640;p->y=150;p->xq=640*256;p->yq=150*256;
    p->dir=DIR_S;p->action=ACT_IDLE;p->last_ms=now;p->action_start=now;
    p->tap_dir=-1;p->stamina=PLAYER_STAMINA_MAX;p->stamina_regen_ms=now;
    for(unsigned i=0;i<8;i++)p->changed_ms[i]=now;
}
void player_set_flow_locked(Player *p, int locked) {
    p->flow_locked = locked ? 1u : 0u;
    if (locked) { p->tapped = 0; p->walk_remainder = 0; }
}
int player_flow_locked(const Player *p) { return p->flow_locked != 0; }
int player_legacy_reset_after_esc_window(Player *p, uint32_t now_ms) {
    uint32_t ee=p->e_events,je=p->j_events,ke=p->k_events,re=p->reset_events;
    player_init(p,now_ms); p->stable=p->candidate=0;
    p->e_events=ee; p->j_events=je; p->k_events=ke; p->reset_events=re+1;
    return 1;
}
uint8_t player_stamina(const Player *p) { return p->stamina; }
static void stamina_update(Player *p,uint32_t now) {
    if(p->action==ACT_DODGE)return;
    if(p->stamina>=PLAYER_STAMINA_MAX){p->stamina_regen_ms=now;return;}
    uint32_t elapsed=now-p->stamina_regen_ms;
    if(elapsed<PLAYER_STAMINA_REGEN_MS)return;
    uint32_t gain=elapsed/PLAYER_STAMINA_REGEN_MS;
    uint32_t missing=PLAYER_STAMINA_MAX-p->stamina;
    if(gain>=missing){p->stamina=PLAYER_STAMINA_MAX;p->stamina_regen_ms=now;}
    else {p->stamina=(uint8_t)(p->stamina+gain);p->stamina_regen_ms+=gain*PLAYER_STAMINA_REGEN_MS;}
}
void player_update(Player *p,uint32_t now,uint8_t raw) {
    uint32_t dt=now-p->last_ms;p->last_ms=now;
    if(dt>100u)dt=100u; /* Never catch up unbounded walk after a stalled loop. */
    uint8_t keys=(uint8_t)~raw,edge=0;
    stamina_update(p,now);
    if(p->flow_locked){
        p->stable=p->candidate=keys;p->tapped=0;
        for(unsigned i=0;i<8;i++)p->changed_ms[i]=now;
        return;
    }
    for(unsigned i=0;i<8;i++) {
        uint8_t bit=(uint8_t)(1u<<i);
        if((keys^p->candidate)&bit) {
            p->candidate=(uint8_t)((p->candidate&~bit)|(keys&bit));
            p->changed_ms[i]=now;
        }
        if(((p->stable^p->candidate)&bit) && now-p->changed_ms[i]>=8u) {
            if(p->candidate&bit)edge|=bit;
            p->stable^=bit;
        }
    }
    /* Flow owns ESC edge dispatch. Player still debounces it, but never
     * performs the legacy immediate reset (first ESC must be deferred). */
    if (p->flow_locked) {
        p->tapped = 0;
        return;
    }
    if(edge&KEY_ESC) {
        /* main/game_flow consumes this edge before calling player_update. */
        edge &= (uint8_t)~KEY_ESC;
    }
    if(edge&KEY_E)++p->e_events;
    int was_busy=busy(p);
    if(was_busy) {
        uint32_t duration=runtime_action_duration(p->action,p->dir);
        uint32_t elapsed=elapsed_us(now,p->action_start);
        if(p->action==ACT_DODGE)dodge_move(p,elapsed);
        if(elapsed<duration) {p->anim_elapsed_us=elapsed;return;}
        uint32_t duration_ms=(duration+999u)/1000u;
        uint32_t after=(now-p->action_start)-duration_ms;
        if(dt>after)dt=after;
        if(p->action==ACT_DODGE)p->stamina_regen_ms=p->action_start+duration_ms;
        start(p,ACT_IDLE,now);
        stamina_update(p,now);
        /* Edges sampled while a locked action ended are not buffered. */
        edge&=(uint8_t)(KEY_ESC|KEY_E);p->tapped=0;
    }
    int d=intent(p->stable);
    if(d>=0)p->dir=d;
    uint8_t dirs=(uint8_t)(p->stable&DIR_KEYS);
    int single=dirs && !(dirs&(dirs-1u));
    if(!single && dirs)p->tapped=0; /* diagonal/chord is never a tap */
    if(single && (edge&DIR_KEYS)) {
        if(p->tapped && p->tap_dir==d && now-p->tap_ms<=280u) {
            p->tapped=0;
            if(p->stamina)start(p,ACT_DODGE,now);
        } else {p->tapped=1;p->tap_dir=d;p->tap_ms=now;}
    }
    if(!busy(p)) {
        if(edge&KEY_J){++p->j_events;if(!scene_interact(now,p->x,p->y))start(p,ACT_MELEE,now);}
        else if(edge&KEY_K){++p->k_events;start(p,ACT_BOW,now);}
    }
    if(busy(p)){if(p->action==ACT_DODGE)dodge_move(p,0);return;}
    int desired=d>=0?ACT_WALK:ACT_IDLE;
    if(p->action!=desired)start(p,desired,now);
    uint32_t period=runtime_action_duration(p->action,p->dir);
    /* Widen BEFORE multiply: non-integral 571429us loop survives >71min. */
    p->anim_elapsed_us=period?(uint32_t)(((uint64_t)(uint32_t)(now-p->action_start)*1000u)%period):0;
    if(d>=0) {
        uint32_t numerator=210u*dt*256u+p->walk_remainder;
        int32_t distance=(int32_t)(numerator/1000u);p->walk_remainder=numerator%1000u;
        if(d&1)distance=distance*181/256;
        (void)move(p,dx[d]*distance,dy[d]*distance);
    }
}
