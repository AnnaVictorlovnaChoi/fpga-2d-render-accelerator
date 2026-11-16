"""Reproducible metadata-only 150->210px/s migration from tested G2.5."""
from pathlib import Path
import json,re
r=Path(__file__).resolve().parents[1]
old=r.parent/'G2.5_V8ArrowBarrage/resources/player'
h=(old/'fpga_player.h').read_text();m=json.loads((old/'manifest.json').read_text())
def group(mt):
 name,body=mt.groups();key=name.removeprefix('fpga_');g=m['groups'][key];idx=0
 def row(x):
  nonlocal idx
  v=list(map(int,x.group(1).split(',')));a,b=v[-2:];a,b=(a*5+3)//7,(b*5+3)//7;v[-2:]=a,b
  g['frames'][idx].update(start_us=a,end_us=b,start_s=a/1e6,end_s=b/1e6);idx+=1
  return '  {'+','.join(map(str,v))+'},'
 body=re.sub(r'\s*\{([\d,\-]+)\},',row,body);g.update(duration_us=571429,duration_s=.571429,timing_basis='G2.6 walk 210px/s; integer G2.5 header times nearest *5/7')
 return f'static const FpgaPlayerFrame {name}_frames[] = {{\n'+body+'\n};'
h=re.sub(r'static const FpgaPlayerFrame (fpga_walk_\w+)_frames\[\] = \{(.*?)\n\};',group,h,flags=re.S)
(r/'resources/player/fpga_player.h').write_text(h)
(r/'resources/player/manifest.json').write_text(json.dumps(m,ensure_ascii=False,indent=2)+'\n')
print('walk metadata 8 directions period=571429us; compressed pixels unchanged')
