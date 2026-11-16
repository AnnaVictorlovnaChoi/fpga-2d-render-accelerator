#!/usr/bin/env python3
"""Offline G2.6-reference RGB565 patches. Runtime performs startup lossless decode only.
Source artwork is inherited unchanged; no import/regeneration of original controls.
"""
import argparse, ctypes, hashlib, json, subprocess, zlib
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'resources/scene'
TEST=ROOT/'tests/gpu_scene_assets'
NAMES=['wall_rise_A','wall_rise_B','wall_rise_C','wall_rise_D','pit_collapse','pit_restore','walllever','traplever','hint']
COUNTS=[8,8,8,8,11,11,2,2,1]
POS=[(60,171),(840,171),(60,408),(760,408),(330,300),(330,300),(746,121),(836,121),(754,101)]
def sha(b):return hashlib.sha256(b).hexdigest()
def dump(p,s):
 p.parent.mkdir(parents=True,exist_ok=True)
 if isinstance(s,bytes):p.write_bytes(s)
 else:p.write_text(s,encoding='utf-8')
def blend(base,rgba):
 d=base.astype(np.uint32);s=rgba.astype(np.uint32);a=s[:,:,3]
 r=(((d>>11)&31)*255//31);g=(((d>>5)&63)*255//63);b=(d&31)*255//31
 r=(s[:,:,0]*a+r*(255-a)+127)//255;g=(s[:,:,1]*a+g*(255-a)+127)//255;b=(s[:,:,2]*a+b*(255-a)+127)//255
 v=((r>>3)<<11)|((g>>2)<<5)|(b>>3)
 return np.where(a,v,d).astype('<u2')
def packed(b):
 c=zlib.compressobj(9,zlib.DEFLATED,-15);return c.compress(b)+c.flush()
def runs(mask):
 out=[]
 for row in mask:
  v=np.flatnonzero(np.diff(np.r_[False,row,False].astype(np.int8)))
  out.append([(int(x),int(e-x)) for x,e in zip(v[::2],v[1::2])])
 return out
def rectangles(rows):
 result=[];active={}
 for y,row in enumerate(rows+[[]]):
  current=set(row)
  for key,start in list(active.items()):
   if key not in current:result.append((*key[:1],start,key[1],y-start));del active[key]
  for key in current:
   if key not in active:active[key]=y
 return result
HEADER='''/* Generated offline opaque RGB565; zero is valid black, no color key. */
#ifndef SCENE_DATA_H
#define SCENE_DATA_H
#include <stdint.h>
typedef struct { uint16_t w,h,count; } SceneAsset;
typedef struct { uint16_t x,w; } SceneSpan;
typedef struct { uint16_t x,y,w,h; } SceneMaskRect;
enum { SCENE_WALL_A,SCENE_WALL_B,SCENE_WALL_C,SCENE_WALL_D,SCENE_PIT_COLLAPSE,SCENE_PIT_RESTORE,SCENE_WALL_LEVER,SCENE_TRAP_LEVER,SCENE_HINT,SCENE_ASSET_COUNT };
extern const SceneAsset scene_assets[SCENE_ASSET_COUNT];
/* Startup-only puff plus reversible byte transforms; no RGBA or runtime render.
 * Static arena, no heap. NULL getters before successful init/invalid args.
 * Hint frames 0/1 select left/right base positions (754/844,101).
 * Wall patch must copy only mask support rects, not whole alpha-zero holes.
 * Restore pit aliases reversed collapse. Opaque black is never changed to 1. */
int scene_assets_init(void);
const uint16_t *scene_patch(unsigned asset,unsigned frame);
const uint16_t *scene_rgb565(unsigned asset,unsigned frame);
const SceneSpan *scene_mask_span(unsigned wall,unsigned frame,unsigned row,unsigned *count);
const SceneMaskRect *scene_mask_rects(unsigned wall,unsigned frame,unsigned *count);
#endif
'''
def generate():
 manifest=json.loads((ROOT/'resources/player/manifest.json').read_text())
 mg=manifest['groups']['map'];pb=(ROOT/'resources/player/player.pack').read_bytes();rawmap=zlib.decompress(pb[mg['pack_offset']:mg['pack_offset']+mg['compressed_bytes']],-15)
 assert sha(rawmap)==mg['raw_sha256'];base=np.frombuffer(rawmap,dtype='<u2').reshape(720,1280)
 blob=bytearray();groups=[];sources=[];gold=[];spanflat=[];rowmeta=[];rectflat=[];rectmeta=[];offset=0;previous=None
 for a,name in enumerate(NAMES):
  imgs=[]
  for f in range(COUNTS[a]):
   p=OUT/'source'/name/f'{f:02}.png';b=p.read_bytes();im=np.asarray(Image.open(p).convert('RGBA'));imgs.append(im);sources.append(dict(path=str(p.relative_to(OUT)).replace('\\','/'),sha256=sha(b)))
  if a==5:
   assert all(np.array_equal(v,collapse[-1-i]) for i,v in enumerate(imgs));groups.append(dict(asset=a,w=620,h=140,count=11,offset=groups[4]['offset'],raw=0,src=len(blob),packed=0,mode=4,ref=4,sha256='reverse alias'));gold.append(gold[4][::-1]);continue
  if a==4:collapse=imgs
  frames=[]
  if a==8:imgs=imgs*2
  for f,im in enumerate(imgs):
   h,w=im.shape[:2];x,y=POS[a]
   if a==8:x+=f*90
   patch=blend(base[y:y+h,x:x+w],im);frames.append(patch.tobytes())
   if a<4:
    alpha=im[:,:,3];yy,xx=np.indices(alpha.shape);partial=(alpha>0)&(alpha<255)
    assert not np.any(partial & (xx+x>=330)&(xx+x<950)&(yy+y>=300)&(yy+y<440)), 'partial wall alpha intersects pit: combinations required'
    rows=runs(alpha>0)
    for rr in rows:rowmeta.append((len(spanflat),len(rr)));spanflat.extend(rr)
    rects=rectangles(rows);rectmeta.append((len(rectflat),len(rects)));rectflat.extend(rects)
  raw=b''.join(frames);data=np.frombuffer(raw,dtype=np.uint8);stride=w*2;fb=w*h*2
  candidates=[(0,0,packed(raw))]
  # Byte predictors are lossless and reversed only once during initialization.
  left=data.copy();left.reshape(-1,stride)[:,2:]=(data.reshape(-1,stride)[:,2:].astype(np.int16)-data.reshape(-1,stride)[:,:-2])&255;candidates.append((1,0,packed(left.tobytes())))
  temporal=data.copy()
  if len(data)>fb:temporal[fb:]=(data[fb:].astype(np.int16)-data[:-fb])&255
  candidates.append((2,0,packed(temporal.tobytes())))
  if previous is not None and len(previous)==len(raw):
   delta=(data.astype(np.int16)-np.frombuffer(previous,dtype=np.uint8))&255;candidates.append((3,a-1,packed(delta.astype(np.uint8).tobytes())))
  mode,ref,p=min(candidates,key=lambda v:len(v[2]));groups.append(dict(asset=a,w=w,h=h,count=len(imgs),offset=offset,raw=len(raw),src=len(blob),packed=len(p),mode=mode,ref=ref,sha256=sha(raw)));blob.extend(p);offset+=len(raw);gold.append(frames);previous=raw
 dump(ROOT/'src/scene_data.h',HEADER)
 c='''/* Generated offline, only startup lossless inverse predictors. */
#include "scene_data.h"
#include "puff.h"
#include <stddef.h>
extern const unsigned char scene_blob_start[];
extern const unsigned char scene_blob_end[];
'''
 c+='const SceneAsset scene_assets[9]={'+','.join('{%d,%d,%d}'%(g['w'],g['h'],g['count']) for g in groups)+'};\n'
 c+='typedef struct {unsigned long dst,raw,src,packed; unsigned mode,ref;} Group;\nstatic const Group groups[9]={'+','.join('{%d,%d,%d,%d,%d,%d}'%(g['offset'],g['raw'],g['src'],g['packed'],g['mode'],g['ref']) for g in groups)+'};\n'
 c+='_Alignas(64) static unsigned char arena[%d];\nstatic int ready;\n'%offset
 for typ,name,vals in [('SceneSpan','spans',spanflat),('SceneMaskRect','rects',rectflat),('unsigned','rows',rowmeta),('unsigned','rectindex',rectmeta)]:
  if typ=='unsigned':c+='static const unsigned %s[][2]={'%name
  else:c+='static const %s %s[]={'%(typ,name)
  c+=','.join('{'+','.join(map(str,v))+'}' for v in vals)+'};\n'
 c+='''int scene_assets_init(void){
 unsigned a; if(ready)return 0;
 for(a=0;a<9;++a){const Group*g=&groups[a];unsigned long n=g->raw,used=g->packed,i;
 unsigned char*d=arena+g->dst;unsigned long stride=scene_assets[a].w*2UL,frame=stride*scene_assets[a].h;
 if(g->mode==4)continue;
 if(g->dst>sizeof arena||g->raw>sizeof arena-g->dst)return -20;
 if(g->src>(unsigned long)(scene_blob_end-scene_blob_start)||g->packed>(unsigned long)(scene_blob_end-scene_blob_start)-g->src)return -21;
 if(puff(d,&n,scene_blob_start+g->src,&used)||n!=g->raw||used!=g->packed)return -(int)(a+1);
 for(i=0;i<n;++i){if(g->mode==1 && i%stride>=2)d[i]=(unsigned char)(d[i]+d[i-2]);
 else if(g->mode==2 && i>=frame)d[i]=(unsigned char)(d[i]+d[i-frame]);
 else if(g->mode==3)d[i]=(unsigned char)(d[i]+arena[groups[g->ref].dst+i]);}
 }ready=1;return 0;
}
const uint16_t*scene_patch(unsigned a,unsigned f){if(!ready||a>=9||f>=scene_assets[a].count)return NULL;if(a==5)f=10-f;return (const uint16_t*)(arena+groups[a].dst+f*(unsigned long)scene_assets[a].w*scene_assets[a].h*2);}
const uint16_t*scene_rgb565(unsigned a,unsigned f){return scene_patch(a,f);}
const SceneSpan*scene_mask_span(unsigned a,unsigned f,unsigned row,unsigned*count){unsigned k;if(count)*count=0;if(a>=4||f>=8||row>=84||!count)return NULL;k=(a*8+f)*84+row;*count=rows[k][1];return spans+rows[k][0];}
const SceneMaskRect*scene_mask_rects(unsigned a,unsigned f,unsigned*count){unsigned k;if(count)*count=0;if(a>=4||f>=8||!count)return NULL;k=a*8+f;*count=rectindex[k][1];return rects+rectindex[k][0];}
'''
 dump(ROOT/'src/scene_data.c',c);dump(OUT/'scene.pack',bytes(blob))
 dump(ROOT/'src/scene_blob.S','.section .rodata.scene_blob,"a"\n.balign 16\n.global scene_blob_start\n.global scene_blob_end\nscene_blob_start:\n.incbin "resources/scene/scene.pack"\nscene_blob_end:\n')
 report=dict(format='offline G2.6 integer reference blend to direct opaque RGB565; startup puff inverse predictors only',groups=groups,sources=sources,base_map_sha256=sha(rawmap),scene_pack_sha256=sha(blob),budget=dict(packed_bytes=len(blob),metadata_span_bytes=len(spanflat)*4+len(rowmeta)*8+len(rectflat)*8+len(rectmeta)*8,arena_bytes=offset,scene_limit=900000,within_scene_limit=len(blob)+len(spanflat)*4+len(rowmeta)*8+len(rectflat)*8+len(rectmeta)*8<=900000),partial_alpha_pit_disjoint=True,max_mask_rects=max(v[1] for v in rectmeta))
 dump(OUT/'manifest.json',json.dumps(report,indent=2)+'\n');print(json.dumps(report['budget']));print('max wall rects',report['max_mask_rects']);return groups,gold,base

def verify(groups,gold,base):
 TEST.mkdir(parents=True,exist_ok=True)
 dump(TEST/'blob.S','.section .rdata,"dr"\n.balign 16\n.global scene_blob_start\n.global scene_blob_end\nscene_blob_start:\n.incbin "resources/scene/scene.pack"\nscene_blob_end:\n')
 lib=TEST/'scene_test.dll';subprocess.run(['gcc','-std=c11','-O2','-shared','-I','src','src/scene_data.c','src/puff.c',str(TEST/'blob.S'),'-o',str(lib)],cwd=ROOT,check=True)
 api=ctypes.CDLL(str(lib));api.scene_patch.restype=ctypes.c_void_p
 assert not api.scene_patch(0,0);assert api.scene_assets_init()==0;assert api.scene_assets_init()==0
 checked=0
 for g,frames in zip(groups,gold):
  for f,raw in enumerate(frames):assert ctypes.string_at(api.scene_patch(g['asset'],f),len(raw))==raw;checked+=len(raw)
  assert not api.scene_patch(g['asset'],g['count'])
 assert not api.scene_patch(9,0)
 # Independently validate integer scalar blend samples, including alpha zero.
 for a in range(9):
  im=np.asarray(Image.open(OUT/'source'/NAMES[a]/'00.png').convert('RGBA'));x,y=POS[a];h,w=im.shape[:2];raw=gold[a][0];v=np.frombuffer(raw,dtype='<u2').reshape(h,w)
  for yy in range(0,h,7):
   for xx in range(0,w,11):
    d=int(base[y+yy,x+xx]);r,g,b,al=map(int,im[yy,xx]);rr=(((d>>11)&31)*255//31);gg=(((d>>5)&63)*255//63);bb=(d&31)*255//31
    expected=d if not al else (((r*al+rr*(255-al)+127)//255>>3)<<11)|(((g*al+gg*(255-al)+127)//255>>2)<<5)|((b*al+bb*(255-al)+127)//255>>3)
    assert int(v[yy,xx])==expected
 # Compare C-returned mask geometry to every original alpha pixel.
 class Rect(ctypes.Structure):_fields_=[('x',ctypes.c_uint16),('y',ctypes.c_uint16),('w',ctypes.c_uint16),('h',ctypes.c_uint16)]
 class Span(ctypes.Structure):_fields_=[('x',ctypes.c_uint16),('w',ctypes.c_uint16)]
 api.scene_mask_rects.restype=ctypes.POINTER(Rect);api.scene_mask_span.restype=ctypes.POINTER(Span)
 combinations=0
 for a in range(4):
  x,y=POS[a]
  for f in range(8):
   im=np.asarray(Image.open(OUT/'source'/NAMES[a]/f'{f:02}.png').convert('RGBA'));h,w=im.shape[:2];mask=np.zeros((h,w),dtype=bool);n=ctypes.c_uint()
   rr=api.scene_mask_rects(a,f,ctypes.byref(n))
   for i in range(n.value):
    r=rr[i];mask[r.y:r.y+r.h,r.x:r.x+r.w]=True
   assert np.array_equal(mask,im[:,:,3]>0)
   for row in range(h):
    sr=api.scene_mask_span(a,f,row,ctypes.byref(n));m=np.zeros(w,dtype=bool)
    for i in range(n.value):m[sr[i].x:sr[i].x+sr[i].w]=True
    assert np.array_equal(m,mask[row])
   # All collapse states plus OFF; restore is independently reverse-verified.
   for pf in range(-1,11):
    under=base[y:y+h,x:x+w].copy()
    if a>=2 and pf>=0:
     pit=np.frombuffer(gold[4][pf],dtype='<u2').reshape(140,620)
     ix0=max(x,330);ix1=min(x+w,950)
     under[:32,ix0-x:ix1-x]=pit[108:140,ix0-330:ix1-330]
    expected=blend(under,im);actual=under.copy();patch=np.frombuffer(gold[a][f],dtype='<u2').reshape(h,w);actual[mask]=patch[mask]
    assert np.array_equal(expected,actual),'pit/wall reference mismatch'
    combinations+=1
 # Compile an independent actual-C decoder against invalid raw-deflate bytes.
 bad=bytearray((OUT/'scene.pack').read_bytes());bad[groups[4]['src']]=7
 dump(TEST/'bad.pack',bytes(bad));dump(TEST/'bad.S',(TEST/'blob.S').read_text().replace('resources/scene/scene.pack','tests/gpu_scene_assets/bad.pack'))
 badlib=TEST/'scene_bad_test.dll';subprocess.run(['gcc','-std=c11','-O2','-shared','-I','src','src/scene_data.c','src/puff.c',str(TEST/'bad.S'),'-o',str(badlib)],cwd=ROOT,check=True)
 badapi=ctypes.CDLL(str(badlib));badapi.scene_patch.restype=ctypes.c_void_p
 assert badapi.scene_assets_init()==-5;assert not badapi.scene_patch(0,0)
 report=dict(status='PASS',actual_C_decoder='src/scene_data.c + src/puff.c',bytes_compared=checked,frames_compared=sum(map(len,gold)),preinit_bounds_idempotence='PASS',scalar_reference_blend='PASS',partial_alpha_pit_disjoint='PASS',mask_geometry='every alpha pixel versus actual C span/rect getters PASS',wall_pit_combinations=combinations,corrupt_stream='actual C returns -5 and getters remain NULL')
 dump(TEST/'verification.json',json.dumps(report,indent=2)+'\n');print(json.dumps(report))
def main():
 p=argparse.ArgumentParser();p.add_argument('--verify',action='store_true');a=p.parse_args();g,gold,base=generate()
 if a.verify:verify(g,gold,base)
if __name__=='__main__':main()
