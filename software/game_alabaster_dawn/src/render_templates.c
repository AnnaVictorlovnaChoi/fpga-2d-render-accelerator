#include "render_templates.h"
#include "../resources/command_templates/enemy_commands.inc"
_Static_assert(sizeof(RenderTemplateOp)==16,"command metadata size");
const RenderTemplateOp *render_template_enemy(unsigned frame,unsigned alpha,unsigned stride,unsigned *count){
 unsigned key=alpha==11?0:alpha==16?1:alpha==32?2:3;*count=0;
 if(frame>=7||key>=3||stride!=1280)return 0;
 const RenderTemplateRange*r=&render_template_ranges[frame][key];*count=r->count;return render_template_ops+r->first;
}
int render_template_validate(unsigned frame,const EnemyFrame*f,const EnemyPlane*p){
 static const unsigned alphas[]={11,16,32};
 if(!f||!p||enemy_payload_bound()!=RENDER_TEMPLATE_PAYLOAD_BYTES)return -1;
 for(unsigned k=0;k<3;k++){
  unsigned count,j=0;const RenderTemplateOp*t=render_template_enemy(frame,alphas[k],1280,&count);if(!t)return -1;
  for(unsigned i=0;i<f->plane_count;i++){
   unsigned a=(p[i].alpha_q5*alphas[k]+16)>>5;if(!a)continue;
   if(j>=count)return -1;
   const RenderTemplateOp*q=t+j++;
   if(q->plane!=i||q->source_offset!=p[i].offset||q->dst_offset!=(uint32_t)p[i].y*1280+p[i].x||q->w!=p[i].w||q->h!=p[i].h||q->alpha!=a||q->opt!=(a==32?0:a)||q->op!=(a==32?2:5))return -1;
  }
  if(j!=count)return -1;
 }
 return 0;
}
