"""G2.9.2: 21 runtime frames, all72 archival RGBA; offline sparse Q5 tiles.
Never invoke the stale source generator. No app incbin or build changes.
"""
from pathlib import Path
from PIL import Image
import json,struct,zlib,hashlib
PROJECT=Path(__file__).resolve().parents[1]
ROOT=PROJECT.parents[2]/'game_materials/Alabaster Dawn/06_dragons/luminir/fpga_v1'
OUT=PROJECT/'resources/dragon'
GROUPS=('BODY','HALO','GROW','LOOP')
SELECTION={g:([0,3,6,9,12,15] if g=='LOOP' else [0,4,8,13,17]) for g in GROUPS}
def sha(b): return hashlib.sha256(b).hexdigest()
def compress(b):
 c=zlib.compressobj(9,zlib.DEFLATED,-15); return c.compress(b)+c.flush()
def main():
 OUT.mkdir(parents=True,exist_ok=True)
 source=json.loads((ROOT/'manifest.json').read_text()); frames=[];tiles=[];spans=[];raw=bytearray();rgba=bytearray();fullbytes=0;fullcmd=0;drop=0;maxerr=0
 for rel,expected in source['source_fingerprints'].items():
  assert sha((ROOT.parent/'ingame_v3'/rel).read_bytes())==expected, 'source changed: '+rel
 archive_frames=[]
 for group in GROUPS:
  for idx in range(18):
   f=dict(next(f for f in source['frames'] if f['group']==group and f['index']==idx))
   image=Image.open(ROOT/f['png']).convert('RGBA'); b=image.tobytes()
   assert sha((ROOT/f['png']).read_bytes())==f['png_sha256']
   f.update(rgba_offset=len(rgba),rgba_bytes=len(b),rgba_sha256=sha(b));rgba.extend(b);archive_frames.append(f)
 for group in GROUPS:
  for runtime_index,idx in enumerate(SELECTION[group]):
   f=dict(next(f for f in source['frames'] if f['group']==group and f['index']==idx))
   png=(ROOT/f['png']).read_bytes(); assert sha(png)==f['png_sha256']
   original=(ROOT.parent/'ingame_v3'/f['source']).read_bytes(); assert sha(original)==f['source_sha256']
   im=Image.open(ROOT/f['png']).convert('RGBA'); orig=Image.open(ROOT.parent/'ingame_v3'/f['source']).convert('RGBA')
   assert orig.getchannel('A').getbbox()==(f['source_x'],f['source_y'],f['source_x']+f['w'],f['source_y']+f['h'])
   assert orig.crop(orig.getchannel('A').getbbox()).tobytes()==im.tobytes()
   pix=list(im.getdata()); aq=[(p[3]*32+127)//255 for p in pix]; rgb=[((((r>>3)<<11)|((g>>2)<<5)|(b>>3)) or 1) for r,g,b,a in pix]
   arch=next(a for a in archive_frames if a['group']==group and a['index']==idx)
   f.update(plane_first=len(tiles),span_first=len(spans),rgba_offset=arch['rgba_offset'],runtime_index=runtime_index)
   f['quantized_zero_nonzero_source']=sum(p[3]>0 and q==0 for p,q in zip(pix,aq));drop+=f['quantized_zero_nonzero_source']
   maxerr=max(maxerr,max(abs(q*255/32-p[3]) for p,q in zip(pix,aq)))
   ffull=0; fguard=0; fbboxpixels=0
   for q in sorted(set(aq)-{0}):
    ids=[i for i,v in enumerate(aq) if v==q]; x0=min(i%im.width for i in ids);x1=max(i%im.width for i in ids)+1;y0=min(i//im.width for i in ids);y1=max(i//im.width for i in ids)+1
    fullbytes+=(x1-x0)*(y1-y0)*2; ffull+=((x1-x0+23)//24)*((y1-y0+23)//24);fullcmd+=1; fbboxpixels+=(x1-x0)*(y1-y0)
    for yy in range(y0,y1,24):
     for xx in range(x0,x1,24):
      occupied=[(x,y) for y in range(yy,min(yy+24,y1)) for x in range(xx,min(xx+24,x1)) if aq[y*im.width+x]==q]
      if not occupied: continue
      tx=min(x for x,y in occupied);ty=min(y for x,y in occupied);ex=max(x for x,y in occupied)+1;ey=max(y for x,y in occupied)+1
      tile=dict(offset=len(raw),w=ex-tx,h=ey-ty,x=tx,y=ty,alpha_q5=q);tiles.append(tile)
      for y in range(ty,ey):
       for x in range(tx,ex):
        i=y*im.width+x;raw.extend(struct.pack('<H',rgb[i] if aq[i]==q else 0))
      fguard+=(ex-tx)*(ey-ty)
   for y in range(im.height):
    x=0
    while x<im.width:
     if not aq[y*im.width+x]:x+=1;continue
     start=x
     while x<im.width and aq[y*im.width+x]:x+=1
     spans.append((y,start,x))
   f.update(plane_count=len(tiles)-f['plane_first'],span_count=len(spans)-f['span_first'],full_bbox_guard24_commands=ffull,full_bbox_guard_pixels=fbboxpixels,sparse24_commands=len(tiles)-f['plane_first'],sparse24_guard_pixels=fguard)
   frames.append(f)
 metadata=bytearray()
 for p in tiles:metadata+=struct.pack('<IHHHHB3x',p['offset'],p['w'],p['h'],p['x'],p['y'],p['alpha_q5'])
 for f in frames:metadata+=struct.pack('<HHHHiiIIIHBB',f['w'],f['h'],f['source_x'],f['source_y'],f['anchor_x_q8'],f['anchor_y_q8'],f['plane_first'],f['plane_count'],f['span_first'],f['span_count'],f['index'],0)
 for s in spans:metadata+=struct.pack('<HHH',*s)
 while len(metadata)%4:metadata+=b'\0'
 payload=metadata+raw
 # Separate streams let loader initialize render data without ever loading archival RGBA.
 files={}
 for name,data in [('render',payload),('rgba',rgba)]:
  packed=compress(data);assert zlib.decompress(packed,-15)==data
  (OUT/(name+'.deflate.bin')).write_bytes(packed)
  files[name]=dict(file=name+'.deflate.bin',raw_bytes=len(data),compressed_bytes=len(packed),sha256=sha(data),compressed_sha256=sha(packed),codec='raw-deflate',prediction='none',raw_crc=zlib.crc32(data)&0xffffffff,packed_crc=zlib.crc32(packed)&0xffffffff)
 streams=[dict(kind=4,id=0 if name=='render' else 1,path='resources/dragon/'+info['file'],codec=1,predictor=0,raw_bytes=info['raw_bytes'],raw_crc=info['raw_crc'],packed_crc=info['packed_crc'],decoded_sha256=info['sha256'],param=0,alignment=4,archive_only=name=='rgba') for name,info in files.items()]
 (OUT/'external_streams.json').write_text(json.dumps(dict(schema=1,streams=streams),indent=2))
 (OUT/'metadata.bin').write_bytes(metadata)
 (OUT/'layout.h').write_text('/* Generated by scripts/generate_dragon.py; no embedded pixels. */\n'+''.join('#define DRAGON_'+k+' '+str(v)+'u\n' for k,v in dict(FRAME_COUNT=len(frames),TILES=len(tiles),SPANS=len(spans),FRAMES_OFFSET=len(tiles)*16,SPANS_OFFSET=len(tiles)*16+len(frames)*32,META_BYTES=len(metadata),RAW_BYTES=len(raw),PAYLOAD_BYTES=len(payload)).items()))
 counts=[len(SELECTION[g]) for g in GROUPS];offsets=[sum(counts[:i]) for i in range(len(GROUPS))]
 with (OUT/'layout.h').open('a') as h:
  for name,values in [('GROUP_COUNTS',counts),('GROUP_OFFSETS',offsets),('SOURCE_INDICES',[f['index'] for f in frames])]:
   h.write('#define DRAGON_'+name+' {'+','.join(map(str,values))+'}\n')
 combos=[dict(loop=i,sparse24_commands=frames[4]['plane_count']+frames[9]['plane_count']+frames[15+i]['plane_count'],full_bbox_guard24_commands=frames[4]['full_bbox_guard24_commands']+frames[9]['full_bbox_guard24_commands']+frames[15+i]['full_bbox_guard24_commands'],sparse_guard_pixels=frames[4]['sparse24_guard_pixels']+frames[9]['sparse24_guard_pixels']+frames[15+i]['sparse24_guard_pixels'],full_bbox_guard_pixels=frames[4]['full_bbox_guard_pixels']+frames[9]['full_bbox_guard_pixels']+frames[15+i]['full_bbox_guard_pixels']) for i in range(6)]
 manifest=dict(schema=292,selection=SELECTION,group_counts=counts,group_offsets=offsets,archive_frame_count=len(archive_frames),archive_selection={g:list(range(18)) for g in GROUPS},archive_frames=archive_frames,archive_policy='all72 original tight RGBA lossless raw-deflate; archive-only id1, never loaded into DDR',runtime_index_policy='compressed group-local index; source_index remains original frame index',alpha_policy='q=(a*32+127)//255; q0 omitted, q32 keyed opaque, q1..31 AlphaSprite; no dither; raw RGBA archive retains exact alpha',quantized_zero_nonzero_source=drop,max_alpha_error_255=maxerr,files=files,raw_bytes=len(raw),metadata_raw_bytes=len(metadata),payload_bytes=len(payload),plane_count=len(tiles),frame_count=len(frames),span_count=len(spans),frame_stride=32,tile_stride=16,span_stride=6,frames_offset=len(tiles)*16,spans_offset=len(tiles)*16+len(frames)*32,pixels_offset=len(metadata),full_plane_bbox_raw_bytes=fullbytes,full_plane_count=fullcmd,render_ddr_bytes=len(payload),archive_rgba_ddr_required=0,staging_peak_bytes=max(v['compressed_bytes'] for v in files.values()),dragon_flash_bytes=sum(v['compressed_bytes'] for v in files.values()),worst_hold_combo=max(combos,key=lambda c:c['sparse24_commands']),hold_combinations=combos,source_fingerprints=source['source_fingerprints'],frames=frames,planes=tiles,spans=spans)
 assert manifest['dragon_flash_bytes']<30*1024**2;assert manifest['staging_peak_bytes']<16*1024**2
 (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2))
 print(json.dumps({k:v for k,v in manifest.items() if k not in ('frames','planes','spans','source_fingerprints','hold_combinations')},indent=2))
if __name__=='__main__':main()
