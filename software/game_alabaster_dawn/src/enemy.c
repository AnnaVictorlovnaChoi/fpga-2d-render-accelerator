#include "enemy.h"
#include "precomputed_data.h"
#include "scene.h"
#include "wave_helper.h"
#include <string.h>
#include <limits.h>
#define GW 80
#define GH 40
#define CELLS (GW*GH)
#define BW 40
#define BH 20
#define SAVE_MAGIC 0x454e3232u
/* 50.4 px/s * 20ms * 256 = 258 + 6/125 Q8 units. */
#define DK_STEP_Q8 258u
#define DK_STEP_REM 6u
#define DK_STEP_DEN 125u
_Static_assert(ENEMY_STEP_MS==20u,"DK rate requires 20ms steps");
#define NAV_NODES 33
#define NAV_INF 0x3fffffffu
typedef struct {int16_t l,t,r,b;} HardRect;
typedef struct {int16_t x,y; uint32_t distance[2]; int16_t next[2];} NavNode;
#define EVENT_CAP 64
/* 16px navigation, 32px collision buckets. No image access or heap. */
/* Slot-stable arrays: hot routing/movement fields are contiguous, no pointers. */
typedef struct { int32_t xq,yq; uint32_t generation,route_step,route_remaining; uint16_t dense; uint8_t live,type,hp,entry,attacking,dying,white,side,route_failed; uint8_t pad; } HotMob;
typedef HotMob Mob;
typedef struct { uint32_t serial,born,attack_at,last_attack,fire_at,flash_at,death_at,melee_serial; int16_t entry_y,entry_x,nav_target; uint8_t recovering,plan_valid; uint32_t plan_wall,plan_field; int16_t plan_x,plan_y; int32_t step_x,step_y; uint32_t remaining,step_len; int32_t rem_x,rem_y,err_x,err_y; uint32_t divisor; int32_t expected_x,expected_y; uint16_t stalled,route_id,route_entry; uint8_t route_variant,speed_phase; uint32_t route_wall; int32_t ray_dx,ray_dy; } ColdMob;
typedef struct { uint32_t walk_due,attack_due,serial; int16_t prev,next; uint8_t walk_frame,attack_frame,group,frame,flags,cache; } VisualCache;
typedef struct { int32_t xq,yq,vxq,vyq; uint32_t serial,id; uint16_t dense; uint8_t live; } Bullet;
typedef struct { uint32_t magic,last_ms,sim_ms,remainder_ms,event_world_ms,active_since,spawn_credit,rng,serial,melee_start,melee_serial,wall_version; int player_cell; uint32_t field_at,field_revision; int16_t target_x,target_y; HardRect hard[8]; uint8_t hard_n,node_n; NavNode nodes[NAV_NODES]; uint16_t links[NAV_NODES][NAV_NODES]; uint8_t started,melee_seen,damage_guard,damage_stamp_valid; uint32_t damage_stamp; EnemyEvent pending_damage; uint8_t damage_pending; EnemyEvent pending_events[EVENT_CAP]; unsigned pending_count; EnemyStats stats; int16_t birth_head,birth_tail; uint16_t replan_cursor; uint32_t expiry_pending[4][32],expiry_due[4][32],expiry_next[4],expiry_generation[ENEMY_CAPACITY]; uint8_t expiry_valid[4]; uint32_t replan_pending[32],replan_grant[32],replan_words; uint32_t replan_generation[ENEMY_CAPACITY]; VisualCache visual[ENEMY_CAPACITY]; HotMob mob[ENEMY_CAPACITY]; ColdMob cold[ENEMY_CAPACITY]; Bullet bullet[ENEMY_BULLET_CAPACITY]; uint16_t active[ENEMY_CAPACITY],free_mob[ENEMY_CAPACITY],active_b[ENEMY_BULLET_CAPACITY],free_b[ENEMY_BULLET_CAPACITY],free_n,free_bn; uint8_t blocked[CELLS],edges[CELLS]; uint16_t dist[2][CELLS]; int16_t entry_node[3][430],entry_origin[2][430]; uint16_t entry_count[3]; } System;
static System s;
_Static_assert(sizeof(HotMob)==32,"HotMob must occupy four 8-byte units");
_Static_assert(sizeof(ColdMob)==116,"ColdMob layout must not silently grow");
_Static_assert(sizeof(VisualCache)==24,"Visual cache must remain compact");
_Static_assert(sizeof(System)<=320000u,"pause state exceeds 320KB");
#define C(m) (s.cold[(unsigned)((m)-s.mob)])
static uint16_t queue[CELLS];
static int16_t bucket[BW*BH],next[ENEMY_CAPACITY],bucket_prev[ENEMY_CAPACITY],bucket_of[ENEMY_CAPACITY];
static void bucket_reset(void);
static void bucket_detach(unsigned);
static void bucket_sync(unsigned);
static void bucket_build(void);
static uint32_t seen[ENEMY_CAPACITY],query_serial;
static uint64_t bucket_rows[BH];
static uint32_t melee_candidates[32];
static void visual_sync(Mob*);
#define V(m) (s.visual[(unsigned)((m)-s.mob)])
/* External immutable descriptors are not serialized: reconstruct on restore. */
static PcRouteCursor mob_route[ENEMY_CAPACITY];
static void replan_cancel(unsigned i);
static void replan_enqueue(unsigned i);
static int replan_needed(const Mob*m);
static int8_t replan_cache[ENEMY_CAPACITY];
static uint8_t replan_cache_active;
enum { EXP_DEATH,EXP_WHITE,EXP_FIRE,EXP_ATTACK };
static uint32_t expiry_time(unsigned k,unsigned i){ColdMob*c=&s.cold[i];return k==EXP_DEATH?c->death_at+150u:k==EXP_WHITE?c->flash_at+120u:k==EXP_FIRE?c->fire_at+6000u:c->attack_at+450u;}
static uint32_t snapshot_epoch;
static EnemySnapshot *snapshot_last;
static uint32_t snapshot_last_version,snapshot_last_epoch;
static uint8_t snapshot_cached;
#define events s.pending_events
#define event_n s.pending_count
static const EnemyConfig cfg={{84,126,105},150,150,900,36,6000,180,20,48,0,{24,60,120},1,2,3,30000,1};
const EnemyConfig *enemy_config(void){return &cfg;}
static const EnemyMotionRate motion_rate[3]={{252,5,ENEMY_MOTION_ONLINE_DK},{378,5,ENEMY_MOTION_PTR9},{63,1,ENEMY_MOTION_PTR9}};
const EnemyMotionRate *enemy_motion_rate(unsigned type){return type<3?&motion_rate[type]:NULL;}
static int abs_i(int x){return x<0?-x:x;}
/* Exact integer floor sqrt; local nav vectors avoid 64-bit loop arithmetic. */
static unsigned root(uint64_t n){if(n<=UINT32_MAX){uint32_t v=(uint32_t)n,bit=1u<<30,r=0;while(bit>v)bit>>=2;while(bit){if(v>=r+bit){v-=r+bit;r=(r>>1)+bit;}else r>>=1;bit>>=2;}return r;}uint64_t bit=(uint64_t)1<<62,r=0;while(bit>n)bit>>=2;while(bit){if(n>=r+bit){n-=r+bit;r=(r>>1)+bit;}else r>>=1;bit>>=2;}return (unsigned)r;}
static uint32_t mob_id(unsigned i){return s.mob[i].generation*1024u+i;}
void enemy_set_player_damage_guard(int invulnerable){s.damage_guard=(uint8_t)!!invulnerable;}
static void event_source(unsigned kind,unsigned type,uint32_t id,int32_t xq,int32_t yq,unsigned source,unsigned reason){
 int damage=kind==ENEMY_EVENT_KNIGHT_ATTACK||kind==ENEMY_EVENT_BULLET_PLAYER_HIT;
 if(damage&&s.damage_stamp_valid&&s.damage_stamp==s.event_world_ms)return;
 EnemyEvent e={s.event_world_ms,id,xq,yq,(uint8_t)kind,(uint8_t)type,(uint8_t)source,(uint8_t)reason};
 if(damage){
  /* Earliest unconsumed damage survives economy saturation. Drain each update. */
  if(!s.damage_pending){s.pending_damage=e;s.damage_pending=1;}
  s.damage_stamp=s.event_world_ms;s.damage_stamp_valid=1;return;
 }
 if(event_n<EVENT_CAP)events[event_n++]=e;else ++s.stats.event_overflow;
}
static void event(unsigned kind,unsigned type,uint32_t id,int32_t xq,int32_t yq,uint32_t now){event_source(kind,type,id,xq,yq,kind==ENEMY_EVENT_KNIGHT_ATTACK?ENEMY_DAMAGE_KNIGHT:kind==ENEMY_EVENT_BULLET_PLAYER_HIT?ENEMY_DAMAGE_BULLET:ENEMY_DAMAGE_NONE,ENEMY_KILL_NONE);(void)now;}
unsigned enemy_events(EnemyEvent*out,unsigned n){unsigned first=0;if(n&&s.damage_pending){if(out)*out++=s.pending_damage;s.damage_pending=0;--n;first=1;}if(n>event_n)n=event_n;if(out&&n)memcpy(out,events,n*sizeof(*out));if(n<event_n)memmove(events,events+n,(event_n-n)*sizeof(*out));event_n-=n;return n+first;}
/* Compact deadline membership queues. The earliest timestamp gates scanning;
 * no live gargoyle cold timestamp is read on a non-due step. Due flags are
 * applied at the original per-entity location, not in this preparation pass. */
static int expiry_is_due(unsigned k,unsigned i){return (s.expiry_due[k][i/32]>>(i%32))&1u;}
static void expiry_cancel(unsigned i){for(unsigned k=0;k<4;k++){s.expiry_pending[k][i/32]&=~(1u<<(i%32));s.expiry_due[k][i/32]&=~(1u<<(i%32));}}
static void expiry_arm(unsigned k,unsigned i){uint32_t at=expiry_time(k,i);s.expiry_generation[i]=s.mob[i].generation;s.expiry_due[k][i/32]&=~(1u<<(i%32));s.expiry_pending[k][i/32]|=1u<<(i%32);if(!s.expiry_valid[k]||(int32_t)(at-s.expiry_next[k])<0){s.expiry_next[k]=at;s.expiry_valid[k]=1;}}
static void expiry_prepare(uint32_t now){for(unsigned k=0;k<4;k++){
 if(!s.expiry_valid[k]||(int32_t)(now-s.expiry_next[k])<0)continue;
 s.expiry_valid[k]=0;
 for(unsigned w=0;w<32;w++){uint32_t bits=s.expiry_pending[k][w];while(bits){unsigned bit=(unsigned)__builtin_ctz(bits),i=w*32+bit;uint32_t mask=1u<<bit;bits&=bits-1;++s.stats.expiryops;
  if(i>=ENEMY_CAPACITY||!s.mob[i].live||s.mob[i].generation!=s.expiry_generation[i]){s.expiry_pending[k][w]&=~mask;continue;}
  uint32_t at=expiry_time(k,i);if((int32_t)(now-at)>=0){s.expiry_due[k][w]|=mask;s.expiry_pending[k][w]&=~mask;}
  else if(!s.expiry_valid[k]||(int32_t)(at-s.expiry_next[k])<0){s.expiry_next[k]=at;s.expiry_valid[k]=1;}
 }}
}}
static void remove_mob(unsigned i){Mob*m=&s.mob[i];expiry_cancel(i);replan_cancel(i);bucket_detach(i);unsigned d=m->dense,last=--s.stats.active;s.active[d]=s.active[last];if(d!=last){unsigned moved=s.active[d];bucket_detach(moved);s.mob[moved].dense=(uint16_t)d;bucket_sync(moved);}VisualCache*v=&s.visual[i];if(v->prev>=0)s.visual[v->prev].next=v->next;else s.birth_head=v->next;if(v->next>=0)s.visual[v->next].prev=v->prev;else s.birth_tail=v->prev;m->live=0;s.free_mob[s.free_n++]=(uint16_t)i;++s.stats.version;}
static void remove_bullet(unsigned i){Bullet*b=&s.bullet[i];unsigned d=b->dense,last=--s.stats.bullets;s.active_b[d]=s.active_b[last];s.bullet[s.active_b[d]].dense=(uint16_t)d;b->live=0;s.free_b[s.free_bn++]=(uint16_t)i;++s.stats.version;}
static void kill(Mob*m,uint32_t now,unsigned source){if(m->dying)return;replan_cancel((unsigned)(m-s.mob));bucket_detach((unsigned)(m-s.mob));m->dying=1;C(m).death_at=now;expiry_cancel((unsigned)(m-s.mob));expiry_arm(EXP_DEATH,(unsigned)(m-s.mob));m->white=0;m->attacking=0;++s.stats.killed;if(source!=ENEMY_DAMAGE_PIT)++s.stats.rewarded_kills;event_source(ENEMY_EVENT_KILLED,m->type,mob_id((unsigned)(m-s.mob)),m->xq,m->yq,source,source==ENEMY_DAMAGE_PIT?ENEMY_KILL_PIT:ENEMY_KILL_PLAYER);visual_sync(m);++s.stats.version;}
/* Integer continuous segment / closed integer footprint rectangle. Half-open
 * hard bounds become [l,r-1] x [t,b-1]; free-side corners use l-1/r,t-1/b.
 * Cross products fit signed 32 bits over this 1280x630 map. */
static int cross(int ax,int ay,int bx,int by,int cx,int cy){return (bx-ax)*(cy-ay)-(by-ay)*(cx-ax);}
static int edge_hit(int x,int y,int tx,int ty,int ax,int ay,int bx,int by){
 int a=cross(x,y,tx,ty,ax,ay),b=cross(x,y,tx,ty,bx,by);
 int c=cross(ax,ay,bx,by,x,y),d=cross(ax,ay,bx,by,tx,ty);
 return ((a<=0&&b>=0)||(a>=0&&b<=0))&&((c<=0&&d>=0)||(c>=0&&d<=0));
}
static int segment_clear(int x,int y,int tx,int ty,int entry){
 ++s.stats.nav_hard_queries;
 if(scene_enemy_hard(x,y,entry)||scene_enemy_hard(tx,ty,entry))return 0;
 int l=x<tx?x:tx,r=x>tx?x:tx,t=y<ty?y:ty,b=y>ty?y:ty;
 for(unsigned i=0;i<s.hard_n;i++){HardRect q=s.hard[i];int rr=q.r-1,bb=q.b-1;
  if(r<q.l||l>rr||b<q.t||t>bb)continue;
  if(edge_hit(x,y,tx,ty,q.l,q.t,rr,q.t)||edge_hit(x,y,tx,ty,rr,q.t,rr,bb)||edge_hit(x,y,tx,ty,rr,bb,q.l,bb)||edge_hit(x,y,tx,ty,q.l,bb,q.l,q.t))return 0;
 }return 1;
}
/* Route clearance includes foot-floor error. Keep the exact scene/grid
 * geometry unchanged. A start in the margin can leave along its legal side. */
static int route_clear(int x,int y,int tx,int ty,int entry){
 if(!segment_clear(x,y,tx,ty,entry))return 0;
 for(unsigned i=0;i<s.hard_n;i++){HardRect q=s.hard[i];
  if((x<q.l&&tx<q.l)||(x>=q.r&&tx>=q.r)||(y<q.t&&ty<q.t)||(y>=q.b&&ty>=q.b))continue;
  int l=q.l-1,t=q.t-1,r=q.r,b=q.b;
  int xl=x<tx?x:tx,xr=x>tx?x:tx,yt=y<ty?y:ty,yb=y>ty?y:ty;
  if(xr<l||xl>r||yb<t||yt>b)continue;
  if(edge_hit(x,y,tx,ty,l,t,r,t)||edge_hit(x,y,tx,ty,r,t,r,b)||edge_hit(x,y,tx,ty,r,b,l,b)||edge_hit(x,y,tx,ty,l,b,l,t))return 0;
 }return 1;
}
static unsigned nav_length(int x,int y){return root((uint32_t)(x*x+y*y));}
static void graph_field(unsigned which,int tx,int ty){
 uint8_t used[NAV_NODES]={0};
 for(unsigned i=0;i<s.node_n;i++){NavNode*n=&s.nodes[i];n->distance[which]=route_clear(n->x,n->y,tx,ty,0)?nav_length(n->x-tx,n->y-ty):NAV_INF;n->next[which]=-1;}
 for(unsigned pass=0;pass<s.node_n;pass++){int best=-1;uint32_t d=NAV_INF;
  for(unsigned i=0;i<s.node_n;i++)if(!used[i]&&s.nodes[i].distance[which]<d){best=(int)i;d=s.nodes[i].distance[which];}
  if(best<0)break;
  used[best]=1;
  for(unsigned i=0;i<s.node_n;i++){unsigned len=s.links[best][i];++s.stats.nav_field_ops;if(len&&d+len<s.nodes[i].distance[which]){s.nodes[i].distance[which]=d+len;s.nodes[i].next[which]=(int16_t)best;}}
 }++s.stats.nav_rebuilds;
}
static void graph_build(void){
 s.hard_n=0;s.node_n=0;
 s.hard[s.hard_n++]=(HardRect){960,70,1180,130};
 s.hard[s.hard_n++]=(HardRect){758,108,782,122};s.hard[s.hard_n++]=(HardRect){848,108,872,122};
 if(s.wall_version&2)s.hard[s.hard_n++]=(HardRect){AD_ALTAR_X-AD_ALTAR_FOOT_HALF_W,AD_ALTAR_Y-AD_ALTAR_FOOT_H,AD_ALTAR_X+AD_ALTAR_FOOT_HALF_W,AD_ALTAR_Y};
 if(s.wall_version&1){s.hard[s.hard_n++]=(HardRect){0,183,440,217};s.hard[s.hard_n++]=(HardRect){840,183,1280,217};s.hard[s.hard_n++]=(HardRect){0,420,520,454};s.hard[s.hard_n++]=(HardRect){760,420,1280,454};}
 for(unsigned i=0;i<s.hard_n;i++)for(unsigned k=0;k<4;k++){HardRect r=s.hard[i];int x=k&1?r.r+2:r.l-3,y=k&2?r.b+2:r.t-3;if(!scene_enemy_hard(x,y,0)){NavNode*n=&s.nodes[s.node_n++];n->x=(int16_t)x;n->y=(int16_t)y;}}
 memset(s.links,0,sizeof s.links);
 for(unsigned i=0;i<s.node_n;i++)for(unsigned j=0;j<i;j++){++s.stats.nav_edge_tests;if(route_clear(s.nodes[i].x,s.nodes[i].y,s.nodes[j].x,s.nodes[j].y,0)){unsigned d=nav_length(s.nodes[i].x-s.nodes[j].x,s.nodes[i].y-s.nodes[j].y);s.links[i][j]=s.links[j][i]=(uint16_t)(d?d:1);}}
 graph_field(0,640,70);
}
static int pit_sweep(int x,int y,int tx,int ty){int dx=tx-x,dy=ty-y,n=abs_i(dx)>abs_i(dy)?abs_i(dx):abs_i(dy);for(int k=0;k<=n;k++)if(scene_enemy_pit(n?x+dx*k/n:x,n?y+dy*k/n:y))return 1;return 0;}
static int cell(int x,int y){if(x<0)x=0;if(x>1279)x=1279;if(y<0)y=0;if(y>639)y=639;return y/16*GW+x/16;}
static const int dirs[8][2]={{1,0},{0,1},{-1,0},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1}};
/* Point-foot grid: identical geometry for membership, edges and movement.
 * Centers differ by 0/+/-16 on each axis. The 17 integer samples of an
 * axis/45-degree edge are exactly the reverse samples of its reverse edge. */
static int edge_geometry(int a,int b){if(s.blocked[b])return 0;return segment_clear(a%GW*16+8,a/GW*16+8,b%GW*16+8,b/GW*16+8,0);}
static void field(unsigned which,int target){
 uint16_t*d=s.dist[which];unsigned head=0,tail=0;
 for(int i=0;i<CELLS;i++)d[i]=UINT16_MAX;
 if(!which){for(int i=0;i<CELLS;i++){int dx=i%GW*16+8-640,dy=i/GW*16+8-70;if(!s.blocked[i]&&dx*dx+dy*dy<=400){d[i]=0;queue[tail++]=(uint16_t)i;}}}
 else {int best=-1,bd=INT_MAX;for(int i=0;i<CELLS;i++)if(!s.blocked[i]&&s.dist[0][i]!=UINT16_MAX){int dx=i%GW-target%GW,dy=i/GW-target/GW,dd=dx*dx+dy*dy;if(dd<bd){best=i;bd=dd;}}if(best>=0){d[best]=0;queue[tail++]=(uint16_t)best;}}
 while(head<tail){int a=queue[head++],x=a%GW,y=a/GW;for(int k=0;k<8;k++){int xx=x+dirs[k][0],yy=y+dirs[k][1];if(xx<0||xx>=GW||yy<0||yy>=GH)continue;int b=yy*GW+xx;++s.stats.nav_field_ops;if(d[b]!=UINT16_MAX||!(s.edges[a]&(1u<<k)))continue;d[b]=(uint16_t)(d[a]+1);queue[tail++]=(uint16_t)b;}}++s.stats.nav_rebuilds;
}
static const int bottom[15]={170,232,294,356,418,480,542,640,738,800,862,924,986,1048,1110};
static int entry_project(int x,int y,int side){
 int c=cell(side==0?72:side==1?1208:x,side==2?568:y),best=-1,bd=INT_MAX;
 for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int xx=c%GW+dx,yy=c/GW+dy;if(xx<0||xx>=GW||yy<0||yy>=GH)continue;int n=yy*GW+xx,tx=xx*16+8,ty=yy*16+8,dd=abs_i(tx-x)+abs_i(ty-y);if(s.dist[0][n]!=UINT16_MAX&&dd<bd&&segment_clear(x,y,tx,ty,1)){best=n;bd=dd;}}
 return best;
}
static void entry_build(void){
 memset(s.entry_count,0,sizeof s.entry_count);
 /* Each side scans 430 rows once. Nearest-row lookup then uses two linear
  * passes with no geometry calls; spawning is one lookup plus a bounded sweep. */
 for(int side=0;side<2;side++){
  int last=-1;
  for(int i=0;i<430;i++){int n=entry_project(side?1250:30,i+150,side);++s.stats.nav_entry_tests;s.entry_node[side][i]=(int16_t)n;s.entry_origin[side][i]=-1;if(n>=0){last=i;++s.entry_count[side];}if(last>=0)s.entry_origin[side][i]=(int16_t)last;}
  last=-1;for(int i=429;i>=0;i--){if(s.entry_node[side][i]>=0)last=i;int prev=s.entry_origin[side][i];if(last>=0&&(prev<0||last-i<i-prev))s.entry_origin[side][i]=(int16_t)last;}
 }
 for(int i=0;i<15;i++){int n=entry_project(bottom[i],605,2);++s.stats.nav_entry_tests;s.entry_node[2][i]=(int16_t)n;if(n>=0)++s.entry_count[2];}
}
static void navigation(const Player*p){uint32_t v=scene_enemy_wall_version();int pc=cell(p->x,p->y);if(s.wall_version!=v){s.wall_version=v;graph_build();for(int i=0;i<CELLS;i++){s.blocked[i]=(uint8_t)scene_enemy_hard(i%GW*16+8,i/GW*16+8,0);s.edges[i]=0;}
 static const uint8_t forward[4]={0,1,4,5},reverse[4]={2,3,6,7};
 for(int a=0;a<CELLS;a++){if(s.blocked[a])continue;for(int j=0;j<4;j++){int k=forward[j],x=a%GW+dirs[k][0],y=a/GW+dirs[k][1];if(x<0||x>=GW||y<0||y>=GH)continue;int b=y*GW+x;++s.stats.nav_edge_tests;if(edge_geometry(a,b)){s.edges[a]|=(uint8_t)(1u<<k);s.edges[b]|=(uint8_t)(1u<<reverse[j]);}}}
 ++s.stats.nav_edge_rebuilds;field(0,cell(640,70));entry_build();s.player_cell=-1;}if(s.player_cell<0||(s.player_cell!=pc&&(s.sim_ms-s.field_at>=500u||abs_i(p->x-s.target_x)>64||abs_i(p->y-s.target_y)>64))){if(s.player_cell<0)++s.stats.field_initial;else if(abs_i(p->x-s.target_x)>64||abs_i(p->y-s.target_y)>64)++s.stats.field_displacement;else ++s.stats.field_periodic;s.player_cell=pc;s.field_at=s.sim_ms;s.target_x=(int16_t)p->x;s.target_y=(int16_t)p->y;++s.field_revision;graph_field(1,p->x,p->y);for(unsigned n=0;n<s.stats.active;n++)replan_enqueue(s.active[n]);}}
void enemy_prepare_navigation(const Player*p){if(p)navigation(p);}
void enemy_init(uint32_t now){memset(&s,0,sizeof s);s.magic=SAVE_MAGIC;s.last_ms=s.sim_ms=now;s.stats.sim_ms=now;bucket_reset();s.birth_head=s.birth_tail=-1;++snapshot_epoch;snapshot_cached=0;snapshot_last=NULL;snapshot_last_version=snapshot_last_epoch=0;replan_cache_active=0;memset(replan_cache,-1,sizeof replan_cache);memset(melee_candidates,0,sizeof melee_candidates);memset(queue,0,sizeof queue);memset(mob_route,0,sizeof mob_route);s.rng=0x13579bd;s.wall_version=UINT32_MAX;s.player_cell=-1;s.free_n=ENEMY_CAPACITY;s.free_bn=ENEMY_BULLET_CAPACITY;for(unsigned i=0;i<ENEMY_CAPACITY;i++)s.free_mob[i]=(uint16_t)(ENEMY_CAPACITY-1-i);for(unsigned i=0;i<ENEMY_BULLET_CAPACITY;i++)s.free_b[i]=(uint16_t)(ENEMY_BULLET_CAPACITY-1-i);event_n=0;query_serial=0;memset(seen,0,sizeof seen);}
static uint16_t route_number(unsigned type,unsigned side,unsigned entry,unsigned variant,unsigned wall){
 return (uint16_t)(type==ENEMY_WOLF?wall*1750u+variant*875u+side*430u+entry:3500u+variant*860u+side*430u+entry);
}
int enemy_spawn_scheduled(unsigned type,unsigned side,unsigned choice,unsigned variant){
  if(type>ENEMY_GARGOYLE||side>2||choice>=(side==2?15u:430u)||variant>1u||(type==ENEMY_GARGOYLE&&side==2))return ENEMY_SPAWN_INVALID;
  if(s.wall_version==UINT32_MAX||s.wall_version!=scene_enemy_wall_version()||(s.wall_version&~1u)){++s.stats.spawn_deferred;return ENEMY_SPAWN_NOT_READY;}
  if(!s.free_n){++s.stats.spawn_deferred;return ENEMY_SPAWN_CAPACITY;}
  uint32_t now=s.sim_ms;
 int x=side==0?30:side==1?1250:bottom[choice],y=side==2?605:150+(int)choice,node=-1;
 const PcRoute *route=0;PcRouteCursor cursor={0};
 if(type!=ENEMY_DK){
  /* Scheduled route uses the exact PC entry and variant; never random fallback. */
  uint16_t first[2];
  if(!(s.wall_version&~1u))route=pc_route_find(type==ENEMY_WOLF?PC_ROUTE_WOLF:PC_ROUTE_GARGOYLE,side,choice,variant,s.wall_version&1u);
  if(!route||route->flags!=PC_ROUTE_VALID||route->point_count<2u||pc_route_point(route,0,first)||first[0]!=x||first[1]!=y||pc_route_cursor_init(route,1,&cursor)){
   ++s.stats.spawn_deferred;++s.stats.route_deferred;++s.stats.route_invalid;return ENEMY_SPAWN_ROUTE;
  }
 }else{
  if(side==2)node=s.entry_node[2][choice];else{int origin=s.entry_origin[side][choice];if(origin>=0){y=150+origin;node=s.entry_node[side][origin];}}
  if(node<0){++s.stats.spawn_deferred;++s.stats.nav_entry_reject;return ENEMY_SPAWN_ROUTE;}
 }
 unsigned i=s.free_mob[--s.free_n];Mob*m=&s.mob[i];uint32_t gen=m->generation+1;if(!gen||gen>=0x3fffffu)gen=1;
  *m=(Mob){.xq=x*256,.yq=y*256,.generation=gen,.dense=s.stats.active,.route_step=0,.route_remaining=route?route->point_count-1u:0,.live=1,.type=(uint8_t)type,.hp=(uint8_t)(type==ENEMY_DK?2:1),.entry=(uint8_t)(type!=ENEMY_GARGOYLE),.side=(uint8_t)side};
  C(m)=(ColdMob){.serial=++s.serial,.born=now,.last_attack=now-cfg.attack_cooldown_ms,.fire_at=now,.entry_y=(int16_t)(node>=0?node/GW*16+8:y),.entry_x=(int16_t)(node>=0?node%GW*16+8:x),.nav_target=-1,.route_id=route?route_number(type,side,choice,variant,s.wall_version&1u):UINT16_MAX,.route_entry=(uint16_t)choice,.route_wall=s.wall_version,.route_variant=(uint8_t)variant};
 expiry_cancel(i);if(type==ENEMY_GARGOYLE)expiry_arm(EXP_FIRE,i);
  s.visual[i]=(VisualCache){.walk_due=now+cfg.walk_ms,.serial=C(m).serial,.prev=s.birth_tail,.next=-1};if(s.birth_tail>=0)s.visual[s.birth_tail].next=(int16_t)i;else s.birth_head=(int16_t)i;s.birth_tail=(int16_t)i;visual_sync(m);
  if(route)mob_route[i]=cursor;else replan_enqueue(i);s.active[s.stats.active++]=(uint16_t)i;bucket_sync(i);++s.stats.spawned;++s.stats.version;return ENEMY_SPAWN_OK;
}
/* One fixed-time AoS sample per substep. No LOS, graph, sqrt, divide or
 * descriptor search here. Bad resources/wall transitions stop, never teleport. */
static void route_move(Mob*m,unsigned i){
 uint16_t xy[2];PcRouteCursor*r=&mob_route[i];
 if(m->route_failed)return;
 if(C(m).route_wall!=s.wall_version||!m->route_remaining||!r->remaining){
  m->route_failed=1;++s.stats.route_invalid;return;
 }
 const unsigned char*q=r->next;xy[0]=(uint16_t)(q[0]|((uint16_t)q[1]<<8));xy[1]=(uint16_t)(q[2]|((uint16_t)q[3]<<8));r->next+=4;--r->remaining;++s.stats.route_fast_reads;
  m->xq=(int32_t)xy[0]*256;m->yq=(int32_t)xy[1]*256;
 ++m->route_step;--m->route_remaining;++s.stats.route_steps;
 if(m->entry&&xy[0]>=60&&xy[0]<1220&&xy[1]>=70&&xy[1]<580)m->entry=0;
}
static void move_toward_proven(Mob*m,int tx,int ty,unsigned dt,int entry,int proven){
 (void)dt;
 if(!C(m).plan_valid||C(m).expected_x!=m->xq||C(m).expected_y!=m->yq||C(m).plan_x!=tx||C(m).plan_y!=ty||C(m).plan_wall!=s.wall_version){
  C(m).plan_valid=0;
  if(proven)++s.stats.hardchecks_saved;
   if(!proven&&m->type!=ENEMY_GARGOYLE&&!segment_clear(m->xq/256,m->yq/256,tx,ty,entry))return;
  ++s.stats.ray_builds;
   /* Exact Q8 ray; wide arithmetic only on route selection, not each tick. */
  int dx=tx*256-m->xq,dy=ty*256-m->yq;
  unsigned len=root((uint64_t)((int64_t)dx*dx+(int64_t)dy*dy)),step=DK_STEP_Q8;
  C(m).ray_dx=dx;C(m).ray_dy=dy;
  C(m).remaining=len;C(m).step_len=step;C(m).divisor=len?len:1;
  C(m).step_x=len?(int32_t)((int64_t)dx*step/len):0;C(m).step_y=len?(int32_t)((int64_t)dy*step/len):0;
  C(m).rem_x=len?(int32_t)((int64_t)dx*step%len):0;C(m).rem_y=len?(int32_t)((int64_t)dy*step%len):0;C(m).err_x=C(m).err_y=0;
  C(m).plan_x=(int16_t)tx;C(m).plan_y=(int16_t)ty;C(m).plan_wall=s.wall_version;C(m).plan_valid=1;
 }
 /* Only DK uses this online path. Phase belongs to the mob, not its ray. */
 unsigned phase=C(m).speed_phase+DK_STEP_REM,extra=phase>=DK_STEP_DEN;
 if(extra)phase-=DK_STEP_DEN;
 unsigned step=C(m).step_len+extra;
 if(C(m).remaining<=step){
  if(m->type!=ENEMY_GARGOYLE&&scene_enemy_hard(tx,ty,entry)){C(m).plan_valid=0;C(m).remaining=0;return;}
  m->xq=tx*256;m->yq=ty*256;C(m).remaining=0;
 }else{int32_t ox=m->xq,oy=m->yq;int32_t oex=C(m).err_x,oey=C(m).err_y;uint32_t orm=C(m).remaining;m->xq+=C(m).step_x;m->yq+=C(m).step_y;C(m).err_x+=C(m).rem_x;C(m).err_y+=C(m).rem_y;
  if(extra){C(m).err_x+=C(m).ray_dx;C(m).err_y+=C(m).ray_dy;}
  /* |old error|<len, |base remainder|<len, |ray axis|<=len:
   * signed sum is strictly <3*len: at most two carries per axis.
   * Negative rays retain signed remainders. No division on warm steps. */
  for(unsigned carry=0;carry<2;carry++){
   if(C(m).err_x>=(int)C(m).divisor){++m->xq;C(m).err_x-=C(m).divisor;}else if(C(m).err_x<=-(int)C(m).divisor){--m->xq;C(m).err_x+=C(m).divisor;}
   if(C(m).err_y>=(int)C(m).divisor){++m->yq;C(m).err_y-=C(m).divisor;}else if(C(m).err_y<=-(int)C(m).divisor){--m->yq;C(m).err_y+=C(m).divisor;}
  }C(m).remaining-=step;
  if(m->type!=ENEMY_GARGOYLE&&scene_enemy_hard(m->xq/256,m->yq/256,entry)){m->xq=ox;m->yq=oy;C(m).err_x=oex;C(m).err_y=oey;C(m).remaining=orm;C(m).plan_valid=0;return;}
 }
 C(m).speed_phase=(uint8_t)phase;
 C(m).expected_x=m->xq;C(m).expected_y=m->yq;
}
static void move_toward(Mob*m,int tx,int ty,unsigned dt,int entry){move_toward_proven(m,tx,ty,dt,entry,0);}
static int replan_needed_raw(const Mob*m){if(m->entry)return !C(m).plan_valid||C(m).plan_wall!=s.wall_version||C(m).expected_x!=m->xq||C(m).expected_y!=m->yq;return C(m).plan_wall!=s.wall_version||C(m).plan_field!=s.field_revision||(!C(m).stalled&&(!C(m).plan_valid||!C(m).remaining||C(m).expected_x!=m->xq||C(m).expected_y!=m->yq));}
static int replan_needed(const Mob*m){unsigned i=(unsigned)(m-s.mob);if(replan_cache_active&&replan_cache[i]>=0){++s.stats.replan_cache_hits;return replan_cache[i];}++s.stats.replan_checks;int result=replan_needed_raw(m);if(result){if(C(m).plan_wall!=s.wall_version)++s.stats.replan_reason_wall;else if(!m->entry&&C(m).plan_field!=s.field_revision)++s.stats.replan_reason_field;else ++s.stats.replan_reason_ray;}if(replan_cache_active)replan_cache[i]=(int8_t)result;return result;}
/* Pending slot bitset with nonempty-word summary. Only event producers and
 * the existing active loop enqueue; idle scheduling never scans capacity. */
static void replan_cancel(unsigned i){unsigned w=i/32;uint32_t bit=1u<<(i%32);s.replan_pending[w]&=~bit;s.replan_grant[w]&=~bit;if(!s.replan_pending[w])s.replan_words&=~(1u<<w);}
static void replan_enqueue(unsigned i){Mob*m=&s.mob[i];if(!m->live||m->type!=ENEMY_DK||m->dying||m->attacking)return;unsigned w=i/32;uint32_t bit=1u<<(i%32);if(!(s.replan_pending[w]&bit)){s.replan_pending[w]|=bit;s.replan_words|=1u<<w;s.replan_generation[i]=m->generation;++s.stats.replan_queue_push;}}
static int replan_granted(unsigned i){return (s.replan_grant[i/32]>>(i%32))&1u;}
static void replan_schedule(void){
 memset(s.replan_grant,0,sizeof s.replan_grant);
 s.stats.replan_step_used=s.stats.replan_step_urgent=0;
 if(!s.replan_words){++s.stats.replan_empty_returns;return;}
 unsigned used=0;
 while(s.replan_words&&used<ENEMY_REPLAN_QUOTA){
  unsigned start=s.replan_cursor,w=start/32;uint32_t bits=s.replan_pending[w]&(~0u<<(start%32));
  if(!bits){unsigned k;for(k=1;k<=32;k++){unsigned nw=(w+k)%32;if(s.replan_words&(1u<<nw)){w=nw;bits=s.replan_pending[w];break;}}}
  unsigned bit=(unsigned)__builtin_ctz(bits),i=w*32+bit;
  replan_cancel(i);++s.stats.replan_queue_pop;++s.stats.replan_scan;
  s.replan_cursor=(uint16_t)((i+1)%ENEMY_CAPACITY);Mob*m=&s.mob[i];
  if(m->live&&m->generation==s.replan_generation[i]&&m->type==ENEMY_DK&&!m->dying&&!m->attacking&&replan_needed(m)){s.replan_grant[w]|=1u<<bit;++used;}
 }
}
static void nav_move(Mob*m,unsigned id,const Player*p,unsigned dt){
 unsigned which=m->type==ENEMY_DK;int x=m->xq/256,y=m->yq/256;
 if(replan_needed(m)){
  int dx=x-p->x,dy=y-p->y;unsigned radius=cfg.attack_radius+ENEMY_REPLAN_NEAR_MARGIN;
  int urgent=(uint32_t)(dx*dx+dy*dy)<=radius*radius;
  if(!replan_granted(id)&&!urgent){
   ++s.stats.replan_deferred;
   /* Wall mismatch or unexpected displacement cannot consume an old ray. */
   if(C(m).plan_valid&&C(m).remaining&&C(m).plan_wall==s.wall_version&&C(m).expected_x==m->xq&&C(m).expected_y==m->yq)move_toward(m,C(m).plan_x,C(m).plan_y,dt,0);
   return;
  }
  if(replan_granted(id)){replan_cancel(id);++s.stats.replan_used;++s.stats.replan_step_used;}
  else{++s.stats.replan_urgent;++s.stats.replan_step_urgent;}
  unsigned total=s.stats.replan_step_used+s.stats.replan_step_urgent;if(total>s.stats.replan_peak)s.stats.replan_peak=total;
  if(C(m).plan_wall!=s.wall_version||(which&&C(m).plan_field!=s.field_revision)){C(m).plan_valid=0;C(m).nav_target=-1;C(m).stalled=0;}
 }
 if(C(m).plan_valid&&C(m).remaining){move_toward(m,C(m).plan_x,C(m).plan_y,dt,0);return;}
 if(C(m).stalled&&C(m).plan_wall==s.wall_version&&(!which||C(m).plan_field==s.field_revision))return;
 C(m).plan_valid=0;C(m).plan_wall=s.wall_version;C(m).plan_field=s.field_revision;
 if(scene_enemy_hard(x,y,0)){C(m).stalled=1;++s.stats.nav_inside;++s.stats.nav_unreachable;return;}
 int tx=which?s.target_x:640,ty=which?s.target_y:70;
 /* At a graph vertex use its shared predecessor, never reselect a rounded
  * equal-cost predecessor and oscillate between two corners. */
 if(C(m).nav_target>=0&&x==s.nodes[C(m).nav_target].x&&y==s.nodes[C(m).nav_target].y){
  int next_node=s.nodes[C(m).nav_target].next[which];C(m).nav_target=(int16_t)next_node;
  if(next_node>=0){move_toward(m,s.nodes[next_node].x,s.nodes[next_node].y,dt,0);++s.stats.nav_reachable;return;}
 }
 if(!route_clear(x,y,tx,ty,0)){
  uint32_t best=NAV_INF;int node=-1;
  for(unsigned i=0;i<s.node_n;i++){NavNode*n=&s.nodes[i];if(n->distance[which]==NAV_INF)continue;unsigned d=nav_length(x-n->x,y-n->y)+n->distance[which];if(d<best&&(x!=n->x||y!=n->y)&&route_clear(x,y,n->x,n->y,0)){best=d;node=(int)i;}}
  if(node<0){C(m).stalled=1;++s.stats.nav_unreachable;return;}tx=s.nodes[node].x;ty=s.nodes[node].y;C(m).nav_target=(int16_t)node;
 }
 move_toward_proven(m,tx,ty,dt,0,1);++s.stats.nav_reachable;
}
static void fire(Mob*m,uint32_t now,const Player*p,const ProjectileSystem*a){if(!s.free_bn||s.stats.bullets+(a?a->active:0)>=PROJECTILE_GLOBAL_CAPACITY){++s.stats.bullet_deferred;return;}int32_t dx=p->xq-m->xq,dy=p->yq-m->yq;unsigned len=root((uint64_t)((int64_t)dx*dx+(int64_t)dy*dy));if(!len)return;unsigned i=s.free_b[--s.free_bn];Bullet*b=&s.bullet[i];*b=(Bullet){.xq=m->xq,.yq=m->yq,.vxq=(int32_t)((int64_t)dx*cfg.bullet_speed*256/len),.vyq=(int32_t)((int64_t)dy*cfg.bullet_speed*256/len),.serial=++s.serial,.id=s.serial,.dense=s.stats.bullets,.live=1};s.active_b[s.stats.bullets++]=(uint16_t)i;++s.stats.version;(void)now;}
/* Unordered intrusive buckets: insert/detach are constant work.
 * Arrow candidates order (impact ascending, bucket row-major ascending,
 * dense descending), preserving G217 ties across buckets as well as within. */
static void bucket_reset(void){
 memset(bucket_rows,0,sizeof bucket_rows);for(unsigned b=0;b<BW*BH;b++)bucket[b]=-1;
 for(unsigned i=0;i<ENEMY_CAPACITY;i++)bucket_of[i]=bucket_prev[i]=next[i]=-1;
}
static void bucket_detach(unsigned i){
 int b=bucket_of[i];if(b<0)return;int p=bucket_prev[i],n=next[i];
 if(p<0)bucket[b]=(int16_t)n;else next[p]=(int16_t)n;
 if(n>=0)bucket_prev[n]=(int16_t)p;
  if(bucket[b]<0)bucket_rows[b/BW]&=~((uint64_t)1<<(b%BW));
 bucket_of[i]=bucket_prev[i]=next[i]=-1;
}
static void bucket_sync(unsigned i){
 Mob*m=&s.mob[i];int x=m->xq/256/32,y=m->yq/256/32;
 int b=m->live&&!m->dying&&x>=0&&x<BW&&y>=0&&y<BH?y*BW+x:-1;
 if(b==bucket_of[i])return;
 bucket_detach(i);if(b<0)return;
 int n=bucket[b];
 bucket_of[i]=(int16_t)b;bucket_prev[i]=-1;next[i]=(int16_t)n;
 bucket[b]=(int16_t)i;bucket_rows[b/BW]|=(uint64_t)1<<(b%BW);
 if(n>=0)bucket_prev[n]=(int16_t)i;
}
static void bucket_build(void){bucket_reset();for(unsigned n=0;n<s.stats.active;n++)bucket_sync(s.active[n]);}
typedef struct {int p[2],d[2],lo[2],hi[2];} PreparedSegment;
static PreparedSegment segment_prepare(int x,int y,int xx,int yy){PreparedSegment q={{x,y},{xx-x,yy-y},{x<xx?x:xx,y<yy?y:yy},{x>xx?x:xx,y>yy?y:yy}};return q;}
static int32_t prepared_impact(const PreparedSegment*q,int l,int t,int r,int b){
 if(q->hi[0]<l||q->lo[0]>r||q->hi[1]<t||q->lo[1]>b){++s.stats.broadphase_reject;return -1;}
 int32_t enter=0,leave=65536;int lo[2]={l,t},hi[2]={r,b};
 for(int k=0;k<2;k++){if(!q->d[k]){if(q->p[k]<lo[k]||q->p[k]>hi[k])return -1;continue;}
  s.stats.divisions+=2;int32_t a=(lo[k]-q->p[k])*65536/q->d[k],z=(hi[k]-q->p[k])*65536/q->d[k];
  if(a>z){int32_t tmp=a;a=z;z=tmp;}if(a>enter)enter=a;if(z<leave)leave=z;if(enter>leave)return -1;
 }return enter<=65536&&leave>=0?enter:-1;
}
static int32_t segment_impact(int x,int y,int xx,int yy,int l,int t,int r,int b){PreparedSegment q=segment_prepare(x,y,xx,yy);return prepared_impact(&q,l,t,r,b);}
static int segment_box(int x,int y,int xx,int yy,int l,int t,int r,int b){return segment_impact(x,y,xx,yy,l,t,r,b)>=0;}
static void arrows_hit(ProjectileSystem*a,uint32_t now){if(!a)return;for(unsigned ai=0;ai<PROJECTILE_CAPACITY;ai++){Projectile*p=&a->slots[ai];if(!p->active)continue;int x=p->collision_xq/256,y=p->collision_yq/256,xx=p->pose.xq/256,yy=p->pose.yq/256;PreparedSegment prepared=segment_prepare(x,y,xx,yy);int l=(x<xx?x:xx)-20,r=(x>xx?x:xx)+20,t=(y<yy?y:yy)-4,b=(y>yy?y:yy)+36;if(l<0)l=0;if(r>1279)r=1279;if(t<0)t=0;if(b>639)b=639;/* One bucket traversal: sorted earliest three candidates. Never truncate
 * crowded buckets; DK blocking and existing hit IDs remain exact. */
int hit[3]={-1,-1,-1};uint32_t distance[3]={UINT32_MAX,UINT32_MAX,UINT32_MAX};
if(l<=r&&t<=b)for(int by=t/32;by<=b/32;by++){uint64_t columns=bucket_rows[by]&(~(uint64_t)0<<(l/32))&(((uint64_t)1<<(r/32+1))-1);while(columns){int bx=__builtin_ctzll(columns);columns&=columns-1;
 /* Swept point against bucket expanded by mob AABB: skip diagonal AABB
  * corner buckets without dropping any possible narrow-phase contact. */
 if(prepared_impact(&prepared,bx*32-16,by*32-32,bx*32+47,by*32+34)<0)continue;
 for(int i=bucket[by*BW+bx];i>=0;i=next[i]){
 Mob*m=&s.mob[i];if(m->dying)continue;uint32_t id=mob_id((unsigned)i);int prior=0;for(unsigned h=0;h<p->hit_count;h++)prior|=p->hit_ids[h]==id;if(prior)continue;
 int mx=m->xq/256,my=m->yq/256;++s.stats.collision_candidates;int32_t impact=prepared_impact(&prepared,mx-16,my-32,mx+16,my+3);if(impact<0)continue;
 for(unsigned k=0;k<3;k++)if((uint32_t)impact<distance[k]||((uint32_t)impact==distance[k]&&(hit[k]<0||bucket_of[i]<bucket_of[hit[k]]||(bucket_of[i]==bucket_of[hit[k]]&&m->dense>s.mob[hit[k]].dense)))){for(unsigned j=2;j>k;j--){hit[j]=hit[j-1];distance[j]=distance[j-1];}hit[k]=i;distance[k]=(uint32_t)impact;break;}
}
}
}
for(unsigned k=0;k<3&&p->active&&hit[k]>=0;k++){Mob*m=&s.mob[hit[k]];p->hit_ids[p->hit_count++]=mob_id((unsigned)hit[k]);if(m->type==ENEMY_DK||p->hit_count>=3){p->active=0;--a->active;++a->culled;++a->version;}if(m->type!=ENEMY_DK)kill(m,now,ENEMY_DAMAGE_ARROW);}
p->collision_xq=p->pose.xq;p->collision_yq=p->pose.yq;if(p->active&&p->pending_cull){p->active=0;--a->active;++a->culled;++a->version;}}}
void enemy_resolve_projectiles(ProjectileSystem*a){if(!a)return;int pending=0;for(unsigned i=0;i<PROJECTILE_CAPACITY;i++)pending|=a->slots[i].active&&a->slots[i].pending_cull;if(pending){arrows_hit(a,s.sim_ms);}}
/* Deadlines use signed modular comparisons; all scheduled deltas < 2^31.
 * Walk phase continues under white/attack, so returning never drifts. */
static void visual_sync(Mob*m){
 VisualCache*v=&V(m);uint8_t group=m->type,frame=v->walk_frame,flags=m->entry?ENEMY_ENTER:0;
 if(m->dying){group=(uint8_t)(ENEMY_SOURCE_DK_RED+m->type);frame=0;flags|=ENEMY_RED;}
 else if(m->white){group=ENEMY_SOURCE_DK_WHITE;frame=0;flags|=ENEMY_WHITE;}
 else if(m->attacking){group=ENEMY_SOURCE_DK_ATTACK;frame=v->attack_frame;flags|=ENEMY_ATTACK;}
 v->group=group;v->frame=frame;v->flags=flags;v->cache=(uint8_t)((flags&(ENEMY_RED|ENEMY_WHITE|ENEMY_ATTACK))?0:1+m->type*2+frame);
}
static void animation_tick(Mob*m,uint32_t now,unsigned dt){
 VisualCache*v=&V(m);(void)dt;
 if(m->dying)return;
 while((int32_t)(now-v->walk_due)>=0){v->walk_due+=cfg.walk_ms;v->walk_frame^=1;}
 if(m->attacking)while(v->attack_frame<2&&(int32_t)(now-v->attack_due)>=0){v->attack_due+=cfg.attack_frame_ms;++v->attack_frame;}
 /* Final active-loop sync observes both animation and state changes. */
}
/* E,NE,N,NW,W,SW,S,SE: diagonals use horizontal primary direction.
 * Half-open rectangles: 200 forward x 150 transverse, never behind. */
static HardRect melee_box(const Player*p){
 static const uint8_t primary[8]={0,0,2,4,4,4,6,0};
 int d=primary[(unsigned)p->dir&7u],x=p->x,y=p->y;
 return d==0?(HardRect){x,y-75,x+200,y+75}:d==4?(HardRect){x-200,y-75,x,y+75}:d==2?(HardRect){x-75,y-200,x+75,y}:(HardRect){x-75,y,x+75,y+200};
}
/* Candidate generation only: damage remains in original active/storm order. */
static void melee_prepare(const Player*p){
 memset(melee_candidates,0,sizeof melee_candidates);
 HardRect q=melee_box(p);int l=q.l,r=q.r-1,t=q.t,b=q.b-1;
 if(r<0||b<0||l>=1280||t>=640)return;
 if(l<0)l=0;
 if(r>1279)r=1279;
 if(t<0)t=0;
 if(b>639)b=639;
 for(int by=t/32;by<=b/32;by++)for(int bx=l/32;bx<=r/32;bx++)for(int i=bucket[by*BW+bx];i>=0;i=next[i])if(s.mob[i].type!=ENEMY_GARGOYLE)melee_candidates[i/32]|=1u<<(i%32);
}
static void enemy_step(const Player*p,ProjectileSystem*a,uint32_t now,unsigned dt,const EnemyStorm*storm,int final_step){memset(replan_cache,-1,sizeof replan_cache);replan_cache_active=1;navigation(p);replan_schedule();int pit_active=scene_enemy_pit_active();expiry_prepare(now);
 if(p->action==ACT_MELEE){if(!s.melee_seen||s.melee_start!=p->action_start){s.melee_start=p->action_start;++s.melee_serial;s.melee_seen=1;}}else s.melee_seen=0;
 int melee_active=0;
 if(p->action==ACT_MELEE){uint32_t duration=runtime_action_duration(ACT_MELEE,(uint32_t)p->dir);
  uint64_t begin=(uint64_t)duration*cfg.melee_window_start_numerator/cfg.melee_window_denominator;
  uint64_t end=(uint64_t)duration*cfg.melee_window_end_numerator/cfg.melee_window_denominator;
  melee_active=p->anim_elapsed_us>=begin&&p->anim_elapsed_us<end;
 }
 if(melee_active)melee_prepare(p);
  for(unsigned n=0;n<s.stats.active;){unsigned i=s.active[n];Mob*m=&s.mob[i];uint8_t old_frame=V(m).frame;animation_tick(m,now,dt);int32_t old_xq=m->xq,old_yq=m->yq;unsigned old_flags=m->entry|(m->attacking<<1)|(m->white<<2);if(m->dying){if(expiry_is_due(EXP_DEATH,i)){remove_mob(i);continue;}++n;continue;}int x=m->xq/256,y=m->yq/256;
 if(storm&&storm->active&&x>=storm->x-6&&x<storm->x+storm->w+6&&y>=storm->y-6&&y<storm->y+storm->h+6)kill(m,now,ENEMY_DAMAGE_STORM);
 if(!m->dying&&melee_active&&(melee_candidates[i/32]&(1u<<(i%32)))&&m->type!=ENEMY_GARGOYLE&&C(m).melee_serial!=s.melee_serial){HardRect q=melee_box(p);if(x>=q.l&&x<q.r&&y>=q.t&&y<q.b){C(m).melee_serial=s.melee_serial;if(!--m->hp)kill(m,now,ENEMY_DAMAGE_MELEE);else{m->white=1;C(m).flash_at=now;expiry_arm(EXP_WHITE,i);}}}
 if(m->dying){++n;continue;}if(m->white&&expiry_is_due(EXP_WHITE,i))m->white=0;
 if(m->type!=ENEMY_DK){int dx=x-640,dy=y-70;if(dx*dx+dy*dy<=cfg.gate_radius*cfg.gate_radius){event(ENEMY_EVENT_GATE_ARRIVAL,m->type,mob_id(i),m->xq,m->yq,now);++s.stats.arrived;++s.stats.gate_arrivals;remove_mob(i);continue;}}
 if(m->type==ENEMY_GARGOYLE){route_move(m,i);if(expiry_is_due(EXP_FIRE,i)){C(m).fire_at=now;expiry_arm(EXP_FIRE,i);fire(m,now,p,a);}}
 else if(m->type==ENEMY_WOLF){route_move(m,i);}
 else if(m->entry){int ready=1;if(replan_needed(m)){if(replan_granted(i)){replan_cancel(i);++s.stats.replan_used;++s.stats.replan_step_used;unsigned total=s.stats.replan_step_used+s.stats.replan_step_urgent;if(total>s.stats.replan_peak)s.stats.replan_peak=total;}else{ready=0;++s.stats.replan_deferred;}}if(ready){move_toward(m,C(m).entry_x,C(m).entry_y,dt,1);if(m->xq==C(m).entry_x*256&&m->yq==C(m).entry_y*256)m->entry=0;}}
 else{int dx=x-p->x,dy=y-p->y;int in_range=dx*dx+dy*dy<=cfg.attack_radius*cfg.attack_radius;
  if(m->attacking){if(expiry_is_due(EXP_ATTACK,i)){
   /* Consume once even out of range. Cooldown remains 900ms from start. */
   m->attacking=0;s.expiry_due[EXP_ATTACK][i/32]&=~(1u<<(i%32));
   if(in_range&&p->action!=ACT_DODGE&&!s.damage_guard){event(ENEMY_EVENT_KNIGHT_ATTACK,m->type,mob_id(i),m->xq,m->yq,now);++s.stats.knighthits;}
   if(!in_range){nav_move(m,i,p,dt);replan_cache[i]=-1;}
  }}
 else if(in_range){if(now-C(m).last_attack>=cfg.attack_cooldown_ms){m->attacking=1;V(m).attack_frame=0;V(m).attack_due=now+cfg.attack_frame_ms;C(m).attack_at=C(m).last_attack=now;expiry_arm(EXP_ATTACK,i);++s.stats.knight_attacks;}}else{nav_move(m,i,p,dt);replan_cache[i]=-1;}}
 if(!pit_active&&m->type!=ENEMY_GARGOYLE)++s.stats.pit_steps_skipped;
  if(pit_active&&m->type!=ENEMY_GARGOYLE&&pit_sweep(x,y,m->xq/256,m->yq/256)){kill(m,now,ENEMY_DAMAGE_PIT);}
  if(m->attacking||m->dying)replan_cancel(i);else if(m->type==ENEMY_DK&&replan_needed(m))replan_enqueue(i);
   visual_sync(m);if(old_xq/8192!=m->xq/8192||old_yq/8192!=m->yq/8192)bucket_sync(i);if(old_frame!=V(m).frame||old_xq!=m->xq||old_yq!=m->yq||old_flags!=(unsigned)(m->entry|(m->attacking<<1)|(m->white<<2)))++s.stats.version;++n;}
 if(final_step){arrows_hit(a,now);}
 for(unsigned n=0;n<s.stats.bullets;){unsigned i=s.active_b[n];Bullet*b=&s.bullet[i];int32_t nx=b->xq+b->vxq*(int32_t)dt/1000,ny=b->yq+b->vyq*(int32_t)dt/1000;int x=b->xq/256,y=b->yq/256,xx=nx/256,yy=ny/256;int hit=p->action!=ACT_DODGE&&segment_box(x,y,xx,yy,p->x-10,p->y-26,p->x+10,p->y+3);if(hit&&cfg.bullet_wall_collision&&!segment_clear(x,y,p->x,p->y,0))hit=0;if(hit&&!s.damage_guard){++s.stats.bullethits;event(ENEMY_EVENT_BULLET_PLAYER_HIT,ENEMY_RED_BULLET,b->id,nx,ny,now);++s.stats.bullet_hits;}if(hit||xx<0||xx>=1280||yy<0||yy>=630||(cfg.bullet_wall_collision&&!segment_clear(x,y,xx,yy,0))){remove_bullet(i);continue;}if(b->xq!=nx||b->yq!=ny)++s.stats.version;b->xq=nx;b->yq=ny;++n;}replan_cache_active=0;}
void enemy_update(const Player*p,ProjectileSystem*a,uint32_t world,int active,const EnemyStorm*storm){
 if(!p){return;}
 uint32_t elapsed=world-s.last_ms;s.last_ms=world;s.event_world_ms=world;
 if(!active){s.remainder_ms=0;if(a)for(unsigned i=0;i<PROJECTILE_CAPACITY;i++)if(a->slots[i].active&&a->slots[i].pending_cull){a->slots[i].active=0;--a->active;++a->culled;++a->version;}return;}
 uint64_t debt=(uint64_t)s.remainder_ms+elapsed;unsigned steps=(unsigned)(debt/ENEMY_STEP_MS);s.remainder_ms=(uint32_t)(debt%ENEMY_STEP_MS);
 if(steps>ENEMY_MAX_STEPS){s.stats.discarded_ms+=(steps-ENEMY_MAX_STEPS)*ENEMY_STEP_MS;steps=ENEMY_MAX_STEPS;}
 if(!steps)return;
  if(!s.stats.active&&!s.stats.bullets){s.sim_ms+=steps*ENEMY_STEP_MS;s.stats.sim_ms=s.sim_ms;s.stats.substeps+=steps;return;}
  for(unsigned k=0;k<steps;k++){s.sim_ms+=ENEMY_STEP_MS;enemy_step(p,a,s.sim_ms,ENEMY_STEP_MS,storm,k+1==steps);++s.stats.substeps;}
  s.stats.sim_ms=s.sim_ms;
}
uint32_t enemy_version(void){return s.stats.version;}
const EnemyStats *enemy_stats(void){return &s.stats;}
void enemy_snapshot(EnemySnapshot*out,uint32_t now){
 if(!out)return;
 if(snapshot_cached&&snapshot_last==out&&snapshot_last_epoch==snapshot_epoch&&snapshot_last_version==s.stats.version){out->world_ms=now;return;}
 out->version=s.stats.version;out->world_ms=now;out->count=out->enemy_count=out->bullet_count=0;
  for(int slot=s.birth_head;slot>=0;slot=s.visual[slot].next){
   unsigned i=(unsigned)slot;Mob*m=&s.mob[i];const VisualCache*v=&s.visual[i];
   out->visibleSprites[out->count++]=(EnemyPose){m->xq,m->yq,(int16_t)(m->xq/256-20),(int16_t)(m->yq/256-40),40,44,mob_id(i),v->serial,m->type,v->group,v->frame,v->flags,v->cache};++out->enemy_count;
  }
 for(unsigned n=0;n<s.stats.bullets;n++){Bullet*b=&s.bullet[s.active_b[n]];out->visibleSprites[out->count++]=(EnemyPose){b->xq,b->yq,(int16_t)(b->xq/256-12),(int16_t)(b->yq/256-10),24,20,b->id,b->serial,ENEMY_RED_BULLET,3,0,0,7};++out->bullet_count;}
 snapshot_last=out;snapshot_last_epoch=snapshot_epoch;snapshot_last_version=s.stats.version;snapshot_cached=1;
}
size_t enemy_save_size(void){return sizeof s;}
int enemy_save(void*dst,size_t size){if(!dst||size<sizeof s)return -1;memcpy(dst,&s,sizeof s);return 0;}
int enemy_restore(const void*src,size_t size){
 if(!src||size<sizeof s)return -1;
 const System*p=(const System*)src;
 if(p->magic!=SAVE_MAGIC||p->stats.active>ENEMY_CAPACITY||p->stats.bullets>ENEMY_BULLET_CAPACITY)return -2;
 /* Validate all descriptors/cursors before committing state. Pointers remain
  * outside the pause blob; route time is represented by step and remaining. */
 for(unsigned n=0;n<p->stats.active;n++){
  unsigned i=p->active[n];if(i>=ENEMY_CAPACITY)return -2;const Mob*m=&p->mob[i];const ColdMob*c=&p->cold[i];
  if(!m->live||m->dense!=n||m->type>ENEMY_GARGOYLE)return -2;
  if(m->type!=ENEMY_DK){
   const PcRoute*r=pc_route_find(m->type==ENEMY_WOLF?PC_ROUTE_WOLF:PC_ROUTE_GARGOYLE,m->side,c->route_entry,c->route_variant,c->route_wall&1u);
   PcRouteCursor cursor;if(!r||pc_route_cursor_init(r,m->route_step+1u,&cursor)||cursor.remaining!=m->route_remaining||c->route_id!=route_number(m->type,m->side,c->route_entry,c->route_variant,c->route_wall&1u)||m->route_step>=r->point_count||m->route_remaining!=r->point_count-1u-m->route_step)return -3;
  }
 }
 {int slot=p->birth_head,prev=-1;unsigned count=0;
  if(p->replan_cursor>=ENEMY_CAPACITY||p->free_n>ENEMY_CAPACITY||p->free_bn>ENEMY_BULLET_CAPACITY)return -2;
  while(slot>=0){if(slot>=ENEMY_CAPACITY||count++>=p->stats.active||!p->mob[slot].live||p->visual[slot].prev!=prev)return -2;prev=slot;slot=p->visual[slot].next;}
  if(count!=p->stats.active||prev!=p->birth_tail)return -2;
 }
 memcpy(&s,src,sizeof s);++snapshot_epoch;
 memset(mob_route,0,sizeof mob_route);
 for(unsigned n=0;n<s.stats.active;n++){unsigned i=s.active[n];Mob*m=&s.mob[i];if(m->type!=ENEMY_DK){const PcRoute*r=pc_route_find(m->type==ENEMY_WOLF?PC_ROUTE_WOLF:PC_ROUTE_GARGOYLE,m->side,C(m).route_entry,C(m).route_variant,C(m).route_wall&1u);(void)pc_route_cursor_init(r,m->route_step+1u,&mob_route[i]);}}
 memset(s.replan_pending,0,sizeof s.replan_pending);memset(s.replan_grant,0,sizeof s.replan_grant);s.replan_words=0;for(unsigned n=0;n<s.stats.active;n++){unsigned i=s.active[n];if(replan_needed(&s.mob[i]))replan_enqueue(i);}
  memset(s.expiry_pending,0,sizeof s.expiry_pending);memset(s.expiry_due,0,sizeof s.expiry_due);memset(s.expiry_valid,0,sizeof s.expiry_valid);
  for(unsigned n=0;n<s.stats.active;n++){unsigned i=s.active[n];Mob*m=&s.mob[i];if(m->dying)expiry_arm(EXP_DEATH,i);else{if(m->white)expiry_arm(EXP_WHITE,i);if(m->type==ENEMY_GARGOYLE)expiry_arm(EXP_FIRE,i);if(m->attacking)expiry_arm(EXP_ATTACK,i);}}
  bucket_build();query_serial=0;memset(seen,0,sizeof seen);return 0;
}
