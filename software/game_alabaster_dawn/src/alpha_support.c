#include "alpha_support.h"
#include "../resources/alpha_support/tables.h"
#include <stddef.h>
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
/* uintptr validation avoids undefined subtraction of unrelated pointers. */
static int plane_index(const void *p,const void *base,size_t count,unsigned *index){
 uintptr_t a=(uintptr_t)p,b=(uintptr_t)base,d;
 if(!p||!base||a<b)return 0;
 d=a-b;
 if(d/16u>=count||d%16u)return 0;
 *index=(unsigned)(d/16u);return 1;
}
static int intersects(unsigned pw,unsigned ph,int x,int y,int w,int h,
                      const uint32_t *masks,const uint8_t *run){
 int64_t right=(int64_t)x+w,bottom=(int64_t)y+h;
 unsigned left,top,r,b,start=0;uint32_t columns;
 if(w<=0||h<=0||!pw||!ph||pw>24||ph>24||right<=0||bottom<=0||x>=(int)pw||y>=(int)ph)return 0;
 left=x<0?0u:(unsigned)x;top=y<0?0u:(unsigned)y;
 r=right>(int64_t)pw?pw:(unsigned)right;
 b=bottom>(int64_t)ph?ph:(unsigned)bottom;
 columns=((1u<<r)-1u)&~((1u<<left)-1u);
 while(start<b){
  unsigned end=run[2],id=(unsigned)run[0]|((unsigned)run[1]<<8);
  if(end>top&&(masks[id]&columns))return 1;
  start=end;run+=3;
 }
 return 0;
}
int alpha_support_dragon(const DragonPlane *p,int x,int y,int w,int h){
 const DragonFrame *f=dragon_frame(DRAGON_BODY,0);
 const DragonPlane *base=f?dragon_plane(f,0):NULL;unsigned i;
 if(!plane_index(p,base,COUNT(as_dragon_offsets),&i))return 0;
 return intersects(p->w,p->h,x,y,w,h,as_dragon_masks,as_dragon_runs+as_dragon_offsets[i]);
}
int alpha_support_flow(const FlowPlane *p,int x,int y,int w,int h){
 const FlowFrame *f=flow_frame(FLOW_ALTAR,0);
 const FlowPlane *base=f?flow_plane(f,0):NULL;unsigned i;
 if(!plane_index(p,base,COUNT(as_flow_offsets),&i))return 0;
 return intersects(p->w,p->h,x,y,w,h,as_flow_masks,as_flow_runs+as_flow_offsets[i]);
}
_Static_assert(sizeof(DragonPlane)==16,"dragon plane stride");
_Static_assert(sizeof(FlowPlane)==16,"flow plane stride");
