#!/usr/bin/env python3
"""Deterministic enemy7 compiler. Originals are read-only; no renderer/loader edits.
RGB mattes: exact boundary-connected color removal (never erase black outline).
Bullet: boundary-connected dark non-red background; retain red glow RGB, binary
mask. NEAREST 32x27 ->24x20 is the requested rounded aspect-preserving size.
All support metadata is generated from the final binary mask, never guessed boxes.
"""
from pathlib import Path
from collections import deque
import argparse, hashlib, json, struct, zlib
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'resources/enemy'
CACHE=['DK_walk_01_30px.png','DK_walk_02_30px.png','WOLF_walk_01_30px_outline.png','WOLF_walk_02_30px_outline.png','gargoyle_walkdown_04.png','gargoyle_walkdown_05.png','red_bullet_32px.png']
ATTACK=['DK_atk_01_raise_30px.png','DK_atk_02_vertical_30px.png','DK_atk_03_horizontal_30px.png']
def flood(im,predicate):
    w,h=im.size; seen=set(); q=deque()
    for y in range(h):
        for x in range(w):
            if x in (0,w-1) or y in (0,h-1):
                if predicate(im.getpixel((x,y))): seen.add((x,y));q.append((x,y))
    while q:
        x,y=q.popleft()
        for a,b in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
            if 0<=a<w and 0<=b<h and (a,b) not in seen and predicate(im.getpixel((a,b))):seen.add((a,b));q.append((a,b))
    rgba=im.convert('RGBA')
    for p in seen:
        r,g,b,_=rgba.getpixel(p);rgba.putpixel(p,(r,g,b,0))
    return rgba

def derive(path):
    im=Image.open(path)
    if im.mode=='RGBA': return im.copy(),'source-alpha'
    if 'red_bullet' in path.name:
        # Background has dark near-neutral blue bias; never remove red-dominant glow.
        return flood(im,lambda p:max(p)<=48 and p[0]<=p[1]+8 and p[0]<=p[2]+8),'boundary-dark48-red-protected'
    matte=im.getpixel((0,0))
    return flood(im,lambda p:p==matte),'boundary-exact-'+','.join(map(str,matte))

def rgb565(im):
    values=[]
    for r,g,b,a in im.getdata():
        v=((r>>3)<<11)|((g>>2)<<5)|(b>>3)
        values.append(max(1,v) if a else 0)
    return struct.pack('<'+'H'*len(values),*values)

def spans(im):
    result=[];rows=[];masks=[]
    for y in range(im.height):
        rows.append(len(result));mask=0;x=0
        while x<im.width:
            if not im.getpixel((x,y))[3]: x+=1;continue
            a=x
            while x<im.width and im.getpixel((x,y))[3]:mask|=1<<x;x+=1
            result.append((y,a,x))
        masks.append(mask)
    rows.append(len(result))
    return result,rows,masks

def tint(im,color):
    r=Image.new('RGBA',im.size,(*color,0));r.putalpha(im.getchannel('A'));return r

def checker(im,scale=6):
    bg=Image.new('RGBA',im.size)
    for y in range(im.height):
        for x in range(im.width):bg.putpixel((x,y),(170,170,170,255) if (x//4+y//4)%2 else (230,230,230,255))
    bg.alpha_composite(im);return bg.convert('RGB').resize((im.width*scale,im.height*scale),Image.Resampling.NEAREST)

def build():
    OUT.mkdir(parents=True,exist_ok=True);(OUT/'source_rgba').mkdir(exist_ok=True)
    sources=[];walk=[];attacks=[];panels=[]
    for folder,names,target in [('cache_materials',CACHE,walk),('ddr_materials',ATTACK,attacks)]:
        for name in names:
            path=ROOT.parent/folder/name;im,policy=derive(path)
            original=Image.open(path)
            if 'red_bullet' in name: im=im.resize((24,20),Image.Resampling.NEAREST)
            im.save(OUT/'source_rgba'/name);target.append(im)
            sources.append(dict(path=str(path.relative_to(ROOT.parent)),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),mode=original.mode,size=list(original.size),derived_size=list(im.size),policy=policy,alpha_values=sorted(set(im.getchannel('A').getdata()))))
            panels.append((name,im))
    # Canvas-local foot center; attack x anchors are manually authored character
    # feet rather than sword/effect box center. Absolute source height is retained.
    attack_anchors=[(9,37),(11,39),(18,33)]
    cache_anchors=[(11,31),(9,31),(10,30),(9,30),(12.5,18),(16,18),(12,10)]
    frames=[]
    for i,im in enumerate(attacks):frames.append((im,attack_anchors[i],i))
    frames.append((tint(walk[0],(255,255,255)),cache_anchors[0],0))
    for source in (0,2,4):
        frames.append((tint(walk[source],(255,0,0)),cache_anchors[source],source))
    for name,(im,_,_) in zip(['dk_attack_0','dk_attack_1','dk_attack_2','dk_white','dk_red','wolf_red','garg_red'],frames):
        im.save(OUT/'source_rgba'/(name+'.png'))
    # Header64, cache7*32, frames7*32, plane16[], span6[], row tables,
    # mask32 per cache row, aligned pixel stream. Header includes body CRC32.
    planes=[];ddr_pixels=bytearray();frame_records=[];allspans=[];cache_info=[]
    for idx,(im,anchor,source) in enumerate(frames):
        first=len(planes);sf=len(allspans);ss,_,_=spans(im);allspans.extend(ss)
        for y in range(0,im.height,24):
            for x in range(0,im.width,24):
                tile=im.crop((x,y,min(x+24,im.width),min(y+24,im.height)))
                if not tile.getchannel('A').getbbox():continue
                planes.append([len(ddr_pixels),tile.width,tile.height,x,y,32]);ddr_pixels.extend(rgb565(tile))
        frame_records.append((im.width,im.height,0,0,int(anchor[0]*256),int(anchor[1]*256),first,len(planes)-first,sf,len(ss),source,0))
    for i,im in enumerate(walk):
        sf=len(allspans);ss,rows,masks=spans(im);allspans.extend(ss)
        cache_info.append((sf,rows,masks))
    cacheoff=64;frameoff=cacheoff+7*32;planeoff=frameoff+len(frames)*32;spanoff=planeoff+len(planes)*16
    rowoff=(spanoff+len(allspans)*6+3)&~3;rows_blob=bytearray();cache_rowoff=[];cache_maskoff=[]
    for sf,rows,masks in cache_info:
        cache_rowoff.append(rowoff+len(rows_blob));rows_blob.extend(struct.pack('<'+'I'*len(rows),*[sf+r for r in rows]))
        cache_maskoff.append(rowoff+len(rows_blob));rows_blob.extend(struct.pack('<'+'I'*len(masks),*masks))
    pixoff=rowoff+len(rows_blob);cache_pixels=bytearray();cache_records=[]
    for i,im in enumerate(walk):
        ax,ay=cache_anchors[i];pix=rgb565(im)
        cache_records.append((im.width,im.height,int(ax*256),int(ay*256),pixoff+len(cache_pixels),len(pix),cache_info[i][0],cache_rowoff[i],i+1,32,0));cache_pixels.extend(pix)
    ddrbase=pixoff+len(cache_pixels)
    body=bytearray()
    for rec in cache_records:body.extend(struct.pack('<HHiiIIIIBBH',*rec))
    for rec in frame_records:body.extend(struct.pack('<HHHHiiIIIHBB',*rec))
    for off,w,h,x,y,a in planes:body.extend(struct.pack('<IHHHHB3x',ddrbase+off,w,h,x,y,a))
    for rec in allspans:body.extend(struct.pack('<HHH',*rec))
    body.extend(bytes(rowoff-64-len(body)));body.extend(rows_blob);body.extend(cache_pixels);body.extend(ddr_pixels)
    total=64+len(body);crc=zlib.crc32(body)
    header=struct.pack('<16I',0x374e4545,1,total,crc,7,len(frames),len(planes),len(allspans),cacheoff,frameoff,planeoff,spanoff,rowoff,pixoff,ddrbase,0)
    payload=header+body;c=zlib.compressobj(9,zlib.DEFLATED,-15);compressed=c.compress(payload)+c.flush()
    (OUT/'payload.bin').write_bytes(payload);(OUT/'payload.deflate').write_bytes(compressed)
    (OUT/'layout.h').write_text('#ifndef ENEMY_LAYOUT_H\n#define ENEMY_LAYOUT_H\n'+f'#define ENEMY_PAYLOAD_BYTES {total}u\n#define ENEMY_RAW_BYTES {len(cache_pixels)+len(ddr_pixels)}u\n#define ENEMY_PAYLOAD_CRC 0x{zlib.crc32(payload):08x}u\n#define ENEMY_PACKED_BYTES {len(compressed)}u\n#endif\n')
    manifest=dict(schema='enemy7-v1',container_kind=7,id=0,predictor=0,compression='single-raw-deflate',payload_bytes=total,raw_pixel_bytes=len(cache_pixels)+len(ddr_pixels),packed_bytes=len(compressed),crc32=f'{zlib.crc32(payload):08x}',body_crc32=f'{crc:08x}',cache_slots=cache_records,attack_anchors=attack_anchors,group_counts=[3,1,1,1,1],group_offsets=[0,3,4,5,6],sources=sources,policies=dict(cache_alpha='binary hardmask no dither',white='#ffffff',red='#ff0000',attack='original source resolution; common character-foot reference; no per-frame height normalization',garg='original widths, bottom-center foot anchors',bullet='24x20 NEAREST, boundary-connected dark48 red-protected hardmask'))
    (OUT/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    stream=dict(kind=7,id=0,path='resources/enemy/payload.deflate',codec=1,predictor=0,raw_bytes=total,raw_crc=zlib.crc32(payload),packed_crc=zlib.crc32(compressed),decoded_sha256=hashlib.sha256(payload).hexdigest(),param=0,alignment=4,archive_only=False)
    (OUT/'external_streams.json').write_text(json.dumps(dict(schema=1,streams=[stream]),indent=2)+'\n')
    sheet=Image.new('RGB',(960,((len(panels)+3)//4)*290),(45,45,45));d=ImageDraw.Draw(sheet)
    for i,(name,im) in enumerate(panels):
        x=(i%4)*240;y=(i//4)*290;d.text((x+3,y+3),name[:33],fill='white');p=checker(im,5);sheet.paste(p,(x+3,y+25));mask=im.getchannel('A').convert('RGB').resize((im.width*2,im.height*2),Image.Resampling.NEAREST);sheet.paste(mask,(x+3,y+220))
    sheet.save(OUT/'contact_checker_mask.png')
    overlay=Image.new('RGB',(560,300),(210,210,210));d=ImageDraw.Draw(overlay)
    for i,(im,(ax,ay)) in enumerate(zip(attacks,attack_anchors)):
        footx=85+i*180;footy=255;rgba=im.resize((im.width*5,im.height*5),Image.Resampling.NEAREST);overlay.paste(rgba,(footx-int(ax*5),footy-int(ay*5)),rgba);d.line((footx-65,footy,footx+65,footy),fill='red');d.line((footx,footy-220,footx,footy+10),fill='red');d.text((footx-50,275),f'{im.width}x{im.height} anchor {ax},{ay}',fill='black')
    overlay.save(OUT/'attack_anchor_overlay.png')
    map_path=ROOT/'resources/scene/source/map_preview.png'
    if map_path.exists():
        base=Image.open(map_path).convert('RGBA');compare=Image.new('RGB',(base.width,base.height*2))
        for mode in range(2):
            canvas=base.copy()
            for i,im in enumerate(walk):
                ax,ay=cache_anchors[i];x=170+i*140;y=min(210,base.height-40)
                view=im if mode else Image.open(ROOT.parent/'cache_materials'/CACHE[i]).convert('RGBA')
                if i==6 and mode==0:view=view.resize((24,20),Image.Resampling.NEAREST)
                canvas.alpha_composite(view,(x-int(ax),y-int(ay)))
            draw=ImageDraw.Draw(canvas);draw.text((10,10),'DERIVED binary key0 RGB565' if mode else 'ORIGINAL source RGB/alpha (matte visible)',fill='yellow')
            compare.paste(canvas.convert('RGB'),(0,mode*base.height))
        compare.save(OUT/'map_compare.png')
    print(json.dumps(dict(payload=total,packed=len(compressed),cache_bytes=len(cache_pixels),ddr_bytes=len(ddr_pixels),planes=len(planes)),indent=2))
if __name__=='__main__':build()
