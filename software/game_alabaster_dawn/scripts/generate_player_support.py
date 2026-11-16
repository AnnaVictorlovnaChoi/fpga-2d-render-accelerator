"""Offline key565 -> immutable row intervals. Runtime never scans a pixel/bit mask."""
from pathlib import Path
import json,zlib,hashlib,struct
R=Path(__file__).resolve().parents[1]
def main():
 m=json.loads((R/'resources/player/manifest.json').read_text());pack=(R/'resources/player/player.pack').read_bytes()
 spans=[];rows=[];frames=[];starts=[];checks=0
 for gi,g in enumerate(m['groups'].values()):
  starts.append(len(frames))
  if gi<2:continue
  raw=zlib.decompress(pack[g['pack_offset']:g['pack_offset']+g['compressed_bytes']],-15)
  for f in g['frames']:
   frames.append(len(rows));pix=struct.unpack('<%dH'%(f['w']*f['h']),raw[f['offset']:f['offset']+f['bytes']])
   for y in range(f['h']):
    rows.append(len(spans));x=0
    while x<f['w']:
     if not pix[y*f['w']+x]:x+=1;continue
     left=x
     while x<f['w'] and pix[y*f['w']+x]:x+=1
     spans.append((left,x));checks+=x-left
   rows.append(len(spans))
 h='''#ifndef AD_PLAYER_SUPPORT_H
#define AD_PLAYER_SUPPORT_H
#include "runtime.h"
typedef struct { uint16_t x0,x1; } PlayerSpan;
int player_support_init(void);
/* Lookup one immutable row, then intersect intervals, never inspect pixels. */
const PlayerSpan *player_support_row(const RuntimeGroup*,const RuntimeFrame*,int y,unsigned *count);
#endif
'''
 c='#include "player_support.h"\n'
 c+='static const PlayerSpan spans[]={'+','.join('{%d,%d}'%s for s in spans)+'};\n'
 for name,a in [('rows',rows),('frames',frames),('starts',starts)]:c+='static const uint32_t '+name+'[]={'+','.join(map(str,a))+'};\n'
 c+='''int player_support_init(void){return 0;}
const PlayerSpan *player_support_row(const RuntimeGroup*g,const RuntimeFrame*f,int y,unsigned*n){
 *n=0;if(!g||!f||y<0||y>=f->h)return 0;
 for(unsigned i=2;i<runtime_group_count();i++)if(g==runtime_group_at(i)){
  for(unsigned j=0;j<g->count;j++)if(f==g->frames+j){unsigned r=frames[starts[i]+j]+(unsigned)y;*n=rows[r+1]-rows[r];return spans+rows[r];}return 0;}return 0;
}
'''
 (R/'src/player_support.h').write_text(h);(R/'src/player_support.c').write_text(c)
 report=dict(spans=len(spans),rows=len(rows),frames=len(frames),metadata_bytes=len(spans)*4+4*(len(rows)+len(frames)+len(starts)),visible_pixels=checks,production_pack_sha256=hashlib.sha256(pack).hexdigest())
 print(json.dumps(report))
if __name__=='__main__':main()
