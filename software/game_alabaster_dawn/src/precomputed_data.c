#include "precomputed_data.h"
#include "external_assets.h"
#include "../resources/precomputed/routes_sizes.h"
#include <stddef.h>
#include <string.h>
_Static_assert(sizeof(PcRoute)==32,"PcRoute ABI");
_Static_assert(offsetof(PcRoute,point_first)==8 && offsetof(PcRoute,hazard_first)==24,"PcRoute layout");
#ifndef EXTERNAL_PRECOMPUTED
#define EXTERNAL_PRECOMPUTED 8u
#endif
static unsigned char arena[PC_RAW_BYTES] __attribute__((aligned(64)));
static PcRoute routes[PC_ROUTE_COUNT] __attribute__((aligned(64)));
/* Decode and full raw/body CRC validation are startup-only costs, not frame work. */
static const unsigned char *data;
static uint32_t points_offset;
static PcRouteStats stats;
static const uint16_t bottom[15]={170,232,294,356,418,480,542,640,738,800,862,924,986,1048,1110};
static uint16_t u16(const unsigned char*p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t u32(const unsigned char*p){return (uint32_t)u16(p)|((uint32_t)u16(p+2)<<16);}
static uint32_t crc(const unsigned char*p,uint32_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1u));}return ~c;}
void precomputed_data_reset(void){data=0;points_offset=0;}
static int bad(void){++stats.validation_failures;return -1;}
int precomputed_data_bind(const void*bytes,uint32_t n){
 const unsigned char*p=bytes;uint32_t po,np;precomputed_data_reset();
 if(!p||n!=PC_RAW_BYTES||n<64||memcmp(p,"PTR9v1\0\0",8))return bad();
 if(u32(p+8)!=1||u32(p+12)!=64||u32(p+16)!=32||u32(p+20)!=PC_ROUTE_COUNT||u32(p+24)!=64||u32(p+36)!=n||u32(p+44)!=20||u32(p+48)!=2||u32(p+52)||u32(p+56)||u32(p+60))return bad();
 po=u32(p+28);np=u32(p+32);
 if(po!=64+PC_ROUTE_COUNT*32||po>n||np!=(n-po)/4||(n-po)%4)return bad();
 if(crc(p+64,n-64)!=u32(p+40)||crc(p,n)!=PC_RAW_CRC32)return bad();
 for(unsigned i=0;i<PC_ROUTE_COUNT;i++){
  const unsigned char*q=p+64+i*32;PcRoute*r=&routes[i];unsigned local,side,entry,variant,wall,kind;
  if(i<3500){kind=1;wall=i/1750;local=i%1750;variant=local/875;local%=875;}else{kind=2;wall=0;local=i-3500;variant=local/860;local%=860;}
  side=local/430;entry=local%430;
  r->kind=q[0];r->side=q[1];r->variant=q[2];r->wall=q[3];r->entry_id=u16(q+4);r->reserved=u16(q+6);r->point_first=u32(q+8);r->point_count=u32(q+12);r->start_x=u16(q+16);r->start_y=u16(q+18);r->end_x=u16(q+20);r->end_y=u16(q+22);r->hazard_first=u32(q+24);r->flags=u32(q+28);
  if(r->kind!=kind||r->side!=side||r->variant!=variant||r->wall!=wall||r->entry_id!=entry||r->reserved||r->flags!=1||r->hazard_first!=UINT32_MAX||r->point_first>np||r->point_count<2||r->point_count>np-r->point_first)return bad();
  if(r->start_x!=(side==2?bottom[entry]:side==0?30:1250)||r->start_y!=(side==2?605:150+entry))return bad();
  if(r->end_x>=1280||r->end_y>=630)return bad();
  int dx=(int)r->end_x-640,dy=(int)r->end_y-70;if(dx*dx+dy*dy>400)return bad();
  q=p+po+r->point_first*4;if(u16(q)!=r->start_x||u16(q+2)!=r->start_y)return bad();
  q+=4*(r->point_count-1);if(u16(q)!=r->end_x||u16(q+2)!=r->end_y)return bad();
 }
 for(uint32_t j=0;j<np;j++){const unsigned char*q=p+po+j*4;if(u16(q)>=1280||u16(q+2)>=630)return bad();}
 points_offset=po;data=p;return 0;
}
int precomputed_data_init(void){if(data)return 0;if(external_decode(EXTERNAL_PRECOMPUTED,0,arena,PC_RAW_BYTES))return bad();return precomputed_data_bind(arena,PC_RAW_BYTES);}
const PcRoute*pc_route_find(unsigned kind,unsigned side,unsigned entry,unsigned variant,unsigned wall){
 ++stats.find_calls;
 if(!data||variant>=2||wall>=2||side>2||entry>=(side==2?15u:430u)||!(kind==1||(kind==2&&side<2))){++stats.bad_requests;return 0;}
 unsigned i=kind==1?wall*1750+variant*875+side*430+entry:3500+variant*860+side*430+entry;
 return &routes[i];
}
int pc_route_point(const PcRoute*r,uint32_t step,uint16_t out[2]){
 ++stats.point_calls;uintptr_t a=(uintptr_t)r,b=(uintptr_t)routes;
 if(!data||!out||a<b||a-b>=sizeof(routes)||(a-b)%sizeof(PcRoute)||step>=r->point_count){++stats.bad_requests;return -1;}
 const unsigned char*p=data+points_offset+4*(r->point_first+step);out[0]=u16(p);out[1]=u16(p+2);return 0;
}
int pc_route_cursor_init(const PcRoute*r,uint32_t step,PcRouteCursor*out){
 uintptr_t a=(uintptr_t)r,b=(uintptr_t)routes;
 if(!data||!out||a<b||a-b>=sizeof routes||(a-b)%sizeof(PcRoute)||step>r->point_count){++stats.bad_requests;return -1;}
 uint32_t count=(PC_RAW_BYTES-points_offset)/4u;
 if(r->point_first>count||r->point_count>count-r->point_first){++stats.bad_requests;return -1;}
 PcRouteCursor c={data+points_offset+4u*(r->point_first+step),data+points_offset+4u*(r->point_first+r->point_count),r->point_count-step};
 *out=c;return 0;
}
const PcRouteStats*pc_route_stats(void){return &stats;}
