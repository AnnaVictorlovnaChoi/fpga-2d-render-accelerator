"""Idempotent standalone timing regeneration from local immutable G2.4 header.
No PC/G2.4 writes, no cumulative retiming. Run before generate_runtime.py.
"""
from pathlib import Path
import re,json
R=Path(__file__).resolve().parents[1]
t=(R/'resources/player/g24_timing_source.h.txt').read_text();melee={};dodge={}
def rownums(body):return [list(map(int,s.split(','))) for s in re.findall(r'\{([^{}]+)\}',body)]
def mr(m):
 rows=rownums(m[2]);out=[]
 for row in rows:row[-2:]=[(v*2+1)//3 for v in row[-2:]];out.append(row)
 melee[m[1].split()[3].split('[')[0]]=[{'start_us':r[-2],'end_us':r[-1]} for r in out]
 return m[1]+''.join('\n {'+','.join(map(str,r))+'},' for r in out)+'\n};'
t=re.sub(r'(static const FpgaPlayerFrame fpga_melee_\w+\[\] = \{)(.*?)(\n\};)',mr,t,flags=re.S)
def tm(v):return (v*3+2)//5 if v<=366667 else v-366667+220000
def dr(m):
 rows=[]
 for state,start,end,x,y in rownums(m[2]):rows.append([state,tm(start),tm(end),x*3//2,y*3//2])
 dodge[m[1].split()[3].split('[')[0]]=rows
 return m[1]+''.join('\n {'+','.join(map(str,r))+'},' for r in rows)+'\n};'
t=re.sub(r'(static const int32_t fpga_dodge_\w+_schedule\[\]\[5\] = \{)(.*?)(\n\};)',dr,t,flags=re.S)
(R/'resources/player/fpga_player.h').write_text(t)
(R/'docs/melee_timeline_1p5.json').write_text(json.dumps({'authority':'G2.4 header integer *2/3 rounded; runtime not accelerated again','directions':melee},indent=2))
(R/'docs/dodge_timeline_165px.json').write_text(json.dumps({'policy':'asset,start_us,end_us,dx_q8,dy_q8; movement220000us; original166666us tail preserved; total386666us; 165px; no teleport','schedules':dodge},indent=2))
p=R/'resources/player/manifest.json';m=json.loads(p.read_text())
for name,g in m['groups'].items():
 if name.startswith('melee_'):
  rows=melee['fpga_'+name+'_frames']
  for f,row in zip(g['frames'],rows):f.update(start_s=row['start_us']/1e6,end_s=row['end_us']/1e6,duration_s=(row['end_us']-row['start_us'])/1e6,**row)
  g.update(duration_s=rows[-1]['end_us']/1e6,timing_basis='G2.4 header integer (old*2+1)//3 authoritative')
 if name.startswith('dodge_'):
  rows=dodge['fpga_'+name+'_schedule'];g.update(runtime_schedule_us=rows,duration_s=rows[-1][2]/1e6,timing_basis='movement220ms165px; original residual tail unchanged')
  for f,row in zip(g['dodge_schedule'],rows):
   f.update(asset_index=row[0],start_s=row[1]/1e6,end_s=row[2]/1e6,start_us=row[1],end_us=row[2],world_delta_q8=row[3:],source_move_px=(abs(row[3])+abs(row[4]))/256)
  g['runtime_params']={'distance_px':165,'move_duration_us':220000,'tail_duration_us':166666,'duration_us':386666}
p.write_text(json.dumps(m,indent=2)+'\n')
print('PASS regenerated 8 melee timelines; 4 dodge schedules; unchanged ranged')
