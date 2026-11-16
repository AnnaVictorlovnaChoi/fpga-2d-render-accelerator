#include "external_assets.h"
#include "flash_storage.h"
#include "puff.h"
#include <string.h>
#include <limits.h>
static ExternalEntry entries[EXTERNAL_MAX_ENTRIES];
static const uint8_t *source;
static uint32_t count,pack_bytes;
static int status;
static uint32_t le32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t le64(const uint8_t*p){return le32(p)|((uint64_t)le32(p+4)<<32);}
uint32_t external_crc32(const void *data,uint32_t n){const uint8_t*p=data;uint32_t c=~0u;while(n--){unsigned b;c^=*p++;for(b=0;b<8;b++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static int range(uint64_t off,uint64_t n,uint64_t max){return off<=max&&n<=max-off;}
static int header(const uint8_t*p,uint32_t capacity,uint32_t *total,uint32_t *n){uint8_t h[64];uint64_t bytes,dir;uint32_t crc;
 if(capacity<64||memcmp(p,"ADFLASH3",8)||le32(p+8)!=3||le32(p+12)!=64||le32(p+16)!=64)return -50;
 *n=le32(p+20);bytes=le64(p+40);dir=le64(p+24);
 if(!*n||*n>EXTERNAL_MAX_ENTRIES||bytes>capacity||bytes>FLASH_CAPACITY-FLASH_ASSET_BASE||bytes<64||dir!=64||!range(dir,(uint64_t)*n*64,bytes))return -51;
 if(le32(p+60)||le64(p+32)!=64+(uint64_t)*n*64||le64(p+32)>bytes)return -52;
 memcpy(h,p,64);crc=le32(h+56);memset(h+56,0,4);if(external_crc32(h,64)!=crc)return -53;*total=(uint32_t)bytes;return 0;}
static int validate_bind(const void*data,uint32_t size){const uint8_t*p=data;uint32_t total,n,i,j;int rc;
 source=0;count=pack_bytes=0;status=-1;if(!p)return -50;rc=header(p,size,&total,&n);if(rc)return status=rc;
 if(external_crc32(p+64,n*64)!=le32(p+48)||external_crc32(p+(uint32_t)le64(p+32),total-(uint32_t)le64(p+32))!=le32(p+52))return status=-54;
 for(i=0;i<n;i++){const uint8_t*d=p+64+i*64;ExternalEntry*e=&entries[i];
 e->kind=le32(d);e->id=le32(d+4);e->codec=le32(d+8);e->predictor=le32(d+12);e->offset=le64(d+16);e->packed=le64(d+24);e->raw=le64(d+32);e->alignment=le32(d+40);e->packed_crc=le32(d+44);e->raw_crc=le32(d+48);e->param=le32(d+52);
 if(!e->kind||e->kind>EXTERNAL_KIND_MAX||e->codec>1||e->predictor>1||le32(d+52)||le32(d+56)||le32(d+60)||!e->alignment||(e->alignment&(e->alignment-1))||e->alignment>4096||e->offset%e->alignment||e->offset<le64(p+32)||!range(e->offset,e->packed,total)||e->raw>0x05f00000u||e->packed>ULONG_MAX||e->raw>ULONG_MAX)return status=-55;
 if(!e->raw||!e->packed||(e->codec==0&&e->raw!=e->packed))return status=-56;
  if(e->kind==EXTERNAL_PRECOMPUTED&&(e->id!=0||e->codec!=1||e->predictor!=0))return status=-55;
  if(e->kind==EXTERNAL_WAVE&&(e->id>1||e->codec!=1||e->predictor!=0))return status=-55;
 for(j=0;j<i;j++){ExternalEntry*a=&entries[j];if(a->kind==e->kind&&a->id==e->id)return status=-57;if(e->packed&&a->packed&&e->offset<a->offset+a->packed&&a->offset<e->offset+e->packed)return status=-58;}
 if(external_crc32(p+(uint32_t)e->offset,(uint32_t)e->packed)!=e->packed_crc)return status=-59;
 }
 source=p;count=n;pack_bytes=total;status=1;return 0;
}
#if !defined(__riscv)
int external_assets_bind(const void*data,uint32_t capacity){return validate_bind(data,capacity);}
#endif
int external_assets_init(void){int rc;uint32_t total,n;uint8_t*p;
 if(status)return status==1?0:status;
#if defined(__riscv)
 extern unsigned char __external_stage_start[],__external_stage_end[];
 p=__external_stage_start;
 if((uintptr_t)__external_stage_end-(uintptr_t)p<EXTERNAL_STAGE_BYTES)return status=-60;
#else
 static uint8_t host_stage[EXTERNAL_STAGE_BYTES];p=host_stage;
#endif
 rc=flash_storage_init();if(!rc)rc=flash_storage_read(FLASH_ASSET_BASE,p,64);if(!rc)rc=header(p,EXTERNAL_STAGE_BYTES,&total,&n);if(!rc)rc=flash_storage_read(FLASH_ASSET_BASE+64,p+64,total-64);if(!rc)rc=validate_bind(p,total);
 flash_storage_seal();if(rc)status=rc;return rc;
}
const ExternalEntry*external_find(uint32_t kind,uint32_t id){uint32_t i;if(status!=1)return 0;for(i=0;i<count;i++)if(entries[i].kind==kind&&entries[i].id==id)return &entries[i];return 0;}
const uint8_t*external_source(uint32_t kind,uint32_t id){const ExternalEntry*e=external_find(kind,id);return e?source+(uint32_t)e->offset:0;}
const uint8_t*external_get(uint32_t kind){return external_source(kind,0);}
int external_decode(uint32_t kind,uint32_t id,void*target,uint32_t bytes){const ExternalEntry*e;unsigned long in,out;int rc=external_assets_init();if(rc)return rc;e=external_find(kind,id);if(!e||!target||e->raw!=bytes){source=0;count=0;return status=-61;}
#if defined(__riscv)
 {uintptr_t d=(uintptr_t)target;extern unsigned char __external_stage_start[];
 if(d<0x1000u||d>(uintptr_t)__external_stage_start||bytes>(uintptr_t)__external_stage_start-d){source=0;count=0;return status=-64;}}
#endif
 in=(unsigned long)e->packed;out=bytes;if(e->codec==0){memcpy(target,source+(uint32_t)e->offset,bytes);rc=0;}else rc=puff(target,&out,source+(uint32_t)e->offset,&in);
 if(rc||in!=e->packed||out!=e->raw){source=0;count=0;return status=-62;}
 if(external_crc32(target,bytes)!=e->raw_crc){source=0;count=0;return status=-63;}
 return 0;}
uint32_t external_pack_bytes(void){return pack_bytes;}
