#include "render.h"
#include "assets.h"
#include "orb_path.h"
#define ALPHA_TAGS_IMPLEMENTATION
#include "alpha_tags.h"
#include "alpha_pixel_masks.h"
#include "2D_DMA.h"
#include "io.h"
#include "bsp.h"
#include "clint.h"
#include <stdint.h>
#include <string.h>

/* 40x23 tiles. Journals belong to physical FBs, not frame sequence numbers.
 * All old damage is restored before ANY current foreground is drawn. */
typedef struct { uint32_t row[23][2]; uint32_t background; uint8_t valid; } Journal;
static Journal journals[3];
static unsigned target;
static uint32_t commands, fences;
static int failed;
static unsigned map_clip,stage;
static uint16_t warm_scratch[1024] __attribute__((aligned(64)));
static LastGpuCommand last_gpu_command;
static RenderCacheStats cache_stats;
static RenderStats render_snapshot;
static uint32_t pending_write[23][2],pending_alpha[23][2];
static uint32_t gpu_rate,display_rate;
const RenderStats *render_stats(void){return &render_snapshot;}
void render_set_rates(uint32_t gpu_fps_x10,uint32_t display_fps_x10){gpu_rate=gpu_fps_x10;display_rate=display_fps_x10;}
const LastGpuCommand *render_last_gpu_command(void){return &last_gpu_command;}
const RenderCacheStats *render_cache_stats(void){return &cache_stats;}
static void gpu_failure(int rc) {
 if(failed)return;
 failed=rc;last_gpu_command.rc=rc;last_gpu_command.stage=stage;
 last_gpu_command.status=read_u32(GPU_BASE+GPU_STATUS_OFFSET);
 last_gpu_command.fifo=read_u32(GPU_BASE+GPU_FIFO_STATUS_OFFSET);
 if(rc==-1)++cache_stats.timeout;
}
static void record_command(unsigned op,uint32_t src,uint32_t dst,unsigned w,unsigned h,
                           unsigned ss,unsigned ds,unsigned color) {
 last_gpu_command=(LastGpuCommand){op,src,dst,w,h,ss,ds,color,0,0,stage,0};
}
static uint32_t fb_base(unsigned fb) {
 return fb==1?FRAMEBUFFER_BASE_1:(fb==2?FRAMEBUFFER_BASE_2:FRAMEBUFFER_BASE_3);
}
static uint32_t address(uint32_t pixel) {
 return (uint32_t)(uintptr_t)asset_pixels + pixel*2u;
}
static int fence(void) {
 uint64_t start,now;unsigned status;int rc=0;if(failed)return failed;
 ++fences;start=clint_getTime(BSP_CLINT);
 for(;;){
  frame_pipeline_poll();status=read_u32(GPU_BASE+GPU_STATUS_OFFSET);
  if(status&GPU_STATUS_ERROR){rc=-3;break;}
  if((status&(GPU_STATUS_DONE|GPU_STATUS_EMPTY|GPU_STATUS_BUSY))==(GPU_STATUS_DONE|GPU_STATUS_EMPTY))break;
  now=clint_getTime(BSP_CLINT);
  if(now-start>=(uint64_t)BSP_CLINT_HZ*2u){rc=-1;break;}
 }
 gpu_wait_record(start,clint_getTime(BSP_CLINT));
 if(rc)gpu_failure(rc);
 else {memset(pending_write,0,sizeof pending_write);memset(pending_alpha,0,sizeof pending_alpha);}
 return rc;
}
static int emit_raw(unsigned op,uint32_t src,uint32_t dst,unsigned w,unsigned h,
                    unsigned ss,unsigned ds,unsigned color) {
 int rc;if(failed)return failed;
 record_command(op,src,dst,w,h,ss,ds,color);
 for(;;){
  rc=bitblt(op,src,dst,w,h,ss,ds,color);
  if(rc!=-2)break;
  rc=gpu_wait_fifo_ready();if(rc){gpu_failure(rc);return rc;}
 }
 if(rc)gpu_failure(rc);else ++commands;return rc;
}
static int emit(unsigned op,uint32_t src,int x,int y,int w,int h,unsigned stride,unsigned color) {
 return emit_raw(op,src,fb_base(target)+((uint32_t)y*1280u+(unsigned)x)*2u,
                 (unsigned)w,(unsigned)h,stride,2560u,color);
}
static int cache_warm(void) {
 unsigned i;stage=RENDER_STAGE_WARM;
 /* assets_init publishes immutable DDR with its existing invalidate/fence.
  * Do NOT wait for DONE before the first COPY: reset status is zero. */
 for(i=0;i<6&&!failed;i++)if(!(cache_stats.resident&(1u<<i))){
  const AssetImage*a=&asset_images[asset_cache_images[i]];
  if(!a->w||!a->h||a->w>32||a->h>32||(unsigned)a->w*a->h>1024){gpu_failure(-6);break;}
  if(emit_raw(GPU_OPERATION_COPY,address(a->offset),(uint32_t)(uintptr_t)warm_scratch,
              a->w,a->h,a->w*2u,a->w*2u,0x80000000u|((i+1u)<<28)))break;
  ++cache_stats.fill;
  if(fence())break;
  cache_stats.resident|=1u<<i;
 }
 return failed;
}
/* Clip destination and corresponding packed source rectangle together. */
static void rect(unsigned op,uint32_t src,unsigned stride,int x,int y,int w,int h,unsigned color) {
 int sx=0,sy=0;
 if(map_clip){
  if(y<50){sy=50-y;h-=sy;y=50;}
  if(y>=680||h<=0)return;
  if(h>680-y)h=680-y;
 }
 if(x<0){sx=-x;w+=x;x=0;}if(y<0){sy=-y;h+=y;y=0;}
 if(x>=1280||y>=720||w<=0||h<=0)return;
 if(w>1280-x)w=1280-x;
 if(h>720-y)h=720-y;
 if(op!=GPU_OPERATION_COLOR)src+=(unsigned)sy*stride+(unsigned)sx*2u;
 emit(op,src,x,y,w,h,stride,color);
}
static void damage(int x,int y,int w,int h) {
 Journal*j=&journals[target-1];int a,b,r,c;
 if(w<=0||h<=0||x>=1280||y>=720||x+w<=0||y+h<=0)return;
 a=x<0?0:x/32;b=(x+w-1)/32;if(b>39)b=39;
 r=y<0?0:y/32;h=(y+h-1)/32;if(h>22)h=22;
 for(;r<=h;r++)for(c=a;c<=b;c++)j->row[r][c>>5]|=1u<<(c&31);
}
static void color(int x,int y,int w,int h,unsigned value) {
 rect(GPU_OPERATION_COLOR,0,0,x,y,w,h,value);damage(x,y,w,h);
}
typedef struct { int x0,y0,x1,y1; } AlphaBox;
/* Only the current node's three displayed label rectangles are candidates. */
static AlphaBox option_boxes[3];
static int box_visible(AlphaBox b){return b.x0<b.x1&&b.y0<b.y1&&b.x0<1280&&b.y0<720&&b.x1>0&&b.y1>0;}
static int box_intersects(AlphaBox a,AlphaBox b){return a.x0<b.x1&&b.x0<a.x1&&a.y0<b.y1&&b.y0<a.y1;}
static int box_contains(AlphaBox b,int x,int y){return x>=b.x0&&x<b.x1&&y>=b.y0&&y<b.y1;}
/* Fixed 40x23 tile sets describe uncompleted destination writes. An RMW reads
 * destination, so it must not race any write; an opaque write must not race an
 * outstanding RMW. Tile false positives are safe and bounded, no all-pairs list. */
static int sprite_hazard(int x,int y,int w,int h,unsigned alpha,unsigned mark){
 int r,c;unsigned hit=0;
 for(r=y/32;r<=(y+h-1)/32;r++)for(c=x/32;c<=(x+w-1)/32;c++){
  uint32_t bit=1u<<(c&31);
  if(mark){pending_write[r][c>>5]|=bit;if(alpha)pending_alpha[r][c>>5]|=bit;}
  else if((alpha?pending_write[r][c>>5]:pending_alpha[r][c>>5])&bit)hit=1;
 }
 return (int)hit;
}
/* No source tag/offset exists in the cache. Any left/top source offset (including
 * an interior split) must select DDR, never just reuse the original cache slot. */
static void sprite_piece(unsigned index,int ox,int oy,int x,int y,int w,int h,unsigned q) {
 const AssetImage*a=&asset_images[asset_cache_images[index]];
 unsigned sx=(unsigned)(x-ox),sy=(unsigned)(y-oy),opt=0;
 uint32_t src=address(a->offset)+sy*a->w*2u+sx*2u;
 if(!w||!h||!q||failed)return;
 if(!sx&&!sy&&(cache_stats.resident&(1u<<index)))opt=(index+1u)<<28;
 /* Cache access_done releases the source, not the destination. */
 if((!opt||sprite_hazard(x,y,w,h,q<32,0))&&fence())return;
 if(!emit(index<3?(q==32?GPU_OPERATION_COPY:GPU_OPERATION_ALPHA):
                  (q==32?GPU_OPERATION_SPRITE:GPU_OPERATION_ALPHASPRITE),
           src,x,y,w,h,a->w*2u,opt|(q==32?0:q))){
  if(opt)++cache_stats.use;else ++cache_stats.fallback;
  if(q<32)++render_snapshot.alpha_commands;
  sprite_hazard(x,y,w,h,q<32,1);
 }
 if(!opt)fence();
}
/* Offline source support: at most 32 row-word tests, never CPU pixel blending.
 * Count only actual nonzero source within an alpha cell, not transparent corners. */
static int piece_alpha_active(unsigned index,unsigned sx,unsigned sy,unsigned w,unsigned h){
 uint32_t bits;unsigned y;
 if(index>=6||!w||!h||sx>=32||sy>=32)return 0;
 if(index<3)return 1; /* RGB565 zero is opaque too; never consult key-zero masks. */
 bits=(w>=32?UINT32_MAX:((1u<<w)-1u))<<sx;
 for(y=sy;y<sy+h&&y<32;y++)if(alpha_sprite_rows[index][y]&bits)return 1;
 return 0;
}
static void boundary(int *v,unsigned *n,int x,int lo,int hi){
 unsigned i,j;if(x<=lo||x>=hi)return;
 for(i=0;i<*n;i++)if(v[i]==x)return;
 for(i=0;i<*n&&v[i]<x;i++){}
 for(j=*n;j>i;j--)v[j]=v[j-1];
 v[i]=x;++*n;
}
/* Five rectangles: <=12 boundaries/axis, <=121 disjoint cells per instance.
 * Labels take Q5=26 priority over the player/laser Q5=16 union. Each pixel is
 * submitted once, so triple overlaps never blend twice. O(N), not 25 cells. */
static void split_sprite(unsigned index,int cx,int cy,AlphaBox player,AlphaBox laser){
 const AssetImage*a;AlphaBox b,candidates[5];int ox,oy,xs[12],ys[12];unsigned nx=2,ny=2,i,j,k,hit=0;
 if(index>=6||failed)return;
 a=&asset_images[asset_cache_images[index]];ox=cx-a->ax;oy=cy-a->ay;
 b=(AlphaBox){ox,oy,ox+a->w,oy+a->h};if(!box_visible(b))return;
 ++render_snapshot.sprite_count;damage(ox,oy,a->w,a->h);
 if(b.x0<0)b.x0=0;
 if(b.y0<0)b.y0=0;
 if(b.x1>1280)b.x1=1280;
 if(b.y1>720)b.y1=720;
 candidates[0]=player;candidates[1]=laser;
 for(k=0;k<3;k++)candidates[k+2]=option_boxes[k];
 xs[0]=b.x0;xs[1]=b.x1;ys[0]=b.y0;ys[1]=b.y1;
 for(k=0;k<5;k++){
  AlphaBox c=candidates[k];if(!box_intersects(b,c))continue;
  boundary(xs,&nx,c.x0,b.x0,b.x1);boundary(xs,&nx,c.x1,b.x0,b.x1);
  boundary(ys,&ny,c.y0,b.y0,b.y1);boundary(ys,&ny,c.y1,b.y0,b.y1);
 }
 for(j=0;j+1<ny;j++)for(i=0;i+1<nx;i++){
  unsigned q=(box_contains(player,xs[i],ys[j])||box_contains(laser,xs[i],ys[j]))?16u:32u;
  for(k=0;k<3;k++)if(box_contains(option_boxes[k],xs[i],ys[j]))q=26;
  /* RGB bullets are opaque even at black pixels; masks apply only to effects. */
  if(q<32&&(index<3||piece_alpha_active(index,(unsigned)(xs[i]-ox),(unsigned)(ys[j]-oy),
                               (unsigned)(xs[i+1]-xs[i]),(unsigned)(ys[j+1]-ys[j]))))hit=1;
  sprite_piece(index,ox,oy,xs[i],ys[j],xs[i+1]-xs[i],ys[j+1]-ys[j],q);
 }
 if(hit)++render_snapshot.alpha_objects;
}
/* Contract-test entry for an ordinary, fully opaque cached instance. */
static void cache_sprite(unsigned index,int x,int y){
 AlphaBox empty={0,0,0,0};split_sprite(index,x,y,empty,empty);
}
static unsigned effect_cache(const EffectPoint*p) {
 unsigned i,id;
 if(p->kind==3)return 5; /* snow, never old per-angle image 185 */
 id=asset_effect_images[p->variant];
 for(i=0;i<6;i++)if(asset_cache_images[i]==id)return i;
 return p->kind==4?3u:0u;
}
static void image(unsigned id,int x,int y,int sprite) {
 const AssetImage*a=&asset_images[id];unsigned k;
 x-=a->ax;y-=a->ay;
 (void)sprite;
 for(k=0;k<a->span_count;k++){
  const AssetSpan*s=&asset_spans[a->span_first+k];
  rect(GPU_OPERATION_COPY,address(a->offset+(uint32_t)s->y*a->w+s->x),a->w*2u,
       x+s->x,y+s->y,s->n,1,0);
 }
 damage(x,y,a->w,a->h);
 /* Spans of one image never overlap; different images MAY overlap. */
 fence();
}
static void restore(uint32_t background) {
 Journal*j=&journals[target-1];unsigned r,c,start;GpuPreparedCopy p;int rc;
 if(!j->valid||j->background!=background){
  rect(GPU_OPERATION_COPY,address(background),2560,0,0,1280,720,0);
 }else for(r=0;r<23&&!failed;r++)for(c=0;c<40&&!failed;){
  if(!(j->row[r][c>>5]&(1u<<(c&31)))){++c;continue;}
  start=c;do{++c;}while(c<40&&(j->row[r][c>>5]&(1u<<(c&31))));
  rc=gpu_prepare_copy(&p,address(background),start*32u,r*32u,(c-start)*32u,r==22?16u:32u,target);
  if(rc){gpu_failure(rc);break;}
  do {rc=gpu_enqueue_copy(&p);if(rc==-2){int w=gpu_wait_fifo_ready();if(w){gpu_failure(w);break;}}}while(rc==-2&&!failed);
  if(rc)gpu_failure(rc);else ++commands;
 }
 memset(j->row,0,sizeof j->row);j->background=background;
 fence();
}
static void number_step(unsigned v,unsigned digits,int x,int y,int step) {
 unsigned div=1,i;for(i=1;i<digits;i++)div*=10;
 for(i=0;i<digits;i++){unsigned d=(v/div)%10;image(asset_digits[d],x,y,0);x+=step?step:asset_images[asset_digits[d]].w;div/=10;}
}
static void number(unsigned v,unsigned digits,int x,int y){number_step(v,digits,x,y,0);}
static void foreground(const Game*g,unsigned state) {
 static const unsigned laser_palette[4]={0x07e0u,0xf800u,0x001fu,0xffe0u};
 unsigned i,layer;int px=g->px_q8/256,py=g->py_q8/256+50;
 AlphaBox player={0,0,0,0},laser={0,0,0,0};
 const NodeAsset*n=asset_node(g->route,g->node);
 memset(option_boxes,0,sizeof option_boxes);
 stage=RENDER_STAGE_ACTORS;
 if(state!=GS_DEFEAT_CG&&state!=GS_GAMEOVER) {
  unsigned dir=g->dir&7u,id=asset_idle[dir];const AssetImage*a;
  if(g->dodging||state==GS_JUMP){
   static const uint8_t phase[16]={0,1,1,2,2,2,2,2,2,2,2,2,2,1,1,0};
   static const uint8_t lift[16]={0,5,10,15,19,22,25,26,26,25,22,19,15,10,5,0};
   unsigned f=(state==GS_JUMP?g->jump_ms:g->dodge_ms)*16u/533u;if(f>15)f=15;
   id=asset_dodge[dir][phase[f]];if(state==GS_JUMP)py-=lift[f];
  }else if(g->moving)id=asset_walk[dir][(g->animation_ms*45u/1000u)%45u];
  a=&asset_images[id];player=(AlphaBox){px-a->ax,py-a->ay,px-a->ax+a->w,py-a->ay+a->h};
  image(id,px,py,0);if(box_visible(player))++render_snapshot.sprite_count;
 }
 /* The opaque laser is above the player. Bullet RMW sees their completed union. */
 if(g->question_no>0&&state!=GS_START&&g->laser_tick<180u){
  const LaserAsset*l=&asset_lasers[(g->game_ms/10000u)%8u];unsigned w=asset_laser_width[g->laser_tick];
  if(w){int left=l->x<l->end_x?l->x:l->end_x;
   int right=l->x>l->end_x?l->x:l->end_x;int len=right-left+1;
   laser=(AlphaBox){left,l->y+50-(int)w/2,right+1,l->y+50-(int)w/2+(int)w};
   color(left,laser.y0,len,w,laser_palette[l->kind&3u]);fence();
   color(left,laser.y0+(w>2?(int)(w-2)/2:0),len,w>2?2:1,0xffff);fence();
   if(box_visible(laser))++render_snapshot.sprite_count;
  }
 }
 /* Draw labels once, after laser and before bullets/effects. Use the selected
  * near image's actual anchor/size and its real six-pixel raised position. */
 if(n&&n->count&&state!=GS_DEFEAT_CG&&state!=GS_GAMEOVER){
  for(i=0;i<n->count&&i<3;i++){
   unsigned id=asset_labels[i][g->near_option==i];const AssetImage*a=&asset_images[id];
   int x=n->ox[i],y=n->oy[i]+50-(g->near_option==i?6:0);
   option_boxes[i]=(AlphaBox){x-a->ax,y-a->ay,x-a->ax+a->w,y-a->ay+a->h};
   image(id,x,y,0);
  }
 }
 stage=RENDER_STAGE_BULLETS;
 /* O(N) fixed texture passes. Opaque same-texture writes can overlap across
  * cores: coverage order is approximate, as before; alpha conflicts are completion-fenced. */
 for(layer=0;layer<5;layer++){
  for(i=0;i<G21_BULLET_CAP;i++)if(g->bullets[i].alive){
   const G21Bullet*b=&g->bullets[i];unsigned l=b->kind;
   if(l==layer){unsigned k=l<3?l:l==3?5u:3u;AlphaBox near_laser=laser;
    /* Ordinary 18x18 trajectory bullets only. Validate point identity so custom
     * replay positions and invalid ages fall back to exact live geometry. */
    if(l<3&&box_visible(laser)&&laser.y1-laser.y0<=(int)ALPHA_TAGS_MAX_LASER_WIDTH&&asset_traj_points&&b->traj<64u){
     const TrajAsset*t=&asset_trajs[b->traj];unsigned first=t->first,age=b->age;
     if(age<t->count&&first<ALPHA_TAGS_POINT_COUNT&&age<ALPHA_TAGS_POINT_COUNT-first){
      const TrajPoint*p=&asset_traj_points[first+age];
      if(p->x==b->x&&p->y==b->y&&!alpha_tags_near_slot(alpha_tags_for_age(first,age,t->count),(g->game_ms/10000u)%8u))
       near_laser=(AlphaBox){0,0,0,0};
     }
    }
    if(!box_visible(player)&&!box_visible(near_laser)&&
        !box_visible(option_boxes[0])&&!box_visible(option_boxes[1])&&!box_visible(option_boxes[2]))cache_sprite(k,b->x,b->y+50);
    else split_sprite(k,b->x,b->y+50,player,near_laser);
   }
  }fence();
 }
 /* The one-second approach belongs to the same frozen game clock as blast.
  * Geometry is a PC-generated table; nested COLOR rectangles need fences. */
 if(state!=GS_START&&(g->game_ms%12000u)>=YELLOW_ORB_START_MS){
  static const uint16_t rainbow[5]={0xf800,0xfd20,0xffe0,0x07e0,0x07ff};
  unsigned age=((g->game_ms%12000u)-YELLOW_ORB_START_MS)*30u/YELLOW_ORB_DURATION_MS;if(age>29)age=29;
  int x=yellow_orb_path[age][0],y=yellow_orb_path[age][1]+50;
  if(box_visible((AlphaBox){x-7,y-7,x+8,y+8}))++render_snapshot.sprite_count;
  for(i=0;i<5;i++){int r=7-(int)i;color(x-r,y-r,r*2+1,r*2+1,rainbow[i]);fence();}
 }
 stage=RENDER_STAGE_EFFECTS;
 for(i=0;i<G21_EFFECT_CAP;i++)if(g->effects[i].alive&&i<asset_effect_count&&g->effects[i].age<asset_effect_frames){
  const G21Effect*e=&g->effects[i];const EffectPoint*p=&asset_effect_points[(unsigned)e->age*asset_effect_count+i];
  if(p->visible&&e->x>-1000&&e->x<2280&&e->y>-1000&&e->y<1720)
   {split_sprite(effect_cache(p),e->x,e->y+50,player,laser);fence();}
  /* Different textures may overlap; keep individual completion.
   * Worst-case 150 fences is deliberate correctness, not a 2ms claim. */
 }
 if(state!=GS_DEFEAT_CG&&state!=GS_GAMEOVER){
  image(asset_stamina[g->stamina>8?8:g->stamina],px,py+8,0);
 }
 /* Reapply static HUD strips after world objects, before card/options/UI.
  * This also protects the UI from effects and world rectangles at map edges. */
 stage=RENDER_STAGE_UI;
 rect(GPU_OPERATION_COPY,address(asset_background_offset),2560,0,0,1280,50,0);
 rect(GPU_OPERATION_COPY,address(asset_background_offset+680u*1280u),2560,0,680,1280,40,0);
 fence();
 if(n&&n->count&&state!=GS_DEFEAT_CG&&state!=GS_GAMEOVER){
  const IslandAsset*start=&asset_islands[0];
  if(state!=GS_START)image(n->card,start->cx,start->cy+50,0);
 }
 /* HP is a 30-slot strip, not a number. Gaps retain the baked empty slots. */
 for(i=0;i<g->hp&&i<30;i++)color(70+(int)i*11,16,10,14,0xf800);
 fence();
 {unsigned seconds=g->game_ms/1000u;int dw=asset_images[asset_digits[0]].w;
  number(seconds/60u,2,477,15);image(asset_digits[10],477+2*dw,15,0);
  number(seconds%60u,2,477+3*dw,15);
  number(g->question_no,2,605,15);image(asset_digits[11],605+2*dw,15,0);
  number(g->question_total,2,605+3*dw,15);
  number_step(render_snapshot.sprite_count,4,944,15,14);
  number_step(render_snapshot.alpha_objects,4,1186,15,20);
 }
 /* Clear right-hand tips only; preserve WASD/JKE and the left tips. */
 color(920,684,360,36,0x0841);fence();
 image(asset_perf_labels[0],922,687,0);image(asset_perf_labels[1],1097,687,0);
 {int dw=asset_images[asset_digits[0]].w;
  number(gpu_rate/10u,3,974,687);color(974+3*dw+2,700,2,2,0xffff);fence();
  number(gpu_rate%10u,1,974+3*dw+7,687);
  number(display_rate/10u,2,1180,687);color(1180+2*dw+2,700,2,2,0xffff);fence();
  number(display_rate%10u,1,1180+2*dw+7,687);
 }
 if(n&&n->count&&state!=GS_START&&state!=GS_DEFEAT_CG&&state!=GS_GAMEOVER){unsigned limit=n->tier*1000u;
  number(g->question_ms<limit?(limit-g->question_ms+999u)/1000u:0,2,815,126);
 }
}
static void graywash(unsigned q) {
 fence();
 /* One packet owns every pixel. Actual large-rectangle DMA requires board test. */
 if(q){rect(GPU_OPERATION_ALPHA,address(asset_graywash_offset),2560,0,0,1280,720,q);damage(0,0,1280,720);}
 fence();
}
int render_init(void) { memset(journals,0,sizeof journals);memset(&cache_stats,0,sizeof cache_stats);memset(&last_gpu_command,0,sizeof last_gpu_command);commands=fences=0;failed=0;memset(&render_snapshot,0,sizeof render_snapshot);gpu_rate=display_rate=0;map_clip=0;stage=RENDER_STAGE_IDLE;return 0; }
int render_frame(unsigned fb,const Game*g) {
 unsigned state;Journal*j;
 commands=fences=0;failed=0;memset(&render_snapshot,0,sizeof render_snapshot);
 if(!g||fb<1||fb>3||!asset_pixels)return -1;
 target=fb;j=&journals[fb-1];state=g->state==GS_PAUSE?g->resume_state:g->state;
 if(cache_warm()){j->valid=0;return failed;}
 stage=RENDER_STAGE_RESTORE;restore(asset_background_offset);
 /* Fade the static island surface BEFORE frozen dynamic actors. */
 if(state>=GS_DEFEAT_ISLAND&&state<=GS_GAMEOVER&&g->node<18){
  unsigned i=g->node;const IslandAsset*a=&asset_islands[i];int w=a->x1-a->x0,h=a->y1-a->y0;
  rect(GPU_OPERATION_COPY,address(asset_fade_offsets[i][state==GS_DEFEAT_ISLAND?(g->defeat_level>8?8:g->defeat_level):8]),w*2u,a->x0,a->y0+50,w,h,0);
  damage(a->x0,a->y0+50,w,h);fence();
 }
 /* Count frozen death actor before HUD is drawn; actual CG remains above wash. */
 if(state==GS_DEFEAT_CG||state==GS_GAMEOVER){
  const AssetImage*a=&asset_images[asset_cg[g->defeat_frame>4?4:g->defeat_frame]];
  int x=g->px_q8/256-a->ax,y=g->py_q8/256+50-a->ay;
  if(x<1280&&x+(int)a->w>0&&y<680&&y+(int)a->h>50)++render_snapshot.sprite_count;
 }
 if(!failed)foreground(g,state);
 if(state==GS_DEFEAT_GREY||state==GS_DEFEAT_CG||state==GS_GAMEOVER){
  unsigned q=state==GS_DEFEAT_GREY?(g->state_ms>=500u?20u:g->state_ms*20u/500u):20u;
  stage=RENDER_STAGE_DEFEAT;graywash(q);
  if(state==GS_DEFEAT_CG||state==GS_GAMEOVER){
   map_clip=1;
   image(asset_cg[g->defeat_frame>4?4:g->defeat_frame],g->px_q8/256,g->py_q8/256+50,0);
   map_clip=0;
  }
 }
 if(g->state==GS_PAUSE)image(asset_pause,640,360,0);
 if(state==GS_GAMEOVER||state==GS_QUIT)image(asset_gameover,640,360,0);
 if(state==GS_WIN)image(asset_win,640,360,0);
 stage=RENDER_STAGE_FINAL;fence();j->valid=(uint8_t)!failed;return failed;
}
uint32_t render_commands(void){return commands;}
uint32_t render_fences(void){return fences;}
