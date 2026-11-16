#!/usr/bin/env python3
"""Offline v9 countdown assets; source PNGs are never rewritten."""
from pathlib import Path
import hashlib,json,struct,zlib
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT.parents[2]/'game_materials/Alabaster Dawn/05_ui/countdown_arrows'
OUT=ROOT/'resources/countdown'
TEST=ROOT/'tests/countdown_assets'
def sha(b):return hashlib.sha256(b).hexdigest()
def dump(p,s):p.write_text(s,encoding='utf-8')
def build():
 OUT.mkdir(parents=True,exist_ok=True);TEST.mkdir(parents=True,exist_ok=True)
 cfg=json.loads((SRC/'params.json').read_text(encoding='utf-8'))
 variants=['flying_W','flying_E','ground_S']; frames=[]; hashes={}; entries=[]
 for i,name in enumerate(variants):
  v=cfg['variants'][name];p=SRC/v['file'];im=Image.open(p).convert('RGBA');assert list(im.size)==v['size']
  frames.append((im,v['anchor_badge_center'],i,p))
 for i in range(16):
  p=SRC/'slot'/f'slot_{i:02}.png'
  if not p.exists():p=SRC.parent/'countdown_ground/slot'/p.name
  im=Image.open(p).convert('RGBA');assert im.size==(14,14)
  frames.append((im,[7,7],i,p))
 hashes[str(SRC/'params.json')]=sha((SRC/'params.json').read_bytes())
 planes=[];desc=[];raw=bytearray();gold=bytearray();spans=[];frame_spans=[]
 for im,anchor,source,p in frames:
  hashes[str(p)]=sha(p.read_bytes());w,h=im.size;px=list(im.getdata());qs=[(a*32+127)//255 for r,g,b,a in px]
  colors=[max(1,((r>>3)<<11)|((g>>2)<<5)|(b>>3)) if q else 0 for (r,g,b,a),q in zip(px,qs)]
  first=len(planes)
  for y in range(0,h,24):
   for x in range(0,w,24):
    tw=min(24,w-x);th=min(24,h-y)
    for q in sorted({qs[(y+yy)*w+x+xx] for yy in range(th) for xx in range(tw)}-{0}):
     off=len(raw)
     for yy in range(th):
      for xx in range(tw):
       k=(y+yy)*w+x+xx;raw.extend(struct.pack('<H',colors[k] if qs[k]==q else 0))
     planes.append((off,tw,th,x,y,q))
  sf=len(spans)
  # Support is exactly the final render-key support, including partial Q1..31.
  for yy in range(h):
   xx=0
   while xx<w:
    if not qs[yy*w+xx]:xx+=1;continue
    x0=xx
    while xx<w and qs[yy*w+xx]:xx+=1
    spans.append((yy,x0,xx))
  frame_spans.append((sf,len(spans)-sf))
  desc.append((w,h,anchor,first,len(planes)-first,source))
  gold.extend(b''.join(struct.pack('<HB',c,q) for c,q in zip(colors,qs)))
  entries.append(dict(source=str(p),source_sha256=hashes[str(p)],w=w,h=h,anchor_q8=[v*256 for v in anchor],planes=len(planes)-first))
 meta=bytearray().join(struct.pack('<IHHHHB3x',*v) for v in planes);fo=len(meta)
 for (w,h,(ax,ay),first,count,source),(sf,sc) in zip(desc,frame_spans):meta+=struct.pack('<HHHHiiIIIHBB',w,h,0,0,ax*256,ay*256,first,count,sf,sc,source,0)
 spanoff=len(meta)
 for span in spans:meta+=struct.pack('<HHH',*span)
 if len(meta)%4:meta+=bytes(4-len(meta)%4)
 payload=bytes(meta)+raw;co=zlib.compressobj(9,zlib.DEFLATED,-15);pack=co.compress(payload)+co.flush()
 (OUT/'countdown.bin').write_bytes(payload);(OUT/'countdown.pack').write_bytes(pack);(TEST/'golden.bin').write_bytes(gold)
 defines=dict(FRAME_COUNT=19,TILES=len(planes),FRAMES_OFFSET=fo,META_BYTES=len(meta),RAW_BYTES=len(raw),PAYLOAD_BYTES=len(payload),META_CRC32=zlib.crc32(meta),RAW_CRC32=zlib.crc32(payload),SPANS=len(spans),SPANS_OFFSET=spanoff)
 dump(OUT/'layout.h','/* Generated countdown kind6 id0 direct565 Q5 sparse24. */\n'+''.join(f'#define COUNTDOWN_{k} {v}u\n' for k,v in defines.items())+'#define COUNTDOWN_GROUP_COUNTS {3,16}\n#define COUNTDOWN_GROUP_OFFSETS {0,3}\n#define COUNTDOWN_SOURCE_INDICES {0,1,2,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}\n')
 c=(ROOT/'src/flow_data.c').read_text().replace('flow','countdown').replace('Flow','Countdown').replace('FLOW','COUNTDOWN')
 # source CRC authenticates exact immutable metadata; no runtime pixel conversion.
 c+='\nconst int16_t countdown_map_positions[3][2]={{115,365},{1165,365},{640,540}};\nconst int16_t countdown_screen_positions[3][2]={{115,415},{1165,415},{640,590}};\nconst int16_t countdown_heading_degrees[3]={270,90,180};\n'
 dxdy=[(v['dx'],v['dy']) for v in cfg['layout']['slot_offsets']]
 slots=[cfg['variants'][v]['scale_slots'] for v in variants]
 c+='const int8_t countdown_slot_dxdy[16][2]={'+','.join('{%d,%d}'%p for p in dxdy)+'};\n'
 c+='const uint8_t countdown_scale_slots[3][15]={'+','.join('{'+','.join(map(str,v))+'}' for v in slots)+'};\n'
 c+='int countdown_visible(uint32_t ms){return ms<COUNTDOWN_DURATION_MS;}\nuint8_t countdown_scale_alpha(uint32_t ms,unsigned i){unsigned past=ms/COUNTDOWN_SEGMENT_MS;if(i>=15||ms>=COUNTDOWN_DURATION_MS||i<past)return 0;if(i>past)return 255;return (uint8_t)(255u-(ms%1000u)*255u/1000u);}\n'
 dump(ROOT/'src/countdown_data.c',c)
 streams={'schema':1,'streams':[dict(kind=6,id=0,path='resources/countdown/countdown.pack',codec=1,predictor=0,raw_bytes=len(payload),raw_crc=zlib.crc32(payload),packed_crc=zlib.crc32(pack),decoded_sha256=sha(payload),param=0,alignment=4,archive_only=False)]}
 dump(OUT/'external_streams.json',json.dumps(streams,indent=2))
 # Actual runtime Q5/565 image, rather than a full-alpha source approximation.
 reconstructed=[]
 for im,anchor,source,p in frames:
  dst=Image.new('RGBA',im.size)
  dst.putdata([(((max(1,((r>>3)<<11)|((g>>2)<<5)|(b>>3))>>11)&31)*255//31,((max(1,((r>>3)<<11)|((g>>2)<<5)|(b>>3))>>5)&63)*255//63,(max(1,((r>>3)<<11)|((g>>2)<<5)|(b>>3))&31)*255//31,((a*32+127)//255)*255//32) for r,g,b,a in im.getdata()])
  reconstructed.append(dst)
 def sprite(canvas,i,center,elapsed):
  if elapsed>=15000:return
  a=frames[i][1];canvas.alpha_composite(reconstructed[i],(center[0]-a[0],center[1]-a[1]))
  for k,s in enumerate(slots[i]):
   past=elapsed//1000;alpha=0 if k<past else 255 if k>past else 255-(elapsed%1000)*255//1000
   if alpha:
    im=reconstructed[3+s].copy();im.putalpha(im.getchannel('A').point(lambda v:v*alpha//255));dx,dy=dxdy[s];canvas.alpha_composite(im,(center[0]+dx-7,center[1]+dy-7))
 contact=Image.new('RGBA',(750,380),(38,49,45,255));d=ImageDraw.Draw(contact)
 for col,elapsed in enumerate([0,500,14500,15000]):
  d.text((col*185+10,5),f'{elapsed}ms',fill='white')
  for i in range(3):sprite(contact,i,(col*185+90,65+i*105),elapsed)
 contact.convert('RGB').save(TEST/'contact.png')
 pm=json.loads((ROOT/'resources/player/manifest.json').read_text());mg=pm['groups']['map'];pb=(ROOT/'resources/player/player.pack').read_bytes();rb=zlib.decompress(pb[mg['pack_offset']:mg['pack_offset']+mg['compressed_bytes']],-15);assert sha(rb)==mg['raw_sha256']
 pixels=[( ((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31,255) for (v,) in struct.iter_unpack('<H',rb)]
 base=Image.new('RGBA',(1280,720));base.putdata(pixels)
 for i,center in enumerate([(115,415),(1165,415),(640,590)]):sprite(base,i,center,0)
 base.convert('RGB').save(TEST/'map_placement.png')
 manifest=dict(format='direct565Q5 sparse24tiles source alpha; key0 transparent, opaque black1',kind=6,id=0,codec=1,predictor=0,frames=entries,source_sha256=hashes,variants=variants,group_counts=[3,16],group_offsets=[0,3],map_positions=[[115,365],[1165,365],[640,540]],screen_positions=[[115,415],[1165,415],[640,590]],headings=[270,90,180],slot_dxdy=dxdy,scale_slots=slots,ring_radius=26,duration_ms=15000,segment_ms=1000,expired='all base and scales hidden; no fallback',animation='scalar alpha only; no rotation/shrink',source_params_timing_override='user confirmed 1000ms per segment replaces source legacy 1500ms',payload_bytes=len(payload),pixel_bytes=len(raw),metadata_bytes=len(meta),packed_bytes=len(pack),payload_crc32=zlib.crc32(payload),packed_crc32=zlib.crc32(pack),metadata_crc32=zlib.crc32(meta),payload_sha256=sha(payload),packed_sha256=sha(pack),base_map_sha256=sha(rb),support=dict(rule='row runs where final alpha_q5 > 0; includes Q1..31',spans=len(spans),span_bytes=6,span_offset=spanoff,frame_first_units='span elements'),font='not involved')
 dump(OUT/'manifest.json',json.dumps(manifest,indent=2));assert all(sha(Path(p).read_bytes())==v for p,v in hashes.items())
 dump(OUT/'README.md','# Countdown6\n\nGenerated by scripts/generate_countdown.py from actual v9 params and PNGs. BASE indices LEFT0=flying_W, RIGHT1=flying_E, BOTTOM2=ground_S. SCALE indices are absolute slot0..15, shared by all instances. Badge source anchors, not image centers. 15 seconds, one sequential scale fade per 1000ms, all sprites absent at expiry. Source timing is overridden by confirmed user direction. No font, source rewrite, rotation, resize, runtime pixel conversion, or runtime compression. Startup raw-deflate puff kind6 id0 predictor0. Use screen center = map center + (0,50); slot sprite origin=center+dxdy-(7,7). Multiply tile Q5 by scalar global fade in helper.\n')
 print(json.dumps({k:manifest[k] for k in ['payload_bytes','packed_bytes','payload_sha256','packed_sha256']}))
if __name__=='__main__':build()
