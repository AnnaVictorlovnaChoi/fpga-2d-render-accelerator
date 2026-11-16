#!/usr/bin/env python3
"""Offline HUD; original art is read-only. LE pointer-free metadata + RGB565."""
import json,zlib,struct,hashlib,argparse
from pathlib import Path
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'resources/hud'
TEMP=ROOT.parent/'临时开发1-性能增强/g224/render'
def sha(b):return hashlib.sha256(b).hexdigest()
def baseline():
 m=json.loads((ROOT/'resources/player/manifest.json').read_text())['groups']['map'];p=(ROOT/'resources/player/player.pack').read_bytes();b=zlib.decompress(p[m['pack_offset']:m['pack_offset']+m['compressed_bytes']],-15)
 rgb=bytearray()
 for (v,) in struct.iter_unpack('<H',b):rgb.extend((((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31))
 return Image.frombytes('RGB',(1280,720),bytes(rgb)).convert('RGBA')
def generate():
 OUT.mkdir(parents=True,exist_ok=True);TEMP.mkdir(parents=True,exist_ok=True)
 base=baseline();text=OUT/'source_images/raster_text'
 def raster(name):return Image.open(text/(name+'.png')).convert('RGBA')
 frames=[]; names=[]
 def add(name,im):names.append(name);frames.append(im.convert('RGBA'));return len(frames)-1
 for hp in range(41):
  im=base.crop((62,14,292,35));d=ImageDraw.Draw(im)
  for j in range(40):d.rectangle((j*5,3,j*5+3,16),fill='#78dc55' if j<hp else '#253522');d.point((j*5,3),fill='#c5f595' if j<hp else '#52604a')
  add('HP_'+str(hp),im)
 add('COIN_BG',base.crop((354,12,432,37)))
 for n in range(10):add('DIGIT_'+str(n),raster(f'digit_{n}'))
 for n in range(4):add('WAVE_'+str(n),raster(f'wave_{n}'))
 for k in range(3):
  for on in range(2):add('KEY_'+str(k)+'_'+str(on),raster(f'key_{k}_{on}'))
 art=OUT/'source_images';pins=[{'path':str(p.relative_to(OUT)).replace('\\','/'),'sha256':sha(p.read_bytes())} for p in sorted(art.rglob('*')) if p.is_file()]
 for n in range(9):
  p=art/f'stamina_fill_{n}.png';b=p.read_bytes();im=Image.open(art/'stamina_base.png').convert('RGBA');im.alpha_composite(Image.open(p).convert('RGBA'));assert im.size==(45,15);add('STAMINA_'+str(n),im)
 add('VICTORY',raster('victory'))
 add('STATS_BG',Image.new('RGBA',(76,20),'#10171c'))
 panel=Image.new('RGBA',(176,44),'#10171c');d=ImageDraw.Draw(panel);d.rectangle((0,0,175,43),outline='#506975')
 panel.alpha_composite(raster('gpu_label'),(5,3));panel.alpha_composite(raster('disp_label'),(5,23))
 panel.alpha_composite(raster('fps_label'),(134,3));panel.alpha_composite(raster('fps_label'),(134,23));add('PERF_BG',panel)
 # Verify actual field pixels, including both PERF rows, before baking Q5 RGB565.
 def rgb565(p):return ((p[0]>>3)<<11)|((p[1]>>2)<<5)|(p[2]>>3)
 top=frames[names.index('STATS_BG')]
 backgrounds=[]
 for tile,box in [(top,(0,0,44,20)),(panel,(60,3,104,20)),(panel,(60,23,104,40))]:
  colors={rgb565(p) for p in tile.crop(box).getdata()};assert len(colors)==1,colors
  backgrounds.append(colors.pop())
 assert backgrounds[1]==backgrounds[2]
 for label,bg in [('STATS',backgrounds[0]),('PERF',backgrounds[1])]:
  for n in range(10):
   src=raster(f'digit_{n}');assert src.getchannel('A').getbbox()[3]<=17
   src=src.crop((0,0,11,17));im=Image.new('RGBA',src.size)
   for y in range(src.height):
    for x in range(src.width):
     p=src.getpixel((x,y));s=rgb565(p);a=(p[3]*32+127)//255
     v=sum((((((s>>shift)&mask)*a+((bg>>shift)&mask)*(32-a))>>5)<<shift) for shift,mask in [(11,31),(5,63),(0,31)])
     im.putpixel((x,y),(((v>>11)&31)<<3,((v>>5)&63)<<2,(v&31)<<3,255))
   add(label+'_DIGIT_'+str(n),im)
 # Maximal equal-alpha horizontal runs merged vertically, then split <=24.
 planes=[];infos=[];raw=bytearray()
 for im in frames:
  a=im.load();q=[[ (a[x,y][3]*32+127)//255 for x in range(im.width)] for y in range(im.height)];start=len(planes);active={};rects=[]
  for y in range(im.height+1):
   runs=[]
   if y<im.height:
    x=0
    while x<im.width:
     al=int(q[y][x]);e=x+1
     while e<im.width and q[y][e]==al:e+=1
     if al:runs.append((x,e,al))
     x=e
   for key,sy in list(active.items()):
    if key not in runs:rects.append((*key,sy,y));del active[key]
   for key in runs:active.setdefault(key,y)
  for x,e,al,sy,ey in rects:
   for yy in range(sy,ey,24):
    for xx in range(x,e,24):
     w=min(24,e-xx);h=min(24,ey-yy);off=len(raw)
     for py in range(yy,yy+h):
      for px in range(xx,xx+w):
       r,g,b,_=a[px,py];raw.extend(struct.pack('<H',((r>>3)<<11)|((g>>2)<<5)|(b>>3)))
     planes.append((off,xx,yy,w,h,al,0,0,0))
  infos.append((start,len(planes)-start,im.width,im.height))
 meta=b''.join(struct.pack('<IHHHHBBBB',*p) for p in planes);payload=meta+raw;c=zlib.compressobj(9,zlib.DEFLATED,-15);packed=c.compress(payload)+c.flush();(OUT/'hud.pack').write_bytes(packed)
 manifest={'schema':1,'streams':[{'kind':10,'id':0,'path':'resources/hud/hud.pack','codec':1,'predictor':0,'raw_bytes':len(payload),'raw_crc':zlib.crc32(payload),'packed_crc':zlib.crc32(packed),'decoded_sha256':sha(payload),'param':0,'alignment':64,'packed_bytes':len(packed),'packed_sha256':sha(packed),'archive_only':False}]};(OUT/'external_streams.json').write_text(json.dumps(manifest,indent=2))
 (OUT/'manifest.json').write_text(json.dumps(dict(payload_bytes=len(payload),pixel_bytes=len(raw),metadata_bytes=len(meta),planes=len(planes),frames=dict(zip(names,infos)),sources=pins,generator_sha256=sha(Path(__file__).read_bytes()),stats_background_rgb565=backgrounds,packed_bytes=len(packed),sha256=sha(payload),crc32=zlib.crc32(payload)),indent=2))
 header='#ifndef HUD_LAYOUT_H\n#define HUD_LAYOUT_H\n'+''.join(f'#define HUD_{k} {v}u\n' for k,v in [('PAYLOAD_BYTES',len(payload)),('META_BYTES',len(meta)),('PLANES',len(planes)),('CRC32',zlib.crc32(payload))])+''.join(f'#define HUD_FRAME_{n} {i}u\n' for i,n in enumerate(names))+'\n#define HUD_FRAME_INFO {'+','.join('{'+','.join(map(str,v))+'}' for v in infos)+'}\n#endif\n';(OUT/'layout.h').write_text(header)
 sheet=Image.new('RGBA',(720,440),'#31403b')
 for j,i in enumerate([0,1,20,40,42,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71]):
  if i<len(frames):sheet.alpha_composite(frames[i],((j%3)*240,(j//3)*45))
 sheet.save(TEMP/'contactsheet.png')
 preview=base.copy()
 for i,x,y in [(40,62,14),(41,354,12),(43,354,13),(42,365,13),(42,376,13),(42,387,13),(53,460,12),(57,630,4),(58,680,4),(61,730,4),(70,618,430)]:preview.alpha_composite(frames[i],(x,y))
 preview.alpha_composite(frames[names.index('PERF_BG')],(1100,634))
 for x,y,value in [(990,28,9999),(1180,28,9999),(1160,637,60),(1160,657,60)]:
  if y==28:preview.alpha_composite(top,(x,y))
  for j,c in enumerate(str(value)):preview.alpha_composite(frames[names.index(('STATS' if y==28 else 'PERF')+'_DIGIT_'+c)],(x+11*j,y))
 preview.save(TEMP/'hud_preview.png');(TEMP/'golden_payload.bin').write_bytes(payload)
 print(json.dumps(dict(payload=len(payload),packed=len(packed),planes=len(planes),frames=len(frames))))
if __name__=='__main__':generate()
