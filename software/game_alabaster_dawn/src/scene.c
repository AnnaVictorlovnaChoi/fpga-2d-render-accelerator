#include "scene.h"
#include "render_metrics.h"
void scene_metrics_visible(void);
#include "scene_data.h"
#include "runtime.h"
#include "v8_platform.h"
#include "renderer.h"
#include "wave_helper.h"
static int lever_enabled;
static uint32_t built_alpha_mask;
static uint32_t scene_alpha_mask(void);

static int altar_frame=0,altar_hint=0,altar_q5=32;
static int built_altar=-1,built_hint=-1,built_altar_q5=-1;
void scene_flow_altar(int frame_index,int hint,unsigned q5){
 if(altar_frame!=frame_index||altar_hint!=hint||altar_q5!=(int)q5){altar_frame=frame_index;altar_hint=hint;altar_q5=(int)q5;scene_flow_changed();}
}
#include <string.h>
#if defined(__riscv)
static uint64_t scene_clock(void){return clint_getTime(SYSTEM_CLINT_CTRL)/(SYSTEM_CLINT_HZ/1000000u);}
#else
static uint64_t scene_clock(void){return 0;}
#endif
const SceneWall scene_walls[4]={{60,121,380,84,205},{840,121,380,84,205},{60,358,460,84,442},{760,358,460,84,442}};
static const int slots[4][4]={{60,183,440,217},{840,183,1220,217},{60,420,520,454},{760,420,1220,454}};
static const int lever_x[2]={770,860};
/* GPU-owned opaque DDR surface. CPU never dereferences its pixels. */
static uint16_t background[1280*720] __attribute__((aligned(64)));
static uint16_t predragon[1280*630] __attribute__((aligned(64)));
static uint16_t dragonbase[1280*630] __attribute__((aligned(64)));
static SceneStats st;
void scene_lever_enabled(int enabled){lever_enabled=!!enabled;if(!lever_enabled&&st.nearest>=0){st.nearest=-1;scene_flow_changed();}}
void scene_flow_changed(void){++st.version;}
static DragonPose dragon_pose={.halo_frame=-1,.beam_kind=-1};
static int built_body=-1,built_halo=-2;
DragonSurface scene_base(int dragon){return (DragonSurface){dragon?dragonbase:predragon,1280,0,50,1280,630};}
const DragonPose *scene_dragon_pose(void){return &dragon_pose;}
void scene_dragon(const DragonPose*p){if(p){if(p->body_frame!=dragon_pose.body_frame||p->halo_frame!=dragon_pose.halo_frame)++st.version;dragon_pose=*p;}}
static int target[2],active[2],animating[2],frame[2]={-1,-1};
static uint32_t began[2],built_version;
int scene_mechanisms_busy(void){return animating[0]||animating[1];}
static WaveVisual wave_desired,wave_built;
void scene_wave(WaveVisual v){if(memcmp(&wave_desired,&v,sizeof v)){wave_desired=v;scene_flow_changed();}}
WaveVisual scene_wave_built(void){return wave_built;}
uint32_t scene_built_version(void){return built_version;}
static int built_frame[2]={-1,-1},built_target[2],built_near=-1;
static SceneRect damage[16]; static unsigned damages;
static SceneRect pending_damage[3][16];static unsigned pending_count[3];
static void pending_add(unsigned fb,SceneRect r){unsigned*n=&pending_count[fb];SceneRect*a=pending_damage[fb];for(unsigned i=0;i<*n;i++){SceneRect b=a[i];if(r.x<=b.x+b.w&&r.x+r.w>=b.x&&r.y<=b.y+b.h&&r.y+r.h>=b.y){int x=r.x<b.x?r.x:b.x,y=r.y<b.y?r.y:b.y,right=r.x+r.w>b.x+b.w?r.x+r.w:b.x+b.w,bottom=r.y+r.h>b.y+b.h?r.y+r.h:b.y+b.h;a[i]=(SceneRect){x,y,right-x,bottom-y};return;}}if(*n<16)a[(*n)++]=r;else{a[0]=(SceneRect){0,0,1280,720};*n=1;}}
unsigned scene_changes_fb(unsigned fb,SceneRect*out,unsigned cap){if(fb<1||fb>3||!cap)return 0;unsigned k=fb-1,n=pending_count[k];if(n>cap){out[0]=(SceneRect){0,0,1280,720};n=1;}else for(unsigned i=0;i<n;i++)out[i]=pending_damage[k][i];pending_count[k]=0;return n;}
static SceneRect journal[32]; static uint32_t journal_version[32]; static unsigned journal_next;
static int inside(int x,int y,int x0,int y0,int x1,int y1){return x>=x0&&x<x1&&y>=y0&&y<y1;}
int scene_nearest(int x,int y){int best=-1,dist=3601;for(int i=0;i<2;i++){int dx=x-lever_x[i],dy=y-115;int d=dx*dx+dy*dy;if(d<=3600&&d<dist){dist=d;best=i;}}return best;}
int scene_walkable(int x,int y){
 if(altar_frame>=0&&inside(x,y,AD_ALTAR_X-AD_ALTAR_FOOT_HALF_W,AD_ALTAR_Y-AD_ALTAR_FOOT_H,AD_ALTAR_X+AD_ALTAR_FOOT_HALF_W,AD_ALTAR_Y))return 0;
 for(int i=0;i<2;i++)if(inside(x,y,lever_x[i]-12,108,lever_x[i]+12,122))return 0;
 if(active[0])for(int i=0;i<4;i++)if(inside(x,y,slots[i][0],slots[i][1],slots[i][2],slots[i][3]))return 0;
 if(active[1]&&inside(x,y,330,250,950,390))return 0;return 1;
}
int scene_enemy_hard(int x,int y,int entry){
 if(!entry&&(x<60||x>=1220||y<70||y>=580))return 1;
 if(x<0||x>=1280||y<0||y>=630)return 1;
 if(inside(x,y,960,70,1180,130))return 1;
 if(altar_frame>=0&&inside(x,y,AD_ALTAR_X-AD_ALTAR_FOOT_HALF_W,AD_ALTAR_Y-AD_ALTAR_FOOT_H,AD_ALTAR_X+AD_ALTAR_FOOT_HALF_W,AD_ALTAR_Y))return 1;
 for(int i=0;i<2;i++)if(inside(x,y,lever_x[i]-12,108,lever_x[i]+12,122))return 1;
 if(active[0])for(int i=0;i<4;i++){int left=slots[i][0],right=slots[i][2];if(left==60)left=0;if(right==1220)right=1280;if(inside(x,y,left,slots[i][1],right,slots[i][3]))return 1;}
 return 0;
}
int scene_enemy_pit(int x,int y){return active[1]&&inside(x,y,330,250,950,390);}
int scene_enemy_pit_active(void){return active[1]!=0;}
uint32_t scene_enemy_wall_version(void){return (uint32_t)active[0]|((uint32_t)(altar_frame>=0)<<1);}
int scene_init(void){int rc=scene_assets_init();if(rc)return rc;return scene_reset_runtime();}
int scene_reset_runtime(void){built_alpha_mask=0;uint32_t next_version=st.version+1u;memset(&wave_desired,0,sizeof wave_desired);memset(&wave_built,0,sizeof wave_built);lever_enabled=0;altar_frame=0;altar_hint=0;altar_q5=32;built_altar=built_hint=built_altar_q5=-1;memset(began,0,sizeof began);memset(built_target,0,sizeof built_target);memset(&st,0,sizeof st);st.nearest=-1;st.version=next_version;built_version=0;memset(target,0,sizeof target);memset(active,0,sizeof active);memset(animating,0,sizeof animating);frame[0]=frame[1]=-1;built_frame[0]=built_frame[1]=-1;built_near=-1;damages=journal_next=0;memset(pending_count,0,sizeof pending_count);built_body=-1;built_halo=-2;dragon_pose=(DragonPose){.halo_frame=-1,.beam_kind=-1};memset(journal_version,0,sizeof journal_version);return 0;}
void scene_update(uint32_t now,int x,int y){
 int near=lever_enabled?scene_nearest(x,y):-1;if(near!=st.nearest){st.nearest=near;++st.version;}
 for(int i=0;i<2;i++)if(animating[i]){unsigned n=i?11:8,step=(now-began[i])/100u;int f;
 if(step>=n){animating[i]=0;active[i]=target[i];f=target[i]?(int)n-1:-1;}else f=i?(int)step:(target[i]?(int)step:(int)n-1-(int)step);
 if(f!=frame[i]){frame[i]=f;++st.version;if(i)++st.trap_steps;else ++st.wall_steps;}}
}
int scene_interact(uint32_t now,int x,int y){if(!lever_enabled)return 0;int k=scene_nearest(x,y);return k<0?0:scene_toggle((unsigned)k,now,x,y);}
int scene_toggle(unsigned control,uint32_t now,int x,int y){if(!lever_enabled||control>1)return 0;int k=(int)control;
 /* Serialize controls, not rendering: completed other control remains presented. */
 if(animating[0]||animating[1])return 1;
 if(!target[k]){int occupied=k?inside(x,y,330,250,950,390):0;if(!k)for(int i=0;i<4;i++)occupied|=inside(x,y,slots[i][0],slots[i][1],slots[i][2],slots[i][3]);if(occupied){++st.rejected;return 1;}}
 target[k]=!target[k];active[k]=1;animating[k]=1;began[k]=now;frame[k]=k?0:(target[k]?0:7);++st.toggles;++st.version;return 1;
}
static SceneRect intersect(SceneRect a,SceneRect b){int x=a.x>b.x?a.x:b.x,y=a.y>b.y?a.y:b.y,r=a.x+a.w<b.x+b.w?a.x+a.w:b.x+b.w,d=a.y+a.h<b.y+b.h?a.y+a.h:b.y+b.h;return (SceneRect){x,y,r>x?r-x:0,d>y?d-y:0};}
static int blit(const uint16_t *src,int stride,int sx,int sy,SceneRect r){if(!r.w||!r.h)return 0;if(!src)return -1;int rc;do{frame_pipeline_poll();rc=bitblt(GPU_OPERATION_COPY,(u32)(uintptr_t)(src+sy*stride+sx),(u32)(uintptr_t)(background+r.y*1280+r.x),(u32)r.w,(u32)r.h,(u32)stride*2,2560,0);if(rc==-2){int ready=gpu_wait_fifo_ready();if(ready)return ready;}}while(rc==-2);if(rc)return rc;
 /* Real completion fence: multi-core commands may otherwise execute overlapping
  * opaque restore/layers out of order. No sleep or VSYNC pacing. */
 return dragon_gpu_fence();}
static int layer(SceneRect dirty,const uint16_t *src,SceneRect full){SceneRect r=intersect(dirty,full);return blit(src,full.w,r.x-full.x,r.y-full.y,r);}
static SceneRect wave_rects[COUNTDOWN_INSTANCE_COUNT][16],wave_bounds[COUNTDOWN_INSTANCE_COUNT];
static int wave_geometry_ready;
static uint32_t wave_rect_recomputes;
static const CountdownFrame *wave_sources[COUNTDOWN_INSTANCE_COUNT][16];
uint32_t scene_countdown_rect_recomputes(void){return wave_rect_recomputes;}
static SceneRect wave_rect_compute(unsigned icon,int seq){
 unsigned slot=seq<0?0:countdown_scale_slots[icon][seq];
 const CountdownFrame*f=countdown_frame(seq<0?COUNTDOWN_BASE:COUNTDOWN_SCALE,seq<0?icon:slot);
 wave_sources[icon][seq+1]=f;wave_rect_recomputes++;
 if(!f)return (SceneRect){0};
 int x=countdown_screen_positions[icon][0],y=countdown_screen_positions[icon][1];
 if(seq>=0){x+=countdown_slot_dxdy[slot][0];y+=countdown_slot_dxdy[slot][1];}
 return (SceneRect){dragon_gpu_origin(x*256,f->anchor_x_q8),dragon_gpu_origin(y*256,f->anchor_y_q8),f->w,f->h};
}
static void wave_geometry_init(void){
 if(wave_geometry_ready)return;
 for(unsigned icon=0;icon<COUNTDOWN_INSTANCE_COUNT;icon++){
  SceneRect u={0};
  for(int seq=-1;seq<15;seq++){
   SceneRect r=wave_rect_compute(icon,seq);wave_rects[icon][seq+1]=r;
   if(!u.w){u=r;continue;}
   int right=u.x+u.w>r.x+r.w?u.x+u.w:r.x+r.w,bottom=u.y+u.h>r.y+r.h?u.y+u.h:r.y+r.h;
   if(r.x<u.x)u.x=r.x;if(r.y<u.y)u.y=r.y;u.w=right-u.x;u.h=bottom-u.y;
  }wave_bounds[icon]=u;
 }wave_geometry_ready=1;
}
static SceneRect wave_rect(unsigned icon,int seq){return wave_rects[icon][seq+1];}
int scene_countdown_overlay(DragonSurface surface,SceneRect dirty,WaveVisual visual){
 if(!visual.visible||!visual.remaining)return 0;
 dirty=intersect(dirty,(SceneRect){surface.x,surface.y,surface.w,surface.h});
 if(dirty.w<=0||dirty.h<=0)return 0;
 DragonSurface dst={surface.pixels+(dirty.y-surface.y)*surface.stride+dirty.x-surface.x,surface.stride,dirty.x,dirty.y,dirty.w,dirty.h};
 for(unsigned icon=0;icon<COUNTDOWN_INSTANCE_COUNT;icon++)for(int seq=-1;seq<15;seq++){
  unsigned gone=15u-visual.remaining;if(seq>=0&&(unsigned)seq<gone)continue;
  SceneRect r=intersect(dirty,wave_rect(icon,seq));if(!r.w||!r.h)continue;
  unsigned q=seq>=0&&(unsigned)seq==gone?visual.alpha_q5:32u;if(!q)continue;
  SceneRect origin=wave_rect(icon,seq);
  int rc=renderer_countdown_draw_resolved(dst,wave_sources[icon][seq+1],origin.x,origin.y,q);if(rc)return rc;
 }
 return 0;
}
static int compose(SceneRect r){int rc=blit(runtime_map(),1280,r.x,r.y,r);if(rc)return rc;
 if(altar_frame>=0){rc=renderer_flow_draw_surface((DragonSurface){background+r.y*1280+r.x,1280,r.x,r.y,r.w,r.h},FLOW_ALTAR,(unsigned)altar_frame,AD_ALTAR_X*256,altar_screen_y()*256,(unsigned)altar_q5);if(rc)return rc;}
 if(altar_hint){rc=renderer_flow_draw_surface((DragonSurface){background+r.y*1280+r.x,1280,r.x,r.y,r.w,r.h},FLOW_START_HINT,0,AD_ALTAR_X*256,altar_hint_center_q8(),32);if(rc)return rc;}
 if(frame[1]>=0){unsigned a=target[1]?4:5;rc=layer(r,scene_patch(a,(unsigned)frame[1]),(SceneRect){330,300,620,140});if(rc)return rc;}
 for(unsigned i=0;i<2;i++){rc=layer(r,scene_patch(6+i,(unsigned)target[i]),(SceneRect){lever_x[i]-24,121,48,48});if(rc)return rc;}
 if(st.nearest>=0){rc=layer(r,scene_patch(8,(unsigned)st.nearest),(SceneRect){lever_x[st.nearest]-16,101,32,16});if(rc)return rc;}
 DragonSurface bg={background,1280,0,0,1280,720};DragonClip clip={r.x,r.y,r.w,r.h};
 uint64_t dragon_begin=dragon_gpu_clock();DragonGPUStats ds={0};
 rc=dragon_gpu_copy(scene_base(0),bg,clip,32,0);if(rc)return rc;if((rc=dragon_gpu_fence()))return rc;
 rc=dragon_gpu_copy(scene_base(1),bg,clip,32,0);if(rc)return rc;if((rc=dragon_gpu_fence()))return rc;
 if(dragon_pose.halo_frame>=0){const DragonFrame*f=dragon_frame(DRAGON_HALO,(unsigned)dragon_pose.halo_frame);if(!f)return -1;rc=dragon_gpu_draw(scene_base(1),f,1070*256,115*256,clip,&ds);if(rc)return rc;}
 const DragonFrame*f=dragon_frame(DRAGON_BODY,(unsigned)dragon_pose.body_frame);if(!f)return -1;
 rc=dragon_gpu_draw(scene_base(1),f,1070*256,115*256,clip,&ds);if(rc)return rc;
 dragon_gpu_record(DRAGON_PERF_BODYHALO,dragon_begin,&ds);
 rc=dragon_gpu_copy(bg,scene_base(1),clip,32,0);if(rc)return rc;if((rc=dragon_gpu_fence()))return rc;
 for(unsigned i=0;i<4;i++)if(frame[0]>=0){const SceneWall*w=&scene_walls[i];unsigned n=0;const SceneMaskRect *m=scene_mask_rects(i,(unsigned)frame[0],&n);for(unsigned j=0;j<n;j++){SceneRect area={w->x+m[j].x,w->y+50+m[j].y,m[j].w,m[j].h};SceneRect crop=intersect(r,area);rc=blit(scene_patch(i,(unsigned)frame[0]),w->w,crop.x-w->x,crop.y-w->y-50,crop);if(rc)return rc;}}
 return 0;
}
static void add(SceneRect r){if(damages<16)damage[damages++]=r;else{damage[0]=(SceneRect){0,0,1280,720};damages=1;}}
SceneRect scene_countdown_bounds(unsigned icon){
 return icon<COUNTDOWN_INSTANCE_COUNT?wave_bounds[icon]:(SceneRect){0};
}
/* Metadata only: the top overlay is never baked into clean scene surfaces. */
static unsigned wave_slot_q5(WaveVisual v,unsigned seq){
 if(!v.visible||!v.remaining)return 0;
 unsigned gone=15u-v.remaining;
 return seq<gone?0:seq==gone?v.alpha_q5:32u;
}
static void wave_damage(void){
 int old_visible=wave_built.visible&&wave_built.remaining;
 int new_visible=wave_desired.visible&&wave_desired.remaining;
 for(unsigned icon=0;icon<COUNTDOWN_INSTANCE_COUNT;icon++){
  if(old_visible!=new_visible){add(scene_countdown_bounds(icon));continue;}
  if(!new_visible)continue;
  /* Bounded per-icon union: normally exactly the current 14x14 slot.
   * Compare weights, not time direction: skipped ticks and rewind both work. */
  SceneRect u={0};
  for(unsigned seq=0;seq<15;seq++){
   if(wave_slot_q5(wave_built,seq)==wave_slot_q5(wave_desired,seq))continue;
   SceneRect r=wave_rect(icon,(int)seq);if(!u.w){u=r;continue;}
   int right=u.x+u.w>r.x+r.w?u.x+u.w:r.x+r.w,bottom=u.y+u.h>r.y+r.h?u.y+u.h:r.y+r.h;
   if(r.x<u.x)u.x=r.x;if(r.y<u.y)u.y=r.y;u.w=right-u.x;u.h=bottom-u.y;
  }
  if(u.w)add(u);
 }
}
static int scene_rebuild_private(void);
int scene_rebuild(void){unsigned tracking=render_metrics_suspend();int rc=scene_rebuild_private();render_metrics_resume(tracking);return rc;}
static int scene_rebuild_private(void){wave_geometry_init();if(built_version==st.version)return 0;uint64_t start=scene_clock();damages=0;
 if(!built_version)add((SceneRect){0,0,1280,720});else{
 if(built_altar!=altar_frame||built_hint!=altar_hint||built_altar_q5!=altar_q5){
   /* Cover complete old/new resource support, not a guessed plaque height. */
   for(unsigned g=0;g<2;g++){FlowGroup group=g?FLOW_START_HINT:FLOW_ALTAR;
    for(unsigned n=0;n<flow_frame_count(group);n++){const FlowFrame*f=flow_frame(group,n);if(f)add((SceneRect){dragon_gpu_origin(AD_ALTAR_X*256,f->anchor_x_q8),dragon_gpu_origin(g?altar_hint_center_q8():altar_screen_y()*256,f->anchor_y_q8),f->w,f->h});}}
  }
 if(built_body!=dragon_pose.body_frame||built_halo!=dragon_pose.halo_frame)add((SceneRect){967,50,206,149});
 if(frame[0]!=built_frame[0])for(unsigned i=0;i<4;i++){const SceneWall*w=&scene_walls[i];add((SceneRect){w->x,w->y+50,w->w,w->h});}
 if(frame[1]!=built_frame[1]||target[1]!=built_target[1])add((SceneRect){330,300,620,140});
 for(unsigned i=0;i<2;i++)if(target[i]!=built_target[i])add((SceneRect){lever_x[i]-24,121,48,48});
 if(built_near!=st.nearest){if(built_near>=0)add((SceneRect){lever_x[built_near]-16,101,32,16});if(st.nearest>=0)add((SceneRect){lever_x[st.nearest]-16,101,32,16});}}
 /* Only underlying-scene mutations rebuild clean GPU surfaces. Wave-only
  * updates issue zero GPU commands here, but still invalidate every FB. */
 for(unsigned i=0;i<damages;i++){int rc=compose(damage[i]);if(rc)return rc;}
 if(built_version)wave_damage();
 for(unsigned i=0;i<damages;i++){for(unsigned fb=0;fb<3;fb++)pending_add(fb,damage[i]);journal[journal_next%32]=damage[i];journal_version[journal_next%32]=st.version;++journal_next;}
 wave_built=wave_desired;built_altar=altar_frame;built_hint=altar_hint;built_altar_q5=altar_q5;
 built_body=dragon_pose.body_frame;built_halo=dragon_pose.halo_frame;
 built_version=st.version;built_frame[0]=frame[0];built_frame[1]=frame[1];built_target[0]=target[0];built_target[1]=target[1];built_near=st.nearest;built_alpha_mask=scene_alpha_mask();st.rebuild_us=(uint32_t)(scene_clock()-start);if(st.rebuild_us>st.rebuild_max_us)st.rebuild_max_us=st.rebuild_us;++st.rebuilds;return 0;
}
/* Return conservative journal damage; wrap/too-old consumers recover fully. */
unsigned scene_changes(uint32_t since,SceneRect*out,unsigned cap){if(since==built_version)return 0;unsigned n=0;if(!since||(journal_next>=32&&since<journal_version[journal_next%32])){if(cap)out[0]=(SceneRect){0,0,1280,720};return 1;}for(unsigned i=0;i<32;i++)if(journal_version[i]>since){if(n>=cap){out[0]=(SceneRect){0,0,1280,720};return 1;}out[n++]=journal[i];}return n;}
static unsigned countdown_alpha(const CountdownFrame*f,unsigned global){if(!f)return 0;for(unsigned i=0;i<f->plane_count;i++){const CountdownPlane*p=countdown_plane(f,i);if(p){unsigned q=(p->alpha_q5*global+16u)>>5;if(q&&q<32&&p->w&&p->h)return 1;}}return 0;}
/* Current opaque patches are COPY objects, not native alpha sprites. */
static uint32_t scene_alpha_mask(void){
 DragonClip clip={0,50,1280,630};uint32_t mask=0;
 if(built_body>=0&&renderer_dragon_alpha(dragon_frame(DRAGON_BODY,(unsigned)built_body),1070*256,115*256,clip))mask|=1u<<1;
 if(built_halo>=0&&renderer_dragon_alpha(dragon_frame(DRAGON_HALO,(unsigned)built_halo),1070*256,115*256,clip))mask|=1u<<3;
 if(built_altar>=0&&built_altar_q5>0&&renderer_flow_alpha(FLOW_ALTAR,(unsigned)built_altar,AD_ALTAR_X*256,altar_screen_y()*256,(unsigned)built_altar_q5,clip))mask|=1u<<6;
 if(built_hint>0&&renderer_flow_alpha(FLOW_START_HINT,0,AD_ALTAR_X*256,altar_hint_center_q8(),32,clip))mask|=1u<<7;
 /* Countdown is a game overlay, one logical token per complete icon. */
 if(wave_built.visible&&wave_built.remaining)for(unsigned i=0;i<COUNTDOWN_INSTANCE_COUNT;i++){
  unsigned hit=countdown_alpha(countdown_frame(COUNTDOWN_BASE,i),32),gone=15u-wave_built.remaining;
  for(unsigned seq=gone;seq<15&&!hit;seq++)hit=countdown_alpha(countdown_frame(COUNTDOWN_SCALE,countdown_scale_slots[i][seq]),seq==gone?wave_built.alpha_q5:32);
  if(hit)mask|=1u<<(16+i);
 }
 return mask;
}
uint32_t scene_alpha_objects(void){uint32_t m=built_alpha_mask,n=0;while(m){n+=m&1u;m>>=1;}return n;}
void scene_metrics_visible(void){
 uint32_t alpha=built_alpha_mask;for(unsigned i=0;i<32;i++)if(alpha&(1u<<i))render_metrics_alpha_object(i);
 render_metrics_object(1); /* Dragon body, baked but logically visible. */
 if(built_halo>=0)render_metrics_object(3);
 render_metrics_object(4);render_metrics_object(5); /* Two lever controls. */
 if(built_altar>=0&&built_altar_q5>0)render_metrics_object(6);
 if(built_hint>0)render_metrics_object(7);
 if(built_near>=0)render_metrics_object(8);
 if(built_frame[0]>=0)for(unsigned i=0;i<4;i++)render_metrics_object(9+i);
 if(built_frame[1]>=0)render_metrics_object(13);
 if(wave_built.visible&&wave_built.remaining)for(unsigned i=0;i<COUNTDOWN_INSTANCE_COUNT;i++)render_metrics_object(16+i);
}
const uint16_t *scene_background(void){return background;}
uint32_t scene_version(void){return st.version;}
int scene_wall_frame(void){return built_frame[0];}
const SceneStats *scene_stats(void){return &st;}
void scene_snapshot_save(SceneSnapshot*s){
 for(unsigned i=0;i<2;i++){s->target[i]=target[i];s->active[i]=active[i];s->animating[i]=animating[i];s->frame[i]=frame[i];s->began[i]=began[i];}
 s->wave_desired=wave_desired;s->wave_built=wave_built;s->built_version=built_version;s->built_alpha_mask=built_alpha_mask;s->lever_enabled=lever_enabled;s->altar_frame=altar_frame;s->altar_hint=altar_hint;s->altar_q5=altar_q5;s->dragon=dragon_pose;s->stats=st;
}
void scene_snapshot_restore(const SceneSnapshot*s){
 for(unsigned i=0;i<2;i++){target[i]=s->target[i];active[i]=s->active[i];animating[i]=s->animating[i];frame[i]=s->frame[i];began[i]=s->began[i];}
 wave_desired=s->wave_desired;wave_built=s->wave_built;built_version=s->built_version;built_alpha_mask=s->built_alpha_mask;lever_enabled=s->lever_enabled;altar_frame=s->altar_frame;altar_hint=s->altar_hint;altar_q5=s->altar_q5;dragon_pose=s->dragon;st=s->stats;
 /* No cache invalidation: paused logic did not mutate scene pixels. */
}
