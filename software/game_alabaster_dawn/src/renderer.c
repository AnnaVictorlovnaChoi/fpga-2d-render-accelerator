#include "render_metrics.h"
#include "alpha_support.h"
#include "renderer.h"
#include "hud.h"
static HudState hud_plan;
static DragonClip hud_previous[3],hud_pause_previous;
int renderer_victory(unsigned fb);
#include "runtime.h"
#include "scene.h"
#include "scene_data.h"
#include "v8_platform.h"
#include "player_support.h"
#include <string.h>
static uint64_t render_work,render_wait;
static uint64_t render_wait_total(void){return gpu_wait_ticks_total()/(SYSTEM_CLINT_HZ/1000000u);}
static int timed_fence(void){return dragon_gpu_fence();}
static void timed_guard(unsigned us){uint64_t t=clint_getTime(SYSTEM_CLINT_CTRL);bsp_uDelay(us);gpu_wait_record(t,clint_getTime(SYSTEM_CLINT_CTRL));}
uint64_t renderer_cpu_work_us(void){return render_work;}
uint64_t renderer_wait_us(void){return render_wait;}
static uint16_t player_surface[1280*630] __attribute__((aligned(64)));
static uint16_t storm_surface[1280*630] __attribute__((aligned(64)));
/* Dedicated immutable ending sources; CPU never reads/writes these pixels. */
static uint16_t flow_original[1280*720] __attribute__((aligned(64)));
static uint16_t flow_world[1280*720] __attribute__((aligned(64)));
static uint16_t flow_gray_tile[24*24] __attribute__((aligned(64)));
static int flow_gray_initialized;
static uint16_t pause_original[1280*720] __attribute__((aligned(64)));
static RendererRect pause_previous[PROJECTILE_CAPACITY+2];
static unsigned pause_dirty,pause_fb_version;
static unsigned pause_source_fb;
static uint32_t pause_objects,flow_objects,pause_alpha,flow_alpha,flow_world_alpha;
static unsigned pause_player_alpha,flow_player_alpha;
static unsigned collecting_alpha;
static uint32_t collected_alpha_bits[128],collected_alpha_count;
static void alpha_object(unsigned token){if(collecting_alpha){if(token<4096){uint32_t m=1u<<(token&31);if(!(collected_alpha_bits[token>>5]&m)){collected_alpha_bits[token>>5]|=m;++collected_alpha_count;}}}else render_metrics_alpha_object(token);}
unsigned renderer_dragon_alpha(const DragonFrame*f,int cx,int cy,DragonClip clip){if(!f)return 0;int x=dragon_gpu_origin(cx,f->anchor_x_q8),y=dragon_gpu_origin(cy,f->anchor_y_q8);for(unsigned i=0;i<f->plane_count;i++){const DragonPlane*p=dragon_plane(f,i);if(p&&p->alpha_q5&&p->alpha_q5<32&&alpha_support_dragon(p,clip.x-x-p->x,clip.y-y-p->y,clip.w,clip.h))return 1;}return 0;}
unsigned renderer_flow_alpha(FlowGroup g,unsigned index,int cx,int cy,unsigned global,DragonClip clip){const FlowFrame*f=flow_frame(g,index);if(!f)return 0;int x=dragon_gpu_origin(cx,f->anchor_x_q8),y=dragon_gpu_origin(cy,f->anchor_y_q8);for(unsigned i=0;i<f->plane_count;i++){const FlowPlane*p=flow_plane(f,i);if(p){unsigned q=(p->alpha_q5*global+16u)>>5;if(q&&q<32&&alpha_support_flow(p,clip.x-x-p->x,clip.y-y-p->y,clip.w,clip.h))return 1;}}return 0;}
static unsigned pause_player,flow_player;
static uint8_t pause_wave_seen;
DragonSurface renderer_scratch(int storm){return (DragonSurface){storm?storm_surface:player_surface,1280,0,50,1280,630};}
static DragonSurface framebuffer(unsigned fb){return (DragonSurface){(uint16_t*)(uintptr_t)(fb==1?FRAMEBUFFER_BASE_1:fb==2?FRAMEBUFFER_BASE_2:FRAMEBUFFER_BASE_3),1280,0,0,1280,720};}
static int component_player(unsigned,const uint8_t*,const RuntimeFrame*,int,int,RendererRect*);
static int component_storm(unsigned);

static RendererRect previous[3][PROJECTILE_CAPACITY+2];
static unsigned dirty_count[3],next_arrow;
static unsigned initialized[3],fb_version[3];
static SceneRect scene_damage[32];
/* A scanline is partitioned only by offline support metadata, never pixels. */
static RendererEnemyStats enemy_counters;
static int mask_alpha(int x,int y,int depth,int *end){
 int alpha=0,wall_frame=scene_wall_frame();if(wall_frame<0)return 0;
 for(unsigned k=0;k<4;k++){
  const SceneWall*w=&scene_walls[k];int row=y-w->y-50;
  if(depth>=w->base*256||row<0||row>=w->h||x>=w->x+w->w||*end<=w->x)continue;
  unsigned n=0;const SceneSpan*m=scene_mask_span(k,(unsigned)wall_frame,(unsigned)row,&n);
  for(unsigned j=0;j<n;j++){enemy_counters.wall_span_visits++;int left=w->x+m[j].x,right=left+m[j].w;if(x>=left&&x<right){alpha=1;if(right<*end)*end=right;}else if(x<left&&left<*end)*end=left;}
 }
 return alpha;
}
static RendererStats stats;
static PlayerFrameView player_view;
const RendererEnemyStats*renderer_enemy_stats(void){return &enemy_counters;}
static EnemySnapshot enemy_owned_plan,enemy_pause_plan;
static const EnemySnapshot *enemy_plan=&enemy_owned_plan;
static EnemySnapshot submitted_enemy[3];
static RendererSnapshot submitted_world[3];
static unsigned submitted_valid[3];
static RendererSnapshot pause_world_metadata;
static unsigned pause_metadata_valid;
int renderer_record_submitted(unsigned fb,const RendererSnapshot*w,const EnemySnapshot*e){
 if(fb<1||fb>3||!w||!e||e->count>ENEMY_VISIBLE_CAPACITY||e->enemy_count>ENEMY_CAPACITY||e->bullet_count>ENEMY_BULLET_CAPACITY)return -1;
 memmove(&submitted_enemy[fb-1],e,offsetof(EnemySnapshot,visibleSprites)+(size_t)e->count*sizeof(EnemyPose));
 submitted_world[fb-1]=*w;submitted_valid[fb-1]=1;return 0;
}
/* Fixed 32px tiles: <=40*20 restore runs, independent of pool occupancy. */
static uint64_t enemy_dirty[3][20],enemy_pause_dirty[20];
static int enemy_component(DragonSurface,int,int);
static RendererRect clip_map(RendererRect);
static void wave_touch(RendererRect);
static void damage_tiles(uint64_t*,RendererRect);
/* Nonzero input; split explicitly for RV32 (no 64-bit ctz helper). */
static unsigned tile_first(uint64_t bits){uint32_t lo=(uint32_t)bits;return lo?(unsigned)__builtin_ctz(lo):32u+(unsigned)__builtin_ctz((uint32_t)(bits>>32));}
static unsigned tile_run_end(uint64_t bits,unsigned left){return tile_first(~(bits>>left))+left;}
static void enemy_touch(unsigned fb,RendererRect r){
 r=clip_map(r);if(!r.w||!r.h)return;wave_touch(r);
 damage_tiles(enemy_dirty[fb-1],r);
}
int renderer_enemy_snapshot(const EnemySnapshot*s){
 if(s&&(s->count>ENEMY_VISIBLE_CAPACITY||s->enemy_count>ENEMY_CAPACITY||s->bullet_count>ENEMY_BULLET_CAPACITY))return -1;
 if(s){memmove(&enemy_owned_plan,s,offsetof(EnemySnapshot,visibleSprites)+(size_t)s->count*sizeof(EnemyPose));}else enemy_owned_plan.count=enemy_owned_plan.enemy_count=enemy_owned_plan.bullet_count=0;
 enemy_plan=&enemy_owned_plan;return 0;
}
int renderer_enemy_snapshot_view(const EnemySnapshot*s){
 if(!s)return renderer_enemy_snapshot(0);
 if(s->count>ENEMY_VISIBLE_CAPACITY||s->enemy_count>ENEMY_CAPACITY||s->bullet_count>ENEMY_BULLET_CAPACITY)return -1;
 enemy_plan=s;return 0;
}

/* Round halves away from zero, including negative differences and anchors.
 * Widen BEFORE subtraction; world/anchor may independently be signed int32. */
static int32_t round_q8(int64_t q)
{
    return (int32_t)(q < 0 ? -((-q + 128) / 256) : (q + 128) / 256);
}
static RendererRect clip_map(RendererRect r)
{
    int32_t right = r.x + r.w, bottom = r.y + r.h;
    if (r.x < 0) r.x = 0;
    if (r.y < 50) r.y = 50;
    if (right > 1280) right = 1280;
    if (bottom > 680) bottom = 680;
    r.w = right > r.x ? right - r.x : 0;
    r.h = bottom > r.y ? bottom - r.y : 0;
    return r;
}
static uint8_t wave_mask,wave_seen[3],wave_active;
static uint64_t wave_tiles[20];
static void damage_tiles(uint64_t *tiles,RendererRect r){
 r=clip_map(r);if(!r.w||!r.h)return;
 unsigned left=(unsigned)r.x/32,right=(unsigned)(r.x+r.w-1)/32;
 uint64_t mask=((1ull<<(right-left+1))-1)<<left;
 for(int y=(r.y-50)/32;y<=(r.y+r.h-51)/32;y++)tiles[y]|=mask;
}
static WaveVisual wave_render;
static const RendererSnapshot *wave_plan;
void renderer_countdown_plan(const RendererSnapshot*s){wave_plan=s;hud_capture(&hud_plan);}
static void wave_touch(RendererRect r){
 if(!wave_active)return;
 enemy_counters.bounds_lookups+=COUNTDOWN_INSTANCE_COUNT;
 for(unsigned i=0;i<COUNTDOWN_INSTANCE_COUNT;i++){SceneRect b=scene_countdown_bounds(i);int l=r.x>b.x?r.x:b.x,t=r.y>b.y?r.y:b.y,right=r.x+r.w<b.x+b.w?r.x+r.w:b.x+b.w,bottom=r.y+r.h<b.y+b.h?r.y+r.h:b.y+b.h;if(l<right&&t<bottom){wave_mask|=(uint8_t)(1u<<i);damage_tiles(wave_tiles,(RendererRect){l,t,right-l,bottom-t});}}
}
static void wave_predict(void){
 const RendererSnapshot*s=wave_plan;if(!s)return;
 const RuntimeFrame*f=runtime_get_frame(s->action,s->dir8,s->elapsed_us);
 if(f)wave_touch((RendererRect){round_q8((int64_t)s->world_x_q8-f->anchor_x_q8),50+round_q8((int64_t)s->world_y_q8-f->anchor_y_q8),f->w,f->h});
 for(unsigned i=0;i<s->arrow_count;i++){int32_t x,y;if(projectile_bbox(&s->arrows[i],&x,&y)){const ArrowAsset*a=&arrow_assets[s->arrows[i].sprite];wave_touch((RendererRect){x,y,a->w,a->h});}}
 const DragonPose*d=&s->dragon;
 if(d->beam_kind!=DRAGON_BEAM_NONE){const DragonFrame*b=dragon_frame(d->beam_kind==DRAGON_BEAM_GROW?DRAGON_GROW:DRAGON_LOOP,d->beam_frame);if(b)wave_touch((RendererRect){dragon_gpu_origin(d->beam_x_q8,b->anchor_x_q8),dragon_gpu_origin(d->beam_y_q8+50*256,b->anchor_y_q8),b->w,b->h});}
}
static int renderer_countdown_finish_impl(unsigned fb){
 if(fb<1||fb>3)return -1;
 /* Merge the disjoint tile footprint before overlay. In particular a fully
  * dirty icon becomes one rectangle, not one re-traversal per 32px tile.
  * Consume merged rows so no alpha pixel can be submitted twice. */
 static uint64_t pending[20];memcpy(pending,wave_tiles,sizeof(pending));
 for(unsigned y=0;y<20;y++)while(pending[y]){
  unsigned left=tile_first(pending[y]),x=tile_run_end(pending[y],left);
  uint64_t mask=((1ull<<(x-left))-1)<<left;unsigned end=y+1;
  pending[y]&=~mask;
  while(end<20&&(pending[end]&mask)==mask){pending[end]&=~mask;end++;}
  int bottom=end==20?680:50+(int)end*32;
  int rc=scene_countdown_overlay(framebuffer(fb),(SceneRect){(int)left*32,50+(int)y*32,(int)(x-left)*32,bottom-50-(int)y*32},wave_render);if(rc)return rc;
 }
 wave_seen[fb-1]=wave_render.visible;
 int rc=timed_fence();if(rc)return rc;
 hud_previous[fb-1]=hud_bar_rect(&hud_plan);
 return hud_draw(fb,framebuffer(fb),&hud_plan);
}
static int copy_rect(unsigned fb, RendererRect r)
{
    int rc;
    const uint16_t *map;
    if (r.w <= 0 || r.h <= 0) return 0;
    wave_touch(r);
    map = scene_background();
    if (!map) return -1;
    /* Main is the sole GPU producer. ISR only latches frame state; this
     * private BSS descriptor stays immutable until enqueue succeeds/aborts. */
    static GpuPreparedCopy prepared;
    ++stats.copy_prepare_calls;
    rc = gpu_prepare_copy(&prepared,(u32)(uintptr_t)map,r.x,r.y,r.w,r.h,fb);
    if(rc)return rc;
    frame_pipeline_poll();
    for (;;) {
        ++stats.copy_enqueue_calls;
        rc = gpu_enqueue_copy(&prepared);
        if(rc != -2)break;
        ++stats.fifo_retries;
        rc = gpu_wait_fifo_ready();
        if(rc)return rc;
    }
    if (rc) return rc;
    ++stats.copy_cmds;
    stats.copy_pixels += (uint32_t)r.w * (uint32_t)r.h;
    return 0;
}
/* Only the existing <=24x24 sprite 4us interval. No copy/fill guard,
 * area-derived pacing, runtime alpha or frame-tail sleep. Cache disabled. */
static int sprite(unsigned fb, const uint8_t *pixels, const RuntimeFrame *f,
                  int32_t x, int32_t y, RendererRect *dirty)
{
    int sy, sx;
    if (!pixels) return -1;
    if(dirty==&previous[fb-1][0])return component_player(fb,pixels,f,x,y,dirty);
    for (sy = 0; sy < f->h; sy += 24) {
        for (sx = 0; sx < f->w; sx += 24) {
            RendererRect tile = {x + sx, y + sy,
                f->w - sx < 24 ? f->w - sx : 24,
                f->h - sy < 24 ? f->h - sy : 24};
            RendererRect r = clip_map(tile);
            int rc, ox, oy;
            if (!r.w || !r.h) continue;
            /* Source moves with clipping; destination never moves the anchor. */
            ox = sx + r.x - tile.x;
            oy = sy + r.y - tile.y;
            timed_guard(4);
            do {
                frame_pipeline_poll();
                rc = pic_sprite((u32)(uintptr_t)pixels, f->w, f->h,
                                r.w, r.h, ox, oy, r.x, r.y, fb, 0, 0);
                if (rc == -2) ++stats.fifo_retries;
            } while (rc == -2);
            if (rc) return rc;
            ++stats.sprite_cmds;
            stats.sprite_pixels += (uint32_t)r.w * (uint32_t)r.h;
            /* Include every accepted write even if a later tile errors.
             * This keeps recovery restore conservative, per framebuffer. */
            if (!dirty->w || !dirty->h) *dirty = r;
            else {
                int32_t right = dirty->x + dirty->w;
                int32_t bottom = dirty->y + dirty->h;
                if (r.x + r.w > right) right = r.x + r.w;
                if (r.y + r.h > bottom) bottom = r.y + r.h;
                if (r.x < dirty->x) dirty->x = r.x;
                if (r.y < dirty->y) dirty->y = r.y;
                dirty->w = right - dirty->x;
                dirty->h = bottom - dirty->y;
            }
        }
    }
    return 0;
}
int renderer_ready(void)
{
    u32 s = read_u32(GPU_BASE);
    if (s & GPU_STATUS_ERROR) return -1;
    return (s & GPU_STATUS_DONE) && !(s & GPU_STATUS_BUSY) &&
           (s & GPU_STATUS_EMPTY);
}
static int renderer_restore_impl(unsigned fb)
{
    unsigned i;
    int rc;
    stats = (RendererStats){0};
    stats.framebuffer = fb;
    if (fb < 1 || fb > 3) return -1;
    i = fb - 1;
    wave_mask=0;memset(wave_tiles,0,sizeof(wave_tiles));wave_render=scene_wave_built();
    wave_active=(wave_render.visible&&wave_render.remaining)||wave_seen[0]||wave_seen[1]||wave_seen[2];
    /* No visible or retired countdown on any FB: wave_touch is a no-op.
     * Avoid resolving every enemy (and player/beam) just to discard bounds. */
    if(wave_active){
     wave_predict();
     for(unsigned e=0;e<enemy_plan->count;e++){DragonClip b=enemy_gpu_bounds(&enemy_plan->visibleSprites[e]);wave_touch((RendererRect){b.x,b.y,b.w,b.h});}
    }
    static uint64_t restore[20];memcpy(restore,enemy_dirty[i],sizeof(restore));
    DragonClip hb=hud_previous[i];RendererRect hr={hb.x,hb.y,hb.w,hb.h};wave_touch(hr);damage_tiles(restore,hr);
    if (!initialized[i]) {
        hud_invalidate(fb);
        rc = copy_rect(fb, (RendererRect){0, 0, 640, 720});if(rc)return rc;
        rc = copy_rect(fb, (RendererRect){640, 0, 640, 720});if(rc)return rc;
        initialized[i] = 1;(void)scene_changes_fb(fb,scene_damage,32);
    } else {
        unsigned count=scene_changes_fb(fb,scene_damage,32);
        for(unsigned j=0;j<count;j++){SceneRect q=scene_damage[j];RendererRect r={q.x,q.y,q.w,q.h};wave_touch(r);damage_tiles(restore,r);
         if(r.y<50){hud_invalidate(fb);int h=r.y+r.h<50?r.h:50-r.y;rc=copy_rect(fb,(RendererRect){r.x,r.y,r.w,h});if(rc)return rc;}
         if(r.y+r.h>680){int y=r.y>680?r.y:680;rc=copy_rect(fb,(RendererRect){r.x,y,r.w,r.y+r.h-y});if(rc)return rc;}
        }
        for(unsigned j=0;j<dirty_count[i];j++){wave_touch(previous[i][j]);damage_tiles(restore,previous[i][j]);}
        wave_touch(previous[i][PROJECTILE_CAPACITY+1]);damage_tiles(restore,previous[i][PROJECTILE_CAPACITY+1]);
        /* Tile-aligned restoration and overlay share exactly one footprint.
         * Include predicted dynamic crossings before any writes are issued. */
        if(wave_render.visible||wave_seen[i])for(unsigned y=0;y<20;y++)restore[y]|=wave_tiles[y];
        for(unsigned row=0;row<20;row++)while(restore[row]){
         unsigned left=tile_first(restore[row]),x=tile_run_end(restore[row],left);
         uint64_t mask=((1ull<<(x-left))-1)<<left;unsigned end=row+1;
         restore[row]&=~mask;
         while(end<20&&(restore[end]&mask)==mask){restore[end]&=~mask;end++;}
         int bottom=end==20?680:50+(int)end*32;
         rc=copy_rect(fb,(RendererRect){(int)left*32,50+(int)row*32,(int)(x-left)*32,bottom-50-(int)row*32});if(rc)return rc;
        }
    }
    memset(enemy_dirty[i],0,sizeof(enemy_dirty[i]));
    fb_version[i]=scene_version();
    dirty_count[i]=0;
    for(unsigned j=0;j<PROJECTILE_CAPACITY+2;j++)previous[i][j]=(RendererRect){0};
    return 0;
}
static int renderer_draw_impl(unsigned fb, const RendererSnapshot *snapshot)
{
    const RendererSnapshot *s = &stats.snapshot;
    const RuntimeGroup *g;
    const RuntimeFrame *f;
    int32_t x, y;
    if (fb < 1 || fb > 3 || !snapshot) return -1;
    /* One BSS copy freezes caller state; no duplicate 1.2 KiB stack copy. */
    stats.snapshot = *snapshot;
    stats.framebuffer = fb;
    stats.draw_valid = 0;
    RuntimeVisual visual;
    if(runtime_get_visual(s->action,s->dir8,s->elapsed_us,&visual))return -1;
    g=visual.group;f=visual.frame;
    if (!g || !f || !g->frames || !f->w || !f->h ||
        g->bytes_per_pixel != 2 || runtime_frame_stride(g, f) != (uint32_t)f->w * 2)
        return -1;
    x = round_q8((int64_t)s->world_x_q8 - f->anchor_x_q8);
    y = 50 + round_q8((int64_t)s->world_y_q8 - f->anchor_y_q8);
    stats.resolved_action = s->action;
    stats.resolved_dir = s->dir8;
    stats.resolved_frame = (uint32_t)(f - g->frames);
    stats.source_stride = (uint32_t)f->w * 2;
    stats.dest_x = x;
    stats.dest_y = y;
    stats.clipped = clip_map((RendererRect){x, y, f->w, f->h});
    stats.draw_valid = 1;
    scene_metrics_visible();
    player_view=player_support_visual(&visual);
    next_arrow=0;dirty_count[fb-1]=1;
    int rc=sprite(fb,visual.pixels,f,x,y,&previous[fb-1][0]);
    if(!rc&&stats.clipped.w&&stats.clipped.h)render_metrics_object(0);
    stats.player_cmds=stats.sprite_cmds;stats.player_pixels=stats.sprite_pixels;
    return rc;
}
const RendererStats *renderer_stats(void) { return &stats; }

static int renderer_draw_next_impl(unsigned fb,const RendererSnapshot*s){
 if(fb<1||fb>3||!s||s->arrow_count>PROJECTILE_CAPACITY)return -1;
 if(next_arrow>=s->arrow_count)return 0;
 const ArrowPose*p=&s->arrows[next_arrow++];if(p->sprite>=104)return -1;
 const ArrowAsset*a=&arrow_assets[p->sprite];int32_t x,y;
 if(!projectile_bbox(p,&x,&y))return 1;
 RuntimeFrame f={.w=a->w,.h=a->h};unsigned i=dirty_count[fb-1]++;
 uint32_t cmds=stats.sprite_cmds,px=stats.sprite_pixels;
 int rc=sprite(fb,projectile_pixels(p->sprite),&f,x,y,&previous[fb-1][i]);
 if(!rc&&stats.sprite_cmds>cmds)render_metrics_object(32+next_arrow-1);
  stats.arrow_cmds+=stats.sprite_cmds-cmds;stats.arrow_pixels+=stats.sprite_pixels-px;stats.sprite_fences++;
 return rc?rc:1;
}

/* Occlusion is already emitted exactly once with the player source. */
static int renderer_occlude_impl(unsigned fb){return fb>=1&&fb<=3?component_storm(fb):-1;}

/* Metadata-only run coalescer. Runs are disjoint within each component;
 * delaying a run cannot reorder overlapping RMW. Sources remain immutable.
 * Bounded BSS queue; full capacity flushes a run without losing coverage. */
typedef struct { DragonClip r; unsigned alpha; } MixRun;
static MixRun mix_runs[1280];
static unsigned mix_count;
static DragonSurface mix_dst,mix_src;
static DragonGPUStats *mix_counters;
static void runs_begin(DragonSurface dst,DragonSurface src,DragonGPUStats*counters){mix_count=0;mix_dst=dst;mix_src=src;mix_counters=counters;}
static int runs_flush(unsigned i){unsigned before=mix_counters?mix_counters->commands:0;int rc=dragon_gpu_copy(mix_dst,mix_src,mix_runs[i].r,mix_runs[i].alpha,mix_counters);if(mix_counters){unsigned n=mix_counters->commands-before;if(mix_runs[i].alpha==32)enemy_counters.copy_commands+=n;else enemy_counters.alpha_commands+=n;}if(rc)return rc;mix_runs[i]=mix_runs[--mix_count];return 0;}
static int runs_add(int x,int y,int w,unsigned alpha){
 if(w<=0)return 0;
 for(unsigned i=0;i<mix_count;){MixRun*p=&mix_runs[i];if(p->r.y+p->r.h<y||p->r.h>=24){int rc=runs_flush(i);if(rc)return rc;}else ++i;}
 for(unsigned i=0;i<mix_count;i++){MixRun*p=&mix_runs[i];if(p->alpha==alpha&&p->r.x==x&&p->r.w==w&&p->r.y+p->r.h==y){++p->r.h;return 0;}}
 if(mix_count==1280){int rc=runs_flush(0);if(rc)return rc;}
 mix_runs[mix_count++]=(MixRun){{x,y,w,1},alpha};return 0;
}
static int runs_end(void){while(mix_count){int rc=runs_flush(mix_count-1);if(rc)return rc;}return 0;}
static int component_player(unsigned fb,const uint8_t*pixels,const RuntimeFrame*f,int x,int y,RendererRect*dirty){
 RendererRect r=clip_map((RendererRect){x,y,f->w,f->h});*dirty=r;
 DragonClip clip={r.x,r.y,r.w,r.h};int rc=dragon_gpu_copy(renderer_scratch(0),scene_base(0),clip,32,0);if(rc)return rc;
 if((rc=timed_fence()))return rc;
 for(int yy=0;yy<f->h;yy+=24)for(int xx=0;xx<f->w;xx+=24){
  RendererRect q=clip_map((RendererRect){x+xx,y+yy,f->w-xx<24?f->w-xx:24,f->h-yy<24?f->h-yy:24});if(!q.w||!q.h)continue;
  timed_guard(4);do{frame_pipeline_poll();rc=bitblt(GPU_OPERATION_SPRITE,(u32)(uintptr_t)(pixels+((q.y-y)*f->w+q.x-x)*2),(u32)(uintptr_t)(player_surface+(q.y-50)*1280+q.x),q.w,q.h,f->w*2,2560,0);}while(rc==-2);if(rc)return rc;
  ++stats.sprite_cmds;stats.sprite_pixels+=(uint32_t)q.w*q.h;
 }
 if((rc=timed_fence()))return rc;
 runs_begin(framebuffer(fb),renderer_scratch(0),0);
 for(int yy=r.y;yy<r.y+r.h;yy++){
  unsigned n;const PlayerSpan*m=player_support_view_row(player_view,yy-y,&n);
  for(unsigned j=0;j<n;j++){int xx=x+m[j].x0,right=x+m[j].x1;if(xx<r.x)xx=r.x;if(right>r.x+r.w)right=r.x+r.w;
   while(xx<right){int end=right;int w=mask_alpha(xx,yy,stats.snapshot.world_y_q8,&end);
    rc=runs_add(xx,yy,end-xx,w?16:32);if(rc)return rc;if(w&&end>xx)alpha_object(0);xx=end;
   }
  }
 }
 if((rc=runs_end()))return rc;
 return timed_fence();
}
static int player_interval(int x,int y,int *end){
 if(!stats.draw_valid||y<stats.dest_y||y>=stats.dest_y+(int)player_view.height||x>=stats.clipped.x+stats.clipped.w||*end<=stats.clipped.x)return 0;
 enemy_counters.player_row_queries++;
 unsigned n;const PlayerSpan*m=player_support_view_row(player_view,y-stats.dest_y,&n);int visible=0;
 for(unsigned i=0;i<n;i++){int l=stats.dest_x+m[i].x0,r=stats.dest_x+m[i].x1;if(x>=l&&x<r){visible=1;if(r<*end)*end=r;}else if(x<l&&l<*end)*end=l;}
 return visible;
}
/* G16: bounded bbox classification; no enemy support, owner or pair graph. */
static int enemy_overlap(RendererRect a,RendererRect b){return a.w&&a.h&&b.w&&b.h&&a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h;}
static uint64_t enemy_pending_tiles[20];
static int enemy_hazard(RendererRect r){
 uint64_t mask=((1ull<<((r.x+r.w-1)/32-r.x/32+1))-1)<<(r.x/32);
 int hit=0;for(int y=(r.y-50)/32;y<=(r.y+r.h-51)/32;y++){hit|=(enemy_pending_tiles[y]&mask)!=0;enemy_counters.cpu_ops++;}
 return hit;
}
static void enemy_mark(RendererRect r){
 uint64_t mask=((1ull<<((r.x+r.w-1)/32-r.x/32+1))-1)<<(r.x/32);
 for(int y=(r.y-50)/32;y<=(r.y+r.h-51)/32;y++){enemy_pending_tiles[y]|=mask;enemy_counters.cpu_ops++;}
}
/* Current ENEMY7 payload has only binary Q32 source planes. */
static unsigned enemy_alpha_hit(const EnemyPose*pose,const EnemyGPUResolved*d,unsigned alpha,RendererRect r){
 if(alpha==32||!alpha)return 0;
 unsigned n;const EnemySpan*sp=enemy_gpu_support(pose,&n);
 for(unsigned i=0;i<n;i++){int y=d->bounds.y+sp[i].y;if(y>=r.y&&y<r.y+r.h&&d->bounds.x+sp[i].x0<r.x+r.w&&d->bounds.x+sp[i].x1>r.x)return 1;}
 return 0;
}
static int enemy_component(DragonSurface dst,int with_player,int journal_fb){
 memset(enemy_pending_tiles,0,sizeof(enemy_pending_tiles));
 enemy_counters=(RendererEnemyStats){0};
 uint64_t begin=dragon_gpu_clock(),wait0=render_wait_total();

 uint32_t fences0=enemy_gpu_metrics()->fences;
 static DragonGPUStats commands;commands=(DragonGPUStats){0};
 int rc=enemy_gpu_warm();if(rc)return rc;
 /* Snapshot is already in birth order. Bullets are the independent last layer. */
 for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<enemy_plan->count;i++){
  const EnemyPose*p=&enemy_plan->visibleSprites[i];
  if((p->type==ENEMY_RED_BULLET)!=(pass==1))continue;
  EnemyGPUResolved resolved;if(enemy_gpu_resolve(p,&resolved))return -1;DragonClip b=resolved.bounds;
  RendererRect r=clip_map((RendererRect){b.x,b.y,b.w,b.h});
  enemy_counters.enemy_objects++;enemy_counters.cpu_ops++;
  if(journal_fb)enemy_touch((unsigned)journal_fb,r);

  if(!r.w||!r.h)continue;
  unsigned alpha=32;
  if(!pass){
   int pl=with_player&&stats.draw_valid&&enemy_overlap(r,stats.clipped),wall=0;
   enemy_counters.cpu_ops++;
   if(scene_wall_frame()>=0)for(unsigned k=0;k<4;k++){
    const SceneWall*w=&scene_walls[k];enemy_counters.cpu_ops++;
    if(p->yq<w->base*256&&enemy_overlap(r,(RendererRect){w->x,w->y+50,w->w,w->h}))wall=1;
   }
   alpha=pl&&wall?11:(pl||wall?16:32);
  }

  unsigned before=commands.commands;
  /* Conservative 160-byte tile hazard set; no per-object candidate graph. */
  if(enemy_hazard(r)){if((rc=enemy_gpu_fence()))return rc;memset(enemy_pending_tiles,0,sizeof(enemy_pending_tiles));}
  if((rc=enemy_gpu_draw_resolved(dst,&resolved,(DragonClip){0,50,1280,630},alpha,&commands)))return rc;
  enemy_mark(r);
  unsigned issued=commands.commands-before;
   if(issued&&journal_fb)render_metrics_object(512+i);
   if(issued&&(journal_fb||collecting_alpha)&&enemy_alpha_hit(p,&resolved,alpha,r))alpha_object(512+i);
  if(pass)enemy_counters.bullet_commands+=issued;
  else if(alpha==32){enemy_counters.fast_objects++;enemy_counters.fast_commands+=issued;}
  else {enemy_counters.complex_objects++;enemy_counters.complex_commands+=issued;enemy_counters.alpha_commands+=issued;}
 }
 if(commands.commands&&((rc=enemy_gpu_fence())))return rc;
 enemy_counters.commands=commands.commands;enemy_counters.total_pixels=commands.pixels;
 enemy_counters.sprite_commands=commands.commands-enemy_counters.alpha_commands;
 enemy_counters.total_ticks=dragon_gpu_clock()-begin;
 enemy_counters.wait_us=render_wait_total()-wait0;
 enemy_counters.enqueue_us=enemy_counters.total_ticks-enemy_counters.wait_us;
 enemy_counters.fences=enemy_gpu_metrics()->fences-fences0;
 enemy_counters.fence_ticks=enemy_counters.wait_us;
 enemy_counters.render_cpu_work_us=enemy_counters.total_ticks-enemy_counters.wait_us;
 enemy_counters.render_wait_us=enemy_counters.wait_us;
 enemy_counters.classify_ticks=enemy_counters.plan_touch_ticks+enemy_counters.playerwall_ticks;
 return 0;
}
static int renderer_enemy_draw_impl(unsigned fb){return fb>=1&&fb<=3?enemy_component(framebuffer(fb),1,(int)fb):-1;}
static int component_storm(unsigned fb){
 const DragonPose*p=&stats.snapshot.dragon;if(p->beam_kind==DRAGON_BEAM_NONE)return 0;
 const DragonFrame*f=dragon_frame(p->beam_kind==DRAGON_BEAM_GROW?DRAGON_GROW:DRAGON_LOOP,(unsigned)p->beam_frame);if(!f)return -1;
 int x=dragon_gpu_origin(p->beam_x_q8,f->anchor_x_q8),y=dragon_gpu_origin(p->beam_y_q8+50*256,f->anchor_y_q8);
 RendererRect r=clip_map((RendererRect){x,y,f->w,f->h});if(!r.w||!r.h)return 0;
 /* Reserve one damage rectangle for storm in addition to player/arrows. */
 previous[fb-1][PROJECTILE_CAPACITY+1]=r;
 DragonClip clip={r.x,r.y,r.w,r.h};int rc=dragon_gpu_copy(renderer_scratch(1),scene_base(1),clip,32,0);if(rc)return rc;if((rc=timed_fence()))return rc;
 /* Main character removes body/halo before all independent component weights. */
 runs_begin(renderer_scratch(1),scene_base(0),0);
 for(int yy=r.y;yy<r.y+r.h;yy++)for(int xx=r.x;xx<r.x+r.w;){int end=r.x+r.w;int has=player_interval(xx,yy,&end);if(has){rc=runs_add(xx,yy,end-xx,32);if(rc)return rc;}xx=end;}
 if((rc=runs_end()))return rc;
 if((rc=timed_fence()))return rc;
 uint64_t beam_begin=dragon_gpu_clock();DragonGPUStats beam_stats={0};
 rc=dragon_gpu_draw(renderer_scratch(1),f,p->beam_x_q8,p->beam_y_q8+50*256,clip,&beam_stats);if(rc)return rc;
 dragon_gpu_record(DRAGON_PERF_BEAM,beam_begin,&beam_stats);
 /* Storm never blends with enemies: reset only actual beam support to W/P. */
 unsigned beam_n;const DragonSpan*beam_sp=dragon_support(f,&beam_n);
 runs_begin(framebuffer(fb),(DragonSurface){(uint16_t*)(uintptr_t)scene_background(),1280,0,0,1280,720},0);
 for(unsigned j=0;j<beam_n;j++){int yy=y+beam_sp[j].y,l=x+beam_sp[j].x0,rr=x+beam_sp[j].x1;if(yy<r.y||yy>=r.y+r.h)continue;if(l<r.x)l=r.x;if(rr>r.x+r.w)rr=r.x+r.w;if((rc=runs_add(l,yy,rr-l,32)))return rc;}
 if((rc=runs_end()))return rc;if((rc=timed_fence()))return rc;
 runs_begin(framebuffer(fb),renderer_scratch(0),0);
 for(unsigned j=0;j<beam_n;j++){int yy=y+beam_sp[j].y,l=x+beam_sp[j].x0,rr=x+beam_sp[j].x1;if(yy<r.y||yy>=r.y+r.h)continue;if(l<r.x)l=r.x;if(rr>r.x+r.w)rr=r.x+r.w;while(l<rr){int end=rr,pl=player_interval(l,yy,&end),wall=mask_alpha(l,yy,stats.snapshot.world_y_q8,&end);if(pl&&(rc=runs_add(l,yy,end-l,wall?16:32)))return rc;if(pl&&wall&&end>l)alpha_object(0);l=end;}}
 if((rc=runs_end()))return rc;if((rc=timed_fence()))return rc;
 uint64_t mix_begin=dragon_gpu_clock();DragonGPUStats mix_stats={0};
 runs_begin(framebuffer(fb),renderer_scratch(1),&mix_stats);
 unsigned n;const DragonSpan*m=dragon_support(f,&n);
 for(unsigned j=0;j<n;j++){int yy=y+m[j].y;if(yy<r.y||yy>=r.y+r.h)continue;int xx=x+m[j].x0,right=x+m[j].x1;if(xx<r.x)xx=r.x;if(right>r.x+r.w)right=r.x+r.w;
  while(xx<right){int end=right;int pl=player_interval(xx,yy,&end);int w=mask_alpha(xx,yy,pl?stats.snapshot.world_y_q8:-2147483647,&end);
   unsigned alpha=w&&pl?11:(w||pl?16:32);
   rc=runs_add(xx,yy,end-xx,alpha);if(rc)return rc;if(alpha<32&&end>xx)alpha_object(2);xx=end;
  }
 }
 if((rc=runs_end()))return rc;
 int done=timed_fence();if(!done&&mix_stats.commands){render_metrics_object(2);if(renderer_dragon_alpha(f,p->beam_x_q8,p->beam_y_q8+50*256,clip))alpha_object(2);}if(!done)dragon_gpu_record(DRAGON_PERF_MIX,mix_begin,&mix_stats);return done;
}

static int renderer_pause_capture_private(unsigned fb);
int renderer_pause_capture(unsigned fb){unsigned tracking=render_metrics_suspend();int rc=renderer_pause_capture_private(fb);render_metrics_resume(tracking);return rc;}
static int renderer_pause_capture_private(unsigned fb){
 if(fb<1||fb>3)return -1;pause_alpha=render_metrics_completed().alpha_objects;pause_player_alpha=render_metrics_completed_player_alpha();pause_player=render_metrics_completed_has_player();pause_objects=render_metrics_completed().visible_objects;pause_source_fb=fb;pause_wave_seen=wave_seen[fb-1];const EnemySnapshot*captured=submitted_valid[fb-1]?&submitted_enemy[fb-1]:enemy_plan;memmove(&enemy_pause_plan,captured,offsetof(EnemySnapshot,visibleSprites)+(size_t)captured->count*sizeof(EnemyPose));
 pause_metadata_valid=submitted_valid[fb-1];if(pause_metadata_valid)pause_world_metadata=submitted_world[fb-1];
 for(unsigned i=0;i<PROJECTILE_CAPACITY+2;i++)pause_previous[i]=previous[fb-1][i];
 for(unsigned i=0;i<20;i++)enemy_pause_dirty[i]=enemy_dirty[fb-1][i];
 pause_dirty=dirty_count[fb-1];pause_fb_version=fb_version[fb-1];
 hud_pause_capture(fb);hud_pause_previous=hud_previous[fb-1];
 int rc=dragon_gpu_copy((DragonSurface){pause_original,1280,0,0,1280,720},framebuffer(fb),(DragonClip){0,0,1280,720},32,0);
 return rc?rc:timed_fence();
}
int renderer_pause_restore(unsigned fb){
 if(fb<1||fb>3)return -1;
 render_metrics_frozen(pause_objects);render_metrics_frozen_alpha(pause_alpha);render_metrics_frozen_player_alpha(pause_player_alpha);render_metrics_frozen_player(pause_player);
 int rc=dragon_gpu_copy(framebuffer(fb),(DragonSurface){pause_original,1280,0,0,1280,720},(DragonClip){0,0,1280,720},32,0);if(rc)return rc;
 if((rc=timed_fence()))return rc;
 /* Pixel copies carry their captured metadata even outside normal phase5.
  * Otherwise resume->ending before the next world render sees stale FB data. */
 if(pause_metadata_valid){if((rc=renderer_record_submitted(fb,&pause_world_metadata,&enemy_pause_plan)))return rc;}
 else submitted_valid[fb-1]=0;
 for(unsigned i=0;i<PROJECTILE_CAPACITY+2;i++)previous[fb-1][i]=pause_previous[i];
 for(unsigned i=0;i<20;i++)enemy_dirty[fb-1][i]=enemy_pause_dirty[i];
 hud_pause_restore(fb);hud_previous[fb-1]=hud_pause_previous;
 wave_seen[fb-1]=pause_wave_seen;dirty_count[fb-1]=pause_dirty;fb_version[fb-1]=pause_fb_version;initialized[fb-1]=1;
 (void)scene_changes_fb(fb,scene_damage,32);return hud_draw_stats(framebuffer(fb));
}
int renderer_pause_show(unsigned fb){
 int rc=renderer_pause_restore(fb);if(rc)return rc;
 if((rc=renderer_flow_sprite(fb,FLOW_PAUSE,0,640*256,360*256,32)))return rc;
 /* Overlay target may be reused before normal history reaches it. Include
  * complete plaque bbox in this target's restore journal (not global scene). */
 const FlowFrame*f=flow_frame(FLOW_PAUSE,0);if(!f)return -1;
 RendererRect r={dragon_gpu_origin(640*256,f->anchor_x_q8),dragon_gpu_origin(360*256,f->anchor_y_q8),f->w,f->h};
 RendererRect*b=&previous[fb-1][PROJECTILE_CAPACITY+1];
 if(!b->w||!b->h)*b=r;else{int l=b->x<r.x?b->x:r.x,t=b->y<r.y?b->y:r.y,rr=b->x+b->w>r.x+r.w?b->x+b->w:r.x+r.w,bb=b->y+b->h>r.y+r.h?b->y+b->h:r.y+r.h;*b=(RendererRect){l,t,rr-l,bb-t};}
 return timed_fence();
}
static int flow_freeze_source(DragonSurface source,const RendererSnapshot*s,uint32_t objects);
int renderer_pause_end_freeze(const RendererSnapshot*s){
 /* Pause UI may have rotated all three FBs. Never write the old source FB. */
 const EnemySnapshot*working=enemy_plan;enemy_plan=&enemy_pause_plan;
 flow_player=pause_player;flow_alpha=pause_alpha;flow_player_alpha=pause_player_alpha;
 int rc=flow_freeze_source((DragonSurface){pause_original,1280,0,0,1280,720},s,pause_objects);
 enemy_plan=working;return rc;
}
DragonSurface renderer_flow_snapshot(int without_player) {
 return (DragonSurface){without_player?flow_world:flow_original,1280,0,0,1280,720};
}
int renderer_victory(unsigned fb){if(fb<1||fb>3)return -1;int rc=hud_victory(framebuffer(fb));if(!rc){render_metrics_object(3000);rc=hud_draw_stats(framebuffer(fb));}return rc;}
int renderer_flow_restore(unsigned fb,int without_player) {
 if(fb<1||fb>3)return -1;
 render_metrics_frozen(flow_objects-(without_player&&flow_player&&flow_objects?1u:0u));render_metrics_frozen_player(!without_player&&flow_player);render_metrics_frozen_alpha(without_player?flow_world_alpha:flow_alpha);render_metrics_frozen_player_alpha(!without_player&&flow_player_alpha);
  int rc=dragon_gpu_copy(framebuffer(fb),renderer_flow_snapshot(without_player),(DragonClip){0,0,1280,720},32,0);if(rc)return rc;if((rc=timed_fence()))return rc;return hud_draw_stats(framebuffer(fb));
}
static int flow_gray_surface(DragonSurface dst,unsigned q5){
 if(q5>32)return -1;if(!q5)return 0;int rc;
 if(!flow_gray_initialized){do{frame_pipeline_poll();rc=bitblt(GPU_OPERATION_COLOR,0,(u32)(uintptr_t)flow_gray_tile,24,24,0,48,(16u<<11)|(32u<<5)|16u);}while(rc==-2);if(rc)return rc;if((rc=timed_fence()))return rc;flow_gray_initialized=1;}
 for(int y=0;y<720;y+=24)for(int x=0;x<1280;x+=24){int w=1280-x<24?1280-x:24,h=720-y<24?720-y:24;timed_guard(4);do{frame_pipeline_poll();rc=bitblt(GPU_OPERATION_ALPHA,(u32)(uintptr_t)flow_gray_tile,(u32)(uintptr_t)(dst.pixels+y*1280+x),w,h,48,2560,q5);}while(rc==-2);if(rc)return rc;}
 return timed_fence();
}
int renderer_flow_gray(unsigned fb,unsigned q5){if(fb<1||fb>3)return -1;int rc=flow_gray_surface(framebuffer(fb),q5);return rc?rc:hud_draw_stats(framebuffer(fb));}
static unsigned flow_death_fb[3];
static int flow_world_gray;
int renderer_flow_death_restore(unsigned fb,const RendererSnapshot*s){
 if(fb<1||fb>3||!s)return -1;int rc;
  render_metrics_frozen(flow_objects-(flow_player&&flow_objects?1u:0u));render_metrics_frozen_player(0);render_metrics_frozen_alpha(flow_world_alpha);render_metrics_frozen_player_alpha(0);
 if(!flow_world_gray){unsigned tracking=render_metrics_suspend();rc=flow_gray_surface(renderer_flow_snapshot(1),20);render_metrics_resume(tracking);if(rc)return rc;flow_world_gray=1;}
 DragonClip r={0,0,1280,720};
 if(flow_death_fb[fb-1]){r=(DragonClip){s->world_x_q8/256-48,s->world_y_q8/256+50-130,96,150};}
 if((rc=dragon_gpu_copy(framebuffer(fb),renderer_flow_snapshot(1),r,32,0)))return rc;
 flow_death_fb[fb-1]=1;if((rc=timed_fence()))return rc;return hud_draw_stats(framebuffer(fb));
}
/* Small sparse offline planes only; no image scanning or CPU pixel synthesis. */
int renderer_flow_draw_surface(DragonSurface dst,FlowGroup group,unsigned index,int cx,int cy,unsigned global) {
 const FlowFrame*f=flow_frame(group,index);if(!f||global>32)return -1;
 int x=dragon_gpu_origin(cx,f->anchor_x_q8),y=dragon_gpu_origin(cy,f->anchor_y_q8);unsigned prev=0;
 for(unsigned i=0;i<f->plane_count;i++){
  const FlowPlane*p=flow_plane(f,i);if(!p||p->w>24||p->h>24)return -1;
  unsigned a=(p->alpha_q5*global+16u)>>5;if(!a)continue;
  int l=x+p->x,t=y+p->y,r=l+p->w,b=t+p->h;
  if(l<dst.x)l=dst.x;if(t<dst.y)t=dst.y;if(r>dst.x+dst.w)r=dst.x+dst.w;if(b>dst.y+dst.h)b=dst.y+dst.h;if(r<=l||b<=t)continue;
  if(prev&&prev!=a){int rc=timed_fence();if(rc)return rc;}prev=a;
  const uint16_t*src=flow_pixels(p);if(!src)return -1;
  timed_guard(4);int rc;do{frame_pipeline_poll();rc=bitblt(a==32?GPU_OPERATION_SPRITE:GPU_OPERATION_ALPHASPRITE,(u32)(uintptr_t)(src+(t-y-p->y)*p->w+l-x-p->x),(u32)(uintptr_t)(dst.pixels+(t-dst.y)*dst.stride+l-dst.x),r-l,b-t,p->w*2,dst.stride*2,a==32?0:a);}while(rc==-2);if(rc)return rc;
 }
 return timed_fence();
}
/* Kind6 countdown uses the same <=24px direct Q5 sparse GPU commands. */
int renderer_countdown_draw_surface(DragonSurface dst,CountdownGroup group,unsigned index,int cx,int cy,unsigned global) {
 const CountdownFrame*f=countdown_frame(group,index);if(!f||global>32)return -1;
 return renderer_countdown_draw_resolved(dst,f,dragon_gpu_origin(cx,f->anchor_x_q8),dragon_gpu_origin(cy,f->anchor_y_q8),global);
}
int renderer_countdown_draw_resolved(DragonSurface dst,const CountdownFrame*f,int x,int y,unsigned global){
 if(!f||global>32)return -1;unsigned prev=0;
 for(unsigned i=0;i<f->plane_count;i++){
  const CountdownPlane*p=countdown_plane(f,i);if(!p||p->w>24||p->h>24)return -1;
  unsigned a=(p->alpha_q5*global+16u)>>5;if(!a)continue;
  int l=x+p->x,t=y+p->y,r=l+p->w,b=t+p->h;
  if(l<dst.x)l=dst.x;if(t<dst.y)t=dst.y;if(r>dst.x+dst.w)r=dst.x+dst.w;if(b>dst.y+dst.h)b=dst.y+dst.h;if(r<=l||b<=t)continue;
  if(prev&&prev!=a){int rc=timed_fence();if(rc)return rc;}prev=a;
  const uint16_t*src=countdown_pixels(p);if(!src)return -1;
  timed_guard(4);int rc;do{frame_pipeline_poll();rc=bitblt(a==32?GPU_OPERATION_SPRITE:GPU_OPERATION_ALPHASPRITE,(u32)(uintptr_t)(src+(t-y-p->y)*p->w+l-x-p->x),(u32)(uintptr_t)(dst.pixels+(t-dst.y)*dst.stride+l-dst.x),r-l,b-t,p->w*2,dst.stride*2,a==32?0:a);}while(rc==-2);if(rc)return rc;
 }
 return timed_fence();
}
int renderer_flow_sprite(unsigned fb,FlowGroup g,unsigned index,int x,int y,unsigned global){if(fb<1||fb>3)return -1;int rc=renderer_flow_draw_surface(framebuffer(fb),g,index,x,y,global);const FlowFrame*f=flow_frame(g,index);if(!rc&&global&&f){int l=dragon_gpu_origin(x,f->anchor_x_q8),t=dragon_gpu_origin(y,f->anchor_y_q8);if(l<1280&&t<720&&l+f->w>0&&t+f->h>0){render_metrics_object(3001+(unsigned)g);if(renderer_flow_alpha(g,index,x,y,global,(DragonClip){0,0,1280,720}))alpha_object(3001+(unsigned)g);}}return rc;}
int renderer_flow_freeze(unsigned fb,const RendererSnapshot*s){
 if(fb<1||fb>3||!s)return -1;
 flow_player=render_metrics_completed_has_player();flow_alpha=render_metrics_completed().alpha_objects;flow_player_alpha=render_metrics_completed_player_alpha();
 const EnemySnapshot*working=enemy_plan;
 if(submitted_valid[fb-1]){enemy_plan=&submitted_enemy[fb-1];s=&submitted_world[fb-1];}
 int rc=flow_freeze_source(framebuffer(fb),s,render_metrics_completed().visible_objects);
 enemy_plan=working;return rc;
}
static int flow_freeze_source_private(DragonSurface source,const RendererSnapshot*s,uint32_t objects);
static int flow_freeze_source(DragonSurface source,const RendererSnapshot*s,uint32_t objects){unsigned tracking=render_metrics_suspend();memset(collected_alpha_bits,0,sizeof collected_alpha_bits);collected_alpha_count=0;collecting_alpha=1;int rc=flow_freeze_source_private(source,s,objects);collecting_alpha=0;if(!rc)flow_world_alpha=collected_alpha_count+scene_alpha_objects();render_metrics_resume(tracking);return rc;}
static int flow_freeze_source_private(DragonSurface source,const RendererSnapshot*s,uint32_t objects){
 if(!s)return -1;
 flow_objects=objects;
  flow_world_gray=0;flow_death_fb[0]=flow_death_fb[1]=flow_death_fb[2]=0;
 int rc=dragon_gpu_copy(renderer_flow_snapshot(0),source,(DragonClip){0,0,1280,720},32,0);if(rc)return rc;if((rc=timed_fence()))return rc;
 DragonSurface world=renderer_flow_snapshot(1);
 DragonSurface base={(uint16_t*)(uintptr_t)scene_background(),1280,0,0,1280,720};
 if((rc=dragon_gpu_copy(world,base,(DragonClip){0,0,1280,720},32,0)))return rc;if((rc=timed_fence()))return rc;
 /* Rebuild the frozen world without P. Body/halo remain in scene base.
  * Beam world-only composition has no player support, hence no P contribution. */
 if((rc=enemy_component(world,0,0)))return rc;
 const DragonPose*p=&s->dragon;
 if(p->beam_kind!=DRAGON_BEAM_NONE){const DragonFrame*f=dragon_frame(p->beam_kind==DRAGON_BEAM_GROW?DRAGON_GROW:DRAGON_LOOP,p->beam_frame);if(!f)return -1;
  int bx=dragon_gpu_origin(p->beam_x_q8,f->anchor_x_q8),by=dragon_gpu_origin(p->beam_y_q8+50*256,f->anchor_y_q8);
  RendererRect br=clip_map((RendererRect){bx,by,f->w,f->h});DragonClip clip={br.x,br.y,br.w,br.h};
  if((rc=dragon_gpu_copy(renderer_scratch(1),scene_base(1),clip,32,0)))return rc;if((rc=timed_fence()))return rc;
  if((rc=dragon_gpu_draw(renderer_scratch(1),f,p->beam_x_q8,p->beam_y_q8+50*256,clip,0)))return rc;
  unsigned count;const DragonSpan*sp=dragon_support(f,&count);
  runs_begin(world,(DragonSurface){(uint16_t*)(uintptr_t)scene_background(),1280,0,0,1280,720},0);
  for(unsigned j=0;j<count;j++){int yy=by+sp[j].y,l=bx+sp[j].x0,rr=bx+sp[j].x1;if(yy<br.y||yy>=br.y+br.h)continue;if(l<br.x)l=br.x;if(rr>br.x+br.w)rr=br.x+br.w;if((rc=runs_add(l,yy,rr-l,32)))return rc;}
  if((rc=runs_end()))return rc;if((rc=timed_fence()))return rc;
  runs_begin(world,renderer_scratch(1),0);
  for(unsigned j=0;j<count;j++){int yy=by+sp[j].y;if(yy<br.y||yy>=br.y+br.h)continue;int xx=bx+sp[j].x0,right=bx+sp[j].x1;if(xx<br.x)xx=br.x;if(right>br.x+br.w)right=br.x+br.w;
   while(xx<right){int end=right;int wall=mask_alpha(xx,yy,-2147483647,&end);if((rc=runs_add(xx,yy,end-xx,wall?16:32)))return rc;if(wall&&end>xx)alpha_object(2);xx=end;}}
  if((rc=runs_end()))return rc;if((rc=timed_fence()))return rc;if(renderer_dragon_alpha(f,p->beam_x_q8,p->beam_y_q8+50*256,clip))alpha_object(2);}
 for(unsigned i=0;i<s->arrow_count;i++){
  const ArrowPose*p=&s->arrows[i];int32_t x,y;if(p->sprite>=104)return -1;if(!projectile_bbox(p,&x,&y))continue;
  const ArrowAsset*a=&arrow_assets[p->sprite];const uint8_t*src=projectile_pixels(p->sprite);
  for(int yy=0;yy<a->h;yy+=24)for(int xx=0;xx<a->w;xx+=24){RendererRect r=clip_map((RendererRect){x+xx,y+yy,a->w-xx<24?a->w-xx:24,a->h-yy<24?a->h-yy:24});if(!r.w||!r.h)continue;
   timed_guard(4);do{frame_pipeline_poll();rc=bitblt(GPU_OPERATION_SPRITE,(u32)(uintptr_t)(src+((r.y-y)*a->w+r.x-x)*2),(u32)(uintptr_t)(flow_world+r.y*1280+r.x),r.w,r.h,a->w*2,2560,0);}while(rc==-2);if(rc)return rc;}
  if((rc=timed_fence()))return rc;
 }
 WaveVisual frozen_wave=scene_wave_built();
 for(unsigned icon=0;icon<COUNTDOWN_INSTANCE_COUNT;icon++){
  if((rc=scene_countdown_overlay(world,scene_countdown_bounds(icon),frozen_wave)))return rc;
 }
 if((rc=timed_fence()))return rc;
 /* Latest rules HUD goes only into private ending bases, once, before gray.
  * Original world geometry and display remain untouched; gray/death never
  * reintroduce colored top pixels. No ordinary FB revision is committed. */
 HudState terminal;hud_capture(&terminal);
 if((rc=hud_draw_terminal_top(renderer_flow_snapshot(0),&terminal)))return rc;
 return hud_draw_terminal_top(world,&terminal);
}

int renderer_reset_for_new_game(void){
 memset(submitted_valid,0,sizeof submitted_valid);pause_metadata_valid=0;
 memset(initialized,0,sizeof initialized);memset(fb_version,0,sizeof fb_version);
 memset(previous,0,sizeof previous);memset(dirty_count,0,sizeof dirty_count);
 memset(enemy_dirty,0,sizeof enemy_dirty);memset(enemy_pause_dirty,0,sizeof enemy_pause_dirty);
 memset(enemy_pending_tiles,0,sizeof enemy_pending_tiles);memset(wave_tiles,0,sizeof wave_tiles);
 memset(wave_seen,0,sizeof wave_seen);memset(hud_previous,0,sizeof hud_previous);
 memset(pause_previous,0,sizeof pause_previous);memset(flow_death_fb,0,sizeof flow_death_fb);
 memset(&enemy_owned_plan,0,sizeof enemy_owned_plan);memset(&enemy_pause_plan,0,sizeof enemy_pause_plan);
 enemy_plan=&enemy_owned_plan;wave_plan=0;wave_mask=wave_active=0;
 pause_dirty=pause_fb_version=pause_source_fb=pause_wave_seen=0;pause_alpha=flow_alpha=flow_world_alpha=0;pause_player_alpha=flow_player_alpha=collecting_alpha=0;next_arrow=0;
 flow_world_gray=0;hud_pause_previous=(DragonClip){0};hud_reset_state();hud_capture(&hud_plan);
 return 0;
}
int renderer_restore(unsigned fb){render_work=render_wait=0;uint64_t t=dragon_gpu_clock(),w=render_wait_total();int rc=renderer_restore_impl(fb);uint64_t elapsed=dragon_gpu_clock()-t,wait=render_wait_total()-w;render_wait+=wait;render_work+=elapsed>wait?elapsed-wait:0;return rc;}

int renderer_draw(unsigned fb,const RendererSnapshot*s){uint64_t t=dragon_gpu_clock(),w=render_wait_total();int rc=renderer_draw_impl(fb,s);uint64_t elapsed=dragon_gpu_clock()-t,wait=render_wait_total()-w;render_wait+=wait;render_work+=elapsed>wait?elapsed-wait:0;return rc;}

int renderer_draw_next(unsigned fb,const RendererSnapshot*s){uint64_t t=dragon_gpu_clock(),w=render_wait_total();int rc=renderer_draw_next_impl(fb,s);uint64_t elapsed=dragon_gpu_clock()-t,wait=render_wait_total()-w;render_wait+=wait;render_work+=elapsed>wait?elapsed-wait:0;return rc;}

int renderer_enemy_draw(unsigned fb){uint64_t t=dragon_gpu_clock(),w=render_wait_total();int rc=renderer_enemy_draw_impl(fb);uint64_t elapsed=dragon_gpu_clock()-t,wait=render_wait_total()-w;render_wait+=wait;render_work+=elapsed>wait?elapsed-wait:0;return rc;}

int renderer_occlude(unsigned fb){uint64_t t=dragon_gpu_clock(),w=render_wait_total();int rc=renderer_occlude_impl(fb);uint64_t elapsed=dragon_gpu_clock()-t,wait=render_wait_total()-w;render_wait+=wait;render_work+=elapsed>wait?elapsed-wait:0;return rc;}

int renderer_countdown_finish(unsigned fb){uint64_t t=dragon_gpu_clock(),w=render_wait_total();int rc=renderer_countdown_finish_impl(fb);uint64_t elapsed=dragon_gpu_clock()-t,wait=render_wait_total()-w;render_wait+=wait;render_work+=elapsed>wait?elapsed-wait:0;return rc;}
