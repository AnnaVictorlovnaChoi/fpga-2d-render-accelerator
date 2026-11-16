"""Offline key0 support rows for six resident textures; no runtime pixel scan."""
from pathlib import Path
import json,re,struct,zlib
ROOT=Path(__file__).resolve().parents[1]
def main():
 generated=(ROOT/'src/assets_generated.c').read_text('utf-8')
 ids=[int(x) for x in re.search(r'asset_cache_images\[6\]\s*=\s*\{([^}]+)',generated)[1].split(',') if x.strip()]
 section=re.search(r'asset_images\[\]\s*=\s*\{(.*?)\};',generated,re.S)[1]
 images=[[int(x) for x in row.split(',')] for row in re.findall(r'\{([^{}]+)\}',section)]
 manifest=json.loads((ROOT/'resources/external_assets.manifest.json').read_text('utf-8'))
 e=next(e for e in manifest['entries'] if e['id']==0);raw=zlib.decompress((ROOT/e['path']).read_bytes(),-15)
 rows=[]
 for cache_index,i in enumerate(ids):
  off,_,w,h,*_=images[i];assert w<=32 and h<=32
  values=struct.unpack_from('<'+'H'*(w*h),raw,off*2)
  # G2.5 three RGB ball rectangles use COPY/ALPHA: black is opaque content,
  # not a transparency key. Only crystal/snow SPRITE textures test key0.
  rr=[((1<<w)-1) if cache_index<3 else sum((1<<x) for x in range(w) if values[y*w+x]) for y in range(h)]
  rows.append(rr+[0]*(32-h))
 lines=['/* Generated RGB565 !=0 row support for exact alpha object counts. */','#ifndef G24_CACHE_SUPPORT_H','#define G24_CACHE_SUPPORT_H','#include <stdint.h>','static const uint32_t alpha_sprite_rows[6][32] = {']
 for rr in rows:lines.append('{'+','.join(f'0x{x:08x}u' for x in rr)+'},')
 lines+=['};','#endif']
 (ROOT/'src/alpha_pixel_masks.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
 print('PASS: six key0 support masks generated offline')
if __name__=='__main__':main()
