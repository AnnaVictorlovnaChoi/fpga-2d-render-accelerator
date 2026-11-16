#!/usr/bin/env python3
"""FLOW5 offline direct565 Q5 sparse24tiles; reads accepted derived RGBA only."""
from pathlib import Path
import hashlib,json,struct,zlib
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
PC=ROOT.parents[2]/'game_materials/Alabaster Dawn/07_maps/start_over_pc_v2'
OUT=ROOT/'resources/flow'
FONT=Path('C:/Windows/Fonts/msyh.ttc')
def sha(b):return hashlib.sha256(b).hexdigest()
def word(r,g,b):return (r>>3)<<11|(g>>2)<<5|(b>>3)
def build():
 cfg=json.loads((PC/'alpha_preparation.json').read_text()); frames=[]; hashes={}; sources={}
 for group in ('altar','death'):
  for i,m in enumerate(cfg[group]):
   p=PC/Path(m['file'].replace('\\','/')); b=p.read_bytes(); im=Image.open(p).convert('RGBA')
   hashes[str(p)]=sha(b); sources[str(p)]=sha(im.tobytes()); frames.append((im,m['anchor'],i))
 font=ImageFont.truetype(str(FONT),16)
 text='按J开始游戏'; width=int(ImageDraw.Draw(Image.new('RGB',(1,1))).textlength(text,font=font))+24
 for text,size,w,h in [('按J开始游戏',16,width,36),('Game Over!',44,400,100)]:
  im=Image.new('RGBA',(w,h),(0,0,0,0)); d=ImageDraw.Draw(im)
  d.rounded_rectangle((0,0,w-1,h-1),8,fill=(23,27,35,240),outline=(215,192,137,255),width=2)
  d.text((w/2,h/2),text,font=ImageFont.truetype(str(FONT),size),anchor='mm',fill='white')
  frames.append((im,(w/2,h),0))
 pause_text='Game Pause...'; pause_font=ImageFont.truetype(str(FONT),44)
 measure=ImageDraw.Draw(Image.new('RGB',(1,1))).textlength(pause_text,font=pause_font)
 pause_w=next(w for w in (400,440,460) if measure+24<w)
 im=Image.new('RGBA',(pause_w,100),(0,0,0,0)); d=ImageDraw.Draw(im)
 d.rounded_rectangle((0,0,pause_w-1,99),8,fill=(23,27,35,240),outline=(215,192,137,255),width=2)
 d.text((pause_w/2,50),pause_text,font=pause_font,anchor='mm',fill='white')
 frames.append((im,(pause_w/2,50),0))
 planes=[]; desc=[]; raw=bytearray(); gold=[]; previews=[]; spans=[]; frame_spans=[]
 for im,anchor,source in frames:
  w,h=im.size; pix=list(im.getdata()); colors=[]; alphas=[]
  for r,g,b,a in pix:
   q=(a*32+127)//255; colors.append(max(1,word(r,g,b)) if q else 0); alphas.append(q)
  first=len(planes)
  for y in range(0,h,24):
   for x in range(0,w,24):
    tw=min(24,w-x);th=min(24,h-y)
    levels=sorted(set(alphas[(y+yy)*w+x+xx] for yy in range(th) for xx in range(tw))-{0})
    for q in levels:
     off=len(raw)
     for yy in range(th):
      for xx in range(tw):
       k=(y+yy)*w+x+xx;raw.extend(struct.pack('<H',colors[k] if alphas[k]==q else 0))
     planes.append((off,tw,th,x,y,q))
  sf=len(spans)
  if len(desc)<5:
   for yy in range(h):
    xx=0
    while xx<w:
     if not alphas[yy*w+xx]:xx+=1;continue
     x0=xx
     while xx<w and alphas[yy*w+xx]:xx+=1
     spans.append((yy,x0,xx))
  frame_spans.append((sf,len(spans)-sf))
  desc.append((w,h,anchor,first,len(planes)-first,source))
  gold.append(b''.join(struct.pack('<HB',c,a) for c,a in zip(colors,alphas)))
  bg=Image.new('RGBA',im.size,(44,63,55,255));bg.alpha_composite(im);previews.append(bg.convert('RGB'))
 meta=bytearray()
 for off,w,h,x,y,q in planes:meta+=struct.pack('<IHHHHB3x',off,w,h,x,y,q)
 frameoff=len(meta)
 for (w,h,(ax,ay),first,count,source),(sf,sc) in zip(desc,frame_spans):meta+=struct.pack('<HHHHiiIIIHBB',w,h,0,0,round(ax*256),round(ay*256),first,count,sf,sc,source,0)
 spanoff=len(meta)
 for yy,x0,x1 in spans:meta+=struct.pack('<HHH',yy,x0,x1)
 if len(meta)%4:meta+=bytes(4-len(meta)%4)
 payload=bytes(meta)+raw;OUT.mkdir(parents=True,exist_ok=True)
 (OUT/'flow.bin').write_bytes(payload)
 co=zlib.compressobj(9,zlib.DEFLATED,-15);packed=co.compress(payload)+co.flush();(OUT/'flow.pack').write_bytes(packed)
 defines={'FRAME_COUNT':13,'TILES':len(planes),'FRAMES_OFFSET':frameoff,'META_BYTES':len(meta),'RAW_BYTES':len(raw),'PAYLOAD_BYTES':len(payload),'META_CRC32':zlib.crc32(meta),'RAW_CRC32':zlib.crc32(payload),'SPANS':len(spans),'SPANS_OFFSET':spanoff}
 (OUT/'layout.h').write_text('/* Generated FLOW5 kind5,id0: raw deflate, predictor0. */\n'+''.join(f'#define FLOW_{k} {v}u\n' for k,v in defines.items())+'#define FLOW_GROUP_COUNTS {5,5,1,1,1}\n#define FLOW_GROUP_OFFSETS {0,5,10,11,12}\n#define FLOW_SOURCE_INDICES {0,1,2,3,4,0,1,2,3,4,0,0,0}\n',encoding='utf-8')
 tests=ROOT/'tests/flow_assets';tests.mkdir(parents=True,exist_ok=True)
 (tests/'golden.bin').write_bytes(b''.join(gold))
 preview_dir=ROOT/'build/resourcepreview';preview_dir.mkdir(parents=True,exist_ok=True)
 contact=Image.new('RGB',(1280,720),(30,35,40))
 for i,im in enumerate(previews[:10]):contact.paste(im,((i%5)*150,(i//5)*150))
 for i,im in enumerate(previews[10:]):contact.paste(im,((i%2)*600,320+(i//2)*140))
 contact.save(preview_dir/'flow13_contacts.png')
 manifest={'format':'direct565Q5 sparse24tiles preservingderivedalpha','kind':5,'id':0,'codec':1,'predictor':0,'raw':len(payload),'packed':len(packed),'raw_crc':zlib.crc32(payload),'packed_crc':zlib.crc32(packed),'metadata_crc':zlib.crc32(meta),'frames':[{'w':w,'h':h,'anchor_q8':[round(ax*256),round(ay*256)],'planes':n} for w,h,(ax,ay),f,n,s in desc],'derived_file_sha256':hashes,'derived_rgba_sha256':sources,'font_path':str(FONT),'font_sha256':sha(FONT.read_bytes()),'font_license':'Local Windows licensed font; raster UI for this user development only. No font redistributed.','ending_gray':{'color565':[16,32,16],'word':word(128,128,128),'resource_words':0,'gpu_fill_static_word':True},'notes':'PC source matte remains approximate; no new matte or scaling. Original death south-facing. Global fade is CPU per-tile q=(sourceQ*globalQ+16)>>5; no runtime pixels constructed.'}
 manifest['original_source_sha256']={m['source']:m['sha256'] for g in ('altar','death') for m in cfg[g]}
 manifest['config_sha256']={p.name:sha(p.read_bytes()) for p in [PC/'alpha_preparation.json',PC/'parameters.json']}
 manifest['pause']={'text':pause_text,'font_size':44,'measured_text_width':measure,'size':[pause_w,100],'anchor_q8':[pause_w*128,12800],'placement':'screen center (640,360); center anchor','runtime_render_or_resize':False,'rgba_archive':False}
 manifest['hint']={'text':'按J开始游戏','size':list(frames[10][0].size),'anchor_q8':[round(v*256) for v in frames[10][1]],'recommended_bottom':263,'source_unchanged':True,'clipping_root_cause':'scene damage y209..220, not source glyphs'}
 manifest['budget']={'metadata':len(meta),'pixel_raw':len(raw),'payload_raw':len(payload),'packed':len(packed)}
 preview_dir=ROOT/'build/resourcepreview';preview_dir.mkdir(parents=True,exist_ok=True)
 preview=Image.new('RGBA',(1280,720),(44,63,55,255))
 hint=frames[10][0];preview.alpha_composite(hint,(640-hint.width//2,263-hint.height))
 preview.alpha_composite(frames[12][0],(640-pause_w//2,310))
 preview.convert('RGB').save(preview_dir/'hint_pause_placement.png')
 (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
 streams={'schema':1,'streams':[{'kind':5,'id':0,'path':'resources/flow/flow.pack','codec':1,'predictor':0,'raw_bytes':len(payload),'raw_crc':zlib.crc32(payload),'packed_crc':zlib.crc32(packed),'decoded_sha256':sha(payload),'param':0,'alignment':4,'archive_only':False}]}
 (OUT/'external_streams.json').write_text(json.dumps(streams,indent=2),encoding='utf-8')
 (OUT/'README.md').write_text('# FLOW5\n\nGenerated from accepted PCv2 derived RGBA without rematting, resizing or altering originals. Original death is south-facing. Legacy source matte remains approximate. Q32 keyed Sprite; Q1..31 keyed AlphaSprite, RGB black promoted to 1 so key0 is never an opaque pixel. Metadata and pixels are one raw-deflate stream kind5 id0, predictor0. flow.bin is decoded golden/raw input, flow.pack is compressed input. Local font licensed only on this machine; no font redistributed. Ending gray565=(16,32,16) is static one-word GPU fill, no resource tile.\n',encoding='utf-8')
 assert all(sha(Path(p).read_bytes())==v for p,v in hashes.items())
 print(json.dumps({'raw':len(payload),'packed':len(packed),'tiles':len(planes),'frames':13}))
if __name__=='__main__':build()
