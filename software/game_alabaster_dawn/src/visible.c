#include "visible.h"
int visible_gate(VisibleGate*g,const VisibleKey*k,uint32_t now,int force){
 const VisibleKey*p=&g->key;
 int changed=!g->valid||p->xq!=k->xq||p->yq!=k->yq||p->action!=k->action||p->dir!=k->dir||p->arrow_version!=k->arrow_version||p->dragon_version!=k->dragon_version||p->enemy_version!=k->enemy_version||p->scene_version!=k->scene_version||p->flow_state!=k->flow_state||p->pause_state!=k->pause_state||p->countdown!=k->countdown;
 if(!force&&!changed&&g->physical_ms==now)return 0;
 changed=changed||p->frame!=k->frame;
 g->key=*k;g->physical_ms=now;g->valid=1;
 return force||changed;
}
static int32_t rounded(int64_t q){return (int32_t)(q<0?-((-q+128)/256):(q+128)/256);}
int visible_signature(const RendererSnapshot *s,VisibleSignature *v){
 const RuntimeGroup *g=runtime_get_group(s->action,s->dir8);
 const RuntimeFrame *f=runtime_get_frame(s->action,s->dir8,s->elapsed_us);
 if(!g||!f)return -1;
 *v=(VisibleSignature){g,f,s->world_x_q8,s->world_y_q8,
 rounded((int64_t)s->world_x_q8-f->anchor_x_q8),50+rounded((int64_t)s->world_y_q8-f->anchor_y_q8),s->action,s->dir8,s->scene_version};return 0;
}
int visible_equal(const VisibleSignature *a,const VisibleSignature *b){
 return a->scene_version==b->scene_version&&a->group==b->group&&a->frame==b->frame&&a->xq==b->xq&&a->yq==b->yq&&a->x==b->x&&a->y==b->y&&a->action==b->action&&a->dir==b->dir;
}
int visible_skip(const VisibleState *v,const VisibleSignature *candidate){
 return !v->phase&&((v->pending&&visible_equal(candidate,&v->submitted))||(!v->pending&&v->valid&&visible_equal(candidate,&v->completed)));
}
void visible_consume(VisibleState *v){
 if(!v->pending)return;
 v->completed=v->submitted;v->valid=1;v->pending=0;
 /* working target/phase may already be rendering the next frame. */
}
