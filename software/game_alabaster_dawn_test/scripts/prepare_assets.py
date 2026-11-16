"""G2.1 deterministic offline assets. No target-time composition or embedded pixel blobs.
Run --snapshot once to import approved local sources; ordinary builds read resources/source only.
"""
from pathlib import Path
import argparse, collections, copy as copy_module, hashlib, json, math, re, shutil, struct, sys, zlib
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
ROOT=Path(__file__).resolve().parents[1]; RES=ROOT/'resources'; SRC=RES/'source'
sys.path.insert(0,str(ROOT/'scripts')); import pack_flash
DIRS=['0_E','45_NE','90_N','135_NW','180_W','225_SW','270_S','315_SE']
MASK_BYTES=1280*630//8

def load(p): return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def dump(p,v): Path(p).write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def copy(a,b):
 b=SRC/b; b.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(a,b)
def snapshot():
 base=ROOT.parent; pre=base/'FPGA_Preprocessed_Final'
 for name in ['map_clean_1280x630.png','ui_top_1280x50.png','ui_bottom_1280x40.png']: copy(base/'G1.0_StaticMap/resources/source'/name,Path('scene')/name)
 for p in (base/'cache_materials/layout_v1/placement_assets_static_ddr').glob('*.png'): copy(p,Path('islands')/p.name)
 for p in (base/'cache_materials/layout_v1/turrets').glob('*.png'): copy(p,Path('turrets')/p.name)
 for folder in ['idle_8dir','walk_8dir','dodge_4dir','CG_defeat']:
  for p in (base/'ddr_materials/characters'/folder).rglob('*'):
   if p.suffix in ('.png','.json') and not any(x in str(p) for x in ['preview','FINAL','REF_','_x4']): copy(p,Path('characters')/p.relative_to(base/'ddr_materials/characters'))
 for folder in ['stamina_bar','sign/fpga']:
  for p in (base/'ddr_materials'/folder).glob('*.png'):copy(p,Path(folder)/p.name)
 old=ROOT.parents[2]/'game_materials/Alabaster Dawn/07_maps/start_over_pc_v2'
 for p in (old/'derived').glob('death_*.png'):copy(p,Path('defeat')/p.name)
 copy(old/'alpha_preparation.json',Path('defeat/alpha_preparation.json'))
 for f in ['layout_params.json','turrets.json','routes/routes.json']:copy(pre/'final_sources/ddr_materials/maps/layout_v1'/f,Path('layout')/Path(f).name)
 copy(pre/'final_sources/ddr_materials/questions/fpga/route_questions.json',Path('questions.json'))
 copy(base/'ddr_materials/maps/ocean.jpg',Path('scene/ocean.jpg'))
 for folder,files in [('projectiles',['schema.json','trajectory_index.json','states_30hz.bin','source/timing_v2.json']),('effects',['schema.json','laser_events_60hz.bin','laser_width_alpha_60hz.bin','approved_preview_compat/variants.json','approved_preview_compat/variants_rgb565.bin','approved_preview_compat/variants_alpha8.bin','approved_preview_compat/sprite_states.bin'])]:
  for f in files:copy(pre/'data'/folder/f,Path(folder)/f)
 # Snapshot rendered glyphs, never the system font. Rebuilds need only PNG+metrics.
 q=load(SRC/'questions.json'); texts=['START','END','WIN!','Game Over!','PAUSE','J K E','0123456789:/','短路线 长路线 可用']
 for route in ('short_route','long_route'):
  for x in q[route]:texts += [x['question']]+x['options']
 chars=sorted(set(''.join(texts))); im=Image.new('RGBA',(1024,((len(chars)+31)//32)*32)); d=ImageDraw.Draw(im)
 font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',20); metrics={}
 for i,ch in enumerate(chars):
  x=i%32*32;y=i//32*32;d.text((x,y),ch,font=font,fill='white');metrics[ch]=[x,y,float(d.textlength(ch,font=font))]
 im.save(SRC/'glyphs.png');dump(SRC/'glyphs.json',metrics)
 dump(SRC/'provenance.json',{'source':'FPGA_Preprocessed_Final, ddr_materials, G1.0 and old transparent death derived; G2.19 never read or modified','glyph_font':'Microsoft YaHei, locally rasterized; system font NOT copied','death_common_source_anchor':[312,1029],'death_scale':70/761})

CACHE_SPECS=[('green_bullet_31px.png',(18,18)),('red_bullet_31px.png',(18,18)),('blue_bullet_31px.png',(18,18)),('crystal_red.png',(12,6)),('crystal_red_left.png',(12,6)),('snowflakes_e02.png',(16,18))]
def snapshot_cache():
 base=ROOT.parent
 for name,size in CACHE_SPECS:
  origin=(base/'cache_materials'/name if 'bullet' in name else base/'cache_materials/fpga'/name if 'snow' in name else base/'ddr_materials/maps/barrage/assets'/name)
  copy(origin,Path('cache_projectiles')/name)
 dump(SRC/'cache_projectiles/provenance.json',{'sources':[{'snapshot':name,'origin':str(origin.relative_to(base)),'sha256':hashlib.sha256(origin.read_bytes()).hexdigest()} for name,_ in CACHE_SPECS for origin in [(base/'cache_materials'/name if 'bullet' in name else base/'cache_materials/fpga'/name if 'snow' in name else base/'ddr_materials/maps/barrage/assets'/name)]]})
def cache_rgba(name,size):
 if 'bullet' in name:
  # COPY: ignore source alpha, preserve black/background, resize full RGB rectangle.
  return Image.open(SRC/'cache_projectiles'/name).convert('RGB').resize(size,Image.Resampling.LANCZOS).convert('RGBA')
 im=rgba(SRC/'cache_projectiles'/name)
 im=im.resize(size,Image.Resampling.LANCZOS);a=np.asarray(im).copy();a[:,:,3]=np.where(a[:,:,3]>=128,255,0);return Image.fromarray(a)

def rgba(p):return Image.open(p).convert('RGBA')
def rgb565(im):
 a=np.asarray(im.convert('RGB'),dtype=np.uint16);return (((a[:,:,0]>>3)<<11)|((a[:,:,1]>>2)<<5)|(a[:,:,2]>>3)).astype('<u2')
def component(a,seed):
 h,w=a.shape; ys,xs=np.where(a);assert len(xs),'empty ground mask'
 sx,sy=min(zip(xs.tolist(),ys.tolist()),key=lambda p:(p[0]-seed[0])**2+(p[1]-seed[1])**2)
 out=np.zeros_like(a); out[sy,sx]=True;todo=collections.deque([(sx,sy)])
 while todo:
  x,y=todo.popleft()
  for xx,yy in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
   if 0<=xx<w and 0<=yy<h and a[yy,xx] and not out[yy,xx]:out[yy,xx]=True;todo.append((xx,yy))
 return out

def speed_trajectories():
 """Float PC step, not resampling baseline rounded coordinates."""
 timing=copy_module.deepcopy(load(SRC/'projectiles/source/timing_v2.json'))
 flags=load(SRC/'projectiles/schema.json')['state']['flags']
 out=RES/'speed_1p2/projectiles';out.mkdir(parents=True,exist_ok=True)
 timing['version']='G2.6_speed_1p2';timing['speed_multiplier']=1.2
 timing['interval_rule']='unchanged baseline; no birth frequency reduction'
 wire=bytearray();states=bytearray();trajs=[];index=[]
 for ti,t in enumerate(timing['turrets']):
  t['speed_px_s']=t['speed_px_s']*6/5
  t['flight_times_s']=[v/1.2 for v in t.get('flight_times_s',[])]
  t['bullets_on_screen']=t.get('bullets_on_screen',0)/1.2
  for di,angle in enumerate(t['angles']):
   first=len(wire)//8;offset=len(states)//20;x=t['map_xy'][0]+(26 if t['side']=='left' else -26);y=float(t['map_xy'][1]);dx=math.cos(math.radians(angle));dy=math.sin(math.radians(angle));bounces=0;count=0
   while True:
    x=x+dx*t['speed_px_s']*(1.0/30);y=y+dy*t['speed_px_s']*(1.0/30)
    touched=False
    if x<0 or x>1280:x=-x if x<0 else 2560-x;dx=-dx;touched=True
    if y<0 or y>630:y=-y if y<0 else 1260-y;dy=-dy;touched=True
    bounces+=int(touched)
    if bounces>2:break
    cx,cy=round(x),round(y);left,top=cx-9,cy-9
    x0,y0,x1,y1=max(0,left),max(0,top),min(1280,left+18),min(630,top+18)
    w,h=max(0,x1-x0),max(0,y1-y0)
    fl=flags['alive']|(flags['bounce'] if touched else 0)|(flags['clipped'] if (w,h)!=(18,18) else 0)
    cell=max(0,min(65535,(max(0,cy)//32)*40+max(0,cx)//32))
    wire.extend(struct.pack('<hhHH',cx,cy,cell,fl));states.extend(struct.pack('<hhHHHHHHHH',left,top+50,x0-left,y0-top,x0,y0+50,w,h,fl,bounces));count+=1
   wire.extend(struct.pack('<hhHH',9,-41,0,flags['end']))
   states.extend(struct.pack('<hhHHHHHHHH',0,0,0,0,0,0,0,0,flags['end'],3))
   trajs.append((first,count,0));index.append(dict(id=ti*4+di,turret=t['id'],direction=di,angle_deg=angle,state_record_offset=offset,live_records=count,terminal_age=count))
 timing['expected_on_screen']=timing.get('expected_on_screen',0)/1.2
 dump(out/'timing_v2.json',timing);dump(out/'trajectory_index.json',index);(out/'states_30hz.bin').write_bytes(states)
 dump(RES/'speed_trajectory_index.json',index)
 return wire,trajs,timing,index

def speed_effects():
 """Keep object births and texture variants; compress age only after first visible."""
 effects=SRC/'effects/approved_preview_compat';variants=load(effects/'variants.json')
 records=list(struct.iter_unpack('<hhffBBH',(effects/'sprite_states.bin').read_bytes()))
 starts=[next((f for f in range(230) if records[f*150+i][4] and records[f*150+i][6]!=65535),230) for i in range(150)]
 raw=bytearray();counts=[]
 for f in range(230):
  n=0
  for i,start in enumerate(starts):
   sf=start+(f-start)*6//5 if f>=start else f
   r=records[(min(sf,229))*150+i];x,y,alpha,rotation,visible,kind,variant=r
   if sf>=230 or f<start or variant==65535:visible=0
   if variant==65535:variant=0
   v=variants[variant];raw.extend(struct.pack('<hhHBB',x+v['width']//2,y+v['height']//2,variant,visible,3 if kind==0 else 4));n+=bool(visible)
  counts.append(n)
 return raw,starts,counts

def speed_counts(trajs,effect_counts):
 header=(ROOT/'src/fire_schedule.h').read_text(encoding='utf-8')
 masks=[int(x,16) for x in re.findall(r'0x[0-9a-fA-F]+',header.split('fire_schedule[FIRE_SCHEDULE_FRAMES]={',1)[1].split('};',1)[0])][:9000]
 delta=[0]*11000;seq=[0]*16;births=0
 for f,mask in enumerate(masks):
  for ti in range(16):
   if mask&(1<<ti):
    tr=ti*4+seq[ti]%4;seq[ti]+=1;births+=1;delta[f]+=1;delta[f+trajs[tr][1]]-=1
 ordinary=[];n=0
 for f in range(9000):n+=delta[f];ordinary.append(n)
 # game.c waves start when cumulative game_ms crosses each 12000ms boundary.
 combined=[n+(effect_counts[(f%360)] if f>=360 and f%360<230 else 0) for f,n in enumerate(ordinary)]
 return dict(duration_s=300,frames=9000,births=births,ordinary_peak=max(ordinary),ordinary_first_peak_frame=ordinary.index(max(ordinary)),ordinary_steady_mean=sum(ordinary[1800:])/7200,effect_max_visible=max(effect_counts),combined_phase_peak=max(combined),combined_first_peak_frame=combined.index(max(combined)),conservative_combined_bound=max(ordinary)+max(effect_counts),effect_wave_period_ms=12000,ordinary_counts=ordinary,combined_counts=combined)

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--snapshot',action='store_true');ap.add_argument('--snapshot-cache',action='store_true');args=ap.parse_args();RES.mkdir(exist_ok=True)
 if args.snapshot:snapshot()
 if args.snapshot or args.snapshot_cache:snapshot_cache()
 glyph=rgba(SRC/'glyphs.png');gm=load(SRC/'glyphs.json')
 # YaHei snapshot has missing-glyph boxes for subscript 0/1. Derive exact
 # subscript shapes from its own digit glyphs; no font or external source.
 for sub,digit in [('₀','0'),('₁','1')]:
  gx,gy,advance=gm[digit];patch=glyph.crop((gx,gy,gx+round(advance),gy+32));patch=patch.crop(patch.getbbox());patch=patch.resize((max(1,round(patch.width*.65)),10),Image.Resampling.LANCZOS)
  gx,gy,_=gm[sub];glyph.paste((0,0,0,0),(gx,gy,gx+32,gy+32));glyph.alpha_composite(patch,(gx,gy+17));gm[sub][2]=patch.width+2
 def text(s,scale=1):
  w=max(1,round(sum(gm[c][2] for c in s)));im=Image.new('RGBA',(w,32));x=0
  for c in s:
   gx,gy,advance=gm[c];im.alpha_composite(glyph.crop((gx,gy,gx+min(32,round(advance)+2),gy+32)),(round(x),0));x+=advance
  if scale!=1:im=im.resize((max(1,round(w*scale)),round(32*scale)),Image.Resampling.LANCZOS)
  return im
 arena=bytearray();images=[];spans=[];dedup={};names={};preview={}
 def add(im,name,anchor=(0,0),sprite=False):
  im=im.convert('RGBA'); a=np.asarray(im); fg=a[:,:,3]>=128; pix=rgb565(im);pix=np.where(fg,np.maximum(pix,1) if sprite else pix,0).astype('<u2')
  key=(im.size,anchor,sprite,hashlib.sha256(pix.tobytes()+fg.tobytes()).digest())
  if key in dedup:names[name]=dedup[key];return dedup[key]
  first=len(spans)
  for y,row in enumerate(fg):
   edges=np.diff(np.r_[False,row,False].astype(np.int8));starts=np.where(edges==1)[0];ends=np.where(edges==-1)[0]
   for x,e in zip(starts,ends):spans.append((y,int(x),int(e-x),0))
  rec=(len(arena)//2,first,im.width,im.height,len(spans)-first,round(anchor[0]),round(anchor[1]));assert rec[4]<65536
  idx=len(images);images.append(rec);arena.extend(pix.tobytes());names[name]=idx;dedup[key]=idx
  if name.startswith(('idle','cg','dodge')):preview[name]=im
  return idx
 def contiguous(im):o=len(arena)//2;arena.extend(rgb565(im).tobytes());return o
 layout=load(SRC/'layout/layout_params.json');routes=load(SRC/'layout/routes.json');questions=load(SRC/'questions.json'); tur=load(SRC/'layout/turrets.json')
 frame=Image.new('RGB',(1280,720));frame.paste(rgba(SRC/'scene/ui_top_1280x50.png').convert('RGB'),(0,0));frame.paste(rgba(SRC/'scene/map_clean_1280x630.png').convert('RGB'),(0,50));frame.paste(rgba(SRC/'scene/ui_bottom_1280x40.png').convert('RGB'),(0,680))
 ocean=frame.copy();oc=Image.open(SRC/'scene/ocean.jpg').convert('RGB').resize(tuple(layout['ocean']['resized_size']),Image.Resampling.NEAREST).crop(tuple(layout['ocean']['crop_after_resize_xyxy']));ocean.paste(oc,(0,50))
 for t in tur['turrets']:
  im=rgba(SRC/'turrets'/Path(tur['assets'][t['id']]).name);x0,y0,x1,y1=t['dest_bbox_map_xyxy'];im=im.resize((x1-x0,y1-y0));frame.paste(im,(x0,y0+50),im)
 for i,(name,y) in enumerate([('START',55),('END',608)]):
  sign=rgba(SRC/'sign/fpga'/('sign_e01.png' if i==0 else 'sign_e02.png'));sign.thumbnail((90,58));frame.paste(sign,(780-sign.width//2,y),sign)
 background_offset=contiguous(frame);ocean_offset=contiguous(ocean);grey=frame.convert('L').convert('RGB');grey_offset=contiguous(grey);graywash_offset=contiguous(Image.new('RGB',(1280,720),(128,128,128)));frame.save(RES/'static_frame.png');grey.save(RES/'grey_frame.png')
 # All island masks are full global 1280x630, LSB first, offsets in bytes.
 ordered=['S']+[i['id'] for i in sorted(routes['islands'],key=lambda x:x['number'])]+['G']; placements={p['id']:p for p in layout['placements']};islands=[];masks=[];fade=[];mask_report=[]
 overview=frame.copy();od=ImageDraw.Draw(overview)
 for idx,key in enumerate(ordered):
  p=placements[key];x0,y0,x1,y1=p['dest_bbox_map_xyxy'];im=rgba(SRC/'islands'/f'{key}.png');a=np.asarray(im).astype(np.int16);r,g,b,alpha=[a[:,:,i] for i in range(4)]
  # Green land + warm sand; water blue and desaturated grey shore/shadow excluded.
  ground=(alpha>=128)&(((g>r*1.07)&(g>b*1.10)&(g>60))|((r>b*1.28)&(g>b*1.16)&(r>100)&(g>75)))
  ground=np.asarray(Image.fromarray((ground*255).astype('uint8')).filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(7)))>0
  seed=((x1-x0)/2,(y1-y0)/2);ground=component(ground,seed);ys,xs=np.where(ground);cx,cy=min(zip(xs.tolist(),ys.tolist()),key=lambda q:(q[0]-seed[0])**2+(q[1]-seed[1])**2);cx+=x0;cy+=y0
  global_mask=np.zeros((630,1280),dtype=bool);global_mask[y0:y1,x0:x1]=ground;masks.append(global_mask);islands.append((x0,y0,x1,y1,cx,cy,idx*MASK_BYTES))
  overlay=Image.new('RGBA',im.size,(20,240,180,0));overlay.putalpha(Image.fromarray((ground*150).astype('uint8')));overview.paste(overlay,(x0,y0+50),overlay);od.text((cx,cy+50),str(idx),fill='red');od.ellipse((cx-3,cy+47,cx+3,cy+53),outline='white')
  level=[];box=(x0,y0+50,x1,y1+50)
  for n in range(9):level.append(contiguous(Image.blend(frame.crop(box),ocean.crop(box),n/8)))
  fade.append(level);mask_report.append({'id':idx,'source_id':key,'bbox':[x0,y0,x1,y1],'anchor':[cx,cy],'walkable_pixels':int(ground.sum()),'connected_components':1})
 maskraw=b''.join(np.packbits(m.ravel(),bitorder='little').tobytes() for m in masks)
 idle=[];walk=[];dodge=[];scales=[];animation_report=[]
 for direction in DIRS:
  im=rgba(SRC/'characters/idle_8dir'/f'{direction}.png');scale=70/im.height;scales.append(scale);dp=SRC/'characters/dodge_4dir'/direction
  if not dp.exists():dp=SRC/'characters/dodge_4dir/diagonal_4dir'/direction
  par=load(dp/'params.json');anchor=par.get('idle_anchor',[im.width/2,im.height-1]);out=im.resize((round(im.width*scale),70),Image.Resampling.LANCZOS);idle.append(add(out,'idle_'+direction,(anchor[0]*scale,anchor[1]*scale)))
  wp=SRC/'characters/walk_8dir'/direction;meta=load(wp/'frames.json');wids=[]
  for f in range(45):
   wim=rgba(wp/f'a{f:04}.png');wout=wim.resize((round(wim.width*scale),round(wim.height*scale)),Image.Resampling.LANCZOS);an=meta['anchor'];wids.append(add(wout,f'walk_{direction}_{f}',(an[0]*scale,an[1]*scale)))
  walk.append(wids);dids=[]
  for state in ('body_rgba','body_A_rgba','body_A_B_rgba'):
   dim=rgba(dp/f'{state}.png');dim=dim.resize((round(dim.width*scale),round(dim.height*scale)),Image.Resampling.LANCZOS);an=par['anchor'];dids.append(add(dim,f'dodge_{direction}_{state}',(an[0]*scale,an[1]*scale)))
  dodge.append(dids);animation_report.append({'direction':direction,'idle_scale':scale,'idle_height':70,'walk_scale':scale,'dodge_scale':scale,'frame_count':45,'native_idle_anchor':anchor,'walk_anchor':meta['anchor']})
 cg=[]
 for i in range(1,6):
  # Read both matching raw CG and prior transparent extraction; never paste white raw backgrounds.
  original=rgba(SRC/'characters/CG_defeat'/f'f{i}.png');assert original.size==(623,1057)
  im=rgba(SRC/'defeat'/f'death_{i}.png');im=im.resize((round(im.width*70/90),round(im.height*70/90)),Image.Resampling.LANCZOS);cg.append(add(im,f'cg{i}',(312*70/761,1029*70/761)))
 stamina=[]
 for i in range(9):
  im=rgba(SRC/'stamina_bar/stamina_base.png');fill=rgba(SRC/'stamina_bar'/f'stamina_fill_{i}.png');im.alpha_composite(fill);im=im.resize((36,12),Image.Resampling.NEAREST);stamina.append(add(im,f'stamina{i}',(18,0)))
 labels=[]
 for k in 'JKE':
  row=[]
  for raised in range(2):
   im=Image.new('RGBA',(30,30),(255,225,110,255) if raised else (35,55,55,255));s=text(k,.8);im.alpha_composite(s,((30-s.width)//2,0));row.append(add(im,f'label{k}_{raised}',(15,30)))
  labels.append(row)
 digits=[]
 for k in '0123456789:/':
  glyphim=text(k);bbox=glyphim.getbbox();assert bbox
  glyphim=glyphim.crop(bbox);glyphim.thumbnail((14,16),Image.Resampling.LANCZOS)
  digit=Image.new('RGBA',(16,16));digit.alpha_composite(glyphim,((16-glyphim.width)//2,16-glyphim.height));digits.append(add(digit,'digit'+k,(0,0)))
 def panel(lines,w=640,h=150,size=1):
  im=Image.new('RGBA',(w,h),(19,38,46,255));d=ImageDraw.Draw(im);d.rectangle((0,0,w-1,h-1),outline=(220,218,150),width=2);y=8
  for line in lines:
   # character wrapping based on offline glyph widths
   current=''
   for ch in line:
    if sum(gm[c][2] for c in current+ch)*size>w-24:
     im.alpha_composite(text(current,size),(12,y));y+=round(28*size);current=''
    current+=ch
   if current:im.alpha_composite(text(current,size),(12,y));y+=round(28*size)
  assert y<=h+8,(lines,y,h)
  return im
 win=add(panel(['WIN!'],320,80,1.5),'win',(160,40));gameover=add(panel(['Game Over!'],320,80,1.5),'gameover',(160,40));pause=add(panel(['PAUSE'],320,80,1.5),'pause',(160,40))
 nodes=[[None]*18 for _ in range(2)];option_report=[];card_report=[];card_previews=[]
 def question_card(q):
  # 20px snapshotted font * .9 = 18px. Strip empty glyph-cell
  # padding before placement, so wrapping and ink bounds are exact.
  scale=.9;im=Image.new('RGBA',(430,97),(19,38,46,255));d=ImageDraw.Draw(im);d.rectangle((0,0,429,96),outline=(220,218,150),width=2)
  def wrap(s,width):
   lines=[];line=''
   for ch in s:
    if line and text(line+ch,scale).width>width:
     if ch in '？，。、；：！?':lines.append(line[:-1]);line=line[-1:]
     else:lines.append(line);line=''
    line+=ch
   if line:lines.append(line)
   return lines
  stem=wrap(q['question'],406);assert len(stem)<=2,(q['question_id'],stem)
  placements=[]
  def place(s,x,y,width):
   ink=text(s,scale);bbox=ink.getchannel('A').point(lambda a:255 if a>=128 else 0).getbbox();assert bbox
   # RGB565 cards use no fractional alpha: crop to actual opaque ink,
   # not LANCZOS nearly-transparent ringing around the 32px glyph cells.
   ink=ink.crop(bbox);assert ink.width<=width and ink.height<=23,(s,ink.size,width)
   box=[x,y,x+ink.width,y+ink.height];assert box[2]<=418 and box[3]<=91
   assert not (box[0]<430 and box[2]>390 and box[1]<92 and box[3]>76),(s,box)
   im.alpha_composite(ink,(x,y));placements.append({'text':s,'bounds':box})
  for i,line in enumerate(stem):place(line,12,8+23*i,406)
  option_y=12+23*len(stem)
  option_lines=[]
  for i,(key,s) in enumerate(zip('JKE',q['options'])):
   lines=wrap(key+' '+s,128);assert len(lines)<=2
   for j,line in enumerate(lines):place(line,12+140*i,option_y+21*j,128)
   option_lines.append(lines)
  card_report.append({'question_id':q['question_id'],'question':q['question'],'options':q['options'],'answer_key':q['answer_key'],'size':[430,97],'font_px':18,'stem_lines':stem,'option_lines':option_lines,'placements':placements,'clock_reserved':[390,76,430,92]})
  card_previews.append((q['question_id'],im));return im
 def positions(node,count):
  m=masks[node];x0,y0,x1,y1,cx,cy,_=islands[node];ys,xs=np.where(m);w=x1-x0;h=y1-y0
  targets=[(cx+(k-(count-1)/2)*min(48,w/(count+1)),cy) for k in range(count)] if w>=h*.65 else [(cx,cy+(k-(count-1)/2)*min(55,h/(count+1))) for k in range(count)]
  return [min(zip(xs.tolist(),ys.tolist()),key=lambda p:(p[0]-x)**2+(p[1]-y)**2) for x,y in targets]
 for route in range(2):
  for node in range(18):nodes[route][node]=(0,0,10,0,[17,17,17],[0,0,0],[0,0,0],0)
  pts=positions(0,2);nodes[route][0]=(2,3,10,0,[1,4,17],[p[0] for p in pts]+[0],[p[1] for p in pts]+[0],add(panel(['J 短路线','K 长路线'],430,100,.8),'startcard',(215,islands[0][5])))
  for q in questions['short_route' if route==0 else 'long_route']:
   node=q['node'];count=len(q['options']);correct=1<<'JKE'.index(q['answer_key']);spec=q['direction_spec'];good=spec['correct'];bad=spec['wrong'];good=good[0] if isinstance(good,list) else good;bad=bad if isinstance(bad,list) else [bad]
   if node==7:good=6
   dest=lambda v:17 if v=='G' else int(v)
   nexts=[];wrong_index=0
   for k in range(count):
    if correct&(1<<k):nexts.append(dest(good))
    else:nexts.append(dest(bad[wrong_index%len(bad)]));wrong_index+=1
   nexts += [17]*(3-count)
   pts=positions(node,count);ox=[p[0] for p in pts]+[0]*(3-count);oy=[p[1] for p in pts]+[0]*(3-count)
   cardim=question_card(q)
   card=add(cardim,f'card{route}_{node}',(215,islands[0][5]))
   cardim.save(RES/f'question_{route}_{node}.png');nodes[route][node]=(count,correct,q['time_limit_s'],int(q['question_id'][1:]),nexts,ox,oy,card)
   for k,p in enumerate(pts):assert masks[node][p[1],p[0]];od.ellipse((p[0]-4,p[1]+46,p[0]+4,p[1]+54),fill=('red','yellow','white')[k])
   option_report.append({'route':route,'node':node,'correct_mask':correct,'next':nexts,'options':pts,'all_in_mask':True})
 overview.save(RES/'walk_masks_preview.png')
 # Projectile records retain END sentinel; centers map-local, flags unchanged.
 trajraw,trajs,timing,index=speed_trajectories();turrets=[]
 for ti in range(16):
  t=next(x for x in tur['turrets'] if x['id']==index[ti*4]['turret']);kind={'green':0,'red':1,'blue':2}[t['type']];interval=next(x['interval_s'] for x in timing['turrets'] if x['id']==t['id']);turrets.append((round(interval*1000),kind,0,[ti*4+j for j in range(4)]))
 cache_images=[];cache_report=[];cache_sheet=Image.new('RGB',(600,125),(30,45,55));cd=ImageDraw.Draw(cache_sheet)
 for slot,(name,size) in enumerate(CACHE_SPECS,1):
  im=cache_rgba(name,size);sprite=slot>3;idx=add(im,'cache_'+name,(size[0]//2,size[1]//2),sprite);cache_images.append(idx)
  im.save(RES/('cache_'+name));big=im.resize((size[0]*3,size[1]*3),Image.Resampling.NEAREST);cache_sheet.paste(big,((slot-1)*100+10,24),big);cd.text(((slot-1)*100+4,5),name.split('_')[0],fill='white')
  cache_report.append({'slot':slot,'image_id':idx,'source':name,'size':list(size),'mode':'SPRITE' if sprite else 'COPY','colorkey':0 if sprite else None,'opaque_black_remap':1 if sprite else 0,'resize':'Pillow RGB LANCZOS' if not sprite else 'Pillow RGBA LANCZOS + alpha>=128'})
 cache_sheet.save(RES/'cache_contactsheet.png')
 cardsheet=Image.new('RGB',(890,9*125),(30,45,55));cs=ImageDraw.Draw(cardsheet)
 for i,(qid,im) in enumerate(card_previews):
  x=10+(i%2)*440;y=(i//2)*125;cs.text((x,y),qid,fill='white');cardsheet.paste(im,(x,y+20))
 cardsheet.save(RES/'questions_contactsheet.png')
 effects=SRC/'effects/approved_preview_compat';variants=load(effects/'variants.json')
 effect_images=[cache_images[{0:5,1:3,2:4}[v['kind']]] for v in variants]
 effectraw,effect_starts,effect_counts=speed_effects()
 speed_report=speed_counts(trajs,effect_counts)
 speed_report.update(speed_multiplier=1.2,intervals_unchanged=True,effect_frames=230,effect_count=150,effect_starts=effect_starts,effect_visible_counts=effect_counts,effect_sampling='start + floor((frame-start)*6/5); first visible start unchanged; alpha/rotation sampled but runtime fixed textures ignore alpha',trajectory_live_min=min(t[1] for t in trajs),trajectory_live_max=max(t[1] for t in trajs),trajectory_records=len(trajraw)//8)
 dump(RES/'speed_1p2_report.json',speed_report)
 assert len(effectraw)==230*150*8
 lasers=[]
 for start,end,nxt,x,y,ex,ey,r,g,b,side in struct.iter_unpack('<IIIhhhhBBBB',(SRC/'effects/laser_events_60hz.bin').read_bytes()):lasers.append((x,y-50,ex,3 if r>200 and g>150 and b<120 else 0 if g>r and g>b else 1 if r>g and r>b else 2 if b>r else 3,0))
 widths=[r[0] for r in struct.iter_unpack('<HBB',(SRC/'effects/laser_width_alpha_60hz.bin').read_bytes())];assert len(widths)==180
 # Append-only performance labels: never perturb existing image IDs, spans,
 # glyph atlas, question cards, cache sizes, or earlier pixel payload offsets.
 # Missing '~' is an offline 20px YaHei snapshot, not a build-time font lookup.
 perf_glyph=rgba(SRC/'perf/tilde.png');perf_meta=load(SRC/'perf/provenance.json')
 assert hashlib.sha256((SRC/'perf/tilde.png').read_bytes()).hexdigest()==perf_meta['glyph_sha256']
 assert perf_meta['character']=='~' and perf_meta['font_px']==20 and not perf_meta['font_copied']
 perf_labels=[];perf_report=[]
 for label in ('GPU','DISP~'):
  advance=lambda ch:perf_meta['advance'] if ch=='~' else gm[ch][2]
  im=Image.new('RGBA',(round(sum(advance(ch) for ch in label)),32));x=0
  for ch in label:
   if ch=='~':patch=perf_glyph
   else:
    gx,gy,a=gm[ch];patch=glyph.crop((gx,gy,gx+min(32,round(a)+2),gy+32))
   im.alpha_composite(patch,(round(x),0));x+=advance(ch)
  im=im.resize((round(im.width*.8),26),Image.Resampling.LANCZOS)
  bbox=im.getchannel('A').point(lambda a:255 if a>=128 else 0).getbbox();assert bbox
  ink=im.crop(bbox);assert ink.width+4<=64 and ink.height<=20
  badge=Image.new('RGBA',(ink.width+4,20));badge.alpha_composite(ink,(2,(20-ink.height)//2))
  idx=add(badge,'perf_'+label,(0,0));perf_labels.append(idx);badge.save(RES/('perf_'+label.replace('~','_estimate')+'.png'))
  perf_report.append({'text':label,'image_id':idx,'size':list(badge.size),'anchor':[0,0],'glyphs':list(label)})
 sheet=Image.new('RGB',(256,80),(19,38,46))
 for i,label in enumerate(('GPU','DISP~')):
  badge=rgba(RES/('perf_'+label.replace('~','_estimate')+'.png'));sheet.paste(badge.resize((badge.width*2,40),Image.Resampling.NEAREST),(8+i*120,20),badge.resize((badge.width*2,40),Image.Resampling.NEAREST))
 sheet.save(RES/'perf_labels_preview.png')
 dump(RES/'perf_labels.json',{'labels':perf_report,'glyph_provenance':perf_meta,'placements':[[920,686],[1095,686]],'estimate_hint':'DISP~ retains visible tilde; display time is estimated'})
 # Generated C metadata contains no raw pixel bytes.
 def c(v):return '{'+','.join(c(x) if isinstance(x,(list,tuple)) else str(int(x)) for x in v)+'}'
 lines=['/* Generated by scripts/prepare_assets.py; pixel payload remains external. */','#include "assets.h"']
 def emit(typ,name,v,dims=''):lines.append(f'const {typ} {name}{dims} = {c(v)};')
 emit('AssetImage','asset_images',images,'[]');emit('AssetSpan','asset_spans',spans,'[]');emit('IslandAsset','asset_islands',islands,'[18]');emit('NodeAsset','asset_nodes',[[[a,b,t,q,n,0,x,y,card] for a,b,t,q,n,x,y,card in route] for route in nodes],'[2][18]');emit('TrajAsset','asset_trajs',trajs,'[64]');emit('TurretAsset','asset_turrets',turrets,'[16]');emit('LaserAsset','asset_lasers',lasers,'[8]');emit('uint8_t','asset_laser_width',widths,'[180]')
 for name,val,dims in [('idle',idle,'[8]'),('walk',walk,'[8][45]'),('dodge',dodge,'[8][3]'),('cg',cg,'[5]'),('stamina',stamina,'[9]'),('labels',labels,'[3][2]'),('digits',digits,'[12]'),('effect_images',effect_images,'[]'),('cache_images',cache_images,'[6]'),('perf_labels',perf_labels,'[2]')]:emit('uint16_t','asset_'+name,val,dims)
 for name,val,typ in [('win',win,'uint16_t'),('gameover',gameover,'uint16_t'),('pause',pause,'uint16_t'),('dodge_distance',60,'uint16_t'),('background_offset',background_offset,'uint32_t'),('ocean_offset',ocean_offset,'uint32_t'),('grey_offset',grey_offset,'uint32_t'),('graywash_offset',graywash_offset,'uint32_t'),('pixels_count',len(arena)//2,'uint32_t'),('effect_frames',230,'uint16_t'),('effect_count',150,'uint16_t')]:lines.append(f'const {typ} asset_{name} = {val}u;')
 emit('uint32_t','asset_fade_offsets',fade,'[18][9]');(ROOT/'src/assets_generated.c').write_text('\n'.join(lines)+'\n',encoding='utf-8')
 arrays=[bytes(arena),maskraw,bytes(trajraw),bytes(effectraw)];manifest={'version':'g21_assets_v1','entries':[]}
 for i,(name,raw) in enumerate(zip(['pixels','masks','trajectories','effects'],arrays)):
  co=zlib.compressobj(9,zlib.DEFLATED,-15);packed=co.compress(raw)+co.flush();(RES/f'{name}.deflate').write_bytes(packed);manifest['entries'].append(dict(kind=3,id=i,codec=1,predictor=0,path=f'resources/{name}.deflate',raw_bytes=len(raw),raw_crc32=zlib.crc32(raw)&0xffffffff,alignment=64))
 dump(RES/'external_assets.manifest.json',manifest);pack=pack_flash.build(manifest,ROOT);(RES/'assets.pack').write_bytes(pack)
 loader='''/* Generated loader sizes; no incbin and no framebuffer aliases. */
#include "assets.h"
#include "external_assets.h"
#include <stddef.h>
'''
 for typ,name,size in [('uint16_t','pixels',len(arena)//2),('uint8_t','masks',len(maskraw)),('TrajPoint','traj',len(trajraw)//8),('EffectPoint','effects',len(effectraw)//8)]:loader+=f'static {typ} decoded_{name}[{size}] __attribute__((aligned(64)));\n'
 loader+='''uint16_t *asset_pixels=decoded_pixels;
const uint8_t *asset_walk_masks=decoded_masks;
const TrajPoint *asset_traj_points=decoded_traj;
const EffectPoint *asset_effect_points=decoded_effects;
_Static_assert(sizeof(TrajPoint)==8,"TrajPoint wire stride");
_Static_assert(sizeof(EffectPoint)==8,"EffectPoint wire stride");
int assets_init(void){
 static int state; int rc; if(state)return state==1?0:state;
 state=-1; rc=external_assets_init();if(rc){state=rc;return rc;}
 rc=external_decode(3,0,decoded_pixels,sizeof decoded_pixels);if(rc){state=rc;return rc;}
 rc=external_decode(3,1,decoded_masks,sizeof decoded_masks);if(rc){state=rc;return rc;}
 rc=external_decode(3,2,decoded_traj,sizeof decoded_traj);if(rc){state=rc;return rc;}
 rc=external_decode(3,3,decoded_effects,sizeof decoded_effects);if(rc){state=rc;return rc;}
 state=1;return 0;
}
int asset_walkable(unsigned island,int x,int y){
 if(island>=18 || (unsigned)x>=1280u || (unsigned)y>=630u)return 0;
 unsigned bit=(unsigned)y*1280u+(unsigned)x;
 return (asset_walk_masks[asset_islands[island].mask_offset+(bit>>3)]>>(bit&7))&1;
}
const NodeAsset *asset_node(unsigned route,unsigned node){
 return route<2 && node<18?&asset_nodes[route][node]:NULL;
}
''';(ROOT/'src/assets.c').write_text(loader,encoding='utf-8')
 # Character contact sheet at actual scale on dark background.
 sheet=Image.new('RGB',(1000,350),(30,45,55));sd=ImageDraw.Draw(sheet)
 for i,(name,im) in enumerate(preview.items()):
  if name.startswith('dodge'):continue
  n=i if name.startswith('idle') else 8+int(name[2:])-1
  if name.startswith('idle'):n=DIRS.index(name[5:])
  x=(n%8)*120+30;y=(n//8)*160+25;sheet.paste(im,(x,y),im);sd.text((x,y+110),name,fill='white')
 sheet.save(RES/'characters_preview.png')
 # Independent structural checks against emitted wire data, not only source objects.
 assert len(maskraw)==18*MASK_BYTES
 for i,island in enumerate(islands):
  x0,y0,x1,y1,cx,cy,offset=island
  decoded=np.unpackbits(np.frombuffer(maskraw[offset:offset+MASK_BYTES],dtype='uint8'),bitorder='little').reshape(630,1280).astype(bool)
  assert decoded[cy,cx] and np.array_equal(component(decoded,(cx,cy)),decoded)
 for route in nodes:
  for count,correct,tier,qid,nexts,ox,oy,card in route:
   for k in range(count):
    node=route.index((count,correct,tier,qid,nexts,ox,oy,card));bit=oy[k]*1280+ox[k];assert maskraw[node*MASK_BYTES+bit//8]&(1<<(bit%8))
 for offset,first,w,h,count,ax,ay in images:
  assert 0<=offset and offset+w*h<=len(arena)//2 and first+count<=len(spans)
  last=(-1,-1)
  for y,x,n,pad in spans[first:first+count]:
   assert 0<=y<h and 0<=x<x+n<=w and (y,x)>last;last=(y,x)
 for first,count,pad in trajs:
  assert 380<=count<=1130
  assert not (struct.unpack_from('<H',trajraw,(first+count-1)*8+6)[0]&0x8000)
  assert struct.unpack_from('<H',trajraw,(first+count)*8+6)[0]&0x8000
 assert all(rgb565(Image.new('RGB',(1,1),(128,128,128))).ravel()==0x8410)
 sources=[{'path':p.relative_to(SRC).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(SRC.rglob('*')) if p.is_file()]
 report={'version':'G2.1','images':len(images),'spans':len(spans),'raw_bytes':[len(a) for a in arrays],'decoded_total_bytes':sum(map(len,arrays)),'pack_bytes':len(pack),'pack_crc32':f'{zlib.crc32(pack)&0xffffffff:08x}','pack_sha256':hashlib.sha256(pack).hexdigest(),'entries':pack_flash.validate(pack),'animations':animation_report,'cg':{'source_read':True,'source_scale':70/761,'derived_scale':70/90,'anchor':[round(312*70/761),round(1029*70/761)]},'masks':mask_report,'mask_stride_bytes':MASK_BYTES,'mask_bit_order':'LSB first global y*1280+x','options':option_report,'source_files':sources,'checks':{'idle_all_70':all(images[x][3]==70 for x in idle),'masks_connected':True,'options_all_in_masks':True,'trajectory_count':len(trajs),'effects_shape':[230,150,8],'crystal_8_snow_16':True,'CRC_pack_roundtrip':True,'no_system_font_copied':True}}
 report['cache_images']=cache_report;report['cards']=card_report;report['checks'].pop('crystal_8_snow_16');report['checks']['cache_three_copy_three_key0']=True;report['version']='G2.5';dump(ROOT/'tests/assets_cache_report.json',report);print(json.dumps({k:report[k] for k in ['images','spans','raw_bytes','decoded_total_bytes','pack_bytes','checks']},ensure_ascii=True))
if __name__=='__main__':main()
