"""One-shot migration from immutable sibling G2.3 header integer microseconds."""
from pathlib import Path
import json,re
root=Path(__file__).resolve().parents[1]
old=root.parent/'G2.3_V8FullActions/resources/player'
h=(old/'fpga_player.h').read_text()
m=json.loads((old/'manifest.json').read_text())
timeline={'schema_version':1,'units':'us','scale_num':2,'scale_den':3,'rounding':'nearest integer: (old_us*2+1)//3','source':'G2.3 fpga_player.h integer timing; no runtime multiplier','fire_anchor_policy':'first selected aimShootWeak frame start (animation transition, not measured projectile release)','groups':{}}
def replace(match):
 name,body=match.groups(); key=name.removeprefix('fpga_'); g=m['groups'][key]; frames=[]
 def row(r):
  values=list(map(int,r.group(1).split(','))); a,b=values[-2:]; na,nb=(a*2+1)//3,(b*2+1)//3; values[-2:]=na,nb
  i=len(frames); mf=g['frames'][i]; frames.append({'source_index':mf['source_index'],'source_start_us':a,'source_end_us':b,'start_us':na,'end_us':nb,'anim':mf['source_frame_metadata'].get('anim')})
  mf.update(start_us=na,end_us=nb,start_s=na/1e6,end_s=nb/1e6)
  return ' {'+','.join(map(str,values))+'},'
 newbody=re.sub(r'\s*\{([\d,\-]+)\},',row,body)
 fire=next(f for f in frames if (f['anim'] or '').startswith('aimShootWeak'))
 duration=frames[-1]['end_us'];g.update(duration_us=duration,duration_s=duration/1e6,timing_basis='G2.4 physical 1.5x; integer G2.3 header times rounded *2/3')
 timeline['groups'][key]={'duration_us':duration,'source_duration_us':frames[-1]['source_end_us'],'scale_num':2,'scale_den':3,'fire_anchor_us':fire['start_us'],'fire_anchor_source_index':fire['source_index'],'frames':frames}
 return f'static const FpgaPlayerFrame {name}_frames[] = {{\n'+newbody+'\n};'
h=re.sub(r'static const FpgaPlayerFrame (fpga_ranged_\w+)_frames\[\] = \{(.*?)\n\};',replace,h,flags=re.S)
assert len(timeline['groups'])==8
(root/'resources/player/fpga_player.h').write_text(h)
(root/'resources/player/manifest.json').write_text(json.dumps(m,ensure_ascii=False,indent=2)+'\n')
(root/'docs/ranged_timeline_1p5.json').write_text(json.dumps(timeline,indent=2)+'\n')
for k,g in timeline['groups'].items():print(k,g['duration_us'],g['fire_anchor_us'])
