#include "enemy_data.h"
#include "external_assets.h"
#include "../resources/enemy/layout.h"
#include <stddef.h>
_Static_assert(sizeof(EnemyCacheFrame)==32,"cache frame32");
_Static_assert(sizeof(EnemyFrame)==32,"frame32");
_Static_assert(sizeof(EnemyPlane)==16,"plane16");
_Static_assert(sizeof(EnemySpan)==6,"span6");
static union { uint32_t align; unsigned char bytes[ENEMY_PAYLOAD_BYTES]; } arena;
static const unsigned char *data;
static const unsigned counts[]={3,1,1,1,1}, offsets[]={0,3,4,5,6};
#define H(i) (((const uint32_t *)(const void *)data)[i])
#define CACHE ((const EnemyCacheFrame *)(const void *)(data+H(8)))
#define FRAMES ((const EnemyFrame *)(const void *)(data+H(9)))
#define PLANES ((const EnemyPlane *)(const void *)(data+H(10)))
#define SPANS ((const EnemySpan *)(const void *)(data+H(11)))
static uint32_t crc32(const unsigned char *p,uint32_t n){uint32_t c=~0u;unsigned j;while(n--){c^=*p++;for(j=0;j<8;j++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return ~c;}
static int range(uint32_t off,uint32_t n,uint32_t total){return off<=total&&n<=total-off;}
static int member(const void *p,const void *base,unsigned n,unsigned stride){uintptr_t a=(uintptr_t)p,b=(uintptr_t)base;return p&&a>=b&&a-b<(uintptr_t)n*stride&&(a-b)%stride==0;}
static int cfvalid(const EnemyCacheFrame *f){return data&&member(f,CACHE,7,sizeof(*f));}
static int ffvalid(const EnemyFrame *f){return data&&member(f,FRAMES,7,sizeof(*f));}
static int pfvalid(const EnemyPlane *p){return data&&member(p,PLANES,H(6),sizeof(*p));}
void enemy_assets_reset(void){data=NULL;}
int enemy_assets_bind(const void *decoded,uint32_t bytes){
 const unsigned char *b=(const unsigned char *)decoded;const uint32_t *h;unsigned i,j;
 const EnemyCacheFrame *c;const EnemyFrame *f;const EnemyPlane *p;const EnemySpan *s;
 data=NULL;
 if(!b||((uintptr_t)b&3u)||bytes!=ENEMY_PAYLOAD_BYTES||bytes<64)return -1;
 h=(const uint32_t *)(const void *)b;
 if(h[0]!=0x374e4545u||h[1]!=1||h[2]!=bytes||h[4]!=7||h[5]!=7||h[15]||h[3]!=crc32(b+64,bytes-64))return -2;
 /* Exact monotonically packed sections prevent overlapping metadata/pixels. */
 if(h[8]!=64||h[9]!=288||h[10]!=512||h[6]>(bytes-512)/16||h[11]!=512+h[6]*16||h[7]>(bytes-h[11])/6||h[12]!=((h[11]+h[7]*6+3)&~3u)||h[13]<h[12]||h[14]<h[13]||h[14]>bytes||(h[13]&1u)||(h[14]&1u))return -3;
 c=(const EnemyCacheFrame *)(const void *)(b+h[8]);f=(const EnemyFrame *)(const void *)(b+h[9]);p=(const EnemyPlane *)(const void *)(b+h[10]);s=(const EnemySpan *)(const void *)(b+h[11]);
 for(i=0;i<7;i++){
  const uint32_t *rows;uint32_t maskoff;unsigned spanend;
  if(!c[i].w||!c[i].h||c[i].w>32||c[i].h>32||c[i].slot!=i+1||c[i].alpha_q5!=32||c[i].reserved||c[i].bytes!=(uint32_t)c[i].w*c[i].h*2||c[i].dataoff<h[13]||!range(c[i].dataoff,c[i].bytes,h[14])||(c[i].dataoff&1)||c[i].span_first>h[7]||c[i].rowoff<h[12]||(c[i].rowoff&3)||!range(c[i].rowoff,(2*c[i].h+1)*4,h[13]))return -4;
  if((i==0&&c[i].dataoff!=h[13])||(i&&c[i].dataoff!=c[i-1].dataoff+c[i-1].bytes)||(i==6&&c[i].dataoff+c[i].bytes!=h[14]))return -4;
  rows=(const uint32_t *)(const void *)(b+c[i].rowoff);maskoff=c[i].rowoff+(c[i].h+1)*4;spanend=rows[c[i].h];
  if(rows[0]!=c[i].span_first||spanend>h[7])return -5;
  for(j=0;j<c[i].h;j++){
   uint32_t k,mask=0,stored=((const uint32_t *)(const void *)(b+maskoff))[j];unsigned last=0;
   if(rows[j]>rows[j+1]||rows[j+1]>spanend)return -5;
   for(k=rows[j];k<rows[j+1];k++){
    unsigned x;if(s[k].y!=j||s[k].x0>=s[k].x1||s[k].x1>c[i].w||(k>rows[j]&&s[k].x0<=last))return -5;
    for(x=s[k].x0;x<s[k].x1;x++){mask|=1u<<x;}last=s[k].x1;
   }
   if(mask!=stored)return -5;
  }
 }
 for(i=0;i<h[6];i++){
  uint32_t n=(uint32_t)p[i].w*p[i].h*2;
  if(!p[i].w||!p[i].h||p[i].w>24||p[i].h>24||p[i].alpha_q5<1||p[i].alpha_q5>32||p[i].reserved[0]||p[i].reserved[1]||p[i].reserved[2]||(p[i].offset&1)||p[i].offset<h[14]||!range(p[i].offset,n,bytes))return -6;
 }
 for(i=0;i<7;i++){
  if(!f[i].w||!f[i].h||f[i].w>256||f[i].h>256||f[i].reserved||f[i].source_index>6||f[i].plane_first>h[6]||f[i].plane_count>h[6]-f[i].plane_first||f[i].span_first>h[7]||f[i].span_count>h[7]-f[i].span_first)return -7;
  for(j=0;j<f[i].plane_count;j++){const EnemyPlane *t=&p[f[i].plane_first+j];if((uint32_t)t->x+t->w>f[i].w||(uint32_t)t->y+t->h>f[i].h)return -8;}
  for(j=0;j<f[i].span_count;j++){const EnemySpan *t=&s[f[i].span_first+j];if(t->y>=f[i].h||t->x0>=t->x1||t->x1>f[i].w)return -9;}
 }
 data=b;return 0;
}
int enemy_assets_init(void){if(data)return 0;if(external_decode(EXTERNAL_ENEMY,0,arena.bytes,ENEMY_PAYLOAD_BYTES))return -1;return enemy_assets_bind(arena.bytes,ENEMY_PAYLOAD_BYTES);}
unsigned enemy_cache_count(void){return 7;}
const EnemyCacheFrame *enemy_cache_frame(unsigned i){return data&&i<7?&CACHE[i]:NULL;}
const uint16_t *enemy_cache_pixels(const EnemyCacheFrame *f){return cfvalid(f)?(const uint16_t *)(const void *)(data+f->dataoff):NULL;}
uint32_t enemy_cache_row_mask(const EnemyCacheFrame *f,unsigned row){return cfvalid(f)&&row<f->h?((const uint32_t *)(const void *)(data+f->rowoff+(f->h+1)*4))[row]:0;}
const EnemySpan *enemy_cache_support(const EnemyCacheFrame *f,unsigned *count){unsigned n=0;if(cfvalid(f)){const uint32_t *r=(const uint32_t *)(const void *)(data+f->rowoff);n=r[f->h]-r[0];}if(count)*count=n;return cfvalid(f)?&SPANS[f->span_first]:NULL;}
const EnemySpan *enemy_cache_support_row(const EnemyCacheFrame *f,unsigned row,unsigned *count){if(cfvalid(f)&&row<f->h){const uint32_t *r=(const uint32_t *)(const void *)(data+f->rowoff);if(count)*count=r[row+1]-r[row];return &SPANS[r[row]];}if(count)*count=0;return NULL;}
unsigned enemy_frame_count(EnemyGroup group){return (unsigned)group<ENEMY_GROUP_COUNT?counts[(unsigned)group]:0;}
const EnemyFrame *enemy_frame(EnemyGroup group,unsigned index){return data&&index<enemy_frame_count(group)?&FRAMES[offsets[(unsigned)group]+index]:NULL;}
const EnemyPlane *enemy_plane(const EnemyFrame *f,unsigned i){return ffvalid(f)&&i<f->plane_count?&PLANES[f->plane_first+i]:NULL;}
const uint16_t *enemy_pixels(const EnemyPlane *p){return pfvalid(p)?(const uint16_t *)(const void *)(data+p->offset):NULL;}
const EnemySpan *enemy_support(const EnemyFrame *f,unsigned *count){if(count)*count=ffvalid(f)?f->span_count:0;return ffvalid(f)?&SPANS[f->span_first]:NULL;}
uint32_t enemy_raw_bound(void){return ENEMY_RAW_BYTES;}
uint32_t enemy_payload_bound(void){return ENEMY_PAYLOAD_BYTES;}
